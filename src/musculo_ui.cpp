// PES21 FX - ventana de la musculatura (26/10 c). Ver musculo_ui.h.
//  - Cabecera: el jugador, si tiene valores propios o usa los generales, el tipo de cuerpo y "Comparar con el juego".
//  - Mapa del cuerpo (frente y espalda): cada musculo pintado segun su valor (azul = mas chico, gris = como el juego,
//    naranja = mas grande). Con el mouse encima se ve el valor y el musculo late en el modelo del juego; un clic lo elige
//    (abre su pestana y marca su fila) y la rueda del mouse lo cambia de a 0.01 (con Shift de a 0.05).
//  - "Todo el cuerpo": General y Definicion muscular. Pestanas por zona (las mismas paginas que el menu Fisico del juego) y
//    "Vivos" (contraccion y respiracion, para todos).
//  - Cada fila: punto de color, nombre, barra con la marca del valor del juego (1) y el tramo pintado hasta el valor, el
//    valor (x1.12), - / + (0.01) y 1 (volver a como el juego). Doble clic en la barra = 1.
#include "musculo_ui.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fx {

// ---------------------------------------------------------------- filas y zonas del mapa
// region: 0 brazos, 1 pecho y espalda, 2 abdomen y cadera, 3 piernas, -1 todo el cuerpo. vista: 1 frente, 2 espalda, 3 las dos,
// 0 sin zona. Zona = capsula (A, B, radio) en el modelo en reposo, lado izquierdo (X > 0; se espeja), en metros.
struct UiRow { const char* label; int idx, comp; float lo, hi; int region; int view; float ax, ay, bx, by, r; const char* help; };
static const UiRow kRows[] = {
    { "General",              167, 0, 0.7f, 1.5f, -1, 0, 0, 0, 0, 0, 0, "Agranda o achica todos los m\xC3\xBAsculos a la vez (no la panza, codos ni mu\xC3\xB1" "ecas). Se multiplica con cada uno." },
    { "Definici\xC3\xB3n muscular",  172, 0, 0.0f, 2.0f, -1, 0, 0, 0, 0, 0, 0, "Qu\xC3\xA9 tan marcados se ven los m\xC3\xBAsculos en la piel. 0 = piel lisa; 1 = como el juego." },
    { "Torso y brazos",       262, 2, 0.5f, 1.5f, -1, 0, 0, 0, 0, 0, 0, "Variantes de TorsoBody: hacia la izquierda ROBUSTO (panza, rollos, pecho ca\xC3\xAD" "do, abdominales borrados, estr\xC3\xAD" "as, brazos m\xC3\xA1s gruesos y lisos), hacia la derecha ATL\xC3\x89TICO (cintura fina, espalda en V, trapecio, abdominales y serratos marcados, venas en los brazos). Cambia el modelo, las normales y las texturas. 1 = TorsoBody." },
    // hombros y brazos
    { "Inclinaci\xC3\xB3n de hombros", 287, 0, 0.7f, 1.3f, 0, 0, 0, 0, 0, 0, 0, "Hombros ca\xC3\xAD" "dos (la tapa del hombro baja y el trapecio junto al cuello sube) o rectos (la l\xC3\xAD" "nea del cuello al hombro m\xC3\xA1s horizontal). 1 = normales." },
    { "Tipo de hombros",      287, 1, 1.0f, 4.0f, 0, 0, 0, 0, 0, 0, 0, "Estrechos, normales, anchos o redondos (deltoides grandes adelante y atr\xC3\xA1s). Se suma a los dem\xC3\xA1s valores del hombro." },
    { "Tama\xC3\xB1o de hombros",    287, 2, 0.7f, 1.3f, 0, 0, 0, 0, 0, 0, 0, "Todo el hombro m\xC3\xA1s grande o m\xC3\xA1s chico: deltoides, ancho, trapecio y clav\xC3\xAD" "cula juntos." },
    { "Hombros",              164, 1, 0.7f, 1.5f, 0, 3, 0.210f, 1.452f, 0.214f, 1.444f, 0.056f, "Deltoides: la tapa del hombro crece alrededor de la articulaci\xC3\xB3n." },
    { "Ancho de hombros",     171, 3, 0.7f, 1.5f, 0, 0, 0, 0, 0, 0, 0, "Corre los hombros hacia afuera, sin alargar los brazos." },
    { "Deltoides frontal",    260, 0, 0.7f, 1.5f, 0, 1, 0.206f, 1.456f, 0.224f, 1.420f, 0.034f, "La parte de adelante del hombro." },
    { "Deltoides posterior",  260, 1, 0.7f, 1.5f, 0, 2, 0.206f, 1.456f, 0.224f, 1.420f, 0.034f, "La parte de atr\xC3\xA1s del hombro." },
    { "B\xC3\xAD" "ceps",               164, 2, 0.7f, 1.5f, 0, 1, 0.252f, 1.408f, 0.368f, 1.298f, 0.038f, "Frente del brazo: engorda y se corre hacia adelante." },
    { "Tr\xC3\xAD" "ceps",              164, 3, 0.7f, 1.5f, 0, 2, 0.252f, 1.408f, 0.368f, 1.292f, 0.038f, "Parte de atr\xC3\xA1s del brazo." },
    { "Antebrazos",           165, 0, 0.7f, 1.5f, 0, 3, 0.430f, 1.234f, 0.580f, 1.094f, 0.036f, "Todo el antebrazo (multiplica a alto y bajo)." },
    { "Antebrazo alto",       260, 2, 0.7f, 1.5f, 0, 1, 0.428f, 1.236f, 0.488f, 1.180f, 0.030f, "La masa del antebrazo cerca del codo." },
    { "Antebrazo bajo",       260, 3, 0.7f, 1.5f, 0, 1, 0.520f, 1.150f, 0.574f, 1.100f, 0.024f, "El antebrazo cerca de la mu\xC3\xB1" "eca." },
    { "Codos",                166, 3, 0.7f, 1.5f, 0, 2, 0.400f, 1.260f, 0.400f, 1.260f, 0.024f, "El codo (no es m\xC3\xBAsculo: General no lo mueve)." },
    { "Mu\xC3\xB1" "ecas",              170, 3, 0.7f, 1.5f, 0, 3, 0.596f, 1.080f, 0.596f, 1.080f, 0.020f, "La mu\xC3\xB1" "eca (no es m\xC3\xBAsculo)." },
    { "Axilas",               162, 0, 0.7f, 1.5f, 0, 1, 0.166f, 1.392f, 0.176f, 1.338f, 0.024f, "La axila de adelante, entre el pecho y el brazo." },
    // pecho, espalda y cuello
    { "Pecho",                165, 1, 0.7f, 1.5f, 1, 1, 0.042f, 1.376f, 0.128f, 1.380f, 0.058f, "Pectorales." },
    { "Pecho alto",           170, 1, 0.7f, 1.5f, 1, 1, 0.036f, 1.452f, 0.122f, 1.448f, 0.028f, "La parte de arriba del pecho, bajo la clav\xC3\xAD" "cula (se multiplica con Pecho)." },
    { "Pecho bajo",           261, 0, 0.7f, 1.5f, 1, 1, 0.046f, 1.330f, 0.126f, 1.334f, 0.028f, "La masa de abajo del pectoral." },
    { "Espalda (toda)",       165, 2, 0.7f, 1.5f, 1, 2, 0.060f, 1.455f, 0.052f, 1.105f, 0.110f, "Multiplica a trapecio, om\xC3\xB3platos, dorsales y espalda baja." },
    { "Trapecio",             167, 1, 0.7f, 1.5f, 1, 3, 0.046f, 1.556f, 0.150f, 1.494f, 0.034f, "Entre el cuello y el hombro." },
    { "Trapecio alto",        261, 2, 0.7f, 1.5f, 1, 2, 0.040f, 1.586f, 0.110f, 1.546f, 0.022f, "Solo hacia arriba, junto al cuello." },
    { "Om\xC3\xB3platos",            167, 2, 0.7f, 1.5f, 1, 2, 0.086f, 1.420f, 0.100f, 1.370f, 0.048f, "Espalda alta, hacia atr\xC3\xA1s." },
    { "Dorsales",             167, 3, 0.7f, 1.5f, 1, 2, 0.150f, 1.378f, 0.132f, 1.222f, 0.044f, "La espalda se ensancha y gana grosor." },
    { "Dorsales: ancho (V)",  261, 1, 0.7f, 1.5f, 1, 2, 0.178f, 1.368f, 0.162f, 1.244f, 0.022f, "Solo hacia los costados: la espalda en V." },
    { "Costados (serratos)",  162, 1, 0.7f, 1.5f, 1, 1, 0.150f, 1.308f, 0.156f, 1.212f, 0.028f, "Los costados de las costillas, bajo la axila." },
    { "Espalda baja",         168, 0, 0.7f, 1.5f, 1, 2, 0.036f, 1.168f, 0.036f, 1.052f, 0.048f, "Centro de la espalda abajo." },
    { "Lumbares",             162, 2, 0.7f, 1.5f, 1, 2, 0.066f, 1.210f, 0.066f, 1.112f, 0.028f, "A los lados de la columna, abajo." },
    { "Cuello",               170, 0, 0.7f, 1.5f, 1, 1, 0.000f, 1.552f, 0.000f, 1.640f, 0.044f, "Frente y costados del cuello." },
    { "Nuca",                 166, 2, 0.7f, 1.5f, 1, 2, 0.000f, 1.560f, 0.000f, 1.648f, 0.044f, "La parte de atr\xC3\xA1s del cuello (se multiplica con Cuello)." },
    { "Base del cuello",      162, 3, 0.7f, 1.5f, 1, 1, 0.046f, 1.520f, 0.076f, 1.506f, 0.024f, "Donde el cuello se une al torso." },
    // abdomen y cadera
    { "Abdominales",          168, 1, 0.7f, 1.5f, 2, 1, 0.040f, 1.250f, 0.040f, 1.062f, 0.038f, "Hacia adelante." },
    { "Oblicuos",             168, 2, 0.7f, 1.5f, 2, 1, 0.134f, 1.198f, 0.140f, 1.062f, 0.033f, "Los costados de la cintura." },
    { "Cintura",              171, 1, 0.7f, 1.5f, 2, 3, 0.158f, 1.168f, 0.152f, 1.082f, 0.022f, "Ancho de la cintura." },
    { "Panza",                168, 3, 0.7f, 1.5f, 2, 1, 0.000f, 1.128f, 0.000f, 1.092f, 0.056f, "El centro de la barriga (no es m\xC3\xBAsculo)." },
    { "Gl\xC3\xBAteos",              169, 0, 0.7f, 1.5f, 2, 2, 0.086f, 0.968f, 0.086f, 0.930f, 0.068f, "Parte alta de los gl\xC3\xBAteos, hacia atr\xC3\xA1s." },
    { "Gl\xC3\xBAteo levantado",     261, 3, 0.7f, 1.5f, 2, 2, 0.086f, 1.004f, 0.086f, 0.990f, 0.034f, "Sube la parte alta del gl\xC3\xBAteo." },
    { "Caderas",              171, 2, 0.7f, 1.5f, 2, 3, 0.172f, 0.968f, 0.172f, 0.930f, 0.028f, "Ancho de la cadera." },
    // piernas
    { "Muslos (todo)",        165, 3, 0.7f, 1.5f, 3, 3, 0.102f, 0.880f, 0.136f, 0.622f, 0.074f, "Multiplica a cu\xC3\xA1" "driceps, isquiotibiales y aductores." },
    { "Cu\xC3\xA1" "driceps",           169, 1, 0.7f, 1.5f, 3, 1, 0.106f, 0.858f, 0.136f, 0.642f, 0.048f, "Frente del muslo." },
    { "Muslo exterior",       262, 0, 0.7f, 1.5f, 3, 1, 0.166f, 0.858f, 0.176f, 0.642f, 0.026f, "El costado de afuera del muslo." },
    { "Isquiotibiales",       169, 2, 0.7f, 1.5f, 3, 2, 0.106f, 0.858f, 0.136f, 0.642f, 0.048f, "Parte de atr\xC3\xA1s del muslo." },
    { "Aductores",            169, 3, 0.7f, 1.5f, 3, 1, 0.056f, 0.838f, 0.086f, 0.682f, 0.026f, "Cara interna del muslo." },
    { "Rodillas",             171, 0, 0.6f, 1.4f, 3, 1, 0.141f, 0.552f, 0.141f, 0.540f, 0.038f, "Grosor de la rodilla." },
    { "Pantorrillas",         166, 0, 0.7f, 1.5f, 3, 2, 0.150f, 0.488f, 0.156f, 0.302f, 0.044f, "Toda la pantorrilla." },
    { "Gemelos",              262, 1, 0.7f, 1.5f, 3, 2, 0.150f, 0.480f, 0.153f, 0.384f, 0.030f, "La panza de la pantorrilla, hacia atr\xC3\xA1s." },
};
static const int kNRows = (int)(sizeof kRows / sizeof kRows[0]);
static const char* const kRegion[] = { "Brazos", "Torso", "Abdomen", "Piernas" };   // pestanas (las paginas del juego: hombros y brazos, pecho espalda y cuello, abdomen y cadera, piernas)
struct UiGlobalRow { const char* label; int idx, comp; float lo, hi; const char* help; };
static const UiGlobalRow kAlive[] = {
    { "Contracci\xC3\xB3n con el movimiento", 263, 0, 0.0f, 2.0f, "El b\xC3\xAD" "ceps se abulta al doblar el codo, el hombro y el trapecio al levantar el brazo, isquiotibiales y pantorrilla al doblar la rodilla. 0 = apagado." },
    { "Respiraci\xC3\xB3n",                   263, 1, 0.0f, 2.0f, "El pecho, las costillas, la espalda y la panza suben y bajan. 0 = apagado." },
    { "Se agita al correr",            263, 2, 0.0f, 2.0f, "Despu\xC3\xA9s de correr respira m\xC3\xA1s r\xC3\xA1pido y m\xC3\xA1s hondo, y se calma de a poco." },
};

// ---------------------------------------------------------------- colores
static ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }
static const ImVec4 kNeutral(0.42f, 0.46f, 0.53f, 1.0f), kWarm(1.00f, 0.56f, 0.20f, 1.0f), kCool(0.32f, 0.64f, 1.00f, 1.0f);
static const ImVec4 kAccent(0.20f, 0.72f, 0.46f, 1.0f), kGold(0.85f, 0.68f, 0.28f, 1.0f);
static ImVec4 Heat(float v, float lo = 0.7f, float hi = 1.5f) {
    const float d = v - 1.0f;
    if (fabsf(d) < 0.005f) return kNeutral;
    if (d > 0.0f) return Mix(Mix(kNeutral, kWarm, 0.35f), kWarm, std::min(d / std::max(hi - 1.0f, 0.05f) * 1.6f, 1.0f));
    return Mix(Mix(kNeutral, kCool, 0.35f), kCool, std::min(-d / std::max(1.0f - lo, 0.05f) * 1.6f, 1.0f));
}
static ImU32 U32(const ImVec4& c, float a = -1.0f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a < 0.0f ? c.w : a)); }

// ---------------------------------------------------------------- estado de la ventana
static int s_sel = -1;          // fila elegida (en el mapa o en la lista)
static int s_tabWant = -1;      // pestana que hay que abrir (al elegir en el mapa)
static bool s_scrollTo = false; // llevar la lista a la fila elegida
static int s_hoverRow = -1;     // fila bajo el mouse (mapa o lista) en este cuadro
static char s_typeName[48] = "";

// ---------------------------------------------------------------- barra con la marca del juego
// devuelve true si cambio. Arrastrar, rueda (0.01; Shift 0.05), doble clic = 1.
static bool ValueBar(const char* id, float* v, float lo, float hi, float w, bool selected) {
    const float h = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    bool changed = false;
    float nv = *v;
    const float pad = 9.0f, x0 = p.x + pad, x1 = p.x + w - pad;
    if (act && ImGui::IsMouseDown(0)) {
        float t = std::clamp((ImGui::GetIO().MousePos.x - x0) / std::max(x1 - x0, 1.0f), 0.0f, 1.0f);
        nv = lo + (hi - lo) * t;
        if (fabsf(nv - 1.0f) < (hi - lo) * 0.012f) nv = 1.0f;   // se pega al valor del juego
    }
    if (hov && ImGui::GetIO().MouseWheel != 0.0f) nv += (ImGui::GetIO().KeyShift ? 0.05f : 0.01f) * (ImGui::GetIO().MouseWheel > 0 ? 1.0f : -1.0f);
    if (hov && ImGui::IsMouseDoubleClicked(0)) nv = 1.0f;
    nv = std::clamp(roundf(nv * 100.0f) / 100.0f, lo, hi);
    if (fabsf(nv - *v) > 1e-4f) { *v = nv; changed = true; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + h * 0.5f;
    auto X = [&](float val) { return x0 + (x1 - x0) * std::clamp((val - lo) / (hi - lo), 0.0f, 1.0f); };
    const ImVec4 c = Heat(*v, lo, hi);
    dl->AddRectFilled(ImVec2(x0 - 4, cy - 3), ImVec2(x1 + 4, cy + 3), U32(ImVec4(0.17f, 0.19f, 0.23f, 1)), 3.0f);
    if (1.0f >= lo && 1.0f <= hi) {   // marca del juego
        const float xg = X(1.0f);
        dl->AddRectFilled(ImVec2(std::min(xg, X(*v)), cy - 3), ImVec2(std::max(xg, X(*v)), cy + 3), U32(c), 3.0f);
        dl->AddLine(ImVec2(xg, cy - 7), ImVec2(xg, cy + 7), U32(ImVec4(0.75f, 0.78f, 0.84f, 0.85f)), 2.0f);
    }
    const float kx = X(*v), kr = (hov || act) ? 8.0f : 7.0f;
    dl->AddCircleFilled(ImVec2(kx, cy), kr + 2.0f, U32(ImVec4(0.05f, 0.06f, 0.08f, 0.9f)));
    dl->AddCircleFilled(ImVec2(kx, cy), kr, U32(fabsf(*v - 1.0f) < 0.005f ? ImVec4(0.86f, 0.88f, 0.92f, 1) : c));
    if (selected) dl->AddCircle(ImVec2(kx, cy), kr + 3.5f, U32(ImVec4(1, 1, 1, 0.9f)), 0, 1.5f);
    return changed;
}

// ---------------------------------------------------------------- mapa del cuerpo
struct Shape { int kind; float ax, ay, bx, by, r; };   // 0 capsula, 1 circulo, 2 elipse (r = radio x, by = radio y)
static const Shape kBody[] = {
    { 1, 0.000f, 1.745f, 0, 0, 0.100f },                          // cabeza
    { 0, 0.000f, 1.560f, 0.000f, 1.660f, 0.052f },                // cuello
    { 0, 0.190f, 1.462f, -0.190f, 1.462f, 0.056f },               // hombros
    { 2, 0.000f, 1.370f, 0, 0.150f, 0.192f },                     // pecho (elipse: centro (ax, ay), radios (r, by))
    { 2, 0.000f, 1.160f, 0, 0.140f, 0.158f },                     // abdomen
    { 2, 0.000f, 0.950f, 0, 0.092f, 0.180f },                     // pelvis
    { 0, 0.205f, 1.445f, 0.400f, 1.262f, 0.050f },                // brazo
    { 0, 0.400f, 1.262f, 0.596f, 1.074f, 0.040f },                // antebrazo
    { 1, 0.642f, 1.028f, 0, 0, 0.044f },                          // mano
    { 0, 0.095f, 0.930f, 0.140f, 0.548f, 0.080f },                // muslo
    { 0, 0.140f, 0.548f, 0.150f, 0.090f, 0.054f },                // pierna
    { 2, 0.160f, 0.040f, 0, 0.032f, 0.050f },                     // pie
};
static float SegDist(float px, float py, float ax, float ay, float bx, float by) {
    const float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay, l2 = vx * vx + vy * vy;
    const float t = l2 > 1e-9f ? std::clamp((wx * vx + wy * vy) / l2, 0.0f, 1.0f) : 0.0f;
    const float dx = wx - vx * t, dy = wy - vy * t;
    return sqrtf(dx * dx + dy * dy);
}
static void Capsule(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, ImU32 col) {
    dl->AddLine(a, b, col, 2.0f * r);
    dl->AddCircleFilled(a, r, col, 24); dl->AddCircleFilled(b, r, col, 24);
}
static void CapsuleOutline(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, ImU32 col, float th) {
    const float dx = b.x - a.x, dy = b.y - a.y, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.5f) { dl->AddCircle(a, r, col, 32, th); return; }
    const float ang = atan2f(dy, dx);
    dl->PathArcTo(a, r, ang + 1.5707963f, ang + 4.712389f, 16);
    dl->PathArcTo(b, r, ang - 1.5707963f, ang + 1.5707963f, 16);
    dl->PathStroke(col, ImDrawFlags_Closed, th);
}
// devuelve la fila elegida con un clic (-1 = ninguna)
static int BodyMap(MusUiModel& m, float W, float H) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##mapa", ImVec2(W, H));
    const bool mapHov = ImGui::IsItemHovered(), mapClick = ImGui::IsItemClicked(0);
    dl->AddRectFilled(o, ImVec2(o.x + W, o.y + H), U32(ImVec4(0.09f, 0.10f, 0.13f, 1)), 10.0f);
    const float top = 24.0f, bottom = 26.0f;
    const float s = std::min((W * 0.5f - 18.0f) / 1.42f, (H - top - bottom) / 1.83f);
    const float base = o.y + H - bottom;
    const float cx[2] = { o.x + W * 0.25f, o.x + W * 0.75f };
    auto P = [&](int view, float x, float y) { return ImVec2(cx[view] + (view == 0 ? x : -x) * s, base - y * s); };
    const ImU32 body = U32(ImVec4(0.19f, 0.21f, 0.26f, 1)), bodyEdge = U32(ImVec4(0.26f, 0.29f, 0.35f, 1));
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hit = -1; float hitR = 1e9f; int hitView = -1;
    for (int view = 0; view < 2; view++) {
        const char* lab = TRS(view == 0 ? "FRENTE" : "ESPALDA");
        const ImVec2 ts = ImGui::CalcTextSize(lab);
        dl->AddText(ImVec2(cx[view] - ts.x * 0.5f, o.y + 6.0f), U32(ImVec4(0.55f, 0.58f, 0.64f, 1)), lab);
        // silueta (las dos mitades)
        for (int pass = 0; pass < 2; pass++) for (const Shape& sh : kBody) for (int side = 0; side < 2; side++) {
            const float sx = side == 0 ? 1.0f : -1.0f;
            if (side == 1 && sh.ax == 0.0f && (sh.kind != 0 || sh.bx == 0.0f || sh.bx == -sh.ax)) continue;   // centrales una vez
            const float grow = pass == 0 ? 1.5f : 0.0f; const ImU32 col = pass == 0 ? bodyEdge : body;
            if (sh.kind == 0) Capsule(dl, P(view, sh.ax * sx, sh.ay), P(view, sh.bx * sx, sh.by), sh.r * s + grow, col);
            else if (sh.kind == 1) dl->AddCircleFilled(P(view, sh.ax * sx, sh.ay), sh.r * s + grow, col, 32);
            else dl->AddEllipseFilled(P(view, sh.ax * sx, sh.ay), ImVec2(sh.r * s + grow, sh.by * s + grow), col, 0.0f, 32);
        }
        // musculos: los grandes primero (los chicos quedan encima)
        int order[kNRows]; int n = 0;
        for (int i = 0; i < kNRows; i++) if (kRows[i].view & (1 << view)) order[n++] = i;
        std::sort(order, order + n, [](int a, int b) { return kRows[a].r > kRows[b].r; });
        const float mxm = (mouse.x - cx[view]) / s * (view == 0 ? 1.0f : -1.0f), mym = (base - mouse.y) / s;
        for (int k = 0; k < n; k++) {
            const UiRow& r = kRows[order[k]];
            const float v = m.Get(r.idx, r.comp);
            const ImVec4 c = Heat(v, r.lo, r.hi);
            const bool central = r.ax == 0.0f && r.bx == 0.0f;
            for (int side = 0; side < (central ? 1 : 2); side++) {
                const float sx = side == 0 ? 1.0f : -1.0f;
                Capsule(dl, P(view, r.ax * sx, r.ay), P(view, r.bx * sx, r.by), r.r * s, U32(c, r.r > 0.07f ? 0.45f : 0.82f));   // las zonas grandes mas transparentes
            }
            if (mapHov && SegDist(fabsf(mxm), mym, fabsf(r.ax), r.ay, fabsf(r.bx), r.by) < r.r && r.r < hitR) { hit = order[k]; hitR = r.r; hitView = view; }
        }
    }
    // contorno del elegido y del que esta bajo el mouse
    for (int pass = 0; pass < 2; pass++) {
        const int i = pass == 0 ? s_sel : hit;
        if (i < 0 || !kRows[i].view) continue;
        const UiRow& r = kRows[i];
        const bool central = r.ax == 0.0f && r.bx == 0.0f;
        for (int view = 0; view < 2; view++) if (r.view & (1 << view)) for (int side = 0; side < (central ? 1 : 2); side++) {
            const float sx = side == 0 ? 1.0f : -1.0f;
            CapsuleOutline(dl, P(view, r.ax * sx, r.ay), P(view, r.bx * sx, r.by), r.r * s + 1.5f, U32(ImVec4(1, 1, 1, pass == 0 ? 0.95f : 0.55f)), pass == 0 ? 2.0f : 1.5f);
        }
    }
    // leyenda
    {
        const float lw = std::min(W - 120.0f, 260.0f), lx = o.x + (W - lw) * 0.5f, ly = o.y + H - 15.0f;
        const int seg = 24;
        for (int k = 0; k < seg; k++) {
            const float a = (float)k / seg, b = (float)(k + 1) / seg;
            const float va = 0.7f + 0.8f * a, vb = 0.7f + 0.8f * b;
            dl->AddRectFilledMultiColor(ImVec2(lx + lw * a, ly - 3), ImVec2(lx + lw * b, ly + 3), U32(Heat(va)), U32(Heat(vb)), U32(Heat(vb)), U32(Heat(va)));
        }
        const ImU32 tc = U32(ImVec4(0.55f, 0.58f, 0.64f, 1));
        const char* l0 = TRS("m\xC3\xA1s chico"); const char* l1 = TRS("m\xC3\xA1s grande");
        dl->AddText(ImVec2(lx - ImGui::CalcTextSize(l0).x - 8.0f, ly - ImGui::GetTextLineHeight() * 0.5f), tc, l0);
        dl->AddText(ImVec2(lx + lw + 8.0f, ly - ImGui::GetTextLineHeight() * 0.5f), tc, l1);
        dl->AddLine(ImVec2(lx + lw * 0.375f, ly - 6), ImVec2(lx + lw * 0.375f, ly + 6), U32(ImVec4(0.85f, 0.88f, 0.92f, 0.9f)), 2.0f);   // 1.0
    }
    int clicked = -1;
    if (hit >= 0) {
        const UiRow& r = kRows[hit];
        s_hoverRow = hit;
        float v = m.Get(r.idx, r.comp);
        if (ImGui::GetIO().MouseWheel != 0.0f) {
            v = std::clamp(roundf((v + (ImGui::GetIO().KeyShift ? 0.05f : 0.01f) * (ImGui::GetIO().MouseWheel > 0 ? 1.0f : -1.0f)) * 100.0f) / 100.0f, r.lo, r.hi);
            m.Set(r.idx, r.comp, v);
        }
        ImGui::BeginTooltip();
        ImGui::TextColored(Heat(v, r.lo, r.hi), "%s  x%.2f", TRS(r.label), v);
        const float g = m.General(r.idx, r.comp);
        if (m.HasOwn() && fabsf(g - v) > 0.004f) { ImGui::SameLine(); ImGui::TextDisabled(TRS("(general x%.2f)"), g); }
        ImGui::PushTextWrapPos(320.0f); ImGui::TextDisabled("%s", TRS(r.help)); ImGui::PopTextWrapPos();
        ImGui::TextDisabled("%s", TRS("Clic: elegir   Rueda: cambiar (Shift: de a 0.05)"));
        ImGui::EndTooltip();
        if (mapClick) clicked = hit;
        (void)hitView;
    }
    return clicked;
}

// ---------------------------------------------------------------- una fila
static void Row(MusUiModel& m, int i, float labelW) {
    const UiRow& r = kRows[i];
    ImGui::PushID(i);
    ImGui::TableNextRow(0, ImGui::GetFrameHeight() + 6.0f);
    float v = m.Get(r.idx, r.comp);
    const bool sel = i == s_sel, hot = i == s_hoverRow;
    if (sel) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, U32(ImVec4(0.20f, 0.30f, 0.26f, 0.45f)));
    else if (hot) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, U32(ImVec4(1, 1, 1, 0.05f)));
    if (sel && s_scrollTo) { ImGui::SetScrollHereY(0.35f); s_scrollTo = false; }
    const ImVec4 c = Heat(v, r.lo, r.hi);
    const bool changed = fabsf(v - 1.0f) > 0.004f;
    ImGui::TableSetColumnIndex(0);
    {   // punto de color + nombre
        ImGui::AlignTextToFramePadding();
        const ImVec2 p = ImGui::GetCursorScreenPos(); const float lh = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 6.0f, p.y + ImGui::GetStyle().FramePadding.y + lh * 0.5f), 5.0f, U32(c));
        ImGui::SetCursorScreenPos(ImVec2(p.x + 18.0f, p.y));
        if (changed) ImGui::PushStyleColor(ImGuiCol_Text, Mix(c, ImVec4(1, 1, 1, 1), 0.35f));
        ImGui::TextUnformatted(TRS(r.label));
        if (changed) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) { s_hoverRow = i; ImGui::BeginTooltip(); ImGui::PushTextWrapPos(320.0f); ImGui::TextUnformatted(TRS(r.help)); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
        if (ImGui::IsItemClicked()) s_sel = i;
    }
    ImGui::TableSetColumnIndex(1);
    static const char* const kShType[5] = { "Normales", "Normales", "Estrechos", "Anchos", "Redondos" };   // (26/10) 287.y: 1..4
    const bool shType = r.idx == 287 && r.comp == 1;
    if (shType) {   // tipo de hombros: lista (no es un multiplicador)
        const int cur = std::clamp((int)lroundf(v < 0.5f ? 1.0f : v), 1, 4);
        static const int kOrder[4] = { 2, 1, 3, 4 };   // como el menu Fisico: estrechos, normales, anchos, redondos
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::BeginCombo("##ty", TRS(kShType[cur]))) {
            for (int o : kOrder) if (ImGui::Selectable(TRS(kShType[o]), o == cur)) { m.Set(r.idx, r.comp, (float)o); s_sel = i; }
            ImGui::EndCombo();
        }
    } else
    if (ValueBar("##v", &v, r.lo, r.hi, ImGui::GetContentRegionAvail().x, sel)) { m.Set(r.idx, r.comp, v); s_sel = i; }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) s_hoverRow = i;
    ImGui::TableSetColumnIndex(2);
    ImGui::AlignTextToFramePadding();
    if (r.idx == 262 && r.comp == 2)   // (26/10) variante: robusto / atletico en vez de un multiplicador
        ImGui::TextColored(changed ? Mix(c, ImVec4(1, 1, 1, 1), 0.25f) : ImVec4(0.62f, 0.65f, 0.71f, 1), "%s", TRS(!changed ? "TorsoBody" : v < 1.0f ? "robusto" : "atl\xC3\xA9tico"));
    else if (r.idx == 287 && r.comp == 0)   // (26/10) inclinacion de los hombros
        ImGui::TextColored(changed ? Mix(c, ImVec4(1, 1, 1, 1), 0.25f) : ImVec4(0.62f, 0.65f, 0.71f, 1), "%s", TRS(!changed ? "normales" : v < 1.0f ? "ca\xC3\xAD" "dos" : "rectos"));
    else if (shType)
        ImGui::TextColored(changed ? Mix(c, ImVec4(1, 1, 1, 1), 0.25f) : ImVec4(0.62f, 0.65f, 0.71f, 1), "%s", TRS(changed ? "propio" : "-"));
    else
    ImGui::TextColored(changed ? Mix(c, ImVec4(1, 1, 1, 1), 0.25f) : ImVec4(0.62f, 0.65f, 0.71f, 1), "x%.2f", v);
    if (m.HasOwn()) {
        const float g = m.General(r.idx, r.comp);
        if (ImGui::IsItemHovered() && fabsf(g - v) > 0.004f) ImGui::SetTooltip("%s: x%.2f", TRS("General"), g);
    }
    ImGui::TableSetColumnIndex(3);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, ImGui::GetStyle().FramePadding.y));
    const float stp = shType ? 1.0f : ImGui::GetIO().KeyShift ? 0.05f : 0.01f;
    if (ImGui::Button("-")) { m.Set(r.idx, r.comp, std::clamp(roundf((v - stp) * 100.0f) / 100.0f, r.lo, r.hi)); s_sel = i; }
    ImGui::SameLine(0, 4);
    if (ImGui::Button("+")) { m.Set(r.idx, r.comp, std::clamp(roundf((v + stp) * 100.0f) / 100.0f, r.lo, r.hi)); s_sel = i; }
    ImGui::SameLine(0, 4);
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button("1")) { m.Set(r.idx, r.comp, 1.0f); s_sel = i; }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", TRS("Como el juego (x1.00)"));
    ImGui::PopStyleVar();
    (void)labelW;
    ImGui::PopID();
}
static bool BeginRows(const char* id) {
    const ImGuiTableFlags fl = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
    if (!ImGui::BeginTable(id, 4, fl)) return false;
    ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed, 172.0f);
    ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthFixed, 96.0f);
    return true;
}

// ---------------------------------------------------------------- cabecera
static void Pill(const char* text, const ImVec4& col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ts = ImGui::CalcTextSize(text), p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight(), padX = 10.0f;
    ImGui::Dummy(ImVec2(ts.x + 2 * padX, h));
    const float y0 = p.y + (h - ts.y) * 0.5f - 3.0f, y1 = p.y + (h + ts.y) * 0.5f + 3.0f;
    dl->AddRectFilled(ImVec2(p.x, y0), ImVec2(p.x + ts.x + 2 * padX, y1), U32(col, 0.18f), (y1 - y0) * 0.5f);
    dl->AddRect(ImVec2(p.x, y0), ImVec2(p.x + ts.x + 2 * padX, y1), U32(col, 0.55f), (y1 - y0) * 0.5f, 0, 1.0f);
    dl->AddText(ImVec2(p.x + padX, p.y + (h - ts.y) * 0.5f), U32(col), text);
}
static int TypeCombo(MusUiModel& m, const char* id, float w) {
    const int cur = m.TypeMatch();
    int pick = -1;
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo(id, TRS(cur >= 0 ? m.TypeName(cur) : "A mano"))) {
        for (int k = 0; k < m.TypeCount(); k++) {
            if (k == m.BuiltinTypes()) ImGui::SeparatorText(TRS("Tipos propios"));
            if (ImGui::Selectable(TRS(m.TypeName(k)), k == cur)) pick = k;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS(m.TypeDesc(k)));
        }
        ImGui::EndCombo();
    }
    return pick;
}

void MuscleUiDraw(MusUiModel& m, ImFont* bold, ImFont* big) {
    s_hoverRow = -1;
    const ImGuiStyle& st = ImGui::GetStyle();
    const float W = ImGui::GetContentRegionAvail().x;
    // --- cabecera: jugador
    if (m.PlayerCount() == 0) {
        ImGui::TextWrapped("%s", TRS("No hay jugadores en pantalla. Entr\xC3\xA1 a Editar > Jugadores > F\xC3\xADsico, o abr\xC3\xAD este men\xC3\xBA en un partido o una repetici\xC3\xB3n para elegir a cualquiera de la cancha."));
        return;
    }
    const int sp = std::clamp(m.Selected(), 0, m.PlayerCount() - 1);
    const MusUiPlayer& pl = m.Player(sp);
    {
        if (big) ImGui::PushFont(big);
        if (m.PlayerCount() > 1) {
            ImGui::SetNextItemWidth(std::min(W - 200.0f, 300.0f));
            if (ImGui::BeginCombo("##jug", pl.name.empty() ? TRS("(sin nombre)") : pl.name.c_str())) {
                for (int i = 0; i < m.PlayerCount(); i++) {
                    const MusUiPlayer& q = m.Player(i);
                    char b[128]; snprintf(b, sizeof b, "%s%s##%u", q.name.empty() ? TRS("(sin nombre)") : q.name.c_str(), q.own ? "  *" : "", q.id);
                    if (ImGui::Selectable(b, i == sp)) m.Select(i);
                }
                ImGui::EndCombo();
            }
        } else ImGui::TextUnformatted(pl.name.empty() ? TRS("(sin nombre)") : pl.name.c_str());
        if (big) ImGui::PopFont();
        ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("id %u", pl.id);
        ImGui::SameLine();
        Pill(TRS(m.HasOwn() ? "VALORES PROPIOS" : "USA LOS GENERALES"), m.HasOwn() ? kAccent : ImVec4(0.62f, 0.65f, 0.71f, 1));
        const float cw = ImGui::CalcTextSize(TRS("Prendida")).x + ImGui::GetFrameHeight() + st.ItemInnerSpacing.x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), W - cw));
        bool on = m.MuscleOn();
        if (ImGui::Checkbox(TRS("Prendida"), &on)) m.SetMuscleOn(on);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("La musculatura de todos los jugadores (Musculatura (prueba))."));
    }
    {   // --- tipo de cuerpo, comparar, interruptor
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(TRS("Tipo de cuerpo")); ImGui::SameLine();
        const int pk = TypeCombo(m, "##tipo", 170.0f);
        if (pk >= 0) m.ApplyType(pk);
        ImGui::SameLine();
        ImGui::Button(TRS("Comparar con el juego"));
        const bool cmp = ImGui::IsItemActive();
        m.Compare(cmp);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Manten\xC3\xA9 apretado: el cuerpo se ve como lo hizo Konami (sin la musculatura). Solt\xC3\xA1 para volver."));
    }
    if (!m.HasOwn()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.65f, 0.71f, 1));
        ImGui::TextWrapped("%s", TRS("Usa los valores generales. Al mover cualquier valor pasa a tener valores propios (solo \xC3\xA9l)."));
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    // --- mapa
    const float mapH = std::clamp(W * 0.62f, 240.0f, 380.0f);
    const int clicked = BodyMap(m, W, mapH);
    if (clicked >= 0) { s_sel = clicked; s_tabWant = kRows[clicked].region; s_scrollTo = true; }
    ImGui::Spacing();
    // --- todo el cuerpo
    if (BeginRows("##global")) {
        for (int i = 0; i < kNRows; i++) if (kRows[i].region < 0) Row(m, i, 0.0f);
        ImGui::EndTable();
    }
    // --- pestanas
    if (ImGui::BeginTabBar("##zonas", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int z = 0; z < 4; z++) {
            ImGuiTabItemFlags tf = s_tabWant == z ? ImGuiTabItemFlags_SetSelected : 0;
            char tl[64]; int nch = 0;   // la pestana dice cuantos valores cambio
            for (int i = 0; i < kNRows; i++) if (kRows[i].region == z && fabsf(m.Get(kRows[i].idx, kRows[i].comp) - 1.0f) > 0.004f) nch++;
            if (nch) snprintf(tl, sizeof tl, "%s  %d###z%d", TRS(kRegion[z]), nch, z); else snprintf(tl, sizeof tl, "%s###z%d", TRS(kRegion[z]), z);
            if (ImGui::BeginTabItem(tl, nullptr, tf)) {
                const float reserve = (m.HasOwn() ? 3.0f : 1.0f) * (ImGui::GetFrameHeight() + st.ItemSpacing.y) + 24.0f;   // lugar para los botones de abajo
                ImGui::BeginChild("##filas", ImVec2(0, std::max(ImGui::GetContentRegionAvail().y - reserve, 240.0f)), ImGuiChildFlags_None);
                if (BeginRows("##t")) { for (int i = 0; i < kNRows; i++) if (kRows[i].region == z) Row(m, i, 0.0f); ImGui::EndTable(); }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGuiTabItemFlags tf = 0;
        if (ImGui::BeginTabItem((std::string(TRS("Vivos")) + "###vivos").c_str(), nullptr, tf)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.65f, 0.71f, 1));
            ImGui::TextWrapped("%s", TRS("Para todos los jugadores: los m\xC3\xBAsculos se mueven con el cuerpo y respiran."));
            ImGui::PopStyleColor();
            if (BeginRows("##vivos")) {
                for (const UiGlobalRow& g : kAlive) {
                    ImGui::PushID(g.idx * 4 + g.comp);
                    ImGui::TableNextRow(0, ImGui::GetFrameHeight() + 6.0f);
                    float v = m.Global(g.idx, g.comp);
                    ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(TRS(g.label));
                    if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(320.0f); ImGui::TextUnformatted(TRS(g.help)); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
                    ImGui::TableSetColumnIndex(1);
                    if (ValueBar("##g", &v, g.lo, g.hi, ImGui::GetContentRegionAvail().x, false)) m.SetGlobal(g.idx, g.comp, v);
                    ImGui::TableSetColumnIndex(2); ImGui::AlignTextToFramePadding(); ImGui::Text(v < 0.005f ? TRS("apag.") : "x%.2f", v);
                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::Button(TRS("Normal"))) m.SetGlobal(g.idx, g.comp, 1.0f);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    s_tabWant = -1;
    // --- acciones
    ImGui::Separator();
    if (m.HasOwn()) {
        auto bw = [&](const char* t) { return ImGui::CalcTextSize(t).x + 2.0f * st.FramePadding.x; };
        const float row1 = (m.OtherCount() > 0 ? 200.0f + st.ItemSpacing.x : 0.0f) + bw(TRS("Copiar los generales")) + st.ItemSpacing.x + bw(TRS("Quitar sus valores"));
        if (m.OtherCount() > 0) {
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::BeginCombo("##copiar", TRS("Copiar de otro jugador..."))) {
                for (int i = 0; i < m.OtherCount(); i++) if (ImGui::Selectable(m.OtherName(i))) m.CopyFromOther(i);
                ImGui::EndCombo();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button(TRS("Copiar los generales"))) m.CopyGeneral();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Le pone los valores de las tarjetas generales (sigue con valores propios)."));
        if (row1 <= ImGui::GetContentRegionAvail().x) ImGui::SameLine();   // si no entra, a la linea de abajo
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.17f, 0.15f, 1)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.22f, 0.18f, 1));
        if (ImGui::Button(TRS("Quitar sus valores"))) m.ClearOwn();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Vuelve a usar los valores generales (borra los suyos del archivo)."));
        ImGui::SetNextItemWidth(170.0f);
        ImGui::InputTextWithHint("##nomtipo", TRS("nombre del tipo"), s_typeName, sizeof s_typeName);
        ImGui::SameLine();
        if (ImGui::Button(TRS("Guardar como tipo de cuerpo"))) m.Status() = m.SaveAsType(s_typeName);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Guarda este cuerpo como un tipo propio: aparece en las listas de tipos (tambi\xC3\xA9n en el men\xC3\xBA F\xC3\xADsico del juego)."));
        if (m.TypeCount() > m.BuiltinTypes()) {
            ImGui::SameLine(); ImGui::SetNextItemWidth(130.0f);
            if (ImGui::BeginCombo("##borrar", TRS("Borrar tipo..."))) {
                for (int k = m.BuiltinTypes(); k < m.TypeCount(); k++) if (ImGui::Selectable(m.TypeName(k))) m.DeleteUserType(k);
                ImGui::EndCombo();
            }
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.45f, 0.30f, 1)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.56f, 0.37f, 1));
        if (ImGui::Button(TRS("Darle valores propios"))) m.MakeOwn();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Copia los valores generales a este jugador; despu\xC3\xA9s se cambian sin tocar al resto."));
    }
    if (!m.Status().empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.58f, 0.64f, 1)); ImGui::TextWrapped("%s", m.Status().c_str()); ImGui::PopStyleColor(); }
    // el musculo bajo el mouse (o el elegido) late en el modelo del juego
    const int hl = s_hoverRow >= 0 ? s_hoverRow : s_sel;
    if (hl >= 0) m.Highlight(kRows[hl].idx, kRows[hl].comp);
    (void)bold; (void)st;
}

} // namespace fx
