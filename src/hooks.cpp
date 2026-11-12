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
// (07/11) PS de la camiseta: 0 / 1 / 2 = 7e64 / a41e / c7ae (los de siempre); + 8 = variante sin SV_IsFrontFace (de lejos o
// del arbitro, sin confirmar: se anota la primera vez que se dibuja). -1 = no es camiseta
static int ShirtPSKind(uint64_t h) {
    switch (h) {
    case 0x7e647d9c43b453f9ULL: return 0;
    case 0xa41e7343f874154fULL: return 1;
    case 0xc7ae89b63e889007ULL: return 2;
    case 0xad33f4525eda9dbaULL: return 8 + 0;
    case 0xc4ab9c1533939fedULL: return 8 + 1;
    case 0xbb02fed5bcfdd140ULL: return 8 + 2;
    default: return -1;
    }
}
static void ShirtPSNoteNew(uint64_t h, UINT verts) {
    static std::atomic<int> seen[3];
    const int k = h == 0xad33f4525eda9dbaULL ? 0 : h == 0xc4ab9c1533939fedULL ? 1 : 2;
    if (seen[k].fetch_add(1) == 0) Log("torso bajo la camiseta: camiseta con el PS %016llx (variante de lejos / arbitro), %u indices", (unsigned long long)h, verts);
}

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
// (08/11) shader escrito en el codigo (vidrio del menu): sin archivo ni cache
ID3DBlob* CompileSourceShader(const char* name, const char* src, const char* profile) {
    if (!s_compile || !src) return nullptr;
    ID3DBlob *code = nullptr, *err = nullptr;
    HRESULT hr = s_compile(src, strlen(src), name, nullptr, nullptr, "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) {
        std::string e = err ? std::string((const char*)err->GetBufferPointer(), err->GetBufferSize()) : "error desconocido";
        Log("ERROR compilando %s:\n%s", name, e.c_str());
        { std::lock_guard<std::mutex> lk(g_errMutex); g_compileErrors.push_back(std::string(name) + ": " + e.substr(0, 300)); }
        if (err) err->Release(); if (code) code->Release();
        return nullptr;
    }
    if (err) err->Release();
    return code;
}
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
// (08/11 e) cierres en CCommandList::LUCDeferDeletion (siempre la entrada 6 de la tabla de listas del juego, en el editor): una lista
// grabada por el juego guarda punteros a todo lo que se enlazo en ella (nuestros shaders, vistas, buffers) y les baja el uso al
// liberarse. En el editor esas listas viven mucho (el visor no se vuelve a grabar cada cuadro), asi que soltar un recurso nuestro
// "al cuadro siguiente" o "a los 240 cuadros" puede dejarle a la lista un puntero muerto. Todo lo nuestro se suelta a los 30 s.
struct Aged { IUnknown* p; double t; };
static std::vector<Aged> s_aged;
void DeferRelease(IUnknown* p) { if (!p) return; std::lock_guard<std::mutex> lk(s_releaseMutex); s_aged.push_back({ p, Now() }); }

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
    auto r = s_replToObj.find(p);   // es uno de nuestros reemplazos (lo volvio a enlazar un restore)
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
int SceneShadowFixStats(int* skipped); void SceneShadowFixRejects(int* r3);   // (mas abajo, junto a los ganchos de Map / Unmap)
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
        if (sums <= 120 && g_cfg.motorPatch && (g_cfg.motorShadowFix == 2 || g_cfg.motorShadowFix == 4)) { int sk = 0, fx = SceneShadowFixStats(&sk); int rj[3]; SceneShadowFixRejects(rj); Log("sombra_parche (10 s): m_shadowProjection corregida en %d subidas del cbuffer de escena, %d sin tocar (sin sombra o sin rotacion; de esas, no era la escena %d, sombra no ortogonal %d; buffers con ventana descartados %d)", fx, sk, rj[0], rj[1], rj[2]); }
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

// (12/11, Agustin: "agregues por slider la posibilidad de probar las versiones viejas, hasta anteayer, con un selector en slider, es para
// grabar un video") z437 VERSION DEL PASTO para comparar: 0 = la actual; 1 = 12/11 madrugada (antes de: reparto sin filas, hojas sueltas
// altas, base menos saturada, pelota asentada, sombra suave, TAA nuevo, puntas humedas / encendidas al azar, hojas medianas de cerca);
// 2 = 11/11 noche (antes de: hojas angostas, mismo alto y brillo a toda distancia, viento nuevo, brillo estable, cielo en el pasto, el sol
// en las puntas, desgaste real); 3 = 11/11 tarde (natural nuevo con hojas 3D, todavia de ~9 cm, sin relleno de atlas ni corte parejo,
// con la luz propia vieja); 4 = 10/11, anteayer (atlas viejo, sin hojas 3D). Cada cosa nueva vuelve a su valor "como antes" (su control
// en 0) en la GPU y en las cuentas del pasto de la DLL, SIN tocar g_cfg.live ni el ini: volver a 0 deja todo como estaba. Lo que cambio
// en el codigo sin control propio (p. ej. el brillo parejo de las hojas 3D) queda como hoy: es la vista mas cercana a cada version
struct GrassVerOv { int ver, r, c; float v; };
static const GrassVerOv kGrassVer[] = {
    // >= 1: lo de 12/11 02:00 en adelante
    {1, 438, 0, 0.0f}, {1, 438, 1, 0.0f}, {1, 438, 3, 0.0f}, {1, 437, 1, 0.0f},
    {1, 439, 0, 0.0f}, {1, 439, 2, 0.0f}, {1, 439, 3, 0.0f},
    {1, 440, 0, 0.0f}, {1, 440, 1, 0.0f}, {1, 440, 2, 0.0f}, {1, 440, 3, 0.0f},
    // >= 2: 12/11 00:00 - 01:40
    {2, 432, 0, 0.0f}, {2, 432, 3, 0.0f}, {2, 433, 0, 1.0f}, {2, 433, 1, 0.0f}, {2, 433, 3, 0.0f},
    {2, 436, 0, 0.0f}, {2, 436, 1, 0.0f}, {2, 436, 2, 0.0f}, {2, 436, 3, 0.0f}, {2, 431, 0, 0.0f},
    // >= 3: 11/11 18:30 - 23:59 (luz propia nueva y290, corte parejo w108, 28 mm w296, relleno / variacion / desparejas, ancho w425)
    {3, 290, 1, 1.0f}, {3, 108, 3, 0.0f}, {3, 296, 3, 90.0f}, {3, 403, 1, 0.0f}, {3, 403, 2, 0.0f}, {3, 403, 3, 0.0f}, {3, 425, 3, 1.0f},
    // >= 4: antes del natural nuevo (11/11 15:20): atlas viejo y sin hojas 3D
    {4, 412, 2, 1.0f}, {4, 410, 2, 0.0f},
};
static int GrassVer() { const float z = g_cfg.live[437][2]; return z > 0.5f ? (int)std::clamp(z + 0.5f, 1.0f, 4.0f) : 0; }
float GrassLive(int r, int c) {
    const int ver = GrassVer();
    float v = g_cfg.live[r][c];
    if (ver > 0) for (const GrassVerOv& o : kGrassVer) if (o.r == r && o.c == c && ver >= o.ver) v = o.v;
    return v;
}

static void UploadParams() {
    if (!g_dev.imm || !g_dev.paramTex) return;
    static float p[kParamCount][4], q[kParamCount][4];
    struct Ov { int set; int idx, comp; float v; };
    std::vector<Ov> ovs;
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        memcpy(p, g_cfg.live, sizeof(p));
        { const int ver = GrassVer(); if (ver > 0) for (const GrassVerOv& o : kGrassVer) if (ver >= o.ver) p[o.r][o.c] = o.v; }   // (12/11) z437 version del pasto
        if (p[139][0] > 0.5f) p[104][0] = 1.0f;   // (20/10) pasto v5: necesita el pasto v3 (x104) prendido en la GPU
        p[258][0] = (g_cfg.motorPatch && g_cfg.motorSunFix && g_cfg.motorShadowFix == 0) ? 1.0f : 0.0f;   // (02/11) x258: la luz esta corregida y el shadow map no (los shaders del sol no miden grosor con el)
        p[147][3] = 1.0f;   // (20/10) w147: esta DLL iguala la cabeza de las caras genericas con la cara (reglas PS + VS)
        p[393][3] = g_tlClock;   // (10/11) fase guardada del ciclo (solluz.cpp) x periodo; antes el reloj (s mod 3600)   // (09/11) w393: reloj en segundos (vuelve a 0 cada hora) para el ciclo de dia y noche de la luz del editor (w392)
        p[393][2] = g_solRealActivo;   // (09/11) z393 = 1: el sol real del motor esta prendido en el editor (el shader deja de imitarlo)
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
struct CtxState { const ShaderEntry* ps = nullptr; const ShaderEntry* vs = nullptr; ID3D11Device* dev = nullptr; bool devKnown = false; ID3D11Buffer* lastHairCB = nullptr; uint64_t lastFace = 0; bool clothCS = false; ID3D11DepthStencilView* shadowDSV = nullptr; bool gameShadow = false; };   // shadowDSV: (27/10 c) el DSV puesto es el shadow map del sol del juego (sombra de la cabeza)   // clothCS: el CS puesto es el simulador del mod de ropa (diagnostico en Dispatch)   // lastHairCB: b11 de la ultima cara dibujada en este contexto (la boca la hereda)
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
    Ctx(c).shadowDSV = HeadShadowBind(n, v, d); Ctx(c).gameShadow = HeadShadowIsGameRes(d);
}
template<int V> static void STDMETHODCALLTYPE hk_OMSetRTU(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* v, ID3D11DepthStencilView* d, UINT us, UINT un, ID3D11UnorderedAccessView* const* u, const UINT* ic) {
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) GrassBaseFlush(c, 1, 0, v ? n : 0, v, d);   // solo UAVs: los targets no cambian
    o_OMSetRTU[V](c, n, v, d, us, un, u, ic);
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) { Ctx(c).shadowDSV = HeadShadowBind(n, v, d); Ctx(c).gameShadow = HeadShadowIsGameRes(d); }
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
// [PES21FX] luz_rastrear = 1 lo prende ((08/11) apagado por defecto: la investigacion termino en sol_parche y costaba CPU en cada Map). Hasta 64 pilas por segundo (por si el buffer es un anillo compartido).
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
    const DWORD64 target = s_ltExeLo + (XA(0x1417ae85aull) - 0x140000000ull);
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
// ---------------------------------------------------------------- la sombra que gira con la camara (27/10, sombra_parche 1)
// Registro de e85da3e (sombra_parche 3): m_shadowProjection (b2 c12..c15) toma la posicion en VISTA y el motor la arma con la
// vista -> mundo equivocada: usa A = [R | c] (las filas de la rotacion de la camara como columnas, c = posicion de la camara)
// en vez de la inversa real B = [R^T | c]. Con A el shadow map da exactamente el sol del cielo (0.0-0.6 grados en las 4 tomas);
// la cuenta del shader le pasa la posicion en vista y la luz queda en R^T R^T sol (hasta 89 grados; 0 solo si R es
// simetrica: camara de TV). Arreglo: S' = S A^-1 B = S C con C = R^T R^T (solo rotacion, la traslacion se cancela), con R la
// 3x3 de m_view (c8..c10) del mismo buffer. Se corrige el cbuffer de escena al subirlo (Unmap / UpdateSubresource); el buffer
// se aprende de la regla copy_cb = 2 SceneCB. Si m_shadowProjection esta en cero o es la misma vista (pasadas sin sombra) no
// se toca.
static std::atomic<ID3D11Resource*> s_scBuf[8]; static std::atomic<UINT> s_scOff[8];
static std::atomic<int> s_scFixed{0}, s_scSkipped{0};
static thread_local void* t_scMapped = nullptr; static thread_local ID3D11Resource* t_scRes = nullptr; static thread_local UINT t_scOffMap = 0;
// (27/10 b) primera prueba en partido: sombras corridas "a cualquier lado". Solo se aprenden buffers enteros (sin ventana: un
// anillo compartido tendria otros cbuffers en ese desplazamiento) y del tamano de cPSScene; y antes de tocar se verifica que
// sea de verdad la escena (m_eyepos = posicion de la camara sacada de m_view) y que S sea una proyeccion ortogonal de luz.
static std::atomic<int> s_scRejWin{0}, s_scRejEye{0}, s_scRejOrtho{0};
static void SceneCBLearn(ID3D11Resource* b, UINT off, UINT len) {
    if (off != 0 || len < 352 || len > 4096) { s_scRejWin++; return; }
    for (int i = 0; i < 8; i++) { ID3D11Resource* e = s_scBuf[i].load(std::memory_order_relaxed); if (e == b) return; }
    for (int i = 0; i < 8; i++) { ID3D11Resource* exp = nullptr; if (s_scBuf[i].compare_exchange_strong(exp, b)) { s_scOff[i].store(0); Log("sombra_parche: cbuffer de escena %d = %p (%u bytes)", i, (void*)b, len); return; } }
}
static int SceneCBIndex(ID3D11Resource* b) {
    if (!b) return -1;
    for (int i = 0; i < 8; i++) if (s_scBuf[i].load(std::memory_order_relaxed) == b) return i;
    return -1;
}
static bool SceneShadowFix(float* p) {   // p = c0 del cbuffer de escena; sin objetos con destructor: __try
    __try {
        float V[3][3], S[3][3];
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { V[i][j] = p[32 + i * 4 + j]; S[i][j] = p[48 + i * 4 + j]; }
        float same = 0, mag = 0;
        for (int i = 0; i < 3; i++) {
            float n = V[i][0] * V[i][0] + V[i][1] * V[i][1] + V[i][2] * V[i][2];
            if (n < 0.96f || n > 1.04f) return false;   // m_view no es una rotacion
            for (int j = 0; j < 3; j++) { same += fabsf(S[i][j] - V[i][j]); mag += fabsf(S[i][j]); }
        }
        if (mag < 1e-6f || same < 1e-3f) return false;   // sin sombra en esta pasada (cero o la vista copiada)
        // es la escena? la posicion de la camara sacada de m_view (-R^T t) tiene que ser m_eyepos (c20)
        float t[3] = { p[35], p[39], p[43] }, eye[3] = { p[80], p[81], p[82] }, de = 0;
        for (int j = 0; j < 3; j++) { float c = -(V[0][j] * t[0] + V[1][j] * t[1] + V[2][j] * t[2]); de += (c - eye[j]) * (c - eye[j]); }
        if (!(de < 0.25f)) { s_scRejEye++; return false; }
        // S: proyeccion ortogonal de la luz (filas x / y del mismo largo y perpendiculares)
        float n0 = sqrtf(S[0][0] * S[0][0] + S[0][1] * S[0][1] + S[0][2] * S[0][2]), n1 = sqrtf(S[1][0] * S[1][0] + S[1][1] * S[1][1] + S[1][2] * S[1][2]);
        float d01 = S[0][0] * S[1][0] + S[0][1] * S[1][1] + S[0][2] * S[1][2];
        // y nada de rotaciones / vistas sueltas (largo ~1) ni matrices con perspectiva (fila 3 distinta de 0 0 0 1): en algunas
        // pasadas c12 no es la sombra del sol y corregirla corria cosas del estadio
        bool affine = fabsf(p[60]) < 1e-4f && fabsf(p[61]) < 1e-4f && fabsf(p[62]) < 1e-4f && fabsf(p[63] - 1.0f) < 1e-3f;
        if (!(affine && n0 > 1e-4f && n0 < 0.6f && n1 > 1e-4f && fabsf(n0 - n1) < 0.15f * n0 && fabsf(d01) < 0.1f * n0 * n1)) { s_scRejOrtho++; return false; }
        float C[3][3];   // C = R^T R^T: C[i][j] = sum_k R[k][i] R[j][k]
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) C[i][j] = V[0][i] * V[j][0] + V[1][i] * V[j][1] + V[2][i] * V[j][2];
        for (int r = 0; r < 3; r++) {
            float o[3];
            for (int j = 0; j < 3; j++) o[j] = S[r][0] * C[0][j] + S[r][1] * C[1][j] + S[r][2] * C[2][j];
            for (int j = 0; j < 3; j++) p[48 + r * 4 + j] = o[j];
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool SceneFixOn() { return g_cfg.motorPatch && (g_cfg.motorShadowFix == 2 || g_cfg.motorShadowFix == 4); }   // (31/10) solo el modo 2: el 1 corrige en el exe (exepatch.cpp)
int SceneShadowFixStats(int* skipped) { if (skipped) *skipped = s_scSkipped.exchange(0); return s_scFixed.exchange(0); }
void SceneShadowFixRejects(int* r3) { r3[0] = s_scRejEye.exchange(0); r3[1] = s_scRejOrtho.exchange(0); r3[2] = s_scRejWin.exchange(0); }

template<int V> static HRESULT STDMETHODCALLTYPE hk_Map(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, D3D11_MAP t, UINT f, D3D11_MAPPED_SUBRESOURCE* m) {
    HRESULT hr = o_Map[V](c, r, sub, t, f, m);
    if (SUCCEEDED(hr) && m && m->pData && t != D3D11_MAP_READ && SceneFixOn()) { int i = SceneCBIndex(r); if (i >= 0) { t_scMapped = m->pData; t_scRes = r; t_scOffMap = s_scOff[i].load(); } }
    if (r && r == s_ltBuf.load(std::memory_order_relaxed) && g_cfg.lightTrace && SUCCEEDED(hr) && m) {
        t_ltRec = LightTraceNote(0); t_ltMapped = m->pData; t_ltSrc = t_ltRec >= 0 ? LightTraceSrc() : 0; }   // con el mismo tope por cuadro que las pilas
    return hr;
}
template<int V> static void STDMETHODCALLTYPE hk_Unmap(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub) {
    if (t_scMapped && r == t_scRes) {
        if (SceneShadowFix((float*)((char*)t_scMapped + t_scOffMap))) s_scFixed++; else s_scSkipped++;
        t_scMapped = nullptr; t_scRes = nullptr; }
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
    if (d && SceneFixOn()) {
        int i = SceneCBIndex(r);
        if (i >= 0) {
            D3D11_BUFFER_DESC bd; ((ID3D11Buffer*)r)->GetDesc(&bd);
            UINT lo = box ? box->left : 0, hi = box ? box->right : bd.ByteWidth, off = s_scOff[i].load();
            if (off >= lo && off + 256 <= hi && hi - lo <= 65536) {
                static thread_local std::vector<char> tmp; tmp.assign((const char*)d, (const char*)d + (hi - lo));
                if (SceneShadowFix((float*)(tmp.data() + (off - lo)))) { s_scFixed++; o_UpdSub[V](c, r, sub, box, tmp.data(), rp, dp); return; }
                s_scSkipped++;
            }
        }
    }
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
    if (FAILED(hr)) {   // (08/11) un FinishCommandList fallido dejaba el lock de la entrada del juego tomado (cuelgues del editor)
        static std::atomic<int> nf{0};
        if (nf++ < 20) { ID3D11Device* d = nullptr; c->GetDevice(&d); HRESULT rr = d ? d->GetDeviceRemovedReason() : S_OK; if (d) d->Release();
            Log("FinishCommandList fallo: hr 0x%08x (contexto %p, dispositivo 0x%08x)", (unsigned)hr, (void*)c, (unsigned)rr);
            if (FAILED(rr)) OnDeviceLost((unsigned)rr); }   // (08/11 e) placa reiniciada (TDR): aviso y cierre ordenado
    }
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
static const int kSavedMax = 64;   // (02/11) 24 -> 64: con los grupos del brazo (t26/t30/t36/t123) los dibujos del brazo pasaban de 24 y lo enlazado despues no se devolvia (se filtraba al dibujo siguiente: la boca tomaba texturas ajenas)
struct DrawScope { Saved s[kSavedMax]; int n = 0; FaceGenExtra brow; std::string faceId; bool skip = false; ID3D11PixelShader* restorePS = nullptr; LodIA lod; GlassScope glass; RingScope ring; FaceTessScope tess; bool hdMesh = false; bool mouth = false; UINT mCnt = 0, mStart = 0; INT mBase = 0; bool wire = false; ID3D11RasterizerState* wireRS = nullptr; };   // wire: vista en alambre del Motor (motorlink.cpp)   // tess: teselacion de la cara (facetess.cpp)   // glass: vidrio de los menus (glass.cpp)   // lod.count > 0: dibujar con la geometria de LOD0 (lod.cpp)

static void Remember(ID3D11DeviceContext* c, DrawScope& d, bool ps, bool srv, int slot) {
    if (d.n >= kSavedMax) {   // (26/10 d) 12 -> 24, (02/11) -> 64: lo que no entra no se devolveria
        static std::atomic<bool> s_full{false};
        if (!s_full.exchange(true)) Log("AVISO: un dibujo enlazo mas de %d recursos del mod; lo que sigue no se devuelve", kSavedMax);
        return;
    }
    if (d.n == 24) { static std::atomic<int> s_over{0}; if (s_over.fetch_add(1) < 3) Log("recursos por dibujo: un dibujo paso de 24 (slot %c t%d): con la DLL anterior esto quedaba enlazado para el dibujo siguiente", ps ? 'P' : 'V', slot); }
    Saved& s = d.s[d.n++]; s.ps = ps; s.srv = srv; s.slot = slot; s.prev = nullptr;
    if (srv) { ID3D11ShaderResourceView* v = nullptr; if (ps) c->PSGetShaderResources(slot, 1, &v); else c->VSGetShaderResources(slot, 1, &v); s.prev = v; }
    else { ID3D11Buffer* b = nullptr; if (ps) c->PSGetConstantBuffers(slot, 1, &b); else c->VSGetConstantBuffers(slot, 1, &b); s.prev = b; }
}
// (03/11) registro: maximo de recursos del mod enlazados en un dibujo (cada 30 s, aunque no pase del tope) y control de la boca
static std::atomic<int> s_resMax{0}, s_resDraws{0}, s_resOver24{0};
static std::atomic<uint64_t> s_resMaxPS{0}, s_resMaxVS{0};
static std::atomic<int> s_mouthDraws{0}, s_mouthOk{0}, s_mouthOther{0}, s_mouthNoRule{0};
static void ResNote(ID3D11DeviceContext* c, int n) {
    if (n <= 0) return;
    s_resDraws.fetch_add(1, std::memory_order_relaxed);
    if (n > 24) s_resOver24.fetch_add(1, std::memory_order_relaxed);
    int m = s_resMax.load(std::memory_order_relaxed);
    while (n > m) { if (s_resMax.compare_exchange_weak(m, n)) { CtxState& cs = Ctx(c); s_resMaxPS = cs.ps ? cs.ps->hash : 0; s_resMaxVS = cs.vs ? cs.vs->hash : 0; break; } }
}
// (03/11, Agustin: "con barba aveces se muestran las texturas de los brazos como pasaba antes") 1 cuadro de cada 30, ANTES de
// enlazar nada: si t0..t3 del PS ya tienen una textura que solo pone la DLL (brazos de TorsoBody, grupos del brazo, venas, boca),
// es una filtracion del dibujo anterior. Y cada cambio de t0 por ArmsTextureFor se anota por PS (por si un dibujo que no es brazo
// se reconoce como brazo por su cantidad de indices).
extern thread_local UINT t_drawVerts;
static std::atomic<int> s_leakDraws{0}, s_leakChecked{0}, s_armSwapDraws{0};
static const char* LeakName(ID3D11ShaderResourceView* v) {
    if (!v) return nullptr;
    switch (ArmsModTextureKind(v)) { case 1: return "arm_N_bsm (brazo de TorsoBody)"; case 2: return "thigh_N_bsm (muslo de TorsoBody)"; case 3: return "torso_N_bsm (torso de TorsoBody)"; case 4: return "referencia de tono del brazo"; default: break; }
    static const char* kF[] = { "file:textures\\pes_arm_anat_n.dds", "file:textures\\pes_arm_anat_c.dds", "file:textures\\pes_arm_anat_g.dds", "file:textures\\pes_veins_arm.dds",
        "file:textures\\pes_oral_bsm.dds", "file:textures\\pes_oral_nrm.dds", "file:textures\\pes_oral_srm.dds", "file:textures\\pes_oral_trm.dds" };
    for (const char* f : kF) if (FileTextureAny(f) == v) return f + 14;
    return nullptr;
}
static void LeakCheck(ID3D11DeviceContext* c, const ShaderEntry* ps, const ShaderEntry* vs) {
    if ((s_presentNo.load(std::memory_order_relaxed) % 30) != 0) return;
    s_leakChecked.fetch_add(1, std::memory_order_relaxed);
    ID3D11ShaderResourceView* v[4] = {}; c->PSGetShaderResources(0, 4, v);
    int hit = -1; const char* name = nullptr;
    for (int i = 0; i < 4 && hit < 0; i++) if (const char* n = LeakName(v[i])) { hit = i; name = n; }
    for (auto* x : v) if (x) x->Release();
    if (hit < 0) return;
    s_leakDraws.fetch_add(1, std::memory_order_relaxed);
    static std::mutex mx; static std::vector<uint64_t> seen;
    const uint64_t key = ((ps ? ps->hash : 0) * 31 + (vs ? vs->hash : 0)) * 8 + hit;
    std::lock_guard<std::mutex> lk(mx);
    if (seen.size() >= 40) return;
    for (uint64_t k : seen) if (k == key) return;
    seen.push_back(key);
    Log("FILTRACION: dibujo PS %016llX VS %016llX (%u indices) llega con %s en t%d antes de que la DLL enlace nada (la dejo puesta otro dibujo)",
        (unsigned long long)(ps ? ps->hash : 0), (unsigned long long)(vs ? vs->hash : 0), t_drawVerts, name, hit);
}
static void ArmSwapNote(const ShaderEntry* ps, UINT idx) {
    s_armSwapDraws.fetch_add(1, std::memory_order_relaxed);
    static std::mutex mx; static std::vector<uint64_t> seen;
    const uint64_t key = (ps ? ps->hash : 0) * 131071ULL + idx;
    std::lock_guard<std::mutex> lk(mx);
    if (seen.size() >= 24) return;
    for (uint64_t k : seen) if (k == key) return;
    seen.push_back(key);
    Log("brazos: t0 cambiado por la textura de TorsoBody en dibujo PS %016llX (%u indices)", (unsigned long long)(ps ? ps->hash : 0), idx);
}
void ResourcesLogPresent() {
    static double s_next = 0; static int s_lines = 0;
    double t = Now(); if (t < s_next || s_lines >= 40) return;
    s_next = t + 30.0;
    int draws = s_resDraws.exchange(0), mouth = s_mouthDraws.exchange(0);
    if (!draws && !mouth && !s_leakDraws.load() && !s_armSwapDraws.load()) return;
    s_lines++;
    Log("recursos por dibujo (30 s): %d dibujos con recursos del mod, maximo %d de %d (PS %016llX VS %016llX), mas de 24: %d | boca 63fab: %d dibujos, t0..t3 del mod %d, otra textura %d, sin regla %d",
        draws, s_resMax.exchange(0), kSavedMax, (unsigned long long)s_resMaxPS.load(), (unsigned long long)s_resMaxVS.load(), s_resOver24.exchange(0),
        mouth, s_mouthOk.exchange(0), s_mouthOther.exchange(0), s_mouthNoRule.exchange(0));
    Log("filtraciones (30 s): %d de %d dibujos revisados llegan con una textura del mod en t0..t3; t0 cambiado por brazos de TorsoBody en %d dibujos",
        s_leakDraws.exchange(0), s_leakChecked.exchange(0), s_armSwapDraws.exchange(0));
    BocaJugLog();
}
static void MouthCheck(ID3D11DeviceContext* c, const ShaderEntry* ps) {
    s_mouthDraws.fetch_add(1, std::memory_order_relaxed);
    if (!ps->rule) { s_mouthNoRule.fetch_add(1, std::memory_order_relaxed); return; }
    static const char* kF[4] = { "file:textures\\pes_oral_bsm.dds", "file:textures\\pes_oral_nrm.dds", "file:textures\\pes_oral_srm.dds", "file:textures\\pes_oral_trm.dds" };
    ID3D11ShaderResourceView* v[4] = {}; c->PSGetShaderResources(0, 4, v);
    bool ok = true;
    for (int i = 0; i < 4; i++) { if (v[i] != FileTexture(kF[i])) ok = false; if (v[i]) v[i]->Release(); }
    (ok ? s_mouthOk : s_mouthOther).fetch_add(1, std::memory_order_relaxed);
}
static void Restore(ID3D11DeviceContext* c, DrawScope& d) {
    ResNote(c, d.n);
    for (int i = d.n - 1; i >= 0; i--) {
        Saved& s = d.s[i];
        if (s.srv) { auto* v = (ID3D11ShaderResourceView*)s.prev; if (s.ps) c->PSSetShaderResources(s.slot, 1, &v); else c->VSSetShaderResources(s.slot, 1, &v); }
        else { auto* b = (ID3D11Buffer*)s.prev; if (s.ps) c->PSSetConstantBuffers(s.slot, 1, &b); else c->VSSetConstantBuffers(s.slot, 1, &b); }
        if (s.prev) s.prev->Release();
    }
    d.n = 0;
}

extern thread_local UINT t_drawVerts; extern thread_local int t_drawKind; extern thread_local UINT t_drawStart; extern thread_local INT t_drawBase; extern thread_local bool t_lodNoted;   // (definidas mas abajo)
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
            if (a.name == "SceneCB") SceneCBLearn(src, window ? off : 0, sd.ByteWidth);   // (27/10) sombra_parche 1: el buffer que se corrige al subirlo
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
            if ((a.onlyIdx && t_drawVerts != a.onlyIdx) || (a.skipIdx && t_drawVerts == a.skipIdx)) break;   // (04/11) boca propia por jugador
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
            // (04/11) la boca que sigue es de esta cara (bocajug.cpp); sin identidad (cara pendiente) = 0, nunca la cara anterior
            Ctx(c).lastFace = (fgFound && !ex.id.empty()) ? strtoull(ex.id.c_str(), nullptr, 16) : 0;
            if (fgFound) {
                d.faceId = ex.id;   // (04/11 m) teselacion por jugador: la regla de la cara (VS, Expr) corre despues de esta (PS)
                // 19/10 c: la boca hereda este b11; se retiene con AddRef porque la entrada de cara puede liberarse (partido nuevo,
                // recarga de tipos de pelo) antes del dibujo de la boca -> puntero colgado -> el driver se caia (nvwgf2umx 0xc0000005)
                if (ex.hairCB) { CtxState& cs = Ctx(c); if (cs.lastHairCB != ex.hairCB) { ex.hairCB->AddRef(); if (cs.lastHairCB) cs.lastHairCB->Release(); cs.lastHairCB = ex.hairCB; } }
                // (04/11) caras en alta (caras_hd.cpp): t0 bsm, t1 normal, t2 srm, t3 trm reescaladas, si existen en PES21FX\caras_hd
                if (!ex.id.empty()) { ID3D11ShaderResourceView* hd[4];
                    if (CarasHdGet(ex.id, hd)) for (int i = 0; i < 4; i++) if (hd[i]) { Remember(c, d, true, true, i); c->PSSetShaderResources(i, 1, &hd[i]); } }
                Remember(c, d, true, true, a.slot); c->PSSetShaderResources(a.slot, 1, &fa);
                Remember(c, d, true, true, a.idx);  c->PSSetShaderResources(a.idx, 1, &fb);
                // 18/10 pelo: parametros del tipo de pelo de ESTA cara (PES21FX_hair.ini) en b11
                if (ex.hairCB) { Remember(c, d, true, false, 11); c->PSSetConstantBuffers(11, 1, &ex.hairCB); }
                // cejas con hebras v2 (04/11): el mapa de pelo en t26 (lo usa el diagnostico "semillas sobre la cara") y se anota este
                // dibujo para repetirlo con el GS de hebras apenas termine (BrowStrands). Ya no se enlaza el UAV de semillas de la v1.
                const ShaderEntry* bvs = Ctx(c).vs;
                if (t_drawKind == 1 && bvs && bvs->repl && !g_cfg.bypass && BrowWanted(c, ex, bvs->hash)) {
                    Remember(c, d, true, true, 26); c->PSSetShaderResources(26, 1, &ex.seedMap);
                    if (ex.browMap) { Remember(c, d, true, true, 44); c->PSSetShaderResources(44, 1, &ex.browMap); }   // (07/11) borrar la ceja pintada bajo las hebras (fila 382)
                    d.brow = ex; d.brow.browVS = bvs->hash; d.brow.browCount = t_drawVerts; d.brow.browStart = t_drawStart; d.brow.browBase = t_drawBase;
                }
            }
            break; }
        case RuleAction::Expr: {   // (31/10) expresiones faciales: donde esta sk_head en la paleta de esta malla (expr.cpp)
            if (ps) break;
            // solo DrawIndexed (con otro tipo de dibujo count 0 -> cbuffer "sin dato")
            const float* hc = nullptr; int hd = -1;
            ID3D11ShaderResourceView* shp = nullptr; ID3D11ShaderResourceView* shw = nullptr; ID3D11ShaderResourceView* seam = nullptr;
            ID3D11Buffer* wrb = nullptr; const ShaderEntry* fp0 = Ctx(c).ps; const bool skinPs0 = fp0 && fp0->repl && (fp0->hash == 0x5ce3ed8702271bc1ULL || fp0->hash == 0xce188ae110354aeaULL);
            // (08/11 p) cara con mas poligonos (caramalla.cpp): volcado de la piel de la cara / dibujo con la malla subdividida (nunca la boca)
            if (t_drawKind == 1 && !d.lod.count && !(fp0 && fp0->hash == 0x63fab7735eee149cULL) && ((skinPs0 && !d.faceId.empty()) || g_caraMallaAny.load(std::memory_order_relaxed))) {
                LodIA L; ID3D11ShaderResourceView* hp = nullptr;
                if (CaraMallaDraw(c, skinPs0 ? d.faceId : std::string(), t_drawVerts, t_drawStart, t_drawBase, &L, &hp)) {
                    d.lod = L; d.hdMesh = true; if (hp) { Remember(c, d, false, true, 114); c->VSSetShaderResources(114, 1, &hp); } }
            }
            if (ID3D11Buffer* b = ExprBufferForDraw(c, t_drawKind == 1 ? t_drawVerts : 0, t_drawStart, t_drawBase, &hc, &hd, &shp, &shw, &seam, false, &wrb, skinPs0)) { Remember(c, d, false, false, a.slot); c->VSSetConstantBuffers(a.slot, 1, &b); }
            ID3D11Buffer* wrPl = skinPs0 ? WrPlayerBuffer(d.faceId) : nullptr;   // (10/11) arrugas por jugador (PS b13 / DS b9); sin valores: nulo = como todos
            if (skinPs0) { Remember(c, d, true, false, 13); c->PSSetConstantBuffers(13, 1, &wrPl); }
            if (wrb) { Remember(c, d, true, false, 12); c->PSSetConstantBuffers(12, 1, &wrb); }   // (08/11) pesos de arrugas del juego suavizados (PS b12 PESWrSmooth)
            if (shp) { Remember(c, d, false, true, 118); c->VSSetShaderResources(118, 1, &shp); }   // (04/11) gestos esculpidos de esta malla
            if (ID3D11ShaderResourceView* bd = ExprBoneDeltaSrv()) { Remember(c, d, false, true, 115); c->VSSetShaderResources(115, 1, &bd); }   // (08/11) gestos en la paleta (t115)
            if (shw) { Remember(c, d, true, true, 31); c->PSSetShaderResources(31, 1, &shw); }      // (04/11) arrugas esculpidas de la sonrisa (PS de la cara)
            // (04/11 b) teselacion de la cara en primeros planos (facetess.cpp): solo piel de la cara (nunca la boca, los ojos ni las
            // pestanas: expr.cpp no les da pesos de borde), con DrawIndexed y el PS de la cara con reemplazo (los parametros t120 del dibujo)
            { const ShaderEntry* fp = Ctx(c).ps; const bool skin = fp && (fp->hash == 0x5ce3ed8702271bc1ULL || fp->hash == 0xce188ae110354aeaULL);
              if (skin && t_drawKind == 1 && !seam) FaceTessNote(0); }   // (04/11 d) registro: piel de la cara sin teselar
            if (seam && t_drawKind == 1 && Ctx(c).ps && Ctx(c).ps->hash != 0x63fab7735eee149cULL) {
                ID3D11ShaderResourceView* prm = nullptr; c->VSGetShaderResources(120, 1, &prm);
                if (!prm) FaceTessNote(1);
                bool tessOn = false;
                if (prm) { if (FaceTessBegin(c, hash, seam, prm, d.tess, d.faceId, wrb, wrPl)) { tessOn = true; Remember(c, d, false, true, 119); c->VSSetShaderResources(119, 1, &seam);
                    d.tess.cnt = t_drawVerts; d.tess.start = t_drawStart; d.tess.base = t_drawBase; } prm->Release(); }
                // (08/11 g) sin teselar tambien: el VS lee el peso de borde (t119) para no aplicar los gestos de la paleta en el borde de la
                // pieza (pes_expr.hlsli PesBoneDeltaEdge: las uniones entre piezas de la cara no se abren)
                if (!tessOn) { Remember(c, d, false, true, 119); c->VSSetShaderResources(119, 1, &seam); }
            }
            if (hc && FreeCamWantsHead()) ExprCaptureHead(c, hc, hd);   // (1/11) camara libre "seguir la cara": matriz de sk_head de este dibujo
            break; }
        case RuleAction::Beard: {   // (04/11 c) barba que sigue los huesos de la cara (expr.cpp BeardForDraw, pes_beard.hlsli)
            if (ps) break;
            ID3D11Buffer* b = nullptr; ID3D11ShaderResourceView* pl = nullptr; ID3D11ShaderResourceView* bd = nullptr;
            if (BeardForDraw(c, t_drawKind == 1 ? t_drawVerts : 0, t_drawStart, t_drawBase, &b, &pl, &bd) && b) {
                Remember(c, d, false, false, a.slot); c->VSSetConstantBuffers(a.slot, 1, &b);
                if (pl && bd) { Remember(c, d, false, true, 116); c->VSSetShaderResources(116, 1, &pl); Remember(c, d, false, true, 117); c->VSSetShaderResources(117, 1, &bd); }
                if (ID3D11ShaderResourceView* dv = ExprBoneDeltaSrv()) { Remember(c, d, false, true, 115); c->VSSetShaderResources(115, 1, &dv); }   // (08/11) gestos en la paleta (t115)
            }
            break; }
        case RuleAction::SetParam: break;   // ya resuelto con la textura de parametros alternativa
        }
    }
}

static std::atomic<bool> g_shirtSeen{false};   // (27/10 c) se dibujo una camiseta desde el ultimo Present
static std::atomic<bool> s_firstRule{false};
thread_local bool t_sparseGrass = false;
// (09/11, hilo boca/oclusion) DIAGNOSTICO del recuadro de la boca con el SSS (w95): un cuadro cada 10 s se anotan, en orden, los
// dibujos de la malla de la boca (65280 indices) y de la cara / boca / composicion del editor, con su profundidad y su mezcla, para
// ver si algun dibujo de la boca escribe profundidad delante de la piel sin pintar color. Solo lectura (no cambia nada del dibujo).
static std::atomic<int> s_mdState{0}; static std::atomic<int> s_mdSeq{0}; static double s_mdLast = 0;
static void MouthDrawDiag(ID3D11DeviceContext* c, uint64_t psh, uint64_t vsh, UINT idx) {
    if (s_mdState.load(std::memory_order_relaxed) != 1) return;
    const bool face = psh == 0x5ce3ed8702271bc1ULL || psh == 0xce188ae110354aeaULL || psh == 0x63fab7735eee149cULL || psh == 0xf807e74d5b900a15ULL || psh == 0x619b86cb5691cdceULL;
    const int seq = s_mdSeq.fetch_add(1, std::memory_order_relaxed);
    if (!(face || idx == 65280u)) return;
    static std::atomic<int> s_lines{0}; if (s_lines.fetch_add(1) > 2000) return;   // tope de toda la sesion
    ID3D11RenderTargetView* rt[8] = {}; ID3D11DepthStencilView* dsv = nullptr; c->OMGetRenderTargets(8, rt, &dsv);
    int nrt = 0; for (auto* r : rt) if (r) { nrt++; r->Release(); }
    ID3D11DepthStencilState* dss = nullptr; UINT ref = 0; c->OMGetDepthStencilState(&dss, &ref);
    D3D11_DEPTH_STENCIL_DESC dd{}; bool hasDss = dss != nullptr; if (dss) { dss->GetDesc(&dd); dss->Release(); } else { dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS; }
    ID3D11BlendState* bs = nullptr; float bf[4]; UINT sm = 0; c->OMGetBlendState(&bs, bf, &sm);
    D3D11_BLEND_DESC bd{}; if (bs) { bs->GetDesc(&bd); bs->Release(); } else { bd.RenderTarget[0].RenderTargetWriteMask = 0xF; }
    ID3D11RasterizerState* rs = nullptr; c->RSGetState(&rs); D3D11_RASTERIZER_DESC rd{}; if (rs) { rs->GetDesc(&rd); rs->Release(); }
    Log("dibujo boca diag #%d ps %016llx vs %016llx idx %u rtv %d dsv %d | prof %s%d escribe %d func %d | mezcla rt0 %d mascara %x %x %x | bias %d cull %d",
        seq, (unsigned long long)psh, (unsigned long long)vsh, idx, nrt, dsv ? 1 : 0, hasDss ? "" : "(defecto) ", dd.DepthEnable ? 1 : 0, (int)dd.DepthWriteMask, (int)dd.DepthFunc,
        bd.RenderTarget[0].BlendEnable ? 1 : 0, bd.RenderTarget[0].RenderTargetWriteMask, bd.RenderTarget[1].RenderTargetWriteMask, bd.RenderTarget[2].RenderTargetWriteMask,
        rs ? rd.DepthBias : 0, rs ? (int)rd.CullMode : 0);
    if (dsv) dsv->Release();
}
static void MouthDrawDiagPresent() {
    const double t = Now();
    if (s_mdState.load() == 1) { s_mdState = 0; s_mdLast = t; return; }
    if (t - s_mdLast > 10.0) { s_mdSeq = 0; s_mdState = 1; Log("dibujo boca diag: cuadro nuevo"); }
}
thread_local UINT t_drawVerts = 0; thread_local int t_drawKind = 0; thread_local UINT t_drawStart = 0; thread_local INT t_drawBase = 0; thread_local bool t_lodNoted = false; thread_local LodIA t_grassSub;   // (20/10) DrawIndexed en curso (LOD de caras)   // vertices del dibujo en curso (para "solo esta capa" del inspector)   // el dibujo en curso es una parcela lejana del pasto (lo marca GrassOnDraw)
static inline bool BeforeDraw(ID3D11DeviceContext* c, DrawScope& d) {
    if (!g_dev.dev) return false;
    CtxState& s = Ctx(c);
    if (!s.devKnown) {   // nuestros recursos (t120, copias) son del dispositivo del juego: nunca mezclar con otro
        ID3D11Device* cd = nullptr; c->GetDevice(&cd);
        s.dev = cd; if (cd) cd->Release();
        s.devKnown = true;
    }
    if (s.dev != g_dev.dev) return false;
    if (g_motorWire.load(std::memory_order_relaxed)) d.wire = MotorWireBegin(c, &d.wireRS);   // (11/11 3icxbh) Motor en tiempo real: geometria del G-buffer en alambre (y429); se devuelve en FX_DRAW
    const ShaderEntry *ps = s.ps, *vs = s.vs;
    // (27/10 b) brazos, manos, muslos y torso de TorsoBody del tono de la cara (brazos.cpp, filas 321 / 327): son las texturas
    // del cuerpo, no un shader, asi que valen tambien con "Ver original" (el shader original lee el mismo t0)
    LeakCheck(c, ps, vs);
    MouthDrawDiag(c, ps ? ps->hash : 0, vs ? vs->hash : 0, t_drawKind == 1 ? t_drawVerts : 0u);   // (09/11) diagnostico del recuadro de la boca
    if (vs) if (ID3D11ShaderResourceView* at = ArmsTextureFor(c, vs->hash, t_drawVerts)) { ArmSwapNote(ps, t_drawVerts); Remember(c, d, true, true, 0); c->PSSetShaderResources(0, 1, &at); }
    if (g_cfg.bypass) return d.n > 0;
    if (g_skinDiagArmed.load(std::memory_order_relaxed)) SkinDiagDraw(c, ps ? ps->hash : 0, vs ? vs->hash : 0, t_drawVerts);   // (24/10) skintone.cpp
    if (g_toneChkArmed.load(std::memory_order_relaxed)) SkinToneCheckDraw(c, ps ? ps->hash : 0, vs ? vs->hash : 0);   // (24/10 c) punto de los dibujos de piel
    if (UiHideShouldSkip(c, ps ? ps->hash : 0)) { d.skip = true; return d.n > 0; }   // (08/11 e) x392: menus del juego ocultos (sin vidrio ni aro)
    { ID3D11PixelShader* edge_ = nullptr;
      if (ID3D11ShaderResourceView* bg_ = EditorBgFor(c, ps ? ps->hash : 0, &edge_)) { Remember(c, d, true, true, 8); c->PSSetShaderResources(8, 1, &bg_); }
      if (edge_ && !d.restorePS) { c->PSGetShader(&d.restorePS, nullptr, nullptr); c->PSSetShader(edge_, nullptr, 0); } }   // (10/11 m042wp) borde del jugador sin el gris del fondo del juego   // (08/11 qk2yxa) x424: fondo del visor del editor negro / imagen propia
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
    // (04/11) pelo y barba en alta (caras_hd.cpp): PS del pelo c75f6047, barba 02f1d510, composicion 4a848614, sombra 213f9f12.
    // Solo cambia un slot si tiene la misma textura del juego que se volco (nunca la boca 63fab773, que no esta en la lista)
    if (ps && (ps->hash == 0xc75f6047d5817df9ULL || ps->hash == 0x02f1d51037669342ULL || ps->hash == 0x4a8486141c44a44dULL || ps->hash == 0x213f9f126b6af9c4ULL)) {
        ID3D11ShaderResourceView* hd[4]; CarasHdHair(c, ps->hash, hd);
        for (int i = 0; i < 4; i++) if (hd[i]) { Remember(c, d, true, true, i); c->PSSetShaderResources(i, 1, &hd[i]); } }
    if (ps && ps->hash == 0x63fab7735eee149cULL) MouthCheck(c, ps);
    // (04/11 boca) mascara del interior de la boca: el mismo dibujo se repite despues con el PS de la mascara (MouthMaskReplay)
    if (ps && ps->hash == 0x63fab7735eee149cULL && t_drawKind == 1 && MouthMaskOn()) { d.mouth = true; d.mCnt = t_drawVerts; d.mStart = t_drawStart; d.mBase = t_drawBase; }   // (03/11) la boca tiene sus 4 texturas del mod? (log 'recursos por dibujo')
    // (04/11) boca propia por jugador (bocajug.cpp): misma malla, vertices del perfil de caras.ini (boca = ...) para la boca de esa cara
    if (ps && ps->hash == 0x63fab7735eee149cULL && t_drawKind == 1 && !d.lod.count) {
        LodIA L; ID3D11ShaderResourceView* bsm = nullptr;
        const uint64_t lf = Ctx(c).lastFace; Ctx(c).lastFace = 0;   // una boca por cara: la siguiente boca sin cara nueva queda comun
        if (BocaJugDraw(c, lf, t_drawVerts, t_drawStart, L, &bsm)) { d.lod = L; if (bsm) { Remember(c, d, true, true, 0); c->PSSetShaderResources(0, 1, &bsm); } }
    }
    if (vs && vs->rule) RunRule(c, vs, d);
    // (20/10) reglas por pareja PS + VS: p.ej. la piel del cuerpo (aa7d1833) cuando dibuja la cabeza de una cara generica (VS faab87ee)
    if (ps && vs) if (const std::vector<const Rule*>* prs = ps->pairRules) for (const Rule* pr : *prs) if (pr->vsCond == vs->hash) RunRuleActions(c, pr, true, ps->hash, d);
    // (2/11) recortes de pasto / tierra (pitchwear.cpp): el PS de las hojas lee el atlas de PARTICULAS (textures\pes_debris.dds, mismo formato)
    if (PitchClipDrawActive()) {
        if (ID3D11ShaderResourceView* a = FileTexture("file:textures\\pes_debris.dds")) { Remember(c, d, true, true, 8); c->PSSetShaderResources(8, 1, &a); }
        if (ID3D11ShaderResourceView* b = FileTexture("file:textures\\pes_debris_n.dds")) { Remember(c, d, true, true, 9); c->PSSetShaderResources(9, 1, &b); }
    }
    // (25/10) manos de TorsoBody (manos.cpp): datos por vertice de la mano en t126 del VS (cuaterniones duales + correctivas)
    if (vs && vs->repl) {
        bool arm = false, hand = false;
        ID3D11ShaderResourceView* hv = HandDataFor(vs->hash, t_drawVerts, t_drawStart, t_drawBase); hand = hv != nullptr;
        if (!hv) hv = LimbDataFor(vs->hash, t_drawVerts, t_drawStart, t_drawBase, &arm);   // (26/10, v12.7) brazos y piernas
        if (hv) { Remember(c, d, false, true, 126); c->VSSetShaderResources(126, 1, &hv); }
        // (31/10) torso bajo la camiseta preciso: mapa de lo que asoma (cuerpo.cpp) en t124 del VS de la piel y de su sombra
        // (08/11) gestos del publico: buffer pes_crowdface en t30 de los 6 VS del publico (sin tocar PES21FX_rules.ini)
        if (ID3D11ShaderResourceView* cf = PublicoSRVFor(vs->hash)) { Remember(c, d, false, true, 30); c->VSSetShaderResources(30, 1, &cf); }
        if (vs->hash == 0x1c02a92ab4ae3362ULL || vs->hash == 0xc6ef0c7519ec95a5ULL) if (ID3D11ShaderResourceView* nv = TorsoNeedSRV()) { Remember(c, d, false, true, 124); c->VSSetShaderResources(124, 1, &nv); }
        if (arm && vs->hash == 0x1c02a92ab4ae3362ULL) {   // (02/11) grupos musculares del brazo
            if (ID3D11ShaderResourceView* an = ArmAnatTexture(0)) { Remember(c, d, true, true, 26); c->PSSetShaderResources(26, 1, &an); }
            if (ID3D11ShaderResourceView* ac = ArmAnatTexture(1)) { Remember(c, d, true, true, 30); c->PSSetShaderResources(30, 1, &ac); }
            if (ID3D11ShaderResourceView* ag = ArmAnatTexture(2)) { Remember(c, d, true, true, 36); c->PSSetShaderResources(36, 1, &ag); }   // (02/11 b) grupos
        }
        if (arm && vs->hash == 0x1c02a92ab4ae3362ULL) if (ID3D11ShaderResourceView* ev = MuscleEffortSRV()) { Remember(c, d, false, true, 123); c->VSSetShaderResources(123, 1, &ev); }   // (02/11) venas por esfuerzo
        // (26/10, v12.7) venas: textura en t29 del PS de la piel para los brazos y las manos (no en la sombra)
        if ((hand || arm) && vs->hash == 0x1c02a92ab4ae3362ULL) if (ID3D11ShaderResourceView* vt = VeinTexture()) { Remember(c, d, true, true, 29); c->PSSetShaderResources(29, 1, &vt); }
        // (27/10) brazos, manos y muslos de TorsoBody para las caras reales: ArmsTextureFor al principio de BeforeDraw
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
    if (ps && ps->repl) { int si = SunPassIndex(ps->hash); if (si >= 0 && si != 7) if (ID3D11ShaderResourceView* hs = HeadShadowSRV()) { Remember(c, d, true, true, 30); c->PSSetShaderResources(30, 1, &hs); } }
    // (04/11 boca) d0e9 / 7dbbc: mascara del interior de la boca en t43 (donde hay boca, la contraluz de antes)
    if (ps && ps->repl && (ps->hash == 0xd0e9d81c09b3da66ULL || ps->hash == 0x7dbbc922c9ae74c5ULL || ps->hash == 0x619b86cb5691cdceULL
        /* (10/11) focos: la sombra fina sobre la cara no entra en la boca */ || ps->hash == 0x142c18270479a113ULL || ps->hash == 0x68d58aa84c0256f5ULL || ps->hash == 0x9ce9d8463800b399ULL || ps->hash == 0xc33576f12dd818bfULL || ps->hash == 0xd5037e255559ea13ULL)) if (ID3D11ShaderResourceView* mm = MouthMaskSRV())   /* (09/11 hilo boca) + 619b: sombra del volumen de la boca */ { Remember(c, d, true, true, 43); c->PSSetShaderResources(43, 1, &mm); }
    // 20/10: desgaste de la cancha en vivo: la posicion de cada jugador sale del dibujo de su short / medias (b5 del VS)
    if (ps && ps->hash == 0x55297925e0c66085ULL) PitchWearNotePlayer(c);
    // (2/11) botines / pelota (aadffecf): anotar la traslacion para reconocer la pelota; en el dibujo de la pelota, b12 = manchas (fila 177)
    if (ps && ps->hash == 0xaadffecf2090e4f1ULL && ps->repl) if (ID3D11Buffer* bb = PitchBallDrawNote(c, t_drawVerts)) { Remember(c, d, true, false, 12); c->PSSetConstantBuffers(12, 1, &bb); }
    // (11/11 4wp0pj) para el segundo paso de los botines sucios (grumos con malla por teselacion, como la piel del cuerpo): que VS, topologia y
    // tipo de dibujo usan los botines. Se anotan los primeros 6 pares distintos (VS, vertices), una sola vez por sesion
    if (ps && ps->hash == 0xaadffecf2090e4f1ULL) { static std::mutex s_bmx; static std::vector<uint64_t> s_bseen; uint64_t k = (vs ? vs->hash : 0) ^ ((uint64_t)t_drawVerts << 40);
        std::lock_guard<std::mutex> lk(s_bmx); if (s_bseen.size() < 6 && std::find(s_bseen.begin(), s_bseen.end(), k) == s_bseen.end()) { s_bseen.push_back(k);
            D3D11_PRIMITIVE_TOPOLOGY tp; c->IAGetPrimitiveTopology(&tp);
            Log("botines (diag teselacion): VS %016llx (reemplazado %s) vertices %u tipo de dibujo %d topologia %d", (unsigned long long)(vs ? vs->hash : 0), vs && vs->repl ? "si" : "no", t_drawVerts, t_drawKind, (int)tp); } }
    // (04/11 g, Agustin: "teselacion a las manos, brazos y piernas") piel del cuerpo (VS 1c02a92a + PS aa7d1833 reemplazado, DrawIndexed):
    // expr.cpp lee la malla una vez (solo pesos de borde) y facetess.cpp pone el HS / DS del cuerpo; b12 = vertices / primer indice
    // de la malla, t119 = pesos de borde (los lee el VS para TEXCOORD12). Fila 364 (361 / 362 expresiones, 363 piel).
    if (vs && vs->hash == 0x1c02a92ab4ae3362ULL && ps && ps->hash == 0xaa7d1833f6a11481ULL && ps->repl && t_drawKind == 1 && g_cfg.live[364][0] > 0.5f) {
        ID3D11ShaderResourceView* seam = nullptr;
        if (ID3D11Buffer* b = ExprBufferForDraw(c, t_drawVerts, t_drawStart, t_drawBase, nullptr, nullptr, nullptr, nullptr, &seam, true)) {
            if (seam) {
                ID3D11ShaderResourceView* prm = nullptr; c->VSGetShaderResources(120, 1, &prm);
                if (prm) {
                    if (FaceTessBegin(c, vs->hash, seam, prm, d.tess)) {
                        Remember(c, d, false, false, 12); c->VSSetConstantBuffers(12, 1, &b);
                        Remember(c, d, false, true, 119); c->VSSetShaderResources(119, 1, &seam);
                    }
                    prm->Release();
                }
            }
        }
    }
    // (27/10 c) hay camisetas en escena (PS de la camiseta 7e64 / a41e / c7ae): el torso se mete bajo la tela (fila 320, z la pone
    // la DLL en Present); sin camiseta (menu Apariencia / Fisico, tecla 4 de Sider) el torso queda entero, sin escalon en la costura
    // (07/11) Agustin: de lejos el torso atraviesa la camiseta. El catalogo tiene otras 3 variantes del G-buffer del kit con las
    // mismas texturas (patron + marking / badge) pero sin SV_IsFrontFace (no se dibujaron en la captura del menu): candidatas a la
    // camiseta de lejos / del arbitro. Sin ellas el personaje no entra en la lista y su torso queda entero (sin x320).
    if (const int sk = ps ? ShirtPSKind(ps->hash) : -1; sk >= 0) {
        g_shirtSeen.store(true, std::memory_order_relaxed);
        if (sk >= 8) ShirtPSNoteNew(ps->hash, t_drawVerts);
        TorsoShirtNote(c, (sk & 3) | ((t_drawVerts == 7284 || t_drawVerts == 6006 || t_drawVerts == 7512 || t_drawVerts == 5550) ? 4 : 0));   // (01/11) +4 = camiseta ajustada (indices de [shirt_tight] detect de cloth.ini)   // (31/10 b) camiseta por jugador
    }
    // 19/10: SSS separable en pantalla antes de la composicion: la irradiancia (t9) llega ya difundida sobre la piel
    if (ps && (ps->hash == 0x619b86cb5691cdceULL || ps->hash == 0xf807e74d5b900a15ULL) && ps->repl) {
        int gz_ = GpuZoneBegin(c, kGzSSSS); ID3D11ShaderResourceView* v0_ = SSSSBeforeComposition(c); GpuZoneEnd(c, gz_);
        if (ID3D11ShaderResourceView* v = v0_) { Remember(c, d, true, true, 9); c->PSSetShaderResources(9, 1, &v);
            if (ID3D11ShaderResourceView* sp = SSSSSpecSRV()) { Remember(c, d, true, true, 10); c->PSSetShaderResources(10, 1, &sp); } }   // (27/10 b) brillo con la luz de prueba del editor
    }
    // 20/10: GTAO (HBAO actual) para la luz global y la composicion: t27 (se calcula una vez por cuadro)
    // (08/11 b) tambien la composicion del editor (f807): ahi no habia GTAO y la oclusion de corto alcance de la boca no se veia
    if (ps && (ps->hash == 0x619b86cb5691cdceULL || ps->hash == 0xfda6da09737592b1ULL || ps->hash == 0xf807e74d5b900a15ULL) && ps->repl) {
        int gz_ = GpuZoneBegin(c, kGzGTAO); ID3D11ShaderResourceView* v = GTAOBeforeComposition(c, s_presentNo.load()); GpuZoneEnd(c, gz_);
        if (!v && g_cfg.live[132][0] > 0.5f) v = GTAONeutralSRV();   // (24/10 g) escena sin GTAO (editor): sin oclusion, no 0
        if (v) { Remember(c, d, true, true, 27); c->PSSetShaderResources(27, 1, &v); }
    }
    // 21/10: SSGI temporal para la composicion: t26 (media resolucion, acumulado en el tiempo)
    // (08/11 0qtsss, Agustin: "el ssgi en el modo editar deberia cargar por completo") tambien la composicion del editor (f807) con x411
    if (ps && ps->hash == 0xf807e74d5b900a15ULL) EditorBgNoteComposition();   // (08/11 qk2yxa) el visor del editor esta en pantalla (fondo x424)
    const bool ssgiEd_ = ps && ps->hash == 0xf807e74d5b900a15ULL && g_cfg.live[411][0] > 0.5f;
    // REGLA FIJA de la boca: la composicion del editor recibe la mascara del interior de la boca (t43) y ahi deja todo como antes
    if (ssgiEd_ && ps->repl) if (ID3D11ShaderResourceView* mm = MouthMaskSRV()) { Remember(c, d, true, true, 43); c->PSSetShaderResources(43, 1, &mm); }
    if (ps && (ps->hash == 0x619b86cb5691cdceULL || ssgiEd_) && ps->repl) {
        int gz_ = GpuZoneBegin(c, kGzSSGI); ID3D11ShaderResourceView* v0_ = SSGIBeforeComposition(c, s_presentNo.load(), ssgiEd_); GpuZoneEnd(c, gz_);
        if (ID3D11ShaderResourceView* v = v0_) { Remember(c, d, true, true, 26); c->PSSetShaderResources(26, 1, &v); }
    }
    // (27/10) 323: ocultar la camiseta interior (inner / inner_cap / inner_glove de common_package.fpk: 9990 / 9714 / 8640 indices,
    // y el cuello alto highneck: 960). El modulo SinCamiseta no las puede tapar (vienen dentro del fpk que el juego carga una vez):
    // pegadas a la piel, asoman con el color de la camiseta interior del equipo (blanco en el editor, rosa en un partido) en la linea
    // de la manga, en la curva del hombro y en la base del cuello, con o sin shaders. 9990 / 9714 / 8640 no los usa ninguna malla
    // de la piel; 960 es comun (utileria), asi que solo se oculta con el mismo VS que ya dibujo una de las otras tres.
    if (g_cfg.live[323][0] > 0.5f && vs && !d.skip) {
        static std::atomic<uint64_t> s_innerVS{0};
        const bool big = t_drawVerts == 9990 || t_drawVerts == 9714 || t_drawVerts == 8640;
        if (big) s_innerVS.store(vs->hash, std::memory_order_relaxed);
        if (big || (t_drawVerts == 960 && vs->hash == s_innerVS.load(std::memory_order_relaxed))) {
            d.skip = true;
            if (g_cfg.live[323][3] > 0.5f) { static std::atomic<int> n{0}; if (n.fetch_add(1) < 40) Log("camiseta interior oculta: %u indices, vs %016llx ps %016llx", t_drawVerts, (unsigned long long)vs->hash, (unsigned long long)(ps ? ps->hash : 0)); }
        }
    }
    if (GrassSkipGameDraw()) d.skip = true;   // (2/11) x178: pasto propio del juego apagado con el mod emitiendo
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
    // (04/11 i) pestanas con hebras: la tira de pestanas (VS 66917213 / PS f0d74b9c) se repite con el GS de pestanas al terminar
    if (t_drawKind == 1 && vs && ps && !g_cfg.bypass && !d.brow.browVS && LashWanted(c, vs->hash, ps->hash)) {
        d.brow = FaceGenExtra(); d.brow.browVS = vs->hash; d.brow.browCount = t_drawVerts; d.brow.browStart = t_drawStart; d.brow.browBase = t_drawBase;
    }
    RingFaceBind(c, d.ring);   // (24/10 e) medicion del color de la cara: u7 (despues de las cejas en u5 / u6)
    return d.n > 0;
}
#define FX_DRAW(verts, kind, inst, call) if (g_catalogHook) { CtxState& cs_ = Ctx(c); CatalogDraw(c, cs_.vs, cs_.ps, verts, kind, inst); } t_drawVerts = (UINT)(verts); t_drawKind = (kind); DrawScope ds; bool r = BeforeDraw(c, ds); \
    if ((kind) == 1 && g_lodAny) { if (!t_lodNoted && !ds.lod.count) { CtxState& cl_ = Ctx(c); LodNoteAnyDraw(c, cl_.vs ? cl_.vs->hash : 0, cl_.ps ? cl_.ps->hash : 0, verts, t_drawStart, t_drawBase, &ds.lod); } t_lodNoted = false; } \
    if ((kind) == 1 && !ds.lod.count && g_caraMallaAny.load(std::memory_order_relaxed) && CaraMallaAnyDraw(c, verts, t_drawStart, t_drawBase, &ds.lod)) ds.hdMesh = true; \
    if ((kind) == 1 && t_grassSub.count) { if (!ds.lod.count) ds.lod = t_grassSub; else t_grassSub.Release(); t_grassSub = LodIA(); } \
    if (!ds.skip) { call; } if (ds.wire) MotorWireEnd(c, ds.wireRS); if (ds.restorePS) { c->PSSetShader(ds.restorePS, nullptr, 0); ds.restorePS->Release(); } if (ds.glass.on) GlassAfterDraw(c, ds.glass); if (ds.ring.on) RingAfterDraw(c, ds.ring); \
    if (ds.ring.overlay && !ds.skip) { RingOverlayBegin(c, ds.ring); call; RingOverlayEnd(c, ds.ring); } \
    { CtxState& cm_ = Ctx(c); if (cm_.ps && (cm_.ps->hash == 0x619b86cb5691cdceULL || cm_.ps->hash == 0xf807e74d5b900a15ULL)) { if (!ds.skip) HairFwdAfterComposition(c); if (g_meterHook) { int gz_ = GpuZoneBegin(c, kGzLightMeter); LightMeterAfterComposition(c); GpuZoneEnd(c, gz_); } } } \
    if (g_inspectOn) { CtxState& ci_ = Ctx(c); if (InspectBegin(c, ci_.ps, ci_.vs, kind, verts, inst)) { call; InspectEnd(c); } } \
    if (ds.mouth && !ds.skip) MouthMaskReplay<V>(c, ds); \
    if (ds.brow.browVS && !ds.skip) BrowStrands<V>(c, ds); if (ds.tess.on && !ds.skip && (kind) == 1) TessAtlas<V>(c, ds); if (ds.tess.on) FaceTessEnd(c, ds.tess); if (r) Restore(c, ds); if (ds.lod.count) ds.lod.Release();
// (26/10 g) antes de cada dibujo que no sea de la base de la cancha: la emision pendiente del pasto del mod (ver GrassBaseFlush)
#define GRASS_FLUSH(c) if (g_grassPendN.load(std::memory_order_relaxed) > 0) { CtxState& cf_ = Ctx(c); GrassBaseFlush(c, 0, cf_.ps ? cf_.ps->hash : 0); }
// (27/10 c) sombra de la cabeza: el mismo dibujo de la pasada de sombras del juego (cascada mas cercana) se repite en el atlas
// propio, una vez por cabeza, con un b2 (g_vsScene) cuya m_projectionView es la caja ortografica de esa cabeza (headshadow.cpp)
// (27/10 d) cualquier tipo de dibujo (el juego puede mandar la sombra con DrawIndexedInstanced); kind para el diagnostico
// (02/11) sombras de jugadores despegadas con sombra_parche 1 en algunos estadios: los que proyectan sombra se dibujan con
// g_vsScene.m_projectionView (b2 del VS, mundo -> cascada) y los que la reciben con m_shadowProjection (vista -> luz). Cada 15 s
// (12 veces) se copia el b2 del primer dibujo de la cascada cercana (viewport arriba a la izquierda) y se anota en el log junto
// a las lineas 'sombra_parche 1:' para comparar las dos matrices en un estadio bueno y uno malo.
static ID3D11Buffer* s_casterStage = nullptr; static UINT s_casterStageSize = 0; static std::atomic<int> s_casterWant{0};
static int s_casterLogN = 0; static double s_casterNext = 0; static uint64_t s_casterVS = 0; static float s_casterVp[2] = {};
// (02/11 c) diagnostico: por que no sale la muestra (f61bed9 en Mestalla: 0 lineas con el shadow map reconocido y la cabeza replicada)
static std::atomic<int> s_cpCalls{0}, s_cpIdle{0}, s_cpVp{0}, s_cpNoCb{0}, s_cpCreateFail{0}, s_cpRace{0}, s_cpCopied{0}; static std::atomic<long> s_cpMapHr{0}; static std::atomic<int> s_cpMapFail{0}; static int s_cpDiagN = 0; static double s_cpDiagT = 0;
static void ShadowCasterProbe(ID3D11DeviceContext* c) {
    s_cpCalls.fetch_add(1, std::memory_order_relaxed);
    if (s_casterWant.load(std::memory_order_relaxed) != 1 || !g_dev.dev) { s_cpIdle.fetch_add(1, std::memory_order_relaxed); return; }   // (02/11 b) tambien en contextos diferidos: la pasada de sombras no va por el inmediato
    D3D11_VIEWPORT vp; UINT n = 1; c->RSGetViewports(&n, &vp);
    if (n < 1 || vp.TopLeftX > 0.5f || vp.TopLeftY > 0.5f) { s_cpVp.fetch_add(1, std::memory_order_relaxed); return; }
    ID3D11Buffer* b = nullptr; c->VSGetConstantBuffers(2, 1, &b); if (!b) { s_cpNoCb.fetch_add(1, std::memory_order_relaxed); return; }
    D3D11_BUFFER_DESC bd; b->GetDesc(&bd);
    if (!s_casterStage || s_casterStageSize != bd.ByteWidth) {
        if (s_casterStage) s_casterStage->Release(); s_casterStage = nullptr;
        D3D11_BUFFER_DESC sd{}; sd.ByteWidth = bd.ByteWidth; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(g_dev.dev->CreateBuffer(&sd, nullptr, &s_casterStage))) { b->Release(); s_casterWant = 0; s_cpCreateFail.fetch_add(1, std::memory_order_relaxed); return; }
        s_casterStageSize = bd.ByteWidth;
    }
    // un solo contexto toma la copia. (03/11) compare_exchange: con exchange(3) un segundo contexto que llegaba tarde pisaba el 2
    // (copia lista) con un 3 y el registro quedaba trabado para siempre en "estado 3, sin pedido" (partido 02/11 22:44)
    { int one = 1; if (!s_casterWant.compare_exchange_strong(one, 3)) { b->Release(); s_cpRace.fetch_add(1, std::memory_order_relaxed); return; } }
    s_cpCopied.fetch_add(1, std::memory_order_relaxed);
    c->CopyResource(s_casterStage, b); b->Release();
    CtxState& cs = Ctx(c); s_casterVS = cs.vs ? cs.vs->hash : 0; s_casterVp[0] = vp.Width; s_casterVp[1] = vp.Height;
    s_casterWant = 2;
}
void ShadowCasterLogPresent() {
    ResourcesLogPresent();
    if (s_casterLogN >= 12 || !g_dev.imm) return;
    int w = s_casterWant.load();
    { double t = Now();   // (02/11 c) diagnostico cada 30 s (10 veces): en que paso se queda la muestra
      // (02/11 d) el tope de 10 se gastaba en el menu (0 dibujos): ahora solo cuentan las lineas con dibujos o con Map fallido,
      // y sin dibujos sale una linea cada 5 minutos (para saber que el registro sigue vivo)
      bool any = s_cpCalls.load() > 0 || s_cpMapFail.load() > 0; static double s_cpIdleT = 0;
      if (s_cpDiagN < 10 && t >= s_cpDiagT && (any || t >= s_cpIdleT)) { s_cpDiagT = t + 30.0; s_cpIdleT = t + 300.0; if (any) s_cpDiagN++;
          Log("sombra jugadores (30 s): estado %d, dibujos en la pasada de sombras %d (sin pedido %d, vista corrida %d, sin b2 %d, no se pudo crear la copia %d, otro contexto %d, copiados %d), Map fallido %d (hr %08lX), muestras %d / 12",
              w, s_cpCalls.exchange(0), s_cpIdle.exchange(0), s_cpVp.exchange(0), s_cpNoCb.exchange(0), s_cpCreateFail.exchange(0), s_cpRace.exchange(0), s_cpCopied.exchange(0), s_cpMapFail.exchange(0), (unsigned long)s_cpMapHr.load(), s_casterLogN); } }
    if (w == 0) { double t = Now(); if (t >= s_casterNext) { s_casterNext = t + 15.0; s_casterWant = 1; } return; }
    if (w != 2 || !s_casterStage) return;
    D3D11_MAPPED_SUBRESOURCE m;
    { HRESULT hr = g_dev.imm->Map(s_casterStage, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
      if (FAILED(hr)) { s_cpMapFail.fetch_add(1, std::memory_order_relaxed); s_cpMapHr.store((long)hr); return; } }   // todavia no ejecuto: se reintenta el proximo segundo
    float f[48] = {}; memcpy(f, m.pData, (std::min)((size_t)s_casterStageSize, sizeof f)); g_dev.imm->Unmap(s_casterStage, 0);
    s_casterWant = 0; s_casterLogN++;
    Log("sombra jugadores: VS %016llX cascada %.0fx%.0f, b2 %u bytes | c0..c3 %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f",
        (unsigned long long)s_casterVS, s_casterVp[0], s_casterVp[1], s_casterStageSize, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15]);
    Log("sombra jugadores: b2 c4..c11 %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f",
        f[16], f[17], f[18], f[19], f[20], f[21], f[22], f[23], f[24], f[25], f[26], f[27], f[28], f[29], f[30], f[31],
        f[32], f[33], f[34], f[35], f[36], f[37], f[38], f[39], f[40], f[41], f[42], f[43], f[44], f[45], f[46], f[47]);
}
template<int V, class F> static void HeadShadowReplay(ID3D11DeviceContext* c, ID3D11DepthStencilView* gameDSV, int kind, F draw) {
    if (g_cfg.live[309][0] < 0.5f) return;   // (31/10) x309 = 0: la DLL tampoco repite dibujos (antes solo el shader dejaba de leer el atlas)
    int n = HeadShadowSlots(); ID3D11DepthStencilView* dsv = HeadShadowDSV();
    if (n <= 0 || !dsv || !HeadShadowIsNearCascade(c, gameDSV, kind)) return;
    ID3D11RenderTargetView* rt[8] = {}; ID3D11DepthStencilView* oldDsv = nullptr; c->OMGetRenderTargets(8, rt, &oldDsv);
    ID3D11Buffer* oldB2 = nullptr; c->VSGetConstantBuffers(2, 1, &oldB2);
    D3D11_VIEWPORT vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetViewports(&nvp, vps);
    D3D11_RECT scs[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; UINT nsc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetScissorRects(&nsc, scs);
    ID3D11RasterizerState* oldRS = nullptr; c->RSGetState(&oldRS);
    if (ID3D11RasterizerState* nc = HeadShadowNoCullRS(oldRS)) c->RSSetState(nc);   // (27/10 f) sin descarte de caras
    o_OMSetRT[V](c, 0, nullptr, dsv);
    const bool body = HeadShadowBodyOn();   // (04/11 piel) cuadrante 3: piernas del mas cercano
    for (int i = 0; i < 4; i++) {
        if (i >= n && !(i == 3 && body)) continue;
        ID3D11Buffer* cb = HeadShadowCB(i); if (!cb) continue;
        D3D11_VIEWPORT vp; HeadShadowViewport(i, vp);
        D3D11_RECT rc{ (LONG)vp.TopLeftX, (LONG)vp.TopLeftY, (LONG)(vp.TopLeftX + vp.Width), (LONG)(vp.TopLeftY + vp.Height) };
        c->RSSetViewports(1, &vp); c->RSSetScissorRects(1, &rc);
        c->VSSetConstantBuffers(2, 1, &cb);
        draw();
    }
    g_headShadowReplays.fetch_add(n + (body ? 1 : 0), std::memory_order_relaxed);
    o_OMSetRT[V](c, 8, rt, oldDsv);
    c->VSSetConstantBuffers(2, 1, &oldB2);
    if (nvp) c->RSSetViewports(nvp, vps); if (nsc) c->RSSetScissorRects(nsc, scs);
    c->RSSetState(oldRS); if (oldRS) oldRS->Release();
    for (auto* r : rt) if (r) r->Release(); if (oldDsv) oldDsv->Release(); if (oldB2) oldB2->Release();
}
// (20/10) LOD0 forzado: el dibujo sale con los buffers/cuentas de la malla de mas detalle y se devuelve el estado del juego
template<int V> static void LodDrawSub(ID3D11DeviceContext* c, DrawScope& ds) { LodIA sv; LodBind(c, ds.lod, sv); o_DrawIdx[V](c, ds.lod.count, ds.lod.start, ds.lod.base); LodRestore(c, sv, ds.lod); }
// (04/11 boca) mascara del interior de la boca (mouthmask.cpp): el mismo DrawIndexed de la boca otra vez, con el PS de la mascara
// sobre la textura R8, la profundidad del juego sin escribir y sin mezcla; despues vuelve todo como estaba. Funciones originales.
template<int V> static void MouthMaskReplay(ID3D11DeviceContext* c, DrawScope& ds) {
    ID3D11RenderTargetView* rt[8] = {}; ID3D11DepthStencilView* dsv = nullptr; c->OMGetRenderTargets(8, rt, &dsv);
    UINT nrt = 0; for (UINT i = 0; i < 8; i++) if (rt[i]) nrt = i + 1;
    ID3D11DepthStencilState* oldDSS = nullptr; UINT oldRef = 0; c->OMGetDepthStencilState(&oldDSS, &oldRef);
    ID3D11PixelShader* mps = nullptr; ID3D11DepthStencilState* mdss = nullptr;
    ID3D11RenderTargetView* mrtv = (dsv && rt[0]) ? MouthMaskBegin(rt[0], oldDSS, &mps, &mdss) : nullptr;
    if (mrtv && mps && mdss) {
        ID3D11PixelShader* oldPS = nullptr; c->PSGetShader(&oldPS, nullptr, nullptr);
        ID3D11BlendState* oldBS = nullptr; float oldBF[4] = {}; UINT oldSM = 0; c->OMGetBlendState(&oldBS, oldBF, &oldSM);
        o_OMSetRT[V](c, 1, &mrtv, dsv);
        o_SetPS[V](c, mps, nullptr, 0);
        c->OMSetDepthStencilState(mdss, 0);
        c->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        if (ds.lod.count) LodDrawSub<V>(c, ds); else o_DrawIdx[V](c, ds.mCnt, ds.mStart, ds.mBase);
        o_OMSetRT[V](c, nrt, rt, dsv);
        o_SetPS[V](c, oldPS, nullptr, 0); if (oldPS) oldPS->Release();
        c->OMSetBlendState(oldBS, oldBF, oldSM); if (oldBS) oldBS->Release();
        c->OMSetDepthStencilState(oldDSS, oldRef);
    }
    if (oldDSS) oldDSS->Release();
    for (auto* r : rt) if (r) r->Release(); if (dsv) dsv->Release();
}
// (04/11) cejas con hebras v2: el mismo DrawIndexed de la cara otra vez, con el GS de hebras (facegen.cpp), ANTES de devolver el
// estado (huesos, gestos, expresiones y render targets de la cara siguen enlazados). Llama la funcion original (sin hooks).
// (07/11 c, Agustin: "la luz no la afecta de manera correcta, no tiene el volumen exacto ni la dimensionalidad 3d") HEBRAS EN EL ATLAS
// de sombra de las cabezas (headshadow.cpp, t30): como TessAtlas, apenas dibujadas las hebras en el G-buffer se repite el mismo
// dibujo sin PS sobre el atlas con el GS de atlas (pes_browat_*_gs: cinta mirando al sol, posicion en la caja de cada cabeza, b12).
// La pasada del sol (pes_headshadow.hlsli, PCSS) lee ese atlas para la piel de la cara y para las hebras mismas: cada pelo da sombra
// a los de abajo y a la piel desde donde viene el sol de verdad. z382 = 0 lo apaga. Solo si el atlas ya se limpio en este cuadro.
template<int V> static void BrowAtlas(ID3D11DeviceContext* c, DrawScope& ds) {
    if (g_cfg.live[309][0] < 0.5f || g_cfg.live[382][2] < 0.5f) return;
    const int n = HeadShadowSlots(); ID3D11DepthStencilView* dsv = HeadShadowDSV();
    if (n <= 0 || !dsv || !HeadShadowClearedThisFrame()) return;
    ID3D11GeometryShader* ags = BrowAtlasGS(ds.brow.browVS); if (!ags) return;
    ID3D11RenderTargetView* rt[8] = {}; ID3D11DepthStencilView* oldDsv = nullptr; c->OMGetRenderTargets(8, rt, &oldDsv);
    UINT nrt = 0; for (UINT i = 0; i < 8; i++) if (rt[i]) nrt = i + 1;
    ID3D11PixelShader* oldPS = nullptr; c->PSGetShader(&oldPS, nullptr, nullptr);
    ID3D11GeometryShader* oldGS = nullptr; c->GSGetShader(&oldGS, nullptr, nullptr);
    ID3D11Buffer* oldB12 = nullptr; c->GSGetConstantBuffers(12, 1, &oldB12);
    D3D11_VIEWPORT vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetViewports(&nvp, vps);
    D3D11_RECT scs[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; UINT nsc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetScissorRects(&nsc, scs);
    ID3D11RasterizerState* oldRS = nullptr; c->RSGetState(&oldRS);
    ID3D11DepthStencilState* oldDSS = nullptr; UINT oldRef = 0; c->OMGetDepthStencilState(&oldDSS, &oldRef);
    o_OMSetRT[V](c, 0, nullptr, dsv);
    o_SetPS[V](c, nullptr, nullptr, 0);   // sin PS: solo profundidad
    c->GSSetShader(ags, nullptr, 0);
    // sin culling (cintas de ambos lados): el RS de la pasada de sombras del juego lleva el sesgo del juego; el de las hebras no
    if (ID3D11RasterizerState* rs = HeadShadowAtlasRS(oldRS)) c->RSSetState(rs);
    if (ID3D11DepthStencilState* dss = HeadShadowAtlasDSS()) c->OMSetDepthStencilState(dss, 0);
    static std::atomic<int> s_logged{0};
    if (s_logged.fetch_add(1) < 1) Log("cejas: hebras en el atlas de sombra (%d cabezas, VS %s)", n, ds.brow.browVS == 0x455eb97bc2c3a958ULL ? "455eb97b" : "faab87ee");
    for (int i = 0; i < n; i++) {
        ID3D11Buffer* cb = HeadShadowCB(i); if (!cb) continue;
        D3D11_VIEWPORT vp; HeadShadowViewport(i, vp);
        D3D11_RECT rc{ (LONG)vp.TopLeftX, (LONG)vp.TopLeftY, (LONG)(vp.TopLeftX + vp.Width), (LONG)(vp.TopLeftY + vp.Height) };
        c->RSSetViewports(1, &vp); c->RSSetScissorRects(1, &rc);
        c->GSSetConstantBuffers(12, 1, &cb);
        if (ds.lod.count && !ds.hdMesh) LodDrawSub<V>(c, ds); else o_DrawIdx[V](c, ds.brow.browCount, ds.brow.browStart, ds.brow.browBase);
    }
    o_OMSetRT[V](c, nrt, rt, oldDsv);
    o_SetPS[V](c, oldPS, nullptr, 0);
    c->GSSetShader(oldGS, nullptr, 0); c->GSSetConstantBuffers(12, 1, &oldB12);
    if (nvp) c->RSSetViewports(nvp, vps); if (nsc) c->RSSetScissorRects(nsc, scs);
    c->RSSetState(oldRS); c->OMSetDepthStencilState(oldDSS, oldRef);
    for (auto* r : rt) if (r) r->Release(); if (oldDsv) oldDsv->Release(); if (oldPS) oldPS->Release(); if (oldGS) oldGS->Release(); if (oldB12) oldB12->Release();
    if (oldRS) oldRS->Release(); if (oldDSS) oldDSS->Release();
}
template<int V> static void BrowStrands(ID3D11DeviceContext* c, DrawScope& ds) {
    if (!BrowStrandsBegin(c, &ds.brow, ds.brow.browVS)) return;
    if (BrowShadowOn(c)) {   // (04/11 k) sombra de contacto de los pelos sobre la piel (antes de las hebras: ellas no se oscurecen)
        if (ds.lod.count && !ds.hdMesh) LodDrawSub<V>(c, ds); else o_DrawIdx[V](c, ds.brow.browCount, ds.brow.browStart, ds.brow.browBase);
        BrowShadowOff(c);
    }
    if (BrowV3Roots(c, ds.brow.browVS)) {   // (08/11 y) hebras v3: raices y despues cada hebra con hasta 16 segmentos
      if (ds.lod.count) LodDrawSub<V>(c, ds); else o_DrawIdx[V](c, ds.brow.browCount, ds.brow.browStart, ds.brow.browBase);
      BrowV3Strands(c);
      if (g_cfg.live[402][0] > 0.5f) {   // (08/11 y) la barba sigue con las hebras de antes (el GS viejo con x416 salta las cejas)
        bool so = BrowSOOn(c);   // (08/11 z3) en modo mixto la barba tambien sigue en el G-buffer: entra a la pasada con su borde
        if (ds.lod.count) LodDrawSub<V>(c, ds); else o_DrawIdx[V](c, ds.brow.browCount, ds.brow.browStart, ds.brow.browBase);
        if (so) BrowSOOff(c); }
    } else
    { bool so = BrowSOOn(c);   // (08/11 v) las hebras tambien a la salida de flujo (pasada propia del pelo tras la composicion)
      if (ds.lod.count && !ds.hdMesh) LodDrawSub<V>(c, ds); else o_DrawIdx[V](c, ds.brow.browCount, ds.brow.browStart, ds.brow.browBase);
      if (so) BrowSOOff(c); }
    BrowAtlas<V>(c, ds);   // (07/11 c) con el estado de las hebras aun puesto (GS: b2 / b10 / b11, t0 / t23 / t24 / t26 / t27 / t120 / t121)
    BrowStrandsEnd(c);
}
// (04/11 piel) CARA TESELADA EN EL ATLAS de las cabezas (headshadow.cpp, t30: sombra propia de la cabeza y grosor de la piel). La
// repeticion de la pasada de sombras del juego (HeadShadowReplay) dibuja la malla del modelo, sin teselar: la sombra de la nariz y
// del menton y el grosor del contraluz salian de los triangulos. Aca, apenas termina el dibujo de la piel de la cara teselada en el
// G-buffer, se repite el mismo dibujo (mismo VS, mismo HS = mismos triangulos) sin PS y sobre el atlas, con el DS del atlas
// (pes_face_tess_*_atlas_ds.hlsl: la posicion teselada de la vista al mundo y del mundo a la caja de cada cabeza, b12). Solo si el
// atlas ya se limpio en este cuadro (la pasada de sombras del juego va ANTES del G-buffer); si no, se saltea y queda en el registro.
// En el atlas quedan las dos (la del modelo y la teselada) y gana la mas cercana al sol: la cara redondeada en lo convexo.
template<int V> static void TessAtlas(ID3D11DeviceContext* c, DrawScope& ds) {
    if (ds.tess.v < 0 || ds.tess.v > 1 || !ds.tess.cnt) return;
    if (g_cfg.live[309][0] < 0.5f) return;
    const int n = HeadShadowSlots(); ID3D11DepthStencilView* dsv = HeadShadowDSV();
    if (n <= 0 || !dsv) return;
    if (!HeadShadowClearedThisFrame()) { HeadShadowTessNote(1); return; }
    ID3D11DomainShader* ads = FaceTessAtlasDS(ds.tess.v); if (!ads) { HeadShadowTessNote(2); return; }
    ID3D11RenderTargetView* rt[8] = {}; ID3D11DepthStencilView* oldDsv = nullptr; c->OMGetRenderTargets(8, rt, &oldDsv);
    UINT nrt = 0; for (UINT i = 0; i < 8; i++) if (rt[i]) nrt = i + 1;   // al devolver: solo los slots que estaban (no pisar UAVs de mas arriba)
    ID3D11PixelShader* oldPS = nullptr; c->PSGetShader(&oldPS, nullptr, nullptr);
    ID3D11DomainShader* oldDS = nullptr; c->DSGetShader(&oldDS, nullptr, nullptr);
    ID3D11Buffer* oldB12 = nullptr; c->DSGetConstantBuffers(12, 1, &oldB12);
    D3D11_VIEWPORT vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetViewports(&nvp, vps);
    D3D11_RECT scs[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; UINT nsc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetScissorRects(&nsc, scs);
    ID3D11RasterizerState* oldRS = nullptr; c->RSGetState(&oldRS);
    ID3D11DepthStencilState* oldDSS = nullptr; UINT oldRef = 0; c->OMGetDepthStencilState(&oldDSS, &oldRef);
    o_OMSetRT[V](c, 0, nullptr, dsv);
    o_SetPS[V](c, nullptr, nullptr, 0);   // sin PS (solo profundidad): la funcion original, el registro del contexto (Ctx.ps) no cambia
    c->DSSetShader(ads, nullptr, 0);
    if (ID3D11RasterizerState* rs = HeadShadowAtlasRS(oldRS)) c->RSSetState(rs);
    if (ID3D11DepthStencilState* dss = HeadShadowAtlasDSS()) c->OMSetDepthStencilState(dss, 0);
    for (int i = 0; i < n; i++) {
        ID3D11Buffer* cb = HeadShadowCB(i); if (!cb) continue;
        D3D11_VIEWPORT vp; HeadShadowViewport(i, vp);
        D3D11_RECT rc{ (LONG)vp.TopLeftX, (LONG)vp.TopLeftY, (LONG)(vp.TopLeftX + vp.Width), (LONG)(vp.TopLeftY + vp.Height) };
        c->RSSetViewports(1, &vp); c->RSSetScissorRects(1, &rc);
        c->DSSetConstantBuffers(12, 1, &cb);
        if (ds.lod.count) LodDrawSub<V>(c, ds); else o_DrawIdx[V](c, ds.tess.cnt, ds.tess.start, ds.tess.base);
        HeadShadowTessNote(0);
    }
    o_OMSetRT[V](c, nrt, rt, oldDsv);
    o_SetPS[V](c, oldPS, nullptr, 0);
    c->DSSetShader(oldDS, nullptr, 0); c->DSSetConstantBuffers(12, 1, &oldB12);
    if (nvp) c->RSSetViewports(nvp, vps); if (nsc) c->RSSetScissorRects(nsc, scs);
    c->RSSetState(oldRS); c->OMSetDepthStencilState(oldDSS, oldRef);
    for (auto* r : rt) if (r) r->Release(); if (oldDsv) oldDsv->Release(); if (oldPS) oldPS->Release(); if (oldDS) oldDS->Release(); if (oldB12) oldB12->Release();
    if (oldRS) oldRS->Release(); if (oldDSS) oldDSS->Release();
}
template<int V> static void STDMETHODCALLTYPE hk_DrawIdx(ID3D11DeviceContext* c, UINT a, UINT b, INT d) {
    GRASS_FLUSH(c)
    { CtxState& cg = Ctx(c); const ShaderEntry* gvs = cg.vs;
      if (g_grassProbeOn) CatalogGrassDraw(c, gvs, a, b, d);
      if (gvs && gvs->hash == 0x498b30bad256e8edULL) GrassOnDraw(c, gvs, cg.ps, a, b, d); else GrassClearSkip();   // (2/11 b) la omision x178 es solo de ESTE dibujo
      if (gvs && gvs->hash == 0x1c02a92ab4ae3362ULL) BodyOnDraw(c, a, b, d);   // (25/10) piel del cuerpo -> la ropa choca con ella (es fisica: no depende de "Ver original")
      else if (cg.ps && ShirtPSKind(cg.ps->hash) >= 0 && g_cfg.live[327][3] > 0.5f) BodyOnDraw(c, a, b, d, 1);   // (27/10 d) camiseta: medicion contra el torso
      if (g_autoDumpAny) InspectAutoDump(c, cg.ps); }
    t_drawStart = b; t_drawBase = d;
    GrassStScope gst; GrassGameStates(c, gst);   // (26/10 g) solo el pasto del juego del G-buffer (lo marca GrassOnDraw)
    FX_DRAW(a, 1, 1, (ds.lod.count ? LodDrawSub<V>(c, ds) : o_DrawIdx[V](c, a, b, d)))
    GrassGameStatesRestore(c, gst);
    { CtxState& ch_ = Ctx(c); if (ch_.gameShadow) ShadowCasterProbe(c); if (ch_.shadowDSV && !ds.skip) HeadShadowReplay<V>(c, ch_.shadowDSV, 1, [&] { o_DrawIdx[V](c, a, b, d); }); }   // (27/10 c) sombra de la cabeza
    // (25/10) pasto v3: al terminar la base de la cancha (PS 5e92 / c956, existe en todas las camaras) se emite el pasto del mod
    { CtxState& cb_ = Ctx(c); if (cb_.ps && (cb_.ps->hash == 0x5e92f2343848915aULL || cb_.ps->hash == 0xc9564b9b155f0a49ULL)) GrassOnBaseDraw(c); }
    // (08/11 c) x393: pasto del mod en el editor, despues de la cara (estado ya devuelto). (08/11 j, Agustin: "el pasto parpadea")
    // tambien tras la piel del cuerpo (VS 1c02a92a): si un cuadro no dibuja la cara en el G-buffer (cerca de las piernas, cara fuera
    // de cuadro o en otra pasada) el pasto no se emitia ese cuadro y parpadeaba; el primero de los dos por cuadro emite.
    { CtxState& ce_ = Ctx(c); if (!ds.faceId.empty() || (ce_.vs && ce_.vs->hash == 0x1c02a92ab4ae3362ULL)) GrassEditorAfterFace(c); } }
template<int V> static void STDMETHODCALLTYPE hk_Draw(ID3D11DeviceContext* c, UINT a, UINT b) { GRASS_FLUSH(c) FX_DRAW(a, 0, 1, o_Draw[V](c, a, b))
    { CtxState& ch_ = Ctx(c); if (ch_.gameShadow) ShadowCasterProbe(c); if (ch_.shadowDSV && !ds.skip) HeadShadowReplay<V>(c, ch_.shadowDSV, 0, [&] { o_Draw[V](c, a, b); }); } }
// (25/10) dibujo indirecto (pasto v3): pasa por BeforeDraw como cualquier dibujo (reglas, t120); vertices e instancias desconocidos
template<int V> static void STDMETHODCALLTYPE hk_DrawIdxInstInd(ID3D11DeviceContext* c, ID3D11Buffer* a, UINT b) { GRASS_FLUSH(c) FX_DRAW(0, 3, 0, o_DrawIdxInstInd[V](c, a, b)) }
template<int V> static void STDMETHODCALLTYPE hk_DrawIdxInst(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, INT e, UINT f) { GRASS_FLUSH(c) FX_DRAW(a, 3, b, o_DrawIdxInst[V](c, a, b, d, e, f))
    { CtxState& ch_ = Ctx(c); if (ch_.gameShadow) ShadowCasterProbe(c); if (ch_.shadowDSV && !ds.skip) HeadShadowReplay<V>(c, ch_.shadowDSV, 3, [&] { o_DrawIdxInst[V](c, a, b, d, e, f); }); } }
template<int V> static void STDMETHODCALLTYPE hk_DrawInst(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, UINT e) { GRASS_FLUSH(c) FX_DRAW(a, 2, b, o_DrawInst[V](c, a, b, d, e))
    { CtxState& ch_ = Ctx(c); if (ch_.gameShadow) ShadowCasterProbe(c); if (ch_.shadowDSV && !ds.skip) HeadShadowReplay<V>(c, ch_.shadowDSV, 2, [&] { o_DrawInst[V](c, a, b, d, e); }); } }

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
        GpuMeterHookDevice(d);   // (07/11) medidor de VRAM por dueno (CreateBuffer / CreateTexture*)
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
static std::atomic<bool> s_planBMode{false};   // Present enganchado por el plan B (vtable compartida de dxgi)
static std::atomic<long> s_presentCalls{0};
static thread_local int t_inPresent = 0;        // (12/11) otro mod que vuelve a llamar Present desde adentro: pasa derecho
struct PresentDepth { PresentDepth() { ++t_inPresent; } ~PresentDepth() { --t_inPresent; } };
static HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (t_inPresent) return o_Present(sc, sync, flags);
    PresentDepth depth_;
    s_presentCalls.fetch_add(1, std::memory_order_relaxed);
    ID3D11Device* d = nullptr;
    if (s_planBMode.load(std::memory_order_relaxed) && g_dev.dev && SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&d)) && d) {
        const bool otro = (d != g_dev.dev); d->Release(); d = nullptr;
        if (otro) return o_Present(sc, sync, flags);   // plan B: la vtable es de todas las cadenas; se queda con la primera que presenta (el juego)
    }
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
        const double now = Now();
        for (auto* p : s_deferredRelease) s_aged.push_back({ p, now });
        s_deferredRelease.clear();
        size_t k = 0;
        for (auto& a : s_aged) { if (now - a.t > 30.0) a.p->Release(); else s_aged[k++] = a; }
        s_aged.resize(k);
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
    GpuMeterPresent();  // (07/11) medidor de VRAM y de ms de GPU: lectura de consultas, marca del cuadro, log cada 10 s
    SunPassPresent();   // (25/10) variantes de la luz del sol: log de cambios
    LightTraceSummary();   // (27/10) quien llena el cbuffer de la luz
    UpdateTimeParams();
    SSGIPresent(s_presentNo.load());
    LightMeterPresent();
    RastroPresent();    // (08/11 i) rastro del motor: ganchos en el primer Present (rastro = 1)
    RastroEventosPresent();   // (08/11) eventos del partido para los gestos del publico (fila 394)
    { static ULONGLONG t = 0; ULONGLONG n = GetTickCount64(); if (n - t > 10000) { t = n; RastroFlush(false); } }
    FaceInjectPresent();   // (09/11) matrices del rig: donde se arma la cara (solo mide, expr_inyeccion)
    SolEditorPresent();   // (09/11) sol real del editor: ganchos (primer Present), aplicar pendiente, log cada 10 s
    CamHookPresent();   // (23/10) camara del motor: ganchos (primer Present), cortes de camara -> 160, log
    MotorPagePresent();   // (11/11) Motor en tiempo real: filas nativas en el estudio 3D (motorpage.cpp)
    MenuMousePresent();   // (11/11 3icxbh) mouse en los menus nativos del juego: ganchos (primer Present), log
    TiempoPresent();    // (31/10) camara lenta: ganchos (visor del editor / reloj del partido, primer Present), 343.w, log
    SolLuzPresent();    // (09/11) sol real del motor en el editor (w378 / w392 -> zfx_SolEditor), z393
    CmdListGuardPresent();
    MouthDrawDiagPresent();   // (09/11) diagnostico del recuadro de la boca (un cuadro cada 10 s)
    StateCachePresent();   // (08/11) estados del mod por descripcion (statecache.cpp)
    RelWatchPresent();   // (08/11 g) quien suelta la ultima referencia de cada estado (cierre 11:19 en LUCDeferDeletion)   // (08/11) locks de las listas de comandos del juego con tope de espera (cuelgues del editor)
    FreeCamPresent();   // (1/11) camara libre: gancho a la camara del visor del editor (primer Present), log
    CharTrackPresent();    // (24/10) personajes: que jugador es cada esqueleto (ganchos en el primer Present, log)
    SkinDiagPresent();     // (24/10) tono de piel: diagnostico de los dibujos de personajes (w184)
    SkinTonePresent();     // (24/10) tono de piel por jugador: guardado y vista previa en el menu Fisico
    EditorBgPresent();   // (08/11 qk2yxa) x424: cuadro nuevo, el fondo vuelve a ir antes de la escena
    { GpuZoneScope gz_(g_dev.imm, kGzGlass); GlassPresent(); }        // (24/10) vidrio de los menus: lectura de lo anotado por el PS y plan de copias del fondo
    MuscleHookPresent();   // (23/10) musculatura: ganchos a los huesos de musculo del motor (primer Present)
    ExprPresent();         // (31/10) expresiones faciales: mallas de cara nuevas (cabeza en la paleta)
    WrPlayerPresent();     // (10/11) arrugas por jugador
    FaceTessPresent();     // (04/11 b) teselacion de la cara: resumen
    { GpuZoneScope gz_(g_dev.imm, kGzBody); BodyPresent(); }         // (25/10) la ropa contra el cuerpo real: mallas de piel nuevas, cuadro nuevo
    MouthMaskPresent();    // (04/11 boca) mascara del interior de la boca: limpiar para el cuadro siguiente
    ArmsPresent();         // (27/10) brazos de TorsoBody para las caras reales: huellas de los torsos del juego, claves del cuadro
    { static unsigned s_noShirt = 1000; s_noShirt = g_shirtSeen.exchange(false) ? 0 : (std::min)(s_noShirt + 1, 1000u);   // (27/10 c) fila 320.z: camisetas en los ultimos 30 Present
      // (27/10 d) en el menu Fisico el jugador se ve sin camiseta: aunque haya camisetas de otros modelos, el torso va entero
      // (si no, el pecho se hunde x320 mm debajo del cuello y de los hombros, que no se mueven, y queda un escalon)
      const bool fis = MusclePhysiqueActive();
      const float on = (s_noShirt < 30 && !fis) ? 1.0f : 0.0f;
      if (on != g_cfg.live[320][2]) Log("torso bajo la camiseta: %s", on > 0.5f ? "hay camisetas en escena (el torso se mete x320 mm)" : fis ? "menu Fisico (torso entero)" : "sin camisetas en escena (torso entero)");
      g_cfg.live[320][2] = on; }
    GrassFrameBoundary(flags);   // (26/10 d) pasto: limite del cuadro (reclamo en la GPU, x63) ANTES de subir t120
    HeadShadowPresent();   // (27/10 c) sombra de la cabeza: cabezas elegidas, cajas y filas 310 / 316..319 ANTES de subir t120
    MotorLinkPresent();    // (11/11 3icxbh) Motor en tiempo real: pedido del modulo de Sider -> fila 429, ANTES de subir t120
    UploadParams();
    WatchShaderFolder();
    PollMatch();
    CatalogPresent();
    LodPresent();
    ExeDumpPresent();
    ExePatchPresent();
    GrassDistPresent();
    { GpuZoneScope gz_(g_dev.imm, kGzPitchWear); PitchWearPresent(); }
    PublicoPresent();   // (08/11) gestos del publico: sube el buffer que calcula el hilo de la CPU (fila 394)
    { GpuZoneScope gz_(g_dev.imm, kGzGrass); GrassPresent(); }
    { GpuZoneScope gz_(g_dev.imm, kGzFaceGen); FaceGenPresent(); }
    { GpuZoneScope gz_(g_dev.imm, kGzCarasHd); CarasHdPresent(); CaraMallaPresent(); }   // (08/11 p) cara con mas poligonos
    InspectPresent();
    { GpuZoneScope gz_(g_dev.imm, kGzFrameCopy); FrameCopyPresent(sc); }   // antes del menu, para que el menu no se refleje en los ojos
    AsyncShotPresent();
    { GpuZoneScope gz_(g_dev.imm, kGzRing); RingPresent(sc); }       // (24/10 e) aro del musculo sobre la imagen final (antes de las capturas y del menu)
    AutoCapturePresent(s_frameTex);
    PanelPresent(s_frameTex);   // (08/11) panel en vivo: recargas pedidas por HTTP y capturas (panel.cpp)
    FisicoCapturePresent(s_frameTex);   // (26/10 c) paginas del menu Fisico
    MenuTestPresent(sc, s_frameTex);   // (08/11) prueba automatica v2: mide, fotografia y escribe PES21FX/diagnostico/<fecha>/ (OJO: nunca terminar un comentario // con barra invertida: continua la linea y se come la siguiente)
    { GpuZoneScope gz_(g_dev.imm, kGzOverlay); OverlayPresent(sc); }
    HRESULT hrP = o_Present(sc, sync, flags);
    if (hrP == DXGI_ERROR_DEVICE_REMOVED || hrP == DXGI_ERROR_DEVICE_RESET) {   // (08/11 e) placa reiniciada (TDR)
        ID3D11Device* d = g_dev.dev; HRESULT rr = d ? d->GetDeviceRemovedReason() : hrP;
        OnDeviceLost((unsigned)(FAILED(rr) ? rr : hrP));
    }
    return hrP;
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

// (12/11) plan B del Present: en una PC con PES 2021 CPY + DreamPatch Switcher (Sider cargado por el Switcher, despues de
// nosotros) el juego nunca paso por los CreateSwapChain enganchados: sin "cadena de intercambio", sin menu Insert y sin los
// ganchos que se ponen en el primer Present. Si a los 15 s de crear el dispositivo no hay cadena, se arma una cadena de prueba
// (ventana oculta, dispositivo aparte con el d3d11 de Windows) solo para leer de su vtable donde esta el codigo de Present y de
// ResizeBuffers, y se engancha el Present verdadero de dxgi.dll (leido del archivo, v4), sin tocar
// la vtable ni el codigo de otro mod. Si la cadena del juego ya se engancho por el camino normal no se hace nada (en 1.07.02 asi es siempre).
}   // namespace fx
HMODULE fx_RealD3D11();   // d3d11_proxy.cpp (fuera del namespace)
namespace fx {
static void* DxgiSlotFromDisk(void** slot) {
    // la vtable esta en dxgi.dll: se lee el mismo lugar en el archivo y se pasa a la direccion cargada
    HMODULE m = nullptr; wchar_t path[MAX_PATH];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)slot, &m) || !m) return nullptr;
    if (m != GetModuleHandleW(L"dxgi.dll")) return nullptr;
    if (!GetModuleFileNameW(m, path, MAX_PATH)) return nullptr;
    const uintptr_t base = (uintptr_t)m, rva = (uintptr_t)slot - base;
    auto dos = (IMAGE_DOS_HEADER*)m; auto nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    void* res = nullptr;
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (rva < sec->VirtualAddress || rva + 8 > sec->VirtualAddress + sec->SizeOfRawData) continue;
        LARGE_INTEGER off; off.QuadPart = sec->PointerToRawData + (rva - sec->VirtualAddress);
        uint64_t v = 0; DWORD got = 0;
        if (SetFilePointerEx(f, off, nullptr, FILE_BEGIN) && ReadFile(f, &v, 8, &got, nullptr) && got == 8) {
            // ImageBase del archivo (en memoria el cargador la reescribe)
            IMAGE_DOS_HEADER fd{}; IMAGE_NT_HEADERS64 fn{}; LARGE_INTEGER z{}; DWORD g2 = 0;
            if (SetFilePointerEx(f, z, nullptr, FILE_BEGIN) && ReadFile(f, &fd, sizeof fd, &g2, nullptr) && g2 == sizeof fd) {
                LARGE_INTEGER o2; o2.QuadPart = fd.e_lfanew;
                if (SetFilePointerEx(f, o2, nullptr, FILE_BEGIN) && ReadFile(f, &fn, sizeof fn, &g2, nullptr) && g2 == sizeof fn && fn.Signature == IMAGE_NT_SIGNATURE) {
                    const uint64_t prva = v - fn.OptionalHeader.ImageBase;
                    if (prva < nt->OptionalHeader.SizeOfImage) {
                        // tiene que caer en una seccion de codigo
                        auto s2 = IMAGE_FIRST_SECTION(nt);
                        for (unsigned k = 0; k < nt->FileHeader.NumberOfSections; ++k, ++s2)
                            if ((s2->Characteristics & IMAGE_SCN_MEM_EXECUTE) && prva >= s2->VirtualAddress && prva < s2->VirtualAddress + s2->Misc.VirtualSize)
                                res = (void*)(base + prva);
                    }
                }
            }
        }
        break;
    }
    CloseHandle(f);
    return res;
}
static DWORD WINAPI PresentPlanB(void*) {
    Sleep(15000);
    {
        std::lock_guard<std::recursive_mutex> lk(s_hookMutex);
        if (s_scHooked || !g_cfg.optPresent) return 0;
    }
    Log("Present: a los 15 s el juego no paso por CreateSwapChain (sin menu ni ganchos del primer Present): plan B");
    auto cdsc = (PFN_D3D11CreateDeviceAndSwapChain)(::fx_RealD3D11() ? GetProcAddress(::fx_RealD3D11(), "D3D11CreateDeviceAndSwapChain") : nullptr);
    if (!cdsc) { Log("Present: plan B sin D3D11CreateDeviceAndSwapChain de Windows"); return 0; }
    WNDCLASSEXW wc{}; wc.cbSize = sizeof wc; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"PES21FX_planB";
    RegisterClassExW(&wc);
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!w) { Log("Present: plan B sin ventana de prueba (%lu)", GetLastError()); return 0; }
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1; sd.BufferDesc.Width = 64; sd.BufferDesc.Height = 64; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = w; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; D3D_FEATURE_LEVEL fl{};
    HRESULT hr = cdsc(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    if (SUCCEEDED(hr) && sc) {
        void** vt = VT(sc);
        void* pres = vt[8]; void* resz = vt[13];
        std::lock_guard<std::recursive_mutex> lk(s_hookMutex);
        if (!s_scHooked) {
            // (12/11 v4) ni MinHook sobre el codigo de otro mod (v2: dsound.dll+0xeaf0 del cloth-mod, pila desbordada) ni
            // cambio de la vtable (v3: el cloth-mod vuelve a poner su puntero, Sider y cloth-mod quedan llamandose entre si,
            // pila desbordada en sider.dll). Se engancha el Present VERDADERO de dxgi.dll, el ultimo de la cadena: su direccion
            // se lee de la vtable de dxgi.dll en el ARCHIVO (la de memoria la pisan los otros mods). Nadie mira ese codigo.
            // (12/11 v5) el plan B ya NO engancha nada: con el cloth-mod y Sider ya enganchados, cambiar la vtable (v3) o
            // meterse en el Present (v2, v4) cerro el juego (pila desbordada). Solo deja anotado que paso para el log.
            void* realPres = DxgiSlotFromDisk(&vt[8]);
            Log("Present: plan B (solo aviso, sin enganchar): en la vtable Present = %s, ResizeBuffers = %s; Present de dxgi.dll = %s. Sin menu",
                ModOff(pres).c_str(), ModOff(resz).c_str(), realPres ? ModOff(realPres).c_str() : "?");
        } else Log("Present: plan B: la cadena del juego se engancho mientras tanto, no se hace nada");
    } else Log("Present: plan B: no se pudo crear la cadena de prueba (%08X)", (unsigned)hr);
    if (sc) sc->Release();
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    DestroyWindow(w); UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (s_planBMode) {
        const long c0 = s_presentCalls.load();
        Sleep(10000);
        const long n = s_presentCalls.load() - c0;
        if (n > 0) Log("Present: plan B: el juego paso %ld veces por nuestro Present en 10 s", n);
        else Log("Present: plan B: en 10 s el juego no paso por nuestro Present (su cadena no usa esta vtable)");
    }
    return 0;
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
    MouthMaskReloadShaders();
    PitchWearReloadShaders();
    LightMeterReloadShaders();
    GTAOReloadShaders();
    FaceTessReloadShaders();
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
    KeyBlockInstall();   // (31/10) teclas del juego bloqueadas mientras se escribe en el menu
    ExePatchInstall();   // (20/10) parches en memoria del motor (antes de que el juego lea su configuracion grafica)
    return true;
}

// llamado por nuestro D3D11CreateDevice (somos el d3d11.dll de la carpeta del juego)
void OnDeviceAndSwapChainCreated(ID3D11Device* d, IDXGISwapChain* sc) {
    Log("D3D11CreateDeviceAndSwapChain: el juego creo dispositivo y cadena juntos");
    OnSwapChain(d, sc);
}
void OnDeviceCreated(ID3D11Device* d) {
    if (!d) return;
    HookDevice(d);
    static std::atomic<bool> s_planB{false};   // (12/11) plan B del Present, una sola vez
    if (!s_planB.exchange(true)) if (HANDLE t = CreateThread(nullptr, 0, PresentPlanB, nullptr, 0, nullptr)) CloseHandle(t);
}
static std::atomic<UINT> s_devFlags{0xffffffffu};
void NoteDeviceFlags(UINT flags, bool ok) {
    s_devFlags.store(flags);
    Log("dispositivo D3D11: el juego lo pidio con banderas 0x%x%s%s", flags,
        (flags & D3D11_CREATE_DEVICE_SINGLETHREADED) ? " (SINGLETHREADED: se quita, el mod crea texturas desde otros hilos)" : " (multihilo)", ok ? "" : " - FALLO");
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
