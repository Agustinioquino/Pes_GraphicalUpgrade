// PES21 FX (11/11, hilo 3icxbh) - MOUSE EN LOS MENUS NATIVOS DEL JUEGO (pedido de Agustin: "que los menus reaccionen con los
// inputs y el mouse, que sean clickeables"; "si el juego no lee el mouse eso si puedes hacerlo mediante dll pero el menu
// nativo del juego"). La DLL NO dibuja ningun menu: solo le dice al menu del juego donde esta el mouse y que boton se apreto.
//
// Leido en PES2021.exe 1.07.02 (sello 62787C6A), C decompilado + desensamblado:
//  * Toda pagina de menu (930 clases) es una menusystem::Window; su entrada por cuadro pasa por 0x140e3e480, que llama en
//    este orden: 0x140e400d0 (prueba del puntero), 0x140e3e6b0 (direcciones del mando), 0x140e3e8c0 (mover la seleccion) y
//    0x140e3ec40 (aceptar / volver / otras acciones, cada una preguntada por 0x140e48f80(ventana, mando, accion)).
//  * 0x140e400d0 es la prueba NATIVA "que fila hay debajo del puntero": recorre los grupos y las filas visibles, pide a cada
//    fila su posicion (vtable +0xa0) y su tamano (vtable +0xc0) en el escenario de 1920 x 1080 y, si el puntero cae adentro,
//    selecciona esa fila con 0x140e42300(ventana, grupo, fila). El juego solo la usa en las paginas con puntero propio (Plan
//    de juego, myClub, paleta de colores) y solo con el mando; en las demas sale enseguida (Impl+0x44 apagado).
//  * Impl = [ventana+0x88]: +0x98 nodo raiz, +0xa0 grupo actual, +0xa4 cantidad de grupos, +0xd8 fila actual, +0xa8..+0xb0
//    vector de 0x18 bytes {grupo, total, visibles, +0xc apagado}, +0xc0..+0xc8 grupos que aceptan filas inactivas,
//    +0x120 + 4 x accion = modo de la accion (0 = no existe en esta pagina). Nodo: +0x0c / +0x10 banderas (visible / activo),
//    +0x170..+0x178 hijos.
//
// Que hace este archivo (tres ganchos, todos en el hilo del menu y en el punto donde el juego hace lo mismo):
//  1) 0x140e400d0: despues del original, la MISMA prueba de rectangulos con la posicion del mouse; si el mouse se movio y hay
//     una fila debajo, la selecciona con la funcion del juego (0x140e42300). Si no hay fila debajo no toca nada.
//  2) 0x140e3e6b0: la rueda pasa a ser "arriba" / "abajo" (el juego mueve la seleccion, con su sonido).
//  3) 0x140e48f80: clic izquierdo sobre la fila seleccionada = accion 0 (aceptar); clic derecho = accion 1 (volver).
// No escribe nada en el exe ni en los datos del menu. [PES21FX] mouse_menus = 0 lo apaga (los ganchos quedan pasando de largo).
#include "core.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace fx {

HWND OverlayHwnd();   // overlay.cpp

static const unsigned kStamp = 0x62787C6A;
static const uintptr_t kHover = 0x140e400d0ull, kDirs = 0x140e3e6b0ull, kAction = 0x140e48f80ull, kSelect = 0x140e42300ull, kScroll = 0x140e3fbd0ull;
// 0x140e400d0: push r13; sub rsp,60h; mov rax,[rcx+88h]; mov r13,rcx; cmp qword [rax+98h],0
static const unsigned char kHoverSig[24] = { 0x41,0x55,0x48,0x83,0xec,0x60,0x48,0x8b,0x81,0x88,0x00,0x00,0x00,0x4c,0x8b,0xe9,0x48,0x83,0xb8,0x98,0x00,0x00,0x00,0x00 };
// 0x140e3e6b0: mov [rsp+20h],r9; push rsi; push r12; push r13; push r15; sub rsp,58h; mov rax,[rcx+88h]
static const unsigned char kDirsSig[23] = { 0x4c,0x89,0x4c,0x24,0x20,0x56,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x83,0xec,0x58,0x48,0x8b,0x81,0x88,0x00,0x00,0x00 };
// 0x140e48f80: sub rsp,28h; mov r9,rdx; test rdx,rdx; jz +7Ah; mov rax,[rcx+88h]; movsxd rdx,r8d; mov ecx,[rax+rdx*4+120h]
static const unsigned char kActionSig[29] = { 0x48,0x83,0xec,0x28,0x4c,0x8b,0xca,0x48,0x85,0xd2,0x74,0x7a,0x48,0x8b,0x81,0x88,0x00,0x00,0x00,0x49,0x63,0xd0,0x8b,0x8c,0x90,0x20,0x01,0x00,0x00 };
// 0x140e42300: mov [rsp+18h],rbx; mov [rsp+20h],rbp; push rdi; sub rsp,30h; mov rdi,rcx; mov ebp,r8d; mov rcx,[rcx+88h]
static const unsigned char kSelectSig[28] = { 0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0xf9,0x41,0x8b,0xe8,0x48,0x8b,0x89,0x88,0x00,0x00,0x00 };
// 0x140e3fbd0: sub rsp,28h; mov rcx,[rcx+88h]; cmp qword [rcx+98h],0; jz +28h
static const unsigned char kScrollSig[21] = { 0x48,0x83,0xec,0x28,0x48,0x8b,0x89,0x88,0x00,0x00,0x00,0x48,0x83,0xb9,0x98,0x00,0x00,0x00,0x00,0x74,0x28 };

typedef void (*PFN_Hover)(void*);
typedef void (*PFN_Dirs)(void*, int*, int*, unsigned char*);
typedef char (*PFN_Action)(void*, void*, int);
typedef void (*PFN_Select)(void*, int, long long);
typedef void* (*PFN_Scroll)(void*, int);
typedef int (*PFN_NodeVec2)(void*, float*);
static PFN_Hover o_hover = nullptr; static PFN_Dirs o_dirs = nullptr; static PFN_Action o_action = nullptr;

static bool s_tried = false, s_ok = false;
// estado del hilo del menu (los tres ganchos corren ahi)
static void* s_pendWin = nullptr; static int s_pendAction = -1;
static POINT s_last = { -32000, -32000 }; static bool s_lWas = false, s_rWas = false; static double s_stepT = 0;
static int s_wheelAcc = 0;
// una muestra del mouse por tanda de ventanas (varias ventanas pueden pedir entrada en el mismo paso del menu)
struct Muestra { double t = 0; POINT p{}; float cw = 0, ch = 0; bool ok = false, moved = false, lEdge = false, rEdge = false; };
static Muestra s_m;
static void* s_rGiven[8] = {}; static int s_rN = 0;                       // ventanas que ya recibieron este clic derecho
static void* s_latchWin = nullptr; static int s_latchG = -1, s_latchR = -1; static double s_latchT = 0;   // clic que tuvo que elegir la fila: acepta en el paso siguiente
// compartido con el hilo de Present / la ventana
static std::atomic<int> s_wheel{0};
static std::atomic<int> s_curX{0}, s_curY{0}; static std::atomic<double> s_moveT{0}, s_liveT{0};
static std::atomic<int> s_nSel{0}, s_nClick{0}, s_nBack{0}, s_nWheel{0}, s_nStep{0};

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static bool SigAt(uintptr_t a, const unsigned char* sig, size_t n) { return Readable((const void*)a, n) && memcmp((const void*)a, sig, n) == 0; }
static inline bool Ptr(const void* p) { const uintptr_t a = (uintptr_t)p; return a >= 0x10000ull && a < 0x7ffffffff000ull && (a & 7) == 0; }
static inline uint32_t U32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint8_t* P8(const uint8_t* p) { uint8_t* v; memcpy(&v, p, 8); return v; }

// lo mismo que 0x140e4df90 (visible, +0x0c) y 0x140e4dd30 (activo, +0x10)
static inline bool NodeFlag(const uint8_t* node, int off) { const uint32_t v = U32(node + off); return (v & 7) == 7 && ((v >> 8) & 1); }
// lo mismo que 0x140e8fa80 / 0x140e8fc30: hijos en +0x170..+0x178
static inline uint32_t ChildCount(const uint8_t* node) { return (uint32_t)((P8(node + 0x178) - P8(node + 0x170)) >> 3); }
static inline uint8_t* Child(const uint8_t* node, uint32_t i) { if (ChildCount(node) <= i) return nullptr; uint8_t* b = P8(node + 0x170); return Ptr(b) ? P8(b + (size_t)i * 8) : nullptr; }

// todas las clases de nodo del menu (60) comparten la posicion 0x140e4d9e0 en la ranura 20; el tamano (ranura 24) es uno de estos
static inline bool RowClass(void** vt) {
    const uintptr_t a = (uintptr_t)vt[0xa0 / 8], b = (uintptr_t)vt[0xc0 / 8];
    return a == 0x140e4d9e0ull && (b == 0x140e4ade0ull || b == 0x140e54130ull || b == 0x140e5b9c0ull || b == 0x140e90710ull || b == 0x140e92170ull);
}

struct Hit { int group = -1, row = -1; float x = 0, y = 0, w = 0, h = 0; };

// replica de la recorrida de 0x140e400d0 con (mx, my) en vez del puntero del juego
static bool HitTest(uint8_t* I, float mx, float my, Hit& out) {
    uint8_t* root = P8(I + 0x98); if (!Ptr(root)) return false;
    const uint32_t ng = U32(I + 0xa4); if (ng == 0 || ng > 64) return false;
    for (uint32_t g = 0; g < ng; g++) {
        {   // grupo anotado en el vector con +0xc en 0: no se prueba
            uint8_t* vb = P8(I + 0xa8); uint8_t* ve = P8(I + 0xb0); bool skip = false;
            if (vb != ve && Ptr(vb) && ve > vb && (size_t)(ve - vb) <= 0x18 * 256)
                for (uint8_t* e = vb; e + 0x18 <= ve; e += 0x18) if (U32(e) == g) { skip = e[0xc] == 0; break; }
            if (skip) continue;
        }
        uint8_t* grp = Child(root, g);
        if (!Ptr(grp) || !NodeFlag(grp, 0x0c) || !NodeFlag(grp, 0x10)) continue;
        bool inList = false;
        { uint8_t* lb = P8(I + 0xc0); uint8_t* le = P8(I + 0xc8);
          if (lb != le && Ptr(lb) && le > lb && (size_t)(le - lb) <= 4 * 256) for (uint8_t* e = lb; e + 4 <= le; e += 4) if (U32(e) == g) { inList = true; break; } }
        const uint32_t n = ChildCount(grp); if (n > 512) continue;
        for (uint32_t r = 0; r < n; r++) {
            uint8_t* node = Child(grp, r);
            if (!Ptr(node) || !NodeFlag(node, 0x0c) || (!NodeFlag(node, 0x10) && !inList)) continue;
            void** vt = *(void***)node; if (!Ptr(vt) || !RowClass(vt)) continue;
            float pos[2] = { 0, 0 }, size[2] = { 0, 0 };
            if (((PFN_NodeVec2)vt[0xa0 / 8])(node, pos) < 0) continue;      // sin clip: el juego usaria valores viejos, aca se saltea
            ((PFN_NodeVec2)vt[0xc0 / 8])(node, size);
            if (!(size[0] > 0.0f) || !(size[1] > 0.0f)) continue;
            if (mx > pos[0] && pos[0] + size[0] > mx && my > pos[1] && pos[1] + size[1] > my) {
                out.group = (int)g; out.row = (int)r; out.x = pos[0]; out.y = pos[1]; out.w = size[0]; out.h = size[1];
                return true;
            }
        }
    }
    return false;
}

// lo mismo que el final de 0x140e400d0: si el grupo esta en el vector se suma el desplazamiento de su lista
static void SelectLikeGame(void* win, uint8_t* I, int g, int r) {
    long long row = r;
    uint8_t* vb = P8(I + 0xa8); uint8_t* ve = P8(I + 0xb0);
    if (vb != ve && Ptr(vb) && ve > vb && (size_t)(ve - vb) <= 0x18 * 256)
        for (uint8_t* e = vb; e + 0x18 <= ve; e += 0x18) if ((int)U32(e) == g) {
            uint8_t* s = (uint8_t*)((PFN_Scroll)kScroll)(win, g);
            if (Ptr(s)) row += (int)U32(s + 0xc0);
            break;
        }
    ((PFN_Select)kSelect)(win, g, row);
}

// apagado con el menu de PES21FX abierto, con la camara libre y con el panel del Motor en tiempo real abierto (ahi el mouse es del Motor)
static bool Enabled() { return s_ok && g_cfg.menuMouse != 0 && !g_menuOpen.load(std::memory_order_relaxed) && !FreeCamActive() && !MotorPanelOpen(); }

static void Sample(double now, bool fresh) {
    s_m = Muestra(); s_m.t = now;
    const bool lNow = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, rNow = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool lEdge = lNow && !s_lWas && !fresh, rEdge = rNow && !s_rWas && !fresh;
    s_lWas = lNow; s_rWas = rNow;
    HWND h = OverlayHwnd(); POINT p{}; RECT rc{};
    if (!h || GetForegroundWindow() != h || !GetCursorPos(&p) || !ScreenToClient(h, &p) || !GetClientRect(h, &rc)) { s_last.x = s_last.y = -32000; return; }
    s_m.cw = (float)(rc.right - rc.left); s_m.ch = (float)(rc.bottom - rc.top);
    s_m.moved = (p.x != s_last.x || p.y != s_last.y) && !fresh && s_last.x != -32000;
    s_last = p; s_m.p = p; s_m.lEdge = lEdge; s_m.rEdge = rEdge;
    s_m.ok = s_m.cw >= 160.0f && s_m.ch >= 90.0f && p.x >= 0 && p.y >= 0 && (float)p.x < s_m.cw && (float)p.y < s_m.ch;
    if (rEdge) s_rN = 0;
    s_curX.store(p.x); s_curY.store(p.y);
    if (s_m.ok && (s_m.moved || lEdge || rEdge)) s_moveT.store(now);
}

static void Step(void* win) {
    const double now = Now();
    const bool fresh = now - s_stepT > 0.25;      // primer paso de un menu despues de un rato sin menus
    const bool batch = fresh || now - s_m.t > 0.004;
    s_stepT = now; s_pendWin = nullptr; s_pendAction = -1;
    if (fresh) { s_wheelAcc = 0; s_wheel.store(0); s_latchWin = nullptr; }
    if (batch) Sample(now, fresh);                 // siempre, para no inventar un clic al volver del menu de PES21FX
    if (!Enabled() || !Ptr(win)) { s_latchWin = nullptr; return; }
    uint8_t* I = P8((uint8_t*)win + 0x88); if (!Ptr(I)) return;
    s_liveT.store(now); s_nStep.fetch_add(1, std::memory_order_relaxed);
    if (!s_m.ok) return;
    if (I[0x44]) return;                              // pagina con puntero propio del juego: no se toca (por ahora)
    if (s_latchWin == win) {                          // clic del paso anterior que primero tuvo que elegir la fila
        const bool same = (int)U32(I + 0xa0) == s_latchG && (int)U32(I + 0xd8) == s_latchR && now - s_latchT < 0.25;
        s_latchWin = nullptr;
        if (same) { s_pendWin = win; s_pendAction = 0; return; }
    }
    if (!s_m.moved && !s_m.lEdge && !s_m.rEdge) return;
    // escenario de 1920 x 1080 centrado en la ventana
    const float cw = s_m.cw, ch = s_m.ch; float sw = cw, sh = ch, ox = 0, oy = 0;
    if (cw * 9.0f > ch * 16.0f) { sw = ch * 16.0f / 9.0f; ox = (cw - sw) * 0.5f; } else if (cw * 9.0f < ch * 16.0f) { sh = cw * 9.0f / 16.0f; oy = (ch - sh) * 0.5f; }
    const float mx = ((float)s_m.p.x - ox) * 1920.0f / sw, my = ((float)s_m.p.y - oy) * 1080.0f / sh;
    Hit hit; const bool on = (s_m.moved || s_m.lEdge) && HitTest(I, mx, my, hit);
    bool chose = false;
    if (on && !((int)U32(I + 0xa0) == hit.group && (int)U32(I + 0xd8) == hit.row)) {
        SelectLikeGame(win, I, hit.group, hit.row); chose = true;
        const int k = s_nSel.fetch_add(1, std::memory_order_relaxed);
        if (k < 12) Log("mouse en menus: fila bajo el mouse -> grupo %d fila %d (rect %.0f %.0f %.0f x %.0f; mouse %.0f %.0f de 1920 x 1080; ventana %.0f x %.0f; seleccion ahora %d / %d)",
                        hit.group, hit.row, hit.x, hit.y, hit.w, hit.h, mx, my, cw, ch, (int)U32(I + 0xa0), (int)U32(I + 0xd8));
    }
    if (s_m.lEdge && on) {
        s_m.lEdge = false;                            // un clic es para una sola ventana
        if ((int)U32(I + 0xa0) == hit.group && (int)U32(I + 0xd8) == hit.row) {
            if (chose) { s_latchWin = win; s_latchG = hit.group; s_latchR = hit.row; s_latchT = now; }   // como con el mando: mover y aceptar en pasos distintos
            else { s_pendWin = win; s_pendAction = 0; }
        }
    } else if (s_m.rEdge) {
        bool given = false; for (int i = 0; i < s_rN; i++) if (s_rGiven[i] == win) given = true;
        if (!given && s_rN < 8) { s_rGiven[s_rN++] = win; s_pendWin = win; s_pendAction = 1; }
    }
}

static void hk_Hover(void* win) {
    o_hover(win);
    Step(win);
}

static void hk_Dirs(void* win, int* dx, int* dy, unsigned char* trig) {
    o_dirs(win, dx, dy, trig);
    const int w = s_wheel.exchange(0);
    if (!Enabled() || !dx || !dy) { s_wheelAcc = 0; return; }
    s_wheelAcc = std::clamp(s_wheelAcc + w, -600, 600);
    if (std::abs(s_wheelAcc) < 120 || *dx != 0 || *dy != 0) return;       // el mando / teclado mandan
    HWND h = OverlayHwnd(); if (!h || GetForegroundWindow() != h) { s_wheelAcc = 0; return; }
    const int sgn = s_wheelAcc > 0 ? 1 : -1; s_wheelAcc -= sgn * 120;
    *dy = sgn > 0 ? -1 : 1;                                             // rueda hacia adelante = arriba
    s_pendWin = nullptr; s_pendAction = -1;
    s_nWheel.fetch_add(1, std::memory_order_relaxed); s_moveT.store(Now());
}

static char hk_Action(void* win, void* pad, int action) {
    if (win && pad && s_pendWin == win && s_pendAction == action && action >= 0 && action < 2) {
        uint8_t* I = P8((uint8_t*)win + 0x88);
        const uint32_t mode = Ptr(I) ? U32(I + 0x120 + (size_t)action * 4) : 0;
        if (mode >= 1 && mode <= 3) {                                    // la pagina tiene esa accion
            s_pendWin = nullptr; s_pendAction = -1;
            (action == 0 ? s_nClick : s_nBack).fetch_add(1, std::memory_order_relaxed);
            return 1;
        }
    }
    return o_action(win, pad, action);
}

static void Install() {
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("mouse en menus: parches en memoria apagados (motor = 0): sin mouse en los menus del juego"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) {
        Log("mouse en menus: exe distinto (base %p, sello %08X): sin gancho", (void*)mod, (unsigned)nt->FileHeader.TimeDateStamp); return;
    }
    if (!SigAt(kHover, kHoverSig, sizeof kHoverSig) || !SigAt(kDirs, kDirsSig, sizeof kDirsSig) || !SigAt(kAction, kActionSig, sizeof kActionSig) ||
        !SigAt(kSelect, kSelectSig, sizeof kSelectSig) || !SigAt(kScroll, kScrollSig, sizeof kScrollSig)) {
        Log("mouse en menus: alguna firma no coincide (0x140e400d0 / 0x140e3e6b0 / 0x140e48f80 / 0x140e42300 / 0x140e3fbd0): se omite"); return;
    }
    if (MH_CreateHook((void*)kHover, (void*)&hk_Hover, (void**)&o_hover) != MH_OK || MH_CreateHook((void*)kDirs, (void*)&hk_Dirs, (void**)&o_dirs) != MH_OK ||
        MH_CreateHook((void*)kAction, (void*)&hk_Action, (void**)&o_action) != MH_OK) {
        MH_RemoveHook((void*)kHover); MH_RemoveHook((void*)kDirs); MH_RemoveHook((void*)kAction);
        Log("mouse en menus: MinHook no pudo crear los ganchos: se omite"); return;
    }
    // el de la prueba del puntero va ultimo: recien ahi empieza a correr el codigo propio
    if (MH_EnableHook((void*)kAction) != MH_OK || MH_EnableHook((void*)kDirs) != MH_OK || MH_EnableHook((void*)kHover) != MH_OK) {
        MH_DisableHook((void*)kHover); MH_DisableHook((void*)kDirs); MH_DisableHook((void*)kAction);
        Log("mouse en menus: MinHook no pudo prender los ganchos: se omite"); return;
    }
    s_ok = true;
    Log("mouse en menus: listo (prueba del puntero 0x140e400d0, direcciones 0x140e3e6b0, acciones 0x140e48f80); mouse_menus = %d (0 lo apaga)", g_cfg.menuMouse);
}

void MenuMouseWheel(int delta) { if (s_ok && g_cfg.menuMouse != 0) s_wheel.fetch_add(delta, std::memory_order_relaxed); }

// para el cursor: posicion en pixeles de la ventana si el mouse se uso hace poco dentro de un menu del juego
bool MenuMouseCursor(float* x, float* y) {
    if (!s_ok || g_cfg.menuMouse == 0) return false;
    const double now = Now();
    if (now - s_liveT.load() > 0.25 || now - s_moveT.load() > 4.0) return false;
    CURSORINFO ci{}; ci.cbSize = sizeof ci;
    if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) return false;   // Windows ya muestra el suyo
    if (x) *x = (float)s_curX.load(); if (y) *y = (float)s_curY.load();
    return true;
}

void MenuMousePresent() {
    if (!s_tried) Install();
    if (!s_ok) return;
    static double s_t = 0; static int s_a[5] = {};
    const double t = Now();
    if (s_t == 0) { s_t = t; return; }
    if (t - s_t < 30.0) return;
    const int b[5] = { s_nStep.load(), s_nSel.load(), s_nClick.load(), s_nBack.load(), s_nWheel.load() };
    if (b[1] != s_a[1] || b[2] != s_a[2] || b[3] != s_a[3] || b[4] != s_a[4])
        Log("mouse en menus (30 s): %d cuadros de menu, %d filas elegidas con el mouse, %d clics de aceptar, %d de volver, %d pasos de rueda",
            b[0] - s_a[0], b[1] - s_a[1], b[2] - s_a[2], b[3] - s_a[3], b[4] - s_a[4]);
    memcpy(s_a, b, sizeof s_a); s_t = t;
}

} // namespace fx
