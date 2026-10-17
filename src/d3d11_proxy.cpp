// PES21 FX - somos el d3d11.dll de la carpeta del juego (el lugar que ocupaba 3DMigoto).
// Asi el mod queda cargado desde el arranque del proceso, antes que cualquier otro mod.
// Reenviamos todo al d3d11.dll de Windows; solo D3D11CreateDevice(AndSwapChain) pasan por nosotros.
#include "core.h"
#include <cstdio>

extern "C" void* fx_real[49] = {};
static const char* const kNames[49] = { "CreateDirect3D11DeviceFromDXGIDevice", "CreateDirect3D11SurfaceFromDXGISurface", "D3D11CoreCreateDevice", "D3D11CoreCreateLayeredDevice", "D3D11CoreGetLayeredDeviceSize", "D3D11CoreRegisterLayers", "D3D11CreateDeviceForD3D12", "D3D11On12CreateDevice", "D3DKMTCloseAdapter", "D3DKMTCreateAllocation", "D3DKMTCreateContext", "D3DKMTCreateDevice", "D3DKMTCreateSynchronizationObject", "D3DKMTDestroyAllocation", "D3DKMTDestroyContext", "D3DKMTDestroyDevice", "D3DKMTDestroySynchronizationObject", "D3DKMTEscape", "D3DKMTGetContextSchedulingPriority", "D3DKMTGetDeviceState", "D3DKMTGetDisplayModeList", "D3DKMTGetMultisampleMethodList", "D3DKMTGetRuntimeData", "D3DKMTGetSharedPrimaryHandle", "D3DKMTLock", "D3DKMTOpenAdapterFromHdc", "D3DKMTOpenResource", "D3DKMTPresent", "D3DKMTQueryAdapterInfo", "D3DKMTQueryAllocationResidency", "D3DKMTQueryResourceInfo", "D3DKMTRender", "D3DKMTSetAllocationPriority", "D3DKMTSetContextSchedulingPriority", "D3DKMTSetDisplayMode", "D3DKMTSetDisplayPrivateDriverFormat", "D3DKMTSetGammaRamp", "D3DKMTSetVidPnSourceOwner", "D3DKMTSignalSynchronizationObject", "D3DKMTUnlock", "D3DKMTWaitForSynchronizationObject", "D3DKMTWaitForVerticalBlankEvent", "D3DPerformance_BeginEvent", "D3DPerformance_EndEvent", "D3DPerformance_GetStatus", "D3DPerformance_SetMarker", "EnableFeatureLevelUpgrade", "OpenAdapter10", "OpenAdapter10_2" };

static HMODULE s_real = nullptr;
using PFN_CD = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using PFN_CDSC = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
static PFN_CD s_realCD = nullptr; static PFN_CDSC s_realCDSC = nullptr;

bool fx_LoadRealD3D11() {
    if (s_real) return true;
    wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring p = std::wstring(sys) + L"\\d3d11.dll";
    s_real = LoadLibraryW(p.c_str());
    if (!s_real) return false;
    for (int i = 0; i < 49; i++) fx_real[i] = (void*)GetProcAddress(s_real, kNames[i]);
    s_realCD = (PFN_CD)GetProcAddress(s_real, "D3D11CreateDevice");
    s_realCDSC = (PFN_CDSC)GetProcAddress(s_real, "D3D11CreateDeviceAndSwapChain");
    return s_realCD && s_realCDSC;
}
HMODULE fx_RealD3D11() { return s_real; }

void fx_EnsureInit();   // main.cpp: inicializa el mod la primera vez (fuera del loader lock)

extern "C" HRESULT WINAPI D3D11CreateDevice(IDXGIAdapter* a, D3D_DRIVER_TYPE t, HMODULE s, UINT f, const D3D_FEATURE_LEVEL* fl, UINT n, UINT v,
                                            ID3D11Device** d, D3D_FEATURE_LEVEL* ofl, ID3D11DeviceContext** c) {
    if (!fx_LoadRealD3D11()) return E_FAIL;
    fx_EnsureInit();
    HRESULT hr = s_realCD(a, t, s, f, fl, n, v, d, ofl, c);
    if (SUCCEEDED(hr) && d && *d) fx::OnDeviceCreated(*d);
    return hr;
}
extern "C" HRESULT WINAPI D3D11CreateDeviceAndSwapChain(IDXGIAdapter* a, D3D_DRIVER_TYPE t, HMODULE s, UINT f, const D3D_FEATURE_LEVEL* fl, UINT n, UINT v,
        const DXGI_SWAP_CHAIN_DESC* sd, IDXGISwapChain** sc, ID3D11Device** d, D3D_FEATURE_LEVEL* ofl, ID3D11DeviceContext** c) {
    if (!fx_LoadRealD3D11()) return E_FAIL;
    fx_EnsureInit();
    HRESULT hr = s_realCDSC(a, t, s, f, fl, n, v, sd, sc, d, ofl, c);
    if (SUCCEEDED(hr) && d && *d) fx::OnDeviceCreated(*d);
    return hr;
}
