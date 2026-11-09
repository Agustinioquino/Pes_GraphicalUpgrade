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
static double s_tLog = 0;
float g_solRealActivo = 0.0f;   // z393 (UploadParams)
static float s_sunSin = 0.0f;   // seno de la altura del sol en el ciclo (negativo de noche)

static void DirFrom(float azDeg, float elDeg, float out[3]) {
    const float a = azDeg * 0.01745329f, e = elDeg * 0.01745329f;
    out[0] = sinf(a) * cosf(e); out[1] = sinf(e); out[2] = cosf(a) * cosf(e);   // hacia el sol, y > 0 de dia
}

void SolLuzPresent() {
    const bool test = g_cfg.live[297][0] > 0.5f;
    const bool nearSun = g_cfg.live[378][3] > 0.5f;
    const int tl = (int)(g_cfg.live[392][3] + 0.5f);
    const bool tlOn = tl >= 1 && tl <= 4;
    // (09/11 i, Agustin: "le falta el ambiente") con la TppAtmosphere en el EditFace (soleditor: SolEditorAtmo / SolEditorSetHora) el
    // ciclo de dia y noche es el del PARTIDO: el motor mueve sol, luna, luz de cielo y exposicion con la hora. Sin atmosfera (archivo
    // viejo: SolEditorAtmo devuelve false) queda la orbita propia con zfx_SolEditor (abajo)
    static bool s_atmoOn = false, s_horaSet = false, s_atmoMissLogged = false;
    static double s_h0 = 0; static bool s_hWas = false;
    if (test && tlOn && !nearSun) {
        if (!s_hWas) s_h0 = Now();
        s_hWas = true;
        if (SolEditorAtmo(true)) {
            const float per = tl == 1 ? 30.0f : tl == 2 ? 60.0f : tl == 3 ? 120.0f : 300.0f;
            double t = (Now() - s_h0) / per; t -= floor(t);
            const float h = (float)fmod(6.0 + 24.0 * t, 24.0);   // arranca al amanecer
            SolEditorSetHora(h); s_horaSet = true;
            if (!s_atmoOn) Log("sol real del editor: ciclo de dia y noche con la atmosfera del motor (vuelta de %.0f s)", per);
            s_atmoOn = true;
            if (s_on) { const float d0[3] = { 0, 1, 0 }, c0[3] = { 1, 1, 1 }; SolEditorSet(d0, c0, 5600.0f, 0.0f, false); s_on = false; }   // un solo sol
            g_solRealActivo = 1.0f;
            static double s_tl2 = 0; if (Now() - s_tl2 > 10.0) { s_tl2 = Now(); Log("sol real del editor (10 s): hora %.2f (atmosfera)", h); }
            return;
        }
        if (!s_atmoMissLogged) { s_atmoMissLogged = true; Log("sol real del editor: sin atmosfera en el EditFace: ciclo con zfx_SolEditor"); }
    } else s_hWas = false;
    if (s_atmoOn) { SolEditorAtmo(false); s_atmoOn = false; }
    if (s_horaSet) { SolEditorHoraLibre(); s_horaSet = false; }
    const bool want = test && (nearSun || tlOn) && SolEditorBody() != nullptr;
    if (!want) {
        if (s_on) { const float d[3] = { 0, 1, 0 }, c[3] = { 1, 1, 1 }; SolEditorSet(d, c, 5600.0f, 0.0f, false); s_on = false; Log("sol real del editor: apagado"); }
        g_solRealActivo = 0.0f; return;
    }
    float az = 0, el = 0, kelvin, lux; float rgb[3] = { 1, 1, 1 }; float d[3];
    const float ev = std::clamp(g_cfg.live[381][0], -6.0f, 6.0f);
    static bool s_tlWas = false; static double s_tl0 = 0;
    if (tlOn && !s_tlWas) s_tl0 = Now();   // cada vez que se prende arranca al amanecer
    s_tlWas = tlOn;
    if (tlOn) {
        // (09/11 e, Agustin: "no es suave, ni la luz recorre como si fuera un timelapse") una orbita continua como la del cielo:
        // sale por un costado (horizonte), sube en arco hasta 65 grados al mediodia y se pone por el otro costado; de noche la
        // luna hace el mismo arco. En el horizonte la fuerza baja a casi 0, asi el cambio sol -> luna no se ve como salto.
        // Se escribe cada cuadro (SolEditorSet descarta lo igual), sin umbrales: el movimiento es continuo
        const float per = tl == 1 ? 30.0f : tl == 2 ? 60.0f : tl == 3 ? 120.0f : 300.0f;
        double t = (Now() - s_tl0) / per; t -= floor(t);
        const float a = 6.2831853f * (float)t, ca = cosf(a), sa = sinf(a);
        const float sN = 0.9063078f, cN = 0.4226183f;   // mediodia a 65 grados
        float o[3] = { ca, sa * sN, sa * cN };            // sol (y > 0 de dia)
        const bool day = sa >= 0.0f;
        s_sunSin = sa * sN;
        if (!day) { o[0] = -o[0]; o[1] = -o[1]; o[2] = -o[2]; }   // luna: el mismo arco de noche
        const float sinEl = o[1];
        const float fade = std::clamp((sinEl - 0.005f) / 0.20f, 0.0f, 1.0f), fadeS = fade * fade * (3.0f - 2.0f * fade);
        if (o[1] < 0.017f) o[1] = 0.017f;   // nunca por debajo de 1 grado (sombras largas pero estables)
        std::copy(o, o + 3, d);
        el = asinf(std::clamp(sinEl, -1.0f, 1.0f)) * 57.29578f; az = atan2f(o[0], o[2]) * 57.29578f;
        if (day) {
            const float up = std::clamp(sinEl / 0.6f, 0.0f, 1.0f);
            kelvin = 2200.0f + (5600.0f - 2200.0f) * sqrtf(up);
            lux = 1500.0f * (0.02f + 0.98f * fadeS);
        } else {
            kelvin = 4100.0f; rgb[0] = 0.996f; rgb[1] = 0.988f; rgb[2] = 0.843f;
            lux = 1.0f + 30.0f * fadeS;
        }
    } else {
        az = g_cfg.live[297][2]; el = std::clamp(g_cfg.live[297][3], 1.0f, 89.0f);
        kelvin = 5200.0f; lux = 1500.0f;
        DirFrom(az, el, d);
    }
    lux *= exp2f(ev);
    const bool ok = SolEditorSet(d, rgb, kelvin, lux, true);
    // z393: 1 = sol de partido; con el ciclo, 2 + seno de la altura del SOL (1..3: < 2 de noche) para el ambiente del shader
    if (ok) { if (!s_on) Log("sol real del editor: prendido (%s)", tlOn ? "ciclo de dia y noche" : "sol de partido"); s_on = true; g_solRealActivo = tlOn ? 2.0f + s_sunSin : 1.0f; }
    else if (!s_logged) { s_logged = true; Log("sol real del editor: SolEditorSet fallo (sin cuerpo valido)"); }
    const double now = Now();
    if (s_on && now - s_tLog > 10.0) { s_tLog = now; Log("sol real del editor (10 s): rumbo %.0f altura %.0f, %.0f K, %.0f lux (EV %+.1f)", az, el, kelvin, lux, ev); }
}

} // namespace fx
