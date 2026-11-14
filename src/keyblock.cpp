// PES21 FX (31/10) - BLOQUEAR LAS TECLAS DEL JUEGO MIENTRAS SE ESCRIBE EN EL MENU (pedido de Agustin)
// El menu (Insert) ya se tragaba los mensajes WM_KEY* cuando ImGui pedia el teclado, pero el juego no lee el teclado por
// mensajes: lo consulta directo (DirectInput8 y/o GetAsyncKeyState / GetKeyState / GetKeyboardState, y WM_INPUT si usa raw
// input). Al escribir en el buscador del menu (Ctrl+F) el juego recibia las mismas teclas y movia menus / jugadores.
// Aca se enganchan esas consultas: mientras g_blockGameKeys esta prendido le devuelven "ninguna tecla apretada" al juego.
// Las llamadas que salen de esta DLL (el menu, las teclas rapidas, ImGui) pasan siempre: se reconoce por la direccion de
// retorno. [PES21FX] bloquear_teclado: 0 = nunca, 1 = solo mientras se escribe en un campo de texto (por defecto),
// 2 = siempre que el menu esta abierto.
#include "core.h"
#include <MinHook.h>
#include <intrin.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#pragma intrinsic(_ReturnAddress)

namespace fx {

std::atomic<bool> g_blockGameKeys{false};
// (14/11, Agustin: "que al abrir el menu con l3+r3 por ejemplo se bloqueen los botones o el juego no los detecte") el MANDO tambien:
// XInputGetState (xinput1_3 que importa el juego, 1_4 y 9_1_0) devuelve el mando quieto y DirectInput (joystick: DualShock /
// DualSense sin Steam Input) devuelve los botones sueltos, la cruceta al centro y los ejes como estaban al abrir. El menu lee el
// mando desde esta DLL (menuvisual.cpp e ImGui): esas llamadas pasan siempre. [PES21FX] bloquear_mando 0 = nunca, 1 = con el menu abierto
std::atomic<bool> g_blockGamePad{false};
static uintptr_t s_selfB = 0, s_selfE = 0;
static std::atomic<int> s_hits[7];   // + 5 XInput, 6 DirectInput joystick   // 0 DirectInput estado, 1 DirectInput datos, 2 GetAsyncKeyState, 3 GetKeyState, 4 GetKeyboardState
static const char* const kHitName[7] = { "DirectInput (estado)", "DirectInput (buffer)", "GetAsyncKeyState", "GetKeyState", "GetKeyboardState", "XInput (mando)", "DirectInput (mando)" };

static bool FromSelf(void* ra) { const uintptr_t a = (uintptr_t)ra; return a >= s_selfB && a < s_selfE; }
static void Hit(int i) { if (s_hits[i].fetch_add(1) == 0) Log(i >= 5 ? "mando: el juego lee por %s (bloqueado con el menu abierto)" : "teclado: el juego lee por %s (bloqueado mientras escribis en el menu)", kHitName[i]); }

typedef SHORT(WINAPI* PFN_GAKS)(int);
typedef SHORT(WINAPI* PFN_GKS)(int);
typedef BOOL(WINAPI* PFN_GKBS)(PBYTE);
static PFN_GAKS o_GetAsyncKeyState = nullptr;
static PFN_GKS o_GetKeyState = nullptr;
static PFN_GKBS o_GetKeyboardState = nullptr;

static SHORT WINAPI hk_GetAsyncKeyState(int vk) {
    if (g_blockGameKeys.load(std::memory_order_relaxed) && !FromSelf(_ReturnAddress()) && vk != g_cfg.menuKey) {
        o_GetAsyncKeyState(vk);   // consume el bit de "se apreto desde la ultima consulta"
        Hit(2); return 0;
    }
    return o_GetAsyncKeyState(vk);
}
static SHORT WINAPI hk_GetKeyState(int vk) {
    if (g_blockGameKeys.load(std::memory_order_relaxed) && !FromSelf(_ReturnAddress()) && vk != g_cfg.menuKey) { Hit(3); return 0; }
    return o_GetKeyState(vk);
}
static BOOL WINAPI hk_GetKeyboardState(PBYTE k) {
    const BOOL r = o_GetKeyboardState(k);
    if (r && k && g_blockGameKeys.load(std::memory_order_relaxed) && !FromSelf(_ReturnAddress())) { memset(k, 0, 256); Hit(4); }
    return r;
}

// DirectInput8: las vtables de IDirectInputDevice8A/W son compartidas por todos los dispositivos de la clase, asi que se
// enganchan creando un teclado propio (no hace falta que el juego ya haya creado el suyo)
typedef HRESULT(STDMETHODCALLTYPE* PFN_GDS)(IDirectInputDevice8W*, DWORD, LPVOID);
typedef HRESULT(STDMETHODCALLTYPE* PFN_GDD)(IDirectInputDevice8W*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
static PFN_GDS o_GDS[2] = {}; static PFN_GDD o_GDD[2] = {};

static int DevType(IDirectInputDevice8W* d) {
    // el tipo no cambia: se pregunta una vez por dispositivo (DIDEVCAPS es igual en A y W)
    static std::mutex m; static std::vector<std::pair<void*, int>> known;
    std::lock_guard<std::mutex> lk(m);
    for (auto& k : known) if (k.first == d) return k.second;
    DIDEVCAPS caps{}; caps.dwSize = sizeof(caps);
    const int t = SUCCEEDED(d->GetCapabilities(&caps)) ? (int)GET_DIDEVICE_TYPE(caps.dwDevType) : 0;
    if (known.size() < 64) known.push_back({ d, t });
    return t;
}
static bool IsKeyboard(IDirectInputDevice8W* d) { return DevType(d) == DI8DEVTYPE_KEYBOARD; }
static bool IsPad(IDirectInputDevice8W* d) { const int t = DevType(d); return t != 0 && t != DI8DEVTYPE_KEYBOARD && t != DI8DEVTYPE_MOUSE && t != DI8DEVTYPE_DEVICE; }
// ultimo estado del joystick que vio el juego sin bloquear: los ejes quedan como estaban (el centro de cada eje depende del mando)
static std::mutex s_joyMx; static std::vector<std::pair<void*, DIJOYSTATE2>> s_joyLast;
static void JoyBlock(IDirectInputDevice8W* d, DWORD cb, void* data) {
    std::lock_guard<std::mutex> lk(s_joyMx);
    DIJOYSTATE2* last = nullptr; for (auto& j : s_joyLast) if (j.first == d) { last = &j.second; break; }
    if (!g_blockGamePad.load(std::memory_order_relaxed)) {   // sin bloquear: se guarda
        if (!last) { if (s_joyLast.size() >= 16) return; s_joyLast.push_back({ d, DIJOYSTATE2{} }); last = &s_joyLast.back().second; }
        memcpy(last, data, std::min<size_t>(cb, sizeof(DIJOYSTATE2))); return;
    }
    DIJOYSTATE* js = (DIJOYSTATE*)data;
    if (last) memcpy(js, last, offsetof(DIJOYSTATE, rgdwPOV)); else memset(js, 0, offsetof(DIJOYSTATE, rgdwPOV));   // ejes
    for (int i = 0; i < 4; i++) js->rgdwPOV[i] = 0xFFFFFFFF;                                                           // cruceta al centro
    memset(js->rgbButtons, 0, sizeof(js->rgbButtons));
    if (cb == sizeof(DIJOYSTATE2)) { DIJOYSTATE2* j2 = (DIJOYSTATE2*)data; memset(j2->rgbButtons, 0, sizeof(j2->rgbButtons)); }
    Hit(6);
}
template<int W> static HRESULT STDMETHODCALLTYPE hk_GDS(IDirectInputDevice8W* d, DWORD cb, LPVOID data) {
    const HRESULT hr = o_GDS[W](d, cb, data);
    if (SUCCEEDED(hr) && data && cb == 256 && g_blockGameKeys.load(std::memory_order_relaxed) && !FromSelf(_ReturnAddress())) { memset(data, 0, cb); Hit(0); }
    else if (SUCCEEDED(hr) && data && (cb == sizeof(DIJOYSTATE) || cb == sizeof(DIJOYSTATE2)) && !FromSelf(_ReturnAddress()) && IsPad(d)) JoyBlock(d, cb, data);
    return hr;
}
template<int W> static HRESULT STDMETHODCALLTYPE hk_GDD(IDirectInputDevice8W* d, DWORD cb, LPDIDEVICEOBJECTDATA od, LPDWORD n, DWORD fl) {
    const HRESULT hr = o_GDD[W](d, cb, od, n, fl);   // se vacia el buffer igual: al cerrar el menu no llegan las teclas viejas
    if (SUCCEEDED(hr) && n && g_blockGameKeys.load(std::memory_order_relaxed) && !FromSelf(_ReturnAddress()) && IsKeyboard(d)) { *n = 0; Hit(1); }
    else if (SUCCEEDED(hr) && n && *n && g_blockGamePad.load(std::memory_order_relaxed) && !FromSelf(_ReturnAddress()) && IsPad(d)) { *n = 0; Hit(6); }
    return hr;
}

static void HookFn(void* target, void* detour, void** orig, const char* name) {
    if (!target) return;
    const MH_STATUS s = MH_CreateHook(target, detour, orig);
    if (s == MH_OK) MH_EnableHook(target);
    else if (s != MH_ERROR_ALREADY_CREATED) Log("teclado: no se pudo enganchar %s (%d)", name, (int)s);
}

static void HookDirectInput() {
    HMODULE di = LoadLibraryW(L"dinput8.dll");
    typedef HRESULT(WINAPI* PFN_DI8C)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    PFN_DI8C create = di ? (PFN_DI8C)GetProcAddress(di, "DirectInput8Create") : nullptr;
    if (!create) { Log("teclado: dinput8.dll no disponible"); return; }
    for (int w = 0; w < 2; w++) {
        IUnknown* dinput = nullptr;
        if (FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, w ? IID_IDirectInput8W : IID_IDirectInput8A, (void**)&dinput, nullptr)) || !dinput) continue;
        IUnknown* dev = nullptr;
        HRESULT hr = w ? ((IDirectInput8W*)dinput)->CreateDevice(GUID_SysKeyboard, (IDirectInputDevice8W**)&dev, nullptr)
                       : ((IDirectInput8A*)dinput)->CreateDevice(GUID_SysKeyboard, (IDirectInputDevice8A**)&dev, nullptr);
        if (SUCCEEDED(hr) && dev) {
            void** vt = *(void***)dev;   // 9 = GetDeviceState, 10 = GetDeviceData
            if (w) { HookFn(vt[9], (void*)&hk_GDS<1>, (void**)&o_GDS[1], "GetDeviceState W"); HookFn(vt[10], (void*)&hk_GDD<1>, (void**)&o_GDD[1], "GetDeviceData W"); }
            else   { HookFn(vt[9], (void*)&hk_GDS<0>, (void**)&o_GDS[0], "GetDeviceState A"); HookFn(vt[10], (void*)&hk_GDD<0>, (void**)&o_GDD[0], "GetDeviceData A"); }
            dev->Release();
        }
        dinput->Release();
    }
    // A y W pueden compartir la misma funcion: entonces queda enganchada una vez (detour del A) y el W no se usa
    if (!o_GDS[1]) o_GDS[1] = o_GDS[0];
    if (!o_GDD[1]) o_GDD[1] = o_GDD[0];
}

// XInput: estado del mando quieto (conectado) con el numero de paquete propio, que cambia una vez al bloquear y otra al soltar
struct XGamepadB { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; };
struct XStateB { DWORD dwPacketNumber; XGamepadB Gamepad; };
typedef DWORD(WINAPI* PFN_XGS)(DWORD, XStateB*);
static PFN_XGS o_XGS[4] = {};
template<int I> static DWORD WINAPI hk_XGS(DWORD user, XStateB* st) {
    const DWORD r = o_XGS[I](user, st);
    if (r == 0 && st && g_blockGamePad.load(std::memory_order_relaxed) && !FromSelf(_ReturnAddress())) {
        const DWORD pk = st->dwPacketNumber; memset(st, 0, sizeof(*st)); st->dwPacketNumber = pk ^ 0x80000000u; Hit(5);
    }
    return r;
}
static void HookXInput() {
    const wchar_t* dlls[3] = { L"xinput1_3.dll", L"xinput1_4.dll", L"xinput9_1_0.dll" };
    void* seen[4] = {}; int n = 0;
    for (int i = 0; i < 3; i++) {
        HMODULE m = GetModuleHandleW(dlls[i]); if (!m && i == 0) m = LoadLibraryW(dlls[i]);   // la del juego (importada): se carga igual
        if (!m) continue;
        for (int ord = 0; ord < 2 && n < 4; ord++) {   // XInputGetState y XInputGetStateEx (ordinal 100: con el boton guia)
            void* f = ord == 0 ? (void*)GetProcAddress(m, "XInputGetState") : (void*)GetProcAddress(m, (LPCSTR)100);
            if (!f) continue; bool dup = false; for (int j = 0; j < n; j++) dup |= seen[j] == f; if (dup) continue;
            seen[n] = f;
            static void* const det[4] = { (void*)&hk_XGS<0>, (void*)&hk_XGS<1>, (void*)&hk_XGS<2>, (void*)&hk_XGS<3> };
            HookFn(f, det[n], (void**)&o_XGS[n], "XInputGetState"); n++;
        }
    }
    Log("mando: %d funciones de XInput enganchadas (el juego no ve el mando con el menu abierto, bloquear_mando = %d)", n, g_cfg.blockPad);
}

// llamado una vez despues de MH_Initialize (InstallHooks)
void KeyBlockInstall() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&KeyBlockInstall, &self);
    if (self) {
        const auto* dos = (const IMAGE_DOS_HEADER*)self;
        const auto* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)self + dos->e_lfanew);
        s_selfB = (uintptr_t)self; s_selfE = s_selfB + nt->OptionalHeader.SizeOfImage;
    }
    HMODULE u = GetModuleHandleW(L"user32.dll"); if (!u) u = LoadLibraryW(L"user32.dll");
    if (u) {
        HookFn((void*)GetProcAddress(u, "GetAsyncKeyState"), (void*)&hk_GetAsyncKeyState, (void**)&o_GetAsyncKeyState, "GetAsyncKeyState");
        HookFn((void*)GetProcAddress(u, "GetKeyState"), (void*)&hk_GetKeyState, (void**)&o_GetKeyState, "GetKeyState");
        HookFn((void*)GetProcAddress(u, "GetKeyboardState"), (void*)&hk_GetKeyboardState, (void**)&o_GetKeyboardState, "GetKeyboardState");
    }
    HookDirectInput();
    HookXInput();
    Log("teclado: bloqueo de teclas del juego listo (bloquear_teclado = %d: 0 nunca, 1 al escribir, 2 con el menu abierto)", g_cfg.blockKeys);
}

} // namespace fx
