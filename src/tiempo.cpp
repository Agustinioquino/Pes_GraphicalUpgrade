// PES21 FX (31/10, 1/11) - CAMARA LENTA (pedido de Agustin: ver las celebraciones y las expresiones despacio, SOLO en el modo
// editar; la repeticion del partido ya la frena su Lua SlowMoServer.lua). Ingenieria inversa de PES2021.exe 1.07.02 (sello
// 62787C6A) sobre el volcado de exedump.cpp:
//
//  * VISOR DEL EDITOR (modo editar / Apariencia): draw::ModelViewBoneUpdator (vtable 0x142c7ae28, [2] = 0x1423f2ca0, partida en
//    .pdata en 0x1423f2ca0 / 0x1423f2ce8 / 0x1423f2dde / 0x1423f2f20 / 0x1423f3752 / 0x1423f3910 / 0x1423f398c, termina en
//    0x1423f3a60) avanza la animacion con un paso FIJO 1/fps (fps = [[0x143713268]+0x9c], 60 o 30): no usa ningun reloj, por
//    eso el gancho al reloj de juego no lo frenaba. Todos los pasos por cuadro terminan en 0x141afd270 (cobra::And::
//    BodyAnimControl::SetStep: rcx = control, xmm1 = paso en segundos; guarda xmm1 x 300 en [[rcx+0x88]+0x10]+0x44, que
//    consume vtable[4] 0x141af9e30 -> fox::anim::AnimControl). Tiene 25 llamadores (publico y estadio del partido tambien):
//    la DLL engancha 0x141afd270 y multiplica el paso SOLO cuando la direccion de retorno cae dentro del BoneUpdator
//    [0x1423f2ca0, 0x1423f3a60): asi se frena el visor del editor y nada mas. Las llamadas de 0x1423f1710 / 0x1423f1820 /
//    0x1423f1cd0 (inicializacion / cambio de modelo, paso 0.001 o 1.0) quedan afuera a proposito.
//  * PARTIDO (opcional, z343 = 1): fox::DeltaGameTimeHolder (vive en [0x1436de470]+0x220): paso por cuadro 0x14035dcb0
//    (holder, xmm1 = delta, r8) lee la escala pedida en +0x30 y la vuelve a 1.0; la DLL la multiplica antes. Frena el partido,
//    las celebraciones y las repeticiones (Agustin no lo quiere por defecto: lo deja apagado).
//
//   343 (ini): x velocidad (1 = normal, 0.25 = un cuarto; 0.02..4), y prendida, z tambien fuera del editor (partido /
//   repeticion). La DLL escribe w = pasos del visor del editor frenados en el ultimo segundo (0 = el visor no esta corriendo).
#include "core.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

namespace fx {

static const unsigned kStamp = 0x62787C6A;
static const uintptr_t kStep = 0x14035dcb0ull;   // DeltaGameTimeHolder: paso por cuadro (partido)
// prologo: mov [rsp+10h],rbx; mov [rsp+18h],r8; push rdi; sub rsp,40h; movaps [rsp+30h],xmm6
static const unsigned char kStepSig[20] = { 0x48,0x89,0x5c,0x24,0x10,0x4c,0x89,0x44,0x24,0x18,0x57,0x48,0x83,0xec,0x40,0x0f,0x29,0x74,0x24,0x30 };
static const unsigned char kStepSig2[5] = { 0xf3,0x0f,0x10,0x77,0x30 };                 // 0x14035dcee movss xmm6,[rdi+30h]
static const unsigned char kStepSig3[7] = { 0xc7,0x47,0x30,0x00,0x00,0x80,0x3f };       // 0x14035dcfb mov dword [rdi+30h],1.0f

static const uintptr_t kSetStep = 0x141afd270ull;  // BodyAnimControl::SetStep (visor del editor)
// mov rax,[rcx+88h]; test rax,rax; jz +11h; mulss xmm1,[rip+...] (300.0); mov rax,[rax+10h]; movss [rax+44h],xmm1; ret
static const unsigned char kSetStepSig[30] = { 0x48,0x8b,0x81,0x88,0x00,0x00,0x00,0x48,0x85,0xc0,0x74,0x11,0xf3,0x0f,0x59,0x0d,0xc8,0xe4,0xaa,0x00,
                                               0x48,0x8b,0x40,0x10,0xf3,0x0f,0x11,0x48,0x44,0xc3 };
static const uintptr_t kBoneLo = 0x1423f2ca0ull, kBoneHi = 0x1423f3a60ull;   // draw::ModelViewBoneUpdator
static const unsigned char kBoneSig[20] = { 0x40,0x53,0x57,0x48,0x83,0xec,0x48,0x0f,0x29,0x74,0x24,0x20,0x48,0x8b,0xda,0x48,0x8b,0xf9,0xe8,0xc9 };

typedef void (*StepFn)(void* holder, float delta, void* r8);
typedef void (*SetStepFn)(void* ctrl, float step);
static StepFn o_step = nullptr;
static SetStepFn o_setStep = nullptr;
static bool s_tried = false, s_okMatch = false, s_okEditor = false;
static std::atomic<int> s_calls{0}, s_editorCalls{0}, s_editorScaled{0};
static std::atomic<float> s_gameScale{1.0f}, s_applied{1.0f}, s_lastDelta{0.0f}, s_editorStep{0.0f};

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
static bool MatchToo() { return g_cfg.live[343][2] > 0.5f; }

// partido: escala del reloj de juego (solo con z343 = 1)
static void hk_Step(void* holder, float delta, void* r8) {
    float* sc = (float*)((char*)holder + 0x30);
    const float g = *sc;
    const float f = MatchToo() ? Factor() : 1.0f;
    if (f != 1.0f) *sc = g * f;
    s_gameScale = g; s_applied = *sc; s_lastDelta = delta; s_calls++;
    o_step(holder, delta, r8);
}

// visor del editor: paso de la animacion por cuadro (solo desde ModelViewBoneUpdator)
static void hk_SetStep(void* ctrl, float step) {
    const uintptr_t ret = (uintptr_t)_ReturnAddress();
    if (ret >= kBoneLo && ret < kBoneHi) {
        s_editorCalls++;
        const float f = Factor();
        if (f != 1.0f) { step *= f; s_editorScaled++; }
        s_editorStep = step;
    }
    o_setStep(ctrl, step);
}

static void Install() {
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("camara lenta: parches en memoria apagados (motor = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) {
        Log("camara lenta: exe distinto (base %p, sello %08X): sin gancho", (void*)mod, (unsigned)nt->FileHeader.TimeDateStamp); return;
    }
    if (!SigAt(kSetStep, kSetStepSig, sizeof kSetStepSig) || !SigAt(kBoneLo, kBoneSig, sizeof kBoneSig))
        Log("camara lenta: la firma de 0x141afd270 / 0x1423f2ca0 (visor del editor) no coincide, se omite");
    else if (MH_CreateHook((void*)kSetStep, (void*)&hk_SetStep, (void**)&o_setStep) == MH_OK && MH_EnableHook((void*)kSetStep) == MH_OK) s_okEditor = true;
    if (!SigAt(kStep, kStepSig, sizeof kStepSig) || !SigAt(0x14035dceeull, kStepSig2, sizeof kStepSig2) || !SigAt(0x14035dcfbull, kStepSig3, sizeof kStepSig3))
        Log("camara lenta: la firma de 0x14035dcb0 (reloj del partido) no coincide, se omite");
    else if (MH_CreateHook((void*)kStep, (void*)&hk_Step, (void**)&o_step) == MH_OK && MH_EnableHook((void*)kStep) == MH_OK) s_okMatch = true;
    Log("camara lenta: visor del editor (0x141afd270 desde ModelViewBoneUpdator) %s; reloj del partido (0x14035dcb0) %s; velocidad x343 = %.2f, %s, partido %s",
        s_okEditor ? "enganchado" : "NO enganchado", s_okMatch ? "enganchado" : "NO enganchado",
        g_cfg.live[343][0], g_cfg.live[343][1] > 0.5f ? "prendida" : "apagada", MatchToo() ? "tambien" : "no (solo el editor)");
}

bool TiempoHooked() { return s_okEditor || s_okMatch; }
float TiempoFactor() { return Factor(); }

void TiempoPresent() {
    if (!s_tried) Install();
    // anotar los cambios de velocidad (menu / tecla), aunque no haya gancho (para ver en el log que la tecla disparo)
    static float s_lastF = -1.0f; static double s_t = 0; static int s_c0 = 0, s_e0 = 0, s_s0 = 0; static bool s_lastMatch = false;
    const float f = Factor(); const bool m = MatchToo();
    if (f != s_lastF || m != s_lastMatch) {
        if (s_lastF >= 0) Log("camara lenta: velocidad x%.2f (%s)%s%s", f, f == 1.0f ? "normal" : f < 1.0f ? "mas lenta" : "mas rapida",
                              m ? ", tambien en el partido" : ", solo en el visor del editor", TiempoHooked() ? "" : " - SIN GANCHO: no tiene efecto");
        s_lastF = f; s_lastMatch = m;
    }
    double t = Now();
    if (s_t == 0) { s_t = t; s_c0 = s_calls.load(); s_e0 = s_editorCalls.load(); s_s0 = s_editorScaled.load(); return; }
    // w343 = pasos del visor frenados en el ultimo segundo (para la tarjeta / el log del RC)
    static double s_t1 = 0; static int s_s1 = 0;
    if (s_t1 == 0) { s_t1 = t; s_s1 = s_editorScaled.load(); }
    else if (t - s_t1 >= 1.0) { g_cfg.live[343][3] = (float)(s_editorScaled.load() - s_s1); s_t1 = t; s_s1 = s_editorScaled.load(); }
    if (t - s_t < 30.0) return;
    const int e = s_editorCalls.load() - s_e0, sc = s_editorScaled.load() - s_s0;
    if (e > 0 || f != 1.0f || s_gameScale.load() != 1.0f)
        Log("camara lenta (30 s): visor del editor %d pasos de animacion (%d frenados, ultimo paso %.4f s); reloj del partido %d pasos, delta %.4f s, escala del juego %.2f, aplicada %.2f",
            e, sc, s_editorStep.load(), s_calls.load() - s_c0, s_lastDelta.load(), s_gameScale.load(), s_applied.load());
    s_t = t; s_c0 = s_calls.load(); s_e0 = s_editorCalls.load(); s_s0 = s_editorScaled.load();
}

} // namespace fx
