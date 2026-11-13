// PES21 FX (13/11) - RENDIMIENTO DEL CUADRO Y DEL EQUIPO. Agustin: "mejorar la prueba automatica de diagnostico para que te de mas valores
// de rendimiento en un archivo de texto ... valores de cpu, gpu, memoria, ram, almacenamiento, lag, etc" y "te hago dos pruebas una de
// cerca y otra en la camara lejana". Solo mide, no cambia nada de lo que se dibuja:
//  - cada cuadro (Present): intervalo, tiempo del mod en Present, espera dentro del Present original (cola de la placa llena o vsync),
//    CPU del mod dentro de los dibujos y del driver en esos dibujos (contexto inmediato y diferidos por separado), dibujos y listas;
//  - cada segundo (hilo propio): CPU del proceso y del sistema, los hilos mas cargados del juego (con su modulo y nombre), RAM del
//    proceso y del sistema, fallos de pagina, lectura / escritura de disco del proceso, VRAM (DXGI) y uso de los motores 3D / copia de
//    la placa (contadores "GPU Engine" de Windows, si estan);
//  - el equipo: placa y driver, procesador, RAM, Windows, pantalla (tamano, vsync, cola de cuadros, pantalla completa).
// Lo usan la prueba automatica (menutest.cpp) y "Grabar rendimiento jugando" (2 minutos, aca). Con el medidor (x375) prendido escribe
// ademas una linea "rendimiento (10 s)" en el log. Informe: PES21FX\diagnostico\<fecha>\rendimiento.txt (+ .csv por segundo) y una
// linea por medicion en PES21FX\diagnostico\rendimiento_todas.csv para comparar escenas (cerca / lejos).
#include "core.h"
#include <intrin.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi1_4.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <cctype>
#include <thread>
#include <unordered_map>
#include <vector>
#include <string>

namespace fx {

std::atomic<bool> g_perfOn{false};
thread_local uint64_t t_perfCall = 0;
thread_local int t_perfDepth = 0;

namespace {
// ---------------------------------------------------------------- acumuladores del cuadro (cualquier hilo)
struct Acc { std::atomic<uint64_t> modImm{0}, modDef{0}, hookImm{0}, hookDef{0}, drvImm{0}, drvDef{0}, dImm{0}, dDef{0}, lists{0}, execT{0}, fin{0}; };
Acc s_acc;
std::atomic<int> s_users{0};       // prueba automatica / grabacion / medidor
std::atomic<bool> s_meterUser{false};

// TSC -> ms (calibrado contra QueryPerformanceCounter en el hilo de Present)
double s_tscPerMs = 0.0; uint64_t s_tsc0 = 0; LARGE_INTEGER s_qpc0 = {}, s_qpf = {};
inline double Ms(uint64_t ticks) { return s_tscPerMs > 0.0 ? double(ticks) / s_tscPerMs : 0.0; }

struct FrameRec { float frame, wait, modPres, modImm, modDef, hookImm, hookDef, drvImm, drvDef, exec; uint32_t dImm, dDef, lists; double t; };
const int kFrMax = 1 << 15;
FrameRec* s_fr = new FrameRec[kFrMax];
std::atomic<int64_t> s_frN{0};
uint64_t s_lastEntry = 0;
std::atomic<DWORD> s_presentTid{0};

// ---------------------------------------------------------------- muestras del sistema (hilo propio, 1 por segundo)
struct ThreadTop { DWORD tid = 0; float pct = 0; char name[112] = {}; };
struct SysSample {
    double t = 0; float procCpu = 0, sysCpu = 0, cores = 0;   // procCpu: % de toda la CPU; cores = nucleos llenos equivalentes
    float wsMB = 0, privMB = 0, sysRamPct = 0, sysRamUsedMB = 0, sysRamTotMB = 0, pfPerS = 0;
    float rdMBs = 0, rdOps = 0, wrMBs = 0, wrOps = 0;
    float vramMB = 0, vramBudMB = 0, sharedMB = 0;
    float gpu3d = -1, gpuCopy = -1, gpu3dAll = -1;   // -1 = sin contadores
    int nThreads = 0; ThreadTop top[6];
};
const int kSsMax = 4096;
SysSample* s_ss = new SysSample[kSsMax];
std::atomic<int64_t> s_ssN{0};
std::mutex s_ssMx;
bool s_samplerStarted = false;

// PDH cargado a mano (sin enlazar pdh.lib: la DLL es un proxy de d3d11 que el juego carga temprano)
typedef PDH_STATUS(WINAPI* PFN_PdhOpenQueryW)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
typedef PDH_STATUS(WINAPI* PFN_PdhAddEnglishCounterW)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
typedef PDH_STATUS(WINAPI* PFN_PdhCollectQueryData)(PDH_HQUERY);
typedef PDH_STATUS(WINAPI* PFN_PdhGetFormattedCounterArrayW)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
struct Pdh { bool ok = false, tried = false; PFN_PdhCollectQueryData collect = nullptr; PFN_PdhGetFormattedCounterArrayW arr = nullptr; PDH_HQUERY q = nullptr; PDH_HCOUNTER gpu = nullptr; int collects = 0; };
Pdh s_pdh;
void PdhInit() {
    if (s_pdh.tried) return; s_pdh.tried = true;
    HMODULE m = LoadLibraryW(L"pdh.dll"); if (!m) { Log("rendimiento: sin pdh.dll (sin uso de la placa en %%)"); return; }
    auto open = (PFN_PdhOpenQueryW)GetProcAddress(m, "PdhOpenQueryW"); auto add = (PFN_PdhAddEnglishCounterW)GetProcAddress(m, "PdhAddEnglishCounterW");
    s_pdh.collect = (PFN_PdhCollectQueryData)GetProcAddress(m, "PdhCollectQueryData"); s_pdh.arr = (PFN_PdhGetFormattedCounterArrayW)GetProcAddress(m, "PdhGetFormattedCounterArrayW");
    if (!open || !add || !s_pdh.collect || !s_pdh.arr) { Log("rendimiento: pdh.dll sin las funciones esperadas"); return; }
    if (open(nullptr, 0, &s_pdh.q) != ERROR_SUCCESS) { Log("rendimiento: PdhOpenQuery fallo"); return; }
    const PDH_STATUS st = add(s_pdh.q, L"\\GPU Engine(*)\\Utilization Percentage", 0, &s_pdh.gpu);
    if (st != ERROR_SUCCESS) { Log("rendimiento: sin contadores 'GPU Engine' de Windows (0x%08x): sin uso de la placa en %%", (unsigned)st); return; }
    s_pdh.collect(s_pdh.q); s_pdh.ok = true;
}
void PdhRead(SysSample& s) {
    if (!s_pdh.ok) return;
    if (s_pdh.collect(s_pdh.q) != ERROR_SUCCESS) return;
    if (++s_pdh.collects < 2) return;   // es una tasa: hacen falta dos lecturas
    DWORD sz = 0, n = 0;
    PDH_STATUS st = s_pdh.arr(s_pdh.gpu, PDH_FMT_DOUBLE, &sz, &n, nullptr);
    if (st != (PDH_STATUS)PDH_MORE_DATA || !sz) return;
    std::vector<unsigned char> buf(sz);
    auto* it = (PPDH_FMT_COUNTERVALUE_ITEM_W)buf.data();
    if (s_pdh.arr(s_pdh.gpu, PDH_FMT_DOUBLE, &sz, &n, it) != ERROR_SUCCESS) return;
    wchar_t pre[32]; swprintf(pre, 32, L"pid_%u_", (unsigned)GetCurrentProcessId()); const size_t pl = wcslen(pre);
    double g3 = 0, gc = 0, all3 = 0;
    for (DWORD i = 0; i < n; i++) {
        const wchar_t* nm = it[i].szName; if (!nm || it[i].FmtValue.CStatus != ERROR_SUCCESS) continue;
        const double v = it[i].FmtValue.doubleValue;
        const bool mine = wcsncmp(nm, pre, pl) == 0;
        if (wcsstr(nm, L"engtype_3D")) { all3 += v; if (mine) g3 += v; }
        else if (mine && wcsstr(nm, L"engtype_Copy")) gc += v;
    }
    s.gpu3d = (float)(std::min)(g3, 100.0); s.gpuCopy = (float)(std::min)(gc, 100.0); s.gpu3dAll = (float)(std::min)(all3, 100.0);
}

// nombre de un hilo: descripcion (Windows 10) o modulo de su direccion de inicio
typedef HRESULT(WINAPI* PFN_GetThreadDescription)(HANDLE, PWSTR*);
typedef LONG(WINAPI* PFN_NtQIT)(HANDLE, int, PVOID, ULONG, PULONG);
std::unordered_map<DWORD, std::string> s_tname;
std::string ThreadName(DWORD tid, HANDLE h) {
    auto f = s_tname.find(tid); if (f != s_tname.end()) return f->second;
    static PFN_GetThreadDescription gtd = (PFN_GetThreadDescription)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription");
    static PFN_NtQIT nqit = (PFN_NtQIT)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread");
    std::string out;
    if (gtd) { PWSTR d = nullptr; if (SUCCEEDED(gtd(h, &d)) && d) { char b[64]; WideCharToMultiByte(CP_UTF8, 0, d, -1, b, sizeof b, nullptr, nullptr); out = b; LocalFree(d); } }
    if (nqit) {
        void* sa = nullptr;
        if (nqit(h, 9 /* ThreadQuerySetWin32StartAddress */, &sa, sizeof sa, nullptr) == 0 && sa) {
            HMODULE m = nullptr; char b[160];
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)sa, &m) && m) {
                wchar_t p[MAX_PATH] = {}; GetModuleFileNameW(m, p, MAX_PATH); const wchar_t* bn = wcsrchr(p, L'\\'); bn = bn ? bn + 1 : p;
                char mb[96]; WideCharToMultiByte(CP_UTF8, 0, bn, -1, mb, sizeof mb, nullptr, nullptr);
                snprintf(b, sizeof b, "%s+0x%llx", mb, (unsigned long long)((uintptr_t)sa - (uintptr_t)m));
            } else snprintf(b, sizeof b, "0x%llx", (unsigned long long)(uintptr_t)sa);
            out = out.empty() ? std::string(b) : out + " (" + b + ")";
        }
    }
    if (out.empty()) out = "?";
    if (s_tname.size() < 4096) s_tname[tid] = out;
    return out;
}

uint64_t FT(const FILETIME& f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }
IDXGIAdapter3* s_a3 = nullptr; bool s_a3Tried = false;
IDXGIAdapter3* A3() {
    if (s_a3Tried || !g_dev.dev) return s_a3; s_a3Tried = true;
    IDXGIDevice* dd = nullptr; IDXGIAdapter* a = nullptr;
    if (SUCCEEDED(g_dev.dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dd)) && dd) { if (SUCCEEDED(dd->GetAdapter(&a)) && a) { a->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&s_a3); a->Release(); } dd->Release(); }
    return s_a3;
}

void SamplerLoop() {
    const DWORD pid = GetCurrentProcessId();
    SYSTEM_INFO si; GetSystemInfo(&si); const int ncpu = (std::max)(1, (int)si.dwNumberOfProcessors);
    uint64_t pK0 = 0, pU0 = 0, sI0 = 0, sK0 = 0, sU0 = 0; double t0 = 0; IO_COUNTERS io0 = {}; DWORD pf0 = 0; bool have0 = false;
    std::unordered_map<DWORD, uint64_t> th0;
    for (;;) {
        if (s_users.load() <= 0 && !s_meterUser.load()) { Sleep(250); have0 = false; continue; }
        PdhInit();
        const double t = Now();
        SysSample s; s.t = t;
        FILETIME a, b, k, u; uint64_t pK = 0, pU = 0, sI = 0, sK = 0, sU = 0;
        if (GetProcessTimes(GetCurrentProcess(), &a, &b, &k, &u)) { pK = FT(k); pU = FT(u); }
        FILETIME si1, sk1, su1; if (GetSystemTimes(&si1, &sk1, &su1)) { sI = FT(si1); sK = FT(sk1); sU = FT(su1); }
        IO_COUNTERS io = {}; GetProcessIoCounters(GetCurrentProcess(), &io);
        PROCESS_MEMORY_COUNTERS_EX pm = {}; pm.cb = sizeof pm; K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof pm);
        MEMORYSTATUSEX ms = {}; ms.dwLength = sizeof ms; GlobalMemoryStatusEx(&ms);
        s.wsMB = float(pm.WorkingSetSize / 1048576.0); s.privMB = float(pm.PrivateUsage / 1048576.0);
        s.sysRamPct = (float)ms.dwMemoryLoad; s.sysRamTotMB = float(ms.ullTotalPhys / 1048576.0); s.sysRamUsedMB = float((ms.ullTotalPhys - ms.ullAvailPhys) / 1048576.0);
        if (IDXGIAdapter3* a3 = A3()) { DXGI_QUERY_VIDEO_MEMORY_INFO l = {}, nl = {}; a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &l); a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nl);
            s.vramMB = float(l.CurrentUsage / 1048576.0); s.vramBudMB = float(l.Budget / 1048576.0); s.sharedMB = float(nl.CurrentUsage / 1048576.0); }
        PdhRead(s);
        // hilos del proceso
        std::unordered_map<DWORD, uint64_t> th1; std::vector<std::pair<uint64_t, DWORD>> dts;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te = {}; te.dwSize = sizeof te;
            if (Thread32First(snap, &te)) do {
                if (te.th32OwnerProcessID != pid) continue;
                HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                if (!h) h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
                if (!h) continue;
                FILETIME c1, e1, k1, u1;
                if (GetThreadTimes(h, &c1, &e1, &k1, &u1)) { const uint64_t v = FT(k1) + FT(u1); th1[te.th32ThreadID] = v;
                    auto f = th0.find(te.th32ThreadID); if (have0 && f != th0.end() && v >= f->second) dts.push_back({ v - f->second, te.th32ThreadID });
                    if (s_tname.find(te.th32ThreadID) == s_tname.end()) ThreadName(te.th32ThreadID, h); }
                CloseHandle(h);
            } while (Thread32Next(snap, &te));
            CloseHandle(snap);
        }
        s.nThreads = (int)th1.size();
        if (have0) {
            const double dt = (std::max)(1e-3, t - t0), dt100 = dt * 1e7;   // FILETIME en 100 ns
            s.procCpu = float(100.0 * double((pK - pK0) + (pU - pU0)) / (dt100 * ncpu)); s.cores = float(double((pK - pK0) + (pU - pU0)) / dt100);
            const double sTot = double((sK - sK0) + (sU - sU0)), sIdle = double(sI - sI0);   // el kernel del sistema incluye el ocio
            s.sysCpu = sTot > 0 ? float(100.0 * (sTot - sIdle) / sTot) : 0.0f;
            s.rdMBs = float((io.ReadTransferCount - io0.ReadTransferCount) / 1048576.0 / dt); s.rdOps = float((io.ReadOperationCount - io0.ReadOperationCount) / dt);
            s.wrMBs = float((io.WriteTransferCount - io0.WriteTransferCount) / 1048576.0 / dt); s.wrOps = float((io.WriteOperationCount - io0.WriteOperationCount) / dt);
            s.pfPerS = float((pm.PageFaultCount - pf0) / dt);
            std::sort(dts.rbegin(), dts.rend());
            for (int i = 0; i < 6 && i < (int)dts.size(); i++) { s.top[i].tid = dts[i].second; s.top[i].pct = float(100.0 * dts[i].first / dt100);
                const std::string& nm = s_tname[dts[i].second]; snprintf(s.top[i].name, sizeof s.top[i].name, "%s", nm.c_str()); }
            { std::lock_guard<std::mutex> lk(s_ssMx); s_ss[s_ssN.load() % kSsMax] = s; s_ssN.fetch_add(1); }
        }
        pK0 = pK; pU0 = pU; sI0 = sI; sK0 = sK; sU0 = sU; t0 = t; io0 = io; pf0 = pm.PageFaultCount; th0.swap(th1); have0 = true;
        const double el = Now() - t; Sleep((DWORD)(std::max)(50.0, 1000.0 - el * 1000.0));
    }
}
void EnsureSampler() { if (s_samplerStarted) return; s_samplerStarted = true; std::thread(SamplerLoop).detach(); }

// ---------------------------------------------------------------- informacion del equipo y de la pantalla
struct ScreenInfo { UINT w = 0, h = 0, bufs = 0, refN = 0, refD = 0, sync = 0, flags = 0; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; BOOL windowed = TRUE, fullscreen = FALSE; DXGI_SWAP_EFFECT eff = DXGI_SWAP_EFFECT_DISCARD; UINT latency = 0; };
ScreenInfo s_scr; double s_scrT = 0;

std::string CpuBrand() {
    int r[4]; char b[64] = {}; __cpuid(r, 0x80000000); if ((unsigned)r[0] < 0x80000004u) return "?";
    for (int i = 0; i < 3; i++) { __cpuid(r, 0x80000002 + i); memcpy(b + 16 * i, r, 16); }
    std::string s(b); while (!s.empty() && s.front() == ' ') s.erase(s.begin()); return s;
}
int PhysCores() {
    DWORD len = 0; GetLogicalProcessorInformation(nullptr, &len); if (!len) return 0;
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> v(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) + 1);
    if (!GetLogicalProcessorInformation(v.data(), &len)) return 0;
    int n = 0; for (size_t i = 0; i < len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); i++) if (v[i].Relationship == RelationProcessorCore) n++; return n;
}
std::string WinVer() {
    typedef LONG(WINAPI* PFN)(PRTL_OSVERSIONINFOW); auto f = (PFN)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    RTL_OSVERSIONINFOW v = {}; v.dwOSVersionInfoSize = sizeof v; if (!f || f(&v) != 0) return "?";
    char b[64]; snprintf(b, sizeof b, "Windows %lu.%lu build %lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber); return b;
}
const char* FmtName(DXGI_FORMAT f) {
    switch (f) { case DXGI_FORMAT_R8G8B8A8_UNORM: return "RGBA8"; case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "RGBA8 sRGB"; case DXGI_FORMAT_B8G8R8A8_UNORM: return "BGRA8";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "BGRA8 sRGB"; case DXGI_FORMAT_R10G10B10A2_UNORM: return "RGB10A2"; case DXGI_FORMAT_R16G16B16A16_FLOAT: return "RGBA16F"; default: return "otro"; }
}
const char* EffName(DXGI_SWAP_EFFECT e) {
    switch (e) { case DXGI_SWAP_EFFECT_DISCARD: return "discard"; case DXGI_SWAP_EFFECT_SEQUENTIAL: return "sequential"; case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return "flip sequential";
    case DXGI_SWAP_EFFECT_FLIP_DISCARD: return "flip discard"; default: return "?"; }
}
}   // namespace

// ---------------------------------------------------------------- ganchos (hooks.cpp)
void PerfDrawDone(uint64_t t0, uint64_t c0, bool imm, bool draw) {
    const uint64_t tot = __rdtsc() - t0, drv = t_perfCall - c0, mod = tot > drv ? tot - drv : 0;
    if (!draw) { (imm ? s_acc.hookImm : s_acc.hookDef).fetch_add(mod, std::memory_order_relaxed); return; }
    if (imm) { s_acc.modImm.fetch_add(mod, std::memory_order_relaxed); s_acc.drvImm.fetch_add(drv, std::memory_order_relaxed); s_acc.dImm.fetch_add(1, std::memory_order_relaxed); }
    else { s_acc.modDef.fetch_add(mod, std::memory_order_relaxed); s_acc.drvDef.fetch_add(drv, std::memory_order_relaxed); s_acc.dDef.fetch_add(1, std::memory_order_relaxed); }
}
void PerfExecTicks(uint64_t ticks) { s_acc.lists.fetch_add(1, std::memory_order_relaxed); s_acc.execT.fetch_add(ticks, std::memory_order_relaxed); }
void PerfFinishList() { s_acc.fin.fetch_add(1, std::memory_order_relaxed); }

static void RecPresent(double t);
static void LiveLog(double t);
void PerfPresent(IDXGISwapChain* sc, UINT sync, uint64_t tEntry, uint64_t tBefore, uint64_t tAfter) {
    // calibrar el TSC (una vez por segundo, con toda la sesion de base)
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    if (!s_qpf.QuadPart) { QueryPerformanceFrequency(&s_qpf); s_qpc0 = q; s_tsc0 = tAfter; }
    else { const double ms = double(q.QuadPart - s_qpc0.QuadPart) * 1000.0 / double(s_qpf.QuadPart); if (ms > 500.0) s_tscPerMs = double(tAfter - s_tsc0) / ms; }
    s_presentTid.store(GetCurrentThreadId(), std::memory_order_relaxed);
    const bool meter = g_cfg.live[375][0] > 0.5f;
    if (meter != s_meterUser.load()) s_meterUser.store(meter);
    const bool on = s_users.load() > 0 || meter;
    if (on && !g_perfOn.load()) { EnsureSampler(); g_perfOn.store(true); s_lastEntry = 0;
        s_acc.modImm = 0; s_acc.modDef = 0; s_acc.hookImm = 0; s_acc.hookDef = 0; s_acc.drvImm = 0; s_acc.drvDef = 0; s_acc.dImm = 0; s_acc.dDef = 0; s_acc.lists = 0; s_acc.execT = 0; s_acc.fin = 0; }
    if (!on) { if (g_perfOn.load()) g_perfOn.store(false); return; }
    const double t = Now();
    if (sc && t - s_scrT > 2.0) { s_scrT = t; DXGI_SWAP_CHAIN_DESC d; if (SUCCEEDED(sc->GetDesc(&d))) {
        s_scr.w = d.BufferDesc.Width; s_scr.h = d.BufferDesc.Height; s_scr.bufs = d.BufferCount; s_scr.refN = d.BufferDesc.RefreshRate.Numerator; s_scr.refD = d.BufferDesc.RefreshRate.Denominator;
        s_scr.fmt = d.BufferDesc.Format; s_scr.windowed = d.Windowed; s_scr.eff = d.SwapEffect; s_scr.flags = d.Flags; }
        BOOL fs = FALSE; sc->GetFullscreenState(&fs, nullptr); s_scr.fullscreen = fs;
        if (g_dev.dev) { IDXGIDevice1* d1 = nullptr; if (SUCCEEDED(g_dev.dev->QueryInterface(__uuidof(IDXGIDevice1), (void**)&d1)) && d1) { d1->GetMaximumFrameLatency(&s_scr.latency); d1->Release(); } } }
    s_scr.sync = sync;
    if (s_lastEntry && s_tscPerMs > 0.0) {
        FrameRec r; r.t = t;
        r.frame = (float)Ms(tEntry - s_lastEntry); r.modPres = (float)Ms(tBefore - tEntry); r.wait = (float)Ms(tAfter - tBefore);
        r.modImm = (float)Ms(s_acc.modImm.exchange(0)); r.modDef = (float)Ms(s_acc.modDef.exchange(0)); r.hookImm = (float)Ms(s_acc.hookImm.exchange(0)); r.hookDef = (float)Ms(s_acc.hookDef.exchange(0)); r.drvImm = (float)Ms(s_acc.drvImm.exchange(0)); r.drvDef = (float)Ms(s_acc.drvDef.exchange(0));
        r.exec = (float)Ms(s_acc.execT.exchange(0)); r.dImm = (uint32_t)s_acc.dImm.exchange(0); r.dDef = (uint32_t)s_acc.dDef.exchange(0); r.lists = (uint32_t)s_acc.lists.exchange(0); s_acc.fin.exchange(0);
        if (r.frame > 0.0f && r.frame < 5000.0f) { s_fr[s_frN.load() % kFrMax] = r; s_frN.fetch_add(1); }
    } else { s_acc.modImm = 0; s_acc.modDef = 0; s_acc.hookImm = 0; s_acc.hookDef = 0; s_acc.drvImm = 0; s_acc.drvDef = 0; s_acc.dImm = 0; s_acc.dDef = 0; s_acc.lists = 0; s_acc.execT = 0; }
    s_lastEntry = tEntry;
    RecPresent(t);
    if (meter) LiveLog(t);
}

// ---------------------------------------------------------------- ventanas de medicion y estadisticas
void PerfUse(bool on) { if (on) s_users.fetch_add(1); else s_users.fetch_sub(1); }
PerfWin PerfBegin() { PerfWin w; w.f0 = s_frN.load(); w.t0 = Now(); return w; }
void PerfEnd(PerfWin& w) { w.f1 = s_frN.load(); w.t1 = Now(); }

PerfStats PerfCompute(const PerfWin& w) {
    PerfStats s;
    const int64_t end = w.f1 > 0 ? w.f1 : s_frN.load(), beg = (std::max)(w.f0, end - (int64_t)kFrMax + 16);
    std::vector<float> ft; ft.reserve((size_t)(std::max<int64_t>)(0, end - beg));
    double sw = 0, smp = 0, smi = 0, smd = 0, shi = 0, shd = 0, sdi = 0, sdd = 0, sx = 0, sdI = 0, sdD = 0, sl = 0, sf = 0;
    for (int64_t i = beg; i < end; i++) { const FrameRec& r = s_fr[i % kFrMax]; ft.push_back(r.frame); sf += r.frame;
        sw += r.wait; smp += r.modPres; smi += r.modImm; smd += r.modDef; shi += r.hookImm; shd += r.hookDef; sdi += r.drvImm; sdd += r.drvDef; sx += r.exec; sdI += r.dImm; sdD += r.dDef; sl += r.lists; }
    s.frames = (int)ft.size(); if (!s.frames) return s;
    const double n = s.frames;
    s.frameMs = float(sf / n); s.fps = s.frameMs > 0 ? 1000.0f / s.frameMs : 0;
    s.waitMs = float(sw / n); s.modPresMs = float(smp / n); s.modImmMs = float(smi / n); s.modDefMs = float(smd / n); s.hookImmMs = float(shi / n); s.hookDefMs = float(shd / n); s.drvImmMs = float(sdi / n); s.drvDefMs = float(sdd / n);
    s.execMs = float(sx / n); s.drawsImm = float(sdI / n); s.drawsDef = float(sdD / n); s.lists = float(sl / n);
    s.gameRtMs = (std::max)(0.0f, s.frameMs - s.waitMs - s.modPresMs - s.modImmMs - s.hookImmMs - s.drvImmMs - s.execMs);
    std::vector<float> so = ft; std::sort(so.begin(), so.end());
    auto pct = [&](double p) { size_t k = (size_t)std::clamp(p * (so.size() - 1), 0.0, double(so.size() - 1)); return so[k]; };
    s.p50 = pct(0.50); s.p90 = pct(0.90); s.p95 = pct(0.95); s.p99 = pct(0.99); s.maxMs = so.back();
    { size_t k = (std::max)((size_t)1, so.size() / 100); double a = 0; for (size_t i = so.size() - k; i < so.size(); i++) a += so[i]; s.low1Fps = float(1000.0 / (a / k)); }
    for (float f : ft) { if (f > 2.0f * s.p50) s.stutters++; if (f > 50.0f) s.over50++; if (f > 100.0f) s.over100++; }
    { double v = 0; for (size_t i = 1; i < ft.size(); i++) v += fabs(ft[i] - ft[i - 1]); s.jitterMs = ft.size() > 1 ? float(v / (ft.size() - 1)) : 0; }
    // muestras del sistema dentro de la ventana
    std::lock_guard<std::mutex> lk(s_ssMx);
    const int64_t sn = s_ssN.load(); int k = 0; double ac[16] = {}; int g3n = 0;
    std::unordered_map<DWORD, std::pair<double, int>> thAcc; std::unordered_map<DWORD, std::string> thNm;
    for (int64_t i = (std::max<int64_t>)(0, sn - kSsMax); i < sn; i++) { const SysSample& x = s_ss[i % kSsMax];
        if (x.t < w.t0 + 0.5 || x.t > (w.t1 > 0 ? w.t1 : Now()) + 0.5) continue;
        ac[0] += x.procCpu; ac[1] += x.sysCpu; ac[2] += x.cores; ac[3] += x.wsMB; ac[4] += x.privMB; ac[5] += x.sysRamPct; ac[6] += x.sysRamUsedMB; ac[7] += x.pfPerS;
        ac[8] += x.rdMBs; ac[9] += x.rdOps; ac[10] += x.wrMBs; ac[11] += x.vramMB; ac[12] += x.vramBudMB; ac[13] += x.sharedMB;
        if (x.gpu3d >= 0) { ac[14] += x.gpu3d; ac[15] += x.gpuCopy; s.gpu3dAll += x.gpu3dAll; g3n++; }
        s.sysRamTotMB = x.sysRamTotMB; s.nThreads = x.nThreads; if (x.rdMBs > s.rdPeakMBs) s.rdPeakMBs = x.rdMBs; if (x.vramMB > s.vramPeakMB) s.vramPeakMB = x.vramMB;
        for (const ThreadTop& tt : x.top) if (tt.tid) { auto& e = thAcc[tt.tid]; e.first += tt.pct; e.second++; thNm[tt.tid] = tt.name; }
        k++; }
    s.samples = k;
    if (k) { s.procCpu = float(ac[0] / k); s.sysCpu = float(ac[1] / k); s.cores = float(ac[2] / k); s.wsMB = float(ac[3] / k); s.privMB = float(ac[4] / k); s.sysRamPct = float(ac[5] / k);
        s.sysRamUsedMB = float(ac[6] / k); s.pfPerS = float(ac[7] / k); s.rdMBs = float(ac[8] / k); s.rdOps = float(ac[9] / k); s.wrMBs = float(ac[10] / k);
        s.vramMB = float(ac[11] / k); s.vramBudMB = float(ac[12] / k); s.sharedMB = float(ac[13] / k); }
    if (g3n) { s.gpu3d = float(ac[14] / g3n); s.gpuCopy = float(ac[15] / g3n); s.gpu3dAll /= g3n; } else { s.gpu3d = s.gpuCopy = s.gpu3dAll = -1; }
    std::vector<std::pair<double, DWORD>> tv; for (auto& kv : thAcc) tv.push_back({ kv.second.first / (std::max)(1, k), kv.first }); std::sort(tv.rbegin(), tv.rend());
    const DWORD rt = s_presentTid.load();
    for (int i = 0; i < 5 && i < (int)tv.size(); i++) { s.top[i].tid = tv[i].second; s.top[i].pct = (float)tv[i].first;
        snprintf(s.top[i].name, sizeof s.top[i].name, "%s%s", thNm[tv[i].second].c_str(), tv[i].second == rt ? " = el que dibuja" : ""); }
    s.renderTid = rt;
    return s;
}

const char* PerfVerdict(const PerfStats& s, char* b, size_t n) {
    const float wf = s.frameMs > 0 ? s.waitMs / s.frameMs : 0;
    const float hz = s_scr.refD ? float(s_scr.refN) / float(s_scr.refD) : 0.0f;
    if (s.gpu3d >= 85.0f || wf >= 0.30f)
        snprintf(b, n, "LIMITA LA PLACA DE VIDEO (motor 3D al %.0f %%, el juego espera %.1f ms por cuadro a que la placa termine): bajar costo de GPU sube los FPS",
            s.gpu3d, s.waitMs);
    else if (s.top[0].pct >= 85.0f)
        snprintf(b, n, "LIMITA EL PROCESADOR: un hilo va lleno (%s al %.0f %% de un nucleo; la placa al %.0f %%): bajar costo de GPU NO sube los FPS",
            s.top[0].name, s.top[0].pct, s.gpu3d);
    else if (s_scr.sync > 0 && hz > 0 && s.fps > 0 && fabsf(s.fps * s_scr.sync - hz) < 0.08f * hz)
        snprintf(b, n, "TOPE DEL VSYNC (%.0f Hz / %u): los FPS estan clavados al refresco", hz, s_scr.sync);
    else
        snprintf(b, n, "MIXTO: ni la placa (%.0f %%) ni un hilo (%.0f %%) van llenos; puede ser un tope de FPS del juego, esperas entre hilos o el disco (%.1f MB/s)",
            s.gpu3d, s.top[0].pct, s.rdMBs);
    return b;
}

std::string PerfHardwareText() {
    std::string o; char b[512];
    if (g_dev.dev) { IDXGIDevice* dd = nullptr; IDXGIAdapter* a = nullptr;
        if (SUCCEEDED(g_dev.dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dd)) && dd) {
            if (SUCCEEDED(dd->GetAdapter(&a)) && a) { DXGI_ADAPTER_DESC d; if (SUCCEEDED(a->GetDesc(&d))) { char nm[128]; WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, nm, sizeof nm, nullptr, nullptr);
                LARGE_INTEGER umd = {}; const bool hv = SUCCEEDED(a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd));
                snprintf(b, sizeof b, "placa: %s, %.0f MB propios, %.0f MB compartidos, driver %u.%u.%u.%u\r\n", nm, d.DedicatedVideoMemory / 1048576.0, d.SharedSystemMemory / 1048576.0,
                    hv ? (unsigned)HIWORD(umd.HighPart) : 0u, hv ? (unsigned)LOWORD(umd.HighPart) : 0u, hv ? (unsigned)HIWORD(umd.LowPart) : 0u, hv ? (unsigned)LOWORD(umd.LowPart) : 0u); o += b; }
                a->Release(); }
            dd->Release(); } }
    SYSTEM_INFO si; GetSystemInfo(&si); MEMORYSTATUSEX ms = {}; ms.dwLength = sizeof ms; GlobalMemoryStatusEx(&ms);
    snprintf(b, sizeof b, "procesador: %s, %d nucleos, %lu hilos | RAM %.1f GB | %s\r\n", CpuBrand().c_str(), PhysCores(), si.dwNumberOfProcessors, ms.ullTotalPhys / 1073741824.0, WinVer().c_str()); o += b;
    const float hz = s_scr.refD ? float(s_scr.refN) / float(s_scr.refD) : 0.0f;
    snprintf(b, sizeof b, "pantalla: %ux%u %s, %s, %u buffers (%s), refresco %.0f Hz, vsync %u%s, cola de cuadros max %u\r\n", s_scr.w, s_scr.h, FmtName(s_scr.fmt),
        s_scr.fullscreen ? "pantalla completa" : (s_scr.windowed ? "ventana" : "completa"), s_scr.bufs, EffName(s_scr.eff), hz, s_scr.sync,
        s_scr.sync ? " (espera al refresco)" : " (sin espera)", s_scr.latency); o += b;
    return o;
}

std::string PerfStatsText(const PerfStats& s, const char* title) {
    std::string o; char b[900];
    snprintf(b, sizeof b, "== %s: %d cuadros en %.1f s ==\r\n", title, s.frames, s.frames ? s.frames * s.frameMs / 1000.0 : 0.0); o += b;
    if (!s.frames) return o + "(sin cuadros)\r\n";
    snprintf(b, sizeof b, "FPS %.1f (cuadro %.1f ms) | mediana %.1f, 90 %% %.1f, 95 %% %.1f, 99 %% %.1f, max %.1f ms | 1 %% mas lento: %.1f FPS\r\n",
        s.fps, s.frameMs, s.p50, s.p90, s.p95, s.p99, s.maxMs, s.low1Fps); o += b;
    snprintf(b, sizeof b, "LAG / TIRONES: %d cuadros de mas del doble de la mediana (%.1f por minuto), %d de mas de 50 ms, %d de mas de 100 ms; variacion de un cuadro al otro %.1f ms\r\n",
        s.stutters, s.frames ? s.stutters * 60000.0 / (s.frames * s.frameMs) : 0.0, s.over50, s.over100, s.jitterMs); o += b;
    snprintf(b, sizeof b, "DONDE SE VA EL CUADRO (ms por cuadro, en el procesador):\r\n"
        "  hilo que dibuja: juego %.2f | mod en sus dibujos %.2f | mod en otros ganchos %.2f | driver en esos dibujos %.2f | listas ejecutadas %.2f | mod en Present %.2f | esperando en Present %.2f\r\n"
        "  otros hilos (listas diferidas): mod en sus dibujos %.2f | mod en otros ganchos %.2f | driver %.2f\r\n"
        "  dibujos por cuadro: %.0f en el inmediato, %.0f en listas diferidas, %.1f listas\r\n",
        s.gameRtMs, s.modImmMs, s.hookImmMs, s.drvImmMs, s.execMs, s.modPresMs, s.waitMs, s.modDefMs, s.hookDefMs, s.drvDefMs, s.drawsImm, s.drawsDef, s.lists); o += b;
    if (s.samples) {
        snprintf(b, sizeof b, "PLACA: motor 3D %s, copia %s (todos los programas: 3D %s) | VRAM %.0f MB de %.0f (pico %.0f) | compartida %.0f MB\r\n",
            s.gpu3d >= 0 ? (std::to_string((int)lroundf(s.gpu3d)) + " %").c_str() : "sin dato", s.gpuCopy >= 0 ? (std::to_string((int)lroundf(s.gpuCopy)) + " %").c_str() : "sin dato",
            s.gpu3dAll >= 0 ? (std::to_string((int)lroundf(s.gpu3dAll)) + " %").c_str() : "sin dato", s.vramMB, s.vramBudMB, s.vramPeakMB, s.sharedMB); o += b;
        snprintf(b, sizeof b, "PROCESADOR: el juego usa %.0f %% de toda la CPU (= %.1f nucleos llenos), el sistema %.0f %%; %d hilos del juego\r\n", s.procCpu, s.cores, s.sysCpu, s.nThreads); o += b;
        o += "  hilos mas cargados (% de un nucleo):";
        for (int i = 0; i < 5; i++) if (s.top[i].tid) { snprintf(b, sizeof b, " [%u] %.0f %% %s |", (unsigned)s.top[i].tid, s.top[i].pct, s.top[i].name); o += b; }
        o += "\r\n";
        snprintf(b, sizeof b, "MEMORIA: el juego %.0f MB en RAM (privada %.0f MB), el sistema %.0f de %.0f MB (%.0f %%), fallos de pagina %.0f por segundo\r\n",
            s.wsMB, s.privMB, s.sysRamUsedMB, s.sysRamTotMB, s.sysRamPct, s.pfPerS); o += b;
        snprintf(b, sizeof b, "DISCO (el juego): lectura %.2f MB/s (pico %.1f), %.0f lecturas/s, escritura %.2f MB/s\r\n", s.rdMBs, s.rdPeakMBs, s.rdOps, s.wrMBs); o += b;
    } else o += "(sin muestras del sistema: la medicion duro menos de 1 s)\r\n";
    char v[400]; o += std::string("VEREDICTO: ") + PerfVerdict(s, v, sizeof v) + "\r\n";
    return o;
}

std::string PerfCsvHeader() {
    return "fecha;escena;medicion;fps;cuadro_ms;p99_ms;max_ms;fps_1pc;tirones;mas_50ms;espera_placa_ms;mod_present_ms;mod_dibujos_ms;driver_ms;juego_hilo_ms;"
           "dibujos_inm;dibujos_dif;placa_3d_pct;vram_mb;cpu_juego_pct;nucleos;hilo_max_pct;hilo_max;ram_mb;ram_sistema_pct;disco_mb_s;fallos_pag_s\r\n";
}
std::string PerfCsvRow(const char* date, const char* scene, const char* what, const PerfStats& s) {
    char b[900]; std::string nm = s.top[0].name; for (char& ch : nm) if (ch == ';') ch = ',';
    snprintf(b, sizeof b, "%s;%s;%s;%.2f;%.2f;%.2f;%.2f;%.2f;%d;%d;%.2f;%.2f;%.2f;%.2f;%.2f;%.0f;%.0f;%.1f;%.0f;%.1f;%.2f;%.1f;%s;%.0f;%.0f;%.2f;%.0f\r\n",
        date, scene, what, s.fps, s.frameMs, s.p99, s.maxMs, s.low1Fps, s.stutters, s.over50, s.waitMs, s.modPresMs, s.modImmMs + s.modDefMs + s.hookImmMs + s.hookDefMs, s.drvImmMs + s.drvDefMs, s.gameRtMs,
        s.drawsImm, s.drawsDef, s.gpu3d, s.vramMB, s.procCpu, s.cores, s.top[0].pct, nm.c_str(), s.wsMB, s.sysRamPct, s.rdMBs, s.pfPerS);
    for (char* p = b; *p; p++) if (*p == '.' && p > b && isdigit((unsigned char)p[-1]) && isdigit((unsigned char)p[1])) *p = ',';   // Excel en castellano
    return b;
}
void PerfAppendAll(const std::string& rows) {
    const std::wstring root = g_cfg.baseDir + L"diagnostico\\"; CreateDirectoryW(root.c_str(), nullptr);
    const std::wstring p = root + L"rendimiento_todas.csv";
    const bool isNew = GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES;
    if (FILE* f = _wfopen(p.c_str(), L"ab")) { if (isNew) { const std::string h = PerfCsvHeader(); fwrite(h.data(), 1, h.size(), f); } fwrite(rows.data(), 1, rows.size(), f); fclose(f); }
}

// ---------------------------------------------------------------- linea del log cada 10 s (con el medidor)
static void LiveLog(double t) {
    static double next = 0; static PerfWin w;
    if (next == 0) { next = t + 10.0; w = PerfBegin(); return; }
    if (t < next) return;
    next = t + 10.0; PerfEnd(w); const PerfStats s = PerfCompute(w); w = PerfBegin();
    if (!s.frames) return;
    char v[400];
    Log("rendimiento (10 s): FPS %.1f, cuadro %.1f ms (99 %% %.1f, max %.1f), tirones %d | hilo que dibuja: juego %.1f, mod en dibujos %.1f (+ otros ganchos %.1f), driver %.1f, listas %.1f, "
        "mod en Present %.1f, esperando a la placa %.1f | otros hilos: mod %.1f, driver %.1f | dibujos %.0f + %.0f | placa 3D %.0f %%, VRAM %.0f MB | CPU del juego %.0f %% (hilo mas cargado %.0f %%: %s) | "
        "RAM %.0f MB, disco %.1f MB/s | %s",
        s.fps, s.frameMs, s.p99, s.maxMs, s.stutters, s.gameRtMs, s.modImmMs, s.hookImmMs, s.drvImmMs, s.execMs, s.modPresMs, s.waitMs, s.modDefMs + s.hookDefMs, s.drvDefMs, s.drawsImm, s.drawsDef,
        s.gpu3d, s.vramMB, s.procCpu, s.top[0].pct, s.top[0].name, s.wsMB, s.rdMBs, PerfVerdict(s, v, sizeof v));
}

// ---------------------------------------------------------------- "Grabar rendimiento jugando" (2 minutos)
namespace {
struct Rec { bool on = false; double end = 0, nextSec = 0; PerfWin all, sec; std::string scene, csv; std::wstring dir; char date[32] = {}; int secs = 0; std::string status; };
Rec s_rec;
const char* const kScenes[] = { "cerca (primer plano)", "camara del partido (lejos)", "repeticion", "editor", "otra" };
}
const char* PerfSceneName(int i) { return kScenes[std::clamp(i, 0, 4)]; }
int PerfSceneCount() { return 5; }
bool PerfRecordActive() { return s_rec.on; }
const std::string& PerfRecordStatus() { return s_rec.status; }
void PerfRecordStart(int scene, double seconds) {
    if (s_rec.on) return;
    s_rec = Rec(); s_rec.on = true; s_rec.scene = PerfSceneName(scene); PerfUse(true);
    SYSTEMTIME st; GetLocalTime(&st); snprintf(s_rec.date, sizeof s_rec.date, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    wchar_t n[48]; swprintf(n, 48, L"rendimiento_%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::wstring root = g_cfg.baseDir + L"diagnostico\\"; CreateDirectoryW(root.c_str(), nullptr); s_rec.dir = root + n;
    const double t = Now(); s_rec.end = t + seconds; s_rec.nextSec = t + 1.0; s_rec.all = PerfBegin(); s_rec.sec = s_rec.all;
    s_rec.csv = "segundo;" + PerfCsvHeader().substr(PerfCsvHeader().find("fps;"));
    s_rec.status = "Grabando el rendimiento: juga normal (camara del partido, repeticiones, primeros planos). Esc o el boton lo cortan.";
    Log("rendimiento: grabacion de %.0f s, escena %s", seconds, s_rec.scene.c_str());
}
static void RecFinish(bool cut) {
    if (!s_rec.on) return;
    PerfEnd(s_rec.all); const PerfStats s = PerfCompute(s_rec.all);
    std::string txt = "RENDIMIENTO JUGANDO" + std::string(cut ? " (CORTADA)" : "") + " - " + s_rec.date + " - escena: " + s_rec.scene + "\r\n" + PerfHardwareText() + "\r\n" + PerfStatsText(s, "todo el tiempo")
        + "\r\nPor segundo: el .csv del mismo nombre (se abre con Excel).\r\n";
    if (FILE* f = _wfopen((s_rec.dir + L".txt").c_str(), L"wb")) { fwrite(txt.data(), 1, txt.size(), f); fclose(f); }
    if (FILE* f = _wfopen((s_rec.dir + L".csv").c_str(), L"wb")) { fwrite(s_rec.csv.data(), 1, s_rec.csv.size(), f); fclose(f); }
    PerfAppendAll(PerfCsvRow(s_rec.date, s_rec.scene.c_str(), cut ? "jugando (cortada)" : "jugando", s));
    char b[300]; std::string fn; for (wchar_t c : s_rec.dir.substr(g_cfg.baseDir.size())) fn += c < 128 ? (char)c : '_';
    snprintf(b, sizeof b, "Listo: %d s grabados, FPS %.1f. Archivo en PES21FX\\%s.txt (y .csv)", s_rec.secs, s.fps, fn.c_str());
    s_rec.status = b; Log("rendimiento: %s", b);
    s_rec.on = false; PerfUse(false);
}
void PerfRecordStop() { RecFinish(true); }
static void RecPresent(double t) {
    if (!s_rec.on) return;
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { RecFinish(true); return; }
    if (t >= s_rec.nextSec) {
        PerfEnd(s_rec.sec);
        const PerfStats s = PerfCompute(s_rec.sec);
        s_rec.secs++;
        if (s.frames) { std::string row = PerfCsvRow("", "", "", s); row = row.substr(3); s_rec.csv += std::to_string(s_rec.secs) + ";" + row; }
        s_rec.sec = PerfBegin(); s_rec.nextSec = t + 1.0;
        char b[200]; snprintf(b, sizeof b, "Grabando el rendimiento: %d de %.0f s (FPS %.1f). Juga normal; Esc corta.", s_rec.secs, s_rec.end - s_rec.all.t0, s.fps); s_rec.status = b;
    }
    if (t >= s_rec.end) RecFinish(false);
}

} // namespace fx
