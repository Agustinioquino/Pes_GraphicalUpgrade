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
#include <cctype>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <atomic>
#include <cmath>
#include <algorithm>

namespace fx {

static std::atomic<float> s_cx{0}, s_cy{0}, s_rad{0}, s_bx0{0}, s_by0{0}, s_bx1{0}, s_by1{0};
static std::atomic<double> s_drawT{-1.0e9};
static std::atomic<bool> s_dragging{false};
static bool s_lWas = false, s_press = false, s_moved = false; static float s_px = 0, s_py = 0, s_lx = 0, s_ly = 0;
static float s_yaw = 3.14159f, s_pitch = 0.0f;       // ultima orientacion conocida (con la camara del juego no se sabe: queda la ultima)

bool MotorGizmoPoll() { return (g_cfg.motorGizmo != 0 || MotorUiOn()) && MotorPageActive() && !g_menuOpen.load(std::memory_order_relaxed); }

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
    char b[220]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    bool nuevo = true;
    {   std::lock_guard<std::mutex> lk(s_errMx);
        if (s_errTotal > 0) { MErr& u = s_err[(s_errTotal - 1) & 7]; if (strcmp(u.msg, b) == 0 && Now() - u.t < 30.0) { u.t = Now(); nuevo = false; } }
        if (nuevo) { MErr& e = s_err[s_errTotal & 7]; e.t = Now(); memcpy(e.msg, b, sizeof b); s_errTotal++; } }
    if (nuevo) Log("motor (ERROR): %s", b);
}
static std::atomic<float> s_hr[6][4];                // zonas que tapan el clic (pixeles de la ventana): barra, escena, inspector, estado, ventana del centro, menu abierto
static std::atomic<int> s_hrN{0};
static float s_openA = 0.0f, s_selY = -1.0f, s_inspA = 1.0f; static double s_uiT = -1.0e9; static int s_selWas = 0;   // (15/11 d) animaciones
static MotorLight s_lights[64]; static int s_nLights = 0; static MotorSceneInfo s_info{}; static double s_sceneT = -1.0e9; static uintptr_t s_selObj = 0;   // (15/11 e) luces de la escena
static int s_sel = 0, s_hotPrev = -1;                // fila marcada del arbol (0 render, 1 camara, 2 grilla, 3 jugador)
static const float kBarH = 38.0f, kLeftW = 250.0f, kRightW = 320.0f, kStatH = 26.0f;
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
static int s_menuOpen = -1, s_menuKb = -1, s_modal = 0, s_miAct[28], s_miN = 0; static float s_menuA = 0.0f, s_mLastX = 0.0f, s_mLastY = 0.0f;
static std::atomic<bool> s_escOpen{false}; static std::atomic<double> s_escT{-1.0e9}; static bool s_kWas[256];
static std::wstring s_scDir; static bool s_scDirTried = false; static char s_scName[64] = ""; static char s_scList[14][64]; static int s_scListN = 0;
static char s_note[220] = ""; static double s_noteT = -1.0e9;
struct PendL { int kind; bool on; char name[40]; }; static PendL s_pend[64]; static int s_pendN = 0; static double s_pendUntil = -1.0e9;
struct OrigL { uintptr_t obj; bool on; }; static OrigL s_orig[64]; static int s_origN = 0;       // como estaba cada luz al entrar al Motor
bool MotorUiEscBusy() { return MotorUiOn() && (s_escOpen.load(std::memory_order_relaxed) || Now() - s_escT.load(std::memory_order_relaxed) < 0.4); }   // menumouse.cpp: Esc cierra el menu abierto, no el Motor
static void Note(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(s_note, sizeof s_note, fmt, ap); va_end(ap); s_noteT = Now(); Log("motor (editor): %s", s_note); }
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
static std::wstring ScenePath(const char* name) { std::wstring f = SceneDir(); for (const char* c = name; *c; c++) f += (wchar_t)(unsigned char)*c; return f; }
static void SceneLabel(const char* nm, char* out, size_t cap) {
    int Y, M, D, h, m, s;
    if (sscanf(nm, "escena_%4d%2d%2d_%2d%2d%2d", &Y, &M, &D, &h, &m, &s) == 6) snprintf(out, cap, "Escena del %02d/%02d/%04d a las %02d:%02d:%02d", D, M, Y, h, m, s);
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
static bool SceneSave(bool asNew) {
    if (SceneDir().empty()) { MotorErr("No se pudo guardar la escena: no existe ni se pudo crear la carpeta de escenas (content\\motor-tiempo-real\\escenas en la carpeta de Sider)."); return false; }
    if (asNew || !s_scName[0]) { SYSTEMTIME t; GetLocalTime(&t); snprintf(s_scName, sizeof s_scName, "escena_%04d%02d%02d_%02d%02d%02d.txt", (int)t.wYear, (int)t.wMonth, (int)t.wDay, (int)t.wHour, (int)t.wMinute, (int)t.wSecond); }
    FILE* fp = _wfopen(ScenePath(s_scName).c_str(), L"wb");
    if (!fp) { MotorErr("No se pudo guardar la escena %s: Windows no dej\xC3\xB3 escribir el archivo en la carpeta de escenas.", s_scName); return false; }
    float pos[3] = {}, yaw = 0.0f, pitch = 0.0f; const bool cf = FreeCamGetPose(pos, &yaw, &pitch) && g_cfg.live[344][0] > 0.5f;
    fprintf(fp, "escena del Motor en tiempo real 1\r\n");
    fprintf(fp, "camara %d %.4f %.4f %.4f %.5f %.5f\r\n", cf ? 1 : 0, pos[0], pos[1], pos[2], yaw, pitch);
    fprintf(fp, "vista %d %d %d\r\n", MotorViewGet(0), MotorViewGet(1), MotorViewGet(2));
    fprintf(fp, "grilla %d\r\njugador %d\r\n", g_cfg.motorGrid != 0 ? 1 : 0, MotorUiPlayer() ? 1 : 0);
    for (int i = 0; i < s_nLights; i++) fprintf(fp, "luz %d %d %s\r\n", s_lights[i].on ? 1 : 0, s_lights[i].kind, s_lights[i].name);
    fclose(fp); char lb[96]; SceneLabel(s_scName, lb, sizeof lb); Note("Guardada: %s", lb);
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
    { "G  /  J  /  C", "grilla  /  jugador  /  c\xC3\xA1mara libre o del juego" },
    { "V  /  Shift + V", "vista del render siguiente  /  anterior" },
    { "Z", "alambre: no, s\xC3\xAD, con teselaci\xC3\xB3n" },
    { "L  /  Shift + L", "prender o apagar todas las luces  /  como estaban" },
    { "Num 1  /  3  /  7", "mirar de frente  /  derecha  /  arriba (con Ctrl, el lado opuesto)" },
    { "Num 0", "volver a la pose inicial de la c\xC3\xA1mara" },
    { "F1", "esta ayuda" },
    { "Esc", "cerrar el men\xC3\xBA abierto o salir del Motor" },
};
static const int kHelpN = (int)(sizeof kHelp / sizeof kHelp[0]);
static void ToolbarDraw() {
    if (!s_iconTried) IconsLoad();
    ImDrawList* dl = ImGui::GetForegroundDrawList(); const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float k = std::max(1.0f, ds.y / 1080.0f);
    float mx = -1.0e6f, my = -1.0e6f, sx = 1.0f, sy = 1.0f; bool front = false;
    { HWND h = OverlayHwnd(); POINT p{}; RECT rc{};
      if (h && GetForegroundWindow() == h && GetCursorPos(&p) && ScreenToClient(h, &p) && GetClientRect(h, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
          sx = (float)(rc.right - rc.left) / ds.x; sy = (float)(rc.bottom - rc.top) / ds.y; mx = (float)p.x / sx; my = (float)p.y / sy; front = true; } }
    ImFont* font = ImGui::GetFont();
    const ImU32 cPanel = IM_COL32(37, 37, 38, 244), cHead = IM_COL32(51, 51, 55, 255), cLine = IM_COL32(14, 14, 15, 255), cRow = IM_COL32(62, 62, 66, 255),
                ink = IM_COL32(222, 222, 222, 255), dim = IM_COL32(142, 142, 146, 255), acc = IM_COL32(240, 138, 36, 255), cField = IM_COL32(27, 27, 28, 255);
    const float barH = kBarH * k, leftW = kLeftW * k, rightW = kRightW * k, statH = kStatH * k, fs = 15.0f * k, fsS = 12.5f * k, rowH = 28.0f * k;
    // (15/11 d, Agustin 02:37: "lindo y tambien con animaciones") al abrir, los paneles entran deslizando desde su borde; la marca
    // del arbol se desliza de una fila a otra; el contenido del inspector entra con un fundido corto al cambiar de fila. Se hace
    // moviendo / atenuando los vertices ya emitidos de cada parte (la lista de dibujo no tiene transformaciones).
    { const double now = Now(); const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime);
      if (now - s_uiT > 0.4) { s_openA = 0.0f; s_selY = -1.0f; s_inspA = 1.0f; s_menuOpen = -1; s_menuKb = -1; s_modal = 0; s_origN = 0; s_pendN = 0; }
      s_uiT = now; s_openA = std::min(1.0f, s_openA + dt / 0.28f); s_inspA = std::min(1.0f, s_inspA + dt / 0.16f);
      if (s_sel != s_selWas) { s_selWas = s_sel; s_inspA = 0.0f; } }
    const float eo = 1.0f - (1.0f - s_openA) * (1.0f - s_openA) * (1.0f - s_openA);      // salida suave
    // (15/11 e, Agustin 02:41: "que este motor en la pantalla pueda leer todo tipos de luces. y sistemas del juego") 4 veces por
    // segundo se releen las luces y el conteo de objetos de la escena grafica (motorscene.cpp); la marcada se sigue por su direccion
    if (Now() - s_sceneT > 0.25) { s_sceneT = Now();
        static MotorLight tmp[64]; MotorSceneInfo si{}; const int n = MotorSceneLights(tmp, 64, &si);
        if (n >= 0) { memcpy(s_lights, tmp, sizeof(MotorLight) * (size_t)n); s_nLights = n; s_info = si;
            for (int i = 0; i < n; i++) { bool f = false; for (int q = 0; q < s_origN; q++) if (s_orig[q].obj == s_lights[i].obj) f = true; if (!f && s_origN < 64) s_orig[s_origN++] = OrigL{ s_lights[i].obj, s_lights[i].on }; }
            if (s_pendN > 0 && Now() < s_pendUntil) PendApply(); }
        else MotorErr("No se pudo leer la escena del motor: el juego estaba cambiando sus listas de objetos. Se reintenta solo; si este aviso sigue, sal\xC3\xAD del Motor y volv\xC3\xA9 a entrar.");
        if (s_sel >= 4) { int f = -1; for (int i = 0; i < s_nLights; i++) if (s_lights[i].obj == s_selObj) f = i; if (f < 0) { s_sel = 0; s_selObj = 0; } else s_sel = 4 + f; } }
    static const char* const kKind[5] = { "Luz", "Sol", "Local", "Punto", "Foco" };
    auto anim = [&](int from, float dx, float dy, float a) {
        for (int i = from; i < dl->VtxBuffer.Size; i++) { ImDrawVert& v = dl->VtxBuffer[i]; v.pos.x += dx; v.pos.y += dy;
            if (a < 0.999f) { const ImU32 al = (ImU32)((float)(v.col >> 24) * std::max(0.0f, a)); v.col = (v.col & 0x00ffffffu) | (al << 24); } } };
    int hot = -1; const int hp = s_hotPrev;
    auto hit = [&](int id, const ImVec2& a, const ImVec2& b) { if (mx >= a.x && mx <= b.x && my >= a.y && my <= b.y) hot = id; return hp == id; };
    auto text = [&](float x, float yc, ImU32 c, const char* t, float size) { const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t); dl->AddText(font, size, ImVec2(x, yc - ts.y * 0.5f), c, t); return ts.x; };
    auto textC = [&](const ImVec2& a, const ImVec2& b, ImU32 c, const char* t, float size) { const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t); dl->AddText(font, size, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), c, t); };
    int nz = 0;
    auto zone = [&](const ImVec2& a, const ImVec2& b) { if (nz < 6) { s_hr[nz][0].store(a.x * sx); s_hr[nz][1].store(a.y * sy); s_hr[nz][2].store(b.x * sx); s_hr[nz][3].store(b.y * sy); nz++; } };
    const bool camFree = g_cfg.live[344][0] > 0.5f, grid = g_cfg.motorGrid != 0, player = MotorUiPlayer();
    // selector partido: n opciones en una caja; devuelve nada, anota hot = id0 + i
    auto seg = [&](int id0, const ImVec2& a, const ImVec2& b, int n, int cur, const char* const* names) {
        dl->AddRectFilled(a, b, cField, 3.0f * k); const float w = (b.x - a.x) / (float)std::max(1, n);
        for (int i = 0; i < n; i++) { const ImVec2 p0(a.x + (float)i * w, a.y), p1(a.x + (float)(i + 1) * w, b.y); const bool hv = hit(id0 + i, p0, p1);
            if (i == cur) dl->AddRectFilled(ImVec2(p0.x + 1.0f, p0.y + 1.0f), ImVec2(p1.x - 1.0f, p1.y - 1.0f), acc, 2.0f * k); else if (hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 22), 2.0f * k);
            textC(p0, p1, i == cur ? IM_COL32(20, 20, 20, 255) : hv ? ink : IM_COL32(190, 190, 192, 255), names[i], fsS + 1.0f * k); }
        dl->AddRect(a, b, cLine, 3.0f * k);
    };
    float mX[6] = {};      // donde empieza cada titulo de la barra de menus (para colgar su lista)
    // ---------------- barra de herramientas ----------------
    { const int v0 = dl->VtxBuffer.Size; const ImVec2 a(0, 0), b(ds.x, barH); zone(a, b);
      dl->AddRectFilled(a, b, cHead); dl->AddLine(ImVec2(0, barH), ImVec2(ds.x, barH), cLine, 1.0f);
      float x = 0.0f;
      { const ImVec2 p0(x, 0), p1(x + barH * 1.15f, barH); const bool hv = hit(0, p0, p1); if (hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 26));
        const ImVec2 o((p0.x + p1.x) * 0.5f, barH * 0.5f); const float h2 = barH * 0.27f;
        if (s_icon[0]) dl->AddImage((ImTextureID)(uintptr_t)s_icon[0], ImVec2(o.x - h2, o.y - h2), ImVec2(o.x + h2, o.y + h2), ImVec2(0, 0), ImVec2(1, 1), ink); else IconFallback(dl, 0, o, barH * 0.95f, ink, k);
        x = p1.x; dl->AddLine(ImVec2(x, 6.0f * k), ImVec2(x, barH - 6.0f * k), IM_COL32(80, 80, 84, 255), 1.0f); }
      x += 14.0f * k; x += text(x, barH * 0.5f, acc, "FOX ENGINE", fs) + 14.0f * k;      // el nombre largo y la escena abierta van a la derecha de la barra
      dl->AddLine(ImVec2(x, 6.0f * k), ImVec2(x, barH - 6.0f * k), IM_COL32(80, 80, 84, 255), 1.0f); x += 8.0f * k;
      // (15/11 g) barra de menus: cada titulo abre su lista de acciones (se dibuja al final, encima de todo)
      { const char* const mn[6] = { "Archivo", "Escena", "Ver", "Render", "Luces", "Ayuda" };
        for (int i = 0; i < 6; i++) { const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, mn[i]); const float w = ts.x + 24.0f * k;
            const ImVec2 p0(x, 0), p1(x + w, barH); const bool hv = hit(600 + i, p0, p1), op = s_menuOpen == i;
            if (op) { dl->AddRectFilled(p0, p1, IM_COL32(43, 43, 46, 255)); dl->AddRectFilled(p0, ImVec2(p1.x, 2.0f * k), acc); } else if (hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 24));
            textC(p0, p1, op ? IM_COL32(255, 214, 170, 255) : ink, mn[i], fs); mX[i] = x; x = p1.x; }
        char sn[140], lb[96]; if (s_scName[0]) { SceneLabel(s_scName, lb, sizeof lb); snprintf(sn, sizeof sn, "Motor en tiempo real  \xC2\xB7  %s", lb); } else snprintf(sn, sizeof sn, "Motor en tiempo real  \xC2\xB7  escena sin guardar");
        const ImVec2 ts = font->CalcTextSizeA(fsS + 1.0f * k, FLT_MAX, 0.0f, sn); if (ds.x - ts.x - 14.0f * k > x + 20.0f * k) text(ds.x - ts.x - 14.0f * k, barH * 0.5f, dim, sn, fsS + 1.0f * k); }
      anim(v0, 0.0f, -(1.0f - eo) * barH, eo);
    }
    // ---------------- arbol de escena ----------------
    const float y0 = barH + 1.0f, y1 = ds.y - statH;
    { const int v0 = dl->VtxBuffer.Size; const ImVec2 a(0, y0), b(leftW, y1); zone(a, b);
      dl->AddRectFilled(a, b, cPanel); dl->AddLine(ImVec2(leftW, y0), ImVec2(leftW, y1), cLine, 1.0f);
      dl->AddRectFilled(a, ImVec2(leftW, y0 + rowH), cHead); text(12.0f * k, y0 + rowH * 0.5f, dim, "ESCENA", fsS);
      const char* const nm[4] = { "Estudio", "C\xC3\xA1mara", "Grilla de piso", "Jugador" }; const char* const st[4] = { "", camFree ? "libre" : "del juego", grid ? "" : "oculta", player ? "" : "sin cargar" };
      const float rowL = 23.0f * k, yL0 = y0 + rowH * 6.0f;       // luces: filas mas bajas, despues de las 4 filas y el titulo del grupo
      const int maxL = std::max(0, (int)((y1 - yL0 - 22.0f * k) / rowL)), shownL = std::min(s_nLights, maxL);
      { const float ty = s_sel < 4 ? y0 + rowH + (float)s_sel * rowH : yL0 + (float)(s_sel - 4) * rowL; const float sh = s_sel < 4 ? rowH : rowL;
        if (s_sel < 4 || s_sel - 4 < shownL) { if (s_selY < 0.0f) s_selY = ty; s_selY += (ty - s_selY) * std::min(1.0f, ImGui::GetIO().DeltaTime * 18.0f); if (fabsf(ty - s_selY) < 0.5f) s_selY = ty;
        dl->AddRectFilled(ImVec2(0, s_selY), ImVec2(leftW, s_selY + sh), cRow); dl->AddRectFilled(ImVec2(0, s_selY), ImVec2(3.0f * k, s_selY + sh), acc); } }
      for (int i = 0; i < 4; i++) { const float y = y0 + rowH + (float)i * rowH; const ImVec2 p0(0, y), p1(leftW, y + rowH); const bool hv = hit(10 + i, p0, p1);
          if (s_sel != i && hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 14));
          const float ind = i == 0 ? 12.0f * k : 34.0f * k; const float yc = y + rowH * 0.5f;
          if (i == 0) { dl->AddTriangleFilled(ImVec2(ind, yc - 3.0f * k), ImVec2(ind + 9.0f * k, yc - 3.0f * k), ImVec2(ind + 4.5f * k, yc + 4.0f * k), dim); }
          else dl->AddRectFilled(ImVec2(ind - 12.0f * k, yc - 1.0f), ImVec2(ind - 4.0f * k, yc), IM_COL32(90, 90, 94, 255));
          const bool off = (i == 2 && !grid) || (i == 3 && !player);
          const float tx = (i == 0 ? ind + 16.0f * k : ind) ; const float w = text(tx, yc, off ? dim : ink, nm[i], fs);
          if (st[i][0]) text(tx + w + 10.0f * k, yc, dim, st[i], fsS);
          if (i >= 2) {      // ojo: visible / oculto
              const ImVec2 e0(leftW - 34.0f * k, y + 3.0f * k), e1(leftW - 8.0f * k, y + rowH - 3.0f * k); const bool he = hit(18 + i, e0, e1);
              if (he) dl->AddRectFilled(e0, e1, IM_COL32(255, 255, 255, 30), 3.0f * k);
              const ImVec2 o((e0.x + e1.x) * 0.5f, yc); const ImU32 c = off ? IM_COL32(100, 100, 104, 255) : ink;
              dl->AddCircle(o, 6.0f * k, c, 16, 1.5f * k); if (!off) dl->AddCircleFilled(o, 2.6f * k, c, 10); else dl->AddLine(ImVec2(o.x - 7.0f * k, o.y + 6.0f * k), ImVec2(o.x + 7.0f * k, o.y - 6.0f * k), c, 1.5f * k);
          } }
      {   // grupo de luces de la escena grafica (todas: sol, focos, puntuales, locales)
          int nOn = 0; for (int i = 0; i < s_nLights; i++) if (s_lights[i].on) nOn++;
          const float yg = y0 + rowH * 5.0f, yc = yg + rowH * 0.5f; char gt[64];
          dl->AddLine(ImVec2(0, yg), ImVec2(leftW, yg), IM_COL32(60, 60, 64, 255), 1.0f);
          dl->AddTriangleFilled(ImVec2(12.0f * k, yc - 3.0f * k), ImVec2(21.0f * k, yc - 3.0f * k), ImVec2(16.5f * k, yc + 4.0f * k), dim);
          const float w = text(28.0f * k, yc, ink, "Luces", fs); snprintf(gt, sizeof gt, "%d prendidas de %d", nOn, s_info.lights); text(28.0f * k + w + 10.0f * k, yc, dim, gt, fsS);
          for (int i = 0; i < shownL; i++) { const MotorLight& L = s_lights[i]; const float y = yL0 + (float)i * rowL, yc2 = y + rowL * 0.5f; const ImVec2 p0(0, y), p1(leftW, y + rowL); const bool hv = hit(300 + i, p0, p1);
              if (s_sel != 4 + i && hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 14));
              const float mxc = std::max(L.color[0], std::max(L.color[1], L.color[2])); const float inv = mxc > 1e-6f ? 1.0f / mxc : 0.0f;
              const ImU32 sw = L.on ? IM_COL32((int)(std::clamp(L.color[0] * inv, 0.0f, 1.0f) * 255.0f), (int)(std::clamp(L.color[1] * inv, 0.0f, 1.0f) * 255.0f), (int)(std::clamp(L.color[2] * inv, 0.0f, 1.0f) * 255.0f), 255) : IM_COL32(70, 70, 74, 255);
              dl->AddCircleFilled(ImVec2(40.0f * k, yc2), 4.5f * k, sw, 12); if (!L.on) dl->AddCircle(ImVec2(40.0f * k, yc2), 4.5f * k, IM_COL32(110, 110, 114, 255), 12, 1.0f);
              char nm2[48]; if (L.name[0]) snprintf(nm2, sizeof nm2, "%.26s", L.name); else snprintf(nm2, sizeof nm2, "%s sin nombre", kKind[L.kind]);
              text(52.0f * k, yc2, L.on ? ink : dim, nm2, fsS + 1.0f * k);
              const ImVec2 e0(leftW - 34.0f * k, y + 2.0f * k), e1(leftW - 8.0f * k, y + rowL - 2.0f * k); const bool he = hit(400 + i, e0, e1);
              if (he) dl->AddRectFilled(e0, e1, IM_COL32(255, 255, 255, 30), 3.0f * k);
              const ImVec2 o((e0.x + e1.x) * 0.5f, yc2); const ImU32 c = L.on ? ink : IM_COL32(100, 100, 104, 255);
              dl->AddCircle(o, 5.0f * k, c, 14, 1.4f * k); if (L.on) dl->AddCircleFilled(o, 2.2f * k, c, 10); else dl->AddLine(ImVec2(o.x - 6.0f * k, o.y + 5.0f * k), ImVec2(o.x + 6.0f * k, o.y - 5.0f * k), c, 1.4f * k); }
          if (s_nLights > shownL) { char mm[48]; snprintf(mm, sizeof mm, "y %d m\xC3\xA1s (no entran en pantalla)", s_nLights - shownL); text(52.0f * k, yL0 + (float)shownL * rowL + 11.0f * k, dim, mm, fsS); }
          if (s_info.lights == 0) text(34.0f * k, yL0 + 11.0f * k, dim, "La escena no tiene luces cargadas.", fsS);
      }
      anim(v0, -(1.0f - eo) * leftW, 0.0f, eo);
    }
    // ---------------- inspector ----------------
    { const int v0 = dl->VtxBuffer.Size; int v1 = v0; const float x0 = ds.x - rightW; const ImVec2 a(x0, y0), b(ds.x, y1); zone(a, b);
      dl->AddRectFilled(a, b, cPanel); dl->AddLine(ImVec2(x0, y0), ImVec2(x0, y1), cLine, 1.0f);
      dl->AddRectFilled(a, ImVec2(ds.x, y0 + rowH), cHead); text(x0 + 12.0f * k, y0 + rowH * 0.5f, dim, "INSPECTOR", fsS);
      const char* const title[4] = { "Estudio  \xC2\xB7  Render", "C\xC3\xA1mara", "Grilla de piso", "Jugador" };
      float y = y0 + rowH; dl->AddRectFilled(ImVec2(x0 + 1.0f, y), ImVec2(ds.x, y + rowH + 4.0f * k), IM_COL32(45, 45, 48, 255)); { char tl[64]; const MotorLight* SL = s_sel >= 4 && s_sel - 4 < s_nLights ? &s_lights[s_sel - 4] : nullptr;
        if (SL) snprintf(tl, sizeof tl, "%s  \xC2\xB7  %.30s", kKind[SL->kind], SL->name[0] ? SL->name : "sin nombre"); else snprintf(tl, sizeof tl, "%s", title[std::min(s_sel, 3)]);
        text(x0 + 12.0f * k, y + (rowH + 4.0f * k) * 0.5f, ink, tl, fs + 1.0f * k); } y += rowH + 4.0f * k + 8.0f * k;
      const float lx = x0 + 12.0f * k, fx0 = x0 + 12.0f * k, fx1 = ds.x - 12.0f * k;
      v1 = dl->VtxBuffer.Size;          // desde aca: contenido de lo marcado (fundido al cambiar)
      auto label = [&](const char* t) { text(lx, y + 9.0f * k, dim, t, fsS); y += 20.0f * k; };
      auto value = [&](const char* nmv, const char* v) { text(lx, y + rowH * 0.5f, dim, nmv, fsS + 1.0f * k); const ImVec2 p0(x0 + rightW * 0.42f, y + 3.0f * k), p1(fx1, y + rowH - 3.0f * k); dl->AddRectFilled(p0, p1, cField, 3.0f * k); text(p0.x + 8.0f * k, y + rowH * 0.5f, ink, v, fsS + 1.0f * k); y += rowH; };
      auto button = [&](int id, const char* t) { const ImVec2 p0(fx0, y + 4.0f * k), p1(fx1, y + rowH + 6.0f * k); const bool hv = hit(id, p0, p1); dl->AddRectFilled(p0, p1, hv ? IM_COL32(86, 86, 92, 255) : IM_COL32(66, 66, 70, 255), 3.0f * k); dl->AddRect(p0, p1, cLine, 3.0f * k); textC(p0, p1, ink, t, fs); y += rowH + 14.0f * k; };
      if (s_sel == 0) {
          label("VISTA DEL RENDER");
          const int n = MotorViewCount(0), cur = MotorViewGet(0); const float rh = 25.0f * k;
          dl->AddRectFilled(ImVec2(fx0, y), ImVec2(fx1, y + (float)n * rh), cField, 3.0f * k);
          for (int i = 0; i < n; i++) { const ImVec2 p0(fx0, y + (float)i * rh), p1(fx1, y + (float)(i + 1) * rh); const bool hv = hit(100 + i, p0, p1);
              if (i == cur) dl->AddRectFilled(p0, p1, IM_COL32(240, 138, 36, 60)); else if (hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 18));
              const ImVec2 o(p0.x + 14.0f * k, (p0.y + p1.y) * 0.5f); dl->AddCircle(o, 5.0f * k, i == cur ? acc : IM_COL32(110, 110, 114, 255), 14, 1.4f * k); if (i == cur) dl->AddCircleFilled(o, 2.6f * k, acc, 10);
              text(p0.x + 28.0f * k, o.y, i == cur ? IM_COL32(255, 214, 170, 255) : ink, MotorViewName(0, i), fsS + 1.5f * k); }
          dl->AddRect(ImVec2(fx0, y), ImVec2(fx1, y + (float)n * rh), cLine, 3.0f * k); y += (float)n * rh + 12.0f * k;
          for (int r = 1; r <= 2; r++) { label(r == 1 ? "ALAMBRE" : "PANTALLA PARTIDA"); const int m = std::min(4, MotorViewCount(r)); const char* nms[4] = { "", "", "", "" }; for (int i = 0; i < m; i++) nms[i] = MotorViewName(r, i);
              if (r == 1 && m == 3) nms[2] = "Con teselaci\xC3\xB3n";
              seg(100 + r * 16, ImVec2(fx0, y), ImVec2(fx1, y + rowH), m, MotorViewGet(r), nms); y += rowH + 12.0f * k; }
          {   label("ESCENA GR\xC3\x81" "FICA DEL MOTOR"); char b1[32];      // lo que el motor tiene cargado ahora (leido de sus listas)
              snprintf(b1, sizeof b1, "%d", s_info.scenes); value("Escenas", b1); snprintf(b1, sizeof b1, "%d", s_info.objects); value("Objetos", b1);
              snprintf(b1, sizeof b1, "%d", s_info.models); value("Modelos", b1); snprintf(b1, sizeof b1, "%d", s_info.lights); value("Luces", b1); }
      } else if (s_sel == 1) {
          label("MODO"); { const char* const nms[2] = { "Libre", "Del juego" }; seg(200, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 2, camFree ? 0 : 1, nms); y += rowH + 12.0f * k; }
          float pos[3] = {}, yaw = 0, pitch = 0; char b1[48];
          if (camFree && FreeCamGetPose(pos, &yaw, &pitch)) {
              label("TRANSFORMACI\xC3\x93N");
              snprintf(b1, sizeof b1, "%.2f m", pos[0]); value("Posici\xC3\xB3n X", b1); snprintf(b1, sizeof b1, "%.2f m", pos[1]); value("Posici\xC3\xB3n Y", b1); snprintf(b1, sizeof b1, "%.2f m", pos[2]); value("Posici\xC3\xB3n Z", b1);
              snprintf(b1, sizeof b1, "%.1f\xC2\xB0", yaw * 57.29578f); value("Giro", b1); snprintf(b1, sizeof b1, "%.1f\xC2\xB0", pitch * 57.29578f); value("Inclinaci\xC3\xB3n", b1);
              y += 8.0f * k; label("LENTE"); snprintf(b1, sizeof b1, "%.1f\xC2\xB0", FreeCamFovNow() * 57.29578f); value("Campo de visi\xC3\xB3n", b1);
              y += 8.0f * k; label("MIRAR DESDE");
              { const char* const v1[3] = { "Derecha", "Arriba", "Frente" }; const char* const v2[3] = { "Izquierda", "Abajo", "Atr\xC3\xA1s" };
                seg(230, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 3, -1, v1); y += rowH + 4.0f * k; seg(233, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 3, -1, v2); y += rowH + 10.0f * k; }
              button(202, "Volver a la pose inicial");
              const char* const ay[4] = { "Bot\xC3\xB3n derecho: mirar y volar (W A S D)", "Bot\xC3\xB3n del medio: panear", "Alt + clic izquierdo: orbitar", "Rueda: acercar y alejar" };
              for (int i = 0; i < 4; i++) text(lx, y + 8.0f * k + (float)i * 17.0f * k, dim, ay[i], fsS);
          } else text(lx, y + 10.0f * k, dim, "La c\xC3\xA1mara la maneja el juego.", fsS + 1.0f * k);
      } else if (s_sel == 2) {
          label("VISIBLE"); { const char* const nms[2] = { "S\xC3\xAD", "No" }; seg(210, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 2, grid ? 0 : 1, nms); y += rowH + 12.0f * k; }
          label("MEDIDAS"); value("Celda", "1 m"); value("L\xC3\xADnea fuerte", "cada 5 m"); value("Eje X", "rojo"); value("Eje Z", "azul");
      } else if (s_sel >= 4) {
          if (s_sel - 4 < s_nLights) { const MotorLight& L = s_lights[s_sel - 4]; char b1[48];
              label("ESTADO"); { const char* const nms[2] = { "Prendida", "Apagada" }; seg(250, ImVec2(fx0, y), ImVec2(fx1, y + rowH), 2, L.on ? 0 : 1, nms); y += rowH + 12.0f * k; }
              label("LUZ"); value("Tipo", kKind[L.kind]);
              const float mxc = std::max(L.color[0], std::max(L.color[1], L.color[2]));
              snprintf(b1, sizeof b1, "%.3g", mxc); value("Intensidad", b1);
              { text(lx, y + rowH * 0.5f, dim, "Color", fsS + 1.0f * k); const float inv = mxc > 1e-6f ? 1.0f / mxc : 0.0f; const ImVec2 p0(x0 + rightW * 0.42f, y + 3.0f * k), p1(fx1, y + rowH - 3.0f * k);
                dl->AddRectFilled(p0, p1, IM_COL32((int)(std::clamp(L.color[0] * inv, 0.0f, 1.0f) * 255.0f), (int)(std::clamp(L.color[1] * inv, 0.0f, 1.0f) * 255.0f), (int)(std::clamp(L.color[2] * inv, 0.0f, 1.0f) * 255.0f), 255), 3.0f * k); dl->AddRect(p0, p1, cLine, 3.0f * k); y += rowH; }
              snprintf(b1, sizeof b1, "%.2f  %.2f  %.2f", L.color[0], L.color[1], L.color[2]); value("Rojo Verde Azul", b1);
              value("Sombra", L.shadow ? "s\xC3\xAD" : "no");
              if (L.kind >= 2) { snprintf(b1, sizeof b1, "%.2f m", L.range); value("Alcance", b1); }
              y += 8.0f * k; label(L.kind == 1 ? "DIRECCI\xC3\x93N" : "POSICI\xC3\x93N");
              snprintf(b1, sizeof b1, L.kind == 1 ? "%.3f" : "%.2f m", L.pos[0]); value("X", b1); snprintf(b1, sizeof b1, L.kind == 1 ? "%.3f" : "%.2f m", L.pos[1]); value("Y", b1); snprintf(b1, sizeof b1, L.kind == 1 ? "%.3f" : "%.2f m", L.pos[2]); value("Z", b1);
              if (!L.writable) text(lx, y + 10.0f * k, dim, "Luz de una clase que no conozco: solo lectura.", fsS);
          }
      } else {
          label("ESTADO"); value("Modelo", player ? "cargado" : "sin cargar"); y += 6.0f * k;
          button(220, player ? "Quitar de la escena" : "Cargar en la escena");
          text(lx, y + 8.0f * k, dim, "Es el \xC3\xBAltimo jugador que abriste en Editar.", fsS);
      }
      anim(v1, (1.0f - s_inspA) * 14.0f * k, 0.0f, s_inspA);
      anim(v0, (1.0f - eo) * rightW, 0.0f, eo);
    }
    // ---------------- barra de estado ----------------
    { const int v0 = dl->VtxBuffer.Size; const ImVec2 a(0, y1), b(ds.x, ds.y); zone(a, b); dl->AddRectFilled(a, b, IM_COL32(30, 30, 31, 250)); dl->AddLine(a, ImVec2(ds.x, y1), cLine, 1.0f);
      char st[200]; snprintf(st, sizeof st, "Vista: %s    Alambre: %s    C\xC3\xA1mara: %s    Objetos: %d    Luces: %d", MotorViewName(0, MotorViewGet(0)), MotorViewName(1, MotorViewGet(1)), camFree ? "libre" : "del juego", s_info.objects, s_info.lights);
      text(12.0f * k, y1 + statH * 0.5f, dim, st, fsS);
      char fp[32]; snprintf(fp, sizeof fp, "%.0f fps", ImGui::GetIO().Framerate); const ImVec2 ts = font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, fp); text(ds.x - ts.x - 12.0f * k, y1 + statH * 0.5f, dim, fp, fsS);
      anim(v0, 0.0f, (1.0f - eo) * statH, eo); }
    {   // ---------------- errores: recuadros rojos apilados abajo a la izquierda de la vista 3D ----------------
        MErr e[8]; int total = 0; { std::lock_guard<std::mutex> lk(s_errMx); memcpy(e, s_err, sizeof e); total = s_errTotal; }
        const double now = Now(); const float bw = std::min(620.0f * k, ds.x - leftW - rightW - 32.0f * k), pad = 12.0f * k; float yb = y1 - 14.0f * k;
        for (int j = 0; j < 8 && j < total && bw > 200.0f * k; j++) { const MErr& m = e[(total - 1 - j) & 7]; const float age = (float)(now - m.t); if (age > 10.0f) continue;
            const float a = std::min(1.0f, std::min(age / 0.18f, (10.0f - age) / 0.5f)); const float wrap = bw - pad * 2.0f;
            const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, wrap, m.msg); const float bh = ts.y + pad * 2.0f + 20.0f * k;
            const ImVec2 p0(leftW + 16.0f * k + (1.0f - a) * -18.0f * k, yb - bh), p1(p0.x + bw, yb);
            dl->AddRectFilled(p0, p1, IM_COL32(128, 22, 22, (int)(238 * a)), 4.0f * k); dl->AddRect(p0, p1, IM_COL32(255, 96, 96, (int)(255 * a)), 4.0f * k, 0, 1.5f * k);
            dl->AddRectFilled(p0, ImVec2(p0.x + 5.0f * k, p1.y), IM_COL32(255, 96, 96, (int)(255 * a)), 4.0f * k);
            dl->AddText(font, fsS + 1.0f * k, ImVec2(p0.x + pad + 4.0f * k, p0.y + pad - 2.0f * k), IM_COL32(255, 170, 170, (int)(255 * a)), "ERROR DEL MOTOR");
            dl->AddText(font, fs, ImVec2(p0.x + pad + 4.0f * k, p0.y + pad + 18.0f * k), IM_COL32(255, 240, 240, (int)(255 * a)), m.msg, nullptr, wrap);
            yb -= bh + 8.0f * k; }
        if (total > 0) {      // cuenta en la barra de estado: clic = volver a mostrarlos
            char eb[40]; snprintf(eb, sizeof eb, total == 1 ? "1 error" : "%d errores", total); const ImVec2 ts = font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, eb);
            const ImVec2 p0(ds.x - ts.x - 110.0f * k, y1 + 3.0f * k), p1(p0.x + ts.x + 16.0f * k, ds.y - 3.0f * k); const bool hv = hit(500, p0, p1);
            dl->AddRectFilled(p0, p1, IM_COL32(150, 28, 28, hv ? 255 : 220), 3.0f * k); textC(p0, p1, IM_COL32(255, 230, 230, 255), eb, fsS); }
    }
    // ---------------- aviso corto (no es un error): pastilla arriba al centro de la vista 3D ----------------
    { const float age = (float)(Now() - s_noteT);
      if (s_note[0] && age < 3.5f) { const float a = std::max(0.0f, std::min(1.0f, std::min(age / 0.15f, (3.5f - age) / 0.4f))); const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, s_note);
          const float cx = (leftW + ds.x - rightW) * 0.5f, yy = barH + 14.0f * k - (1.0f - a) * 8.0f * k; const ImVec2 p0(cx - ts.x * 0.5f - 16.0f * k, yy), p1(cx + ts.x * 0.5f + 16.0f * k, yy + 32.0f * k);
          dl->AddRectFilled(p0, p1, IM_COL32(30, 30, 31, (int)(240 * a)), 16.0f * k); dl->AddRect(p0, p1, IM_COL32(240, 138, 36, (int)(220 * a)), 16.0f * k, 0, 1.2f * k);
          dl->AddText(font, fs, ImVec2(cx - ts.x * 0.5f, yy + (32.0f * k - ts.y) * 0.5f), IM_COL32(236, 236, 236, (int)(255 * a)), s_note); } }
    // ---------------- ventana al centro de la vista 3D: abrir escena / controles y atajos ----------------
    if (s_modal != 0) {
        const ImVec2 va(leftW + 1.0f, y0), vb(ds.x - rightW - 1.0f, y1); zone(va, vb); hit(898, va, vb);
        dl->AddRectFilled(va, vb, IM_COL32(0, 0, 0, 120));
        const float pw = std::min((s_modal == 1 ? 520.0f : 700.0f) * k, vb.x - va.x - 24.0f * k), lh = 27.0f * k; const int rows = s_modal == 1 ? std::max(1, s_scListN) : kHelpN;
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
                if (cur) { const ImVec2 ts = font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, "abierta"); text(r1.x - ts.x - 10.0f * k, (r0.y + r1.y) * 0.5f, dim, "abierta", fsS); } }
        } else {
            const float kx = p0.x + 16.0f * k, dx = p0.x + pw * 0.33f;
            for (int i = 0; i < kHelpN; i++) { const float yc = y + (float)i * lh + lh * 0.5f; if (i == 5) dl->AddLine(ImVec2(p0.x + 10.0f * k, yc - lh * 0.5f), ImVec2(p1.x - 10.0f * k, yc - lh * 0.5f), IM_COL32(70, 70, 74, 255), 1.0f);
                const ImVec2 ts = font->CalcTextSizeA(fsS + 1.0f * k, FLT_MAX, 0.0f, kHelp[i][0]);
                dl->AddRectFilled(ImVec2(kx - 6.0f * k, yc - 10.0f * k), ImVec2(kx + ts.x + 6.0f * k, yc + 10.0f * k), cField, 3.0f * k); dl->AddRect(ImVec2(kx - 6.0f * k, yc - 10.0f * k), ImVec2(kx + ts.x + 6.0f * k, yc + 10.0f * k), IM_COL32(80, 80, 84, 255), 3.0f * k);
                text(kx, yc, IM_COL32(255, 214, 170, 255), kHelp[i][0], fsS + 1.0f * k); text(dx, yc, ink, kHelp[i][1], fsS + 1.5f * k); }
        }
        { const char* ct = "Cerrar  (Esc)"; const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, ct); const ImVec2 c0(p1.x - ts.x - 44.0f * k, p1.y - 42.0f * k), c1(p1.x - 12.0f * k, p1.y - 12.0f * k); const bool hv = hit(899, c0, c1);
          dl->AddRectFilled(c0, c1, hv ? IM_COL32(86, 86, 92, 255) : IM_COL32(66, 66, 70, 255), 3.0f * k); dl->AddRect(c0, c1, cLine, 3.0f * k); textC(c0, c1, ink, ct, fs); }
    }
    // ---------------- menu desplegable abierto (lo ultimo: queda encima y gana el clic) ----------------
    struct MI { const char* t; const char* sc; int id; int chk; int kind; };      // kind 0 accion, 1 separador, 2 titulo de grupo, 3 en preparacion (gris); chk 1 = tilde, 3 = opcion elegida
    auto menuItems = [&](int m, MI* it) { int n = 0;
        auto add = [&](const char* t, const char* sc, int id, int chk = -1, int kind = 0) { if (n < 28) it[n++] = MI{ t, sc, id, chk, kind }; };
        if (m == 0) { add("Nueva escena", "Ctrl + N", 710); add("Abrir escena...", "Ctrl + O", 711); add("Guardar escena", "Ctrl + S", 712); add("Guardar como escena nueva", "Ctrl + Shift + S", 713); add(nullptr, "", -1, -1, 1); add("Cerrar el Motor", "Esc", 0); }
        else if (m == 1) { add(player ? "Quitar el jugador" : "Cargar el jugador", "J", 2); add("Grilla de piso", "G", 1, grid ? 1 : 0); add(nullptr, "", -1, -1, 1);
            add("Cargar animaci\xC3\xB3n (.gani)...", "en preparaci\xC3\xB3n", -1, -1, 3); add("Cargar estadio...", "en preparaci\xC3\xB3n", -1, -1, 3); add("Cinem\xC3\xA1ticas del juego...", "en preparaci\xC3\xB3n", -1, -1, 3); }
        else if (m == 2) { add("C\xC3\xA1mara libre", "C", 3, camFree ? 1 : 0); add(nullptr, "", -1, -1, 1); add("MIRAR DESDE", "", -1, -1, 2);
            add("Frente", "Num 1", 232); add("Atr\xC3\xA1s", "Ctrl + Num 1", 235); add("Derecha", "Num 3", 230); add("Izquierda", "Ctrl + Num 3", 233); add("Arriba", "Num 7", 231); add("Abajo", "Ctrl + Num 7", 234);
            add(nullptr, "", -1, -1, 1); add("Volver a la pose inicial", "Num 0", 202); }
        else if (m == 3) { add("VISTA", "V  /  Shift + V", -1, -1, 2); { const int c = MotorViewGet(0); for (int i = 0; i < MotorViewCount(0) && i < 16; i++) add(MotorViewName(0, i), "", 100 + i, c == i ? 3 : 2); }
            add(nullptr, "", -1, -1, 1); add("ALAMBRE", "Z", -1, -1, 2); { const int c = MotorViewGet(1); for (int i = 0; i < MotorViewCount(1) && i < 4; i++) add(i == 2 ? "S\xC3\xAD, con teselaci\xC3\xB3n" : i == 1 ? "S\xC3\xAD" : MotorViewName(1, i), "", 116 + i, c == i ? 3 : 2); }
            add(nullptr, "", -1, -1, 1); add("PANTALLA PARTIDA", "", -1, -1, 2); { const int c = MotorViewGet(2); for (int i = 0; i < MotorViewCount(2) && i < 4; i++) add(MotorViewName(2, i), "", 132 + i, c == i ? 3 : 2); } }
        else if (m == 4) { add("Prender todas", "L", 700); add("Apagar todas", "L", 701); add("Volver a como estaban", "Shift + L", 702); }
        else { add("Controles y atajos", "F1", 730); add("Volver a mostrar los errores", "", 500); }
        return n; };
    if (s_menuOpen >= 0) {
        MI it[28]; const int n = menuItems(s_menuOpen, it); s_miN = n; s_menuA = std::min(1.0f, s_menuA + ImGui::GetIO().DeltaTime / 0.11f);
        const float ih = 26.0f * k, sepH = 9.0f * k, headH = 22.0f * k, padX = 12.0f * k, chkW = 24.0f * k; float wT = 0.0f, wS = 0.0f, h = 10.0f * k;
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
    auto act = [&](int id) {
        if (id == 0) MotorUiBack();
        else if (id == 1 || id == 20) g_cfg.motorGrid = grid ? 0 : 1;
        else if (id == 2 || id == 21 || id == 220) MotorUiTogglePlayer();
        else if (id == 3) SetCam(!camFree);
        else if (id >= 10 && id < 14) { s_sel = id - 10; s_selObj = 0; }
        else if (id >= 300 && id < 364 && id - 300 < s_nLights) { s_sel = 4 + (id - 300); s_selObj = s_lights[id - 300].obj; }
        else if (id >= 400 && id < 464 && id - 400 < s_nLights) { MotorLight& L = s_lights[id - 400];
            if (!L.writable) MotorErr("La luz \"%s\" es de una clase que el Motor no conoce todav\xC3\xAD" "a: se puede ver pero no cambiar.", L.name);
            else if (MotorLightSetOn(L, !L.on)) L.on = !L.on; else MotorErr("No se pudo %s la luz \"%s\": el juego la sac\xC3\xB3 de la escena mientras tanto.", L.on ? "apagar" : "prender", L.name); }
        else if ((id == 250 || id == 251) && s_sel >= 4 && s_sel - 4 < s_nLights) { MotorLight& L = s_lights[s_sel - 4];
            if (!L.writable) MotorErr("La luz \"%s\" es de una clase que el Motor no conoce todav\xC3\xAD" "a: se puede ver pero no cambiar.", L.name);
            else if (MotorLightSetOn(L, id == 250)) L.on = id == 250; else MotorErr("No se pudo %s la luz \"%s\": el juego la sac\xC3\xB3 de la escena mientras tanto.", id == 250 ? "prender" : "apagar", L.name); }
        else if (id == 500) { int tot = 0; { std::lock_guard<std::mutex> lk(s_errMx); tot = s_errTotal; for (MErr& m : s_err) if (m.msg[0]) m.t = Now(); } if (tot == 0) Note("No hubo errores del Motor"); }
        else if (id >= 100 && id < 148) MotorViewSet((id - 100) / 16, (id - 100) % 16);
        else if (id == 200 || id == 201) SetCam(id == 200);
        else if (id == 202) { if (camFree) FreeCamGizmoHome(); else Note("La pose inicial es de la c\xC3\xA1mara libre: Ver > C\xC3\xA1mara libre (C)"); }
        else if (id >= 230 && id < 236) { static const int kAx[6] = { 0, 2, 4, 1, 3, 5 };      // +X, +Y, +Z / -X, -Y, -Z
            if (camFree) FreeCamGizmoAxis(kAx[id - 230]); else Note("Mirar desde un lado es de la c\xC3\xA1mara libre: Ver > C\xC3\xA1mara libre (C)"); }
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
        else if (id == 740 || id == 741) { const int n = std::max(1, MotorViewCount(0)); MotorViewSet(0, (MotorViewGet(0) + (id == 740 ? 1 : n - 1)) % n); Note("Vista: %s", MotorViewName(0, MotorViewGet(0))); }
        else if (id == 742) { const int n = std::max(1, std::min(3, MotorViewCount(1))); MotorViewSet(1, (MotorViewGet(1) + 1) % n); }
        else if (id >= 800 && id < 814) { if (id - 800 < s_scListN) SceneLoad(s_scList[id - 800]); s_modal = 0; }
        else if (id == 898 || id == 899) s_modal = 0;
    };
    // ---------------- teclado (el juego no lo ve dentro del Motor); volando con el boton derecho las letras son de la camara ----------------
    { auto tap = [&](int vk) { const bool d = (GetAsyncKeyState(vk) & 0x8000) != 0; const bool t = d && !s_kWas[vk & 255]; s_kWas[vk & 255] = d; return t; };
      const bool tF1 = tap(VK_F1), tF2 = tap(VK_F2), tEsc = tap(VK_ESCAPE), tLe = tap(VK_LEFT), tRi = tap(VK_RIGHT), tUp = tap(VK_UP), tDn = tap(VK_DOWN), tEn = tap(VK_RETURN),
                 tN = tap('N'), tO = tap('O'), tS = tap('S'), tG = tap('G'), tJ = tap('J'), tC = tap('C'), tV = tap('V'), tZ = tap('Z'), tL = tap('L'),
                 t0 = tap(VK_NUMPAD0), t1 = tap(VK_NUMPAD1), t3 = tap(VK_NUMPAD3), t7 = tap(VK_NUMPAD7);
      const bool kb = front && !g_menuOpen.load(std::memory_order_relaxed) && !g_menuTyping.load(std::memory_order_relaxed) && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0;
      const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0, alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
      if (fabsf(mx - s_mLastX) + fabsf(my - s_mLastY) > 2.0f) { s_mLastX = mx; s_mLastY = my; s_menuKb = -1; }      // el mouse se movio: manda el mouse
      if (!kb) {}
      else if (s_modal != 0) { if (tEsc || tEn) { s_modal = 0; s_escT.store(Now()); } }
      else if (s_menuOpen >= 0) {
          auto step = [&](int d) { int i = s_menuKb; for (int q = 0; q < s_miN; q++) { i = (i < 0 ? (d > 0 ? 0 : s_miN - 1) : (i + d + s_miN) % s_miN); if (s_miAct[i] >= 0) { s_menuKb = i; return; } } };
          if (tEsc || tF2) { s_menuOpen = -1; s_escT.store(Now()); }
          else if (tLe || tRi) { s_menuOpen = (s_menuOpen + (tRi ? 1 : 5)) % 6; s_menuA = 0.0f; s_menuKb = -1; }
          else if (tDn) step(1); else if (tUp) step(-1);
          else if (tEn && s_menuKb >= 0 && s_menuKb < s_miN && s_miAct[s_menuKb] >= 0) { const int a = s_miAct[s_menuKb]; s_menuOpen = -1; act(a); }
      } else if (tF2) { s_menuOpen = 0; s_menuA = 0.0f; s_menuKb = -1; }
      else if (tF1) act(730);
      else if (ctrl && !alt) { if (tN) act(710); else if (tO) act(711); else if (tS) act(shift ? 713 : 712); else if (t1) act(235); else if (t3) act(233); else if (t7) act(234); }
      else if (!alt) {
          if (tG) act(1); else if (tJ) act(2); else if (tC) act(3); else if (tV) act(shift ? 741 : 740); else if (tZ) act(742);
          else if (tL) { bool any = false; for (int i = 0; i < s_nLights; i++) if (s_lights[i].on) any = true; act(shift ? 702 : any ? 701 : 700); }
          else if (t1) act(232); else if (t3) act(230); else if (t7) act(231); else if (t0) act(202); }
    }
    // ---------------- clic (al soltar, sobre lo mismo que se apreto) ----------------
    const bool lNow = front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0; const bool lEdge = lNow && !s_tlWas, lUp = !lNow && s_tlWas; s_tlWas = lNow;
    const bool inMenu = (hot >= 600 && hot < 606) || hot == 699 || (hot >= 1000 && hot < 1028);
    if (s_menuOpen >= 0 && hot >= 600 && hot < 606 && hot - 600 != s_menuOpen) { s_menuOpen = hot - 600; s_menuA = 0.0f; s_menuKb = -1; }      // con un menu abierto, pasar por otro titulo lo cambia
    if (lEdge) { s_tPress = hot; if (s_menuOpen >= 0 && !inMenu) { s_menuOpen = -1; s_tPress = -1; } }      // clic afuera: cierra el menu y no hace nada mas
    if (lUp && s_tPress >= 0 && s_tPress == hot) {
        if (hot >= 600 && hot < 606) { s_menuOpen = s_menuOpen == hot - 600 ? -1 : hot - 600; s_menuA = 0.0f; s_menuKb = -1; }
        else if (hot >= 1000 && hot < 1028) { const int a = hot - 1000 < s_miN ? s_miAct[hot - 1000] : -1; s_menuOpen = -1; if (a >= 0) act(a); }
        else if (hot != 699 && hot != 897) act(hot);
        static int nl = 0; if (nl++ < 60) Log("motor (editor): clic %d", hot);
    }
    if (!lNow) s_tPress = -1;
    s_hotPrev = hot; s_escOpen.store(s_menuOpen >= 0 || s_modal != 0, std::memory_order_relaxed);
}

// (px, py) en pixeles de la ventana del juego
bool MotorGizmoOver(float px, float py) {
    if (Now() - s_drawT.load(std::memory_order_relaxed) > 0.3) return false;
    if (s_dragging.load(std::memory_order_relaxed)) return true;
    if (MotorUiOn()) { const int n = s_hrN.load(); for (int i = 0; i < n && i < 6; i++) if (px >= s_hr[i][0].load() && px <= s_hr[i][2].load() && py >= s_hr[i][1].load() && py <= s_hr[i][3].load()) return true; }   // menu propio: volver, capsula, tarjeta
    const float dx = px - s_cx.load(), dy = py - s_cy.load(), r = s_rad.load() * 1.22f;
    if (dx * dx + dy * dy <= r * r) return true;
    return px >= s_bx0.load() && px <= s_bx1.load() && py >= s_by0.load() && py <= s_by1.load();
}

static void SetCam(bool on) {
    { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.live[344][0] = on ? 2.0f : 0.0f; }
    Log("motor (gizmo): camara %s", on ? "libre" : "del juego");
}

void MotorGizmoDraw() {
    if (MotorUiOn()) ToolbarDraw();
    if (g_cfg.motorGizmo == 0) { s_drawT.store(Now()); return; }
    ImDrawList* dl = ImGui::GetForegroundDrawList(); const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float k = std::max(1.0f, ds.y / 1080.0f), R = 54.0f * k, mg = 30.0f * k;
    const bool ui_ = MotorUiOn();      // (15/11 c) con el editor a la vista el gizmo va dentro de la vista 3D: a la izquierda del inspector y debajo de la barra
    const ImVec2 C(ds.x - mg - R - (ui_ ? kRightW * k : 0.0f), mg + R + (ui_ ? kBarH * k : 0.0f));
    // mouse (pixeles de la ventana, llevados al tamano del dibujo)
    float mx = -1.0e6f, my = -1.0e6f; bool front = false;
    { HWND h = OverlayHwnd(); POINT p{}; RECT rc{};
      if (h && GetForegroundWindow() == h && GetCursorPos(&p) && ScreenToClient(h, &p) && GetClientRect(h, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
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
    const float bs = 34.0f * k, gap = 8.0f * k; const float by = C.y + R + 16.0f * k, bx = C.x - bs - gap * 0.5f;
    const ImVec2 b0(bx, by), b1(bx + bs + gap, by);
    { const float sx = s_rad.load() / R; s_bx0.store(b0.x * sx); s_by0.store(b0.y * sx); s_bx1.store((b1.x + bs) * sx); s_by1.store((b1.y + bs) * sx); }
    auto inBtn = [&](const ImVec2& b) { return mx >= b.x && mx <= b.x + bs && my >= b.y && my <= b.y + bs; };
    const int hotBtn = inBtn(b0) ? 0 : inBtn(b1) ? 1 : -1;
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
    ImFont* font = ImGui::GetFont(); const float fs = 15.0f * k;
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
    const char* txt = camOn ? "Camara libre" : "Camara del juego";
    const float ft = 14.0f * k; const ImVec2 ts = font->CalcTextSizeA(ft, FLT_MAX, 0.0f, txt);
    const ImVec2 tp(C.x - ts.x * 0.5f, by + bs + 6.0f * k);
    dl->AddText(font, ft, ImVec2(tp.x + 1.0f, tp.y + 1.0f), IM_COL32(0, 0, 0, 200), txt);
    dl->AddText(font, ft, tp, IM_COL32(236, 239, 244, 230), txt);
}

} // namespace fx
