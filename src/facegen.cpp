// PES21 FX - "quinta textura" de la cara generada en vivo (18/10).
// Cuando un dibujo con regla `facegen` aparece con una textura de normal (t1) que no vimos, se anota un pedido;
// en el siguiente Present se dibuja (contexto inmediato) un triangulo de pantalla completa con
// shaders\pes_facegen_ps.hlsl leyendo las 4 texturas de esa cara, a dos texturas propias con mips. De ahi en mas,
// cada dibujo de esa cara las recibe en los slots pedidos. Cache por recurso; se vacia al empezar un partido nuevo
// (las texturas de cara del partido anterior se liberan y sus direcciones pueden reutilizarse).
#include "core.h"
#include <d3d11_1.h>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace fx {

struct FaceEntry { ID3D11ShaderResourceView* a = nullptr; ID3D11ShaderResourceView* b = nullptr; bool pending = false; bool failed = false; };
struct FaceReq { ID3D11Resource* key; ID3D11ShaderResourceView* in[4]; UINT w, h; };

static std::unordered_map<ID3D11Resource*, FaceEntry> s_cache;
static std::vector<FaceReq> s_reqs;
static std::mutex s_mx;
static ID3D11VertexShader* s_vs = nullptr;
static ID3D11PixelShader* s_ps = nullptr;
static ID3D11SamplerState* s_samp = nullptr;
static bool s_shadersTried = false, s_shadersOk = false;
static int s_generated = 0;
int FaceGenCount() { return s_generated; }

static void ReleaseEntry(FaceEntry& e) { if (e.a) e.a->Release(); if (e.b) e.b->Release(); e.a = e.b = nullptr; }

void FaceGenClear() {
    std::lock_guard<std::mutex> lk(s_mx);
    for (auto& kv : s_cache) ReleaseEntry(kv.second);
    s_cache.clear();
    for (auto& r : s_reqs) for (int i = 0; i < 4; i++) if (r.in[i]) r.in[i]->Release();
    s_reqs.clear();
}
void FaceGenReloadShaders() {
    std::lock_guard<std::mutex> lk(s_mx);
    if (s_vs) { s_vs->Release(); s_vs = nullptr; }
    if (s_ps) { s_ps->Release(); s_ps = nullptr; }
    s_shadersTried = false; s_shadersOk = false;
    // lo ya generado se rehace con el shader nuevo
    for (auto& kv : s_cache) ReleaseEntry(kv.second);
    s_cache.clear();
}

// llamado antes de un dibujo con regla facegen (cualquier contexto): devuelve las dos texturas si ya existen
bool FaceGenLookup(ID3D11DeviceContext* c, ID3D11ShaderResourceView** outA, ID3D11ShaderResourceView** outB) {
    *outA = *outB = nullptr;
    ID3D11ShaderResourceView* in[4] = {};
    c->PSGetShaderResources(0, 4, in);
    if (!in[1]) { for (int i = 0; i < 4; i++) if (in[i]) in[i]->Release(); return false; }
    ID3D11Resource* key = nullptr; in[1]->GetResource(&key);
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        auto it = s_cache.find(key);
        if (it != s_cache.end()) {
            if (it->second.a) { *outA = it->second.a; *outB = it->second.b; found = true; }
        } else if (in[0] && in[2] && in[3] && s_cache.size() < 96) {
            D3D11_RESOURCE_DIMENSION dim; key->GetType(&dim);
            if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
                D3D11_TEXTURE2D_DESC td; ((ID3D11Texture2D*)key)->GetDesc(&td);
                FaceReq r; r.key = key; r.w = td.Width > 2048 ? 2048 : td.Width; r.h = td.Height > 2048 ? 2048 : td.Height;
                for (int i = 0; i < 4; i++) { r.in[i] = in[i]; in[i]->AddRef(); }
                s_reqs.push_back(r);
                s_cache[key].pending = true;
            }
        }
    }
    key->Release();
    for (int i = 0; i < 4; i++) if (in[i]) in[i]->Release();
    return found;
}

static bool EnsureShaders() {
    if (s_shadersTried) return s_shadersOk;
    s_shadersTried = true;
    ID3DBlob* vb = CompileNamedShader(L"pes_facegen_vs.hlsl", "vs_5_0");
    ID3DBlob* pb = CompileNamedShader(L"pes_facegen_ps.hlsl", "ps_5_0");
    if (vb && pb && g_dev.dev) {
        if (SUCCEEDED(g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_vs)) &&
            SUCCEEDED(g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_ps))) s_shadersOk = true;
    }
    if (vb) vb->Release(); if (pb) pb->Release();
    if (!s_samp && g_dev.dev) {
        D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        g_dev.dev->CreateSamplerState(&sd, &s_samp);
    }
    if (!s_shadersOk) Log("facegen: no se pudieron compilar los shaders del generador (ver errores arriba)");
    return s_shadersOk;
}

static bool MakeTarget(UINT w, UINT h, ID3D11Texture2D** tex, ID3D11RenderTargetView** rtv, ID3D11ShaderResourceView** srv) {
    D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 0; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE; d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(g_dev.dev->CreateTexture2D(&d, nullptr, tex))) return false;
    if (FAILED(g_dev.dev->CreateRenderTargetView(*tex, nullptr, rtv))) return false;
    if (FAILED(g_dev.dev->CreateShaderResourceView(*tex, nullptr, srv))) return false;
    return true;
}

// en Present, contexto inmediato: genera hasta 2 caras por cuadro
void FaceGenPresent() {
    if (!g_dev.dev || !g_dev.imm) return;
    std::vector<FaceReq> todo;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        if (s_reqs.empty()) return;
        int n = (int)s_reqs.size() < 2 ? (int)s_reqs.size() : 2;
        todo.assign(s_reqs.begin(), s_reqs.begin() + n);
        s_reqs.erase(s_reqs.begin(), s_reqs.begin() + n);
    }
    ID3D11DeviceContext* c = g_dev.imm;
    bool ok = EnsureShaders();
    // --- guardar estado ---
    ID3D11RenderTargetView* oldRT[8] = {}; ID3D11DepthStencilView* oldDS = nullptr;
    c->OMGetRenderTargets(8, oldRT, &oldDS);
    UINT nvp = 1; D3D11_VIEWPORT oldVP[1] = {}; c->RSGetViewports(&nvp, oldVP); if (nvp == 0) nvp = 1;
    ID3D11VertexShader* oldVS = nullptr; ID3D11PixelShader* oldPS = nullptr; ID3D11GeometryShader* oldGS = nullptr; ID3D11HullShader* oldHS = nullptr; ID3D11DomainShader* oldDSh = nullptr;
    c->VSGetShader(&oldVS, nullptr, nullptr); c->PSGetShader(&oldPS, nullptr, nullptr); c->GSGetShader(&oldGS, nullptr, nullptr); c->HSGetShader(&oldHS, nullptr, nullptr); c->DSGetShader(&oldDSh, nullptr, nullptr);
    ID3D11ShaderResourceView* oldSRV[4] = {}; c->PSGetShaderResources(0, 4, oldSRV);
    ID3D11SamplerState* oldSamp = nullptr; c->PSGetSamplers(0, 1, &oldSamp);
    ID3D11BlendState* oldBS = nullptr; FLOAT bf[4]; UINT sm; c->OMGetBlendState(&oldBS, bf, &sm);
    ID3D11DepthStencilState* oldDSS = nullptr; UINT sref; c->OMGetDepthStencilState(&oldDSS, &sref);
    ID3D11RasterizerState* oldRS = nullptr; c->RSGetState(&oldRS);
    ID3D11InputLayout* oldIL = nullptr; c->IAGetInputLayout(&oldIL);
    D3D11_PRIMITIVE_TOPOLOGY oldTopo; c->IAGetPrimitiveTopology(&oldTopo);

    for (auto& r : todo) {
        FaceEntry e;
        if (ok) {
            ID3D11Texture2D *ta = nullptr, *tb = nullptr; ID3D11RenderTargetView *ra = nullptr, *rb = nullptr;
            if (MakeTarget(r.w, r.h, &ta, &ra, &e.a) && MakeTarget(r.w, r.h, &tb, &rb, &e.b)) {
                ID3D11RenderTargetView* rts[2] = { ra, rb };
                c->OMSetRenderTargets(2, rts, nullptr);
                D3D11_VIEWPORT vp{ 0, 0, (float)r.w, (float)r.h, 0, 1 }; c->RSSetViewports(1, &vp);
                c->VSSetShader(s_vs, nullptr, 0); c->PSSetShader(s_ps, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
                c->PSSetShaderResources(0, 4, r.in); c->PSSetSamplers(0, 1, &s_samp);
                c->OMSetBlendState(nullptr, nullptr, 0xffffffff); c->OMSetDepthStencilState(nullptr, 0); c->RSSetState(nullptr);
                c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                c->Draw(3, 0);
                ID3D11RenderTargetView* none[2] = {}; c->OMSetRenderTargets(2, none, nullptr);
                c->GenerateMips(e.a); c->GenerateMips(e.b);
                s_generated++;
                if (s_generated <= 3) Log("facegen: cara nueva %ux%u (%d generadas)", r.w, r.h, s_generated);
            } else { ReleaseEntry(e); e.failed = true; }
            if (ra) ra->Release(); if (rb) rb->Release(); if (ta) ta->Release(); if (tb) tb->Release();
        } else e.failed = true;
        e.pending = false;
        { std::lock_guard<std::mutex> lk(s_mx); auto it = s_cache.find(r.key); if (it != s_cache.end()) { ReleaseEntry(it->second); it->second = e; } else ReleaseEntry(e); }
        for (int i = 0; i < 4; i++) if (r.in[i]) r.in[i]->Release();
    }
    // --- restaurar estado ---
    c->OMSetRenderTargets(8, oldRT, oldDS); for (auto* v : oldRT) if (v) v->Release(); if (oldDS) oldDS->Release();
    c->RSSetViewports(nvp, oldVP);
    c->VSSetShader(oldVS, nullptr, 0); c->PSSetShader(oldPS, nullptr, 0); c->GSSetShader(oldGS, nullptr, 0); c->HSSetShader(oldHS, nullptr, 0); c->DSSetShader(oldDSh, nullptr, 0);
    if (oldVS) oldVS->Release(); if (oldPS) oldPS->Release(); if (oldGS) oldGS->Release(); if (oldHS) oldHS->Release(); if (oldDSh) oldDSh->Release();
    c->PSSetShaderResources(0, 4, oldSRV); for (auto* v : oldSRV) if (v) v->Release();
    c->PSSetSamplers(0, 1, &oldSamp); if (oldSamp) oldSamp->Release();
    c->OMSetBlendState(oldBS, bf, sm); if (oldBS) oldBS->Release();
    c->OMSetDepthStencilState(oldDSS, sref); if (oldDSS) oldDSS->Release();
    c->RSSetState(oldRS); if (oldRS) oldRS->Release();
    c->IASetInputLayout(oldIL); if (oldIL) oldIL->Release();
    c->IASetPrimitiveTopology(oldTopo);
}

} // namespace fx
