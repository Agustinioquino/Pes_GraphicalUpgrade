; PES21 FX - reenvio de las exportaciones de d3d11.dll que no nos interesan al d3d11.dll de Windows.
; La tabla fx_real[] la llena d3d11_proxy.cpp al cargar (GetProcAddress sobre System32\d3d11.dll).
EXTERN fx_real:QWORD
.code
CreateDirect3D11DeviceFromDXGIDevice PROC
    jmp QWORD PTR [fx_real + 0]
CreateDirect3D11DeviceFromDXGIDevice ENDP
CreateDirect3D11SurfaceFromDXGISurface PROC
    jmp QWORD PTR [fx_real + 8]
CreateDirect3D11SurfaceFromDXGISurface ENDP
D3D11CoreCreateDevice PROC
    jmp QWORD PTR [fx_real + 16]
D3D11CoreCreateDevice ENDP
D3D11CoreCreateLayeredDevice PROC
    jmp QWORD PTR [fx_real + 24]
D3D11CoreCreateLayeredDevice ENDP
D3D11CoreGetLayeredDeviceSize PROC
    jmp QWORD PTR [fx_real + 32]
D3D11CoreGetLayeredDeviceSize ENDP
D3D11CoreRegisterLayers PROC
    jmp QWORD PTR [fx_real + 40]
D3D11CoreRegisterLayers ENDP
D3D11CreateDeviceForD3D12 PROC
    jmp QWORD PTR [fx_real + 48]
D3D11CreateDeviceForD3D12 ENDP
D3D11On12CreateDevice PROC
    jmp QWORD PTR [fx_real + 56]
D3D11On12CreateDevice ENDP
D3DKMTCloseAdapter PROC
    jmp QWORD PTR [fx_real + 64]
D3DKMTCloseAdapter ENDP
D3DKMTCreateAllocation PROC
    jmp QWORD PTR [fx_real + 72]
D3DKMTCreateAllocation ENDP
D3DKMTCreateContext PROC
    jmp QWORD PTR [fx_real + 80]
D3DKMTCreateContext ENDP
D3DKMTCreateDevice PROC
    jmp QWORD PTR [fx_real + 88]
D3DKMTCreateDevice ENDP
D3DKMTCreateSynchronizationObject PROC
    jmp QWORD PTR [fx_real + 96]
D3DKMTCreateSynchronizationObject ENDP
D3DKMTDestroyAllocation PROC
    jmp QWORD PTR [fx_real + 104]
D3DKMTDestroyAllocation ENDP
D3DKMTDestroyContext PROC
    jmp QWORD PTR [fx_real + 112]
D3DKMTDestroyContext ENDP
D3DKMTDestroyDevice PROC
    jmp QWORD PTR [fx_real + 120]
D3DKMTDestroyDevice ENDP
D3DKMTDestroySynchronizationObject PROC
    jmp QWORD PTR [fx_real + 128]
D3DKMTDestroySynchronizationObject ENDP
D3DKMTEscape PROC
    jmp QWORD PTR [fx_real + 136]
D3DKMTEscape ENDP
D3DKMTGetContextSchedulingPriority PROC
    jmp QWORD PTR [fx_real + 144]
D3DKMTGetContextSchedulingPriority ENDP
D3DKMTGetDeviceState PROC
    jmp QWORD PTR [fx_real + 152]
D3DKMTGetDeviceState ENDP
D3DKMTGetDisplayModeList PROC
    jmp QWORD PTR [fx_real + 160]
D3DKMTGetDisplayModeList ENDP
D3DKMTGetMultisampleMethodList PROC
    jmp QWORD PTR [fx_real + 168]
D3DKMTGetMultisampleMethodList ENDP
D3DKMTGetRuntimeData PROC
    jmp QWORD PTR [fx_real + 176]
D3DKMTGetRuntimeData ENDP
D3DKMTGetSharedPrimaryHandle PROC
    jmp QWORD PTR [fx_real + 184]
D3DKMTGetSharedPrimaryHandle ENDP
D3DKMTLock PROC
    jmp QWORD PTR [fx_real + 192]
D3DKMTLock ENDP
D3DKMTOpenAdapterFromHdc PROC
    jmp QWORD PTR [fx_real + 200]
D3DKMTOpenAdapterFromHdc ENDP
D3DKMTOpenResource PROC
    jmp QWORD PTR [fx_real + 208]
D3DKMTOpenResource ENDP
D3DKMTPresent PROC
    jmp QWORD PTR [fx_real + 216]
D3DKMTPresent ENDP
D3DKMTQueryAdapterInfo PROC
    jmp QWORD PTR [fx_real + 224]
D3DKMTQueryAdapterInfo ENDP
D3DKMTQueryAllocationResidency PROC
    jmp QWORD PTR [fx_real + 232]
D3DKMTQueryAllocationResidency ENDP
D3DKMTQueryResourceInfo PROC
    jmp QWORD PTR [fx_real + 240]
D3DKMTQueryResourceInfo ENDP
D3DKMTRender PROC
    jmp QWORD PTR [fx_real + 248]
D3DKMTRender ENDP
D3DKMTSetAllocationPriority PROC
    jmp QWORD PTR [fx_real + 256]
D3DKMTSetAllocationPriority ENDP
D3DKMTSetContextSchedulingPriority PROC
    jmp QWORD PTR [fx_real + 264]
D3DKMTSetContextSchedulingPriority ENDP
D3DKMTSetDisplayMode PROC
    jmp QWORD PTR [fx_real + 272]
D3DKMTSetDisplayMode ENDP
D3DKMTSetDisplayPrivateDriverFormat PROC
    jmp QWORD PTR [fx_real + 280]
D3DKMTSetDisplayPrivateDriverFormat ENDP
D3DKMTSetGammaRamp PROC
    jmp QWORD PTR [fx_real + 288]
D3DKMTSetGammaRamp ENDP
D3DKMTSetVidPnSourceOwner PROC
    jmp QWORD PTR [fx_real + 296]
D3DKMTSetVidPnSourceOwner ENDP
D3DKMTSignalSynchronizationObject PROC
    jmp QWORD PTR [fx_real + 304]
D3DKMTSignalSynchronizationObject ENDP
D3DKMTUnlock PROC
    jmp QWORD PTR [fx_real + 312]
D3DKMTUnlock ENDP
D3DKMTWaitForSynchronizationObject PROC
    jmp QWORD PTR [fx_real + 320]
D3DKMTWaitForSynchronizationObject ENDP
D3DKMTWaitForVerticalBlankEvent PROC
    jmp QWORD PTR [fx_real + 328]
D3DKMTWaitForVerticalBlankEvent ENDP
D3DPerformance_BeginEvent PROC
    jmp QWORD PTR [fx_real + 336]
D3DPerformance_BeginEvent ENDP
D3DPerformance_EndEvent PROC
    jmp QWORD PTR [fx_real + 344]
D3DPerformance_EndEvent ENDP
D3DPerformance_GetStatus PROC
    jmp QWORD PTR [fx_real + 352]
D3DPerformance_GetStatus ENDP
D3DPerformance_SetMarker PROC
    jmp QWORD PTR [fx_real + 360]
D3DPerformance_SetMarker ENDP
EnableFeatureLevelUpgrade PROC
    jmp QWORD PTR [fx_real + 368]
EnableFeatureLevelUpgrade ENDP
OpenAdapter10 PROC
    jmp QWORD PTR [fx_real + 376]
OpenAdapter10 ENDP
OpenAdapter10_2 PROC
    jmp QWORD PTR [fx_real + 384]
OpenAdapter10_2 ENDP
END
