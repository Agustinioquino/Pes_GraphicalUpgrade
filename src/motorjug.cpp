// (17/11 3icxbh) Motor en tiempo real: mover / girar / escalar al jugador y elegirle animacion (poses y festejos del juego).
// Agustin 17/11 03:00: "la idea es que pueda mover el jugador con opciones"; 03:01: "cargar mas jugadores moverlos y la pestana de
// animaciones"; 03:06: "opciones de mover rotar", "escalar". Esta es la primera parte: el jugador que ya esta en el Motor. Los
// jugadores propios del Motor (varios, sin el visor del Editar) van despues: aca se anota en el registro lo que hace falta saber
// para crearlos (hilos y recursos de animacion fuera del partido).
//
// Leido en el C (c_142332744.c, c_141daa3e0.c, c_141ae8400.c) y en los bytes del exe 1.07.02 (leidos en la PC el 17/11):
//   draw::ModelViewBoneUpdator::vf2 0x1423f2ca0(upd, buf): la pose por cuadro del jugador del visor. upd+0x18 = controlador del clip
//   de cuerpo (upd+0x20 si upd+0xc8 == 1), upd+0xc0 pose, +0xc4 id, +0xcc reproducir, +0xcd / +0xcf / +0xd1 quieto (cuerpo, cara,
//   manos), +0xd2 espejo. Controlador: R = [ctl+0x188]; R+0xa0 / +0xb0 / +0xc0 = las tres filas del giro (FUN_141afccc0 escribe
//   (cos, 0, -sin), (0, 1, 0), (sin, 0, cos) y deja el cuarto valor de cada fila), R+0xd0 = posicion x, y, z (FUN_141afd060).
//   Son datos: aca se escriben igual que esas dos funciones, sin llamar al juego.
//   Tick del visor draw::util::TaskModelView::Impl 0x141ee7530(impl): maquina de estados del modelo (toma el candado impl+0x510).
//   Animacion: 0x141f061a0(mv, pose 0..0x2c, float, c4, c5, c6) y 0x141f06160(mv, festejo 0x2c..0xcd, float, c4, c5, c6), las que usan
//   los menus del Editar (-> Impl 0x141ee9360 / 0x141ee9140: float = impl+0x3dc, c4 = +0x3f1 reproducir, c5 = +0x3f0, c6 = +0x3f2
//   espejo). Los festejos se cargan solos al pedirlos (FUN_1423f1790 -> FUN_141ecf110 -> FUN_141ecfe30) y el visor espera.
//   mv = edit::ModelView [0x1438040d0]; Impl = [[mv+0x78]+0x90].
//
// Solo actua con el estudio del Motor abierto ([PES21FX] motor_jugador_mover = 1, de fabrica 1). Al cerrar el Motor el jugador
// vuelve a su lugar, giro y tamano, y no se toca nada del Editar.
#include "core.h"
#include <MinHook.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace fx {

static const uintptr_t kBoneVf2 = 0x1423f2ca0ull, kImplTick = 0x141ee7530ull, kMV = 0x1438040d0ull, kSetPose = 0x141f061a0ull, kSetCeleb = 0x141f06160ull;
static const uintptr_t kAnimResMgr = 0x141ed1770ull, kAnimResFind = 0x141ed1700ull, kFaceResPtr = 0x143802ff8ull, kHandResPtr = 0x143803000ull;
static const uintptr_t kYawCmd = 0x1419e19f0ull, kPosCmd = 0x1419e1980ull, kBoneIdxTab = 0x1438032e0ull;   // ordenes 0x516 / 0x517 al modelo; tabla de FUN_141ec54f0
static const unsigned kStamp = 0x62787C6A;
static const unsigned char kBoneSig[] = { 0x40, 0x53, 0x57, 0x48, 0x83, 0xec, 0x48, 0x0f, 0x29, 0x74, 0x24, 0x20, 0x48, 0x8b, 0xda, 0x48, 0x8b, 0xf9 };
static const unsigned char kTickSig[] = { 0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x89, 0x5c, 0x24, 0x50, 0x48, 0x89, 0x74, 0x24, 0x58, 0x48, 0x8b, 0xd9, 0x48, 0x8d, 0xb9, 0x10, 0x05, 0x00, 0x00 };
static const unsigned char kPoseSig[] = { 0x48, 0x83, 0xec, 0x38, 0x89, 0x91, 0x00, 0x01, 0x00, 0x00, 0x48, 0x8b, 0x49, 0x78, 0x48, 0x85, 0xc9, 0x75, 0x07, 0x32, 0xc0 };
static const unsigned char kCelebSig[] = { 0x48, 0x83, 0xec, 0x38, 0x48, 0x8b, 0x49, 0x78, 0x48, 0x85, 0xc9, 0x75, 0x07, 0x32, 0xc0, 0x48, 0x83, 0xc4, 0x38, 0xc3 };

static const unsigned char kYawSig[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf9, 0x8b, 0xda, 0xb9, 0x18, 0x00, 0x00, 0x00, 0xe8 };
static const unsigned char kPosSig[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf9, 0x48, 0x8b, 0xda, 0xb9, 0x20, 0x00, 0x00, 0x00, 0xe8 };
typedef void(__fastcall* PFN_Yaw)(uintptr_t, uint32_t); typedef void(__fastcall* PFN_Pos)(uintptr_t, const float*);
static PFN_Yaw o_yaw = nullptr; static PFN_Pos o_pos = nullptr; static bool s_okCmd = false;
typedef void(__fastcall* PFN_Bone)(uintptr_t, uintptr_t);
typedef uint64_t(__fastcall* PFN_Tick)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef uint8_t(__fastcall* PFN_Anim)(uintptr_t, int, float, uint8_t, uint8_t, uint8_t);
static PFN_Bone o_bone = nullptr; static PFN_Tick o_tick = nullptr;
static bool s_tried = false, s_okBone = false, s_okTick = false, s_okAnim = false;
static std::atomic<bool> s_on{false};          // Motor abierto y opcion prendida (lo pone Present)
// lo que pide el editor (Present) y lee el hilo del juego
static std::atomic<float> s_uPos[3], s_uYaw{0.0f}, s_uScale{1.0f};
static std::atomic<bool> s_uFix{false}, s_uPause{false};
static std::atomic<int> s_aKind{0}, s_aId{0x2b}, s_aSeq{0}, s_aSt{0}; static std::atomic<bool> s_aMirror{false};
static std::atomic<bool> s_seen{false};
// solo el hilo que corre vf2
static uintptr_t s_upd = 0; static bool s_applied = false, s_paused = false;
static int s_aDone = 0;                         // solo el hilo del tick

static inline bool PtrOk(uintptr_t a) { return a >= 0x10000ull && a < 0x7ffffffff000ull && (a & 3) == 0; }
static bool RdP(uintptr_t a, uintptr_t* v) { __try { *v = *(uintptr_t*)a; return true; } __except (1) { return false; } }

// (17/11 b, prueba de Agustin 03:46 con 9C6E2EEA: "el giro si funciona la posicion no y el tamano tampoco"; 03:48: "se ve un poco de
// stuttering") Leido de nuevo en el C: el visor NO usa la posicion ni el tamano de la raiz del controlador. FUN_1423f3430 (dentro de
// vf2) le manda al modelo dos ordenes por cuadro: 0x516 FUN_1419e19f0(modelo = [upd+0xb0], giro en radianes = upd+0xb8) y 0x517
// FUN_1419e1980(modelo, posicion x y z) con el desplazamiento del clip. Ahora se enganchan esas dos funciones y, SOLO mientras corre
// vf2 del jugador del Motor (marca por hilo), se les suma el giro y el lugar pedidos. Ya no se escribe nada en el controlador del clip
// en cada cuadro (cada pose crea un controlador nuevo y la escritura por cuadro peleaba con el juego: posible causa del tiron).
// Tamano: el juego pone la estatura como escala del hueso raiz "root_main" (FUN_1423f2a70: FUN_141afa3c0(ctl, FUN_141ec54f0(0x13), s)
// + FUN_141afa690(ctl, s)); son datos, aca se escriben igual (estatura del juego x escala pedida) y solo cuando cambian.
static thread_local uintptr_t t_obj = 0; static thread_local bool t_posSent = false;
static std::atomic<float> s_wPos[3], s_rootSc{1.0f};      // lugar final mandado al modelo y escala total de la raiz (para los iconos del piso y la vista de huesos)
static bool ScaleRead(uintptr_t ctl, int* idx, float* cur) {
    __try {
        const int i = *(const int*)(kBoneIdxTab + 0x13 * 4); const uintptr_t R = *(uintptr_t*)(ctl + 0x188), arr = *(uintptr_t*)(ctl + 0x298);
        if (!PtrOk(R) || !PtrOk(arr) || i < 0 || i >= *(int*)(R + 0xf8)) return false;
        *idx = i; *cur = *(float*)(arr + (size_t)i * 4); return *cur == *cur && *cur > 0.01f && *cur < 100.0f;
    } __except (1) { return false; }
}
static bool ScaleWrite(uintptr_t ctl, int i, float s) {      // lo mismo que FUN_141afa3c0(ctl, i, s) + FUN_141afa690(ctl, s)
    __try {
        const uintptr_t a = *(uintptr_t*)(ctl + 0x180); if (!PtrOk(a)) return false;
        const uintptr_t b = *(uintptr_t*)(a + 0x20); if (!PtrOk(b)) return false;
        const uintptr_t rest = *(uintptr_t*)(b + 0xa0 + 0x50), R = *(uintptr_t*)(ctl + 0x188); if (!PtrOk(rest) || !PtrOk(R)) return false;
        const uintptr_t cur = *(uintptr_t*)(R + 0xf0), arr = *(uintptr_t*)(ctl + 0x298), ex = *(uintptr_t*)(ctl + 0xf0);
        if (!PtrOk(cur) || !PtrOk(arr) || !PtrOk(ex) || i < 0 || i >= *(int*)(R + 0xf8) || !(s > 0.0f)) return false;
        const float* src = (const float*)(rest + 0x10 + (size_t)i * 0x20); float* dst = (float*)(cur + 0x10 + (size_t)i * 0x20);
        for (int k = 0; k < 4; k++) dst[k] = s * src[k];
        *(float*)(arr + (size_t)i * 4) = s; if (i == 0) *(float*)(ctl + 0x2b4) = 1.0f / s;
        *(float*)(ex + 0xc0) = s;
        return true;
    } __except (1) { return false; }
}
static bool UpdCtl(uintptr_t upd, uintptr_t* ctl, uintptr_t* obj) {
    __try { const int kind = *(int*)(upd + 0xc8); *ctl = *(uintptr_t*)(upd + (kind == 1 ? 0x20 : 0x18)); *obj = *(uintptr_t*)(upd + 0xb0); return PtrOk(*obj); } __except (1) { return false; }
}
// quieto / en movimiento: los mismos tres bytes que usa el juego (cuerpo, cara, manos); al soltar vuelven a lo que pide +0xcc / +0xce / +0xd0
static void UpdPause(uintptr_t upd, bool on) {
    __try { unsigned char* u = (unsigned char*)upd;
        if (on) { u[0xcd] = 1; u[0xcf] = 1; u[0xd1] = 1; }
        else { u[0xcd] = u[0xcc] == 0; u[0xcf] = u[0xce] == 0; u[0xd1] = u[0xd0] == 0; }
    } __except (1) {}
}
static void __fastcall hk_Yaw(uintptr_t obj, uint32_t bits) {
    if (t_obj && obj == t_obj) { float a; memcpy(&a, &bits, 4); const float add = s_uYaw.load(std::memory_order_relaxed) * 0.017453292f; if (a == a && add != 0.0f) { a += add; memcpy(&bits, &a, 4); } }
    o_yaw(obj, bits);
}
static void __fastcall hk_Pos(uintptr_t obj, const float* v) {
    if (t_obj && obj == t_obj && v) {
        // el desplazamiento del clip gira con el jugador (sentido del giro del modelo INFERIDO: +angulo lleva +z hacia +x) y despues se suma el lugar pedido
        const float a = s_uYaw.load(std::memory_order_relaxed) * 0.017453292f, c = cosf(a), s = sinf(a);
        const float k = s_uFix.load(std::memory_order_relaxed) ? 0.0f : 1.0f;      // "fijo en su lugar": sin el desplazamiento del clip
        float w[4] = { k * (v[0] * c + v[2] * s) + s_uPos[0].load(), k * v[1] + s_uPos[1].load(), k * (-v[0] * s + v[2] * c) + s_uPos[2].load(), 0.0f };
        for (int i = 0; i < 3; i++) s_wPos[i].store(w[i], std::memory_order_relaxed);
        t_posSent = true; o_pos(obj, w); return;
    }
    o_pos(obj, v);
}
static bool CallPos(uintptr_t obj, const float* w) { __try { o_pos(obj, w); return true; } __except (1) { return false; } }

static void __fastcall hk_Bone(uintptr_t upd, uintptr_t buf) {
    static std::atomic<int> nl{0};
    static uintptr_t s_scCtl = 0; static float s_scBase = 1.0f, s_scW = 0.0f; static bool s_ownPos = false; static ULONGLONG s_updT = 0;
    const bool on = s_on.load(std::memory_order_relaxed);
    uintptr_t ctl = 0, obj = 0;
    if (!((on || s_applied || s_paused) && s_okCmd && UpdCtl(upd, &ctl, &obj))) { o_bone(upd, buf); return; }
    // un solo jugador a la vez: si en el mismo cuadro corre otro actualizador (otro modelo con su pose), no se toca
    const ULONGLONG nowMs = GetTickCount64();
    if (s_upd && upd != s_upd && nowMs - s_updT < 100) { o_bone(upd, buf); return; }
    if (upd != s_upd && nl.fetch_add(1) < 6) Log("motor (jugador): jugador del visor %p, modelo %p, controlador %p (hilo %lu)", (void*)upd, (void*)obj, (void*)ctl, GetCurrentThreadId());
    s_upd = upd; s_updT = nowMs;
    if (on) {
        s_seen.store(true, std::memory_order_relaxed); s_applied = true;
        // tamano: estatura del juego x escala pedida, escrita solo cuando cambia (controlador nuevo en cada pose: el juego vuelve a poner la estatura)
        int bi = 0; float cur = 1.0f; const float sc = s_uScale.load(std::memory_order_relaxed);
        if (PtrOk(ctl) && ScaleRead(ctl, &bi, &cur)) {
            if (ctl != s_scCtl || fabsf(cur - s_scW) > 1e-5f) { s_scCtl = ctl; s_scBase = cur; s_scW = cur; static int n2 = 0; if (n2++ < 6) Log("motor (jugador): estatura del juego en la raiz (hueso %d): %.4f", bi, cur); }
            const float want = s_scBase * sc;
            if (fabsf(want - s_scW) > 1e-5f && ScaleWrite(ctl, bi, want)) s_scW = want;
            s_rootSc.store(s_scW, std::memory_order_relaxed);
        }
        const bool pz = s_uPause.load(std::memory_order_relaxed);
        if (pz || s_paused) { UpdPause(upd, pz); s_paused = pz; }
        t_obj = obj; t_posSent = false;
        o_bone(upd, buf);
        t_obj = 0;
        if (!t_posSent) {      // este cuadro el juego no mando el lugar (clip sin desplazamiento): se manda el pedido, y al volver a cero una vez mas
            float w[4] = { s_uPos[0].load(), s_uPos[1].load(), s_uPos[2].load(), 0.0f }; const bool any = w[0] != 0.0f || w[1] != 0.0f || w[2] != 0.0f;
            if (any || s_ownPos) { CallPos(obj, w); s_ownPos = any; }
            for (int i = 0; i < 3; i++) s_wPos[i].store(w[i], std::memory_order_relaxed);
        }
        return;
    }
    // Motor cerrado: todo como lo deja el juego (el giro y el lugar vuelven solos: ya no se les suma nada)
    if (s_scCtl == ctl && PtrOk(ctl) && fabsf(s_scW - s_scBase) > 1e-5f) { int bi = 0; float cur = 1.0f; if (ScaleRead(ctl, &bi, &cur) && fabsf(cur - s_scW) < 1e-4f) ScaleWrite(ctl, bi, s_scBase); }
    if (s_paused) UpdPause(upd, false);
    o_bone(upd, buf);
    if (s_ownPos) { const float z[4] = {}; CallPos(obj, z); s_ownPos = false; }
    Log("motor (jugador): Motor cerrado: el jugador vuelve a su lugar, giro y tamano");
    s_applied = false; s_paused = false; s_upd = 0; s_scCtl = 0; s_scW = 0.0f; s_rootSc.store(1.0f);
}

// --- lo que hace falta saber para crear jugadores propios del Motor fuera de un partido (solo anota, una vez por sesion) ---
struct GStr { union { char buf[16]; char* ptr; }; size_t size, cap; };
typedef void*(__fastcall* PtrFn)(); typedef void*(__fastcall* AnimResFindFn)(void*, void*);
static void* FixdemoRes() {
    __try { GStr s; memset(&s, 0, sizeof s); memcpy(s.buf, "fixdemo", 8); s.size = 7; s.cap = 15; void* mgr = ((PtrFn)kAnimResMgr)(); return mgr ? ((AnimResFindFn)kAnimResFind)(mgr, &s) : nullptr; } __except (1) { return (void*)1; }
}
static void DiagOnce(uintptr_t impl) {
    static bool done = false; if (done) return; done = true;
    uintptr_t fr = 0, hr = 0; RdP(kFaceResPtr, &fr); RdP(kHandResPtr, &hr);
    void* fx = FixdemoRes();
    char hex[0xa0 * 3 + 4] = {}; unsigned char lid[0xa0] = {}; bool ok = false;
    __try { memcpy(lid, (const void*)(impl + 0x10), sizeof lid); ok = true; } __except (1) {}
    if (ok) for (int i = 0; i < 0xa0; i++) snprintf(hex + i * 3, 4, "%02x ", lid[i]);
    Log("motor (jugador): datos para los jugadores propios: tick del visor en el hilo %lu; recurso de animacion \"fixdemo\" %p%s; recursos de cara %p y manos %p", GetCurrentThreadId(), fx, fx == (void*)1 ? " (error al buscarlo)" : fx ? "" : " (no esta cargado fuera del partido)", (void*)fr, (void*)hr);
    if (ok) Log("motor (jugador): LoadId del jugador del visor (impl+0x10, 0xa0 B): %s", hex);
}

static bool CallAnim(uintptr_t fn, uintptr_t mv, int id, bool mirror, uint8_t* ret) {
    __try { *ret = ((PFN_Anim)fn)(mv, id, 0.0f, 1, 1, mirror ? 1 : 0); return true; } __except (1) { return false; }
}
static uint64_t __fastcall hk_Tick(uintptr_t impl, uintptr_t a, uintptr_t b, uintptr_t c) {
    if (s_on.load(std::memory_order_relaxed)) {
        DiagOnce(impl);
        const int seq = s_aSeq.load(std::memory_order_relaxed);
        if (seq != s_aDone && s_okAnim) {
            s_aDone = seq;
            uintptr_t mv = 0, task = 0, im = 0;
            if (RdP(kMV, &mv) && PtrOk(mv) && RdP(mv + 0x78, &task) && PtrOk(task) && RdP(task + 0x90, &im) && im == impl) {
                const int kind = s_aKind.load(), id = s_aId.load(); const bool mir = s_aMirror.load(); uint8_t r = 0;
                const bool okId = kind == 1 ? (id >= 0x2c && id <= 0xcd) : (id >= 0 && id <= 0x2c);
                if (okId && CallAnim(kind == 1 ? kSetCeleb : kSetPose, mv, id, mir, &r) && r) { s_aSt.store(2); Log("motor (jugador): animacion pedida al visor: %s %d%s", kind == 1 ? "festejo" : "pose", kind == 1 ? id - 0x2b : id, mir ? " (espejo)" : ""); }
                else { s_aSt.store(3); Log("motor (jugador): el visor no acepto la animacion (%s, id %d)", kind == 1 ? "festejo" : "pose", id); }
            } else { s_aSt.store(3); Log("motor (jugador): animacion sin poner: este tick no es el del visor del Editar (impl %p)", (void*)impl); }
        }
    }
    return o_tick(impl, a, b, c);
}

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static bool SigOk(uintptr_t a, const unsigned char* sig, size_t n) { return Readable((const void*)a, n) && memcmp((const void*)a, sig, n) == 0; }
static void Install() {
    s_tried = true;
    if (!g_cfg.motorPatch || !g_cfg.motorJug) { Log("motor (jugador): apagado (motor = 0 o motor_jugador_mover = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("motor (jugador): exe distinto: no se engancha"); return; }
    if (SigOk(kBoneVf2, kBoneSig, sizeof kBoneSig)) { if (MH_CreateHook((void*)kBoneVf2, (void*)&hk_Bone, (void**)&o_bone) == MH_OK && MH_EnableHook((void*)kBoneVf2) == MH_OK) s_okBone = true; }
    else Log("motor (jugador): la firma de 0x1423f2ca0 no coincide: mover / girar queda apagado");
    if (s_okBone) {
        if (SigOk(kYawCmd, kYawSig, sizeof kYawSig) && SigOk(kPosCmd, kPosSig, sizeof kPosSig)) {
            if (MH_CreateHook((void*)kYawCmd, (void*)&hk_Yaw, (void**)&o_yaw) == MH_OK && MH_CreateHook((void*)kPosCmd, (void*)&hk_Pos, (void**)&o_pos) == MH_OK && MH_EnableHook((void*)kYawCmd) == MH_OK && MH_EnableHook((void*)kPosCmd) == MH_OK) s_okCmd = true;
        } else Log("motor (jugador): la firma de las ordenes de giro / lugar del modelo (0x1419e19f0 / 0x1419e1980) no coincide: mover queda apagado");
    }
    s_okAnim = SigOk(kSetPose, kPoseSig, sizeof kPoseSig) && SigOk(kSetCeleb, kCelebSig, sizeof kCelebSig);
    if (SigOk(kImplTick, kTickSig, sizeof kTickSig)) { if (MH_CreateHook((void*)kImplTick, (void*)&hk_Tick, (void**)&o_tick) == MH_OK && MH_EnableHook((void*)kImplTick) == MH_OK) s_okTick = true; }
    else Log("motor (jugador): la firma de 0x141ee7530 no coincide: animaciones apagadas");
    Log("motor (jugador): pose por cuadro (0x1423f2ca0) %s; ordenes de giro y lugar del modelo %s; tick del visor (0x141ee7530) %s; funciones de animacion %s", s_okBone ? "enganchada" : "NO", s_okCmd ? "enganchadas" : "NO", s_okTick ? "enganchado" : "NO", s_okAnim ? "reconocidas" : "NO reconocidas");
}

// desde Present (motorgrid.cpp MotorGridPresent)
void MotorJugPresent() {
    const bool motor = MotorPageActive();
    if (motor && !s_tried) Install();
    s_on.store((s_okBone || s_okTick) && motor && g_cfg.motorJug != 0, std::memory_order_relaxed);
    if (!motor) s_seen.store(false, std::memory_order_relaxed);
    // (17/11 b, Agustin 03:46: "deberia renderizar la vista luz albedo como predeterminada"; 03:50: "al salir del motor que se quiten las
    // opciones de luces y las vistas") al entrar: vista Albedo. Al salir: vista Normal, sin alambre ni pantalla partida, sin las vistas
    // de cuerpo (519) ni el esqueleto (426), las luces que se tocaron en el Motor como estaban, y el jugador en su lugar.
    static bool s_was = false;
    if (motor != s_was) {
        s_was = motor;
        if (motor) { MotorViewSet(0, 5); Log("motor: al entrar, vista Albedo"); }
        else {
            for (int r = 0; r < 3; r++) MotorViewSet(r, 0);
            { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.live[519][0] = 0.0f; g_cfg.live[519][2] = 0.0f; g_cfg.live[426][0] = 0.0f; g_cfg.live[426][1] = 0.0f; }
            const int nl = MotorLightsRestore();
            MotorJugReset(); s_uPause.store(false);
            Log("motor: al salir, vista Normal, vistas de prueba apagadas y %d luces como estaban", nl);
        }
    }
}
void MotorJugGet(MotorJugState* s) {
    s->hooked = s_okBone; s->seen = s_seen.load(); for (int i = 0; i < 3; i++) s->pos[i] = s_uPos[i].load(); s->yawDeg = s_uYaw.load(); s->scale = s_uScale.load();
    s->kind = s_aKind.load(); s->id = s_aId.load(); s->pause = s_uPause.load(); s->mirror = s_aMirror.load(); s->fix = s_uFix.load(); s->animSt = (s_okTick && s_okAnim) ? s_aSt.load() : -1;
    for (int i = 0; i < 3; i++) s->wpos[i] = s_wPos[i].load(); s->rootScale = s_rootSc.load(); s->hooked = s_okBone && s_okCmd;
}
void MotorJugSet(const float* p, float yawDeg, float scale) {
    for (int i = 0; i < 3; i++) s_uPos[i].store(std::clamp(p[i], -200.0f, 200.0f)); s_uYaw.store(std::clamp(yawDeg, -720.0f, 720.0f)); s_uScale.store(std::clamp(scale, 0.05f, 10.0f));
}
void MotorJugSetFix(bool on) { s_uFix.store(on); }
void MotorJugPause(bool on) { s_uPause.store(on); }
void MotorJugAnim(int kind, int id, bool mirror) { s_aKind.store(kind == 1 ? 1 : 0); s_aId.store(id); s_aMirror.store(mirror); s_aSt.store(1); s_aSeq.fetch_add(1); }
void MotorJugReset() { const float z[3] = {}; MotorJugSet(z, 0.0f, 1.0f); }

} // namespace fx
