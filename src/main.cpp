// PES21 FX - punto de entrada. Sider llama a PES21FX_Init(carpeta) desde PES21FX.lua (LuaJIT FFI).
#include "core.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

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
static std::string ModOff(void* p) {
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
    void* fr[40]; USHORT k = RtlCaptureStackBackTrace(0, 40, fr, nullptr);
    for (USHORT i = 0; i < k; i++) fprintf(g_log, "    %s\n", ModOff(fr[i]).c_str());
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
static LPTOP_LEVEL_EXCEPTION_FILTER s_prevFilter = nullptr;
static LONG WINAPI Unhandled(EXCEPTION_POINTERS* ep) {
    LogCrash(ep, "CIERRE DEL JUEGO");
    return s_prevFilter ? s_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

} // namespace fx

static std::atomic<bool> s_inited{false};

extern "C" __declspec(dllexport) int PES21FX_Init(const char* baseDir) {
    if (s_inited.exchange(true)) return 1;
    // carpeta del mod (UTF-8 -> UTF-16)
    std::string b = baseDir ? baseDir : "";
    if (!b.empty() && b.back() != '\\' && b.back() != '/') b += '\\';
    int n = MultiByteToWideChar(CP_UTF8, 0, b.c_str(), -1, nullptr, 0);
    std::wstring wb(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, b.c_str(), -1, wb.data(), n);
    fx::g_cfg.baseDir = wb;
    fx::g_log = _wfopen((wb + L"PES21FX.log").c_str(), L"w");
    fx::Log("PES21 FX 0.1.2 - carpeta: %s", b.c_str());
    AddVectoredExceptionHandler(1, fx::Vectored);
    fx::s_prevFilter = SetUnhandledExceptionFilter(fx::Unhandled);
    if (!fx::LoadConfig()) fx::Log("aviso: no se pudo leer la configuracion");
    if (!fx::InstallHooks()) { fx::Log("ERROR: no se pudieron instalar los ganchos de D3D11"); return 0; }
    fx::Log("ganchos instalados");
    return 2;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
