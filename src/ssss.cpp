// PES21 FX - dispersion subsuperficial en pantalla separable (19/10): justo antes del dibujo de composicion (619b/f807)
// se toma la irradiancia difusa que ese shader va a leer (t9), se la pasa por un blur horizontal y uno vertical (perfil de
// piel de d'Eon, ancho por profundidad, solo piel) y se le da al dibujo la version difundida en el mismo slot.
#include "core.h"
#include <d3d11.h>

namespace fx {

static ID3D11VertexShader* s_vs = nullptr; static ID3D11PixelShader* s_psH = nullptr; static ID3D11PixelShader* s_psV = nullptr; static bool s_tried = false;
static ID3D11Texture2D* s_tex[2] = {}; static ID3D11RenderTargetView* s_rtv[2] = {}; static ID3D11ShaderResourceView* s_srv[2] = {};
static UINT s_w = 0, s_h = 0; static DXGI_FORMAT s_fmt = DXGI_FORMAT_UNKNOWN;
static ID3D11SamplerState* s_sampP = nullptr; static ID3D11SamplerState* s_sampL = nullptr;
static ID3D11RasterizerState* s_rs = nullptr; static ID3D11DepthStencilState* s_dss = nullptr; static ID3D11BlendState* s_bs = nullptr;
std::atomic<int> g_ssssDraws{0};

static void EnsureShaders() {
    if (s_tried || !g_dev.dev) return; s_tried = true;
    ID3DBlob* vb = CompileNamedShader(L"pes_fs_vs.hlsl", "vs_5_0");
    ID3DBlob* hb = CompileNamedShader(L"pes_ssss_h_ps.hlsl", "ps_5_0");
    ID3DBlob* pb = CompileNamedShader(L"pes_ssss_v_ps.hlsl", "ps_5_0");
    if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_vs); vb->Release(); }
    if (hb) { g_dev.dev->CreatePixelShader(hb->GetBufferPointer(), hb->GetBufferSize(), nullptr, &s_psH); hb->Release(); }
    if (pb) { g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_psV); pb->Release(); }
    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    g_dev.dev->CreateSamplerState(&sd, &s_sampP); sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; g_dev.dev->CreateSamplerState(&sd, &s_sampL);
    D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; g_dev.dev->CreateRasterizerState(&rd, &s_rs);
    D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = FALSE; dd.StencilEnable = FALSE; g_dev.dev->CreateDepthStencilState(&dd, &s_dss);
    D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; g_dev.dev->CreateBlendState(&bd, &s_bs);
    Log("ssss: shaders %s", (s_vs && s_psH && s_psV) ? "listos" : "NO compilaron (ver errores)");
}
void SSSSReloadShaders() {
    if (s_vs) { s_vs->Release(); s_vs = nullptr; } if (s_psH) { s_psH->Release(); s_psH = nullptr; } if (s_psV) { s_psV->Release(); s_psV = nullptr; }
    s_tried = false;
}
static bool EnsureTargets(UINT w, UINT h, DXGI_FORMAT fmt) {
    if (s_tex[0] && s_w == w && s_h == h && s_fmt == fmt) return true;
    for (int i = 0; i < 2; i++) { if (s_srv[i]) s_srv[i]->Release(); if (s_rtv[i]) s_rtv[i]->Release(); if (s_tex[i]) s_tex[i]->Release(); s_srv[i] = nullptr; s_rtv[i] = nullptr; s_tex[i] = nullptr; }
    D3D11_TEXTURE2D_DESC td{}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int i = 0; i < 2; i++) {
        if (FAILED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_tex[i]))) return false;
        if (FAILED(g_dev.dev->CreateRenderTargetView(s_tex[i], nullptr, &s_rtv[i]))) return false;
        if (FAILED(g_dev.dev->CreateShaderResourceView(s_tex[i], nullptr, &s_srv[i]))) return false;
    }
    s_w = w; s_h = h; s_fmt = fmt;
    Log("ssss: targets %ux%u fmt %d", w, h, (int)fmt);
    return true;
}

// Devuelve el SRV difundido para t9 (o null si no se pudo). Se llama en BeforeDraw del shader de composicion.
ID3D11ShaderResourceView* SSSSBeforeComposition(ID3D11DeviceContext* c) {
    if (!g_dev.dev || g_cfg.live[95][3] < 0.5f) return nullptr;
    EnsureShaders(); if (!s_vs || !s_psH || !s_psV) return nullptr;
    ID3D11ShaderResourceView* in[12] = {}; c->PSGetShaderResources(0, 12, in);
    auto rel = [&]() { for (auto* v : in) if (v) v->Release(); };
    ID3D11ShaderResourceView* light = in[9]; ID3D11ShaderResourceView* depth = in[11]; ID3D11ShaderResourceView* mat = in[1]; ID3D11ShaderResourceView* nrm = in[0];
    if (!light || !depth || !mat || !nrm) { rel(); return nullptr; }
    ID3D11Resource* res = nullptr; light->GetResource(&res); if (!res) { rel(); return nullptr; }
    D3D11_TEXTURE2D_DESC td; ((ID3D11Texture2D*)res)->GetDesc(&td); res->Release();
    D3D11_SHADER_RESOURCE_VIEW_DESC vd; light->GetDesc(&vd);
    DXGI_FORMAT fmt = vd.Format == DXGI_FORMAT_UNKNOWN ? td.Format : vd.Format;
    if (td.SampleDesc.Count != 1 || !EnsureTargets(td.Width, td.Height, fmt)) { rel(); return nullptr; }
    // guardar estado
    ID3D11RenderTargetView* oRT[8] = {}; ID3D11DepthStencilView* oDS = nullptr; c->OMGetRenderTargets(8, oRT, &oDS);
    UINT nvp = 8; D3D11_VIEWPORT ovp[8]; c->RSGetViewports(&nvp, ovp);
    ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr; ID3D11HullShader* oHS = nullptr; ID3D11DomainShader* oDSh = nullptr;
    c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr); c->HSGetShader(&oHS, nullptr, nullptr); c->DSGetShader(&oDSh, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL); D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS);
    ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDSS, &oRef);
    ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0; c->OMGetBlendState(&oBS, oBF, &oSM);
    ID3D11SamplerState* oS[2] = {}; c->PSGetSamplers(0, 2, oS);
    ID3D11ShaderResourceView* oVS120 = nullptr; c->PSGetShaderResources(120, 1, &oVS120);
    // estado comun
    ID3D11ShaderResourceView* nulls[5] = {};
    c->VSSetShader(s_vs, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->RSSetState(s_rs); c->OMSetDepthStencilState(s_dss, 0); float bf[4] = {}; c->OMSetBlendState(s_bs, bf, 0xffffffff);
    ID3D11SamplerState* ss[2] = { s_sampP, s_sampL }; c->PSSetSamplers(0, 2, ss);
    D3D11_VIEWPORT vp{ 0, 0, (float)td.Width, (float)td.Height, 0, 1 }; c->RSSetViewports(1, &vp);
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->PSSetShaderResources(120, 1, &prm);
    // pasada H: light -> tex0
    c->OMSetRenderTargets(1, &s_rtv[0], nullptr);
    ID3D11ShaderResourceView* sv0[5] = { light, depth, mat, nrm, in[8] }; c->PSSetShaderResources(0, 5, sv0);   // t4 = albedo (tono de piel)
    c->PSSetShader(s_psH, nullptr, 0); c->Draw(3, 0);
    // pasada V: tex0 -> tex1
    c->PSSetShaderResources(0, 5, nulls);
    c->OMSetRenderTargets(1, &s_rtv[1], nullptr);
    ID3D11ShaderResourceView* sv1[5] = { s_srv[0], depth, mat, nrm, in[8] }; c->PSSetShaderResources(0, 5, sv1);
    c->PSSetShader(s_psV, nullptr, 0); c->Draw(3, 0);
    c->PSSetShaderResources(0, 5, nulls);
    // restaurar
    c->OMSetRenderTargets(8, oRT, oDS); for (auto* r : oRT) if (r) r->Release(); if (oDS) oDS->Release();
    c->RSSetViewports(nvp, ovp);
    c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0); c->HSSetShader(oHS, nullptr, 0); c->DSSetShader(oDSh, nullptr, 0);
    if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release(); if (oHS) oHS->Release(); if (oDSh) oDSh->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release(); c->IASetPrimitiveTopology(oTopo);
    c->RSSetState(oRS); if (oRS) oRS->Release();
    c->OMSetDepthStencilState(oDSS, oRef); if (oDSS) oDSS->Release();
    c->OMSetBlendState(oBS, oBF, oSM); if (oBS) oBS->Release();
    c->PSSetSamplers(0, 2, oS); for (auto* s : oS) if (s) s->Release();
    c->PSSetShaderResources(0, 12, in);   // vuelve t0..t11 del juego (t9 lo reemplaza el que llama)
    c->PSSetShaderResources(120, 1, &oVS120); if (oVS120) oVS120->Release();
    rel();
    g_ssssDraws++;
    return s_srv[1];
}

} // namespace fx
