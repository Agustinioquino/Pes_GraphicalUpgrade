// PES21 FX - SOMBRA PROPIA DE LA CABEZA (27/10 c): un shadow map de alta resolucion solo para las cabezas de los jugadores
// mas cercanos a la camara. El shadow map del estadio reparte 2048 px en decenas de metros (2 cm por texel): la nariz, las
// cejas, el menton y el pelo no proyectan una sombra legible sobre la cara. Aca se vuelven a dibujar los dibujos de la pasada
// de sombras del juego (la cascada mas cercana: cara, pelo, cuerpo, camiseta, manos, pelota, lo que sea que el motor mando a
// esa cascada) en un atlas propio de 2048 x 2048 (hasta 3 cabezas de 1024 x 1024: una caja de 1.8 m alrededor de la cabeza,
// 1.8 mm por texel) con una proyeccion ortografica propia: el VS de la pasada de sombras del juego proyecta con
// g_vsScene.m_projectionView (b2) sobre la posicion en el MUNDO (ver 81b5930d), asi que alcanza con darle un b2 propio.
// Los 7 shaders del sol leen el atlas (t30) con la misma caja (filas 310, 316..319 en espacio de vista) y multiplican la sombra.
//   309 (ini): x prendido, y fuerza, z suavidad (texels), w sesgo (texels)
//   310 (DLL): x cantidad de cabezas, yzw direccion hacia el sol en vista | 316: xyz eje "derecha" de la caja en vista, w medio lado
//   317..319 (DLL): centro de cada caja en vista; w = 1 si la profundidad del juego es invertida (0 = cerca)
#include "core.h"
#include <d3d11.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <atomic>

namespace fx {

static const int kHsSlots = 3, kHsAtlas = 2048, kHsSlotPx = 1024;
static const int kHsRowUser = 309, kHsRowInfo = 310, kHsRowAxis = 316, kHsRowCenter = 317;
static ID3D11Texture2D* s_tex = nullptr; static ID3D11DepthStencilView* s_dsv = nullptr; static ID3D11ShaderResourceView* s_srv = nullptr;
static ID3D11Buffer* s_cb[kHsSlots] = {};
static int s_count = 0;                    // cabezas con caja este cuadro
static bool s_reversed = false;            // el juego limpia su shadow map a 0 (profundidad invertida)
static int s_clearFrame = -1;              // cuadro en el que se limpio el atlas
static ID3D11Resource* s_shadowRes = nullptr;   // recurso del shadow map del juego (comparacion, sin referencia propia)
static int s_shadowResFrame = -1;
static std::atomic<ID3D11DepthStencilView*> s_cascadeDSV{nullptr};   // DSV de la primera cascada dibujada despues de la limpieza (la mas cercana)
static float s_half = 0.9f;
std::atomic<int> g_headShadowReplays{0};
static int s_replayLast = 0; static double s_logT = 0;

static bool Ensure() {
    if (s_tex || !g_dev.dev) return s_tex != nullptr;
    D3D11_TEXTURE2D_DESC td{}; td.Width = kHsAtlas; td.Height = kHsAtlas; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_tex))) { Log("sombra cabeza: no se pudo crear el atlas"); return false; }
    D3D11_DEPTH_STENCIL_VIEW_DESC dd{}; dd.Format = DXGI_FORMAT_D32_FLOAT; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = DXGI_FORMAT_R32_FLOAT; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
    if (FAILED(g_dev.dev->CreateDepthStencilView(s_tex, &dd, &s_dsv)) || FAILED(g_dev.dev->CreateShaderResourceView(s_tex, &sd, &s_srv))) { Log("sombra cabeza: no se pudieron crear las vistas"); return false; }
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 512; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    for (int i = 0; i < kHsSlots; i++) if (FAILED(g_dev.dev->CreateBuffer(&bd, nullptr, &s_cb[i]))) { Log("sombra cabeza: no se pudo crear el cbuffer %d", i); return false; }
    Log("sombra cabeza: atlas %dx%d, %d cabezas de %d px", kHsAtlas, kHsAtlas, kHsSlots, kHsSlotPx);
    return true;
}

// mundo -> vista con la matriz de vista del SceneCB (columnas de 4 en memoria, como la lee el VS: v_j = sum_i M[4j+i] w_i + M[4j+3])
static void ToView(const float* M, const float w[3], float v[3], bool point) {
    for (int j = 0; j < 3; j++) v[j] = M[4 * j] * w[0] + M[4 * j + 1] * w[1] + M[4 * j + 2] * w[2] + (point ? M[4 * j + 3] : 0.0f);
}
static void Cross(const float a[3], const float b[3], float o[3]) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
static float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static bool Norm(float a[3]) { float l = sqrtf(Dot(a, a)); if (l < 1e-6f) return false; a[0] /= l; a[1] /= l; a[2] /= l; return true; }

// Present (antes de subir t120): elige las cabezas, arma las matrices y publica las filas
void HeadShadowPresent() {
    const float* U = g_cfg.live[kHsRowUser];
    float* I = g_cfg.live[kHsRowInfo];
    s_count = 0; I[0] = 0.0f;
    if (U[0] < 0.5f || g_cfg.live[101][0] > 0.5f) return;   // apagado, o "sol libre" (la luz no es la del LightCB)
    if (!Ensure() || !g_dev.imm) return;
    std::vector<float> L, S; int lf = -1, sf = -1;
    if (!NamedCBRead("LightCB", L, &lf) || L.size() < 4 || !NamedCBRead("SceneCB", S, &sf) || S.size() < 64) return;
    if (NamedAge("LightCB") > 3) return;   // sin pasada del sol hace poco (menus, editor, noche)
    float Lv[3] = { L[0], L[1], L[2] }; if (!Norm(Lv)) return;
    const float* M = &S[32];   // m_view
    // direccion hacia el sol en el mundo: w_i = sum_j M[4j+i] v_j (inversa de la rotacion de la vista; igual que en hooks.cpp)
    float Lw[3]; for (int i = 0; i < 3; i++) Lw[i] = Lv[0] * M[i] + Lv[1] * M[4 + i] + Lv[2] * M[8 + i];
    if (!Norm(Lw)) return;
    float up[3] = { 0, 1, 0 }; if (fabsf(Lw[1]) > 0.98f) { up[0] = 1; up[1] = 0; }
    float Rw[3]; Cross(up, Lw, Rw); if (!Norm(Rw)) return;
    float Uw[3]; Cross(Lw, Rw, Uw);
    // jugadores: los mas cercanos a la camara que caen en la pantalla
    static CharInfo ci[kCharSlots]; static bool alive[kCharSlots];
    CharSnapshot(ci, alive);
    struct Cand { float cw[3], cv[3], d; };
    std::vector<Cand> cand;
    const float* P = &S[16];   // m_projection
    for (int sl = 0; sl < kCharSlots; sl++) {
        if (!alive[sl] || !ci[sl].node) continue;
        float pos[3]; if (!CharNodeWorldPos(ci[sl].node, pos)) continue;
        Cand c; c.cw[0] = pos[0]; c.cw[1] = pos[1] + 1.45f; c.cw[2] = pos[2];   // centro de la caja: cabeza y hombros de un jugador parado
        ToView(M, c.cw, c.cv, true);
        float clip[4]; for (int j = 0; j < 4; j++) clip[j] = P[4 * j] * c.cv[0] + P[4 * j + 1] * c.cv[1] + P[4 * j + 2] * c.cv[2] + P[4 * j + 3];
        if (clip[3] <= 0.05f) continue;
        float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
        if (fabsf(nx) > 1.4f || fabsf(ny) > 1.4f) continue;
        c.d = sqrtf(Dot(c.cv, c.cv));
        if (c.d > 40.0f) continue;
        cand.push_back(c);
    }
    std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    const float half = s_half;
    int n = (int)std::min<size_t>(cand.size(), kHsSlots);
    for (int i = 0; i < n; i++) {
        const Cand& c = cand[i];
        // m_projectionView propio: columna 0 = x, 1 = y, 2 = z, 3 = w (cada columna son 4 floats seguidos en memoria)
        float cb[128] = {};
        float kz = s_reversed ? 1.0f : -1.0f;   // z lineal: 0 = lo mas cerca del sol (dot(p - c, -L) / 2h + 0.5); invertida: 1 - eso
        cb[0] = Rw[0] / half; cb[1] = Rw[1] / half; cb[2] = Rw[2] / half; cb[3] = -Dot(c.cw, Rw) / half;
        cb[4] = Uw[0] / half; cb[5] = Uw[1] / half; cb[6] = Uw[2] / half; cb[7] = -Dot(c.cw, Uw) / half;
        cb[8] = kz * Lw[0] / (2 * half); cb[9] = kz * Lw[1] / (2 * half); cb[10] = kz * Lw[2] / (2 * half); cb[11] = -kz * Dot(c.cw, Lw) / (2 * half) + 0.5f;
        cb[15] = 1.0f;
        for (int k = 0; k < 16; k++) { cb[16 + k] = cb[k]; }   // m_projection igual (por si un VS proyecta en dos pasos con vista identidad)
        cb[32] = cb[37] = cb[42] = cb[47] = 1.0f;              // m_view = identidad
        g_dev.imm->UpdateSubresource(s_cb[i], 0, nullptr, cb, 0, 0);
        float* C = g_cfg.live[kHsRowCenter + i]; C[0] = c.cv[0]; C[1] = c.cv[1]; C[2] = c.cv[2]; C[3] = s_reversed ? 1.0f : 0.0f;
    }
    float Rv[3]; ToView(M, Rw, Rv, false);
    I[0] = (float)n; I[1] = Lv[0]; I[2] = Lv[1]; I[3] = Lv[2];
    float* A = g_cfg.live[kHsRowAxis]; A[0] = Rv[0]; A[1] = Rv[1]; A[2] = Rv[2]; A[3] = half;
    s_count = n;
    double t = Now();
    if (t - s_logT > 10.0) { s_logT = t; int r = g_headShadowReplays.exchange(0); Log("sombra cabeza: %d cabezas, %d dibujos replicados en 10 s (z %s)", n, r, s_reversed ? "invertida" : "normal"); }
}

int HeadShadowSlots() { return s_count; }
ID3D11ShaderResourceView* HeadShadowSRV() { return s_count > 0 ? s_srv : nullptr; }
ID3D11DepthStencilView* HeadShadowDSV() { return s_dsv; }
ID3D11Buffer* HeadShadowCB(int i) { return (i >= 0 && i < kHsSlots) ? s_cb[i] : nullptr; }
void HeadShadowViewport(int i, D3D11_VIEWPORT& vp) { vp.TopLeftX = (float)((i % 2) * kHsSlotPx); vp.TopLeftY = (float)((i / 2) * kHsSlotPx); vp.Width = vp.Height = (float)kHsSlotPx; vp.MinDepth = 0; vp.MaxDepth = 1; }

// el DSV es el shadow map del sol del juego? (se compara el recurso con el SRV que las reglas guardan como "ShadowMap")
bool HeadShadowIsGameShadow(ID3D11DepthStencilView* v) {
    if (!v || s_count <= 0) return false;
    int f = PresentNo();
    if (s_shadowResFrame != f) { s_shadowResFrame = f; s_shadowRes = NamedSRVResource("ShadowMap"); }
    if (!s_shadowRes) return false;
    ID3D11Resource* r = nullptr; v->GetResource(&r); if (!r) return false;
    bool same = (r == s_shadowRes); r->Release();
    return same;
}
// el juego limpia su shadow map: se limpia el atlas con el mismo valor (y se aprende si la profundidad es invertida)
void HeadShadowOnClear(ID3D11DeviceContext* c, ID3D11DepthStencilView* v, float depth) {
    if (!HeadShadowIsGameShadow(v) || !s_dsv) return;
    bool rev = depth < 0.5f;
    if (rev != s_reversed) { s_reversed = rev; Log("sombra cabeza: el juego limpia el shadow map a %.0f (profundidad %s)", depth, rev ? "invertida" : "normal"); }
    int f = PresentNo();
    if (s_clearFrame == f) return;
    s_clearFrame = f;
    s_cascadeDSV.store(nullptr);
    c->ClearDepthStencilView(s_dsv, D3D11_CLEAR_DEPTH, depth, 0);
}
// el dibujo es de la cascada mas cercana? (vista en el origen del shadow map y el primer DSV que se dibuja despues de la limpieza:
// si cada cascada tiene su propia vista del recurso, se replica solo la primera; si comparten DSV con vistas corridas, solo la de (0, 0))
bool HeadShadowIsNearCascade(ID3D11DeviceContext* c, ID3D11DepthStencilView* dsv) {
    D3D11_VIEWPORT vp; UINT n = 1; c->RSGetViewports(&n, &vp);
    if (n < 1 || vp.TopLeftX > 0.5f || vp.TopLeftY > 0.5f) return false;
    ID3D11DepthStencilView* exp = nullptr;
    if (s_cascadeDSV.compare_exchange_strong(exp, dsv)) return true;
    return exp == dsv;
}
void HeadShadowRelease() {
    for (int i = 0; i < kHsSlots; i++) { if (s_cb[i]) s_cb[i]->Release(); s_cb[i] = nullptr; }
    if (s_srv) s_srv->Release(); if (s_dsv) s_dsv->Release(); if (s_tex) s_tex->Release(); s_srv = nullptr; s_dsv = nullptr; s_tex = nullptr;
}

} // namespace fx
