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
bool FreeCamActive() { const float v = g_cfg.live[344][0]; return v > 2.5f ? ReplayNow() : v > 0.5f; }

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
int FreeCamReplayDpad() { if (!s_padFly.load()) return 0; MvPad p; if (!PadGet(p) || p.b[kPadL1]) return 0; return p.b[kPadRight] ? 1 : p.b[kPadLeft] ? -1 : 0; }
FreeCamPadInfo FreeCamPadState() {
    FreeCamPadInfo i; i.wanted = FreeCamPadWanted(); i.flying = s_padFly.load(); i.step = s_phStep.load(); i.paused = s_phPause.load();
    i.advancing = s_phAdv.load(); i.scale = FreeCamPhotoScale(); i.given = s_padGive.load(); return i;
}
void FreeCamPadTake() { s_padGive = false; }
static void PhotoReset() { s_phStep = -1; s_phPause = false; s_phAdv = false; }
static float s_pv[3] = {}, s_lv[2] = {};   // velocidad / giro del mando, suavizados (arranca y frena sin saltos)
static bool s_padWas[kPadCount] = {};
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
void FreeCamGizmoOrbit(float dyaw, float dpitch) { std::lock_guard<std::mutex> lk(s_gzMx); s_gzDYaw += dyaw; s_gzDPitch += dpitch; }
void FreeCamGizmoAxis(int a) { s_gzAxis.store(a); }      // 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z: la camara se pone de ese lado del pivote
void FreeCamGizmoHome() { s_gzHome.store(1); }
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
        s_padFly = false; s_padGive = false; memset(s_pv, 0, sizeof s_pv); memset(s_lv, 0, sizeof s_lv);
        return false;
    }
    if (!s_on) {
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
    { static bool s_homeWas = false; const bool home = front && Key(VK_HOME);
      if (home && !s_homeWas) { TakeFromGame(pos, tgt); Log("camara libre: Inicio, se vuelve a la camara del juego"); }
      if (s_gzHome.exchange(0) && s_homeOk) { memcpy(s_pos, s_homePos, 12); memcpy(s_pivot, s_homePiv, 12); s_yaw = s_homeYaw; s_pitch = s_homePitch; s_zoomTarget = 1.0f;
          Log("camara libre: inicio (gizmo del Motor), vuelve a la pose con la que se prendio"); }
      s_homeWas = home; }
    float speed = std::clamp(g_cfg.live[344][1], 0.05f, 50.0f);
    if (Key(VK_SHIFT)) speed *= 4.0f;
    if (Key(VK_CONTROL)) speed *= 0.25f;
    const float sens = std::clamp(g_cfg.live[344][3], 0.05f, 10.0f);
    // (14/11) [ y ] cambian la velocidad base en pasos de 10 % (control fino de la velocidad, Agustin)
    { static bool s_b0 = false, s_b1 = false; const bool b0 = front && !typing && Key(VK_OEM_4), b1 = front && !typing && Key(VK_OEM_6);
      if ((b0 && !s_b0) || (b1 && !s_b1)) { const float was = std::clamp(g_cfg.live[344][1], 0.05f, 50.0f); g_cfg.live[344][1] = std::clamp(g_cfg.live[344][1] * (b1 ? 1.1f : 1.0f / 1.1f), 0.05f, 50.0f); speed *= g_cfg.live[344][1] / was; }
      s_b0 = b0; s_b1 = b1; }
    // --- mando (14/11) ---
    { static bool s_repWas = false; const bool rep = ReplayNow();   // (14/11 k) al terminar la repeticion la pausa del visor no queda en el partido
      if (s_repWas && !rep && s_phPause.load()) { s_phPause = false; Log("camara libre: termino la repeticion, sin pausa"); } s_repWas = rep; }
    MvPad pad; const bool padOk = front && !typing && !g_menuOpen.load() && FreeCamPadWanted() && PadGet(pad);
    s_padFly = padOk;
    float padMul = 1.0f;
    if (padOk) {
        bool ed[kPadCount]; for (int i = 0; i < kPadCount; i++) { ed[i] = pad.b[i] && !s_padWas[i]; s_padWas[i] = pad.b[i]; }
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
        if (ed[kPadCross]) { if (s_phStep.load() < 0) s_phStep = NearestStep(cur()); s_phPause = !s_phPause.load(); Log("modo foto: %s", s_phPause.load() ? "pausa" : "sigue"); }
        if (l1 && (ed[kPadLeft] || ed[kPadRight])) {   // L1 + cruceta izquierda / derecha: reloj del modo foto
            int st = s_phStep.load(); if (st < 0) st = NearestStep(cur());
            st = std::clamp(st + (ed[kPadRight] ? 1 : -1), 0, kPhotoN - 1); s_phStep = st;
            Log("modo foto: reloj x%.2f", kPhotoSteps[st]);
        }
        if (ed[kPadCircle]) { PhotoReset(); Log("modo foto: velocidad del menu"); }
        {   // (14/11 i, Agustin: "que cambiar el desenfoque de profundidad sea una opcion en el menu con mando") R3 (soltar sin L3) =
            // desenfoque si / no; L3 + R3 sigue siendo el menu
            static bool s_r3 = false, s_r3Combo = false;
            if (pad.b[kPadR3]) { if (!s_r3) s_r3Combo = false; if (pad.b[kPadL3]) s_r3Combo = true; s_r3 = true; }
            else if (s_r3) { s_r3 = false; if (!s_r3Combo) FreeCamDofToggle("R3"); }
        }
        if (ed[kPadOptions]) { s_padGive = true; Log("camara libre: OPTIONS, el mando vuelve al juego hasta que se apague la camara libre"); }
        s_phAdv = s_phPause.load() && pad.b[kPadSquare];
    } else { memset(s_padWas, 0, sizeof s_padWas); s_phAdv = false; }
    // (14/11) modo foto con el teclado: P pausa / sigue, O mantenida avanza, , y . reloj mas lento / rapido, N velocidad del menu
    if (front && !typing && !g_menuOpen.load() && !MotorPageActive()) {
        { static bool s_tabWas = false; const bool tab = Key(VK_TAB);   // (14/11 g) siguiendo la cara: Tab = otro sujeto (Shift + Tab, el anterior)
          if (tab && !s_tabWas && s_follow) s_subjReq = Key(VK_SHIFT) ? -1 : 1; s_tabWas = tab; }
        static bool s_kw[5] = {}; const int vk[5] = { 'P', 'O', VK_OEM_COMMA, VK_OEM_PERIOD, 'N' }; bool kd[5], ke[5];
        for (int i = 0; i < 5; i++) { kd[i] = Key(vk[i]); ke[i] = kd[i] && !s_kw[i]; s_kw[i] = kd[i]; }
        auto cur = [] { return g_cfg.live[343][1] > 0.5f && TiempoMatchToo() ? TiempoFactor() : 1.0f; };
        if (ke[0]) { if (s_phStep.load() < 0) s_phStep = NearestStep(cur()); s_phPause = !s_phPause.load(); Log("modo foto: %s (P)", s_phPause.load() ? "pausa" : "sigue"); }
        if (ke[2] || ke[3]) { int st = s_phStep.load(); if (st < 0) st = NearestStep(cur()); st = std::clamp(st + (ke[3] ? 1 : -1), 0, kPhotoN - 1); s_phStep = st; Log("modo foto: reloj x%.2f (teclado)", kPhotoSteps[st]); }
        if (ke[4]) { PhotoReset(); Log("modo foto: velocidad del menu (N)"); }
        { static bool s_bWas = false; const bool bk = Key('B'); if (bk && !s_bWas && FreeCamActive()) FreeCamDofToggle("B"); s_bWas = bk; }   // (14/11 i) B = desenfoque si / no
        if (!padOk) s_phAdv = s_phPause.load() && kd[1];
        else if (kd[1] && s_phPause.load()) s_phAdv = true;
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
            if (Key('E')) s_lift += lv; if (Key('Q')) s_lift -= lv;
        }
        if (padOk) {   // (14/11) mando: stick izquierdo arriba / abajo acerca / aleja, de costado y el derecho giran, L2 / R2 bajan / suben
            const float base = std::clamp(g_cfg.live[344][1], 0.05f, 50.0f), sm = (speed / base) * padMul;
            s_dist *= 1.0f + Curve(pad.ly) * 0.9f * dt * sm;
            s_oYaw -= (Curve(pad.lx) + Curve(pad.rx)) * 1.4f * dt * std::min(padMul, 1.5f);
            s_oPitch -= Curve(pad.ry) * 1.1f * dt * std::min(padMul, 1.5f);
            if (!pad.b[kPadL1]) s_lift += (pad.r2 - pad.l2) * 0.5f * std::clamp(s_dist * tanHalf, 0.05f, 2.0f) * dt * sm;
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
        if (Key('E')) mv[1] += 1.0f;
        if (Key('Q')) mv[1] -= 1.0f;
        for (int i = 0; i < 3; i++) s_pos[i] += mv[i] * speed * dt;
    }
    {   // (14/11) mando: stick izquierdo adelante / atras / costados (hacia donde mira), L2 / R2 bajar / subir; velocidad suavizada
        float tv[3] = {};
        if (padOk && !s_follow) {
            const float fw = -Curve(pad.ly), sd = Curve(pad.lx), vy = pad.r2 * pad.r2 - pad.l2 * pad.l2;
            const float sp2 = speed * padMul;
            for (int i = 0; i < 3; i++) tv[i] = (f[i] * fw + r[i] * sd) * sp2;
            tv[1] += vy * sp2;
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
        if (!s_on) { memcpy(s_orig, cam + 0x10, sizeof s_orig); s_haveOrig = true; s_restore = true; s_rtMain = rt; s_rtMainT = Now(); }
        float* pos = (float*)(cam + 0x10); float* tgt = (float*)(cam + 0x1c); float* up = (float*)(cam + 0x28); float* fov = (float*)(cam + 0x40);
        int proj; memcpy(&proj, cam + 0x3c, 4);
        float extra[3]; memcpy(&extra[0], cam + 0x34, 4); memcpy(&extra[1], cam + 0x38, 4); memcpy(&extra[2], cam + 0x44, 4);
        if (Update(pos, tgt, up, fov, proj, "editor", extra)) s_editorApplied++;
    } else if (!FreeCamActive() && s_on) {
        Update(nullptr, nullptr, nullptr, nullptr, 0, "editor", nullptr);   // solo anota "apagada"
    }
    o_setCam(rt, cam);
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

void FreeCamPresent() {
    if (!s_tried) Install();
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
bool FreeCamGetPose(float* pos, float* yaw, float* pitch) { memcpy(pos, s_pos, 12); *yaw = s_yaw; *pitch = s_pitch; return s_on; }
// se llama desde Update (hilo de render) despues de TakeFromGame al prender
static void PoseApplyPending() {
    std::lock_guard<std::mutex> lk(s_poseMx);
    if (!s_poseWant) return;
    s_poseWant = false; memcpy(s_pos, s_posePos, 12); s_yaw = s_poseYaw; s_pitch = std::clamp(s_posePitch, -1.55f, 1.55f);
    if (s_follow) { s_follow = false; s_lockNode = nullptr; s_headSeen = false; }
    Log("camara libre: pose puesta desde el panel (%.2f, %.2f, %.2f) giro %.0f / %.0f", s_pos[0], s_pos[1], s_pos[2], s_yaw * 57.2958f, s_pitch * 57.2958f);
}

} // namespace fx
