// PES21 FX - punto de entrada. Sider llama a PES21FX_Init(carpeta) desde PES21FX.lua (LuaJIT FFI).
#include "core.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <psapi.h>
#include <tlhelp32.h>

namespace fx {

static FILE* g_log = nullptr;
static std::mutex g_logMutex;

void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_log) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

double Now() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(f.QuadPart);
}

// ---------------------------------------------------------------- registro de cuelgues
// Si el juego se cierra por un error, queda en el log en que modulo y donde paso (y la pila).
std::string ModOff(void* p) {
    HMODULE m = nullptr; char n[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &m))
        GetModuleFileNameA(m, n, MAX_PATH);
    const char* b = strrchr(n, '\\');
    char out[MAX_PATH + 32];
    snprintf(out, sizeof(out), "%s+0x%llx", b ? b + 1 : n, (unsigned long long)((uintptr_t)p - (uintptr_t)m));
    return out;
}
static void LogCrash(EXCEPTION_POINTERS* ep, const char* kind) {
    if (!g_log) return;
    bool locked = g_logMutex.try_lock();
    auto* er = ep->ExceptionRecord;
    fprintf(g_log, "*** %s 0x%08lx en %s (hilo %lu)\n", kind, er->ExceptionCode, ModOff(er->ExceptionAddress).c_str(), GetCurrentThreadId());
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
        fprintf(g_log, "    %s en la direccion %p\n", er->ExceptionInformation[0] == 1 ? "escritura" : er->ExceptionInformation[0] == 8 ? "ejecucion" : "lectura",
                (void*)er->ExceptionInformation[1]);
    CONTEXT* cx = ep->ContextRecord;
    fprintf(g_log, "    RIP=%p RSP=%p RCX=%p RDX=%p\n", (void*)cx->Rip, (void*)cx->Rsp, (void*)cx->Rcx, (void*)cx->Rdx);
    // la pila "a mano": direcciones de retorno que caen dentro de algun modulo (sirve aunque no haya info de unwind)
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t sp = (uintptr_t)cx->Rsp;
    if (VirtualQuery((void*)sp, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT) {
        uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        int shown = 0;
        for (uintptr_t a = sp; a + 8 <= end && a < sp + 0x2000 && shown < 24; a += 8) {
            uintptr_t v = *(uintptr_t*)a;
            HMODULE m = nullptr;
            if (v > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)v, &m) && m) {
                fprintf(g_log, "    [rsp+0x%03llx] %s\n", (unsigned long long)(a - sp), ModOff((void*)v).c_str());
                shown++;
            }
        }
    }
    fflush(g_log);
    if (locked) g_logMutex.unlock();
}
static bool IsFatal(DWORD c) {
    return c == EXCEPTION_ACCESS_VIOLATION || c == EXCEPTION_ILLEGAL_INSTRUCTION || c == EXCEPTION_PRIV_INSTRUCTION ||
           c == EXCEPTION_STACK_OVERFLOW || c == EXCEPTION_INT_DIVIDE_BY_ZERO || c == 0xC0000409 || c == 0xC0000374;
}
static LONG CALLBACK Vectored(EXCEPTION_POINTERS* ep) {
    static std::atomic<int> n{0};
    if (IsFatal(ep->ExceptionRecord->ExceptionCode) && n++ < 6) LogCrash(ep, "excepcion");
    return EXCEPTION_CONTINUE_SEARCH;
}
// ---------------------------------------------------------------- carga/descarga de DLLs (para cruzar con la direccion de un cierre)
struct FxUStr { USHORT Length, MaximumLength; PWSTR Buffer; };
struct FxLdrData { ULONG Flags; const FxUStr* FullDllName; const FxUStr* BaseDllName; PVOID DllBase; ULONG SizeOfImage; };
using PFN_LdrCb = VOID(CALLBACK*)(ULONG, const FxLdrData*, PVOID);
using PFN_LdrReg = LONG(NTAPI*)(ULONG, PFN_LdrCb, PVOID, PVOID*);
// ---------------------------------------------------------------- hilos huerfanos de una DLL que se descarga
// Caso real: "DreamPatch Switcher.dll" deja un hilo propio dormido (Sleep) y otro hilo descarga la DLL;
// cuando el Sleep termina, el hilo vuelve a codigo que ya no existe y el juego se cierra
// (RIP dentro de la DLL descargada, RCX = direccion de ntdll justo despues del syscall).
// Aca, en el momento de la descarga, se buscan los hilos que arrancaron en esa DLL, estan ejecutandola
// o van a volver a ella, y se los hace terminar limpiamente (RIP -> RtlExitUserThread).
struct FxTBI { LONG ExitStatus; PVOID TebBaseAddress; struct { HANDLE p, t; } ClientId; ULONG_PTR AffinityMask; LONG Priority; LONG BasePriority; };
using PFN_NtQIT = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static bool ReadableAt(uintptr_t p, size_t n) {
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery((void*)p, &m, sizeof(m)) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return p + n <= (uintptr_t)m.BaseAddress + m.RegionSize;
}
static int OrphanThreads(uintptr_t b, uintptr_t e, const char* dll) {
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto qit = (PFN_NtQIT)GetProcAddress(nt, "NtQueryInformationThread");
    void* exitFn = (void*)GetProcAddress(nt, "RtlExitUserThread");
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) { Log("  (no se pudo listar los hilos)"); return 0; }
    THREADENTRY32 te{}; te.dwSize = sizeof(te);
    int n = 0, seen = 0; DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        seen++;
        bool orphan = false; char why[96] = "";
        void* start = nullptr;
        if (qit && qit(h, 9 /*ThreadQuerySetWin32StartAddress*/, &start, sizeof(start), nullptr) == 0 && (uintptr_t)start >= b && (uintptr_t)start < e) {
            orphan = true; snprintf(why, sizeof(why), "arranco en %s+0x%llx", dll, (unsigned long long)((uintptr_t)start - b));
        }
        if (SuspendThread(h) == (DWORD)-1) { CloseHandle(h); continue; }
        CONTEXT cx{}; cx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        bool gotCx = GetThreadContext(h, &cx) != 0;
        if (gotCx) {
            if (cx.Rip >= b && cx.Rip < e) { orphan = true; snprintf(why, sizeof(why), "ejecuta %s+0x%llx", dll, (unsigned long long)(cx.Rip - b)); }
            FxTBI tbi{};
            if (!orphan && qit && qit(h, 0 /*ThreadBasicInformation*/, &tbi, sizeof(tbi), nullptr) == 0 && tbi.TebBaseAddress && ReadableAt((uintptr_t)tbi.TebBaseAddress, 24)) {
                uintptr_t sb = *(uintptr_t*)((uintptr_t)tbi.TebBaseAddress + 8);   // NT_TIB.StackBase
                for (uintptr_t a = cx.Rsp & ~(uintptr_t)7; a + 8 <= sb && a < cx.Rsp + 0x20000; a += 8) {
                    if (!ReadableAt(a, 8)) break;
                    uintptr_t v = *(uintptr_t*)a;
                    if (v >= b && v < e) { orphan = true; snprintf(why, sizeof(why), "va a volver a %s+0x%llx", dll, (unsigned long long)(v - b)); break; }
                }
            }
        }
        if (orphan && gotCx) {
            Log("  hilo %lu: %s, parado en %s -> se lo hace terminar", te.th32ThreadID, why, ModOff((void*)cx.Rip).c_str());
            cx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            cx.Rip = (DWORD64)exitFn; cx.Rcx = 0;
            if (exitFn && SetThreadContext(h, &cx)) { ResumeThread(h); n++; }
            else Log("  (no se pudo redirigir el hilo %lu; queda suspendido)", te.th32ThreadID);
            CloseHandle(h);
            continue;
        }
        ResumeThread(h);
        CloseHandle(h);
    }
    CloseHandle(snap);
    Log("  hilos revisados: %d, huerfanos de %s: %d", seen, dll, n);
    return n;
}

// volcado de una DLL tal como esta en memoria (para analizar una DLL empaquetada) + sus hilos
static void DumpModule(const wchar_t* name, const wchar_t* outName) {
    HMODULE m = GetModuleHandleW(name);
    if (!m) { Log("volcado: %ls no esta cargada", name); return; }
    auto* dos = (IMAGE_DOS_HEADER*)m; auto* nt = (IMAGE_NT_HEADERS64*)((uint8_t*)m + dos->e_lfanew);
    DWORD size = nt->OptionalHeader.SizeOfImage;
    std::wstring path = g_cfg.baseDir + outName;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { Log("volcado: no se pudo crear %ls", outName); return; }
    DWORD total = 0;
    for (DWORD off = 0; off < size; off += 0x1000) {
        uint8_t page[0x1000]; MEMORY_BASIC_INFORMATION mbi{};
        uintptr_t a = (uintptr_t)m + off;
        bool ok = VirtualQuery((void*)a, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
        if (ok) memcpy(page, (void*)a, sizeof(page)); else memset(page, 0, sizeof(page));
        DWORD wr; WriteFile(h, page, sizeof(page), &wr, nullptr); total += wr;
    }
    CloseHandle(h);
    Log("volcado: %ls -> %ls (%lu bytes, base %p)", name, outName, total, (void*)m);
    // hilos que arrancaron en esa DLL
    HMODULE ntd = GetModuleHandleW(L"ntdll.dll");
    auto qit = (PFN_NtQIT)GetProcAddress(ntd, "NtQueryInformationThread");
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{}; te.dwSize = sizeof(te); DWORD pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE th = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!th) continue;
        void* start = nullptr;
        if (qit && qit(th, 9, &start, sizeof(start), nullptr) == 0 && (uintptr_t)start >= (uintptr_t)m && (uintptr_t)start < (uintptr_t)m + size) {
            CONTEXT cx{}; cx.ContextFlags = CONTEXT_CONTROL;
            std::string where = "?";
            if (SuspendThread(th) != (DWORD)-1) { if (GetThreadContext(th, &cx)) where = ModOff((void*)cx.Rip); ResumeThread(th); }
            Log("  hilo %lu arranco en %ls+0x%llx, ahora en %s", te.th32ThreadID, name, (unsigned long long)((uintptr_t)start - (uintptr_t)m), where.c_str());
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
}

static VOID CALLBACK DllNotify(ULONG reason, const FxLdrData* d, PVOID) {
    if (!d || !d->BaseDllName) return;
    Log("%s %.*ls en %p-%p", reason == 1 ? "DLL cargada:" : "DLL DESCARGADA:", (int)(d->BaseDllName->Length / 2), d->BaseDllName->Buffer,
        d->DllBase, (void*)((uintptr_t)d->DllBase + d->SizeOfImage));
    if (reason == 2) {
        char nm[260]; snprintf(nm, sizeof(nm), "%.*ls", (int)(d->BaseDllName->Length / 2), d->BaseDllName->Buffer);
        // quien la descarga (hilo actual)
        void* fr[16]; USHORT k = RtlCaptureStackBackTrace(1, 16, fr, nullptr);
        std::string who;
        for (USHORT i = 0; i < k; i++) { std::string m = ModOff(fr[i]); if (m.rfind("ntdll", 0) == 0) continue; who += m + " < "; if (who.size() > 200) break; }
        Log("  la descarga el hilo %lu: %s", GetCurrentThreadId(), who.c_str());
        uintptr_t b = (uintptr_t)d->DllBase, e = b + d->SizeOfImage;
        ScanRefsInto(b, e, nm);
        if (g_cfg.optOrphan) OrphanThreads(b, e, nm); else Log("  (orphan_fix = 0: no se tocan los hilos)");
    }
}
static void WatchDlls() {
    static PVOID cookie = nullptr;
    auto reg = (PFN_LdrReg)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification");
    if (reg) reg(0, DllNotify, nullptr, &cookie);
}

// DreamPatch Switcher.dll se descarga sola a los pocos segundos pero deja algo que la sigue llamando
// (cierre en "DreamPatch Switcher.dll"+0xdcb0). Se "clavan" en memoria las DLL de la carpeta del juego
// que ya estan cargadas para que ninguna pueda descargarse con codigo suyo todavia en uso.
static void PinGameDlls() {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe; dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
    for (auto& ch : dir) ch = (wchar_t)towlower(ch);
    HMODULE mods[1024]; DWORD need = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need)) return;
    int n = (int)(need / sizeof(HMODULE));
    for (int i = 0; i < n && i < 1024; i++) {
        wchar_t path[MAX_PATH]; if (!GetModuleFileNameW(mods[i], path, MAX_PATH)) continue;
        std::wstring lp = path; for (auto& ch : lp) ch = (wchar_t)towlower(ch);
        if (lp.rfind(dir, 0) != 0 || lp.size() < 4 || lp.compare(lp.size() - 4, 4, L".exe") == 0) continue;
        HMODULE h = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, path, &h)) Log("DLL clavada en memoria: %ls", path + dir.size());
    }
}

static LPTOP_LEVEL_EXCEPTION_FILTER s_prevFilter = nullptr;
static LONG WINAPI Unhandled(EXCEPTION_POINTERS* ep) {
    LogCrash(ep, "CIERRE DEL JUEGO");
    return s_prevFilter ? s_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

} // namespace fx

static std::atomic<bool> s_inited{false};
static HMODULE s_self = nullptr;
bool fx_LoadRealD3D11();

// Inicializa el mod. Se llama la primera vez que el juego pide un dispositivo D3D11 (fuera del loader lock).
static int InitMod() {
    if (s_inited.exchange(true)) return 1;
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring wb = exe; wb = wb.substr(0, wb.find_last_of(L"\\/") + 1) + L"PES21FX\\";
    fx::g_cfg.baseDir = wb;
    CreateDirectoryW(wb.c_str(), nullptr);
    fx::g_log = _wfopen((wb + L"PES21FX.log").c_str(), L"w");
    fx::Log("PES21 FX 0.2 (d3d11.dll) - carpeta: %ls", wb.c_str());
    AddVectoredExceptionHandler(1, fx::Vectored);
    fx::s_prevFilter = SetUnhandledExceptionFilter(fx::Unhandled);
    fx::WatchDlls();
    HMODULE nv = LoadLibraryW(L"nvapi64.dll");   // 3DMigoto la dejaba cargada; hacemos lo mismo
    fx::Log("nvapi64.dll: %s", nv ? "cargada y retenida" : "no esta (placa no NVIDIA?)");
    for (const wchar_t* mn : {L"sider.dll", L"dsound.dll", L"dxgi.dll", L"DreamPatch Switcher.dll", L"PES2021.exe"}) {
        HMODULE hm = GetModuleHandleW(mn);
        fx::Log("modulo %ls en %p", mn, (void*)hm);
    }
    fx::Log("modulo propio (d3d11.dll) en %p", (void*)s_self);
    if (!fx::LoadConfig()) fx::Log("aviso: no se pudo leer la configuracion");
    if (fx::g_cfg.optPin) fx::PinGameDlls();
    if (fx::g_cfg.optDump) fx::DumpModule(L"DreamPatch Switcher.dll", L"dump_switcher.bin");
    if (!fx::g_cfg.optHooks) { fx::Log("hooks = 0: el mod queda cargado pero sin ganchos (prueba)"); return 2; }
    if (!fx::InstallHooks()) { fx::Log("ERROR: no se pudieron instalar los ganchos de D3D11"); return 0; }
    fx::Log("ganchos instalados");
    return 2;
}
void fx_EnsureInit() { InitMod(); }

// compatibilidad con el modulo de Sider viejo (ya no hace falta)
extern "C" __declspec(dllexport) int PES21FX_Init(const char*) { return InitMod(); }

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) { s_self = h; DisableThreadLibraryCalls(h); fx_LoadRealD3D11(); }
    if (reason == DLL_PROCESS_DETACH && fx::g_log) {   // el juego se esta cerrando "normalmente" (ExitProcess)
        fprintf(fx::g_log, "--- fin del proceso (%s)\n", reserved ? "el juego salio" : "DLL descargada");
        fflush(fx::g_log);
    }
    return TRUE;
}
