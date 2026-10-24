// PES21 FX - musculatura (23/10, prueba). Ingenieria inversa de PES2021.exe 1.07.02 (sello 62787C6A).
// El esqueleto del motor tiene huesos de deformacion por musculo (dsk_deltoid, dsk_pectoralis, dsk_trapezius,
// dsk_upperarm, dsk_thighmain, ...), y a cada uno lo mueve una "restriccion" de su clase (cobra::rd::DeltoidConstraint,
// PectConstraint, ...). Cada cuadro, CharacterConstraint::Update (vtable[3], 0x141a6f2b0) copia la transformacion base
// del hueso (escala, rotacion, traslacion: 12 floats) y llama a vtable[4], el Eval de la clase, que la ajusta a la pose
// (p. ej. el deltoide se infla hasta x1.6 al levantar el brazo). La DLL cambia la entrada [4] de las vtables de esos
// musculos (sin tocar codigo; solo si coinciden el sello del exe, el COL de cada vtable y la funcion original) y, despues
// del Eval del juego, multiplica la escala del hueso por el slider: el musculo crece y todo lo que esta pegado a ese hueso
// (el cuerpo y, si la ropa usa el mismo hueso, la camiseta) lo sigue. Se evalua cada cuadro: el cambio se ve al instante.
//   162: x on | y brazos y hombros | z pecho y espalda | w piernas (1 = como el juego)
//   163: x la ropa acompana (mangas, short, borde de la camiseta) | y ejes: 0 = los que ya infla el juego, 1 = parejo
#include "core.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <string>

namespace fx {

static const unsigned kStamp = 0x62787C6A;
// grupo: 0 brazos y hombros, 1 pecho y espalda, 2 piernas; 10..12 = ropa que acompana al grupo (g - 10)
// ejes: bit k = escala del eje k (los que el Eval del juego ya modula para ese musculo)
struct MusCls { const char* name; uintptr_t vt, col, eval; int group; unsigned axes; };
static const MusCls s_cls[] = {
    { "deltoides",      0x142ab2f18ull, 0x142da5818ull, 0x141a58a80ull, 0, 6 },
    { "brazo",          0x142ab2f50ull, 0x142da58b0ull, 0x141a67300ull, 0, 6 },
    { "brazo piel",     0x142ab2f88ull, 0x142da5948ull, 0x141a68820ull, 0, 6 },
    { "brazo piel 2",   0x142ab2fc0ull, 0x142da59e0ull, 0x141a67e60ull, 0, 6 },
    { "antebrazo",      0x142ab3158ull, 0x142da5e08ull, 0x141a5a5a0ull, 0, 6 },
    { "pectoral",       0x142ab32a8ull, 0x142da6198ull, 0x141a605c0ull, 1, 1 },
    { "trapecio",       0x142ab3068ull, 0x142da5ba8ull, 0x141a647f0ull, 1, 1 },
    { "omoplato",       0x142ab3398ull, 0x142da63f8ull, 0x141a619d0ull, 1, 1 },
    { "muslo",          0x142ab3550ull, 0x142da68a8ull, 0x141a64180ull, 2, 4 },
    { "pantorrilla",    0x142ab3748ull, 0x142da6e08ull, 0x141a5cf10ull, 2, 1 },
    { "manga",          0x142ab2ff8ull, 0x142da5a78ull, 0x141a62e20ull, 10, 2 },
    { "borde camiseta", 0x142ab35f8ull, 0x142da6a70ull, 0x141a5ad80ull, 11, 7 },
    { "short",          0x142ab35c0ull, 0x142da69d8ull, 0x141a5dd30ull, 12, 3 },
};
static const int kN = (int)(sizeof(s_cls) / sizeof(s_cls[0]));

typedef void (__fastcall* EvalFn)(void* self, void* a2, float* out);
static EvalFn s_orig[kN] = {};
static std::atomic<unsigned> s_calls[kN];
static bool s_tried = false; static int s_ok = 0;

static float Factor(int i) {
    const float* P = g_cfg.live[162]; const float* Q = g_cfg.live[163];
    if (P[0] < 0.5f) return 1.0f;
    int g = s_cls[i].group;
    if (g >= 10) { if (Q[0] < 0.5f) return 1.0f; g -= 10; }
    float f = P[1 + g];
    if (f < 0.05f) return 1.0f;   // sin valor en el ini
    return std::clamp(f, 0.5f, 2.0f);
}

template<int I> static void __fastcall hk_Eval(void* self, void* a2, float* out) {
    s_orig[I](self, a2, out);
    s_calls[I].fetch_add(1, std::memory_order_relaxed);
    float f = Factor(I);
    if (f != 1.0f && out) {
        unsigned ax = g_cfg.live[163][1] > 0.5f ? 7u : s_cls[I].axes;
        for (int k = 0; k < 3; k++) if (ax & (1u << k)) out[k] *= f;
    }
}
template<int... Is> struct HookList { static void* const v[sizeof...(Is)]; };
template<int... Is> void* const HookList<Is...>::v[sizeof...(Is)] = { (void*)&hk_Eval<Is>... };
static void* const* Hooks() { return HookList<0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12>::v; }
static_assert(kN == 13, "la lista de ganchos tiene que tener una entrada por musculo");

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}

static void Install() {
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("musculatura: parches en memoria apagados (motor = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("musculatura: exe distinto, sin ganchos"); return; }
    std::string bad;
    for (int i = 0; i < kN; i++) {
        void** vt = (void**)s_cls[i].vt;
        if (!Readable(vt - 1, 6 * sizeof(void*)) || (uintptr_t)vt[-1] != s_cls[i].col || (uintptr_t)vt[4] != s_cls[i].eval) { bad += std::string(" ") + s_cls[i].name; continue; }
        DWORD old = 0;
        if (!VirtualProtect(&vt[4], sizeof(void*), PAGE_READWRITE, &old)) { bad += std::string(" ") + s_cls[i].name + "(protect)"; continue; }
        s_orig[i] = (EvalFn)vt[4];
        InterlockedExchangePointer(&vt[4], Hooks()[i]);
        VirtualProtect(&vt[4], sizeof(void*), old, &old);
        s_ok++;
    }
    Log("musculatura: %d de %d musculos enganchados%s%s", s_ok, kN, bad.empty() ? "" : "; no coinciden:", bad.c_str());
}

void MuscleHookPresent() {
    if (!s_tried) Install();
    if (!s_ok) return;
    static double s_t = 0; static unsigned s_prev[kN] = {};
    double t = Now();
    if (s_t == 0) { s_t = t; for (int i = 0; i < kN; i++) s_prev[i] = s_calls[i].load(); return; }
    if (t - s_t < 30.0) return;
    std::string line; char b[64]; unsigned total = 0;
    for (int i = 0; i < kN; i++) {
        unsigned c = s_calls[i].load(), d = c - s_prev[i]; s_prev[i] = c; total += d;
        snprintf(b, sizeof b, " %s %.0f/s", s_cls[i].name, d / (t - s_t)); line += b;
    }
    const float* P = g_cfg.live[162];
    if (total) Log("musculatura (30 s, %s: brazos %.2f pecho %.2f piernas %.2f, ropa %s, ejes %s):%s", P[0] > 0.5f ? "prendida" : "apagada",
                   P[1], P[2], P[3], g_cfg.live[163][0] > 0.5f ? "si" : "no", g_cfg.live[163][1] > 0.5f ? "parejo" : "del juego", line.c_str());
    s_t = t;
}

} // namespace fx
