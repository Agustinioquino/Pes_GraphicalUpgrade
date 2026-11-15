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
static std::atomic<float> s_hr[4][4];                // zonas que tapan el clic (pixeles de la ventana): barra, escena, inspector, estado
static std::atomic<int> s_hrN{0};
static float s_openA = 0.0f, s_selY = -1.0f, s_inspA = 1.0f; static double s_uiT = -1.0e9; static int s_selWas = 0;   // (15/11 d) animaciones
static MotorLight s_lights[64]; static int s_nLights = 0; static MotorSceneInfo s_info{}; static double s_sceneT = -1.0e9; static uintptr_t s_selObj = 0;   // (15/11 e) luces de la escena
static int s_sel = 0, s_hotPrev = -1;                // fila marcada del arbol (0 render, 1 camara, 2 grilla, 3 jugador)
static const float kBarH = 38.0f, kLeftW = 250.0f, kRightW = 320.0f, kStatH = 26.0f;
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
      if (now - s_uiT > 0.4) { s_openA = 0.0f; s_selY = -1.0f; s_inspA = 1.0f; }
      s_uiT = now; s_openA = std::min(1.0f, s_openA + dt / 0.28f); s_inspA = std::min(1.0f, s_inspA + dt / 0.16f);
      if (s_sel != s_selWas) { s_selWas = s_sel; s_inspA = 0.0f; } }
    const float eo = 1.0f - (1.0f - s_openA) * (1.0f - s_openA) * (1.0f - s_openA);      // salida suave
    // (15/11 e, Agustin 02:41: "que este motor en la pantalla pueda leer todo tipos de luces. y sistemas del juego") 4 veces por
    // segundo se releen las luces y el conteo de objetos de la escena grafica (motorscene.cpp); la marcada se sigue por su direccion
    if (Now() - s_sceneT > 0.25) { s_sceneT = Now();
        static MotorLight tmp[64]; MotorSceneInfo si{}; const int n = MotorSceneLights(tmp, 64, &si);
        if (n >= 0) { memcpy(s_lights, tmp, sizeof(MotorLight) * (size_t)n); s_nLights = n; s_info = si; }
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
    auto zone = [&](const ImVec2& a, const ImVec2& b) { if (nz < 4) { s_hr[nz][0].store(a.x * sx); s_hr[nz][1].store(a.y * sy); s_hr[nz][2].store(b.x * sx); s_hr[nz][3].store(b.y * sy); nz++; } };
    const bool camFree = g_cfg.live[344][0] > 0.5f, grid = g_cfg.motorGrid != 0, player = MotorUiPlayer();
    // selector partido: n opciones en una caja; devuelve nada, anota hot = id0 + i
    auto seg = [&](int id0, const ImVec2& a, const ImVec2& b, int n, int cur, const char* const* names) {
        dl->AddRectFilled(a, b, cField, 3.0f * k); const float w = (b.x - a.x) / (float)std::max(1, n);
        for (int i = 0; i < n; i++) { const ImVec2 p0(a.x + (float)i * w, a.y), p1(a.x + (float)(i + 1) * w, b.y); const bool hv = hit(id0 + i, p0, p1);
            if (i == cur) dl->AddRectFilled(ImVec2(p0.x + 1.0f, p0.y + 1.0f), ImVec2(p1.x - 1.0f, p1.y - 1.0f), acc, 2.0f * k); else if (hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 22), 2.0f * k);
            textC(p0, p1, i == cur ? IM_COL32(20, 20, 20, 255) : hv ? ink : IM_COL32(190, 190, 192, 255), names[i], fsS + 1.0f * k); }
        dl->AddRect(a, b, cLine, 3.0f * k);
    };
    // ---------------- barra de herramientas ----------------
    { const int v0 = dl->VtxBuffer.Size; const ImVec2 a(0, 0), b(ds.x, barH); zone(a, b);
      dl->AddRectFilled(a, b, cHead); dl->AddLine(ImVec2(0, barH), ImVec2(ds.x, barH), cLine, 1.0f);
      float x = 0.0f;
      { const ImVec2 p0(x, 0), p1(x + barH * 1.15f, barH); const bool hv = hit(0, p0, p1); if (hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 26));
        const ImVec2 o((p0.x + p1.x) * 0.5f, barH * 0.5f); const float h2 = barH * 0.27f;
        if (s_icon[0]) dl->AddImage((ImTextureID)(uintptr_t)s_icon[0], ImVec2(o.x - h2, o.y - h2), ImVec2(o.x + h2, o.y + h2), ImVec2(0, 0), ImVec2(1, 1), ink); else IconFallback(dl, 0, o, barH * 0.95f, ink, k);
        x = p1.x; dl->AddLine(ImVec2(x, 6.0f * k), ImVec2(x, barH - 6.0f * k), IM_COL32(80, 80, 84, 255), 1.0f); }
      x += 14.0f * k; x += text(x, barH * 0.5f, acc, "FOX ENGINE", fs) + 10.0f * k; x += text(x, barH * 0.5f, ink, "Motor en tiempo real", fs) + 22.0f * k;
      dl->AddLine(ImVec2(x, 6.0f * k), ImVec2(x, barH - 6.0f * k), IM_COL32(80, 80, 84, 255), 1.0f); x += 8.0f * k;
      const char* const tn[3] = { "Grilla", "Jugador", "C\xC3\xA1mara libre" }; const bool on[3] = { grid, player, camFree }; const int ic[3] = { 3, 2, 1 };
      for (int i = 0; i < 3; i++) { const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, tn[i]); const float w = ts.x + 46.0f * k;
          const ImVec2 p0(x, 5.0f * k), p1(x + w, barH - 5.0f * k); const bool hv = hit(1 + i, p0, p1);
          if (on[i]) dl->AddRectFilled(p0, p1, IM_COL32(240, 138, 36, 48), 3.0f * k); else if (hv) dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 24), 3.0f * k);
          if (on[i]) dl->AddRectFilled(ImVec2(p0.x, p1.y - 2.0f * k), p1, acc);
          const ImVec2 o(p0.x + 17.0f * k, barH * 0.5f); const float h2 = 8.5f * k; const ImU32 c = on[i] ? IM_COL32(255, 190, 120, 255) : ink;
          if (s_icon[ic[i]]) dl->AddImage((ImTextureID)(uintptr_t)s_icon[ic[i]], ImVec2(o.x - h2, o.y - h2), ImVec2(o.x + h2, o.y + h2), ImVec2(0, 0), ImVec2(1, 1), c); else IconFallback(dl, ic[i], o, 28.0f * k, c, k);
          text(p0.x + 32.0f * k, barH * 0.5f, c, tn[i], fs); x = p1.x + 4.0f * k; }
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
      char st[160]; snprintf(st, sizeof st, "Vista: %s    Alambre: %s    C\xC3\xA1mara: %s    Objetos: %d    Luces: %d", MotorViewName(0, MotorViewGet(0)), MotorViewName(1, MotorViewGet(1)), camFree ? "libre" : "del juego", s_info.objects, s_info.lights);
      text(12.0f * k, y1 + statH * 0.5f, dim, st, fsS);
      char fp[32]; snprintf(fp, sizeof fp, "%.0f fps", ImGui::GetIO().Framerate); const ImVec2 ts = font->CalcTextSizeA(fsS, FLT_MAX, 0.0f, fp); text(ds.x - ts.x - 12.0f * k, y1 + statH * 0.5f, dim, fp, fsS);
      anim(v0, 0.0f, (1.0f - eo) * statH, eo); }
    s_hrN.store(nz);
    // ---------------- clic (al soltar, sobre lo mismo que se apreto) ----------------
    const bool lNow = front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0; const bool lEdge = lNow && !s_tlWas, lUp = !lNow && s_tlWas; s_tlWas = lNow;
    if (lEdge) s_tPress = hot;
    if (lUp && s_tPress >= 0 && s_tPress == hot) {
        if (hot == 0) MotorUiBack();
        else if (hot == 1 || hot == 20) g_cfg.motorGrid = grid ? 0 : 1;
        else if (hot == 2 || hot == 21 || hot == 220) MotorUiTogglePlayer();
        else if (hot == 3) SetCam(!camFree);
        else if (hot >= 10 && hot < 14) { s_sel = hot - 10; s_selObj = 0; }
        else if (hot >= 300 && hot < 364 && hot - 300 < s_nLights) { s_sel = 4 + (hot - 300); s_selObj = s_lights[hot - 300].obj; }
        else if (hot >= 400 && hot < 464 && hot - 400 < s_nLights) { MotorLight& L = s_lights[hot - 400]; if (L.writable && MotorLightSetOn(L, !L.on)) L.on = !L.on; }
        else if ((hot == 250 || hot == 251) && s_sel >= 4 && s_sel - 4 < s_nLights) { MotorLight& L = s_lights[s_sel - 4]; if (L.writable && MotorLightSetOn(L, hot == 250)) L.on = hot == 250; }
        else if (hot >= 100 && hot < 148) MotorViewSet((hot - 100) / 16, (hot - 100) % 16);
        else if (hot == 200 || hot == 201) SetCam(hot == 200);
        else if (hot == 202) FreeCamGizmoHome();
        else if (hot >= 230 && hot < 236) { static const int kAx[6] = { 0, 2, 4, 1, 3, 5 }; FreeCamGizmoAxis(kAx[hot - 230]); }   // +X, +Y, +Z / -X, -Y, -Z
        else if (hot == 210 || hot == 211) g_cfg.motorGrid = hot == 210 ? 1 : 0;
        static int nl = 0; if (nl++ < 60) Log("motor (editor): clic %d", hot);
    }
    if (!lNow) s_tPress = -1;
    s_hotPrev = hot;
}

// (px, py) en pixeles de la ventana del juego
bool MotorGizmoOver(float px, float py) {
    if (Now() - s_drawT.load(std::memory_order_relaxed) > 0.3) return false;
    if (s_dragging.load(std::memory_order_relaxed)) return true;
    if (MotorUiOn()) { const int n = s_hrN.load(); for (int i = 0; i < n && i < 4; i++) if (px >= s_hr[i][0].load() && px <= s_hr[i][2].load() && py >= s_hr[i][1].load() && py <= s_hr[i][3].load()) return true; }   // menu propio: volver, capsula, tarjeta
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
