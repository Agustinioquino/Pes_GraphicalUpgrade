// PES21 FX - ganchos de D3D11: reemplazo de shaders por hash, parametros en t120, reglas por shader y Present.
#include "core.h"
#include <MinHook.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <DDSTextureLoader.h>
#include <algorithm>
#include <set>
#include <cwctype>

namespace fx {

DeviceState g_dev;
std::atomic<int> g_replacedCount{0}, g_failedCount{0};
std::vector<std::string> g_compileErrors;
std::mutex g_errMutex;

// ---------------------------------------------------------------- hash igual al de 3DMigoto (FNV-1 de 64 bits, base cero)
static uint64_t Fnv64(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data; uint64_t h = 0;
    for (size_t i = 0; i < len; i++) { h *= 0x100000001b3ULL; h ^= p[i]; }
    return h;
}

// ---------------------------------------------------------------- compilador
using PFN_D3DCompile = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
using PFN_D3DCreateBlob = HRESULT(WINAPI*)(SIZE_T, ID3DBlob**);
static PFN_D3DCompile s_compile = nullptr;
static PFN_D3DCreateBlob s_createBlob = nullptr;

std::wstring ShaderPath(uint64_t hash, const char* type) {
    wchar_t b[64]; swprintf(b, 64, L"%016llx-%hs_replace.txt", (unsigned long long)hash, type);
    return g_cfg.baseDir + L"shaders\\" + b;
}

static std::set<std::pair<uint64_t, char>> s_available;   // shaders que tenemos en la carpeta

static void ScanShaderFolder() {
    s_available.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_cfg.baseDir + L"shaders\\*_replace.txt").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { Log("no hay shaders en la carpeta shaders"); return; }
    do {
        std::wstring n = fd.cFileName;
        if (n.size() < 20 || n[16] != L'-') continue;
        uint64_t hash = wcstoull(n.substr(0, 16).c_str(), nullptr, 16);
        wchar_t t = n[17];   // 'p' / 'v' / 'c'
        s_available.insert({hash, (char)t});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    Log("shaders propios en la carpeta: %d", (int)s_available.size());
}

static bool ReadFileA(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(h, nullptr), rd = 0;
    out.resize(sz);
    BOOL ok = sz ? ReadFile(h, out.data(), sz, &rd, nullptr) : TRUE;
    CloseHandle(h);
    return ok && rd == sz;
}

// cache de shaders compilados: shaders\cache\<hash>-<tipo>_<hash del fuente>.cso (si cambias el .txt se recompila solo)
static std::wstring CachePath(uint64_t hash, const char* type, uint64_t srcHash) {
    wchar_t b[96]; swprintf(b, 96, L"%016llx-%hs_%016llx.cso", (unsigned long long)hash, type, (unsigned long long)srcHash);
    return g_cfg.baseDir + L"shaders\\cache\\" + b;
}

static ID3DBlob* CompileFile(uint64_t hash, const char* type, const char* profile) {
    if (!s_compile) return nullptr;
    std::string src;
    std::wstring path = ShaderPath(hash, type);
    if (!ReadFileA(path, src)) return nullptr;
    uint64_t srcHash = Fnv64(src.data(), src.size()) ^ 0x5045533231465831ULL;
    std::wstring cpath = CachePath(hash, type, srcHash);
    {
        std::string bin;
        if (s_createBlob && ReadFileA(cpath, bin) && !bin.empty()) {
            ID3DBlob* b = nullptr;
            if (SUCCEEDED(s_createBlob(bin.size(), &b)) && b) { memcpy(b->GetBufferPointer(), bin.data(), bin.size()); return b; }
        }
    }
    ID3DBlob *code = nullptr, *err = nullptr;
    char name[64]; snprintf(name, sizeof(name), "%016llx-%s_replace.txt", (unsigned long long)hash, type);
    HRESULT hr = s_compile(src.data(), src.size(), name, nullptr, nullptr, "main", profile,
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) {
        std::string e = err ? std::string((const char*)err->GetBufferPointer(), err->GetBufferSize()) : "error desconocido";
        Log("ERROR compilando %s:\n%s", name, e.c_str());
        { std::lock_guard<std::mutex> lk(g_errMutex); g_compileErrors.push_back(std::string(name) + ": " + e.substr(0, 300)); }
        if (err) err->Release();
        if (code) code->Release();
        g_failedCount++;
        return nullptr;
    }
    if (err) err->Release();
    CreateDirectoryW((g_cfg.baseDir + L"shaders\\cache").c_str(), nullptr);
    HANDLE h = CreateFileW(cpath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(h, code->GetBufferPointer(), (DWORD)code->GetBufferSize(), &wr, nullptr); CloseHandle(h); }
    return code;
}

// ---------------------------------------------------------------- tabla de shaders creados por el juego
struct ShaderEntry {
    uint64_t hash = 0;
    char type = 0;                       // 'p' 'v' 'c'
    ID3D11DeviceChild* repl = nullptr;   // nuestro shader (o null)
    const Rule* rule = nullptr;
    ID3D11Device* dev = nullptr;         // dispositivo que lo creo
};
static std::unordered_map<void*, ShaderEntry> s_shaders;   // nodos estables: los punteros a ShaderEntry no se invalidan
static std::shared_mutex s_shMutex;
static std::vector<IUnknown*> s_deferredRelease;
static std::mutex s_releaseMutex;

static const ShaderEntry* FindShader(void* p) {
    if (!p) return nullptr;
    std::shared_lock<std::shared_mutex> lk(s_shMutex);
    auto it = s_shaders.find(p);
    return it == s_shaders.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------- recursos con nombre (copias de cbuffers, referencias, texturas)
struct NamedRes { ID3D11Buffer* buf = nullptr; ID3D11ShaderResourceView* srv = nullptr; bool failed = false; };
static std::map<std::string, NamedRes> s_named;
static std::mutex s_namedMutex;

static ID3D11ShaderResourceView* FileTexture(const std::string& rel) {
    // rel = "file:textures\\algo.dds" -> se carga una sola vez
    std::lock_guard<std::mutex> lk(s_namedMutex);
    auto& r = s_named[rel];
    if (r.srv || r.failed || !g_dev.dev) return r.srv;
    std::string p = rel.substr(5);
    int n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    std::wstring wp(n ? n - 1 : 0, L'\0'); if (n) MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, wp.data(), n);
    HRESULT hr = DirectX::CreateDDSTextureFromFile(g_dev.dev, (g_cfg.baseDir + wp).c_str(), nullptr, &r.srv);
    Log("textura %s: %s (0x%08x)", p.c_str(), SUCCEEDED(hr) ? "ok" : "NO SE PUDO CARGAR", (unsigned)hr);
    if (FAILED(hr)) { r.srv = nullptr; r.failed = true; }
    return r.srv;
}

static void ReleaseNamed() {
    std::lock_guard<std::mutex> lk(s_namedMutex);
    for (auto& kv : s_named) { if (kv.second.buf) kv.second.buf->Release(); if (kv.second.srv) kv.second.srv->Release(); }
    s_named.clear();
}

// ---------------------------------------------------------------- texturas de parametros (t120)
static bool CreateParamTex(ID3D11Texture1D** tex, ID3D11ShaderResourceView** srv) {
    if (!g_dev.dev) return false;
    D3D11_TEXTURE1D_DESC d{}; d.Width = kParamCount; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; d.Usage = D3D11_USAGE_DYNAMIC;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_dev.dev->CreateTexture1D(&d, nullptr, tex))) return false;
    return SUCCEEDED(g_dev.dev->CreateShaderResourceView(*tex, nullptr, srv));
}

static void UploadParams() {
    if (!g_dev.imm || !g_dev.paramTex) return;
    static float p[kParamCount][4], q[kParamCount][4];
    struct Ov { int set; int idx, comp; float v; };
    std::vector<Ov> ovs;
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        memcpy(p, g_cfg.live, sizeof(p));
        for (auto& kv : g_cfg.rules) {
            if (kv.second.paramSet < 0) continue;
            for (auto& a : kv.second.actions) if (a.kind == RuleAction::SetParam) {
                float v = a.value;
                if (!a.var.empty()) { auto it = g_cfg.vars.find(a.var); v = it == g_cfg.vars.end() ? 0.f : it->second; }
                ovs.push_back({kv.second.paramSet, a.idx, a.comp, v});
            }
        }
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(g_dev.imm->Map(g_dev.paramTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, p, sizeof(p)); g_dev.imm->Unmap(g_dev.paramTex, 0); }
    int maxSet = -1; for (auto& o : ovs) maxSet = (std::max)(maxSet, o.set);
    for (int s = 0; s <= maxSet; s++) {
        while ((int)g_dev.altTex.size() <= s) {
            ID3D11Texture1D* t = nullptr; ID3D11ShaderResourceView* v = nullptr;
            if (!CreateParamTex(&t, &v)) return;
            g_dev.altTex.push_back(t); g_dev.altSRV.push_back(v);
        }
        memcpy(q, p, sizeof(q));
        for (auto& o : ovs) if (o.set == s) q[o.idx][o.comp] = o.v;
        if (SUCCEEDED(g_dev.imm->Map(g_dev.altTex[s], 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, q, sizeof(q)); g_dev.imm->Unmap(g_dev.altTex[s], 0); }
    }
}

// ---------------------------------------------------------------- estado por contexto
struct CtxState { const ShaderEntry* ps = nullptr; const ShaderEntry* vs = nullptr; ID3D11Device* dev = nullptr; bool devKnown = false; };
static std::unordered_map<ID3D11DeviceContext*, CtxState> s_ctx;
static std::shared_mutex s_ctxMutex;
static CtxState& Ctx(ID3D11DeviceContext* c) {
    { std::shared_lock<std::shared_mutex> lk(s_ctxMutex); auto it = s_ctx.find(c); if (it != s_ctx.end()) return it->second; }
    std::unique_lock<std::shared_mutex> lk(s_ctxMutex); return s_ctx[c];
}

// ---------------------------------------------------------------- originales
using PFN_CreateVS = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**);
using PFN_CreatePS = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);
using PFN_CreateCS = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11ComputeShader**);
using PFN_CreateDC = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, UINT, ID3D11DeviceContext**);
using PFN_SetPS = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT);
using PFN_SetVS = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11VertexShader*, ID3D11ClassInstance* const*, UINT);
using PFN_SetCS = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11ComputeShader*, ID3D11ClassInstance* const*, UINT);
using PFN_DrawIdx = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using PFN_Draw = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using PFN_DrawIdxInst = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
using PFN_DrawInst = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
using PFN_Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using PFN_Resize = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using PFN_D3D11CreateDevice = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using PFN_D3D11CreateDeviceAndSwapChain = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

static PFN_CreateVS o_CreateVS; static PFN_CreatePS o_CreatePS; static PFN_CreateCS o_CreateCS; static PFN_CreateDC o_CreateDC;
static PFN_SetPS o_SetPS[2]; static PFN_SetVS o_SetVS[2]; static PFN_SetCS o_SetCS[2];
static PFN_DrawIdx o_DrawIdx[2]; static PFN_Draw o_Draw[2]; static PFN_DrawIdxInst o_DrawIdxInst[2]; static PFN_DrawInst o_DrawInst[2];
static PFN_Present o_Present; static PFN_Resize o_Resize;
static PFN_D3D11CreateDevice o_D3D11CreateDevice; static PFN_D3D11CreateDeviceAndSwapChain o_D3D11CreateDeviceAndSwapChain;

// ---------------------------------------------------------------- registro de shaders
static const Rule* RuleFor(uint64_t h) {
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    auto it = g_cfg.rules.find(h);
    return it == g_cfg.rules.end() ? nullptr : &it->second;
}

static ID3D11DeviceChild* BuildReplacement(ID3D11Device* dev, uint64_t h, char type) {
    if (!dev || !g_cfg.optReplace || !s_available.count({h, type})) return nullptr;
    const char* t = type == 'p' ? "ps" : type == 'v' ? "vs" : "cs";
    const char* prof = type == 'p' ? "ps_5_0" : type == 'v' ? "vs_5_0" : "cs_5_0";
    ID3DBlob* b = CompileFile(h, t, prof);
    if (!b) return nullptr;
    ID3D11DeviceChild* out = nullptr; HRESULT hr = E_FAIL;
    if (type == 'p') hr = o_CreatePS(dev, b->GetBufferPointer(), b->GetBufferSize(), nullptr, (ID3D11PixelShader**)&out);
    else if (type == 'v') hr = o_CreateVS(dev, b->GetBufferPointer(), b->GetBufferSize(), nullptr, (ID3D11VertexShader**)&out);
    else hr = o_CreateCS(dev, b->GetBufferPointer(), b->GetBufferSize(), nullptr, (ID3D11ComputeShader**)&out);
    b->Release();
    if (FAILED(hr)) { Log("ERROR creando el shader %016llx-%s (0x%08x)", (unsigned long long)h, t, (unsigned)hr); return nullptr; }
    g_replacedCount++;
    Log("shader propio activo: %016llx-%s", (unsigned long long)h, t);
    return out;
}

static void Register(ID3D11Device* dev, void* obj, const void* code, SIZE_T len, char type) {
    if (!obj || !code) return;
    ShaderEntry e; e.hash = Fnv64(code, len); e.type = type; e.dev = dev;
    e.repl = BuildReplacement(dev, e.hash, type);
    e.rule = RuleFor(e.hash);
    std::unique_lock<std::shared_mutex> lk(s_shMutex);
    auto it = s_shaders.find(obj);
    if (it != s_shaders.end()) {   // el juego libero un shader y la direccion se reutilizo
        if (it->second.repl) { std::lock_guard<std::mutex> lk2(s_releaseMutex); s_deferredRelease.push_back(it->second.repl); }
        it->second = e;
    } else s_shaders.emplace(obj, e);
}

static HRESULT STDMETHODCALLTYPE hk_CreateVS(ID3D11Device* d, const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11VertexShader** o) {
    HRESULT hr = o_CreateVS(d, c, n, l, o); if (SUCCEEDED(hr) && o) Register(d, *o, c, n, 'v'); return hr; }
static HRESULT STDMETHODCALLTYPE hk_CreatePS(ID3D11Device* d, const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11PixelShader** o) {
    HRESULT hr = o_CreatePS(d, c, n, l, o); if (SUCCEEDED(hr) && o) Register(d, *o, c, n, 'p'); return hr; }
static HRESULT STDMETHODCALLTYPE hk_CreateCS(ID3D11Device* d, const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11ComputeShader** o) {
    HRESULT hr = o_CreateCS(d, c, n, l, o); if (SUCCEEDED(hr) && o) Register(d, *o, c, n, 'c'); return hr; }

static void HookContext(ID3D11DeviceContext* c, int v);
static HRESULT STDMETHODCALLTYPE hk_CreateDC(ID3D11Device* d, UINT f, ID3D11DeviceContext** o) {
    HRESULT hr = o_CreateDC(d, f, o); if (SUCCEEDED(hr) && o && *o) HookContext(*o, 1); return hr; }

// ---------------------------------------------------------------- bind de shaders
static inline ID3D11ShaderResourceView* ParamSRVFor(const ShaderEntry* e) {
    if (e && e->rule && e->rule->paramSet >= 0 && e->rule->paramSet < (int)g_dev.altSRV.size()) return g_dev.altSRV[e->rule->paramSet];
    return g_dev.paramSRV;
}

// los parametros (t120) se enlazan justo antes de cada dibujo (BeforeDraw), igual que 3DMigoto,
// asi no se pierden si el juego limpia todos los slots entre pasadas
template<int V> static void STDMETHODCALLTYPE hk_SetPS(ID3D11DeviceContext* c, ID3D11PixelShader* s, ID3D11ClassInstance* const* ci, UINT n) {
    const ShaderEntry* e = FindShader(s);
    Ctx(c).ps = e;
    if (e && e->repl && !g_cfg.bypass) { o_SetPS[V](c, (ID3D11PixelShader*)e->repl, ci, n); return; }
    o_SetPS[V](c, s, ci, n);
}
template<int V> static void STDMETHODCALLTYPE hk_SetVS(ID3D11DeviceContext* c, ID3D11VertexShader* s, ID3D11ClassInstance* const* ci, UINT n) {
    const ShaderEntry* e = FindShader(s);
    Ctx(c).vs = e;
    if (e && e->repl && !g_cfg.bypass) { o_SetVS[V](c, (ID3D11VertexShader*)e->repl, ci, n); return; }
    o_SetVS[V](c, s, ci, n);
}
template<int V> static void STDMETHODCALLTYPE hk_SetCS(ID3D11DeviceContext* c, ID3D11ComputeShader* s, ID3D11ClassInstance* const* ci, UINT n) {
    const ShaderEntry* e = FindShader(s);
    if (e && e->repl && !g_cfg.bypass) {
        o_SetCS[V](c, (ID3D11ComputeShader*)e->repl, ci, n);
        if (e->dev == g_dev.dev && g_dev.paramSRV) { ID3D11ShaderResourceView* v = g_dev.paramSRV; c->CSSetShaderResources(120, 1, &v); }
        return;
    }
    o_SetCS[V](c, s, ci, n);
}

// ---------------------------------------------------------------- reglas antes de cada dibujo
// lo que una regla enlaza se devuelve a como estaba despues del dibujo (como "post ps-t20 = ..." en 3DMigoto)
struct Saved { bool ps; bool srv; int slot; IUnknown* prev; };
struct DrawScope { Saved s[12]; int n = 0; };

static void Remember(ID3D11DeviceContext* c, DrawScope& d, bool ps, bool srv, int slot) {
    if (d.n >= 12) return;
    Saved& s = d.s[d.n++]; s.ps = ps; s.srv = srv; s.slot = slot; s.prev = nullptr;
    if (srv) { ID3D11ShaderResourceView* v = nullptr; if (ps) c->PSGetShaderResources(slot, 1, &v); else c->VSGetShaderResources(slot, 1, &v); s.prev = v; }
    else { ID3D11Buffer* b = nullptr; if (ps) c->PSGetConstantBuffers(slot, 1, &b); else c->VSGetConstantBuffers(slot, 1, &b); s.prev = b; }
}
static void Restore(ID3D11DeviceContext* c, DrawScope& d) {
    for (int i = d.n - 1; i >= 0; i--) {
        Saved& s = d.s[i];
        if (s.srv) { auto* v = (ID3D11ShaderResourceView*)s.prev; if (s.ps) c->PSSetShaderResources(s.slot, 1, &v); else c->VSSetShaderResources(s.slot, 1, &v); }
        else { auto* b = (ID3D11Buffer*)s.prev; if (s.ps) c->PSSetConstantBuffers(s.slot, 1, &b); else c->VSSetConstantBuffers(s.slot, 1, &b); }
        if (s.prev) s.prev->Release();
    }
    d.n = 0;
}

static void RunRule(ID3D11DeviceContext* c, const ShaderEntry* e, DrawScope& d) {
    const bool ps = e->type == 'p';
    for (auto& a : e->rule->actions) {
        switch (a.kind) {
        case RuleAction::CopyCB: {
            ID3D11Buffer* src = nullptr;
            if (ps) c->PSGetConstantBuffers(a.slot, 1, &src); else c->VSGetConstantBuffers(a.slot, 1, &src);
            if (!src) break;
            std::lock_guard<std::mutex> lk(s_namedMutex);
            auto& r = s_named[a.name];
            D3D11_BUFFER_DESC sd; src->GetDesc(&sd);
            if (r.buf) { D3D11_BUFFER_DESC dd; r.buf->GetDesc(&dd); if (dd.ByteWidth != sd.ByteWidth) { r.buf->Release(); r.buf = nullptr; } }
            if (!r.buf && g_dev.dev) {
                sd.Usage = D3D11_USAGE_DEFAULT; sd.CPUAccessFlags = 0; sd.MiscFlags = 0; sd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; sd.StructureByteStride = 0;
                g_dev.dev->CreateBuffer(&sd, nullptr, &r.buf);
            }
            if (r.buf) c->CopyResource(r.buf, src);
            src->Release();
            break; }
        case RuleAction::RefSRV: {
            ID3D11ShaderResourceView* v = nullptr;
            if (ps) c->PSGetShaderResources(a.slot, 1, &v); else c->VSGetShaderResources(a.slot, 1, &v);
            if (!v) break;
            std::lock_guard<std::mutex> lk(s_namedMutex);
            auto& r = s_named[a.name];
            if (r.srv != v) { if (r.srv) r.srv->Release(); r.srv = v; } else v->Release();
            break; }
        case RuleAction::BindCB: {
            ID3D11Buffer* b = nullptr;
            { std::lock_guard<std::mutex> lk(s_namedMutex); auto it = s_named.find(a.name); if (it != s_named.end()) b = it->second.buf; }
            if (b) { Remember(c, d, ps, false, a.slot); if (ps) c->PSSetConstantBuffers(a.slot, 1, &b); else c->VSSetConstantBuffers(a.slot, 1, &b); }
            break; }
        case RuleAction::BindSRV: {
            ID3D11ShaderResourceView* v = nullptr;
            if (a.name.rfind("file:", 0) == 0) v = FileTexture(a.name);
            else { std::lock_guard<std::mutex> lk(s_namedMutex); auto it = s_named.find(a.name); if (it != s_named.end()) v = it->second.srv; }
            if (v) { Remember(c, d, ps, true, a.slot); if (ps) c->PSSetShaderResources(a.slot, 1, &v); else c->VSSetShaderResources(a.slot, 1, &v); }
            break; }
        case RuleAction::EventTurf: OnTurfDrawn(); break;
        case RuleAction::SetParam: break;   // ya resuelto con la textura de parametros alternativa
        }
    }
}

static std::atomic<bool> s_firstRule{false};
static inline bool BeforeDraw(ID3D11DeviceContext* c, DrawScope& d) {
    if (g_cfg.bypass || !g_dev.dev) return false;
    CtxState& s = Ctx(c);
    if (!s.devKnown) {   // nuestros recursos (t120, copias) son del dispositivo del juego: nunca mezclar con otro
        ID3D11Device* cd = nullptr; c->GetDevice(&cd);
        s.dev = cd; if (cd) cd->Release();
        s.devKnown = true;
    }
    if (s.dev != g_dev.dev) return false;
    const ShaderEntry *ps = s.ps, *vs = s.vs;
    if (!ps && !vs) return false;
    bool pr = ps && ps->repl, vr = vs && vs->repl;
    if (pr || vr) {
        // si el PS tiene parametros propios (p.ej. w5 = $pes_push) los ven el PS y el VS de ese dibujo
        ID3D11ShaderResourceView* v = (ps && ps->rule && ps->rule->paramSet >= 0) ? ParamSRVFor(ps) : ParamSRVFor(vs);
        if (v) {
            if (pr) c->PSSetShaderResources(120, 1, &v);
            if (vr) c->VSSetShaderResources(120, 1, &v);
        }
    }
    if (!g_cfg.optRules) return false;
    if ((ps && ps->rule) || (vs && vs->rule)) {
        if (!s_firstRule.exchange(true)) Log("primer dibujo con regla");
    }
    if (ps && ps->rule) RunRule(c, ps, d);
    if (vs && vs->rule) RunRule(c, vs, d);
    return d.n > 0;
}
#define FX_DRAW(call) DrawScope ds; bool r = BeforeDraw(c, ds); call; if (r) Restore(c, ds);
template<int V> static void STDMETHODCALLTYPE hk_DrawIdx(ID3D11DeviceContext* c, UINT a, UINT b, INT d) { FX_DRAW(o_DrawIdx[V](c, a, b, d)) }
template<int V> static void STDMETHODCALLTYPE hk_Draw(ID3D11DeviceContext* c, UINT a, UINT b) { FX_DRAW(o_Draw[V](c, a, b)) }
template<int V> static void STDMETHODCALLTYPE hk_DrawIdxInst(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, INT e, UINT f) { FX_DRAW(o_DrawIdxInst[V](c, a, b, d, e, f)) }
template<int V> static void STDMETHODCALLTYPE hk_DrawInst(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, UINT e) { FX_DRAW(o_DrawInst[V](c, a, b, d, e)) }

// ---------------------------------------------------------------- instalacion sobre vtables
static std::set<void*> s_hookedFns;
struct HookRec { const char* name; void* target; void** orig; };
static std::vector<HookRec> s_hookList;
static bool Hook(void* target, void* detour, void** orig, const char* name) {
    if (!target || s_hookedFns.count(target)) return false;     // ya enganchada (contextos que comparten codigo)
    MH_STATUS s = MH_CreateHook(target, detour, orig);
    if (s != MH_OK) { Log("no se pudo enganchar %s (%d)", name, (int)s); return false; }
    MH_EnableHook(target);
    s_hookedFns.insert(target);
    s_hookList.push_back({name, target, orig});
    Log("gancho %s: %s (trampolin %p)", name, ModOff(target).c_str(), orig ? *orig : nullptr);
    return true;
}
static void** VT(void* obj) { return *(void***)obj; }

static std::recursive_mutex s_hookMutex;
template<int V> static void HookContextV(ID3D11DeviceContext* c) {
    std::lock_guard<std::recursive_mutex> lk(s_hookMutex);
    if (!g_cfg.optCtx) return;
    void** vt = VT(c);
    int n = 0;
    n += Hook(vt[9], (void*)&hk_SetPS<V>, (void**)&o_SetPS[V], "PSSetShader");
    n += Hook(vt[11], (void*)&hk_SetVS<V>, (void**)&o_SetVS[V], "VSSetShader");
    n += Hook(vt[12], (void*)&hk_DrawIdx<V>, (void**)&o_DrawIdx[V], "DrawIndexed");
    n += Hook(vt[13], (void*)&hk_Draw<V>, (void**)&o_Draw[V], "Draw");
    n += Hook(vt[20], (void*)&hk_DrawIdxInst<V>, (void**)&o_DrawIdxInst[V], "DrawIndexedInstanced");
    n += Hook(vt[21], (void*)&hk_DrawInst<V>, (void**)&o_DrawInst[V], "DrawInstanced");
    n += Hook(vt[69], (void*)&hk_SetCS<V>, (void**)&o_SetCS[V], "CSSetShader");
    if (n) Log("contexto enganchado (variante %d, %d funciones)", V, n);
}
static void HookContext(ID3D11DeviceContext* c, int v) {
    if (v == 0) HookContextV<0>(c); else HookContextV<1>(c);
}

static bool s_deviceHooked = false;
static bool s_confirmed = false;     // el dispositivo ya se vio en Present (es el del juego)

// el dispositivo "activo" es donde viven la textura de parametros y los recursos con nombre
static void SetActiveDevice(ID3D11Device* d) {
    std::lock_guard<std::recursive_mutex> lk(s_hookMutex);
    if (g_dev.dev == d) return;
    if (g_dev.dev) {
        // lo viejo se deja vivo a proposito (otro hilo podria estar usandolo); solo se olvida
        Log("cambio de dispositivo D3D11");
        { std::lock_guard<std::mutex> lk2(s_namedMutex); s_named.clear(); }
        g_dev.altSRV.clear(); g_dev.altTex.clear();
        g_dev.paramSRV = nullptr; g_dev.paramTex = nullptr; g_dev.imm = nullptr; g_dev.dev = nullptr;
    }
    d->AddRef();
    ID3D11DeviceContext* imm = nullptr; d->GetImmediateContext(&imm);
    ID3D11Texture1D* t = nullptr; ID3D11ShaderResourceView* v = nullptr;
    g_dev.dev = d;                       // CreateParamTex usa g_dev.dev
    CreateParamTex(&t, &v);
    g_dev.paramTex = t; g_dev.paramSRV = v; g_dev.imm = imm;
    if (imm) HookContextV<0>(imm);
    Log("dispositivo del juego: %p (nivel 0x%x)", (void*)d, (unsigned)d->GetFeatureLevel());
}

static void HookFactory(IDXGIFactory* fac);
static void HookFactoryOf(ID3D11Device* d) {
    // la fabrica DXGI que usa el juego para crear su cadena de intercambio: dispositivo -> adaptador -> fabrica
    IDXGIDevice* xd = nullptr; IDXGIAdapter* ad = nullptr; IDXGIFactory* f = nullptr;
    if (SUCCEEDED(d->QueryInterface(__uuidof(IDXGIDevice), (void**)&xd)) && xd) {
        if (SUCCEEDED(xd->GetAdapter(&ad)) && ad) {
            if (SUCCEEDED(ad->GetParent(__uuidof(IDXGIFactory), (void**)&f)) && f) { HookFactory(f); f->Release(); }
            ad->Release();
        }
        xd->Release();
    }
}
static void HookDevice(ID3D11Device* d) {
    std::lock_guard<std::recursive_mutex> lk(s_hookMutex);
    if (!s_deviceHooked) {
        s_deviceHooked = true;
        void** vt = VT(d);
        Hook(vt[12], (void*)&hk_CreateVS, (void**)&o_CreateVS, "CreateVertexShader");
        Hook(vt[15], (void*)&hk_CreatePS, (void**)&o_CreatePS, "CreatePixelShader");
        Hook(vt[18], (void*)&hk_CreateCS, (void**)&o_CreateCS, "CreateComputeShader");
        Hook(vt[27], (void*)&hk_CreateDC, (void**)&o_CreateDC, "CreateDeferredContext");
        ID3D11DeviceContext* imm = nullptr; d->GetImmediateContext(&imm);
        if (imm) { HookContextV<0>(imm); imm->Release(); }
        ID3D11DeviceContext* dc = nullptr;
        if (o_CreateDC && SUCCEEDED(o_CreateDC(d, 0, &dc)) && dc) { HookContextV<1>(dc); dc->Release(); }
        Log("dispositivo D3D11 enganchado");
    }
    if (g_cfg.optDxgi) HookFactoryOf(d);
    // el dispositivo del juego se decide al crear la cadena de intercambio (el juego crea varios de prueba)
}

// ---------------------------------------------------------------- cadena de intercambio
static void HookSwapChain(IDXGISwapChain* sc);
using PFN_CreateSC = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using PFN_CreateSCHwnd = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
static PFN_CreateSC o_CreateSC; static PFN_CreateSCHwnd o_CreateSCHwnd;

static IDXGISwapChain* s_gameSC = nullptr;
static void OnSwapChain(IUnknown* devUnk, IDXGISwapChain* sc) {
    if (!sc) return;
    s_gameSC = sc;
    ID3D11Device* d = nullptr;
    if (devUnk && SUCCEEDED(devUnk->QueryInterface(__uuidof(ID3D11Device), (void**)&d)) && d) {
        if (!s_deviceHooked) { Log("AVISO: el dispositivo ya existia al cargar el mod"); HookDevice(d); }
        SetActiveDevice(d);
        s_confirmed = true;
        d->Release();
    }
    if (g_cfg.optPresent) HookSwapChain(sc);
    DXGI_SWAP_CHAIN_DESC sd{}; sc->GetDesc(&sd);
    Log("cadena de intercambio del juego: %ux%u", sd.BufferDesc.Width, sd.BufferDesc.Height);
}
static HRESULT STDMETHODCALLTYPE hk_CreateSC(IDXGIFactory* f, IUnknown* dev, DXGI_SWAP_CHAIN_DESC* d, IDXGISwapChain** sc) {
    HRESULT hr = o_CreateSC(f, dev, d, sc);
    if (SUCCEEDED(hr) && sc && *sc) OnSwapChain(dev, *sc);
    return hr;
}
static HRESULT STDMETHODCALLTYPE hk_CreateSCHwnd(IDXGIFactory2* f, IUnknown* dev, HWND w, const DXGI_SWAP_CHAIN_DESC1* d,
                                                 const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fd, IDXGIOutput* o, IDXGISwapChain1** sc) {
    HRESULT hr = o_CreateSCHwnd(f, dev, w, d, fd, o, sc);
    if (SUCCEEDED(hr) && sc && *sc) OnSwapChain(dev, *sc);
    return hr;
}
static bool s_facHooked = false;
static void HookFactory(IDXGIFactory* fac) {
    std::lock_guard<std::recursive_mutex> lk(s_hookMutex);
    if (s_facHooked) return;
    s_facHooked = true;
    bool ok = Hook(VT(fac)[10], (void*)&hk_CreateSC, (void**)&o_CreateSC, "IDXGIFactory::CreateSwapChain");
    IDXGIFactory2* f2 = nullptr;
    if (SUCCEEDED(fac->QueryInterface(__uuidof(IDXGIFactory2), (void**)&f2)) && f2) {
        Hook(VT(f2)[15], (void*)&hk_CreateSCHwnd, (void**)&o_CreateSCHwnd, "IDXGIFactory2::CreateSwapChainForHwnd");
        f2->Release();
    }
    Log("ganchos de DXGI: %s", ok ? "ok" : "FALLARON (sin menu ni parametros)");
}

static HRESULT WINAPI hk_D3D11CreateDevice(IDXGIAdapter* a, D3D_DRIVER_TYPE t, HMODULE s, UINT f, const D3D_FEATURE_LEVEL* fl, UINT n, UINT v,
                                           ID3D11Device** d, D3D_FEATURE_LEVEL* ofl, ID3D11DeviceContext** c) {
    HRESULT hr = o_D3D11CreateDevice(a, t, s, f, fl, n, v, d, ofl, c);
    if (SUCCEEDED(hr) && d && *d) HookDevice(*d);
    return hr;
}
static HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChain(IDXGIAdapter* a, D3D_DRIVER_TYPE t, HMODULE s, UINT f, const D3D_FEATURE_LEVEL* fl, UINT n, UINT v,
        const DXGI_SWAP_CHAIN_DESC* sd, IDXGISwapChain** sc, ID3D11Device** d, D3D_FEATURE_LEVEL* ofl, ID3D11DeviceContext** c) {
    HRESULT hr = o_D3D11CreateDeviceAndSwapChain(a, t, s, f, fl, n, v, sd, sc, d, ofl, c);
    if (SUCCEEDED(hr) && d && *d) HookDevice(*d);
    return hr;
}

// ---------------------------------------------------------------- Present
static HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    ID3D11Device* d = nullptr;
    if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&d)) && d) {
        if (!s_deviceHooked) {   // el juego creo el dispositivo antes que nosotros
            Log("AVISO: el dispositivo ya existia al cargar el mod; los shaders creados antes no se reemplazan");
            HookDevice(d);
        }
        if (g_dev.dev != d) SetActiveDevice(d);
        s_confirmed = true;
        d->Release();
    }
    static bool first = true;
    if (first) { first = false; Log("primer Present (%d shaders propios activos)", g_replacedCount.load()); }
    {   // liberar shaders viejos de una recarga anterior
        std::lock_guard<std::mutex> lk(s_releaseMutex);
        for (auto* p : s_deferredRelease) p->Release();
        s_deferredRelease.clear();
    }
    DXGI_SWAP_CHAIN_DESC sd; sc->GetDesc(&sd);
    PollKeys(GetForegroundWindow() == sd.OutputWindow);
    UpdateTimeParams();
    UploadParams();
    OverlayPresent(sc);
    return o_Present(sc, sync, flags);
}
void OverlayResize();
static HRESULT STDMETHODCALLTYPE hk_Resize(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl) {
    OverlayResize();
    return o_Resize(sc, n, w, h, f, fl);
}
// Present se engancha cambiando el puntero de la vtable (lo mismo que hacen Sider y cloth-mod):
// asi la cadena queda juego -> ... -> nosotros -> lo que habia antes, sin pisar a nadie
static bool s_scHooked = false;
static void PatchSlot(void** slot, void* fn, void** orig) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
    *orig = *slot; *slot = fn;
    VirtualProtect(slot, sizeof(void*), old, &old);
}
static std::string ModOf(void* p) {
    HMODULE m = nullptr; char n[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &m))
        GetModuleFileNameA(m, n, MAX_PATH);
    const char* b = strrchr(n, '\\'); return b ? b + 1 : n;
}
static void HookSwapChain(IDXGISwapChain* sc) {
    std::lock_guard<std::recursive_mutex> lk(s_hookMutex);
    if (s_scHooked) return;
    s_scHooked = true;
    void** vt = VT(sc);
    Log("Present antes de nosotros: %s", ModOf(vt[8]).c_str());
    PatchSlot(&vt[8], (void*)&hk_Present, (void**)&o_Present);
    PatchSlot(&vt[13], (void*)&hk_Resize, (void**)&o_Resize);
    Log("Present enganchado");
}

void ReloadShaders() {
    ScanShaderFolder();
    std::unique_lock<std::shared_mutex> lk(s_shMutex);
    int n = 0;
    { std::lock_guard<std::mutex> lk2(g_errMutex); g_compileErrors.clear(); }
    g_failedCount = 0; g_replacedCount = 0;
    for (auto& kv : s_shaders) {
        ShaderEntry& e = kv.second;
        ID3D11DeviceChild* nr = BuildReplacement(e.dev, e.hash, e.type);
        if (e.repl) { std::lock_guard<std::mutex> lk3(s_releaseMutex); s_deferredRelease.push_back(e.repl); }
        e.repl = nr; if (nr) n++;
        e.rule = RuleFor(e.hash);
    }
    Log("recarga: %d shaders propios", n);
}

// ---------------------------------------------------------------- arranque

bool InstallHooks() {
    HMODULE comp = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!comp) comp = LoadLibraryW(L"d3dcompiler_46.dll");
    s_compile = comp ? (PFN_D3DCompile)GetProcAddress(comp, "D3DCompile") : nullptr;
    s_createBlob = comp ? (PFN_D3DCreateBlob)GetProcAddress(comp, "D3DCreateBlob") : nullptr;
    wchar_t cpath[MAX_PATH] = L"?";
    if (comp) GetModuleFileNameW(comp, cpath, MAX_PATH);
    Log("compilador de shaders: %ls", s_compile ? cpath : L"NO ENCONTRADO");
    ScanShaderFolder();
    if (MH_Initialize() != MH_OK) return false;

    HMODULE d3d = LoadLibraryW(L"d3d11.dll");
    if (!d3d) return false;
    wchar_t path[MAX_PATH]; GetModuleFileNameW(d3d, path, MAX_PATH);
    Log("d3d11.dll: %ls", path);
    std::wstring lp = path; for (auto& ch : lp) ch = (wchar_t)towlower(ch);
    if (lp.find(L"system32") == std::wstring::npos)
        Log("AVISO: d3d11.dll no es el de Windows (3DMigoto sigue activo?). Renombra el d3d11.dll de la carpeta del juego.");
    void* cd = GetProcAddress(d3d, "D3D11CreateDevice");
    void* cds = GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain");

    if (!g_cfg.optExports) {
        // sin tocar D3D11CreateDevice: un dispositivo propio alcanza para enganchar los metodos (el codigo es el mismo para todos)
        using PFN_CD = PFN_D3D11CreateDevice;
        ID3D11Device* dd = nullptr; ID3D11DeviceContext* dc = nullptr;
        HRESULT hr = cd ? ((PFN_CD)cd)(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dd, nullptr, &dc) : E_FAIL;
        Log("dispositivo propio para enganchar: 0x%08x", (unsigned)hr);
        if (FAILED(hr) || !dd) return false;
        HookDevice(dd);
        if (dc) dc->Release();
        dd->Release();
        return true;
    }
    Hook(cd, (void*)&hk_D3D11CreateDevice, (void**)&o_D3D11CreateDevice, "D3D11CreateDevice");
    bool ok = s_hookedFns.count(cd) > 0;
    Hook(cds, (void*)&hk_D3D11CreateDeviceAndSwapChain, (void**)&o_D3D11CreateDeviceAndSwapChain, "D3D11CreateDeviceAndSwapChain");
    return ok;
}

// ---------------------------------------------------------------- diagnostico: referencias a una DLL que se descarga
static bool Readable(uintptr_t p, size_t n) {
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery((void*)p, &m, sizeof(m)) || m.State != MEM_COMMIT) return false;
    if (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return p + n <= (uintptr_t)m.BaseAddress + m.RegionSize;
}
// sigue saltos (jmp rel32 / jmp [rip] / mov rax,imm;jmp rax) y devuelve la primera direccion dentro de [b,e)
static uintptr_t Follow(uintptr_t p, uintptr_t b, uintptr_t e) {
    for (int i = 0; i < 6 && p; i++) {
        if (p >= b && p < e) return p;
        if (!Readable(p, 16)) return 0;
        const uint8_t* c = (const uint8_t*)p;
        if (c[0] == 0xE9) p = p + 5 + *(int32_t*)(c + 1);
        else if (c[0] == 0xEB) p = p + 2 + (int8_t)c[1];
        else if (c[0] == 0xFF && c[1] == 0x25) { uintptr_t q = p + 6 + *(int32_t*)(c + 2); if (!Readable(q, 8)) return 0; p = *(uintptr_t*)q; }
        else if (c[0] == 0x48 && c[1] == 0xB8 && c[10] == 0xFF && c[11] == 0xE0) p = *(uintptr_t*)(c + 2);
        else return 0;
    }
    return (p >= b && p < e) ? p : 0;
}
static int s_refs = 0;
static void Check(const char* what, int idx, void* v, uintptr_t b, uintptr_t e) {
    uintptr_t hit = Follow((uintptr_t)v, b, e);
    if (!hit) return;
    s_refs++;
    if (idx >= 0) Log("    -> %s[%d] = %s llega a +0x%llx", what, idx, ModOff(v).c_str(), (unsigned long long)(hit - b));
    else Log("    -> %s = %s llega a +0x%llx", what, ModOff(v).c_str(), (unsigned long long)(hit - b));
}
static void CheckVT(const char* what, void* obj, int n, uintptr_t b, uintptr_t e) {
    if (!obj || !Readable((uintptr_t)obj, 8)) return;
    void** vt = *(void***)obj;
    if (!Readable((uintptr_t)vt, n * sizeof(void*))) return;
    for (int i = 0; i < n; i++) Check(what, i, vt[i], b, e);
}
void ScanRefsInto(uintptr_t b, uintptr_t e, const char* dll) {
    s_refs = 0;
    Log("  revisando quien apunta todavia a %s ...", dll);
    for (auto& h : s_hookList) {
        Check(h.name, -1, h.target, b, e);                     // el codigo enganchado
        if (h.orig && *h.orig) Check("trampolin", -1, *h.orig, b, e);   // lo que llamamos como "original"
    }
    Check("o_Present", -1, (void*)o_Present, b, e);
    Check("o_Resize", -1, (void*)o_Resize, b, e);
    if (s_gameSC) {
        CheckVT("vtable cadena", s_gameSC, 40, b, e);
        DXGI_SWAP_CHAIN_DESC sd{};
        if (SUCCEEDED(s_gameSC->GetDesc(&sd)) && sd.OutputWindow) {
            Check("WndProc del juego", -1, (void*)GetWindowLongPtrW(sd.OutputWindow, GWLP_WNDPROC), b, e);
        }
    }
    Check("WndProc guardado por el menu", -1, g_overlayOldProc, b, e);
    if (g_dev.dev) CheckVT("vtable dispositivo", g_dev.dev, 50, b, e);
    if (g_dev.imm) CheckVT("vtable contexto", g_dev.imm, 145, b, e);
    struct { const wchar_t* mod; const char* fn; } ex[] = {
        {L"d3d11.dll", "D3D11CreateDevice"}, {L"d3d11.dll", "D3D11CreateDeviceAndSwapChain"},
        {L"dxgi.dll", "CreateDXGIFactory"}, {L"dxgi.dll", "CreateDXGIFactory1"}, {L"dxgi.dll", "CreateDXGIFactory2"},
        {L"kernel32.dll", "LoadLibraryA"}, {L"kernel32.dll", "LoadLibraryW"}, {L"kernel32.dll", "LoadLibraryExW"},
        {L"kernel32.dll", "GetProcAddress"}, {L"kernel32.dll", "CreateFileW"}, {L"kernel32.dll", "CreateFileA"},
        {L"kernel32.dll", "ReadFile"}, {L"kernel32.dll", "CreateThread"}, {L"kernel32.dll", "ExitThread"},
        {L"kernelbase.dll", "LoadLibraryExW"}, {L"kernelbase.dll", "CreateFileW"}, {L"kernelbase.dll", "GetProcAddress"},
        {L"ntdll.dll", "LdrLoadDll"}, {L"ntdll.dll", "NtCreateFile"}, {L"ntdll.dll", "RtlExitUserThread"},
        {L"user32.dll", "SetWindowLongPtrW"}, {L"user32.dll", "PeekMessageW"}, {L"user32.dll", "GetMessageW"},
        {L"user32.dll", "DispatchMessageW"}, {L"user32.dll", "CallWindowProcW"}, {L"user32.dll", "GetAsyncKeyState"},
        {L"xinput1_4.dll", "XInputGetState"}, {L"xinput1_3.dll", "XInputGetState"}, {L"dinput8.dll", "DirectInput8Create"},
    };
    for (auto& x : ex) {
        HMODULE m = GetModuleHandleW(x.mod); if (!m) continue;
        void* f = (void*)GetProcAddress(m, x.fn); if (!f) continue;
        Check(x.fn, -1, f, b, e);
    }
    Log("  %d referencias encontradas", s_refs);
}

} // namespace fx
