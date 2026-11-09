// PES21 FX (09/11, Agustin: "el timelapse tiene que funcionar con un sol real luz de noche real, y el sol cercano tiene que ser
// un sol real del juego asi como pasa a la hora de jugar") - EL SOL REAL DEL MOTOR EN EL EDITOR.
// El EditFace.fpkd de LucesEditor trae una fox::grx::DirectionalLight "zfx_SolEditor" apagada (tools/fox/editface_sol.py
// --sin-luz). El motor la dibuja con la misma pasada del sol y las mismas cascadas de sombra que en el partido (PluginSunlight /
// PluginShadow no miran el modo). soleditor.cpp (hilo de descompilacion) la encuentra y la escribe; aca se decide QUE sol:
//  * w378 = 1 "Sol de partido": rumbo z297 / altura w297, 5200 K, fuerte (x381 = EV).
//  * w392 = 1..4 "Ciclo de dia y noche": la misma cuenta que hacia el shader (amanece a la derecha, mediodia alto, atardece a la
//    izquierda) pero con el sol y la luna del motor: sol 2300 K en el horizonte -> 5600 K arriba; luna fria y debil de noche.
// Mientras el sol real esta prendido la DLL escribe z393 = 1 y la luz de prueba del shader (pes_ssss pasada 1) deja de imitarlo.
// Solo con la luz de prueba x297 prendida; apagada o fuera del editor, el sol queda apagado como lo trae el archivo.
#include "core.h"
#include <cmath>
#include <algorithm>

namespace fx {

static bool s_on = false, s_logged = false;
static float s_last[6] = { -999, -999, -999, -999, -999, -999 };
static double s_tLog = 0;
float g_solRealActivo = 0.0f;   // z393 (UploadParams)

static void DirFrom(float azDeg, float elDeg, float out[3]) {
    const float a = azDeg * 0.01745329f, e = elDeg * 0.01745329f;
    out[0] = sinf(a) * cosf(e); out[1] = sinf(e); out[2] = cosf(a) * cosf(e);   // hacia el sol, y > 0 de dia
}

void SolLuzPresent() {
    const bool test = g_cfg.live[297][0] > 0.5f;
    const bool nearSun = g_cfg.live[378][3] > 0.5f;
    const int tl = (int)(g_cfg.live[392][3] + 0.5f);
    const bool tlOn = tl >= 1 && tl <= 4;
    const bool want = test && (nearSun || tlOn) && SolEditorBody() != nullptr;
    if (!want) {
        if (s_on) { const float d[3] = { 0, 1, 0 }, c[3] = { 1, 1, 1 }; SolEditorSet(d, c, 5600.0f, 0.0f, false); s_on = false; Log("sol real del editor: apagado"); }
        g_solRealActivo = 0.0f; s_last[0] = -999; return;
    }
    float az, el, kelvin, lux; float rgb[3] = { 1, 1, 1 };
    const float ev = std::clamp(g_cfg.live[381][0], -6.0f, 6.0f);
    if (tlOn) {
        const float per = tl == 1 ? 30.0f : tl == 2 ? 60.0f : tl == 3 ? 120.0f : 300.0f;
        double t = fmod(Now(), 3600.0) / per; t -= floor(t);
        const float sD = sinf(6.2831853f * (float)t);
        const float tt = (float)t;
        if (sD > 0.0f) {   // dia: el sol
            el = std::max(75.0f * sD, 1.5f); az = 100.0f - 200.0f * std::clamp(tt * 2.0f, 0.0f, 1.0f);
            const float up = std::clamp(sD / 0.6f, 0.0f, 1.0f);
            kelvin = 2300.0f + (5600.0f - 2300.0f) * up;
            lux = 2500.0f * (0.15f + 0.85f * std::min(sD / 0.35f, 1.0f));
        } else {           // noche: la luna
            el = std::max(45.0f * -sD, 1.5f); az = 100.0f - 200.0f * std::clamp(tt * 2.0f - 1.0f, 0.0f, 1.0f);
            kelvin = 4100.0f; rgb[0] = 0.996f; rgb[1] = 0.988f; rgb[2] = 0.843f;
            lux = 40.0f * std::min(-sD / 0.25f, 1.0f) + 2.0f;
        }
    } else {
        az = g_cfg.live[297][2]; el = std::clamp(g_cfg.live[297][3], 1.0f, 89.0f);
        kelvin = 5200.0f; lux = 2500.0f;
    }
    lux *= exp2f(ev);
    const float cur[6] = { az, el, kelvin, lux, rgb[2], 1 };
    bool changed = !s_on;
    for (int i = 0; i < 6 && !changed; ++i) changed = fabsf(cur[i] - s_last[i]) > (i == 3 ? 0.002f * lux + 0.05f : 0.05f);
    if (changed) {
        float d[3]; DirFrom(az, el, d);
        const bool ok = SolEditorSet(d, rgb, kelvin, lux, true);
        if (ok) { std::copy(cur, cur + 6, s_last); if (!s_on) Log("sol real del editor: prendido (%s)", tlOn ? "ciclo de dia y noche" : "sol de partido"); s_on = true; }
        else if (!s_logged) { s_logged = true; Log("sol real del editor: SolEditorSet fallo (sin cuerpo valido)"); }
        if (ok) g_solRealActivo = 1.0f;
    }
    const double now = Now();
    if (s_on && now - s_tLog > 10.0) { s_tLog = now; Log("sol real del editor (10 s): rumbo %.0f altura %.0f, %.0f K, %.0f lux (EV %+.1f)", az, el, kelvin, lux, ev); }
}

} // namespace fx
