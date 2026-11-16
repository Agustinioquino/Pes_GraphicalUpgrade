// PES21 FX (16/11, hilo "Poner jugadores reales en la banca") - BANCO DE SUPLENTES, PASO 1.
//
// Pedido de Agustin: en el banco tienen que estar los suplentes REALES de cada equipo (cara, cuerpo, dorsal) con la pechera de
// suplente, en todos los estadios, en el juego y en las cinematicas, en lugar de los munecos de Konami; la cantidad = los
// convocados de ese partido. Este paso NO crea jugadores todavia: hace lo que se puede hacer seguro y junta en vivo lo que falta
// saber para el paso 2. Detalle y C reconstruido: /mnt/project-files/banca_suplentes/20261116_como_arma_el_banco.cpp.md.
//
// Lo que hace (todo con la fila 516 prendida; de fabrica APAGADA):
//  1) Reloj del juego: engancha FixDemoMain::vf3 (0x141f37770), la actualizacion por cuadro del sistema de cinematicas (es el
//     mismo lugar donde el juego mueve a sus jugadores sueltos de las cinematicas, FUN_141f377e0 -> FUN_141f86d40). Todo lo de
//     abajo corre ahi, en el hilo del juego. El registro dice cuantas veces por segundo corre en el partido (NO SE todavia si corre
//     fuera de las cinematicas: si da 0, el paso 2 usa otro reloj).
//  2) Convocados: lee la ficha del partido de las cinematicas (puntero 0x1438041d0, objeto 0x52e0 B; FUN_141f58cb0 NO se llama
//     porque crea la ficha si no existe). Por lado: base = ficha + lado * 0x2830 + 0xf8; 40 lugares de 0x100 B (lugar + 8 = datos
//     del jugador, + 8 = id de 8 B, + 8 + 0x9c = nombre); base + 0x2818 = titulares (11), base + 0x2819 = convocados (23 de
//     fabrica, FUN_141f59960; lo pone FUN_141f59f20 desde el equipo + 0x2eec). Suplentes = lugares titulares .. convocados - 1
//     (FUN_141f5b6e0 los llena en el orden de la formacion del equipo). Al cambiar, los anota en PES21FX.log.
//  3) Esconde el banco de Konami: es UN solo modelo quieto por estadio (entidad "stNNN_sc2020_dir_bench", demoGroup BENCH,
//     medido en vivo el 16/11 con PES21FX_medir_banca.lua). Se busca en la lista de escenas (0x14374d958, igual que el medidor)
//     y se apaga con StadiumModelBody::SetVisible (0x1419055f0, cuerpo + 0x81), la funcion del juego.
//     Antes de cada cambio se vuelve a comprobar toda la cadena (tablas de funciones y nombre). Se guarda su caja: ahi van a ir
//     los asientos del paso 2. Al apagar la fila se vuelve a mostrar.
//  4) Sonda de cinematicas: engancha FixDemoHuman::vf1 (0x141f87e00), donde una cinematica le da su clip a un jugador suelto,
//     y anota el recurso del clip (puntero, +0x20, +0x28) y el jugador (LoadId +0x370). Con eso el paso 2 sabe como pedir y
//     reusar el clip de sentado (dml_md_lockerSpeech_001_sit_00N) para los suplentes.
//
// Fila 516 (ini / menu "Banco de suplentes"): x on / off, y esconder el banco de Konami (1 = si), z anotar cinematicas (1 = si),
// w libre (paso 2: tope de suplentes por equipo, 0 = todos los convocados).
#include "core.h"
#include <MinHook.h>
#include <atomic>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace fx {

namespace {
const unsigned kStamp = 0x62787C6A;
const int kRow = 516;

// FixDemoMain::vf3: mov [rsp+8],rbx; push rdi; sub rsp,20h; mov rbx,rcx; xor dil,dil; mov rcx,[rcx+0C98h]
const uintptr_t kFdmVf3 = 0x141f37770ull;
const unsigned char kFdmVf3Sig[23] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x40,0x32,0xff,0x48,0x8b,0x89,0x98,0x0c,0x00,0x00 };
// FixDemoHuman::vf1: mov [rsp+8],rbx; mov [rsp+10h],rbp; mov [rsp+18h],rsi; mov [rsp+20h],rdi; push r14; sub rsp,..
const uintptr_t kFdhVf1 = 0x141f87e00ull;
const unsigned char kFdhVf1Sig[22] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56 };
// StadiumModelBody::SetVisible: cmp [rcx+81h],dl; je +0b; mov [rcx+81h],dl; jmp 0x141906600; ret
const uintptr_t kSetVisible = 0x1419055f0ull;
const unsigned char kSetVisibleSig[20] = { 0x38,0x91,0x81,0x00,0x00,0x00,0x74,0x0b,0x88,0x91,0x81,0x00,0x00,0x00,0xe9,0xfd,0x0f,0x00,0x00,0xc3 };

// FUN_141906600 = StadiumModelBody "aplicar" (la llaman vf6 y SetVisible): visible = +0x80 (del nodo) Y +0x81; lo pasa al objeto
// de dibujo [+0x88] (+0x1b4 bit 0 = escondido). Prologo: mov rax,rsp; push rbp; push r12; push r13; push r14; push r15; lea rbp,[rax-1d8h]
const uintptr_t kApply = 0x141906600ull;
const unsigned char kApplySig[19] = { 0x48,0x8b,0xc4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0x28,0xfe,0xff,0xff };

const uintptr_t kSheetPtr = 0x1438041d0ull;      // ficha del partido de las cinematicas (DAT_1438041d0)
const uintptr_t kScenes = 0x14374d958ull;        // primera escena (lista circular por +0)
const uintptr_t kVtModel = 0x142a0a880ull, kVtFunctor = 0x142a72358ull, kVtBody = 0x142a72268ull, kVtStadiumModel = 0x142a721e8ull;
const uintptr_t kExeLo = 0x140000000ull, kExeHi = 0x143a00000ull;

typedef void(__fastcall* FdmVf3)(void*);
typedef void(__fastcall* FdhVf1)(void*, void*, uint32_t, uint8_t, uint64_t, void*);
typedef void(__fastcall* SetVisibleFn)(void*, char);
typedef void(__fastcall* ApplyFn)(void*);
FdmVf3 o_fdm = nullptr; FdhVf1 o_fdh = nullptr; SetVisibleFn o_vis = nullptr; ApplyFn o_apply = nullptr;

bool s_tried = false, s_okTick = false, s_okProbe = false, s_okHide = false, s_okVisHook = false, s_okApply = false;
std::atomic<unsigned> s_applyBench{0};               // veces que el juego aplico la visibilidad de un banco (con la fila prendida)
std::atomic<unsigned> s_gameShows{0};               // veces que el juego quiso mostrar el banco (queda escondido)

bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
bool SigAt(uintptr_t a, const unsigned char* sig, size_t n) { return Readable((const void*)a, n) && memcmp((const void*)a, sig, n) == 0; }
bool SafeCopy(void* dst, const void* src, size_t n) {
    __try { memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template <class T> bool Rd(uintptr_t a, T& out) { return a >= 0x10000 && SafeCopy(&out, (const void*)a, sizeof(T)); }
bool Heap(uintptr_t p) { return p >= 0x10000 && p < 0x7fffffff0000ull && (p < kExeLo || p >= kExeHi); }
bool InExe(uintptr_t p) { return p >= kExeLo && p < kExeHi; }

// fox::String del campo: [campo] -> registro, [registro] -> texto (como fox_cadena del medidor)
bool FoxStr(uintptr_t field, char* out, size_t n) {
    out[0] = 0; uintptr_t rec = 0, txt = 0;
    if (!Rd(field, rec) || !Heap(rec) || !Rd(rec, txt) || !Heap(txt)) return false;
    char buf[96] = {}; if (!SafeCopy(buf, (const void*)txt, sizeof buf - 1)) return false;
    size_t i = 0; for (; i + 1 < n && buf[i]; i++) { const unsigned char c = (unsigned char)buf[i]; if (c < 0x20 || c > 0x7e) return false; out[i] = (char)c; }
    out[i] = 0; return i > 0;
}

float Live(int k, float def) { return (!g_cfg.info[kRow][k].defined && g_cfg.live[kRow][k] == 0.0f) ? def : g_cfg.live[kRow][k]; }

// ---------------------------------------------------------------- estado (lo escribe el hilo del juego, lo anota Present)
std::mutex s_mx;
struct Sub { uint64_t id = 0; char name[64] = {}; };
struct Side { int starters = -1, total = -1, nSubs = 0; Sub subs[40]; };
struct State {
    Side side[2];
    unsigned sheetSeq = 0;                           // cambia cuando cambia la lista
    bool benchFound = false, benchHidden = false; char benchName[64] = {}; float box[6] = {};
    unsigned benchSeq = 0;
} s_st;
std::atomic<unsigned> s_ticks{0};
char s_logBuf[8][256]; std::atomic<int> s_logN{0};   // lineas de la sonda (hilo del juego) que Present pasa al registro

// cuerpo del banco encontrado (para mostrarlo de nuevo); se vuelve a comprobar antes de cada escritura
uintptr_t s_benchModel = 0, s_benchBody = 0, s_benchEnt = 0, s_benchScene = 0;
int s_findCooldown = 0;

// modelo -> functor (vt) -> cuerpo (vt, +0x30 asa, [asa+0x18] entidad) -> entidad StadiumModel (+0x28 nombre)
bool BenchChain(uintptr_t model, uintptr_t& body, uintptr_t& ent, char* name, size_t n) {
    uintptr_t vt = 0, funs = 0; uint32_t nfun = 0;
    if (!Rd(model, vt) || vt != kVtModel || !Rd(model + 0x100, nfun) || nfun < 1 || nfun > 16 || !Rd(model + 0x108, funs) || !Heap(funs)) return false;
    for (uint32_t i = 0; i < nfun; i++) {
        uintptr_t f = 0, fvt = 0, c = 0, cvt = 0, h = 0, e = 0, evt = 0;
        if (!Rd(funs + 8 * i, f) || !Heap(f) || !Rd(f, fvt) || fvt != kVtFunctor || !Rd(f + 0x10, c) || !Heap(c)) continue;
        if (!Rd(c, cvt) || cvt != kVtBody || !Rd(c + 0x30, h) || !Heap(h) || !Rd(h + 0x18, e) || !Heap(e)) continue;
        if (!Rd(e, evt) || evt != kVtStadiumModel) continue;
        if (!FoxStr(e + 0x28, name, n)) continue;
        body = c; ent = e; return true;
    }
    return false;
}

// recorre las escenas como el medidor y devuelve el modelo del banco de Konami
bool FindBench(uintptr_t& model, uintptr_t& body, uintptr_t& ent, uintptr_t& scene, char* name, size_t n) {
    uintptr_t first = 0; if (!Rd(kScenes, first) || !Heap(first)) return false;
    uintptr_t e = first;
    for (int ne = 0; ne < 64 && Heap(e); ne++) {
        uintptr_t next = 0, arr = 0; uint32_t nl = 0;
        if (!Rd(e, next) || !Rd(e + 0x28, nl) || !Rd(e + 0x30, arr)) break;
        if (Heap(arr) && nl >= 1 && nl <= 256) {
            for (uint32_t li = 0; li < nl; li++) {
                uintptr_t lst = 0, head = 0; uint32_t cnt = 0;
                if (!Rd(arr + 8 * li, lst) || !Heap(lst) || !Rd(lst, head) || !Rd(lst + 0x10, cnt) || !Heap(head) || cnt < 1 || cnt > 100000) continue;
                uintptr_t o = head;
                for (int k = 0; k < 8192 && Heap(o); k++) {
                    uintptr_t vt = 0, sg = 0, sc = 0; uint32_t fl = 0;
                    if (!Rd(o, vt) || !Rd(o + 0x18, sg) || !Rd(o + 0x24, fl) || !Rd(o + 0x28, sc)) break;
                    if ((fl & 1) != 1 || sc != e) break;
                    if (vt == kVtModel) {
                        uintptr_t b = 0, en = 0; char nm[64];
                        if (BenchChain(o, b, en, nm, sizeof nm) && strstr(nm, "_dir_bench")) {
                            model = o; body = b; ent = en; scene = e; strncpy_s(name, n, nm, _TRUNCATE); return true;
                        }
                    }
                    if (sg == head) break;
                    o = sg;
                }
            }
        }
        if (next == first) break;
        e = next;
    }
    return false;
}

// el banco guardado sigue siendo el mismo objeto vivo
bool BenchStillValid() {
    if (!s_benchModel) return false;
    uintptr_t b = 0, en = 0, sc = 0; uint32_t fl = 0; char nm[64];
    if (!Rd(s_benchModel + 0x24, fl) || (fl & 1) != 1 || !Rd(s_benchModel + 0x28, sc) || sc != s_benchScene) return false;
    if (!BenchChain(s_benchModel, b, en, nm, sizeof nm) || b != s_benchBody || en != s_benchEnt || !strstr(nm, "_dir_bench")) return false;
    return true;
}

void BenchSetVisible(bool vis) {
    if (!s_okHide || !BenchStillValid()) return;
    uint8_t cur = 0; if (!Rd(s_benchBody + 0x81, cur)) return;
    if ((cur != 0) == vis) return;
    (o_vis ? o_vis : (SetVisibleFn)kSetVisible)((void*)s_benchBody, vis ? 1 : 0);
    std::lock_guard<std::mutex> lk(s_mx); s_st.benchHidden = !vis; s_st.benchSeq++;
}

// cuerpo -> asa (+0x30) -> entidad (+0x18) StadiumModel con nombre "..._dir_bench..."
bool IsBenchBody(uintptr_t body) {
    uintptr_t vt = 0, h = 0, e = 0, evt = 0; char nm[64];
    if (!Rd(body, vt) || vt != kVtBody || !Rd(body + 0x30, h) || !Heap(h) || !Rd(h + 0x18, e) || !Heap(e) || !Rd(e, evt) || evt != kVtStadiumModel) return false;
    return FoxStr(e + 0x28, nm, sizeof nm) && strstr(nm, "_dir_bench") != nullptr;
}

// 16/11 prueba 1: el juego vuelve a mostrar el banco al terminar la cinematica (FUN_14191d410 -> FUN_14191d830 -> SetVisible
// con los grupos de objetos de cada modo) y el reloj de FixDemoMain no corre en el juego. Por eso el escondido se aplica aca:
// si el juego pide mostrar el banco con la fila prendida, se lo deja escondido.
void __fastcall hk_Vis(void* body, char vis) {
    if (vis && Live(0, 0.0f) > 0.5f && Live(1, 1.0f) > 0.5f && IsBenchBody((uintptr_t)body)) {
        s_gameShows.fetch_add(1, std::memory_order_relaxed);
        vis = 0;                                       // si ya estaba escondido, el original no hace nada
    }
    o_vis(body, vis);
}

// 16/11 prueba 2: en el juego el banco que se ve no pasa nunca por SetVisible (otra instancia, creada despues de la
// presentacion). Toda instancia aplica su visibilidad en FUN_141906600 (+0x80 y +0x81 -> objeto de dibujo): ahi se pone
// +0x81 = 0 antes de que el juego la aplique, en el hilo del juego.
void __fastcall hk_Apply(void* body) {
    if (Live(0, 0.0f) > 0.5f && Live(1, 1.0f) > 0.5f && IsBenchBody((uintptr_t)body)) {
        s_applyBench.fetch_add(1, std::memory_order_relaxed);
        *((volatile uint8_t*)body + 0x81) = 0;
    }
    o_apply(body);
}

// solo lectura (desde Present): todos los cuerpos "_dir_bench" de las escenas y su estado
struct BenchSeen { uintptr_t body; uint8_t node, vis; uint32_t drawFlags; bool drawOk; float box[6]; };
int ScanBenches(BenchSeen* out, int max) {
    int n = 0; uintptr_t first = 0; if (!Rd(kScenes, first) || !Heap(first)) return 0;
    uintptr_t e = first;
    for (int ne = 0; ne < 64 && Heap(e) && n < max; ne++) {
        uintptr_t next = 0, arr = 0; uint32_t nl = 0;
        if (!Rd(e, next) || !Rd(e + 0x28, nl) || !Rd(e + 0x30, arr)) break;
        if (Heap(arr) && nl >= 1 && nl <= 256) {
            for (uint32_t li = 0; li < nl && n < max; li++) {
                uintptr_t lst = 0, head = 0; uint32_t cnt = 0;
                if (!Rd(arr + 8 * li, lst) || !Heap(lst) || !Rd(lst, head) || !Rd(lst + 0x10, cnt) || !Heap(head) || cnt < 1 || cnt > 100000) continue;
                uintptr_t o = head;
                for (int k = 0; k < 8192 && Heap(o) && n < max; k++) {
                    uintptr_t vt = 0, sg = 0, sc = 0; uint32_t fl = 0;
                    if (!Rd(o, vt) || !Rd(o + 0x18, sg) || !Rd(o + 0x24, fl) || !Rd(o + 0x28, sc)) break;
                    if ((fl & 1) != 1 || sc != e) break;
                    uint32_t nfun = 0; uintptr_t funs = 0;
                    if (vt == kVtModel && Rd(o + 0x100, nfun) && nfun >= 1 && nfun <= 16 && Rd(o + 0x108, funs) && Heap(funs)) {
                        for (uint32_t i = 0; i < nfun && n < max; i++) {
                            uintptr_t f = 0, fvt = 0, c = 0;
                            if (!Rd(funs + 8 * i, f) || !Heap(f) || !Rd(f, fvt) || fvt != kVtFunctor || !Rd(f + 0x10, c) || !Heap(c) || !IsBenchBody(c)) continue;
                            BenchSeen& b = out[n++]; b.body = c; b.node = b.vis = 0xff; b.drawFlags = 0; b.drawOk = false;
                            for (int q = 0; q < 6; q++) { b.box[q] = 0.0f; Rd(o + 0x80 + (q < 3 ? 4 * q : 0x10 + 4 * (q - 3)), b.box[q]); }
                            Rd(c + 0x80, b.node); Rd(c + 0x81, b.vis);
                            uintptr_t d = 0; if (Rd(c + 0x88, d) && Heap(d) && Rd(d + 0x1b4, b.drawFlags)) b.drawOk = true;
                        }
                    }
                    if (sg == head) break;
                    o = sg;
                }
            }
        }
        if (next == first) break;
        e = next;
    }
    return n;
}

// caja del banco para el VS del publico (filas 517 / 518, las escribe BancaFrame; no van al ini)
float s_crowdBox[6] = {}; bool s_crowdBoxOk = false;

// ---------------------------------------------------------------- convocados
void ReadSheet() {
    uintptr_t sheet = 0; if (!Rd(kSheetPtr, sheet) || !Heap(sheet)) return;
    State tmp{};
    for (int s = 0; s < 2; s++) {
        const uintptr_t base = sheet + (uintptr_t)s * 0x2830 + 0xf8;
        uint8_t st = 0, tot = 0;
        if (!Rd(base + 0x2818, st) || !Rd(base + 0x2819, tot)) return;
        Side& d = tmp.side[s]; d.starters = st; d.total = tot;
        if (st < 1 || st > 11 || tot < st || tot > 40) continue;      // ficha sin armar: sin suplentes
        for (int i = st; i < tot && d.nSubs < 40; i++) {
            const uintptr_t slot = base + (uintptr_t)i * 0x100;
            uint64_t id = 0; if (!Rd(slot + 8, id) || id == 0) continue;
            Sub& u = d.subs[d.nSubs++]; u.id = id;
            char nm[64] = {}; if (SafeCopy(nm, (const void*)(slot + 8 + 0x9c), 61)) { nm[61] = 0; for (char& c : nm) if (c && (unsigned char)c < 0x20) c = '?'; strncpy_s(u.name, nm, _TRUNCATE); }
        }
    }
    std::lock_guard<std::mutex> lk(s_mx);
    bool same = true;
    for (int s = 0; s < 2 && same; s++) {
        const Side &a = s_st.side[s], &b = tmp.side[s];
        same = a.starters == b.starters && a.total == b.total && a.nSubs == b.nSubs;
        for (int i = 0; same && i < a.nSubs; i++) same = a.subs[i].id == b.subs[i].id;
    }
    if (!same) { for (int s = 0; s < 2; s++) s_st.side[s] = tmp.side[s]; s_st.sheetSeq++; }
}

// ---------------------------------------------------------------- hilo del juego
void Tick() {
    s_ticks.fetch_add(1, std::memory_order_relaxed);
    const bool on = Live(0, 0.0f) > 0.5f;
    const bool hide = on && Live(1, 1.0f) > 0.5f;
    static unsigned n = 0; n++;
    if ((n % 30) == 0 && on) ReadSheet();
    // banco de Konami: buscarlo (cada ~2 s mientras no esta) y esconderlo / mostrarlo
    if (s_benchModel && !BenchStillValid()) {   // otra cancha / se descargo: olvidar sin escribir nada
        s_benchModel = s_benchBody = s_benchEnt = s_benchScene = 0;
        std::lock_guard<std::mutex> lk(s_mx); s_st.benchFound = false; s_st.benchHidden = false; s_st.benchSeq++;
    }
    if (on && !s_benchModel && --s_findCooldown <= 0) {
        s_findCooldown = 120;
        uintptr_t m = 0, b = 0, en = 0, sc = 0; char nm[64] = {};
        if (FindBench(m, b, en, sc, nm, sizeof nm)) {
            s_benchModel = m; s_benchBody = b; s_benchEnt = en; s_benchScene = sc;
            std::lock_guard<std::mutex> lk(s_mx);
            s_st.benchFound = true; strncpy_s(s_st.benchName, nm, _TRUNCATE);
            for (int k = 0; k < 6; k++) { float v = 0; Rd(m + 0x80 + (k < 3 ? 4 * k : 0x10 + 4 * (k - 3)), v); s_st.box[k] = v; }
            s_st.benchSeq++;
        }
    }
    if (s_benchModel) BenchSetVisible(!hide);
}

void __fastcall hk_Fdm(void* self) {
    o_fdm(self);
    Tick();
}

void __fastcall hk_Fdh(void* fd, void* res, uint32_t a3, uint8_t a4, uint64_t a5, void* a6) {
    if (res && Live(0, 0.0f) > 0.5f && Live(2, 1.0f) > 0.5f) {
        static int lines = 0;
        if (lines < 200) {
            uintptr_t r20 = 0, r28 = 0, r0 = 0; uint64_t pid = 0; uint32_t bones = 0; float start = 0;
            Rd((uintptr_t)res, r0); Rd((uintptr_t)res + 0x20, r20); Rd((uintptr_t)res + 0x28, r28); Rd((uintptr_t)fd + 0x370, pid);
            if (Heap(r20)) Rd(r20 + 0xf8, bones);
            uint32_t lo = (uint32_t)a5; memcpy(&start, &lo, 4);
            const int k = s_logN.load();
            if (k < 8) {
                sprintf_s(s_logBuf[k], "banca: cinematica da clip a jugador %llu: recurso %p (vt %p) +0x20 %p (%u huesos) +0x28 %p, modo %u, espejo %u, desde %.3f, extra %p",
                          (unsigned long long)pid, res, (void*)r0, (void*)r20, bones, (void*)r28, a3, a4, start, a6);
                s_logN.store(k + 1); lines++;
            }
        }
    }
    o_fdh(fd, res, a3, a4, a5, a6);
}

void Install() {
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("banca: parches en memoria apagados (motor = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("banca: exe distinto (sello %08X): sin ganchos", (unsigned)nt->FileHeader.TimeDateStamp); return; }
    s_okHide = SigAt(kSetVisible, kSetVisibleSig, sizeof kSetVisibleSig);
    if (s_okHide && MH_CreateHook((void*)kSetVisible, (void*)&hk_Vis, (void**)&o_vis) == MH_OK && MH_EnableHook((void*)kSetVisible) == MH_OK) s_okVisHook = true;
    else o_vis = nullptr;
    if (!SigAt(kApply, kApplySig, sizeof kApplySig)) Log("banca: la firma de 0x141906600 (aplicar visibilidad) no coincide: el banco solo se esconde por SetVisible");
    else if (MH_CreateHook((void*)kApply, (void*)&hk_Apply, (void**)&o_apply) == MH_OK && MH_EnableHook((void*)kApply) == MH_OK) s_okApply = true;
    if (!SigAt(kFdmVf3, kFdmVf3Sig, sizeof kFdmVf3Sig)) Log("banca: la firma de FixDemoMain::vf3 (0x141f37770) no coincide: sin reloj, no hace nada");
    else if (MH_CreateHook((void*)kFdmVf3, (void*)&hk_Fdm, (void**)&o_fdm) == MH_OK && MH_EnableHook((void*)kFdmVf3) == MH_OK) s_okTick = true;
    if (!SigAt(kFdhVf1, kFdhVf1Sig, sizeof kFdhVf1Sig)) Log("banca: la firma de FixDemoHuman::vf1 (0x141f87e00) no coincide: sin sonda de cinematicas");
    else if (MH_CreateHook((void*)kFdhVf1, (void*)&hk_Fdh, (void**)&o_fdh) == MH_OK && MH_EnableHook((void*)kFdhVf1) == MH_OK) s_okProbe = true;
    Log("banca: reloj %s, sonda de cinematicas %s, esconder el banco %s (fila 516: x %.0f)", s_okTick ? "enganchado" : "NO", s_okProbe ? "enganchada" : "NO",
        !s_okHide ? "NO (firma de 0x1419055f0 distinta)" : s_okVisHook ? (s_okApply ? "listo (SetVisible y al aplicar)" : "solo por SetVisible") : "solo en cinematicas (gancho de SetVisible fallo)", Live(0, 0.0f));
}
} // namespace

// filas 517 / 518 para el VS del publico (UpdateTimeParams, con g_cfgMutex tomado): 517 = x min, x max, z min, z max;
// 518 = y desde, y hasta (pies de la figura), z 1 = valida. Margenes: 0,5 m a lo ancho, 1 m de fondo; altura: del piso del banco
// - 0,6 m a + 0,8 m (la tribuna de adelante esta mas arriba).
void BancaFrame(float (*p)[4], double) {
    const bool on = s_crowdBoxOk && Live(0, 0.0f) > 0.5f && Live(1, 1.0f) > 0.5f;
    if (!on) { for (int k = 0; k < 4; k++) p[517][k] = p[518][k] = 0.0f; return; }
    const float* b = s_crowdBox;
    p[517][0] = b[0] - 0.5f; p[517][1] = b[3] + 0.5f; p[517][2] = b[2] - 1.0f; p[517][3] = b[5] + 1.0f;
    p[518][0] = b[1] - 0.6f; p[518][1] = b[1] + 0.8f; p[518][2] = 1.0f; p[518][3] = 0.0f;
}

void BancaPresent() {
    if (!s_tried) Install();
    if (!s_okTick && !s_okProbe && !s_okVisHook && !s_okApply) return;
    // lineas de la sonda
    const int k = s_logN.load();
    for (int i = 0; i < k; i++) Log("%s", s_logBuf[i]);
    if (k) s_logN.store(0);
    // reloj: cuantas veces por segundo corre FixDemoMain::vf3 (cada ~10 s, solo prendido)
    static double nextT = 0; static unsigned lastTicks = 0; static bool wasOn = false;
    const bool on = Live(0, 0.0f) > 0.5f;
    if (on != wasOn) { wasOn = on; Log("banca: %s", on ? "prendido (paso 1: esconder el banco y leer convocados)" : "apagado"); }
    const double t = Now();
    if (on && t >= nextT) {
        const unsigned tk = s_ticks.load(std::memory_order_relaxed);
        if (nextT > 0) Log("banca: reloj del juego %.1f por segundo", (tk - lastTicks) / 10.0);
        // solo lectura: todos los bancos de las escenas y si se dibujan
        BenchSeen bs[8]; const int nb = ScanBenches(bs, 8);
        char line[512];
        if (nb > 0) Log("banca: caja del banco para el publico x %.2f..%.2f, y %.2f..%.2f, z %.2f..%.2f (figuras del publico con los pies ahi = escondidas)",
                        bs[0].box[0], bs[0].box[3], bs[0].box[1], bs[0].box[4], bs[0].box[2], bs[0].box[5]); int w = sprintf_s(line, "banca: bancos en escena %d, aplicados escondidos %u:", nb, s_applyBench.load(std::memory_order_relaxed));
        for (int i = 0; i < nb && w > 0 && w < 440; i++)
            w += sprintf_s(line + w, sizeof line - w, " [%p nodo %u visible %u dibujo %s]", (void*)bs[i].body, bs[i].node, bs[i].vis,
                           !bs[i].drawOk ? "?" : (bs[i].drawFlags & 1) ? "ESCONDIDO" : "se dibuja");
        Log("%s", line);
        ReadSheet();   // convocados tambien sin cinematica (solo lectura)
        lastTicks = tk; nextT = t + 10.0;
    }
    // el juego quiso mostrar el banco y se lo dejo escondido
    static unsigned seenShows = 0; const unsigned shows = s_gameShows.load(std::memory_order_relaxed);
    if (shows != seenShows) { Log("banca: el juego pidio mostrar el banco de Konami (%u veces en total): queda escondido", shows); seenShows = shows; }
    // caja del banco cada segundo (los munecos del banco en el juego son publico: los esconde el VS del publico, pes_crowd_face.hlsli)
    static double nextBox = 0;
    if (!on) s_crowdBoxOk = false;
    else if (t >= nextBox) {
        nextBox = t + 1.0;
        BenchSeen bs[8]; const int nb = ScanBenches(bs, 8);
        if (nb > 0) { for (int q = 0; q < 6; q++) s_crowdBox[q] = bs[0].box[q]; s_crowdBoxOk = true; } else s_crowdBoxOk = false;
    }
    // cambios de convocados / banco
    static unsigned seenSheet = 0, seenBench = 0;
    State st; { std::lock_guard<std::mutex> lk(s_mx); st = s_st; }
    if (st.sheetSeq != seenSheet) {
        seenSheet = st.sheetSeq;
        for (int s = 0; s < 2; s++) {
            const Side& d = st.side[s];
            Log("banca: %s: titulares %d, convocados %d, suplentes %d", s ? "visitante" : "local", d.starters, d.total, d.nSubs);
            for (int i = 0; i < d.nSubs; i++) Log("banca:   suplente %2d: id %llu %s", i + 1, (unsigned long long)d.subs[i].id, d.subs[i].name);
        }
    }
    if (st.benchSeq != seenBench) {
        seenBench = st.benchSeq;
        if (st.benchFound) Log("banca: banco de Konami %s (%s), caja %.2f %.2f %.2f / %.2f %.2f %.2f", st.benchHidden ? "ESCONDIDO" : "visible", st.benchName,
                               st.box[0], st.box[1], st.box[2], st.box[3], st.box[4], st.box[5]);
        else Log("banca: banco de Konami no esta (otra pantalla o estadio sin el)");
    }
}

} // namespace fx
