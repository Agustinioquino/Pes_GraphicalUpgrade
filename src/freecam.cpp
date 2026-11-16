// PES21 FX (31/10, 1/11) - CAMARA LIBRE: zoom y movimiento con teclado y mouse en el visor del editor (modo editar /
// Apariencia: pedido de Agustin, para mirar de cerca las expresiones y las celebraciones), y tambien en el partido / las
// repeticiones si se prende ahi.
//
// Dos entradas al mismo estado de camara (pos + giro + zoom, se mueve en tiempo real, no depende del reloj del juego):
//  * VISOR DEL EDITOR: la camara no pasa por el CameraSystem del partido (hk_Cam recibe 0 llamadas en el editor). La arma
//    draw::ModelViewCommon (vtable 0x142c78ca0) en un cobra::rc::camera::CameraImpl (+0x10 posicion, +0x1c objetivo, +0x28
//    arriba, +0x34 / +0x38 cerca / lejos, +0x3c tipo de proyeccion (0 = perspectiva), +0x40 campo de vision, +0x44 relacion de
//    aspecto, +0x58 continuidad) y cada cuadro el nodo [7] 0x1423efa10 llama 0x1419bf3f0 (RenderTarget::SetCamera: rcx =
//    cliente del render target, rdx = CameraImpl), que copia la camara en el comando 5 del render target -> 0x14198afd0 en el
//    hilo de render -> fox::gr::Camera. La DLL engancha 0x1419bf3f0 (MinHook, con firma) y pisa los campos del CameraImpl
//    antes de dejarlo seguir. Lo usan tambien otros render targets (FixDemo / repeticion), asi que con la camara libre
//    prendida todos muestran la misma camara propia.
//  * PARTIDO / REPETICION: gancho de camhook.cpp (CameraSystem 0x14198d3b0, parametro de 0x104 bytes: +0 posicion, +0x0c
//    objetivo, +0x18 arriba, +0x2c proyeccion, +0x30 campo de vision, +0x48 continua).
//
//   344 (ini): x 0 apagada / 1 prendida (el juego no ve el teclado mientras tanto, keyblock.cpp) / 2 prendida y el juego
//   sigue viendo el teclado; y velocidad (m/s); z zoom (x el campo de vision del juego; menos = mas cerca); w sensibilidad
//   del mouse. 345 la escribe la DLL: xyz posicion de la camara, w campo de vision aplicado.
//   Teclas (solo con la ventana del juego al frente): W / S adelante y atras, A / D de costado, Q / E bajar y subir,
//   flechas giran, mouse con el boton derecho apretado gira, Re Pag / Av Pag (o + / -) zoom, Shift x4, Ctrl /4,
//   Inicio = volver a la camara del juego (se toma de nuevo la posicion de donde este el juego).
#include "core.h"
#include <MinHook.h>
#include <cmath>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <mutex>
#include "menuvisual.h"

namespace fx {

static bool s_on = false;
// (08/11 e) zoom minimo: los 3 congelamientos del 08/11 fueron reinicios de la placa (TDR, nvlddmkm 153) con la camara libre a
// zoom 0.10-0.26 (campo de vision 0.02-0.09 rad): la cara llena la pantalla y la placa pasa los 2 s que Windows tolera.
static const float kZoomMin = 0.05f;   // (08/11 e) Agustin no quiere zoom minimo: queda el de siempre
static float s_zoomTarget = 1.0f; static std::atomic<int> s_wheel{0};
static float s_fovBase = 0.0f, s_fovWritten = -1.0f;   // fov del juego sin zoom / ultimo fov escrito
// "seguir la cara" (x346). (1/11) v1: la camara orbitaba alrededor de la cabeza sacada de la matriz de sk_head del dibujo de la
// cara (b6, expr.cpp): llegaba casi en cero e intermitente y no andaba. (02/11, Agustin: "una camara cercana a la cara que la siga
// en las animaciones, una camara fija centrada en el rostro") v2: la cara sale del ESQUELETO (exprbones.cpp ExprFaceFrame: centro
// de la cara + ejes frente / arriba de sk_head, al mundo con la matriz del nodo) y hay dos modos:
//   x346 = 1 FIJA AL ROSTRO: la camara va pegada a la cara, de frente, a ~0.6 m, y gira con la cabeza: la cara queda quieta y
//            centrada en la pantalla aunque el jugador se mueva, festeje o gire. W / S acercan y alejan, A / D / flechas y el mouse
//            con el boton derecho corren el angulo respecto de la cara (perfil, tres cuartos, desde abajo) y ese angulo se mantiene.
//   x346 = 2 ORBITAR LA CABEZA: como v1: apunta al centro de la cara pero el angulo es del mundo (no gira con la cabeza).
static bool s_follow = false, s_headSeen = false; static int s_followMode = 0;
static int s_subjReq = 0;   // (14/11 g) cambiar de sujeto al seguir la cara: +1 siguiente (derecha), -1 anterior
static float s_head[3] = {}, s_hfw[3] = { 0, 0, 1 }, s_hup[3] = { 0, 1, 0 };   // cara suavizada: centro, frente, arriba
static float s_dist = 2.0f, s_oYaw = 0, s_oPitch = 0;                            // orbita: distancia, giro, altura
// (02/11 e) en el modo fijo la distancia sale del campo de vision: el visor del editor usa un campo de vision MUY cerrado (0.09 rad,
// camara a 6.5 m) y a 0.6 m fijos se veia solo la nariz. s_fixHalf = media altura visible a la distancia de la cara (m)
static float s_fixHalf = 0.20f;
// (02/11 f, Agustin: "la camara esta muy bien, lo que le falta es la opcion de subir y bajar") Q / E suben y bajan la camara Y el
// punto al que mira (encuadre mas arriba: pelo / mas abajo: menton, cuello, hombros) sin cambiar el angulo; s_lift en metros, a lo
// largo del arriba de la cara (modo fijo) o del mundo (orbitar). Las flechas arriba / abajo siguen girando por arriba / por abajo.
static float s_lift = 0.0f, s_center[3] = {};
static float s_upOut[3] = { 0, 1, 0 };
static const void* s_lockNode = nullptr; static double s_headT = 0;                                          // personaje seguido
static std::atomic<int> s_headFrames{0};

// (02/11) la captura de b6 (expr.cpp ExprCaptureHead) ya no se usa: daba la cabeza casi en cero
bool FreeCamWantsHead() { return false; }
static float s_pos[3] = {}, s_yaw = 0, s_pitch = 0;
static void PoseApplyPending();
static double s_t = 0;
static POINT s_mouse{}; static bool s_mouseWas = false;
static std::atomic<int> s_applies{0}, s_editorCalls{0}, s_editorApplied{0};
static int s_logged = 0;
static const char* s_origen = "";

// (14/11) x344 = 3: solo mientras dura la repeticion (pitchwear.cpp ReplayNow, detector v2 y448); al terminar vuelve la camara del juego
// (14/11 n) tambien con el visor de repeticiones abierto en pausa (ReplayViewerCalled): antes, si se abria en pausa no se prendia nunca
bool FreeCamActive() { const float v = g_cfg.live[344][0]; return v > 2.5f ? (ReplayNow() || ReplayViewerCalled()) : v > 0.5f; }

// (14/11 n, Agustin: "la pausa y despausa a veces no funciona") TOQUES QUE NO SE PIERDEN: la camara libre leia el mando y las teclas una
// vez por cuadro del juego; con el pasto pesado el juego iba a 10 cuadros por segundo y un toque corto de cruz / P (menos de 100 ms)
// caia entre dos cuadros: no pasaba nada. Mientras la camara libre esta prendida un hilo propio mira el mando (MvPadPoll cuenta cada
// boton que se aprieta) y las teclas de toque cada ~4 ms; Update usa esas cuentas para los toques (lo mantenido sigue con el estado).
static const int kTapVk[] = { 'P', 'N', 'B', 'M', VK_OEM_COMMA, VK_OEM_PERIOD, VK_TAB, VK_HOME, VK_OEM_4, VK_OEM_6 };   // (15/11) M = desenfoque de movimiento
static std::atomic<uint32_t> s_keyPress[256];
static uint32_t s_keySeen[256] = {}; static bool s_keyWas[256] = {};
static uint32_t s_padSeen[kPadCount] = {};
static std::atomic<bool> s_sampOn{false};
static std::atomic<int> s_kbRplDir{0};   // teclas I / O en la repeticion: -1 atras lento, 1 adelante lento
static void SamplerLoop() {
    bool was[sizeof kTapVk / sizeof kTapVk[0]] = {};
    for (;;) {
        if (!FreeCamActive()) { s_sampOn = false; memset(was, 0, sizeof was); Sleep(40); continue; }
        MvPad p; MvPadPoll(p);   // cuenta los botones apretados (menuvisual.cpp MvPadPresses)
        for (size_t i = 0; i < sizeof kTapVk / sizeof kTapVk[0]; i++) {
            const bool d = (GetAsyncKeyState(kTapVk[i]) & 0x8000) != 0;
            if (d && !was[i]) s_keyPress[kTapVk[i] & 255].fetch_add(1, std::memory_order_relaxed);
            was[i] = d;
        }
        s_sampOn = true;
        Sleep(4);
    }
}
static DWORD WINAPI SamplerThread(LPVOID) { SamplerLoop(); return 0; }
static void SamplerEnsure() { static bool s_started = false; if (s_started) return; s_started = true;
    if (HANDLE h = CreateThread(nullptr, 0, &SamplerThread, nullptr, 0, nullptr)) CloseHandle(h); }
// toque de una tecla desde el ultimo cuadro (lo contado por el hilo; si el hilo no corre, el flanco de este cuadro). allow = false:
// el toque se descarta (menu abierto, escribiendo, otra ventana)
static bool KeyTap(int vk, bool allow) {
    const int i = vk & 255; const bool d = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const uint32_t c = s_keyPress[i].load(std::memory_order_relaxed);
    const bool t = s_sampOn.load(std::memory_order_relaxed) ? c != s_keySeen[i] : (d && !s_keyWas[i]);
    s_keySeen[i] = c; s_keyWas[i] = d;
    return allow && t;
}

// (14/11, Agustin: "que lea el control para un control preciso", "como si fuera un modo foto pero que tiene la opcion de avanzar en
// el modo repeticion en camara lenta") MANDO EN LA CAMARA LIBRE. overlay.cpp lee el mando cada cuadro (MvPadPoll, la DLL pasa por
// los ganchos de keyblock.cpp) y lo deja aca; el juego no lo ve mientras la camara libre vuela con el mando (g_blockGamePad).
//   stick izquierdo moverse, stick derecho mirar (curva suave: poco recorrido = movimiento fino), L2 / R2 bajar / subir (gatillo
//   analogico), L1 preciso (/4), R1 rapido (x3), cruceta arriba / abajo zoom, cruceta izquierda / derecha = velocidad (y344, pasos
//   de 10 %), triangulo = volver a la camara del juego. MODO FOTO (reloj del juego, tiempo.cpp lo aplica mientras la camara libre
//   esta prendida): cruz pausa / sigue, cuadrado mantenido avanza despacio, L1 + cruceta izquierda / derecha mas lenta / mas rapida
//   (los pasos de kPhotoSteps), circulo vuelve a la velocidad del menu. OPTIONS le devuelve el mando al juego (salir de la
//   repeticion) hasta que la camara libre se apague (o se vuelva a prender el interruptor de la tarjeta).
static std::mutex s_padMx; static MvPad s_padIn; static double s_padT = -1.0;
void FreeCamPadFeed(const MvPad& p) { std::lock_guard<std::mutex> lk(s_padMx); s_padIn = p; s_padT = Now(); }
static bool PadGet(MvPad& p) {
    std::lock_guard<std::mutex> lk(s_padMx);
    if (s_padT < 0.0 || Now() - s_padT > 0.25) return false;
    p = s_padIn; return p.connected;
}
static const float kPhotoSteps[] = { 0.02f, 0.05f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
static const int kPhotoN = (int)(sizeof kPhotoSteps / sizeof kPhotoSteps[0]), kPhotoNormal = 6;
static std::atomic<int> s_phStep{-1}; static std::atomic<bool> s_phPause{false}, s_phAdv{false}, s_padFly{false};
static std::atomic<bool> s_padGive{false};   // OPTIONS: el mando vuelve al juego (para salir de la repeticion) hasta que la camara libre se apague
const float* FreeCamPhotoSteps(int* n) { if (n) *n = kPhotoN; return kPhotoSteps; }
static int NearestStep(float v);
void FreeCamPhotoPause(bool on) { if (s_phStep.load() < 0) s_phStep = NearestStep(1.0f); s_phPause = on; }   // (14/11 k) rastro.cpp: el visor estaba en pausa
static int NearestStep(float v) { int b = kPhotoNormal; float bd = 1e9f; for (int i = 0; i < kPhotoN; i++) { const float d = fabsf(logf(kPhotoSteps[i]) - logf(std::max(0.01f, v))); if (d < bd) { bd = d; b = i; } } return b; }
bool FreeCamPadWanted() {
    if (!g_cfg.camPad || s_padGive.load() || !FreeCamActive() || MotorPageActive()) return false;
    return lroundf(g_cfg.live[344][0]) != 2;   // "sin bloquear el teclado": el juego sigue con todo, el mando no vuela
}
// reloj del juego pedido por el modo foto (-1 = no pide nada: queda lo del menu)
float FreeCamPhotoScale() {
    if (!FreeCamActive()) return -1.0f;
    const int st = s_phStep.load();
    if (st < 0 || st >= kPhotoN) return -1.0f;
    const float sp = kPhotoSteps[st];
    if (s_phPause.load()) return s_phAdv.load() ? (sp > 0.99f ? 0.25f : std::max(sp, 0.1f)) : 0.02f;
    return sp;
}
// (14/11 g) cruceta izquierda / derecha mantenida mientras la camara vuela con el mando (sin L1): el visor de la repeticion la usa
// (14/11 i) desenfoque de profundidad con la camara libre: R3 / B lo prenden o apagan (camara_sin_dof, hooks.cpp UploadParams)
void FreeCamDofToggle(const char* how) {
    g_cfg.camNoDof = g_cfg.camNoDof ? 0 : 1; SaveOption("camara_sin_dof", g_cfg.camNoDof ? "1" : "0");
    Log("camara libre: desenfoque de profundidad %s (%s)", g_cfg.camNoDof ? "APAGADO" : "como el juego", how);
}
// (15/11, Agustin: "permitirme apagar o prender el desenfoque de movimiento") con la camara libre: L3 (soltar sin R3) / M / el menu
// (camara_sin_desenfoque_mov; hooks.cpp UploadParams x138 = 1, y138 = 0 en 24943e55 = sin desenfoque; repeticion.cpp sin barrido)
void FreeCamMblurToggle(const char* how) {
    g_cfg.camNoMblur = g_cfg.camNoMblur ? 0 : 1; SaveOption("camara_sin_desenfoque_mov", g_cfg.camNoMblur ? "1" : "0");
    Log("camara libre: desenfoque de movimiento %s (%s)", g_cfg.camNoMblur ? "APAGADO" : "como el juego (fila 138)", how);
}
int FreeCamReplayDpad() {   // (14/11 n) tambien el teclado: O adelante lento, I atras lento (en la repeticion)
    int d = 0;
    if (s_padFly.load()) { MvPad p; if (PadGet(p) && !p.b[kPadL1]) d = p.b[kPadRight] ? 1 : p.b[kPadLeft] ? -1 : 0; }
    return d != 0 ? d : s_kbRplDir.load(std::memory_order_relaxed);
}
FreeCamPadInfo FreeCamPadState() {
    FreeCamPadInfo i; i.wanted = FreeCamPadWanted(); i.flying = s_padFly.load(); i.step = s_phStep.load(); i.paused = s_phPause.load();
    i.advancing = s_phAdv.load(); i.scale = FreeCamPhotoScale(); i.given = s_padGive.load(); return i;
}
void FreeCamPadTake() { s_padGive = false; }
static void PhotoReset() { s_phStep = -1; s_phPause = false; s_phAdv = false; }
static float s_pv[3] = {}, s_lv[2] = {};   // velocidad / giro del mando, suavizados (arranca y frena sin saltos)
static bool s_padWas[kPadCount] = {};
static bool s_r3 = false, s_r3Combo = false, s_l3 = false, s_l3Combo = false;   // R3 / L3 apretados (soltar = desenfoque / desenfoque de movimiento) y si fue parte de L3 + R3
static float Curve(float x) { return x * (0.25f + 0.75f * fabsf(x)); }   // fino cerca del centro, todo al fondo
// (11/11, Agustin 23:33 / 23:36: el Motor "que venga con la camara libre", manejada como en Blender) Dentro del estudio del Motor
// (MotorPageActive) la camara libre se maneja como en un editor: las teclas W A S D Q E y el giro con el mouse valen SOLO con el
// boton derecho apretado (mientras tanto el juego no ve el teclado); las flechas quedan para el menu del juego. El gizmo de ejes
// (motorgizmo.cpp) pide orbitar alrededor del pivote (el punto al que miraba la camara del juego al prender) o alinear la vista a
// un eje. s_pivot se toma en TakeFromGame.
static float s_pivot[3] = {}; static bool s_pivotOk = false;
static float s_homePos[3] = {}, s_homePiv[3] = {}, s_homeYaw = 0, s_homePitch = 0; static bool s_homeOk = false;   // pose al prender (el visor no reescribe su camara: "inicio" vuelve aca)
static std::atomic<int> s_gzAxis{-1}; static std::atomic<int> s_gzHome{0};
static std::mutex s_gzMx; static float s_gzDYaw = 0, s_gzDPitch = 0;
static bool MotorFly() { return MotorPageActive() && FreeCamActive(); }
// (15/11, Agustin 01:51: el Motor abre "solo el menu", sin cargar al jugador) sin modelo el visor del Editar no pide camara y
// Update no corre: la grilla y el gizmo quedarian muertos. FreeCamMotorTick (hilo de Present, motorpage.cpp) corre Update sobre
// una camara de mentira cuando la del visor lleva 0.3 s sin llegar. Al volver la camara real (se cargo un jugador) se toma su
// pose (s_virtual) para que el jugador quede encuadrado como lo encuadra el juego.
static std::atomic<double> s_realT{-1.0e9}; static bool s_virtual = false;
static float s_fovOut = 0.364f;
void FreeCamGizmoOrbit(float dyaw, float dpitch) { std::lock_guard<std::mutex> lk(s_gzMx); s_gzDYaw += dyaw; s_gzDPitch += dpitch; }
void FreeCamGizmoAxis(int a) { s_gzAxis.store(a); }      // 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z: la camara se pone de ese lado del pivote
void FreeCamGizmoHome() { s_gzHome.store(1); }
// (15/11 q, Agustin 17:28: "faltan las vistas en perspectiva y en no") vista ortografica del Motor (Num 5, como Blender). La proyeccion
// del visor (+0x3c) no se toca: se hace con un teleobjetivo (campo de vision 0.02 rad) y la camara alejada del pivote lo justo para que
// lo que esta en el pivote se vea del mismo tamano; con ese angulo la perspectiva es menos de 1 % de un lado al otro de la escena.
// El cambio es animado (0.25 s) y lo que se ve en el pivote no cambia de tamano mientras pasa. s_orthoFar = lejos que hace falta.
static std::atomic<int> s_orthoWant{ 0 }; static float s_orthoA = 0.0f, s_posOut[3] = {}; static bool s_posOutOk = false; static std::atomic<float> s_orthoFar{ 0.0f };
void FreeCamOrthoSet(int on) { s_orthoWant.store(on ? 1 : 0); }
bool FreeCamOrtho() { return s_orthoWant.load() != 0; }
// (15/11 q, Agustin 17:27: "permitirme crear una vista extra, con otra camara"; 17:39 eligio probar alternando cuadros) VISTA EXTRA:
// el juego dibuja una sola camara por cuadro, asi que en los cuadros "B" el visor recibe la camara extra (orbita alrededor del mismo
// pivote que la camara libre) y motorextra.cpp guarda esa imagen y vuelve a poner en pantalla la del cuadro anterior. Cada vista se
// renueva la mitad de las veces. s_xOff = intercambiar (si el juego dibuja la camara un cuadro despues de recibirla).
static std::atomic<int> s_xOn{ 0 }, s_xOff{ 0 }; static std::atomic<unsigned> s_xFrame{ 0 };
static std::mutex s_xMx; static float s_xYaw = 0.0f, s_xPitch = 0.25f, s_xDist = 3.0f, s_xPiv[3] = { 0.0f, 0.95f, 0.0f }, s_xFov = 0.364f; static bool s_xInit = false;
void FreeCamExtraSet(int on) { s_xOn.store(on ? 1 : 0); if (!on) { std::lock_guard<std::mutex> lk(s_xMx); s_xInit = false; } }
bool FreeCamExtra() { return s_xOn.load() != 0; }
void FreeCamExtraSwap() { s_xOff.store(s_xOff.load() ^ 1); }
void FreeCamExtraTick() { s_xFrame.fetch_add(1); }
bool FreeCamExtraBFrame() { return s_xOn.load() != 0 && ((s_xFrame.load() + (unsigned)s_xOff.load()) & 1u) != 0; }
void FreeCamExtraOrbit(float dyaw, float dpitch) { std::lock_guard<std::mutex> lk(s_xMx); s_xYaw += dyaw; s_xPitch = std::clamp(s_xPitch + dpitch, -1.45f, 1.45f); }
void FreeCamExtraZoom(float f) { std::lock_guard<std::mutex> lk(s_xMx); s_xDist = std::clamp(s_xDist * f, 0.3f, 120.0f); }
void FreeCamExtraPreset(int p) { std::lock_guard<std::mutex> lk(s_xMx);      // 0 frente, 1 costado derecho, 2 arriba, 3 atras, 4 costado izquierdo
    static const float kY[5] = { 0.0f, 1.5707963f, 0.0f, 3.1415927f, -1.5707963f }, kP[5] = { 0.12f, 0.12f, 1.45f, 0.12f, 0.12f };
    const int q = std::clamp(p, 0, 4); s_xYaw = kY[q]; s_xPitch = kP[q]; }
static double s_headLostT = -1.0;   // (08/11 e) cuando se perdio el dato de la cabeza siguiendo la cara (-1 = no)
static bool FreeCamHeadLost() {   // se pidio seguir la cara, se estaba siguiendo y el dato se corto: se suelta hasta que vuelva
    if (s_headLostT < 0.0) return false;
    if (g_cfg.live[346][0] < 0.5f) { s_headLostT = -1.0; return false; }
    if (ExprFaceAny()) { s_headLostT = -1.0; return false; }
    return true;
}
bool FreeCamBlocksKeys() {
    const float v = g_cfg.live[344][0];
    // (12/11, Agustin 00:05: "las teclas del teclado que funcionaban como mando no deberian funcionar asi en el menu ... en el de
    // motor") con el estudio del Motor abierto el juego no ve el teclado: el Motor se maneja con el mouse (y el mando sigue
    // andando); Esc = salir lo pone menumouse.cpp. [PES21FX] motor_teclado = 0 lo deja como antes
    if (g_cfg.motorKeys != 0 && MotorPageActive()) return true;
    if (FreeCamActive() && MotorPageActive()) return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;   // en el Motor: solo mientras se vuela
    return FreeCamActive() && (v < 1.5f || v > 2.5f);   // 1 y 3 (solo en la repeticion) bloquean el teclado mientras vuela
}

static bool Key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static bool GameInFront() {
    DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}
static float Len3(const float* v) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

static void TakeFromGame(const float* pos, const float* tgt) {
    float d[3] = { tgt[0] - pos[0], tgt[1] - pos[1], tgt[2] - pos[2] };
    float l = Len3(d); if (l < 1e-5f) { d[0] = 0; d[1] = 0; d[2] = 1; l = 1; }
    memcpy(s_pos, pos, 12);
    { const float pd = std::clamp(l, 0.3f, 60.0f); for (int i = 0; i < 3; i++) s_pivot[i] = pos[i] + d[i] / l * pd; s_pivotOk = true; }
    s_pitch = asinf(std::clamp(d[1] / l, -1.0f, 1.0f));
    s_yaw = atan2f(d[0], d[2]);
    s_t = Now();
    GetCursorPos(&s_mouse); s_mouseWas = false;
}

// Nucleo: lee las teclas / el mouse, mueve la camara propia y la escribe en pos / tgt / up (xyz) y fov (si proj == 0).
// Devuelve false si la camara libre esta apagada (no toca nada).
static bool Update(float* pos, float* tgt, float* up, float* fov, int proj, const char* origen, const float* extra) {
    if (!FreeCamActive()) {
        if (s_on) { s_on = false; s_follow = false; s_headSeen = false; s_lockNode = nullptr; g_cfg.live[345][3] = -1.0f; Log("camara libre: apagada"); }
        if (s_phStep.load() >= 0 || s_phPause.load()) { PhotoReset(); Log("modo foto: el reloj del juego vuelve a lo del menu"); }
        s_padFly = false; s_padGive = false; memset(s_pv, 0, sizeof s_pv); memset(s_lv, 0, sizeof s_lv); s_kbRplDir = 0; s_r3 = s_l3 = false;
        return false;
    }
    const bool real = strcmp(origen, "motor") != 0;
    if (real) s_realT.store(Now(), std::memory_order_relaxed);
    if (s_on && real && s_virtual) {     // llego la camara del visor despues de la de mentira: se toma su pose
        s_virtual = false; TakeFromGame(pos, tgt);
        memcpy(s_homePos, s_pos, 12); memcpy(s_homePiv, s_pivot, 12); s_homeYaw = s_yaw; s_homePitch = s_pitch; s_homeOk = true; s_fovWritten = -1.0f;
        Log("camara libre: llego la camara del visor (se cargo un modelo): se toma su pose (%.2f, %.2f, %.2f)", s_pos[0], s_pos[1], s_pos[2]);
    }
    if (!s_on) {
        s_virtual = !real;
        MvPadPresses(s_padSeen); for (int vk : kTapVk) { s_keySeen[vk & 255] = s_keyPress[vk & 255].load(); s_keyWas[vk & 255] = true; }   // (14/11 n) lo apretado antes de prender no cuenta
        TakeFromGame(pos, tgt);
        memcpy(s_homePos, s_pos, 12); memcpy(s_homePiv, s_pivot, 12); s_homeYaw = s_yaw; s_homePitch = s_pitch; s_homeOk = true;
        s_on = true; s_zoomTarget = std::clamp(g_cfg.live[344][2], kZoomMin, 3.0f); s_wheel = 0; s_fovWritten = -1.0f;
        Log("camara libre: prendida (%s) en (%.2f, %.2f, %.2f) giro %.0f / %.0f grados; objetivo (%.2f, %.2f, %.2f), arriba (%.2f, %.2f, %.2f), proyeccion %d, campo de vision %.4f, cerca / lejos / aspecto %.3f / %.1f / %.3f",
            origen, pos[0], pos[1], pos[2], s_yaw * 57.2958f, s_pitch * 57.2958f, tgt[0], tgt[1], tgt[2], up[0], up[1], up[2], proj, *fov, extra[0], extra[1], extra[2]);
        s_logged = 0;
    }
    if (strcmp(origen, s_origen) != 0) { s_origen = origen; }
    PoseApplyPending();   // (08/11) pose pedida por el panel en vivo
    // --- entrada (tiempo real) ---
    double t = Now(); float dt = (float)std::clamp(t - s_t, 0.0, 0.1); s_t = t;
    const bool front = GameInFront();
    const bool typing = g_menuTyping.load();
    SamplerEnsure();
    { const bool home = KeyTap(VK_HOME, front);
      if (home) { TakeFromGame(pos, tgt); Log("camara libre: Inicio, se vuelve a la camara del juego"); }
      if (s_gzHome.exchange(0) && s_homeOk) { memcpy(s_pos, s_homePos, 12); memcpy(s_pivot, s_homePiv, 12); s_yaw = s_homeYaw; s_pitch = s_homePitch; s_zoomTarget = 1.0f;
          Log("camara libre: inicio (gizmo del Motor), vuelve a la pose con la que se prendio"); }
    }
    float speed = std::clamp(g_cfg.live[344][1], 0.05f, 50.0f);
    if (Key(VK_SHIFT)) speed *= 4.0f;
    if (Key(VK_CONTROL)) speed *= 0.25f;
    const float sens = std::clamp(g_cfg.live[344][3], 0.05f, 10.0f);
    // (15/11, Agustin: "le falta una opcion para que la camara baja mas rapido o mas lento") subir / bajar (Q / E, L2 / R2) = la
    // velocidad de vuelo x camara_vertical (0.1 .. 5; el menu de la camara libre)
    const float vmul = std::clamp(g_cfg.camVert, 0.1f, 5.0f);
    // (14/11) [ y ] cambian la velocidad base en pasos de 10 % (control fino de la velocidad, Agustin)
    { const bool b0 = KeyTap(VK_OEM_4, front && !typing), b1 = KeyTap(VK_OEM_6, front && !typing);
      if (b0 || b1) { const float was = std::clamp(g_cfg.live[344][1], 0.05f, 50.0f); g_cfg.live[344][1] = std::clamp(g_cfg.live[344][1] * (b1 ? 1.1f : 1.0f / 1.1f), 0.05f, 50.0f); speed *= g_cfg.live[344][1] / was; } }
    // --- mando (14/11) ---
    { static bool s_repWas = false; const bool rep = ReplayNow();   // (14/11 k) al terminar la repeticion la pausa del visor no queda en el partido
      if (s_repWas && !rep && s_phPause.load()) { s_phPause = false; Log("camara libre: termino la repeticion, sin pausa"); } s_repWas = rep; }
    MvPad pad; const bool padOk = front && !typing && !g_menuOpen.load() && FreeCamPadWanted() && PadGet(pad);
    s_padFly = padOk;
    float padMul = 1.0f;
    if (padOk) {
        bool ed[kPadCount]; uint32_t pc[kPadCount]; MvPadPresses(pc);
        for (int i = 0; i < kPadCount; i++) { const bool snap = pad.b[i] && !s_padWas[i]; ed[i] = s_sampOn.load() ? pc[i] != s_padSeen[i] : snap; s_padSeen[i] = pc[i]; s_padWas[i] = pad.b[i]; }   // (14/11 n) toques aunque caigan entre cuadros
        // (14/11, Agustin: "lo de la velocidad ... con las flechas del mando izquierda derecha") cruceta izquierda / derecha = velocidad
        // de la camara de a 10 % (mantener: repite cada 0.12 s despues de 0.35 s); con L1 apretado, la cruceta cambia el reloj
        const bool l1 = pad.b[kPadL1];
        if (!l1 && pad.b[kPadR1]) padMul *= 3.0f;
        if (l1 && !pad.b[kPadR1] && !pad.b[kPadLeft] && !pad.b[kPadRight]) padMul *= 0.25f;
        {   // (14/11 g, Agustin: "usar los gatillos para cambiar de cara, los gatillos mas otro boton") siguiendo la cara: L1 + R2 = la
            // cara siguiente (a la derecha), L1 + L2 = la anterior; al apretar el gatillo hasta la mitad
            static bool s_r2Was = false, s_l2Was = false; const bool r2 = pad.r2 > 0.5f, l2 = pad.l2 > 0.5f;
            if (s_follow && pad.b[kPadL1]) { if (r2 && !s_r2Was) s_subjReq = 1; else if (l2 && !s_l2Was) s_subjReq = -1; }
            s_r2Was = r2; s_l2Was = l2;
        }
        // (14/11 g) en la repeticion la cruceta izquierda / derecha es la del visor (avanzar / retroceder lento con las velocidades de
        // SlowMoServer, rastro.cpp PhotoReplayCtl); fuera de la repeticion, la velocidad de la camara
        if (!ReplayNow()) { static double s_rep = 0.0; const int dir = l1 ? 0 : pad.b[kPadRight] ? 1 : pad.b[kPadLeft] ? -1 : 0;
          const bool edge = !l1 && (ed[kPadLeft] || ed[kPadRight]);
          if (edge) s_rep = t + 0.35;
          if (dir != 0 && (edge || t >= s_rep)) {
              if (!edge) s_rep = t + 0.12;
              const float was = std::clamp(g_cfg.live[344][1], 0.05f, 50.0f);
              g_cfg.live[344][1] = std::clamp(g_cfg.live[344][1] * (dir > 0 ? 1.1f : 1.0f / 1.1f), 0.05f, 50.0f);
              speed *= std::clamp(g_cfg.live[344][1], 0.05f, 50.0f) / was;
              if (edge) Log("camara libre: velocidad %.2f m/s (cruceta)", g_cfg.live[344][1]);
          } }
        // zoom con la cruceta arriba / abajo (suave, como Re Pag / Av Pag)
        if (pad.b[kPadUp]) s_zoomTarget *= 1.0f - 0.45f * dt;
        if (pad.b[kPadDown]) s_zoomTarget *= 1.0f + 0.45f * dt;
        if (ed[kPadTriangle]) { TakeFromGame(pos, tgt); memset(s_pv, 0, sizeof s_pv); Log("camara libre: triangulo, se vuelve a la camara del juego"); }
        // modo foto: el reloj del juego
        auto cur = [] { return g_cfg.live[343][1] > 0.5f && TiempoMatchToo() ? TiempoFactor() : 1.0f; };
        if (ed[kPadCross]) { if (s_phStep.load() < 0) s_phStep = NearestStep(cur()); s_phPause = !s_phPause.load(); Log("modo foto: %s (cruz del mando)", s_phPause.load() ? "pausa" : "sigue"); }
        if (l1 && (ed[kPadLeft] || ed[kPadRight])) {   // L1 + cruceta izquierda / derecha: reloj del modo foto
            int st = s_phStep.load(); if (st < 0) st = NearestStep(cur());
            st = std::clamp(st + (ed[kPadRight] ? 1 : -1), 0, kPhotoN - 1); s_phStep = st;
            Log("modo foto: reloj x%.2f", kPhotoSteps[st]);
        }
        if (ed[kPadCircle]) { PhotoReset(); Log("modo foto: velocidad del menu"); }
        {   // (14/11 i, Agustin: "que cambiar el desenfoque de profundidad sea una opcion en el menu con mando") R3 (soltar sin L3) =
            // desenfoque si / no; L3 + R3 sigue siendo el menu
            if (pad.b[kPadR3] || ed[kPadR3]) { if (!s_r3) s_r3Combo = false; if (pad.b[kPadL3] || ed[kPadL3]) s_r3Combo = true; s_r3 = true; }   // (14/11 n) tambien un toque entre cuadros
            if (!pad.b[kPadR3] && s_r3) { s_r3 = false; if (!s_r3Combo) FreeCamDofToggle("R3"); }
            // (15/11) L3 (soltar sin R3) = desenfoque de movimiento si / no; L3 + R3 sigue siendo el menu
            if (pad.b[kPadL3] || ed[kPadL3]) { if (!s_l3) s_l3Combo = false; if (pad.b[kPadR3] || ed[kPadR3]) s_l3Combo = true; s_l3 = true; }
            if (!pad.b[kPadL3] && s_l3) { s_l3 = false; if (!s_l3Combo) FreeCamMblurToggle("L3"); }
        }
        if (ed[kPadOptions]) { s_padGive = true; Log("camara libre: OPTIONS, el mando vuelve al juego hasta que se apague la camara libre"); }
        s_phAdv = s_phPause.load() && pad.b[kPadSquare];
    } else { memset(s_padWas, 0, sizeof s_padWas); s_phAdv = false; MvPadPresses(s_padSeen);   // sin volar con el mando: los toques contados se descartan
             // (15/11) R3 / L3 apretado y cortado (L3 + R3 abrio el menu en el medio, otra ventana, OPTIONS): al soltarlo no cambia nada
             if (s_r3) s_r3Combo = true; if (s_l3) s_l3Combo = true; }
    // (14/11) modo foto con el teclado: P pausa / sigue, O mantenida avanza, , y . reloj mas lento / rapido, N velocidad del menu
    const bool kbOk = front && !typing && !g_menuOpen.load() && !MotorPageActive();
    {   // (14/11 n) los toques se leen siempre (si no se acepta la tecla, el toque se descarta: no aparece despues al cerrar el menu)
        // (15/11) con Alt apretado las letras no son de la camara: Alt + Shift + B / N / M son atajos de la DLL (piel del cuerpo, tejido,
        // esconder los menus) y antes B / N de la camara se disparaban tambien
        const bool kbL = kbOk && !Key(VK_MENU);
        const bool tab = KeyTap(VK_TAB, kbOk), kP = KeyTap('P', kbL), kC = KeyTap(VK_OEM_COMMA, kbL), kD = KeyTap(VK_OEM_PERIOD, kbL), kN = KeyTap('N', kbL), kB = KeyTap('B', kbL), kM = KeyTap('M', kbL);
        const bool kO = kbOk && Key('O'), kI = kbOk && Key('I');
        const bool rep = ReplayNow() || ReplayViewerCalled();
        s_kbRplDir = (kbOk && rep) ? (kO && !kI ? 1 : kI && !kO ? -1 : 0) : 0;   // (14/11 n) en la repeticion: O adelante lento, I atras lento
        if (kbOk) {
            if (tab && s_follow) s_subjReq = Key(VK_SHIFT) ? -1 : 1;   // (14/11 g) siguiendo la cara: Tab = otro sujeto (Shift + Tab, el anterior)
            auto cur = [] { return g_cfg.live[343][1] > 0.5f && TiempoMatchToo() ? TiempoFactor() : 1.0f; };
            if (kP) { if (s_phStep.load() < 0) s_phStep = NearestStep(cur()); s_phPause = !s_phPause.load(); Log("modo foto: %s (P)", s_phPause.load() ? "pausa" : "sigue"); }
            if (kC || kD) { int st = s_phStep.load(); if (st < 0) st = NearestStep(cur()); st = std::clamp(st + (kD ? 1 : -1), 0, kPhotoN - 1); s_phStep = st; Log("modo foto: reloj x%.2f (teclado)", kPhotoSteps[st]); }
            if (kN) { PhotoReset(); Log("modo foto: velocidad del menu (N)"); }
            if (kB && FreeCamActive()) FreeCamDofToggle("B");   // (14/11 i) B = desenfoque si / no
            if (kM && FreeCamActive()) FreeCamMblurToggle("M");   // (15/11) M = desenfoque de movimiento si / no
            if (!padOk) s_phAdv = s_phPause.load() && kO;
            else if (kO && s_phPause.load()) s_phAdv = true;
        }
    }
    const bool motor = MotorFly();           // dentro del Motor: flechas para el menu, volar solo con el boton derecho
    if (front && !typing) {
        const float rot = 1.2f * dt;   // flechas: ~70 grados por segundo
        if (!s_follow && !motor) {
            if (Key(VK_LEFT)) s_yaw += rot; if (Key(VK_RIGHT)) s_yaw -= rot;
            if (Key(VK_UP)) s_pitch += rot; if (Key(VK_DOWN)) s_pitch -= rot;
        }
        // zoom suave (Agustin 1/11): las teclas y la rueda mueven un objetivo y el zoom real lo sigue con un retardo corto
        if (Key(VK_PRIOR) || Key(VK_OEM_PLUS) || Key(VK_ADD)) s_zoomTarget *= 1.0f - 0.45f * dt;
        if (Key(VK_NEXT) || Key(VK_OEM_MINUS) || Key(VK_SUBTRACT)) s_zoomTarget *= 1.0f + 0.45f * dt;
    }
    {
        const int wheel = s_wheel.exchange(0);
        // (15/11 c, Agustin 02:29: "funciones de vista de motor moderno permitir mover la camara, rotar, panear, zoom") en el Motor la
        // rueda ACERCA la camara al pivote (como un editor), no cambia el campo de vision; fuera del Motor sigue siendo el zoom
        if (wheel != 0 && front && motor && s_pivotOk && !s_follow) {
            float d[3] = { s_pivot[0] - s_pos[0], s_pivot[1] - s_pos[1], s_pivot[2] - s_pos[2] }; const float dist = Len3(d);
            if (dist > 1e-3f) { const float nd = std::clamp(dist * powf(0.88f, (float)wheel / 120.0f), 0.3f, 80.0f); for (int i = 0; i < 3; i++) s_pos[i] = s_pivot[i] - d[i] / dist * nd; }
        } else
        if (wheel != 0 && front) s_zoomTarget *= powf(0.92f, (float)wheel / 120.0f);   // cada muesca: 8 % mas cerca (o mas lejos)
        s_zoomTarget = std::clamp(s_zoomTarget, kZoomMin, 3.0f);
        float z = g_cfg.live[344][2];
        if (fabsf(z - s_zoomTarget) > 1e-4f) z += (s_zoomTarget - z) * std::clamp(6.0f * dt, 0.0f, 1.0f);   // ~1/6 s para llegar
        else { z = s_zoomTarget; }
        // si el usuario movio el slider del menu, el objetivo lo sigue
        if (g_menuOpen.load()) s_zoomTarget = z = std::clamp(g_cfg.live[344][2], kZoomMin, 3.0f);
        g_cfg.live[344][2] = std::clamp(z, kZoomMin, 3.0f);
    }
    // mouse: gira con el boton derecho apretado (no mientras el menu del mod esta abierto: ahi el mouse es del menu)
    POINT m; GetCursorPos(&m);
    const bool look = front && !g_menuOpen.load() && Key(VK_RBUTTON);
    if (look && s_mouseWas && !s_follow) { s_yaw -= (m.x - s_mouse.x) * 0.0025f * sens; s_pitch -= (m.y - s_mouse.y) * 0.0025f * sens; }
    {   // (14/11) stick derecho: mirar, con la velocidad de giro suavizada (~0.1 s) para que se pueda apuntar fino
        const float ks = std::clamp(sens, 0.3f, 3.0f) * (padOk && pad.b[kPadL1] && !pad.b[kPadR1] ? 0.4f : 1.0f);
        const float ty = padOk && !s_follow ? -Curve(pad.rx) * 1.9f * ks : 0.0f, tp = padOk && !s_follow ? -Curve(pad.ry) * 1.4f * ks : 0.0f;
        const float kk = 1.0f - expf(-dt / 0.10f);
        s_lv[0] += (ty - s_lv[0]) * kk; s_lv[1] += (tp - s_lv[1]) * kk;
        if (!s_follow) { s_yaw += s_lv[0] * dt; s_pitch += s_lv[1] * dt; }
    }
    s_pitch = std::clamp(s_pitch, -1.55f, 1.55f);
    if (motor && !s_follow && s_pivotOk) {   // pedidos del gizmo de ejes: orbitar alrededor del pivote / mirar desde un eje
        float dy = 0, dp = 0; { std::lock_guard<std::mutex> lk(s_gzMx); dy = s_gzDYaw; dp = s_gzDPitch; s_gzDYaw = s_gzDPitch = 0; }
        const int ax = s_gzAxis.exchange(-1);
        if (dy != 0.0f || dp != 0.0f || (ax >= 0 && ax < 6)) {
            float d[3] = { s_pivot[0] - s_pos[0], s_pivot[1] - s_pos[1], s_pivot[2] - s_pos[2] };
            const float dist = std::clamp(Len3(d), 0.3f, 60.0f);
            if (ax >= 0 && ax < 6) {
                static const float kF[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };   // hacia donde mira
                s_pitch = asinf(std::clamp(kF[ax][1], -1.0f, 1.0f)); if (ax != 2 && ax != 3) s_yaw = atan2f(kF[ax][0], kF[ax][2]);
                Log("camara libre: vista desde el eje %s%c (gizmo del Motor), a %.2f m del pivote", (ax & 1) ? "-" : "+", "XYZ"[ax / 2], dist);
            } else { s_yaw += dy; s_pitch += dp; }
            s_pitch = std::clamp(s_pitch, -1.55f, 1.55f);
            const float c2 = cosf(s_pitch); const float nf[3] = { c2 * sinf(s_yaw), sinf(s_pitch), c2 * cosf(s_yaw) };
            for (int i = 0; i < 3; i++) s_pos[i] = s_pivot[i] - nf[i] * dist;
        }
    } else if (!motor) { s_gzAxis.store(-1); }
    const float cp = cosf(s_pitch), sp = sinf(s_pitch);
    float f[3] = { cp * sinf(s_yaw), sp, cp * cosf(s_yaw) };
    // derecha: el mundo del juego es diestro (mirando a -z la derecha es +x); Agustin 1/11: con la version anterior A / D iban al reves
    float r[3] = { -f[2], 0.0f, f[0] }; float rl = Len3(r); if (rl > 1e-5f) { r[0] /= rl; r[2] /= rl; } else { r[0] = 1; r[2] = 0; }
    if (motor && !s_follow) {   // (15/11 c) navegacion de editor en el Motor: boton del medio = panear, Alt + clic izquierdo = orbitar el pivote
        static bool navWas = false;
        const bool ok = front && !g_menuOpen.load(), mid = ok && Key(VK_MBUTTON), alt = ok && Key(VK_MENU) && Key(VK_LBUTTON);
        const float mdx = (float)(m.x - s_mouse.x), mdy = (float)(m.y - s_mouse.y);
        if (navWas && mid) {
            float d[3] = { s_pivot[0] - s_pos[0], s_pivot[1] - s_pos[1], s_pivot[2] - s_pos[2] };
            const float sc = (s_pivotOk ? std::clamp(Len3(d), 0.3f, 80.0f) : 3.0f) * 0.0016f;
            const float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };   // arriba de la pantalla
            for (int i = 0; i < 3; i++) { const float dl = (-r[i] * mdx + u[i] * mdy) * sc; s_pos[i] += dl; if (s_pivotOk) s_pivot[i] += dl; }
        } else if (navWas && alt && s_pivotOk && (mdx != 0.0f || mdy != 0.0f)) FreeCamGizmoOrbit(-mdx * 0.005f, -mdy * 0.005f);   // se cumple en el cuadro que sigue
        navWas = mid || alt;
    }
    // --- seguir la cara: el centro de la cara es el objetivo; la camara va fija al rostro (1) u orbita en el mundo (2) ---
    const int mode = std::clamp((int)(g_cfg.live[346][0] + 0.5f), 0, 2);
    const bool wantFollow = mode >= 1;
    float hc[3], hf[3], hu[3]; double age = 1e9;
    if (s_subjReq != 0) { if (wantFollow && ExprFaceCycle(s_pos, f, &s_lockNode, s_subjReq)) Log("camara libre: sigue otra cara (%s)", s_subjReq > 0 ? "siguiente" : "anterior"); s_subjReq = 0; }
    const bool haveHead = wantFollow && ExprFaceFrame(s_pos, f, &s_lockNode, hc, hf, hu, &age) && age < 0.5;
    if (haveHead) {
        const float tau = std::clamp(g_cfg.live[346][1], 0.0f, 2.0f);   // suavizado (s)
        const float k = (tau > 1e-3f && s_headSeen) ? 1.0f - expf(-dt / tau) : 1.0f;
        for (int i = 0; i < 3; i++) { s_head[i] += (hc[i] - s_head[i]) * k; s_hfw[i] += (hf[i] - s_hfw[i]) * k; s_hup[i] += (hu[i] - s_hup[i]) * k; }
        float l = Len3(s_hfw); if (l > 1e-4f) for (float& v : s_hfw) v /= l; else memcpy(s_hfw, hf, 12);
        s_headSeen = true; s_headT = t; s_headFrames++;
    }
    // ejes de la cara (suavizados): F frente (hacia donde mira), U arriba (perpendicular a F), R = U x F
    float F[3] = { s_hfw[0], s_hfw[1], s_hfw[2] }, U[3], R[3];
    { const float d = s_hup[0] * F[0] + s_hup[1] * F[1] + s_hup[2] * F[2];
      for (int i = 0; i < 3; i++) U[i] = s_hup[i] - d * F[i];
      float l = Len3(U); if (l < 1e-3f) { U[0] = 0; U[1] = 1; U[2] = 0; } else for (float& v : U) v /= l;
      R[0] = U[1] * F[2] - U[2] * F[1]; R[1] = U[2] * F[0] - U[0] * F[2]; R[2] = U[0] * F[1] - U[1] * F[0]; }
    const bool follow = haveHead || (wantFollow && s_headSeen && t - s_headT < 1.5);   // cortes cortos del dato: se queda donde esta
    // campo de vision que se va a ver (vertical, radianes): el del juego x zoom
    float fovNow = (s_fovBase > 0.0f ? s_fovBase : (fov && *fov == *fov && *fov > 0.0f ? *fov : 0.8f)) * std::clamp(g_cfg.live[344][2], kZoomMin, 3.0f);
    if (fovNow > 3.2f) fovNow *= 0.0174533f;   // en grados (camara del partido)
    fovNow = std::clamp(fovNow, 0.01f, 2.5f);
    const float tanHalf = tanf(0.5f * fovNow);
    if (follow && (!s_follow || mode != s_followMode)) {   // entrar (o cambiar de modo)
        if (s_follow && s_followMode == 1) s_fixHalf = std::clamp(s_dist * tanHalf, 0.05f, 2.0f);
        if (mode == 1) {   // de frente a la cara, encuadrada como la ultima vez (cabeza y cuello al principio)
            s_dist = std::clamp(s_fixHalf / tanHalf, 0.15f, 50.0f); s_oYaw = 0.0f; s_oPitch = 0.0f; s_lift = 0.0f;
        } else {           // desde donde esta la camara
            float d[3] = { s_pos[0] - s_head[0], s_pos[1] - s_head[1], s_pos[2] - s_head[2] };
            s_dist = std::clamp(Len3(d), 0.15f, 50.0f);
            s_oYaw = atan2f(d[0], d[2]); s_oPitch = asinf(std::clamp(d[1] / (s_dist > 1e-5f ? s_dist : 1.0f), -1.0f, 1.0f));
            s_lift = 0.0f;
        }
        s_follow = true; s_followMode = mode;
        Log("camara libre: siguiendo la cara (%s) en (%.2f, %.2f, %.2f), frente (%.2f, %.2f, %.2f), distancia %.2f m",
            mode == 1 ? "fija al rostro" : "orbitando la cabeza", s_head[0], s_head[1], s_head[2], F[0], F[1], F[2], s_dist);
    } else if (!follow && s_follow) {
        if (s_followMode == 1) s_fixHalf = std::clamp(s_dist * tanHalf, 0.05f, 2.0f);
        s_follow = false; s_lockNode = nullptr; s_headSeen = false; if (wantFollow) s_headLostT = Now();
        Log("camara libre: deja de seguir la cara%s", wantFollow ? " (sin dato de la cabeza: el personaje tiene que verse y la boca por huesos tiene que reconocer su cara)" : "");
    }
    s_upOut[0] = 0.0f; s_upOut[1] = 1.0f; s_upOut[2] = 0.0f;
    if (s_follow) {
        // W / S acercan y alejan, A / D y flechas izquierda / derecha giran alrededor, flechas arriba / abajo giran por arriba / abajo,
        // Q / E bajan y suben la camara (y lo que mira), el mouse con el boton derecho tambien gira (en el modo fijo el angulo es
        // respecto de la cara y se mantiene)
        if (front && !typing) {
            const float rot = 1.2f * dt, base = std::clamp(g_cfg.live[344][1], 0.05f, 50.0f);
            if (Key('W')) s_dist *= 1.0f - 0.6f * dt * (speed / base);
            if (Key('S')) s_dist *= 1.0f + 0.6f * dt * (speed / base);
            if (Key('D') || Key(VK_RIGHT)) s_oYaw -= rot; if (Key('A') || Key(VK_LEFT)) s_oYaw += rot;
            if (Key(VK_UP)) s_oPitch += rot; if (Key(VK_DOWN)) s_oPitch -= rot;
            // subir / bajar: ~1/4 de lo que se ve por segundo (Shift mas rapido, Ctrl mas lento)
            const float lv = 0.5f * std::clamp(s_dist * tanHalf, 0.05f, 2.0f) * dt * (speed / base);
            if (Key('E')) s_lift += lv * vmul; if (Key('Q')) s_lift -= lv * vmul;
        }
        if (padOk) {   // (14/11) mando: stick izquierdo arriba / abajo acerca / aleja, de costado y el derecho giran, L2 / R2 bajan / suben
            const float base = std::clamp(g_cfg.live[344][1], 0.05f, 50.0f), sm = (speed / base) * padMul;
            s_dist *= 1.0f + Curve(pad.ly) * 0.9f * dt * sm;
            s_oYaw -= (Curve(pad.lx) + Curve(pad.rx)) * 1.4f * dt * std::min(padMul, 1.5f);
            s_oPitch -= Curve(pad.ry) * 1.1f * dt * std::min(padMul, 1.5f);
            if (!pad.b[kPadL1]) s_lift += (pad.r2 - pad.l2) * 0.5f * std::clamp(s_dist * tanHalf, 0.05f, 2.0f) * dt * sm * vmul;
        }
        s_lift = std::clamp(s_lift, -1.2f, 0.8f);
        {   // centro del encuadre = cara + subir / bajar
            const float* L = s_followMode == 1 ? U : nullptr;
            for (int i = 0; i < 3; i++) s_center[i] = s_head[i] + s_lift * (L ? L[i] : (i == 1 ? 1.0f : 0.0f));
        }
        if (look && s_mouseWas) { s_oYaw -= (m.x - s_mouse.x) * 0.0025f * sens; s_oPitch += (m.y - s_mouse.y) * 0.0025f * sens; }
        s_dist = std::clamp(s_dist, 0.15f, 50.0f); s_oPitch = std::clamp(s_oPitch, -1.4f, 1.4f);
        const float ocp = cosf(s_oPitch), osp = sinf(s_oPitch);
        if (s_followMode == 1) {
            // direccion de la cara a la camara en los ejes de la cara: 0 / 0 = de frente
            const float cy = cosf(s_oYaw), sy = sinf(s_oYaw);
            for (int i = 0; i < 3; i++) s_pos[i] = s_center[i] + s_dist * (ocp * (cy * F[i] - sy * R[i]) + osp * U[i]);
            memcpy(s_upOut, U, 12);   // la cara queda derecha en la pantalla aunque el jugador incline la cabeza
            if (s_upOut[1] < 0.2f) { s_upOut[0] = 0.0f; s_upOut[1] = 1.0f; s_upOut[2] = 0.0f; }   // cabeza dada vuelta (chilena, caida): sin dar vuelta la imagen
        } else {
            s_pos[0] = s_center[0] + s_dist * ocp * sinf(s_oYaw); s_pos[1] = s_center[1] + s_dist * osp; s_pos[2] = s_center[2] + s_dist * ocp * cosf(s_oYaw);
        }
        float d[3] = { s_center[0] - s_pos[0], s_center[1] - s_pos[1], s_center[2] - s_pos[2] }; float l = Len3(d);
        if (l > 1e-5f) { f[0] = d[0] / l; f[1] = d[1] / l; f[2] = d[2] / l; s_yaw = atan2f(f[0], f[2]); s_pitch = asinf(std::clamp(f[1], -1.0f, 1.0f)); }
    } else if (front && !typing && (!motor || look)) {
        float mv[3] = {};
        if (Key('W')) { mv[0] += f[0]; mv[1] += f[1]; mv[2] += f[2]; }
        if (Key('S')) { mv[0] -= f[0]; mv[1] -= f[1]; mv[2] -= f[2]; }
        if (Key('D')) { mv[0] += r[0]; mv[2] += r[2]; }
        if (Key('A')) { mv[0] -= r[0]; mv[2] -= r[2]; }
        if (Key('E')) mv[1] += vmul;
        if (Key('Q')) mv[1] -= vmul;
        for (int i = 0; i < 3; i++) s_pos[i] += mv[i] * speed * dt;
    }
    {   // (14/11) mando: stick izquierdo adelante / atras / costados (hacia donde mira), L2 / R2 bajar / subir; velocidad suavizada
        float tv[3] = {};
        if (padOk && !s_follow) {
            const float fw = -Curve(pad.ly), sd = Curve(pad.lx), vy = pad.r2 * pad.r2 - pad.l2 * pad.l2;
            const float sp2 = speed * padMul;
            for (int i = 0; i < 3; i++) tv[i] = (f[i] * fw + r[i] * sd) * sp2;
            tv[1] += vy * sp2 * vmul;
        }
        const float kk = 1.0f - expf(-dt / 0.12f);
        for (int i = 0; i < 3; i++) { s_pv[i] += (tv[i] - s_pv[i]) * kk; if (!s_follow) s_pos[i] += s_pv[i] * dt; }
    }
    s_mouse = m; s_mouseWas = look;
    g_cfg.live[346][2] = s_follow ? 1.0f : 0.0f; g_cfg.live[346][3] = haveHead ? (float)age : -1.0f;
    // --- escribir la camara ---
    pos[0] = s_pos[0]; pos[1] = s_pos[1]; pos[2] = s_pos[2];
    if (s_follow) { tgt[0] = s_center[0]; tgt[1] = s_center[1]; tgt[2] = s_center[2]; }
    else { tgt[0] = s_pos[0] + f[0]; tgt[1] = s_pos[1] + f[1]; tgt[2] = s_pos[2] + f[2]; }
    up[0] = s_upOut[0]; up[1] = s_upOut[1]; up[2] = s_upOut[2];
    float fovApplied = -1.0f;
    if (proj == 0) {
        const float z = std::clamp(g_cfg.live[344][2], kZoomMin, 3.0f);
        // El visor del editor NO reescribe su CameraImpl cada cuadro (solo con entrada del pad): multiplicar en el lugar
        // acumulaba el zoom cuadro a cuadro (fov 0.364 -> 0.092 en el log de Agustin). Se guarda el fov base del juego y
        // se escribe base x zoom; si el valor que llega no es el que escribimos, es que el juego lo cambio: nuevo base.
        if (*fov == *fov && *fov > 0.0f && *fov < 180.0f) {
            if (*fov != s_fovWritten) s_fovBase = *fov;
            *fov = s_fovBase * z; s_fovWritten = *fov; fovApplied = *fov;
        }
        // (15/11 q) ortografica: alejar a lo largo de la mirada y cerrar el angulo en la misma proporcion (tan), con el pivote quieto
        const float want = (motor && !s_follow && s_pivotOk && s_orthoWant.load()) ? 1.0f : 0.0f;
        s_orthoA = want > s_orthoA ? std::min(want, s_orthoA + dt / 0.25f) : std::max(want, s_orthoA - dt / 0.25f);
        s_orthoFar.store(0.0f); s_posOutOk = false;
        if (s_orthoA > 0.0f && fovApplied > 0.0f && s_pivotOk) {
            const float e = s_orthoA * s_orthoA * (3.0f - 2.0f * s_orthoA);
            const float t0 = tanf(fovApplied * 0.5f), tO = tanf(0.02f * 0.5f), tN = t0 + (tO - t0) * e;
            float d[3] = { s_pivot[0] - s_pos[0], s_pivot[1] - s_pos[1], s_pivot[2] - s_pos[2] };
            const float dist = std::max(0.3f, d[0] * f[0] + d[1] * f[1] + d[2] * f[2]), nd = dist * t0 / std::max(1.0e-5f, tN);
            for (int i = 0; i < 3; i++) { pos[i] = s_pos[i] - f[i] * (nd - dist); if (!s_follow) tgt[i] = pos[i] + f[i]; }
            *fov = 2.0f * atanf(tN); s_fovWritten = *fov; fovApplied = *fov; s_orthoFar.store(nd); memcpy(s_posOut, pos, 12); s_posOutOk = true;
            static int nl = 0; if (e >= 1.0f && nl++ < 3) Log("camara libre (Motor): vista ortografica, camara a %.1f m del pivote (antes %.2f)", nd, dist);
        }
    }
    float* q = g_cfg.live[345]; q[0] = s_pos[0]; q[1] = s_pos[1]; q[2] = s_pos[2]; q[3] = fovApplied;
    if (++s_applies % 600 == 0 && s_logged < 20) {   // cada ~10 s de uso
        Log("camara libre (%s): (%.2f, %.2f, %.2f) giro %.0f / %.0f, zoom x%.2f (campo de vision %.4f), %s%s", origen, s_pos[0], s_pos[1], s_pos[2],
            s_yaw * 57.2958f, s_pitch * 57.2958f, g_cfg.live[344][2], fovApplied, front ? "ventana al frente" : "ventana atras: sin teclas",
            s_follow ? " - siguiendo la cara" : wantFollow ? " - seguir la cara pedido pero sin dato de la cabeza" : "");
        s_logged++;
    }
    return true;
}

void FreeCamWheel(int delta) { s_wheel += delta; }

// partido / repeticion: parametro del CameraSystem (camhook.cpp)
void FreeCamApply(unsigned char* b) {
    float pos[3], tgt[3], up[3], fov, extra[3]; int proj;
    memcpy(pos, b, 12); memcpy(tgt, b + 0x0c, 12); memcpy(up, b + 0x18, 12); memcpy(&proj, b + 0x2c, 4); memcpy(&fov, b + 0x30, 4);
    memcpy(&extra[0], b + 0x24, 4); memcpy(&extra[1], b + 0x28, 4); memcpy(&extra[2], b + 0x34, 4);
    if (!Update(pos, tgt, up, &fov, proj, "partido", extra)) return;
    memcpy(b, pos, 12); memcpy(b + 0x0c, tgt, 12); memcpy(b + 0x18, up, 12); memcpy(b + 0x30, &fov, 4);
    b[0x48] = 1;   // continua: sin reinicio de la historia temporal (TAA del juego, SSGI, GTAO) en cada cuadro
}

// ---- visor del editor: gancho a RenderTarget::SetCamera(cliente, CameraImpl) ----
static const unsigned kStamp = 0x62787C6A;
static const uintptr_t kSetCam = 0x1419bf3f0ull;
// push rbx; sub rsp,150h; mov rax,[rip+security_cookie]; xor rax,rsp
static const unsigned char kSetCamSig[19] = { 0x40,0x53,0x48,0x81,0xec,0x50,0x01,0x00,0x00,0x48,0x8b,0x05,0x20,0xa0,0xb5,0x01,0x48,0x33,0xc4 };
typedef void (*SetCamFn)(void* rt, unsigned char* cam);
static SetCamFn o_setCam = nullptr;
static bool s_tried = false, s_okEditor = false;

// (1/11) al apagar la camara libre el visor no recompone su camara (solo la reescribe cuando hay entrada del pad), asi que se
// guarda el CameraImpl tal como estaba al prender y se restaura una vez al apagar (pedido de Agustin: volver a la posicion original)
static unsigned char s_orig[0x50]; static bool s_haveOrig = false, s_restore = false;

static std::mutex s_wcMx; static float s_wc[11] = {}; static double s_wcT = 0.0;
static void hk_SetCam(void* rt, unsigned char* cam) {
    s_editorCalls++;
    if (cam && !FreeCamActive() && s_restore) {
        if (s_haveOrig) { memcpy(cam + 0x10, s_orig, sizeof s_orig); Log("camara libre: camara del visor restaurada a la original"); }
        s_restore = false; s_haveOrig = false;
    }
    // (08/11 e) cuelgue de Agustin (07:54 UTC, volcado cuelgue_20261108_0755): con la camara libre siguiendo la cara, el editor recargo
    // al jugador (edit_idle_loop_face.gani) y el hilo de dibujo del juego quedo esperando su lista de comandos para siempre; los
    // cierres de las 21:22 / 21:43 del 07/11 fueron en el mismo momento. Hasta saber la causa exacta: (1) la camara propia se escribe
    // solo en el render target del visor (el primero que llega al prender), no en los que el juego arma al recargar el modelo; (2) si
    // se sigue la cara y el dato de la cabeza se pierde (= recarga), la camara del juego pasa sin tocar hasta que la cara vuelva.
    static void* s_rtMain = nullptr; static double s_rtMainT = 0.0;
    if (!FreeCamActive()) s_rtMain = nullptr;
    if (rt == s_rtMain) s_rtMainT = Now();
    else if (s_rtMain && Now() - s_rtMainT > 1.0 && !FreeCamHeadLost()) { Log("camara libre: el visor cambio de render target (%p -> %p)", s_rtMain, rt); s_rtMain = rt; s_rtMainT = Now(); }   // el visor viejo ya no dibuja: se adopta el nuevo
    if (cam && FreeCamActive() && s_on && s_rtMain && rt != s_rtMain) {
        static int n = 0; if (n++ < 10) Log("camara libre: otro render target del editor (%p, el visor es %p): se deja la camara del juego", rt, s_rtMain);
        o_setCam(rt, cam); return;
    }
    if (cam && FreeCamActive() && s_on && FreeCamHeadLost()) {
        static double t = 0; if (Now() - t > 5.0) { t = Now(); Log("camara libre: sin la cara (recarga del jugador?): la camara del juego pasa sin tocar"); }
        o_setCam(rt, cam); return;
    }
    if (cam && FreeCamActive()) {
        if (!s_on || !s_haveOrig) { memcpy(s_orig, cam + 0x10, sizeof s_orig); s_haveOrig = true; s_restore = true; s_rtMain = rt; s_rtMainT = Now(); }   // (15/11) tambien si la prendio la camara de mentira del Motor
        float* pos = (float*)(cam + 0x10); float* tgt = (float*)(cam + 0x1c); float* up = (float*)(cam + 0x28); float* fov = (float*)(cam + 0x40);
        int proj; memcpy(&proj, cam + 0x3c, 4);
        float extra[3]; memcpy(&extra[0], cam + 0x34, 4); memcpy(&extra[1], cam + 0x38, 4); memcpy(&extra[2], cam + 0x44, 4);
        if (Update(pos, tgt, up, fov, proj, "editor", extra)) s_editorApplied++;
    } else if (!FreeCamActive() && s_on) {
        Update(nullptr, nullptr, nullptr, nullptr, 0, "editor", nullptr);   // solo anota "apagada"
    }
    // (15/11 o, Agustin 05:08 / 05:23: "veo la camara y los recortes de la escena del modo editar", "desmontar los limites de
    // renderizado del modo editar para este menu") dentro del Motor la camara del visor corta mas cerca y mucho mas lejos que la
    // del Editar ([PES21FX] motor_cerca / motor_lejos en metros; 0 = como el juego). Fuera del Motor no se toca.
    // Al salir del Motor se devuelven los valores que tenia esa camara (Agustin 05:28: "que al salir se vuelva a la normal del modo editar").
    static unsigned char* s_clipCam = nullptr; static float s_clipOrig[2] = {}; static bool s_clipOn = false;
    if (cam && s_clipOn && !MotorPageActive()) { if (cam == s_clipCam) { memcpy(cam + 0x34, &s_clipOrig[0], 4); memcpy(cam + 0x38, &s_clipOrig[1], 4); Log("camara libre (Motor): cerca / lejos del visor devueltos a %.3f / %.1f", s_clipOrig[0], s_clipOrig[1]); } s_clipOn = false; }
    if (cam && MotorPageActive()) { int proj; memcpy(&proj, cam + 0x3c, 4); if (proj == 0) {
        float nr, fr; memcpy(&nr, cam + 0x34, 4); memcpy(&fr, cam + 0x38, 4);
        if (!s_clipOn || cam != s_clipCam) { s_clipCam = cam; s_clipOrig[0] = nr; s_clipOrig[1] = fr; s_clipOn = true; }
        const float wn = g_cfg.motorNear, wf = g_cfg.motorFar;
        if (wn > 0.0f && nr == nr && nr > wn) memcpy(cam + 0x34, &wn, 4);
        if (wf > 0.0f && fr == fr && fr < wf) memcpy(cam + 0x38, &wf, 4);
        { const float od = s_orthoFar.load(), of = od + 2000.0f; float cf; memcpy(&cf, cam + 0x38, 4); if (od > 0.0f && cf == cf && cf < of) memcpy(cam + 0x38, &of, 4);
          float cn; memcpy(&cn, cam + 0x34, 4); const float on = od > 20.0f ? std::min(5.0f, od * 0.25f) : 0.0f; if (on > 0.0f && cn == cn && cn < on) memcpy(cam + 0x34, &on, 4); }   // ortografica: lejos sin cortar, cerca un poco mas afuera (precision)
        static int nl = 0; if (nl++ < 3) Log("camara libre (Motor): cerca / lejos del visor %.3f / %.1f -> %.3f / %.1f", nr, fr, wn > 0.0f && nr > wn ? wn : nr, wf > 0.0f && fr < wf ? wf : fr);
    } }
    // (15/11 ythnmm, Agustin: "le falta opciones en el modo editar jugador para mover la camara, hacer zoom") camara del visor del Editar
    // con el mouse (menumouse.cpp MenuViewCamAdjust): se corre solo la copia que se manda al render target y despues se devuelve lo del
    // juego, asi el CameraImpl del visor no acumula nada y al salir queda la camara de siempre. Nunca con la camara libre ni en el Motor.
    // (15/11 f) ahora queda escrita en el CameraImpl (asi el recorte y las sombras del juego ven la misma camara); MenuViewCamAdjust
    // devuelve la del juego al salir de Apariencia
    unsigned char vcSave[0x34];
    if (cam && !FreeCamActive() && !MotorPageActive()) MenuViewCamAdjust(cam, vcSave);   // (ythnmm ec655d9) queda escrita; se restaura al salir de Apariencia
    // (15/11 q) vista extra: en los cuadros B se manda la camara extra y despues se deja la de siempre en el visor
    unsigned char xSave[0x34]; bool xb = false;
    if (cam && FreeCamActive() && MotorPageActive() && s_pivotOk && rt == s_rtMain && FreeCamExtraBFrame()) {
        std::lock_guard<std::mutex> lk(s_xMx);
        if (!s_xInit) { const float d[3] = { s_pos[0] - s_pivot[0], s_pos[1] - s_pivot[1], s_pos[2] - s_pivot[2] }; const float l = Len3(d);
            s_xDist = std::clamp(l, 0.3f, 120.0f); s_xYaw = atan2f(d[0], d[2]) + 1.5707963f; s_xPitch = 0.15f; s_xInit = true;
            // (15/11 r, Agustin 18:43: "no es independiente, se mueve con la vista normal") la camara extra guarda SU punto de giro y su
            // campo de vision al prenderse: mover, girar o hacer zoom con la principal ya no la mueve
            memcpy(s_xPiv, s_pivot, 12); { float f0; memcpy(&f0, cam + 0x40, 4); s_xFov = (f0 == f0 && f0 > 0.1f && f0 < 2.0f) ? f0 : (s_fovBase > 0.1f ? s_fovBase : 0.364f); }
            Log("motor: vista extra prendida (alternando cuadros), camara propia a %.2f m de su punto (%.2f, %.2f, %.2f), de costado, campo de vision %.3f", s_xDist, s_xPiv[0], s_xPiv[1], s_xPiv[2], s_xFov); }
        memcpy(xSave, cam + 0x10, sizeof xSave); xb = true;
        const float cp = cosf(s_xPitch); const float c[3] = { cp * sinf(s_xYaw), sinf(s_xPitch), cp * cosf(s_xYaw) };
        float* pos = (float*)(cam + 0x10); float* tgt = (float*)(cam + 0x1c); float* up = (float*)(cam + 0x28);
        for (int i = 0; i < 3; i++) { pos[i] = s_xPiv[i] + c[i] * s_xDist; tgt[i] = s_xPiv[i]; } up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f;
        memcpy(cam + 0x40, &s_xFov, 4); { float cn; memcpy(&cn, cam + 0x34, 4); const float n0 = 0.05f; if (cn == cn && cn > n0) memcpy(cam + 0x34, &n0, 4); }   // sin la lente ni el cerca de la ortografica
    }
    // (15/11 s, Agustin 19:28: "la grilla no sigue las lineas correctamente") la grilla del Motor (motorgrid.cpp) armaba su camara con
    // la pose de la camara libre y el campo de vision PEDIDO (x345), no con lo que de verdad se le manda al visor (campo de vision ya
    // suavizado, objetivo, arriba, aspecto +0x44): al hacer zoom o girar, las lineas y el jugador se separaban. Se guarda la camara
    // tal cual llega al juego (solo la del visor principal, no la de la vista extra)
    if (cam && FreeCamActive() && MotorPageActive() && rt == s_rtMain && !xb) {
        std::lock_guard<std::mutex> lk(s_wcMx); memcpy(s_wc, cam + 0x10, 12); memcpy(s_wc + 3, cam + 0x1c, 12); memcpy(s_wc + 6, cam + 0x28, 12);
        memcpy(s_wc + 9, cam + 0x40, 4); memcpy(s_wc + 10, cam + 0x44, 4); s_wcT = Now();
    }
    o_setCam(rt, cam);
    if (xb) memcpy(cam + 0x10, xSave, sizeof xSave);
}
// para la grilla del Motor: la ultima camara que se le mando al visor (posicion, objetivo, arriba, campo de vision vertical, aspecto)
bool FreeCamWrittenCam(float* pos, float* tgt, float* up, float* fov, float* asp) {
    std::lock_guard<std::mutex> lk(s_wcMx);
    if (s_wcT <= 0.0 || Now() - s_wcT > 0.5) return false;
    memcpy(pos, s_wc, 12); memcpy(tgt, s_wc + 3, 12); memcpy(up, s_wc + 6, 12); *fov = s_wc[9]; *asp = s_wc[10];
    return s_wc[9] == s_wc[9] && s_wc[9] > 0.001f && s_wc[9] < 3.0f;
}

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}

static void Install() {
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("camara libre: parches en memoria apagados (motor = 0): sin camara libre en el editor"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("camara libre: exe distinto: sin gancho en el editor"); return; }
    if (!Readable((const void*)kSetCam, sizeof kSetCamSig) || memcmp((const void*)kSetCam, kSetCamSig, sizeof kSetCamSig) != 0) {
        Log("camara libre: la firma de 0x1419bf3f0 (camara del visor del editor) no coincide, se omite"); return;
    }
    if (MH_CreateHook((void*)kSetCam, (void*)&hk_SetCam, (void**)&o_setCam) == MH_OK && MH_EnableHook((void*)kSetCam) == MH_OK) s_okEditor = true;
    Log("camara libre: camara del visor del editor (0x1419bf3f0 RenderTarget::SetCamera) %s", s_okEditor ? "enganchada" : "NO se pudo enganchar");
}

// (15/11 q, Agustin 17:40: "le falta resolucion al motor") el visor del Editar dibuja en una imagen del tamano del recuadro loaderTex
// (1238 x 1080 en su PC) y el Motor la muestra estirada. Dentro del Motor se le pide al juego el tamano de la pantalla x motor_resolucion
// con SU funcion de cambiar el tamano en vivo (TaskModelView 0x141ee5490: toma el candado del visor +0x510, rehace sus imagenes y su
// aspecto); al salir del Motor se le devuelve el tamano que tenia. Cadena: edit::ModelView [0x1438040d0] -> +0x78 tarea -> +0x90 Impl
// (+0xfc ancho, +0x100 alto, +0x104 aspecto). Hallazgo: decompilado/hallazgos/20261115_graficos_niveles_y_visor_editar.cpp.md
static bool Readable(const void* p, size_t n);
static void MotorResTick() {
    static uintptr_t s_task = 0; static int s_ow = 0, s_oh = 0; static bool s_set = false; static double s_t = -1.0e9; static int s_nl = 0;
    static int s_exe = -1; if (s_exe < 0) { HMODULE mod = GetModuleHandleW(nullptr); auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
        s_exe = ((uintptr_t)mod == 0x140000000ull && nt->FileHeader.TimeDateStamp == 0x62787C6A) ? 1 : 0; }
    if (s_exe != 1) return;
    const uintptr_t kMV = 0x1438040d0ull;
    const uintptr_t mv = Readable((const void*)kMV, 8) ? *(const uintptr_t*)kMV : 0;
    const uintptr_t task = (mv && Readable((const void*)(mv + 0x78), 8)) ? *(const uintptr_t*)(mv + 0x78) : 0;
    const uintptr_t impl = (task && Readable((const void*)(task + 0x90), 8)) ? *(const uintptr_t*)(task + 0x90) : 0;
    if (!impl || !Readable((const void*)(impl + 0xfc), 12)) { if (task != s_task) s_set = false; return; }
    if (s_set && task != s_task) s_set = false;      // otro visor: lo guardado era del anterior
    const int cw = *(const int*)(impl + 0xfc), ch = *(const int*)(impl + 0x100); const double now = Now();
    typedef void (*ResizeFn)(uintptr_t, int, int, uintptr_t); const ResizeFn resize = (ResizeFn)0x141ee5490ull;
    const bool want = MotorPageActive() && g_cfg.motorRes > 0 && g_bbW >= 640 && g_bbH >= 360;
    // (15/11 r, Agustin 18:43: "la resolucion parece seguir siendo baja" con el visor ya a 1920 x 1080) escala de resolucion interna del
    // juego (menu de graficos, fila 2, 50..100 %: byte 0x14352c638, por cuadro float 0x143802f38 = pct / 100; decompilado 15/11,
    // INFERIDO que es la resolucion interna). En el Motor se pone al 100 % y al salir vuelve la del juego. Se anota cada cambio.
    {   static int s_pctOrig = -1; static int s_lastPct = -2; static float s_lastF = -2.0f; static int s_nlp = 0;
        unsigned char* pPct = (unsigned char*)0x14352c638ull; const float* pF = (const float*)0x143802f38ull;
        if (Readable(pPct, 1) && Readable(pF, 4)) {
            const int pct = *pPct; const float f = *pF;
            if ((pct != s_lastPct || std::fabs(f - s_lastF) > 0.001f) && s_nlp < 16) { s_nlp++; Log("motor: escala de resolucion interna del juego %d %% (por cuadro %.3f)%s", pct, f, MotorPageActive() ? " - en el Motor" : ""); }
            s_lastPct = pct; s_lastF = f;
            if (want && pct >= 50 && pct < 100) { if (s_pctOrig < 0) s_pctOrig = pct; DWORD op; if (VirtualProtect(pPct, 1, PAGE_READWRITE, &op)) { *pPct = 100; VirtualProtect(pPct, 1, op, &op); } }
            else if (!want && s_pctOrig >= 0) { DWORD op; if (VirtualProtect(pPct, 1, PAGE_READWRITE, &op)) { *pPct = (unsigned char)s_pctOrig; VirtualProtect(pPct, 1, op, &op); } s_pctOrig = -1; }
        } }
    if (want) {
        const float sc = std::clamp((float)g_cfg.motorRes / 100.0f, 0.5f, 2.0f);
        const int W = std::clamp((int)((float)g_bbW * sc + 0.5f) & ~1, 320, 8192), H = std::clamp((int)((float)g_bbH * sc + 0.5f) & ~1, 180, 8192);
        if (cw == W && ch == H) return;
        if (now - s_t < 1.0) return; s_t = now;
        if (!s_set) { s_ow = cw; s_oh = ch; s_task = task; s_set = true; }
        resize(task, W, H, 0);
        if (s_nl++ < 12) Log("motor: visor a %d x %d (%d %% de la pantalla %u x %u; tenia %d x %d, aspecto %.3f)", W, H, g_cfg.motorRes, g_bbW, g_bbH, cw, ch, *(const float*)(impl + 0x104));
    } else if (s_set) {
        if (cw != s_ow || ch != s_oh) { if (now - s_t < 0.2) return; s_t = now; resize(task, s_ow, s_oh, 0); if (s_nl++ < 12) Log("motor: visor devuelto a %d x %d (fuera del Motor)", s_ow, s_oh); }
        s_set = false;
    }
}

void FreeCamPresent() {
    if (!s_tried) Install();
    MotorResTick();
    static double s_t0 = 0; static int s_c0 = 0, s_a0 = 0;
    double t = Now();
    if (s_t0 == 0) { s_t0 = t; return; }
    if (t - s_t0 < 30.0) return;
    const int c = s_editorCalls.load() - s_c0, a = s_editorApplied.load() - s_a0;
    if (c > 0 || a > 0) Log("camara libre (30 s): camara del visor del editor %d veces (%d con la camara libre), %s", c, a, FreeCamActive() ? "prendida" : "apagada");
    s_t0 = t; s_c0 = s_editorCalls.load(); s_a0 = s_editorApplied.load();
}

// (08/11) panel en vivo (panel.cpp): poner / leer la pose de la camara libre desde la pagina. Se prende la camara libre si no
// lo estaba (x344 = 2: el juego sigue viendo el teclado) y se sale del modo seguir la cara.
static std::mutex s_poseMx; static bool s_poseWant = false; static float s_posePos[3]; static float s_poseYaw = 0, s_posePitch = 0;
void FreeCamSetPose(const float* pos, float yaw, float pitch) {
    std::lock_guard<std::mutex> lk(s_poseMx);
    memcpy(s_posePos, pos, 12); s_poseYaw = yaw; s_posePitch = pitch; s_poseWant = true;
    if (!FreeCamActive()) g_cfg.live[344][0] = 2.0f;
}
bool FreeCamGetPose(float* pos, float* yaw, float* pitch) { memcpy(pos, s_posOutOk ? s_posOut : s_pos, 12);   /* (15/11 q) ortografica: la camara alejada que se escribe */ *yaw = s_yaw; *pitch = s_pitch; return s_on; }
// (15/11) Motor sin modelo: mantiene viva la camara libre aunque el visor no pida camara. Camara de mentira = la del estudio del
// Editar (a 6.5 m, campo de vision 0.364 rad, mirando al centro a 0.95 m del piso). Devuelve el campo de vision vertical que se ve
void FreeCamMotorTick() {
    if (!FreeCamActive() || !MotorPageActive()) return;
    if (Now() - s_realT.load(std::memory_order_relaxed) < 0.3) return;
    static float pos[3], tgt[3], up[3], fov; static const float extra[3] = { 0.1f, 1000.0f, 1.7778f };
    if (!s_on) { pos[0] = 0; pos[1] = 0.95f; pos[2] = 6.5f; tgt[0] = 0; tgt[1] = 0.95f; tgt[2] = 0; up[0] = 0; up[1] = 1; up[2] = 0; fov = 0.364f; s_fovWritten = -1.0f; }
    else if (!s_virtual) { s_virtual = true; fov = s_fovBase > 0.0f ? s_fovBase : 0.364f; s_fovWritten = -1.0f; }
    Update(pos, tgt, up, &fov, 0, "motor", extra);
    s_fovOut = fov;
}
// para la grilla del Motor (motorgrid.cpp): campo de vision vertical en radianes de la camara libre
float FreeCamFovNow() { const float f = g_cfg.live[345][3]; return (f > 0.001f && f < 3.0f) ? f : s_fovOut; }
// se llama desde Update (hilo de render) despues de TakeFromGame al prender
static void PoseApplyPending() {
    std::lock_guard<std::mutex> lk(s_poseMx);
    if (!s_poseWant) return;
    s_poseWant = false; memcpy(s_pos, s_posePos, 12); s_yaw = s_poseYaw; s_pitch = std::clamp(s_posePitch, -1.55f, 1.55f);
    if (s_follow) { s_follow = false; s_lockNode = nullptr; s_headSeen = false; }
    Log("camara libre: pose puesta desde el panel (%.2f, %.2f, %.2f) giro %.0f / %.0f", s_pos[0], s_pos[1], s_pos[2], s_yaw * 57.2958f, s_pitch * 57.2958f);
}

} // namespace fx
