// ---------------------------------------------------------------- (15/11) grilla de piso del Motor en tiempo real
// Agustin 01:51: "enfocate en el menu en si, sin fondo con grilla". El estudio del Motor no muestra el fondo del Editar: en su
// lugar va un fondo gris oscuro con una grilla de piso en perspectiva (como la de Blender), con el eje X en rojo y el Z en azul.
// Se dibuja en una textura propia (CPU, 960 x 540) con la camara libre del Motor (freecam.cpp: posicion, giro y campo de vision)
// y se entrega como fondo en el mismo dibujo donde antes iba el negro (glass.cpp EditorBgFor): queda DETRAS del jugador y de los
// paneles, no encima. Una vez por cuadro. [PES21FX] motor_grilla = 0 deja el fondo gris sin lineas (15/11 o; antes negro).
#ifndef MOTORGRID_TEST
#include "core.h"
#endif
#include <atomic>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

namespace fx {

static const int kW = 960, kH = 540;
static ID3D11Texture2D* s_tex = nullptr; static ID3D11ShaderResourceView* s_srv = nullptr; static bool s_tried = false;
static std::vector<uint32_t> s_px;
static std::atomic<bool> s_fresh{false};       // ya se dibujo en este cuadro
// (15/11 o) relacion de aspecto de la PANTALLA: la textura de 960 x 540 se estira a toda la pantalla; con un monitor 21:9 (el de
// Agustin, 2560 x 1080) la grilla se proyectaba como 16:9 y quedaba estirada de costado (no seguia a la camara como el jugador)
static float s_asp = 16.0f / 9.0f;

void MotorGridPresent() { s_fresh.store(false, std::memory_order_relaxed); }

static inline void Plot(int x, int y, uint32_t rgb, float a) {
    if ((unsigned)x >= (unsigned)kW || (unsigned)y >= (unsigned)kH || a <= 0.0f) return;
    uint32_t& d = s_px[(size_t)y * kW + x]; if (a > 1.0f) a = 1.0f;
    const int ia = (int)(a * 256.0f);
    const int r = (int)(d & 255), g = (int)((d >> 8) & 255), b = (int)((d >> 16) & 255);
    const int nr = r + ((((int)(rgb & 255)) - r) * ia >> 8), ng = g + ((((int)((rgb >> 8) & 255)) - g) * ia >> 8), nb = b + ((((int)((rgb >> 16) & 255)) - b) * ia >> 8);
    d = 0xff000000u | (uint32_t)nr | ((uint32_t)ng << 8) | ((uint32_t)nb << 16);
}
// linea con suavizado simple (dos pixeles por paso); a0 / a1 = opacidad en cada punta
static void Line(float x0, float y0, float x1, float y1, uint32_t rgb, float a0, float a1) {
    const float dx = x1 - x0, dy = y1 - y0; const float len = std::max(std::fabs(dx), std::fabs(dy));
    if (!(len >= 0.0f) || len > 8000.0f) return;
    const int n = (int)len + 1; const bool steep = std::fabs(dy) > std::fabs(dx);
    for (int i = 0; i <= n; i++) {
        const float t = n > 0 ? (float)i / (float)n : 0.0f; const float x = x0 + dx * t, y = y0 + dy * t, a = a0 + (a1 - a0) * t;
        if (steep) { const int xi = (int)std::floor(x); const float fr = x - (float)xi; Plot(xi, (int)(y + 0.5f), rgb, a * (1.0f - fr)); Plot(xi + 1, (int)(y + 0.5f), rgb, a * fr); }
        else { const int yi = (int)std::floor(y); const float fr = y - (float)yi; Plot((int)(x + 0.5f), yi, rgb, a * (1.0f - fr)); Plot((int)(x + 0.5f), yi + 1, rgb, a * fr); }
    }
}

struct Cam { float pos[3], f[3], r[3], u[3], th; };
// segmento del mundo -> pantalla, recortado contra el plano cercano; la opacidad baja con la distancia
static void Seg(const Cam& c, const float* a, const float* b, uint32_t rgb, float alpha, float fadeDist, float ka = 1.0f, float kb = 1.0f) {
    float pa[3], pb[3];
    for (int k = 0; k < 2; k++) {
        const float* p = k ? b : a; float* o = k ? pb : pa; const float d[3] = { p[0] - c.pos[0], p[1] - c.pos[1], p[2] - c.pos[2] };
        o[0] = d[0] * c.r[0] + d[1] * c.r[1] + d[2] * c.r[2]; o[1] = d[0] * c.u[0] + d[1] * c.u[1] + d[2] * c.u[2]; o[2] = d[0] * c.f[0] + d[1] * c.f[1] + d[2] * c.f[2];
    }
    const float zn = 0.05f;
    if (pa[2] < zn && pb[2] < zn) return;
    if (pa[2] < zn) { const float t = (zn - pa[2]) / (pb[2] - pa[2]); for (int i = 0; i < 3; i++) pa[i] += (pb[i] - pa[i]) * t; }
    if (pb[2] < zn) { const float t = (zn - pb[2]) / (pa[2] - pb[2]); for (int i = 0; i < 3; i++) pb[i] += (pa[i] - pb[i]) * t; }
    const float asp = s_asp, hx = 0.5f * kW, hy = 0.5f * kH;
    float x0 = hx + pa[0] / (pa[2] * c.th * asp) * hx, y0 = hy - pa[1] / (pa[2] * c.th) * hy;
    float x1 = hx + pb[0] / (pb[2] * c.th * asp) * hx, y1 = hy - pb[1] / (pb[2] * c.th) * hy;
    // recorte grueso al rectangulo agrandado (para no recorrer lineas enormes)
    const float lo = -2000.0f, hiX = kW + 2000.0f, hiY = kH + 2000.0f;
    if ((x0 < lo && x1 < lo) || (x0 > hiX && x1 > hiX) || (y0 < lo && y1 < lo) || (y0 > hiY && y1 > hiY)) return;
    if (std::fabs(x0) > 6000.0f || std::fabs(y0) > 6000.0f || std::fabs(x1) > 6000.0f || std::fabs(y1) > 6000.0f) {
        // una punta muy lejos de la pantalla: se parte en tramos y se dibujan solo los que caen cerca
        float m[3]; for (int i = 0; i < 3; i++) m[i] = 0.5f * (a[i] + b[i]);
        const float l2 = (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
        const float km = 0.5f * (ka + kb);
        if (l2 > 0.01f) { Seg(c, a, m, rgb, alpha, fadeDist, ka, km); Seg(c, m, b, rgb, alpha, fadeDist, km, kb); }
        return;
    }
    // ka / kb: opacidad de cada punta del tramo (la grilla la calcula por distancia en el piso); si se recorto contra el plano
    // cercano, la punta recortada toma la de la otra
    const bool ca = pa[2] <= 0.0501f, cb = pb[2] <= 0.0501f; const float qa = ca ? kb : ka, qb = cb ? ka : kb;
    const float f0 = std::clamp(1.0f - pa[2] / fadeDist, 0.0f, 1.0f) * qa, f1 = std::clamp(1.0f - pb[2] / fadeDist, 0.0f, 1.0f) * qb;
    Line(x0, y0, x1, y1, rgb, alpha * f0, alpha * f1);
}

// pinta el fondo y la grilla en s_px con la camara c
static void GridPaint(const Cam& c) {
    // --- fondo: gris oscuro con un degrade suave hacia arriba ---
    for (int y = 0; y < kH; y++) {
        const float t = (float)y / (float)(kH - 1); const int v = (int)(30.0f + 14.0f * (1.0f - t));
        const uint32_t col = 0xff000000u | (uint32_t)v | ((uint32_t)v << 8) | ((uint32_t)(v + 3) << 16);
        uint32_t* row = &s_px[(size_t)y * kW]; for (int x = 0; x < kW; x++) row[x] = col;
    }
    // --- grilla (15/11 o, Agustin 05:14: "al desactivar la grilla del piso todo se pone negro, la grilla a cierta distancia pierde la
    // simetria"). Apagada queda el fondo gris de arriba, sin lineas. Antes era un cuadrado alrededor de la camara que se apagaba por la
    // profundidad de cada punta (interpolada en pantalla): de costado se cortaba en las esquinas del cuadrado y cada linea se apagaba
    // distinto. Ahora: un CIRCULO alrededor de la camara, cada linea partida en tramos cortos y cada punta apagada por su distancia real
    // en el piso (igual para todos lados), y las lineas finas se apagan cuando quedan a menos de ~3 pixeles de su vecina (lejos se
    // juntaban y hacian dibujos raros). Opciones: [PES21FX] motor_grilla_celda (m), motor_grilla_linea_fuerte (cada N celdas, 0 = sin),
    // motor_grilla_intensidad (0..200 %), motor_grilla_ejes, motor_grilla_tamano (m de lado; el circulo es de ese diametro).
    if (g_cfg.motorGrid != 0) {
        const float cell = std::clamp(g_cfg.motorGridCell, 0.1f, 10.0f); const int major = std::clamp(g_cfg.motorGridMajor, 0, 50);
        const float gain = std::clamp((float)g_cfg.motorGridAlpha, 0.0f, 200.0f) / 100.0f;
        // (15/11 q, Agustin 17:28: "que la grilla se renderize con la distancia al maximo") tamano 0 = MAXIMA: hasta 2 km (el horizonte
        // del visor). Para que no cueste: por niveles, como los editores. Nivel 0 = la celda hasta 60 celdas de la camara; nivel 1 = cada
        // M celdas (M = la linea fuerte, o 10) desde ahi hasta 60 x M celdas; nivel 2 = cada M x M... Cada nivel dibuja solo el anillo que
        // el anterior no cubre (sin lineas dobles) y sus tramos son mas largos cuanto mas lejos.
        const bool maxi = g_cfg.motorGridSize <= 0;
        const float Rmax = maxi ? 2000.0f : std::min((float)std::clamp(g_cfg.motorGridSize, 10, 199) * 0.5f, cell * 600.0f);
        const float fade = maxi ? Rmax : std::min(Rmax, (9.0f + 6.0f * std::fabs(c.pos[1]) + 0.45f / c.th) * std::max(1.0f, Rmax / 20.0f));
        const int M = major > 1 ? major : 10;
        const float px = c.pos[0], pz = c.pos[2];
        const float asp = s_asp, hx = 0.5f * kW, hy = 0.5f * kH;
        auto proj = [&](const float* w, float& sx, float& sy) {      // false = detras de la camara
            const float d[3] = { w[0] - c.pos[0], w[1] - c.pos[1], w[2] - c.pos[2] };
            const float z = d[0] * c.f[0] + d[1] * c.f[1] + d[2] * c.f[2]; if (z < 0.05f) return false;
            sx = hx + (d[0] * c.r[0] + d[1] * c.r[1] + d[2] * c.r[2]) / (z * c.th * asp) * hx; sy = hy - (d[0] * c.u[0] + d[1] * c.u[1] + d[2] * c.u[2]) / (z * c.th) * hy; return true; };
        // separacion en pantalla entre esta linea y su vecina (a "st" de distancia por el otro eje), en pixeles
        auto spacing = [&](const float* m, int axis, float st) {
            float n[3] = { m[0], 0.0f, m[2] }; n[axis] += st; float t[3] = { m[0], 0.0f, m[2] }; t[2 - axis] += st * 0.5f;
            float ax, ay, bx, by, tx, ty; if (!proj(m, ax, ay) || !proj(n, bx, by) || !proj(t, tx, ty)) return 1.0e6f;
            float dx = tx - ax, dy = ty - ay; const float l = sqrtf(dx * dx + dy * dy); if (l < 1.0e-4f) return 1.0e6f; dx /= l; dy /= l;
            return std::fabs((bx - ax) * dy - (by - ay) * dx); };
        // (15/11 r, Agustin 18:43: "la grilla sigue desapareciendo en el horizonte") en Maxima no se apaga por distancia hasta el
        // ultimo 15 % del radio de 2 km
        auto fadeAt = [&](float x, float z) { const float d = sqrtf((x - px) * (x - px) + (z - pz) * (z - pz)) / fade;
            const float a = maxi ? std::clamp((1.0f - d) / 0.15f, 0.0f, 1.0f) : std::clamp(1.0f - d, 0.0f, 1.0f); return a * a * (3.0f - 2.0f * a); };
        float rIn = 0.0f, step = cell; long long mult = 1;
        for (int lv = 0; lv < 5 && rIn < Rmax; lv++, step *= (float)M, mult *= M) {
            const float R = maxi ? std::min(Rmax, step * 60.0f) : Rmax;
            for (int axis = 0; axis < 2; axis++) {          // 0: lineas de x fija (corren por z, el eje Z azul) / 1: de z fija (el eje X rojo)
                const float pc = axis == 0 ? px : pz, po = axis == 0 ? pz : px;
                const long long k0 = (long long)std::ceil((pc - R) / step), k1 = (long long)std::floor((pc + R) / step);
                for (long long kk = k0; kk <= k1; kk++) {
                    const float v = (float)kk * step, dv = v - pc; const float hc = sqrtf(std::max(0.0f, R * R - dv * dv)); if (hc < 1.0e-3f) continue;
                    const float hi = rIn > std::fabs(dv) ? sqrtf(rIn * rIn - dv * dv) : 0.0f;      // tramo que ya dibujo el nivel anterior
                    const long long kc = kk * mult;      // numero de la linea en celdas
                    const bool ax0 = kk == 0 && g_cfg.motorGridAxes != 0, maj = major > 0 && kc % major == 0;
                    const uint32_t col = ax0 ? (axis == 0 ? 0x00e08c40u : 0x005448eau) : 0x00909090u;
                    const float base = (ax0 ? 0.95f : maj ? 0.50f : 0.24f) * gain; if (base <= 0.003f) continue;
                    for (int side = 0; side < 2; side++) {      // los dos lados del anillo (o la linea entera si no hay anillo adentro)
                        float t = side == 0 ? po - hc : po + hi; const float tEnd = side == 0 ? (hi > 0.0f ? po - hi : po + hc) : po + hc;
                        if (side == 1 && hi <= 0.0f) break;
                        while (t < tEnd) {
                            const float dist = std::fabs(dv) + std::fabs(t - po); const float sg = std::clamp(dist * 0.18f, 0.25f, 6.0f * (float)mult); const float t2 = std::min(tEnd, t + sg);
                            float a[3], b[3]; if (axis == 0) { a[0] = b[0] = v; a[2] = t; b[2] = t2; } else { a[2] = b[2] = v; a[0] = t; b[0] = t2; } a[1] = b[1] = 0.0f;
                            float fa = fadeAt(a[0], a[2]), fb = fadeAt(b[0], b[2]);
                            if (fa > 0.003f || fb > 0.003f) {
                                float sp = 1.0f;
                                if (!ax0) { float m[3] = { (a[0] + b[0]) * 0.5f, 0.0f, (a[2] + b[2]) * 0.5f }; const float s1 = spacing(m, axis, step) * (maj && mult < major ? (float)major / (float)mult : 1.0f); sp = std::clamp((s1 - 2.0f) / 4.0f, 0.0f, 1.0f);
                                    // en Maxima las lineas fuertes y las del ultimo nivel no se apagan del todo al juntarse cerca del horizonte:
                                    // se funden en una franja suave en vez de cortarse
                                    if (maxi && (maj || R >= Rmax || lv >= 3)) sp = std::max(sp, 0.30f); }
                                if (sp > 0.003f) Seg(c, a, b, col, base * sp, 1.0e9f, fa, fb);
                            }
                            t = t2;
                        }
                    }
                }
            }
            rIn = R;
            if (!maxi) break;
        }
    }
}

#ifndef MOTORGRID_TEST
// fondo del estudio del Motor para este cuadro (nullptr = no se pudo: va el negro)
ID3D11ShaderResourceView* MotorGridSRV(ID3D11DeviceContext* ctx) {
    if (!g_dev.dev || !ctx) return nullptr;
    if (!s_tried) {
        s_tried = true;
        D3D11_TEXTURE2D_DESC td{}; td.Width = kW; td.Height = kH; td.MipLevels = td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DYNAMIC; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (SUCCEEDED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_tex)) && s_tex) g_dev.dev->CreateShaderResourceView(s_tex, nullptr, &s_srv);
        s_px.assign((size_t)kW * kH, 0xff000000u);
        Log("motor (grilla): textura de fondo %d x %d %s", kW, kH, s_srv ? "lista" : "NO se pudo crear: queda el fondo negro");
    }
    if (!s_srv) return nullptr;
    if (s_fresh.exchange(true)) return s_srv;
    // --- camara ---
    Cam c{}; float yaw = 3.14159f, pitch = 0.0f;
    if (!FreeCamGetPose(c.pos, &yaw, &pitch)) { c.pos[0] = 0; c.pos[1] = 0.95f; c.pos[2] = 6.5f; yaw = 3.14159f; pitch = 0.0f; }
    const float cp = cosf(pitch), sp = sinf(pitch);
    c.f[0] = cp * sinf(yaw); c.f[1] = sp; c.f[2] = cp * cosf(yaw);
    c.r[0] = -c.f[2]; c.r[1] = 0; c.r[2] = c.f[0]; { const float l = sqrtf(c.r[0] * c.r[0] + c.r[2] * c.r[2]); if (l > 1e-5f) { c.r[0] /= l; c.r[2] /= l; } else { c.r[0] = 1; c.r[2] = 0; } }
    c.u[0] = c.r[1] * c.f[2] - c.r[2] * c.f[1]; c.u[1] = c.r[2] * c.f[0] - c.r[0] * c.f[2]; c.u[2] = c.r[0] * c.f[1] - c.r[1] * c.f[0];
    c.th = tanf(0.5f * std::clamp(FreeCamFovNow(), 0.02f, 2.4f));
    s_asp = (g_bbW > 0 && g_bbH > 0) ? std::clamp((float)g_bbW / (float)g_bbH, 0.5f, 5.0f) : 16.0f / 9.0f;
    // (15/11 o) si la camara y las opciones no cambiaron desde el ultimo dibujo, la textura ya esta bien: no se rehace
    {   struct Key { float p[3], yaw, pitch, th, cell, asp; int on, size, alpha, major, axes; };
        static Key s_last{}; static bool s_have = false; Key kk{}; memcpy(kk.p, c.pos, sizeof kk.p); kk.yaw = yaw; kk.pitch = pitch; kk.th = c.th; kk.cell = g_cfg.motorGridCell; kk.asp = s_asp;
        kk.on = g_cfg.motorGrid; kk.size = g_cfg.motorGridSize; kk.alpha = g_cfg.motorGridAlpha; kk.major = g_cfg.motorGridMajor; kk.axes = g_cfg.motorGridAxes;
        if (s_have && memcmp(&kk, &s_last, sizeof kk) == 0) return s_srv;
        s_last = kk; s_have = true; }
    GridPaint(c);
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(ctx->Map(s_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)) && ms.pData) {
        for (int y = 0; y < kH; y++) memcpy((uint8_t*)ms.pData + (size_t)y * ms.RowPitch, &s_px[(size_t)y * kW], (size_t)kW * 4);
        ctx->Unmap(s_tex, 0);
    }
    return s_srv;
}

#endif
} // namespace fx
