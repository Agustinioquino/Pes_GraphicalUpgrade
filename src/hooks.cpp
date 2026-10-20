// PES21 FX - ganchos de D3D11: reemplazo de shaders por hash, parametros en t120, reglas por shader y Present.
#include "core.h"
#include <MinHook.h>
#include <d3dcompiler.h>
#include <ScreenGrab.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <dxgi1_2.h>
#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>
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

// #include "pes_common.hlsli" dentro de un shader: se busca en la carpeta shaders
struct PesInclude : public ID3DInclude {
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR file, LPCVOID, LPCVOID* data, UINT* bytes) override {
        std::wstring w; for (const char* c = file; *c; c++) w.push_back((wchar_t)(unsigned char)*c);
        std::string src;
        if (!ReadFileA(g_cfg.baseDir + L"shaders\\" + w, src)) { Log("include no encontrado: shaders\\%s", file); return E_FAIL; }
        char* buf = (char*)malloc(src.size() + 1); if (!buf) return E_OUTOFMEMORY;
        memcpy(buf, src.data(), src.size()); buf[src.size()] = 0;
        *data = buf; *bytes = (UINT)src.size();
        return S_OK;
    }
    HRESULT __stdcall Close(LPCVOID data) override { free((void*)data); return S_OK; }
};
static PesInclude s_include;
// hash de todos los .hlsli (si cambia un include, se recompila todo lo que lo usa)
static uint64_t IncludesHash() {
    uint64_t h = 0; WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((g_cfg.baseDir + L"shaders\\*.hlsli").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do { std::string src; if (ReadFileA(g_cfg.baseDir + L"shaders\\" + fd.cFileName, src)) h ^= Fnv64(src.data(), src.size()) * 0x9E3779B97F4A7C15ULL; } while (FindNextFileW(f, &fd));
    FindClose(f);
    return h;
}

static ID3DBlob* CompileFile(uint64_t hash, const char* type, const char* profile) {
    if (!s_compile) return nullptr;
    std::string src;
    std::wstring path = ShaderPath(hash, type);
    if (!ReadFileA(path, src)) return nullptr;
    uint64_t srcHash = Fnv64(src.data(), src.size()) ^ 0x5045533231465831ULL;
    if (src.find("#include") != std::string::npos) srcHash ^= IncludesHash();
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
    HRESULT hr = s_compile(src.data(), src.size(), name, nullptr, &s_include, "main", profile,
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

// shaders propios por nombre (generadores): shaders\<file>, con #include y cache como los demas
ID3DBlob* CompileNamedShader(const wchar_t* file, const char* profile) {
    if (!s_compile) return nullptr;
    std::string src;
    if (!ReadFileA(g_cfg.baseDir + L"shaders\\" + file, src)) { Log("shader %ls: no encontrado", file); return nullptr; }
    uint64_t srcHash = Fnv64(src.data(), src.size()) ^ 0x50455346414345ULL;
    if (src.find("#include") != std::string::npos) srcHash ^= IncludesHash();
    uint64_t nameHash = 0; for (const wchar_t* p = file; *p; p++) nameHash = nameHash * 31 + *p;
    std::wstring cpath = CachePath(nameHash, profile, srcHash);
    {
        std::string bin;
        if (s_createBlob && ReadFileA(cpath, bin) && !bin.empty()) {
            ID3DBlob* b = nullptr;
            if (SUCCEEDED(s_createBlob(bin.size(), &b)) && b) { memcpy(b->GetBufferPointer(), bin.data(), bin.size()); return b; }
        }
    }
    ID3DBlob *code = nullptr, *err = nullptr;
    char name[96]; snprintf(name, sizeof(name), "%ls", file);
    HRESULT hr = s_compile(src.data(), src.size(), name, nullptr, &s_include, "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) {
        std::string e = err ? std::string((const char*)err->GetBufferPointer(), err->GetBufferSize()) : "error desconocido";
        Log("ERROR compilando %s:\n%s", name, e.c_str());
        { std::lock_guard<std::mutex> lk(g_errMutex); g_compileErrors.push_back(std::string(name) + ": " + e.substr(0, 300)); }
        if (err) err->Release(); if (code) code->Release();
        return nullptr;
    }
    if (err) err->Release();
    CreateDirectoryW((g_cfg.baseDir + L"shaders\\cache").c_str(), nullptr);
    HANDLE h = CreateFileW(cpath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(h, code->GetBufferPointer(), (DWORD)code->GetBufferSize(), &wr, nullptr); CloseHandle(h); }
    return code;
}

// ---------------------------------------------------------------- tabla de shaders creados por el juego
static std::unordered_map<void*, ShaderEntry> s_shaders;   // nodos estables: los punteros a ShaderEntry no se invalidan
static std::unordered_map<void*, void*> s_replToObj;         // 19/10: nuestro shader de reemplazo -> shader del juego (para cuando
                                                            // alguien vuelve a enlazar el reemplazo, p.ej. al restaurar estado)
static std::shared_mutex s_shMutex;
static std::vector<IUnknown*> s_deferredRelease;
static std::mutex s_releaseMutex;

std::vector<const ShaderEntry*> AllShaders() {
    std::shared_lock<std::shared_mutex> lk(s_shMutex);
    std::vector<const ShaderEntry*> v; v.reserve(s_shaders.size());
    for (auto& kv : s_shaders) v.push_back(&kv.second);
    return v;
}
const ShaderEntry* FindShaderByHash(uint64_t h) {
    std::shared_lock<std::shared_mutex> lk(s_shMutex);
    for (auto& kv : s_shaders) if (kv.second.hash == h && kv.second.dev == g_dev.dev) return &kv.second;
    return nullptr;
}
static const ShaderEntry* FindShader(void* p) {
    if (!p) return nullptr;
    std::shared_lock<std::shared_mutex> lk(s_shMutex);
    auto it = s_shaders.find(p);
    if (it != s_shaders.end()) return &it->second;
    auto r = s_replToObj.find(p);   // es uno de nuestros reemplazos (lo volvio a enlazar BrowDrawAfterFace o un restore)
    if (r != s_replToObj.end()) { it = s_shaders.find(r->second); if (it != s_shaders.end()) return &it->second; }
    return nullptr;
}

// ---------------------------------------------------------------- recursos con nombre (copias de cbuffers, referencias, texturas)
struct NamedRes { ID3D11Buffer* buf = nullptr; ID3D11ShaderResourceView* srv = nullptr; bool failed = false; int copied = -1; bool cleared = false; };
static std::atomic<int> s_presentNo{0};
static std::map<std::string, NamedRes> s_named;
static std::mutex s_namedMutex;

void NamedSetSRV(const std::string& name, ID3D11ShaderResourceView* v) {
    std::lock_guard<std::mutex> lk(s_namedMutex); auto& r = s_named[name];
    if (r.srv == v) return; if (v) v->AddRef(); if (r.srv) r.srv->Release(); r.srv = v; r.failed = false;
}
static ID3D11ShaderResourceView* FileTexture(const std::string& rel) {
    // rel = "file:textures\\algo.dds" -> se carga una sola vez;  "gen:gris|blanco|negro|normal" -> textura neutra 4x4
    std::lock_guard<std::mutex> lk(s_namedMutex);
    auto& r = s_named[rel];
    if (r.srv || r.failed || !g_dev.dev) return r.srv;
    if (rel.rfind("gen:", 0) == 0) {
        uint32_t px = rel == "gen:blanco" ? 0xffffffffu : rel == "gen:negro" ? 0xff000000u : rel == "gen:normal" ? 0xffff8080u : 0xff808080u;   // RGBA8 (A en el byte alto)
        uint32_t data[16]; for (auto& d0 : data) d0 = px;
        D3D11_TEXTURE2D_DESC td{}; td.Width = td.Height = 4; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{ data, 16, 0 }; ID3D11Texture2D* t = nullptr;
        if (SUCCEEDED(g_dev.dev->CreateTexture2D(&td, &sd, &t))) { g_dev.dev->CreateShaderResourceView(t, nullptr, &r.srv); t->Release(); }
        if (!r.srv) r.failed = true;
        return r.srv;
    }
    std::string p = rel.substr(5);
    int n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    std::wstring wp(n ? n - 1 : 0, L'\0'); if (n) MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, wp.data(), n);
    HRESULT hr;
    bool png = wp.size() > 4 && _wcsicmp(wp.c_str() + wp.size() - 4, L".png") == 0;
    if (png) { EnsureComInit(); hr = DirectX::CreateWICTextureFromFileEx(g_dev.dev, nullptr, (g_cfg.baseDir + wp).c_str(), 0, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, DirectX::WIC_LOADER_IGNORE_SRGB, nullptr, &r.srv); }
    else hr = DirectX::CreateDDSTextureFromFile(g_dev.dev, (g_cfg.baseDir + wp).c_str(), nullptr, &r.srv);
    Log("textura %s: %s (0x%08x)", p.c_str(), SUCCEEDED(hr) ? "ok" : "NO SE PUDO CARGAR", (unsigned)hr);
    if (FAILED(hr)) { r.srv = nullptr; r.failed = true; }
    return r.srv;
}

ID3D11ShaderResourceView* FileTextureAny(const std::string& rel) { return FileTexture(rel); }
// 19/10 inspector: lee un cbuffer copiado por regla (copy_cb) -> floats. Se llama en Present (contexto inmediato):
// copia a staging y lee la copia del cuadro anterior (sin esperar a la GPU).
bool NamedCBRead(const std::string& name, std::vector<float>& out) {
    // 19/10 c: anillo de 3 staging por nombre: se copia al de hoy y se lee el de hace 2 cuadros (DO_NOT_WAIT ya no falla
    // cuando la GPU va 1-2 cuadros atras); si aun asi no esta listo se devuelve la ultima lectura buena
    struct Ring { ID3D11Buffer* st[3] = {}; int i = 0; UINT bytes = 0; std::vector<float> last; };
    static std::map<std::string, Ring> rings;
    ID3D11Buffer* src = nullptr; UINT bytes = 0;
    { std::lock_guard<std::mutex> lk(s_namedMutex); auto it = s_named.find(name); if (it == s_named.end() || !it->second.buf) return false; src = it->second.buf; D3D11_BUFFER_DESC d; src->GetDesc(&d); bytes = d.ByteWidth; src->AddRef(); }
    Ring& r = rings[name];
    if (r.bytes != bytes) { for (auto*& b : r.st) { if (b) b->Release(); b = nullptr; } r.bytes = bytes; r.last.clear(); }
    for (auto*& b : r.st) if (!b) { D3D11_BUFFER_DESC d{}; d.ByteWidth = bytes; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &b); }
    bool ok = false;
    if (g_dev.imm && r.st[0] && r.st[1] && r.st[2]) {
        ID3D11Buffer* rd = r.st[(r.i + 1) % 3];   // el mas viejo (copiado hace 2 llamadas)
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(g_dev.imm->Map(rd, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) { out.assign((const float*)m.pData, (const float*)m.pData + bytes / 4); g_dev.imm->Unmap(rd, 0); r.last = out; ok = true; }
        else if (!r.last.empty()) { out = r.last; ok = true; }
        g_dev.imm->CopyResource(r.st[r.i], src); r.i = (r.i + 1) % 3;
    }
    src->Release();
    return ok;
}
const Rule* RuleForHash(uint64_t h) { auto it = g_cfg.rules.find(h); return it == g_cfg.rules.end() ? nullptr : &it->second; }
std::atomic<int> g_drawOrder{0};
std::atomic<int> g_aliasDraws{0};   // dibujos redirigidos por alias en el cuadro (diagnostico)   // numero de orden del dibujo dentro del cuadro (se reinicia en Present)
std::string NamedOrigin(ID3D11ShaderResourceView* v) {
    std::lock_guard<std::mutex> lk(s_namedMutex);
    for (auto& kv : s_named) if (kv.second.srv == v) return kv.first.rfind("file:", 0) == 0 ? "archivo del mod: " + kv.first.substr(5) : "recurso del mod: " + kv.first;
    if (v == g_dev.paramSRV) return "parametros del mod (t120)";
    return "";
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
    // textura de parametros para las parcelas lejanas del pasto (la variante minima del juego): igual que la normal
    // con la marca w60 = 1 -> el VS del pasto las escala para que cubran su celda de 3 m
    if (!g_dev.grassTex) CreateParamTex(&g_dev.grassTex, &g_dev.grassSRV);
    if (g_dev.grassTex) { memcpy(q, p, sizeof(q)); q[63][3] = 1.0f;
        if (SUCCEEDED(g_dev.imm->Map(g_dev.grassTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, q, sizeof(q)); g_dev.imm->Unmap(g_dev.grassTex, 0); } }
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
struct CtxState { const ShaderEntry* ps = nullptr; const ShaderEntry* vs = nullptr; ID3D11Device* dev = nullptr; bool devKnown = false; ID3D11Buffer* lastHairCB = nullptr; };   // lastHairCB: b11 de la ultima cara dibujada en este contexto (la boca la hereda)
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
using PFN_ClearDSV = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
static PFN_ClearDSV o_ClearDSV[2];
// cejas (18/10): para dibujar las hebras hace falta saber si el juego usa profundidad invertida (limpia a 0) o normal (limpia a 1)
std::atomic<int> g_depthClearZero{0}, g_depthClearOne{0};
template<int V> static void STDMETHODCALLTYPE hk_ClearDSV(ID3D11DeviceContext* c, ID3D11DepthStencilView* v, UINT f, FLOAT d, UINT8 st) {
    if (f & D3D11_CLEAR_DEPTH) { if (d < 0.5f) g_depthClearZero++; else g_depthClearOne++; }
    o_ClearDSV[V](c, v, f, d, st);
}

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
    e.code = std::make_shared<std::vector<uint8_t>>((const uint8_t*)code, (const uint8_t*)code + len);
    e.repl = BuildReplacement(dev, e.hash, type);
    e.rule = RuleFor(e.hash);
    std::unique_lock<std::shared_mutex> lk(s_shMutex);
    auto it = s_shaders.find(obj);
    if (it != s_shaders.end()) {   // el juego libero un shader y la direccion se reutilizo
        if (it->second.repl) { std::lock_guard<std::mutex> lk2(s_releaseMutex); s_deferredRelease.push_back(it->second.repl); s_replToObj.erase(it->second.repl); }
        it->second = e;
    } else s_shaders.emplace(obj, e);
    if (e.repl) s_replToObj[e.repl] = obj;
}

static HRESULT STDMETHODCALLTYPE hk_CreateVS(ID3D11Device* d, const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11VertexShader** o) {
    HRESULT hr = o_CreateVS(d, c, n, l, o); if (SUCCEEDED(hr) && o) Register(d, *o, c, n, 'v'); return hr; }
static HRESULT STDMETHODCALLTYPE hk_CreatePS(ID3D11Device* d, const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11PixelShader** o) {
    HRESULT hr = o_CreatePS(d, c, n, l, o); if (SUCCEEDED(hr) && o) Register(d, *o, c, n, 'p'); return hr; }
static HRESULT STDMETHODCALLTYPE hk_CreateCS(ID3D11Device* d, const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11ComputeShader** o) {
    HRESULT hr = o_CreateCS(d, c, n, l, o); if (SUCCEEDED(hr) && o) Register(d, *o, c, n, 'c'); return hr; }

// (20/10) formatos de entrada: se anota cada input layout que crea el juego (elementos con semantica, formato, slot y
// offset) para poder describir la malla de un dibujo (el pasto, para armar una malla propia)
struct ILElem { std::string sem; UINT idx, fmt, slot, off, cls, step; };
static std::map<void*, std::vector<ILElem>> s_layouts; static std::mutex s_layoutMutex;
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateIL)(ID3D11Device*, const D3D11_INPUT_ELEMENT_DESC*, UINT, const void*, SIZE_T, ID3D11InputLayout**);
static PFN_CreateIL o_CreateIL = nullptr;
static HRESULT STDMETHODCALLTYPE hk_CreateIL(ID3D11Device* d, const D3D11_INPUT_ELEMENT_DESC* e, UINT n, const void* bc, SIZE_T bn, ID3D11InputLayout** o) {
    HRESULT hr = o_CreateIL(d, e, n, bc, bn, o);
    if (SUCCEEDED(hr) && o && *o && e && n > 0 && n < 32) {
        std::vector<ILElem> v; for (UINT i = 0; i < n; i++) v.push_back({ e[i].SemanticName ? e[i].SemanticName : "?", e[i].SemanticIndex, (UINT)e[i].Format, e[i].InputSlot, e[i].AlignedByteOffset, (UINT)e[i].InputSlotClass, e[i].InstanceDataStepRate });
        std::lock_guard<std::mutex> lk(s_layoutMutex); if (s_layouts.size() < 4096) s_layouts[*o] = v;
    }
    return hr;
}
std::string DescribeInputLayout(ID3D11InputLayout* il) {
    std::lock_guard<std::mutex> lk(s_layoutMutex); auto it = s_layouts.find(il); if (it == s_layouts.end()) return "(layout no anotado)";
    std::string r; for (auto& e : it->second) { char b[128]; snprintf(b, sizeof(b), "%s%u fmt %u slot %u off %u%s; ", e.sem.c_str(), e.idx, e.fmt, e.slot, e.off, e.cls ? " (instancia)" : ""); r += b; }
    return r;
}
// (20/10) LOD de texturas como lo hacen los drivers: todos los samplers lineales del juego pasan a anisotropico N
// ([PES21FX] aniso = 16) y reciben un sesgo de mip global (mip_bias, negativo = mas nitido). Los samplers "point"
// (G-buffer, profundidad) y los de comparacion (sombras) no se tocan.
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateSS)(ID3D11Device*, const D3D11_SAMPLER_DESC*, ID3D11SamplerState**);
static PFN_CreateSS o_CreateSS = nullptr; static std::atomic<int> s_ssUpgraded{0};
static HRESULT STDMETHODCALLTYPE hk_CreateSS(ID3D11Device* d, const D3D11_SAMPLER_DESC* sd, ID3D11SamplerState** o) {
    if (sd && (g_cfg.aniso > 1 || g_cfg.mipBias != 0.0f)) {
        D3D11_SAMPLER_DESC m = *sd; bool changed = false;
        bool point = m.Filter == D3D11_FILTER_MIN_MAG_MIP_POINT || m.Filter == D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
        bool cmpF = (m.Filter & 0x80) != 0;   // D3D11_FILTER_COMPARISON_*
        bool linear = !point && !cmpF && m.Filter < D3D11_FILTER_MINIMUM_MIN_MAG_MIP_POINT;
        if (linear && m.MaxLOD > 0.0f) {
            if (g_cfg.aniso > 1 && (m.Filter != D3D11_FILTER_ANISOTROPIC || m.MaxAnisotropy < (UINT)g_cfg.aniso)) { m.Filter = D3D11_FILTER_ANISOTROPIC; m.MaxAnisotropy = (UINT)g_cfg.aniso; changed = true; }
            if (g_cfg.mipBias != 0.0f) { m.MipLODBias += g_cfg.mipBias; changed = true; }
        }
        if (changed) { HRESULT hr = o_CreateSS(d, &m, o); if (SUCCEEDED(hr)) { if (s_ssUpgraded.fetch_add(1) == 0) Log("samplers: anisotropico %dx y sesgo de mip %.2f forzados", g_cfg.aniso, g_cfg.mipBias); return hr; } }
    }
    return o_CreateSS(d, sd, o);
}
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
    CatalogCS(e);
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
struct DrawScope { Saved s[12]; int n = 0; FaceGenExtra brow; bool skip = false; ID3D11PixelShader* restorePS = nullptr; };

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
            if (r.buf) { c->CopyResource(r.buf, src); r.copied = s_presentNo.load(); r.cleared = false; }
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
        case RuleAction::FaceGen: {
            if (!ps) break;
            ID3D11ShaderResourceView *fa = nullptr, *fb = nullptr; FaceGenExtra ex;
            if (FaceGenLookup(c, &fa, &fb, &ex)) {
                // 19/10 c: la boca hereda este b11; se retiene con AddRef porque la entrada de cara puede liberarse (partido nuevo,
                // recarga de tipos de pelo) antes del dibujo de la boca -> puntero colgado -> el driver se caia (nvwgf2umx 0xc0000005)
                if (ex.hairCB) { CtxState& cs = Ctx(c); if (cs.lastHairCB != ex.hairCB) { ex.hairCB->AddRef(); if (cs.lastHairCB) cs.lastHairCB->Release(); cs.lastHairCB = ex.hairCB; } }
                Remember(c, d, true, true, a.slot); c->PSSetShaderResources(a.slot, 1, &fa);
                Remember(c, d, true, true, a.idx);  c->PSSetShaderResources(a.idx, 1, &fb);
                // 18/10 pelo: parametros del tipo de pelo de ESTA cara (PES21FX_hair.ini) en b11
                if (ex.hairCB) { Remember(c, d, true, false, 11); c->PSSetConstantBuffers(11, 1, &ex.hairCB); }
                // cejas con hebras: mapa de semillas en t26 y buffer de semillas (UAV 5) limpio para este dibujo
                if (ex.seedMap && ex.seedsUAV && g_cfg.live[74][0] > 0.5f) {
                    Remember(c, d, true, true, 26); c->PSSetShaderResources(26, 1, &ex.seedMap);
                    const UINT z[4] = { 0, 0, 0, 0 };
                    if (ex.argsUAV) c->ClearUnorderedAccessViewUint(ex.argsUAV, z);   // 18/10: solo se limpia el contador (las semillas se escriben compactas)
                    else c->ClearUnorderedAccessViewUint(ex.seedsUAV, z);
                    // el UAV va en el slot 5 (u5): hay que enlazarlo con la cuenta real de render targets (<= 5), no con "mantener",
                    // porque si el juego dejo 8 slots declarados la llamada se rechaza en silencio
                    ID3D11RenderTargetView* rts[8] = {}; ID3D11DepthStencilView* dsv = nullptr; c->OMGetRenderTargets(8, rts, &dsv);
                    UINT nrt = 0; for (UINT i = 0; i < 8; i++) if (rts[i]) nrt = i + 1;
                    if (nrt <= 5) { ID3D11UnorderedAccessView* uavs[2] = { ex.seedsUAV, ex.argsUAV }; c->OMSetRenderTargetsAndUnorderedAccessViews(nrt, rts, dsv, 5, ex.argsUAV ? 2 : 1, uavs, nullptr); d.brow = ex; d.brow.rtCount = (int)nrt; }
                    else { static std::atomic<bool> warned{false}; if (!warned.exchange(true)) Log("cejas: la cara se dibuja con %u render targets, no puedo enlazar el buffer de semillas", nrt); }
                    for (auto* v : rts) if (v) v->Release(); if (dsv) dsv->Release();
                }
            }
            break; }
        case RuleAction::SetParam: break;   // ya resuelto con la textura de parametros alternativa
        }
    }
}

static std::atomic<bool> s_firstRule{false};
thread_local bool t_sparseGrass = false;
thread_local UINT t_drawVerts = 0;   // vertices del dibujo en curso (para "solo esta capa" del inspector)   // el dibujo en curso es una parcela lejana del pasto (lo marca GrassOnDraw)
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
    // 19/10: PS sin registrar (lo creo otra DLL, p.ej. cloth-mod): si el ini lo declara "como = <hash>", se trata como ese shader
    // (nuestro reemplazo + sus reglas) solo durante este dibujo
    if (!ps && vs && g_cfg.optRules) {
        ID3D11PixelShader* raw = nullptr; c->PSGetShader(&raw, nullptr, nullptr);
        if (raw) {
            uint64_t fp = InspectFingerprint(c, vs); uint64_t al = InspectAliasFor(fp, vs->hash, t_drawVerts);
            const ShaderEntry* e = al ? FindShaderByHash(al) : nullptr;
            { static std::mutex mx; static std::set<uint64_t> seen; bool first; { std::lock_guard<std::mutex> lk(mx); first = seen.insert(fp).second; }
              if (first && seen.size() < 300) Log("alias: huella %016llx (VS %016llx, %u vertices, %s) -> como %016llx: entrada %s, reemplazo %s", (unsigned long long)fp, (unsigned long long)vs->hash, t_drawVerts, c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED ? "diferido" : "inmediato", (unsigned long long)al, e ? "si" : "NO", (e && e->repl) ? "si" : "NO"); }
            if (e && e->repl && !g_cfg.bypass) { d.restorePS = raw; c->PSSetShader((ID3D11PixelShader*)e->repl, nullptr, 0); ps = e; g_aliasDraws++; }
            else raw->Release();
        }
    }
    if (!ps && !vs) { static std::atomic<int> n{0}; if (n < 20 && t_drawVerts >= 2000) { n++; Log("alias: dibujo con PS y VS sin registrar (%u vertices): no se puede redirigir", t_drawVerts); } return false; }
    bool pr = ps && ps->repl, vr = vs && vs->repl;
    if (pr || vr) {
        // si el PS tiene parametros propios (p.ej. w5 = $pes_push) los ven el PS y el VS de ese dibujo
        ID3D11ShaderResourceView* v = (ps && ps->rule && ps->rule->paramSet >= 0) ? ParamSRVFor(ps) : ParamSRVFor(vs);
        if (v) {
            if (pr) c->PSSetShaderResources(120, 1, &v);
            if (vr) { ID3D11ShaderResourceView* vv = (t_sparseGrass && g_dev.grassSRV) ? g_dev.grassSRV : v; c->VSSetShaderResources(120, 1, &vv); }
        }
        t_sparseGrass = false;
    }
    if (ps && ps->hash == 0x5ce3ed8702271bc1ULL) {   // 18/10 c: diagnostico de la cara: cuantos dibujos llegan aca y con que
        g_stFaceDraws++; if (ps->rule) g_stFaceRule++;
        if (c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED) g_stFaceCtxDef++;
    }
    if (!g_cfg.optRules) return false;
    if ((ps && ps->rule) || (vs && vs->rule)) {
        if (!s_firstRule.exchange(true)) Log("primer dibujo con regla");
    }
    if (ps && ps->rule) RunRule(c, ps, d);
    if (vs && vs->rule) RunRule(c, vs, d);
    // 19/10: la boca (63fab7) hereda el b11 (tipo de pelo + semilla de la cara) de la ultima cara dibujada en este contexto:
    // variacion de dientes/encias por jugador
    if (ps && ps->hash == 0x63fab7735eee149cULL && ps->repl && s.lastHairCB) { Remember(c, d, true, false, 11); c->PSSetConstantBuffers(11, 1, &s.lastHairCB); }
    // 20/10: desgaste de la cancha en vivo: la posicion de cada jugador sale del dibujo de su short / medias (b5 del VS)
    if (ps && ps->hash == 0x55297925e0c66085ULL) PitchWearNotePlayer(c);
    // 19/10: SSS separable en pantalla antes de la composicion: la irradiancia (t9) llega ya difundida sobre la piel
    if (ps && (ps->hash == 0x619b86cb5691cdceULL || ps->hash == 0xf807e74d5b900a15ULL) && ps->repl) {
        if (ID3D11ShaderResourceView* v = SSSSBeforeComposition(c)) { Remember(c, d, true, true, 9); c->PSSetShaderResources(9, 1, &v); }
    }
    // 19/10: capas editadas desde el inspector (PES21FX_capas.ini): textura por slot u "ocultar" el dibujo
    g_drawOrder++;
    if (ps && g_layersAny.load()) { LayerOv ov;
        if (InspectShouldSkip(ps->hash, t_drawVerts)) { d.skip = true; g_layerSkips++; }
        if (ID3D11PixelShader* hp = InspectHighlightPS(ps->hash)) { if (!d.restorePS) c->PSGetShader(&d.restorePS, nullptr, nullptr); c->PSSetShader(hp, nullptr, 0); g_layerSkips++; }
        if (InspectLayerFor(ps->hash, ov)) {
            for (auto& kv : ov.slot) {
                ID3D11ShaderResourceView* v = (kv.second == "none") ? nullptr : FileTexture(kv.second);
                if (kv.second == "none" || v) { Remember(c, d, true, true, kv.first); c->PSSetShaderResources(kv.first, 1, &v); }
            }
        } }
    if (ps && ps->hash == 0x5ce3ed8702271bc1ULL) {
        ID3D11ShaderResourceView* chk[4] = {}; c->PSGetShaderResources(23, 4, chk);
        if (chk[0]) g_stFaceT23++; if (chk[3]) g_stFaceT26++;
        // 19/10: una vez cada 10 s, describir lo que hay en t23..t26 (es nuestra textura generada o es otra cosa?)
        static double nextDesc = 0; double tn = Now();
        if (tn > nextDesc) { nextDesc = tn + 10.0;
            char line[512]; int n = snprintf(line, sizeof(line), "cara: t23..t26 en el dibujo:");
            for (int i = 0; i < 4 && n < 480; i++) {
                if (!chk[i]) { n += snprintf(line + n, sizeof(line) - n, " [t%d vacio]", 23 + i); continue; }
                ID3D11Resource* res = nullptr; chk[i]->GetResource(&res);
                D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN; if (res) res->GetType(&dim);
                if (res && dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { D3D11_TEXTURE2D_DESC td; ((ID3D11Texture2D*)res)->GetDesc(&td);
                    D3D11_SHADER_RESOURCE_VIEW_DESC vd; chk[i]->GetDesc(&vd);
                    n += snprintf(line + n, sizeof(line) - n, " [t%d %ux%u fmt %d mips %u bind %u misc %u vista mip %u/%u]", 23 + i, td.Width, td.Height, (int)td.Format, td.MipLevels, td.BindFlags, td.MiscFlags, vd.Texture2D.MostDetailedMip, vd.Texture2D.MipLevels); }
                else n += snprintf(line + n, sizeof(line) - n, " [t%d dim %d]", 23 + i, (int)dim);
                if (res) res->Release();
            }
            Log("%s", line);
        }
        for (auto* v : chk) if (v) v->Release();
    }
    return d.n > 0;
}
#define FX_DRAW(verts, kind, inst, call) if (g_catalogHook) { CtxState& cs_ = Ctx(c); CatalogDraw(c, cs_.vs, cs_.ps, verts, kind, inst); } t_drawVerts = (UINT)(verts); DrawScope ds; bool r = BeforeDraw(c, ds); if (!ds.skip) { call; } if (ds.restorePS) { c->PSSetShader(ds.restorePS, nullptr, 0); ds.restorePS->Release(); } \
    if (g_inspectOn) { CtxState& ci_ = Ctx(c); if (InspectBegin(c, ci_.ps, ci_.vs, kind, verts, inst)) { call; InspectEnd(c); } } \
    if (r) Restore(c, ds); if (ds.brow.seedsUAV) BrowDrawAfterFace(c, &ds.brow);
template<int V> static void STDMETHODCALLTYPE hk_DrawIdx(ID3D11DeviceContext* c, UINT a, UINT b, INT d) {
    { CtxState& cg = Ctx(c); const ShaderEntry* gvs = cg.vs;
      if (g_grassProbeOn) CatalogGrassDraw(c, gvs, a, b, d);
      if (gvs && gvs->hash == 0x498b30bad256e8edULL) GrassOnDraw(c, gvs, a, b, d);
      if (g_autoDumpAny) InspectAutoDump(c, cg.ps); }
    FX_DRAW(a, 1, 1, o_DrawIdx[V](c, a, b, d)) }
template<int V> static void STDMETHODCALLTYPE hk_Draw(ID3D11DeviceContext* c, UINT a, UINT b) { FX_DRAW(a, 0, 1, o_Draw[V](c, a, b)) }
template<int V> static void STDMETHODCALLTYPE hk_DrawIdxInst(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, INT e, UINT f) { FX_DRAW(a, 3, b, o_DrawIdxInst[V](c, a, b, d, e, f)) }
template<int V> static void STDMETHODCALLTYPE hk_DrawInst(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, UINT e) { FX_DRAW(a, 2, b, o_DrawInst[V](c, a, b, d, e)) }

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
    n += Hook(vt[53], (void*)&hk_ClearDSV<V>, (void**)&o_ClearDSV[V], "ClearDepthStencilView");
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
        Hook(vt[11], (void*)&hk_CreateIL, (void**)&o_CreateIL, "CreateInputLayout");
        Hook(vt[23], (void*)&hk_CreateSS, (void**)&o_CreateSS, "CreateSamplerState");
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


// ---------------------------------------------------------------- Present
// 19/10: copia de la imagen final de cada cuadro (antes del menu), como recurso con nombre "pes_frame" para las reglas
// (srv = N pes_frame): la usan los reflejos en espacio de pantalla de los ojos. Se renueva en cada Present.
static ID3D11Texture2D* s_frameTex = nullptr; static UINT s_frameW = 0, s_frameH = 0; static DXGI_FORMAT s_frameFmt = DXGI_FORMAT_UNKNOWN;
static void FrameCopyPresent(IDXGISwapChain* sc) {
    if (!g_dev.dev || !g_dev.imm || !g_cfg.optPresent) return;
    ID3D11Texture2D* bb = nullptr; if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) return;
    D3D11_TEXTURE2D_DESC bd; bb->GetDesc(&bd);
    if (bd.SampleDesc.Count != 1) { bb->Release(); return; }
    DXGI_FORMAT fmt = bd.Format;
    switch (fmt) {   // formatos typeless: vista con el equivalente UNORM
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: fmt = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: fmt = DXGI_FORMAT_B8G8R8A8_UNORM; break;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: fmt = DXGI_FORMAT_R10G10B10A2_UNORM; break;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: fmt = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
        default: break;
    }
    if (!s_frameTex || s_frameW != bd.Width || s_frameH != bd.Height || s_frameFmt != bd.Format) {
        ID3D11ShaderResourceView* oldSrv = nullptr;
        { std::lock_guard<std::mutex> lk(s_namedMutex); auto& r = s_named["pes_frame"]; oldSrv = r.srv; r.srv = nullptr; r.failed = false; }
        if (oldSrv) oldSrv->Release();
        if (s_frameTex) { s_frameTex->Release(); s_frameTex = nullptr; }
        D3D11_TEXTURE2D_DESC td = bd; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.MiscFlags = 0; td.Usage = D3D11_USAGE_DEFAULT; td.CPUAccessFlags = 0; td.MipLevels = 1; td.ArraySize = 1;
        if (SUCCEEDED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_frameTex)) && s_frameTex) {
            D3D11_SHADER_RESOURCE_VIEW_DESC vd{}; vd.Format = fmt; vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels = 1;
            ID3D11ShaderResourceView* v = nullptr;
            if (SUCCEEDED(g_dev.dev->CreateShaderResourceView(s_frameTex, &vd, &v)) && v) { std::lock_guard<std::mutex> lk(s_namedMutex); s_named["pes_frame"].srv = v; }
            else { s_frameTex->Release(); s_frameTex = nullptr; }
            Log("pes_frame: copia del cuadro %ux%u fmt %d: %s", bd.Width, bd.Height, (int)bd.Format, s_frameTex ? "ok" : "NO SE PUDO CREAR");
        }
        s_frameW = bd.Width; s_frameH = bd.Height; s_frameFmt = bd.Format;
    }
    if (s_frameTex) g_dev.imm->CopyResource(s_frameTex, bb);
    bb->Release();
}
// 19/10: CAPTURA AUTOMATICA en partido: cada N segundos guarda la imagen final (jpg) en PES21FX\capturas\ y una linea en
// capturas\luz.txt con la luz de la escena (sol, color, exposicion, niebla) y los parametros del mod. Sirve para calibrar
// la luz sin que el usuario tenga que sacar capturas. [PES21FX] auto_captura = 1, auto_captura_seg, auto_captura_max
static float ReadElev(const std::vector<float>& L, const std::vector<float>& S, float* azOut) {
    // m_lightDir0 (b3) esta en espacio de vista; el "arriba" del mundo en vista es la fila 1 de m_view (b2: offset 32 floats)
    if (L.size() < 4 || S.size() < 48) { if (azOut) *azOut = 0; return 0; }
    float lx = L[0], ly = L[1], lz = L[2]; float n = sqrtf(lx * lx + ly * ly + lz * lz); if (n < 1e-6f) return 0; lx /= n; ly /= n; lz /= n;
    float ux = S[32 + 4], uy = S[32 + 5], uz = S[32 + 6];   // m_view fila 1 (_m10,_m11,_m12)
    float fx = S[32 + 8], fy = S[32 + 9], fz = S[32 + 10];  // fila 2 (adelante)
    float rx = S[32 + 0], ry = S[32 + 1], rz = S[32 + 2];   // fila 0 (derecha)
    float up = lx * ux + ly * uy + lz * uz;
    float e = asinf(std::clamp(up, -1.f, 1.f)) * 57.2958f; if (e < 0) e = -e;
    if (azOut) { float x = lx * rx + ly * ry + lz * rz, z = lx * fx + ly * fy + lz * fz; *azOut = atan2f(x, z) * 57.2958f; if (*azOut < 0) *azOut += 360.f; }
    return e;
}
void AutoCapturePresent(ID3D11Texture2D* frame) {
    static double last = 0; static int count = 0; static bool dirOk = false;
    if (!g_cfg.autoCap || !frame || !g_dev.imm) return;
    if (g_cfg.live[93][3] < 0.5f) return;                 // solo en partido
    double t = Now(); if (t - last < g_cfg.autoCapSec) return; last = t;
    if (count >= g_cfg.autoCapMax) return;
    std::wstring dir = g_cfg.baseDir + L"capturas"; if (!dirOk) { CreateDirectoryW(dir.c_str(), nullptr); dirOk = true; }
    std::vector<float> L, S; bool lok = NamedCBRead("LightCB", L), sok = NamedCBRead("SceneCB", S);
    float az = 0, el = lok && sok ? ReadElev(L, S, &az) : 0;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t name[96]; swprintf(name, 96, L"%02d%02d%02d_sol%02.0f.jpg", st.wHour, st.wMinute, st.wSecond, el);
    EnsureComInit();
    HRESULT hr = DirectX::SaveWICTextureToFile(g_dev.imm, frame, GUID_ContainerFormatJpeg, (dir + L"\\" + name).c_str(), nullptr, nullptr, false);
    if (FAILED(hr)) { Log("captura automatica: no se pudo guardar (0x%08x)", (unsigned)hr); return; }
    count++;
    FILE* f = _wfopen((dir + L"\\luz.txt").c_str(), L"ab");
    if (f) {
        fprintf(f, "%02d:%02d:%02d | %ls | ", st.wHour, st.wMinute, st.wSecond, name);
        if (lok && L.size() >= 12) fprintf(f, "sol elev=%.1f az=%.0f dir=(%.3f %.3f %.3f) color0=(%.0f %.0f %.0f %.2f) color1=(%.0f %.0f %.0f) | ", el, az, L[0], L[1], L[2], L[4], L[5], L[6], L[7], L[8], L[9], L[10]);
        else fprintf(f, "sin LightCB | ");
        if (sok && S.size() >= 120) fprintf(f, "exposure=(%.4g %.4g %.4g %.4g) fog=(%.3f %.3f %.3f) fogP=(%.3g %.3g %.3g %.3g) | ", S[92], S[93], S[94], S[95], S[108], S[109], S[110], S[96], S[97], S[98], S[99]);
        else fprintf(f, "sin SceneCB | ");
        const auto& p = g_cfg.live;
        fprintf(f, "mod: sol z4=%.2f w4=%.2f curva x65=%.0f (%.2f %.2f %.2f) amb(%.2f %.2f) niebla(%.2f %.2f) calidez %.2f azul %.2f | cielo w47=%.0f x47=%.2f | rebote w42=%.0f x42=%.2f | sssss w95=%.0f | brillo piel w103=%.0f | pupila %.2f\n",
            p[4][2], p[4][3], p[65][0], p[65][1], p[65][2], p[65][3], p[66][0], p[66][1], p[66][2], p[66][3], p[67][0], p[67][1], p[47][3], p[47][0], p[42][3], p[42][0], p[95][3], p[103][3], p[93][0]);
        fclose(f);
    }
    if (count == 1) Log("captura automatica: primera imagen en capturas\\ (cada %.0f s, maximo %d)", g_cfg.autoCapSec, g_cfg.autoCapMax);
}
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
    g_bbW = sd.BufferDesc.Width; g_bbH = sd.BufferDesc.Height;
    PollKeys(GetForegroundWindow() == sd.OutputWindow);
    // copias de cbuffers de luz que no se renovaron en el ultimo cuadro (menus 3D, editor, pre-partido: no corre la
    // pasada de luz del sol): se ponen en cero para que los efectos que las leen se apaguen en vez de usar el sol
    // del ultimo partido
    {
        int now = s_presentNo.fetch_add(1); g_drawOrder = 0;
        std::lock_guard<std::mutex> lk(s_namedMutex);
        for (auto& kv : s_named) {
            NamedRes& r = kv.second;
            if (!r.buf || r.cleared || kv.first.find("Light") == std::string::npos) continue;
            if (r.copied >= now - 1) continue;
            D3D11_BUFFER_DESC bd; r.buf->GetDesc(&bd);
            static std::vector<char> zeros; if (zeros.size() < bd.ByteWidth) zeros.assign(bd.ByteWidth, 0);
            if (g_dev.imm) { g_dev.imm->UpdateSubresource(r.buf, 0, nullptr, zeros.data(), 0, 0); r.cleared = true; }
        }
    }
    UpdateTimeParams();
    UploadParams();
    WatchShaderFolder();
    PollMatch();
    CatalogPresent();
    PitchWearPresent();
    GrassPresent();
    FaceGenPresent();
    InspectPresent();
    FrameCopyPresent(sc);   // antes del menu, para que el menu no se refleje en los ojos
    AutoCapturePresent(s_frameTex);
    MenuTestPresent(sc);
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

// cada ~1 s se mira si algun shader cambio en disco (fecha y cantidad); si cambio, se recompila solo ese cambio
void WatchShaderFolder() {
    static double next = 0; static ULONGLONG lastSig = 0;
    double t = Now(); if (t < next) return; next = t + 1.0;
    WIN32_FIND_DATAW fd; ULONGLONG sig = 0; int n = 0;
    const wchar_t* masks[2] = { L"shaders\\*_replace.txt", L"shaders\\*.hlsli" };
    for (int mi = 0; mi < 2; mi++) {
        HANDLE h = FindFirstFileW((g_cfg.baseDir + masks[mi]).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do { ULONGLONG w = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime; sig ^= w * 0x9E3779B97F4A7C15ULL + (++n); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (sig == 0) return;
    if (lastSig == 0) { lastSig = sig; return; }
    if (sig != lastSig) { lastSig = sig; Log("cambio en la carpeta de shaders: recargando"); ReloadShaders(); }
}

void ReloadShaders() {
    ScanShaderFolder();
    FaceGenReloadShaders();
    SSSSReloadShaders();
    PitchWearReloadShaders();
    std::unique_lock<std::shared_mutex> lk(s_shMutex);
    int n = 0;
    { std::lock_guard<std::mutex> lk2(g_errMutex); g_compileErrors.clear(); }
    g_failedCount = 0; g_replacedCount = 0;
    for (auto& kv : s_shaders) {
        ShaderEntry& e = kv.second;
        ID3D11DeviceChild* nr = BuildReplacement(e.dev, e.hash, e.type);
        if (e.repl) { std::lock_guard<std::mutex> lk3(s_releaseMutex); s_deferredRelease.push_back(e.repl); s_replToObj.erase(e.repl); }
        e.repl = nr; if (nr) { n++; s_replToObj[nr] = kv.first; }
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
    if (MH_Initialize() != MH_OK) { Log("MinHook no inicio"); return false; }
    return true;
}

// llamado por nuestro D3D11CreateDevice (somos el d3d11.dll de la carpeta del juego)
void OnDeviceCreated(ID3D11Device* d) { if (d) HookDevice(d); }

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
