// PES21 FX - GTAO (20/10): oclusion ambiental por horizontes (el HBAO actual) calculada por la DLL antes de la composicion
// diferida con la profundidad y la normal del G-buffer (shaders\pes_gtao_ps.hlsl + desenfoque bilateral H/V). El resultado
// se enlaza en t27 de la composicion (619b) y de la luz global (fda6), que lo mezclan con el SSAO del juego segun y132.
#include "core.h"
#include <atomic>

namespace fx {

static ID3D11VertexShader* s_vs = nullptr; static ID3D11PixelShader* s_ps = nullptr; static ID3D11PixelShader* s_bh = nullptr; static ID3D11PixelShader* s_bv = nullptr; static ID3D11PixelShader* s_tp = nullptr; static bool s_tried = false;
// (21/10) acumulacion temporal: historia RG16F (oclusion, profundidad lineal) en ping-pong + camara del cuadro anterior
static ID3D11Texture2D* s_hTex[2] = {}; static ID3D11RenderTargetView* s_hRTV[2] = {}; static ID3D11ShaderResourceView* s_hSRV[2] = {}; static int s_hPing = 0;
static ID3D11Buffer* s_prev = nullptr; static bool s_prevOk = false; static DXGI_FORMAT s_loggedFmt = DXGI_FORMAT_FORCE_UINT;
static ID3D11Texture2D* s_tex[2] = {}; static ID3D11RenderTargetView* s_rtv[2] = {}; static ID3D11ShaderResourceView* s_srv[2] = {};
static UINT s_w = 0, s_h = 0;
static ID3D11SamplerState* s_sampP = nullptr; static ID3D11SamplerState* s_sampL = nullptr;
static ID3D11RasterizerState* s_rs = nullptr; static ID3D11DepthStencilState* s_dss = nullptr; static ID3D11BlendState* s_bs = nullptr;
static std::atomic<int> s_lastFrame{-1}; static ID3D11ShaderResourceView* s_last = nullptr;
std::atomic<int> g_gtaoDraws{0};
// (23/11 ru34vc) 534 VISIBILIDAD DEL CIELO DE LEJOS: pes_skyvis_ps a media resolucion (RGBA8: normal doblada + visibilidad) y su
// desenfoque H/V; la composicion (619b) y la luz global (fda6) la leen en t50. Techos, gradas, banco de suplentes, tunel.
static ID3D11PixelShader* s_fps = nullptr; static ID3D11PixelShader* s_fbh = nullptr; static ID3D11PixelShader* s_fbv = nullptr;
static ID3D11Texture2D* s_fTex[2] = {}; static ID3D11RenderTargetView* s_fRTV[2] = {}; static ID3D11ShaderResourceView* s_fSRV[2] = {};
static UINT s_fw = 0, s_fh = 0; static std::atomic<int> s_fFrame{-1}; static ID3D11ShaderResourceView* s_fLast = nullptr;
std::atomic<int> g_skyVisDraws{0};

static void EnsureShaders() {
    if (s_tried || !g_dev.dev) return; s_tried = true;
    ID3DBlob* vb = CompileNamedShader(L"pes_fs_vs.hlsl", "vs_5_0");
    ID3DBlob* pb = CompileNamedShader(L"pes_gtao_ps.hlsl", "ps_5_0");
    ID3DBlob* hb = CompileNamedShader(L"pes_gtao_bh_ps.hlsl", "ps_5_0");
    ID3DBlob* vb2 = CompileNamedShader(L"pes_gtao_bv_ps.hlsl", "ps_5_0");
    ID3DBlob* tpb = CompileNamedShader(L"pes_gtao_tp_ps.hlsl", "ps_5_0");
    if (tpb) { g_dev.dev->CreatePixelShader(tpb->GetBufferPointer(), tpb->GetBufferSize(), nullptr, &s_tp); tpb->Release(); }
    { ID3DBlob* f0 = CompileNamedShader(L"pes_skyvis_ps.hlsl", "ps_5_0"); ID3DBlob* f1 = CompileNamedShader(L"pes_skyvis_bh_ps.hlsl", "ps_5_0"); ID3DBlob* f2 = CompileNamedShader(L"pes_skyvis_bv_ps.hlsl", "ps_5_0");
      if (f0) { g_dev.dev->CreatePixelShader(f0->GetBufferPointer(), f0->GetBufferSize(), nullptr, &s_fps); f0->Release(); }
      if (f1) { g_dev.dev->CreatePixelShader(f1->GetBufferPointer(), f1->GetBufferSize(), nullptr, &s_fbh); f1->Release(); }
      if (f2) { g_dev.dev->CreatePixelShader(f2->GetBufferPointer(), f2->GetBufferSize(), nullptr, &s_fbv); f2->Release(); }
      Log("cielo de lejos: shaders %s", (s_fps && s_fbh && s_fbv) ? "listos" : "NO compilaron (ver errores)"); }
    if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_vs); vb->Release(); }
    if (pb) { g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_ps); pb->Release(); }
    if (hb) { g_dev.dev->CreatePixelShader(hb->GetBufferPointer(), hb->GetBufferSize(), nullptr, &s_bh); hb->Release(); }
    if (vb2) { g_dev.dev->CreatePixelShader(vb2->GetBufferPointer(), vb2->GetBufferSize(), nullptr, &s_bv); vb2->Release(); }
    if (!s_sampP) {
        D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        g_dev.dev->CreateSamplerState(&sd, &s_sampP); sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; g_dev.dev->CreateSamplerState(&sd, &s_sampL);
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; g_dev.dev->CreateRasterizerState(&rd, &s_rs);
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = FALSE; dd.StencilEnable = FALSE; g_dev.dev->CreateDepthStencilState(&dd, &s_dss);
        D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; g_dev.dev->CreateBlendState(&bd, &s_bs);
    }
    Log("gtao: shaders %s", (s_vs && s_ps && s_bh && s_bv) ? "listos" : "NO compilaron (ver errores)");
}
void GTAOReloadShaders() {
    if (s_vs) { s_vs->Release(); s_vs = nullptr; } if (s_ps) { s_ps->Release(); s_ps = nullptr; } if (s_bh) { s_bh->Release(); s_bh = nullptr; } if (s_bv) { s_bv->Release(); s_bv = nullptr; } if (s_tp) { s_tp->Release(); s_tp = nullptr; }
    if (s_fps) { s_fps->Release(); s_fps = nullptr; } if (s_fbh) { s_fbh->Release(); s_fbh = nullptr; } if (s_fbv) { s_fbv->Release(); s_fbv = nullptr; }
    s_tried = false;
}
static bool EnsureTargets(UINT w, UINT h) {
    if (s_tex[0] && s_w == w && s_h == h) return true;
    for (int i = 0; i < 2; i++) { if (s_srv[i]) s_srv[i]->Release(); if (s_rtv[i]) s_rtv[i]->Release(); if (s_tex[i]) s_tex[i]->Release(); s_srv[i] = nullptr; s_rtv[i] = nullptr; s_tex[i] = nullptr; }
    D3D11_TEXTURE2D_DESC td{}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int i = 0; i < 2; i++) {
        if (FAILED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_tex[i]))) return false;
        if (FAILED(g_dev.dev->CreateRenderTargetView(s_tex[i], nullptr, &s_rtv[i]))) return false;
        if (FAILED(g_dev.dev->CreateShaderResourceView(s_tex[i], nullptr, &s_srv[i]))) return false;
    }
    td.Format = DXGI_FORMAT_R16G16_FLOAT;
    for (int i = 0; i < 2; i++) {
        if (s_hSRV[i]) s_hSRV[i]->Release(); if (s_hRTV[i]) s_hRTV[i]->Release(); if (s_hTex[i]) s_hTex[i]->Release(); s_hSRV[i] = nullptr; s_hRTV[i] = nullptr; s_hTex[i] = nullptr;
        if (FAILED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_hTex[i]))) return false;
        if (FAILED(g_dev.dev->CreateRenderTargetView(s_hTex[i], nullptr, &s_hRTV[i]))) return false;
        if (FAILED(g_dev.dev->CreateShaderResourceView(s_hTex[i], nullptr, &s_hSRV[i]))) return false;
    }
    s_w = w; s_h = h; s_prevOk = false;
    Log("gtao: targets %ux%u R8 + historia RG16F", w, h);
    return true;
}

static bool EnsureFarTargets(UINT w, UINT h) {
    if (s_fTex[0] && s_fw == w && s_fh == h) return true;
    for (int i = 0; i < 2; i++) { if (s_fSRV[i]) s_fSRV[i]->Release(); if (s_fRTV[i]) s_fRTV[i]->Release(); if (s_fTex[i]) s_fTex[i]->Release(); s_fSRV[i] = nullptr; s_fRTV[i] = nullptr; s_fTex[i] = nullptr; }
    D3D11_TEXTURE2D_DESC td{}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int i = 0; i < 2; i++) {
        if (FAILED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_fTex[i]))) return false;
        if (FAILED(g_dev.dev->CreateRenderTargetView(s_fTex[i], nullptr, &s_fRTV[i]))) return false;
        if (FAILED(g_dev.dev->CreateShaderResourceView(s_fTex[i], nullptr, &s_fSRV[i]))) return false;
    }
    s_fw = w; s_fh = h; Log("cielo de lejos: targets %ux%u RGBA8", w, h);
    return true;
}
// (23/11 ru34vc) t50 de la composicion y la luz global: la del cuadro, o una neutra (1x1: sin normal doblada, cielo entero)
ID3D11ShaderResourceView* SkyVisSRV(int frame) {
    if ((g_cfg.live[534][0] > 0.5f || g_cfg.live[536][0] > 0.5f) && s_fFrame.load() == frame && s_fLast) return s_fLast;
    static ID3D11ShaderResourceView* s_v = nullptr; static bool tried = false;
    if (!tried && g_dev.dev) { tried = true;
        D3D11_TEXTURE2D_DESC td{}; td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const uint8_t v[4] = { 128, 128, 128, 255 }; D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = v; sd.SysMemPitch = 4;
        ID3D11Texture2D* t = nullptr;
        if (SUCCEEDED(g_dev.dev->CreateTexture2D(&td, &sd, &t)) && t) { g_dev.dev->CreateShaderResourceView(t, nullptr, &s_v); t->Release(); } }
    return s_v;
}
// (24/10 g) textura neutra (1x1, 1 = sin oclusion) para t27 cuando el GTAO no se calcula en esta escena (editor, menus: la
// escena es mas chica que la pantalla): la composicion lo pide igual si el GTAO esta prendido y sin nada enlazado leia 0
ID3D11ShaderResourceView* GTAONeutralSRV() {
    static ID3D11ShaderResourceView* s_v = nullptr; static bool tried = false;
    if (tried || !g_dev.dev) return s_v;
    tried = true;
    D3D11_TEXTURE2D_DESC td{}; td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const uint8_t one[4] = { 255, 255, 255, 255 }; D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = one; sd.SysMemPitch = 4;
    ID3D11Texture2D* t = nullptr;
    if (SUCCEEDED(g_dev.dev->CreateTexture2D(&td, &sd, &t)) && t) { g_dev.dev->CreateShaderResourceView(t, nullptr, &s_v); t->Release(); }
    return s_v;
}

// Devuelve el SRV de la oclusion GTAO para t27 (null si esta apagado o fallo). Se llama en BeforeDraw de la composicion
// y de la luz global; se calcula una vez por cuadro (la luz global corre antes y la composicion reutiliza).
ID3D11ShaderResourceView* GTAOBeforeComposition(ID3D11DeviceContext* c, int frame) {
    if (!g_dev.dev || g_cfg.live[132][0] < 0.5f) return nullptr;
    if (s_lastFrame.load() == frame && s_last) return s_last;
    EnsureShaders(); if (!s_vs || !s_ps || !s_bh || !s_bv) return nullptr;
    ID3D11ShaderResourceView* in[12] = {}; c->PSGetShaderResources(0, 12, in);
    auto rel = [&]() { for (auto* v : in) if (v) v->Release(); };
    // composicion: t0 normal, t11 profundidad; luz global (fda6): t6 normal, t5 profundidad
    ID3D11ShaderResourceView* depth = in[11] ? in[11] : in[5]; ID3D11ShaderResourceView* nrm = in[11] ? in[0] : in[6];
    if (!depth || !nrm) { rel(); return nullptr; }
    ID3D11Resource* res = nullptr; depth->GetResource(&res); if (!res) { rel(); return nullptr; }
    D3D11_TEXTURE2D_DESC td; ((ID3D11Texture2D*)res)->GetDesc(&td); res->Release();
    { // (21/10) formato real de la profundidad -> escalon (156.x) para la tolerancia del GTAO
      D3D11_SHADER_RESOURCE_VIEW_DESC vd; depth->GetDesc(&vd); DXGI_FORMAT f = vd.Format;
      float q = 0.0f;
      switch (f) { case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_TYPELESS: q = 1.0f / 65535.0f; break;
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS: q = 1.0f / 16777215.0f; break;
        case DXGI_FORMAT_R16_FLOAT: q = 1.0f / 2048.0f; break;
        default: q = 0.0f; break; }
      if (td.Width >= 640 && f != s_loggedFmt) { s_loggedFmt = f; Log("gtao: profundidad %ux%u formato %d (vista) / %d (textura), escalon %.3g", td.Width, td.Height, (int)f, (int)td.Format, q); }
      g_cfg.live[156][0] = q; }
    // (20/10) solo a pantalla completa: la luz global tambien corre para una sonda de 16x16; si se calculaba ahi, ese AO chico
    // quedaba como el del cuadro y la composicion lo usaba -> parpadeo oscuro / en bloques (y se recreaban las texturas 650 veces)
    if (td.Width < 640 || (g_bbW && (td.Width < g_bbW * 3 / 4 || td.Width > g_bbW * 3))) { rel(); return nullptr; }
    if (td.SampleDesc.Count != 1 || !EnsureTargets(td.Width, td.Height)) { rel(); return nullptr; }
    ID3D11Buffer* scb = nullptr; c->PSGetConstantBuffers(2, 1, &scb);
    ID3D11Buffer* sys = nullptr; c->PSGetConstantBuffers(0, 1, &sys);
    if (!scb) { rel(); if (sys) sys->Release(); return nullptr; }
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
    ID3D11ShaderResourceView* o120 = nullptr; c->PSGetShaderResources(120, 1, &o120);
    ID3D11ShaderResourceView* nulls[2] = {};
    c->VSSetShader(s_vs, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->RSSetState(s_rs); c->OMSetDepthStencilState(s_dss, 0); float bf[4] = {}; c->OMSetBlendState(s_bs, bf, 0xffffffff);
    ID3D11SamplerState* ss[2] = { s_sampP, s_sampL }; c->PSSetSamplers(0, 2, ss);
    D3D11_VIEWPORT vp{ 0, 0, (float)td.Width, (float)td.Height, 0, 1 }; c->RSSetViewports(1, &vp);
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->PSSetShaderResources(120, 1, &prm);
    c->PSSetConstantBuffers(2, 1, &scb); if (sys) c->PSSetConstantBuffers(0, 1, &sys);
    // 1) GTAO -> tex0
    c->OMSetRenderTargets(1, &s_rtv[0], nullptr);
    ID3D11ShaderResourceView* sv0[2] = { depth, nrm }; c->PSSetShaderResources(0, 2, sv0);
    c->PSSetShader(s_ps, nullptr, 0); c->Draw(3, 0);
    // 2) desenfoque H: tex0 -> tex1
    c->PSSetShaderResources(0, 2, nulls); c->OMSetRenderTargets(1, &s_rtv[1], nullptr);
    ID3D11ShaderResourceView* sv1[2] = { s_srv[0], depth }; c->PSSetShaderResources(0, 2, sv1);
    c->PSSetShader(s_bh, nullptr, 0); c->Draw(3, 0);
    // 3) desenfoque V: tex1 -> tex0
    c->PSSetShaderResources(0, 2, nulls); c->OMSetRenderTargets(1, &s_rtv[0], nullptr);
    ID3D11ShaderResourceView* sv2[2] = { s_srv[1], depth }; c->PSSetShaderResources(0, 2, sv2);
    c->PSSetShader(s_bv, nullptr, 0); c->Draw(3, 0);
    c->PSSetShaderResources(0, 2, nulls);
    // 3b) (23/11 ru34vc) visibilidad del cielo de lejos a media resolucion: skyvis -> f0, H f0 -> f1, V f1 -> f0
    if ((g_cfg.live[534][0] > 0.5f || g_cfg.live[536][0] > 0.5f) && s_fps && s_fbh && s_fbv && EnsureFarTargets((td.Width + 1) / 2, (td.Height + 1) / 2)) {
        D3D11_VIEWPORT vh{ 0, 0, (float)s_fw, (float)s_fh, 0, 1 }; c->RSSetViewports(1, &vh);
        c->OMSetRenderTargets(1, &s_fRTV[0], nullptr);
        c->PSSetShaderResources(0, 2, sv0); c->PSSetShader(s_fps, nullptr, 0); c->Draw(3, 0);
        c->PSSetShaderResources(0, 2, nulls); c->OMSetRenderTargets(1, &s_fRTV[1], nullptr);
        { ID3D11ShaderResourceView* f[2] = { s_fSRV[0], depth }; c->PSSetShaderResources(0, 2, f); }
        c->PSSetShader(s_fbh, nullptr, 0); c->Draw(3, 0);
        c->PSSetShaderResources(0, 2, nulls); c->OMSetRenderTargets(1, &s_fRTV[0], nullptr);
        { ID3D11ShaderResourceView* f[2] = { s_fSRV[1], depth }; c->PSSetShaderResources(0, 2, f); }
        c->PSSetShader(s_fbv, nullptr, 0); c->Draw(3, 0);
        c->PSSetShaderResources(0, 2, nulls);
        c->RSSetViewports(1, &vp);
        s_fLast = s_fSRV[0]; s_fFrame = frame; g_skyVisDraws++;
    }
    // 4) (21/10) acumulacion temporal: tex0 + historia anterior -> historia nueva (la que se usa)
    ID3D11ShaderResourceView* outSRV = s_srv[0];
    bool temporal = s_tp && s_hRTV[0] && s_hRTV[1];
    if (temporal) {
        D3D11_BUFFER_DESC bd; scb->GetDesc(&bd);
        if (s_prev) { D3D11_BUFFER_DESC pd; s_prev->GetDesc(&pd); if (pd.ByteWidth != bd.ByteWidth) { s_prev->Release(); s_prev = nullptr; } }
        if (!s_prev && bd.ByteWidth >= 480) { D3D11_BUFFER_DESC pd = bd; pd.Usage = D3D11_USAGE_DEFAULT; pd.CPUAccessFlags = 0; pd.MiscFlags = 0; pd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; pd.StructureByteStride = 0; g_dev.dev->CreateBuffer(&pd, nullptr, &s_prev); s_prevOk = false; }
        if (s_prev) {
            if (!s_prevOk) { c->CopyResource(s_prev, scb); s_prevOk = true; }
            { static int s_cutSeen = 0; if (CamCutSince(s_cutSeen)) { ZeroBuffer(c, s_prev); g_cutUse[1]++; } }   // (23/10) corte de camara del motor: sin historia
            ID3D11Buffer* o3 = nullptr; c->PSGetConstantBuffers(3, 1, &o3);
            int hn = s_hPing, ho = 1 - s_hPing; s_hPing = 1 - s_hPing;
            c->OMSetRenderTargets(1, &s_hRTV[hn], nullptr);
            ID3D11ShaderResourceView* sv3[3] = { s_srv[0], s_hSRV[ho], depth }; c->PSSetShaderResources(0, 3, sv3);
            c->PSSetConstantBuffers(3, 1, &s_prev);
            c->PSSetShader(s_tp, nullptr, 0); c->Draw(3, 0);
            ID3D11ShaderResourceView* n3[3] = {}; c->PSSetShaderResources(0, 3, n3);
            c->CopyResource(s_prev, scb);
            c->PSSetConstantBuffers(3, 1, &o3); if (o3) o3->Release();
            outSRV = s_hSRV[hn];
        } else temporal = false;
    }
    g_cfg.live[156][1] = temporal ? 1.0f : 0.0f;
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
    c->PSSetShaderResources(0, 12, in);
    c->PSSetShaderResources(120, 1, &o120); if (o120) o120->Release();
    c->PSSetConstantBuffers(2, 1, &scb); scb->Release(); if (sys) { c->PSSetConstantBuffers(0, 1, &sys); sys->Release(); }
    rel();
    g_gtaoDraws++; s_lastFrame = frame; s_last = outSRV;
    return outSRV;
}

} // namespace fx
