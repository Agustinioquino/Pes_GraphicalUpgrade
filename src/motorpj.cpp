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
//
// (17/11 06:36 / 06:37 / 06:38, Agustin: "el limite de jugadores en pantalla", "haz que todos puedan usar gestos distintos y animaciones",
// "opciones de gestos de cara, mejorar el sistema, el codigo, fluidez y opciones")
//  - hasta 32 jugadores (kMax). El byte del ctor (0x60 + lugar) solo es la semilla de dos sorteos del FixDemoHuman (fd+0x36c x 10000 /
//    x 25000, leido en FUN_141f85e40): 0x60..0x7f no choca con la banca (0x40 + orden).
//  - ANIMACION POR JUGADOR: cualquiera de los 3049 clips de cuerpo de FixDemo (motorclips.inc, del catalogo de cinematicas). Cada clip
//    se pide una vez al gestor (FUN_141eb16e0), se frena su descarga (banca.cpp BancaGaniKeep) y se comparte entre los que lo usan. El
//    cambio es el de la banca 2e (SubSwitch): vf1 de nuevo arma otro controlador con el lugar de fd+8. Reloj propio por jugador
//    (velocidad, pausa, desde el principio); si el clip no da la vuelta solo se lo vuelve al principio a mano (como la banca).
//  - GESTO DE LA CARA POR JUGADOR (leido en el C): fd+0x58 = controlador de cara y manos {+0 animador de la cara, +8 / +0x10 manos}.
//    El animador de la cara tiene 8 capas: 0..3 base (clip = numero de la lista fija de 166 del exe, FUN_141b03f60), 4..7 sumadas
//    (clip 0xa6 + n con mascara FUN_141ee0870, FUN_141b04020); tiempo FUN_141b04e20 (segundos, da la vuelta solo), peso FUN_141b04fb0.
//    Cada cuadro: capa 0 = el gesto elegido con su peso, capa 1 = base_normal con el resto, capa 4 = parpadeo (add_eyeblink_single,
//    sumado 2) cada ~3,7 s; despues FUN_141ec21a0 (evalua los tres animadores, como FixDemoHuman::vf4) y FUN_141ec4970(ctl, h+0x148, 3, 0):
//    modo 3 = la cara sale del animador y las manos de la tabla neutra del juego (modo 1, el de FUN_141ec7b10, pone las dos neutras).
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
const uintptr_t kDbPtr = 0x143714240ull;
// (17/11 prueba de Agustin 05:13: aparecen pero no se mueven) leido en el C: vf1 / vf2 copian fd+8..+0x14 a la raiz del controlador
// UNA vez (FUN_141afd060(ctl, x, y, z, 0) + FUN_141afccc0(ctl, giro * pi / 180, 0)) y la base para cuando el clip da la vuelta a
// fd+0x18..+0x24 (FUN_141f86d40 la vuelve a poner con fd+0x416). Escribir fd+8 despues no mueve nada: al cambiar el pedido se llaman
// esas dos funciones (como el juego) y se copia tambien la base de la vuelta.
const uintptr_t kRootPos = 0x141afd060ull, kRootYaw = 0x141afccc0ull;
const char* const kClipDir = "cpk_dat/common/demo/anime/FoxAnim/FixDemo/Animations/";
const char* const kIdleFile = "dml_ent_aisle_cmn_idle01.gani";   // parado esperando (pasillo): el de siempre
const uintptr_t kClipLen = 0x141af8be0ull, kClipTime = 0x141af8910ull, kClipSeek = 0x141afa7b0ull;
const uintptr_t kAnimGetClip = 0x141b04930ull, kAnimSetBase = 0x141b03f60ull, kAnimSetClip = 0x141b04020ull, kAnimTime = 0x141b04e20ull, kAnimWeight = 0x141b04fb0ull;
const uintptr_t kAddMask = 0x141ee0870ull, kFhEval = 0x141ec21a0ull, kFhSend = 0x141ec4970ull;
#include "motorclips.inc"
const int kClipN = (int)(sizeof kClipRows / sizeof kClipRows[0]), kGroupN = (int)(sizeof kClipGroupKey / sizeof kClipGroupKey[0]), kFaceN = (int)(sizeof kFaceBase / sizeof kFaceBase[0]);
int ClipIndexOf(const char* file) { for (int i = 0; i < kClipN; i++) if (!strcmp(kClipRows[i].file, file)) return i; return -1; }
int FaceIndexOf(const char* n) { for (int i = 0; i < kFaceN; i++) if (!strcmp(kFaceBase[i], n)) return i; return -1; }
const int kIdleClip = ClipIndexOf(kIdleFile), kBaseNormal = FaceIndexOf("base_normal");
void ClipPathOf(int clip, char* out, size_t n) { snprintf(out, n, "%s%s", kClipDir, clip >= 0 && clip < kClipN ? kClipRows[clip].file : kIdleFile); }

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
typedef void(__fastcall* RootPosFn)(void*, float, float, float, char);
typedef void(__fastcall* RootYawFn)(void*, float, char);
typedef float(__fastcall* ClipTimeFn)(void*, uint32_t, uint8_t);
typedef void(__fastcall* ClipSeekFn)(void*, uint32_t, uint32_t, float, uint8_t, uint8_t);
typedef uint32_t(__fastcall* AnimGetClipFn)(void*, uint32_t);
typedef void(__fastcall* AnimSetBaseFn)(void*, uint32_t, uint32_t, uint8_t, uint64_t, uint64_t, uint64_t);
typedef void(__fastcall* AnimSetClipFn)(void*, uint32_t, uint32_t, uint8_t, uint64_t, uint64_t, uint64_t);
typedef void(__fastcall* AnimTimeFn)(void*, uint32_t, float);
typedef void(__fastcall* AnimWeightFn)(void*, uint32_t, float);
typedef void*(__fastcall* AddMaskFn)(uint64_t*, int);
typedef void(__fastcall* FhEvalFn)(void*, float);
typedef void(__fastcall* FhSendFn)(void*, void*, int, uint8_t);
typedef char(__fastcall* HumanOkFn)(void*);
struct GStr { union { char buf[16]; char* ptr; }; size_t size, cap; };

const int kMax = kMotorPjMax;
constexpr int kPoseMax = 32;
struct Pj {
    // lo escribe la interfaz (con s_mx)
    int state = 0;                  // 0 libre, 1 pedido, 2 cargando, 3 listo, 4 fallo, 5 sacar
    int kind = 0; uint64_t id = 0; char name[48] = {};
    float pos[3] = {}, yaw = 0.0f, scale = 1.0f;
    int clip = -1;                  // clip de cuerpo pedido (motorclips.inc); -1 = parado
    float speed = 1.0f; bool paused = false; int restart = 0;   // restart: lo sube la interfaz
    float seek = -1.0f; int seekN = 0;                          // (06:52) linea de tiempo: ir a ese segundo del clip (seekN lo sube la interfaz)
    float wpos[3] = {}; bool hasW = false;                     // (17/11 tarde) lugar real de la raiz (lo escribe el hilo del juego, con s_mx)
    float curT = 0.0f, lenT = 0.0f;                             // lo escribe el hilo del juego (con s_mx): tiempo del clip y su largo
    int face = -1; float faceW = 1.0f; bool blink = true;       // gesto (lista de 166); -1 = cara neutra del juego
    bool hidden = false;
    int mode = 0;                   // (17/11 18:01) 0 en bucle, 1 una vez (queda en el final), 2 ida y vuelta
    bool back = false;              // ida y vuelta: yendo para atras (solo el hilo del juego)
    struct PoseK { int bone; float e[3]; }; PoseK pose[kPoseMax] = {}; int nPose = 0;   // (17/11 18:23) posar: giro por hueso sobre la animacion (grados)
    float boneW[3] = {}; bool boneWOk = false; int boneQ = -1;                          // lugar en el mundo del hueso que mira la interfaz (lo escribe el juego)
    // solo el hilo del juego
    uint8_t* fd = nullptr; double tLoad = 0, tLast = 0, tPlay = 0; bool faceTried = false; bool ownSaid = false;
    float syncT = -1.0f; bool pairB = false;   // (17/11 20:45) pareja de clips propios: B va al segundo de A (syncT) y en el lugar que le da A
    float scW = 1.0f; void* scCtl = nullptr; float scBase = 0.0f;   // (17/11 tarde) tamano por root_main como FUN_141f87eb0
    float lastPos[3] = {}, lastYaw = 0.0f; void* posCtl = nullptr; int nMoved = 0;
    int clipA = -2, clipFail = -2, restartA = 0, seekA = 0, endTicks = 0; uint32_t seed = 0;   // clipA: el clip puesto
    uint8_t lid[0xa0] = {};
    bool reload = false; double reloadAt = 0;   // (17/11 17:22) el primero de cada apertura del Motor se vuelve a crear una vez (ropa)
    // (17/11 22:55, Agustin: "creo que las alturas de los jugadores no se respetan"; registro: los dos "estatura del modelo 180.0 cm")
    // estatura (cm) y peso (kg) de la base: byte +0 y +1 del registro (campos 0x2e / 0x2f del getter FUN_1414cc040; FUN_141590500 los
    // copia junto con el id de +0x2c y c_140e96750 los pasa a float como estatura). 0 = no se sabe (se usa 180 / 75 como antes).
    uint8_t hcm = 0, wkg = 0;
    // (17/11 22:55, "no coinciden correctamente") contactos de la pareja de clips propios (ContactIK): solo el hilo del juego
    int partner = -1; float lift = 0.0f, shr[2] = { 0.0f, 0.0f }; double cLog = 0.0; int cN = 0;
    struct Snap { bool ok = false; float t = 0; float hand[2][3] = {}; float chest[3] = {}; float X[3] = {}; float sc = 1.0f; } sn[2];
    // (17/11 23:08, Agustin: "darme opciones para editar las animaciones en tiempo real, mover huesos, expresiones, con ayudas de ia")
    // EDITOR: lugares de los huesos en el mundo para la vista (los escribe el juego, con s_mx) y mano / pie agarrado (lo escribe la interfaz)
    float bw[32][3] = {}; int bwN = 0; int bpar[32] = {}; int blab[32] = {}; float bax[3][3] = {}; float bsc = 1.0f;
    int grab = -1; float grabT[3] = {};       // 0 mano izq, 1 mano der, 2 pie izq, 3 pie der; grabT = punto en el mundo
};
// (17/11 17:21-17:22, prueba de Agustin con B35F155B y captura) el PRIMER jugador que se crea al abrir el Motor sale sin remera, con el short a
// medias y sin medias; los que se agregan despues (aun con el mismo id) salen vestidos, y sacarlo y volver a agregarlo lo viste (reusa el
// mismo modelo: "listo en 0.0 s"). INFERIDO: el primero se arma antes de que el juego tenga cargada la ropa. Arreglo: el primer jugador de
// cada apertura, 1.5 s despues de estar listo, se saca y se vuelve a crear solo, en el mismo lugar y con lo mismo.
static bool s_firstMade = false;
Pj s_pj[kMax];
std::mutex s_mx;
std::atomic<int> s_sel{-1};
std::atomic<bool> s_dead{false};
std::atomic<double> s_lastTick{0.0};
char s_msg[200] = {};               // con s_mx
// ---- (17/11 23:08, Agustin: "enfoquemonos en darme opciones para editar las animaciones gani en tiempo real, mover huesos, expresiones,
// con ayudas de ia") EDITOR: claves por jugador encima del clip. Cada clave tiene un segundo y, por hueso, un giro (grados en los ejes del
// modelo, como POSAR: primero Z, despues X, despues Y). Entre claves cada hueso va por su curva: Fluida (pasa por las claves con
// velocidad, Catmull-Rom), Con peso (frena y arranca en cada clave) o Lineal. Lo que no tiene claves queda en la pose fija de POSAR.
// "Auto clave": tocar un hueso guarda (o cambia) la clave del segundo actual.
constexpr int kEdB = 32, kEdK = 96;
struct EdKey { float t; uint32_t mask; float e[kEdB][3]; };
struct EdLayer { int n = 0; EdKey k[kEdK]; };
static EdLayer s_ed[kMax];                    // con s_mx
static int s_edInterp = 0; static bool s_edAuto = true;     // con s_mx
static EdKey s_edClip; static bool s_edClipOk = false;      // copiar / pegar clave
static int EdKeyAt(const EdLayer& L, float t) { for (int i = 0; i < L.n; i++) if (fabsf(L.k[i].t - t) < 0.5f / 30.0f) return i; return -1; }
static int EdKeyNew(EdLayer& L, float t) {
    if (L.n >= kEdK) return -1;
    int at = L.n; while (at > 0 && L.k[at - 1].t > t) { L.k[at] = L.k[at - 1]; at--; }
    memset(&L.k[at], 0, sizeof L.k[at]); L.k[at].t = t; L.n++; return at; }
static bool EdEvalBone(const EdLayer& L, int b, float t, int mode, float* e) {
    int id[kEdK], m = 0; const uint32_t bit = 1u << b;
    for (int i = 0; i < L.n; i++) if (L.k[i].mask & bit) id[m++] = i;
    if (!m) return false;
    if (m == 1 || t <= L.k[id[0]].t) { memcpy(e, L.k[id[0]].e[b], 12); return true; }
    if (t >= L.k[id[m - 1]].t) { memcpy(e, L.k[id[m - 1]].e[b], 12); return true; }
    int j = 0; while (j + 1 < m && L.k[id[j + 1]].t <= t) j++;
    const EdKey& A = L.k[id[j]]; const EdKey& B = L.k[id[j + 1]]; const float dt = std::max(1e-4f, B.t - A.t), u = std::clamp((t - A.t) / dt, 0.0f, 1.0f);
    if (mode == 2) { for (int c = 0; c < 3; c++) e[c] = A.e[b][c] + (B.e[b][c] - A.e[b][c]) * u; return true; }
    if (mode == 1) { const float s = u * u * (3.0f - 2.0f * u); for (int c = 0; c < 3; c++) e[c] = A.e[b][c] + (B.e[b][c] - A.e[b][c]) * s; return true; }
    // Fluida: Hermite con la velocidad de los vecinos (Catmull-Rom con tiempos distintos); en las puntas, velocidad 0
    const EdKey* P0 = j > 0 ? &L.k[id[j - 1]] : nullptr; const EdKey* P3 = j + 2 < m ? &L.k[id[j + 2]] : nullptr;
    const float u2 = u * u, u3 = u2 * u, h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    for (int c = 0; c < 3; c++) {
        const float m0 = P0 ? (B.e[b][c] - P0->e[b][c]) / std::max(1e-4f, B.t - P0->t) * dt : 0.0f;
        const float m1 = P3 ? (P3->e[b][c] - A.e[b][c]) / std::max(1e-4f, P3->t - A.t) * dt : 0.0f;
        e[c] = h00 * A.e[b][c] + h10 * m0 + h01 * B.e[b][c] + h11 * m1; }
    return true;
}
// giro del hueso en el editor (grados) <-> cuaternion, mismo orden que PoseApply: q = Ry * Rx * Rz
static void EulQ(const float* e, float* q) {
    const float hx = e[0] * 0.00872665f, hy = e[1] * 0.00872665f, hz = e[2] * 0.00872665f;
    const float qx[4] = { sinf(hx), 0, 0, cosf(hx) }, qy[4] = { 0, sinf(hy), 0, cosf(hy) }, qz[4] = { 0, 0, sinf(hz), cosf(hz) };
    const float x = qy[3] * qx[0], y = qy[1] * qx[3], z = -qy[1] * qx[0], w = qy[3] * qx[3];      // qy * qx
    const float t[4] = { x, y, z, w };
    q[0] = t[3] * qz[0] + t[0] * qz[3] + t[1] * qz[2] - t[2] * qz[1]; q[1] = t[3] * qz[1] - t[0] * qz[2] + t[1] * qz[3] + t[2] * qz[0];
    q[2] = t[3] * qz[2] + t[0] * qz[1] - t[1] * qz[0] + t[2] * qz[3]; q[3] = t[3] * qz[3] - t[0] * qz[0] - t[1] * qz[1] - t[2] * qz[2]; }
static void QEul(const float* q, float* e) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float m11 = 1 - 2 * (y * y + z * z), m13 = 2 * (x * z + y * w), m21 = 2 * (x * y + z * w), m22 = 1 - 2 * (x * x + z * z), m23 = 2 * (y * z - x * w), m31 = 2 * (x * z - y * w), m33 = 1 - 2 * (x * x + y * y);
    const float sx = std::clamp(-m23, -1.0f, 1.0f); e[0] = asinf(sx) * 57.29578f;
    if (fabsf(m23) < 0.99999f) { e[1] = atan2f(m13, m33) * 57.29578f; e[2] = atan2f(m21, m22) * 57.29578f; } else { e[1] = atan2f(-m31, m11) * 57.29578f; e[2] = 0.0f; } }
// guarda el giro de un hueso: clave del segundo t (Auto clave o si ya hay claves) o pose fija (con s_mx tomado)
static void EdPutLocked(int i, float t, int bone, const float* e) {
    Pj& p = s_pj[i]; EdLayer& L = s_ed[i];
    if (bone < 0 || bone >= kEdB) return;
    if (s_edAuto || L.n > 0) { int k = EdKeyAt(L, t); if (k < 0) k = EdKeyNew(L, t); if (k < 0) return;
        if (e) { memcpy(L.k[k].e[bone], e, 12); L.k[k].mask |= 1u << bone; } else L.k[k].mask &= ~(1u << bone); return; }
    for (int k = 0; k < p.nPose; k++) if (p.pose[k].bone == bone) { if (e) memcpy(p.pose[k].e, e, 12); else { for (int q = k; q + 1 < p.nPose; q++) p.pose[q] = p.pose[q + 1]; p.nPose--; } return; }
    if (!e || p.nPose >= kPoseMax) return;
    int at = p.nPose; while (at > 0 && p.pose[at - 1].bone > bone) { p.pose[at] = p.pose[at - 1]; at--; }
    p.pose[at].bone = bone; memcpy(p.pose[at].e, e, 12); p.nPose++; }
// la pose del segundo t: claves + pose fija (con s_mx tomado)
static void EdEvalLocked(int i, float t, Pj::PoseK* out, int* n) {
    const Pj& p = s_pj[i]; const EdLayer& L = s_ed[i]; int m = 0;
    for (int b = 0; b < kEdB && m < kPoseMax; b++) {
        float e[3] = { 0, 0, 0 }; bool any = EdEvalBone(L, b, t, s_edInterp, e);
        for (int k = 0; k < p.nPose; k++) if (p.pose[k].bone == b) { for (int c = 0; c < 3; c++) e[c] += p.pose[k].e[c]; any = true; }
        if (any) { out[m].bone = b; memcpy(out[m].e, e, 12); m++; } }
    *n = m; }
bool s_pumpSaid = false;            // hilo del juego
// clips de cuerpo pedidos al gestor del juego (compartidos entre jugadores); solo el hilo del juego
const int kClipMax = 40;
struct ClipE { int clip = -1; int st = 0; double t = 0, lastUse = 0; uint64_t slot[2] = {}, hash = 0; bool kept = false; };   // st 0 nada, 1 pedido, 2 listo, 3 no se puede
ClipE s_ce[kClipMax];

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
void DbBody(uintptr_t r, uint8_t* hcm, uint8_t* wkg) {
    __try { const uint8_t h = *(const uint8_t*)r, w = *(const uint8_t*)(r + 1);
        *hcm = (h >= 140 && h <= 220) ? h : 0; *wkg = (w >= 40 && w <= 140) ? w : 0; } __except (1) { *hcm = 0; *wkg = 0; }
}
void DbName(uintptr_t r, char* out, size_t n) {
    out[0] = 0;
    __try { if (*(const int*)(r + 0x144) >= 0) { const char* s = (const char*)(r + 0x75); size_t i = 0; for (; i + 1 < n && s[i]; i++) out[i] = ((unsigned char)s[i] < 0x20) ? '?' : s[i]; out[i] = 0; } } __except (1) { out[0] = 0; }
}

// --- clips de cuerpo ---
int ClipQuery(ClipE& e, const char* path) {
    __try {
        void* mgr = ((PtrFn)kResMgr)(); if (!mgr) return 0;
        uint64_t out[2] = { 0, 0 }; ((ResFindFn)kResFind)(mgr, out, path);
        if ((uint8_t)out[1] != 0 && out[0] != 0xb8a0bf169f98ull) { e.hash = out[0]; return 1; }
        return 0;
    } __except (1) { return -1; }
}
bool ClipAsk(ClipE& e, const char* path) {
    __try { GStr s; memset(&s, 0, sizeof s); s.cap = 15; ((StrAssignFn)kStrAssign)(&s, path, strlen(path)); e.slot[0] = e.slot[1] = 0; ((GaniQueueFn)kGaniQueue)(e.slot, &s); return true; }
    __except (1) { return false; }
}
bool Pump() { __try { ((GaniPumpFn)kGaniPump)(0, 0, 0, 0); return true; } __except (1) { return false; } }
void* FixdemoRes() {
    __try { GStr s; memset(&s, 0, sizeof s); memcpy(s.buf, "fixdemo", 8); s.size = 7; s.cap = 15; void* mgr = ((PtrFn)kAnimResMgr)(); return mgr ? ((AnimResFindFn)kAnimResFind)(mgr, &s) : nullptr; }
    __except (1) { return nullptr; }
}
bool ClipInUse(int clip) { for (int i = 0; i < kMax; i++) if (s_pj[i].fd && (s_pj[i].clipA == clip)) return true; return false; }
void ClipDrop(ClipE& e) { if (e.kept) BancaGaniKeep(e.hash, false); e = ClipE{}; }
// la entrada del clip (la crea si hace falta; si no hay lugar suelta la que hace mas que nadie usa)
ClipE* ClipEntry(int clip, double now) {
    int fr = -1, old = -1;
    for (int i = 0; i < kClipMax; i++) { if (s_ce[i].st && s_ce[i].clip == clip) { s_ce[i].lastUse = now; return &s_ce[i]; } if (!s_ce[i].st && fr < 0) fr = i; }
    if (fr < 0) for (int i = 0; i < kClipMax; i++) if (!ClipInUse(s_ce[i].clip) && (old < 0 || s_ce[i].lastUse < s_ce[old].lastUse)) old = i;
    if (fr < 0 && old >= 0) { ClipDrop(s_ce[old]); fr = old; }
    if (fr < 0) return nullptr;
    s_ce[fr] = ClipE{}; s_ce[fr].clip = clip; s_ce[fr].lastUse = now; return &s_ce[fr];
}
// 1 listo, 0 todavia, -1 no se puede
int ClipReady(int clip, double now) {
    ClipE* e = ClipEntry(clip, now); if (!e) { Msg("demasiadas animaciones distintas a la vez (%d)", kClipMax); return -1; }
    if (e->st == 2) return 1;
    if (e->st == 3) return -1;
    char path[192]; ClipPathOf(clip, path, sizeof path);
    const int q = ClipQuery(*e, path);
    if (q < 0) { e->st = 3; Msg("error al buscar el clip %s en el gestor del juego", path + strlen(kClipDir)); return -1; }
    if (q > 0) {
        e->st = 2; e->kept = BancaGaniKeep(e->hash, true);
        Log("motor (jugadores propios): clip listo (%s)%s", path + strlen(kClipDir), e->kept ? "; el juego no lo descarga mientras haya jugadores" : "; OJO: sin freno de descarga (gancho de la banca apagado o sin lugar)");
        return 1;
    }
    if (e->st == 0) { if (!ClipAsk(*e, path)) { e->st = 3; Msg("error al pedir el clip %s", path + strlen(kClipDir)); return -1; } e->st = 1; e->t = now; return 0; }
    if (now - e->t > 3.0) { if (!s_pumpSaid) { s_pumpSaid = true; Log("motor (jugadores propios): el clip no carga solo: se llama a la bomba de carga del juego"); } if (!Pump()) { e->st = 3; Msg("error en la bomba de carga de clips"); return -1; } }
    if (now - e->t > 20.0) { e->st = 3; Msg("el clip %s no cargo en 20 s", path + strlen(kClipDir)); return -1; }
    return 0;
}

// --- un jugador ---
// (17/11 06:47, Agustin: "cuando agregue el segundo messi no se ve") leido en el C: FUN_141f86c30 -> FUN_141e9ce00 busca el LoadId en la
// lista de modelos ya pedidos del gestor de jugadores (FUN_141e9b7b0, lista en +0x30) con FUN_141e9cf10 / FUN_1408be6e0, que compara
// solo: u16 +0, u32 +4 (el id), bytes +0x4c..+0x50, u32 +0x54, el texto de +0x58 (el nombre de la ficha, FUN_141f61520 lo copia de
// ficha+0x9c), +0x96 y +0x97. Dos jugadores con el mismo LoadId comparten ese modelo y se ve uno solo (el registro de 03:21: los tres
// Messi "listos en 0.0 s"). Al repetido se le agregan espacios al final del nombre: otro LoadId para el gestor, el mismo jugador.
bool LidSame(const uint8_t* a, const uint8_t* b) {
    if (memcmp(a, b, 2) || memcmp(a + 4, b + 4, 4) || memcmp(a + 0x4c, b + 0x4c, 5) || memcmp(a + 0x54, b + 0x54, 4) || a[0x96] != b[0x96] || a[0x97] != b[0x97]) return false;
    return strncmp((const char*)a + 0x58, (const char*)b + 0x58, 0x3e) == 0;
}
void LidUnique(Pj& p, int slot) {
    for (int tries = 0; tries < 40; tries++) {
        bool clash = false;
        for (int j = 0; j < kMax && !clash; j++) if (j != slot && s_pj[j].fd && LidSame(s_pj[j].lid, p.lid)) clash = true;
        if (!clash) { if (tries) Log("motor (jugadores propios): jugador %d repetido: %d espacios al final del nombre para que el juego le cargue su propio modelo", slot + 1, tries); return; }
        char* nm = (char*)p.lid + 0x58; const size_t l = strnlen(nm, 0x3d); if (l >= 0x3c) return;
        nm[l] = ' '; nm[l + 1] = 0;
    }
}
bool Create(Pj& p, int slot, const uint8_t* visorLid) {
    __try {
        uint32_t saved = 0, id = 0x19, heap = 0x19;
        ((HeapPushFn)kHeapPush)(&saved, &id);
        void* mem = ((AllocFn)kAlloc)(&heap, 0x578, 0x10);
        uint8_t* fd = mem ? (uint8_t*)((FdCtorFn)kFdCtor)(mem, (uint8_t)(0x60 + slot)) : nullptr;
        if (fd) {
            if (p.kind == 2 && visorLid) memcpy(p.lid, visorLid, sizeof p.lid);
            else {
                uint8_t pl[0xF0] = {}; memcpy(pl, &p.id, 8); pl[8] = 1; pl[0x39] = p.hcm ? p.hcm : 180; pl[0x3a] = p.wkg ? p.wkg : 75;   // (22:55) los de la base
                strncpy_s((char*)pl + 0x9c, 61, p.name, _TRUNCATE);
                ((LoadIdFn)kLoadId)(nullptr, p.lid, 0, 0, pl, 0, 0, 0, 0, 0);
            }
            LidUnique(p, slot);
            if (p.reload) {      // (17/11 17:50, Agustin: "salio sin estar vestido messi") el recreado reusaba el MISMO modelo (registro: mismo puntero,
                                 // "listo en 0.0 s"): el juego guarda el modelo por LoadId. El primero (el que sale sin ropa) se pide con tres espacios
                                 // de mas en el nombre; asi el recreado, con el nombre de verdad, es un modelo nuevo y nunca se reusa el roto.
                char* nm = (char*)p.lid + 0x58; const size_t l = strnlen(nm, 0x3d); if (l + 3 < 0x3d) { memcpy(nm + l, "   ", 3); nm[l + 3] = 0; } }
            fd[0x490] = 0; fd[0x514] = 0;                   // sin efectos de lluvia / aliento
            ((FdLoadFn)kFdLoad)(fd, p.lid, 0u, 0, 0);
        }
        ((HeapPopFn)kHeapPop)(&saved);
        p.fd = fd; p.scBase = 0.0f; p.scCtl = nullptr; return fd != nullptr;
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
    ((RootPosFn)kRootPos)(ctl, pos[0], pos[1], pos[2], 1);   // (prueba 03:19: sin remera, short ni medias al mover) 1 = tambien la ropa (objeto [ctl+0x138]+0x30: +0x100 lugar, +0x120 = 1 salto)
    ((RootYawFn)kRootYaw)(ctl, yaw * 0.017453292f, 0);
    const bool first = ctl != p.posCtl;
    p.posCtl = ctl; memcpy(p.lastPos, pos, 12); p.lastYaw = yaw;
    if (!first && p.nMoved++ < 4) Log("motor (jugadores propios): jugador movido a (%.2f, %.2f, %.2f) giro %.1f", pos[0], pos[1], pos[2], yaw);
}
bool Start(Pj& p, void* res, int clip, const float* pos, float yaw) {
    __try {
        Place(p, pos, yaw);
        p.fd[0x416] = 1;                                     // al dar la vuelta el clip vuelve a la base
        char path[192]; ClipPathOf(clip, path, sizeof path);
        uint32_t saved = 0, id = 0x19; ((HeapPushFn)kHeapPush)(&saved, &id);
        const uint32_t idx = ((ClipIdxFn)kClipIdx)(res, path);
        BancaFdhPlay(p.fd, res, idx, 0, 0);
        ((HeapPopFn)kHeapPop)(&saved);
        p.fd[0x43e] = 3;                                     // ropa: reinicio de la tela como la cinematica
        p.faceTried = false; FaceEnsure(p);
        void* h = nullptr; memcpy(&h, p.fd + 0x48, 8); if (h) ((HumanShowFn)kHumanShow)(h, 1);
        void* ctl = nullptr; memcpy(&ctl, p.fd + 0x50, 8);
        p.posCtl = ctl; memcpy(p.lastPos, pos, 12); p.lastYaw = yaw;   // vf1 ya puso este lugar en la raiz
        p.tPlay = 0.0; p.tLast = Now(); p.scW = 1.0f; p.scCtl = nullptr; p.clipA = clip; p.endTicks = 0;
        return ctl != nullptr;
    } __except (1) { s_dead.store(true); return false; }
}
// (06:37) otro clip al jugador ya cargado (como la banca 2e SubSwitch): vf1 -> vf2 borra los controladores del cuerpo y de la cara y arma
// los nuevos con el lugar y el giro de fd+8..0x14. La raiz se vuelve a poner en el proximo RootMove (controlador nuevo).
bool Switch(Pj& p, void* res, int clip) {
    __try {
        char path[192]; ClipPathOf(clip, path, sizeof path);
        p.fd[0x416] = 1;
        uint32_t saved = 0, id = 0x19; ((HeapPushFn)kHeapPush)(&saved, &id);
        const uint32_t idx = ((ClipIdxFn)kClipIdx)(res, path);
        BancaFdhPlay(p.fd, res, idx, 0, 0);
        ((HeapPopFn)kHeapPop)(&saved);
        p.faceTried = false; FaceEnsure(p);
        void* ctl = nullptr; memcpy(&ctl, p.fd + 0x50, 8);
        p.posCtl = nullptr; p.tPlay = 0.0; p.scW = 1.0f; p.scCtl = nullptr; p.clipA = clip; p.endTicks = 0;
        return ctl != nullptr;
    } __except (1) { s_dead.store(true); return false; }
}
// gesto de la cara (ver arriba): capa 0 el gesto, capa 1 base_normal con el resto del peso, capa 4 parpadeo; manos neutras
void FaceGesture(Pj& p, void* h, void* face, int g, float w, bool blink) {
    void* an = nullptr; memcpy(&an, face, 8); if (!an || !h) return;
    w = std::clamp(w, 0.0f, 1.0f);
    const float t = (float)p.tPlay;
    if (((AnimGetClipFn)kAnimGetClip)(an, 0) != (uint32_t)g) ((AnimSetBaseFn)kAnimSetBase)(an, 0, (uint32_t)g, 0, 0, 0, 0);
    ((AnimTimeFn)kAnimTime)(an, 0, t); ((AnimWeightFn)kAnimWeight)(an, 0, w);
    if (kBaseNormal >= 0 && w < 0.999f) {
        if (((AnimGetClipFn)kAnimGetClip)(an, 1) != (uint32_t)kBaseNormal) ((AnimSetBaseFn)kAnimSetBase)(an, 1, (uint32_t)kBaseNormal, 0, 0, 0, 0);
        ((AnimTimeFn)kAnimTime)(an, 1, t); ((AnimWeightFn)kAnimWeight)(an, 1, 1.0f - w);
    } else ((AnimWeightFn)kAnimWeight)(an, 1, 0.0f);
    ((AnimWeightFn)kAnimWeight)(an, 2, 0.0f); ((AnimWeightFn)kAnimWeight)(an, 3, 0.0f);
    float bw = 0.0f;
    if (blink) {
        if (((AnimGetClipFn)kAnimGetClip)(an, 4) != 0xa8u) { uint64_t m = 0xb8a0bf169f98ull; ((AddMaskFn)kAddMask)(&m, 2); ((AnimSetClipFn)kAnimSetClip)(an, 4, 0xa8u, 0, 0, 0, m); }
        const float per = 3.2f + (float)(p.seed % 7) * 0.15f, ph = fmodf(t + (float)(p.seed % 97) * 0.031f, per);
        if (ph < 0.26f) { bw = 1.0f; ((AnimTimeFn)kAnimTime)(an, 4, ph); }
    }
    ((AnimWeightFn)kAnimWeight)(an, 4, bw);
    for (uint32_t l = 5; l < 8; l++) ((AnimWeightFn)kAnimWeight)(an, l, 0.0f);
    ((FhEvalFn)kFhEval)(face, 0.0f);
    void* hp = nullptr; memcpy(&hp, (const uint8_t*)h + 0x148, 8); if (!hp) return;
    void** vt = *(void***)h; if (!vt || !((HumanOkFn)vt[2])(h)) return;
    ((FhSendFn)kFhSend)(face, hp, 3, 0);
}
// (17/11 19:31-19:45, Agustin: dedos, sonrisa y parpadeo en el saludo) clips propios (grupo "propias", hechos con tools/gani_cuerpo.py):
// la cara y las manos van por cuadro desde src/motorpista.inc mientras corre el clip y el Inspector no tiene un gesto elegido.
// Manos: el juego arma su juego de poses de mano (DAT_143803000, FUN_141ec1330) agregando en orden los 162 nombres de la lista del exe
// 0x143535a50..0x143535f60 (la .data del exe en disco esta cifrada: la lista se lee en memoria). Solo si el juego tiene las 162 (cuenta
// en +0x50 del juego, como FUN_141b05a60) el numero de pose = lugar en la lista; si no, manos neutras como siempre (modo 3).
// Animadores (FUN_141ec21a0 evalua los tres): cara [ctl+0], mano izquierda [ctl+8], mano derecha [ctl+0x10] (INFERIDO: la secuencia del
// juego guarda "handL" y despues "handR", y el cuadro de 300 B de FUN_141ec04b0 / FUN_141ec0960 los pasa a +8 y +0x10 en ese orden).
// FUN_141ec4970 modo 0 = cara y manos desde los animadores.
#include "motorpista.inc"
const uintptr_t kHandList = 0x143535a50ull; const int kHandN = 162;
static const char* s_handName[kHandN] = {}; static int s_handSt = 0;   // 0 sin leer, 1 lista usable, 2 no (solo el hilo del juego)
static bool HandNameEq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *b == 0 && (*a == 0 || *a == '.'); }
int HandIndexOf(const char* n) {
    __try {
        if (s_handSt == 0) {
            s_handSt = 2;
            const uintptr_t set = *(const uintptr_t*)kHandResPtr;
            const uint32_t cnt = PtrOk(set) ? *(const uint32_t*)(set + 0x50) : 0;
            int ok = 0;
            for (int i = 0; i < kHandN; i++) { const uintptr_t a = *(const uintptr_t*)(kHandList + 8ull * i); s_handName[i] = PtrOk(a) ? (const char*)a : nullptr; ok += s_handName[i] ? 1 : 0; }
            // (20:42, prueba de Agustin: 254 poses) FUN_141ec1330 agrega la lista del exe en orden al armar el juego de manos, y despues el
            // juego suma otras al final: con 162 o mas, las primeras 162 son las de la lista (INFERIDO; el volcado de abajo lo confirma)
            if (cnt >= (uint32_t)kHandN && ok == kHandN) s_handSt = 1;
            if (PtrOk(set)) { const uintptr_t arr = *(const uintptr_t*)(set + 0x58); if (PtrOk(arr)) { const uint64_t* e = (const uint64_t*)arr;
                Log("motor (manos): entradas 0 %016llx %016llx, 1 %016llx %016llx, 161 %016llx %016llx, 162 %016llx %016llx", e[0], e[1], e[2], e[3], e[322], e[323], e[324], e[325]); } }
            Log("motor (manos): el juego tiene %u poses de mano; lista del exe %s (0 = %s, 161 = %s)", cnt, s_handSt == 1 ? "usada" : "NO usada (manos neutras)",
                s_handName[0] ? s_handName[0] : "?", s_handName[kHandN - 1] ? s_handName[kHandN - 1] : "?");
        }
        if (s_handSt != 1 || !n) return -1;
        for (int i = 0; i < kHandN; i++) if (HandNameEq(s_handName[i], n)) return i;
    } __except (1) { s_handSt = 2; }
    return -1;
}
const OwnTrack* OwnTrackOf(int clip) {
    if (clip < 0 || clip >= kClipN) return nullptr;
    for (const OwnTrack& t : kOwnTracks) if (!strcmp(t.file, kClipRows[clip].file)) return &t;
    return nullptr;
}
// capa 0 la pose de ahora (peso a), capa 1 la anterior (1 - a); 2 y 3 apagadas. Poses de un cuadro: tiempo 0.
static void HandLayers(void* an, int c, int pv, float a) {
    if (!an || c < 0) return;
    if (((AnimGetClipFn)kAnimGetClip)(an, 0) != (uint32_t)c) ((AnimSetBaseFn)kAnimSetBase)(an, 0, (uint32_t)c, 0, 0, 0, 0);
    ((AnimTimeFn)kAnimTime)(an, 0, 0.0f); ((AnimWeightFn)kAnimWeight)(an, 0, pv >= 0 ? a : 1.0f);
    if (pv >= 0 && a < 0.999f) {
        if (((AnimGetClipFn)kAnimGetClip)(an, 1) != (uint32_t)pv) ((AnimSetBaseFn)kAnimSetBase)(an, 1, (uint32_t)pv, 0, 0, 0, 0);
        ((AnimTimeFn)kAnimTime)(an, 1, 0.0f); ((AnimWeightFn)kAnimWeight)(an, 1, 1.0f - a);
    } else ((AnimWeightFn)kAnimWeight)(an, 1, 0.0f);
    ((AnimWeightFn)kAnimWeight)(an, 2, 0.0f); ((AnimWeightFn)kAnimWeight)(an, 3, 0.0f);
}
static void Blink(Pj& p, void* an, float t, bool blink) {
    float bw = 0.0f;
    if (blink) {
        if (((AnimGetClipFn)kAnimGetClip)(an, 4) != 0xa8u) { uint64_t m = 0xb8a0bf169f98ull; ((AddMaskFn)kAddMask)(&m, 2); ((AnimSetClipFn)kAnimSetClip)(an, 4, 0xa8u, 0, 0, 0, m); }
        const float per = 3.2f + (float)(p.seed % 7) * 0.15f, ph = fmodf(t + (float)(p.seed % 97) * 0.031f, per);
        if (ph < 0.26f) { bw = 1.0f; ((AnimTimeFn)kAnimTime)(an, 4, ph); }
    }
    ((AnimWeightFn)kAnimWeight)(an, 4, bw);
    for (uint32_t l = 5; l < 8; l++) ((AnimWeightFn)kAnimWeight)(an, l, 0.0f);
}
// claves de la pista: cambio suave en 6 cuadros. Cara: capa 0 la de ahora, 1 la anterior, 2 base_normal con el resto, 4 parpadeo.
void OwnFace(Pj& p, void* h, void* face, const OwnTrack* tr, float tc, bool blink) {
    void* an = nullptr; memcpy(&an, face, 8); if (!an || !h || !tr || tr->n <= 0) return;
    const float fr = std::max(0.0f, tc) * 30.0f;
    int i = 0; while (i + 1 < tr->n && (float)tr->keys[i + 1].frame <= fr) i++;
    const OwnKey& k = tr->keys[i]; const OwnKey* q = i > 0 ? &tr->keys[i - 1] : nullptr;
    // (20:49, Agustin: "a las animaciones faciales les hace falta una transicion mas realista") fundido de 11 cuadros (o lo que dure la
    // clave anterior si es mas corta) con entrada y salida suaves; cada gesto sigue su propio tiempo desde que empezo (si la clave siguiente
    // repite el gesto no vuelve a 0), asi el habla y el parpadeo del gesto no se cortan en cada clave
    const float fade = q ? std::clamp((float)(k.frame - q->frame), 4.0f, 11.0f) : 1.0f;
    float a = q ? std::clamp((fr - (float)k.frame) / fade, 0.0f, 1.0f) : 1.0f; a = a * a * (3.0f - 2.0f * a);
    auto desde = [&](int j) { while (j > 0 && !strcmp(tr->keys[j - 1].face, tr->keys[j].face)) j--; return (float)tr->keys[j].frame; };
    const float fk0 = desde(i), fq0 = q ? desde(i - 1) : 0.0f;
    const int g = FaceIndexOf(k.face), gq = q ? FaceIndexOf(q->face) : -1;
    float w0 = g >= 0 ? a * std::clamp(k.faceW, 0.0f, 1.0f) : 0.0f, w1 = gq >= 0 ? (1.0f - a) * std::clamp(q->faceW, 0.0f, 1.0f) : 0.0f;
    if (g >= 0) {
        if (((AnimGetClipFn)kAnimGetClip)(an, 0) != (uint32_t)g) ((AnimSetBaseFn)kAnimSetBase)(an, 0, (uint32_t)g, 0, 0, 0, 0);
        ((AnimTimeFn)kAnimTime)(an, 0, (fr - fk0) / 30.0f);
    }
    ((AnimWeightFn)kAnimWeight)(an, 0, w0);
    if (gq >= 0 && w1 > 0.001f) {
        if (((AnimGetClipFn)kAnimGetClip)(an, 1) != (uint32_t)gq) ((AnimSetBaseFn)kAnimSetBase)(an, 1, (uint32_t)gq, 0, 0, 0, 0);
        ((AnimTimeFn)kAnimTime)(an, 1, (fr - fq0) / 30.0f);
    } else w1 = 0.0f;
    ((AnimWeightFn)kAnimWeight)(an, 1, w1);
    const float wr = std::max(0.0f, 1.0f - w0 - w1);
    if (kBaseNormal >= 0 && wr > 0.001f) {
        if (((AnimGetClipFn)kAnimGetClip)(an, 2) != (uint32_t)kBaseNormal) ((AnimSetBaseFn)kAnimSetBase)(an, 2, (uint32_t)kBaseNormal, 0, 0, 0, 0);
        ((AnimTimeFn)kAnimTime)(an, 2, tc); ((AnimWeightFn)kAnimWeight)(an, 2, wr);
    } else ((AnimWeightFn)kAnimWeight)(an, 2, 0.0f);
    ((AnimWeightFn)kAnimWeight)(an, 3, 0.0f);
    Blink(p, an, tc, blink);
    const int hl = HandIndexOf(k.handL), hr = HandIndexOf(k.handR);
    const int ql = q ? HandIndexOf(q->handL) : -1, qr = q ? HandIndexOf(q->handR) : -1;
    void* anL = nullptr; void* anR = nullptr; memcpy(&anL, (const uint8_t*)face + 8, 8); memcpy(&anR, (const uint8_t*)face + 0x10, 8);
    const bool hands = s_handSt == 1 && hl >= 0 && hr >= 0 && anL && anR;
    if (hands) { HandLayers(anL, hl, ql, a); HandLayers(anR, hr, qr, a); }
    if (!p.ownSaid) { p.ownSaid = true; Log("motor (clip propio): %s con cara y manos de la pista (%d claves; manos %s)", tr->file, tr->n, hands ? "del clip" : "neutras"); }
    ((FhEvalFn)kFhEval)(face, 0.0f);
    void* hp = nullptr; memcpy(&hp, (const uint8_t*)h + 0x148, 8); if (!hp) return;
    void** vt = *(void***)h; if (!vt || !((HumanOkFn)vt[2])(h)) return;
    ((FhSendFn)kFhSend)(face, hp, hands ? 0 : 3, 0);
}
// (17/11 prueba 03:19: "no el escalar") la escala del hueso raiz se escribia (registro: 1.00 -> 1.38) y no se veia: la raiz del esqueleto no
// pasa su escala a los demas huesos. Ahora se escala la matriz de la raiz del controlador: las tres filas del giro en [ctl+0x188] + 0xa0 /
// + 0xb0 / + 0xc0 (las que escribe FUN_141afccc0, con la posicion en + 0xd0) se dejan con largo = escala pedida. Asi se agranda todo el
// esqueleto alrededor de los pies (la raiz esta en el piso). Cada cuadro despues de vf4, por si el clip vuelve a escribir el giro.
void RowScale(Pj& p, float want) {
    void* ctl = nullptr; memcpy(&ctl, p.fd + 0x50, 8); if (!ctl) return;
    const uintptr_t R = *(const uintptr_t*)((uintptr_t)ctl + 0x188); if (!PtrOk(R)) return;
    float* m = (float*)(R + 0xa0); bool ch = false;
    for (int r = 0; r < 3; r++) { float* v = m + r * 4; const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l < 1e-4f || l > 100.0f) return;
        if (fabsf(l - want) > 1e-4f) { const float f = want / l; v[0] *= f; v[1] *= f; v[2] *= f; ch = true; } }
    if (ch && fabsf(want - p.scW) > 1e-4f) { p.scW = want; static int nl = 0; if (nl++ < 8) Log("motor (jugadores propios): tamano %.2f en la matriz de la raiz", want); }
}
// (17/11 17:22, Agustin: "la escala no funciona") la matriz de la raiz (RowScale) se pasa a giro puro y pierde el largo. Leido en el C:
// FUN_141f87eb0 (lo llama el juego cada vez que el jugador recibe un controlador nuevo, FUN_141f88xxx) pone la estatura como escala del
// hueso root_main (indice = tabla 0x1438032e0 [0x13]) con FUN_141afa3c0(ctl, i, s) y la guarda por hueso en fd + 0x2cc + 4 * i (fd + 0x2c8 = 1
// ya calculado); en los controladores siguientes usa ese valor guardado. Aca: base = la del juego (leida una vez), s = base x escala pedida,
// con la funcion real sobre el controlador actual y el mismo valor en fd + 0x2cc, asi un clip nuevo lo conserva.
// (17/11 17:54, Agustin: "la escala sigue sin afectar al tamano del jugador"; el registro si mostraba root_main 1.18) Leido en el C:
// la escala del hueso 0 (arr [ctl+0x298][0]) la pasa al grafo de la animacion cobra::And::BodyAnimControl::vf2 (0x141af9c60), que
// escribe s y 1/s en tres nodos del grafo ([[ctl+0x88]+0x68]: entrada 1 +0x20/+0x24, entradas 11 y 13 +0xb0/+0xb4) antes de evaluar. El
// FixDemoHuman evalua con vf4 sin pasar por ahi. Aca se escribe lo mismo justo antes de vf4 y se deja en 1 despues (el grafo puede ser
// compartido con otros jugadores del mismo recurso).
static bool GraphScale(void* ctl, float sc) {
    __try {
        const uintptr_t r = *(const uintptr_t*)((uintptr_t)ctl + 0x88); if (!PtrOk(r)) return false;
        const uintptr_t g = *(const uintptr_t*)(r + 0x68); if (!PtrOk(g)) return false;
        const unsigned cnt = *(const uint16_t*)(g + 0xc); const uint32_t off = *(const uint32_t*)(g + 8); if (!off || cnt < 2) return false;
        const uintptr_t T = g + off; const float inv = 1.0f / sc; bool any = false;
        { const uint32_t o = *(const uint32_t*)(T + 4); if (o) { float* q = (float*)(g + o + 0x20); q[0] = sc; q[1] = inv; any = true; } }
        if (cnt > 0xb) { const uint32_t o = *(const uint32_t*)(T + 0x2c); if (o) { float* q = (float*)(g + o + 0xb0); q[0] = sc; q[1] = inv; } }
        if (cnt > 0xd) { const uint32_t o = *(const uint32_t*)(T + 0x34); if (o) { float* q = (float*)(g + o + 0xb0); q[0] = sc; q[1] = inv; } }
        return any;
    } __except (1) { return false; }
}
typedef void (__fastcall* BoneScaleFn)(void*, unsigned, float);
static void RootMainScale(Pj& p, float scale) {
    void* ctl = nullptr; memcpy(&ctl, p.fd + 0x50, 8); if (!ctl) return;
    const int i = *(const int*)(0x1438032e0ull + 0x13 * 4); if (i < 0 || i >= 20) return;
    const uintptr_t arr = *(const uintptr_t*)((uintptr_t)ctl + 0x298); if (!PtrOk(arr)) return;
    float* keep = (float*)(p.fd + 0x2cc + 4 * i);
    if (p.scBase <= 0.0f) { const float b = p.fd[0x2c8] ? *keep : *(const float*)(arr + (size_t)i * 4); if (!(b > 0.3f && b < 3.0f)) return; p.scBase = b;
        Log("motor (jugadores propios): estatura del juego %.3f (root_main %d)", b, i); }
    const float want = p.scBase * std::clamp(scale, 0.05f, 10.0f);
    if (ctl == p.scCtl && fabsf(want - p.scW) < 1e-5f && fabsf(*(const float*)(arr + (size_t)i * 4) - want) < 1e-5f) return;
    ((BoneScaleFn)0x141afa3c0ull)(ctl, (unsigned)i, want); *keep = want;
    if (fabsf(want - p.scW) > 1e-4f) { static int nl = 0; if (nl++ < 12) Log("motor (jugadores propios): tamano %.0f %% (root_main %.3f)", scale * 100.0f, want); }
    p.scW = want; p.scCtl = ctl;
}
static void StoreWorld(Pj& p, const float* w) { std::lock_guard<std::mutex> lk(s_mx); memcpy(p.wpos, w, 12); p.hasW = true; }
// (17/11 17:24, video de Agustin) los iconos del piso se dibujaban en el lugar PEDIDO; el jugador queda donde lo deja el clip (la raiz del
// controlador, fila de la posicion en [ctl+0x188] + 0xd0, la que escribe FUN_141afd060). Se lee cada cuadro despues de vf4 para dibujarlos ahi.
static bool ReadRootPos(void* ctl, float* o) { const uintptr_t R = *(const uintptr_t*)((uintptr_t)ctl + 0x188); if (!PtrOk(R)) return false;
    const float* v = (const float*)(R + 0xd0); for (int k = 0; k < 3; k++) { if (!(v[k] == v[k]) || fabsf(v[k]) > 500.0f) return false; o[k] = v[k]; } return true; }
// ---- (17/11 18:23, Agustin: "animaciones tenemos que avanzar en el sistema avanzado") POSAR, base del sistema avanzado.
// Leido en el C (draw::ModelViewBoneUpdator::vf2 0x1423f2ca0, que manda manos y cara hueso por hueso): despues de evaluar, el
// controlador del clip tiene por hueso la ROTACION en [ctl+0x20] + i*0x10 (cuaternion, FUN_141b063e0) y el LUGAR en
// [[ctl+0x88]+0x50] + i*0x10 (FUN_141b06390), los dos en el espacio del modelo; cantidad = [[ctl+0x100]+0x20]+0xf8 (FUN_141b04910).
// FUN_141f86d40 (despues de vf4) manda esa pose. Aca, entre los dos, se gira cada hueso pedido y todos los que cuelgan de el alrededor
// de su lugar. Los padres y los nombres no estan leidos: se buscan en la descripcion del esqueleto ([[ctl+0x100]+0x20]) una tabla de
// padres (cada uno menor que su hueso, el 0 sin padre) y una de hashes de nombre (contra src/skbonenames.inc). INFERIDO: si no aparece
// la de padres, se gira solo ese hueso (se anota en el registro).
#include "skbonenames.inc"
static const char* SkName(uint32_t h) { for (const auto& e : kSkNames) if (e.h == h) return e.n; return nullptr; }
struct SkDesc { uintptr_t desc = 0; int n = 0; int par[400]; bool parOk = false; uint32_t hash[400]; bool hashOk = false;
    // (17/11 22:55) papeles de los huesos para los contactos (0 izquierda, 1 derecha), por los padres y el lugar (SkRoles)
    bool roleTried = false, roleOk = false; int head = -1, chest = -1, thigh[2] = { -1, -1 }, foot[2] = { -1, -1 }, up[2] = { -1, -1 }, fo[2] = { -1, -1 }, hand[2] = { -1, -1 };
    int lab[400]; };      // (23:08) nombre de cada hueso para el editor (MotorBoneLabel); 0 = sin nombre
static SkDesc s_sk[kMax];
// (17/11 22:55, registro de la prueba de las 19:53 con B4FCF296) en el FixDemoHuman [[ctl+0x100]+0x20] es el mismo objeto que [ctl+0x88]
// (el de los lugares en +0x50): su +0xf8 es un puntero, no la cantidad. La cantidad esta en +0x18 (0x14 = 20, igual que ctl+0x18 / +0x1c,
// y 20 = los huesos del clip de cuerpo: raiz, cadera, 2 x (muslo, rodilla, pie), columna, pecho, cuello, cabeza, 2 x (clavicula, brazo,
// antebrazo, mano)). Los padres pueden estar un nivel mas adentro (objeto compartido del esqueleto, el mismo puntero en los dos jugadores):
// se buscan en la descripcion y en los objetos a los que apunta.
static bool SkParTry(SkDesc& d, uintptr_t q, int n) {
    MEMORY_BASIC_INFORMATION mi{}; if (!VirtualQuery((void*)q, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || q + (size_t)n * 4 > (uintptr_t)mi.BaseAddress + mi.RegionSize) return false;
    for (int wdt = 1; wdt <= 4; wdt *= 2) {      // padres de 1, 2 o 4 bytes
        bool okp = true; int roots = 0; static int pv[400]; static int kids[400];
        for (int i = 0; i < n && okp; i++) { int v = wdt == 1 ? (int)*(const uint8_t*)(q + i) : wdt == 2 ? (int)*(const uint16_t*)(q + 2 * i) : *(const int*)(q + 4 * i);
            const int none = wdt == 1 ? 0xff : wdt == 2 ? 0xffff : -1;
            if (v == none) { roots++; pv[i] = -1; continue; } if (v < 0 || v >= i) okp = false; else pv[i] = v; }
        if (!okp || roots < 1 || roots > 2 || pv[0] != -1) continue;
        for (int i = 0; i < n; i++) kids[i] = 0; for (int i = 0; i < n; i++) if (pv[i] >= 0) kids[pv[i]]++;
        int leaves = 0; for (int i = 0; i < n; i++) if (!kids[i]) leaves++;
        if (leaves < 3 || leaves * 2 > n + 2) continue;     // un esqueleto tiene pocas puntas (cabeza, manos, pies)
        for (int i = 0; i < n; i++) d.par[i] = pv[i]; d.parOk = true; return true;
    }
    return false;
}
static void SkScan(int slot, void* ctl) {
    SkDesc& d = s_sk[slot];
    __try {
        const uintptr_t a = *(const uintptr_t*)((uintptr_t)ctl + 0x100); if (!PtrOk(a)) return;
        const uintptr_t desc = *(const uintptr_t*)(a + 0x20); if (!PtrOk(desc)) return;
        int n = *(const int*)(desc + 0xf8);
        if (n < 2 || n > 400) { const int n1 = *(const int*)(desc + 0x18), n2 = *(const int*)((uintptr_t)ctl + 0x18); if (n1 >= 2 && n1 <= 400 && n1 == n2) n = n1; }
        if (d.desc == desc && d.n == n) return;
        d = SkDesc{}; d.desc = desc; d.n = n;
        if (n < 2 || n > 400) { d.n = 0;
            static bool said[kMax]; if (!said[slot]) { said[slot] = true; char hx[1100]; int o;
                auto dump = [&](const char* nm, uintptr_t base, int len) { o = 0; for (int q = 0; q < len; q += 8) o += snprintf(hx + o, sizeof hx - o, "%s%llx", q ? " " : "", (unsigned long long)*(const uint64_t*)(base + q)); Log("motor (posar): %s %p: %s", nm, (void*)base, hx); };
                dump("ctl", (uintptr_t)ctl, 0x140); dump("[ctl+0x100]", a, 0x80); dump("desc", desc, 0x100); }
            return; }
        int parOff = -1, parIn = -1, hashOff = -1;
        for (int off = 0; off <= 0x1f0 && (!d.parOk || hashOff < 0); off += 8) {
            const uintptr_t q = *(const uintptr_t*)(desc + off); if (!PtrOk(q)) continue;
            if (!d.parOk && SkParTry(d, q, n)) parOff = off;
            if (hashOff < 0) { MEMORY_BASIC_INFORMATION mi{};
                if (VirtualQuery((void*)q, &mi, sizeof mi) && mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && q + (size_t)n * 4 <= (uintptr_t)mi.BaseAddress + mi.RegionSize) {
                    int hit = 0; for (int i = 0; i < n; i++) if (SkName(*(const uint32_t*)(q + 4 * i))) hit++;
                    if (hit * 3 >= n) { hashOff = off; for (int i = 0; i < n; i++) d.hash[i] = *(const uint32_t*)(q + 4 * i); d.hashOk = true; } } }
        }
        for (int off = 0; off <= 0x1f0 && !d.parOk; off += 8) {     // un nivel mas adentro
            const uintptr_t q = *(const uintptr_t*)(desc + off); if (!PtrOk(q)) continue;
            MEMORY_BASIC_INFORMATION mi{}; if (!VirtualQuery((void*)q, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || q + 0x100 > (uintptr_t)mi.BaseAddress + mi.RegionSize) continue;
            for (int o2 = 0; o2 < 0x100 && !d.parOk; o2 += 8) { const uintptr_t q2 = *(const uintptr_t*)(q + o2); if (PtrOk(q2) && SkParTry(d, q2, n)) { parOff = off; parIn = o2; } }
        }
        Log("motor (posar): jugador %d: esqueleto %p con %d huesos; padres %s (+0x%x%s%x); nombres %s (+0x%x)", slot + 1, (void*)desc, n,
            d.parOk ? "encontrados" : "NO encontrados (se gira solo el hueso)", parOff, parIn >= 0 ? " y adentro +0x" : "", parIn >= 0 ? parIn : 0, d.hashOk ? "encontrados" : "NO encontrados", hashOff);
        if (d.parOk) { char b[1400]; int o = 0; for (int i = 0; i < n && o < 1300; i++) o += snprintf(b + o, sizeof b - o, "%s%d", i ? " " : "", d.par[i]); Log("motor (posar): padres: %s", b); }
    } __except (1) { d = SkDesc{}; }
}
static inline void QMul(const float* a, const float* b, float* o) {      // o = a * b (x, y, z, w)
    const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]; o[0] = x; o[1] = y; o[2] = z; o[3] = w; }
static inline void QRot(const float* q, const float* v, float* o) { const float cx = 2.0f * (q[1] * v[2] - q[2] * v[1]), cy = 2.0f * (q[2] * v[0] - q[0] * v[2]), cz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    o[0] = v[0] + q[3] * cx + (q[1] * cz - q[2] * cy); o[1] = v[1] + q[3] * cy + (q[2] * cx - q[0] * cz); o[2] = v[2] + q[3] * cz + (q[0] * cy - q[1] * cx); }
static void StoreBone(Pj& p, const float* bw, bool ok) { std::lock_guard<std::mutex> lk(s_mx); memcpy(p.boneW, bw, 12); p.boneWOk = ok; }   // fuera del __try (C2712)
static void PoseApply(Pj& p, void* ctl, const Pj& w) {
    const int slot = (int)(&p - s_pj);
    SkScan(slot, ctl); const SkDesc& d = s_sk[slot];
    bool wok = false; float bw[3] = {};
    __try {
        const uintptr_t qa = *(const uintptr_t*)((uintptr_t)ctl + 0x20); const uintptr_t r = *(const uintptr_t*)((uintptr_t)ctl + 0x88); if (!PtrOk(qa) || !PtrOk(r)) return;
        const uintptr_t pa = *(const uintptr_t*)(r + 0x50); if (!PtrOk(pa) || d.n < 2) return;
        float* Q = (float*)qa; float* P = (float*)pa; const int n = d.n;
        static bool inSub[400];
        for (int k = 0; k < w.nPose && k < kPoseMax; k++) { const int b = w.pose[k].bone; if (b < 0 || b >= n) continue;
            const float* e = w.pose[k].e; if (fabsf(e[0]) + fabsf(e[1]) + fabsf(e[2]) < 1e-3f) continue;
            float qx[4] = { sinf(e[0] * 0.00872665f), 0, 0, cosf(e[0] * 0.00872665f) }, qy[4] = { 0, sinf(e[1] * 0.00872665f), 0, cosf(e[1] * 0.00872665f) }, qz[4] = { 0, 0, sinf(e[2] * 0.00872665f), cosf(e[2] * 0.00872665f) }, t[4], R[4];
            QMul(qy, qx, t); QMul(t, qz, R);      // giro en los ejes del modelo: primero Z, despues X, despues Y
            const float pv[3] = { P[b * 4], P[b * 4 + 1], P[b * 4 + 2] };
            for (int i = 0; i < n; i++) { bool in = i == b; if (!in && d.parOk) { int j = d.par[i]; for (int g = 0; g < 64 && j >= 0; g++) { if (j == b) { in = true; break; } j = d.par[j]; } } inSub[i] = in; }
            for (int i = 0; i < n; i++) { if (!inSub[i]) continue; float o[4]; QMul(R, Q + i * 4, o); memcpy(Q + i * 4, o, 16);
                if (i != b) { const float v[3] = { P[i * 4] - pv[0], P[i * 4 + 1] - pv[1], P[i * 4 + 2] - pv[2] }; float rv[3]; QRot(R, v, rv); for (int c = 0; c < 3; c++) P[i * 4 + c] = pv[c] + rv[c]; } }
        }
        if (w.boneQ >= 0 && w.boneQ < n) { const float* m = P + w.boneQ * 4; float rt[3];      // lugar del hueso en el mundo (para marcarlo en la vista)
            if (ReadRootPos(ctl, rt)) { const uintptr_t R0 = *(const uintptr_t*)((uintptr_t)ctl + 0x188); const float* M = (const float*)(R0 + 0xa0);
                for (int c = 0; c < 3; c++) bw[c] = rt[c] + M[c] * m[0] + M[4 + c] * m[1] + M[8 + c] * m[2]; wok = true; } }
    } __except (1) { return; }
    StoreBone(p, bw, wok);
}
// ---- (17/11 22:55, Agustin con video: "no se coinciden correctamente", "creo que las alturas de los jugadores no se respetan"; 23:03:
// "que se adapte dependiendo la diferencia: si miden mas o menos lo mismo no hace falta que se ponga en puntas de pie, solo que levante
// un poco los hombros y brazos") CONTACTOS de la pareja de clips propios. Los .gani se arman sobre un esqueleto aproximado y a estatura 1;
// en el juego cada brazo mide distinto y cada jugador tiene su estatura, asi que las manos no caen justas. motorpista.inc trae por
// cuadro donde tiene que quedar cada muneca respecto del otro (su muneca o su pecho + vector en su marco). Aca, despues de PoseApply y
// antes de mandar la pose: se miden los huesos reales (lugares del controlador pasados al mundo con la raiz), se escala el vector por el
// tamano del otro y se corrige el brazo con IK de dos huesos (en el choque y el apreton cada uno pone la mitad: se encuentran en el
// medio). Si el brazo no llega, el mas bajo sube el hombro (desde 3 cm de diferencia) y, si aun falta, se pone en puntas de pie (desde
// 8 cm de diferencia; cuanto mas diferencia, mas alto). Los papeles de los huesos (cabeza, pecho, muslos, pies, brazos) salen de los
// padres y del lugar en el primer cuadro (SkRoles). INFERIDO: los lugares de [[ctl+0x88]+0x50] estan en el espacio del modelo y la
// matriz de [ctl+0x188]+0xa0 (filas = ejes del modelo en el mundo) + el lugar de +0xd0 los pasa al mundo (como el marcador de PoseApply).
static inline void V3(float* o, float x, float y, float z) { o[0] = x; o[1] = y; o[2] = z; }
static inline void VSub(const float* a, const float* b, float* o) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static inline float VDot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static inline float VLen(const float* a) { return sqrtf(VDot(a, a)); }
static inline void VCross(const float* a, const float* b, float* o) { const float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0]; o[0] = x; o[1] = y; o[2] = z; }
static inline bool VNorm(float* a) { const float l = VLen(a); if (l < 1e-6f) return false; a[0] /= l; a[1] /= l; a[2] /= l; return true; }
static inline void QAxisAng(const float* ax, float ang, float* q) { const float h = 0.5f * ang, sn = sinf(h); q[0] = ax[0] * sn; q[1] = ax[1] * sn; q[2] = ax[2] * sn; q[3] = cosf(h); }
static void QFromTo(const float* a0, const float* b0, float* q) {     // giro mas corto de la direccion a a la b
    float a[3] = { a0[0], a0[1], a0[2] }, b[3] = { b0[0], b0[1], b0[2] }; if (!VNorm(a) || !VNorm(b)) { V3(q, 0, 0, 0); q[3] = 1; return; }
    float c[3]; VCross(a, b, c); const float d = VDot(a, b);
    if (d < -0.9999f) { float t[3] = { 1, 0, 0 }; if (fabsf(a[0]) > 0.9f) V3(t, 0, 1, 0); VCross(a, t, c); VNorm(c); V3(q, c[0], c[1], c[2]); q[3] = 0; return; }
    q[0] = c[0]; q[1] = c[1]; q[2] = c[2]; q[3] = 1.0f + d; const float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]); for (int k = 0; k < 4; k++) q[k] /= l;
}
// gira el hueso b y todo lo que cuelga de el, alrededor del punto pv (giros y lugares en el espacio del modelo)
static void RotSub(float* Q, float* P, int n, const int* par, int b, const float* R, const float* pv) {
    for (int i = 0; i < n; i++) {
        bool in = i == b; for (int j = par[i], g = 0; !in && j >= 0 && g < 64; j = par[j], g++) if (j == b) in = true;
        if (!in) continue;
        float o[4]; QMul(R, Q + i * 4, o); memcpy(Q + i * 4, o, 16);
        const float v[3] = { P[i * 4] - pv[0], P[i * 4 + 1] - pv[1], P[i * 4 + 2] - pv[2] }; float rv[3]; QRot(R, v, rv);
        for (int c = 0; c < 3; c++) P[i * 4 + c] = pv[c] + rv[c];
    }
}
struct RootX { float rt[3]; float A[9]; float Ai[9]; };       // mundo = rt + Pm.x * fila0 + Pm.y * fila1 + Pm.z * fila2
static bool RootGet(void* ctl, RootX& x) {
    if (!ReadRootPos(ctl, x.rt)) return false;
    const uintptr_t R0 = *(const uintptr_t*)((uintptr_t)ctl + 0x188); if (!PtrOk(R0)) return false;
    const float* M = (const float*)(R0 + 0xa0);
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) { const float v = M[r * 4 + c]; if (!(v == v) || fabsf(v) > 100.0f) return false; x.A[r * 3 + c] = v; }
    // mundo = A^T Pm -> Pm = (A^T)^-1 (mundo - rt)
    float T[9]; for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) T[r * 3 + c] = x.A[c * 3 + r];
    const float det = T[0] * (T[4] * T[8] - T[5] * T[7]) - T[1] * (T[3] * T[8] - T[5] * T[6]) + T[2] * (T[3] * T[7] - T[4] * T[6]);
    if (fabsf(det) < 1e-6f) return false; const float id = 1.0f / det;
    x.Ai[0] = (T[4] * T[8] - T[5] * T[7]) * id; x.Ai[1] = (T[2] * T[7] - T[1] * T[8]) * id; x.Ai[2] = (T[1] * T[5] - T[2] * T[4]) * id;
    x.Ai[3] = (T[5] * T[6] - T[3] * T[8]) * id; x.Ai[4] = (T[0] * T[8] - T[2] * T[6]) * id; x.Ai[5] = (T[2] * T[3] - T[0] * T[5]) * id;
    x.Ai[6] = (T[3] * T[7] - T[4] * T[6]) * id; x.Ai[7] = (T[1] * T[6] - T[0] * T[7]) * id; x.Ai[8] = (T[0] * T[4] - T[1] * T[3]) * id;
    return true;
}
static inline void ToWorld(const RootX& x, const float* m, float* w) { for (int c = 0; c < 3; c++) w[c] = x.rt[c] + m[0] * x.A[c] + m[1] * x.A[3 + c] + m[2] * x.A[6 + c]; }
static inline void DirToModel(const RootX& x, const float* d, float* m) { for (int r = 0; r < 3; r++) m[r] = x.Ai[r * 3] * d[0] + x.Ai[r * 3 + 1] * d[1] + x.Ai[r * 3 + 2] * d[2]; }
static inline void ToModel(const RootX& x, const float* w, float* m) { float d[3]; VSub(w, x.rt, d); DirToModel(x, d, m); }
static bool SkRoles(int slot, SkDesc& d, const float* P) {
    const int n = d.n; static int kids[400]; for (int i = 0; i < n; i++) kids[i] = 0; for (int i = 0; i < n; i++) if (d.par[i] >= 0) kids[d.par[i]]++;
    int lv[16], nl = 0; for (int i = 0; i < n && nl < 16; i++) if (!kids[i]) lv[nl++] = i;
    auto y = [&](int i) { return P[i * 4 + 1]; };
    auto bad = [&](const char* why) { Log("motor (contactos): jugador %d: no se reconocen los huesos (%s; %d puntas); sin correccion", slot + 1, why, nl); return false; };
    if (nl != 5) return bad("hacen falta 5 puntas: cabeza, 2 manos, 2 pies");
    for (int a = 0; a < nl; a++) for (int b = a + 1; b < nl; b++) if (y(lv[b]) > y(lv[a])) std::swap(lv[a], lv[b]);   // de arriba a abajo
    d.head = lv[0]; int hd[2] = { lv[1], lv[2] }, ft[2] = { lv[3], lv[4] };
    auto gp = [&](int i) { return i >= 0 ? d.par[i] : -1; };
    for (int k = 0; k < 2; k++) { if (gp(gp(hd[k])) < 0 || gp(gp(ft[k])) < 0) return bad("cadenas cortas"); }
    if (P[gp(gp(ft[0])) * 4] < P[gp(gp(ft[1])) * 4]) std::swap(ft[0], ft[1]);        // x del modelo = izquierda (0)
    if (P[gp(gp(hd[0])) * 4] < P[gp(gp(hd[1])) * 4]) std::swap(hd[0], hd[1]);
    for (int k = 0; k < 2; k++) { d.foot[k] = ft[k]; d.thigh[k] = gp(gp(ft[k])); d.hand[k] = hd[k]; d.fo[k] = gp(hd[k]); d.up[k] = gp(d.fo[k]); }
    const int cl = gp(d.up[0]); d.chest = gp(cl); if (d.chest < 0) return bad("sin pecho");
    float L[4]; for (int k = 0; k < 2; k++) { float v[3]; VSub(P + d.fo[k] * 4, P + d.up[k] * 4, v); L[k * 2] = VLen(v); VSub(P + d.hand[k] * 4, P + d.fo[k] * 4, v); L[k * 2 + 1] = VLen(v); }
    for (int k = 0; k < 4; k++) if (L[k] < 0.12f || L[k] > 0.5f) return bad("largos de brazo raros");
    {   // nombres para el editor: 1 raiz, 2 cadera, 3 columna, 4 pecho, 5 cuello, 6 cabeza, 7-10 clavicula / brazo / antebrazo / mano izq,
        // 11-14 der, 15-17 muslo / pierna / pie izq, 18-20 der
        for (int i = 0; i < n; i++) d.lab[i] = 0;
        for (int i = 0; i < n; i++) if (d.par[i] < 0) d.lab[i] = 1;
        const int hips = gp(d.thigh[0]); if (hips >= 0 && d.lab[hips] == 0) d.lab[hips] = 2;
        for (int j = gp(d.chest), g = 0; j >= 0 && j != hips && g < 16; j = gp(j), g++) if (!d.lab[j]) d.lab[j] = 3;
        d.lab[d.chest] = 4; d.lab[d.head] = 6; if (gp(d.head) >= 0 && gp(d.head) != d.chest) d.lab[gp(d.head)] = 5;
        for (int k = 0; k < 2; k++) { if (gp(d.up[k]) >= 0 && gp(d.up[k]) != d.chest) d.lab[gp(d.up[k])] = 7 + 4 * k; d.lab[d.up[k]] = 8 + 4 * k; d.lab[d.fo[k]] = 9 + 4 * k; d.lab[d.hand[k]] = 10 + 4 * k;
            d.lab[d.thigh[k]] = 15 + 3 * k; d.lab[gp(d.foot[k])] = 16 + 3 * k; d.lab[d.foot[k]] = 17 + 3 * k; }
    }
    Log("motor (contactos): jugador %d: cabeza %d, pecho %d, muslos %d/%d, pies %d/%d, brazo izq %d-%d-%d (%.1f + %.1f cm), der %d-%d-%d (%.1f + %.1f cm)", slot + 1,
        d.head, d.chest, d.thigh[0], d.thigh[1], d.foot[0], d.foot[1], d.up[0], d.fo[0], d.hand[0], L[0] * 100, L[1] * 100, d.up[1], d.fo[1], d.hand[1], L[2] * 100, L[3] * 100);
    { char b[1600]; int o = 0; for (int i = 0; i < n && o < 1500; i++) o += snprintf(b + o, sizeof b - o, "%s%d(%.2f %.2f %.2f)", i ? " " : "", i, P[i * 4], P[i * 4 + 1], P[i * 4 + 2]); Log("motor (contactos): lugares en el modelo: %s", b); }
    return true;
}
// IK de dos huesos en el espacio del modelo: el codo (o la rodilla) se dobla para la distancia y la cadena entera gira para apuntar al
// objetivo. Re / Rs = los dos giros hechos (codo primero, despues la cadena desde el hombro), para pasarlos a la pose del editor.
static void IK2(float* Q, float* P, const SkDesc& d, int u, int f, int h, const float* T, const float* upM, float* Re, float* Rs) {
    V3(Re, 0, 0, 0); Re[3] = 1; V3(Rs, 0, 0, 0); Rs[3] = 1;
    float S[3], E[3], W[3]; memcpy(S, P + u * 4, 12); memcpy(E, P + f * 4, 12); memcpy(W, P + h * 4, 12);
    float uv[3], vv[3], st[3]; VSub(E, S, uv); VSub(W, E, vv); VSub(T, S, st);
    const float a = VLen(uv), b = VLen(vv); if (a < 1e-3f || b < 1e-3f) return;
    const float dT = std::clamp(VLen(st), fabsf(a - b) + 0.01f, (a + b) * 0.995f);
    float nx[3]; VCross(uv, vv, nx);
    if (!VNorm(nx)) { float back[3]; VCross(uv, upM, back); VCross(back, uv, nx); if (!VNorm(nx)) return; }   // recto: dobla hacia abajo
    const float beta = acosf(std::clamp(VDot(uv, vv) / (a * b), -1.0f, 1.0f));          // 0 = recto
    const float beta2 = 3.14159265f - acosf(std::clamp((a * a + b * b - dT * dT) / (2.0f * a * b), -1.0f, 1.0f));
    QAxisAng(nx, beta2 - beta, Re); RotSub(Q, P, d.n, d.par, f, Re, E);
    float W2[3], sw[3]; memcpy(W2, P + h * 4, 12); VSub(W2, S, sw);
    QFromTo(sw, st, Rs); RotSub(Q, P, d.n, d.par, u, Rs, S);
}
static void ArmIK(float* Q, float* P, const SkDesc& d, int k, const float* T, const float* upM) { float Re[4], Rs[4]; IK2(Q, P, d, d.up[k], d.fo[k], d.hand[k], T, upM, Re, Rs); }
static void ContactIK(Pj& p, void* ctl, float dtR) {
    const int slot = (int)(&p - s_pj); SkDesc& d = s_sk[slot];
    p.sn[1] = p.sn[0]; p.sn[0].ok = false;
    const OwnTrack* tr = OwnTrackOf(p.clipA);
    if (!tr || !d.parOk || d.n < 2) { p.lift = 0.0f; return; }
    __try {
        const uintptr_t qa = *(const uintptr_t*)((uintptr_t)ctl + 0x20); const uintptr_t r = *(const uintptr_t*)((uintptr_t)ctl + 0x88); if (!PtrOk(qa) || !PtrOk(r)) return;
        const uintptr_t pa = *(const uintptr_t*)(r + 0x50); if (!PtrOk(pa)) return;
        float* Q = (float*)qa; float* P = (float*)pa;
        if (!d.roleTried) { d.roleTried = true; d.roleOk = SkRoles(slot, d, P); }
        if (!d.roleOk) return;
        RootX X; if (!RootGet(ctl, X)) return;
        const float t = ((ClipTimeFn)kClipTime)(ctl, 0, 1);
        // lo de este jugador antes de corregir (lo usa el otro)
        Pj::Snap& s0 = p.sn[0]; s0.t = t;
        for (int k = 0; k < 2; k++) ToWorld(X, P + d.hand[k] * 4, s0.hand[k]);
        ToWorld(X, P + d.chest * 4, s0.chest);
        float tl[3], tr_[3], hw[3]; ToWorld(X, P + d.thigh[0] * 4, tl); ToWorld(X, P + d.thigh[1] * 4, tr_); ToWorld(X, P + d.head * 4, hw);
        VSub(tl, tr_, s0.X); s0.X[1] = 0.0f; if (!VNorm(s0.X)) return;
        { const float mid[3] = { 0.5f * (tl[0] + tr_[0]), 0.5f * (tl[1] + tr_[1]), 0.5f * (tl[2] + tr_[2]) }; float v[3]; VSub(hw, mid, v); s0.sc = tr->torso > 0.1f ? std::clamp(VLen(v) / tr->torso, 0.6f, 1.5f) : 1.0f; }
        s0.ok = true;
        if (p.partner < 0 || p.partner >= kMax || p.partner == slot) { p.lift = 0.0f; return; }
        const Pj& o = s_pj[p.partner]; if (!o.sn[0].ok) return;
        float k1 = 0.0f; const float dt01 = o.sn[1].ok ? o.sn[0].t - o.sn[1].t : 0.0f;
        if (fabsf(t - o.sn[0].t) > 0.25f) return;                                    // fuera de sincronia: sin corregir
        if (dt01 > 0.001f && dt01 < 0.25f) k1 = std::clamp((t - o.sn[0].t) / dt01, -1.0f, 1.5f);   // lo del otro llevado a mi segundo
        auto at = [&](const float* a0, const float* a1, float* out) { for (int c = 0; c < 3; c++) out[c] = a0[c] + (k1 != 0.0f ? (a0[c] - a1[c]) * k1 : 0.0f); };
        const float up[3] = { 0, 1, 0 }; float Z[3]; VCross(o.sn[0].X, up, Z);
        const float fr = t * 30.0f;
        float goal[2][3]; bool has[2] = { false, false }; float errC[2] = { 0, 0 };
        for (int si = 0; si < tr->nSeg; si++) {
            const OwnSeg& g = tr->seg[si]; if (g.hand < 0 || g.hand > 1 || has[g.hand]) continue;
            const float kf = fr - (float)g.f0; if (kf < 0.0f || kf > (float)(g.n - 1)) continue;
            const int i0 = std::min((int)kf, g.n - 1), i1 = std::min(i0 + 1, g.n - 1); const float a = kf - (float)i0;
            const float* c0 = tr->cdat + (size_t)(g.off + i0) * 4; const float* c1 = tr->cdat + (size_t)(g.off + i1) * 4;
            const float w = c0[0] + (c1[0] - c0[0]) * a; if (w <= 0.001f) continue;
            const float v[3] = { c0[1] + (c1[1] - c0[1]) * a, c0[2] + (c1[2] - c0[2]) * a, c0[3] + (c1[3] - c0[3]) * a };
            float A[3]; if (g.anchor < 2) at(o.sn[0].hand[g.anchor], o.sn[1].hand[g.anchor], A); else at(o.sn[0].chest, o.sn[1].chest, A);
            const float sc = g.anchor == 2 ? o.sn[0].sc : 0.5f * (o.sn[0].sc + s0.sc);
            float Tw[3]; for (int c = 0; c < 3; c++) Tw[c] = A[c] + sc * (v[0] * o.sn[0].X[c] + v[1] * up[c] + v[2] * Z[c]);
            float e[3]; VSub(Tw, s0.hand[g.hand], e); if (g.anchor < 2) for (int c = 0; c < 3; c++) e[c] *= 0.5f;
            const float el = VLen(e); errC[g.hand] = el;
            if (el > 0.40f) { if (p.cN < 40 && p.cLog + 1.0 < s_lastTick.load()) { p.cN++; p.cLog = s_lastTick.load(); Log("motor (contactos): jugador %d cuadro %.0f: la mano %s esta a %.0f cm del punto: demasiado lejos, sin corregir", slot + 1, fr, g.hand ? "der" : "izq", el * 100.0f); } continue; }
            for (int c = 0; c < 3; c++) goal[g.hand][c] = s0.hand[g.hand][c] + w * e[c];
            has[g.hand] = true;
        }
        // diferencia de estatura (cm; > 0 = el otro es mas alto): hombros desde 3 cm, puntas de pie desde 8 cm
        const float diff = (p.hcm && o.hcm) ? (float)o.hcm - (float)p.hcm : (o.sn[0].sc - s0.sc) * 180.0f;
        const float shMax = std::clamp((diff - 3.0f) / 8.0f, 0.0f, 1.0f) * 0.2443f;          // hasta 14 grados de hombro
        const float tipMax = std::clamp((diff - 8.0f) / 12.0f, 0.0f, 1.0f) * 0.06f * s0.sc;   // hasta 6 cm de talon
        float upM[3]; DirToModel(X, up, upM); VNorm(upM);
        float shW[2] = { 0, 0 }, liftW = 0.0f;
        for (int k = 0; k < 2; k++) {
            if (!has[k]) continue;
            float Sw[3], Ew[3], Hw[3], v[3]; ToWorld(X, P + d.up[k] * 4, Sw); ToWorld(X, P + d.fo[k] * 4, Ew); ToWorld(X, P + d.hand[k] * 4, Hw);
            VSub(Ew, Sw, v); float L = VLen(v); VSub(Hw, Ew, v); L = (L + VLen(v)) * 0.97f;
            VSub(goal[k], Sw, v); float over = VLen(v) - L; if (over <= 0.0f || v[1] <= 0.0f) continue;
            over *= v[1] / std::max(VLen(v), 1e-3f);                     // lo que falta para arriba
            const int cl = d.par[d.up[k]]; float cw[3]; ToWorld(X, P + cl * 4, cw); VSub(Sw, cw, v); const float rc = std::max(0.05f, VLen(v));
            const float a = std::min(shMax, asinf(std::min(1.0f, over / rc))); shW[k] = a; over -= rc * sinf(a);
            if (over > 0.0f) liftW = std::max(liftW, std::min(tipMax, over));
        }
        const float kk = std::clamp(dtR * 6.0f, 0.0f, 1.0f);
        p.lift += (liftW - p.lift) * kk; for (int k = 0; k < 2; k++) p.shr[k] += (shW[k] - p.shr[k]) * kk;
        // puntas de pie: todo el cuerpo sube y cada pie gira sobre la punta (talon arriba, la punta queda en el piso)
        if (p.lift > 0.001f) {
            for (int i = 0; i < d.n; i++) for (int c = 0; c < 3; c++) P[i * 4 + c] += upM[c] * p.lift;
            float xm[3]; VSub(P + d.thigh[0] * 4, P + d.thigh[1] * 4, xm); const float dd = VDot(xm, upM); for (int c = 0; c < 3; c++) xm[c] -= upM[c] * dd;
            if (VNorm(xm)) {
                const float ha = 0.08f * s0.sc, tz = 0.14f * s0.sc, Rr = sqrtf(ha * ha + tz * tz), ph = atan2f(tz, ha);
                const float th = std::max(0.0f, ph - acosf(std::min(1.0f, (ha + p.lift) / Rr)));
                float R[4]; QAxisAng(xm, th, R);
                for (int k = 0; k < 2; k++) { float pv[3]; memcpy(pv, P + d.foot[k] * 4, 12); RotSub(Q, P, d.n, d.par, d.foot[k], R, pv); }
            }
        }
        // hombro arriba (clavicula): gira alrededor de su base hacia arriba
        for (int k = 0; k < 2; k++) {
            if (p.shr[k] < 0.002f) continue;
            const int cl = d.par[d.up[k]]; if (cl < 0) continue;
            float dir[3], ax[3]; VSub(P + d.up[k] * 4, P + cl * 4, dir); VCross(dir, upM, ax); if (!VNorm(ax)) continue;
            float R[4], pv[3]; memcpy(pv, P + cl * 4, 12); QAxisAng(ax, p.shr[k], R); RotSub(Q, P, d.n, d.par, cl, R, pv);
        }
        for (int k = 0; k < 2; k++) { if (!has[k]) continue; float Tm[3]; ToModel(X, goal[k], Tm); ArmIK(Q, P, d, k, Tm, upM); }
        if ((has[0] || has[1]) && p.cN < 40 && p.cLog + 0.5 < s_lastTick.load()) { p.cN++; p.cLog = s_lastTick.load();
            float aw[2][3]; for (int k = 0; k < 2; k++) ToWorld(X, P + d.hand[k] * 4, aw[k]);
            float r0[3], r1[3]; VSub(aw[0], goal[0], r0); VSub(aw[1], goal[1], r1);
            Log("motor (contactos): jugador %d cuadro %.0f: corrijo izq %.1f cm (queda %.1f), der %.1f cm (queda %.1f); hombros %.0f / %.0f grados, puntas %.1f cm (el otro %+.0f cm; estaturas %.2f / %.2f)",
                slot + 1, fr, has[0] ? errC[0] * 100 : 0.0f, has[0] ? VLen(r0) * 100 : 0.0f, has[1] ? errC[1] * 100 : 0.0f, has[1] ? VLen(r1) * 100 : 0.0f,
                p.shr[0] * 57.3f, p.shr[1] * 57.3f, p.lift * 100.0f, diff, s0.sc, o.sn[0].sc); }
    } __except (1) { return; }
}
// ---- (23:08) EDITOR en el hilo del juego: mano / pie agarrado (IK hacia el punto del mouse, el giro queda en la pose del editor) y los
// lugares de todos los huesos en el mundo para la vista (puntos, anillos, nombres)
static void EdPutGame(int slot, float t, int bone, const float* e) { std::lock_guard<std::mutex> lk(s_mx); EdPutLocked(slot, t, bone, e); }
static void StoreBones(Pj& p, const float (*bw)[3], int n, const int* par, const int* lab, const float (*ax)[3], float sc) {
    std::lock_guard<std::mutex> lk(s_mx); p.bwN = n; memcpy(p.bw, bw, sizeof(float) * 3 * n); memcpy(p.bpar, par, sizeof(int) * n); memcpy(p.blab, lab, sizeof(int) * n); memcpy(p.bax, ax, sizeof p.bax); p.bsc = sc; }
static void EditPass(Pj& p, void* ctl, const Pj& w) {
    const int slot = (int)(&p - s_pj); SkDesc& d = s_sk[slot];
    if (!d.parOk || d.n < 2) return;
    float bw[32][3]; int par[32], lab[32], n = 0; float ax[3][3]; float sc = 1.0f; bool ok = false;
    int wb[2] = { -1, -1 }; float we[2][3] = {}; float tNow = 0.0f;
    __try {
        const uintptr_t qa = *(const uintptr_t*)((uintptr_t)ctl + 0x20); const uintptr_t r = *(const uintptr_t*)((uintptr_t)ctl + 0x88); if (!PtrOk(qa) || !PtrOk(r)) return;
        const uintptr_t pa = *(const uintptr_t*)(r + 0x50); if (!PtrOk(pa)) return;
        float* Q = (float*)qa; float* P = (float*)pa;
        if (!d.roleTried) { d.roleTried = true; d.roleOk = SkRoles(slot, d, P); }
        RootX X; if (!RootGet(ctl, X)) return;
        tNow = ((ClipTimeFn)kClipTime)(ctl, 0, 1);
        if (d.roleOk && w.grab >= 0 && w.grab < 4) {      // agarrar: IK de la cadena hacia el punto, y el giro hecho pasa a la pose
            const int k = w.grab & 1; const bool leg = w.grab >= 2;
            const int u = leg ? d.thigh[k] : d.up[k], f = leg ? d.par[d.foot[k]] : d.fo[k], h = leg ? d.foot[k] : d.hand[k];
            if (u >= 0 && f >= 0 && h >= 0 && u < kEdB && f < kEdB) {
                float upM[3]; const float up[3] = { 0, 1, 0 }; DirToModel(X, up, upM); VNorm(upM);
                float Tm[3]; ToModel(X, w.grabT, Tm); float Re[4], Rs[4]; IK2(Q, P, d, u, f, h, Tm, upM, Re, Rs);
                float Ou[4] = { 0, 0, 0, 1 }, Of[4] = { 0, 0, 0, 1 };
                for (int q = 0; q < w.nPose; q++) { if (w.pose[q].bone == u) EulQ(w.pose[q].e, Ou); if (w.pose[q].bone == f) EulQ(w.pose[q].e, Of); }
                // aplicado sobre la pose: hombro Rs * Ou; codo Rs * Re * Of * Rs^-1 (PoseApply gira primero el hombro y despues el codo)
                float nu[4], t1[4], t2[4], nf[4]; const float Rsi[4] = { -Rs[0], -Rs[1], -Rs[2], Rs[3] };
                QMul(Rs, Ou, nu); QMul(Rs, Re, t1); QMul(t1, Of, t2); QMul(t2, Rsi, nf);
                wb[0] = u; QEul(nu, we[0]); wb[1] = f; QEul(nf, we[1]);
            }
        }
        n = std::min(d.n, 32);
        for (int i = 0; i < n; i++) { ToWorld(X, P + i * 4, bw[i]); par[i] = d.par[i]; lab[i] = d.roleOk ? d.lab[i] : 0; }
        for (int r2 = 0; r2 < 3; r2++) { float v[3] = { X.A[r2 * 3], X.A[r2 * 3 + 1], X.A[r2 * 3 + 2] }; VNorm(v); memcpy(ax[r2], v, 12); }
        sc = p.sn[0].ok ? p.sn[0].sc : 1.0f; ok = true;
    } __except (1) { return; }
    for (int q = 0; q < 2; q++) if (wb[q] >= 0) EdPutGame(slot, tNow, wb[q], we[q]);
    if (ok) StoreBones(p, bw, n, par, lab, ax, sc);
}
// (17/11 20:45, Agustin: "la animacion no vuelve hacia atras", "no respetan sus posiciones") vf4 solo va para adelante y el recorrido
// de la raiz se va sumando al lugar del jugador: al mover el clip con FUN_141afa7b0 el lugar queda donde estaba. En los clips propios
// (que caminan) se vuelve la raiz al lugar puesto, el clip a 0 y se repasa con vf4 hasta el segundo pedido (como si hubiera corrido).
static void Replay(Pj& p, void* ctl, float t) {
    ((RootPosFn)kRootPos)(ctl, p.lastPos[0], p.lastPos[1], p.lastPos[2], 1);
    ((RootYawFn)kRootYaw)(ctl, p.lastYaw * 0.017453292f, 0);
    ((ClipSeekFn)kClipSeek)(ctl, 0, 0, 0.0f, 1, 1);
    const bool gs = p.scW > 0.0f && fabsf(p.scW - 1.0f) > 1e-4f && GraphScale(ctl, p.scW);   // el mismo tamano que en Update
    ((FdVf4Fn)kFdVf4)(p.fd, 0.0f);
    const float step = std::max(1.0f / 30.0f, t / 120.0f);     // hasta 120 pasos por salto
    for (float tt = step; tt < t - 1e-4f; tt += step) ((FdVf4Fn)kFdVf4)(p.fd, tt);
    if (gs) GraphScale(ctl, 1.0f);
    p.endTicks = 0;
    static int nl = 0; if (nl++ < 6) Log("motor (clip propio): jugador repasado hasta %.2f s desde su lugar (%.2f, %.2f, %.2f)", t, p.lastPos[0], p.lastPos[1], p.lastPos[2]);
}
static void StoreTimes(Pj& p, float len, float cur) { std::lock_guard<std::mutex> lk(s_mx); p.lenT = len; p.curT = cur; }   // fuera del __try (C2712)
int Update(Pj& p, const Pj& w, double now) {
    __try {
        const float* pos = w.pos; const float yaw = w.yaw, scale = w.scale;
        RootMove(p, pos, yaw);
        // reloj propio: velocidad y pausa del Inspector; "desde el principio" vuelve el clip a 0
        const double dt = std::clamp(now - p.tLast, 0.0, 0.1); p.tLast = now;
        void* ctl = nullptr; memcpy(&ctl, p.fd + 0x50, 8);
        const bool walk = OwnTrackOf(p.clipA) != nullptr;     // clip propio: el recorrido se repasa desde el lugar puesto (Replay)
        if (p.syncT >= 0.0f && ctl) {                          // pareja: B al segundo de A
            const float len = ((ClipTimeFn)kClipLen)(ctl, 0, 1); const float t = std::clamp(p.syncT, 0.0f, std::max(0.0f, len - 0.001f));
            p.syncT = -1.0f; p.restartA = w.restart; p.seekA = w.seekN; p.back = false;
            if (walk) Replay(p, ctl, t); else ((ClipSeekFn)kClipSeek)(ctl, 0, 0, t, 1, 1);
            p.tPlay = t; p.endTicks = 0; }
        else if (w.restart != p.restartA) { p.restartA = w.restart; p.tPlay = 0.0; p.endTicks = 0; if (ctl) { if (walk) Replay(p, ctl, 0.0f); else ((ClipSeekFn)kClipSeek)(ctl, 0, 0, 0.0f, 1, 1); } }
        else if (w.seekN != p.seekA && ctl) {      // (06:52) linea de tiempo: vf4 lleva el clip al tiempo que recibe (solo para adelante), asi que
            p.seekA = w.seekN;                     // para ir a cualquier punto se mueve el clip (FUN_141afa7b0) y el reloj al mismo segundo
            const float len = ((ClipTimeFn)kClipLen)(ctl, 0, 1); const float t = std::clamp(w.seek, 0.0f, std::max(0.0f, len - 0.001f));
            if (walk) Replay(p, ctl, t); else ((ClipSeekFn)kClipSeek)(ctl, 0, 0, t, 1, 1);
            p.tPlay = t; p.endTicks = 0; }
        else if (!w.paused) {
            const double st = dt * (double)std::clamp(w.speed, 0.0f, 4.0f); const double L = (double)p.lenT;
            // (17/11 18:01, Agustin: "un menu mas accesible con mas opciones") modo de reproduccion. vf4 solo va para adelante: para ir
            // para atras se mueve el clip a ese segundo (FUN_141afa7b0) antes de cada vf4.
            if (w.mode == 2 && L > 0.2) {
                if (!p.back) { p.tPlay += st; if (p.tPlay >= L - 0.002) { p.tPlay = L - 0.002; p.back = true; } }
                else { p.tPlay -= st; if (p.tPlay <= 0.0) { p.tPlay = 0.0; p.back = false; }
                    if (ctl) { if (walk) Replay(p, ctl, (float)p.tPlay); else ((ClipSeekFn)kClipSeek)(ctl, 0, 0, (float)p.tPlay, 1, 1); } }
            } else { p.back = false; p.tPlay += st; if (w.mode == 1 && L > 0.2 && p.tPlay > L - 0.002) p.tPlay = L - 0.002; }
        }
        bool gs = false; if (ctl && p.scW > 0.0f && fabsf(p.scW - 1.0f) > 1e-4f) gs = GraphScale(ctl, p.scW);      // (17:54) tamano en el grafo, solo durante vf4
        { static int nl = 0; if (gs && nl++ < 3) Log("motor (jugadores propios): tamano %.3f en el grafo de la animacion", p.scW); }
        ((FdVf4Fn)kFdVf4)(p.fd, (float)p.tPlay);
        if (gs) GraphScale(ctl, 1.0f);
        memcpy(&ctl, p.fd + 0x50, 8);
        if (ctl) PoseApply(p, ctl, w);
        if (ctl) ContactIK(p, ctl, (float)dt);         // (22:55) contactos de la pareja de clips propios
        if (ctl) EditPass(p, ctl, w);                   // (23:08) editor: agarrar mano / pie y lugares de los huesos para la vista
        if (ctl && !w.paused && w.mode == 0) {      // si el clip no da la vuelta solo (como la banca): 6 cuadros quieto en el final -> al principio
            const float len = ((ClipTimeFn)kClipLen)(ctl, 0, 1), cur = ((ClipTimeFn)kClipTime)(ctl, 0, 1);
            if (len > 0.2f && cur >= len - 0.003f) { if (++p.endTicks >= 6) { if (walk) { Replay(p, ctl, 0.0f); p.tPlay = 0.0; } else ((ClipSeekFn)kClipSeek)(ctl, 0, 0, 0.0f, 1, 1); p.endTicks = 0; } } else p.endTicks = 0;
        }
        if (ctl) StoreTimes(p, ((ClipTimeFn)kClipLen)(ctl, 0, 1), ((ClipTimeFn)kClipTime)(ctl, 0, 1));
        RootMainScale(p, scale);
        // (23:20 Agustin: "asegurate que el cursor siempre este abajo del jugador en las animaciones") el lugar de los iconos del piso es
        // la cadera real en el mundo (x / z) a la altura de la raiz: los clips que caminan llevan el cuerpo y la raiz puede quedar atras
        { float rw[3]; if (ctl && ReadRootPos(ctl, rw)) { for (int b = 0; b < p.bwN && b < 32; b++) if (p.blab[b] == 2) { rw[0] = p.bw[b][0]; rw[2] = p.bw[b][2]; break; } StoreWorld(p, rw); } }
        ((FdPushFn)kFdPush)(p.fd);
        void* h = nullptr; memcpy(&h, p.fd + 0x48, 8);
        void* face = FaceEnsure(p);
        const OwnTrack* own = w.face < 0 ? OwnTrackOf(p.clipA) : nullptr;     // clip propio: su cara y sus manos (si no hay gesto elegido)
        if (h && face) {
            if (own) OwnFace(p, h, face, own, ctl ? ((ClipTimeFn)kClipTime)(ctl, 0, 1) : (float)p.tPlay, w.blink);
            else if (w.face >= 0 && w.face < kFaceN) FaceGesture(p, h, face, w.face, w.faceW, w.blink); else ((FaceNeutralFn)kFaceNeutral)(h, face, 0);
        }
        if (h) { const uint8_t vis = *((const uint8_t*)h + 0x5a); if (p.hidden ? vis != 0 : vis == 0) ((HumanShowFn)kHumanShow)(h, p.hidden ? 0 : 1); }   // (ojo del panel Escena)
        return 1;
    } __except (1) { s_dead.store(true); return -1; }
}

bool ReadLid(uintptr_t impl, uint8_t* out) { __try { memcpy(out, (const void*)(impl + 0x10), 0xa0); return true; } __except (1) { return false; } }
void DestroyAll(const char* why) {
    int n = 0;
    for (int i = 0; i < kMax; i++) { Pj& p = s_pj[i]; if (p.fd && Destroy(p)) n++; }
    { std::lock_guard<std::mutex> lk(s_mx); for (int i = 0; i < kMax; i++) if (s_pj[i].state) s_pj[i].state = 0; }
    s_sel.store(-1);
    for (int i = 0; i < kClipMax; i++) if (s_ce[i].st) ClipDrop(s_ce[i]);
    s_pumpSaid = false; s_firstMade = false;
    if (n) Log("motor (jugadores propios): %d sacados (%s)", n, why);
}

} // namespace

// hilo del juego: tick del visor (motorjug.cpp hk_Tick), siempre (con el Motor abierto o no)
// (17/11 18:20, Agustin: "permitir cargar luces") JUEGO DE LUCES del juego en el Motor, en el hilo del juego (aca, como los jugadores).
// Leido en el C: draw::Light (0x58 B, ctor 0x141edb8f0); 0x141edbc20(arma, n) arma la ruta cpk_dat/common/render/model/light/
// {Default, EditFace, EditSpike, MatchMenu}.xml con el string del juego; 0x141edbcf0(luz, arma) la carga (y libera el string);
// 0x141edc0b0(luz, 1) la pone activa cuando esta lista (pila del sistema 0x143802cc0: tapa a la de abajo; con 0 la saca y vuelve la de
// abajo); 0x141edc000(luz) la descarga. Estado: 0 nada, 1 cargando, 2 activa, 3 no cargo.
const uintptr_t kLtCtor = 0x141edb8f0ull, kLtPath = 0x141edbc20ull, kLtLoad = 0x141edbcf0ull, kLtAct = 0x141edc0b0ull, kLtUnload = 0x141edc000ull;
typedef void*(__fastcall* LtCtorFn)(void*); typedef void*(__fastcall* LtPathFn)(void*, int, uint64_t, uint64_t); typedef void(__fastcall* LtLoadFn)(void*, void*);
typedef void(__fastcall* LtActFn)(void*, char); typedef void(__fastcall* LtUnloadFn)(void*);
static std::atomic<int> s_ltWant{0}, s_ltState{0}; static int s_ltHave = 0; static uint8_t* s_lt = nullptr; static bool s_ltPushed = false; static double s_ltT = 0.0;
void MotorLightSetReq(int which) { s_ltWant.store(std::clamp(which, 0, 4)); }
int MotorLightSetGet(int* state) { if (state) *state = s_ltState.load(); return s_ltWant.load(); }
static void LightSetTick(bool on) {
    const int w = on ? s_ltWant.load() : 0;
    if (w == s_ltHave && (w == 0 || s_ltPushed || s_ltState.load() == 3)) return;
    uint32_t saved = 0, id = 0x19, heap = 0x19; bool pushed = false;
    __try {
        ((HeapPushFn)kHeapPush)(&saved, &id); pushed = true;
        if (w != s_ltHave) {
            if (s_lt) { if (s_ltPushed) ((LtActFn)kLtAct)(s_lt, 0); ((LtUnloadFn)kLtUnload)(s_lt); if (s_ltHave) Log("motor (luces): juego de luces %d sacado%s", s_ltHave, s_ltPushed ? " (vuelve el de antes)" : ""); }
            s_ltPushed = false; s_ltHave = w; s_ltState.store(w ? 1 : 0);
            if (w > 0) {
                if (!s_lt) { void* mem = ((AllocFn)kAlloc)(&heap, 0x58, 0x10); s_lt = mem ? (uint8_t*)((LtCtorFn)kLtCtor)(mem) : nullptr; }
                if (!s_lt) { s_ltState.store(3); Log("motor (luces): no se pudo crear el draw::Light"); }
                else { alignas(16) uint8_t arg[0x40] = {}; ((LtPathFn)kLtPath)(arg, w - 1, 0, 0); ((LtLoadFn)kLtLoad)(s_lt, arg); s_ltT = Now();
                    Log("motor (luces): cargando juego de luces %d (id %d)", w, *(int*)(s_lt + 0xc)); if (*(int*)(s_lt + 0xc) == -1) s_ltState.store(3); }
            }
        }
        if (w > 0 && s_lt && !s_ltPushed && s_ltState.load() == 1) {
            ((LtActFn)kLtAct)(s_lt, 1);
            if (s_lt[0x10]) { s_ltPushed = true; s_ltState.store(2); Log("motor (luces): juego de luces %d activo (%.1f s)", w, Now() - s_ltT); }
            else if (Now() - s_ltT > 20.0) { s_ltState.store(3); Log("motor (luces): el juego de luces %d no quedo listo en 20 s", w); }
        }
    } __except (1) { s_ltState.store(3); s_ltWant.store(0); s_ltHave = 0; s_ltPushed = false; Log("motor (luces): error dentro del juego al cambiar el juego de luces: se deja"); }
    if (pushed) { __try { ((HeapPopFn)kHeapPop)(&saved); } __except (1) {} }
}
void MotorPjTick(bool on, uintptr_t impl) {
    LightSetTick(on);
    const double now = Now(); s_lastTick.store(now);
    static bool s_had = false;
    if (!on || s_dead.load()) {
        bool any = false; for (int i = 0; i < kMax; i++) if (s_pj[i].fd) any = true;
        if (any || s_had) DestroyAll(on ? "error dentro del juego" : "se cerro el Motor");
        s_had = false; return;
    }
    // copia de lo que pidio la interfaz
    static Pj want[kMax];
    { std::lock_guard<std::mutex> lk(s_mx); for (int i = 0; i < kMax; i++) { const Pj& q = s_pj[i]; Pj& w = want[i];
        w.state = q.state; w.kind = q.kind; w.id = q.id; memcpy(w.name, q.name, sizeof w.name); memcpy(w.pos, q.pos, 12); w.yaw = q.yaw; w.scale = q.scale;
        w.clip = q.clip; w.speed = q.speed; w.paused = q.paused; w.restart = q.restart; w.seek = q.seek; w.seekN = q.seekN; w.face = q.face; w.faceW = q.faceW; w.blink = q.blink; w.hidden = q.hidden; w.mode = q.mode; EdEvalLocked(i, (float)q.tPlay, w.pose, &w.nPose); w.boneQ = q.boneQ; w.grab = q.grab; memcpy(w.grabT, q.grabT, 12); } }
    // (17/11 20:45, Agustin: "no respetan sus posiciones", "a la hora de chocar las manos las alturas no coinciden") pareja de clips
    // propios (kOwnPairs): con A en un jugador y B en otro, B se pone a la distancia del clip delante de A, mirandolo, y los dos van al
    // mismo segundo (al formarse la pareja arrancan juntos desde 0; despues B sigue el reloj, la velocidad, la pausa y el modo de A).
    {
        static int s_pa = -1, s_pb = -1; int ia = -1, ib = -1; const OwnPair* pr = nullptr;
        for (const OwnPair& q : kOwnPairs) {
            ia = ib = -1;
            for (int i = 0; i < kMax; i++) {
                if (want[i].state != 3 || !s_pj[i].fd || want[i].clip < 0 || want[i].clip >= kClipN) continue;
                const char* f = kClipRows[want[i].clip].file;
                if (ia < 0 && !strcmp(f, q.a)) ia = i; else if (ib < 0 && !strcmp(f, q.b)) ib = i;
            }
            if (ia >= 0 && ib >= 0) { pr = &q; break; }
        }
        for (int i = 0; i < kMax; i++) { s_pj[i].pairB = false; s_pj[i].partner = -1; }
        if (pr) {
            Pj& A = s_pj[ia]; Pj& B = s_pj[ib];
            const float sc = 0.5f * ((A.scW > 0.0f ? A.scW : 1.0f) + (B.scW > 0.0f ? B.scW : 1.0f));
            const float y = want[ia].yaw * 0.017453292f, d = pr->dist * sc;     // adelante = (sen giro, 0, cos giro), como RootYaw
            const float bp[3] = { want[ia].pos[0] + d * sinf(y), want[ia].pos[1], want[ia].pos[2] + d * cosf(y) };
            float by = want[ia].yaw + 180.0f; if (by >= 360.0f) by -= 360.0f;
            memcpy(want[ib].pos, bp, 12); want[ib].yaw = by;
            want[ib].speed = want[ia].speed; want[ib].paused = want[ia].paused; want[ib].mode = want[ia].mode;
            { std::lock_guard<std::mutex> lk(s_mx); memcpy(s_pj[ib].pos, bp, 12); s_pj[ib].yaw = by; s_pj[ib].speed = want[ia].speed; s_pj[ib].paused = want[ia].paused; s_pj[ib].mode = want[ia].mode; }
            B.pairB = true; A.partner = ib; B.partner = ia;
            if (A.clipA == want[ia].clip && B.clipA == want[ib].clip) {
                if (ia != s_pa || ib != s_pb) {
                    A.syncT = 0.0f; B.syncT = 0.0f; s_pa = ia; s_pb = ib;
                    Log("motor (clip propio): pareja %s (jugador %d) y %s (jugador %d) a %.2f m, desde 0 juntos", pr->a, ia + 1, pr->b, ib + 1, d);
                } else if (A.syncT < 0.0f && fabs(B.tPlay - A.tPlay) > 0.05) B.syncT = (float)A.tPlay;
            }
        } else s_pa = s_pb = -1;
    }
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
        const int wclip = (want[i].clip >= 0 && want[i].clip < kClipN) ? want[i].clip : kIdleClip;
        if (st == 1) {            // crear
            const int c = ClipReady(wclip, now); if (c == 0) { any = true; continue; }
            void* res = FixdemoRes();
            if (c < 0 || !res) { if (!res) Msg("no esta el recurso de clips \"fixdemo\" del juego"); std::lock_guard<std::mutex> lk(s_mx); p.state = 4; continue; }
            uint8_t vl[0xa0] = {}; bool vok = false;
            if (want[i].kind == 2) vok = impl && ReadLid(impl, vl);
            if (want[i].kind == 2 && !vok) { Msg("no pude leer el jugador del visor del Editar"); std::lock_guard<std::mutex> lk(s_mx); p.state = 4; continue; }
            p.kind = want[i].kind; p.id = want[i].id; memcpy(p.name, want[i].name, sizeof p.name);
            p.seed = (uint32_t)(i + 1) * 2654435761u; p.clipA = -2; p.clipFail = -2; p.restartA = want[i].restart;
            if (!s_firstMade) { s_firstMade = true; p.reload = true; p.reloadAt = 0; }
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
            if (ClipReady(wclip, now) != 1 || !res || !Start(p, res, wclip, want[i].pos, want[i].yaw)) { Destroy(p); Msg("el jugador %d cargo pero no se le pudo poner el clip", i + 1); std::lock_guard<std::mutex> lk(s_mx); p.state = 4; continue; }
            void* h = nullptr; memcpy(&h, p.fd + 0x48, 8); float hc = 0.0f; if (h) memcpy(&hc, (const uint8_t*)h + 0x10, 4);
            Log("motor (jugadores propios): jugador %d listo en %.1f s (modelo %p, estatura del modelo %.1f cm)", i + 1, now - p.tLoad, h, hc);
            { std::lock_guard<std::mutex> lk(s_mx); p.state = 3; snprintf(s_msg, sizeof s_msg, "%s", ""); }
            if (p.reload) p.reloadAt = now + 1.5;
            any = true; continue;
        }
        if (st == 3 && p.fd && p.reload && p.reloadAt > 0.0 && now >= p.reloadAt) {      // (17:22) el primero se vuelve a crear para que salga vestido
            const bool ok = Destroy(p); p.reload = false; p.reloadAt = 0; p.posCtl = nullptr; p.faceTried = false;
            { std::lock_guard<std::mutex> lk(s_mx); p.state = ok ? 1 : 4; }
            Log("motor (jugadores propios): jugador %d se vuelve a crear una vez (el primero de la apertura sale sin ropa)%s", i + 1, ok ? "" : ": no se pudo sacar");
            any = true; continue;
        }
        if (st == 3 && p.fd) {
            if (wclip != p.clipA && wclip != p.clipFail) {      // (06:37) otra animacion pedida desde el Inspector
                const int c = ClipReady(wclip, now);
                if (c < 0) p.clipFail = wclip;
                else if (c > 0) { void* res = FixdemoRes();
                    if (!res || !Switch(p, res, wclip)) { p.clipFail = wclip; if (s_dead.load()) { Msg("error al cambiar la animacion del jugador %d: se sacan todos", i + 1); break; } }
                    else { static int nl = 0; if (nl++ < 20) Log("motor (jugadores propios): jugador %d con la animacion %s", i + 1, kClipRows[wclip].file); } }
            }
            if (Update(p, want[i], now) < 0) { Msg("error al mover al jugador %d: se sacan todos", i + 1); break; }
            any = true;
        }
    }
    s_had = any;
}

// interfaz (Present)
static int AddSlot(int kind, uint64_t key, const char* nm, const Pj* like) {
    int slot = -1, used = 0; for (int i = 0; i < kMax; i++) { if (s_pj[i].state == 0 || s_pj[i].state == 4) { if (slot < 0) slot = i; } else used++; }
    if (slot < 0) { snprintf(s_msg, sizeof s_msg, "Ya hay %d jugadores (el maximo)", kMax); return -1; }
    Pj& p = s_pj[slot]; p.state = 1; p.hidden = false; p.kind = kind; p.id = key; memcpy(p.name, nm, sizeof p.name);
    p.pos[0] = 1.2f * (float)(used + 1); p.pos[1] = 0.0f; p.pos[2] = 0.0f; p.yaw = 0.0f; p.scale = 1.0f;
    p.clip = -1; p.speed = 1.0f; p.paused = false; p.face = -1; p.faceW = 1.0f; p.blink = true; p.hcm = 0; p.wkg = 0;
    if (like) { p.hcm = like->hcm; p.wkg = like->wkg; memcpy(p.pos, like->pos, 12); p.pos[0] += 1.0f; p.yaw = like->yaw; p.scale = like->scale; p.clip = like->clip; p.speed = like->speed; p.paused = like->paused; p.face = like->face; p.faceW = like->faceW; p.blink = like->blink; }
    snprintf(s_msg, sizeof s_msg, "Cargando %s...", nm);
    s_sel.store(slot);
    return slot;
}
int MotorPjAdd(int kind, uint32_t id) {
    char nm[48] = {}; uint64_t key = 0; uint8_t hcm = 0, wkg = 0;
    if (kind == 0) {
        const uintptr_t r = DbFind(id, &key);
        if (!r) { Msg("no encontre el id %u en la base de datos del juego", id); return -1; }
        DbName(r, nm, sizeof nm); if (!nm[0]) snprintf(nm, sizeof nm, "Jugador %u", id);
        DbBody(r, &hcm, &wkg);
    } else if (kind == 1) { key = 0xffffull; snprintf(nm, sizeof nm, "Jugador base"); }
    else snprintf(nm, sizeof nm, "Igual al del Editar");
    std::lock_guard<std::mutex> lk(s_mx);
    const int slot = AddSlot(kind, key, nm, nullptr); if (slot < 0) return -1;
    s_pj[slot].hcm = hcm; s_pj[slot].wkg = wkg;
    Log("motor (jugadores propios): pedido %s (id %u, clave 0x%llx) en el lugar %d; estatura %u cm, peso %u kg en la base", nm, id, (unsigned long long)key, slot + 1, hcm, wkg);
    return slot;
}
void MotorPjRemove(int i) { if (i < 0 || i >= kMax) return; std::lock_guard<std::mutex> lk(s_mx); if (s_pj[i].state == 1 || s_pj[i].state == 4) s_pj[i].state = 0; else if (s_pj[i].state) s_pj[i].state = 5; if (s_sel.load() == i) s_sel.store(-1); }
void MotorPjShow(int i, bool on) { if (i < 0 || i >= kMax) return; std::lock_guard<std::mutex> lk(s_mx); s_pj[i].hidden = !on; Log("motor (jugadores propios): jugador %d %s", i + 1, on ? "visible" : "oculto"); }
void MotorPjSelect(int i) { s_sel.store(i >= 0 && i < kMax ? i : -1); }
int MotorPjSel() { const int i = s_sel.load(); if (i < 0) return -1; std::lock_guard<std::mutex> lk(s_mx); const int st = s_pj[i].state; return (st >= 1 && st <= 3) ? i : -1; }
bool MotorPjGet(int i, float* pos, float* yaw, float* scale) {
    if (i < 0 || i >= kMax) return false; std::lock_guard<std::mutex> lk(s_mx); const Pj& p = s_pj[i]; if (p.state < 1 || p.state > 3) return false;
    memcpy(pos, p.pos, 12); *yaw = p.yaw; *scale = p.scale; return true;
}
bool MotorPjWorld(int i, float* w) {
    if (i < 0 || i >= kMax) return false; std::lock_guard<std::mutex> lk(s_mx); const Pj& p = s_pj[i]; if (p.state != 3 || !p.hasW) return false;
    memcpy(w, p.wpos, 12); return true;
}
void MotorPjSet(int i, const float* pos, float yaw, float scale) {
    if (i < 0 || i >= kMax) return; std::lock_guard<std::mutex> lk(s_mx); Pj& p = s_pj[i]; if (p.state < 1 || p.state > 3) return;
    for (int k = 0; k < 3; k++) p.pos[k] = std::clamp(pos[k], -200.0f, 200.0f); p.yaw = std::clamp(yaw, -720.0f, 720.0f); p.scale = std::clamp(scale, 0.05f, 10.0f);
}
// (06:36) duplicar: el mismo jugador (mismo tipo e id) un metro al costado, con su giro, tamano, animacion y gesto
int MotorPjDup(int i) {
    if (i < 0 || i >= kMax) return -1; std::lock_guard<std::mutex> lk(s_mx); const Pj src = s_pj[i]; if (src.state < 1 || src.state > 3) return -1;
    const int slot = AddSlot(src.kind, src.id, src.name, &src);
    if (slot >= 0) Log("motor (jugadores propios): jugador %d duplicado en el lugar %d", i + 1, slot + 1);
    return slot;
}
// (17/11 15:54 PC, cierre del juego: PES2021.exe+0x23c2dfe escribe en 0x14 justo despues de poner dml_prop_circleflag_uefa_cl_01_anm.gani
// a un jugador) los clips del grupo "prop" son de OBJETOS (banderines, copas, compu): no van en un jugador.
static bool ClipIsProp(int c) { if (c < 0 || c >= kClipN) return false; const int g = kClipRows[c].group; return g >= 0 && g < (int)(sizeof kClipGroupKey / sizeof kClipGroupKey[0]) && strcmp(kClipGroupKey[g], "prop") == 0; }
bool MotorClipIsProp(int c) { return ClipIsProp(c); }
void MotorPjAnimSet(int i, int clip, float speed, bool paused) {
    if (ClipIsProp(clip)) { Log("motor (jugadores propios): %s es de un objeto, no de un jugador: no se pone", kClipRows[clip].file); return; }
    std::lock_guard<std::mutex> lk(s_mx);
    for (int q = (i < 0 ? 0 : i); q < (i < 0 ? kMax : std::min(i + 1, kMax)); q++) { Pj& p = s_pj[q]; if (p.state < 1 || p.state > 3) continue;
        p.clip = clip >= -1 && clip < kClipN ? clip : -1; p.speed = std::clamp(speed, 0.0f, 4.0f); p.paused = paused; }
}
void MotorPjModeSet(int i, int mode) { std::lock_guard<std::mutex> lk(s_mx); for (int q = (i < 0 ? 0 : i); q < (i < 0 ? kMax : std::min(i + 1, kMax)); q++) s_pj[q].mode = std::clamp(mode, 0, 2); }
void MotorPjSeek(int i, float t) { if (i < 0 || i >= kMax) return; std::lock_guard<std::mutex> lk(s_mx); s_pj[i].seek = t; s_pj[i].seekN++; }
void MotorPjRestart(int i) { std::lock_guard<std::mutex> lk(s_mx); for (int q = (i < 0 ? 0 : i); q < (i < 0 ? kMax : std::min(i + 1, kMax)); q++) s_pj[q].restart++; }
void MotorPjFaceSet(int i, int face, float w, bool blink) {
    std::lock_guard<std::mutex> lk(s_mx);
    for (int q = (i < 0 ? 0 : i); q < (i < 0 ? kMax : std::min(i + 1, kMax)); q++) { Pj& p = s_pj[q]; if (p.state < 1 || p.state > 3) continue;
        p.face = face >= -1 && face < kFaceN ? face : -1; p.faceW = std::clamp(w, 0.0f, 1.0f); p.blink = blink; }
}
int MotorClipCount() { return kClipN; }
const char* MotorClipFile(int i) { return i >= 0 && i < kClipN ? kClipRows[i].file : ""; }
int MotorClipGroup(int i) { return i >= 0 && i < kClipN ? kClipRows[i].group : -1; }
int MotorClipGroupCount() { return kGroupN; }
const char* MotorClipGroupKey(int g) { return g >= 0 && g < kGroupN ? kClipGroupKey[g] : ""; }
int MotorClipIdle() { return kIdleClip; }
int MotorFaceCount() { return kFaceN; }
const char* MotorFaceName(int i) { return i >= 0 && i < kFaceN ? kFaceBase[i] : ""; }
void MotorPjPoseSet(int i, int bone, const float* e) { if (i < 0 || i >= kMax) return; std::lock_guard<std::mutex> lk(s_mx); Pj& p = s_pj[i];
    if (bone < 0) { p.nPose = 0; return; }
    for (int k = 0; k < p.nPose; k++) if (p.pose[k].bone == bone) { if (e) memcpy(p.pose[k].e, e, 12); else { for (int q = k; q + 1 < p.nPose; q++) p.pose[q] = p.pose[q + 1]; p.nPose--; } return; }
    if (!e || p.nPose >= kPoseMax) return;
    int at = p.nPose; while (at > 0 && p.pose[at - 1].bone > bone) { p.pose[at] = p.pose[at - 1]; at--; }      // de padre a hijo (los padres tienen numero menor)
    p.pose[at].bone = bone; memcpy(p.pose[at].e, e, 12); p.nPose++; }
bool MotorPjPoseGet(int i, int bone, float* e, int* nBones, int* nPosed, char* name, size_t nn, float* world) {
    if (i < 0 || i >= kMax) return false; std::lock_guard<std::mutex> lk(s_mx); Pj& p = s_pj[i]; const SkDesc& d = s_sk[i];
    p.boneQ = bone; if (nBones) *nBones = d.n; if (nPosed) *nPosed = p.nPose;
    if (e) { e[0] = e[1] = e[2] = 0.0f; for (int k = 0; k < p.nPose; k++) if (p.pose[k].bone == bone) memcpy(e, p.pose[k].e, 12); }
    if (name && nn) { const char* s0 = d.hashOk && bone >= 0 && bone < d.n ? SkName(d.hash[bone]) : nullptr; if (s0) snprintf(name, nn, "%s", s0); else snprintf(name, nn, "hueso %d", bone); }
    if (world) memcpy(world, p.boneW, 12);
    return p.boneWOk && bone >= 0 && bone < d.n; }
void MotorPjInfoGet(MotorPjInfo* o) {
    memset(o, 0, sizeof *o); o->sel = s_sel.load();
    const double lt = s_lastTick.load(); o->ticking = lt > 0.0 && Now() - lt < 1.5;
    std::lock_guard<std::mutex> lk(s_mx);
    for (int i = 0; i < kMax; i++) { const Pj& p = s_pj[i]; o->st[i] = p.state; o->hidden[i] = p.hidden; o->id[i] = p.id; o->kind[i] = p.kind; memcpy(o->name[i], p.name, sizeof o->name[i]);
        o->curT[i] = p.curT; o->lenT[i] = p.lenT;
        o->clip[i] = p.clip; o->speed[i] = p.speed; o->paused[i] = p.paused; o->face[i] = p.face; o->faceW[i] = p.faceW; o->blink[i] = p.blink; o->mode[i] = p.mode;
        o->clipOn[i] = p.fd && p.clipA == ((p.clip >= 0 && p.clip < kClipN) ? p.clip : kIdleClip); o->clipBad[i] = p.clipFail >= 0 && p.clipFail == ((p.clip >= 0 && p.clip < kClipN) ? p.clip : kIdleClip); }
    memcpy(o->msg, s_msg, sizeof o->msg); o->dead = s_dead.load();
}


// ---- (17/11 23:08) EDITOR: lo que usa la interfaz (motorgizmo.cpp)
const char* MotorBoneLabel(int lab) {
    static const char* const k[21] = { "", "Ra\xC3\xADz", "Cadera", "Columna", "Pecho", "Cuello", "Cabeza", "Clav\xC3\xAD" "cula izq.", "Brazo izq.", "Antebrazo izq.", "Mano izq.",
        "Clav\xC3\xAD" "cula der.", "Brazo der.", "Antebrazo der.", "Mano der.", "Muslo izq.", "Pierna izq.", "Pie izq.", "Muslo der.", "Pierna der.", "Pie der." };
    return lab >= 0 && lab < 21 ? k[lab] : ""; }
bool MotorEdInfoGet(int i, MotorEdInfo* o) {
    memset(o, 0, sizeof *o); if (i < 0 || i >= kMax) return false; std::lock_guard<std::mutex> lk(s_mx); const Pj& p = s_pj[i]; if (p.state != 3) return false;
    o->n = std::min(p.bwN, 32); memcpy(o->pos, p.bw, sizeof o->pos); memcpy(o->par, p.bpar, sizeof o->par); memcpy(o->lab, p.blab, sizeof o->lab); memcpy(o->ax, p.bax, sizeof o->ax);
    o->sc = p.bsc; o->curT = p.curT; o->lenT = p.lenT; const EdLayer& L = s_ed[i]; o->nKeys = L.n; for (int k = 0; k < L.n; k++) o->keyT[k] = L.k[k].t;
    o->interp = s_edInterp; o->autoKey = s_edAuto; o->grab = p.grab; return o->n > 0; }
bool MotorEdGet(int i, int bone, float* e) {
    e[0] = e[1] = e[2] = 0.0f; if (i < 0 || i >= kMax || bone < 0 || bone >= kEdB) return false; std::lock_guard<std::mutex> lk(s_mx);
    Pj::PoseK v[kPoseMax]; int n = 0; EdEvalLocked(i, s_pj[i].curT, v, &n);
    for (int k = 0; k < n; k++) if (v[k].bone == bone) { memcpy(e, v[k].e, 12); return true; } return false; }
// e = giro total que se quiere ver en el hueso (claves + pose fija): a la clave va lo que falta sobre la pose fija
void MotorEdSet(int i, int bone, const float* e) {
    if (i < 0 || i >= kMax || bone < 0 || bone >= kEdB) return; std::lock_guard<std::mutex> lk(s_mx); Pj& p = s_pj[i];
    if (!e) { EdPutLocked(i, p.curT, bone, nullptr); return; }
    float v[3] = { e[0], e[1], e[2] };
    if (s_edAuto || s_ed[i].n > 0) for (int k = 0; k < p.nPose; k++) if (p.pose[k].bone == bone) for (int c = 0; c < 3; c++) v[c] -= p.pose[k].e[c];
    EdPutLocked(i, p.curT, bone, v); }
void MotorEdGrab(int i, int limb, const float* w) { if (i < 0 || i >= kMax) return; std::lock_guard<std::mutex> lk(s_mx); Pj& p = s_pj[i]; p.grab = (w && limb >= 0 && limb < 4) ? limb : -1; if (w) memcpy(p.grabT, w, 12); }
void MotorEdOpt(int interp, int autoKey) { std::lock_guard<std::mutex> lk(s_mx); if (interp >= 0 && interp <= 2) s_edInterp = interp; if (autoKey >= 0) s_edAuto = autoKey != 0; }
// op: 0 clave aqui con todo, 1 borrar la clave idx, 2 mover la clave idx al segundo t, 3 copiar la clave de aqui, 4 pegar aqui,
// 5 sacar todo (claves y pose fija), 6 espejar lo editado aqui (izquierda <-> derecha), 7 ir a la clave idx (devuelve su segundo)
float MotorEdKey(int i, int op, int idx, float t) {
    if (i < 0 || i >= kMax) return -1.0f; std::lock_guard<std::mutex> lk(s_mx); Pj& p = s_pj[i]; EdLayer& L = s_ed[i]; const float now = p.curT;
    auto all = [&](float at, float (*e)[3], uint32_t* mask) { Pj::PoseK v[kPoseMax]; int n = 0; EdEvalLocked(i, at, v, &n); *mask = 0;
        for (int b = 0; b < kEdB; b++) V3(e[b], 0, 0, 0); for (int k = 0; k < n; k++) { memcpy(e[v[k].bone], v[k].e, 12); *mask |= 1u << v[k].bone; } };
    if (op == 0) { static float e[kEdB][3]; uint32_t m = 0; all(now, e, &m);
        for (int b = 0; b < kEdB && b < p.bwN; b++) if (p.blab[b]) m |= 1u << b;     // los huesos con nombre quedan fijos aca (aunque esten en 0)
        p.nPose = 0; int k = EdKeyAt(L, now); if (k < 0) k = EdKeyNew(L, now); if (k < 0) return -1.0f;
        memcpy(L.k[k].e, e, sizeof e); L.k[k].mask = m; return now; }
    if (op == 1) { if (idx < 0 || idx >= L.n) return -1.0f; for (int q = idx; q + 1 < L.n; q++) L.k[q] = L.k[q + 1]; L.n--; return now; }
    if (op == 2) { if (idx < 0 || idx >= L.n) return -1.0f; EdKey K = L.k[idx]; for (int q = idx; q + 1 < L.n; q++) L.k[q] = L.k[q + 1]; L.n--;
        K.t = std::max(0.0f, t); const int at = EdKeyNew(L, K.t); if (at >= 0) L.k[at] = K; return K.t; }
    if (op == 3) { uint32_t m = 0; all(now, s_edClip.e, &m); s_edClip.mask = m; s_edClipOk = m != 0; return now; }
    if (op == 4) { if (!s_edClipOk) return -1.0f; int k = EdKeyAt(L, now); if (k < 0) k = EdKeyNew(L, now); if (k < 0) return -1.0f;
        for (int b = 0; b < kEdB; b++) if (s_edClip.mask & (1u << b)) memcpy(L.k[k].e[b], s_edClip.e[b], 12); L.k[k].mask |= s_edClip.mask; return now; }
    if (op == 5) { L.n = 0; p.nPose = 0; return now; }
    if (op == 6) { static float e[kEdB][3], o[kEdB][3]; uint32_t m = 0; all(now, e, &m); int twin[kEdB];
        for (int b = 0; b < kEdB; b++) { twin[b] = b; const int lb = b < p.bwN ? p.blab[b] : 0; if (!lb) continue;
            const int want = (lb >= 7 && lb <= 10) ? lb + 4 : (lb >= 11 && lb <= 14) ? lb - 4 : (lb >= 15 && lb <= 17) ? lb + 3 : (lb >= 18 && lb <= 20) ? lb - 3 : lb;
            for (int c = 0; c < kEdB && c < p.bwN; c++) if (p.blab[c] == want) twin[b] = c; }
        uint32_t m2 = 0; for (int b = 0; b < kEdB; b++) { const int s2 = twin[b]; o[b][0] = e[s2][0]; o[b][1] = -e[s2][1]; o[b][2] = -e[s2][2]; if (m & (1u << s2)) m2 |= 1u << b; }
        p.nPose = 0; int k = EdKeyAt(L, now); if (k < 0) k = EdKeyNew(L, now); if (k < 0) return -1.0f;
        for (int b = 0; b < kEdB; b++) if (m2 & (1u << b)) memcpy(L.k[k].e[b], o[b], 12); L.k[k].mask |= m2; return now; }
    if (op == 7) return idx >= 0 && idx < L.n ? L.k[idx].t : -1.0f;
    return -1.0f; }
// archivo de trabajo del editor (texto): "zfx_editor 1", "clip <archivo>", "suave <0-2>", y por clave "clave <segundo>" + "hueso <i> <x> <y> <z>"
bool MotorEdSave(int i, const wchar_t* path) {
    if (i < 0 || i >= kMax) return false; FILE* fp = nullptr; if (_wfopen_s(&fp, path, L"wb") || !fp) return false;
    { std::lock_guard<std::mutex> lk(s_mx); const Pj& p = s_pj[i]; const EdLayer& L = s_ed[i];
      fprintf(fp, "zfx_editor 1\nclip %s\nsuave %d\n", (p.clip >= 0 && p.clip < kClipN) ? kClipRows[p.clip].file : "-", s_edInterp);
      for (int k = 0; k < L.n; k++) { fprintf(fp, "clave %.4f\n", L.k[k].t); for (int b = 0; b < kEdB; b++) if (L.k[k].mask & (1u << b)) fprintf(fp, "hueso %d %.3f %.3f %.3f\n", b, L.k[k].e[b][0], L.k[k].e[b][1], L.k[k].e[b][2]); }
      for (int k = 0; k < p.nPose; k++) fprintf(fp, "fijo %d %.3f %.3f %.3f\n", p.pose[k].bone, p.pose[k].e[0], p.pose[k].e[1], p.pose[k].e[2]); }
    fclose(fp); return true; }
bool MotorEdLoad(int i, const wchar_t* path, char* clipOut, size_t cn) {
    if (i < 0 || i >= kMax) return false; FILE* fp = nullptr; if (_wfopen_s(&fp, path, L"rb") || !fp) return false;
    static EdLayer L; L.n = 0; Pj::PoseK fxp[kPoseMax]; int nf = 0; int interp = -1; char ln[256]; bool ok = false; if (clipOut && cn) clipOut[0] = 0;
    while (fgets(ln, sizeof ln, fp)) { float t, x, y, z; int b; char nm[200];
        if (!strncmp(ln, "zfx_editor", 10)) ok = true;
        else if (sscanf_s(ln, "clip %199s", nm, (unsigned)sizeof nm) == 1) { if (clipOut && cn) snprintf(clipOut, cn, "%s", nm); }
        else if (sscanf_s(ln, "suave %d", &b) == 1) interp = b;
        else if (sscanf_s(ln, "clave %f", &t) == 1) { if (L.n < kEdK) { memset(&L.k[L.n], 0, sizeof L.k[L.n]); L.k[L.n].t = t; L.n++; } }
        else if (sscanf_s(ln, "hueso %d %f %f %f", &b, &x, &y, &z) == 4) { if (L.n > 0 && b >= 0 && b < kEdB) { EdKey& K = L.k[L.n - 1]; K.e[b][0] = x; K.e[b][1] = y; K.e[b][2] = z; K.mask |= 1u << b; } }
        else if (sscanf_s(ln, "fijo %d %f %f %f", &b, &x, &y, &z) == 4) { if (nf < kPoseMax && b >= 0 && b < kEdB) { fxp[nf].bone = b; fxp[nf].e[0] = x; fxp[nf].e[1] = y; fxp[nf].e[2] = z; nf++; } } }
    fclose(fp); if (!ok) return false;
    std::lock_guard<std::mutex> lk(s_mx); s_ed[i] = L; memcpy(s_pj[i].pose, fxp, sizeof(Pj::PoseK) * nf); s_pj[i].nPose = nf; if (interp >= 0 && interp <= 2) s_edInterp = interp; return true; }
} // namespace fx
