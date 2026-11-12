// PES21 FX - "Sol libre (experimentar)" v2 (12/11, hilo hefeaf). Ingenieria inversa de PES2021.exe 1.07.02 (sello 62787C6A).
//
// Hasta ahora el sol libre (x101..w101, x102) solo giraba la luz dentro de los shaders de la pasada de luz: las sombras seguian
// al sol real del partido. Con x450 = 1 la DLL mueve el sol DEL MOTOR, asi que el shadow map, la luz y todo lo que lee la
// direccion del LightCB (pasto, fila 111) lo siguen:
//   - PluginShadow::vf7 0x1416c66e0 elige la luz direccional de la sombra (plugin+0x60, o +0x68 si no hay; FUN_141721d70) y
//     PluginShadow::vf8 0x1416c2ad0 arma la camara de sombras con FUN_14169f040(luz) = *(*(luz+0x38)+0x70) (direccion, vec4).
//   - PluginSunlight::vf8 0x1416ee820 (bucle de luces) baja esa misma direccion al cbuffer (DirLight_ToView 0x14171e220).
//   - La atmosfera la escribe con FUN_14169f3c0(luz, dir) (cuenta +0x68 y avisa con vfunc +0x78). Aca se usa la misma funcion.
// Los ganchos son los de soleditor.cpp (hk_shadow despues del original, hk_sun antes), asi que con motor = 0 no hace nada.
// Lo que habia en la luz antes de escribir se guarda como "sol real": si la atmosfera o el Daylight Engine lo cambian, se toma
// de nuevo; al apagar se devuelve. El signo se copia del sol real (la fuente guarda la direccion de viaje: y < 0 de dia).
//
// Filas: 450 = x sombras que siguen al sol libre, y azimut desde el sol del partido, z temperatura (K, 0 = no), w giro (grados/s)
//        451 = x "copiar el sol del partido" (se vuelve a 0 sola)
//        452 = (la escribe la DLL, no va al ini) xyz direccion final en el mundo (hacia el sol), w 1 = la usa el shader, 2 = ya esta
//              en el motor (el shader no gira nada y deja las sombras reales encendidas)
//        453 = (la escribe la DLL) xyz tinte de la temperatura (luminancia 1), w 1 = usarlo
#include "core.h"
#include <atomic>
#include <mutex>
#include <cmath>
#include <algorithm>

namespace fx {

typedef void (__fastcall* FnSetDir)(void*, const float*);
static const uintptr_t kSetDir = 0x14169f3c0ull;

static std::mutex s_mx;
static bool s_wantOn = false; static float s_want[3] = { 0, 1, 0 };   // mundo, hacia el sol
static void* s_light = nullptr; static double s_lightT = -1.0;          // ultima luz de la sombra (hk_shadow)
static void* s_realLight = nullptr; static float s_real[4] = {}; static bool s_haveReal = false;
static float s_wrote[4] = {}; static bool s_active = false;
static double s_appliedT = -1.0;
static int s_writes = 0, s_reals = 0, s_fails = 0;

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}

static float* DirOf(void* light) {   // sin objetos C++
    __try {
        if (!Readable((char*)light + 0x38, 8)) return nullptr;
        char* data = *(char* volatile*)((char*)light + 0x38);
        if (!Readable(data + 0x70, 16)) return nullptr;
        return (float*)(data + 0x70);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
static bool CallSet(void* light, const float* d) {   // sin objetos C++
    __try { ((FnSetDir)kSetDir)(light, d); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void Apply(void* light) {
    if (!light) return;
    float* d = DirOf(light); if (!d) return;
    float cur[4] = { d[0], d[1], d[2], d[3] };
    float n = sqrtf(cur[0] * cur[0] + cur[1] * cur[1] + cur[2] * cur[2]);
    if (!(n > 0.5f && n < 1.5f)) return;   // no parece una direccion: no se toca
    std::lock_guard<std::mutex> lk(s_mx);
    bool ours = s_active && light == s_realLight && fabsf(cur[0] - s_wrote[0]) + fabsf(cur[1] - s_wrote[1]) + fabsf(cur[2] - s_wrote[2]) < 1e-4f;
    if (!ours) { for (int k = 0; k < 4; k++) s_real[k] = cur[k]; s_haveReal = true; s_realLight = light; if (s_active) s_reals++; }
    if (s_wantOn) {
        float sg = s_real[1] < -0.05f ? -1.0f : s_real[1] > 0.05f ? 1.0f : (g_cfg.solEditorDirSigno < 0 ? -1.0f : 1.0f);
        float t[4] = { s_want[0] * sg, s_want[1] * sg, s_want[2] * sg, s_real[3] };
        if (fabsf(cur[0] - t[0]) + fabsf(cur[1] - t[1]) + fabsf(cur[2] - t[2]) > 1e-5f) {
            if (CallSet(light, t)) s_writes++; else { s_fails++; return; }
        }
        for (int k = 0; k < 4; k++) s_wrote[k] = t[k];
        s_active = true; s_appliedT = Now();
    } else if (s_active) {
        if (ours) CallSet(light, s_real);   // se devuelve el sol real
        s_active = false;
    }
}

static void* LightOf(void* plugin) {   // sin objetos C++
    __try { void* l = *(void* volatile*)((char*)plugin + 0x60); if (!l) l = *(void* volatile*)((char*)plugin + 0x68); return l; } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
void SolLibreShadow(void* plugin) {   // PluginShadow::vf7, despues del original (ya eligio la luz)
    void* l = LightOf(plugin);
    { std::lock_guard<std::mutex> lk(s_mx); s_light = l; s_lightT = Now(); }
    Apply(l);
}
void SolLibreSun() {   // PluginSunlight::vf8, antes del original (antes de bajar la luz al cbuffer)
    void* l = nullptr;
    { std::lock_guard<std::mutex> lk(s_mx); if (Now() - s_lightT < 0.1) l = s_light; }
    Apply(l);
}

// temperatura -> RGB (aproximacion de Tanner Helland), relativo a 6500 K y con luminancia 1
static void KelvinRgb(float K, float out[3]) {
    auto f = [](float K, float c[3]) {
        float t = K / 100.0f;
        c[0] = t <= 66 ? 255.f : 329.698727f * powf(t - 60, -0.1332047592f);
        c[1] = t <= 66 ? 99.4708025861f * logf(t) - 161.1195681661f : 288.1221695283f * powf(t - 60, -0.0755148492f);
        c[2] = t >= 66 ? 255.f : (t <= 19 ? 0.f : 138.5177312231f * logf(t - 10) - 305.0447927307f);
        for (int i = 0; i < 3; i++) c[i] = std::clamp(c[i], 1.0f, 255.0f) / 255.0f;
    };
    float a[3], b[3]; f(std::clamp(K, 1000.0f, 15000.0f), a); f(6500.0f, b);
    for (int i = 0; i < 3; i++) out[i] = a[i] / b[i];
    float L = 0.2126f * out[0] + 0.7152f * out[1] + 0.0722f * out[2];
    for (int i = 0; i < 3; i++) out[i] /= (std::max)(L, 1e-3f);
}

// UpdateTimeParams (con g_cfgMutex tomado), cada cuadro
void SolLibreFrame(float (*p)[4], double t) {
    static double lastT = -1.0, logT = 0.0; static float spin = 0.0f;
    double dt = lastT < 0 ? 0.0 : std::clamp(t - lastT, 0.0, 0.25); lastT = t;
    bool on = p[101][0] > 0.5f, eng = on && p[450][0] > 0.5f, rel = p[450][1] > 0.5f;
    // sol real en el mundo (hacia el sol): con el motor movido, lo que la DLL guardo antes de escribir; si no, la fila 111
    float r[3] = {}; bool rOk = false; double appliedT; bool active;
    { std::lock_guard<std::mutex> lk(s_mx); appliedT = s_appliedT; active = s_active;
      if (active && s_haveReal) { float sg = s_real[1] < 0 ? -1.0f : 1.0f; for (int k = 0; k < 3; k++) r[k] = s_real[k] * sg; rOk = true; } }
    if (!rOk && !active) { r[0] = p[111][0]; r[1] = p[111][1]; r[2] = p[111][2]; rOk = r[0] * r[0] + r[1] * r[1] + r[2] * r[2] > 0.25f; }
    float azR = 0, elR = 0;
    if (rOk) { float n = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]); azR = atan2f(r[0], r[2]) * 57.29578f; elR = asinf(std::clamp(r[1] / n, -1.0f, 1.0f)) * 57.29578f; if (azR < 0) azR += 360.0f; }
    if (p[451][0] > 0.5f) {   // copiar el sol del partido a los sliders
        p[451][0] = 0.0f;
        if (rOk) { p[101][1] = rel ? 0.0f : azR; p[101][2] = std::clamp(elR, -5.0f, 90.0f); spin = 0.0f; }
        Log("sol libre: copiar el sol del partido %s (azimut %.0f, elevacion %.0f)", rOk ? "ok" : "sin dato", azR, elR);
    }
    if (on && fabsf(p[450][3]) > 1e-4f) { spin = fmodf(spin + p[450][3] * (float)dt, 360.0f); }
    float az = (p[101][1] + spin + (rel && rOk ? azR : 0.0f)) * 0.01745329f, el = std::clamp(p[101][2], -10.0f, 90.0f) * 0.01745329f;
    float w[3] = { cosf(el) * sinf(az), sinf(el), cosf(el) * cosf(az) };
    { std::lock_guard<std::mutex> lk(s_mx); s_wantOn = eng; for (int k = 0; k < 3; k++) s_want[k] = w[k]; }
    bool inEng = eng && t - appliedT < 0.25;
    p[452][0] = w[0]; p[452][1] = w[1]; p[452][2] = w[2]; p[452][3] = !on ? 0.0f : inEng ? 2.0f : 1.0f;
    float K = p[450][2];
    if (on && K >= 1000.0f) { float c[3]; KelvinRgb(K, c); p[453][0] = c[0]; p[453][1] = c[1]; p[453][2] = c[2]; p[453][3] = 1.0f; }
    else { p[453][0] = p[453][1] = p[453][2] = 1.0f; p[453][3] = 0.0f; }
    if (on && t - logT >= 30.0) { logT = t; int wr, re, fa; { std::lock_guard<std::mutex> lk(s_mx); wr = s_writes; re = s_reals; fa = s_fails; }
        Log("sol libre: azimut %.0f elevacion %.0f | sombras %s (%s) | sol real %s az %.0f el %.0f | escrituras %d, la atmosfera lo piso %d, fallos %d",
            az * 57.29578f, el * 57.29578f, eng ? "siguen" : "del sol real", eng ? (inEng ? "motor movido" : "SIN luz del motor: solo shader") : "-",
            rOk ? "ok" : "?", azR, elR, wr, re, fa); }
}

}  // namespace fx
