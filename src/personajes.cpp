// PES21 FX - personajes del motor (24/10): que jugador es cada esqueleto. Ingenieria inversa de PES2021.exe 1.07.02
// (sello 62787C6A). La cadena, del lado del juego al lado del render:
//  - draw::load::HumanList (vtable 0x142b98c00, COL 0x142e083e8) [14] = 0x141ecd100 "crear personaje" (this, HumanData* e):
//    e+8 = clave del jugador (u64: los 32 bits altos son el id del jugador con banderas; con & 0x800bffff es el mismo id de la
//    carpeta face\real\<id>, 0x141ecc5c0 lo formatea con "%d"), e+0x54 = tipo de personaje. Al volver, e+0xa8 = el
//    CharacterImpl recien creado (0x1419e1240; cliente de la escena) y CharacterImpl+8 = handle del nodo de la escena.
//  - El comando 0xb de la escena (0x1419c9700) crea el SceneCharacterNode con ese handle: nodo+0x10 (0x1419c4bd0).
//  - SceneCharacterNode (vtable 0x142aab948, COL 0x142da4618): [3] = 0x141a232b0 arma el rig (0x141a26260): el esqueleto
//    principal queda en nodo+0x88 y cada restriccion de musculo guarda ese mismo esqueleto en +0x10 (Skel_AddConstraint
//    0x1419f5eb0). [0] = 0x141a22f50 = destructor.
//  - Nombre: PlayerDB_Find(*(*(0x143714240)+0x48), clave) 0x1414c0e40 -> registro de 0x17c bytes (clave en +0x2c: u16
//    lugar, u32 id en +0x30); PlayerRec_GetName 0x1414c5d70 (nombre UTF-8 en registro+0x38).
// Solo cambia entradas de vtables (sin tocar codigo) si coinciden el sello del exe, el COL y la funcion original.
#include "core.h"
#include <atomic>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fx {

static const unsigned kStamp = 0x62787C6A;
static const uintptr_t kHumanListVt = 0x142b98c00ull, kHumanListCol = 0x142e083e8ull, kHumanCreate = 0x141ecd100ull;
static const uintptr_t kNodeVt = 0x142aab948ull, kNodeCol = 0x142da4618ull, kNodeDtor = 0x141a22f50ull, kNodeBuild = 0x141a232b0ull;
static const uintptr_t kPlayerDbGlobal = 0x143714240ull, kDbFind = 0x1414c0e40ull, kRecName = 0x1414c5d70ull;
static const unsigned char kDbFindSig[] = { 0x48, 0x89, 0x54, 0x24, 0x10, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xc2, 0x48, 0x8b, 0xd9 };
static const unsigned char kRecNameSig[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x81, 0x44, 0x01, 0x00, 0x00, 0x48, 0x8b, 0xd9, 0xc1 };

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static bool SigAt(uintptr_t a, const unsigned char* sig, size_t n) { return Readable((const void*)a, n) && memcmp((const void*)a, sig, n) == 0; }
static bool SafeCopy(void* dst, const void* src, size_t n) {
    __try { memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---------------------------------------------------------------- lo que anota HumanList al crear (handle -> jugador)
struct Pending { uint32_t id = 0; uint64_t key = 0; int type = -1; char name[64] = {}; };
static std::mutex s_mx;
static std::unordered_map<uint64_t, Pending> s_byHandle;
static std::deque<uint64_t> s_handleOrder;

// ---------------------------------------------------------------- nodos armados (lugar -> personaje)
struct Slot { bool used = false; CharInfo info; };
static Slot s_slots[kCharSlots];
static std::atomic<unsigned> s_gen{1};
static unsigned s_seq = 0;

// esqueleto -> lugar, sin candado para los Eval: puntero (48 bits) | (lugar + 1) << 48. Se buscan siempre las 8 casillas
// a partir del hash (sin cortar en una vacia), asi borrar es poner 0.
static const int kSkH = 256;
static std::atomic<uint64_t> s_skel[kSkH];
static inline unsigned SkHash(const void* p) { uint64_t x = (uint64_t)(uintptr_t)p; x ^= x >> 21; x *= 0x9E3779B97F4A7C15ull; return (unsigned)(x >> 56); }
static const uint64_t kPtrMask = 0xFFFFFFFFFFFFull;
int CharSlotForSkel(const void* skel) {
    if (!skel) return -1;
    const uint64_t want = (uint64_t)(uintptr_t)skel & kPtrMask;
    const unsigned h = SkHash(skel);
    for (int i = 0; i < 8; i++) {
        uint64_t v = s_skel[(h + i) & (kSkH - 1)].load(std::memory_order_acquire);
        if (v && (v & kPtrMask) == want) return (int)(v >> 48) - 1;
    }
    return -1;
}
static void SkelPut(const void* skel, int slot) {   // con s_mx tomado
    const uint64_t want = (uint64_t)(uintptr_t)skel & kPtrMask, val = want | ((uint64_t)(slot + 1) << 48);
    const unsigned h = SkHash(skel);
    int freeI = -1;
    for (int i = 0; i < 8; i++) {
        int j = (h + i) & (kSkH - 1);
        uint64_t v = s_skel[j].load();
        if (v && (v & kPtrMask) == want) { s_skel[j].store(val, std::memory_order_release); return; }
        if (!v && freeI < 0) freeI = j;
    }
    if (freeI < 0) freeI = (int)(h & (kSkH - 1));   // las 8 llenas (no deberia pasar con < 64 nodos): se pisa la primera
    s_skel[freeI].store(val, std::memory_order_release);
}
static void SkelDel(const void* skel) {   // con s_mx tomado
    if (!skel) return;
    const uint64_t want = (uint64_t)(uintptr_t)skel & kPtrMask;
    const unsigned h = SkHash(skel);
    for (int i = 0; i < 8; i++) {
        int j = (h + i) & (kSkH - 1);
        uint64_t v = s_skel[j].load();
        if (v && (v & kPtrMask) == want) s_skel[j].store(0, std::memory_order_release);
    }
}

// ---------------------------------------------------------------- nombre del jugador (base de datos del juego)
typedef void* (__fastcall* DbFindFn)(void* db, uint64_t key);
typedef const char* (__fastcall* RecNameFn)(void* rec);
static bool s_nameOk = false;
static bool PlayerName(uint64_t key, char* out, size_t n) {
    out[0] = 0;
    if (!s_nameOk) return false;
    __try {
        void* admin = *(void* volatile*)kPlayerDbGlobal;
        if (!admin) return false;
        void* db = *(void* volatile*)((char*)admin + 0x48);
        if (!db) return false;
        void* rec = ((DbFindFn)kDbFind)(db, key);
        if (!rec || *(volatile uint32_t*)((char*)rec + 0x30) != (uint32_t)(key >> 32)) return false;   // registro por defecto = no esta
        const char* nm = ((RecNameFn)kRecName)(rec);
        if (!nm) return false;
        size_t i = 0;
        for (; i + 1 < n && nm[i]; i++) out[i] = nm[i];
        out[i] = 0;
        return i > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return false; }
}

// ---------------------------------------------------------------- ganchos
// los ganchos pasan los 4 registros de argumentos tal cual (por si la funcion usara mas de los que se ven) y devuelven rax
typedef void* (__fastcall* CreateFn)(void* self, void* entry, void* a3, void* a4);
typedef void* (__fastcall* BuildFn)(void* node, void* a2, void* a3, void* a4);
typedef void* (__fastcall* DtorFn)(void* node, void* flags, void* a3, void* a4);
static CreateFn o_create = nullptr; static BuildFn o_build = nullptr; static DtorFn o_dtor = nullptr;
static std::atomic<unsigned> s_nCreate{0}, s_nBuild{0}, s_nBuildKnown{0}, s_nDtor{0}, s_nNoName{0};
static bool s_tried = false, s_okCreate = false, s_okNode = false;

// registro de lo que paso (se escribe en el log desde Present: los ganchos corren en otros hilos)
static std::mutex s_logMx; static std::vector<std::string> s_logQ;
static void Note(const char* fmt, ...) {
    char b[320]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    std::lock_guard<std::mutex> lk(s_logMx);
    if (s_logQ.size() < 256) s_logQ.push_back(b);
}

static void NoteCreate(void* e, void* before);
static void* __fastcall hk_Create(void* self, void* e, void* a3, void* a4) {
    void* before = nullptr;
    if (e) SafeCopy(&before, (char*)e + 0xa8, sizeof before);
    void* r = o_create(self, e, a3, a4);
    s_nCreate.fetch_add(1, std::memory_order_relaxed);
    if (e) NoteCreate(e, before);
    return r;
}
static void NoteCreate(void* e, void* before) {
    Pending p; void* impl = nullptr; uint64_t handle = 0; unsigned char type = 0xff;
    if (!SafeCopy(&p.key, (char*)e + 8, 8) || !SafeCopy(&type, (char*)e + 0x54, 1) || !SafeCopy(&impl, (char*)e + 0xa8, sizeof impl)) return;
    if (!impl || impl == before || !SafeCopy(&handle, (char*)impl + 8, 8) || !handle) return;
    p.id = (uint32_t)(p.key >> 32); p.type = type;
    if (!PlayerName(p.key, p.name, sizeof p.name)) s_nNoName.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(s_mx);
        if (!s_byHandle.count(handle)) s_handleOrder.push_back(handle);
        s_byHandle[handle] = p;
        while (s_handleOrder.size() > 1024) { s_byHandle.erase(s_handleOrder.front()); s_handleOrder.pop_front(); }
    }
    static std::atomic<int> s_n{0};
    if (s_n.fetch_add(1) < 80)
        Note("personajes: creado id %u (0x%08X, clave %016llX, tipo %d), handle %llX, nombre \"%s\"", p.id & 0x800bffffu, p.id,
             (unsigned long long)p.key, p.type, (unsigned long long)handle, p.name);
}

static void NoteBuild(void* node);
static void* __fastcall hk_Build(void* node, void* a2, void* a3, void* a4) {
    void* r = o_build(node, a2, a3, a4);
    s_nBuild.fetch_add(1, std::memory_order_relaxed);
    if (node) NoteBuild(node);
    return r;
}
static void NoteBuild(void* node) {
    uint64_t handle = 0; void* skel = nullptr;
    if (!SafeCopy(&handle, (char*)node + 0x10, 8) || !SafeCopy(&skel, (char*)node + 0x88, sizeof skel) || !skel) return;
    int slot = -1; bool known = false; CharInfo ci;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        for (int i = 0; i < kCharSlots && slot < 0; i++) if (s_slots[i].used && s_slots[i].info.node == node) slot = i;
        if (slot < 0) for (int i = 0; i < kCharSlots && slot < 0; i++) if (!s_slots[i].used) slot = i;
        if (slot < 0) {   // tabla llena (no deberia pasar): se reemplaza el mas viejo
            unsigned best = ~0u; for (int i = 0; i < kCharSlots; i++) if (s_slots[i].info.seq < best) { best = s_slots[i].info.seq; slot = i; }
        }
        Slot& s = s_slots[slot];
        if (s.used && s.info.skel && s.info.skel != skel) SkelDel(s.info.skel);
        s = Slot(); s.used = true;
        s.info.node = node; s.info.skel = skel; s.info.handle = handle; s.info.seq = ++s_seq; s.info.t = Now();
        auto it = s_byHandle.find(handle);
        if (it != s_byHandle.end()) {
            known = true;
            s.info.known = true; s.info.key = it->second.key; s.info.rawId = it->second.id; s.info.id = it->second.id & 0x800bffffu;
            s.info.type = it->second.type; memcpy(s.info.name, it->second.name, sizeof s.info.name);
        }
        SkelPut(skel, slot);
        ci = s.info;
        s_gen.fetch_add(1);
    }
    if (known) s_nBuildKnown.fetch_add(1, std::memory_order_relaxed);
    static std::atomic<int> s_n{0};
    if (s_n.fetch_add(1) < 80) {
        if (known) Note("personajes: armado (lugar %d) handle %llX esqueleto %p -> id %u tipo %d \"%s\"", slot, (unsigned long long)handle, skel, ci.id, ci.type, ci.name);
        else Note("personajes: armado (lugar %d) handle %llX esqueleto %p -> sin datos del jugador (no paso por HumanList)", slot, (unsigned long long)handle, skel);
    }
}

static void* __fastcall hk_Dtor(void* node, void* flags, void* a3, void* a4) {
    s_nDtor.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(s_mx);
        uint64_t handle = 0;
        if (node && SafeCopy(&handle, (char*)node + 0x10, 8)) {
            auto it = s_byHandle.find(handle);
            if (it != s_byHandle.end()) s_byHandle.erase(it);   // (la cola de orden se limpia sola al pasar de 1024)
        }
        for (int i = 0; i < kCharSlots; i++) {
            if (!s_slots[i].used || s_slots[i].info.node != node) continue;
            SkelDel(s_slots[i].info.skel);
            s_slots[i] = Slot();
            s_gen.fetch_add(1);
        }
    }
    return o_dtor(node, flags, a3, a4);
}

static bool PatchVt(uintptr_t vt, uintptr_t col, int i, uintptr_t expect, void* hook, void** orig) {
    void** v = (void**)vt;
    if (!Readable(v - 1, (i + 2) * sizeof(void*)) || (uintptr_t)v[-1] != col || (uintptr_t)v[i] != expect) return false;
    DWORD old = 0;
    if (!VirtualProtect(&v[i], sizeof(void*), PAGE_READWRITE, &old)) return false;
    *orig = v[i];
    InterlockedExchangePointer(&v[i], hook);
    VirtualProtect(&v[i], sizeof(void*), old, &old);
    return true;
}

void CharTrackInstall() {
    if (s_tried) return;
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("personajes: parches en memoria apagados (motor = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("personajes: exe distinto, sin ganchos"); return; }
    s_nameOk = SigAt(kDbFind, kDbFindSig, sizeof kDbFindSig) && SigAt(kRecName, kRecNameSig, sizeof kRecNameSig) && Readable((void*)kPlayerDbGlobal, 8);
    s_okCreate = PatchVt(kHumanListVt, kHumanListCol, 14, kHumanCreate, (void*)&hk_Create, (void**)&o_create);
    // primero el destructor: si no se puede, no se arma la tabla (quedarian nodos muertos en ella)
    bool d = PatchVt(kNodeVt, kNodeCol, 0, kNodeDtor, (void*)&hk_Dtor, (void**)&o_dtor);
    s_okNode = d && PatchVt(kNodeVt, kNodeCol, 3, kNodeBuild, (void*)&hk_Build, (void**)&o_build);
    Log("personajes: HumanList::crear %s, SceneCharacterNode armar/destruir %s, nombres %s",
        s_okCreate ? "enganchado" : "NO (vtable distinta)", s_okNode ? "enganchados" : (d ? "NO (armar)" : "NO (destructor)"),
        s_nameOk ? "si" : "no (firma distinta)");
}

bool CharTrackOk() { return s_okNode; }
unsigned CharTableGen() { return s_gen.load(); }
bool CharGet(int slot, CharInfo& out) {
    if (slot < 0 || slot >= kCharSlots) return false;
    std::lock_guard<std::mutex> lk(s_mx);
    if (!s_slots[slot].used) return false;
    out = s_slots[slot].info; return true;
}
int CharSnapshot(CharInfo out[kCharSlots], bool alive[kCharSlots]) {
    std::lock_guard<std::mutex> lk(s_mx);
    int n = 0;
    for (int i = 0; i < kCharSlots; i++) { alive[i] = s_slots[i].used; if (alive[i]) { out[i] = s_slots[i].info; n++; } }
    return n;
}

void CharTrackPresent() {
    if (!s_tried) CharTrackInstall();
    {   // un nodo armado antes de que HumanList anotara su handle (carrera entre hilos): se completa aca
        std::lock_guard<std::mutex> lk(s_mx);
        for (auto& s : s_slots) {
            if (!s.used || s.info.known) continue;
            auto it = s_byHandle.find(s.info.handle);
            if (it == s_byHandle.end()) continue;
            s.info.known = true; s.info.key = it->second.key; s.info.rawId = it->second.id; s.info.id = it->second.id & 0x800bffffu;
            s.info.type = it->second.type; memcpy(s.info.name, it->second.name, sizeof s.info.name);
            s_gen.fetch_add(1);
        }
    }
    {
        std::vector<std::string> q;
        { std::lock_guard<std::mutex> lk(s_logMx); q.swap(s_logQ); }
        for (auto& l : q) Log("%s", l.c_str());
    }
    static double s_t = 0;
    double t = Now();
    if (s_t == 0) { s_t = t; return; }
    if (t - s_t < 30.0) return;
    s_t = t;
    if (!s_okCreate && !s_okNode) return;
    int alive = 0, known = 0;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        for (auto& s : s_slots) if (s.used) { alive++; if (s.info.known) known++; }
    }
    static unsigned s_prevC = 0, s_prevB = 0;
    unsigned c = s_nCreate.load(), b = s_nBuild.load();
    if (c != s_prevC || b != s_prevB || alive)
        Log("personajes (30 s): %d nodos vivos (%d con jugador), creados %u (sin nombre %u), armados %u (con jugador %u), destruidos %u",
            alive, known, c, s_nNoName.load(), b, s_nBuildKnown.load(), s_nDtor.load());
    s_prevC = c; s_prevB = b;
}

} // namespace fx
