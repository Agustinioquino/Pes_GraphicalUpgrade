// PES21 FX (31/10) - CAMARA LIBRE: zoom y movimiento con teclado y mouse en el visor del editor, las celebraciones y las
// repeticiones (pedido de Agustin, para mirar de cerca las expresiones y las celebraciones).
//
// Se apoya en el gancho de la camara del motor de camhook.cpp (CameraSystem 0x14198d3b0, que aplica la camara principal
// una vez por cuadro de render con un parametro de 0x104 bytes: +0 posicion, +0x0c objetivo, +0x18 arriba, +0x24 / +0x28
// cerca / lejos, +0x2c tipo de proyeccion (0 = perspectiva: +0x30 / +0x34 campo de vision y relacion de aspecto), +0x48
// continua). Con la camara libre prendida, antes de dejar pasar el parametro se le pisan posicion, objetivo y arriba con una
// camara propia que se mueve en tiempo real (no depende del reloj del juego: con la camara lenta puesta se mueve igual) y se
// multiplica el campo de vision por el zoom del menu.
//
//   344 (ini): x 0 apagada / 1 prendida (el juego no ve el teclado mientras tanto, keyblock.cpp) / 2 prendida y el juego
//   sigue viendo el teclado; y velocidad (m/s); z zoom (x el campo de vision del juego; menos = mas cerca); w sensibilidad
//   del mouse. 345 la escribe la DLL: xyz posicion de la camara, w campo de vision aplicado.
//   Teclas (solo con la ventana del juego al frente): W / S adelante y atras, A / D de costado, Q / E bajar y subir,
//   flechas giran, mouse con el boton derecho apretado gira, Re Pag / Av Pag (o + / -) zoom, Shift x4, Ctrl /4,
//   Inicio = volver a la camara del juego (se toma de nuevo la posicion de donde este el juego).
#include "core.h"
#include <cmath>
#include <cstring>
#include <atomic>
#include <algorithm>

namespace fx {

static bool s_on = false;
static float s_pos[3] = {}, s_yaw = 0, s_pitch = 0;
static double s_t = 0;
static POINT s_mouse{}; static bool s_mouseWas = false;
static std::atomic<int> s_applies{0};
static int s_logged = 0;

bool FreeCamActive() { return g_cfg.live[344][0] > 0.5f; }
bool FreeCamBlocksKeys() { const float v = g_cfg.live[344][0]; return v > 0.5f && v < 1.5f; }

static bool Key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static bool GameInFront() {
    DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}
static float Len3(const float* v) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

static void TakeFromGame(const unsigned char* b) {
    float pos[3], tgt[3]; memcpy(pos, b, 12); memcpy(tgt, b + 0x0c, 12);
    float d[3] = { tgt[0] - pos[0], tgt[1] - pos[1], tgt[2] - pos[2] };
    float l = Len3(d); if (l < 1e-5f) { d[0] = 0; d[1] = 0; d[2] = 1; l = 1; }
    memcpy(s_pos, pos, 12);
    s_pitch = asinf(std::clamp(d[1] / l, -1.0f, 1.0f));
    s_yaw = atan2f(d[0], d[2]);
    s_t = Now();
    GetCursorPos(&s_mouse); s_mouseWas = false;
}

void FreeCamApply(unsigned char* b) {
    if (!FreeCamActive()) {
        if (s_on) { s_on = false; Log("camara libre: apagada"); }
        return;
    }
    if (!s_on) {
        s_on = true; TakeFromGame(b);
        float up[3], nf[2], fov[2]; int proj; memcpy(up, b + 0x18, 12); memcpy(nf, b + 0x24, 8); memcpy(&proj, b + 0x2c, 4); memcpy(fov, b + 0x30, 8);
        Log("camara libre: prendida en (%.2f, %.2f, %.2f) giro %.0f / %.0f grados; arriba (%.2f, %.2f, %.2f), cerca / lejos %.3f / %.1f, proyeccion %d, +0x30 = %.4f, +0x34 = %.4f",
            s_pos[0], s_pos[1], s_pos[2], s_yaw * 57.2958f, s_pitch * 57.2958f, up[0], up[1], up[2], nf[0], nf[1], proj, fov[0], fov[1]);
        s_logged = 0;
    }
    // --- entrada (tiempo real) ---
    double t = Now(); float dt = (float)std::clamp(t - s_t, 0.0, 0.1); s_t = t;
    const bool front = GameInFront();
    const bool typing = g_menuTyping.load();
    if (front && Key(VK_HOME)) { TakeFromGame(b); Log("camara libre: Inicio, se vuelve a la camara del juego"); }
    float speed = std::clamp(g_cfg.live[344][1], 0.05f, 50.0f);
    if (Key(VK_SHIFT)) speed *= 4.0f;
    if (Key(VK_CONTROL)) speed *= 0.25f;
    const float sens = std::clamp(g_cfg.live[344][3], 0.05f, 10.0f);
    if (front && !typing) {
        const float rot = 1.2f * dt;   // flechas: ~70 grados por segundo
        if (Key(VK_LEFT)) s_yaw += rot; if (Key(VK_RIGHT)) s_yaw -= rot;
        if (Key(VK_UP)) s_pitch += rot; if (Key(VK_DOWN)) s_pitch -= rot;
        float z = g_cfg.live[344][2];
        if (Key(VK_PRIOR) || Key(VK_OEM_PLUS) || Key(VK_ADD)) z *= 1.0f - 0.8f * dt;
        if (Key(VK_NEXT) || Key(VK_OEM_MINUS) || Key(VK_SUBTRACT)) z *= 1.0f + 0.8f * dt;
        g_cfg.live[344][2] = std::clamp(z, 0.05f, 3.0f);
    }
    // mouse: gira con el boton derecho apretado (no mientras el menu del mod esta abierto: ahi el mouse es del menu)
    POINT m; GetCursorPos(&m);
    const bool look = front && !g_menuOpen.load() && Key(VK_RBUTTON);
    if (look && s_mouseWas) { s_yaw -= (m.x - s_mouse.x) * 0.0025f * sens; s_pitch -= (m.y - s_mouse.y) * 0.0025f * sens; }
    s_mouse = m; s_mouseWas = look;
    s_pitch = std::clamp(s_pitch, -1.55f, 1.55f);
    const float cp = cosf(s_pitch), sp = sinf(s_pitch);
    float f[3] = { cp * sinf(s_yaw), sp, cp * cosf(s_yaw) };
    float r[3] = { f[2], 0.0f, -f[0] }; float rl = Len3(r); if (rl > 1e-5f) { r[0] /= rl; r[2] /= rl; } else { r[0] = 1; r[2] = 0; }
    if (front && !typing) {
        float mv[3] = {};
        if (Key('W')) { mv[0] += f[0]; mv[1] += f[1]; mv[2] += f[2]; }
        if (Key('S')) { mv[0] -= f[0]; mv[1] -= f[1]; mv[2] -= f[2]; }
        if (Key('D')) { mv[0] += r[0]; mv[2] += r[2]; }
        if (Key('A')) { mv[0] -= r[0]; mv[2] -= r[2]; }
        if (Key('E')) mv[1] += 1.0f;
        if (Key('Q')) mv[1] -= 1.0f;
        for (int i = 0; i < 3; i++) s_pos[i] += mv[i] * speed * dt;
    }
    // --- escribir la camara en el parametro del motor ---
    float tgt[3] = { s_pos[0] + f[0], s_pos[1] + f[1], s_pos[2] + f[2] };
    const float up[3] = { 0.0f, 1.0f, 0.0f };
    memcpy(b, s_pos, 12); memcpy(b + 0x0c, tgt, 12); memcpy(b + 0x18, up, 12);
    b[0x48] = 1;   // continua: sin reinicio de la historia temporal (TAA del juego, SSGI, GTAO) en cada cuadro
    int proj; memcpy(&proj, b + 0x2c, 4);
    float fovApplied = -1.0f;
    if (proj == 0) {
        float fov; memcpy(&fov, b + 0x30, 4);
        const float z = std::clamp(g_cfg.live[344][2], 0.05f, 3.0f);
        if (fov == fov && fov > 0.0f && fov < 10.0f) { fov *= z; memcpy(b + 0x30, &fov, 4); fovApplied = fov; }
    }
    float* q = g_cfg.live[345]; q[0] = s_pos[0]; q[1] = s_pos[1]; q[2] = s_pos[2]; q[3] = fovApplied;
    if (++s_applies % 600 == 0 && s_logged < 20) {   // cada ~10 s de uso
        Log("camara libre: (%.2f, %.2f, %.2f) giro %.0f / %.0f, zoom x%.2f (campo de vision %.4f), %s", s_pos[0], s_pos[1], s_pos[2],
            s_yaw * 57.2958f, s_pitch * 57.2958f, g_cfg.live[344][2], fovApplied, front ? "ventana al frente" : "ventana atras: sin teclas");
        s_logged++;
    }
}

} // namespace fx
