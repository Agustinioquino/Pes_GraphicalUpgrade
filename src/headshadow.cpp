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
#include <map>

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
// (27/10 d) diagnostico: la primera version no replico ningun dibujo (log de Agustin: "0 dibujos replicados") y casi nunca
// encontro cabezas. Se cuenta cada paso para ver donde se corta
static std::atomic<int> s_dBindAny{0}, s_dBindRT{0}, s_dBindOk{0}, s_dDraws[4], s_dVpOff{0}, s_dOtherDsv{0};
static std::atomic<int> s_dVpX{-1}, s_dVpY{-1}, s_dVpW{0}, s_dVpH{0}, s_dRtN{-1};
static int s_cAlive = 0, s_cNoPos = 0, s_cBehind = 0, s_cOff = 0, s_cFar = 0, s_cOk = 0;
static float s_vLen[3] = {}; static float s_vDet = 1.0f;
static float s_cNear[4] = {0, 0, 0, 0}, s_cEye[3] = {0, 0, 0}, s_cClip[4] = {}, s_cClip2[4] = {}, s_cPP[4] = {}, s_cSgn[3] = {};

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

// (27/10 d) mundo -> vista. El log de a2417f4 mostro que la 4a columna de m_view (M[3], M[7], M[11]) es la posicion del OJO, no la
// traslacion de la vista: con "R w + M[.3]" la vista de un punto quedaba al lado del ojo y todos los jugadores salian "fuera de
// pantalla" o "detras". Ahora: v = R (w - ojo), con R las filas de m_view (ejes de la camara en el mundo, como en hooks.cpp) y el
// ojo de m_eyepos. Es la vista "de la proyeccion" (m_projection: w = z, z positiva hacia adelante).
static float Sgn(float x) { return x < 0.0f ? -1.0f : 1.0f; }
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
    if (!NamedCBRead("LightCB", L, &lf) || L.size() < 4 || !NamedCBRead("SceneCB", S, &sf) || S.size() < 88) return;
    if (NamedAge("LightCB") > 3) return;   // sin pasada del sol hace poco (menus, editor, noche)
    float Lv[3] = { L[0], L[1], L[2] }; if (!Norm(Lv)) return;
    const float* PV = &S[0];   // m_projectionView (mundo -> recorte)
    const float* eye = &S[80];  // m_eyepos
    const float* pp = &S[84];   // m_projectionParam
    // la vista de los shaders del sol (la de m_lightDir0 y la posicion rearmada con m_projectionParam) puede tener z negativa hacia
    // adelante (comentario 18/10 c de a748) y x / y con otro signo que la de la proyeccion: el signo de cada eje sale de projParam
    // (x_shader = ndc * pp.x * zC; x_proyeccion = ndc * |zC| / P00, P00 > 0). zC = pp.z / (prof - pp.w) con prof a mitad de rango
    const float zs = Sgn(pp[2] / ((fabsf(pp[3] - 0.5f) > 1e-4f) ? (0.5f - pp[3]) : 1e-4f));
    const float sx = Sgn(pp[0]) * zs * Sgn(S[16]), sy = Sgn(pp[1]) * zs * Sgn(S[21]);   // S[16] = P0, S[21] = P5
    Lv[0] *= sx; Lv[1] *= sy; Lv[2] *= zs;   // sol en la vista de la proyeccion
    // (27/10 e) la vista sale de m_projectionView y m_projection (V = P^-1 PV), no de m_view: el log de bc42ca3 mostro que R (w - ojo)
    // tampoco coincidia con lo que proyecta el juego (w del recorte 43 contra z de la vista -28). Con P de la forma
    // x' = P0 x + P2 z + P3, y' = P5 y + P6 z + P7, w' = z (verificado en el log: w = z exacto) cada fila de la vista es una
    // combinacion de filas de PV. VV[j][0..3]: fila j de la vista (mundo -> vista de la proyeccion)
    const float* P = &S[16];   // m_projection
    float VV[3][4];
    for (int i = 0; i < 4; i++) {
        VV[2][i] = PV[12 + i];
        VV[0][i] = (PV[i] - P[2] * PV[12 + i] - (i == 3 ? P[3] : 0.0f)) / (fabsf(P[0]) > 1e-6f ? P[0] : 1e-6f);
        VV[1][i] = (PV[4 + i] - P[6] * PV[12 + i] - (i == 3 ? P[7] : 0.0f)) / (fabsf(P[5]) > 1e-6f ? P[5] : 1e-6f);
    }
    s_vLen[0] = sqrtf(VV[0][0] * VV[0][0] + VV[0][1] * VV[0][1] + VV[0][2] * VV[0][2]);
    s_vLen[1] = sqrtf(VV[1][0] * VV[1][0] + VV[1][1] * VV[1][1] + VV[1][2] * VV[1][2]);
    s_vLen[2] = sqrtf(VV[2][0] * VV[2][0] + VV[2][1] * VV[2][1] + VV[2][2] * VV[2][2]);
    // (27/10 f) la vista sacada de PV puede ser un REFLEJO (determinante -1: P0 < 0 con x espejada): un producto cruz hecho en esa
    // vista sale con el signo cambiado respecto del mismo producto hecho en el mundo. El shader arma Up = cross(L, R) en vista:
    // se le avisa con el bit 8 de 317.w para que lo invierta (las manchas negras del partido de las 10:07 PC)
    const float det = VV[0][0] * (VV[1][1] * VV[2][2] - VV[1][2] * VV[2][1]) - VV[0][1] * (VV[1][0] * VV[2][2] - VV[1][2] * VV[2][0]) + VV[0][2] * (VV[1][0] * VV[2][1] - VV[1][1] * VV[2][0]);
    s_vDet = det;
    auto toV = [&](const float w[3], float v[3], bool point) { for (int j = 0; j < 3; j++) v[j] = VV[j][0] * w[0] + VV[j][1] * w[1] + VV[j][2] * w[2] + (point ? VV[j][3] : 0.0f); };
    // direccion hacia el sol en el mundo: la traspuesta de la rotacion de la vista
    float Lw[3]; for (int i = 0; i < 3; i++) Lw[i] = Lv[0] * VV[0][i] + Lv[1] * VV[1][i] + Lv[2] * VV[2][i];
    if (!Norm(Lw)) return;
    float up[3] = { 0, 1, 0 }; if (fabsf(Lw[1]) > 0.98f) { up[0] = 1; up[1] = 0; }
    float Rw[3]; Cross(up, Lw, Rw); if (!Norm(Rw)) return;
    float Uw[3]; Cross(Lw, Rw, Uw);
    // jugadores: los mas cercanos a la camara que caen en la pantalla
    static CharInfo ci[kCharSlots]; static bool alive[kCharSlots];
    CharSnapshot(ci, alive);
    struct Cand { float cw[3], cv[3], d; };
    std::vector<Cand> cand;
    int cAlive = 0, cNoPos = 0, cBehind = 0, cOff = 0, cFar = 0; float bestD = 1e9f;
    for (int sl = 0; sl < kCharSlots; sl++) {
        if (!alive[sl] || !ci[sl].node) continue;
        cAlive++;
        float pos[3]; if (!CharNodeWorldPos(ci[sl].node, pos)) { cNoPos++; continue; }
        Cand c; c.cw[0] = pos[0]; c.cw[1] = pos[1] + 1.45f; c.cw[2] = pos[2];   // centro de la caja: cabeza y hombros de un jugador parado
        toV(c.cw, c.cv, true);
        // en pantalla: con m_projectionView sobre la posicion en el mundo (no depende de como este armada m_view)
        float clip[4]; for (int j = 0; j < 4; j++) clip[j] = PV[4 * j] * c.cw[0] + PV[4 * j + 1] * c.cw[1] + PV[4 * j + 2] * c.cw[2] + PV[4 * j + 3];
        float clip2[4]; for (int j = 0; j < 4; j++) clip2[j] = P[4 * j] * c.cv[0] + P[4 * j + 1] * c.cv[1] + P[4 * j + 2] * c.cv[2] + P[4 * j + 3];
        c.d = sqrtf(Dot(c.cv, c.cv));
        if (c.d < bestD) { bestD = c.d; s_cNear[0] = c.cv[0]; s_cNear[1] = c.cv[1]; s_cNear[2] = c.cv[2]; s_cNear[3] = clip[3];
            for (int k = 0; k < 4; k++) { s_cClip[k] = clip[k]; s_cClip2[k] = clip2[k]; } }
        if (clip[3] <= 0.05f) { cBehind++; continue; }
        float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
        if (fabsf(nx) > 1.4f || fabsf(ny) > 1.4f) { cOff++; continue; }
        if (c.d > 60.0f) { cFar++; continue; }
        cand.push_back(c);
    }
    s_cAlive = cAlive; s_cNoPos = cNoPos; s_cBehind = cBehind; s_cOff = cOff; s_cFar = cFar; s_cOk = (int)cand.size();
    s_cEye[0] = eye[0]; s_cEye[1] = eye[1]; s_cEye[2] = eye[2]; for (int k = 0; k < 4; k++) s_cPP[k] = pp[k]; s_cSgn[0] = sx; s_cSgn[1] = sy; s_cSgn[2] = zs;
    std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    const float half = s_half;
    int n = (int)std::min<size_t>(cand.size(), kHsSlots);
    for (int i = 0; i < n; i++) {
        Cand c = cand[i];
        // (27/10 d) centro de la caja pegado a la grilla de texels del atlas (en el plano perpendicular al sol): la caja seguia
        // al jugador de a fracciones de texel y los bordes de la sombra (cuello de la camiseta sobre el cuello, menton) titilaban
        {
            const float tx = 2.0f * half / kHsSlotPx;
            float cr = Dot(c.cw, Rw), cu = Dot(c.cw, Uw), cl = Dot(c.cw, Lw);
            cr = floorf(cr / tx + 0.5f) * tx; cu = floorf(cu / tx + 0.5f) * tx;
            for (int k = 0; k < 3; k++) c.cw[k] = cr * Rw[k] + cu * Uw[k] + cl * Lw[k];
            ToView(M, c.cw, c.cv, true);
        }
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
        float* C = g_cfg.live[kHsRowCenter + i]; C[0] = c.cv[0]; C[1] = c.cv[1]; C[2] = c.cv[2]; C[3] = (s_reversed ? 1.0f : 0.0f) + (S[16] < 0 ? 2.0f : 0.0f) + (S[21] < 0 ? 4.0f : 0.0f) + (s_vDet < 0 ? 8.0f : 0.0f);   // bit 0 z invertida, 1 / 2 signo de P0 / P5, 3 vista reflejada
    }
    float Rv[3]; toV(Rw, Rv, false);
    I[0] = (float)n; I[1] = Lv[0]; I[2] = Lv[1]; I[3] = Lv[2];
    float* A = g_cfg.live[kHsRowAxis]; A[0] = Rv[0]; A[1] = Rv[1]; A[2] = Rv[2]; A[3] = half;
    s_count = n;
    double t = Now();
    if (t - s_logT > 10.0) { s_logT = t; int r = g_headShadowReplays.exchange(0);
        Log("sombra cabeza: %d cabezas, %d dibujos replicados en 10 s (z %s)", n, r, s_reversed ? "invertida" : "normal");
        Log("sombra cabeza (diag): personajes %d (sin posicion %d, detras %d, fuera de pantalla %d, lejos %d, validos %d); el mas cercano en vista (%.2f %.2f %.2f) w %.2f; ojo %.2f %.2f %.2f",
            s_cAlive, s_cNoPos, s_cBehind, s_cOff, s_cFar, s_cOk, s_cNear[0], s_cNear[1], s_cNear[2], s_cNear[3], s_cEye[0], s_cEye[1], s_cEye[2]);
        Log("sombra cabeza (diag): el mas cercano en recorte: con m_projectionView (%.2f %.2f %.2f %.2f), con m_projection x vista (%.2f %.2f %.2f %.2f); projParam (%.4f %.4f %.4f %.4f) -> signos de la vista del sol x %.0f y %.0f z %.0f; sol en vista %.3f %.3f %.3f",
            s_cClip[0], s_cClip[1], s_cClip[2], s_cClip[3], s_cClip2[0], s_cClip2[1], s_cClip2[2], s_cClip2[3], s_cPP[0], s_cPP[1], s_cPP[2], s_cPP[3], s_cSgn[0], s_cSgn[1], s_cSgn[2],
            g_cfg.live[kHsRowInfo][1], g_cfg.live[kHsRowInfo][2], g_cfg.live[kHsRowInfo][3]);
        Log("sombra cabeza (diag): largo de los ejes de la vista sacada de PV (deberian ser 1): %.3f %.3f %.3f; determinante %.2f", s_vLen[0], s_vLen[1], s_vLen[2], s_vDet);
        Log("sombra cabeza (diag): DSV del shadow map puesto %d veces (%d con targets de color, %d aceptadas); dibujos ahi: indexados %d, simples %d, instanciados %d, indexados instanciados %d; descartados por vista corrida %d, por otro DSV %d; vista vista (%d %d %dx%d), targets %d",
            s_dBindAny.exchange(0), s_dBindRT.exchange(0), s_dBindOk.exchange(0), s_dDraws[1].exchange(0), s_dDraws[0].exchange(0), s_dDraws[2].exchange(0), s_dDraws[3].exchange(0),
            s_dVpOff.exchange(0), s_dOtherDsv.exchange(0), s_dVpX.load(), s_dVpY.load(), s_dVpW.load(), s_dVpH.load(), s_dRtN.load()); }
}

int HeadShadowSlots() { return s_count; }
// (27/10 f) estado de rasterizado para la repeticion: el del juego pero SIN descarte de caras. La caja propia puede tener la
// orientacion opuesta a la matriz de sombras del juego y entonces el descarte dejaba solo las caras de atras (sombra de la cara
// opuesta: manchas). Con las dos caras se queda la mas cercana al sol, como corresponde. Cache por estado original.
static std::map<ID3D11RasterizerState*, ID3D11RasterizerState*> s_rsNoCull;
ID3D11RasterizerState* HeadShadowNoCullRS(ID3D11RasterizerState* orig) {
    auto it = s_rsNoCull.find(orig); if (it != s_rsNoCull.end()) return it->second;
    if (!g_dev.dev) return nullptr;
    D3D11_RASTERIZER_DESC rd{};
    if (orig) orig->GetDesc(&rd); else { rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_BACK; rd.DepthClipEnable = TRUE; }
    rd.CullMode = D3D11_CULL_NONE;
    ID3D11RasterizerState* rs = nullptr; g_dev.dev->CreateRasterizerState(&rd, &rs);
    if (orig) orig->AddRef();   // la clave se retiene para que el puntero no se reutilice con otro estado
    s_rsNoCull[orig] = rs; return rs;
}
ID3D11ShaderResourceView* HeadShadowSRV() { return s_count > 0 ? s_srv : nullptr; }
ID3D11DepthStencilView* HeadShadowDSV() { return s_dsv; }
ID3D11Buffer* HeadShadowCB(int i) { return (i >= 0 && i < kHsSlots) ? s_cb[i] : nullptr; }
void HeadShadowViewport(int i, D3D11_VIEWPORT& vp) { vp.TopLeftX = (float)((i % 2) * kHsSlotPx); vp.TopLeftY = (float)((i / 2) * kHsSlotPx); vp.Width = vp.Height = (float)kHsSlotPx; vp.MinDepth = 0; vp.MaxDepth = 1; }

// el DSV es el shadow map del sol del juego? (se compara el recurso con el SRV que las reglas guardan como "ShadowMap")
static bool IsGameShadowRes(ID3D11DepthStencilView* v) {
    if (!v) return false;
    int f = PresentNo();
    if (s_shadowResFrame != f) { s_shadowResFrame = f; s_shadowRes = NamedSRVResource("ShadowMap"); }
    if (!s_shadowRes) return false;
    ID3D11Resource* r = nullptr; v->GetResource(&r); if (!r) return false;
    bool same = (r == s_shadowRes); r->Release();
    return same;
}
bool HeadShadowIsGameShadow(ID3D11DepthStencilView* v) { return s_count > 0 && IsGameShadowRes(v); }
// OMSetRenderTargets: el DSV es el shadow map del sol? se acepta aunque vengan targets de color (nulos o no: algunos motores
// ponen un target "tonto" en la pasada de sombras)
ID3D11DepthStencilView* HeadShadowBind(UINT n, ID3D11RenderTargetView* const* rt, ID3D11DepthStencilView* dsv) {
    if (!dsv || !IsGameShadowRes(dsv)) return nullptr;
    s_dBindAny.fetch_add(1, std::memory_order_relaxed);
    int nr = 0; if (rt) for (UINT i = 0; i < n && i < 8; i++) if (rt[i]) nr++;
    if (nr) s_dBindRT.fetch_add(1, std::memory_order_relaxed);
    s_dRtN.store(nr);
    if (s_count <= 0) return nullptr;
    s_dBindOk.fetch_add(1, std::memory_order_relaxed);
    return dsv;
}
// el juego limpia su shadow map: se limpia el atlas con el mismo valor (y se aprende si la profundidad es invertida)
void HeadShadowOnClear(ID3D11DeviceContext* c, ID3D11DepthStencilView* v, float depth) {
    if (!IsGameShadowRes(v) || !s_dsv) return;
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
bool HeadShadowIsNearCascade(ID3D11DeviceContext* c, ID3D11DepthStencilView* dsv, int kind) {
    if (kind >= 0 && kind < 4) s_dDraws[kind].fetch_add(1, std::memory_order_relaxed);
    D3D11_VIEWPORT vp; UINT n = 1; c->RSGetViewports(&n, &vp);
    if (n >= 1) { s_dVpX.store((int)vp.TopLeftX); s_dVpY.store((int)vp.TopLeftY); s_dVpW.store((int)vp.Width); s_dVpH.store((int)vp.Height); }
    if (n < 1 || vp.TopLeftX > 0.5f || vp.TopLeftY > 0.5f) { s_dVpOff.fetch_add(1, std::memory_order_relaxed); return false; }
    ID3D11DepthStencilView* exp = nullptr;
    if (s_cascadeDSV.compare_exchange_strong(exp, dsv)) return true;
    if (exp != dsv) { s_dOtherDsv.fetch_add(1, std::memory_order_relaxed); return false; }
    return true;
}
void HeadShadowRelease() {
    for (int i = 0; i < kHsSlots; i++) { if (s_cb[i]) s_cb[i]->Release(); s_cb[i] = nullptr; }
    if (s_srv) s_srv->Release(); if (s_dsv) s_dsv->Release(); if (s_tex) s_tex->Release(); s_srv = nullptr; s_dsv = nullptr; s_tex = nullptr;
}

} // namespace fx
