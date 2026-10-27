// PES21 FX - parches EN MEMORIA del motor (20/10). El exe en disco nunca se toca: con el volcado del exe desempaquetado
// (exedump.cpp) se ubicaron las funciones que leen la configuracion grafica desde los scripts Lua del juego (Fox Engine:
// GrPluginPesShadow.ShadowResolution, GrShadowSettings.DirectionalLightShadowResolution / CascadeShadowRangeScale, ...).
// Se enganchan con MinHook: primero se registra en el log lo que el juego usa y, si el usuario lo pide en PES21FX.ini
// ([Motor]), se cambian los valores recien leidos antes de que el motor los use (la textura de sombras se crea despues).
// Todo esta protegido por la version del exe (sello de tiempo 62787C6A) y la firma de bytes de cada funcion: si no
// coinciden no se engancha nada.
#include "core.h"
#include <MinHook.h>
#include <tlhelp32.h>
#include <vector>
#include <cstring>
#include <algorithm>

namespace fx {

static const unsigned kExeStamp = 0x62787C6A;
// cada lector: prologo [mov [rsp+10],rbx ; push rdi ; sub rsp,20 ; mov rdi,rdx ; mov rbx,rcx] (16 bytes)
static const unsigned char kReaderSig[16] = { 0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xfa,0x48,0x8b,0xd9 };
// cada busqueda en el mapa de configuraciones: [mov r11,rsp ; push rdi ; sub rsp,40 ; mov qword [r11-18],-2]
static const unsigned char kLookupSig[8]  = { 0x4c,0x8b,0xdc,0x57,0x48,0x83,0xec,0x40 };
static const unsigned char kLookupSig2[10] = { 0x4c,0x8b,0xdc,0x55,0x56,0x57,0x48,0x83,0xec,0x40 };   // GrShadowSettings (guarda rbp/rsi)

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
// resolucion pedida para el mapa de sombras del sol: [Motor] sombra_resolucion manda; si no, el menu (x142... y142 = 1 -> 5120).
// El juego apila las 3 cascadas en vertical (4096 x 12288 en calidad alta) y D3D11 no admite texturas de mas de 16384:
// 5120 (x3 = 15360) es lo maximo que entra; 8192 (el preset maximo de Konami) no se podria crear en PC.
static int WantShadowRes() {
    if (g_cfg.motorShadowRes > 0) return (std::min)(g_cfg.motorShadowRes, 5440);
    return g_cfg.live[142][1] > 0.5f ? 5120 : 0;
}
static void Apply(int i, void* p) {
    if (!p) { Log("motor: %s leido pero no se pudo ubicar la estructura", s_p[i].name); return; }
    int* v = (int*)p; float* f = (float*)p;
    int want = WantShadowRes();
    if (i == 0) {
        Log("motor: %s = %d", s_p[i].name, v[0]);
        if (want > 0 && v[0] > 0 && v[0] != want) { Log("motor:   -> %d (sombras del sol mas finas)", want); v[0] = want; }
    } else if (i == 1) {
        Log("motor: %s: direccional %d, spot %d, point %d, escala de cascadas %.3f, mezcla de cascadas %.3f", s_p[i].name, v[0], v[1], v[2], f[3], f[4]);
        if (want > 0 && v[0] > 0 && v[0] != want) { Log("motor:   direccional -> %d", want); v[0] = want; }
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
        if (!SigOk(p.reader, kReaderSig, sizeof kReaderSig) || !(SigOk(p.lookup, kLookupSig, sizeof kLookupSig) || SigOk(p.lookup, kLookupSig2, sizeof kLookupSig2))) { Log("motor: %s: la firma no coincide, se omite", p.name); continue; }
        if (MH_CreateHook((void*)p.lookup, lookups[i], (void**)&p.oLookup) != MH_OK || MH_CreateHook((void*)p.reader, readers[i], (void**)&p.oReader) != MH_OK) { Log("motor: %s: no se pudo enganchar", p.name); continue; }
        MH_EnableHook((void*)p.lookup); MH_EnableHook((void*)p.reader); n++;
    }
    Log("motor: %d lectores de configuracion enganchados (sombra_resolucion = %d, sombra_cascadas = %.2f)", n, g_cfg.motorShadowRes, g_cfg.motorCascadeScale);
}

// ---------------------------------------------------------------- LOD de modelos (20/10, x142)
// El motor elige el nivel de detalle de cada modelo (jugadores, publico, estadio) con un factor de tamano en pantalla que
// usa una "resolucion de LOD" global (0x14374cd94; en PES 2021 vale 1800 aunque la pantalla sea 1080: Konami ya lo sube).
// Funcion del motor en 0x141686380: factor = resolucion x LodScale x ModelLodScale / ... -> mas resolucion = cada modelo se
// considera mas grande en pantalla = conserva la malla detallada hasta mas lejos. La DLL la multiplica por x142 (1 = juego).
// Si el juego la cambia (menu de opciones) se toma el valor nuevo como base.
// ---------------------------------------------------------------- el sol que gira con la camara (27/10, sol_parche)
// Registro de la partida 27fdac4 (27/10): la luz del sol en espacio de vista que el motor sube al cbuffer de la luz es
// luz_vista = A x sol_cielo con A = vista -> mundo (la traspuesta de la que corresponde): la luz gira el doble de lo que gira
// la camara y solo coincide con el cielo en la camara de TV. El shadow map se arma con esa misma luz, por eso sol y sombra
// pegan de otro lado al cambiar de camara. La cuenta esta en 0x14171e220 (rcx = render, rdx = camara, r8 = luz; la llama el
// bucle de luces de 0x1416ee820): en 0x14171e2a7 hace v = d.x * fila0 + d.y * fila1 + d.z * fila2 con las filas de la matriz de
// vista en rdx + 0x1f0 (la misma disposicion que m_view del cbuffer, donde el shader proyecta con las columnas). El parche
// reescribe esos 57 bytes para que haga v = (fila0 . d, fila1 . d, fila2 . d), con el mismo largo y los mismos registros
// (xmm0 queda con la constante x [r8+0x40] como antes; xmm1 y xmm3 ya eran descartables). Usa insertps (SSE4.1) y haddps (SSE3).
static const uintptr_t kSunFixAt = 0x14171e2a7ull;
static const unsigned char kSunFixOld[57] = {
    0x0f,0x28,0xd1, 0x0f,0x28,0xc1, 0x0f,0xc6,0xd1,0x55, 0x0f,0x59,0x92,0x00,0x02,0x00,0x00, 0x0f,0xc6,0xc1,0x00,
    0x0f,0x59,0x82,0xf0,0x01,0x00,0x00, 0x0f,0xc6,0xc9,0xaa, 0x0f,0x59,0x8a,0x10,0x02,0x00,0x00, 0x0f,0x58,0xd0,
    0x0f,0x28,0x05,0xb8,0xcd,0x2e,0x01, 0x41,0x0f,0x59,0x40,0x40, 0x0f,0x58,0xd1 };
static const unsigned char kSunFixNew[57] = {
    0x66,0x0f,0x3a,0x21,0xc9,0x08,            // insertps xmm1,xmm1,8   (w = 0: la fila 3 no entra)
    0x0f,0x28,0x92,0xf0,0x01,0x00,0x00,       // movaps xmm2,[rdx+1f0]
    0x0f,0x59,0xd1,                           // mulps  xmm2,xmm1
    0x0f,0x28,0x82,0x00,0x02,0x00,0x00,       // movaps xmm0,[rdx+200]
    0x0f,0x59,0xc1,                           // mulps  xmm0,xmm1
    0x0f,0x59,0x8a,0x10,0x02,0x00,0x00,       // mulps  xmm1,[rdx+210]
    0xf2,0x0f,0x7c,0xd0,                      // haddps xmm2,xmm0      -> (f0.d parcial, f1.d parcial)
    0xf2,0x0f,0x7c,0xc9,                      // haddps xmm1,xmm1
    0xf2,0x0f,0x7c,0xd1,                      // haddps xmm2,xmm1      -> (f0.d, f1.d, f2.d, f2.d)
    0x0f,0x28,0x05,0xb5,0xcd,0x2e,0x01,       // movaps xmm0,[rip+...] = 0x142a0b090 (la misma constante, 3 bytes antes)
    0x41,0x0f,0x59,0x40,0x40 };               // mulps  xmm0,[r8+40]
static bool s_sunFixDone = false; static int s_sunFixTries = 0;
static void SunFixPresent() {
    if (s_sunFixDone || !g_cfg.motorSunFix) return;
    if (!SigOk(kSunFixAt, kSunFixOld, sizeof kSunFixOld)) {
        if (SigOk(kSunFixAt, kSunFixNew, sizeof kSunFixNew)) { s_sunFixDone = true; Log("motor: sol_parche ya estaba puesto"); return; }
        if (++s_sunFixTries == 30) Log("motor: sol_parche: los bytes de 0x14171e2a7 no coinciden con los esperados (exe distinto?): no se parchea");
        return;
    }
    // ningun otro hilo puede estar ejecutando esos bytes mientras se reescriben: se suspenden todos y se mira su RIP
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    std::vector<HANDLE> held; bool busy = false;
    THREADENTRY32 te{}; te.dwSize = sizeof te; DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!th) continue;
        if (SuspendThread(th) == (DWORD)-1) { CloseHandle(th); continue; }
        held.push_back(th);
        CONTEXT c{}; c.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(th, &c) && c.Rip >= kSunFixAt - 0x100 && c.Rip < kSunFixAt + sizeof kSunFixOld + 0x100) busy = true;
    }
    CloseHandle(snap);
    bool written = false;
    if (!busy) {
        DWORD old = 0;
        if (VirtualProtect((void*)kSunFixAt, sizeof kSunFixNew, PAGE_EXECUTE_READWRITE, &old)) {
            memcpy((void*)kSunFixAt, kSunFixNew, sizeof kSunFixNew);
            DWORD tmp = 0; VirtualProtect((void*)kSunFixAt, sizeof kSunFixNew, old, &tmp);
            FlushInstructionCache(GetCurrentProcess(), (void*)kSunFixAt, sizeof kSunFixNew);
            written = true;
        }
    }
    for (HANDLE th : held) { ResumeThread(th); CloseHandle(th); }
    if (written) { s_sunFixDone = true; Log("motor: sol_parche puesto en 0x14171e2a7: el sol en vista se calcula con las columnas de la matriz de vista (luz y sombra fijas al cielo)"); }
    else if (!busy) { s_sunFixDone = true; Log("motor: sol_parche: no se pudo escribir el codigo (VirtualProtect)"); }
}

static int s_lodBase = -1, s_lodLast = -1; static bool s_lodChecked = false, s_lodOk = false; static double s_lodT = 0;
void ExePatchPresent() {
    if (!g_cfg.motorPatch) return;
    double t = Now(); if (t - s_lodT < 1.0) return; s_lodT = t;
    SunFixPresent();   // (27/10) el sol que gira con la camara
    volatile int* g = (volatile int*)0x14374cd94ull;
    if (!s_lodChecked) {
        s_lodChecked = true;
        HMODULE mod = GetModuleHandleW(nullptr);
        auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
        MEMORY_BASIC_INFORMATION mi{};
        s_lodOk = (uintptr_t)mod == 0x140000000ull && nt->FileHeader.TimeDateStamp == kExeStamp && VirtualQuery((void*)g, &mi, sizeof mi)
               && mi.State == MEM_COMMIT && (mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY));
        if (!s_lodOk) { Log("motor: LOD de modelos no disponible (exe distinto o memoria protegida %08X)", (unsigned)mi.Protect); return; }
    }
    if (!s_lodOk) return;
    int cur = *g;
    if (cur <= 100 || cur > 30000) return;
    if (s_lodBase < 0 || (cur != s_lodBase && cur != s_lodLast)) { s_lodBase = cur; Log("motor: resolucion de LOD del juego = %d", cur); }
    float m = g_cfg.live[142][0]; if (m < 1.01f) m = 1.0f; if (m > 3.0f) m = 3.0f;
    int want = (int)(s_lodBase * m + 0.5f);
    if (cur != want) { *g = want; Log("motor: resolucion de LOD %d -> %d (x%.2f: mallas detalladas hasta mas lejos)", cur, want, m); }
    s_lodLast = want;
}

} // namespace fx
