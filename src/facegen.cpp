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
#include <algorithm>
#include <cstring>
#include <string>
#include <DDSTextureLoader.h>
#include <ScreenGrab.h>
#include <WICTextureLoader.h>
#include <wincodec.h>
#include <objbase.h>
#include <cstdio>

namespace fx {

struct FaceEntry {
    ID3D11ShaderResourceView* a = nullptr; ID3D11ShaderResourceView* b = nullptr; ID3D11ShaderResourceView* c = nullptr;   // c = semillas de cejas
    ID3D11Buffer* seeds = nullptr; ID3D11UnorderedAccessView* seedsUAV = nullptr; ID3D11ShaderResourceView* seedsSRV = nullptr;
    ID3D11Buffer* args = nullptr; ID3D11UnorderedAccessView* argsUAV = nullptr;   // 18/10: argumentos de DrawInstancedIndirect (cuenta de semillas que escribe la cara)
    bool pending = false; bool failed = false;
    // 18/10 pelo: identidad de la cara (hash del mapa de normales), tipo asignado y sus parametros (b11); mapa propio del usuario
    std::string id, type; ID3D11Buffer* hairCB = nullptr; int hairGen = -1; bool userMap = false; UINT w = 0, h = 0;
};
// cbuffer b11 que leen el shader de cara y los de hebras (6 float4 = 96 bytes)
struct HairCB { float a[4]; float b[4]; float root[4]; float tip[4]; float brow[4]; float misc[4]; };
static std::atomic<int> s_hairGen{0};
void FaceGenHairChanged() { s_hairGen++; }
static HairCB MakeHairCB(const HairType& t, bool userMap) {
    HairCB cb{};
    cb.a[0] = t.largo; cb.a[1] = t.rizo; cb.a[2] = t.rigidez; cb.a[3] = t.peso;
    cb.b[0] = t.grosor; cb.b[1] = t.densidad; cb.b[2] = t.brillo; cb.b[3] = t.flequillo;
    for (int i = 0; i < 3; i++) { cb.root[i] = t.raiz[i] < 0 ? 0 : t.raiz[i]; cb.tip[i] = t.punta[i] < 0 ? 0 : t.punta[i]; }
    cb.root[3] = t.raiz[0] >= 0 ? 1.f : 0.f; cb.tip[3] = t.punta[0] >= 0 ? 1.f : 0.f;
    cb.brow[0] = t.cejaGrosor; cb.brow[1] = t.cejaDens; cb.brow[2] = t.cejaElev; cb.brow[3] = t.cejaLargo;
    cb.misc[0] = 1.f; cb.misc[1] = userMap ? 1.f : 0.f; cb.misc[2] = 0; cb.misc[3] = 0;
    return cb;
}
// identidad estable de una cara: hash FNV-1a de un mip chico (<= 64 px) de su mapa de normales (copia a staging, contexto inmediato)
static std::string HashFaceTexture(ID3D11DeviceContext* c, ID3D11Resource* res) {
    D3D11_RESOURCE_DIMENSION dim; res->GetType(&dim); if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return "";
    ID3D11Texture2D* tex = (ID3D11Texture2D*)res; D3D11_TEXTURE2D_DESC d; tex->GetDesc(&d);
    UINT mip = 0; UINT w = d.Width, h = d.Height;
    while ((w > 64 || h > 64) && mip + 1 < d.MipLevels) { mip++; w = (w + 1) / 2; h = (h + 1) / 2; }
    D3D11_TEXTURE2D_DESC sd = d; sd.Width = w; sd.Height = h; sd.MipLevels = 1; sd.ArraySize = 1; sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
    ID3D11Texture2D* st = nullptr; if (FAILED(g_dev.dev->CreateTexture2D(&sd, nullptr, &st))) return "";
    c->CopySubresourceRegion(st, 0, 0, 0, 0, res, D3D11CalcSubresource(mip, 0, d.MipLevels), nullptr);
    D3D11_MAPPED_SUBRESOURCE m{}; std::string out;
    if (SUCCEEDED(c->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
        bool bc = (d.Format >= DXGI_FORMAT_BC1_TYPELESS && d.Format <= DXGI_FORMAT_BC5_SNORM) || (d.Format >= DXGI_FORMAT_BC6H_TYPELESS && d.Format <= DXGI_FORMAT_BC7_UNORM_SRGB);
        bool bc8 = d.Format <= DXGI_FORMAT_BC1_UNORM_SRGB || (d.Format >= DXGI_FORMAT_BC4_TYPELESS && d.Format <= DXGI_FORMAT_BC4_SNORM);
        UINT rows = bc ? (h + 3) / 4 : h;
        UINT rowBytes = bc ? ((w + 3) / 4) * (bc8 ? 8 : 16) : w * 4;
        if (rowBytes > m.RowPitch) rowBytes = m.RowPitch;
        uint64_t hsh = 1469598103934665603ull;
        for (UINT y = 0; y < rows; y++) { const uint8_t* row = (const uint8_t*)m.pData + (size_t)y * m.RowPitch; for (UINT x = 0; x < rowBytes; x++) { hsh ^= row[x]; hsh *= 1099511628211ull; } }
        c->Unmap(st, 0);
        char buf[32]; snprintf(buf, sizeof(buf), "%012llx", (unsigned long long)(hsh & 0xffffffffffffull)); out = buf;
    }
    st->Release();
    return out;
}
static std::wstring Widen(const std::string& a) { return std::wstring(a.begin(), a.end()); }
static void EnsureCom() { static bool done = false; if (!done) { done = true; CoInitializeEx(nullptr, COINIT_MULTITHREADED); } }   // WIC (png) en el hilo de Present
static bool FileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
// mapa de pelo propio del usuario: PES21FX\hair\<id>.png (o .dds)
static ID3D11ShaderResourceView* LoadUserHairMap(const std::string& id) {
    ID3D11ShaderResourceView* srv = nullptr;
    std::wstring base = g_cfg.baseDir + L"hair\\" + Widen(id);
    if (FileExists(base + L".png")) { EnsureCom(); if (FAILED(DirectX::CreateWICTextureFromFileEx(g_dev.dev, g_dev.imm, (base + L".png").c_str(), 0, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, DirectX::WIC_LOADER_IGNORE_SRGB, nullptr, &srv))) srv = nullptr; }
    if (!srv && FileExists(base + L".dds")) { if (FAILED(DirectX::CreateDDSTextureFromFile(g_dev.dev, (base + L".dds").c_str(), nullptr, &srv))) srv = nullptr; }
    return srv;
}
static const UINT kSeedCount = 65536;   // grilla 256x256 de la quinta textura
static const UINT kSeedBytes = 64;
// 18/10: estadisticas para el log (cada 10 s): consultas, aciertos, sin entradas, pendientes, dibujos de hebras
static std::atomic<int> s_stLook{0}, s_stHit{0}, s_stNoIn{0}, s_stPend{0}, s_stFull{0}, s_stDraw{0};
static double s_stNext = 0;
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

static void ReleaseEntry(FaceEntry& e) {
    if (e.a) e.a->Release(); if (e.b) e.b->Release(); if (e.c) e.c->Release(); e.a = e.b = e.c = nullptr;
    if (e.seedsUAV) e.seedsUAV->Release(); if (e.seedsSRV) e.seedsSRV->Release(); if (e.seeds) e.seeds->Release();
    e.seedsUAV = nullptr; e.seedsSRV = nullptr; e.seeds = nullptr;
    if (e.argsUAV) e.argsUAV->Release(); if (e.args) e.args->Release(); e.argsUAV = nullptr; e.args = nullptr;
    if (e.hairCB) e.hairCB->Release(); e.hairCB = nullptr;
}
// hebras de ceja: shaders propios (se compilan en Present, no en los hilos del juego)
static ID3D11VertexShader* s_browVS = nullptr; static ID3D11PixelShader* s_browPS = nullptr; static bool s_browTried = false; static ID3D11RasterizerState* s_browRS = nullptr;
static void EnsureBrowShaders() {
    if (s_browTried || !g_dev.dev) return;
    s_browTried = true;
    ID3DBlob* vb = CompileNamedShader(L"pes_brow_vs.hlsl", "vs_5_0");
    ID3DBlob* pb = CompileNamedShader(L"pes_brow_ps.hlsl", "ps_5_0");
    if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_browVS); vb->Release(); }
    if (pb) { g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_browPS); pb->Release(); }
    if (!s_browRS) { D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; g_dev.dev->CreateRasterizerState(&rd, &s_browRS); }
    Log("cejas: shaders de hebras %s", (s_browVS && s_browPS) ? "listos" : "NO compilaron (ver errores)");
}
bool BrowReady() { return s_browVS && s_browPS; }

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
    if (s_browVS) { s_browVS->Release(); s_browVS = nullptr; }
    if (s_browPS) { s_browPS->Release(); s_browPS = nullptr; }
    s_browTried = false;
    // lo ya generado se rehace con el shader nuevo
    for (auto& kv : s_cache) ReleaseEntry(kv.second);
    s_cache.clear();
}

// llamado antes de un dibujo con regla facegen (cualquier contexto): devuelve las dos texturas si ya existen
bool FaceGenLookup(ID3D11DeviceContext* c, ID3D11ShaderResourceView** outA, ID3D11ShaderResourceView** outB, FaceGenExtra* ex) {
    *outA = *outB = nullptr; if (ex) { ex->seedMap = nullptr; ex->seedsUAV = nullptr; ex->seedsSRV = nullptr; ex->hairCB = nullptr; }
    ID3D11ShaderResourceView* in[4] = {};
    c->PSGetShaderResources(0, 4, in);
    s_stLook++;
    if (!in[1]) { s_stNoIn++; for (int i = 0; i < 4; i++) if (in[i]) in[i]->Release(); return false; }
    ID3D11Resource* key = nullptr; in[1]->GetResource(&key);
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        auto it = s_cache.find(key);
        if (it != s_cache.end()) {
            if (it->second.a) { s_stHit++; *outA = it->second.a; *outB = it->second.b; found = true;
                FaceEntry& fe = it->second;
                if (ex) { ex->seedMap = fe.c; ex->seedsUAV = fe.seedsUAV; ex->seedsSRV = fe.seedsSRV; ex->args = fe.args; ex->argsUAV = fe.argsUAV; ex->hairCB = fe.hairCB; } }
            else s_stPend++;
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
static std::atomic<int> s_browFaces{0};   // caras con hebras dibujadas en este cuadro (x77 = maximo por pantalla)
void FaceGenPresent() {
    s_browFaces.store(0);
    { double t = Now(); if (t > s_stNext) { s_stNext = t + 10.0;
        if (s_stLook.load() > 0) Log("facegen: 10 s: %d consultas, %d con texturas, %d pendientes, %d sin t1, %d en cache, %d dibujos de hebras", s_stLook.load(), s_stHit.load(), s_stPend.load(), s_stNoIn.load(), (int)s_cache.size(), s_stDraw.load());
        s_stLook = 0; s_stHit = 0; s_stPend = 0; s_stNoIn = 0; s_stDraw = 0; } }
    if (!g_dev.dev || !g_dev.imm) return;
    // PES21FX_hair.ini recargado: tipo y parametros nuevos para cada cara ya generada (aca, en el hilo de Present)
    { static int seenGen = 0; int g = s_hairGen.load();
      if (g != seenGen) { seenGen = g; std::lock_guard<std::mutex> lk(s_mx);
        for (auto& kv : s_cache) { FaceEntry& fe = kv.second; if (!fe.hairCB) continue;
            const HairType* ht = HairTypeFor(fe.id, &fe.type);
            if (ht) { HairCB cb = MakeHairCB(*ht, fe.userMap); g_dev.imm->UpdateSubresource(fe.hairCB, 0, nullptr, &cb, 0, 0); fe.hairGen = g; } } } }
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
    EnsureBrowShaders();
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
            ID3D11Texture2D *ta = nullptr, *tb = nullptr, *tc = nullptr; ID3D11RenderTargetView *ra = nullptr, *rb = nullptr, *rc = nullptr;
            if (MakeTarget(r.w, r.h, &ta, &ra, &e.a) && MakeTarget(r.w, r.h, &tb, &rb, &e.b) && MakeTarget(r.w, r.h, &tc, &rc, &e.c)) {
                // buffer de semillas de cejas (lo escribe el shader de cara en cada dibujo, lo lee el dibujo de hebras)
                D3D11_BUFFER_DESC bd{}; bd.ByteWidth = kSeedCount * kSeedBytes; bd.Usage = D3D11_USAGE_DEFAULT;
                bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = kSeedBytes;
                if (SUCCEEDED(g_dev.dev->CreateBuffer(&bd, nullptr, &e.seeds))) {
                    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_UNKNOWN; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = kSeedCount;
                    g_dev.dev->CreateUnorderedAccessView(e.seeds, &ud, &e.seedsUAV);
                    D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = DXGI_FORMAT_UNKNOWN; sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Buffer.NumElements = kSeedCount;
                    g_dev.dev->CreateShaderResourceView(e.seeds, &sd, &e.seedsSRV);
                }
                // argumentos del dibujo indirecto: {vertices por instancia, instancias, 0, 0}; la cara los escribe (contador de semillas)
                { D3D11_BUFFER_DESC ad{}; ad.ByteWidth = 16; ad.Usage = D3D11_USAGE_DEFAULT; ad.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
                  ad.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
                  if (SUCCEEDED(g_dev.dev->CreateBuffer(&ad, nullptr, &e.args))) {
                      D3D11_UNORDERED_ACCESS_VIEW_DESC au{}; au.Format = DXGI_FORMAT_R32_TYPELESS; au.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; au.Buffer.NumElements = 4; au.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
                      g_dev.dev->CreateUnorderedAccessView(e.args, &au, &e.argsUAV);
                  } }
                ID3D11RenderTargetView* rts[3] = { ra, rb, rc };
                c->OMSetRenderTargets(3, rts, nullptr);
                D3D11_VIEWPORT vp{ 0, 0, (float)r.w, (float)r.h, 0, 1 }; c->RSSetViewports(1, &vp);
                c->VSSetShader(s_vs, nullptr, 0); c->PSSetShader(s_ps, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
                c->PSSetShaderResources(0, 4, r.in); c->PSSetSamplers(0, 1, &s_samp);
                c->OMSetBlendState(nullptr, nullptr, 0xffffffff); c->OMSetDepthStencilState(nullptr, 0); c->RSSetState(nullptr);
                c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                c->Draw(3, 0);
                ID3D11RenderTargetView* none[3] = {}; c->OMSetRenderTargets(3, none, nullptr);
                c->GenerateMips(e.a); c->GenerateMips(e.b); c->GenerateMips(e.c);
                s_generated++;
                e.w = r.w; e.h = r.h;
                // 18/10 pelo: identidad de la cara, tipo de pelo (PES21FX_hair.ini) y mapa de pelo propio (PES21FX\hair\<id>.dds)
                { ID3D11Resource* res = nullptr; r.in[1]->GetResource(&res);
                  if (res) { e.id = HashFaceTexture(c, res); res->Release(); } }
                if (!e.id.empty()) {
                    ID3D11ShaderResourceView* usrv = LoadUserHairMap(e.id);
                    if (usrv) { if (e.c) e.c->Release(); e.c = usrv; e.userMap = true; }
                }
                const HairType* ht = HairTypeFor(e.id, &e.type);
                if (ht) {
                    HairCB cb = MakeHairCB(*ht, e.userMap);
                    D3D11_BUFFER_DESC cd{}; cd.ByteWidth = sizeof(HairCB); cd.Usage = D3D11_USAGE_DEFAULT; cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                    D3D11_SUBRESOURCE_DATA init{ &cb, 0, 0 };
                    g_dev.dev->CreateBuffer(&cd, &init, &e.hairCB); e.hairGen = s_hairGen.load();
                }
                Log("pelo: cara %s (%ux%u) -> tipo '%s'%s", e.id.empty() ? "?" : e.id.c_str(), r.w, r.h, e.type.c_str(), e.userMap ? ", mapa propio hair\\<id>.png" : "");
            } else { ReleaseEntry(e); e.failed = true; }
            if (ra) ra->Release(); if (rb) rb->Release(); if (rc) rc->Release(); if (ta) ta->Release(); if (tb) tb->Release(); if (tc) tc->Release();
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


// 18/10 pelo: "Volcar mapas de pelo": escribe hair\auto\<id>.png (el mapa de pelo automatico de cada cara generada, listo para
// editar y copiar a hair\<id>.png) y hair\auto\lista.txt (id, tamano, tipo). Se llama desde el menu (Present, contexto inmediato).
int FaceGenExportHair(std::string& status) {
    if (!g_dev.imm) { status = "Sin dispositivo"; return 0; }
    std::wstring dir = g_cfg.baseDir + L"hair"; CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\auto"; CreateDirectoryW(dir.c_str(), nullptr);
    int n = 0, fail = 0; std::string lista = "; id de cara ; tamano ; tipo asignado ; mapa propio\n";
    std::lock_guard<std::mutex> lk(s_mx);
    for (auto& kv : s_cache) {
        FaceEntry& e = kv.second; if (!e.c || e.id.empty()) continue;
        ID3D11Resource* res = nullptr; e.c->GetResource(&res); if (!res) continue;
        std::wstring path = dir + L"\\" + Widen(e.id) + L".png";
        EnsureCom();
        HRESULT hr = DirectX::SaveWICTextureToFile(g_dev.imm, res, GUID_ContainerFormatPng, path.c_str(), nullptr, nullptr, false);
        res->Release();
        if (SUCCEEDED(hr)) n++; else fail++;
        char line[256]; snprintf(line, sizeof(line), "%s ; %ux%u ; %s ; %s\n", e.id.c_str(), e.w, e.h, e.type.c_str(), e.userMap ? "si" : "no"); lista += line;
    }
    { FILE* f = _wfopen((dir + L"\\lista.txt").c_str(), L"wb"); if (f) { fwrite(lista.data(), 1, lista.size(), f); fclose(f); } }
    char buf[160]; snprintf(buf, sizeof(buf), "%d mapas de pelo en PES21FX\\hair\\auto (%d fallaron)", n, fail); status = buf;
    Log("pelo: %s", buf);
    return n;
}

// --- hebras de ceja: se dibujan justo despues de la cara, en el mismo contexto y con los mismos render targets ---
// (el G-buffer sigue enlazado). Se guarda y restaura el estado que se toca.
void BrowDrawAfterFace(ID3D11DeviceContext* c, FaceGenExtra* ex) {
    if (!ex || !ex->seedsUAV) return;
    // 1) soltar los UAV del shader de cara (semillas y argumentos)
    ID3D11UnorderedAccessView* nullUav[2] = { nullptr, nullptr };
    c->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 5, 2, nullUav, nullptr);
    if (!s_browVS || !s_browPS || !s_browRS || !ex->seedsSRV) return;
    float on = g_cfg.live[74][0]; if (on < 0.5f) return;
    UINT perSeed = (UINT)std::max(1.0f, std::min(3.0f, g_cfg.live[74][1]));
    int maxFaces = (int)g_cfg.live[77][0]; if (maxFaces < 1) maxFaces = 22;
    if (s_browFaces.fetch_add(1) >= maxFaces) return;   // solo las primeras N caras del cuadro (rendimiento)
    // 2) guardar estado
    ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr; ID3D11HullShader* oHS = nullptr; ID3D11DomainShader* oDS = nullptr;
    c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr); c->HSGetShader(&oHS, nullptr, nullptr); c->DSGetShader(&oDS, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL);
    D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11ShaderResourceView* oVS30 = nullptr; c->VSGetShaderResources(30, 1, &oVS30);
    ID3D11ShaderResourceView* oVS120 = nullptr; c->VSGetShaderResources(120, 1, &oVS120);
    ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS);
    ID3D11Buffer* oVScb = nullptr; c->VSGetConstantBuffers(11, 1, &oVScb);
    ID3D11Buffer* oPScb = nullptr; c->PSGetConstantBuffers(11, 1, &oPScb);
    // profundidad: la cara se dibuja con "igual" (hay pre-pasada de profundidad) y eso descartaria todas las hebras.
    // Se usa el mismo estado pero con comparacion "menor o igual" (o "mayor o igual" si el juego usa profundidad invertida).
    ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDSS, &oRef);
    ID3D11DepthStencilState* useDSS = nullptr;
    D3D11_DEPTH_STENCIL_DESC dd{};
    if (oDSS) oDSS->GetDesc(&dd); else { dd.DepthEnable = TRUE; dd.StencilEnable = FALSE; }
    D3D11_COMPARISON_FUNC origFunc = dd.DepthFunc;
    // la direccion de la profundidad se toma de la propia cara (mayor = invertida); solo si la cara usa "igual"
    // (pre-pasada) se recurre al conteo de limpiezas. Antes se usaba siempre el conteo y, como el juego limpia mas
    // shadow maps (a 1) que buffers principales (a 0), al segundo cambiaba de signo y las hebras desaparecian.
    bool reversed;
    if (origFunc == D3D11_COMPARISON_GREATER || origFunc == D3D11_COMPARISON_GREATER_EQUAL) reversed = true;
    else if (origFunc == D3D11_COMPARISON_LESS || origFunc == D3D11_COMPARISON_LESS_EQUAL) reversed = false;
    else reversed = g_depthClearZero.load() > g_depthClearOne.load();
    dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = reversed ? D3D11_COMPARISON_GREATER_EQUAL : D3D11_COMPARISON_LESS_EQUAL;
    {
        static ID3D11DepthStencilState* cacheDSS[2] = {}; static D3D11_DEPTH_STENCIL_DESC cacheDesc[2] = {}; static bool cacheOk[2] = {};
        int ci = reversed ? 1 : 0;
        if (!cacheOk[ci] || memcmp(&cacheDesc[ci], &dd, sizeof(dd)) != 0) {
            if (cacheDSS[ci]) { cacheDSS[ci]->Release(); cacheDSS[ci] = nullptr; }
            g_dev.dev->CreateDepthStencilState(&dd, &cacheDSS[ci]); cacheDesc[ci] = dd; cacheOk[ci] = true;
        }
        useDSS = cacheDSS[ci];
    }
    if (useDSS) c->OMSetDepthStencilState(useDSS, oRef);
    static std::atomic<int> s_logged{0};
    if (s_logged.load() < 1) { s_logged++; Log("cejas: primer dibujo de hebras (RTs %d, profundidad del juego func %d, %s, %u semillas x %u)", ex->rtCount, (int)origFunc, reversed ? "invertida" : "normal", (unsigned)kSeedCount, perSeed); }
    // 3) dibujar
    c->VSSetShader(s_browVS, nullptr, 0); c->PSSetShader(s_browPS, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShaderResources(30, 1, &ex->seedsSRV);
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->VSSetShaderResources(120, 1, &prm); c->PSSetShaderResources(120, 1, &prm);
    c->RSSetState(s_browRS);   // sin culling (cintas vistas de ambos lados)
    if (ex->hairCB) { c->VSSetConstantBuffers(11, 1, &ex->hairCB); c->PSSetConstantBuffers(11, 1, &ex->hairCB); }
    // 18/10: dibujo indirecto: tantas instancias como semillas escribio ESTA cara en este cuadro (las caras lejanas casi
    // no escriben semillas y cuestan nada). En la "prueba de dibujo" (z75 = 3) se dibuja una grilla fija.
    s_stDraw++;
    if (g_cfg.live[75][2] > 2.5f || !ex->args) c->DrawInstanced(6 * 4, 4096 * perSeed, 0, 0);
    else c->DrawInstancedIndirect(ex->args, 0);
    // 4) restaurar
    c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0); c->HSSetShader(oHS, nullptr, 0); c->DSSetShader(oDS, nullptr, 0);
    if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release(); if (oHS) oHS->Release(); if (oDS) oDS->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release();
    c->IASetPrimitiveTopology(oTopo);
    c->VSSetShaderResources(30, 1, &oVS30); if (oVS30) oVS30->Release();
    c->VSSetShaderResources(120, 1, &oVS120); if (oVS120) oVS120->Release();
    c->RSSetState(oRS); if (oRS) oRS->Release();
    c->VSSetConstantBuffers(11, 1, &oVScb); if (oVScb) oVScb->Release();
    c->PSSetConstantBuffers(11, 1, &oPScb); if (oPScb) oPScb->Release();
    c->OMSetDepthStencilState(oDSS, oRef); if (oDSS) oDSS->Release();
}

} // namespace fx
