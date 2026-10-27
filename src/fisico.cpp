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
    kSetText = 0x140e4b030ull, kSetBtnMode = 0x140e433d0ull, kSetHintStr = 0x140e43500ull, kUiSetColor = 0x140e81350ull;
// (24/10 d) paleta de colores del juego (menusystem::ColorPaletteWindow, la de pelo / ojos / accesorios): la abre el menu
// base al decidir sobre una fila de color ("c_color", estado 5; tabla de tipos 0x143707d60: tipo 6 -> 0x140e3caf0 abre la
// ventana con el componente de color (tipo 8, color RGBA en + 0xb4) como dueno; la ventana le escribe el color elegido cada
// cuadro (vtable[31]) y al cerrarse el menu base llama a vtable[62] (fila, lista)). Sus paletas: [0x2a] / [0x2b] = puntero y
// cantidad de "p_all" (72 colores genericos; en el modo 0 se ven p_current, p_all y p_creat = colores creados a mano)
static const uintptr_t kPaletteVt = 0x1427d0350ull;
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
    { kUiSetColor,  { 0x48, 0x83, 0xec, 0x38, 0x0f, 0xb6, 0x42, 0x03, 0x44, 0x0f } },
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
typedef void (__fastcall* UiSetColorFn)(void* comp, const unsigned char rgba[4]);

static bool s_ok = false, s_tried = false;
static std::atomic<int> s_sliderMode{-1};   // -1 sin probar, 1 barras, 0 selectores (la fila no tiene barra)

// ---------------------------------------------------------------- filas de musculatura (26/10 c: diseno por secciones)
// Cada pagina tiene subtitulos ("c_subHeader", estado 10 de la lista generica: el mismo rotulo de seccion que usa el juego en
// otros menus) y filas de estos tipos:
//   K_MUS     musculo del jugador: barra -50..+50 = x0.50..x1.50 de a 0.01 (1 = como el juego)
//   K_DEF     definicion muscular del jugador: barra 0..100 = 0..2 de a 0.02
//   K_HEAD    subtitulo de seccion (no hace nada)
//   K_PRESET  "Complexion" (selector: pone todos los valores del jugador de un tipo; hasta el 26/10 d se llamaba "Tipo de cuerpo")
//   K_GLOBAL  opcion de todos los jugadores (PES21FX.ini): barra 0..100 = 0..2 de a 0.02
//   K_SWITCH  interruptor de todos (selector No / Si)
//   K_COMPARE "Ver": con la musculatura o como el juego (mientras se edita, para comparar)
//   K_VARIANT (26/10) selector de forma con pasos con nombre (kLSel): los de los hombros (fila 287 del jugador: inclinacion
//             caidos .. rectos, tipo y tamano; musculo.cpp ComputeFactors)
// (26/10 e) "Tipo de cuerpo" (z262 del jugador: 0.5 robusto .. 1 TorsoBody .. 1.5 atletico; variantes de TorsoBody,
//             pes_variants.hlsli) es una barra K_MUS de a 0.01 (2 % de la variante por paso; antes un selector de a 20 %, a los saltos)
//             y el valor que va a los shaders sigue al elegido en ~0.15 s (musculo.cpp MuscleVariantT): el cuerpo cambia suave
enum { K_MUS = 0, K_DEF, K_HEAD, K_PRESET, K_GLOBAL, K_SWITCH, K_COMPARE, K_VARIANT };
struct MRow { const char* label; int idx, comp, kind; };
#define MU(l, i, c) { l, i, c, K_MUS }
#define HD(l) { l, 0, 0, K_HEAD }
static const MRow kPgBody[] = {   // todo el cuerpo, hombros y cuello
    HD("Todo el cuerpo"), { "Complexi\xC3\xB3n", 0, 0, K_PRESET }, MU("Tipo de cuerpo", 262, 2), MU("General", 167, 0),
    { "Definici\xC3\xB3n muscular", 172, 0, K_DEF }, { "Ver", 0, 0, K_COMPARE },
    HD("Hombros y trapecio"), { "Inclinaci\xC3\xB3n de hombros", 287, 0, K_VARIANT }, { "Tipo de hombros", 287, 1, K_VARIANT },
    { "Tama\xC3\xB1o de hombros", 287, 2, K_VARIANT }, MU("Hombros", 164, 1), MU("Ancho de hombros", 171, 3), MU("Deltoides frontal", 260, 0), MU("Deltoides posterior", 260, 1),
    MU("Trapecio", 167, 1), MU("Trapecio alto", 261, 2),
};
static const MRow kPgArms[] = {   // brazos
    HD("Brazo"), MU("B\xC3\xAD" "ceps", 164, 2), MU("Tr\xC3\xAD" "ceps", 164, 3), MU("Axilas", 162, 0),
    HD("Antebrazo"), MU("Antebrazos", 165, 0), MU("Antebrazo alto", 260, 2), MU("Antebrazo bajo", 260, 3),
    HD("Articulaciones"), MU("Codos", 166, 3), MU("Mu\xC3\xB1" "ecas", 170, 3),
};
static const MRow kPgTorso[] = {   // pecho, espalda y (26/10) cuello (la pagina 1 lleva ahora el selector de la variante)
    HD("Pecho"), MU("Pecho", 165, 1), MU("Pecho alto", 170, 1), MU("Pecho bajo", 261, 0), MU("Costados (serratos)", 162, 1),
    HD("Espalda"), MU("Espalda (toda)", 165, 2), MU("Om\xC3\xB3" "platos", 167, 2), MU("Dorsales", 167, 3), MU("Dorsales: ancho (V)", 261, 1),
    MU("Espalda baja", 168, 0), MU("Lumbares", 162, 2),
    HD("Cuello"), MU("Cuello", 170, 0), MU("Nuca", 166, 2), MU("Base del cuello", 162, 3),
};
static const MRow kPgCore[] = {   // abdomen y cadera
    HD("Abdomen"), MU("Abdominales", 168, 1), MU("Oblicuos", 168, 2), MU("Cintura", 171, 1), MU("Panza", 168, 3),
    HD("Gl\xC3\xBA" "teos y cadera"), MU("Gl\xC3\xBA" "teos", 169, 0), MU("Gl\xC3\xBA" "teo levantado", 261, 3), MU("Caderas", 171, 2),
};
static const MRow kPgLegs[] = {   // piernas
    HD("Muslo"), MU("Muslos (todo)", 165, 3), MU("Cu\xC3\xA1" "driceps", 169, 1), MU("Muslo exterior", 262, 0), MU("Isquiotibiales", 169, 2),
    MU("Aductores", 169, 3),
    HD("Rodilla y pantorrilla"), MU("Rodillas", 171, 0), MU("Pantorrillas", 166, 0), MU("Gemelos", 262, 1),
};
static const MRow kPgAlive[] = {   // musculos vivos y opciones (para todos los jugadores)
    HD("Para todos los jugadores"), { "Musculatura", 164, 0, K_SWITCH },
    { "Contracci\xC3\xB3n con el movimiento", 263, 0, K_GLOBAL }, { "Respiraci\xC3\xB3n", 263, 1, K_GLOBAL }, { "Se agita al correr", 263, 2, K_GLOBAL },
    HD("Men\xC3\xBA y ropa"), { "Resaltar el m\xC3\xBAsculo elegido", 183, 0, K_SWITCH }, { "La ropa acompa\xC3\xB1" "a", 166, 1, K_SWITCH },
};
#undef MU
#undef HD
struct MPage { const char* title; const MRow* rows; int n; const char* next; };
#define PG(t, r, nx) { t, r, (int)(sizeof r / sizeof r[0]), nx }
static const MPage kMP[] = {
    PG("Musculatura: cuerpo y hombros", kPgBody, "Brazos  >"),
    PG("Musculatura: brazos", kPgArms, "Pecho, espalda y cuello  >"),
    PG("Musculatura: pecho, espalda y cuello", kPgTorso, "Abdomen y cadera  >"),
    PG("Musculatura: abdomen y cadera", kPgCore, "Piernas  >"),
    PG("Musculatura: piernas", kPgLegs, "M\xC3\xBAsculos vivos  >"),
    PG("M\xC3\xBAsculos vivos (para todos)", kPgAlive, "Piel  >"),
};
#undef PG
static const int kNMP = (int)(sizeof kMP / sizeof kMP[0]);   // paginas de musculatura: 1..kNMP
static_assert(sizeof kPgBody / sizeof kPgBody[0] <= 16 && sizeof kPgTorso / sizeof kPgTorso[0] <= 16, "17 filas como maximo (16 + salto)");
// (26/10) selectores de forma: pasos con nombre y el valor del jugador de cada uno (se elige el paso mas cercano al valor)
static const char* const kSlopeItems[] = { "Muy ca\xC3\xAD" "dos", "Ca\xC3\xAD" "dos", "Un poco ca\xC3\xAD" "dos", "Normales", "Un poco rectos", "Rectos", "Muy rectos" };
static const char* const kSSizeItems[] = { "Muy chicos", "Chicos", "Un poco chicos", "Normales", "Un poco grandes", "Grandes", "Muy grandes" };
static const float kSevenVals[] = { 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f };
static const char* const kSTypeItems[] = { "Estrechos", "Normales", "Anchos", "Redondos" };
static const float kSTypeVals[] = { 2.0f, 1.0f, 3.0f, 4.0f };   // 1 = normales (lo que falta en los archivos vale 1)
struct LSel { int idx, comp; const char* const* items; const float* vals; int n; };
#define LS(i, c, it, va) { i, c, it, va, (int)(sizeof it / sizeof it[0]) }
static const LSel kLSel[] = {
    LS(287, 0, kSlopeItems, kSevenVals),   // hombros: inclinacion
    LS(287, 1, kSTypeItems, kSTypeVals),   //          tipo
    LS(287, 2, kSSizeItems, kSevenVals),   //          tamano
};
#undef LS
static_assert(sizeof kSlopeItems / sizeof kSlopeItems[0] == sizeof kSevenVals / sizeof kSevenVals[0] &&
              sizeof kSSizeItems / sizeof kSSizeItems[0] == sizeof kSevenVals / sizeof kSevenVals[0] &&
              sizeof kSTypeItems / sizeof kSTypeItems[0] == sizeof kSTypeVals / sizeof kSTypeVals[0], "pasos y valores de los selectores");
static const LSel* LSelOf(int idx, int comp) { for (const LSel& l : kLSel) if (l.idx == idx && l.comp == comp) return &l; return nullptr; }
static int LSelIndex(const LSel& l, float v) {
    if (v < 0.05f) v = 1.0f;   // sin valor = 1
    int best = 0; float bd = 1e9f;
    for (int i = 0; i < l.n; i++) { const float d = fabsf(l.vals[i] - v); if (d < bd) { bd = d; best = i; } }
    return best;
}
static const int kJump0 = 13;   // fila del salto en la pagina 0 (Fisico)
static const int kSkinPage = kNMP + 1;   // la piel va despues de las paginas de musculatura
static const int kLastPage = kSkinPage;
static const int kSteps = 25;                                 // piel: -25..+25 (los perfiles de piel guardan pasos)
static const int kMSteps = 50; static const float kMStep = 0.01f;   // (26/10) musculatura: -50..+50 -> x0.50..x1.50 de a 0.01
static const int kGSteps = 100; static const float kGStep = 0.02f;  // (26/10 c) definicion y opciones de todos: 0..100 -> 0..2 de a 0.02
static bool IsMusPage(int page) { return page >= 1 && page <= kNMP; }
static const MPage* PageOf(int page) { return IsMusPage(page) ? &kMP[page - 1] : nullptr; }
static const MRow* RowOf(int page, int row) {
    const MPage* p = PageOf(page);
    return p && row >= 0 && row < p->n ? &p->rows[row] : nullptr;
}
static int JumpRow(int page) { const MPage* p = PageOf(page); return page == 0 ? kJump0 : p ? p->n : -1; }
static int FirstRow(int page) {   // la primera fila que no es un subtitulo
    const MPage* p = PageOf(page);
    if (p) for (int i = 0; i < p->n; i++) if (p->rows[i].kind != K_HEAD) return i;
    return 0;
}
static const int kSkinRows = 7;   // (24/10 e) pagina de piel: color, tono conocido, luz, saturacion, matiz, cuerpo como la cara; (24/10 g) a que se aplica
static int CountOf(int page) { return IsMusPage(page) ? kMP[page - 1].n + 1 : page == kSkinPage ? kSkinRows : kJump0 + 1; }
static bool IsBar(int kind) { return kind == K_MUS || kind == K_DEF || kind == K_GLOBAL; }
static int BarMin(int kind) { return kind == K_MUS ? -kMSteps : 0; }
static int BarMax(int kind) { return kind == K_MUS ? kMSteps : kGSteps; }

// estado de la pagina (el menu es uno solo a la vez; se reinicia cada vez que el juego arma las filas)
static void* s_menu = nullptr; static int s_page = 0;
static std::atomic<int> s_hlReq{-1}; static std::atomic<long long> s_hlReqT{0};   // (26/10 c) resaltado pedido por la ventana del mod
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
static int G_SliderRange(void* sl, int v, int steps = kSteps) { __try { return ((SliderRangeFn)kSliderRange)(sl, steps, -steps, v); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; } }
static int G_SliderRange2(void* sl, int v, int mn, int mx) { __try { return ((SliderRangeFn)kSliderRange)(sl, mx, mn, v); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; } }
static int G_SliderGet(void* sl) { __try { return ((SliderGetFn)kSliderGet)(sl); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
static bool G_SelFill(void* sel, int idx, int steps = kSteps) {
    __try {
        ((SelClearFn)kSelClear)(sel);
        char b[16];
        for (int k = -steps; k <= steps; k++) { snprintf(b, sizeof b, "%+d", k); ((SelAddFn)kSelAdd)(sel, b); }
        ((SelSetFn)kSelSet)(sel, (unsigned)idx);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// (26/10 c) selector con un rango de pasos y su rotulo: musculo "+12", definicion / opciones "x1.24"
static bool G_SelFillKind(void* sel, int kind, int units) {
    __try {
        ((SelClearFn)kSelClear)(sel);
        char b[16];
        for (int k = BarMin(kind); k <= BarMax(kind); k++) {
            if (kind == K_MUS) snprintf(b, sizeof b, "%+d", k); else snprintf(b, sizeof b, "x%.2f", kGStep * (float)k);
            ((SelAddFn)kSelAdd)(sel, b);
        }
        ((SelSetFn)kSelSet)(sel, (unsigned)(units - BarMin(kind)));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void G_SelSetIdx(void* sel, unsigned i) { __try { ((SelSetFn)kSelSet)(sel, i); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
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
// (24/10 b) la barra de abajo no muestra rotulos sueltos de L1 / R1 (Bar_SetItem 0x140e37540: los indices 6..9 no tienen
// lugar); los dobles van en 12..16 (12 -> lugar 6, 13 -> 7, 14 -> 11 (stick R), 15 -> 9, 16 -> 8): el 12 es el de L1 R1,
// antes del L2 R2 del zoom
// (24/10 c) rotulo doble de la barra (indice -> lugar: 12 -> 6, 13 -> 7, 14 -> 11, 15 -> 9, 16 -> 8; los lugares 6..11 son los
// botones L1, R1, L2, R2, L3, R3): 12 = flechas izq / der y 14 = palanca derecha (los vio el usuario; el visor usa el 14 para
// "girar"), 15 = L2 R2 (acercar / alejar, texto 0x3e001f) -> el par cae en el lugar del boton derecho: 13 (lugar 7 = R1) = L1 R1
// (24/10 d) 13 = cruceta (lo vio el usuario); quedan 16 (lugar 8, entre la cruceta y L2 R2) y 10 / 11 (lugares 10 y 12)
enum { H_L1R1 = 16, H_ARROWS = 12, H_STICK = 14, H_DPAD = 13 };
static void PageHints(void* menu, int page) {
    if (page < 0 || page > kLastPage) return;
    __try {
        ((SetBtnModeFn)kSetBtnMode)(menu, B_L1, page > 0 ? 2 : 0);
        ((SetBtnModeFn)kSetBtnMode)(menu, B_R1, page < kLastPage ? 2 : 0);
        ((SetHintStrFn)kSetHintStr)(menu, B_L1, "");
        ((SetHintStrFn)kSetHintStr)(menu, B_R1, "");
        ((SetHintStrFn)kSetHintStr)(menu, H_ARROWS, "");   // versiones anteriores lo ponian en 12 (flechas) y 14 (palanca)
        ((SetHintStrFn)kSetHintStr)(menu, H_STICK, "");
        ((SetHintStrFn)kSetHintStr)(menu, H_DPAD, "");
        ((SetHintStrFn)kSetHintStr)(menu, H_L1R1, "Cambiar p\xC3\xA1gina");
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// titulo con contador de paginas
static const char* PageTitle(int page) {
    static char b[96];
    const int tot = kLastPage + 1;
    if (page == 0) snprintf(b, sizeof b, "F\xC3\xADsico  (1/%d)", tot);
    else if (IsMusPage(page)) snprintf(b, sizeof b, "%s  (%d/%d)", kMP[page - 1].title, page + 1, tot);
    else snprintf(b, sizeof b, "Piel: tono propio  (%d/%d)", tot, tot);
    return b;
}


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
static bool s_filling = false;   // (24/10 f) rellenando filas: el juego avisa "cambio de valor" al rellenar un selector y hay que ignorarlo
static bool G_SelFillList(void* sel, const char* const* items, int n, int idx);
// (26/10) fila "Complexion" (tipos de cuerpo del mod): "A mano" + los tipos (el que coincide con los valores del jugador queda elegido)
static void FillPresetRow(void* menu, void* list, int row) {
    void* w = Child(list, (unsigned)row);
    if (!w) return;
    static const char* items[16]; const int np = std::min(MusclePresetCount(), 15);
    items[0] = "A mano"; for (int k = 0; k < np; k++) items[k + 1] = MusclePresetName(k);
    G_SetRow(menu, 1, (unsigned)row); G_SetEnable(w); SetLabel(w, "Complexi\xC3\xB3n");
    void* sel = FindComp(w, "selector");
    if (sel) G_SelFillList(sel, items, np + 1, MusclePresetMatch(s_editId) + 1);
}
// valor de una fila: musculo / definicion del jugador (o el general), opcion de todos
static float RowValue(const MRow& r) {
    switch (r.kind) {
    case K_MUS: case K_DEF: case K_VARIANT: return MuscleProfileGet(s_editId, r.idx, r.comp);
    case K_GLOBAL: case K_SWITCH: return g_cfg.live[r.idx][r.comp];
    default: return 0.0f;
    }
}
static int ToUnits(const MRow& r, float f) {
    if (r.kind == K_MUS) { if (f < 0.05f) f = 1.0f; return std::clamp((int)lroundf((f - 1.0f) / kMStep), -kMSteps, kMSteps); }   // 0 = sin valor = 1
    return std::clamp((int)lroundf(f / kGStep), 0, kGSteps);
}
static float FromUnits(const MRow& r, int u) {
    if (r.kind == K_MUS) return 1.0f + kMStep * (float)std::clamp(u, -kMSteps, kMSteps);
    return kGStep * (float)std::clamp(u, 0, kGSteps);
}
// (26/10 b) rotulo de una fila con su valor si no es el del juego ("Biceps  x1.12"); (26/10 c) las opciones en 0: "(apagado)"
static const char* RowLabel(const MRow& r, float v) {
    static char b[96];
    if (r.kind == K_GLOBAL && v < 0.005f) { snprintf(b, sizeof b, "%s  (apagado)", r.label); return b; }
    if (r.idx == 262 && r.comp == 2) {   // (26/10) tipo de cuerpo: 0.5 robusto .. 1 TorsoBody .. 1.5 atletico (0 = sin valor = normal)
        if (v < 0.05f || fabsf(v - 1.0f) < 0.005f) snprintf(b, sizeof b, "%s  (normal)", r.label);
        else snprintf(b, sizeof b, "%s  %s %d %%", r.label, v < 1.0f ? "robusto" : "atl\xC3\xA9tico", (int)lroundf(fabsf(v - 1.0f) * 200.0f));
        return b;
    }
    if (!IsBar(r.kind) || fabsf(v - 1.0f) < 0.005f) return r.label;
    snprintf(b, sizeof b, "%s  x%.2f", r.label, v);
    return b;
}
static const char* const kSiNoRow[2] = { "No", "S\xC3\xAD" };
static const char* const kCompareItems[2] = { "Con la musculatura", "Como el juego" };
static void FillRow(void* menu, void* list, int i, const MRow& r) {
    void* w = Child(list, (unsigned)i);
    if (!w) return;
    switch (r.kind) {
    case K_HEAD: {   // subtitulo de seccion: "c_subHeader" (si la lista no lo acepta, una fila de texto)
        static int logged = 0;
        int st = 10; bool ok = G_SetRow(menu, 10, (unsigned)i);
        if (!ok) { st = 6; ok = G_SetRow(menu, 6, (unsigned)i); }
        G_SetEnable(w);
        SetLabel(w, r.label);
        if (logged++ < 2) Log("fisico: subtitulo \"%s\" en la fila %d: estado %d %s, rotulo %s", r.label, i, st, ok ? "aceptado" : "RECHAZADO", FindComp(w, "setName") ? "ok" : "sin setName");
        return;
    }
    case K_PRESET: FillPresetRow(menu, list, i); return;
    case K_VARIANT: {   // (26/10) selector de forma: Robusto .. Normal .. Atletico, hombros caidos .. rectos, tipo, tamano
        G_SetRow(menu, 1, (unsigned)i); G_SetEnable(w); SetLabel(w, r.label);
        void* sel = FindComp(w, "selector"); const LSel* l = LSelOf(r.idx, r.comp);
        if (sel && l) G_SelFillList(sel, l->items, l->n, LSelIndex(*l, RowValue(r)));
        return;
    }
    case K_COMPARE: case K_SWITCH: {
        G_SetRow(menu, 1, (unsigned)i); G_SetEnable(w); SetLabel(w, r.label);
        void* sel = FindComp(w, "selector");
        if (sel) {
            if (r.kind == K_COMPARE) G_SelFillList(sel, kCompareItems, 2, MuscleCompareOn() ? 1 : 0);
            else G_SelFillList(sel, kSiNoRow, 2, g_cfg.live[r.idx][r.comp] > 0.5f ? 1 : 0);
        }
        return;
    }
    default: break;
    }
    const float fv = RowValue(r);
    const int v = ToUnits(r, fv);
    bool done = false;
    if (s_sliderMode.load() != 0) {   // barra ("c_slider"): componente de tipo 0x11 de la fila
        void* sl = CompOfType(w, 0x11);
        if (sl) {
            G_SetRow(menu, 2, (unsigned)i);
            G_SetEnable(w);
            SetLabel(w, RowLabel(r, fv));
            done = G_SliderRange2(sl, v, BarMin(r.kind), BarMax(r.kind)) == 0;
        }
        if (s_sliderMode.load() < 0) {
            s_sliderMode = done ? 1 : 0;
            Log("fisico: las filas de musculatura usan %s", done ? "barras (c_slider)" : "selectores con numeros (la fila no tiene barra)");
        }
    }
    if (!done) {   // selector con numeros
        G_SetRow(menu, 1, (unsigned)i);
        G_SetEnable(w);
        SetLabel(w, RowLabel(r, fv));
        void* sel = FindComp(w, "selector");
        if (sel) G_SelFillKind(sel, r.kind, v);
    }
}
static void FillMusclePage(void* menu, int page, int skipRow = -1) {
    void* list = List(menu);
    const MPage* pg = PageOf(page);
    if (!list || !pg) return;
    struct Guard { Guard() { s_filling = true; } ~Guard() { s_filling = false; } } guard;
    for (int i = 0; i < pg->n; i++) if (i != skipRow) FillRow(menu, list, i, pg->rows[i]);
    if (skipRow < 0) AddJump(menu, pg->n, pg->next);
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
// (24/10 d) fila 0 = color ("c_color"): muestra el tono como un cuadradito de color y con X abre la paleta de colores del
// juego con los 30 tonos (y "crear color" para uno propio). Si la fila no tiene componente de color queda el selector de antes.
static uint32_t s_toneRgba[kSkinTones]; static bool s_toneRgbaInit = false;   // bytes r, g, b, a (como los lee Ui_SetColor)
static void* s_colorComp = nullptr; static uint32_t s_colorShown = 0; static bool s_colorRow = false;
static uint32_t Rgba(unsigned char r, unsigned char g, unsigned char b) { return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xff000000u; }
static uint32_t ToneRgba(int t) { unsigned char c[3]; SkinToneSwatch(t, c); return Rgba(c[0], c[1], c[2]); }
static uint32_t ProfileRgba(int tono, uint32_t rgb) {
    if (rgb) return Rgba((unsigned char)(rgb >> 16), (unsigned char)(rgb >> 8), (unsigned char)rgb);
    if (tono > 0) return ToneRgba(tono);
    const int gi = FisicoSkinIndex();
    return ToneRgba(gi >= 1 ? 5 * gi - 2 : 13);
}
static void G_UiSetColor(void* comp, uint32_t rgba) {
    unsigned char b[4] = { (unsigned char)rgba, (unsigned char)(rgba >> 8), (unsigned char)(rgba >> 16), (unsigned char)(rgba >> 24) };
    __try { ((UiSetColorFn)kUiSetColor)(comp, b); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void ToneLabel(char* out, size_t n, int tono, uint32_t rgb) {
    if (rgb) snprintf(out, n, "Tono de piel (FX): propio");
    else if (tono > 0 && (tono - 3) % 5 == 0 && tono >= 3 && tono <= 28) snprintf(out, n, "Tono de piel (FX): %d (color %d)", tono, (tono - 3) / 5 + 1);
    else if (tono > 0) snprintf(out, n, "Tono de piel (FX): %d", tono);
    else snprintf(out, n, "Tono de piel (FX): del juego");
}
// (24/10 e) tonos conocidos: escala Monk (1 muy claro .. 10 muy oscuro) llevada a la escala de 30 tonos
static const int kMonk[10] = { 1, 2, 3, 6, 9, 15, 21, 27, 29, 30 };
static const char* kMonkName[11] = { "-", "1 Muy claro", "2 Claro", "3 Claro c\xC3\xA1lido", "4 Medio claro", "5 Medio", "6 Medio oscuro", "7 Bronceado", "8 Moreno", "9 Oscuro", "10 Muy oscuro" };
static int MonkOf(int tono) { for (int i = 0; i < 10; i++) if (kMonk[i] == tono) return i + 1; return 0; }
static const char* kSiNo[2] = { "No", "S\xC3\xAD" };
static const char* kZona[3] = { "Cara y cuerpo", "Solo el cuerpo", "Solo el torso" };   // (24/10 g) a que se aplica el tono
// filas 2..4: barras -25..+25 (o selectores con numeros si la fila no tiene barra)
static void FillBarRow(void* menu, void* list, int row, const char* label, int v) {
    void* w = Child(list, (unsigned)row);
    if (!w) return;
    bool done = false;
    if (s_sliderMode.load() != 0) {
        void* sl = CompOfType(w, 0x11);
        if (sl) { G_SetRow(menu, 2, (unsigned)row); G_SetEnable(w); SetLabel(w, label); done = G_SliderRange(sl, v) == 0; }
        if (s_sliderMode.load() < 0) s_sliderMode = done ? 1 : 0;
    }
    if (!done) { G_SetRow(menu, 1, (unsigned)row); G_SetEnable(w); SetLabel(w, label); void* sel = FindComp(w, "selector"); if (sel) G_SelFill(sel, v + kSteps); }
}
static void FillSkinPage(void* menu) {
    void* list = List(menu);
    if (!list) return;
    struct Guard { Guard() { s_filling = true; } ~Guard() { s_filling = false; } } guard;
    static char tones[kSkinTones + 1][24]; static const char* toneP[kSkinTones + 1];
    static bool init = false;
    if (!init) {
        snprintf(tones[0], sizeof tones[0], "Del juego"); toneP[0] = tones[0];
        for (int t = 1; t <= kSkinTones; t++) {
            if ((t - 3) % 5 == 0 && t >= 3 && t <= 28) snprintf(tones[t], sizeof tones[t], "%d (color %d)", t, (t - 3) / 5 + 1);
            else snprintf(tones[t], sizeof tones[t], "%d", t);
            toneP[t] = tones[t];
        }
        init = true;
    }
    if (!s_toneRgbaInit) { for (int t = 1; t <= kSkinTones; t++) s_toneRgba[t - 1] = ToneRgba(t); s_toneRgbaInit = true; }
    SkinToneProf q; SkinToneGetProf(s_editId, q);
    // fila 0: color
    s_colorComp = nullptr; s_colorRow = false;
    {
        void* w = Child(list, 0u);
        void* comp = nullptr;
        if (w && G_SetRow(menu, 5, 0u)) comp = CompOfType(w, 8);
        static int logged = 0;
        if (comp) {
            G_SetEnable(w);
            char lab[64]; ToneLabel(lab, sizeof lab, q.tono, q.rgb); SetLabel(w, lab);
            const uint32_t c = ProfileRgba(q.tono, q.rgb);
            G_UiSetColor(comp, c);
            uint32_t back = 0; Rd((char*)comp + 0xb4, back);
            s_colorComp = comp; s_colorShown = back; s_colorRow = true;
            if (logged++ < 3) Log("fisico: fila de color de la piel lista (componente %p, color %08x -> leido %08x)", comp, c, back);
        } else if (w) {
            if (logged++ < 3) Log("fisico: la fila 0 no tiene componente de color (estado 5): queda el selector con numeros");
            G_SetRow(menu, 1, 0u); G_SetEnable(w); SetLabel(w, "Tono de piel (FX)");
            void* sel = FindComp(w, "selector");
            if (sel) G_SelFillList(sel, toneP, kSkinTones + 1, q.tono);
        }
    }
    {   // fila 1: tono conocido (escala Monk)
        void* w = Child(list, 1u);
        if (w) { G_SetRow(menu, 1, 1u); G_SetEnable(w); SetLabel(w, "Tono conocido (escala Monk)"); void* sel = FindComp(w, "selector"); if (sel) G_SelFillList(sel, kMonkName, 11, q.rgb ? 0 : MonkOf(q.tono)); }
    }
    FillBarRow(menu, list, 2, "Luminosidad", q.luz);
    FillBarRow(menu, list, 3, "Saturaci\xC3\xB3n", q.sat);
    FillBarRow(menu, list, 4, "Matiz: oliva / rosado", q.matiz);
    {   // fila 5: el cuerpo toma el color de la cara (medido en la base del cuello)
        void* w = Child(list, 5u);
        if (w) { G_SetRow(menu, 1, 5u); G_SetEnable(w); SetLabel(w, "Cuerpo del color de la cara"); void* sel = FindComp(w, "selector"); if (sel) G_SelFillList(sel, kSiNo, 2, q.igualar ? 1 : 0); }
    }
    {   // (24/10 g) fila 6: a que se aplica el tono. "Solo el torso": el torso (TorsoBody) toma el tono y brazos, cuello, cara y
        // piernas quedan como el juego (para igualar un torso que salio de otro color que los brazos)
        void* w = Child(list, 6u);
        if (w) { G_SetRow(menu, 1, 6u); G_SetEnable(w); SetLabel(w, "Aplicar el tono a"); void* sel = FindComp(w, "selector"); if (sel) G_SelFillList(sel, kZona, 3, std::clamp(q.zona, 0, 2)); }
    }
}
// valor nuevo de una fila de la pagina de piel (idx = indice del selector o valor de la barra -25..25)
static void ApplySkin(int row, int idx) {
    if (!s_editId || row < 0 || row >= kSkinRows) return;
    SkinToneProf q; SkinToneGetProf(s_editId, q);
    switch (row) {
    case 0: q.tono = std::clamp(idx, 0, kSkinTones); q.rgb = 0; break;   // selector de numeros (sin componente de color)
    case 1: if (idx >= 1 && idx <= 10) { q.tono = kMonk[idx - 1]; q.rgb = 0; } else return; break;   // "-" no cambia nada
    case 2: q.luz = std::clamp(idx, -kSteps, kSteps); break;
    case 3: q.sat = std::clamp(idx, -kSteps, kSteps); break;
    case 4: q.matiz = std::clamp(idx, -kSteps, kSteps); break;
    case 5: q.igualar = idx ? 1 : 0; break;
    case 6: q.zona = std::clamp(idx, 0, 2); break;
    }
    SkinToneSetProf(s_editId, s_editName, q);
    if (row == 1 && s_colorRow && s_colorComp) {   // el cuadradito y el rotulo de la fila 0 siguen al tono
        const uint32_t c = ProfileRgba(q.tono, q.rgb); G_UiSetColor(s_colorComp, c); Rd((char*)s_colorComp + 0xb4, s_colorShown);
        void* w = Child(List(s_menu), 0u); if (w) { char lab[64]; ToneLabel(lab, sizeof lab, q.tono, q.rgb); SetLabel(w, lab); }
    }
}
// (24/10 e) la paleta la abre el menu base por su cuenta (no pasa por decidir), y toma sus 72 colores genericos de una
// tabla fija del exe (p_all, 0x1434d1dc0, solo lectura). Mientras la pagina Piel esta activa, esa tabla lleva en memoria
// una grilla ordenada de tonos (ver PalApply); al salir vuelve la original (el exe en disco no se toca)
static const uintptr_t kPalAll = 0x1434d1dc0ull; static const int kPalN = 72;
static uint32_t s_palOrig[kPalN]; static bool s_palSaved = false, s_palPatched = false;
static void PalWrite(const uint32_t* v) {
    DWORD old = 0; void* dst = (void*)kPalAll;
    if (!VirtualProtect(dst, sizeof(uint32_t) * kPalN, PAGE_READWRITE, &old)) { Log("fisico: no se pudo escribir la tabla de la paleta"); return; }
    memcpy(dst, v, sizeof(uint32_t) * kPalN);
    VirtualProtect(dst, sizeof(uint32_t) * kPalN, old, &old);
}
static uint32_t VariantRgba(int t, float dh, float kc) {   // tono t con el matiz corrido dh grados y el croma x kc (en Oklab)
    unsigned char c[3]; SkinToneSwatch(t, c);
    float lin[3]; for (int k = 0; k < 3; k++) { float v = c[k] / 255.0f; lin[k] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f); }
    float l = 0.4122214708f * lin[0] + 0.5363325363f * lin[1] + 0.0514459929f * lin[2], m = 0.2119034982f * lin[0] + 0.6806995451f * lin[1] + 0.1073969566f * lin[2], sc = 0.0883024619f * lin[0] + 0.2817188376f * lin[1] + 0.6299787005f * lin[2];
    l = cbrtf(l); m = cbrtf(m); sc = cbrtf(sc);
    float L = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * sc, A = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * sc, B = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * sc;
    const float r = dh * 3.14159265f / 180.0f, cs = cosf(r) * kc, sn = sinf(r) * kc; const float A2 = A * cs - B * sn, B2 = A * sn + B * cs; A = A2; B = B2;
    const float l_ = L + 0.3963377774f * A + 0.2158037573f * B, m_ = L - 0.1055613458f * A - 0.0638541728f * B, s_ = L - 0.0894841775f * A - 1.2914855480f * B;
    const float l3 = l_ * l_ * l_, m3 = m_ * m_ * m_, s3 = s_ * s_ * s_;
    float o[3] = { 4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3, -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3, -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3 };
    unsigned char q[3]; for (int k = 0; k < 3; k++) { float v = std::clamp(o[k], 0.0f, 1.0f); v = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f; q[k] = (unsigned char)std::clamp((int)lroundf(v * 255.0f), 0, 255); }
    return Rgba(q[0], q[1], q[2]);
}
static void PalApply(bool on) {
    if (on == s_palPatched) return;
    if (on) {
        if (!s_palSaved) { if (!Rd((void*)kPalAll, s_palOrig)) return; s_palSaved = true; }
        // (24/10 f) grilla de 12 x 6 (la vio el usuario): columnas = claro a oscuro (12 pasos de la escala), filas = matiz
        // (rosado fuerte, rosado, neutro = la escala tal cual, oliva, oliva fuerte); ultima fila: los 6 colores del juego y 6
        // tonos medios apagados. Solo la fila neutra se reconoce como "tono N"; el resto queda como color propio
        static const int kCol[12] = { 1, 3, 6, 9, 12, 15, 18, 21, 24, 27, 29, 30 };
        static const float kRowH[5] = { 10.0f, 5.0f, 0.0f, -5.0f, -10.0f }, kRowC[5] = { 1.10f, 1.05f, 1.0f, 0.97f, 0.92f };
        uint32_t v[kPalN];
        for (int r = 0; r < 5; r++) for (int c = 0; c < 12; c++) v[r * 12 + c] = r == 2 ? s_toneRgba[kCol[c] - 1] : VariantRgba(kCol[c], kRowH[r], kRowC[r]);
        for (int g = 1; g <= 6; g++) v[60 + g - 1] = ToneRgba(5 * g - 2);
        static const int kMuted[6] = { 6, 10, 14, 18, 22, 26 };
        for (int i = 0; i < 6; i++) v[66 + i] = VariantRgba(kMuted[i], 0.0f, 0.72f);
        PalWrite(v);
        Log("fisico: paleta del juego con la tabla de tonos (12 pasos x 5 matices + 6 del juego + 6 apagados)");
    } else if (s_palSaved) PalWrite(s_palOrig);
    s_palPatched = on;
}
// el color que muestra la fila (lo cambia la paleta del juego mientras esta abierta): si cambio, al perfil del jugador
static void SkinColorPoll(void* menu) {
    if (!s_colorRow || !s_colorComp || !s_editId) return;
    uint32_t v = 0; if (!Rd((char*)s_colorComp + 0xb4, v) || v == s_colorShown) return;
    s_colorShown = v;
    const uint32_t rgb24 = v & 0xffffffu;
    SkinToneProf q; SkinToneGetProf(s_editId, q);
    int t = 0; for (int i = 0; i < kSkinTones; i++) if ((s_toneRgba[i] & 0xffffffu) == rgb24) { t = i + 1; break; }
    const int gi = FisicoSkinIndex();
    q.rgb = 0;
    if (t > 0) q.tono = t;
    else if (gi >= 1 && (ToneRgba(5 * gi - 2) & 0xffffffu) == rgb24) q.tono = 0;
    else { q.tono = 0; q.rgb = ((v & 0xff) << 16) | (v & 0xff00) | ((v >> 16) & 0xff); }
    SkinToneSetProf(s_editId, s_editName, q);
    void* w = Child(List(menu), 0u);
    if (w) { char lab[64]; ToneLabel(lab, sizeof lab, q.tono, q.rgb); SetLabel(w, lab); }
    void* w1 = Child(List(menu), 1u); void* sel = w1 ? FindComp(w1, "selector") : nullptr;   // el tono conocido sigue al color
    if (sel) { s_filling = true; G_SelFillList(sel, kMonkName, 11, q.rgb ? 0 : MonkOf(q.tono)); s_filling = false; }
    static int n = 0; if (n++ < 20) Log("fisico: color de piel elegido %08x -> %s", v, q.rgb ? "propio" : q.tono ? "tono" : "del juego");
}
// despues de que el menu base abrio la paleta (decidir sobre la fila de color): ponerle los 30 tonos en lugar de los 72
// colores genericos. La ventana queda como hija del menu (+ 0x40, lista circular por + 0x48 / + 0x50)
static void PatchPalette(void* menu) {
    void* first = nullptr; if (!Rd((char*)menu + 0x40, first) || !first) { Log("fisico: la paleta no aparecio como hija del menu"); return; }
    void* c = first;
    for (int i = 0; i < 6 && c; i++) {
        void* vt = nullptr; void* parent = nullptr;
        if (Rd(c, vt) && vt == (void*)kPaletteVt && Rd((char*)c + 0x38, parent) && parent == menu) {
            __try {
                *(const uint32_t**)((char*)c + 0x2a * 8) = s_toneRgba;
                *(uint32_t*)((char*)c + 0x2b * 8) = (uint32_t)kSkinTones;
            } __except (EXCEPTION_EXECUTE_HANDLER) { Log("fisico: no se pudo cambiar la paleta"); return; }
            Log("fisico: paleta del juego abierta con los %d tonos", kSkinTones);
            return;
        }
        void* nx = nullptr; if (!Rd((char*)c + 0x50, nx) || nx == first) break; c = nx;
    }
    Log("fisico: no encontre la ventana de la paleta entre las hijas del menu");
}
// color de piel del juego (1..6) del jugador del menu: copia de su apariencia en menu + 0xf8 (+ 0x2c, bits 8..10; campo 0x50)
int FisicoSkinIndex() {
    void* menu = s_menu; uint32_t v = 0;
    if (!s_ok || !menu || !Rd((char*)menu + 0xf8 + 0x2c, v)) return 0;
    int i = (int)((v >> 8) & 7);
    return i >= 1 && i <= 6 ? i : 0;
}
static int s_capPage = -1; static double s_capT = 0;   // (26/10 c) captura de la pagina (para revisar el diseno)
static void GoPage(void* menu, int page, int cursor) {
    s_page = page;
    if (cursor <= 0) cursor = FirstRow(page);   // (26/10 c) no arrancar sobre un subtitulo
    s_capPage = page; s_capT = Now() + 0.8;
    if (page == 0) {
        o_rows(menu, nullptr, nullptr, nullptr);   // el juego vuelve a armar sus 13 filas (y sus valores)
        AddJump(menu, kJump0, "Musculatura  >");
        SetTitle(menu, PageTitle(0), 0);
    } else if (page == kSkinPage) {
        FillSkinPage(menu);
        SetTitle(menu, PageTitle(kSkinPage), 0);
    } else {
        FillMusclePage(menu, page);
        SetTitle(menu, PageTitle(page), 0);
    }
    G_SetNumRows(menu, CountOf(page));
    G_SetCursor(menu, cursor);
    PageHints(menu, page);
    Log("fisico: pagina %d (%s, id %u)", page, s_editName[0] ? s_editName : "jugador", s_editId);
}
static std::atomic<bool> s_hlChanged{false};   // un valor cambio: el resaltado vuelve a empezar
static double s_cfgDirtyT = 0;   // (26/10 c) opciones de todos cambiadas en el menu del juego: se guardan en PES21FX.ini
static void CfgDirty() { s_cfgDirtyT = Now(); }
// el tipo de cuerpo de la pagina sigue a los valores ("A mano" o el que coincida)
static void RefreshPresetRow(void* menu) {
    const MPage* pg = PageOf(s_page);
    if (!pg) return;
    for (int i = 0; i < pg->n; i++) if (pg->rows[i].kind == K_PRESET) {
        void* w = Child(List(menu), (unsigned)i); void* sel = w ? FindComp(w, "selector") : nullptr;
        if (sel) { s_filling = true; G_SelSetIdx(sel, (unsigned)(MusclePresetMatch(s_editId) + 1)); s_filling = false; }
    }
}
// barra: units = valor de la barra (o del selector ya corrido)
static void ApplyBar(void* menu, int row, int units) {
    const MRow* r = RowOf(s_page, row);
    if (!r || !IsBar(r->kind)) return;
    const float cur = RowValue(*r);
    if (ToUnits(*r, cur) == std::clamp(units, BarMin(r->kind), BarMax(r->kind))) return;   // sin cambio (p. ej. al pasar el cursor)
    const float f = FromUnits(*r, units);
    if (r->kind == K_GLOBAL) { g_cfg.live[r->idx][r->comp] = f; CfgDirty(); }
    else {
        if (!s_editId) return;
        MuscleProfileSet(s_editId, s_editName, r->idx, r->comp, f);
        s_hlChanged = true;
        RefreshPresetRow(menu);
    }
    if (void* w = Child(List(menu), (unsigned)row)) SetLabel(w, RowLabel(*r, f));   // (26/10 b) el rotulo muestra el valor
}
static int ReadRowSteps(void* menu, int row, int steps = kSteps) {
    void* w = Child(List(menu), (unsigned)row);
    if (!w) return 0;
    if (s_sliderMode.load() == 1) { void* sl = CompOfType(w, 0x11); if (sl) return G_SliderGet(sl); }
    void* sel = FindComp(w, "selector");
    return sel ? G_SelGet(sel) - steps : 0;
}
static int ReadRowUnits(void* menu, int row, int kind) { return ReadRowSteps(menu, row, -BarMin(kind)); }
// (26/10) fila "Complexion": indice del selector (0 = a mano, k + 1 = tipo k)
static void ApplyPreset(void* menu, int row, int sel) {
    if (!s_editId || sel < 1 || sel > MusclePresetCount()) return;
    MusclePresetApply(s_editId, s_editName, sel - 1);
    FillMusclePage(menu, s_page, row);   // las demas filas muestran los valores nuevos (la del selector queda como esta)
    s_hlChanged = true;
}
// selectores: tipo de cuerpo, "Ver" (comparar) e interruptores de todos
static void ApplySelector(void* menu, int row, int idx) {
    const MRow* r = RowOf(s_page, row);
    if (!r) return;
    switch (r->kind) {
    case K_PRESET: ApplyPreset(menu, row, idx); break;
    case K_COMPARE: MuscleCompare(idx == 1); break;
    case K_VARIANT: {   // (26/10) solo si cambio (el juego avisa tambien al pasar el cursor)
        const LSel* l = LSelOf(r->idx, r->comp);
        if (!s_editId || !l || LSelIndex(*l, RowValue(*r)) == std::clamp(idx, 0, l->n - 1)) break;
        MuscleProfileSet(s_editId, s_editName, r->idx, r->comp, l->vals[std::clamp(idx, 0, l->n - 1)]);
        s_hlChanged = true;
        RefreshPresetRow(menu);
        break; }
    case K_SWITCH: {
        const bool now = g_cfg.live[r->idx][r->comp] > 0.5f, want = idx != 0;
        if (now == want) break;   // el juego avisa tambien al pasar el cursor: solo si cambio
        const float b = g_cfg.base[r->idx][r->comp]; g_cfg.live[r->idx][r->comp] = want ? (b > 0.5f ? b : 1.0f) : 0.0f; CfgDirty(); break; }
    default: break;
    }
}
static void MusPageValue(void* menu, int row, int v, bool fromSet) {   // un valor cambio en una pagina de musculatura
    const MRow* r = RowOf(s_page, row);
    if (!r || r->kind == K_HEAD) return;
    if (IsBar(r->kind)) {
        if (fromSet) ApplyBar(menu, row, s_sliderMode.load() == 1 ? v : v + BarMin(r->kind));
        else ApplyBar(menu, row, ReadRowUnits(menu, row, r->kind));
    } else {
        if (fromSet) ApplySelector(menu, row, v);
        else { void* sel = FindComp(Child(List(menu), (unsigned)row), "selector"); if (sel) ApplySelector(menu, row, G_SelGet(sel)); }
    }
}

// ---------------------------------------------------------------- ganchos de la vtable de MenuEditPlBodySetting
static bool Mine(void* menu) { return menu && menu == s_menu; }
static void* __fastcall hk_rows(void* menu, void* a2, void* a3, void* a4) {
    void* r = o_rows(menu, a2, a3, a4);
    s_menu = menu; s_page = 0;
    s_capPage = 0; s_capT = Now() + 0.8;   // (26/10 c) captura del menu (para revisar el diseno)
    ReadEditPlayer();
    AddJump(menu, kJump0, "Musculatura  >");
    PageHints(menu, 0);
    SetTitle(menu, PageTitle(0), 0);
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
        if (IsMusPage(s_page) && row == JumpRow(s_page)) { GoPage(menu, s_page + 1, 0); return nullptr; }   // (cursor: la primera fila)
        if (s_page == kSkinPage && row == 0 && s_colorRow) return o_decide(menu, a2, a3, a4);   // la paleta del juego la abre el menu base
        if (s_page > 0) return nullptr;   // decidir sobre una barra: nada
    }
    return o_decide(menu, a2, a3, a4);
}
static void* __fastcall hk_back(void* menu, void* a2, void* a3, void* a4) {
    // (26/10) circulo / Volver desde cualquier pagina del mod: a la primera pestana de Fisico (la del juego), con el cursor en
    // "Musculatura  >" (L1 / R1 siguen pasando de a una pagina)
    if (Mine(menu) && s_page > 0) { GoPage(menu, 0, kJump0); return nullptr; }
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
    if (Mine(menu) && s_page == kSkinPage) {   // Restablecer en la pagina de piel: vuelve al color del juego
        if (s_editId) SkinToneSetProf(s_editId, s_editName, SkinToneProf());
        int cur = CursorRow(menu);
        FillSkinPage(menu);
        G_SetCursor(menu, cur < 0 ? 0 : cur);
        return nullptr;
    }
    if (Mine(menu) && s_page > 0) {   // Restablecer en una pagina de musculatura: esa pagina vuelve a +0 (como el juego)
        const int n = CountOf(s_page) - 1;
        for (int i = 0; i < n; i++) {
            const MRow* r = RowOf(s_page, i); if (!r) continue;
            if ((r->kind == K_MUS || r->kind == K_DEF || r->kind == K_VARIANT) && s_editId) MuscleProfileSet(s_editId, s_editName, r->idx, r->comp, 1.0f);
            else if (r->kind == K_GLOBAL) { g_cfg.live[r->idx][r->comp] = 1.0f; CfgDirty(); }
        }
        int cur = CursorRow(menu);
        FillMusclePage(menu, s_page);
        G_SetCursor(menu, cur < 0 ? 0 : cur);
        return nullptr;
    }
    return o_reset(menu, a2, a3, a4);
}
static void* __fastcall hk_val(void* menu, void* a2, void* a3, void* a4) {   // (menu, lista, fila): leer el valor de la fila
    const int list = (int)(intptr_t)a2, row = (int)(intptr_t)a3;
    if (Mine(menu) && s_page == kSkinPage) {
        if (s_filling) return nullptr;
        if (list == 0 && row == 0 && s_colorRow) SkinColorPoll(menu);   // la paleta se cerro
        else if (list == 0 && row >= 2 && row <= 4) ApplySkin(row, ReadRowSteps(menu, row));
        else if (list == 0) { void* sel = FindComp(Child(List(menu), (unsigned)row), "selector"); if (sel) ApplySkin(row, G_SelGet(sel)); }
        return nullptr;
    }
    if (Mine(menu) && s_page > 0) {
        if (s_filling || list != 0) return nullptr;
        MusPageValue(menu, row, 0, false);
        return nullptr;
    }
    if (Mine(menu) && row >= kJump0) return nullptr;
    return o_val(menu, a2, a3, a4);
}
static void* __fastcall hk_valset(void* menu, void* a2, void* a3, void* a4) {   // (menu, lista, fila, valor nuevo)
    const int list = (int)(intptr_t)a2, row = (int)(intptr_t)a3, v = (int)(intptr_t)a4;
    if (Mine(menu) && s_page == kSkinPage) {
        if (s_filling) return nullptr;
        if (list == 0 && row >= 2 && row <= 4) ApplySkin(row, s_sliderMode.load() == 1 ? v : v - kSteps);
        else if (list == 0 && !(row == 0 && s_colorRow)) ApplySkin(row, v);
        return nullptr;
    }
    if (Mine(menu) && s_page > 0) {
        if (s_filling || list != 0) return nullptr;
        MusPageValue(menu, row, v, true);
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
// (24/10 b) ademas del brillo, como en la cara: un aro y un punto blanco brillante sobre el musculo. Marca = punto de la piel
// en reposo (lado izquierdo; el shader espeja X) y radio del aro: x174..w174 = brillo, capsulas, todo, marcas; x186 latido
// (0..1), y186 desvanecido; 187 + k = marca k (punto, radio). El aro late como el de la cara (radio +- 6 %).
struct HlCap { float ax, ay, az, bx, by, bz, r; };
struct HlMark { float x, y, z, r; };
struct HlSet { int n; HlCap c[2]; bool all; int nm; HlMark m[2]; };
static HlSet HlFor(int idx, int comp) {
    static const HlCap kDelt = { 0.205f, 1.455f, 0.030f, 0.215f, 1.445f, 0.030f, 0.075f };
    static const HlCap kTrap = { 0.040f, 1.570f, -0.025f, 0.150f, 1.500f, -0.020f, 0.055f };
    HlSet h{}; auto one = [&](HlCap c) { h.c[h.n++] = c; };
    auto mark = [&](float x, float y, float z, float r) { if (h.nm < 2) h.m[h.nm++] = { x, y, z, r }; };
    switch (idx * 4 + comp) {   // marcas: punto sobre la piel en reposo y radio del aro
    case 164 * 4 + 1: case 171 * 4 + 3: mark(0.235f, 1.460f, 0.030f, 0.045f); break;   // hombros / ancho de hombros
    case 164 * 4 + 2: mark(0.300f, 1.360f, 0.070f, 0.035f); break;                     // biceps
    case 164 * 4 + 3: mark(0.300f, 1.355f, -0.020f, 0.035f); break;                    // triceps
    case 165 * 4 + 0: mark(0.500f, 1.170f, 0.085f, 0.030f); break;                     // antebrazos
    case 165 * 4 + 1: mark(0.090f, 1.375f, 0.170f, 0.050f); break;                     // pecho
    case 170 * 4 + 1: mark(0.080f, 1.470f, 0.130f, 0.035f); break;                     // pecho alto
    case 165 * 4 + 2: mark(0.080f, 1.300f, -0.120f, 0.070f); break;                    // espalda (toda)
    case 167 * 4 + 1: mark(0.100f, 1.545f, -0.010f, 0.035f); break;                    // trapecio
    case 167 * 4 + 2: mark(0.090f, 1.400f, -0.125f, 0.045f); break;                    // omoplatos
    case 167 * 4 + 3: mark(0.150f, 1.300f, -0.070f, 0.045f); break;                    // dorsales
    case 168 * 4 + 0: mark(0.040f, 1.120f, -0.120f, 0.040f); break;                    // espalda baja
    case 170 * 4 + 0: mark(0.000f, 1.580f, 0.080f, 0.035f); break;                     // cuello
    case 168 * 4 + 1: mark(0.045f, 1.150f, 0.180f, 0.045f); break;                     // abdominales
    case 168 * 4 + 2: mark(0.150f, 1.130f, 0.100f, 0.040f); break;                     // oblicuos
    case 171 * 4 + 1: mark(0.170f, 1.130f, 0.030f, 0.040f); break;                     // cintura
    case 168 * 4 + 3: mark(0.000f, 1.100f, 0.190f, 0.050f); break;                     // panza
    case 169 * 4 + 0: mark(0.085f, 0.950f, -0.120f, 0.050f); break;                    // gluteos
    case 171 * 4 + 2: mark(0.180f, 0.950f, 0.030f, 0.040f); break;                     // caderas
    case 165 * 4 + 3: mark(0.120f, 0.750f, 0.150f, 0.060f); break;                     // muslos (todo)
    case 169 * 4 + 1: mark(0.120f, 0.760f, 0.160f, 0.050f); break;                     // cuadriceps
    case 169 * 4 + 2: mark(0.120f, 0.760f, -0.020f, 0.050f); break;                    // isquiotibiales
    case 169 * 4 + 3: mark(0.060f, 0.760f, 0.070f, 0.040f); break;                     // aductores
    case 166 * 4 + 0: mark(0.160f, 0.400f, -0.020f, 0.040f); break;                    // pantorrillas
    case 171 * 4 + 0: mark(0.142f, 0.545f, 0.120f, 0.035f); break;                     // rodillas
    // (24/10 g) detalles
    case 162 * 4 + 0: mark(0.165f, 1.360f, 0.060f, 0.035f); break;                     // axilas
    case 162 * 4 + 1: mark(0.140f, 1.250f, 0.090f, 0.040f); break;                     // costados (serratos)
    case 162 * 4 + 2: mark(0.070f, 1.170f, -0.100f, 0.040f); break;                    // lumbares
    case 162 * 4 + 3: mark(0.050f, 1.505f, 0.070f, 0.030f); break;                     // base del cuello
    case 166 * 4 + 2: mark(0.000f, 1.590f, -0.030f, 0.035f); break;                    // nuca
    case 166 * 4 + 3: mark(0.400f, 1.255f, -0.010f, 0.030f); break;                    // codos
    case 170 * 4 + 3: mark(0.585f, 1.095f, 0.080f, 0.025f); break;                     // munecas
    // (26/10) v7
    case 260 * 4 + 0: mark(0.215f, 1.440f, 0.085f, 0.035f); break;                     // deltoides frontal
    case 260 * 4 + 1: mark(0.215f, 1.445f, -0.035f, 0.035f); break;                    // deltoides posterior
    case 260 * 4 + 2: mark(0.450f, 1.215f, 0.070f, 0.030f); break;                     // antebrazo alto
    case 260 * 4 + 3: mark(0.540f, 1.130f, 0.075f, 0.025f); break;                     // antebrazo bajo
    case 261 * 4 + 0: mark(0.100f, 1.330f, 0.170f, 0.040f); break;                     // pecho bajo
    case 261 * 4 + 1: mark(0.175f, 1.330f, -0.040f, 0.045f); break;                    // dorsales: ancho
    case 261 * 4 + 2: mark(0.080f, 1.570f, -0.020f, 0.030f); break;                    // trapecio alto
    case 261 * 4 + 3: mark(0.085f, 0.990f, -0.120f, 0.045f); break;                    // gluteo levantado
    case 262 * 4 + 0: mark(0.190f, 0.760f, 0.060f, 0.045f); break;                     // muslo exterior
    case 262 * 4 + 1: mark(0.160f, 0.430f, -0.060f, 0.035f); break;                    // gemelos
    }
    switch (idx * 4 + comp) {
    case 167 * 4 + 0: case 172 * 4 + 0: h.all = true; break;                                        // general / definicion
    case 260 * 4 + 0: one({ 0.200f, 1.460f, 0.070f, 0.215f, 1.420f, 0.075f, 0.045f }); break;        // deltoides frontal
    case 260 * 4 + 1: one({ 0.200f, 1.460f, -0.020f, 0.215f, 1.420f, -0.025f, 0.045f }); break;      // deltoides posterior
    case 260 * 4 + 2: one({ 0.420f, 1.240f, 0.045f, 0.490f, 1.180f, 0.055f, 0.050f }); break;        // antebrazo alto
    case 260 * 4 + 3: one({ 0.500f, 1.160f, 0.055f, 0.575f, 1.095f, 0.060f, 0.040f }); break;        // antebrazo bajo
    case 261 * 4 + 0: one({ 0.040f, 1.340f, 0.165f, 0.140f, 1.345f, 0.135f, 0.050f }); break;        // pecho bajo
    case 261 * 4 + 1: one({ 0.165f, 1.400f, -0.040f, 0.150f, 1.220f, -0.030f, 0.055f }); break;      // dorsales: ancho
    case 261 * 4 + 2: one({ 0.040f, 1.590f, -0.020f, 0.120f, 1.540f, -0.015f, 0.040f }); break;      // trapecio alto
    case 261 * 4 + 3: one({ 0.085f, 1.010f, -0.070f, 0.085f, 0.980f, -0.070f, 0.065f }); break;      // gluteo levantado
    case 262 * 4 + 0: one({ 0.170f, 0.900f, 0.050f, 0.180f, 0.620f, 0.060f, 0.055f }); break;        // muslo exterior
    case 262 * 4 + 1: one({ 0.160f, 0.500f, -0.010f, 0.165f, 0.330f, -0.010f, 0.050f }); break;      // gemelos
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
    case 162 * 4 + 0: one({ 0.150f, 1.400f, 0.070f, 0.170f, 1.330f, 0.050f, 0.045f }); break;        // axilas
    case 162 * 4 + 1: one({ 0.140f, 1.320f, 0.080f, 0.150f, 1.200f, 0.070f, 0.050f }); break;        // costados (serratos)
    case 162 * 4 + 2: one({ 0.070f, 1.220f, -0.090f, 0.070f, 1.100f, -0.090f, 0.050f }); break;      // lumbares
    case 162 * 4 + 3: one({ 0.030f, 1.520f, 0.040f, 0.080f, 1.500f, 0.020f, 0.045f }); break;        // base del cuello
    case 166 * 4 + 2: one({ 0.000f, 1.550f, -0.020f, 0.000f, 1.660f, -0.010f, 0.050f }); break;      // nuca
    case 166 * 4 + 3: one({ 0.385f, 1.270f, 0.030f, 0.415f, 1.245f, 0.030f, 0.045f }); break;        // codos
    case 170 * 4 + 3: one({ 0.570f, 1.105f, 0.058f, 0.610f, 1.075f, 0.058f, 0.035f }); break;        // munecas
    }
    return h;
}
static int s_hlRow = -1, s_hlPage = -1; static double s_hlT0 = 0; static bool s_hlOn = false;
static void HlClear() { for (int i = 174; i <= 182; i++) for (int c = 0; c < 4; c++) g_cfg.live[i][c] = 0.0f;
                        for (int i = 186; i <= 188; i++) for (int c = 0; c < 4; c++) g_cfg.live[i][c] = 0.0f; }
void FisicoHighlightPresent() {
    void* menu = s_menu;
    if (s_cfgDirtyT > 0.0 && Now() - s_cfgDirtyT > 1.5) { s_cfgDirtyT = 0.0; SaveConfigValues(); }   // (26/10 c) opciones de todos cambiadas en el juego
    if (MuscleCompareOn() && !MusclePhysiqueActive()) MuscleCompare(false);   // "Ver: como el juego" solo mientras se edita
    const bool skinPage = s_ok && menu && s_page == kSkinPage && MusclePhysiqueActive();
    if (skinPage) SkinColorPoll(menu);
    PalApply(skinPage && s_colorRow && s_toneRgbaInit);
    const bool want = s_ok && g_cfg.live[183][0] > 0.5f && menu && s_page > 0 && MusclePhysiqueActive();
    const MRow* r = want ? RowOf(s_page, CursorRow(menu)) : nullptr;
    // (26/10 c) pedido de la ventana del mod (en los ultimos 0.25 s): manda sobre la fila del juego
    static MRow s_reqRow = { "", 0, 0, K_MUS };
    const int req = s_hlReq.load(); const bool reqOn = req >= 0 && (long long)(Now() * 1000.0) - s_hlReqT.load() < 250;
    if (reqOn && s_ok && g_cfg.live[183][0] > 0.5f && menu && MusclePhysiqueActive()) { s_reqRow.idx = req / 4; s_reqRow.comp = req % 4; r = &s_reqRow; }
    if (r && (r->kind == K_HEAD || r->kind == K_GLOBAL || r->kind == K_SWITCH)) r = nullptr;   // subtitulos y opciones de todos: nada
    // (26/10) los selectores de forma (hombros) no resaltan nada: el brillo tapaba justo lo que se quiere ver.
    // (26/10 e) tampoco la seccion "Todo el cuerpo": complexion, tipo de cuerpo, general, definicion muscular y ver (lo pidio el
    // usuario: el brillo de todo el cuerpo lo dejaba blanco y no se veia el cambio); tambien desde la ventana del mod
    if (r && (r->kind == K_VARIANT || r->kind == K_PRESET || r->kind == K_COMPARE || r->kind == K_DEF || LSelOf(r->idx, r->comp) ||
              (r->idx == 167 && r->comp == 0) || (r->idx == 172 && r->comp == 0) || (r->idx == 262 && r->comp == 2))) r = nullptr;
    if (!r) { if (s_hlOn) { HlClear(); s_hlOn = false; s_hlRow = -1; } return; }
    const double t = Now();
    const int row = reqOn ? 1000 + req : CursorRow(menu);
    if (row != s_hlRow || s_page != s_hlPage || s_hlChanged.exchange(false)) { s_hlRow = row; s_hlPage = s_page; s_hlT0 = t; }
    const float dur = g_cfg.live[183][2], age = (float)(t - s_hlT0);
    float fade = 1.0f;
    if (dur > 0.05f) fade = age <= dur ? 1.0f : std::max(0.0f, 1.0f - (age - dur) / 0.6f);
    const float per = std::clamp(g_cfg.live[183][3] > 0.05f ? g_cfg.live[183][3] : 1.6f, 0.4f, 4.0f);
    const float pulse = 0.5f + 0.5f * sinf(6.2831853f * (float)fmod(t - s_hlT0, 1000.0) / per - 1.5707963f);   // arranca abajo y sube
    const float k = std::clamp(g_cfg.live[183][1], 0.0f, 1.5f) * (0.35f + 0.65f * pulse) * fade;
    const HlSet h = HlFor(r->idx, r->comp);
    HlClear();
    g_cfg.live[174][0] = k; g_cfg.live[174][1] = (float)h.n; g_cfg.live[174][2] = h.all ? 1.0f : 0.0f; g_cfg.live[174][3] = (float)h.nm;
    for (int i = 0; i < h.n; i++) {
        const HlCap& c = h.c[i];
        float* A = g_cfg.live[175 + 2 * i]; float* B = g_cfg.live[176 + 2 * i];
        A[0] = c.ax; A[1] = c.ay; A[2] = c.az; A[3] = c.r; B[0] = c.bx; B[1] = c.by; B[2] = c.bz; B[3] = 0.0f;
    }
    g_cfg.live[186][0] = pulse; g_cfg.live[186][1] = fade;
    for (int i = 0; i < h.nm; i++) { float* M = g_cfg.live[187 + i]; M[0] = h.m[i].x; M[1] = h.m[i].y; M[2] = h.m[i].z; M[3] = h.m[i].r; }
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
    Log("fisico: menu Fisico del juego enganchado: fila 'Musculatura' (%d paginas con subtitulos + piel, barras -%d..+%d; L1 / R1 cambian de pagina)", kNMP, kMSteps, kMSteps);
}
// (26/10 c) una captura de cada pagina al mostrarla (PES21FX\capturas\fisico_p<pagina>.jpg, se pisa): para revisar el diseno
void FisicoCapturePresent(ID3D11Texture2D* frame) {
    if (s_capPage < 0 || !frame || Now() < s_capT) return;
    const int page = s_capPage; s_capPage = -1;
    if (!s_ok || !MusclePhysiqueActive()) return;
    const std::wstring dir = g_cfg.baseDir + L"capturas"; CreateDirectoryW(dir.c_str(), nullptr);
    wchar_t nm[64]; swprintf(nm, 64, L"\\fisico_p%d.jpg", page);
    if (AsyncShot(frame, dir + nm)) { static int n = 0; if (n++ < 30) Log("fisico: captura de la pagina %d (capturas\\fisico_p%d.jpg)", page, page); }
}
// (26/10 c) la ventana del mod (musculo_ui.cpp) pide el resaltado de un musculo (mouse encima o elegido)
void FisicoHighlightRequest(int idx, int comp) { s_hlReq = idx * 4 + comp; s_hlReqT = (long long)(Now() * 1000.0); }
void FisicoCfgDirty() { CfgDirty(); }
const char* FisicoEditName() { return s_editName; }
bool FisicoEditing(uint32_t* id) { if (id) *id = s_editId; return s_ok && s_menu && s_editId; }

} // namespace fx
