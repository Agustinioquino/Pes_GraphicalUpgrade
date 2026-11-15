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
//  (v3) clic izquierdo sobre una fila con selector de opciones = "derecha" (o "izquierda" en la mitad izquierda del selector), por el
//     gancho 2: el juego cambia la opcion con su propia funcion (0x140e3c770), su sonido y sus avisos a la pagina.
// No escribe nada en el exe ni en los datos del menu. [PES21FX] mouse_menus = 0 lo apaga (los ganchos quedan pasando de largo).
#include "core.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <cstdio>
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
typedef uint8_t* (*PFN_ChildByType)(void* sub, int type, int idx);   // 0x140e4f5d0: hijo de un nodo por tipo (2 = selector)
static const uintptr_t kChildByType = 0x140e4f5d0ull;
static PFN_Hover o_hover = nullptr; static PFN_Dirs o_dirs = nullptr; static PFN_Action o_action = nullptr;

static bool s_tried = false, s_ok = false;
// (14/11 ythnmm, Agustin: "continuar al igual que en el primer menu de apretar cualquier tecla") pantalla del titulo menu::TitleStart
// (vtable 0x142671970): su vf40 0x140a9ae40 -> 0x140a9ac10 lee el mando 0 boton por boton (vf+0x40) y si hay alguno llama a
// 0x141b58200(1) (mando activo) y 0x140a99250(pagina) ("titleOut", +0x9c = 1). Con el mouse: el clic izquierdo hace lo mismo, cuando
// el "pressStart_on" ya se mostro (+0xd8) y no salio todavia (+0x9c != 1).
static const uintptr_t kTitleVt = 0x142671970ull, kPadActive = 0x141b58200ull, kTitleGo = 0x140a99250ull;
static const unsigned char kPadActiveSig[14] = { 0x8b,0xd1,0x48,0x8b,0x0d,0x8f,0x6c,0xca,0x01,0x48,0x85,0xc9,0x74,0x07 };
static const unsigned char kTitleGoSig[14] = { 0x40,0x53,0x48,0x83,0xec,0x20,0xba,0x13,0x00,0x00,0x00,0x48,0x8b,0xd9 };
static bool s_titleOk = false;
// (14/11 ythnmm, Agustin: "el video de introduccion no es salteable") video de la intro menu::Movie (vtable 0x142671230): su vf40
// 0x140a98b80, en el estado 2 (+0xa4, video andando), con el boton aceptar (codigo 0x24) o 0x1b corta el video: si +0xa5 == 0 lo pone
// en 1 y llama a 0x140e72ee0([pagina+0x90]) (para el video), despues +0xa4 = 3 y +0xa6 = 1. El clic izquierdo hace exactamente eso.
static const uintptr_t kMovieVt = 0x142671230ull, kMovieStop = 0x140e72ee0ull;
static const unsigned char kMovieStopSig[14] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8 };
static bool s_movieOk = false;
// estado del hilo del menu (los tres ganchos corren ahi)
// (14/11 e, Agustin: "la deteccion del mouse en este menu es rara"; "no todos los menus son iguales") RECTANGULO DIBUJADO de una fila.
// El tamano de la fila que usa el juego (nodo +0x8c) es el del diseno ORIGINAL; un parche que redibuja el menu (tarjetas grandes,
// pestanas en otro lugar) lo deja mal. El reproductor de las pantallas (AFP) sabe lo que dibuja: 0x1400c17b0(id, propiedad, salida)
// con 0x1015 / 0x1016 = ancho / alto, que calcula 0x14002e950 y guarda en objeto+0xf0 {x0, x1, y0, y1} en el espacio del PADRE
// (contenido x matriz propia, objeto+0x80), y 0x100f = matriz global {a, b, c, d, tx, ty} (0x14002d240, la misma de la posicion
// 0x1008). id = [nodo+0x40]+0xc; objeto = 0x14004a890(id). Rect global = (rect del padre) x (matriz propia)^-1 x (matriz global).
typedef int (*PFN_AfpGet)(uint64_t id, uint32_t prop, void* out, void* p4);
typedef uint8_t* (*PFN_AfpObj)(uint32_t id);
static const uintptr_t kAfpGet = 0x1400c17b0ull, kAfpObj = 0x14004a890ull;
static const unsigned char kAfpGetSig[20] = { 0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x48,0x83,0xec,0x38,0x89,0xc8,0xc1,0xe8,0x1b,0x83,0xe0,0x0f,0x89 };
static const unsigned char kAfpObjSig[20] = { 0x0f,0xb7,0xd1,0x48,0x8b,0x05,0x7e,0x11,0x6f,0x03,0x48,0x8b,0x04,0xd0,0x48,0x85,0xc0,0x74,0x0d,0x33 };
static bool s_afpOk = false;
// (14/11 e) ELEGIR EQUIPO (menu::TeamSelectCommon y las clases que comparten sus funciones): la lista de ligas / equipos es un carrusel
// de 9 elementos "listItem_0..8" (el elegido es el 4) que la ventana mueve por su cuenta: 0x140c975b0 mira arriba / abajo mantenidos
// y llama a la ranura 0x250 (anterior, 0x140c97420) o 0x258 (siguiente, 0x140c97330) si no esta deshabilitada ([ventana+0x120]),
// esta activa (Impl+9 bit 1) y termino la animacion anterior (0x140c96f40). Con el mouse: rueda = esas mismas ranuras; clic en un
// elemento = tantos pasos como falten para traerlo al centro; clic en el del centro = aceptar. Los rectangulos se leen en cada
// movimiento del mouse (la lista se puede mover o cambiar de liga): 0x140e4f120(contenedor+0x98, nombre) con los nombres del juego.
typedef uint8_t* (*PFN_FindChild)(void* sub, const char* name);
typedef char (*PFN_WinBool)(void*);
static const uintptr_t kFindChild = 0x140e4f120ull, kTsIdle = 0x140c96f40ull, kTsPrev = 0x140c97420ull, kTsNext = 0x140c97330ull, kTsMove = 0x140c97440ull, kTsNames = 0x1434c83c0ull;
static const unsigned char kFindChildSig[20] = { 0x4c,0x8b,0xdc,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x49,0xc7,0x43,0xa0 };
static const unsigned char kTsIdleSig[20] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0x72,0x8c,0x1a,0x00,0x48,0x8b,0x15,0x33,0x14,0x83 };
static bool s_tsOk = false;
static void* s_tsWin = nullptr; static double s_tsT = 0; static int s_tsPend = 0; static double s_tsPendT = 0; static void* s_tsPendWin = nullptr;
// (14/11 e, Agustin: "si se usa el mouse que no aparezcan los iconos del mando y cuando se mueva el mando vuelvan, asi como el cursor
// desaparezca") barra de botones del juego (menusystem::GuideBar, unica, DAT_143707d10; nodo raiz en +0x70): visible = propiedad 0x1007
// del AFP, que el juego pone con 0x141f96bc0([nodo+0x40], 0 / 1). Se esconde mientras lo ultimo usado sea el mouse y vuelve con el mando.
static const uintptr_t kGuideBar = 0x143707d10ull, kAfpSetVisible = 0x141f96bc0ull;
static const unsigned char kAfpSetVisibleSig[13] = { 0x8b,0x49,0x0c,0x44,0x0f,0xb6,0xc2,0xba,0x07,0x10,0x00,0x00,0xe9 };
static bool s_guideOk = false, s_guideHidden = false; static double s_guideT = 0;
static std::atomic<double> s_padT{0};                                    // ultima vez que el mando / teclado movio o apreto algo en un menu

// (14/11 g, Agustin: "al usar mando, para que funcione bien, que se reinicie la seleccion") por ventana: la fila que tenia elegida el
// mando antes de que el mouse eligiera otra. La primera direccion del mando despues del mouse vuelve a esa fila (y no mueve).
struct PadSel { void* win; uintptr_t vt; int g, r; bool dirty; double t; };   // vt: la misma direccion puede ser otra pagina despues
static PadSel s_padSel[8] = {};
static PadSel* PadSelFor(void* win, bool create) {
    PadSel* old = &s_padSel[0];
    for (PadSel& e : s_padSel) { if (e.win == win) return &e; if (e.t < old->t) old = &e; }
    if (!create) return nullptr;
    *old = PadSel{ win, 0, -1, -1, false, 0 }; return old;
}

static void* s_pendWin = nullptr; static int s_pendAction = -1;
static void* s_pendDxWin = nullptr; static int s_pendDx = 0;              // (v3) clic sobre una fila con selector: "derecha" / "izquierda" para el juego
static POINT s_last = { -32000, -32000 }; static bool s_lWas = false, s_rWas = false; static double s_stepT = 0;
static int s_wheelAcc = 0;
// una muestra del mouse por tanda de ventanas (varias ventanas pueden pedir entrada en el mismo paso del menu)
struct Muestra { double t = 0; POINT p{}; float cw = 0, ch = 0; bool ok = false, moved = false, lEdge = false, rEdge = false; };
static Muestra s_m;
static void* s_rGiven[8] = {}; static int s_rN = 0;                       // ventanas que ya recibieron este clic derecho
static void* s_latchWin = nullptr; static int s_latchG = -1, s_latchR = -1, s_latchDir = 0; static double s_latchT = 0;   // clic que tuvo que elegir la fila: acepta en el paso siguiente
// compartido con el hilo de Present / la ventana
static std::atomic<int> s_wheel{0};
static bool s_overB = false; static std::atomic<bool> s_over{false};   // (14/11 d) el mouse esta justo sobre una fila (se puede hacer clic): mano
static std::atomic<int> s_curX{0}, s_curY{0}; static std::atomic<double> s_moveT{0}, s_liveT{0};
static std::atomic<int> s_nSel{0}, s_nClick{0}, s_nBack{0}, s_nWheel{0}, s_nStep{0}, s_nNear{0}, s_nOpt{0}, s_nExtra{0};
// (14/11 ythnmm) acciones del juego por indice (tabla 0x1427bcdf0, codigo del boton = 19 + boton de DirectInput): 0 aceptar, 1 volver,
// 2 cuadrado, 3 triangulo, 4 OPTIONS, 5 SHARE, 6 / 7 L2 / R2, 8 / 9 L1 / R1, 10 / 11 L3 / R3. Agustin 14/11 09:34: con el mouse solo
// rueda, clic izquierdo y clic derecho (nada de botones extra ni teclas). s_extraAct queda para pedir una accion desde un clic.
static int s_extraAct = -1; static double s_extraT = 0;
static void* s_wheelOptWin = nullptr; static float s_wheelOptMx = 0;       // (14/11 ythnmm) el mouse esta sobre la fila elegida y es un selector: la rueda cambia la opcion

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

// todas las clases de nodo comparten la posicion (ranura 20) y guardan el objeto del AFP en +0x40
static inline bool NodeClass(void** vt) { return (uintptr_t)vt[0xa0 / 8] == 0x140e4d9e0ull; }

// (14/11 e) rectangulo de lo que el nodo DIBUJA, en el escenario de 1920 x 1080. false = no se pudo leer (se usa el del juego)
static bool DrawnRect(uint8_t* node, float& x, float& y, float& w, float& h) {
    if (!s_afpOk || g_cfg.menuMouseDrawn == 0) return false;
    uint8_t* hd = P8(node + 0x40); if (!Ptr(hd)) return false;
    const uint32_t id = U32(hd + 0xc); if (((id >> 27) & 0xf) != 4) return false;
    uint8_t* o = ((PFN_AfpObj)kAfpObj)(id); if (!Ptr(o)) return false;
    float ww = 0, G[6] = { 1, 0, 0, 1, 0, 0 };
    if (((PFN_AfpGet)kAfpGet)(id, 0x1015, &ww, nullptr) != 0) return false;     // deja el rect al dia en objeto+0xf0
    if (((PFN_AfpGet)kAfpGet)(id, 0x100f, G, nullptr) != 0) return false;
    float b[4]; memcpy(b, o + 0xf0, 16);                                        // x0, x1, y0, y1 (padre)
    float L[6] = { 1, 0, 0, 1, 0, 0 };
    if (o[0xcc] == 0) { memcpy(&L[0], o + 0x80, 4); memcpy(&L[1], o + 0x84, 4); memcpy(&L[2], o + 0x90, 4); memcpy(&L[3], o + 0x94, 4); memcpy(&L[4], o + 0xb0, 4); memcpy(&L[5], o + 0xb4, 4); }
    if (o[0xcd] != 0) { float px, py; memcpy(&px, o + 0xc0, 4); memcpy(&py, o + 0xc4, 4); L[4] -= px * L[0] + py * L[2]; L[5] -= px * L[1] + py * L[3]; }
    const float det = L[0] * L[3] - L[1] * L[2];
    for (float v : b) if (!std::isfinite(v)) return false;
    for (float v : G) if (!std::isfinite(v)) return false;
    if (!std::isfinite(det) || std::fabs(det) < 1e-6f || !(b[1] > b[0]) || !(b[3] > b[2])) return false;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < 4; i++) {
        const float px = ((i & 1) ? b[1] : b[0]) - L[4], py = ((i & 2) ? b[3] : b[2]) - L[5];
        const float lx = (px * L[3] - py * L[2]) / det, ly = (py * L[0] - px * L[1]) / det;      // punto en el espacio del objeto
        const float gx = lx * G[0] + ly * G[2] + G[4], gy = lx * G[1] + ly * G[3] + G[5];
        x0 = std::min(x0, gx); y0 = std::min(y0, gy); x1 = std::max(x1, gx); y1 = std::max(y1, gy);
    }
    if (!(x1 - x0 >= 4.0f) || !(y1 - y0 >= 4.0f) || !(x1 - x0 < 2600.0f) || !(y1 - y0 < 1600.0f) || x1 < -200.0f || y1 < -200.0f || x0 > 2120.0f || y0 > 1280.0f) return false;
    x = x0; y = y0; w = x1 - x0; h = y1 - y0; return true;
}

// (14/11 f) el nodo dice "visible" pero el AFP no lo muestra: el objeto o un padre con la bandera de invisible (0x20000 en +0x20, la de
// la propiedad 0x1007) o con el alfa de su color en 0 (+0xdc, si tiene transformacion de color: 0x8000000 en +0x24). En el menu
// principal del Dream Patch la lista del medio (grupo 1) no se dibuja pero sus filas siguen "visibles" para el juego: el mouse las
// elegia y despues el mando quedaba moviendose en una lista que no se ve (Agustin 14/11 13:42).
static bool AfpShown(uint8_t* node, uint32_t* why = nullptr) {
    if (!s_afpOk || g_cfg.menuMouseDrawn == 0) return true;
    uint8_t* hd = P8(node + 0x40); if (!Ptr(hd)) return true;
    const uint32_t id = U32(hd + 0xc); if (((id >> 27) & 0xf) != 4) return true;
    uint8_t* o = ((PFN_AfpObj)kAfpObj)(id);
    for (int i = 0; i < 32 && Ptr(o); i++) {
        // (15/11) NO se mira 0x20000 en +0x20: el log del 14/11 21:59 lo muestra prendido en TODOS los objetos (tambien en los que se ven).
        // La lista escondida del menu principal se reconoce por el alfa: un padre con 0x8000000 en +0x24 y +0xdc = 0.00
        if (U32(o + 0x24) & 0x8000000u) { float a; memcpy(&a, o + 0xdc, 4); if (std::isfinite(a) && a < 0.02f) { if (why) *why = 0x200u + (uint32_t)i; return false; } }
        int16_t depth; memcpy(&depth, o + 0x78, 2); if (depth < 1) break;
        o = P8(o + 0x40);
    }
    return true;
}

struct Hit { int group = -1, row = -1; float x = 0, y = 0, w = 0, h = 0, dist = 0; };   // dist > 0: no esta justo encima, es la fila mas cercana de su grupo

// replica de la recorrida de 0x140e400d0 con (mx, my) en vez del puntero del juego
static bool HitTest(uint8_t* I, float mx, float my, Hit& out, bool useShown = true) {
    int nSeen = 0, nHidden = 0;                    // si el AFP da por escondidas TODAS las filas, la lectura esta mal: se repite sin esa prueba
    uint8_t* root = P8(I + 0x98); if (!Ptr(root)) return false;
    const uint32_t ng = U32(I + 0xa4); if (ng == 0 || ng > 64) return false;
    // (v2) huecos entre filas de un mismo grupo (pestanas separadas, listas con margen): con el mouse dentro del rectangulo que
    // encierra a las filas del grupo vale la fila mas cercana. Una fila justo debajo del mouse gana siempre (se devuelve al toque).
    Hit best; float bestD = 1e9f;
    // (14/11 ythnmm, Agustin: "la precision") filas que se pisan (grillas de iconos, el rect del juego es mas grande que el dibujo): gana la
    // que tiene el centro mas cerca del mouse, no la primera de la lista. Y la "mas cercana" en los huecos solo vale a pocos pixeles
    // (antes llegaba a 160 y elegia iconos a 140 px). mouse_menus_cerca 2 = como antes.
    Hit direct; float directD = 1e9f;
    // (14/11 e) segunda prueba con lo que la fila DIBUJA (DrawnRect). Una fila cuyo rect del juego cae casi todo fuera de su dibujo
    // (menu redibujado por un parche) se prueba solo con el dibujo; las demas primero con el rect del juego (como siempre) y, si el
    // mouse no cae en ninguno, con el dibujo: gana el dibujo mas chico que contiene al mouse.
    Hit drawn; float drawnA = 1e18f;
    Hit directAll; float directAllD = 1e9f; int nRows = 0, nBogus = 0;   // si la mayoria de las filas "no coincide" el que esta mal es el dibujo leido: se ignora
    const bool wide = g_cfg.menuMouseNear >= 2;
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
        Hit cand; float nearD = 1e9f; int rows = 0; float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
        // (14/11 f) GRILLA CON FILAS CHICAS: en un menu redibujado por un parche la fila del juego puede ser solo la esquina de la tarjeta
        // (126 x 126 con tarjetas cada 420 px). Si las filas del grupo son mucho mas chicas que la distancia entre ellas, cada fila
        // ocupa su celda: hasta la columna siguiente y hasta la fila siguiente de su columna (o el borde de la grilla).
        struct Cell { uint32_t r; float x, y, w, h; }; static Cell s_cell[96]; int nc = 0; bool cells = false;
        if (g_cfg.menuMouseDrawn != 0 && n <= 96) {
            for (uint32_t r = 0; r < n; r++) {
                uint8_t* node = Child(grp, r);
                if (!Ptr(node) || !NodeFlag(node, 0x0c) || (!NodeFlag(node, 0x10) && !inList)) continue;
                void** vt = *(void***)node; if (!Ptr(vt) || !RowClass(vt)) continue;
                float pos[2] = { 0, 0 }, size[2] = { 0, 0 };
                if (((PFN_NodeVec2)vt[0xa0 / 8])(node, pos) < 0) continue;
                ((PFN_NodeVec2)vt[0xc0 / 8])(node, size);
                if (!(size[0] > 0.0f) || !(size[1] > 0.0f) || !(size[0] < 4000.0f) || !(size[1] < 4000.0f) || !(std::fabs(pos[0]) < 8000.0f) || !(std::fabs(pos[1]) < 8000.0f)) continue;
                if (useShown && !AfpShown(node)) continue;
                s_cell[nc++] = { r, pos[0], pos[1], size[0], size[1] };
            }
            float xs[96], ys[96]; int nx = 0, ny = 0; float maxW = 0, maxH = 0;
            for (int i = 0; i < nc; i++) {
                bool fx = false, fy = false;
                for (int k = 0; k < nx; k++) if (std::fabs(xs[k] - s_cell[i].x) < 6.0f) fx = true;
                for (int k = 0; k < ny; k++) if (std::fabs(ys[k] - s_cell[i].y) < 6.0f) fy = true;
                if (!fx) xs[nx++] = s_cell[i].x; if (!fy) ys[ny++] = s_cell[i].y;
                maxW = std::max(maxW, s_cell[i].w); maxH = std::max(maxH, s_cell[i].h);
            }
            std::sort(xs, xs + nx); std::sort(ys, ys + ny);
            float pitchX = 1e9f, pitchY = 1e9f;
            for (int k = 1; k < nx; k++) pitchX = std::min(pitchX, xs[k] - xs[k - 1]);
            for (int k = 1; k < ny; k++) pitchY = std::min(pitchY, ys[k] - ys[k - 1]);
            cells = nc >= 2 && ((nx >= 2 && maxW < 0.6f * pitchX) || (nx < 2 && ny >= 2 && maxH < 0.6f * pitchY));
            if (cells) {
                const float right = nx >= 2 ? xs[nx - 1] + (xs[nx - 1] - xs[nx - 2]) : xs[0] + maxW;
                const float bottom = ny >= 2 ? ys[ny - 1] + (ys[ny - 1] - ys[ny - 2]) : ys[0] + maxH;
                for (int i = 0; i < nc; i++) {
                    Cell& c = s_cell[i]; float nxX = right, nxY = bottom;
                    for (int k = 0; k < nx; k++) if (xs[k] > c.x + 6.0f) { nxX = xs[k]; break; }
                    for (int j = 0; j < nc; j++) if (std::fabs(s_cell[j].x - c.x) < 6.0f && s_cell[j].y > c.y + 6.0f) nxY = std::min(nxY, s_cell[j].y);
                    const float gapX = nx >= 2 ? 0.03f * (nxX - c.x) : 0.0f;
                    if (nx >= 2) c.w = std::max(8.0f, nxX - c.x - gapX);
                    c.h = std::max(8.0f, nxY - c.y - 2.0f);
                }
            }
        }
        for (uint32_t r = 0; r < n; r++) {
            uint8_t* node = Child(grp, r);
            if (!Ptr(node) || !NodeFlag(node, 0x0c) || (!NodeFlag(node, 0x10) && !inList)) continue;
            void** vt = *(void***)node; if (!Ptr(vt) || !RowClass(vt)) continue;
            float pos[2] = { 0, 0 }, size[2] = { 0, 0 };
            if (((PFN_NodeVec2)vt[0xa0 / 8])(node, pos) < 0) continue;      // sin clip: el juego usaria valores viejos, aca se saltea
            ((PFN_NodeVec2)vt[0xc0 / 8])(node, size);
            if (!(size[0] > 0.0f) || !(size[1] > 0.0f)) continue;
            nSeen++;
            if (useShown && !AfpShown(node)) { nHidden++; continue; }       // (14/11 f) el juego la da por visible pero no se dibuja
            if (cells) for (int i = 0; i < nc; i++) if (s_cell[i].r == r) { size[0] = s_cell[i].w; size[1] = s_cell[i].h; break; }
            if (!cells) {
                float dx0, dy0, dw, dh;
                if (DrawnRect(node, dx0, dy0, dw, dh)) {
                    const float ix = std::min(pos[0] + size[0], dx0 + dw) - std::max(pos[0], dx0), iy = std::min(pos[1] + size[1], dy0 + dh) - std::max(pos[1], dy0);
                    const bool bogus = !(ix > 0.0f && iy > 0.0f && ix * iy >= 0.5f * size[0] * size[1]);
                    if (mx > dx0 && mx < dx0 + dw && my > dy0 && my < dy0 + dh && dw * dh < drawnA) {
                        drawnA = dw * dh; drawn.group = (int)g; drawn.row = (int)r; drawn.x = dx0; drawn.y = dy0; drawn.w = dw; drawn.h = dh; drawn.dist = 0;
                    }
                    nRows++;
                    if (bogus) {
                        nBogus++;
                        if (mx > pos[0] && pos[0] + size[0] > mx && my > pos[1] && pos[1] + size[1] > my) {
                            const float nx = (mx - (pos[0] + size[0] * 0.5f)) / size[0], ny = (my - (pos[1] + size[1] * 0.5f)) / size[1];
                            const float dd = std::max(std::fabs(nx), std::fabs(ny));
                            if (dd < directAllD) { directAllD = dd; directAll.group = (int)g; directAll.row = (int)r; directAll.x = pos[0]; directAll.y = pos[1]; directAll.w = size[0]; directAll.h = size[1]; directAll.dist = 0; }
                        }
                        continue;
                    }
                }
            }
            if (mx > pos[0] && pos[0] + size[0] > mx && my > pos[1] && pos[1] + size[1] > my) {
                const float nx = (mx - (pos[0] + size[0] * 0.5f)) / size[0], ny = (my - (pos[1] + size[1] * 0.5f)) / size[1];
                const float dd = std::max(std::fabs(nx), std::fabs(ny));
                if (dd < directD) { directD = dd; direct.group = (int)g; direct.row = (int)r; direct.x = pos[0]; direct.y = pos[1]; direct.w = size[0]; direct.h = size[1]; direct.dist = 0; }
                continue;
            }
            if (!(size[0] < 4000.0f) || !(size[1] < 4000.0f) || !(std::fabs(pos[0]) < 8000.0f) || !(std::fabs(pos[1]) < 8000.0f)) continue;
            rows++; bx0 = std::min(bx0, pos[0]); by0 = std::min(by0, pos[1]); bx1 = std::max(bx1, pos[0] + size[0]); by1 = std::max(by1, pos[1] + size[1]);
            const float dx = std::max(std::max(pos[0] - mx, mx - (pos[0] + size[0])), 0.0f), dy = std::max(std::max(pos[1] - my, my - (pos[1] + size[1])), 0.0f);
            const float d = std::sqrt(dx * dx + dy * dy);
            const float cap = wide ? std::min(160.0f, 1.25f * std::max(size[0], size[1])) : std::min(28.0f, 0.30f * std::min(size[0], size[1]));
            if (d <= cap && d < nearD) { nearD = d; cand.group = (int)g; cand.row = (int)r; cand.x = pos[0]; cand.y = pos[1]; cand.w = size[0]; cand.h = size[1]; cand.dist = std::max(d, 0.01f); }
        }
        if (rows >= 2 && cand.group >= 0 && nearD < bestD && mx > bx0 && mx < bx1 && my > by0 && my < by1) { bestD = nearD; best = cand; }
    }
    if (useShown && nSeen > 0 && nHidden == nSeen) return HitTest(I, mx, my, out, false);
    const bool drawnBad = nBogus * 10 > nRows * 6;
    if (drawnBad && directAll.group >= 0 && (direct.group < 0 || directAllD < directD)) { out = directAll; return true; }
    if (direct.group >= 0) { out = direct; return true; }
    if (!drawnBad && drawn.group >= 0) { out = drawn; return true; }
    if (g_cfg.menuMouseNear != 0 && best.group >= 0) { out = best; return true; }
    return false;
}

// lo mismo que el final de 0x140e400d0: si el grupo esta en el vector se suma el desplazamiento de su lista. Devuelve el numero de
// fila que el juego guarda en Impl+0xd8 para la fila dibujada r del grupo g
static int AbsRow(void* win, uint8_t* I, int g, int r) {
    int row = r;
    uint8_t* vb = P8(I + 0xa8); uint8_t* ve = P8(I + 0xb0);
    if (vb != ve && Ptr(vb) && ve > vb && (size_t)(ve - vb) <= 0x18 * 256)
        for (uint8_t* e = vb; e + 0x18 <= ve; e += 0x18) if ((int)U32(e) == g) {
            uint8_t* s = (uint8_t*)((PFN_Scroll)kScroll)(win, g);
            if (Ptr(s)) row += (int)U32(s + 0xc0);
            break;
        }
    return row;
}
static void SelectLikeGame(void* win, int g, int absRow) { ((PFN_Select)kSelect)(win, g, (long long)absRow); }

// (v3) fila con selector de opciones. En el juego "aceptar" sobre una de esas filas no hace nada (0x140e3c660 -> 0x140e3daa0 lo da
// por atendido) y la opcion se cambia con izquierda / derecha (0x140e3c770). Con el mouse: el clic pasa a ser "derecha", o "izquierda"
// si cae en la mitad izquierda del selector. Aca solo se mira lo mismo que mira 0x140e3c770: clase de control 0 o 9 de la fila
// actual (vector Impl+0xe0, 12 bytes {clase, grupo, fila}) y un hijo de tipo 2 visible y activo. Devuelve +1 / -1, o 0 si no hay selector
static int SelectorDir(uint8_t* I, const Hit& hit, float mx) {
    const int g = (int)U32(I + 0xa0), r = (int)U32(I + 0xd8);
    uint8_t* kb = P8(I + 0xe0); uint8_t* ke = P8(I + 0xe8); int kind = -1;
    if (kb == ke || !Ptr(kb) || ke < kb || (size_t)(ke - kb) > 12 * 2048) return 0;
    for (uint8_t* e = kb; e + 12 <= ke; e += 12) if ((int)U32(e + 4) == g && (int)U32(e + 8) == r) { kind = (int)U32(e); break; }
    if (kind != 0 && kind != 9) return 0;
    uint8_t* root = P8(I + 0x98); uint8_t* grp = Ptr(root) ? Child(root, (uint32_t)hit.group) : nullptr;
    uint8_t* row = Ptr(grp) ? Child(grp, (uint32_t)hit.row) : nullptr;
    if (!Ptr(row) || !Ptr(P8(row + 0x98)) || !Ptr(P8(row + 0x98 + 0x78))) return 0;
    uint8_t* sel = ((PFN_ChildByType)kChildByType)(row + 0x98, 2, 0);
    if (!Ptr(sel) || !NodeFlag(sel, 0x0c) || !NodeFlag(sel, 0x10)) return 0;
    void** vt = *(void***)sel; float pos[2] = { 0, 0 }, size[2] = { 0, 0 };
    if (Ptr(vt) && RowClass(vt) && ((PFN_NodeVec2)vt[0xa0 / 8])(sel, pos) >= 0) {
        ((PFN_NodeVec2)vt[0xc0 / 8])(sel, size);
        if (size[0] > 8.0f && size[0] < 4000.0f && mx > pos[0] && mx < pos[0] + size[0] * 0.5f) return -1;
    }
    return 1;
}

// apagado con el menu de PES21FX abierto, con la camara libre y con el panel del Motor en tiempo real abierto (ahi el mouse es del Motor)
static bool Enabled() { return s_ok && g_cfg.menuMouse != 0 && !g_menuOpen.load(std::memory_order_relaxed) && (!FreeCamActive() || MotorPageActive()) && !MotorPanelOpen(); }

static void Sample(double now, bool fresh) {
    s_m = Muestra(); s_m.t = now;
    const bool lNow = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, rNow = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    // (15/11 n, Agustin 05:10: "cuando se selecciona la opcion camara de juego el click derecho hace regresar al menu editar") en el Motor
    // el clic derecho nunca es volver (se sale con su boton Volver o Esc): con la camara del juego tambien es para mirar
    const bool lEdge = lNow && !s_lWas && !fresh, rEdge = rNow && !s_rWas && !fresh && !MotorPageActive();
    s_lWas = lNow; s_rWas = rNow;
    HWND h = OverlayHwnd(); POINT p{}; RECT rc{};
    if (!h || GetForegroundWindow() != h || !GetCursorPos(&p) || !ScreenToClient(h, &p) || !GetClientRect(h, &rc)) { s_last.x = s_last.y = -32000; return; }
    s_m.cw = (float)(rc.right - rc.left); s_m.ch = (float)(rc.bottom - rc.top);
    s_m.moved = (p.x != s_last.x || p.y != s_last.y) && !fresh && s_last.x != -32000;
    s_last = p; s_m.p = p; s_m.lEdge = lEdge; s_m.rEdge = rEdge;
    if (s_m.moved) s_overB = false;
    s_m.ok = s_m.cw >= 160.0f && s_m.ch >= 90.0f && p.x >= 0 && p.y >= 0 && (float)p.x < s_m.cw && (float)p.y < s_m.ch;
    if (rEdge) s_rN = 0;
    s_curX.store(p.x); s_curY.store(p.y);
    if (s_m.ok && (s_m.moved || lEdge || rEdge)) s_moveT.store(now);
}

// (14/11 e) barra de botones del juego: escondida mientras lo ultimo usado sea el mouse (hilo del menu)
static void GuideSync(double now) {
    if (!s_guideOk) return;
    const double mt = s_moveT.load();
    const bool want = g_cfg.menuMouseHideGuide != 0 && Enabled() && mt > 0 && mt > s_padT.load();
    if (want == s_guideHidden && (!want || now - s_guideT < 0.5)) return;       // escondida: se repite cada medio segundo por si el juego la vuelve a mostrar
    uint8_t* gb = *(uint8_t**)kGuideBar; if (!Ptr(gb)) return;
    uint8_t* node = P8(gb + 0x70); if (!Ptr(node)) return;
    void** vt = *(void***)node; if (!Ptr(vt) || !NodeClass(vt)) return;
    uint8_t* hd = P8(node + 0x40); if (!Ptr(hd)) return;
    ((void (*)(void*, unsigned char))kAfpSetVisible)(hd, want ? 0 : 1);
    if (want != s_guideHidden) { static int s_n = 0; if (s_n++ < 6) Log("mouse en menus: barra de botones del juego %s", want ? "escondida (se usa el mouse)" : "a la vista (se uso el mando)"); }
    s_guideHidden = want; s_guideT = now;
}

static bool IsTeamSel(void* win) {
    if (!s_tsOk) return false;
    void** vt = *(void***)win;
    // (15/11) + menu::MenuEditCmnNationalitySelectBase 0x142728bd0 (Editar: elegir liga / nacionalidad): hereda de TeamSelectCommon
    // (mismas ranuras 0x250 / 0x258, su propio mover 0x140c9f310); antes caia en la prueba general = un solo rect gigante y el clic aceptaba
    const uintptr_t mv = Ptr(vt) ? (uintptr_t)vt[0x260 / 8] : 0;
    return Ptr(vt) && (uintptr_t)vt[0x250 / 8] == kTsPrev && (uintptr_t)vt[0x258 / 8] == kTsNext && (mv == kTsMove || mv == 0x140c9f310ull);
}

// rect de un nodo cualquiera: lo que dibuja; si no se puede leer, el del juego
static bool AnyRect(uint8_t* node, float r[4]) {
    if (!Ptr(node)) return false;
    void** vt = *(void***)node; if (!Ptr(vt) || !NodeClass(vt)) return false;
    if (DrawnRect(node, r[0], r[1], r[2], r[3])) return true;
    if (!RowClass(vt)) return false;
    float pos[2] = { 0, 0 }, size[2] = { 0, 0 };
    if (((PFN_NodeVec2)vt[0xa0 / 8])(node, pos) < 0) return false;
    ((PFN_NodeVec2)vt[0xc0 / 8])(node, size);
    if (!(size[0] > 4.0f) || !(size[1] > 4.0f) || !(size[0] < 2600.0f) || !(size[1] < 1600.0f)) return false;
    r[0] = pos[0]; r[1] = pos[1]; r[2] = size[0]; r[3] = size[1]; return true;
}

// (14/11 e) Elegir equipo. Devuelve true si la ventana quedo atendida aca (no sigue la prueba general de filas)
static bool TeamSelStep(void* win, uint8_t* I, float mx, float my, double now) {
    uint8_t* w = (uint8_t*)win;
    static struct { void* win; bool inside; double t; } s_in[4] = {};
    int slot = -1; for (int i = 0; i < 4; i++) if (s_in[i].win == win) slot = i;
    if (slot < 0) { slot = 0; for (int i = 1; i < 4; i++) if (s_in[i].t < s_in[slot].t) slot = i; s_in[slot].win = win; s_in[slot].inside = false; }
    s_in[slot].t = now;
    if (s_tsPendWin == win && s_tsPend != 0) {                             // pasos pedidos por la rueda o por un clic: uno por vez, cuando el juego puede
        if (now - s_tsPendT > 2.0) s_tsPend = 0;
        else if (w[0x120] == 0 && ((I[9] >> 1) & 1) != 0 && Ptr(P8(I + 0x90)) && ((PFN_WinBool)kTsIdle)(win) != 0) {
            void** vt = *(void***)win; w[0x16e] = 0;
            ((void (*)(void*))vt[(s_tsPend > 0 ? 0x258 : 0x250) / 8])(win);
            s_tsPend += s_tsPend > 0 ? -1 : 1; s_tsPendT = now;
            s_nWheel.fetch_add(1, std::memory_order_relaxed);
        }
    }
    const bool act = s_m.moved || s_m.lEdge;
    int hit = -1; bool inside = s_in[slot].inside; int nItems = 0; float rr[9][4] = {};
    if (act) {
        uint8_t* root = P8(I + 0x98); uint8_t* c0 = Ptr(root) ? Child(root, 0) : nullptr; uint8_t* c1 = Ptr(c0) ? Child(c0, 0) : nullptr;
        const char** names = (const char**)kTsNames;
        if (!Ptr(c1) || !Ptr(P8(c1 + 0x98 + 0x88))) return false;
        float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f, bestA = 1e18f;
        for (int k = 0; k < 9; k++) {
            uint8_t* nd = ((PFN_FindChild)kFindChild)(c1 + 0x98, names[k]);
            float* r = rr[k];
            if (!AnyRect(nd, r)) { r[2] = 0; continue; }
            nItems++;
            bx0 = std::min(bx0, r[0]); by0 = std::min(by0, r[1]); bx1 = std::max(bx1, r[0] + r[2]); by1 = std::max(by1, r[1] + r[3]);
            if (mx > r[0] && mx < r[0] + r[2] && my > r[1] && my < r[1] + r[3] && r[2] * r[3] < bestA) { bestA = r[2] * r[3]; hit = k; }
        }
        {   // diagnostico: una vez por clase de ventana, donde quedo cada elemento
            static uintptr_t s_seen[8] = {}; static int s_n = 0; const uintptr_t vt = *(uintptr_t*)win; bool seen = false;
            for (int i = 0; i < s_n; i++) if (s_seen[i] == vt) seen = true;
            if (!seen && s_n < 8) {
                s_seen[s_n++] = vt; char buf[640]; int o = 0;
                for (int k = 0; k < 9 && o < 560; k++) o += snprintf(buf + o, sizeof buf - (size_t)o, " %d:(%.0f %.0f %.0fx%.0f)", k, rr[k][0], rr[k][1], rr[k][2], rr[k][3]);
                Log("mouse en menus: Elegir equipo (vtable %p): %d elementos de la lista medidos;%s; activa %d, modo equipo %d", (void*)vt, nItems, buf, (int)((I[9] >> 1) & 1), (int)w[0x9b]);
            }
        }
        if (nItems == 0) return false;
        inside = mx > bx0 && mx < bx1 && my > by0 && my < by1;
        s_in[slot].inside = inside;
    }
    if (!inside) {
        if (s_tsWin == win) s_tsWin = nullptr;
        if (s_m.rEdge) { bool given = false; for (int i = 0; i < s_rN; i++) if (s_rGiven[i] == win) given = true;
            if (!given && s_rN < 8) { s_rGiven[s_rN++] = win; s_pendWin = win; s_pendAction = 1; } }
        return true;
    }
    s_tsWin = win; s_tsT = now;
    if (act && hit >= 0) s_overB = true;
    if (s_m.moved) s_over.store(s_overB);
    {   // rueda: adelante = el de arriba
        static int s_acc = 0; const int wv = s_wheel.exchange(0);
        s_acc = std::clamp(s_acc + wv, -720, 720);
        while (std::abs(s_acc) >= 120) {
            const int sgn = s_acc > 0 ? 1 : -1; s_acc -= sgn * 120;
            if (s_tsPendWin != win) { s_tsPendWin = win; s_tsPend = 0; }
            s_tsPend = std::clamp(s_tsPend - sgn, -8, 8); s_tsPendT = now; s_moveT.store(now);
        }
    }
    if (s_m.lEdge && hit >= 0) {
        s_m.lEdge = false; s_moveT.store(now);
        static int s_n = 0;
        if (hit == 4) { s_pendWin = win; s_pendAction = 0; if (s_n++ < 12) Log("mouse en menus: Elegir equipo, clic en el elegido -> aceptar"); }
        else { s_tsPendWin = win; s_tsPend = hit - 4; s_tsPendT = now; if (s_n++ < 12) Log("mouse en menus: Elegir equipo, clic en el elemento %d -> %d pasos", hit, hit - 4); }
    } else if (s_m.rEdge) {
        bool given = false; for (int i = 0; i < s_rN; i++) if (s_rGiven[i] == win) given = true;
        if (!given && s_rN < 8) { s_rGiven[s_rN++] = win; s_pendWin = win; s_pendAction = 1; }
    }
    return true;
}

// (15/11 ythnmm, Agustin 04:12: "un boton sobre game settings que permita cerrar el juego, y que se pueda elegir con el mouse",
// "que solo se muestre en la pantalla principal, el menu previo a elegir el modo") boton "Salir del juego" sobre la pestana de mas
// a la derecha de la fila de arriba del menu principal (menu::TopModeSelect 0x142676ea8; en el Dream Patch es GAME SETTINGS),
// solo con esa pagina activa y en su primer nivel (ventana+0x98 == 0: las tarjetas que abren otra lista lo suben a 1, vf49
// 0x140ab0830). Cerrar = lo mismo que la X de la ventana: el bucle principal sigue mientras [0x143713268]+0xfc sea 0 (0x1403a6ac0;
// la X de la ventana y el nodo de flujo ProcExit, 0x141265760, ponen 1). Primer clic: "Salir? clic otra vez"; segundo clic
// (dentro de 4 s) cierra. [PES21FX] mouse_boton_salir = 0 lo saca.
static const uintptr_t kMoveSel = 0x140e3e8c0ull;   // (15/11) mover la seleccion del juego (ver hk_Dirs, paginas sin mando)
static const unsigned char kMoveSelSig[24] = { 0x44,0x88,0x4c,0x24,0x20,0x44,0x89,0x44,0x24,0x18,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56 };
static const uintptr_t kTopModeVt = 0x142676ea8ull, kGameRoot = 0x143713268ull;
// (Agustin 04:19: "que funcione como la opcion de salir") el boton manda el MISMO evento que la opcion Salir del menu principal:
// TopMenu/TopMenuNew.json "24_Exit" -> ProcExit (0x141265760, que solo pone [0x143713268]+0xfc = 1, sin guardar nada aparte).
// Se dispara con 0x140ab1f10 (TopModeSelect: animacion de salida "dmmOut" + 0x140e45290 = evento del flujo, lo mismo que hace su
// vf49 al aceptar una tarjeta). Si a los 3 s el juego sigue (flujo sin ese evento en otro parche), se pone el 1 directo.
static const uintptr_t kTopModeNext = 0x140ab1f10ull;
static const unsigned char kTopModeNextSig[28] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x49,0x8b,0xf8,0x48,0x8b,0x89,0xe0,0x00,0x00,0x00 };
static double s_exitFallT = -1.0;
static void ExitFallback(double now) {
    if (s_exitFallT < 0 || now - s_exitFallT < 3.0) return;
    s_exitFallT = -1.0; uint8_t* gr = *(uint8_t**)kGameRoot;
    if (Ptr(gr) && gr[0xfc] == 0) { gr[0xfc] = 1; Log("mouse en menus: boton Salir: el flujo no cerro en 3 s -> se cierra como la X de la ventana"); }
}
static std::atomic<double> s_exitShowT{-1.0e9}, s_exitArmT{-1.0e9};
static std::atomic<float> s_exitX{0}, s_exitY{0}, s_exitW{0}, s_exitH{0};
static std::atomic<bool> s_exitHover{false};
static bool ExitTabRect(uint8_t* I, float r[4]) {      // pestana de arriba de todo y mas a la derecha del grupo 0 (rect del juego en x / y; ancho dibujado si se puede)
    uint8_t* root = P8(I + 0x98); uint8_t* grp = Ptr(root) ? Child(root, 0) : nullptr; if (!Ptr(grp)) return false;
    const uint32_t n = std::min<uint32_t>(ChildCount(grp), 64);
    float px[64], py[64], pw[64], ph[64]; int m = 0;
    for (uint32_t i = 0; i < n; i++) { uint8_t* nd = Child(grp, i); if (!Ptr(nd)) continue; void** vt = *(void***)nd; if (!Ptr(vt) || !RowClass(vt)) continue;
        if (!NodeFlag(nd, 0x0c)) continue;
        float pos[2] = { 0, 0 }, size[2] = { 0, 0 }; if (((PFN_NodeVec2)vt[0xa0 / 8])(nd, pos) < 0) continue; ((PFN_NodeVec2)vt[0xc0 / 8])(nd, size);
        float d[4]; const bool dk = DrawnRect(nd, d[0], d[1], d[2], d[3]);
        if (!(pos[0] > -100.0f && pos[0] < 2000.0f && pos[1] > -100.0f && pos[1] < 1100.0f)) continue;
        px[m] = pos[0]; py[m] = pos[1]; pw[m] = dk && d[2] > size[0] ? d[2] : size[0]; ph[m] = dk && d[3] > 8.0f ? d[3] : size[1]; m++; }
    if (m < 2) return false;
    float top = 1e9f; for (int i = 0; i < m; i++) top = std::min(top, py[i]);
    int best = -1, cnt = 0; float prevX = -1e9f;
    for (int i = 0; i < m; i++) if (py[i] < top + 12.0f) { cnt++; if (best < 0 || px[i] > px[best]) best = i; }
    if (best < 0 || cnt < 2) return false;                // tiene que haber una fila de pestanas (2 o mas a la misma altura)
    for (int i = 0; i < m; i++) if (i != best && py[i] < top + 12.0f && px[i] > prevX) prevX = px[i];
    float w = pw[best]; const float pitch = px[best] - prevX;
    if (pitch > 40.0f && (w < pitch * 0.6f || w > pitch * 1.4f)) w = pitch - 12.0f;   // tamano del juego que no es el dibujado (126 x 126 en el Dream Patch)
    r[0] = px[best]; r[1] = py[best]; r[2] = std::clamp(w, 120.0f, 700.0f); r[3] = ph[best]; return true;
}
bool MenuExitButton(float* x, float* y, float* w, float* h, int* state) {   // overlay.cpp lo dibuja (pixeles de la ventana)
    if (!s_ok || g_cfg.menuMouse == 0 || g_cfg.menuExitBtn == 0 || Now() - s_exitShowT.load() > 0.25) return false;
    *x = s_exitX.load(); *y = s_exitY.load(); *w = s_exitW.load(); *h = s_exitH.load();
    *state = Now() - s_exitArmT.load() < 4.0 ? 2 : (s_exitHover.load() ? 1 : 0);
    return *w > 4.0f && *h > 4.0f;
}

// (15/11 ythnmm, Agustin 17:24: "le falta opciones en el modo editar jugador opciones para mover la camara, hacer zoom") CAMARA DEL
// VISOR DEL EDITAR CON EL MOUSE. Solo rueda, clic izquierdo y derecho (regla del 14/11), y solo con el mouse FUERA de las filas del menu
// (sobre el jugador): arrastrar con el izquierdo = girar alrededor del jugador, arrastrar con el derecho = correr la camara (arriba /
// abajo / costados), rueda = acercar / alejar. Un clic derecho sin arrastrar sigue siendo "volver" (se manda al soltar). Sobre las filas
// todo sigue como antes (la rueda mueve la lista).
// El visor existe mientras vive edit::ModelView ([0x1438040d0], visor-editar-impl); su camara (CameraImpl) llega cada cuadro a
// RenderTarget::SetCamera 0x1419bf3f0 (gancho de freecam.cpp). Ahi MenuViewCamAdjust corre posicion / objetivo sobre la camara del
// juego (giro y altura alrededor del objetivo, distancia x zoom, corrimiento en el plano de la pantalla) y freecam.cpp devuelve los
// valores del juego despues de mandarla: nada se acumula en el juego y al salir del visor (otro ModelView o ninguno) todo vuelve a cero.
// [PES21FX] mouse_camara_editar = 0 lo apaga.
static const uintptr_t kModelView = 0x1438040d0ull;
static std::atomic<double> s_vcLiveT{-1.0e9};
static std::atomic<int> s_vcDrag{0};                 // 0 nada, 1 girando (izquierdo), 2 corriendo (derecho)
static std::atomic<int> s_vcWheel{0};
static std::atomic<bool> s_vcBackReq{false};          // clic derecho sin arrastrar fuera de las filas: "volver" al soltar
static void* s_vcBackWin = nullptr; static double s_vcBackT = 0;
static std::atomic<float> s_vcDYaw{0}, s_vcDPitch{0}, s_vcDPanX{0}, s_vcDPanY{0};   // pedido del mouse (lo consume MenuViewCamAdjust)
static float s_vcYaw = 0, s_vcPitch = 0, s_vcZoom = 1.0f, s_vcZoomCur = 1.0f, s_vcPan[3] = {};
static uintptr_t s_vcMv = 0; static double s_vcT = 0; static int s_vcLog = 0;
static bool VcLive() { return s_ok && g_cfg.menuMouse != 0 && g_cfg.menuViewCam != 0 && Now() - s_vcLiveT.load() < 0.3; }
static void VcReset(const char* why) {
    if (s_vcYaw != 0 || s_vcPitch != 0 || s_vcZoom != 1.0f || s_vcPan[0] != 0 || s_vcPan[1] != 0 || s_vcPan[2] != 0) Log("mouse en menus: camara del visor del Editar vuelve a la del juego (%s)", why);
    s_vcYaw = s_vcPitch = 0; s_vcZoom = s_vcZoomCur = 1.0f; s_vcPan[0] = s_vcPan[1] = s_vcPan[2] = 0;
    s_vcDYaw = 0; s_vcDPitch = 0; s_vcDPanX = 0; s_vcDPanY = 0; s_vcWheel = 0;
}
// (15/11 b, Agustin 17:31: "lo de arrastrar que detecte el menu a la izquierda para poder seguir haciendo click en las partes del menu o
// volver avanzar") ZONA DEL MENU: todo lo que esta a la izquierda del borde derecho de las filas visibles (mas 32 px) es del menu, aunque
// no haya una fila justo debajo: ahi los clics y la rueda siguen como siempre (elegir, aceptar, volver, mover la lista). La camara solo se
// maneja a la derecha de esa franja. Cada ventana del cuadro suma sus filas (Step); el valor se renueva cada 0.15 s.
static std::atomic<float> s_menuRightPx{-1.0f}; static std::atomic<double> s_menuRightT{-1.0e9};
static void VcMenuExtent(uint8_t* I, double now) {
    uint8_t* root = P8(I + 0x98); if (!Ptr(root)) return;
    const float cw = s_m.cw, ch = s_m.ch; if (cw < 160.0f || ch < 90.0f) return;
    float sw = cw, sh = ch, ox = 0, oy = 0;
    if (cw * 9.0f > ch * 16.0f) { sw = ch * 16.0f / 9.0f; ox = (cw - sw) * 0.5f; } else if (cw * 9.0f < ch * 16.0f) { sh = cw * 9.0f / 16.0f; oy = (ch - sh) * 0.5f; }
    (void)oy; (void)sh;
    float right = -1.0f; int n = 0;
    const uint32_t ng = std::min<uint32_t>(U32(I + 0xa4), 64);
    for (uint32_t g = 0; g < ng; g++) { uint8_t* grp = Child(root, g); if (!Ptr(grp)) continue; const uint32_t nr = std::min<uint32_t>(ChildCount(grp), 512);
        for (uint32_t r = 0; r < nr; r++) { uint8_t* nd = Child(grp, r); if (!Ptr(nd) || !NodeFlag(nd, 0x0c)) continue; void** v = *(void***)nd; if (!Ptr(v) || !RowClass(v)) continue;
            float pos[2] = { 0, 0 }, size[2] = { 0, 0 }; if (((PFN_NodeVec2)v[0xa0 / 8])(nd, pos) < 0) continue; ((PFN_NodeVec2)v[0xc0 / 8])(nd, size);
            float x = pos[0], w = size[0], dy = 0, dw = 0, dh = 0; if (DrawnRect(nd, x, dy, dw, dh) && dw > 0) w = dw; else x = pos[0];
            if (!(x == x) || !(w == w) || w <= 0 || x + w < 0 || x > 1920.0f) continue;
            right = std::max(right, x + w); n++; } }
    if (n == 0) return;
    const float px = ox + right * sw / 1920.0f + 32.0f;
    const bool fresh = now - s_menuRightT.load() > 0.15;
    if (fresh || px > s_menuRightPx.load()) s_menuRightPx.store(px);
    if (fresh) s_menuRightT.store(now);
}
static bool VcInMenu(int x) { return Now() - s_menuRightT.load() < 0.5 && (float)x < s_menuRightPx.load(); }
bool MenuViewCamHint() { return VcLive() && !s_over.load() && !VcInMenu(s_curX.load()); }   // overlay.cpp: la guia de abajo muestra girar / mover / zoom
// lo llama freecam.cpp hk_SetCam antes de mandar la camara; save = 0x34 bytes desde +0x10 (lo devuelve despues)
bool MenuViewCamAdjust(unsigned char* cam, unsigned char* save) {
    if (!s_ok || g_cfg.menuMouse == 0 || g_cfg.menuViewCam == 0 || g_menuOpen.load(std::memory_order_relaxed)) return false;
    const uintptr_t mv = Readable((const void*)kModelView, 8) ? *(const uintptr_t*)kModelView : 0;
    if (!mv) { if (s_vcMv) { VcReset("salio del visor"); s_vcMv = 0; } return false; }
    if (mv != s_vcMv) { VcReset("visor nuevo"); s_vcMv = mv; }
    int proj; memcpy(&proj, cam + 0x3c, 4); if (proj != 0) return false;
    float pos[3], tgt[3], fov; memcpy(pos, cam + 0x10, 12); memcpy(tgt, cam + 0x1c, 12); memcpy(&fov, cam + 0x40, 4);
    for (float f : pos) if (!(f == f) || fabsf(f) > 1.0e5f) return false;
    for (float f : tgt) if (!(f == f) || fabsf(f) > 1.0e5f) return false;
    if (!(fov > 0.005f && fov < 3.0f)) return false;
    const float v[3] = { pos[0] - tgt[0], pos[1] - tgt[1], pos[2] - tgt[2] };
    const float d = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (d < 0.01f) return false;
    s_vcLiveT.store(Now());
    if (s_vcLog < 2) { s_vcLog++; Log("mouse en menus: camara del visor del Editar a mano (camara del juego a %.2f m, campo de vision %.3f): arrastrar = girar, derecho = mover, rueda = zoom", d, fov); }
    // pedido del mouse
    const double now = Now(); const float dt = (float)std::clamp(now - s_vcT, 0.0, 0.1); s_vcT = now;
    s_vcYaw += s_vcDYaw.exchange(0); s_vcPitch += s_vcDPitch.exchange(0);
    { const int w = s_vcWheel.exchange(0); if (w) s_vcZoom = std::clamp(s_vcZoom * powf(0.88f, (float)w / 120.0f), 0.12f, 3.0f); }
    if (fabsf(s_vcZoomCur - s_vcZoom) > 1e-4f) s_vcZoomCur += (s_vcZoom - s_vcZoomCur) * std::clamp(12.0f * dt, 0.0f, 1.0f); else s_vcZoomCur = s_vcZoom;
    const float pitch0 = asinf(std::clamp(v[1] / d, -1.0f, 1.0f)), yaw0 = atan2f(v[0], v[2]);
    s_vcPitch = std::clamp(s_vcPitch, -1.35f - pitch0, 1.35f - pitch0);   // nunca justo arriba / abajo del jugador
    if (s_vcYaw > 6.2831853f || s_vcYaw < -6.2831853f) s_vcYaw = fmodf(s_vcYaw, 6.2831853f);
    const float pt = pitch0 + s_vcPitch, yw = yaw0 + s_vcYaw, dn = d * s_vcZoomCur;
    const float nv[3] = { dn * cosf(pt) * sinf(yw), dn * sinf(pt), dn * cosf(pt) * cosf(yw) };
    // ejes de la camara nueva (derecha / arriba de la pantalla) para correrla en el plano de la pantalla
    const float f[3] = { -nv[0] / dn, -nv[1] / dn, -nv[2] / dn };
    float r[3] = { -f[2], 0.0f, f[0] }; { const float l = sqrtf(r[0] * r[0] + r[2] * r[2]); if (l < 1e-4f) return false; r[0] /= l; r[2] /= l; }   // f x (0,1,0)
    const float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
    { const float px = s_vcDPanX.exchange(0), py = s_vcDPanY.exchange(0);   // fraccion del alto de la pantalla
      if (px != 0 || py != 0) { const float h = 2.0f * dn * tanf(fov * 0.5f);
          for (int i = 0; i < 3; i++) s_vcPan[i] += (-r[i] * px + u[i] * py) * h;
          const float pl = sqrtf(s_vcPan[0] * s_vcPan[0] + s_vcPan[1] * s_vcPan[1] + s_vcPan[2] * s_vcPan[2]), mx = std::max(1.5f, 1.5f * d);
          if (pl > mx) for (float& c : s_vcPan) c *= mx / pl; } }
    if (s_vcYaw == 0 && s_vcPitch == 0 && s_vcZoomCur == 1.0f && s_vcPan[0] == 0 && s_vcPan[1] == 0 && s_vcPan[2] == 0) return false;
    memcpy(save, cam + 0x10, 0x34);
    float nt[3], np[3]; for (int i = 0; i < 3; i++) { nt[i] = tgt[i] + s_vcPan[i]; np[i] = nt[i] + nv[i]; }
    memcpy(cam + 0x10, np, 12); memcpy(cam + 0x1c, nt, 12);
    return true;
}
// cada cuadro (MenuMousePresent): botones y movimiento del mouse para la camara del visor
static void VcInput() {
    static bool s_l = true, s_r = true; static POINT s_p0{}, s_pl{}; static double s_t0 = 0; static bool s_far = false;
    const bool lNow = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, rNow = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool lEdge = lNow && !s_l, rEdge = rNow && !s_r, lUp = !lNow && s_l, rUp = !rNow && s_r; s_l = lNow; s_r = rNow;
    HWND h = OverlayHwnd(); POINT p{}; RECT rc{};
    const bool ok = VcLive() && h && GetForegroundWindow() == h && GetCursorPos(&p) && ScreenToClient(h, &p) && GetClientRect(h, &rc)
        && p.x >= 0 && p.y >= 0 && p.x < rc.right && p.y < rc.bottom && rc.bottom > 90;
    if (!ok) { s_vcDrag = 0; return; }
    const float ch = (float)rc.bottom;
    int drag = s_vcDrag.load();
    if (drag == 0 && (lEdge || rEdge) && !s_over.load() && !VcInMenu(p.x)) {
        drag = lEdge ? 1 : 2; s_vcDrag = drag; s_p0 = s_pl = p; s_t0 = Now(); s_far = false;
        static int s_nl = 0; if (s_nl++ < 6) Log("mouse en menus: camara del visor: %s desde x %d (el menu llega hasta x %.0f de %d)", drag == 1 ? "girar" : "mover", (int)p.x, s_menuRightPx.load(), (int)rc.right);
    }
    if (drag != 0) {
        const float dx = (float)(p.x - s_pl.x), dy = (float)(p.y - s_pl.y); s_pl = p;
        if (abs(p.x - s_p0.x) + abs(p.y - s_p0.y) > 6) s_far = true;
        if (drag == 1 && (dx != 0 || dy != 0)) { const float k = 3.6f / ch;   // alto de la pantalla = ~200 grados
            s_vcDYaw.store(s_vcDYaw.load() - dx * k); s_vcDPitch.store(s_vcDPitch.load() + dy * k); }
        if (drag == 2 && (dx != 0 || dy != 0)) { s_vcDPanX.store(s_vcDPanX.load() + dx / ch); s_vcDPanY.store(s_vcDPanY.load() + dy / ch); }
        if ((drag == 1 && lUp) || (drag == 2 && rUp)) {
            if (drag == 2 && !s_far && Now() - s_t0 < 0.6) s_vcBackReq = true;   // fue un clic derecho: "volver"
            s_vcDrag = 0;
        }
    }
}

static void Step(void* win) {
    const double now = Now();
    const bool fresh = now - s_stepT > 0.25;      // primer paso de un menu despues de un rato sin menus
    const bool batch = fresh || now - s_m.t > 0.004;
    s_stepT = now; s_pendWin = nullptr; s_pendAction = -1; s_pendDxWin = nullptr;
    if (fresh) { s_wheelAcc = 0; s_wheel.store(0); s_latchWin = nullptr; s_extraAct = -1; s_wheelOptWin = nullptr; }
    if (batch) { Sample(now, fresh); GuideSync(now); ExitFallback(now); }   // siempre, para no inventar un clic al volver del menu de PES21FX
    // (12/11) en el Motor el juego no ve el teclado (freecam.cpp FreeCamBlocksKeys): Esc = "volver" lo pone la DLL, este o no el
    // mouse en menus prendido, para que siempre se pueda salir sin mando
    if (g_cfg.motorKeys != 0 && Ptr(win) && MotorPageIs(win) && !g_menuOpen.load(std::memory_order_relaxed)) {
        static bool s_escWas = true; HWND h = OverlayHwnd();
        const bool esc = h && GetForegroundWindow() == h && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        const bool edge = esc && !s_escWas && !fresh && !MotorUiEscBusy(); s_escWas = esc;   // (15/11 g) con un menu o una ventana del Motor abiertos, Esc cierra eso (motorgizmo.cpp)
        const bool btn = MotorUiTakeBack();              // (15/11) boton Volver de la barra de iconos del Motor (motorgizmo.cpp)
        if (edge || btn) { s_pendWin = win; s_pendAction = 1; Log("mouse en menus: %s en el Motor -> volver", btn ? "boton Volver" : "Esc"); return; }
    }
    if (!Enabled() || !Ptr(win)) { s_latchWin = nullptr; return; }
    if (s_rWas && FreeCamActive()) { s_liveT.store(now); return; }   // volando con la camara libre del Motor: el mouse no elige filas
    uint8_t* I = P8((uint8_t*)win + 0x88); if (!Ptr(I)) return;
    s_liveT.store(now); s_nStep.fetch_add(1, std::memory_order_relaxed);
    if (!s_m.ok) return;
    if (s_vcBackReq.load() && (win == s_vcBackWin || now - s_vcBackT > 1.0)) {   // (15/11) clic derecho sin arrastrar sobre el visor: volver
        s_vcBackReq = false; s_vcBackWin = nullptr; s_pendWin = win; s_pendAction = 1; s_moveT.store(now); return;
    }
    if (VcLive()) VcMenuExtent(I, now);
    if (s_vcDrag.load() != 0 || (VcLive() && !s_over.load() && !VcInMenu(s_m.p.x) && (s_m.lEdge || s_m.rEdge))) {   // (15/11) el mouse esta moviendo la camara del visor: no elige filas
        if (s_m.rEdge && (!s_vcBackWin || now - s_vcBackT > 0.1)) { s_vcBackWin = win; s_vcBackT = now; }
        s_m.lEdge = s_m.rEdge = false; s_moveT.store(now); return;
    }
    if (s_m.lEdge && s_movieOk && *(uintptr_t*)win == kMovieVt) {        // video de la intro: clic = saltearlo
        uint8_t* w = (uint8_t*)win;
        if (w[0xa4] == 2 && Ptr(P8(w + 0x90))) {
            s_m.lEdge = false; s_moveT.store(now);
            if (w[0xa5] == 0) { w[0xa5] = 1; ((void (*)(void*))kMovieStop)(P8(w + 0x90)); }
            w[0xa4] = 3; w[0xa6] = 1;
            Log("mouse en menus: clic en el video de la intro -> saltearlo (permitido por el juego %d / %d)", (int)w[0xb9], (int)U32(w + 0xa0));
            return;
        }
    }
    if (s_m.lEdge && s_titleOk && *(uintptr_t*)win == kTitleVt) {        // titulo: clic = "apreta cualquier boton"
        uint8_t* w = (uint8_t*)win;
        if (U32(w + 0x9c) != 1 && w[0xd8] != 0) {
            s_m.lEdge = false; s_moveT.store(now);
            ((void (*)(int))kPadActive)(1); ((void (*)(void*))kTitleGo)(win);
            Log("mouse en menus: clic en la pantalla del titulo -> continuar");
            return;
        }
    }
    {   // (14/11 ythnmm) diagnostico: la primera vez que se usa el mouse en cada clase de pagina, cuantos grupos / filas ve la DLL
        static uintptr_t s_seen[32] = {}; static int s_nSeen = 0; const uintptr_t vt = *(uintptr_t*)win;
        bool seen = false; for (int i = 0; i < s_nSeen; i++) if (s_seen[i] == vt) { seen = true; break; }
        if (!seen && s_nSeen < 32 && (s_m.moved || s_m.lEdge)) {
            s_seen[s_nSeen++] = vt;
            int rows = 0, rowsOk = 0; uint8_t* root = P8(I + 0x98); const uint32_t ng = Ptr(root) ? std::min<uint32_t>(U32(I + 0xa4), 64) : 0;
            for (uint32_t g = 0; g < ng; g++) { uint8_t* grp = Child(root, g); if (!Ptr(grp)) continue; const uint32_t n = std::min<uint32_t>(ChildCount(grp), 512);
                for (uint32_t r = 0; r < n; r++) { uint8_t* nd = Child(grp, r); if (!Ptr(nd)) continue; rows++; void** v = *(void***)nd; if (Ptr(v) && RowClass(v)) rowsOk++; } }
            Log("mouse en menus: pagina vtable %p: puntero propio %d, grupos %u, filas %d (%d que la DLL sabe medir), grupo / fila actual %d / %d, activa %d",
                (void*)vt, (int)I[0x44], ng, rows, rowsOk, (int)U32(I + 0xa0), (int)U32(I + 0xd8), (int)((I[9] >> 1) & 1));
            int shown = 0;                           // (14/11 e) rect del juego y rect dibujado de cada fila (hasta 24 por pagina)
            for (uint32_t g = 0; g < ng && shown < 24; g++) { uint8_t* grp = Child(root, g); if (!Ptr(grp)) continue; const uint32_t n = std::min<uint32_t>(ChildCount(grp), 512);
                for (uint32_t r = 0; r < n && shown < 24; r++) { uint8_t* nd = Child(grp, r); if (!Ptr(nd)) continue; void** v = *(void***)nd; if (!Ptr(v) || !RowClass(v)) continue;
                    float pos[2] = { 0, 0 }, size[2] = { 0, 0 }, d[4] = { 0, 0, 0, 0 };
                    const int pr = ((PFN_NodeVec2)v[0xa0 / 8])(nd, pos); ((PFN_NodeVec2)v[0xc0 / 8])(nd, size);
                    const bool dk = DrawnRect(nd, d[0], d[1], d[2], d[3]); shown++;
                    uint32_t why = 0; const bool sh = AfpShown(nd, &why);
                    char chain[160]; int co = 0; chain[0] = 0;      // cadena de padres del AFP: banderas +0x20 y alfa
                    { uint8_t* hd = P8(nd + 0x40); const uint32_t id = Ptr(hd) ? U32(hd + 0xc) : 0; uint8_t* o = (s_afpOk && ((id >> 27) & 0xf) == 4) ? ((PFN_AfpObj)kAfpObj)(id) : nullptr;
                      for (int i = 0; i < 6 && Ptr(o) && co < 130; i++) { float a; memcpy(&a, o + 0xdc, 4); int16_t dp; memcpy(&dp, o + 0x78, 2);
                          co += snprintf(chain + co, sizeof chain - (size_t)co, " %x/%x/%.2f", U32(o + 0x20) & 0xfffffu, U32(o + 0x24) >> 24, a); if (dp < 1) break; o = P8(o + 0x40); } }
                    Log("mouse en menus:   fila %u / %u: juego %.0f %.0f %.0f x %.0f (%d), dibujo %s %.0f %.0f %.0f x %.0f, visible %d activa %d, mostrada %d (%x), afp%s",
                        g, r, pos[0], pos[1], size[0], size[1], pr, dk ? "si" : "no", d[0], d[1], d[2], d[3], (int)NodeFlag(nd, 0x0c), (int)NodeFlag(nd, 0x10), (int)sh, why, chain); } }
        }
    }
    if (I[0x44]) {                                    // pagina con puntero propio: el mouse mueve ese puntero (FeedOwnPointer); aca solo los clics
        if (g_cfg.menuMouseOwnPtr == 0) return;
        if (s_m.lEdge) { s_m.lEdge = false; s_pendWin = win; s_pendAction = 0; s_moveT.store(now); }      // clic = aceptar (como X / A)
        else if (s_m.rEdge && !FreeCamActive()) {                                                         // derecho = volver
            bool given = false; for (int i = 0; i < s_rN; i++) if (s_rGiven[i] == win) given = true;
            if (!given && s_rN < 8) { s_rGiven[s_rN++] = win; s_pendWin = win; s_pendAction = 1; }
        }
        return;
    }
    // (15/11, Agustin: "si se selecciona un partido y detecta el mouse moverse al momento de cargar se abre el menu que permite cambiarse
    // de local o visitante") la pantalla de elegir lado (menu::SideSelect 0x14267c718 / menu::SideSelectEach 0x14271ecf8) sigue armada
    // durante la carga: el log del 14/11 22:16:24 muestra al mouse eligiendo su fila 2 con 16330 cuadros de menu (carga). Ahi el mouse solo
    // actua si la ventana esta activa para el mando (Impl+9 bit 1, lo mismo que mira 0x140e3f270) y con un clic, nunca solo por moverse.
    const uintptr_t pageVt = *(uintptr_t*)win;
    const bool sideSel = pageVt == 0x14267c718ull || pageVt == 0x14271ecf8ull;
    if (sideSel) {
        if (((I[9] >> 1) & 1) == 0) { if (s_m.lEdge || s_m.rEdge) { static int s_n = 0; if (s_n++ < 6) Log("mouse en menus: elegir lado inactivo (carga): el mouse no lo toca"); } return; }
        if (!s_m.lEdge && !s_m.rEdge) return;
    }
    if (s_latchWin == win) {                          // clic del paso anterior que primero tuvo que elegir la fila
        const bool same = (int)U32(I + 0xa0) == s_latchG && (int)U32(I + 0xd8) == s_latchR && now - s_latchT < 0.25;
        s_latchWin = nullptr;
        if (same) {
            if (s_latchDir != 0) { s_pendDxWin = win; s_pendDx = s_latchDir; } else { s_pendWin = win; s_pendAction = 0; }
            return;
        }
    }
    // escenario de 1920 x 1080 centrado en la ventana
    const float cw = s_m.cw, ch = s_m.ch; float sw = cw, sh = ch, ox = 0, oy = 0;
    if (cw * 9.0f > ch * 16.0f) { sw = ch * 16.0f / 9.0f; ox = (cw - sw) * 0.5f; } else if (cw * 9.0f < ch * 16.0f) { sh = cw * 9.0f / 16.0f; oy = (ch - sh) * 0.5f; }
    const float mx = ((float)s_m.p.x - ox) * 1920.0f / sw, my = ((float)s_m.p.y - oy) * 1080.0f / sh;
    if (g_cfg.menuExitBtn != 0 && pageVt == kTopModeVt) {                  // (15/11) boton "Salir del juego" del menu principal
        float r[4];
        if (((I[9] >> 1) & 1) != 0 && *(int32_t*)((uint8_t*)win + 0x98) == 0 && ExitTabRect(I, r)) {
            // (Agustin 04:16: "un icono que quede ni muy grande y similar al estilo del resto de cartas del menu") cuadrado chico con el icono de apagar, alineado al borde derecho de la pestana
            const float bh = 54.0f, gap = 12.0f, bw = bh, bx = r[0] + r[2] - bw, by = std::max(4.0f, r[1] - gap - bh);
            s_exitX.store(ox + bx * sw / 1920.0f); s_exitY.store(oy + by * sh / 1080.0f); s_exitW.store(bw * sw / 1920.0f); s_exitH.store(bh * sh / 1080.0f);
            s_exitShowT.store(now);
            const bool over = mx >= bx && mx < bx + bw && my >= by && my < by + bh;
            s_exitHover.store(over);
            if (s_m.lEdge && over) {
                s_m.lEdge = false; s_moveT.store(now);
                if (now - s_exitArmT.load() < 4.0) {
                    if (SigAt(kTopModeNext, kTopModeNextSig, sizeof kTopModeNextSig)) {
                        ((void (*)(void*, unsigned char, const char*))kTopModeNext)(win, 0, "24_Exit");
                        Log("mouse en menus: boton Salir del juego -> evento 24_Exit (la opcion Salir del menu principal)");
                    } else Log("mouse en menus: boton Salir del juego: 0x140ab1f10 no coincide, se cierra como la X de la ventana");
                    s_exitFallT = SigAt(kTopModeNext, kTopModeNextSig, sizeof kTopModeNextSig) ? now : now - 3.0;
                    ExitFallback(now);
                    s_exitArmT.store(-1.0e9);
                } else { s_exitArmT.store(now); Log("mouse en menus: boton Salir del juego: primer clic (falta confirmar)"); }
                return;
            }
            if (s_m.lEdge) s_exitArmT.store(-1.0e9);                        // clic en otro lado: se desarma
            if (over) { s_overB = true; s_over.store(true); return; }        // sobre el boton: no elige filas del menu (mano del dardo)
        }
    }
    if (g_cfg.menuMouseTeamSel != 0 && IsTeamSel(win) && TeamSelStep(win, I, mx, my, now)) return;
    if (s_extraAct >= 0) {   // (14/11 ythnmm) accion pedida: la ventana bajo el mouse que tenga esa accion; si ninguna, la primera que la tenga
        if (now - s_extraT > 0.5) s_extraAct = -1;
        else {
            const uint32_t mode = U32(I + 0x120 + (size_t)s_extraAct * 4);
            Hit h2; const bool over = HitTest(I, mx, my, h2) && h2.dist <= 0.0f;
            if (mode >= 1 && mode <= 3 && (over || now - s_extraT > 0.05)) {
                s_pendWin = win; s_pendAction = s_extraAct; s_extraAct = -1; s_moveT.store(now);
                const int k = s_nExtra.fetch_add(1, std::memory_order_relaxed);
                if (k < 12) Log("mouse en menus: accion pedida -> %d de la pagina (vtable %p)", s_pendAction, *(void**)win);
                return;
            }
        }
    }
    if (!s_m.moved && !s_m.lEdge && !s_m.rEdge) return;
    {   // (v4) panel del Motor: arrastrar desde la franja del titulo (motorpage.cpp). La primera fila dibujada del grupo 0 da el lugar
        float tx = 0, ty = 0, tw = 0;
        uint8_t* root0 = P8(I + 0x98); uint8_t* g0 = Ptr(root0) ? Child(root0, 0) : nullptr; uint8_t* r0 = Ptr(g0) ? Child(g0, 0) : nullptr;
        if (Ptr(r0)) { void** vt = *(void***)r0; float pos[2] = { 0, 0 }, size[2] = { 0, 0 };
            if (Ptr(vt) && RowClass(vt) && ((PFN_NodeVec2)vt[0xa0 / 8])(r0, pos) >= 0) { ((PFN_NodeVec2)vt[0xc0 / 8])(r0, size); tx = pos[0]; ty = pos[1]; tw = size[0]; } }
        if (MotorPageDrag(win, mx, my, s_lWas, s_m.lEdge, tx, ty, tw)) { s_m.lEdge = false; s_moveT.store(now); return; }
    }
    if (MotorGizmoOver((float)s_m.p.x, (float)s_m.p.y)) { s_m.lEdge = false; s_moveT.store(now); return; }   // el gizmo de camara del Motor (motorgizmo.cpp): el clic es suyo
    Hit hit; const bool on = (s_m.moved || s_m.lEdge) && HitTest(I, mx, my, hit);
    // (v3) la fila actual del juego (Impl+0xd8) cuenta el desplazamiento de la lista: se compara con ese numero, no con el de la fila dibujada
    const int hitAbs = on ? AbsRow(win, I, hit.group, hit.row) : -1;
    if (on && hit.dist <= 0.0f) s_overB = true;
    if (s_m.moved) s_over.store(s_overB);
    bool chose = false;
    if (on && !((int)U32(I + 0xa0) == hit.group && (int)U32(I + 0xd8) == hitAbs)) {
        if (PadSel* ps = PadSelFor(win, true)) { if (!ps->dirty || ps->vt != *(uintptr_t*)win) { ps->vt = *(uintptr_t*)win; ps->g = (int)U32(I + 0xa0); ps->r = (int)U32(I + 0xd8); ps->dirty = true; } ps->t = now; }
        SelectLikeGame(win, hit.group, hitAbs); chose = true;
        const int k = s_nSel.fetch_add(1, std::memory_order_relaxed);
        if (hit.dist > 0) s_nNear.fetch_add(1, std::memory_order_relaxed);
        if (k < 12 || (hit.dist > 0 && s_nNear.load(std::memory_order_relaxed) <= 8))
            Log("mouse en menus: fila %s -> grupo %d fila %d (rect %.0f %.0f %.0f x %.0f; mouse %.0f %.0f de 1920 x 1080; a %.0f; ventana %.0f x %.0f; seleccion ahora %d / %d)",
                hit.dist > 0 ? "mas cercana" : "bajo el mouse", hit.group, hit.row, hit.x, hit.y, hit.w, hit.h, mx, my, hit.dist, cw, ch, (int)U32(I + 0xa0), (int)U32(I + 0xd8));
    }
    if (s_m.moved) {   // (14/11 ythnmm) rueda sobre un selector = cambiar la opcion (si la fila bajo el mouse ya es la elegida)
        if (on && hit.dist <= 0.0f && g_cfg.menuMouseWheelOpt != 0 && (int)U32(I + 0xa0) == hit.group && (int)U32(I + 0xd8) == hitAbs && SelectorDir(I, hit, mx) != 0) { s_wheelOptWin = win; s_wheelOptMx = mx; }
        else if (s_wheelOptWin == win) s_wheelOptWin = nullptr;
    }
    if (s_m.lEdge && on) {
        s_m.lEdge = false;                            // un clic es para una sola ventana
        if ((int)U32(I + 0xa0) == hit.group && (int)U32(I + 0xd8) == hitAbs) {
            const int dir = SelectorDir(I, hit, mx);     // fila con selector: el clic cambia la opcion en vez de "aceptar"
            if (chose) { s_latchWin = win; s_latchG = hit.group; s_latchR = hitAbs; s_latchDir = dir; s_latchT = now; }   // como con el mando: mover y aceptar en pasos distintos
            else if (dir != 0) { s_pendDxWin = win; s_pendDx = dir; }
            else { s_pendWin = win; s_pendAction = 0; }
        }
    } else if (s_m.rEdge && !FreeCamActive()) {       // con la camara libre del Motor el boton derecho es para volar
        bool given = false; for (int i = 0; i < s_rN; i++) if (s_rGiven[i] == win) given = true;
        if (!given && s_rN < 8) { s_rGiven[s_rN++] = win; s_pendWin = win; s_pendAction = 1; }
    }
}

// (15/11 ythnmm) paginas con puntero propio del juego (Impl+0x44: Plan de juego, myClub, paleta de colores). El juego mueve ese
// puntero con el analogico: menusystem::PointingCursor (singleton 0x143707fe8, vtable 0x1427c8500, 8 punteros de 0x58 bytes desde +0x80;
// +0x10 estado 0..2 = a la vista, +0x14 posicion x / y en el escenario de 1920 x 1080, +0x3c cuadros de "salto" animado, +0x48 limites
// propios en +0x2c, si no los generales en +0x70). La prueba nativa de 0x140e400d0 lee esa posicion (FUN_140e69a70) y elige la fila;
// se salta si +0x3c no es 0 (FUN_140e69ea0). Aca, ANTES de esa prueba, el mouse escribe la posicion cuando se movio: el juego elige
// la fila como con el analogico y el analogico sigue desde donde quedo.
static const uintptr_t kPtrMgr = 0x143707fe8ull, kPtrMgrVt = 0x1427c8500ull;
static void FeedOwnPointer(void* win) {
    static POINT s_last[8]; static bool s_have[8] = {}; static int s_nLog = 0;
    if (!s_ok || !Ptr(win) || g_cfg.menuMouseOwnPtr == 0 || !Enabled()) return;
    uint8_t* I = P8((uint8_t*)win + 0x88); if (!Ptr(I) || !I[0x44] || (!I[0x45] && !I[0x46])) return;
    const uint32_t idx = U32(I + 0x114); if (idx >= 8) return;
    uint8_t* mgr = *(uint8_t**)kPtrMgr; if (!Ptr(mgr) || *(uintptr_t*)mgr != kPtrMgrVt) return;
    uint8_t* c = mgr + 0x80 + (size_t)idx * 0x58;
    HWND h = OverlayHwnd(); POINT p{}; RECT rc{};
    if (!h || GetForegroundWindow() != h || !GetCursorPos(&p) || !ScreenToClient(h, &p) || !GetClientRect(h, &rc)) { s_have[idx] = false; return; }
    const float cw = (float)(rc.right - rc.left), ch = (float)(rc.bottom - rc.top);
    if (cw < 160.0f || ch < 90.0f || p.x < 0 || p.y < 0 || (float)p.x >= cw || (float)p.y >= ch) return;
    const bool moved = s_have[idx] && (p.x != s_last[idx].x || p.y != s_last[idx].y);
    s_last[idx] = p; s_have[idx] = true;
    if (!moved || c[0x10] > 2) return;                // sin moverse no se pisa el analogico; puntero escondido: la pagina no lo usa
    float sw = cw, sh = ch, ox = 0, oy = 0;
    if (cw * 9.0f > ch * 16.0f) { sw = ch * 16.0f / 9.0f; ox = (cw - sw) * 0.5f; } else if (cw * 9.0f < ch * 16.0f) { sh = cw * 9.0f / 16.0f; oy = (ch - sh) * 0.5f; }
    float mx = ((float)p.x - ox) * 1920.0f / sw, my = ((float)p.y - oy) * 1080.0f / sh;
    float b[4]; memcpy(b, c[0x48] ? c + 0x2c : mgr + 0x70, sizeof b);
    if (b[2] > 1.0f && b[3] > 1.0f && b[2] < 10000.0f && b[3] < 10000.0f) { mx = std::clamp(mx, b[0], b[0] + b[2]); my = std::clamp(my, b[1], b[1] + b[3]); }
    memcpy(c + 0x14, &mx, 4); memcpy(c + 0x18, &my, 4);
    *(uint32_t*)(c + 0x3c) = 0;
    s_moveT.store(Now());
    if (s_nLog < 10) { s_nLog++; Log("mouse en menus: puntero propio %u de la pagina (vtable %p) -> %.0f %.0f (limites %.0f %.0f %.0f x %.0f)", idx, *(void**)win, mx, my, b[0], b[1], b[2], b[3]); }
}

static void hk_Hover(void* win) {
    FeedOwnPointer(win);
    o_hover(win);
    MotorPageSeen(win);     // motorpage.cpp: para saber si el estudio del Motor esta abierto (icono de la ultima tecla)
    Step(win);
}

static void hk_Dirs(void* win, int* dx, int* dy, unsigned char* trig) {
    o_dirs(win, dx, dy, trig);
    if (dx && dy && (*dx != 0 || *dy != 0)) {                            // (14/11 e) el mando / teclado movio: vuelve la barra de botones, se va la flecha
        const bool mouseLast = s_moveT.load() > s_padT.load();
        s_padT.store(Now());
        PadSel* ps = PadSelFor(win, false);
        if (ps && ps->dirty) {                                           // (14/11 g) vuelve la seleccion que tenia el mando
            ps->dirty = false;
            uint8_t* I = Ptr(win) ? P8((uint8_t*)win + 0x88) : nullptr;
            if (mouseLast && g_cfg.menuMousePadReset != 0 && Enabled() && Ptr(I) && ps->vt == *(uintptr_t*)win && Now() - ps->t < 300.0 && ps->g >= 0 && ps->r >= 0 && (uint32_t)ps->g < U32(I + 0xa4) &&
                !((int)U32(I + 0xa0) == ps->g && (int)U32(I + 0xd8) == ps->r)) {
                SelectLikeGame(win, ps->g, ps->r);
                *dx = 0; *dy = 0; if (trig) *trig = 0;
                static int s_n = 0; if (s_n++ < 8) Log("mouse en menus: se uso el mando -> vuelve la seleccion a grupo %d fila %d", ps->g, ps->r);
            }
        }
    }
    const bool ts = s_tsWin != nullptr && Now() - s_tsT < 0.2;           // el mouse esta sobre la lista de Elegir equipo: la rueda es de esa ventana (TeamSelStep)
    const int w = ts ? 0 : s_wheel.exchange(0);
    void* const optWin = s_pendDxWin; s_pendDxWin = nullptr;
    if (!Enabled() || !dx || !dy) { s_wheelAcc = 0; return; }
    {   // (15/11 05:39 cierre al entrar a Editar jugadores, 0xc0000005 en 0x1408ec1c0 < 0x140e69a94 < 0x140e3cc9b, RAX = FFFFFFFF)
        // el juego mueve la seleccion con 0x140e3e8c0 -> 0x140e3cbd0, que lee el puntero numero Impl+0x114 SIN mirar si es valido
        // (0x140e69a70: base + 0x94 + n * 0x58). El juego solo llega ahi con un mando de verdad (0x140e3e6b0 sale si
        // 0x141b57c60(Impl+0x114) no da mando); si la pagina no tiene mando (Impl+0x114 = -1) la rueda / el clic sobre un selector
        // NO pueden mandar direcciones: se descartan.
        // (15/11 06:04, Agustin: "en el menu editar la rueda del mouse no funciona") en esas paginas la DLL mueve ella misma con la MISMA
        // funcion del juego (0x140e3e8c0, lo que 0x140e3e480 llama despues de leer las direcciones) con Impl+0x114 = 0 solo durante esa
        // llamada: 0x140e3cbd0 lee el puntero 0 (memoria valida) y lo pisa con la posicion de la fila actual si hay una.
        uint8_t* I = P8((uint8_t*)win + 0x88);
        if (!Ptr(I) || U32(I + 0x114) >= 8u) {
            int mdx = 0, mdy = 0; unsigned char mtr = 1;
            if (optWin && optWin == win && *dx == 0 && *dy == 0) mdx = s_pendDx > 0 ? 1 : -1;
            else {
                s_wheelAcc = std::clamp(s_wheelAcc + w, -600, 600);
                HWND h = OverlayHwnd();
                if (std::abs(s_wheelAcc) >= 120 && *dx == 0 && *dy == 0 && h && GetForegroundWindow() == h) {
                    const int sgn = s_wheelAcc > 0 ? 1 : -1; s_wheelAcc -= sgn * 120;
                    if (s_wheelOptWin == win) mdx = sgn > 0 ? 1 : -1; else { mdy = sgn > 0 ? -1 : 1; mtr = 0; }
                }
            }
            if ((mdx != 0 || mdy != 0) && Ptr(I) && SigAt(kMoveSel, kMoveSelSig, sizeof kMoveSelSig)) {
                const uint32_t keep = U32(I + 0x114);
                *(uint32_t*)(I + 0x114) = 0;
                ((int (*)(void*, int, int, unsigned char))kMoveSel)(win, mdx, mdy, mtr);
                *(uint32_t*)(I + 0x114) = keep;
                s_pendWin = nullptr; s_pendAction = -1; s_moveT.store(Now());
                s_nWheel.fetch_add(1, std::memory_order_relaxed);
                static int s_n = 0; if (s_n++ < 6) Log("mouse en menus: pagina sin mando (vtable %p): la DLL mueve %d / %d con 0x140e3e8c0", *(void**)win, mdx, mdy);
            }
            return;
        }
    }
    if (optWin && optWin == win && *dx == 0 && *dy == 0) {               // (v3) clic sobre un selector: lo mismo que apretar derecha / izquierda
        *dx = s_pendDx > 0 ? 1 : -1; if (trig) *trig = 1;                 // 1 = recien apretado: al llegar a la ultima opcion da la vuelta
        s_wheelAcc = 0; s_moveT.store(Now());
        const int k = s_nOpt.fetch_add(1, std::memory_order_relaxed);
        if (k < 8) Log("mouse en menus: clic sobre un selector -> opcion %s", s_pendDx > 0 ? "siguiente" : "anterior");
        return;
    }
    s_wheelAcc = std::clamp(s_wheelAcc + w, -600, 600);
    if (std::abs(s_wheelAcc) < 120 || *dx != 0 || *dy != 0) return;       // el mando / teclado mandan
    HWND h = OverlayHwnd(); if (!h || GetForegroundWindow() != h) { s_wheelAcc = 0; return; }
    const int sgn = s_wheelAcc > 0 ? 1 : -1; s_wheelAcc -= sgn * 120;
    if (s_wheelOptWin == win) {                                          // sobre un selector: la rueda cambia la opcion (adelante = siguiente)
        *dx = sgn > 0 ? 1 : -1; if (trig) *trig = 1;
        s_pendWin = nullptr; s_pendAction = -1;
        s_nWheel.fetch_add(1, std::memory_order_relaxed); s_nOpt.fetch_add(1, std::memory_order_relaxed); s_moveT.store(Now());
        return;
    }
    *dy = sgn > 0 ? -1 : 1;                                             // rueda hacia adelante = arriba
    s_pendWin = nullptr; s_pendAction = -1;
    s_nWheel.fetch_add(1, std::memory_order_relaxed); s_moveT.store(Now());
}

static char hk_Action(void* win, void* pad, int action) {
    if (win && pad && s_pendWin == win && s_pendAction == action && action >= 0 && action < 12) {
        uint8_t* I = P8((uint8_t*)win + 0x88);
        const uint32_t mode = Ptr(I) ? U32(I + 0x120 + (size_t)action * 4) : 0;
        if (mode >= 1 && mode <= 3) {                                    // la pagina tiene esa accion
            s_pendWin = nullptr; s_pendAction = -1;
            if (action < 2) (action == 0 ? s_nClick : s_nBack).fetch_add(1, std::memory_order_relaxed);
            return 1;
        }
    }
    const char r = o_action(win, pad, action);
    if (r) s_padT.store(Now());                                          // (14/11 e) boton del mando / teclado
    return r;
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
    s_titleOk = SigAt(kPadActive, kPadActiveSig, sizeof kPadActiveSig) && SigAt(kTitleGo, kTitleGoSig, sizeof kTitleGoSig);
    s_movieOk = SigAt(kMovieStop, kMovieStopSig, sizeof kMovieStopSig);
    s_afpOk = SigAt(kAfpGet, kAfpGetSig, sizeof kAfpGetSig) && SigAt(kAfpObj, kAfpObjSig, sizeof kAfpObjSig);
    s_guideOk = s_afpOk && SigAt(kAfpSetVisible, kAfpSetVisibleSig, sizeof kAfpSetVisibleSig) && Readable((const void*)kGuideBar, 8);
    s_tsOk = SigAt(kFindChild, kFindChildSig, sizeof kFindChildSig) && SigAt(kTsIdle, kTsIdleSig, sizeof kTsIdleSig) && Readable((const void*)kTsNames, 72);
    if (s_tsOk) { const char** names = (const char**)kTsNames; for (int k = 0; k < 9; k++) if (!Readable(names[k], 11) || memcmp(names[k], "listItem_", 9) != 0) s_tsOk = false; }
    Log("mouse en menus: rect dibujado %d, barra de botones %d, Elegir equipo %d (1 = firmas del exe bien)", (int)s_afpOk, (int)s_guideOk, (int)s_tsOk);
    if (!s_titleOk) Log("mouse en menus: firma del titulo distinta (0x141b58200 / 0x140a99250): sin clic en el titulo");
    s_ok = true;
    Log("mouse en menus: listo (prueba del puntero 0x140e400d0, direcciones 0x140e3e6b0, acciones 0x140e48f80); mouse_menus = %d (0 lo apaga)", g_cfg.menuMouse);
}

void MenuMouseWheel(int delta) {
    if (!s_ok || g_cfg.menuMouse == 0) return;
    if (VcLive() && !s_over.load() && !VcInMenu(s_curX.load())) { s_vcWheel.fetch_add(delta, std::memory_order_relaxed); return; }   // (15/11) rueda sobre el jugador del Editar = zoom
    s_wheel.fetch_add(delta, std::memory_order_relaxed);
}

bool MenuMouseOverRow() { return s_over.load(); }   // (14/11 d) para la mano del cursor sobre los menus del juego

// para el cursor: posicion en pixeles de la ventana si el mouse se uso hace poco dentro de un menu del juego
bool MenuMouseCursor(float* x, float* y) {
    if (!s_ok || g_cfg.menuMouse == 0) return false;
    const double now = Now();
    if (now - s_liveT.load() > 0.25 || now - s_moveT.load() > 4.0) return false;
    if (s_padT.load() > s_moveT.load()) return false;                    // (14/11 e) se uso el mando despues del mouse: la flecha se va enseguida
    CURSORINFO ci{}; ci.cbSize = sizeof ci;
    if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) return false;   // Windows ya muestra el suyo
    // (v2) el juego tarda 2 cuadros en mostrar la fila elegida (medido en video a 60 fps): la flecha se dibuja con ese mismo retraso
    // para que flecha y fila marcada vayan juntas ([PES21FX] mouse_menus_retraso, 0..4 cuadros)
    static int s_hx[8] = {}, s_hy[8] = {}; static unsigned s_hn = 0; static double s_hT = 0;
    const int cx = s_curX.load(), cy = s_curY.load();
    if (now - s_hT > 0.25) for (int i = 0; i < 8; i++) { s_hx[i] = cx; s_hy[i] = cy; }      // primera vez despues de un rato: sin saltos
    s_hT = now; s_hn++; s_hx[s_hn & 7] = cx; s_hy[s_hn & 7] = cy;
    const unsigned d = (unsigned)std::clamp(g_cfg.menuMouseDelay, 0, 4);
    if (x) *x = (float)s_hx[(s_hn - d) & 7]; if (y) *y = (float)s_hy[(s_hn - d) & 7];
    return true;
}

void MenuMousePresent() {
    if (!s_tried) Install();
    if (!s_ok) return;
    VcInput();
    {   // (v2) atajo para sacar / poner el mouse en los menus (y su flecha): [PES21FX] tecla_mouse_menus (sin tecla de fabrica; ej. F8).
        // No se guarda en el ini: al abrir el juego vale lo que diga mouse_menus
        static bool s_was = false; const int vk = g_cfg.menuMouseKey;
        const bool down = vk > 0 && vk < 256 && (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (down && !s_was && !g_menuTyping.load(std::memory_order_relaxed)) {
            HWND h = OverlayHwnd();
            if (h && GetForegroundWindow() == h && !(GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000)) {
                g_cfg.menuMouse = g_cfg.menuMouse ? 0 : 1;
                if (g_cfg.menuMouse) s_moveT.store(Now());
                Log("mouse en menus: %s con la tecla", g_cfg.menuMouse ? "prendido" : "apagado");
            }
        }
        s_was = down;
    }
    static double s_t = 0; static int s_a[5] = {};
    const double t = Now();
    if (s_t == 0) { s_t = t; return; }
    if (t - s_t < 30.0) return;
    const int b[5] = { s_nStep.load(), s_nSel.load(), s_nClick.load(), s_nBack.load(), s_nWheel.load() };
    if (b[1] != s_a[1] || b[2] != s_a[2] || b[3] != s_a[3] || b[4] != s_a[4])
        Log("mouse en menus (30 s): %d cuadros de menu, %d filas elegidas con el mouse (%d por cercania en total), %d clics de aceptar, %d de volver, %d pasos de rueda; %d clics sobre selectores en total",
            b[0] - s_a[0], b[1] - s_a[1], s_nNear.load(), b[2] - s_a[2], b[3] - s_a[3], b[4] - s_a[4], s_nOpt.load());
    memcpy(s_a, b, sizeof s_a); s_t = t;
}

} // namespace fx
