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
// w tope de suplentes por equipo (0 = todos los convocados).
// Fila 517 (paso 2a, suplentes reales sentados): x on / off (de fabrica prendido; necesita x516 e y516), y cuantos llevan la
// pechera de suplente (0 a 100 %, de fabrica 50: a quienes les toca se sortea al cargar el partido), z altura del asiento (cm),
// w adelante / atras (cm). El paso 2a esta explicado mas abajo ("paso 2a").
// Fila 518 (2e, 17/11): x que llevan los que tienen pechera (0 segun el clima del partido: verano la pechera, invierno la parte de arriba
// de la campera; 1 siempre la pechera; 2 siempre la campera), y que pechera (0 la de suplente del juego en los dos equipos; 1 la propia de
// cada equipo: naranja el local, verde el visitante, con su escudo), z probar clips (0 al azar; 1 a 8 todos con ese clip, al momento),
// w clips de la sala de reunion (1 si). La pechera y la campera las pone PecheraPorJugador.lua (v0.15) con los modelos que ya usa en el
// entrenamiento: lee pes21fx_banca_ropa (abajo) con GetProcAddress y cambia las tablas de piezas 0 y 8 en cada pedido de modelo.
// (2e, Agustin 17/11 03:39: "si el clima es invierno lluvioso o con nieve se cargue el modelo de campera entero y solo en invierno despejado
// se cargue sin la parte del pantalon"): con campera y weather 1 (lluvia) o 2 (nieve) del partido, la campera ENTERA: el suplente se pide
// con el tipo de la ropa de entrenamiento (LoadId+0x4c = 0x16 el local, 0x17 el visitante; sin pechera), y el juego le apaga el uniforme
// como en el entrenamiento (mascara &= 0x11). El modulo (v0.16) pone en la pieza 7 la campera del visitante.
#include "core.h"
#include <MinHook.h>
#include <atomic>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <cstdarg>
#include <cctype>
#include <cmath>
#include <intrin.h>

// (2e) lo lee PecheraPorJugador.lua v0.15 (GetProcAddress de esta DLL, exportado en d3d11.def). Lo escribe el hilo del juego al armar la
// lista de suplentes y vuelve a 0 al soltarlos (fin del partido): 0 nada (tablas como siempre), 1 las piezas 0 / 8 son la parte de arriba
// de la campera del local / del visitante, 2 la pieza 8 es la pechera verde del visitante (pecheras propias de cada equipo), 3 campera
// entera (lluvia o nieve: la pieza 7 = la campera del visitante).
extern "C" { volatile unsigned char pes21fx_banca_ropa = 0; }

// (v7d) publicados por config.cpp (espacio global, fuera de fx)
extern std::atomic<unsigned> g_camSeq; extern std::atomic<float> g_camPV[16]; extern std::atomic<double> g_camT;
extern std::atomic<float> g_clkComp, g_clkPlayer; extern std::atomic<unsigned> g_clkCompN, g_clkPlayerN;
namespace fx {

// (21/11 3icxbh) laboratorio de animaciones (motorlab.cpp): banco que usa (bit 0 local, bit 1 visitante; sus suplentes reales no se
// muestran) y su reloj (los jugadores del Motor corren con el del partido y se sueltan al terminar)
std::atomic<int> g_labHideSide{0};
void MotorLabGameTick(); void MotorLabMatchEnd();

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

// (16/11 paso 1g) cobra::rd::impl::SceneStadiumNodeImpl::vf2 (0x1419b75b0) recorre en CADA cuadro todos los StadiumModelImpl del
// estadio y llama FUN_1419b79f0(impl, dt, mostrar), que pisa el objeto de dibujo: [impl+0x20]+0x1ac = mostrar ? 0 : -1. Por eso el
// escondido de SetVisible / aplicar (+0x81 -> +0x1ac = -1, +0x1b4 bit 0) duraba hasta el cuadro siguiente y los suplentes se veian.
// StadiumModelImpl (FUN_1419b1910): +0x20 modelo, +0x38 mascara de direccion, +0x3c mascara de kind, +0x44 = 1 << demoGroup.
// Prologo: push rbx; sub rsp,20h; mov rbx,rcx; mov rcx,[rcx+20h]; test rcx,rcx; je ...
const uintptr_t kImplShow = 0x1419b79f0ull;
const unsigned char kImplShowSig[22] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x49,0x20,0x48,0x85,0xc9,0x0f,0x84,0x8e,0x01,0x00,0x00 };
const uint32_t kGroupBench = 1u << 1;                 // StadiumModel_DemoGroup BENCH = 1
const uint32_t kKindScarecrow = (1u << 6) | (1u << 7) | (1u << 8);   // STAFF_SCARECROW_A / B / C

const uintptr_t kSheetPtr = 0x1438041d0ull;      // ficha del partido de las cinematicas (DAT_1438041d0)
const uintptr_t kScenes = 0x14374d958ull;        // primera escena (lista circular por +0)
const uintptr_t kVtModel = 0x142a0a880ull, kVtFunctor = 0x142a72358ull, kVtBody = 0x142a72268ull, kVtStadiumModel = 0x142a721e8ull;
const uintptr_t kExeLo = 0x140000000ull, kExeHi = 0x143a00000ull;

typedef void(__fastcall* FdmVf3)(void*);
typedef void(__fastcall* FdhVf1)(void*, void*, uint32_t, uint8_t, uint64_t, void*);
typedef void(__fastcall* SetVisibleFn)(void*, char);
typedef void(__fastcall* ApplyFn)(void*);
typedef void(__fastcall* ImplShowFn)(void*, float, char);
FdmVf3 o_fdm = nullptr; FdhVf1 o_fdh = nullptr; SetVisibleFn o_vis = nullptr; ApplyFn o_apply = nullptr; ImplShowFn o_implShow = nullptr;

bool s_tried = false, s_okTick = false, s_okProbe = false, s_okHide = false, s_okVisHook = false, s_okApply = false, s_okImpl = false;
std::atomic<unsigned> s_applyBench{0};               // veces que el juego aplico la visibilidad de un banco (con la fila prendida)
std::atomic<unsigned> s_gameShows{0};
std::atomic<unsigned> s_implHidden{0};               // cuadros x modelos que la escena del estadio queria mostrar y quedaron escondidos
std::atomic<uintptr_t> s_benchDraw[8];               // objetos de dibujo de los "_dir_bench" vistos (los pone ScanBenches)
std::atomic<uintptr_t> s_implSeen[16]; std::atomic<uint32_t> s_implSeenMask[16]; std::atomic<int> s_implSeenN{0};   // para el registro               // veces que el juego quiso mostrar el banco (queda escondido)

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
std::atomic<unsigned long> s_fdmThread{0};        // hilo de FixDemoMain::vf3 (donde el juego crea y mueve a sus jugadores sueltos)
void SubsTickFromScene();                          // paso 2a (abajo): reloj de reserva desde la escena del estadio
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

// cuerpo -> asa (+0x30) -> entidad (+0x18) StadiumModel con nombre "..._dir_bench..." (los suplentes genericos sentados: materiales
// so_benchmember_2017set_home1 / _away, campera st_jersey_bench00 / 01 con el color de cada equipo, 0 huesos).
bool IsBenchBody(uintptr_t body, char* nameOut = nullptr, size_t n = 0) {
    uintptr_t vt = 0, h = 0, e = 0, evt = 0; char nm[64];
    if (!Rd(body, vt) || vt != kVtBody || !Rd(body + 0x30, h) || !Heap(h) || !Rd(h + 0x18, e) || !Heap(e) || !Rd(e, evt) || evt != kVtStadiumModel) return false;
    if (!FoxStr(e + 0x28, nm, sizeof nm) || !strstr(nm, "_dir_bench")) return false;
    if (nameOut && n) strncpy_s(nameOut, n, nm, _TRUNCATE);
    return true;
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

// 16/11 paso 1g: el que de verdad decide si se dibuja (cada cuadro, hilo del juego). Esconde los StadiumModelImpl del grupo de
// cinematica BENCH con kind de espantapajaros (todos los estadios: Konami y mods usan el mismo campo demoGroup) o cuyo modelo sea
// el de un "_dir_bench" visto por nombre (por si un estadio de mod no puso el grupo).
void __fastcall hk_ImplShow(void* impl, float dt, char show) {
    if (show && Live(0, 0.0f) > 0.5f && Live(1, 1.0f) > 0.5f) {
        const uintptr_t im = (uintptr_t)impl; uint32_t kind = 0, group = 0; uintptr_t mdl = 0;
        bool bench = Rd(im + 0x3c, kind) && Rd(im + 0x44, group) && group == kGroupBench && (kind & kKindScarecrow) != 0;
        if (!bench && Rd(im + 0x20, mdl) && mdl)
            for (int i = 0; i < 8 && !bench; i++) bench = s_benchDraw[i].load(std::memory_order_relaxed) == mdl;
        if (bench) {
            show = 0;
            s_implHidden.fetch_add(1, std::memory_order_relaxed);
            int n = s_implSeenN.load(std::memory_order_relaxed); bool known = false;
            for (int i = 0; i < n && !known; i++) known = s_implSeen[i].load(std::memory_order_relaxed) == im;
            if (!known && n < 16) { s_implSeen[n].store(im); s_implSeenMask[n].store((group << 16) | (kind & 0xffff)); s_implSeenN.store(n + 1); }
        }
    }
    o_implShow(impl, dt, show);
}

// ---------------------------------------------------------------- 16/11 paso 1h: figuras sentadas que NO estan en el _dir_bench
// Prueba de 1g (18:20): Monumental perfecto, pero en el Sports Park de Konami (st045_sc2018) quedan sentadas en el banco algunas
// figuras de staff (camperas oscura / blanca / negra con botella / amarilla). No son del _dir_bench: Konami las pone dentro de los
// otros modelos de espantapajaros del estadio (stNNN_scYYYY_front_others / _stand / dir_demoRoute ...), donde TODAS las figuras
// del costado van juntas en pocas mallas por material (ver st009_sc2018_front_others: 32 mallas, 1 grupo). Esconder el modelo o
// la malla entera sacaria tambien a los parados del costado (guardias, utileros, camarografos). El paquete que usa el juego para
// el st045 no se puede leer (no esta en Data ni en los mods; la copia del mod Stade Bauer trae otras cajas), asi que la regla es
// general y vale para todos los estadios:
//   Al cargar cada fmdl (antes de que el juego lo suba a la placa), si es un espantapajaros de estadio (grupo de mallas
//   "MESH_stNNN_scYYYY_<parte>") y no es el _dir_bench:
//    a) mallas de suplente genericas fuera del _dir_bench (grupo "MESH_so_benchmen*" o material "so_benchmember*", como hace el
//       mod Stade Bauer en su front_stand): se esconden enteras.
//    b) dentro de la caja de los suplentes del _dir_bench de ese estadio (+0.6 m): cada pieza conectada de cada malla (pies de
//       hasta 1.2 m) se junta con las que se tocan en x / z (de todas las mallas) = una figura. Se esconde la figura si esta
//       SENTADA: apoya en el piso del banco (abajo <= piso + 0.35 m), mide entre 1.0 y 1.55 m, ocupa >= 0.35 m y tiene algun
//       material que no es de utileria (m_pr_ / pr_ / z_m_pr). Parados (1.8 m), conservadoras y botellas sueltas quedan.
//   Esconder = los triangulos de esa figura quedan degenerados (los 3 indices iguales) en TODOS los LOD de la malla: no se dibuja
//   ni da sombra. Es el mismo fmdl en memoria; se rehace al cargar otra vez el estadio. La fila 516 (x y y) tiene que estar
//   prendida al CARGAR el estadio; apagarla en el partido no las vuelve a mostrar hasta la proxima carga.
//   La caja del banco sale del _dir_bench (o de las mallas de suplente de (a)) de ese mismo estadio: si todavia no se cargo, se
//   busca el fmdl del _dir_bench en la memoria del paquete (+-64 MB del fmdl actual, dentro de la misma region) .
// Donde: FUN_141738a30 / FUN_141739530 (datos del modelo, &archivo): si [archivo+0x40] == 2 y [archivo+0x60] empieza con "FMDL",
// llaman al dispositivo grafico (vf 0x220 / 0x228) que crea los buffers de la placa con esos bytes. Se engancha la entrada.
const uintptr_t kFmdlUp1 = 0x141738a30ull;
const unsigned char kFmdlUp1Sig[25] = { 0x40,0x55,0x56,0x57,0x48,0x83,0xec,0x60,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48,0x89,0x9c,0x24,0x90,0x00,0x00,0x00 };
const uintptr_t kFmdlUp2 = 0x141739530ull;
const unsigned char kFmdlUp2Sig[21] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf2,0x48,0x8b,0xf9 };
typedef void(__fastcall* FmdlUp1Fn)(void*, void*);
typedef uint64_t(__fastcall* FmdlUp2Fn)(void*, void*);
FmdlUp1Fn o_fmdlUp1 = nullptr; FmdlUp2Fn o_fmdlUp2 = nullptr; bool s_okFmdl = false;

const float kMargin = 0.6f, kMaxFoot = 1.2f, kGround = 0.35f, kMinH = 1.0f, kMaxH = 1.55f, kMinFoot = 0.35f;

// registro desde el hilo de carga: lo pasa Present
std::mutex s_fmMx; char s_fmLog[16][220]; int s_fmLogN = 0; unsigned s_fmLost = 0;
void FmLog(const char* fmt, ...) {
    char b[220]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    std::lock_guard<std::mutex> lk(s_fmMx);
    if (s_fmLogN < 16) strcpy_s(s_fmLog[s_fmLogN++], b); else s_fmLost++;
}

// caja de los suplentes por estadio ("stNNN_scYYYY"); se junta lo visto en la misma carga (60 s), si no se reemplaza
struct BenchBox { char pre[16] = {}; float mn[3], mx[3]; ULONGLONG t = 0; bool ok = false; };
std::mutex s_boxMx; BenchBox s_boxes[16];
void BoxPut(const char* pre, const float* mn, const float* mx) {
    std::lock_guard<std::mutex> lk(s_boxMx); const ULONGLONG t = GetTickCount64();
    BenchBox* slot = nullptr; BenchBox* old = &s_boxes[0];
    for (auto& b : s_boxes) { if (b.ok && !strcmp(b.pre, pre)) { slot = &b; break; } if (!b.ok || b.t < old->t) old = &b; }
    if (slot && t - slot->t < 60000) { for (int i = 0; i < 3; i++) { slot->mn[i] = std::min(slot->mn[i], mn[i]); slot->mx[i] = std::max(slot->mx[i], mx[i]); } slot->t = t; return; }
    if (!slot) slot = old;
    strcpy_s(slot->pre, pre); memcpy(slot->mn, mn, 12); memcpy(slot->mx, mx, 12); slot->t = t; slot->ok = true;
}
bool BoxGet(const char* pre, float* mn, float* mx) {
    std::lock_guard<std::mutex> lk(s_boxMx); const ULONGLONG t = GetTickCount64();
    for (auto& b : s_boxes) if (b.ok && !strcmp(b.pre, pre) && t - b.t < 60000) { memcpy(mn, b.mn, 12); memcpy(mx, b.mx, 12); return true; }
    return false;
}

// lector de fmdl 2.03 con limites (mismo formato que tools/fox/fmdl.py)
struct Fm {
    uint8_t* p = nullptr; size_t size = 0;
    struct { uint16_t cnt; uint32_t off; bool ok; } b0[32] = {};
    struct { uint32_t off, len; bool ok; } b1[8] = {};
    const uint8_t* E(int t, size_t i, size_t sz) const {
        if (t < 0 || t >= 32 || !b0[t].ok || i >= b0[t].cnt) return nullptr;
        const size_t o = (size_t)b0[t].off + sz * i; return (o + sz <= size) ? p + o : nullptr;
    }
};
template <class T> T U(const uint8_t* q) { T v; memcpy(&v, q, sizeof v); return v; }
bool FmOpen(Fm& f, uint8_t* p, size_t avail) {
    if (avail < 56 || U<uint32_t>(p) != 0x4c444d46u) return false;
    const float ver = U<float>(p + 4); if (!(ver > 1.99f && ver < 2.11f)) return false;
    const uint64_t info = U<uint64_t>(p + 8); const uint32_t n0 = U<uint32_t>(p + 32), n1 = U<uint32_t>(p + 36);
    const uint32_t s0 = U<uint32_t>(p + 40), s0len = U<uint32_t>(p + 44), s1 = U<uint32_t>(p + 48), s1len = U<uint32_t>(p + 52);
    const uint64_t end = std::max<uint64_t>((uint64_t)s0 + s0len, (uint64_t)s1 + s1len);
    if (end > avail || end < 56 || n0 > 64 || n1 > 16 || info < 56 || info + 8ull * n0 + 12ull * n1 > end) return false;
    f.p = p; f.size = (size_t)end;
    for (uint32_t i = 0; i < n0; i++) {
        const uint8_t* e = p + info + 8 * i; const uint16_t t = U<uint16_t>(e);
        if (t < 32) { f.b0[t].cnt = U<uint16_t>(e + 2); f.b0[t].off = s0 + U<uint32_t>(e + 4); f.b0[t].ok = true; }
    }
    for (uint32_t i = 0; i < n1; i++) {
        const uint8_t* e = p + info + 8 * n0 + 12 * i; const uint32_t t = U<uint32_t>(e);
        if (t < 8) { f.b1[t].off = s1 + U<uint32_t>(e + 4); f.b1[t].len = U<uint32_t>(e + 8); f.b1[t].ok = true; }
    }
    return f.b0[3].ok && f.b0[9].ok && f.b0[10].ok && f.b0[11].ok && f.b0[14].ok && f.b1[2].ok && f.b0[12].ok && f.b1[3].ok;
}
// cadena n (bloque 12 -> seccion 1 bloque 3), sin copiar: devuelve largo
const char* FmStr(const Fm& f, uint32_t id, size_t& len) {
    const uint8_t* e = f.E(12, id, 8); if (!e) return nullptr;
    len = U<uint16_t>(e + 2); const size_t o = (size_t)f.b1[3].off + U<uint32_t>(e + 4);
    return (o + len <= f.size) ? (const char*)f.p + o : nullptr;
}
bool StrStarts(const char* s, size_t len, const char* pre) { const size_t n = strlen(pre); return len >= n && !memcmp(s, pre, n); }

struct FmMesh {
    uint32_t nv = 0; const uint8_t* pos = nullptr; uint32_t stride = 0;   // posicion float3
    uint16_t* idx = nullptr; uint32_t nidx = 0;                             // indices de todos los LOD de la malla
    uint16_t mat = 0, group = 0xffff; bool bench = false;
};
bool FmMeshes(const Fm& f, std::vector<FmMesh>& out) {
    const uint16_t nm = f.b0[3].cnt; out.assign(nm, FmMesh{});
    uint32_t ibOff = 0; bool ib = false;
    for (uint16_t i = 0; i < f.b0[14].cnt && !ib; i++) { const uint8_t* b = f.E(14, i, 16); if (b && U<uint32_t>(b) == 1) { ibOff = f.b1[2].off + U<uint32_t>(b + 8); ib = true; } }
    if (!ib) return false;
    uint32_t lods = 1; if (f.b0[16].ok) { const uint8_t* l = f.E(16, 0, 4); if (l) lods = std::min<uint32_t>(std::max<uint32_t>(U<uint32_t>(l), 1u), 16u); }
    for (uint16_t k = 0; k < nm; k++) {
        const uint8_t* e = f.E(3, k, 0x30); if (!e) return false;
        FmMesh& m = out[k]; m.mat = U<uint16_t>(e + 4); m.nv = U<uint16_t>(e + 0xA);
        const uint16_t fmt = U<uint16_t>(e + 8); const uint32_t ff = U<uint32_t>(e + 0x10), fc = U<uint32_t>(e + 0x14); const uint64_t fi = U<uint64_t>(e + 0x18);
        // posicion: formato de malla -> buffers -> formato de vertice (uso 0, tipo 1 = float3)
        const uint8_t* fa = f.E(9, fmt, 8); if (!fa) return false;
        const uint8_t nmf = fa[0]; const uint16_t fmf = U<uint16_t>(fa + 4), fvf = U<uint16_t>(fa + 6); uint32_t vi = 0;
        for (uint8_t j = 0; j < nmf && !m.pos; j++) {
            const uint8_t* mf = f.E(10, (size_t)fmf + j, 8); if (!mf) return false;
            const uint8_t bidx = mf[0], nf = mf[1], stride = mf[2]; const uint32_t moff = U<uint32_t>(mf + 4);
            const uint8_t* bo = f.E(14, bidx, 16);
            for (uint8_t q = 0; q < nf && bo; q++) {
                const uint8_t* vf = f.E(11, (size_t)fvf + vi + q, 4); if (!vf) return false;
                if (vf[0] == 0 && vf[1] == 1) {
                    const size_t base = (size_t)f.b1[2].off + U<uint32_t>(bo + 8) + moff + U<uint16_t>(vf + 2);
                    if (m.nv && stride >= 12 && base + (size_t)stride * (m.nv - 1) + 12 <= f.size) { m.pos = f.p + base; m.stride = stride; }
                }
            }
            vi += nf;
        }
        // indices: rango de todos los LOD (bloque 17 relativo a la primera cara de la malla)
        uint32_t lo = 0, hi = fc;
        for (uint32_t j = 0; j < lods; j++) { const uint8_t* l = f.E(17, (size_t)fi + j, 8); if (!l) break; const uint32_t a = U<uint32_t>(l), c = U<uint32_t>(l + 4); lo = std::min(lo, a); hi = std::max(hi, a + c); }
        const size_t io = (size_t)ibOff + 2ull * ((size_t)ff + lo); const uint32_t n = (hi - lo) / 3 * 3;
        if (io + 2ull * n <= f.size) { m.idx = (uint16_t*)(f.p + io); m.nidx = n; }
    }
    // grupo de cada malla (bloque 2: +4 grupo, nmallas, primera)
    for (uint16_t g = 0; g < f.b0[2].cnt; g++) {
        const uint8_t* a = f.E(2, g, 0x20); if (!a) break;
        const uint16_t grp = U<uint16_t>(a + 4), n = U<uint16_t>(a + 6), first = U<uint16_t>(a + 8);
        for (uint32_t k = first; k < (uint32_t)first + n && k < nm; k++) out[k].group = grp;
    }
    return true;
}
// prefijo "stNNN_scYYYY" y parte ("front_others", "dir_bench"...) del grupo "MESH_stNNN_scYYYY_<parte>"
bool FmStadium(const Fm& f, char* pre, char* part, size_t partN) {
    for (uint16_t g = 0; g < f.b0[1].cnt; g++) {
        const uint8_t* e = f.E(1, g, 8); if (!e) return false;
        size_t len = 0; const char* s = FmStr(f, U<uint16_t>(e), len); if (!s || len < 19 || len > 80) continue;
        if (memcmp(s, "MESH_st", 7) || memcmp(s + 10, "_sc", 3) || s[17] != '_') continue;
        bool dig = true; for (int i : {7, 8, 9, 13, 14, 15, 16}) dig = dig && s[i] >= '0' && s[i] <= '9';
        if (!dig) continue;
        memcpy(pre, s + 5, 12); pre[12] = 0;
        const size_t pl = std::min(len - 18, partN - 1); memcpy(part, s + 18, pl); part[pl] = 0;
        return true;
    }
    return false;
}
void FmBenchFlags(const Fm& f, std::vector<FmMesh>& ms, bool wholeFile) {
    for (auto& m : ms) {
        if (wholeFile) { m.bench = true; continue; }
        size_t len = 0; const char* s = nullptr;
        const uint8_t* ge = (m.group != 0xffff) ? f.E(1, m.group, 8) : nullptr;
        if (ge && (s = FmStr(f, U<uint16_t>(ge), len)) && StrStarts(s, len, "MESH_so_benchmen")) { m.bench = true; continue; }
        const uint8_t* me = f.E(4, m.mat, 0x10);
        if (me && (s = FmStr(f, U<uint16_t>(me), len)) && StrStarts(s, len, "so_benchmember")) m.bench = true;
    }
}
bool FmBenchBox(const std::vector<FmMesh>& ms, float* mn, float* mx) {
    bool any = false;
    for (auto& m : ms) {
        if (!m.bench || !m.pos) continue;
        for (uint32_t v = 0; v < m.nv; v++) {
            float q[3]; memcpy(q, m.pos + (size_t)v * m.stride, 12);
            for (int i = 0; i < 3; i++) { if (!any || q[i] < mn[i]) mn[i] = q[i]; if (!any || q[i] > mx[i]) mx[i] = q[i]; }
            any = true;
        }
    }
    return any;
}
bool IsPropMat(const Fm& f, uint16_t mat) {
    size_t len = 0; const uint8_t* me = f.E(4, mat, 0x10); const char* s = me ? FmStr(f, U<uint16_t>(me), len) : nullptr;
    return s && (StrStarts(s, len, "m_pr_") || StrStarts(s, len, "pr_") || StrStarts(s, len, "z_m_pr"));
}
// 1i: botellas de utileria (Konami las llama "m_pr_bottole"; se acepta tambien "bottle"), sin importar mayusculas
bool IsBottleMat(const Fm& f, uint16_t mat) {
    size_t len = 0; const uint8_t* me = f.E(4, mat, 0x10); const char* s = me ? FmStr(f, U<uint16_t>(me), len) : nullptr;
    if (!s) return false;
    static const char* const kW[2] = { "bottole", "bottle" };
    for (const char* w : kW) {
        const size_t n = strlen(w);
        for (size_t i = 0; i + n <= len; i++) {
            size_t j = 0; while (j < n && (char)tolower((unsigned char)s[i + j]) == w[j]) j++;
            if (j == n) return true;
        }
    }
    return false;
}

// el _dir_bench del mismo estadio en la memoria del paquete (si se carga despues que este modelo)
bool ScanBenchNear(uint8_t* self, const char* pre, float* mn, float* mx) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(self, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const uintptr_t rb = (uintptr_t)mi.BaseAddress, re = rb + mi.RegionSize, s = (uintptr_t)self;
    const uintptr_t lo = std::max<uintptr_t>(rb, s > (64ull << 20) ? s - (64ull << 20) : 0), hi = std::min<uintptr_t>(re, s + (64ull << 20));
    for (uintptr_t q = s - ((s - lo) / 16) * 16; q + 56 <= hi; q += 16) {
        if (q == s || U<uint32_t>((const uint8_t*)q) != 0x4c444d46u) continue;
        Fm f; if (!FmOpen(f, (uint8_t*)q, re - q)) continue;
        char p2[16], part[40]; if (!FmStadium(f, p2, part, sizeof part) || strcmp(p2, pre) || strcmp(part, "dir_bench")) continue;
        std::vector<FmMesh> ms; if (!FmMeshes(f, ms)) continue;
        FmBenchFlags(f, ms, true);
        if (FmBenchBox(ms, mn, mx)) return true;
    }
    return false;
}

// ---------------------------------------------------------------- 16/11 paso 2a: asientos del banco
// Los suplentes reales se sientan donde Konami sento a sus figuras: en el _dir_bench cada figura es UNA malla (st009_sc2020: 11
// de material so_benchmember_2017set_home1 con x < 0 y 11 de ..._away con x > 0; el Sports Park trae dos filas, la de atras
// 0,18 m mas alta). De cada malla sale un asiento: centro en x, fondo y frente en z, piso y cabeza en y, y el lado por el nombre
// del material. Una malla de suplentes mas ancha que una persona (estadios de mods que las juntan) se parte en piezas conectadas,
// se juntan las que se tocan y cada grupo da un asiento cada 0,62 m. Se guardan por estadio ("stNNN") al cargar el fmdl.
// (2d) bLo / bHi: lo mas adelante / atras en z de la figura en la franja del asiento (0,38..0,65 m sobre sus pies) = la cola;
// en BuildSpots se toma el lado del respaldo. NAN si la figura no tiene vertices ahi.
struct Seat { float x, z0, z1, y0, y1; int side; float bLo, bHi; };
struct SeatSet { char pre[8] = {}; ULONGLONG t = 0; int n = 0; Seat s[64]; unsigned seq = 0; };
std::mutex s_seatMx; SeatSet s_seats;
bool SeatAdd(const char* pre, const Seat& s) {
    std::lock_guard<std::mutex> lk(s_seatMx); const ULONGLONG t = GetTickCount64();
    if (strncmp(s_seats.pre, pre, 5) || t - s_seats.t > 60000) { s_seats.n = 0; memcpy(s_seats.pre, pre, 5); s_seats.pre[5] = 0; s_seats.seq++; }
    s_seats.t = t;
    for (int i = 0; i < s_seats.n; i++) {
        const Seat& o = s_seats.s[i];
        if (fabsf(o.x - s.x) < 0.25f && fabsf((o.z0 + o.z1) - (s.z0 + s.z1)) * 0.5f < 0.4f) return false;   // ya estaba (el fmdl pasa dos veces)
    }
    if (s_seats.n >= 64) return false;
    s_seats.s[s_seats.n++] = s; s_seats.seq++; return true;
}
// se esta cargando un estadio: los asientos de otro ya no valen
void SeatStadium(const char* pre) {
    std::lock_guard<std::mutex> lk(s_seatMx);
    if (strncmp(s_seats.pre, pre, 5)) { s_seats.n = 0; memcpy(s_seats.pre, pre, 5); s_seats.pre[5] = 0; s_seats.seq++; }
}
bool StrHas(const char* s, size_t len, const char* w) {
    const size_t n = strlen(w);
    for (size_t i = 0; i + n <= len; i++) { size_t j = 0; while (j < n && (char)tolower((unsigned char)s[i + j]) == w[j]) j++; if (j == n) return true; }
    return false;
}
const float kSeatMinH = 0.85f, kSeatMaxH = 1.75f, kSeatOneW = 1.05f, kSeatPitch = 0.62f;
// (2d, Agustin 17/11: "no apoyados del todo bien en las sillas") la cola de la figura de Konami: los vertices entre 0,38 y 0,65 m
// sobre sus pies dentro de la caja [xa, xb] x [za, zb]; lo y hi = sus z extremos (NAN si no hay). Medido en st009_sc2020 (22
// figuras): el alto del asiento queda a 0,40 m de los pies y en 15 de 22 la cola es lo mas atras de la figura (las que se apoyan
// en el respaldo con la espalda la tienen 0,10..0,27 m mas adelante).
void ButtBand(const FmMesh& m, float y0, float xa, float xb, float za, float zb, float& lo, float& hi) {
    lo = INFINITY; hi = -INFINITY;
    for (uint32_t v = 0; v < m.nv; v++) {
        float q[3]; memcpy(q, m.pos + (size_t)v * m.stride, 12);
        if (q[0] < xa || q[0] > xb || q[2] < za || q[2] > zb || q[1] < y0 + 0.38f || q[1] > y0 + 0.65f) continue;
        lo = std::min(lo, q[2]); hi = std::max(hi, q[2]);
    }
    if (lo > hi) lo = hi = NAN;
}
unsigned SeatsFromMeshes(const Fm& f, const std::vector<FmMesh>& ms, const char* pre) {
    unsigned added = 0;
    for (const FmMesh& m : ms) {
        if (!m.bench || !m.pos || !m.nv) continue;
        size_t len = 0; const uint8_t* me = f.E(4, m.mat, 0x10); const char* nm = me ? FmStr(f, U<uint16_t>(me), len) : nullptr;
        const bool isSub = nm && StrStarts(nm, len, "so_benchmember");
        const int side = !nm ? -1 : StrHas(nm, len, "home") ? 0 : StrHas(nm, len, "away") ? 1 : -1;
        float mn[3], mx[3];
        for (uint32_t v = 0; v < m.nv; v++) {
            float q[3]; memcpy(q, m.pos + (size_t)v * m.stride, 12);
            for (int i = 0; i < 3; i++) { if (!v || q[i] < mn[i]) mn[i] = q[i]; if (!v || q[i] > mx[i]) mx[i] = q[i]; }
        }
        const float wx = mx[0] - mn[0], h = mx[1] - mn[1], wz = mx[2] - mn[2];
        if (wx <= kSeatOneW) {   // una figura = una malla (Konami)
            if (h >= kSeatMinH && h <= kSeatMaxH && wx >= 0.3f && wz >= 0.3f && wz <= 1.8f) {
                float bLo, bHi; ButtBand(m, mn[1], mn[0], mx[0], mn[2], mx[2], bLo, bHi);
                if (SeatAdd(pre, Seat{ (mn[0] + mx[0]) * 0.5f, mn[2], mx[2], mn[1], mx[1], side, bLo, bHi })) added++;
            }
            continue;
        }
        if (!isSub || !m.idx || m.nv > 60000) continue;   // utileria larga (microfonos) o demasiado grande
        // varias figuras en una malla: piezas conectadas -> grupos que se tocan en x / z
        std::vector<uint32_t> par(m.nv); for (uint32_t v = 0; v < m.nv; v++) par[v] = v;
        auto fd = [&](uint32_t a) { while (par[a] != a) { par[a] = par[par[a]]; a = par[a]; } return a; };
        std::vector<uint8_t> used(m.nv, 0);
        for (uint32_t t = 0; t + 2 < m.nidx; t += 3) {
            const uint32_t a = m.idx[t], b = m.idx[t + 1], c = m.idx[t + 2];
            if (a >= m.nv || b >= m.nv || c >= m.nv || (a == b && b == c)) continue;
            used[a] = used[b] = used[c] = 1;
            uint32_t ra = fd(a), rb = fd(b); if (ra != rb) par[rb] = ra;
            ra = fd(a); const uint32_t rc = fd(c); if (ra != rc) par[rc] = ra;
        }
        struct Box { float mn[3], mx[3]; };
        std::vector<Box> pcs; std::vector<int> slot(m.nv, -1);
        for (uint32_t v = 0; v < m.nv; v++) {
            if (!used[v]) continue;
            const uint32_t r = fd(v); float q[3]; memcpy(q, m.pos + (size_t)v * m.stride, 12);
            if (slot[r] < 0) { slot[r] = (int)pcs.size(); pcs.push_back(Box{ { q[0], q[1], q[2] }, { q[0], q[1], q[2] } }); }
            Box& b = pcs[slot[r]]; for (int i = 0; i < 3; i++) { b.mn[i] = std::min(b.mn[i], q[i]); b.mx[i] = std::max(b.mx[i], q[i]); }
        }
        const size_t n = pcs.size(); if (!n || n > 4000) continue;
        std::vector<int> gp(n); for (size_t i = 0; i < n; i++) gp[i] = (int)i;
        auto gf = [&](int a) { while (gp[a] != a) { gp[a] = gp[gp[a]]; a = gp[a]; } return a; };
        for (size_t i = 0; i < n; i++) for (size_t j = i + 1; j < n; j++) {
            const Box& a = pcs[i]; const Box& b = pcs[j];
            if (a.mn[0] - 0.03f <= b.mx[0] && b.mn[0] - 0.03f <= a.mx[0] && a.mn[2] - 0.03f <= b.mx[2] && b.mn[2] - 0.03f <= a.mx[2]) {
                const int ra = gf((int)i), rb = gf((int)j); if (ra != rb) gp[rb] = ra;
            }
        }
        std::vector<Box> g(n); std::vector<uint8_t> init(n, 0);
        for (size_t i = 0; i < n; i++) {
            const int r = gf((int)i);
            if (!init[r]) { g[r] = pcs[i]; init[r] = 1; }
            for (int a = 0; a < 3; a++) { g[r].mn[a] = std::min(g[r].mn[a], pcs[i].mn[a]); g[r].mx[a] = std::max(g[r].mx[a], pcs[i].mx[a]); }
        }
        for (size_t i = 0; i < n; i++) {
            if (!init[i]) continue;
            const Box& b = g[i]; const float gw = b.mx[0] - b.mn[0], gh = b.mx[1] - b.mn[1], gz = b.mx[2] - b.mn[2];
            if (gh < kSeatMinH || gh > kSeatMaxH || gz < 0.3f || gw < 0.3f) continue;
            const int k = gw <= kSeatOneW ? 1 : std::min(20, std::max(1, (int)(gw / kSeatPitch + 0.5f)));
            for (int q = 0; q < k; q++) {
                float bLo, bHi; ButtBand(m, b.mn[1], b.mn[0] + q * gw / k, b.mn[0] + (q + 1) * gw / k, b.mn[2], b.mx[2], bLo, bHi);
                if (SeatAdd(pre, Seat{ b.mn[0] + (q + 0.5f) * gw / k, b.mn[2], b.mx[2], b.mn[1], b.mx[1], side, bLo, bHi })) added++;
            }
        }
    }
    return added;
}

// Reparto: los asientos de un lado (por material; si no dice, x < 0 = local) se ordenan por fila (la de adelante primero: fondo
// mas cerca de la cancha) y, dentro de la fila, del medio de la cancha hacia afuera. Cada fila usa el piso y el fondo del medio de
// sus figuras. Si hay mas suplentes que figuras, se reparten parejo a lo ancho de cada fila (no menos de 0,50 m entre dos).
struct Spot { float x, y, z, yaw; };
int BuildSpots(const SeatSet& ss, int side, int want, float upCm, float fwdCm, Spot* out, int max, int* seatsOut, int* rowsOut, char* diag = nullptr, size_t dn = 0) {
    if (seatsOut) *seatsOut = 0; if (rowsOut) *rowsOut = 0; if (diag && dn) diag[0] = 0;
    if (ss.n <= 0 || want <= 0) return 0;
    float zc = 0; for (int i = 0; i < ss.n; i++) zc += (ss.s[i].z0 + ss.s[i].z1) * 0.5f; zc /= ss.n;
    const float dir = zc > 0 ? -1.0f : 1.0f;                    // hacia la cancha
    const float yaw = zc > 0 ? 180.0f : 0.0f;                   // el jugador mira a +z con giro 0
    struct S { float x, back, y0, butt; }; S s[64]; int n = 0;
    for (int i = 0; i < ss.n; i++) {
        const Seat& a = ss.s[i]; const int sd = a.side >= 0 ? a.side : (a.x < 0 ? 0 : 1);
        if (sd != side) continue;
        s[n++] = S{ a.x, dir < 0 ? a.z1 : a.z0, a.y0, dir < 0 ? a.bHi : a.bLo };
    }
    if (seatsOut) *seatsOut = n;
    if (!n) return 0;
    std::sort(s, s + n, [](const S& a, const S& b) { return fabsf(a.back) < fabsf(b.back); });
    struct Row { int first, count, nb; float back, floor, x0, x1, butt; }; Row rows[8]; int nr = 0;
    for (int i = 0; i < n; ) {
        int j = i + 1; while (j < n && fabsf(s[j].back) - fabsf(s[i].back) <= 0.45f) j++;
        if (nr < 8) {
            Row& r = rows[nr++]; r.first = i; r.count = j - i;
            float tb[64], tf[64]; for (int k = i; k < j; k++) { tb[k - i] = s[k].back; tf[k - i] = s[k].y0; }
            std::sort(tb, tb + r.count); std::sort(tf, tf + r.count);
            r.back = tb[r.count / 2]; r.floor = tf[r.count / 2];
            // (2d) la cola de la fila: el cuartil del lado del respaldo de las colas medidas (las figuras que se tiran atras con la
            // espalda la tienen mas adelante y no cuentan); una cola a mas de 0,45 m del fondo de la fila no es de esta fila
            float tc[64]; r.nb = 0;
            for (int k = i; k < j; k++) if (!std::isnan(s[k].butt) && fabsf(s[k].butt - r.back) <= 0.45f) tc[r.nb++] = s[k].butt;
            std::sort(tc, tc + r.nb);
            r.butt = r.nb ? tc[dir < 0 ? (r.nb * 3) / 4 : (r.nb - 1) / 4] : NAN;
            std::sort(s + i, s + j, [](const S& a, const S& b) { return fabsf(a.x) < fabsf(b.x); });
            r.x0 = r.x1 = s[i].x; for (int k = i; k < j; k++) { r.x0 = std::min(r.x0, s[k].x); r.x1 = std::max(r.x1, s[k].x); }
        }
        i = j;
    }
    if (rowsOut) *rowsOut = nr;
    // (2d) cadera (la raiz del clip: la pista 3 de los clips sentados no corre la cadera en x / z) a kHipButt de la cola de la fila,
    // hacia la cancha. Antes iba a 0,25 m de la espalda de la figura: con la cola en el respaldo quedaba 15..19 cm adelante, en el
    // borde de la silla (capturas 17/11). Sin colas medidas, como antes.
    const float kHipButt = 0.09f, kBackOld = 0.25f;
    auto hipZ = [&](const Row& r) { return std::isnan(r.butt) ? r.back + dir * (kBackOld + fwdCm * 0.01f) : r.butt + dir * (kHipButt + fwdCm * 0.01f); };
    if (diag && dn) {
        size_t pos = 0;
        for (int r = 0; r < nr && pos + 1 < dn; r++) {
            const int w = _snprintf_s(diag + pos, dn - pos, _TRUNCATE, "%sfila %d: fondo z %.2f, cola z %.2f (%d de %d figuras), cadera z %.2f, piso y %.2f", r ? "; " : "", r + 1,
                                      rows[r].back, rows[r].butt, rows[r].nb, rows[r].count, hipZ(rows[r]), rows[r].floor);
            pos = w < 0 ? dn : pos + (size_t)w;
        }
    }
    int o = 0;
    if (want <= n) {
        for (int r = 0; r < nr && o < want && o < max; r++)
            for (int k = 0; k < rows[r].count && o < want && o < max; k++)
                out[o++] = Spot{ s[rows[r].first + k].x, rows[r].floor + upCm * 0.01f, hipZ(rows[r]), yaw };
        return o;
    }
    int left = want;
    for (int r = 0; r < nr && o < max; r++) {
        const Row& w = rows[r]; const float a = w.x0 - 0.31f, b = w.x1 + 0.31f, width = b - a;
        int t = (r == nr - 1) ? left : (int)((float)want * w.count / n + 0.5f);
        t = std::min(std::min(t, left), std::max(w.count, (int)(width / 0.50f)));
        // del medio de la cancha hacia afuera
        const bool inner0 = fabsf(a) < fabsf(b);
        for (int k = 0; k < t && o < max; k++) {
            const float u = (k + 0.5f) / t;
            out[o++] = Spot{ inner0 ? a + u * width : b - u * width, w.floor + upCm * 0.01f, hipZ(w), yaw };
        }
        left -= t;
    }
    return o;
}

__declspec(noinline) void ProcessFmdlInner(uint8_t* p, size_t avail) {
    {
        Fm f; if (!FmOpen(f, p, avail)) return;
        char pre[16], part[40]; if (!FmStadium(f, pre, part, sizeof part)) return;   // no es espantapajaros de estadio
        std::vector<FmMesh> ms; if (!FmMeshes(f, ms)) { FmLog("banca: %s_%s: no se pudo leer el fmdl, sin tocar", pre, part); return; }
        const bool isBench = !strcmp(part, "dir_bench");
        FmBenchFlags(f, ms, isBench);
        float mn[3] = {}, mx[3] = {};
        if (FmBenchBox(ms, mn, mx)) BoxPut(pre, mn, mx);
        SeatStadium(pre);
        const unsigned seatsNew = SeatsFromMeshes(f, ms, pre);   // paso 2a (antes de esconder: usa las caras de las mallas)
        if (isBench) { FmLog("banca: %s_dir_bench cargado: caja de los suplentes x %.2f..%.2f z %.2f..%.2f, %u asientos", pre, mn[0], mx[0], mn[2], mx[2], seatsNew); return; }
        if (seatsNew) FmLog("banca: %s_%s: %u asientos mas (mallas de suplente fuera del _dir_bench)", pre, part, seatsNew);
        // a) mallas de suplente fuera del _dir_bench
        unsigned benchMeshes = 0, tris = 0;
        for (auto& m : ms) if (m.bench && m.idx) {
            for (uint32_t t = 0; t + 2 < m.nidx; t += 3) if (m.idx[t + 1] != m.idx[t] || m.idx[t + 2] != m.idx[t]) { m.idx[t + 1] = m.idx[t + 2] = m.idx[t]; tris++; }
            benchMeshes++;
        }
        // b) figuras sentadas en la caja del banco
        const char* src = "de este estadio";
        if (!BoxGet(pre, mn, mx)) { if (ScanBenchNear(p, pre, mn, mx)) { BoxPut(pre, mn, mx); src = "buscada en el paquete"; } else src = nullptr; }
        unsigned figs = 0, kept = 0, bottles = 0;
        if (src) {
            struct Piece { uint16_t mesh; uint32_t root; float mn[3], mx[3]; int grp; };
            std::vector<Piece> pcs; std::vector<std::vector<uint32_t>> rootOf(ms.size());
            const float ex = kMargin + 1.0f;
            for (size_t k = 0; k < ms.size(); k++) {
                FmMesh& m = ms[k]; if (m.bench || !m.pos || !m.idx || !m.nv) continue;
                std::vector<uint32_t> par(m.nv); for (uint32_t v = 0; v < m.nv; v++) par[v] = v;
                auto fd = [&](uint32_t a) { while (par[a] != a) { par[a] = par[par[a]]; a = par[a]; } return a; };
                std::vector<uint8_t> used(m.nv, 0);
                for (uint32_t t = 0; t + 2 < m.nidx; t += 3) {
                    const uint32_t a = m.idx[t], b = m.idx[t + 1], c = m.idx[t + 2];
                    if (a >= m.nv || b >= m.nv || c >= m.nv || (a == b && b == c)) continue;
                    used[a] = used[b] = used[c] = 1;
                    uint32_t ra = fd(a), rb = fd(b); if (ra != rb) par[rb] = ra;
                    ra = fd(a); const uint32_t rc = fd(c); if (ra != rc) par[rc] = ra;
                }
                std::vector<int> slot(m.nv, -1); const size_t first = pcs.size();
                std::vector<uint32_t>& ro = rootOf[k]; ro.assign(m.nv, 0xffffffffu);
                for (uint32_t v = 0; v < m.nv; v++) {
                    if (!used[v]) continue;
                    const uint32_t r = fd(v); ro[v] = r; float q[3]; memcpy(q, m.pos + (size_t)v * m.stride, 12);
                    if (slot[r] < 0) { slot[r] = (int)pcs.size(); Piece pc{ (uint16_t)k, r, { q[0], q[1], q[2] }, { q[0], q[1], q[2] }, -1 }; pcs.push_back(pc); }
                    Piece& pc = pcs[slot[r]]; for (int i = 0; i < 3; i++) { pc.mn[i] = std::min(pc.mn[i], q[i]); pc.mx[i] = std::max(pc.mx[i], q[i]); }
                }
                // solo piezas chicas cerca del banco
                size_t w = first;
                for (size_t i = first; i < pcs.size(); i++) {
                    const Piece& pc = pcs[i]; const float cx = (pc.mn[0] + pc.mx[0]) * 0.5f, cz = (pc.mn[2] + pc.mx[2]) * 0.5f;
                    if (pc.mx[0] - pc.mn[0] > kMaxFoot || pc.mx[2] - pc.mn[2] > kMaxFoot) continue;
                    if (cx < mn[0] - ex || cx > mx[0] + ex || cz < mn[2] - ex || cz > mx[2] + ex) continue;
                    pcs[w++] = pc;
                }
                pcs.resize(w);
            }
            // figuras: piezas (de cualquier malla) cuyas cajas x / z se tocan (3 cm)
            const size_t n = pcs.size(); std::vector<int> par(n); for (size_t i = 0; i < n; i++) par[i] = (int)i;
            auto fd = [&](int a) { while (par[a] != a) { par[a] = par[par[a]]; a = par[a]; } return a; };
            for (size_t i = 0; i < n; i++) for (size_t j = i + 1; j < n; j++) {
                const Piece& a = pcs[i]; const Piece& b = pcs[j];
                if (a.mn[0] - 0.03f <= b.mx[0] && b.mn[0] - 0.03f <= a.mx[0] && a.mn[2] - 0.03f <= b.mx[2] && b.mn[2] - 0.03f <= a.mx[2]) {
                    const int ra = fd((int)i), rb = fd((int)j); if (ra != rb) par[rb] = ra;
                }
            }
            struct Fig { float mn[3], mx[3]; bool person = false, init = false, bottle = false; };
            std::vector<Fig> fg(n);
            for (size_t i = 0; i < n; i++) {
                Fig& g = fg[fd((int)i)]; const Piece& pc = pcs[i];
                if (!g.init) { memcpy(g.mn, pc.mn, 12); memcpy(g.mx, pc.mx, 12); g.init = true; }
                for (int a = 0; a < 3; a++) { g.mn[a] = std::min(g.mn[a], pc.mn[a]); g.mx[a] = std::max(g.mx[a], pc.mx[a]); }
                if (!IsPropMat(f, ms[pc.mesh].mat)) g.person = true;
                if (IsBottleMat(f, ms[pc.mesh].mat)) g.bottle = true;
            }
            std::vector<std::vector<uint8_t>> hide(ms.size());
            for (size_t i = 0; i < n; i++) {
                if (fd((int)i) != (int)i) continue;
                const Fig& g = fg[i]; const float cx = (g.mn[0] + g.mx[0]) * 0.5f, cz = (g.mn[2] + g.mx[2]) * 0.5f;
                if (cx < mn[0] - kMargin || cx > mx[0] + kMargin || cz < mn[2] - kMargin || cz > mx[2] + kMargin) continue;
                const float h = g.mx[1] - g.mn[1], foot = std::max(g.mx[0] - g.mn[0], g.mx[2] - g.mn[2]);
                const bool sit = g.person && g.mn[1] <= mn[1] + kGround && h >= kMinH && h <= kMaxH && foot >= kMinFoot;
                if (!sit) {
                    if (g.person) { kept++; continue; }
                    // 1i (prueba 1h, 21:48): en el Sports Park quedaron las botellas que tenian en la mano o al lado los suplentes
                    // genericos (el suplente era una malla de (a) y la botella otra malla de utileria). Botella suelta dentro de la
                    // caja del banco (+0.6 m, hasta 0.3 m sobre el techo de la caja): se esconden SOLO sus piezas de botella (si
                    // toca una conservadora u otra utileria, esa queda). Las botellas del resto del estadio quedan.
                    if (!g.bottle || (g.mn[1] + g.mx[1]) * 0.5f > mx[1] + 0.3f) continue;
                    bottles++;
                    for (size_t j = 0; j < n; j++) if (fd((int)j) == (int)i && IsBottleMat(f, ms[pcs[j].mesh].mat)) {
                        std::vector<uint8_t>& hv = hide[pcs[j].mesh]; if (hv.empty()) hv.assign(ms[pcs[j].mesh].nv, 0);
                        hv[pcs[j].root] = 1;
                    }
                    continue;
                }
                figs++;
                if (figs <= 6) FmLog("banca: %s_%s: figura sentada en el banco escondida x %.2f..%.2f z %.2f..%.2f alto %.2f", pre, part, g.mn[0], g.mx[0], g.mn[2], g.mx[2], h);
                for (size_t j = 0; j < n; j++) if (fd((int)j) == (int)i) {
                    std::vector<uint8_t>& hv = hide[pcs[j].mesh]; if (hv.empty()) hv.assign(ms[pcs[j].mesh].nv, 0);
                    hv[pcs[j].root] = 1;
                }
            }
            for (size_t k = 0; k < ms.size(); k++) {
                if (hide[k].empty()) continue;
                FmMesh& m = ms[k]; const std::vector<uint32_t>& ro = rootOf[k];
                // la marca va en la raiz de la pieza; cada vertice usado sabe su raiz (primera pasada)
                std::vector<uint8_t> hv(m.nv, 0); for (uint32_t v = 0; v < m.nv; v++) if (ro[v] != 0xffffffffu && hide[k][ro[v]]) hv[v] = 1;
                for (uint32_t t = 0; t + 2 < m.nidx; t += 3) {
                    const uint32_t a = m.idx[t], b = m.idx[t + 1], c = m.idx[t + 2];
                    if (a >= m.nv || b >= m.nv || c >= m.nv || (a == b && b == c)) continue;
                    if (hv[a] || hv[b] || hv[c]) { m.idx[t + 1] = m.idx[t + 2] = m.idx[t]; tris++; }
                }
            }
        }
        FmLog("banca: %s_%s: %u figuras sentadas, %u botellas del banco y %u mallas de suplente escondidas (%u triangulos), %u personas paradas quedan; caja del banco %s",
                  pre, part, figs, bottles, benchMeshes, tris, kept, src ? src : "NO hallada (solo mallas de suplente)");
    }
}
void ProcessFmdl(void* fileRef) {
    if (!(Live(0, 0.0f) > 0.5f && Live(1, 1.0f) > 0.5f)) return;
    uintptr_t file = 0, data = 0; int st = 0;
    if (!Rd((uintptr_t)fileRef, file) || !Heap(file) || !Rd(file + 0x40, st) || st != 2 || !Rd(file + 0x60, data) || !Heap(data)) return;
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery((void*)data, &mi, sizeof mi) || mi.State != MEM_COMMIT || !(mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) || (mi.Protect & PAGE_GUARD)) return;
    uint32_t magic = 0; if (!Rd(data, magic) || magic != 0x4c444d46u) return;
    const size_t avail = (uintptr_t)mi.BaseAddress + mi.RegionSize - data;
    __try { ProcessFmdlInner((uint8_t*)data, avail); }
    __except (EXCEPTION_EXECUTE_HANDLER) { FmLog("banca: error al leer un fmdl de estadio (%p): sin tocar", (void*)data); }
}
void __fastcall hk_FmdlUp1(void* mdl, void* fileRef) { ProcessFmdl(fileRef); o_fmdlUp1(mdl, fileRef); }
uint64_t __fastcall hk_FmdlUp2(void* mdl, void* fileRef) { ProcessFmdl(fileRef); return o_fmdlUp2(mdl, fileRef); }

// solo lectura (desde Present): todos los cuerpos "_dir_bench" de las escenas y su estado
struct BenchSeen { uintptr_t body; uint8_t node, vis; uint32_t drawFlags; bool drawOk; char name[40]; };
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
                            if (!Rd(funs + 8 * i, f) || !Heap(f) || !Rd(f, fvt) || fvt != kVtFunctor || !Rd(f + 0x10, c) || !Heap(c)) continue;
                            char nm[40]; if (!IsBenchBody(c, nm, sizeof nm)) continue;
                            BenchSeen& b = out[n++]; strncpy_s(b.name, nm, _TRUNCATE); b.body = c; b.node = b.vis = 0xff; b.drawFlags = 0; b.drawOk = false;
                            Rd(c + 0x80, b.node); Rd(c + 0x81, b.vis);
                            uintptr_t d = 0; if (Rd(c + 0x88, d) && Heap(d) && Rd(d + 0x1b4, b.drawFlags)) b.drawOk = true;
                            if (Heap(d) && n <= 8) s_benchDraw[n - 1].store(d, std::memory_order_relaxed);
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
    for (int i = n < 8 ? n : 8; i < 8; i++) s_benchDraw[i].store(0, std::memory_order_relaxed);   // los de otro estadio ya no valen
    return n;
}

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
    s_fdmThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
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

// ---------------------------------------------------------------- 16/11 paso 2a: suplentes reales sentados
// Lo que hace (fila 516 x e y prendidas y fila 517 x = 1), todo en el hilo del juego:
//  1) Reloj: match::Human vf19 (0x1408bf4c0), la funcion con la que cada jugador del partido manda su pose a la escena en cada
//     cuadro. Se usa la del jugador 0. FixDemoMain::vf3 (el reloj del paso 1) no corre en el juego (medido: 0 por segundo).
//     Solo se trabaja si ese hilo es el mismo que el de las cinematicas (donde el juego crea y mueve a sus jugadores sueltos);
//     si no coincide se prueba con el de la escena del estadio (FUN_1419b79f0) y, si tampoco, no se crea nada y queda anotado.
//  2) Convocados de verdad (doc 26): equipo = [[0x143702208] + 0x1978 + lado * 0x28]; orden u32[40] en +4; jugador = equipo
//     + 0xa8 + idx * 0x128 + 0x30 (0xF0 B); convocados u8 en +0x2eec. Suplentes = lugares 11 .. convocados - 1.
//  3) Asientos: donde estaban las figuras de Konami de ese lado (arriba).
//  4) Clips: 7 de los 9 "sentado" del vestuario (dml_md_lockerSpeech_001_sit_00N; el 1 y el 6 terminan corridos 8 y 5 cm y
//     saltarian al repetir). Medido en los archivos: la raiz queda en el origen (se mueve menos de 1,5 cm) y la cadera baja
//     47 a 55 cm, asi que el asiento se le da directo al jugador. Se piden como los pide la cinematica (FUN_141b079e0 /
//     FUN_141eb16e0) y, para que ninguna cinematica los descargue mientras estan en uso, se frena su descarga (FUN_141b08080).
//  5) Cada suplente es un FixDemoHuman del juego (el jugador suelto de las cinematicas): FUN_141f85e40, LoadId con
//     FUN_141f61520 (pechera de suplente = 2 en el ultimo argumento), FUN_141f86c30 y sondeo FUN_141f87230. (2b) Hasta 8 cargando
//     a la vez, cada uno arranca en un punto al azar de su clip.
//     Cuando cargo: posicion y giro en fd+8 / +0x14, FUN_141afa060 (numero del clip en el recurso "fixdemo") y vf1.
//  6) Cada cuadro: vf4 (avanza el clip) y FUN_141f86d40 (manda la pose), como el reproductor de cinematicas.
//  7) Se sueltan todos (vf0 con el heap 0x19) antes de que el juego borre a sus jugadores (gancho en el destructor de
//     match::Human, FUN_1408be4f0), al apagar la opcion o si cambia el partido. El que entra a la cancha deja el banco.
//  8) (2b) Pedido de Agustin (16/11 23:26): que esten sentados al empezar el partido aunque la carga tarde mas. Reloj = actualizar el
//     modo partido (FUN_141fc70d0, corre en la carga, el partido y las cinematicas; el de los jugadores queda de respaldo). La lista
//     se arma cuando match::Match empieza a crear a los jugadores (estado 6) y, sin entrada (paso 0x0f), match::Match::vf1 no se
//     llama en el estado 0x1c (jugadores esperando su carga) mientras falten suplentes: la pantalla de carga sigue hasta que estan
//     todos (tope 45 s, o 15 s sin avance). Con la entrada (paso 0x0e) la carga cierra antes y no se frena. En la carga el
//     destructor de match::Human no los suelta (el juego puede borrar jugadores al armar el suyo).
// Detalle y C reconstruido: /mnt/project-files/banca_suplentes/20261116_como_arma_el_banco.cpp.md, secciones 26 a 28.
const int kRow2 = 517;   // x suplentes reales, y % con pechera, z altura del asiento (cm), w adelante / atras (cm)
float Live2(int k, float def) { return (!g_cfg.info[kRow2][k].defined && g_cfg.live[kRow2][k] == 0.0f) ? def : g_cfg.live[kRow2][k]; }
const int kRow3 = 518;   // (2e) x ropa (0 segun el clima, 1 pechera, 2 campera), y que pechera (0 la del juego, 1 la de cada equipo), z probar clips, w clips de la sala
float Live3(int k, float def) { return (!g_cfg.info[kRow3][k].defined && g_cfg.live[kRow3][k] == 0.0f) ? def : g_cfg.live[kRow3][k]; }

const uintptr_t kMatchPtr = 0x143702208ull;
const uintptr_t kVtDrawHuman = 0x14261c078ull;                       // draw::util::Human
// match::Player::vf5 (0x140925710): lo que hace cada jugador del partido en cada cuadro para dibujarse (termina en
// FUN_1408bf160 -> FUN_141ec8980, mandar la pose). push rbx; sub rsp,30h; mov rbx,rcx; mov rcx,[rcx+7d0h]; test rcx,rcx; je
// (16/11 2a-2: antes match::Human::vf19 0x1408bf4c0, que en la prueba no corrio nunca durante el partido)
const uintptr_t kHumanPush = 0x140925710ull;
const unsigned char kHumanPushSig[21] = { 0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0xd9,0x48,0x8b,0x89,0xd0,0x07,0x00,0x00,0x48,0x85,0xc9,0x0f,0x84 };
// FUN_1408be4f0 (destructor base de match::Human): push rdi; sub rsp,30h; mov [rsp+20h],-2; mov [rsp+50h],rbx; mov [rsp+..],rsi
const uintptr_t kHumanDtor = 0x1408be4f0ull;
const unsigned char kHumanDtorSig[20] = { 0x40,0x57,0x48,0x83,0xec,0x30,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48,0x89,0x5c,0x24,0x50 };
// FUN_141b08080 (cobra::And::ResManager: descargar por hash): push rdi; sub rsp,30h; mov [rsp+20h],-2; mov [rsp+48h],rbx
const uintptr_t kResUnload = 0x141b08080ull;
const unsigned char kResUnloadSig[20] = { 0x40,0x57,0x48,0x83,0xec,0x30,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48,0x89,0x5c,0x24,0x48 };
// funciones del juego que se llaman (mismo exe: sello kStamp)
const uintptr_t kAlloc = 0x1403fb460ull, kHeapPush = 0x1403fb780ull, kHeapPop = 0x1403fb760ull;
const uintptr_t kFdCtor = 0x141f85e40ull, kLoadId = 0x141f61520ull, kFdLoad = 0x141f86c30ull, kFdPoll = 0x141f87230ull;
const uintptr_t kFdPush = 0x141f86d40ull, kFdVf4 = 0x141f87340ull, kFdVf0 = 0x141f863f0ull;
const uintptr_t kResMgr = 0x141b08cf0ull, kResFind = 0x141b079e0ull, kGaniQueue = 0x141eb16e0ull, kGaniPump = 0x141eb0f50ull, kStrAssign = 0x1404023c0ull;
const uintptr_t kAnimResMgr = 0x141ed1770ull, kAnimResFind = 0x141ed1700ull, kClipIdx = 0x141afa060ull;
const uintptr_t kClipLen = 0x141af8be0ull, kClipTime = 0x141af8910ull, kClipSeek = 0x141afa7b0ull;
// (2a-3) FUN_141ec8320(draw::util::Human*, bool): mostrar / esconder (h+0x5a, modelo+0x25 y orden 0x510 al hilo que dibuja).
// Es lo que hace match::Human::vf14 (0x1408c0140) cada cuadro y lo que hace la cinematica al activar a un jugador
// (FUN_141f45950). Sin esto el suplente carga y se mueve pero no se dibuja (prueba 2a-2: 24 sentados, ninguno visible).
const uintptr_t kHumanShow = 0x141ec8320ull;
typedef void(__fastcall* HumanShowFn)(void*, uint8_t);
// (2c) cara y manos. El cuerpo sale de vf1 / vf4 / FUN_141f86d40, pero los huesos de la cara y de los dedos son otro esqueleto
// (rig de cara y de manos) que recibe su pose aparte: draw::util::Human no la arma solo. Sin ella quedan sin pose y la piel de la
// cara y las manos se estira hacia cualquier lado (prueba 2a-3: caras y manos estiradas; parpados -87 grados contra -2..-10).
//  - La cinematica, despues de vf1, llama FixDemoHuman::vf3 (0x141f882b0, vtable + 0x18): crea en fd+0x58 el controlador de cara y
//    manos (0x418 B en el heap 0x19, FUN_141ebf3c0(c, 2): [0] cara, [1] [2] manos) y pone fd+0x6a (usar) / fd+0x6c (clip de cara).
//    vf2 (la llama vf1) lo borra, por eso va despues. El destructor de FixDemoHuman (FUN_141f86290) lo suelta.
//  - Los jugadores del partido, cada cuadro, en match::Player::vf10 (0x140922b40) y el arbitro en Referee::vf10 (0x140a36fa0):
//    FUN_141ec7b10(humano, controlador, 0) -> FUN_141ec1240 -> FUN_141ec4970(c, modelo, 1, 0): cara y manos en la pose neutra de
//    las tablas del juego (DAT_142b74020 / 74220 cara, 74420 / 746a0 y 74920 / 74a60 manos) y se mandan al modelo (orden 0x514 y
//    las de las manos).
//  Aca: el controlador se crea con vf3(fd, 0, 0) (fd+0x6a = 0: la cinematica no le pone clip de cara) y cada cuadro se manda la
//  pose neutra como los jugadores del partido.
const uintptr_t kFdVf3 = 0x141f882b0ull, kFaceNeutral = 0x141ec7b10ull;
// (2d) FUN_141f88270(fd, k): la cinematica baja la base por la estatura del modelo: fd+0xc += (h+0x10 - 180) * -0.01 * k (k sale del
// corte, +0x124 del registro de la persona; lo llama FUN_141f45950 al activar a cada jugador). Sentado, un modelo mas alto lleva la cola
// mas arriba (s = altura / 180 escala el esqueleto): para dejarla en el asiento (0,40 m sobre los pies, figuras de Konami) se baja
// la base (s - 1) * 0,40 m = (altura - 180) * 0,0022 m -> k = 0,22.
const uintptr_t kFdHeight = 0x141f88270ull;
typedef void(__fastcall* FdHeightFn)(void*, float);
const float kStatureK = 0.22f;
const uintptr_t kFaceResPtr = 0x143802ff8ull, kHandResPtr = 0x143803000ull;   // recursos que usa FUN_141ebf3c0 (cara 9 capas, manos)
typedef void(__fastcall* FdVf3Fn)(void*, uint8_t, uint32_t, uint64_t);
typedef void(__fastcall* FaceNeutralFn)(void*, void*, uint8_t);
std::atomic<unsigned> s_faceMade{0}, s_faceFail{0};
// (2b) crearlos en la pantalla de carga y que el partido arranque con todos sentados:
// FUN_141fc70d0 = actualizar el modo partido (mensaje 2 de game_mode::MatchListener::vf2 0x141fc7e90): corre en la carga, en el
// partido y en las cinematicas, y es el reloj de los suplentes. mov rax,rsp; push rdi; push r14; push r15; sub rsp,60h; mov [rax-58h],-2; mov [rax+8],rbx
const uintptr_t kModeUpd = 0x141fc70d0ull;
const unsigned char kModeUpdSig[24] = { 0x48,0x8b,0xc4,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x48,0xc7,0x40,0xa8,0xfe,0xff,0xff,0xff,0x48,0x89,0x58,0x08 };
// match::Match::vf1 (0x14041b380, vtable 0x14259fea8): estado en this+8; 6..0x1b crea a los 22 jugadores, 0x1c espera que carguen,
// 0x1d listo. El paso 0x0f de FUN_141fde040 la llama cada cuadro (vuelve a 0x141fde4c8) y recien con 0x1d cierra la pantalla de carga
// (FUN_140e4cb70) y arranca el partido. Para frenar el arranque no se la llama mientras esta en 0x1c y faltan suplentes (con tope).
// push rbp; push rsi; push rdi; sub rsp,70h; mov [rsp+38h],-2; mov [rsp+0a0h],rbx
const uintptr_t kMatchVf1 = 0x14041b380ull;
const unsigned char kMatchVf1Sig[25] = { 0x40,0x55,0x56,0x57,0x48,0x83,0xec,0x70,0x48,0xc7,0x44,0x24,0x38,0xfe,0xff,0xff,0xff,0x48,0x89,0x9c,0x24,0xa0,0x00,0x00,0x00 };
const uintptr_t kMatchVf1RetStep0f = 0x141fde4c8ull;   // despues del call [rax+8] de 0x141fde4c5 (paso 0x0f, sin entrada)
const int kParallel = 8;                                 // suplentes cargando a la vez
const double kHoldMax = 45.0;                            // tope de espera de la carga (s)
typedef void(__fastcall* ModeUpdFn)(void*, void*, void*);
typedef void(__fastcall* MatchVf1Fn)(void*, void*);
ModeUpdFn o_modeUpd = nullptr; MatchVf1Fn o_matchVf1 = nullptr;
bool s_okLoad = false;
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
typedef void*(__fastcall* PtrFn)();
typedef void*(__fastcall* ResFindFn)(void*, void*, const char*);
typedef void(__fastcall* GaniQueueFn)(void*, void*);
typedef void(__fastcall* GaniPumpFn)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef void*(__fastcall* StrAssignFn)(void*, const char*, size_t);
typedef void*(__fastcall* AnimResFindFn)(void*, void*);
typedef uint32_t(__fastcall* ClipIdxFn)(void*, const char*);
typedef float(__fastcall* ClipTimeFn)(void*, uint32_t, uint8_t);
typedef void(__fastcall* ClipSeekFn)(void*, uint64_t, uint32_t, float, uint8_t, uint8_t);
typedef void(__fastcall* HumanPushFn)(void*);
typedef void(__fastcall* HumanDtorFn)(void*, void*, void*, void*);
typedef uint64_t(__fastcall* ResUnloadFn)(void*, void*);
HumanPushFn o_humanPush = nullptr; HumanDtorFn o_humanDtor = nullptr; ResUnloadFn o_resUnload = nullptr;
bool s_okSubs = false;

struct GStr { union { char buf[16]; char* ptr; }; size_t size, cap; };   // std::string de MSVC (el del juego)

// clips de sentado
// (2e, pedido de Agustin "revisa mas ganis que sirvan jugadores sentados"; lista medida en /mnt/project-files/banca_suplentes/
// clips_sentados_fixdemo_20261117.txt). Solo los que dan la vuelta sin salto (menos de 10 grados entre el ultimo cuadro y el primero) y
// con la cadera a la altura del asiento (50 a 56 cm abajo): los del vestuario 2, 3 y 5 (el 4, 7 y 9 saltaban 19 a 22 grados al
// repetir) y los de la sala de reunion (waiting_01 sub1 / sub2 / sub3, 03a, 03c: 0,4 a 1,5 grados). En la sala la cadera mira de costado
// (el clip es de una mesa redonda): giro = lo contrario del giro medio de la cadera (pista 2) contra la raiz, para que miren a la cancha.
// La raiz del FixDemoHuman gira con FUN_141afccc0(ctl, fd+0x14 en radianes): cuaternion (0, sin(a/2), 0, cos(a/2)), adelante +z ->
// (sin a, 0, cos a), la misma cuenta que se uso para medir la cadera.
// (23/11, banca v7b2 al juego, Agustin: "podemos incorporarlo al juego", "todo esto es agregar no sacar") ESCENAS: lo que hace cada
// suplente en la vista previa (posturas con cuerpo vivo, charlas, molestar, rascarse, dedos...), horneado en un .gani por jugador y
// escena (zfx_banca_eE_jJ, 60 s, hornear.py): esc = escena 1..4 (0 = clip de Konami de siempre), jug = jugador de la vista (0..8).
// Los de una escena se sientan juntos, en el orden de la vista a lo largo del banco y con el mismo reloj, asi los toques entre
// vecinos (molestar, charla) pasan a la vez. Los clips de siempre se quedan (el resto del banco y algunos bancos enteros).
struct ClipDef { const char* name; float yaw; bool sala; int esc, jug; int vivo = -1; };   // vivo: version viva (zfx_banca_vivo_*) del clip de siempre n
#define ESC_J(e, j) { "zfx_banca_e" #e "_j" #j, 0.0f, false, e, j }
#define ESC(e) ESC_J(e, 0), ESC_J(e, 1), ESC_J(e, 2), ESC_J(e, 3), ESC_J(e, 4), ESC_J(e, 5), ESC_J(e, 6), ESC_J(e, 7), ESC_J(e, 8)
const ClipDef kClipDef[] = {
    { "dml_md_lockerSpeech_001_sit_002", 2.7f, false, 0, 0 }, { "dml_md_lockerSpeech_001_sit_003", -0.1f, false, 0, 0 },
    { "dml_md_lockerSpeech_001_sit_005", -0.1f, false, 0, 0 },
    { "dml_md_meeting_waiting_01_sub1", -96.7f, true, 0, 0 }, { "dml_md_meeting_waiting_01_sub2", 110.5f, true, 0, 0 },
    { "dml_md_meeting_waiting_01_sub3", 96.2f, true, 0, 0 }, { "dml_md_meeting_waiting_03a", 2.1f, true, 0, 0 }, { "dml_md_meeting_waiting_03c", -1.7f, true, 0, 0 },
    ESC(1), ESC(2), ESC(3), ESC(4),
    // (23/11, Agustin: "esos 6 suplentes podes darle mas movimiento pero manteniendo sus poses?") los clips de siempre, horneados con
    // la vista previa SIN cambios de postura ni charlas: la misma pose con respiracion, cuerpo vivo, miradas al partido, rascarse,
    // pierna, tamborileo, dedos y pies en el piso. Ya miran a la cancha (giro 0). Si cargo, reemplaza a su clip de siempre; si no, queda el de siempre.
    { "zfx_banca_vivo_sit_002", 0.0f, false, 0, 0, 0 }, { "zfx_banca_vivo_sit_003", 0.0f, false, 0, 0, 1 }, { "zfx_banca_vivo_sit_005", 0.0f, false, 0, 0, 2 },
    { "zfx_banca_vivo_waiting_01_sub1", 0.0f, true, 0, 0, 3 }, { "zfx_banca_vivo_waiting_01_sub2", 0.0f, true, 0, 0, 4 },
    { "zfx_banca_vivo_waiting_01_sub3", 0.0f, true, 0, 0, 5 }, { "zfx_banca_vivo_waiting_03a", 0.0f, true, 0, 0, 6 }, { "zfx_banca_vivo_waiting_03c", 0.0f, true, 0, 0, 7 },
};
#undef ESC
#undef ESC_J
const int kNumEsc = 4, kEscJug = 9;
// orden de los jugadores de la vista a lo largo del banco: de +x a -x en el marco del clip (hornear.py, escena st009)
const int kEscOrden[kEscJug] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };   // x de la cadera: 2.54 1.90 1.27 0.64 0 -0.63 -1.27 -1.90 -2.54 m
const float kEscFade = 0.9f;                             // fundido al dar la vuelta la escena (s)
int EscClip(int e, int j) { for (int i = 0; i < (int)(sizeof kClipDef / sizeof kClipDef[0]); i++) if (kClipDef[i].esc == e && kClipDef[i].jug == j) return i; return -1; }
const int kNumClips = (int)(sizeof kClipDef / sizeof kClipDef[0]);
struct Clip { char path[112]; uint64_t slot[2]; bool ok, asked; };
Clip s_clip[kNumClips];
std::atomic<uint64_t> s_clipHash[kNumClips];
std::atomic<int> s_protect{0}; std::atomic<unsigned> s_unloadBlocked{0};

// la descarga de un .gani nuestro se frena mientras haya suplentes creados con el (el gestor no cuenta usuarios)
// (17/11 3icxbh) clips que usan los jugadores propios del Motor (motorpj.cpp): tampoco se descargan mientras esten anotados aca
std::atomic<uint64_t> s_keepHash[48];   // (17/11 3icxbh) 48: animaciones por jugador del Motor (motorpj.cpp)
uint64_t __fastcall hk_ResUnload(void* mgr, void* key) {
    if (key) {
        uint64_t h = 0; uint8_t t = 0;
        if (Rd((uintptr_t)key, h) && Rd((uintptr_t)key + 8, t) && t >= 1 && t <= 5 && h)
            for (int i = 0; i < 48; i++) if (s_keepHash[i].load(std::memory_order_relaxed) == h) { s_unloadBlocked.fetch_add(1, std::memory_order_relaxed); return 0; }
    }
    if (key && s_protect.load(std::memory_order_relaxed) > 0) {
        uint64_t h = 0; uint8_t t = 0;
        if (Rd((uintptr_t)key, h) && Rd((uintptr_t)key + 8, t) && t >= 1 && t <= 5 && h)   // el hash sale de la ruta: vale para las 5 listas del gestor
            for (int i = 0; i < kNumClips; i++) if (s_clipHash[i].load(std::memory_order_relaxed) == h) { s_unloadBlocked.fetch_add(1, std::memory_order_relaxed); return 0; }
    }
    return o_resUnload(mgr, key);
}

// cupos de la placa (los mismos que anota el medidor): [gestor] + 0x10 cupo, + 0x14 usados
const uintptr_t kBudget[6] = { 0x14374cd70ull, 0x14374cd80ull, 0x14374cd88ull, 0x14374da88ull, 0x14374e458ull, 0x14374e460ull };
const char* const kBudgetName[6] = { "texturas", "modelos", "materiales", "buffers de vertices", "buffers de indices", "flujos de vertices" };
const int kBudgetMin[6] = { 250, 120, 500, 120, 120, 250 };
bool BudgetRead(int k, int& cap, int& used) {
    uintptr_t mgr = 0;
    return Rd(kBudget[k], mgr) && Heap(mgr) && Rd(mgr + 0x10, cap) && Rd(mgr + 0x14, used) && cap > 0 && cap <= 32767 && used >= 0 && used <= cap;
}
bool BudgetOk(char* why, size_t n) {
    for (int k = 0; k < 6; k++) { int cap = 0, used = 0; if (BudgetRead(k, cap, used) && cap - used < kBudgetMin[k]) { sprintf_s(why, n, "%s: quedan %d de %d", kBudgetName[k], cap - used, cap); return false; } }
    return true;
}

struct LiveSub {
    void* fd = nullptr; int side = 0, order = 0, place = 0, clip = 0, state = 0;   // 0 falta, 1 cargando, 2 sentado, 3 fallo / salio
    uint32_t teamIdx = 0; uint64_t id = 0; char name[40] = {}; Spot spot{}; bool bib = false, gk = false;
    float t0 = 0, start = 0, tCreateT = 0, phase0 = 0; double tCreate = 0; int endTicks = 0; bool faceTried = false; uint8_t player[0xF0] = {};
    float heightCm = 0, dyCm = 0;                      // (2d) estatura del modelo (h+0x10) y lo que se bajo la base por ella
    float offT = 0; bool off = false; uint8_t skip = 0; float back = 0;   // (v7d) fuera de cuadro: cuanto hace, escondido, cuadros salteados
    uint8_t bibByte = 0;                               // (2e) pechera del LoadId si le toca: 1 pieza 0, 2 pieza 8
    uint8_t kindOv = 0;                                // (2e) campera entera (lluvia / nieve): tipo del LoadId 0x16 / 0x17, sin pechera
    int shown = -1;                                    // (2e) clip que tiene puesto (el suyo o el de "probar clips")
    int esc = 0; float fadeA = -1000.0f;               // (23/11) escena horneada (0 = clip de siempre) y ultimo fundido de vuelta
    float blinkNext = -1.0f, blinkStart = -1000.0f; void* blinkFace = nullptr;   // (23/11) parpadeo: proximo, ultimo y animador donde se puso el clip
    int fcClip = -1, fcPrev = -1, fcLayer = 1; float fcW = 0, fcPrevW = 0, fcT0 = -1000.0f, fcPrevT0 = -1000.0f, fcNext = -1.0f;   // (23/11) cara: actual / anterior (fundido)
};
enum { kIdle = 0, kWait, kClips, kRun, kOff };
struct Subs {
    int phase = kIdle; uintptr_t match = 0, team[2] = {}; LiveSub s[48]; int n = 0, cur = 0;
    float T = 0; double tPhase = 0, lastTick = 0, lastCheck = 0, tSpawn = 0, tRetry = 0, tProgress = 0; bool pumpOwn = false, seekSaid = false, sheetSaid = false, doneSaid = false, resSaid = false;
    float A = 0; double lastG = -1.0;                  // (2d) reloj de la animacion (el del juego: pausa = quieto) y la ultima lectura de la fila 157
    int test = 0;                                      // (2e) "probar clips" que se aplico (0 = cada uno el suyo)
    float escT0[2] = {}; bool escT0Set[2] = {};        // (23/11) reloj comun de la escena de cada banco (todos en el mismo cuadro)
};
Subs s_subs;                                           // solo el hilo del juego (y el destructor, con s_subMx)
std::recursive_mutex s_subMx;
std::atomic<int> s_subActive{0}; std::atomic<unsigned> s_subTicks{0}, s_subSkipped{0};
std::atomic<bool> s_subsDead{false};                   // hubo un error dentro del juego: no se toca nada mas en esta sesion
std::atomic<float> s_animSum{0.0f}, s_realSum{0.0f};   // (2d) segundos de animacion / reales de los sentados desde el ultimo informe
std::atomic<bool> s_animGame{false};                   // (2d) el reloj de la animacion sigue al del juego (fila 157 reconocida)
std::atomic<unsigned long> s_implThread{0}, s_pushThread{0}, s_srcThread{0};
std::atomic<int> s_src{0};                             // 0 sin decidir, 1 match::Human vf19, 2 escena del estadio
std::atomic<ULONGLONG> s_matchEnd{0};
// (2b) s_live: este partido ya arranco (match::Match llego a 0x1d o los jugadores se dibujan): recien ahi el destructor de un jugador
// es el fin del partido. s_ended: termino el partido y todavia no empezo a cargar otro (no se arma la lista de nuevo).
std::atomic<bool> s_live{false}, s_ended{false};
std::atomic<int> s_prepWhy{-1}, s_matchState{-1};
std::atomic<double> s_modeT{0.0};
std::atomic<unsigned> s_modeCalls{0};
std::atomic<unsigned long> s_modeThread{0};

uint64_t s_rng = 0;
uint32_t Rnd() { if (!s_rng) { LARGE_INTEGER c; QueryPerformanceCounter(&c); s_rng = (uint64_t)c.QuadPart | 1; } s_rng ^= s_rng << 13; s_rng ^= s_rng >> 7; s_rng ^= s_rng << 17; return (uint32_t)(s_rng >> 16); }

__declspec(noinline) bool FdDestroy(void* fd) {
    __try {
        uint32_t saved = 0, id = 0x19;
        ((HeapPushFn)kHeapPush)(&saved, &id);
        ((FdVf0Fn)kFdVf0)(fd, 1);
        ((HeapPopFn)kHeapPop)(&saved);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void SubsDestroyAll(const char* why) {
    Subs& S = s_subs; int k = 0, bad = 0;
    const bool dead = s_subsDead.load();              // con un error previo solo se sueltan los que quedaron bien sentados
    for (int i = 0; i < S.n; i++) if (S.s[i].fd) { if (!dead || S.s[i].state == 2) { if (FdDestroy(S.s[i].fd)) k++; else bad++; } S.s[i].fd = nullptr; }
    if (k || bad || S.phase != kIdle) Log("banca: suplentes reales: %d sacados del banco (%s)%s", k, why, bad ? ", con errores al soltar" : "");
    if (bad) s_subsDead.store(true);
    S.n = 0; S.cur = 0; S.phase = kIdle; S.match = 0; S.pumpOwn = false; s_subActive.store(0); s_protect.store(0);
    pes21fx_banca_ropa = 0;                            // (2e) las tablas de piezas vuelven a las de siempre en el proximo pedido de modelo
}

// 1 = ya esta en el gestor, 0 = no, -1 = error
__declspec(noinline) int ClipQuery(int i) {
    __try {
        void* mgr = ((PtrFn)kResMgr)(); if (!mgr) return 0;
        uint64_t out[2] = { 0, 0 };
        ((ResFindFn)kResFind)(mgr, out, s_clip[i].path);
        if ((uint8_t)out[1] != 0 && out[0] != 0xb8a0bf169f98ull) { s_clipHash[i].store(out[0]); s_clip[i].ok = true; return 1; }
        s_clip[i].ok = false; return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
__declspec(noinline) bool ClipAsk(int i) {
    __try {
        GStr s; memset(&s, 0, sizeof s); s.cap = 15;
        ((StrAssignFn)kStrAssign)(&s, s_clip[i].path, strlen(s_clip[i].path));   // lo libera el juego (FUN_141eb16e0)
        s_clip[i].slot[0] = s_clip[i].slot[1] = 0;
        ((GaniQueueFn)kGaniQueue)(s_clip[i].slot, &s);
        s_clip[i].asked = true; return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
__declspec(noinline) bool PumpOnce() {
    __try { ((GaniPumpFn)kGaniPump)(0, 0, 0, 0); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
__declspec(noinline) void* FixdemoRes() {
    __try {
        GStr s; memset(&s, 0, sizeof s); memcpy(s.buf, "fixdemo", 8); s.size = 7; s.cap = 15;
        void* mgr = ((PtrFn)kAnimResMgr)();
        return mgr ? ((AnimResFindFn)kAnimResFind)(mgr, &s) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
__declspec(noinline) bool SubCreate(LiveSub& u, bool haveSW, uint8_t season, uint8_t weather) {
    __try {
        uint32_t saved = 0, id = 0x19, heap = 0x19;
        ((HeapPushFn)kHeapPush)(&saved, &id);
        void* mem = ((AllocFn)kAlloc)(&heap, 0x578, 0x10);
        uint8_t* fd = mem ? (uint8_t*)((FdCtorFn)kFdCtor)(mem, (uint8_t)(0x40 + u.order)) : nullptr;
        if (fd) {
            uint8_t lid[0xa0];
            ((LoadIdFn)kLoadId)(nullptr, lid, u.side, 0, u.player, u.gk ? 1 : 0, 0, 0, 0, (uint8_t)(u.bib ? u.bibByte : 0));
            if (haveSW) { lid[0x4f] = season; lid[0x50] = weather; }
            if (u.bib && u.kindOv) lid[0x4c] = u.kindOv;  // (2e) campera entera: tipo de la ropa de entrenamiento (FUN_141f61520 lo pone en +0x4c)
            fd[0x490] = 0; fd[0x514] = 0;                 // sin efectos de lluvia / aliento de la cinematica
            ((FdLoadFn)kFdLoad)(fd, lid, (uint32_t)u.side, (uint8_t)u.place, (uint8_t)(u.gk ? 1 : 0));
        }
        ((HeapPopFn)kHeapPop)(&saved);
        u.fd = fd; return fd != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { u.fd = nullptr; s_subsDead.store(true); return false; }
}
// 1 = cargo, 0 = todavia, -1 = error
__declspec(noinline) int SubPoll(LiveSub& u) {
    __try {
        uint32_t saved = 0, id = 0x19;
        ((HeapPushFn)kHeapPush)(&saved, &id);
        const uint64_t r = ((FdPollFn)kFdPoll)(u.fd);
        ((HeapPopFn)kHeapPop)(&saved);
        return (r & 0xff) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { s_subsDead.store(true); return -1; }
}
// (2c) crea (una vez) el controlador de cara y manos del suplente con FixDemoHuman::vf3, como la cinematica. Devuelve fd+0x58.
// Si los recursos de cara / manos del juego todavia no estan (carga), no lo crea y se reintenta el cuadro siguiente.
// Se llama dentro de un __try (SubStart / SubUpdate).
void* FaceEnsure(LiveSub& u) {
    uint8_t* fd = (uint8_t*)u.fd; if (!fd) return nullptr;
    void* face = nullptr; memcpy(&face, fd + 0x58, 8);
    if (face || u.faceTried) return face;
    uintptr_t fr = 0, hr = 0;
    if (!Rd(kFaceResPtr, fr) || !fr || !Rd(kHandResPtr, hr) || !hr) return nullptr;
    u.faceTried = true;
    uintptr_t vt = 0, f3 = 0;
    if (!Rd((uintptr_t)fd, vt) || !Rd(vt + 0x18, f3) || f3 != kFdVf3) {
        s_faceFail.fetch_add(1);
        static int said = 0; if (said++ < 2) Log("banca: suplentes reales: FixDemoHuman::vf3 no esta donde se esperaba (%p): sin cara ni manos", (void*)f3);
        return nullptr;
    }
    uint32_t saved = 0, id = 0x19;
    ((HeapPushFn)kHeapPush)(&saved, &id);
    ((FdVf3Fn)f3)(fd, 0, 0, 0);
    ((HeapPopFn)kHeapPop)(&saved);
    memcpy(&face, fd + 0x58, 8);
    if (face) s_faceMade.fetch_add(1); else s_faceFail.fetch_add(1);
    return face;
}
__declspec(noinline) bool SubStart(LiveSub& u, void* res, float T) {
    __try {
        uint8_t* fd = (uint8_t*)u.fd;
        const float yaw = u.spot.yaw + kClipDef[u.clip].yaw;   // (2e) giro propio del clip (sala de reunion)
        memcpy(fd + 8, &u.spot.x, 4); memcpy(fd + 0xc, &u.spot.y, 4); memcpy(fd + 0x10, &u.spot.z, 4); memcpy(fd + 0x14, &yaw, 4);
        {   // (2d) estatura: como la cinematica (FUN_141f88270), solo con una altura creible
            void* hh = nullptr; memcpy(&hh, fd + 0x48, 8); float hc = 0;
            if (hh) memcpy(&hc, (const uint8_t*)hh + 0x10, 4);
            u.heightCm = hc; u.dyCm = 0;
            if (hh && hc >= 150.0f && hc <= 215.0f) { ((FdHeightFn)kFdHeight)(fd, kStatureK); float y = 0; memcpy(&y, fd + 0xc, 4); u.dyCm = (y - u.spot.y) * 100.0f; }
        }
        fd[0x416] = 1;                                    // al dar la vuelta el clip vuelve a la base (FUN_141f86d40)
        uint32_t saved = 0, id = 0x19;
        ((HeapPushFn)kHeapPush)(&saved, &id);
        const uint32_t idx = ((ClipIdxFn)kClipIdx)(res, s_clip[u.clip].path);
        uint32_t bits = 0; memcpy(&bits, &u.start, 4);
        (o_fdh ? o_fdh : (FdhVf1)kFdhVf1)(fd, res, idx, 0, (uint64_t)bits, nullptr);
        ((HeapPopFn)kHeapPop)(&saved);
        // (2c) ropa: lo mismo que la cinematica al activar a un jugador (FUN_141f45950: fd+0x43e = 3): en los 3 cuadros siguientes
        // FUN_141f86d40 reinicia la tela dos veces (FUN_141ec80e0, orden 0x51b 0 -> 1) y la prende (FUN_141ec8700(h, 1, 1.0)).
        // Sin esto la tela (pechera) arranca donde se creo el modelo y no donde se sienta.
        fd[0x43e] = 3;
        FaceEnsure(u);                                    // (2c) cara y manos (despues de vf1: vf2 borra fd+0x58)
        void* ctl = nullptr; memcpy(&ctl, fd + 0x50, 8);
        // (2b) cada uno arranca en otro punto del clip (se crean varios a la vez): vf4 recibe el tiempo desde el principio y la primera
        // vez que pasa de 0,05 s salta ahi (FUN_141afa7b0), asi que alcanza con correr el origen del reloj
        if (ctl) { const float len = ((ClipTimeFn)kClipLen)(ctl, 0, 1); u.phase0 = len > 1.5f ? fmodf(u.phase0, len - 0.5f) : 0.0f; }
        else u.phase0 = 0.0f;
        u.t0 = T - u.phase0; u.endTicks = 0;
        if (u.esc) {                                      // (23/11) escena: el reloj comun del banco (los que cargan despues saltan al mismo cuadro)
            Subs& S = s_subs; const int sd = u.side & 1;
            if (!S.escT0Set[sd]) { S.escT0[sd] = u.t0; S.escT0Set[sd] = true; }
            u.t0 = S.escT0[sd]; u.fadeA = -1000.0f;
        }
        void* h = nullptr; memcpy(&h, fd + 0x48, 8);
        if (h) ((HumanShowFn)kHumanShow)(h, 1);
        u.shown = u.clip;
        return ctl != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { s_subsDead.store(true); return false; }
}
// (2e) "probar clips": cambia el clip de un sentado al momento. vf1 -> vf2 borra el controlador del cuerpo (fd+0x50) y el de cara y manos
// (fd+0x58) y arma el nuevo con la base y el giro de fd+8..0x14 (la base ya trae lo de la estatura: no se toca). Sin fundido.
__declspec(noinline) bool SubSwitch(LiveSub& u, void* res, int clip, float T) {
    __try {
        uint8_t* fd = (uint8_t*)u.fd;
        const float yaw = u.spot.yaw + kClipDef[clip].yaw;
        memcpy(fd + 0x14, &yaw, 4);
        uint32_t saved = 0, id = 0x19;
        ((HeapPushFn)kHeapPush)(&saved, &id);
        const uint32_t idx = ((ClipIdxFn)kClipIdx)(res, s_clip[clip].path);
        const float start = 0.0f; uint32_t bits = 0; memcpy(&bits, &start, 4);
        (o_fdh ? o_fdh : (FdhVf1)kFdhVf1)(fd, res, idx, 0, (uint64_t)bits, nullptr);
        ((HeapPopFn)kHeapPop)(&saved);
        u.faceTried = false; FaceEnsure(u);              // vf2 borro el de cara y manos: se vuelve a crear
        void* ctl = nullptr; memcpy(&ctl, fd + 0x50, 8);
        float ph = u.phase0;
        if (ctl) { const float len = ((ClipTimeFn)kClipLen)(ctl, 0, 1); ph = len > 1.5f ? fmodf(ph, len - 0.5f) : 0.0f; }
        u.t0 = T - ph; u.endTicks = 0; u.shown = clip;
        if (kClipDef[clip].esc && s_subs.escT0Set[u.side & 1]) u.t0 = s_subs.escT0[u.side & 1];   // (23/11) vuelve al reloj comun de la escena
        u.fadeA = -1000.0f;
        return ctl != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { s_subsDead.store(true); return false; }
}
// (23/11) VUELTA CON FUNDIDO de una escena horneada (60 s): poco antes del final se vuelve a pedir el mismo clip desde 0 con
// FUN_141afb9a0(control, &pedido, &fundido, capa 0, curva 0), lo mismo que arma FixDemoHuman::vf2 (0x141f87af0) pero con duracion:
// pedido = el de vf2 (+8 indice del clip en el recurso, +0xc arranque s, +0x14 velocidad 1.0, +0x34 -1, el resto 0), fundido =
// { modo 0, banderas 0, duracion s } (capa 0: local_b4 = duracion * 300 tics). El controlador no se destruye (vf1 / vf2 si y cortan) y
// vf4 sigue con el mismo reloj absoluto: todos los de la escena dan la vuelta en el mismo cuadro y siguen juntos.
struct ClipPlay { uint64_t r0; uint32_t idx; float start; uint8_t p10, pad11[3]; float speed; uint32_t z18; uint64_t z20, z28; uint8_t z30, pad31[3]; int32_t m34; };
static_assert(sizeof(ClipPlay) == 0x38, "pedido de clip de FixDemoHuman::vf2");
struct ClipBlend { int32_t mode; uint8_t flags, pad[3]; float dur; };
static_assert(sizeof(ClipBlend) == 12, "fundido de FixDemoHuman::vf2");
typedef void(__fastcall* ClipPlayFn)(void*, void*, void*, uint32_t, uint32_t);
const uintptr_t kClipPlay = 0x141afb9a0ull;
std::atomic<unsigned> s_escFades{0};
// 1 = dio la vuelta con fundido, 0 = todavia no, -1 = error
__declspec(noinline) int EscLoop(LiveSub& u, void* res, float A) {
    __try {
        uint8_t* fd = (uint8_t*)u.fd; void* ctl = nullptr; memcpy(&ctl, fd + 0x50, 8);
        if (!ctl || !res || A - u.fadeA < 5.0f) return 0;
        const float len = ((ClipTimeFn)kClipLen)(ctl, 0, 1), cur = ((ClipTimeFn)kClipTime)(ctl, 0, 1);
        if (len < 5.0f || cur < len - kEscFade - 0.05f) return 0;
        uint32_t saved = 0, id = 0x19;
        ((HeapPushFn)kHeapPush)(&saved, &id);
        ClipPlay pl; memset(&pl, 0, sizeof pl); pl.idx = ((ClipIdxFn)kClipIdx)(res, s_clip[u.shown].path); pl.start = 0.0f; pl.speed = 1.0f; pl.m34 = -1;
        ClipBlend bl; memset(&bl, 0, sizeof bl); bl.dur = kEscFade;
        ((ClipPlayFn)kClipPlay)(ctl, &pl, &bl, 0, 0);
        ((HeapPopFn)kHeapPop)(&saved);
        u.fadeA = A; s_escFades.fetch_add(1, std::memory_order_relaxed);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { s_subsDead.store(true); return -1; }
}
// (23/11, Agustin: "agregales blink animations") parpadeo. El controlador de cara y manos (fd+0x58, el mismo tipo que usan los
// jugadores: [0] animador de cara, [1] [2] manos) tiene en el animador de cara 4 capas base (0..3) y 4 sumadas (4..7). Asi las llena
// FUN_141ec3170 en el partido: capa sumada 4 + n = clip 0xa6 + n (los 166 base van primero) con la mascara de zona FUN_141ee0870(&m, n),
// FUN_141b04020(anim, capa, clip, 0, 0, 0.0f, mascara) si cambio el clip, FUN_141b04e20(anim, capa, tiempo s) (x 300, da la vuelta sola)
// y FUN_141b04fb0(anim, capa, peso). Despues FUN_141ec7b10 (lo de siempre) evalua el animador (vf +0x38) y manda la pose al modelo.
// add_eyeblink_single = sumado n 2 (8 cuadros, parpados 25 grados; el que usa Konami en 262 de 308 tramos de Faceadd.bin) -> clip 0xa8.
// Aca solo se toca la capa sumada 4 del animador de cara: las capas base y las manos quedan como estaban (pose neutra).
const uintptr_t kAnimSetClip = 0x141b04020ull, kAnimSetTime = 0x141b04e20ull, kAnimSetWeight = 0x141b04fb0ull, kFaceMask = 0x141ee0870ull;
typedef void(__fastcall* AnimSetClipFn)(void*, uint32_t, uint32_t, char, char, float, uint64_t);
typedef void(__fastcall* AnimSetTimeFn)(void*, uint32_t, float);
typedef void(__fastcall* AnimSetWeightFn)(void*, uint32_t, float);   // (desensamblado: movss [rax+r8*4], xmm2 -> float en xmm2)
typedef uint64_t*(__fastcall* FaceMaskFn)(uint64_t*, uint32_t);
const uint32_t kBlinkLayer = 4, kBlinkAdd = 2, kBlinkClip = 0xa6 + kBlinkAdd;
const float kBlinkLen = 8.0f / 30.0f;
std::atomic<unsigned> s_blinks{0};
typedef void(__fastcall* AnimSpeedFn)(void*, float);
typedef void(__fastcall* AnimEvalFn)(void*);
const uintptr_t kAnimSpeed = 0x141afd270ull;
void FaceEval(void* face) {                            // FUN_141ec1240 sin el envio: velocidad 0 y vf +0x38 de cara y manos
    void* a[3] = {}; memcpy(a, face, sizeof a);
    for (int i = 0; i < 3; i++) if (a[i]) ((AnimSpeedFn)kAnimSpeed)(a[i], 0.0f);
    for (int i = 0; i < 3; i++) if (a[i]) { uintptr_t vt = 0; memcpy(&vt, a[i], 8); AnimEvalFn f = nullptr; memcpy(&f, (void*)(vt + 0x38), 8); f(a[i]); }
}
void SubBlink(LiveSub& u, void* face, float T) {
    void* fa = nullptr; memcpy(&fa, face, 8); if (!fa) return;
    if (fa != u.blinkFace) {                           // controlador nuevo (al crear o despues de "probar clips"): poner el clip una vez
        uint64_t m = 0; ((FaceMaskFn)kFaceMask)(&m, kBlinkAdd);
        ((AnimSetClipFn)kAnimSetClip)(fa, kBlinkLayer, kBlinkClip, 0, 0, 0.0f, m);
        u.blinkFace = fa; u.blinkNext = T + 0.4f + (float)(Rnd() % 1000) * 0.004f; u.blinkStart = -1000.0f;
    }
    if (T < u.blinkStart || u.blinkNext - T > 12.0f) { u.blinkStart = -1000.0f; u.blinkNext = T + 1.0f; }   // el reloj volvio atras
    if (T >= u.blinkNext) {                            // cada 1.8-6 s, a veces doble (0.38 s despues)
        const bool dbl = u.blinkStart < T - 1.0f && Rnd() % 6 == 0;
        u.blinkStart = T; u.blinkNext = T + (dbl ? 0.38f : 1.8f + (float)(Rnd() % 1000) * 0.0042f);
        s_blinks.fetch_add(1, std::memory_order_relaxed);
    }
    const float bt = T - u.blinkStart, w = bt >= 0.0f && bt < kBlinkLen ? 1.0f : 0.0f;
    ((AnimSetTimeFn)kAnimSetTime)(fa, kBlinkLayer, w > 0.0f ? bt : 0.0f);
    ((AnimSetWeightFn)kAnimSetWeight)(fa, kBlinkLayer, w);
}
// (23/11, Agustin: "y darles expresiones a todos") caras. Mismo animador de cara que el parpadeo: capas base 0..3 con un clip de los 166
// de la lista fija del exe (indice = byte que FUN_141ec3170 pasa a FUN_141b03f60(anim, capa, clip, 0, 0, 0.0f, 0)). Capa 0 = base_normal
// (67) con lo que sobra del peso, capas 1 / 2 = la cara actual y la anterior, que se cruzan en kFaceFade s (el juego tampoco cambia la
// emocion antes de 0.2 s). Los de la escena siguen la linea de tiempo de la vista previa (banca_caras.inc: cuadro del clip horneado ->
// clip y peso, sacado de acciones.json; los zfx_ de la vista que no existen en el juego van a su par de Konami); los de los clips de
// siempre, caras tranquilas al azar cada 6-14 s. La pose de cara sale despues con FUN_141ec7b70(h, controlador, 3): FUN_141ec4970 con
// modo 3 toma la CARA del animador (modo 1 / 2 = tablas neutras) y las MANOS de las tablas neutras (modo 1 / 3), como hasta ahora.
// Solo huesos de la cara del rig (skf_): nada del modelo ni del interior de la boca.
struct FaceKey { uint16_t f; uint8_t clip, w; };     // cuadro (30 por s) del clip horneado, clip base, peso x 100
#include "banca_caras.inc"                           // kEscCara[e][j] / kEscCaraN[e][j] (generado por tools/banca/caras_inc.py)
const uintptr_t kAnimSetBase = 0x141b03f60ull, kHumanFace = 0x141ec7b70ull;
typedef void(__fastcall* HumanFaceFn)(void*, void*, uint32_t);
const uint32_t kFaceBaseNormal = 67;
const float kFaceFade = 0.45f;
const uint8_t kCalmFace[][2] = { { 67, 0 }, { 107, 60 }, { 155, 35 }, { 71, 70 }, { 114, 40 }, { 119, 45 }, { 155, 25 }, { 67, 0 } };
std::atomic<unsigned> s_faceChanges{0};
void SubFace(LiveSub& u, void* face, float T, float clipT) {
    void* fa = nullptr; memcpy(&fa, face, 8); if (!fa) return;
    if (u.fcT0 > T + 1.0f) { u.fcT0 = u.fcPrevT0 = -1000.0f; u.fcNext = T; }       // el reloj volvio atras
    int clip = (int)kFaceBaseNormal; float w = 0.0f;
    const int e = kClipDef[u.shown >= 0 ? u.shown : u.clip].esc, j = kClipDef[u.shown >= 0 ? u.shown : u.clip].jug;
    if (e >= 1 && e <= kNumEsc && j >= 0 && j < kEscJug && clipT >= 0.0f) {          // escena: la cara de la vista en ese cuadro
        const FaceKey* k = kEscCara[e - 1][j]; const int n = kEscCaraN[e - 1][j]; const int f = (int)(clipT * 30.0f);
        for (int i = 0; i < n && k[i].f <= f; i++) { clip = k[i].clip; w = k[i].w * 0.01f; }
        if (w > 0.0f) u.fcNext = T;                                                     // al terminar la cara de la escena sale una tranquila
    }
    if (w <= 0.0f) {                                                                    // clips de siempre (y la escena entre caras): tranquilas al azar
        if (T >= u.fcNext) { const int r = (int)(Rnd() % (sizeof kCalmFace / sizeof kCalmFace[0])); u.fcNext = T + 6.0f + (float)(Rnd() % 800) * 0.01f;
                             clip = kCalmFace[r][0]; w = kCalmFace[r][1] * 0.01f; if (clip == u.fcClip) { w = u.fcW; } }
        else { clip = u.fcClip >= 0 ? u.fcClip : (int)kFaceBaseNormal; w = u.fcW; }
    }
    if (w <= 0.0f) clip = (int)kFaceBaseNormal;
    if (fa != u.blinkFace || u.fcClip < 0) {                                           // animador nuevo: capa 0 = base_normal
        ((AnimSetClipFn)kAnimSetBase)(fa, 0, kFaceBaseNormal, 0, 0, 0.0f, 0);
        u.fcClip = -2; u.fcPrev = -1; u.fcW = u.fcPrevW = 0.0f;
    }
    if (clip != u.fcClip || std::fabs(w - u.fcW) > 0.01f) {                            // cara nueva: la actual pasa a "anterior" y se cruza
        const float k0 = std::clamp((T - u.fcT0) / kFaceFade, 0.0f, 1.0f);
        u.fcPrev = u.fcClip; u.fcPrevW = u.fcW * k0; u.fcPrevT0 = u.fcT0;   // lo que pesaba la cara actual en este momento
        u.fcLayer = u.fcLayer == 1 ? 2 : 1; u.fcClip = clip; u.fcW = w; u.fcT0 = T;
        if (clip != (int)kFaceBaseNormal) ((AnimSetClipFn)kAnimSetBase)(fa, (uint32_t)u.fcLayer, (uint32_t)clip, 0, 0, 0.0f, 0);
        s_faceChanges.fetch_add(1, std::memory_order_relaxed);
    }
    float k = std::clamp((T - u.fcT0) / kFaceFade, 0.0f, 1.0f); k = k * k * (3.0f - 2.0f * k);
    const float wn = u.fcClip == (int)kFaceBaseNormal ? 0.0f : u.fcW * k;
    const float wo = (u.fcPrev < 0 || u.fcPrev == (int)kFaceBaseNormal) ? 0.0f : u.fcPrevW * (1.0f - k);
    const uint32_t ln = (uint32_t)u.fcLayer, lo = ln == 1 ? 2u : 1u;
    ((AnimSetTimeFn)kAnimSetTime)(fa, ln, std::max(0.0f, T - u.fcT0));
    ((AnimSetTimeFn)kAnimSetTime)(fa, lo, std::max(0.0f, T - u.fcPrevT0));
    ((AnimSetTimeFn)kAnimSetTime)(fa, 0, 0.0f);
    ((AnimSetWeightFn)kAnimSetWeight)(fa, ln, wn);
    ((AnimSetWeightFn)kAnimSetWeight)(fa, lo, wo);
    ((AnimSetWeightFn)kAnimSetWeight)(fa, 0, std::max(0.0f, 1.0f - wn - wo));
    ((AnimSetWeightFn)kAnimSetWeight)(fa, 3, 0.0f);
}
// (23/11, Agustin: "agregar el lod ... asi mi juego no anda a 10 fps"; prueba: 14-30 FPS con la placa al 96 %). El suplente se dibujaba
// siempre con el modelo completo: draw::util::Human+0xc = modo de LOD (0 / 1 / 2 = nivel fijo, 3 = por tamano en pantalla) y nadie lo
// ponia. La cinematica lo pone al activar a cada persona (FUN_141f458c0 segun FixDemoHumanData.lod): LOD_AUTO = FUN_141ec83e0(h, 1.25f,
// 0.2f) (modo 3: FUN_1419e1b20(modelo, 3) + FUN_1419e1b70 con los cortes 2.747 / 1.25 y 2.747 / 0.2, 2.747 = 1 / tan(20 grados));
// LOD_A / B / D = FUN_141ec8490(h, 0 / 1 / 2, 0, 0). FUN_141ec83e0 solo aplica si el modelo ya cargo (vf +0x10): se llama al sentarlo.
const uintptr_t kHumanLodAuto = 0x141ec83e0ull;
typedef void(__fastcall* HumanLodAutoFn)(void*, float, float);
std::atomic<unsigned> s_lodSet{0};
void SubLod(LiveSub& u) {
    __try {
        // x520 = distancia del detalle: los cortes son 2.747 / parametro, asi que dividir los parametros por k aleja los cortes k veces
        const float k = (!g_cfg.info[520][0].defined && g_cfg.live[520][0] == 0.0f) ? 1.0f : g_cfg.live[520][0];
        if (k <= 0.01f) return;                                   // 0 = sin LOD (modelo completo, como antes)
        void* h = nullptr; memcpy(&h, (uint8_t*)u.fd + 0x48, 8); if (!h) return;
        ((HumanLodAutoFn)kHumanLodAuto)(h, 1.25f / k, 0.2f / k);
        if (*(const int32_t*)((const uint8_t*)h + 0xc) == 3) s_lodSet.fetch_add(1, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { static int said = 0; if (said++ < 2) Log("banca: suplentes reales: error al poner el LOD de %s (sin LOD)", u.name); }
}
// avanza y manda la pose; devuelve 2 si tuvo que volver el clip al principio a mano
std::atomic<unsigned> s_offHidden{0}, s_offSkipped{0};
std::atomic<unsigned> s_backSeeks{0};
__declspec(noinline) int SubUpdate(LiveSub& u, float T, bool moved) {
    __try {
        uint8_t* fd = (uint8_t*)u.fd;
        if (u.back < 0.0f) {                              // (v7e) repeticion para atras: el clip va para atras (al llegar a 0, desde el final)
            void* c0 = nullptr; memcpy(&c0, fd + 0x50, 8);
            if (c0) {
                const float len = ((ClipTimeFn)kClipLen)(c0, 0, 1), cur = ((ClipTimeFn)kClipTime)(c0, 0, 1);
                float nt = cur + u.back; if (len > 1.0f) { while (nt < 0.0f) nt += len; } else nt = std::max(0.0f, nt);
                ((ClipSeekFn)kClipSeek)(c0, 0, 0, nt, 1, 1); s_backSeeks.fetch_add(1, std::memory_order_relaxed);
            }
            u.back = 0.0f;
        }
        ((FdVf4Fn)kFdVf4)(fd, T - u.t0);
        ((FdPushFn)kFdPush)(fd);
        void* h = nullptr; memcpy(&h, fd + 0x48, 8);
        void* face = FaceEnsure(u);                       // (2c) cara y manos en pose neutra, como match::Player::vf10
        if (h && face) {                                  // (23/11) cara (escena / tranquila) + parpadeo; la pose de cara sale del animador
            float clipT = -1.0f; { void* c_ = nullptr; memcpy(&c_, fd + 0x50, 8); if (c_) clipT = ((ClipTimeFn)kClipTime)(c_, 0, 1); }
            SubFace(u, face, T, clipT); SubBlink(u, face, T);
            // (23/11 v7d, Agustin: "las expresiones ... vibran") antes: FaceNeutral (FUN_141ec1240: evalua y MANDA la pose neutra con
            // FUN_141ec4970 modo 1) y despues el modo 3. Cada envio pide un bufer (FUN_141962b00) y lo entrega al modelo (FUN_1419e1a40):
            // con dos envios por cuadro el dibujo tomaba a veces el neutro y a veces la cara -> la cara titilaba cuadro a cuadro. Ahora
            // se evalua igual que FUN_141ec1240 (velocidad 0 + vf +0x38 de los 3 animadores) y se manda UNA vez, en modo 3.
            FaceEval(face);
            ((HumanFaceFn)kHumanFace)(h, face, 3);        // la cara del animador (modo 3: manos neutras)
        }
        if (((g_labHideSide.load(std::memory_order_relaxed) >> (u.side & 1)) & 1) || u.off) { if (h && *((const uint8_t*)h + 0x5a) != 0) { ((HumanShowFn)kHumanShow)(h, 0); if (u.off) s_offHidden.fetch_add(1, std::memory_order_relaxed); } }   // (21/11) banco del laboratorio; (v7d) fuera de cuadro
        else if (h && *((const uint8_t*)h + 0x5a) == 0) ((HumanShowFn)kHumanShow)(h, 1);   // si algo lo escondio, se vuelve a mostrar
        void* ctl = nullptr; memcpy(&ctl, fd + 0x50, 8);
        if (ctl) {
            const float len = ((ClipTimeFn)kClipLen)(ctl, 0, 1), cur = ((ClipTimeFn)kClipTime)(ctl, 0, 1);
            if (len > 1.0f && cur >= len - 0.003f) {
                if (moved && ++u.endTicks >= 6) { ((ClipSeekFn)kClipSeek)(ctl, 0, 0, 0.0f, 1, 1); u.endTicks = 0; return 2; }
            } else u.endTicks = 0;
        }
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { s_subsDead.store(true); return -1; }
}

// arma la lista de los dos equipos; 0 = listo, otro = todavia no se puede (se reintenta) y por que
const char* const kPrepWhy[6] = { "", "no hay partido", "este estadio no trajo figuras en el banco (sin asientos)", "el estadio todavia esta cargando",
                                  "los asientos guardados son de otro estadio", "los equipos del partido todavia no estan" };
int SubsPrepare() {
    Subs& S = s_subs;
    uintptr_t m = 0; if (!Rd(kMatchPtr, m) || !Heap(m)) return 1;
    SeatSet ss; { std::lock_guard<std::mutex> lk(s_seatMx); ss = s_seats; }
    const ULONGLONG now = GetTickCount64();
    if (ss.n <= 0) return 2;
    if (now - ss.t < 3000) return 3;
    if (ss.t <= s_matchEnd.load()) {                                   // asientos de antes del partido anterior: solo si sigue el mismo estadio
        uintptr_t bm = 0, bb = 0, be = 0, bs = 0; char nm[64] = {};
        if (!FindBench(bm, bb, be, bs, nm, sizeof nm) || strncmp(nm, ss.pre, 5)) return 4;
    }
    uintptr_t team[2] = {}; uint8_t conv[2] = {};
    for (int sd = 0; sd < 2; sd++)
        if (!Rd(m + 0x1978 + (uintptr_t)sd * 0x28, team[sd]) || !Heap(team[sd]) || !Rd(team[sd] + 0x2eec, conv[sd]) || conv[sd] < 11 || conv[sd] > 40) return 5;
    S.n = 0; S.cur = 0; S.match = m; S.team[0] = team[0]; S.team[1] = team[1]; S.doneSaid = false; S.resSaid = false; S.test = 0;
    S.escT0Set[0] = S.escT0Set[1] = false;
    int escUsada = 0;
    const int cap = (int)(Live(3, 0.0f) + 0.5f);
    const float pct = std::min(100.0f, std::max(0.0f, Live2(1, 50.0f))), up = Live2(2, 0.0f), fwd = Live2(3, 0.0f);
    // (2e, Agustin 17/11: "mostrar las pecheras de suplente en verano y las camperas en invierno", "solo la pechera de la campera y no el
    // pantalon largo"). La estacion es la de PES21FX_match.ini (la misma que usan el viento y PecheraPorJugador.lua; 0 verano, 1
    // invierno). Campera = la parte de arriba de la campera de cada equipo, puesta como pechera (pieza 0 el local, 8 el visitante) sobre el
    // uniforme, asi quedan el short, las medias y las piernas (con el tipo 0x16 de la ropa de entrenamiento el juego apaga el uniforme).
    const int ropaOpt = (int)(Live3(0, 0.0f) + 0.5f), bibOpt = (int)(Live3(1, 0.0f) + 0.5f);
    const bool known = g_cfg.match.known; const int season = g_cfg.match.season;
    const bool jacket = ropaOpt == 2 || (ropaOpt == 0 && known && season == 1);
    const int wth = g_cfg.match.weather;
    const bool full = jacket && known && (wth == 1 || wth == 2);   // lluvia o nieve: campera entera
    const uint8_t bb[2] = { (uint8_t)(full ? 0 : (jacket || bibOpt == 1) ? 1 : 2), (uint8_t)(full ? 0 : 2) };
    pes21fx_banca_ropa = (unsigned char)(full ? 3 : jacket ? 1 : bibOpt == 1 ? 2 : 0);
    Log("banca: ropa de los suplentes: %s (opcion %s, estacion %s, tiempo %d)%s",
        full ? "la campera entera de cada equipo (lluvia o nieve)" : jacket ? "la parte de arriba de la campera de cada equipo" :
        bibOpt == 1 ? "la pechera propia de cada equipo" : "la pechera de suplente del juego",
        ropaOpt == 0 ? "segun el clima" : ropaOpt == 1 ? "siempre pechera" : "siempre campera",
        !known ? "sin dato (verano)" : season == 1 ? "invierno" : season == 0 ? "verano" : "otra (como verano)", wth,
        full ? ": tipo 0x16 el local, 0x17 el visitante" : bb[0] == 1 ? ": local pieza 0, visitante pieza 8" : ": las dos pieza 8");
    // (2e) clips que se reparten: los de la sala de reunion solo con w518 = 1
    int allow[kNumClips], na = 0; const bool sala = Live3(3, 1.0f) > 0.5f;
    for (int i = 0; i < kNumClips; i++) if (!kClipDef[i].esc && kClipDef[i].vivo < 0 && (sala || !kClipDef[i].sala)) allow[na++] = i;
    for (int sd = 0; sd < 2; sd++) {
        static LiveSub tmp[40]; int ns = 0;
        for (int i = 11; i < conv[sd] && ns < 40; i++) {
            uint32_t idx = 0xff; if (!Rd(team[sd] + 4 + (uintptr_t)i * 4, idx) || idx >= 40) continue;
            LiveSub u{};
            if (!SafeCopy(u.player, (const void*)(team[sd] + 0xa8 + (uintptr_t)idx * 0x128 + 0x30), sizeof u.player)) continue;
            u.player[0x9c + 60] = 0;                                  // FUN_141f61520 copia el nombre con strcpy_s de 61
            memcpy(&u.id, u.player, 8);
            if (!u.id || (uint32_t)u.id == 0xffffu || (uint32_t)u.id == 0xffffffffu) continue;
            u.gk = u.player[8] == 0; u.side = sd; u.teamIdx = idx; u.place = i;
            char nm[62] = {}; memcpy(nm, u.player + 0x9c, 61); for (char& c : nm) if (c && (unsigned char)c < 0x20) c = '?';
            strncpy_s(u.name, nm, _TRUNCATE);
            tmp[ns++] = u;
        }
        if (cap > 0 && ns > cap) ns = cap;
        Spot sp[40]; int seats = 0, rows = 0;
        char rowDiag[400]; const int got = BuildSpots(ss, sd, ns, up, fwd, sp, 40, &seats, &rows, rowDiag, sizeof rowDiag);
        const int nb = std::min(got, (int)(got * pct / 100.0f + 0.5f));
        int ord[40]; for (int k = 0; k < got; k++) ord[k] = k;
        for (int k = 0; k < nb; k++) { const int j = k + (int)(Rnd() % (uint32_t)(got - k)); std::swap(ord[k], ord[j]); tmp[ord[k]].bib = true; tmp[ord[k]].bibByte = bb[sd]; tmp[ord[k]].kindOv = (uint8_t)(full ? (sd ? 0x17 : 0x16) : 0); }
        // clips: una vuelta mezclada de los permitidos, asi dos vecinos nunca comparten clip; (2b) cada uno arranca en un punto al azar del
        // clip (phase0, se acota al largo real en SubStart), asi los que repiten clip no se mueven juntos.
        int perm[kNumClips]; for (int k = 0; k < na; k++) perm[k] = allow[k];
        for (int k = 0; k < na - 1; k++) std::swap(perm[k], perm[k + (int)(Rnd() % (uint32_t)(na - k))]);
        for (int k = 0; k < got; k++) { tmp[k].clip = perm[k % na]; tmp[k].esc = 0; tmp[k].phase0 = (float)(Rnd() % 9000u) / 1000.0f; }
        // (23/11) escena horneada: 4 de cada 5 bancos. Los asientos se ordenan a lo largo del banco (eje x de la raiz: con el giro a,
        // (cos a, 0, -sin a), el mismo marco del clip) y el k-esimo toma el jugador k (desde un corrimiento al azar) del orden de la
        // vista; todos con el mismo punto de arranque. Los que sobran (mas de 9) siguen con los clips de siempre.
        char escDiag[64] = "sin escena (clips de siempre)";
        if (got > 0 && (Rnd() % 5u) != 0u) {
            int e = 1 + (int)(Rnd() % (uint32_t)kNumEsc); if (e == escUsada) e = 1 + (e % kNumEsc);
            float lx[40]; int byx[40];
            for (int k = 0; k < got; k++) { const float a = sp[k].yaw * 0.017453292f; lx[k] = sp[k].x * cosf(a) - sp[k].z * sinf(a); byx[k] = k; }
            std::sort(byx, byx + got, [&](int a, int b) { return lx[a] > lx[b]; });
            const int nE = std::min(got, kEscJug), off = (int)(Rnd() % (uint32_t)(kEscJug - nE + 1));
            const float ph = (float)(Rnd() % 50000u) / 1000.0f;
            int puestos = 0;
            for (int r = 0; r < nE; r++) { const int c = EscClip(e, kEscOrden[off + r]); if (c < 0) continue; LiveSub& u = tmp[byx[r]]; u.clip = c; u.esc = e; u.phase0 = ph; puestos++; }
            escUsada = e;
            sprintf_s(escDiag, "escena %d en %d de %d (jugadores %d..%d de la vista)", e, puestos, got, off, off + nE - 1);
        }
        for (int k = 0; k < got && S.n < 48; k++) {
            LiveSub& u = tmp[k]; u.spot = sp[k]; u.order = S.n; u.start = 0.0f;
            S.s[S.n++] = u;
        }
        Log("banca: %s: %d convocados, %d suplentes, %d asientos en %d fila(s) (%s); se sientan %d, %d con pechera (%.0f %%); %s; %s", sd ? "visitante" : "local", (int)conv[sd], ns,
            seats, rows, ss.pre, got, nb, pct, rowDiag, escDiag);
    }
    return 0;
}

// (2a-2) solo lectura, desde Present: anota una vez por partido los suplentes de verdad de cada equipo (los que se van a sentar),
// para poder comprobarlos aunque el reloj no corra
void LogRealSubs() {
    static uint64_t lastSig = 0;
    uintptr_t m = 0; if (!Rd(kMatchPtr, m) || !Heap(m)) { lastSig = 0; return; }
    char line[2][900] = {}; int cnt[2] = {}; uint8_t conv[2] = {}; uint64_t sig = (uint64_t)m;
    for (int sd = 0; sd < 2; sd++) {
        uintptr_t team = 0;
        if (!Rd(m + 0x1978 + (uintptr_t)sd * 0x28, team) || !Heap(team) || !Rd(team + 0x2eec, conv[sd]) || conv[sd] < 11 || conv[sd] > 40) return;
        size_t pos = 0;
        for (int i = 11; i < conv[sd]; i++) {
            uint32_t idx = 0xff; if (!Rd(team + 4 + (uintptr_t)i * 4, idx) || idx >= 40) continue;
            uint8_t pl[0xF0] = {}; if (!SafeCopy(pl, (const void*)(team + 0xa8 + (uintptr_t)idx * 0x128 + 0x30), sizeof pl)) continue;
            uint64_t id = 0; memcpy(&id, pl, 8); uint16_t num = 0; memcpy(&num, pl + 0x86, 2);
            char nm[62] = {}; memcpy(nm, pl + 0x9c, 60); for (char& c : nm) if (c && (unsigned char)c < 0x20) c = '?';
            sig = sig * 1099511628211ull ^ id;
            if (pos + 1 < sizeof line[sd]) {   // _TRUNCATE: un nombre largo corta la linea, no llama al manejador de errores del CRT
                const int w = _snprintf_s(line[sd] + pos, sizeof line[sd] - pos, _TRUNCATE, "%s%s (%u, id %llu%s)", cnt[sd] ? ", " : "", nm, (unsigned)num,
                                          (unsigned long long)id, pl[8] == 0 ? ", arquero" : "");
                pos = w < 0 ? sizeof line[sd] : pos + (size_t)w;
            }
            cnt[sd]++;
        }
    }
    if (sig == lastSig) return;
    lastSig = sig;
    for (int sd = 0; sd < 2; sd++) Log("banca: suplentes de verdad del %s (%d convocados, %d suplentes): %s", sd ? "visitante" : "local", (int)conv[sd], cnt[sd], line[sd]);
}

// (23/11 v7d, Agustin: "otra forma de optimizar"; log: el hilo que arma los dibujos 23-72 ms por cuadro, la placa espera < 1 ms).
// Fila 521: x esconder al suplente que no entra en cuadro (y sus sombras: no se dibuja), y animar menos al escondido (1 de cada 3
// cuadros). Cuadro = vista x proyeccion de SceneCB (config.cpp, cuadro anterior); se prueban los pies y la cabeza con 25 % de margen.
// Se muestra en el acto al entrar y se esconde recien a los 0.6 s fuera. Sin camara reciente (SceneCB viejo), todos a la vista.
float Live521(int k, float def) { return (!g_cfg.info[521][k].defined && g_cfg.live[521][k] == 0.0f) ? def : g_cfg.live[521][k]; }
bool CamPV(float M[16]) {
    for (int tries = 0; tries < 3; tries++) {
        const unsigned a = g_camSeq.load(std::memory_order_acquire); if (a & 1u) continue;
        for (int i = 0; i < 16; i++) M[i] = g_camPV[i].load(std::memory_order_relaxed);
        if (g_camSeq.load(std::memory_order_acquire) == a) return a != 0 && Now() - g_camT.load(std::memory_order_relaxed) < 0.5;
    }
    return false;
}
bool InView(const float M[16], float x, float y, float z) {
    for (int k = 0; k < 2; k++) {
        const float yy = y + (k ? 1.45f : 0.05f);
        const float cx = M[0] * x + M[1] * yy + M[2] * z + M[3], cy = M[4] * x + M[5] * yy + M[6] * z + M[7], cw = M[12] * x + M[13] * yy + M[14] * z + M[15];
        if (cw > 0.05f && fabsf(cx) <= cw * 1.25f && fabsf(cy) <= cw * 1.25f) return true;
    }
    return false;
}
void SubsTick(void* host) {
    if (s_subsDead.load(std::memory_order_relaxed)) return;   // (los sentados se sueltan igual cuando el juego borra a sus jugadores)
    Subs& S = s_subs;
    const double now = Now();
    if (S.lastTick > 0 && now - S.lastTick < 0.002) return;          // una vez por cuadro
    const float dt = S.lastTick > 0 ? (float)std::min(0.1, now - S.lastTick) : 0.0f;
    S.lastTick = now; S.T += dt;
    // (2d, Agustin 17/11: "no siguen el gameclock cuando se pausa el juego"; "el gameclock es lo mismo que usa la fisica de la ropa")
    // La animacion de los sentados va con el reloj del juego: la fila 157 (config.cpp) = m_timeParam.x de SysCB, el mismo que usan
    // cloth-mod (game_clock = 1), los gestos (AnimT) y las arrugas (WrClock): la pausa lo para, la camara lenta lo frena, el salto
    // atras al empezar una repeticion no cuenta. Hasta que ese reloj se reconoce (157.w = 0), el real. S.T (real) queda para los
    // tiempos de espera de la carga.
    float adt = dt;
    if (g_cfg.live[157][3] >= 0.5f) {
        const double g = (double)g_cfg.live[157][0];
        double dG = S.lastG < 0.0 ? 0.0 : g - S.lastG; if (dG < -50000.0) dG += 100000.0;   // vuelta del reloj (100000 s)
        S.lastG = g; s_animGame.store(true, std::memory_order_relaxed);
        (void)dG;
    } else { S.lastG = -1.0; s_animGame.store(false, std::memory_order_relaxed); }
    {   // (23/11 v7d, Agustin: "la idea es que vaya como la fisica de la ropa con el game clock"; probo moviendo la repeticion y no se
        // animaban). Lo que se vio en su log: (1) en la repeticion el reloj de la composicion (fila 157) queda QUIETO (21.039 durante
        // minutos) -> ahora tambien se lee el de los jugadores (cPSSystem de la piel de la cara, SysCBP) y vale el que avance; (2) con
        // pocos FPS el juego avanza su reloj 1/60 por cuadro aunque el cuadro dure mas (por eso iban a 0.25-0.4: el mod de ropa
        // tambien lo corrige) -> el ritmo es avance / min(cuadro real, 1/60), suavizado 0.3 s, tope 1. Pausa = 0, camara lenta < 1.
        // Hasta que llegue algun reloj del juego, el tiempo real.
        static float lc = -1.0f, lp = -1.0f; static unsigned nc = 0, np = 0; static float rateS = 1.0f, tAcc = 0.0f, sinCopia = 0.0f;
        const unsigned nc2 = g_clkCompN.load(std::memory_order_relaxed), np2 = g_clkPlayerN.load(std::memory_order_relaxed);
        const float c = g_clkComp.load(std::memory_order_relaxed), pl = g_clkPlayer.load(std::memory_order_relaxed);
        tAcc += dt; sinCopia += dt;
        if (nc2 != nc || np2 != np) {                    // llego una copia nueva de algun reloj
            float dc = (lc >= 0.0f && nc2 != nc) ? c - lc : 0.0f, dp = (lp >= 0.0f && np2 != np) ? pl - lp : 0.0f;
            if (fabsf(dc) > 0.5f) dc = 0.0f; if (fabsf(dp) > 0.5f) dp = 0.0f;   // saltos (empieza la repeticion, vuelta); (v7e) hacia atras vale
            // (v7e, Agustin: "al retroceder no se ejecutan las animaciones al reves") el que se movio: en la repeticion la composicion
            // queda quieta (0) y el de los jugadores va para atras (negativo); antes max() lo tapaba con el 0 y el tope era 0..1

            const unsigned fr = std::max(nc2 - nc, np2 - np);                                  // cuadros del juego desde la ultima copia
            const float d = fabsf(dc) >= fabsf(dp) ? dc : dp, norm = std::max(1e-4f, std::min(tAcc, (float)fr / 60.0f));   // el juego avanza a lo sumo 1/60 por cuadro
            // (v7f, Agustin 24/11: "las animaciones se ven toscas y raras") con tope 4 el ritmo saltaba 0.4 / 1.3 / -0.2 entre copias
            // (log 02:45-02:47): tope -1..1 otra vez, zona muerta chica (el ruido alrededor de 0 ya no las mueve para atras y
            // adelante) y suavizado mas largo (0.6 s)
            float r = std::clamp(d / norm, -1.0f, 1.0f); if (fabsf(r) < 0.06f) r = 0.0f;
            const float k = std::min(1.0f, tAcc / 0.6f); sinCopia = 0.0f;
            rateS += (r - rateS) * k; tAcc = 0.0f;
            if (nc2 != nc) lc = c; if (np2 != np) lp = pl; nc = nc2; np = np2;
        }
        // (v7f, Agustin 24/11: "los modelos no estan animados fuera de la vista del shader ... como antes") los dos relojes los copian
        // las reglas de shaders (SysCB / SysCBP): con los shaders apagados (vista original) no llega ninguna copia y el ritmo quedaba
        // con el ultimo valor (quietos). Sin copia nueva en 0.3 s: tiempo real, como antes de v7d
        const bool known = (nc2 > 60 || np2 > 60) && sinCopia < 0.3f;   // algun reloj del juego llega (pausa: rateS -> 0, quietos como la ropa)
        if (!known) rateS = 1.0f;
        adt = known ? dt * rateS : dt;
        s_animGame.store(known, std::memory_order_relaxed);
    }
    // (v7e) S.A sigue solo para adelante (vf4, vueltas de escena, esperas y sorteos van con el); lo de ir para atras (retroceder la
    // repeticion) se junta por suplente en u.back y SubUpdate lleva el clip para atras con FUN_141afa7b0 (vf4 ve avance 0)
    const float back = std::min(0.0f, adt);
    S.A += std::max(0.0f, adt);                        // (vf4 recibe A - t0: los dos del mismo reloj; sin vuelta, como S.T)
    if (s_subActive.load(std::memory_order_relaxed) > 0) { s_animSum.store(s_animSum.load(std::memory_order_relaxed) + adt, std::memory_order_relaxed); s_realSum.store(s_realSum.load(std::memory_order_relaxed) + dt, std::memory_order_relaxed); }
    s_subTicks.fetch_add(1, std::memory_order_relaxed);
    const bool on = Live(0, 0.0f) > 0.5f && Live(1, 1.0f) > 0.5f && Live2(0, 1.0f) > 0.5f;
    if (!on) { if (S.phase != kIdle) SubsDestroyAll("opcion apagada"); return; }
    uintptr_t m = 0; Rd(kMatchPtr, m);
    if (S.phase != kIdle && m != S.match) { SubsDestroyAll("cambio el partido"); return; }
    switch (S.phase) {
    case kIdle:
        if (s_ended.load()) return;                                     // (2b) termino el partido: hasta que cargue otro no se arma la lista
        // (2b) en la carga, recien cuando match::Match empieza a crear a los jugadores (estado 6): el estadio ya esta
        if (s_okLoad && !s_live.load() && s_matchState.load() < 6) return;
        if (now < S.tRetry) return;
        S.tRetry = now + 0.25;
        {
            static int lastWhy = -1; const int why = SubsPrepare();
            s_prepWhy.store(why);
            if (why != lastWhy) { lastWhy = why; if (why) Log("banca: suplentes reales: todavia no (%s)", kPrepWhy[why]); }
            if (why) return;
        }
        if (!S.n) { S.phase = kOff; Log("banca: suplentes reales: no hay a quien sentar en este partido (sin asientos de ese estadio o sin suplentes)"); return; }
        S.phase = kWait; S.tPhase = now; S.T = 0;
        return;
    case kWait: {
        if (now - S.tPhase < 0.5) return;
        uintptr_t sheet = 0;
        // (2c) en la pantalla de carga la ficha de las cinematicas puede no estar todavia. Solo se usa para estacion / clima del
        // LoadId (FUN_141f61520 la pide con FUN_141f58cb0, que la crea si falta) y eso se pisa con lo de la cancha; lo que hace
        // falta de verdad es el recurso de clips "fixdemo". Se sigue con cualquiera de los dos.
        const bool haveSheet = Rd(kSheetPtr, sheet) && Heap(sheet), haveRes = !haveSheet && FixdemoRes() != nullptr;
        if (!haveSheet && !haveRes) { if (!S.sheetSaid) { S.sheetSaid = true; Log("banca: suplentes reales: esperando la ficha de las cinematicas o el recurso \"fixdemo\""); } return; }
        if (haveRes) Log("banca: suplentes reales: sin ficha de las cinematicas todavia, pero esta el recurso \"fixdemo\": se sigue");
        int ready = 0;
        for (int i = 0; i < kNumClips; i++) {
            sprintf_s(s_clip[i].path, "cpk_dat/common/demo/anime/FoxAnim/FixDemo/Animations/%s.gani", kClipDef[i].name);
            s_clip[i].asked = false;
            const int q = ClipQuery(i);
            if (q < 0) { s_subsDead.store(true); Log("banca: suplentes reales: error al buscar el clip %s en el gestor: se apaga", kClipDef[i].name); return; }
            if (q) ready++;
        }
        s_protect.store(1);
        for (int i = 0; i < kNumClips; i++) if (!s_clip[i].ok && !ClipAsk(i)) { s_subsDead.store(true); Log("banca: suplentes reales: error al pedir el clip %s: se apaga", kClipDef[i].name); return; }
        Log("banca: suplentes reales: %d de %d clips de sentado ya estaban cargados, el resto se pidio", ready, kNumClips);
        S.phase = kClips; S.tPhase = now; S.pumpOwn = false;
        return; }
    case kClips: {
        int ready = 0;
        for (int i = 0; i < kNumClips; i++) { const int q = ClipQuery(i); if (q < 0) { s_subsDead.store(true); return; } if (q) ready++; }
        const double w = now - S.tPhase;
        if (ready < kNumClips && w > 3.0) {                            // nadie los esta leyendo: se llama a la bomba del juego
            if (!S.pumpOwn) { S.pumpOwn = true; Log("banca: suplentes reales: los clips no cargan solos en el partido (%d de %d): se llama a la bomba de carga del juego", ready, kNumClips); }
            if (!PumpOnce()) { s_subsDead.store(true); Log("banca: suplentes reales: error en la bomba de carga: se apaga"); return; }
        }
        if (ready < kNumClips && w < 20.0) return;
        if (!ready) { S.phase = kOff; Log("banca: suplentes reales: ningun clip de sentado cargo en 20 s: no se sienta a nadie en este partido"); return; }
        if (ready < kNumClips) for (int k = 0; k < S.n; k++) {
            LiveSub& u = S.s[k]; if (s_clip[u.clip].ok) continue;
            // (23/11) un clip de escena que no cargo (no esta instalado): vuelve a uno de los de siempre (no a otro jugador de la escena)
            int c = u.esc ? (int)(Rnd() % 3u) : u.clip;
            for (int t = 0; t < kNumClips && (!s_clip[c].ok || (u.esc && kClipDef[c].esc)); t++) c = (c + 1) % kNumClips;
            u.clip = c; u.esc = 0; u.phase0 = (float)(Rnd() % 9000u) / 1000.0f;
        }
        for (int k = 0; k < S.n; k++) {                                // (23/11) clip de siempre -> su version viva, si cargo
            LiveSub& u = S.s[k]; if (u.esc || u.clip < 0 || kClipDef[u.clip].vivo >= 0) continue;
            for (int i = 0; i < kNumClips; i++) if (kClipDef[i].vivo == u.clip && s_clip[i].ok) { u.clip = i; u.phase0 = (float)(Rnd() % 50000u) / 1000.0f; break; }
        }
        if (!FixdemoRes()) {                                            // (2b) en la carga puede faltar todavia: se espera
            if (!S.resSaid) { S.resSaid = true; Log("banca: suplentes reales: esperando el recurso de clips \"fixdemo\""); }
            if (w < 60.0) return;
            S.phase = kOff; Log("banca: suplentes reales: no esta el recurso de clips \"fixdemo\" en 60 s: no se sienta a nadie"); return;
        }
        Log("banca: suplentes reales: %d clips listos en %.1f s%s; se crean %d suplentes, hasta %d a la vez", ready, w, S.pumpOwn ? " (con la bomba propia)" : "", S.n, kParallel);
        S.phase = kRun; S.tPhase = now; S.tSpawn = now; S.lastCheck = now; S.tProgress = now;
        return; }
    case kRun: break;
    default: return;
    }
    // (2b) crear: hasta kParallel cargando a la vez (uno nuevo por cuadro, en el orden de la lista)
    {
        int loading = 0, pending = 0, next = -1;
        for (int k = 0; k < S.n; k++) {
            const int st = S.s[k].state;
            if (st == 1) loading++;
            if (st == 0 || st == 1) pending++;
            if (st == 0 && next < 0) next = k;
        }
        if (next >= 0 && loading < kParallel && now - S.tSpawn >= 0.03) {
            LiveSub& u = S.s[next];
            char why[96] = {};
            if (!BudgetOk(why, sizeof why)) {
                int left = 0; for (int k = next; k < S.n; k++) if (S.s[k].state == 0) { S.s[k].state = 3; left++; }
                Log("banca: suplentes reales: no se crean mas (%s); quedan %d sin crear de %d", why, left, S.n);
            } else {
                bool haveSW = false; uint8_t season = 0, weather = 0;
                if (host) {                                               // manga / clima como los de la cancha
                    uintptr_t h = 0, vt = 0; uint8_t a = 0, b = 0;
                    if (Rd((uintptr_t)host + 0x7d0, h) && Heap(h) && Rd(h, vt) && vt == kVtDrawHuman && Rd(h + 0xa8 + 0x4f, a) && Rd(h + 0xa8 + 0x50, b)) { haveSW = true; season = a; weather = b; }
                }
                if (S.cur == 0) {
                    uintptr_t sheet = 0; uint8_t a = 0xff, b = 0xff; if (Rd(kSheetPtr, sheet) && Heap(sheet)) { Rd(sheet + 0x5c, a); Rd(sheet + 0x58, b); }
                    Log("banca: suplentes reales: LoadId +0x4f / +0x50 de la cancha %s %u / %u; de la ficha de cinematicas %u / %u", haveSW ? "" : "(no leido)", season, weather, a, b);
                }
                S.cur++;                                                  // creados (con o sin exito)
                int c0 = 0, c1 = 0; BudgetRead(0, c0, c1);
                Log("banca: creando suplente %d de %d: %s %llu %s (lugar %d%s%s) en x %.2f y %.2f z %.2f, clip %s (giro %+.0f) desde %.1f s; texturas usadas %d de %d; %d cargando",
                    next + 1, S.n, u.side ? "visitante" : "local", (unsigned long long)u.id, u.name, u.place, u.gk ? ", arquero" : "",
                    !u.bib ? "" : u.kindOv ? ", campera entera" : u.bibByte == 1 ? ", pechera (pieza 0)" : ", pechera (pieza 8)",
                    u.spot.x, u.spot.y, u.spot.z, kClipDef[u.clip].name, kClipDef[u.clip].yaw, u.phase0, c1, c0, loading);
                S.tSpawn = now;
                if (SubCreate(u, haveSW, season, weather)) { u.state = 1; u.tCreate = now; u.tCreateT = S.T; S.tProgress = now; }
                else if (s_subsDead.load()) { Log("banca: suplentes reales: ERROR dentro del juego al crear: no se toca nada mas en esta sesion"); return; }
                else { u.state = 3; Log("banca: suplentes reales: sin memoria para el suplente %d", next + 1); }
            }
        }
        for (int k = 0; k < S.n; k++) {
            LiveSub& u = S.s[k]; if (u.state != 1) continue;
            const int r = SubPoll(u);
            if (r < 0) { u.state = 5; Log("banca: suplentes reales: ERROR dentro del juego al cargar a %s: no se toca nada mas en esta sesion", u.name); return; }
            if (r == 1) {
                void* res = FixdemoRes();
                if (res && SubStart(u, res, S.A)) {
                    u.state = 2; s_subActive.fetch_add(1); S.tProgress = now;
                    // (v7e, Agustin 23/11 19:22: "los lods provocan que no se carguen correctamente las animaciones": de lejos no se
                    // animaban) y no daban FPS (el cuello es la CPU de dibujos): SubLod ya no se llama, modelo completo como antes de v7c
                    // SubLod(u);
                    void* face = nullptr; memcpy(&face, (uint8_t*)u.fd + 0x58, 8);
                    Log("banca: suplente %d sentado: %s (cargo en %.1f s; cara y manos %s; estatura %.0f cm, base %+.1f cm)", k + 1, u.name, now - u.tCreate,
                        face ? "con controlador" : u.faceTried ? "SIN controlador (fallo vf3)" : "esperando los recursos del juego", u.heightCm, u.dyCm);
                } else {
                    if (s_subsDead.load()) { Log("banca: suplentes reales: ERROR dentro del juego al dar el clip a %s: no se toca nada mas en esta sesion", u.name); return; }
                    Log("banca: suplentes reales: %s cargo pero no tomo el clip: se saca", u.name);
                    FdDestroy(u.fd); u.fd = nullptr; u.state = 3;
                }
            } else if (S.T - u.tCreateT > 30.0f) {   // 30 s de reloj de suplentes (en la carga tambien corre)
                Log("banca: suplentes reales: %s no cargo en 30 s: se saca", u.name);
                FdDestroy(u.fd); u.fd = nullptr; u.state = 3;
            }
        }
        if (pending && !S.doneSaid) {
            bool any = false; for (int k = 0; k < S.n && !any; k++) any = S.s[k].state == 0 || S.s[k].state == 1;
            if (!any) { S.doneSaid = true; Log("banca: suplentes reales: listo, %d sentados de %d en %.1f s", s_subActive.load(), S.n, now - S.tPhase); }
        }
    }
    // (2e) probar clips (z518): 1..kNumClips = todos con ese clip, 0 = cada uno el suyo; al momento, sin recargar el partido
    {
        int want = (int)(Live3(2, 0.0f) + 0.5f); if (want < 0 || want > kNumClips) want = 0;
        if (want && !s_clip[want - 1].ok) want = 0;                    // ese clip no cargo
        if (want != S.test) {
            void* res = FixdemoRes(); int n = 0, bad = 0;
            if (res) {
                for (int k = 0; k < S.n; k++) {
                    LiveSub& u = S.s[k]; if (u.state != 2 || !u.fd) continue;
                    const int c = want ? want - 1 : u.clip; if (c == u.shown) continue;
                    if (SubSwitch(u, res, c, S.A)) n++; else bad++;
                    if (s_subsDead.load()) { Log("banca: suplentes reales: ERROR dentro del juego al cambiar el clip: no se toca nada mas en esta sesion"); return; }
                }
                S.test = want;
                if (want) Log("banca: probar clips: %d de %d con el %d de %d, %s (giro %+.0f)%s", n, s_subActive.load(), want, kNumClips, kClipDef[want - 1].name, kClipDef[want - 1].yaw, bad ? ", con fallos" : "");
                else Log("banca: probar clips: apagado, %d vuelven a su clip", n);
            }
        }
    }
    // mover a los sentados
    void* resE = nullptr; bool resEAsked = false;
    float camM[16]; const bool camOk = CamPV(camM);
    for (int k = 0; k < S.n; k++) {
        LiveSub& u = S.s[k]; if (u.state != 2 || !u.fd) continue;
        const ClipDef& cd_ = kClipDef[u.shown >= 0 ? u.shown : u.clip];
        if ((cd_.esc || cd_.vivo >= 0) && adt > 0.0f) {       // (23/11) escena o version viva: vuelta con fundido
            if (!resEAsked) { resEAsked = true; resE = FixdemoRes(); }
            const int r = EscLoop(u, resE, S.A);
            if (r < 0) { u.state = 5; Log("banca: suplentes reales: ERROR dentro del juego al dar la vuelta la escena de %s: no se toca nada mas en esta sesion", u.name); return; }
            if (r > 0 && s_escFades.load(std::memory_order_relaxed) <= 2) Log("banca: %s %d: %s da la vuelta con fundido de %.1f s", cd_.esc ? "escena" : "vivo", cd_.esc ? cd_.esc : cd_.vivo, u.name, kEscFade);
        }
        if (Live521(0, 1.0f) > 0.5f && camOk) {             // (v7d) fuera de cuadro
            if (InView(camM, u.spot.x, u.spot.y, u.spot.z)) { u.offT = 0.0f; u.off = false; } else { u.offT += dt; if (u.offT > 0.6f) u.off = true; }
        } else u.off = false;
        u.back += back;                                       // (v7e) retroceso pendiente (los salteados lo juntan)
        if (u.off && Live521(1, 1.0f) > 0.5f && (++u.skip % 3) != 0) { s_offSkipped.fetch_add(1, std::memory_order_relaxed); continue; }
        const int r = SubUpdate(u, S.A, adt > 0.0f);
        if (r < 0) { u.state = 5; Log("banca: suplentes reales: ERROR dentro del juego al mover a %s: no se toca nada mas en esta sesion", u.name); return; }
        if (r == 2 && !S.seekSaid) { S.seekSaid = true; Log("banca: suplentes reales: el clip no da la vuelta solo; se lo vuelve al principio a mano"); }
    }
    // cada 2 s: el que entro a la cancha deja el banco
    if (now - S.lastCheck >= 2.0) {
        S.lastCheck = now;
        for (int k = 0; k < S.n; k++) {
            LiveSub& u = S.s[k]; if (u.state != 2 || !u.fd) continue;
            bool onPitch = false;
            for (int i = 0; i < 11 && !onPitch; i++) { uint32_t idx = 0xff; if (Rd(S.team[u.side] + 4 + (uintptr_t)i * 4, idx)) onPitch = idx == u.teamIdx; }
            if (!onPitch) continue;
            Log("banca: %s entro a la cancha: deja el banco", u.name);
            FdDestroy(u.fd); u.fd = nullptr; u.state = 3; s_subActive.fetch_sub(1);
        }
    }
}

// Reloj: el jugador 0 del partido, una vez por cuadro. Si el 0 no aparece en medio segundo (expulsado, otro orden) vale
// cualquier jugador, no mas de una vez cada 8 ms. El hilo se anota (el heap "actual" es por hilo y se pone en cada llamada).
std::atomic<unsigned> s_pushCalls{0}, s_threadChanges{0};
bool TickGate(uint32_t idx) {
    s_pushCalls.fetch_add(1, std::memory_order_relaxed);
    static std::atomic<double> last0{0.0}, lastAny{0.0};
    const double now = Now();
    if (idx == 0) last0.store(now, std::memory_order_relaxed);
    else {
        if (now - last0.load(std::memory_order_relaxed) < 0.5) return false;
        if (now - lastAny.load(std::memory_order_relaxed) < 0.008) return false;
    }
    lastAny.store(now, std::memory_order_relaxed);
    const unsigned long tid = GetCurrentThreadId();
    const unsigned long prev = s_srcThread.exchange(tid, std::memory_order_relaxed);
    s_pushThread.store(tid, std::memory_order_relaxed);
    if (prev != tid) {
        const unsigned n = s_threadChanges.fetch_add(1, std::memory_order_relaxed);
        if (n < 4) Log("banca: reloj de los suplentes reales: jugador %u, hilo %lu%s", idx, tid, n ? " (cambio de hilo)" : "");
    }
    s_src.store(1, std::memory_order_relaxed);
    return true;
}

void __fastcall hk_HumanPush(void* self) {
    if (s_okSubs && self) {
        const uint32_t idx = *(const uint32_t*)((const uint8_t*)self + 0x18);
        s_live.store(true, std::memory_order_relaxed);               // los jugadores se dibujan: el partido esta en juego
        if (!s_okLoad) s_ended.store(false, std::memory_order_relaxed);   // sin los ganchos de la carga, como en 2a
        // (2b) el reloj es el del modo partido; este (hilos de trabajo) queda de respaldo si aquel no corre
        const bool modeFresh = s_okLoad && Now() - s_modeT.load(std::memory_order_relaxed) < 0.25;
        if (!modeFresh && idx < 22 && TickGate(idx)) {
            std::lock_guard<std::recursive_mutex> lk(s_subMx);
            SubsTick(self);
        }
    }
    o_humanPush(self);
}
// el juego borra a un jugador del partido: antes se sueltan los nuestros (la escena se va detras)
// (2b) en la carga (antes de que arranque el partido) no: los suplentes ya se crean ahi y el juego puede borrar jugadores al armar el suyo
void __fastcall hk_HumanDtor(void* self, void* a2, void* a3, void* a4) {
    if (s_okSubs) {
        std::lock_guard<std::recursive_mutex> lk(s_subMx);
        if (s_live.load() || !s_okLoad) {
            MotorLabMatchEnd();                          // (21/11) los del laboratorio tambien, antes de que se vaya la escena
            if (s_subs.phase != kIdle) SubsDestroyAll("termino el partido");
            s_live.store(false); s_ended.store(true);
            s_matchEnd.store(GetTickCount64());
        } else if (s_subs.phase != kIdle) {
            static unsigned said = 0;
            if (said++ < 3) Log("banca: el juego borro un jugador del partido durante la carga: los suplentes reales siguen (fase %d)", s_subs.phase);
        }
    } else s_matchEnd.store(GetTickCount64());
    o_humanDtor(self, a2, a3, a4);
}

// (2b) reloj: actualizar el modo partido (carga, partido, cinematicas, repeticiones), una vez por cuadro en el hilo del juego
void __fastcall hk_ModeUpd(void* self, void* p2, void* p3) {
    if (s_okSubs && s_okLoad) {
        s_modeCalls.fetch_add(1, std::memory_order_relaxed);
        const unsigned long tid = GetCurrentThreadId(), prev = s_modeThread.exchange(tid, std::memory_order_relaxed);
        if (prev != tid) { static std::atomic<unsigned> n{0}; if (n.fetch_add(1) < 4) Log("banca: reloj de los suplentes = actualizar el modo partido, hilo %lu%s", tid, prev ? " (cambio de hilo)" : ""); }
        s_modeT.store(Now(), std::memory_order_relaxed);
        std::lock_guard<std::recursive_mutex> lk(s_subMx);
        SubsTick(nullptr);
    }
    MotorLabGameTick();                                  // (21/11) jugadores del laboratorio de animaciones (motorlab.cpp)
    o_modeUpd(self, p2, p3);
}

// (2b) espera de la carga. Se espera mientras se pueda llegar: lista por armar (estadio o equipos sin listos), clips cargando,
// suplentes por crear o cargando; nunca mas de kHoldMax, ni 15 s sin que se siente nadie. Dentro de s_subMx.
struct Hold { uintptr_t mobj = 0; double t0 = 0; bool on = false, done = false; };
Hold s_hold;
bool HoldWanted(double now, char* why, size_t n) {
    Subs& S = s_subs;
    if (s_subsDead.load()) { strcpy_s(why, n, "error dentro del juego, suplentes apagados"); return false; }
    if (!(Live(0, 0.0f) > 0.5f && Live(1, 1.0f) > 0.5f && Live2(0, 1.0f) > 0.5f)) { strcpy_s(why, n, "opcion apagada"); return false; }
    const double w = now - s_hold.t0;
    if (w > kHoldMax) { sprintf_s(why, n, "tope de %.0f s", kHoldMax); return false; }
    switch (S.phase) {
    case kIdle: {
        const int r = s_prepWhy.load();
        if (r == -1 || r == 3 || r == 5) { if (w < 12.0) return true; sprintf_s(why, n, "no se armo la lista en 12 s: %s", r < 0 ? "sin probar" : kPrepWhy[r]); return false; }
        sprintf_s(why, n, "%s", r > 0 && r < 6 ? kPrepWhy[r] : "sin lista"); return false; }
    case kWait: case kClips:
        if (w < 25.0) return true;
        strcpy_s(why, n, "los clips de sentado no estuvieron en 25 s"); return false;
    case kRun: {
        int pend = 0; for (int k = 0; k < S.n; k++) if (S.s[k].state == 0 || S.s[k].state == 1) pend++;
        if (!pend) { strcpy_s(why, n, "todos listos"); return false; }
        if (now - S.tProgress > 15.0) { sprintf_s(why, n, "15 s sin avance, faltan %d", pend); return false; }
        return true; }
    default: strcpy_s(why, n, "no hay a quien sentar"); return false;
    }
}
void __fastcall hk_MatchVf1(void* self, void* p2) {
    if (s_okSubs && s_okLoad && self) {
        int st = -1; Rd((uintptr_t)self + 8, st);
        s_matchState.store(st, std::memory_order_relaxed);
        if (st >= 0 && st < 0x1c) {                                   // un partido cargando: lo de antes ya termino
            std::lock_guard<std::recursive_mutex> lk(s_subMx);
            s_ended.store(false); s_live.store(false);
            if (s_hold.mobj != (uintptr_t)self || s_hold.done || s_hold.on) s_hold = Hold{};
        }
        if (st == 0x1c && _ReturnAddress() == (void*)kMatchVf1RetStep0f) {
            std::lock_guard<std::recursive_mutex> lk(s_subMx);
            const double now = Now();
            if (s_hold.mobj != (uintptr_t)self) { s_hold = Hold{}; s_hold.mobj = (uintptr_t)self; s_hold.t0 = now; }
            if (!s_hold.done) {
                SubsTick(nullptr);                                     // por si el reloj del modo partido no corre
                char why[128] = {};
                if (HoldWanted(now, why, sizeof why)) {
                    if (!s_hold.on) { s_hold.on = true; Log("banca: la carga espera a los suplentes reales (fase %d, %d en la lista)", s_subs.phase, s_subs.n); }
                    return;                                            // match::Match sigue en 0x1c: la pantalla de carga sigue
                }
                s_hold.done = true;
                Log("banca: la carga espero %.1f s a los suplentes reales: %d sentados de %d (%s)", s_hold.on ? now - s_hold.t0 : 0.0, s_subActive.load(), s_subs.n, why);
            }
        }
    }
    o_matchVf1(self, p2);
    if (s_okSubs && s_okLoad && self) { int st = 0; if (Rd((uintptr_t)self + 8, st) && st >= 0x1d) s_live.store(true, std::memory_order_relaxed); }
}
void SubsTickFromScene() {}   // (2a-2) la escena del estadio corre en hilos de trabajo distintos: ya no se usa de reloj

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
    if (!SigAt(kImplShow, kImplShowSig, sizeof kImplShowSig)) Log("banca: la firma de 0x1419b79f0 (la escena del estadio muestra cada modelo) no coincide: el banco de Konami se sigue viendo");
    else if (MH_CreateHook((void*)kImplShow, (void*)&hk_ImplShow, (void**)&o_implShow) == MH_OK && MH_EnableHook((void*)kImplShow) == MH_OK) s_okImpl = true;
    if (!SigAt(kFdmVf3, kFdmVf3Sig, sizeof kFdmVf3Sig)) Log("banca: la firma de FixDemoMain::vf3 (0x141f37770) no coincide: sin reloj, no hace nada");
    else if (MH_CreateHook((void*)kFdmVf3, (void*)&hk_Fdm, (void**)&o_fdm) == MH_OK && MH_EnableHook((void*)kFdmVf3) == MH_OK) s_okTick = true;
    if (!SigAt(kFdhVf1, kFdhVf1Sig, sizeof kFdhVf1Sig)) Log("banca: la firma de FixDemoHuman::vf1 (0x141f87e00) no coincide: sin sonda de cinematicas");
    else if (MH_CreateHook((void*)kFdhVf1, (void*)&hk_Fdh, (void**)&o_fdh) == MH_OK && MH_EnableHook((void*)kFdhVf1) == MH_OK) s_okProbe = true;
    {   // 16/11 paso 1h: figuras sentadas de los otros espantapajaros, al cargar cada fmdl
        const bool a = SigAt(kFmdlUp1, kFmdlUp1Sig, sizeof kFmdlUp1Sig), b = SigAt(kFmdlUp2, kFmdlUp2Sig, sizeof kFmdlUp2Sig);
        if (!a || !b) Log("banca: la firma de 0x141738a30 / 0x141739530 (fmdl a la placa) no coincide (%d %d): las figuras sentadas de otros modelos quedan", a, b);
        else if (MH_CreateHook((void*)kFmdlUp1, (void*)&hk_FmdlUp1, (void**)&o_fmdlUp1) == MH_OK && MH_EnableHook((void*)kFmdlUp1) == MH_OK) {
            if (MH_CreateHook((void*)kFmdlUp2, (void*)&hk_FmdlUp2, (void**)&o_fmdlUp2) == MH_OK && MH_EnableHook((void*)kFmdlUp2) == MH_OK) s_okFmdl = true;
            else { MH_DisableHook((void*)kFmdlUp1); MH_RemoveHook((void*)kFmdlUp1); o_fmdlUp1 = nullptr; }
        }
        Log("banca: figuras sentadas en el banco de los otros modelos del estadio (al cargar) %s", s_okFmdl ? "enganchado" : "NO");
    }
    {   // 16/11 paso 2a: suplentes reales sentados
        const bool a = SigAt(kHumanPush, kHumanPushSig, sizeof kHumanPushSig), b = SigAt(kHumanDtor, kHumanDtorSig, sizeof kHumanDtorSig), c = SigAt(kResUnload, kResUnloadSig, sizeof kResUnloadSig);
        if (!a || !b || !c || !s_okFmdl || !s_okImpl)
            Log("banca: suplentes reales NO: firmas de 0x1408bf4c0 / 0x1408be4f0 / 0x141b08080 %d %d %d, asientos (fmdl) %d, escena del estadio %d", a, b, c, s_okFmdl, s_okImpl);
        else if (MH_CreateHook((void*)kResUnload, (void*)&hk_ResUnload, (void**)&o_resUnload) == MH_OK && MH_EnableHook((void*)kResUnload) == MH_OK) {
            if (MH_CreateHook((void*)kHumanDtor, (void*)&hk_HumanDtor, (void**)&o_humanDtor) == MH_OK && MH_EnableHook((void*)kHumanDtor) == MH_OK) {
                if (MH_CreateHook((void*)kHumanPush, (void*)&hk_HumanPush, (void**)&o_humanPush) == MH_OK && MH_EnableHook((void*)kHumanPush) == MH_OK) s_okSubs = true;
                else { MH_DisableHook((void*)kHumanDtor); MH_RemoveHook((void*)kHumanDtor); MH_DisableHook((void*)kResUnload); MH_RemoveHook((void*)kResUnload); }
            } else { MH_DisableHook((void*)kResUnload); MH_RemoveHook((void*)kResUnload); }
        }
        Log("banca: suplentes reales sentados (paso 2a, fila 517) %s", s_okSubs ? "enganchado" : "NO");
    }
    if (s_okSubs) {   // 16/11 paso 2b: crearlos en la pantalla de carga y arrancar con todos sentados
        const bool a = SigAt(kModeUpd, kModeUpdSig, sizeof kModeUpdSig), b = SigAt(kMatchVf1, kMatchVf1Sig, sizeof kMatchVf1Sig);
        if (!a || !b) Log("banca: firmas de 0x141fc70d0 / 0x14041b380 distintas (%d %d): los suplentes se crean en el partido, como en 2a", a, b);
        else if (MH_CreateHook((void*)kMatchVf1, (void*)&hk_MatchVf1, (void**)&o_matchVf1) == MH_OK && MH_EnableHook((void*)kMatchVf1) == MH_OK) {
            if (MH_CreateHook((void*)kModeUpd, (void*)&hk_ModeUpd, (void**)&o_modeUpd) == MH_OK && MH_EnableHook((void*)kModeUpd) == MH_OK) s_okLoad = true;
            else { MH_DisableHook((void*)kMatchVf1); MH_RemoveHook((void*)kMatchVf1); o_matchVf1 = nullptr; }
        }
        Log("banca: suplentes reales en la pantalla de carga (paso 2b) %s", s_okLoad ? "enganchado" : "NO (se crean en el partido)");
    }
    Log("banca: escondido por la escena del estadio (cada cuadro) %s", s_okImpl ? "enganchado" : "NO");
    Log("banca: reloj %s, sonda de cinematicas %s, esconder el banco %s (fila 516: x %.0f)", s_okTick ? "enganchado" : "NO", s_okProbe ? "enganchada" : "NO",
        !s_okHide ? "NO (firma de 0x1419055f0 distinta)" : s_okVisHook ? (s_okApply ? "listo (SetVisible y al aplicar)" : "solo por SetVisible") : "solo en cinematicas (gancho de SetVisible fallo)", Live(0, 0.0f));
}
} // namespace

// (21/11 3icxbh) para el laboratorio de animaciones (motorlab.cpp): asientos del banco de un lado (mundo: x, y, z, giro en grados, como
// fd+8..0x14), los mismos que usan los suplentes reales (BuildSpots con la altura y el adelante / atras de la fila 517)
int BancaLabSpots(int side, int want, float (*out)[4], int max, char* why, size_t wn) {
    if (why && wn) why[0] = 0;
    if (!(Live(0, 0.0f) > 0.5f && Live(1, 1.0f) > 0.5f)) { if (why) sprintf_s(why, wn, "Prende Estadio > Banco de suplentes (con Esconder el banco de Konami = Si) y entra de nuevo al partido: asi se leen los asientos del banco"); return 0; }
    SeatSet ss; { std::lock_guard<std::mutex> lk(s_seatMx); ss = s_seats; }
    if (ss.n <= 0) { if (why) sprintf_s(why, wn, "%s", kPrepWhy[2]); return 0; }
    if (ss.t <= s_matchEnd.load()) {
        uintptr_t bm = 0, bb = 0, be = 0, bs = 0; char nm[64] = {};
        if (!FindBench(bm, bb, be, bs, nm, sizeof nm) || strncmp(nm, ss.pre, 5)) { if (why) sprintf_s(why, wn, "%s", kPrepWhy[4]); return 0; }
    }
    Spot sp[40]; int seats = 0, rows = 0;
    const int got = BuildSpots(ss, side & 1, std::min(want, 40), Live2(2, 0.0f), Live2(3, 0.0f), sp, 40, &seats, &rows);
    if (got <= 0 && why) sprintf_s(why, wn, "el banco %s no tiene asientos en este estadio (%d de %s)", side ? "visitante" : "local", ss.n, ss.pre);
    const int n = std::min(got, max);
    for (int k = 0; k < n; k++) { out[k][0] = sp[k].x; out[k][1] = sp[k].y; out[k][2] = sp[k].z; out[k][3] = sp[k].yaw; }
    return n;
}
// los suplentes de un lado del partido (convocados despues de los 11): id para la base (motorpj.cpp DbFind) y nombre
int BancaLabRealSubs(int side, uint32_t* ids, char (*names)[40], int max) {
    uintptr_t m = 0, team = 0; uint8_t conv = 0;
    if (!Rd(kMatchPtr, m) || !Heap(m) || !Rd(m + 0x1978 + (uintptr_t)(side & 1) * 0x28, team) || !Heap(team) || !Rd(team + 0x2eec, conv) || conv < 11 || conv > 40) return 0;
    int n = 0;
    for (int i = 11; i < conv && n < max; i++) {
        uint32_t idx = 0xff; if (!Rd(team + 4 + (uintptr_t)i * 4, idx) || idx >= 40) continue;
        uint8_t pl[0xF0] = {}; if (!SafeCopy(pl, (const void*)(team + 0xa8 + (uintptr_t)idx * 0x128 + 0x30), sizeof pl)) continue;
        uint64_t id = 0; memcpy(&id, pl, 8); if (!id || (uint32_t)id == 0xffffu || (uint32_t)id == 0xffffffffu) continue;
        ids[n] = (uint32_t)(id >> 32) ? (uint32_t)(id >> 32) : (uint32_t)(uint16_t)id;
        for (int c = 0; c < 39; c++) { const char ch = (char)pl[0x9c + c]; names[n][c] = ch && (unsigned char)ch < 0x20 ? '?' : ch; if (!ch) break; } names[n][39] = 0;
        n++;
    }
    return n;
}
bool BancaLabInMatch() { return s_okLoad && Now() - s_modeT.load(std::memory_order_relaxed) < 1.0; }

// (17/11 3icxbh) para motorpj.cpp: frenar la descarga de un .gani (hash del gestor) y reproducir un clip con vf1 sin pasar por la sonda
bool BancaGaniKeep(uint64_t h, bool on) {
    if (!h || !s_okSubs) return false;
    for (int i = 0; i < 48; i++) if (s_keepHash[i].load() == h) { if (!on) s_keepHash[i].store(0); return true; }
    if (!on) return true;
    for (int i = 0; i < 48; i++) { uint64_t z = 0; if (s_keepHash[i].compare_exchange_strong(z, h)) return true; }
    return false;
}
void BancaFdhPlay(void* fd, void* res, uint32_t idx, uint8_t a4, uint64_t startBits) { (o_fdh ? o_fdh : (FdhVf1)kFdhVf1)(fd, res, idx, a4, startBits, nullptr); }
void BancaPresent() {
    if (!s_tried) Install();
    if (!s_okTick && !s_okProbe && !s_okVisHook && !s_okApply && !s_okImpl && !s_okFmdl && !s_okSubs) return;
    {   // lineas del hilo de carga (paso 1h)
        char tmp[16][220]; int n = 0; unsigned lost = 0;
        { std::lock_guard<std::mutex> lk(s_fmMx); n = s_fmLogN; for (int i = 0; i < n; i++) strcpy_s(tmp[i], s_fmLog[i]); s_fmLogN = 0; lost = s_fmLost; s_fmLost = 0; }
        for (int i = 0; i < n; i++) Log("%s", tmp[i]);
        if (lost) Log("banca: (%u lineas de figuras del banco no anotadas)", lost);
    }
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
        char line[640]; int w = sprintf_s(line, "banca: bancos en escena %d, aplicados escondidos %u:", nb, s_applyBench.load(std::memory_order_relaxed));
        for (int i = 0; i < nb && w > 0 && w < 400; i++)
            w += sprintf_s(line + w, sizeof line - w, " [%s %p nodo %u visible %u dibujo %s]", bs[i].name, (void*)bs[i].body, bs[i].node, bs[i].vis,
                           !bs[i].drawOk ? "?" : (bs[i].drawFlags & 1) ? "ESCONDIDO" : "se dibuja");
        Log("%s", line);
        Log("banca: la escena del estadio quiso mostrar los suplentes de Konami %u veces (quedaron escondidos)", s_implHidden.load(std::memory_order_relaxed));
        ReadSheet();   // convocados tambien sin cinematica (solo lectura)
        if (s_okSubs) {   // paso 2a
            static unsigned lastSub = 0; const unsigned st = s_subTicks.load(std::memory_order_relaxed);
            LogRealSubs();
            static unsigned lastMode = 0; const unsigned md = s_modeCalls.load(std::memory_order_relaxed);
            Log("banca: suplentes reales %s: %d sentados, reloj %.1f por segundo (modo partido %.1f por segundo hilo %lu, estado del partido %d; jugadores hilo %lu, %u cambios de hilo, %u llamadas en total), %u descargas de clips frenadas; cara y manos: %u creados, %u fallos;  parpadeos %u, cambios de cara %u; LOD por distancia %u; fuera de cuadro: escondidos %u veces, cuadros salteados %u; para atras %u%s",
                Live2(0, 1.0f) > 0.5f ? "prendido" : "apagado", s_subActive.load(), nextT > 0 ? (st - lastSub) / 10.0 : 0.0, nextT > 0 ? (md - lastMode) / 10.0 : 0.0,
                s_modeThread.load(), s_matchState.load(), s_srcThread.load(), s_threadChanges.load(), s_pushCalls.load(), s_unloadBlocked.load(),
                s_faceMade.load(), s_faceFail.load(), s_blinks.load(), s_faceChanges.load(), s_lodSet.load(), s_offHidden.load(), s_offSkipped.load(), s_backSeeks.load(), s_subsDead.load() ? "; APAGADO por un error dentro del juego" : "");
            {   // (2d) cuanto avanzo la animacion de los sentados contra el tiempo real (pausa = 0, camara lenta < 1)
                const float a = s_animSum.exchange(0.0f), r = s_realSum.exchange(0.0f);
                if (r > 0.5f) Log("banca: animacion de los suplentes: %.1f s en %.1f s reales (%.2f; con el reloj %s)", a, r, a / r,
                                  s_animGame.load() ? "del juego (composicion o jugadores, corregido por FPS), como la ropa" : "real: el del juego todavia no se reconocio");
            }
            lastSub = st; lastMode = md;
        }
        lastTicks = tk; nextT = t + 10.0;
    }
    // el juego quiso mostrar el banco y se lo dejo escondido
    static unsigned seenShows = 0; const unsigned shows = s_gameShows.load(std::memory_order_relaxed);
    if (shows != seenShows) { Log("banca: el juego pidio mostrar el banco de Konami (%u veces en total): queda escondido", shows); seenShows = shows; }
    // modelos nuevos escondidos por la escena del estadio (una vez cada uno)
    static int seenImpl = 0; const int ni = s_implSeenN.load();
    for (; seenImpl < ni; seenImpl++) {
        const uintptr_t im = s_implSeen[seenImpl].load(); const uint32_t m = s_implSeenMask[seenImpl].load(); uintptr_t mdl = 0; Rd(im + 0x20, mdl);
        Log("banca: suplentes de Konami escondidos en la escena del estadio: modelo %p (grupo %s, kind %04X)", (void*)mdl,
            (m >> 16) == kGroupBench ? "BENCH" : "otro, por nombre _dir_bench", m & 0xffff);
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
