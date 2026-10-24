// PES21 FX - musculatura dentro del menu Fisico del juego (24/10): Editar > Jugadores > Fisico. Ingenieria inversa de
// PES2021.exe 1.07.02 (sello 62787C6A).
// menu::MenuEditPlBodySetting (vtable 0x142704dd8, COL 0x142cd50d8) arma 13 filas (altura, peso y 11 de contextura) en la
// lista generica "editSettingList" (17 filas como maximo: PlBase_SetRow 0x140cd39f0 rechaza la fila 17). Cada fila puede ser
// selector, barra ("c_slider"), salto ("c_jump"), etc. (estado -> animacion 0x1434ca010, estado -> tipo 0x14273a560), y la
// lista muestra N filas con la animacion "setNumChoicesAuto_N" (PlBase_SetNumRows 0x140cd3680; N = vtable[78]).
// El mod agrega la fila 13 "Musculatura" (salto). Al elegirla, la lista pasa a la pagina 1 (16 barras + salto a la 2); la
// pagina 2 tiene 10 barras; Atras vuelve a la pagina anterior y L1 / R1 pasan de una pagina a otra. Los valores (-25..+25 = x0.50..x1.50) van al perfil del
// jugador editado (EditWork + 0xe0 = su clave; musculatura_jugadores.ini) y el modelo cambia al instante.
//  vtable: [49] decidir 0x140cd35d0, [50] atras 0x140c2e3e0, [52] restablecer 0x140c2e420, [62] / [63] cambio de valor
//  0x140c2e120 / 0x140c2e140 (el menu base los llama desde 0x140e3c770 con fila y valor), [76] armar filas 0x140c2e1e0,
//  [78] cantidad de filas 0x14217f830 (devuelve 13), [81] titulo 0x140c2e1d0 (texto 0x23001b).
// Solo cambia entradas de la vtable si coinciden el sello del exe, el COL y las funciones originales.
#include "core.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <algorithm>

namespace fx {

static const unsigned kStamp = 0x62787C6A;
static const uintptr_t kBodyVt = 0x142704dd8ull, kBodyCol = 0x142cd50d8ull;
static const uintptr_t kEditWorkGlobal = 0x1438040a8ull;
// funciones del juego (con su firma para verificar antes de llamarlas)
static const uintptr_t kGetComp = 0x140e4f5d0ull, kSetRow = 0x140cd39f0ull, kSetNumRows = 0x140cd3680ull, kSetCursor = 0x140e42300ull,
    kSetEnable = 0x140e4dfc0ull, kSelClear = 0x140e54140ull, kSelAdd = 0x140e53ee0ull, kSelSet = 0x140e54180ull, kSelGet = 0x140e540f0ull,
    kSliderRange = 0x140e5bf80ull, kSliderGet = 0x140e5b9d0ull, kTitleSetStr = 0x140e56ca0ull, kTitleSetId = 0x140e56c10ull,
    kSetText = 0x140e4b030ull, kSetBtnMode = 0x140e433d0ull, kSetHintStr = 0x140e43500ull;
struct Sig { uintptr_t a; unsigned char b[10]; };
static const Sig kSigs[] = {
    { kGetComp,     { 0x48, 0x8b, 0x49, 0x78, 0x44, 0x8b, 0xd2, 0x45, 0x33, 0xc9 } },
    { kSetRow,      { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18 } },
    { kSetNumRows,  { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x30 } },
    { kSetCursor,   { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x6c, 0x24, 0x20 } },
    { kSetEnable,   { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10 } },
    { kSelClear,    { 0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24 } },
    { kSelAdd,      { 0x48, 0x85, 0xd2, 0x0f, 0x84, 0x45, 0x01, 0x00, 0x00, 0x4c } },
    { kSelSet,      { 0x40, 0x57, 0x48, 0x83, 0xec, 0x60, 0x48, 0xc7, 0x44, 0x24 } },
    { kSelGet,      { 0x8b, 0x81, 0xd8, 0x00, 0x00, 0x00, 0xc3, 0xcc, 0xcc, 0xcc } },
    { kSliderRange, { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20 } },
    { kSliderGet,   { 0x8b, 0x81, 0xd0, 0x00, 0x00, 0x00, 0xc3, 0xcc, 0xcc, 0xcc } },
    { kTitleSetStr, { 0x4c, 0x8b, 0xdc, 0x57, 0x48, 0x83, 0xec, 0x60, 0x49, 0xc7 } },
    { kTitleSetId,  { 0x40, 0x53, 0x48, 0x83, 0xec, 0x60, 0x48, 0xc7, 0x44, 0x24 } },
    { kSetBtnMode,  { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48 } },
    { kSetHintStr,  { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20 } },
};
// entradas de la vtable que se cambian
// botones del menu (0x140e3ec40; tabla boton -> codigo de pad 0x1427bcdf0: 0 decidir 0x24, 1 atras 0x23, 2 cuadrado 0x13,
// 3 triangulo 0x16, 6 0x19, 7 0x1a, 8 0x17, 9 0x18): en la prueba del 24/10 los botones 8 / 9 resultaron ser L2 / R2 (los del
// zoom de la camara) -> 6 = L1 -> vtable[53], 7 = R1 -> vtable[54] (en el Fisico no hacen nada: el juego no los habilita; se
// habilitan con 0x140e433d0 (menu, boton, 2) y se rotulan con vtable[23] 0x140e43500 (indice = boton))
enum { V_DECIDE = 49, V_BACK = 50, V_RESET = 52, V_L1 = 53, V_R1 = 54, V_VAL = 62, V_VALSET = 63, V_ROWS = 76, V_COUNT = 78 };
enum { B_L1 = 6, B_R1 = 7 };
static const struct { int i; uintptr_t orig; } kVt[] = {
    { V_DECIDE, 0x140cd35d0ull }, { V_BACK, 0x140c2e3e0ull }, { V_RESET, 0x140c2e420ull }, { V_VAL, 0x140c2e120ull },
    { V_VALSET, 0x140c2e140ull }, { V_ROWS, 0x140c2e1e0ull }, { V_COUNT, 0x14217f830ull },
    { V_L1, 0x141b42640ull }, { V_R1, 0x141b42640ull },
};

typedef void* (__fastcall* Fn4)(void*, void*, void*, void*);
typedef int (__fastcall* CountFn)(void*);
static Fn4 o_decide = nullptr, o_back = nullptr, o_reset = nullptr, o_val = nullptr, o_valset = nullptr, o_rows = nullptr, o_l1 = nullptr, o_r1 = nullptr;
static CountFn o_count = nullptr;

typedef void* (__fastcall* GetCompFn)(void* cont, int type, int idx);
typedef char (__fastcall* SetRowFn)(void* menu, unsigned state, unsigned row, char flag);
typedef void (__fastcall* SetNumRowsFn)(void* menu, int n);
typedef void (__fastcall* SetCursorFn)(void* menu, int list, long long row);
typedef void (__fastcall* SetEnableFn)(void* w, char a, char b, unsigned mask);
typedef void (__fastcall* SelClearFn)(void* sel);
typedef void (__fastcall* SelAddFn)(void* sel, const char* s);
typedef void (__fastcall* SelSetFn)(void* sel, unsigned i);
typedef unsigned (__fastcall* SelGetFn)(void* sel);
typedef int (__fastcall* SliderRangeFn)(void* sl, int mx, int mn, int v);
typedef int (__fastcall* SliderGetFn)(void* sl);
typedef void (__fastcall* TitleSetStrFn)(void* headline, const char* s);
typedef void (__fastcall* TitleSetIdFn)(void* headline, int id);
struct GameStr { union { char buf[16]; char* ptr; }; size_t size, cap; };   // std::string de MSVC (32 bytes)
typedef void (__fastcall* SetTextFn)(void* comp, const GameStr* s, int a, int b);
typedef void (__fastcall* SetBtnModeFn)(void* menu, int button, int mode);
typedef void (__fastcall* SetHintStrFn)(void* menu, int idx, const char* s);

static bool s_ok = false, s_tried = false;
static std::atomic<int> s_sliderMode{-1};   // -1 sin probar, 1 barras, 0 selectores (la fila no tiene barra)

// ---------------------------------------------------------------- filas de musculatura (pagina 1 y 2)
struct MRow { const char* label; int idx, comp; };
static const MRow kPage1[] = {   // torso y brazos (16 + salto a la pagina 2)
    { "General", 167, 0 }, { "Hombros", 164, 1 }, { "Ancho de hombros", 171, 3 }, { "B\xC3\xAD" "ceps", 164, 2 },
    { "Tr\xC3\xAD" "ceps", 164, 3 }, { "Antebrazos", 165, 0 }, { "Pecho", 165, 1 }, { "Pecho alto", 170, 1 },
    { "Espalda (toda)", 165, 2 }, { "Trapecio", 167, 1 }, { "Om\xC3\xB3" "platos", 167, 2 }, { "Dorsales", 167, 3 },
    { "Espalda baja", 168, 0 }, { "Cuello", 170, 0 }, { "Abdominales", 168, 1 }, { "Oblicuos", 168, 2 },
};
static const MRow kPage2[] = {   // cintura y piernas
    { "Cintura", 171, 1 }, { "Panza", 168, 3 }, { "Gl\xC3\xBA" "teos", 169, 0 }, { "Caderas", 171, 2 },
    { "Muslos (todo)", 165, 3 }, { "Cu\xC3\xA1" "driceps", 169, 1 }, { "Isquiotibiales", 169, 2 }, { "Aductores", 169, 3 },
    { "Pantorrillas", 166, 0 }, { "Rodillas", 171, 0 },
};
static const int kN1 = (int)(sizeof kPage1 / sizeof kPage1[0]), kN2 = (int)(sizeof kPage2 / sizeof kPage2[0]);
static const int kJump0 = 13, kJump1 = kN1, kJump2 = kN2;   // fila del salto en la pagina 0 (Fisico), en la 1 y en la 2
static const int kLastPage = 3;   // (24/10) pagina 3 = piel: tono (1..30, 0 = el del juego) y matiz (-10..+10)
static const int kSteps = 25; static const float kStep = 0.02f;   // -25..+25 -> x0.50..x1.50
static const MRow* RowOf(int page, int row) {
    if (page == 1 && row >= 0 && row < kN1) return &kPage1[row];
    if (page == 2 && row >= 0 && row < kN2) return &kPage2[row];
    return nullptr;
}
static int CountOf(int page) { return page == 1 ? kN1 + 1 : page == 2 ? kN2 + 1 : page == 3 ? 2 : kJump0 + 1; }

// estado de la pagina (el menu es uno solo a la vez; se reinicia cada vez que el juego arma las filas)
static void* s_menu = nullptr; static int s_page = 0;
static uint32_t s_editId = 0; static char s_editName[64] = {};

// ---------------------------------------------------------------- utilidades (lectura segura de la UI del juego)
static bool SafeCopy(void* dst, const void* src, size_t n) {
    __try { memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> static bool Rd(const void* p, T& out) { return p && SafeCopy(&out, p, sizeof out); }
static void* Child(void* w, unsigned i) {   // hijos de un nodo de UI: vector en + 0x170 / + 0x178 (Ui_GetChild 0x140e8fa80)
    char* b = nullptr; char* e = nullptr;
    if (!w || !Rd((char*)w + 0x170, b) || !Rd((char*)w + 0x178, e) || !b || e < b || i >= (unsigned)((e - b) / 8)) return nullptr;
    void* c = nullptr; Rd(b + (size_t)i * 8, c); return c;
}
static void* Ctl(void* menu) { void* c = nullptr; Rd((char*)menu + 0x88, c); return c; }
static void* List(void* menu) { void* ctl = Ctl(menu); void* root = nullptr; if (!ctl || !Rd((char*)ctl + 0x98, root)) return nullptr; return Child(root, 0); }
static int CursorRow(void* menu) { void* ctl = Ctl(menu); int r = -1; if (ctl) Rd((char*)ctl + 0xd8, r); return r; }
static void* FindComp(void* w, const char* name) {   // componente por nombre: contenedor (+ 0x98)->vtable[1](contenedor, nombre)
    if (!w) return nullptr;
    __try {
        void* cont = (char*)w + 0x98; void** vt = *(void***)cont;
        return ((void* (__fastcall*)(void*, const char*))vt[1])(cont, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
static void* CompOfType(void* w, int type) {
    if (!w) return nullptr;
    __try { return ((GetCompFn)kGetComp)((char*)w + 0x98, type, 0); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
static bool CallSetText(void* comp, const GameStr* s) {
    __try { ((SetTextFn)kSetText)(comp, s, 0, 0); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void SetLabel(void* w, const char* utf8) {
    void* c = FindComp(w, "setName");
    if (!c) return;
    static char big[128];
    GameStr s{}; size_t n = strlen(utf8);
    if (n < 16) { memcpy(s.buf, utf8, n + 1); s.cap = 15; }
    else { n = std::min(n, sizeof big - 1); memcpy(big, utf8, n); big[n] = 0; s.ptr = big; s.cap = n; }
    s.size = n;
    CallSetText(c, &s);
}
static bool G_SetRow(void* menu, unsigned state, unsigned row) {
    __try { return ((SetRowFn)kSetRow)(menu, state, row, 0) != 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void G_SetEnable(void* w) { if (!w) return; __try { ((SetEnableFn)kSetEnable)(w, 1, 1, 2); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
static void G_SetNumRows(void* menu, int n) { __try { ((SetNumRowsFn)kSetNumRows)(menu, n); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
static void G_SetCursor(void* menu, int row) { __try { ((SetCursorFn)kSetCursor)(menu, 0, row); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
static int G_SliderRange(void* sl, int v) { __try { return ((SliderRangeFn)kSliderRange)(sl, kSteps, -kSteps, v); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; } }
static int G_SliderGet(void* sl) { __try { return ((SliderGetFn)kSliderGet)(sl); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
static bool G_SelFill(void* sel, int idx) {
    __try {
        ((SelClearFn)kSelClear)(sel);
        char b[16];
        for (int k = -kSteps; k <= kSteps; k++) { snprintf(b, sizeof b, "%+d", k); ((SelAddFn)kSelAdd)(sel, b); }
        ((SelSetFn)kSelSet)(sel, (unsigned)idx);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static int G_SelGet(void* sel) { __try { return (int)((SelGetFn)kSelGet)(sel); } __except (EXCEPTION_EXECUTE_HANDLER) { return kSteps; } }
static void SetTitle(void* menu, const char* s, int id) {   // "headline" del objeto lista del menu (controlador + 0x90)
    void* ctl = Ctl(menu); void* lst = nullptr;
    if (!ctl || !Rd((char*)ctl + 0x90, lst) || !lst) return;
    void* h = FindComp(lst, "headline");
    if (!h) return;
    __try { if (s) ((TitleSetStrFn)kTitleSetStr)(h, s); else ((TitleSetIdFn)kTitleSetId)(h, id); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// L1 / R1 cambian de pagina; el rotulo de abajo dice a cual va cada uno. Modo 2 = "recien apretado" (Menu_v70 0x140e48f80
// lee el modo en controlador + 0x120 + boton * 4: 0 apagado, 1 / 2 / 3 = funciones 0x48 / 0x40 / 0x38 del pad); sin destino
// queda apagado (ni sonido ni llamada)
static void PageHints(void* menu, int page) {
    static const char* kL1[] = { "", "F\xC3\xADsico", "Torso y brazos", "Cintura y piernas" };
    static const char* kR1[] = { "Musculatura", "Cintura y piernas", "Piel", "" };
    if (page < 0 || page > kLastPage) return;
    __try {
        ((SetBtnModeFn)kSetBtnMode)(menu, B_L1, kL1[page][0] ? 2 : 0);
        ((SetBtnModeFn)kSetBtnMode)(menu, B_R1, kR1[page][0] ? 2 : 0);
        ((SetHintStrFn)kSetHintStr)(menu, B_L1, kL1[page]);
        ((SetHintStrFn)kSetHintStr)(menu, B_R1, kR1[page]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static int ToSteps(float f) { return std::clamp((int)lroundf((f - 1.0f) / kStep), -kSteps, kSteps); }
static float FromSteps(int n) { return 1.0f + kStep * (float)std::clamp(n, -kSteps, kSteps); }

// jugador que se esta editando: EditWork (global 0x1438040a8) + 0xe0 = clave (id en los 32 bits altos)
static void ReadEditPlayer() {
    s_editId = 0; s_editName[0] = 0;
    void* ew = nullptr; uint64_t key = 0;
    if (!Rd((void*)kEditWorkGlobal, ew) || !ew || !Rd((char*)ew + 0xe0, key)) return;
    s_editId = (uint32_t)(key >> 32) & 0x800bffffu;
    CharPlayerName(key, s_editName, sizeof s_editName);
}

// ---------------------------------------------------------------- paginas
static void AddJump(void* menu, int row, const char* label) {
    void* w = Child(List(menu), (unsigned)row);
    if (!w) return;
    G_SetRow(menu, 0, (unsigned)row);   // estado 0 = "c_jump" (tipo 255: decidir llega a vtable[49])
    G_SetEnable(w);
    SetLabel(w, label);
}
static void FillMusclePage(void* menu, int page) {
    void* list = List(menu);
    if (!list) return;
    const int n = page == 1 ? kN1 : kN2;
    for (int i = 0; i < n; i++) {
        const MRow& r = page == 1 ? kPage1[i] : kPage2[i];
        void* w = Child(list, (unsigned)i);
        if (!w) continue;
        const int v = ToSteps(MuscleProfileGet(s_editId, r.idx, r.comp));
        bool done = false;
        if (s_sliderMode.load() != 0) {   // barra ("c_slider"): componente de tipo 0x11 de la fila
            void* sl = CompOfType(w, 0x11);
            if (sl) {
                G_SetRow(menu, 2, (unsigned)i);
                G_SetEnable(w);
                SetLabel(w, r.label);
                done = G_SliderRange(sl, v) == 0;
            }
            if (s_sliderMode.load() < 0) {
                s_sliderMode = done ? 1 : 0;
                Log("fisico: las filas de musculatura usan %s", done ? "barras (c_slider)" : "selectores con numeros (la fila no tiene barra)");
            }
        }
        if (!done) {   // selector con numeros -25..+25
            G_SetRow(menu, 1, (unsigned)i);
            G_SetEnable(w);
            SetLabel(w, r.label);
            void* sel = FindComp(w, "selector");
            if (sel) G_SelFill(sel, v + kSteps);
        }
    }
    if (page == 1) AddJump(menu, kJump1, "Cintura y piernas  >");
    if (page == 2) AddJump(menu, kJump2, "Piel  >");
}
// ---------------------------------------------------------------- pagina 3: piel (24/10)
// fila 0 = tono (selector: "Del juego" + 1..30; los 6 colores del juego son 3, 8, 13, 18, 23 y 28), fila 1 = matiz -10..+10
static bool G_SelFillList(void* sel, const char* const* items, int n, int idx) {
    __try {
        ((SelClearFn)kSelClear)(sel);
        for (int i = 0; i < n; i++) ((SelAddFn)kSelAdd)(sel, items[i]);
        ((SelSetFn)kSelSet)(sel, (unsigned)std::clamp(idx, 0, n - 1));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void FillSkinPage(void* menu) {
    void* list = List(menu);
    if (!list) return;
    static char tones[kSkinTones + 1][24]; static const char* toneP[kSkinTones + 1];
    static char hues[21][20]; static const char* hueP[21];
    static bool init = false;
    if (!init) {
        snprintf(tones[0], sizeof tones[0], "Del juego"); toneP[0] = tones[0];
        for (int t = 1; t <= kSkinTones; t++) {
            if ((t - 3) % 5 == 0 && t >= 3 && t <= 28) snprintf(tones[t], sizeof tones[t], "%d (color %d)", t, (t - 3) / 5 + 1);
            else snprintf(tones[t], sizeof tones[t], "%d", t);
            toneP[t] = tones[t];
        }
        for (int m = -10; m <= 10; m++) {
            if (m == 0) snprintf(hues[m + 10], sizeof hues[0], "0 (neutro)");
            else snprintf(hues[m + 10], sizeof hues[0], m < 0 ? "%d oliva" : "+%d rosado", m);
            hueP[m + 10] = hues[m + 10];
        }
        init = true;
    }
    int tono = 0, matiz = 0; SkinToneGet(s_editId, &tono, &matiz);
    struct { const char* label; const char* const* items; int n, idx; } rows[2] = {
        { "Tono de piel (FX)", toneP, kSkinTones + 1, tono },
        { "Matiz: oliva / rosado", hueP, 21, matiz + 10 },
    };
    for (int i = 0; i < 2; i++) {
        void* w = Child(list, (unsigned)i);
        if (!w) continue;
        G_SetRow(menu, 1, (unsigned)i);   // selector
        G_SetEnable(w);
        SetLabel(w, rows[i].label);
        void* sel = FindComp(w, "selector");
        if (sel) G_SelFillList(sel, rows[i].items, rows[i].n, rows[i].idx);
    }
}
static void ApplySkin(int row, int idx) {
    if (!s_editId || row < 0 || row > 1) return;
    int tono = 0, matiz = 0; SkinToneGet(s_editId, &tono, &matiz);
    if (row == 0) tono = std::clamp(idx, 0, kSkinTones); else matiz = std::clamp(idx - 10, -10, 10);
    SkinToneSet(s_editId, s_editName, tono, matiz);
}
// color de piel del juego (1..6) del jugador del menu: copia de su apariencia en menu + 0xf8 (+ 0x2c, bits 8..10; campo 0x50)
int FisicoSkinIndex() {
    void* menu = s_menu; uint32_t v = 0;
    if (!s_ok || !menu || !Rd((char*)menu + 0xf8 + 0x2c, v)) return 0;
    int i = (int)((v >> 8) & 7);
    return i >= 1 && i <= 6 ? i : 0;
}
static void GoPage(void* menu, int page, int cursor) {
    s_page = page;
    if (page == 0) {
        o_rows(menu, nullptr, nullptr, nullptr);   // el juego vuelve a armar sus 13 filas (y sus valores)
        AddJump(menu, kJump0, "Musculatura  >");
        SetTitle(menu, nullptr, 0x23001b);
    } else if (page == 3) {
        FillSkinPage(menu);
        SetTitle(menu, "Piel: tono propio (FX)", 0);
    } else {
        FillMusclePage(menu, page);
        SetTitle(menu, page == 1 ? "Musculatura (1/3): torso y brazos" : "Musculatura (2/3): cintura y piernas", 0);
    }
    G_SetNumRows(menu, CountOf(page));
    G_SetCursor(menu, cursor);
    PageHints(menu, page);
    Log("fisico: pagina %d (%s, id %u)", page, s_editName[0] ? s_editName : "jugador", s_editId);
}
static std::atomic<bool> s_hlChanged{false};   // un valor cambio: el resaltado vuelve a empezar
static void ApplyValue(int row, int steps) {
    const MRow* r = RowOf(s_page, row);
    if (!r || !s_editId) return;
    float cur = MuscleProfileGet(s_editId, r->idx, r->comp);
    if (ToSteps(cur) == std::clamp(steps, -kSteps, kSteps)) return;   // sin cambio (p. ej. al pasar el cursor)
    MuscleProfileSet(s_editId, s_editName, r->idx, r->comp, FromSteps(steps));
    s_hlChanged = true;
}
static int ReadRowSteps(void* menu, int row) {
    void* w = Child(List(menu), (unsigned)row);
    if (!w) return 0;
    if (s_sliderMode.load() == 1) { void* sl = CompOfType(w, 0x11); if (sl) return G_SliderGet(sl); }
    void* sel = FindComp(w, "selector");
    return sel ? G_SelGet(sel) - kSteps : 0;
}

// ---------------------------------------------------------------- ganchos de la vtable de MenuEditPlBodySetting
static bool Mine(void* menu) { return menu && menu == s_menu; }
static void* __fastcall hk_rows(void* menu, void* a2, void* a3, void* a4) {
    void* r = o_rows(menu, a2, a3, a4);
    s_menu = menu; s_page = 0;
    ReadEditPlayer();
    AddJump(menu, kJump0, "Musculatura  >");
    PageHints(menu, 0);
    static int s_n = 0;
    if (s_n++ < 20) Log("fisico: menu armado (%s, id %u): fila 'Musculatura' agregada", s_editName[0] ? s_editName : "jugador", s_editId);
    return r;
}
static int __fastcall hk_count(void* menu) {
    if (!Mine(menu)) return CountOf(0);   // menu nuevo (todavia sin armar): pagina 0
    return CountOf(s_page);
}
static void* __fastcall hk_decide(void* menu, void* a2, void* a3, void* a4) {
    if (Mine(menu)) {
        int row = CursorRow(menu);
        if (s_page == 0 && row == kJump0) { GoPage(menu, 1, 0); return nullptr; }
        if (s_page == 1 && row == kJump1) { GoPage(menu, 2, 0); return nullptr; }
        if (s_page == 2 && row == kJump2) { GoPage(menu, 3, 0); return nullptr; }
        if (s_page > 0) return nullptr;   // decidir sobre una barra: nada
    }
    return o_decide(menu, a2, a3, a4);
}
static void* __fastcall hk_back(void* menu, void* a2, void* a3, void* a4) {
    if (Mine(menu) && s_page > 0) { int p = s_page; GoPage(menu, p - 1, p == 1 ? kJump0 : p == 2 ? kJump1 : kJump2); return nullptr; }
    return o_back(menu, a2, a3, a4);
}
static void* __fastcall hk_l1(void* menu, void* a2, void* a3, void* a4) {   // pagina anterior (arriba de todo)
    if (Mine(menu) && s_page > 0) { GoPage(menu, s_page - 1, 0); return nullptr; }
    return o_l1(menu, a2, a3, a4);
}
static void* __fastcall hk_r1(void* menu, void* a2, void* a3, void* a4) {   // pagina siguiente
    if (Mine(menu) && s_page < kLastPage) { GoPage(menu, s_page + 1, 0); return nullptr; }
    return o_r1(menu, a2, a3, a4);
}
static void* __fastcall hk_reset(void* menu, void* a2, void* a3, void* a4) {
    if (Mine(menu) && s_page == 3) {   // Restablecer en la pagina de piel: vuelve al color del juego
        if (s_editId) SkinToneSet(s_editId, s_editName, 0, 0);
        int cur = CursorRow(menu);
        FillSkinPage(menu);
        G_SetCursor(menu, cur < 0 ? 0 : cur);
        return nullptr;
    }
    if (Mine(menu) && s_page > 0) {   // Restablecer en una pagina de musculatura: esa pagina vuelve a +0 (como el juego)
        const int n = s_page == 1 ? kN1 : kN2;
        for (int i = 0; i < n; i++) { const MRow* r = RowOf(s_page, i); if (r && s_editId) MuscleProfileSet(s_editId, s_editName, r->idx, r->comp, 1.0f); }
        int cur = CursorRow(menu);
        FillMusclePage(menu, s_page);
        G_SetCursor(menu, cur < 0 ? 0 : cur);
        return nullptr;
    }
    return o_reset(menu, a2, a3, a4);
}
static void* __fastcall hk_val(void* menu, void* a2, void* a3, void* a4) {   // (menu, lista, fila): leer el valor de la fila
    const int list = (int)(intptr_t)a2, row = (int)(intptr_t)a3;
    if (Mine(menu) && s_page == 3) { if (list == 0) { void* sel = FindComp(Child(List(menu), (unsigned)row), "selector"); if (sel) ApplySkin(row, G_SelGet(sel)); } return nullptr; }
    if (Mine(menu) && s_page > 0) { if (list == 0) ApplyValue(row, ReadRowSteps(menu, row)); return nullptr; }
    if (Mine(menu) && row >= kJump0) return nullptr;
    return o_val(menu, a2, a3, a4);
}
static void* __fastcall hk_valset(void* menu, void* a2, void* a3, void* a4) {   // (menu, lista, fila, valor nuevo)
    const int list = (int)(intptr_t)a2, row = (int)(intptr_t)a3, v = (int)(intptr_t)a4;
    if (Mine(menu) && s_page == 3) { if (list == 0) ApplySkin(row, v); return nullptr; }
    if (Mine(menu) && s_page > 0) {
        if (list == 0) ApplyValue(row, s_sliderMode.load() == 1 ? v : v - kSteps);
        return nullptr;
    }
    if (Mine(menu) && row >= kJump0) return nullptr;
    return o_valset(menu, a2, a3, a4);
}

// ---------------------------------------------------------------- resaltado del musculo de la fila elegida (24/10)
// Como el brillo que late en los menus de la cara del juego: la piel del musculo que se esta editando se aclara y late
// despacio. El VS de la piel del cuerpo (1c02a92ab4ae3362, reemplazo propio) calcula por vertice cuanto cae adentro de unas
// capsulas en el espacio del modelo EN REPOSO (pose A de 45 grados; X = izquierda, Y = arriba, Z = adelante; con |x| cada
// una vale para los dos lados; medidas con los huesos de musculo del log y los modelos de TorsoBody) y el PS (aa7d1833)
// mezcla el color con blanco segun el pulso que manda la DLL.
//   x174 pulso (0 = apagado) | y174 capsulas (hasta 4) | z174 todo el cuerpo ("General"); 175 + 2k = (A, radio), 176 + 2k = B
//   x183 prendido | y183 intensidad | z183 segundos que dura despues de moverse o cambiar un valor (0 = siempre) | w183 latido (s)
struct HlCap { float ax, ay, az, bx, by, bz, r; };
struct HlSet { int n; HlCap c[2]; bool all; };
static HlSet HlFor(int idx, int comp) {
    static const HlCap kDelt = { 0.205f, 1.455f, 0.030f, 0.215f, 1.445f, 0.030f, 0.075f };
    static const HlCap kTrap = { 0.040f, 1.570f, -0.025f, 0.150f, 1.500f, -0.020f, 0.055f };
    HlSet h{}; auto one = [&](HlCap c) { h.c[h.n++] = c; };
    switch (idx * 4 + comp) {
    case 167 * 4 + 0: h.all = true; break;                                                          // general
    case 164 * 4 + 1: one(kDelt); break;                                                             // hombros
    case 171 * 4 + 3: one(kDelt); one(kTrap); break;                                                 // ancho de hombros
    case 164 * 4 + 2: one({ 0.235f, 1.425f, 0.065f, 0.365f, 1.295f, 0.070f, 0.048f }); break;        // biceps (frente del brazo)
    case 164 * 4 + 3: one({ 0.235f, 1.425f, -0.015f, 0.365f, 1.290f, -0.010f, 0.048f }); break;      // triceps (atras)
    case 165 * 4 + 0: one({ 0.425f, 1.235f, 0.040f, 0.575f, 1.095f, 0.060f, 0.050f }); break;        // antebrazos
    case 165 * 4 + 1: one({ 0.035f, 1.375f, 0.165f, 0.145f, 1.385f, 0.130f, 0.080f }); break;        // pecho
    case 170 * 4 + 1: one({ 0.030f, 1.470f, 0.115f, 0.130f, 1.470f, 0.100f, 0.050f }); break;        // pecho alto
    case 165 * 4 + 2: one({ 0.070f, 1.480f, -0.075f, 0.060f, 1.080f, -0.085f, 0.130f }); break;      // espalda (toda)
    case 167 * 4 + 1: one(kTrap); break;                                                             // trapecio
    case 167 * 4 + 2: one({ 0.080f, 1.420f, -0.100f, 0.100f, 1.370f, -0.100f, 0.075f }); break;      // omoplatos
    case 167 * 4 + 3: one({ 0.150f, 1.400f, -0.050f, 0.130f, 1.200f, -0.040f, 0.065f }); break;      // dorsales
    case 168 * 4 + 0: one({ 0.035f, 1.180f, -0.100f, 0.035f, 1.040f, -0.090f, 0.075f }); break;      // espalda baja
    case 170 * 4 + 0: one({ 0.000f, 1.530f, 0.030f, 0.000f, 1.650f, 0.020f, 0.060f }); break;        // cuello
    case 168 * 4 + 1: one({ 0.045f, 1.260f, 0.150f, 0.045f, 1.040f, 0.160f, 0.060f }); break;        // abdominales
    case 168 * 4 + 2: one({ 0.150f, 1.220f, 0.070f, 0.150f, 1.040f, 0.075f, 0.055f }); break;        // oblicuos
    case 171 * 4 + 1: one({ 0.160f, 1.200f, 0.030f, 0.155f, 1.060f, 0.030f, 0.065f }); break;        // cintura
    case 168 * 4 + 3: one({ 0.000f, 1.130f, 0.150f, 0.000f, 1.090f, 0.155f, 0.090f }); break;        // panza
    case 169 * 4 + 0: one({ 0.085f, 0.970f, -0.060f, 0.085f, 0.930f, -0.060f, 0.085f }); break;      // gluteos
    case 171 * 4 + 2: one({ 0.160f, 0.980f, 0.030f, 0.160f, 0.930f, 0.030f, 0.070f }); break;        // caderas
    case 165 * 4 + 3: one({ 0.100f, 0.900f, 0.060f, 0.140f, 0.600f, 0.090f, 0.090f }); break;        // muslos (todo)
    case 169 * 4 + 1: one({ 0.100f, 0.880f, 0.110f, 0.140f, 0.620f, 0.120f, 0.060f }); break;        // cuadriceps
    case 169 * 4 + 2: one({ 0.100f, 0.880f, 0.010f, 0.140f, 0.620f, 0.040f, 0.060f }); break;        // isquiotibiales
    case 169 * 4 + 3: one({ 0.050f, 0.860f, 0.060f, 0.100f, 0.660f, 0.080f, 0.050f }); break;        // aductores
    case 166 * 4 + 0: one({ 0.155f, 0.500f, 0.025f, 0.165f, 0.280f, 0.025f, 0.060f }); break;        // pantorrillas
    case 171 * 4 + 0: one({ 0.142f, 0.550f, 0.090f, 0.142f, 0.530f, 0.090f, 0.065f }); break;        // rodillas
    }
    return h;
}
static int s_hlRow = -1, s_hlPage = -1; static double s_hlT0 = 0; static bool s_hlOn = false;
static void HlClear() { for (int i = 174; i <= 182; i++) for (int c = 0; c < 4; c++) g_cfg.live[i][c] = 0.0f; }
void FisicoHighlightPresent() {
    void* menu = s_menu;
    const bool want = s_ok && g_cfg.live[183][0] > 0.5f && menu && s_page > 0 && MusclePhysiqueActive();
    const MRow* r = want ? RowOf(s_page, CursorRow(menu)) : nullptr;
    if (!r) { if (s_hlOn) { HlClear(); s_hlOn = false; s_hlRow = -1; } return; }
    const double t = Now();
    const int row = CursorRow(menu);
    if (row != s_hlRow || s_page != s_hlPage || s_hlChanged.exchange(false)) { s_hlRow = row; s_hlPage = s_page; s_hlT0 = t; }
    const float dur = g_cfg.live[183][2], age = (float)(t - s_hlT0);
    float fade = 1.0f;
    if (dur > 0.05f) fade = age <= dur ? 1.0f : std::max(0.0f, 1.0f - (age - dur) / 0.6f);
    const float per = std::clamp(g_cfg.live[183][3] > 0.05f ? g_cfg.live[183][3] : 1.6f, 0.4f, 4.0f);
    const float pulse = 0.5f + 0.5f * sinf(6.2831853f * (float)fmod(t - s_hlT0, 1000.0) / per - 1.5707963f);   // arranca abajo y sube
    const float k = std::clamp(g_cfg.live[183][1], 0.0f, 1.5f) * (0.35f + 0.65f * pulse) * fade;
    const HlSet h = HlFor(r->idx, r->comp);
    HlClear();
    g_cfg.live[174][0] = k; g_cfg.live[174][1] = (float)h.n; g_cfg.live[174][2] = h.all ? 1.0f : 0.0f;
    for (int i = 0; i < h.n; i++) {
        const HlCap& c = h.c[i];
        float* A = g_cfg.live[175 + 2 * i]; float* B = g_cfg.live[176 + 2 * i];
        A[0] = c.ax; A[1] = c.ay; A[2] = c.az; A[3] = c.r; B[0] = c.bx; B[1] = c.by; B[2] = c.bz; B[3] = 0.0f;
    }
    s_hlOn = true;
}

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}

void FisicoInstall() {
    if (s_tried) return;
    s_tried = true;
    if (!g_cfg.motorPatch) return;
    if (!DiagOn(1)) { Log("fisico: ganchos del menu Fisico apagados (y173 = 0, Diagnostico > Ganchos nuevos)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("fisico: exe distinto, sin musculatura en el menu Fisico"); return; }
    for (auto& s : kSigs) if (!Readable((void*)s.a, sizeof s.b) || memcmp((void*)s.a, s.b, sizeof s.b) != 0) { Log("fisico: la funcion 0x%llx no coincide, sin musculatura en el menu Fisico", (unsigned long long)s.a); return; }
    // 0x140e4b030 (poner el texto de un componente) suele estar desviada por otro mod (Sider la usa para sus textos): se llama
    // igual, pasa por el desvio y termina en la original
    if (!Readable((void*)kSetText, 6)) { Log("fisico: la funcion de texto de la UI no se puede leer"); return; }
    void** vt = (void**)kBodyVt;
    if (!Readable(vt - 1, 90 * sizeof(void*)) || (uintptr_t)vt[-1] != kBodyCol) { Log("fisico: la vtable de MenuEditPlBodySetting no coincide"); return; }
    for (auto& e : kVt) if ((uintptr_t)vt[e.i] != e.orig) { Log("fisico: vtable[%d] = %p (se esperaba 0x%llx): sin cambios", e.i, vt[e.i], (unsigned long long)e.orig); return; }
    void* hooks[] = { (void*)&hk_decide, (void*)&hk_back, (void*)&hk_reset, (void*)&hk_val, (void*)&hk_valset, (void*)&hk_rows, (void*)&hk_count, (void*)&hk_l1, (void*)&hk_r1 };
    void** origs[] = { (void**)&o_decide, (void**)&o_back, (void**)&o_reset, (void**)&o_val, (void**)&o_valset, (void**)&o_rows, (void**)&o_count, (void**)&o_l1, (void**)&o_r1 };
    static_assert(sizeof hooks / sizeof hooks[0] == sizeof kVt / sizeof kVt[0], "un gancho por entrada de la vtable");
    DWORD old = 0;
    if (!VirtualProtect(&vt[V_DECIDE], (V_COUNT - V_DECIDE + 1) * sizeof(void*), PAGE_READWRITE, &old)) { Log("fisico: VirtualProtect fallo (%lu)", GetLastError()); return; }
    for (int k = 0; k < (int)(sizeof hooks / sizeof hooks[0]); k++) { *origs[k] = vt[kVt[k].i]; InterlockedExchangePointer(&vt[kVt[k].i], hooks[k]); }
    VirtualProtect(&vt[V_DECIDE], (V_COUNT - V_DECIDE + 1) * sizeof(void*), old, &old);
    s_ok = true;
    Log("fisico: menu Fisico del juego enganchado: fila 'Musculatura' (2 paginas: %d + %d valores, -%d..+%d; L1 / R1 cambian de pagina)", kN1, kN2, kSteps, kSteps);
}
bool FisicoEditing(uint32_t* id) { if (id) *id = s_editId; return s_ok && s_menu && s_editId; }

} // namespace fx
