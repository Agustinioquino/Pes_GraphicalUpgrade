// PES21 FX - ganchos de D3D11: reemplazo de shaders por hash, parametros en t120, reglas por shader y Present.
#include "core.h"
#include <MinHook.h>
#include <d3d11_1.h>
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
#include <intrin.h>
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
int PresentNo() { return s_presentNo.load(); }   // (23/10) numero de cuadro (sube en cada Present): camhook.cpp, medidor
static std::map<std::string, NamedRes> s_named;
static std::mutex s_namedMutex;

void NamedSetSRV(const std::string& name, ID3D11ShaderResourceView* v) {
    std::lock_guard<std::mutex> lk(s_namedMutex); auto& r = s_named[name];
    if (r.srv == v) return; if (v) v->AddRef(); if (r.srv) r.srv->Release(); r.srv = v; r.failed = false;
}
// (27/10 c) recurso detras de un SRV guardado por regla (ref_srv): la sombra de la cabeza compara el DSV de la pasada de sombras con "ShadowMap"
ID3D11Resource* NamedSRVResource(const std::string& name) {
    ID3D11ShaderResourceView* v = nullptr;
    { std::lock_guard<std::mutex> lk(s_namedMutex); auto it = s_named.find(name); if (it == s_named.end() || !it->second.srv) return nullptr; v = it->second.srv; v->AddRef(); }
    ID3D11Resource* r = nullptr; v->GetResource(&r); v->Release();
    if (r) r->Release();   // sin referencia propia: solo se compara el puntero
    return r;
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
bool NamedCBRead(const std::string& name, std::vector<float>& out, int* frameOut) {
    // 19/10 c: anillo de staging por nombre, lectura sin esperar (DO_NOT_WAIT) de una copia de hace 1-3 cuadros.
    // (26/10) Antes se copiaba y se leia "la de hace 2 llamadas" en CADA llamada: con 2-3 lecturas por cuadro (pupila, sol en
    // mundo, log) esa copia era del mismo cuadro, casi nunca estaba lista y se devolvia la ultima lectura buena, que podia
    // quedar vieja un minuto entero (log: direccion del sol fija en vista mientras la camara se movia; la fila 111 y el pasto
    // con el sol girando). Ahora se copia una vez por cuadro, cada copia lleva su numero de cuadro, se devuelve la mas nueva
    // que ya este lista y frameOut dice de que cuadro es (para no mezclar el sol de un cuadro con la camara de otro).
    struct Ring { ID3D11Buffer* st[4] = {}; int frame[4] = { -1, -1, -1, -1 }; int i = 0; UINT bytes = 0; std::vector<float> last; int lastFrame = -1; };
    static std::map<std::string, Ring> rings;
    ID3D11Buffer* src = nullptr; UINT bytes = 0;
    { std::lock_guard<std::mutex> lk(s_namedMutex); auto it = s_named.find(name); if (it == s_named.end() || !it->second.buf) return false; src = it->second.buf; D3D11_BUFFER_DESC d; src->GetDesc(&d); bytes = d.ByteWidth; src->AddRef(); }
    Ring& r = rings[name];
    if (r.bytes != bytes) { for (auto*& b : r.st) { if (b) b->Release(); b = nullptr; } for (int& f : r.frame) f = -1; r.bytes = bytes; r.last.clear(); r.lastFrame = -1; }
    for (auto*& b : r.st) if (!b) { D3D11_BUFFER_DESC d{}; d.ByteWidth = bytes; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &b); }
    bool ok = false;
    if (g_dev.imm && r.st[0] && r.st[1] && r.st[2] && r.st[3]) {
        const int now = s_presentNo.load();
        // la mas nueva que ya este lista (sin la de este cuadro, que recien se copia abajo)
        for (int k = 1; k <= 4; k++) {
            int j = (r.i - k + 4) % 4;
            if (r.frame[j] < 0 || r.frame[j] <= r.lastFrame) break;   // de aca para atras todo es igual o mas viejo que lo leido
            D3D11_MAPPED_SUBRESOURCE m{};
            if (SUCCEEDED(g_dev.imm->Map(r.st[j], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
                r.last.assign((const float*)m.pData, (const float*)m.pData + bytes / 4); g_dev.imm->Unmap(r.st[j], 0); r.lastFrame = r.frame[j]; break; }
        }
        if (!r.last.empty()) { out = r.last; ok = true; }
        int prev = (r.i + 3) % 4;
        if (r.frame[prev] != now) { g_dev.imm->CopyResource(r.st[r.i], src); r.frame[r.i] = now; r.i = (r.i + 1) % 4; }   // una copia por cuadro
    }
    if (frameOut) *frameOut = r.lastFrame;
    src->Release();
    return ok;
}
// (21/10) cuadros desde la ultima copia de un cbuffer con nombre (-1 = nunca se copio)
int NamedAge(const std::string& name) {
    std::lock_guard<std::mutex> lk(s_namedMutex);
    auto it = s_named.find(name); if (it == s_named.end() || !it->second.buf || it->second.copied < 0) return -1;
    return s_presentNo.load() - it->second.copied;
}
const Rule* RuleForHash(uint64_t h) { auto it = g_cfg.rules.find(h); return it == g_cfg.rules.end() ? nullptr : &it->second; }
std::atomic<int> g_drawOrder{0};
std::atomic<int> g_lightAltFrames{0};   // (20/10) cuadros en que el sol llego por una variante no reemplazada de la luz
std::atomic<int> g_aliasDraws{0};   // dibujos redirigidos por alias en el cuadro (diagnostico)   // numero de orden del dibujo dentro del cuadro (se reinicia en Present)
// (25/10) luz diferida: cuales de las 21 variantes de la pasada corren en cada cuadro. 0..6 = sol con reemplazo de siempre
// (filtro de sombras, color del sol, piel); 7 = sol sin sombra (d0e9) y 8 = sol con mascara de pantalla (7dbbc), con
// reemplazo desde el 25/10; 9..20 = luces de foco / punto (torres, sin reemplazo). Si al mover la camara el juego pasa de
// unas a otras, el log lo deja escrito ("luz del sol: variantes ...").
static const uint64_t kSunPass[21] = {
    0xa748f426e9b978ccULL, 0xb8a29991599380d3ULL, 0xd51409f5f8d54cbdULL, 0xde643fcb8d6a6beaULL, 0xe9c13241d7bc9625ULL,
    0x1f623dbab7ee8019ULL, 0x230d92c927f58da3ULL,
    0xd0e9d81c09b3da66ULL, 0x7dbbc922c9ae74c5ULL,
    0x03b955fa0df6521bULL, 0x142c18270479a113ULL, 0x16c135a014d253b2ULL, 0x2a809f328b164023ULL, 0x5ae81b14fd75960eULL,
    0x68d58aa84c0256f5ULL, 0x7bd80690738f243fULL, 0x9ce9d8463800b399ULL, 0xc33576f12dd818bfULL, 0xca8a31c98b1584acULL,
    0xd5037e255559ea13ULL, 0xf9f5dc3e044ba823ULL };
static std::atomic<int> s_sunDraws[21];
static inline void SunPassNote(uint64_t h) {
    for (int i = 0; i < 21; i++) if (kSunPass[i] == h) { s_sunDraws[i].fetch_add(1, std::memory_order_relaxed); return; }
}
static inline int SunPassIndex(uint64_t h) { for (int i = 0; i < 9; i++) if (kSunPass[i] == h) return i; return -1; }   // solo las del sol
// (26/10) ancho de la vista mas grande en la que corrio una pasada del sol (cuadro anterior y este)
static std::atomic<UINT> s_sunVpMaxPrev{0}, s_sunVpMaxCur{0}; static std::atomic<int> s_sunSmallSkips{0};
static bool SunPassSmallView(ID3D11DeviceContext* c, uint64_t h) {
    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; UINT n = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    c->RSGetViewports(&n, vp);
    if (!n) return false;
    UINT w = (UINT)vp[0].Width, hgt = (UINT)vp[0].Height;
    UINT cur = s_sunVpMaxCur.load(); while (w > cur && !s_sunVpMaxCur.compare_exchange_weak(cur, w)) {}
    UINT ref = (std::max)(s_sunVpMaxPrev.load(), s_sunVpMaxCur.load());
    bool tooSmall = ref > 0 && w * 4 < ref * 3;
    { static std::mutex mx; static std::set<uint64_t> seen; uint64_t key = h ^ ((uint64_t)w << 1) ^ ((uint64_t)hgt << 20); bool first;
      { std::lock_guard<std::mutex> lk(mx); first = seen.size() < 40 && seen.insert(key).second; }
      if (first) Log("luz del sol: %08llx en una vista de %ux%u (la mas grande vista: %u)%s", (unsigned long long)(h >> 32), w, hgt, ref, tooSmall ? " -> vista chica: no se copia el sol" : ""); }
    if (tooSmall) s_sunSmallSkips++;
    return tooSmall;
}
static std::string SunPassList(uint32_t mask, const int* n) {
    std::string r; char b[48];
    for (int i = 0; i < 21; i++) if (mask & (1u << i)) { snprintf(b, sizeof(b), "%s%08llx%s x%d", r.empty() ? "" : ", ", (unsigned long long)(kSunPass[i] >> 32), i == 7 ? " (sol sin sombra)" : i == 8 ? " (sol con mascara)" : i > 8 ? " (foco)" : "", n ? n[i] : 0); r += b; }
    return r.empty() ? "ninguna" : r;
}
// (27/10) registro del sol: ajustes. Al empezar, los del sol / luz / sombras; despues cada valor que cambia (menu, teclas, recarga del ini)
static void SunSettingsPresent() {
    static double next = 0; double t = Now(); if (t < next) return; next = t + 1.0;
    static float prev[kParamCount][4]; static bool first = true;
    static const int kSunRows[] = { 4, 20, 21, 30, 34, 44, 47, 53, 54, 64, 83, 85, 87, 101, 102, 109, 111, 121, 134, 150, 151, 152, 296, 312, 313, 314 };
    const char* cn = "xyzw";
    if (first) {
        first = false; memcpy(prev, g_cfg.live, sizeof(prev));
        for (int r : kSunRows) SunLog("ajuste al empezar: fila %d = x %.3f  y %.3f  z %.3f  w %.3f", r, g_cfg.live[r][0], g_cfg.live[r][1], g_cfg.live[r][2], g_cfg.live[r][3]);
        return;
    }
    // filas que escribe la DLL sola (tiempos, sol medido, tabla de tonos de piel, vista, cuadro): no son ajustes
    auto autoRow = [](int r) { return r == 10 || r == 23 || r == 24 || r == 44 || r == 63 || r == 93 || (r >= 111 && r <= 114) || r == 157 || r == 159
                                      || (r >= 192 && r <= 254) || r == 255 || r == 257 || (r >= 264 && r <= 284) || r == 312; };
    // y por si falta alguna: un valor que cambia 5 segundos seguidos se toma como automatico y no se anota mas
    static unsigned char streak[kParamCount][4]; static bool muted[kParamCount][4];
    int lines = 0;
    for (int r = 0; r < kParamCount; r++) for (int c = 0; c < 4; c++) {
        float v = g_cfg.live[r][c];
        if (v == prev[r][c]) { streak[r][c] = 0; continue; }
        float was = prev[r][c]; prev[r][c] = v;
        if (autoRow(r) || muted[r][c]) continue;
        if (++streak[r][c] >= 5) { muted[r][c] = true; SunLog("ajuste: %c%d cambia solo (lo mueve la DLL): no se anota mas", cn[c], r); continue; }
        if (lines++ < 40) SunLog("ajuste: %c%d %.3f -> %.3f", cn[c], r, was, v);
    }
    if (lines > 40) SunLog("ajuste: %d cambios mas en este segundo (no anotados)", lines - 40);
}
static void SunPassPresent() {
    SunSettingsPresent();
    static uint32_t lastMask = 0xffffffffu; static int lines = 0; static double nextSum = 0;
    static int frames = 0, framesOrig = 0, framesNone = 0, switches = 0, sums = 0;
    int n[21]; uint32_t mask = 0;
    for (int i = 0; i < 21; i++) { n[i] = s_sunDraws[i].exchange(0, std::memory_order_relaxed); if (n[i]) mask |= 1u << i; }
    s_sunVpMaxPrev = s_sunVpMaxCur.exchange(0);
    frames++; if (!(mask & 0x1ffu)) framesNone++; else if (!(mask & 0x7fu)) framesOrig++;
    // solo cambian las del sol (los focos de noche entran y salen todo el tiempo); los cuadros sin ninguna pasada del sol
    // (uno suelto cada tanto) van solo al resumen de 10 s, si no llenaban el tope de lineas en un minuto
    if ((mask & 0x1ffu) && (mask & 0x1ffu) != lastMask) {
        if (lastMask != 0xffffffffu) switches++;
        if (lines < 200) { lines++; Log("luz del sol: variantes en el cuadro %d: %s", s_presentNo.load(), SunPassList(mask, n).c_str()); }
        lastMask = mask & 0x1ffu;
    }
    // (26/10) sol en mundo cuadro a cuadro contra el sol del archivo del cielo y contra la camara: en las repeticiones el sol de
    // la luz se aleja del cielo (log de 7894072). Si la direccion de la luz respecto de la camara queda fija mientras la camara
    // gira, el motor arma la luz por camara (iluminacion de repeticion); si salta solo en los cortes, es un preset por toma.
    static int dFrames = 0, dOff = 0, dJumps = 0, dLines = 0, dLastF = -1; static bool dWasOff = false;
    static float dMax = 0, azMin = 1e9f, azMax = -1e9f, camMin = 1e9f, camMax = -1e9f, relMin = 1e9f, relMax = -1e9f, dPrev[3] = {};
    static float resMin = 1e9f, resMax = -1e9f, upMin = 1e9f, upMax = -1e9f;   // (27/10) modelo: rumbo de la luz = rumbo del cielo + 2 x (camara + 90)
    { std::vector<float> L, S, K; int lf = -1, sf = -2;
      if (NamedCBRead("LightCB", L, &lf) && L.size() >= 4 && NamedCBRead("SceneCB", S, &sf) && S.size() >= 64 && lf == sf && lf != dLastF
          && NamedAge("SkyMatCB") >= 0 && NamedAge("SkyMatCB") <= 30 && NamedCBRead("SkyMatCB", K) && K.size() >= 16) {
          dLastF = lf;
          const float* M = &S[32]; float w[3], k[3] = { K[12], K[13], K[14] };
          for (int i = 0; i < 3; i++) w[i] = L[0] * M[i] + L[1] * M[4 + i] + L[2] * M[8 + i];
          float wn = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]), kn = sqrtf(k[0] * k[0] + k[1] * k[1] + k[2] * k[2]);
          if (wn > 1e-6f && kn > 1e-6f) {
              for (int i = 0; i < 3; i++) { w[i] /= wn; k[i] /= kn; }
              if (w[1] < 0) { w[0] = -w[0]; w[1] = -w[1]; w[2] = -w[2]; }
              float dev = acosf(std::clamp(w[0] * k[0] + w[1] * k[1] + w[2] * k[2], -1.0f, 1.0f)) * 57.2958f;
              float az = atan2f(w[2], w[0]) * 57.2958f, cam = atan2f(M[10], M[8]) * 57.2958f;   // fila 2 de m_view = eje z de la camara
              float rel = fmodf(az - cam + 540.0f, 360.0f) - 180.0f;
              // (27/10) log del 26/10 22:01: la luz del motor se separa del cielo 2 x (rumbo de la camara + 90) y la altura no cambia
              float res = fmodf(az - (atan2f(k[2], k[0]) * 57.2958f + 2.0f * (cam + 90.0f)) + 1080.0f, 360.0f) - 180.0f;
              resMin = (std::min)(resMin, res); resMax = (std::max)(resMax, res); upMin = (std::min)(upMin, w[1] - k[1]); upMax = (std::max)(upMax, w[1] - k[1]);
              if (dFrames > 0 && acosf(std::clamp(w[0] * dPrev[0] + w[1] * dPrev[1] + w[2] * dPrev[2], -1.0f, 1.0f)) * 57.2958f > 3.0f) dJumps++;
              for (int i = 0; i < 3; i++) dPrev[i] = w[i];
              SunWatchFrame(w, dev, lf);
              // (27/10) registro del sol: estado una vez por segundo (pedido de Agustin: todo lo del sol y la luz en un txt)
              { static double nextSt = 0; double tn = Now();
                if (tn >= nextSt) { nextSt = tn + 1.0;
                    float ex[4] = {}; if (S.size() >= 80) for (int i = 0; i < 4; i++) ex[i] = S[76 + i];
                    float c0[4] = {}, c1[4] = {}; for (int i = 0; i < 4; i++) { if (L.size() >= 8) c0[i] = L[4 + i]; if (L.size() >= 12) c1[i] = L[8 + i]; }
                    float skyAz = atan2f(k[2], k[0]) * 57.2958f;
                    SunLog("estado: cuadro %d | luz del juego en mundo (%.3f, %.3f, %.3f) altura %.1f rumbo %.0f | sol del cielo (%.3f, %.3f, %.3f) altura %.1f rumbo %.0f | diferencia %.1f grados (resto del modelo 2x %.0f) | camara rumbo %.0f, luz respecto de la camara %.0f | luz en vista (%.3f, %.3f, %.3f) | color del sol (%.3f, %.3f, %.3f | %.3f) color 1 (%.3f, %.3f, %.3f | %.3f) | exposicion (%.3f, %.3f, %.3f, %.3f) | pasadas: %s",
                        lf, w[0], w[1], w[2], asinf(std::clamp(w[1], -1.0f, 1.0f)) * 57.2958f, az, k[0], k[1], k[2], asinf(std::clamp(k[1], -1.0f, 1.0f)) * 57.2958f, skyAz,
                        dev, res, cam, rel, L[0], L[1], L[2], c0[0], c0[1], c0[2], c0[3], c1[0], c1[1], c1[2], c1[3], ex[0], ex[1], ex[2], ex[3], SunPassList(mask & 0x1ffu, n).c_str());
                } }   // (26/10) quien escribe el sol de las repeticiones (sunwatch.cpp)
              dFrames++; dMax = (std::max)(dMax, dev); if (dev > 5.0f) dOff++;
              azMin = (std::min)(azMin, az); azMax = (std::max)(azMax, az); camMin = (std::min)(camMin, cam); camMax = (std::max)(camMax, cam);
              relMin = (std::min)(relMin, rel); relMax = (std::max)(relMax, rel);
              bool off = dev > 5.0f;
              if (off != dWasOff && dLines < 300) { dLines++;
                  Log("luz del sol: cuadro %d, el sol de la luz %s del sol del cielo (%.1f grados; luz en mundo %.3f %.3f %.3f, rumbo %.0f, camara %.0f, luz respecto de la camara %.0f)",
                      lf, off ? "se separa" : "vuelve al", dev, w[0], w[1], w[2], az, cam, rel); }
              dWasOff = off;
          } } }
    double t = Now();
    if (t > nextSum) {
        if (nextSum > 0 && dFrames > 0 && sums <= 120)
            Log("luz del sol (10 s): %d cuadros medidos, %d con la luz a mas de 5 grados del cielo (maximo %.1f), %d saltos de mas de 3 grados entre cuadros; rumbo de la luz %.0f..%.0f, de la camara %.0f..%.0f, luz respecto de la camara %.0f..%.0f",
                dFrames, dOff, dMax, dJumps, azMin, azMax, camMin, camMax, relMin, relMax);
        if (nextSum > 0 && dFrames > 0 && sums <= 120 && dMax > 5.0f)
            Log("luz del sol (10 s): rumbo de la luz menos (rumbo del cielo + 2 x (camara + 90)) %.1f..%.1f grados; altura de la luz menos la del cielo %.3f..%.3f",
                resMin, resMax, upMin, upMax);
        resMin = upMin = 1e9f; resMax = upMax = -1e9f;
        dFrames = dOff = dJumps = 0; dMax = 0; azMin = camMin = relMin = 1e9f; azMax = camMax = relMax = -1e9f;
        if (nextSum > 0 && frames > 0 && framesNone < frames && sums++ < 120)
            Log("luz del sol (10 s): %d cuadros, %d solo con el sol sin sombra / con mascara (d0e9 / 7dbbc), %d sin pasada de sol, %d cambios de variante, LightCB desde la copia alternativa %d, pasadas del sol en vista chica (sin copiar) %d",
                frames, framesOrig, framesNone, switches, g_lightAltFrames.exchange(0), s_sunSmallSkips.exchange(0));
        // direccion del sol que usa la luz (m_lightDir0, espacio de vista) contra la direccion con la que se arma el shadow map
        // (la recta en la que las coordenadas x / y de m_shadowProjection no cambian). Si no coinciden, el lado iluminado y la
        // sombra proyectada no concuerdan. Tambien las dos en espacio mundo (deberian quedar quietas al mover la camara).
        { std::vector<float> L, S; int lf = -1, sf = -2;
          if (sums <= 120 && NamedCBRead("LightCB", L, &lf) && L.size() >= 4 && NamedCBRead("SceneCB", S, &sf) && S.size() >= 64 && lf == sf) {
              auto nrm = [](float* v) { float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (n > 1e-8f) { v[0] /= n; v[1] /= n; v[2] /= n; } return n; };
              float l[3] = { L[0], L[1], L[2] }; float a[3] = { S[48], S[49], S[50] }, b[3] = { S[52], S[53], S[54] };
              float d[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
              if (nrm(l) > 1e-8f && nrm(d) > 1e-8f) {
                  float c = fabsf(l[0] * d[0] + l[1] * d[1] + l[2] * d[2]); float ang = acosf((std::min)(c, 1.0f)) * 57.2958f;
                  const float* M = &S[32];
                  auto toW = [&](const float* v, float* w) { for (int i = 0; i < 3; i++) w[i] = v[0] * M[i] + v[1] * M[4 + i] + v[2] * M[8 + i]; if (w[1] < 0) { w[0] = -w[0]; w[1] = -w[1]; w[2] = -w[2]; } };
                  float lw[3], dw[3]; toW(l, lw); toW(d, dw);
                  Log("luz del sol: direccion de la luz (vista %.3f %.3f %.3f | mundo %.3f %.3f %.3f) y del shadow map (mundo %.3f %.3f %.3f): %.1f grados entre las dos",
                      l[0], l[1], l[2], lw[0], lw[1], lw[2], dw[0], dw[1], dw[2], ang);
              } } }
        nextSum = t + 10.0; frames = framesOrig = framesNone = switches = 0;
    }
}
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
        if (p[139][0] > 0.5f) p[104][0] = 1.0f;   // (20/10) pasto v5: necesita el pasto v3 (x104) prendido en la GPU
        p[147][3] = 1.0f;   // (20/10) w147: esta DLL iguala la cabeza de las caras genericas con la cara (reglas PS + VS)
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
    // (25/10 d) los hilos de dibujo leen altSRV sin lock (ParamSRVFor): con lugar reservado, agregar texturas no mueve el arreglo
    if (g_dev.altSRV.capacity() < 64) { g_dev.altSRV.reserve(64); g_dev.altTex.reserve(64); }
    for (int s = 0; s <= maxSet && s < 64; s++) {
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
struct CtxState { const ShaderEntry* ps = nullptr; const ShaderEntry* vs = nullptr; ID3D11Device* dev = nullptr; bool devKnown = false; ID3D11Buffer* lastHairCB = nullptr; bool clothCS = false; ID3D11DepthStencilView* shadowDSV = nullptr; };   // shadowDSV: (27/10 c) el DSV puesto es el shadow map del sol del juego (sombra de la cabeza)   // clothCS: el CS puesto es el simulador del mod de ropa (diagnostico en Dispatch)   // lastHairCB: b11 de la ultima cara dibujada en este contexto (la boca la hereda)
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
using PFN_DrawIdxInstInd = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
static PFN_DrawIdxInstInd o_DrawIdxInstInd[2];   // (25/10) DrawIndexedInstancedIndirect: lo usa el pasto v3 (lista de parcelas en la GPU)
static PFN_Present o_Present; static PFN_Resize o_Resize;
using PFN_ClearDSV = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
static PFN_ClearDSV o_ClearDSV[2];
// (25/10 e) listas de comandos: los dibujos de la piel grabados en un contexto diferido cuentan cuando su lista se ejecuta
using PFN_ExecCL = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);
using PFN_FinishCL = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, BOOL, ID3D11CommandList**);
static PFN_ExecCL o_ExecCL[2]; static PFN_FinishCL o_FinishCL[2];
// (26/10 g) cambios de targets y ClearState: la emision pendiente del pasto (despues de la base de la cancha) sale antes.
// (27/10 c) OMSetRenderTargets tambien dice si el DSV puesto es el shadow map del sol (sombra de la cabeza)
using PFN_OMSetRT = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
using PFN_OMSetRTU = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);
using PFN_ClearState = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
static PFN_OMSetRT o_OMSetRT[2]; static PFN_OMSetRTU o_OMSetRTU[2]; static PFN_ClearState o_ClearState[2];
// (25/10 e) diagnostico de la ropa contra el cuerpo: en el despacho del simulador del mod de ropa se copia su lista de
// instancias (t18: matriz de mundo de cada prenda) para comparar con los personajes del lote (cada 20 s, ver cuerpo.cpp)
using PFN_Dispatch = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT);
static PFN_Dispatch o_Dispatch[2];
template<int V> static void STDMETHODCALLTYPE hk_Dispatch(ID3D11DeviceContext* c, UINT x, UINT y, UINT z) {
    if (g_bodyDiagArmed.load(std::memory_order_relaxed) && Ctx(c).clothCS) BodyNoteClothDispatch(c, x);
    o_Dispatch[V](c, x, y, z);
}
template<int V> static void STDMETHODCALLTYPE hk_OMSetRT(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* v, ID3D11DepthStencilView* d) {
    GrassBaseFlush(c, 1, 0, v ? n : 0, v, d);
    o_OMSetRT[V](c, n, v, d);
    Ctx(c).shadowDSV = (n == 0 && HeadShadowIsGameShadow(d)) ? d : nullptr;
}
template<int V> static void STDMETHODCALLTYPE hk_OMSetRTU(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* v, ID3D11DepthStencilView* d, UINT us, UINT un, ID3D11UnorderedAccessView* const* u, const UINT* ic) {
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) GrassBaseFlush(c, 1, 0, v ? n : 0, v, d);   // solo UAVs: los targets no cambian
    o_OMSetRTU[V](c, n, v, d, us, un, u, ic);
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) Ctx(c).shadowDSV = (n == 0 && HeadShadowIsGameShadow(d)) ? d : nullptr;
}
template<int V> static void STDMETHODCALLTYPE hk_ClearState(ID3D11DeviceContext* c) {
    GrassBaseFlush(c, 5, 0);
    o_ClearState[V](c);
}
// (27/10) quien llena el cbuffer de la luz del sol. El log del 26/10 22:01 muestra que el motor arma m_lightDir0 con el rumbo
// del sol del cielo + 2 x (rumbo de la camara + 90): con la camara de TV casi no se nota y con otras camaras el sol (y su
// shadow map) pega de otro lado. La busqueda en memoria del sol en mundo (sunwatch.cpp) no encontro donde lo guarda el
// motor, asi que se va por el otro lado: el b3 de las pasadas del sol es LightCB; cada Map / UpdateSubresource de ese
// buffer anota la pila de llamadas del exe (hasta 10 marcos) y, al Unmap, lo que se escribio en m_lightDir0. Cada 30 s el
// log resume las pilas distintas ("luz: llena el cbuffer ...") para buscar la funcion en el volcado del exe (tools/re).
// [PES21FX] luz_rastrear = 0 lo apaga. Hasta 64 pilas por segundo (por si el buffer es un anillo compartido).
using PFN_Map = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
using PFN_Unmap = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
using PFN_UpdSub = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT);
static PFN_Map o_Map[2]; static PFN_Unmap o_Unmap[2]; static PFN_UpdSub o_UpdSub[2];
static std::atomic<ID3D11Resource*> s_ltBuf{nullptr};
static std::atomic<UINT> s_ltLen{0}, s_ltOff{0};
static std::atomic<int> s_ltBudget{64};
static std::atomic<bool> s_ltDone{false};   // tras 20 resumenes (10 min) se deja de rastrear
struct LtRec { uintptr_t fr[10]; int n = 0, count = 0, kind = 0; float l[3] = {}; bool haveL = false; };
static std::mutex s_ltMx; static std::vector<LtRec> s_ltRecs; static int s_ltMaps = 0, s_ltSkipped = 0;
static thread_local const void* t_ltMapped = nullptr; static thread_local int t_ltRec = -1;
static uintptr_t s_ltExeLo = 0, s_ltExeHi = 0;
static thread_local uintptr_t t_ltSrc = 0;
// la subida del juego (0x1417ae7ea): Map(buffer, 0, WRITE_DISCARD) en 0x1417ae856 y despues memcpy(mapeado, [rsp+48], n);
// desde el Map se lee ese origen en el marco del llamador (solo si el retorno tiene los bytes de 1.07.02). (27/10) en la PC
// el retorno inmediato del gancho no era el exe (hay otra capa entre el juego y Map): se desenrolla la pila con la tabla de
// excepciones hasta el marco cuyo retorno es 0x1417ae85a; ahi Rsp es el del llamador
static std::atomic<int> s_ltSrcOk{0}, s_ltSrcMiss{0}, s_ltArmReq{0};
static uintptr_t LightTraceSrc() {
    static const unsigned char kSig[9] = { 0x85, 0xc0, 0x78, 0x34, 0x45, 0x8b, 0x44, 0x34, 0x18 };
    if (!s_ltExeLo) return 0;
    const DWORD64 target = s_ltExeLo + (0x1417ae85aull - 0x140000000ull);
    if (target + 9 > s_ltExeHi || memcmp((const void*)target, kSig, 9) != 0) return 0;
    CONTEXT c; RtlCaptureContext(&c);
    for (int i = 0; i < 16 && c.Rip; i++) {
        DWORD64 img = 0; PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(c.Rip, &img, nullptr);
        if (!fe) { c.Rip = *(const DWORD64*)c.Rsp; c.Rsp += 8; }
        else { void* hd = nullptr; DWORD64 ef = 0; RtlVirtualUnwind(UNW_FLAG_NHANDLER, img, c.Rip, fe, &c, &hd, &ef, nullptr); }
        if (c.Rip == target) { s_ltSrcOk++; return *(const uintptr_t*)(c.Rsp + 0x48); }
    }
    s_ltSrcMiss++;
    return 0;
}
static int LightTraceNote(int kind) {   // kind 0 = Map, 1 = UpdateSubresource; devuelve el indice de la pila
    if (!s_ltExeLo) { HMODULE m = GetModuleHandleW(nullptr); auto* dh = (IMAGE_DOS_HEADER*)m; auto* nh = (IMAGE_NT_HEADERS*)((char*)m + dh->e_lfanew);
        s_ltExeLo = (uintptr_t)m; s_ltExeHi = s_ltExeLo + nh->OptionalHeader.SizeOfImage; }
    LtRec r; r.kind = kind;
    { std::lock_guard<std::mutex> lk(s_ltMx); s_ltMaps++; }
    if (s_ltBudget.fetch_sub(1, std::memory_order_relaxed) <= 0) { std::lock_guard<std::mutex> lk(s_ltMx); s_ltSkipped++; return -1; }
    void* bt[48]; USHORT n = RtlCaptureStackBackTrace(2, 48, bt, nullptr);
    for (USHORT i = 0; i < n && r.n < 10; i++) { uintptr_t a = (uintptr_t)bt[i]; if (a >= s_ltExeLo && a < s_ltExeHi) r.fr[r.n++] = a; }
    std::lock_guard<std::mutex> lk(s_ltMx);
    for (size_t i = 0; i < s_ltRecs.size(); i++) {
        auto& e = s_ltRecs[i];
        if (e.n == r.n && e.kind == kind && !memcmp(e.fr, r.fr, sizeof(uintptr_t) * r.n)) { e.count++; return (int)i; }
    }
    if (s_ltRecs.size() >= 24) return -1;
    r.count = 1; s_ltRecs.push_back(r); return (int)s_ltRecs.size() - 1;
}
static bool LightTraceRead(const void* data, UINT off, float* l) {   // sin objetos con destructor: __try
    __try { memcpy(l, (const char*)data + off, 3 * sizeof(float)); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void LightTraceValue(int rec, const void* data) {   // los primeros 3 floats de la ventana de LightCB (m_lightDir0)
    if (rec < 0 || !data) return;
    float l[3]; if (!LightTraceRead(data, s_ltOff.load(std::memory_order_relaxed), l)) return;
    std::lock_guard<std::mutex> lk(s_ltMx);
    if (rec < (int)s_ltRecs.size()) { memcpy(s_ltRecs[rec].l, l, sizeof l); s_ltRecs[rec].haveL = true; }
}
static void LightTraceSummary() {   // en Present
    static double next = 0; double t = Now();
    s_ltBudget.store(64, std::memory_order_relaxed);   // por cuadro
    if (!g_cfg.lightTrace || t < next) return;
    bool first = next == 0; next = t + 30.0; if (first) return;
    static int sums = 0; if (sums >= 20) { s_ltDone.store(true); s_ltBuf.store(nullptr); return; }
    std::lock_guard<std::mutex> lk(s_ltMx);
    if (s_ltRecs.empty()) { if (sums++ < 3) Log("luz: en 30 s nadie lleno el cbuffer de la luz con Map / UpdateSubresource (buffer %p, %u bytes desde %u)", (void*)s_ltBuf.load(), s_ltLen.load(), s_ltOff.load()); return; }
    sums++;
    Log("luz: el cbuffer de la luz (buffer %p, ventana %u bytes desde %u) se lleno %d veces en 30 s (%d sin pila por el tope); pilas distintas %zu",
        (void*)s_ltBuf.load(), s_ltLen.load(), s_ltOff.load(), s_ltMaps, s_ltSkipped, s_ltRecs.size());
    for (auto& e : s_ltRecs) {
        std::string st; char b[24];
        for (int i = 0; i < e.n; i++) { snprintf(b, sizeof b, "%s+0x%llx", i ? " <- " : "", (unsigned long long)(e.fr[i] - s_ltExeLo + 0x140000000ull)); st += b; }
        if (e.haveL) Log("  llena el cbuffer (%s, %d veces, m_lightDir0 %.3f %.3f %.3f): %s", e.kind ? "UpdateSubresource" : "Map", e.count, e.l[0], e.l[1], e.l[2], st.c_str());
        else Log("  llena el cbuffer (%s, %d veces): %s", e.kind ? "UpdateSubresource" : "Map", e.count, st.c_str());
    }
    Log("luz: origen de la copia encontrado %d veces, no encontrado %d; pedidos de vigilancia %d", s_ltSrcOk.exchange(0), s_ltSrcMiss.exchange(0), s_ltArmReq.exchange(0));
    s_ltRecs.clear(); s_ltMaps = s_ltSkipped = 0;
}
template<int V> static HRESULT STDMETHODCALLTYPE hk_Map(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, D3D11_MAP t, UINT f, D3D11_MAPPED_SUBRESOURCE* m) {
    HRESULT hr = o_Map[V](c, r, sub, t, f, m);
    if (r && r == s_ltBuf.load(std::memory_order_relaxed) && g_cfg.lightTrace && SUCCEEDED(hr) && m) {
        t_ltRec = LightTraceNote(0); t_ltMapped = m->pData; t_ltSrc = t_ltRec >= 0 ? LightTraceSrc() : 0; }   // con el mismo tope por cuadro que las pilas
    return hr;
}
template<int V> static void STDMETHODCALLTYPE hk_Unmap(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub) {
    if (t_ltMapped && r == s_ltBuf.load(std::memory_order_relaxed)) {
        LightTraceValue(t_ltRec, t_ltMapped);
        // (27/10) si lo copiado es la luz en espacio de vista (no el sol del cielo en mundo, que tambien pasa por este buffer),
        // se vigila quien escribe su origen: ahi se arma la luz que gira con la camara
        float l[3]; const float* S = g_cfg.live[312];
        if (t_ltSrc && LightTraceRead(t_ltMapped, 0, l)) {
            float n = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]), d = fabsf(l[0] * S[0] + l[1] * S[1] + l[2] * S[2]);
            if (n > 0.98f && n < 1.02f && S[3] > 0.5f && d < 0.999f * n) { s_ltArmReq++; SunWatchRequestArm(t_ltSrc); }
        }
        t_ltMapped = nullptr; t_ltRec = -1; t_ltSrc = 0; }
    o_Unmap[V](c, r, sub);
}
template<int V> static void STDMETHODCALLTYPE hk_UpdSub(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, const D3D11_BOX* box, const void* d, UINT rp, UINT dp) {
    if (r && r == s_ltBuf.load(std::memory_order_relaxed) && g_cfg.lightTrace) { int i = LightTraceNote(1); if (!box) LightTraceValue(i, d); }
    o_UpdSub[V](c, r, sub, box, d, rp, dp);
}
template<int V> static void STDMETHODCALLTYPE hk_ExecCL(ID3D11DeviceContext* c, ID3D11CommandList* l, BOOL r) {
    GrassBaseFlush(c, 4, 0);   // (26/10 g) lo pendiente de este contexto va antes que la lista
    BodyOnExecute(c, l);
    GrassOnExecuteCL(c, l);   // (26/10 d) pasto: demora grabacion -> ejecucion, listas anidadas
    o_ExecCL[V](c, l, r);
}
template<int V> static HRESULT STDMETHODCALLTYPE hk_FinishCL(ID3D11DeviceContext* c, BOOL r, ID3D11CommandList** l) {
    GrassBaseFlush(c, 3, 0);   // (26/10 g) la base pendiente se emite dentro de esta lista
    HRESULT hr = o_FinishCL[V](c, r, l);
    if (SUCCEEDED(hr) && l && *l) BodyOnFinish(c, *l);
    GrassOnFinishCL(c, (SUCCEEDED(hr) && l) ? *l : nullptr);   // (26/10 d) pasto: la grabacion termino (siempre, aunque falle)
    return hr;
}
// cejas (18/10): para dibujar las hebras hace falta saber si el juego usa profundidad invertida (limpia a 0) o normal (limpia a 1)
std::atomic<int> g_depthClearZero{0}, g_depthClearOne{0};
template<int V> static void STDMETHODCALLTYPE hk_ClearDSV(ID3D11DeviceContext* c, ID3D11DepthStencilView* v, UINT f, FLOAT d, UINT8 st) {
    if (f & D3D11_CLEAR_DEPTH) { if (d < 0.5f) g_depthClearZero++; else g_depthClearOne++; HeadShadowOnClear(c, v, d); }   // (27/10 c) el atlas de la cabeza se limpia con el shadow map del juego
    GrassBaseFlush(c, 2, 0);   // (26/10 g) antes de borrar la profundidad de la pasada
    o_ClearDSV[V](c, v, f, d, st);
}

// ---------------------------------------------------------------- registro de shaders
static const Rule* RuleFor(uint64_t h) {
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    auto it = g_cfg.rules.find(h);
    return it == g_cfg.rules.end() ? nullptr : &it->second;
}
static const std::vector<const Rule*>* PairRulesFor(uint64_t h) {   // (20/10) [rule <ps> vs <vs>]
    // (25/10 d) la lista no se modifica nunca despues de crearla y no se libera (son pocas y solo cambian al recargar): los hilos
    // de dibujo la recorren sin lock, y reasignar un std::vector mientras otro hilo lo recorre podia leer memoria liberada
    std::vector<const Rule*> out;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    for (auto& kv : g_cfg.pairRules) if (kv.first.first == h) out.push_back(&kv.second);
    return out.empty() ? nullptr : new std::vector<const Rule*>(std::move(out));
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

// (25/10) el simulador del mod de ropa (cloth_sim.hlsl, que compila el mod al arrancar): se reconoce por los nombres de sus
// recursos en la reflexion del bytecode. Al ponerlo se le enlazan los parametros (t120) y la tabla de la musculatura (t121).
static bool HasBytes(const void* code, SIZE_T len, const char* s) {
    const size_t n = strlen(s); const char* c = (const char*)code;
    if (!code || len < n) return false;
    for (SIZE_T i = 0; i + n <= len; i++) if (c[i] == s[0] && !memcmp(c + i, s, n)) return true;
    return false;
}
static void Register(ID3D11Device* dev, void* obj, const void* code, SIZE_T len, char type) {
    if (!obj || !code) return;
    ShaderEntry e; e.hash = Fnv64(code, len); e.type = type; e.dev = dev;
    if (type == 'c' && HasBytes(code, len, "g_legSkin") && HasBytes(code, len, "g_boneCache")) {
        e.clothSim = true;
        Log("ropa: simulador del mod de ropa reconocido (%016llx): recibe los parametros de PES21 FX y la tabla de la musculatura", (unsigned long long)e.hash);
    }
    e.code = std::make_shared<std::vector<uint8_t>>((const uint8_t*)code, (const uint8_t*)code + len);
    e.repl = BuildReplacement(dev, e.hash, type);
    e.rule = RuleFor(e.hash);
    if (type == 'p') e.pairRules = PairRulesFor(e.hash);
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
bool InputLayoutInfo(ID3D11InputLayout* il, std::vector<InputElemInfo>& out) {   // (25/10) cuerpo.cpp: formato de la malla de la piel
    std::lock_guard<std::mutex> lk(s_layoutMutex); auto it = s_layouts.find(il); if (it == s_layouts.end()) return false;
    out.clear(); for (auto& e : it->second) if (!e.cls) out.push_back({ e.sem, e.idx, e.fmt, e.slot, e.off });
    return true;
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
    // (25/10 d) el puntero a la regla se lee una sola vez: al recargar la configuracion ReloadShaders lo cambia (o lo deja en null)
    // mientras los hilos de dibujo lo usan
    const Rule* r = e ? e->rule : nullptr;
    if (r && r->paramSet >= 0 && r->paramSet < (int)g_dev.altSRV.size()) return g_dev.altSRV[r->paramSet];
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
// (25/10) pasto v3: la DLL enlaza los shaders de un dibujo del juego (entradas + objetos tal como estaban puestos) desde otro
// dibujo, de modo que BeforeDraw los vea como ese dibujo (reglas, t120). Devuelve lo que habia (con referencias a los
// objetos) para volver a ponerlo con la misma funcion; quien llama libera vsObj / psObj devueltos.
ShaderBind BindShadersRaw(ID3D11DeviceContext* c, const ShaderBind& b) {
    int V = c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED ? 1 : 0;
    PFN_SetVS fv = o_SetVS[V] ? o_SetVS[V] : (o_SetVS[0] ? o_SetVS[0] : o_SetVS[1]);
    PFN_SetPS fp = o_SetPS[V] ? o_SetPS[V] : (o_SetPS[0] ? o_SetPS[0] : o_SetPS[1]);
    CtxState& s = Ctx(c);
    ShaderBind old; old.vs = s.vs; old.ps = s.ps; old.vsObj = nullptr; old.psObj = nullptr;
    c->VSGetShader(&old.vsObj, nullptr, nullptr); c->PSGetShader(&old.psObj, nullptr, nullptr);
    s.vs = b.vs; s.ps = b.ps;
    if (fv) fv(c, b.vsObj, nullptr, 0); else c->VSSetShader(b.vsObj, nullptr, 0);
    if (fp) fp(c, b.psObj, nullptr, 0); else c->PSSetShader(b.psObj, nullptr, 0);
    return old;
}
template<int V> static void STDMETHODCALLTYPE hk_SetCS(ID3D11DeviceContext* c, ID3D11ComputeShader* s, ID3D11ClassInstance* const* ci, UINT n) {
    const ShaderEntry* e = FindShader(s);
    CatalogCS(e);
    Ctx(c).clothCS = e && e->clothSim;
    if (e && e->repl && !g_cfg.bypass) {
        o_SetCS[V](c, (ID3D11ComputeShader*)e->repl, ci, n);
        if (e->dev == g_dev.dev && g_dev.paramSRV) { ID3D11ShaderResourceView* v = g_dev.paramSRV; c->CSSetShaderResources(120, 1, &v); }
        return;
    }
    o_SetCS[V](c, s, ci, n);
    // (25/10) simulador del mod de ropa: parametros (t120: escena de menu, fuerza) y zonas de la musculatura (t121). Es fisica,
    // no un shader reemplazado: no depende de "Ver original". Sin esto el simulador lee ceros y esas mejoras quedan apagadas.
    if (e && e->clothSim && e->dev == g_dev.dev && g_dev.paramSRV) {
        // (25/10) t122..t124: el cuerpo real de cada personaje (cuerpo.cpp) para que la tela choque con la piel
        ID3D11ShaderResourceView* v[5] = { g_dev.paramSRV, MuscleClothSRV(), nullptr, nullptr, nullptr };
        BodyClothSRVs(v + 2);
        c->CSSetShaderResources(120, 5, v);
        BodySimSeen();
    }
}

// ---------------------------------------------------------------- reglas antes de cada dibujo
// lo que una regla enlaza se devuelve a como estaba despues del dibujo (como "post ps-t20 = ..." en 3DMigoto)
struct Saved { bool ps; bool srv; int slot; IUnknown* prev; };
struct DrawScope { Saved s[24]; int n = 0; FaceGenExtra brow; bool skip = false; ID3D11PixelShader* restorePS = nullptr; LodIA lod; GlassScope glass; RingScope ring; };   // glass: vidrio de los menus (glass.cpp)   // lod.count > 0: dibujar con la geometria de LOD0 (lod.cpp)

static void Remember(ID3D11DeviceContext* c, DrawScope& d, bool ps, bool srv, int slot) {
    if (d.n >= 24) return;   // (26/10 d) 12 -> 24: el torso ya enlaza 11 (t127, t16..t21, t24, t31..t33); lo que no entra no se devolveria
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

extern thread_local UINT t_drawVerts; extern thread_local UINT t_drawStart; extern thread_local INT t_drawBase; extern thread_local bool t_lodNoted;   // (definidas mas abajo)
static void RunRuleActions(ID3D11DeviceContext* c, const Rule* rule, bool ps, uint64_t hash, DrawScope& d);
static void RunRule(ID3D11DeviceContext* c, const ShaderEntry* e, DrawScope& d) { RunRuleActions(c, e->rule, e->type == 'p', e->hash, d); }
static void RunRuleActions(ID3D11DeviceContext* c, const Rule* rule, bool ps, uint64_t hash, DrawScope& d) {
    if (!rule) return;   // (25/10 d) una recarga pudo dejar la regla en null entre la comprobacion y esta llamada
    // (26/10) la pasada del sol tambien corre para una vista chica (otra camara del juego, mas chica que la principal): su
    // m_lightDir0 esta en el espacio de ESA camara y, copiada a LightCB, el sol de la composicion / el cielo / el pasto / la
    // fila 111 quedaba fijo respecto de la otra camara y giraba con la principal (log: 50-75 grados contra el shadow map).
    // Solo se copian el sol y su shadow map desde la vista mas grande de las pasadas del sol.
    bool sunSmall = false;
    if (ps && SunPassIndex(hash) >= 0) sunSmall = SunPassSmallView(c, hash);
    for (auto& a : rule->actions) {
        if (sunSmall && (a.kind == RuleAction::CopyCB || a.kind == RuleAction::RefSRV) && (a.name == "LightCB" || a.name == "LightCBAlt" || a.name == "ShadowMap")) continue;
        switch (a.kind) {
        case RuleAction::CopyCB: {
            // (20/10) con ID3D11DeviceContext1 el juego puede enlazar una ventana (first/num constantes) de un buffer grande:
            // se copia solo esa ventana; si el enlace es el buffer entero (lo normal) se copia todo como antes
            ID3D11Buffer* src = nullptr; UINT first = 0, num = 0;
            ID3D11DeviceContext1* c1 = nullptr;
            if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) {
                if (ps) c1->PSGetConstantBuffers1(a.slot, 1, &src, &first, &num); else c1->VSGetConstantBuffers1(a.slot, 1, &src, &first, &num);
                c1->Release();
            } else { if (ps) c->PSGetConstantBuffers(a.slot, 1, &src); else c->VSGetConstantBuffers(a.slot, 1, &src); }
            if (!src) break;
            D3D11_BUFFER_DESC sd; src->GetDesc(&sd);
            UINT off = first * 16, len = sd.ByteWidth;
            bool window = num && (off > 0 || num * 16 < sd.ByteWidth);
            if (window) len = off < sd.ByteWidth ? (std::min)(num * 16, sd.ByteWidth - off) & ~15u : 0;
            if (!len) { src->Release(); break; }
            if (a.name == "LightCB" && !s_ltDone.load(std::memory_order_relaxed)) { s_ltBuf.store(src, std::memory_order_relaxed); s_ltOff.store(window ? off : 0); s_ltLen.store(len); }   // (27/10) rastreo de quien la llena
            std::lock_guard<std::mutex> lk(s_namedMutex);
            auto& r = s_named[a.name];
            if (r.buf) { D3D11_BUFFER_DESC dd; r.buf->GetDesc(&dd); if (dd.ByteWidth != len) { r.buf->Release(); r.buf = nullptr; } }
            if (!r.buf && g_dev.dev) {
                D3D11_BUFFER_DESC nd = {}; nd.ByteWidth = len;
                nd.Usage = D3D11_USAGE_DEFAULT; nd.CPUAccessFlags = 0; nd.MiscFlags = 0; nd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; nd.StructureByteStride = 0;
                g_dev.dev->CreateBuffer(&nd, nullptr, &r.buf);
            }
            if (r.buf) {
                if (window) { D3D11_BOX bx = { off, 0, 0, off + len, 1, 1 }; c->CopySubresourceRegion(r.buf, 0, 0, 0, 0, src, 0, &bx); }
                else c->CopyResource(r.buf, src);
                r.copied = s_presentNo.load(); r.cleared = false;
            }
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
            bool fgFound = FaceGenLookup(c, &fa, &fb, &ex);
            // (20/10) LOD de caras: anotar la malla de este dibujo y, con LOD0 forzado, cambiarla por la de mas detalle
            if (g_lodAny && ex.key && t_drawVerts > 0) { const ShaderEntry* vse = Ctx(c).vs; t_lodNoted = true; LodNoteFace(c, ex.key, ex.t0, ex.id, vse ? vse->hash : 0, hash, t_drawVerts, t_drawStart, t_drawBase, &d.lod); }
            if (fgFound) {
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
thread_local UINT t_drawVerts = 0; thread_local UINT t_drawStart = 0; thread_local INT t_drawBase = 0; thread_local bool t_lodNoted = false; thread_local LodIA t_grassSub;   // (20/10) DrawIndexed en curso (LOD de caras)   // vertices del dibujo en curso (para "solo esta capa" del inspector)   // el dibujo en curso es una parcela lejana del pasto (lo marca GrassOnDraw)
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
    if (g_skinDiagArmed.load(std::memory_order_relaxed)) SkinDiagDraw(c, ps ? ps->hash : 0, vs ? vs->hash : 0, t_drawVerts);   // (24/10) skintone.cpp
    if (g_toneChkArmed.load(std::memory_order_relaxed)) SkinToneCheckDraw(c, ps ? ps->hash : 0, vs ? vs->hash : 0);   // (24/10 c) punto de los dibujos de piel
    GlassBeforeDraw(c, ps ? ps->hash : 0, vs ? vs->hash : 0, t_drawVerts, d.glass);   // (24/10) vidrio de los menus: GS + PS propios (se devuelven en FX_DRAW)
    RingBeforeDraw(c, ps ? ps->hash : 0, vs ? vs->hash : 0, d.ring);   // (24/10 c) aro del musculo: variante de la piel / dibujo extra encima del jugador
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
        const Rule* psr = ps ? ps->rule : nullptr;
        ID3D11ShaderResourceView* v = (psr && psr->paramSet >= 0) ? ParamSRVFor(ps) : ParamSRVFor(vs);
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
    if (ps) SunPassNote(ps->hash);   // (25/10) variantes de la luz del sol por cuadro (log)
    if (ps) GrassDiagState(c, ps->hash);   // (26/10 f) stencil de las pasadas del sol y de la base (log, 3 veces cada una)
    if (!g_cfg.optRules) { RingFaceBind(c, d.ring); return false; }
    if ((ps && ps->rule) || (vs && vs->rule)) {
        if (!s_firstRule.exchange(true)) Log("primer dibujo con regla");
    }
    if (ps && ps->rule) RunRule(c, ps, d);
    if (vs && vs->rule) RunRule(c, vs, d);
    // (20/10) reglas por pareja PS + VS: p.ej. la piel del cuerpo (aa7d1833) cuando dibuja la cabeza de una cara generica (VS faab87ee)
    if (ps && vs) if (const std::vector<const Rule*>* prs = ps->pairRules) for (const Rule* pr : *prs) if (pr->vsCond == vs->hash) RunRuleActions(c, pr, true, ps->hash, d);
    // (25/10) manos de TorsoBody (manos.cpp): datos por vertice de la mano en t126 del VS (cuaterniones duales + correctivas)
    if (vs && vs->repl) {
        bool arm = false, hand = false;
        ID3D11ShaderResourceView* hv = HandDataFor(vs->hash, t_drawVerts, t_drawStart, t_drawBase); hand = hv != nullptr;
        if (!hv) hv = LimbDataFor(vs->hash, t_drawVerts, t_drawStart, t_drawBase, &arm);   // (26/10, v12.7) brazos y piernas
        if (hv) { Remember(c, d, false, true, 126); c->VSSetShaderResources(126, 1, &hv); }
        // (26/10, v12.7) venas: textura en t29 del PS de la piel para los brazos y las manos (no en la sombra)
        if ((hand || arm) && vs->hash == 0x1c02a92ab4ae3362ULL) if (ID3D11ShaderResourceView* vt = VeinTexture()) { Remember(c, d, true, true, 29); c->PSSetShaderResources(29, 1, &vt); }
        // (27/10) brazos, manos y muslos de TorsoBody para las caras reales (brazos.cpp): la textura de la cara (2048) en t0 se
        // cambia por arm_N / thigh_N de TorsoBody del tono del jugador; los dibujos del torso solo anotan su tono
        if (ID3D11ShaderResourceView* at = ArmsTextureFor(c, vs->hash, t_drawVerts)) { Remember(c, d, true, true, 0); c->PSSetShaderResources(0, 1, &at); }
        // (26/10) variantes robusto / atletico de TorsoBody (manos.cpp + pes_variants.hlsli): datos por vertice del torso y de los
        // brazos en t127 del VS (piel y sombras); en el torso, los mapas del PS de la piel en t16 / t17 / t18
        bool vTorso = false;
        if (ID3D11ShaderResourceView* vd = VariantDataFor(vs->hash, t_drawVerts, &vTorso)) {
            Remember(c, d, false, true, 127); c->VSSetShaderResources(127, 1, &vd);
            ID3D11ShaderResourceView* vm[3] = {};
            if (vTorso && vs->hash == 0x1c02a92ab4ae3362ULL && VariantTextures(vm)) {
                Remember(c, d, true, true, 16); Remember(c, d, true, true, 17); Remember(c, d, true, true, 18);
                c->PSSetShaderResources(16, 3, vm);
            }
        }
        // (26/10) columna marcada: en todos los torsos (aunque no haya variantes en uso)
        if (ID3D11ShaderResourceView* sp = SpineTextureFor(vs->hash, t_drawVerts)) { Remember(c, d, true, true, 19); c->PSSetShaderResources(19, 1, &sp); }
        // (26/10 b) base esculpida: la correccion del normal map del torso (t20)
        if (ID3D11ShaderResourceView* bt = BaseTextureFor(vs->hash, t_drawVerts)) { Remember(c, d, true, true, 20); c->PSSetShaderResources(20, 1, &bt); }
        {   // (26/10 b) piel real: zonas del torso (t21 brillo / rugosidad, t24 translucidez / rubor)
            ID3D11ShaderResourceView* sk[2] = {};
            if (SkinTexturesFor(vs->hash, t_drawVerts, sk)) { Remember(c, d, true, true, 21); Remember(c, d, true, true, 24); c->PSSetShaderResources(21, 1, &sk[0]); c->PSSetShaderResources(24, 1, &sk[1]); }
        }
        // (26/10 c) pelvis sin short (SinCamiseta, tecla 4): albedo propio en t23 (su presencia le dice al PS que es la pelvis)
        if (ID3D11ShaderResourceView* pa = PelvisAlbedoFor(vs->hash, t_drawVerts)) {
            Remember(c, d, true, true, 23); c->PSSetShaderResources(23, 1, &pa);
            ID3D11ShaderResourceView* pd[2] = {};   // (26/10 d) y su detalle de piel real (t34 normal, t35 albedo)
            if (PelvisDetailFor(vs->hash, t_drawVerts, pd)) { Remember(c, d, true, true, 34); Remember(c, d, true, true, 35); c->PSSetShaderResources(34, 2, pd); }
        }
        {   // (26/10 d) tope del relieve: inclinacion de la malla de cada version (t31 base, t32 atletico, t33 robusto)
            ID3D11ShaderResourceView* tl[3] = {};
            if (TiltTexturesFor(vs->hash, t_drawVerts, tl)) { Remember(c, d, true, true, 31); Remember(c, d, true, true, 32); Remember(c, d, true, true, 33); c->PSSetShaderResources(31, 3, tl); }
        }
    }
    // 19/10: la boca (63fab7) hereda el b11 (tipo de pelo + semilla de la cara) de la ultima cara dibujada en este contexto:
    // variacion de dientes/encias por jugador
    if (ps && ps->hash == 0x63fab7735eee149cULL && ps->repl && s.lastHairCB) { Remember(c, d, true, false, 11); c->PSSetConstantBuffers(11, 1, &s.lastHairCB); }
    // (27/10 c) sombra de la cabeza: el atlas propio (t30) a los 7 shaders del sol reemplazados
    if (ps && ps->repl) { int si = SunPassIndex(ps->hash); if (si >= 0 && si < 7) if (ID3D11ShaderResourceView* hs = HeadShadowSRV()) { Remember(c, d, true, true, 30); c->PSSetShaderResources(30, 1, &hs); } }
    // 20/10: desgaste de la cancha en vivo: la posicion de cada jugador sale del dibujo de su short / medias (b5 del VS)
    if (ps && ps->hash == 0x55297925e0c66085ULL) PitchWearNotePlayer(c);
    // 19/10: SSS separable en pantalla antes de la composicion: la irradiancia (t9) llega ya difundida sobre la piel
    if (ps && (ps->hash == 0x619b86cb5691cdceULL || ps->hash == 0xf807e74d5b900a15ULL) && ps->repl) {
        if (ID3D11ShaderResourceView* v = SSSSBeforeComposition(c)) { Remember(c, d, true, true, 9); c->PSSetShaderResources(9, 1, &v);
            if (ID3D11ShaderResourceView* sp = SSSSSpecSRV()) { Remember(c, d, true, true, 10); c->PSSetShaderResources(10, 1, &sp); } }   // (27/10 b) brillo con la luz de prueba del editor
    }
    // 20/10: GTAO (HBAO actual) para la luz global y la composicion: t27 (se calcula una vez por cuadro)
    if (ps && (ps->hash == 0x619b86cb5691cdceULL || ps->hash == 0xfda6da09737592b1ULL) && ps->repl) {
        ID3D11ShaderResourceView* v = GTAOBeforeComposition(c, s_presentNo.load());
        if (!v && g_cfg.live[132][0] > 0.5f) v = GTAONeutralSRV();   // (24/10 g) escena sin GTAO (editor): sin oclusion, no 0
        if (v) { Remember(c, d, true, true, 27); c->PSSetShaderResources(27, 1, &v); }
    }
    // 21/10: SSGI temporal para la composicion: t26 (media resolucion, acumulado en el tiempo)
    if (ps && ps->hash == 0x619b86cb5691cdceULL && ps->repl) {
        if (ID3D11ShaderResourceView* v = SSGIBeforeComposition(c, s_presentNo.load())) { Remember(c, d, true, true, 26); c->PSSetShaderResources(26, 1, &v); }
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
    // (27/10) cuello de las caras reales (brazos.cpp + pes_collar.hlsli): torso_N de TorsoBody en t29 de los PS de la cara
    if (ps && (ps->hash == 0x5ce3ed8702271bc1ULL || ps->hash == 0xce188ae110354aeaULL))
        if (ID3D11ShaderResourceView* tt = ArmsTorsoTextureForFace()) { Remember(c, d, true, true, 29); c->PSSetShaderResources(29, 1, &tt); }
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
    RingFaceBind(c, d.ring);   // (24/10 e) medicion del color de la cara: u7 (despues de las cejas en u5 / u6)
    return d.n > 0;
}
#define FX_DRAW(verts, kind, inst, call) if (g_catalogHook) { CtxState& cs_ = Ctx(c); CatalogDraw(c, cs_.vs, cs_.ps, verts, kind, inst); } t_drawVerts = (UINT)(verts); DrawScope ds; bool r = BeforeDraw(c, ds); \
    if ((kind) == 1 && g_lodAny) { if (!t_lodNoted && !ds.lod.count) { CtxState& cl_ = Ctx(c); LodNoteAnyDraw(c, cl_.vs ? cl_.vs->hash : 0, cl_.ps ? cl_.ps->hash : 0, verts, t_drawStart, t_drawBase, &ds.lod); } t_lodNoted = false; } \
    if ((kind) == 1 && t_grassSub.count) { if (!ds.lod.count) ds.lod = t_grassSub; else t_grassSub.Release(); t_grassSub = LodIA(); } \
    if (!ds.skip) { call; } if (ds.restorePS) { c->PSSetShader(ds.restorePS, nullptr, 0); ds.restorePS->Release(); } if (ds.glass.on) GlassAfterDraw(c, ds.glass); if (ds.ring.on) RingAfterDraw(c, ds.ring); \
    if (ds.ring.overlay && !ds.skip) { RingOverlayBegin(c, ds.ring); call; RingOverlayEnd(c, ds.ring); } \
    if (g_meterHook) { CtxState& cm_ = Ctx(c); if (cm_.ps && cm_.ps->hash == 0x619b86cb5691cdceULL) LightMeterAfterComposition(c); } \
    if (g_inspectOn) { CtxState& ci_ = Ctx(c); if (InspectBegin(c, ci_.ps, ci_.vs, kind, verts, inst)) { call; InspectEnd(c); } } \
    if (r) Restore(c, ds); if (ds.brow.seedsUAV) BrowDrawAfterFace(c, &ds.brow); if (ds.lod.count) ds.lod.Release();
// (26/10 g) antes de cada dibujo que no sea de la base de la cancha: la emision pendiente del pasto del mod (ver GrassBaseFlush)
#define GRASS_FLUSH(c) if (g_grassPendN.load(std::memory_order_relaxed) > 0) { CtxState& cf_ = Ctx(c); GrassBaseFlush(c, 0, cf_.ps ? cf_.ps->hash : 0); }
// (27/10 c) sombra de la cabeza: el mismo dibujo de la pasada de sombras del juego (cascada mas cercana) se repite en el atlas
// propio, una vez por cabeza, con un b2 (g_vsScene) cuya m_projectionView es la caja ortografica de esa cabeza (headshadow.cpp)
template<int V> static void HeadShadowReplay(ID3D11DeviceContext* c, ID3D11DepthStencilView* gameDSV, UINT a, UINT b, INT d) {
    int n = HeadShadowSlots(); ID3D11DepthStencilView* dsv = HeadShadowDSV();
    if (n <= 0 || !dsv || !HeadShadowIsNearCascade(c, gameDSV)) return;
    ID3D11RenderTargetView* rt[8] = {}; ID3D11DepthStencilView* oldDsv = nullptr; c->OMGetRenderTargets(8, rt, &oldDsv);
    ID3D11Buffer* oldB2 = nullptr; c->VSGetConstantBuffers(2, 1, &oldB2);
    D3D11_VIEWPORT vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetViewports(&nvp, vps);
    D3D11_RECT scs[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; UINT nsc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetScissorRects(&nsc, scs);
    o_OMSetRT[V](c, 0, nullptr, dsv);
    for (int i = 0; i < n; i++) {
        ID3D11Buffer* cb = HeadShadowCB(i); if (!cb) continue;
        D3D11_VIEWPORT vp; HeadShadowViewport(i, vp);
        D3D11_RECT rc{ (LONG)vp.TopLeftX, (LONG)vp.TopLeftY, (LONG)(vp.TopLeftX + vp.Width), (LONG)(vp.TopLeftY + vp.Height) };
        c->RSSetViewports(1, &vp); c->RSSetScissorRects(1, &rc);
        c->VSSetConstantBuffers(2, 1, &cb);
        o_DrawIdx[V](c, a, b, d);
    }
    g_headShadowReplays.fetch_add(n, std::memory_order_relaxed);
    o_OMSetRT[V](c, 8, rt, oldDsv);
    c->VSSetConstantBuffers(2, 1, &oldB2);
    if (nvp) c->RSSetViewports(nvp, vps); if (nsc) c->RSSetScissorRects(nsc, scs);
    for (auto* r : rt) if (r) r->Release(); if (oldDsv) oldDsv->Release(); if (oldB2) oldB2->Release();
}
// (20/10) LOD0 forzado: el dibujo sale con los buffers/cuentas de la malla de mas detalle y se devuelve el estado del juego
template<int V> static void LodDrawSub(ID3D11DeviceContext* c, DrawScope& ds) { LodIA sv; LodBind(c, ds.lod, sv); o_DrawIdx[V](c, ds.lod.count, ds.lod.start, ds.lod.base); LodRestore(c, sv, ds.lod); }
template<int V> static void STDMETHODCALLTYPE hk_DrawIdx(ID3D11DeviceContext* c, UINT a, UINT b, INT d) {
    GRASS_FLUSH(c)
    { CtxState& cg = Ctx(c); const ShaderEntry* gvs = cg.vs;
      if (g_grassProbeOn) CatalogGrassDraw(c, gvs, a, b, d);
      if (gvs && gvs->hash == 0x498b30bad256e8edULL) GrassOnDraw(c, gvs, cg.ps, a, b, d);
      if (gvs && gvs->hash == 0x1c02a92ab4ae3362ULL) BodyOnDraw(c, a, b, d);   // (25/10) piel del cuerpo -> la ropa choca con ella (es fisica: no depende de "Ver original")
      if (g_autoDumpAny) InspectAutoDump(c, cg.ps); }
    t_drawStart = b; t_drawBase = d;
    GrassStScope gst; GrassGameStates(c, gst);   // (26/10 g) solo el pasto del juego del G-buffer (lo marca GrassOnDraw)
    FX_DRAW(a, 1, 1, (ds.lod.count ? LodDrawSub<V>(c, ds) : o_DrawIdx[V](c, a, b, d)))
    GrassGameStatesRestore(c, gst);
    { CtxState& ch_ = Ctx(c); if (ch_.shadowDSV && !ds.skip) HeadShadowReplay<V>(c, ch_.shadowDSV, a, b, d); }   // (27/10 c) sombra de la cabeza
    // (25/10) pasto v3: al terminar la base de la cancha (PS 5e92 / c956, existe en todas las camaras) se emite el pasto del mod
    { CtxState& cb_ = Ctx(c); if (cb_.ps && (cb_.ps->hash == 0x5e92f2343848915aULL || cb_.ps->hash == 0xc9564b9b155f0a49ULL)) GrassOnBaseDraw(c); } }
template<int V> static void STDMETHODCALLTYPE hk_Draw(ID3D11DeviceContext* c, UINT a, UINT b) { GRASS_FLUSH(c) FX_DRAW(a, 0, 1, o_Draw[V](c, a, b)) }
// (25/10) dibujo indirecto (pasto v3): pasa por BeforeDraw como cualquier dibujo (reglas, t120); vertices e instancias desconocidos
template<int V> static void STDMETHODCALLTYPE hk_DrawIdxInstInd(ID3D11DeviceContext* c, ID3D11Buffer* a, UINT b) { GRASS_FLUSH(c) FX_DRAW(0, 3, 0, o_DrawIdxInstInd[V](c, a, b)) }
template<int V> static void STDMETHODCALLTYPE hk_DrawIdxInst(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, INT e, UINT f) { GRASS_FLUSH(c) FX_DRAW(a, 3, b, o_DrawIdxInst[V](c, a, b, d, e, f)) }
template<int V> static void STDMETHODCALLTYPE hk_DrawInst(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, UINT e) { GRASS_FLUSH(c) FX_DRAW(a, 2, b, o_DrawInst[V](c, a, b, d, e)) }

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
    n += Hook(vt[39], (void*)&hk_DrawIdxInstInd<V>, (void**)&o_DrawIdxInstInd[V], "DrawIndexedInstancedIndirect");
    n += Hook(vt[69], (void*)&hk_SetCS<V>, (void**)&o_SetCS[V], "CSSetShader");
    n += Hook(vt[53], (void*)&hk_ClearDSV<V>, (void**)&o_ClearDSV[V], "ClearDepthStencilView");
    n += Hook(vt[41], (void*)&hk_Dispatch<V>, (void**)&o_Dispatch[V], "Dispatch");
    n += Hook(vt[58], (void*)&hk_ExecCL<V>, (void**)&o_ExecCL[V], "ExecuteCommandList");
    n += Hook(vt[114], (void*)&hk_FinishCL<V>, (void**)&o_FinishCL[V], "FinishCommandList");
    n += Hook(vt[33], (void*)&hk_OMSetRT<V>, (void**)&o_OMSetRT[V], "OMSetRenderTargets");   // (26/10 g) pasto: emision pendiente
    n += Hook(vt[34], (void*)&hk_OMSetRTU<V>, (void**)&o_OMSetRTU[V], "OMSetRenderTargetsAndUnorderedAccessViews");
    n += Hook(vt[110], (void*)&hk_ClearState<V>, (void**)&o_ClearState[V], "ClearState");
    if (g_cfg.lightTrace) {   // (27/10) quien llena el cbuffer de la luz
        n += Hook(vt[14], (void*)&hk_Map<V>, (void**)&o_Map[V], "Map");
        n += Hook(vt[15], (void*)&hk_Unmap<V>, (void**)&o_Unmap[V], "Unmap");
        n += Hook(vt[48], (void*)&hk_UpdSub<V>, (void**)&o_UpdSub[V], "UpdateSubresource");
    }
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
    // m_lightDir0 (b3) esta en espacio de vista; se pasa a mundo con m_view (b2: offset 32 floats; fila j = eje j de la camara
    // en el mundo), como la fila 111. (26/10) antes se hacia el producto con la fila 1 (eje de la camara en mundo) contra la
    // luz en vista: mezclaba espacios y la altura cambiaba con la inclinacion de la camara
    if (L.size() < 4 || S.size() < 48) { if (azOut) *azOut = 0; return 0; }
    float lx = L[0], ly = L[1], lz = L[2]; float n = sqrtf(lx * lx + ly * ly + lz * lz); if (n < 1e-6f) return 0; lx /= n; ly /= n; lz /= n;
    const float* M = &S[32];
    float wx = lx * M[0] + ly * M[4] + lz * M[8], wy = lx * M[1] + ly * M[5] + lz * M[9], wz = lx * M[2] + ly * M[6] + lz * M[10];
    if (wy < 0) { wx = -wx; wy = -wy; wz = -wz; }
    float e = asinf(std::clamp(wy, -1.f, 1.f)) * 57.2958f;
    if (azOut) { *azOut = atan2f(wx, wz) * 57.2958f; if (*azOut < 0) *azOut += 360.f; }
    return e;
}
// (20/10) capturas A/B: con auto_captura_ab = 1, en cada captura se guarda ademas un par: A = todo como esta, B = el mismo
// cuadro (3 despues) con UN grupo del menu apagado (su interruptor), ciclando por todos los grupos activos. Asi se ve el
// efecto real de cada funcion en la misma escena sin que nadie toque el menu. capturas\ab\HHMMSS_<grupo>_A.jpg / _B.jpg
struct ABState { bool on = false; int idx = -1, comp = 0; float prev = 0; int frames = 0; int stage = 0; std::wstring base; std::string title; };   // stage 1 = esperando B, 2 = esperando A2
static ABState s_ab; static int s_abNext = 0;
static std::wstring SlugW(const std::string& t) {
    std::wstring o; for (unsigned char ch : t) { if (o.size() >= 28) break; if (isalnum(ch)) o += (wchar_t)tolower(ch); else if (ch == ' ' || ch == '_' || ch == '-') { if (!o.empty() && o.back() != L'_') o += L'_'; } }
    while (!o.empty() && o.back() == L'_') o.pop_back(); return o.empty() ? L"grupo" : o;
}
void AutoCapturePresent(ID3D11Texture2D* frame) {
    static double last = 0; static int count = 0; static bool dirOk = false;
    if (!g_cfg.autoCap || !frame || !g_dev.imm) return;
    if (s_ab.on) {   // (20/10) A -> B (grupo apagado 3 cuadros) -> A2 (prendido de nuevo 3 cuadros): si A y A2 difieren, la camara se movio
        if (++s_ab.frames >= 3) {
            EnsureComInit(); s_ab.frames = 0;
            if (s_ab.stage == 1) {
                HRESULT hr = AsyncShot(frame, s_ab.base + L"_B.jpg") ? S_OK : E_FAIL;
                if (s_ab.idx >= 0) g_cfg.live[s_ab.idx][s_ab.comp] = s_ab.prev;
                if (FAILED(hr)) { Log("captura A/B: no se pudo guardar B (0x%08x)", (unsigned)hr); s_ab = ABState(); }
                else s_ab.stage = 2;
            } else {
                AsyncShot(frame, s_ab.base + L"_A2.jpg");
                s_ab = ABState();
            }
        }
        return;
    }
    if (g_cfg.live[93][3] < 0.5f) return;                 // solo en partido
    double t = Now(); if (t - last < g_cfg.autoCapSec) return; last = t;
    if (count >= g_cfg.autoCapMax) return;
    std::wstring dir = g_cfg.baseDir + L"capturas"; if (!dirOk) { CreateDirectoryW(dir.c_str(), nullptr); CreateDirectoryW((dir + L"\\ab").c_str(), nullptr); dirOk = true; }
    static int cycle = 0; cycle++;
    if (g_cfg.autoCapAB && (cycle & 1) && !g_cfg.groups.empty()) {   // un ciclo si, uno no: la captura normal (con luz.txt) sigue
        // siguiente grupo con interruptor prendido (no diagnostico, no ropa: esa necesita recompilar)
        // orden: primero Iluminacion, despues Cara / Piel / Cancha, despues el resto
        static std::vector<int> order; if (order.size() != g_cfg.groups.size()) { order.clear();
            auto pri = [](const std::string& sec) { if (sec.rfind("Ilumin", 0) == 0) return 0; if (sec.rfind("Cara", 0) == 0 || sec.rfind("Piel", 0) == 0 || sec.rfind("Cancha", 0) == 0 || sec.rfind("Sombras", 0) == 0) return 1; return 2; };
            for (int p = 0; p < 3; p++) for (int i = 0; i < (int)g_cfg.groups.size(); i++) if (pri(g_cfg.groups[i].section) == p) order.push_back(i); }
        int n = (int)order.size(); int pick = -1;
        for (int k = 0; k < n; k++) { int gi = order[(s_abNext + k) % n]; const MenuGroup& g = g_cfg.groups[gi];
            if (g.tIdx < 0 || g.tIdx >= kParamCount) continue; if (g.section.rfind("Diagn", 0) == 0 || g.section.rfind("Ropa", 0) == 0) continue;
            if (g_cfg.live[g.tIdx][g.tComp] < 0.5f) continue; pick = gi; s_abNext = (s_abNext + k + 1) % n; break; }
        if (pick >= 0) {
            const MenuGroup& g = g_cfg.groups[pick];
            SYSTEMTIME st; GetLocalTime(&st); wchar_t nm[64]; swprintf(nm, 64, L"%02d%02d%02d_", st.wHour, st.wMinute, st.wSecond);
            s_ab.base = dir + L"\\ab\\" + nm + SlugW(g.title);
            EnsureComInit();
            HRESULT hr = AsyncShot(frame, s_ab.base + L"_A.jpg") ? S_OK : E_FAIL;
            if (SUCCEEDED(hr)) { s_ab.on = true; s_ab.stage = 1; s_ab.idx = g.tIdx; s_ab.comp = g.tComp; s_ab.prev = g_cfg.live[g.tIdx][g.tComp]; s_ab.frames = 0; s_ab.title = g.title; g_cfg.live[g.tIdx][g.tComp] = 0.0f;
                static bool said = false; if (!said) { said = true; Log("captura A/B: pares en capturas\\ab\\ (A = todo, B = un grupo apagado), primero '%s'", g.title.c_str()); }
                return; }
        }
    }
    std::vector<float> L, S; bool lok = NamedCBRead("LightCB", L), sok = NamedCBRead("SceneCB", S);
    float az = 0, el = lok && sok ? ReadElev(L, S, &az) : 0;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t name[96]; swprintf(name, 96, L"%02d%02d%02d_sol%02.0f.jpg", st.wHour, st.wMinute, st.wSecond, el);
    EnsureComInit();
    HRESULT hr = AsyncShot(frame, dir + L"\\" + name) ? S_OK : E_FAIL;   // (20/10) asincronica: sin tiron
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
        fprintf(f, "mod: sol z4=%.2f w4=%.2f curva x65=%.0f (%.2f %.2f %.2f) amb(%.2f %.2f) niebla(%.2f %.2f) calidez %.2f azul %.2f | cielo w47=%.0f x47=%.2f | rebote w42=%.0f x42=%.2f | sssss w95=%.0f | brillo piel w103=%.0f | pupila %.2f",
            p[4][2], p[4][3], p[65][0], p[65][1], p[65][2], p[65][3], p[66][0], p[66][1], p[66][2], p[66][3], p[67][0], p[67][1], p[47][3], p[47][0], p[42][3], p[42][0], p[95][3], p[103][3], p[93][0]);
        // (20/10) el medidor de luz en la misma linea
        fprintf(f, " | %s\n", LightMeterSummary().c_str());
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
        // (20/10) el juego tiene 21 variantes de la pasada de luz del sol (sombras / tipo de camara); las 7 reemplazadas copian
        // LightCB y las otras 14 copian LightCBAlt. Si en el cuadro no corrio ninguna de las 7 (primeros planos, algunas
        // camaras de repeticion), LightCB se rellena con la copia alternativa en vez de apagarse: el sol sigue llegando a
        // la composicion, el tonemap y la DLL en todas las camaras.
        { auto a = s_named.find("LightCBAlt"), m = s_named.find("LightCB");
          if (a != s_named.end() && m != s_named.end() && a->second.buf && m->second.buf && a->second.copied >= now - 1 && m->second.copied < now - 1 && g_dev.imm) {
              D3D11_BUFFER_DESC da, dm; a->second.buf->GetDesc(&da); m->second.buf->GetDesc(&dm);
              if (da.ByteWidth == dm.ByteWidth) { g_dev.imm->CopyResource(m->second.buf, a->second.buf); m->second.copied = now; m->second.cleared = false; g_lightAltFrames++; }
          } }
        // (24/10 g) w44: la pasada de luz del sol corrio hace poco (partido). Sin ella (menus 3D, editor, previa) el juego ilumina
        // con un mapa esferico de estudio a media resolucion y los kits dejan su detalle fino (ver 55297925)
        { auto m2 = s_named.find("LightCB"), a2 = s_named.find("LightCBAlt");
          const bool sun = (m2 != s_named.end() && m2->second.copied >= now - 30) || (a2 != s_named.end() && a2->second.copied >= now - 30);
          g_cfg.live[44][3] = sun ? 1.0f : 0.0f; }
        for (auto& kv : s_named) {
            NamedRes& r = kv.second;
            if (!r.buf || r.cleared || kv.first.find("Light") == std::string::npos) continue;
            if (r.copied >= now - 1) continue;
            D3D11_BUFFER_DESC bd; r.buf->GetDesc(&bd);
            static std::vector<char> zeros; if (zeros.size() < bd.ByteWidth) zeros.assign(bd.ByteWidth, 0);
            if (g_dev.imm) { g_dev.imm->UpdateSubresource(r.buf, 0, nullptr, zeros.data(), 0, 0); r.cleared = true; }
        }
    }
    SunPassPresent();   // (25/10) variantes de la luz del sol: log de cambios
    LightTraceSummary();   // (27/10) quien llena el cbuffer de la luz
    UpdateTimeParams();
    SSGIPresent(s_presentNo.load());
    LightMeterPresent();
    CamHookPresent();   // (23/10) camara del motor: ganchos (primer Present), cortes de camara -> 160, log
    CharTrackPresent();    // (24/10) personajes: que jugador es cada esqueleto (ganchos en el primer Present, log)
    SkinDiagPresent();     // (24/10) tono de piel: diagnostico de los dibujos de personajes (w184)
    SkinTonePresent();     // (24/10) tono de piel por jugador: guardado y vista previa en el menu Fisico
    GlassPresent();        // (24/10) vidrio de los menus: lectura de lo anotado por el PS y plan de copias del fondo
    MuscleHookPresent();   // (23/10) musculatura: ganchos a los huesos de musculo del motor (primer Present)
    BodyPresent();         // (25/10) la ropa contra el cuerpo real: mallas de piel nuevas, cuadro nuevo
    ArmsPresent();         // (27/10) brazos de TorsoBody para las caras reales: huellas de los torsos del juego, claves del cuadro
    GrassFrameBoundary(flags);   // (26/10 d) pasto: limite del cuadro (reclamo en la GPU, x63) ANTES de subir t120
    HeadShadowPresent();   // (27/10 c) sombra de la cabeza: cabezas elegidas, cajas y filas 310 / 316..319 ANTES de subir t120
    UploadParams();
    WatchShaderFolder();
    PollMatch();
    CatalogPresent();
    LodPresent();
    ExeDumpPresent();
    ExePatchPresent();
    GrassDistPresent();
    PitchWearPresent();
    GrassPresent();
    FaceGenPresent();
    InspectPresent();
    FrameCopyPresent(sc);   // antes del menu, para que el menu no se refleje en los ojos
    AsyncShotPresent();
    RingPresent(sc);       // (24/10 e) aro del musculo sobre la imagen final (antes de las capturas y del menu)
    AutoCapturePresent(s_frameTex);
    FisicoCapturePresent(s_frameTex);   // (26/10 c) paginas del menu Fisico
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
    LightMeterReloadShaders();
    GTAOReloadShaders();
    GlassReloadShaders();
    RingReloadShaders();
    SSGIReloadShaders();
    BodyReloadShaders();
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
        e.pairRules = (e.type == 'p') ? PairRulesFor(e.hash) : nullptr;
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
    ExePatchInstall();   // (20/10) parches en memoria del motor (antes de que el juego lea su configuracion grafica)
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
