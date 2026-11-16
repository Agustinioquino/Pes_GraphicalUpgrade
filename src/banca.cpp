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
#include <vector>
#include <cstdarg>

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

__declspec(noinline) void ProcessFmdlInner(uint8_t* p, size_t avail) {
    {
        Fm f; if (!FmOpen(f, p, avail)) return;
        char pre[16], part[40]; if (!FmStadium(f, pre, part, sizeof part)) return;   // no es espantapajaros de estadio
        std::vector<FmMesh> ms; if (!FmMeshes(f, ms)) { FmLog("banca: %s_%s: no se pudo leer el fmdl, sin tocar", pre, part); return; }
        const bool isBench = !strcmp(part, "dir_bench");
        FmBenchFlags(f, ms, isBench);
        float mn[3], mx[3];
        if (FmBenchBox(ms, mn, mx)) BoxPut(pre, mn, mx);
        if (isBench) { FmLog("banca: %s_dir_bench cargado: caja de los suplentes x %.2f..%.2f z %.2f..%.2f", pre, mn[0], mx[0], mn[2], mx[2]); return; }
        // a) mallas de suplente fuera del _dir_bench
        unsigned benchMeshes = 0, tris = 0;
        for (auto& m : ms) if (m.bench && m.idx) {
            for (uint32_t t = 0; t + 2 < m.nidx; t += 3) if (m.idx[t + 1] != m.idx[t] || m.idx[t + 2] != m.idx[t]) { m.idx[t + 1] = m.idx[t + 2] = m.idx[t]; tris++; }
            benchMeshes++;
        }
        // b) figuras sentadas en la caja del banco
        const char* src = "de este estadio";
        if (!BoxGet(pre, mn, mx)) { if (ScanBenchNear(p, pre, mn, mx)) { BoxPut(pre, mn, mx); src = "buscada en el paquete"; } else src = nullptr; }
        unsigned figs = 0, kept = 0;
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
            struct Fig { float mn[3], mx[3]; bool person = false, init = false; };
            std::vector<Fig> fg(n);
            for (size_t i = 0; i < n; i++) {
                Fig& g = fg[fd((int)i)]; const Piece& pc = pcs[i];
                if (!g.init) { memcpy(g.mn, pc.mn, 12); memcpy(g.mx, pc.mx, 12); g.init = true; }
                for (int a = 0; a < 3; a++) { g.mn[a] = std::min(g.mn[a], pc.mn[a]); g.mx[a] = std::max(g.mx[a], pc.mx[a]); }
                if (!IsPropMat(f, ms[pc.mesh].mat)) g.person = true;
            }
            std::vector<std::vector<uint8_t>> hide(ms.size());
            for (size_t i = 0; i < n; i++) {
                if (fd((int)i) != (int)i) continue;
                const Fig& g = fg[i]; const float cx = (g.mn[0] + g.mx[0]) * 0.5f, cz = (g.mn[2] + g.mx[2]) * 0.5f;
                if (cx < mn[0] - kMargin || cx > mx[0] + kMargin || cz < mn[2] - kMargin || cz > mx[2] + kMargin) continue;
                const float h = g.mx[1] - g.mn[1], foot = std::max(g.mx[0] - g.mn[0], g.mx[2] - g.mn[2]);
                const bool sit = g.person && g.mn[1] <= mn[1] + kGround && h >= kMinH && h <= kMaxH && foot >= kMinFoot;
                if (!sit) { if (g.person) kept++; continue; }
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
        FmLog("banca: %s_%s: %u figuras sentadas y %u mallas de suplente escondidas (%u triangulos), %u personas paradas quedan; caja del banco %s",
                  pre, part, figs, benchMeshes, tris, kept, src ? src : "NO hallada (solo mallas de suplente)");
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
    Log("banca: escondido por la escena del estadio (cada cuadro) %s", s_okImpl ? "enganchado" : "NO");
    Log("banca: reloj %s, sonda de cinematicas %s, esconder el banco %s (fila 516: x %.0f)", s_okTick ? "enganchado" : "NO", s_okProbe ? "enganchada" : "NO",
        !s_okHide ? "NO (firma de 0x1419055f0 distinta)" : s_okVisHook ? (s_okApply ? "listo (SetVisible y al aplicar)" : "solo por SetVisible") : "solo en cinematicas (gancho de SetVisible fallo)", Live(0, 0.0f));
}
} // namespace

void BancaPresent() {
    if (!s_tried) Install();
    if (!s_okTick && !s_okProbe && !s_okVisHook && !s_okApply && !s_okImpl && !s_okFmdl) return;
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
