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
static uintptr_t s_selfB = 0, s_selfE = 0;
static std::atomic<int> s_hits[5];   // 0 DirectInput estado, 1 DirectInput datos, 2 GetAsyncKeyState, 3 GetKeyState, 4 GetKeyboardState
static const char* const kHitName[5] = { "DirectInput (estado)", "DirectInput (buffer)", "GetAsyncKeyState", "GetKeyState", "GetKeyboardState" };

static bool FromSelf(void* ra) { const uintptr_t a = (uintptr_t)ra; return a >= s_selfB && a < s_selfE; }
static void Hit(int i) { if (s_hits[i].fetch_add(1) == 0) Log("teclado: el juego lee por %s (bloqueado mientras escribis en el menu)", kHitName[i]); }

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

static bool IsKeyboard(IDirectInputDevice8W* d) {
    // el tipo no cambia: se pregunta una vez por dispositivo (DIDEVCAPS es igual en A y W)
    static std::mutex m; static std::vector<std::pair<void*, bool>> known;
    std::lock_guard<std::mutex> lk(m);
    for (auto& k : known) if (k.first == d) return k.second;
    DIDEVCAPS caps{}; caps.dwSize = sizeof(caps);
    const bool kb = SUCCEEDED(d->GetCapabilities(&caps)) && GET_DIDEVICE_TYPE(caps.dwDevType) == DI8DEVTYPE_KEYBOARD;
    if (known.size() < 64) known.push_back({ d, kb });
    return kb;
}
template<int W> static HRESULT STDMETHODCALLTYPE hk_GDS(IDirectInputDevice8W* d, DWORD cb, LPVOID data) {
    const HRESULT hr = o_GDS[W](d, cb, data);
    if (SUCCEEDED(hr) && data && cb == 256 && g_blockGameKeys.load(std::memory_order_relaxed) && !FromSelf(_ReturnAddress())) { memset(data, 0, cb); Hit(0); }
    return hr;
}
template<int W> static HRESULT STDMETHODCALLTYPE hk_GDD(IDirectInputDevice8W* d, DWORD cb, LPDIDEVICEOBJECTDATA od, LPDWORD n, DWORD fl) {
    const HRESULT hr = o_GDD[W](d, cb, od, n, fl);   // se vacia el buffer igual: al cerrar el menu no llegan las teclas viejas
    if (SUCCEEDED(hr) && n && g_blockGameKeys.load(std::memory_order_relaxed) && !FromSelf(_ReturnAddress()) && IsKeyboard(d)) { *n = 0; Hit(1); }
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
    Log("teclado: bloqueo de teclas del juego listo (bloquear_teclado = %d: 0 nunca, 1 al escribir, 2 con el menu abierto)", g_cfg.blockKeys);
}

} // namespace fx
