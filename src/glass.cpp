// PES21 FX - vidrio en el menu Fisico (24/10), estilo "Liquid Glass" del iPhone.
// Los menus de PES se dibujan con un solo pixel shader (55a003da: textura x color, alfa) sobre la imagen RGB10A2 que despues
// compone 87552b53. Con la opcion prendida (y por defecto solo en Editar > Jugadores > Fisico), cada dibujo de ese shader pasa
// por nuestro geometry shader (pes_glass_gs.hlsl: decide una vez por triangulo si el elemento es un panel y le agrega su
// rectangulo en pantalla) y nuestro PS (pes_glass_ps.hlsl: vidrio con fondo desenfocado, borde brillante, refraccion y
// esquinas redondeadas; texto, iconos y fotos quedan igual). El fondo del vidrio es una copia de la imagen del menu tomada
// antes del primer dibujo con una textura chica (los fondos de pantalla completa del menu usan texturas grandes y quedan en
// la copia), desenfocada en 3 niveles (Kawase doble).
// v2 (24/10 c): la v1 bajaba mucho los cuadros: cada dibujo escribia un buffer (u1) y la placa esperaba a que terminara el
// anterior, la decision se hacia en cada pixel (25 lecturas) y la pasada se cortaba en cada mascara del menu (una copia del
// fondo por cada corte). Ahora no hay buffer, la decision es por triangulo y la pasada solo se corta por tiempo.
//   189: x on | y esmerilado | z radio del desenfoque | w tinte;  190: x brillo del borde | y refraccion | z radio de las
//   esquinas (px) | w brillo de arriba;  191: x opacidad de paneles opacos | y tamano minimo (px) | z tolerancia | w log
//   192: x donde (0 = solo el menu Fisico de Editar > Jugadores, 1 = todos los menus)
#include "core.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace fx {

static const uint64_t kUiPS = 0x55a003da3f888a81ULL, kUiVS1 = 0xfb68a5a0762dad46ULL, kUiVS2 = 0x1932d77df62c65beULL;

static std::mutex s_mx;   // creacion de recursos
static bool s_tried = false;
static ID3D11VertexShader* s_fsVS = nullptr; static ID3D11GeometryShader* s_gs = nullptr;
static ID3D11PixelShader *s_ps = nullptr, *s_down = nullptr, *s_up = nullptr;
static ID3D11SamplerState* s_samp = nullptr; static ID3D11RasterizerState* s_rs = nullptr;
static ID3D11DepthStencilState* s_dss = nullptr; static ID3D11BlendState* s_bs = nullptr;
static ID3D11Buffer* s_drawCB[2] = {};   // inmutables: (0, premultiplicada, 0, 0)
// copia de la imagen y niveles del desenfoque: 0 = 1/2, 1 = 1/4, 2 = 1/8 (bajada), 3 = 1/4, 4 = 1/2 (subida: el que se usa)
static ID3D11Texture2D* s_snap = nullptr; static ID3D11ShaderResourceView* s_snapSRV = nullptr;
static ID3D11Texture2D* s_lv[5] = {}; static ID3D11RenderTargetView* s_lvRTV[5] = {}; static ID3D11ShaderResourceView* s_lvSRV[5] = {};
static UINT s_w = 0, s_h = 0; static DXGI_FORMAT s_fmt = DXGI_FORMAT_UNKNOWN;
static std::atomic<bool> s_haveSnap{false};
std::atomic<int> g_glassDraws{0}, g_glassSnaps{0};
// diagnostico (w191): resumen de la ultima pasada
struct PassInfo { int draws = 0, snapAt = -1, bound = 0; UINT tw[6] = {}, th[6] = {}; };
static PassInfo s_last; static std::mutex s_lastMx; static double s_nextLog = 0;

// una pasada del menu = los dibujos de 55a003da sobre la misma imagen, en el mismo contexto, sin una pausa de mas de 6 ms
// (otros dibujos en el medio, como las mascaras del menu a otra imagen, no la cortan)
struct GlassPass { ID3D11DeviceContext* ctx = nullptr; ID3D11Resource* tex = nullptr; bool inUI = false, snapped = false; double lastT = 0; int k = 0; PassInfo info; };
static thread_local GlassPass t_gp;

template<class T> static void RelT(T*& p) { if (p) { p->Release(); p = nullptr; } }

static void EnsureShaders() {
    if (s_tried || !g_dev.dev) return; s_tried = true;
    ID3D11Device* d = g_dev.dev;
    if (ID3DBlob* b = CompileNamedShader(L"pes_fs_vs.hlsl", "vs_5_0")) { d->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_fsVS); b->Release(); }
    if (ID3DBlob* b = CompileNamedShader(L"pes_glass_gs.hlsl", "gs_5_0")) { d->CreateGeometryShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_gs); b->Release(); }
    if (ID3DBlob* b = CompileNamedShader(L"pes_glass_ps.hlsl", "ps_5_0")) { d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_ps); b->Release(); }
    if (ID3DBlob* b = CompileNamedShader(L"pes_glass_down_ps.hlsl", "ps_5_0")) { d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_down); b->Release(); }
    if (ID3DBlob* b = CompileNamedShader(L"pes_glass_up_ps.hlsl", "ps_5_0")) { d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_up); b->Release(); }
    if (!s_samp) {
        D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        d->CreateSamplerState(&sd, &s_samp);
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; d->CreateRasterizerState(&rd, &s_rs);
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = FALSE; dd.StencilEnable = FALSE; d->CreateDepthStencilState(&dd, &s_dss);
        D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; d->CreateBlendState(&bd, &s_bs);
        for (int i = 0; i < 2; i++) {
            UINT v[4] = { 0, (UINT)i, 0, 0 };
            D3D11_BUFFER_DESC cb{}; cb.ByteWidth = 16; cb.Usage = D3D11_USAGE_IMMUTABLE; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            D3D11_SUBRESOURCE_DATA sd2{}; sd2.pSysMem = v;
            d->CreateBuffer(&cb, &sd2, &s_drawCB[i]);
        }
    }
    Log("vidrio: shaders %s", (s_fsVS && s_gs && s_ps && s_down && s_up) ? "listos (v2)" : "NO compilaron (ver errores)");
}
void GlassReloadShaders() {
    std::lock_guard<std::mutex> lk(s_mx);
    RelT(s_fsVS); RelT(s_gs); RelT(s_ps); RelT(s_down); RelT(s_up);
    s_tried = false;
}

static bool EnsureTargets(const D3D11_TEXTURE2D_DESC& rd) {
    if (s_snap && s_w == rd.Width && s_h == rd.Height && s_fmt == rd.Format) return true;
    RelT(s_snapSRV); RelT(s_snap);
    for (int i = 0; i < 5; i++) { RelT(s_lvSRV[i]); RelT(s_lvRTV[i]); RelT(s_lv[i]); }
    s_haveSnap = false;
    D3D11_TEXTURE2D_DESC td{}; td.Width = rd.Width; td.Height = rd.Height; td.MipLevels = 1; td.ArraySize = 1; td.Format = rd.Format;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_snap))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = rd.Format == DXGI_FORMAT_R10G10B10A2_TYPELESS ? DXGI_FORMAT_R10G10B10A2_UNORM : rd.Format;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
    if (FAILED(g_dev.dev->CreateShaderResourceView(s_snap, &sv, &s_snapSRV))) return false;
    const UINT div[5] = { 2, 4, 8, 4, 2 };
    for (int i = 0; i < 5; i++) {
        D3D11_TEXTURE2D_DESC ld = td; ld.Width = (std::max)(1u, (rd.Width + div[i] - 1) / div[i]); ld.Height = (std::max)(1u, (rd.Height + div[i] - 1) / div[i]);
        ld.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; ld.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_dev.dev->CreateTexture2D(&ld, nullptr, &s_lv[i]))) return false;
        if (FAILED(g_dev.dev->CreateRenderTargetView(s_lv[i], nullptr, &s_lvRTV[i]))) return false;
        if (FAILED(g_dev.dev->CreateShaderResourceView(s_lv[i], nullptr, &s_lvSRV[i]))) return false;
    }
    s_w = rd.Width; s_h = rd.Height; s_fmt = rd.Format;
    Log("vidrio: copia del menu %ux%u (formato %d) + desenfoque en 3 niveles", rd.Width, rd.Height, (int)rd.Format);
    return true;
}

// copia la imagen del menu tal como esta en este punto de la pasada y la desenfoca (todo grabado en el contexto del juego)
static void Snapshot(ID3D11DeviceContext* c, ID3D11Texture2D* tex) {
    ID3D11RenderTargetView* oRT[8] = {}; ID3D11DepthStencilView* oDS = nullptr; c->OMGetRenderTargets(8, oRT, &oDS);
    UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; D3D11_VIEWPORT ovp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; c->RSGetViewports(&nvp, ovp);
    UINT nsc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; D3D11_RECT osc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; c->RSGetScissorRects(&nsc, osc);
    ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr; ID3D11HullShader* oHS = nullptr; ID3D11DomainShader* oDSh = nullptr;
    c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr); c->HSGetShader(&oHS, nullptr, nullptr); c->DSGetShader(&oDSh, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL); D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS);
    ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDSS, &oRef);
    ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0; c->OMGetBlendState(&oBS, oBF, &oSM);
    ID3D11SamplerState* oS1 = nullptr; c->PSGetSamplers(1, 1, &oS1);
    ID3D11ShaderResourceView* oT0 = nullptr; c->PSGetShaderResources(0, 1, &oT0);
    ID3D11ShaderResourceView* o120 = nullptr; c->PSGetShaderResources(120, 1, &o120);

    c->CopyResource(s_snap, tex);
    c->VSSetShader(s_fsVS, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->RSSetState(s_rs); c->OMSetDepthStencilState(s_dss, 0); float bf[4] = {}; c->OMSetBlendState(s_bs, bf, 0xffffffff);
    c->PSSetSamplers(1, 1, &s_samp);
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->PSSetShaderResources(120, 1, &prm);
    auto pass = [&](ID3D11PixelShader* ps, ID3D11ShaderResourceView* src, int dst) {
        ID3D11ShaderResourceView* nul = nullptr; c->PSSetShaderResources(0, 1, &nul);
        c->OMSetRenderTargets(1, &s_lvRTV[dst], nullptr);
        D3D11_TEXTURE2D_DESC ld; s_lv[dst]->GetDesc(&ld);
        D3D11_VIEWPORT vp{ 0, 0, (float)ld.Width, (float)ld.Height, 0, 1 }; c->RSSetViewports(1, &vp);
        c->PSSetShaderResources(0, 1, &src); c->PSSetShader(ps, nullptr, 0); c->Draw(3, 0);
    };
    pass(s_down, s_snapSRV, 0); pass(s_down, s_lvSRV[0], 1); pass(s_down, s_lvSRV[1], 2);
    pass(s_up, s_lvSRV[2], 3); pass(s_up, s_lvSRV[3], 4);
    { ID3D11ShaderResourceView* nul = nullptr; c->PSSetShaderResources(0, 1, &nul); }

    c->OMSetRenderTargets(8, oRT, oDS); for (auto* r : oRT) if (r) r->Release(); if (oDS) oDS->Release();
    c->RSSetViewports(nvp, ovp); c->RSSetScissorRects(nsc, osc);
    c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0); c->HSSetShader(oHS, nullptr, 0); c->DSSetShader(oDSh, nullptr, 0);
    if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release(); if (oHS) oHS->Release(); if (oDSh) oDSh->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release(); c->IASetPrimitiveTopology(oTopo);
    c->RSSetState(oRS); if (oRS) oRS->Release();
    c->OMSetDepthStencilState(oDSS, oRef); if (oDSS) oDSS->Release();
    c->OMSetBlendState(oBS, oBF, oSM); if (oBS) oBS->Release();
    c->PSSetSamplers(1, 1, &oS1); if (oS1) oS1->Release();
    c->PSSetShaderResources(0, 1, &oT0); if (oT0) oT0->Release();
    c->PSSetShaderResources(120, 1, &o120); if (o120) o120->Release();
    g_glassSnaps++;
}

// 0 = no sirve (sin mezcla, aditiva...), 1 = alfa normal (SRC_ALPHA, INV_SRC_ALPHA), 2 = premultiplicada (ONE, INV_SRC_ALPHA)
static int BlendMode(ID3D11BlendState* bs) {
    if (!bs) return 0;
    D3D11_BLEND_DESC bd; bs->GetDesc(&bd);
    const auto& r = bd.RenderTarget[0];
    if (!r.BlendEnable || r.BlendOp != D3D11_BLEND_OP_ADD || r.DestBlend != D3D11_BLEND_INV_SRC_ALPHA) return 0;
    if ((r.RenderTargetWriteMask & 7) != 7) return 0;
    if (r.SrcBlend == D3D11_BLEND_SRC_ALPHA) return 1;
    if (r.SrcBlend == D3D11_BLEND_ONE) return 2;
    return 0;
}

// (24/10) por pedido: solo en Editar > Jugadores > Fisico (y sus paginas de musculatura y piel); x192 = 1 lo lleva a todos los
// menus. El menu Fisico se da por activo hasta 0.15 s despues de su ultimo paso (asi no queda vidrio en el menu siguiente)
static bool GlassWanted() {
    if (g_cfg.live[189][0] < 0.5f) return false;
    return g_cfg.live[192][0] > 0.5f || MusclePhysiqueAge() < 0.15;
}

static void EndPass(GlassPass& st) {
    if (!st.inUI) return;
    st.inUI = false;
    std::lock_guard<std::mutex> lk(s_lastMx); s_last = st.info;
}

bool GlassBeforeDraw(ID3D11DeviceContext* c, uint64_t ps, uint64_t vs, UINT verts, GlassScope& g) {
    (void)verts;
    if (ps != kUiPS) return false;   // los demas dibujos no cortan la pasada (solo el tiempo o otra imagen)
    GlassPass& st = t_gp;
    if (st.ctx != c) { EndPass(st); st.ctx = c; }
    if (!GlassWanted() || !g_dev.dev) { EndPass(st); return false; }
    ID3D11Texture2D* tex = nullptr;
    {
        ID3D11RenderTargetView* rtv = nullptr; c->OMGetRenderTargets(1, &rtv, nullptr);
        if (rtv) { ID3D11Resource* r = nullptr; rtv->GetResource(&r); rtv->Release(); if (r) { r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex); r->Release(); } }
    }
    if (!tex) return false;
    D3D11_TEXTURE2D_DESC td; tex->GetDesc(&td);
    bool fmtOk = td.Format == DXGI_FORMAT_R10G10B10A2_TYPELESS || td.Format == DXGI_FORMAT_R10G10B10A2_UNORM;
    UINT minW = g_bbW ? g_bbW * 3 / 4 : 1024;
    if (!fmtOk || td.SampleDesc.Count != 1 || td.ArraySize != 1 || td.Width < minW) { tex->Release(); return false; }
    std::lock_guard<std::mutex> lk(s_mx);   // los recursos compartidos (copias, shaders) no cambian mientras se usan
    EnsureShaders();
    if (!s_fsVS || !s_gs || !s_ps || !s_down || !s_up || !EnsureTargets(td)) { tex->Release(); return false; }
    const double now = Now();
    if (!st.inUI || st.tex != (ID3D11Resource*)tex || now - st.lastT > 0.006) {
        EndPass(st);
        st.inUI = true; st.tex = tex; st.k = 0; st.snapped = false; st.info = PassInfo();
    }
    st.lastT = now;
    const int k = st.k++;
    st.info.draws = st.k;
    // textura del dibujo: los fondos de pantalla completa usan texturas grandes; la copia del fondo se toma antes del primer
    // dibujo con una textura chica (un panel, un texto), una vez por pasada
    UINT tw = 0, th = 0;
    {
        ID3D11ShaderResourceView* t8 = nullptr; c->PSGetShaderResources(8, 1, &t8);
        if (t8) { ID3D11Resource* r = nullptr; t8->GetResource(&r); ID3D11Texture2D* t2 = nullptr; if (r) { r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t2); r->Release(); }
            if (t2) { D3D11_TEXTURE2D_DESC d8; t2->GetDesc(&d8); tw = d8.Width; th = d8.Height; t2->Release(); }
            t8->Release(); }
    }
    if (k < 6) { st.info.tw[k] = tw; st.info.th[k] = th; }
    if (!st.snapped && (tw < 1024 || th < 512)) {
        Snapshot(c, tex); st.snapped = true; s_haveSnap = true; st.info.snapAt = k;
    }
    tex->Release();
    if (!s_haveSnap || (vs != kUiVS1 && vs != kUiVS2)) return false;
    D3D11_PRIMITIVE_TOPOLOGY topo; c->IAGetPrimitiveTopology(&topo);
    if (topo != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST && topo != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP) return false;
    { ID3D11GeometryShader* ogs = nullptr; c->GSGetShader(&ogs, nullptr, nullptr); if (ogs) { ogs->Release(); return false; } }
    int mode = 0;
    { ID3D11BlendState* bs = nullptr; float bf[4]; UINT sm = 0; c->OMGetBlendState(&bs, bf, &sm); mode = BlendMode(bs); if (bs) bs->Release(); }
    if (!mode) return false;
    // PS: el nuestro, el fondo (t20 desenfocado, t21 nitido), parametros, sampler y la mezcla (b13)
    c->PSGetShader(&g.ps, nullptr, nullptr);
    c->PSGetShaderResources(20, 2, g.srv);
    c->PSGetShaderResources(120, 1, &g.t120);
    c->PSGetSamplers(15, 1, &g.samp);
    c->PSGetConstantBuffers(13, 1, &g.cb);
    // GS: la textura y el sampler del dibujo (para decidir que es), la copia (tamano de pantalla) y los parametros
    ID3D11ShaderResourceView* t8 = nullptr; c->PSGetShaderResources(8, 1, &t8);
    ID3D11SamplerState* s0 = nullptr; c->PSGetSamplers(0, 1, &s0);
    c->GSGetShaderResources(8, 1, &g.gsT8); c->GSGetShaderResources(21, 1, &g.gsT21); c->GSGetShaderResources(120, 1, &g.gsT120); c->GSGetSamplers(0, 1, &g.gsS0);
    c->GSSetShaderResources(8, 1, &t8); c->GSSetSamplers(0, 1, &s0);
    c->GSSetShaderResources(21, 1, &s_snapSRV);
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->GSSetShaderResources(120, 1, &prm);
    if (t8) t8->Release(); if (s0) s0->Release();
    c->GSSetShader(s_gs, nullptr, 0); c->PSSetShader(s_ps, nullptr, 0);
    ID3D11ShaderResourceView* sv[2] = { s_lvSRV[4], s_snapSRV }; c->PSSetShaderResources(20, 2, sv);
    c->PSSetShaderResources(120, 1, &prm);
    c->PSSetSamplers(15, 1, &s_samp);
    c->PSSetConstantBuffers(13, 1, &s_drawCB[mode == 2 ? 1 : 0]);
    g.on = true;
    st.info.bound++;
    g_glassDraws++;
    return true;
}

void GlassAfterDraw(ID3D11DeviceContext* c, GlassScope& g) {
    if (!g.on) return;
    c->GSSetShader(nullptr, nullptr, 0);
    c->GSSetShaderResources(8, 1, &g.gsT8); if (g.gsT8) g.gsT8->Release();
    c->GSSetShaderResources(21, 1, &g.gsT21); if (g.gsT21) g.gsT21->Release();
    c->GSSetShaderResources(120, 1, &g.gsT120); if (g.gsT120) g.gsT120->Release();
    c->GSSetSamplers(0, 1, &g.gsS0); if (g.gsS0) g.gsS0->Release();
    c->PSSetShader(g.ps, nullptr, 0); if (g.ps) g.ps->Release();
    c->PSSetShaderResources(20, 2, g.srv); for (auto* v : g.srv) if (v) v->Release();
    c->PSSetShaderResources(120, 1, &g.t120); if (g.t120) g.t120->Release();
    c->PSSetSamplers(15, 1, &g.samp); if (g.samp) g.samp->Release();
    c->PSSetConstantBuffers(13, 1, &g.cb); if (g.cb) g.cb->Release();
    g = GlassScope();
}

// en Present: (w191) cada 20 s, como fue la ultima pasada del menu
void GlassPresent() {
    if (g_cfg.live[189][0] < 0.5f || g_cfg.live[191][3] < 0.5f) return;
    const double now = Now();
    if (now < s_nextLog) return;
    PassInfo p; { std::lock_guard<std::mutex> lk(s_lastMx); p = s_last; s_last = PassInfo(); }
    if (p.draws <= 0) return;
    s_nextLog = now + 20.0;
    char tex[160]; int n = 0; tex[0] = 0;
    for (int i = 0; i < 6 && i < p.draws && n < 140; i++) n += snprintf(tex + n, sizeof tex - n, " %ux%u", p.tw[i], p.th[i]);
    Log("vidrio: pasada del menu de %d dibujos, %d con nuestro shader, copia del fondo antes del #%d | texturas de los primeros:%s | %d copias desde el inicio",
        p.draws, p.bound, p.snapAt, tex, g_glassSnaps.load());
}

} // namespace fx
