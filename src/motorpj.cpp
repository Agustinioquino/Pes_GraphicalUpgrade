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
constexpr int kPoseMax = 24;
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
    uint8_t* fd = nullptr; double tLoad = 0, tLast = 0, tPlay = 0; bool faceTried = false;
    float scW = 1.0f; void* scCtl = nullptr; float scBase = 0.0f;   // (17/11 tarde) tamano por root_main como FUN_141f87eb0
    float lastPos[3] = {}, lastYaw = 0.0f; void* posCtl = nullptr; int nMoved = 0;
    int clipA = -2, clipFail = -2, restartA = 0, seekA = 0, endTicks = 0; uint32_t seed = 0;   // clipA: el clip puesto
    uint8_t lid[0xa0] = {};
    bool reload = false; double reloadAt = 0;   // (17/11 17:22) el primero de cada apertura del Motor se vuelve a crear una vez (ropa)
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
                uint8_t pl[0xF0] = {}; memcpy(pl, &p.id, 8); pl[8] = 1; pl[0x39] = 180; pl[0x3a] = 75;
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
struct SkDesc { uintptr_t desc = 0; int n = 0; int par[400]; bool parOk = false; uint32_t hash[400]; bool hashOk = false; };
static SkDesc s_sk[kMax];
static void SkScan(int slot, void* ctl) {
    SkDesc& d = s_sk[slot];
    __try {
        const uintptr_t a = *(const uintptr_t*)((uintptr_t)ctl + 0x100); if (!PtrOk(a)) return;
        const uintptr_t desc = *(const uintptr_t*)(a + 0x20); if (!PtrOk(desc)) return;
        const int n = *(const int*)(desc + 0xf8); if (d.desc == desc && d.n == n) return;
        d = SkDesc{}; d.desc = desc; d.n = n;
        if (n < 2 || n > 400) { d.n = 0;
            // (17/11 15:48 PC: "749176384 huesos") +0xf8 de [[ctl+0x100]+0x20] no es la cantidad en el controlador del FixDemoHuman: se
            // anota el controlador para encontrarla (una vez por jugador): qwords de ctl, de [ctl+0x100] y de la descripcion
            static bool said[kMax]; if (!said[slot]) { said[slot] = true; char hx[1100]; int o;
                auto dump = [&](const char* nm, uintptr_t base, int len) { o = 0; for (int q = 0; q < len; q += 8) o += snprintf(hx + o, sizeof hx - o, "%s%llx", q ? " " : "", (unsigned long long)*(const uint64_t*)(base + q)); Log("motor (posar): %s %p: %s", nm, (void*)base, hx); };
                dump("ctl", (uintptr_t)ctl, 0x140); dump("ctl+0x140", (uintptr_t)ctl + 0x140, 0x100); dump("[ctl+0x100]", a, 0x80); dump("desc", desc, 0x100); dump("desc+0x100", desc + 0x100, 0x60);
                const uintptr_t r = *(const uintptr_t*)((uintptr_t)ctl + 0x88); if (PtrOk(r)) dump("[ctl+0x88]", r, 0x80); }
            return; }
        int parOff = -1, parW = 0, hashOff = -1;
        for (int off = 0; off <= 0x1f0 && (parOff < 0 || hashOff < 0); off += 8) {
            const uintptr_t q = *(const uintptr_t*)(desc + off); if (!PtrOk(q)) continue;
            MEMORY_BASIC_INFORMATION mi{}; if (!VirtualQuery((void*)q, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || q + (size_t)n * 4 > (uintptr_t)mi.BaseAddress + mi.RegionSize) continue;
            for (int wdt = 1; wdt <= 4 && parOff < 0; wdt *= 2) {      // padres de 1, 2 o 4 bytes
                bool okp = true; int roots = 0;
                for (int i = 0; i < n && okp; i++) { int v = wdt == 1 ? (int)*(const uint8_t*)(q + i) : wdt == 2 ? (int)*(const uint16_t*)(q + 2 * i) : *(const int*)(q + 4 * i);
                    const int none = wdt == 1 ? 0xff : wdt == 2 ? 0xffff : -1;
                    if (v == none) { roots++; continue; } if (v < 0 || v >= i) okp = false; }
                if (okp && roots >= 1 && roots <= 2) { parOff = off; parW = wdt;
                    for (int i = 0; i < n; i++) { int v = wdt == 1 ? (int)*(const uint8_t*)(q + i) : wdt == 2 ? (int)*(const uint16_t*)(q + 2 * i) : *(const int*)(q + 4 * i);
                        d.par[i] = (v == (wdt == 1 ? 0xff : wdt == 2 ? 0xffff : -1)) ? -1 : v; } d.parOk = true; }
            }
            if (hashOff < 0) { int hit = 0; for (int i = 0; i < n; i++) if (SkName(*(const uint32_t*)(q + 4 * i))) hit++;
                if (hit * 3 >= n) { hashOff = off; for (int i = 0; i < n; i++) d.hash[i] = *(const uint32_t*)(q + 4 * i); d.hashOk = true; } }
        }
        Log("motor (posar): jugador %d: esqueleto %p con %d huesos; padres %s (+0x%x, %d bytes); nombres %s (+0x%x)", slot + 1, (void*)desc, n,
            d.parOk ? "encontrados" : "NO encontrados (se gira solo el hueso)", parOff, parW, d.hashOk ? "encontrados" : "NO encontrados", hashOff);
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
static void StoreTimes(Pj& p, float len, float cur) { std::lock_guard<std::mutex> lk(s_mx); p.lenT = len; p.curT = cur; }   // fuera del __try (C2712)
int Update(Pj& p, const Pj& w, double now) {
    __try {
        const float* pos = w.pos; const float yaw = w.yaw, scale = w.scale;
        RootMove(p, pos, yaw);
        // reloj propio: velocidad y pausa del Inspector; "desde el principio" vuelve el clip a 0
        const double dt = std::clamp(now - p.tLast, 0.0, 0.1); p.tLast = now;
        void* ctl = nullptr; memcpy(&ctl, p.fd + 0x50, 8);
        if (w.restart != p.restartA) { p.restartA = w.restart; p.tPlay = 0.0; p.endTicks = 0; if (ctl) ((ClipSeekFn)kClipSeek)(ctl, 0, 0, 0.0f, 1, 1); }
        else if (w.seekN != p.seekA && ctl) {      // (06:52) linea de tiempo: vf4 lleva el clip al tiempo que recibe (solo para adelante), asi que
            p.seekA = w.seekN;                     // para ir a cualquier punto se mueve el clip (FUN_141afa7b0) y el reloj al mismo segundo
            const float len = ((ClipTimeFn)kClipLen)(ctl, 0, 1); const float t = std::clamp(w.seek, 0.0f, std::max(0.0f, len - 0.001f));
            ((ClipSeekFn)kClipSeek)(ctl, 0, 0, t, 1, 1); p.tPlay = t; p.endTicks = 0; }
        else if (!w.paused) {
            const double st = dt * (double)std::clamp(w.speed, 0.0f, 4.0f); const double L = (double)p.lenT;
            // (17/11 18:01, Agustin: "un menu mas accesible con mas opciones") modo de reproduccion. vf4 solo va para adelante: para ir
            // para atras se mueve el clip a ese segundo (FUN_141afa7b0) antes de cada vf4.
            if (w.mode == 2 && L > 0.2) {
                if (!p.back) { p.tPlay += st; if (p.tPlay >= L - 0.002) { p.tPlay = L - 0.002; p.back = true; } }
                else { p.tPlay -= st; if (p.tPlay <= 0.0) { p.tPlay = 0.0; p.back = false; } if (ctl) ((ClipSeekFn)kClipSeek)(ctl, 0, 0, (float)p.tPlay, 1, 1); }
            } else { p.back = false; p.tPlay += st; if (w.mode == 1 && L > 0.2 && p.tPlay > L - 0.002) p.tPlay = L - 0.002; }
        }
        bool gs = false; if (ctl && p.scW > 0.0f && fabsf(p.scW - 1.0f) > 1e-4f) gs = GraphScale(ctl, p.scW);      // (17:54) tamano en el grafo, solo durante vf4
        { static int nl = 0; if (gs && nl++ < 3) Log("motor (jugadores propios): tamano %.3f en el grafo de la animacion", p.scW); }
        ((FdVf4Fn)kFdVf4)(p.fd, (float)p.tPlay);
        if (gs) GraphScale(ctl, 1.0f);
        memcpy(&ctl, p.fd + 0x50, 8);
        if (ctl) PoseApply(p, ctl, w);
        if (ctl && !w.paused && w.mode == 0) {      // si el clip no da la vuelta solo (como la banca): 6 cuadros quieto en el final -> al principio
            const float len = ((ClipTimeFn)kClipLen)(ctl, 0, 1), cur = ((ClipTimeFn)kClipTime)(ctl, 0, 1);
            if (len > 0.2f && cur >= len - 0.003f) { if (++p.endTicks >= 6) { ((ClipSeekFn)kClipSeek)(ctl, 0, 0, 0.0f, 1, 1); p.endTicks = 0; } } else p.endTicks = 0;
        }
        if (ctl) StoreTimes(p, ((ClipTimeFn)kClipLen)(ctl, 0, 1), ((ClipTimeFn)kClipTime)(ctl, 0, 1));
        RootMainScale(p, scale);
        { float rw[3]; if (ctl && ReadRootPos(ctl, rw)) StoreWorld(p, rw); }
        ((FdPushFn)kFdPush)(p.fd);
        void* h = nullptr; memcpy(&h, p.fd + 0x48, 8);
        void* face = FaceEnsure(p);
        if (h && face) { if (w.face >= 0 && w.face < kFaceN) FaceGesture(p, h, face, w.face, w.faceW, w.blink); else ((FaceNeutralFn)kFaceNeutral)(h, face, 0); }
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
        w.clip = q.clip; w.speed = q.speed; w.paused = q.paused; w.restart = q.restart; w.seek = q.seek; w.seekN = q.seekN; w.face = q.face; w.faceW = q.faceW; w.blink = q.blink; w.hidden = q.hidden; w.mode = q.mode; memcpy(w.pose, q.pose, sizeof w.pose); w.nPose = q.nPose; w.boneQ = q.boneQ; } }
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
    p.clip = -1; p.speed = 1.0f; p.paused = false; p.face = -1; p.faceW = 1.0f; p.blink = true;
    if (like) { memcpy(p.pos, like->pos, 12); p.pos[0] += 1.0f; p.yaw = like->yaw; p.scale = like->scale; p.clip = like->clip; p.speed = like->speed; p.paused = like->paused; p.face = like->face; p.faceW = like->faceW; p.blink = like->blink; }
    snprintf(s_msg, sizeof s_msg, "Cargando %s...", nm);
    s_sel.store(slot);
    return slot;
}
int MotorPjAdd(int kind, uint32_t id) {
    char nm[48] = {}; uint64_t key = 0;
    if (kind == 0) {
        const uintptr_t r = DbFind(id, &key);
        if (!r) { Msg("no encontre el id %u en la base de datos del juego", id); return -1; }
        DbName(r, nm, sizeof nm); if (!nm[0]) snprintf(nm, sizeof nm, "Jugador %u", id);
    } else if (kind == 1) { key = 0xffffull; snprintf(nm, sizeof nm, "Jugador base"); }
    else snprintf(nm, sizeof nm, "Igual al del Editar");
    std::lock_guard<std::mutex> lk(s_mx);
    const int slot = AddSlot(kind, key, nm, nullptr); if (slot < 0) return -1;
    Log("motor (jugadores propios): pedido %s (id %u, clave 0x%llx) en el lugar %d", nm, id, (unsigned long long)key, slot + 1);
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

} // namespace fx
