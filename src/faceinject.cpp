// PES21 FX - gancho de las matrices del rig (09/11 6nmngk, Agustin: "segui buscando las funciones" -> "dale"). PASO 1: SOLO MIDE.
//
// Decompilado (/mnt/project-files/expresiones/inyeccion_huesos_cara.md): SceneCharacterNode::vf2 (0x141a269f0) arma cada cuadro
//   cuerpo (si nodo+0xf0): restricciones 0x1419f7a90 (aca esta el Eval del cuello donde escribe exprbones.cpp) -> matrices 0x1419f8820
//   manos y cara (si nodo+0x34 y nodo+0x158 == 2): enganche 0x1419f8390 (copia huesos del padre), restricciones de la cara solo si
//   nodo+0xf1 (mirada, parpados, labios por fuera, papada), y al final 0x1419f8820(cara) = lo ultimo antes de dibujar.
// 0x1419f8820(rig): VTransform en rig+0x50 (cantidad en rig+0x58, paso 0x30) -> VMatrix4 en rig+0x78 (paso 0x40); padre en
// rig+0x120. FaceRig = vtable 0x142ab4560, CharacterRig = 0x142aa4cd0.
//
// Este paso no cambia nada: cuenta llamadas por tipo de rig, hilos, caras distintas, cuadros con cara armada, y si el cuerpo
// padre de cada cara es un personaje conocido (personajes.cpp). Log "inyeccion caras:" cada 10 s (los primeros 2 min) y cada
// 60 s despues. [PES21FX] expr_inyeccion = 0 no pone el gancho. Paso 2 (despues del log): escribir la pose del mod en rig+0x50
// antes del original, detras de expr_inyeccion = 2.
#include "core.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <cstdio>

namespace fx {

namespace {
const unsigned kStamp = 0x62787C6A;
const uintptr_t kRigMatrices = 0x1419f8820ull;
const uintptr_t kVtFaceRig = 0x142ab4560ull, kVtCharRig = 0x142aa4cd0ull;

typedef uintptr_t (__fastcall* RigFn)(void* rig);
RigFn o_rig = nullptr;
bool s_tried = false, s_ok = false;

std::atomic<unsigned> s_face{0}, s_body{0}, s_bodyKnown{0}, s_other{0}, s_faceKnown{0}, s_faceFrames{0}, s_bad{0};
std::atomic<int> s_lastFaceFrame{-1};
std::atomic<unsigned> s_tid[8];            // hilos distintos que arman caras
std::atomic<uintptr_t> s_faces[64];        // caras distintas en la ventana
std::atomic<unsigned long long> s_totFace{0}, s_totBody{0};
// primera cara y primer cuerpo vistos: tamanos (una vez)
std::atomic<bool> s_infoFace{false}, s_infoBody{false};
char s_infoF[200], s_infoB[200];
std::atomic<bool> s_infoFReady{false}, s_infoBReady{false};

struct RigPeek { uintptr_t vt, parent, xfCount, bones; };
bool Peek(const void* rig, RigPeek& p) {
    __try {
        const char* r = (const char*)rig;
        p.vt = *(const volatile uintptr_t*)r;
        p.parent = *(const volatile uintptr_t*)(r + 0x120);
        p.xfCount = *(const volatile uintptr_t*)(r + 0x58);
        p.bones = *(const volatile unsigned*)(r + 0x30);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void NoteTid() {
    unsigned t = GetCurrentThreadId();
    for (auto& s : s_tid) {
        unsigned v = s.load(std::memory_order_relaxed);
        if (v == t) return;
        if (v == 0) { unsigned z = 0; if (s.compare_exchange_strong(z, t) || z == t) return; }
    }
}
void NoteFace(uintptr_t rig) {
    for (auto& s : s_faces) {
        uintptr_t v = s.load(std::memory_order_relaxed);
        if (v == rig) return;
        if (v == 0) { uintptr_t z = 0; if (s.compare_exchange_strong(z, rig) || z == rig) return; }
    }
}

uintptr_t __fastcall hk_Rig(void* rig) {
    RigPeek p{};
    if (rig && Peek(rig, p)) {
        if (p.vt == kVtFaceRig) {
            s_face.fetch_add(1, std::memory_order_relaxed);
            NoteTid(); NoteFace((uintptr_t)rig);
            int f = PresentNo();
            if (s_lastFaceFrame.exchange(f) != f) s_faceFrames.fetch_add(1, std::memory_order_relaxed);
            if (p.parent && CharSlotForSkel((const void*)p.parent) >= 0) s_faceKnown.fetch_add(1, std::memory_order_relaxed);
            if (g_cfg.optExprInject >= 2) ExprBonesReapply(rig);   // (10/11) paso 2: lo del mod queda ultimo
            if (!s_infoFace.exchange(true)) {
                snprintf(s_infoF, sizeof s_infoF, "primera cara %p: %llu transformaciones, %llu huesos, padre %p (personaje %d), hilo %lu", rig,
                         (unsigned long long)p.xfCount, (unsigned long long)p.bones, (void*)p.parent, p.parent ? CharSlotForSkel((const void*)p.parent) : -1, GetCurrentThreadId());
                s_infoFReady = true;
            }
        } else if (p.vt == kVtCharRig) {
            s_body.fetch_add(1, std::memory_order_relaxed);
            int slot = CharSlotForSkel(rig);
            if (slot >= 0) {
                if (g_cfg.optExprInject >= 2) ExprBonesReapply(rig);
                s_bodyKnown.fetch_add(1, std::memory_order_relaxed);
                if (!s_infoBody.exchange(true)) {
                    snprintf(s_infoB, sizeof s_infoB, "primer cuerpo de personaje %p: %llu transformaciones, %llu huesos, personaje %d, hilo %lu", rig,
                             (unsigned long long)p.xfCount, (unsigned long long)p.bones, slot, GetCurrentThreadId());
                    s_infoBReady = true;
                }
            }
        } else s_other.fetch_add(1, std::memory_order_relaxed);
    } else s_bad.fetch_add(1, std::memory_order_relaxed);
    return o_rig(rig);
}

void Install() {
    s_tried = true;
    if (g_cfg.optExprInject <= 0) { Log("inyeccion caras: apagado (expr_inyeccion = 0)"); return; }
    if (!g_cfg.motorPatch) { Log("inyeccion caras: parches en memoria apagados (motor = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("inyeccion caras: exe distinto, sin gancho"); return; }
    unsigned char b[16] = {};
    __try { memcpy(b, (const void*)kRigMatrices, sizeof b); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("inyeccion caras: no se puede leer 0x1419f8820"); return; }
    char hex[64]; for (int i = 0; i < 16; i++) snprintf(hex + i * 3, 4, "%02X ", b[i]);
    // otro gancho ya puesto (salto) o relleno: no se engancha encima
    if (b[0] == 0xE9 || b[0] == 0xCC || b[0] == 0xC3) { Log("inyeccion caras: 0x1419f8820 empieza raro (%s), sin gancho", hex); return; }
    if (MH_CreateHook((void*)kRigMatrices, (void*)&hk_Rig, (void**)&o_rig) == MH_OK && MH_EnableHook((void*)kRigMatrices) == MH_OK) s_ok = true;
    Log("inyeccion caras: gancho en 0x1419f8820 (matrices del rig) %s; bytes %s(hilo de Present %lu)", s_ok ? g_cfg.optExprInject >= 2 ? "puesto, lo del mod queda ultimo" : "puesto, solo mide" : "NO", hex, GetCurrentThreadId());
}
}  // namespace

void FaceInjectPresent() {
    if (!s_tried) { Install(); return; }
    if (!s_ok) return;
    if (s_infoFReady.exchange(false)) Log("inyeccion caras: %s", s_infoF);
    if (s_infoBReady.exchange(false)) Log("inyeccion caras: %s", s_infoB);
    static ULONGLONG t0 = GetTickCount64(), last = t0; static int f0 = PresentNo();
    ULONGLONG n = GetTickCount64();
    ULONGLONG every = (n - t0 < 120000) ? 10000 : 60000;
    if (n - last < every) return;
    double sec = (n - last) / 1000.0; last = n;
    int f = PresentNo(); int frames = f - f0; f0 = f;
    unsigned fc = s_face.exchange(0), bd = s_body.exchange(0), bk = s_bodyKnown.exchange(0), ot = s_other.exchange(0), fk = s_faceKnown.exchange(0), ff = s_faceFrames.exchange(0), bad = s_bad.exchange(0);
    s_totFace += fc; s_totBody += bd;
    int nf = 0; for (auto& s : s_faces) if (s.exchange(0)) nf++;
    char tids[96] = {}; int nt = 0; size_t o = 0;
    for (auto& s : s_tid) { unsigned v = s.exchange(0); if (v) { nt++; o += snprintf(tids + o, sizeof tids - o, "%s%u", o ? "," : "", v); if (o >= sizeof tids) break; } }
    if (!fc && !bd && !ot) { Log("inyeccion caras: %.0f s sin rigs (menu o carga)", sec); return; }
    extern std::atomic<unsigned> g_exprReapplied, g_exprReapplyBusy;
    const unsigned rap = g_exprReapplied.exchange(0), rbz = g_exprReapplyBusy.exchange(0);
    Log("inyeccion caras: reaplicado %u huesos (ocupado %u) [expr_inyeccion %d]", rap, rbz, g_cfg.optExprInject);
    Log("inyeccion caras: %.0f s, %d cuadros: caras %u (%d distintas, en %u cuadros = %.0f %%, %.1f por cara y cuadro; padre conocido %u), "
        "cuerpos %u (de personaje %u), otros rigs %u, ilegibles %u; hilos de la cara %d [%s]",
        sec, frames, fc, nf, ff, frames > 0 ? 100.0 * ff / frames : 0.0, (nf && ff) ? (double)fc / ((double)nf * ff) : 0.0, fk, bd, bk, ot, bad, nt, tids);
}

}  // namespace fx
