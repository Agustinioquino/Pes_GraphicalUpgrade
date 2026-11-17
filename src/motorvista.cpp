// (16/11 3icxbh) Motor en tiempo real: el jugador en la ESCENA PRINCIPAL, dibujado como en el partido.
// Agustin 16/11 18:24: "el jugador se renderiza en 2d pq se renderiza igual que el modo editar normal". En el Editar el jugador va en
// una escena aparte del visor (cobra::rd::RenderTargetContext: escena propia + camara + vista, imagen recortada por color y pegada
// sobre el menu). En el partido los jugadores van en la escena principal y los dibuja la vista "MainViewport" con todo el render.
//
// Leido en el C (hallazgos/20261115_render_target_segunda_vista.cpp.md + bytes del exe 1.07.02 leidos en la PC el 16/11):
//   RenderTargetContext_Cuadro 0x14198ba20(ctx), una vez por cuadro por cada visor: ctx+0x08..+0x10 lista de objetos, +0x30 escena
//   propia, +0x58 camara, +0x60 vista propia, +0x68 MainViewport, +0x81 cerrado, +0x84 pedidos. Con pedidos llama a cada objeto
//   vf 0x30(1) y vf 0x48(): el objeto se entrega a la escena que tiene en obj+0x20 (0x14198a390 / 0x1419c4e10 la ponen al agregarlo).
//   Escena principal = [0x14374d960]. Vista: +0x458 + tipo * 8 = mascara de instancias por tipo de plugin; el visor al crearse
//   apaga TODO el tipo 1 en MainViewport (modelos, sombras, luces, diferido, TAA...). 0x141688d70(vista, camara) pone la camara.
//
// Que hace (solo con el estudio del Motor abierto y [PES21FX] motor_escena = 1, de fabrica 1):
//   1) antes del cuadro del visor, cada objeto de su lista pasa a la escena principal (obj+0x20);
//   2) MainViewport vuelve a dibujar el tipo 1 y usa la camara del visor (la que mueve la camara libre);
//   3) glass.cpp deja de dibujar el fondo del menu para que se vea la vista principal.
// Al cerrar el Motor (o con motor_escena = 0) todo vuelve: objetos a la escena del visor, mascara y camara de antes.
#include "core.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>

namespace fx {

static const uintptr_t kCtxFrame = 0x14198ba20ull, kMainScene = 0x14374d960ull, kViewSetCam = 0x141688d70ull;
static const unsigned kStamp = 0x62787C6A;
static const unsigned char kCtxFrameSig[] = { 0x48, 0x89, 0x5c, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x99, 0x88, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xf9 };
static const unsigned char kSetCamSig[] = { 0x48 };   // 0x141688d70 = lea rcx,[rcx+0x4a0]; jmp (no se puede firmar largo: se comprueba abajo)

typedef void(__fastcall* PFN_CtxFrame)(uintptr_t);
typedef void(__fastcall* PFN_SetCam)(uintptr_t, uintptr_t);
static PFN_CtxFrame o_ctxFrame = nullptr;
static std::atomic<bool> s_want{false};      // lo pone el Present: Motor abierto y motor_escena = 1
static std::atomic<bool> s_live{false};      // ya movido (para glass.cpp: no dibujar el fondo del menu)
static bool s_tried = false, s_ok = false;
// estado guardado (solo se toca en el hilo que corre el cuadro del visor)
static uintptr_t s_ctx = 0, s_main = 0, s_oldCam = 0; static uint64_t s_oldMask1 = 0; static bool s_moved = false;
static int s_nObjs = 0;

static inline bool PtrOk(uintptr_t a) { return a >= 0x10000ull && a < 0x7ffffffff000ull && (a & 7) == 0; }

static bool MoveObjs(uintptr_t ctx, uintptr_t scene, int* n) {
    __try {
        uintptr_t* b = *(uintptr_t**)(ctx + 0x08); uintptr_t* e = *(uintptr_t**)(ctx + 0x10); int k = 0;
        if (!PtrOk((uintptr_t)b) || !PtrOk((uintptr_t)e) || e < b || e - b > 4096) return false;
        for (uintptr_t* p = b; p != e; ++p) { const uintptr_t o = *p; if (PtrOk(o)) { *(uintptr_t*)(o + 0x20) = scene; k++; } }
        if (n) *n = k; return true;
    } __except (1) { return false; }
}
static bool ReadCtx(uintptr_t ctx, uintptr_t* own, uintptr_t* cam, uintptr_t* view, uintptr_t* main, unsigned* w, unsigned* h, bool* closed) {
    __try {
        *own = *(uintptr_t*)(ctx + 0x30); *cam = *(uintptr_t*)(ctx + 0x58); *view = *(uintptr_t*)(ctx + 0x60); *main = *(uintptr_t*)(ctx + 0x68);
        *closed = *(unsigned char*)(ctx + 0x81) != 0;
        *w = PtrOk(*view) ? *(unsigned*)(*view + 0x648) : 0; *h = PtrOk(*view) ? *(unsigned*)(*view + 0x64c) : 0;
        return PtrOk(*own) && PtrOk(*cam) && PtrOk(*view) && PtrOk(*main);
    } __except (1) { return false; }
}
static bool SetCam(uintptr_t view, uintptr_t cam) { __try { ((PFN_SetCam)kViewSetCam)(view, cam); return true; } __except (1) { return false; } }
static bool RW64(uintptr_t a, uint64_t* old, uint64_t v) { __try { if (old) *old = *(uint64_t*)a; *(uint64_t*)a = v; return true; } __except (1) { return false; } }
static bool R64(uintptr_t a, uint64_t* v) { __try { *v = *(uint64_t*)a; return true; } __except (1) { return false; } }

// vuelve todo como estaba (objetos a la escena del visor, mascara y camara de MainViewport)
static void Undo(const char* why) {
    if (!s_moved) return;
    uintptr_t own = 0, cam = 0, view = 0, main = 0; unsigned w = 0, h = 0; bool closed = false; int n = 0;
    if (s_ctx && ReadCtx(s_ctx, &own, &cam, &view, &main, &w, &h, &closed)) MoveObjs(s_ctx, own, &n);
    if (s_main) { RW64(s_main + 0x458 + 8, nullptr, s_oldMask1); if (s_oldCam) SetCam(s_main, s_oldCam); }
    Log("motor (escena): el jugador vuelve a la escena del visor (%s): %d objetos, mascara tipo 1 de la vista principal = %016llx", why, n, (unsigned long long)s_oldMask1);
    s_moved = false; s_live.store(false); s_ctx = 0; s_main = 0; s_oldCam = 0;
}

static void __fastcall hk_CtxFrame(uintptr_t ctx) {
    const bool want = s_want.load(std::memory_order_relaxed);
    if (!want) { if (s_moved && ctx == s_ctx) Undo("Motor cerrado o motor_escena = 0"); o_ctxFrame(ctx); return; }
    uintptr_t own = 0, cam = 0, view = 0, main = 0; unsigned w = 0, h = 0; bool closed = false;
    const bool rd = ReadCtx(ctx, &own, &cam, &view, &main, &w, &h, &closed);
    { // (17/11 07:47) para mover a los jugadores propios sin el jugador del Editar: que visores corren su cuadro y con que datos (cada 5 s, 24 veces)
      static ULONGLONG t = 0; static int nl = 0; const ULONGLONG nw = GetTickCount64();
      if (nw - t > 5000 && nl < 24) { t = nw; nl++; uintptr_t* b0 = nullptr; uintptr_t* e0 = nullptr; __try { b0 = *(uintptr_t**)(ctx + 0x08); e0 = *(uintptr_t**)(ctx + 0x10); } __except (1) {}
        Log("motor (escena): cuadro de visor %p: leido %d, cerrado %d, %ux%u, objetos %lld, escena %p, camara %p, vista %p, principal %p, movido %d",
            (void*)ctx, rd ? 1 : 0, closed ? 1 : 0, w, h, (long long)((PtrOk((uintptr_t)b0) && e0 >= b0) ? e0 - b0 : -1), (void*)own, (void*)cam, (void*)view, (void*)main, s_moved ? 1 : 0); }
    }
    if (!rd || closed || w < 512 || h < 256) { o_ctxFrame(ctx); return; }   // solo el visor grande
    uintptr_t scene = 0; { uint64_t v = 0; if (R64(kMainScene, &v)) scene = (uintptr_t)v; }
    if (!PtrOk(scene)) { o_ctxFrame(ctx); return; }
    if (s_moved && ctx != s_ctx) { o_ctxFrame(ctx); return; }   // otro visor: no se toca
    int n = 0;
    if (MoveObjs(ctx, scene, &n)) {
        if (!s_moved) {
            uint64_t oc = 0; R64(main + 0x4a0, &oc);
            if (RW64(main + 0x458 + 8, &s_oldMask1, ~0ull) && SetCam(main, cam)) {
                s_moved = true; s_ctx = ctx; s_main = main; s_oldCam = (uintptr_t)oc; s_live.store(true);
                Log("motor (escena): jugador a la escena principal: visor %p (%ux%u, escena %p), %d objetos -> escena %p; vista principal %p: tipo 1 %016llx -> todo, camara %p -> %p",
                    (void*)ctx, w, h, (void*)own, n, (void*)scene, (void*)main, (unsigned long long)s_oldMask1, (void*)oc, (void*)cam);
            } else { MoveObjs(ctx, own, nullptr); Log("motor (escena): no se pudo preparar la vista principal: se deja como estaba"); }
        } else {
            if (n != s_nObjs) Log("motor (escena): el visor ahora tiene %d objetos (antes %d)", n, s_nObjs);
            uint64_t m = 0; if (R64(main + 0x458 + 8, &m) && m != ~0ull) RW64(main + 0x458 + 8, nullptr, ~0ull);   // si el juego la vuelve a apagar
            uint64_t oc = 0; if (R64(main + 0x4a0, &oc) && (uintptr_t)oc != cam) SetCam(main, cam);
        }
        s_nObjs = n;
    }
    o_ctxFrame(ctx);
}

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static void Install() {
    s_tried = true;
    if (!g_cfg.motorPatch || !g_cfg.motorScene) { Log("motor (escena): apagado (motor = 0 o motor_escena = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("motor (escena): exe distinto: no se engancha"); return; }
    if (!Readable((const void*)kCtxFrame, sizeof kCtxFrameSig) || memcmp((const void*)kCtxFrame, kCtxFrameSig, sizeof kCtxFrameSig) != 0) { Log("motor (escena): la firma de 0x14198ba20 no coincide: no se engancha"); return; }
    (void)kSetCamSig;
    if (MH_CreateHook((void*)kCtxFrame, (void*)&hk_CtxFrame, (void**)&o_ctxFrame) == MH_OK && MH_EnableHook((void*)kCtxFrame) == MH_OK) s_ok = true;
    Log("motor (escena): cuadro del visor (0x14198ba20) %s", s_ok ? "enganchado" : "NO se pudo enganchar");
}

// desde Present (motorgrid.cpp MotorGridPresent)
void MotorVistaPresent() {
    const bool motor = MotorPageActive();
    if (motor && !s_tried) Install();
    s_want.store(s_ok && motor && g_cfg.motorScene != 0, std::memory_order_relaxed);
}
// glass.cpp: con el jugador en la escena principal el fondo del menu no se dibuja (taparia la vista principal)
bool MotorVistaLive() { return s_live.load(std::memory_order_relaxed) && s_want.load(std::memory_order_relaxed); }

} // namespace fx
