// PES21 FX - punto de entrada. Sider llama a PES21FX_Init(carpeta) desde PES21FX.lua (LuaJIT FFI).
#include "core.h"
#include <cstdarg>
#include <cstdio>

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
    fx::Log("PES21 FX 0.1 - carpeta: %s", b.c_str());
    if (!fx::LoadConfig()) fx::Log("aviso: no se pudo leer la configuracion");
    if (!fx::InstallHooks()) { fx::Log("ERROR: no se pudieron instalar los ganchos de D3D11"); return 0; }
    fx::Log("ganchos instalados");
    return 2;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
