// PES21 FX - parches EN MEMORIA del motor (20/10). El exe en disco nunca se toca: con el volcado del exe desempaquetado
// (exedump.cpp) se ubicaron las funciones que leen la configuracion grafica desde los scripts Lua del juego (Fox Engine:
// GrPluginPesShadow.ShadowResolution, GrShadowSettings.DirectionalLightShadowResolution / CascadeShadowRangeScale, ...).
// Se enganchan con MinHook: primero se registra en el log lo que el juego usa y, si el usuario lo pide en PES21FX.ini
// ([Motor]), se cambian los valores recien leidos antes de que el motor los use (la textura de sombras se crea despues).
// Todo esta protegido por la version del exe (sello de tiempo 62787C6A) y la firma de bytes de cada funcion: si no
// coinciden no se engancha nada.
#include "core.h"
#include <MinHook.h>
#include <cstring>

namespace fx {

static const unsigned kExeStamp = 0x62787C6A;
// cada lector: prologo [mov [rsp+10],rbx ; push rdi ; sub rsp,20 ; mov rdi,rdx ; mov rbx,rcx] (16 bytes)
static const unsigned char kReaderSig[16] = { 0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xfa,0x48,0x8b,0xd9 };
// cada busqueda en el mapa de configuraciones: [mov r11,rsp ; push rdi ; sub rsp,40 ; mov qword [r11-18],-2]
static const unsigned char kLookupSig[8] = { 0x4c,0x8b,0xdc,0x57,0x48,0x83,0xec,0x40 };

typedef int  (__fastcall *ReaderFn)(void* self, void* L, void* a3, void* key);
typedef void*(__fastcall *LookupFn)(void* map, void* key);

struct Patch { const char* name; uintptr_t reader, lookup; ReaderFn oReader; LookupFn oLookup; int kind; };
static Patch s_p[3] = {
    { "GrPluginPesShadow.ShadowResolution",  0x1416b9a10, 0x1416b4b50, nullptr, nullptr, 0 },
    { "GrShadowSettings",                   0x1416c70c0, 0x1416bfeb0, nullptr, nullptr, 1 },
    { "GrPluginPlanarShadowCombiner.ShadowQuality", 0x1416bdcc0 - 0x10, 0x1416bb1e0, nullptr, nullptr, 2 },
};
static thread_local int t_capIdx = -1; static thread_local void* t_capPtr = nullptr;

template<int I> static void* __fastcall hk_Lookup(void* map, void* key) {
    void* r = s_p[I].oLookup(map, key);
    if (t_capIdx == I) t_capPtr = r;
    return r;
}
static void Apply(int i, void* p) {
    if (!p) { Log("motor: %s leido pero no se pudo ubicar la estructura", s_p[i].name); return; }
    int* v = (int*)p; float* f = (float*)p;
    if (i == 0) {
        Log("motor: %s = %d", s_p[i].name, v[0]);
        if (g_cfg.motorShadowRes > 0 && v[0] > 0 && v[0] != g_cfg.motorShadowRes) { Log("motor:   -> %d (PES21FX.ini [Motor] sombra_resolucion)", g_cfg.motorShadowRes); v[0] = g_cfg.motorShadowRes; }
    } else if (i == 1) {
        Log("motor: %s: direccional %d, spot %d, point %d, escala de cascadas %.3f, mezcla de cascadas %.3f", s_p[i].name, v[0], v[1], v[2], f[3], f[4]);
        if (g_cfg.motorShadowRes > 0 && v[0] > 0 && v[0] != g_cfg.motorShadowRes) { Log("motor:   direccional -> %d", g_cfg.motorShadowRes); v[0] = g_cfg.motorShadowRes; }
        if (g_cfg.motorCascadeScale > 0.0f && f[3] > 0.0f) { Log("motor:   escala de cascadas -> %.3f", g_cfg.motorCascadeScale); f[3] = g_cfg.motorCascadeScale; }
    } else {
        Log("motor: %s = %d", s_p[i].name, v[0]);
    }
}
template<int I> static int __fastcall hk_Reader(void* self, void* L, void* a3, void* key) {
    t_capIdx = I; t_capPtr = nullptr;
    int r = s_p[I].oReader(self, L, a3, key);
    t_capIdx = -1;
    Apply(I, t_capPtr);
    return r;
}

static bool SigOk(uintptr_t a, const unsigned char* sig, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery((void*)a, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return memcmp((const void*)a, sig, n) == 0;
}

void ExePatchInstall() {
    if (!g_cfg.motorPatch) { Log("motor: parches en memoria apagados (motor = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kExeStamp) {
        Log("motor: exe distinto (base %p, sello %08X): sin parches", (void*)mod, (unsigned)nt->FileHeader.TimeDateStamp); return;
    }
    void* readers[3] = { (void*)&hk_Reader<0>, (void*)&hk_Reader<1>, (void*)&hk_Reader<2> };
    void* lookups[3] = { (void*)&hk_Lookup<0>, (void*)&hk_Lookup<1>, (void*)&hk_Lookup<2> };
    int n = 0;
    for (int i = 0; i < 3; i++) {
        Patch& p = s_p[i];
        if (!SigOk(p.reader, kReaderSig, sizeof kReaderSig) || !SigOk(p.lookup, kLookupSig, sizeof kLookupSig)) { Log("motor: %s: la firma no coincide, se omite", p.name); continue; }
        if (MH_CreateHook((void*)p.lookup, lookups[i], (void**)&p.oLookup) != MH_OK || MH_CreateHook((void*)p.reader, readers[i], (void**)&p.oReader) != MH_OK) { Log("motor: %s: no se pudo enganchar", p.name); continue; }
        MH_EnableHook((void*)p.lookup); MH_EnableHook((void*)p.reader); n++;
    }
    Log("motor: %d lectores de configuracion enganchados (sombra_resolucion = %d, sombra_cascadas = %.2f)", n, g_cfg.motorShadowRes, g_cfg.motorCascadeScale);
}

} // namespace fx
