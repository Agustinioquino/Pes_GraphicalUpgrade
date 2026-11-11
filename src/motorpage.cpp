// PES21 FX (11/11/2026, hilo del Motor en tiempo real) - EL MOTOR EN EL MENU NATIVO DEL JUEGO
//
// Agustin (20:23 - 20:38): el Motor sin el overlay de Sider, con los MENUS DEL JUEGO, manejables con teclado, mouse y control;
// la DLL solo para lo que el juego no hace. Leido en el exe 1.07.02 (memoria del proyecto panel-nativo-motor-diseno):
//  - El boton 12 del menu de Editar ("Motor en tiempo real", modulo de Sider) abre el estudio 3D: un nodo de flujo propio
//    (BugCheck/BugCheckSim...) cuya pagina es la de Apariencia del juego (menu::MenuEditPlAppearance, tabla 0x1426edeb8),
//    una lista nativa (MenuEditCmnSettingList) con el visor 3D al lado.
//  - Todas las paginas del Editar nacen en 0x140a977d0(clave, nombre del nodo, bandera). Gancho ahi: si la pagina recien
//    creada es la de Apariencia y el nodo es uno de los del Motor, se le cambia la tabla de funciones por una COPIA con
//    ocho ranuras propias. La pagina de Apariencia de verdad (Editar > Jugadores) no se toca.
//  - Con eso las filas pasan a ser las del Motor (lista nativa: mismo dibujo, sonido, cursor, mouse y control del juego):
//    "Vista", "Alambre" y "Pantalla partida", cada una con el selector nativo de izquierda / derecha. Cambiar una fila
//    escribe la fila 429 (la misma que usan el menu de PES21FX y el panel de Sider).
//  - Vestir al jugador: la pagina de Apariencia pide el modelo pero no el uniforme (por eso el estudio salia sin camiseta).
//    Antes de que la pagina pida el modelo se llama a la funcion del juego que viste al visor (0x141f06920), igual que hace
//    el juego con su maniqui (0x140ce8ef0): mismo hilo, mismo momento, sin Lua en el medio (la 0.6 del modulo se colgaba
//    por llamarla desde Lua). Equipo: el del ultimo jugador que se vio en el Editar si es el mismo; si no, "sin equipo"
//    (0xfffcc000), el valor que usa el propio juego.
//  - El boton "restablecer apariencia" de esa pagina (accion 3, escribe datos del jugador) queda anulado en la copia, y tambien
//    el "aceptar" de la pagina (mandaba el evento de la fila al flujo: salir a Cara / Peinado / Fisico).
//  - Segunda lectura (22:40): el nombre que recibe la fabrica es el del nodo del flujo (nodo+0x38; 0x141fb1f80 -> "menu" ->
//    0x140a958e0); la pagina de Apariencia solo esta en la tabla del Editar (0x14266b620, entrada 75); aceptar sobre una fila
//    con selector no hace nada en el juego (0x140e3daa0), la opcion se cambia con izquierda / derecha (0x140e3c770), que avisa
//    por la ranura 63; con el mouse, el clic sobre el selector lo convierte menumouse.cpp en "derecha".
// Nada se escribe en el exe. [PES21FX] motor_pagina = 0 deja la pagina como antes; motor_vestir = 0 no viste.
#include "core.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace fx {

static const unsigned kStamp = 0x62787C6A;
static const uintptr_t kFactory = 0x140a977d0ull;          // fabrica de paginas del Editar (clave, nombre, bandera)
static const uintptr_t kVtApp = 0x1426edeb8ull;            // menu::MenuEditPlAppearance (89 ranuras)
static const int kSlots = 89;
// 0x140a977d0: mov [rsp+8],rbx; mov [rsp+10h],rbp; mov [rsp+18h],rsi; push rdi; push r12..r15; sub rsp,20h; xor r15d,r15d
static const unsigned char kFactorySig[31] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x20,0x45,0x33,0xff };
// 0x141f06920: mov [rsp+8],rbx; mov [rsp+18h],r8d; push rdi; sub rsp,50h; lea r10,[rcx+0C8h]
static const unsigned char kDressSig[22] = { 0x48,0x89,0x5c,0x24,0x08,0x44,0x89,0x44,0x24,0x18,0x57,0x48,0x83,0xec,0x50,0x4c,0x8d,0x91,0xc8,0x00,0x00,0x00 };
// ranuras de la tabla que se comprueban antes de copiarla (indice, valor esperado)
static const struct { int slot; uintptr_t fn; } kExpect[] = {
    { 49, 0x140c09410ull }, { 50, 0x140c09370ull }, { 52, 0x140c09470ull }, { 62, 0x141b42640ull }, { 63, 0x141b42640ull },
    { 75, 0x140c09280ull }, { 76, 0x140c08e70ull }, { 78, 0x140c08da0ull }, { 85, 0x1403f9410ull }, { 86, 0x140b725f0ull } };

// funciones del juego que se llaman (todas con argumentos simples; leidas en el desensamblado)
typedef void* (*PFN_Factory)(void* key, void* name, void* flag);
typedef uintptr_t (*PFN_Page)(void* page);
typedef void (*PFN_SetCount)(void* page, unsigned n);                       // 0x140cd3680
typedef unsigned char (*PFN_RowType)(void* page, unsigned type, unsigned row, char);   // 0x140cd39f0
typedef void (*PFN_RowTexts)(void* page);                                   // 0x140cd3740: textos por las ranuras 85 / 86
typedef void* (*PFN_Child)(void* node, unsigned i);                         // 0x140e8fa80
typedef void* (*PFN_Find)(void* sub, const char* name);                     // [nodo+0x98]->ranura 1
typedef void (*PFN_NodeOn)(void* node, char on, char b, unsigned mask);     // 0x140e4dfc0
typedef void (*PFN_SelClear)(void* sel);                                    // 0x140e54140
typedef void (*PFN_SelAdd)(void* sel, const char* text);                    // 0x140e53ee0
typedef void (*PFN_SelSet)(void* sel, unsigned i);                          // 0x140e54180
typedef void (*PFN_Headline)(void* node, const char* text);                 // 0x140e56ca0
typedef void* (*PFN_GetMv)();                                               // 0x141f05cd0
typedef void (*PFN_MvHold)(void* mv, char on, void*, void*);                // 0x141f06e10
typedef uintptr_t (*PFN_Dress)(void* mv, uint64_t key, uint32_t team, uint32_t a, uint32_t gk, uint32_t kit, uint8_t b);   // 0x141f06920
static const PFN_SetCount g_SetCount = (PFN_SetCount)0x140cd3680ull;
static const PFN_RowType g_RowType = (PFN_RowType)0x140cd39f0ull;
static const PFN_RowTexts g_RowTexts = (PFN_RowTexts)0x140cd3740ull;
static const PFN_Child g_Child = (PFN_Child)0x140e8fa80ull;
static const PFN_NodeOn g_NodeOn = (PFN_NodeOn)0x140e4dfc0ull;
static const PFN_SelClear g_SelClear = (PFN_SelClear)0x140e54140ull;
static const PFN_SelAdd g_SelAdd = (PFN_SelAdd)0x140e53ee0ull;
static const PFN_SelSet g_SelSet = (PFN_SelSet)0x140e54180ull;
static const PFN_Headline g_Headline = (PFN_Headline)0x140e56ca0ull;
static const PFN_GetMv g_GetMv = (PFN_GetMv)0x141f05cd0ull;
static const PFN_MvHold g_MvHold = (PFN_MvHold)0x141f06e10ull;
static const PFN_Dress g_Dress = (PFN_Dress)0x141f06920ull;
static const uintptr_t kCtx = 0x1438040a8ull, kInvalidTeam = 0x143528f44ull, kVtMv = 0x142ba7c68ull;

static PFN_Factory o_factory = nullptr;
static bool s_tried = false, s_ok = false;
static void* s_vt[1 + kSlots] = {};        // [0] = el dato de tipo que va antes de la tabla; la pagina apunta a s_vt + 1
static PFN_Page o_build = nullptr, o_ready = nullptr;
static uint64_t s_seenKey = 0; static uint32_t s_seenTeam = 0;    // ultimo jugador que se vio en el Editar con su equipo
static std::atomic<int> s_nPages{0};
// (22:48, Agustin: el icono de la ultima tecla "solo en el motor") la pagina del estudio del Motor que esta viva: la anota la fabrica y
// la confirma en cada cuadro el gancho de entrada de los menus (menumouse.cpp llama a MotorPageSeen con cada ventana activa)
static std::atomic<void*> s_motorWin{nullptr}; static std::atomic<double> s_motorT{-1.0e9};

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static inline bool Ptr(const void* p) { const uintptr_t a = (uintptr_t)p; return a >= 0x10000ull && a < 0x7ffffffff000ull && (a & 7) == 0; }
static inline uint32_t U32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint64_t U64(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }
static inline uint8_t* P8(const uint8_t* p) { uint8_t* v; memcpy(&v, p, 8); return v; }
static bool Writable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & 0xff) != PAGE_READWRITE) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}

// std::string del juego (MSVC): 16 bytes en linea o puntero, largo en +0x10, capacidad en +0x18
static bool GameStr(const void* s, char* out, size_t n) {
    out[0] = 0;
    if (!Ptr(s) || !Readable(s, 0x20)) return false;
    const uint8_t* b = (const uint8_t*)s; const uint64_t len = U64(b + 0x10), cap = U64(b + 0x18);
    if (len >= n || len > 512 || cap < len) return false;
    const char* t = cap >= 0x10 ? (const char*)P8(b) : (const char*)b;
    if (cap >= 0x10 && (!Ptr((const void*)((uintptr_t)t & ~7ull)) || !Readable(t, (size_t)len + 1))) return false;
    memcpy(out, t, (size_t)len); out[len] = 0; return true;
}

// ---------------------------------------------------------------- las filas del Motor
static const char* const kVista[] = { "Normal", "Luz difusa", "Luz especular", "Normales", "Normales de mundo", "Albedo (sin luz)", "Profundidad", "Oclusion", "Sombra del sol", "Solo luz (sin texturas)", "Material" };
static const char* const kNoSi[] = { "No", "Si" };
static const char* const kCorte[] = { "No", "25 %", "50 %", "75 %" };
static const float kCorteV[] = { 0.0f, 0.25f, 0.5f, 0.75f };
struct Fila { const char* nombre; const char* const* op; int n; };
static const Fila kFilas[] = { { "Vista", kVista, 11 }, { "Alambre", kNoSi, 2 }, { "Pantalla partida", kCorte, 4 } };
static const int kNFilas = (int)(sizeof kFilas / sizeof kFilas[0]);

static int FilaActual(int r) {
    float v[4];
    { std::lock_guard<std::mutex> lk(g_cfgMutex); for (int i = 0; i < 4; i++) v[i] = g_cfg.live[429][i]; }
    if (r == 0) return std::clamp((int)(v[0] + 0.5f), 0, 10);
    if (r == 1) return v[1] > 0.5f ? 1 : 0;
    int best = 0; for (int i = 1; i < 4; i++) if (std::fabs(kCorteV[i] - v[2]) < std::fabs(kCorteV[best] - v[2])) best = i;
    return best;
}

static void* Sub(void* node, const char* name) {      // hijo por nombre, como hace el juego: [nodo+0x98]->ranura 1 (nodo+0x98, nombre)
    if (!Ptr(node)) return nullptr;
    void* sub = (uint8_t*)node + 0x98; void** vt = *(void***)sub;
    if (!Ptr(vt) || !Ptr(vt[1])) return nullptr;
    return ((PFN_Find)vt[1])(sub, name);
}

// ranura 78: cantidad de filas
static unsigned my_Count(void*) { return (unsigned)kNFilas; }
// ranuras 85 / 86: el texto de cada fila es literal (sin numero de texto del juego)
static int my_RowTextId(void*, unsigned) { return -1; }
static const char* my_RowText(void*, unsigned row) { return row < (unsigned)kNFilas ? kFilas[row].nombre : ""; }
// ranura 52: "restablecer apariencia" (escribe datos del jugador): no en el Motor. Ranura 49: aceptar de la pagina (mandaba el
// evento de la fila al flujo). Sobre una fila con selector el juego ni la llama; queda anulada por si no hay fila elegida
static void my_Nop(void*) {}

// ranura 76: arma las filas. Primero corre la funcion de la pagina (deja el titulo con el escudo, los botones de ayuda y devuelve
// "listo" como siempre) y despues se ponen las filas del Motor.
// La funcion de la pagina (0x140c08e70) recorre su lista de filas (vector de u32 en +0x120..+0x128: 0 cara, 1 peinado, 2 fisico,
// 3 ...) y a cada una le pone texto por numero, evento "0_Face"... y apagado segun el jugador; al final fija la cantidad. Mientras
// corre se le deja una lista de kNFilas entradas con un valor que no conoce (>= 4): asi solo crea las filas vacias y fija la
// cantidad en kNFilas. La lista se restaura apenas vuelve. Si la lista no es la esperada no se toca y las filas se corrigen despues.
static uintptr_t my_Build(void* page) {
    uint8_t* pg = (uint8_t*)page;
    uint32_t* vb = (uint32_t*)P8(pg + 0x120); uint32_t* ve = (uint32_t*)P8(pg + 0x128);
    uint32_t saved[17] = {}; size_t n = 0; bool tamper = false;
    if (Ptr(vb) && ve > vb && (((uintptr_t)ve - (uintptr_t)vb) & 3) == 0 && (uintptr_t)ve - (uintptr_t)vb <= 17 * 4) {
        n = (size_t)(ve - vb);
        if (n >= (size_t)kNFilas && n <= 17 && Writable(vb, n * 4)) {
            bool sane = true; for (size_t i = 0; i < n; i++) if (vb[i] >= 16) sane = false;
            if (sane) {
                memcpy(saved, vb, n * 4);
                for (int i = 0; i < kNFilas; i++) vb[i] = 0xff;
                uint32_t* cut = vb + kNFilas; memcpy(pg + 0x128, &cut, 8);
                tamper = true;
            }
        }
    }
    const uintptr_t ret = o_build(page);
    if (tamper) { memcpy(vb, saved, n * 4); memcpy(pg + 0x128, &ve, 8); }
    uint8_t* I = P8(pg + 0x88);
    uint8_t* root = Ptr(I) ? P8(I + 0x98) : nullptr;
    void* list = Ptr(root) ? g_Child(root, 0) : nullptr;
    const int k = s_nPages.load(std::memory_order_relaxed);
    if (!Ptr(list)) { if (k <= 6) Log("motor (pagina): sin lista de filas en la pagina %p: quedan las filas de Apariencia", page); return ret; }
    if (!tamper) g_SetCount(page, (unsigned)kNFilas);
    int hechas = 0;
    for (int i = 0; i < kNFilas; i++) {
        g_RowType(page, 1, (unsigned)i, 0);                              // 1 = fila con selector (clase de control 0), como 0x140c079e0
        void* row = g_Child(list, (unsigned)i);
        if (!Ptr(row)) continue;
        g_NodeOn(row, 1, 1, 2);                                          // la pagina apaga algunas (cara / peinado de caras reales)
        void* sel = Sub(row, "selector");
        if (!Ptr(sel)) continue;
        g_SelClear(sel);
        for (int j = 0; j < kFilas[i].n; j++) g_SelAdd(sel, kFilas[i].op[j]);
        g_SelSet(sel, (unsigned)FilaActual(i));
        hechas++;
    }
    g_RowTexts(page);                                                    // nombres de las filas (ranuras 85 / 86)
    uint8_t* top = Ptr(I) ? P8(I + 0x90) : nullptr;
    void* head = Ptr(top) ? Sub(top, "headline") : nullptr;
    if (Ptr(head)) g_Headline(head, "Motor en tiempo real");
    if (k <= 6) Log("motor (pagina): filas del Motor puestas en la pagina %p (%d de %d con selector; lista de la pagina %s, %d entradas; titulo %s)",
                    page, hechas, kNFilas, tamper ? "apartada mientras armaba" : "sin tocar", (int)n, Ptr(head) ? "puesto" : "sin nodo");
    return ret;
}

// ranura 63: el juego cambio el valor de una fila (izquierda / derecha; 0x140e3c770 la llama con grupo, fila y opcion nueva)
static void my_Value(void*, int group, int row, int value) {
    if (group != 0 || row < 0 || row >= kNFilas) return;
    const int v = std::clamp(value, 0, kFilas[row].n - 1);
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        if (row == 0) g_cfg.live[429][0] = (float)v;
        else if (row == 1) g_cfg.live[429][1] = v ? 1.0f : 0.0f;
        else g_cfg.live[429][2] = kCorteV[v];
    }
    static int n = 0; if (n++ < 40) Log("motor (pagina): %s -> %s", kFilas[row].nombre, kFilas[row].op[v]);
}

// ranura 75: la pagina espera al visor 3D y le pide el modelo. Justo antes se viste al visor (equipo + camiseta), con las mismas
// guardas que usa el juego para su maniqui
static uintptr_t my_Ready(void* page) {
    if (g_cfg.motorDress) {
        uint8_t* ctx = *(uint8_t**)kCtx; uint8_t* mv = (uint8_t*)g_GetMv();
        if (Ptr(ctx) && Ptr(mv) && Readable(ctx + 0xe0, 0x10) && Readable(mv, 0x118) && *(uintptr_t*)mv == kVtMv && Ptr(P8(mv + 0x78)) &&
            U32(mv + 0x90) == 0 && U32(mv + 0x8c) - 4u < 2u) {
            const uint64_t key = U64(ctx + 0xe0); const uint32_t id = (uint32_t)(key >> 32), bad = *(uint32_t*)kInvalidTeam;
            if (id != 0 && (uint16_t)key != 0xffff) {
                uint32_t team = 0xfffcc000u;                              // "sin equipo" del propio juego
                if (key == s_seenKey && s_seenTeam != bad && s_seenTeam != 0xffffffffu) team = s_seenTeam;
                g_MvHold(mv, 0, nullptr, nullptr);
                g_Dress(mv, key, team, 0, 0, team & 0xffffc000u, 0);
                static int n = 0; if (n++ < 20) Log("motor (pagina): visor vestido antes de pedir el modelo (jugador %04X / %08X, equipo %08X%s)",
                                                    (unsigned)(uint16_t)key, (unsigned)id, (unsigned)team, team == 0xfffcc000u ? " = sin equipo" : "");
            }
        }
    }
    return o_ready(page);
}

// ---------------------------------------------------------------- el gancho
static void* hk_Factory(void* key, void* name, void* flag) {
    void* page = o_factory(key, name, flag);
    {   // de paso: el ultimo jugador con equipo que se vio en el Editar (para vestirlo con su camiseta)
        uint8_t* ctx = *(uint8_t**)kCtx;
        if (Ptr(ctx) && Readable(ctx + 0xe0, 0x10)) {
            const uint64_t k = U64(ctx + 0xe0); const uint32_t t = U32(ctx + 0xe8), bad = *(uint32_t*)kInvalidTeam;
            if ((uint32_t)(k >> 32) != 0 && (uint16_t)k != 0xffff && t != bad && t != 0xffffffffu) { s_seenKey = k; s_seenTeam = t; }
        }
    }
    if (!page || !Readable(page, 0x138) || *(uintptr_t*)page != kVtApp) return page;
    char nm[96];
    const bool okName = GameStr(name, nm, sizeof nm);
    const int k = s_nPages.fetch_add(1, std::memory_order_relaxed);
    const bool mine = okName && strncmp(nm, "BugCheck/BugCheckSim", 20) == 0;
    const bool swap = mine && g_cfg.motorPage != 0;
    if (k < 12) Log("motor (pagina): pagina de Apariencia %p creada para el nodo \"%s\" -> %s", page, okName ? nm : "?",
                    swap ? "filas del Motor" : mine ? "es la del Motor, pero motor_pagina = 0: quedan las filas de Apariencia" : "se deja como es");
    if (mine) { s_motorWin.store(page); s_motorT.store(Now()); }
    if (swap) *(void***)page = &s_vt[1];
    return page;
}

// la ventana que esta recibiendo entrada en este cuadro (la pasa menumouse.cpp): si es la del estudio del Motor, sigue viva
void MotorPageSeen(void* win) {
    if (!win || win != s_motorWin.load(std::memory_order_relaxed)) return;
    const uintptr_t vp = *(uintptr_t*)win;
    if (vp == (uintptr_t)&s_vt[1] || vp == kVtApp) s_motorT.store(Now(), std::memory_order_relaxed);
    else s_motorWin.store(nullptr, std::memory_order_relaxed);         // esa memoria ya es de otra ventana
}
// el estudio del Motor esta abierto (su pagina recibio entrada hace menos de 0.6 s)
bool MotorPageActive() { return Now() - s_motorT.load(std::memory_order_relaxed) < 0.6; }

void MotorPagePresent() {
    if (s_tried) return;
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("motor (pagina): parches en memoria apagados (motor = 0): el estudio queda con las filas de Apariencia"); return; }
    if (!g_cfg.motorPage) Log("motor (pagina): motor_pagina = 0: el estudio queda con las filas de Apariencia (el gancho va igual, solo para saber cuando esta abierto)");
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("motor (pagina): exe distinto: sin gancho"); return; }
    if (!Readable((const void*)kFactory, sizeof kFactorySig) || memcmp((const void*)kFactory, kFactorySig, sizeof kFactorySig) != 0 ||
        !Readable((const void*)0x141f06920ull, sizeof kDressSig) || memcmp((const void*)0x141f06920ull, kDressSig, sizeof kDressSig) != 0) {
        Log("motor (pagina): firma distinta en 0x140a977d0 o 0x141f06920: se omite"); return;
    }
    void** vt = (void**)kVtApp;
    if (!Readable(vt - 1, sizeof(void*) * (kSlots + 1))) { Log("motor (pagina): no se puede leer la tabla 0x1426edeb8: se omite"); return; }
    for (const auto& e : kExpect) if ((uintptr_t)vt[e.slot] != e.fn) { Log("motor (pagina): la ranura %d de la tabla no es la esperada (%p): se omite", e.slot, vt[e.slot]); return; }
    memcpy(s_vt, vt - 1, sizeof(void*) * (kSlots + 1));
    o_build = (PFN_Page)vt[76]; o_ready = (PFN_Page)vt[75];
    s_vt[1 + 49] = (void*)&my_Nop; s_vt[1 + 52] = (void*)&my_Nop; s_vt[1 + 63] = (void*)&my_Value; s_vt[1 + 75] = (void*)&my_Ready;
    s_vt[1 + 76] = (void*)&my_Build; s_vt[1 + 78] = (void*)&my_Count; s_vt[1 + 85] = (void*)&my_RowTextId; s_vt[1 + 86] = (void*)&my_RowText;
    if (MH_CreateHook((void*)kFactory, (void*)&hk_Factory, (void**)&o_factory) != MH_OK || MH_EnableHook((void*)kFactory) != MH_OK) {
        MH_RemoveHook((void*)kFactory); Log("motor (pagina): MinHook no pudo poner el gancho en 0x140a977d0: se omite"); return;
    }
    s_ok = true;
    Log("motor (pagina): lista (fabrica de paginas del Editar 0x140a977d0; el estudio del Motor lleva filas nativas Vista / Alambre / Pantalla partida; vestir = %d)", g_cfg.motorDress);
}

}   // namespace fx
