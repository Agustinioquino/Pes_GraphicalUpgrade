// PES21 FX - musculatura (23/10, prueba). Ingenieria inversa de PES2021.exe 1.07.02 (sello 62787C6A).
// El esqueleto del motor tiene huesos de deformacion por musculo (dsk_deltoid, dsk_pectoralis, dsk_trapezius,
// dsk_upperarm, dsk_thighmain, ...), y a cada uno lo mueve una "restriccion" de su clase (cobra::rd::DeltoidConstraint,
// PectConstraint, ...). Cada cuadro, CharacterConstraint::Update (vtable[3], 0x141a6f2b0) copia la transformacion base
// del hueso (escala, rotacion, traslacion: 12 floats) y llama a vtable[4], el Eval de la clase, que la ajusta a la pose
// (p. ej. el deltoide se infla hasta x1.6 al levantar el brazo). La DLL cambia la entrada [4] de las vtables de esos
// musculos (sin tocar codigo; solo si coinciden el sello del exe, el COL de cada vtable y la funcion original) y, despues
// del Eval del juego, multiplica la escala del hueso por el slider: el musculo crece y todo lo que esta pegado a ese hueso
// (el cuerpo y, si la ropa usa el mismo hueso, la camiseta) lo sigue. Se evalua cada cuadro: el cambio se ve al instante.
//   164: x on | y hombros | z biceps | w triceps        165: x antebrazos | y pecho | z espalda | w muslos
//   166: x pantorrillas | y la ropa acompana (mangas, short) | z 0 = hacia afuera (sin tocar el largo), 1 = parejo
#include "core.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <string>

namespace fx {

static const unsigned kStamp = 0x62787C6A;
// grupo: 0 hombros, 1 brazo (biceps / triceps), 2 antebrazos, 3 pecho, 4 espalda, 5 muslos, 6 pantorrillas;
// 10 + g = ropa que acompana al grupo g. ejes: bit k = escala del eje k del hueso. Los huesos de Fox no guardan
// rotacion de reposo (el FMDL solo trae posiciones): en reposo sus ejes son los del modelo, X = costado, Y = arriba,
// Z = adelante. Por eso el brazo (que en la pose de reposo va hacia el costado) crece en Y y Z, la pierna (hacia abajo)
// en X y Z, el pecho y la espalda hacia afuera (Z) y el trapecio hacia arriba y atras (Y, Z). El largo nunca se toca.
struct MusCls { const char* name; uintptr_t vt, col, eval; int group; unsigned axes; };
static const MusCls s_cls[] = {
    { "deltoides",      0x142ab2f18ull, 0x142da5818ull, 0x141a58a80ull, 0, 6 },
    { "brazo",          0x142ab2f50ull, 0x142da58b0ull, 0x141a67300ull, 1, 6 },
    { "brazo piel",     0x142ab2f88ull, 0x142da5948ull, 0x141a68820ull, 1, 6 },
    { "brazo piel 2",   0x142ab2fc0ull, 0x142da59e0ull, 0x141a67e60ull, 1, 6 },
    { "antebrazo",      0x142ab3158ull, 0x142da5e08ull, 0x141a5a5a0ull, 2, 6 },
    { "pectoral",       0x142ab32a8ull, 0x142da6198ull, 0x141a605c0ull, 3, 4 },
    { "trapecio",       0x142ab3068ull, 0x142da5ba8ull, 0x141a647f0ull, 4, 6 },
    { "omoplato",       0x142ab3398ull, 0x142da63f8ull, 0x141a619d0ull, 4, 4 },
    { "espalda baja",   0x142ab32e0ull, 0x142da6230ull, 0x141a54d70ull, 4, 4 },
    { "muslo",          0x142ab3550ull, 0x142da68a8ull, 0x141a64180ull, 5, 5 },
    { "pantorrilla",    0x142ab3748ull, 0x142da6e08ull, 0x141a5cf10ull, 6, 5 },
    { "manga",          0x142ab2ff8ull, 0x142da5a78ull, 0x141a62e20ull, 11, 6 },
    { "borde del short",0x142ab35f8ull, 0x142da6a70ull, 0x141a5ad80ull, 15, 5 },
    { "short",          0x142ab35c0ull, 0x142da69d8ull, 0x141a5dd30ull, 15, 5 },
};
static const int kN = (int)(sizeof(s_cls) / sizeof(s_cls[0]));
static const float kArmR = 0.045f;   // medio grosor del brazo de adelante hacia atras (m, medido en arm.fmdl)

typedef void (__fastcall* EvalFn)(void* self, void* a2, float* out);
static EvalFn s_orig[kN] = {};
static std::atomic<unsigned> s_calls[kN];
static bool s_tried = false; static int s_ok = 0;

static float F(float v) { return v < 0.05f ? 1.0f : std::clamp(v, 0.5f, 2.0f); }   // 0 = sin valor en el ini
static float GroupF(int g) {
    const float* A = g_cfg.live[164]; const float* B = g_cfg.live[165]; const float* C = g_cfg.live[166];
    switch (g) { case 0: return F(A[1]); case 2: return F(B[0]); case 3: return F(B[1]); case 4: return F(B[2]); case 5: return F(B[3]); case 6: return F(C[0]); }
    return 1.0f;
}

template<int I> static void __fastcall hk_Eval(void* self, void* a2, float* out) {
    s_orig[I](self, a2, out);
    s_calls[I].fetch_add(1, std::memory_order_relaxed);
    if (!out || g_cfg.live[164][0] < 0.5f) return;
    int g = s_cls[I].group; bool ropa = g >= 10;
    if (ropa) { if (g_cfg.live[166][1] < 0.5f) return; g -= 10; }
    bool parejo = g_cfg.live[166][2] > 0.5f;
    if (g == 1) {
        // biceps adelante (+Z) y triceps atras: el grosor de adelante hacia atras crece con los dos y el hueso se corre
        // hacia el lado que mas crece (asi el otro lado queda donde estaba); el ancho crece con el promedio
        float b = F(g_cfg.live[164][2]) - 1.0f, t = F(g_cfg.live[164][3]) - 1.0f;
        if (b == 0.0f && t == 0.0f) return;
        if (parejo) { float sp = 1.0f + 0.5f * (b + t); out[0] *= sp; out[1] *= sp; out[2] *= sp; return; }
        out[2] *= 1.0f + b + t; out[1] *= 1.0f + 0.5f * (b + t);
        if (!ropa) out[10] += kArmR * (b - t);
        return;
    }
    float f = GroupF(g);
    if (f == 1.0f) return;
    unsigned ax = parejo ? 7u : s_cls[I].axes;
    for (int k = 0; k < 3; k++) if (ax & (1u << k)) out[k] *= f;
}
template<int... Is> struct HookList { static void* const v[sizeof...(Is)]; };
template<int... Is> void* const HookList<Is...>::v[sizeof...(Is)] = { (void*)&hk_Eval<Is>... };
static void* const* Hooks() { return HookList<0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13>::v; }
static_assert(kN == 14, "la lista de ganchos tiene que tener una entrada por musculo");

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
    const float* A = g_cfg.live[164]; const float* B = g_cfg.live[165]; const float* C = g_cfg.live[166];
    if (total) Log("musculatura (30 s, %s: hombros %.2f biceps %.2f triceps %.2f antebrazos %.2f pecho %.2f espalda %.2f muslos %.2f pantorrillas %.2f, ropa %s, %s):%s",
                   A[0] > 0.5f ? "prendida" : "apagada", A[1], A[2], A[3], B[0], B[1], B[2], B[3], C[0], C[1] > 0.5f ? "si" : "no", C[2] > 0.5f ? "parejo" : "hacia afuera", line.c_str());
    s_t = t;
}

} // namespace fx
