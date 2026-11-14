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
// cielo desde arriba: centro = cenit, borde = horizonte. X del mundo a la derecha, Z hacia abajo (como el panel del viento)
static ImVec2 DomePos(ImVec2 c, float R, float azDeg, float elDeg) {
    const float r = R * (1.0f - std::clamp(elDeg, -8.0f, 90.0f) / 90.0f);
    return ImVec2(c.x + sinf(azDeg * kDeg) * r, c.y + cosf(azDeg * kDeg) * r);
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
bool MvSunWidget(MvSunIn& in) {
    if (!in.az || !in.el || !in.on) return false;
    const float k = in.k; bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool side = avail >= 470.0f * k;
    const float D = side ? std::min(avail * 0.46f, 250.0f * k) : std::min(avail, 250.0f * k);
    const float pvW = side ? std::min(avail - D - 14.0f * k, 330.0f * k) : std::min(avail, 330.0f * k), pvH = D;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 c(p0.x + D * 0.5f, p0.y + D * 0.5f); const float R = D * 0.5f - 14.0f * k;
    const bool on = *in.on > 0.5f;
    // donde se dibuja el sol: el que manda el sol libre este cuadro (con giro / recorrido); apagado, el sol del partido
    float wantAz = in.relative && in.realOk ? *in.az + in.realAz : *in.az, wantEl = *in.el;
    if (on && in.finalOk) { wantAz = in.finalAz; wantEl = in.finalEl; }
    else if (!on && in.realOk) { wantAz = in.realAz; wantEl = in.realEl; }
    wantAz = fmodf(wantAz + 720.0f, 360.0f);
    const ImGuiID wid = ImGui::GetID("##soldome");
    ImGui::InvisibleButton("##soldome", ImVec2(D, D));
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    if (act) {   // arrastrar el sol: azimut por el angulo, altura por la distancia al centro; Ctrl = solo el azimut
        const ImVec2 m = ImGui::GetIO().MousePos; const float dx = m.x - c.x, dy = m.y - c.y;
        float az = atan2f(dx, dy) / kDeg; if (az < 0) az += 360.0f;
        const float el = std::clamp(90.0f * (1.0f - sqrtf(dx * dx + dy * dy) / R), -5.0f, 90.0f);
        if (!on) *in.on = 1.0f;
        *in.az = in.relative && in.realOk ? fmodf(az - in.realAz + 720.0f, 360.0f) : az;
        if (!ImGui::GetIO().KeyCtrl) *in.el = roundf(el);
        *in.az = roundf(*in.az);
        wantAz = az; wantEl = ImGui::GetIO().KeyCtrl ? wantEl : *in.el; changed = true;
    }
    if (hov && !act && ImGui::GetIO().MouseWheel != 0.0f) {   // rueda: subir / bajar el sol
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        *in.el = std::clamp(*in.el + ImGui::GetIO().MouseWheel * (ImGui::GetIO().KeyShift ? 0.5f : 2.0f), -5.0f, 90.0f); changed = true;
        if (!on) *in.on = 1.0f;
    }
    const float shAz = Follow(wid + 1, wantAz, 14.0f, in.anim && !act, true), shEl = Follow(wid + 2, wantEl, 14.0f, in.anim && !act);
    const float t = (float)fmod(in.now, 3600.0);
    const float hv = ImGui::GetStateStorage()->GetFloat(wid + 3, 0.0f);
    { float* h = ImGui::GetStateStorage()->GetFloatRef(wid + 3, 0.0f); *h = std::clamp(*h + ((hov || act) ? 1.0f : -1.0f) * ImGui::GetIO().DeltaTime * 6.0f, 0.0f, 1.0f); }
    // --- el cielo ---
    ImVec4 top, hor; SkyColors(shEl, top, hor);
    for (int i = 12; i >= 1; i--) {   // degrade del borde (horizonte) al centro (cenit)
        const float f = i / 12.0f;
        dl->AddCircleFilled(c, R * f, ImGui::GetColorU32(Lerp4(top, hor, f * f)), 48);
    }
    dl->AddCircle(c, R, Col(1, 1, 1, 0.35f + 0.25f * hv), 64, 1.4f * k);
    for (int ring = 1; ring <= 2; ring++) dl->AddCircle(c, R * (1.0f - ring * 30.0f / 90.0f), Col(1, 1, 1, 0.13f), 48, 1.0f);
    char lb[16];
    for (int ring = 0; ring <= 2; ring++) {   // alturas 0 / 30 / 60 grados, en diagonal arriba a la derecha
        snprintf(lb, sizeof lb, "%d", ring * 30); const float rr = R * (1.0f - ring * 30.0f / 90.0f);
        dl->AddText(ImVec2(c.x + rr * 0.707f + 2.0f * k, c.y - rr * 0.707f - ImGui::GetFontSize() * 0.9f), Col(1, 1, 1, 0.45f), lb);
    }
    // la cancha en el medio y un jugador con su sombra (la vista previa de hacia donde van las sombras)
    const float pw = R * 0.78f, ph = pw * 68.0f / 105.0f;
    DrawPitchTop(dl, ImVec2(c.x - pw * 0.5f, c.y - ph * 0.5f), ImVec2(c.x + pw * 0.5f, c.y + ph * 0.5f), 1.0f);
    if (shEl > 0.5f) {
        const float len = std::min(R * 0.62f, (R * 0.11f) / tanf(std::max(shEl, 4.0f) * kDeg));
        const float sx = -sinf(shAz * kDeg), sy = -cosf(shAz * kDeg);
        const ImVec2 e(c.x + sx * len, c.y + sy * len);
        const float wd = 3.2f * k;
        dl->AddQuadFilled(ImVec2(c.x - sy * wd, c.y + sx * wd), ImVec2(c.x + sy * wd, c.y - sx * wd), ImVec2(e.x + sy * wd * 0.6f, e.y - sx * wd * 0.6f), ImVec2(e.x - sy * wd * 0.6f, e.y + sx * wd * 0.6f), Col(0, 0, 0, 0.45f * Smooth(0.5f, 6.0f, shEl)));
    }
    dl->AddCircleFilled(c, 3.6f * k, Col(0.95f, 0.95f, 1.0f, 0.95f), 12);
    // el sol del partido (fantasma) cuando el sol libre esta prendido
    if (on && in.realOk) {
        const ImVec2 rp = DomePos(c, R, in.realAz, in.realEl);
        dl->AddCircle(rp, 7.0f * k, Col(1, 1, 1, 0.60f), 20, 1.3f * k);
        dl->AddCircleFilled(rp, 2.0f * k, Col(1, 1, 1, 0.60f), 8);
    }
    // el sol
    const ImVec2 sp = DomePos(c, R, shAz, shEl);
    const ImVec4 sc = SunColor(in, shEl);
    const float pulse = in.anim ? 0.5f + 0.5f * sinf(t * 2.2f) : 0.5f;
    const float sr = (8.0f + 2.0f * hv + (act ? 1.5f : 0.0f)) * k;
    SunGlyph(dl, sp, sr, on ? sc : ImVec4(sc.x, sc.y, sc.z, 0.55f), in.anim ? t * 0.5f : 0.0f, on ? (0.6f + 0.4f * pulse) * Sat(in.intensity) + 0.3f * hv : 0.25f);
    if (shEl < 0.0f) dl->AddText(ImVec2(sp.x - 18.0f * k, sp.y + 13.0f * k), Col(1, 1, 1, 0.7f), TRS("de noche"));
    if (hov && !act) ImGui::SetTooltip("%s", TRS("Arrastra el sol: el angulo es el Azimut y la distancia al centro la Elevacion (centro = arriba de todo, borde = horizonte). Rueda del mouse = subir o bajar. Ctrl = mover solo el Azimut. Prende el sol libre solo."));
    if (act) {
        char b[64]; snprintf(b, sizeof b, TRS("Azimut %.0f  Elevacion %.0f"), wantAz, *in.el);
        ImDrawList* fg = ImGui::GetForegroundDrawList(); const ImVec2 ts = ImGui::CalcTextSize(b);
        const ImVec2 bp(sp.x - ts.x * 0.5f - 7 * k, sp.y - sr * 2.2f - ts.y - 8 * k);
        fg->AddRectFilled(bp, ImVec2(bp.x + ts.x + 14 * k, bp.y + ts.y + 6 * k), Col(0.10f, 0.12f, 0.16f, 0.92f), 8.0f * k);
        fg->AddText(ImVec2(bp.x + 7 * k, bp.y + 3 * k), IM_COL32_WHITE, b);
    }
    // --- vista previa: como queda la luz (vista de TV: la camara mira la cancha de costado) ---
    ImVec2 q0;
    if (side) q0 = ImVec2(p0.x + D + 14.0f * k, p0.y);
    else { ImGui::Dummy(ImVec2(0, 6.0f * k)); q0 = ImGui::GetCursorScreenPos(); }
    const ImVec2 q1(q0.x + pvW, q0.y + pvH);
    const float rr = 10.0f * k;
    const float hY = q0.y + pvH * 0.52f;   // horizonte
    dl->AddRectFilledMultiColor(q0, ImVec2(q1.x, hY), ImGui::GetColorU32(top), ImGui::GetColorU32(top), ImGui::GetColorU32(hor), ImGui::GetColorU32(hor));
    // tribuna lejana
    dl->AddRectFilled(ImVec2(q0.x, hY - pvH * 0.07f), ImVec2(q1.x, hY), Col(top.x * 0.4f + 0.05f, top.y * 0.4f + 0.05f, top.z * 0.4f + 0.07f, 1));
    // cancha en perspectiva: franjas cada vez mas anchas hacia la camara
    const float L[3] = { cosf(shEl * kDeg) * sinf(shAz * kDeg), sinf(shEl * kDeg), cosf(shEl * kDeg) * cosf(shAz * kDeg) };
    const float day = Smooth(-4.0f, 6.0f, shEl), sunI = on ? Sat(in.intensity / 1.5f) * 0.6f + 0.4f : 1.0f;
    const float gl = (0.25f + 0.75f * day * Sat(L[1] * 1.6f + 0.15f)) * sunI;
    for (int i = 0; i < 8; i++) {
        const float f0 = i / 8.0f, f1 = (i + 1) / 8.0f;
        const float y0 = hY + (q1.y - hY) * f0 * f0, y1 = hY + (q1.y - hY) * f1 * f1;
        const float g = i % 2 ? 0.92f : 1.0f;
        dl->AddRectFilled(ImVec2(q0.x, y0), ImVec2(q1.x, y1), Col((0.12f + 0.25f * gl * sc.x) * g, (0.25f + 0.40f * gl * sc.y) * g, (0.10f + 0.12f * gl * sc.z) * g, 1));
    }
    // pelota (o cabeza): sombreado por puntos con la luz del sol, luz del cielo y el borde encendido a contraluz
    const float br = pvH * 0.17f; const ImVec2 bc(q0.x + pvW * 0.5f, hY + (q1.y - hY) * 0.50f - br);
    if (shEl > 0.0f) {   // sombra en el piso (hacia el lado contrario al sol; larga con el sol bajo)
        const float tl = std::min(3.2f, 1.0f / tanf(std::max(shEl, 5.0f) * kDeg));
        const float ox = -L[0] / std::max(0.08f, sqrtf(L[0] * L[0] + L[2] * L[2])) * tl * br * 0.9f, oz = -L[2] / std::max(0.08f, sqrtf(L[0] * L[0] + L[2] * L[2])) * tl * br * 0.25f;
        const ImVec2 sc0(bc.x + ox * 0.5f, bc.y + br + oz * 0.5f - br * 0.05f);
        const float a = 0.50f * Smooth(0.0f, 8.0f, shEl) * sunI;
        for (int i = 3; i >= 1; i--) {
            const float s = 1.0f + 0.18f * i;
            dl->AddEllipseFilled(sc0, ImVec2((br + fabsf(ox) * 0.5f) * s, (br * 0.22f + fabsf(oz) * 0.5f) * s), Col(0, 0, 0, a / (i + 0.5f)), 0.0f, 28);
        }
    }
    {
        const int N = std::clamp((int)(br * 2.0f / (1.5f * k)), 16, 56);
        const float cell = br * 2.0f / N;
        const ImVec4 sky = Lerp4(top, hor, 0.5f);
        for (int iy = 0; iy < N; iy++) for (int ix = 0; ix < N; ix++) {
            const float u = (ix + 0.5f) / N * 2.0f - 1.0f, v = (iy + 0.5f) / N * 2.0f - 1.0f, rr2 = u * u + v * v;
            if (rr2 > 1.0f) continue;
            const float nz = -sqrtf(1.0f - rr2), n[3] = { u, -v, nz };   // normal (la camara mira hacia +Z)
            const float lam = std::max(0.0f, n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) * day * sunI;
            const float amb = 0.18f + 0.22f * Sat(n[1] * 0.5f + 0.5f);
            const float rim = powf(1.0f - fabsf(nz), 3.0f) * Sat(L[2]) * day * 1.1f;   // sol detras: el borde se enciende (transmision)
            float r = 0.92f * (lam * sc.x + amb * sky.x) + rim * sc.x, g = 0.90f * (lam * sc.y + amb * sky.y) + rim * sc.y * 0.85f, b = 0.86f * (lam * sc.z + amb * sky.z) + rim * sc.z * 0.6f;
            {   // brillo del sol sobre la pelota (Blinn, la camara mira hacia +Z)
                const float hx = L[0], hy = L[1], hz = L[2] - 1.0f, hn = sqrtf(hx * hx + hy * hy + hz * hz) + 1e-4f;
                const float sp = powf(std::max(0.0f, (n[0] * hx + n[1] * hy + n[2] * hz) / hn), 40.0f) * day * sunI * 0.7f;
                r += sp * sc.x; g += sp * sc.y; b += sp * sc.z;
            }
            const float x = bc.x - br + ix * cell, y = bc.y - br + iy * cell;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cell + 0.6f, y + cell + 0.6f), Col(Sat(r), Sat(g), Sat(b), 1));
        }
        dl->AddCircle(bc, br - 0.3f, Col(0, 0, 0, 0.35f), 40, 1.6f);
    }
    // el sol en la vista previa (si esta del lado de la camara se ve arriba del horizonte)
    if (shEl > -2.0f && L[2] > -0.15f) {
        const float sx = q0.x + pvW * (0.5f + 0.5f * L[0] / std::max(0.35f, L[2] + 0.35f) * 0.6f), sy = hY - (pvH * 0.52f - 10.0f * k) * Sat(shEl / 70.0f);
        if (sx > q0.x && sx < q1.x) SunGlyph(dl, ImVec2(sx, sy), 6.0f * k, sc, in.anim ? -t * 0.4f : 0.0f, 0.8f);
    }
    if (on && in.anim) {   // nubes que pasan (z455) o simplemente un brillo que cruza el vidrio
        const float sweep = fmodf(t * 0.18f, 1.6f) - 0.3f;
        const float sx = q0.x + pvW * sweep;
        dl->PushClipRect(q0, q1, true);
        dl->AddRectFilledMultiColor(ImVec2(sx - 30 * k, q0.y), ImVec2(sx, q1.y), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 14), IM_COL32(255, 255, 255, 14), IM_COL32(255, 255, 255, 0));
        dl->AddRectFilledMultiColor(ImVec2(sx, q0.y), ImVec2(sx + 30 * k, q1.y), IM_COL32(255, 255, 255, 14), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 14));
        dl->PopClipRect();
    }
    // marco de vidrio de la vista previa
    {   // las esquinas redondeadas: se tapan con el color de la tarjeta usando un borde grueso
        dl->AddRect(ImVec2(q0.x - 1, q0.y - 1), ImVec2(q1.x + 1, q1.y + 1), ImGui::GetColorU32(ImGuiCol_FrameBg), rr + 1, 0, 2.5f * k);
        dl->AddRect(q0, q1, Col(1, 1, 1, 0.22f), rr, 0, 1.0f);
        dl->AddRectFilledMultiColor(ImVec2(q0.x + 2, q0.y + 2), ImVec2(q1.x - 2, q0.y + pvH * 0.22f), IM_COL32(255, 255, 255, 18), IM_COL32(255, 255, 255, 18), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    }
    {
        char b[96];
        if (on) snprintf(b, sizeof b, TRS("Sol libre: %.0f / %.0f grados"), wantAz, wantEl);
        else snprintf(b, sizeof b, "%s", in.realOk ? TRS("Sol del partido (arrastralo para moverlo)") : TRS("Arrastra el sol para moverlo"));
        dl->AddText(ImVec2(q0.x + 8 * k, q1.y - ImGui::GetFontSize() - 6 * k), Col(1, 1, 1, 0.85f), b);
        dl->AddText(ImVec2(q0.x + 8 * k, q0.y + 6 * k), Col(1, 1, 1, 0.65f), TRS("Vista previa"));
    }
    if (on && in.realOk) {   // leyenda del circulito
        const ImVec2 lp(p0.x + 4.0f * k, p0.y + D - ImGui::GetFontSize() - 2.0f * k);
        dl->AddCircle(ImVec2(lp.x + 6.0f * k, lp.y + ImGui::GetFontSize() * 0.5f), 5.0f * k, Col(1, 1, 1, 0.6f), 16, 1.2f * k);
        dl->AddText(ImVec2(lp.x + 15.0f * k, lp.y), Col(1, 1, 1, 0.6f), TRS("sol del partido"));
    }
    if (side) { ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + D)); ImGui::Dummy(ImVec2(D + 14.0f * k + pvW, 2.0f * k)); }
    else { ImGui::Dummy(ImVec2(pvW, pvH)); }
    if (ImGui::IsMouseHoveringRect(q0, q1) && !act) ImGui::SetTooltip("%s", TRS("Como pega este sol en una pelota y en el pasto, visto desde la camara de TV. Con el sol detras se enciende el borde (la luz que atraviesa orejas y nariz)."));
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
// simbolos: cruz azul, circulo rojo, cuadrado rosa, triangulo verde (dibujados, no son imagenes de nadie)
void MvPadGlyph(ImDrawList* dl, ImVec2 c, float r, int btn, float alpha, float lit) {
    static const ImVec4 kCol[4] = { ImVec4(0.49f, 0.70f, 1.0f, 1), ImVec4(1.0f, 0.42f, 0.45f, 1), ImVec4(0.98f, 0.55f, 0.85f, 1), ImVec4(0.33f, 0.88f, 0.70f, 1) };
    const float th = std::max(1.2f, r * 0.22f);
    if (btn >= kPadCross && btn <= kPadTriangle) {
        const ImVec4 cc = kCol[btn];
        dl->AddCircleFilled(c, r, Col(0.10f + 0.25f * lit * cc.x, 0.11f + 0.25f * lit * cc.y, 0.14f + 0.25f * lit * cc.z, (0.85f) * alpha), 24);
        if (lit > 0.01f) dl->AddCircle(c, r * (1.0f + 0.25f * lit), ColA(cc, 0.6f * lit * alpha), 24, 2.0f);
        const ImU32 sc = ColA(Lerp4(cc, ImVec4(1, 1, 1, 1), 0.35f * lit), alpha);
        const float s = r * 0.48f;
        if (btn == kPadCross) { dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), sc, th); dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), sc, th); }
        else if (btn == kPadCircle) dl->AddCircle(c, s * 1.05f, sc, 20, th);
        else if (btn == kPadSquare) dl->AddRect(ImVec2(c.x - s * 0.9f, c.y - s * 0.9f), ImVec2(c.x + s * 0.9f, c.y + s * 0.9f), sc, 0, 0, th);
        else dl->AddTriangle(ImVec2(c.x, c.y - s * 1.05f), ImVec2(c.x + s * 1.0f, c.y + s * 0.70f), ImVec2(c.x - s * 1.0f, c.y + s * 0.70f), sc, th);
        return;
    }
    // el resto: una pastilla con el nombre
    static const char* kName[kPadCount] = { "", "", "", "", "L1", "R1", "L2", "R2", "SHARE", "OPT", "L3", "R3", "PS", "PAD", "", "", "", "" };
    if (btn >= kPadUp) {   // cruceta
        const float a = r * 0.36f;
        dl->AddRectFilled(ImVec2(c.x - a, c.y - r), ImVec2(c.x + a, c.y + r), Col(0.75f, 0.78f, 0.84f, 0.85f * alpha), 2.0f);
        dl->AddRectFilled(ImVec2(c.x - r, c.y - a), ImVec2(c.x + r, c.y + a), Col(0.75f, 0.78f, 0.84f, 0.85f * alpha), 2.0f);
        return;
    }
    const char* n = kName[btn]; ImFont* f = ImGui::GetFont(); const float fs = r * 1.15f;
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, n);
    const ImVec2 a(c.x - ts.x * 0.5f - r * 0.45f, c.y - r * 0.75f), b(c.x + ts.x * 0.5f + r * 0.45f, c.y + r * 0.75f);
    dl->AddRectFilled(a, b, Col(0.12f + 0.4f * lit, 0.14f + 0.4f * lit, 0.18f + 0.4f * lit, 0.88f * alpha), r * 0.5f);
    dl->AddRect(a, b, Col(1, 1, 1, 0.35f * alpha), r * 0.5f);
    dl->AddText(f, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), Col(1, 1, 1, alpha), n);
}
ImVec2 MvPadSize(float k) { return ImVec2(232.0f * k, 150.0f * k); }
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
    const ImVec2 S = MvPadSize(k);
    const float A = alpha;
    const ImU32 body = Col(0.12f, 0.13f, 0.16f, 0.82f * A), bodyHi = Col(0.30f, 0.32f, 0.38f, 0.55f * A), edge = Col(1, 1, 1, 0.28f * A);
    // gatillos y botones de arriba (L2 / R2 se llenan segun cuanto se aprietan)
    const float tW = 46.0f * k, tH = 14.0f * k;
    const float l2 = p.l2, r2 = p.r2;
    for (int s = 0; s < 2; s++) {
        const float x = s ? o.x + S.x - 30.0f * k - tW : o.x + 30.0f * k;
        const float fill = s ? r2 : l2;
        const ImVec2 a(x, o.y + 2.0f * k), b(x + tW, o.y + 2.0f * k + tH);
        dl->AddRectFilled(a, b, Col(0.10f, 0.11f, 0.14f, 0.75f * A), 6.0f * k, ImDrawFlags_RoundCornersTop);
        if (fill > 0.01f) dl->AddRectFilled(ImVec2(a.x, b.y - tH * fill), b, Col(0.45f, 0.85f, 0.65f, 0.75f * A), 6.0f * k, ImDrawFlags_RoundCornersTop);
        dl->AddText(ImVec2(a.x + tW * 0.5f - 7.0f * k, a.y), Col(1, 1, 1, 0.75f * A), s ? "R2" : "L2");
        const float lb = lit(s ? kPadR1 : kPadL1);
        const ImVec2 a1(x - 2.0f * k, b.y + 1.0f * k), b1(x + tW + 2.0f * k, b.y + 8.0f * k);
        dl->AddRectFilled(a1, b1, Col(0.18f + 0.35f * lb, 0.19f + 0.55f * lb, 0.23f + 0.40f * lb, 0.85f * A), 3.0f * k);
    }
    // cuerpo: una sola silueta (cuerpo + dos agarres) como poligono, asi con poca opacidad no se ven costuras
    const float top = o.y + 26.0f * k;
    {
        ImVector<ImVec2> pts;
        auto arc = [&](ImVec2 c, float rx, float ry, float a0, float a1, int n) { for (int i = 0; i <= n; i++) { const float a = a0 + (a1 - a0) * i / n; pts.push_back(ImVec2(c.x + cosf(a) * rx, c.y + sinf(a) * ry)); } };
        const float x0 = o.x + 12.0f * k, x1 = o.x + S.x - 12.0f * k, r = 24.0f * k, yb = o.y + S.y - 6.0f * k, ym = o.y + S.y - 58.0f * k;
        arc(ImVec2(x0 + r, top + r), r, r, kPI, kPI * 1.5f, 8);                       // arriba a la izquierda
        arc(ImVec2(x1 - r, top + r), r, r, kPI * 1.5f, kPI * 2.0f, 8);                // arriba a la derecha
        arc(ImVec2(x1 - 30.0f * k, yb - 30.0f * k), 30.0f * k, 30.0f * k, 0.0f, kPI * 0.95f, 14);   // agarre derecho
        pts.push_back(ImVec2(o.x + S.x * 0.5f + 46.0f * k, ym + 30.0f * k));
        arc(ImVec2(o.x + S.x * 0.5f, ym + 30.0f * k), 46.0f * k, 9.0f * k, 0.0f, -kPI, 10); // borde de abajo del centro (entre los agarres)
        arc(ImVec2(x0 + 30.0f * k, yb - 30.0f * k), 30.0f * k, 30.0f * k, kPI * 0.05f, kPI, 14);    // agarre izquierdo
        // el arco del borde de abajo va al reves (de derecha a izquierda por arriba): ya queda en orden
        dl->AddConcavePolyFilled(pts.Data, pts.Size, body);
        dl->AddPolyline(pts.Data, pts.Size, edge, ImDrawFlags_Closed, 1.0f);
    }
    // reflejo de vidrio arriba
    dl->AddRectFilledMultiColor(ImVec2(o.x + 30.0f * k, top + 2.0f * k), ImVec2(o.x + S.x - 30.0f * k, top + 20.0f * k), Col(1, 1, 1, 0.10f * A), Col(1, 1, 1, 0.10f * A), Col(1, 1, 1, 0), Col(1, 1, 1, 0));
    // panel tactil (centro)
    {
        const float lt = std::max(lit(kPadTouch), lit(kPadPS) * 0.4f);
        const ImVec2 a(o.x + S.x * 0.5f - 34.0f * k, top + 4.0f * k), b(o.x + S.x * 0.5f + 34.0f * k, top + 34.0f * k);
        dl->AddRectFilled(a, b, Col(0.16f + 0.3f * lt, 0.17f + 0.35f * lt, 0.21f + 0.4f * lt, 0.85f * A), 6.0f * k);
        dl->AddRect(a, b, bodyHi, 6.0f * k);
        // share / options a los costados
        const float ls = lit(kPadShare), lo = lit(kPadOptions);
        dl->AddRectFilled(ImVec2(a.x - 12.0f * k, a.y + 2.0f * k), ImVec2(a.x - 6.0f * k, a.y + 12.0f * k), Col(0.5f + 0.5f * ls, 0.52f + 0.48f * ls, 0.58f + 0.42f * ls, 0.8f * A), 2.0f * k);
        dl->AddRectFilled(ImVec2(b.x + 6.0f * k, a.y + 2.0f * k), ImVec2(b.x + 12.0f * k, a.y + 12.0f * k), Col(0.5f + 0.5f * lo, 0.52f + 0.48f * lo, 0.58f + 0.42f * lo, 0.8f * A), 2.0f * k);
        // boton central
        const float lp = lit(kPadPS);
        dl->AddCircleFilled(ImVec2(o.x + S.x * 0.5f, b.y + 26.0f * k), 5.0f * k, Col(0.35f + 0.6f * lp, 0.38f + 0.6f * lp, 0.45f + 0.5f * lp, 0.85f * A), 16);
    }
    // cruceta
    {
        const ImVec2 c(o.x + 50.0f * k, top + 30.0f * k); const float r = 16.0f * k, w = 5.6f * k;
        struct { int b; float dx, dy; } d[4] = { { kPadUp, 0, -1 }, { kPadDown, 0, 1 }, { kPadLeft, -1, 0 }, { kPadRight, 1, 0 } };
        for (auto& q : d) {
            const float lv = lit(q.b);
            const ImVec2 m(c.x + q.dx * r * 0.55f, c.y + q.dy * r * 0.55f);
            const ImVec2 a(m.x - (q.dx ? r * 0.45f : w), m.y - (q.dy ? r * 0.45f : w)), b(m.x + (q.dx ? r * 0.45f : w), m.y + (q.dy ? r * 0.45f : w));
            dl->AddRectFilled(a, b, Col(0.42f + 0.5f * lv, 0.45f + 0.5f * lv, 0.52f + 0.45f * lv, 0.85f * A), 2.0f * k);
        }
    }
    // botones de la derecha (triangulo arriba, circulo derecha, cruz abajo, cuadrado izquierda)
    {
        const ImVec2 c(o.x + S.x - 50.0f * k, top + 30.0f * k); const float d = 14.0f * k, r = 7.6f * k;
        struct { int b; float dx, dy; } q[4] = { { kPadTriangle, 0, -1 }, { kPadCircle, 1, 0 }, { kPadCross, 0, 1 }, { kPadSquare, -1, 0 } };
        for (auto& e : q) { const float lv = lit(e.b); MvPadGlyph(dl, ImVec2(c.x + e.dx * d, c.y + e.dy * d), r * (1.0f + 0.12f * lv), e.b, A, lv); }
    }
    // analogicos: base, perilla corrida segun el stick y una estela corta
    for (int s = 0; s < 2; s++) {
        const ImVec2 c(o.x + S.x * 0.5f + (s ? 30.0f : -30.0f) * k, o.y + S.y - 50.0f * k);
        const float R = 15.0f * k, sx = s ? p.rx : p.lx, sy = s ? p.ry : p.ly;
        float* fx_ = st->GetFloatRef(base + 40 + s * 2, 0.0f); float* fy_ = st->GetFloatRef(base + 41 + s * 2, 0.0f);
        if (anim) { const float a = 1.0f - expf(-dt * 22.0f); *fx_ += (sx - *fx_) * a; *fy_ += (sy - *fy_) * a; } else { *fx_ = sx; *fy_ = sy; }
        const float lv = lit(s ? kPadR3 : kPadL3);
        dl->AddCircleFilled(c, R, Col(0.07f, 0.08f, 0.10f, 0.9f * A), 28);
        dl->AddCircle(c, R, edge, 28, 1.0f);
        const float mag = sqrtf(*fx_ * *fx_ + *fy_ * *fy_);
        if (mag > 0.05f) {   // estela: marca hacia donde se empuja
            dl->AddLine(c, ImVec2(c.x + *fx_ * R * 0.62f, c.y + *fy_ * R * 0.62f), Col(0.45f, 0.85f, 0.65f, 0.55f * A * Sat(mag)), 3.0f * k);
            dl->AddCircle(c, R * 0.98f, Col(0.45f, 0.85f, 0.65f, 0.45f * A * Sat(mag)), 28, 1.5f * k);
        }
        const ImVec2 kc(c.x + *fx_ * R * 0.55f, c.y + *fy_ * R * 0.55f);
        dl->AddCircleFilled(ImVec2(kc.x, kc.y + 1.5f * k), R * 0.62f, Col(0, 0, 0, 0.35f * A), 24);
        dl->AddCircleFilled(kc, R * 0.60f * (1.0f - 0.08f * lv), Col(0.30f + 0.3f * lv, 0.32f + 0.5f * lv, 0.38f + 0.35f * lv, 0.95f * A), 24);
        dl->AddCircle(kc, R * 0.42f, Col(1, 1, 1, 0.18f * A), 20, 1.0f);
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
