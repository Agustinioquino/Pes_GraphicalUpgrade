// (17/11 3icxbh) Motor en tiempo real, etapa 2: jugadores PROPIOS del Motor (no el del visor del Editar).
// Agustin 17/11 04:35: eligio "Pasar a jugadores propios"; 04:35: "la idea es que pases a crear jugadores propios asi como en un futuro
// permitirme importar jugadores por su id"; 04:37: "puedes dar la opcion de cargar el jugador base de konami el que se usa a la hora de
// crear jugadores en el modo editar?". Antes (03:06 / 03:08): el Motor no debe usar el render del Editar para no romperlo.
//
// Cada jugador propio es un FixDemoHuman del juego (el jugador suelto de las cinematicas), creado igual que los suplentes de la banca
// (banca.cpp, paso 2a..2e): FUN_141f85e40 (ctor, heap 0x19), LoadId FUN_141f61520, carga FUN_141f86c30 y sondeo FUN_141f87230; cuando
// cargo: lugar y giro en fd+8 / +0xc / +0x10 / +0x14 (grados), clip por FixDemoHuman::vf1 con el recurso "fixdemo", cara y manos con vf3,
// y en cada cuadro vf4 (avanza el clip), FUN_141f86d40 (manda la pose), FUN_141ec7b10 (cara y manos neutras) y FUN_141ec8320 (mostrar).
// Se suelta con vf0 (heap 0x19). Todo en el hilo del juego: el reloj es el tick del visor (motorjug.cpp hk_Tick, 0x141ee7530), el
// mismo hilo del menu (registro del 17/11 04:28).
//
// De donde sale el jugador (leido en el C, c_141eec360.c / c_14149e2d0.c):
//   - LoadId FUN_141f61520 solo toma de la ficha de 0xF0 B: +0 el id de 8 B, +0x39 estatura (cm, byte), +0x3a (con la estatura ->
//     FUN_141962020 -> LoadId+0xc), +0x86 (u16) y +0x9c el nombre. El modelo (cara, pelo, cuerpo) lo busca el cargador por el id.
//   - Base de datos = [[0x143714240] + 0x48]. Jugadores: registros de 0x17c B desde la base, con su id de 8 B en +0x2c (u16 bajo = id
//     corto, u32 alto; FUN_1414bdbe0 los ordena asi); con [base + 0x183789c] == 2 el registro del id corto N esta en base + N * 0x17c
//     (N <= 0x7531, FUN_1414c0e40); cantidad en base + 0xd0bce8. Nombre: registro + 0x75 si [registro + 0x144] >= 0 (FUN_1414c5db0).
//     Un id que no esta devuelve el registro vacio base + 0x184c3bc.
//   - "Jugador base": id 0xffff (el mismo que trae el LoadId del visor del Editar, impl+0x10). INFERIDO: el cargador lo resuelve con el
//     registro vacio del juego, el de un jugador recien creado; se confirma en la prueba (el registro anota que modelo cargo).
//   - "Igual al del Editar": se copia el LoadId entero del visor (impl+0x10, 0xa0 B).
// El uniforme sale del bloque del lado 0 de la ficha de las cinematicas (DAT_1438041d0): fuera del partido es la del ultimo partido o
// vacia. No se toca la ficha (la usa el juego).
// Tamano: FUN_141afa3c0(ctl, root_main, s) + FUN_141afa690(ctl, s) sobre el controlador del cuerpo (fd+0x50), con s = estatura del
// juego x escala pedida (doc LEEME_arranque.cpp.md, seccion d). Cada clip nuevo crea otro controlador: se vuelve a aplicar.
#include "core.h"
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdarg>

namespace fx {

bool BancaGaniKeep(uint64_t h, bool on);
void BancaFdhPlay(void* fd, void* res, uint32_t idx, uint8_t a4, uint64_t startBits);

namespace {

const uintptr_t kAlloc = 0x1403fb460ull, kHeapPush = 0x1403fb780ull, kHeapPop = 0x1403fb760ull;
const uintptr_t kFdCtor = 0x141f85e40ull, kLoadId = 0x141f61520ull, kFdLoad = 0x141f86c30ull, kFdPoll = 0x141f87230ull;
const uintptr_t kFdPush = 0x141f86d40ull, kFdVf4 = 0x141f87340ull, kFdVf0 = 0x141f863f0ull, kFdVf3 = 0x141f882b0ull, kFaceNeutral = 0x141ec7b10ull;
const uintptr_t kHumanShow = 0x141ec8320ull, kFaceResPtr = 0x143802ff8ull, kHandResPtr = 0x143803000ull;
const uintptr_t kResMgr = 0x141b08cf0ull, kResFind = 0x141b079e0ull, kGaniQueue = 0x141eb16e0ull, kGaniPump = 0x141eb0f50ull, kStrAssign = 0x1404023c0ull;
const uintptr_t kAnimResMgr = 0x141ed1770ull, kAnimResFind = 0x141ed1700ull, kClipIdx = 0x141afa060ull;
const uintptr_t kBoneScale = 0x141afa3c0ull, kRootScale = 0x141afa690ull, kBoneIdxTab = 0x1438032e0ull;
const uintptr_t kDbPtr = 0x143714240ull;
// (17/11 prueba de Agustin 05:13: aparecen pero no se mueven) leido en el C: vf1 / vf2 copian fd+8..+0x14 a la raiz del controlador
// UNA vez (FUN_141afd060(ctl, x, y, z, 0) + FUN_141afccc0(ctl, giro * pi / 180, 0)) y la base para cuando el clip da la vuelta a
// fd+0x18..+0x24 (FUN_141f86d40 la vuelve a poner con fd+0x416). Escribir fd+8 despues no mueve nada: al cambiar el pedido se llaman
// esas dos funciones (como el juego) y se copia tambien la base de la vuelta.
const uintptr_t kRootPos = 0x141afd060ull, kRootYaw = 0x141afccc0ull;
const char* const kClipPath = "cpk_dat/common/demo/anime/FoxAnim/FixDemo/Animations/dml_ent_aisle_cmn_idle01.gani";   // parado esperando (pasillo)

typedef void*(__fastcall* AllocFn)(uint32_t*, size_t, size_t);
typedef uint32_t*(__fastcall* HeapPushFn)(uint32_t*, uint32_t*);
typedef void(__fastcall* HeapPopFn)(uint32_t*);
typedef void*(__fastcall* FdCtorFn)(void*, uint8_t);
typedef void*(__fastcall* LoadIdFn)(void*, void*, int, uint64_t, void*, char, char, char, char, uint8_t);
typedef void(__fastcall* FdLoadFn)(void*, void*, uint32_t, uint8_t, uint8_t);
typedef uint64_t(__fastcall* FdPollFn)(void*);
typedef void(__fastcall* FdPushFn)(void*);
typedef uint32_t(__fastcall* FdVf4Fn)(void*, float);
typedef void*(__fastcall* FdVf0Fn)(void*, uint64_t);
typedef void(__fastcall* FdVf3Fn)(void*, uint8_t, uint32_t, uint64_t);
typedef void(__fastcall* FaceNeutralFn)(void*, void*, uint8_t);
typedef void(__fastcall* HumanShowFn)(void*, uint8_t);
typedef void*(__fastcall* PtrFn)();
typedef void*(__fastcall* ResFindFn)(void*, void*, const char*);
typedef void(__fastcall* GaniQueueFn)(void*, void*);
typedef void(__fastcall* GaniPumpFn)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef void*(__fastcall* StrAssignFn)(void*, const char*, size_t);
typedef void*(__fastcall* AnimResFindFn)(void*, void*);
typedef uint32_t(__fastcall* ClipIdxFn)(void*, const char*);
typedef void(__fastcall* BoneScaleFn)(void*, int, float);
typedef void(__fastcall* RootScaleFn)(void*, float);
typedef void(__fastcall* RootPosFn)(void*, float, float, float, char);
typedef void(__fastcall* RootYawFn)(void*, float, char);
struct GStr { union { char buf[16]; char* ptr; }; size_t size, cap; };

const int kMax = 8;
struct Pj {
    // lo escribe la interfaz (con s_mx)
    int state = 0;                  // 0 libre, 1 pedido, 2 cargando, 3 listo, 4 fallo, 5 sacar
    int kind = 0; uint64_t id = 0; char name[48] = {};
    float pos[3] = {}, yaw = 0.0f, scale = 1.0f;
    // solo el hilo del juego
    uint8_t* fd = nullptr; double t0 = 0, tLoad = 0; bool faceTried = false;
    void* scCtl = nullptr; float scBase = 1.0f, scW = 0.0f; int rootIdx = -1;
    float lastPos[3] = {}, lastYaw = 0.0f; void* posCtl = nullptr; int nMoved = 0;
    uint8_t lid[0xa0] = {};
};
Pj s_pj[kMax];
std::mutex s_mx;
std::atomic<int> s_sel{-1};
std::atomic<bool> s_dead{false};
std::atomic<double> s_lastTick{0.0};
char s_msg[200] = {};               // con s_mx
int s_clipSt = 0; double s_clipT = 0; uint64_t s_clipSlot[2] = {}; uint64_t s_clipHash = 0; bool s_kept = false, s_pumpSaid = false;   // hilo del juego

inline bool PtrOk(uintptr_t a) { return a >= 0x10000ull && a < 0x7ffffffff000ull; }
bool RdQ(uintptr_t a, uint64_t* v) { __try { *v = *(const uint64_t*)a; return true; } __except (1) { return false; } }
void Msg(const char* fmt, ...) {
    char b[200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    { std::lock_guard<std::mutex> lk(s_mx); memcpy(s_msg, b, sizeof s_msg); }
    Log("motor (jugadores propios): %s", b);
}

// --- base de datos de jugadores ---
// devuelve el registro (0x17c B) del id que escribio Agustin, o 0
uintptr_t DbFind(uint32_t want, uint64_t* key) {
    __try {
        const uintptr_t g = *(const uintptr_t*)kDbPtr; if (!PtrOk(g)) return 0;
        const uintptr_t db = *(const uintptr_t*)(g + 0x48); if (!PtrOk(db)) return 0;
        const int mode = *(const int*)(db + 0x183789c); const uint32_t n = *(const uint32_t*)(db + 0xd0bce8);
        if (mode == 2 && want <= 0x7531) {
            const uintptr_t r = db + (uintptr_t)want * 0x17c; const uint64_t k = *(const uint64_t*)(r + 0x2c);
            if ((uint16_t)k == (uint16_t)want) { *key = k; return r; }
        }
        const uint32_t lim = std::min<uint32_t>(n ? n : 0x7532u, 0x10000u);
        for (uint32_t i = 0; i < lim; i++) {
            const uintptr_t r = db + (uintptr_t)i * 0x17c; const uint64_t k = *(const uint64_t*)(r + 0x2c);
            if ((uint32_t)(k >> 32) == want || (want <= 0xffff && (uint16_t)k == (uint16_t)want && (k >> 32) == 0)) { *key = k; return r; }
        }
        return 0;
    } __except (1) { return 0; }
}
void DbName(uintptr_t r, char* out, size_t n) {
    out[0] = 0;
    __try { if (*(const int*)(r + 0x144) >= 0) { const char* s = (const char*)(r + 0x75); size_t i = 0; for (; i + 1 < n && s[i]; i++) out[i] = ((unsigned char)s[i] < 0x20) ? '?' : s[i]; out[i] = 0; } } __except (1) { out[0] = 0; }
}

// --- clip de parado ---
int ClipQuery() {
    __try {
        void* mgr = ((PtrFn)kResMgr)(); if (!mgr) return 0;
        uint64_t out[2] = { 0, 0 }; ((ResFindFn)kResFind)(mgr, out, kClipPath);
        if ((uint8_t)out[1] != 0 && out[0] != 0xb8a0bf169f98ull) { s_clipHash = out[0]; return 1; }
        return 0;
    } __except (1) { return -1; }
}
bool ClipAsk() {
    __try { GStr s; memset(&s, 0, sizeof s); s.cap = 15; ((StrAssignFn)kStrAssign)(&s, kClipPath, strlen(kClipPath)); s_clipSlot[0] = s_clipSlot[1] = 0; ((GaniQueueFn)kGaniQueue)(s_clipSlot, &s); return true; }
    __except (1) { return false; }
}
bool Pump() { __try { ((GaniPumpFn)kGaniPump)(0, 0, 0, 0); return true; } __except (1) { return false; } }
void* FixdemoRes() {
    __try { GStr s; memset(&s, 0, sizeof s); memcpy(s.buf, "fixdemo", 8); s.size = 7; s.cap = 15; void* mgr = ((PtrFn)kAnimResMgr)(); return mgr ? ((AnimResFindFn)kAnimResFind)(mgr, &s) : nullptr; }
    __except (1) { return nullptr; }
}
// 1 listo, 0 todavia, -1 no se puede
int ClipReady(double now) {
    if (s_clipSt == 2) return 1;
    if (s_clipSt == 3) return -1;
    const int q = ClipQuery();
    if (q < 0) { s_clipSt = 3; Msg("error al buscar el clip de parado en el gestor del juego"); return -1; }
    if (q > 0) {
        s_clipSt = 2; s_kept = BancaGaniKeep(s_clipHash, true);
        Log("motor (jugadores propios): clip de parado listo (%s)%s", kClipPath, s_kept ? "; el juego no lo descarga mientras haya jugadores" : "; OJO: sin freno de descarga (gancho de la banca apagado)");
        return 1;
    }
    if (s_clipSt == 0) { if (!ClipAsk()) { s_clipSt = 3; Msg("error al pedir el clip de parado"); return -1; } s_clipSt = 1; s_clipT = now; return 0; }
    if (now - s_clipT > 3.0) { if (!s_pumpSaid) { s_pumpSaid = true; Log("motor (jugadores propios): el clip no carga solo: se llama a la bomba de carga del juego"); } if (!Pump()) { s_clipSt = 3; Msg("error en la bomba de carga de clips"); return -1; } }
    if (now - s_clipT > 20.0) { s_clipSt = 3; Msg("el clip de parado no cargo en 20 s"); return -1; }
    return 0;
}

// --- un jugador ---
bool Create(Pj& p, int slot, const uint8_t* visorLid) {
    __try {
        uint32_t saved = 0, id = 0x19, heap = 0x19;
        ((HeapPushFn)kHeapPush)(&saved, &id);
        void* mem = ((AllocFn)kAlloc)(&heap, 0x578, 0x10);
        uint8_t* fd = mem ? (uint8_t*)((FdCtorFn)kFdCtor)(mem, (uint8_t)(0x60 + slot)) : nullptr;
        if (fd) {
            if (p.kind == 2 && visorLid) memcpy(p.lid, visorLid, sizeof p.lid);
            else {
                uint8_t pl[0xF0] = {}; memcpy(pl, &p.id, 8); pl[8] = 1; pl[0x39] = 180; pl[0x3a] = 75;
                strncpy_s((char*)pl + 0x9c, 61, p.name, _TRUNCATE);
                ((LoadIdFn)kLoadId)(nullptr, p.lid, 0, 0, pl, 0, 0, 0, 0, 0);
            }
            fd[0x490] = 0; fd[0x514] = 0;                   // sin efectos de lluvia / aliento
            ((FdLoadFn)kFdLoad)(fd, p.lid, 0u, 0, 0);
        }
        ((HeapPopFn)kHeapPop)(&saved);
        p.fd = fd; return fd != nullptr;
    } __except (1) { p.fd = nullptr; s_dead.store(true); return false; }
}
int Poll(Pj& p) {
    __try { uint32_t saved = 0, id = 0x19; ((HeapPushFn)kHeapPush)(&saved, &id); const uint64_t r = ((FdPollFn)kFdPoll)(p.fd); ((HeapPopFn)kHeapPop)(&saved); return (r & 0xff) ? 1 : 0; }
    __except (1) { s_dead.store(true); return -1; }
}
bool Destroy(Pj& p) {
    if (!p.fd) return true;
    __try { uint32_t saved = 0, id = 0x19; ((HeapPushFn)kHeapPush)(&saved, &id); ((FdVf0Fn)kFdVf0)(p.fd, 1); ((HeapPopFn)kHeapPop)(&saved); p.fd = nullptr; return true; }
    __except (1) { p.fd = nullptr; s_dead.store(true); return false; }
}
void* FaceEnsure(Pj& p) {
    void* face = nullptr; memcpy(&face, p.fd + 0x58, 8);
    if (face || p.faceTried) return face;
    uint64_t fr = 0, hr = 0; if (!RdQ(kFaceResPtr, &fr) || !fr || !RdQ(kHandResPtr, &hr) || !hr) return nullptr;
    p.faceTried = true;
    uint64_t vt = 0, f3 = 0; if (!RdQ((uintptr_t)p.fd, &vt) || !RdQ(vt + 0x18, &f3) || f3 != kFdVf3) return nullptr;
    uint32_t saved = 0, id = 0x19; ((HeapPushFn)kHeapPush)(&saved, &id); ((FdVf3Fn)f3)(p.fd, 0, 0, 0); ((HeapPopFn)kHeapPop)(&saved);
    memcpy(&face, p.fd + 0x58, 8); return face;
}
void Place(Pj& p, const float* pos, float yaw) { memcpy(p.fd + 8, &pos[0], 4); memcpy(p.fd + 0xc, &pos[1], 4); memcpy(p.fd + 0x10, &pos[2], 4); memcpy(p.fd + 0x14, &yaw, 4);
    memcpy(p.fd + 0x18, &pos[0], 4); memcpy(p.fd + 0x1c, &pos[1], 4); memcpy(p.fd + 0x20, &pos[2], 4); memcpy(p.fd + 0x24, &yaw, 4); }
// lugar y giro a la raiz del controlador del cuerpo, solo cuando cambian (o hay controlador nuevo)
void RootMove(Pj& p, const float* pos, float yaw) {
    void* ctl = nullptr; memcpy(&ctl, p.fd + 0x50, 8); if (!ctl) return;
    if (ctl == p.posCtl && !memcmp(pos, p.lastPos, 12) && yaw == p.lastYaw) return;
    Place(p, pos, yaw);
    ((RootPosFn)kRootPos)(ctl, pos[0], pos[1], pos[2], 0);
    ((RootYawFn)kRootYaw)(ctl, yaw * 0.017453292f, 0);
    const bool first = ctl != p.posCtl;
    p.posCtl = ctl; memcpy(p.lastPos, pos, 12); p.lastYaw = yaw;
    if (!first && p.nMoved++ < 4) Log("motor (jugadores propios): jugador movido a (%.2f, %.2f, %.2f) giro %.1f", pos[0], pos[1], pos[2], yaw);
}
bool Start(Pj& p, void* res, const float* pos, float yaw) {
    __try {
        Place(p, pos, yaw);
        p.fd[0x416] = 1;                                     // al dar la vuelta el clip vuelve a la base
        uint32_t saved = 0, id = 0x19; ((HeapPushFn)kHeapPush)(&saved, &id);
        const uint32_t idx = ((ClipIdxFn)kClipIdx)(res, kClipPath);
        BancaFdhPlay(p.fd, res, idx, 0, 0);
        ((HeapPopFn)kHeapPop)(&saved);
        p.fd[0x43e] = 3;                                     // ropa: reinicio de la tela como la cinematica
        p.faceTried = false; FaceEnsure(p);
        void* h = nullptr; memcpy(&h, p.fd + 0x48, 8); if (h) ((HumanShowFn)kHumanShow)(h, 1);
        void* ctl = nullptr; memcpy(&ctl, p.fd + 0x50, 8);
        p.posCtl = ctl; memcpy(p.lastPos, pos, 12); p.lastYaw = yaw;   // vf1 ya puso este lugar en la raiz
        p.t0 = Now(); p.scCtl = nullptr; p.scW = 0.0f;
        return ctl != nullptr;
    } __except (1) { s_dead.store(true); return false; }
}
void ScaleApply(Pj& p, float want) {
    void* ctl = nullptr; memcpy(&ctl, p.fd + 0x50, 8); if (!ctl) return;
    if (p.rootIdx < 0) { p.rootIdx = *(const int*)(kBoneIdxTab + 0x13 * 4); }
    const uintptr_t arr = *(const uintptr_t*)((uintptr_t)ctl + 0x298); if (!PtrOk(arr) || p.rootIdx < 0 || p.rootIdx > 400) return;
    const float cur = *(const float*)(arr + (size_t)p.rootIdx * 4); if (!(cur > 0.01f && cur < 100.0f)) return;
    if (ctl != p.scCtl || fabsf(cur - p.scW) > 1e-5f) { p.scCtl = ctl; p.scBase = cur; p.scW = cur; }      // controlador nuevo o el juego volvio a poner la estatura
    const float s = p.scBase * want;
    if (fabsf(s - p.scW) > 1e-5f) { ((BoneScaleFn)kBoneScale)(ctl, p.rootIdx, s); ((RootScaleFn)kRootScale)(ctl, s); p.scW = s;
        static int nl = 0; if (nl++ < 6) Log("motor (jugadores propios): tamano %.2f (estatura del juego %.4f, raiz %d) escrito", want, p.scBase, p.rootIdx); }
}
int Update(Pj& p, const float* pos, float yaw, float scale) {
    __try {
        RootMove(p, pos, yaw);
        ((FdVf4Fn)kFdVf4)(p.fd, (float)(Now() - p.t0));
        ScaleApply(p, scale);
        ((FdPushFn)kFdPush)(p.fd);
        void* h = nullptr; memcpy(&h, p.fd + 0x48, 8);
        void* face = FaceEnsure(p);
        if (h && face) ((FaceNeutralFn)kFaceNeutral)(h, face, 0);
        if (h && *((const uint8_t*)h + 0x5a) == 0) ((HumanShowFn)kHumanShow)(h, 1);
        return 1;
    } __except (1) { s_dead.store(true); return -1; }
}

bool ReadLid(uintptr_t impl, uint8_t* out) { __try { memcpy(out, (const void*)(impl + 0x10), 0xa0); return true; } __except (1) { return false; } }
void DestroyAll(const char* why) {
    int n = 0;
    for (int i = 0; i < kMax; i++) { Pj& p = s_pj[i]; if (p.fd && Destroy(p)) n++; }
    { std::lock_guard<std::mutex> lk(s_mx); for (int i = 0; i < kMax; i++) if (s_pj[i].state) s_pj[i].state = 0; }
    s_sel.store(-1);
    if (s_kept) { BancaGaniKeep(s_clipHash, false); s_kept = false; }
    s_clipSt = 0; s_pumpSaid = false;
    if (n) Log("motor (jugadores propios): %d sacados (%s)", n, why);
}

} // namespace

// hilo del juego: tick del visor (motorjug.cpp hk_Tick), siempre (con el Motor abierto o no)
void MotorPjTick(bool on, uintptr_t impl) {
    const double now = Now(); s_lastTick.store(now);
    static bool s_had = false;
    if (!on || s_dead.load()) {
        bool any = false; for (int i = 0; i < kMax; i++) if (s_pj[i].fd) any = true;
        if (any || s_had) DestroyAll(on ? "error dentro del juego" : "se cerro el Motor");
        s_had = false; return;
    }
    // copia de lo que pidio la interfaz
    Pj want[kMax]; { std::lock_guard<std::mutex> lk(s_mx); for (int i = 0; i < kMax; i++) { want[i].state = s_pj[i].state; want[i].kind = s_pj[i].kind; want[i].id = s_pj[i].id; memcpy(want[i].name, s_pj[i].name, sizeof want[i].name); memcpy(want[i].pos, s_pj[i].pos, 12); want[i].yaw = s_pj[i].yaw; want[i].scale = s_pj[i].scale; } }
    bool any = false;
    for (int i = 0; i < kMax; i++) {
        Pj& p = s_pj[i]; const int st = want[i].state;
        if (st == 5) {            // sacar
            const bool ok = Destroy(p);
            { std::lock_guard<std::mutex> lk(s_mx); p.state = 0; }
            Log("motor (jugadores propios): jugador %d sacado%s", i + 1, ok ? "" : " (con error)");
            if (s_sel.load() == i) s_sel.store(-1);
            continue;
        }
        if (st == 1) {            // crear
            const int c = ClipReady(now); if (c == 0) { any = true; continue; }
            void* res = FixdemoRes();
            if (c < 0 || !res) { if (!res) Msg("no esta el recurso de clips \"fixdemo\" del juego"); std::lock_guard<std::mutex> lk(s_mx); p.state = 4; continue; }
            uint8_t vl[0xa0] = {}; bool vok = false;
            if (want[i].kind == 2) vok = impl && ReadLid(impl, vl);
            if (want[i].kind == 2 && !vok) { Msg("no pude leer el jugador del visor del Editar"); std::lock_guard<std::mutex> lk(s_mx); p.state = 4; continue; }
            p.kind = want[i].kind; p.id = want[i].id; memcpy(p.name, want[i].name, sizeof p.name);
            if (!Create(p, i, vok ? vl : nullptr)) { Msg("el juego no pudo crear al jugador %d", i + 1); std::lock_guard<std::mutex> lk(s_mx); p.state = 4; continue; }
            char hex[0x20 * 3 + 1] = {}; for (int b = 0; b < 0x20; b++) snprintf(hex + b * 3, 4, "%02x ", p.lid[b]);
            Log("motor (jugadores propios): creando el jugador %d: %s (%s, id %llu = 0x%llx); LoadId: %s ... +0x4c %02x %02x %02x %02x, +0x94 %02x %02x %02x %02x",
                i + 1, p.name, p.kind == 0 ? "por id" : p.kind == 1 ? "jugador base" : "igual al del Editar", (unsigned long long)p.id, (unsigned long long)p.id, hex,
                p.lid[0x4c], p.lid[0x4d], p.lid[0x4e], p.lid[0x4f], p.lid[0x94], p.lid[0x95], p.lid[0x96], p.lid[0x97]);
            p.tLoad = now;
            { std::lock_guard<std::mutex> lk(s_mx); p.state = 2; }
            any = true; continue;
        }
        if (st == 2 && p.fd) {    // cargando
            const int r = Poll(p);
            if (r < 0) { std::lock_guard<std::mutex> lk(s_mx); p.state = 4; continue; }
            if (!r) { if (now - p.tLoad > 30.0) { Destroy(p); Msg("el jugador %d no cargo en 30 s", i + 1); std::lock_guard<std::mutex> lk(s_mx); p.state = 4; } any = true; continue; }
            void* res = FixdemoRes();
            if (!res || !Start(p, res, want[i].pos, want[i].yaw)) { Destroy(p); Msg("el jugador %d cargo pero no se le pudo poner el clip", i + 1); std::lock_guard<std::mutex> lk(s_mx); p.state = 4; continue; }
            void* h = nullptr; memcpy(&h, p.fd + 0x48, 8); float hc = 0.0f; if (h) memcpy(&hc, (const uint8_t*)h + 0x10, 4);
            Log("motor (jugadores propios): jugador %d listo en %.1f s (modelo %p, estatura del modelo %.1f cm)", i + 1, now - p.tLoad, h, hc);
            { std::lock_guard<std::mutex> lk(s_mx); p.state = 3; snprintf(s_msg, sizeof s_msg, "%s", ""); }
            any = true; continue;
        }
        if (st == 3 && p.fd) {
            if (Update(p, want[i].pos, want[i].yaw, want[i].scale) < 0) { Msg("error al mover al jugador %d: se sacan todos", i + 1); break; }
            any = true;
        }
    }
    s_had = any;
}

// interfaz (Present)
int MotorPjAdd(int kind, uint32_t id) {
    char nm[48] = {}; uint64_t key = 0;
    if (kind == 0) {
        const uintptr_t r = DbFind(id, &key);
        if (!r) { Msg("no encontre el id %u en la base de datos del juego", id); return -1; }
        DbName(r, nm, sizeof nm); if (!nm[0]) snprintf(nm, sizeof nm, "Jugador %u", id);
    } else if (kind == 1) { key = 0xffffull; snprintf(nm, sizeof nm, "Jugador base"); }
    else snprintf(nm, sizeof nm, "Igual al del Editar");
    std::lock_guard<std::mutex> lk(s_mx);
    int slot = -1, used = 0; for (int i = 0; i < kMax; i++) { if (s_pj[i].state == 0 || s_pj[i].state == 4) { if (slot < 0) slot = i; } else used++; }
    if (slot < 0) { snprintf(s_msg, sizeof s_msg, "Ya hay %d jugadores (el maximo)", kMax); return -1; }
    Pj& p = s_pj[slot]; p.state = 1; p.kind = kind; p.id = key; memcpy(p.name, nm, sizeof p.name);
    p.pos[0] = 1.2f * (float)(used + 1); p.pos[1] = 0.0f; p.pos[2] = 0.0f; p.yaw = 0.0f; p.scale = 1.0f;
    snprintf(s_msg, sizeof s_msg, "Cargando %s...", nm);
    s_sel.store(slot);
    Log("motor (jugadores propios): pedido %s (id %u, clave 0x%llx) en el lugar %d", nm, id, (unsigned long long)key, slot + 1);
    return slot;
}
void MotorPjRemove(int i) { if (i < 0 || i >= kMax) return; std::lock_guard<std::mutex> lk(s_mx); if (s_pj[i].state == 1 || s_pj[i].state == 4) s_pj[i].state = 0; else if (s_pj[i].state) s_pj[i].state = 5; if (s_sel.load() == i) s_sel.store(-1); }
void MotorPjSelect(int i) { s_sel.store(i >= 0 && i < kMax ? i : -1); }
int MotorPjSel() { const int i = s_sel.load(); if (i < 0) return -1; std::lock_guard<std::mutex> lk(s_mx); const int st = s_pj[i].state; return (st >= 1 && st <= 3) ? i : -1; }
bool MotorPjGet(int i, float* pos, float* yaw, float* scale) {
    if (i < 0 || i >= kMax) return false; std::lock_guard<std::mutex> lk(s_mx); const Pj& p = s_pj[i]; if (p.state < 1 || p.state > 3) return false;
    memcpy(pos, p.pos, 12); *yaw = p.yaw; *scale = p.scale; return true;
}
void MotorPjSet(int i, const float* pos, float yaw, float scale) {
    if (i < 0 || i >= kMax) return; std::lock_guard<std::mutex> lk(s_mx); Pj& p = s_pj[i]; if (p.state < 1 || p.state > 3) return;
    for (int k = 0; k < 3; k++) p.pos[k] = std::clamp(pos[k], -200.0f, 200.0f); p.yaw = std::clamp(yaw, -720.0f, 720.0f); p.scale = std::clamp(scale, 0.05f, 10.0f);
}
void MotorPjInfoGet(MotorPjInfo* o) {
    memset(o, 0, sizeof *o); o->sel = s_sel.load();
    const double lt = s_lastTick.load(); o->ticking = lt > 0.0 && Now() - lt < 1.5;
    std::lock_guard<std::mutex> lk(s_mx);
    for (int i = 0; i < kMax; i++) { const Pj& p = s_pj[i]; o->st[i] = p.state; o->id[i] = p.id; o->kind[i] = p.kind; memcpy(o->name[i], p.name, sizeof o->name[i]); }
    memcpy(o->msg, s_msg, sizeof o->msg); o->dead = s_dead.load();
}

} // namespace fx
