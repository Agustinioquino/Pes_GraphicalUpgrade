// PES21 FX (31/10) - CAMARA LENTA: escala del reloj de juego del motor (pedido de Agustin: ver las celebraciones y las
// expresiones despacio). Ingenieria inversa de PES2021.exe 1.07.02 (sello 62787C6A) sobre el volcado de exedump.cpp:
//
//  * fox::DeltaGameTimeHolder (vtable 0x14257ff68; vive en el objeto global 0x1436de470 + 0x220, lo devuelve 0x14029c000):
//    +0x08 delta del cuadro (s), +0x0c escala del cuadro, +0x30 escala pedida (la pone el juego y vale para UN cuadro),
//    +0xe0 delta x escala, +0x100 / +0x110 tiempo acumulado (segundos y contador).
//  * Paso por cuadro 0x14035dcb0 (holder, xmm1 = delta, r8): lee +0x30, lo guarda en +0x0c, lo vuelve a 1.0 y acumula
//    delta x escala. Lo llama el lazo de juego 0x140318570 justo despues de poner +0x30 = [lazo+0x15c] x [lazo+0x160] cuando
//    el juego pidio un cambio de velocidad (byte [lazo+0x76]); si no lo pidio, +0x30 vale 1.0.
//
// La DLL engancha el paso (MinHook, solo si coinciden el sello del exe y las firmas de bytes) y multiplica +0x30 por la
// velocidad del menu antes de dejarlo seguir: el juego entero (partido, celebraciones, repeticiones, visor del editor)
// corre mas despacio o mas rapido, y el menu del mod y el mouse siguen a velocidad real (no dependen de ese reloj).
//   343 (ini): x velocidad (1 = normal, 0.25 = un cuarto; 0.02..4), y prendida. La DLL escribe z = escala propia del
//   juego vista en el ultimo cuadro (1 salvo que el juego pida su propia camara lenta) y w = escala aplicada.
#include "core.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <algorithm>

namespace fx {

static const unsigned kStamp = 0x62787C6A;
static const uintptr_t kStep = 0x14035dcb0ull;   // DeltaGameTimeHolder: paso por cuadro
// prologo: mov [rsp+10h],rbx; mov [rsp+18h],r8; push rdi; sub rsp,40h; movaps [rsp+30h],xmm6
static const unsigned char kStepSig[20] = { 0x48,0x89,0x5c,0x24,0x10,0x4c,0x89,0x44,0x24,0x18,0x57,0x48,0x83,0xec,0x40,0x0f,0x29,0x74,0x24,0x30 };
static const unsigned char kStepSig2[5] = { 0xf3,0x0f,0x10,0x77,0x30 };                 // 0x14035dcee movss xmm6,[rdi+30h]
static const unsigned char kStepSig3[7] = { 0xc7,0x47,0x30,0x00,0x00,0x80,0x3f };       // 0x14035dcfb mov dword [rdi+30h],1.0f

typedef void (*StepFn)(void* holder, float delta, void* r8);
static StepFn o_step = nullptr;
static bool s_tried = false, s_ok = false;
static std::atomic<int> s_calls{0};
static std::atomic<float> s_gameScale{1.0f}, s_applied{1.0f}, s_lastDelta{0.0f};

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static bool SigAt(uintptr_t a, const unsigned char* sig, size_t n) { return Readable((const void*)a, n) && memcmp((const void*)a, sig, n) == 0; }

// velocidad pedida por el menu (1 = el juego tal cual)
static float Factor() {
    if (g_cfg.live[343][1] <= 0.5f) return 1.0f;
    float v = g_cfg.live[343][0];
    if (!(v == v)) return 1.0f;
    return std::clamp(v, 0.02f, 4.0f);
}

static void hk_Step(void* holder, float delta, void* r8) {
    float* sc = (float*)((char*)holder + 0x30);
    const float g = *sc;
    const float f = Factor();
    if (f != 1.0f) *sc = g * f;
    s_gameScale = g; s_applied = *sc; s_lastDelta = delta; s_calls++;
    o_step(holder, delta, r8);
}

static void Install() {
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("camara lenta: parches en memoria apagados (motor = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) {
        Log("camara lenta: exe distinto (base %p, sello %08X): sin gancho", (void*)mod, (unsigned)nt->FileHeader.TimeDateStamp); return;
    }
    if (!SigAt(kStep, kStepSig, sizeof kStepSig) || !SigAt(0x14035dceeull, kStepSig2, sizeof kStepSig2) || !SigAt(0x14035dcfbull, kStepSig3, sizeof kStepSig3)) {
        Log("camara lenta: la firma de 0x14035dcb0 no coincide, se omite"); return;
    }
    if (MH_CreateHook((void*)kStep, (void*)&hk_Step, (void**)&o_step) == MH_OK && MH_EnableHook((void*)kStep) == MH_OK) s_ok = true;
    Log("camara lenta: reloj de juego del motor %s (0x14035dcb0; velocidad x343 = %.2f, %s)", s_ok ? "enganchado" : "NO se pudo enganchar",
        g_cfg.live[343][0], g_cfg.live[343][1] > 0.5f ? "prendida" : "apagada");
}

bool TiempoHooked() { return s_ok; }

void TiempoPresent() {
    if (!s_tried) Install();
    float* p = g_cfg.live[343];
    p[2] = s_ok ? s_gameScale.load() : -1.0f;
    p[3] = s_ok ? s_applied.load() : -1.0f;
    if (!s_ok) return;
    // anotar los cambios de velocidad (menu / tecla) y, cada 30 s con la camara lenta puesta, cuantos pasos del reloj hubo
    static float s_lastF = -1.0f; static double s_t = 0; static int s_c0 = 0;
    const float f = Factor();
    if (f != s_lastF) {
        if (s_lastF >= 0) Log("camara lenta: velocidad x%.2f (%s)", f, f == 1.0f ? "normal" : f < 1.0f ? "mas lenta" : "mas rapida");
        s_lastF = f;
    }
    double t = Now();
    if (s_t == 0) { s_t = t; s_c0 = s_calls.load(); return; }
    if (t - s_t < 30.0) return;
    if (f != 1.0f || s_gameScale.load() != 1.0f)
        Log("camara lenta (30 s): %d pasos del reloj, delta del juego %.4f s, escala propia del juego %.2f, aplicada %.2f",
            s_calls.load() - s_c0, s_lastDelta.load(), s_gameScale.load(), s_applied.load());
    s_t = t; s_c0 = s_calls.load();
}

} // namespace fx
