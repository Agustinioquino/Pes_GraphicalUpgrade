// PES21 FX - version publica limitada (compilada con PES21FX_PUBLIC): funciones reservadas para versiones futuras.
// Cada limite fija un parametro (el menu no lo muestra y el ini no lo puede prender) o le pone un tope.
// Se aplican al cargar la config (base, live, menu) y en cada subida de parametros (live), asi valen tambien para la DLL.
#include "core.h"
#include <algorithm>
#include <cstring>

namespace fx {

#ifdef PES21FX_PUBLIC
namespace {
struct Limit { int idx; int comp; bool cap; float value; };   // comp -1 = las cuatro; cap = tope maximo, si no valor fijo
const Limit kLimits[] = {
    // Rebote de luz SSGI: no va en esta version
    { 153, 0, false, 0 }, { 315, 0, false, 0 },
    // Luz volumetrica (rayos de sol en el aire)
    { 335, 0, false, 0 },
    // Cara: teselacion de la cara y del cuerpo, por jugador
    { 357, 0, false, 0 }, { 364, 0, false, 0 }, { 371, 0, false, 0 },
    // Expresiones: gestos y animaciones, gestos por huesos, clips .gani
    { 355, -1, false, 0 }, { 356, 0, false, 0 }, { 356, 1, false, 0 }, { 356, 3, false, 0 }, { 361, -1, false, 0 }, { 362, -1, false, 0 },
    { 383, 0, false, 0 }, { 383, 3, false, 0 },
    // Expresiones mas marcadas: limitadas
    { 340, 1, true, 1.35f }, { 340, 2, true, 1.2f }, { 340, 3, true, 0.35f },
    { 341, -1, true, 1.0f }, { 348, -1, true, 1.0f }, { 347, 3, true, 1.0f },
    { 350, 0, true, 1.0f },     // abrir la boca: sin "Por huesos + parpados (prueba)"
    // Pestanas con hebras
    { 368, 0, false, 0 },
    // Poros v6: limitados
    { 94, 0, true, 0.7f },
    // Camara libre, seguir la cara, camara lenta
    { 344, 0, false, 0 }, { 346, 0, false, 0 }, { 343, 1, false, 0 },
    // Torso original en la fisica
    { 349, 3, false, 0 },
    // Luz de prueba del editor: sin el banco de pruebas (foco, bruma, reflector, segunda luz, vistas, EV)
    { 377, 0, false, 0 }, { 377, 3, false, 0 }, { 378, 1, false, 0 }, { 379, 3, false, 0 },
    { 380, 0, false, 0 }, { 380, 3, false, 0 }, { 381, 0, false, 0 }, { 381, 1, false, 0 },
    // Ropa con fisica (cloth-mod): fuera, como en los paquetes anteriores
    { 70, 3, false, 0 }, { 78, 0, false, 0 }, { 79, 0, false, 0 }, { 80, 1, false, 0 }, { 255, 2, false, 0 }, { 256, 0, false, 0 },
    { 352, 3, false, 0 }, { 353, 3, false, 0 },
};
// grupos del menu que no van (id = nombre de la seccion sin "Menu")
const char* const kHiddenGroups[] = {
    "GITemporal", "Volumetric", "FaceTess", "ExprShow", "ExprGani", "CamaraLibre", "CamaraLenta", "TorsoOriginal",
    "PerfMeter", "DebugView", "DiagHooks",
};

bool Fixed(int idx, int comp) {
    for (auto& l : kLimits) if (!l.cap && l.idx == idx && (l.comp < 0 || l.comp == comp)) return true;
    return false;
}
void ApplyTo(float (*p)[4]) {
    for (auto& l : kLimits) {
        if (l.idx < 0 || l.idx >= kParamCount) continue;
        for (int c = 0; c < 4; c++) {
            if (l.comp >= 0 && l.comp != c) continue;
            float& v = p[l.idx][c];
            v = l.cap ? (std::min)(v, l.value) : l.value;
        }
    }
}
}  // namespace

// con g_cfgMutex tomado (o sobre una Config que todavia no es la global)
void PublicLimitConfig(Config& c) {
    ApplyTo(c.base); ApplyTo(c.live);
    // diagnosticos y herramientas de desarrollo: apagados siempre
    c.autoCap = false; c.exeDump = false; c.memWatch = false; c.optDump = false; c.lightTrace = false;
    c.meterOn = false; c.sunWatch = false; c.grassDumpDebugP = false;
    // teclas que tocan algo reservado (camara libre, camara lenta...): fuera, para que ni un cuadro lo prendan
    c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(), [](const KeyBind& k) {
        for (auto& a : k.assigns) if (Fixed(a.idx, a.comp)) return true;
        return false;
    }), c.keys.end());
    auto& gs = c.groups;
    gs.erase(std::remove_if(gs.begin(), gs.end(), [](const MenuGroup& g) {
        if (g.section.rfind("Ropa", 0) == 0) return true;
        if (g.section.rfind("Diagn", 0) == 0 && g.id != "ShadowPatch") return true;   // de Diagnostico solo queda el parche de sombras
        for (const char* h : kHiddenGroups) if (g.id == h) return true;
        return g.tIdx >= 0 && Fixed(g.tIdx, g.tComp);
    }), gs.end());
    for (auto& g : gs) {
        auto& sl = g.sliders;
        sl.erase(std::remove_if(sl.begin(), sl.end(), [](const MenuSlider& s) { return s.ext.empty() && Fixed(s.idx, s.comp); }), sl.end());
        for (auto& s : sl) {
            if (!s.ext.empty()) continue;
            for (auto& l : kLimits) if (l.cap && l.idx == s.idx && (l.comp < 0 || l.comp == s.comp)) {
                if (!s.options.empty() && s.optVals.empty()) { int n = (int)l.value + 1; if ((int)s.options.size() > n) s.options.resize(n); if ((int)s.optionsEn.size() > n) s.optionsEn.resize(n); }
                else s.hi = (std::min)(s.hi, l.value);
            }
        }
    }
}
void PublicClampLive(float (*live)[4]) { ApplyTo(live); }

const char* PublicNotice() {
    switch (g_cfg.lang) {
    case 1: return "Limited public version: it includes some features from the newest builds, but not all of them. "
                   "The rest will be released little by little. A Patreon is planned to support the project.";
    case 2: return "Versao publica limitada: inclui algumas funcoes das versoes mais novas, mas nao todas. "
                   "As demais serao liberadas aos poucos. Esta previsto um Patreon para apoiar o projeto.";
    default: return "Version publica limitada: trae algunas funciones de las versiones mas nuevas, pero no todas. "
                    "Las demas se iran liberando poco a poco. Tengo pensado abrir un Patreon para apoyar el proyecto.";
    }
}
#else
void PublicLimitConfig(Config&) {}
void PublicClampLive(float (*)[4]) {}
const char* PublicNotice() { return nullptr; }
#endif

}  // namespace fx
