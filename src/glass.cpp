// PES21 FX - vidrio de los menus del juego (24/10), estilo "Liquid Glass" del iPhone.
// Los menus de PES (y el marcador del partido) se dibujan con un solo pixel shader (55a003da: textura x color, alfa) sobre la
// imagen RGB10A2 que despues compone 87552b53. Con la opcion prendida, cada dibujo de ese shader pasa por nuestro geometry
// shader (le agrega el rectangulo del elemento en pantalla) y nuestro PS (shaders\pes_glass_ps.hlsl), que decide por
// elemento si es un panel (vidrio: fondo esmerilado con borde brillante y refraccion) o texto / icono / foto (sin cambios).
// El fondo del vidrio es una copia de la imagen del menu tomada justo antes del primer panel (y otra vez antes de un panel
// que pisa algo dibujado despues de la ultima copia: paneles dentro de paneles), desenfocada en 3 niveles (Kawase doble).
// Cada pasada del menu anota en un buffer (u1) que dibujos fueron vidrio y su rectangulo; esa lista vuelve a la CPU unos
// cuadros despues (los menus son quietos) y decide donde tomar las copias en las pasadas siguientes.
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
static const uint64_t kFinalPS = 0x87552b538a1356edULL, kTaaPS = 0x1a7534929602c747ULL;
static const int kMaxDraws = 1024, kRing = 8, kMaxSnaps = 16;

static std::mutex s_mx;   // creacion de recursos, plan de copias y anillo de lectura
static bool s_tried = false;
static ID3D11VertexShader* s_fsVS = nullptr; static ID3D11GeometryShader* s_gs = nullptr;
static ID3D11PixelShader *s_ps = nullptr, *s_down = nullptr, *s_up = nullptr;
static ID3D11SamplerState* s_samp = nullptr; static ID3D11RasterizerState* s_rs = nullptr;
static ID3D11DepthStencilState* s_dss = nullptr; static ID3D11BlendState* s_bs = nullptr;
static ID3D11Buffer* s_drawCB[kMaxDraws * 2 + 2] = {};   // inmutables (k, premultiplicada): un cbuffer por numero de dibujo
// copia de la imagen y niveles del desenfoque: 0 = 1/2, 1 = 1/4, 2 = 1/8 (bajada), 3 = 1/4, 4 = 1/2 (subida: el que se usa)
static ID3D11Texture2D* s_snap = nullptr; static ID3D11ShaderResourceView* s_snapSRV = nullptr;
static ID3D11Texture2D* s_lv[5] = {}; static ID3D11RenderTargetView* s_lvRTV[5] = {}; static ID3D11ShaderResourceView* s_lvSRV[5] = {};
static UINT s_w = 0, s_h = 0; static DXGI_FORMAT s_fmt = DXGI_FORMAT_UNKNOWN;
static std::atomic<bool> s_haveSnap{false};
// lo que anota el PS por dibujo y su vuelta a la CPU
static ID3D11Buffer* s_rec = nullptr; static ID3D11UnorderedAccessView* s_recUAV = nullptr;
static ID3D11Buffer* s_stage[kRing] = {}; static int s_stageSeq[kRing];
static std::atomic<int> s_passSeq{0}; static int s_lastParsed = 0;
static std::vector<int> s_sched;   // numeros de dibujo antes de los que se copia el fondo (bajo s_mx)
static std::atomic<int> s_armSeq{-1}; static double s_nextLog = 0;
std::atomic<int> g_glassDraws{0}, g_glassSnaps{0};
// diagnostico: lo que sabe la CPU de cada dibujo de la pasada anotada
struct GlassNote { uint64_t vs = 0; UINT verts = 0, tw = 0, th = 0; int fmt = 0; int src = 0, dst = 0; bool bound = false; };
static GlassNote s_notes[kRing][kMaxDraws]; static int s_noteN[kRing] = {};
static ID3D11Resource* s_uiTex = nullptr;   // ultima imagen del menu (sin retener: solo para comparar punteros en el log)

// una pasada del menu = los dibujos de 55a003da sobre la misma imagen, en el mismo contexto, sin dibujos a otra imagen en el
// medio (los de otros shaders sobre la misma imagen, p.ej. las mascaras cdff1e1b, no la cortan) y sin una pausa de mas de
// 4 ms (en los menus 2D el contexto puede grabar solo menu cuadro tras cuadro)
struct GlassPass { ID3D11DeviceContext* ctx = nullptr; ID3D11Resource* tex = nullptr; bool inUI = false; double lastT = 0; int k = 0, seq = 0, snaps = 0; size_t next = 0; std::vector<int> sched; };
static thread_local GlassPass t_gp;

template<class T> static void RelT(T*& p) { if (p) { p->Release(); p = nullptr; } }
static ID3D11Resource* RT0Resource(ID3D11DeviceContext* c) {   // con AddRef
    ID3D11RenderTargetView* rtv = nullptr; c->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return nullptr;
    ID3D11Resource* r = nullptr; rtv->GetResource(&r); rtv->Release();
    return r;
}

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
    }
    if (!s_drawCB[0]) {
        for (int i = 0; i < kMaxDraws * 2 + 2; i++) {
            UINT v[4] = { i < kMaxDraws * 2 ? (UINT)(i >> 1) : 0xffffu, (UINT)(i & 1), 0, 0 };
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = v;
            d->CreateBuffer(&bd, &sd, &s_drawCB[i]);
        }
    }
    Log("vidrio: shaders %s", (s_fsVS && s_gs && s_ps && s_down && s_up) ? "listos" : "NO compilaron (ver errores)");
}
void GlassReloadShaders() {
    std::lock_guard<std::mutex> lk(s_mx);
    RelT(s_fsVS); RelT(s_gs); RelT(s_ps); RelT(s_down); RelT(s_up);
    s_tried = false;
}

static bool EnsureRec() {
    if (s_rec) return true;
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = kMaxDraws * 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = 16;
    std::vector<uint32_t> zero(kMaxDraws * 4, 0); D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = zero.data();
    if (FAILED(g_dev.dev->CreateBuffer(&bd, &sd, &s_rec))) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_UNKNOWN; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = kMaxDraws;
    if (FAILED(g_dev.dev->CreateUnorderedAccessView(s_rec, &ud, &s_recUAV))) { RelT(s_rec); return false; }
    D3D11_BUFFER_DESC st = bd; st.Usage = D3D11_USAGE_STAGING; st.BindFlags = 0; st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (int i = 0; i < kRing; i++) { g_dev.dev->CreateBuffer(&st, nullptr, &s_stage[i]); s_stageSeq[i] = -1; }
    return true;
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

// comienzo de una pasada del menu: lo anotado por la pasada anterior va al anillo de lectura y el buffer se limpia
static void PassBegin(ID3D11DeviceContext* c, GlassPass& st) {
    int prev = st.seq - 1;
    if (prev >= 1) { int slot = prev % kRing; c->CopyResource(s_stage[slot], s_rec); s_stageSeq[slot] = prev; }
    st.sched = s_sched; st.next = 0;
    const UINT z[4] = { 0, 0, 0, 0 }; c->ClearUnorderedAccessViewUint(s_recUAV, z);
    if (s_armSeq.load() == st.seq) s_noteN[st.seq % kRing] = 0;
}

// 0 = no sirve (sin mezcla, aditiva...), 1 = alfa normal (SRC_ALPHA, INV_SRC_ALPHA), 2 = premultiplicada (ONE, INV_SRC_ALPHA)
static int BlendMode(ID3D11BlendState* bs, int* src, int* dst) {
    if (!bs) { *src = *dst = 0; return 0; }
    D3D11_BLEND_DESC bd; bs->GetDesc(&bd);
    const auto& r = bd.RenderTarget[0];
    *src = r.BlendEnable ? (int)r.SrcBlend : 0; *dst = r.BlendEnable ? (int)r.DestBlend : 0;
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

bool GlassBeforeDraw(ID3D11DeviceContext* c, uint64_t ps, uint64_t vs, UINT verts, GlassScope& g) {
    GlassPass& st = t_gp;
    if (st.ctx != c) { st.ctx = c; st.inUI = false; }
    if (ps != kUiPS || !GlassWanted() || !g_dev.dev) {
        // otro shader en medio de la pasada: si dibuja sobre la misma imagen la pasada sigue
        if (st.inUI) { ID3D11Resource* r = RT0Resource(c); if (r != st.tex) st.inUI = false; if (r) r->Release(); }
        // (diagnostico) la composicion final y el TAA: leen / escriben la misma imagen que el menu?
        if ((ps == kFinalPS || ps == kTaaPS) && g_cfg.live[191][3] > 0.5f && s_uiTex) {
            static std::atomic<int> n{0};
            if (n < 6) {
                bool same = false;
                if (ps == kFinalPS) { ID3D11ShaderResourceView* t0 = nullptr; c->PSGetShaderResources(0, 1, &t0); if (t0) { ID3D11Resource* r = nullptr; t0->GetResource(&r); same = r == s_uiTex; if (r) r->Release(); t0->Release(); } }
                else { ID3D11RenderTargetView* rt[2] = {}; c->OMGetRenderTargets(2, rt, nullptr); for (auto* v : rt) if (v) { ID3D11Resource* r = nullptr; v->GetResource(&r); same |= r == s_uiTex; if (r) r->Release(); v->Release(); } }
                n++; Log("vidrio: %s %s la imagen del menu", ps == kFinalPS ? "la composicion final (87552b53)" : "el TAA (1a753492)", same ? (ps == kFinalPS ? "LEE" : "ESCRIBE") : (ps == kFinalPS ? "no lee" : "no escribe"));
            }
        }
        if (ps != kUiPS) return false;
        st.inUI = false; return false;
    }
    ID3D11Texture2D* tex = nullptr;
    { ID3D11Resource* r = RT0Resource(c); if (r) { r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex); r->Release(); } }
    if (!tex) { st.inUI = false; return false; }
    D3D11_TEXTURE2D_DESC td; tex->GetDesc(&td);
    bool fmtOk = td.Format == DXGI_FORMAT_R10G10B10A2_TYPELESS || td.Format == DXGI_FORMAT_R10G10B10A2_UNORM;
    UINT minW = g_bbW ? g_bbW * 3 / 4 : 1024;
    if (!fmtOk || td.SampleDesc.Count != 1 || td.ArraySize != 1 || td.Width < minW) { tex->Release(); st.inUI = false; return false; }
    std::lock_guard<std::mutex> lk(s_mx);   // los recursos compartidos (copias, buffer de anotaciones) no cambian mientras se usan
    EnsureShaders();
    if (!s_fsVS || !s_gs || !s_ps || !s_down || !s_up || !EnsureRec() || !EnsureTargets(td)) { tex->Release(); st.inUI = false; return false; }
    double now = Now();
    if (!st.inUI || st.tex != (ID3D11Resource*)tex || now - st.lastT > 0.004) {
        st.inUI = true; st.tex = tex; st.k = 0; st.snaps = 0; st.seq = ++s_passSeq;
        PassBegin(c, st);
        if (s_uiTex != tex) { s_uiTex = tex; static std::atomic<int> n{0}; if (n++ < 4) Log("vidrio: pasada del menu sobre %ux%u formato %d (contexto %s)", td.Width, td.Height, (int)td.Format, c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED ? "diferido" : "inmediato"); }
    }
    st.lastT = now;
    int k = st.k++;
    // copias del fondo: antes del primer panel de vidrio (sin plan todavia: antes del primer dibujo) y donde diga el plan
    bool want = false;
    if (st.sched.empty()) want = st.snaps == 0;
    else while (st.next < st.sched.size() && st.sched[st.next] <= k) { st.next++; want = true; }
    if (want && st.snaps < kMaxSnaps) { Snapshot(c, tex); st.snaps++; s_haveSnap = true; }
    tex->Release();

    bool armed = s_armSeq.load() == st.seq && k < kMaxDraws;
    GlassNote* note = nullptr;
    if (armed) {
        note = &s_notes[st.seq % kRing][k]; *note = GlassNote(); note->vs = vs; note->verts = verts;
        ID3D11ShaderResourceView* t8 = nullptr; c->PSGetShaderResources(8, 1, &t8);
        if (t8) { ID3D11Resource* r = nullptr; t8->GetResource(&r); ID3D11Texture2D* t2 = nullptr; if (r) { r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t2); r->Release(); }
            if (t2) { D3D11_TEXTURE2D_DESC d8; t2->GetDesc(&d8); note->tw = d8.Width; note->th = d8.Height; note->fmt = (int)d8.Format; t2->Release(); }
            t8->Release(); }
        int& nn = s_noteN[st.seq % kRing]; if (k + 1 > nn) nn = k + 1;
    }
    if (!s_haveSnap || (vs != kUiVS1 && vs != kUiVS2)) return false;
    D3D11_PRIMITIVE_TOPOLOGY topo; c->IAGetPrimitiveTopology(&topo);
    if (topo != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST && topo != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP) return false;
    { ID3D11GeometryShader* ogs = nullptr; c->GSGetShader(&ogs, nullptr, nullptr); if (ogs) { ogs->Release(); return false; } }
    ID3D11BlendState* bs = nullptr; float bf[4]; UINT sm = 0; c->OMGetBlendState(&bs, bf, &sm);
    int bsrc = 0, bdst = 0; int mode = BlendMode(bs, &bsrc, &bdst); if (bs) bs->Release();
    if (note) { note->src = bsrc; note->dst = bdst; }
    if (!mode) return false;
    // enlazar nuestro GS + PS, el fondo (t20 desenfocado, t21 nitido), los parametros, el sampler y el numero de dibujo
    c->PSGetShader(&g.ps, nullptr, nullptr);
    c->PSGetShaderResources(20, 2, g.srv);
    c->PSGetShaderResources(120, 1, &g.t120);
    c->PSGetSamplers(15, 1, &g.samp);
    c->PSGetConstantBuffers(13, 1, &g.cb);
    c->GSSetShader(s_gs, nullptr, 0); c->PSSetShader(s_ps, nullptr, 0);
    ID3D11ShaderResourceView* sv[2] = { s_lvSRV[4], s_snapSRV }; c->PSSetShaderResources(20, 2, sv);
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->PSSetShaderResources(120, 1, &prm);
    c->PSSetSamplers(15, 1, &s_samp);
    ID3D11Buffer* cb = s_drawCB[k < kMaxDraws ? k * 2 + (mode == 2 ? 1 : 0) : kMaxDraws * 2 + (mode == 2 ? 1 : 0)];
    c->PSSetConstantBuffers(13, 1, &cb);
    c->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 1, 1, &s_recUAV, nullptr);
    g.on = true;
    if (note) note->bound = true;
    g_glassDraws++;
    return true;
}

void GlassAfterDraw(ID3D11DeviceContext* c, GlassScope& g) {
    if (!g.on) return;
    ID3D11UnorderedAccessView* nu = nullptr;
    c->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 1, 1, &nu, nullptr);
    c->GSSetShader(nullptr, nullptr, 0);
    c->PSSetShader(g.ps, nullptr, 0); if (g.ps) g.ps->Release();
    c->PSSetShaderResources(20, 2, g.srv); for (auto* v : g.srv) if (v) v->Release();
    c->PSSetShaderResources(120, 1, &g.t120); if (g.t120) g.t120->Release();
    c->PSSetSamplers(15, 1, &g.samp); if (g.samp) g.samp->Release();
    c->PSSetConstantBuffers(13, 1, &g.cb); if (g.cb) g.cb->Release();
    g = GlassScope();
}

// en Present: leer lo que anoto una pasada de hace unos cuadros, armar el plan de copias y (con w191) anotar en el log
void GlassPresent() {
    if (!g_dev.imm || g_cfg.live[189][0] < 0.5f) return;
    double now = Now();
    bool logOn = g_cfg.live[191][3] > 0.5f;
    if (logOn && now > s_nextLog && s_armSeq.load() < s_passSeq.load()) { s_armSeq = s_passSeq.load() + 2; s_nextLog = now + 20.0; }
    int cur = s_passSeq.load();
    int target = cur - 3;
    if (target < 1 || target <= s_lastParsed || !s_rec) return;
    int slot = target % kRing;
    { std::lock_guard<std::mutex> lk(s_mx); if (s_stageSeq[slot] != target) return; }
    D3D11_MAPPED_SUBRESOURCE m{};
    HRESULT hr = g_dev.imm->Map(s_stage[slot], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING || FAILED(hr)) return;
    std::vector<uint32_t> rec(kMaxDraws * 4);
    memcpy(rec.data(), m.pData, kMaxDraws * 16);
    g_dev.imm->Unmap(s_stage[slot], 0);
    s_lastParsed = target;

    struct R { int k, x0, y0, x1, y1; uint32_t f, w; };
    std::vector<R> drawn;
    for (int k = 0; k < kMaxDraws; k++) {
        uint32_t f = rec[k * 4 + 2]; if (!(f & 1)) continue;
        R r; r.k = k; r.x0 = rec[k * 4] & 0xffff; r.y0 = rec[k * 4] >> 16; r.x1 = rec[k * 4 + 1] & 0xffff; r.y1 = rec[k * 4 + 1] >> 16; r.f = f; r.w = rec[k * 4 + 3];
        drawn.push_back(r);
    }
    // plan: copiar antes del primer panel de vidrio y antes de cada panel que pisa algo dibujado despues de la ultima copia
    std::vector<int> sched; std::vector<const R*> since;
    auto over = [](const R& a, const R& b) { int w = (std::min)(a.x1, b.x1) - (std::max)(a.x0, b.x0), h = (std::min)(a.y1, b.y1) - (std::max)(a.y0, b.y0); return w > 2 && h > 2; };
    int nGlass = 0, nSheen = 0;
    for (const R& r : drawn) {
        if (r.f & 32) nSheen++;
        if (r.f & 16) {
            nGlass++;
            bool need = sched.empty();
            if (!need) for (const R* s : since) if (over(*s, r)) { need = true; break; }
            if (need && (int)sched.size() < kMaxSnaps) { sched.push_back(r.k); since.clear(); }
        }
        since.push_back(&r);
    }
    { std::lock_guard<std::mutex> lk(s_mx); s_sched = sched; }

    if (logOn && target == s_armSeq.load()) {
        std::string sp; for (int v : sched) { char b[16]; snprintf(b, sizeof(b), " #%d", v); sp += b; }
        Log("vidrio: pasada %d: %d dibujos visibles, %d de vidrio, %d con brillo; copias del fondo antes de:%s (%d dibujos con nuestro shader, %d copias desde el inicio)",
            target, (int)drawn.size(), nGlass, nSheen, sp.empty() ? " (ninguna)" : sp.c_str(), g_glassDraws.load(), g_glassSnaps.load());
        const GlassNote* notes = s_notes[target % kRing]; int nn = s_noteN[target % kRing];
        int shown = 0;
        for (const R& r : drawn) {
            bool interesting = (r.f & (16 | 32)) || ((r.x1 - r.x0) >= 64 && (r.y1 - r.y0) >= 24);
            if (!interesting || shown >= 70) continue;
            shown++;
            const GlassNote* nt = r.k < nn ? &notes[r.k] : nullptr;
            Log("vidrio:  #%d (%d,%d)-(%d,%d) %dx%d |%s%s%s%s%s%s%s%s | alfa %.2f lum %.2f dif %.2f dentro %u | VS %08x %u vert | tex %ux%u fmt %d | mezcla %d/%d%s",
                r.k, r.x0, r.y0, r.x1, r.y1, r.x1 - r.x0, r.y1 - r.y0,
                (r.f & 2) ? " recto" : " girado", (r.f & 4) ? " parejo" : "", (r.f & 8) ? " transl" : " opaco", (r.f & 16) ? " VIDRIO" : "", (r.f & 32) ? " BRILLO" : "",
                (r.f & 64) ? " tex-entera" : " tex-parte", (r.f & 128) ? " rect" : "", (r.f & 256) ? " suave" : "",
                (r.w & 255) / 255.0f, ((r.w >> 8) & 255) / 255.0f, ((r.w >> 16) & 255) / 255.0f, r.w >> 24,
                nt ? (unsigned)(nt->vs >> 32) : 0u, nt ? nt->verts : 0u, nt ? nt->tw : 0u, nt ? nt->th : 0u, nt ? nt->fmt : 0, nt ? nt->src : 0, nt ? nt->dst : 0, (nt && !nt->bound) ? " (sin nuestro shader)" : "");
        }
    }
}

} // namespace fx
