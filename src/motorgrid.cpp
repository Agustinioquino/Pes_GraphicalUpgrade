// ---------------------------------------------------------------- (15/11) grilla de piso del Motor en tiempo real
// Agustin 01:51: "enfocate en el menu en si, sin fondo con grilla". El estudio del Motor no muestra el fondo del Editar: en su
// lugar va un fondo gris oscuro con una grilla de piso en perspectiva (como la de Blender), con el eje X en rojo y el Z en azul.
// Se dibuja en una textura propia (CPU, 960 x 540) con la camara libre del Motor (freecam.cpp: posicion, giro y campo de vision)
// y se entrega como fondo en el mismo dibujo donde antes iba el negro (glass.cpp EditorBgFor): queda DETRAS del jugador y de los
// paneles, no encima. Una vez por cuadro. [PES21FX] motor_grilla = 0 deja el fondo negro liso.
#include "core.h"
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
static void Seg(const Cam& c, const float* a, const float* b, uint32_t rgb, float alpha, float fadeDist) {
    float pa[3], pb[3];
    for (int k = 0; k < 2; k++) {
        const float* p = k ? b : a; float* o = k ? pb : pa; const float d[3] = { p[0] - c.pos[0], p[1] - c.pos[1], p[2] - c.pos[2] };
        o[0] = d[0] * c.r[0] + d[1] * c.r[1] + d[2] * c.r[2]; o[1] = d[0] * c.u[0] + d[1] * c.u[1] + d[2] * c.u[2]; o[2] = d[0] * c.f[0] + d[1] * c.f[1] + d[2] * c.f[2];
    }
    const float zn = 0.05f;
    if (pa[2] < zn && pb[2] < zn) return;
    if (pa[2] < zn) { const float t = (zn - pa[2]) / (pb[2] - pa[2]); for (int i = 0; i < 3; i++) pa[i] += (pb[i] - pa[i]) * t; }
    if (pb[2] < zn) { const float t = (zn - pb[2]) / (pa[2] - pb[2]); for (int i = 0; i < 3; i++) pb[i] += (pa[i] - pb[i]) * t; }
    const float asp = (float)kW / (float)kH, hx = 0.5f * kW, hy = 0.5f * kH;
    float x0 = hx + pa[0] / (pa[2] * c.th * asp) * hx, y0 = hy - pa[1] / (pa[2] * c.th) * hy;
    float x1 = hx + pb[0] / (pb[2] * c.th * asp) * hx, y1 = hy - pb[1] / (pb[2] * c.th) * hy;
    // recorte grueso al rectangulo agrandado (para no recorrer lineas enormes)
    const float lo = -2000.0f, hiX = kW + 2000.0f, hiY = kH + 2000.0f;
    if ((x0 < lo && x1 < lo) || (x0 > hiX && x1 > hiX) || (y0 < lo && y1 < lo) || (y0 > hiY && y1 > hiY)) return;
    if (std::fabs(x0) > 6000.0f || std::fabs(y0) > 6000.0f || std::fabs(x1) > 6000.0f || std::fabs(y1) > 6000.0f) {
        // una punta muy lejos de la pantalla: se parte en tramos y se dibujan solo los que caen cerca
        float m[3]; for (int i = 0; i < 3; i++) m[i] = 0.5f * (a[i] + b[i]);
        const float l2 = (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
        if (l2 > 0.01f) { Seg(c, a, m, rgb, alpha, fadeDist); Seg(c, m, b, rgb, alpha, fadeDist); }
        return;
    }
    const float f0 = std::clamp(1.0f - pa[2] / fadeDist, 0.0f, 1.0f), f1 = std::clamp(1.0f - pb[2] / fadeDist, 0.0f, 1.0f);
    Line(x0, y0, x1, y1, rgb, alpha * f0, alpha * f1);
}

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
    // --- fondo: gris oscuro con un degrade suave hacia arriba ---
    for (int y = 0; y < kH; y++) {
        const float t = (float)y / (float)(kH - 1); const int v = (int)(30.0f + 14.0f * (1.0f - t));
        const uint32_t col = 0xff000000u | (uint32_t)v | ((uint32_t)v << 8) | ((uint32_t)(v + 3) << 16);
        uint32_t* row = &s_px[(size_t)y * kW]; for (int x = 0; x < kW; x++) row[x] = col;
    }
    // --- grilla: celdas de 1 m, lineas mas fuertes cada 5 m, 40 x 40 m alrededor de la camara ---
    const int half = 20; const float fade = 9.0f + 6.0f * std::fabs(c.pos[1]) + 0.45f / c.th;
    const int cx = (int)std::floor(c.pos[0] + 0.5f), cz = (int)std::floor(c.pos[2] + 0.5f);
    for (int i = -half; i <= half; i++) {
        const int gx = cx + i, gz = cz + i;
        {   const float a[3] = { (float)gx, 0, (float)(cz - half) }, b[3] = { (float)gx, 0, (float)(cz + half) };
            if (gx == 0) Seg(c, a, b, 0x00e08c40u, 0.95f, fade * 1.6f);            // eje Z (azul; el color va en BGR dentro del u32 RGBA)
            else Seg(c, a, b, 0x00909090u, (gx % 5 == 0) ? 0.50f : 0.24f, fade); }
        {   const float a[3] = { (float)(cx - half), 0, (float)gz }, b[3] = { (float)(cx + half), 0, (float)gz };
            if (gz == 0) Seg(c, a, b, 0x005448eau, 0.95f, fade * 1.6f);            // eje X (rojo)
            else Seg(c, a, b, 0x00909090u, (gz % 5 == 0) ? 0.50f : 0.24f, fade); }
    }
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(ctx->Map(s_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)) && ms.pData) {
        for (int y = 0; y < kH; y++) memcpy((uint8_t*)ms.pData + (size_t)y * ms.RowPitch, &s_px[(size_t)y * kW], (size_t)kW * 4);
        ctx->Unmap(s_tex, 0);
    }
    return s_srv;
}

} // namespace fx
