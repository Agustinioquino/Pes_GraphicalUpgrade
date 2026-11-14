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
// (14/11 b) Agustin: "no me gusta como se plasma lo del sol" -> panorama del cielo visto desde la cancha: una franja con la silueta
// de las tribunas, los arcos y las torres de luz; izquierda / derecha = hacia donde esta el sol (azimut, la vuelta entera), arriba /
// abajo = la altura. Al lado, un jugador parado en el pasto con la luz de ese sol y su sombra.
static float WrapDeg(float a) { a = fmodf(a, 360.0f); return a < 0 ? a + 360.0f : a; }
// alto de la tribuna (en grados de altura) segun el rumbo: las de los costados mas altas, detras de los arcos mas bajas
static float StandDeg(float az) { const float c = fabsf(cosf(az * kDeg)); return 6.0f + 9.0f * Smooth(0.25f, 0.85f, c); }
static void DrawPlayer(ImDrawList* dl, ImVec2 feet, float h, const float L[3], ImVec4 sun, ImVec4 sky, float day, float sunI, float A) {
    // sombra en el pasto: del pie hacia el lado contrario al sol, larga con el sol bajo (vista desde la camara: X a la derecha,
    // Z hacia el fondo = hacia arriba en la pantalla, achicado por la perspectiva)
    if (L[1] > 0.02f && day > 0.01f) {
        const float len = std::min(3.0f, 1.0f / tanf(std::max(asinf(Sat(L[1])), 5.0f * kDeg))) * h;
        const float hx = sqrtf(L[0] * L[0] + L[2] * L[2]) + 1e-4f, dx = -L[0] / hx, dz = -L[2] / hx;
        const ImVec2 e(feet.x + dx * len, feet.y - dz * len * 0.30f);
        const float w = h * 0.13f, nx = -(e.y - feet.y), ny = e.x - feet.x, nl = sqrtf(nx * nx + ny * ny) + 1e-4f;
        const ImVec2 q[4] = { ImVec2(feet.x + nx / nl * w, feet.y + ny / nl * w), ImVec2(e.x + nx / nl * w * 0.7f, e.y + ny / nl * w * 0.7f),
                              ImVec2(e.x - nx / nl * w * 0.7f, e.y - ny / nl * w * 0.7f), ImVec2(feet.x - nx / nl * w, feet.y - ny / nl * w) };
        dl->AddConvexPolyFilled(q, 4, Col(0.02f, 0.06f, 0.02f, 0.55f * day * sunI * A));
        dl->AddEllipseFilled(feet, ImVec2(w * 1.6f, w * 0.45f), Col(0, 0, 0, 0.45f * A), 0, 16);
    }
    // jugador: piernas, short, camiseta, brazos, cabeza; cada parte con luz del sol (lado que mira al sol) y del cielo
    auto shade = [&](float nx, float ny, ImVec4 base) {
        const float lam = std::max(0.0f, nx * L[0] + ny * L[1] - 0.35f * L[2]) * day * sunI;
        const float amb = 0.30f + 0.15f * ny;
        return ImVec4(Sat(base.x * (lam * sun.x * 0.95f + amb * sky.x)), Sat(base.y * (lam * sun.y * 0.95f + amb * sky.y)), Sat(base.z * (lam * sun.z * 0.95f + amb * sky.z)), A);
    };
    const ImVec4 kit(0.85f, 0.18f, 0.20f, 1), shorts(0.95f, 0.95f, 0.97f, 1), skin(0.86f, 0.64f, 0.50f, 1), sock(0.85f, 0.18f, 0.20f, 1);
    const float u = h / 10.0f;
    auto part = [&](ImVec2 a, ImVec2 b, ImVec4 base, float r) {   // mitad izquierda y derecha con distinta luz (volumen)
        const float mx = (a.x + b.x) * 0.5f;
        dl->AddRectFilled(a, ImVec2(mx, b.y), ImGui::GetColorU32(shade(-0.8f, 0.3f, base)), r, ImDrawFlags_RoundCornersLeft);
        dl->AddRectFilled(ImVec2(mx, a.y), b, ImGui::GetColorU32(shade(0.8f, 0.3f, base)), r, ImDrawFlags_RoundCornersRight);
    };
    part(ImVec2(feet.x - 1.3f * u, feet.y - 4.6f * u), ImVec2(feet.x - 0.2f * u, feet.y - 0.3f * u), sock, 0.4f * u);   // piernas
    part(ImVec2(feet.x + 0.2f * u, feet.y - 4.6f * u), ImVec2(feet.x + 1.3f * u, feet.y - 0.3f * u), sock, 0.4f * u);
    part(ImVec2(feet.x - 1.5f * u, feet.y - 5.6f * u), ImVec2(feet.x + 1.5f * u, feet.y - 4.2f * u), shorts, 0.5f * u);   // short
    part(ImVec2(feet.x - 1.7f * u, feet.y - 8.4f * u), ImVec2(feet.x + 1.7f * u, feet.y - 5.4f * u), kit, 0.8f * u);   // camiseta
    part(ImVec2(feet.x - 2.4f * u, feet.y - 8.1f * u), ImVec2(feet.x - 1.6f * u, feet.y - 5.4f * u), skin, 0.4f * u);   // brazos
    part(ImVec2(feet.x + 1.6f * u, feet.y - 8.1f * u), ImVec2(feet.x + 2.4f * u, feet.y - 5.4f * u), skin, 0.4f * u);
    const ImVec2 hc(feet.x, feet.y - 9.3f * u);
    dl->AddCircleFilled(hc, 1.0f * u, ImGui::GetColorU32(shade(0.0f, 0.5f, skin)), 20);
    if (L[0] < -0.05f || L[0] > 0.05f) {   // el lado de la cabeza que mira al sol, mas claro
        const float sg = L[0] > 0 ? 1.0f : -1.0f;
        dl->PathArcTo(hc, 0.75f * u, sg > 0 ? -1.2f : kPI - 1.2f, sg > 0 ? 1.2f : kPI + 1.2f, 10);
        dl->PathStroke(ImGui::GetColorU32(shade(sg, 0.2f, skin)), 0, 0.5f * u);
    }
    if (L[2] > 0.25f && day > 0.01f) {   // sol detras (contraluz): borde encendido
        const float rim = Sat((L[2] - 0.25f) * 1.6f) * day * sunI;
        dl->AddCircle(hc, 1.02f * u, Col(sun.x, sun.y * 0.9f, sun.z * 0.7f, 0.8f * rim * A), 20, 1.4f);
        dl->AddRect(ImVec2(feet.x - 1.75f * u, feet.y - 8.45f * u), ImVec2(feet.x + 1.75f * u, feet.y - 5.35f * u), Col(sun.x, sun.y * 0.9f, sun.z * 0.7f, 0.6f * rim * A), 0.8f * u, 0, 1.3f);
    }
    dl->AddRectFilled(ImVec2(feet.x - 1.5f * u, feet.y - 0.4f * u), ImVec2(feet.x - 0.1f * u, feet.y + 0.2f * u), Col(0.08f, 0.08f, 0.09f, A), 0.3f * u);   // botines
    dl->AddRectFilled(ImVec2(feet.x + 0.1f * u, feet.y - 0.4f * u), ImVec2(feet.x + 1.5f * u, feet.y + 0.2f * u), Col(0.08f, 0.08f, 0.09f, A), 0.3f * u);
}
bool MvSunWidget(MvSunIn& in) {
    if (!in.az || !in.el || !in.on) return false;
    const float k = in.k; bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool side = avail >= 520.0f * k;
    const float pvW = side ? std::min(150.0f * k, avail * 0.26f) : 0.0f;
    const float W = side ? avail - pvW - 10.0f * k : avail, Hs = std::clamp(W * 0.36f, 130.0f * k, 190.0f * k);
    const ImVec2 p0 = ImGui::GetCursorScreenPos(), p1(p0.x + W, p0.y + Hs);
    const bool on = *in.on > 0.5f;
    float wantAz = in.relative && in.realOk ? *in.az + in.realAz : *in.az, wantEl = *in.el;
    if (on && in.finalOk) { wantAz = in.finalAz; wantEl = in.finalEl; }
    else if (!on && in.realOk) { wantAz = in.realAz; wantEl = in.realEl; }
    wantAz = WrapDeg(wantAz);
    // franja: x = azimut 0..360 (el centro = 180), y = altura: arriba 90, el horizonte a 3/4 del alto, abajo -15 (pasto)
    const float horY = p0.y + Hs * 0.80f;
    auto X = [&](float az) { return p0.x + WrapDeg(az) / 360.0f * W; };
    auto Y = [&](float el) { return el >= 0 ? horY - (horY - p0.y - 6.0f * k) * (el / 90.0f) : horY - el / 15.0f * (p1.y - horY); };
    const ImGuiID wid = ImGui::GetID("##solpano");
    ImGui::InvisibleButton("##solpano", ImVec2(W, Hs));
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    if (act) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float az = WrapDeg((m.x - p0.x) / W * 360.0f);
        float el = m.y <= horY ? (horY - m.y) / (horY - p0.y - 6.0f * k) * 90.0f : -(m.y - horY) / (p1.y - horY) * 15.0f;
        el = std::clamp(el, -5.0f, 90.0f);
        if (!on) *in.on = 1.0f;
        *in.az = roundf(in.relative && in.realOk ? WrapDeg(az - in.realAz) : az);
        if (!ImGui::GetIO().KeyCtrl) *in.el = roundf(el);
        wantAz = az; wantEl = *in.el; changed = true;
    }
    if (hov && !act && ImGui::GetIO().MouseWheel != 0.0f) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        *in.el = std::clamp(*in.el + ImGui::GetIO().MouseWheel * (ImGui::GetIO().KeyShift ? 0.5f : 2.0f), -5.0f, 90.0f); changed = true;
        if (!on) *in.on = 1.0f;
    }
    // el sol dibujado sigue al valor suave (el azimut por el camino corto, cruzando el borde de la franja)
    const float shAz = Follow(wid + 1, wantAz, 14.0f, in.anim && !act, true), shEl = Follow(wid + 2, wantEl, 14.0f, in.anim && !act);
    const float t = (float)fmod(in.now, 3600.0);
    { float* h = ImGui::GetStateStorage()->GetFloatRef(wid + 3, 0.0f); *h = std::clamp(*h + ((hov || act) ? 1.0f : -1.0f) * ImGui::GetIO().DeltaTime * 6.0f, 0.0f, 1.0f); }
    const float hv = ImGui::GetStateStorage()->GetFloat(wid + 3, 0.0f);
    ImVec4 top, hor; SkyColors(shEl, top, hor);
    const ImVec4 sc = SunColor(in, shEl);
    const float rr = 10.0f * k;
    dl->PushClipRect(p0, p1, true);
    // cielo: degrade, mas un resplandor alrededor del sol (mas grande y calido con el sol bajo)
    dl->AddRectFilledMultiColor(p0, ImVec2(p1.x, horY), ImGui::GetColorU32(top), ImGui::GetColorU32(top), ImGui::GetColorU32(hor), ImGui::GetColorU32(hor));
    const ImVec2 sp(X(shAz), Y(shEl));
    {
        const float low = 1.0f - Smooth(0.0f, 30.0f, shEl), gr = (40.0f + 50.0f * low) * k;
        for (int c = -1; c <= 1; c++) for (int i = 7; i >= 1; i--) dl->AddCircleFilled(ImVec2(sp.x + c * W, sp.y), gr * i / 7.0f, ColA(sc, (0.035f + 0.03f * low) * (on ? 1.0f : 0.6f)), 32);
    }
    // estrellas de noche
    if (shEl < 2.0f) { const float n = 1.0f - Smooth(-6.0f, 2.0f, shEl); unsigned r = 777u; for (int i = 0; i < 40; i++) { r = r * 1664525u + 1013904223u; const float x = p0.x + (r >> 8) % 10000 / 10000.0f * W; r = r * 1664525u + 1013904223u; const float y = p0.y + (r >> 8) % 10000 / 10000.0f * (horY - p0.y) * 0.7f; dl->AddCircleFilled(ImVec2(x, y), 0.9f * k, Col(1, 1, 1, 0.7f * n * (in.anim ? 0.6f + 0.4f * sinf(t * 2.0f + i) : 1.0f)), 6); } }
    // lineas de altura 30 / 60
    for (int e = 30; e <= 60; e += 30) { dl->AddLine(ImVec2(p0.x, Y((float)e)), ImVec2(p1.x, Y((float)e)), Col(1, 1, 1, 0.10f), 1.0f); char b[8]; snprintf(b, sizeof b, "%d", e); dl->AddText(ImVec2(p0.x + 4.0f * k, Y((float)e) - ImGui::GetFontSize() - 1.0f), Col(1, 1, 1, 0.40f), b); }
    // el sol (detras de las tribunas si esta bajo)
    const float pulse = in.anim ? 0.5f + 0.5f * sinf(t * 2.2f) : 0.5f;
    const float sr = (9.0f + 2.0f * hv + (act ? 1.5f : 0.0f)) * k;
    for (int c = -1; c <= 1; c++) SunGlyph(dl, ImVec2(sp.x + c * W, sp.y), sr, on ? sc : ImVec4(sc.x, sc.y, sc.z, 0.6f), in.anim ? t * 0.5f : 0.0f, on ? (0.6f + 0.4f * pulse) * Sat(in.intensity) + 0.3f * hv : 0.3f);
    // tribunas: silueta con el techo, mas alta a los costados; los arcos en el medio de las cabeceras; torres de luz en las esquinas
    {
        const ImVec4 stand(top.x * 0.25f + 0.04f, top.y * 0.25f + 0.05f, top.z * 0.25f + 0.08f, 1);
        const int N = 72; ImVec2 prev;
        for (int i = 0; i <= N; i++) {
            const float az = 360.0f * i / N, x = p0.x + W * i / N, y = Y(StandDeg(az));
            if (i) { dl->AddQuadFilled(prev, ImVec2(x, y), ImVec2(x, horY), ImVec2(prev.x, horY), ImGui::GetColorU32(stand)); dl->AddLine(prev, ImVec2(x, y), Col(1, 1, 1, 0.10f), 1.0f); }
            prev = ImVec2(x, y);
        }
        // filas de publico (puntitos claros)
        unsigned r = 99u;
        for (int i = 0; i < 260; i++) { r = r * 1664525u + 1013904223u; const float az = (r >> 8) % 36000 / 100.0f; r = r * 1664525u + 1013904223u; const float f = (r >> 8) % 1000 / 1000.0f;
            const float y = horY - (horY - Y(StandDeg(az))) * (0.15f + 0.7f * f); dl->AddCircleFilled(ImVec2(X(az), y), 0.8f * k, Col(0.9f, 0.9f, 0.95f, 0.18f + 0.12f * f), 4); }
        for (int c = 0; c < 4; c++) {   // torres de luz en las esquinas (45, 135, 225, 315)
            const float az = 45.0f + 90.0f * c, x = X(az), yt = Y(StandDeg(az) + 9.0f);
            dl->AddLine(ImVec2(x, horY), ImVec2(x, yt), Col(0.6f, 0.62f, 0.68f, 0.7f), 1.4f * k);
            dl->AddRectFilled(ImVec2(x - 5.0f * k, yt - 3.0f * k), ImVec2(x + 5.0f * k, yt + 2.0f * k), shEl < 4.0f ? Col(1.0f, 0.97f, 0.85f, 0.95f) : Col(0.7f, 0.72f, 0.78f, 0.8f), 1.0f);
        }
        static const struct { float az; const char* t; } kMark[4] = { { 0, "Tribuna" }, { 90, "Arco" }, { 180, "Tribuna" }, { 270, "Arco" } };
        for (auto& m : kMark) {
            const float x = X(m.az + 0.001f);
            if (!strcmp(m.t, "Arco")) {   // el arco: dos palos y el travesano sobre el horizonte
                const float gw = 14.0f * k, gh = 7.0f * k;
                dl->AddLine(ImVec2(x - gw, horY), ImVec2(x - gw, horY - gh), IM_COL32_WHITE, 1.5f * k); dl->AddLine(ImVec2(x + gw, horY), ImVec2(x + gw, horY - gh), IM_COL32_WHITE, 1.5f * k);
                dl->AddLine(ImVec2(x - gw, horY - gh), ImVec2(x + gw, horY - gh), IM_COL32_WHITE, 1.5f * k);
            }
            const char* tt = TRS(m.t); const float tw = ImGui::CalcTextSize(tt).x;
            dl->AddText(ImVec2(std::clamp(x - tw * 0.5f, p0.x + 2.0f, p1.x - tw - 2.0f), horY + 3.0f * k), Col(1, 1, 1, 0.65f), tt);
        }
    }
    // pasto (con franjas) debajo del horizonte
    {
        const ImVec4 g0(0.13f, 0.34f, 0.17f, 1), g1(0.15f, 0.39f, 0.19f, 1);
        const float day = Smooth(-4.0f, 8.0f, shEl), gl = 0.45f + 0.55f * day;
        for (int i = 0; i < 12; i++) dl->AddRectFilled(ImVec2(p0.x + W * i / 12.0f, horY), ImVec2(p0.x + W * (i + 1) / 12.0f, p1.y), ImGui::GetColorU32(ImVec4((i % 2 ? g0 : g1).x * gl, (i % 2 ? g0 : g1).y * gl, (i % 2 ? g0 : g1).z * gl, 1)));
        dl->AddLine(ImVec2(p0.x, horY), ImVec2(p1.x, horY), Col(1, 1, 1, 0.25f), 1.0f);
    }
    // el sol del partido (marca) cuando el sol libre esta prendido
    if (on && in.realOk) {
        const ImVec2 rp(X(in.realAz), Y(in.realEl));
        dl->AddCircle(rp, 7.0f * k, Col(1, 1, 1, 0.75f), 20, 1.3f * k);
        dl->AddLine(ImVec2(rp.x, rp.y + 8.0f * k), ImVec2(rp.x, rp.y + 13.0f * k), Col(1, 1, 1, 0.6f), 1.0f);
        dl->AddText(ImVec2(rp.x - ImGui::CalcTextSize(TRS("partido")).x * 0.5f, rp.y + 13.0f * k), Col(1, 1, 1, 0.75f), TRS("partido"));
    }
    if (in.anim && hv > 0.01f && !act) {   // guia: la cruz del mouse sobre la franja
        const ImVec2 m = ImGui::GetIO().MousePos;
        dl->AddLine(ImVec2(m.x, p0.y), ImVec2(m.x, p1.y), Col(1, 1, 1, 0.12f * hv), 1.0f);
        dl->AddLine(ImVec2(p0.x, m.y), ImVec2(p1.x, m.y), Col(1, 1, 1, 0.12f * hv), 1.0f);
    }
    dl->PopClipRect();
    // marco de vidrio
    dl->AddRect(ImVec2(p0.x - 1, p0.y - 1), ImVec2(p1.x + 1, p1.y + 1), ImGui::GetColorU32(ImGuiCol_FrameBg), rr + 1, 0, 2.5f * k);
    dl->AddRect(p0, p1, Col(1, 1, 1, 0.20f + 0.25f * hv), rr, 0, 1.0f);
    dl->AddRectFilledMultiColor(ImVec2(p0.x + 2, p0.y + 2), ImVec2(p1.x - 2, p0.y + Hs * 0.18f), IM_COL32(255, 255, 255, 16), IM_COL32(255, 255, 255, 16), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    {
        char b[96];
        if (on) snprintf(b, sizeof b, TRS("Rumbo %.0f   Altura %.0f"), wantAz, wantEl);
        else snprintf(b, sizeof b, "%s", in.realOk ? TRS("Sol del partido: arrastralo para moverlo") : TRS("Arrastra el sol para moverlo"));
        const ImVec2 ts = ImGui::CalcTextSize(b);
        dl->AddRectFilled(ImVec2(p1.x - ts.x - 16.0f * k, p0.y + 6.0f * k), ImVec2(p1.x - 6.0f * k, p0.y + 10.0f * k + ts.y), Col(0, 0, 0, 0.35f), 6.0f * k);
        dl->AddText(ImVec2(p1.x - ts.x - 11.0f * k, p0.y + 8.0f * k), Col(1, 1, 1, 0.92f), b);
    }
    if (hov && !act) ImGui::SetTooltip("%s", TRS("El cielo visto desde la cancha, la vuelta entera. Arrastra el sol: a los costados = hacia donde esta; arriba / abajo = la altura. Rueda del mouse = subir o bajar. Ctrl = mover solo a los costados. Prende el sol libre solo."));
    if (act) {
        char b[64]; snprintf(b, sizeof b, TRS("Rumbo %.0f  Altura %.0f"), wantAz, *in.el);
        ImDrawList* fg = ImGui::GetForegroundDrawList(); const ImVec2 ts = ImGui::CalcTextSize(b);
        const ImVec2 bp(sp.x - ts.x * 0.5f - 7 * k, sp.y - sr * 2.4f - ts.y - 8 * k);
        fg->AddRectFilled(bp, ImVec2(bp.x + ts.x + 14 * k, bp.y + ts.y + 6 * k), Col(0.10f, 0.12f, 0.16f, 0.92f), 8.0f * k);
        fg->AddText(ImVec2(bp.x + 7 * k, bp.y + 3 * k), IM_COL32_WHITE, b);
    }
    // vista previa: un jugador con esta luz y su sombra
    if (side) {
        const ImVec2 q0(p1.x + 10.0f * k, p0.y), q1(q0.x + pvW, p1.y);
        const float qh = (q1.y - q0.y) * 0.42f;
        dl->AddRectFilledMultiColor(q0, ImVec2(q1.x, q0.y + qh), ImGui::GetColorU32(top), ImGui::GetColorU32(top), ImGui::GetColorU32(hor), ImGui::GetColorU32(hor));
        const float day = Smooth(-4.0f, 6.0f, shEl), sunI = on ? Sat(in.intensity / 1.5f) * 0.6f + 0.4f : 1.0f;
        for (int i = 0; i < 6; i++) {
            const float f0 = i / 6.0f, f1 = (i + 1) / 6.0f, y0 = q0.y + qh + (q1.y - q0.y - qh) * f0 * f0, y1 = q0.y + qh + (q1.y - q0.y - qh) * f1 * f1;
            const float gl = (0.4f + 0.6f * day) * (i % 2 ? 0.92f : 1.0f);
            dl->AddRectFilled(ImVec2(q0.x, y0), ImVec2(q1.x, y1), Col(0.14f * gl + 0.06f * sc.x * day, 0.38f * gl, 0.17f * gl, 1));
        }
        const float L[3] = { cosf(shEl * kDeg) * sinf(shAz * kDeg), sinf(shEl * kDeg), cosf(shEl * kDeg) * cosf(shAz * kDeg) };
        DrawPlayer(dl, ImVec2((q0.x + q1.x) * 0.5f, q1.y - (q1.y - q0.y) * 0.16f), (q1.y - q0.y) * 0.62f, L, sc, Lerp4(top, hor, 0.5f), day, sunI, 1.0f);
        dl->AddRect(ImVec2(q0.x - 1, q0.y - 1), ImVec2(q1.x + 1, q1.y + 1), ImGui::GetColorU32(ImGuiCol_FrameBg), rr + 1, 0, 2.5f * k);
        dl->AddRect(q0, q1, Col(1, 1, 1, 0.20f), rr, 0, 1.0f);
        dl->AddText(ImVec2(q0.x + 7.0f * k, q0.y + 5.0f * k), Col(1, 1, 1, 0.75f), TRS("Vista previa"));
        if (ImGui::IsMouseHoveringRect(q0, q1) && !act) ImGui::SetTooltip("%s", TRS("Un jugador con la luz de este sol y su sombra en el pasto, visto desde la camara de TV. Con el sol de frente a la camara se enciende el borde (contraluz)."));
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y)); ImGui::Dummy(ImVec2(W + pvW, 4.0f * k));
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
// simbolos de los botones como en el mando de PS5: trazos finos y claros sobre un circulo oscuro (sin colores de relleno); al
// apretar se enciende un halo del color de cada simbolo. 'mono' = el gris del mando (sobre el plastico blanco)
static void GlyphShape(ImDrawList* dl, ImVec2 c, float s, int btn, ImU32 sc, float th) {
    if (btn == kPadCross) { dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), sc, th); dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), sc, th); }
    else if (btn == kPadCircle) dl->AddCircle(c, s * 1.08f, sc, 24, th);
    else if (btn == kPadSquare) dl->AddRect(ImVec2(c.x - s * 0.92f, c.y - s * 0.92f), ImVec2(c.x + s * 0.92f, c.y + s * 0.92f), sc, 0, 0, th);
    else dl->AddTriangle(ImVec2(c.x, c.y - s * 1.10f), ImVec2(c.x + s * 1.05f, c.y + s * 0.72f), ImVec2(c.x - s * 1.05f, c.y + s * 0.72f), sc, th);
}
static const ImVec4 kGlyphCol[4] = { ImVec4(0.55f, 0.72f, 1.0f, 1), ImVec4(1.0f, 0.45f, 0.48f, 1), ImVec4(1.0f, 0.60f, 0.88f, 1), ImVec4(0.40f, 0.92f, 0.78f, 1) };
void MvPadGlyph(ImDrawList* dl, ImVec2 c, float r, int btn, float alpha, float lit) {
    const float th = std::max(1.3f, r * 0.17f);
    if (btn >= kPadCross && btn <= kPadTriangle) {
        const ImVec4 cc = kGlyphCol[btn];
        if (lit > 0.01f) for (int i = 3; i >= 1; i--) dl->AddCircleFilled(c, r * (1.0f + 0.22f * i * lit), ColA(cc, 0.10f * lit * alpha), 24);
        dl->AddCircleFilled(c, r, Col(0.06f, 0.07f, 0.09f, 0.88f * alpha), 28);
        dl->AddCircle(c, r - 0.5f, Col(1, 1, 1, 0.22f * alpha), 28, 1.0f);
        GlyphShape(dl, c, r * 0.46f, btn, ColA(Lerp4(ImVec4(0.93f, 0.94f, 0.96f, 1), cc, 0.25f + 0.6f * lit), alpha), th);
        return;
    }
    if (btn >= kPadUp) {   // cruceta
        const float a = r * 0.34f; const ImU32 cl = Col(0.90f, 0.91f, 0.94f, 0.9f * alpha);
        dl->AddRectFilled(ImVec2(c.x - a, c.y - r), ImVec2(c.x + a, c.y + r), cl, a * 0.6f);
        dl->AddRectFilled(ImVec2(c.x - r, c.y - a), ImVec2(c.x + r, c.y + a), cl, a * 0.6f);
        dl->AddCircleFilled(c, a * 0.55f, Col(0.06f, 0.07f, 0.09f, 0.6f * alpha), 12);
        return;
    }
    static const char* kName[kPadCount] = { "", "", "", "", "L1", "R1", "L2", "R2", "CREATE", "OPTIONS", "L3", "R3", "PS", "PAD", "", "", "", "" };
    const char* n = kName[btn]; ImFont* f = ImGui::GetFont(); const float fs = r * 1.15f;
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, n);
    const ImVec2 a(c.x - ts.x * 0.5f - r * 0.5f, c.y - r * 0.78f), b(c.x + ts.x * 0.5f + r * 0.5f, c.y + r * 0.78f);
    dl->AddRectFilled(a, b, Col(0.06f + 0.3f * lit, 0.07f + 0.3f * lit, 0.09f + 0.35f * lit, 0.88f * alpha), r * 0.78f);
    dl->AddRect(a, b, Col(1, 1, 1, 0.25f * alpha), r * 0.78f);
    dl->AddText(f, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), Col(0.93f, 0.94f, 0.96f, alpha), n);
}
// --- dibujo del mando: la forma del mando de PS5 (plastico blanco en las alas y agarres, frente negro con los analogicos, panel
// tactil blanco con la barra de luz a los costados). Todo dibujado con curvas; no es una imagen.
ImVec2 MvPadSize(float k) { return ImVec2(250.0f * k, 158.0f * k); }
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
    const ImVec2 S = MvPadSize(k); const float A = alpha;
    const ImVec2 c0(o.x + S.x * 0.5f, o.y);   // centro de arriba
    const float hw = S.x * 0.5f, H = S.y;
    auto P = [&](float u, float v) { return ImVec2(c0.x + u * hw, c0.y + v * H); };
    const ImVec4 white(0.93f, 0.94f, 0.96f, 1), black(0.07f, 0.08f, 0.10f, 1), blue(0.30f, 0.55f, 1.0f, 1);
    ImVector<ImVec2> raw, sm;
    // gatillos L2 / R2 (detras) y L1 / R1
    for (int s = 0; s < 2; s++) {
        const float sg = s ? 1.0f : -1.0f, fill = s ? p.r2 : p.l2;
        const float tr[][2] = { { 0.50f, 0.05f }, { 0.78f, 0.02f }, { 0.86f, 0.10f }, { 0.80f, 0.16f }, { 0.52f, 0.16f } };
        raw.resize(0); for (auto& q : tr) raw.push_back(P(sg * q[0], q[1]));
        SmoothClosed(raw.Data, raw.Size, sm, 5);
        dl->AddConcavePolyFilled(sm.Data, sm.Size, ColA(Lerp4(ImVec4(0.12f, 0.13f, 0.16f, 0.92f), blue, 0.75f * fill), A));
        dl->AddText(ImVec2(P(sg * 0.67f, 0.0f).x - 8.0f * k, P(0, 0.025f).y), Col(1, 1, 1, (0.45f + 0.5f * fill) * A), s ? "R2" : "L2");
        const float lb = lit(s ? kPadR1 : kPadL1);
        dl->AddRectFilled(P(sg > 0 ? 0.52f : -0.84f, 0.150f), P(sg > 0 ? 0.84f : -0.52f, 0.195f), ColA(Lerp4(white, blue, 0.7f * lb), 0.95f * A), 3.0f * k);
    }
    // cuerpo blanco (alas y agarres)
    static const float kBody[][2] = {
        { 0.00f, 0.20f }, { 0.30f, 0.19f }, { 0.55f, 0.18f }, { 0.74f, 0.19f }, { 0.88f, 0.25f }, { 0.96f, 0.37f }, { 1.00f, 0.52f },
        { 1.00f, 0.68f }, { 0.97f, 0.83f }, { 0.91f, 0.95f }, { 0.83f, 1.00f }, { 0.74f, 0.98f }, { 0.67f, 0.89f }, { 0.60f, 0.76f },
        { 0.50f, 0.67f }, { 0.34f, 0.63f }, { 0.00f, 0.62f } };
    MirrorShape(kBody, IM_ARRAYSIZE(kBody), c0, hw, H, raw);
    SmoothClosed(raw.Data, raw.Size, sm, 6);
    dl->AddConcavePolyFilled(sm.Data, sm.Size, ColA(white, 0.90f * A));
    // sombra suave abajo de las alas (volumen)
    for (int s = 0; s < 2; s++) { const float sg = s ? 1.0f : -1.0f; dl->AddEllipseFilled(P(sg * 0.80f, 0.80f), ImVec2(hw * 0.14f, H * 0.16f), Col(0.55f, 0.58f, 0.64f, 0.20f * A), sg * 0.4f, 24); }
    // frente negro (entre los agarres, con los analogicos)
    static const float kFront[][2] = { { 0.00f, 0.40f }, { 0.30f, 0.40f }, { 0.42f, 0.46f }, { 0.50f, 0.58f }, { 0.48f, 0.66f }, { 0.34f, 0.64f }, { 0.00f, 0.63f } };
    MirrorShape(kFront, IM_ARRAYSIZE(kFront), c0, hw, H, raw);
    SmoothClosed(raw.Data, raw.Size, sm, 6);
    dl->AddConcavePolyFilled(sm.Data, sm.Size, ColA(black, 0.92f * A));
    // panel tactil blanco con la barra de luz a los costados (late suave)
    {
        const float lt = std::max(lit(kPadTouch), lit(kPadPS) * 0.5f);
        static const float kPad[][2] = { { 0.00f, 0.20f }, { 0.31f, 0.20f }, { 0.29f, 0.30f }, { 0.25f, 0.41f }, { 0.00f, 0.42f } };
        MirrorShape(kPad, IM_ARRAYSIZE(kPad), c0, hw, H, raw);
        dl->AddConvexPolyFilled(raw.Data, raw.Size, ColA(Lerp4(ImVec4(0.97f, 0.97f, 0.99f, 1), blue, 0.25f * lt), 0.96f * A));
        dl->AddPolyline(raw.Data, raw.Size, Col(0.70f, 0.72f, 0.78f, 0.6f * A), ImDrawFlags_Closed, 1.0f);
        const float glow = (anim ? 0.55f + 0.45f * sinf((float)now * 1.6f) : 0.8f) * (0.6f + 0.4f * lt);
        for (int s = 0; s < 2; s++) {
            const float sg = s ? 1.0f : -1.0f;
            const ImVec2 a = P(sg * 0.33f, 0.21f), b = P(sg * 0.27f, 0.42f);
            for (int i = 3; i >= 1; i--) dl->AddLine(a, b, ColA(blue, 0.18f * glow * A), (1.5f + 2.2f * i) * k);
            dl->AddLine(a, b, ColA(Lerp4(blue, ImVec4(0.8f, 0.9f, 1, 1), 0.4f), 0.95f * glow * A), 1.6f * k);
        }
        // create / options
        const float ls = lit(kPadShare), lo = lit(kPadOptions);
        dl->AddRectFilled(P(-0.40f, 0.22f), P(-0.36f, 0.29f), ColA(Lerp4(ImVec4(0.55f, 0.57f, 0.62f, 1), blue, ls), 0.9f * A), 2.0f * k);
        dl->AddRectFilled(P(0.36f, 0.22f), P(0.40f, 0.29f), ColA(Lerp4(ImVec4(0.55f, 0.57f, 0.62f, 1), blue, lo), 0.9f * A), 2.0f * k);
    }
    // cruceta (cuatro flechas separadas, como en el de PS5)
    {
        const ImVec2 c = P(-0.62f, 0.40f); const float r = 0.13f * hw, w = 0.045f * hw;
        struct { int b; float dx, dy; } d[4] = { { kPadUp, 0, -1 }, { kPadDown, 0, 1 }, { kPadLeft, -1, 0 }, { kPadRight, 1, 0 } };
        for (auto& q : d) {
            const float lv = lit(q.b);
            const ImVec2 tip(c.x + q.dx * r * 0.30f, c.y + q.dy * r * 0.30f), end(c.x + q.dx * r, c.y + q.dy * r);
            const ImVec2 pr(-q.dy, q.dx);
            const ImVec2 pts[5] = { tip, ImVec2(tip.x + q.dx * w + pr.x * w * 1.2f, tip.y + q.dy * w + pr.y * w * 1.2f), ImVec2(end.x + pr.x * w * 1.2f, end.y + pr.y * w * 1.2f),
                                    ImVec2(end.x - pr.x * w * 1.2f, end.y - pr.y * w * 1.2f), ImVec2(tip.x + q.dx * w - pr.x * w * 1.2f, tip.y + q.dy * w - pr.y * w * 1.2f) };
            dl->AddConvexPolyFilled(pts, 5, ColA(Lerp4(ImVec4(0.80f, 0.82f, 0.86f, 1), blue, 0.8f * lv), 0.95f * A));
            dl->AddPolyline(pts, 5, Col(0.45f, 0.47f, 0.52f, 0.7f * A), ImDrawFlags_Closed, 1.0f);
        }
    }
    // botones de accion: grises claros con el simbolo gris (como el de PS5); el apretado se enciende con su color
    {
        const ImVec2 c = P(0.62f, 0.40f); const float d = 0.11f * hw, r = 0.058f * hw;
        struct { int b; float dx, dy; } q[4] = { { kPadTriangle, 0, -1 }, { kPadCircle, 1, 0 }, { kPadCross, 0, 1 }, { kPadSquare, -1, 0 } };
        for (auto& e : q) {
            const float lv = lit(e.b); const ImVec2 bc(c.x + e.dx * d, c.y + e.dy * d); const ImVec4 cc = kGlyphCol[e.b];
            if (lv > 0.01f) for (int i = 3; i >= 1; i--) dl->AddCircleFilled(bc, r * (1.0f + 0.3f * i * lv), ColA(cc, 0.12f * lv * A), 20);
            dl->AddCircleFilled(ImVec2(bc.x, bc.y + 1.0f * k), r, Col(0.45f, 0.47f, 0.52f, 0.45f * A), 20);
            dl->AddCircleFilled(bc, r, ColA(Lerp4(ImVec4(0.84f, 0.85f, 0.88f, 1), cc, 0.35f * lv), 0.97f * A), 20);
            GlyphShape(dl, bc, r * 0.48f, e.b, ColA(Lerp4(ImVec4(0.40f, 0.42f, 0.47f, 1), ImVec4(cc.x * 0.6f, cc.y * 0.6f, cc.z * 0.6f, 1), lv), A), std::max(1.1f, r * 0.17f));
        }
    }
    // analogicos: base negra, perilla con el borde texturado, corrida segun el stick; arco verde-azul hacia donde se empuja
    for (int s = 0; s < 2; s++) {
        const ImVec2 c = P(s ? 0.22f : -0.22f, 0.53f);
        const float R = 0.105f * hw, sx = s ? p.rx : p.lx, sy = s ? p.ry : p.ly;
        float* fx_ = st->GetFloatRef(base + 40 + s * 2, 0.0f); float* fy_ = st->GetFloatRef(base + 41 + s * 2, 0.0f);
        if (anim) { const float a = 1.0f - expf(-dt * 22.0f); *fx_ += (sx - *fx_) * a; *fy_ += (sy - *fy_) * a; } else { *fx_ = sx; *fy_ = sy; }
        const float lv = lit(s ? kPadR3 : kPadL3), mag = sqrtf(*fx_ * *fx_ + *fy_ * *fy_);
        dl->AddCircleFilled(c, R, Col(0.02f, 0.02f, 0.03f, 0.95f * A), 28);
        if (mag > 0.05f) {
            const float ang = atan2f(*fy_, *fx_);
            dl->PathArcTo(c, R * 0.96f, ang - 0.7f, ang + 0.7f, 12); dl->PathStroke(ColA(blue, 0.85f * Sat(mag) * A), 0, 2.2f * k);
        }
        const ImVec2 kc(c.x + *fx_ * R * 0.45f, c.y + *fy_ * R * 0.45f); const float kr = R * 0.74f * (1.0f - 0.06f * lv);
        dl->AddCircleFilled(ImVec2(kc.x, kc.y + 1.5f * k), kr, Col(0, 0, 0, 0.45f * A), 24);
        dl->AddCircleFilled(kc, kr, ColA(Lerp4(ImVec4(0.14f, 0.15f, 0.18f, 1), blue, 0.35f * lv), 0.98f * A), 24);
        for (int i = 0; i < 16; i++) { const float a = i * (2.0f * kPI / 16); dl->AddCircleFilled(ImVec2(kc.x + cosf(a) * kr * 0.82f, kc.y + sinf(a) * kr * 0.82f), 0.9f * k, Col(0.32f, 0.34f, 0.38f, 0.8f * A), 6); }
        dl->AddCircle(kc, kr * 0.55f, Col(0.28f, 0.30f, 0.34f, 0.8f * A), 20, 1.0f);
    }
    // boton PS y microfono
    {
        const float lp = lit(kPadPS);
        const ImVec2 pc = P(0.0f, 0.535f);
        if (lp > 0.01f) dl->AddCircleFilled(pc, 9.0f * k, ColA(blue, 0.25f * lp * A), 16);
        dl->AddText(ImGui::GetFont(), 10.0f * k, ImVec2(pc.x - 6.0f * k, pc.y - 6.0f * k), ColA(Lerp4(ImVec4(0.75f, 0.77f, 0.82f, 1), blue, lp), A), "PS");
        dl->AddRectFilled(P(-0.035f, 0.590f), P(0.035f, 0.605f), Col(0.25f, 0.27f, 0.31f, 0.9f * A), 2.0f * k);
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
