// PES21 FX (08/11) - GUARDIA DE LAS LISTAS DE COMANDOS DEL JUEGO
// Cuelgues del editor 08/11 07:54 y 09:08 UTC (volcados cuelgue_20261108_0755 / _0908): el hilo de dibujo del juego gira para
// siempre en FUN_141ff6660 (PES2021+0x1ff668e). Cada entrada de la tabla de listas (objetos de 0x20: +0x10 ID3D11CommandList,
// +0x18 lock) tiene un lock que se toma con "lock xadd +1 hasta leer 0" y se suelta escribiendo 0. El valor "basura" del lock
// (0x1C521F34, 0x09E85EEE) NO es memoria pisada: es la cuenta de intentos del propio hilo que gira (crece +1 por intento).
// Protocolo del juego: FUN_141ff6560 (empezar a grabar) toma el lock y lo DEJA tomado; FUN_141ff65f0 (FinishCommandList) guarda
// la lista y lo suelta. Si FinishCommandList falla, FUN_141ff65f0 sale con 0x80000001 SIN soltar el lock, y si la grabacion se
// abandona (el editor recarga al jugador) tampoco: la entrada queda con el lock tomado, +0x10 = 0, y quien la ejecuta
// (FUN_141ff6660) o la libera (FUN_141ff6700) espera para siempre.
// Arreglo: (1) FUN_141ff65f0: si el FinishCommandList falla se suelta el lock igual (la entrada queda vacia: ejecutarla no hace
// nada, FUN_141ff56f0 ya maneja la lista nula); (2) FUN_141ff6560 / 6660 / 6700 se reemplazan por la misma logica con un tope de
// espera (kMaxWait): pasado el tope se anota en el log y se toma el lock igual (el dueno no va a volver: grabar una lista
// toma milisegundos). Mismo comportamiento que el original mientras el lock se suelte a tiempo.
#include "core.h"
#include <MinHook.h>
#include <windows.h>
#include <atomic>

namespace fx {

static const unsigned kStamp = 0x62787C6A;
static const uintptr_t kBegin = 0x141ff6560ull, kFinish = 0x141ff65f0ull, kGet = 0x141ff6660ull, kFree = 0x141ff6700ull;
// firmas (primeros bytes de cada funcion en el volcado del 08/11 09:08)
static const unsigned char kSigBegin[]  = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xb8,0x01,0x00,0x00,0x00,0xf0,0x0f,0xc1,0x41,0x18,0x85,0xc0 };
static const unsigned char kSigFinish[] = { 0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x02,0x4c,0x8b,0xca,0x33,0xff,0x48,0x8b,0xd9 };
static const unsigned char kSigGet[]    = { 0x40,0x53,0x48,0x83,0xec,0x30,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48,0x8b,0xd9,0x48,0x8d,0x41,0x18 };
static const unsigned char kSigFree[]   = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xb8,0x01,0x00,0x00,0x00,0xf0,0x0f,0xc1,0x41,0x18 };

static const double kMaxWait = 2.0;   // s
static std::atomic<int> s_forced{0}, s_finishFail{0};

typedef long long (*BeginFn)(long long);
typedef long long (*FinishFn)(long long, long long*, char);
typedef long long* (*GetFn)(long long);
typedef void (*FreeFn)(long long);
static BeginFn o_begin; static FinishFn o_finish; static GetFn o_get; static FreeFn o_free;


static void Acquire(long long node, const char* who) {
    volatile long* lk = (volatile long*)(node + 0x18);
    if (InterlockedExchangeAdd(lk, 1) == 0) return;
    LARGE_INTEGER f, t0, t; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    for (unsigned i = 1;; i++) {
        SwitchToThread();
        if (InterlockedExchangeAdd(lk, 1) == 0) return;
        if ((i & 255) == 0) {
            QueryPerformanceCounter(&t);
            if ((double)(t.QuadPart - t0.QuadPart) / (double)f.QuadPart > kMaxWait) {
                int n = ++s_forced;
                if (n <= 20) Log("listas del juego: %s: la entrada %p (serie %08x) tenia el lock tomado hace mas de %.0f s (lista %p): se toma igual (antes el juego quedaba congelado) [%d]",
                    who, (void*)node, *(unsigned*)(node + 0xc), kMaxWait, *(void**)(node + 0x10), n);
                InterlockedExchange(lk, 1);
                return;
            }
        }
    }
}
static void ReleaseLk(long long node) { InterlockedExchange((volatile long*)(node + 0x18), 0); }

static long long hk_begin(long long node) {   // = FUN_141ff6560: toma el lock (lo deja tomado) y suelta la lista vieja
    Acquire(node, "empezar a grabar");
    IUnknown* l = *(IUnknown**)(node + 0x10);
    if (l) { l->Release(); *(void**)(node + 0x10) = nullptr; }
    return 0;
}
static long long hk_finish(long long node, long long* ctx, char restore) {
    long long r = o_finish(node, ctx, restore);
    if (r != 0) {
        int n = ++s_finishFail;
        if (n <= 20) Log("listas del juego: FinishCommandList fallo en la entrada %p (serie %08x): se suelta el lock (antes quedaba tomado y el juego se congelaba al ejecutarla) [%d]",
            (void*)node, *(unsigned*)(node + 0xc), n);
        ReleaseLk(node);
    }
    return r;
}
static long long* hk_get(long long node) {   // = FUN_141ff6660: lista con AddRef (o nula)
    Acquire(node, "ejecutar");
    IUnknown* l = *(IUnknown**)(node + 0x10);
    if (l) l->AddRef();
    ReleaseLk(node);
    return (long long*)l;
}
static void hk_free(long long node) {   // = FUN_141ff6700
    Acquire(node, "liberar");
    IUnknown* l = *(IUnknown**)(node + 0x10);
    if (l) { l->Release(); *(void**)(node + 0x10) = nullptr; }
    ReleaseLk(node);
}

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static bool Sig(uintptr_t a, const unsigned char* s, size_t n) { return Readable((const void*)a, n) && memcmp((const void*)a, s, n) == 0; }

static bool s_tried = false;
void CmdListGuardPresent() {
    if (s_tried) return;
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("listas del juego: parches en memoria apagados (motor = 0): sin guardia"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("listas del juego: exe distinto: sin guardia"); return; }
    if (!Sig(kBegin, kSigBegin, sizeof kSigBegin) || !Sig(kFinish, kSigFinish, sizeof kSigFinish) || !Sig(kGet, kSigGet, sizeof kSigGet) || !Sig(kFree, kSigFree, sizeof kSigFree)) {
        Log("listas del juego: las firmas de 0x141ff6560/65f0/6660/6700 no coinciden: sin guardia"); return;
    }
    bool ok = MH_CreateHook((void*)kBegin, (void*)&hk_begin, (void**)&o_begin) == MH_OK
           && MH_CreateHook((void*)kFinish, (void*)&hk_finish, (void**)&o_finish) == MH_OK
           && MH_CreateHook((void*)kGet, (void*)&hk_get, (void**)&o_get) == MH_OK
           && MH_CreateHook((void*)kFree, (void*)&hk_free, (void**)&o_free) == MH_OK;
    if (ok) ok = MH_EnableHook((void*)kBegin) == MH_OK && MH_EnableHook((void*)kFinish) == MH_OK && MH_EnableHook((void*)kGet) == MH_OK && MH_EnableHook((void*)kFree) == MH_OK;
    Log("listas del juego: guardia de locks (tope %.0f s, FinishCommandList fallido suelta el lock) %s", kMaxWait, ok ? "enganchada" : "NO se pudo enganchar");
}

}  // namespace fx
