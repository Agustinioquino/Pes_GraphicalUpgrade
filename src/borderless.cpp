// (15/11) Ventana sin bordes a pantalla completa ("ventana en pantalla completa").
// [PES21FX] ventana_sin_bordes = 1 (de fabrica 0). Solo actua si el juego esta en modo Ventana (settings.exe); vale desde el proximo arranque.
// Como arma el juego su ventana (decompilado/es/15_sistema_y_memoria.md ficha B4, detalle/settings_opciones.md 1.4):
//   App_InitFromSettings 0x140404da0: en modo ventana llama AdjustWindowRectEx(estilo 0xca0000) y, si la ventana CON bordes no entra en
//     el modo mas grande de la pantalla, achica la resolucion (1920x1080 en un monitor 1920x1080 queda en ~1850x1041).
//   CreateMainWindow 0x140405920: crea la ventana con estilo 0xca0000 (titulo + menu de sistema + minimizar).
// El mod arranca antes que las dos (la prueba de D3D11CreateDevice de 0x14040d870 se hace dentro de 0x14040b040, primera linea util de
// 0x140404da0), asi que alcanza con dos ganchos de user32, sin tocar el exe:
//   AdjustWindowRectEx: para un estilo con titulo devuelve el rectangulo sin agrandar -> el juego no achica la resolucion.
//   CreateWindowExW / A: la primera ventana principal con titulo sale sin bordes (WS_POPUP) y ocupando el monitor entero.
// Respaldo: al crear la cadena de intercambio se revisa la ventana y, si quedo con bordes o en otro lugar, se corrige (BorderlessEnsure).
// La imagen se dibuja a la resolucion elegida en settings.exe; si es menor que la del monitor Windows la estira.
#include "core.h"
#include <MinHook.h>
#include <atomic>

namespace fx {

using PFN_Adj = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD);
using PFN_CwW = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using PFN_CwA = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static PFN_Adj o_adj = nullptr; static PFN_CwW o_cwW = nullptr; static PFN_CwA o_cwA = nullptr;
static std::atomic<HWND> s_wnd{nullptr};
static std::atomic<int> s_adjN{0};
static bool s_on = false;

static const DWORD kBorders = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
static const DWORD kExBorders = WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE;

static RECT MonitorRect(HWND w) {
    HMONITOR m = w ? MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY) : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{}; mi.cbSize = sizeof mi;
    if (m && GetMonitorInfoW(m, &mi)) return mi.rcMonitor;
    return RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}
static bool IsMainStyle(DWORD style, HWND parent) { return !parent && !(style & WS_CHILD) && (style & WS_CAPTION) == WS_CAPTION; }

static BOOL WINAPI hk_Adj(LPRECT r, DWORD style, BOOL menu, DWORD ex) {
    if (s_on && r && !menu && (style & WS_CAPTION) == WS_CAPTION && !(style & WS_CHILD)) { s_adjN++; return TRUE; }   // sin bordes: ventana = area de dibujo
    return o_adj(r, style, menu, ex);
}
template <class F, class S> static HWND Create(F orig, DWORD ex, S cls, S name, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID p) {
    if (!s_on || s_wnd.load() || !IsMainStyle(style, parent) || w < 512 || h < 512) return orig(ex, cls, name, style, x, y, w, h, parent, menu, inst, p);
    const RECT m = MonitorRect(nullptr);
    const DWORD st = (style & ~kBorders) | WS_POPUP;
    HWND hw = orig(ex & ~kExBorders, cls, name, st, m.left, m.top, m.right - m.left, m.bottom - m.top, parent, menu, inst, p);
    if (hw) { s_wnd = hw; Log("ventana sin bordes: ventana principal creada sin bordes en %ld,%ld de %ldx%ld (el juego pedia %dx%d con estilo %08lX)", m.left, m.top, m.right - m.left, m.bottom - m.top, w, h, style); }
    return hw;
}
static HWND WINAPI hk_CwW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID p) {
    return Create(o_cwW, ex, cls, name, style, x, y, w, h, parent, menu, inst, p);
}
static HWND WINAPI hk_CwA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID p) {
    return Create(o_cwA, ex, cls, name, style, x, y, w, h, parent, menu, inst, p);
}

// llamado una vez desde InitMod, despues de MH_Initialize (InstallHooks): antes de que el juego lea el tamano y cree la ventana
void BorderlessStart() {
    s_on = g_cfg.borderless != 0;
    if (!s_on) { Log("ventana sin bordes: apagada (ventana_sin_bordes = 0)"); return; }
    HMODULE u = GetModuleHandleW(L"user32.dll");
    void* a = u ? (void*)GetProcAddress(u, "AdjustWindowRectEx") : nullptr;
    void* cw = u ? (void*)GetProcAddress(u, "CreateWindowExW") : nullptr;
    void* ca = u ? (void*)GetProcAddress(u, "CreateWindowExA") : nullptr;
    bool ok = a && cw && ca
        && MH_CreateHook(a, (void*)&hk_Adj, (void**)&o_adj) == MH_OK && MH_EnableHook(a) == MH_OK
        && MH_CreateHook(cw, (void*)&hk_CwW, (void**)&o_cwW) == MH_OK && MH_EnableHook(cw) == MH_OK
        && MH_CreateHook(ca, (void*)&hk_CwA, (void**)&o_cwA) == MH_OK && MH_EnableHook(ca) == MH_OK;
    if (!ok) { Log("ventana sin bordes: NO se pudieron poner los ganchos de user32 (queda solo el arreglo al crear la imagen)"); return; }
    Log("ventana sin bordes: prendida (ganchos de user32 puestos antes de crear la ventana)");
}

// llamado al crear la cadena de intercambio (hooks.cpp OnSwapChain): revisa que la ventana haya quedado sin bordes y en todo el monitor
void BorderlessEnsure(HWND w, bool windowed, unsigned bw, unsigned bh) {
    if (!g_cfg.borderless) return;
    if (!w || !IsWindow(w)) { Log("ventana sin bordes: no hay ventana para revisar"); return; }
    if (!windowed) { Log("ventana sin bordes: el juego esta en Pantalla completa; para usarla hay que elegir Ventana en settings.exe"); return; }
    const RECT m = MonitorRect(w);
    RECT r{}; GetWindowRect(w, &r);
    const LONG_PTR st = GetWindowLongPtrW(w, GWL_STYLE);
    const bool styleOk = !(st & WS_CAPTION), posOk = r.left == m.left && r.top == m.top && r.right == m.right && r.bottom == m.bottom;
    if (!styleOk || !posOk) {
        SetWindowLongPtrW(w, GWL_STYLE, (st & ~(LONG_PTR)kBorders) | WS_POPUP | WS_VISIBLE);
        SetWindowLongPtrW(w, GWL_EXSTYLE, GetWindowLongPtrW(w, GWL_EXSTYLE) & ~(LONG_PTR)kExBorders);
        SetWindowPos(w, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        Log("ventana sin bordes: corregida al crear la imagen (tenia bordes: %s, estaba en %ld,%ld %ldx%ld)", styleOk ? "no" : "si", r.left, r.top, r.right - r.left, r.bottom - r.top);
    }
    const unsigned mw = (unsigned)(m.right - m.left), mh = (unsigned)(m.bottom - m.top);
    Log("ventana sin bordes: imagen %ux%u en un monitor de %ux%u%s (AdjustWindowRectEx atendida %d veces)", bw, bh, mw, mh,
        bw == mw && bh == mh ? " - nitida, punto por punto" : " - Windows la estira: para que sea nitida elegir la resolucion del monitor en settings.exe", s_adjN.load());
}

} // namespace fx
