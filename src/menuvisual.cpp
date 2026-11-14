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
#include <utility>
#include <vector>
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
// (14/11 f) Agustin: "hacer la vista previa de la altura mas detallada" / "la imagen del corner mas detallada". Fondo de costado
// comun: cielo, tribuna con sus gradas, techo y gente, torre de luces, carteles y la cancha con franjas de corte en perspectiva.
// 'light' = cuanto ilumina el sol (0 noche .. 1 mediodia); 'warm' = tinte naranja de la luz baja.
static void SideStadium(ImDrawList* dl, ImVec2 a, ImVec2 b, float ground, ImVec4 top, ImVec4 hor, float light, ImVec4 sunCol, float k) {
    const float W = b.x - a.x;
    dl->AddRectFilledMultiColor(a, ImVec2(b.x, ground), ImGui::GetColorU32(top), ImGui::GetColorU32(top), ImGui::GetColorU32(hor), ImGui::GetColorU32(hor));
    // tribuna del fondo: tres gradas, techo con su sombra, gente (puntitos de colores)
    const float st0 = ground - (b.y - a.y) * 0.18f;
    const ImVec4 stc = Lerp4(ImVec4(0.06f, 0.07f, 0.10f, 1), ImVec4(0.30f, 0.32f, 0.38f, 1), light);
    for (int i = 0; i < 3; i++) {
        const float y0 = st0 + i * (ground - st0) * 0.28f, y1 = st0 + (i + 1) * (ground - st0) * 0.28f;
        dl->AddRectFilled(ImVec2(a.x, y0), ImVec2(b.x, y1), ImGui::GetColorU32(Lerp4(stc, ImVec4(0, 0, 0, 1), 0.15f * i)));
        dl->AddLine(ImVec2(a.x, y1), ImVec2(b.x, y1), ImGui::GetColorU32(Lerp4(stc, ImVec4(1, 1, 1, 1), 0.15f)), 1.0f);
        unsigned sd = 977u + i * 131u;
        for (float x = a.x + 2.0f * k; x < b.x; x += 3.2f * k) {
            sd = sd * 1664525u + 1013904223u; const float r = (sd >> 8) / 16777216.0f;
            static const ImVec4 kFan[4] = { ImVec4(0.85f, 0.20f, 0.20f, 1), ImVec4(0.90f, 0.90f, 0.92f, 1), ImVec4(0.20f, 0.35f, 0.80f, 1), ImVec4(0.95f, 0.80f, 0.25f, 1) };
            dl->AddRectFilled(ImVec2(x, y0 + 1.5f * k + r * 2.0f * k), ImVec2(x + 1.6f * k, y0 + 3.0f * k + r * 2.0f * k), ImGui::GetColorU32(Lerp4(stc, kFan[(sd >> 4) & 3], 0.35f + 0.4f * light)));
        }
    }
    // techo (voladizo) arriba de la tribuna
    dl->AddQuadFilled(ImVec2(a.x, st0 - 7.0f * k), ImVec2(b.x, st0 - 9.0f * k), ImVec2(b.x, st0 - 4.0f * k), ImVec2(a.x, st0 - 2.0f * k), ImGui::GetColorU32(Lerp4(stc, ImVec4(0.8f, 0.82f, 0.86f, 1), 0.25f)));
    dl->AddRectFilledMultiColor(ImVec2(a.x, st0 - 2.0f * k), ImVec2(b.x, st0 + 6.0f * k), IM_COL32(0, 0, 0, 110), IM_COL32(0, 0, 0, 110), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
    // torre de luces (prende de noche)
    const float tx = a.x + W * 0.86f, ty = st0 - (b.y - a.y) * 0.24f;
    dl->AddLine(ImVec2(tx, st0 - 7.0f * k), ImVec2(tx, ty), ImGui::GetColorU32(stc), 2.0f * k);
    dl->AddRectFilled(ImVec2(tx - 7.0f * k, ty - 5.0f * k), ImVec2(tx + 7.0f * k, ty), ImGui::GetColorU32(stc), 1.0f);
    const float on = 1.0f - light;
    for (int i = 0; i < 4; i++) { const ImVec2 lp(tx - 5.0f * k + i * 3.3f * k, ty - 2.5f * k);
        if (on > 0.3f) dl->AddCircleFilled(lp, 4.0f * k, ImGui::GetColorU32(ImVec4(1, 0.97f, 0.85f, 0.15f * on)), 10);
        dl->AddCircleFilled(lp, 1.1f * k, ImGui::GetColorU32(ImVec4(1, 0.97f, 0.85f, 0.4f + 0.6f * on)), 6); }
    // carteles de publicidad al borde de la cancha
    const float ad0 = ground - 6.0f * k;
    for (int i = 0; i < 6; i++) { const float x0 = a.x + i * W / 6.0f, x1 = x0 + W / 6.0f - 1.0f;
        static const ImVec4 kAd[3] = { ImVec4(0.10f, 0.35f, 0.75f, 1), ImVec4(0.80f, 0.15f, 0.20f, 1), ImVec4(0.12f, 0.55f, 0.35f, 1) };
        dl->AddRectFilled(ImVec2(x0, ad0), ImVec2(x1, ground), ImGui::GetColorU32(Lerp4(ImVec4(0.05f, 0.05f, 0.07f, 1), kAd[i % 3], 0.35f + 0.65f * std::max(light, on * 0.8f))));
        dl->AddLine(ImVec2(x0 + 2.0f * k, ad0 + 3.0f * k), ImVec2(x1 - 3.0f * k, ad0 + 3.0f * k), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.35f + 0.3f * light)), 1.0f); }
    // cancha: franjas de corte que se abren hacia adelante (perspectiva) con la luz del sol
    const ImVec4 g0 = Lerp4(ImVec4(0.04f, 0.10f, 0.06f, 1), ImVec4(0.18f, 0.44f, 0.20f, 1), std::max(light, on * 0.55f));
    const ImVec4 g1 = Lerp4(g0, ImVec4(0.30f, 0.60f, 0.30f, 1), 0.18f);
    const ImVec4 gl = Lerp4(g0, sunCol, 0.10f * light);
    float y = ground; float h = 2.0f * k; int i = 0;
    while (y < b.y) {   // franjas con el borde suave (el corte no es una raya dura)
        const float y1 = std::min(b.y, y + h); const ImVec4 c0 = (i & 1) ? g1 : gl, c1 = Lerp4(c0, (i & 1) ? gl : g1, 0.35f);
        dl->AddRectFilledMultiColor(ImVec2(a.x, y), ImVec2(b.x, y1), ImGui::GetColorU32(c0), ImGui::GetColorU32(c0), ImGui::GetColorU32(c1), ImGui::GetColorU32(c1));
        y = y1; h *= 1.55f; i++;
    }
    // bruma del fondo (lejos el pasto se aclara y se pone un poco celeste) y brillo del sol sobre el pasto
    const float depth = b.y - ground;
    dl->AddRectFilledMultiColor(ImVec2(a.x, ground), ImVec2(b.x, ground + depth * 0.35f), ImGui::GetColorU32(ImVec4(hor.x, hor.y, hor.z, 0.30f)), ImGui::GetColorU32(ImVec4(hor.x, hor.y, hor.z, 0.30f)), 0, 0);
    // textura: hojitas cortas por todo el pasto, chicas y apretadas al fondo, mas grandes adelante; cada una con su tono
    unsigned sd = 4242u;
    auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return (sd >> 8) / 16777216.0f; };
    const ImVec4 gd = Lerp4(g0, ImVec4(0, 0, 0, 1), 0.30f), gb = Lerp4(Lerp4(g1, ImVec4(0.55f, 0.75f, 0.35f, 1), 0.35f), sunCol, 0.15f * light);
    for (int n = 0; n < 340; n++) {
        const float f = rnd(), x = a.x + rnd() * (b.x - a.x), yy = ground + f * f * depth;   // mas hojitas cerca del horizonte (se ven chicas)
        const float d = (yy - ground) / std::max(depth, 1.0f), len = (0.8f + 4.5f * d) * k * (0.6f + 0.6f * rnd());
        const ImVec4 cc = Lerp4(gd, gb, rnd()); const float lean2 = (rnd() - 0.5f) * 0.8f;
        dl->AddLine(ImVec2(x, yy), ImVec2(x + lean2 * len, yy - len), ImGui::GetColorU32(ImVec4(cc.x, cc.y, cc.z, 0.55f + 0.3f * d)), (0.7f + 0.7f * d) * k);
    }
}
// (14/11 i) "a las nubes dale un diseno mejor": cumulo con base plana y copetes redondos de distinto tamano, borde suave (capas
// con poca opacidad), luz arriba y gris azulado abajo; 'w' = ancho, 'seed' cambia la forma
static void Cloud(ImDrawList* dl, ImVec2 p, float w, unsigned seed, float alpha, ImVec4 lit, ImVec4 shade) {
    unsigned sd = seed; auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return (sd >> 8) / 16777216.0f; };
    struct B { float x, y, r; } bl[9]; int n = 0;
    for (int i = 0; i < 7; i++) {   // copetes: grandes al medio, chicos a los costados
        const float u = (i + 0.5f) / 7.0f, mid = 1.0f - fabsf(u - 0.5f) * 1.6f;
        const float r = w * (0.10f + 0.12f * mid) * (0.8f + 0.4f * rnd());
        bl[n++] = { p.x + (u - 0.5f) * w * 0.82f, p.y - r * (0.55f + 0.35f * mid), r };
    }
    bl[n++] = { p.x - w * 0.12f, p.y - w * 0.28f * (0.8f + 0.3f * rnd()), w * 0.16f };   // copete alto
    bl[n++] = { p.x + w * 0.10f, p.y - w * 0.22f * (0.8f + 0.3f * rnd()), w * 0.13f };
    const float base = p.y, top = p.y - w * 0.45f;
    // borde suave: tres pasadas que crecen con menos opacidad
    for (int g = 2; g >= 0; g--) {
        const float grow = 1.0f + 0.07f * g, a = alpha * (g == 0 ? 1.0f : 0.18f);
        for (int i = 0; i < n; i++) {
            const int v0 = dl->VtxBuffer.Size;
            dl->AddCircleFilled(ImVec2(bl[i].x, std::min(bl[i].y, base - bl[i].r * 0.2f)), bl[i].r * grow, ImGui::GetColorU32(ImVec4(lit.x, lit.y, lit.z, a)), 20);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(0, top), ImVec2(0, base), ImGui::GetColorU32(ImVec4(lit.x, lit.y, lit.z, 1)), ImGui::GetColorU32(ImVec4(shade.x, shade.y, shade.z, 1)));
        }
    }
    // base plana (los cumulos tienen el fondo derecho) con la sombra de abajo
    dl->AddRectFilledMultiColor(ImVec2(p.x - w * 0.40f, base - w * 0.10f), ImVec2(p.x + w * 0.40f, base), ImGui::GetColorU32(ImVec4(Lerp4(lit, shade, 0.7f).x, Lerp4(lit, shade, 0.7f).y, Lerp4(lit, shade, 0.7f).z, alpha)),
        ImGui::GetColorU32(ImVec4(Lerp4(lit, shade, 0.7f).x, Lerp4(lit, shade, 0.7f).y, Lerp4(lit, shade, 0.7f).z, alpha)), ImGui::GetColorU32(ImVec4(shade.x, shade.y, shade.z, alpha)), ImGui::GetColorU32(ImVec4(shade.x, shade.y, shade.z, alpha)));
    // brillo arriba a la izquierda de los copetes grandes
    for (int i = 2; i < 5; i++) dl->AddCircleFilled(ImVec2(bl[i].x - bl[i].r * 0.25f, bl[i].y - bl[i].r * 0.30f), bl[i].r * 0.45f, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.22f * alpha)), 14);
}
// (14/11 h) "el cesped mejor": hojas de adelante finitas, que se afinan en la punta, de alturas y tonos distintos (algunas puntas
// secas), dobladas por el viento ('lean' -1..1) y moviendose un poco; la punta toma la luz del sol
static void GrassBlades(ImDrawList* dl, float x0, float x1, float yBase, float hMax, int n, float lean, float sway, float tt, float light, ImVec4 sunCol, unsigned seed, float k) {
    unsigned sd = seed;
    auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return (sd >> 8) / 16777216.0f; };
    for (int i = 0; i < n; i++) {
        const float x = x0 + rnd() * (x1 - x0), hh = hMax * (0.45f + 0.55f * rnd()), ph = rnd() * 6.28f, w0 = (1.4f + 1.2f * rnd()) * k;
        const float bendA = lean * (0.55f + 0.45f * rnd()) + (sway > 0.0f ? sinf(tt * (1.8f + sway) + ph) * 0.18f * (0.3f + sway * 0.2f) : 0.0f) + (rnd() - 0.5f) * 0.25f;
        const float tone = rnd(); const bool dry = rnd() < 0.12f;
        const ImVec4 base = Lerp4(ImVec4(0.10f, 0.28f, 0.12f, 1), ImVec4(0.18f, 0.40f, 0.16f, 1), tone);
        ImVec4 tip = Lerp4(ImVec4(0.30f, 0.58f, 0.24f, 1), ImVec4(0.45f, 0.70f, 0.28f, 1), tone);
        if (dry) tip = ImVec4(0.66f, 0.62f, 0.34f, 1);
        tip = Lerp4(tip, sunCol, 0.18f * light);
        const float dim = 0.55f + 0.45f * light;
        // curva: sube y se dobla hacia 'bendA'; 5 tramos que se afinan
        ImVec2 prev(x, yBase);
        for (int sgm = 1; sgm <= 5; sgm++) {
            const float u = sgm / 5.0f, bx = bendA * hh * 0.85f * u * u, by = hh * u * (1.0f - 0.35f * fabsf(bendA) * u);
            const ImVec2 cur(x + bx, yBase - by);
            const ImVec4 c = Lerp4(base, tip, u); 
            dl->AddLine(prev, cur, ImGui::GetColorU32(ImVec4(c.x * dim, c.y * dim, c.z * dim, 1)), std::max(0.6f * k, w0 * (1.0f - 0.8f * u)));
            prev = cur;
        }
    }
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
enum { kSiShadow, kSiTemp, kSiPower, kSiSpin, kSiDay, kSiCloud, kSiHeightCol, kSiCopy, kSiCompass, kSiWave, kSiGust, kSiSwirl, kSiStadium, kSiGrass, kSiShirt, kSiLen, kSiFace, kSiOrbit, kSiNo, kSiFilm, kSiSnail, kSiPitch };
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
    case kSiCompass: {   // brujula con la aguja
        dl->AddCircle(c, s * 0.40f, col, 20, th);
        const float a = -kPI * 0.25f + sinf(t * 1.5f) * 0.15f; const ImVec2 d(cosf(a), sinf(a)), n(-d.y, d.x);
        dl->AddTriangleFilled(ImVec2(c.x + d.x * s * 0.32f, c.y + d.y * s * 0.32f), ImVec2(c.x + n.x * s * 0.09f, c.y + n.y * s * 0.09f), ImVec2(c.x - n.x * s * 0.09f, c.y - n.y * s * 0.09f), col);
        dl->AddTriangleFilled(ImVec2(c.x - d.x * s * 0.32f, c.y - d.y * s * 0.32f), ImVec2(c.x + n.x * s * 0.09f, c.y + n.y * s * 0.09f), ImVec2(c.x - n.x * s * 0.09f, c.y - n.y * s * 0.09f), col & 0x66FFFFFF); } break;
    case kSiWave: {      // linea que sube y baja (el promedio que cambia durante el partido)
        for (int i = 0; i < 10; i++) { const float x0 = -0.42f + i * 0.084f, x1 = x0 + 0.084f;
            dl->AddLine(ImVec2(c.x + x0 * s, c.y + sinf(x0 * 7.0f + t) * s * 0.2f), ImVec2(c.x + x1 * s, c.y + sinf(x1 * 7.0f + t) * s * 0.2f), col, th); } } break;
    case kSiGust: {      // tres rayas de viento, la del medio larga
        const float o = sinf(t * 3.0f) * s * 0.06f;
        dl->AddLine(ImVec2(c.x - s * 0.40f, c.y - s * 0.22f), ImVec2(c.x + s * 0.10f + o, c.y - s * 0.22f), col, th);
        dl->AddLine(ImVec2(c.x - s * 0.40f, c.y), ImVec2(c.x + s * 0.42f + o, c.y), col, th * 1.4f);
        dl->AddLine(ImVec2(c.x - s * 0.40f, c.y + s * 0.22f), ImVec2(c.x + s * 0.02f + o, c.y + s * 0.22f), col, th); } break;
    case kSiSwirl: {     // remolino
        dl->PathClear(); for (int i = 0; i <= 24; i++) { const float u = i / 24.0f, a = u * kPI * 3.2f + t * 2.0f, r = s * (0.06f + 0.36f * u); dl->PathLineTo(ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r)); }
        dl->PathStroke(col, 0, th); } break;
    case kSiStadium:     // tribunas a los costados de la cancha
        dl->AddRectFilled(ImVec2(c.x - s * 0.45f, c.y - s * 0.30f), ImVec2(c.x - s * 0.28f, c.y + s * 0.30f), col, 1.0f);
        dl->AddRectFilled(ImVec2(c.x + s * 0.28f, c.y - s * 0.30f), ImVec2(c.x + s * 0.45f, c.y + s * 0.30f), col, 1.0f);
        dl->AddLine(ImVec2(c.x - s * 0.20f, c.y + s * 0.30f), ImVec2(c.x + s * 0.20f, c.y + s * 0.30f), col, th);
        dl->AddLine(ImVec2(c.x - s * 0.22f, c.y - s * 0.05f), ImVec2(c.x + s * 0.18f, c.y - s * 0.05f), col & 0x88FFFFFF, th); break;
    case kSiGrass:       // hojas de pasto dobladas
        for (int i = 0; i < 4; i++) { const float x = c.x - s * 0.33f + i * s * 0.22f, b = s * (0.10f + 0.06f * sinf(t * 2.0f + i));
            dl->AddBezierQuadratic(ImVec2(x, c.y + s * 0.38f), ImVec2(x, c.y), ImVec2(x + b, c.y - s * (0.28f + 0.06f * (i & 1))), col, th); } break;
    case kSiShirt: {     // camiseta
        const ImVec2 q[8] = { ImVec2(c.x - s * 0.15f, c.y - s * 0.38f), ImVec2(c.x - s * 0.45f, c.y - s * 0.20f), ImVec2(c.x - s * 0.34f, c.y - s * 0.02f), ImVec2(c.x - s * 0.24f, c.y - s * 0.08f),
                              ImVec2(c.x - s * 0.24f, c.y + s * 0.40f), ImVec2(c.x + s * 0.24f, c.y + s * 0.40f), ImVec2(c.x + s * 0.24f, c.y - s * 0.08f), ImVec2(c.x + s * 0.34f, c.y - s * 0.02f) };
        dl->AddPolyline(q, 8, col, 0, th); dl->AddLine(q[7], ImVec2(c.x + s * 0.45f, c.y - s * 0.20f), col, th); dl->AddLine(ImVec2(c.x + s * 0.45f, c.y - s * 0.20f), ImVec2(c.x + s * 0.15f, c.y - s * 0.38f), col, th);
        dl->AddBezierQuadratic(ImVec2(c.x - s * 0.15f, c.y - s * 0.38f), ImVec2(c.x, c.y - s * 0.22f), ImVec2(c.x + s * 0.15f, c.y - s * 0.38f), col, th); } break;
    case kSiLen:         // dos rachas, una corta y una larga
        dl->AddLine(ImVec2(c.x - s * 0.42f, c.y - s * 0.12f), ImVec2(c.x - s * 0.10f, c.y - s * 0.12f), col, th * 1.4f);
        dl->AddLine(ImVec2(c.x - s * 0.42f, c.y + s * 0.14f), ImVec2(c.x + s * 0.42f, c.y + s * 0.14f), col, th * 1.4f);
        dl->AddLine(ImVec2(c.x + s * 0.42f, c.y + s * 0.02f), ImVec2(c.x + s * 0.42f, c.y + s * 0.26f), col, th); break;
    case kSiHeightCol:   // sol bajo naranja sobre la linea
        dl->AddLine(ImVec2(c.x - s * 0.45f, c.y + s * 0.15f), ImVec2(c.x + s * 0.45f, c.y + s * 0.15f), col, th);
        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.15f), s * 0.22f, kPI, kPI * 2.0f, 12); dl->PathFillConvex(IM_COL32(255, 150, 70, (col >> 24) & 255)); break;
    case kSiFace: {   // cara de frente con el marco de la camara
        dl->AddCircle(c, s * 0.30f, col, 18, th); dl->AddCircleFilled(ImVec2(c.x - s * 0.10f, c.y - s * 0.05f), s * 0.035f, col, 6); dl->AddCircleFilled(ImVec2(c.x + s * 0.10f, c.y - s * 0.05f), s * 0.035f, col, 6);
        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.02f), s * 0.14f, kPI * 0.2f, kPI * 0.8f, 8); dl->PathStroke(col, 0, th);
        const float e = s * 0.46f, l = s * 0.14f;
        for (int i = 0; i < 4; i++) { const float sx = (i & 1) ? 1.0f : -1.0f, sy = (i & 2) ? 1.0f : -1.0f;
            dl->AddLine(ImVec2(c.x + sx * e, c.y + sy * e), ImVec2(c.x + sx * (e - l), c.y + sy * e), col, th); dl->AddLine(ImVec2(c.x + sx * e, c.y + sy * e), ImVec2(c.x + sx * e, c.y + sy * (e - l)), col, th); } } break;
    case kSiOrbit: {  // punto con una orbita y la camara dando la vuelta
        dl->AddCircleFilled(c, s * 0.10f, col, 10); dl->AddEllipse(c, ImVec2(s * 0.42f, s * 0.20f), col, -0.3f, 24, th);
        const float a = t * 2.0f; const ImVec2 q(c.x + cosf(a) * s * 0.42f * cosf(-0.3f) - sinf(a) * s * 0.20f * sinf(-0.3f), c.y + cosf(a) * s * 0.42f * sinf(-0.3f) + sinf(a) * s * 0.20f * cosf(-0.3f));
        dl->AddRectFilled(ImVec2(q.x - s * 0.09f, q.y - s * 0.06f), ImVec2(q.x + s * 0.09f, q.y + s * 0.06f), col, 1.0f); } break;
    case kSiNo:       // circulo tachado
        dl->AddCircle(c, s * 0.32f, col, 18, th); dl->AddLine(ImVec2(c.x - s * 0.22f, c.y + s * 0.22f), ImVec2(c.x + s * 0.22f, c.y - s * 0.22f), col, th); break;
    case kSiFilm: {   // pelicula con la flecha de volver (repeticion)
        dl->AddRect(ImVec2(c.x - s * 0.40f, c.y - s * 0.28f), ImVec2(c.x + s * 0.40f, c.y + s * 0.28f), col, 2.0f, 0, th);
        for (int i = 0; i < 4; i++) { const float x = c.x - s * 0.30f + i * s * 0.2f; dl->AddRectFilled(ImVec2(x - s * 0.04f, c.y - s * 0.24f), ImVec2(x + s * 0.04f, c.y - s * 0.17f), col); dl->AddRectFilled(ImVec2(x - s * 0.04f, c.y + s * 0.17f), ImVec2(x + s * 0.04f, c.y + s * 0.24f), col); }
        const float a0 = -t * 2.5f; dl->PathArcTo(c, s * 0.11f, a0, a0 + kPI * 1.5f, 10); dl->PathStroke(col, 0, th); } break;
    case kSiSnail: {  // caracol (camara lenta)
        dl->PathClear(); for (int i = 0; i <= 20; i++) { const float u = i / 20.0f, a = u * kPI * 2.6f, r = s * (0.05f + 0.22f * u); dl->PathLineTo(ImVec2(c.x + s * 0.05f + cosf(a) * r, c.y - s * 0.02f + sinf(a) * r)); }
        dl->PathStroke(col, 0, th);
        dl->AddLine(ImVec2(c.x - s * 0.45f, c.y + s * 0.26f), ImVec2(c.x + s * 0.42f, c.y + s * 0.26f), col, th * 1.3f);
        dl->AddLine(ImVec2(c.x - s * 0.36f, c.y + s * 0.26f), ImVec2(c.x - s * 0.44f, c.y - s * 0.02f), col, th); dl->AddCircleFilled(ImVec2(c.x - s * 0.44f, c.y - s * 0.04f), s * 0.04f, col, 6); } break;
    case kSiPitch:    // cancha chica
        dl->AddRect(ImVec2(c.x - s * 0.42f, c.y - s * 0.28f), ImVec2(c.x + s * 0.42f, c.y + s * 0.28f), col, 1.0f, 0, th);
        dl->AddLine(ImVec2(c.x, c.y - s * 0.28f), ImVec2(c.x, c.y + s * 0.28f), col, th); dl->AddCircle(c, s * 0.10f, col, 12, th); break;
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
        // la camara de TV (abajo, del lado -Z) para orientarse
        dl->AddText(ImVec2(c.x - ImGui::CalcTextSize(TRS("TV")).x * 0.5f, c.y - R - 7.0f * k - ImGui::GetFontSize()), Col(1, 1, 1, 0.55f), TRS("TV"));
        if (on && in.realOk) { const ImVec2 rp(c.x + sinf(in.realAz * kDeg) * R, c.y + cosf(in.realAz * kDeg) * R); dl->AddCircle(rp, 6.0f * k, Col(1, 1, 1, 0.8f), 16, 1.5f * k); }
        const ImVec2 sp(c.x + sinf(shAz * kDeg) * R, c.y + cosf(shAz * kDeg) * R);
        const float pulse = in.anim ? 0.5f + 0.5f * sinf((float)t * 2.2f) : 0.5f;
        SunGlyph(dl, sp, (8.0f + (hovD || actD ? 1.5f : 0.0f)) * k, on ? sc : ImVec4(sc.x, sc.y, sc.z, 0.6f), in.anim ? (float)fmod(t, 3600.0) * 0.5f : 0.0f, on ? 0.6f + 0.4f * pulse : 0.25f);
        if (hovD && !actD) ImGui::SetTooltip("%s", TRS("De que lado viene el sol: arrastralo por el anillo (o la rueda del mouse). La raya negra es la sombra del jugador en la cancha. El circulo blanco es el sol del partido. TV = lado de la camara."));
    }
    // --- 2) la altura: vista de costado, el sol sobre un arco ---
    const float AW = std::clamp(D * 0.95f, 130.0f * k, 190.0f * k);
    ImGui::SetCursorScreenPos(ImVec2(p0.x + D + 8.0f * k, p0.y));
    const ImVec2 a0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##solalt", ImVec2(AW, D));
    const bool hovA = ImGui::IsItemHovered(), actA = ImGui::IsItemActive();
    const ImVec2 base(a0.x + 12.0f * k, a0.y + D - 46.0f * k); const float AR = std::min(AW - 24.0f * k, D - 60.0f * k);
    if (actA) { const ImVec2 m = ImGui::GetIO().MousePos; const float el = atan2f(base.y - m.y, m.x - base.x) / kDeg; setEl(el); wantEl = *in.el; }
    if (hovA && ImGui::GetIO().MouseWheel != 0.0f) { ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); setEl(*in.el + ImGui::GetIO().MouseWheel * (ImGui::GetIO().KeyShift ? 0.5f : 2.0f)); }
    const float shEl = Follow(wid + 2, wantEl, 14.0f, in.anim && !actA);
    {
        SkyColors(shEl, top, hor);
        const float light = Smooth(-6.0f, 25.0f, shEl);
        dl->PushClipRect(a0, ImVec2(a0.x + AW, a0.y + D), true);
        SideStadium(dl, a0, ImVec2(a0.x + AW, a0.y + D), base.y, top, hor, light, sc, k);
        // resplandor del sol en el cielo (mas grande y naranja cuando esta bajo)
        const ImVec2 sp0(base.x + cosf(shEl * kDeg) * AR, base.y - sinf(shEl * kDeg) * AR);
        if (on && shEl > -3.0f) for (int i = 4; i >= 1; i--) dl->AddCircleFilled(sp0, (10.0f + i * (7.0f + 6.0f * (1.0f - light))) * k, ColA(sc, 0.045f * (on ? 1.0f : 0.4f)), 24);
        {   // un par de nubes que toman el color del cielo (naranjas al atardecer, oscuras de noche)
            const ImVec4 cl = Lerp4(Lerp4(hor, ImVec4(1, 1, 1, 1), 0.55f * light + 0.15f), sc, 0.15f * (1.0f - light)), cs = Lerp4(top, hor, 0.5f);
            const float dx = in.anim ? fmodf((float)fmod(t, 3600.0) * 2.0f * k, AW + 60.0f * k) : 0.0f;
            Cloud(dl, ImVec2(a0.x + fmodf(AW * 0.25f + dx, AW + 60.0f * k) - 30.0f * k, a0.y + D * 0.20f), 36.0f * k, 501u, 0.85f, cl, cs);
            Cloud(dl, ImVec2(a0.x + fmodf(AW * 0.80f + dx * 0.7f, AW + 60.0f * k) - 30.0f * k, a0.y + D * 0.33f), 26.0f * k, 733u, 0.70f, cl, cs);
        }
        GrassBlades(dl, a0.x - 4.0f * k, a0.x + AW + 4.0f * k, a0.y + D + 1.0f * k, 12.0f * k, 70, 0.0f, in.anim ? 0.6f : 0.0f, (float)fmod(t, 3600.0), light, sc, 991u, k);
        dl->PathArcTo(base, AR, -kPI * 0.5f, 0.0f, 24); dl->PathStroke(Col(1, 1, 1, 0.30f), 0, 2.0f * k);
        for (int e = 0; e <= 90; e += 15) { const float a = -e * kDeg, l = e % 45 == 0 ? 6.0f : 3.5f;
            dl->AddLine(ImVec2(base.x + cosf(a) * (AR - l * k), base.y + sinf(a) * (AR - l * k)), ImVec2(base.x + cosf(a) * (AR + l * k), base.y + sinf(a) * (AR + l * k)), Col(1, 1, 1, 0.5f), 1.0f);
            if (e % 45 == 0) { char lb[8]; snprintf(lb, sizeof lb, "%d", e); dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.72f, ImVec2(base.x + cosf(a) * (AR - 18.0f * k) - 4.0f * k, base.y + sinf(a) * (AR - 18.0f * k) - 6.0f * k), Col(1, 1, 1, 0.55f), lb); } }
        dl->PopClipRect();
        const ImVec2 sp(base.x + cosf(shEl * kDeg) * AR, base.y - sinf(shEl * kDeg) * AR);
        SunGlyph(dl, sp, (8.0f + (hovA || actA ? 1.5f : 0.0f)) * k, on ? sc : ImVec4(sc.x, sc.y, sc.z, 0.6f), in.anim ? (float)fmod(t, 3600.0) * 0.5f : 0.0f, on ? 0.8f : 0.25f);
        dl->AddRect(a0, ImVec2(a0.x + AW, a0.y + D), Col(1, 1, 1, hovA || actA ? 0.45f : 0.18f), 9.0f * k, 0, 1.0f);
        char b[32]; snprintf(b, sizeof b, TRS("Altura %.0f"), wantEl);
        const ImVec2 tp(a0.x + 7.0f * k, a0.y + D - ImGui::GetFontSize() - 5.0f * k), tsz = ImGui::CalcTextSize(b);
        dl->AddRectFilled(ImVec2(tp.x - 4.0f * k, tp.y - 1.0f * k), ImVec2(tp.x + tsz.x + 4.0f * k, tp.y + tsz.y + 1.0f * k), Col(0, 0, 0, 0.45f), 5.0f * k);
        dl->AddText(tp, IM_COL32_WHITE, b);
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
// particulas que vuelan con el viento dentro del rectangulo a..b (la cancha); largo y velocidad segun los m/s, mas en la racha
static void WindParticles(ImDrawList* dl, ImVec2 a, ImVec2 b, float dirDeg, float spd, float gust, float turb, double now, bool anim, float k) {
    const float W = b.x - a.x, H = b.y - a.y, dx = cosf(dirDeg * kDeg), dy = sinf(dirDeg * kDeg);
    static WindDot s_dots[90]; static bool s_init = false;
    if (!s_init) { unsigned s = 12345u; for (auto& d : s_dots) { s = s * 1664525u + 1013904223u; d.x = (s >> 8) / 16777216.0f; s = s * 1664525u + 1013904223u; d.y = (s >> 8) / 16777216.0f; s = s * 1664525u + 1013904223u; d.life = (s >> 8) / 16777216.0f; d.seed = d.x * 7.0f + d.y * 3.0f; } s_init = true; }
    const int nDots = std::clamp((int)(20 + spd * 7.0f), 20, 90);
    const float dt = anim ? std::min(ImGui::GetIO().DeltaTime, 0.05f) : 0.0f;
    const float v = (0.04f + spd * 0.03f) * (1.0f + 0.6f * gust);
    dl->PushClipRect(a, b, true);
    for (int i = 0; i < nDots; i++) {
        WindDot& d = s_dots[i];
        const float wob = sinf((float)now * (2.0f + turb * 4.0f) + d.seed) * 0.15f * (0.3f + turb);
        d.x += (dx - dy * wob) * v * dt; d.y += (dy + dx * wob) * v * dt * W / std::max(H, 1.0f);
        d.life += dt * 0.5f;
        if (d.x < -0.05f || d.x > 1.05f || d.y < -0.05f || d.y > 1.05f || d.life > 1.0f) {   // vuelve a nacer del lado de donde viene
            unsigned s = (unsigned)(d.seed * 1000.0f + now * 977.0f); s = s * 1664525u + 1013904223u;
            const float r1 = (s >> 8) / 16777216.0f; s = s * 1664525u + 1013904223u; const float r2 = (s >> 8) / 16777216.0f;
            d.x = r1; d.y = r2; d.life = 0.0f;
            if (fabsf(dx) > fabsf(dy)) d.x = dx > 0 ? -0.02f + r2 * 0.3f : 1.02f - r2 * 0.3f; else d.y = dy > 0 ? -0.02f + r1 * 0.3f : 1.02f - r1 * 0.3f;
        }
        const ImVec2 p(a.x + d.x * W, a.y + d.y * H);
        const float tail = (6.0f + spd * 2.2f) * k * (0.7f + 0.6f * gust);
        const float al = 0.55f * sinf(Sat(d.life) * kPI) * Sat(0.25f + spd / 6.0f);
        dl->AddLine(p, ImVec2(p.x - dx * tail, p.y - dy * tail), Col(1, 1, 1, al), 1.4f * k);
    }
    dl->PopClipRect();
}
// (14/11 e) Agustin: "me gusta mas la vista del sol, el viento podes rehacerlo tambien": misma idea que la tarjeta del sol. Arriba los
// preajustes de un clic; abajo el dial (la cancha desde arriba con las particulas; se arrastra la flecha por el anillo = rumbo fijo),
// la vista de costado (banderin y pasto que se doblan; se arrastra la barra = fuerza general) y los botones con dibujo.
bool MvWindWidget(MvWindIn& in) {
    const float k = in.k; bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x;
    const double t = in.now;
    static const float kPreMs[5] = { 2.0f, 0.8f, 3.0f, 6.5f, 11.0f };
    const int preset = in.preset ? std::clamp((int)lroundf(*in.preset), 0, 4) : 0;
    const float strength = in.strength ? *in.strength : 1.0f;
    // --- preajustes de un clic ---
    if (in.preset) {
        static const char* kN[5] = { "Del partido", "Calmo", "Brisa", "Ventoso", "Tormenta" };
        static const ImVec4 kTop[5] = { ImVec4(0.20f, 0.42f, 0.30f, 1), ImVec4(0.32f, 0.58f, 0.72f, 1), ImVec4(0.28f, 0.52f, 0.78f, 1), ImVec4(0.30f, 0.38f, 0.58f, 1), ImVec4(0.26f, 0.24f, 0.36f, 1) };
        static const ImVec4 kBot[5] = { ImVec4(0.12f, 0.28f, 0.18f, 1), ImVec4(0.55f, 0.74f, 0.82f, 1), ImVec4(0.45f, 0.66f, 0.84f, 1), ImVec4(0.42f, 0.48f, 0.62f, 1), ImVec4(0.14f, 0.13f, 0.20f, 1) };
        const float bw = (avail - 4 * 6.0f * k) / 5.0f, h = 30.0f * k;
        for (int i = 0; i < 5; i++) {
            if (i) ImGui::SameLine(0, 6.0f * k);
            ImGui::PushID(i);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##wpre", ImVec2(bw, h))) { *in.preset = (float)i; changed = true; }
            const bool hv = ImGui::IsItemHovered(), cur = preset == i;
            dl->AddRectFilledMultiColor(p, ImVec2(p.x + bw, p.y + h), ImGui::GetColorU32(kTop[i]), ImGui::GetColorU32(kTop[i]), ImGui::GetColorU32(kBot[i]), ImGui::GetColorU32(kBot[i]));
            // rayitas de viento: mas y mas largas con mas viento (se mueven con la animacion)
            dl->PushClipRect(p, ImVec2(p.x + bw, p.y + h), true);
            for (int j = 0; j < i + (i == 0 ? 2 : 1); j++) {
                const float y = p.y + h * (0.25f + 0.5f * ((j * 37) % 10) / 10.0f), L = bw * (0.10f + 0.05f * i);
                const float x = p.x + fmodf((float)(in.anim ? t : 0.0) * bw * (0.15f + 0.12f * i) + j * bw * 0.37f, bw + L) - L * 0.5f;
                dl->AddLine(ImVec2(x, y), ImVec2(x + L, y), Col(1, 1, 1, 0.22f), 1.4f * k);
            }
            dl->PopClipRect();
            dl->AddRect(p, ImVec2(p.x + bw, p.y + h), cur ? Col(0.45f, 0.95f, 0.66f, 1) : Col(1, 1, 1, hv ? 0.5f : 0.18f), 7.0f * k, 0, cur ? 2.0f * k : 1.0f);
            const char* nm = TRS(kN[i]); const ImVec2 ts = ImGui::CalcTextSize(nm);
            dl->AddText(ImVec2(p.x + (bw - ts.x) * 0.5f + 1, p.y + (h - ts.y) * 0.5f + 1), Col(0, 0, 0, 0.6f), nm);
            dl->AddText(ImVec2(p.x + (bw - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), IM_COL32_WHITE, nm);
            if (hv) { if (i == 0) ImGui::SetTooltip("%s", TRS("Cada partido trae su viento: hora, clima, estacion, estadio y azar."));
                      else ImGui::SetTooltip(TRS("%s: unos %.1f m/s (%.0f km/h), por la Fuerza general."), nm, kPreMs[i], kPreMs[i] * 3.6f); }
            ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(0, 4.0f * k));
    }
    const bool wide = avail >= 600.0f * k;
    const float D = std::clamp(avail * (wide ? 0.30f : 0.48f), 150.0f * k, 210.0f * k);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    // --- 1) hacia donde sopla: la cancha desde arriba con el anillo ---
    const bool fixed = in.fixed && *in.fixed > 0.5f;
    float dir = in.live ? in.dirDeg : (in.fixedDeg ? *in.fixedDeg : 0.0f);
    const ImGuiID wid = ImGui::GetID("##vientodial");
    const ImVec2 c(p0.x + D * 0.5f, p0.y + D * 0.5f); const float R = D * 0.5f - 12.0f * k;
    ImGui::InvisibleButton("##vientodial", ImVec2(D, D));
    const bool hovD = ImGui::IsItemHovered(), actD = ImGui::IsItemActive();
    if (actD && in.fixed && in.fixedDeg) {
        const ImVec2 m = ImGui::GetIO().MousePos; const float mx = m.x - c.x, my = m.y - c.y;
        if (mx * mx + my * my > 16.0f * k * k) { float d = atan2f(my, mx) / kDeg; if (d < 0) d += 360.0f; *in.fixed = 1.0f; *in.fixedDeg = roundf(d); dir = *in.fixedDeg; changed = true; }
    }
    if (hovD && in.strength && ImGui::GetIO().MouseWheel != 0.0f) { ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); *in.strength = std::clamp(*in.strength + ImGui::GetIO().MouseWheel * 0.05f, 0.0f, 3.0f); changed = true; }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && in.fixed) { *in.fixed = 0.0f; changed = true; }
    if (fixed && in.fixedDeg && !in.live) dir = *in.fixedDeg;
    const float shDir = Follow(wid + 1, dir, 10.0f, in.anim && !actD, true);
    const float speed = in.live ? in.speed : kPreMs[preset] * strength;
    const float shSpd = Follow(wid + 2, speed, 4.0f, in.anim);
    {
        dl->AddCircleFilled(c, R + 10.0f * k, Col(0, 0, 0, 0.25f), 64);
        dl->AddCircle(c, R, Col(1, 1, 1, hovD || actD ? 0.45f : 0.22f), 64, 10.0f * k);
        for (int i = 0; i < 24; i++) { const float a = i * kPI / 12; const float l = i % 6 == 0 ? 6.0f : 3.0f; dl->AddLine(ImVec2(c.x + cosf(a) * (R - l * k), c.y + sinf(a) * (R - l * k)), ImVec2(c.x + cosf(a) * (R + l * k), c.y + sinf(a) * (R + l * k)), Col(1, 1, 1, 0.35f), 1.0f); }
        const float pw = R * 1.05f, ph = pw * 68.0f / 105.0f;
        const ImVec2 pa(c.x - pw * 0.5f, c.y - ph * 0.5f), pb(c.x + pw * 0.5f, c.y + ph * 0.5f);
        DrawPitchTop(dl, pa, pb, 1.0f);
        WindParticles(dl, pa, pb, shDir, shSpd, in.gust, in.turb, t, in.anim, k);
        // flecha por la cancha (la de 2 s clarita) y la perilla en el anillo, del lado hacia donde sopla
        auto arrow = [&](float deg, float len, ImU32 col, float th) {
            const float r = deg * kDeg, ax = cosf(r), ay = sinf(r);
            const ImVec2 q0(c.x - ax * len * 0.5f, c.y - ay * len * 0.5f), q1(c.x + ax * len * 0.5f, c.y + ay * len * 0.5f);
            dl->AddLine(q0, q1, col, th);
            const float hs = 8.0f * k; dl->AddTriangleFilled(q1, ImVec2(q1.x - ax * hs - ay * hs * 0.6f, q1.y - ay * hs + ax * hs * 0.6f), ImVec2(q1.x - ax * hs + ay * hs * 0.6f, q1.y - ay * hs - ax * hs * 0.6f), col);
        };
        const float L0 = ph * 0.35f, L1 = pw * 0.85f;
        if (in.live) arrow(in.soonDirDeg, L0 + (L1 - L0) * Sat(in.soonSpeed / 12.0f), Col(1, 1, 1, 0.28f), 2.0f * k);
        const float pulse = in.anim ? 0.85f + 0.15f * sinf((float)t * 3.0f) : 1.0f;
        arrow(shDir, L0 + (L1 - L0) * Sat(shSpd / 12.0f), Col(1.0f, 0.92f, 0.47f, 0.92f * pulse), 3.0f * k);
        const ImVec2 kp(c.x + cosf(shDir * kDeg) * R, c.y + sinf(shDir * kDeg) * R);
        dl->AddCircleFilled(kp, (8.0f + (hovD || actD ? 1.5f : 0.0f)) * k, fixed ? Col(1.0f, 0.92f, 0.47f, 1) : Col(0.75f, 0.85f, 0.95f, 0.9f), 20);
        WindGlyph(dl, kp, 11.0f * k, Col(0.10f, 0.12f, 0.16f, 1), in.anim ? (float)fmod(t, 3600.0) : 0.0f);
        const char* lab = TRS(fixed ? "Rumbo fijo" : "Rumbo del partido"); const ImVec2 ts = ImGui::CalcTextSize(lab);
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y + ph * 0.5f + 4.0f * k), Col(1, 1, 1, 0.6f), lab);
        if (hovD && !actD) ImGui::SetTooltip("%s", TRS("Hacia donde sopla: arrastra por el anillo (pasa el Rumbo a Fijo). Clic derecho: el rumbo del partido. Rueda del mouse: Fuerza general. La flecha clarita es el viento de dentro de 2 s."));
    }
    // --- 2) la fuerza: vista de costado con el banderin y el pasto; la barra de la derecha se arrastra ---
    const float AW = std::clamp(D * 0.95f, 130.0f * k, 190.0f * k);
    ImGui::SetCursorScreenPos(ImVec2(p0.x + D + 8.0f * k, p0.y));
    const ImVec2 a0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##vientofuerza", ImVec2(AW, D));
    const bool hovA = ImGui::IsItemHovered(), actA = ImGui::IsItemActive();
    const float gx = a0.x + AW - 14.0f * k, gy0 = a0.y + 12.0f * k, gy1 = a0.y + D - 34.0f * k;   // barra de la fuerza
    if (actA && in.strength) { const float u = Sat((gy1 - ImGui::GetIO().MousePos.y) / (gy1 - gy0)); *in.strength = roundf(u * 3.0f * 20.0f) / 20.0f; changed = true; }
    if (hovA && in.strength && ImGui::GetIO().MouseWheel != 0.0f) { ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); *in.strength = std::clamp(*in.strength + ImGui::GetIO().MouseWheel * 0.05f, 0.0f, 3.0f); changed = true; }
    {
        const float ground = a0.y + D - 44.0f * k;
        const float bend = Sat(shSpd / 12.0f);
        const ImVec4 sk0 = Lerp4(ImVec4(0.22f, 0.45f, 0.80f, 1), ImVec4(0.24f, 0.26f, 0.34f, 1), bend), sk1 = Lerp4(ImVec4(0.62f, 0.78f, 0.93f, 1), ImVec4(0.42f, 0.44f, 0.52f, 1), bend);
        dl->PushClipRect(a0, ImVec2(a0.x + AW, a0.y + D), true);
        SideStadium(dl, a0, ImVec2(a0.x + AW, a0.y + D), ground, sk0, sk1, 1.0f - 0.35f * bend, ImVec4(1.0f, 0.96f, 0.86f, 1), k);
        // de costado se ve la parte del viento que va de izquierda a derecha (cos del rumbo); el signo dice para donde se dobla
        const float side = cosf(shDir * kDeg) >= 0.0f ? 1.0f : -1.0f;
        const float tt = in.anim ? (float)fmod(t, 3600.0) : 0.0f;
        // nubes que pasan mas rapido con mas viento (las de atras mas chicas, lentas y transparentes); con tormenta, mas grises
        for (int i = 2; i >= 0; i--) {
            const float sz = (44.0f - 9.0f * i) * k, sp = (4.0f + shSpd * 4.5f) * k * (1.0f - 0.25f * i);
            const float x = a0.x + fmodf(tt * sp * side + i * AW * 0.43f + AW * 8.0f, AW + sz * 1.2f) - sz * 0.6f, y = a0.y + (24.0f + i * 13.0f) * k;
            const ImVec4 lit = Lerp4(ImVec4(1.0f, 1.0f, 1.0f, 1), ImVec4(0.72f, 0.74f, 0.80f, 1), bend), shd = Lerp4(ImVec4(0.72f, 0.78f, 0.88f, 1), ImVec4(0.38f, 0.40f, 0.48f, 1), bend);
            Cloud(dl, ImVec2(x, y), sz, 77u + i * 31u, 0.92f - 0.2f * i, lit, shd);
        }
        // arco del corner en la cal (en perspectiva) y la linea de fondo
        const ImVec2 cp(a0.x + AW * 0.40f, ground + 18.0f * k);
        dl->AddLine(ImVec2(a0.x, cp.y + 1.0f * k), ImVec2(a0.x + AW, cp.y - 3.0f * k), Col(0.95f, 0.97f, 0.95f, 0.55f), 1.6f * k);
        dl->PathEllipticalArcTo(cp, ImVec2(16.0f * k, 5.0f * k), 0.0f, 0.0f, kPI, 16); dl->PathStroke(Col(0.95f, 0.97f, 0.95f, 0.55f), 0, 1.4f * k);
        // banderin: palo con franjas, tela a cuadros que flamea con luz y sombra segun la ola, y su sombra en el pasto
        const ImVec2 pole(cp.x, cp.y), top(cp.x, ground - D * 0.40f);
        const float FL = 40.0f * k, FH = 26.0f * k, droop = (1.0f - Sat(shSpd / 5.0f)) * 0.9f;   // sin viento cuelga
        const float ang = droop * 1.35f;   // 0 = horizontal, ~1.35 = colgando
        auto fpt = [&](float u, float v) {   // u a lo largo (0 palo .. 1 punta), v de arriba (0) a abajo (1)
            const float wave = sinf(u * 5.5f - tt * (3.0f + shSpd * 1.3f)) * u * FH * 0.14f * (0.35f + bend);
            const float flap = sinf(u * 3.0f - tt * (2.0f + shSpd)) * u * 0.08f * bend;
            const float x = side * cosf(ang + flap) * u * FL, y = sinf(ang + flap) * u * FL + wave;
            const float hh = FH * (1.0f - 0.18f * u) * (1.0f - droop * 0.5f);
            return ImVec2(top.x + x + side * sinf(ang) * v * hh * 0.0f, top.y + 2.0f * k + y + v * hh);
        };
        // sombra de la tela (aplastada en el pasto)
        dl->AddQuadFilled(ImVec2(pole.x, pole.y), ImVec2(pole.x + side * (FL * 0.9f) * cosf(ang), pole.y + 3.0f * k), ImVec2(pole.x + side * FL * 0.7f * cosf(ang), pole.y + 6.0f * k), ImVec2(pole.x - side * 2.0f * k, pole.y + 3.0f * k), Col(0, 0, 0, 0.18f));
        dl->AddLine(ImVec2(pole.x, pole.y), ImVec2(pole.x - side * 10.0f * k, pole.y + 3.0f * k), Col(0, 0, 0, 0.3f), 2.0f * k);
        const int NU = 8, NV = 4;
        for (int i = 0; i < NU; i++) for (int j = 0; j < NV; j++) {
            const float u0 = (float)i / NU, u1 = (float)(i + 1) / NU, v0 = (float)j / NV, v1 = (float)(j + 1) / NV;
            const float slope = cosf((u0 + u1) * 2.75f - tt * (3.0f + shSpd * 1.3f));   // la ola: un lado al sol y el otro a la sombra
            const bool chk = ((i / 2) + (j / 2)) & 1;
            const ImVec4 base2 = chk ? ImVec4(0.95f, 0.92f, 0.20f, 1) : ImVec4(0.92f, 0.20f, 0.18f, 1);
            const ImVec4 cc = Lerp4(base2, slope > 0 ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1), fabsf(slope) * 0.22f * (0.4f + bend));
            dl->AddQuadFilled(fpt(u0, v0), fpt(u1, v0), fpt(u1, v1), fpt(u0, v1), ImGui::GetColorU32(cc));
        }
        // palo con franjas y la punta
        for (int i = 0; i < 6; i++) { const float y0 = top.y + (pole.y - top.y) * i / 6.0f, y1 = top.y + (pole.y - top.y) * (i + 1) / 6.0f;
            dl->AddLine(ImVec2(pole.x, y0), ImVec2(pole.x, y1), (i & 1) ? Col(0.92f, 0.20f, 0.18f, 1) : Col(0.96f, 0.96f, 0.98f, 1), 2.6f * k); }
        dl->AddLine(ImVec2(pole.x + 0.8f * k, top.y), ImVec2(pole.x + 0.8f * k, pole.y), Col(0, 0, 0, 0.25f), 0.8f * k);
        dl->AddCircleFilled(ImVec2(top.x, top.y - 1.5f * k), 2.2f * k, Col(0.96f, 0.96f, 0.98f, 1), 10);
        // pasto de adelante que se dobla con el viento (dos capas: atras mas bajo y oscuro) y hojitas que vuelan con las rachas
        GrassBlades(dl, a0.x - 4.0f * k, a0.x + AW + 4.0f * k, a0.y + D - 8.0f * k, 10.0f * k, 70, side * bend * 1.1f, in.anim ? shSpd * 0.25f : 0.0f, tt, 0.75f, ImVec4(1, 0.96f, 0.86f, 1), 313u, k);
        GrassBlades(dl, a0.x - 4.0f * k, a0.x + AW + 4.0f * k, a0.y + D + 1.0f * k, 15.0f * k, 60, side * bend * 1.3f, in.anim ? shSpd * 0.3f : 0.0f, tt, 1.0f, ImVec4(1, 0.96f, 0.86f, 1), 717u, k);
        const int nLeaf = (int)(bend * 10.0f + in.gust * 4.0f);
        for (int i = 0; i < nLeaf; i++) {
            const float ph = fmodf(tt * (0.25f + shSpd * 0.04f) + i * 0.137f, 1.0f);
            const float x = side > 0 ? a0.x + ph * (AW + 20.0f * k) - 10.0f * k : a0.x + AW - ph * (AW + 20.0f * k) + 10.0f * k;
            const float y = ground - 10.0f * k + sinf(ph * 9.0f + i) * 8.0f * k - i * 2.0f * k;
            const float r = tt * 6.0f + i; const ImVec2 dv(cosf(r) * 2.2f * k, sinf(r) * 1.0f * k);
            dl->AddLine(ImVec2(x - dv.x, y - dv.y), ImVec2(x + dv.x, y + dv.y), (i & 1) ? Col(0.75f, 0.55f, 0.25f, 0.85f) : Col(0.45f, 0.65f, 0.30f, 0.85f), 1.8f * k);
        }
        dl->PopClipRect();
        // barra de la fuerza
        const float u = in.strength ? Sat(*in.strength / 3.0f) : 0.33f, gy = gy1 - (gy1 - gy0) * u;
        dl->AddRectFilled(ImVec2(gx - 3.0f * k, gy0), ImVec2(gx + 3.0f * k, gy1), Col(0, 0, 0, 0.35f), 3.0f * k);
        dl->AddRectFilled(ImVec2(gx - 3.0f * k, gy), ImVec2(gx + 3.0f * k, gy1), Col(0.75f, 0.90f, 1.0f, 0.95f), 3.0f * k);
        dl->AddLine(ImVec2(gx - 6.0f * k, gy1 - (gy1 - gy0) / 3.0f), ImVec2(gx + 6.0f * k, gy1 - (gy1 - gy0) / 3.0f), Col(1, 1, 1, 0.6f), 1.0f);   // 1 = normal
        dl->AddCircleFilled(ImVec2(gx, gy), (6.0f + (hovA || actA ? 1.5f : 0.0f)) * k, Col(0.95f, 0.96f, 0.98f, 1), 16);
        dl->AddRect(a0, ImVec2(a0.x + AW, a0.y + D), Col(1, 1, 1, hovA || actA ? 0.45f : 0.18f), 9.0f * k, 0, 1.0f);
        char b[48]; snprintf(b, sizeof b, "%.1f m/s  x%.2f", shSpd, in.strength ? *in.strength : 1.0f);
        const ImVec2 tp(a0.x + 7.0f * k, a0.y + D - ImGui::GetFontSize() - 5.0f * k), tsz = ImGui::CalcTextSize(b);
        dl->AddRectFilled(ImVec2(tp.x - 4.0f * k, tp.y - 1.0f * k), ImVec2(tp.x + tsz.x + 4.0f * k, tp.y + tsz.y + 1.0f * k), Col(0, 0, 0, 0.45f), 5.0f * k);
        dl->AddText(tp, IM_COL32_WHITE, b);
        if (hovA && !actA) ImGui::SetTooltip("%s", TRS("Fuerza general: arrastra la barra de la derecha (o la rueda del mouse). La raya = 1, normal. El banderin y el pasto muestran cuanto sopla ahora."));
    }
    // --- 3) las demas opciones con botones de icono ---
    const float bwAll = wide ? avail - (D + AW + 18.0f * k) : avail;
    if (wide) ImGui::SetCursorScreenPos(ImVec2(p0.x + D + AW + 18.0f * k, p0.y)); else ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + D + 8.0f * k));
    ImGui::BeginGroup();
    const float cw = (bwAll - 6.0f * k) * 0.5f;
    int col = 0;
    auto next = [&]() { if (++col % 2) ImGui::SameLine(0, 6.0f * k); };
    // chip de 3 valores: clic pasa al siguiente
    auto chip3 = [&](const char* id, int ic, const char* label, float* v, const float* vals, const char* const* names, const char* tip) {
        if (!v) return;
        int cur = 0; for (int i = 1; i < 3; i++) if (fabsf(vals[i] - *v) < fabsf(vals[cur] - *v)) cur = i;
        if (Chip(id, ic, TRS(label), TRS(names[cur]), cur > 0, cw, k, t, in.anim)) { *v = vals[(cur + 1) % 3]; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS(tip));
        next();
    };
    if (in.fixed) {
        if (Chip("##w_rb", kSiCompass, TRS("Rumbo"), TRS(fixed ? "Fijo (el del dial)" : "Del partido"), fixed, cw, k, t, in.anim)) { *in.fixed = fixed ? 0.0f : 1.0f; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Del partido = cada partido trae el suyo. Fijo = siempre el del dial (para grabar videos iguales)."));
        next();
    }
    { static const float v[3] = { 0.0f, 0.5f, 1.0f }; static const char* n[3] = { "Parejo", "Natural", "Mucho" };
      chip3("##w_ch", kSiWave, "Cambio en el partido", in.change, v, n, "El viento medio sube y baja de a poco durante el partido. Clic: parejo / natural / mucho."); }
    { static const float v[3] = { 0.0f, 1.0f, 2.0f }; static const char* n[3] = { "Viento parejo", "Natural", "Muy racheado" };
      chip3("##w_ra", kSiGust, "Rachas", in.gusts, v, n, "Cuanto mas fuerte que el promedio llegan las rachas. Clic: parejo / natural / muy racheado."); }
    { static const float v[3] = { 0.5f, 1.0f, 2.0f }; static const char* n[3] = { "Cortas y seguidas", "Natural", "Largas" };
      chip3("##w_lr", kSiLen, "Largo de las rachas", in.gustLen, v, n, "Rafagas cortas y seguidas, o rachas largas y sostenidas. Clic para cambiar."); }
    { static const float v[3] = { 0.0f, 1.0f, 2.0f }; static const char* n[3] = { "Aire liso", "Natural", "Revuelto" };
      chip3("##w_tu", kSiSwirl, "Turbulencia", in.turbP, v, n, "Los remolinos chicos y rapidos encima de la racha. Clic: liso / natural / revuelto."); }
    { static const float v[3] = { 0.0f, 1.0f, 2.0f }; static const char* n[3] = { "Clavado", "Natural", "Mucho" };
      chip3("##w_gi", kSiSpin, "Giro del rumbo", in.veer, v, n, "Cuanto se corre solo el rumbo. Clic: clavado / natural / mucho."); }
    if (in.shelterMode && in.shelter) {
        const int cur = *in.shelterMode < 0.5f ? 0 : (*in.shelter < 0.25f ? 1 : (*in.shelter > 0.75f ? 3 : 2));
        static const char* n[4] = { "Segun el estadio", "Cancha abierta", "Medio", "Estadio cerrado" };
        if (Chip("##w_re", kSiStadium, TRS("Reparo del estadio"), TRS(n[cur]), cur != 0, cw, k, t, in.anim)) {
            const int nx = (cur + 1) % 4; *in.shelterMode = nx == 0 ? 0.0f : 1.0f; if (nx) *in.shelter = nx == 1 ? 0.0f : nx == 2 ? 0.5f : 1.0f; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Cuanto tapan las tribunas. Segun el estadio = la tabla viento_estadios.txt. Abierta = mas viento y mas liso; cerrado = menos y mas revuelto."));
        next();
    }
    if (col % 2) ImGui::NewLine();
    auto strip = [&](const char* id, int ic, const char* label, float* v, ImVec4 fill) {
        if (!v) return;
        const ImVec2 lp = ImGui::GetCursorScreenPos(); char sub[64]; snprintf(sub, sizeof sub, "%s  %.2f", TRS(label), *v);
        SmallIcon(dl, ImVec2(lp.x + 9.0f * k, lp.y + ImGui::GetFontSize() * 0.5f), 18.0f * k, ic, Col(0.85f, 0.87f, 0.9f, 1), in.anim ? (float)fmod(t, 3600.0) : 0.0f);
        dl->AddText(ImVec2(lp.x + 22.0f * k, lp.y), Col(0.85f, 0.87f, 0.9f, 1), sub); ImGui::Dummy(ImVec2(1, ImGui::GetFontSize() + 2.0f * k));
        if (Strip(id, v, 0.0f, 3.0f, bwAll, k, nullptr, 0, fill, in.anim)) changed = true;
    };
    strip("##w_pa", kSiGrass, "Viento en el pasto", in.grass, ImVec4(0.40f, 0.80f, 0.42f, 1));
    strip("##w_ro", kSiShirt, "Viento en la ropa", in.cloth, ImVec4(0.55f, 0.75f, 1.0f, 1));
    ImGui::EndGroup();
    const float yEnd = std::max(ImGui::GetItemRectMax().y, p0.y + D);
    ImGui::SetCursorScreenPos(ImVec2(p0.x, yEnd + 4.0f * k)); ImGui::Dummy(ImVec2(avail, 1.0f));
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
// --- dibujo del mando (14/11 e). Agustin: "respetar la forma del mando correcta". Los contornos (cuerpo, gatillos, frente gris del
// PS5, panel tactil y puntas de los agarres del PS4) estan CALCADOS de las 3 fotos que mando Agustin (blanco PS5, rojo PS4, rojo
// Xbox): mitad izquierda del contorno espejada, en unidades u = -1..1 (medio ancho) y v = 0..1 (alto con los gatillos). Los botones
// van en el lugar medido sobre las mismas fotos. Dibujo plano con sombra suave, como las fotos; no es una imagen.
static const float kB_ps5[][2] = { { 0.000f, 0.131f }, { 0.266f, 0.134f }, { 0.438f, 0.151f }, { 0.484f, 0.151f }, { 0.499f, 0.124f }, { 0.635f, 0.124f }, { 0.752f, 0.158f }, { 0.757f, 0.178f }, { 0.797f, 0.191f }, { 0.899f, 0.349f }, { 0.944f, 0.450f }, { 0.975f, 0.544f }, { 1.000f, 0.671f }, { 1.000f, 0.815f }, { 0.980f, 0.893f }, { 0.959f, 0.930f }, { 0.899f, 0.977f }, { 0.863f, 0.987f }, { 0.808f, 0.987f }, { 0.742f, 0.993f }, { 0.737f, 1.000f }, { 0.722f, 1.000f }, { 0.691f, 0.973f }, { 0.554f, 0.752f }, { 0.524f, 0.725f }, { 0.478f, 0.698f }, { 0.443f, 0.688f }, { 0.387f, 0.688f }, { 0.352f, 0.695f }, { 0.000f, 0.695f } };
static const float kT_ps5[][2] = { { 0.757f, 0.111f }, { 0.757f, 0.121f }, { 0.651f, 0.121f }, { 0.620f, 0.114f }, { 0.499f, 0.117f }, { 0.499f, 0.091f }, { 0.539f, 0.044f }, { 0.590f, 0.007f }, { 0.610f, 0.000f }, { 0.651f, 0.000f }, { 0.701f, 0.030f }, { 0.737f, 0.074f } };
static const float kB_ps4[][2] = { { 0.000f, 0.179f }, { 0.335f, 0.179f }, { 0.461f, 0.170f }, { 0.755f, 0.175f }, { 0.818f, 0.245f }, { 0.856f, 0.301f }, { 0.956f, 0.550f }, { 0.987f, 0.668f }, { 1.000f, 0.773f }, { 1.000f, 0.834f }, { 0.987f, 0.886f }, { 0.969f, 0.926f }, { 0.944f, 0.952f }, { 0.893f, 0.983f }, { 0.818f, 1.000f }, { 0.743f, 0.996f }, { 0.674f, 0.974f }, { 0.611f, 0.926f }, { 0.567f, 0.869f }, { 0.486f, 0.707f }, { 0.461f, 0.690f }, { 0.392f, 0.721f }, { 0.273f, 0.725f }, { 0.210f, 0.707f }, { 0.172f, 0.686f }, { 0.000f, 0.686f } };
static const float kT_ps4[][2] = { { 0.712f, 0.118f }, { 0.687f, 0.122f }, { 0.561f, 0.118f }, { 0.505f, 0.109f }, { 0.498f, 0.100f }, { 0.561f, 0.013f }, { 0.599f, 0.000f }, { 0.624f, 0.000f }, { 0.674f, 0.026f } };
static const float kB_xb[][2] = { { 0.000f, 0.134f }, { 0.273f, 0.134f }, { 0.289f, 0.127f }, { 0.521f, 0.127f }, { 0.625f, 0.155f }, { 0.675f, 0.182f }, { 0.686f, 0.203f }, { 0.752f, 0.244f }, { 0.791f, 0.296f }, { 0.884f, 0.447f }, { 0.950f, 0.577f }, { 1.000f, 0.739f }, { 1.000f, 0.835f }, { 0.989f, 0.873f }, { 0.956f, 0.924f }, { 0.906f, 0.962f }, { 0.829f, 0.990f }, { 0.791f, 0.990f }, { 0.758f, 0.979f }, { 0.697f, 0.945f }, { 0.631f, 0.893f }, { 0.515f, 0.780f }, { 0.438f, 0.735f }, { 0.377f, 0.722f }, { 0.000f, 0.722f } };
static const float kT_xb[][2] = { { 0.377f, 0.007f }, { 0.394f, 0.003f }, { 0.438f, 0.007f }, { 0.532f, 0.065f }, { 0.543f, 0.076f }, { 0.565f, 0.124f }, { 0.543f, 0.124f }, { 0.444f, 0.103f }, { 0.377f, 0.103f } };
static const float kF_ps5[][2] = { { 0.000f, 0.409f }, { 0.261f, 0.409f }, { 0.306f, 0.396f }, { 0.332f, 0.376f }, { 0.342f, 0.352f }, { 0.362f, 0.352f }, { 0.362f, 0.389f }, { 0.372f, 0.409f }, { 0.423f, 0.456f }, { 0.484f, 0.500f }, { 0.549f, 0.564f }, { 0.651f, 0.695f }, { 0.742f, 0.846f }, { 0.787f, 0.943f }, { 0.808f, 0.970f }, { 0.808f, 0.983f }, { 0.767f, 0.990f }, { 0.722f, 0.990f }, { 0.711f, 0.973f }, { 0.696f, 0.970f }, { 0.691f, 0.936f }, { 0.681f, 0.926f }, { 0.666f, 0.926f }, { 0.666f, 0.906f }, { 0.651f, 0.903f }, { 0.651f, 0.889f }, { 0.641f, 0.886f }, { 0.641f, 0.872f }, { 0.630f, 0.869f }, { 0.630f, 0.856f }, { 0.620f, 0.852f }, { 0.620f, 0.829f }, { 0.605f, 0.826f }, { 0.605f, 0.802f }, { 0.590f, 0.799f }, { 0.590f, 0.785f }, { 0.580f, 0.782f }, { 0.570f, 0.755f }, { 0.544f, 0.732f }, { 0.484f, 0.695f }, { 0.448f, 0.685f }, { 0.382f, 0.685f }, { 0.347f, 0.691f }, { 0.000f, 0.691f } };
static const float kTP_ps4[][2] = { { 0.000f, 0.183f }, { 0.323f, 0.183f }, { 0.323f, 0.419f }, { 0.292f, 0.437f }, { 0.000f, 0.437f } };
static const float kG_ps4[][2] = { { 0.987f, 0.843f }, { 0.962f, 0.921f }, { 0.906f, 0.969f }, { 0.850f, 0.991f }, { 0.781f, 0.996f }, { 0.712f, 0.983f }, { 0.680f, 0.969f }, { 0.618f, 0.921f }, { 0.618f, 0.904f }, { 0.781f, 0.908f }, { 0.837f, 0.900f }, { 0.918f, 0.873f }, { 0.962f, 0.843f } };

static const float kAspect[3] = { 1.326f, 1.393f, 1.247f };   // ancho / alto de cada foto
static const float kTopRoom[3] = { 0.0f, 0.0f, 0.06f };   // Xbox: lugar arriba para los gatillos subidos
ImVec2 MvPadSize(float k) { const float w = 240.0f * k; return ImVec2(w, w / kAspect[s_style] * (1.0f + kTopRoom[s_style])); }
// medio contorno (de arriba al centro de abajo, u >= 0) espejado: contorno cerrado
static void MirrorShape(const float (*half)[2], int n, ImVec2 o, float hw, float H, ImVector<ImVec2>& pts) {
    pts.resize(0);
    for (int i = 0; i < n; i++) pts.push_back(ImVec2(o.x + half[i][0] * hw, o.y + half[i][1] * H));
    for (int i = n - 1; i >= 0; i--) if (half[i][0] > 1e-4f) pts.push_back(ImVec2(o.x - half[i][0] * hw, o.y + half[i][1] * H));
}
// forma de un lado (puntos con u del lado derecho), espejada al lado 'sg'
static void SideShape(const float (*pts)[2], int n, float sg, ImVec2 o, float hw, float H, ImVector<ImVec2>& out) {
    out.resize(0); for (int i = 0; i < n; i++) out.push_back(ImVec2(o.x + sg * pts[i][0] * hw, o.y + pts[i][1] * H));
}
// relleno con degrade vertical (luz arriba, sombra abajo) + borde fino
static void FillShade(ImDrawList* dl, const ImVector<ImVec2>& pts, ImVec4 top, ImVec4 bot, float y0, float y1, float a, ImU32 edge, float edgeW) {
    const int v0 = dl->VtxBuffer.Size;
    dl->AddConcavePolyFilled(pts.Data, pts.Size, ColA(top, a));
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(0, y0), ImVec2(0, y1), ColA(top, 1.0f) | IM_COL32_A_MASK, ColA(bot, 1.0f) | IM_COL32_A_MASK);
    if (edge) dl->AddPolyline(pts.Data, pts.Size, edge, ImDrawFlags_Closed, edgeW);
}
// logo de PlayStation simplificado: la P alta (palo y panza) parada sobre la S acostada, en 'r' de radio
static void PsLogo(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    const float th = std::max(1.0f, r * 0.20f);
    const ImVec2 p0(c.x - r * 0.12f, c.y + r * 0.55f), p1(c.x - r * 0.12f, c.y - r * 0.75f);
    dl->AddLine(p0, p1, col, th * 1.25f);                                                                  // palo de la P
    dl->AddBezierCubic(p1, ImVec2(c.x + r * 0.55f, c.y - r * 0.70f), ImVec2(c.x + r * 0.60f, c.y - r * 0.05f), ImVec2(c.x + r * 0.08f, c.y - r * 0.08f), col, th);   // panza
    // la S acostada: dos trazos finos, uno a cada lado del palo (como en el logo)
    dl->AddBezierQuadratic(ImVec2(c.x - r * 0.25f, c.y + r * 0.30f), ImVec2(c.x - r * 0.85f, c.y + r * 0.42f), ImVec2(c.x - r * 0.50f, c.y + r * 0.62f), col, th * 0.8f);
    dl->AddBezierQuadratic(ImVec2(c.x + r * 0.05f, c.y + r * 0.62f), ImVec2(c.x + r * 0.80f, c.y + r * 0.45f), ImVec2(c.x + r * 0.70f, c.y + r * 0.22f), col, th * 0.8f);
}
// logo de Xbox simplificado: la esfera con la X de dos trazos que se afinan en las puntas y se abren curvos hacia afuera
static void XboxLogo(ImDrawList* dl, ImVec2 c, float r, ImU32 sphere, ImU32 xcol) {
    dl->AddCircleFilled(c, r, sphere, 32);
    for (int s = 0; s < 2; s++) {
        const ImVec2 d(s ? 0.7071f : -0.7071f, 0.7071f), n(-d.y, d.x);   // direccion del trazo y su normal
        ImVec2 q[18];
        for (int i = 0; i < 9; i++) {
            const float u = i / 8.0f, t = -0.86f + 1.72f * u;                 // a lo largo del diametro
            const float w = r * (0.04f + 0.12f * (1.0f - t * t));             // grueso en el medio, fino en las puntas
            const float bow = r * 0.16f * t * t * (t < 0 ? 1.0f : -1.0f);     // las puntas se curvan (como el logo)
            const ImVec2 m(c.x + d.x * t * r + n.x * bow, c.y + d.y * t * r + n.y * bow);
            q[i] = ImVec2(m.x + n.x * w, m.y + n.y * w); q[17 - i] = ImVec2(m.x - n.x * w, m.y - n.y * w);
        }
        dl->AddConcavePolyFilled(q, 18, xcol);
    }
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
    const float hw = S.x * 0.5f, H = S.y / (1.0f + kTopRoom[sty]);
    const ImVec2 c0(o.x + S.x * 0.5f, o.y + S.y - H);   // centro de arriba del cuerpo (el Xbox deja lugar para los gatillos)
    auto P = [&](float u, float v) { return ImVec2(c0.x + u * hw, c0.y + v * H); };
    auto mix = [](ImVec4 a, ImVec4 b, float t) { return Lerp4(a, b, t); };
    const ImVec4 W1(1, 1, 1, 1), K1(0, 0, 0, 1);
    // colores: cuerpo + tonos del mismo color (como las fotos)
    const ImVec4 body = s_body.x >= 0 ? s_body : ps5 ? ImVec4(0.96f, 0.96f, 0.97f, 1) : xb ? ImVec4(0.80f, 0.12f, 0.12f, 1) : ImVec4(0.88f, 0.17f, 0.17f, 1);
    const bool light = body.x * 0.3f + body.y * 0.59f + body.z * 0.11f > 0.6f;
    const ImVec4 hi = mix(body, W1, 0.12f), lo = mix(body, K1, light ? 0.06f : 0.16f), dk = mix(body, K1, light ? 0.30f : 0.32f), deep = mix(body, K1, light ? 0.45f : 0.55f);
    const ImU32 edge = ColA(mix(body, K1, light ? 0.25f : 0.40f), 0.7f * A);
    const ImVec4 accent = xb ? ImVec4(0.45f, 0.90f, 0.40f, 1) : ImVec4(0.30f, 0.58f, 1.0f, 1);
    const ImVec4 grey(0.80f, 0.81f, 0.83f, 1);   // gris del PS5 (frente, analogicos, gatillos)
    ImVector<ImVec2> pts;
    // --- gatillos (L2 / R2) con el L1 / R1 en la base
    for (int s = 0; s < 2; s++) {
        const float sg = s ? 1.0f : -1.0f, fill = s ? p.r2 : p.l2, lb = lit(s ? kPadR1 : kPadL1);
        const float ty = std::max(0.0f, fill) * 0.02f;   // el gatillo apretado baja un poco
        if (xb) {   // (14/11 g) gatillos subidos (la punta sube, la base sigue pegada detras de LB / RB): no se pisan
            SideShape(kT_xb, IM_ARRAYSIZE(kT_xb), sg, ImVec2(c0.x, c0.y + ty * H), hw, H, pts);
            for (int i = 0; i < pts.Size; i++) if (kT_xb[i][1] < 0.09f) pts[i].y -= 0.07f * H;
        }
        else if (ps5) SideShape(kT_ps5, IM_ARRAYSIZE(kT_ps5), sg, ImVec2(c0.x, c0.y + ty * H), hw, H, pts);
        else SideShape(kT_ps4, IM_ARRAYSIZE(kT_ps4), sg, ImVec2(c0.x, c0.y + ty * H), hw, H, pts);
        const ImVec4 tc = ps5 ? grey : mix(body, K1, 0.12f);
        FillShade(dl, pts, mix(mix(tc, W1, 0.12f), accent, 0.75f * fill), mix(mix(tc, K1, 0.10f), accent, 0.6f * fill), P(0, 0).y, P(0, 0.13f).y, 0.96f * A, edge, 1.0f);
        (void)lb;
    }
    // --- cuerpo
    if (xb) MirrorShape(kB_xb, IM_ARRAYSIZE(kB_xb), c0, hw, H, pts); else if (ps5) MirrorShape(kB_ps5, IM_ARRAYSIZE(kB_ps5), c0, hw, H, pts); else MirrorShape(kB_ps4, IM_ARRAYSIZE(kB_ps4), c0, hw, H, pts);
    dl->AddEllipseFilled(P(0, 1.02f), ImVec2(hw * 0.95f, H * 0.045f), Col(0, 0, 0, 0.28f * A), 0.0f, 32);   // sombra en el piso
    // (14/11 g) "un toque mas 3d": el canto del mando (el mismo contorno corrido para abajo, mas oscuro) da el grosor
    {
        ImVector<ImVec2> ex; ex.resize(pts.Size);
        for (int i = 0; i < pts.Size; i++) ex[i] = ImVec2(pts[i].x, pts[i].y + 5.0f * k);
        FillShade(dl, ex, mix(body, K1, light ? 0.30f : 0.45f), mix(body, K1, light ? 0.45f : 0.65f), P(0, 0.4f).y, P(0, 1.0f).y, 0.95f * A, 0, 0);
        for (int i = 0; i < pts.Size; i++) ex[i] = ImVec2(pts[i].x, pts[i].y + 2.5f * k);
        FillShade(dl, ex, mix(body, K1, light ? 0.18f : 0.30f), mix(body, K1, light ? 0.30f : 0.48f), P(0, 0.4f).y, P(0, 1.0f).y, 0.95f * A, 0, 0);
    }
    FillShade(dl, pts, hi, lo, P(0, 0.12f).y, P(0, 1.0f).y, 0.95f * A, edge, 1.1f);
    {   // brillo del borde de arriba (la luz pega de arriba) y un reflejo largo y suave arriba a la izquierda
        const float thr = P(0, 0.34f).y; ImVector<ImVec2> rim;
        int iL = pts.Size - 1; while (iL > 0 && pts[iL].y < thr) iL--;
        for (int i = iL + 1; i < pts.Size; i++) rim.push_back(ImVec2(pts[i].x, pts[i].y + 1.6f * k));
        for (int i = 0; i < pts.Size && pts[i].y < thr; i++) rim.push_back(ImVec2(pts[i].x, pts[i].y + 1.6f * k));
        if (rim.Size > 1) dl->AddPolyline(rim.Data, rim.Size, ColA(W1, (light ? 0.55f : 0.30f) * A), 0, 1.4f * k);
        dl->AddEllipseFilled(P(-0.42f, 0.26f), ImVec2(hw * 0.30f, H * 0.040f), ColA(W1, (light ? 0.14f : 0.10f) * A), -0.12f, 28);
        dl->AddEllipseFilled(P(-0.42f, 0.26f), ImVec2(hw * 0.18f, H * 0.022f), ColA(W1, (light ? 0.12f : 0.08f) * A), -0.12f, 24);
    }
    // --- L1 / R1 (LB / RB): la pieza de abajo del gatillo, medida en las fotos; se ve entera (antes era solo una raya) y se ilumina
    for (int s = 0; s < 2; s++) {
        const float sg = s ? 1.0f : -1.0f, lb = lit(s ? kPadR1 : kPadL1), dn = lb * 0.006f;
        static const float kBpPS5[][2] = { { 0.495f, 0.128f }, { 0.560f, 0.112f }, { 0.650f, 0.110f }, { 0.735f, 0.120f }, { 0.775f, 0.150f }, { 0.770f, 0.168f }, { 0.640f, 0.160f }, { 0.500f, 0.165f } };
        static const float kBpPS4[][2] = { { 0.495f, 0.130f }, { 0.550f, 0.113f }, { 0.640f, 0.110f }, { 0.715f, 0.120f }, { 0.760f, 0.160f }, { 0.745f, 0.178f }, { 0.620f, 0.172f }, { 0.500f, 0.175f } };
        static const float kBpXb[][2] = { { 0.270f, 0.140f }, { 0.360f, 0.118f }, { 0.480f, 0.105f }, { 0.600f, 0.112f }, { 0.700f, 0.140f }, { 0.735f, 0.172f }, { 0.660f, 0.160f }, { 0.500f, 0.148f }, { 0.300f, 0.158f } };
        const float (*bp)[2] = xb ? kBpXb : ps5 ? kBpPS5 : kBpPS4; const int nb = xb ? 9 : 8;
        pts.resize(0); for (int i = 0; i < nb; i++) pts.push_back(P(sg * bp[i][0], bp[i][1] + dn));
        const ImVec4 bc = ps5 ? ImVec4(0.84f, 0.85f, 0.87f, 1) : mix(body, K1, 0.18f);
        FillShade(dl, pts, mix(mix(bc, W1, 0.15f), accent, 0.85f * lb), mix(mix(bc, K1, 0.10f), accent, 0.7f * lb), P(0, 0.10f).y, P(0, 0.18f).y, 0.98f * A, 0, 0);
        dl->AddPolyline(pts.Data, 5, ColA(mix(ps5 ? ImVec4(0.10f, 0.10f, 0.12f, 1) : deep, accent, 0.6f * lb), 0.95f * A), 0, 2.0f * k);   // filo oscuro de arriba
        dl->AddPolyline(pts.Data, pts.Size, ColA(mix(body, K1, light ? 0.35f : 0.5f), 0.6f * A), ImDrawFlags_Closed, 1.0f);
        if (lb > 0.01f) dl->AddPolyline(pts.Data, pts.Size, ColA(accent, 0.5f * lb * A), ImDrawFlags_Closed, 3.0f * k);
    }
    if (ps5) {
        // frente gris (calcado): baja por el borde de adentro de los agarres
        MirrorShape(kF_ps5, IM_ARRAYSIZE(kF_ps5), c0, hw, H, pts);
        FillShade(dl, pts, mix(grey, W1, 0.05f), mix(grey, K1, 0.06f), P(0, 0.40f).y, P(0, 1.0f).y, 0.97f * A, 0, 0);
        // panel tactil: el blanco de arriba con el borde gris, y la barra de luz a los costados (late suave)
        const float lt = std::max(lit(kPadTouch), lit(kPadPS) * 0.4f);
        pts.resize(0);
        pts.push_back(P(-0.425f, 0.150f)); pts.push_back(P(0.425f, 0.150f)); pts.push_back(P(0.405f, 0.300f)); pts.push_back(P(0.375f, 0.395f)); pts.push_back(P(0.345f, 0.410f));
        pts.push_back(P(-0.345f, 0.410f)); pts.push_back(P(-0.375f, 0.395f)); pts.push_back(P(-0.405f, 0.300f));
        dl->AddConvexPolyFilled(pts.Data, pts.Size, ColA(mix(ImVec4(0.985f, 0.985f, 0.99f, 1), accent, 0.22f * lt), 0.97f * A));
        dl->AddPolyline(pts.Data + 1, 6, Col(0.72f, 0.73f, 0.76f, 0.95f * A), 0, 2.2f * k);
        const float glow = (anim ? 0.55f + 0.45f * sinf((float)now * 1.6f) : 0.8f) * (0.5f + 0.5f * lt);
        for (int s = 0; s < 2; s++) {
            const float sg = s ? 1.0f : -1.0f; const ImVec2 a = P(sg * 0.425f, 0.17f), b = P(sg * 0.385f, 0.39f);
            for (int i = 3; i >= 1; i--) dl->AddLine(a, b, ColA(accent, 0.10f * glow * A), (1.5f + 2.0f * i) * k);
            dl->AddLine(a, b, ColA(mix(accent, W1, 0.4f), 0.55f * glow * A), 1.2f * k);
        }
    } else if (!xb) {
        // PS4: panel tactil y puntas de los agarres un tono mas oscuros (calcados)
        const float lt = std::max(lit(kPadTouch), lit(kPadPS) * 0.4f);
        MirrorShape(kTP_ps4, IM_ARRAYSIZE(kTP_ps4), c0, hw, H, pts);
        FillShade(dl, pts, mix(mix(body, K1, 0.30f), accent, 0.25f * lt), mix(body, K1, 0.36f), P(0, 0.18f).y, P(0, 0.44f).y, 0.95f * A, ColA(deep, 0.35f * A), 1.0f);
        for (int s = 0; s < 2; s++) {
            SideShape(kG_ps4, IM_ARRAYSIZE(kG_ps4), s ? 1.0f : -1.0f, c0, hw, H, pts);
            FillShade(dl, pts, mix(body, K1, 0.22f), mix(body, K1, 0.32f), P(0, 0.83f).y, P(0, 1.0f).y, 0.9f * A, 0, 0);
        }
        for (int r = 0; r < 3; r++) for (int i = -3 + (r & 1); i <= 3 - (r & 1); i++) dl->AddCircleFilled(P(i * 0.024f + (r & 1 ? 0.012f : 0.0f) - (r & 1 ? 0.012f : 0.0f), 0.495f + r * 0.022f), 0.9f * k, ColA(deep, 0.75f * A), 6);
    } else {
        // Xbox: el filo claro del borde de adentro de los agarres (sigue el contorno calcado)
        for (int s = 0; s < 2; s++) {
            const float sg = s ? 1.0f : -1.0f; pts.resize(0);
            for (int i = 18; i <= 23; i++) pts.push_back(P(sg * kB_xb[i][0], kB_xb[i][1] - 0.012f));
            dl->AddPolyline(pts.Data, pts.Size, ColA(mix(body, W1, 0.28f), 0.75f * A), 0, 3.0f * k);
        }
    }
    // --- botones del medio: create / share / options (PS) o view / menu (Xbox)
    {
        const float ls = lit(kPadShare), lo2 = lit(kPadOptions);
        if (xb) {
            for (int s = 0; s < 2; s++) { const ImVec2 c = P(s ? 0.17f : -0.17f, 0.31f); const float l = s ? lo2 : ls, r = 0.040f * hw;
                dl->AddCircleFilled(c, r, ColA(mix(dk, accent, 0.8f * l), 0.95f * A), 16); dl->AddCircle(c, r, ColA(deep, 0.8f * A), 16, 1.4f * k); }
        } else {
            const float x = ps5 ? 0.475f : 0.405f, y0 = ps5 ? 0.185f : 0.195f, y1 = ps5 ? 0.235f : 0.255f, w = ps5 ? 0.022f : 0.030f;
            for (int s = 0; s < 2; s++) { const float sg = s ? 1.0f : -1.0f; const float l = s ? lo2 : ls;
                dl->AddRectFilled(P(sg * x - w, y0), P(sg * x + w, y1), ColA(mix(ps5 ? grey : dk, accent, l), 0.95f * A), w * hw); }
        }
    }
    // --- cruceta
    {
        struct { int b; float dx, dy; } d[4] = { { kPadUp, 0, -1 }, { kPadDown, 0, 1 }, { kPadLeft, -1, 0 }, { kPadRight, 1, 0 } };
        if (xb) {
            const ImVec2 c = P(-0.27f, 0.528f); const float L = 0.160f * hw, w = 0.044f * hw;
            dl->AddCircleFilled(c, L * 1.15f, ColA(mix(body, K1, 0.04f), 0.5f * A), 28);
            dl->AddRectFilled(ImVec2(c.x - w, c.y - L), ImVec2(c.x + w, c.y + L), ColA(lo, 0.95f * A), 2.0f * k);
            dl->AddRectFilled(ImVec2(c.x - L, c.y - w), ImVec2(c.x + L, c.y + w), ColA(lo, 0.95f * A), 2.0f * k);
            dl->AddRect(ImVec2(c.x - w, c.y - L), ImVec2(c.x + w, c.y + L), ColA(deep, 0.6f * A), 2.0f * k, 0, 1.2f * k);
            dl->AddRect(ImVec2(c.x - L, c.y - w), ImVec2(c.x + L, c.y + w), ColA(deep, 0.6f * A), 2.0f * k, 0, 1.2f * k);
            for (auto& q : d) { const float lv = lit(q.b); if (lv < 0.01f) continue; const ImVec2 m(c.x + q.dx * L * 0.6f, c.y + q.dy * L * 0.6f);
                const float ex = q.dx ? L * 0.4f : w, ey = q.dy ? L * 0.4f : w;
                dl->AddRectFilled(ImVec2(m.x - ex, m.y - ey), ImVec2(m.x + ex, m.y + ey), ColA(accent, 0.9f * lv * A), 2.0f * k); }
        } else {
            // cuatro teclas separadas (medidas en las fotos): PS5 0.11 de largo x 0.09 de ancho; PS4 mas anchas (0.13) en un hueco redondo
            const ImVec2 c = ps5 ? P(-0.655f, 0.375f) : P(-0.64f, 0.395f);
            const float off = (ps5 ? 0.100f : 0.104f) * hw, hl = (ps5 ? 0.056f : 0.054f) * hw, hwid = (ps5 ? 0.046f : 0.050f) * hw;
            if (!ps5) { dl->AddCircleFilled(c, off + hl * 1.25f, ColA(mix(body, K1, 0.07f), 0.7f * A), 32); dl->AddCircle(c, off + hl * 1.25f, ColA(deep, 0.3f * A), 32, 1.0f); }
            for (auto& q : d) {
                const float lv = lit(q.b);
                const ImVec2 m(c.x + q.dx * off, c.y + q.dy * off), pr(-q.dy, q.dx);
                auto Q = [&](float a, float b) { return ImVec2(m.x + q.dx * a * hl + pr.x * b * hwid, m.y + q.dy * a * hl + pr.y * b * hwid); };
                // de afuera (a = 1) redondeado hacia adentro, con los lados que se cierran en una punta corta
                ImVec2 kp[9] = { Q(-1.0f, 0.0f), Q(-0.55f, 0.85f), Q(0.55f, 1.0f), Q(0.85f, 0.95f), Q(1.0f, 0.6f), Q(1.0f, -0.6f), Q(0.85f, -0.95f), Q(0.55f, -1.0f), Q(-0.55f, -0.85f) };
                const ImVec4 kc = ps5 ? ImVec4(0.93f, 0.93f, 0.95f, 1) : mix(body, W1, 0.06f);
                { ImVec2 kd[9]; for (int j = 0; j < 9; j++) kd[j] = ImVec2(kp[j].x, kp[j].y + 1.8f * k * (1.0f - lv)); dl->AddConvexPolyFilled(kd, 9, ColA(mix(kc, K1, 0.35f), 0.9f * A)); }
                dl->AddConvexPolyFilled(kp, 9, ColA(mix(kc, accent, 0.8f * lv), 0.97f * A));
                dl->AddPolyline(kp, 9, ps5 ? Col(0.62f, 0.63f, 0.67f, 0.95f * A) : ColA(deep, 0.6f * A), ImDrawFlags_Closed, 1.8f * k);
                if (ps5) dl->AddTriangleFilled(Q(0.55f, 0.0f), Q(0.15f, 0.28f), Q(0.15f, -0.28f), Col(0.66f, 0.67f, 0.71f, 0.9f * A));   // flechita
                else dl->AddPolyline(kp, 9, ColA(mix(body, W1, 0.25f), 0.35f * A), ImDrawFlags_Closed, 0.8f * k);
            }
        }
    }
    // --- botones de accion (en el lugar medido de las fotos)
    {
        const ImVec2 c = xb ? P(0.52f, 0.34f) : ps5 ? P(0.65f, 0.36f) : P(0.62f, 0.38f);
        const float d = (xb ? 0.130f : ps5 ? 0.134f : 0.147f) * hw, r = (xb ? 0.069f : ps5 ? 0.064f : 0.062f) * hw;
        struct { int b; float dx, dy; } q[4] = { { kPadTriangle, 0, -1 }, { kPadCircle, 1, 0 }, { kPadCross, 0, 1 }, { kPadSquare, -1, 0 } };
        for (auto& e : q) {
            const float lv = lit(e.b); const ImVec2 bc(c.x + e.dx * d, c.y + e.dy * d); const ImVec4 cc = FaceCol(e.b);
            if (lv > 0.01f) for (int i = 3; i >= 1; i--) dl->AddCircleFilled(bc, r * (1.0f + 0.3f * i * lv), ColA(cc, 0.13f * lv * A), 20);
            const ImVec4 face = ps5 ? ImVec4(0.94f, 0.94f, 0.96f, 1) : mix(body, W1, 0.05f);
            dl->AddCircleFilled(ImVec2(bc.x, bc.y + 1.8f * k * (1.0f - lv)), r, ColA(mix(face, K1, 0.35f), 0.9f * A), 24);   // canto del boton (baja al apretar)
            { const int v0 = dl->VtxBuffer.Size; dl->AddCircleFilled(bc, r, ColA(mix(face, cc, 0.45f * lv), 0.97f * A), 24);
              ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(bc.x, bc.y - r), ImVec2(bc.x, bc.y + r), ColA(mix(mix(face, W1, 0.10f), cc, 0.45f * lv), 1) | IM_COL32_A_MASK, ColA(mix(mix(face, K1, 0.10f), cc, 0.45f * lv), 1) | IM_COL32_A_MASK); }
            dl->AddCircle(bc, r, ps5 ? Col(0.70f, 0.71f, 0.75f, 0.95f * A) : ColA(deep, 0.55f * A), 24, 1.6f * k);
            dl->PathArcTo(bc, r * 0.80f, kPI * 1.12f, kPI * 1.62f, 10); dl->PathStroke(ColA(W1, 0.55f * A), 0, 1.2f * k);   // brillo
            const ImVec4 gc = ps5 ? ImVec4(0.66f, 0.67f, 0.71f, 1) : deep;
            if (xb) XboxLetter(dl, bc, r * 0.55f, e.b, ColA(mix(gc, mix(cc, K1, 0.2f), 0.2f + 0.8f * lv), A));
            else GlyphShape(dl, bc, r * 0.46f, e.b, ColA(mix(gc, mix(cc, K1, 0.3f), lv), A), std::max(1.1f, r * 0.15f));
        }
    }
    // --- analogicos: hueco, perilla corrida segun el stick, arco del color de acento hacia donde se empuja
    for (int s = 0; s < 2; s++) {
        const ImVec2 c = xb ? (s ? P(0.26f, 0.52f) : P(-0.53f, 0.32f)) : ps5 ? P(s ? 0.30f : -0.30f, 0.555f) : P(s ? 0.31f : -0.31f, 0.585f);
        const float R = (xb ? 0.124f : ps5 ? 0.12f : 0.125f) * hw, sx = s ? p.rx : p.lx, sy = s ? p.ry : p.ly;
        float* fx_ = st->GetFloatRef(base + 40 + s * 2, 0.0f); float* fy_ = st->GetFloatRef(base + 41 + s * 2, 0.0f);
        if (anim) { const float a = 1.0f - expf(-dt * 22.0f); *fx_ += (sx - *fx_) * a; *fy_ += (sy - *fy_) * a; } else { *fx_ = sx; *fy_ = sy; }
        const float lv = lit(s ? kPadR3 : kPadL3), mag = sqrtf(*fx_ * *fx_ + *fy_ * *fy_);
        const ImVec4 well = ps5 ? ImVec4(0.74f, 0.75f, 0.78f, 1) : mix(body, K1, 0.30f), cap = ps5 ? ImVec4(0.58f, 0.59f, 0.63f, 1) : mix(body, K1, 0.42f);
        dl->AddCircleFilled(c, R, ColA(well, 0.95f * A), 32);
        if (xb) dl->AddCircle(c, R, ColA(mix(body, W1, 0.25f), 0.6f * A), 32, 1.6f * k);
        if (mag > 0.05f) { const float ang = atan2f(*fy_, *fx_); dl->PathArcTo(c, R * 0.97f, ang - 0.7f, ang + 0.7f, 12); dl->PathStroke(ColA(accent, 0.9f * Sat(mag) * A), 0, 2.2f * k); }
        const ImVec2 kc(c.x + *fx_ * R * 0.30f, c.y + *fy_ * R * 0.30f); const float kr = R * 0.70f * (1.0f - 0.07f * lv);
        dl->AddCircleFilled(ImVec2(kc.x, kc.y + 1.5f * k), kr, ColA(K1, 0.25f * A), 24);
        const int v0 = dl->VtxBuffer.Size;
        dl->AddCircleFilled(kc, kr, ColA(mix(cap, accent, 0.35f * lv), 0.98f * A), 24);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(kc.x, kc.y - kr), ImVec2(kc.x, kc.y + kr), ColA(mix(mix(cap, W1, 0.12f), accent, 0.35f * lv), 1) | IM_COL32_A_MASK, ColA(mix(mix(cap, K1, 0.10f), accent, 0.35f * lv), 1) | IM_COL32_A_MASK);
        dl->AddCircle(kc, kr * 0.62f, ColA(mix(cap, K1, 0.15f), 0.6f * A), 20, 1.2f * k);
        dl->PathArcTo(c, R * 0.96f, kPI * 1.05f, kPI * 1.95f, 16); dl->PathStroke(ColA(K1, 0.30f * A), 0, 2.2f * k);   // el hueco: sombra arriba
        dl->PathArcTo(kc, kr * 0.88f, kPI * 1.10f, kPI * 1.60f, 12); dl->PathStroke(ColA(W1, 0.40f * A), 0, 1.4f * k);   // brillo de la perilla
    }
    // --- boton central: PS (PS5 en el frente gris, PS4 negro redondo) o el boton Xbox que brilla
    {
        const float lp = lit(kPadPS);
        if (xb) {
            const ImVec2 c = P(0.0f, 0.212f); const float r = 0.072f * hw;
            const float gl = (anim ? 0.75f + 0.25f * sinf((float)now * 1.4f) : 0.9f) * (0.7f + 0.3f * lp);
            for (int i = 3; i >= 1; i--) dl->AddCircleFilled(c, r * (1.0f + 0.28f * i), Col(1, 1, 1, 0.08f * gl * A), 28);
            XboxLogo(dl, c, r, ColA(mix(ImVec4(0.96f, 0.96f, 0.97f, 1), accent, 0.5f * lp), 0.97f * A), Col(0.62f, 0.63f, 0.67f, 0.95f * A));
        } else if (ps5) {
            const ImVec2 pc = P(0.0f, 0.600f); const float r = 0.050f * hw;
            if (lp > 0.01f) dl->AddCircleFilled(pc, r * 1.4f, ColA(accent, 0.30f * lp * A), 20);
            PsLogo(dl, pc, r, ColA(mix(ImVec4(0.64f, 0.65f, 0.69f, 1), accent, lp), A));
            for (int r2 = 0; r2 < 2; r2++) for (int i = -4; i <= 4; i++) dl->AddCircleFilled(P(i * 0.020f, 0.475f + r2 * 0.022f), 0.9f * k, Col(0.66f, 0.67f, 0.71f, 0.9f * A), 6);
        } else {
            const ImVec2 pc = P(0.0f, 0.59f); const float r = 0.048f * hw;
            dl->AddCircleFilled(pc, r, ColA(mix(ImVec4(0.05f, 0.05f, 0.07f, 1), accent, 0.6f * lp), 0.97f * A), 24);
            dl->AddCircle(pc, r, Col(0.75f, 0.75f, 0.78f, 0.7f * A), 24, 1.2f * k);
            PsLogo(dl, pc, r * 0.78f, Col(0.95f, 0.95f, 0.97f, A));
        }
    }
}
void MvPadHints(float k, float alpha) {
    struct { int b; const char* t; } h[] = { { kPadCross, "Elegir" }, { kPadCircle, "Volver" }, { kPadTriangle, "Escribir" }, { kPadOptions, "Buscar" }, { kPadSquare, "Ventanas" }, { kPadUp, "Moverse" }, { kPadL1, "Fino / rapido" } };
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

// ------------------------------------------------------------------------------------------------------------------------------
// (14/11) TECLADO EN PANTALLA (Agustin: "deberias agregar un teclado virtual para buscar con el mando"). Ventana propia arriba de todo,
// sin navegacion de ImGui (el mando lo maneja esta funcion): la tecla elegida tiene un marco que se desliza y cada tecla se hunde al
// apretarla. Se escribe directo en el texto del buscador (el campo de ImGui se suelta mientras tanto).
static bool s_vkOpen = false, s_vkJustOpened = false;
static int s_vkRow = 2, s_vkCol = 0, s_vkSug = 0, s_vkPage = 0;   // filas: 0 sugerencias, 1..4 teclas, 5 fila de abajo
static float s_vkSelX = -1, s_vkSelY = -1, s_vkSelW = 0, s_vkVis = 0;
static double s_vkPressT[80] = {}, s_vkOpenT = -10.0;
static bool s_vkPrevB[kPadCount] = {}; static double s_vkRepT[4] = {}; static bool s_vkDirWas[4] = {};
static std::string s_vkRecent[4];   // ultimas busquedas (de esta sesion)
void MvVkOpen() { if (!s_vkOpen) { s_vkOpen = true; s_vkJustOpened = true; s_vkOpenT = ImGui::GetTime(); s_vkRow = 2; s_vkCol = 0; } }
void MvVkClose() { s_vkOpen = false; }
bool MvVkIsOpen() { return s_vkOpen; }
bool MvVkVisible() { return s_vkOpen || s_vkVis > 0.0f; }
// dos paginas: letras y simbolos / acentos (R1 o la tecla ?123 cambia)
static const char* const kVkRows[2][4][10] = {
  { { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
    { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
    { "a", "s", "d", "f", "g", "h", "j", "k", "l", "\xc3\xb1" },
    { "z", "x", "c", "v", "b", "n", "m", ",", ".", "-" } },
  { { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
    { "\xc3\xa1", "\xc3\xa9", "\xc3\xad", "\xc3\xb3", "\xc3\xba", "\xc3\xbc", "\xc3\xa7", "'", "\"", "?" },
    { "@", "#", "%", "&", "*", "+", "=", "/", "_", ":" },
    { "(", ")", "[", "]", "<", ">", "!", ";", "~", "|" } } };
enum { kVkPage = 0, kVkSpace, kVkBack, kVkDone };   // fila de abajo: ?123 (2) | espacio (4) | borrar (2) | listo (2)
static const int kVkSpC0[4] = { 0, 2, 6, 8 }, kVkSpN[4] = { 2, 4, 2, 2 };
static int VkSpecialAt(int col) { return col < 2 ? kVkPage : col < 6 ? kVkSpace : col < 8 ? kVkBack : kVkDone; }
static const int kVkClear = 4;   // id 40 + 4: borrar todo (tacho del campo / L1)
static void VkBackspace(char* buf) { int n = (int)strlen(buf); if (!n) return; n--; while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) n--; buf[n] = 0; }
static bool VkAppend(char* buf, int cap, const char* t) { const int n = (int)strlen(buf), m = (int)strlen(t); if (n + m >= cap) return false; memcpy(buf + n, t, m + 1); return true; }
static float VkEase(float x) { x = Sat(x); return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x); }
// iconos dibujados de las teclas especiales (centro c, tamano s = alto de la letra)
static void VkIcon(ImDrawList* dl, int which, ImVec2 c, float s, ImU32 col, float th) {
    switch (which) {
    case kVkSpace: {   // barra de espacio: corchete acostado
        const float w = s * 0.46f, h = s * 0.30f;
        const ImVec2 p[4] = { ImVec2(c.x - w, c.y - h * 0.4f), ImVec2(c.x - w, c.y + h * 0.6f), ImVec2(c.x + w, c.y + h * 0.6f), ImVec2(c.x + w, c.y - h * 0.4f) };
        dl->AddPolyline(p, 4, col, 0, th); break; }
    case kVkBack: {    // borrar: etiqueta con punta a la izquierda y una cruz
        const float w = s * 0.62f, h = s * 0.36f, t = s * 0.28f;
        const ImVec2 p[5] = { ImVec2(c.x - w - t * 0.6f, c.y), ImVec2(c.x - w + t * 0.4f, c.y - h), ImVec2(c.x + w, c.y - h), ImVec2(c.x + w, c.y + h), ImVec2(c.x - w + t * 0.4f, c.y + h) };
        dl->AddPolyline(p, 5, col, ImDrawFlags_Closed, th);
        const float xc = c.x + w * 0.22f, xr = h * 0.42f;
        dl->AddLine(ImVec2(xc - xr, c.y - xr), ImVec2(xc + xr, c.y + xr), col, th); dl->AddLine(ImVec2(xc - xr, c.y + xr), ImVec2(xc + xr, c.y - xr), col, th); break; }
    case kVkDone: {    // listo: tilde
        dl->AddLine(ImVec2(c.x - s * 0.42f, c.y + s * 0.02f), ImVec2(c.x - s * 0.12f, c.y + s * 0.30f), col, th * 1.3f);
        dl->AddLine(ImVec2(c.x - s * 0.12f, c.y + s * 0.30f), ImVec2(c.x + s * 0.46f, c.y - s * 0.30f), col, th * 1.3f); break; }
    case kVkClear: {   // tacho
        const float w = s * 0.30f, h = s * 0.40f;
        dl->AddLine(ImVec2(c.x - w * 1.35f, c.y - h * 0.62f), ImVec2(c.x + w * 1.35f, c.y - h * 0.62f), col, th);
        dl->AddLine(ImVec2(c.x - w * 0.45f, c.y - h * 0.62f), ImVec2(c.x - w * 0.35f, c.y - h * 0.95f), col, th); dl->AddLine(ImVec2(c.x - w * 0.35f, c.y - h * 0.95f), ImVec2(c.x + w * 0.35f, c.y - h * 0.95f), col, th); dl->AddLine(ImVec2(c.x + w * 0.35f, c.y - h * 0.95f), ImVec2(c.x + w * 0.45f, c.y - h * 0.62f), col, th);
        const ImVec2 p[4] = { ImVec2(c.x - w, c.y - h * 0.4f), ImVec2(c.x - w * 0.8f, c.y + h), ImVec2(c.x + w * 0.8f, c.y + h), ImVec2(c.x + w, c.y - h * 0.4f) };
        dl->AddPolyline(p, 4, col, 0, th);
        dl->AddLine(ImVec2(c.x, c.y - h * 0.2f), ImVec2(c.x, c.y + h * 0.75f), col, th * 0.8f); break; }
    case 10: {         // reloj (busquedas recientes)
        dl->AddCircle(c, s * 0.36f, col, 20, th); dl->AddLine(c, ImVec2(c.x, c.y - s * 0.22f), col, th); dl->AddLine(c, ImVec2(c.x + s * 0.16f, c.y + s * 0.08f), col, th); break; }
    case 11: {         // lupa chica (palabra sugerida)
        const ImVec2 lc(c.x - s * 0.06f, c.y - s * 0.06f); dl->AddCircle(lc, s * 0.24f, col, 16, th);
        dl->AddLine(ImVec2(lc.x + s * 0.17f, lc.y + s * 0.17f), ImVec2(lc.x + s * 0.36f, lc.y + s * 0.36f), col, th * 1.2f); break; }
    }
}

// (14/11 b, Agustin: "para el teclado manten el diseno glass y colores, asi como la animacion al abrir con l3+r3, una animacion y mostrar
// los botones para abrir y cerrar") el fondo es el mismo vidrio del menu (backdrop = GlassBackdrop de overlay.cpp), las teclas son
// paneles translucidos con el verde del menu; al abrir sube, crece desde el 96 % y las filas entran en cascada; al cerrar se desvanece.
// (14/11 c, Agustin: "el teclado me parece feo, has las teclas mas pequenas, los textos mas grandes, mejora los iconos, y dales un
// diseno mas alineado y mas opciones") v2: teclas cuadradas chicas en una grilla de 10 columnas donde TODO se alinea (cabecera, campo,
// sugerencias, fila de abajo y ayuda usan los mismos bordes), letras grandes, iconos dibujados (espacio, borrar, listo, tacho, reloj),
// y opciones nuevas: pagina de simbolos y acentos (?123 / R1), fila de sugerencias (busquedas recientes + palabras del menu), tacho en el
// campo (L1), contador de letras.
bool MvVirtualKeyboard(char* buf, int cap, const MvPad& p, float k, float alpha, bool anim, void (*backdrop)(ImVec2, ImVec2, float, float)) {
    ImGuiIO& io = ImGui::GetIO(); const float dt = io.DeltaTime; const double now = ImGui::GetTime();
    s_vkVis = anim ? (s_vkOpen ? std::min(1.0f, s_vkVis + dt / 0.22f) : std::max(0.0f, s_vkVis - dt / 0.16f)) : (s_vkOpen ? 1.0f : 0.0f);
    if (!s_vkOpen && s_vkVis <= 0.0f) { memcpy(s_vkPrevB, p.b, sizeof s_vkPrevB); s_vkSelX = -1; return false; }

    // --- medidas: grilla de 10 columnas; todo lo demas se alinea a sus bordes ---
    const ImVec2 ds = io.DisplaySize; const float fs0 = ImGui::GetFontSize();
    const float key = std::clamp(std::min(46.0f * k, (ds.x - 32.0f - 36.0f * k) / 10.6f), 24.0f, 80.0f), gap = std::max(3.0f, key * 0.12f);
    const float gridW = 10 * key + 9 * gap, pad = 18.0f * k, W = gridW + 2 * pad;
    const float headH = fs0 * 1.4f, fieldH = key * 0.95f, sugH = key * 0.72f, hintH = fs0 * 1.5f;
    const float H = pad + headH + gap * 2 + fieldH + gap * 2 + sugH + gap * 2 + 5 * key + 4 * gap + gap * 2 + 1.0f + gap * 2 + hintH + pad * 0.7f;
    // sugerencias: recientes primero (reloj), despues palabras del menu (lupa) (sin repetir lo que ya esta escrito)
    const char* fixedSug[] = { "pasto", "sol", "viento", "sombra", "piel", "luz", "camara", "mando", "pelo", "cara" };
    struct Sug { std::string t; bool recent; float x0, x1; }; std::vector<Sug> sug;
    for (auto& r : s_vkRecent) if (!r.empty() && r != buf) sug.push_back({ r, true, 0, 0 });
    for (const char* f : fixedSug) { bool dup = false; for (auto& q : sug) dup |= q.t == f; if (!dup && strcmp(f, buf) != 0) sug.push_back({ f, false, 0, 0 }); }
    { float x = 0; const float sfs = fs0 * 0.95f; size_t n = 0;
      for (; n < sug.size(); n++) { const float w = ImGui::GetFont()->CalcTextSizeA(sfs, FLT_MAX, 0, sug[n].t.c_str()).x + sugH * 1.15f + 14.0f * k;
          if (x + w > gridW + 0.5f) break; sug[n].x0 = x; sug[n].x1 = x + w; x += w + gap; }
      sug.resize(n);
      // se reparte el sobrante para que la fila termine justo en el borde de la grilla
      if (n > 0) { const float extra = (gridW - (x - gap)) / n; for (size_t i = 0; i < n; i++) { sug[i].x0 += extra * i; sug[i].x1 += extra * (i + 1); } } }
    const int nSug = (int)sug.size();

    bool changed = false; int press = -1;   // 0..39 teclas, 40 + especial, 50 + sugerencia
    if (s_vkOpen) {
        bool edge[kPadCount] = {};
        for (int i = 0; i < kPadCount; i++) edge[i] = p.b[i] && !s_vkPrevB[i];
        if (s_vkJustOpened) { for (bool& e : edge) e = false; s_vkJustOpened = false; }   // el boton que lo abrio no escribe
        const bool dirNow[4] = { p.b[kPadUp] || p.ly < -0.55f, p.b[kPadDown] || p.ly > 0.55f, p.b[kPadLeft] || p.lx < -0.55f, p.b[kPadRight] || p.lx > 0.55f };
        int dRow = 0, dCol = 0;
        for (int d = 0; d < 4; d++) {
            bool fire = false;
            if (dirNow[d] && !s_vkDirWas[d]) { fire = true; s_vkRepT[d] = now + 0.32; }
            else if (dirNow[d] && now >= s_vkRepT[d]) { fire = true; s_vkRepT[d] = now + 0.075; }
            s_vkDirWas[d] = dirNow[d];
            if (fire) { if (d == 0) dRow--; else if (d == 1) dRow++; else if (d == 2) dCol--; else dCol++; }
        }
        auto colX = [&](int c) { return c * (key + gap) + key * 0.5f; };
        if (dRow) {
            const int minRow = nSug > 0 ? 0 : 1, nRows = 6 - minRow;
            const int was = s_vkRow; s_vkRow = minRow + ((s_vkRow - minRow + dRow) % nRows + nRows) % nRows;
            if (s_vkRow == 0 && was != 0) { float best = 1e9f; const float x = colX(s_vkCol); for (int i = 0; i < nSug; i++) { const float d = fabsf((sug[i].x0 + sug[i].x1) * 0.5f - x); if (d < best) { best = d; s_vkSug = i; } } }
            if (was == 0 && s_vkRow != 0 && s_vkSug < nSug) s_vkCol = std::clamp((int)(((sug[s_vkSug].x0 + sug[s_vkSug].x1) * 0.5f) / (key + gap)), 0, 9);
            if (s_vkRow == 5) s_vkCol = kVkSpC0[VkSpecialAt(s_vkCol)];
        }
        if (dCol) {
            if (s_vkRow == 0) s_vkSug = nSug ? (s_vkSug + dCol + nSug) % nSug : 0;
            else if (s_vkRow == 5) { const int sp = (VkSpecialAt(s_vkCol) + (dCol > 0 ? 1 : 3)) % 4; s_vkCol = kVkSpC0[sp]; }
            else s_vkCol = (s_vkCol + dCol + 10) % 10;
        }
        if (s_vkRow == 0 && nSug == 0) s_vkRow = 1;
        if (edge[kPadCross]) press = s_vkRow == 0 ? 50 + s_vkSug : s_vkRow < 5 ? (s_vkRow - 1) * 10 + s_vkCol : 40 + VkSpecialAt(s_vkCol);
        if (edge[kPadSquare]) press = 40 + kVkBack;
        if (edge[kPadTriangle]) press = 40 + kVkSpace;
        if (edge[kPadL1]) press = 40 + kVkClear;
        if (edge[kPadR1]) press = 40 + kVkPage;
        if (edge[kPadCircle] || edge[kPadOptions]) press = 40 + kVkDone;
    }
    memcpy(s_vkPrevB, p.b, sizeof s_vkPrevB);

    // --- ventana: sube, crece desde el 96 % y aparece (igual que el menu) ---
    const float ev = VkEase(s_vkVis), A = alpha * ev;
    const float zs = anim ? 0.96f + 0.04f * ev : 1.0f;
    const ImVec2 c0(ds.x * 0.5f, ds.y - H * 0.5f - 24.0f * k + (1.0f - ev) * 40.0f * k);
    const ImVec2 pos(c0.x - W * 0.5f, c0.y - H * 0.5f);
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(ImVec2(W, H)); if (s_vkOpen) ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f); ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * k);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##teclado_mando", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | (s_vkOpen ? 0 : ImGuiWindowFlags_NoInputs));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const float R = 16.0f * k, th = std::max(1.2f, 1.7f * k);
    { const ImVec2 ga(c0.x - W * 0.5f * zs, c0.y - H * 0.5f * zs), gb(c0.x + W * 0.5f * zs, c0.y + H * 0.5f * zs);
      if (backdrop) backdrop(ga, gb, R, A);
      else { dl->AddRectFilled(ga, gb, Col(0.07f, 0.08f, 0.11f, 0.92f * A), R); dl->AddRect(ga, gb, Col(1, 1, 1, 0.14f * A), R, 0, 1.0f); } }
    const float x0 = pos.x + pad, x1 = x0 + gridW;
    float y = pos.y + pad;
    const float tOpen = (float)(now - s_vkOpenT);
    auto rowIn = [&](int r) { return anim && s_vkOpen ? VkEase((tOpen - 0.035f * r) / 0.20f) : 1.0f; };
    const ImU32 txtHi = Col(0.97f, 0.98f, 1.0f, A), txtLo = Col(0.66f, 0.70f, 0.76f, A);

    // cabecera: icono + titulo a la izquierda; a la derecha con que se abre y se cierra
    { const float cy = y + headH * 0.5f, gr = fs0 * 0.44f;
      const ImVec2 ia(x0, cy - fs0 * 0.45f), ib(x0 + fs0 * 1.45f, cy + fs0 * 0.45f);
      dl->AddRectFilled(ia, ib, ColA(acc, 0.18f * A), 4.0f * k); dl->AddRect(ia, ib, ColA(acc, A), 4.0f * k, 0, 1.4f * k);
      const float cw = (ib.x - ia.x - 6.0f * k) / 5.0f;
      for (int r = 0; r < 2; r++) for (int c = 0; c < 5; c++) dl->AddRectFilled(ImVec2(ia.x + 3.0f * k + c * cw + 0.8f * k, ia.y + 3.0f * k + r * 4.4f * k), ImVec2(ia.x + 3.0f * k + (c + 1) * cw - 0.8f * k, ia.y + 5.8f * k + r * 4.4f * k), ColA(acc, 0.95f * A), 0.8f * k);
      dl->AddLine(ImVec2(ia.x + cw * 1.3f, ib.y - 3.0f * k), ImVec2(ib.x - cw * 1.3f, ib.y - 3.0f * k), ColA(acc, 0.95f * A), 1.6f * k);
      dl->AddText(ImGui::GetFont(), fs0 * 1.08f, ImVec2(ib.x + 10.0f * k, cy - fs0 * 0.54f), txtHi, TRS("Teclado del mando"));
      const char* tA = TRS("Abrir"); const char* tC = TRS("Cerrar");
      const float wA = ImGui::CalcTextSize(tA).x, wC = ImGui::CalcTextSize(tC).x, gw = gr * 2.7f;
      float hx = x1 - wC - gw;
      MvPadGlyph(dl, ImVec2(hx + gr, cy), gr, kPadCircle, A, s_vkOpen && p.b[kPadCircle] ? 1.0f : 0.0f); dl->AddText(ImVec2(hx + gw, cy - fs0 * 0.5f), txtLo, tC);
      dl->AddLine(ImVec2(hx - 9.0f * k, cy - fs0 * 0.4f), ImVec2(hx - 9.0f * k, cy + fs0 * 0.4f), Col(1, 1, 1, 0.18f * A), 1.0f);
      hx -= 18.0f * k + wA + gr * 4.4f;
      MvPadGlyph(dl, ImVec2(hx + gr * 1.7f, cy), gr, kPadOptions, A); dl->AddText(ImVec2(hx + gr * 4.4f, cy - fs0 * 0.5f), txtLo, tA);
      y += headH + gap * 2; }
    // campo: lupa, texto grande, cursor que titila, contador y tacho (L1)
    { const float fh = fieldH, ffs = fs0 * 1.22f; const ImVec2 fa(x0, y), fb(x1, y + fh);
      dl->AddRectFilled(fa, fb, Col(0, 0, 0, 0.32f * A), fh * 0.5f);
      dl->AddRectFilledMultiColor(ImVec2(fa.x + fh * 0.5f, fa.y + 1), ImVec2(fb.x - fh * 0.5f, fa.y + fh * 0.45f), Col(1, 1, 1, 0.05f * A), Col(1, 1, 1, 0.05f * A), Col(1, 1, 1, 0), Col(1, 1, 1, 0));
      dl->AddRect(fa, fb, ColA(acc, (0.45f + 0.25f * (anim ? 0.5f + 0.5f * sinf((float)now * 3.0f) : 0.5f)) * A), fh * 0.5f, 0, 1.4f * k);
      const ImVec2 lc(fa.x + fh * 0.52f, fa.y + fh * 0.47f); const float lr = ffs * 0.27f;
      dl->AddCircle(lc, lr, ColA(acc, A), 18, th); dl->AddLine(ImVec2(lc.x + lr * 0.72f, lc.y + lr * 0.72f), ImVec2(lc.x + lr * 1.55f, lc.y + lr * 1.55f), ColA(acc, A), th * 1.2f);
      // a la derecha: tacho (clic o L1) y contador
      const float tb = fh * 0.8f; const ImVec2 ta2(fb.x - fh * 0.5f - tb * 0.5f - 4.0f * k, fa.y + (fh - tb) * 0.5f);
      ImGui::SetCursorScreenPos(ta2); const bool clr = ImGui::InvisibleButton("##vk_clear", ImVec2(tb, tb)); const bool clrH = ImGui::IsItemHovered();
      if (clr && s_vkOpen) press = 40 + kVkClear;
      const float cd = anim ? Sat(1.0f - (float)(now - s_vkPressT[40 + kVkClear]) / 0.2f) : 0.0f;
      if (clrH || cd > 0) dl->AddCircleFilled(ImVec2(ta2.x + tb * 0.5f, ta2.y + tb * 0.5f), tb * 0.5f, ColA(acc, (0.18f + 0.3f * cd) * A), 24);
      VkIcon(dl, kVkClear, ImVec2(ta2.x + tb * 0.5f, ta2.y + tb * 0.5f), ffs, buf[0] ? Col(0.85f, 0.88f, 0.92f, A) : Col(0.5f, 0.53f, 0.58f, A), th);
      MvPadGlyph(dl, ImVec2(ta2.x - fs0 * 0.75f, fa.y + fh * 0.5f), fs0 * 0.36f, kPadL1, A * 0.8f);
      char cnt[24]; snprintf(cnt, sizeof cnt, "%d", (int)strlen(buf)); const float cntW = ImGui::CalcTextSize(cnt).x;
      const float cntX = ta2.x - fs0 * 1.5f - cntW;
      if (buf[0]) dl->AddText(ImVec2(cntX, fa.y + (fh - fs0) * 0.5f), Col(0.5f, 0.54f, 0.6f, A), cnt);
      const float tx = fa.x + fh * 0.98f, ty = fa.y + (fh - ffs) * 0.5f;
      const bool empty = !buf[0];
      const char* shown = empty ? TRS("Escribi lo que buscas") : buf;
      ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(ffs, FLT_MAX, 0, shown); const float maxW = cntX - tx - 10.0f * k;
      const char* from = shown; while (!empty && ts.x > maxW && *from) { from++; while ((*from & 0xC0) == 0x80) from++; ts = ImGui::GetFont()->CalcTextSizeA(ffs, FLT_MAX, 0, from); }
      dl->PushClipRect(ImVec2(tx, fa.y), ImVec2(cntX - 4.0f * k, fb.y), true);
      dl->AddText(ImGui::GetFont(), ffs, ImVec2(tx, ty), empty ? Col(0.52f, 0.56f, 0.62f, A) : txtHi, from);
      dl->PopClipRect();
      if (s_vkOpen && fmodf((float)now, 1.0f) < 0.6f) { const float cx = tx + (empty ? 0.0f : ts.x) + 2.0f * k; dl->AddLine(ImVec2(cx, ty + 2), ImVec2(cx, ty + ffs - 2), ColA(acc, A), 2.0f * k); }
      y += fh + gap * 2; }

    // posicion de cada cosa elegible (para el marco que se desliza)
    const float ySug = y, yKeys = y + sugH + gap * 2, yBot = yKeys + 4 * (key + gap);
    auto keyRect = [&](int r, int c, int span, ImVec2& ka, ImVec2& kb) {   // r: 1..5
        const float off = (1.0f - rowIn(r)) * 12.0f * k;
        ka = ImVec2(x0 + c * (key + gap), yKeys + (r - 1) * (key + gap) + off); kb = ImVec2(ka.x + span * key + (span - 1) * gap, ka.y + key); };
    { ImVec2 ka, kb;
      if (s_vkRow == 0 && nSug) { ka = ImVec2(x0 + sug[s_vkSug].x0, ySug); kb = ImVec2(x0 + sug[s_vkSug].x1, ySug + sugH); }
      else if (s_vkRow < 5) keyRect(s_vkRow, s_vkCol, 1, ka, kb); else { const int sp = VkSpecialAt(s_vkCol); keyRect(5, kVkSpC0[sp], kVkSpN[sp], ka, kb); }
      const float f = anim ? 1.0f - expf(-dt * 22.0f) : 1.0f;
      if (s_vkSelX < 0) { s_vkSelX = ka.x; s_vkSelY = ka.y; s_vkSelW = kb.x - ka.x; }
      s_vkSelX += (ka.x - s_vkSelX) * f; s_vkSelY += (ka.y - s_vkSelY) * f; s_vkSelW += ((kb.x - ka.x) - s_vkSelW) * f;
      const float selH = kb.y - ka.y, pulse = anim ? 0.5f + 0.5f * sinf((float)now * 5.0f) : 0.5f, rr = std::min(selH * 0.5f, 9.0f * k);
      const ImVec2 sa(s_vkSelX, s_vkSelY), sb(s_vkSelX + s_vkSelW, s_vkSelY + selH);
      for (int i = 3; i >= 1; i--) dl->AddRectFilled(ImVec2(sa.x - i * 2.2f * k, sa.y - i * 2.2f * k), ImVec2(sb.x + i * 2.2f * k, sb.y + i * 2.2f * k), ColA(acc, (0.05f + 0.03f * pulse) * A), rr + i * 2.2f * k); }

    const float kr = std::min(8.0f * k, key * 0.2f);
    // una tecla (o chip): vidrio translucido, la elegida con el verde del menu; se hunde y destella al apretar
    auto drawPanel = [&](int id, ImVec2 ka, ImVec2 kb, float rin, bool sel, bool strong, float round, bool& hov) -> float {
        const float KA = A * rin;
        ImGui::SetCursorScreenPos(ka); ImGui::PushID(id);
        const bool clicked = ImGui::InvisibleButton("k", ImVec2(kb.x - ka.x, kb.y - ka.y)); hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (clicked && s_vkOpen) press = id;
        float* hv = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID(id + 9000), 0.0f);
        *hv += ((sel ? 1.0f : hov ? 0.5f : 0.0f) - *hv) * (anim ? 1.0f - expf(-dt * 16.0f) : 1.0f);
        const float down = anim ? Sat(1.0f - (float)(now - s_vkPressT[id]) / 0.18f) : 0.0f, push = 1.8f * k * down;
        const ImVec2 ta(ka.x, ka.y + push), tb(kb.x, kb.y + push);
        dl->AddRectFilled(ImVec2(ta.x, ta.y + 2.0f * k), ImVec2(tb.x, tb.y + 2.0f * k), Col(0, 0, 0, 0.26f * KA), round);
        const ImVec4 base = strong ? ImVec4(acc.x, acc.y, acc.z, 0.30f) : ImVec4(1, 1, 1, id >= 40 ? 0.065f : 0.09f);
        dl->AddRectFilled(ta, tb, ColA(Lerp4(base, ImVec4(acc.x, acc.y, acc.z, 0.34f), *hv * (sel ? 1.0f : 0.45f)), KA), round);
        dl->AddRectFilledMultiColor(ImVec2(ta.x + 2, ta.y + 2), ImVec2(tb.x - 2, ta.y + (tb.y - ta.y) * 0.45f), Col(1, 1, 1, 0.07f * KA), Col(1, 1, 1, 0.07f * KA), Col(1, 1, 1, 0), Col(1, 1, 1, 0));
        dl->AddRect(ta, tb, ColA(Lerp4(ImVec4(1, 1, 1, 0.13f), ImVec4(acc.x, acc.y, acc.z, 0.95f), *hv), KA), round, 0, (1.0f + 0.6f * *hv) * k);
        dl->AddLine(ImVec2(ta.x + round, ta.y + 0.8f), ImVec2(tb.x - round, ta.y + 0.8f), Col(1, 1, 1, (0.22f + 0.2f * *hv) * KA), 1.0f);
        if (down > 0.0f) { dl->AddRectFilled(ta, tb, ColA(acc, 0.40f * down * KA), round);
            const float gx = (1.0f - down) * 5.0f * k; dl->AddRect(ImVec2(ta.x - gx, ta.y - gx), ImVec2(tb.x + gx, tb.y + gx), ColA(acc, 0.6f * down * KA), round + gx, 0, 1.4f * k); }
        return push;
    };
    auto isSel = [&](int id) { return s_vkRow == 0 ? id == 50 + s_vkSug : s_vkRow < 5 ? id == (s_vkRow - 1) * 10 + s_vkCol : id == 40 + VkSpecialAt(s_vkCol); };
    auto hoverSelect = [&](int id, bool hov) { if (!hov || !s_vkOpen || (io.MouseDelta.x == 0 && io.MouseDelta.y == 0)) return;
        if (id >= 50) { s_vkRow = 0; s_vkSug = id - 50; } else if (id >= 40) { s_vkRow = 5; s_vkCol = kVkSpC0[id - 40]; } else { s_vkRow = id / 10 + 1; s_vkCol = id % 10; } };

    // fila de sugerencias (recientes con reloj, palabras del menu con destello)
    { const float rin = rowIn(0), sfs = fs0 * 0.95f;
      for (int i = 0; i < nSug; i++) {
          const ImVec2 ka(x0 + sug[i].x0, ySug + (1.0f - rin) * 12.0f * k), kb(x0 + sug[i].x1, ka.y + sugH); bool hov;
          const float push = drawPanel(50 + i, ka, kb, rin, isSel(50 + i), false, sugH * 0.5f, hov); hoverSelect(50 + i, hov);
          const float cy = (ka.y + kb.y) * 0.5f + push;
          VkIcon(dl, sug[i].recent ? 10 : 11, ImVec2(ka.x + sugH * 0.55f, cy), sfs, sug[i].recent ? Col(0.75f, 0.79f, 0.85f, A * rin) : ColA(acc, A * rin), th * 0.85f);
          dl->AddText(ImGui::GetFont(), sfs, ImVec2(ka.x + sugH * 1.0f, cy - sfs * 0.5f), isSel(50 + i) ? txtHi : Col(0.86f, 0.89f, 0.93f, A * rin), sug[i].t.c_str());
      } }
    // teclas: letras grandes (1.5x)
    for (int r = 1; r <= 4; r++) for (int c = 0; c < 10; c++) {
        const int id = (r - 1) * 10 + c; ImVec2 ka, kb; keyRect(r, c, 1, ka, kb); bool hov;
        const float push = drawPanel(id, ka, kb, rowIn(r), isSel(id), false, kr, hov); hoverSelect(id, hov);
        const char* lb = kVkRows[s_vkPage][r - 1][c];
        const float lfs = fs0 * (isSel(id) ? 1.58f : 1.48f);
        const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(lfs, FLT_MAX, 0.0f, lb);
        dl->AddText(ImGui::GetFont(), lfs, ImVec2((ka.x + kb.x - ts.x) * 0.5f, (ka.y + kb.y - ts.y) * 0.5f + push), isSel(id) ? txtHi : Col(0.90f, 0.92f, 0.96f, A * rowIn(r)), lb);
        if (r == 1 && !isSel(id)) { (void)0; }
    }
    // fila de abajo: ?123 | espacio | borrar | listo, alineada a las columnas; icono + texto + simbolo del boton del mando
    { static const int kGl[4] = { kPadR1, kPadTriangle, kPadSquare, kPadOptions };
      for (int sp = 0; sp < 4; sp++) {
          const int id = 40 + sp; ImVec2 ka, kb; keyRect(5, kVkSpC0[sp], kVkSpN[sp], ka, kb); bool hov;
          const float push = drawPanel(id, ka, kb, rowIn(5), isSel(id), sp == kVkDone, kr, hov); hoverSelect(id, hov);
          const float cy = (ka.y + kb.y) * 0.5f + push, A5 = A * rowIn(5), ifs = fs0 * 1.15f;
          const ImU32 ic = sp == kVkDone || isSel(id) ? Col(1, 1, 1, A5) : Col(0.88f, 0.91f, 0.95f, A5);
          const char* lb = sp == kVkPage ? (s_vkPage == 0 ? "?123" : "abc") : sp == kVkSpace ? TRS("Espacio") : sp == kVkBack ? TRS("Borrar") : TRS("Listo");
          const float lfs = sp == kVkPage ? fs0 * 1.15f : fs0 * 1.02f;
          const float lw = ImGui::GetFont()->CalcTextSizeA(lfs, FLT_MAX, 0, lb).x, iw = sp == kVkPage ? 0.0f : ifs * (sp == kVkBack ? 1.6f : 1.3f);
          const float gx = kb.x - fs0 * 0.62f;   // simbolo del boton del mando, chico, en la esquina derecha
          MvPadGlyph(dl, ImVec2(gx, ka.y + fs0 * 0.62f + push), fs0 * 0.30f, kGl[sp], A5 * 0.75f);
          float lx = (ka.x + kb.x - (iw + lw)) * 0.5f;
          if (sp != kVkPage) VkIcon(dl, sp, ImVec2(lx + iw * 0.4f, cy), ifs, ic, th);
          dl->AddText(ImGui::GetFont(), lfs, ImVec2(lx + iw, cy - lfs * 0.5f), ic, lb);
      } }
    // ayuda: una linea fina y 6 celdas iguales alineadas a la grilla
    { const float ly = yBot + key + gap * 2; dl->AddLine(ImVec2(x0, ly), ImVec2(x1, ly), Col(1, 1, 1, 0.10f * A), 1.0f);
      const float hy = ly + gap * 2, r = fs0 * 0.40f, hfs = fs0 * 0.92f;
      struct { int b; const char* t; } h[] = { { kPadUp, "Moverse" }, { kPadCross, "Escribir" }, { kPadSquare, "Borrar" }, { kPadTriangle, "Espacio" }, { kPadR1, "Simbolos" }, { kPadCircle, "Cerrar" } };
      const int nH = 6; const float cellW = gridW / nH;
      for (int i = 0; i < nH; i++) {
          const char* t = TRS(h[i].t); const float tw = ImGui::GetFont()->CalcTextSizeA(hfs, FLT_MAX, 0, t).x, w = r * 2.5f + tw;
          if (w > cellW - 4.0f * k) continue;
          const float hx = x0 + cellW * i + (cellW - w) * 0.5f, cy = hy + hintH * 0.5f;
          MvPadGlyph(dl, ImVec2(hx + r, cy), r, h[i].b, A * 0.9f, s_vkOpen && p.b[h[i].b] ? 1.0f : 0.0f);
          dl->AddText(ImGui::GetFont(), hfs, ImVec2(hx + r * 2.5f, cy - hfs * 0.5f), txtLo, t);
      } }
    ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(3);

    // --- aplicar ---
    if (press >= 0) {
        s_vkPressT[press] = now;
        if (press >= 50) { if (press - 50 < nSug) { snprintf(buf, cap, "%s", sug[press - 50].t.c_str()); changed = true; } }
        else if (press < 40) changed = VkAppend(buf, cap, kVkRows[s_vkPage][press / 10][press % 10]);
        else switch (press - 40) {
            case kVkSpace: if (buf[0] && buf[strlen(buf) - 1] != ' ') changed = VkAppend(buf, cap, " "); break;
            case kVkBack: if (buf[0]) { VkBackspace(buf); changed = true; } break;
            case kVkClear: if (buf[0]) { buf[0] = 0; changed = true; } break;
            case kVkPage: s_vkPage = 1 - s_vkPage; break;
            case kVkDone:
                s_vkOpen = false;
                if (buf[0]) {   // se guarda en las recientes
                    std::string q = buf; while (!q.empty() && q.back() == ' ') q.pop_back();
                    if (!q.empty()) { int at = 3; for (int i = 0; i < 4; i++) if (s_vkRecent[i] == q) { at = i; break; }
                        for (int i = at; i > 0; i--) s_vkRecent[i] = s_vkRecent[i - 1]; s_vkRecent[0] = q; }
                }
                break;
        }
    }
    return changed;
}

// ------------------------------------------------------------------------------------------------------------------------------
// (14/11, Agustin: "puedes mejorar el cursor que se muestra en el menu? asi como anadir un boton para activar y desactivar el cursor")
// (14/11 b, Agustin: "el cursor no me gusta con la animacion [las ondas verdes] ... que brille al poder hacer click y cambie por una mano
// asi como el cursor de windows, asi como en las partes donde se pueda escribir que cambie, que se muestre arriba del menu insert")
// flecha clasica de Windows; sobre lo que se toca: MANO con el dedo arriba y un brillo suave del color del menu que respira; sobre un
// campo de texto: barra I. Al apretar se achica apenas (sin ondas). Se dibuja en la capa de arriba de todo (foreground).
static void CurShape(ImDrawList* dl, const ImVec2* p, int n, ImU32 fillTop, ImU32 fillBot, ImU32 edge, float edgeW, ImVec2 g0, ImVec2 g1) {
    const int v0 = dl->VtxBuffer.Size; dl->AddConcavePolyFilled(p, n, IM_COL32_WHITE);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, g0, g1, fillTop, fillBot);
    dl->AddPolyline(p, n, edge, ImDrawFlags_Closed, edgeW);
}
void MvCursorDraw(ImDrawList* dl, ImVec2 m, int kind, bool down, float k, float alpha, bool anim) {
    if (alpha <= 0.0f) return;
    const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const float dt = ImGui::GetIO().DeltaTime; const double now = ImGui::GetTime();
    static float s_hov = 0.0f, s_dn = 0.0f, s_flash = 0.0f; static int s_kindWas = 0;
    const float f = anim ? 1.0f - expf(-dt * 18.0f) : 1.0f;
    s_hov += ((kind == 1 ? 1.0f : 0.0f) - s_hov) * f; s_dn += ((down ? 1.0f : 0.0f) - s_dn) * (anim ? 1.0f - expf(-dt * 30.0f) : 1.0f);
    if (kind == 1 && s_kindWas != 1) s_flash = 1.0f;   // al entrar a algo que se toca: un destello corto
    s_flash = anim ? std::max(0.0f, s_flash - dt / 0.35f) : 0.0f; s_kindWas = kind;
    const ImU32 edge = Col(0.04f, 0.05f, 0.07f, 0.95f * alpha), white = Col(1, 1, 1, alpha), grey = Col(0.84f, 0.87f, 0.91f, alpha);
    if (kind == 2) {   // barra I para escribir
        const float h = 10.0f * k, w = 3.6f * k;
        for (int pass = 0; pass < 2; pass++) {
            const ImU32 c = pass == 0 ? Col(0, 0, 0, 0.8f * alpha) : white; const float th = pass == 0 ? 3.6f * k : 1.6f * k;
            dl->AddLine(ImVec2(m.x, m.y - h), ImVec2(m.x, m.y + h), c, th);
            dl->AddLine(ImVec2(m.x - w, m.y - h), ImVec2(m.x + w, m.y - h), c, th); dl->AddLine(ImVec2(m.x - w, m.y + h), ImVec2(m.x + w, m.y + h), c, th);
        }
        return;
    }
    const float sc = k * (1.0f - 0.08f * s_dn);
    if (kind == 1) {
        // brillo: halo del color del menu que respira, mas fuerte al entrar
        const float breath = anim ? 0.5f + 0.5f * sinf((float)now * 3.2f) : 0.5f, gl = s_hov * (0.55f + 0.25f * breath) + 0.6f * s_flash;
        const ImVec2 hc(m.x + 3.5f * sc, m.y + 10.0f * sc);
        for (int i = 5; i >= 1; i--) dl->AddCircleFilled(hc, (5.0f + i * 3.4f) * k, ColA(acc, 0.055f * gl * alpha), 28);
        // mano con el indice arriba (la punta del dedo es el punto del mouse)
        auto R = [&](float x0, float y0, float x1, float y1) { return std::make_pair(ImVec2(m.x + x0 * sc, m.y + y0 * sc), ImVec2(m.x + x1 * sc, m.y + y1 * sc)); };
        struct Part { float x0, y0, x1, y1, r; };
        static const Part parts[5] = { { -1.7f, 0.0f, 1.7f, 11.5f, 1.7f }, { 1.5f, 5.2f, 4.6f, 12.0f, 1.5f }, { 4.4f, 6.0f, 7.4f, 12.4f, 1.5f }, { 7.2f, 7.4f, 9.8f, 12.8f, 1.3f }, { -1.7f, 9.0f, 9.8f, 17.6f, 3.0f } };
        // sombra
        for (const Part& q : parts) { auto r = R(q.x0, q.y0, q.x1, q.y1); dl->AddRectFilled(ImVec2(r.first.x + 1.6f * k, r.first.y + 2.2f * k), ImVec2(r.second.x + 1.6f * k, r.second.y + 2.2f * k), Col(0, 0, 0, 0.22f * alpha), q.r * sc); }
        // borde (todo un poco mas grande en oscuro) + pulgar
        const float e = 1.1f * k;
        for (const Part& q : parts) { auto r = R(q.x0, q.y0, q.x1, q.y1); dl->AddRectFilled(ImVec2(r.first.x - e, r.first.y - e), ImVec2(r.second.x + e, r.second.y + e), edge, q.r * sc + e); }
        const ImVec2 t0(m.x - 1.0f * sc, m.y + 14.2f * sc), t1(m.x - 4.6f * sc, m.y + 9.6f * sc);
        dl->AddLine(t0, t1, edge, 3.4f * sc + 2 * e); dl->AddCircleFilled(t1, 1.7f * sc + e, edge, 12);
        // relleno: blanco que se tine apenas del verde del menu
        const ImU32 top = ColA(Lerp4(ImVec4(1, 1, 1, 1), ImVec4(0.90f, 1.0f, 0.95f, 1), 0.6f * s_hov), alpha), bot = ColA(Lerp4(ImVec4(0.86f, 0.89f, 0.93f, 1), Lerp4(acc, ImVec4(1, 1, 1, 1), 0.45f), 0.7f * s_hov), alpha);
        dl->AddLine(t0, t1, top, 3.4f * sc); dl->AddCircleFilled(t1, 1.7f * sc, top, 12);
        for (const Part& q : parts) { auto r = R(q.x0, q.y0, q.x1, q.y1); const int v0 = dl->VtxBuffer.Size; dl->AddRectFilled(r.first, r.second, IM_COL32_WHITE, q.r * sc);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(m.x, m.y), ImVec2(m.x, m.y + 18.0f * sc), top, bot); }
        // lineas de los dedos y la una
        dl->AddLine(ImVec2(m.x + 1.6f * sc, m.y + 9.5f * sc), ImVec2(m.x + 1.6f * sc, m.y + 12.0f * sc), Col(0.35f, 0.38f, 0.44f, 0.8f * alpha), 1.0f * k);
        dl->AddLine(ImVec2(m.x + 4.5f * sc, m.y + 9.8f * sc), ImVec2(m.x + 4.5f * sc, m.y + 12.2f * sc), Col(0.35f, 0.38f, 0.44f, 0.8f * alpha), 1.0f * k);
        dl->AddLine(ImVec2(m.x + 7.3f * sc, m.y + 10.2f * sc), ImVec2(m.x + 7.3f * sc, m.y + 12.6f * sc), Col(0.35f, 0.38f, 0.44f, 0.8f * alpha), 1.0f * k);
        dl->AddLine(ImVec2(m.x - 0.6f * sc, m.y + 1.2f * sc), ImVec2(m.x - 0.6f * sc, m.y + 7.0f * sc), Col(1, 1, 1, 0.9f * alpha), 1.0f * k);   // brillo del indice
        (void)grey; return;
    }
    // flecha clasica (como la de Windows): blanca con borde negro y sombra suave
    static const float P[7][2] = { { 0, 0 }, { 0, 16.0f }, { 3.9f, 12.3f }, { 6.6f, 18.4f }, { 9.0f, 17.4f }, { 6.4f, 11.4f }, { 11.4f, 11.4f } };
    ImVec2 pts[7], sh[7]; for (int i = 0; i < 7; i++) { pts[i] = ImVec2(m.x + P[i][0] * sc, m.y + P[i][1] * sc); sh[i] = ImVec2(pts[i].x + 1.5f * k, pts[i].y + 2.0f * k); }
    dl->AddConcavePolyFilled(sh, 7, Col(0, 0, 0, 0.25f * alpha));
    CurShape(dl, pts, 7, white, grey, edge, 1.3f * k, m, ImVec2(m.x, m.y + 18.0f * sc));
    dl->AddLine(ImVec2(pts[0].x + 1.3f * k, pts[0].y + 3.0f * k), ImVec2(pts[1].x + 1.3f * k, pts[1].y - 4.0f * k), Col(1, 1, 1, 0.9f * alpha), 1.0f * k);
}

// ------------------------------------------------------------------------------------------------------------------------------
// (14/11, Agustin: "mas diseno para las opciones de camara libre") TARJETA DE LA CAMARA LIBRE: 4 modos con dibujo (apagada, prendida,
// sin bloquear el teclado, solo en las repeticiones), un visor (la cancha vista por la camara, con el marco, REC cuando vuela y el
// zoom: arrastrar de costado o la rueda), el teclado W A S D Q E y el mouse que se iluminan con lo que se aprieta, tiras de velocidad
// y sensibilidad, y "seguir la cara" con dibujo.
static void CamGlyph(ImDrawList* dl, ImVec2 c, float s, int mode, ImU32 col, float t) {
    const float th = std::max(1.2f, s * 0.08f);
    // cuerpo de la camara
    const ImVec2 a(c.x - s * 0.42f, c.y - s * 0.20f), b(c.x + s * 0.20f, c.y + s * 0.24f);
    dl->AddRect(a, b, col, s * 0.06f, 0, th);
    const ImVec2 L[4] = { ImVec2(b.x, c.y - s * 0.04f), ImVec2(c.x + s * 0.44f, c.y - s * 0.18f), ImVec2(c.x + s * 0.44f, c.y + s * 0.22f), ImVec2(b.x, c.y + s * 0.10f) };
    dl->AddPolyline(L, 4, col, ImDrawFlags_Closed, th);
    dl->AddCircle(ImVec2(c.x - s * 0.11f, c.y + s * 0.02f), s * 0.10f, col, 12, th);
    if (mode == 0) dl->AddLine(ImVec2(c.x - s * 0.46f, c.y + s * 0.34f), ImVec2(c.x + s * 0.46f, c.y - s * 0.34f), col, th * 1.3f);
    if (mode == 1) dl->AddCircleFilled(ImVec2(a.x + s * 0.10f, a.y - s * 0.10f), s * 0.06f * (0.7f + 0.3f * sinf(t * 4.0f)), IM_COL32(255, 80, 80, (col >> 24) & 255), 8);
    if (mode == 2) for (int i = 0; i < 4; i++) dl->AddRectFilled(ImVec2(c.x - s * 0.40f + i * s * 0.22f, c.y + s * 0.34f), ImVec2(c.x - s * 0.26f + i * s * 0.22f, c.y + s * 0.44f), col, 1.0f);
    if (mode == 3) { const float a0 = -t * 2.5f; dl->PathArcTo(ImVec2(c.x + s * 0.30f, c.y - s * 0.36f), s * 0.13f, a0, a0 + kPI * 1.5f, 10); dl->PathStroke(col, 0, th); }
}
static void KeyCap(ImDrawList* dl, ImVec2 a, float w, float h, const char* t, bool on, float k, ImVec4 acc) {
    const ImVec2 b(a.x + w, a.y + h); const float r = 5.0f * k, push = on ? 1.5f * k : 0.0f;
    dl->AddRectFilled(ImVec2(a.x, a.y + 2.5f * k), ImVec2(b.x, b.y + 2.5f * k), Col(0, 0, 0, 0.45f), r);
    dl->AddRectFilled(ImVec2(a.x, a.y + push), ImVec2(b.x, b.y + push), on ? ColA(acc, 0.75f) : Col(1, 1, 1, 0.10f), r);
    dl->AddRectFilledMultiColor(ImVec2(a.x + 2, a.y + 2 + push), ImVec2(b.x - 2, a.y + h * 0.45f + push), Col(1, 1, 1, 0.08f), Col(1, 1, 1, 0.08f), Col(1, 1, 1, 0), Col(1, 1, 1, 0));
    dl->AddRect(ImVec2(a.x, a.y + push), ImVec2(b.x, b.y + push), on ? ColA(Lerp4(acc, ImVec4(1, 1, 1, 1), 0.3f), 1) : Col(1, 1, 1, 0.18f), r, 0, 1.0f);
    const ImVec2 ts = ImGui::CalcTextSize(t);
    dl->AddText(ImVec2(a.x + (w - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f + push), on ? Col(1, 1, 1, 1) : Col(0.85f, 0.88f, 0.92f, 1), t);
}

// (14/11, Agustin: "que muestres los iconos de el control y su funcion") LEYENDA DEL MANDO de las tarjetas de camara: cada fila es
// el simbolo del boton (con el estilo PS5 / PS4 / Xbox elegido) y lo que hace; se ilumina mientras se aprieta. Los sticks se dibujan
// con la perilla donde esta el stick de verdad y los gatillos se llenan con lo apretado.
enum { kLgStickL = 100, kLgStickR, kLgDpadUD, kLgDpadLR, kLgL2R2, kLgL1R1, kLgL3R3, kLgL1, kLgR1 };
struct LgItem { int g; const char* label; const char* sub; };
static float LgLit(int g, const MvPad& p) {
    switch (g) {
    case kLgStickL: return Sat(sqrtf(p.lx * p.lx + p.ly * p.ly));
    case kLgStickR: return Sat(sqrtf(p.rx * p.rx + p.ry * p.ry));
    case kLgDpadUD: return (p.b[kPadUp] || p.b[kPadDown]) ? 1.0f : 0.0f;
    case kLgDpadLR: return (p.b[kPadLeft] || p.b[kPadRight]) ? 1.0f : 0.0f;
    case kLgL2R2: return std::max(p.l2, p.r2);
    case kLgL1R1: return (p.b[kPadL1] && p.b[kPadR1]) ? 1.0f : 0.0f;
    case kLgL3R3: return (p.b[kPadL3] && p.b[kPadR3]) ? 1.0f : 0.0f;
    case kLgL1: return p.b[kPadL1] && !p.b[kPadR1] ? 1.0f : 0.0f;
    case kLgR1: return p.b[kPadR1] && !p.b[kPadL1] ? 1.0f : 0.0f;
    default: return g >= 0 && g < kPadCount && p.b[g] ? 1.0f : 0.0f;
    }
}
static void LgDpad(ImDrawList* dl, ImVec2 c, float r, bool ud, const MvPad& p, ImVec4 acc) {
    const float a = r * 0.36f;
    const ImVec2 arm[4][2] = { { ImVec2(c.x - a, c.y - r), ImVec2(c.x + a, c.y - a) }, { ImVec2(c.x - a, c.y + a), ImVec2(c.x + a, c.y + r) },
                               { ImVec2(c.x - r, c.y - a), ImVec2(c.x - a, c.y + a) }, { ImVec2(c.x + a, c.y - a), ImVec2(c.x + r, c.y + a) } };
    const bool on[4] = { p.b[kPadUp], p.b[kPadDown], p.b[kPadLeft], p.b[kPadRight] };
    dl->AddRectFilled(ImVec2(c.x - a, c.y - a), ImVec2(c.x + a, c.y + a), Col(0.80f, 0.82f, 0.86f, 0.35f));
    for (int i = 0; i < 4; i++) {
        const bool rel = ud ? i < 2 : i >= 2;
        const ImU32 cl = on[i] ? ColA(acc, 1.0f) : rel ? Col(0.92f, 0.93f, 0.96f, 0.95f) : Col(0.80f, 0.82f, 0.86f, 0.28f);
        dl->AddRectFilled(arm[i][0], arm[i][1], cl, a * 0.45f);
    }
    // flechitas en los brazos que importan
    const ImU32 ar = Col(0.08f, 0.09f, 0.11f, 0.9f); const float t = a * 0.55f;
    if (ud) { dl->AddTriangleFilled(ImVec2(c.x, c.y - r + t * 0.6f), ImVec2(c.x - t, c.y - r + t * 1.6f), ImVec2(c.x + t, c.y - r + t * 1.6f), ar);
              dl->AddTriangleFilled(ImVec2(c.x, c.y + r - t * 0.6f), ImVec2(c.x + t, c.y + r - t * 1.6f), ImVec2(c.x - t, c.y + r - t * 1.6f), ar); }
    else    { dl->AddTriangleFilled(ImVec2(c.x - r + t * 0.6f, c.y), ImVec2(c.x - r + t * 1.6f, c.y + t), ImVec2(c.x - r + t * 1.6f, c.y - t), ar);
              dl->AddTriangleFilled(ImVec2(c.x + r - t * 0.6f, c.y), ImVec2(c.x + r - t * 1.6f, c.y - t), ImVec2(c.x + r - t * 1.6f, c.y + t), ar); }
}
static void LgStick(ImDrawList* dl, ImVec2 c, float r, float x, float y, const char* tag, float lit, ImVec4 acc) {
    dl->AddCircleFilled(c, r, Col(0.05f, 0.06f, 0.08f, 0.92f), 28);
    dl->AddCircle(c, r - 0.5f, ColA(Lerp4(ImVec4(1, 1, 1, 0.25f), acc, lit), 1.0f), 28, 1.2f);
    const ImVec2 k2(c.x + x * r * 0.42f, c.y + y * r * 0.42f);
    dl->AddCircleFilled(ImVec2(k2.x, k2.y + r * 0.08f), r * 0.60f, Col(0, 0, 0, 0.45f), 24);
    dl->AddCircleFilled(k2, r * 0.60f, ColA(Lerp4(ImVec4(0.30f, 0.32f, 0.37f, 1), ImVec4(acc.x * 0.8f, acc.y * 0.8f, acc.z * 0.8f, 1), lit), 1.0f), 24);
    dl->AddCircle(k2, r * 0.42f, Col(1, 1, 1, 0.18f), 20, 1.0f);
    ImFont* f = ImGui::GetFont(); const float fs = r * 0.78f; const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, tag);
    dl->AddText(f, fs, ImVec2(k2.x - ts.x * 0.5f, k2.y - ts.y * 0.5f), Col(1, 1, 1, 0.92f), tag);
}
static void LgGlyph(ImDrawList* dl, ImVec2 c, float r, int g, const MvPad& p, float lit, ImVec4 acc) {
    const bool xb = MvPadGetStyle() == kPadStyleXbox;
    switch (g) {
    case kLgStickL: LgStick(dl, c, r, p.lx, p.ly, xb ? "LS" : "L", lit, acc); return;
    case kLgStickR: LgStick(dl, c, r, p.rx, p.ry, xb ? "RS" : "R", lit, acc); return;
    case kLgDpadUD: LgDpad(dl, c, r, true, p, acc); return;
    case kLgDpadLR: LgDpad(dl, c, r, false, p, acc); return;
    case kLgL2R2: MvPadGlyph(dl, ImVec2(c.x - r * 1.18f, c.y), r * 0.74f, kPadL2, 1.0f, p.l2); MvPadGlyph(dl, ImVec2(c.x + r * 1.18f, c.y), r * 0.74f, kPadR2, 1.0f, p.r2); return;
    case kLgL1R1: MvPadGlyph(dl, ImVec2(c.x - r * 1.18f, c.y), r * 0.74f, kPadL1, 1.0f, p.b[kPadL1] ? 1.0f : 0.0f); MvPadGlyph(dl, ImVec2(c.x + r * 1.18f, c.y), r * 0.74f, kPadR1, 1.0f, p.b[kPadR1] ? 1.0f : 0.0f); return;
    case kLgL3R3: MvPadGlyph(dl, ImVec2(c.x - r * 1.18f, c.y), r * 0.74f, kPadL3, 1.0f, p.b[kPadL3] ? 1.0f : 0.0f); MvPadGlyph(dl, ImVec2(c.x + r * 1.18f, c.y), r * 0.74f, kPadR3, 1.0f, p.b[kPadR3] ? 1.0f : 0.0f); return;
    case kLgL1: MvPadGlyph(dl, c, r * 0.86f, kPadL1, 1.0f, lit); return;
    case kLgR1: MvPadGlyph(dl, c, r * 0.86f, kPadR1, 1.0f, lit); return;
    default: MvPadGlyph(dl, c, r * 0.80f, g, 1.0f, lit); return;
    }
}
// encabezado de la seccion del mando: icono, titulo, estado y (si toggle) el interruptor camara_mando
static bool LgHeader(const char* title, const char* state, bool stateOn, int* toggle, float k, ImVec4 acc, bool anim) {
    ImDrawList* dl = ImGui::GetWindowDrawList(); bool ch = false;
    const float avail = ImGui::GetContentRegionAvail().x, h = 26.0f * k; const ImVec2 p = ImGui::GetCursorScreenPos();
    // mando chiquito dibujado como icono
    { const ImVec2 c(p.x + 13.0f * k, p.y + h * 0.5f); const float w = 11.0f * k;
      dl->AddRectFilled(ImVec2(c.x - w, c.y - w * 0.45f), ImVec2(c.x + w, c.y + w * 0.35f), ColA(acc, 0.9f), w * 0.45f);
      dl->AddCircleFilled(ImVec2(c.x - w * 0.72f, c.y + w * 0.35f), w * 0.42f, ColA(acc, 0.9f), 12); dl->AddCircleFilled(ImVec2(c.x + w * 0.72f, c.y + w * 0.35f), w * 0.42f, ColA(acc, 0.9f), 12);
      dl->AddCircleFilled(ImVec2(c.x - w * 0.45f, c.y), w * 0.16f, Col(0.06f, 0.07f, 0.09f, 1), 8); dl->AddCircleFilled(ImVec2(c.x + w * 0.45f, c.y), w * 0.16f, Col(0.06f, 0.07f, 0.09f, 1), 8); }
    const ImVec2 ts = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(p.x + 32.0f * k, p.y + (h - ts.y) * 0.5f), Col(0.95f, 0.96f, 0.98f, 1), title);
    // estado (pastilla)
    float x = p.x + 32.0f * k + ts.x + 10.0f * k;
    if (state && *state) { const float fs = ImGui::GetFontSize() * 0.84f; const ImVec2 ss = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, state);
        const ImVec2 a(x, p.y + (h - ss.y) * 0.5f - 3.0f * k), b(x + ss.x + 22.0f * k, p.y + (h + ss.y) * 0.5f + 3.0f * k);
        dl->AddRectFilled(a, b, stateOn ? ColA(acc, 0.22f) : Col(1, 1, 1, 0.06f), (b.y - a.y) * 0.5f);
        dl->AddCircleFilled(ImVec2(a.x + 9.0f * k, (a.y + b.y) * 0.5f), 3.2f * k, stateOn ? ColA(acc, 1) : Col(0.6f, 0.62f, 0.66f, 1), 10);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + 16.0f * k, a.y + 3.0f * k), stateOn ? Col(0.9f, 1, 0.94f, 1) : Col(0.70f, 0.73f, 0.78f, 1), state); }
    ImGui::Dummy(ImVec2(avail - (toggle ? 46.0f * k : 0.0f), h));
    if (toggle) {   // interruptor
        ImGui::SameLine(0, 0); const ImVec2 q = ImGui::GetCursorScreenPos(); const float tw = 40.0f * k, th = 20.0f * k;
        ImGui::SetCursorScreenPos(ImVec2(q.x + 6.0f * k, p.y + (h - th) * 0.5f));
        if (ImGui::InvisibleButton("##lgtog", ImVec2(tw, th))) { *toggle = *toggle ? 0 : 1; ch = true; }
        const bool hv = ImGui::IsItemHovered();
        if (hv) ImGui::SetTooltip("%s", TRS("El mando mueve la camara libre (el juego no lo ve mientras vuela)"));
        const float o = Follow(ImGui::GetItemID(), *toggle ? 1.0f : 0.0f, 12.0f, anim);
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        dl->AddRectFilled(a, b, ColA(Lerp4(ImVec4(1, 1, 1, hv ? 0.16f : 0.10f), ImVec4(acc.x, acc.y, acc.z, 0.85f), o), 1), th * 0.5f);
        dl->AddCircleFilled(ImVec2(a.x + th * 0.5f + (tw - th) * o, (a.y + b.y) * 0.5f), th * 0.38f, Col(1, 1, 1, 0.95f), 16);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
        ImGui::Dummy(ImVec2(0, 0));
    }
    return ch;
}
// grilla de filas "simbolo + que hace"; cols columnas; dim = la seccion no esta en uso (se ve, mas apagada)
static void LgGrid(const LgItem* it, int n, const MvPad& p, int cols, float k, ImVec4 acc, bool anim, bool dim) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x, g = 6.0f * k, cw = (avail - (cols - 1) * g) / cols, ch = 38.0f * k;
    const ImVec2 o = ImGui::GetCursorScreenPos(); const int rows = (n + cols - 1) / cols;
    for (int i = 0; i < n; i++) {
        const int cx = i % cols, cy = i / cols; const ImVec2 a(o.x + cx * (cw + g), o.y + cy * (ch + g)), b(a.x + cw, a.y + ch);
        const float lit0 = p.connected ? LgLit(it[i].g, p) : 0.0f;
        const float lit = Follow(ImGui::GetID(it[i].label), lit0, 14.0f, anim);
        dl->AddRectFilled(a, b, ColA(Lerp4(ImVec4(1, 1, 1, dim ? 0.025f : 0.045f), ImVec4(acc.x, acc.y, acc.z, 0.24f), lit), 1), 8.0f * k);
        dl->AddRect(a, b, ColA(Lerp4(ImVec4(1, 1, 1, 0.10f), ImVec4(acc.x, acc.y, acc.z, 0.9f), lit), 1), 8.0f * k, 0, 1.0f);
        const bool pair = it[i].g == kLgL2R2 || it[i].g == kLgL1R1 || it[i].g == kLgL3R3;
        const float gx = a.x + (pair ? 34.0f : 23.0f) * k;
        LgGlyph(dl, ImVec2(gx, (a.y + b.y) * 0.5f), 13.0f * k, it[i].g, p, lit, acc);
        const float tx = a.x + (pair ? 68.0f : 46.0f) * k;
        dl->PushClipRect(ImVec2(tx, a.y), ImVec2(b.x - 4.0f * k, b.y), true);
        const bool hasSub = it[i].sub && *it[i].sub;
        const ImU32 tc = dim ? Col(0.72f, 0.75f, 0.80f, 1) : Col(0.95f, 0.96f, 0.98f, 1);
        dl->AddText(ImVec2(tx, hasSub ? a.y + 4.0f * k : a.y + (ch - ImGui::GetFontSize()) * 0.5f), lit > 0.5f ? Col(1, 1, 1, 1) : tc, it[i].label);
        if (hasSub) dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.82f, ImVec2(tx, a.y + ch * 0.52f), ColA(Lerp4(ImVec4(0.58f, 0.62f, 0.68f, 1), ImVec4(0.6f, 0.95f, 0.75f, 1), lit), 1), it[i].sub);
        dl->PopClipRect();
    }
    ImGui::Dummy(ImVec2(avail, rows * ch + (rows - 1) * g));
}
// tira logaritmica (velocidades: el mismo recorrido para 0.1 -> 1 que para 1 -> 10) con botones - / + de 10 % (mantener repite)
static bool LogStrip(const char* id, float* v, float lo, float hi, float w, float k, const ImVec4* grad, int nGrad, ImVec4 fill, bool anim) {
    bool ch = false; ImGui::PushID(id);
    const float bw = 26.0f * k, sw = w - 2 * (bw + 6.0f * k);
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto Step = [&](const char* sid, bool up) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton(sid, ImVec2(bw, 18.0f * k))) { *v = std::clamp(*v * (up ? 1.1f : 1.0f / 1.1f), lo, hi); ch = true; }
        const bool hv = ImGui::IsItemHovered(), ac = ImGui::IsItemActive();
        dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + 18.0f * k), ac ? ColA(fill.w > 0 ? fill : ImVec4(0.3f, 0.8f, 0.55f, 1), 0.6f) : Col(1, 1, 1, hv ? 0.16f : 0.08f), 9.0f * k);
        const ImVec2 c(p.x + bw * 0.5f, p.y + 9.0f * k); const ImU32 lc = Col(0.95f, 0.96f, 0.98f, 1);
        dl->AddLine(ImVec2(c.x - 4.5f * k, c.y), ImVec2(c.x + 4.5f * k, c.y), lc, 1.6f * k);
        if (up) dl->AddLine(ImVec2(c.x, c.y - 4.5f * k), ImVec2(c.x, c.y + 4.5f * k), lc, 1.6f * k);
        if (hv) ImGui::SetTooltip("%s", TRS(up ? "10 % mas (mantener: sigue)" : "10 % menos (mantener: sigue)"));
    };
    Step("##m", false); ImGui::SameLine(0, 6.0f * k);
    float tt = logf(std::clamp(*v, lo, hi) / lo) / logf(hi / lo);
    ImGui::PopItemFlag();
    if (Strip("##s", &tt, 0.0f, 1.0f, sw, k, grad, nGrad, fill, anim)) { *v = std::clamp(lo * powf(hi / lo, tt), lo, hi); ch = true; }
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    ImGui::SameLine(0, 6.0f * k); Step("##p", true);
    ImGui::PopItemFlag();
    ImGui::PopID();
    return ch;
}
bool MvCamWidget(MvCamIn& in) {
    const float k = in.k; bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x; const double t = in.now;
    const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const int mode = in.mode ? std::clamp((int)lroundf(*in.mode), 0, 3) : 0;
    // --- 1) modos ---
    { static const char* kN[4] = { "Apagada", "Prendida", "Sin bloquear teclado", "Solo repeticiones" };
      static const char* kTip[4] = { "La camara del juego, como siempre.", "Camara propia: W A S D, Q / E, mouse con el boton derecho. El juego no ve el teclado mientras tanto.",
                                     "Igual que prendida, pero el juego sigue viendo el teclado.", "Se prende sola cuando empieza una repeticion y al terminar vuelve la del juego." };
      const float bw = (avail - 3 * 6.0f * k) / 4.0f, h = 54.0f * k;
      for (int i = 0; i < 4; i++) {
          if (i) ImGui::SameLine(0, 6.0f * k);
          ImGui::PushID(i); const ImVec2 p = ImGui::GetCursorScreenPos();
          if (ImGui::InvisibleButton("##cmode", ImVec2(bw, h)) && in.mode) { *in.mode = (float)i; changed = true; }
          const bool hv = ImGui::IsItemHovered(), cur = mode == i;
          const float ha = Follow(ImGui::GetItemID(), hv ? 1.0f : 0.0f, 12.0f, in.anim), oa = Follow(ImGui::GetItemID() + 1, cur ? 1.0f : 0.0f, 10.0f, in.anim);
          const ImVec4 top = Lerp4(ImVec4(1, 1, 1, 0.07f + 0.05f * ha), ImVec4(acc.x, acc.y, acc.z, 0.38f), oa), bot = Lerp4(ImVec4(1, 1, 1, 0.03f), ImVec4(acc.x * 0.6f, acc.y * 0.6f, acc.z * 0.6f, 0.30f), oa);
          dl->AddRectFilledMultiColor(p, ImVec2(p.x + bw, p.y + h), ColA(top, 1), ColA(top, 1), ColA(bot, 1), ColA(bot, 1));
          dl->AddRect(p, ImVec2(p.x + bw, p.y + h), ColA(Lerp4(ImVec4(1, 1, 1, 0.16f + 0.25f * ha), ImVec4(acc.x, acc.y, acc.z, 1), oa), 1), 8.0f * k, 0, (1.0f + oa) * k);
          const ImU32 ic = ColA(Lerp4(ImVec4(0.80f, 0.83f, 0.88f, 1), ImVec4(1, 1, 1, 1), oa), 1);
          CamGlyph(dl, ImVec2(p.x + bw * 0.5f, p.y + h * 0.36f), 26.0f * k, i, ic, in.anim && (cur || hv) ? (float)fmod(t, 3600.0) : 0.0f);
          const char* nm = TRS(kN[i]); const float fs = ImGui::GetFontSize() * 0.86f; const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, nm);
          dl->PushClipRect(p, ImVec2(p.x + bw, p.y + h), true);
          dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + std::max(4.0f * k, (bw - ts.x) * 0.5f), p.y + h * 0.68f), cur ? Col(1, 1, 1, 1) : Col(0.80f, 0.83f, 0.88f, 1), nm);
          dl->PopClipRect();
          if (hv) ImGui::SetTooltip("%s", TRS(kTip[i]));
          ImGui::PopID();
      }
      ImGui::Dummy(ImVec2(0, 4.0f * k)); }
    // --- 2) visor (izquierda) + teclas y tiras (derecha) ---
    const bool wide = avail >= 560.0f * k;
    const float VW = wide ? avail * 0.52f : avail, VH = VW * 0.56f;
    const ImVec2 v0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##camvisor", ImVec2(VW, VH));
    const bool hovV = ImGui::IsItemHovered(), actV = ImGui::IsItemActive();
    if (in.zoom) {
        if (actV && ImGui::GetIO().MouseDelta.x != 0.0f) { *in.zoom = std::clamp(*in.zoom * expf(-ImGui::GetIO().MouseDelta.x * 0.006f), 0.1f, 2.0f); changed = true; }
        if (hovV && ImGui::GetIO().MouseWheel != 0.0f) { ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); *in.zoom = std::clamp(*in.zoom * expf(-ImGui::GetIO().MouseWheel * 0.08f), 0.1f, 2.0f); changed = true; }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { *in.zoom = 1.0f; changed = true; }
    }
    if (hovV) ImGui::SetTooltip("%s", TRS("Arrastrar de costado o la rueda: zoom. Clic derecho: el del juego (1)."));
    {
        const ImVec2 a = v0, b(v0.x + VW, v0.y + VH); const float rr = 10.0f * k;
        const float z = Follow(ImGui::GetID("##camzoom"), in.zoom ? *in.zoom : 1.0f, 10.0f, in.anim), sc = 1.0f / std::max(0.1f, z);
        dl->AddRectFilledMultiColor(a, b, Col(0.10f, 0.16f, 0.24f, 1), Col(0.10f, 0.16f, 0.24f, 1), Col(0.06f, 0.08f, 0.11f, 1), Col(0.06f, 0.08f, 0.11f, 1));
        dl->PushClipRect(a, b, true);
        // escena: tribuna al fondo, cancha en perspectiva con franjas, jugadores; se agranda con el zoom alrededor del centro
        const ImVec2 cc((a.x + b.x) * 0.5f, a.y + VH * 0.55f);
        auto P = [&](float x, float y) { return ImVec2(cc.x + x * VW * 0.5f * sc, cc.y + y * VH * 0.5f * sc); };
        dl->AddRectFilledMultiColor(P(-3, -1.6f), P(3, -0.55f), Col(0.16f, 0.18f, 0.24f, 1), Col(0.16f, 0.18f, 0.24f, 1), Col(0.22f, 0.24f, 0.30f, 1), Col(0.22f, 0.24f, 0.30f, 1));
        for (int i = 0; i < 70; i++) { const float x = -2.8f + (i * 37 % 100) / 100.0f * 5.6f, y = -1.45f + (i * 53 % 90) / 100.0f; dl->AddCircleFilled(P(x, y), 1.4f * k * sc, (i % 3) ? Col(0.55f, 0.58f, 0.66f, 0.7f) : Col(0.85f, 0.35f, 0.32f, 0.7f), 6); }
        for (int s2 = 0; s2 < 8; s2++) {   // franjas del pasto en perspectiva
            const float y0 = -0.55f + s2 * 0.2f, y1 = y0 + 0.2f, w0 = 1.2f + (y0 + 0.55f) * 1.4f, w1 = 1.2f + (y1 + 0.55f) * 1.4f;
            const ImVec2 q[4] = { P(-w0, y0), P(w0, y0), P(w1, y1), P(-w1, y1) };
            dl->AddConvexPolyFilled(q, 4, (s2 & 1) ? Col(0.20f, 0.46f, 0.22f, 1) : Col(0.24f, 0.52f, 0.25f, 1));
        }
        dl->AddLine(P(-1.2f, -0.55f), P(1.2f, -0.55f), Col(1, 1, 1, 0.75f), 1.4f * k); dl->AddLine(P(0, -0.55f), P(0, 1.05f), Col(1, 1, 1, 0.6f), 1.2f * k);
        dl->AddEllipse(P(0, 0.12f), ImVec2(0.42f * VW * 0.5f * sc, 0.16f * VH * 0.5f * sc), Col(1, 1, 1, 0.6f), 0.0f, 32, 1.2f * k);
        const float ph = in.anim ? (float)t : 0.0f;
        for (int i = 0; i < 9; i++) { const float x = -1.0f + i * 0.25f + 0.05f * sinf(ph * 0.9f + i), y = -0.2f + 0.45f * ((i * 7) % 5) / 5.0f + 0.04f * cosf(ph * 0.7f + i * 2);
            const float ps = (0.8f + (y + 0.55f) * 0.6f) * k * sc;
            dl->AddEllipseFilled(ImVec2(P(x, y).x, P(x, y).y + 1.5f * ps), ImVec2(4.0f * ps, 1.5f * ps), Col(0, 0, 0, 0.3f));
            dl->AddRectFilled(ImVec2(P(x, y).x - 2.2f * ps, P(x, y).y - 9.0f * ps), ImVec2(P(x, y).x + 2.2f * ps, P(x, y).y), (i % 2) ? Col(0.85f, 0.20f, 0.22f, 1) : Col(0.92f, 0.92f, 0.96f, 1), 1.0f * ps);
            dl->AddCircleFilled(ImVec2(P(x, y).x, P(x, y).y - 10.5f * ps), 1.8f * ps, Col(0.85f, 0.68f, 0.55f, 1), 8); }
        dl->AddCircleFilled(P(0.15f + 0.08f * sinf(ph * 1.3f), 0.25f), 1.8f * k * sc, Col(1, 1, 1, 1), 8);
        dl->PopClipRect();
        // marco del visor: esquinas, centro, REC, zoom
        const float e = 12.0f * k, m = 8.0f * k; const ImU32 fc = Col(1, 1, 1, 0.85f);
        for (int i = 0; i < 4; i++) { const float sx = (i & 1) ? -1.0f : 1.0f, sy = (i & 2) ? -1.0f : 1.0f; const ImVec2 q((i & 1) ? b.x - m : a.x + m, (i & 2) ? b.y - m : a.y + m);
            dl->AddLine(q, ImVec2(q.x + sx * e, q.y), fc, 1.6f * k); dl->AddLine(q, ImVec2(q.x, q.y + sy * e), fc, 1.6f * k); }
        dl->AddLine(ImVec2(cc.x - 7 * k, a.y + VH * 0.5f), ImVec2(cc.x + 7 * k, a.y + VH * 0.5f), Col(1, 1, 1, 0.5f), 1.0f); dl->AddLine(ImVec2(cc.x, a.y + VH * 0.5f - 7 * k), ImVec2(cc.x, a.y + VH * 0.5f + 7 * k), Col(1, 1, 1, 0.5f), 1.0f);
        const bool live = in.active;
        const char* st = TRS(live ? "EN VIVO" : mode == 3 ? (in.replay ? "REPETICION" : "ESPERANDO REPETICION") : mode == 0 ? "APAGADA" : "LISTA");
        const float blink = in.anim ? 0.5f + 0.5f * sinf((float)t * 5.0f) : 1.0f;
        dl->AddCircleFilled(ImVec2(a.x + m + 10 * k, a.y + m + 18 * k), 4.5f * k, live ? Col(1.0f, 0.25f, 0.25f, 0.4f + 0.6f * blink) : Col(0.6f, 0.62f, 0.66f, 0.8f), 12);
        dl->AddText(ImVec2(a.x + m + 19 * k, a.y + m + 10 * k), Col(1, 1, 1, 0.9f), st);
        char zb[32]; snprintf(zb, sizeof zb, "x%.2f", in.zoom ? *in.zoom : 1.0f); const ImVec2 zs2 = ImGui::CalcTextSize(zb);
        const ImVec2 zp(b.x - m - zs2.x - 10 * k, a.y + m + 8 * k);
        dl->AddRectFilled(ImVec2(zp.x - 6 * k, zp.y - 2 * k), ImVec2(zp.x + zs2.x + 6 * k, zp.y + zs2.y + 2 * k), Col(0, 0, 0, 0.5f), 6 * k);
        dl->AddText(zp, ColA(acc, 1), zb);
        // barra del zoom abajo: 0.1 ... 2
        const float zt = Sat((logf(std::max(0.1f, in.zoom ? *in.zoom : 1.0f)) - logf(0.1f)) / (logf(2.0f) - logf(0.1f)));
        const float bx0 = a.x + VW * 0.25f, bx1 = b.x - VW * 0.25f, by = b.y - m - 6 * k;
        dl->AddLine(ImVec2(bx0, by), ImVec2(bx1, by), Col(1, 1, 1, 0.35f), 2.0f * k);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, ImVec2(bx0 - 16 * k, by - 7 * k), Col(1, 1, 1, 0.7f), "+"); dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, ImVec2(bx1 + 6 * k, by - 7 * k), Col(1, 1, 1, 0.7f), "-");
        dl->AddCircleFilled(ImVec2(bx0 + (bx1 - bx0) * zt, by), (hovV || actV ? 6.0f : 5.0f) * k, Col(1, 1, 1, 1), 14);
        dl->AddRect(a, b, hovV || actV ? ColA(acc, 0.9f) : Col(1, 1, 1, 0.16f), rr, 0, 1.2f * k);
    }
    // derecha: teclas que se iluminan + mouse, y las tiras
    { const float rx = wide ? v0.x + VW + 14.0f * k : v0.x, ry = wide ? v0.y : v0.y + VH + 10.0f * k, rw = wide ? avail - VW - 14.0f * k : avail;
      const float kw = std::min(30.0f * k, (rw - 60.0f * k) / 5.0f), kh = kw, g = 4.0f * k;
      ImGui::SetCursorScreenPos(ImVec2(rx, ry)); ImGui::BeginGroup();   // (el grupo deja la columna alineada en rx)
      ImGui::TextDisabled("%s", TRS("Moverse")); const float ky = ImGui::GetCursorScreenPos().y + 2.0f * k;
      KeyCap(dl, ImVec2(rx, ky), kw, kh, "Q", in.keys[4], k, acc); KeyCap(dl, ImVec2(rx + (kw + g), ky), kw, kh, "W", in.keys[0], k, acc); KeyCap(dl, ImVec2(rx + 2 * (kw + g), ky), kw, kh, "E", in.keys[5], k, acc);
      KeyCap(dl, ImVec2(rx, ky + kh + g), kw, kh, "A", in.keys[1], k, acc); KeyCap(dl, ImVec2(rx + (kw + g), ky + kh + g), kw, kh, "S", in.keys[2], k, acc); KeyCap(dl, ImVec2(rx + 2 * (kw + g), ky + kh + g), kw, kh, "D", in.keys[3], k, acc);
      // mouse con el boton derecho
      { const float mx = rx + 3 * (kw + g) + 14.0f * k, mw = kw * 1.1f, mh = kh * 2 + g; const ImVec2 ma(mx, ky), mb(mx + mw, ky + mh);
        dl->AddRectFilled(ImVec2(ma.x, ma.y + 2.5f * k), ImVec2(mb.x, mb.y + 2.5f * k), Col(0, 0, 0, 0.45f), mw * 0.5f);
        dl->AddRectFilled(ma, mb, Col(1, 1, 1, 0.10f), mw * 0.5f); dl->AddRect(ma, mb, Col(1, 1, 1, 0.22f), mw * 0.5f, 0, 1.0f);
        dl->AddRectFilled(ImVec2(ma.x + mw * 0.5f, ma.y + 1), ImVec2(mb.x - 1, ma.y + mh * 0.42f), in.rmb ? ColA(acc, 0.85f) : Col(1, 1, 1, 0.06f), mw * 0.5f, ImDrawFlags_RoundCornersTopRight);
        dl->AddLine(ImVec2(ma.x + mw * 0.5f, ma.y + 2), ImVec2(ma.x + mw * 0.5f, ma.y + mh * 0.42f), Col(1, 1, 1, 0.25f), 1.0f); dl->AddLine(ImVec2(ma.x + 2, ma.y + mh * 0.42f), ImVec2(mb.x - 2, ma.y + mh * 0.42f), Col(1, 1, 1, 0.25f), 1.0f);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, ImVec2(mb.x + 6.0f * k, ma.y + mh * 0.3f), Col(0.66f, 0.70f, 0.76f, 1), TRS("girar")); }
      ImGui::SetCursorScreenPos(ImVec2(rx, ky + 2 * kh + g + 10.0f * k));
      const float sw = rw;
      if (in.speed) {   // (14/11, Agustin: "control preciso de la velocidad") tira logaritmica 0.05 .. 50 m/s, - / + de 10 %, valores de un clic
          const bool pl1 = in.pad.b[kPadL1] && !in.pad.b[kPadR1], pr1 = in.pad.b[kPadR1] && !in.pad.b[kPadL1];
          const char* mod = in.keys[6] ? "  x4" : in.keys[7] ? "  /4" : pr1 ? "  x3" : pl1 ? "  /4" : "";
          ImGui::Text("%s  %.2f m/s%s", TRS("Velocidad"), *in.speed, mod);
          static const ImVec4 gS[3] = { ImVec4(0.25f, 0.55f, 0.85f, 1), ImVec4(0.30f, 0.80f, 0.55f, 1), ImVec4(1.0f, 0.70f, 0.30f, 1) };
          if (LogStrip("##camspd", in.speed, 0.05f, 50.0f, sw, k, gS, 3, ImVec4(), in.anim)) changed = true;
          static const float kPv[6] = { 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f }; static const char* kPn[6] = { "0.25", "0.5", "1", "2", "5", "10" };
          const float pw = (sw - 5 * 4.0f * k) / 6.0f;
          for (int i = 0; i < 6; i++) { if (i) ImGui::SameLine(0, 4.0f * k); ImGui::PushID(i + 80); const ImVec2 q = ImGui::GetCursorScreenPos();
              if (ImGui::InvisibleButton("##pv", ImVec2(pw, 20.0f * k))) { *in.speed = kPv[i]; changed = true; }
              const bool cur = fabsf(*in.speed - kPv[i]) < kPv[i] * 0.02f, hv = ImGui::IsItemHovered();
              dl->AddRectFilled(q, ImVec2(q.x + pw, q.y + 20.0f * k), cur ? ColA(acc, 0.45f) : Col(1, 1, 1, hv ? 0.14f : 0.07f), 10.0f * k);
              const ImVec2 ts = ImGui::CalcTextSize(kPn[i]); dl->AddText(ImVec2(q.x + (pw - ts.x) * 0.5f, q.y + (20.0f * k - ts.y) * 0.5f), cur ? Col(1, 1, 1, 1) : Col(0.80f, 0.83f, 0.88f, 1), kPn[i]);
              ImGui::PopID(); }
          ImGui::Dummy(ImVec2(0, 2.0f * k)); }
      if (in.sens) { ImGui::Text("%s  %.2f", TRS("Sensibilidad del mouse"), *in.sens);
          if (Strip("##camsens", in.sens, 0.1f, 5.0f, sw, k, nullptr, 0, acc, in.anim)) changed = true; }
      ImGui::EndGroup();
      ImGui::SetCursorScreenPos(ImVec2(v0.x, std::max(ImGui::GetCursorScreenPos().y, v0.y + (wide ? VH : 0.0f)) + 6.0f * k)); }
    if (!wide) ImGui::SetCursorScreenPos(ImVec2(v0.x, ImGui::GetCursorScreenPos().y));
    // --- 3) seguir la cara ---
    if (in.follow) {
        ImGui::Dummy(ImVec2(0, 2.0f * k));
        const int f = std::clamp((int)lroundf(*in.follow), 0, 2);
        const float cw = (avail - 2 * 6.0f * k) / 3.0f;
        static const char* kF[3] = { "No seguir", "Fija al rostro", "Orbitar la cabeza" };
        static const char* kFs[3] = { "camara libre", "pegada a la cara", "angulo del mundo" };
        static const int kFi[3] = { kSiNo, kSiFace, kSiOrbit };
        for (int i = 0; i < 3; i++) { if (i) ImGui::SameLine(0, 6.0f * k); ImGui::PushID(i + 20);
            if (Chip("##cf", kFi[i], TRS(kF[i]), TRS(kFs[i]), f == i, cw, k, t, in.anim)) { *in.follow = (float)i; changed = true; }
            ImGui::PopID(); }
    }
    // --- 4) (14/11) el mando: iconos con lo que hace cada boton, se iluminan al apretar ---
    {   ImGui::Dummy(ImVec2(0, 6.0f * k));
        const bool on = in.padOn && *in.padOn;
        const char* st = TRS(!on ? "apagado" : !in.pad.connected ? "sin mando conectado" : in.padGiven ? "devuelto al juego (OPTIONS)" : in.padFly ? "activo: cerrar el menu y volar" : mode == 2 ? "no vuela en este modo" : mode == 0 ? "prende la camara libre" : mode == 3 && !in.active ? "esperando repeticion" : "listo");
        if (LgHeader(TRS("Mando"), st, on && in.padFly, in.padOn, k, acc, in.anim)) changed = true;
        static const LgItem kIt[] = {
            { kLgStickL, "Moverse", "adelante, atras y costados" }, { kLgStickR, "Mirar", "girar la camara" },
            { kLgL2R2, "Bajar / subir", "segun cuanto se apriete" }, { kLgDpadUD, "Zoom", "acercar / alejar" },
            { kLgL1, "Preciso", "todo a 1/4, para apuntar fino" }, { kLgR1, "Rapido", "x3 mientras se mantiene" },
            { kLgL1R1, "Velocidad base", "mantener + cruceta arriba / abajo" }, { kPadTriangle, "Volver", "a la camara del juego" },
            { kLgL3R3, "Menu", "abrir y cerrar el Insert" }, { kPadOptions, "Soltar el mando", "el juego lo vuelve a ver" } };
        LgItem it[10]; for (int i = 0; i < 10; i++) { it[i] = kIt[i]; it[i].label = TRS(kIt[i].label); it[i].sub = TRS(kIt[i].sub); }
        const float aw = ImGui::GetContentRegionAvail().x;
        LgGrid(it, 10, in.pad, 2, k, acc, in.anim, !on || !in.pad.connected);
    }
    ImGui::Dummy(ImVec2(0, 2.0f * k));
    return changed;
}

// (14/11) TARJETA DE LA CAMARA LENTA: velocidades de un clic con el caracol (que camina a esa velocidad) y donde frena
bool MvSlowWidget(MvSlowIn& in) {
    const float k = in.k; bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x; const double t = in.now;
    const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const bool on = in.on && *in.on > 0.5f; const float sp = in.speed ? *in.speed : 1.0f;
    // velocidades (14/11, Agustin: "agregar valores de mas camara lenta o camara rapida"): fila lenta y fila normal / rapida
    { static const float kV[11] = { 0.02f, 0.05f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
      static const char* kN[11] = { "Casi quieta", "1/20", "1/10", "1/4", "1/2", "3/4", "Normal", "x1.5", "x2", "x3", "x4" };
      for (int row = 0; row < 2; row++) {
          const int i0 = row == 0 ? 0 : 6, n = row == 0 ? 6 : 5;
          ImGui::TextDisabled("%s", TRS(row == 0 ? "Mas lenta" : "Normal y mas rapida"));
          const float bw = (avail - (n - 1) * 6.0f * k) / n, h = 46.0f * k;
          for (int j = 0; j < n; j++) {
              const int i = i0 + j;
              if (j) ImGui::SameLine(0, 6.0f * k); ImGui::PushID(i + 40);
              const ImVec2 p = ImGui::GetCursorScreenPos();
              const bool cur = i == 6 ? !on || fabsf(sp - 1.0f) < 0.01f : on && fabsf(sp - kV[i]) < kV[i] * 0.02f;
              if (ImGui::InvisibleButton("##slow", ImVec2(bw, h))) { if (in.on) *in.on = i == 6 ? 0.0f : 1.0f; if (in.speed && i != 6) *in.speed = kV[i]; changed = true; }
              const bool hv = ImGui::IsItemHovered();
              const float oa = Follow(ImGui::GetItemID(), cur ? 1.0f : 0.0f, 10.0f, in.anim);
              const ImVec4 tint = i > 6 ? ImVec4(1.0f, 0.68f, 0.30f, 1) : acc;   // las rapidas en naranja
              dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + h), ColA(Lerp4(ImVec4(1, 1, 1, hv ? 0.10f : 0.06f), ImVec4(tint.x, tint.y, tint.z, 0.32f), oa), 1), 8.0f * k);
              dl->AddRect(p, ImVec2(p.x + bw, p.y + h), ColA(Lerp4(ImVec4(1, 1, 1, hv ? 0.4f : 0.16f), tint, oa), 1), 8.0f * k, 0, (1.0f + oa) * k);
              // caracol que avanza a esa velocidad; Normal = una pelota que corre; las rapidas, la pelota con estela
              dl->PushClipRect(p, ImVec2(p.x + bw, p.y + h), true);
              const float run = in.anim ? fmodf((float)t * 0.35f * std::min(kV[i], 4.0f) * 3.0f, 1.0f) : 0.5f, x = p.x + 10.0f * k + (bw - 20.0f * k) * run;
              const ImU32 ic = cur ? Col(1, 1, 1, 1) : Col(0.80f, 0.83f, 0.88f, 1);
              if (i < 6) SmallIcon(dl, ImVec2(x, p.y + h * 0.33f), 20.0f * k, kSiSnail, ic, 0.0f);
              else { if (i > 6) for (int s2 = 1; s2 <= 3; s2++) dl->AddLine(ImVec2(x - (4.0f + 5.0f * s2) * k, p.y + h * 0.36f + (s2 - 2) * 2.5f * k), ImVec2(x - (8.0f + 8.0f * s2 + 3.0f * (i - 6)) * k, p.y + h * 0.36f + (s2 - 2) * 2.5f * k), ColA(tint, 0.6f - 0.15f * s2), 1.3f * k);
                     dl->AddCircleFilled(ImVec2(x, p.y + h * 0.36f), 4.5f * k, ic, 12); }
              dl->AddLine(ImVec2(p.x + 6 * k, p.y + h * 0.57f), ImVec2(p.x + bw - 6 * k, p.y + h * 0.57f), Col(1, 1, 1, 0.12f), 1.0f);
              const char* nm = (i == 0 || i == 6) ? TRS(kN[i]) : kN[i]; const ImVec2 ts = ImGui::CalcTextSize(nm);
              dl->AddText(ImVec2(p.x + std::max(3.0f * k, (bw - ts.x) * 0.5f), p.y + h * 0.62f), cur ? Col(1, 1, 1, 1) : Col(0.82f, 0.85f, 0.90f, 1), nm);
              dl->PopClipRect();
              ImGui::PopID();
          }
      }
      // ajuste fino: cualquier valor entre 0.02 y 4 (- / + de 10 %)
      if (in.speed && in.on) {
          float v = on ? sp : 1.0f;
          ImGui::Text("%s  x%.2f", TRS("Ajuste fino"), v);
          static const ImVec4 gT[3] = { ImVec4(0.25f, 0.55f, 0.85f, 1), ImVec4(0.30f, 0.80f, 0.55f, 1), ImVec4(1.0f, 0.68f, 0.30f, 1) };
          if (LogStrip("##slowfine", &v, 0.02f, 4.0f, avail, k, gT, 3, ImVec4(), in.anim)) {
              if (fabsf(v - 1.0f) < 0.015f) { *in.on = 0.0f; *in.speed = 1.0f; } else { *in.on = 1.0f; *in.speed = v; }
              changed = true; }
      }
      ImGui::Dummy(ImVec2(0, 4.0f * k)); }
    // donde frena
    if (in.where) {
        const int w = std::clamp((int)lroundf(*in.where), 0, 2);
        static const char* kW[3] = { "Modo editar", "Partido y repeticiones", "Solo repeticiones" };
        static const char* kWs[3] = { "celebraciones, caras", "todo el reloj del juego", "el partido sigue normal" };
        static const int kWi[3] = { kSiFace, kSiPitch, kSiFilm };
        const float cw = (avail - 2 * 6.0f * k) / 3.0f;
        for (int i = 0; i < 3; i++) { if (i) ImGui::SameLine(0, 6.0f * k); ImGui::PushID(i + 60);
            const char* sub = TRS(kWs[i]); char buf2[64];
            if (i == 2 && w == 2) { snprintf(buf2, sizeof buf2, "%s", TRS(in.replay ? "frenando ahora" : "esperando repeticion")); sub = buf2; }
            if (Chip("##sw", kWi[i], TRS(kW[i]), sub, w == i, cw, k, t, in.anim)) { *in.where = (float)i; changed = true; }
            ImGui::PopID(); }
    }
    // (14/11, Agustin: "como si fuera un modo foto pero que tiene la opcion de avanzar en el modo repeticion en camara lenta")
    // MODO FOTO: con la camara libre volando con el mando, el mando tambien maneja el reloj del juego
    {   ImGui::Dummy(ImVec2(0, 6.0f * k));
        char st[96];
        if (!in.pad.connected) snprintf(st, sizeof st, "%s", TRS("sin mando conectado"));
        else if (!in.padFly) snprintf(st, sizeof st, "%s", TRS("con la camara libre volando con el mando"));
        else if (in.photoPaused) snprintf(st, sizeof st, "%s", TRS(in.photoAdv ? "avanzando despacio" : "en pausa"));
        else if (in.photoScale > 0.0f) snprintf(st, sizeof st, "%s x%.2f", TRS("reloj"), in.photoScale);
        else snprintf(st, sizeof st, "%s", TRS("velocidad del menu"));
        LgHeader(TRS("Modo foto"), st, in.padFly && (in.photoPaused || in.photoScale > 0.0f), nullptr, k, acc, in.anim);
        // riel con los pasos del modo foto (el actual encendido)
        if (in.steps && in.nSteps > 1) {
            const ImVec2 p = ImGui::GetCursorScreenPos(); const float w2 = ImGui::GetContentRegionAvail().x, h2 = 30.0f * k;
            const float x0 = p.x + 14.0f * k, x1 = p.x + w2 - 14.0f * k, y = p.y + 9.0f * k;
            dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), Col(1, 1, 1, 0.18f), 2.0f * k);
            for (int i = 0; i < in.nSteps; i++) {
                const float x = x0 + (x1 - x0) * i / (in.nSteps - 1); const bool cur = in.padFly && in.photoStep == i, nrm = fabsf(in.steps[i] - 1.0f) < 1e-3f;
                const ImVec4 tint = in.steps[i] > 1.01f ? ImVec4(1.0f, 0.68f, 0.30f, 1) : acc;
                const float pr = Follow(ImGui::GetID((int)(900 + i)), cur ? 1.0f : 0.0f, 12.0f, in.anim);
                if (pr > 0.01f) dl->AddCircleFilled(ImVec2(x, y), (6.0f + 4.0f * pr) * k, ColA(tint, 0.25f * pr), 16);
                dl->AddCircleFilled(ImVec2(x, y), (nrm ? 4.0f : 3.0f) * k + 2.0f * k * pr, ColA(Lerp4(ImVec4(0.6f, 0.63f, 0.68f, 1), tint, std::max(pr, nrm ? 0.4f : 0.0f)), 1), 12);
                char lb[12]; if (in.steps[i] < 1.0f) snprintf(lb, sizeof lb, "%g", in.steps[i]); else snprintf(lb, sizeof lb, "x%g", in.steps[i]);
                const float fs = ImGui::GetFontSize() * 0.74f; const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, lb);
                dl->AddText(ImGui::GetFont(), fs, ImVec2(x - ts.x * 0.5f, y + 7.0f * k), cur ? Col(1, 1, 1, 1) : Col(0.62f, 0.66f, 0.72f, 1), lb);
            }
            if (in.padFly && in.photoPaused) {   // pausa: dos barras sobre el paso actual
                const int st2 = std::clamp(in.photoStep, 0, in.nSteps - 1); const float x = x0 + (x1 - x0) * st2 / (in.nSteps - 1);
                const ImU32 pc = in.photoAdv ? ColA(acc, 1) : Col(1, 1, 1, 0.95f);
                if (in.photoAdv) dl->AddTriangleFilled(ImVec2(x - 3.0f * k, y - 15.0f * k), ImVec2(x - 3.0f * k, y - 7.0f * k), ImVec2(x + 4.0f * k, y - 11.0f * k), pc);
                else { dl->AddRectFilled(ImVec2(x - 4.0f * k, y - 15.0f * k), ImVec2(x - 1.5f * k, y - 7.0f * k), pc); dl->AddRectFilled(ImVec2(x + 1.5f * k, y - 15.0f * k), ImVec2(x + 4.0f * k, y - 7.0f * k), pc); }
            }
            ImGui::Dummy(ImVec2(w2, h2));
        }
        static const LgItem kIt[] = {
            { kPadCross, "Pausa / seguir", "congela la repeticion" }, { kPadSquare, "Avanzar despacio", "mantener, en pausa" },
            { kLgDpadLR, "Mas lenta / mas rapida", "pasos de 0.02 a x4" }, { kPadCircle, "Velocidad del menu", "deja el modo foto" } };
        LgItem it[4]; for (int i = 0; i < 4; i++) { it[i] = kIt[i]; it[i].label = TRS(kIt[i].label); it[i].sub = TRS(kIt[i].sub); }
        LgGrid(it, 4, in.pad, 2, k, acc, in.anim, !in.padFly);
    }
    ImGui::Dummy(ImVec2(0, 2.0f * k));
    return changed;
}

} // namespace fx
