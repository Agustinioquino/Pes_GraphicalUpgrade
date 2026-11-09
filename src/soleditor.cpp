// PES21 FX - sol real del motor en el editor (09/11). Ingenieria inversa de PES2021.exe 1.07.02 (sello 62787C6A).
//
// El editor no tiene sol porque su escena de estudio (EditFace.fox2) solo trae focos. tools/fox/editface_sol.py le agrega una
// entidad fox::grx::DirectionalLight ("zfx_SolEditor"); este modulo:
//   1) la encuentra: cambia 5 entradas de la vtable de fox::grx::DirectionalLightBody (0x142a48290) -- sin tocar codigo, solo
//      si cada entrada todavia apunta a la funcion esperada --:
//        [1]  0x141811ed0 destructor                  -> suelta el puntero
//        [7]  0x14181a0b0 (= [15]) aplicar propiedades -> llama FUN_141812350: entidad -> DirectionalLightSource (cuerpo+0x88)
//        [8]  0x141813f70 desactivar                   -> suelta el puntero
//        [6]  0x141815570 iniciar (al cargar la escena: FUN_141815620 y despues [11]) -> guarda el cuerpo (09/11 b: el log
//             mostro que la primera creacion entra por aca, no por [14])
//        [14] 0x14181a030 activar (FUN_141815620: crea la fuente y la mete en la escena de los focos) -> guarda el cuerpo
//      Entidad = *(*(cuerpo+0x30)+0x18) (FUN_1402b9e60). Campos de la entidad (reflexion, 0x141816020): enable +0x130 (bool),
//      direction +0x140 (Vector3, 16 B, hacia el sol; la gira el transform de la entidad, identidad en el fox2), color +0x170
//      (RGBA float), temperature +0x180, lux +0x188 (float).
//   2) SolEditorSet(): guarda lo pedido y lo escribe en la entidad desde la entrada [7] (hilo del motor) antes de llamar a la
//      original, que lo baja a la fuente. Si el motor no llama [7] seguido, Present la llama una vez cuando hay algo pendiente
//      (lo anota en el log).
//   3) log (sol_editor_log = 1, defecto): creacion del sol, vector global del sol del partido 0x1437d5700 (lo que TppAtmosphere
//      le da a su fuente: y > 0 de dia = la convencion "hacia el sol"), llamadas por segundo a PluginSunlight::vf8 0x1416ee820 y
//      PluginShadow::vf7 0x1416c66e0 (vtables 0x142a1afd8 [8] / 0x142a11eb0 [7]) y la luz que eligio para la sombra (plugin+0x60).
//      El filtro de "vista chica" de hooks.cpp ya anota cuando no copia el sol ("luz del sol: ... vista chica").
// Nada de esto escribe codigo del exe; con motor = 0 no engancha.
#include "core.h"
#include <atomic>
#include <mutex>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace fx {

static const unsigned kStamp = 0x62787C6A;
static const uintptr_t kBodyVtbl = 0x142a48290ull;     // fox::grx::DirectionalLightBody
static const uintptr_t kSunPlugVtbl = 0x142a1afd8ull;  // fox::gr::PluginSunlight
static const uintptr_t kShadowPlugVtbl = 0x142a11eb0ull; // fox::gr::PluginShadow
static const uintptr_t kSunGlobalDir = 0x1437d5700ull;   // direccion del sol de la atmosfera (FUN_141943ce0 -> FUN_14169f3c0)

typedef uintptr_t (__fastcall* Fn4)(void*, uintptr_t, uintptr_t, uintptr_t);
static Fn4 o_init = nullptr, o_dtor = nullptr, o_apply = nullptr, o_apply15 = nullptr, o_deact = nullptr, o_act = nullptr, o_sun = nullptr, o_shadow = nullptr;

static bool s_tried = false, s_bodyOk = false, s_plugOk = false;
static std::atomic<void*> s_body{nullptr};
static std::atomic<int> s_applies{0}, s_sunCalls{0}, s_shadowCalls{0}, s_created{0}, s_forced{0};
static std::atomic<void*> s_shadowLight{nullptr};

struct Pending { float dir[3]; float rgb[3]; float kelvin, lux; bool on; };
static std::mutex s_mx; static Pending s_pend; static bool s_hasPend = false; static double s_pendT = 0;

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static bool LogOn() { return g_cfg.solEditorLog; }

static char* EntityOf(void* body) {
    __try {
        char* h = *(char* volatile*)((char*)body + 0x30);
        if (!h) return nullptr;
        return *(char* volatile*)(h + 0x18);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
static bool BodyValid(void* b) {
    if (!b || !Readable(b, 0x98)) return false;
    __try { return *(uintptr_t volatile*)b == kBodyVtbl; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void WriteEntity(char* e, const Pending& p) {   // sin objetos C++: puede usar __try
    __try {
        *(volatile unsigned char*)(e + 0x130) = p.on ? 1 : 0;
        float* d = (float*)(e + 0x140); d[0] = p.dir[0]; d[1] = p.dir[1]; d[2] = p.dir[2]; d[3] = 0.0f;
        float* c = (float*)(e + 0x170); c[0] = p.rgb[0]; c[1] = p.rgb[1]; c[2] = p.rgb[2]; c[3] = 1.0f;
        *(volatile float*)(e + 0x180) = p.kelvin;
        *(volatile float*)(e + 0x188) = p.lux;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static bool TakePending(Pending* out) {
    std::lock_guard<std::mutex> lk(s_mx);
    if (!s_hasPend) return false;
    *out = s_pend; s_hasPend = false; return true;
}
static void WritePending(void* body) {
    Pending p;
    if (!TakePending(&p)) return;
    char* e = EntityOf(body);
    if (!e || !Readable(e + 0x130, 0x60)) return;
    WriteEntity(e, p);
}
static void CallApply(void* b) { __try { o_apply(b, 0, 0, 0); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
static void ReadSunGlobal(float g[4]) { __try { memcpy(g, (const void*)kSunGlobalDir, 16); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
static bool PendingAge(double* t0) { std::lock_guard<std::mutex> lk(s_mx); *t0 = s_pendT; return s_hasPend; }

static uintptr_t __fastcall hk_dtor(void* b, uintptr_t a, uintptr_t c, uintptr_t d) {
    void* exp = b; s_body.compare_exchange_strong(exp, nullptr);
    return o_dtor(b, a, c, d);
}
static uintptr_t __fastcall hk_deact(void* b, uintptr_t a, uintptr_t c, uintptr_t d) {
    void* exp = b; if (s_body.compare_exchange_strong(exp, nullptr) && LogOn()) Log("sol_editor: sol desactivado (cuerpo %p)", b);
    return o_deact(b, a, c, d);
}
static void NoteCreated(void* b, const char* via);
static uintptr_t __fastcall hk_init(void* b, uintptr_t a, uintptr_t c, uintptr_t d) {
    uintptr_t r = o_init(b, a, c, d);
    NoteCreated(b, "iniciar");
    return r;
}
static uintptr_t __fastcall hk_act(void* b, uintptr_t a, uintptr_t c, uintptr_t d) {
    uintptr_t r = o_act(b, a, c, d);
    NoteCreated(b, "activar");
    return r;
}
static void NoteCreated(void* b, const char* via) {
    s_body = b; s_created++;
    if (LogOn()) {
        char* e = EntityOf(b); float dir[3] = {}, lux = 0, k = 0; unsigned char en = 0; void* src = nullptr;
        __try { if (e) { memcpy(dir, e + 0x140, 12); lux = *(float*)(e + 0x188); k = *(float*)(e + 0x180); en = *(unsigned char*)(e + 0x130); }
                src = *(void**)((char*)b + 0x88); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        Log("sol_editor: dirLightSource cuerpo=%p entidad=%p fuente=%p enable=%d dir=(%.3f %.3f %.3f) lux=%.0f %.0f K (hilo %lu)",
            b, (void*)e, src, en, dir[0], dir[1], dir[2], lux, k, GetCurrentThreadId());
        Log("sol_editor: (creado via %s)", via);
    }
}
static uintptr_t __fastcall hk_apply(void* b, uintptr_t a, uintptr_t c, uintptr_t d) {
    s_applies++;
    if (b == s_body.load()) WritePending(b);
    return o_apply(b, a, c, d);
}
static uintptr_t __fastcall hk_apply15(void* b, uintptr_t a, uintptr_t c, uintptr_t d) {
    s_applies++;
    if (b == s_body.load()) WritePending(b);
    return o_apply15(b, a, c, d);
}
static uintptr_t __fastcall hk_sun(void* self, uintptr_t a, uintptr_t c, uintptr_t d) {
    s_sunCalls++;
    return o_sun(self, a, c, d);
}
static uintptr_t __fastcall hk_shadow(void* self, uintptr_t a, uintptr_t c, uintptr_t d) {
    uintptr_t r = o_shadow(self, a, c, d);
    s_shadowCalls++;
    __try { s_shadowLight = *(void* volatile*)((char*)self + 0x60); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

static bool Swap(uintptr_t vtbl, int idx, uintptr_t expect, void* hook, Fn4* orig) {
    void** slot = (void**)(vtbl + idx * sizeof(void*));
    if (!Readable(slot, sizeof(void*)) || (uintptr_t)*slot != expect) {
        Log("sol_editor: vtable %llx[%d] = %p, se esperaba %llx: se omite", (unsigned long long)vtbl, idx, Readable(slot, 8) ? *slot : nullptr, (unsigned long long)expect);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *orig = (Fn4)*slot;
    InterlockedExchangePointer(slot, hook);
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

static void Install() {
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("sol_editor: parches en memoria apagados (motor = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("sol_editor: exe distinto: sin ganchos"); return; }
    // comprobar las 5 entradas antes de cambiar ninguna
    struct E { int i; uintptr_t a; } want[] = { {1, 0x141811ed0ull}, {6, 0x141815570ull}, {7, 0x14181a0b0ull}, {8, 0x141813f70ull}, {14, 0x14181a030ull}, {15, 0x14181a0b0ull} };
    bool ok = true;
    for (auto& w : want) { void** s = (void**)(kBodyVtbl + w.i * 8); if (!Readable(s, 8) || (uintptr_t)*s != w.a) ok = false; }
    if (ok) {
        s_bodyOk = Swap(kBodyVtbl, 1, 0x141811ed0ull, (void*)&hk_dtor, &o_dtor) && Swap(kBodyVtbl, 6, 0x141815570ull, (void*)&hk_init, &o_init) && Swap(kBodyVtbl, 8, 0x141813f70ull, (void*)&hk_deact, &o_deact)
                && Swap(kBodyVtbl, 14, 0x14181a030ull, (void*)&hk_act, &o_act) && Swap(kBodyVtbl, 7, 0x14181a0b0ull, (void*)&hk_apply, &o_apply)
                && Swap(kBodyVtbl, 15, 0x14181a0b0ull, (void*)&hk_apply15, &o_apply15);
    } else Log("sol_editor: la vtable de DirectionalLightBody no coincide: sin control del sol del editor");
    s_plugOk = Swap(kSunPlugVtbl, 8, 0x1416ee820ull, (void*)&hk_sun, &o_sun) && Swap(kShadowPlugVtbl, 7, 0x1416c66e0ull, (void*)&hk_shadow, &o_shadow);
    Log("sol_editor: cuerpo de la luz direccional %s, plugins sol/sombra %s", s_bodyOk ? "enganchado" : "NO", s_plugOk ? "contados" : "NO");
}

void* SolEditorBody() {
    void* b = s_body.load();
    return BodyValid(b) ? b : nullptr;
}

bool SolEditorSet(const float dirHaciaSol[3], const float rgb[3], float kelvin, float lux, bool on) {
    float l = sqrtf(dirHaciaSol[0] * dirHaciaSol[0] + dirHaciaSol[1] * dirHaciaSol[1] + dirHaciaSol[2] * dirHaciaSol[2]);
    if (!(l > 1e-6f) || !std::isfinite(lux)) return false;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        Pending p{}; for (int i = 0; i < 3; i++) { p.dir[i] = dirHaciaSol[i] / l; p.rgb[i] = rgb[i]; }
        p.kelvin = kelvin; p.lux = (std::max)(lux, 0.0f); p.on = on;
        if (!s_hasPend || memcmp(&p, &s_pend, sizeof p) != 0) { s_pend = p; s_hasPend = true; s_pendT = Now(); }
    }
    return SolEditorBody() != nullptr;
}

void SolEditorPresent() {
    if (!s_tried) Install();
    if (!s_bodyOk && !s_plugOk) return;
    // si el motor no llama "aplicar" seguido, se llama una vez con lo pendiente (solo cuerpos validos)
    if (s_bodyOk) {
        double t0 = 0; bool pend = PendingAge(&t0);
        void* b = SolEditorBody();
        if (pend && b && Now() - t0 > 0.25) {
            WritePending(b);
            CallApply(b);
            if (s_forced.fetch_add(1) == 0 && LogOn()) Log("sol_editor: el motor no aplico en 0.25 s: se aplica desde Present (hilo %lu)", GetCurrentThreadId());
        }
    }
    if (!LogOn()) return;
    static double s_t = 0; static int a0 = 0, sc0 = 0, sh0 = 0;
    double t = Now();
    if (s_t == 0) { s_t = t; return; }
    if (t - s_t < 10.0) return;
    double dt = t - s_t;
    int a = s_applies.load(), sc = s_sunCalls.load(), sh = s_shadowCalls.load();
    float g[4] = {};
    ReadSunGlobal(g);
    void* b = SolEditorBody();
    if (sc != sc0 || sh != sh0 || a != a0 || b)
        Log("plugins_editor: sunlight=%.1f/s shadow=%.1f/s luz_principal=%p aplicar=%.1f/s sol_editor=%p (creados %d, forzados %d)",
            (sc - sc0) / dt, (sh - sh0) / dt, s_shadowLight.load(), (a - a0) / dt, b, s_created.load(), s_forced.load());
    if (g[0] != 0 || g[1] != 0 || g[2] != 0)
        Log("sol_dir_partido: DAT_1437d5700=(%.3f %.3f %.3f) partido %d", g[0], g[1], g[2], g_matchGen.load());
    s_t = t; a0 = a; sc0 = sc; sh0 = sh;
}

} // namespace fx
