// PES21 FX (25/10) - manos de TorsoBody: skinning por cuaterniones duales y formas correctivas por pose (puno, dedos que se
// apoyan entre si, nudillos, venas que se marcan al cerrar). Todo el calculo esta en el vertex shader (shaders\pes_hands.hlsli,
// incluido en el VS de la piel 1c02a92a y en el de sombras c6ef0c75); la DLL solo reconoce el dibujo de cada mano por su
// cantidad de indices (la malla arm_mat de hand_l / hand_r de TorsoBody: x302 / y302) y le enlaza en t126 los datos por vertice
// de esa mano (textures\pes_hand_l.dds / pes_hand_r.dds, generados con la malla). Cualquier otro dibujo de esos VS no recibe
// t126 y sigue con el skinning del juego. x300 = 0 apaga todo (la mano vuelve exactamente a la del juego).
// (26/10) Filas 300..302 de t120 (antes 200..202: la tabla de tonos de piel por jugador de skintone.cpp usa 194..254 y las
// borraba en cada cuadro, asi que la tarjeta no se podia prender y la DLL nunca reconocia la mano).
#include "core.h"
#include <atomic>
#include <cmath>

namespace fx {

static std::atomic<int> s_handLogs{0};

ID3D11ShaderResourceView* HandDataFor(uint64_t vsHash, UINT indexCount, UINT start, INT base) {
    if (vsHash != 0x1c02a92ab4ae3362ULL && vsHash != 0xc6ef0c7519ec95a5ULL) return nullptr;
    UINT left = (UINT)(g_cfg.live[302][0] + 0.5f), right = (UINT)(g_cfg.live[302][1] + 0.5f);
    if (g_cfg.live[300][0] < 0.5f) {   // apagadas: avisar una vez si llega una mano (asi el log dice por que no se deforma)
        static std::atomic<bool> s_offNoted{false};
        if ((indexCount == left || indexCount == right) && !s_offNoted.exchange(true)) Log("manos: llega la mano (%u indices) pero la tarjeta esta apagada (x300 = 0)", indexCount);
        return nullptr;
    }
    const char* f = nullptr;
    if (left && indexCount == left) f = "file:textures\\pes_hand_l.dds";
    else if (right && indexCount == right) f = "file:textures\\pes_hand_r.dds";
    if (!f) return nullptr;
    ID3D11ShaderResourceView* v = FileTextureAny(f);
    if (s_handLogs.load(std::memory_order_relaxed) < 12) {
        s_handLogs++;
        Log("manos: dibujo de la mano %s (VS %016llx, %u indices, inicio %u, base %d): datos %s", indexCount == left ? "izquierda" : "derecha",
            (unsigned long long)vsHash, indexCount, start, base, v ? "enlazados en t126" : "NO CARGADOS");
    }
    return v;
}

// (26/10, v12.7) musculos con textura: los brazos y piernas de TorsoBody v12.6 (cada malla de piel con su cantidad de indices)
// reciben en t126 del VS 1c02a92a sus datos (textures\pes_limb_*.dds: huesos del codo / la rodilla, ejes de reposo y por vertice
// cuanto es biceps, antebrazo, deltoides, isquios, pantorrilla, cuadriceps y venas). El VS mide la pose como musculo.cpp
// (codo y rodilla doblados, brazo levantado) y le pasa al PS de la piel cuanto marcar el relieve muscular y las venas; los
// brazos y las manos reciben ademas la textura de venas (textures\pes_veins_arm.dds) en t29 del PS. x305 = 0 apaga todo.
struct LimbEntry { UINT idx; const char* file; bool arm; };
static const LimbEntry kLimbs[] = {
    { 37458, "file:textures\\pes_limb_arm.dds", true },            { 37461, "file:textures\\pes_limb_arm_cap.dds", true },
    { 34272, "file:textures\\pes_limb_arm_glove.dds", true },      { 34278, "file:textures\\pes_limb_arm_glove_cap.dds", true },
    { 50340, "file:textures\\pes_limb_thigh_short.dds", false },   { 33102, "file:textures\\pes_limb_thigh_middle.dds", false },
    { 21690, "file:textures\\pes_limb_thigh_long.dds", false },    { 51876, "file:textures\\pes_limb_thigh_short_classic.dds", false },
    { 24525, "file:textures\\pes_limb_thigh_middle_classic.dds", false }, { 15996, "file:textures\\pes_limb_thigh_long_classic.dds", false },
};
static std::atomic<int> s_limbLogs{0};

ID3D11ShaderResourceView* LimbDataFor(uint64_t vsHash, UINT indexCount, UINT start, INT base, bool* isArm) {
    if (isArm) *isArm = false;
    if (vsHash != 0x1c02a92ab4ae3362ULL || g_cfg.live[305][0] < 0.5f) return nullptr;
    for (const LimbEntry& e : kLimbs) {
        if (e.idx != indexCount) continue;
        ID3D11ShaderResourceView* v = FileTextureAny(e.file);
        if (isArm) *isArm = e.arm;
        if (s_limbLogs.load(std::memory_order_relaxed) < 16) {
            s_limbLogs++;
            Log("musculos con textura: dibujo %s (%u indices, inicio %u, base %d): datos %s", e.file + 14, indexCount, start, base, v ? "enlazados en t126" : "NO CARGADOS");
        }
        return v;
    }
    return nullptr;
}

ID3D11ShaderResourceView* VeinTexture() {
    if (g_cfg.live[305][0] < 0.5f) return nullptr;
    static std::atomic<bool> s_noted{false};
    ID3D11ShaderResourceView* v = FileTextureAny("file:textures\\pes_veins_arm.dds");
    if (!s_noted.exchange(true)) Log("musculos con textura: textura de venas %s", v ? "cargada (t29 del PS)" : "NO CARGADA (falta textures\\pes_veins_arm.dds)");
    return v;
}

// (26/10) variantes robusto / atletico de TorsoBody (pes_variants.hlsli): las mallas del torso (pants_*_sub 1..3) y de los brazos
// (arm, arm_cap, arm_glove, arm_glove_cap y sus capas de suciedad) se reconocen por su cantidad de indices y reciben en t127 del VS
// de la piel (1c02a92a) y de las sombras (c6ef0c75) sus datos por vertice (textures\pes_var_<malla>.dds, tools/fox/torso/
// make_variants.py). El shader solo los usa si la posicion de reposo del vertice coincide (si TorsoBody cambia, no deforma nada).
// Las del torso ademas llevan en el PS los mapas normales delta (t16 robusto, t17 atletico) y las estrias (t18).
// Solo se enlazan si alguna variante esta en uso (fila 264: general distinta de 0 o la tabla de jugadores) y x285 prendido.
struct VarEntry { UINT idx; const char* file; bool torso; };
static const VarEntry kVars[] = {
    { 26634, "file:textures\\pes_var_torso_1.dds", true }, { 27201, "file:textures\\pes_var_torso_2.dds", true },
    { 34683, "file:textures\\pes_var_torso_3.dds", true },
    { 37458, "file:textures\\pes_var_arm.dds", false },       { 37461, "file:textures\\pes_var_arm_cap.dds", false },
    { 34272, "file:textures\\pes_var_arm_glove.dds", false }, { 34278, "file:textures\\pes_var_arm_glove_cap.dds", false },
    { 36366, "file:textures\\pes_var_arm_dirt.dds", false },  { 34275, "file:textures\\pes_var_arm_glove_dirt.dds", false },
    { 32898, "file:textures\\pes_var_arm_glove_cap_dirt.dds", false },
};
static std::atomic<int> s_varLogs{0};
static bool VariantsInUse() {
    if (g_cfg.live[285][0] < 0.5f || g_cfg.live[264][2] < 0.5f) return false;
    if (fabsf(g_cfg.live[264][0]) > 0.001f) return true;
    const int n = (int)(g_cfg.live[194][0] + 0.5f);
    if (g_cfg.live[264][1] > 0.5f) for (int i = 0; i < n && i < 20; i++) if (fabsf(g_cfg.live[265 + i][0]) > 0.001f) return true;
    return false;
}
ID3D11ShaderResourceView* VariantDataFor(uint64_t vsHash, UINT indexCount, bool* torso) {
    if (torso) *torso = false;
    if (vsHash != 0x1c02a92ab4ae3362ULL && vsHash != 0xc6ef0c7519ec95a5ULL) return nullptr;
    const VarEntry* e = nullptr;
    for (const VarEntry& k : kVars) if (k.idx == indexCount) { e = &k; break; }
    if (!e) return nullptr;
    const bool baseOn = g_cfg.live[289][0] > 0.5f;   // (26/10 b) base esculpida: los datos van siempre (no solo con variantes)
    if (!baseOn && !VariantsInUse()) return nullptr;
    ID3D11ShaderResourceView* v = FileTextureAny(e->file);
    if (torso) *torso = e->torso;
    if (s_varLogs.load(std::memory_order_relaxed) < 16) {
        s_varLogs++;
        Log("variantes del torso: dibujo %s (VS %016llx, %u indices): datos %s", e->file + 14, (unsigned long long)vsHash, indexCount, v ? "enlazados en t127" : "NO CARGADOS");
    }
    return v;
}
// (26/10 b) base esculpida (x289): la textura que corrige el normal map de TorsoBody (espalda suave; el volumen que ya muestra la
// malla sale de la textura) en t20 del PS de la piel, en todos los torsos
// (26/10 c) pelvis "sin short" (SinCamiseta, tecla 4): la malla 0 de pants_*_sub.fmdl pasa a ser piel (material del torso) con
// 3732 indices (pants_out_sub / inner / tight) o 3774 (pants_in_sub). Con el VS de la piel solo puede ser la pelvis (el short usa
// otro shader). Sus texturas propias cubren el cuadrante libre de abajo a la derecha de la del torso (el PS lo reescala a 0..1):
// detalle del normal map (t20), piel por zona (t21 / t24) y albedo (t23: su presencia le dice al PS que es la pelvis).
static bool PelvisDraw(uint64_t vsHash, UINT indexCount) {
    return vsHash == 0x1c02a92ab4ae3362ULL && (indexCount == 3732 || indexCount == 3774);
}
ID3D11ShaderResourceView* PelvisAlbedoFor(uint64_t vsHash, UINT indexCount) {
    if (!PelvisDraw(vsHash, indexCount)) return nullptr;
    ID3D11ShaderResourceView* v = FileTextureAny("file:textures\\pes_var_pelvis_alb.dds");
    static std::atomic<bool> s_noted{false};
    if (!s_noted.exchange(true)) Log("pelvis sin short: dibujo de la pelvis (%u indices), texturas %s", indexCount, v ? "cargadas (t20 / t21 / t23 / t24)" : "NO CARGADAS (faltan las pes_var_pelvis_*.dds)");
    return v;
}
ID3D11ShaderResourceView* BaseTextureFor(uint64_t vsHash, UINT indexCount) {
    if (PelvisDraw(vsHash, indexCount)) return FileTextureAny("file:textures\\pes_var_pelvis_n.dds");
    if (vsHash != 0x1c02a92ab4ae3362ULL || g_cfg.live[289][0] <= 0.5f) return nullptr;
    const VarEntry* e = nullptr;
    for (const VarEntry& k : kVars) if (k.idx == indexCount) { e = &k; break; }
    if (!e) return nullptr;
    // torso: la del torso (v 0..0.5); brazos (y sus capas): la del brazo (todo su atlas)
    ID3D11ShaderResourceView* v = FileTextureAny(e->torso ? "file:textures\\pes_var_torso_base_n.dds" : "file:textures\\pes_var_arm_base_n.dds");
    static std::atomic<int> s_noted{0};
    const int bit = e->torso ? 1 : 2;
    if (!(s_noted.fetch_or(bit) & bit)) Log("base esculpida: textura del %s %s", e->torso ? "torso" : "brazo", v ? "cargada (t20 del PS de la piel)" : "NO CARGADA");
    return v;
}
// (26/10 b) piel real (x293): brillo / rugosidad (t21) y translucidez / rubor (t24) por zona del torso. (26/10 c) y de los brazos
// (pes_var_arm_skin_*: todo el atlas del brazo, cuadradas; el PS elige las uv por el tamano)
bool SkinTexturesFor(uint64_t vsHash, UINT indexCount, ID3D11ShaderResourceView* out[2]) {
    out[0] = out[1] = nullptr;
    if (vsHash != 0x1c02a92ab4ae3362ULL || g_cfg.live[293][0] <= 0.5f) return false;
    if (PelvisDraw(vsHash, indexCount)) {   // (26/10 c) la pelvis sin short
        out[0] = FileTextureAny("file:textures\\pes_var_pelvis_skin_a.dds");
        out[1] = FileTextureAny("file:textures\\pes_var_pelvis_skin_b.dds");
        return out[0] && out[1];
    }
    const VarEntry* e = nullptr;
    for (const VarEntry& k : kVars) if (k.idx == indexCount) { e = &k; break; }
    if (!e) return false;
    out[0] = FileTextureAny(e->torso ? "file:textures\\pes_var_torso_skin_a.dds" : "file:textures\\pes_var_arm_skin_a.dds");
    out[1] = FileTextureAny(e->torso ? "file:textures\\pes_var_torso_skin_b.dds" : "file:textures\\pes_var_arm_skin_b.dds");
    static std::atomic<int> s_noted{0};
    const int bit = e->torso ? 1 : 2;
    if (!(s_noted.fetch_or(bit) & bit)) Log("piel real: zonas del %s %s", e->torso ? "torso" : "brazo", (out[0] && out[1]) ? "cargadas (t21 / t24)" : "NO CARGADAS (faltan las pes_var_*_skin_a/b.dds)");
    return out[0] && out[1];
}
// (26/10) columna marcada (x288): el mapa normal delta de la columna en t19 del PS de la piel, en todos los torsos de TorsoBody
// (con o sin variante; no depende de x285)
ID3D11ShaderResourceView* SpineTextureFor(uint64_t vsHash, UINT indexCount) {
    if (vsHash != 0x1c02a92ab4ae3362ULL || g_cfg.live[288][0] <= 0.001f) return nullptr;
    bool torso = false;
    for (const VarEntry& k : kVars) if (k.idx == indexCount) { torso = k.torso; break; }
    if (!torso) return nullptr;
    ID3D11ShaderResourceView* v = FileTextureAny("file:textures\\pes_var_torso_spine_n.dds");
    static std::atomic<bool> s_noted{false};
    if (!s_noted.exchange(true)) Log("columna marcada: mapa %s", v ? "cargado (t19 del PS de la piel)" : "NO CARGADO (falta textures\\pes_var_torso_spine_n.dds)");
    return v;
}
bool VariantTextures(ID3D11ShaderResourceView* out[3]) {
    out[0] = FileTextureAny("file:textures\\pes_var_torso_rob_n.dds");
    out[1] = FileTextureAny("file:textures\\pes_var_torso_atl_n.dds");
    out[2] = FileTextureAny("file:textures\\pes_var_torso_rob_m.dds");
    static std::atomic<bool> s_noted{false};
    if (!s_noted.exchange(true)) Log("variantes del torso: mapas del PS %s / %s / %s", out[0] ? "ok" : "FALTA pes_var_torso_rob_n.dds", out[1] ? "ok" : "FALTA pes_var_torso_atl_n.dds", out[2] ? "ok" : "FALTA pes_var_torso_rob_m.dds");
    return out[0] && out[1];
}

} // namespace fx
