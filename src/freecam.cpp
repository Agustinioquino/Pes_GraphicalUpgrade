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

namespace fx {

static bool s_on = false;
static float s_zoomTarget = 1.0f; static std::atomic<int> s_wheel{0};
static float s_fovBase = 0.0f, s_fovWritten = -1.0f;   // fov del juego sin zoom / ultimo fov escrito
// (1/11) "seguir la cara" (x346): la camara orbita alrededor de la cabeza (expr.cpp la saca de la matriz de sk_head del dibujo de la cara)
static bool s_follow = false, s_headSeen = false;
static float s_head[3] = {}, s_dist = 2.0f, s_oYaw = 0, s_oPitch = 0;   // cabeza suavizada; orbita: distancia, giro alrededor, altura
static std::atomic<int> s_headFrames{0};

bool FreeCamWantsHead() { return FreeCamActive() && g_cfg.live[346][0] > 0.5f; }
static float s_pos[3] = {}, s_yaw = 0, s_pitch = 0;
static double s_t = 0;
static POINT s_mouse{}; static bool s_mouseWas = false;
static std::atomic<int> s_applies{0}, s_editorCalls{0}, s_editorApplied{0};
static int s_logged = 0;
static const char* s_origen = "";

bool FreeCamActive() { return g_cfg.live[344][0] > 0.5f; }
bool FreeCamBlocksKeys() { const float v = g_cfg.live[344][0]; return v > 0.5f && v < 1.5f; }

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
    s_pitch = asinf(std::clamp(d[1] / l, -1.0f, 1.0f));
    s_yaw = atan2f(d[0], d[2]);
    s_t = Now();
    GetCursorPos(&s_mouse); s_mouseWas = false;
}

// Nucleo: lee las teclas / el mouse, mueve la camara propia y la escribe en pos / tgt / up (xyz) y fov (si proj == 0).
// Devuelve false si la camara libre esta apagada (no toca nada).
static bool Update(float* pos, float* tgt, float* up, float* fov, int proj, const char* origen, const float* extra) {
    if (!FreeCamActive()) {
        if (s_on) { s_on = false; s_follow = false; s_headSeen = false; Log("camara libre: apagada"); }
        return false;
    }
    if (!s_on) {
        TakeFromGame(pos, tgt);
        s_on = true; s_zoomTarget = std::clamp(g_cfg.live[344][2], 0.05f, 3.0f); s_wheel = 0; s_fovWritten = -1.0f;
        Log("camara libre: prendida (%s) en (%.2f, %.2f, %.2f) giro %.0f / %.0f grados; objetivo (%.2f, %.2f, %.2f), arriba (%.2f, %.2f, %.2f), proyeccion %d, campo de vision %.4f, cerca / lejos / aspecto %.3f / %.1f / %.3f",
            origen, pos[0], pos[1], pos[2], s_yaw * 57.2958f, s_pitch * 57.2958f, tgt[0], tgt[1], tgt[2], up[0], up[1], up[2], proj, *fov, extra[0], extra[1], extra[2]);
        s_logged = 0;
    }
    if (strcmp(origen, s_origen) != 0) { s_origen = origen; }
    // --- entrada (tiempo real) ---
    double t = Now(); float dt = (float)std::clamp(t - s_t, 0.0, 0.1); s_t = t;
    const bool front = GameInFront();
    const bool typing = g_menuTyping.load();
    { static bool s_homeWas = false; const bool home = front && Key(VK_HOME);
      if (home && !s_homeWas) { TakeFromGame(pos, tgt); Log("camara libre: Inicio, se vuelve a la camara del juego"); }
      s_homeWas = home; }
    float speed = std::clamp(g_cfg.live[344][1], 0.05f, 50.0f);
    if (Key(VK_SHIFT)) speed *= 4.0f;
    if (Key(VK_CONTROL)) speed *= 0.25f;
    const float sens = std::clamp(g_cfg.live[344][3], 0.05f, 10.0f);
    if (front && !typing) {
        const float rot = 1.2f * dt;   // flechas: ~70 grados por segundo
        if (!s_follow) {
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
        s_zoomTarget = std::clamp(s_zoomTarget, 0.05f, 3.0f);
        float z = g_cfg.live[344][2];
        if (fabsf(z - s_zoomTarget) > 1e-4f) z += (s_zoomTarget - z) * std::clamp(6.0f * dt, 0.0f, 1.0f);   // ~1/6 s para llegar
        else { z = s_zoomTarget; }
        // si el usuario movio el slider del menu, el objetivo lo sigue
        if (g_menuOpen.load()) s_zoomTarget = z = std::clamp(g_cfg.live[344][2], 0.05f, 3.0f);
        g_cfg.live[344][2] = std::clamp(z, 0.05f, 3.0f);
    }
    // mouse: gira con el boton derecho apretado (no mientras el menu del mod esta abierto: ahi el mouse es del menu)
    POINT m; GetCursorPos(&m);
    const bool look = front && !g_menuOpen.load() && Key(VK_RBUTTON);
    if (look && s_mouseWas && !s_follow) { s_yaw -= (m.x - s_mouse.x) * 0.0025f * sens; s_pitch -= (m.y - s_mouse.y) * 0.0025f * sens; }
    s_pitch = std::clamp(s_pitch, -1.55f, 1.55f);
    const float cp = cosf(s_pitch), sp = sinf(s_pitch);
    float f[3] = { cp * sinf(s_yaw), sp, cp * cosf(s_yaw) };
    // derecha: el mundo del juego es diestro (mirando a -z la derecha es +x); Agustin 1/11: con la version anterior A / D iban al reves
    float r[3] = { -f[2], 0.0f, f[0] }; float rl = Len3(r); if (rl > 1e-5f) { r[0] /= rl; r[2] /= rl; } else { r[0] = 1; r[2] = 0; }
    // --- seguir la cara: la cabeza es el objetivo y la camara orbita a su alrededor ---
    float head[3]; double age = 1e9; const bool wantFollow = g_cfg.live[346][0] > 0.5f;
    const bool haveHead = wantFollow && ExprHeadWorld(head, &age) && age < 1.0;
    if (haveHead) {
        const float tau = std::clamp(g_cfg.live[346][1], 0.0f, 2.0f);   // suavizado de la cabeza (s)
        const float k = tau > 1e-3f ? 1.0f - expf(-dt / tau) : 1.0f;
        if (!s_headSeen) memcpy(s_head, head, 12); else for (int i = 0; i < 3; i++) s_head[i] += (head[i] - s_head[i]) * k;
        s_headSeen = true; s_headFrames++;
    }
    const bool follow = haveHead || (wantFollow && s_headSeen && age < 3.0);
    if (follow && !s_follow) {   // entrar a la orbita desde donde esta la camara
        float d[3] = { s_pos[0] - s_head[0], s_pos[1] - s_head[1], s_pos[2] - s_head[2] };
        s_dist = std::clamp(Len3(d), 0.15f, 50.0f);
        s_oYaw = atan2f(d[0], d[2]); s_oPitch = asinf(std::clamp(d[1] / (s_dist > 1e-5f ? s_dist : 1.0f), -1.0f, 1.0f));
        s_follow = true;
        Log("camara libre: siguiendo la cara en (%.2f, %.2f, %.2f), distancia %.2f m", s_head[0], s_head[1], s_head[2], s_dist);
    } else if (!follow && s_follow) {
        s_follow = false; Log("camara libre: deja de seguir la cara%s", wantFollow ? " (sin dato de la cabeza: hay que estar en el visor del editor con la cara a la vista)" : "");
    }
    if (s_follow) {
        // en la orbita: W / S acercan y alejan, A / D y flechas izquierda / derecha giran alrededor, Q / E y flechas arriba / abajo
        // suben y bajan, el mouse con el boton derecho tambien orbita
        if (front && !typing) {
            const float rot = 1.2f * dt;
            if (Key('W')) s_dist *= 1.0f - 0.6f * dt * (speed / std::clamp(g_cfg.live[344][1], 0.05f, 50.0f));
            if (Key('S')) s_dist *= 1.0f + 0.6f * dt * (speed / std::clamp(g_cfg.live[344][1], 0.05f, 50.0f));
            if (Key('D') || Key(VK_RIGHT)) s_oYaw -= rot; if (Key('A') || Key(VK_LEFT)) s_oYaw += rot;
            if (Key('E') || Key(VK_UP)) s_oPitch += rot; if (Key('Q') || Key(VK_DOWN)) s_oPitch -= rot;
        }
        if (look && s_mouseWas) { s_oYaw -= (m.x - s_mouse.x) * 0.0025f * sens; s_oPitch += (m.y - s_mouse.y) * 0.0025f * sens; }
        s_dist = std::clamp(s_dist, 0.15f, 50.0f); s_oPitch = std::clamp(s_oPitch, -1.4f, 1.4f);
        const float ocp = cosf(s_oPitch), osp = sinf(s_oPitch);
        s_pos[0] = s_head[0] + s_dist * ocp * sinf(s_oYaw); s_pos[1] = s_head[1] + s_dist * osp; s_pos[2] = s_head[2] + s_dist * ocp * cosf(s_oYaw);
        float d[3] = { s_head[0] - s_pos[0], s_head[1] - s_pos[1], s_head[2] - s_pos[2] }; float l = Len3(d);
        if (l > 1e-5f) { f[0] = d[0] / l; f[1] = d[1] / l; f[2] = d[2] / l; s_yaw = atan2f(f[0], f[2]); s_pitch = asinf(std::clamp(f[1], -1.0f, 1.0f)); }
    } else if (front && !typing) {
        float mv[3] = {};
        if (Key('W')) { mv[0] += f[0]; mv[1] += f[1]; mv[2] += f[2]; }
        if (Key('S')) { mv[0] -= f[0]; mv[1] -= f[1]; mv[2] -= f[2]; }
        if (Key('D')) { mv[0] += r[0]; mv[2] += r[2]; }
        if (Key('A')) { mv[0] -= r[0]; mv[2] -= r[2]; }
        if (Key('E')) mv[1] += 1.0f;
        if (Key('Q')) mv[1] -= 1.0f;
        for (int i = 0; i < 3; i++) s_pos[i] += mv[i] * speed * dt;
    }
    s_mouse = m; s_mouseWas = look;
    g_cfg.live[346][2] = s_follow ? 1.0f : 0.0f; g_cfg.live[346][3] = haveHead ? (float)age : -1.0f;
    // --- escribir la camara ---
    pos[0] = s_pos[0]; pos[1] = s_pos[1]; pos[2] = s_pos[2];
    if (s_follow) { tgt[0] = s_head[0]; tgt[1] = s_head[1]; tgt[2] = s_head[2]; }
    else { tgt[0] = s_pos[0] + f[0]; tgt[1] = s_pos[1] + f[1]; tgt[2] = s_pos[2] + f[2]; }
    up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f;
    float fovApplied = -1.0f;
    if (proj == 0) {
        const float z = std::clamp(g_cfg.live[344][2], 0.05f, 3.0f);
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
    if (cam && FreeCamActive()) {
        if (!s_on) { memcpy(s_orig, cam + 0x10, sizeof s_orig); s_haveOrig = true; s_restore = true; }
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

} // namespace fx
