// PES21 FX - desgaste de la cancha en tiempo real (20/10), como en FC 24: un mapa de desgaste (R16, 1024x512, en uv de
// la cancha: 140 x 79 m) que la DLL va pintando con las posiciones de los jugadores durante el partido. Cada dibujo de
// short / medias (PS 5529...) copia la traslacion de m_world (cVSObject b5, fila 3) a un buffer de staging; en Present se
// leen las posiciones (2 cuadros de atraso), se estima la velocidad por el cuadro anterior y se dibujan pinceles
// gaussianos sumados sobre el mapa. El mapa se publica como recurso con nombre "pes_wear" (reglas: srv = 18 pes_wear en
// la base de la cancha, srv = 8 pes_wear en el VS de las hojas) y los shaders lo suman al desgaste del pasto v3.
//   x115 velocidad del desgaste, y115 radio del pincel (m), z115 pelado al maximo, w115 on/off. Se borra al empezar
//   otro partido (deteccion de "partido nuevo" del reloj del cesped).
#include "core.h"
#include <d3d11_1.h>
#include <atomic>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
#include <mutex>

namespace fx {

extern std::atomic<int> g_replayCallsV2, g_replayPlayV2, g_replayMoveV2;   // (13/11 tanda 2) rastro.cpp hk_rpl: llamadas / estado 5 / cursor avanzando (log de y448)
static const int kMaxPlayers = 48, kStages = 8;
// (12/11 17:30, log de Agustin con la grabacion: 'lecturas que fallaron' 4086 de 5400 cuadros, posiciones leidas ~3 por segundo -> cada lectura
// era un 'hueco' (> 0.25 s), sin velocidades: ni pisadas, ni zancada, ni botines sucios) ANILLOS DE 8: cada cuadro se intenta el mas viejo
// pendiente con 3 cuadros o mas (la GPU y las listas diferidas del juego van 2-3 cuadros atras); si no esta listo se reintenta el cuadro
// siguiente y solo se pierde si el anillo da la vuelta (antes: anillo de 3, leido a los 2 cuadros y descartado si fallaba)
static const int kStageMinAge = 3;
static int StageOldest(const int* stamp, int now) { int b = -1; for (int i = 0; i < kStages; i++) if (stamp[i] >= 0 && now - stamp[i] >= kStageMinAge && (b < 0 || stamp[i] < stamp[b])) b = i; return b; }
static int s_rdOk[3] = {}, s_rdRetry[3] = {}, s_rdLost[3] = {};   // lecturas por anillo (0 jugadores, 1 botines / pelota, 2 capsula de la pelota) para el log
static const int kWearBrushes = 192;   // (12/11) pinceles del desgaste por cuadro (se dibujan de a 48: el cbuffer del VS)
static ID3D11Buffer* s_stage[kStages] = {}; static int s_stageFrame[kStages] = { -1, -1, -1, -1, -1, -1, -1, -1 }; static double s_stageG[kStages] = {}; static std::atomic<int> s_stageW{0};   // s_stageG: reloj del juego acumulado al escribirse
static std::atomic<int> s_count{0};          // dibujos anotados en el cuadro actual
static std::atomic<int> s_noteCalls{0}, s_noteOk{0}, s_noteShort{0}, s_noteNoStage{0}, s_noteBone{0}, s_boneShort{0}, s_boneNone{0}; static int s_posBad = 0, s_posZero = 0, s_posOk = 0, s_rowPick[4] = {}; static ID3D11Buffer* s_zeroBuf = nullptr;   // (08/11 aa) s_zeroBuf: 64 B en cero para limpiar el hueso del slot cuando el dibujo no tiene b6   // (08/11 z) diagnostico
static int s_frame = 0;
static ID3D11Texture2D* s_tex = nullptr; static ID3D11RenderTargetView* s_rtv = nullptr; static ID3D11ShaderResourceView* s_srv = nullptr;
static ID3D11VertexShader* s_vs = nullptr; static ID3D11PixelShader* s_ps = nullptr; static bool s_tried = false;
static ID3D11Buffer* s_cb = nullptr; static ID3D11BlendState* s_bsAdd = nullptr; static ID3D11RasterizerState* s_rs = nullptr; static ID3D11DepthStencilState* s_dss = nullptr;
static std::vector<float> s_prev;           // posiciones del cuadro anterior (x, z)
// (2/11) tanda 14 "reacciones" (pedido de Agustin: mas rastro, mas desgaste por los jugadores, pelota manchada). Fila 177:
//   x manchas de la pelota (verde / tierra / cal; 0 = no, 1 = natural), y frenadas y arranques marcan mas (0..2), z rastro de la
//   pelota segun su velocidad (0..2). El desgaste se duplica en la CPU (s_wearCpu, 256 x 128 celdas de 55 x 62 cm) para saber cuanta
//   tierra hay bajo la pelota sin leer la GPU; la pelota se reconoce entre los dibujos del PS de botines/pelota (aadffecf) porque la
//   traslacion de su m_world coincide con la capsula de la pelota del pasto (s_ball): el indice de ese dibujo queda como "la pelota"
//   y en sus dibujos se enlaza b12 (PESBallC: x 1, y verde, z tierra, w cal).
struct PlayerPrev { float x, z, vx, vz, skid; float stride = 0.f; int foot = 0; float printX = 0.f, printZ = 0.f; int printDue = 0; float svx = 0.f, svz = 0.f, sax = 0.f, saz = 0.f; int trk = 0; float y = 0.f, yb = 0.f; int yok = 0, air = 0, slide = 0, land = 0; float printK = 0.f;
    int id = -1; double lastRealG = -1e9, feetG = -1e9; int printReal = 0; float printHx = 0.f, printHz = 0.f; };   // (13/11 tanda 2) z447 pisadas v2: id estable del jugador, reloj del juego (s_gClk) de su ultima pisada real y de sus pies vistos, la huella es de una pisada real y su rumbo (hueso del pie)   // (12/11 yo8viw) printK: fuerza de la accion en la ultima pisada (desgaste real)   // (11/11 4wp0pj) y: alto del hueso 0 (cadera), yb: su alto de pie (promedio lento), air / slide / land: en el aire, barriendose, cayo de un salto   // (11/11) sv / sa: velocidad y aceleracion suavizadas (m/s, m/s2); trk: cuadros seguidos
// (08/11, Agustin: "las pisadas tienen que ser mejoradas ... desprendimiento realista del pasto con mantos de tierra") fila 389:
//   x huellas marcadas (pisada por pisada, pie izquierdo / derecho, en vez de una mancha continua) | y fuerza de la huella |
//   z cantidad de pasto que salta | w segundos que los terrones quedan en el piso
// fila 390: x reaccion (sensibilidad a frenadas, giros y remates) | y tamano de los terrones (cm) | z polvo y tierra | w calidad (0 baja, 1 normal, 2 alta)
static inline float FpOn()      { return g_cfg.live[389][0] > 0.5f ? 1.f : 0.f; }
static inline float FpForce()   { return g_cfg.live[389][1] > 0.f ? std::clamp(g_cfg.live[389][1], 0.f, 2.f) : 1.f; }
static inline float ClodAmt()   { return std::clamp(g_cfg.live[389][2], 0.f, 3.f); }
static inline float ClodRest()  { return std::clamp(g_cfg.live[389][3], 0.f, 40.f); }
static inline float ReactK()    { return g_cfg.live[390][0] > 0.05f ? std::clamp(g_cfg.live[390][0], 0.3f, 2.f) : 1.f; }
static inline float ClodSize()  { return (g_cfg.live[390][1] > 0.5f ? std::clamp(g_cfg.live[390][1], 2.f, 12.f) : 6.f) * 0.01f; }
static inline float DustAmt()   { return std::clamp(g_cfg.live[390][2], 0.f, 2.f); }
static inline int   ClipCap()   { int q = (int)(g_cfg.live[390][3] + 0.5f); return q <= 0 ? 512 : (q == 1 ? 1024 : 2048); }
// pinceles de desgaste pendientes (huecos que deja el pasto arrancado), los consume la pasada del desgaste
struct WearBrush { float u, v, k, rad; };
static std::vector<WearBrush> s_wearQueue;
// (12/11, Agustin: "crea un pitch wear real con el gameclock ... con memoria", "tiene que ser progresivo y no siempre el desgaste es igual,
// un jugador al patear fuerte o al presionar mucho en una zona el desgaste es mayor") DESGASTE REAL, fila 431: x 1 = on, y diferencia por
// accion (0 = todo igual, 1 = natural, 2 = muy marcada).
//  - reloj del PARTIDO: el mapa solo se gasta mientras el tick del partido (MatchInfo+0x1198, gancho del final del tick en rastro.cpp) avanza:
//    en pausa, en las repeticiones y en las escenas no se suma nada (antes la repeticion de una barrida la volvia a marcar)
//  - memoria: se borra solo con un partido nuevo (Sider reescribe match.ini) o, sin eso, cuando el tick del partido vuelve a empezar; el
//    entretiempo (cancha sin dibujarse 20 s) ya no lo borra
//  - por accion: caminar o estar parado casi no gasta; la pisada fuerte (arranque, frenada, giro), el pie de apoyo del remate y el raspon
//    del botin, y la barrida (raspon largo) gastan mucho mas. Lo progresivo (primero mas corto, despues seco, al final tierra) es la curva
//    del shader (pes_grass.hlsli, PESWearLiveCurve con x431)
static inline bool WearReal() { return g_cfg.live[431][0] > 0.5f; }
static inline float WearActK() { return std::clamp(g_cfg.live[431][1], 0.f, 2.f); }
// (12/11, Agustin: "un slider para activar en el momento y que el desgaste se note mucho mas de los jugadores", para grabar) w437 PRUEBA:
// multiplica en vivo la velocidad del desgaste (x115) de lo que hacen los jugadores y la pelota: 0 o 1 = como antes, 20 = cada pisada,
// frenada, giro o barrida se ve crecer en segundos. Sigue frenado en pausa y en las repeticiones (reloj del partido)
// (13/11 tanda 2) con y448 las pruebas (w437, z441) solo actuan en la repeticion con x441 (WearTestsOn): dejadas prendidas jugando, z441 7
// saturaba el mapa y la pelota ya no cambiaba nada donde se juega. y448 0 = como antes (siempre)
static bool WearTestsOn();
static inline float WearBoost() { const float b = g_cfg.live[437][3]; return (b > 1.f && WearTestsOn()) ? std::min(b, 30.f) : 1.f; }
static int s_tkLast = -1; static double s_tkChangeT = -1e9; static bool s_tkSeen = false;
static void SimClockPoll() { int tk = g_simTick.load(std::memory_order_relaxed); if (tk >= 0 && tk != s_tkLast) { s_tkLast = tk; s_tkChangeT = Now(); s_tkSeen = true; } }
// 1 = el desgaste puede sumar ahora; 0 = partido detenido para la simulacion (pausa, repeticion, escena). Sin el gancho: siempre 1 (como antes)
// (12/11 16:15, Agustin: "el jugador al correr no deja ninguna marca ni desgaste") en su partido el tick de MatchInfo+0x1198 quedo clavado
// (403 durante 20 min, 473 despues) con los jugadores corriendo: la DLL creia que el partido estaba detenido y no sumaba nada. CONFIANZA en
// el reloj: promedio (~60 s) de 'el tick avanzo hace < 0.25 s' medido solo con el juego andando (reloj del juego) y jugadores corriendo.
// Con el gancho bueno queda ~1 (las repeticiones lo bajan poco); si el tick no acompana (< 0.15) se suma con el reloj del juego, como sin
// gancho (la pausa igual frena: sin tiempo de juego no hay posiciones nuevas)
// (12/11 16:45) y la REPETICION se toma del visor de repeticiones (rastro.cpp hk_rpl, 0x14217c0b0 con el objeto reproduciendo): no depende del tick
static float s_tkTrust = 0.5f;
// (13/11 tanda 2) y448 DETECTOR DE REPETICIONES v2 (ReplayV2Present, una vez por Present): en su log el visor decia "reproduciendo" todo
// el partido en vivo, asi que InReplay() era siempre verdadero. Ahora: repeticion = el reloj crudo del juego (m_timeParam.x de SysCB) esta
// mas de 0.25 s detras de su maximo del partido (la repeticion lo rebobina), o va a 0.05..0.85 de ritmo durante 0.3 s (camara lenta), o el
// visor con el cursor avanzando (rastro.cpp), salvo que el visor este roto (dice reproduciendo > 95 % de 60 s de juego en vivo). 0 = como antes
static bool s_rpClock = false, s_rpBroken = false;
static inline bool RpV2() { return GrassLive(448, 1) > 0.5f; }
static inline bool HookPlaying() { const unsigned long long r = g_replayMs.load(std::memory_order_relaxed); return r != 0 && GetTickCount64() - r < 300; }
static inline bool InReplay() { return RpV2() ? (s_rpClock || (HookPlaying() && !s_rpBroken)) : HookPlaying(); }
// (12/11 16:50, Agustin: para el video, el desgaste apareciendo en camara lenta) fila 441: x DESGASTE SOLO EN LA REPETICION (para grabar):
// jugando no suma y en la repeticion (tambien en camara lenta: velocidades y pisadas van con el reloj del juego) va marcando a medida que
// pisan. Necesita el gancho del visor de repeticiones; sin el, x441 no hace nada (13/11 tanda 2: con y448 tambien sin el gancho, por el
// reloj crudo). y = pisadas en la posicion real del botin (ver FootPt)
static inline bool ReplayMode() { return g_cfg.live[441][0] > 0.5f && (g_replayHook.load(std::memory_order_relaxed) != 0 || RpV2()); }
static bool WearTestsOn() { return !RpV2() || (ReplayMode() && InReplay()); }
// (14/11, Agustin: "una opcion de camara libre con camara lenta en repeticion") x344 = 3 / z343 = 2: la camara libre y la camara lenta
// se prenden solas mientras dura la repeticion. Solo con el detector v2 (y448): el visor solo decia "reproduciendo" todo el partido
bool ReplayNow() { return RpV2() && InReplay(); }
static inline bool FootOn() { return g_cfg.live[441][1] > 0.5f && g_cfg.live[115][3] > 0.5f; }
// (12/11 16:55, Agustin: "un slider para exagerar esas pisadas") z441 EXAGERAR LAS PISADAS (0 o 1 = normal, hasta 10): cada huella marca mas
// (aplastado, huella del desgaste y gasto por pisada) y es mas grande (x1 + 0.12 por punto: 10 = ~2x de tamano); con mas de 1 tambien
// gastan las pisadas comunes (no solo las fuertes). (13/11 tanda 2) con y448: solo en la repeticion con x441 (WearTestsOn)
static inline float FootEx() { const float e = g_cfg.live[441][2]; return (e > 1.f && WearTestsOn()) ? std::min(e, 10.f) : 1.f; }
static inline float FootExR() { return 1.f + 0.12f * (FootEx() - 1.f); }
static inline float SimGate() { if (ReplayMode()) return InReplay() ? 1.f : 0.f; if (!WearReal()) return 1.f; if (InReplay()) return 0.f; if (!s_tkSeen || s_tkTrust < 0.15f) return 1.f; return (Now() - s_tkChangeT) < 0.25 ? 1.f : 0.f; }
static void WearCpuAdd(float u, float v, float k, float radM);
// pincel de desgaste por una accion (cola: lo dibuja PitchWearPresent en este mismo cuadro)
static void WearStamp(float x, float z, float k, float rad) {
    if (k <= 0.f) return; float u = x / 140.f + 0.5f, v = z / 79.f + 0.5f; if (u < 0.f || u > 1.f || v < 0.f || v > 1.f) return;
    s_wearQueue.push_back({ u, v, k, rad }); WearCpuAdd(u, v, k, rad);
}
static std::vector<PlayerPrev> s_pl;        // jugadores del cuadro anterior con su velocidad (m/s) y cuanto frenaban (0..1)
static float s_wearCpu[128][256] = {};
static float s_stain[3] = {};               // verde, tierra, cal (0..1)
static ID3D11Buffer* s_ballCb = nullptr;    // b12 del PS de la pelota
static const int kBallSlots = 128;   // (2/11 paso 10) 64 desbordaba en silencio (22 x 2 botines + arbitros + pelota + repeticiones)
static ID3D11Buffer* s_bdStage[kStages] = {}; static int s_bdFrame[kStages] = { -1, -1, -1, -1, -1, -1, -1, -1 }; static int s_bdPres = 0; static std::atomic<int> s_bdW{0}; static std::atomic<int> s_bdCount{0};
static UINT s_bdIdx[kStages][kBallSlots] = {};   // indice (vertices) de cada dibujo anotado, por anillo
// (12/11 16:50, Agustin: "como sabe ... cual es la posicion de los pies al pisar") PISADAS REALES: de cada dibujo de botines (PS aadffecf)
// se copian ademas sus huesos (VSBones b6, float4x3 m_boneMatrices[32]: desde 12/11 17:30 solo los 4 primeros, ver kFtLat) y en Present se pasan al mundo con
// m_world (como el hueso 0 del short). Se quedan los puntos a la altura del piso y cerca de un jugador seguido; el suelo es el 2.o mas bajo
// (desde 17:30 el suelo es y = 0). PISADA = un punto que venia arriba (> 6 cm) y baja a < 3 cm (con histeresis: vale en camara lenta; talon y punta = una). La pisada va al jugador mas cercano en
// vez de la zancada estimada; si no se ven pies o no hubo pisadas en 2 s, vuelve la zancada (log "pisadas reales (15 s)")
static ID3D11Buffer* s_ftStage[kStages] = {}; static UINT8 s_ftOk[kStages][kBallSlots] = {};
struct FootPt { float x, y, z; int up; };   // up: venia levantado (subio a > suelo + 7 cm y todavia no bajo a < suelo + 3.5 cm)
struct FootStrike { float x, z; double t; int used; double g = 0.0; int pid = -1, bone = -1; float hx = 0.f, hz = 0.f; };   // (13/11 tanda 2) z447: g reloj del juego (s_gClk), jugador (id), hueso y rumbo del pie
static std::vector<FootPt> s_ftPrev; static std::vector<FootStrike> s_ftStrikes; static float s_ftGround = 0.f; static bool s_ftGroundOk = false;
// (13/11 tanda 2) z447 PISADAS v2 (Agustin: "las pisadas tienen que ser mejoradas"): en su log se veian 6-21 pisadas reales cada 15 s contra
// ~270 pasos, y una sola pisada real en 2 s apagaba la zancada de TODOS. Ahora cada pie es una pista (jugador estable + hueso) con su propio
// piso: base = min(y, base + 3 cm por s de juego); apoyado = y < base + 2.5 cm y el pie casi quieto (< 0.8 m/s); en el aire = > 1.2 m/s o
// y > base + 4 cm; PISADA = en el aire -> apoyado. Autoprueba de 5 s: un hueso es pie si con su jugador corriendo esta apoyado el 20-75 % del
// tiempo y sube y baja mas de 5 cm. Todo en el reloj del juego (s_gClk: pausa = nada, camara lenta = mas lento)
static double s_gClk = 0.0;   // reloj del juego acumulado (GameStep), una vez por Present en PitchWearPresent
static double s_bdG[kStages] = {}, s_stageGc[kStages] = {};   // s_gClk de cada anillo de botines / jugadores al cerrarse su cuadro
struct FootTrk { int pid, bone, st, upd, n; float cx, cz, y, base, yLo, yHi; double g, seenR; };   // st: 0 sin saber, 1 apoyado, 2 en el aire
struct FootObs { int pl, bone; float cx, cy, cz, px, pz, fx, fz; };
static std::vector<FootTrk> s_ftTrk; static int s_ftTrkUpd = 0, s_ftBoneMask = 0xF, s_plNextId = 0;
static int s_ftStN[4] = {}, s_ftStIn[4] = {}, s_ftRngN[4] = {}; static float s_ftRngSum[4] = {}, s_ftStPct[4] = {}, s_ftRngCm[4] = {}; static double s_ftTestT = -1.0;
static float s_ftFwdAcc = 0.f, s_ftExpect = 0.f;   // s_ftFwdAcc: si el eje z del hueso apunta hacia donde corre (+) o al reves (-); s_ftExpect: pasos esperados (log)
static double s_ftPtsT = -1e9, s_ftStrikeT = -1e9; static int s_ftLogPts = 0, s_ftLogFrames = 0, s_ftLogStrikes = 0, s_ftLogUsed = 0, s_ftLogStride = 0;
static const UINT kFtBones = 4, kFtSlotB = kFtBones * 48;   // bytes por dibujo en s_ftStage
// (12/11 17:30, log: 'suelo' -0.22 m y 230 pisadas de las que se usaron 42) Los huesos de la paleta son matrices de PIEL (hueso * inversa de
// la pose de referencia): su traslacion NO es la articulacion sino donde cae el origen del modelo (el piso entre los pies) pegado a ese hueso.
// Para el pie eso es justo la suela: ~0 apoyado, sube al levantarlo. Los huesos del torso con las rodillas flexionadas dan -0.1..-0.25 (de ahi
// el 'suelo' corrido) y despues del ultimo hueso de la malla queda basura de otros dibujos. Ahora: solo los primeros 4 (y no mas que la ventana
// enlazada del b6), el suelo es la cancha (y = 0, la pelota apoyada da 0.11 = su radio), apoyado < 3 cm, levantado > 6 cm, y la huella va al
// costado del pie: el origen corrido +-9 cm en x del modelo (el lado que queda mas lejos del centro del jugador)
static const float kFtLat = 0.09f, kFtUp = 0.06f, kFtDown = 0.03f;
static int s_ftBoneN[kFtBones] = {}, s_ftBoneLow[kFtBones] = {}; static float s_ftBoneY[kFtBones] = {};
static std::atomic<int> s_ftNoB6{0};
static std::atomic<UINT> s_ballIdx{0};      // indice de vertices del dibujo de la pelota (0 = aun no)
static std::atomic<int> s_bdCalls{0}, s_bdOffX177{0}, s_bdNoStage{0}, s_bdNoCb{0}, s_bdNoted{0};   // (2/11 b) diagnostico: por que no se anota
static std::atomic<int> s_ballIdxVotes{0}; static int s_ballLogged = 0;
static std::atomic<int> s_bdOverflow{0}; static int s_ballChanges = 0; static UINT s_ballHist[10] = {}; static int s_ballHp = 0;   // (2/11 paso 10) dibujos que no entraron en los slots; cambios del indice votado
// (13/11) x505 BOTINES EN EL PASTO (Agustin 13/11 16:18: "las pisadas pueden tener una colision detectable por el pasto en los botines?",
// "si esto no consume mucho quizas puedas hacer que a cierta distancia no haga esto", "que esta colision solo la detecte el cesped").
// SOLO SE VE: en cada dibujo de botines (PS aadffecf) la GPU copia su b5 (m_world por columnas + m_useWeightCount) y su b6 (huesos 0..3 de
// este cuadro y del anterior, c96) a un lugar del pozo pes_feet_raw, con el sello del cuadro (s_colGen: la copia lo toma cuando la GPU
// ejecuta la lista, asi las listas diferidas quedan en su cuadro). En Present un compute (pes_feet_cs.hlsl) elige los pies cerca de la
// camara (el lado de cada pie con la cadera del jugador, como las pisadas) y el VS del pasto (498b30) los lee en t10. Sin x505 no se copia nada
static const UINT kColSlots = 256, kColF4 = 116, kColOutF4 = 160;   // lugar: 2 cabecera + 5 b5 (c4..c8) + 108 b6 (c0..c107) + 1 libre; igual a NS / SF4 del compute
static ID3D11Device* s_colDev = nullptr;
static ID3D11Buffer* s_colRaw = nullptr; static ID3D11ShaderResourceView* s_colRawSRV = nullptr;
static ID3D11Buffer* s_colOut = nullptr; static ID3D11ShaderResourceView* s_colOutSRV = nullptr; static ID3D11UnorderedAccessView* s_colOutUAV = nullptr;
static ID3D11Buffer* s_colGen = nullptr; static ID3D11Buffer* s_colCB = nullptr; static ID3D11Buffer* s_colDiag[4] = {}; static int s_colDiagF[4] = { -1, -1, -1, -1 };
static ID3D11ComputeShader* s_colCS = nullptr; static bool s_colTried = false, s_colPendReset = false;
static std::atomic<bool> s_colReady{false}, s_colRan{false}, s_colFar{true};
static std::atomic<UINT> s_colAt{0};
static std::atomic<int> s_colCalls{0}, s_colCopies{0}, s_colNoCb{0}, s_colNoPrev{0}, s_colBall{0}, s_colFarSkip{0};
static UINT s_colGenV = 1;   // siempre igual al valor subido a s_colGen: un lugar viejo tiene sello <= s_colGenV
static double s_plGc = 0.0;  // (x505) reloj del juego (s_gClk) de la lectura de la que salio s_pl (para adelantar las caderas)
static float s_colFl[4] = {}, s_colD = 12.f, s_colN = 8.f;   // (x505) para el log: piso de cada hueso, distancia y pies a la vez del ultimo compute
static inline bool BootColOn() { return GrassLive(505, 0) > 0.001f; }   // z437 (version vieja del pasto) la apaga: kGrassVer
// (11/11 4wp0pj, Agustin: "que el pasto manche los botines con pedazos de tierra en 3d o con detalle en 3d de cesped en la parte de abajo
// despues de un rato en partido progresivo y dependiendo si el jugador corrio mucho o el estado de la cancha" + "tiene que funcionar con las
// mecanicas de gameplay del juego ... no solo algo visual que se activa por que si") BOTINES SUCIOS POR JUGADOR. Cada jugador que sigue el
// desgaste (dibujos del short) junta en sus botines, por METRO CORRIDO en tiempo de juego: tierra (mas donde la cancha esta pelada -
// s_wearCpu bajo sus pies -, mas en frenadas / giros / arranques - skid -, el doble con cancha mojada) y pasto pegado (mas en cancha
// sana y humeda). Parado no junta nada; el pasto pegado se cae solo en ~4 min, la tierra seca en ~15. Cada dibujo de botines (PS aadffecf,
// el mismo que la pelota) se asigna al jugador mas cercano por la traslacion de su b5 (leida 2 cuadros despues, como la pelota) y recibe
// en b12 x 2, y tierra, z pasto, w mojado; el shader pone grumos de tierra y hojitas de pasto con relieve (normal) donde la mascara de
// barro del juego (t4) dice que se ensucia primero (suela, tapones, bordes). Fila 430: x cantidad (0 = no), y relieve, z pasto pegado,
// w cuanto se desprende solo.
struct BootDirt { float x, z, dirt, grass; double seen; };
static std::vector<BootDirt> s_bd; static int s_bdGen = -1;
static UINT s_bdKey[kStages][kBallSlots] = {};          // clave de cada dibujo anotado (vertices ^ textura t0) para no cruzar botines
static UINT s_bootKey[kBallSlots] = {}; static float s_bootVal[kBallSlots][4] = {}; static float s_bootAvgV[4] = { 2.f, 0.f, 0.f, 0.f };
static ID3D11Buffer* s_bootCb[kBallSlots] = {}; static ID3D11Buffer* s_bootAvgCb = nullptr;
static std::atomic<int> s_bootHit{0}, s_bootAvgN{0}, s_bootAssigned{0}; static float s_bootReplayT = 0.f; /* (12/11) segundos de repeticion (tiempo ya jugado) en los que no se sumo */ static float s_bootSlideT = 0.f; static int s_bootLandN = 0; static float s_bootYb = 0.f; static int s_bootYn = 0;
static inline float BootAmt() { return std::clamp(g_cfg.live[430][0], 0.f, 2.f); }
static float WetNow() {   // misma regla que PESEqWet de los shaders (filas 110 / 114): automatico = lluvia del partido
    const float* Q = g_cfg.live[110]; const float* L = g_cfg.live[114]; int m = (int)(Q[2] + 0.5f); float wet = 0.f;
    if (m == 0) wet = ((int)(L[0] + 0.5f) == 1) ? 1.f : 0.f; else if (m == 2) wet = 1.f;
    return wet * (Q[3] > 0.001f ? std::clamp(Q[3], 0.f, 1.f) : 1.f);
}
static UINT BootKey(ID3D11DeviceContext* c, UINT verts) {
    ID3D11ShaderResourceView* v = nullptr; c->PSGetShaderResources(0, 1, &v); UINT k = verts ^ (UINT)(((uintptr_t)v >> 4) * 2654435761u); if (v) v->Release(); return k ? k : 1u;
}
static void WearCpuAdd(float u, float v, float k, float radM) {
    // mismo pincel que pes_wear_ps.hlsl: k * exp(-r2 * 2.2), r en radios, hasta 1.5 radios
    float cu = u * 256.f, cv = v * 128.f, ru = radM / 140.f * 256.f, rv = radM / 79.f * 128.f;
    int u0 = std::max(0, (int)(cu - 1.5f * ru)), u1 = std::min(255, (int)(cu + 1.5f * ru) + 1), v0 = std::max(0, (int)(cv - 1.5f * rv)), v1 = std::min(127, (int)(cv + 1.5f * rv) + 1);
    for (int j = v0; j <= v1; j++) for (int i = u0; i <= u1; i++) {
        float dx = (i + 0.5f - cu) / std::max(ru, 0.01f), dy = (j + 0.5f - cv) / std::max(rv, 0.01f); float r2 = dx * dx + dy * dy;
        if (r2 < 2.25f) s_wearCpu[j][i] += k * expf(-r2 * 2.2f);
    }
}
static float WearCpuAt(float x, float z) {
    int i = std::clamp((int)((x / 140.f + 0.5f) * 256.f), 0, 255), j = std::clamp((int)((z / 79.f + 0.5f) * 128.f), 0, 127);
    return s_wearCpu[j][i];
}
// distancia (m) a la linea de cal mas cercana de una cancha de 105 x 68 centrada (x a lo largo, z a lo ancho)
static float SegDist(float px, float pz, float ax, float az, float bx, float bz) {
    float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz; float t = l2 > 1e-6f ? std::clamp(((px - ax) * dx + (pz - az) * dz) / l2, 0.f, 1.f) : 0.f;
    float qx = ax + dx * t - px, qz = az + dz * t - pz; return sqrtf(qx * qx + qz * qz);
}
static float LineDist(float x, float z) {
    const float L = 52.5f, W = 34.f; float d = 1e9f;
    auto seg = [&](float ax, float az, float bx, float bz) { d = std::min(d, SegDist(x, z, ax, az, bx, bz)); };
    seg(-L, -W, L, -W); seg(-L, W, L, W); seg(-L, -W, -L, W); seg(L, -W, L, W); seg(0, -W, 0, W);
    d = std::min(d, fabsf(sqrtf(x * x + z * z) - 9.15f));
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float gx = sgn * L, bx = sgn * (L - 16.5f), ax = sgn * (L - 5.5f), px = sgn * (L - 11.f);
        seg(gx, -20.16f, bx, -20.16f); seg(gx, 20.16f, bx, 20.16f); seg(bx, -20.16f, bx, 20.16f);
        seg(gx, -9.16f, ax, -9.16f); seg(gx, 9.16f, ax, 9.16f); seg(ax, -9.16f, ax, 9.16f);
        if (sgn * (x - bx) < 0.f) d = std::min(d, fabsf(sqrtf((x - px) * (x - px) + z * z) - 9.15f));   // semicirculo del area, fuera del area
    }
    return d;
}
static int s_matchGenSeen = -1; static bool s_cleared = false;
static double s_lastT = 0; static int s_logged = 0;
// ---- recortes de pasto (particulas en CPU, dibujadas con el PS de las hojas en la pasada del pasto)
struct Clip { float x, y, z, vx, vy, vz, age, life, size, rot; int kind = 0; int rest = 0; float spin = 0.f, ph = 0.f; };   // (11/11) spin: giro propio (rad/s), ph: fase del aleteo de las hojas   // (08/11) rest = 1: quedo en el piso (tarjeta acostada, w = giro + 10 * (tipo + 5))   // (2/11) kind: 0 manojo arrancado, 1 terron de tierra, 2 polvo, 3 pedazo de hoja (bandas de textures\pes_debris.dds)
static thread_local bool t_clipDraw = false; bool PitchClipDrawActive() { return t_clipDraw; }
static const int kMaxClips = 2048;
static std::vector<Clip> s_clips;
static float Rand01();
// (11/11, Agustin: "como se calculan estos pedazitos que vuelan ... debe ser rehecho") RECORTES v2. Antes salian CADA CUADRO detras de todo
// jugador que corria (90 por segundo a 60 FPS, mas con mas FPS), de la cadera y no del pie, con la velocidad cruda (ruido = frenadas falsas),
// desaparecian en el aire a los 0.5-1.2 s y la pelota rodando tiraba pasto. Ahora salen SOLO en la pisada (una por media zancada) y segun lo
// que hace ese pie: impulso / sprint -> hacia atras; frenada -> hacia adelante y bajo; giro -> hacia afuera de la curva; remate -> hacia
// donde sale la pelota. Trotar o caminar no arranca nada. Cantidad por pisada (no depende de los FPS).
//   tipos: 0 manojo arrancado con raiz, 1 terron de tierra, 2 polvo, 3 pedazo de hoja (bandas de textures\pes_debris.dds)
enum { kClipTuft = 0, kClipClod = 1, kClipDust = 2, kClipBlade = 3 };
static std::atomic<int> s_strikeN{0}; static std::atomic<int> s_bornN[4];
static void PushClip(int kind, float x, float y, float z, float vx, float vy, float vz, float size) {
    if ((int)s_clips.size() >= ClipCap()) return;
    Clip c; c.x = x; c.y = y; c.z = z; c.vx = vx; c.vy = vy; c.vz = vz; c.age = 0.f; c.size = size; c.rot = Rand01() * 6.2832f; c.kind = kind;
    float sg = Rand01() < 0.5f ? -1.f : 1.f;
    c.spin = sg * (kind == kClipBlade ? 8.f + 14.f * Rand01() : kind == kClipTuft ? 4.f + 6.f * Rand01() : kind == kClipClod ? 2.f + 5.f * Rand01() : 0.3f);
    c.ph = Rand01() * 6.2832f;
    c.life = kind == kClipDust ? 0.7f + 0.7f * Rand01() : 4.0f;   // en el aire viven hasta tocar el piso (4 s de seguridad); el polvo se disipa
    s_clips.push_back(c); s_bornN[kind & 3]++;
}
// (x, z) donde pisa el pie, (tx, tz) hacia donde sale despedido (unitario), inten 0..~1.5 (fuerza de la accion)
static void EmitStrike(float x, float z, float tx, float tz, float inten, bool fromBall) {
    const float* C = g_cfg.live[116]; if (C[3] < 0.5f || inten < 0.05f) return;
    if ((int)s_clips.size() >= ClipCap() - 4) return;
    s_strikeN++;
    const float amount = C[0] > 0.f ? C[0] : 1.0f, bsz = (C[1] > 0.f ? C[1] : 3.0f) * 0.01f;
    const float w = WearCpuAt(x, z); const bool dry = g_cfg.live[114][0] > 1.5f, wet = g_cfg.live[114][0] > 0.5f && !dry;
    const float px = -tz, pz = tx;   // costado del tiro
    const float I1 = std::min(inten, 1.f);
    // pedazos de hoja sueltos: pocos en el sprint (~0.5 por pisada), muchos en la frenada / el giro fuerte. Abanico mas cerrado con mas fuerza
    float want = amount * 4.5f * inten; int n = (int)want; if (Rand01() < want - n) n++; n = std::min(n, 14);
    for (int e = 0; e < n; e++) {
        float a = (Rand01() - 0.5f) * (1.6f - 0.7f * I1);
        float dx = tx + px * a, dz = tz + pz * a, l = sqrtf(dx * dx + dz * dz) + 1e-4f; dx /= l; dz /= l;
        float hs = (0.5f + 2.0f * inten) * (0.5f + Rand01()), vy = (0.6f + 1.5f * inten) * (0.6f + 0.8f * Rand01());
        PushClip(kClipBlade, x + (Rand01() - 0.5f) * 0.12f, 0.03f, z + (Rand01() - 0.5f) * 0.12f, dx * hs, vy, dz * hs, bsz * (0.6f + 0.8f * Rand01()));
    }
    // manojos con raiz y terrones (el "divot"): solo acciones fuertes y con x389 z > 0; en lo pelado sale tierra, con cancha humeda mas
    const float clodA = ClodAmt();
    if (clodA > 0.001f && inten > 0.45f) {
        float wantC = clodA * (inten - 0.45f) * (wet ? 3.0f : 2.0f); int m = (int)wantC; if (Rand01() < wantC - m) m++; m = std::min(m, 4);
        const float csz = ClodSize(), dust = DustAmt();
        for (int e = 0; e < m; e++) {
            bool soil = (w > 0.25f && Rand01() < w) || Rand01() < 0.25f;
            float a = (Rand01() - 0.5f) * 0.7f; float dx = tx + px * a, dz = tz + pz * a, l = sqrtf(dx * dx + dz * dz) + 1e-4f; dx /= l; dz /= l;
            float hs = (0.8f + 1.6f * inten) * (0.6f + 0.8f * Rand01()), vy = (1.0f + 1.6f * inten) * (0.6f + 0.7f * Rand01());
            PushClip(soil ? kClipClod : kClipTuft, x, 0.02f, z, dx * hs, vy, dz * hs, csz * (0.6f + 0.8f * Rand01()) * (soil ? 0.8f : 1.f));
            if (dust > 0.001f && (soil || dry) && Rand01() < dust * (dry ? 0.9f : 0.4f))
                PushClip(kClipDust, x, 0.03f, z, dx * hs * 0.4f, vy * 0.35f, dz * hs * 0.4f, csz * 2.5f * (0.7f + 0.6f * Rand01()));
        }
        // hueco en el desgaste: donde se arranco el pasto queda la tierra
        if (m > 0 && g_cfg.live[115][3] > 0.5f && !WearReal() && SimGate() > 0.5f) { float k = 0.03f * std::min((float)m * 1.5f, 4.f) * (fromBall ? 1.5f : 1.f); s_wearQueue.push_back({ x / 140.f + 0.5f, z / 79.f + 0.5f, k, 0.14f }); WearCpuAdd(x / 140.f + 0.5f, z / 79.f + 0.5f, k, 0.14f); }
    }
    // tierra seca / pelada: polvo aunque no salga terron
    if (DustAmt() > 0.001f && (dry || w > 0.4f) && Rand01() < DustAmt() * 0.5f * I1)
        PushClip(kClipDust, x, 0.03f, z, tx * 0.6f, 0.25f, tz * 0.6f, ClodSize() * 2.5f * (0.7f + 0.6f * Rand01()));
}
static ID3D11Buffer* s_clipBuf = nullptr; static ID3D11ShaderResourceView* s_clipSRV = nullptr; static ID3D11VertexShader* s_clipVS = nullptr; static bool s_clipTried = false;
static std::atomic<int> s_clipCount{0}; static std::atomic<int> s_clipFrame{-1};
static float Rand01() { static uint32_t st = 0x9E3779B9u; st ^= st << 13; st ^= st >> 17; st ^= st << 5; return (st & 0xffffff) / 16777216.0f; }
static void ClipSimAndUpload(float dt) {
    if (!g_dev.dev || !g_dev.imm) return;
    if (!s_clipBuf) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxClips * 32; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_SHADER_RESOURCE; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = 16;
        if (SUCCEEDED(g_dev.dev->CreateBuffer(&d, nullptr, &s_clipBuf))) {
            D3D11_SHADER_RESOURCE_VIEW_DESC v = {}; v.Format = DXGI_FORMAT_UNKNOWN; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; v.Buffer.FirstElement = 0; v.Buffer.NumElements = kMaxClips * 2;
            g_dev.dev->CreateShaderResourceView(s_clipBuf, &v, &s_clipSRV);
        }
    }
    if (!s_clipTried) { s_clipTried = true; ID3DBlob* vb = CompileNamedShader(L"pes_clip_vs.hlsl", "vs_5_0");
        if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_clipVS); vb->Release(); }
        Log("recortes de pasto: shader %s", s_clipVS ? "listo" : "NO compilo (ver errores)"); }
    // viento de las nubes (z84 direccion, y84 velocidad)
    float ang = g_cfg.live[84][2] * 0.0174533f, wsp = std::min(std::max(g_cfg.live[84][1], 0.f), 60.f) * 0.03f;
    float wx = cosf(ang) * wsp, wz = sinf(ang) * wsp;
    // (11/11) RECORTES v2: gravedad real y arrastre del aire por tipo (velocidad terminal: hoja ~1.2 m/s, manojo ~5, terron ~12): la hoja
    // planea y aletea de costado, el terron sigue una parabola y pica. Arrastre implicito (estable con cualquier dt)
    size_t n = 0;
    for (size_t i = 0; i < s_clips.size(); i++) {
        Clip& c = s_clips[i];
        if (c.rest) { c.age += dt; if (c.age < c.life) s_clips[n++] = c; continue; }   // (08/11) en el piso: quieto hasta que se desvanece
        c.age += dt;
        if (c.kind == kClipDust) {   // polvo: casi sin peso, lo lleva el aire, se frena rapido, crece y se va
            float a = std::min(1.f, 3.0f * dt); c.vx += (wx - c.vx) * a; c.vz += (wz - c.vz) * a; c.vy += (0.12f - c.vy) * a;
            c.size *= 1.0f + 0.7f * dt; c.x += c.vx * dt; c.y += c.vy * dt; c.z += c.vz * dt; c.rot += c.spin * dt;
            if (c.age < c.life) s_clips[n++] = c;
            continue;
        }
        const float kD = c.kind == kClipBlade ? 6.5f : c.kind == kClipTuft ? 0.35f : 0.07f;   // arrastre cuadratico (vt = sqrt(9.81 / kD))
        const float kW = c.kind == kClipBlade ? 1.0f : c.kind == kClipTuft ? 0.15f : 0.0f;    // cuanto lo lleva el viento
        float rx = c.vx - wx * kW, rz = c.vz - wz * kW, ry = c.vy; float sp = sqrtf(rx * rx + ry * ry + rz * rz);
        float f = 1.0f / (1.0f + kD * sp * dt);
        c.vx = wx * kW + rx * f; c.vz = wz * kW + rz * f; c.vy = ry * f - 9.81f * dt;
        if (c.kind == kClipBlade) {   // aleteo: la hoja planea de costado (perpendicular a su avance) y se mece
            c.ph += (6.0f + 0.25f * fabsf(c.spin)) * dt;
            float hs = sqrtf(c.vx * c.vx + c.vz * c.vz) + 1e-3f, sx = -c.vz / hs, sz = c.vx / hs, fl = 2.8f * sinf(c.ph);
            c.vx += sx * fl * dt; c.vz += sz * fl * dt; c.vy += 1.4f * cosf(c.ph * 1.3f) * dt * std::min(1.f, sp);
        }
        c.x += c.vx * dt; c.y += c.vy * dt; c.z += c.vz * dt; c.rot += c.spin * dt;
        if (c.y <= 0.010f && c.vy < 0.f) {
            // toca el pasto: el terron pica (y rueda un poco), el manojo apenas, la hoja se queda; despues quedan acostados (w389 s)
            float restS = ClodRest();
            if (c.kind == kClipClod && c.vy < -1.0f) { c.y = 0.011f; c.vy = -c.vy * 0.30f; c.vx *= 0.55f; c.vz *= 0.55f; c.spin *= 0.5f; s_clips[n++] = c; continue; }
            if (c.kind == kClipTuft && c.vy < -1.6f) { c.y = 0.011f; c.vy = -c.vy * 0.15f; c.vx *= 0.4f; c.vz *= 0.4f; c.spin *= 0.4f; s_clips[n++] = c; continue; }
            if (restS < 0.1f) continue;
            if (fabsf(c.x) > 55.f || fabsf(c.z) > 36.f) continue;   // fuera de la cancha no queda nada
            c.rest = 1; c.y = c.kind == kClipBlade ? 0.012f + 0.010f * Rand01() : 0.008f; c.vx = c.vz = c.vy = 0.f; c.age = 0.f;
            c.life = restS * (0.6f + 0.8f * Rand01()) * (c.kind == kClipBlade ? 0.35f : 1.f);   // la hoja suelta se pierde entre el pasto antes
            s_clips[n++] = c; continue;
        }
        if (c.age < c.life) s_clips[n++] = c;
    }
    s_clips.resize(n);
    if (!s_clipBuf || !s_clipSRV) return;
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_dev.imm->Map(s_clipBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    float* q = (float*)m.pData;
    for (size_t i = 0; i < n; i++) { const Clip& c = s_clips[i]; float fade = std::min(1.0f, (c.life - c.age) * (c.rest ? 1.0f : (c.kind == kClipDust ? 2.0f : 4.0f)));
        q[i * 8 + 0] = c.x; q[i * 8 + 1] = c.y; q[i * 8 + 2] = c.z; q[i * 8 + 3] = c.size * fade;
        q[i * 8 + 4] = c.vx; q[i * 8 + 5] = c.vy; q[i * 8 + 6] = c.vz; q[i * 8 + 7] = fmodf(c.rot, 6.2832f) + 10.0f * (float)(c.kind + (c.rest ? 5 : 0)); }   // (2/11) w = giro + 10 * tipo; (08/11) + 50 si esta acostado en el piso
    g_dev.imm->Unmap(s_clipBuf, 0);
    s_clipCount = (int)n;
}
// desde GrassOnDraw (estado del pasto puesto: G-buffer, profundidad, mezcla): un dibujo instanciado con nuestro VS y el PS de las hojas
void PitchClipsDraw(ID3D11DeviceContext* c, int frame) {
    int n = s_clipCount.load(); if (n <= 0 || !s_clipVS || !s_clipSRV || g_cfg.live[116][3] < 0.5f) return;
    s_clipFrame = frame;   // (26/10 d) "una vez por grabacion" lo decide GrassOnDraw (el cuadro de grabacion no identifica el de la GPU)
    ID3D11VertexShader* oVS = nullptr; c->VSGetShader(&oVS, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL);
    ID3D11ShaderResourceView* oT[2] = {}; c->VSGetShaderResources(120, 1, &oT[0]); c->VSGetShaderResources(122, 1, &oT[1]);
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->VSSetShaderResources(120, 1, &prm); c->VSSetShaderResources(122, 1, &s_clipSRV);
    c->VSSetShader(s_clipVS, nullptr, 0); c->IASetInputLayout(nullptr);
    t_clipDraw = true;   // (2/11) BeforeDraw enlaza el atlas de particulas (pes_debris) en t8 / t9 en vez del de hojas
    c->DrawInstanced(6, (UINT)n, 0, 0);   // pasa por el gancho: PS de las hojas reemplazado + reglas (atlas t8/t9, t120)
    t_clipDraw = false;
    c->VSSetShader(oVS, nullptr, 0); if (oVS) oVS->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release();
    c->VSSetShaderResources(120, 1, &oT[0]); c->VSSetShaderResources(122, 1, &oT[1]); for (auto* v : oT) if (v) v->Release();
}
void PitchClipsReloadShaders() { if (s_clipVS) { s_clipVS->Release(); s_clipVS = nullptr; } s_clipTried = false; }

static void EnsureRes() {
    if (!g_dev.dev) return;
    for (int i = 0; i < kStages; i++) if (!s_stage[i]) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxPlayers * 192; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;   // (08/11 z) 8 filas de b5 por dibujo + (08/11 aa) 64 B del hueso 0 de b6. OJO (08/11 aa): desde 4c92e60 el CPUAccessFlags habia quedado dentro del comentario -> el Map fallaba y NINGUNA posicion se leia (por eso los jugadores no disparaban nada)
        g_dev.dev->CreateBuffer(&d, nullptr, &s_stage[i]);
    }
    if (!s_zeroBuf) { float z[16] = {}; D3D11_BUFFER_DESC d = {}; d.ByteWidth = 64; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE; D3D11_SUBRESOURCE_DATA sd = { z, 0, 0 }; g_dev.dev->CreateBuffer(&d, &sd, &s_zeroBuf); }
    if (!s_tex) {
        D3D11_TEXTURE2D_DESC td{}; td.Width = 1024; td.Height = 512; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R16_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (SUCCEEDED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_tex))) {
            g_dev.dev->CreateRenderTargetView(s_tex, nullptr, &s_rtv); g_dev.dev->CreateShaderResourceView(s_tex, nullptr, &s_srv);
            if (s_srv) NamedSetSRV("pes_wear", s_srv);
        }
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxPlayers * 16 + 16; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_dev.dev->CreateBuffer(&d, nullptr, &s_cb);
        D3D11_BLEND_DESC bd{}; auto& rt = bd.RenderTarget[0]; rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_ONE; rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_ONE; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        g_dev.dev->CreateBlendState(&bd, &s_bsAdd);
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; g_dev.dev->CreateRasterizerState(&rd, &s_rs);
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = FALSE; g_dev.dev->CreateDepthStencilState(&dd, &s_dss);
    }
    if (!s_tried) { s_tried = true;
        ID3DBlob* vb = CompileNamedShader(L"pes_wear_vs.hlsl", "vs_5_0"); ID3DBlob* pb = CompileNamedShader(L"pes_wear_ps.hlsl", "ps_5_0");
        if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_vs); vb->Release(); }
        if (pb) { g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_ps); pb->Release(); }
        Log("desgaste en vivo: shaders %s", (s_vs && s_ps) ? "listos" : "NO compilaron (ver errores)");
    }
}
// ---------------------------------------------------------------- pasto aplastado temporal (20/10)
// Segundo mapa ("pes_flat", R16, mismas uv que el desgaste) que se apaga solo: cada cuadro se multiplica por exp(-dt/tau)
// y se suman pinceles donde estan los jugadores y donde rueda la pelota (con el rastro entre la posicion anterior y la
// actual). Las hojas lo leen para agacharse y abrirse hacia afuera del rastro y la base de la cancha para cambiar el grano
// (pisadas y rastro de la pelota que se ven y se recuperan en ~2 s). La pelota sale del dibujo del pasto del juego:
// cb4[3] y cb4[4] del VS de las hojas son la capsula de la pelota (posicion actual y anterior, radio 0.112 m).
//   136: x on | y fuerza | z recuperacion (s) | w fuerza de la pelota
static ID3D11Texture2D* s_flatTex = nullptr; static ID3D11RenderTargetView* s_flatRTV = nullptr; static ID3D11ShaderResourceView* s_flatSRV = nullptr;
static ID3D11VertexShader* s_fsVS = nullptr; static ID3D11PixelShader* s_zeroPS = nullptr; static bool s_flatTried = false;
static ID3D11BlendState* s_bsDecay = nullptr; static ID3D11Buffer* s_flatCB = nullptr; static ID3D11BlendState* s_bsAddR = nullptr; static ID3D11BlendState* s_bsAddG = nullptr;   // (27/10 n) pinceles al canal R (jugadores) o G (rastro de la pelota)
static ID3D11BlendState* s_bsAddB = nullptr;   // (13/11 tanda 2) z448: huellas al canal B (pisadas que se borran despacio)
static ID3D11Buffer* s_ballStage[kStages] = {}; static int s_ballStageFrame[kStages] = { -1, -1, -1, -1, -1, -1, -1, -1 }; static int s_ballStamp[kStages] = { -1, -1, -1, -1, -1, -1, -1, -1 }; static int s_ballPres = 0; static std::atomic<int> s_ballW{0}; static std::atomic<int> s_ballNoted{-1};
static float s_ball[3] = {}, s_ballPrev[3] = {}; static bool s_ballOk = false; static float s_ballSpPrev = 0.f; static int s_flatFrame = 0; static double s_flatT = 0; static int s_flatMatch = -1;
static void EnsureFlat() {
    if (!g_dev.dev) return;
    if (!s_flatTex) {
        // (27/10 n) tanda 11: R = pisadas de los jugadores (se levanta en z136 s), G = rastro de la pelota (dura z329 s, 30 por defecto):
        // Agustin queria ver por donde fue la pelota; con 2 s de recuperacion no quedaba rastro
        // (13/11 tanda 2) siempre 4 canales (+2 MB): B = huellas que se borran despacio (z448 s; 0 = B en cero), A = 0
        D3D11_TEXTURE2D_DESC td{}; td.Width = 1024; td.Height = 512; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R16G16B16A16_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (SUCCEEDED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_flatTex))) {
            g_dev.dev->CreateRenderTargetView(s_flatTex, nullptr, &s_flatRTV); g_dev.dev->CreateShaderResourceView(s_flatTex, nullptr, &s_flatSRV);
            if (s_flatSRV) NamedSetSRV("pes_flat", s_flatSRV);
            if (s_flatRTV && g_dev.imm) { float z[4] = {}; g_dev.imm->ClearRenderTargetView(s_flatRTV, z); }
        }
        D3D11_BLEND_DESC bd{}; auto& rt = bd.RenderTarget[0]; rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_ZERO; rt.DestBlend = D3D11_BLEND_BLEND_FACTOR; rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ZERO; rt.DestBlendAlpha = D3D11_BLEND_BLEND_FACTOR; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        g_dev.dev->CreateBlendState(&bd, &s_bsDecay);
        { D3D11_BLEND_DESC ba{}; auto& r2 = ba.RenderTarget[0]; r2.BlendEnable = TRUE; r2.SrcBlend = D3D11_BLEND_ONE; r2.DestBlend = D3D11_BLEND_ONE; r2.BlendOp = D3D11_BLEND_OP_ADD;
          r2.SrcBlendAlpha = D3D11_BLEND_ONE; r2.DestBlendAlpha = D3D11_BLEND_ONE; r2.BlendOpAlpha = D3D11_BLEND_OP_ADD;
          r2.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED; g_dev.dev->CreateBlendState(&ba, &s_bsAddR);
          r2.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_GREEN; g_dev.dev->CreateBlendState(&ba, &s_bsAddG);
          r2.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_BLUE; g_dev.dev->CreateBlendState(&ba, &s_bsAddB); }
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxPlayers * 16 + 16; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_dev.dev->CreateBuffer(&d, nullptr, &s_flatCB);
    }
    for (int i = 0; i < kStages; i++) if (!s_ballStage[i]) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 32; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &s_ballStage[i]); }
    if (!s_flatTried) { s_flatTried = true;
        ID3DBlob* vb = CompileNamedShader(L"pes_fs_vs.hlsl", "vs_5_0"); ID3DBlob* pb = CompileNamedShader(L"pes_zero_ps.hlsl", "ps_5_0");
        if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_fsVS); vb->Release(); }
        if (pb) { g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_zeroPS); pb->Release(); }
        Log("pasto aplastado: shaders %s", (s_fsVS && s_zeroPS) ? "listos" : "NO compilaron");
    }
}
// desde GrassOnDraw (una vez por cuadro): la capsula de la pelota (cb4[3], cb4[4] del VS de las hojas) al staging
void PitchBallNote(ID3D11DeviceContext* c, int frame) {
    if (!g_dev.dev || (g_cfg.live[136][0] < 0.5f && !(g_cfg.live[139][0] > 0.5f && g_cfg.live[139][2] > 0.001f))) return;
    s_ballNoted = frame;   // (26/10 d) una vez por grabacion (lo decide GrassOnDraw)
    int w = s_ballW.load(); if (!s_ballStage[w]) return;
    ID3D11Buffer* cb = nullptr; UINT first = 0, num = 0; ID3D11DeviceContext1* c1 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) { c1->VSGetConstantBuffers1(4, 1, &cb, &first, &num); c1->Release(); }
    else c->VSGetConstantBuffers(4, 1, &cb);
    if (!cb) return;
    D3D11_BUFFER_DESC d; cb->GetDesc(&d); UINT off = first * 16 + 48;
    if (d.ByteWidth >= off + 32) { D3D11_BOX bx = { off, 0, 0, off + 32, 1, 1 }; c->CopySubresourceRegion(s_ballStage[w], 0, 0, 0, 0, cb, 0, &bx); s_ballStageFrame[w] = frame; }
    cb->Release();
}
// (11/11, Agustin: "estos deberian seguir el gameclock para que no se borren en la repeticion") RELOJ DEL JUEGO para los recortes, el
// seguimiento de los jugadores (pisadas, frenadas) y el pasto aplastado: el mismo de las nubes (fila 157, config.cpp: m_timeParam del
// juego; la pausa lo congela, la camara lenta de la repeticion lo frena, el salto hacia atras al empezarla no cuenta). En pausa los
// pedacitos quedan quietos en el aire y no se desvanecen; en la repeticion en camara lenta vuelan despacio y las acciones repetidas los
// vuelven a arrancar (la velocidad de los jugadores se mide en tiempo de juego). Hasta ver ese reloj avanzar (w157 = 0): el real.
static float GameStep(double& lastG, double& lastR) {
    const double t = Now(), g = (double)g_cfg.live[157][0];
    const double dR = lastR < 0.0 ? 0.0 : t - lastR; lastR = t;
    double dG = lastG < -1e8 ? 0.0 : g - lastG; lastG = g; if (dG < -50000.0) dG += 100000.0;   // vuelta del reloj de las nubes (100000 s)
    if (g_cfg.live[157][3] < 0.5f) return (float)std::clamp(dR, 0.0, 0.1);
    return (float)std::clamp(dG, 0.0, 0.1);
}
static int s_plGen = 0;   // (11/11) actualizaciones de s_pl: la huella de cada pisada se estampa una sola vez aunque el seguimiento se saltee cuadros (pausa)
static void PitchFlatPresent() {
    const float* F = g_cfg.live[136];
    bool v5 = g_cfg.live[139][0] > 0.5f && g_cfg.live[139][2] > 0.001f;   // (20/10) pasto v5: las hojas se doblan con el mapa
    if ((F[0] < 0.5f && !v5) || !g_dev.imm) return;
    EnsureFlat(); if (!s_flatRTV || !s_fsVS || !s_zeroPS || !s_vs || !s_ps || !s_flatCB || !s_bsAddR || !s_bsAddG) return;
    static double s_fG = -1e9, s_fR = -1.0; float dt = GameStep(s_fG, s_fR); s_flatT = Now(); s_flatFrame++;   // (11/11) reloj del juego
    static int s_flatPlGen = -1; const bool newPl = s_plGen != s_flatPlGen; s_flatPlGen = s_plGen;
    if (g_matchGen.load() != s_flatMatch) { s_flatMatch = g_matchGen.load(); float z[4] = {}; g_dev.imm->ClearRenderTargetView(s_flatRTV, z); s_ballOk = false; }
    // pelota: el staging mas viejo ya listo (anillo de 8, ver kStageMinAge)
    static int s_ballPresN = 0; int ballNewN = 0; s_ballPresN = std::min(s_ballPresN + 1, 1000);   // (13/11 tanda 2) z446: Presents desde la lectura anterior (0 = sin lectura nueva)
    { int w = s_ballW.load(); s_ballStamp[w] = s_ballStageFrame[w] >= 0 ? s_ballPres : -1; s_ballStageFrame[w] = -1; s_ballPres++;
      const int nw = (w + 1) % kStages; if (s_ballStamp[nw] >= 0) { s_ballStamp[nw] = -1; s_rdLost[2]++; } s_ballW = nw;   // (12/11 17:30) anillo de 8
      const int rd = StageOldest(s_ballStamp, s_ballPres);
      if (rd >= 0 && s_ballStage[rd]) {
          D3D11_MAPPED_SUBRESOURCE m;
          if (FAILED(g_dev.imm->Map(s_ballStage[rd], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) s_rdRetry[2]++;   // se reintenta el cuadro siguiente
          else { s_rdOk[2]++; s_ballStamp[rd] = -1;
              const float* q = (const float*)m.pData;
              bool ok = fabsf(q[0]) < 70.f && fabsf(q[2]) < 45.f && q[1] > -0.5f && q[1] < 20.f && (fabsf(q[0]) + fabsf(q[2]) > 0.01f);
              if (ok) { if (s_ballOk) { s_ballPrev[0] = s_ball[0]; s_ballPrev[1] = s_ball[1]; s_ballPrev[2] = s_ball[2]; } else { s_ballPrev[0] = q[0]; s_ballPrev[1] = q[1]; s_ballPrev[2] = q[2]; }
                        s_ball[0] = q[0]; s_ball[1] = q[1]; s_ball[2] = q[2]; s_ballOk = true; ballNewN = s_ballPresN; s_ballPresN = 0;
                        // (13/11 tanda 2) una vez, con la primera pelota que rueda: cb4[3] (este cuadro) contra cb4[4] (el anterior) y el paso de
                        // juego de un cuadro. Una velocidad razonable (1-30 m/s) confirma que cb4[4] es el cuadro anterior dibujado (prueba 8)
                        { static bool s_kinLog = false; const float ex = q[0] - q[4], ez = q[2] - q[6], dd = sqrtf(ex * ex + ez * ez);
                          if (!s_kinLog && q[1] < 0.3f && q[5] < 0.3f && dd > 0.005f && dd < 1.5f && dt > 1e-4f) {
                              s_kinLog = true; Log("pelota: recorrido %.3f m en %.4f s de juego = %.2f m/s", dd, dt, dd / dt); } } }
              g_dev.imm->Unmap(s_ballStage[rd], 0);
          }
      } }
    float brush[kMaxPlayers][4]; int nb = 0; int nbP = 0;   // (27/10 n) los primeros nbP pinceles son de jugadores (canal R), el resto de la pelota (G)
    // (13/11 tanda 2) z447 pisadas v2: las huellas van en su propio arreglo de 128 (antes compartian los 32 lugares con los pinceles base y las
    // frenadas: con 22-25 jugadores los ultimos perdian su huella en el unico cuadro que les tocaba) y se dibujan primero, de a 48. z448: las
    // huellas tambien al canal B (pisadas que se borran despacio); sin z447 se copian ahi las mismas del arreglo de siempre
    static const int kPrints = 128; float prt[kPrints][4]; int npr = 0;
    const bool ftV2 = GrassLive(447, 2) > 0.5f, slowB = GrassLive(448, 2) > 0.f && s_bsAddB != nullptr;
    auto sstep = [](float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); };
    auto addPrt = [&](float u, float v, float kk, float rr) { if (npr < kPrints) { prt[npr][0] = u; prt[npr][1] = v; prt[npr][2] = kk; prt[npr][3] = rr; npr++; } };
    float k = std::clamp(F[1] > 0.f ? F[1] : 1.0f, 0.f, 2.f);
    // jugadores (ultimas posiciones conocidas del desgaste en vivo): pisada suave
    const float kAct = std::clamp(g_cfg.live[177][1], 0.f, 2.f);   // (2/11) frenadas: pisada mas fuerte y alargada hacia atras
    for (size_t i = 0; i < s_pl.size() && (ftV2 || nb < kMaxPlayers - 16); i++) {
        const PlayerPrev& p = s_pl[i]; float sp = sqrtf(p.vx * p.vx + p.vz * p.vz);
        const float fpOn = FpOn();
        float kBase = 2.5f * dt * k * (1.f + 2.5f * p.skid * kAct) * (fpOn > 0.5f ? 0.35f : 1.f);
        // (13/11 tanda 2) z447: con sus pies a la vista la huella la hacen las pisadas (x0.25), y parado ya no deja un disco aplastado de 1 m
        // x0.25 solo mientras sus pisadas reales marcan (lastRealG): con los huesos descartados (s_ftBoneMask 0) vuelve la zancada y el pincel entero
        if (ftV2) kBase *= ((FootOn() && s_gClk - p.lastRealG < 0.9) ? 0.25f : 1.f) * (0.2f + 0.8f * sstep(0.3f, 1.5f, sp));
        if (nb < kMaxPlayers - 16) { brush[nb][0] = p.x / 140.f + 0.5f; brush[nb][1] = p.z / 79.f + 0.5f; brush[nb][2] = kBase; brush[nb][3] = 0.45f; nb++; }
        // (08/11) huella: una marca por pisada (pie izquierdo / derecho alternados, zancada segun la velocidad), con forma de botin
        // (talon mas fuerte + punta hacia adelante). La marca entera en un cuadro (no por dt): se ve cada pisada por separado
        if (ftV2) {   // (13/11 tanda 2) z447: una pisada real marca a cualquier velocidad (girando en el lugar); la punta va hacia donde mira el pie
            if (fpOn > 0.5f && p.printDue && newPl && (sp > 0.3f || p.printReal)) {
                float ux = 0.f, uz = 0.f; bool toe = true;
                if (sp > 0.3f) { ux = p.vx / sp; uz = p.vz / sp; }
                else if (p.printHx * p.printHx + p.printHz * p.printHz > 0.25f) { ux = p.printHx; uz = p.printHz; }
                else toe = false;
                const float kf = FpForce() * k * (0.55f + 0.45f * std::min(sp / 6.f, 1.f)) * (1.f + 1.5f * p.skid * kAct);
                addPrt(p.printX / 140.f + 0.5f, p.printZ / 79.f + 0.5f, 0.75f * kf * FootEx(), 0.13f * FootExR());
                if (toe) addPrt((p.printX + ux * 0.12f) / 140.f + 0.5f, (p.printZ + uz * 0.12f) / 79.f + 0.5f, 0.55f * kf * FootEx(), 0.10f * FootExR());
            }
        } else if (fpOn > 0.5f && p.printDue && newPl && sp > 0.3f && nb < kMaxPlayers - 16) {
            float ux = p.vx / sp, uz = p.vz / sp, kf = FpForce() * k * (0.55f + 0.45f * std::min(sp / 6.f, 1.f)) * (1.f + 1.5f * p.skid * kAct);
            brush[nb][0] = p.printX / 140.f + 0.5f; brush[nb][1] = p.printZ / 79.f + 0.5f; brush[nb][2] = 0.75f * kf * FootEx(); brush[nb][3] = 0.13f * FootExR(); nb++;
            if (slowB) addPrt(brush[nb - 1][0], brush[nb - 1][1], brush[nb - 1][2], brush[nb - 1][3]);
            if (nb < kMaxPlayers - 16) { brush[nb][0] = (p.printX + ux * 0.12f) / 140.f + 0.5f; brush[nb][1] = (p.printZ + uz * 0.12f) / 79.f + 0.5f; brush[nb][2] = 0.55f * kf * FootEx(); brush[nb][3] = 0.10f * FootExR(); nb++;
                if (slowB) addPrt(brush[nb - 1][0], brush[nb - 1][1], brush[nb - 1][2], brush[nb - 1][3]); }
        }
        if (p.skid > 0.2f && kAct > 0.01f && sp > 1.f && nb < kMaxPlayers - 16) {   // marca de frenada: 2 pinceles detras, en la direccion de la que venia
            for (int e = 1; e <= 2 && nb < kMaxPlayers - 16; e++) { float bx = p.x - p.vx / sp * 0.35f * e, bz = p.z - p.vz / sp * 0.35f * e;
                brush[nb][0] = bx / 140.f + 0.5f; brush[nb][1] = bz / 79.f + 0.5f; brush[nb][2] = 2.0f * dt * k * p.skid * kAct; brush[nb][3] = 0.35f; nb++; }
        }
    }
    nbP = nb;
    // pelota en el piso: rastro entre la posicion anterior y la actual
    if (s_ballOk) {
        float onGround = std::clamp(1.0f - (s_ball[1] - 0.12f) / 0.25f, 0.0f, 1.0f);
        // (13/11 tanda 2) x446 ZONA DE LA PELOTA (con y446 "pelota v2"): los pinceles del rastro y del pique miden S veces mas (y marcan 1/S), para
        // que el BORDE del mapa siga a la zona viva del VS de las hojas. El reconocimiento de la pelota (0.3 m, 6 cm) y la prueba del piso no cambian
        const float zS = GrassLive(446, 1) > 0.5f ? std::clamp(GrassLive(446, 0), 1.f, 2.f) : 1.f;
        // (27/10 n) pique: la pelota venia del aire (> 40 cm) y toca el piso: marca redonda mas fuerte
        if (onGround > 0.5f && s_ballPrev[1] > 0.4f && nb < kMaxPlayers) {
            brush[nb][0] = s_ball[0] / 140.f + 0.5f; brush[nb][1] = s_ball[2] / 79.f + 0.5f; brush[nb][2] = 0.6f * k * (F[3] > 0.f ? F[3] : 1.0f) / zS; brush[nb][3] = 0.26f * zS; nb++;
        }
        float dx = s_ball[0] - s_ballPrev[0], dz = s_ball[2] - s_ballPrev[2]; float dist = sqrtf(dx * dx + dz * dz);
        if (dist > 4.0f) dist = 0.0f;   // salto de camara / saque
        int steps = std::clamp((int)(dist / 0.08f) + 1, 1, 15);
        // (13/11 tanda 2) z446: el tramo se estampa UNA vez por lectura nueva (antes en cada Present hasta la lectura siguiente: lo marcado
        // dependia de cuantas lecturas llegaban y el rastro se saturaba), con la fuerza x los Presents desde la lectura anterior (hasta 6); la
        // pelota quieta (< 1 cm) x0.25, asi el disco aplastado deja de crecer. 0 = como antes
        const bool segOnce = GrassLive(446, 2) > 0.f; const int segN = std::min(ballNewN, 6);
        const float segK = !segOnce ? 1.f : (segN > 0 ? (float)segN * (dist < 0.01f ? 0.25f : 1.f) : 0.f);
        const float dtSp = (segOnce && segN > 0) ? dt * (float)segN : dt;   // z446: la velocidad sobre el tiempo del tramo entero
        float kb = (F[3] > 0.f ? F[3] : 1.0f) * k * onGround;
        { const float kv = std::clamp(g_cfg.live[177][2], 0.f, 2.f); float spd = dtSp > 0.002f ? std::min(dist / dtSp / 12.f, 1.5f) : 0.f;   // (2/11) rapida: aplasta mas; lenta: menos
          kb *= std::max(0.25f, 1.f + kv * (spd - 0.5f)); }
        // (2/11) manchas de la pelota (fila 177 x). Agustin: PROGRESIVAS y MUY SUTILES, con los minutos de partido y segun la humedad de la
        // cancha; nada por contacto. La pelota solo "recuerda" por donde rueda (promedios lentos de tierra bajo ella y de cruces de cal) y
        // la mancha crece con el reloj: verde ~ 25 min, tierra ~ 30 min (x2 con cancha humeda), cal ~ 20 min (menos con humedad)
        { const float kS = std::clamp(g_cfg.live[177][0], 0.f, 2.f);
          if (kS > 0.001f) {
              static float wearEma = 0.f, lineEma = 0.f;
              float roll = (dist > 0.005f && dist < 4.f) ? onGround : 0.f;
              if (roll > 0.f) { float a = std::min(1.f, dt / 20.f);   // promedios de ~20 s de rodado
                  wearEma += (std::clamp(WearCpuAt(s_ball[0], s_ball[2]), 0.f, 1.f) - wearEma) * a;
                  lineEma += ((LineDist(s_ball[0], s_ball[2]) < 0.12f ? 1.f : 0.f) - lineEma) * a; }
              float mins = std::max(g_cfg.live[10][0], 0.f);                   // minutos reales de partido (0 fuera del partido)
              float wet = g_cfg.live[114][0] > 0.5f && g_cfg.live[114][0] < 1.5f ? 1.f : 0.f, dry = g_cfg.live[114][0] > 1.5f ? 1.f : 0.f;
              float tg = 0.30f * (1.f - expf(-mins / 25.f)) * (1.f + 0.6f * wet);
              float td = 0.35f * (1.f - expf(-mins / 30.f)) * (0.3f + 0.7f * wearEma) * (1.f + 1.5f * wet + 0.3f * dry);
              float tc = 0.20f * (1.f - expf(-mins / 20.f)) * (0.2f + 0.8f * std::min(lineEma * 6.f, 1.f)) * (1.f - 0.7f * wet);
              float a2 = std::min(1.f, dt / 8.f);                                // se acerca al objetivo en ~8 s: sin saltos
              s_stain[0] += (tg - s_stain[0]) * a2; s_stain[1] += (td - s_stain[1]) * a2; s_stain[2] += (tc - s_stain[2]) * a2;
              if (mins < 0.01f) { wearEma *= 0.99f; lineEma *= 0.99f; }
              for (float& v : s_stain) v = std::clamp(v, 0.f, 1.f);
          } else s_stain[0] = s_stain[1] = s_stain[2] = 0.f; }
        for (int sIdx = 0; sIdx < steps && nb < kMaxPlayers && segK > 0.f; sIdx++) {
            float a = steps > 1 ? (float)sIdx / (steps - 1) : 1.0f;
            float bx = s_ballPrev[0] + dx * a, bz = s_ballPrev[2] + dz * a;
            brush[nb][0] = bx / 140.f + 0.5f; brush[nb][1] = bz / 79.f + 0.5f; brush[nb][2] = std::min(0.2f, 0.04f + 2.5f * dt) * kb * segK / zS; brush[nb][3] = 0.21f * zS; nb++;   // (2/11) Agustin: que marque mas al rodar. (2/11 c) antes 0.12 + 9 dt (~0.27 por cuadro): saturaba el rastro y el pasto quedaba 'aplastado todo el tiempo'
        }
        // (08/11, Agustin: "desprendimiento ... chutando") REMATE: la pelota pasa de lenta a rapida en un cuadro, estando en el piso.
        // El pie de apoyo y el botin arrancan pasto detras de donde estaba la pelota, hacia adelante (el golpe) y un poco hacia los costados
        float sp = dt > 0.002f ? dist / dt : 0.f;   // (11/11) en pausa (reloj del juego quieto) no hay remate
        { float rk = ReactK(); float thr = 9.0f / rk, jump = 5.0f / rk;
          if (sp > thr && sp - s_ballSpPrev > jump && sp < 60.f && s_ballPrev[1] < 0.35f && dist > 0.01f) {
              float ix = dx / dist, iz = dz / dist; float strength = std::min((sp - thr) / 14.f + 0.5f, 2.0f) * rk;
              // (11/11) recortes v2: el botin raspa el pasto debajo / detras de la pelota y salen hacia donde va el tiro
              EmitStrike(s_ballPrev[0] - ix * 0.25f, s_ballPrev[2] - iz * 0.25f, ix, iz, std::min(0.30f + 0.35f * strength, 1.2f), true);
              // (12/11) desgaste real: el pie de apoyo (al costado y algo detras de la pelota) y el raspon del botin debajo de la pelota
              if (WearReal() && g_cfg.live[115][3] > 0.5f && SimGate() > 0.5f) {
                  const float aK = WearActK(), rt = (g_cfg.live[115][0] > 0.f ? g_cfg.live[115][0] : 1.f) * WearBoost(), st = std::min(strength, 1.6f);
                  const float sg = Rand01() < 0.5f ? -1.f : 1.f, px = -iz * sg, pz = ix * sg;
                  WearStamp(s_ballPrev[0] - ix * 0.30f + px * 0.22f, s_ballPrev[2] - iz * 0.30f + pz * 0.22f, 0.045f * aK * st * rt, 0.15f);
                  for (int e = 0; e < 3; e++) { float a = -0.35f + 0.15f * e; WearStamp(s_ballPrev[0] + ix * a, s_ballPrev[2] + iz * a, 0.020f * aK * st * rt, 0.11f); }
              }
              static int kickLog = 0; if (kickLog < 3) { kickLog++; Log("pasto: remate a %.1f m/s (antes %.1f): terrones en (%.1f, %.1f)", sp, s_ballSpPrev, s_ballPrev[0], s_ballPrev[2]); }
          }
          s_ballSpPrev = (dist > 0.f && sp < 60.f) ? sp : 0.f; }
        // (11/11) recortes v2: la pelota rodando ya no tira pasto (antes un recorte el 60 % de los cuadros a mas de 9 m/s); solo el remate
    }
    ID3D11DeviceContext* c = g_dev.imm;
    ID3D11RenderTargetView* oRT[8] = {}; ID3D11DepthStencilView* oDS = nullptr; c->OMGetRenderTargets(8, oRT, &oDS);
    UINT nvp = 8; D3D11_VIEWPORT ovp[8]; c->RSGetViewports(&nvp, ovp);
    ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr;
    c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL); D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS); ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDSS, &oRef);
    ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0; c->OMGetBlendState(&oBS, oBF, &oSM);
    ID3D11Buffer* oCB = nullptr; c->VSGetConstantBuffers(0, 1, &oCB);
    c->OMSetRenderTargets(1, &s_flatRTV, nullptr); D3D11_VIEWPORT vp{ 0, 0, 1024, 512, 0, 1 }; c->RSSetViewports(1, &vp);
    c->GSSetShader(nullptr, nullptr, 0); c->IASetInputLayout(nullptr);
    c->RSSetState(s_rs); c->OMSetDepthStencilState(s_dss, 0);
    // 1) se apaga de a poco: dest *= exp(-dt / tau)
    float tau = std::clamp(F[2] > 0.1f ? F[2] : 2.0f, 0.3f, 10.0f); float f = expf(-dt / tau);
    // (27/10 n) el rastro de la pelota (canal G) se desvanece en z329 s (0 = 30)
    float tauB = std::clamp(g_cfg.live[329][2] > 0.5f ? g_cfg.live[329][2] : 30.0f, 1.0f, 300.0f); float fB = expf(-dt / tauB); float bfD[4] = { f, fB, fB, fB };
    // (13/11 tanda 2) z448 PISADAS QUE SE BORRAN DESPACIO: el canal B (huellas) se apaga en z448 s, mas lento con la cancha mojada y mas
    // rapido con el sol de hora pico (w414); z448 0 = B en cero cada cuadro (los shaders leen 0, como antes)
    { const float z448 = GrassLive(448, 2);
      const float tauP = std::clamp(z448, 0.5f, 120.f) * (1.f + 0.5f * WetNow()) * (1.f - 0.3f * std::clamp(g_cfg.live[414][3], 0.f, 1.f));
      // en tramos de >= 0.4 %: un factor por cuadro tan cerca de 1 se redondea en el UNORM de 16 bits y B quedaba clavado (~0.04 con 90 s)
      static float s_bAcc = 0.f; s_bAcc += dt; float fP = 1.f;
      if (z448 > 0.f) { const float e = expf(-s_bAcc / tauP); if (e <= 0.996f) { fP = e; s_bAcc = 0.f; } } else s_bAcc = 0.f;
      bfD[2] = (z448 > 0.f) ? fP : 0.f; }
    c->VSSetShader(s_fsVS, nullptr, 0); c->PSSetShader(s_zeroPS, nullptr, 0); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->OMSetBlendState(s_bsDecay, bfD, 0xffffffff); c->Draw(3, 0);
    // (13/11 tanda 2) huellas primero, de a 48 (el cbuffer del VS): con z447 al canal R (ya no van en brush), con z448 tambien al B
    if (npr > 0 && (ftV2 || slowB)) {
        float bf[4] = {};
        c->VSSetShader(s_vs, nullptr, 0); c->PSSetShader(s_ps, nullptr, 0); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        c->VSSetConstantBuffers(0, 1, &s_flatCB);
        for (int b0 = 0; b0 < npr; b0 += kMaxPlayers) {
            const int nn = std::min(npr - b0, kMaxPlayers);
            D3D11_MAPPED_SUBRESOURCE mp; if (FAILED(c->Map(s_flatCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) break;
            float* cbp = (float*)mp.pData; cbp[0] = (float)nn; cbp[1] = 140.f; cbp[2] = 79.f; cbp[3] = 0.f; memcpy(cbp + 4, &prt[b0][0], nn * 16); c->Unmap(s_flatCB, 0);
            if (ftV2) { c->OMSetBlendState(s_bsAddR, bf, 0xffffffff); c->DrawInstanced(4, (UINT)nn, 0, 0); }
            if (slowB) { c->OMSetBlendState(s_bsAddB, bf, 0xffffffff); c->DrawInstanced(4, (UINT)nn, 0, 0); }
        }
    }
    // 2) pinceles nuevos (suma)
    if (nb > 0) {
        D3D11_MAPPED_SUBRESOURCE mc;
        if (SUCCEEDED(c->Map(s_flatCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) {
            float* cbp = (float*)mc.pData; cbp[0] = (float)nb; cbp[1] = 140.f; cbp[2] = 79.f; cbp[3] = 0.f; memcpy(cbp + 4, brush, nb * 16); c->Unmap(s_flatCB, 0);
            float bf[4] = {};
            c->VSSetShader(s_vs, nullptr, 0); c->PSSetShader(s_ps, nullptr, 0); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            c->VSSetConstantBuffers(0, 1, &s_flatCB);
            // (27/10 n) jugadores al canal R, pelota al canal G (el pincel escribe los 4 canales; la mascara elige)
            // (SV_InstanceID arranca en 0 en cada dibujo: el primer pincel de la pelota va en hdr.w y el VS lo suma)
            if (nbP > 0) { c->OMSetBlendState(s_bsAddR, bf, 0xffffffff); c->DrawInstanced(4, (UINT)nbP, 0, 0); }
            if (nb > nbP) {
                if (SUCCEEDED(c->Map(s_flatCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) {
                    float* cb2 = (float*)mc.pData; cb2[0] = (float)nb; cb2[1] = 140.f; cb2[2] = 79.f; cb2[3] = (float)nbP; memcpy(cb2 + 4, brush, nb * 16); c->Unmap(s_flatCB, 0);
                    c->OMSetBlendState(s_bsAddG, bf, 0xffffffff); c->DrawInstanced(4, (UINT)(nb - nbP), 0, 0);
                }
            }
        }
    }
    c->OMSetRenderTargets(8, oRT, oDS); for (auto* v : oRT) if (v) v->Release(); if (oDS) oDS->Release();
    c->RSSetViewports(nvp, ovp);
    c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0); if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release(); c->IASetPrimitiveTopology(oTopo);
    c->RSSetState(oRS); if (oRS) oRS->Release(); c->OMSetDepthStencilState(oDSS, oRef); if (oDSS) oDSS->Release();
    c->OMSetBlendState(oBS, oBF, oSM); if (oBS) oBS->Release();
    c->VSSetConstantBuffers(0, 1, &oCB); if (oCB) oCB->Release();
    static int logged = 0; if (logged < 2 && s_ballOk) { logged++; Log("pasto aplastado: pelota en (%.1f, %.2f, %.1f), %d pinceles", s_ball[0], s_ball[1], s_ball[2], nb); }
}

void PitchClipsReloadShaders();
void PitchWearReloadShaders() { if (s_vs) { s_vs->Release(); s_vs = nullptr; } if (s_ps) { s_ps->Release(); s_ps = nullptr; } s_tried = false; if (s_fsVS) { s_fsVS->Release(); s_fsVS = nullptr; } if (s_zeroPS) { s_zeroPS->Release(); s_zeroPS = nullptr; } s_flatTried = false; PitchClipsReloadShaders(); }

// en el dibujo del short / medias: copiar la traslacion de m_world (b5 del VS, fila 3 = bytes 112..127) al staging del cuadro
void PitchWearNotePlayer(ID3D11DeviceContext* c) {
    if (!g_dev.dev || g_cfg.live[115][3] < 0.5f) return;
    s_noteCalls++;
    int w = s_stageW.load(); if (!s_stage[w]) { s_noteNoStage++; return; }
    int i = s_count.fetch_add(1); if (i >= kMaxPlayers) return;
    ID3D11Buffer* cb = nullptr; UINT first = 0, num = 0; ID3D11DeviceContext1* c1 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) { c1->VSGetConstantBuffers1(5, 1, &cb, &first, &num); c1->Release(); }
    else c->VSGetConstantBuffers(5, 1, &cb);
    if (!cb) return;
    D3D11_BUFFER_DESC d; cb->GetDesc(&d); UINT off = first * 16;
    // (08/11 z) se copian las 8 primeras filas de b5 (128 B) y en Present se elige la que parece una posicion de la cancha (fila 7, fila 3 o la
    // columna w de las filas 4..6, como con la pelota): antes solo la fila 7 y, segun la malla / el mod de kits, la posicion caia fuera de rango
    if (d.ByteWidth >= off + 128) { D3D11_BOX bx = { off, 0, 0, off + 128, 1, 1 }; c->CopySubresourceRegion(s_stage[w], 0, (UINT)i * 192, 0, 0, cb, 0, &bx); s_noteOk++; }
    else s_noteShort++;
    cb->Release();
    // (08/11 aa, Agustin: "los jugadores no disparan nada") el cuerpo va con esqueleto (VSBones b6, float4x3 m_boneMatrices[32]): si m_world
    // (b5) es la identidad la posicion del jugador esta en el hueso 0 (pelvis). Se copian sus 3 registros (48 B; la traslacion es la columna w,
    // como lee sb0 el VS del cuerpo 1c02) y en Present se calcula m_world * hueso0. Si no hay b6 (malla rigida) quedan los candidatos de b5.
    { ID3D11Buffer* bb = nullptr; UINT f6 = 0, n6 = 0; ID3D11DeviceContext1* c1b = nullptr;
      if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1b)) && c1b) { c1b->VSGetConstantBuffers1(6, 1, &bb, &f6, &n6); c1b->Release(); }
      else c->VSGetConstantBuffers(6, 1, &bb);
      D3D11_BOX bz = { 0, 0, 0, 64, 1, 1 };
      if (bb) { D3D11_BUFFER_DESC d6; bb->GetDesc(&d6); UINT o6 = f6 * 16;
          if (d6.ByteWidth >= o6 + 48) { D3D11_BOX b6 = { o6, 0, 0, o6 + 48, 1, 1 }; c->CopySubresourceRegion(s_stage[w], 0, (UINT)i * 192 + 128, 0, 0, bb, 0, &b6); s_noteBone++; }
          else s_boneShort++;
          bb->Release(); }
      else { s_boneNone++; if (s_zeroBuf) c->CopySubresourceRegion(s_stage[w], 0, (UINT)i * 192 + 128, 0, 0, s_zeroBuf, 0, &bz); } }
}

// (x505) copia del botin al pozo, en el contexto del dibujo (puede ser diferido): 3 copias en la GPU, sin leer nada en la CPU
static void BootColCopy(ID3D11DeviceContext* c, UINT verts) {
    s_colCalls++;
    if (!s_colReady.load(std::memory_order_acquire) || s_colDev != g_dev.dev) return;
    if (s_colFar.load(std::memory_order_relaxed)) { s_colFarSkip++; return; }   // nadie a menos de D + 3 m de la camara
    const UINT bi = s_ballIdx.load(std::memory_order_relaxed);
    if (bi != 0 && verts == bi && s_ballIdxVotes.load(std::memory_order_relaxed) >= 3) { s_colBall++; return; }   // la pelota no es un botin
    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) || !c1) { s_colNoCb++; return; }
    ID3D11Buffer* b5 = nullptr; ID3D11Buffer* b6 = nullptr; UINT f5 = 0, n5 = 0, f6 = 0, n6 = 0;
    c1->VSGetConstantBuffers1(5, 1, &b5, &f5, &n5); c1->VSGetConstantBuffers1(6, 1, &b6, &f6, &n6); c1->Release();
    if (b5 && b6) {
        D3D11_BUFFER_DESC d5, d6; b5->GetDesc(&d5); b6->GetDesc(&d6);
        const UINT a5 = f5 * 16 + 64, a6 = f6 * 16;   // b5 c4..c8 (m_world + m_useWeightCount); b6 desde el primer hueso
        const UINT room6 = d6.ByteWidth > a6 ? d6.ByteWidth - a6 : 0u, win6 = n6 ? (std::min)(n6 * 16u, room6) : room6;
        const UINT nB = (std::min)(4u, win6 / 48u);
        const bool prevOk = win6 >= 108u * 16u;   // g_vsPrevBone en c96: huesos 0..3 del cuadro anterior dentro de la ventana
        if (a5 + 80 <= d5.ByteWidth && (n5 == 0 || n5 * 16 >= 144) && nB > 0) {
            const UINT slot = s_colAt.fetch_add(1) % kColSlots, base = slot * kColF4 * 16;
            D3D11_BOX x5 = { a5, 0, 0, a5 + 80, 1, 1 }; c->CopySubresourceRegion(s_colRaw, 0, base + 2 * 16, 0, 0, b5, 0, &x5);
            const UINT len6 = prevOk ? 108u * 16u : nB * 48u;
            D3D11_BOX x6 = { a6, 0, 0, a6 + len6, 1, 1 }; c->CopySubresourceRegion(s_colRaw, 0, base + 7 * 16, 0, 0, b6, 0, &x6);
            const UINT e = (nB - 1) * 2 + (prevOk ? 1u : 0u);
            D3D11_BOX xh = { e * 32, 0, 0, e * 32 + 32, 1, 1 }; c->CopySubresourceRegion(s_colRaw, 0, base, 0, 0, s_colGen, 0, &xh);   // sello del cuadro (al final)
            s_colCopies++; if (!prevOk) s_colNoPrev++;
        } else s_colNoCb++;
    } else s_colNoCb++;
    if (b5) b5->Release(); if (b6) b6->Release();
}

// (2/11) desde BeforeDraw, en cada dibujo del PS de botines / pelota (aadffecf): anota la traslacion de su m_world (b5 fila 3) con el
// indice del dibujo para reconocer la pelota en Present; devuelve el b12 a enlazar si este dibujo ya es "la pelota" (con manchas) o nullptr
ID3D11Buffer* PitchBallDrawNote(ID3D11DeviceContext* c, UINT verts) {
    if (!g_dev.dev || verts == 0) return nullptr;
    const bool ballOn = g_cfg.live[177][0] >= 0.001f || g_cfg.live[443][0] >= 0.001f, bootOn = BootAmt() >= 0.001f;   // (11/11 4wp0pj) tambien se anota para los botines sucios; (12/11 20:30) y para x443 (pasto delante de la pelota: la marca RT2.w de la pelota)
    const bool footOn = FootOn();   // (12/11 16:50) y para las pisadas reales
    const bool colOn = BootColOn();   // (13/11) x505 botines en el pasto
    s_bdCalls++; if (!ballOn && !bootOn && !footOn && !colOn) { s_bdOffX177++; return nullptr; }
    UINT bi = s_ballIdx.load(std::memory_order_relaxed); int slot = -1; const UINT key = bootOn ? BootKey(c, verts) : 0u;
    {   // (2/11 paso 10) se anota siempre (antes se dejaba de anotar con 3 votos y un botin votado quedaba fijo para todo el partido)
        int w = s_bdW.load(); if (!s_bdStage[w]) s_bdNoStage++; else { int i = s_bdCount.fetch_add(1);
            if (i >= kBallSlots) s_bdOverflow++; else { slot = i; ID3D11Buffer* cb = nullptr; UINT first = 0, num = 0; ID3D11DeviceContext1* c1 = nullptr;
                if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) { c1->VSGetConstantBuffers1(5, 1, &cb, &first, &num); c1->Release(); } else c->VSGetConstantBuffers(5, 1, &cb);
                // (2/11 e) se copian las 8 primeras filas de b5 (128 B): la traslacion puede estar en la fila 3, en la 7 o en la columna w de las
                // filas 4..6 (matriz traspuesta, como la del pasto); antes solo la fila 7 y la pelota nunca se reconocio
                if (!cb) s_bdNoCb++; else { D3D11_BUFFER_DESC d; cb->GetDesc(&d); UINT off = first * 16;
                    if (d.ByteWidth >= off + 128) { D3D11_BOX bx = { off, 0, 0, off + 128, 1, 1 }; c->CopySubresourceRegion(s_bdStage[w], 0, (UINT)i * 128, 0, 0, cb, 0, &bx); s_bdIdx[w][i] = verts; s_bdKey[w][i] = key; s_bdNoted++; }
                    else s_bdNoCb++;
                    cb->Release(); }
                s_ftOk[w][i] = 0;   // (12/11 16:50) pisadas reales: los huesos del botin (s_ftOk = cuantos)
                if ((footOn || colOn) && s_ftStage[w]) { ID3D11Buffer* bb = nullptr; UINT f6 = 0, n6 = 0; ID3D11DeviceContext1* c1b = nullptr;
                    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1b)) && c1b) { c1b->VSGetConstantBuffers1(6, 1, &bb, &f6, &n6); c1b->Release(); } else c->VSGetConstantBuffers(6, 1, &bb);
                    if (bb) { D3D11_BUFFER_DESC d6; bb->GetDesc(&d6); const UINT o6 = f6 * 16;
                        UINT nB = d6.ByteWidth > o6 ? (std::min)(kFtBones, (d6.ByteWidth - o6) / 48u) : 0u;
                        if (n6 > 0) nB = (std::min)(nB, n6 * 16u / 48u);   // (12/11 17:30) no leer fuera de la ventana enlazada
                        if (nB > 0) { D3D11_BOX b6 = { o6, 0, 0, o6 + nB * 48u, 1, 1 }; c->CopySubresourceRegion(s_ftStage[w], 0, (UINT)i * kFtSlotB, 0, 0, bb, 0, &b6); s_ftOk[w][i] = (UINT8)nB; }
                        bb->Release(); }
                    if (!s_ftOk[w][i]) s_ftNoB6++; } } }
    }
    if (colOn) BootColCopy(c, verts);   // (13/11) x505 botines en el pasto (solo se ve)
    if (ballOn && bi != 0 && verts == bi && s_ballIdxVotes.load(std::memory_order_relaxed) >= 3) return s_ballCb;   // (2/11 paso 10) b12 solo con el indice confirmado
    if (!bootOn) return nullptr;
    // (11/11 4wp0pj) botin: el valor del jugador asignado a este mismo orden de dibujo hace 2 cuadros (si la clave coincide); si no, el promedio
    if (slot >= 0 && s_bootCb[slot] && s_bootKey[slot] == key) { s_bootHit++; return s_bootCb[slot]; }
    s_bootAvgN++; return s_bootAvgCb;
}
// (13/11 tanda 2) z447 PISADAS v2: cada pie es una pista (id estable del jugador + hueso). gF = reloj del juego (s_gClk) del cuadro leido,
// tf = reloj real. Pisada = en el aire -> apoyado, solo en los huesos que pasaron la autoprueba (s_ftBoneMask; hasta la primera, todos)
static void FootTracksV2(const std::vector<FootObs>& obs, double gF, double tf, bool stamp) {   // (13/11) stamp = false: solo aprende (x505 sin pisadas)
    s_ftTrkUpd++;
    if (s_ftTestT < 0.0) s_ftTestT = tf;
    for (const FootObs& o : obs) {
        if (o.pl < 0 || o.pl >= (int)s_pl.size() || o.bone < 0 || o.bone > 3) continue;
        PlayerPrev& P = s_pl[o.pl]; if (P.id < 0) continue;
        P.feetG = gF;   // sus pies a la vista (pincel base x0.25, log)
        const float sv = sqrtf(P.svx * P.svx + P.svz * P.svz); const bool mov = P.trk >= 4 && sv > 1.5f;
        FootTrk* T = nullptr; for (FootTrk& t : s_ftTrk) if (t.pid == P.id && t.bone == o.bone) { T = &t; break; }
        if (!T) { if (s_ftTrk.size() < 256) s_ftTrk.push_back(FootTrk{ P.id, o.bone, 0, s_ftTrkUpd, 0, o.cx, o.cz, o.cy, o.cy, o.cy, o.cy, gF, tf }); continue; }
        if (T->upd == s_ftTrkUpd) continue;   // el mismo pie otra vez en este cuadro (otro dibujo)
        T->upd = s_ftTrkUpd; T->seenR = tf;
        const double dtg = gF - T->g;
        if (dtg < 0.002) continue;   // pausa: nada cambia (la posicion vieja queda para medir la velocidad despues)
        if (dtg > 0.3) { T->st = 0; T->base = o.cy; }   // hueco largo: se empieza de nuevo
        else {
            const float dx = o.cx - T->cx, dz = o.cz - T->cz, vF = sqrtf(dx * dx + dz * dz) / (float)dtg;
            T->base = std::min(o.cy, T->base + 0.03f * (float)dtg);   // el piso de este pie: baja enseguida, sube 3 cm por s de juego
            const bool stance = o.cy < T->base + 0.025f && vF < 0.8f, swing = vF > 1.2f || o.cy > T->base + 0.04f;
            const int ns = stance ? 1 : (swing ? 2 : T->st);
            if (mov) { s_ftStN[o.bone]++; if (ns == 1) s_ftStIn[o.bone]++; T->yLo = std::min(T->yLo, o.cy); T->yHi = std::max(T->yHi, o.cy); T->n++;
                if (o.fx != 0.f || o.fz != 0.f) { const float d = (o.fx * P.svx + o.fz * P.svz) / sv; s_ftFwdAcc += (std::clamp(d, -1.f, 1.f) - s_ftFwdAcc) * 0.01f; } }
            if (stamp && T->st == 2 && ns == 1 && ((s_ftBoneMask >> o.bone) & 1)) {
                // el talon y la punta (otro hueso del mismo jugador) a menos de 25 cm y 0.4 s de juego son UNA pisada
                bool dupS = false;
                for (const FootStrike& s : s_ftStrikes) if (s.pid == P.id && s.bone != o.bone && gF - s.g < 0.4) { const float ex = s.x - o.px, ez = s.z - o.pz; if (ex * ex + ez * ez < 0.25f * 0.25f) { dupS = true; break; } }
                if (!dupS) { const float sg = s_ftFwdAcc >= 0.f ? 1.f : -1.f, okF = fabsf(s_ftFwdAcc) > 0.3f ? 1.f : 0.f;   // rumbo solo si el frente del hueso ya se aprendio
                    FootStrike fs{ o.px, o.pz, tf, 0 }; fs.g = gF; fs.pid = P.id; fs.bone = o.bone; fs.hx = o.fx * sg * okF; fs.hz = o.fz * sg * okF;
                    s_ftStrikes.push_back(fs); s_ftStrikeT = tf; s_ftLogStrikes++; }
            }
            T->st = ns;
        }
        T->cx = o.cx; T->cz = o.cz; T->y = o.cy; T->g = gF;
    }
    s_ftTrk.erase(std::remove_if(s_ftTrk.begin(), s_ftTrk.end(), [tf](const FootTrk& t) { return tf - t.seenR > 2.0; }), s_ftTrk.end());   // pies que no se ven hace 2 s
    // autoprueba cada 5 s: un hueso es pie si, con su jugador corriendo, esta apoyado el 20-75 % del tiempo y sube y baja mas de 5 cm
    if (tf - s_ftTestT >= 5.0) { s_ftTestT = tf;
        for (FootTrk& t : s_ftTrk) { if (t.n >= 8) { s_ftRngSum[t.bone] += t.yHi - t.yLo; s_ftRngN[t.bone]++; } t.yLo = t.yHi = t.y; t.n = 0; }
        int mask = 0; bool any = false;
        for (int b = 0; b < 4; b++) {
            const float pct = s_ftStN[b] > 0 ? (float)s_ftStIn[b] / (float)s_ftStN[b] : 0.f, rng = s_ftRngN[b] > 0 ? s_ftRngSum[b] / (float)s_ftRngN[b] : 0.f;
            s_ftStPct[b] = pct; s_ftRngCm[b] = rng * 100.f;
            if (s_ftStN[b] >= 30) { any = true; if (pct >= 0.2f && pct <= 0.75f && rng > 0.05f) mask |= 1 << b; }
            s_ftStN[b] = s_ftStIn[b] = s_ftRngN[b] = 0; s_ftRngSum[b] = 0.f; }
        if (any) { static int maskLogs = 0;
            if (mask != s_ftBoneMask && maskLogs < 10) { maskLogs++; Log("pisadas v2: huesos que son pies %c%c%c%c (apoyado %.0f/%.0f/%.0f/%.0f %%, sube y baja %.1f/%.1f/%.1f/%.1f cm)%s",
                (mask & 1) ? 'S' : '-', (mask & 2) ? 'S' : '-', (mask & 4) ? 'S' : '-', (mask & 8) ? 'S' : '-', 100.f * s_ftStPct[0], 100.f * s_ftStPct[1], 100.f * s_ftStPct[2], 100.f * s_ftStPct[3],
                s_ftRngCm[0], s_ftRngCm[1], s_ftRngCm[2], s_ftRngCm[3], mask ? "" : ": ninguno, vuelve la zancada"); }
            s_ftBoneMask = mask; } }
}
static void BallDrawPresent() {
    if (!g_dev.dev || !g_dev.imm) return;
    for (int i = 0; i < kStages; i++) if (!s_bdStage[i]) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 128 * kBallSlots; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &s_bdStage[i]); }
    if (FootOn() || BootColOn()) for (int i = 0; i < kStages; i++) if (!s_ftStage[i]) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = kFtSlotB * kBallSlots; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &s_ftStage[i]); }   // (12/11 16:50)
    if (!s_ballCb) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 16; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; g_dev.dev->CreateBuffer(&d, nullptr, &s_ballCb); }
    if (!s_bootAvgCb) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 16; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; D3D11_SUBRESOURCE_DATA sd = { s_bootAvgV, 0, 0 }; g_dev.dev->CreateBuffer(&d, &sd, &s_bootAvgCb); }   // (11/11 4wp0pj)
    // publicar las manchas (b12: x 1 pelota, y verde, z tierra, w cal), escaladas por x177
    if (s_ballCb) { const float kS = std::clamp(g_cfg.live[177][0], 0.f, 2.f); float v[4] = { 1.f, std::min(1.f, s_stain[0] * kS), std::min(1.f, s_stain[1] * kS), std::min(1.f, s_stain[2] * kS) }; g_dev.imm->UpdateSubresource(s_ballCb, 0, nullptr, v, 0, 0); }
    // anillo de 8 como el desgaste: leer el mas viejo listo y comparar con la pelota del pasto
    int w = s_bdW.load(); int n = std::min(s_bdCount.exchange(0), kBallSlots); s_bdFrame[w] = n > 0 ? s_bdPres : -1; s_bdPres++; s_bdG[w] = s_gClk;   // (13/11 tanda 2) reloj del juego de este cuadro (pisadas v2)
    {   // (12/11 17:30) anillo de 8: el que se va a escribir ahora, si no se llego a leer, se pierde (y se limpia)
        const int nw = (w + 1) % kStages; if (s_bdFrame[nw] >= 0) s_rdLost[1]++;
        s_bdFrame[nw] = -1; memset(s_bdIdx[nw], 0, sizeof s_bdIdx[nw]); memset(s_bdKey[nw], 0, sizeof s_bdKey[nw]); memset(s_ftOk[nw], 0, sizeof s_ftOk[nw]); s_bdW = nw; }
    const int rd0 = StageOldest(s_bdFrame, s_bdPres); const int rd = rd0 >= 0 ? rd0 : (w + 1) % kStages; const int pending = rd0 >= 0 ? s_bdFrame[rd0] : -1;
    // (2/11 b) diagnostico mientras no se reconoce: cada 30 s, hasta 4 veces por partido, por que no se anota o no se lee
    { static int presents = 0, noPending = 0, noBall = 0, mapFail = 0, logs = 0, gen = -1;
      if (g_matchGen.load() != gen) { gen = g_matchGen.load(); presents = noPending = noBall = mapFail = logs = 0;
          s_ballIdx = 0; s_ballIdxVotes = 0; s_ballChanges = 0; s_ballLogged = 0; memset(s_ballHist, 0, sizeof s_ballHist); s_ballHp = 0; }   // (2/11 paso 10) partido nuevo: se vuelve a buscar la pelota
      presents++; if (pending < 0) noPending++; if (!s_ballOk) noBall++;
      if (presents % 1800 == 0 && logs < 4) { logs++;   // (2/11 paso 10) tambien con indice ya votado (antes se callaba justo cuando el indice alternaba)
          Log("pelota: diagnostico (%d cuadros): dibujos aadffecf %d, saltados por x177 %d, sin staging %d, sin b5 %d, anotados %d, desbordes %d; cuadros sin anotaciones %d, sin pelota del pasto %d, lecturas que fallaron %d; indice %u votos %d cambios %d; x177 %.2f capsula %s (%.1f, %.2f, %.1f)",
              presents, s_bdCalls.load(), s_bdOffX177.load(), s_bdNoStage.load(), s_bdNoCb.load(), s_bdNoted.load(), s_bdOverflow.load(), noPending, noBall, mapFail, s_ballIdx.load(), s_ballIdxVotes.load(), s_ballChanges, g_cfg.live[177][0], s_ballOk ? "si" : "no", s_ball[0], s_ball[1], s_ball[2]); }
      if (pending < 0 || !s_bdStage[rd]) return;   // (11/11 4wp0pj) sin pelota del pasto igual se leen los botines
      D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_dev.imm->Map(s_bdStage[rd], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) { mapFail++; s_rdRetry[1]++; return; }   // (12/11 17:30) se reintenta el cuadro siguiente
      s_rdOk[1]++; s_bdFrame[rd] = -1;   // (2/11 paso 10) sin el memset los slots viejos volvian como candidatos
    const float* q = (const float*)m.pData; UINT found = 0; int nf = 0; static int s_noMatch = 0; int nCand = 0; float bestD = 1e9f; int bestH = -1;
    for (int i = 0; i < kBallSlots && s_ballOk; i++) { if (!s_bdIdx[rd][i]) continue; nCand++; const float* r = q + i * 32;
        // candidatos a traslacion: fila 3 (m_world por filas), fila 7 (matriz en 4..7), columna w de 0..2 y de 4..6 (matriz traspuesta)
        const float cand[4][3] = { { r[12], r[13], r[14] }, { r[28], r[29], r[30] }, { r[3], r[7], r[11] }, { r[19], r[23], r[27] } };
        for (int k = 0; k < 4; k++) { float dx = cand[k][0] - s_ball[0], dy = cand[k][1] - s_ball[1], dz = cand[k][2] - s_ball[2];
            float d2 = dx * dx + dy * dy + dz * dz;
            // (2/11 paso 10) antes se quedaba el ULTIMO dentro de 0.3 m: con la pelota al pie los botines (b5 a y 0..0.05, la capsula a ~0.11)
            // entraban en el radio y el indice alternaba pelota <-> botin cada cuadro. Ahora: primero los que estan a la altura de la
            // capsula (|dy| < 6 cm), y entre esos el mas cercano
            if (d2 < 0.3f * 0.3f) { nf++; int h = fabsf(dy) < 0.06f ? 1 : 0; if (h > bestH || (h == bestH && d2 < bestD)) { bestH = h; bestD = d2; found = s_bdIdx[rd][i]; } break; } } }
    if (s_ballOk && !found && nCand > 0) { if (++s_noMatch == 600) {   // ~10 s sin reconocerla: volcar que se leyo (una vez)
            Log("pelota: sin dibujo reconocido tras 600 cuadros (%d dibujos de botines/pelota por cuadro); capsula del pasto (%.2f, %.2f, %.2f)", nCand, s_ball[0], s_ball[1], s_ball[2]);
            for (int i = 0, n = 0; i < kBallSlots && n < 3; i++) { if (!s_bdIdx[rd][i]) continue; n++; const float* r = q + i * 32;
                Log("pelota: candidato %u indices b5 f0 (%.2f %.2f %.2f %.2f) f3 (%.2f %.2f %.2f %.2f) f4 (%.2f %.2f %.2f %.2f) f7 (%.2f %.2f %.2f %.2f)", s_bdIdx[rd][i],
                    r[0], r[1], r[2], r[3], r[12], r[13], r[14], r[15], r[16], r[17], r[18], r[19], r[28], r[29], r[30], r[31]); } } }
    {   // (11/11 n, Agustin: "los pastos alrededor de los botines no se deforman o hunden") DIAGNOSTICO para la proxima tanda: si los dibujos de
        // botines traen su propia posicion en b5 (cerca del suelo, separados de la pelota) se pueden pasar al pasto como la pelota. Cada ~30 s
        // (6 veces) se anotan los dibujos a menos de 2.5 m de la pelota con sus 4 lecturas de traslacion y la fila 0
        static int s_bootLogs = 0; static UINT s_bootF = 0;
        if (s_ballOk && s_bootLogs < 6 && (++s_bootF % 1800u) == 900u) { s_bootLogs++; int n = 0;
            for (int i = 0; i < kBallSlots && n < 8; i++) { if (!s_bdIdx[rd][i]) continue; const float* r = q + i * 32;
                const float cand[4][3] = { { r[12], r[13], r[14] }, { r[28], r[29], r[30] }, { r[3], r[7], r[11] }, { r[19], r[23], r[27] } };
                int kc = -1; for (int k = 0; k < 4 && kc < 0; k++) { const float dx = cand[k][0] - s_ball[0], dz = cand[k][2] - s_ball[2]; if (dx * dx + dz * dz < 2.5f * 2.5f && cand[k][1] > -0.3f && cand[k][1] < 1.2f) kc = k; }
                if (kc < 0) continue; n++;
                Log("botines (diag): dibujo %u lectura %d (%.3f %.3f %.3f) pelota (%.3f %.3f %.3f) f0 (%.2f %.2f %.2f %.2f) f1 (%.2f %.2f %.2f %.2f) f2 (%.2f %.2f %.2f %.2f)", s_bdIdx[rd][i], kc,
                    cand[kc][0], cand[kc][1], cand[kc][2], s_ball[0], s_ball[1], s_ball[2], r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11]); }
            Log("botines (diag): %d de %d dibujos de botines / pelota a menos de 2.5 m de la pelota", n, nCand); }
    }
    // (11/11 4wp0pj) botines sucios: cada dibujo leido (orden i del cuadro) -> jugador mas cercano a su traslacion (a menos de 1.5 m) -> su
    // tierra / pasto en el b12 de ese orden de dibujo; el dibujo de la pelota y los que no estan a la altura del piso quedan con el promedio
    if (BootAmt() >= 0.001f) { const double tn = Now(); float sumD = 0.f, sumG = 0.f; int nAct = 0;
        for (const BootDirt& b : s_bd) if (tn - b.seen < 2.0) { sumD += b.dirt; sumG += b.grass; nAct++; }
        const float wet = WetNow();
        s_bootAvgV[0] = 2.f; s_bootAvgV[1] = nAct ? sumD / nAct : 0.f; s_bootAvgV[2] = nAct ? sumG / nAct : 0.f; s_bootAvgV[3] = wet;
        // (12/11, Agustin: "un slider que suba este valor de suciedad en el momento para ver como se ve") fila 434: x tierra, y pasto forzados
        // (como minimo) en todos los botines; 0 = lo que junto cada jugador
        const float tryD = std::clamp(g_cfg.live[434][0], 0.f, 1.f), tryG = std::clamp(g_cfg.live[434][1], 0.f, 1.f);
        s_bootAvgV[1] = std::max(s_bootAvgV[1], tryD); s_bootAvgV[2] = std::max(s_bootAvgV[2], tryG);
        for (int i = 0; i < kBallSlots; i++) { s_bootKey[i] = 0; if (!s_bdIdx[rd][i] || s_bdIdx[rd][i] == found) continue; const float* r = q + i * 32;
            const float cand[4][3] = { { r[12], r[13], r[14] }, { r[28], r[29], r[30] }, { r[3], r[7], r[11] }, { r[19], r[23], r[27] } };
            int kc = -1; for (int k = 0; k < 4 && kc < 0; k++) if (fabsf(cand[k][0]) < 80.f && fabsf(cand[k][2]) < 50.f && cand[k][1] > -0.3f && cand[k][1] < 1.2f && fabsf(cand[k][0]) + fabsf(cand[k][2]) > 0.02f) kc = k;
            if (kc < 0) continue;
            float bestD = 1.5f * 1.5f; int bj = -1;
            for (size_t j = 0; j < s_bd.size(); j++) { if (tn - s_bd[j].seen > 2.0) continue; float dx = s_bd[j].x - cand[kc][0], dz = s_bd[j].z - cand[kc][2]; float d2 = dx * dx + dz * dz; if (d2 < bestD) { bestD = d2; bj = (int)j; } }
            if (bj < 0) continue;
            s_bootKey[i] = s_bdKey[rd][i]; s_bootVal[i][0] = 2.f; s_bootVal[i][1] = std::max(s_bd[bj].dirt, tryD); s_bootVal[i][2] = std::max(s_bd[bj].grass, tryG); s_bootVal[i][3] = wet; s_bootAssigned++;
            if (!s_bootCb[i]) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 16; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; g_dev.dev->CreateBuffer(&d, nullptr, &s_bootCb[i]); }
            if (s_bootCb[i]) g_dev.imm->UpdateSubresource(s_bootCb[i], 0, nullptr, s_bootVal[i], 0, 0); else s_bootKey[i] = 0; }
        if (s_bootAvgCb) g_dev.imm->UpdateSubresource(s_bootAvgCb, 0, nullptr, s_bootAvgV, 0, 0);
        static double nextL = 0.0; if (tn >= nextL && g_cfg.live[58][3] > 0.5f) { nextL = tn + 15.0; float mx = 0.f, mg = 0.f; for (const BootDirt& b : s_bd) { mx = std::max(mx, b.dirt); mg = std::max(mg, b.grass); }
            Log("botines sucios (15 s): jugadores %d (activos %d) | tierra media %.2f max %.2f, pasto medio %.2f max %.2f, mojado %.2f | barridas %.1f s, caidas de salto %d, repeticion sin sumar %.1f s, cadera de pie media %.2f m | dibujos con su jugador %d, con el promedio %d, asignados %d | x430 %.2f y430 %.2f z430 %.2f w430 %.2f",
                (int)s_bd.size(), nAct, s_bootAvgV[1], mx, s_bootAvgV[2], mg, wet, s_bootSlideT, s_bootLandN, s_bootReplayT, s_bootYn ? s_bootYb / s_bootYn : -1.f, s_bootHit.exchange(0), s_bootAvgN.exchange(0), s_bootAssigned.exchange(0), g_cfg.live[430][0], g_cfg.live[430][1], g_cfg.live[430][2], g_cfg.live[430][3]); s_bootSlideT = 0.f; s_bootLandN = 0; s_bootReplayT = 0.f; s_bootYb = 0.f; s_bootYn = 0; } }
    if ((FootOn() || BootColOn()) && s_ftStage[rd]) { D3D11_MAPPED_SUBRESOURCE mf;   // (12/11 16:50) pisadas reales (ver s_ftStage)
      const bool ftV2 = GrassLive(447, 2) > 0.5f;   // (13/11 tanda 2) z447 pisadas v2 (FootTracksV2)
      const bool learnOnly = BootColOn();   // (13/11) x505: aprende huesos que son pies, frente y piso aunque no haya pisadas (y441)
      if (SUCCEEDED(g_dev.imm->Map(s_ftStage[rd], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mf))) {
        const float* fq = (const float*)mf.pData; std::vector<FootPt> now; now.reserve(96); std::vector<FootObs> obs; if (ftV2 || learnOnly) obs.reserve(96);
        auto nearPlayer = [](float x, float z) { for (const PlayerPrev& p : s_pl) { const float dx = p.x - x, dz = p.z - z; if (dx * dx + dz * dz < 1.2f * 1.2f) return true; } return false; };
        auto okPt = [](float x, float y, float z) { return fabsf(x) < 80.f && fabsf(z) < 50.f && y > -0.08f && y < 0.5f && fabsf(x) + fabsf(z) > 0.02f; };
        auto hipD2 = [](float x, float z) { float b = 1e9f; for (const PlayerPrev& p : s_pl) { const float dx = p.x - x, dz = p.z - z; b = std::min(b, dx * dx + dz * dz); } return b; };
        for (int i = 0; i < kBallSlots; i++) { if (!s_bdIdx[rd][i] || s_bdIdx[rd][i] == found || !s_ftOk[rd][i]) continue;
            const float* r = q + i * 32; const float* B = fq + (size_t)i * (kFtSlotB / 4);
            for (int kb = 0; kb < (int)s_ftOk[rd][i] && kb < (int)kFtBones; kb++) { const float* M = B + kb * 12;
                // matriz de piel: x' = M0 x + M1 y + M2 z + M3, y' = M4.. + M7, z' = M8.. + M11 (float4x3 por columnas, como en el VS)
                float sp[3][3] = { { M[3], M[7], M[11] }, { M[3] + kFtLat * M[0], M[7] + kFtLat * M[4], M[11] + kFtLat * M[8] }, { M[3] - kFtLat * M[0], M[7] - kFtLat * M[4], M[11] - kFtLat * M[8] } };
                if (fabsf(sp[0][0]) + fabsf(sp[0][1]) + fabsf(sp[0][2]) < 1e-4f || fabsf(sp[0][0]) > 200.f || fabsf(sp[0][1]) > 200.f || fabsf(sp[0][2]) > 200.f) continue;
                // al mundo: m_world en las filas 4..7 (como el short; en los personajes es la identidad), en las filas 0..3, o ya en el mundo
                float wp[3][3]; int kc = -1;
                for (int k = 0; k < 3 && kc < 0; k++) {
                    for (int e = 0; e < 3; e++) { const float bx = sp[e][0], by = sp[e][1], bz = sp[e][2];
                        if (k == 0) { wp[e][0] = bx * r[16] + by * r[20] + bz * r[24] + r[28]; wp[e][1] = bx * r[17] + by * r[21] + bz * r[25] + r[29]; wp[e][2] = bx * r[18] + by * r[22] + bz * r[26] + r[30]; }
                        else if (k == 1) { wp[e][0] = bx * r[0] + by * r[4] + bz * r[8] + r[12]; wp[e][1] = bx * r[1] + by * r[5] + bz * r[9] + r[13]; wp[e][2] = bx * r[2] + by * r[6] + bz * r[10] + r[14]; }
                        else { wp[e][0] = bx; wp[e][1] = by; wp[e][2] = bz; } }
                    if (okPt(wp[0][0], wp[0][1], wp[0][2]) && nearPlayer(wp[0][0], wp[0][2])) kc = k; }
                if (kc < 0) continue;
                const int side = hipD2(wp[1][0], wp[1][2]) >= hipD2(wp[2][0], wp[2][2]) ? 1 : 2;   // el costado mas lejos del centro del jugador
                const float px = wp[side][0], py = wp[0][1], pz = wp[side][2];
                s_ftBoneN[kb]++; s_ftBoneY[kb] += py; if (py < kFtDown) s_ftBoneLow[kb]++;
                bool dup = false; for (const FootPt& f : now) if (fabsf(f.x - px) + fabsf(f.y - py) + fabsf(f.z - pz) < 0.01f) { dup = true; break; }
                if (!dup) { now.push_back({ px, py, pz, 0 });
                    if (ftV2 || learnOnly) {   // (13/11 tanda 2) z447: centro (velocidad del pie), costado (huella), frente = eje z del hueso al mundo, jugador mas cercano
                        const float dmx = M[2], dmy = M[6], dmz = M[10]; float fx = dmx, fz = dmz;
                        if (kc == 0) { fx = dmx * r[16] + dmy * r[20] + dmz * r[24]; fz = dmx * r[18] + dmy * r[22] + dmz * r[26]; }
                        else if (kc == 1) { fx = dmx * r[0] + dmy * r[4] + dmz * r[8]; fz = dmx * r[2] + dmy * r[6] + dmz * r[10]; }
                        const float fl = sqrtf(fx * fx + fz * fz); if (fl > 1e-3f) { fx /= fl; fz /= fl; } else { fx = 0.f; fz = 0.f; }
                        int pl = -1; float bd2 = 1.2f * 1.2f;
                        for (size_t j = 0; j < s_pl.size(); j++) { const float dx = s_pl[j].x - wp[0][0], dz = s_pl[j].z - wp[0][2], d2 = dx * dx + dz * dz; if (d2 < bd2) { bd2 = d2; pl = (int)j; } }
                        if (pl >= 0) obs.push_back({ pl, kb, wp[0][0], wp[0][1], wp[0][2], px, pz, fx, fz }); } } } }
        g_dev.imm->Unmap(s_ftStage[rd], 0);
        const double tf = Now(); s_ftLogFrames++; s_ftLogPts += (int)now.size();
        if (now.size() >= 2) { float lo1 = 1e9f, lo2 = 1e9f; for (const FootPt& f : now) { if (f.y < lo1) { lo2 = lo1; lo1 = f.y; } else if (f.y < lo2) lo2 = f.y; }
            s_ftGround = s_ftGroundOk ? s_ftGround + (lo2 - s_ftGround) * 0.02f : lo2; s_ftGroundOk = true; s_ftPtsT = tf; }   // (solo para el log: el suelo es y = 0)
        // con histeresis (no importa cuanto baja por cuadro: en camara lenta el pie baja de a poco); el talon y la punta del mismo pie
        // (o el mismo pie apoyando de nuevo enseguida) son UNA pisada: a menos de 25 cm y 0.4 s de otra, no cuenta
        if (!ftV2 && learnOnly) FootTracksV2(obs, s_bdG[rd], tf, false);   // (13/11) x505 sin z447: solo aprender (mascara, frente, piso), sin pisadas
        if (ftV2) FootTracksV2(obs, s_bdG[rd], tf, FootOn());   // (13/11 tanda 2) z447: por pie y relativo a su piso, en el reloj del juego (x505 sin y441: solo aprende)
        else if (FootOn()) for (FootPt& f : now) { float bd = 0.35f * 0.35f; const FootPt* pv = nullptr;
            for (const FootPt& g : s_ftPrev) { const float dx = g.x - f.x, dz = g.z - f.z, d2 = dx * dx + dz * dz; if (d2 < bd) { bd = d2; pv = &g; } }
            const float hgt = f.y;   // (12/11 17:30) el suelo es la cancha
            f.up = hgt > kFtUp || (pv && pv->up && hgt > kFtDown);
            if (pv && pv->up && !f.up) { bool dupS = false;
                for (const FootStrike& s : s_ftStrikes) { const float dx = s.x - f.x, dz = s.z - f.z; if (dx * dx + dz * dz < 0.25f * 0.25f && tf - s.t < 0.4) { dupS = true; break; } }
                if (!dupS) { s_ftStrikes.push_back({ f.x, f.z, tf, 0 }); s_ftStrikeT = tf; s_ftLogStrikes++; } } }
        s_ftPrev.swap(now);
        if (ftV2) { const double gF = s_bdG[rd];   // (13/11 tanda 2) z447: vencen en el reloj del juego (en pausa no se pierden)
            s_ftStrikes.erase(std::remove_if(s_ftStrikes.begin(), s_ftStrikes.end(), [gF](const FootStrike& s) { return s.pid < 0 || gF - s.g > 1.0; }), s_ftStrikes.end());
            if (s_ftStrikes.size() > 256) s_ftStrikes.erase(s_ftStrikes.begin(), s_ftStrikes.begin() + (std::ptrdiff_t)(s_ftStrikes.size() - 256)); }
        else s_ftStrikes.erase(std::remove_if(s_ftStrikes.begin(), s_ftStrikes.end(), [tf](const FootStrike& s) { return tf - s.t > 1.0; }), s_ftStrikes.end());   // las usadas quedan 1 s para no repetir
      }
      { static double nextF = 0.0; const double tn = Now(); if (tn >= nextF && g_cfg.live[58][3] > 0.5f) { nextF = tn + 15.0;
          Log("pisadas reales (15 s): puntos de pie por cuadro %.1f (%d cuadros), mas bajo %.3f m, pisadas %d, usadas %d, por zancada %d, botines sin b6 %d -> %s | huesos 0..3: %d/%d/%d/%d puntos, y media %.2f/%.2f/%.2f/%.2f, apoyados %d/%d/%d/%d | lecturas (ok/reintento/perdidas): jugadores %d/%d/%d, botines %d/%d/%d, pelota %d/%d/%d",
              s_ftLogFrames ? (float)s_ftLogPts / s_ftLogFrames : 0.f, s_ftLogFrames, s_ftGround, s_ftLogStrikes, s_ftLogUsed, s_ftLogStride, s_ftNoB6.exchange(0),
              (tn - s_ftPtsT < 0.5 && tn - s_ftStrikeT < 2.0) ? "con el botin real" : "con la zancada (no se ven pies o no hubo pisadas)",
              s_ftBoneN[0], s_ftBoneN[1], s_ftBoneN[2], s_ftBoneN[3], s_ftBoneN[0] ? s_ftBoneY[0] / s_ftBoneN[0] : 0.f, s_ftBoneN[1] ? s_ftBoneY[1] / s_ftBoneN[1] : 0.f,
              s_ftBoneN[2] ? s_ftBoneY[2] / s_ftBoneN[2] : 0.f, s_ftBoneN[3] ? s_ftBoneY[3] / s_ftBoneN[3] : 0.f, s_ftBoneLow[0], s_ftBoneLow[1], s_ftBoneLow[2], s_ftBoneLow[3],
              s_rdOk[0], s_rdRetry[0], s_rdLost[0], s_rdOk[1], s_rdRetry[1], s_rdLost[1], s_rdOk[2], s_rdRetry[2], s_rdLost[2]);
          if (ftV2) { int wf = 0; for (const PlayerPrev& p : s_pl) if (s_gClk - p.feetG < 0.5) wf++;   // (13/11 tanda 2) z447
              auto bc = [](int b) { return ((s_ftBoneMask >> b) & 1) ? 'S' : '-'; };
              Log("pisadas v2 (15 s): pisadas reales usadas %d de ~%.0f pasos esperados (%.0f %%), detectadas %d, por zancada %d | jugadores con pies a la vista %d de %d | huesos que son pies %c%c%c%c (apoyado %.0f/%.0f/%.0f/%.0f %%, sube y baja %.1f/%.1f/%.1f/%.1f cm), pistas %d, frente del pie %.2f",
                  s_ftLogUsed, s_ftExpect, s_ftExpect > 0.5f ? 100.f * (float)s_ftLogUsed / s_ftExpect : 0.f, s_ftLogStrikes, s_ftLogStride, wf, (int)s_pl.size(), bc(0), bc(1), bc(2), bc(3),
                  100.f * s_ftStPct[0], 100.f * s_ftStPct[1], 100.f * s_ftStPct[2], 100.f * s_ftStPct[3], s_ftRngCm[0], s_ftRngCm[1], s_ftRngCm[2], s_ftRngCm[3], (int)s_ftTrk.size(), s_ftFwdAcc); }
          s_ftExpect = 0.f;
          s_ftLogPts = s_ftLogFrames = s_ftLogStrikes = s_ftLogUsed = s_ftLogStride = 0;
          for (UINT b = 0; b < kFtBones; b++) { s_ftBoneN[b] = s_ftBoneLow[b] = 0; s_ftBoneY[b] = 0.f; }
          for (int k = 0; k < 3; k++) s_rdOk[k] = s_rdRetry[k] = s_rdLost[k] = 0; } } }
    memset(s_ftOk[rd], 0, sizeof s_ftOk[rd]);
    g_dev.imm->Unmap(s_bdStage[rd], 0); memset(s_bdIdx[rd], 0, sizeof s_bdIdx[rd]); memset(s_bdKey[rd], 0, sizeof s_bdKey[rd]);
    if (found) {   // (2/11 paso 10) voto por el indice mas frecuente de los ultimos 10 cuadros con candidato (antes 3 seguidos, que un botin intercalado reiniciaba)
        UINT* hist = s_ballHist; s_ballHist[s_ballHp] = found; s_ballHp = (s_ballHp + 1) % 10;
        UINT best = 0; int bestN = 0; for (int a = 0; a < 10; a++) { if (!hist[a]) continue; int n = 0; for (int b = 0; b < 10; b++) n += hist[b] == hist[a]; if (n > bestN) { bestN = n; best = hist[a]; } }
        UINT cur = s_ballIdx.load(); int prevVotes = s_ballIdxVotes.load();
        if (best != cur) { s_ballChanges++; if (s_ballChanges <= 5) Log("pelota: indice candidato %u -> %u (%d de 10 cuadros, %d dibujos cerca de la capsula, dy %s)", cur, best, bestN, nf, bestH > 0 ? "a la altura de la capsula" : "por debajo"); }
        s_ballIdx = best; s_ballIdxVotes = bestN;
        if (s_ballLogged < 2 && prevVotes < 3 && bestN >= 3) { s_ballLogged++; Log("pelota: dibujo reconocido (indice %u, %d de 10 cuadros, %d dibujos cerca de la capsula); manchas verde %.2f tierra %.2f cal %.2f", best, bestN, nf, s_stain[0], s_stain[1], s_stain[2]); } }
    }
}
// (11/11 4wp0pj) tierra y pasto que junta cada jugador en los botines, en tiempo de juego (dt de GameStep: pausa = nada, camara lenta = menos).
// Por metro corrido (velocidad suavizada; parado o caminando despacio casi nada): con la cancha a 1 (natural) un jugador que corre ~1.5 km en
// un partido de 10 min llega a ~0.6 de tierra en zona pelada y ~0.5 de pasto en zona sana; el que casi no se mueve queda limpio
static void BootDirtUpdate(const std::vector<PlayerPrev>& pl, float dt) {
    const float amt = BootAmt(); if (amt < 0.001f || dt <= 0.f) return;
    if (g_matchGen.load() != s_bdGen) { s_bdGen = g_matchGen.load(); s_bd.clear(); }
    if (g_cfg.live[10][0] <= 0.f) return;   // fuera del partido (editor, menus): no junta
    // (12/11, Agustin: "la suciedad tiene que tener memoria y ir con el gameclock interno") lo de cada jugador dura todo el partido (solo se borra
    // con un partido nuevo) y solo suma cuando el reloj del juego (fila 157) pasa por tiempo NUEVO: si la repeticion lo rebobina y lo vuelve a
    // correr, lo ya jugado no ensucia de nuevo. Con el reloj del juego apagado (w157 0) se usa el dt de siempre
    static double s_gMax = -1e18; static int s_gGen = -1;
    if (s_gGen != s_bdGen) { s_gGen = s_bdGen; s_gMax = -1e18; }
    if (g_cfg.live[157][3] >= 0.5f) { const double g = (double)g_cfg.live[157][0];
        if (s_gMax < -1e17) s_gMax = g;
        double dNew = g - s_gMax; if (dNew < -50000.0) dNew += 100000.0;   // vuelta del reloj (100000 s)
        if (dNew <= 0.0) { s_bootReplayT += dt; dt = 0.f; } else { dt = std::min(dt, (float)dNew); s_gMax = g; } }
    // (13/11 tanda 2) y448: el reloj de las nubes (157) nunca vuelve atras, asi que la repeticion ensuciaba otra vez; con el detector v2 no suma
    if (RpV2() && dt > 0.f && InReplay()) { s_bootReplayT += dt; dt = 0.f; }
    const bool accum = dt > 0.f;
    const double tn = Now(); const float wet = WetNow(), dry = g_cfg.live[114][0] > 1.5f ? 1.f : 0.f;
    const float fall = g_cfg.live[430][3] > 0.001f ? std::clamp(g_cfg.live[430][3], 0.f, 3.f) : 1.f;
    std::vector<char> used(s_bd.size(), 0);
    for (const PlayerPrev& p : pl) {
        // mismo jugador: el registro mas cercano a menos de 2.5 m; si no hay (corte de camara, saque, repeticion), el mas cercano que no se vio hace 0.3 s
        int bj = -1; float bd = 2.5f * 2.5f;
        for (size_t j = 0; j < s_bd.size(); j++) { if (used[j]) continue; float dx = s_bd[j].x - p.x, dz = s_bd[j].z - p.z, d2 = dx * dx + dz * dz; if (d2 < bd) { bd = d2; bj = (int)j; } }
        if (bj < 0) { bd = 1e9f; for (size_t j = 0; j < s_bd.size(); j++) { if (used[j] || tn - s_bd[j].seen < 0.3) continue; float dx = s_bd[j].x - p.x, dz = s_bd[j].z - p.z, d2 = dx * dx + dz * dz; if (d2 < bd) { bd = d2; bj = (int)j; } } }
        if (bj < 0) { if (s_bd.size() >= (size_t)kMaxPlayers) continue; s_bd.push_back(BootDirt{ p.x, p.z, 0.f, 0.f, tn }); used.push_back(0); bj = (int)s_bd.size() - 1; }
        used[bj] = 1; BootDirt& b = s_bd[bj]; b.x = p.x; b.z = p.z; b.seen = tn; if (p.yok && p.yb > 0.f) { s_bootYb += p.yb; s_bootYn++; }
        if (p.trk < 4 || !accum) continue;   // recien aparecido: sin velocidad confiable; repeticion: ya se sumo
        const float sp = std::min(sqrtf(p.svx * p.svx + p.svz * p.svz), 11.f);
        const float run = (sp > 1.2f && !p.air) ? sp * dt : 0.f;                       // metros corridos (caminar despacio o en el aire no ensucia)
        const float bare = std::clamp(WearCpuAt(p.x, p.z), 0.f, 1.f);                  // cancha pelada bajo sus pies (desgaste del partido)
        const float act = 1.f + 2.5f * std::clamp(p.skid, 0.f, 1.f);                    // frenadas, giros y arranques clavan el botin
        b.dirt  += run / 1500.f * amt * (0.25f + 0.75f * bare) * act * (1.f + 1.3f * wet) * (1.f - 0.35f * dry) * (1.f - 0.5f * b.dirt);
        b.grass += run / 1400.f * amt * (1.f - 0.6f * bare) * (0.6f + 0.4f * act) * (1.f + 0.6f * wet) * (1.f - 0.5f * dry) * (1.f - 0.6f * b.grass);
        if (p.slide) {   // barrida: el botin y el costado se arrastran por el pasto (~1 s de barrida = +0.12 de tierra, +0.15 de pasto)
            b.dirt  += dt * 0.12f * amt * (0.4f + 0.6f * bare) * (1.f + 1.2f * wet) * (1.f - 0.4f * b.dirt);
            b.grass += dt * 0.15f * amt * (1.f - 0.5f * bare) * (1.f + 0.5f * wet) * (1.f - 0.4f * b.grass); s_bootSlideT += dt; }
        if (p.land) {    // caida de un salto: los tapones se clavan
            b.dirt  += 0.025f * amt * (0.3f + 0.7f * bare) * (1.f + 1.2f * wet); b.grass += 0.02f * amt * (1.f - 0.5f * bare); s_bootLandN++; }
        // lo que se desprende solo: el pasto en ~4 min de juego (mas rapido corriendo, el botin lo sacude), la tierra seca en ~15 (mojada se pega)
        b.grass -= b.grass * dt * fall * (1.f / 240.f) * (1.f + 0.06f * sp);
        b.dirt  -= b.dirt * dt * fall * (1.f / 900.f) * (1.f - 0.6f * wet);
        b.dirt = std::clamp(b.dirt, 0.f, 1.f); b.grass = std::clamp(b.grass, 0.f, 1.f);
    }
}
// (13/11 tanda 2) y448 detector de repeticiones v2 (ver s_rpClock): una vez por Present. Reloj crudo = m_timeParam.x de SysCB (el mismo que
// lee config.cpp para la fila 157, pero sin acumular: la repeticion lo rebobina). Se borra con un partido nuevo o si quedo > 120 s atras
static void ReplayV2Present() {
    static double lastT = -1.0; const double t = Now(); const float dR = lastT < 0.0 ? 0.f : (float)std::clamp(t - lastT, 0.0, 0.25); lastT = t;
    static int gen = -1, iniGen = -1, lastFr = -1, ringN = 0, ringI = 0, smpLive = 0, smpHook = 0;
    static double rawMax = 0.0, ringT[64] = {}, winT = -1.0, nextL = 0.0; static float ringV[64] = {}, raw = 0.f, rate = -1.f, slowT = 0.f;
    static float accSlow = 0.f, accIn = 0.f, accClk = 0.f, accHook = 0.f; static bool haveMax = false;
    if (!RpV2()) { s_rpClock = false; slowT = 0.f; return; }   // y448 0: como antes
    if (g_matchGen.load() != gen || g_matchIniGen.load() != iniGen) { gen = g_matchGen.load(); iniGen = g_matchIniGen.load(); haveMax = false; ringN = 0; slowT = 0.f; }
    std::vector<float> Y; int fr = -1; bool rewound = false;
    if (NamedCBRead("SysCB", Y, &fr) && Y.size() >= 20) {
        raw = Y[16];
        if (fr != lastFr) { lastFr = fr; ringT[ringI] = t; ringV[ringI] = raw; ringI = (ringI + 1) % 64; ringN = std::min(ringN + 1, 64); }
        if (!haveMax) { rawMax = raw; haveMax = true; }
        if ((double)raw > rawMax) rawMax = raw;
        if (rawMax - (double)raw > 120.0) { Log("repeticiones v2: el reloj crudo quedo %.0f s atras (%.1f, max %.1f): se toma como nuevo", rawMax - (double)raw, raw, rawMax); rawMax = raw; }
        rewound = (double)raw < rawMax - 0.25;
        // ritmo del reloj crudo sobre los ultimos ~0.25 s reales (al entrar en pausa la rampa dura menos que los 0.3 s que pide la camara lenta)
        rate = -1.f;
        if (ringN >= 2) { const int nI = (ringI + 63) % 64;
            for (int kk = 1; kk < ringN; kk++) { const int j = (ringI + 63 - kk) % 64; const double dT = ringT[nI] - ringT[j];
                if (dT >= 0.25) { if (dT < 1.0) rate = (float)((double)(ringV[nI] - ringV[j]) / dT); break; } } }
    } else rate = -1.f;
    const bool modSlow = g_cfg.live[343][1] > 0.5f && g_cfg.live[343][2] > 0.5f && g_cfg.live[343][2] < 1.5f;   // (14/11) z343 = 2 (solo en la repeticion) frena recien cuando ya se detecto   // la camara lenta del mod en el partido (tiempo.cpp, fila 343) no es una repeticion
    const bool slow = !modSlow && rate > 0.05f && rate < 0.85f;
    slowT = slow ? slowT + dR : 0.f;
    s_rpClock = rewound || slowT >= 0.3f;
    // el visor esta roto si dice "reproduciendo" > 95 % de 60 s de juego en vivo (ritmo 0.9-1.1, jugadores corriendo y, si el tick del partido
    // es confiable -s_tkTrust-, avanzando; en su partido el tick quedo clavado 20 min)
    { bool mov = false; for (const PlayerPrev& p : s_pl) if (p.trk >= 4 && p.svx * p.svx + p.svz * p.svz > 1.5f) { mov = true; break; }
      const float r157 = g_cfg.live[157][3] > 0.5f ? g_cfg.live[157][2] : -1.f;
      // tick clavado > 10 s con ritmo 0.9-1.1, jugadores corriendo y el reloj crudo sin rebobinar = tick no confiable, no repeticion
      // (s_tkTrust solo se mide fuera de la repeticion, y con el visor roto InReplay() es siempre verdadero: no bajaba nunca)
      const bool tickOk = !s_tkSeen || s_tkTrust < 0.15f || (t - s_tkChangeT) < 0.25 || (t - s_tkChangeT) > 10.0;
      if (tickOk && r157 > 0.9f && r157 < 1.1f && mov && !s_rpClock) { smpLive++; if (HookPlaying()) smpHook++; }
      if (winT < 0.0) winT = t;
      if (t - winT >= 60.0) {
          if (!s_rpBroken && smpLive >= 300 && (float)smpHook > 0.95f * (float)smpLive) { s_rpBroken = true;
              Log("repeticiones v2: el visor dijo 'reproduciendo' %d de %d cuadros de juego en vivo (60 s): se ignora, la repeticion sale del reloj crudo", smpHook, smpLive); }
          winT = t; smpLive = smpHook = 0; } }
    const bool inR = InReplay(); if (inR) accIn += dR; if (s_rpClock) accClk += dR; if (HookPlaying()) accHook += dR; if (slow) accSlow += dR;
    if (t >= nextL) { nextL = t + 30.0;
        Log("repeticiones v2 (30 s): reloj crudo %.2f / max %.2f s, ritmo %.2f, camara lenta %.1f s | visor %s: llamadas %d, reproduciendo %d, cursor avanzando %d | aviso del juego %s | en repeticion %.1f s (por el reloj %.1f s, por el visor %.1f s) | x441 %.0f",
            raw, rawMax, rate, accSlow, g_replayHook.load() ? "enganchado" : "sin gancho", g_replayCallsV2.exchange(0), g_replayPlayV2.exchange(0), g_replayMoveV2.exchange(0),
            s_rpBroken ? "roto (se ignora)" : "ok", accIn, accClk, accHook, g_cfg.live[441][0]);
        accIn = accClk = accHook = accSlow = 0.f; }
}
// (x505) recursos del pozo de botines y del compute (contexto inmediato, en Present)
static bool EnsureCol() {
    if (s_colDev != g_dev.dev) {
        // dispositivo nuevo (SetActiveDevice): lo viejo era del dispositivo que el juego dejo; se suelta sin Release a proposito (un hilo
        // del juego podria estar copiando todavia) y se crea de nuevo. Pasa una vez por sesion como mucho
        if (s_colDev) Log("botines en el pasto (x505): dispositivo nuevo, se crean de nuevo los recursos");
        s_colReady.store(false, std::memory_order_release); s_colRan.store(false, std::memory_order_release);
        s_colRaw = nullptr; s_colRawSRV = nullptr; s_colOut = nullptr; s_colOutSRV = nullptr; s_colOutUAV = nullptr; s_colGen = nullptr; s_colCB = nullptr; s_colCS = nullptr;
        for (int i = 0; i < 4; i++) { s_colDiag[i] = nullptr; s_colDiagF[i] = -1; }
        s_colTried = false; s_colDev = g_dev.dev;
    }
    if (s_colTried) return s_colReady.load(std::memory_order_acquire);
    s_colTried = true;
    ID3D11Device* dev = g_dev.dev; if (!dev) return false;
    bool ok = true;
    {   // pozo: 256 lugares de 116 float4, en cero (sello 0 = nunca valido)
        std::vector<float> z((size_t)kColSlots * kColF4 * 4, 0.f);
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kColSlots * kColF4 * 16; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = { z.data(), 0, 0 };
        ok = ok && SUCCEEDED(dev->CreateBuffer(&d, &sd, &s_colRaw)) && s_colRaw;
        if (ok) { D3D11_SHADER_RESOURCE_VIEW_DESC v = {}; v.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; v.Buffer.FirstElement = 0; v.Buffer.NumElements = kColSlots * kColF4;
            ok = SUCCEEDED(dev->CreateShaderResourceView(s_colRaw, &v, &s_colRawSRV)) && s_colRawSRV; }
    }
    if (ok) {   // pes_feet: 160 float4 (StructuredBuffer), en cero (sin la marca 505 = apagado)
        float z[kColOutF4 * 4] = {};
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kColOutF4 * 16; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = 16;
        D3D11_SUBRESOURCE_DATA sd = { z, 0, 0 };
        ok = SUCCEEDED(dev->CreateBuffer(&d, &sd, &s_colOut)) && s_colOut;
        if (ok) { D3D11_SHADER_RESOURCE_VIEW_DESC v = {}; v.Format = DXGI_FORMAT_UNKNOWN; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; v.Buffer.FirstElement = 0; v.Buffer.NumElements = kColOutF4;
            ok = SUCCEEDED(dev->CreateShaderResourceView(s_colOut, &v, &s_colOutSRV)) && s_colOutSRV; }
        if (ok) { D3D11_UNORDERED_ACCESS_VIEW_DESC u = {}; u.Format = DXGI_FORMAT_UNKNOWN; u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; u.Buffer.FirstElement = 0; u.Buffer.NumElements = kColOutF4;
            ok = SUCCEEDED(dev->CreateUnorderedAccessView(s_colOut, &u, &s_colOutUAV)) && s_colOutUAV; }
    }
    if (ok) {   // sello: 8 entradas de 32 B, entrada e = (huesos - 1) * 2 + hay cuadro anterior: { sello, 0, 0, 0,  huesos, anterior, 0, 0 }
        float gb[64] = {}; for (int e = 0; e < 8; e++) { gb[e * 8] = (float)s_colGenV; gb[e * 8 + 4] = (float)(e / 2 + 1); gb[e * 8 + 5] = (float)(e & 1); }
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = 256; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = { gb, 0, 0 };
        ok = SUCCEEDED(dev->CreateBuffer(&d, &sd, &s_colGen)) && s_colGen;
    }
    if (ok) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 496; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; ok = SUCCEEDED(dev->CreateBuffer(&d, nullptr, &s_colCB)) && s_colCB; }
    if (ok) for (int i = 0; i < 4; i++) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 48; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = 16;   // como pes_feet (copia entre buffers estructurados)
        if (FAILED(dev->CreateBuffer(&d, nullptr, &s_colDiag[i]))) s_colDiag[i] = nullptr; }
    if (ok) { if (ID3DBlob* b = CompileNamedShader(L"pes_feet_cs.hlsl", "cs_5_0")) { if (FAILED(dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_colCS))) s_colCS = nullptr; b->Release(); }
        ok = s_colCS != nullptr; }
    if (ok) NamedSetSRV("pes_feet", s_colOutSRV);
    Log(ok ? "botines en el pasto (x505): compute pes_feet_cs.hlsl listo (pozo de 256 lugares, 475 KB)" : "botines en el pasto (x505): compute pes_feet_cs.hlsl NO disponible (el pasto queda como antes)");
    s_colReady.store(ok, std::memory_order_release);
    return ok;
}
static const float kColSideRule = 1.f;   // (x505) +1 = el costado mas lejos de la cadera (la regla de las pisadas, kFtLat); -1 = el mas cerca
// (x505) log cada 15 s (con w58): cada 60 Presents copia pes_feet[0] y [147..148] a un staging y lee el mas viejo listo (>= 3 cuadros)
static void BootColDiag() {
    static int s_n = 0; static double s_next = 0.0, a[12] = {}, ys = 0.0; static int an = 0, yn = 0;
    if (g_cfg.live[58][3] <= 0.5f) return;
    ID3D11DeviceContext* imm = g_dev.imm; s_n++;
    for (int i = 0; i < 4; i++) { if (!s_colDiag[i] || s_colDiagF[i] < 0 || s_n - s_colDiagF[i] < 3) continue;
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(imm->Map(s_colDiag[i], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) { const float* f = (const float*)m.pData;
            a[0] += f[0]; a[1] += f[1]; for (int k = 0; k < 7; k++) a[2 + k] += f[4 + k]; if (f[11] >= 0.f) { ys += f[11]; yn++; } an++;
            imm->Unmap(s_colDiag[i], 0); s_colDiagF[i] = -1; } }
    if (s_n % 60 == 0) for (int i = 0; i < 4; i++) if (s_colDiag[i] && s_colDiagF[i] < 0) {
        D3D11_BOX b0 = { 0, 0, 0, 16, 1, 1 }, b1 = { 147 * 16, 0, 0, 149 * 16, 1, 1 };
        imm->CopySubresourceRegion(s_colDiag[i], 0, 0, 0, 0, s_colOut, 0, &b0); imm->CopySubresourceRegion(s_colDiag[i], 0, 16, 0, 0, s_colOut, 0, &b1);
        s_colDiagF[i] = s_n; break; }
    const double tn = Now(); if (tn < s_next) return;
    if (s_next == 0.0) { s_next = tn + 15.0; return; }
    s_next = tn + 15.0;
    const double k = an ? 1.0 / an : 0.0;
    auto bc = [](int b) { return ((s_ftBoneMask >> b) & 1) ? 'S' : '-'; };
    Log("botines en el pasto (15 s): dibujos de botines %d, copiados %d (sin b5/b6 %d, sin cuadro anterior %d, pelota %d, lejos de la camara %d) | por cuadro (muestras %d): lugares de este cuadro %.1f, "
        "pies validos %.1f, sin repetir %.1f, elegidos %.1f, huellas que se levantan %.1f, desbordes %.0f, pies sin cadera %.1f | lado: mantenido %.1f, cambiado %.1f por cuadro, regla %+.0f | "
        "suela apoyada %.3f m (piso %+.3f %+.3f %+.3f %+.3f) | huesos que son pies %c%c%c%c, frente %+.2f | D %.0f m, pies a la vez %.0f, acostado %.2f s | x505 %.2f",
        s_colCalls.exchange(0), s_colCopies.exchange(0), s_colNoCb.exchange(0), s_colNoPrev.exchange(0), s_colBall.exchange(0), s_colFarSkip.exchange(0), an,
        a[2] * k, a[3] * k, a[4] * k, a[0] * k, a[1] * k, a[5] * k, a[6] * k, a[7] * k, a[8] * k, (double)kColSideRule,
        yn ? ys / yn : -1.0, s_colFl[0], s_colFl[1], s_colFl[2], s_colFl[3], bc(0), bc(1), bc(2), bc(3), s_ftFwdAcc, s_colD, s_colN,
        GrassLive(505, 3) > 0.01f ? std::clamp(GrassLive(505, 3), 0.15f, 3.f) : 0.75f, GrassLive(505, 0));
    for (double& v : a) v = 0.0; an = 0; ys = 0.0; yn = 0;
}
static void BootColPresent(UINT flags) {
    static int s_fr = 0, s_lastOn = -100, s_mg = -1; static double s_g0 = -1.0;
    if (flags & DXGI_PRESENT_TEST) return;   // Present de prueba: ni compute ni sello nuevo (como GrassFrameBoundary)
    s_fr++;
    if (!BootColOn()) { s_colRan.store(false, std::memory_order_release); s_colFar.store(true, std::memory_order_relaxed); return; }
    if (!EnsureCol()) return;
    ID3D11DeviceContext* imm = g_dev.imm;
    const float yD = GrassLive(505, 1), zN = GrassLive(505, 2), wT = GrassLive(505, 3);
    const float D = yD > 0.5f ? std::clamp(yD, 4.f, 30.f) : 12.f;
    // caderas de los jugadores (s_pl, w115), adelantadas por la demora de su lectura; nadie cerca de la camara = no se copia nada
    float cb[124] = {};
    const float lagG = (float)std::clamp(s_gClk - s_plGc, 0.0, 0.3);
    int nH = 0; bool nearAny = false;
    for (const PlayerPrev& p : s_pl) {
        if (nH >= 48) break;
        const float hx = p.x + p.svx * lagG, hz = p.z + p.svz * lagG;
        cb[28 + nH * 2] = hx; cb[29 + nH * 2] = hz; nH++;
        const float ex = hx - g_eyePos[0], ey = g_eyePos[1], ez = hz - g_eyePos[2];
        if (ex * ex + ey * ey + ez * ez < (D + 3.f) * (D + 3.f)) nearAny = true;
    }
    s_colFar.store(!g_eyeValid || !nearAny, std::memory_order_relaxed);
    // empezar de cero (recien prendido, partido nuevo): sello nuevo SIN compute, asi el compute siguiente solo toma lo copiado despues
    bool reset = s_fr - s_lastOn > 2; s_lastOn = s_fr;
    const int mg = g_matchGen.load(); if (mg != s_mg) { s_mg = mg; reset = true; }
    float gb[64] = {};
    auto upGen = [&]() { for (int e = 0; e < 8; e++) { gb[e * 8] = (float)s_colGenV; gb[e * 8 + 4] = (float)(e / 2 + 1); gb[e * 8 + 5] = (float)(e & 1); } imm->UpdateSubresource(s_colGen, 0, nullptr, gb, 0, 0); };
    if (reset) { s_colGenV = s_colGenV >= (1u << 23) ? 1u : s_colGenV + 1u; upGen(); s_colPendReset = true; s_g0 = s_gClk; s_colRan.store(false, std::memory_order_release); return; }
    const double dG = s_g0 < 0.0 ? 0.0 : std::clamp(s_gClk - s_g0, 0.0, 0.1); s_g0 = s_gClk;   // s de juego del cuadro (pausa = 0)
    // piso de cada hueso: promedio del piso (base) de las pistas de pisadas v2 vistas hace menos de 0.5 s (lo aprende FootTracksV2)
    float flS[4] = {}; int flN[4] = {}; const double tn = Now();
    for (const FootTrk& t : s_ftTrk) if (t.bone >= 0 && t.bone < 4 && t.st != 0 && tn - t.seenR < 0.5) { flS[t.bone] += t.base; flN[t.bone]++; }
    const float tauV = (wT > 0.01f ? std::clamp(wT, 0.15f, 3.f) : 0.75f) - 0.10f;   // w505 = segundos visibles: retencion 0.10 + 3 tau
    cb[0] = (float)s_colGenV; cb[1] = (float)kColSlots; cb[2] = zN > 0.5f ? std::clamp(zN, 2.f, 16.f) : 8.f; cb[3] = D;
    cb[4] = g_eyePos[0]; cb[5] = g_eyePos[1]; cb[6] = g_eyePos[2]; cb[7] = g_eyeValid ? 1.f : 0.f;
    cb[8] = (float)(s_ftBoneMask ? s_ftBoneMask : 0xF);   // mascara 0 (autoprueba sin pies) = todos, como antes de aprender
    cb[9] = fabsf(s_ftFwdAcc) > 0.3f ? (s_ftFwdAcc > 0.f ? 1.f : -1.f) : 0.f;
    cb[10] = std::max(tauV / 3.f, 0.02f); cb[11] = (float)dG;
    for (int k = 0; k < 4; k++) { cb[12 + k] = flN[k] ? std::clamp(flS[k] / flN[k], -0.04f, 0.04f) : 0.f; s_colFl[k] = cb[12 + k]; }
    cb[16] = 0.05f; cb[17] = 0.15f; cb[18] = 0.045f; cb[19] = 0.07f;   // talon, punta, radio de la suela, alcance del borde (m)
    cb[20] = s_colPendReset ? 1.f : 0.f; cb[21] = 0.10f; cb[22] = kColSideRule; cb[23] = (float)nH;
    cb[24] = 0.03f; cb[25] = 0.8f; cb[26] = 0.04f; cb[27] = kFtLat;
    s_colD = D; s_colN = cb[2];
    s_colPendReset = false;
    imm->UpdateSubresource(s_colCB, 0, nullptr, cb, 0, 0);
    ID3D11DeviceContext1* i1 = nullptr; if (FAILED(imm->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&i1))) i1 = nullptr;
    ID3D11ComputeShader* pcs = nullptr; imm->CSGetShader(&pcs, nullptr, nullptr);
    ID3D11ShaderResourceView* pT0 = nullptr; imm->CSGetShaderResources(0, 1, &pT0);
    ID3D11Buffer* pB0 = nullptr; UINT pf0 = 0, pn0 = 0; if (i1) i1->CSGetConstantBuffers1(0, 1, &pB0, &pf0, &pn0); else imm->CSGetConstantBuffers(0, 1, &pB0);
    ID3D11UnorderedAccessView* pU0 = nullptr; imm->CSGetUnorderedAccessViews(0, 1, &pU0);
    imm->CSSetShader(s_colCS, nullptr, 0); imm->CSSetShaderResources(0, 1, &s_colRawSRV); imm->CSSetConstantBuffers(0, 1, &s_colCB);
    imm->CSSetUnorderedAccessViews(0, 1, &s_colOutUAV, nullptr);
    imm->Dispatch(1, 1, 1);
    imm->CSSetUnorderedAccessViews(0, 1, &pU0, nullptr);   // primero el u0: pes_feet se lee como SRV en el VS del pasto
    imm->CSSetShader(pcs, nullptr, 0); imm->CSSetShaderResources(0, 1, &pT0);
    if (i1 && pB0 && pn0) i1->CSSetConstantBuffers1(0, 1, &pB0, &pf0, &pn0); else imm->CSSetConstantBuffers(0, 1, &pB0);
    if (pcs) pcs->Release(); if (pT0) pT0->Release(); if (pB0) pB0->Release(); if (pU0) pU0->Release(); if (i1) i1->Release();
    // sello del cuadro siguiente: lo que la GPU copie desde ahora lleva gen + 1
    s_colGenV = s_colGenV >= (1u << 23) ? 1u : s_colGenV + 1u; upGen();
    s_colRan.store(true, std::memory_order_release);
    BootColDiag();
}
// (x505) para BeforeDraw (hilos del juego): *want = x505 prendido; devuelve pes_feet si ya hay un compute hecho, si no nullptr (se enlaza vacio)
ID3D11ShaderResourceView* PitchFeetSRV(bool* want) {
    const bool on = BootColOn(); if (want) *want = on;
    return (on && s_colRan.load(std::memory_order_acquire) && s_colDev == g_dev.dev) ? s_colOutSRV : nullptr;
}
void PitchWearPresent(UINT flags) {
    if (!g_dev.dev || !g_dev.imm) return;
    { static double s_cG = -1e9, s_cR = -1.0; s_gClk += GameStep(s_cG, s_cR); }   // (13/11 tanda 2) reloj del juego comun (pisadas v2)
    ReplayV2Present();    // (13/11 tanda 2) y448 detector de repeticiones v2
    EnsureRes();
    PitchFlatPresent();   // (20/10) pasto aplastado temporal (jugadores + pelota)
    BallDrawPresent();    // (2/11) reconocer el dibujo de la pelota y publicar sus manchas
    BootColPresent(flags);   // (13/11) x505 botines en el pasto: pies cerca de la camara para el VS del pasto (t10)
    const float* P = g_cfg.live[115];
    // (08/11 z, Agustin: "no se aprecian las huellas ni el desgaste") diagnostico cada 15 s: en su partido no hubo ni una pisada en el log
    { static double nextD = 0.0; double tn = Now(); if (tn >= nextD && g_cfg.live[58][3] > 0.5f) { nextD = tn + 15.0;
        Log("desgaste en vivo (15 s): dibujos de short 5529 %d (anotados %d, b5 corto %d, sin staging %d; hueso 0 de b6 %d, b6 corto %d, sin b6 %d) | posiciones: validas %d (hueso 0 %d, fila 7 %d, fila 3 %d, columna w %d), fuera de rango %d, en cero %d | jugadores seguidos %d, recortes vivos %d | x115 %.2f w115 %.0f x136 %.0f x389 %.0f",
            s_noteCalls.exchange(0), s_noteOk.exchange(0), s_noteShort.exchange(0), s_noteNoStage.exchange(0), s_noteBone.exchange(0), s_boneShort.exchange(0), s_boneNone.exchange(0), s_posOk, s_rowPick[0], s_rowPick[1], s_rowPick[2], s_rowPick[3], s_posBad, s_posZero, (int)s_pl.size(), (int)s_clips.size(), P[0], P[3], g_cfg.live[136][0], g_cfg.live[389][0]);
        s_posOk = s_posBad = s_posZero = 0; s_rowPick[0] = s_rowPick[1] = s_rowPick[2] = s_rowPick[3] = 0;
        Log("recortes v2 (15 s): pisadas que arrancaron pasto %d | nacidos: hojas %d, manojos %d, terrones %d, polvo %d | vivos %d (tope %d) | x116 %.2f y116 %.1f w116 %.0f z389 %.2f",
            s_strikeN.exchange(0), s_bornN[kClipBlade].exchange(0), s_bornN[kClipTuft].exchange(0), s_bornN[kClipClod].exchange(0), s_bornN[kClipDust].exchange(0), (int)s_clips.size(), ClipCap(),
            g_cfg.live[116][0], g_cfg.live[116][1], g_cfg.live[116][3], g_cfg.live[389][2]); } }
    if (P[3] < 0.5f) { s_count = 0; return; }
    EnsureRes(); if (!s_tex || !s_cb) return;
    // partido nuevo: borrar
    SimClockPoll();
    if (!WearReal()) {
      if (g_matchGen.load() != s_matchGenSeen || !s_cleared) { s_matchGenSeen = g_matchGen.load(); float z[4] = {}; g_dev.imm->ClearRenderTargetView(s_rtv, z); s_cleared = true; s_prev.clear(); s_pl.clear();
          memset(s_wearCpu, 0, sizeof s_wearCpu); s_stain[0] = s_stain[1] = s_stain[2] = 0.f; }
    } else {
      // (12/11) desgaste real: MEMORIA. Partido nuevo = Sider reescribio match.ini, o el tick del partido volvio a empezar despues de un rato
      // sin cancha. La cancha sin dibujarse 20 s (entretiempo, menu de pausa largo) solo reinicia el seguimiento de los jugadores
      static int s_iniSeen = -1, s_tkMax = 0, s_tkAtGen = -1; static bool s_pendGen = false;
      bool clearNow = !s_cleared; const char* why = "primer partido";
      const int ig = g_matchIniGen.load(); if (s_iniSeen < 0) s_iniSeen = ig; else if (ig != s_iniSeen) { s_iniSeen = ig; clearNow = true; why = "match.ini nuevo"; }
      if (g_matchGen.load() != s_matchGenSeen) { s_matchGenSeen = g_matchGen.load(); s_prev.clear(); s_pl.clear();
          if (s_tkSeen) { s_pendGen = true; s_tkAtGen = s_tkLast; } else if (!clearNow) { clearNow = true; why = "cancha nueva (sin reloj del partido)"; } }
      if (s_pendGen && s_tkLast != s_tkAtGen) { s_pendGen = false;
          Log("desgaste real: la cancha volvio, tick del partido %d (antes llego a %d): %s", s_tkLast, s_tkMax, (s_tkMax > 1000 && s_tkLast < s_tkMax / 2) ? "partido nuevo, se borra" : "mismo partido, se conserva");
          if (s_tkMax > 1000 && s_tkLast < s_tkMax / 2 && !clearNow) { clearNow = true; why = "el tick del partido volvio a empezar"; } }
      if (clearNow) { float z[4] = {}; g_dev.imm->ClearRenderTargetView(s_rtv, z); s_cleared = true; s_prev.clear(); s_pl.clear(); s_wearQueue.clear();
          memset(s_wearCpu, 0, sizeof s_wearCpu); s_stain[0] = s_stain[1] = s_stain[2] = 0.f; s_tkMax = 0; Log("desgaste real: mapa borrado (%s)", why); }
      if (s_tkLast > s_tkMax) s_tkMax = s_tkLast;
      { static double nextL = 0.0; if (Now() >= nextL) { nextL = Now() + 30.0;
          Log("desgaste real (30 s): reloj del partido %s (tick %d, confianza %.2f%s), repeticiones %s (%d llamadas, %d reproduciendo), se suma %s, accion x%.2f, acelerar x%.1f", s_tkSeen ? "enganchado" : "SIN gancho (se suma con el reloj del juego)", s_tkLast, s_tkTrust, (s_tkSeen && s_tkTrust < 0.15f) ? ": el tick no acompana, se suma con el reloj del juego" : "", g_replayHook.load() ? "enganchadas" : "sin gancho", g_replayCalls.exchange(0), g_replayPlay.exchange(0), SimGate() > 0.5f ? "si" : InReplay() ? "no (repeticion)" : "no (partido detenido)", WearActK(), WearBoost()); } }
    }
    // anillo de 8 (12/11 17:30): este cuadro escribio en w; el siguiente escribe en (w+1); se lee el mas viejo pendiente con >= 3 cuadros (ver kStageMinAge)
    int w = s_stageW.load(); int n = std::min(s_count.exchange(0), kMaxPlayers);
    s_stageFrame[w] = n > 0 ? s_frame : -1; s_frame++;
    { const int nw = (w + 1) % kStages; if (s_stageFrame[nw] >= 0) { s_stageFrame[nw] = -1; s_rdLost[0]++; } }   // (12/11 17:30) anillo de 8
    double t = Now(); s_lastT = t;
    static double s_wG = -1e9, s_wR = -1.0; const float gdt = GameStep(s_wG, s_wR);   // (11/11) reloj del juego (pausa: 0; camara lenta: menos)
    { static double s_trT = 0.0; const float dtR = (float)std::clamp(t - s_trT, 0.0, 0.1); s_trT = t;   // (12/11 16:15) confianza en el tick
      bool mov = false; for (const PlayerPrev& p : s_pl) if (p.trk >= 4 && p.svx * p.svx + p.svz * p.svz > 1.5f) { mov = true; break; }
      if (s_tkSeen && gdt > 0.f && mov && !InReplay()) { const float adv = (t - s_tkChangeT) < 0.25 ? 1.f : 0.f; s_tkTrust += (adv - s_tkTrust) * (1.f - expf(-dtR / 60.f)); } }
    static double s_gAcc = 0.0; s_gAcc += gdt; s_stageG[w] = s_gAcc; s_stageGc[w] = s_gClk;   // (12/11 17:30) reloj del juego de cada lectura (antes: acumulado desde la ultima lectura)
    ClipSimAndUpload(gdt);
    s_stageW = (w + 1) % kStages;
    // (12/11 17:30) posiciones: la lectura MAS NUEVA ya lista (>= 3 cuadros); las anteriores se descartan (la velocidad sale con el reloj del
    // juego de cada lectura, asi que saltear cuadros no la cambia); si ninguna esta lista se reintenta el cuadro siguiente
    int rd = -1; int fRd = -1; D3D11_MAPPED_SUBRESOURCE m;
    { int order[kStages]; int no = 0; for (int i = 0; i < kStages; i++) if (s_stageFrame[i] >= 0 && s_frame - s_stageFrame[i] >= kStageMinAge && s_stage[i]) order[no++] = i;
      std::sort(order, order + no, [](int a, int b) { return s_stageFrame[a] > s_stageFrame[b]; });
      for (int k = 0; k < no && rd < 0; k++) if (SUCCEEDED(g_dev.imm->Map(s_stage[order[k]], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) rd = order[k];
      if (rd < 0) { if (no > 0) s_rdRetry[0]++; return; }
      fRd = s_stageFrame[rd]; for (int i = 0; i < kStages; i++) if (i != rd && s_stageFrame[i] >= 0 && s_stageFrame[i] < fRd) { s_stageFrame[i] = -1; s_rdLost[0]++; }
      s_stageFrame[rd] = -1; s_rdOk[0]++; }
    const double gRd = s_stageG[rd];
    const float* q = (const float*)m.pData; int cnt = 0; float pos[kMaxPlayers][2]; float posY[kMaxPlayers]; int posYok[kMaxPlayers];
    for (int i = 0; i < kMaxPlayers && cnt < kMaxPlayers; i++) {
        // (08/11 z) candidatos de posicion dentro de las 8 filas copiadas: fila 7 (xyz), fila 3 (xyz), columna w de las filas 4..6
        const float* r = q + i * 48; float x = 0, y = 0, z = 0; int pick = -1;
        // (08/11 aa) primero m_world (b5 c4..c7, filas r[16..31]) * hueso 0 (b6: traslacion en la columna w de sus 3 registros, r[32+3], r[32+7], r[32+11]):
        // vale tanto si m_world es la identidad (todo en los huesos) como si el hueso es local al jugador
        const float* B = r + 32; float bx = B[3], by = B[7], bz = B[11]; bool hasB = fabsf(bx) + fabsf(by) + fabsf(bz) > 1e-4f && fabsf(bx) < 200.f && fabsf(by) < 200.f && fabsf(bz) < 200.f;
        float hx = 0, hy = 0, hz = 0;
        if (hasB) { hx = bx * r[16] + by * r[20] + bz * r[24] + r[28]; hy = bx * r[17] + by * r[21] + bz * r[25] + r[29]; hz = bx * r[18] + by * r[22] + bz * r[26] + r[30]; }
        const float cand[4][3] = { { hx, hy, hz }, { r[28], r[29], r[30] }, { r[12], r[13], r[14] }, { r[19], r[23], r[27] } };
        for (int k = (hasB ? 0 : 1); k < 4 && pick < 0; k++) { float cx = cand[k][0], cy = cand[k][1], cz = cand[k][2];
            if (fabsf(cx) < 80.f && fabsf(cz) < 50.f && fabsf(cy) < 5.f && !(fabsf(cx) < 0.01f && fabsf(cz) < 0.01f)) { x = cx; y = cy; z = cz; pick = k; } }
        if (pick < 0) { if (fabsf(r[28]) < 0.01f && fabsf(r[30]) < 0.01f) s_posZero++; else s_posBad++; continue; }
        s_rowPick[pick]++; s_posOk++;   // (08/11 aa) 0 = hueso 0, 1 = fila 7, 2 = fila 3, 3 = columna w
        bool dup = false; for (int k = 0; k < cnt; k++) if (fabsf(pos[k][0] - x) < 0.05f && fabsf(pos[k][1] - z) < 0.05f) { dup = true; break; }
        if (dup) continue;
        pos[cnt][0] = x; pos[cnt][1] = z; posY[cnt] = y; posYok[cnt] = pick == 0 ? 1 : 0; cnt++;   // (11/11 4wp0pj) el alto solo vale del hueso 0
    }
    g_dev.imm->Unmap(s_stage[rd], 0);
    if (cnt == 0) return;
    // (11/11) recortes v2: tiempo real desde la ultima lectura de posiciones (si un cuadro no se pudo leer -DO_NOT_WAIT- la distancia era
    // de 2 cuadros sobre el dt de 1 y la velocidad salia doble); un hueco largo (pausa, repeticion, carga) reinicia las velocidades
    // (11/11) en tiempo de juego: en pausa (no avanzo) no se toca nada y se sigue sumando; en la camara lenta las velocidades salen bien
    static double s_plG = -1.0; static int s_plF = -1000; const float plAcc = s_plG < 0.0 ? 1.f : (float)(gRd - s_plG);   // tiempo de juego entre las dos lecturas
    if (plAcc < 0.002f) return;
    const bool gapP = (fRd - s_plF) > 15 || plAcc > 0.1f; s_plF = fRd; s_plG = gRd;   // hueco: mas de 15 cuadros o 0.1 s de juego entre lecturas
    float dtP = std::clamp(plAcc, 0.002f, 0.1f);
    // velocidad: el mas cercano del cuadro anterior
    float brush[kWearBrushes][4]; int nb = 0;
    float rate = (P[0] > 0.f ? P[0] : 1.0f) * WearBoost(), rad = std::clamp(P[1], 0.3f, 2.0f);
    std::vector<PlayerPrev> plNow; plNow.reserve(cnt);
    const float kAct = std::clamp(g_cfg.live[177][1], 0.f, 2.f);   // (2/11) frenadas y arranques desgastan mas (y arrastran la marca)
    const bool real = WearReal(); const float aK = WearActK(), simK = SimGate();   // (12/11) desgaste real
    // (12/11 16:50) pisadas reales: cada pisada del botin al jugador mas cercano (una por actualizacion; la que sobra queda para la siguiente)
    const bool feetLive = FootOn() && (t - s_ftPtsT) < 0.5 && (t - s_ftStrikeT) < 2.0;
    int strikeOf[kMaxPlayers]; for (int i = 0; i < kMaxPlayers; i++) strikeOf[i] = -1;
    // (13/11 tanda 2) z447 PISADAS v2: sin el apagado global (una pisada real en 2 s apagaba la zancada de todos): cada jugador vuelve a la
    // zancada si no tuvo pisada real en 0.9 s de juego (lastRealG). La pisada va a su jugador (id) o, si no, al mas cercano a 1.2 m (antes la
    // deteccion aceptaba 1.2 m y la asignacion 0.9); se marca usada solo cuando se estampa (antes tambien en un hueco, y se perdia)
    const bool ftV2 = GrassLive(447, 2) > 0.5f; const double gNowC = s_stageGc[rd];
    if (ftV2) { if (!gapP && FootOn() && !s_ftStrikes.empty()) {
        int biOf[kMaxPlayers];   // el mismo jugador anterior que elige el bucle de abajo (y su salto de 15 m/s)
        for (int i = 0; i < cnt; i++) { float b2 = 1e9f; int bk = -1; for (size_t k = 0; k < s_pl.size(); k++) { const float dx = s_pl[k].x - pos[i][0], dz = s_pl[k].z - pos[i][1], d2 = dx * dx + dz * dz; if (d2 < b2) { b2 = d2; bk = (int)k; } }
            biOf[i] = (bk >= 0 && sqrtf(b2) / dtP <= 15.f) ? bk : -1; }
        for (size_t s = 0; s < s_ftStrikes.size(); s++) { const FootStrike& fs = s_ftStrikes[s]; if (fs.used || fs.pid < 0 || gNowC - fs.g > 1.0) continue; int bp = -1; bool idSeen = false;
            for (int i = 0; i < cnt; i++) if (biOf[i] >= 0 && s_pl[biOf[i]].id == fs.pid) { idSeen = true; if (strikeOf[i] < 0) bp = i; break; }   // su jugador ya tiene una: queda para la siguiente
            if (bp < 0 && !idSeen) { float bd = 1.2f * 1.2f; for (int i = 0; i < cnt; i++) { if (biOf[i] < 0 || strikeOf[i] >= 0) continue; const float dx = pos[i][0] - fs.x, dz = pos[i][1] - fs.z, d2 = dx * dx + dz * dz; if (d2 < bd) { bd = d2; bp = i; } } }
            if (bp >= 0) strikeOf[bp] = (int)s; } } }
    else if (feetLive) for (size_t s = 0; s < s_ftStrikes.size(); s++) { FootStrike& fs = s_ftStrikes[s]; if (fs.used) continue; float bd = 0.9f * 0.9f; int bp = -1;
        for (int i = 0; i < cnt; i++) { const float dx = pos[i][0] - fs.x, dz = pos[i][1] - fs.z, d2 = dx * dx + dz * dz; if (d2 < bd) { bd = d2; bp = i; } }
        if (bp >= 0 && strikeOf[bp] < 0) { strikeOf[bp] = (int)s; fs.used = 1; } }
    for (int i = 0; i < cnt; i++) {
        float best = 1e9f; int bi = -1; for (size_t k = 0; k < s_pl.size(); k++) { float dx = s_pl[k].x - pos[i][0], dz = s_pl[k].z - pos[i][1]; float d2 = dx * dx + dz * dz; if (d2 < best) { best = d2; bi = (int)k; } }
        float speed = best < 1e8f ? sqrtf(best) / dtP : 0.f;           // m/s
        if (speed > 15.f || gapP) { speed = 0.f; bi = -1; }               // salto de camara / teletransporte / hueco
        PlayerPrev pn{ pos[i][0], pos[i][1], 0.f, 0.f, 0.f };
        if (bi >= 0) { const PlayerPrev& pp = s_pl[bi];
            pn.vx = (pos[i][0] - pp.x) / dtP; pn.vz = (pos[i][1] - pp.z) / dtP;
            { float r = sqrtf(pn.vx * pn.vx + pn.vz * pn.vz); if (r > 11.f) { pn.vx *= 11.f / r; pn.vz *= 11.f / r; } }   // nadie corre a mas de 11 m/s (cruce de dos jugadores)
            pn.trk = std::min(pp.trk + 1, 1000);
            pn.yb = pp.yb; pn.air = pp.air; pn.slide = pp.slide;
            pn.id = pp.id; pn.lastRealG = pp.lastRealG; pn.feetG = pp.feetG;   // (13/11 tanda 2) z447
            // (11/11) recortes v2: velocidad (~80 ms) y aceleracion (~120 ms) suavizadas: la resta cruda de posiciones cuadro a cuadro tenia
            // ruido (0.1 m/s de error = 6 m/s2 de "aceleracion") y daba frenadas y giros falsos que tiraban pasto al azar
            float aV = 1.f - expf(-dtP / 0.08f), aA = 1.f - expf(-dtP / 0.12f);
            if (pp.trk == 0) { pn.svx = pn.vx; pn.svz = pn.vz; pn.sax = pn.saz = 0.f; }   // recien aparecido: arranca con su velocidad (sin "arranque" falso)
            else { pn.svx = pp.svx + (pn.vx - pp.svx) * aV; pn.svz = pp.svz + (pn.vz - pp.svz) * aV;
                pn.sax = pp.sax + ((pn.svx - pp.svx) / dtP - pp.sax) * aA; pn.saz = pp.saz + ((pn.svz - pp.svz) / dtP - pp.saz) * aA;
                float am = sqrtf(pn.sax * pn.sax + pn.saz * pn.saz); if (am > 15.f) { pn.sax *= 15.f / am; pn.saz *= 15.f / am; } }
            float sS = sqrtf(pn.svx * pn.svx + pn.svz * pn.svz), sP = sqrtf(pp.svx * pp.svx + pp.svz * pp.svz);
            float hx = sS > 0.3f ? pn.svx / sS : 0.f, hz = sS > 0.3f ? pn.svz / sS : 0.f;
            float aL = pn.sax * hx + pn.saz * hz, aT = -pn.sax * hz + pn.saz * hx;   // a lo largo (+ acelera, - frena) y de costado (+ hacia la izquierda)
            const float rk = ReactK();
            float brake  = std::clamp((-aL - 4.f / rk) / 10.f * rk, 0.f, 1.f) * std::min(std::max(sS, sP) / 4.f, 1.f);   // frenada
            float push   = std::clamp((aL - 3.f / rk) / 8.f * rk, 0.f, 1.f);                                               // arranque
            float cutK   = std::clamp((fabsf(aT) - 4.f / rk) / 10.f * rk, 0.f, 1.f) * std::min(sS / 3.f, 1.f);             // giro / cambio de direccion
            float sprint = sS > 6.0f ? std::min(0.06f + 0.1f * (sS - 6.0f) / 3.0f, 0.2f) * rk : 0.f;                        // sprint: pocos y chicos
            float skid = std::max(std::max(brake, cutK), std::max(push * 0.6f, sS > 7.0f ? 0.12f * rk : 0.f));
            pn.skid = std::max(skid, pp.skid * expf(-dtP / 0.1f));
            // (08/11) zancada: una huella cada media zancada (0.75 m parado / caminando .. 1.1 m al sprint), alternando el pie
            pn.stride = pp.stride + sS * dtP; pn.foot = pp.foot;
            float half = std::clamp(0.55f + 0.08f * sS, 0.6f, 1.15f);
            bool footHit = false, doPrint = false;
            if (ftV2) {   // (13/11 tanda 2) z447: por jugador
                const bool realRecent = FootOn() && gNowC - pp.lastRealG < 0.9;
                if (realRecent) pn.stride = std::min(pn.stride, half);   // con sus pies a la vista la zancada no se acumula (al volver no salen varias juntas)
                footHit = strikeOf[i] >= 0;
                doPrint = footHit || (!realRecent && pn.stride >= half && sS > 0.3f);
                if (FootOn() && pn.trk >= 4 && sS > 0.8f) s_ftExpect += sS * dtP / half;   // pasos esperados (log)
            } else {
                if (feetLive) pn.stride = std::min(pn.stride, half);   // con pies a la vista la zancada no se acumula (al volver no salen varias juntas)
                footHit = feetLive && strikeOf[i] >= 0;   // (12/11 16:50) pisada real del botin; sin pies a la vista, la zancada estimada
                doPrint = footHit || (!feetLive && pn.stride >= half && sS > 0.3f);
            }
            if (doPrint) {
                if (footHit) { FootStrike& fs = s_ftStrikes[strikeOf[i]]; pn.printX = fs.x; pn.printZ = fs.z; pn.stride = 0.f; pn.foot ^= 1; s_ftLogUsed++;
                    if (ftV2) { fs.used = 1; pn.lastRealG = gNowC; pn.printReal = 1; pn.printHx = fs.hx; pn.printHz = fs.hz; } }
                else { pn.stride -= half; pn.foot ^= 1; float side = pn.foot ? 0.11f : -0.11f; pn.printX = pos[i][0] + (-hz) * side; pn.printZ = pos[i][1] + hx * side; if (FootOn()) s_ftLogStride++; }
                pn.printDue = 1;
                // (11/11) recortes v2: lo que arranca ESTE pie al pisar. Impulso / sprint -> atras; frenada -> adelante (patina); giro -> hacia
                // afuera de la curva. Trotar o caminar (sin aceleracion) no arranca nada
                float inten = std::max(std::max(push * 0.9f, brake * 1.1f), cutK) + sprint;
                pn.printK = inten;   // (12/11) desgaste real: cuanto gasta esta pisada
                if (inten >= 0.05f && pn.trk >= 4) {
                    float wB = push + sprint, wF = brake * 1.2f, wC = cutK, sg = aT > 0.f ? -1.f : 1.f;
                    float tx = -hx * wB + hx * wF + (-hz) * sg * wC, tz = -hz * wB + hz * wF + hx * sg * wC;
                    float l = sqrtf(tx * tx + tz * tz); if (l < 1e-3f) { tx = -hx; tz = -hz; } else { tx /= l; tz /= l; }
                    if (tx == 0.f && tz == 0.f && pn.printReal) { tx = -pn.printHx; tz = -pn.printHz; }   // (13/11 tanda 2) z447: pisada real casi parado: hacia atras del pie
                    EmitStrike(pn.printX + tx * 0.08f, pn.printZ + tz * 0.08f, tx, tz, inten, false);
                }
            }
        }
        if (pn.id < 0) { pn.id = s_plNextId; s_plNextId = (s_plNextId + 1) & 0x3fffffff; }   // (13/11 tanda 2) z447: id estable nuevo (recien aparecido o despues de un hueco)
        // (11/11 4wp0pj, Agustin: "si el jugador decide tirarse a barrer tambien es un valor si el jugador salta") barrida y salto por el alto
        // de la cadera (hueso 0) contra su alto de pie: mas de 28 cm abajo moviendose = barrida (arrastra el botin por el pasto); mas de 22 cm
        // arriba = en el aire (no junta); al volver al piso, la caida clava los tapones
        pn.y = posY[i]; pn.yok = posYok[i]; pn.land = 0;
        if (pn.yok && bi >= 0) { const float sv = sqrtf(pn.svx * pn.svx + pn.svz * pn.svz);
            if (pn.yb < 0.3f) pn.yb = pn.y;
            const float dy = pn.y - pn.yb; const int wasAir = pn.air;
            pn.slide = (pn.yb > 0.45f && dy < -0.28f && (sv > 1.5f || pn.slide)) ? 1 : 0;
            pn.air = (pn.yb > 0.45f && dy > 0.22f) ? 1 : 0;
            pn.land = (wasAir && !pn.air) ? 1 : 0;
            if (!pn.slide && !pn.air && fabsf(dy) < 0.15f) pn.yb += (pn.y - pn.yb) * (1.f - expf(-dtP / 2.f));   // alto de pie: ~2 s
        } else { pn.slide = pn.air = 0; }
        float k = (0.25f + 0.75f * std::min(speed / 7.f, 1.f)) * rate * dtP * 0.08f * (1.f + 3.f * pn.skid * kAct);   // parado: poco; corriendo: mas; frenando: mucho
        // (12/11) desgaste real: parado / caminando casi nada, corriendo poco; lo que gasta son las acciones (frenada, giro, arranque) y se
        // acumula donde se insiste; nada con el partido detenido (pausa, repeticion)
        if (real) k = (0.08f + 0.50f * std::min(speed / 7.f, 1.f)) * rate * dtP * 0.08f * (1.f + 5.f * pn.skid * aK) * simK;
        else k *= simK;   // (12/11 16:50) sin desgaste real SimGate es 1, salvo con x441 (solo en la repeticion)
        brush[nb][0] = pos[i][0] / 140.f + 0.5f; brush[nb][1] = pos[i][1] / 79.f + 0.5f; brush[nb][2] = k; brush[nb][3] = rad; nb++;
        WearCpuAdd(brush[nb - 1][0], brush[nb - 1][1], k, rad);
        if (pn.skid > 0.2f && kAct > 0.01f && speed > 1.f && nb < kWearBrushes) {   // arrastre detras del jugador que frena
            float ux = pn.vx / speed, uz = pn.vz / speed, bx = pos[i][0] - ux * 0.4f, bz = pos[i][1] - uz * 0.4f;
            brush[nb][0] = bx / 140.f + 0.5f; brush[nb][1] = bz / 79.f + 0.5f; brush[nb][2] = k * 0.7f; brush[nb][3] = rad * 0.8f; nb++;
            WearCpuAdd(brush[nb - 1][0], brush[nb - 1][1], k * 0.7f, rad * 0.8f);
        }
        // (12/11) desgaste real: BARRIDA (pn.slide, deteccion del hilo de botines por la cadera): raspon largo por todo el tramo recorrido
        if (real && bi >= 0 && pn.slide && simK > 0.5f) { const PlayerPrev& pp = s_pl[bi];
            float dx = pos[i][0] - pp.x, dz = pos[i][1] - pp.z, dl = sqrtf(dx * dx + dz * dz); int st = std::clamp((int)(dl / 0.2f) + 1, 1, 6);
            for (int e = 0; e < st; e++) { float a = (e + 0.5f) / st; WearStamp(pp.x + dx * a, pp.z + dz * a, 0.030f * aK * rate, 0.22f); }
            static int slideLog = 0; if (slideLog < 5) { slideLog++; Log("desgaste real: barrida en (%.1f, %.1f), cadera %.2f m (de pie %.2f m) a %.1f m/s", pos[i][0], pos[i][1], pn.y, pn.yb, speed); } }
        // caida de un salto: los tapones se clavan (marca chica)
        if (real && pn.land && simK > 0.5f) WearStamp(pos[i][0], pos[i][1], 0.012f * aK * rate, 0.18f);
        plNow.push_back(pn);
    }
    // (11/11) recortes v2: los recortes salen en la pisada (EmitStrike, arriba); w116 = 0 los borra
    if (g_cfg.live[116][3] < 0.5f) s_clips.clear();
    BootDirtUpdate(plNow, dtP);   // (11/11 4wp0pj) botines sucios por jugador
    s_prev.assign(&pos[0][0], &pos[0][0] + cnt * 2); s_pl.swap(plNow); s_plGen++;
    s_plGc = s_stageGc[rd];   // (x505) reloj del juego de esta lectura de jugadores
    // (08/11) huecos del pasto arrancado (cola de EmitStrike) y huellas en el desgaste (marcas que quedan)
    // (12/11) desgaste real: la pisada fuerte deja su marca aunque las huellas (x389) esten apagadas, mas cuanto mas fuerte
    if (real && simK > 0.5f) for (const PlayerPrev& pp : s_pl) { if (!pp.printDue || (pp.printK < 0.12f && FootEx() <= 1.f)) continue;
        WearStamp(pp.printX, pp.printZ, 0.022f * aK * std::clamp(pp.printK, FootEx() > 1.f ? 0.3f : 0.f, 1.5f) * rate * FootEx(), 0.15f * FootExR()); }
    for (const WearBrush& wb : s_wearQueue) { if (nb >= kWearBrushes) break; brush[nb][0] = wb.u; brush[nb][1] = wb.v; brush[nb][2] = wb.k * (real ? 1.f : rate * simK); brush[nb][3] = wb.rad; nb++; }
    s_wearQueue.clear();
    if (FpOn() > 0.5f) for (const PlayerPrev& pp : s_pl) { if (nb >= kWearBrushes) break; if (!pp.printDue) continue;
        float kf = 0.012f * FpForce() * rate * (1.f + 2.f * pp.skid * kAct) * simK * FootEx(); brush[nb][0] = pp.printX / 140.f + 0.5f; brush[nb][1] = pp.printZ / 79.f + 0.5f; brush[nb][2] = kf; brush[nb][3] = 0.14f * FootExR(); nb++;
        WearCpuAdd(brush[nb - 1][0], brush[nb - 1][1], kf, 0.14f * FootExR()); }
    if (!s_vs || !s_ps) return;
    // subir pinceles y dibujar
    if (nb <= 0) return;
    ID3D11DeviceContext* c = g_dev.imm;
    ID3D11RenderTargetView* oRT[8] = {}; ID3D11DepthStencilView* oDS = nullptr; c->OMGetRenderTargets(8, oRT, &oDS);
    UINT nvp = 8; D3D11_VIEWPORT ovp[8]; c->RSGetViewports(&nvp, ovp);
    ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr;
    c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL); D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS); ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDSS, &oRef);
    ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0; c->OMGetBlendState(&oBS, oBF, &oSM);
    ID3D11Buffer* oCB = nullptr; c->VSGetConstantBuffers(0, 1, &oCB);
    c->OMSetRenderTargets(1, &s_rtv, nullptr); D3D11_VIEWPORT vp{ 0, 0, 1024, 512, 0, 1 }; c->RSSetViewports(1, &vp);
    c->VSSetShader(s_vs, nullptr, 0); c->PSSetShader(s_ps, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0);
    c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    c->RSSetState(s_rs); c->OMSetDepthStencilState(s_dss, 0); float bf[4] = {}; c->OMSetBlendState(s_bsAdd, bf, 0xffffffff);
    c->VSSetConstantBuffers(0, 1, &s_cb);
    for (int b0 = 0; b0 < nb; b0 += kMaxPlayers) {   // (12/11) de a 48 pinceles (tamano del cbuffer del VS)
        const int nn = std::min(nb - b0, kMaxPlayers);
        D3D11_MAPPED_SUBRESOURCE mc; if (FAILED(c->Map(s_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) break;
        float* cbp = (float*)mc.pData; cbp[0] = (float)nn; cbp[1] = 140.f; cbp[2] = 79.f; cbp[3] = 0.f;
        memcpy(cbp + 4, &brush[b0][0], nn * 16); c->Unmap(s_cb, 0);
        c->DrawInstanced(4, (UINT)nn, 0, 0);
    }
    // restaurar
    c->OMSetRenderTargets(8, oRT, oDS); for (auto* v : oRT) if (v) v->Release(); if (oDS) oDS->Release();
    c->RSSetViewports(nvp, ovp);
    c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0); if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release(); c->IASetPrimitiveTopology(oTopo);
    c->RSSetState(oRS); if (oRS) oRS->Release(); c->OMSetDepthStencilState(oDSS, oRef); if (oDSS) oDSS->Release();
    c->OMSetBlendState(oBS, oBF, oSM); if (oBS) oBS->Release();
    c->VSSetConstantBuffers(0, 1, &oCB); if (oCB) oCB->Release();
    if (s_logged < 3 && nb > 0) { s_logged++; Log("desgaste en vivo: %d jugadores pintados (ej. %.1f, %.1f)", nb, pos[0][0], pos[0][1]); }
}

} // namespace fx
