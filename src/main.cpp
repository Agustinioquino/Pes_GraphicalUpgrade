// PES21 FX - punto de entrada. Sider llama a PES21FX_Init(carpeta) desde PES21FX.lua (LuaJIT FFI).
#include "core.h"
#include <algorithm>
#include <vector>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <psapi.h>
#include <tlhelp32.h>

namespace fx {

static FILE* g_log = nullptr;
static std::mutex g_logMutex;

// (27/10) registro del sol: archivo propio por partida (PES21FX\registro_sol\sol_AAAAMMDD_HHMMSS.txt) que el juego no pisa. Recibe
// las lineas del sol y la luz del log ("luz del sol", "cielo:", "sol:", "luz:") y las de SunLog (estado por segundo, ajustes)
static FILE* g_sunLog = nullptr; static bool g_sunLogTried = false;
static FILE* SunLogFile() {
    if (g_sunLog || g_sunLogTried || g_cfg.baseDir.empty()) return g_sunLog;
    g_sunLogTried = true;
    std::wstring d = g_cfg.baseDir + L"registro_sol\\"; CreateDirectoryW(d.c_str(), nullptr);
    SYSTEMTIME st; GetLocalTime(&st); wchar_t n[64];
    swprintf(n, 64, L"sol_%04d%02d%02d_%02d%02d%02d.txt", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_sunLog = _wfopen((d + n).c_str(), L"w");
    if (g_sunLog) fputs(
        "Registro del sol y la luz de PES21FX (un archivo por partida; el juego no lo borra).\n"
        "Vectores (x, y, z) en el mundo: y = altura (altura en grados = arcoseno de y), x / z = plano de la cancha. Rumbos en grados.\n"
        "estado: una linea por segundo con la luz del juego en mundo, el sol del archivo del cielo, la diferencia entre los dos (0-5 = coinciden),\n"
        "        la camara, el color del sol (rgb e intensidad), la exposicion y las pasadas del sol que corrieron en ese cuadro.\n"
        "ajuste: un valor del menu / ini que cambio (fila y componente, antes -> ahora). Al empezar se anotan los ajustes del sol y las sombras.\n"
        "El resto son las lineas del sol del PES21FX.log, copiadas con la misma hora.\n\n", g_sunLog);
    return g_sunLog;
}
static bool SunLine(const char* m) {
    return !strncmp(m, "luz del sol", 11) || !strncmp(m, "cielo:", 6) || !strncmp(m, "sol:", 4) || !strncmp(m, "luz:", 4) || !strncmp(m, "  llena el cbuffer", 18);
}
static void WriteLine(FILE* f, const SYSTEMTIME& st, const char* m) {
    fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, m); fflush(f);
}

void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_log) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char buf[8192]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    WriteLine(g_log, st, buf);
    if (SunLine(buf)) if (FILE* f = SunLogFile()) WriteLine(f, st, buf);
}
// solo al registro del sol
void SunLog(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    FILE* f = SunLogFile(); if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char buf[8192]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    WriteLine(f, st, buf);
}

// (26/10) para escribir bloques largos de una vez (memwatch.cpp): LogLock devuelve el archivo con el log tomado
FILE* LogLock() { g_logMutex.lock(); return g_log; }
void LogUnlock() { g_logMutex.unlock(); }

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
// (24/10) bytes del codigo alrededor de RIP: si no coinciden con la copia de la vigilancia (codewatch.cpp), el codigo del
// juego estaba cambiado cuando se cayo
static bool SafeReadCode(void* dst, uintptr_t src, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery((void*)src, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    if (src + n > (uintptr_t)mi.BaseAddress + mi.RegionSize) return false;
    __try { memcpy(dst, (const void*)src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void HexMark(char* out, size_t cap, const unsigned char* b, size_t n, size_t mark) {
    size_t k = 0; out[0] = 0;
    for (size_t i = 0; i < n && k + 6 < cap; i++) k += snprintf(out + k, cap - k, i == mark ? "[%02x] " : "%02x ", b[i]);
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
    fprintf(g_log, "    RAX=%p RBX=%p RBP=%p RSI=%p RDI=%p R8=%p R9=%p\n    R10=%p R11=%p R12=%p R13=%p R14=%p R15=%p\n",
            (void*)cx->Rax, (void*)cx->Rbx, (void*)cx->Rbp, (void*)cx->Rsi, (void*)cx->Rdi, (void*)cx->R8, (void*)cx->R9,
            (void*)cx->R10, (void*)cx->R11, (void*)cx->R12, (void*)cx->R13, (void*)cx->R14, (void*)cx->R15);
    {
        unsigned char cb[48], sb[48]; char line[48 * 5 + 8];
        const uintptr_t c0 = (uintptr_t)cx->Rip - 16;
        if (SafeReadCode(cb, c0, sizeof cb)) {
            HexMark(line, sizeof line, cb, sizeof cb, 16);
            fprintf(g_log, "    codigo en RIP-16: %s\n", line);
            if (CodeWatchOrig(c0, sb, sizeof sb)) {
                if (memcmp(cb, sb, sizeof cb) != 0) { HexMark(line, sizeof line, sb, sizeof sb, 16); fprintf(g_log, "    copia de antes  : %s <- EL CODIGO ESTABA CAMBIADO\n", line); }
                else fprintf(g_log, "    (el codigo es igual a la copia de la vigilancia)\n");
            }
        } else fprintf(g_log, "    codigo en RIP: no se puede leer\n");
    }
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
    if (!strcmp(kind, "CIERRE DEL JUEGO")) GpuMeterCrashInfo(g_log, ep);   // (07/11) ultimas destrucciones de recursos D3D11 (gpumeter.cpp)
    MemCrashInfo(g_log, ep);   // (26/10) mapa de memoria, quien la toco, dlmalloc y el final de sider.log (memwatch.cpp)
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
// direccion de retorno real: los bytes de antes son una instruccion CALL
static bool AfterCall(uintptr_t v) {
    if (!ReadableAt(v - 7, 7)) return false;
    const uint8_t* p = (const uint8_t*)v;
    if (p[-5] == 0xE8) return true;                                         // call rel32
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) return true;               // call r/m (sin desplazamiento)
    if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) return true;               // call [r+d8] / con SIB
    if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) return true;               // call [rip+d32] / [r+d32]
    if (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10) return true;               // call [r+SIB+d32]
    if (p[-3] == 0x41 && p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) return true; // call r8..r15
    return false;
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
        bool orphan = false, stackOnly = false; char why[96] = "";
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
                    // solo direcciones de retorno reales: dentro de una pagina ejecutable de la DLL (no el HMODULE ni datos)
                    if (v < b + 0x1000 || v >= e) continue;
                    MEMORY_BASIC_INFORMATION mi{};
                    if (!VirtualQuery((void*)v, &mi, sizeof(mi)) || !(mi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) continue;
                    // 10/11: un valor viejo en la pila no es una vuelta real. Solo cuenta si justo antes hay un CALL
                    // (E8 rel32, FF /2 con 2..7 bytes). Y un hilo que NO arranco en la DLL (pool de COM/ntdll del juego)
                    // no se termina por esto: matar un hilo del sistema parado en ntdll cerro el juego (22:40 PC, Switcher).
                    if (!AfterCall(v)) continue;
                    snprintf(why, sizeof(why), "va a volver a %s+0x%llx", dll, (unsigned long long)(v - b));
                    stackOnly = true; break;
                }
            }
        }
        if (stackOnly && !orphan) {
            Log("  hilo %lu: %s (solo por la pila, no arranco en la DLL), parado en %s -> no se toca", te.th32ThreadID, why, ModOff((void*)cx.Rip).c_str());
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
        // solo para DLL de la carpeta del juego (mods); nunca para las del sistema o del driver
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring gdir = exe; gdir = gdir.substr(0, gdir.find_last_of(L"\\/") + 1);
        std::wstring full = d->FullDllName ? std::wstring(d->FullDllName->Buffer, d->FullDllName->Length / 2) : L"";
        for (auto& ch : gdir) ch = (wchar_t)towlower(ch);
        for (auto& ch : full) ch = (wchar_t)towlower(ch);
        bool gameDll = full.rfind(gdir, 0) == 0;
        if (g_cfg.optOrphan && gameDll) OrphanThreads(b, e, nm);
        else if (g_cfg.optOrphan) Log("  (DLL del sistema: no se tocan los hilos)");
        else Log("  (orphan_fix = 0: no se tocan los hilos)");
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

// ---------------------------------------------------------------- (08/11) volcado propio del cierre
// Agustin: "agrandar el log ... algo asi como un crash dump". Al cerrarse el juego queda en PES21FX\cierres\ una carpeta por
// cierre (AAAAMMDD_HHMMSS) con: cierre.dmp (minidump con la memoria de datos del proceso, la de los hilos, los handles y los modulos
// descargados: se abre con WinDbg o Visual Studio y deja ver el objeto liberado), PES21FX.log y sider.log copiados en ese momento
// (al reabrir el juego se pisan) y el ini del momento. Se guardan los ultimos 8 cierres. [PES21FX] crash_dump = 0 lo apaga,
// 1 = minidump con datos (unos cientos de MB), 2 = memoria completa (varios GB).
// dbghelp se carga al arrancar (cargar una DLL dentro del cierre puede trabarse con el candado del cargador). El volcado lo escribe
// otro hilo (MiniDumpWriteDump desde el hilo que se cae no ve bien su propia pila).
using PFN_MDWD = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, int, const void*, const void*, const void*);
static PFN_MDWD s_mdwd = nullptr;
// (08/11 b) MINIDUMP_EXCEPTION_INFORMATION en dbghelp.h esta empaquetada a 4 bytes (pshpack4): ExceptionPointers va en +4, no en +8.
// Con el alineado normal dbgcore leia basura como puntero y se caia dentro del volcado (cierre 08/11 21:43: 0 bytes, sin logs)
#pragma pack(push, 4)
struct FxMdExc { DWORD ThreadId; EXCEPTION_POINTERS* ExceptionPointers; BOOL ClientPointers; };
#pragma pack(pop)
static_assert(sizeof(FxMdExc) == 16 && offsetof(FxMdExc, ExceptionPointers) == 4, "MINIDUMP_EXCEPTION_INFORMATION");
struct DumpJob { HANDLE file; FxMdExc exc; int type; BOOL ok; DWORD err; bool hasExc = true; };
static DWORD WINAPI DumpThread(LPVOID p) {
    auto* j = (DumpJob*)p;
    __try {
        j->ok = s_mdwd(GetCurrentProcess(), GetCurrentProcessId(), j->file, j->type, j->hasExc ? &j->exc : nullptr, nullptr, nullptr);
        j->err = j->ok ? 0 : GetLastError();
    } __except (EXCEPTION_EXECUTE_HANDLER) { j->ok = FALSE; j->err = 0xE0000000u | (GetExceptionCode() & 0xFFFFFF); }   // que el volcado nunca tire el cierre
    return 0;
}
static void PruneCrashDirs(const std::wstring& root, int keep) {
    std::vector<std::wstring> v; WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((root + L"2*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do { if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) v.push_back(fd.cFileName); } while (FindNextFileW(f, &fd));
    FindClose(f);
    std::sort(v.begin(), v.end());
    for (size_t i = 0; i + keep < v.size(); i++) {
        std::wstring d = root + v[i] + L"\\";
        HANDLE g = FindFirstFileW((d + L"*").c_str(), &fd);
        if (g != INVALID_HANDLE_VALUE) { do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((d + fd.cFileName).c_str()); } while (FindNextFileW(g, &fd)); FindClose(g); }
        RemoveDirectoryW(d.c_str());
    }
}
// (08/11 c) hang = true: congelamiento detectado por el vigia (sin Present); carpeta <fecha>_congelado, sin excepcion
static void WriteDumpTo(EXCEPTION_POINTERS* ep, bool hang) {
    std::wstring root = g_cfg.baseDir + L"cierres\\"; CreateDirectoryW(root.c_str(), nullptr);
    PruneCrashDirs(root, 7);
    SYSTEMTIME st; GetLocalTime(&st); wchar_t n[48];
    swprintf(n, 48, L"%04d%02d%02d_%02d%02d%02d%ls", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, hang ? L"_congelado" : L"");
    std::wstring dir = root + n + L"\\"; CreateDirectoryW(dir.c_str(), nullptr);
    if (g_log) fprintf(g_log, "    volcado del %s: PES21FX\\cierres\\%ls\\\n", hang ? "congelamiento" : "cierre", n);
    // primero los archivos de texto (si el volcado falla, quedan igual); al final se vuelve a copiar el log con las lineas del volcado
    auto copyAll = [&]() {
        if (g_log) fflush(g_log);
        CopyFileW((g_cfg.baseDir + L"PES21FX.log").c_str(), (dir + L"PES21FX.log").c_str(), FALSE);
        CopyFileW((g_cfg.baseDir + L"PES21FX.ini").c_str(), (dir + L"PES21FX.ini").c_str(), FALSE);
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring gdir = exe; gdir = gdir.substr(0, gdir.find_last_of(L"\\/") + 1);
        CopyFileW((gdir + L"sider.log").c_str(), (dir + L"sider.log").c_str(), FALSE);
    };
    copyAll();
    if (s_mdwd) {
        // (08/11 e) GENERIC_READ tambien: dbghelp relee el archivo para escribir el directorio de streams; solo con escritura fallaba con
            // 0x80070057 y quedaba un .dmp de 0.7 MB "sin informacion del sistema" (cierres 07:11 y 10:11)
            HANDLE h = CreateFileW((dir + (hang ? L"congelado.dmp" : L"cierre.dmp")).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            // MiniDumpWithDataSegs 0x1 | WithFullMemory 0x2 | WithHandleData 0x4 | WithUnloadedModules 0x20 | WithIndirectlyReferencedMemory 0x40
            // | WithPrivateReadWriteMemory 0x200 | WithFullMemoryInfo 0x800 | WithThreadInfo 0x1000
            int type = 0x1 | 0x4 | 0x20 | 0x40 | 0x800 | 0x1000;
            type |= g_cfg.optCrashDump >= 2 ? 0x2 : 0x200;
            DumpJob j{ h, { GetCurrentThreadId(), ep, FALSE }, type, FALSE, 0 };
            j.hasExc = ep != nullptr;
            HANDLE t = CreateThread(nullptr, 0, DumpThread, &j, 0, nullptr);
            if (t) { WaitForSingleObject(t, 120000); CloseHandle(t); } else DumpThread(&j);
            LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz); CloseHandle(h);
            if (g_log) fprintf(g_log, "    cierre.dmp: %s (%.1f MB%s)\n", j.ok ? "escrito" : "FALLO", sz.QuadPart / 1048576.0, j.ok ? "" : ", ver error");
            if (!j.ok && g_log) fprintf(g_log, "    MiniDumpWriteDump error %lu\n", j.err);
        } else if (g_log) fprintf(g_log, "    cierre.dmp: no se pudo crear (error %lu)\n", GetLastError());
    } else if (g_log) fprintf(g_log, "    cierre.dmp: dbghelp.dll no disponible\n");
    copyAll();
}

// ---------------------------------------------------------------- (08/11 e) placa de video reiniciada (TDR)
// Los 3 congelamientos del 08/11 (04:54, 06:08, 07:25 PC) fueron DXGI_ERROR_DEVICE_HUNG: el driver de NVIDIA reinicio la placa
// (evento nvlddmkm 153) y el juego quedo con la ultima imagen, sin poder recrear su dispositivo. En vez de dejarlo congelado se
// anota la causa, se copian los registros y se avisa con un cuadro; al aceptar se cierra el juego. Una sola vez por sesion.
static DWORD WINAPI DeviceLostThread(LPVOID p) {
    const unsigned reason = (unsigned)(uintptr_t)p;
    wchar_t msg[600];
    swprintf(msg, 600, L"PES21 FX: la placa de video se reinici\u00f3 (TDR, c\u00f3digo 0x%08X).\n\nWindows le da a la placa unos 2 segundos por dibujo; se pas\u00f3 y el driver la reinici\u00f3. El juego perdi\u00f3 lo que ten\u00eda cargado en la placa y no puede seguir.\n\nLos registros quedaron en PES21FX\\cierres. Aceptar cierra el juego.", reason);
    MessageBoxW(nullptr, msg, L"PES21 FX", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    if (g_log) { fprintf(g_log, "placa reiniciada: aviso aceptado, cerrando el juego\n"); fflush(g_log); }
    TerminateProcess(GetCurrentProcess(), 0x887A0006u);
    return 0;
}
void OnDeviceLost(unsigned reason) {
    static std::atomic<int> once{0};
    if (once++) return;
    Log("PLACA DE VIDEO REINICIADA (TDR): dispositivo perdido, motivo 0x%08x (0x887A0006 = GPU colgada, 0x887A0005 = quitado, 0x887A0007 = reset). El juego no puede seguir; aviso en pantalla.", reason);
    if (g_cfg.optCrashDump > 0 && !g_cfg.baseDir.empty()) WriteDumpTo(nullptr, true);
    HANDLE t = CreateThread(nullptr, 0, DeviceLostThread, (LPVOID)(uintptr_t)reason, 0, nullptr);
    if (t) CloseHandle(t);
}

static void WriteCrashDump(EXCEPTION_POINTERS* ep) {
    static std::atomic<int> once{0};
    if (once++ || g_cfg.optCrashDump <= 0 || g_cfg.baseDir.empty()) return;
    WriteDumpTo(ep, false);
}

// ---------------------------------------------------------------- (08/11 c) vigia de congelamientos
// Cuelgues del editor 07:54 / 09:08: el juego quedo congelado sin cerrarse y el volcado hubo que sacarlo a mano. Un hilo mira el
// numero de cuadro (Present): si no avanza en [PES21FX] congelado_seg segundos (0 = apagado) con la ventana del juego sin
// minimizar, anota en el log que hilos del juego estan vivos y escribe PES21FX\cierres\<fecha>_congelado\ (congelado.dmp con el
// nivel de crash_dump + logs + ini). Una vez por sesion; si el juego vuelve a dibujar se anota cuanto duro.
static BOOL CALLBACK FindGameWnd(HWND w, LPARAM lp) {
    DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(w) && !GetWindow(w, GW_OWNER)) { *(HWND*)lp = w; return FALSE; }
    return TRUE;
}
static DWORD WINAPI HangWatch(LPVOID) {
    int last = -1; ULONGLONG since = GetTickCount64(); bool dumped = false, reported = false;
    for (;;) {
        Sleep(1000);
        const int secs = g_cfg.optHangSec;
        const int n = PresentNo();
        const ULONGLONG now = GetTickCount64();
        if (n != last) {
            if (reported && last > 0) Log("vigia: el juego volvio a dibujar despues de %.1f s congelado", (now - since) / 1000.0);
            last = n; since = now; reported = false; continue;
        }
        if (secs <= 0 || n < 300) continue;   // apagado, o todavia arrancando (pantallas de carga)
        HWND w = nullptr; EnumWindows(FindGameWnd, (LPARAM)&w);
        if (!w || IsIconic(w)) { since = now; continue; }   // minimizado: no dibuja y esta bien
        if (reported || now - since < (ULONGLONG)secs * 1000) continue;
        reported = true;
        Log("CONGELADO: sin cuadros nuevos desde hace %d s (ultimo cuadro %d)%s", secs, n, dumped ? " - ya se guardo un volcado en esta sesion" : "");
        if (!dumped && g_cfg.optCrashDump > 0 && !g_cfg.baseDir.empty()) { dumped = true; WriteDumpTo(nullptr, true); Log("vigia: volcado del congelamiento listo: ya se puede cerrar el juego"); }
    }
}
void StartHangWatch() {
    if (HANDLE t = CreateThread(nullptr, 0, HangWatch, nullptr, 0, nullptr)) CloseHandle(t);
    Log("vigia de congelamientos: %d s sin cuadros -> volcado en PES21FX\\cierres\\<fecha>_congelado\\", g_cfg.optHangSec);
}

static LPTOP_LEVEL_EXCEPTION_FILTER s_prevFilter = nullptr;
static LONG WINAPI Unhandled(EXCEPTION_POINTERS* ep) {
    LogCrash(ep, "CIERRE DEL JUEGO");
    WriteCrashDump(ep);
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
    // (05/11) el log de la sesion anterior se conserva como PES21FX.anterior.log: si el juego se cerro y se volvio a abrir,
    // el informe del cierre no se pierde (cierre del 05/11 17:41: el log se piso al reabrir)
    MoveFileExW((wb + L"PES21FX.log").c_str(), (wb + L"PES21FX.anterior.log").c_str(), MOVEFILE_REPLACE_EXISTING);
    fx::g_log = _wfopen((wb + L"PES21FX.log").c_str(), L"w");
    fx::Log("PES21 FX 0.2 (d3d11.dll) - carpeta: %ls", wb.c_str());
    AddVectoredExceptionHandler(1, fx::Vectored);
    fx::s_prevFilter = SetUnhandledExceptionFilter(fx::Unhandled);
    if (HMODULE dh = LoadLibraryW(L"dbghelp.dll")) fx::s_mdwd = (fx::PFN_MDWD)GetProcAddress(dh, "MiniDumpWriteDump");
    fx::Log("volcado de cierres: %s", fx::s_mdwd ? "listo (PES21FX\\cierres\\)" : "dbghelp.dll no disponible");
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
    fx::StartHangWatch();   // (08/11 c) congelamientos: volcado automatico
    fx::PanelStart(fx::g_cfg.panelPort);   // (08/11) panel en vivo: HTTP en 127.0.0.1 (panel.cpp)
    fx::MemWatchStart();   // (26/10) liberador de memoria del juego + VirtualProtect/VirtualFree (memwatch.cpp)
    fx::RastroV4Start();   // (10/11 d) rastro v4: solo crea un hilo que espera Ctrl+Shift+F9 (rastro_v4.ini); no engancha nada al arrancar
    fx::CoverStart();   // (08/11 h) medidor de cobertura del codigo (cobertura = 1 en el ini)
    if (fx::g_cfg.coverOn) fx::Log("codigo: vigilancia del codigo apagada mientras se mide la cobertura");
    else if (fx::DiagOn(2)) fx::CodeWatchStart();
    else fx::Log("codigo: vigilancia del codigo del juego apagada (z173 = 0)");
    return 2;
}
void fx_EnsureInit() { InitMod(); }

// compatibilidad con el modulo de Sider viejo (ya no hace falta)
extern "C" __declspec(dllexport) int PES21FX_Init(const char*) { return InitMod(); }

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) { s_self = h; DisableThreadLibraryCalls(h); fx_LoadRealD3D11(); }
    if (reason == DLL_PROCESS_DETACH && fx::g_log) {   // el juego se esta cerrando "normalmente" (ExitProcess)
        fx::CoverFlush();   // (08/11 h) ultimo volcado de la cobertura
        fx::RastroFlush(true);
        fx::RastroV4Flush(true);   // (10/11 d)   // (08/11 i) rastro del motor
        fprintf(fx::g_log, "--- fin del proceso (%s)\n", reserved ? "el juego salio" : "DLL descargada");
        fflush(fx::g_log);
    }
    return TRUE;
}
