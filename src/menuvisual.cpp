// PES21 FX - (14/11) menu Insert visual (ver menuvisual.h). Agustin: "darle al menu una accesibilidad mayor ... iconos para ciertos
// cambios o una imagen de preview del cambio ... con el sol o el viento mover un icono de sol y que eso se represente con el sol
// experimental ... como si fuera una interfaz visual adaptativa"; "que el menu pueda leer los inputs del control y mostrar unos
// iconos de botones de playstation"; "un joystick imagen con las teclas que son presionadas ... con cierta baja opacidad, no muy
// grande, con animaciones". Todo se dibuja con lineas y triangulos de ImGui (sin imagenes).
#include "menuvisual.h"
#include <imgui_internal.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

namespace fx {
const char* TRS(const char* es);

static constexpr float kPI = 3.14159265f, kDeg = 0.01745329f;
static ImU32 Col(float r, float g, float b, float a) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); }
static ImU32 ColA(ImVec4 c, float a) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a)); }
static ImVec4 Lerp4(ImVec4 a, ImVec4 b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }
static float Sat(float x) { return std::clamp(x, 0.0f, 1.0f); }
static float Smooth(float a, float b, float x) { const float t = Sat((x - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }
// valor que sigue a otro suave (vistas dinamicas); sin animacion salta
static float Follow(ImGuiID id, float target, float speed, bool anim, bool angle = false) {
    float* v = ImGui::GetStateStorage()->GetFloatRef(id, -1.0e9f);
    if (!anim || *v < -1.0e8f) { *v = target; return target; }
    float d = target - *v;
    if (angle) { d = fmodf(d + 540.0f, 360.0f) - 180.0f; }
    *v += d * (1.0f - expf(-ImGui::GetIO().DeltaTime * speed));
    if (angle) *v = fmodf(*v + 360.0f, 360.0f);
    return *v;
}

// ------------------------------------------------------------------------------------------------------------------------------
// iconos
MvIcon MvIconFromName(const char* n) {
    if (!n || !*n) return kMvIcoNone;
    if (!strcmp(n, "sol") || !strcmp(n, "sun")) return kMvIcoSun;
    if (!strcmp(n, "viento") || !strcmp(n, "wind")) return kMvIcoWind;
    return kMvIcoNone;
}
static void SunGlyph(ImDrawList* dl, ImVec2 c, float r, ImVec4 col, float rot, float glow) {
    if (glow > 0.01f) for (int i = 4; i >= 1; i--) dl->AddCircleFilled(c, r * (1.0f + 0.45f * i), ColA(col, 0.06f * glow * (5 - i)), 32);
    const int n = 8;
    for (int i = 0; i < n; i++) {
        const float a = rot + i * (2.0f * kPI / n), ca = cosf(a), sa = sinf(a);
        const float r0 = r * 1.35f, r1 = r * (i % 2 ? 1.75f : 1.95f);
        dl->AddLine(ImVec2(c.x + ca * r0, c.y + sa * r0), ImVec2(c.x + ca * r1, c.y + sa * r1), ColA(col, 1.0f), std::max(1.2f, r * 0.22f));
    }
    dl->AddCircleFilled(c, r, ColA(col, 1.0f), 28);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.28f, c.y - r * 0.30f), r * 0.42f, IM_COL32(255, 255, 255, 110), 20);   // reflejo
}
static void WindGlyph(ImDrawList* dl, ImVec2 c, float s, ImU32 col, float t) {
    // tres lineas con la punta enrulada, la del medio mas larga; con t >= 0 se corren un poco (sopla)
    const float th = std::max(1.3f, s * 0.11f);
    for (int i = 0; i < 3; i++) {
        const float y = c.y + (i - 1) * s * 0.30f;
        const float len = s * (i == 1 ? 0.95f : 0.70f), ph = t >= 0 ? sinf((float)t * 3.0f + i * 1.7f) * s * 0.06f : 0.0f;
        const float x0 = c.x - s * 0.50f + ph, x1 = x0 + len;
        dl->PathLineTo(ImVec2(x0, y));
        dl->PathLineTo(ImVec2(x1, y));
        const float rr = s * (i == 1 ? 0.16f : 0.12f);
        dl->PathArcTo(ImVec2(x1, y - rr), rr, kPI * 0.5f, -kPI * 0.9f, 10);
        dl->PathStroke(col, 0, th);
    }
}
void MvDrawIcon(ImDrawList* dl, ImVec2 c, float size, MvIcon ico, ImU32 col, float t) {
    if (ico == kMvIcoSun) { ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col); SunGlyph(dl, c, size * 0.26f, cv, t >= 0 ? (float)t * 0.6f : 0.0f, 0.0f); }
    else if (ico == kMvIcoWind) WindGlyph(dl, c, size, col, t);
}

// ------------------------------------------------------------------------------------------------------------------------------
// sol
// color del cielo segun la altura del sol: noche azul oscuro, amanecer naranja / rosa, de dia celeste
static void SkyColors(float el, ImVec4& top, ImVec4& hor) {
    const ImVec4 nT(0.03f, 0.05f, 0.12f, 1), nH(0.08f, 0.10f, 0.20f, 1);
    const ImVec4 sT(0.20f, 0.24f, 0.45f, 1), sH(0.98f, 0.55f, 0.30f, 1);
    const ImVec4 dT(0.18f, 0.42f, 0.80f, 1), dH(0.65f, 0.82f, 0.96f, 1);
    if (el < 0.0f) { const float t = Smooth(-8.0f, 0.0f, el); top = Lerp4(nT, sT, t); hor = Lerp4(nH, sH, t); }
    else { const float t = Smooth(0.0f, 22.0f, el); top = Lerp4(sT, dT, t); hor = Lerp4(sH, dH, t); }
}
static ImVec4 SunColor(const MvSunIn& in, float el) {
    ImVec4 c(1.0f, 0.96f, 0.86f, 1);
    const float low = 1.0f - Smooth(0.0f, 20.0f, el);   // el sol bajo se ve naranja
    c = Lerp4(c, ImVec4(1.0f, 0.62f, 0.30f, 1), std::max(low * 0.65f, in.warm));
    c.x *= in.tint[0]; c.y *= in.tint[1]; c.z *= in.tint[2];
    const float m = std::max(c.x, std::max(c.y, c.z)); if (m > 1e-3f) { c.x /= m; c.y /= m; c.z /= m; }
    return c;
}
static void DrawPitchTop(ImDrawList* dl, ImVec2 a, ImVec2 b, float alpha) {
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); const float W = b.x - a.x, H = b.y - a.y;
    // franjas del corte del pasto
    for (int i = 0; i < 10; i++) {
        const float x0 = a.x + W * i / 10.0f, x1 = a.x + W * (i + 1) / 10.0f;
        dl->AddRectFilled(ImVec2(x0, a.y), ImVec2(x1, b.y), i % 2 ? Col(0.16f, 0.40f, 0.21f, 0.92f * alpha) : Col(0.18f, 0.45f, 0.24f, 0.92f * alpha));
    }
    const ImU32 ln = Col(0.92f, 0.96f, 0.92f, 0.65f * alpha);
    dl->AddRect(a, b, ln, 0, 0, 1.0f);
    dl->AddLine(ImVec2(c.x, a.y), ImVec2(c.x, b.y), ln);
    dl->AddCircle(c, H * 9.15f / 68.0f, ln, 24);
    const float aH = H * 40.3f / 68.0f * 0.5f, aW = W * 16.5f / 105.0f;
    dl->AddRect(ImVec2(a.x, c.y - aH), ImVec2(a.x + aW, c.y + aH), ln); dl->AddRect(ImVec2(b.x - aW, c.y - aH), ImVec2(b.x, c.y + aH), ln);
}
// (14/11 c) Agustin: "no me parece practica la vista del sol ... como se plasma y las opciones que se puedan cambiar de manera
// visual". Tablero del sol: arriba, momentos del dia de un clic; a la izquierda la cancha desde arriba con un anillo (arrastrar el sol
// por el anillo = de que lado viene) y la sombra del jugador; al lado, la altura vista de costado (arrastrar el sol por el arco); a la
// derecha, botones con icono para cada opcion (sombras, temperatura con su tira de colores, fuerza, giro, recorrido del dia, nubes,
// color segun la altura, copiar el sol del partido). Todo cambia con un clic o arrastrando.
static float WrapDeg(float a) { a = fmodf(a, 360.0f); return a < 0 ? a + 360.0f : a; }
static ImVec4 KelvinCol(float K) {   // aproximacion del color de un cuerpo negro (2000..12000 K)
    const float t = K / 100.0f; float r, g, b;
    r = t <= 66 ? 1.0f : Sat(1.292936f * powf(t - 60, -0.1332047f));
    g = t <= 66 ? Sat(0.3900816f * logf(t) - 0.6318414f) : Sat(1.1298909f * powf(t - 60, -0.0755148f));
    b = t >= 66 ? 1.0f : t <= 19 ? 0.0f : Sat(0.5432068f * logf(t - 10) - 1.1962541f);
    return ImVec4(r, g, b, 1);
}
// iconos chicos para los botones (todo con lineas)
enum { kSiShadow, kSiTemp, kSiPower, kSiSpin, kSiDay, kSiCloud, kSiHeightCol, kSiCopy };
static void SmallIcon(ImDrawList* dl, ImVec2 c, float s, int ic, ImU32 col, float t) {
    const float th = std::max(1.2f, s * 0.11f);
    switch (ic) {
    case kSiShadow:   // persona y su sombra
        dl->AddCircleFilled(ImVec2(c.x - s * 0.15f, c.y - s * 0.38f), s * 0.13f, col, 10);
        dl->AddLine(ImVec2(c.x - s * 0.15f, c.y - s * 0.22f), ImVec2(c.x - s * 0.15f, c.y + s * 0.18f), col, th * 1.4f);
        dl->AddLine(ImVec2(c.x - s * 0.15f, c.y + s * 0.38f), ImVec2(c.x + s * 0.45f, c.y + s * 0.20f), col & 0x77FFFFFF, th * 2.2f);
        dl->AddLine(ImVec2(c.x - s * 0.15f, c.y + s * 0.18f), ImVec2(c.x - s * 0.15f, c.y + s * 0.38f), col, th * 1.4f); break;
    case kSiTemp:     // termometro
        dl->AddRect(ImVec2(c.x - s * 0.10f, c.y - s * 0.45f), ImVec2(c.x + s * 0.10f, c.y + s * 0.15f), col, s * 0.1f, 0, th);
        dl->AddCircleFilled(ImVec2(c.x, c.y + s * 0.28f), s * 0.18f, col, 12); dl->AddLine(ImVec2(c.x, c.y - s * 0.2f), ImVec2(c.x, c.y + s * 0.2f), col, th * 1.3f); break;
    case kSiPower:    // sol chico con rayos largos
        dl->AddCircleFilled(c, s * 0.18f, col, 14);
        for (int i = 0; i < 8; i++) { const float a = i * kPI / 4 + t * 0.4f; dl->AddLine(ImVec2(c.x + cosf(a) * s * 0.28f, c.y + sinf(a) * s * 0.28f), ImVec2(c.x + cosf(a) * s * 0.45f, c.y + sinf(a) * s * 0.45f), col, th); } break;
    case kSiSpin: {   // flecha que gira
        const float a0 = t * 2.0f; dl->PathArcTo(c, s * 0.34f, a0, a0 + kPI * 1.5f, 16); dl->PathStroke(col, 0, th * 1.2f);
        const ImVec2 e(c.x + cosf(a0 + kPI * 1.5f) * s * 0.34f, c.y + sinf(a0 + kPI * 1.5f) * s * 0.34f); const float ta = a0 + kPI * 1.5f + kPI * 0.5f;
        dl->AddTriangleFilled(ImVec2(e.x + cosf(ta) * s * 0.16f, e.y + sinf(ta) * s * 0.16f), ImVec2(e.x + cosf(ta + 2.3f) * s * 0.13f, e.y + sinf(ta + 2.3f) * s * 0.13f), ImVec2(e.x + cosf(ta - 2.3f) * s * 0.13f, e.y + sinf(ta - 2.3f) * s * 0.13f), col); } break;
    case kSiDay: {    // arco del dia con el sol encima
        dl->AddLine(ImVec2(c.x - s * 0.45f, c.y + s * 0.25f), ImVec2(c.x + s * 0.45f, c.y + s * 0.25f), col, th);
        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.25f), s * 0.38f, kPI, kPI * 2.0f, 16); dl->PathStroke(col & 0x88FFFFFF, 0, th);
        const float a = kPI + fmodf(t * 0.8f, kPI); dl->AddCircleFilled(ImVec2(c.x + cosf(a) * s * 0.38f, c.y + s * 0.25f + sinf(a) * s * 0.38f), s * 0.11f, col, 10); } break;
    case kSiCloud: {
        const float dx = sinf(t * 0.8f) * s * 0.05f;
        dl->AddCircleFilled(ImVec2(c.x - s * 0.18f + dx, c.y + s * 0.05f), s * 0.18f, col, 14); dl->AddCircleFilled(ImVec2(c.x + s * 0.05f + dx, c.y - s * 0.06f), s * 0.24f, col, 14);
        dl->AddCircleFilled(ImVec2(c.x + s * 0.27f + dx, c.y + s * 0.07f), s * 0.16f, col, 14); dl->AddRectFilled(ImVec2(c.x - s * 0.18f + dx, c.y + s * 0.05f), ImVec2(c.x + s * 0.27f + dx, c.y + s * 0.23f), col); } break;
    case kSiHeightCol:   // sol bajo naranja sobre la linea
        dl->AddLine(ImVec2(c.x - s * 0.45f, c.y + s * 0.15f), ImVec2(c.x + s * 0.45f, c.y + s * 0.15f), col, th);
        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.15f), s * 0.22f, kPI, kPI * 2.0f, 12); dl->PathFillConvex(IM_COL32(255, 150, 70, (col >> 24) & 255)); break;
    case kSiCopy:     // dos hojas
        dl->AddRect(ImVec2(c.x - s * 0.35f, c.y - s * 0.35f), ImVec2(c.x + s * 0.12f, c.y + s * 0.15f), col, 2.0f, 0, th);
        dl->AddRectFilled(ImVec2(c.x - s * 0.12f, c.y - s * 0.12f), ImVec2(c.x + s * 0.35f, c.y + s * 0.38f), col, 2.0f); break;
    }
}
// boton con icono: fondo de vidrio, se ilumina al pasar, verde cuando esta prendido; 'sub' = el valor actual debajo del nombre
static bool Chip(const char* id, int ic, const char* label, const char* sub, bool on, float w, float k, double now, bool anim) {
    const float h = 44.0f * k;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool r = ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    float* ha = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetItemID(), 0.0f);
    *ha = anim ? std::clamp(*ha + (hov ? 1.0f : -1.0f) * ImGui::GetIO().DeltaTime * 8.0f, 0.0f, 1.0f) : (hov ? 1.0f : 0.0f);
    float* oa = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetItemID() + 1, on ? 1.0f : 0.0f);
    *oa = anim ? std::clamp(*oa + (on ? 1.0f : -1.0f) * ImGui::GetIO().DeltaTime * 7.0f, 0.0f, 1.0f) : (on ? 1.0f : 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(p.x + w, p.y + h); const float rr = 9.0f * k, push = act ? 1.0f * k : 0.0f;
    dl->AddRectFilled(ImVec2(p.x, p.y + push), ImVec2(b.x, b.y + push), ColA(Lerp4(ImVec4(1, 1, 1, 0.05f + 0.05f * *ha), ImVec4(0.22f, 0.72f, 0.48f, 0.30f + 0.08f * *ha), *oa), 1.0f), rr);
    dl->AddRectFilledMultiColor(ImVec2(p.x + 2, p.y + 2 + push), ImVec2(b.x - 2, p.y + h * 0.45f + push), IM_COL32(255, 255, 255, (int)(14 + 14 * *ha)), IM_COL32(255, 255, 255, (int)(14 + 14 * *ha)), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    dl->AddRect(ImVec2(p.x, p.y + push), ImVec2(b.x, b.y + push), ColA(Lerp4(ImVec4(1, 1, 1, 0.14f + 0.2f * *ha), ImVec4(0.45f, 0.92f, 0.66f, 0.75f), *oa), 1.0f), rr, 0, 1.0f);
    const ImU32 icc = ColA(Lerp4(ImVec4(0.78f, 0.81f, 0.86f, 1), ImVec4(0.55f, 1.0f, 0.75f, 1), *oa), 1.0f);
    SmallIcon(dl, ImVec2(p.x + 22.0f * k, p.y + h * 0.5f + push), 26.0f * k, ic, icc, anim && (on || hov) ? (float)fmod(now, 3600.0) : 0.0f);
    const float tx = p.x + 42.0f * k;
    dl->PushClipRect(ImVec2(tx, p.y), ImVec2(b.x - 4.0f * k, b.y), true);
    dl->AddText(ImVec2(tx, p.y + 5.0f * k + push), Col(0.93f, 0.94f, 0.96f, 1), label);
    if (sub && *sub) dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.86f, ImVec2(tx, p.y + h * 0.52f + push), ColA(Lerp4(ImVec4(0.60f, 0.64f, 0.70f, 1), ImVec4(0.55f, 0.95f, 0.72f, 1), *oa), 1.0f), sub);
    dl->PopClipRect();
    return r;
}
// tira de valores: degrade (o lleno) con la perilla; arrastrar o clic para elegir; rueda ajusta fino
static bool Strip(const char* id, float* v, float lo, float hi, float w, float k, const ImVec4* grad, int nGrad, ImVec4 fill, bool anim) {
    const float h = 18.0f * k;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive(); bool ch = false;
    if (act) { const float t = Sat((ImGui::GetIO().MousePos.x - p.x) / w); const float nv = lo + (hi - lo) * t; if (nv != *v) { *v = nv; ch = true; } }
    if (hov && ImGui::GetIO().MouseWheel != 0.0f) { ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); *v = std::clamp(*v + ImGui::GetIO().MouseWheel * (hi - lo) * 0.02f, lo, hi); ch = true; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y0 = p.y + h * 0.30f, y1 = p.y + h * 0.70f, t = Sat((*v - lo) / (hi - lo));
    if (grad && nGrad > 1) for (int i = 0; i < nGrad - 1; i++) {
        const float x0 = p.x + w * i / (nGrad - 1), x1 = p.x + w * (i + 1) / (nGrad - 1);
        dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x1, y1), ImGui::GetColorU32(grad[i]), ImGui::GetColorU32(grad[i + 1]), ImGui::GetColorU32(grad[i + 1]), ImGui::GetColorU32(grad[i]));
    } else { dl->AddRectFilled(ImVec2(p.x, y0), ImVec2(p.x + w, y1), Col(0, 0, 0, 0.35f), (y1 - y0) * 0.5f); dl->AddRectFilled(ImVec2(p.x, y0), ImVec2(p.x + w * t, y1), ImGui::GetColorU32(fill), (y1 - y0) * 0.5f); }
    const float kx = p.x + w * t, r = (6.0f + (hov || act ? 1.5f : 0.0f)) * k;
    dl->AddCircleFilled(ImVec2(kx, p.y + h * 0.5f + 1.0f * k), r, Col(0, 0, 0, 0.4f), 16);
    dl->AddCircleFilled(ImVec2(kx, p.y + h * 0.5f), r, Col(0.95f, 0.96f, 0.98f, 1), 16);
    (void)anim;
    return ch;
}
bool MvSunWidget(MvSunIn& in) {
    if (!in.az || !in.el || !in.on) return false;
    const float k = in.k; bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool on = *in.on > 0.5f;
    float wantAz = in.relative && in.realOk ? *in.az + in.realAz : *in.az, wantEl = *in.el;
    if (on && in.finalOk) { wantAz = in.finalAz; wantEl = in.finalEl; }
    else if (!on && in.realOk) { wantAz = in.realAz; wantEl = in.realEl; }
    wantAz = WrapDeg(wantAz);
    auto setAz = [&](float az) { if (!on) *in.on = 1.0f; *in.az = roundf(in.relative && in.realOk ? WrapDeg(az - in.realAz) : WrapDeg(az)); changed = true; };
    auto setEl = [&](float el) { if (!on) *in.on = 1.0f; *in.el = roundf(std::clamp(el, -5.0f, 90.0f)); changed = true; };
    const double t = in.now;
    // --- momentos del dia de un clic ---
    {
        struct Pre { const char* n; float az, el, K, hc; };
        static const Pre kPre[] = { { "Manana", 100, 22, 4300, 0.6f }, { "Mediodia", 160, 68, 5800, 0.0f }, { "Tarde", 230, 38, 5000, 0.3f },
                                    { "Atardecer", 265, 7, 3000, 0.9f }, { "Contraluz", 0, 14, 4200, 0.6f }, { "Noche", 200, -5, 0, 0.0f } };
        const float bw = (avail - 5 * 6.0f * k) / 6.0f;
        for (int i = 0; i < 6; i++) {
            if (i) ImGui::SameLine(0, 6.0f * k);
            const Pre& q = kPre[i];
            const bool cur = on && fabsf(WrapDeg(*in.az - (in.relative && in.realOk ? WrapDeg(q.az - in.realAz) : q.az) + 180.0f) - 180.0f) < 1.0f && fabsf(*in.el - q.el) < 1.0f;
            ImGui::PushID(i);
            const ImVec2 p = ImGui::GetCursorScreenPos(); const float h = 30.0f * k;
            if (ImGui::InvisibleButton("##pre", ImVec2(bw, h))) {
                setAz(q.az); setEl(q.el);
                if (in.kelvin) *in.kelvin = q.K;
                if (in.heightCol) *in.heightCol = q.hc;
                if (in.follow) *in.follow = 1.0f;
            }
            const bool hv = ImGui::IsItemHovered();
            ImVec4 top, hor; SkyColors(q.el, top, hor);
            dl->AddRectFilledMultiColor(p, ImVec2(p.x + bw, p.y + h), ImGui::GetColorU32(top), ImGui::GetColorU32(top), ImGui::GetColorU32(hor), ImGui::GetColorU32(hor));
            dl->AddRect(p, ImVec2(p.x + bw, p.y + h), cur ? Col(0.45f, 0.95f, 0.66f, 1) : Col(1, 1, 1, hv ? 0.5f : 0.18f), 7.0f * k, 0, cur ? 2.0f * k : 1.0f);
            const char* nm = TRS(q.n); const ImVec2 ts = ImGui::CalcTextSize(nm);
            dl->AddText(ImVec2(p.x + (bw - ts.x) * 0.5f + 1, p.y + (h - ts.y) * 0.5f + 1), Col(0, 0, 0, 0.6f), nm);
            dl->AddText(ImVec2(p.x + (bw - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), IM_COL32_WHITE, nm);
            if (hv) ImGui::SetTooltip(TRS("Pone el sol libre como a la %s (lado, altura y color de la luz). Despues se ajusta abajo."), nm);
            ImGui::PopID();
        }
    }
    ImGui::Dummy(ImVec2(0, 4.0f * k));
    const bool wide = avail >= 600.0f * k;
    const float D = std::clamp(avail * (wide ? 0.30f : 0.48f), 150.0f * k, 210.0f * k);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    // --- 1) de que lado: la cancha desde arriba con el anillo ---
    const ImGuiID wid = ImGui::GetID("##soldial");
    const ImVec2 c(p0.x + D * 0.5f, p0.y + D * 0.5f); const float R = D * 0.5f - 12.0f * k;
    ImGui::InvisibleButton("##soldial", ImVec2(D, D));
    const bool hovD = ImGui::IsItemHovered(), actD = ImGui::IsItemActive();
    if (actD) { const ImVec2 m = ImGui::GetIO().MousePos; setAz(atan2f(m.x - c.x, m.y - c.y) / kDeg); wantAz = WrapDeg(atan2f(m.x - c.x, m.y - c.y) / kDeg); }
    if (hovD && ImGui::GetIO().MouseWheel != 0.0f) { ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); setAz(*in.az + (in.relative && in.realOk ? in.realAz : 0.0f) + ImGui::GetIO().MouseWheel * 5.0f); }
    const float shAz = Follow(wid + 1, wantAz, 14.0f, in.anim && !actD, true);
    ImVec4 top, hor; SkyColors(wantEl, top, hor);
    const ImVec4 sc = SunColor(in, wantEl);
    {
        dl->AddCircleFilled(c, R + 10.0f * k, Col(0, 0, 0, 0.25f), 64);
        dl->AddCircle(c, R, Col(1, 1, 1, hovD || actD ? 0.45f : 0.22f), 64, 10.0f * k);   // el anillo (riel del sol)
        for (int i = 0; i < 24; i++) { const float a = i * kPI / 12; const float l = i % 6 == 0 ? 6.0f : 3.0f; dl->AddLine(ImVec2(c.x + sinf(a) * (R - l * k), c.y + cosf(a) * (R - l * k)), ImVec2(c.x + sinf(a) * (R + l * k), c.y + cosf(a) * (R + l * k)), Col(1, 1, 1, 0.35f), 1.0f); }
        const float pw = R * 1.05f, ph = pw * 68.0f / 105.0f;
        DrawPitchTop(dl, ImVec2(c.x - pw * 0.5f, c.y - ph * 0.5f), ImVec2(c.x + pw * 0.5f, c.y + ph * 0.5f), 1.0f);
        // sombra del jugador (hacia el lado contrario al sol, larga con el sol bajo)
        if (wantEl > 0.5f) {
            const float len = std::min(R * 0.75f, (R * 0.10f) / tanf(std::max(wantEl, 4.0f) * kDeg));
            const float sx = -sinf(shAz * kDeg), sy = -cosf(shAz * kDeg); const ImVec2 e(c.x + sx * len, c.y + sy * len); const float wd = 3.4f * k;
            dl->AddQuadFilled(ImVec2(c.x - sy * wd, c.y + sx * wd), ImVec2(c.x + sy * wd, c.y - sx * wd), ImVec2(e.x + sy * wd * 0.5f, e.y - sx * wd * 0.5f), ImVec2(e.x - sy * wd * 0.5f, e.y + sx * wd * 0.5f), Col(0, 0, 0, 0.55f));
        }
        dl->AddCircleFilled(c, 4.0f * k, Col(0.95f, 0.30f, 0.30f, 1), 12); dl->AddCircle(c, 4.0f * k, IM_COL32_WHITE, 12, 1.2f);
        // la camara de TV (abajo, del lado -Z) para orientarse
        dl->AddText(ImVec2(c.x - ImGui::CalcTextSize(TRS("TV")).x * 0.5f, c.y - R - 7.0f * k - ImGui::GetFontSize()), Col(1, 1, 1, 0.55f), TRS("TV"));
        if (on && in.realOk) { const ImVec2 rp(c.x + sinf(in.realAz * kDeg) * R, c.y + cosf(in.realAz * kDeg) * R); dl->AddCircle(rp, 6.0f * k, Col(1, 1, 1, 0.8f), 16, 1.5f * k); }
        const ImVec2 sp(c.x + sinf(shAz * kDeg) * R, c.y + cosf(shAz * kDeg) * R);
        const float pulse = in.anim ? 0.5f + 0.5f * sinf((float)t * 2.2f) : 0.5f;
        SunGlyph(dl, sp, (8.0f + (hovD || actD ? 1.5f : 0.0f)) * k, on ? sc : ImVec4(sc.x, sc.y, sc.z, 0.6f), in.anim ? (float)fmod(t, 3600.0) * 0.5f : 0.0f, on ? 0.6f + 0.4f * pulse : 0.25f);
        if (hovD && !actD) ImGui::SetTooltip("%s", TRS("De que lado viene el sol: arrastralo por el anillo (o la rueda del mouse). La raya negra es la sombra del jugador en la cancha. El circulo blanco es el sol del partido. TV = lado de la camara."));
    }
    // --- 2) la altura: vista de costado, el sol sobre un arco ---
    const float AW = std::clamp(D * 0.72f, 110.0f * k, 150.0f * k);
    ImGui::SetCursorScreenPos(ImVec2(p0.x + D + 8.0f * k, p0.y));
    const ImVec2 a0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##solalt", ImVec2(AW, D));
    const bool hovA = ImGui::IsItemHovered(), actA = ImGui::IsItemActive();
    const ImVec2 base(a0.x + 12.0f * k, a0.y + D - 30.0f * k); const float AR = std::min(AW - 24.0f * k, D - 46.0f * k);
    if (actA) { const ImVec2 m = ImGui::GetIO().MousePos; const float el = atan2f(base.y - m.y, m.x - base.x) / kDeg; setEl(el); wantEl = *in.el; }
    if (hovA && ImGui::GetIO().MouseWheel != 0.0f) { ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); setEl(*in.el + ImGui::GetIO().MouseWheel * (ImGui::GetIO().KeyShift ? 0.5f : 2.0f)); }
    const float shEl = Follow(wid + 2, wantEl, 14.0f, in.anim && !actA);
    {
        SkyColors(shEl, top, hor);
        dl->AddRectFilledMultiColor(a0, ImVec2(a0.x + AW, base.y), ImGui::GetColorU32(top), ImGui::GetColorU32(top), ImGui::GetColorU32(hor), ImGui::GetColorU32(hor));
        dl->AddRectFilled(ImVec2(a0.x, base.y), ImVec2(a0.x + AW, a0.y + D), Col(0.15f, 0.38f, 0.19f, 1));
        dl->AddRect(a0, ImVec2(a0.x + AW, a0.y + D), Col(1, 1, 1, hovA || actA ? 0.45f : 0.18f), 9.0f * k, 0, 1.0f);
        dl->PathArcTo(base, AR, -kPI * 0.5f, 0.0f, 24); dl->PathStroke(Col(1, 1, 1, 0.30f), 0, 2.0f * k);
        for (int e = 0; e <= 90; e += 30) { const float a = -e * kDeg; dl->AddLine(ImVec2(base.x + cosf(a) * (AR - 5 * k), base.y + sinf(a) * (AR - 5 * k)), ImVec2(base.x + cosf(a) * (AR + 5 * k), base.y + sinf(a) * (AR + 5 * k)), Col(1, 1, 1, 0.5f), 1.0f); }
        // jugador de costado y su sombra en el piso (larga con el sol bajo)
        const float ph = 26.0f * k;
        // el jugador va en el medio del piso (no en el centro del arco) para que la sombra larga del sol bajo entre en el cuadro
        const ImVec2 pl(a0.x + AW * 0.62f, base.y);
        dl->PushClipRect(a0, ImVec2(a0.x + AW, a0.y + D), true);
        if (shEl > 0.5f) { const float len = std::min(AW * 0.58f, ph / tanf(std::max(shEl, 3.0f) * kDeg)); dl->AddLine(ImVec2(pl.x, pl.y + 1.5f * k), ImVec2(pl.x - len, pl.y + 1.5f * k), Col(0, 0, 0, 0.55f), 4.0f * k); }
        dl->AddLine(ImVec2(pl.x, pl.y), ImVec2(pl.x, pl.y - ph * 0.62f), Col(0.95f, 0.95f, 0.98f, 1), 4.0f * k);
        dl->AddCircleFilled(ImVec2(pl.x, pl.y - ph * 0.62f - 5.0f * k), 4.5f * k, Col(0.95f, 0.95f, 0.98f, 1), 12);
        dl->PopClipRect();
        const ImVec2 sp(base.x + cosf(shEl * kDeg) * AR, base.y - sinf(shEl * kDeg) * AR);
        SunGlyph(dl, sp, (8.0f + (hovA || actA ? 1.5f : 0.0f)) * k, on ? sc : ImVec4(sc.x, sc.y, sc.z, 0.6f), in.anim ? (float)fmod(t, 3600.0) * 0.5f : 0.0f, on ? 0.8f : 0.25f);
        char b[32]; snprintf(b, sizeof b, TRS("Altura %.0f"), wantEl);
        dl->AddText(ImVec2(a0.x + 7.0f * k, a0.y + D - ImGui::GetFontSize() - 5.0f * k), IM_COL32_WHITE, b);
        if (hovA && !actA) ImGui::SetTooltip("%s", TRS("La altura del sol, vista de costado: arrastralo por el arco (o la rueda del mouse). Abajo = atardecer y sombras largas; arriba = mediodia."));
    }
    // --- 3) las demas opciones con botones de icono ---
    const float bx = wide ? p0.x + D + AW + 18.0f * k : p0.x;
    const float bwAll = wide ? avail - (D + AW + 18.0f * k) : avail;
    if (wide) ImGui::SetCursorScreenPos(ImVec2(bx, p0.y)); else ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + D + 8.0f * k));
    ImGui::BeginGroup();
    const float cw = (bwAll - 6.0f * k) * 0.5f;
    char sub[64];
    auto cyc = [&](float* v, const float* vals, int n) { int cur = 0; for (int i = 1; i < n; i++) if (fabsf(vals[i] - *v) < fabsf(vals[cur] - *v)) cur = i; *v = vals[(cur + 1) % n]; changed = true; };
    if (in.follow) {
        const bool f = *in.follow > 0.5f;
        if (Chip("##ch_sh", kSiShadow, TRS("Sombras siguen"), TRS(f ? "Si: el motor mueve el sol" : "No: solo la luz"), f, cw, k, t, in.anim)) { *in.follow = f ? 0.0f : 1.0f; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Si: las sombras de jugadores, pelota y estadio van con el sol libre. No: solo cambia la luz."));
        ImGui::SameLine(0, 6.0f * k);
    }
    if (in.copy) {
        if (Chip("##ch_cp", kSiCopy, TRS("Copiar del partido"), in.realOk ? TRS("pone el sol real") : TRS("sin dato"), false, cw, k, t, in.anim)) { *in.copy = 1.0f; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Pone el sol libre donde esta el sol del partido ahora, para partir de ahi."));
    }
    if (in.spin) {
        static const float kV[3] = { 0.0f, 4.0f, 15.0f }; static const char* kN[3] = { "Quieto", "Lento", "Rapido" };
        int cur = 0; for (int i = 1; i < 3; i++) if (fabsf(kV[i] - *in.spin) < fabsf(kV[cur] - *in.spin)) cur = i;
        if (Chip("##ch_sp", kSiSpin, TRS("Giro"), TRS(kN[cur]), cur > 0, cw, k, t, in.anim)) cyc(in.spin, kV, 3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("El sol da vueltas solo alrededor de la cancha. Clic: quieto / lento / rapido."));
        ImGui::SameLine(0, 6.0f * k);
    }
    if (in.dayRun) {
        static const float kV[3] = { 0.0f, 6.0f, 18.0f }; static const char* kN[3] = { "No", "Un dia en 30 s", "Un dia en 10 s" };
        int cur = 0; for (int i = 1; i < 3; i++) if (fabsf(kV[i] - *in.dayRun) < fabsf(kV[cur] - *in.dayRun)) cur = i;
        if (Chip("##ch_dy", kSiDay, TRS("Recorrido del dia"), TRS(kN[cur]), cur > 0, cw, k, t, in.anim)) cyc(in.dayRun, kV, 3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("El sol sale, sube hasta la altura elegida y se pone, una y otra vez. Clic para cambiar la velocidad."));
    }
    if (in.clouds) {
        static const float kV[3] = { 0.0f, 0.4f, 0.8f }; static const char* kN[3] = { "Cielo limpio", "Algunas", "Muchas" };
        int cur = 0; for (int i = 1; i < 3; i++) if (fabsf(kV[i] - *in.clouds) < fabsf(kV[cur] - *in.clouds)) cur = i;
        if (Chip("##ch_cl", kSiCloud, TRS("Nubes que pasan"), TRS(kN[cur]), cur > 0, cw, k, t, in.anim)) cyc(in.clouds, kV, 3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Cada tanto una nube tapa el sol y la luz baja suave. Clic: limpio / algunas / muchas."));
        ImGui::SameLine(0, 6.0f * k);
    }
    if (in.heightCol) {
        const bool hc = *in.heightCol > 0.05f;
        if (Chip("##ch_hc", kSiHeightCol, TRS("Naranja al bajar"), TRS(hc ? "Si" : "No"), hc, cw, k, t, in.anim)) { *in.heightCol = hc ? 0.0f : 0.7f; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Con el sol bajo la luz se pone naranja y mas debil, como al atardecer."));
    }
    // temperatura: tira de colores (Juego, vela, atardecer, mediodia, nublado)
    if (in.kelvin) {
        const ImVec2 lp = ImGui::GetCursorScreenPos();
        snprintf(sub, sizeof sub, *in.kelvin < 1000.0f ? "%s" : "%s  %.0f K", TRS(*in.kelvin < 1000.0f ? "Color: el del juego" : "Color de la luz"), *in.kelvin);
        SmallIcon(dl, ImVec2(lp.x + 9.0f * k, lp.y + ImGui::GetFontSize() * 0.5f), 18.0f * k, kSiTemp, Col(0.85f, 0.87f, 0.9f, 1), 0);
        dl->AddText(ImVec2(lp.x + 22.0f * k, lp.y), Col(0.85f, 0.87f, 0.9f, 1), sub); ImGui::Dummy(ImVec2(1, ImGui::GetFontSize() + 2.0f * k));
        ImVec4 g[6] = { ImVec4(0.55f, 0.57f, 0.62f, 1), KelvinCol(2000), KelvinCol(3500), KelvinCol(5500), KelvinCol(8000), KelvinCol(12000) };
        float u = *in.kelvin < 1000.0f ? 0.0f : 0.2f + 0.8f * Sat((*in.kelvin - 2000.0f) / 10000.0f);
        if (Strip("##k", &u, 0.0f, 1.0f, bwAll, k, g, 6, ImVec4(), in.anim)) { *in.kelvin = u < 0.1f ? 0.0f : roundf((2000.0f + 10000.0f * Sat((u - 0.2f) / 0.8f)) / 50.0f) * 50.0f; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Izquierda (gris) = el color del juego. Despues: vela, atardecer, mediodia, nublado / frio."));
    }
    if (in.intensityP) {
        const ImVec2 lp = ImGui::GetCursorScreenPos();
        snprintf(sub, sizeof sub, TRS("Fuerza del sol  %.2f"), *in.intensityP);
        SmallIcon(dl, ImVec2(lp.x + 9.0f * k, lp.y + ImGui::GetFontSize() * 0.5f), 18.0f * k, kSiPower, Col(0.85f, 0.87f, 0.9f, 1), 0);
        dl->AddText(ImVec2(lp.x + 22.0f * k, lp.y), Col(0.85f, 0.87f, 0.9f, 1), sub); ImGui::Dummy(ImVec2(1, ImGui::GetFontSize() + 2.0f * k));
        if (Strip("##int", in.intensityP, 0.0f, 3.0f, bwAll, k, nullptr, 0, ImVec4(1.0f, 0.80f, 0.35f, 1), in.anim)) changed = true;
    }
    ImGui::EndGroup();
    const float yEnd = std::max(ImGui::GetItemRectMax().y, p0.y + D);
    ImGui::SetCursorScreenPos(ImVec2(p0.x, yEnd + 4.0f * k)); ImGui::Dummy(ImVec2(avail, 1.0f));
    return changed;
}

// ------------------------------------------------------------------------------------------------------------------------------
// viento
struct WindDot { float x, y, life, seed; };
bool MvWindWidget(MvWindIn& in) {
    const float k = in.k; bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = std::min(ImGui::GetContentRegionAvail().x, 340.0f * k), H = W * 68.0f / 105.0f, gd = W * 2.0f / 105.0f;
    const ImVec2 o = ImGui::GetCursorScreenPos(), a(o.x + gd, o.y), b(a.x + W, a.y + H), c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const ImGuiID wid = ImGui::GetID("##vientocancha");
    ImGui::InvisibleButton("##vientocancha", ImVec2(W + 2 * gd, H));
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const bool fixed = in.fixed && *in.fixed > 0.5f;
    float dir = in.live ? in.dirDeg : (in.fixedDeg ? *in.fixedDeg : 0.0f);
    if (act && in.fixed && in.fixedDeg) {   // arrastrar: el rumbo fijo apunta hacia el mouse
        const ImVec2 m = ImGui::GetIO().MousePos; const float dx = m.x - c.x, dy = m.y - c.y;
        if (dx * dx + dy * dy > 16.0f * k * k) {
            float d = atan2f(dy, dx) / kDeg; if (d < 0) d += 360.0f;
            *in.fixed = 1.0f; *in.fixedDeg = roundf(d); dir = *in.fixedDeg; changed = true;
        }
    }
    if (hov && in.strength && ImGui::GetIO().MouseWheel != 0.0f) {   // rueda: la fuerza general
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        *in.strength = std::clamp(*in.strength + ImGui::GetIO().MouseWheel * 0.05f, 0.0f, 3.0f); changed = true;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && in.fixed) { *in.fixed = 0.0f; changed = true; }   // clic derecho: rumbo del partido
    if (fixed && in.fixedDeg && !in.live) dir = *in.fixedDeg;
    const float shDir = Follow(wid + 1, dir, 10.0f, in.anim && !act, true);
    const float speed = in.live ? in.speed : (in.strength ? 2.0f * *in.strength : 2.0f);
    const float shSpd = Follow(wid + 2, speed, 4.0f, in.anim);
    DrawPitchTop(dl, a, b, 1.0f);
    const ImU32 ln = Col(0.92f, 0.96f, 0.92f, 0.65f);
    const float ga = H * 7.32f / 68.0f * 0.5f;
    dl->AddRect(ImVec2(a.x - gd, c.y - ga), ImVec2(a.x, c.y + ga), ln); dl->AddRect(ImVec2(b.x, c.y - ga), ImVec2(b.x + gd, c.y + ga), ln);
    // particulas que vuelan con el viento (largo y velocidad segun los m/s, mas visibles en la racha)
    const float dx = cosf(shDir * kDeg), dy = sinf(shDir * kDeg);
    static WindDot s_dots[90]; static bool s_init = false;
    if (!s_init) { unsigned s = 12345u; for (auto& d : s_dots) { s = s * 1664525u + 1013904223u; d.x = (s >> 8) / 16777216.0f; s = s * 1664525u + 1013904223u; d.y = (s >> 8) / 16777216.0f; s = s * 1664525u + 1013904223u; d.life = (s >> 8) / 16777216.0f; d.seed = d.x * 7.0f + d.y * 3.0f; } s_init = true; }
    const int nDots = std::clamp((int)(20 + shSpd * 7.0f), 20, 90);
    const float dt = in.anim ? std::min(ImGui::GetIO().DeltaTime, 0.05f) : 0.0f;
    const float v = (0.04f + shSpd * 0.03f) * (1.0f + 0.6f * in.gust);
    dl->PushClipRect(a, b, true);
    for (int i = 0; i < nDots; i++) {
        WindDot& d = s_dots[i];
        const float wob = sinf((float)in.now * (2.0f + in.turb * 4.0f) + d.seed) * 0.15f * (0.3f + in.turb);
        d.x += (dx - dy * wob) * v * dt * 105.0f / 105.0f; d.y += (dy + dx * wob) * v * dt * 105.0f / 68.0f;
        d.life += dt * 0.5f;
        if (d.x < -0.05f || d.x > 1.05f || d.y < -0.05f || d.y > 1.05f || d.life > 1.0f) {   // vuelve a nacer del lado de donde viene
            unsigned s = (unsigned)(d.seed * 1000.0f + in.now * 977.0f); s = s * 1664525u + 1013904223u;
            const float r1 = (s >> 8) / 16777216.0f; s = s * 1664525u + 1013904223u; const float r2 = (s >> 8) / 16777216.0f;
            d.x = r1; d.y = r2; d.life = 0.0f;
            if (fabsf(dx) > fabsf(dy)) d.x = dx > 0 ? -0.02f + r2 * 0.3f : 1.02f - r2 * 0.3f; else d.y = dy > 0 ? -0.02f + r1 * 0.3f : 1.02f - r1 * 0.3f;
        }
        const ImVec2 p(a.x + d.x * W, a.y + d.y * H);
        const float tail = (6.0f + shSpd * 2.2f) * k * (0.7f + 0.6f * in.gust);
        const float al = 0.55f * sinf(Sat(d.life) * kPI) * Sat(0.25f + shSpd / 6.0f);
        dl->AddLine(p, ImVec2(p.x - dx * tail, p.y - dy * tail), Col(1, 1, 1, al), 1.4f * k);
    }
    dl->PopClipRect();
    // flechas: la de dentro de 2 s (clara) y la de ahora (amarilla); con el mouse encima, la que se arrastra
    auto arrow = [&](float deg, float len, ImU32 col, float th) {
        const float r = deg * kDeg, ax = cosf(r), ay = sinf(r);
        const ImVec2 p0(c.x - ax * len * 0.5f, c.y - ay * len * 0.5f), p1(c.x + ax * len * 0.5f, c.y + ay * len * 0.5f);
        dl->AddLine(p0, p1, col, th);
        const float hs = 9.0f * k; const ImVec2 l(p1.x - ax * hs - ay * hs * 0.6f, p1.y - ay * hs + ax * hs * 0.6f), rr(p1.x - ax * hs + ay * hs * 0.6f, p1.y - ay * hs - ax * hs * 0.6f);
        dl->AddTriangleFilled(p1, l, rr, col);
        return p1;
    };
    const float L0 = H * 0.35f, L1 = H * 0.9f;
    if (in.live) arrow(in.soonDirDeg, L0 + (L1 - L0) * Sat(in.soonSpeed / 12.0f), Col(1, 1, 1, 0.28f), 2.0f * k);
    const float pulse = in.anim ? 0.85f + 0.15f * sinf((float)in.now * 3.0f) : 1.0f;
    const ImVec2 tip = arrow(shDir, (L0 + (L1 - L0) * Sat(shSpd / 12.0f)) * (act ? 1.05f : 1.0f), Col(1.0f, 0.92f, 0.47f, 0.92f * pulse), 3.0f * k);
    if (hov || act) dl->AddCircle(tip, 7.0f * k, Col(1.0f, 0.92f, 0.47f, 0.8f), 16, 1.5f * k);
    if (fixed) dl->AddText(ImVec2(a.x + 5 * k, a.y + 3 * k), Col(1, 1, 1, 0.8f), TRS("Rumbo fijo"));
    if (hov && !act) ImGui::SetTooltip("%s", TRS("Arrastra para fijar hacia donde sopla (pasa el Rumbo a Fijo). Clic derecho: volver al rumbo del partido. Rueda del mouse: Fuerza general."));
    return changed;
}

// ------------------------------------------------------------------------------------------------------------------------------
// mando
#ifdef _WIN32
typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XPadG;
typedef struct { DWORD dwPacketNumber; XPadG Gamepad; } XStateG;
typedef DWORD(WINAPI* PFN_XGS)(DWORD, XStateG*);
typedef MMRESULT(WINAPI* PFN_JGPE)(UINT, LPJOYINFOEX);
typedef MMRESULT(WINAPI* PFN_JGDC)(UINT, LPJOYCAPSW, UINT);
static PFN_XGS s_xgs = nullptr; static PFN_JGPE s_jgpe = nullptr; static PFN_JGDC s_jgdc = nullptr; static bool s_loaded = false;
static int s_joy = -1; static JOYCAPSW s_caps = {}; static DWORD s_scanT = 0; static DWORD s_xFailT = 0;
static float Ax(DWORD v, UINT lo, UINT hi) { if (hi <= lo) return 0.0f; const float f = ((float)v - lo) / (float)(hi - lo) * 2.0f - 1.0f; return fabsf(f) < 0.12f ? 0.0f : std::clamp(f, -1.0f, 1.0f); }
static float Dz(SHORT v) { const float f = v / 32767.0f; return fabsf(f) < 0.15f ? 0.0f : std::clamp(f, -1.0f, 1.0f); }
#endif
bool MvPadPoll(MvPad& p) {
    p = MvPad();
#ifdef _WIN32
    if (!s_loaded) {
        s_loaded = true;
        HMODULE x = LoadLibraryW(L"xinput1_4.dll"); if (!x) x = LoadLibraryW(L"xinput1_3.dll"); if (!x) x = LoadLibraryW(L"xinput9_1_0.dll");
        if (x) s_xgs = (PFN_XGS)GetProcAddress(x, "XInputGetState");
        HMODULE w = LoadLibraryW(L"winmm.dll");
        if (w) { s_jgpe = (PFN_JGPE)GetProcAddress(w, "joyGetPosEx"); s_jgdc = (PFN_JGDC)GetProcAddress(w, "joyGetDevCapsW"); }
    }
    const DWORD now = GetTickCount();
    if (s_xgs && now - s_xFailT > 1500) {   // XInput (mando de Xbox, o DualShock / DualSense con Steam Input / DS4Windows)
        for (DWORD i = 0; i < 4; i++) {
            XStateG st = {};
            if (s_xgs(i, &st) != 0) continue;
            const WORD w = st.Gamepad.wButtons;
            p.connected = true; p.source = 1;
            p.b[kPadCross] = w & 0x1000; p.b[kPadCircle] = w & 0x2000; p.b[kPadSquare] = w & 0x4000; p.b[kPadTriangle] = w & 0x8000;
            p.b[kPadL1] = w & 0x0100; p.b[kPadR1] = w & 0x0200; p.b[kPadShare] = w & 0x0020; p.b[kPadOptions] = w & 0x0010;
            p.b[kPadL3] = w & 0x0040; p.b[kPadR3] = w & 0x0080;
            p.b[kPadUp] = w & 0x0001; p.b[kPadDown] = w & 0x0002; p.b[kPadLeft] = w & 0x0004; p.b[kPadRight] = w & 0x0008;
            p.l2 = st.Gamepad.bLeftTrigger / 255.0f; p.r2 = st.Gamepad.bRightTrigger / 255.0f; p.b[kPadL2] = p.l2 > 0.25f; p.b[kPadR2] = p.r2 > 0.25f;
            p.lx = Dz(st.Gamepad.sThumbLX); p.ly = -Dz(st.Gamepad.sThumbLY); p.rx = Dz(st.Gamepad.sThumbRX); p.ry = -Dz(st.Gamepad.sThumbRY);
            return true;
        }
        s_xFailT = now;   // sin mando XInput: se vuelve a mirar cada 1.5 s (XInputGetState de un puerto vacio es lento)
    }
    if (s_jgpe) {   // joystick de Windows: el DualShock 4 / DualSense enchufado sin Steam Input (orden de botones de Sony)
        if (s_joy < 0 && now - s_scanT > 2000) {
            s_scanT = now;
            for (UINT i = 0; i < 16 && s_joy < 0; i++) { JOYINFOEX ji = { sizeof ji, JOY_RETURNALL }; if (s_jgpe(i, &ji) == JOYERR_NOERROR) { s_joy = (int)i; if (s_jgdc) s_jgdc(i, &s_caps, sizeof s_caps); } }
        }
        if (s_joy >= 0) {
            JOYINFOEX ji = { sizeof ji, JOY_RETURNALL };
            if (s_jgpe((UINT)s_joy, &ji) != JOYERR_NOERROR) { s_joy = -1; return false; }
            const DWORD b = ji.dwButtons;
            p.connected = true; p.source = 2;
            p.b[kPadSquare] = b & 1; p.b[kPadCross] = b & 2; p.b[kPadCircle] = b & 4; p.b[kPadTriangle] = b & 8;
            p.b[kPadL1] = b & 16; p.b[kPadR1] = b & 32; p.b[kPadL2] = b & 64; p.b[kPadR2] = b & 128;
            p.b[kPadShare] = b & 256; p.b[kPadOptions] = b & 512; p.b[kPadL3] = b & 1024; p.b[kPadR3] = b & 2048; p.b[kPadPS] = b & 4096; p.b[kPadTouch] = b & 8192;
            const JOYCAPSW& c = s_caps;
            p.lx = Ax(ji.dwXpos, c.wXmin, c.wXmax); p.ly = Ax(ji.dwYpos, c.wYmin, c.wYmax);
            p.rx = Ax(ji.dwZpos, c.wZmin, c.wZmax); p.ry = Ax(ji.dwRpos, c.wRmin, c.wRmax);
            p.l2 = c.wUmax > c.wUmin ? Sat(((float)ji.dwUpos - (float)c.wUmin) / (float)(c.wUmax - c.wUmin)) : (p.b[kPadL2] ? 1.0f : 0.0f);
            p.r2 = c.wVmax > c.wVmin ? Sat(((float)ji.dwVpos - (float)c.wVmin) / (float)(c.wVmax - c.wVmin)) : (p.b[kPadR2] ? 1.0f : 0.0f);
            if (ji.dwPOV != 0xFFFF && ji.dwPOV <= 36000) {
                const int pv = (int)ji.dwPOV;
                p.b[kPadUp] = pv >= 31500 || pv <= 4500; p.b[kPadRight] = pv >= 4500 && pv <= 13500; p.b[kPadDown] = pv >= 13500 && pv <= 22500; p.b[kPadLeft] = pv >= 22500 && pv <= 31500;
            }
            return true;
        }
    }
#endif
    return false;
}
void MvPadFeedImGui(const MvPad& p) {
    ImGuiIO& io = ImGui::GetIO();
    if (!p.connected || p.source != 2 || (io.BackendFlags & ImGuiBackendFlags_HasGamepad)) return;   // XInput ya lo pasa el backend de ImGui
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, p.b[kPadCross]); io.AddKeyEvent(ImGuiKey_GamepadFaceRight, p.b[kPadCircle]);
    io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, p.b[kPadSquare]); io.AddKeyEvent(ImGuiKey_GamepadFaceUp, p.b[kPadTriangle]);
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, p.b[kPadUp]); io.AddKeyEvent(ImGuiKey_GamepadDpadDown, p.b[kPadDown]);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, p.b[kPadLeft]); io.AddKeyEvent(ImGuiKey_GamepadDpadRight, p.b[kPadRight]);
    io.AddKeyEvent(ImGuiKey_GamepadL1, p.b[kPadL1]); io.AddKeyEvent(ImGuiKey_GamepadR1, p.b[kPadR1]);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, p.l2 > 0.1f, p.l2); io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, p.r2 > 0.1f, p.r2);
    io.AddKeyEvent(ImGuiKey_GamepadStart, p.b[kPadOptions]); io.AddKeyEvent(ImGuiKey_GamepadBack, p.b[kPadShare]);
    io.AddKeyEvent(ImGuiKey_GamepadL3, p.b[kPadL3]); io.AddKeyEvent(ImGuiKey_GamepadR3, p.b[kPadR3]);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, p.lx < -0.1f, Sat(-p.lx)); io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, p.lx > 0.1f, Sat(p.lx));
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, p.ly < -0.1f, Sat(-p.ly)); io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, p.ly > 0.1f, Sat(p.ly));
    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickLeft, p.rx < -0.1f, Sat(-p.rx)); io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickRight, p.rx > 0.1f, Sat(p.rx));
    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickUp, p.ry < -0.1f, Sat(-p.ry)); io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown, p.ry > 0.1f, Sat(p.ry));
}
// ------------------------------------------------------------------------------------------------------------------------------
// estilo del mando (14/11 d): Agustin: "este mando de ps5 puede cambiar por uno de xbox dependiendo el display del mod de sider
// controller ... segun los iconos que se muestran en el juego". Los iconos del juego los pone livecpk\Dream Patch 21 Graphic
// Controller (copiado de Opcionales\Controller Graphics PS5 / PS4 / XBOX): ext_fnt.bin = 757104 bytes en el de Xbox (55036 en los
// de PlayStation); keyConfig.bin = 648437 en el de PS5 y 744837 en el de PS4.
static int s_style = kPadStylePS5; static ImVec4 s_body(-1, 0, 0, 0);
void MvPadSetStyle(int style, unsigned rgb) {
    s_style = std::clamp(style, 0, 2);
    s_body = rgb ? ImVec4(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, 1) : ImVec4(-1, 0, 0, 0);
}
int MvPadGetStyle() { return s_style; }
int MvPadDetectStyle(const wchar_t* gameDir) {
#ifdef _WIN32
    auto size = [](const std::wstring& f) -> long long { WIN32_FILE_ATTRIBUTE_DATA d; if (!GetFileAttributesExW(f.c_str(), GetFileExInfoStandard, &d)) return -1; return ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow; };
    const std::wstring g = gameDir ? gameDir : L"";
    const wchar_t* roots[] = { L"Dream Patch Sider\\livecpk\\Dream Patch 21 Graphic Controller\\", L"sider\\livecpk\\Dream Patch 21 Graphic Controller\\", L"livecpk\\Dream Patch 21 Graphic Controller\\" };
    for (const wchar_t* r : roots) {
        const std::wstring d = g + r;
        const long long fnt = size(d + L"common\\menu\\font\\ext_fnt.bin"), key = size(d + L"common\\menu\\mode\\keyConfig.bin");
        if (fnt < 0 && key < 0) continue;
        if (fnt > 300000) return kPadStyleXbox;          // la fuente de botones de Xbox es mucho mas grande
        if (key == 744837) return kPadStylePS4;
        return kPadStylePS5;
    }
#else
    (void)gameDir;
#endif
    return kPadStylePS5;   // sin el mod: los botones del juego son de PlayStation
}

// simbolos de los botones: en PlayStation trazos finos y claros sobre un circulo oscuro; en Xbox la letra con su color
static void GlyphShape(ImDrawList* dl, ImVec2 c, float s, int btn, ImU32 sc, float th) {
    if (btn == kPadCross) { dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), sc, th); dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), sc, th); }
    else if (btn == kPadCircle) dl->AddCircle(c, s * 1.08f, sc, 24, th);
    else if (btn == kPadSquare) dl->AddRect(ImVec2(c.x - s * 0.92f, c.y - s * 0.92f), ImVec2(c.x + s * 0.92f, c.y + s * 0.92f), sc, 0, 0, th);
    else dl->AddTriangle(ImVec2(c.x, c.y - s * 1.10f), ImVec2(c.x + s * 1.05f, c.y + s * 0.72f), ImVec2(c.x - s * 1.05f, c.y + s * 0.72f), sc, th);
}
static const char* kXboxLetter[4] = { "A", "B", "X", "Y" };   // cruz = A, circulo = B, cuadrado = X, triangulo = Y
static void XboxLetter(ImDrawList* dl, ImVec2 c, float s, int btn, ImU32 col) {
    ImFont* f = ImGui::GetFont(); const float fs = s * 3.0f; const char* t = kXboxLetter[btn];
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, t);
    dl->AddText(f, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, t);
}
static const ImVec4 kGlyphCol[4] = { ImVec4(0.55f, 0.72f, 1.0f, 1), ImVec4(1.0f, 0.45f, 0.48f, 1), ImVec4(1.0f, 0.60f, 0.88f, 1), ImVec4(0.40f, 0.92f, 0.78f, 1) };
static const ImVec4 kXboxCol[4] = { ImVec4(0.42f, 0.85f, 0.30f, 1), ImVec4(1.0f, 0.36f, 0.32f, 1), ImVec4(0.30f, 0.62f, 1.0f, 1), ImVec4(1.0f, 0.82f, 0.22f, 1) };
static ImVec4 FaceCol(int btn) { return s_style == kPadStyleXbox ? kXboxCol[btn] : kGlyphCol[btn]; }
void MvPadGlyph(ImDrawList* dl, ImVec2 c, float r, int btn, float alpha, float lit) {
    const float th = std::max(1.3f, r * 0.17f);
    if (btn >= kPadCross && btn <= kPadTriangle) {
        const ImVec4 cc = FaceCol(btn);
        if (lit > 0.01f) for (int i = 3; i >= 1; i--) dl->AddCircleFilled(c, r * (1.0f + 0.22f * i * lit), ColA(cc, 0.10f * lit * alpha), 24);
        dl->AddCircleFilled(c, r, Col(0.06f, 0.07f, 0.09f, 0.88f * alpha), 28);
        dl->AddCircle(c, r - 0.5f, Col(1, 1, 1, 0.22f * alpha), 28, 1.0f);
        if (s_style == kPadStyleXbox) XboxLetter(dl, c, r * 0.5f, btn, ColA(Lerp4(cc, ImVec4(1, 1, 1, 1), 0.15f + 0.35f * lit), alpha));
        else GlyphShape(dl, c, r * 0.46f, btn, ColA(Lerp4(ImVec4(0.93f, 0.94f, 0.96f, 1), cc, 0.25f + 0.6f * lit), alpha), th);
        return;
    }
    if (btn >= kPadUp) {   // cruceta
        const float a = r * 0.34f; const ImU32 cl = Col(0.90f, 0.91f, 0.94f, 0.9f * alpha);
        dl->AddRectFilled(ImVec2(c.x - a, c.y - r), ImVec2(c.x + a, c.y + r), cl, a * 0.6f);
        dl->AddRectFilled(ImVec2(c.x - r, c.y - a), ImVec2(c.x + r, c.y + a), cl, a * 0.6f);
        dl->AddCircleFilled(c, a * 0.55f, Col(0.06f, 0.07f, 0.09f, 0.6f * alpha), 12);
        return;
    }
    static const char* kPS5[kPadCount] = { "", "", "", "", "L1", "R1", "L2", "R2", "CREATE", "OPTIONS", "L3", "R3", "PS", "PAD", "", "", "", "" };
    static const char* kPS4[kPadCount] = { "", "", "", "", "L1", "R1", "L2", "R2", "SHARE", "OPTIONS", "L3", "R3", "PS", "PAD", "", "", "", "" };
    static const char* kXb[kPadCount] = { "", "", "", "", "LB", "RB", "LT", "RT", "VIEW", "MENU", "LS", "RS", "XBOX", "", "", "", "", "" };
    const char* n = (s_style == kPadStyleXbox ? kXb : s_style == kPadStylePS4 ? kPS4 : kPS5)[btn]; ImFont* f = ImGui::GetFont(); const float fs = r * 1.15f;
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, n);
    const ImVec2 a(c.x - ts.x * 0.5f - r * 0.5f, c.y - r * 0.78f), b(c.x + ts.x * 0.5f + r * 0.5f, c.y + r * 0.78f);
    dl->AddRectFilled(a, b, Col(0.06f + 0.3f * lit, 0.07f + 0.3f * lit, 0.09f + 0.35f * lit, 0.88f * alpha), r * 0.78f);
    dl->AddRect(a, b, Col(1, 1, 1, 0.25f * alpha), r * 0.78f);
    dl->AddText(f, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), Col(0.93f, 0.94f, 0.96f, alpha), n);
}
// --- dibujo del mando (14/11 d, segun las fotos de Agustin): dibujo plano de un solo color con sombra suave de arriba hacia abajo,
// detalles en el mismo tono un poco mas oscuro y bordes finos. PS5 blanco con el frente gris; PS4 y Xbox del color del cuerpo.
// Todo con curvas de ImGui; no es una imagen.
ImVec2 MvPadSize(float k) { return ImVec2(240.0f * k, 178.0f * k); }
// curva suave por los puntos (Catmull-Rom), cerrada
static void SmoothClosed(const ImVec2* in, int n, ImVector<ImVec2>& out, int sub) {
    out.resize(0);
    for (int i = 0; i < n; i++) {
        const ImVec2 p0 = in[(i - 1 + n) % n], p1 = in[i], p2 = in[(i + 1) % n], p3 = in[(i + 2) % n];
        for (int j = 0; j < sub; j++) {
            const float t = (float)j / sub, t2 = t * t, t3 = t2 * t;
            out.push_back(ImVec2(0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3),
                                 0.5f * ((2 * p1.y) + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3)));
        }
    }
}
// medio contorno (lado derecho, de arriba al centro de abajo) en unidades: x 0..1 = medio ancho, y 0..1 = alto; se espeja
static void MirrorShape(const float (*half)[2], int n, ImVec2 o, float hw, float H, ImVector<ImVec2>& pts) {
    pts.resize(0);
    for (int i = 0; i < n; i++) pts.push_back(ImVec2(o.x + half[i][0] * hw, o.y + half[i][1] * H));
    for (int i = n - 1; i >= 0; i--) if (half[i][0] > 1e-4f) pts.push_back(ImVec2(o.x - half[i][0] * hw, o.y + half[i][1] * H));
}
// forma de un lado (sin espejar): puntos (u, v) con u ya con signo
static void SideShape(const float (*pts)[2], int n, float sg, ImVec2 o, float hw, float H, ImVector<ImVec2>& out) {
    out.resize(0); for (int i = 0; i < n; i++) out.push_back(ImVec2(o.x + sg * pts[i][0] * hw, o.y + pts[i][1] * H));
    if (sg < 0) std::reverse(out.begin(), out.end());
}
// relleno con degrade vertical (luz arriba, sombra abajo) + borde fino
static void FillShade(ImDrawList* dl, const ImVector<ImVec2>& pts, ImVec4 top, ImVec4 bot, float y0, float y1, float a, ImU32 edge, float edgeW) {
    const int v0 = dl->VtxBuffer.Size;
    dl->AddConcavePolyFilled(pts.Data, pts.Size, ColA(top, a));
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(0, y0), ImVec2(0, y1), ColA(top, 1.0f) | IM_COL32_A_MASK, ColA(bot, 1.0f) | IM_COL32_A_MASK);
    if (edge) dl->AddPolyline(pts.Data, pts.Size, edge, ImDrawFlags_Closed, edgeW);
}
void MvPadDraw(ImDrawList* dl, ImVec2 o, float k, const MvPad& p, float alpha, bool anim, double now) {
    if (alpha <= 0.01f) return;
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID base = ImHashStr("##mvpad");
    const float dt = ImGui::GetIO().DeltaTime;
    auto lit = [&](int i) {   // el boton apretado se enciende rapido y se apaga suave
        float* v = st->GetFloatRef(base + 10 + i, 0.0f);
        if (!anim) { *v = p.b[i] ? 1.0f : 0.0f; return *v; }
        *v = p.b[i] ? std::min(1.0f, *v + dt * 18.0f) : std::max(0.0f, *v - dt * 4.0f);
        return *v;
    };
    const int sty = s_style;
    const bool xb = sty == kPadStyleXbox, ps5 = sty == kPadStylePS5;
    const ImVec2 S = MvPadSize(k); const float A = alpha;
    const ImVec2 c0(o.x + S.x * 0.5f, o.y);   // centro de arriba
    const float hw = S.x * 0.5f, H = S.y;
    auto P = [&](float u, float v) { return ImVec2(c0.x + u * hw, c0.y + v * H); };
    auto mix = [](ImVec4 a, ImVec4 b, float t) { return Lerp4(a, b, t); };
    const ImVec4 W1(1, 1, 1, 1), K1(0, 0, 0, 1);
    // colores: cuerpo + tonos del mismo color (como las fotos)
    ImVec4 body = s_body.x >= 0 ? s_body : ps5 ? ImVec4(0.95f, 0.95f, 0.97f, 1) : xb ? ImVec4(0.80f, 0.13f, 0.13f, 1) : ImVec4(0.88f, 0.20f, 0.20f, 1);
    const float lum = body.x * 0.3f + body.y * 0.59f + body.z * 0.11f;
    const bool light = lum > 0.6f;
    const ImVec4 hi = mix(body, W1, 0.18f), lo = mix(body, K1, light ? 0.10f : 0.22f), dk = mix(body, K1, light ? 0.30f : 0.40f), deep = mix(body, K1, light ? 0.45f : 0.62f);
    const ImU32 edge = ColA(mix(body, K1, light ? 0.28f : 0.45f), 0.75f * A);
    const ImVec4 accent = xb ? ImVec4(0.45f, 0.90f, 0.40f, 1) : ImVec4(0.30f, 0.58f, 1.0f, 1);
    ImVector<ImVec2> raw, sm;
    // --- gatillos L2 / R2 (cuernos de atras) y L1 / R1
    for (int s = 0; s < 2; s++) {
        const float sg = s ? 1.0f : -1.0f, fill = s ? p.r2 : p.l2;
        static const float kTrPS5[][2] = { { 0.50f, 0.17f }, { 0.52f, 0.07f }, { 0.58f, 0.005f }, { 0.65f, 0.015f }, { 0.71f, 0.09f }, { 0.74f, 0.18f } };
        static const float kTrPS4[][2] = { { 0.50f, 0.17f }, { 0.52f, 0.08f }, { 0.57f, 0.025f }, { 0.63f, 0.035f }, { 0.67f, 0.10f }, { 0.69f, 0.18f } };
        static const float kTrXb[][2] = { { 0.47f, 0.19f }, { 0.50f, 0.07f }, { 0.55f, 0.0f }, { 0.60f, 0.02f }, { 0.64f, 0.10f }, { 0.67f, 0.20f } };
        const float (*tr)[2] = xb ? kTrXb : ps5 ? kTrPS5 : kTrPS4;
        const float ty = std::max(0.0f, fill) * 0.025f;   // el gatillo apretado baja un poco
        raw.resize(0); for (int i = 0; i < 6; i++) raw.push_back(P(sg * tr[i][0], tr[i][1] + ty));
        SmoothClosed(raw.Data, raw.Size, sm, 5);
        FillShade(dl, sm, mix(mix(hi, W1, ps5 ? 0.0f : 0.1f), accent, 0.75f * fill), mix(lo, accent, 0.6f * fill), P(0, 0).y, P(0, 0.18f).y, 0.95f * A, edge, 1.0f);
        // L1 / R1 (banda debajo del gatillo)
        const float lb = lit(s ? kPadR1 : kPadL1);
        const float x0 = (xb ? 0.46f : 0.50f), x1 = (xb ? 0.70f : ps5 ? 0.75f : 0.70f), y0 = xb ? 0.175f : 0.155f;
        raw.resize(0);
        raw.push_back(P(sg * x0, y0 + 0.03f)); raw.push_back(P(sg * (x0 + 0.02f), y0)); raw.push_back(P(sg * (x1 - 0.03f), y0 - 0.005f)); raw.push_back(P(sg * (x1 + 0.03f), y0 + 0.035f));
        dl->AddConvexPolyFilled(raw.Data, raw.Size, ColA(mix(ps5 ? ImVec4(0.30f, 0.31f, 0.34f, 1) : dk, accent, 0.8f * lb), 0.95f * A));
    }
    // --- cuerpo
    static const float kBodyPS5[][2] = {
        { 0.00f, 0.17f }, { 0.30f, 0.165f }, { 0.56f, 0.17f }, { 0.71f, 0.19f }, { 0.83f, 0.25f }, { 0.91f, 0.35f }, { 0.95f, 0.49f }, { 0.98f, 0.66f },
        { 0.99f, 0.81f }, { 0.96f, 0.93f }, { 0.88f, 1.00f }, { 0.80f, 0.98f }, { 0.73f, 0.89f }, { 0.65f, 0.77f }, { 0.55f, 0.69f }, { 0.40f, 0.655f }, { 0.00f, 0.645f } };
    static const float kBodyPS4[][2] = {
        { 0.00f, 0.17f }, { 0.40f, 0.165f }, { 0.62f, 0.17f }, { 0.75f, 0.19f }, { 0.84f, 0.27f }, { 0.88f, 0.40f }, { 0.90f, 0.56f }, { 0.93f, 0.73f },
        { 0.94f, 0.87f }, { 0.90f, 0.97f }, { 0.81f, 1.00f }, { 0.72f, 0.95f }, { 0.65f, 0.84f }, { 0.57f, 0.73f }, { 0.45f, 0.685f }, { 0.28f, 0.675f }, { 0.00f, 0.675f } };
    static const float kBodyXb[][2] = {
        { 0.00f, 0.165f }, { 0.12f, 0.175f }, { 0.28f, 0.20f }, { 0.48f, 0.19f }, { 0.64f, 0.20f }, { 0.78f, 0.23f }, { 0.88f, 0.30f }, { 0.94f, 0.43f },
        { 0.97f, 0.60f }, { 0.99f, 0.78f }, { 0.97f, 0.91f }, { 0.91f, 0.99f }, { 0.83f, 0.97f }, { 0.75f, 0.87f }, { 0.65f, 0.75f }, { 0.53f, 0.69f }, { 0.30f, 0.665f }, { 0.00f, 0.66f } };
    if (xb) MirrorShape(kBodyXb, IM_ARRAYSIZE(kBodyXb), c0, hw, H, raw); else if (ps5) MirrorShape(kBodyPS5, IM_ARRAYSIZE(kBodyPS5), c0, hw, H, raw); else MirrorShape(kBodyPS4, IM_ARRAYSIZE(kBodyPS4), c0, hw, H, raw);
    SmoothClosed(raw.Data, raw.Size, sm, 6);
    // sombra en el piso (da volumen sobre el fondo oscuro)
    dl->AddEllipseFilled(P(0, 0.99f), ImVec2(hw * 0.95f, H * 0.05f), Col(0, 0, 0, 0.22f * A), 0.0f, 32);
    FillShade(dl, sm, hi, lo, P(0, 0.15f).y, P(0, 1.0f).y, (ps5 ? 0.94f : 0.92f) * A, edge, 1.2f);
    // brillo suave arriba a la izquierda (como la luz de las fotos)
    if (ps5) {
        // frente gris (entre los agarres) y el filo gris de adentro de cada agarre
        static const float kFront[][2] = { { 0.00f, 0.45f }, { 0.27f, 0.445f }, { 0.41f, 0.50f }, { 0.50f, 0.59f }, { 0.545f, 0.68f }, { 0.40f, 0.655f }, { 0.00f, 0.645f } };
        MirrorShape(kFront, IM_ARRAYSIZE(kFront), c0, hw, H, raw);
        SmoothClosed(raw.Data, raw.Size, sm, 5);
        FillShade(dl, sm, ImVec4(0.80f, 0.81f, 0.84f, 1), ImVec4(0.72f, 0.73f, 0.77f, 1), P(0, 0.45f).y, P(0, 0.68f).y, 0.96f * A, Col(0.60f, 0.61f, 0.66f, 0.6f * A), 1.0f);
        for (int s2 = 0; s2 < 2; s2++) { const float sg = s2 ? 1.0f : -1.0f;
            dl->AddBezierCubic(P(sg * 0.50f, 0.66f), P(sg * 0.60f, 0.72f), P(sg * 0.68f, 0.84f), P(sg * 0.775f, 0.965f), Col(0.76f, 0.77f, 0.81f, 0.95f * A), 5.0f * k); }
    } else {
        // puntas de los agarres un tono mas oscuras
        static const float kGripPS4[][2] = { { 0.64f, 0.81f }, { 0.80f, 0.79f }, { 0.935f, 0.79f }, { 0.94f, 0.87f }, { 0.90f, 0.97f }, { 0.81f, 1.00f }, { 0.72f, 0.95f } };
        static const float kGripXb[][2] = { { 0.70f, 0.82f }, { 0.84f, 0.80f }, { 0.985f, 0.80f }, { 0.97f, 0.91f }, { 0.91f, 0.99f }, { 0.83f, 0.97f }, { 0.75f, 0.87f } };
        for (int s = 0; s < 2; s++) {
            SideShape(xb ? kGripXb : kGripPS4, 7, s ? 1.0f : -1.0f, c0, hw, H, raw);
            SmoothClosed(raw.Data, raw.Size, sm, 4);
            FillShade(dl, sm, mix(hi, lo, 0.8f), dk, P(0, 0.79f).y, P(0, 1.0f).y, 0.75f * A, 0, 0);
        }
        // borde de adentro de los agarres con un filo claro (Xbox)
    }
    // --- panel tactil (PS5 / PS4)
    if (!xb) {
        const float lt = std::max(lit(kPadTouch), lit(kPadPS) * 0.4f);
        if (ps5) {
            static const float kPad[][2] = { { 0.00f, 0.18f }, { 0.30f, 0.18f }, { 0.29f, 0.30f }, { 0.255f, 0.42f }, { 0.20f, 0.445f }, { 0.00f, 0.45f } };
            MirrorShape(kPad, IM_ARRAYSIZE(kPad), c0, hw, H, raw);
            FillShade(dl, raw, mix(ImVec4(0.98f, 0.98f, 1, 1), accent, 0.25f * lt), ImVec4(0.90f, 0.91f, 0.94f, 1), P(0, 0.18f).y, P(0, 0.45f).y, 0.97f * A, Col(0.66f, 0.68f, 0.73f, 0.75f * A), 1.2f);
            // barra de luz a los costados (late suave)
            const float glow = (anim ? 0.55f + 0.45f * sinf((float)now * 1.6f) : 0.8f) * (0.6f + 0.4f * lt);
            for (int s = 0; s < 2; s++) {
                const float sg = s ? 1.0f : -1.0f; const ImVec2 a = P(sg * 0.325f, 0.20f), b = P(sg * 0.28f, 0.43f);
                for (int i = 3; i >= 1; i--) dl->AddLine(a, b, ColA(accent, 0.14f * glow * A), (1.5f + 2.0f * i) * k);
                dl->AddLine(a, b, ColA(mix(accent, W1, 0.4f), 0.85f * glow * A), 1.4f * k);
            }
        } else {
            raw.resize(0); raw.push_back(P(-0.28f, 0.175f)); raw.push_back(P(0.28f, 0.175f)); raw.push_back(P(0.28f, 0.42f)); raw.push_back(P(0.25f, 0.445f)); raw.push_back(P(-0.25f, 0.445f)); raw.push_back(P(-0.28f, 0.42f));
            FillShade(dl, raw, mix(dk, accent, 0.25f * lt), mix(dk, K1, 0.1f), P(0, 0.17f).y, P(0, 0.45f).y, 0.95f * A, ColA(deep, 0.5f * A), 1.0f);
            // parlante (puntitos)
            for (int r = 0; r < 2; r++) for (int i = -3; i <= 3; i++) dl->AddCircleFilled(P(i * 0.016f + (r ? 0.008f : 0.0f), 0.475f + r * 0.018f), 0.9f * k, ColA(deep, 0.8f * A), 6);
        }
    }
    // --- botones del medio: create / share / options (PS) o view / menu (Xbox)
    {
        const float ls = lit(kPadShare), lo2 = lit(kPadOptions);
        if (xb) {
            for (int s = 0; s < 2; s++) { const ImVec2 c = P(s ? 0.17f : -0.17f, 0.34f); const float l = s ? lo2 : ls;
                dl->AddCircleFilled(c, 5.5f * k, ColA(mix(dk, accent, 0.8f * l), 0.95f * A), 16); dl->AddCircle(c, 5.5f * k, ColA(deep, 0.7f * A), 16, 1.0f); }
        } else {
            const float x = ps5 ? 0.37f : 0.355f;
            for (int s = 0; s < 2; s++) { const float sg = s ? 1.0f : -1.0f; const float l = s ? lo2 : ls;
                dl->AddRectFilled(P(sg * x - 0.017f, 0.215f), P(sg * x + 0.017f, 0.29f), ColA(mix(ps5 ? ImVec4(0.72f, 0.73f, 0.77f, 1) : dk, accent, l), 0.95f * A), 3.0f * k); }
        }
    }
    // --- cruceta
    {
        const ImVec2 c = xb ? P(-0.25f, 0.585f) : P(-0.60f, 0.385f);
        const float r = (xb ? 0.11f : 0.135f) * hw;
        if (xb) {
            dl->AddCircleFilled(c, r * 1.25f, ColA(lo, 0.9f * A), 28); dl->AddCircle(c, r * 1.25f, ColA(deep, 0.35f * A), 28, 1.0f);
            const float w = r * 0.36f; const ImVec4 dc = dk;
            dl->AddRectFilled(ImVec2(c.x - w, c.y - r), ImVec2(c.x + w, c.y + r), ColA(dc, 0.95f * A), 2.0f * k);
            dl->AddRectFilled(ImVec2(c.x - r, c.y - w), ImVec2(c.x + r, c.y + w), ColA(dc, 0.95f * A), 2.0f * k);
            struct { int b; float dx, dy; } d[4] = { { kPadUp, 0, -1 }, { kPadDown, 0, 1 }, { kPadLeft, -1, 0 }, { kPadRight, 1, 0 } };
            for (auto& q : d) { const float lv = lit(q.b); if (lv < 0.01f) continue; const ImVec2 m(c.x + q.dx * r * 0.62f, c.y + q.dy * r * 0.62f);
                dl->AddRectFilled(ImVec2(m.x - (q.dx ? r * 0.38f : w), m.y - (q.dy ? r * 0.38f : w)), ImVec2(m.x + (q.dx ? r * 0.38f : w), m.y + (q.dy ? r * 0.38f : w)), ColA(accent, 0.9f * lv * A), 2.0f * k); }
        } else {
            if (!ps5) { dl->AddCircleFilled(c, r * 1.18f, ColA(mix(body, K1, 0.06f), 0.6f * A), 28); dl->AddCircle(c, r * 1.18f, ColA(deep, 0.25f * A), 28, 1.0f); }
            const float w = 0.045f * hw;
            struct { int b; float dx, dy; } d[4] = { { kPadUp, 0, -1 }, { kPadDown, 0, 1 }, { kPadLeft, -1, 0 }, { kPadRight, 1, 0 } };
            for (auto& q : d) {
                const float lv = lit(q.b);
                const ImVec2 tip(c.x + q.dx * r * (ps5 ? 0.28f : 0.36f), c.y + q.dy * r * (ps5 ? 0.28f : 0.36f)), end(c.x + q.dx * r, c.y + q.dy * r);
                const ImVec2 pr(-q.dy, q.dx);
                const ImVec2 pts[5] = { tip, ImVec2(tip.x + q.dx * w + pr.x * w * 1.25f, tip.y + q.dy * w + pr.y * w * 1.25f), ImVec2(end.x + pr.x * w * 1.25f, end.y + pr.y * w * 1.25f),
                                        ImVec2(end.x - pr.x * w * 1.25f, end.y - pr.y * w * 1.25f), ImVec2(tip.x + q.dx * w - pr.x * w * 1.25f, tip.y + q.dy * w - pr.y * w * 1.25f) };
                const ImVec4 bc = ps5 ? ImVec4(0.93f, 0.94f, 0.96f, 1) : hi;
                dl->AddConvexPolyFilled(pts, 5, ColA(mix(bc, accent, 0.8f * lv), 0.96f * A));
                dl->AddPolyline(pts, 5, ps5 ? Col(0.62f, 0.64f, 0.69f, 0.85f * A) : ColA(deep, 0.6f * A), ImDrawFlags_Closed, 1.0f);
                if (ps5) dl->AddCircleFilled(ImVec2((tip.x + end.x) * 0.5f + q.dx * w * 0.6f, (tip.y + end.y) * 0.5f + q.dy * w * 0.6f), 0.9f * k, Col(0.60f, 0.62f, 0.67f, 0.8f * A), 6);
            }
        }
    }
    // --- botones de accion
    {
        const ImVec2 c = xb ? P(0.55f, 0.37f) : P(0.60f, 0.385f);
        const float d = (xb ? 0.105f : 0.115f) * hw, r = (xb ? 0.052f : 0.056f) * hw;
        struct { int b; float dx, dy; } q[4] = { { kPadTriangle, 0, -1 }, { kPadCircle, 1, 0 }, { kPadCross, 0, 1 }, { kPadSquare, -1, 0 } };
        if (!ps5 && !xb) { dl->AddCircleFilled(c, d + r * 1.3f, ColA(mix(body, K1, 0.06f), 0.6f * A), 32); dl->AddCircle(c, d + r * 1.3f, ColA(deep, 0.25f * A), 32, 1.0f); }
        for (auto& e : q) {
            const float lv = lit(e.b); const ImVec2 bc(c.x + e.dx * d, c.y + e.dy * d); const ImVec4 cc = FaceCol(e.b);
            if (lv > 0.01f) for (int i = 3; i >= 1; i--) dl->AddCircleFilled(bc, r * (1.0f + 0.3f * i * lv), ColA(cc, 0.13f * lv * A), 20);
            dl->AddCircleFilled(ImVec2(bc.x, bc.y + 1.0f * k), r, ColA(K1, 0.25f * A), 20);
            const ImVec4 face = ps5 ? ImVec4(0.95f, 0.95f, 0.97f, 1) : mix(body, W1, 0.06f);
            dl->AddCircleFilled(bc, r, ColA(mix(face, cc, 0.40f * lv), 0.97f * A), 20);
            dl->AddCircle(bc, r, ps5 ? Col(0.60f, 0.62f, 0.67f, 0.85f * A) : ColA(deep, 0.55f * A), 20, 1.0f);
            const ImVec4 gc = ps5 ? ImVec4(0.55f, 0.57f, 0.62f, 1) : deep;
            if (xb) XboxLetter(dl, bc, r * 0.55f, e.b, ColA(mix(gc, mix(cc, K1, 0.2f), 0.3f + 0.7f * lv), A));
            else GlyphShape(dl, bc, r * 0.46f, e.b, ColA(mix(gc, mix(cc, K1, 0.3f), lv), A), std::max(1.1f, r * 0.16f));
        }
    }
    // --- analogicos: hueco oscuro, perilla corrida segun el stick, arco del color de acento hacia donde se empuja
    for (int s = 0; s < 2; s++) {
        const ImVec2 c = xb ? (s ? P(0.25f, 0.585f) : P(-0.53f, 0.37f)) : P(s ? (ps5 ? 0.22f : 0.27f) : (ps5 ? -0.22f : -0.27f), ps5 ? 0.545f : 0.565f);
        const float R = (xb ? 0.10f : 0.10f) * hw, sx = s ? p.rx : p.lx, sy = s ? p.ry : p.ly;
        float* fx_ = st->GetFloatRef(base + 40 + s * 2, 0.0f); float* fy_ = st->GetFloatRef(base + 41 + s * 2, 0.0f);
        if (anim) { const float a = 1.0f - expf(-dt * 22.0f); *fx_ += (sx - *fx_) * a; *fy_ += (sy - *fy_) * a; } else { *fx_ = sx; *fy_ = sy; }
        const float lv = lit(s ? kPadR3 : kPadL3), mag = sqrtf(*fx_ * *fx_ + *fy_ * *fy_);
        const ImVec4 well = ps5 ? ImVec4(0.62f, 0.63f, 0.67f, 1) : deep, cap = ps5 ? ImVec4(0.55f, 0.56f, 0.60f, 1) : dk;
        if (xb) dl->AddCircleFilled(c, R * 1.18f, ColA(lo, 0.9f * A), 28);
        dl->AddCircleFilled(c, R, ColA(well, 0.95f * A), 28);
        if (mag > 0.05f) { const float ang = atan2f(*fy_, *fx_); dl->PathArcTo(c, R * 0.97f, ang - 0.7f, ang + 0.7f, 12); dl->PathStroke(ColA(accent, 0.9f * Sat(mag) * A), 0, 2.2f * k); }
        const ImVec2 kc(c.x + *fx_ * R * 0.42f, c.y + *fy_ * R * 0.42f); const float kr = R * 0.78f * (1.0f - 0.07f * lv);
        dl->AddCircleFilled(ImVec2(kc.x, kc.y + 1.5f * k), kr, ColA(K1, 0.30f * A), 24);
        const int v0 = dl->VtxBuffer.Size;
        dl->AddCircleFilled(kc, kr, ColA(mix(cap, accent, 0.35f * lv), 0.98f * A), 24);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(kc.x, kc.y - kr), ImVec2(kc.x, kc.y + kr), ColA(mix(mix(cap, W1, 0.15f), accent, 0.35f * lv), 1) | IM_COL32_A_MASK, ColA(mix(mix(cap, K1, 0.12f), accent, 0.35f * lv), 1) | IM_COL32_A_MASK);
        dl->AddCircle(kc, kr * 0.62f, ColA(mix(cap, K1, 0.2f), 0.7f * A), 20, 1.2f);
    }
    // --- boton central: PS (PS5 en el frente gris, PS4 negro redondo) o el boton Xbox que brilla
    {
        const float lp = lit(kPadPS);
        if (xb) {
            const ImVec2 c = P(0.0f, 0.245f); const float r = 0.060f * hw;
            const float gl = (anim ? 0.75f + 0.25f * sinf((float)now * 1.4f) : 0.9f) * (0.7f + 0.3f * lp);
            for (int i = 3; i >= 1; i--) dl->AddCircleFilled(c, r * (1.0f + 0.25f * i), Col(1, 1, 1, 0.07f * gl * A), 24);
            dl->AddCircleFilled(c, r, ColA(mix(ImVec4(0.92f, 0.93f, 0.95f, 1), accent, 0.5f * lp), 0.95f * gl * A), 24);
            const float x = r * 0.42f; const ImU32 lc = ColA(mix(body, K1, 0.2f), 0.9f * A);
            dl->AddBezierQuadratic(ImVec2(c.x - x, c.y - x), c, ImVec2(c.x + x, c.y + x), lc, 1.8f * k);
            dl->AddBezierQuadratic(ImVec2(c.x + x, c.y - x), c, ImVec2(c.x - x, c.y + x), lc, 1.8f * k);
        } else if (ps5) {
            const ImVec2 pc = P(0.0f, 0.57f);
            if (lp > 0.01f) dl->AddCircleFilled(pc, 9.0f * k, ColA(accent, 0.30f * lp * A), 16);
            dl->AddText(ImGui::GetFont(), 10.0f * k, ImVec2(pc.x - 6.0f * k, pc.y - 6.0f * k), ColA(mix(ImVec4(0.52f, 0.54f, 0.58f, 1), accent, lp), A), "PS");
            for (int i = -2; i <= 2; i++) dl->AddCircleFilled(P(i * 0.016f, 0.50f), 0.9f * k, Col(0.55f, 0.57f, 0.61f, 0.8f * A), 6);
        } else {
            const ImVec2 pc = P(0.0f, 0.60f); const float r = 0.035f * hw;
            dl->AddCircleFilled(pc, r, ColA(mix(ImVec4(0.08f, 0.08f, 0.10f, 1), accent, 0.6f * lp), 0.95f * A), 16);
            dl->AddText(ImGui::GetFont(), 7.0f * k, ImVec2(pc.x - 4.0f * k, pc.y - 4.5f * k), Col(0.9f, 0.9f, 0.92f, 0.9f * A), "PS");
        }
    }
}
void MvPadHints(float k, float alpha) {
    struct { int b; const char* t; } h[] = { { kPadCross, "Elegir" }, { kPadCircle, "Volver" }, { kPadTriangle, "Escribir" }, { kPadSquare, "Ventanas" }, { kPadUp, "Moverse" }, { kPadL1, "Fino / rapido" } };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetFontSize() * 0.48f;
    ImVec2 p = ImGui::GetCursorScreenPos(); const float y = p.y + ImGui::GetFontSize() * 0.5f + 1.0f;
    const float maxX = p.x + ImGui::GetContentRegionAvail().x;
    for (auto& e : h) {
        const char* t = TRS(e.t);
        const float w = r * 2.6f + ImGui::CalcTextSize(t).x + 14.0f * k;
        if (p.x + w > maxX) break;
        MvPadGlyph(dl, ImVec2(p.x + r * 1.2f, y), r, e.b, alpha);
        dl->AddText(ImVec2(p.x + r * 2.6f, p.y), Col(0.75f, 0.78f, 0.84f, alpha), t);
        p.x += w;
    }
    ImGui::Dummy(ImVec2(1, ImGui::GetFontSize() + 2.0f));
}

} // namespace fx
