// ---------------------------------------------------------------- (11/11) gizmo de camara del Motor en tiempo real
// Agustin 23:33: el Motor "que venga con la camara libre y las opciones de camara como un icono". 23:36: "mostralas como blender
// en una esquina con un icono clickeable para cambiar los tipos de camara ... en blender arriba a la derecha se muestra un icono
// que se mueve al mover la camara y muestra los ejes z, y, x y ademas permite ahi seleccionar cada uno con un click o mover la
// camara desde ahi".
// El juego no trae nada parecido: lo dibuja la DLL, solo con el estudio del Motor abierto (MotorPageActive, motorpage.cpp).
//   - bola de ejes arriba a la derecha: X rojo, Y verde (arriba en el mundo del juego), Z azul; gira con la camara libre;
//   - clic en la punta de un eje: la camara se pone de ese lado y mira al pivote (freecam.cpp FreeCamGizmoAxis);
//   - arrastrar sobre la bola: orbita alrededor del pivote (FreeCamGizmoOrbit);
//   - dos botones debajo: camara (cambia entre "Libre" y "Del juego") e inicio (vuelve a la pose de la camara del juego).
// Con la camara libre del Motor: boton derecho apretado = volar (mouse gira, W A S D Q E mueven), rueda = zoom.
// menumouse.cpp pregunta MotorGizmoOver para no mandar ese clic al menu del juego. [PES21FX] motor_gizmo = 0 lo saca.
#include "core.h"
#include <imgui.h>
#include <WICTextureLoader.h>
#include <string>
#include <mutex>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>

namespace fx {

static std::atomic<float> s_cx{0}, s_cy{0}, s_rad{0}, s_bx0{0}, s_by0{0}, s_bx1{0}, s_by1{0};
static std::atomic<double> s_drawT{-1.0e9};
static std::atomic<bool> s_dragging{false};
static bool s_lWas = false, s_press = false, s_moved = false; static float s_px = 0, s_py = 0, s_lx = 0, s_ly = 0;
static float s_yaw = 3.14159f, s_pitch = 0.0f;       // ultima orientacion conocida (con la camara del juego no se sabe: queda la ultima)

// (15/11 n) con el menu del mod abierto el editor se sigue dibujando (detras del menu: MotorDL da la lista de fondo) y no toma el mouse
bool MotorGizmoPoll() { return (g_cfg.motorGizmo != 0 || MotorUiOn()) && MotorPageActive(); }
static ImDrawList* MotorDL() { return g_menuOpen.load(std::memory_order_relaxed) ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList(); }

// ---------------------------------------------------------------- (15/11) barra de iconos del menu propio del Motor
// Agustin 01:51: "sin el menu tipico del modo editar con un boton para volver atras, y en cuanto a los iconos de este si podes
// inyectarlos por sider". El estudio abre vacio; arriba a la izquierda va el boton Volver con el titulo y debajo una columna de
// iconos: Vista (abre / cierra el panel nativo de filas), Jugador (lo carga / lo quita) y Grilla. Los dibujos de los iconos son
// archivos PNG de la carpeta del Motor en Sider (content\motor-tiempo-real\iconos\<nombre>.png: blancos con transparencia,
// se cambian sin recompilar); si falta alguno va un dibujo simple hecho por la DLL. Sider no puede dibujar fuera de su propio
// overlay, por eso el dibujo en pantalla lo hace la DLL.
static const char* const kIconFile[4] = { "volver", "vista", "jugador", "grilla" };
static ID3D11ShaderResourceView* s_icon[4] = {}; static bool s_iconTried = false;
static bool s_tlWas = false; static int s_tPress = -1;

static void IconsLoad() {
    s_iconTried = true;
    // (15/11 b) la carpeta content del Motor esta al lado de sider.dll (en la PC de Agustin: <juego>\Dream Patch Sider\), no al
    // lado del exe: se busca primero ahi y, si Sider no esta cargado o no tiene la carpeta, al lado del exe.
    if (!g_dev.dev) return;
    std::wstring dir;
    for (int k = 0; k < 2 && dir.empty(); k++) {
        HMODULE m = k == 0 ? GetModuleHandleW(L"sider.dll") : nullptr; if (k == 0 && !m) continue;
        wchar_t mod[MAX_PATH] = L""; const DWORD n = GetModuleFileNameW(m, mod, MAX_PATH); if (n == 0 || n >= MAX_PATH) continue;
        std::wstring d(mod); const size_t cut = d.find_last_of(L"\\/"); if (cut == std::wstring::npos) continue;
        d = d.substr(0, cut + 1) + L"content\\motor-tiempo-real\\iconos\\";
        if (GetFileAttributesW(d.c_str()) != INVALID_FILE_ATTRIBUTES) dir = d;
    }
    if (dir.empty()) { Log("motor (iconos): no esta la carpeta content\\motor-tiempo-real\\iconos (ni junto a sider.dll ni junto al exe): los dibuja la DLL"); return; }
    EnsureComInit(); int ok = 0;
    for (int i = 0; i < 4; i++) {
        std::wstring f = dir; for (const char* c = kIconFile[i]; *c; c++) f += (wchar_t)*c; f += L".png";
        if (GetFileAttributesW(f.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        if (SUCCEEDED(DirectX::CreateWICTextureFromFileEx(g_dev.dev, nullptr, f.c_str(), 256, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0,
                DirectX::WIC_LOADER_IGNORE_SRGB | DirectX::WIC_LOADER_FORCE_RGBA32, nullptr, &s_icon[i])) && s_icon[i]) ok++;
        else s_icon[i] = nullptr;
    }
    Log("motor (iconos): %d de 4 archivos PNG cargados de %ls (los que faltan los dibuja la DLL)", ok, dir.c_str());
}
// dibujo de reserva de cada icono (si no esta su PNG)
static void IconFallback(ImDrawList* dl, int i, const ImVec2& o, float s, ImU32 ink, float k) {
    if (i == 0) {            // flecha a la izquierda
        dl->AddTriangleFilled(ImVec2(o.x - s * 0.30f, o.y), ImVec2(o.x, o.y - s * 0.26f), ImVec2(o.x, o.y + s * 0.26f), ink);
        dl->AddRectFilled(ImVec2(o.x - 1.0f, o.y - s * 0.09f), ImVec2(o.x + s * 0.30f, o.y + s * 0.09f), ink);
    } else if (i == 1) {     // ojo
        dl->AddCircle(o, s * 0.27f, ink, 24, 2.2f * k); dl->AddCircleFilled(o, s * 0.11f, ink, 16);
    } else if (i == 2) {     // persona
        dl->AddCircleFilled(ImVec2(o.x, o.y - s * 0.17f), s * 0.12f, ink, 16);
        dl->AddRectFilled(ImVec2(o.x - s * 0.20f, o.y + 0.0f), ImVec2(o.x + s * 0.20f, o.y + s * 0.28f), ink, s * 0.10f);
    } else {                 // grilla
        for (int j = -1; j <= 1; j++) {
            dl->AddLine(ImVec2(o.x + j * s * 0.18f, o.y - s * 0.27f), ImVec2(o.x + j * s * 0.18f, o.y + s * 0.27f), ink, 1.6f * k);
            dl->AddLine(ImVec2(o.x - s * 0.27f, o.y + j * s * 0.18f), ImVec2(o.x + s * 0.27f, o.y + j * s * 0.18f), ink, 1.6f * k);
        }
    }
}
// (15/11 c) Agustin 02:21..02:28: "no me gusta la forma de los menus", "quiero menus nuevos ... con formas nuevas, funciones
// nuevas", "aspecto de motor real, como si fuera el motor fox engine en tiempo real", "funciones de un motor de juego real".
// Distribucion de EDITOR DE MOTOR (PROVISORIA dibujada por la DLL hasta tener la pantalla propia del juego, ver memoria
// motor-menu-nativo-propio): barra de herramientas arriba, arbol de ESCENA a la izquierda, INSPECTOR a la derecha con las
// propiedades de lo marcado en el arbol, barra de estado abajo. Solo muestra cosas que existen y funcionan (nada de adorno).
bool MotorGizmoOver(float px, float py);
static void SetCam(bool on);
// (15/11 f, Agustin 02:44: "que tenga un log para los errores y se muestren en un recuadro rojo con texto explicando como pasa
// en unreal") errores del Motor: cualquier parte (hilo del juego o de Present) llama MotorErr("que paso y por que"); quedan en
// el registro (PES21FX.log, "motor (ERROR)") y se muestran 10 s en un recuadro rojo dentro de la vista 3D; la barra de estado
// lleva la cuenta y con un clic los vuelve a mostrar.
struct MErr { double t; char msg[220]; };
static MErr s_err[8]; static int s_errTotal = 0; static std::mutex s_errMx;
void MotorErr(const char* fmt, ...) {
    char b[220]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, TRS(fmt), ap); va_end(ap);
    bool nuevo = true;
    {   std::lock_guard<std::mutex> lk(s_errMx);
        if (s_errTotal > 0) { MErr& u = s_err[(s_errTotal - 1) & 7]; if (strcmp(u.msg, b) == 0 && Now() - u.t < 30.0) { u.t = Now(); nuevo = false; } }
        if (nuevo) { MErr& e = s_err[s_errTotal & 7]; e.t = Now(); memcpy(e.msg, b, sizeof b); s_errTotal++; } }
    if (nuevo) Log("motor (ERROR): %s", b);
}
static std::atomic<float> s_hr[16][4];   // (17/11) 16: + linea de tiempo                // zonas que tapan el clic (pixeles de la ventana): barra, escena, inspector, estado, ventana del centro, menu abierto
static std::atomic<int> s_hrN{0};
static float s_openA = 0.0f, s_selY = -1.0f, s_inspA = 1.0f; static double s_uiT = -1.0e9; static int s_selWas = 0;   // (15/11 d) animaciones
static MotorLight s_lights[64]; static int s_nLights = 0; static MotorSceneInfo s_info{}; static double s_sceneT = -1.0e9; static uintptr_t s_selObj = 0;   // (15/11 e) luces de la escena
static int s_sel = 0, s_hotPrev = -1;                // fila marcada del arbol (0 render, 1 camara, 2 grilla, 3 jugador)
// (15/11 o, Agustin 05:13 / 05:15: "los botones para abrir o cerrar la seccion de luces no funciona la flecha", "la flecha de escena
// tampoco") secciones del arbol que se pliegan con su flecha (y la de luces tambien con un clic en su fila); la flecha gira animada
static int s_jKind = 0, s_jN[2] = { 44, 1 }, s_jSent = -1; static bool s_jMirror = false, s_jTouched = false;      // (17/11) Inspector del jugador: animacion elegida (pose 1..45, festejo 1..162); s_jSent < 0 = todavia no se pidio ninguna
static bool s_foldStudio = false, s_foldLights = false; static float s_foldA[2] = { 1.0f, 1.0f };
static bool s_foldPj = false; static float s_foldPjA = 1.0f;      // (17/11 03:19, Agustin: los jugadores "a la izquierda con una flecha") grupo Jugadores del panel Escena
// (15/11 o, Agustin 05:12: "opciones para mover las pestanas de la izquierda y derecha asi como opciones para cerrarlas y abrirlas, para
// mover o reescalar el tamano, algo asi como funciona blender") paneles: el borde de adentro se arrastra (ancho), la X del titulo lo
// cierra y la pestanita del costado (o Ver > Paneles, T / N como en Blender) lo vuelve a abrir. Se guarda en el ini.
static int s_pDrag = 0; static float s_pA[2] = { -1.0f, -1.0f }; static float s_rightPx = -1.0f;
// (15/11 q, Agustin 17:27: "permitirme mover las cartas y que se adapten") cada panel se agarra del titulo y se suelta a la izquierda,
// a la derecha (si ya hay otro, se apilan arriba / abajo con una division que se arrastra) o en cualquier otro lado (flotante, con
// esquina para cambiar el tamano). La vista 3D, el gizmo, los avisos y las ventanas usan lo que dejan libre los paneles pegados.
// s_mv: 0 nada, 1 / 2 = panel agarrado (+1); s_mvOn = ya se movio (antes de 5 puntos es un clic); s_rsz 1 / 2 = esquina; s_spl = division
static int s_mvId = 0, s_rsz = 0; static bool s_mvOn = false, s_spl = false; static float s_mvGX = 0, s_mvGY = 0, s_mvW = 0, s_mvH = 0, s_mvPX = 0, s_mvPY = 0;
static int s_dropDock = -1, s_dropTop = 0; static float s_dropR[4] = {};
// (15/11 q) ventanita de la vista extra (motorextra.cpp): posicion y ancho en puntos de 1080p; s_xvDrag 1 mover, 2 girar la camara extra, 3 tamano
static float s_xv[3] = { -1.0f, -1.0f, 420.0f }, s_xvPx = 0, s_xvPy = 0, s_xvGX = 0, s_xvGY = 0, s_xvLX = 0, s_xvLY = 0; static int s_xvDrag = 0;
static void DockSave() { char b[160]; snprintf(b, sizeof b, "%d,%d", g_cfg.motorDock[0], g_cfg.motorDock[1]); SaveOption("motor_panel_lugar", b);
    snprintf(b, sizeof b, "%.2f", g_cfg.motorSplit); SaveOption("motor_panel_division", b); SaveOption("motor_panel_arriba", g_cfg.motorStackTop ? "1" : "0");
    const float (&f)[2][4] = g_cfg.motorFloat; snprintf(b, sizeof b, "%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f", f[0][0], f[0][1], f[0][2], f[0][3], f[1][0], f[1][1], f[1][2], f[1][3]); SaveOption("motor_panel_flotante", b); }
// (15/11 o, Agustin 05:16: "esta parte deberia dejar modificar variables") campos del inspector: arrastrar de costado cambia el numero
// (Shift = fino), un clic sin arrastrar deja escribirlo (Enter o clic afuera aplica, Esc cancela); sliders con su numero al lado
static int s_dragId = -1, s_pressId = -1, s_editId = -1, s_editDone = 0, s_editAge = 0, s_editLen = 0; static float s_pressVal = 0.0f, s_dragV0 = 0.0f, s_dragX0 = 0.0f;
static bool s_dragMoved = false, s_editFresh = false, s_colorOpen = false, s_gridDirty = false; static char s_editBuf[24];
// (15/11 p, Agustin 05:31: "cuando despues te pida agregar mas cosas que usen esa opcion de cerrar") cada seccion del inspector y de las
// Preferencias se pliega con su flecha; lo plegado se recuerda por nombre de seccion y por lo que esta marcado (luz, grilla...)
struct FoldS { uint32_t h; float a; bool shut; }; static FoldS s_fold[64]; static int s_foldN = 0; static uint32_t s_secHash[40]; static int s_secN = 0;
static FoldS& FoldOf(uint32_t h) { for (int i = 0; i < s_foldN; i++) if (s_fold[i].h == h) return s_fold[i]; if (s_foldN < 64) { s_fold[s_foldN] = FoldS{ h, 1.0f, false }; return s_fold[s_foldN++]; } return s_fold[63]; }
static uint32_t HashS(const char* t) { uint32_t h = 2166136261u; for (; *t; t++) { h ^= (unsigned char)*t; h *= 16777619u; } return h; }
// (15/11 p, Agustin 05:36: "una seccion de configuracion o preferencias que permita subir el numero de pasos del ctrl z, activar el
// autoguardado, modificar el tamano del texto, etc", "arriba en editar") Editar > Preferencias (Ctrl + ,): ventana con categorias a la
// izquierda (Interfaz, Edicion, Camara, Sonido, Idioma); todo se guarda en [PES21FX] del ini al soltar.
static int s_prefCat = 0, s_prefSave = 0, s_selEd = -1; static float s_pendScale = -1.0f, s_pendText = -1.0f; static double s_autoT = -1.0; static int s_autoMark = -1, s_actCount = 0;
// (15/11 p, Agustin 05:37: "que tambien funcione el atajo, y que se muestre el icono abajo a la derecha") el ultimo atajo del Motor y lo que hizo
static char s_kbLabel[64] = ""; static std::atomic<double> s_kbLabelT{-1.0e9};
static std::atomic<int> s_curKind{0};      // (15/11 p) cursor que pide el editor: 0 el normal, 3 flecha doble de costado (borde de un panel), 4 vertical
static float s_pjIdF = 7511.0f;      // (17/11 etapa 2) id escrito en el Inspector para agregar un jugador propio
// (17/11 06:36..06:38) hasta 32 jugadores: la lista del panel Escena muestra kPjRows filas y corre con la rueda; animacion y gesto por jugador
static const int kPjRows = 10; static int s_pjScroll = 0; static int s_pjGroup = -1;   // s_pjGroup: grupo de clips a la vista en el Inspector
static bool s_pjSnap = false; static int s_camFollow = 0;   // (20:47) 0 quieta, 1 sigue al elegido, 2 sigue y mira la cara   // s_camFollow (07:16): la camara acompana al jugador elegido cuando se mueve
        // ajustar a la grilla (lugar a la celda, giro de a 15 grados) al arrastrar
static int s_pjListTop = -1;         // primera fila de la lista de clips del Inspector
static int s_pjRowClip[8] = {}, s_pjRowFace[6] = {}, s_pjFaceScroll = 0;
// (17/11 18:01-18:02, Agustin: "un menu mas accesible con mas opciones, mas descripcion", "opciones de bajar con la rueda del mouse en esta
// seccion") listas de clips y gestos: la rueda encima las recorre (rectangulo del cuadro anterior); pestanas Grupo / Favoritos / Recientes
static float s_clR[4] = { 0, 0, -1, -1 }, s_fcR[4] = { 0, 0, -1, -1 }; static int s_clWheel = 0, s_pjTab = 0, s_flTop = 0;
static double s_tipT = 0.0; static int s_tipId = -1; static ImVec2 s_tipA, s_tipB;
// (17/11 07:27, Agustin: "la lectura del clip del juego deberia tener nombres de los archivos y un buscador") buscador de clips y gestos:
// clic en el campo y escribir (las letras llegan por WM_CHAR, overlay.cpp MotorUiChar); Esc o la X lo borra. Con texto, la lista muestra
// los clips de TODOS los grupos cuyo archivo contiene lo escrito (sin distinguir mayusculas).
static std::mutex s_srMx; static char s_sr[48] = ""; static std::atomic<int> s_srOn{0};   // 0 nada, 1 clips, 2 gestos
static int s_poseBone = 0; static bool s_edPts = true;   // (23:08) editor: puntos de los huesos en la vista static int s_srTop = 0, s_srFTop = 0; static std::atomic<bool> s_srEsc{false};
struct PjClip { bool ok; float pos[3], yaw, scale, speed, faceW; int clip, face; bool paused, blink; }; static PjClip s_pjCb{};   // (07:23) copiar / pegar entre jugadores
static bool SrMatch(const char* f, const char* q) { if (!q[0]) return true; for (; *f; f++) { const char* a = f; const char* b = q; while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; } if (!*b) return true; } return false; }
bool MotorUiChar(unsigned c) {
    if (!s_srOn.load()) return false;
    std::lock_guard<std::mutex> lk(s_srMx); size_t l = strlen(s_sr);
    if (c == 8) { if (l) s_sr[l - 1] = 0; }
    else if (c == 27 || c == 13) { s_srOn.store(0); if (c == 27) s_srEsc.store(true); }
    else if (c >= 32 && c < 127 && l + 1 < sizeof s_sr) { s_sr[l] = (char)c; s_sr[l + 1] = 0; s_srTop = 0; s_srFTop = 0; }
    return true; }
static int s_tlMode = 0;             // (06:52) linea de tiempo abajo: 0 automatica (con jugadores), 1 siempre, 2 oculta; s_tlAll: los botones mueven a todos
static bool s_tlAll = false; static int s_tlScroll = 0;   // lo que hay en cada fila de las listas (para el clic)
// nombres para la interfaz: grupo de clips (clave del catalogo de cinematicas), clip sin "dml_" ni ".gani", gesto con su familia
static const char* GroupLabel(int g) {
    static const struct { const char* key; const char* es; } kG[] = { { "assistant", "Juez de l\xC3\xADnea" }, { "change", "Cambios" }, { "coach", "T\xC3\xA9" "cnico" },
        { "end", "Final del partido" }, { "ent", "Entrada a la cancha" }, { "foul", "Faltas y tarjetas" }, { "goal", "Goles y festejos" }, { "hugSet", "Abrazos" },
        { "injury", "Lesiones" }, { "md", "Durante el partido (md)" }, { "menu", "Men\xC3\xBAs" }, { "mobH", "Gente alrededor (mobH)" }, { "offsideGoal", "Gol en offside" },
        { "pk", "Penales" }, { "prop", "Utiler\xC3\xAD" "a" }, { "tu", "Otros (tu)" }, { "idle", "Parado" }, { "match", "Partido" }, { "uniselect", "Elegir uniforme" }, { "propias", "Propias (hechas a pedido)" } };
    const char* k = MotorClipGroupKey(g); for (const auto& e : kG) if (!strcmp(e.key, k)) return e.es; return k; }
static void ClipLabel(int c, char* out, size_t n) { snprintf(out, n, "%s", MotorClipFile(c)); }   // (07:27) el nombre del archivo entero
// (17/11 18:01, Agustin: "mas descripcion") descripcion en castellano armada con las partes del nombre del archivo (motorclipdesc.inc):
// dml_foul_beJudged02.gani -> "Falta Â· lo sancionan 02"; lo que no esta en la tabla queda como viene
#include "motorclipdesc.inc"
static void ClipDesc(int c, char* out, size_t n) {
    const char* f = MotorClipFile(c); char b[192]; snprintf(b, sizeof b, "%s", f);
    // (17/11 19:30) clips propios (tools/gani_cuerpo.py): nombre armado a mano
    static const struct { const char* f; const char* es; } kOwn[] = { { "zfx_saludo_abrazo_a.gani", "Saludo, choque de manos y abrazo (jugador A)" },
        { "zfx_saludo_abrazo_b.gani", "Saludo, choque de manos y abrazo (jugador B)" } };
    for (const auto& e : kOwn) if (!strcmp(f, e.f)) { snprintf(out, n, "%s", e.es); return; }
    { char* q = strstr(b, ".gani"); if (q) *q = 0; q = strstr(b, "__"); if (q) *q = 0; }
    char* s0 = b; if (!strncmp(s0, "dml_", 4)) s0 += 4;
    std::string o; char* ctx = nullptr;
    for (char* t = strtok_s(s0, "_", &ctx); t; t = strtok_s(nullptr, "_", &ctx)) {
        const size_t L = strlen(t); size_t d = L; while (d > 0 && isdigit((unsigned char)t[d - 1])) d--;
        char key[64]; const size_t kl = std::min(d, sizeof key - 1); memcpy(key, t, kl); key[kl] = 0; const char* dig = t + d;
        const char* w = key; bool found = false;
        if (kl) for (const auto& e : kClipWords) if (!strcmp(e.k, key)) { w = e.es; found = true; break; }
        if (kl && found && !*w && !*dig) continue;
        if (!o.empty()) o += kl ? " \xC2\xB7 " : " ";
        if (kl && *w) o += w;
        if (*dig) { if (kl && *w) o += " "; o += dig; }
    }
    if (!o.empty() && o[0] >= 'a' && o[0] <= 'z') o[0] = (char)(o[0] - 32);
    snprintf(out, n, "%s", o.empty() ? f : o.c_str());
}
static void FaceLabel(int i, char* out, size_t n) {
    static const struct { const char* pre; const char* es; } kF[] = { { "angr", "Enojo" }, { "base", "Base" }, { "bitter", "Amargo" }, { "dml", "Cinem\xC3\xA1tica" }, { "gk", "Arquero" },
        { "head", "Cabezazo" }, { "loss", "Tristeza" }, { "neut", "Neutro" }, { "pain", "Dolor" }, { "pow", "Fuerza" }, { "smil", "Sonrisa" }, { "smug", "Canchero" },
        { "spit", "Escupir" }, { "supris", "Sorpresa" }, { "tire", "Cansancio" } };
    const char* f = MotorFaceName(i); const char* fam = ""; for (const auto& e : kF) { const size_t l = strlen(e.pre); if (!strncmp(f, e.pre, l) && (f[l] == '_' || !f[l])) { fam = e.es; break; } }
    snprintf(out, n, "%s%s%s", TRS(fam), fam[0] ? ":  " : "", f); }
static void KbLabel(const char* t) { snprintf(s_kbLabel, sizeof s_kbLabel, "%s", TRS(t)); s_kbLabelT.store(Now()); }
const char* MotorKeyLabel(double* t) { if (t) *t = s_kbLabelT.load(); return s_kbLabel; }
static const float kBarH = 38.0f, kStatH = 26.0f;
// (15/11 h, Agustin 03:17: "parecen que todo es muy pequeno") tamano de la interfaz del Motor: [PES21FX] motor_escala (de fabrica
// 1.4; se elige en Ver > Tamano de la interfaz o con Ctrl + / Ctrl -). Todo el editor (y el corrimiento del gizmo) usa UiK.
static const float kScales[4] = { 1.0f, 1.2f, 1.4f, 1.6f };
static float UiK(const ImVec2& ds) { return std::max(1.0f, ds.y / 1080.0f) * std::clamp(g_cfg.motorScale, 0.8f, 2.0f); }
static int ScaleIdx() { int b = 0; for (int i = 1; i < 4; i++) if (fabsf(kScales[i] - g_cfg.motorScale) < fabsf(kScales[b] - g_cfg.motorScale)) b = i; return b; }
// (15/11 i) idioma del editor = idioma del menu del mod: se guarda en [PES21FX] language y se releen los textos de las tarjetas
static void LangSet(int i) { static const char* const kKey[3] = { "es", "en", "pt" }; i = std::clamp(i, 0, 3);
    if (i == 3) { g_cfg.langAuto = 1; SaveOption("idioma_auto", "1"); return; }      // como el juego (motorlink.cpp GameLangPoll)
    { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.langAuto = 0; g_cfg.lang = i; LoadMenuLang(); }
    SaveOption("idioma_auto", "0"); SaveOption("language", kKey[i]); }
// nombre de una vista en el idioma elegido ("Normales" ya existe en la tabla con otro sentido: se resuelve aca)
static const char* ViewTr(int row, int op) { if (row == 0 && op == 3 && g_cfg.lang != 0) return g_cfg.lang == 2 ? "Normais" : "Normals"; return TRS(MotorViewName(row, op)); }
static void ScaleSetF(float v) { g_cfg.motorScale = std::clamp(v, 0.8f, 2.0f); char b[16]; snprintf(b, sizeof b, "%.2f", g_cfg.motorScale); SaveOption("motor_escala", b); MenuFontsDirty(); }   // (15/11 p) Preferencias: cualquier tamano
static void ScaleSet(int i) { i = std::clamp(i, 0, 3); g_cfg.motorScale = kScales[i]; char b[16]; snprintf(b, sizeof b, "%.1f", kScales[i]); SaveOption("motor_escala", b); MenuFontsDirty(); }   // la letra del editor se rehace a su tamano
// (15/11 h, Agustin 03:19: "al ingresar a veces aparece el fondo de edicion, podrias agregar una transicion al ingresar y salir")
// al entrar: tapa oscura 0.2 s y fundido de 0.35 s; al salir (boton, menu o Esc): fundido a oscuro de 0.18 s y recien ahi se vuelve.
static double s_enterT = -1.0e9, s_exitT = -1.0, s_coverEnd = -1.0; static bool s_exitSent = false, s_sceneOnce = false;
static std::atomic<int> s_uiWheel{0}; static int s_treeScroll = 0;      // rueda sobre el arbol: corre la lista de luces
// (15/11 g) Agustin 02:47: "separa cada accion en una barra arriba asi como lo hacen programas o menus como blender", "por
// ejemplo abrir. cerrar. crear escena guardar escena. abrir interfaz para cargar ganis"; 02:49: "la vista del motor tiene que
// depender del mouse para acceder a opciones y del teclado" (el mando por ahora no hace falta).
// Barra de MENUS desplegables (Archivo, Escena, Ver, Render, Luces, Ayuda): cada accion del Motor es un renglon con su atajo de
// teclado a la derecha. Mouse: clic en el titulo, pasar por encima cambia de menu, clic afuera cierra. Teclado: F2 abre la
// barra, flechas, Enter, Esc; y los atajos directos (Ayuda > Controles y atajos, F1). En el Motor el juego no ve el teclado
// (freecam.cpp FreeCamBlocksKeys), asi que las letras no mueven nada del juego. Los numeros de arriba (2..8) son de los
// modulos de Sider: aca no se usan.
// ESCENAS: una escena del Motor es un archivo de texto en content\motor-tiempo-real\escenas\ de la carpeta de Sider (si Sider
// no esta, PES21FX\escenas\ junto a la DLL) con la camara, las vistas, la grilla, si esta el jugador y que luces estan
// prendidas. Lo que todavia no existe (animaciones .gani, estadios, cinematicas) figura en gris "en preparacion": no hace nada.
static int s_menuOpen = -1, s_menuKb = -1, s_modal = 0, s_miAct[40], s_miN = 0; static float s_menuA = 0.0f, s_mLastX = 0.0f, s_mLastY = 0.0f;
static std::atomic<bool> s_escOpen{false}; static std::atomic<double> s_escT{-1.0e9}, s_uiAlive{-1.0e9}; static bool s_kWas[256];
static std::wstring s_scDir; static bool s_scDirTried = false; static char s_scName[64] = ""; static char s_scList[14][64]; static int s_scListN = 0;
static char s_note[220] = ""; static double s_noteT = -1.0e9;
struct PendL { int kind; bool on; char name[40]; }; static PendL s_pend[64]; static int s_pendN = 0; static double s_pendUntil = -1.0e9;
struct OrigL { uintptr_t obj; bool on; float i0; }; static OrigL s_orig[64]; static int s_origN = 0;       // como estaba cada luz al entrar al Motor (i0: intensidad = 100 %)
bool MotorUiEscBusy() { return MotorUiOn() && Now() - s_uiAlive.load(std::memory_order_relaxed) < 0.3; }      // con el editor a la vista Esc lo atiende el editor (cierra menu / ventana, o sale con fundido)   // menumouse.cpp: Esc cierra el menu abierto, no el Motor
int MotorCursorKind() { if (!MotorUiOn() || Now() - s_uiAlive.load(std::memory_order_relaxed) > 0.3) return 0; return s_curKind.load(std::memory_order_relaxed); }   // overlay.cpp
static int s_atScene = 1, s_atScroll = 0; static bool s_atBypass = false;    // (06:48) Render > Prueba automatica de shaders: escena del informe, medir sin el mod, fila de arriba de los resultados
static std::string AtUtf8(const std::string& in) {      // los textos de menutest.cpp vienen en Latin-1 o en UTF-8
    bool ok = true; for (size_t i = 0; i < in.size() && ok;) { const unsigned char c = (unsigned char)in[i]; const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > in.size()) { ok = false; break; } for (int q = 1; q < n; q++) if (((unsigned char)in[i + q] >> 6) != 2) ok = false; i += n; }
    if (ok) return in; std::string o; for (unsigned char c : in) { if (c < 0x80) o += (char)c; else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); } } return o; }
static std::atomic<int> s_tlZone{-1}, s_tlWheel{0};
// (17/11 07:50, Agustin: "le falta a la barra de inspector poder usar el mouse") el Inspector se corre con la rueda y con una barra de desplazamiento
static std::atomic<int> s_inZone{-1}, s_inWheel{0}; static float s_inScroll = 0.0f, s_inMax = 0.0f, s_inDragY0 = 0.0f, s_inDragS0 = 0.0f; static bool s_inDrag = false; static int s_inSelWas = -1;   // (06:52) zona de la linea de tiempo: la rueda encima corre las pistas
static std::atomic<int> s_treeZone{-1};      // zona del arbol de escena (la rueda encima corre la lista de luces)
static const int kMenuOrd[7] = { 0, 6, 1, 2, 3, 4, 5 };      // orden de los menus en la barra: Archivo, Editar, Escena, Ver, Render, Luces, Ayuda
static void Note(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(s_note, sizeof s_note, TRS(fmt), ap); va_end(ap); s_noteT = Now(); Log("motor (editor): %s", s_note); }
static const std::wstring& SceneDir() {
    if (s_scDirTried) return s_scDir;
    s_scDirTried = true; std::wstring base;
    if (HMODULE m = GetModuleHandleW(L"sider.dll")) { wchar_t mod[MAX_PATH] = L""; const DWORD n = GetModuleFileNameW(m, mod, MAX_PATH);
        if (n > 0 && n < MAX_PATH) { std::wstring d(mod); const size_t cut = d.find_last_of(L"\\/");
            if (cut != std::wstring::npos) { d = d.substr(0, cut + 1) + L"content\\motor-tiempo-real\\"; if (GetFileAttributesW(d.c_str()) != INVALID_FILE_ATTRIBUTES) base = d; } } }
    if (base.empty()) { base = g_cfg.baseDir + L"PES21FX\\"; CreateDirectoryW(base.c_str(), nullptr); }
    base += L"escenas\\"; CreateDirectoryW(base.c_str(), nullptr);
    if (GetFileAttributesW(base.c_str()) != INVALID_FILE_ATTRIBUTES) s_scDir = base;
    Log("motor (escenas): carpeta %ls", s_scDir.empty() ? L"(no se pudo crear)" : s_scDir.c_str());
    return s_scDir;
}
// (17/11 18:01) favoritos (content\motor-tiempo-real\clips_favoritos.txt, un archivo de clip por linea) y recientes (los ultimos 10, sin guardar)
static std::vector<int> s_fav, s_rec; static bool s_favLoaded = false;
static int ClipByFile(const char* f) { const int n = MotorClipCount(); for (int i = 0; i < n; i++) if (!strcmp(MotorClipFile(i), f)) return i; return -1; }
static void FavLoad();
static void FavSave();
static bool FavHas(int c) { FavLoad(); return std::find(s_fav.begin(), s_fav.end(), c) != s_fav.end(); }
static void FavToggle(int c) { FavLoad(); auto it = std::find(s_fav.begin(), s_fav.end(), c); if (it != s_fav.end()) s_fav.erase(it); else s_fav.push_back(c); FavSave(); }
static void RecPush(int c) { if (c < 0) return; auto it = std::find(s_rec.begin(), s_rec.end(), c); if (it != s_rec.end()) s_rec.erase(it); s_rec.insert(s_rec.begin(), c); if (s_rec.size() > 10) s_rec.resize(10); }
static std::wstring ScenePath(const char* name) { std::wstring f = SceneDir(); for (const char* c = name; *c; c++) f += (wchar_t)(unsigned char)*c; return f; }
static void FavLoad() {
    if (s_favLoaded) return; s_favLoaded = true; const std::wstring& d = SceneDir(); if (d.empty()) return;
    FILE* fp = nullptr; if (_wfopen_s(&fp, (d + L"clips_favoritos.txt").c_str(), L"rb") || !fp) return;
    char ln[200]; while (fgets(ln, sizeof ln, fp)) { size_t l = strlen(ln); while (l && (ln[l - 1] == '\n' || ln[l - 1] == '\r' || ln[l - 1] == ' ')) ln[--l] = 0;
        const int c = ClipByFile(ln); if (c >= 0 && std::find(s_fav.begin(), s_fav.end(), c) == s_fav.end()) s_fav.push_back(c); }
    fclose(fp); }
static void FavSave() {
    const std::wstring& d = SceneDir(); if (d.empty()) return;
    FILE* fp = nullptr; if (_wfopen_s(&fp, (d + L"clips_favoritos.txt").c_str(), L"wb") || !fp) return;
    for (int c : s_fav) fprintf(fp, "%s\n", MotorClipFile(c)); fclose(fp); }
static void SceneLabel(const char* nm, char* out, size_t cap) {
    int Y, M, D, h, m, s;
    if (strcmp(nm, "escena_autoguardado.txt") == 0) { snprintf(out, cap, "%s", TRS("Autoguardado")); return; }
    if (sscanf(nm, "escena_%4d%2d%2d_%2d%2d%2d", &Y, &M, &D, &h, &m, &s) == 6) snprintf(out, cap, TRS("Escena del %02d/%02d/%04d a las %02d:%02d:%02d"), D, M, Y, h, m, s);
    else snprintf(out, cap, "%.60s", nm);
}
// las luces pedidas por la escena abierta: por tipo + nombre (y orden, si hay varias con el mismo nombre)
static void PendApply() {
    for (int j = 0; j < s_pendN; j++) { int occ = 0;
        for (int q = 0; q < j; q++) if (s_pend[q].kind == s_pend[j].kind && strcmp(s_pend[q].name, s_pend[j].name) == 0) occ++;
        for (int i = 0; i < s_nLights; i++) { MotorLight& L = s_lights[i];
            if (L.kind != s_pend[j].kind || strcmp(L.name, s_pend[j].name) != 0) continue;
            if (occ-- > 0) continue;
            if (L.writable && L.on != s_pend[j].on && MotorLightSetOn(L, s_pend[j].on)) L.on = s_pend[j].on;
            break; } }
}
static const char* const kAutoName = "escena_autoguardado.txt";      // (15/11 p) Editar > Preferencias > Autoguardado
static bool SceneWrite(const char* name) {
    if (SceneDir().empty()) { MotorErr("No se pudo guardar la escena: no existe ni se pudo crear la carpeta de escenas (content\\motor-tiempo-real\\escenas en la carpeta de Sider)."); return false; }
    FILE* fp = _wfopen(ScenePath(name).c_str(), L"wb");
    if (!fp) { MotorErr("No se pudo guardar la escena %s: Windows no dej\xC3\xB3 escribir el archivo en la carpeta de escenas.", name); return false; }
    float pos[3] = {}, yaw = 0.0f, pitch = 0.0f; const bool cf = FreeCamGetPose(pos, &yaw, &pitch) && g_cfg.live[344][0] > 0.5f;
    fprintf(fp, "escena del Motor en tiempo real 1\r\n");
    fprintf(fp, "camara %d %.4f %.4f %.4f %.5f %.5f\r\n", cf ? 1 : 0, pos[0], pos[1], pos[2], yaw, pitch);
    fprintf(fp, "vista %d %d %d\r\n", MotorViewGet(0), MotorViewGet(1), MotorViewGet(2));
    fprintf(fp, "grilla %d\r\njugador %d\r\n", g_cfg.motorGrid != 0 ? 1 : 0, MotorUiPlayer() ? 1 : 0);
    for (int i = 0; i < s_nLights; i++) fprintf(fp, "luz %d %d %s\r\n", s_lights[i].on ? 1 : 0, s_lights[i].kind, s_lights[i].name);
    fclose(fp); return true;
}
static bool SceneSave(bool asNew) {
    if (asNew || !s_scName[0] || strcmp(s_scName, kAutoName) == 0) { SYSTEMTIME t; GetLocalTime(&t); snprintf(s_scName, sizeof s_scName, "escena_%04d%02d%02d_%02d%02d%02d.txt", (int)t.wYear, (int)t.wMonth, (int)t.wDay, (int)t.wHour, (int)t.wMinute, (int)t.wSecond); }
    if (!SceneWrite(s_scName)) return false;
    char lb[96]; SceneLabel(s_scName, lb, sizeof lb); Note("Guardada: %s", lb);
    return true;
}
static bool SceneLoad(const char* name) {
    FILE* fp = SceneDir().empty() ? nullptr : _wfopen(ScenePath(name).c_str(), L"rb");
    if (!fp) { MotorErr("No se pudo abrir la escena %s: el archivo no est\xC3\xA1 o no se puede leer.", name); return false; }
    char ln[200]; bool head = false, hasCam = false; int cf = 0, v[3] = { 0, 0, 0 }, gr = 1, pl = 0, np = 0; float p[3] = {}, yaw = 0.0f, pitch = 0.0f; static PendL pend[64];
    while (fgets(ln, sizeof ln, fp)) {
        size_t n = strlen(ln); while (n > 0 && (ln[n - 1] == '\r' || ln[n - 1] == '\n')) ln[--n] = 0;
        if (!head) { if (strncmp(ln, "escena del Motor en tiempo real", 31) != 0) break; head = true; continue; }
        int a = 0, b = 0, off = 0;
        if (sscanf(ln, "camara %d %f %f %f %f %f", &cf, &p[0], &p[1], &p[2], &yaw, &pitch) == 6) hasCam = true;
        else if (sscanf(ln, "vista %d %d %d", &v[0], &v[1], &v[2]) == 3) {}
        else if (sscanf(ln, "grilla %d", &gr) == 1) {}
        else if (sscanf(ln, "jugador %d", &pl) == 1) {}
        else if (sscanf(ln, "luz %d %d %n", &a, &b, &off) >= 2 && off > 0 && np < 64 && b >= 0 && b < 5) { pend[np].on = a != 0; pend[np].kind = b; snprintf(pend[np].name, sizeof pend[np].name, "%.39s", ln + off); np++; }
    }
    fclose(fp);
    if (!head) { MotorErr("El archivo %s no es una escena del Motor: le falta la primera l\xC3\xADnea (\"escena del Motor en tiempo real\").", name); return false; }
    for (int r = 0; r < 3; r++) if (v[r] >= 0 && v[r] < MotorViewCount(r)) MotorViewSet(r, v[r]);
    g_cfg.motorGrid = gr != 0 ? 1 : 0;
    if ((pl != 0) != MotorUiPlayer()) MotorUiTogglePlayer();
    const bool fin = std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]) && std::isfinite(yaw) && std::isfinite(pitch);
    if (hasCam && cf != 0 && fin) FreeCamSetPose(p, yaw, pitch); else if (hasCam && cf == 0) SetCam(false);
    memcpy(s_pend, pend, sizeof(PendL) * (size_t)np); s_pendN = np; s_pendUntil = Now() + 3.0; PendApply();
    snprintf(s_scName, sizeof s_scName, "%.63s", name); char lb[96]; SceneLabel(s_scName, lb, sizeof lb); Note("Abierta: %s", lb);
    return true;
}
static void SceneList() {
    s_scListN = 0; if (SceneDir().empty()) return;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((SceneDir() + L"escena_*.txt").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return;
    static char all[64][64]; int n = 0;
    do { if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || n >= 64) continue;
         int q = 0; bool ok = true; for (; fd.cFileName[q] && q < 63; q++) { if (fd.cFileName[q] < 32 || fd.cFileName[q] > 126) { ok = false; break; } all[n][q] = (char)fd.cFileName[q]; }
         all[n][q] = 0; if (ok && !fd.cFileName[q]) n++; } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (int i = 0; i < n && s_scListN < 14; i++) { int best = -1; for (int j = 0; j < n; j++) if (all[j][0] && (best < 0 || strcmp(all[j], all[best]) > 0)) best = j;      // las mas nuevas primero
        if (best < 0) break; memcpy(s_scList[s_scListN++], all[best], 64); all[best][0] = 0; }
}
static const char* const kHelp[][2] = {
    { "Clic izquierdo", "elegir en los men\xC3\xBAs, el \xC3\xA1rbol de escena y el inspector" },
    { "Bot\xC3\xB3n derecho", "mantenerlo apretado: mirar y volar con W A S D, Q y E" },
    { "Bot\xC3\xB3n del medio", "panear" },
    { "Alt + clic izquierdo", "orbitar alrededor del centro" },
    { "Rueda", "acercar y alejar" },
    { "F2", "abrir la barra de men\xC3\xBAs (flechas, Enter, Esc)" },
    { "Ctrl + N  /  O  /  S", "escena nueva  /  abrir  /  guardar" },
    { "Ctrl + Shift + S", "guardar como escena nueva" },
    { "Ctrl + Z  /  Ctrl + Y", "deshacer  /  rehacer" },
    { "Ctrl + ,", "preferencias" },
    { "G  /  J  /  C", "grilla  /  jugador  /  c\xC3\xA1mara libre o del juego" },
    { "V  /  Shift + V", "vista del render siguiente  /  anterior" },
    { "Z", "alambre: no, s\xC3\xAD, con teselaci\xC3\xB3n" },
    { "L  /  Shift + L", "prender o apagar todas las luces  /  como estaban" },
    { "Num 1  /  3  /  7", "mirar de frente  /  derecha  /  arriba (con Ctrl, el lado opuesto)" },
    { "Num 0", "volver a la pose inicial de la c\xC3\xA1mara" },
    { "Num 5", "perspectiva u ortogr\xC3\xA1" "fica" },
    { "X", "vista extra con otra c\xC3\xA1mara (en su ventanita: arrastrar = girar, Shift = acercar)" },
    { "Ctrl +  /  Ctrl -", "agrandar  /  achicar la interfaz" },
    { "T  /  N", "mostrar u ocultar el panel Escena  /  Inspector" },
    { "Borde de un panel", "arrastrarlo cambia su ancho" },
    { "T\xC3\xADtulo de un panel", "arrastrarlo lo mueve: a un costado, apilado con el otro o flotante" },
    { "F1", "esta ayuda" },
    { "Esc", "cerrar el men\xC3\xBA abierto o salir del Motor" },
};
static const int kHelpN = (int)(sizeof kHelp / sizeof kHelp[0]);
// (15/11 k, Agustin 04:09: "cuadrados deslizandose a la derecha o izquierda dependiendo si se entra o sale, como si fuera un efecto
// glitch"; 04:11: "una animacion dinamica, y con estilo") TAPA DE BLOQUES. Vista previa con las mismas cuentas:
// scratchpad glitch/prev.py -> /mnt/project-files/motor/transicion_glitch_v2.mp4. La pantalla se parte en 14 filas de alto desparejo;
// cada fila se mueve con su atraso (barrido en diagonal + azar fijo por fila) y una curva suave. a = cuanto tapa (0..1).
// side -1 = la tapa esta pegada a la izquierda (su borde avanza hacia la derecha al subir a); +1 = pegada a la derecha.
//   entrar: llega con side -1 y se destapa con side +1 (todo corre hacia la derecha);
//   salir:  llega con side +1 y se destapa con side -1 (todo corre hacia la izquierda).
// En el borde: astillas de la fila (dos tiras por fila, faltantes, ancho que cambia 24 veces por segundo, alguna verde o lima),
// una linea verde con su fantasma lima (separacion de color) y "desgarros" (el borde salta un poco). Adentro: lineas de barrido.
// Con todo tapado: el logo del menu con separacion de color, "MOTOR EN TIEMPO REAL" y una barra con lo que lleva cargado.
// Suena MotorGlitchSound (motorsound.cpp): uno al entrar y otro mas grave al salir.
static inline float Hash1(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return (float)(x & 0xffffff) / 16777215.0f; }
static inline float EaseIO(float p) { return p < 0.5f ? 4.0f * p * p * p : 1.0f - (-2.0f * p + 2.0f) * (-2.0f * p + 2.0f) * (-2.0f * p + 2.0f) * 0.5f; }
// barra de carga de la tapa: MotorCoverLoad(objetivo 0..1, etapa) en cada cuadro antes de MotorGlitchCover; lo mostrado se acerca
// suave al objetivo, nunca vuelve atras dentro de una tapa y arranca de 0 en una tapa nueva (objetivo mas bajo o 0.5 s sin pedidos)
static float s_cvShown = 0.0f; static int s_cvStage = 0; static double s_cvLoadT = -1.0e9;
// (15/11 n, Agustin 05:04: "la barra de carga es poco precisa") la barra avanza con el TIEMPO que tarda de verdad cada paso, aprendido de
// las entradas anteriores (se guarda en [PES21FX] motor_carga_tiempos): pasos 0 pagina del Motor abierta, 1 pagina armada (MotorUiReady),
// 2 escena leida, 3 iconos, 4 destapado; 5 = salir hasta que vuelve el Editar. Entre un paso y el siguiente se llena en proporcion al
// tiempo esperado y nunca pasa al siguiente sin que ocurra; al ocurrir salta a su lugar. Asi el 50 % es la mitad del tiempo real.
static double s_ldT0 = -1.0e9, s_ldHit[5] = {}; static float s_ldE[6] = { 0.35f, 0.70f, 0.78f, 0.82f, 0.90f, 0.45f }; static bool s_ldRead = false;
static void LdRead() { if (s_ldRead) return; s_ldRead = true; float v[6];
    if (sscanf(g_cfg.motorLoadTimes.c_str(), "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) for (int i = 0; i < 6; i++) if (v[i] > 0.02f && v[i] < 5.0f) s_ldE[i] = v[i];
    for (int i = 1; i < 5; i++) s_ldE[i] = std::max(s_ldE[i], s_ldE[i - 1] + 0.02f); }
static void LdSave() { char b[96]; snprintf(b, sizeof b, "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f", s_ldE[0], s_ldE[1], s_ldE[2], s_ldE[3], s_ldE[4], s_ldE[5]); SaveOption("motor_carga_tiempos", b); }
static void LdStart(double now) { LdRead(); s_ldT0 = now; for (double& h : s_ldHit) h = 0.0; }
static void LdHit(int i, double now) { if (s_ldHit[i] != 0.0) return; s_ldHit[i] = now;      // (15/11 q) una sola vez por entrada: antes el paso 4 se anotaba y guardaba el ini en cada cuadro
    if (i == 4) { for (int q = 0; q < 5; q++) if (s_ldHit[q] > 0.0) s_ldE[q] = 0.6f * s_ldE[q] + 0.4f * (float)(s_ldHit[q] - s_ldT0);
        for (int q = 1; q < 5; q++) s_ldE[q] = std::max(s_ldE[q], s_ldE[q - 1] + 0.02f); LdSave();
        Log("motor (editor): carga en %.2f s (pagina %.2f, armada %.2f, escena %.2f, iconos %.2f)", (float)(now - s_ldT0), s_ldHit[0] > 0 ? (float)(s_ldHit[0] - s_ldT0) : -1.0f,
            s_ldHit[1] > 0 ? (float)(s_ldHit[1] - s_ldT0) : -1.0f, s_ldHit[2] > 0 ? (float)(s_ldHit[2] - s_ldT0) : -1.0f, s_ldHit[3] > 0 ? (float)(s_ldHit[3] - s_ldT0) : -1.0f); } }
static float LdProgress(double now, int* stage) {
    int k = -1; for (int i = 0; i < 5; i++) if (s_ldHit[i] > 0.0) k = i;
    if (stage) *stage = std::clamp(k + 1, 0, 4);
    if (k >= 4) return 1.0f;
    const float Ed = s_ldE[4], lo = k >= 0 ? s_ldE[k] / Ed : 0.0f, hi = s_ldE[k + 1] / Ed;
    const float t0 = k >= 0 ? (float)(s_ldHit[k] - s_ldT0) : 0.0f, span = std::max(0.05f, s_ldE[k + 1] - (k >= 0 ? s_ldE[k] : 0.0f));
    const float f = std::clamp(((float)(now - s_ldT0) - t0) / span, 0.0f, 1.0f);
    return std::min(hi - 0.01f, lo + (hi - lo) * f);
}
static void MotorCoverLoad(float tgt, int stage) {
    const double now = Now(); const float dt = (float)std::min(0.1, std::max(0.0, now - s_cvLoadT));
    if (now - s_cvLoadT > 0.5 || tgt < s_cvShown - 0.3f) s_cvShown = 0.0f;
    s_cvLoadT = now; s_cvStage = stage;
    if (tgt > s_cvShown) s_cvShown = std::min(tgt, s_cvShown + (tgt - s_cvShown) * std::min(1.0f, dt * (tgt >= 0.999f ? 18.0f : 12.0f)));
}
// (15/11 n, Agustin 05:03: "algunas partes del menu se colan en la pantalla de carga al entrar y salir") la tapa se dibuja al FINAL
// del cuadro (MotorCoverFlush, overlay.cpp antes de ImGui::Render): el gizmo de camara, la ayuda del mouse, el cursor y los avisos que
// se dibujan despues del editor ya no quedan encima. MotorGlitchCover solo anota lo pedido en este cuadro.
static float s_cvPendA = 0.0f, s_cvPendSide = 1.0f; static bool s_cvPend = false;
void MotorGlitchCover(float a, float side) { if (!s_cvPend || a > s_cvPendA) { s_cvPendA = a; s_cvPendSide = side; } s_cvPend = true; }
static void GlitchCoverDraw(float a, float side) {
    a = std::clamp(a, 0.0f, 1.0f); if (a <= 0.001f) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList(); const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float k = ds.y / 540.0f;                                         // las medidas de la vista previa son para 960 x 540
    const ImU32 cBg = IM_COL32(10, 19, 14, 255);                      // (15/11 m, Agustin 04:20: "colores verdes")
    const double now = Now(); const uint32_t tick = (uint32_t)(now * 24.0);
    const int kRows = 14; const float kD = 0.36f;
    float ys[kRows + 1]; { float w[kRows], sum = 0.0f; for (int r = 0; r < kRows; r++) { w[r] = 0.6f + 0.8f * Hash1(0x9e37u + (uint32_t)r * 2654435761u); sum += w[r]; }
        ys[0] = 0.0f; for (int r = 0; r < kRows; r++) ys[r + 1] = ys[r] + w[r] / sum * ds.y; }
    for (int r = 0; r < kRows; r++) {
        const float y0 = ys[r], y1 = ys[r + 1], rh = y1 - y0;
        const float dly = kD * (0.62f * ((float)r / (float)(kRows - 1)) + 0.38f * Hash1(0x51u + (uint32_t)r * 7919u));
        const float p = EaseIO(std::clamp(a * (1.0f + kD) - dly, 0.0f, 1.0f));
        if (p <= 0.0f) continue;
        const float mot = 4.0f * p * (1.0f - p);
        float tear = 0.0f;
        if (p < 1.0f && Hash1((uint32_t)r * 977u + tick * 131u) > 0.86f) tear = (Hash1((uint32_t)r * 31u + tick * 7u) - 0.5f) * 2.0f * rh * 1.6f;
        const float xe = (side < 0.0f ? ds.x * p : ds.x * (1.0f - p)) + tear * mot;
        const float x0 = side < 0.0f ? 0.0f : xe, x1 = side < 0.0f ? xe : ds.x;
        if (x1 > x0) {
            dl->AddRectFilled(ImVec2(std::max(0.0f, x0), y0), ImVec2(std::min(ds.x, x1), y1 + 0.5f), cBg);
            for (float yy = y0 + 2.0f * k; yy < y1; yy += 4.0f * k) dl->AddLine(ImVec2(std::max(0.0f, x0), yy), ImVec2(std::min(ds.x, x1), yy), IM_COL32(120, 255, 170, 10), 1.0f);
        }
        if (p < 1.0f) {
            const float dir = side < 0.0f ? 1.0f : -1.0f;                   // hacia donde queda lo destapado
            for (int sub = 0; sub < 2; sub++) {
                const float sy0 = y0 + rh * 0.5f * (float)sub, sy1 = sy0 + rh * 0.5f; float x = xe;
                for (int q = 0; q < 4; q++) {
                    const float h = Hash1((uint32_t)r * 131u + (uint32_t)sub * 7717u + (uint32_t)q * 17u + tick * 977u);
                    const float bw = rh * (0.5f + 1.8f * Hash1((uint32_t)r * 53u + (uint32_t)sub * 11u + (uint32_t)q * 3u + tick * 3u)) * (0.4f + 0.6f * mot);
                    x += dir * rh * 0.35f * Hash1((uint32_t)r * 71u + (uint32_t)q + (uint32_t)sub * 5u + tick * 5u);
                    if (h < 0.30f + 0.17f * (float)q) { x += dir * bw; continue; }
                    const ImU32 c = h > 0.955f ? IM_COL32(52, 232, 128, (int)(215.0f * mot + 40.0f)) : h > 0.925f ? IM_COL32(186, 255, 92, (int)(120.0f * mot + 20.0f)) : cBg;
                    const float xa = std::min(x, x + dir * bw), xb = std::max(x, x + dir * bw);
                    dl->AddRectFilled(ImVec2(xa, sy0), ImVec2(xb, sy1), c);
                    x += dir * bw;
                }
            }
            const int al = (int)(235.0f * mot);
            dl->AddRectFilled(ImVec2(xe - 1.5f * k, y0), ImVec2(xe + 1.5f * k, y1), IM_COL32(52, 232, 128, al));
            const float gx = xe - dir * 6.0f * k;
            dl->AddRectFilled(ImVec2(gx - 1.0f, y0), ImVec2(gx + 1.0f, y1), IM_COL32(186, 255, 92, (int)((float)al * 0.55f)));
        }
    }
    const float la = std::clamp((a - 0.86f) / 0.14f, 0.0f, 1.0f);
    if (la > 0.0f) {
        const float cx = ds.x * 0.5f, cy = ds.y * 0.5f - 14.0f * k;
        const float j = Hash1(tick * 11u) > 0.7f ? (Hash1(tick * 3u + 1u) - 0.5f) * 6.0f * k : 0.0f;
        float asp = 1.0f; void* lg = MenuLogoTex(&asp); ImFont* font = MotorFont(0);
        if (lg && asp > 0.05f) {
            const float lh = 70.0f * k, lw = std::min(lh * asp, ds.x * 0.5f); const ImVec2 p0(cx - lw * 0.5f, cy - lh * 0.5f - 6.0f * k), p1(p0.x + lw, p0.y + lh);
            dl->AddImage((ImTextureID)(uintptr_t)lg, ImVec2(p0.x - 3.0f * k + j, p0.y), ImVec2(p1.x - 3.0f * k + j, p1.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(186, 255, 92, (int)(110.0f * la)));
            dl->AddImage((ImTextureID)(uintptr_t)lg, ImVec2(p0.x + 3.0f * k - j, p0.y), ImVec2(p1.x + 3.0f * k - j, p1.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(52, 232, 128, (int)(130.0f * la)));
            dl->AddImage((ImTextureID)(uintptr_t)lg, p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(255.0f * la)));
        } else {
            const char* t = "NEM MOTOR"; const float fsz = 40.0f * k; const ImVec2 ts = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, t); const ImVec2 o(cx - ts.x * 0.5f, cy - ts.y * 0.5f - 6.0f * k);
            dl->AddText(font, fsz, ImVec2(o.x - 3.0f * k + j, o.y), IM_COL32(186, 255, 92, (int)(110.0f * la)), t);
            dl->AddText(font, fsz, ImVec2(o.x + 3.0f * k - j, o.y), IM_COL32(52, 232, 128, (int)(130.0f * la)), t);
            dl->AddText(font, fsz, o, IM_COL32(235, 235, 238, (int)(255.0f * la)), t);
        }
        { const char* t = TRS("MOTOR EN TIEMPO REAL"); ImFont* fl = MotorFont(2); const float fsz = 16.0f * k; const ImVec2 ts = fl->CalcTextSizeA(fsz, FLT_MAX, 0.0f, t);
          dl->AddText(fl, fsz, ImVec2(cx - ts.x * 0.5f, cy + 44.0f * k - ts.y * 0.5f), IM_COL32(190, 255, 215, (int)(255.0f * la)), t); }   // letra de contorno (Lack UA Line)
        // (15/11 m, Agustin 04:20: "una barra de cuanto lleva cargado") la barra sigue la carga real (MotorCoverLoad): boton aceptado,
        // pagina armada, escena leida, iconos; abajo la etapa y el porcentaje
        const float bw = 220.0f * k, bh = 4.0f * k, by = cy + 62.0f * k, bx0 = cx - bw * 0.5f; const float u = s_cvShown;
        dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + bw, by + bh), IM_COL32(22, 48, 33, (int)(255.0f * la)), 2.0f * k);
        const float fx = bx0 + bw * std::clamp(u, 0.0f, 1.0f);
        if (fx > bx0 + 0.5f) {
            dl->AddRectFilled(ImVec2(bx0, by - 2.0f * k), ImVec2(fx, by + bh + 2.0f * k), IM_COL32(52, 232, 128, (int)(38.0f * la)), 3.0f * k);
            dl->AddRectFilled(ImVec2(bx0, by), ImVec2(fx, by + bh), IM_COL32(52, 232, 128, (int)(255.0f * la)), 2.0f * k);
            const float sx = bx0 + (fx - bx0) * (float)std::fmod(now * 0.85, 1.0);          // brillo que corre por lo cargado
            if (std::min(fx, sx + 16.0f * k) > std::max(bx0, sx - 16.0f * k))
                dl->AddRectFilled(ImVec2(std::max(bx0, sx - 16.0f * k), by), ImVec2(std::min(fx, sx + 16.0f * k), by + bh), IM_COL32(214, 255, 228, (int)(110.0f * la)), 2.0f * k);
            dl->AddRectFilled(ImVec2(fx - 1.5f * k, by - 3.0f * k), ImVec2(fx + 1.5f * k, by + bh + 3.0f * k), IM_COL32(214, 255, 228, (int)(235.0f * la)));
        }
        { static const char* const kSt[6] = { "Abriendo el Motor", "Armando la interfaz", "Leyendo la escena", "Cargando los iconos", "Listo", "Volviendo al Editar" };
          const float fsz = 10.0f * k; const char* t = TRS(kSt[std::clamp(s_cvStage, 0, 5)]);
          dl->AddText(font, fsz, ImVec2(bx0, by + bh + 6.0f * k), IM_COL32(130, 196, 158, (int)(230.0f * la)), t);
          char pc[16]; snprintf(pc, sizeof pc, "%d %%", (int)(std::clamp(u, 0.0f, 1.0f) * 100.0f + 0.5f));
          const ImVec2 ts = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, pc);
          dl->AddText(font, fsz, ImVec2(bx0 + bw - ts.x, by + bh + 6.0f * k), IM_COL32(52, 232, 128, (int)(255.0f * la)), pc); }
    }
}
void MotorCoverFlush() { if (!s_cvPend) return; s_cvPend = false; GlitchCoverDraw(s_cvPendA, s_cvPendSide); }
// estado de la tapa fuera del editor: antes de que abra (se acepto el boton 12) y despues de cerrarlo (hasta que vuelve el Editar)
static std::atomic<double> s_cvArriveT{ -1.0e9 }, s_cvCancelT{ -1.0e9 }, s_cvExitT{ -1.0e9 }, s_cvRevT{ -1.0e9 };
static double s_sndEnterT = -1.0e9;
static void EnterSoundOnce() { const double n = Now(); if (n - s_sndEnterT > 3.0) { s_sndEnterT = n; MotorGlitchSound(0); } }
void MotorCoverArrive() { s_cvArriveT.store(Now()); s_cvCancelT.store(-1.0e9); EnterSoundOnce(); LdStart(Now()); }
void MotorCoverCancel() { const double t = Now() - s_cvArriveT.load(); if (t >= 0.0 && t < 2.8) { s_cvArriveT.store(-1.0e9); s_cvCancelT.store(Now()); } }
bool MotorCoverArriving() { const double t = Now() - s_cvArriveT.load(); return t >= 0.0 && t < 2.8; }
// overlay.cpp, cada cuadro, antes del editor (con el editor a la vista la tapa la dibuja ToolbarDraw)
void MotorPreCoverDraw() {
    if (g_cfg.motorOwnMenu == 0 || g_cfg.motorPage == 0) return;
    MotorPreCoverPoll();                                                   // motorpage.cpp: se acepto el boton 12?
    if (MotorUiOn()) return;
    const double now = Now();
    if (now - s_cvExitT.load() < 3.0) {                                    // despues de salir: tapado hasta que vuelve el menu de Editar
        if (s_cvRevT.load() < s_cvExitT.load() && (MotorEditMenuSeenT() > s_cvExitT.load() + 0.05 || now - s_cvExitT.load() > 2.0)) { s_cvRevT.store(now);
            if (now - s_cvExitT.load() < 2.0) { s_ldE[5] = 0.6f * s_ldE[5] + 0.4f * (float)(now - s_cvExitT.load()); LdSave(); } }
        const float a = s_cvRevT.load() < s_cvExitT.load() ? 1.0f : 1.0f - (float)(now - s_cvRevT.load()) / 0.5f;
        const bool back = s_cvRevT.load() >= s_cvExitT.load();
        MotorCoverLoad(back ? 1.0f : std::min(0.97f, (0.38f + (float)(now - s_cvExitT.load())) / (0.38f + s_ldE[5])), back ? 4 : 5);
        if (a > 0.0f) { MotorGlitchCover(a, -1.0f); return; }
    }
    { const double t = now - s_cvArriveT.load();                           // antes de entrar: llega desde la izquierda
      if (t >= 0.0 && t < 2.8) { float a = EaseIO(std::min(1.0f, (float)t / 0.32f)); if (t > 2.5) a = std::max(0.0f, 1.0f - (float)(t - 2.5) / 0.3f);
        int st = 0; const float pr = LdProgress(now, &st); MotorCoverLoad(pr, st); MotorGlitchCover(a, -1.0f); return; } }
    { const double t = now - s_cvCancelT.load();                           // no era el Motor: se retira por donde vino
      if (t >= 0.0 && t < 0.3) MotorGlitchCover(1.0f - (float)t / 0.3f, -1.0f); }
}
// ---------------------------------------------------------------- (15/11 n) deshacer / rehacer
// Agustin 05:08: "le faltan atajos de ctrl+z y ctrl+y". Cada accion del editor (menus, teclas, clics en la escena) guarda como estaba la
// escena antes: vistas, grilla (prendida y tamano), jugador y luces prendidas / apagadas. Ctrl+Z vuelve un paso, Ctrl+Y (o Ctrl+Shift+Z)
// lo rehace. La camara no entra (como en Blender). Hasta 40 pasos (15/11 p: de 10 a 200, en Editar > Preferencias); una accion nueva borra lo que habia para rehacer.
// (15/11 o) tambien lo que se cambia en el inspector: color, intensidad, sombra, alcance y posicion de cada luz, y las opciones de la
// grilla. Un arrastre entero (de un campo o de un slider) es UN paso.
struct UndoS { int v[3]; int grid, gridSize, gAlpha, gMajor, gAxes; float gCell; bool player; int nl; uintptr_t obj[64]; bool on[64], sh[64]; float col[64][4], pos[64][3], rng[64];
    bool pjOk[kMotorPjMax], pjHid[kMotorPjMax]; float pjT[kMotorPjMax][5]; };      // (17/11 17:22, Agustin: "el ctrl+z deberia ser capaz de deshacer cambios mover escala etc") jugadores propios: lugar, giro, tamano, oculto
static const int kUndoMax = 200;      // (15/11 p) Editar > Preferencias: de 10 a 200 pasos ([PES21FX] motor_deshacer_pasos, 40 de fabrica)
static UndoS s_undo[kUndoMax], s_redo[kUndoMax]; static int s_undoN = 0, s_redoN = 0;
static void GridSave() {      // (15/11 o) opciones de la grilla al ini
    char b[24]; snprintf(b, sizeof b, "%d", g_cfg.motorGridAlpha); SaveOption("motor_grilla_intensidad", b); snprintf(b, sizeof b, "%d", g_cfg.motorGridMajor); SaveOption("motor_grilla_linea_fuerte", b);
    SaveOption("motor_grilla_ejes", g_cfg.motorGridAxes ? "1" : "0"); snprintf(b, sizeof b, "%.2f", g_cfg.motorGridCell); SaveOption("motor_grilla_celda", b); snprintf(b, sizeof b, "%d", g_cfg.motorGridSize); SaveOption("motor_grilla_tamano", b); }
static void UndoSnap(UndoS& u) {
    for (int r = 0; r < 3; r++) u.v[r] = MotorViewGet(r);
    u.grid = g_cfg.motorGrid; u.gridSize = g_cfg.motorGridSize; u.player = MotorUiPlayer();
    u.gAlpha = g_cfg.motorGridAlpha; u.gMajor = g_cfg.motorGridMajor; u.gAxes = g_cfg.motorGridAxes; u.gCell = g_cfg.motorGridCell;
    u.nl = s_nLights; for (int i = 0; i < s_nLights; i++) { const MotorLight& L = s_lights[i]; u.obj[i] = L.obj; u.on[i] = L.on; u.sh[i] = L.shadow;
        memcpy(u.col[i], L.color, sizeof u.col[i]); memcpy(u.pos[i], L.pos, sizeof u.pos[i]); u.rng[i] = L.range; }
    static MotorPjInfo PI; MotorPjInfoGet(&PI);
    for (int i = 0; i < kMotorPjMax; i++) { float* t = u.pjT[i]; u.pjOk[i] = MotorPjGet(i, t, &t[3], &t[4]); u.pjHid[i] = PI.hidden[i]; if (!u.pjOk[i]) memset(t, 0, sizeof u.pjT[i]); }
}
static bool UndoSame(const UndoS& a, const UndoS& b) {
    if (a.v[0] != b.v[0] || a.v[1] != b.v[1] || a.v[2] != b.v[2] || a.grid != b.grid || a.gridSize != b.gridSize || a.player != b.player) return false;
    if (a.gAlpha != b.gAlpha || a.gMajor != b.gMajor || a.gAxes != b.gAxes || a.gCell != b.gCell) return false;
    for (int i = 0; i < kMotorPjMax; i++) if (a.pjOk[i] && b.pjOk[i] && (a.pjHid[i] != b.pjHid[i] || memcmp(a.pjT[i], b.pjT[i], sizeof a.pjT[i]) != 0)) return false;
    for (int i = 0; i < a.nl; i++) for (int j = 0; j < b.nl; j++) if (a.obj[i] == b.obj[j]) {
        if (a.on[i] != b.on[j] || a.sh[i] != b.sh[j] || a.rng[i] != b.rng[j] || memcmp(a.col[i], b.col[j], sizeof a.col[i]) != 0 || memcmp(a.pos[i], b.pos[j], sizeof a.pos[i]) != 0) return false; }
    return true;
}
static int UndoLim() { return std::clamp(g_cfg.motorUndoSteps, 10, kUndoMax); }
static void UndoPush(UndoS* st, int& n, const UndoS& u) { const int lim = UndoLim(); if (n >= lim) { const int drop = n - lim + 1; memmove(st, st + drop, sizeof(UndoS) * (size_t)(n - drop)); n -= drop; } st[n++] = u; s_actCount++; }
static void UndoApply(const UndoS& u) {
    for (int r = 0; r < 3; r++) if (u.v[r] >= 0 && u.v[r] < MotorViewCount(r) && MotorViewGet(r) != u.v[r]) MotorViewSet(r, u.v[r]);
    g_cfg.motorGrid = u.grid; g_cfg.motorGridSize = u.gridSize;
    if (g_cfg.motorGridAlpha != u.gAlpha || g_cfg.motorGridMajor != u.gMajor || g_cfg.motorGridAxes != u.gAxes || g_cfg.motorGridCell != u.gCell) {
        g_cfg.motorGridAlpha = u.gAlpha; g_cfg.motorGridMajor = u.gMajor; g_cfg.motorGridAxes = u.gAxes; g_cfg.motorGridCell = u.gCell; GridSave(); }
    if (u.player != MotorUiPlayer()) MotorUiTogglePlayer();
    {   static MotorPjInfo PI; MotorPjInfoGet(&PI);      // jugadores que siguen en la escena: lugar, giro, tamano y oculto como estaban
        for (int i = 0; i < kMotorPjMax; i++) { if (!u.pjOk[i]) continue; float p[3], y, sc; if (!MotorPjGet(i, p, &y, &sc)) continue;
            MotorPjSet(i, u.pjT[i], u.pjT[i][3], u.pjT[i][4]); if (PI.hidden[i] != u.pjHid[i]) MotorPjShow(i, !u.pjHid[i]); } }
    int bad = 0;
    for (int i = 0; i < s_nLights; i++) { MotorLight& L = s_lights[i];
        for (int j = 0; j < u.nl; j++) if (u.obj[j] == L.obj) {
            if (u.on[j] != L.on) { if (L.writable && MotorLightSetOn(L, u.on[j])) L.on = u.on[j]; else bad++; }
            if (memcmp(u.col[j], L.color, sizeof L.color) != 0) { if (MotorLightSetColor(L, u.col[j])) memcpy(L.color, u.col[j], sizeof L.color); else bad++; }
            if (u.sh[j] != L.shadow) { if (MotorLightSetShadow(L, u.sh[j])) L.shadow = u.sh[j]; else bad++; }
            if (u.rng[j] != L.range && MotorLightCan(L, 2)) { if (MotorLightSetRange(L, u.rng[j])) L.range = u.rng[j]; else bad++; }
            if (memcmp(u.pos[j], L.pos, sizeof L.pos) != 0 && MotorLightCan(L, 3)) { if (MotorLightSetPos(L, u.pos[j])) memcpy(L.pos, u.pos[j], sizeof L.pos); else bad++; } } }
    if (bad) MotorErr("%d luces no se pudieron cambiar: son de una clase que el Motor no conoce o el juego las sac\xC3\xB3 de la escena mientras tanto.", bad);
}
static void UndoStep(bool redo) {
    UndoS* from = redo ? s_redo : s_undo; int& fn = redo ? s_redoN : s_undoN; UndoS* to = redo ? s_undo : s_redo; int& tn = redo ? s_undoN : s_redoN;
    if (fn == 0) { Note(redo ? "No hay nada para rehacer" : "No hay nada para deshacer"); return; }
    UndoS now; UndoSnap(now); UndoPush(to, tn, now); UndoApply(from[--fn]);
    Note(redo ? "Rehecho (quedan %d)" : "Deshecho (quedan %d)", fn);
}
// (15/11 o) un paso de deshacer que dura varios cuadros (arrastrar un campo o un slider, escribir un numero)
static UndoS s_ub; static bool s_ubOn = false, s_ubEndPending = false;
static void UndoBegin() { if (!s_ubOn) { UndoSnap(s_ub); s_ubOn = true; } }
static void UndoEnd() { if (!s_ubOn) return; s_ubOn = false; UndoS now; UndoSnap(now); if (!UndoSame(s_ub, now)) { UndoPush(s_undo, s_undoN, s_ub); s_redoN = 0; } }
// (17/11 17:24, Agustin: "apretar shift y mantener para seleccionar muchos") VARIOS ELEGIDOS: Mayus + clic (en la lista o en la vista)
// suma o saca un jugador; clic solo deja uno. El elegido principal (MotorPjSel) lleva los iconos y el Inspector; lo que se le hace a el
// (mover, girar, tamano) se les hace a los demas elegidos con la misma diferencia.
static uint32_t s_pjMulti = 0; static bool s_kbUndo = false;
static void PjPick(int q, bool add) {
    if (q < 0 || q >= kMotorPjMax) { s_pjMulti = 0; MotorPjSelect(-1); return; }
    const uint32_t b = 1u << q;
    if (!add) { s_pjMulti = b; MotorPjSelect(q); return; }
    if (MotorPjSel() >= 0) s_pjMulti |= 1u << MotorPjSel();
    if ((s_pjMulti & b) && MotorPjSel() == q) { s_pjMulti &= ~b; int nx = -1; for (int i = 0; i < kMotorPjMax; i++) if (s_pjMulti & (1u << i)) { nx = i; break; } MotorPjSelect(nx); }
    else { s_pjMulti |= b; MotorPjSelect(q); }
}
static int PjMultiCount() { int n = 0; const int s0 = MotorPjSel(); for (int i = 0; i < kMotorPjMax; i++) { float p[3], y, sc; if (((s_pjMulti >> i) & 1u) && i != s0 && MotorPjGet(i, p, &y, &sc)) n++; } return n + (s0 >= 0 ? 1 : 0); }
static void PjGroupSet(const float* np, float ny, float ns) {      // el elegido a (np, ny, ns) y los demas elegidos con la misma diferencia
    const int s0 = MotorPjSel(); float p0[3], y0 = 0.0f, sc0 = 1.0f; const bool has = s0 >= 0 && MotorPjGet(s0, p0, &y0, &sc0);
    MotorJugSet(np, ny, ns);
    if (!has) return;
    const float d[3] = { np[0] - p0[0], np[1] - p0[1], np[2] - p0[2] }, dy = ny - y0, f = sc0 > 1e-4f ? ns / sc0 : 1.0f;
    for (int i = 0; i < kMotorPjMax; i++) { if (i == s0 || !((s_pjMulti >> i) & 1u)) continue; float p[3], y, sc; if (!MotorPjGet(i, p, &y, &sc)) continue;
        const float q[3] = { p[0] + d[0], p[1] + d[1], p[2] + d[2] }; MotorPjSet(i, q, y + dy, std::clamp(sc * f, 0.05f, 10.0f)); }
}
static void PjHideSel() {      // (17/11 17:24, Agustin: "me faltan opciones para ocultar jugadores") H: oculta o muestra los elegidos
    static MotorPjInfo PI; MotorPjInfoGet(&PI); const int s0 = MotorPjSel(); if (s0 < 0) return;
    const bool hide = !PI.hidden[s0]; UndoBegin();
    for (int i = 0; i < kMotorPjMax; i++) if (i == s0 || ((s_pjMulti >> i) & 1u)) { float p[3], y, sc; if (MotorPjGet(i, p, &y, &sc)) MotorPjShow(i, !hide); }
    UndoEnd();
}
static void PjShowAll() { static MotorPjInfo PI; MotorPjInfoGet(&PI); UndoBegin(); int n = 0; for (int i = 0; i < kMotorPjMax; i++) if (PI.hidden[i]) { float p[3], y, sc; if (MotorPjGet(i, p, &y, &sc)) { MotorPjShow(i, true); n++; } } UndoEnd(); }
static void ToolbarDraw() {
    if (!s_iconTried) IconsLoad();
    ImDrawList* dl = MotorDL(); const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float k = UiK(ds);
    float mx = -1.0e6f, my = -1.0e6f, sx = 1.0f, sy = 1.0f; bool front = false;
    { HWND h = OverlayHwnd(); POINT p{}; RECT rc{};
      if (h && !g_menuOpen.load(std::memory_order_relaxed) && GetForegroundWindow() == h && GetCursorPos(&p) && ScreenToClient(h, &p) && GetClientRect(h, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
          sx = (float)(rc.right - rc.left) / ds.x; sy = (float)(rc.bottom - rc.top) / ds.y; mx = (float)p.x / sx; my = (float)p.y / sy; front = true; } }
    ImFont* font = MotorFont(0);                 // (15/11 m) letra Lack UA (overlay.cpp); sin el archivo, la del menu
    const ImU32 cPanel = IM_COL32(37, 37, 38, 244), cHead = IM_COL32(51, 51, 55, 255), cLine = IM_COL32(14, 14, 15, 255), cRow = IM_COL32(62, 62, 66, 255),
                ink = IM_COL32(222, 222, 222, 255), dim = IM_COL32(142, 142, 146, 255), acc = IM_COL32(240, 138, 36, 255), cField = IM_COL32(27, 27, 28, 255);
    // (15/11 p) Editar > Preferencias > Tamano del texto ([PES21FX] motor_texto): las letras x tf y las filas crecen con ellas (rf)
    const float tf = std::clamp(s_pendText > 0.0f ? s_pendText : g_cfg.motorText, 0.8f, 1.5f), rf = 0.55f + 0.45f * tf;
    const float barH = kBarH * k * std::max(1.0f, rf), statH = kStatH * k * std::max(1.0f, rf), fs = 15.0f * k * tf, fsS = 12.5f * k * tf, rowH = 28.0f * k * rf;
    s_secN = 0;
    const bool lDown = front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, lEdge0 = lDown && !s_tlWas;      // boton izquierdo (el clic se resuelve al final)
    // (17/11 07:23, aprobado: "clic derecho = valor de fabrica") borde del boton derecho para los campos numericos
    static bool s_rWas = false; const bool rDown = front && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0, rEdge = rDown && !s_rWas; s_rWas = rDown;
    static int s_hovId = -1; static double s_hovT = 0.0;      // campo bajo el mouse y desde cuando (para la ayuda)
    // (15/11 o) paneles: leftW / rightW = ancho del panel (se dibuja entero), leftV / rightV = lo que se ve (abre y cierra animado)
    float leftW = std::clamp(g_cfg.motorPanelLW, 160.0f, 900.0f) * k, rightW = std::clamp(g_cfg.motorPanelRW, 200.0f, 900.0f) * k;
    { const float room = ds.x - 260.0f * k; if (leftW + rightW > room) { const float f = std::max(0.2f, room / (leftW + rightW)); leftW *= f; rightW *= f; } }
    { const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime);
      for (int sd = 0; sd < 2; sd++) { const float want = (sd == 0 ? g_cfg.motorPanelLOn : g_cfg.motorPanelROn) ? 1.0f : 0.0f;
          if (s_pA[sd] < 0.0f) s_pA[sd] = want; else s_pA[sd] = want > s_pA[sd] ? std::min(want, s_pA[sd] + dt / 0.16f) : std::max(want, s_pA[sd] - dt / 0.16f); }
      for (int q = 0; q < 2; q++) { const bool open = q == 0 ? !s_foldStudio : !s_foldLights; s_foldA[q] = open ? std::min(1.0f, s_foldA[q] + dt / 0.12f) : std::max(0.0f, s_foldA[q] - dt / 0.12f); }
      s_foldPjA = !s_foldPj ? std::min(1.0f, s_foldPjA + dt / 0.12f) : std::max(0.0f, s_foldPjA - dt / 0.12f); }
    auto ez = [](float a) { return 1.0f - (1.0f - a) * (1.0f - a) * (1.0f - a); };
    const float aL = ez(s_pA[0]), aR = ez(s_pA[1]);
    const float y0 = barH + 1.0f, y1 = ds.y - statH;
    // (15/11 q) lugar de cada panel: P[i] = rectangulo de dibujo (x, ya, yb, ancho W) y lo que se ve (V, abre / cierra animado)
    struct PLay { int dock; float x, ya, yb, W, V, a; };
    PLay P[2]; float SW[2] = { 0.0f, 0.0f }, SV[2] = { 0.0f, 0.0f };
    { const float aP[2] = { aL, aR }, WP[2] = { leftW, rightW }; int dk[2] = { std::clamp(g_cfg.motorDock[0], 0, 2), std::clamp(g_cfg.motorDock[1], 0, 2) };
      if (s_mvOn && s_mvId > 0) dk[s_mvId - 1] = 2;      // el que se esta moviendo sigue al mouse
      for (int i = 0; i < 2; i++) if (dk[i] < 2) { SW[dk[i]] = std::max(SW[dk[i]], WP[i]); SV[dk[i]] = std::max(SV[dk[i]], WP[i] * aP[i]); }
      { const float room = ds.x - 260.0f * k; if (SW[0] + SW[1] > room) { const float f = std::max(0.2f, room / (SW[0] + SW[1])); for (int q = 0; q < 2; q++) { SW[q] *= f; SV[q] *= f; } } }
      for (int i = 0; i < 2; i++) { PLay& Q = P[i]; Q.dock = dk[i]; Q.a = aP[i];
          if (dk[i] < 2) { const int o = 1 - i; Q.W = SW[dk[i]]; Q.V = Q.W * aP[i]; Q.x = dk[i] == 0 ? 0.0f : ds.x - Q.W; Q.ya = y0; Q.yb = y1;
              if (dk[o] == dk[i]) {      // dos en el mismo costado: apilados, la division es la parte del de arriba
                  const int top = std::clamp(g_cfg.motorStackTop, 0, 1); const float sp = std::clamp(g_cfg.motorSplit, 0.15f, 0.85f);
                  const float wT = sp * std::max(0.001f, aP[top]), wB = (1.0f - sp) * std::max(0.001f, aP[1 - top]), ym = y0 + (y1 - y0) * wT / (wT + wB);
                  if (i == top) Q.yb = ym; else Q.ya = ym; } }
          else { const float* F = g_cfg.motorFloat[i]; float w = F[2] * k, h = F[3] * k, x = F[0] * k, y = F[1] * k;
              if (s_mvOn && s_mvId == i + 1) { w = s_mvW; h = s_mvH; x = mx - s_mvGX; y = my - s_mvGY; }
              w = std::clamp(w, 160.0f * k, std::max(160.0f * k, ds.x - 40.0f * k)); h = std::clamp(h, 120.0f * k, std::max(120.0f * k, y1 - y0));
              x = std::clamp(x, 0.0f, std::max(0.0f, ds.x - w)); y = std::clamp(y, y0, std::max(y0, y1 - h));
              Q.W = w; Q.V = w * (aP[i] > 0.001f ? 1.0f : 0.0f); Q.x = x; Q.ya = y; Q.yb = y + h; } } }
    const float leftV = SV[0], rightV = SV[1];
    // donde caeria el panel agarrado si se suelta ahora (se dibuja como un recuadro naranja)
    s_dropDock = -1;
    if (s_mvOn && s_mvId > 0) { const int i = s_mvId - 1, o = 1 - i; const float ed = 70.0f * k;
        s_dropDock = mx < ed ? 0 : mx > ds.x - ed ? 1 : 2; s_dropTop = my < (y0 + y1) * 0.5f ? 1 : 0;
        if (s_dropDock < 2) { const bool st = g_cfg.motorDock[o] == s_dropDock && (o == 0 ? g_cfg.motorPanelLOn : g_cfg.motorPanelROn);
            const float w = std::max(i == 0 ? leftW : rightW, st ? SW[s_dropDock] : 0.0f), x = s_dropDock == 0 ? 0.0f : ds.x - w; const float ym = (y0 + y1) * 0.5f;
            s_dropR[0] = x; s_dropR[2] = x + w; s_dropR[1] = st && !s_dropTop ? ym : y0; s_dropR[3] = st && s_dropTop ? ym : y1; }
        else { s_dropR[0] = P[i].x; s_dropR[1] = P[i].ya; s_dropR[2] = P[i].x + P[i].W; s_dropR[3] = P[i].yb; } }
    if (s_editId >= 0 && s_editDone && ++s_editAge > 3) { s_editId = -1; s_editDone = 0; }      // el campo que se escribia ya no esta a la vista
    // (15/11 d, Agustin 02:37: "lindo y tambien con animaciones") al abrir, los paneles entran deslizando desde su borde; la marca
    // del arbol se desliza de una fila a otra; el contenido del inspector entra con un fundido corto al cambiar de fila. Se hace
    // moviendo / atenuando los vertices ya emitidos de cada parte (la lista de dibujo no tiene transformaciones).
    { const double now = Now(); const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime);
      if (now - s_uiT > 0.4) { s_openA = 0.0f; s_selY = -1.0f; s_inspA = 1.0f; s_menuOpen = -1; s_menuKb = -1; s_modal = 0; s_origN = 0; s_pendN = 0; s_enterT = now; s_exitT = -1.0; s_exitSent = false; s_coverEnd = -1.0; s_sceneOnce = false; s_autoT = -1.0; EnterSoundOnce();
          { std::lock_guard<std::mutex> lk(g_cfgMutex); if (g_cfg.motorCamSpeed > 0.0f) g_cfg.live[344][1] = g_cfg.motorCamSpeed; if (g_cfg.motorCamSens > 0.0f) g_cfg.live[344][3] = g_cfg.motorCamSens; }   /* (15/11 p) Preferencias > Camara */
          if (now - s_ldT0 > 3.0) LdStart(now); LdHit(0, now); }
      s_uiT = now; s_openA = std::min(1.0f, s_openA + dt / 0.28f); s_inspA = std::min(1.0f, s_inspA + dt / 0.16f);
      if (s_sel != s_selWas) { s_selWas = s_sel; s_inspA = 0.0f; } }
    const float eo = 1.0f - (1.0f - s_openA) * (1.0f - s_openA) * (1.0f - s_openA);      // salida suave
    // (15/11 e, Agustin 02:41: "que este motor en la pantalla pueda leer todo tipos de luces. y sistemas del juego") 4 veces por
    // segundo se releen las luces y el conteo de objetos de la escena grafica (motorscene.cpp); la marcada se sigue por su direccion
    if (Now() - s_sceneT > 0.25) { s_sceneT = Now();
        static MotorLight tmp[64]; MotorSceneInfo si{}; const int n = MotorSceneLights(tmp, 64, &si);
        if (n >= 0) { memcpy(s_lights, tmp, sizeof(MotorLight) * (size_t)n); s_nLights = n; s_info = si; s_sceneOnce = true;
            for (int i = 0; i < n; i++) { bool f = false; for (int q = 0; q < s_origN; q++) if (s_orig[q].obj == s_lights[i].obj) f = true; if (!f && s_origN < 64) { const float* c0 = s_lights[i].color; const float m0 = std::max(c0[0], std::max(c0[1], c0[2])); s_orig[s_origN++] = OrigL{ s_lights[i].obj, s_lights[i].on, m0 > 1.0e-6f ? m0 : 1.0f }; } }
            if (s_pendN > 0 && Now() < s_pendUntil) PendApply(); }
        else MotorErr("No se pudo leer la escena del motor: el juego estaba cambiando sus listas de objetos. Se reintenta solo; si este aviso sigue, sal\xC3\xAD del Motor y volv\xC3\xA9 a entrar.");
        if (s_sel >= 4) { int f = -1; for (int i = 0; i < s_nLights; i++) if (s_lights[i].obj == s_selObj) f = i; if (f < 0) { s_sel = 0; s_selObj = 0; } else s_sel = 4 + f; } }
    static const char* const kKind[5] = { "Luz", "Sol", "Local", "Punto", "Foco" };
    auto anim = [&](int from, float dx, float dy, float a) {
        for (int i = from; i < dl->VtxBuffer.Size; i++) { ImDrawVert& v = dl->VtxBuffer[i]; v.pos.x += dx; v.pos.y += dy;
            if (a < 0.999f) { const ImU32 al = (ImU32)((float)(v.col >> 24) * std::max(0.0f, a)); v.col = (v.col & 0x00ffffffu) | (al << 24); } } };
    int hot = -1; const int hp = s_hotPrev;
    bool hitOff = false;      // (15/11 o) panel abriendo / cerrando: sus botones no se pueden tocar
    float hitY0 = -1.0e9f, hitY1 = 1.0e9f;      // (07:50) parte visible del Inspector corrido: lo tapado no se toca
    auto hit = [&](int id, const ImVec2& a, const ImVec2& b) { if (!hitOff && mx >= a.x && mx <= b.x && my >= a.y && my <= b.y && my >= hitY0 && my <= hitY1) { hot = id; s_tipA = a; s_tipB = b; } return hp == id && !hitOff; };
    auto text = [&](float x, float yc, ImU32 c, const char* t, float size) { t = TRS(t); const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t); dl->AddText(font, size, ImVec2(x, yc - ts.y * 0.5f), c, t); return ts.x; };
    auto textC = [&](const ImVec2& a, const ImVec2& b, ImU32 c, const char* t, float size) { t = TRS(t); const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t); dl->AddText(font, size, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), c, t); };
    int nz = 0;
    auto zone = [&](const ImVec2& a, const ImVec2& b) { if (nz < 16) { s_hr[nz][0].store(a.x * sx); s_hr[nz][1].store(a.y * sy); s_hr[nz][2].store(b.x * sx); s_hr[nz][3].store(b.y * sy); nz++; } };
    const bool camFree = g_cfg.live[344][0] > 0.5f, grid = g_cfg.motorGrid != 0, player = MotorUiPlayer();
    // selector partido: n opciones en una caja; devuelve nada, anota hot = id0 + i
    auto seg = [&](int id0, const ImVec2& a, const ImVec2& b, int n, int cur, const char* const* names) {
        dl->AddRectFilled(a, b, cField, 3.0f * k); const float w = (b.x - a.x) / (float)std::max(1, n);
        for (int i = 0; i < n; i++) { const ImVec2 p0(a.x + (float)i * w, a.y), p1(a.x + (float)(i + 1) * w, b.y); const bool hv = hit(id0 + i, p0, p1);
            if (i == cur) dl->AddRectFilled(ImVec2(p0.x + 1.0f, p0.y + 1.0f), ImVec2(p1.x - 1.0f, p1.y - 1.0f), acc, 2.0f * k); else if (hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 22), 2.0f * k);
            // (17/11 07:16, Agustin: "mejora un poco la interfaz") el texto que no entra en su parte se achica (hasta 70 %) y se recorta: antes se
            // pisaba con la parte de al lado ("Si Con teselaci...", "on nombres")
            { float fz = fsS + 1.0f * k; const char* t = TRS(names[i]); const float tw = font->CalcTextSizeA(fz, FLT_MAX, 0.0f, t).x, room = w - 8.0f * k;
              if (tw > room && tw > 0.0f) fz = std::max(fz * 0.7f, fz * room / tw);
              dl->PushClipRect(ImVec2(p0.x + 2.0f, p0.y), ImVec2(p1.x - 2.0f, p1.y), true);
              textC(p0, p1, i == cur ? IM_COL32(20, 20, 20, 255) : hv ? ink : IM_COL32(190, 190, 192, 255), names[i], fz); dl->PopClipRect(); } }
        dl->AddRect(a, b, cLine, 3.0f * k);
    };
    // (15/11 o) campo numerico editable (ids 2000..2499): arrastrar de costado cambia el valor (step por punto de pantalla, Shift = fino);
    // un clic sin arrastrar lo deja escribir. Devuelve true si v cambio en este cuadro (quien llama lo aplica al juego).
    auto inside = [&](const ImVec2& a, const ImVec2& b) { return mx >= a.x && mx <= b.x && my >= a.y && my <= b.y && my >= hitY0 && my <= hitY1; };
    float pOffC = 0.0f;      // (15/11 q) corrimiento del panel que se esta dibujando: los recortes van a su lugar real
    // (15/11 r) el recorte se pone en el lugar de dibujo (sin corrimiento): ImGui descarta las letras que caen fuera del recorte al
    // armarlas, y con el recorte ya corrido el texto del panel movido desaparecia (Agustin 18:43). Al final del panel clipShift corre
    // los recortes de sus comandos junto con los vertices.
    auto clipPush = [&](const ImVec2& a, const ImVec2& b) { dl->PushClipRect(a, b, true); };
    auto clipBegin = [&]() { dl->AddDrawCmd(); return dl->CmdBuffer.Size - 1; };
    auto clipShift = [&](int c0, float tx) { if (tx == 0.0f) return; for (int i = std::max(0, c0); i < dl->CmdBuffer.Size; i++) { ImVec4& r = dl->CmdBuffer[i].ClipRect; r.x += tx; r.z += tx; } dl->AddDrawCmd(); };
    auto numField = [&](int id, const ImVec2& p0, const ImVec2& p1, float& v, float step, float lo, float hi, const char* fmt, const char* unit) -> bool {
        const bool hv = hit(id, p0, p1); bool ch = false;
        if (lEdge0 && !hitOff && inside(p0, p1)) { s_pressId = id; s_pressVal = v; }
        // (07:23) clic derecho = valor de fabrica (los del jugador, la velocidad, la intensidad y cerca / lejos de la camara)
        { float d = 0.0f; bool has = true;
          switch (id) { case 2100: case 2101: case 2102: case 2103: case 2130: case 2131: case 2132: d = 0.0f; break; case 2104: case 2120: case 2121: d = 100.0f; break; case 2014: d = 0.05f; break; case 2015: d = 1000.0f; break; default: has = false; }
          if (has && rEdge && !hitOff && inside(p0, p1) && s_editId != id && v != d) { UndoBegin(); s_ubEndPending = true; v = std::clamp(d, lo, hi); ch = true; Note("Valor de f\xC3\xA1" "brica"); }
          if (inside(p0, p1) && !hitOff) { if (s_hovId != id) { s_hovId = id; s_hovT = Now(); }
            else if (Now() - s_hovT > 0.7 && s_dragId != id && s_editId != id) {      // (07:23) ayuda al pasar el mouse
              const char* hlp = has ? "Arrastr\xC3\xA1 de costado (May\xC3\xBAs = fino), clic para escribir, clic derecho = de f\xC3\xA1" "brica" : "Arrastr\xC3\xA1 de costado (May\xC3\xBAs = fino) o clic para escribir";
              const char* ht = TRS(hlp); const float hs = fsS - 0.5f * k; const ImVec2 hz = font->CalcTextSizeA(hs, FLT_MAX, 0.0f, ht);
              ImDrawList* fg = ImGui::GetForegroundDrawList(); const float hx = std::min(p0.x + pOffC, ds.x - hz.x - 14.0f * k), hy = p0.y - hz.y - 10.0f * k;
              fg->AddRectFilled(ImVec2(hx, hy), ImVec2(hx + hz.x + 12.0f * k, hy + hz.y + 6.0f * k), IM_COL32(18, 18, 20, 240), 4.0f * k); fg->AddRect(ImVec2(hx, hy), ImVec2(hx + hz.x + 12.0f * k, hy + hz.y + 6.0f * k), IM_COL32(90, 90, 96, 255), 4.0f * k);
              fg->AddText(font, hs, ImVec2(hx + 6.0f * k, hy + 3.0f * k), IM_COL32(225, 225, 228, 255), ht); } }
          else if (s_hovId == id) s_hovId = -1; }
        if (s_dragId == id && lDown) {
            if (fabsf(mx + pOffC - s_dragX0) > 3.0f * k) s_dragMoved = true;
            if (s_dragMoved) { const float fine = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 0.1f : 1.0f; const float nv = std::clamp(s_dragV0 + (mx + pOffC - s_dragX0) / k * step * fine, lo, hi); if (nv != v) { v = nv; ch = true; } } }
        const bool ed = s_editId == id;
        if (ed && s_editDone) {
            if (s_editDone == 1) { char b[24]; memcpy(b, s_editBuf, sizeof b); b[std::min(s_editLen, 23)] = 0; for (char* q = b; *q; q++) if (*q == ',') *q = '.';
                char* e = nullptr; const float nv = strtof(b, &e); if (e != b && nv == nv) { const float c2 = std::clamp(nv, lo, hi); if (c2 != v) { UndoBegin(); s_ubEndPending = true; v = c2; ch = true; } } }
            s_editId = -1; s_editDone = 0; }
        const bool act2 = s_dragId == id || (ed && !s_editDone);
        dl->AddRectFilled(p0, p1, ed ? IM_COL32(16, 16, 17, 255) : (hv || act2) ? IM_COL32(44, 44, 47, 255) : cField, 3.0f * k);
        if (ed) dl->AddRect(p0, p1, acc, 3.0f * k, 0, 1.5f * k);
        char b[48]; if (s_editId == id) snprintf(b, sizeof b, "%.*s", std::min(s_editLen, 23), s_editBuf); else { snprintf(b, sizeof b, fmt, v); if (unit && unit[0]) { const size_t l = strlen(b); snprintf(b + l, sizeof b - l, "%s", unit); } }
        const float yc = (p0.y + p1.y) * 0.5f, sz = fsS + 1.0f * k; const ImVec2 ts = font->CalcTextSizeA(sz, FLT_MAX, 0.0f, b);
        clipPush(p0, p1); dl->AddText(font, sz, ImVec2(p0.x + 8.0f * k, yc - ts.y * 0.5f), ink, b);
        if (s_editId == id) { if (s_editFresh) dl->AddRectFilled(ImVec2(p0.x + 6.0f * k, yc - ts.y * 0.5f), ImVec2(p0.x + 10.0f * k + ts.x, yc + ts.y * 0.5f), IM_COL32(240, 138, 36, 70));
            if (std::fmod(Now(), 1.0) < 0.55) dl->AddLine(ImVec2(p0.x + 9.0f * k + ts.x, yc - ts.y * 0.45f), ImVec2(p0.x + 9.0f * k + ts.x, yc + ts.y * 0.45f), ink, 1.2f * k); }
        else if (hv || s_dragId == id) {      // flechitas a los costados: se puede arrastrar
            const float ax = 5.0f * k; const ImU32 c = IM_COL32(150, 150, 154, 255);
            dl->AddTriangleFilled(ImVec2(p1.x - 6.0f * k, yc - ax * 0.7f), ImVec2(p1.x - 6.0f * k, yc + ax * 0.7f), ImVec2(p1.x - 6.0f * k + ax * 0.8f, yc), c);
            dl->AddTriangleFilled(ImVec2(p1.x - 16.0f * k, yc - ax * 0.7f), ImVec2(p1.x - 16.0f * k, yc + ax * 0.7f), ImVec2(p1.x - 16.0f * k - ax * 0.8f, yc), c); }
        dl->PopClipRect();
        return ch; };
    // slider (ids 2500..2999): el valor sigue al mouse mientras se arrastra; v fuera de [lo, hi] deja la perilla en la punta
    auto slider = [&](int id, const ImVec2& p0, const ImVec2& p1, float& v, float lo, float hi, ImU32 fill) -> bool {
        const bool hv = hit(id, p0, p1); bool ch = false;
        if (lEdge0 && !hitOff && inside(p0, p1)) { s_pressId = id; s_pressVal = v; }
        const float pad = 7.0f * k, x0 = p0.x + pad, x1 = p1.x - pad;
        if (s_dragId == id && lDown) { const float t = std::clamp((mx - x0) / std::max(1.0f, x1 - x0), 0.0f, 1.0f); const float nv = lo + (hi - lo) * t; if (nv != v) { v = nv; ch = true; } }
        const float t = std::clamp((v - lo) / std::max(1.0e-6f, hi - lo), 0.0f, 1.0f), yc = (p0.y + p1.y) * 0.5f, th = 4.0f * k, kx = x0 + (x1 - x0) * t;
        if ((fill >> 24) != 0) { dl->AddRectFilled(ImVec2(x0, yc - th * 0.5f), ImVec2(x1, yc + th * 0.5f), cField, th);      // fill transparente = barra pintada por quien llama
            dl->AddRectFilled(ImVec2(x0, yc - th * 0.5f), ImVec2(kx, yc + th * 0.5f), fill, th); }
        const bool on = hv || s_dragId == id; dl->AddCircleFilled(ImVec2(kx, yc), (on ? 7.5f : 6.5f) * k, on ? IM_COL32(255, 255, 255, 255) : IM_COL32(222, 222, 222, 255), 18);
        dl->AddCircle(ImVec2(kx, yc), (on ? 7.5f : 6.5f) * k, cLine, 18, 1.0f);
        return ch; };
    // flecha de una seccion: a = 1 abierta (apunta abajo), 0 cerrada (apunta a la derecha)
    auto arrow = [&](float cx, float cy, float a, ImU32 c) { const float an = (1.0f - a) * -1.5707963f, ca = cosf(an), sa = sinf(an);
        const ImVec2 q[3] = { ImVec2(-4.5f * k, -2.6f * k), ImVec2(4.5f * k, -2.6f * k), ImVec2(0.0f, 4.0f * k) }; ImVec2 r[3];
        for (int i = 0; i < 3; i++) r[i] = ImVec2(cx + q[i].x * ca - q[i].y * sa, cy + q[i].x * sa + q[i].y * ca);
        dl->AddTriangleFilled(r[0], r[1], r[2], c); };
    // boton X chico (cerrar panel)
    auto xBtn = [&](int id, float cx, float cy) { const float h = 9.0f * k; const ImVec2 a(cx - h, cy - h), b(cx + h, cy + h); const bool hv = hit(id, a, b);
        if (hv) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 30), 3.0f * k); const float e = 4.0f * k; const ImU32 c = hv ? ink : dim;
        dl->AddLine(ImVec2(cx - e, cy - e), ImVec2(cx + e, cy + e), c, 1.5f * k); dl->AddLine(ImVec2(cx - e, cy + e), ImVec2(cx + e, cy - e), c, 1.5f * k); };
    // (15/11 o, Agustin 05:31: "un icono al lado", "los objetos con un texto un poco mas pequeno y con iconos adaptativos dependiendo de
    // que se trate cada uno") iconos dibujados con lineas, de 1 a 1 con el tamano de la letra: 0 estudio, 1 camara, 2 grilla, 3 jugador,
    // 10 grupo de luces, 20 + clase de luz (0 luz, 1 sol, 2 local, 3 punto, 4 foco)
    auto glyph = [&](int t, const ImVec2& c, float r, ImU32 col) {
        const float w = std::max(1.0f, 1.3f * k);
        if (t == 0) { const ImVec2 a(c.x, c.y - r), b(c.x + r * 0.87f, c.y - r * 0.5f), d(c.x + r * 0.87f, c.y + r * 0.5f), e(c.x, c.y + r), f(c.x - r * 0.87f, c.y + r * 0.5f), g(c.x - r * 0.87f, c.y - r * 0.5f);
            const ImVec2 hx[6] = { a, b, d, e, f, g }; dl->AddPolyline(hx, 6, col, ImDrawFlags_Closed, w); dl->AddLine(g, c, col, w); dl->AddLine(b, c, col, w); dl->AddLine(c, e, col, w); }
        else if (t == 1) { dl->AddRect(ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x + r * 0.35f, c.y + r * 0.6f), col, 1.5f * k, 0, w);
            dl->AddTriangleFilled(ImVec2(c.x + r * 0.35f, c.y), ImVec2(c.x + r, c.y - r * 0.55f), ImVec2(c.x + r, c.y + r * 0.55f), col); }
        else if (t == 2) { dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 0.0f, 0, w);
            for (int q = 1; q < 3; q++) { const float o = -r + 2.0f * r * (float)q / 3.0f; dl->AddLine(ImVec2(c.x + o, c.y - r), ImVec2(c.x + o, c.y + r), col, w * 0.8f); dl->AddLine(ImVec2(c.x - r, c.y + o), ImVec2(c.x + r, c.y + o), col, w * 0.8f); } }
        else if (t == 3) { dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.52f), r * 0.38f, col, 12);
            dl->PathArcTo(ImVec2(c.x, c.y + r * 0.95f), r * 0.85f, 3.14159f, 6.28318f, 12); dl->PathFillConvex(col); }
        else if (t == 10 || t == 20 || t == 22) { dl->AddCircle(ImVec2(c.x, c.y - r * 0.25f), r * 0.62f, col, 14, w);          // bombita
            dl->AddRectFilled(ImVec2(c.x - r * 0.3f, c.y + r * 0.42f), ImVec2(c.x + r * 0.3f, c.y + r * 0.85f), col, 1.0f * k);
            if (t == 22) dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.25f), r * 0.25f, col, 10); }
        else if (t == 21) { dl->AddCircleFilled(c, r * 0.42f, col, 12);                                                         // sol
            for (int q = 0; q < 8; q++) { const float an = (float)q * 0.785398f; dl->AddLine(ImVec2(c.x + cosf(an) * r * 0.62f, c.y + sinf(an) * r * 0.62f), ImVec2(c.x + cosf(an) * r, c.y + sinf(an) * r), col, w); } }
        else if (t == 23) { dl->AddCircleFilled(c, r * 0.32f, col, 10); dl->AddCircle(c, r * 0.75f, col, 14, w * 0.8f); }          // punto
        else if (t == 24) { dl->AddRectFilled(ImVec2(c.x - r * 0.3f, c.y - r), ImVec2(c.x + r * 0.3f, c.y - r * 0.45f), col, 1.0f * k);   // foco: cono
            dl->AddTriangle(ImVec2(c.x - r * 0.3f, c.y - r * 0.45f), ImVec2(c.x + r * 0.3f, c.y - r * 0.45f), ImVec2(c.x + r * 0.95f, c.y + r), col, w);
            dl->AddTriangle(ImVec2(c.x - r * 0.3f, c.y - r * 0.45f), ImVec2(c.x - r * 0.95f, c.y + r), ImVec2(c.x + r * 0.95f, c.y + r), col, w); }
        // (15/11 p) Preferencias: 30 edicion (flecha de deshacer), 31 sonido (parlante), 32 idioma (globo), 33 interfaz (ventana)
        else if (t == 30) { dl->PathArcTo(ImVec2(c.x + r * 0.1f, c.y + r * 0.15f), r * 0.72f, 3.6f, 7.4f, 14); dl->PathStroke(col, 0, w);
            const ImVec2 tip(c.x + r * 0.1f + cosf(3.6f) * r * 0.72f, c.y + r * 0.15f + sinf(3.6f) * r * 0.72f);
            dl->AddTriangleFilled(ImVec2(tip.x - r * 0.42f, tip.y - r * 0.05f), ImVec2(tip.x + r * 0.32f, tip.y - r * 0.35f), ImVec2(tip.x + r * 0.12f, tip.y + r * 0.42f), col); }
        else if (t == 31) { dl->AddRectFilled(ImVec2(c.x - r * 0.95f, c.y - r * 0.32f), ImVec2(c.x - r * 0.45f, c.y + r * 0.32f), col, 1.0f * k);
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.5f, c.y - r * 0.3f), ImVec2(c.x + r * 0.1f, c.y - r * 0.85f), ImVec2(c.x + r * 0.1f, c.y + r * 0.85f), col);
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.5f, c.y - r * 0.3f), ImVec2(c.x + r * 0.1f, c.y + r * 0.85f), ImVec2(c.x - r * 0.5f, c.y + r * 0.3f), col);
            dl->PathArcTo(ImVec2(c.x + r * 0.15f, c.y), r * 0.5f, -0.8f, 0.8f, 8); dl->PathStroke(col, 0, w); dl->PathArcTo(ImVec2(c.x + r * 0.15f, c.y), r * 0.85f, -0.8f, 0.8f, 10); dl->PathStroke(col, 0, w); }
        else if (t == 32) { dl->AddCircle(c, r * 0.9f, col, 18, w); dl->AddLine(ImVec2(c.x - r * 0.9f, c.y), ImVec2(c.x + r * 0.9f, c.y), col, w * 0.8f);
            dl->AddEllipse(c, ImVec2(r * 0.4f, r * 0.9f), col, 0.0f, 16, w * 0.8f); }
        else if (t == 33) { dl->AddRect(ImVec2(c.x - r, c.y - r * 0.8f), ImVec2(c.x + r, c.y + r * 0.8f), col, 1.5f * k, 0, w); dl->AddRectFilled(ImVec2(c.x - r, c.y - r * 0.8f), ImVec2(c.x + r, c.y - r * 0.35f), col, 1.5f * k, ImDrawFlags_RoundCornersTop);
            dl->AddLine(ImVec2(c.x - r * 0.35f, c.y - r * 0.35f), ImVec2(c.x - r * 0.35f, c.y + r * 0.8f), col, w * 0.8f); }
    };
    // (15/11 p) titulo de seccion plegable: flecha que gira + nombre un poco mas grande; clic en toda la fila la pliega (ids 1100..1139).
    // Lo plegado se recuerda por nombre + salt (lo marcado en el arbol, o la categoria de las Preferencias). Devuelve si esta abierta.
    auto secHead = [&](float xa, float xb, float& yy, const char* t, uint32_t salt) -> bool {
        const uint32_t h = HashS(t) * 31u + salt; FoldS& F = FoldOf(h);
        const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime); F.a = F.shut ? std::max(0.0f, F.a - dt / 0.12f) : std::min(1.0f, F.a + dt / 0.12f);
        const float hh = 22.0f * k * rf; const ImVec2 p0(xa, yy), p1(xb, yy + hh); bool hv = false;
        if (s_secN < 40) { s_secHash[s_secN] = h; hv = hit(1100 + s_secN, p0, p1); s_secN++; }
        if (hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 14), 3.0f * k);
        arrow(xa + 10.0f * k, yy + hh * 0.5f, ez(F.a), hv ? ink : dim);
        text(xa + 21.0f * k, yy + hh * 0.5f, hv ? ink : IM_COL32(176, 176, 182, 255), t, fsS + 1.0f * k);
        yy += hh + (F.shut ? 3.0f * k : 1.0f * k); return !F.shut; };
    float mX[7] = {};      // donde empieza cada titulo de la barra de menus (para colgar su lista)
    // ---------------- barra de herramientas ----------------
    { const int v0 = dl->VtxBuffer.Size; const ImVec2 a(0, 0), b(ds.x, barH); zone(a, b);
      dl->AddRectFilled(a, b, cHead); dl->AddLine(ImVec2(0, barH), ImVec2(ds.x, barH), cLine, 1.0f);
      float x = 0.0f;
      // (15/11 i, Agustin 03:21: "incluye el logo nem glass ... pequeno arriba a la izquierda del todo") el mismo logo del menu del mod
      { float asp = 1.0f; void* lg = MenuLogoTex(&asp); if (lg && asp > 0.05f) { const float lh = barH * 0.74f, lw = std::min(lh * asp, barH * 3.0f), m = 8.0f * k;
          dl->AddImage((ImTextureID)(uintptr_t)lg, ImVec2(m, (barH - lh) * 0.5f), ImVec2(m + lw, (barH + lh) * 0.5f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255)); x = m + lw + m;
          dl->AddLine(ImVec2(x, 6.0f * k), ImVec2(x, barH - 6.0f * k), IM_COL32(80, 80, 84, 255), 1.0f); } }
      { const ImVec2 p0(x, 0), p1(x + barH * 1.15f, barH); const bool hv = hit(0, p0, p1); if (hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 26));
        const ImVec2 o((p0.x + p1.x) * 0.5f, barH * 0.5f); const float h2 = barH * 0.27f;
        if (s_icon[0]) dl->AddImage((ImTextureID)(uintptr_t)s_icon[0], ImVec2(o.x - h2, o.y - h2), ImVec2(o.x + h2, o.y + h2), ImVec2(0, 0), ImVec2(1, 1), ink); else IconFallback(dl, 0, o, barH * 0.95f, ink, k);
        x = p1.x; dl->AddLine(ImVec2(x, 6.0f * k), ImVec2(x, barH - 6.0f * k), IM_COL32(80, 80, 84, 255), 1.0f); }
      x += 14.0f * k;
      {   // (15/11 n, Agustin 05:11: "dale estilo al nombre al lado del icono nem") "NEM" en cursiva verde con brillo, "MOTOR" en cursiva de
          // contorno blanca; un golpe de glitch corto (separacion de color y salto) al abrir el editor y cada 7 s
          ImFont* fi = MotorFont(1); ImFont* fl = MotorFont(3); const float tsz = fs * 1.14f; const char* na = "NEM"; const char* nb = "MOTOR";
          const ImVec2 sa = fi->CalcTextSizeA(tsz, FLT_MAX, 0.0f, na), sb = fl->CalcTextSizeA(tsz, FLT_MAX, 0.0f, nb);
          const float y = barH * 0.5f - sa.y * 0.5f; const double tn = Now(); const uint32_t tk = (uint32_t)(tn * 30.0);
          const bool gl = s_openA < 0.999f || std::fmod(tn, 7.0) < 0.12; const float gj = gl ? (Hash1(tk * 7u + 3u) - 0.5f) * 5.0f * k : 0.0f;
          for (int q = 0; q < 4; q++) { const float ox = (q & 1 ? 1.0f : -1.0f) * k, oy = (q & 2 ? 1.0f : -1.0f) * k;   // brillo
              dl->AddText(fi, tsz, ImVec2(x + ox, y + oy), IM_COL32(52, 232, 128, 34), na); }
          if (gl) { dl->AddText(fi, tsz, ImVec2(x - 2.0f * k + gj, y), IM_COL32(186, 255, 92, 150), na); dl->AddText(fi, tsz, ImVec2(x + 2.0f * k - gj, y), IM_COL32(255, 60, 120, 110), na); }
          dl->AddText(fi, tsz, ImVec2(x, y), IM_COL32(52, 232, 128, 255), na); x += sa.x + 5.0f * k;
          if (gl) dl->AddText(fl, tsz, ImVec2(x + gj * 0.6f, y), IM_COL32(186, 255, 92, 140), nb);
          dl->AddText(fl, tsz, ImVec2(x, y), IM_COL32(236, 242, 238, 255), nb); x += sb.x + 14.0f * k; }      // el nombre largo y la escena abierta van a la derecha de la barra
      dl->AddLine(ImVec2(x, 6.0f * k), ImVec2(x, barH - 6.0f * k), IM_COL32(80, 80, 84, 255), 1.0f); x += 8.0f * k;
      // (15/11 g) barra de menus: cada titulo abre su lista de acciones (se dibuja al final, encima de todo)
      { const char* const mn[7] = { "Archivo", "Escena", "Ver", "Render", "Luces", "Ayuda", "Editar" };
        for (int q = 0; q < 7; q++) { const int i = kMenuOrd[q]; const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, TRS(mn[i])); const float w = ts.x + 24.0f * k;
            const ImVec2 p0(x, 0), p1(x + w, barH); const bool hv = hit(600 + i, p0, p1), op = s_menuOpen == i;
            if (op) { dl->AddRectFilled(p0, p1, IM_COL32(43, 43, 46, 255)); dl->AddRectFilled(p0, ImVec2(p1.x, 2.0f * k), acc); } else if (hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 24));
            textC(p0, p1, op ? IM_COL32(255, 214, 170, 255) : ink, mn[i], fs); mX[i] = x; x = p1.x; }
        char sn[140], lb[96]; if (s_scName[0]) { SceneLabel(s_scName, lb, sizeof lb); snprintf(sn, sizeof sn, TRS("Motor en tiempo real  \xC2\xB7  %s"), lb); } else snprintf(sn, sizeof sn, TRS("Motor en tiempo real  \xC2\xB7  escena sin guardar"));
        const ImVec2 ts = font->CalcTextSizeA(fsS + 1.0f * k, FLT_MAX, 0.0f, sn); if (ds.x - ts.x - 14.0f * k > x + 20.0f * k) text(ds.x - ts.x - 14.0f * k, barH * 0.5f, dim, sn, fsS + 1.0f * k); }
      anim(v0, 0.0f, -(1.0f - eo) * barH, eo);
    }
    // (15/11 q) cada panel se dibuja en su lugar de siempre (Escena desde x = 0, Inspector pegado a la derecha) y despues se corre
    // entero a donde esta (P[i]); mientras se dibuja, el mouse se lleva al mismo lugar (mx - dx) y los recortes se corren con pOff.
    bool pMoving = false;
    // donde se ve de verdad el panel (para la zona de clics que no le llegan al juego)
    auto pVis = [&](const PLay& Q, float& vx0, float& vx1) { vx0 = Q.dock == 1 ? Q.x + Q.W - Q.V : Q.x; vx1 = vx0 + Q.V; };
    // titulo: agarradera (6 puntitos) + toda la fila sirve para arrastrar el panel (ids 985 / 986); la X de cerrar va despues y gana
    auto pHead = [&](int i, float hx0, float hx1, float hy) { const ImVec2 g0(hx0, hy), g1(hx1 - 30.0f * k, hy + rowH); const bool hv = hit(985 + i, g0, g1) || s_mvId == i + 1;
        if (hv) dl->AddRectFilled(ImVec2(hx0, hy), ImVec2(hx1, hy + rowH), IM_COL32(255, 255, 255, 12));
        const ImU32 c = hv ? acc : IM_COL32(110, 110, 116, 255); const float gx = hx1 - 46.0f * k, gy = hy + rowH * 0.5f;
        for (int q = 0; q < 6; q++) dl->AddCircleFilled(ImVec2(gx + (float)(q % 2) * 5.0f * k, gy + (float)(q / 2 - 1) * 5.0f * k), 1.3f * k, c, 6); };
    // ---------------- arbol de escena ----------------
    auto drawTree = [&]() { const PLay& Q = P[0]; s_treeZone.store(-1); if (Q.V <= 0.5f) return;
      const float leftW = Q.W, y0 = Q.ya, y1 = Q.yb, aL = Q.a; const float slide = Q.dock == 0 ? -(Q.W - Q.V) : Q.dock == 1 ? (Q.W - Q.V) : 0.0f, dx = Q.x + slide;
      pMoving = s_mvOn && s_mvId == 1; mx -= dx; pOffC = dx;
      { float vx0, vx1; pVis(Q, vx0, vx1); s_treeZone.store(nz); zone(ImVec2(vx0, y0), ImVec2(vx1, y1)); }
      { const int c0 = clipBegin(); const int v0 = dl->VtxBuffer.Size; const ImVec2 a(0, y0), b(leftW, y1); hitOff = aL < 0.999f || pMoving;
      dl->AddRectFilled(a, b, cPanel); dl->AddLine(ImVec2(leftW, y0), ImVec2(leftW, y1), cLine, 1.0f); if (Q.dock != 0) dl->AddLine(ImVec2(0, y0), ImVec2(0, y1), cLine, 1.0f);
      if (Q.dock == 2) dl->AddRect(ImVec2(0, y0), ImVec2(leftW, y1), IM_COL32(70, 70, 76, 255), 0.0f, 0, 1.0f);
      dl->AddRectFilled(a, ImVec2(leftW, y0 + rowH), cHead); text(12.0f * k, y0 + rowH * 0.5f, dim, "ESCENA", fsS);
      pHead(0, 0.0f, leftW, y0);
      xBtn(992, leftW - 16.0f * k, y0 + rowH * 0.5f);
      // (15/11 o) secciones plegables: Estudio (sus 3 filas de abajo) y Luces (la lista)
      const bool fS = s_foldStudio, fL = s_foldLights; const int nRows = fS ? 1 : 4;
      // (17/11 03:19) Jugadores: grupo plegable con el del Editar y los propios del Motor, cada uno con su ojo
      static MotorPjInfo PJ; MotorPjInfoGet(&PJ); int pjAll[kMotorPjMax], nPjL = 0; for (int q = 0; q < kMotorPjMax; q++) if (PJ.st[q]) pjAll[nPjL++] = q;
      s_pjScroll = std::clamp(s_pjScroll, 0, std::max(0, nPjL - kPjRows));
      const int* pjIdx = pjAll + s_pjScroll;
      const int nSub = (fS || s_foldPj) ? 0 : std::min(nPjL - s_pjScroll, kPjRows);      // (03:23) sin el jugador del Editar; (06:36) hasta kPjRows a la vista
      char pjCnt[24]; snprintf(pjCnt, sizeof pjCnt, "%d", nPjL);
      const char* const nm[4] = { "Estudio", "C\xC3\xA1mara", "Grilla de piso", "Jugadores" }; const char* const st[4] = { "", camFree ? "libre" : "del juego", grid ? "" : "oculta", pjCnt };
      const float rowL = 23.0f * k * rf, yGrp = y0 + rowH * (1.0f + (float)(nRows + nSub)), yL0 = yGrp + rowH;       // luces: filas mas bajas, despues de las filas y el titulo del grupo
      const int maxL = fL ? 0 : std::max(0, (int)((y1 - yL0 - 22.0f * k) / rowL)), shownL = std::min(s_nLights, maxL);
      { const int wd = s_uiWheel.exchange(0);
        const float yP0 = y0 + rowH * (float)(1 + nRows), yP1 = yP0 + rowH * (float)nSub;
        if (wd != 0 && nSub > 0 && my >= yP0 && my < yP1 && nPjL > kPjRows) s_pjScroll = std::clamp(s_pjScroll - (wd > 0 ? 1 : -1) * 2, 0, std::max(0, nPjL - kPjRows));   // (06:36) rueda sobre los jugadores
        else if (wd != 0) s_treeScroll -= (wd > 0 ? 1 : -1) * 3;
        s_treeScroll = std::clamp(s_treeScroll, 0, std::max(0, s_nLights - maxL)); }
      const int sc = s_treeScroll;      // primera luz a la vista
      { int subSel = -1; if (s_sel == 3 && nSub > 0) { for (int j = 0; j < nPjL; j++) if (pjIdx[j] == PJ.sel) subSel = j; }
        const float ty = subSel >= 0 ? y0 + rowH * (float)(1 + nRows + subSel) : s_sel < 4 ? y0 + rowH + (float)s_sel * rowH : yL0 + (float)(s_sel - 4 - sc) * rowL; const float sh = s_sel < 4 ? rowH : rowL;
        if ((s_sel < 4 && s_sel < nRows) || (s_sel >= 4 && s_sel - 4 >= sc && s_sel - 4 - sc < shownL)) { if (s_selY < 0.0f) s_selY = ty; s_selY += (ty - s_selY) * std::min(1.0f, ImGui::GetIO().DeltaTime * 18.0f); if (fabsf(ty - s_selY) < 0.5f) s_selY = ty;
        dl->AddRectFilled(ImVec2(0, s_selY), ImVec2(leftW, s_selY + sh), cRow); dl->AddRectFilled(ImVec2(0, s_selY), ImVec2(3.0f * k, s_selY + sh), acc); } }
      for (int i = 0; i < nRows; i++) { const float y = y0 + rowH + (float)i * rowH; const ImVec2 p0(0, y), p1(leftW, y + rowH); const bool hv = hit(10 + i, p0, p1);
          if (s_sel != i && hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 14));
          const float ind = i == 0 ? 12.0f * k : 34.0f * k; const float yc = y + rowH * 0.5f;
          if (i == 0) { const ImVec2 f0(ind - 6.0f * k, y + 2.0f * k), f1(ind + 15.0f * k, y + rowH - 2.0f * k); const bool ha = hit(14, f0, f1);   // flecha: pliega el Estudio
              if (ha) dl->AddRectFilled(f0, f1, IM_COL32(255, 255, 255, 34), 3.0f * k); arrow(ind + 4.5f * k, yc, s_foldA[0], ha ? ink : dim); }
          else if (i == 3) { const ImVec2 f0(ind - 16.0f * k, y + 2.0f * k), f1(ind - 1.0f * k, y + rowH - 2.0f * k); const bool ha = hit(16, f0, f1);   // flecha: pliega los jugadores
              if (ha) dl->AddRectFilled(f0, f1, IM_COL32(255, 255, 255, 34), 3.0f * k); arrow(ind - 8.5f * k, yc, s_foldPjA, ha ? ink : dim); }
          else dl->AddRectFilled(ImVec2(ind - 12.0f * k, yc - 1.0f), ImVec2(ind - 4.0f * k, yc), IM_COL32(90, 90, 94, 255));
          const bool off = (i == 2 && !grid);
          // categoria (Estudio): letra mas grande + icono; objetos (camara, grilla, jugador): letra un poco mas chica + su icono
          if (i == 0) glyph(0, ImVec2(ind + 24.0f * k, yc), 6.5f * k, IM_COL32(120, 200, 255, 255)); else glyph(i, ImVec2(ind + 3.0f * k, yc), 5.5f * k, off ? IM_COL32(100, 100, 104, 255) : IM_COL32(190, 190, 196, 255));
          const float tx = (i == 0 ? ind + 37.0f * k : ind + 15.0f * k) ; const float w = text(tx, yc, off ? dim : ink, nm[i], i == 0 ? fs * 1.1f : fsS + 1.0f * k);
          if (st[i][0]) text(tx + w + 10.0f * k, yc, dim, st[i], fsS - 0.5f * k);
          if (i == 2) {      // ojo: visible / oculto
              const ImVec2 e0(leftW - 34.0f * k, y + 3.0f * k), e1(leftW - 8.0f * k, y + rowH - 3.0f * k); const bool he = hit(18 + i, e0, e1);
              if (he) dl->AddRectFilled(e0, e1, IM_COL32(255, 255, 255, 30), 3.0f * k);
              const ImVec2 o((e0.x + e1.x) * 0.5f, yc); const ImU32 c = off ? IM_COL32(100, 100, 104, 255) : ink;
              dl->AddCircle(o, 6.0f * k, c, 16, 1.5f * k); if (!off) dl->AddCircleFilled(o, 2.6f * k, c, 10); else dl->AddLine(ImVec2(o.x - 7.0f * k, o.y + 6.0f * k), ImVec2(o.x + 7.0f * k, o.y - 6.0f * k), c, 1.5f * k);
          } }
      for (int j = 0; j < nSub; j++) {      // jugadores: el del Editar (ojo = cargarlo / sacarlo) y los propios (clic = elegirlo, ojo = mostrar / ocultar)
          const int q = pjIdx[j]; const float y = y0 + rowH * (float)(1 + nRows + j), yc = y + rowH * 0.5f;
          const ImVec2 p0(0, y), p1(leftW - 36.0f * k, y + rowH); const bool hv = hit(3000 + q, p0, p1);
          const bool isSel = s_sel == 3 && PJ.sel == q, inMulti = s_sel == 3 && ((s_pjMulti >> q) & 1u) && PJ.sel != q;
          if (inMulti) dl->AddRectFilled(p0, ImVec2(leftW, y + rowH), IM_COL32(240, 138, 36, 40));
          if (!isSel && hv) dl->AddRectFilled(p0, ImVec2(leftW, y + rowH), IM_COL32(255, 255, 255, 14));
          const float ind = 56.0f * k; dl->AddRectFilled(ImVec2(ind - 12.0f * k, yc - 1.0f), ImVec2(ind - 4.0f * k, yc), IM_COL32(90, 90, 94, 255));
          const bool off = PJ.hidden[q];
          glyph(3, ImVec2(ind + 3.0f * k, yc), 5.0f * k, off ? IM_COL32(100, 100, 104, 255) : IM_COL32(190, 190, 196, 255));
          char nmj[64]; snprintf(nmj, sizeof nmj, "%d.  %.30s", q + 1, PJ.name[q]);
          if (j == 0 && s_pjScroll > 0) text(leftW - 52.0f * k, yc, dim, "\xE2\x96\xB2", fsS - 2.0f * k);                       // hay mas arriba
          if (j == nSub - 1 && s_pjScroll + nSub < nPjL) text(leftW - 52.0f * k, yc, dim, "\xE2\x96\xBC", fsS - 2.0f * k);     // hay mas abajo
          const float w = text(ind + 15.0f * k, yc, off ? dim : ink, nmj, fsS);
          const char* stj = PJ.st[q] == 1 || PJ.st[q] == 2 ? "cargando..." : PJ.st[q] == 4 ? "no carg\xC3\xB3" : PJ.st[q] == 5 ? "sacando..." : "";
          if (stj[0]) text(ind + 15.0f * k + w + 8.0f * k, yc, dim, stj, fsS - 0.5f * k);
          const ImVec2 e0(leftW - 34.0f * k, y + 3.0f * k), e1(leftW - 8.0f * k, y + rowH - 3.0f * k); const bool he = hit(3100 + q, e0, e1);
          if (he) dl->AddRectFilled(e0, e1, IM_COL32(255, 255, 255, 30), 3.0f * k);
          const ImVec2 o((e0.x + e1.x) * 0.5f, yc); const ImU32 c = off ? IM_COL32(100, 100, 104, 255) : ink;
          dl->AddCircle(o, 6.0f * k, c, 16, 1.5f * k); if (!off) dl->AddCircleFilled(o, 2.6f * k, c, 10); else dl->AddLine(ImVec2(o.x - 7.0f * k, o.y + 6.0f * k), ImVec2(o.x + 7.0f * k, o.y - 6.0f * k), c, 1.5f * k);
      }
      {   // grupo de luces de la escena grafica (todas: sol, focos, puntuales, locales)
          int nOn = 0; for (int i = 0; i < s_nLights; i++) if (s_lights[i].on) nOn++;
          const float yg = yGrp, yc = yg + rowH * 0.5f; char gt[64];
          dl->AddLine(ImVec2(0, yg), ImVec2(leftW, yg), IM_COL32(60, 60, 64, 255), 1.0f);
          { const ImVec2 g0(0, yg), g1(leftW, yg + rowH); const bool hg = hit(15, g0, g1);      // toda la fila del grupo pliega / despliega las luces
            if (hg) dl->AddRectFilled(g0, g1, IM_COL32(255, 255, 255, 14)); arrow(16.5f * k, yc, s_foldA[1], hg ? ink : dim); }
          glyph(10, ImVec2(36.0f * k, yc), 6.5f * k, IM_COL32(255, 206, 110, 255));
          const float w = text(49.0f * k, yc, ink, "Luces", fs * 1.1f); snprintf(gt, sizeof gt, TRS("%d prendidas de %d"), nOn, s_info.lights); text(49.0f * k + w + 10.0f * k, yc, dim, gt, fsS - 0.5f * k);
          for (int r = 0; r < shownL && sc + r < s_nLights; r++) { const int i = sc + r; const MotorLight& L = s_lights[i]; const float y = yL0 + (float)r * rowL, yc2 = y + rowL * 0.5f; const ImVec2 p0(0, y), p1(leftW, y + rowL); const bool hv = hit(300 + i, p0, p1);
              if (s_sel != 4 + i && hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 14));
              const float mxc = std::max(L.color[0], std::max(L.color[1], L.color[2])); const float inv = mxc > 1e-6f ? 1.0f / mxc : 0.0f;
              const ImU32 sw = L.on ? IM_COL32((int)(std::clamp(L.color[0] * inv, 0.0f, 1.0f) * 255.0f), (int)(std::clamp(L.color[1] * inv, 0.0f, 1.0f) * 255.0f), (int)(std::clamp(L.color[2] * inv, 0.0f, 1.0f) * 255.0f), 255) : IM_COL32(70, 70, 74, 255);
              glyph(20 + std::clamp(L.kind, 0, 4), ImVec2(42.0f * k, yc2), 5.5f * k, L.on ? sw : IM_COL32(96, 96, 100, 255));      // icono segun la clase, del color de la luz
              char nm2[48]; if (L.name[0]) snprintf(nm2, sizeof nm2, "%.26s", L.name); else snprintf(nm2, sizeof nm2, TRS("%s sin nombre"), TRS(kKind[L.kind]));
              text(55.0f * k, yc2, L.on ? ink : dim, nm2, fsS);
              const ImVec2 e0(leftW - 34.0f * k, y + 2.0f * k), e1(leftW - 8.0f * k, y + rowL - 2.0f * k); const bool he = hit(400 + i, e0, e1);
              if (he) dl->AddRectFilled(e0, e1, IM_COL32(255, 255, 255, 30), 3.0f * k);
              const ImVec2 o((e0.x + e1.x) * 0.5f, yc2); const ImU32 c = L.on ? ink : IM_COL32(100, 100, 104, 255);
              dl->AddCircle(o, 5.0f * k, c, 14, 1.4f * k); if (L.on) dl->AddCircleFilled(o, 2.2f * k, c, 10); else dl->AddLine(ImVec2(o.x - 6.0f * k, o.y + 5.0f * k), ImVec2(o.x + 6.0f * k, o.y - 5.0f * k), c, 1.4f * k); }
          if (s_nLights > shownL && !fL) { char mm[64]; snprintf(mm, sizeof mm, TRS("%d a %d de %d  \xC2\xB7  rueda del mouse: ver m\xC3\xA1s"), sc + 1, std::min(s_nLights, sc + shownL), s_nLights); text(34.0f * k, yL0 + (float)shownL * rowL + 11.0f * k, dim, mm, fsS);
              const float tr0 = yL0, tr1 = yL0 + (float)shownL * rowL, th = (tr1 - tr0) * (float)shownL / (float)s_nLights, ty0 = tr0 + (tr1 - tr0 - th) * (float)sc / (float)std::max(1, s_nLights - shownL);
              dl->AddRectFilled(ImVec2(leftW - 5.0f * k, ty0), ImVec2(leftW - 2.0f * k, ty0 + th), IM_COL32(110, 110, 114, 255), 1.5f * k); }
          if (s_info.lights == 0 && !fL) text(34.0f * k, yL0 + 11.0f * k, dim, "La escena no tiene luces cargadas.", fsS);
      }
      { const float tx = dx + (Q.dock == 1 ? 1.0f : Q.dock == 0 ? -1.0f : 0.0f) * (1.0f - eo) * leftW; anim(v0, tx, 0.0f, eo * std::min(1.0f, aL * 1.6f) * (pMoving ? 0.85f : 1.0f)); clipShift(c0, tx); } hitOff = false;
      } mx += dx; pOffC = 0.0f; pMoving = false; };
    // ---------------- inspector ----------------
    auto drawInsp = [&]() { const PLay& Q = P[1]; s_inZone.store(-1); if (Q.V <= 0.5f) return;
      const float rightW = Q.W, y0 = Q.ya, y1 = Q.yb, aR = Q.a; const float slide = Q.dock == 0 ? -(Q.W - Q.V) : Q.dock == 1 ? (Q.W - Q.V) : 0.0f, dx = Q.x + slide - (ds.x - Q.W);
      pMoving = s_mvOn && s_mvId == 2; mx -= dx; pOffC = dx;
      { float vx0, vx1; pVis(Q, vx0, vx1); s_inZone.store(nz); zone(ImVec2(vx0, y0), ImVec2(vx1, y1)); }
      { const int c0 = clipBegin(); const int v0 = dl->VtxBuffer.Size; int v1 = v0; const float x0 = ds.x - rightW; const ImVec2 a(x0, y0), b(ds.x, y1); hitOff = aR < 0.999f || pMoving;
      dl->AddRectFilled(a, b, cPanel); dl->AddLine(ImVec2(x0, y0), ImVec2(x0, y1), cLine, 1.0f); if (Q.dock != 1) dl->AddLine(ImVec2(ds.x - 1.0f, y0), ImVec2(ds.x - 1.0f, y1), cLine, 1.0f);
      if (Q.dock == 2) dl->AddRect(a, b, IM_COL32(70, 70, 76, 255), 0.0f, 0, 1.0f);
      clipPush(a, b);
      dl->AddRectFilled(a, ImVec2(ds.x, y0 + rowH), cHead); text(x0 + 12.0f * k, y0 + rowH * 0.5f, dim, "INSPECTOR", fsS);
      pHead(1, x0, ds.x, y0);
      xBtn(993, ds.x - 16.0f * k, y0 + rowH * 0.5f);
      const char* const title[4] = { "Estudio  \xC2\xB7  Render", "C\xC3\xA1mara", "Grilla de piso", "Jugador" };
      float y = y0 + rowH; dl->AddRectFilled(ImVec2(x0 + 1.0f, y), ImVec2(ds.x, y + rowH + 4.0f * k), IM_COL32(45, 45, 48, 255)); { char tl[64]; const MotorLight* SL = s_sel >= 4 && s_sel - 4 < s_nLights ? &s_lights[s_sel - 4] : nullptr;
        if (SL) snprintf(tl, sizeof tl, "%s  \xC2\xB7  %.30s", TRS(kKind[SL->kind]), SL->name[0] ? SL->name : TRS("sin nombre")); else snprintf(tl, sizeof tl, "%s", title[std::min(s_sel, 3)]);
        text(x0 + 12.0f * k, y + (rowH + 4.0f * k) * 0.5f, ink, tl, fs + 1.0f * k); } y += rowH + 4.0f * k;
      // (07:50) contenido corrible: rueda encima del Inspector, barra a la derecha (arrastrable o clic en el riel)
      const float yC = y, viewH = std::max(10.0f, y1 - yC);
      if (s_sel != s_inSelWas) { s_inSelWas = s_sel; s_inScroll = 0.0f; }
      { const int wd = s_inWheel.exchange(0);      // (18:02) rueda sobre la lista de clips o de gestos: recorre esa lista; en el resto, el Inspector
        auto in4 = [&](const float* r) { return r[2] > r[0] && mx >= r[0] && mx <= r[2] && my >= r[1] && my <= r[3]; };
        if (wd != 0) { const int rows = (wd > 0 ? -2 : 2) * std::max(1, abs(wd) / 120);
          if (in4(s_clR)) s_clWheel += rows; else if (in4(s_fcR)) s_pjFaceScroll += rows; else s_inScroll -= (float)wd / 120.0f * 60.0f * k; }
        s_clR[2] = s_fcR[2] = -1.0f; }
      s_inScroll = std::clamp(s_inScroll, 0.0f, s_inMax);
      clipPush(ImVec2(x0, yC), b); hitY0 = yC; hitY1 = y1;
      y += 8.0f * k - s_inScroll; const float yTop = y;
      const float lx = x0 + 12.0f * k, fx0 = x0 + 12.0f * k, fx1 = ds.x - 12.0f * k - (s_inMax > 0.0f ? 6.0f * k : 0.0f);
      v1 = dl->VtxBuffer.Size;          // desde aca: contenido de lo marcado (fundido al cambiar)
      // (17/11 07:18, Agustin: "mejora las tarjetas, el diseno mas intuitivo") cada seccion del Inspector es una tarjeta: borde redondeado,
      // fondo apenas mas claro y franja de color a la izquierda del titulo; se cierra cuando empieza la siguiente o al final del panel
      float cardY0 = -1.0f;
      auto cardClose = [&]() { if (cardY0 < 0.0f) return; const ImVec2 ca(x0 + 5.0f * k, cardY0), cb(ds.x - 6.0f * k, std::max(cardY0 + 22.0f * k * rf, y - 2.0f * k));
          dl->AddRectFilled(ca, cb, IM_COL32(255, 255, 255, 6), 6.0f * k); dl->AddRect(ca, cb, IM_COL32(255, 255, 255, 24), 6.0f * k, 0, 1.0f);
          dl->AddRectFilled(ImVec2(ca.x, ca.y + 4.0f * k), ImVec2(ca.x + 2.5f * k, ca.y + 18.0f * k * rf), (acc & 0x00ffffffu) | 0xb0000000u, 1.5f * k); cardY0 = -1.0f; y += 6.0f * k; };
      auto label = [&](const char* t) -> bool { cardClose(); cardY0 = y; y += 2.0f * k; return secHead(x0 + 8.0f * k, ds.x - 9.0f * k, y, t, (uint32_t)std::min(s_sel, 4)); };      // (15/11 p) plegable
      auto value = [&](const char* nmv, const char* v) { text(lx, y + rowH * 0.5f, dim, nmv, fsS + 1.0f * k); const ImVec2 p0(x0 + rightW * 0.42f, y + 3.0f * k), p1(fx1, y + rowH - 3.0f * k); dl->AddRectFilled(p0, p1, cField, 3.0f * k); text(p0.x + 8.0f * k, y + rowH * 0.5f, ink, v, fsS + 1.0f * k); y += rowH; };
      auto button = [&](int id, const char* t) { const ImVec2 p0(fx0, y + 4.0f * k), p1(fx1, y + rowH + 6.0f * k); const bool hv = hit(id, p0, p1); dl->AddRectFilled(p0, p1, hv ? IM_COL32(86, 86, 92, 255) : IM_COL32(66, 66, 70, 255), 3.0f * k); dl->AddRect(p0, p1, cLine, 3.0f * k); textC(p0, p1, ink, t, fs); y += rowH + 14.0f * k; };
      if (s_sel == 0) {
          if (label("VISTA DEL RENDER")) {
          const int n = MotorViewCount(0), cur = MotorViewGet(0); const float rh = 25.0f * k * rf;
          dl->AddRectFilled(ImVec2(fx0, y), ImVec2(fx1, y + (float)n * rh), cField, 3.0f * k);
          for (int i = 0; i < n; i++) { const ImVec2 p0(fx0, y + (float)i * rh), p1(fx1, y + (float)(i + 1) * rh); const bool hv = hit(100 + i, p0, p1);
              if (i == cur) dl->AddRectFilled(p0, p1, IM_COL32(240, 138, 36, 60)); else if (hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 18));
              const ImVec2 o(p0.x + 14.0f * k, (p0.y + p1.y) * 0.5f); dl->AddCircle(o, 5.0f * k, i == cur ? acc : IM_COL32(110, 110, 114, 255), 14, 1.4f * k); if (i == cur) dl->AddCircleFilled(o, 2.6f * k, acc, 10);
              text(p0.x + 28.0f * k, o.y, i == cur ? IM_COL32(255, 214, 170, 255) : ink, ViewTr(0, i), fsS + 1.5f * k); }
          dl->AddRect(ImVec2(fx0, y), ImVec2(fx1, y + (float)n * rh), cLine, 3.0f * k); y += (float)n * rh + 12.0f * k; }
          for (int r = 1; r <= 2; r++) { if (!label(r == 1 ? "ALAMBRE" : "PANTALLA PARTIDA")) continue; const int m = std::min(4, MotorViewCount(r)); const char* nms[4] = { "", "", "", "" }; for (int i = 0; i < m; i++) nms[i] = ViewTr(r, i);
              if (r == 1 && m == 3) nms[2] = "Con teselaci\xC3\xB3n";
              seg(100 + r * 16, ImVec2(fx0, y), ImVec2(fx1, y + rowH), m, MotorViewGet(r), nms); y += rowH + 12.0f * k;
              // (17/11 06:59, Agustin: "distintos colores en las mallas para entender de que se tratan") motorlink.cpp g_motorWireCol
              if (r == 1 && MotorViewGet(1) > 0) { static const char* const cn[2] = { "Con su textura", "Un color por malla" }; seg(3530, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 2, g_motorWireCol.load() ? 1 : 0, cn); y += rowH + 12.0f * k; } }
          // (17/11, Agustin 03:11: "vistas debugs para probar los torsos, partes de cuerpos") las vistas de diagnostico del cuerpo que ya
          // tiene el mod (fila 519, pes_debug_cuerpo.hlsli) y el esqueleto (fila 426), a mano desde el Motor
          if (label("CUERPO (PRUEBAS)")) {
              const int tz = (int)(g_cfg.live[519][2] + 0.5f);
              text(lx, y + rowH * 0.5f, dim, "Torso a trav\xC3\xA9s de la ropa", fsS); y += rowH - 4.0f * k;
              { const char* const nms[2] = { "No", "S\xC3\xAD" }; seg(1430, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 2, g_cfg.live[519][0] > 0.5f ? 1 : 0, nms); y += rowH + 6.0f * k; }
              text(lx, y + rowH * 0.5f, dim, "Piezas de colores y costuras", fsS); y += rowH - 4.0f * k;
              { const char* const nms[3] = { "No", "Piezas", "Solo textura" }; seg(1434, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 3, std::clamp(tz, 0, 2), nms); y += rowH + 6.0f * k; }
              text(lx, y + rowH * 0.5f, dim, "Esqueleto (huesos)", fsS); y += rowH - 4.0f * k;
              { const char* const nms[3] = { "No", "S\xC3\xAD", "Con nombres" }; seg(1440, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 3, g_cfg.live[426][0] > 1.5f ? 2 : g_cfg.live[426][0] > 0.5f ? 1 : 0, nms); y += rowH + 6.0f * k; }
              // (17/11 b, Agustin 04:01: "permitirme mostrar solo los huesos de la cara, del cuerpo y del uniforme") y426: 0 todos, 1 cara
              // (esqueleto de la cara y skf_*), 2 cuerpo (sk_* y manos), 3 ropa (el resto del esqueleto del cuerpo: huesos de ayuda dsk_* y otros)
              if (g_cfg.live[426][0] > 0.5f) { const char* const nms[4] = { "Todos", "Cara", "Cuerpo", "Ropa" }; seg(1446, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 4, std::clamp((int)(g_cfg.live[426][1] + 0.5f), 0, 3), nms); y += rowH + 6.0f * k; }
              y += 6.0f * k; }
          if (label("ESCENA GR\xC3\x81" "FICA DEL MOTOR")) { char b1[32];      // lo que el motor tiene cargado ahora (leido de sus listas)
              snprintf(b1, sizeof b1, "%d", s_info.scenes); value("Escenas", b1); snprintf(b1, sizeof b1, "%d", s_info.objects); value("Objetos", b1);
              snprintf(b1, sizeof b1, "%d", s_info.models); value("Modelos", b1); snprintf(b1, sizeof b1, "%d", s_info.lights); value("Luces", b1); }
          // (17/11 18:20, Agustin: "permitir cargar luces") juego de luces del juego (cpk_dat/common/render/model/light/*.xml): el juego
          // tiene uno activo a la vez (una pila: el nuevo tapa al del Editar y al sacarlo vuelve el de antes). motorpj.cpp MotorLightSetReq
          if (label("JUEGO DE LUCES")) { int st = 0; const int w = MotorLightSetGet(&st);
              const char* const ln[5] = { "Editar", "Default", "Cara", "Botines", "MenÃº" }; seg(3600, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 5, w, ln); y += rowH + 6.0f * k;
              const char* stt = w == 0 ? "Las luces de siempre del Editar." : st == 2 ? "Activo." : st == 3 ? "El juego no lo cargÃ³ (mirÃ¡ el registro)." : "Cargando...";
              text(lx, y + 9.0f * k, st == 3 ? IM_COL32(255, 150, 120, 255) : dim, stt, fsS); y += 20.0f * k;
              text(lx, y + 9.0f * k, dim, "Archivos del juego: Default, EditFace, EditSpike, MatchMenu.", fsS); y += 26.0f * k; }
      } else if (s_sel == 1) {
          if (label("MODO")) { const char* const nms[2] = { "Libre", "Del juego" }; seg(200, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 2, camFree ? 0 : 1, nms); y += rowH + 12.0f * k; }
          float pos[3] = {}, yaw = 0, pitch = 0; char b1[48];
          if (camFree && FreeCamGetPose(pos, &yaw, &pitch)) {
              if (label("TRANSFORMACI\xC3\x93N")) {
              snprintf(b1, sizeof b1, "%.2f m", pos[0]); value("Posici\xC3\xB3n X", b1); snprintf(b1, sizeof b1, "%.2f m", pos[1]); value("Posici\xC3\xB3n Y", b1); snprintf(b1, sizeof b1, "%.2f m", pos[2]); value("Posici\xC3\xB3n Z", b1);
              snprintf(b1, sizeof b1, "%.1f\xC2\xB0", yaw * 57.29578f); value("Giro", b1); snprintf(b1, sizeof b1, "%.1f\xC2\xB0", pitch * 57.29578f); value("Inclinaci\xC3\xB3n", b1); }
              y += 8.0f * k; if (label("LENTE")) { snprintf(b1, sizeof b1, "%.1f\xC2\xB0", FreeCamFovNow() * 57.29578f); value("Campo de visi\xC3\xB3n", b1); }
              y += 8.0f * k; if (label("MIRAR DESDE"))
              { const char* const v1[3] = { "Derecha", "Arriba", "Frente" }; const char* const v2[3] = { "Izquierda", "Abajo", "Atr\xC3\xA1s" };
                seg(230, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 3, -1, v1); y += rowH + 4.0f * k; seg(233, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 3, -1, v2); y += rowH + 10.0f * k;
                button(202, "Volver a la pose inicial"); }
              const char* const ay[4] = { "Bot\xC3\xB3n derecho: mirar y volar (W A S D)", "Bot\xC3\xB3n del medio: panear", "Alt + clic izquierdo: orbitar", "Rueda: acercar y alejar" };
              for (int i = 0; i < 4; i++) text(lx, y + 8.0f * k + (float)i * 17.0f * k, dim, ay[i], fsS);
          } else text(lx, y + 10.0f * k, dim, "La c\xC3\xA1mara la maneja el juego.", fsS + 1.0f * k);
      } else if (s_sel == 2) {
          // (15/11 o, Agustin 05:08: "la grilla no tiene opciones") todo se cambia desde aca y se guarda en el ini
          if (label("VISIBLE")) { const char* const nms[2] = { "S\xC3\xAD", "No" }; seg(210, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 2, grid ? 0 : 1, nms); y += rowH + 12.0f * k; }
          if (label("TAMA\xC3\x91O (LADO)")) { static const char* const gn[4] = { "M\xC3\xA1xima", "40 m", "100 m", "150 m" }; static const int gv[4] = { 0, 40, 100, 150 }; int c = -1; for (int q = 0; q < 4; q++) if (g_cfg.motorGridSize == gv[q]) c = q;
              seg(780, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 4, c, gn); y += rowH + 12.0f * k; }
          if (label("CELDA")) { static const char* const cn[4] = { "25 cm", "50 cm", "1 m", "2 m" }; static const float cv[4] = { 0.25f, 0.5f, 1.0f, 2.0f }; int c = -1; for (int q = 0; q < 4; q++) if (fabsf(g_cfg.motorGridCell - cv[q]) < 1.0e-3f) c = q;
              seg(784, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 4, c, cn); y += rowH + 12.0f * k; }
          if (label("L\xC3\x8DNEA FUERTE")) { static const char* const mn[3] = { "Sin", "Cada 5", "Cada 10" }; const int c = g_cfg.motorGridMajor == 0 ? 0 : g_cfg.motorGridMajor == 10 ? 2 : g_cfg.motorGridMajor == 5 ? 1 : -1;
              seg(788, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 3, c, mn); y += rowH + 12.0f * k; }
          if (label("INTENSIDAD")) { float v = (float)g_cfg.motorGridAlpha; const float nx = fx1 - 78.0f * k; bool ch = slider(2503, ImVec2(fx0, y), ImVec2(nx - 8.0f * k, y + rowH), v, 0.0f, 200.0f, IM_COL32(150, 150, 156, 255));
              ch |= numField(2008, ImVec2(nx, y + 2.0f * k), ImVec2(fx1, y + rowH - 2.0f * k), v, 0.5f, 0.0f, 200.0f, "%.0f", " %");
              if (ch) { g_cfg.motorGridAlpha = (int)(v + 0.5f); s_gridDirty = true; } y += rowH + 12.0f * k; }
          if (label("EJES (X ROJO, Z AZUL)")) { const char* const nms[2] = { "Se ven", "Ocultos" }; seg(792, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 2, g_cfg.motorGridAxes ? 0 : 1, nms); y += rowH + 12.0f * k; }
          button(799, "Grilla como vino");
      } else if (s_sel == 3) {
          // (17/11, Agustin 03:00 / 03:06: "que pueda mover el jugador con opciones", "opciones de mover rotar", "escalar", "la pestana
          // de animaciones") motorjug.cpp: lugar, giro y tamano del jugador y sus animaciones (poses y festejos del juego)
          // (17/11 etapa 2, Agustin 04:35 / 04:37: jugadores propios, "importar jugadores por su id", "cargar el jugador base de konami")
          // motorpj.cpp: lista de jugadores del Motor; el elegido es el que mueven los campos de abajo, los iconos del piso y las teclas
          // (17/11 06:36..06:38, Agustin: "mejorar los jugadores sus opciones las formas de mover, el limite de jugadores", "que todos puedan
          // usar gestos distintos y animaciones", "opciones de gestos de cara") hasta 32 jugadores; animacion y gesto propios de cada uno
          static MotorPjInfo PI; MotorPjInfoGet(&PI);
          int nUsed = 0; for (int q = 0; q < kMotorPjMax; q++) if (PI.st[q]) nUsed++;
          const int qs = MotorPjSel();
          auto arrowRow = [&](int idL, int idR, const char* nmv, const char* val, bool on) {
              text(lx, y + rowH * 0.5f, dim, nmv, fsS + 1.0f * k);
              const float bx0 = x0 + rightW * 0.42f, bw = rowH - 6.0f * k;
              const ImVec2 l0(bx0, y + 3.0f * k), l1(bx0 + bw, y + rowH - 3.0f * k), r0(fx1 - bw, y + 3.0f * k), r1(fx1, y + rowH - 3.0f * k);
              const bool hl = hit(idL, l0, l1), hr = hit(idR, r0, r1);
              dl->AddRectFilled(ImVec2(l1.x + 3.0f * k, l0.y), ImVec2(r0.x - 3.0f * k, r1.y), cField, 3.0f * k);
              dl->AddRectFilled(l0, l1, hl ? IM_COL32(86, 86, 92, 255) : IM_COL32(66, 66, 70, 255), 3.0f * k); dl->AddRectFilled(r0, r1, hr ? IM_COL32(86, 86, 92, 255) : IM_COL32(66, 66, 70, 255), 3.0f * k);
              arrow((l0.x + l1.x) * 0.5f, (l0.y + l1.y) * 0.5f, 2.0f, ink); arrow((r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f, 0.0f, ink);
              dl->PushClipRect(ImVec2(l1.x + 4.0f * k, l0.y), ImVec2(r0.x - 4.0f * k, r1.y), true);
              text(l1.x + 9.0f * k, y + rowH * 0.5f, on ? ink : dim, val, fsS);
              dl->PopClipRect();
              y += rowH; };
          auto listRow = [&](int id, const char* t, bool cur) {
              const ImVec2 r0(fx0, y + 1.0f * k), r1(fx1, y + rowH * 0.86f); const bool hv = hit(id, r0, r1);
              if (id >= 3400 && id < 3408) { const int c = s_pjRowClip[id - 3400];      // (18:01) clic derecho = favorito; marca naranja a la derecha
                  if (c >= 0 && rEdge && !hitOff && inside(r0, r1)) { FavToggle(c); Note(FavHas(c) ? "Agregado a Favoritos" : "Sacado de Favoritos"); }
                  if (c >= 0 && FavHas(c)) { const ImVec2 sc(r1.x - 9.0f * k, (r0.y + r1.y) * 0.5f); const float rr = 4.5f * k;
                      dl->AddQuadFilled(ImVec2(sc.x, sc.y - rr), ImVec2(sc.x + rr, sc.y), ImVec2(sc.x, sc.y + rr), ImVec2(sc.x - rr, sc.y), acc); } }
              if (cur) dl->AddRectFilled(r0, r1, IM_COL32(64, 52, 40, 255), 3.0f * k); else if (hv) dl->AddRectFilled(r0, r1, IM_COL32(255, 255, 255, 18), 3.0f * k);
              if (cur) dl->AddRectFilled(r0, ImVec2(r0.x + 3.0f * k, r1.y), acc);
              dl->PushClipRect(r0, r1, true); text(r0.x + 10.0f * k, (r0.y + r1.y) * 0.5f, cur ? ink : dim, t, fsS); dl->PopClipRect();
              y += rowH * 0.86f; };
          if (label("JUGADORES DEL MOTOR")) {
              // la lista va en el panel Escena (grupo Jugadores); aca: el elegido (con su X para sacarlo) y agregar
              { const int q = PI.sel; const ImVec2 p0(fx0, y + 2.0f * k), p1(fx1, y + rowH);
                dl->AddRectFilled(p0, p1, q >= 0 ? IM_COL32(64, 52, 40, 255) : cField, 3.0f * k); if (q >= 0) dl->AddRect(p0, p1, acc, 3.0f * k, 0, 1.5f * k);
                char nmq[64]; if (q >= 0) snprintf(nmq, sizeof nmq, "%d.  %.30s", q + 1, PI.name[q]); else snprintf(nmq, sizeof nmq, "%s", TRS("Ning\xC3\xBAn jugador elegido"));
                text(p0.x + 9.0f * k, (p0.y + p1.y) * 0.5f, ink, nmq, fsS + 1.0f * k);
                if (q >= 0) { const float dx0 = fx1 - rowH + 6.0f * k; const ImVec2 d0(dx0, p0.y + 3.0f * k), d1(p1.x - 3.0f * k, p1.y - 3.0f * k); const bool hd = hit(3200 + q, d0, d1);
                    if (hd) dl->AddRectFilled(d0, d1, IM_COL32(120, 52, 46, 255), 3.0f * k);
                    const ImVec2 c((d0.x + d1.x) * 0.5f, (d0.y + d1.y) * 0.5f); const float r = 4.0f * k; const ImU32 xc = hd ? ink : dim;
                    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), xc, 1.5f * k); dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), xc, 1.5f * k); }
                y += rowH + 3.0f * k;
                if (q >= 0) {      // (07:23) tarjeta del elegido: de donde sale, que animacion tiene y como esta
                    static const char* const kK[3] = { "por id", "jugador base", "igual al del Editar" };
                    char l1[96], l2[96]; snprintf(l1, sizeof l1, "%s  \xC2\xB7  id %llu  \xC2\xB7  %s", TRS(kK[std::clamp(PI.kind[q], 0, 2)]), (unsigned long long)(PI.id[q] >> 32 ? PI.id[q] >> 32 : PI.id[q] & 0xffff),
                        TRS(PI.hidden[q] ? "oculto" : PI.st[q] == 3 ? "listo" : PI.st[q] == 4 ? "no carg\xC3\xB3" : "cargando"));
                    if (PI.clip[q] >= 0) snprintf(l2, sizeof l2, "%s%s", MotorClipFile(PI.clip[q]), PI.paused[q] ? TRS("  (en pausa)") : ""); else snprintf(l2, sizeof l2, "%s", TRS("parado (el de siempre)"));
                    dl->PushClipRect(ImVec2(fx0, y), ImVec2(fx1, y + 40.0f * k), true);
                    text(fx0 + 9.0f * k, y + 9.0f * k, dim, l1, fsS - 0.5f * k); text(fx0 + 9.0f * k, y + 27.0f * k, IM_COL32(200, 180, 150, 255), l2, fsS - 0.5f * k);
                    dl->PopClipRect(); y += 38.0f * k; } }
              y += 6.0f * k;
              text(lx, y + rowH * 0.5f, dim, "Id del jugador", fsS + 1.0f * k);
              numField(2110, ImVec2(x0 + rightW * 0.42f, y + 3.0f * k), ImVec2(fx1, y + rowH - 3.0f * k), s_pjIdF, 1.0f, 1.0f, 9999999.0f, "%.0f", ""); y += rowH;
              { const char* const nms[2] = { "Oculto", "A la vista" }; text(lx, y + rowH * 0.5f, dim, "Jugador del Editar", fsS + 1.0f * k);      // (07:47)
                seg(3370, ImVec2(x0 + rightW * 0.42f, y), ImVec2(fx1, y + rowH), 2, MotorVistaEditorHidden() ? 0 : 1, nms); y += rowH + 6.0f * k; }
              button(1480, "Agregar por id");
              button(1481, "Agregar el jugador base");
              button(1482, "Agregar igual al del Editar");
              { char cn[64]; snprintf(cn, sizeof cn, TRS("%d de %d jugadores"), nUsed, kMotorPjMax); text(lx, y + 9.0f * k, dim, cn, fsS); y += 20.0f * k; }
              if (PI.dead) text(lx, y + 9.0f * k, IM_COL32(255, 150, 120, 255), "Hubo un error dentro del juego: jugadores propios apagados hasta reiniciar.", fsS);
              else if (PI.msg[0]) text(lx, y + 9.0f * k, dim, PI.msg, fsS);
              else if (!PI.ticking) text(lx, y + 9.0f * k, dim, "Esperando al juego (el visor del Editar no avanza).", fsS);
              y += 26.0f * k;
          }
          MotorJugState J; MotorJugGet(&J); const bool ownPj = qs >= 0;
          // (17/11 03:23, Agustin: "puedes sacar al jugador del editor, y permitirme opciones correctas") solo los jugadores propios
          if (!ownPj) { text(lx, y + 9.0f * k, dim, "Agreg\xC3\xA1 un jugador, o hac\xC3\xA9 clic en uno en la vista", fsS); y += 20.0f * k; text(lx, y + 9.0f * k, dim, "o en el panel Escena (grupo Jugadores) para elegirlo.", fsS); y += 26.0f * k; }
          else {
              float v[5] = { J.pos[0], J.pos[1], J.pos[2], J.yawDeg, J.scale * 100.0f }; bool ch = false;
              auto fld = [&](int id, const char* nmv, float& val, float step, float lo, float hi, const char* fmt, const char* unit) { text(lx, y + rowH * 0.5f, dim, nmv, fsS + 1.0f * k);
                  const bool c2 = numField(id, ImVec2(x0 + rightW * 0.42f, y + 3.0f * k), ImVec2(fx1, y + rowH - 3.0f * k), val, step, lo, hi, fmt, unit); y += rowH; return c2; };
              if (label("POSICI\xC3\x93N")) { ch |= fld(2100, "X (costado)", v[0], 0.01f, -100.0f, 100.0f, "%.2f", " m"); ch |= fld(2101, "Y (altura)", v[1], 0.01f, -20.0f, 50.0f, "%.2f", " m"); ch |= fld(2102, "Z (fondo)", v[2], 0.01f, -100.0f, 100.0f, "%.2f", " m"); y += 6.0f * k; }
              if (label("GIRO")) { ch |= fld(2103, "Sobre el piso", v[3], 0.5f, -720.0f, 720.0f, "%.1f", "\xC2\xB0"); y += 6.0f * k; }
              if (label("TAMA\xC3\x91O")) { ch |= fld(2104, "Escala", v[4], 0.5f, 10.0f, 500.0f, "%.0f", " %"); y += 6.0f * k; }
              if (ch) PjGroupSet(v, v[3], v[4] / 100.0f);
              if (label("MOVER")) {
                  { const char* const nms[2] = { "Libre", "A la grilla" }; text(lx, y + rowH * 0.5f, dim, "Al arrastrar", fsS + 1.0f * k);
                    seg(3303, ImVec2(x0 + rightW * 0.42f, y), ImVec2(fx1, y + rowH), 2, s_pjSnap ? 1 : 0, nms); y += rowH + 8.0f * k; }
                  // (17/11 07:16, Agustin: "permite la opcion de seguir la camara") la camara se corre junto con el jugador elegido
                  // (17/11 20:46, Agustin: "una opcion de camara para seguir el rostro en animaciones") "La cara": sigue la raiz animada y mira la cabeza
                  { const char* const nms[3] = { "Quieta", "Sigue", "La cara" }; text(lx, y + rowH * 0.5f, dim, "C\xC3\xA1mara", fsS + 1.0f * k);
                    seg(3305, ImVec2(x0 + rightW * 0.42f, y), ImVec2(fx1, y + rowH), 3, s_camFollow, nms); y += rowH + 8.0f * k; }
                  button(1400, "Volver al centro");
                  button(3300, "Duplicar (mismo jugador al lado)");
                  {   // (07:23) copiar / pegar entre jugadores, dos botones en una fila
                      const float mxp = (fx0 + fx1) * 0.5f; const ImVec2 a0(fx0, y + 4.0f * k), a1(mxp - 3.0f * k, y + rowH + 6.0f * k), b0(mxp + 3.0f * k, y + 4.0f * k), b1(fx1, y + rowH + 6.0f * k);
                      const bool ha = hit(3360, a0, a1), hb = hit(3361, b0, b1);
                      dl->AddRectFilled(a0, a1, ha ? IM_COL32(86, 86, 92, 255) : IM_COL32(66, 66, 70, 255), 3.0f * k); dl->AddRectFilled(b0, b1, hb ? IM_COL32(86, 86, 92, 255) : s_pjCb.ok ? IM_COL32(66, 66, 70, 255) : IM_COL32(52, 52, 55, 255), 3.0f * k);
                      textC(a0, a1, ink, "Copiar (Ctrl+C)", fsS); textC(b0, b1, s_pjCb.ok ? ink : dim, "Pegar (Ctrl+V)", fsS); y += rowH + 10.0f * k; }
                  button(3301, "Mirar a la c\xC3\xA1mara");
                  button(3302, "Poner a todos en fila");
                  button(3362, PjMultiCount() > 1 ? "Ocultar / mostrar los elegidos (H)" : "Ocultar / mostrar (H)");
                  { const int nm = PjMultiCount(); if (nm > 1) { char tm[64]; snprintf(tm, sizeof tm, "%d jugadores elegidos: se mueven juntos.", nm); text(lx, y + 9.0f * k, ink, tm, fsS); y += 20.0f * k; } }
                  text(lx, y + 9.0f * k, dim, "May\xC3\xBAs + clic: sumar o sacar de los elegidos.", fsS); y += 20.0f * k;
                  text(lx, y + 9.0f * k, dim, "Clic en un jugador de la vista: elegirlo y arrastrarlo.", fsS); y += 20.0f * k;
                  text(lx, y + 9.0f * k, dim, "Flechas: mover.  Q / E: girar.  R / F: altura.", fsS); y += 20.0f * k;
                  text(lx, y + 9.0f * k, dim, "+ / -: tama\xC3\xB1o.  May\xC3\xBAs: m\xC3\xA1s r\xC3\xA1pido.  Supr: sacarlo.", fsS); y += 20.0f * k;
                  text(lx, y + 9.0f * k, dim, "Espacio: reproducir / pausa.  Coma / punto: un cuadro.", fsS); y += 26.0f * k;
              }
              if (label("ANIMACI\xC3\x93N")) {      // (17/11 18:01, Agustin: "un menu mas accesible con mas opciones, mas descripcion" y rueda del mouse en esta seccion)
                  const int nC = MotorClipCount(), cur = PI.clip[qs] >= 0 && PI.clip[qs] < nC ? PI.clip[qs] : MotorClipIdle();
                  static int s_lastSel = -2; if (s_lastSel != qs || s_pjGroup < 0) { s_lastSel = qs; s_pjGroup = MotorClipGroup(cur); s_pjListTop = -1; }
                  static int s_clHov = -1; const int cw = s_clWheel; s_clWheel = 0;
                  char b1[160];
                  if (PI.clip[qs] < 0) snprintf(b1, sizeof b1, "%s", TRS("Parado (el de siempre)")); else ClipDesc(cur, b1, sizeof b1);
                  value("Ahora", b1);
                  { const char* stt = PI.clipBad[qs] ? "El juego no carg\xC3\xB3 esta animaci\xC3\xB3n." : !PI.clipOn[qs] ? "Cargando la animaci\xC3\xB3n..." : nullptr;
                    if (stt) { text(lx, y + 9.0f * k, PI.clipBad[qs] ? IM_COL32(255, 150, 120, 255) : dim, stt, fsS); y += 20.0f * k; } }
                  char sq[48]; { std::lock_guard<std::mutex> lk(s_srMx); memcpy(sq, s_sr, sizeof sq); }
                  auto searchRow = [&](int idF, int idX, int mode) {      // campo del buscador
                      const ImVec2 p0(fx0, y + 3.0f * k), p1(fx1 - 26.0f * k, y + rowH - 3.0f * k), x0b(fx1 - 22.0f * k, y + 3.0f * k), x1b(fx1, y + rowH - 3.0f * k);
                      const bool on = s_srOn.load() == mode, hv = hit(idF, p0, p1), hx = hit(idX, x0b, x1b);
                      dl->AddRectFilled(p0, p1, cField, 3.0f * k); dl->AddRect(p0, p1, on ? acc : hv ? IM_COL32(120, 120, 126, 255) : cLine, 3.0f * k, 0, on ? 1.5f : 1.0f);
                      char t[64]; const bool blink = fmod(ImGui::GetTime(), 1.0) < 0.55;
                      if (sq[0] || on) snprintf(t, sizeof t, "%s%s", sq, on && blink ? "|" : ""); else snprintf(t, sizeof t, "%s", TRS("Buscar por nombre de archivo..."));
                      dl->PushClipRect(p0, p1, true); text(p0.x + 8.0f * k, (p0.y + p1.y) * 0.5f, sq[0] || on ? ink : IM_COL32(120, 120, 126, 255), t, fsS); dl->PopClipRect();
                      dl->AddRectFilled(x0b, x1b, hx ? IM_COL32(86, 86, 92, 255) : IM_COL32(56, 56, 60, 255), 3.0f * k); textC(x0b, x1b, ink, "x", fsS);
                      y += rowH + 2.0f * k; };
                  auto moreRow = [&](int idU, int idD) {      // "Mas arriba / Mas abajo" debajo de una lista
                      const ImVec2 u0(fx0, y + 2.0f * k), u1((fx0 + fx1) * 0.5f - 2.0f * k, y + rowH - 4.0f * k), d0((fx0 + fx1) * 0.5f + 2.0f * k, y + 2.0f * k), d1(fx1, y + rowH - 4.0f * k);
                      const bool hu = hit(idU, u0, u1), hd = hit(idD, d0, d1);
                      dl->AddRectFilled(u0, u1, hu ? IM_COL32(86, 86, 92, 255) : IM_COL32(56, 56, 60, 255), 3.0f * k); dl->AddRectFilled(d0, d1, hd ? IM_COL32(86, 86, 92, 255) : IM_COL32(56, 56, 60, 255), 3.0f * k);
                      textC(u0, u1, ink, "M\xC3\xA1s arriba", fsS); textC(d0, d1, ink, "M\xC3\xA1s abajo", fsS); y += rowH + 4.0f * k; };
                  const int nRowsL = 8; float lt = -1.0f, lb = -1.0f;
                  searchRow(3350, 3351, 1);
                  if (sq[0]) {      // resultados del buscador: todos los grupos
                      static int s_hits[4096]; int nh = 0; for (int i = 0; i < nC && nh < 4096; i++) if (SrMatch(MotorClipFile(i), sq)) s_hits[nh++] = i;
                      snprintf(b1, sizeof b1, "%d %s", nh, TRS(nh == 1 ? "clip encontrado" : "clips encontrados")); text(lx, y + 9.0f * k, dim, b1, fsS); y += 20.0f * k;
                      s_srTop = std::clamp(s_srTop + cw, 0, std::max(0, nh - nRowsL)); lt = y;
                      for (int r = 0; r < nRowsL; r++) { const int h = s_srTop + r; s_pjRowClip[r] = h < nh ? s_hits[h] : -1; if (h >= nh) continue; ClipLabel(s_hits[h], b1, sizeof b1); listRow(3400 + r, b1, s_hits[h] == cur && PI.clip[qs] >= 0); }
                      lb = y; if (nh > nRowsL) moreRow(3352, 3353);
                  } else {
                      { FavLoad(); char t0[40], t1[40]; snprintf(t0, sizeof t0, "%s (%d)", TRS("Favoritos"), (int)s_fav.size()); snprintf(t1, sizeof t1, "%s (%d)", TRS("Recientes"), (int)s_rec.size());
                        const char* const tb[3] = { "Por grupo", t0, t1 }; seg(3380, ImVec2(fx0, y + 2.0f * k), ImVec2(fx1, y + rowH), 3, std::clamp(s_pjTab, 0, 2), tb); y += rowH + 6.0f * k; }
                      if (s_pjTab == 0) {   // lista del grupo a la vista (clic = ponerla); rueda o flechas para recorrer
                          { const int g = std::clamp(s_pjGroup, 0, MotorClipGroupCount() - 1); int n = 0; for (int i = 0; i < nC; i++) if (MotorClipGroup(i) == g) n++;
                            snprintf(b1, sizeof b1, "%s  (%d)", TRS(GroupLabel(g)), n); arrowRow(3310, 3311, "Grupo", b1, true); }
                          int g0 = -1, g1 = -1; for (int i = 0; i < nC; i++) if (MotorClipGroup(i) == s_pjGroup) { if (g0 < 0) g0 = i; g1 = i; }
                          if (g0 >= 0) {
                              if (s_pjListTop < g0 || s_pjListTop > g1) s_pjListTop = (cur >= g0 && cur <= g1) ? std::max(g0, cur - 3) : g0;
                              s_pjListTop = std::clamp(s_pjListTop + cw, g0, std::max(g0, g1 - nRowsL + 1)); lt = y;
                              for (int r = 0; r < nRowsL; r++) { const int c = s_pjListTop + r; s_pjRowClip[r] = c <= g1 ? c : -1; if (c > g1) continue; ClipLabel(c, b1, sizeof b1); listRow(3400 + r, b1, c == cur && PI.clip[qs] >= 0); }
                              lb = y; moreRow(3314, 3315);
                          }
                      } else {      // favoritos (se guardan) o recientes (los ultimos 10 que pusiste)
                          const std::vector<int>& L = s_pjTab == 1 ? s_fav : s_rec; const int nl = (int)L.size();
                          if (nl == 0) { for (int r = 0; r < nRowsL; r++) s_pjRowClip[r] = -1;
                              text(lx, y + 9.0f * k, dim, s_pjTab == 1 ? "Clic derecho en un clip de la lista para sumarlo ac\xC3\xA1." : "Ac\xC3\xA1 aparecen las \xC3\xBAltimas animaciones que pusiste.", fsS); y += 26.0f * k; }
                          else { s_flTop = std::clamp(s_flTop + cw, 0, std::max(0, nl - nRowsL)); lt = y;
                              for (int r = 0; r < nRowsL; r++) { const int h = s_flTop + r; s_pjRowClip[r] = h < nl ? L[h] : -1; if (h >= nl) continue; ClipLabel(L[h], b1, sizeof b1); listRow(3400 + r, b1, L[h] == cur && PI.clip[qs] >= 0); }
                              lb = y; if (nl > nRowsL) moreRow(3387, 3388); }
                      }
                  }
                  if (lt >= 0.0f) { s_clR[0] = fx0; s_clR[1] = lt; s_clR[2] = fx1; s_clR[3] = lb; }
                  // ficha del clip: el que esta bajo el mouse en la lista o, si no, el que tiene puesto
                  s_clHov = hot >= 3400 && hot < 3408 ? s_pjRowClip[hot - 3400] : -1;
                  { const int fc = s_clHov >= 0 ? s_clHov : PI.clip[qs] >= 0 ? cur : -1;
                    if (fc >= 0) { const ImVec2 c0(fx0, y + 2.0f * k), c1(fx1, y + 2.0f * k + 58.0f * k);
                      dl->AddRectFilled(c0, c1, IM_COL32(38, 38, 41, 255), 4.0f * k); dl->AddRect(c0, c1, fc == cur && s_clHov < 0 ? acc : cLine, 4.0f * k);
                      dl->PushClipRect(c0, c1, true);
                      ClipDesc(fc, b1, sizeof b1); text(c0.x + 8.0f * k, c0.y + 11.0f * k, ink, b1, fsS + 0.5f * k);
                      ClipLabel(fc, b1, sizeof b1); text(c0.x + 8.0f * k, c0.y + 28.0f * k, IM_COL32(130, 130, 136, 255), b1, fsS - 1.0f * k);
                      if (fc == cur && PI.lenT[qs] > 0.0f) snprintf(b1, sizeof b1, TRS("Duraci\xC3\xB3n %.2f s  \xC2\xB7  %d cuadros  \xC2\xB7  cuadro %d"), PI.lenT[qs], (int)(PI.lenT[qs] * 30.0f + 0.5f), (int)(PI.curT[qs] * 30.0f) + 1);
                      else snprintf(b1, sizeof b1, "%s%s", TRS("Clic: ponerla.  Clic derecho: "), TRS(FavHas(fc) ? "sacar de Favoritos." : "a Favoritos."));
                      text(c0.x + 8.0f * k, c0.y + 45.0f * k, dim, b1, fsS - 1.0f * k);
                      dl->PopClipRect(); y += 64.0f * k; } }
                  { float sp = PI.speed[qs] * 100.0f; text(lx, y + rowH * 0.5f, dim, "Velocidad", fsS + 1.0f * k);
                    if (numField(2120, ImVec2(x0 + rightW * 0.42f, y + 3.0f * k), ImVec2(fx1, y + rowH - 3.0f * k), sp, 1.0f, 0.0f, 400.0f, "%.0f", " %")) MotorPjAnimSet(qs, PI.clip[qs], sp / 100.0f, PI.paused[qs]);
                    y += rowH + 4.0f * k; }
                  { const char* const nms[2] = { "Reproducir", "Pausa" }; seg(3320, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 2, PI.paused[qs] ? 1 : 0, nms); y += rowH + 6.0f * k; }
                  text(lx, y + 9.0f * k, dim, "Al llegar al final:", fsS); y += 20.0f * k;
                  { const char* const md[3] = { "En bucle", "Una vez", "Ida y vuelta" }; seg(3383, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 3, std::clamp(PI.mode[qs], 0, 2), md); y += rowH + 8.0f * k; }
                  button(3322, "Desde el principio");
                  button(3323, "Parado (el de siempre)");
                  button(3324, "Esta animaci\xC3\xB3n a todos");
                  button(3386, "Desfasar a los que tienen esta animaci\xC3\xB3n");
              }
              if (label("GESTO DE LA CARA")) {
                  const int nF = MotorFaceCount(), fc = PI.face[qs]; char b1[96];
                  if (fc < 0) snprintf(b1, sizeof b1, "%s", TRS("Neutra (la del juego)")); else FaceLabel(fc, b1, sizeof b1);
                  arrowRow(3330, 3331, "Gesto", b1, true);
                  { const int nRowsF = 6; static int s_fTop = 0; if (fc >= 0 && (fc < s_fTop || fc >= s_fTop + nRowsF)) s_fTop = std::max(0, fc - 2); s_fTop = std::clamp(s_fTop, 0, std::max(0, nF - nRowsF));
                    if (s_pjFaceScroll) { s_fTop = std::clamp(s_fTop + s_pjFaceScroll, 0, std::max(0, nF - nRowsF)); s_pjFaceScroll = 0; }
                    { const float ft = y;
                      for (int r = 0; r < nRowsF; r++) { const int f = s_fTop + r; s_pjRowFace[r] = f < nF ? f : -1; if (f >= nF) continue; FaceLabel(f, b1, sizeof b1); listRow(3410 + r, b1, f == fc); }
                      s_fcR[0] = fx0; s_fcR[1] = ft; s_fcR[2] = fx1; s_fcR[3] = y; }
                    { const ImVec2 u0(fx0, y + 2.0f * k), u1((fx0 + fx1) * 0.5f - 2.0f * k, y + rowH - 4.0f * k), d0((fx0 + fx1) * 0.5f + 2.0f * k, y + 2.0f * k), d1(fx1, y + rowH - 4.0f * k);
                      const bool hu = hit(3332, u0, u1), hd = hit(3333, d0, d1);
                      dl->AddRectFilled(u0, u1, hu ? IM_COL32(86, 86, 92, 255) : IM_COL32(56, 56, 60, 255), 3.0f * k); dl->AddRectFilled(d0, d1, hd ? IM_COL32(86, 86, 92, 255) : IM_COL32(56, 56, 60, 255), 3.0f * k);
                      textC(u0, u1, ink, "M\xC3\xA1s arriba", fsS); textC(d0, d1, ink, "M\xC3\xA1s abajo", fsS); y += rowH + 4.0f * k; } }
                  { float w = PI.faceW[qs] * 100.0f; text(lx, y + rowH * 0.5f, dim, "Intensidad", fsS + 1.0f * k);
                    if (numField(2121, ImVec2(x0 + rightW * 0.42f, y + 3.0f * k), ImVec2(fx1, y + rowH - 3.0f * k), w, 1.0f, 0.0f, 100.0f, "%.0f", " %")) MotorPjFaceSet(qs, fc, w / 100.0f, PI.blink[qs]);
                    y += rowH + 4.0f * k; }
                  { const char* const nms[2] = { "Parpadea", "Sin parpadeo" }; seg(3340, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 2, PI.blink[qs] ? 0 : 1, nms); y += rowH + 8.0f * k; }
                  button(3343, "Cara neutra");
                  button(3342, "Este gesto a todos");
                  text(lx, y + 9.0f * k, dim, "El gesto sigue el reloj de la animaci\xC3\xB3n del jugador.", fsS); y += 26.0f * k;
              }
              // (17/11 23:08, Agustin: "enfoquemonos en darme opciones para editar las animaciones gani en tiempo real, mover huesos,
              // expresiones, etc. con ayudas de ia"; 23:10 "la vista de huesos deberia ser mas responsiva para el usuario") EDITAR ANIMACION:
              // los puntos de las articulaciones salen solos sobre el jugador elegido (BoneEditGizmo); clic = elegir, anillos = girar,
              // cuadradito de mano / pie = agarrar (IK). Claves en la linea de tiempo; entre claves la curva elegida.
              if (label("EDITAR ANIMACI\xC3\x93N")) { static MotorEdInfo E; char b1[96]; const bool hasE = MotorEdInfoGet(qs, &E);
                  if (!hasE) { text(lx, y + 9.0f * k, dim, "Esperando los huesos del jugador...", fsS); y += 26.0f * k; }
                  else {
                      if (s_poseBone < 0 || s_poseBone >= E.n || !E.lab[s_poseBone]) { s_poseBone = -1; for (int bb = 0; bb < E.n; bb++) if (E.lab[bb] == 4) s_poseBone = bb; if (s_poseBone < 0) s_poseBone = 0; }
                      snprintf(b1, sizeof b1, "%s", E.lab[s_poseBone] ? TRS(MotorBoneLabel(E.lab[s_poseBone])) : "-"); arrowRow(3700, 3701, "Hueso", b1, true);
                      float e[3]; MotorEdGet(qs, s_poseBone, e); bool ch = false;
                      ch |= fld(2130, "Adelante / atr\xC3\xA1s (X)", e[0], 0.5f, -180.0f, 180.0f, "%.1f", "\xC2\xB0"); ch |= fld(2131, "Girar (Y)", e[1], 0.5f, -180.0f, 180.0f, "%.1f", "\xC2\xB0"); ch |= fld(2132, "Al costado (Z)", e[2], 0.5f, -180.0f, 180.0f, "%.1f", "\xC2\xB0");
                      if (ch) MotorEdSet(qs, s_poseBone, e);
                      y += 6.0f * k;
                      auto two = [&](int ia, const char* ta, int ib, const char* tb) { const float mid = (fx0 + fx1) * 0.5f;
                          const ImVec2 a0(fx0, y + 4.0f * k), a1(mid - 3.0f * k, y + rowH + 6.0f * k), b0(mid + 3.0f * k, y + 4.0f * k), b1_(fx1, y + rowH + 6.0f * k);
                          const bool ha = hit(ia, a0, a1), hb = hit(ib, b0, b1_);
                          dl->AddRectFilled(a0, a1, ha ? IM_COL32(86, 86, 92, 255) : IM_COL32(66, 66, 70, 255), 3.0f * k); dl->AddRect(a0, a1, cLine, 3.0f * k); textC(a0, a1, ink, ta, fsS + 0.5f * k);
                          dl->AddRectFilled(b0, b1_, hb ? IM_COL32(86, 86, 92, 255) : IM_COL32(66, 66, 70, 255), 3.0f * k); dl->AddRect(b0, b1_, cLine, 3.0f * k); textC(b0, b1_, ink, tb, fsS + 0.5f * k);
                          y += rowH + 12.0f * k; };
                      two(3709, "Clave aqu\xC3\xAD (K)", 3710, "Borrar clave");
                      two(3714, "\xE2\x97\x80 Clave anterior", 3715, "Clave siguiente \xE2\x96\xB6");
                      two(3711, "Copiar pose", 3712, "Pegar pose");
                      two(3713, "Espejar (izq. \xE2\x86\x94 der.)", 3702, "Hueso sin giro");
                      { text(lx, y + rowH * 0.5f, dim, "Entre claves", fsS + 1.0f * k); const char* const nms[3] = { "Fluida", "Con peso", "Lineal" };
                        seg(3704, ImVec2(x0 + rightW * 0.42f, y), ImVec2(fx1, y + rowH), 3, E.interp, nms); y += rowH + 8.0f * k; }
                      { text(lx, y + rowH * 0.5f, dim, "Auto clave", fsS + 1.0f * k); const char* const nms[2] = { "S\xC3\xAD", "No" };
                        seg(3707, ImVec2(x0 + rightW * 0.42f, y), ImVec2(fx1, y + rowH), 2, E.autoKey ? 0 : 1, nms); y += rowH + 8.0f * k; }
                      { text(lx, y + rowH * 0.5f, dim, "Puntos", fsS + 1.0f * k); const char* const nms[2] = { "Ver", "Ocultar" };
                        seg(3718, ImVec2(x0 + rightW * 0.42f, y), ImVec2(fx1, y + rowH), 2, s_edPts ? 0 : 1, nms); y += rowH + 8.0f * k; }
                      snprintf(b1, sizeof b1, E.nKeys == 1 ? TRS("1 clave (rombos en la l\xC3\xADnea de tiempo)") : TRS("%d claves (rombos en la l\xC3\xADnea de tiempo)"), E.nKeys); text(lx, y + 9.0f * k, dim, b1, fsS); y += 22.0f * k;
                      two(3716, "Guardar trabajo", 3717, "Abrir trabajo");
                      button(3703, "Sacar todo lo editado");
                      text(lx, y + 9.0f * k, dim, "Clic en un punto del jugador = elegir. Anillos = girar.", fsS); y += 18.0f * k;
                      text(lx, y + 9.0f * k, dim, "Cuadradito de mano o pie = agarrar y llevar.", fsS); y += 26.0f * k;
                  } }
          }
      } else if (s_sel >= 4) {
          if (s_sel - 4 < s_nLights) { MotorLight& L = s_lights[s_sel - 4]; char b1[96];
              float i0 = 1.0f; for (int q = 0; q < s_origN; q++) if (s_orig[q].obj == L.obj) i0 = s_orig[q].i0;
              const float I = std::max(L.color[0], std::max(L.color[1], L.color[2])); float tint[3] = { 1.0f, 1.0f, 1.0f }; if (I > 1.0e-6f) for (int q = 0; q < 3; q++) tint[q] = std::max(0.0f, L.color[q] / I);
              const bool canC = MotorLightCan(L, 0), canR = MotorLightCan(L, 2), canP = MotorLightCan(L, 3);
              auto fail = [&](const char* what) { MotorErr("No se pudo cambiar %s de la luz \"%s\": el juego la sac\xC3\xB3 de la escena o no es una clase que conozco.", TRS(what), L.name); };
              auto setColor = [&](const float* t3, float inten) { float c4[4] = { t3[0] * inten, t3[1] * inten, t3[2] * inten, L.color[3] }; if (MotorLightSetColor(L, c4)) memcpy(L.color, c4, sizeof c4); else fail("el color"); };
              if (label("ESTADO")) { const char* const nms[2] = { "Prendida", "Apagada" }; seg(250, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 2, L.on ? 0 : 1, nms); y += rowH + 12.0f * k; }
              if (label("LUZ")) { value("Tipo", kKind[L.kind]);
              // Intensidad (Agustin 05:16 / 05:17: "un valor entendible por el usuario o un slider", "o ambas un slider con numero", "0 es apagado"):
              // % de la intensidad con la que vino la luz (100 % = como estaba). 0 % la apaga (y el estado pasa a Apagada); al subir se prende.
              { text(lx, y + rowH * 0.5f, dim, "Intensidad", fsS + 1.0f * k); float pct = L.on ? I / i0 * 100.0f : 0.0f; const float sx0 = x0 + rightW * 0.42f - 6.0f * k, nx = fx1 - 74.0f * k;
                bool ch = false; if (canC) { ch |= slider(2500, ImVec2(sx0, y), ImVec2(nx - 6.0f * k, y + rowH), pct, 0.0f, 300.0f, acc); ch |= numField(2000, ImVec2(nx, y + 3.0f * k), ImVec2(fx1, y + rowH - 3.0f * k), pct, 1.0f, 0.0f, 100000.0f, "%.0f", " %"); }
                else { snprintf(b1, sizeof b1, "%.0f %%", pct); text(sx0 + 8.0f * k, y + rowH * 0.5f, ink, b1, fsS + 1.0f * k); }
                if (ch) { if (pct < 0.5f) { if (L.on) { if (MotorLightSetOn(L, false)) L.on = false; else fail("el estado"); } }
                          else { setColor(tint, i0 * pct / 100.0f); if (!L.on) { if (MotorLightSetOn(L, true)) L.on = true; else fail("el estado"); } } }
                y += rowH; snprintf(b1, sizeof b1, TRS("valor del motor: %.4g  (100 %% = %.4g)"), I, i0); text(sx0 + 4.0f * k, y + 6.0f * k, IM_COL32(110, 110, 114, 255), b1, fsS - 0.5f * k); y += 16.0f * k; }
              // Color: muestra (clic = abre el selector: tono, saturacion y temperaturas); el brillo va en Intensidad
              { text(lx, y + rowH * 0.5f, dim, "Color", fsS + 1.0f * k); const ImVec2 p0(x0 + rightW * 0.42f, y + 3.0f * k), p1(fx1, y + rowH - 3.0f * k); const bool hv = canC && hit(260, p0, p1);
                dl->AddRectFilled(p0, p1, IM_COL32((int)(std::min(1.0f, tint[0]) * 255.0f), (int)(std::min(1.0f, tint[1]) * 255.0f), (int)(std::min(1.0f, tint[2]) * 255.0f), 255), 3.0f * k);
                dl->AddRect(p0, p1, hv || s_colorOpen ? acc : cLine, 3.0f * k, 0, hv || s_colorOpen ? 1.5f * k : 1.0f); y += rowH; }
              if (s_colorOpen && canC) {
                  float mxT = std::max(tint[0], std::max(tint[1], tint[2])), mnT = std::min(tint[0], std::min(tint[1], tint[2])); float hue = 0.0f, sat = mxT > 1.0e-6f ? (mxT - mnT) / mxT : 0.0f;
                  if (mxT - mnT > 1.0e-6f) { const float d = mxT - mnT; if (mxT == tint[0]) hue = std::fmod((tint[1] - tint[2]) / d + 6.0f, 6.0f); else if (mxT == tint[1]) hue = (tint[2] - tint[0]) / d + 2.0f; else hue = (tint[0] - tint[1]) / d + 4.0f; hue *= 60.0f; }
                  auto hsv = [](float h, float sa, float* o) { h = std::fmod(h, 360.0f) / 60.0f; const int i = (int)h; const float f = h - (float)i, p = 1.0f - sa, q = 1.0f - sa * f, t = 1.0f - sa * (1.0f - f);
                      const float r[6][3] = { { 1, t, p }, { q, 1, p }, { p, 1, t }, { p, q, 1 }, { t, p, 1 }, { 1, p, q } }; for (int c = 0; c < 3; c++) o[c] = r[std::clamp(i, 0, 5)][c]; };
                  const float bx0 = x0 + rightW * 0.42f, bh = 14.0f * k;
                  { text(lx + 10.0f * k, y + rowH * 0.5f, dim, "Tono", fsS); const ImVec2 h0(bx0, y + (rowH - bh) * 0.5f), h1(fx1, y + (rowH + bh) * 0.5f);      // barra de tonos
                    for (int sgm = 0; sgm < 6; sgm++) { float ca[3], cb[3]; hsv((float)sgm * 60.0f, 1.0f, ca); hsv((float)(sgm + 1) * 60.0f - 0.01f, 1.0f, cb); const float xa = h0.x + (h1.x - h0.x) * (float)sgm / 6.0f, xb = h0.x + (h1.x - h0.x) * (float)(sgm + 1) / 6.0f;
                        const ImU32 ua = IM_COL32((int)(ca[0] * 255), (int)(ca[1] * 255), (int)(ca[2] * 255), 255), ub = IM_COL32((int)(cb[0] * 255), (int)(cb[1] * 255), (int)(cb[2] * 255), 255);
                        dl->AddRectFilledMultiColor(ImVec2(xa, h0.y), ImVec2(xb, h1.y), ua, ub, ub, ua); }
                    float hv2 = hue; if (slider(2501, ImVec2(h0.x - 7.0f * k, y), ImVec2(h1.x + 7.0f * k, y + rowH), hv2, 0.0f, 359.9f, IM_COL32(0, 0, 0, 0))) { float t3[3]; hsv(hv2, std::max(sat, 0.05f), t3); setColor(t3, I > 1.0e-6f ? I : i0); }
                    y += rowH; }
                  { text(lx + 10.0f * k, y + rowH * 0.5f, dim, "Saturaci\xC3\xB3n", fsS); float pure[3]; hsv(hue, 1.0f, pure); const ImVec2 h0(bx0, y + (rowH - bh) * 0.5f), h1(fx1, y + (rowH + bh) * 0.5f);
                    dl->AddRectFilledMultiColor(h0, h1, IM_COL32(255, 255, 255, 255), IM_COL32((int)(pure[0] * 255), (int)(pure[1] * 255), (int)(pure[2] * 255), 255), IM_COL32((int)(pure[0] * 255), (int)(pure[1] * 255), (int)(pure[2] * 255), 255), IM_COL32(255, 255, 255, 255));
                    float sv = sat * 100.0f; if (slider(2502, ImVec2(h0.x - 7.0f * k, y), ImVec2(h1.x + 7.0f * k, y + rowH), sv, 0.0f, 100.0f, IM_COL32(0, 0, 0, 0))) { float t3[3]; hsv(hue, sv / 100.0f, t3); setColor(t3, I > 1.0e-6f ? I : i0); }
                    y += rowH; }
                  {   // temperaturas de luz tipicas (Kelvin -> RGB, aproximacion de Tanner Helland)
                      static const int kv[5] = { 2700, 4000, 5500, 6500, 9000 }; const float w5 = (fx1 - fx0) / 5.0f;
                      for (int q = 0; q < 5; q++) { const float T = (float)kv[q] / 100.0f; float r = T <= 66 ? 255.0f : 329.7f * powf(T - 60.0f, -0.1332f), g = T <= 66 ? 99.47f * logf(T) - 161.1f : 288.1f * powf(T - 60.0f, -0.0755f), bb = T >= 66 ? 255.0f : T <= 19 ? 0.0f : 138.5f * logf(T - 10.0f) - 305.0f;
                          float t3[3] = { std::clamp(r, 0.0f, 255.0f) / 255.0f, std::clamp(g, 0.0f, 255.0f) / 255.0f, std::clamp(bb, 0.0f, 255.0f) / 255.0f }; const float m3 = std::max(t3[0], std::max(t3[1], t3[2])); for (float& c : t3) c /= m3;
                          const ImVec2 c0(fx0 + w5 * (float)q + 2.0f * k, y + 3.0f * k), c1(fx0 + w5 * (float)(q + 1) - 2.0f * k, y + rowH - 3.0f * k); const bool hk = hit(270 + q, c0, c1);
                          dl->AddRectFilled(c0, c1, IM_COL32((int)(t3[0] * 255), (int)(t3[1] * 255), (int)(t3[2] * 255), 255), 3.0f * k); if (hk) dl->AddRect(c0, c1, acc, 3.0f * k, 0, 1.5f * k);
                          char kb[16]; snprintf(kb, sizeof kb, "%d K", kv[q]); textC(c0, c1, IM_COL32(30, 30, 30, 255), kb, fsS - 1.0f * k);
                          if (hk && lEdge0 && !hitOff && s_menuOpen < 0 && s_modal == 0) { s_pressId = -1; UndoBegin(); setColor(t3, I > 1.0e-6f ? I : i0); s_ubEndPending = true; } }
                      y += rowH + 4.0f * k; }
              }
              {   // Rojo Verde Azul del tono (0..255, el mas alto siempre 255: el brillo va en Intensidad)
                  text(lx, y + rowH * 0.5f, dim, "Tono R G B", fsS + 1.0f * k); const float fx = x0 + rightW * 0.42f, w3 = (fx1 - fx) / 3.0f;
                  for (int q = 0; q < 3; q++) { float v = tint[q] * 255.0f; if (canC) { if (numField(2001 + q, ImVec2(fx + w3 * (float)q + (q ? 2.0f * k : 0.0f), y + 3.0f * k), ImVec2(fx + w3 * (float)(q + 1) - (q < 2 ? 2.0f * k : 0.0f), y + rowH - 3.0f * k), v, 1.0f, 0.0f, 255.0f, "%.0f", "")) {
                          float t3[3] = { tint[0], tint[1], tint[2] }; t3[q] = v / 255.0f; const float m3 = std::max(t3[0], std::max(t3[1], t3[2])); if (m3 > 1.0e-4f) { for (float& c : t3) c /= m3; setColor(t3, I > 1.0e-6f ? I : i0); } } }
                      else { snprintf(b1, sizeof b1, "%.0f", v); text(fx + w3 * (float)q + 8.0f * k, y + rowH * 0.5f, ink, b1, fsS + 1.0f * k); } }
                  y += rowH; }
                if (L.kind >= 2) { text(lx, y + rowH * 0.5f, dim, "Alcance", fsS + 1.0f * k); const ImVec2 p0(x0 + rightW * 0.42f, y + 3.0f * k), p1(fx1, y + rowH - 3.0f * k);
                  float r = L.range; if (canR) { if (numField(2004, p0, p1, r, 0.02f, 0.05f, 500.0f, "%.2f", " m")) { if (MotorLightSetRange(L, r)) L.range = r; else fail("el alcance"); } }
                  else { snprintf(b1, sizeof b1, "%.2f m", L.range); text(p0.x + 8.0f * k, y + rowH * 0.5f, ink, b1, fsS + 1.0f * k); }
                  y += rowH; }
              y += 10.0f * k; }      // fin de LUZ
              if (label("SOMBRA")) { const char* const nms[2] = { "Proyecta", "No proyecta" }; seg(252, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 2, L.shadow ? 0 : 1, nms); y += rowH + 12.0f * k; }
              y += 8.0f * k; if (label(L.kind == 1 ? "DIRECCI\xC3\x93N" : "POSICI\xC3\x93N"))
              { static const char* const an[3] = { "X", "Y", "Z" };
                for (int q = 0; q < 3; q++) { text(lx, y + rowH * 0.5f, dim, an[q], fsS + 1.0f * k); const ImVec2 p0(x0 + rightW * 0.42f, y + 3.0f * k), p1(fx1, y + rowH - 3.0f * k); float v = L.pos[q];
                    if (canP) { if (numField(2005 + q, p0, p1, v, L.kind == 1 ? 0.005f : 0.01f, L.kind == 1 ? -1.0f : -2000.0f, L.kind == 1 ? 1.0f : 2000.0f, L.kind == 1 ? "%.3f" : "%.2f", L.kind == 1 ? "" : " m")) {
                            float np[3] = { L.pos[0], L.pos[1], L.pos[2] }; np[q] = v; if (MotorLightSetPos(L, np)) memcpy(L.pos, np, sizeof np); else fail(L.kind == 1 ? "la direcci\xC3\xB3n" : "la posici\xC3\xB3n"); } }
                    else { snprintf(b1, sizeof b1, L.kind == 1 ? "%.3f" : "%.2f m", v); text(p0.x + 8.0f * k, y + rowH * 0.5f, ink, b1, fsS + 1.0f * k); }
                    y += rowH; } }
              y += 6.0f * k;
              if (!L.writable) text(lx, y + 4.0f * k, dim, "Luz de una clase que no conozco: solo lectura.", fsS);
              else { text(lx, y + 4.0f * k, IM_COL32(110, 110, 114, 255), "Arrastr\xC3\xA1 un n\xC3\xBAmero de costado o hac\xC3\xA9 clic para escribirlo.", fsS - 0.5f * k);
                     if (!canP || (L.kind >= 2 && !canR)) text(lx, y + 20.0f * k, IM_COL32(110, 110, 114, 255), "Lo que no tiene recuadro es de solo lectura en esta clase de luz.", fsS - 0.5f * k); }
          }
      } else {
          if (label("ESTADO")) { value("Modelo", player ? "cargado" : "sin cargar"); y += 6.0f * k;
          button(220, player ? "Quitar de la escena" : "Cargar en la escena"); }
          text(lx, y + 8.0f * k, dim, "Es el \xC3\xBAltimo jugador que abriste en Editar.", fsS);
      }
      cardClose();
      dl->PopClipRect(); hitY0 = -1.0e9f; hitY1 = 1.0e9f;
      { const float contH = (y - yTop) + 16.0f * k; s_inMax = std::max(0.0f, contH - viewH); s_inScroll = std::min(s_inScroll, s_inMax);
        if (s_inMax > 0.0f) {      // barra de desplazamiento
          const float tx0 = ds.x - 7.0f * k, tx1 = ds.x - 2.0f * k, th = std::max(24.0f * k, viewH * viewH / (viewH + s_inMax)), ty = yC + (viewH - th) * (s_inScroll / s_inMax);
          const ImVec2 r0(tx0 - 3.0f * k, yC), r1(ds.x, y1); const bool over = !hitOff && inside(r0, r1);
          if (lEdge0 && over) { s_inDrag = true; if (my < ty || my > ty + th) s_inScroll = std::clamp((my - yC - th * 0.5f) / std::max(1.0f, viewH - th) * s_inMax, 0.0f, s_inMax); s_inDragY0 = my; s_inDragS0 = s_inScroll; }
          if (s_inDrag) { if (!lDown) s_inDrag = false; else s_inScroll = std::clamp(s_inDragS0 + (my - s_inDragY0) / std::max(1.0f, viewH - th) * s_inMax, 0.0f, s_inMax); }
          dl->AddRectFilled(ImVec2(tx0, yC + 2.0f * k), ImVec2(tx1, y1 - 2.0f * k), IM_COL32(255, 255, 255, 10), 2.5f * k);
          dl->AddRectFilled(ImVec2(tx0, ty), ImVec2(tx1, ty + th), (over || s_inDrag) ? IM_COL32(200, 200, 205, 200) : IM_COL32(150, 150, 155, 140), 2.5f * k);
          if (s_inScroll > 0.5f) dl->AddRectFilledMultiColor(ImVec2(x0 + 1.0f, yC), ImVec2(ds.x - 8.0f * k, yC + 10.0f * k), IM_COL32(0, 0, 0, 90), IM_COL32(0, 0, 0, 90), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
        } else s_inDrag = false; }
      dl->PopClipRect();
      anim(v1, (1.0f - s_inspA) * 14.0f * k, 0.0f, s_inspA);
      { const float tx = dx + (Q.dock == 0 ? -1.0f : Q.dock == 1 ? 1.0f : 0.0f) * (1.0f - eo) * rightW; anim(v0, tx, 0.0f, eo * std::min(1.0f, aR * 1.6f) * (pMoving ? 0.85f : 1.0f)); clipShift(c0, tx); } hitOff = false;
      } mx += dx; pOffC = 0.0f; pMoving = false; };
    // pegados primero, flotantes encima, el que se mueve arriba de todo (el ultimo en dibujarse gana el clic)
    { int ord[2] = { 0, 1 }; auto rk = [&](int i) { return (s_mvOn && s_mvId == i + 1) ? 2 : P[i].dock == 2 ? 1 : 0; }; if (rk(0) > rk(1)) { ord[0] = 1; ord[1] = 0; }
      for (int q = 0; q < 2; q++) { if (ord[q] == 0) drawTree(); else drawInsp(); } }
    // flotante: esquina de abajo a la derecha para cambiar el tamano (ids 987 / 988)
    for (int i = 0; i < 2; i++) { const PLay& Q = P[i]; if (Q.dock != 2 || Q.V < 0.5f || Q.a < 0.999f || (s_mvOn && s_mvId == i + 1)) continue;
        const float g = 16.0f * k; const ImVec2 c1(Q.x + Q.W, Q.yb), c0(c1.x - g, c1.y - g); zone(c0, c1); const bool hv = hit(987 + i, c0, c1) || s_rsz == i + 1;
        const ImU32 c = hv ? acc : IM_COL32(120, 120, 126, 255); for (int q = 1; q <= 3; q++) { const float o = (float)q * 4.0f * k; dl->AddLine(ImVec2(c1.x - o, c1.y - 2.0f * k), ImVec2(c1.x - 2.0f * k, c1.y - o), c, 1.2f * k); } }
    // dos apilados: la division entre los dos se arrastra (id 989)
    for (int sd = 0; sd < 2; sd++) { if (P[0].dock != sd || P[1].dock != sd || P[0].a < 0.999f || P[1].a < 0.999f || s_openA < 0.999f) continue;
        const int top = std::clamp(g_cfg.motorStackTop, 0, 1); const float ym = P[top].yb, x0 = P[top].x, x1 = P[top].x + P[top].W; const ImVec2 h0(x0, ym - 4.0f * k), h1(x1, ym + 4.0f * k); zone(h0, h1);
        const bool hv = hit(989, h0, h1) || s_spl; dl->AddLine(ImVec2(x0, ym), ImVec2(x1, ym), cLine, 2.0f * k);
        if (hv) { for (int g = 3; g >= 1; g--) { const float e = (1.5f + (float)g * 1.6f) * k; dl->AddRectFilled(ImVec2(x0, ym - e), ImVec2(x1, ym + e), IM_COL32(240, 138, 36, s_spl ? 20 : 12)); } dl->AddRectFilled(ImVec2(x0, ym - 1.5f * k), ImVec2(x1, ym + 1.5f * k), s_spl ? acc : IM_COL32(240, 138, 36, 190)); } }
    // (15/11 q, Agustin 17:27 / 17:39) VISTA EXTRA: ventanita flotante con la imagen de la camara extra (cuadros alternados)
    if (FreeCamExtra() && s_openA > 0.5f) {
        float asp = 1.7778f; void* srv = MotorExtraSRV(&asp); asp = std::clamp(asp, 0.5f, 4.0f);
        const float w = std::clamp(s_xv[2], 220.0f, 1400.0f) * k, th = rowH, h = w / asp;
        if (s_xv[0] < 0.0f) { s_xv[0] = (ds.x - rightV) / k - s_xv[2] - 24.0f; s_xv[1] = (y1 - h - th) / k - 24.0f; }
        const float x = std::clamp(s_xv[0] * k, 0.0f, std::max(0.0f, ds.x - w)), y = std::clamp(s_xv[1] * k, y0, std::max(y0, y1 - h - th)); s_xvPx = x; s_xvPy = y;
        const ImVec2 a(x, y), b(x + w, y + th + h); zone(a, b);
        dl->AddRectFilled(ImVec2(a.x + 4.0f * k, a.y + 6.0f * k), ImVec2(b.x + 6.0f * k, b.y + 8.0f * k), IM_COL32(0, 0, 0, 90), 6.0f * k);      // sombra
        dl->AddRectFilled(a, ImVec2(b.x, a.y + th), cHead, 5.0f * k, ImDrawFlags_RoundCornersTop);
        { const bool hv = hit(960, a, ImVec2(b.x, a.y + th)) || s_xvDrag == 1; if (hv) dl->AddRectFilled(a, ImVec2(b.x, a.y + th), IM_COL32(255, 255, 255, 12), 5.0f * k, ImDrawFlags_RoundCornersTop); }
        text(x + 10.0f * k, y + th * 0.5f, dim, "VISTA EXTRA", fsS);
        { float bx = x + 10.0f * k + font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, TRS("VISTA EXTRA")).x + 14.0f * k;
          const char* const bn[4] = { "Frente", "Costado", "Arriba", "Intercambiar" };
          for (int q = 0; q < 4; q++) { const ImVec2 ts = font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, TRS(bn[q])); const ImVec2 p0(bx, y + 4.0f * k), p1(bx + ts.x + 14.0f * k, y + th - 4.0f * k);
              if (p1.x > b.x - 30.0f * k) break; const bool hv = hit(962 + q, p0, p1);
              dl->AddRectFilled(p0, p1, hv ? IM_COL32(86, 86, 92, 255) : IM_COL32(62, 62, 66, 255), 3.0f * k); textC(p0, p1, hv ? ink : IM_COL32(200, 200, 204, 255), bn[q], fsS); bx = p1.x + 6.0f * k; } }
        xBtn(966, b.x - 14.0f * k, y + th * 0.5f);
        const ImVec2 i0(x, y + th), i1(b.x, b.y);
        if (srv) dl->AddImage((ImTextureID)(uintptr_t)srv, i0, i1); else { dl->AddRectFilled(i0, i1, IM_COL32(20, 20, 21, 255)); textC(i0, i1, dim, "Esperando la primera imagen...", fsS + 1.0f * k); }
        { const bool hv = hit(961, i0, i1) || s_xvDrag == 2; if (hv) text(x + 10.0f * k, b.y - 14.0f * k, IM_COL32(230, 230, 232, 220), "Arrastr\xC3\xA1 para girar  \xC2\xB7  con Shift: acercar o alejar", fsS - 0.5f * k); }
        dl->AddRect(a, b, s_xvDrag ? acc : IM_COL32(80, 80, 86, 255), 5.0f * k, 0, 1.0f);
        { const float g = 16.0f * k; const ImVec2 c0(b.x - g, b.y - g); const bool hv = hit(967, c0, b) || s_xvDrag == 3; const ImU32 c = hv ? acc : IM_COL32(150, 150, 156, 255);
          for (int q = 1; q <= 3; q++) { const float o = (float)q * 4.0f * k; dl->AddLine(ImVec2(b.x - o, b.y - 2.0f * k), ImVec2(b.x - 2.0f * k, b.y - o), c, 1.2f * k); } }
    }
    // panel agarrado: recuadro naranja donde va a caer
    if (s_mvOn && s_dropDock >= 0) { const ImVec2 r0(s_dropR[0] + 3.0f * k, s_dropR[1] + 3.0f * k), r1(s_dropR[2] - 3.0f * k, s_dropR[3] - 3.0f * k);
        dl->AddRectFilled(r0, r1, IM_COL32(240, 138, 36, 34), 4.0f * k); dl->AddRect(r0, r1, IM_COL32(240, 138, 36, 200), 4.0f * k, 0, 2.0f * k);
        const char* const dn[3] = { "Pegar a la izquierda", "Pegar a la derecha", "Flotante" }; textC(r0, ImVec2(r1.x, r0.y + 40.0f * k), IM_COL32(255, 214, 170, 255), dn[s_dropDock], fsS + 1.0f * k); }
    s_rightPx = rightV;      // el gizmo de ejes se corre lo que mide el inspector (unidades del dibujo)
    // ---------------- (15/11 o) bordes de los paneles (arrastrar = ancho) y pestanitas para volver a abrirlos ----------------
    if (s_openA > 0.999f) {
        const float hw = 4.0f * k;
        for (int sd = 0; sd < 2; sd++) { const bool on = (P[0].dock == sd && P[0].a > 0.999f) || (P[1].dock == sd && P[1].a > 0.999f); if (!on) continue;
            const float ex = sd == 0 ? leftV : ds.x - rightV; const ImVec2 h0(ex - hw, y0 + rowH), h1(ex + hw, y1); zone(h0, h1);
            const bool hv = hit(990 + sd, h0, h1) || s_pDrag == sd + 1;
            if (hv) { const bool dr = s_pDrag == sd + 1;      // (15/11 p) linea del color del Motor con brillo (el cursor pasa a la flecha doble)
                for (int g = 4; g >= 1; g--) { const float e = (1.5f + (float)g * 1.6f) * k; dl->AddRectFilled(ImVec2(ex - e, y0), ImVec2(ex + e, y1), IM_COL32(240, 138, 36, dr ? 20 : 12)); }
                dl->AddRectFilled(ImVec2(ex - 1.5f * k, y0), ImVec2(ex + 1.5f * k, y1), dr ? acc : IM_COL32(240, 138, 36, 190)); } }
        for (int pi = 0; pi < 2; pi++) { const float a = P[pi].a; if (a > 0.001f) continue;      // cerrado: pestanita en su costado (flotante: el de siempre)
            const int sd = P[pi].dock == 2 ? pi : P[pi].dock; const bool both = P[1 - pi].a <= 0.001f && (P[1 - pi].dock == 2 ? 1 - pi : P[1 - pi].dock) == sd;
            const float tw = 14.0f * k, th2 = 64.0f * k, yc = (y0 + y1) * 0.5f + (both ? (pi == 0 ? -38.0f : 38.0f) * k : 0.0f); const ImVec2 t0(sd == 0 ? 0.0f : ds.x - tw, yc - th2 * 0.5f), t1(sd == 0 ? tw : ds.x, yc + th2 * 0.5f);
            zone(t0, t1); const bool hv = hit(994 + pi, t0, t1);
            dl->AddRectFilled(t0, t1, hv ? IM_COL32(70, 70, 76, 250) : IM_COL32(45, 45, 48, 235), 4.0f * k, sd == 0 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersLeft);
            const float cx = (t0.x + t1.x) * 0.5f, d = sd == 0 ? 1.0f : -1.0f; const ImU32 c = hv ? acc : dim;
            dl->AddTriangleFilled(ImVec2(cx - 2.5f * k * d, yc - 5.0f * k), ImVec2(cx - 2.5f * k * d, yc + 5.0f * k), ImVec2(cx + 3.0f * k * d, yc), c); }
    }
    // ---------------- barra de estado ----------------
    { const int v0 = dl->VtxBuffer.Size; const ImVec2 a(0, y1), b(ds.x, ds.y); zone(a, b); dl->AddRectFilled(a, b, IM_COL32(30, 30, 31, 250)); dl->AddLine(a, ImVec2(ds.x, y1), cLine, 1.0f);
      char st[200]; snprintf(st, sizeof st, TRS("Vista: %s    Alambre: %s    C\xC3\xA1mara: %s    Objetos: %d    Luces: %d"), ViewTr(0, MotorViewGet(0)), ViewTr(1, MotorViewGet(1)), TRS(camFree ? (FreeCamOrtho() ? "libre, ortogr\xC3\xA1" "fica" : "libre") : "del juego"), s_info.objects, s_info.lights);
      text(12.0f * k, y1 + statH * 0.5f, dim, st, fsS);
      char fp[32]; snprintf(fp, sizeof fp, TRS("%.0f fps"), ImGui::GetIO().Framerate); const ImVec2 ts = font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, fp); text(ds.x - ts.x - 12.0f * k, y1 + statH * 0.5f, dim, fp, fsS);
      anim(v0, 0.0f, (1.0f - eo) * statH, eo); }
    {   // ---------------- errores: recuadros rojos apilados abajo a la izquierda de la vista 3D ----------------
        MErr e[8]; int total = 0; { std::lock_guard<std::mutex> lk(s_errMx); memcpy(e, s_err, sizeof e); total = s_errTotal; }
        const double now = Now(); const float bw = std::min(620.0f * k, ds.x - leftV - rightV - 32.0f * k), pad = 12.0f * k; float yb = y1 - 14.0f * k;
        for (int j = 0; j < 8 && j < total && bw > 200.0f * k; j++) { const MErr& m = e[(total - 1 - j) & 7]; const float age = (float)(now - m.t); if (age > 10.0f) continue;
            const float a = std::min(1.0f, std::min(age / 0.18f, (10.0f - age) / 0.5f)); const float wrap = bw - pad * 2.0f;
            const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, wrap, m.msg); const float bh = ts.y + pad * 2.0f + 20.0f * k;
            const ImVec2 p0(leftV + 16.0f * k + (1.0f - a) * -18.0f * k, yb - bh), p1(p0.x + bw, yb);
            dl->AddRectFilled(p0, p1, IM_COL32(128, 22, 22, (int)(238 * a)), 4.0f * k); dl->AddRect(p0, p1, IM_COL32(255, 96, 96, (int)(255 * a)), 4.0f * k, 0, 1.5f * k);
            dl->AddRectFilled(p0, ImVec2(p0.x + 5.0f * k, p1.y), IM_COL32(255, 96, 96, (int)(255 * a)), 4.0f * k);
            dl->AddText(font, fsS + 1.0f * k, ImVec2(p0.x + pad + 4.0f * k, p0.y + pad - 2.0f * k), IM_COL32(255, 170, 170, (int)(255 * a)), TRS("ERROR DEL MOTOR"));
            dl->AddText(font, fs, ImVec2(p0.x + pad + 4.0f * k, p0.y + pad + 18.0f * k), IM_COL32(255, 240, 240, (int)(255 * a)), m.msg, nullptr, wrap);
            yb -= bh + 8.0f * k; }
        if (total > 0) {      // cuenta en la barra de estado: clic = volver a mostrarlos
            char eb[40]; snprintf(eb, sizeof eb, TRS(total == 1 ? "1 error" : "%d errores"), total); const ImVec2 ts = font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, eb);
            const ImVec2 p0(ds.x - ts.x - 110.0f * k, y1 + 3.0f * k), p1(p0.x + ts.x + 16.0f * k, ds.y - 3.0f * k); const bool hv = hit(500, p0, p1);
            dl->AddRectFilled(p0, p1, IM_COL32(150, 28, 28, hv ? 255 : 220), 3.0f * k); textC(p0, p1, IM_COL32(255, 230, 230, 255), eb, fsS); }
    }
    // ---------------- aviso corto (no es un error): pastilla arriba al centro de la vista 3D ----------------
    { const float age = (float)(Now() - s_noteT);
      if (s_note[0] && age < 3.5f) { const float a = std::max(0.0f, std::min(1.0f, std::min(age / 0.15f, (3.5f - age) / 0.4f))); const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, s_note);
          const float cx = (leftV + ds.x - rightV) * 0.5f, yy = barH + 14.0f * k - (1.0f - a) * 8.0f * k; const ImVec2 p0(cx - ts.x * 0.5f - 16.0f * k, yy), p1(cx + ts.x * 0.5f + 16.0f * k, yy + 32.0f * k);
          dl->AddRectFilled(p0, p1, IM_COL32(30, 30, 31, (int)(240 * a)), 16.0f * k); dl->AddRect(p0, p1, IM_COL32(240, 138, 36, (int)(220 * a)), 16.0f * k, 0, 1.2f * k);
          dl->AddText(font, fs, ImVec2(cx - ts.x * 0.5f, yy + (32.0f * k - ts.y) * 0.5f), IM_COL32(236, 236, 236, (int)(255 * a)), s_note); } }
    // ---------------- ventana al centro de la vista 3D: abrir escena / controles y atajos ----------------
    // ---------------- (15/11 p) Editar > Preferencias (Ctrl + ,): categorias a la izquierda, secciones plegables a la derecha ----------------
    // ---------------- (17/11 06:52 / 06:53, Agustin: "una linea de tiempo", "volver atras o adelante, pausa, reproducir", "si se cargan muchas
    // animaciones en distintos jugadores una barra que cambie la linea de cada animacion", "un visualizador real de animaciones") LINEA DE
    // TIEMPO abajo de la vista: controles (principio, cuadro atras, reproducir / pausa, cuadro adelante, final) para el elegido o para todos,
    // y una pista por jugador con su animacion: el cabezal muestra el segundo del clip y se arrastra para ir a cualquier cuadro (lo pausa).
    s_tlZone.store(-1);
    {   static MotorPjInfo TI; MotorPjInfoGet(&TI); int tr[kMotorPjMax], nT = 0; for (int i = 0; i < kMotorPjMax; i++) if (TI.st[i] == 3) tr[nT++] = i;
        const bool show = s_tlMode == 1 || (s_tlMode == 0 && nT > 0);
        if (show && s_modal == 0) {
            const int maxRows = 6; s_tlScroll = std::clamp(s_tlScroll, 0, std::max(0, nT - maxRows)); const int nVis = std::min(nT - s_tlScroll, maxRows);
            const float tw = 23.0f * k * rf, th = rowH + 6.0f * k + (float)std::max(1, nVis) * tw + 8.0f * k;
            const ImVec2 a(leftV + 1.0f, y1 - th), b(ds.x - rightV - 1.0f, y1);
            if (b.x - a.x > 300.0f * k) {
                s_tlZone.store(nz); zone(a, b); hit(3599, a, b);
                dl->AddRectFilled(a, b, IM_COL32(30, 30, 31, 238)); dl->AddLine(a, ImVec2(b.x, a.y), cLine, 1.0f); dl->AddRectFilled(a, ImVec2(b.x, a.y + rowH), cHead);
                text(a.x + 12.0f * k, a.y + rowH * 0.5f, dim, "L\xC3\x8DNEA DE TIEMPO", fsS);
                const int qs = MotorPjSel(); const int ref = qs >= 0 && TI.st[qs] == 3 ? qs : (nT ? tr[0] : -1);
                const bool paused = ref >= 0 ? TI.paused[ref] : false;
                {   // controles
                    const float bw = 34.0f * k, bh = rowH - 6.0f * k, cx = (a.x + b.x) * 0.5f - bw * 2.5f, cy = a.y + 3.0f * k;
                    for (int i = 0; i < 5; i++) { const ImVec2 p0(cx + (float)i * (bw + 4.0f * k), cy), p1(p0.x + bw, cy + bh); const bool hv = hit(3500 + i, p0, p1);
                        dl->AddRectFilled(p0, p1, hv ? IM_COL32(86, 86, 92, 255) : (i == 2 && !paused && ref >= 0) ? IM_COL32(74, 60, 44, 255) : IM_COL32(56, 56, 60, 255), 3.0f * k);
                        const ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f); const float r = 5.5f * k; const ImU32 ic = ink;
                        if (i == 0 || i == 4) { const float sgn = i == 0 ? -1.0f : 1.0f; dl->AddTriangleFilled(ImVec2(c.x - sgn * r * 0.6f, c.y - r), ImVec2(c.x - sgn * r * 0.6f, c.y + r), ImVec2(c.x + sgn * r * 0.9f, c.y), ic);
                            dl->AddRectFilled(ImVec2(c.x + sgn * r * 0.9f - 1.2f * k, c.y - r), ImVec2(c.x + sgn * r * 0.9f + 1.2f * k, c.y + r), ic); }
                        else if (i == 1 || i == 3) { const float sgn = i == 1 ? -1.0f : 1.0f; dl->AddTriangleFilled(ImVec2(c.x - sgn * r * 0.2f, c.y - r * 0.8f), ImVec2(c.x - sgn * r * 0.2f, c.y + r * 0.8f), ImVec2(c.x + sgn * r * 0.9f, c.y), ic);
                            dl->AddRectFilled(ImVec2(c.x - sgn * r * 0.9f - 1.0f * k, c.y - r * 0.8f), ImVec2(c.x - sgn * r * 0.9f + 1.0f * k, c.y + r * 0.8f), ic); }
                        else if (paused || ref < 0) dl->AddTriangleFilled(ImVec2(c.x - r * 0.7f, c.y - r), ImVec2(c.x - r * 0.7f, c.y + r), ImVec2(c.x + r, c.y), ic);
                        else { dl->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r), ImVec2(c.x - r * 0.25f, c.y + r), ic); dl->AddRectFilled(ImVec2(c.x + r * 0.25f, c.y - r), ImVec2(c.x + r * 0.8f, c.y + r), ic); } }
                    const char* const nms[2] = { "El elegido", "Todos" }; const float sx0 = b.x - 230.0f * k;
                    {   // (17/11 17:52, Agustin: "una opcion para reproducir en camara lenta o frame a frame en la linea de tiempo") velocidad
                        static const float kSp[4] = { 0.1f, 0.25f, 0.5f, 1.0f }; static const char* const kSpN[4] = { "0,1\xC3\x97", "0,25\xC3\x97", "0,5\xC3\x97", "1\xC3\x97" };
                        const float sw = 46.0f * k, s0 = cx + 5.0f * (bw + 4.0f * k) + 14.0f * k; const float cur = ref >= 0 ? TI.speed[ref] : 1.0f;
                        if (s0 + 4.0f * (sw + 3.0f * k) < sx0 - 8.0f * k)
                            for (int i = 0; i < 4; i++) { const ImVec2 p0(s0 + (float)i * (sw + 3.0f * k), cy), p1(p0.x + sw, cy + bh); const bool hv = hit(3510 + i, p0, p1);
                                const bool on = ref >= 0 && fabsf(cur - kSp[i]) < 0.01f;
                                dl->AddRectFilled(p0, p1, on ? IM_COL32(240, 138, 36, 255) : hv ? IM_COL32(86, 86, 92, 255) : IM_COL32(56, 56, 60, 255), 3.0f * k);
                                textC(p0, p1, on ? IM_COL32(20, 20, 20, 255) : ink, kSpN[i], fsS - 0.5f * k); } }
                    if (sx0 > cx + 5.0f * (bw + 4.0f * k) + 10.0f * k) seg(3505, ImVec2(sx0, cy), ImVec2(b.x - 40.0f * k, cy + bh), 2, s_tlAll ? 1 : 0, nms);
                    xBtn(3507, b.x - 18.0f * k, a.y + rowH * 0.5f);      // ocultar la linea de tiempo (Ver > Linea de tiempo la vuelve a mostrar)
                    if (ref >= 0) { char t[64]; snprintf(t, sizeof t, "%.2f / %.2f s   cuadro %d", TI.curT[ref], TI.lenT[ref], (int)(TI.curT[ref] * 30.0f + 0.5f)); text(a.x + 150.0f * k, a.y + rowH * 0.5f, ink, t, fsS); }
                }
                if (nT == 0) text(a.x + 16.0f * k, a.y + rowH + 6.0f * k + tw * 0.5f, dim, "Sin jugadores listos: agreg\xC3\xA1 uno en el Inspector (Jugadores).", fsS);
                const float nameW = std::min(220.0f * k, (b.x - a.x) * 0.3f);
                for (int r = 0; r < nVis; r++) {
                    const int i = tr[s_tlScroll + r]; const float ty = a.y + rowH + 6.0f * k + (float)r * tw, tyc = ty + tw * 0.5f;
                    const bool sel = i == qs;
                    { const ImVec2 n0(a.x + 4.0f * k, ty + 1.0f * k), n1(a.x + nameW, ty + tw - 1.0f * k); const bool hv = hit(3000 + i, n0, n1);
                      if (sel) dl->AddRectFilled(n0, n1, IM_COL32(64, 52, 40, 255), 3.0f * k); else if (hv) dl->AddRectFilled(n0, n1, IM_COL32(255, 255, 255, 18), 3.0f * k);
                      char nm[64]; snprintf(nm, sizeof nm, "%d. %.22s", i + 1, TI.name[i]); dl->PushClipRect(n0, n1, true); text(n0.x + 8.0f * k, tyc, sel ? ink : dim, nm, fsS); dl->PopClipRect(); }
                    const ImVec2 t0(a.x + nameW + 8.0f * k, ty + 2.0f * k), t1(b.x - 10.0f * k, ty + tw - 2.0f * k);
                    dl->AddRectFilled(t0, t1, cField, 3.0f * k);
                    const float len = std::max(0.001f, TI.lenT[i]); float v = std::clamp(TI.curT[i], 0.0f, len);
                    // marcas de segundo
                    for (float s2 = 1.0f; s2 < len; s2 += 1.0f) { const float x = t0.x + (t1.x - t0.x) * s2 / len; dl->AddLine(ImVec2(x, t1.y - 5.0f * k), ImVec2(x, t1.y), IM_COL32(90, 90, 96, 255), 1.0f); }
                    const ImU32 fill = TI.paused[i] ? IM_COL32(120, 120, 128, 140) : (sel ? IM_COL32(240, 138, 36, 150) : IM_COL32(110, 150, 200, 120));
                    dl->AddRectFilled(t0, ImVec2(t0.x + (t1.x - t0.x) * v / len, t1.y), fill, 3.0f * k);
                    { char cn[96]; if (TI.clip[i] < 0) snprintf(cn, sizeof cn, "%s", TRS("Parado (el de siempre)")); else ClipLabel(TI.clip[i], cn, sizeof cn);
                      dl->PushClipRect(t0, t1, true); text(t0.x + 8.0f * k, tyc, IM_COL32(230, 230, 232, 220), cn, fsS - 1.0f * k); dl->PopClipRect(); }
                    if (slider(2600 + i, t0, t1, v, 0.0f, len, IM_COL32(0, 0, 0, 0))) { MotorPjSeek(i, v); if (!TI.paused[i]) MotorPjAnimSet(i, TI.clip[i], TI.speed[i], true); MotorPjSelect(i); s_sel = 3; }
                    dl->AddRect(t0, t1, sel ? acc : cLine, 3.0f * k, 0, sel ? 1.5f * k : 1.0f);
                }
                if (nT > maxRows) { char t[48]; snprintf(t, sizeof t, TRS("%d-%d de %d  (rueda)"), s_tlScroll + 1, s_tlScroll + nVis, nT); text(a.x + 16.0f * k, b.y - 9.0f * k, dim, t, fsS - 1.0f * k);
                    if (my >= a.y && my <= b.y && mx >= a.x && mx <= b.x) { const int wd = s_tlWheel.exchange(0); if (wd) s_tlScroll = std::clamp(s_tlScroll - (wd > 0 ? 1 : -1), 0, nT - maxRows); } }
            }
        }
    }
    // ---------------- (17/11 06:48, Agustin: "ahora que tenemos el menu puedes agregar una vista para probar la prueba automatica y ver que todo
    // funcione bien con los shaders?") Render > Prueba automatica de shaders: la misma prueba del menu del mod (menutest.cpp), con sus resultados.
    // Mientras corre solo queda una franja arriba (las fotos de la prueba no salen tapadas por la ventana).
    const bool mtOn = MenuTestActive();
    if (mtOn && (s_modal == 0 || s_modal == 5)) {
        const ImVec2 a(leftV + 1.0f, y0), b(ds.x - rightV - 1.0f, y0 + rowH + 8.0f * k); zone(a, b); hit(3598, a, b);
        dl->AddRectFilled(a, b, IM_COL32(30, 30, 31, 230)); dl->AddRectFilled(a, ImVec2(a.x + 3.0f * k, b.y), acc);
        const std::string st = AtUtf8(MenuTestStatus()); const float cy = (a.y + b.y) * 0.5f;
        const ImVec2 c0(b.x - 110.0f * k, a.y + 4.0f * k), c1(b.x - 8.0f * k, b.y - 4.0f * k); const bool hv = hit(3524, c0, c1);
        dl->AddRectFilled(c0, c1, hv ? IM_COL32(150, 60, 50, 255) : IM_COL32(96, 50, 46, 255), 3.0f * k); textC(c0, c1, ink, "Cortar (Esc)", fsS);
        const ImVec2 g0(c0.x - 170.0f * k, cy - 4.0f * k), g1(c0.x - 12.0f * k, cy + 4.0f * k); dl->AddRectFilled(g0, g1, cField, 3.0f * k);
        dl->AddRectFilled(g0, ImVec2(g0.x + (g1.x - g0.x) * std::clamp(MenuTestProgress(), 0.0f, 1.0f), g1.y), acc, 3.0f * k);
        dl->PushClipRect(ImVec2(a.x + 10.0f * k, a.y), ImVec2(g0.x - 10.0f * k, b.y), true); text(a.x + 14.0f * k, cy, ink, st.c_str(), fsS); dl->PopClipRect();
    }
    if (s_modal == 3) {
        const ImVec2 va(leftV + 1.0f, y0), vb(ds.x - rightV - 1.0f, y1); zone(va, vb); hit(898, va, vb);
        dl->AddRectFilled(va, vb, IM_COL32(0, 0, 0, 120));
        const float pw = std::min(780.0f * k, vb.x - va.x - 24.0f * k), ph = std::min(520.0f * k * rf, y1 - y0 - 16.0f * k);
        const ImVec2 p0((va.x + vb.x - pw) * 0.5f, std::max(y0 + 8.0f * k, (y0 + y1 - ph) * 0.5f)), p1(p0.x + pw, p0.y + ph); hit(897, p0, p1);
        dl->AddRectFilled(ImVec2(p0.x + 5.0f * k, p0.y + 6.0f * k), ImVec2(p1.x + 5.0f * k, p1.y + 6.0f * k), IM_COL32(0, 0, 0, 100), 5.0f * k);
        dl->AddRectFilled(p0, p1, IM_COL32(37, 37, 38, 255), 4.0f * k); dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + rowH), cHead, 4.0f * k); dl->AddRect(p0, p1, cLine, 4.0f * k);
        dl->AddRectFilled(p0, ImVec2(p0.x + 3.0f * k, p0.y + rowH), acc);
        text(p0.x + 14.0f * k, p0.y + rowH * 0.5f, ink, "PREFERENCIAS", fsS + 1.0f * k);
        { const char* hk = TRS("Ctrl + ,"); const ImVec2 ts = font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, hk); text(p1.x - ts.x - 14.0f * k, p0.y + rowH * 0.5f, dim, hk, fsS); }
        static const char* const kCat[5] = { "Interfaz", "Edici\xC3\xB3n", "C\xC3\xA1mara", "Sonido", "Idioma" };
        static const int kCatIco[5] = { 33, 30, 1, 31, 32 };
        const float cw = std::min(180.0f * k * tf, pw * 0.32f), ch = 30.0f * k * rf, cy0 = p0.y + rowH + 6.0f * k;
        dl->AddRectFilled(ImVec2(p0.x + 1.0f, p0.y + rowH), ImVec2(p0.x + cw, p1.y - 1.0f), IM_COL32(31, 31, 32, 255), 4.0f * k, ImDrawFlags_RoundCornersBottomLeft);
        dl->AddLine(ImVec2(p0.x + cw, p0.y + rowH), ImVec2(p0.x + cw, p1.y), cLine, 1.0f);
        for (int i = 0; i < 5; i++) { const ImVec2 r0(p0.x + 6.0f * k, cy0 + (float)i * ch), r1(p0.x + cw - 6.0f * k, cy0 + (float)(i + 1) * ch - 2.0f * k); const bool hv = hit(900 + i, r0, r1), cur = s_prefCat == i;
            if (cur) { dl->AddRectFilled(r0, r1, cRow, 3.0f * k); dl->AddRectFilled(r0, ImVec2(r0.x + 3.0f * k, r1.y), acc, 1.5f * k); } else if (hv) dl->AddRectFilled(r0, r1, IM_COL32(255, 255, 255, 16), 3.0f * k);
            const float yc = (r0.y + r1.y) * 0.5f; glyph(kCatIco[i], ImVec2(r0.x + 17.0f * k, yc), 6.0f * k, cur ? IM_COL32(120, 200, 255, 255) : IM_COL32(170, 170, 176, 255));
            text(r0.x + 32.0f * k, yc, cur ? ink : IM_COL32(196, 196, 200, 255), kCat[i], fs); }
        const float cx0 = p0.x + cw + 14.0f * k, cx1 = p1.x - 16.0f * k, lxc = cx0 + 8.0f * k, fxa = cx0 + (cx1 - cx0) * 0.42f, nxw = 84.0f * k;
        float y = p0.y + rowH + 10.0f * k; const float yEnd = p1.y - 52.0f * k;
        dl->PushClipRect(ImVec2(cx0 - 4.0f * k, p0.y + rowH), ImVec2(p1.x - 2.0f * k, yEnd), true);
        const uint32_t salt = 100u + (uint32_t)s_prefCat;
        auto sec = [&](const char* t) { return secHead(cx0, cx1, y, t, salt); };
        auto rowLbl = [&](const char* t) { text(lxc, y + rowH * 0.5f, dim, t, fsS + 1.0f * k); };
        auto note = [&](const char* t) { const char* tt = TRS(t); const float wrap = cx1 - lxc, sz = fsS - 0.5f * k; const ImVec2 ts = font->CalcTextSizeA(sz, FLT_MAX, wrap, tt);
            dl->AddText(font, sz, ImVec2(lxc, y + 4.0f * k), IM_COL32(118, 118, 122, 255), tt, nullptr, wrap); y += ts.y + 14.0f * k; };
        auto sliNum = [&](int sl, int nf, float& v, float lo, float hi, float step, const char* fmt, const char* unit) {      // slider + numero; devuelve si cambio
            bool chg = slider(sl, ImVec2(fxa - 6.0f * k, y), ImVec2(cx1 - nxw - 6.0f * k, y + rowH), v, lo, hi, acc);
            chg |= numField(nf, ImVec2(cx1 - nxw, y + 3.0f * k), ImVec2(cx1, y + rowH - 3.0f * k), v, step, lo, hi, fmt, unit); return chg; };
        auto btn = [&](int id, const char* t) { const ImVec2 b0(lxc, y + 4.0f * k), b1(cx1, y + rowH + 4.0f * k); const bool hv = hit(id, b0, b1);
            dl->AddRectFilled(b0, b1, hv ? IM_COL32(86, 86, 92, 255) : IM_COL32(66, 66, 70, 255), 3.0f * k); dl->AddRect(b0, b1, cLine, 3.0f * k); textC(b0, b1, ink, t, fs); y += rowH + 14.0f * k; };
        if (s_prefCat == 0) {
            if (sec("TAMA\xC3\x91O")) {
                rowLbl("Interfaz"); { float v = (s_pendScale > 0.0f ? s_pendScale : g_cfg.motorScale) * 100.0f; if (sliNum(2504, 2009, v, 80.0f, 200.0f, 0.5f, "%.0f", " %")) s_pendScale = std::clamp(v, 80.0f, 200.0f) / 100.0f; y += rowH + 4.0f * k; }
                rowLbl("Texto"); { float v = tf * 100.0f; if (sliNum(2505, 2010, v, 80.0f, 150.0f, 0.5f, "%.0f", " %")) s_pendText = std::clamp(v, 80.0f, 150.0f) / 100.0f; y += rowH + 4.0f * k; }
                note("Interfaz agranda todo el editor (tambi\xC3\xA9n con Ctrl + y Ctrl -). Texto agranda solo las letras y sus filas. Se aplica al soltar."); }
            if (sec("PANELES")) {
                rowLbl("Panel Escena (T)"); { const char* const nms[2] = { "Abierto", "Cerrado" }; seg(910, ImVec2(fxa, y), ImVec2(cx1, y + rowH), 2, g_cfg.motorPanelLOn ? 0 : 1, nms); y += rowH + 6.0f * k; }
                rowLbl("Panel Inspector (N)"); { const char* const nms[2] = { "Abierto", "Cerrado" }; seg(912, ImVec2(fxa, y), ImVec2(cx1, y + rowH), 2, g_cfg.motorPanelROn ? 0 : 1, nms); y += rowH + 6.0f * k; }
                btn(796, "Restablecer paneles"); }
            if (sec("IMAGEN")) {      // (15/11 q, Agustin 17:40: "le falta resolucion al motor")
                rowLbl("Resoluci\xC3\xB3n del visor"); { const char* const nms[5] = { "Editar", "50 %", "75 %", "100 %", "150 %" }; static const int rv[5] = { 0, 50, 75, 100, 150 }; int c = -1; for (int q = 0; q < 5; q++) if (g_cfg.motorRes == rv[q]) c = q;
                    seg(940, ImVec2(fxa, y), ImVec2(cx1, y + rowH), 5, c, nms); y += rowH + 6.0f * k; }
                note("El Motor dibuja la escena a este tanto de la resoluci\xC3\xB3n de tu pantalla (100 % = n\xC3\xADtida). Editar = el tama\xC3\xB1o chico del recuadro del Editar. M\xC3\xA1s de 100 % suaviza los bordes y pide m\xC3\xA1s a la placa."); }
        } else if (s_prefCat == 1) {
            if (sec("DESHACER (CTRL + Z)")) {
                rowLbl("Pasos guardados"); { float v = (float)UndoLim(); if (sliNum(2506, 2011, v, 10.0f, (float)kUndoMax, 0.5f, "%.0f", "")) { g_cfg.motorUndoSteps = (int)(v + 0.5f); s_prefSave |= 4; } y += rowH + 4.0f * k; }
                { char b1[200]; snprintf(b1, sizeof b1, TRS("Ahora hay %d pasos para deshacer y %d para rehacer. Un arrastre entero cuenta como un paso."), s_undoN, s_redoN); note(b1); } }
            if (sec("AUTOGUARDADO")) {
                rowLbl("Autoguardado"); { const char* const nms[2] = { "Prendido", "Apagado" }; seg(920, ImVec2(fxa, y), ImVec2(cx1, y + rowH), 2, g_cfg.motorAutoSave ? 0 : 1, nms); y += rowH + 6.0f * k; }
                rowLbl("Cada"); { static const char* const mn[4] = { "1 min", "2 min", "5 min", "10 min" }; static const int mv[4] = { 1, 2, 5, 10 }; int c = -1; for (int q = 0; q < 4; q++) if (g_cfg.motorAutoSaveMin == mv[q]) c = q;
                    seg(922, ImVec2(fxa, y), ImVec2(cx1, y + rowH), 4, c, mn); y += rowH + 6.0f * k; }
                note("Guarda la escena en \"Autoguardado\" (Archivo > Abrir escena) solo si cambi\xC3\xB3 algo. No pisa la escena que abriste o guardaste."); }
        } else if (s_prefCat == 2) {
            if (sec("C\xC3\x81MARA LIBRE")) {
                rowLbl("Velocidad"); { float v = std::clamp(g_cfg.live[344][1], 0.05f, 50.0f); if (sliNum(2507, 2012, v, 0.1f, 20.0f, 0.02f, "%.2f", " m/s")) { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.live[344][1] = v; g_cfg.motorCamSpeed = v; s_prefSave |= 8; } y += rowH + 4.0f * k; }
                rowLbl("Sensibilidad del mouse"); { float v = std::clamp(g_cfg.live[344][3], 0.05f, 10.0f); if (sliNum(2508, 2013, v, 0.1f, 5.0f, 0.01f, "%.2f", "")) { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.live[344][3] = v; g_cfg.motorCamSens = v; s_prefSave |= 16; } y += rowH + 4.0f * k; }
                note("Mientras volas: Shift va 4 veces m\xC3\xA1s r\xC3\xA1pido y Ctrl 4 veces m\xC3\xA1s lento. Se pone cada vez que entr\xC3\xA1s al Motor."); }
            if (sec("RECORTE DE LA VISTA")) {
                rowLbl("Cerca"); { float v = g_cfg.motorNear; if (numField(2014, ImVec2(fxa, y + 3.0f * k), ImVec2(cx1, y + rowH - 3.0f * k), v, 0.005f, 0.0f, 10.0f, "%.3f", " m")) { g_cfg.motorNear = v; s_prefSave |= 32; } y += rowH + 4.0f * k; }
                rowLbl("Lejos"); { float v = g_cfg.motorFar; if (numField(2015, ImVec2(fxa, y + 3.0f * k), ImVec2(cx1, y + rowH - 3.0f * k), v, 5.0f, 0.0f, 100000.0f, "%.0f", " m")) { g_cfg.motorFar = v; s_prefSave |= 64; } y += rowH + 4.0f * k; }
                note("Lo que est\xC3\xA1 m\xC3\xA1s cerca o m\xC3\xA1s lejos que esto no se dibuja. 0 = como el modo Editar. Al salir del Motor vuelve lo del juego."); }
        } else if (s_prefCat == 3) {
            if (sec("TRANSICI\xC3\x93N")) {
                rowLbl("Sonido al entrar y salir"); { const char* const nms[2] = { "S\xC3\xAD", "No" }; seg(930, ImVec2(fxa, y), ImVec2(cx1, y + rowH), 2, g_cfg.motorSound ? 0 : 1, nms); y += rowH + 6.0f * k; }
                rowLbl("Volumen"); { float v = (float)g_cfg.motorSoundVol; if (sliNum(2509, 2016, v, 0.0f, 100.0f, 0.5f, "%.0f", " %")) { g_cfg.motorSoundVol = (int)(v + 0.5f); s_prefSave |= 128; } y += rowH + 4.0f * k; } }
        } else {
            if (sec("IDIOMA DEL EDITOR")) {
                static const char* const ln[4] = { "Espa\xC3\xB1ol", "English", "Portugu\xC3\xAAs", "Como el juego (autom\xC3\xA1tico)" }; const int cur = g_cfg.langAuto ? 3 : std::clamp(g_cfg.lang, 0, 2); const float rh = 27.0f * k * rf;
                dl->AddRectFilled(ImVec2(lxc, y), ImVec2(cx1, y + 4.0f * rh), cField, 3.0f * k);
                for (int i = 0; i < 4; i++) { const ImVec2 r0(lxc, y + (float)i * rh), r1(cx1, y + (float)(i + 1) * rh); const bool hv = hit(770 + i, r0, r1);
                    if (i == cur) dl->AddRectFilled(r0, r1, IM_COL32(240, 138, 36, 60)); else if (hv) dl->AddRectFilled(r0, r1, IM_COL32(255, 255, 255, 18));
                    const ImVec2 o(r0.x + 14.0f * k, (r0.y + r1.y) * 0.5f); dl->AddCircle(o, 5.0f * k, i == cur ? acc : IM_COL32(110, 110, 114, 255), 14, 1.4f * k); if (i == cur) dl->AddCircleFilled(o, 2.6f * k, acc, 10);
                    text(r0.x + 28.0f * k, o.y, i == cur ? IM_COL32(255, 214, 170, 255) : ink, ln[i], fsS + 1.5f * k); }
                dl->AddRect(ImVec2(lxc, y), ImVec2(cx1, y + 4.0f * rh), cLine, 3.0f * k); y += 4.0f * rh + 10.0f * k;
                note("Es el mismo idioma del men\xC3\xBA del mod (Insert)."); }
        }
        dl->PopClipRect();
        { const char* ct = TRS("Cerrar  (Esc)"); const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, ct); const ImVec2 c0(p1.x - ts.x - 44.0f * k, p1.y - 42.0f * k), c1(p1.x - 12.0f * k, p1.y - 12.0f * k); const bool hv = hit(899, c0, c1);
          dl->AddRectFilled(c0, c1, hv ? IM_COL32(86, 86, 92, 255) : IM_COL32(66, 66, 70, 255), 3.0f * k); dl->AddRect(c0, c1, cLine, 3.0f * k); textC(c0, c1, ink, ct, fs); }
        text(p0.x + cw + 16.0f * k, p1.y - 27.0f * k, IM_COL32(110, 110, 114, 255), "Se guarda solo en el ini del mod.", fsS - 0.5f * k);
    } else if (s_modal == 5) { if (!mtOn) {
        const ImVec2 va(leftV + 1.0f, y0), vb(ds.x - rightV - 1.0f, y1); zone(va, vb); hit(898, va, vb);
        dl->AddRectFilled(va, vb, IM_COL32(0, 0, 0, 120));
        const float lh = 24.0f * k * rf, pw = std::min(820.0f * k, vb.x - va.x - 24.0f * k);
        static std::string sec[400], grp[400]; static float dfv[400], cpv[400], gpv[400]; static int stv[400]; int nr = 0;
        { const size_t n = MenuTestResults(); for (size_t i = 0; i < n && nr < 400; i++) { std::string g, sc; float df, cpu, gpu; int st; if (!MenuTestResult_(i, g, sc, df, st, cpu, gpu)) break; if (st >= 2) continue;
            sec[nr] = AtUtf8(sc); grp[nr] = AtUtf8(g); dfv[nr] = df; cpv[nr] = cpu; gpv[nr] = gpu; stv[nr] = st; nr++; } }
        const int listRows = std::clamp((int)((vb.y - va.y - 380.0f * k) / lh), 4, 14);
        const float ph = rowH + 8.0f * k + 4.0f * lh + 3.0f * (rowH + 10.0f * k) + 2.0f * lh + (float)listRows * lh + 70.0f * k;
        const ImVec2 p0((va.x + vb.x - pw) * 0.5f, std::max(y0 + 8.0f * k, (y0 + y1 - ph) * 0.5f)), p1(p0.x + pw, p0.y + ph); hit(897, p0, p1);
        dl->AddRectFilled(ImVec2(p0.x + 5.0f * k, p0.y + 6.0f * k), ImVec2(p1.x + 5.0f * k, p1.y + 6.0f * k), IM_COL32(0, 0, 0, 100), 5.0f * k);
        dl->AddRectFilled(p0, p1, IM_COL32(37, 37, 38, 255), 4.0f * k); dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + rowH), cHead, 4.0f * k); dl->AddRect(p0, p1, cLine, 4.0f * k);
        dl->AddRectFilled(p0, ImVec2(p0.x + 3.0f * k, p0.y + rowH), acc);
        text(p0.x + 14.0f * k, p0.y + rowH * 0.5f, ink, "PRUEBA AUTOM\xC3\x81TICA DE SHADERS", fsS + 1.0f * k);
        float y = p0.y + rowH + 8.0f * k; const float lx = p0.x + 16.0f * k, fx0 = p0.x + pw * 0.34f, fx1 = p1.x - 16.0f * k;
        static const char* const kIntro[4] = { "Apaga un momento cada efecto prendido del mod, mide cu\xC3\xA1nto cuesta (procesador y placa), mira si cambia la imagen",
            "y saca una foto. Despu\xC3\xA9s prende una por una las vistas de depuraci\xC3\xB3n y las fotograf\xC3\xAD" "a. Al final deja todo como estaba (no guarda",
            "nada en el ini) y escribe PES21FX\\diagnostico\\<fecha>\\ con resumen.txt, informe.json y las fotos. Con la c\xC3\xA1mara quieta; tarda",
            "de 2 a 4 minutos (10 a 15 con el rendimiento completo). Conviene esconder los paneles (T y N) para que la foto muestre m\xC3\xA1s escena." };
        for (int i = 0; i < 4; i++) text(lx, y + lh * (0.5f + (float)i), dim, kIntro[i], fsS); y += 4.0f * lh + 4.0f * k;
        {   // escena (para el informe de rendimiento)
            text(lx, y + rowH * 0.5f, dim, "Escena del informe", fsS + 1.0f * k);
            const ImVec2 f0(fx0, y + 3.0f * k), f1(fx1, y + rowH - 3.0f * k); dl->AddRectFilled(f0, f1, cField, 3.0f * k);
            const ImVec2 l0(f0.x, f0.y), l1(f0.x + 26.0f * k, f1.y), r0(f1.x - 26.0f * k, f0.y), r1(f1.x, f1.y);
            const bool hl = hit(3525, l0, l1), hr2 = hit(3526, r0, r1);
            if (hl) dl->AddRectFilled(l0, l1, IM_COL32(86, 86, 92, 255), 3.0f * k); if (hr2) dl->AddRectFilled(r0, r1, IM_COL32(86, 86, 92, 255), 3.0f * k);
            arrow((l0.x + l1.x) * 0.5f, (l0.y + l1.y) * 0.5f, 2.0f, ink); arrow((r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f, 0.0f, ink);
            textC(l1, ImVec2(r0.x, r1.y), ink, PerfSceneName(s_atScene), fsS + 1.0f * k); y += rowH + 10.0f * k; }
        {   static const char* const nb[2] = { "Solo con el mod", "Tambi\xC3\xA9n sin el mod (al final)" };
            text(lx, y + rowH * 0.5f, dim, "Comparar", fsS + 1.0f * k); seg(3527, ImVec2(fx0, y + 3.0f * k), ImVec2(fx1, y + rowH - 3.0f * k), 2, s_atBypass ? 1 : 0, nb); y += rowH + 10.0f * k; }
        {   const ImVec2 b0(fx0, y + 2.0f * k), b1(fx1, y + rowH + 4.0f * k); const bool hv = hit(3521, b0, b1);
            dl->AddRectFilled(b0, b1, hv ? IM_COL32(250, 150, 50, 255) : IM_COL32(220, 122, 30, 255), 3.0f * k); textC(b0, b1, IM_COL32(20, 20, 22, 255), "Empezar la prueba", fs); y += rowH + 12.0f * k; }
        {   const std::string st = AtUtf8(MenuTestStatus()); if (!st.empty()) { dl->PushClipRect(ImVec2(lx, y), ImVec2(fx1, y + lh), true); text(lx, y + lh * 0.5f, ink, st.c_str(), fsS); dl->PopClipRect(); }
            char t[200]; const std::wstring& wd = MenuTestDir(); std::string d; for (wchar_t c : wd) d += c < 128 ? (char)c : '_'; const size_t cut = d.find("PES21FX"); if (cut != std::string::npos) d = d.substr(cut);
            if (nr || MenuTestCount(0)) snprintf(t, sizeof t, TRS("Probados %d, cambian la imagen %d, sin probar %d   ruido %.4f   %s"), MenuTestCount(0), MenuTestCount(1), MenuTestCount(2), MenuTestNoise(), d.c_str());
            else snprintf(t, sizeof t, "%s", TRS("Todav\xC3\xAD" "a no hay resultados en esta sesi\xC3\xB3n."));
            text(lx, y + lh * 1.5f, dim, t, fsS); y += 2.0f * lh; }
        {   const ImVec2 L0(p0.x + 10.0f * k, y), L1(p1.x - 10.0f * k, y + (float)listRows * lh); dl->AddRectFilled(L0, L1, IM_COL32(30, 30, 31, 255), 3.0f * k); dl->AddRect(L0, L1, cLine, 3.0f * k);
            s_atScroll = std::clamp(s_atScroll, 0, std::max(0, nr - listRows));
            const int wd2 = s_tlWheel.exchange(0); if (wd2 && mx >= L0.x && mx <= L1.x && my >= L0.y && my <= L1.y) s_atScroll = std::clamp(s_atScroll - (wd2 > 0 ? 3 : -3), 0, std::max(0, nr - listRows));
            const float c1x = L0.x + 8.0f * k, c2x = L0.x + (L1.x - L0.x) * 0.24f, c3x = L0.x + (L1.x - L0.x) * 0.58f, c4x = L0.x + (L1.x - L0.x) * 0.78f;
            dl->PushClipRect(L0, L1, true);
            for (int r = 0; r < listRows && s_atScroll + r < nr; r++) { const int i = s_atScroll + r; const float yc = L0.y + ((float)r + 0.5f) * lh;
                if (r & 1) dl->AddRectFilled(ImVec2(L0.x, yc - lh * 0.5f), ImVec2(L1.x, yc + lh * 0.5f), IM_COL32(255, 255, 255, 8));
                text(c1x, yc, dim, sec[i].c_str(), fsS); text(c2x, yc, ink, grp[i].c_str(), fsS);
                char t[96]; if (stv[i] == 1) snprintf(t, sizeof t, TRS("cambia la imagen (%.4f)"), dfv[i]); else snprintf(t, sizeof t, TRS("sin efecto ac\xC3\xA1 (%.4f)"), dfv[i]);
                text(c3x, yc, stv[i] == 1 ? IM_COL32(120, 220, 120, 255) : IM_COL32(240, 128, 104, 255), t, fsS);
                if (cpv[i] != 0.0f || gpv[i] != 0.0f) { snprintf(t, sizeof t, TRS("%.2f ms CPU / %.2f placa"), cpv[i], gpv[i]); text(c4x, yc, dim, t, fsS); } }
            if (nr == 0) textC(L0, L1, dim, "Ac\xC3\xA1 aparecen los efectos probados: si cambian la imagen y cu\xC3\xA1nto cuestan.", fsS);
            dl->PopClipRect();
            if (nr > listRows) { char t[48]; snprintf(t, sizeof t, TRS("%d-%d de %d  (rueda)"), s_atScroll + 1, std::min(nr, s_atScroll + listRows), nr); text(L0.x + 4.0f * k, L1.y + 12.0f * k, dim, t, fsS - 1.0f * k); } }
        { const char* ct = TRS("Cerrar  (Esc)"); const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, ct); const ImVec2 c0(p1.x - ts.x - 44.0f * k, p1.y - 42.0f * k), c1(p1.x - 12.0f * k, p1.y - 12.0f * k); const bool hv = hit(899, c0, c1);
          dl->AddRectFilled(c0, c1, hv ? IM_COL32(86, 86, 92, 255) : IM_COL32(66, 66, 70, 255), 3.0f * k); dl->AddRect(c0, c1, cLine, 3.0f * k); textC(c0, c1, ink, ct, fs); }
    } } else if (s_modal != 0) {
        const ImVec2 va(leftV + 1.0f, y0), vb(ds.x - rightV - 1.0f, y1); zone(va, vb); hit(898, va, vb);
        dl->AddRectFilled(va, vb, IM_COL32(0, 0, 0, 120));
        const float pw = std::min((s_modal == 1 ? 520.0f : 700.0f) * k, vb.x - va.x - 24.0f * k), lh = 27.0f * k * rf; const int rows = s_modal == 1 ? std::max(1, s_scListN) : kHelpN;
        const float ph = rowH + 12.0f * k + (float)rows * lh + 56.0f * k;
        const ImVec2 p0((va.x + vb.x - pw) * 0.5f, std::max(y0 + 8.0f * k, (y0 + y1 - ph) * 0.5f)), p1(p0.x + pw, p0.y + ph); hit(897, p0, p1);
        dl->AddRectFilled(ImVec2(p0.x + 5.0f * k, p0.y + 6.0f * k), ImVec2(p1.x + 5.0f * k, p1.y + 6.0f * k), IM_COL32(0, 0, 0, 100), 5.0f * k);
        dl->AddRectFilled(p0, p1, IM_COL32(37, 37, 38, 255), 4.0f * k); dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + rowH), cHead, 4.0f * k); dl->AddRect(p0, p1, cLine, 4.0f * k);
        dl->AddRectFilled(p0, ImVec2(p0.x + 3.0f * k, p0.y + rowH), acc);
        text(p0.x + 14.0f * k, p0.y + rowH * 0.5f, ink, s_modal == 1 ? "ABRIR ESCENA" : "CONTROLES Y ATAJOS", fsS + 1.0f * k);
        float y = p0.y + rowH + 8.0f * k;
        if (s_modal == 1) {
            if (s_scListN == 0) text(p0.x + 16.0f * k, y + lh * 0.5f, dim, "Todav\xC3\xAD" "a no hay escenas guardadas. Se guardan con Archivo > Guardar escena (Ctrl + S).", fsS + 1.0f * k);
            for (int i = 0; i < s_scListN; i++) { const ImVec2 r0(p0.x + 8.0f * k, y + (float)i * lh), r1(p1.x - 8.0f * k, y + (float)(i + 1) * lh); const bool hv = hit(800 + i, r0, r1), cur = strcmp(s_scList[i], s_scName) == 0;
                if (hv) dl->AddRectFilled(r0, r1, IM_COL32(240, 138, 36, 96), 3.0f * k); else if (cur) dl->AddRectFilled(r0, r1, cRow, 3.0f * k);
                char lb[96]; SceneLabel(s_scList[i], lb, sizeof lb); text(r0.x + 10.0f * k, (r0.y + r1.y) * 0.5f, ink, lb, fs);
                if (cur) { const ImVec2 ts = font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, TRS("abierta")); text(r1.x - ts.x - 10.0f * k, (r0.y + r1.y) * 0.5f, dim, "abierta", fsS); } }
        } else {
            const float kx = p0.x + 16.0f * k, dx = p0.x + pw * 0.33f;
            for (int i = 0; i < kHelpN; i++) { const float yc = y + (float)i * lh + lh * 0.5f; if (i == 5) dl->AddLine(ImVec2(p0.x + 10.0f * k, yc - lh * 0.5f), ImVec2(p1.x - 10.0f * k, yc - lh * 0.5f), IM_COL32(70, 70, 74, 255), 1.0f);
                const ImVec2 ts = font->CalcTextSizeA(fsS + 1.0f * k, FLT_MAX, 0.0f, TRS(kHelp[i][0]));
                dl->AddRectFilled(ImVec2(kx - 6.0f * k, yc - 10.0f * k), ImVec2(kx + ts.x + 6.0f * k, yc + 10.0f * k), cField, 3.0f * k); dl->AddRect(ImVec2(kx - 6.0f * k, yc - 10.0f * k), ImVec2(kx + ts.x + 6.0f * k, yc + 10.0f * k), IM_COL32(80, 80, 84, 255), 3.0f * k);
                text(kx, yc, IM_COL32(255, 214, 170, 255), kHelp[i][0], fsS + 1.0f * k); text(dx, yc, ink, kHelp[i][1], fsS + 1.5f * k); }
        }
        { const char* ct = TRS("Cerrar  (Esc)"); const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, ct); const ImVec2 c0(p1.x - ts.x - 44.0f * k, p1.y - 42.0f * k), c1(p1.x - 12.0f * k, p1.y - 12.0f * k); const bool hv = hit(899, c0, c1);
          dl->AddRectFilled(c0, c1, hv ? IM_COL32(86, 86, 92, 255) : IM_COL32(66, 66, 70, 255), 3.0f * k); dl->AddRect(c0, c1, cLine, 3.0f * k); textC(c0, c1, ink, ct, fs); }
    }
    // ---------------- menu desplegable abierto (lo ultimo: queda encima y gana el clic) ----------------
    struct MI { const char* t; const char* sc; int id; int chk; int kind; };      // kind 0 accion, 1 separador, 2 titulo de grupo, 3 en preparacion (gris); chk 1 = tilde, 3 = opcion elegida
    auto menuItems = [&](int m, MI* it) { int n = 0;
        auto add = [&](const char* t, const char* sc, int id, int chk = -1, int kind = 0) { if (n < 40) it[n++] = MI{ TRS(t), TRS(sc), id, chk, kind }; };      // (15/11 p) hasta 32; (17/11) 40: el menu Ver ya tenia 34
        if (m == 0) { add("Nueva escena", "Ctrl + N", 710); add("Abrir escena...", "Ctrl + O", 711); add("Guardar escena", "Ctrl + S", 712); add("Guardar como escena nueva", "Ctrl + Shift + S", 713); add(nullptr, "", -1, -1, 1); add("Cerrar el Motor", "Esc", 0); }
        else if (m == 1) { add(player ? "Quitar el jugador" : "Cargar el jugador", "J", 2); add("Grilla de piso", "G", 1, grid ? 1 : 0);
            add("TAMA\xC3\x91O DE LA GRILLA", "", -1, -1, 2); { static const char* const gn[4] = { "M\xC3\xA1xima (hasta el horizonte)", "40 x 40 m", "100 x 100 m", "150 x 150 m" }; static const int gv[4] = { 0, 40, 100, 150 };
              for (int i = 0; i < 4; i++) add(gn[i], "", 780 + i, g_cfg.motorGridSize == gv[i] ? 3 : 2); } add(nullptr, "", -1, -1, 1);
            add("Cargar animaci\xC3\xB3n (.gani)...", "en preparaci\xC3\xB3n", -1, -1, 3); add("Cargar estadio...", "en preparaci\xC3\xB3n", -1, -1, 3); add("Cinem\xC3\xA1ticas del juego...", "en preparaci\xC3\xB3n", -1, -1, 3); }
        else if (m == 2) { add("C\xC3\xA1mara libre", "C", 3, camFree ? 1 : 0); add(nullptr, "", -1, -1, 1); add("MIRAR DESDE", "", -1, -1, 2);
            add("Frente", "Num 1", 232); add("Atr\xC3\xA1s", "Ctrl + Num 1", 235); add("Derecha", "Num 3", 230); add("Izquierda", "Ctrl + Num 3", 233); add("Arriba", "Num 7", 231); add("Abajo", "Ctrl + Num 7", 234);
            add(nullptr, "", -1, -1, 1); add("PROYECCI\xC3\x93N", "Num 5", -1, -1, 2); add("Perspectiva", "", 237, FreeCamOrtho() ? 2 : 3); add("Ortogr\xC3\xA1" "fica", "", 238, FreeCamOrtho() ? 3 : 2);
            add(nullptr, "", -1, -1, 1); add("Vista extra (otra c\xC3\xA1mara)", "X", 968, FreeCamExtra() ? 1 : 0);
            add(nullptr, "", -1, -1, 1); add("Volver a la pose inicial", "Num 0", 202);
            add(nullptr, "", -1, -1, 1); add("TAMA\xC3\x91O DE LA INTERFAZ", "Ctrl +  /  Ctrl -", -1, -1, 2);
            { static const char* const sn[4] = { "Chico", "Mediano", "Grande", "Muy grande" }; const int c = ScaleIdx(); for (int i = 0; i < 4; i++) add(sn[i], "", 750 + i, c == i ? 3 : 2); }
            // (15/11 i, Agustin 03:22: "puedes agregar un selector de idiomas?") el mismo idioma del menu del mod ([PES21FX] language)
            add(nullptr, "", -1, -1, 1); add("IDIOMA", "", -1, -1, 2);
            { static const char* const ln[3] = { "Espa\xC3\xB1ol", "English", "Portugu\xC3\xAAs" }; const int c = std::clamp(g_cfg.lang, 0, 2); for (int i = 0; i < 3; i++) add(ln[i], "", 770 + i, !g_cfg.langAuto && c == i ? 3 : 2); add("Como el juego (autom\xC3\xA1tico)", "", 773, g_cfg.langAuto ? 3 : 2); }
            // (15/11 o, Agustin 05:12) paneles como en Blender: T el de la escena, N el inspector
            add(nullptr, "", -1, -1, 1); add("PANELES", "", -1, -1, 2); add("Panel Escena", "T", 794, g_cfg.motorPanelLOn ? 1 : 0); add("Panel Inspector", "N", 795, g_cfg.motorPanelROn ? 1 : 0); add("Restablecer paneles", "", 796);
            // (17/11 06:52) linea de tiempo de las animaciones (abajo de la vista)
            add("L\xC3\xADnea de tiempo", "", 3508, s_tlMode == 2 ? 0 : 1); }
        else if (m == 3) { add("VISTA", "V  /  Shift + V", -1, -1, 2); { const int c = MotorViewGet(0); for (int i = 0; i < MotorViewCount(0) && i < 16; i++) add(ViewTr(0, i), "", 100 + i, c == i ? 3 : 2); }
            add(nullptr, "", -1, -1, 1); add("ALAMBRE", "Z", -1, -1, 2); { const int c = MotorViewGet(1); for (int i = 0; i < MotorViewCount(1) && i < 4; i++) add(i == 2 ? "S\xC3\xAD, con teselaci\xC3\xB3n" : i == 1 ? "S\xC3\xAD" : ViewTr(1, i), "", 116 + i, c == i ? 3 : 2); }
            add(nullptr, "", -1, -1, 1); add("PANTALLA PARTIDA", "", -1, -1, 2); { const int c = MotorViewGet(2); for (int i = 0; i < MotorViewCount(2) && i < 4; i++) add(ViewTr(2, i), "", 132 + i, c == i ? 3 : 2); }
            // (17/11 06:48, Agustin: "una vista para probar la prueba automatica y ver que todo funcione bien con los shaders")
            add(nullptr, "", -1, -1, 1); add("Prueba autom\xC3\xA1tica de shaders...", "", 3520); }
        else if (m == 4) { add("Prender todas", "L", 700); add("Apagar todas", "L", 701); add("Volver a como estaban", "Shift + L", 702); }
        else if (m == 5) { add("Controles y atajos", "F1", 730); add("Volver a mostrar los errores", "", 500); }
        else {      // (15/11 p, Agustin 05:36: "arriba en editar") Editar: deshacer, rehacer (con cuantos pasos hay) y preferencias
            static char s_ub2[48], s_rb2[48]; snprintf(s_ub2, sizeof s_ub2, TRS("Deshacer  (%d)"), s_undoN); snprintf(s_rb2, sizeof s_rb2, TRS("Rehacer  (%d)"), s_redoN);
            if (n < 40) it[n++] = MI{ s_ub2, TRS("Ctrl + Z"), 720, -1, s_undoN > 0 ? 0 : 3 };
            if (n < 40) it[n++] = MI{ s_rb2, TRS("Ctrl + Y"), 721, -1, s_redoN > 0 ? 0 : 3 };
            add(nullptr, "", -1, -1, 1); add("Preferencias...", "Ctrl + ,", 722); }
        return n; };
    if (s_menuOpen >= 0) {
        MI it[40]; const int n = menuItems(s_menuOpen, it); s_miN = n; s_menuA = std::min(1.0f, s_menuA + ImGui::GetIO().DeltaTime / 0.11f);
        const float ih = 26.0f * k * rf, sepH = 9.0f * k, headH = 22.0f * k * rf, padX = 12.0f * k, chkW = 24.0f * k; float wT = 0.0f, wS = 0.0f, h = 10.0f * k;
        for (int i = 0; i < n; i++) { if (it[i].kind == 1) { h += sepH; continue; } const bool hd = it[i].kind == 2;
            wT = std::max(wT, font->CalcTextSizeA(hd ? fsS : fs, FLT_MAX, 0.0f, it[i].t).x - (hd ? chkW : 0.0f)); wS = std::max(wS, font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, it[i].sc).x); h += hd ? headH : ih; }
        const float w = padX + chkW + wT + (wS > 0.0f ? 40.0f * k + wS : 0.0f) + padX;
        const ImVec2 a(std::max(0.0f, std::min(mX[s_menuOpen], ds.x - w - 4.0f * k)), barH), b(a.x + w, a.y + h);
        zone(a, b); const int v0 = dl->VtxBuffer.Size; hit(699, a, b);
        dl->AddRectFilled(ImVec2(a.x + 4.0f * k, a.y + 5.0f * k), ImVec2(b.x + 4.0f * k, b.y + 5.0f * k), IM_COL32(0, 0, 0, 90), 5.0f * k);
        dl->AddRectFilled(a, b, IM_COL32(43, 43, 46, 253)); dl->AddRect(a, b, cLine);
        float y = a.y + 5.0f * k;
        for (int i = 0; i < n; i++) { const MI& e = it[i]; s_miAct[i] = -1;
            if (e.kind == 1) { dl->AddLine(ImVec2(a.x + 8.0f * k, y + sepH * 0.5f), ImVec2(b.x - 8.0f * k, y + sepH * 0.5f), IM_COL32(70, 70, 74, 255), 1.0f); y += sepH; continue; }
            if (e.kind == 2) { text(a.x + padX, y + headH * 0.55f, dim, e.t, fsS); if (e.sc[0]) { const ImVec2 ts = font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, e.sc); text(b.x - padX - ts.x, y + headH * 0.55f, dim, e.sc, fsS); } y += headH; continue; }
            const ImVec2 p0(a.x + 1.0f, y), p1(b.x - 1.0f, y + ih); const bool en = e.kind == 0; bool hv = false;
            if (en) { s_miAct[i] = e.id; hv = hit(1000 + i, p0, p1); if (s_menuKb >= 0) hv = i == s_menuKb; }
            if (hv) dl->AddRectFilled(p0, p1, IM_COL32(240, 138, 36, 100));
            const float yc = y + ih * 0.5f, cxk = a.x + padX + 7.0f * k; const ImU32 off = IM_COL32(112, 112, 116, 255);
            if (e.chk == 1) { dl->AddLine(ImVec2(cxk - 5.0f * k, yc), ImVec2(cxk - 1.5f * k, yc + 4.0f * k), acc, 2.0f * k); dl->AddLine(ImVec2(cxk - 1.5f * k, yc + 4.0f * k), ImVec2(cxk + 6.0f * k, yc - 4.5f * k), acc, 2.0f * k); }
            else if (e.chk == 3) dl->AddCircleFilled(ImVec2(cxk, yc), 3.4f * k, acc, 12);
            text(a.x + padX + chkW, yc, en ? (hv ? IM_COL32(255, 255, 255, 255) : ink) : off, e.t, fs);
            if (e.sc[0]) { const ImVec2 ts = font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, e.sc); text(b.x - padX - ts.x, yc, en ? (hv ? IM_COL32(255, 230, 200, 255) : dim) : off, e.sc, fsS); }
            y += ih; }
        anim(v0, 0.0f, -(1.0f - s_menuA) * 8.0f * k, s_menuA);
    } else s_miN = 0;
    {   // ---------------- transicion de entrada y de salida (tapa todo menos el cursor) ----------------
        const double now = Now(); float a = 0.0f; const float te = (float)(now - s_enterT);
        // tapado hasta que este todo cargado: la pagina lista (MotorUiReady), la escena leida una vez y los iconos; minimo 0.2 s, tope 2 s.
        // Recien al destaparse entran los paneles deslizando (s_openA)
        if (s_coverEnd < 0.0) { const bool ready = MotorUiReady() && s_sceneOnce && s_iconTried;
            if ((te >= 0.2f && ready) || te >= 2.0f) { s_coverEnd = now; s_openA = 0.0f; Log("motor (editor): entrada destapada a los %.2f s (%s)", te, ready ? "todo cargado" : "tope de 2 s"); } }
        if (s_coverEnd < 0.0) a = 1.0f; else { const float tf = (float)(now - s_coverEnd); if (tf < 0.55f) a = 1.0f - tf / 0.55f; }
        if (MotorUiReady()) LdHit(1, now); if (s_sceneOnce) LdHit(2, now); if (s_iconTried) LdHit(3, now);    // pasos de la carga (barra por tiempo aprendido)
        if (s_coverEnd >= 0.0) LdHit(4, now);
        { int st = 0; const float pr = LdProgress(now, &st); MotorCoverLoad(pr, st); }
        s_cvArriveT.store(-1.0e9);    // la tapa de antes de entrar ya la sigue esta
        // destapar al entrar: side +1 (los bloques se van hacia la derecha). Salir: llegan con side +1 (corriendose a la izquierda) y, ya
        // cerrado el editor, MotorPreCoverDraw sigue tapando hasta que vuelve el Editar y destapa con side -1
        if (s_exitT >= 0.0) { const float tx = (float)(now - s_exitT); a = std::max(a, EaseIO(std::min(1.0f, tx / 0.36f))); MotorCoverLoad(std::min(0.97f, tx / (0.38f + s_ldE[5])), 5);
            if (tx >= 0.38f && !s_exitSent) { s_exitSent = true; MotorUiBack(); s_cvExitT.store(now); s_cvRevT.store(-1.0e9); }
            if (tx > 1.2f) { s_exitT = -1.0; MotorErr("El juego no acept\xC3\xB3 el pedido de volver al Editar. Prob\xC3\xA1 de nuevo con Archivo > Cerrar el Motor."); } }
        MotorGlitchCover(a, 1.0f);
    }
    s_hrN.store(nz);
    // ---------------- acciones: las mismas para el clic, los menus y el teclado ----------------
    auto lightsAll = [&](int mode, bool say) {       // 0 apagar todas, 1 prender todas, 2 como estaban al entrar
        int bad = 0, n = 0;
        for (int i = 0; i < s_nLights; i++) { MotorLight& L = s_lights[i]; bool want = mode == 1;
            if (mode == 2) { bool f = false; for (int q = 0; q < s_origN; q++) if (s_orig[q].obj == L.obj) { want = s_orig[q].on; f = true; } if (!f) continue; }
            if (L.on == want) continue;
            if (L.writable && MotorLightSetOn(L, want)) { L.on = want; n++; } else bad++; }
        if (bad) MotorErr("%d luces no se pudieron cambiar: son de una clase que el Motor no conoce o el juego las sac\xC3\xB3 de la escena mientras tanto.", bad);
        if (say) { if (mode == 0) Note("%d luces apagadas", n); else if (mode == 1) Note("%d luces prendidas", n); else Note("%d luces vueltas a como estaban", n); } };
    auto panelSet = [&](int sd, int on) { int& v = sd == 0 ? g_cfg.motorPanelLOn : g_cfg.motorPanelROn; v = on ? 1 : 0; SaveOption(sd == 0 ? "motor_panel_izq_abierto" : "motor_panel_der_abierto", on ? "1" : "0"); };
    auto act0 = [&](int id) {
        if (id != 3350 && id != 3351 && id != 3360 && id != 3361) s_srOn.store(0);      // (07:27) clic en otra cosa: deja de escribir en el buscador
        if (id == 0) { if (s_exitT < 0.0) { s_exitT = Now(); s_exitSent = false; MotorGlitchSound(1); } }      // salir: primero el fundido (abajo)
        else if (id == 1 || id == 20) g_cfg.motorGrid = grid ? 0 : 1;
        else if (id == 1400) { MotorJugReset(); Note("Jugador al centro"); }
        else if (id == 1460) { PjPick(-1, false); s_sel = 3; s_selObj = 0; }
        else if (id >= 3000 && id < 3000 + kMotorPjMax) { PjPick(id - 3000, (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0); s_sel = 3; s_selObj = 0; s_pjGroup = -1; }
        else if (id >= 3200 && id < 3200 + kMotorPjMax) { MotorPjRemove(id - 3200); Note("Jugador sacado"); }
        else if (id == 3508) { s_tlMode = s_tlMode == 2 ? 1 : 2; Note(s_tlMode == 2 ? "L\xC3\xADnea de tiempo oculta" : "L\xC3\xADnea de tiempo visible"); }
        else if (id == 3520) { s_modal = 5; s_menuOpen = -1; }
        else if (id == 3530 || id == 3531) { g_motorWireCol.store(id == 3531); Note(id == 3531 ? "Alambre: un color por malla (te\xC3\xB1ido sobre la textura)" : "Alambre: con su textura"); }
        else if (id == 3521) { if (!MenuTestActive()) { MenuTestStart(s_atScene, s_atBypass); s_atScroll = 0; } }
        else if (id == 3524) MenuTestStop();
        else if (id == 3525 || id == 3526) s_atScene = (s_atScene + (id == 3526 ? 1 : PerfSceneCount() - 1)) % std::max(1, PerfSceneCount());
        else if (id == 3527 || id == 3528) s_atBypass = id == 3528;
        else if (id >= 3510 && id <= 3513) {      // (17:52) velocidad de la linea de tiempo: al elegido o a todos (segun "El elegido / Todos")
            static const float kSp[4] = { 0.1f, 0.25f, 0.5f, 1.0f }; const float sp = kSp[id - 3510];
            static MotorPjInfo PI; MotorPjInfoGet(&PI); const int qs = MotorPjSel(); int n = 0;
            for (int i = 0; i < kMotorPjMax; i++) { if (PI.st[i] != 3 || (!s_tlAll && qs >= 0 && i != qs)) continue; MotorPjAnimSet(i, PI.clip[i], sp, PI.paused[i]); n++; }
            Note(n ? "Velocidad %.2fx" : "No hay jugadores listos", sp);
        }
        else if (id >= 3500 && id <= 3507) {      // (06:52) linea de tiempo: principio, cuadro atras, reproducir / pausa, cuadro adelante, final
            static MotorPjInfo PI; MotorPjInfoGet(&PI);
            if (id == 3505 || id == 3506) s_tlAll = id == 3506;
            else if (id == 3507) { s_tlMode = 2; Note("L\xC3\xADnea de tiempo oculta (Ver > L\xC3\xADnea de tiempo)"); }
            else { const int qs = MotorPjSel(); int ref = qs >= 0 && PI.st[qs] == 3 ? qs : -1;
                if (ref < 0) for (int i = 0; i < kMotorPjMax && ref < 0; i++) if (PI.st[i] == 3) ref = i;
                if (ref < 0) Note("No hay jugadores listos");
                else { const bool pauseAll = !PI.paused[ref];     // reproducir / pausa: todos toman el estado del de referencia
                    for (int i = 0; i < kMotorPjMax; i++) { if (PI.st[i] != 3 || (!s_tlAll && i != ref)) continue;
                        const float len = std::max(0.0f, PI.lenT[i]);
                        if (id == 3502) MotorPjAnimSet(i, PI.clip[i], PI.speed[i], pauseAll);
                        else { float t = id == 3500 ? 0.0f : id == 3504 ? std::max(0.0f, len - 0.001f) : PI.curT[i] + (id == 3501 ? -1.0f : 1.0f) / 30.0f;
                            if (len > 0.0f) { if (t < 0.0f) t += len; if (t >= len) t -= len; } t = std::clamp(t, 0.0f, std::max(0.0f, len - 0.001f));
                            MotorPjSeek(i, t); if (!PI.paused[i] && id != 3500) MotorPjAnimSet(i, PI.clip[i], PI.speed[i], true); } } } }
        }
        else if (id >= 3100 && id < 3100 + kMotorPjMax) { static MotorPjInfo PI; MotorPjInfoGet(&PI); const int q = id - 3100; MotorPjShow(q, PI.hidden[q]); Note(PI.hidden[q] ? "Jugador visible" : "Jugador oculto"); }
        else if ((id >= 3300 && id <= 3343) || (id >= 3360 && id <= 3362)) {      // (17/11 tarde) 3360-3362 copiar, pegar, ocultar: antes quedaban fuera del rango      // (06:36..06:38) opciones del jugador elegido: mover, animacion y gesto
            static MotorPjInfo PI; MotorPjInfoGet(&PI); const int q = MotorPjSel();
            const int nC = MotorClipCount(), nG = std::max(1, MotorClipGroupCount()), nF = MotorFaceCount();
            const int cur = q >= 0 && PI.clip[q] >= 0 && PI.clip[q] < nC ? PI.clip[q] : MotorClipIdle();
            if (id == 3303 || id == 3304) s_pjSnap = id == 3304;
            else if (id >= 3305 && id <= 3307) { s_camFollow = id - 3305; Note(s_camFollow == 2 ? "La c\xC3\xA1mara sigue la cara del jugador elegido" : s_camFollow ? "La c\xC3\xA1mara sigue al jugador elegido" : "C\xC3\xA1mara quieta"); }
            else if (id == 3302) {      // todos en fila sobre X, mirando a +Z, separados 1,2 m, centrados
                int idx[kMotorPjMax], n = 0; for (int i = 0; i < kMotorPjMax; i++) if (PI.st[i] >= 1 && PI.st[i] <= 3) idx[n++] = i;
                for (int j = 0; j < n; j++) { float p[3], yw, sc; if (!MotorPjGet(idx[j], p, &yw, &sc)) continue; p[0] = ((float)j - 0.5f * (float)(n - 1)) * 1.2f; p[1] = 0.0f; p[2] = 0.0f; MotorPjSet(idx[j], p, 0.0f, sc); }
                Note("%d jugadores en fila", n); }
            else if (q < 0) {}
            else if (id == 3300) { if (MotorPjDup(q) >= 0) Note("Jugador duplicado"); }
            else if (id == 3301) { float o[3], d[3], p[3], yw, sc;
                if (MotorGridRay(0.5f, 0.5f, o, d) && MotorPjGet(q, p, &yw, &sc)) { MotorPjSet(q, p, atan2f(o[0] - p[0], o[2] - p[2]) * 57.29578f, sc); Note("Mirando a la c\xC3\xA1mara"); } }
            else if (id == 3310 || id == 3311) { s_pjGroup = ((s_pjGroup < 0 ? MotorClipGroup(cur) : s_pjGroup) + (id == 3311 ? 1 : nG - 1)) % nG; s_pjListTop = -1; }
            else if (id == 3312 || id == 3313) { const int c = (cur + (id == 3313 ? 1 : nC - 1)) % std::max(1, nC); MotorPjAnimSet(q, c, PI.speed[q], PI.paused[q]); s_pjGroup = MotorClipGroup(c); }
            else if (id == 3314 || id == 3315) s_pjListTop += id == 3315 ? 6 : -6;
            else if (id == 3362) PjHideSel();
            else if (id == 3360 || id == 3361) { const int q = MotorPjSel(); static MotorPjInfo PI; MotorPjInfoGet(&PI);
            if (q >= 0 && id == 3360) { PjClip& c = s_pjCb; c.ok = MotorPjGet(q, c.pos, &c.yaw, &c.scale); c.clip = PI.clip[q]; c.speed = PI.speed[q]; c.paused = PI.paused[q]; c.face = PI.face[q]; c.faceW = PI.faceW[q]; c.blink = PI.blink[q];
                if (c.ok) Note("Copiado: lugar, giro, tama\xC3\xB1o, animaci\xC3\xB3n y gesto del jugador %d", q + 1); }
            else if (q >= 0 && s_pjCb.ok) { const PjClip& c = s_pjCb; UndoBegin(); MotorPjSet(q, c.pos, c.yaw, c.scale); UndoEnd(); MotorPjAnimSet(q, c.clip, c.speed, c.paused); MotorPjFaceSet(q, c.face, c.faceW, c.blink); Note("Pegado en el jugador %d", q + 1); }
            else if (q >= 0) Note("Primero copi\xC3\xA1 un jugador (Ctrl + C)"); }
        else if (id == 3370 || id == 3371) { MotorVistaHideEditor(id == 3370); Note(id == 3370 ? "Jugador del Editar oculto" : "Jugador del Editar a la vista"); }
        else if (id == 3350) { s_srOn.store(1); }
            else if (id == 3351) { std::lock_guard<std::mutex> lk(s_srMx); s_sr[0] = 0; s_srOn.store(0); s_srTop = 0; }
            else if (id == 3352 || id == 3353) s_srTop += id == 3353 ? 6 : -6;
            else if (id == 3320 || id == 3321) MotorPjAnimSet(q, PI.clip[q], PI.speed[q], id == 3321);
            else if (id == 3322) MotorPjRestart(q);
            else if (id == 3323) { MotorPjAnimSet(q, -1, PI.speed[q], false); s_pjGroup = MotorClipGroup(MotorClipIdle()); }
            else if (id == 3324) { MotorPjAnimSet(-1, PI.clip[q], PI.speed[q], PI.paused[q]); MotorPjRestart(-1); Note("Animaci\xC3\xB3n puesta a todos"); }
            else if (id == 3330 || id == 3331) { const int f = PI.face[q] + (id == 3331 ? 1 : -1); MotorPjFaceSet(q, f < -1 ? nF - 1 : f >= nF ? -1 : f, PI.faceW[q], PI.blink[q]); }
            else if (id == 3332 || id == 3333) s_pjFaceScroll += id == 3333 ? 5 : -5;
            else if (id == 3340 || id == 3341) MotorPjFaceSet(q, PI.face[q], PI.faceW[q], id == 3340);
            else if (id == 3342) { MotorPjFaceSet(-1, PI.face[q], PI.faceW[q], PI.blink[q]); Note("Gesto puesto a todos"); }
            else if (id == 3343) MotorPjFaceSet(q, -1, PI.faceW[q], PI.blink[q]);
        }
        else if (id >= 3700 && id <= 3719) { const int q = MotorPjSel();       // (23:08) EDITAR ANIMACION
            static MotorEdInfo E; const bool hasE = q >= 0 && MotorEdInfoGet(q, &E);
            auto seekKey = [&](float t) { if (t < 0.0f || q < 0) return; MotorPjInfo PI2; MotorPjInfoGet(&PI2); MotorPjSeek(q, t); MotorPjAnimSet(q, PI2.clip[q], PI2.speed[q], true); };
            auto clipPath = [&](std::wstring& out) { MotorPjInfo PI2; MotorPjInfoGet(&PI2); const std::wstring& d = SceneDir(); if (d.empty() || q < 0) return false;
                const char* f = PI2.clip[q] >= 0 ? MotorClipFile(PI2.clip[q]) : "parado"; char nm[160]; snprintf(nm, sizeof nm, "editor_%s.txt", f); out = ScenePath(nm); return true; };
            if ((id == 3700 || id == 3701) && hasE) { const int dir = id == 3700 ? -1 : 1; int b = s_poseBone;      // solo los huesos con nombre
                for (int g = 0; g < E.n; g++) { b = (b + dir + E.n) % E.n; if (E.lab[b]) break; } s_poseBone = b; }
            else if (q < 0) {}
            else if (id == 3702) { MotorEdSet(q, s_poseBone, nullptr); Note("Hueso sin giro"); }
            else if (id == 3703) { MotorEdKey(q, 5, 0, 0.0f); Note("Se sac\xC3\xB3 todo lo editado"); }
            else if (id >= 3704 && id <= 3706) { MotorEdOpt(id - 3704, -1); static const char* const nn[3] = { "Entre claves: fluida", "Entre claves: con peso", "Entre claves: lineal" }; Note("%s", TRS(nn[id - 3704])); }
            else if (id == 3707 || id == 3708) MotorEdOpt(-1, id == 3707 ? 1 : 0);
            else if (id == 3709) { if (MotorEdKey(q, 0, 0, 0.0f) >= 0.0f) Note("Clave guardada"); }
            else if (id == 3710 && hasE) { int best = -1; for (int k2 = 0; k2 < E.nKeys; k2++) if (fabsf(E.keyT[k2] - E.curT) < 0.5f / 30.0f) best = k2; if (best >= 0) { MotorEdKey(q, 1, best, 0.0f); Note("Clave borrada"); } else Note("No hay clave en este cuadro"); }
            else if ((id == 3714 || id == 3715) && hasE) { float t = -1.0f;
                for (int k2 = 0; k2 < E.nKeys; k2++) { const float kt = E.keyT[k2]; if (id == 3714 ? kt < E.curT - 0.01f : kt > E.curT + 0.01f) { if (t < 0.0f || (id == 3714 ? kt > t : kt < t)) t = kt; } }
                if (t >= 0.0f) seekKey(t); }
            else if (id == 3711) { if (MotorEdKey(q, 3, 0, 0.0f) >= 0.0f) Note("Pose copiada"); }
            else if (id == 3712) { if (MotorEdKey(q, 4, 0, 0.0f) >= 0.0f) Note("Pose pegada"); else Note("Primero copi\xC3\xA1 una pose"); }
            else if (id == 3713) { MotorEdKey(q, 6, 0, 0.0f); Note("Pose espejada"); }
            else if (id == 3716) { std::wstring f; if (clipPath(f) && MotorEdSave(q, f.c_str())) Note("Trabajo guardado (content\\motor-tiempo-real)"); else MotorErr("No se pudo guardar el trabajo del editor"); }
            else if (id == 3717) { std::wstring f; char cl[160]; if (clipPath(f) && MotorEdLoad(q, f.c_str(), cl, sizeof cl)) Note("Trabajo abierto"); else MotorErr("No hay trabajo guardado para esta animaci\xC3\xB3n"); }
            else if (id == 3718 || id == 3719) s_edPts = id == 3718; }
        else if (id >= 3600 && id <= 3604) { MotorLightSetReq(id - 3600); static const char* const nn[5] = { "Luces del Editar", "Luces: Default", "Luces: EditFace", "Luces: EditSpike", "Luces: MatchMenu" }; Note("%s", TRS(nn[id - 3600])); }
        else if ((id >= 3380 && id <= 3388)) {      // (18:01) pestanas de la lista, modo al terminar y desfase
            static MotorPjInfo PI; MotorPjInfoGet(&PI); const int q = MotorPjSel();
            if (id <= 3382) { s_pjTab = id - 3380; s_flTop = 0; }
            else if (id == 3387 || id == 3388) s_flTop += id == 3388 ? 6 : -6;
            else if (id <= 3385 && q >= 0) { const int m = id - 3383; MotorPjModeSet(q, m);
                for (int i = 0; i < kMotorPjMax; i++) { float p[3], yw, sc; if (((s_pjMulti >> i) & 1u) && i != q && MotorPjGet(i, p, &yw, &sc)) MotorPjModeSet(i, m); } }
            else if (id == 3386 && q >= 0) { int js[kMotorPjMax], n = 0; const int c = PI.clip[q];
                for (int i = 0; i < kMotorPjMax; i++) if (PI.st[i] == 3 && PI.clip[i] == c && PI.clipOn[i] && PI.lenT[i] > 0.0f) js[n++] = i;
                if (n < 2) Note("PonÃ© primero esta animaciÃ³n a varios jugadores");
                else { for (int j = 0; j < n; j++) { const int i = js[j]; MotorPjSeek(i, std::min(PI.lenT[i] * (float)j / (float)n, PI.lenT[i] - 0.001f)); }
                       Note("%d jugadores desfasados", n); } }
        }
        else if (id >= 3400 && id < 3408 && MotorClipIsProp(s_pjRowClip[id - 3400])) Note("Es la animaci\xC3\xB3n de un objeto (banderines, copas): no va en un jugador");
        else if (id >= 3400 && id < 3408) { const int q = MotorPjSel(); const int c = s_pjRowClip[id - 3400]; if (q >= 0 && c >= 0) { static MotorPjInfo PI; MotorPjInfoGet(&PI); MotorPjAnimSet(q, c, PI.speed[q], false); RecPush(c); } }
        else if (id >= 3410 && id < 3416) { const int q = MotorPjSel(); const int f = s_pjRowFace[id - 3410]; if (q >= 0 && f >= 0) { static MotorPjInfo PI; MotorPjInfoGet(&PI); MotorPjFaceSet(q, f, PI.faceW[q], PI.blink[q]); } }
        else if (id >= 1480 && id <= 1482) { if (MotorPjAdd(id - 1480, (uint32_t)(s_pjIdF + 0.5f)) >= 0) Note(id == 1480 ? "Cargando jugador por id" : id == 1481 ? "Cargando el jugador base" : "Cargando igual al del Editar"); }
        else if (id == 1430 || id == 1431) { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.live[519][0] = (float)(id - 1430); if (id == 1431 && g_cfg.live[519][1] < 0.5f) g_cfg.live[519][1] = 2.0f; }
        else if (id >= 1434 && id <= 1436) { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.live[519][2] = (float)(id - 1434); }
        else if (id >= 1446 && id <= 1449) { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.live[426][1] = (float)(id - 1446); }
        else if (id >= 1440 && id <= 1442) { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.live[426][0] = (float)(id - 1440); }
        else if (id == 1404 || id == 1405) MotorJugSetFix(id == 1404);
        else if (id == 1410 || id == 1411) { s_jKind = id - 1410; s_jTouched = true; }
        else if (id == 1414 || id == 1415) { const int nMax = s_jKind == 1 ? 162 : 45; int& n = s_jN[s_jKind]; n += id == 1414 ? -1 : 1; if (n < 1) n = nMax; if (n > nMax) n = 1; s_jTouched = true; }
        else if (id == 1418 || id == 1419) MotorJugPause(id == 1419);
        else if (id == 1422 || id == 1423) { s_jMirror = id == 1423; s_jTouched = true; }
        else if (id == 2 || id == 21 || id == 220) { s_sel = 3; s_selObj = 0; }      // (03:23) el jugador del Editar no va en el Motor: abre Jugadores
        else if (id == 3) SetCam(!camFree);
        else if (id >= 10 && id < 14) { s_sel = id - 10; s_selObj = 0; }
        else if (id >= 300 && id < 364 && id - 300 < s_nLights) { s_sel = 4 + (id - 300); s_selObj = s_lights[id - 300].obj; }
        else if (id >= 400 && id < 464 && id - 400 < s_nLights) { MotorLight& L = s_lights[id - 400];
            if (!L.writable) MotorErr("La luz \"%s\" es de una clase que el Motor no conoce todav\xC3\xAD" "a: se puede ver pero no cambiar.", L.name);
            else if (MotorLightSetOn(L, !L.on)) L.on = !L.on; else MotorErr("No se pudo %s la luz \"%s\": el juego la sac\xC3\xB3 de la escena mientras tanto.", TRS(L.on ? "apagar" : "prender"), L.name); }
        else if ((id == 250 || id == 251) && s_sel >= 4 && s_sel - 4 < s_nLights) { MotorLight& L = s_lights[s_sel - 4];
            if (!L.writable) MotorErr("La luz \"%s\" es de una clase que el Motor no conoce todav\xC3\xAD" "a: se puede ver pero no cambiar.", L.name);
            else if (MotorLightSetOn(L, id == 250)) L.on = id == 250; else MotorErr("No se pudo %s la luz \"%s\": el juego la sac\xC3\xB3 de la escena mientras tanto.", TRS(id == 250 ? "prender" : "apagar"), L.name); }
        else if (id == 500) { int tot = 0; { std::lock_guard<std::mutex> lk(s_errMx); tot = s_errTotal; for (MErr& m : s_err) if (m.msg[0]) m.t = Now(); } if (tot == 0) Note("No hubo errores del Motor"); }
        else if (id >= 100 && id < 148) MotorViewSet((id - 100) / 16, (id - 100) % 16);
        else if (id == 200 || id == 201) SetCam(id == 200);
        else if (id == 202) { if (camFree) FreeCamGizmoHome(); else Note("La pose inicial es de la c\xC3\xA1mara libre: Ver > C\xC3\xA1mara libre (C)"); }
        else if (id >= 230 && id < 236) { static const int kAx[6] = { 0, 2, 4, 1, 3, 5 };      // +X, +Y, +Z / -X, -Y, -Z
            if (camFree) FreeCamGizmoAxis(kAx[id - 230]); else Note("Mirar desde un lado es de la c\xC3\xA1mara libre: Ver > C\xC3\xA1mara libre (C)"); }
        else if (id == 968) { const int on = !FreeCamExtra(); if (on && !camFree) SetCam(true); FreeCamExtraSet(on); Note(on ? "Vista extra prendida (cada vista se renueva a la mitad de los cuadros)" : "Vista extra apagada"); }
        else if (id >= 962 && id <= 964) { static const int kp[3] = { 0, 1, 2 }; FreeCamExtraPreset(kp[id - 962]); }
        else if (id == 965) { FreeCamExtraSwap(); Note("Cuadros de la vista extra intercambiados"); }
        else if (id == 966) { FreeCamExtraSet(0); Note("Vista extra apagada"); }
        else if (id == 236 || id == 237 || id == 238) {      // (15/11 q, Agustin 17:28) perspectiva / ortografica (Num 5); 237 / 238 = elegir una
            const int on = id == 236 ? !FreeCamOrtho() : id == 238; if (on && !camFree) SetCam(true); FreeCamOrthoSet(on); Note(on ? "Vista ortogr\xC3\xA1" "fica" : "Vista en perspectiva"); }
        else if (id == 210 || id == 211) g_cfg.motorGrid = id == 210 ? 1 : 0;
        else if (id >= 700 && id < 703) lightsAll(id == 700 ? 1 : id == 701 ? 0 : 2, true);
        else if (id == 710) {      // escena nueva: el estudio vacio, como al entrar
            if (MotorUiPlayer()) MotorUiTogglePlayer();
            g_cfg.motorGrid = 1; for (int r = 0; r < 3; r++) MotorViewSet(r, 0);
            if (camFree) FreeCamGizmoHome();
            lightsAll(2, false); s_pendN = 0; s_scName[0] = 0; s_sel = 0; s_selObj = 0; Note("Escena nueva: el estudio vac\xC3\xADo"); }
        else if (id == 711) { SceneList(); s_modal = 1; }
        else if (id == 712 || id == 713) SceneSave(id == 713);
        else if (id == 730) s_modal = 2;
        else if (id >= 750 && id < 754) ScaleSet(id - 750);
        else if (id >= 770 && id < 774) LangSet(id - 770);
        else if (id >= 780 && id < 784) { static const int gv[4] = { 0, 40, 100, 150 }; g_cfg.motorGridSize = gv[id - 780]; if (!grid) g_cfg.motorGrid = 1; char b[8]; snprintf(b, sizeof b, "%d", gv[id - 780]); SaveOption("motor_grilla_tamano", b); if (gv[id - 780] == 0) Note("Grilla hasta el horizonte"); else Note("Grilla de %d x %d m", gv[id - 780], gv[id - 780]); }
        else if (id == 760 || id == 761) ScaleSet(ScaleIdx() + (id == 760 ? 1 : -1));
        else if (id == 740 || id == 741) { const int n = std::max(1, MotorViewCount(0)); MotorViewSet(0, (MotorViewGet(0) + (id == 740 ? 1 : n - 1)) % n); Note("Vista: %s", ViewTr(0, MotorViewGet(0))); }
        else if (id == 742) { const int n = std::max(1, std::min(3, MotorViewCount(1))); MotorViewSet(1, (MotorViewGet(1) + 1) % n); }
        else if (id >= 800 && id < 814) { if (id - 800 < s_scListN) SceneLoad(s_scList[id - 800]); s_modal = 0; }
        else if (id == 898 || id == 899) s_modal = 0;
        // (15/11 o / p) arbol, paneles, grilla, inspector y Preferencias
        else if (id == 14) s_foldStudio = !s_foldStudio;
        else if (id == 15) s_foldLights = !s_foldLights;
        else if (id == 16) s_foldPj = !s_foldPj;
        else if (id == 992 || id == 994 || id == 794 || id == 910 || id == 911) panelSet(0, id == 910 ? 1 : id == 911 ? 0 : !g_cfg.motorPanelLOn);
        else if (id == 993 || id == 995 || id == 795 || id == 912 || id == 913) panelSet(1, id == 912 ? 1 : id == 913 ? 0 : !g_cfg.motorPanelROn);
        else if (id == 796) { g_cfg.motorPanelLW = 250.0f; g_cfg.motorPanelRW = 320.0f; SaveOption("motor_panel_izq", "250"); SaveOption("motor_panel_der", "320"); panelSet(0, 1); panelSet(1, 1);
            g_cfg.motorDock[0] = 0; g_cfg.motorDock[1] = 1; g_cfg.motorSplit = 0.5f; g_cfg.motorStackTop = 0; DockSave(); Note("Paneles como vinieron"); }
        else if (id >= 784 && id < 788) { static const float cv[4] = { 0.25f, 0.5f, 1.0f, 2.0f }; g_cfg.motorGridCell = cv[id - 784]; if (!grid) g_cfg.motorGrid = 1; GridSave(); }
        else if (id >= 788 && id < 791) { static const int mv[3] = { 0, 5, 10 }; g_cfg.motorGridMajor = mv[id - 788]; GridSave(); }
        else if (id == 792 || id == 793) { g_cfg.motorGridAxes = id == 792 ? 1 : 0; GridSave(); }
        else if (id == 799) { g_cfg.motorGridAlpha = 100; g_cfg.motorGridCell = 1.0f; g_cfg.motorGridMajor = 5; g_cfg.motorGridAxes = 1; g_cfg.motorGridSize = 0; g_cfg.motorGrid = 1; GridSave(); Note("Grilla como vino"); }
        else if (id == 260) s_colorOpen = !s_colorOpen;
        else if ((id == 252 || id == 253) && s_sel >= 4 && s_sel - 4 < s_nLights) { MotorLight& L = s_lights[s_sel - 4];
            if (L.shadow != (id == 252)) { if (MotorLightSetShadow(L, id == 252)) L.shadow = id == 252; else MotorErr("No se pudo cambiar la sombra de la luz \"%s\": el juego la sac\xC3\xB3 de la escena o no es una clase que conozco.", L.name); } }
        else if (id >= 1100 && id < 1140 && id - 1100 < s_secN) { FoldS& F = FoldOf(s_secHash[id - 1100]); F.shut = !F.shut; }
        else if (id == 720 || id == 721) UndoStep(id == 721);
        else if (id == 722) { s_modal = 3; s_menuOpen = -1; }
        else if (id >= 900 && id < 905) s_prefCat = id - 900;
        else if (id >= 940 && id < 945) { static const int rv[5] = { 0, 50, 75, 100, 150 }; g_cfg.motorRes = rv[id - 940]; char b[8]; snprintf(b, sizeof b, "%d", rv[id - 940]); SaveOption("motor_resolucion", b); }
        else if (id == 920 || id == 921) { g_cfg.motorAutoSave = id == 920 ? 1 : 0; SaveOption("motor_autoguardado", id == 920 ? "1" : "0"); s_autoT = -1.0; }
        else if (id >= 922 && id < 926) { static const int mv[4] = { 1, 2, 5, 10 }; g_cfg.motorAutoSaveMin = mv[id - 922]; char b[8]; snprintf(b, sizeof b, "%d", mv[id - 922]); SaveOption("motor_autoguardado_minutos", b); s_autoT = -1.0; }
        else if (id == 930 || id == 931) { g_cfg.motorSound = id == 930 ? 1 : 0; SaveOption("motor_sonido", id == 930 ? "1" : "0"); }
    };
    auto act = [&](int id) {      // (15/11 n) cada accion guarda como estaba la escena antes (Ctrl+Z)
        if (id == 0 || id == 720 || id == 721) { act0(id); return; }      // salir, deshacer y rehacer no son un paso
        UndoS before; UndoSnap(before); act0(id); UndoS after; UndoSnap(after);
        if (!UndoSame(before, after)) { UndoPush(s_undo, s_undoN, before); s_redoN = 0; } };
    // ---------------- teclado (el juego no lo ve dentro del Motor); volando con el boton derecho las letras son de la camara ----------------
    { auto tap = [&](int vk) { const bool d = (GetAsyncKeyState(vk) & 0x8000) != 0; const bool t = d && !s_kWas[vk & 255]; s_kWas[vk & 255] = d; return t; };
      const bool tF1 = tap(VK_F1), tF2 = tap(VK_F2), tEsc = tap(VK_ESCAPE), tLe = tap(VK_LEFT), tRi = tap(VK_RIGHT), tUp = tap(VK_UP), tDn = tap(VK_DOWN), tEn = tap(VK_RETURN), tDel = tap(VK_DELETE),
                 tN = tap('N'), tO = tap('O'), tS = tap('S'), tG = tap('G'), tH = tap('H'), tJ = tap('J'), tC = tap('C'), tV = tap('V'), tZ = tap('Z'), tY = tap('Y'), tL = tap('L'),
                 t0 = tap(VK_NUMPAD0), t1 = tap(VK_NUMPAD1), t3 = tap(VK_NUMPAD3), t5 = tap(VK_NUMPAD5), t7 = tap(VK_NUMPAD7),
                 tPa = tap(VK_OEM_PLUS), tPb = tap(VK_ADD), tMa = tap(VK_OEM_MINUS), tMb = tap(VK_SUBTRACT), tPl = tPa || tPb, tMi = tMa || tMb,
                 tT = tap('T'), tX = tap('X'), tCm = tap(VK_OEM_COMMA), tDot = tap(VK_OEM_PERIOD), tDec = tap(VK_DECIMAL), tBk = tap(VK_BACK), tSpc = tap(VK_SPACE);
      // (15/11 o) numeros para escribir en un campo: los de arriba y los del teclado numerico (los de este ya se miraron arriba)
      int dig = -1; for (int d = 0; d <= 9; d++) if (tap('0' + d)) dig = d;
      { const bool np[10] = { t0, t1, tap(VK_NUMPAD2), t3, tap(VK_NUMPAD4), t5, tap(VK_NUMPAD6), t7, tap(VK_NUMPAD8), tap(VK_NUMPAD9) }; for (int d = 0; d <= 9; d++) if (np[d]) dig = d; }
      // (15/11 p, Agustin 05:37: "que tambien funcione el atajo, y que se muestre el icono abajo a la derecha") cada atajo deja su nombre
      // para el cuadrito de teclas (overlay.cpp KeyHudDraw lo pone arriba de las teclas)
      auto kact = [&](int id) { static const struct { int id; const char* t; } kN[] = {
              { 0, "Salir del Motor" }, { 1, "Grilla de piso" }, { 2, "Jugador" }, { 3, "C\xC3\xA1mara libre o del juego" }, { 202, "Pose inicial de la c\xC3\xA1mara" },
              { 230, "Mirar desde la derecha" }, { 231, "Mirar desde arriba" }, { 232, "Mirar de frente" }, { 233, "Mirar desde la izquierda" }, { 234, "Mirar desde abajo" }, { 235, "Mirar desde atr\xC3\xA1s" }, { 236, "Perspectiva u ortogr\xC3\xA1" "fica" }, { 968, "Vista extra" },
              { 700, "Prender todas las luces" }, { 701, "Apagar todas las luces" }, { 702, "Luces como estaban" }, { 710, "Escena nueva" }, { 711, "Abrir escena" }, { 712, "Guardar escena" },
              { 713, "Guardar como escena nueva" }, { 722, "Preferencias" }, { 730, "Controles y atajos" }, { 740, "Vista siguiente" }, { 741, "Vista anterior" }, { 742, "Alambre" },
              { 760, "Agrandar la interfaz" }, { 761, "Achicar la interfaz" }, { 794, "Panel Escena" }, { 795, "Panel Inspector" } };
          for (const auto& e : kN) if (e.id == id) KbLabel(e.t);
          act(id); };
      auto kundo = [&](bool redo) { KbLabel(redo ? "Rehacer" : "Deshacer"); UndoStep(redo); };
      const bool kb = front && !g_menuOpen.load(std::memory_order_relaxed) && !g_menuTyping.load(std::memory_order_relaxed) && !s_srOn.load(std::memory_order_relaxed) && !s_srEsc.exchange(false) && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0;
      const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0, alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
      if (fabsf(mx - s_mLastX) + fabsf(my - s_mLastY) > 2.0f) { s_mLastX = mx; s_mLastY = my; s_menuKb = -1; }      // el mouse se movio: manda el mouse
      if (!kb) {}
      else if (s_editId >= 0 && !s_editDone) {      // (15/11 o) escribiendo un numero: el primero reemplaza lo que habia; Enter aplica, Esc cancela
          char c = 0; if (dig >= 0) c = (char)('0' + dig); else if (tDot || tDec || tCm) c = '.'; else if (tMa || tMb) c = '-';
          if (c) { if (s_editFresh) { s_editLen = 0; s_editFresh = false; } if (s_editLen < 22) s_editBuf[s_editLen++] = c; s_editBuf[s_editLen] = 0; }
          else if (tBk) { if (s_editFresh) { s_editLen = 0; s_editFresh = false; } else if (s_editLen > 0) s_editLen--; s_editBuf[s_editLen] = 0; }
          else if (tEn) { s_editDone = 1; s_editAge = 0; }
          else if (tEsc) { s_editDone = 2; s_editAge = 0; s_escT.store(Now()); } }
      else if (s_modal != 0) { if (tEsc || tEn) { s_modal = 0; s_escT.store(Now()); } else if (ctrl && !alt && tCm && s_modal == 3) { s_modal = 0; KbLabel("Preferencias"); } else if (ctrl && !alt && (tZ || tY)) kundo(tY || shift); }
      else if (s_menuOpen >= 0) {
          auto step = [&](int d) { int i = s_menuKb; for (int q = 0; q < s_miN; q++) { i = (i < 0 ? (d > 0 ? 0 : s_miN - 1) : (i + d + s_miN) % s_miN); if (s_miAct[i] >= 0) { s_menuKb = i; return; } } };
          if (tEsc || tF2) { s_menuOpen = -1; s_escT.store(Now()); }
          else if (tLe || tRi) { int q = 0; for (int j = 0; j < 7; j++) if (kMenuOrd[j] == s_menuOpen) q = j; s_menuOpen = kMenuOrd[(q + (tRi ? 1 : 6)) % 7]; s_menuA = 0.0f; s_menuKb = -1; }
          else if (tDn) step(1); else if (tUp) step(-1);
          else if (tEn && s_menuKb >= 0 && s_menuKb < s_miN && s_miAct[s_menuKb] >= 0) { const int a = s_miAct[s_menuKb]; s_menuOpen = -1; act(a); }
      } else if (tF2) { s_menuOpen = 0; s_menuA = 0.0f; s_menuKb = -1; KbLabel("Barra de men\xC3\xBAs"); }
      else if (tF1 && !(GetAsyncKeyState(VK_MENU) & 0x8000)) kact(730);     // (17/11 06:57) Alt + F1 = captura de NVIDIA App: no abrir la ayuda
      else if (tEsc) { kact(0); s_escT.store(Now()); }
      else if (ctrl && !alt && s_sel == 3 && MotorPjSel() >= 0 && (tC || tV)) kact(tC ? 3360 : 3361);      // (07:23) copiar / pegar lugar, giro, tamano, animacion y gesto
      else if (ctrl && !alt) { if (tZ) kundo(shift); else if (tY) kundo(true); else if (tCm) kact(722); else if (tN) kact(710); else if (tO) kact(711); else if (tS) kact(shift ? 713 : 712); else if (tPl) kact(760); else if (tMi) kact(761); else if (t1) kact(235); else if (t3) kact(233); else if (t7) kact(234); }
      else if (!alt) {
          if (tSpc) kact(3502); else if (tCm) kact(3501); else if (tDot) kact(3503);      // (07:23) Espacio = reproducir / pausa, coma / punto = un cuadro atras / adelante
          else if (tG) kact(1); else if (tJ) kact(2); else if (tC) kact(3); else if (tV) kact(shift ? 741 : 740); else if (tZ) kact(742);
          else if (tT) kact(794); else if (tN) kact(795); else if (tX) kact(968);
          else if (tap('K') && MotorPjSel() >= 0) { if (MotorEdKey(MotorPjSel(), 0, 0, 0.0f) >= 0.0f) { KbLabel("Clave aqu\xC3\xAD"); Note("Clave guardada"); } }
          else if (tH && s_sel == 3 && MotorPjSel() >= 0) { if (shift) { PjShowAll(); KbLabel("Mostrar todos"); } else { PjHideSel(); KbLabel("Ocultar / mostrar"); } }
          else if (tDel && s_sel == 3 && MotorPjSel() >= 0) { MotorPjRemove(MotorPjSel()); Note("Jugador sacado (Supr)"); }
          else if (tL) { bool any = false; for (int i = 0; i < s_nLights; i++) if (s_lights[i].on) any = true; kact(shift ? 702 : any ? 701 : 700); }
          else if (t1) kact(232); else if (t3) kact(230); else if (t7) kact(231); else if (t0) kact(202); else if (t5) kact(236); }
      // (17/11 b, Agustin 03:46: "asi como con teclas del teclado") el jugador con el teclado, mientras se mantiene la tecla: flechas =
      // mover sobre el piso (segun se ve en pantalla), R / F = subir / bajar, Q / E = girar, + / - = tamano; Mayus = cuatro veces mas rapido.
      // Sin el boton derecho (volando, las letras son de la camara), sin menu abierto y sin estar escribiendo un numero.
      {   MotorJugState J; MotorJugGet(&J);
          if (kb && !ctrl && !alt && J.hooked && J.seen && J.movable && !(s_editId >= 0 && !s_editDone) && s_modal == 0 && s_menuOpen < 0) {
              auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0 ? 1.0f : 0.0f; };
              const float mvR = held(VK_RIGHT) - held(VK_LEFT), mvF = held(VK_UP) - held(VK_DOWN), mvY = held('R') - held('F'), rot = held('E') - held('Q');
              const float scl = std::max(held(VK_OEM_PLUS), held(VK_ADD)) - std::max(held(VK_OEM_MINUS), held(VK_SUBTRACT));
              if (mvR != 0.0f || mvF != 0.0f || mvY != 0.0f || rot != 0.0f || scl != 0.0f) {
                  const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 0.1f) * (shift ? 4.0f : 1.0f);
                  float fx = 0.0f, fz = 1.0f, rx = 1.0f, rz = 0.0f;      // adelante y derecha de la pantalla, sobre el piso
                  { float o[3], d0[3], d1[3], d2[3]; if (MotorGridRay(0.5f, 0.5f, o, d0) && MotorGridRay(0.0f, 0.5f, o, d1) && MotorGridRay(1.0f, 0.5f, o, d2)) {
                        const float lr = sqrtf((d2[0] - d1[0]) * (d2[0] - d1[0]) + (d2[2] - d1[2]) * (d2[2] - d1[2]));
                        if (lr > 1e-5f) { rx = (d2[0] - d1[0]) / lr; rz = (d2[2] - d1[2]) / lr; const float lf = sqrtf(d0[0] * d0[0] + d0[2] * d0[2]); if (lf > 0.05f) { fx = d0[0] / lf; fz = d0[2] / lf; } else { fx = -rz; fz = rx; if (fx * d0[0] + fz * d0[2] < 0.0f) { fx = -fx; fz = -fz; } } } } }
                  float np[3] = { J.pos[0] + (rx * mvR + fx * mvF) * dt, J.pos[1] + mvY * 0.5f * dt, J.pos[2] + (rz * mvR + fz * mvF) * dt };
                  if (!s_kbUndo) { UndoBegin(); s_kbUndo = true; }      // (17/11 tarde) mantener la tecla = un paso de deshacer
                  PjGroupSet(np, J.yawDeg + rot * 90.0f * dt, std::clamp(J.scale * expf(scl * 0.6f * dt), 0.1f, 5.0f));
                  KbLabel(rot != 0.0f ? "Girar al jugador" : scl != 0.0f ? "Tama\xC3\xB1o del jugador" : "Mover al jugador");
              } else if (s_kbUndo) { s_kbUndo = false; UndoEnd(); } } else if (s_kbUndo) { s_kbUndo = false; UndoEnd(); } }
    }
    // ---------------- clic (al soltar, sobre lo mismo que se apreto) ----------------
    const bool lNow = lDown; const bool lEdge = lEdge0, lUp = !lNow && s_tlWas; s_tlWas = lNow;
    const bool inMenu = (hot >= 600 && hot < 607) || hot == 699 || (hot >= 1000 && hot < 1040);
    if (s_menuOpen >= 0 && hot >= 600 && hot < 607 && hot - 600 != s_menuOpen) { s_menuOpen = hot - 600; s_menuA = 0.0f; s_menuKb = -1; }      // con un menu abierto, pasar por otro titulo lo cambia
    const bool menuWas = s_menuOpen >= 0;
    if (lEdge) { s_tPress = hot; if (s_menuOpen >= 0 && !inMenu) { s_menuOpen = -1; s_tPress = -1; } }      // clic afuera: cierra el menu y no hace nada mas
    // (15/11 o) campos y sliders: apretar arranca el arrastre (un paso de deshacer hasta soltar); soltar sin mover un campo deja escribirlo
    if (lEdge && s_editId >= 0 && !s_editDone && hot != s_editId) { s_editDone = 1; s_editAge = 0; }      // clic afuera: aplica lo escrito
    if (lEdge && !menuWas && hot >= 2000 && hot < 3000 && hot == s_pressId && hot != s_editId) { s_dragId = hot; s_dragV0 = s_pressVal; s_dragX0 = mx; s_dragMoved = false; UndoBegin(); }
    if (s_dragId >= 0 && !lNow) {
        if (!s_dragMoved && s_dragId < 2500) { s_editId = s_dragId; snprintf(s_editBuf, sizeof s_editBuf, "%g", s_dragV0); s_editLen = (int)strlen(s_editBuf); s_editFresh = true; s_editAge = 0; s_editDone = 0; }
        s_dragId = -1; UndoEnd(); }
    // (15/11 o) borde de un panel: arrastrar cambia su ancho (la vista 3D se queda con 260 puntos como minimo); al soltar se guarda
    if (lEdge && !menuWas && (hot == 990 || hot == 991)) s_pDrag = hot - 989;
    if (s_pDrag) {
        if (lNow) { const int sd = s_pDrag - 1; const float w = sd == 0 ? std::clamp(mx / k, 160.0f, std::max(160.0f, (ds.x - rightV - 260.0f * k) / k)) : std::clamp((ds.x - mx) / k, 200.0f, std::max(200.0f, (ds.x - leftV - 260.0f * k) / k));
                    if (P[0].dock == sd) g_cfg.motorPanelLW = w; if (P[1].dock == sd) g_cfg.motorPanelRW = std::max(200.0f, w); }      // (15/11 q) el costado entero: los paneles pegados ahi
        else { char b[16]; snprintf(b, sizeof b, "%.0f", g_cfg.motorPanelLW); SaveOption("motor_panel_izq", b); snprintf(b, sizeof b, "%.0f", g_cfg.motorPanelRW); SaveOption("motor_panel_der", b); s_pDrag = 0; } }
    // (15/11 q) mover un panel por su titulo, cambiar el tamano de uno flotante, mover la division de dos apilados
    if (lEdge && !menuWas && (hot == 985 || hot == 986)) { const int i = hot - 985; s_mvId = i + 1; s_mvOn = false; s_mvPX = mx; s_mvPY = my;
        s_mvGX = mx - P[i].x; s_mvGY = my - P[i].ya; s_mvW = P[i].W; s_mvH = std::min(P[i].yb - P[i].ya, P[i].dock == 2 ? 1.0e9f : 620.0f * k);
        if (P[i].dock < 2) s_mvGX = std::min(s_mvGX, s_mvW - 40.0f * k); }
    if (s_mvId > 0) { const int i = s_mvId - 1;
        if (lNow) { if (!s_mvOn && fabsf(mx - s_mvPX) + fabsf(my - s_mvPY) > 5.0f * k) { s_mvOn = true; s_menuOpen = -1; } }
        else { if (s_mvOn && s_dropDock >= 0) { const int o = 1 - i;
                if (s_dropDock == 2) { float* F = g_cfg.motorFloat[i]; F[0] = s_dropR[0] / k; F[1] = s_dropR[1] / k; F[2] = (s_dropR[2] - s_dropR[0]) / k; F[3] = (s_dropR[3] - s_dropR[1]) / k; g_cfg.motorDock[i] = 2; }
                else { g_cfg.motorDock[i] = s_dropDock; if (g_cfg.motorDock[o] == s_dropDock) g_cfg.motorStackTop = s_dropTop ? i : o; }
                DockSave(); static const char* const dn[3] = { "Panel pegado a la izquierda", "Panel pegado a la derecha", "Panel flotante" }; Note("%s", TRS(dn[s_dropDock])); Log("motor (editor): panel %d -> lugar %d", i, s_dropDock); }
            s_mvId = 0; s_mvOn = false; s_tPress = -1; } }
    if (lEdge && !menuWas && (hot == 987 || hot == 988)) s_rsz = hot - 986;
    if (s_rsz > 0) { const int i = s_rsz - 1; float* F = g_cfg.motorFloat[i];
        if (lNow) { F[2] = std::max(160.0f, (mx - P[i].x) / k); F[3] = std::max(120.0f, (my - P[i].ya) / k); if (i == 0) g_cfg.motorPanelLW = std::clamp(F[2], 160.0f, 900.0f); else g_cfg.motorPanelRW = std::clamp(F[2], 200.0f, 900.0f); F[2] = i == 0 ? g_cfg.motorPanelLW : g_cfg.motorPanelRW; }
        else { DockSave(); char b[16]; snprintf(b, sizeof b, "%.0f", i == 0 ? g_cfg.motorPanelLW : g_cfg.motorPanelRW); SaveOption(i == 0 ? "motor_panel_izq" : "motor_panel_der", b); s_rsz = 0; s_tPress = -1; } }
    if (lEdge && !menuWas && hot == 989) s_spl = true;
    // (15/11 q) ventanita de la vista extra: mover por el titulo, girar la camara extra arrastrando la imagen (Shift = acercar), esquina = tamano
    if (lEdge && !menuWas && (hot == 960 || hot == 961 || hot == 967)) { s_xvDrag = hot == 960 ? 1 : hot == 961 ? 2 : 3; s_xvGX = mx - s_xvPx; s_xvGY = my - s_xvPy; s_xvLX = mx; s_xvLY = my; }
    if (s_xvDrag) { if (lNow) { const float ddx = mx - s_xvLX, ddy = my - s_xvLY; s_xvLX = mx; s_xvLY = my;
            if (s_xvDrag == 1) { s_xv[0] = (mx - s_xvGX) / k; s_xv[1] = (my - s_xvGY) / k; }
            else if (s_xvDrag == 2) { if (GetAsyncKeyState(VK_SHIFT) & 0x8000) FreeCamExtraZoom(powf(1.006f, ddy / k)); else FreeCamExtraOrbit(-ddx * 0.008f / k, ddy * 0.008f / k); }
            else s_xv[2] = std::clamp((mx - s_xvPx) / k, 220.0f, 1400.0f); }
        else { s_xvDrag = 0; s_tPress = -1; } }
    if (s_spl) { if (lNow) g_cfg.motorSplit = std::clamp((my - y0) / std::max(1.0f, y1 - y0), 0.15f, 0.85f); else { DockSave(); s_spl = false; s_tPress = -1; } }
    if (lUp && s_tPress >= 0 && s_tPress == hot) {
        if (hot >= 600 && hot < 607) { s_menuOpen = s_menuOpen == hot - 600 ? -1 : hot - 600; s_menuA = 0.0f; s_menuKb = -1; }
        else if (hot >= 1000 && hot < 1040) { const int a = hot - 1000 < s_miN ? s_miAct[hot - 1000] : -1; s_menuOpen = -1; if (a >= 0) act(a); }
        else if (hot != 699 && hot != 897 && hot != 960 && hot != 961 && hot != 967 && !(hot >= 985 && hot <= 991) && !(hot >= 2000 && hot < 3000)) act(hot);
        static int nl = 0; if (nl++ < 60) Log("motor (editor): clic %d", hot);
    }
    if (!lNow) s_tPress = -1;
    s_uiAlive.store(Now(), std::memory_order_relaxed);
    // (15/11 o / p) fin del cuadro: cierra el paso de deshacer de lo escrito o arrastrado; guarda lo de la grilla y las Preferencias al soltar
    if (s_ubEndPending && s_dragId < 0) { s_ubEndPending = false; UndoEnd(); }
    if (s_dragId < 0 && s_editId < 0) {
        if (s_gridDirty) { s_gridDirty = false; GridSave(); }
        if (s_pendScale > 0.0f) { ScaleSetF(s_pendScale); s_pendScale = -1.0f; }
        if (s_pendText > 0.0f) { g_cfg.motorText = s_pendText; char b[16]; snprintf(b, sizeof b, "%.2f", s_pendText); SaveOption("motor_texto", b); MenuFontsDirty(); s_pendText = -1.0f; }
        if (s_prefSave) { char b[24];
            if (s_prefSave & 4) { snprintf(b, sizeof b, "%d", UndoLim()); SaveOption("motor_deshacer_pasos", b); }
            if (s_prefSave & 8) { snprintf(b, sizeof b, "%.2f", g_cfg.motorCamSpeed); SaveOption("motor_camara_velocidad", b); }
            if (s_prefSave & 16) { snprintf(b, sizeof b, "%.2f", g_cfg.motorCamSens); SaveOption("motor_camara_sensibilidad", b); }
            if (s_prefSave & 32) { snprintf(b, sizeof b, "%.3f", g_cfg.motorNear); SaveOption("motor_cerca", b); }
            if (s_prefSave & 64) { snprintf(b, sizeof b, "%.0f", g_cfg.motorFar); SaveOption("motor_lejos", b); }
            if (s_prefSave & 128) { snprintf(b, sizeof b, "%d", g_cfg.motorSoundVol); SaveOption("motor_sonido_volumen", b); }
            s_prefSave = 0; } }
    if (s_selEd != s_sel) { s_selEd = s_sel; s_editId = -1; s_editDone = 0; s_colorOpen = false; }      // otra fila marcada: se deja de escribir
    // (15/11 p) autoguardado: cada N minutos, solo si hubo un paso nuevo de deshacer desde el ultimo (la escena abierta no se pisa)
    if (g_cfg.motorAutoSave && s_coverEnd >= 0.0 && s_exitT < 0.0) { const double now = Now();
        if (s_autoT < 0.0) { s_autoT = now; s_autoMark = s_actCount; }
        else if (now - s_autoT > 60.0 * (double)std::clamp(g_cfg.motorAutoSaveMin, 1, 60)) { s_autoT = now;
            if (s_actCount != s_autoMark && SceneWrite(kAutoName)) { s_autoMark = s_actCount; SYSTEMTIME t; GetLocalTime(&t); Note("Autoguardado a las %02d:%02d", (int)t.wHour, (int)t.wMinute); } } }
    s_curKind.store((s_pDrag != 0 || hot == 990 || hot == 991 || s_rsz != 0 || hot == 987 || hot == 988) ? 3 : (s_spl || hot == 989) ? 4 : 0, std::memory_order_relaxed);
    // (18:01) ayuda al pasar el mouse por los botones del Motor (0,7 s quieto encima); en las filas de clips: archivo y descripcion
    { const char* tp = nullptr; static char tb[256];
      switch (hot) {
        case 3380: tp = "Las animaciones del juego ordenadas por grupo (festejos, entradas, banco...)."; break;
        case 3381: tp = "Tus favoritas: clic derecho en un clip de cualquier lista para sumarlo o sacarlo."; break;
        case 3382: tp = "Las \xC3\xBAltimas 10 animaciones que pusiste."; break;
        case 3383: tp = "Al terminar vuelve a empezar."; break;
        case 3384: tp = "Se reproduce una vez y queda quieta en el \xC3\xBAltimo cuadro."; break;
        case 3385: tp = "Va hasta el final y vuelve para atr\xC3\xA1s, sin salto."; break;
        case 3386: tp = "Los jugadores con esta misma animaci\xC3\xB3n arrancan en momentos distintos, para que no se muevan todos igual."; break;
        case 3320: tp = "Reproducir (Espacio)."; break;
        case 3321: tp = "Pausa (Espacio). Coma / punto: un cuadro para atr\xC3\xA1s / adelante."; break;
        case 3322: tp = "Vuelve al primer cuadro de la animaci\xC3\xB3n."; break;
        case 3323: tp = "Saca la animaci\xC3\xB3n: queda la postura de siempre del juego."; break;
        case 3324: tp = "Pone esta animaci\xC3\xB3n a todos los jugadores del Motor."; break;
        case 3310: case 3311: tp = "Cambiar de grupo de animaciones."; break;
        case 3312: case 3313: tp = "Animaci\xC3\xB3n anterior / siguiente del grupo."; break;
        case 3350: tp = "Escrib\xC3\xAD parte del nombre del archivo (por ejemplo: goal, idle, bench)."; break;
        case 3351: tp = "Borrar la b\xC3\xBAsqueda."; break;
        case 3362: tp = "Ocultar o mostrar (H). May\xC3\xBAs + H: mostrar todos."; break;
        case 3600: tp = "Las luces de siempre del Editar."; break;
        case 3601: tp = "Default.xml: las luces de fabrica del juego."; break;
        case 3602: tp = "EditFace.xml: las del Editar en la cara."; break;
        case 3603: tp = "EditSpike.xml: las del Editar en los botines."; break;
        case 3604: tp = "MatchMenu.xml: las del men\xC3\xBA del partido."; break;
        case 3307: tp = "La c\xC3\xA1mara acompa\xC3\xB1" "a al jugador mientras camina y mira su cara."; break;
        case 3510: tp = "Diez veces m\xC3\xA1s lento."; break;
        case 3511: tp = "Cuatro veces m\xC3\xA1s lento."; break;
        case 3512: tp = "A la mitad de la velocidad."; break;
        case 3513: tp = "Velocidad normal."; break;
        default:
          if (hot >= 3400 && hot < 3408 && s_pjRowClip[hot - 3400] >= 0) { char d[160]; ClipDesc(s_pjRowClip[hot - 3400], d, sizeof d);
              snprintf(tb, sizeof tb, "%s\n%s", d, MotorClipFile(s_pjRowClip[hot - 3400])); tp = tb; } }
      if (hot != s_tipId) { s_tipId = hot; s_tipT = Now(); }
      else if (tp && !lDown && Now() - s_tipT > 0.7) {
          const char* ht = TRS(tp); const float hs = fsS - 0.5f * k; const ImVec2 hz = font->CalcTextSizeA(hs, 360.0f * k, 360.0f * k, ht);
          ImDrawList* fg = ImGui::GetForegroundDrawList(); const float hx = std::clamp(s_tipA.x, 4.0f, std::max(4.0f, ds.x - hz.x - 16.0f * k));
          float hy = s_tipA.y - hz.y - 10.0f * k; if (hy < 4.0f) hy = s_tipB.y + 6.0f * k;
          fg->AddRectFilled(ImVec2(hx, hy), ImVec2(hx + hz.x + 12.0f * k, hy + hz.y + 6.0f * k), IM_COL32(18, 18, 20, 240), 4.0f * k); fg->AddRect(ImVec2(hx, hy), ImVec2(hx + hz.x + 12.0f * k, hy + hz.y + 6.0f * k), IM_COL32(90, 90, 96, 255), 4.0f * k);
          fg->AddText(font, hs, ImVec2(hx + 6.0f * k, hy + 3.0f * k), IM_COL32(225, 225, 228, 255), ht, nullptr, 360.0f * k); } }
    s_hotPrev = hot; s_escOpen.store(s_menuOpen >= 0 || s_modal != 0 || s_editId >= 0, std::memory_order_relaxed);
}

// (px, py) en pixeles de la ventana del juego
// (17/11 b, Agustin 03:46: "deberia ser capaz de hacer estas acciones con iconos sobre el piso del jugador asi como con teclas del
// teclado") ICONOS DEL PISO: a los pies del jugador, con la misma camara de la grilla (motorgrid.cpp MotorGridProject / MotorGridRay):
// disco del centro = mover libre sobre el piso; flecha roja X y azul Z = mover por ese eje; flecha verde = altura; aro amarillo con
// perilla = girar; cuadrado blanco = tamano. Se arrastran con el boton izquierdo. Tamano en el mundo, segun el jugador (17/11 tarde).
static std::atomic<float> s_fh[6][3]; static std::atomic<int> s_fhN{0}, s_fDrag{-1}; static float s_wY = 0.0f, s_cen[2] = {};
static float PtSegD2(float px, float py, const ImVec2& a, const ImVec2& b);
static void JugFloorGizmo() {
    s_fhN.store(0, std::memory_order_relaxed);
    MotorJugState J; MotorJugGet(&J);
    if (!J.hooked || !J.seen || !J.movable || !MotorVistaLive() || g_menuOpen.load(std::memory_order_relaxed)) { s_fDrag.store(-1); return; }
    const ImVec2 ds = ImGui::GetIO().DisplaySize; const float k = std::max(1.0f, ds.y / 1080.0f);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float P[3] = { J.wpos[0], J.wpos[1], J.wpos[2] };
    auto proj = [&](const float* w, ImVec2& o) { float u, v; if (!MotorGridProject(w, &u, &v)) return false; o = ImVec2(u * ds.x, v * ds.y); return fabsf(u) < 4.0f && fabsf(v) < 4.0f; };
    ImVec2 c0, c1; const float Pu[3] = { P[0], P[1] + 1.0f, P[2] };
    if (!proj(P, c0) || !proj(Pu, c1)) { s_fDrag.store(-1); return; }
    const float ppm = std::max(1.0f, sqrtf((c1.x - c0.x) * (c1.x - c0.x) + (c1.y - c0.y) * (c1.y - c0.y)));      // pixeles por metro a los pies
    // (17/11 17:24, Agustin con video: "los iconos de mover no se mueven y escalan correctamente con el jugador") antes median 120 px fijos
    // en pantalla: cerca de la camara quedaban chicos y lejos enormes al lado del jugador. Ahora miden en el mundo segun el tamano del
    // jugador (0.55 m x escala), con un minimo de 34 px para poder tomarlos de lejos.
    const float sz = std::clamp(std::max(0.55f * std::clamp(J.scale, 0.1f, 5.0f), 34.0f * k / ppm), 0.05f, 20.0f), L = sz;
    const float a = J.yawDeg * 0.017453292f;
    // (17/11 c, Agustin 04:28: "un estilo intermedio entre la a y la b") plato de vidrio con marcas (B) + flechas con punta y rombo (A):
    // 0 rombo = mover libre, 1 / 2 / 3 flechas X / Z / altura, 4 perilla del giro en el borde del plato, 5 cubito de tamano en el borde
    const float Rd = sz * 1.35f, d45 = 0.70710678f;
    const float W[6][3] = { { P[0] + L * 0.38f, P[1], P[2] + L * 0.38f }, { P[0] + L, P[1], P[2] }, { P[0], P[1], P[2] + L }, { P[0], P[1] + L * 0.95f, P[2] },
                            { P[0] + Rd * sinf(a), P[1], P[2] + Rd * cosf(a) }, { P[0] - Rd * d45, P[1], P[2] + Rd * d45 } };
    ImVec2 S[6]; bool ok[6]; for (int i = 0; i < 6; i++) ok[i] = proj(W[i], S[i]);
    // mouse
    float mx = -1.0e6f, my = -1.0e6f, sx = 1.0f, sy = 1.0f; bool front = false, overUi = false;
    { HWND h = OverlayHwnd(); POINT p{}; RECT rc{};
      if (h && GetForegroundWindow() == h && GetCursorPos(&p) && ScreenToClient(h, &p) && GetClientRect(h, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
          sx = (float)(rc.right - rc.left) / ds.x; sy = (float)(rc.bottom - rc.top) / ds.y; mx = (float)p.x / sx; my = (float)p.y / sy; front = true;
          if (MotorUiOn()) { const int n = s_hrN.load(); for (int i = 0; i < n && i < 16; i++) if ((float)p.x >= s_hr[i][0].load() && (float)p.x <= s_hr[i][2].load() && (float)p.y >= s_hr[i][1].load() && (float)p.y <= s_hr[i][3].load()) overUi = true; }
      } }
    const float hr = 13.0f * k;
    for (int i = 0; i < 6; i++) { s_fh[i][0].store(ok[i] ? S[i].x * sx : -1.0e6f); s_fh[i][1].store(S[i].y * sy); s_fh[i][2].store(hr * 1.25f * sx); }
    s_fhN.store(6, std::memory_order_relaxed);
    static bool s_was = false; static float s_m0[2], s_p0[3], s_yaw0, s_sc0, s_hit0[3], s_ang0; static ImVec2 s_upDir;
    const bool lNow = front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, lEdge = lNow && !s_was; s_was = lNow;
    int drag = s_fDrag.load(), hot = -1;
    if (drag < 0 && front && !overUi && s_dragId < 0 && !s_dragging.load()) { static const int kOrd[6] = { 5, 4, 3, 1, 2, 0 }; for (int q = 0; q < 6 && hot < 0; q++) { const int i = kOrd[q]; if (!ok[i]) continue; const float ex = mx - S[i].x, ey = my - S[i].y; if (ex * ex + ey * ey <= hr * hr * 1.5f) hot = i; } }
    auto hit = [&](float* out) { float o[3], d[3]; if (!MotorGridRay(mx / ds.x, my / ds.y, o, d) || fabsf(d[1]) < 1e-4f) return false; const float t = (s_p0[1] - o[1]) / d[1]; if (t <= 0.0f || t > 2000.0f) return false; for (int i = 0; i < 3; i++) out[i] = o[i] + d[i] * t; return true; };
    if (lEdge && hot >= 0) {
        memcpy(s_p0, J.pos, sizeof s_p0); s_yaw0 = J.yawDeg; s_sc0 = J.scale; s_m0[0] = mx; s_m0[1] = my; s_upDir = ImVec2(c1.x - c0.x, c1.y - c0.y);
        // el plano del arrastre pasa por los pies: se guarda el punto tocado en el mundo (con la altura del jugador en el mundo)
        const float keep = s_p0[1]; s_p0[1] = P[1]; float h3[3]; const bool hk = hit(h3); s_p0[1] = keep;
        if (hk || hot == 3 || hot == 5) { if (hk) { memcpy(s_hit0, h3, sizeof h3); s_ang0 = atan2f(h3[0] - P[0], h3[2] - P[2]); } drag = hot; s_fDrag.store(drag); s_wY = P[1]; s_cen[0] = P[0]; s_cen[1] = P[2]; UndoBegin(); }
    }
    if (drag >= 0 && !lNow) { drag = -1; s_fDrag.store(-1); UndoEnd(); }
    if (drag >= 0) {
        float np[3] = { s_p0[0], s_p0[1], s_p0[2] }, ny = s_yaw0, ns = s_sc0;
        const float keep = s_p0[1]; s_p0[1] = s_wY; float h3[3]; const bool hk = hit(h3); s_p0[1] = keep;
        if (drag == 0 && hk) { np[0] += h3[0] - s_hit0[0]; np[2] += h3[2] - s_hit0[2]; }
        else if (drag == 1 && hk) np[0] += h3[0] - s_hit0[0];
        else if (drag == 2 && hk) np[2] += h3[2] - s_hit0[2];
        else if (drag == 3) { const float l2 = s_upDir.x * s_upDir.x + s_upDir.y * s_upDir.y; if (l2 > 1.0f) np[1] += ((mx - s_m0[0]) * s_upDir.x + (my - s_m0[1]) * s_upDir.y) / l2; }
        else if (drag == 4 && hk) { float d = atan2f(h3[0] - s_cen[0], h3[2] - s_cen[1]) - s_ang0; while (d > 3.14159265f) d -= 6.2831853f; while (d < -3.14159265f) d += 6.2831853f; ny = s_yaw0 + d * 57.29578f;
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) ny = floorf(ny / 15.0f + 0.5f) * 15.0f; }      // Mayus: de a 15 grados
        else if (drag == 5) ns = std::clamp(s_sc0 * expf(((mx - s_m0[0]) - (my - s_m0[1])) / (220.0f * k)), 0.1f, 5.0f);
        if (s_pjSnap) {      // (06:36) ajustar a la grilla: lugar a la celda de la grilla, giro de a 15 grados
            const float cell = std::clamp(g_cfg.motorGridCell, 0.1f, 10.0f);
            if (drag == 0 || drag == 1) np[0] = floorf(np[0] / cell + 0.5f) * cell;
            if (drag == 0 || drag == 2) np[2] = floorf(np[2] / cell + 0.5f) * cell;
            if (drag == 4) ny = floorf(ny / 15.0f + 0.5f) * 15.0f; }
        PjGroupSet(np, ny, ns);
    }
    // --- dibujo ---
    const int act = drag >= 0 ? drag : hot;
    static const ImU32 kC[6] = { IM_COL32(236, 239, 244, 255), IM_COL32(240, 70, 82, 255), IM_COL32(60, 140, 245, 255), IM_COL32(120, 205, 60, 255), IM_COL32(250, 196, 40, 255), IM_COL32(240, 242, 246, 255) };
    // (17/11 d, Agustin 04:30: "asi esta bien los iconos con un poco menos de opacidad") todo al 70 %; el icono bajo el mouse o arrastrado, entero
    auto fade = [&](ImU32 c, int i) { if (act == i) return c; const ImU32 al = (c >> 24) & 0xffu; return (c & 0x00ffffffu) | ((ImU32)(al * 70 / 100) << 24); };
    const ImU32 sh = IM_COL32(0, 0, 0, 105), dark = IM_COL32(20, 24, 30, 180);
    // (17/11 18:15, Agustin: "el jugador debe verse arriba de la rueda de rota") los iconos se dibujan encima de la imagen, sin profundidad:
    // lo que queda en el piso DETRAS del jugador (mas lejos de la camara que sus pies) y cae sobre su figura en pantalla no se dibuja.
    // Figura = dos capsulas en pantalla segun el tamano: piernas (0 a 0.9 m, radio 0.17 m) y torso / cabeza (0.9 a 1.8 m, radio 0.26 m).
    float camO[3] = {}, camD[3] = { 0, 0, 1 }; const bool hasCam = MotorGridRay(c0.x / ds.x, c0.y / ds.y, camO, camD);
    auto depth = [&](const float* w) { return (w[0] - camO[0]) * camD[0] + (w[1] - camO[1]) * camD[1] + (w[2] - camO[2]) * camD[2]; };
    const float dP = depth(P), scJ = std::clamp(J.scale, 0.1f, 5.0f);
    ImVec2 bA, bB, bC; const float wA[3] = { P[0], P[1] + 0.05f * scJ, P[2] }, wB[3] = { P[0], P[1] + 0.9f * scJ, P[2] }, wC[3] = { P[0], P[1] + 1.8f * scJ, P[2] };
    const bool body = hasCam && proj(wA, bA) && proj(wB, bB) && proj(wC, bC);
    const float rL = 0.17f * scJ * ppm, rT = 0.26f * scJ * ppm;
    auto hidden = [&](const float* w, const ImVec2& q) { if (!body || depth(w) <= dP) return false;
        return PtSegD2(q.x, q.y, bA, bB) <= rL * rL || PtSegD2(q.x, q.y, bB, bC) <= rT * rT; };
    {   // plato de vidrio: disco oscuro translucido, borde amarillo, marcas cada 15 grados (mas largas cada 90) y arco del giro hecho
        enum { NS = 96, NB = 5 }; static ImVec2 g[NB + 1][NS]; static float gw[NB + 1][NS][3]; bool all = true;
        for (int b = 0; b <= NB; b++) for (int i = 0; i < NS; i++) { const float t = (float)i / NS * 6.2831853f, r = Rd * (float)b / NB;
            float* w = gw[b][i]; w[0] = P[0] + r * sinf(t); w[1] = P[1]; w[2] = P[2] + r * cosf(t); if (!proj(w, g[b][i])) all = false; }
        if (all) {
            for (int b = 0; b < NB; b++) for (int i = 0; i < NS; i++) { const int j = (i + 1) % NS;      // disco en trozos: los de atras del jugador no
                const float wm[3] = { (gw[b][i][0] + gw[b + 1][j][0]) * 0.5f, P[1], (gw[b][i][2] + gw[b + 1][j][2]) * 0.5f };
                const ImVec2 qm((g[b][i].x + g[b + 1][j].x) * 0.5f, (g[b][i].y + g[b + 1][j].y) * 0.5f);
                if (!hidden(wm, qm)) dl->AddQuadFilled(g[b][i], g[b][j], g[b + 1][j], g[b + 1][i], IM_COL32(20, 24, 32, 80)); }
            for (int i = 0; i < NS; i++) { const int j = (i + 1) % NS; const float wm[3] = { (gw[NB][i][0] + gw[NB][j][0]) * 0.5f, P[1], (gw[NB][i][2] + gw[NB][j][2]) * 0.5f };
                const ImVec2 qm((g[NB][i].x + g[NB][j].x) * 0.5f, (g[NB][i].y + g[NB][j].y) * 0.5f);
                if (!hidden(wm, qm)) dl->AddLine(g[NB][i], g[NB][j], IM_COL32(250, 196, 40, act == 4 ? 230 : 105), 2.2f * k); }
            for (int t = 0; t < 24; t++) { const float an = (float)t / 24.0f * 6.2831853f, l = (t % 6 ? 0.06f : 0.13f) * Rd;
                const float w0[3] = { P[0] + (Rd - l) * sinf(an), P[1], P[2] + (Rd - l) * cosf(an) }, w1[3] = { P[0] + Rd * sinf(an), P[1], P[2] + Rd * cosf(an) }; ImVec2 q0, q1;
                if (proj(w0, q0) && proj(w1, q1) && !hidden(w1, q1)) dl->AddLine(q0, q1, IM_COL32(255, 255, 255, t % 6 ? 80 : 170), 1.3f * k); }
            if (fabsf(a) > 0.002f) { const int m = std::clamp((int)(fabsf(a) / 6.2831853f * 96.0f) + 2, 2, 64); ImVec2 qp; bool hp = false; float wp[3] = {};
                for (int i = 0; i < m; i++) { const float t = a * (float)i / (float)(m - 1); const float w[3] = { P[0] + Rd * sinf(t), P[1], P[2] + Rd * cosf(t) }; ImVec2 q; const bool okp = proj(w, q);
                    if (okp && hp) { const float wm[3] = { (w[0] + wp[0]) * 0.5f, P[1], (w[2] + wp[2]) * 0.5f }; if (!hidden(wm, ImVec2((q.x + qp.x) * 0.5f, (q.y + qp.y) * 0.5f))) dl->AddLine(qp, q, fade(kC[4], 4), 4.5f * k); }
                    qp = q; hp = okp; memcpy(wp, w, sizeof wp); } }
        }
    }
    {   // rombo para mover libre sobre el piso
        const float r0 = L * 0.30f, r1 = L * 0.46f; const float wq[4][3] = { { P[0] + r0, P[1], P[2] + r0 }, { P[0] + r1, P[1], P[2] + r0 }, { P[0] + r1, P[1], P[2] + r1 }, { P[0] + r0, P[1], P[2] + r1 } };
        ImVec2 q[4]; bool okq = true; for (int i = 0; i < 4; i++) okq &= proj(wq[i], q[i]);
        const float wc[3] = { P[0] + (r0 + r1) * 0.5f, P[1], P[2] + (r0 + r1) * 0.5f };
        if (okq && !hidden(wc, ImVec2((q[0].x + q[2].x) * 0.5f, (q[0].y + q[2].y) * 0.5f))) { dl->AddConvexPolyFilled(q, 4, IM_COL32(255, 255, 255, act == 0 ? 120 : 38)); dl->AddPolyline(q, 4, IM_COL32(255, 255, 255, act == 0 ? 255 : 140), ImDrawFlags_Closed, (act == 0 ? 2.0f : 1.3f) * k); }
    }
    auto arrow = [&](int i) { if (!ok[i]) return; const ImU32 c = fade(kC[i], i); const float w = (act == i ? 4.2f : 3.2f) * k;
        float dx = S[i].x - c0.x, dy = S[i].y - c0.y; const float l = sqrtf(dx * dx + dy * dy); if (l < 1.0f) return; dx /= l; dy /= l; const float hs = 16.0f * k;
        const ImVec2 tip(S[i].x + dx * hs, S[i].y + dy * hs), b1(S[i].x - dy * hs * 0.45f, S[i].y + dx * hs * 0.45f), b2(S[i].x + dy * hs * 0.45f, S[i].y - dx * hs * 0.45f);
        if (i == 3) { dl->AddLine(c0, S[i], sh, w + 2.0f * k); dl->AddLine(c0, S[i], c, w); }      // la de altura se ve siempre (es la manija)
        else for (int sgi = 0; sgi < 10; sgi++) { const float f0 = sgi / 10.0f, f1 = (sgi + 1) / 10.0f, fm = (f0 + f1) * 0.5f;      // las del piso: en trozos, como el plato
            const float wm[3] = { P[0] + (W[i][0] - P[0]) * fm, P[1], P[2] + (W[i][2] - P[2]) * fm };
            const ImVec2 q0(c0.x + (S[i].x - c0.x) * f0, c0.y + (S[i].y - c0.y) * f0), q1(c0.x + (S[i].x - c0.x) * f1, c0.y + (S[i].y - c0.y) * f1);
            if (hidden(wm, ImVec2((q0.x + q1.x) * 0.5f, (q0.y + q1.y) * 0.5f))) continue; dl->AddLine(q0, q1, sh, w + 2.0f * k); dl->AddLine(q0, q1, c, w); }
        if (i != 3 && hidden(W[i], S[i]) && act != i) return;
        dl->AddTriangleFilled(ImVec2(tip.x + dx, tip.y + dy), ImVec2(b1.x - dy, b1.y + dx), ImVec2(b2.x + dy, b2.y - dx), sh); dl->AddTriangleFilled(tip, b1, b2, c);
        if (act == i) dl->AddTriangle(tip, b1, b2, IM_COL32(255, 255, 255, 230), 1.6f * k); };
    arrow(1); arrow(2); arrow(3);
    if (ok[5] && (act == 5 || !hidden(W[5], S[5]))) { const float q = 7.0f * k; dl->AddRectFilled(ImVec2(S[5].x - q - 1.5f * k, S[5].y - q - 1.5f * k), ImVec2(S[5].x + q + 1.5f * k, S[5].y + q + 1.5f * k), dark, 3.0f * k);
        dl->AddRectFilled(ImVec2(S[5].x - q, S[5].y - q), ImVec2(S[5].x + q, S[5].y + q), fade(kC[5], 5), 2.0f * k);
        if (act == 5) dl->AddRect(ImVec2(S[5].x - q - 4.0f * k, S[5].y - q - 4.0f * k), ImVec2(S[5].x + q + 4.0f * k, S[5].y + q + 4.0f * k), IM_COL32(255, 255, 255, 230), 3.0f * k, 0, 1.6f * k); }
    if (ok[4] && (act == 4 || !hidden(W[4], S[4]))) { dl->AddCircleFilled(S[4], 10.5f * k, sh, 24); dl->AddCircleFilled(S[4], 9.0f * k, fade(kC[4], 4), 24); dl->AddCircle(S[4], 9.0f * k, fade(IM_COL32(255, 255, 255, 255), 4), 24, 2.0f * k);
        if (act == 4) dl->AddCircle(S[4], 14.0f * k, IM_COL32(255, 255, 255, 220), 24, 1.6f * k); }
    dl->AddCircleFilled(c0, 8.0f * k, dark, 20); dl->AddCircleFilled(c0, 6.8f * k, fade(kC[0], 0), 20);
    if (act >= 0) {
        char t[96]; const char* nm = act == 4 ? "Girar" : act == 5 ? "Tama\xC3\xB1o" : act == 3 ? "Altura" : act == 1 ? "Mover X" : act == 2 ? "Mover Z" : "Mover";
        if (act == 4) snprintf(t, sizeof t, "%s  %.1f\xC2\xB0", TRS(nm), J.yawDeg); else if (act == 5) snprintf(t, sizeof t, "%s  %.0f %%", TRS(nm), J.scale * 100.0f);
        else snprintf(t, sizeof t, "%s  %.2f, %.2f, %.2f m", TRS(nm), J.pos[0], J.pos[1], J.pos[2]);
        ImFont* font = MotorFont(0); const float fs = 15.0f * k; const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, t); const ImVec2 tp(S[act].x + 18.0f * k, S[act].y - ts.y - 10.0f * k);
        dl->AddRectFilled(ImVec2(tp.x - 7.0f * k, tp.y - 4.0f * k), ImVec2(tp.x + ts.x + 7.0f * k, tp.y + ts.y + 4.0f * k), IM_COL32(18, 21, 27, 225), 5.0f * k);
        dl->AddText(font, fs, tp, IM_COL32(236, 239, 244, 255), t);
    }
}

// (17/11 06:36, Agustin: "las formas de mover") CLIC EN UN JUGADOR DE LA VISTA: cada jugador propio listo tiene una franja en la pantalla
// de los pies a la cabeza (misma camara de la grilla); un clic ahi lo elige (Inspector de Jugadores) y, sin soltar, lo arrastra sobre el
// piso (con "A la grilla", a la celda). Los iconos del piso del elegido ganan el clic (JugFloorGizmo va antes).
static std::atomic<float> s_ph[kMotorPjMax][4]; static std::atomic<int> s_phN{0}; static std::atomic<int> s_pickDrag{-1};
static float PtSegD2(float px, float py, const ImVec2& a, const ImVec2& b) { const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-4f ? ((px - a.x) * dx + (py - a.y) * dy) / l2 : 0.0f; t = std::clamp(t, 0.0f, 1.0f); const float ex = px - (a.x + dx * t), ey = py - (a.y + dy * t); return ex * ex + ey * ey; }
static void JugPick() {
    s_phN.store(0, std::memory_order_relaxed);
    if (!MotorVistaLive() || g_menuOpen.load(std::memory_order_relaxed)) { s_pickDrag.store(-1); return; }
    const ImVec2 ds = ImGui::GetIO().DisplaySize; const float k = std::max(1.0f, ds.y / 1080.0f);
    float mx = -1.0e6f, my = -1.0e6f, sx = 1.0f, sy = 1.0f; bool front = false, overUi = false;
    { HWND h = OverlayHwnd(); POINT p{}; RECT rc{};
      if (h && GetForegroundWindow() == h && GetCursorPos(&p) && ScreenToClient(h, &p) && GetClientRect(h, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
          sx = (float)(rc.right - rc.left) / ds.x; sy = (float)(rc.bottom - rc.top) / ds.y; mx = (float)p.x / sx; my = (float)p.y / sy; front = true;
          if (MotorUiOn()) { const int n = s_hrN.load(); for (int i = 0; i < n && i < 16; i++) if ((float)p.x >= s_hr[i][0].load() && (float)p.x <= s_hr[i][2].load() && (float)p.y >= s_hr[i][1].load() && (float)p.y <= s_hr[i][3].load()) overUi = true; }
      } }
    static MotorPjInfo PI; MotorPjInfoGet(&PI);
    auto proj = [&](const float* w, ImVec2& o) { float u, v; if (!MotorGridProject(w, &u, &v)) return false; o = ImVec2(u * ds.x, v * ds.y); return fabsf(u) < 4.0f && fabsf(v) < 4.0f; };
    int best = -1; float bestD = 1.0e30f; int n = 0;
    for (int i = 0; i < kMotorPjMax; i++) {
        if (PI.st[i] != 3 || PI.hidden[i]) continue; float p[3], yw, sc; if (!MotorPjGet(i, p, &yw, &sc)) continue;
        const float hd[3] = { p[0], p[1] + 1.78f * sc, p[2] }, sd[3] = { p[0] + 0.25f * sc, p[1], p[2] }; ImVec2 a, b, c;
        if (!proj(p, a) || !proj(hd, b) || !proj(sd, c)) continue;
        const float r = std::max(10.0f * k, sqrtf((c.x - a.x) * (c.x - a.x) + (c.y - a.y) * (c.y - a.y)));      // medio ancho del cuerpo en pantalla
        if (n < kMotorPjMax) { s_ph[n][0].store(a.x * sx); s_ph[n][1].store(a.y * sy); s_ph[n][2].store(b.x * sx); s_ph[n][3].store(b.y * sy); n++; }
        const float d2 = PtSegD2(mx, my, a, b); if (d2 <= r * r && d2 < bestD) { bestD = d2; best = i; }
        (void)r;
    }
    s_phN.store(n, std::memory_order_relaxed);
    static bool s_was = false; static float s_hit0[3], s_p0[3], s_y0; const bool lNow = front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, lEdge = lNow && !s_was; s_was = lNow;
    auto floorHit = [&](float yPlane, float* out) { float o[3], d[3]; if (!MotorGridRay(mx / ds.x, my / ds.y, o, d) || fabsf(d[1]) < 1e-4f) return false; const float t = (yPlane - o[1]) / d[1]; if (t <= 0.0f || t > 2000.0f) return false; for (int i = 0; i < 3; i++) out[i] = o[i] + d[i] * t; return true; };
    int drag = s_pickDrag.load();
    if (drag < 0 && lEdge && best >= 0 && !overUi && s_fDrag.load() < 0 && s_dragId < 0 && !s_dragging.load()) {
        float p[3], yw, sc; if (MotorPjGet(best, p, &yw, &sc) && floorHit(p[1], s_hit0)) {
            const bool add = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0; if (!add && ((s_pjMulti >> best) & 1u) && PjMultiCount() > 1) MotorPjSelect(best); else PjPick(best, add);
            s_sel = 3; s_selObj = 0; s_pjGroup = -1; memcpy(s_p0, p, sizeof s_p0); s_y0 = p[1]; drag = best; s_pickDrag.store(best); UndoBegin(); }
    }
    if (drag >= 0 && !lNow) { drag = -1; s_pickDrag.store(-1); UndoEnd(); }
    if (drag >= 0) { float h3[3], p[3], yw, sc;
        if (floorHit(s_y0, h3) && MotorPjGet(drag, p, &yw, &sc)) {
            float np[3] = { s_p0[0] + h3[0] - s_hit0[0], s_p0[1], s_p0[2] + h3[2] - s_hit0[2] };
            if (s_pjSnap) { const float cell = std::clamp(g_cfg.motorGridCell, 0.1f, 10.0f); np[0] = floorf(np[0] / cell + 0.5f) * cell; np[2] = floorf(np[2] / cell + 0.5f) * cell; }
            if (MotorPjSel() == drag) PjGroupSet(np, yw, sc); else MotorPjSet(drag, np, yw, sc); } }
    // marca del jugador bajo el mouse (o arrastrado): su numero sobre la cabeza
    const int show = drag >= 0 ? drag : (s_fDrag.load() < 0 && !overUi ? best : -1);
    if (show >= 0) { float p[3], yw, sc; ImVec2 b; if (MotorPjGet(show, p, &yw, &sc)) { const float hd[3] = { p[0], p[1] + 1.95f * sc, p[2] };
        if (proj(hd, b)) { ImDrawList* dl = ImGui::GetBackgroundDrawList(); char t[64]; snprintf(t, sizeof t, "%d.  %.30s", show + 1, PI.name[show]);
            ImFont* font = MotorFont(0); const float fs = 14.0f * k; const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, t); const ImVec2 tp(b.x - ts.x * 0.5f, b.y - ts.y - 6.0f * k);
            dl->AddRectFilled(ImVec2(tp.x - 7.0f * k, tp.y - 3.0f * k), ImVec2(tp.x + ts.x + 7.0f * k, tp.y + ts.y + 3.0f * k), drag >= 0 ? IM_COL32(240, 138, 36, 230) : IM_COL32(18, 21, 27, 215), 5.0f * k);
            dl->AddText(font, fs, tp, drag >= 0 ? IM_COL32(20, 20, 20, 255) : IM_COL32(236, 239, 244, 255), t); } } }
}

bool MotorGizmoOver(float px, float py) {
    if (Now() - s_drawT.load(std::memory_order_relaxed) > 0.3) return false;
    if (s_dragging.load(std::memory_order_relaxed) || s_fDrag.load(std::memory_order_relaxed) >= 0 || s_pickDrag.load(std::memory_order_relaxed) >= 0) return true;
    { const int n = s_phN.load(std::memory_order_relaxed); const float r = 14.0f;      // (06:36) un jugador propio bajo el mouse: el clic es para elegirlo
      for (int i = 0; i < n && i < kMotorPjMax; i++) if (PtSegD2(px, py, ImVec2(s_ph[i][0].load(), s_ph[i][1].load()), ImVec2(s_ph[i][2].load(), s_ph[i][3].load())) <= r * r) return true; }
    { const int n = s_fhN.load(std::memory_order_relaxed); for (int i = 0; i < n && i < 6; i++) { const float ex = px - s_fh[i][0].load(), ey = py - s_fh[i][1].load(), er = s_fh[i][2].load(); if (ex * ex + ey * ey <= er * er) return true; } }      // (17/11 b) iconos del piso del jugador
    if (MotorUiOn()) { const int n = s_hrN.load(); for (int i = 0; i < n && i < 16; i++) if (px >= s_hr[i][0].load() && px <= s_hr[i][2].load() && py >= s_hr[i][1].load() && py <= s_hr[i][3].load()) return true; }   // menu propio: volver, capsula, tarjeta
    const float dx = px - s_cx.load(), dy = py - s_cy.load(), r = s_rad.load() * 1.22f;
    if (dx * dx + dy * dy <= r * r) return true;
    return px >= s_bx0.load() && px <= s_bx1.load() && py >= s_by0.load() && py <= s_by1.load();
}

// overlay.cpp (hilo de la ventana): la rueda sobre un panel del editor no mueve la camara; sobre el arbol corre la lista
bool MotorUiWheel(int delta) {
    if (!MotorUiOn() || Now() - s_drawT.load(std::memory_order_relaxed) > 0.3) return false;
    HWND h = OverlayHwnd(); POINT p{}; if (!h || !GetCursorPos(&p) || !ScreenToClient(h, &p)) return false;
    const int n = s_hrN.load();
    for (int i = 0; i < n && i < 16; i++) if ((float)p.x >= s_hr[i][0].load() && (float)p.x <= s_hr[i][2].load() && (float)p.y >= s_hr[i][1].load() && (float)p.y <= s_hr[i][3].load()) { if (i == s_treeZone.load()) s_uiWheel.fetch_add(delta); else if (i == s_tlZone.load()) s_tlWheel.fetch_add(delta); else if (i == s_inZone.load()) s_inWheel.fetch_add(delta); return true; }
    return false;
}

static void SetCam(bool on) {
    { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.live[344][0] = on ? 2.0f : 0.0f; }
    Log("motor (gizmo): camara %s", on ? "libre" : "del juego");
}

void MotorGizmoDraw() {
    // (17/11) con el jugador en la escena principal la grilla del piso se dibuja encima de la vista principal (motorgrid.cpp), debajo de los paneles
    // (17/11 07:08) la grilla va en la composicion con profundidad (motorgrid.cpp MotorGridCompSRV); encima solo si ninguna composicion la tomo
    if (MotorPageActive() && MotorVistaLive() && g_dev.imm) { ID3D11ShaderResourceView* g = MotorGridOverlay(g_dev.imm); MotorGridOverlayStore(g);
        if (g && !MotorGridCompBound()) ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(uintptr_t)g, ImVec2(0, 0), ImGui::GetIO().DisplaySize); }
    else MotorGridOverlayStore(nullptr);
    // (07:16) camara que sigue al elegido: cuando el jugador se mueve (arrastre, campos, teclas o la animacion) la camara se corre lo mismo
    {   static int s_fq = -1; static float s_fp[3];
        const int q = MotorPageActive() && s_camFollow ? MotorPjSel() : -1; float p[3], yw, sc;
        if (q >= 0 && MotorPjGet(q, p, &yw, &sc)) {
            float wr[3]; if (MotorPjWorld(q, wr)) { p[0] = wr[0]; p[2] = wr[2]; }      // (20:47) la raiz animada (camina con el clip), no solo el lugar puesto
            // (17/11 23:18, Agustin: "la camara de seguir va con muchos saltos") la raiz se actualiza con el reloj del juego, no con el de la
            // pantalla, y la camara copiaba cada paso de golpe: ahora sigue un punto suavizado (se acerca al jugador con un retraso de ~0.15 s)
            { static float s_sm[3]; static int s_smq = -1; const float dtu = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f), a = 1.0f - expf(-dtu * 7.0f);
              if (s_smq != q) { memcpy(s_sm, p, sizeof s_sm); s_smq = q; } else for (int i = 0; i < 3; i++) s_sm[i] += (p[i] - s_sm[i]) * a;
              memcpy(p, s_sm, sizeof s_sm); }
            if (q == s_fq) { const float d[3] = { p[0] - s_fp[0], p[1] - s_fp[1], p[2] - s_fp[2] };
                float cp[3], cy, cpt;
                if (fabsf(d[0]) + fabsf(d[2]) < 50.0f && !FreeCamOrtho() && FreeCamGetPose(cp, &cy, &cpt)) {
                    for (int i = 0; i < 3; i++) cp[i] += d[i];
                    if (s_camFollow == 2) {     // mira la cara
                        // (17/11 23:17, Agustin: "la camara de la cara no funciona correctamente, deberia seguir el rostro con un zoom, apuntar
                        // siempre hacia la cara") antes miraba a 1.62 m sobre los pies desde donde estaba la camara: con el jugador agachado o
                        // inclinado se perdia la cara. Ahora: el hueso real de la cabeza (MotorEdInfoGet), la camara se va adelante de la cara
                        // a 1.05 m (cabeza y hombros) y la mira; lugar y giro con suavidad.
                        static MotorEdInfo E; float H[3] = { p[0], p[1] + 1.62f * sc, p[2] }, fw[3] = { sinf(yw * 0.017453292f), 0.0f, cosf(yw * 0.017453292f) }; float s2 = sc;
                        if (MotorEdInfoGet(q, &E)) { for (int b = 0; b < E.n; b++) if (E.lab[b] == 6) { memcpy(H, E.pos[b], 12);
                            const float l = sqrtf(E.ax[2][0] * E.ax[2][0] + E.ax[2][2] * E.ax[2][2]); if (l > 0.1f) { fw[0] = E.ax[2][0] / l; fw[2] = E.ax[2][2] / l; } s2 = E.sc; } }
                        const float F[3] = { H[0] + fw[0] * 0.05f * s2, H[1] + 0.07f * s2, H[2] + fw[2] * 0.05f * s2 };
                        const float C[3] = { F[0] + fw[0] * 1.05f * s2, F[1] + 0.04f * s2, F[2] + fw[2] * 1.05f * s2 };
                        const float af = 1.0f - expf(-std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f) * 5.0f); float c0[3]; for (int i = 0; i < 3; i++) { c0[i] = cp[i] - d[i]; cp[i] = c0[i] + (C[i] - c0[i]) * af; }
                        const float hx = F[0] - cp[0], hy = F[1] - cp[1], hz = F[2] - cp[2], hd = sqrtf(hx * hx + hz * hz);
                        if (hd > 0.05f) { float ty = atan2f(hx, hz), tp = atan2f(hy, hd), dy = ty - cy;
                            while (dy > 3.14159265f) dy -= 6.2831853f; while (dy < -3.14159265f) dy += 6.2831853f;
                            cy += dy * std::min(1.0f, af * 2.0f); cpt += (tp - cpt) * std::min(1.0f, af * 2.0f); } }
                    if (fabsf(d[0]) + fabsf(d[1]) + fabsf(d[2]) > 1e-4f || s_camFollow == 2) FreeCamSetPose(cp, cy, cpt); } }
            s_fq = q; memcpy(s_fp, p, sizeof s_fp);
        } else s_fq = -1; }
    if (MotorPageActive()) { JugFloorGizmo(); JugPick(); } else { s_fhN.store(0); s_fDrag.store(-1); s_phN.store(0); s_pickDrag.store(-1); }
    if (MotorUiOn()) ToolbarDraw();
    if (g_cfg.motorGizmo == 0) { s_drawT.store(Now()); return; }
    ImDrawList* dl = MotorDL(); const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float k = std::max(1.0f, ds.y / 1080.0f), R = 54.0f * k, mg = 30.0f * k;
    const bool ui_ = MotorUiOn();      // (15/11 c) con el editor a la vista el gizmo va dentro de la vista 3D: a la izquierda del inspector y debajo de la barra
    const float ku = UiK(ds);
    const ImVec2 C(ds.x - mg - R - (ui_ ? std::max(0.0f, s_rightPx) : 0.0f), mg + R + (ui_ ? kBarH * ku * std::max(1.0f, 0.55f + 0.45f * std::clamp(g_cfg.motorText, 0.8f, 1.5f)) : 0.0f));      // (15/11 p) a la izquierda del inspector, mida lo que mida
    // mouse (pixeles de la ventana, llevados al tamano del dibujo)
    float mx = -1.0e6f, my = -1.0e6f; bool front = false;
    { HWND h = OverlayHwnd(); POINT p{}; RECT rc{};
      if (h && !g_menuOpen.load(std::memory_order_relaxed) && GetForegroundWindow() == h && GetCursorPos(&p) && ScreenToClient(h, &p) && GetClientRect(h, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
          mx = (float)p.x * ds.x / (float)(rc.right - rc.left); my = (float)p.y * ds.y / (float)(rc.bottom - rc.top); front = true;
          const float sx = (float)(rc.right - rc.left) / ds.x, sy = (float)(rc.bottom - rc.top) / ds.y;       // lo que ve menumouse: pixeles de la ventana
          s_cx.store(C.x * sx); s_cy.store(C.y * sy); s_rad.store(R * sx);
      } }
    const bool lNow = front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0; const bool lEdge = lNow && !s_lWas; const bool lUp = !lNow && s_lWas; s_lWas = lNow;
    const bool camOn = FreeCamActive();
    { float pos[3], y = 0, p = 0; if (FreeCamGetPose(pos, &y, &p) && camOn) { s_yaw = y; s_pitch = p; } }
    // ejes de la camara (los mismos de freecam.cpp): f frente, r derecha, u arriba
    const float cp = cosf(s_pitch), sp = sinf(s_pitch);
    const float f[3] = { cp * sinf(s_yaw), sp, cp * cosf(s_yaw) };
    float r[3] = { -f[2], 0.0f, f[0] }; { const float l = sqrtf(r[0] * r[0] + r[2] * r[2]); if (l > 1e-5f) { r[0] /= l; r[2] /= l; } else { r[0] = 1; r[2] = 0; } }
    const float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
    struct Punta { ImVec2 p; float depth; int idx; };
    Punta pt[6];
    for (int i = 0; i < 6; i++) {
        const int a = i / 2; const float s = (i & 1) ? -1.0f : 1.0f;
        const float L = R * 0.78f;
        pt[i].p = ImVec2(C.x + s * r[a] * L, C.y - s * u[a] * L); pt[i].depth = s * f[a]; pt[i].idx = i;
    }
    std::sort(pt, pt + 6, [](const Punta& a, const Punta& b) { return a.depth > b.depth; });     // lo de atras primero
    const float dxc = mx - C.x, dyc = my - C.y; const bool inBall = dxc * dxc + dyc * dyc <= R * R * 1.22f * 1.22f;
    const float er = 11.0f * k;                                                                   // radio de cada punta
    int hot = -1;
    for (int i = 5; i >= 0 && hot < 0; i--) { const float ex = mx - pt[i].p.x, ey = my - pt[i].p.y; if (ex * ex + ey * ey <= er * er * 1.3f) hot = pt[i].idx; }
    // botones debajo de la bola
    const float bs = 34.0f * k, gap = 8.0f * k; const float by = C.y + R + 16.0f * k, bx = C.x - bs * 1.5f - gap;
    const ImVec2 b0(bx, by), b1(bx + bs + gap, by), b2(bx + 2.0f * (bs + gap), by);   // (17/11 07:12, Agustin) b2 = perspectiva / ortografica
    { const float sx = s_rad.load() / R; s_bx0.store(b0.x * sx); s_by0.store(b0.y * sx); s_bx1.store((b2.x + bs) * sx); s_by1.store((b1.y + bs) * sx); }
    auto inBtn = [&](const ImVec2& b) { return mx >= b.x && mx <= b.x + bs && my >= b.y && my <= b.y + bs; };
    const int hotBtn = inBtn(b0) ? 0 : inBtn(b1) ? 1 : inBtn(b2) ? 2 : -1;
    // --- entrada ---
    if (lEdge && (inBall || hotBtn >= 0)) { s_press = true; s_moved = false; s_px = s_lx = mx; s_py = s_ly = my; s_dragging.store(inBall && hotBtn < 0); }
    if (s_press && lNow && s_dragging.load()) {
        if (!s_moved && (std::fabs(mx - s_px) > 4.0f * k || std::fabs(my - s_py) > 4.0f * k)) { s_moved = true; if (!camOn) SetCam(true); }
        if (s_moved) FreeCamGizmoOrbit(-(mx - s_lx) * 0.009f / k, -(my - s_ly) * 0.009f / k);
        s_lx = mx; s_ly = my;
    }
    if (s_press && lUp) {
        if (!s_moved) {
            if (hotBtn == 0) SetCam(!camOn);
            else if (hotBtn == 1) { if (!camOn) SetCam(true); FreeCamGizmoHome(); Log("motor (gizmo): inicio"); }
            else if (hotBtn == 2) { const int on = !FreeCamOrtho(); if (on && !camOn) SetCam(true); FreeCamOrthoSet(on); Log("motor (gizmo): %s", on ? "ortografica" : "perspectiva"); }
            else if (hot >= 0) { if (!camOn) SetCam(true); FreeCamGizmoAxis(hot); }
        }
        s_press = false; s_dragging.store(false);
    }
    if (!lNow) { s_press = false; s_dragging.store(false); }
    s_drawT.store(Now());
    // --- dibujo ---
    const float al = camOn ? 1.0f : 0.55f; auto A = [al](int v) { return (int)((float)v * al); };
    if (inBall || s_dragging.load()) dl->AddCircleFilled(C, R * 1.2f, IM_COL32(255, 255, 255, 34), 48);
    static const ImU32 kCol[3] = { IM_COL32(234, 72, 84, 255), IM_COL32(126, 196, 46, 255), IM_COL32(64, 140, 240, 255) };
    static const char* const kNom[3] = { "X", "Y", "Z" };
    ImFont* font = MotorFont(0); const float fs = 15.0f * k;
    for (int i = 0; i < 6; i++) {
        const int a = pt[i].idx / 2; const bool pos = (pt[i].idx & 1) == 0; const bool h = hot == pt[i].idx;
        ImU32 c = kCol[a]; c = (c & 0x00ffffffu) | ((ImU32)A(pos ? 255 : 150) << 24);
        if (pos) {
            dl->AddLine(C, pt[i].p, c, 2.6f * k);
            dl->AddCircleFilled(pt[i].p, er, c, 24);
            if (h) dl->AddCircle(pt[i].p, er + 1.5f * k, IM_COL32(255, 255, 255, 255), 24, 1.6f * k);
            const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, kNom[a]);
            dl->AddText(font, fs, ImVec2(pt[i].p.x - ts.x * 0.5f, pt[i].p.y - ts.y * 0.5f), IM_COL32(12, 14, 18, A(255)), kNom[a]);
        } else {
            dl->AddCircleFilled(pt[i].p, er * 0.82f, (c & 0x00ffffffu) | ((ImU32)A(h ? 200 : 70) << 24), 24);
            dl->AddCircle(pt[i].p, er * 0.82f, c, 24, 1.5f * k);
        }
    }
    auto boton = [&](const ImVec2& b, bool hov, bool on) {
        dl->AddRectFilled(b, ImVec2(b.x + bs, b.y + bs), on ? IM_COL32(64, 110, 190, 230) : IM_COL32(28, 31, 38, hov ? 235 : 190), 8.0f * k);
        dl->AddRect(b, ImVec2(b.x + bs, b.y + bs), IM_COL32(255, 255, 255, hov ? 150 : 50), 8.0f * k, 0, 1.2f * k);
    };
    const ImU32 ink = IM_COL32(236, 239, 244, 255);
    boton(b0, hotBtn == 0, camOn);
    {   // camara de video: cuerpo, visor en triangulo y lente
        const ImVec2 o(b0.x + bs * 0.5f, b0.y + bs * 0.5f); const float w = bs * 0.20f, hh = bs * 0.15f;
        dl->AddRectFilled(ImVec2(o.x - w * 1.5f, o.y - hh), ImVec2(o.x + w * 0.5f, o.y + hh), ink, 2.0f * k);
        dl->AddTriangleFilled(ImVec2(o.x + w * 0.6f, o.y), ImVec2(o.x + w * 1.5f, o.y - hh), ImVec2(o.x + w * 1.5f, o.y + hh), ink);
    }
    boton(b1, hotBtn == 1, false);
    {   // casita: volver a la camara del juego
        const ImVec2 o(b1.x + bs * 0.5f, b1.y + bs * 0.5f); const float w = bs * 0.26f;
        dl->AddTriangleFilled(ImVec2(o.x, o.y - w), ImVec2(o.x - w, o.y), ImVec2(o.x + w, o.y), ink);
        dl->AddRectFilled(ImVec2(o.x - w * 0.68f, o.y), ImVec2(o.x + w * 0.68f, o.y + w * 0.85f), ink);
    }
    boton(b2, hotBtn == 2, FreeCamOrtho());
    {   // proyeccion: cuadrado = ortografica, trapecio (lejos mas chico) = perspectiva
        const ImVec2 o(b2.x + bs * 0.5f, b2.y + bs * 0.5f); const float w = bs * 0.24f, t = FreeCamOrtho() ? w : w * 0.55f;
        const ImVec2 q[4] = { ImVec2(o.x - t, o.y - w), ImVec2(o.x + t, o.y - w), ImVec2(o.x + w, o.y + w), ImVec2(o.x - w, o.y + w) };
        dl->AddPolyline(q, 4, ink, ImDrawFlags_Closed, 1.8f * k);
    }
    const char* txt = camOn ? (FreeCamOrtho() ? "Camara libre - ortografica" : "Camara libre - perspectiva") : "Camara del juego";
    const float ft = 14.0f * k; const ImVec2 ts = font->CalcTextSizeA(ft, FLT_MAX, 0.0f, txt);
    const ImVec2 tp(C.x - ts.x * 0.5f, by + bs + 6.0f * k);
    dl->AddText(font, ft, ImVec2(tp.x + 1.0f, tp.y + 1.0f), IM_COL32(0, 0, 0, 200), txt);
    dl->AddText(font, ft, tp, IM_COL32(236, 239, 244, 230), txt);
}

} // namespace fx
