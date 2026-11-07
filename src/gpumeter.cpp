// PES21 FX (07/11) - MEDIDOR DE MEMORIA DE VIDEO Y DE COSTO EN LA GPU (pedido de Agustin: "enfocarme en la optimizacion y el poco
// uso de la memoria"; primero medir, despues recortar). Solo mide: no cambia nada de lo que se dibuja.
//
// 1) VRAM del proceso: IDXGIAdapter3::QueryVideoMemoryInfo (memoria local de la placa y compartida): uso actual y presupuesto
//    que da Windows, con el pico de la sesion.
// 2) VRAM por dueno: se enganchan CreateBuffer / CreateTexture1D / 2D / 3D del dispositivo. Cada recurso creado se anota con su
//    tamano estimado (formato x mips x capas x muestras; sin la alineacion del driver) y, con ID3DDestructionNotifier, se borra
//    de la cuenta cuando se destruye. Dueno: el juego (exe o memoria sin modulo), otra DLL (Sider, mod de ropa, ...) o el mod.
//    En el mod, el archivo de origen sale de la pila (RtlCaptureStackBackTrace) y de d3d11.map (mapa del enlazador que la CI
//    copia al paquete): "gtao.obj", "caras_hd.obj" ... Sin el mapa, el log da la direccion (+0x...) para buscarla despues.
//    Las texturas de DirectXTK (DDSTextureLoader: caras HD) se cuentan para el archivo que las pidio.
// 3) Costo en la GPU: consultas de tiempo alrededor de cada efecto del mod (SSSS, GTAO, SSGI, medidor de luz, las tareas de
//    Present) y del cuadro entero (de Present a Present). Se leen 3 cuadros despues, sin esperar a la GPU.
// Cada 10 s: lineas "medidor: ..." en el log y un resumen en el menu Insert (Diagnostico > Medidor de memoria y rendimiento).
// Fila 375: x375 = 1 prende el medidor (menu, al instante). [PES21FX] medidor = 0 ni siquiera pone los ganchos (al reiniciar).
#include "core.h"
#include <dxgi1_4.h>
#include <d3dcommon.h>
#include <MinHook.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

namespace fx {

// ---------------------------------------------------------------- tamano de un recurso
static UINT FmtBits(DXGI_FORMAT f) {
    const int v = (int)f;
    if (v >= 1 && v <= 4) return 128;
    if (v >= 5 && v <= 8) return 96;
    if (v >= 9 && v <= 22) return 64;
    if (v >= 23 && v <= 47) return 32;
    if (v >= 48 && v <= 59) return 16;
    if (v >= 60 && v <= 65) return 8;
    if (v == 66) return 1;
    if (v >= 67 && v <= 69) return 32;
    if (v == 85 || v == 86 || v == 115) return 16;
    if (v >= 87 && v <= 93) return 32;
    return 32;
}
static int BcBytes(DXGI_FORMAT f) {   // bytes por bloque de 4 x 4 (0 = no es BC)
    const int v = (int)f;
    if ((v >= 70 && v <= 72) || (v >= 79 && v <= 81)) return 8;                 // BC1, BC4
    if ((v >= 73 && v <= 78) || (v >= 82 && v <= 84) || (v >= 94 && v <= 99)) return 16;   // BC2, BC3, BC5, BC6H, BC7
    return 0;
}
static UINT MipCount(UINT mips, UINT w, UINT h, UINT d) {
    if (mips) return mips;
    UINT m = (std::max)((std::max)(w, h), d), n = 1; while (m > 1) { m >>= 1; n++; } return n;
}
static uint64_t SurfBytes(DXGI_FORMAT f, UINT w, UINT h) {
    if (int bc = BcBytes(f)) return (uint64_t)((w + 3) / 4) * ((h + 3) / 4) * bc;
    return ((uint64_t)w * FmtBits(f) + 7) / 8 * h;
}
static uint64_t TexBytes(DXGI_FORMAT f, UINT w, UINT h, UINT d, UINT mips, UINT arr, UINT samples) {
    uint64_t s = 0; const UINT n = MipCount(mips, w, h, d);
    for (UINT i = 0; i < n; i++) {
        s += SurfBytes(f, (std::max)(1u, w >> i), (std::max)(1u, h >> i)) * (std::max)(1u, d >> i);
    }
    return s * (std::max)(1u, arr) * (std::max)(1u, samples);
}

// ---------------------------------------------------------------- registro de recursos vivos
enum { kCatTex, kCatRT, kCatUAV, kCatMesh, kCatCB, kCatBuf, kCatStaging, kCatN };
static const char* const kCatName[kCatN] = { "texturas", "targets", "UAV", "mallas", "cbuffers", "buffers", "staging (RAM)" };
static int Cat(bool tex, UINT bind, D3D11_USAGE usage) {
    if (usage == D3D11_USAGE_STAGING) return kCatStaging;
    if (bind & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL)) return kCatRT;
    if (bind & D3D11_BIND_UNORDERED_ACCESS) return kCatUAV;
    if (tex) return kCatTex;
    if (bind & (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER)) return kCatMesh;
    if (bind & D3D11_BIND_CONSTANT_BUFFER) return kCatCB;
    return kCatBuf;
}
enum { kOwnGame, kOwnOther, kOwnMod };
struct LiveRes { uint64_t bytes; int cat; int owner; int otherIdx; uintptr_t frames[4]; };
// a proposito sin destruir (new y nunca delete): el juego libera recursos despues del final de la DLL al salir
static std::mutex* s_mx = new std::mutex;
static std::unordered_map<void*, LiveRes>* s_live = new std::unordered_map<void*, LiveRes>;
static std::vector<std::string>* s_otherNames = new std::vector<std::string>;
static std::unordered_map<uintptr_t, int>* s_retCache = new std::unordered_map<uintptr_t, int>;   // direccion de retorno -> dueno
static uintptr_t s_modB = 0, s_modE = 0, s_exeB = 0, s_exeE = 0;
static std::atomic<bool> s_on{false};
static std::atomic<uint64_t> s_created{0}, s_destroyed{0}, s_noNotify{0};

static void ImageRange(HMODULE m, uintptr_t& b, uintptr_t& e) {
    b = (uintptr_t)m; e = b;
    if (!m) return;
    auto* dos = (IMAGE_DOS_HEADER*)m; auto* nt = (IMAGE_NT_HEADERS*)(b + dos->e_lfanew);
    e = b + nt->OptionalHeader.SizeOfImage;
}
static int OwnerOf(uintptr_t ret, int& otherIdx) {   // sin s_mx tomado (GetModuleHandleEx toma el lock del cargador)
    otherIdx = -1;
    if (ret >= s_modB && ret < s_modE) return kOwnMod;
    if (ret >= s_exeB && ret < s_exeE) return kOwnGame;
    {   std::lock_guard<std::mutex> lk(*s_mx);
        auto it = s_retCache->find(ret);
        if (it != s_retCache->end()) { if (it->second >= 0) { otherIdx = it->second; return kOwnOther; } return kOwnGame; } }
    HMODULE m = nullptr; std::string name;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ret, &m) && m) {
        char n[MAX_PATH] = "?"; GetModuleFileNameA(m, n, MAX_PATH);
        const char* sl = strrchr(n, '\\'); name = sl ? sl + 1 : n;
    }
    std::lock_guard<std::mutex> lk(*s_mx);
    int idx = -1;
    if (!name.empty()) {
        for (size_t i = 0; i < s_otherNames->size(); i++) if ((*s_otherNames)[i] == name) idx = (int)i;
        if (idx < 0) { s_otherNames->push_back(name); idx = (int)s_otherNames->size() - 1; }
    }
    if (s_retCache->size() < 4096) (*s_retCache)[ret] = idx;   // idx -1 = memoria sin modulo (codigo del juego descifrado)
    if (idx >= 0) { otherIdx = idx; return kOwnOther; }
    return kOwnGame;
}
static void WINAPI OnDestroyed(void* p) {
    std::lock_guard<std::mutex> lk(*s_mx);
    if (s_live->erase(p)) s_destroyed.fetch_add(1, std::memory_order_relaxed);
}
static void Track(ID3D11DeviceChild* r, uint64_t bytes, int cat, uintptr_t ret) {
    if (!r || !s_on.load(std::memory_order_relaxed)) return;
    LiveRes lr{}; lr.bytes = bytes; lr.cat = cat;
    lr.owner = OwnerOf(ret, lr.otherIdx);
    if (lr.owner == kOwnMod) {   // pila: quien pidio el recurso (ret) y las siguientes direcciones dentro del mod
        void* fr[16] = {}; USHORT n = RtlCaptureStackBackTrace(0, 16, fr, nullptr); int k = 0;
        lr.frames[k++] = ret;
        USHORT i = 0; while (i < n && (uintptr_t)fr[i] != ret) i++;
        for (i++; i < n && k < 4; i++) { uintptr_t a = (uintptr_t)fr[i]; if (a >= s_modB && a < s_modE) lr.frames[k++] = a; }
    }
    ID3DDestructionNotifier* dn = nullptr;
    if (FAILED(r->QueryInterface(__uuidof(ID3DDestructionNotifier), (void**)&dn)) || !dn) { s_noNotify.fetch_add(1, std::memory_order_relaxed); return; }
    { std::lock_guard<std::mutex> lk(*s_mx); (*s_live)[(void*)r] = lr; }
    UINT id = 0;
    if (FAILED(dn->RegisterDestructionCallback(&OnDestroyed, (void*)r, &id))) {
        std::lock_guard<std::mutex> lk(*s_mx); s_live->erase((void*)r); s_noNotify.fetch_add(1, std::memory_order_relaxed);
    } else s_created.fetch_add(1, std::memory_order_relaxed);
    dn->Release();
}

using PFN_CBuf = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_BUFFER_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Buffer**);
using PFN_CT1 = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE1D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture1D**);
using PFN_CT2 = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
using PFN_CT3 = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE3D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture3D**);
static PFN_CBuf o_CBuf = nullptr; static PFN_CT1 o_CT1 = nullptr; static PFN_CT2 o_CT2 = nullptr; static PFN_CT3 o_CT3 = nullptr;

static HRESULT STDMETHODCALLTYPE hk_CBuf(ID3D11Device* d, const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* init, ID3D11Buffer** out) {
    uintptr_t ret = (uintptr_t)_ReturnAddress();
    HRESULT hr = o_CBuf(d, desc, init, out);
    if (SUCCEEDED(hr) && desc && out && *out) Track(*out, desc->ByteWidth, Cat(false, desc->BindFlags, desc->Usage), ret);
    return hr;
}
static HRESULT STDMETHODCALLTYPE hk_CT1(ID3D11Device* d, const D3D11_TEXTURE1D_DESC* desc, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture1D** out) {
    uintptr_t ret = (uintptr_t)_ReturnAddress();
    HRESULT hr = o_CT1(d, desc, init, out);
    if (SUCCEEDED(hr) && desc && out && *out) Track(*out, TexBytes(desc->Format, desc->Width, 1, 1, desc->MipLevels, desc->ArraySize, 1), Cat(true, desc->BindFlags, desc->Usage), ret);
    return hr;
}
static HRESULT STDMETHODCALLTYPE hk_CT2(ID3D11Device* d, const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture2D** out) {
    uintptr_t ret = (uintptr_t)_ReturnAddress();
    HRESULT hr = o_CT2(d, desc, init, out);
    if (SUCCEEDED(hr) && desc && out && *out) Track(*out, TexBytes(desc->Format, desc->Width, desc->Height, 1, desc->MipLevels, desc->ArraySize, desc->SampleDesc.Count), Cat(true, desc->BindFlags, desc->Usage), ret);
    return hr;
}
static HRESULT STDMETHODCALLTYPE hk_CT3(ID3D11Device* d, const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture3D** out) {
    uintptr_t ret = (uintptr_t)_ReturnAddress();
    HRESULT hr = o_CT3(d, desc, init, out);
    if (SUCCEEDED(hr) && desc && out && *out) Track(*out, TexBytes(desc->Format, desc->Width, desc->Height, desc->Depth, desc->MipLevels, 1, 1), Cat(true, desc->BindFlags, desc->Usage), ret);
    return hr;
}

static bool HookOne(void* target, void* detour, void** orig, const char* name) {
    if (!target) return false;
    MH_STATUS s = MH_CreateHook(target, detour, orig);
    if (s != MH_OK) { Log("medidor: no se pudo enganchar %s (%d)", name, (int)s); return false; }
    if (MH_EnableHook(target) != MH_OK) { Log("medidor: no se pudo prender el gancho %s", name); return false; }
    return true;
}
void GpuMeterHookDevice(ID3D11Device* d) {   // desde HookDevice (una vez, antes de que el juego cree recursos)
    static bool done = false;
    if (done || !d || !g_cfg.meterOn) return; done = true;
    HMODULE me = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&GpuMeterHookDevice, &me);
    ImageRange(me, s_modB, s_modE); ImageRange(GetModuleHandleW(nullptr), s_exeB, s_exeE);
    void** vt = *(void***)d;
    int n = 0;
    n += HookOne(vt[3], (void*)&hk_CBuf, (void**)&o_CBuf, "CreateBuffer");
    n += HookOne(vt[4], (void*)&hk_CT1, (void**)&o_CT1, "CreateTexture1D");
    n += HookOne(vt[5], (void*)&hk_CT2, (void**)&o_CT2, "CreateTexture2D");
    n += HookOne(vt[6], (void*)&hk_CT3, (void**)&o_CT3, "CreateTexture3D");
    s_on.store(n == 4);
    Log("medidor: %d de 4 creadores de recursos enganchados (memoria por dueno %s)", n, n == 4 ? "prendida" : "apagada");
}

// ---------------------------------------------------------------- mapa del enlazador (d3d11.map) para nombrar archivos del mod
struct MapSym { uint32_t rva; uint16_t obj; };
static std::vector<MapSym> s_syms; static std::vector<std::string> s_objs; static int s_mapState = 0;   // 0 sin leer, 1 ok, 2 no esta
static void LoadMap() {
    if (s_mapState) return; s_mapState = 2;
    wchar_t p[MAX_PATH] = {}; HMODULE me = (HMODULE)s_modB; if (!me) return;
    GetModuleFileNameW(me, p, MAX_PATH);
    wchar_t* dot = wcsrchr(p, L'.'); if (!dot) return; wcscpy(dot, L".map");
    FILE* f = _wfopen(p, L"rb"); if (!f) { Log("medidor: no esta d3d11.map junto a la DLL: los archivos del mod salen como direcciones"); return; }
    char line[2048]; unsigned long long pref = 0x180000000ULL; std::unordered_map<std::string, uint16_t> idx;
    while (fgets(line, sizeof line, f)) {
        if (const char* q = strstr(line, "Preferred load address is")) { sscanf(q + 25, "%llx", &pref); continue; }
        unsigned sec = 0, off = 0; char name[1024]; unsigned long long rb = 0;
        if (sscanf(line, " %x:%x %1023s %llx", &sec, &off, name, &rb) != 4 || rb < pref || sec == 0) continue;
        // ultimo campo de la linea = objeto ("gtao.obj", "minhook:hook.obj")
        char* e = line + strlen(line); while (e > line && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
        char* b = strrchr(line, ' '); if (!b) continue; b++;
        std::string obj = b; if (obj.size() < 4 || obj.find(".obj") == std::string::npos) continue;
        auto it = idx.find(obj); uint16_t oi;
        if (it == idx.end()) { oi = (uint16_t)s_objs.size(); s_objs.push_back(obj); idx[obj] = oi; } else oi = it->second;
        s_syms.push_back({ (uint32_t)(rb - pref), oi });
    }
    fclose(f);
    std::sort(s_syms.begin(), s_syms.end(), [](const MapSym& a, const MapSym& b) { return a.rva < b.rva; });
    s_mapState = s_syms.empty() ? 2 : 1;
    Log("medidor: d3d11.map leido: %zu simbolos de %zu archivos", s_syms.size(), s_objs.size());
}
static const std::string* ObjOf(uintptr_t a) {
    if (s_mapState != 1 || a < s_modB) return nullptr;
    uint32_t rva = (uint32_t)(a - s_modB);
    auto it = std::upper_bound(s_syms.begin(), s_syms.end(), rva, [](uint32_t v, const MapSym& s) { return v < s.rva; });
    if (it == s_syms.begin()) return nullptr;
    return &s_objs[(--it)->obj];
}
static bool LibObj(const std::string& o) {   // codigo de bibliotecas: se sigue subiendo por la pila
    return o.find("DDSTextureLoader") != std::string::npos || o.find("WICTextureLoader") != std::string::npos ||
           o.find("ScreenGrab") != std::string::npos || o.find("imgui") != std::string::npos || o.find(':') != std::string::npos ||
           o.find("gpumeter") != std::string::npos;
}
static std::string ModKey(const LiveRes& r, bool& viaLoader) {
    viaLoader = false;
    if (s_mapState == 1) {
        for (int i = 0; i < 4 && r.frames[i]; i++) {
            const std::string* o = ObjOf(r.frames[i]); if (!o) continue;
            if (o->find("DDSTextureLoader") != std::string::npos || o->find("WICTextureLoader") != std::string::npos) viaLoader = true;
            if (LibObj(*o)) continue;
            std::string s = *o; if (s.size() > 4) s.resize(s.size() - 4); return s;   // sin ".obj"
        }
    }
    char b[32]; snprintf(b, sizeof b, "+0x%llx", (unsigned long long)(r.frames[0] ? r.frames[0] - s_modB : 0)); return b;
}

// ---------------------------------------------------------------- consultas de tiempo en la GPU
static const char* const kZoneName[kGpuZoneN] = { "SSSS piel", "GTAO", "SSGI", "medidor de luz", "ropa contra el cuerpo (Present)",
    "desgaste de la cancha (Present)", "pasto (Present)", "caras (Present)", "caras HD (Present)", "aro del Fisico", "copia del cuadro",
    "menu Insert", "vidrio de menus (Present)" };
struct ZoneQ { ID3D11Query* dis = nullptr; ID3D11Query* a = nullptr; ID3D11Query* b = nullptr; std::atomic<int> tag{-1}; int zone = 0; };
static const int kZoneQ = 96;
static ZoneQ s_zq[kZoneQ]; static std::atomic<int> s_zqState{0}; static std::mutex s_zqMx;
static double s_zSum[kGpuZoneN] = {}, s_zMax[kGpuZoneN] = {}; static int s_zLost = 0, s_zDisjoint = 0, s_zBusy = 0;
struct FrameQ { ID3D11Query* dis = nullptr; ID3D11Query* ts = nullptr; int frame = -1; bool open = false; bool read = false; };
static const int kFrameQ = 8; static FrameQ s_fq[kFrameQ];
static double s_fSum = 0, s_fMax = 0; static int s_fN = 0;
static double s_cpuSum = 0, s_cpuMax = 0; static int s_cpuN = 0; static double s_lastPresentT = 0;
static int s_frame = 0;
static std::atomic<bool> s_want{false};   // x375 (menu)

static bool ZonesReady() {
    int st = s_zqState.load(); if (st) return st == 1;
    std::lock_guard<std::mutex> lk(s_zqMx);
    if (s_zqState.load()) return s_zqState.load() == 1;
    bool ok = g_dev.dev != nullptr && g_cfg.meterOn;
    D3D11_QUERY_DESC qd = {};
    for (auto& z : s_zq) { if (!ok) break;
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT; if (FAILED(g_dev.dev->CreateQuery(&qd, &z.dis))) { z.dis = nullptr; ok = false; break; }
        qd.Query = D3D11_QUERY_TIMESTAMP;
        if (FAILED(g_dev.dev->CreateQuery(&qd, &z.a)) || FAILED(g_dev.dev->CreateQuery(&qd, &z.b))) ok = false; }
    for (auto& f : s_fq) { if (!ok) break;
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT; if (FAILED(g_dev.dev->CreateQuery(&qd, &f.dis))) { f.dis = nullptr; ok = false; break; }
        qd.Query = D3D11_QUERY_TIMESTAMP; if (FAILED(g_dev.dev->CreateQuery(&qd, &f.ts))) { f.ts = nullptr; ok = false; } }
    if (!ok && g_dev.dev && g_cfg.meterOn) Log("medidor: no se pudieron crear las consultas de tiempo (sin ms por efecto)");
    s_zqState.store(ok ? 1 : 2); return ok;
}
int GpuZoneBegin(ID3D11DeviceContext* c, int zone) {
    if (!c || zone < 0 || zone >= kGpuZoneN || !s_want.load(std::memory_order_relaxed) || !ZonesReady()) return -1;
    const int f = s_frame;
    for (int i = 0; i < kZoneQ; i++) { int e = -1;
        if (s_zq[i].tag.compare_exchange_strong(e, f)) { s_zq[i].zone = zone; c->Begin(s_zq[i].dis); c->End(s_zq[i].a); return i; } }
    s_zBusy++; return -1;
}
void GpuZoneEnd(ID3D11DeviceContext* c, int i) {
    if (!c || i < 0 || i >= kZoneQ) return;
    c->End(s_zq[i].b); c->End(s_zq[i].dis);
}
static void ReadZones() {   // contexto inmediato
    for (auto& z : s_zq) { int tg = z.tag.load(); if (tg < 0 || tg > s_frame - 3) continue;
        if (s_frame - tg > 120) { z.tag = -1; s_zLost++; continue; }   // nunca se ejecuto (lista diferida descartada)
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d = {}; if (g_dev.imm->GetData(z.dis, &d, sizeof d, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        UINT64 a = 0, b = 0;
        if (g_dev.imm->GetData(z.a, &a, 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK || g_dev.imm->GetData(z.b, &b, 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        const int zone = z.zone; z.tag = -1;
        if (d.Disjoint || !d.Frequency) { s_zDisjoint++; continue; }
        double ms = b >= a ? double(b - a) * 1000.0 / double(d.Frequency) : 0.0;
        s_zSum[zone] += ms; if (ms > s_zMax[zone]) s_zMax[zone] = ms; }
}
static void FrameStamp() {   // al empezar Present: marca de tiempo del cuadro (la diferencia con la anterior = cuadro en la GPU)
    FrameQ& cur = s_fq[s_frame % kFrameQ]; FrameQ& prev = s_fq[(s_frame + kFrameQ - 1) % kFrameQ];
    // leer los cuadros viejos antes de reusar su consulta
    for (int k = 3; k < kFrameQ; k++) {
        FrameQ& a = s_fq[(s_frame + kFrameQ - k - 1) % kFrameQ]; FrameQ& b = s_fq[(s_frame + kFrameQ - k) % kFrameQ];
        if (a.frame < 0 || a.read || b.frame != a.frame + 1 || a.open) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d = {}; UINT64 ta = 0, tb = 0;
        if (g_dev.imm->GetData(a.dis, &d, sizeof d, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        if (g_dev.imm->GetData(a.ts, &ta, 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK || g_dev.imm->GetData(b.ts, &tb, 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        if (!d.Disjoint && d.Frequency && tb >= ta) { double ms = double(tb - ta) * 1000.0 / double(d.Frequency); s_fSum += ms; if (ms > s_fMax) s_fMax = ms; s_fN++; }
        a.read = true;
    }
    if (cur.frame >= 0 && cur.open) { g_dev.imm->End(cur.dis); cur.open = false; }   // (no deberia pasar: 8 cuadros de atraso)
    g_dev.imm->End(cur.ts);
    if (prev.open) { g_dev.imm->End(prev.dis); prev.open = false; }
    g_dev.imm->Begin(cur.dis); cur.open = true; cur.read = false; cur.frame = s_frame;
}

// ---------------------------------------------------------------- VRAM del proceso (DXGI)
static IDXGIAdapter3* Adapter3() {
    static IDXGIAdapter3* a3 = nullptr; static bool tried = false;
    if (tried || !g_dev.dev) return a3; tried = true;
    IDXGIDevice* dd = nullptr; IDXGIAdapter* a = nullptr;
    if (SUCCEEDED(g_dev.dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dd)) && dd) {
        if (SUCCEEDED(dd->GetAdapter(&a)) && a) { a->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3); a->Release(); }
        dd->Release();
    }
    if (!a3) Log("medidor: la placa / Windows no da IDXGIAdapter3 (sin uso de VRAM del proceso)");
    return a3;
}

static double MB(uint64_t b) { return double(b) / (1024.0 * 1024.0); }
static std::string s_summary;
std::string GpuMeterSummary() { return s_summary; }

void GpuMeterPresent() {
    if (!g_cfg.meterOn || !g_dev.imm) return;
    const bool want = g_cfg.live[375][0] > 0.5f;
    static bool wasOn = false;
    if (want != wasOn) { wasOn = want; Log("medidor: %s (x375)", want ? "prendido" : "apagado"); if (!want) s_summary.clear(); }
    s_want.store(want);
    if (!want) { s_lastPresentT = 0; return; }
    const double t = Now();
    if (s_lastPresentT > 0) { double ms = (t - s_lastPresentT) * 1000.0; if (ms < 1000.0) { s_cpuSum += ms; if (ms > s_cpuMax) s_cpuMax = ms; s_cpuN++; } }
    s_lastPresentT = t;
    if (ZonesReady()) { ReadZones(); FrameStamp(); }
    s_frame++;
    static double next = 0; static int frames0 = 0; static uint64_t peakLocal = 0;
    IDXGIAdapter3* a3 = Adapter3();
    DXGI_QUERY_VIDEO_MEMORY_INFO loc = {}, nonloc = {};
    if (a3) { a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &loc); a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonloc);
              if (loc.CurrentUsage > peakLocal) peakLocal = loc.CurrentUsage; }
    if (next == 0 || t > next + 30.0) { next = t + 10.0; frames0 = s_frame; for (int z = 0; z < kGpuZoneN; z++) { s_zSum[z] = 0; s_zMax[z] = 0; } s_fSum = s_fMax = 0; s_fN = 0; s_cpuSum = s_cpuMax = 0; s_cpuN = 0; return; }
    if (t < next) return;
    next = t + 10.0;
    const int nFrames = (std::max)(1, s_frame - frames0); frames0 = s_frame;
    LoadMap();

    // memoria por dueno
    uint64_t own[3][kCatN] = {}; uint64_t cnt[3] = {};
    std::map<std::string, uint64_t> modBy, modCnt, otherBy; uint64_t loaderBytes = 0;
    {
        std::lock_guard<std::mutex> lk(*s_mx);
        for (auto& kv : *s_live) { const LiveRes& r = kv.second;
            own[r.owner][r.cat] += r.bytes; cnt[r.owner]++;
            if (r.owner == kOwnMod && r.cat != kCatStaging) { bool vl = false; std::string k = ModKey(r, vl); modBy[k] += r.bytes; modCnt[k]++; if (vl) loaderBytes += r.bytes; }
            if (r.owner == kOwnOther && r.cat != kCatStaging && r.otherIdx >= 0 && r.otherIdx < (int)s_otherNames->size()) otherBy[(*s_otherNames)[r.otherIdx]] += r.bytes; }
    }
    auto vramOf = [&](int o) { uint64_t s = 0; for (int c = 0; c < kCatN; c++) if (c != kCatStaging) s += own[o][c]; return s; };
    const uint64_t vGame = vramOf(kOwnGame), vOther = vramOf(kOwnOther), vMod = vramOf(kOwnMod);
    char buf[4096]; int n = 0;
    n = snprintf(buf, sizeof buf, "medidor: VRAM del proceso %.0f MB (pico %.0f) de %.0f MB de presupuesto | compartida %.0f MB | estimado por recursos: juego %.0f MB, mod %.0f MB, otras DLL %.0f MB",
        MB(loc.CurrentUsage), MB(peakLocal), MB(loc.Budget), MB(nonloc.CurrentUsage), MB(vGame), MB(vMod), MB(vOther));
    Log("%s", buf);
    for (int o = 0; o < 3; o++) {
        static const char* const ow[3] = { "juego", "otras DLL", "mod" };
        n = snprintf(buf, sizeof buf, "medidor:   %s (%llu recursos):", ow[o], (unsigned long long)cnt[o]);
        for (int c = 0; c < kCatN && n < (int)sizeof buf - 64; c++) if (own[o][c]) n += snprintf(buf + n, sizeof buf - n, " %s %.1f MB |", kCatName[c], MB(own[o][c]));
        Log("%s", buf);
    }
    { std::vector<std::pair<uint64_t, std::string>> v; for (auto& kv : modBy) v.push_back({ kv.second, kv.first });
      std::sort(v.rbegin(), v.rend());
      n = snprintf(buf, sizeof buf, "medidor:   mod por archivo (VRAM):");
      for (size_t i = 0; i < v.size() && i < 16 && n < (int)sizeof buf - 80; i++) n += snprintf(buf + n, sizeof buf - n, " %s %.1f MB (%llu) |", v[i].second.c_str(), MB(v[i].first), (unsigned long long)modCnt[v[i].second]);
      if (loaderBytes) n += snprintf(buf + n, sizeof buf - n, " de eso, cargado de archivos DDS / imagenes: %.1f MB", MB(loaderBytes));
      Log("%s", buf); }
    if (!otherBy.empty()) { n = snprintf(buf, sizeof buf, "medidor:   otras DLL:");
      for (auto& kv : otherBy) if (n < (int)sizeof buf - 80) n += snprintf(buf + n, sizeof buf - n, " %s %.1f MB |", kv.first.c_str(), MB(kv.second));
      Log("%s", buf); }

    // costo en la GPU
    n = snprintf(buf, sizeof buf, "medidor: GPU por cuadro (10 s, %d cuadros): cuadro en la GPU %.2f ms (max %.2f), en la CPU %.2f ms (max %.2f) | efectos del mod:",
        nFrames, s_fN ? s_fSum / s_fN : 0.0, s_fMax, s_cpuN ? s_cpuSum / s_cpuN : 0.0, s_cpuMax);
    double modSum = 0;
    for (int z = 0; z < kGpuZoneN && n < (int)sizeof buf - 80; z++) if (s_zSum[z] > 0) { double avg = s_zSum[z] / nFrames; modSum += avg;
        n += snprintf(buf + n, sizeof buf - n, " %s %.2f (max %.2f) |", kZoneName[z], avg, s_zMax[z]); }
    n += snprintf(buf + n, sizeof buf - n, " suma %.2f ms (sin el pasto ni las sombras propias: ver \"pasto GPU\")", modSum);
    if (s_zLost || s_zDisjoint || s_zBusy) n += snprintf(buf + n, sizeof buf - n, " | consultas perdidas %d, disjuntas %d, sin lugar %d", s_zLost, s_zDisjoint, s_zBusy);
    Log("%s", buf);
    {   // resumen para el menu (se actualiza cada 10 s)
        std::string out; char sb[512];
        snprintf(sb, sizeof sb, "VRAM del juego completo: %.0f MB de %.0f MB disponibles (pico %.0f MB).\nEstimado por recursos: juego %.0f MB, mod %.0f MB, otras DLL %.0f MB.",
            MB(loc.CurrentUsage), MB(loc.Budget), MB(peakLocal), MB(vGame), MB(vMod), MB(vOther)); out += sb;
        std::vector<std::pair<uint64_t, std::string>> v; for (auto& kv : modBy) v.push_back({ kv.second, kv.first }); std::sort(v.rbegin(), v.rend());
        if (!v.empty()) { out += "\nLo que mas ocupa del mod:"; for (size_t i = 0; i < v.size() && i < 5; i++) { snprintf(sb, sizeof sb, " %s %.0f MB%s", v[i].second.c_str(), MB(v[i].first), i + 1 < (std::min)(v.size(), (size_t)5) ? "," : "."); out += sb; } }
        snprintf(sb, sizeof sb, "\nCuadro en la GPU: %.1f ms (%.0f FPS). Efectos del mod medidos: %.1f ms.", s_fN ? s_fSum / s_fN : 0.0, (s_fN && s_fSum > 0) ? 1000.0 * s_fN / s_fSum : 0.0, modSum); out += sb;
        std::vector<std::pair<double, int>> zs; for (int z = 0; z < kGpuZoneN; z++) if (s_zSum[z] > 0) zs.push_back({ s_zSum[z] / nFrames, z }); std::sort(zs.rbegin(), zs.rend());
        if (!zs.empty()) { out += "\nMas caros:"; for (size_t i = 0; i < zs.size() && i < 5; i++) { snprintf(sb, sizeof sb, " %s %.2f ms%s", kZoneName[zs[i].second], zs[i].first, i + 1 < (std::min)(zs.size(), (size_t)5) ? "," : "."); out += sb; } }
        out += "\nEl pasto y las sombras propias se miden aparte (lineas \"pasto GPU\" del log).";
        s_summary = out;
    }
    for (int z = 0; z < kGpuZoneN; z++) { s_zSum[z] = 0; s_zMax[z] = 0; }
    s_fSum = s_fMax = 0; s_fN = 0; s_cpuSum = s_cpuMax = 0; s_cpuN = 0; s_zLost = s_zDisjoint = s_zBusy = 0;
    if (s_noNotify.load()) { Log("medidor:   %llu recursos sin aviso de destruccion (no se cuentan)", (unsigned long long)s_noNotify.load()); }
}

} // namespace fx
