// PES21 FX - aro y punto blanco del musculo elegido en el menu Fisico, como capa de interfaz (24/10 c).
// Antes se pintaban sobre la piel (se veian como parte de la textura). Ahora:
//  1) mientras late el resaltado (x174), cada dibujo de la piel del cuerpo con nuestro VS (aa7d1833 + 1c02a92a) usa la
//     variante pes_skin_hl_ps.hlsl, que suma en un buffer (u5, despues de los 5 render targets del G-buffer) la posicion de
//     los pixeles visibles cerca de cada marca del musculo (el VS dice la cercania y el lado en o4.w / o5.w);
//  2) cuando el menu dibuja la imagen del jugador (PS 765e1433, el ultimo dibujo de la pasada del menu) se repite ese
//     mismo dibujo con pes_ring_ps.hlsl, que pone el aro y el punto en el promedio de cada marca, y el buffer se limpia.
// El viewport de la escena (para pasar de pixeles de la escena a la imagen del jugador) va en x193..w193.
#include "core.h"
#include <atomic>
#include <cmath>
#include <cstdio>

namespace fx {

static const uint64_t kSkinPS = 0xaa7d1833f6a11481ULL, kSkinVS = 0x1c02a92ab4ae3362ULL, kViewPS = 0x765e1433238f156cULL;
static std::mutex s_mx;
static bool s_tried = false;
static ID3D11PixelShader *s_hlPS = nullptr, *s_ringPS = nullptr;
static ID3D11Buffer* s_acc = nullptr; static ID3D11UnorderedAccessView* s_accUAV = nullptr; static ID3D11ShaderResourceView* s_accSRV = nullptr;
static ID3D11BlendState* s_bs = nullptr; static ID3D11DepthStencilState* s_dss = nullptr;
static std::atomic<int> s_lastSkinFrame{-1}, s_lastRingFrame{-100};
static std::atomic<int> s_skinBinds{0}, s_rings{0};

static bool Ensure() {
    if (s_tried) return s_hlPS && s_ringPS && s_acc;
    s_tried = true;
    ID3D11Device* d = g_dev.dev; if (!d) return false;
    if (ID3DBlob* b = CompileNamedShader(L"pes_skin_hl_ps.hlsl", "ps_5_0")) { d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_hlPS); b->Release(); }
    if (ID3DBlob* b = CompileNamedShader(L"pes_ring_ps.hlsl", "ps_5_0")) { d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_ringPS); b->Release(); }
    if (!s_acc) {
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 64; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        UINT zero[16] = {}; D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = zero;
        if (SUCCEEDED(d->CreateBuffer(&bd, &sd, &s_acc))) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = 16; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            d->CreateUnorderedAccessView(s_acc, &ud, &s_accUAV);
            D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = DXGI_FORMAT_R32_TYPELESS; sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            sv.BufferEx.NumElements = 16; sv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
            d->CreateShaderResourceView(s_acc, &sv, &s_accSRV);
        }
        D3D11_BLEND_DESC bl{}; auto& r = bl.RenderTarget[0];
        r.BlendEnable = TRUE; r.SrcBlend = D3D11_BLEND_SRC_ALPHA; r.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; r.BlendOp = D3D11_BLEND_OP_ADD;
        r.SrcBlendAlpha = D3D11_BLEND_ZERO; r.DestBlendAlpha = D3D11_BLEND_ONE; r.BlendOpAlpha = D3D11_BLEND_OP_ADD; r.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        d->CreateBlendState(&bl, &s_bs);
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = FALSE; dd.StencilEnable = FALSE; d->CreateDepthStencilState(&dd, &s_dss);
    }
    bool ok = s_hlPS && s_ringPS && s_acc && s_accUAV && s_accSRV && s_bs && s_dss;
    Log("aro del musculo: %s", ok ? "listo (capa de interfaz)" : "NO se pudo preparar (ver errores de shaders)");
    return ok;
}
void RingReloadShaders() {
    std::lock_guard<std::mutex> lk(s_mx);
    if (s_hlPS) { s_hlPS->Release(); s_hlPS = nullptr; }
    if (s_ringPS) { s_ringPS->Release(); s_ringPS = nullptr; }
    s_tried = false;
}

static bool Active() { return g_cfg.live[174][0] > 0.001f && g_cfg.live[174][3] > 0.5f && g_cfg.live[186][1] > 0.001f; }

bool RingBeforeDraw(ID3D11DeviceContext* c, uint64_t ps, uint64_t vs, RingScope& r) {
    if (ps != kSkinPS && ps != kViewPS) return false;
    if (!Active() || !g_dev.dev) return false;
    std::lock_guard<std::mutex> lk(s_mx);
    if (!Ensure()) return false;
    if (ps == kViewPS) {   // la imagen del jugador en el menu: el aro va encima, despues de este dibujo
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
                if (n < 4) { n++; Log("aro del musculo: imagen del jugador en el menu %ux%u, escena %.0fx%.0f -> %s", td.Width, td.Height, vw, vh, ok ? "aro encima" : "NO coincide (sin aro)"); }
            }
            t8->Release();
        }
        r.overlay = ok;
        return ok;
    }
    if (vs != kSkinVS) return false;
    // piel del cuerpo: variante que junta las marcas en u5 (despues de los render targets del G-buffer)
    ID3D11RenderTargetView* rts[8] = {}; ID3D11DepthStencilView* dsv = nullptr; c->OMGetRenderTargets(8, rts, &dsv);
    UINT nrt = 0; for (UINT i = 0; i < 8; i++) if (rts[i]) nrt = i + 1;
    for (auto* v : rts) if (v) v->Release(); if (dsv) dsv->Release();
    if (nrt > 5) return false;
    const int f = PresentNo();
    if (s_lastSkinFrame.exchange(f) != f && f - s_lastRingFrame.load() > 2) {   // nadie las leyo: empezar de cero
        const UINT z[4] = { 0, 0, 0, 0 }; c->ClearUnorderedAccessViewUint(s_accUAV, z);
    }
    UINT nvp = 1; D3D11_VIEWPORT vp{}; c->RSGetViewports(&nvp, &vp);
    if (nvp >= 1 && vp.Width > 1.0f) { g_cfg.live[193][0] = vp.TopLeftX; g_cfg.live[193][1] = vp.TopLeftY; g_cfg.live[193][2] = vp.Width; g_cfg.live[193][3] = vp.Height; }
    c->PSGetShader(&r.prevPS, nullptr, nullptr);
    c->PSSetShader(s_hlPS, nullptr, 0);
    c->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 5, 1, &s_accUAV, nullptr);
    r.on = true;
    s_skinBinds++;
    return true;
}

void RingAfterDraw(ID3D11DeviceContext* c, RingScope& r) {
    if (!r.on) return;
    ID3D11UnorderedAccessView* nu = nullptr;
    c->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 5, 1, &nu, nullptr);
    c->PSSetShader(r.prevPS, nullptr, 0); if (r.prevPS) r.prevPS->Release();
    r.prevPS = nullptr; r.on = false;
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
    c->PSSetShaderResources(20, 1, &s_accSRV);
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->PSSetShaderResources(120, 1, &prm);
}
void RingOverlayEnd(ID3D11DeviceContext* c, RingScope& r) {
    c->PSSetShader(r.oPS, nullptr, 0); if (r.oPS) r.oPS->Release();
    c->OMSetBlendState(r.oBS, r.oBF, r.oSM); if (r.oBS) r.oBS->Release();
    c->OMSetDepthStencilState(r.oDSS, r.oRef); if (r.oDSS) r.oDSS->Release();
    c->PSSetShaderResources(20, 1, &r.oT20); if (r.oT20) r.oT20->Release();
    c->PSSetShaderResources(120, 1, &r.oT120); if (r.oT120) r.oT120->Release();
    const UINT z[4] = { 0, 0, 0, 0 }; c->ClearUnorderedAccessViewUint(s_accUAV, z);   // el proximo cuadro junta de nuevo
    s_lastRingFrame = PresentNo();
    s_rings++;
    r = RingScope();
}

} // namespace fx
