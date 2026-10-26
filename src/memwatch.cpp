// PES21 FX - vigilancia de la memoria del juego (26/10). Los cierres en PES2021.exe+0x3fe31d (25/10 a la manana y 25/10
// 21:41, modo Editar) son del administrador de memoria del juego (dlmalloc, release_unused_segments): para devolverle a
// Windows un segmento libre llama a su win32munmap (PES2021.exe+0x3fc0d0), que recorre el segmento con VirtualQuery y
// libera con VirtualFree(MEM_RELEASE) cada reserva entera. Si a mitad de camino una region no es "una reserva entera y
// comprometida" (alguien cambio la proteccion de unas paginas, las descomprometio, ...), devuelve -1 DESPUES de haber
// liberado lo anterior, y dlmalloc vuelve a anotar el bloque como libre escribiendo en la memoria que ya no existe -> crash.
// Aca:
//  1) se engancha ese win32munmap: antes de liberar nada se recorre el segmento y, si lo de Windows no es lo que el juego
//     espera, se anota en el log el mapa de regiones y quien toco esas paginas, y no se libera nada: el juego sigue con el
//     bloque como libre y lo vuelve a usar, sin cierre (26/10 12:50: retener es lo de siempre; antes se liberaba cada reserva
//     entera, [PES21FX] memoria_arreglo = 2, y tras un cierre de otro tipo se prefiere no soltar nada que el juego no solto).
//  2) se enganchan VirtualProtect y VirtualFree (KernelBase) y se guarda en un anillo quien los llama (modulo + offset,
//     hilo, direccion, tamano, proteccion): asi se ve si Sider / un modulo Lua / otra DLL toco la memoria del juego.
//     Cada 30 s queda un resumen por modulo. [PES21FX] memoria_vigilar = 0 lo apaga.
//  3) en un cierre: el mapa de memoria alrededor de la direccion que fallo, las ultimas llamadas que tocaron esa zona, el
//     estado de dlmalloc si el cierre es el de 0x3fe31d y las ultimas lineas de sider.log (que estaba haciendo Sider).
#include "core.h"
#include <MinHook.h>
#include <intrin.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#pragma intrinsic(_ReturnAddress)

namespace fx {

static const unsigned kStamp = 0x62787C6A;
static const uintptr_t kMunmap = 0x1403fc0d0;    // win32munmap de dlmalloc dentro del exe
static const uintptr_t kCrashRip = 0x1403fe31d;  // release_unused_segments: mov [rbx+38h], ecx (insert_large_chunk)
// prologo: mov [rsp+8],rbx ; push rdi ; sub rsp,50h ; mov rdi,rdx ; mov rbx,rcx
static const unsigned char kMunmapSig[16] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x50,0x48,0x8b,0xfa,0x48,0x8b,0xd9 };

// ---------------------------------------------------------------- anillo de llamadas a VirtualProtect / VirtualFree
struct MwEv { double t; uintptr_t addr; size_t size; uint32_t a, b; void* caller; uint32_t tid; uint8_t kind; uint8_t ok; };
enum { EV_PROTECT = 1, EV_FREE = 2, EV_MUNMAP = 3 };
static const unsigned kRing = 8192;
static MwEv s_ring[kRing];
static std::atomic<unsigned> s_ringN{0};
static thread_local int t_in = 0;               // no anotar lo que hacemos nosotros (MinHook usa VirtualProtect)
static std::atomic<bool> s_watch{false};

static void Push(uint8_t kind, uintptr_t addr, size_t size, uint32_t a, uint32_t b, void* caller, bool ok) {
    const unsigned i = s_ringN.fetch_add(1) % kRing;
    MwEv& e = s_ring[i];
    e.kind = 0;                                   // (si un cierre lee a mitad de escritura, la entrada se saltea)
    e.t = Now(); e.addr = addr; e.size = size; e.a = a; e.b = b; e.caller = caller; e.tid = GetCurrentThreadId(); e.ok = ok ? 1 : 0;
    e.kind = kind;
}

typedef BOOL (WINAPI *PFN_VP)(LPVOID, SIZE_T, DWORD, PDWORD);
typedef BOOL (WINAPI *PFN_VF)(LPVOID, SIZE_T, DWORD);
static PFN_VP o_vp = nullptr;
static PFN_VF o_vf = nullptr;

static BOOL WINAPI hk_VirtualProtect(LPVOID p, SIZE_T n, DWORD prot, PDWORD old) {
    void* ra = _ReturnAddress();
    if (t_in || !s_watch.load(std::memory_order_relaxed)) return o_vp(p, n, prot, old);
    DWORD o = 0; PDWORD po = old ? old : &o;
    BOOL r = o_vp(p, n, prot, po);
    Push(EV_PROTECT, (uintptr_t)p, n, prot, r ? *po : 0, ra, r != 0);
    return r;
}
static BOOL WINAPI hk_VirtualFree(LPVOID p, SIZE_T n, DWORD type) {
    void* ra = _ReturnAddress();
    BOOL r = o_vf(p, n, type);
    if (!t_in && s_watch.load(std::memory_order_relaxed)) Push(EV_FREE, (uintptr_t)p, n, type, 0, ra, r != 0);
    return r;
}

// ---------------------------------------------------------------- textos
static const char* ProtName(DWORD p) {
    switch (p & 0xFF) {
    case PAGE_NOACCESS: return "sin acceso"; case PAGE_READONLY: return "R"; case PAGE_READWRITE: return "RW";
    case PAGE_WRITECOPY: return "WC"; case PAGE_EXECUTE: return "X"; case PAGE_EXECUTE_READ: return "RX";
    case PAGE_EXECUTE_READWRITE: return "RWX"; case PAGE_EXECUTE_WRITECOPY: return "XWC"; default: return "?";
    }
}
static const char* StateName(DWORD s) { return s == MEM_COMMIT ? "comprometida" : s == MEM_RESERVE ? "reservada" : s == MEM_FREE ? "LIBRE" : "?"; }
static const char* TypeName(DWORD t) { return t == MEM_PRIVATE ? "privada" : t == MEM_IMAGE ? "imagen" : t == MEM_MAPPED ? "mapeada" : "-"; }
static void FreeType(char* out, size_t cap, DWORD t) {
    snprintf(out, cap, "%s%s%s", (t & MEM_RELEASE) ? "liberar" : "", (t & MEM_DECOMMIT) ? "descomprometer" : "", (t & ~(MEM_RELEASE | MEM_DECOMMIT)) ? "+otro" : "");
}
// quien llamo: "sider.dll+0x1234", o "codigo sin modulo (JIT de Lua?) en 0x..."
static std::string Who(void* ra) {
    HMODULE m = nullptr;
    if (ra && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ra, &m) && m) return ModOff(ra);
    char b[64]; snprintf(b, sizeof b, "codigo sin modulo (JIT de Lua?) en %p", ra); return b;
}
static std::string WhoModule(void* ra) {
    std::string w = Who(ra); size_t k = w.find('+');
    return k == std::string::npos ? std::string("sin modulo (JIT?)") : w.substr(0, k);
}

// mapa de regiones de [b, e): una linea por region (como lo ve VirtualQuery)
static void LogRegions(FILE* f, uintptr_t b, uintptr_t e, int maxLines) {
    uintptr_t a = b; int k = 0;
    while (a < e && k < maxLines) {
        MEMORY_BASIC_INFORMATION mi{};
        if (!VirtualQuery((void*)a, &mi, sizeof mi)) { fprintf(f, "      %p: VirtualQuery fallo\n", (void*)a); break; }
        fprintf(f, "      %p-%p (%6llu KB) reserva %p, %s, %s, %s%s\n", mi.BaseAddress, (void*)((uintptr_t)mi.BaseAddress + mi.RegionSize),
                (unsigned long long)(mi.RegionSize >> 10), mi.AllocationBase, StateName(mi.State), TypeName(mi.Type),
                mi.State == MEM_COMMIT ? ProtName(mi.Protect) : "-", (mi.Protect & PAGE_GUARD) ? " GUARD" : "");
        a = (uintptr_t)mi.BaseAddress + mi.RegionSize; k++;
    }
    if (a < e) fprintf(f, "      ... (sigue)\n");
}
// llamadas del anillo que tocaron [b, e): las ultimas maxLines (las mas nuevas al final)
static int LogTouches(FILE* f, uintptr_t b, uintptr_t e, int maxLines) {
    const unsigned n = s_ringN.load(), cnt = n < kRing ? n : kRing;
    const double now = Now();
    auto hit = [&](const MwEv& x) { return x.kind && x.kind != EV_MUNMAP && x.addr + (x.size ? x.size : 1) > b && x.addr < e; };
    int total = 0;
    for (unsigned j = 0; j < cnt; j++) if (hit(s_ring[(n - cnt + j) % kRing])) total++;
    int idx = 0;
    for (unsigned j = 0; j < cnt; j++) {
        const MwEv x = s_ring[(n - cnt + j) % kRing];
        if (!hit(x) || idx++ < total - maxLines) continue;
        if (x.kind == EV_PROTECT)
            fprintf(f, "      hace %.1f s, hilo %u: VirtualProtect(%p, %llu) %s -> %s%s, llamado desde %s\n", now - x.t, x.tid, (void*)x.addr,
                    (unsigned long long)x.size, ProtName(x.b), ProtName(x.a), x.ok ? "" : " (fallo)", Who(x.caller).c_str());
        else {
            char ft[48]; FreeType(ft, sizeof ft, x.a);
            fprintf(f, "      hace %.1f s, hilo %u: VirtualFree(%p, %llu, %s)%s, llamado desde %s\n", now - x.t, x.tid, (void*)x.addr,
                    (unsigned long long)x.size, ft, x.ok ? "" : " (fallo)", Who(x.caller).c_str());
        }
    }
    if (total == 0) fprintf(f, "      (ninguna llamada a VirtualProtect/VirtualFree sobre esa zona en las ultimas %u anotadas)\n", cnt);
    else if (total > maxLines) fprintf(f, "      (%d llamadas en total sobre esa zona, se muestran las ultimas %d)\n", total, maxLines);
    return total;
}

// ultimas lineas de sider.log (que estaba haciendo Sider / los modulos Lua)
static void LogSiderTail(FILE* f, int lines) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe; dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
    const std::wstring cands[] = { dir + L"Dream Patch Sider\\sider.log", dir + L"sider\\sider.log", dir + L"sider.log" };
    for (const auto& p : cands) {
        HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz);
        static char buf[6144];
        const LONGLONG take = sz.QuadPart < (LONGLONG)sizeof(buf) - 1 ? sz.QuadPart : (LONGLONG)sizeof(buf) - 1;
        LARGE_INTEGER off; off.QuadPart = sz.QuadPart - take;
        DWORD rd = 0;
        SetFilePointerEx(h, off, nullptr, FILE_BEGIN);
        ReadFile(h, buf, (DWORD)take, &rd, nullptr);
        CloseHandle(h);
        buf[rd] = 0;
        int seen = 0; char* s = buf + rd;
        while (s > buf && seen <= lines) { s--; if (*s == '\n') seen++; }
        if (*s == '\n') s++;
        fprintf(f, "    ultimas lineas de %ls:\n", p.c_str() + dir.size());
        char* ctx = nullptr;
        for (char* ln = strtok_s(s, "\r\n", &ctx); ln; ln = strtok_s(nullptr, "\r\n", &ctx)) fprintf(f, "      | %.240s\n", ln);
        return;
    }
    fprintf(f, "    (no encontre sider.log)\n");
}

static void LogProcessMem(FILE* f) {
    PROCESS_MEMORY_COUNTERS_EX pm{}; pm.cb = sizeof pm;
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof ms;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof pm) && GlobalMemoryStatusEx(&ms))
        fprintf(f, "    memoria del proceso: privada %llu MB (pico de uso %llu MB), en RAM %llu MB | Windows: RAM libre %llu de %llu MB, comprometida libre %llu MB\n",
                (unsigned long long)(pm.PrivateUsage >> 20), (unsigned long long)(pm.PeakPagefileUsage >> 20), (unsigned long long)(pm.WorkingSetSize >> 20),
                (unsigned long long)(ms.ullAvailPhys >> 20), (unsigned long long)(ms.ullTotalPhys >> 20), (unsigned long long)(ms.ullAvailPageFile >> 20));
}

// ---------------------------------------------------------------- win32munmap del juego
typedef int (__fastcall *PFN_Munmap)(void* ptr, size_t size);
static PFN_Munmap o_munmap = nullptr;
static std::atomic<unsigned> s_mCalls{0}, s_mHarmless{0}, s_mOdd{0}, s_mFixed{0}, s_mKept{0}, s_mOrigFail{0};
static std::atomic<unsigned long long> s_mBytes{0};
static bool s_fix = false;   // true = liberar cada reserva entera (memoria_arreglo = 2); false = retener
static std::mutex s_logOdd;

// lo mismo que revisa el juego, sin liberar nada: 1 = su win32munmap terminaria bien, 0 = fallaria en la primera
// reserva sin haber liberado nada (inofensivo: el juego se queda con el bloque), -1 = fallaria DESPUES de liberar algo
// (el cierre de 0x3fe31d)
static int GameOutcome(uintptr_t p, size_t size) {
    bool freed = false;
    while (size) {
        MEMORY_BASIC_INFORMATION mi{};
        if (!VirtualQuery((void*)p, &mi, sizeof mi)) return freed ? -1 : 0;
        if ((uintptr_t)mi.BaseAddress != p || (uintptr_t)mi.AllocationBase != p || mi.State != MEM_COMMIT || mi.RegionSize > size) return freed ? -1 : 0;
        p += mi.RegionSize; size -= mi.RegionSize; freed = true;
    }
    return 1;
}
// reservas que empiezan dentro de [p, p+size) y no se salen; todo comprometido. Si se puede, las lista en out.
static bool PlanRelease(uintptr_t p, size_t size, std::vector<uintptr_t>& out) {
    const uintptr_t e = p + size; uintptr_t a = p;
    while (a < e) {
        MEMORY_BASIC_INFORMATION mi{};
        if (!VirtualQuery((void*)a, &mi, sizeof mi) || mi.State != MEM_COMMIT || mi.Type != MEM_PRIVATE) return false;
        const uintptr_t ab = (uintptr_t)mi.AllocationBase;
        if (ab < p) return false;                                       // la reserva empieza antes del segmento
        if (out.empty() || out.back() != ab) { if (ab != a) return false; out.push_back(ab); }
        a = (uintptr_t)mi.BaseAddress + mi.RegionSize;
    }
    if (a != e) return false;                                           // la ultima region se sale del segmento
    MEMORY_BASIC_INFORMATION mi{};                                      // y la reserva no sigue despues del final
    if (VirtualQuery((void*)e, &mi, sizeof mi) && (uintptr_t)mi.AllocationBase == out.back() && mi.State != MEM_FREE) return false;
    return true;
}

static int __fastcall hk_Munmap(void* ptr, size_t size) {
    void* ra = _ReturnAddress();
    s_mCalls.fetch_add(1);
    const uintptr_t p = (uintptr_t)ptr;
    const int outcome = size ? GameOutcome(p, size) : 1;
    if (outcome >= 0) {   // bien, o falla sin liberar nada: igual que siempre
        if (outcome == 0) s_mHarmless.fetch_add(1);
        int r = o_munmap(ptr, size);
        if (r == 0) s_mBytes.fetch_add(size); else s_mOrigFail.fetch_add(1);
        Push(EV_MUNMAP, p, size, (uint32_t)r, 0, ra, r == 0);
        return r;
    }
    // Windows no ve el segmento como el juego espera: su win32munmap liberaria una parte y despues el juego escribiria
    // en lo liberado (el cierre de 0x3fe31d)
    s_mOdd.fetch_add(1);
    std::vector<uintptr_t> plan;
    const bool canFix = s_fix && PlanRelease(p, size, plan);
    const bool verbose = s_mOdd.load() <= 40;
    if (verbose) {   // el mapa ANTES de tocar nada
        std::lock_guard<std::mutex> lk(s_logOdd);
        Log("memoria: el juego quiso devolverle a Windows %llu KB en %p (llamado desde %s) y la memoria NO esta como la dejo (su liberador soltaria una parte y el juego escribiria en lo soltado):",
            (unsigned long long)(size >> 10), ptr, Who(ra).c_str());
        if (FILE* f = LogLock()) {
            LogRegions(f, p, p + size, 24);
            fprintf(f, "    quien toco esa zona:\n");
            LogTouches(f, p, p + size, 16);
            LogProcessMem(f);
            LogSiderTail(f, 12);
            fflush(f);
        }
        LogUnlock();
    }
    int r = -1;
    if (canFix) {
        t_in++;
        bool ok = true;
        for (uintptr_t ab : plan) ok &= VirtualFree((void*)ab, 0, MEM_RELEASE) != 0;
        t_in--;
        r = ok ? 0 : -1;
        if (ok) { s_mFixed.fetch_add(1); s_mBytes.fetch_add(size); }
    } else s_mKept.fetch_add(1);
    Push(EV_MUNMAP, p, size, (uint32_t)r, 1, ra, r == 0);
    if (verbose) {
        if (canFix) Log("memoria:   -> %s (%zu reservas liberadas enteras; sin esto el juego se cerraba en PES2021.exe+0x3fe31d)", r == 0 ? "arreglado" : "VirtualFree fallo", plan.size());
        else Log("memoria:   -> retenido: no se libera nada, el juego sigue con ese bloque como libre (sin cierre en PES2021.exe+0x3fe31d)");
    }
    return r;
}

// ---------------------------------------------------------------- resumen cada 30 s
static DWORD WINAPI SummaryThread(void*) {
    unsigned last = 0;
    for (;;) {
        Sleep(30000);
        const unsigned n = s_ringN.load();
        const unsigned cnt = (n - last) < kRing ? (n - last) : kRing;
        struct Agg { std::string mod; unsigned prot = 0, privProt = 0, free = 0, partial = 0; };
        std::vector<Agg> ag;
        auto slot = [&](void* ra) -> Agg& {
            std::string m = WhoModule(ra);
            for (auto& x : ag) if (x.mod == m) return x;
            ag.push_back({ m }); return ag.back();
        };
        for (unsigned j = 0; j < cnt; j++) {
            const MwEv e = s_ring[(n - cnt + j) % kRing];
            if (!e.kind || e.kind == EV_MUNMAP) continue;
            Agg& a = slot(e.caller);
            if (e.kind == EV_PROTECT) {
                a.prot++;
                MEMORY_BASIC_INFORMATION mi{};
                if (VirtualQuery((void*)e.addr, &mi, sizeof mi) && mi.Type == MEM_PRIVATE) a.privProt++;
            } else { a.free++; if (e.a & MEM_DECOMMIT) a.partial++; }
        }
        last = n;
        std::string s;
        for (auto& x : ag) {
            char b[200]; snprintf(b, sizeof b, "%s%s: VirtualProtect %u (%u en memoria privada), VirtualFree %u (%u descomprometer)",
                                  s.empty() ? "" : " | ", x.mod.c_str(), x.prot, x.privProt, x.free, x.partial);
            s += b; if (s.size() > 1200) { s += " ..."; break; }
        }
        Log("memoria (30 s): segmentos devueltos %u (%llu MB), que el juego se queda sin problema %u, PELIGROSOS %u (arreglados %u, retenidos %u), fallos propios del juego %u | %s",
            s_mCalls.load(), s_mBytes.load() >> 20, s_mHarmless.load(), s_mOdd.load(), s_mFixed.load(), s_mKept.load(), s_mOrigFail.load(), s.empty() ? "nadie llamo VirtualProtect/VirtualFree" : s.c_str());
    }
}

void MemWatchStart() {
    s_fix = g_cfg.memFix >= 2;
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    bool exeOk = (uintptr_t)mod == 0x140000000ull && nt->FileHeader.TimeDateStamp == kStamp;
    if (!exeOk) Log("memoria: exe distinto (sello %08X): sin gancho en el liberador del juego", (unsigned)nt->FileHeader.TimeDateStamp);
    else if (memcmp((const void*)kMunmap, kMunmapSig, sizeof kMunmapSig) != 0) Log("memoria: el liberador del juego (PES2021.exe+0x3fc0d0) no tiene la firma esperada: sin gancho");
    else if (MH_CreateHook((void*)kMunmap, (void*)&hk_Munmap, (void**)&o_munmap) != MH_OK || MH_EnableHook((void*)kMunmap) != MH_OK)
        Log("memoria: no se pudo enganchar el liberador del juego");
    else Log("memoria: liberador del juego vigilado (PES2021.exe+0x3fc0d0, segmentos peligrosos: %s)", s_fix ? "se liberan enteros (memoria_arreglo = 2)" : "se retienen");
    if (g_cfg.memWatch) {
        HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
        void* vp = kb ? (void*)GetProcAddress(kb, "VirtualProtect") : nullptr;
        void* vf = kb ? (void*)GetProcAddress(kb, "VirtualFree") : nullptr;
        t_in++;
        bool ok = vp && vf && MH_CreateHook(vp, (void*)&hk_VirtualProtect, (void**)&o_vp) == MH_OK && MH_CreateHook(vf, (void*)&hk_VirtualFree, (void**)&o_vf) == MH_OK
                  && MH_EnableHook(vp) == MH_OK && MH_EnableHook(vf) == MH_OK;
        t_in--;
        if (ok) { s_watch = true; Log("memoria: VirtualProtect/VirtualFree vigilados (quien los llama queda en un anillo de %u; resumen cada 30 s)", kRing); }
        else Log("memoria: no se pudo enganchar VirtualProtect/VirtualFree");
    } else Log("memoria: memoria_vigilar = 0: sin anillo de VirtualProtect/VirtualFree");
    if (HANDLE h = CreateThread(nullptr, 0, SummaryThread, nullptr, 0, nullptr)) { SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(h); }
}

// llamado desde el registro de cierres (main.cpp), con el log ya tomado
void MemCrashInfo(FILE* f, EXCEPTION_POINTERS* ep) {
    auto* er = ep->ExceptionRecord; CONTEXT* cx = ep->ContextRecord;
    __try {
        if ((uintptr_t)cx->Rip == kCrashRip) {
            fprintf(f, "    === por que: el administrador de memoria del juego (dlmalloc) devolvio un segmento de %llu KB en %p a Windows,\n"
                       "        su liberador fallo a mitad de camino y el juego anoto el bloque como libre en memoria que ya no existe.\n"
                       "        Pasa cuando alguien cambio esas paginas (proteccion / descomprometer) mientras el juego las tenia.\n",
                    (unsigned long long)(cx->Rbp >> 10), (void*)cx->Rbx);
            uintptr_t ms = (uintptr_t)cx->Rsi; size_t fp = 0, mfp = 0;
            __try { fp = *(size_t*)(ms + 0x358); mfp = *(size_t*)(ms + 0x360); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            fprintf(f, "    dlmalloc %p: memoria tomada de Windows %llu MB (maximo %llu MB)\n", (void*)ms, (unsigned long long)(fp >> 20), (unsigned long long)(mfp >> 20));
            fprintf(f, "    regiones del segmento:\n");
            LogRegions(f, (uintptr_t)cx->Rbx, (uintptr_t)cx->Rbx + (uintptr_t)cx->Rbp, 24);
            fprintf(f, "    quien toco el segmento:\n");
            LogTouches(f, (uintptr_t)cx->Rbx, (uintptr_t)cx->Rbx + (uintptr_t)cx->Rbp, 16);
        } else if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
            const uintptr_t a = er->ExceptionInformation[1];
            fprintf(f, "    memoria alrededor de %p:\n", (void*)a);
            LogRegions(f, (a & ~(uintptr_t)0xFFFF) - 0x10000, (a & ~(uintptr_t)0xFFFF) + 0x20000, 10);
            fprintf(f, "    quien toco esa zona:\n");
            LogTouches(f, (a & ~(uintptr_t)0xFFF) - 0x10000, (a & ~(uintptr_t)0xFFF) + 0x10000, 12);
        }
        fprintf(f, "    liberador del juego: %u segmentos devueltos, %u peligrosos (arreglados %u, retenidos %u)\n", s_mCalls.load(), s_mOdd.load(), s_mFixed.load(), s_mKept.load());
        LogProcessMem(f);
    } __except (EXCEPTION_EXECUTE_HANDLER) { fprintf(f, "    (fallo al armar el informe de memoria)\n"); }
    LogSiderTail(f, 15);
}

} // namespace fx
