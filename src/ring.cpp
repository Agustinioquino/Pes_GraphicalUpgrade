// PES21 FX - aro y punto blanco del musculo elegido en el menu Fisico, como capa de interfaz (24/10 c).
// Antes se pintaban sobre la piel (se veian como parte de la textura). Ahora:
//  1) mientras late el resaltado (x174), cada dibujo de la piel del cuerpo con nuestro VS (aa7d1833 + 1c02a92a) usa la
//     variante pes_skin_hl_ps.hlsl, que suma en un buffer (u5, despues de los 5 render targets del G-buffer) la posicion de
//     los pixeles visibles cerca de cada marca del musculo (el VS dice la cercania y el lado en o4.w / o5.w);
//  2) cuando el menu dibuja la imagen del jugador (PS 55a003da con la textura de la escena, del tamano del viewport de la
//     piel) se repite ese mismo dibujo con pes_ring_ps.hlsl, que pone el aro y el punto en el promedio de cada marca, y el
//     buffer se limpia.
// El viewport de la escena (para pasar de pixeles de la escena a la imagen del jugador) va en x193..w193.
#include "core.h"
#include <dxgi.h>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace fx {

// (24/10 d) la imagen del jugador la pone el PS comun del menu (55a003da) como segundo dibujo de la pasada, con la textura
// de la escena (1238x1080, el tamano del viewport de la piel); 765e1433 era otra cosa (textura de 128x128)
static const uint64_t kSkinPS = 0xaa7d1833f6a11481ULL, kSkinVS = 0x1c02a92ab4ae3362ULL, kViewPS = 0x55a003da3f888a81ULL;
// (24/10 e) medicion del color de la piel (skintone.cpp, "cuerpo como la cara"): las variantes de la piel y de la cara
// (5ce3ed87 / ce188ae1) acumulan en el mismo buffer (lugares 4..6); se copia a un buffer de lectura y se lee 2 cuadros despues
static const uint64_t kFacePS = 0x5ce3ed8702271bc1ULL, kFace2PS = 0xce188ae110354aeaULL;
static ID3D11PixelShader *s_faceHlPS = nullptr, *s_face2HlPS = nullptr;
static ID3D11Buffer* s_read[3] = {}; static int s_readFrame[3] = { -1, -1, -1 };
static std::mutex s_mx;
static bool s_tried = false;
static ID3D11PixelShader *s_hlPS = nullptr, *s_ringPS = nullptr;
// (24/10 d) dos buffers alternados por cuadro: la piel del cuadro f anota en [f & 1] y el aro del cuadro f lee lo que
// anoto el cuadro f - 1 (completo); antes se leia a medio anotar y se borraba al usarse, y el aro parpadeaba
static ID3D11Buffer* s_acc[2] = {}; static ID3D11UnorderedAccessView* s_accUAV[2] = {}; static ID3D11ShaderResourceView* s_accSRV[2] = {};
static ID3D11BlendState* s_bs = nullptr; static ID3D11DepthStencilState* s_dss = nullptr;
// (24/10 e) el aro se dibuja en Present sobre la imagen final (pes_ring_screen_ps.hlsl, triangulo de pantalla completa) con
// las marcas del mismo cuadro (ya todos los dibujos de la piel pasaron): sin atraso al mover la camara y sin parpadeo. El
// dibujo repetido sobre la imagen del jugador en el menu (RingOverlay*) queda apagado salvo x193 < 0 (prueba)
static ID3D11VertexShader* s_fsVS = nullptr; static ID3D11PixelShader* s_screenPS = nullptr; static ID3D11RasterizerState* s_rs = nullptr;
static ID3D11RenderTargetView* s_bbRTV = nullptr; static ID3D11Texture2D* s_bbTex = nullptr;
static std::atomic<int> s_lastSkinFrame{-1}, s_lastRingFrame{-100};
static std::atomic<int> s_skinBinds{0}, s_rings{0};

static bool Ensure() {
    if (s_tried) return s_hlPS && s_ringPS && s_acc[0] && s_acc[1];
    s_tried = true;
    ID3D11Device* d = g_dev.dev; if (!d) return false;
    if (ID3DBlob* b = CompileNamedShader(L"pes_skin_hl_ps.hlsl", "ps_5_0")) { d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_hlPS); b->Release(); }
    if (ID3DBlob* b = CompileNamedShader(L"pes_face_hl_ps.hlsl", "ps_5_0")) { d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_faceHlPS); b->Release(); }
    if (ID3DBlob* b = CompileNamedShader(L"pes_face2_hl_ps.hlsl", "ps_5_0")) { d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_face2HlPS); b->Release(); }
    if (ID3DBlob* b = CompileNamedShader(L"pes_ring_ps.hlsl", "ps_5_0")) { d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_ringPS); b->Release(); }
    if (ID3DBlob* b = CompileNamedShader(L"pes_ring_screen_ps.hlsl", "ps_5_0")) { d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_screenPS); b->Release(); }
    if (ID3DBlob* b = CompileNamedShader(L"pes_fs_vs.hlsl", "vs_5_0")) { d->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_fsVS); b->Release(); }
    if (!s_rs) { D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; d->CreateRasterizerState(&rd, &s_rs); }
    if (!s_acc[0]) {
        for (int i = 0; i < 2; i++) {
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 128; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
            bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            UINT zero[32] = {}; D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = zero;
            if (SUCCEEDED(d->CreateBuffer(&bd, &sd, &s_acc[i]))) {
                D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
                ud.Buffer.NumElements = 32; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
                d->CreateUnorderedAccessView(s_acc[i], &ud, &s_accUAV[i]);
                D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = DXGI_FORMAT_R32_TYPELESS; sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
                sv.BufferEx.NumElements = 32; sv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
                d->CreateShaderResourceView(s_acc[i], &sv, &s_accSRV[i]);
            }
        }
        for (int i = 0; i < 3; i++) { D3D11_BUFFER_DESC rd{}; rd.ByteWidth = 128; rd.Usage = D3D11_USAGE_STAGING; rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d->CreateBuffer(&rd, nullptr, &s_read[i]); }
        D3D11_BLEND_DESC bl{}; auto& r = bl.RenderTarget[0];
        r.BlendEnable = TRUE; r.SrcBlend = D3D11_BLEND_SRC_ALPHA; r.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; r.BlendOp = D3D11_BLEND_OP_ADD;
        r.SrcBlendAlpha = D3D11_BLEND_ZERO; r.DestBlendAlpha = D3D11_BLEND_ONE; r.BlendOpAlpha = D3D11_BLEND_OP_ADD; r.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        d->CreateBlendState(&bl, &s_bs);
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = FALSE; dd.StencilEnable = FALSE; d->CreateDepthStencilState(&dd, &s_dss);
    }
    bool ok = s_hlPS && s_faceHlPS && s_face2HlPS && s_ringPS && s_screenPS && s_fsVS && s_rs && s_acc[0] && s_accUAV[0] && s_accSRV[0] && s_acc[1] && s_accUAV[1] && s_accSRV[1] && s_bs && s_dss;
    Log("aro del musculo: %s", ok ? "listo (capa de interfaz)" : "NO se pudo preparar (ver errores de shaders)");
    return ok;
}
void RingReloadShaders() {
    std::lock_guard<std::mutex> lk(s_mx);
    if (s_hlPS) { s_hlPS->Release(); s_hlPS = nullptr; }
    if (s_faceHlPS) { s_faceHlPS->Release(); s_faceHlPS = nullptr; }
    if (s_face2HlPS) { s_face2HlPS->Release(); s_face2HlPS = nullptr; }
    if (s_ringPS) { s_ringPS->Release(); s_ringPS = nullptr; }
    if (s_screenPS) { s_screenPS->Release(); s_screenPS = nullptr; }
    if (s_fsVS) { s_fsVS->Release(); s_fsVS = nullptr; }
    s_tried = false;
}

static bool Active() { return g_cfg.live[174][0] > 0.001f && g_cfg.live[174][3] > 0.5f && g_cfg.live[186][1] > 0.001f; }

bool RingBeforeDraw(ID3D11DeviceContext* c, uint64_t ps, uint64_t vs, RingScope& r) {
    const bool face = ps == kFacePS || ps == kFace2PS;
    if (ps != kSkinPS && ps != kViewPS && !face) return false;
    const bool meas = g_skinMeasure.load(std::memory_order_relaxed) && MusclePhysiqueActive();
    if (!g_dev.dev) return false;
    if (face) { if (!meas) return false; }
    else if (!Active() && !(meas && ps == kSkinPS)) return false;
    std::lock_guard<std::mutex> lk(s_mx);
    if (!Ensure()) return false;
    if (ps == kViewPS) {   // la imagen del jugador en el menu: el aro va encima, despues de este dibujo (apagado: va en Present)
        if (g_cfg.live[186][3] < 0.5f) return false;
        // la imagen tiene que ser la de la escena (mismo tamano que el viewport de la piel)
        ID3D11ShaderResourceView* t8 = nullptr; c->PSGetShaderResources(8, 1, &t8);
        bool ok = false;
        if (t8) {
            ID3D11Resource* res = nullptr; t8->GetResource(&res);
            ID3D11Texture2D* tx = nullptr; if (res) { res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tx); res->Release(); }
            if (tx) {
                D3D11_TEXTURE2D_DESC td; tx->GetDesc(&td); tx->Release();
                const float vw = g_cfg.live[193][2], vh = g_cfg.live[193][3];
                ok = vw > 1.0f && fabsf((float)td.Width - vw) <= 4.0f && fabsf((float)td.Height - vh) <= 4.0f;
                static std::atomic<int> n{0};
                if (ok && n < 4) { n++; Log("aro del musculo: imagen del jugador en el menu %ux%u = escena %.0fx%.0f -> aro encima", td.Width, td.Height, vw, vh); }
            }
            t8->Release();
        }
        r.overlay = ok;
        return ok;
    }
    if (!face && vs != kSkinVS) return false;
    // piel del cuerpo (o cara, midiendo): variante que junta las marcas / colores en u5 (despues de los render targets del G-buffer)
    ID3D11RenderTargetView* rts[8] = {}; ID3D11DepthStencilView* dsv = nullptr; c->OMGetRenderTargets(8, rts, &dsv);
    UINT nrt = 0; for (UINT i = 0; i < 8; i++) if (rts[i]) nrt = i + 1;
    for (auto* v : rts) if (v) v->Release(); if (dsv) dsv->Release();
    if (nrt > 5) return false;
    const int f = PresentNo(); const int w = f & 1;
    if (s_lastSkinFrame.exchange(f) != f) {   // primer dibujo de piel del cuadro: el buffer de este cuadro empieza de cero
        const UINT z[4] = { 0, 0, 0, 0 }; c->ClearUnorderedAccessViewUint(s_accUAV[w], z);
    }
    UINT nvp = 1; D3D11_VIEWPORT vp{}; c->RSGetViewports(&nvp, &vp);
    if (!face && nvp >= 1 && vp.Width > 1.0f) { g_cfg.live[193][0] = vp.TopLeftX; g_cfg.live[193][1] = vp.TopLeftY; g_cfg.live[193][2] = vp.Width; g_cfg.live[193][3] = vp.Height; }
    c->PSGetShader(&r.prevPS, nullptr, nullptr);
    c->PSSetShader(ps == kFacePS ? s_faceHlPS : ps == kFace2PS ? s_face2HlPS : s_hlPS, nullptr, 0);
    if (face) { r.face = true; r.uav = s_accUAV[w]; }   // la cara va en u7 (u5 / u6 son las semillas de las cejas): se enlaza al final (RingFaceBind)
    else c->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 5, 1, &s_accUAV[w], nullptr);
    r.on = true;
    s_skinBinds++;
    return true;
}

// al final de BeforeDraw (despues de que la regla de las cejas puso sus UAV en u5 / u6): u7 = el buffer de medicion
void RingFaceBind(ID3D11DeviceContext* c, RingScope& r) {
    if (!r.on || !r.face || !r.uav) return;
    ID3D11UnorderedAccessView* u[3] = {};
    c->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 5, 3, u);
    if (u[2]) u[2]->Release();
    u[2] = r.uav;
    c->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 5, 3, u, nullptr);
    if (u[0]) u[0]->Release(); if (u[1]) u[1]->Release();
}
void RingAfterDraw(ID3D11DeviceContext* c, RingScope& r) {
    if (!r.on) return;
    ID3D11UnorderedAccessView* nu[3] = {};
    if (r.face) c->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 5, 3, nu, nullptr);
    else c->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 5, 1, nu, nullptr);
    c->PSSetShader(r.prevPS, nullptr, 0); if (r.prevPS) r.prevPS->Release();
    r.prevPS = nullptr; r.on = false; r.face = false; r.uav = nullptr;
}

void RingOverlayBegin(ID3D11DeviceContext* c, RingScope& r) {
    c->PSGetShader(&r.oPS, nullptr, nullptr);
    c->OMGetBlendState(&r.oBS, r.oBF, &r.oSM);
    c->OMGetDepthStencilState(&r.oDSS, &r.oRef);
    c->PSGetShaderResources(20, 1, &r.oT20);
    c->PSGetShaderResources(120, 1, &r.oT120);
    c->PSSetShader(s_ringPS, nullptr, 0);
    float bf[4] = {}; c->OMSetBlendState(s_bs, bf, 0xffffffff);
    c->OMSetDepthStencilState(s_dss, 0);
    c->PSSetShaderResources(20, 1, &s_accSRV[(PresentNo() + 1) & 1]);   // lo que anoto el cuadro anterior
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->PSSetShaderResources(120, 1, &prm);
}
void RingOverlayEnd(ID3D11DeviceContext* c, RingScope& r) {
    c->PSSetShader(r.oPS, nullptr, 0); if (r.oPS) r.oPS->Release();
    c->OMSetBlendState(r.oBS, r.oBF, r.oSM); if (r.oBS) r.oBS->Release();
    c->OMSetDepthStencilState(r.oDSS, r.oRef); if (r.oDSS) r.oDSS->Release();
    c->PSSetShaderResources(20, 1, &r.oT20); if (r.oT20) r.oT20->Release();
    c->PSSetShaderResources(120, 1, &r.oT120); if (r.oT120) r.oT120->Release();
    s_lastRingFrame = PresentNo();
    s_rings++;
    r = RingScope();
}

// en Present: el aro sobre la imagen final, en el rectangulo de pantalla donde quedo el viewport de la escena (x193..w193;
// en el menu Fisico la escena se dibuja en el mismo lugar de la pantalla donde se ve)
void RingPresent(IDXGISwapChain* sc) {
    if (!g_dev.dev || !g_dev.imm || !sc) return;
    const int f = PresentNo() - 1;   // el contador ya subio al entrar a Present: los dibujos de este cuadro llevan el anterior
    if (s_lastSkinFrame.load() != f) return;   // este cuadro no dibujo piel con la variante
    std::lock_guard<std::mutex> lk(s_mx);
    if (!Ensure()) return;
    ID3D11DeviceContext* c = g_dev.imm;
    if (g_skinMeasure.load()) {   // (24/10 e) medicion: copiar lo de este cuadro y leer lo de hace 2
        const int w = f % 3;
        if (s_read[w]) { c->CopyResource(s_read[w], s_acc[f & 1]); s_readFrame[w] = f; }
        const int rd = (f + 1) % 3;
        if (s_read[rd] && s_readFrame[rd] >= 0 && s_readFrame[rd] <= f - 2) {
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(c->Map(s_read[rd], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
                const uint32_t* q = (const uint32_t*)m.pData;
                SkinMeasureReport(q + 16, q + 20, q[24], q[25], q[27]);
                c->Unmap(s_read[rd], 0); s_readFrame[rd] = -1;
            }
        }
    }
    if (!Active()) return;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) return;
    if (bb != s_bbTex || !s_bbRTV) {
        if (s_bbRTV) { s_bbRTV->Release(); s_bbRTV = nullptr; }
        s_bbTex = bb;
        if (FAILED(g_dev.dev->CreateRenderTargetView(bb, nullptr, &s_bbRTV))) { bb->Release(); return; }
    }
    bb->Release();
    // estado que se toca (se devuelve al final)
    ID3D11RenderTargetView* oRT[8] = {}; ID3D11DepthStencilView* oDS = nullptr; c->OMGetRenderTargets(8, oRT, &oDS);
    UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; D3D11_VIEWPORT ovp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; c->RSGetViewports(&nvp, ovp);
    ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr;
    c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL); D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS);
    ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDSS, &oRef);
    ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0; c->OMGetBlendState(&oBS, oBF, &oSM);
    ID3D11ShaderResourceView* oT20 = nullptr; c->PSGetShaderResources(20, 1, &oT20);
    ID3D11ShaderResourceView* oT120 = nullptr; c->PSGetShaderResources(120, 1, &oT120);

    // donde se ve la escena en la pantalla: si el viewport arranca en (0, 0) la escena es una textura aparte (1238x1080) que el
    // menu pega pegada al borde derecho e inferior; si no, es el mismo rectangulo
    { float* V = g_cfg.live[193]; float* S = g_cfg.live[185];
      if (V[0] < 0.5f && V[1] < 0.5f) { S[0] = (float)g_bbW - V[2]; S[1] = (float)g_bbH - V[3]; } else { S[0] = V[0]; S[1] = V[1]; }
      S[2] = V[2]; S[3] = V[3]; }
    c->OMSetRenderTargets(1, &s_bbRTV, nullptr);
    D3D11_VIEWPORT vp{ 0, 0, (float)g_bbW, (float)g_bbH, 0, 1 }; c->RSSetViewports(1, &vp);
    c->VSSetShader(s_fsVS, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0); c->PSSetShader(s_screenPS, nullptr, 0);
    c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->RSSetState(s_rs); c->OMSetDepthStencilState(s_dss, 0); float bf[4] = {}; c->OMSetBlendState(s_bs, bf, 0xffffffff);
    c->PSSetShaderResources(20, 1, &s_accSRV[f & 1]);   // lo que anoto ESTE cuadro
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->PSSetShaderResources(120, 1, &prm);
    c->Draw(3, 0);

    c->OMSetRenderTargets(8, oRT, oDS); for (auto* r : oRT) if (r) r->Release(); if (oDS) oDS->Release();
    c->RSSetViewports(nvp, ovp);
    c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0);
    if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release(); c->IASetPrimitiveTopology(oTopo);
    c->RSSetState(oRS); if (oRS) oRS->Release();
    c->OMSetDepthStencilState(oDSS, oRef); if (oDSS) oDSS->Release();
    c->OMSetBlendState(oBS, oBF, oSM); if (oBS) oBS->Release();
    c->PSSetShaderResources(20, 1, &oT20); if (oT20) oT20->Release();
    c->PSSetShaderResources(120, 1, &oT120); if (oT120) oT120->Release();
    s_rings++;
    static int n = 0;
    if (n++ < 2) Log("aro del musculo: dibujado en Present sobre la imagen final (escena en %.0f, %.0f, %.0fx%.0f; pantalla %ux%u)", g_cfg.live[193][0], g_cfg.live[193][1], g_cfg.live[193][2], g_cfg.live[193][3], g_bbW, g_bbH);
}

} // namespace fx
