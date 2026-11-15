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
static const char* const kIconTip[4] = { "Volver", "Vista y render", "Jugador", "Grilla" };
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
// (15/11 b) Agustin 02:21 / 02:23: "no me gusta la forma de los menus", "quiero menus nuevos ... con formas nuevas, funciones
// nuevas". Forma nueva, sin nada del Editar: boton redondo Volver arriba a la izquierda, una CAPSULA flotante abajo al centro
// con Vista / Jugador / Grilla (icono + nombre) y, al tocar Vista, una TARJETA flotante encima de la capsula con las vistas del
// render como pastillas (se elige con un clic, sin listas ni flechas izquierda / derecha), Alambre y Pantalla partida como
// selectores partidos. La lista nativa del Editar ya no se muestra nunca (motorpage.cpp UiTick).
bool MotorGizmoOver(float px, float py);
static std::atomic<float> s_hr[3][4];                // zonas que tapan el clic: 0 volver, 1 capsula, 2 tarjeta (pixeles de la ventana)
static std::atomic<int> s_hrN{0};
static float s_cardA = 0.0f;                         // 0..1: aparicion de la tarjeta
static void ToolbarDraw() {
    if (!s_iconTried) IconsLoad();
    ImDrawList* dl = ImGui::GetForegroundDrawList(); const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float k = std::max(1.0f, ds.y / 1080.0f);
    float mx = -1.0e6f, my = -1.0e6f, sx = 1.0f, sy = 1.0f; bool front = false;
    { HWND h = OverlayHwnd(); POINT p{}; RECT rc{};
      if (h && GetForegroundWindow() == h && GetCursorPos(&p) && ScreenToClient(h, &p) && GetClientRect(h, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
          sx = (float)(rc.right - rc.left) / ds.x; sy = (float)(rc.bottom - rc.top) / ds.y; mx = (float)p.x / sx; my = (float)p.y / sy; front = true; } }
    ImFont* font = ImGui::GetFont();
    const ImU32 ink = IM_COL32(236, 239, 244, 255), dim = IM_COL32(150, 158, 172, 255), acc = IM_COL32(86, 156, 255, 255);
    auto in = [&](const ImVec2& a, const ImVec2& b) { return mx >= a.x && mx <= b.x && my >= a.y && my <= b.y; };
    int nz = 0;
    auto zone = [&](const ImVec2& a, const ImVec2& b) { if (nz < 3) { s_hr[nz][0].store(a.x * sx); s_hr[nz][1].store(a.y * sy); s_hr[nz][2].store(b.x * sx); s_hr[nz][3].store(b.y * sy); nz++; } };
    int hot = -1;                                    // 0 volver, 1..3 capsula, 100 + fila * 16 + opcion = pastilla de la tarjeta
    // --- Volver: boton redondo + titulo ---
    const float br = 23.0f * k; const ImVec2 bc(34.0f * k + br, 30.0f * k + br);
    const ImVec2 b0(bc.x - br, bc.y - br), b1(bc.x + br, bc.y + br); zone(b0, b1);
    if (in(b0, b1)) hot = 0;
    // --- capsula ---
    const float cw = 104.0f * k, ch = 70.0f * k, padx = 10.0f * k; const float W = cw * 3.0f + padx * 2.0f;
    const ImVec2 d0((ds.x - W) * 0.5f, ds.y - 34.0f * k - ch), d1(d0.x + W, d0.y + ch); zone(d0, d1);
    ImVec2 c0[4], c1[4];
    for (int i = 1; i < 4; i++) { c0[i] = ImVec2(d0.x + padx + (float)(i - 1) * cw, d0.y + 6.0f * k); c1[i] = ImVec2(c0[i].x + cw, d1.y - 6.0f * k); if (in(c0[i], c1[i])) hot = i; }
    // --- tarjeta de vistas ---
    const bool open = MotorUiPanel();
    { const float dt = std::min(0.05f, ImGui::GetIO().DeltaTime); s_cardA = open ? std::min(1.0f, s_cardA + dt * 9.0f) : std::max(0.0f, s_cardA - dt * 12.0f); }
    struct Chip { ImVec2 a, b; int row, op; }; Chip chips[24]; int nchip = 0;
    const float cardW = 620.0f * k, cpad = 22.0f * k, chipH = 36.0f * k, cgap = 8.0f * k, secH = 30.0f * k;
    const int nV = MotorViewCount(0), nA = MotorViewCount(1), nC = MotorViewCount(2), cols = 3, rowsV = (nV + cols - 1) / cols;
    const float cardH = cpad + secH + (float)rowsV * (chipH + cgap) + 8.0f * k + secH + chipH + 14.0f * k + secH + chipH + cpad;
    const float rise = (1.0f - s_cardA) * 14.0f * k;
    const ImVec2 k0((ds.x - cardW) * 0.5f, d0.y - 18.0f * k - cardH + rise), k1(k0.x + cardW, k0.y + cardH);
    float yV = 0, yA = 0, yC = 0;
    if (s_cardA > 0.0f) {
        if (open) zone(k0, k1);
        float y = k0.y + cpad; yV = y; y += secH;
        const float cwv = (cardW - cpad * 2.0f - cgap * (float)(cols - 1)) / (float)cols;
        for (int i = 0; i < nV && nchip < 24; i++) { const int cx = i % cols, cy = i / cols; const ImVec2 a(k0.x + cpad + (float)cx * (cwv + cgap), y + (float)cy * (chipH + cgap)); chips[nchip++] = { a, ImVec2(a.x + cwv, a.y + chipH), 0, i }; }
        y += (float)rowsV * (chipH + cgap) + 8.0f * k; yA = y; y += secH;
        for (int r = 1; r <= 2; r++) { const int n = r == 1 ? nA : nC; const float w = (cardW - cpad * 2.0f) / (float)std::max(1, n);
            for (int i = 0; i < n && nchip < 24; i++) { const ImVec2 a(k0.x + cpad + (float)i * w, y); chips[nchip++] = { a, ImVec2(a.x + w, a.y + chipH), r, i }; }
            if (r == 1) { y += chipH + 14.0f * k; yC = y; y += secH; } }
        if (open) for (int i = 0; i < nchip; i++) if (in(chips[i].a, chips[i].b)) hot = 100 + chips[i].row * 16 + chips[i].op;
    }
    s_hrN.store(nz);
    // --- clic (al soltar, sobre lo mismo que se apreto) ---
    const bool lNow = front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0; const bool lEdge = lNow && !s_tlWas, lUp = !lNow && s_tlWas; s_tlWas = lNow;
    if (lEdge) s_tPress = hot;
    if (lUp) {
        if (s_tPress >= 0 && s_tPress == hot) {
            if (hot == 0) MotorUiBack();
            else if (hot == 1) MotorUiTogglePanel();
            else if (hot == 2) MotorUiTogglePlayer();
            else if (hot == 3) g_cfg.motorGrid = g_cfg.motorGrid ? 0 : 1;
            else if (hot >= 100) MotorViewSet((hot - 100) / 16, (hot - 100) % 16);
            if (hot < 100) Log("motor (menu): clic en %s", kIconTip[hot]);
        } else if (s_tPress < 0 && hot < 0 && open && !MotorGizmoOver(mx * sx, my * sy)) MotorUiTogglePanel();      // clic afuera: cierra la tarjeta
        s_tPress = -1;
    }
    if (!lNow) s_tPress = -1;
    // --- dibujo: Volver ---
    dl->AddCircleFilled(bc, br, IM_COL32(20, 23, 30, hot == 0 ? 245 : 215), 40);
    dl->AddCircle(bc, br, IM_COL32(255, 255, 255, hot == 0 ? 150 : 40), 40, 1.2f * k);
    if (s_icon[0]) { const float h2 = br * 0.62f; dl->AddImage((ImTextureID)(uintptr_t)s_icon[0], ImVec2(bc.x - h2, bc.y - h2), ImVec2(bc.x + h2, bc.y + h2), ImVec2(0, 0), ImVec2(1, 1), ink); }
    else IconFallback(dl, 0, bc, br * 2.0f, ink, k);
    {   const char* t = "Motor en tiempo real"; const float fs = 21.0f * k; const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, t);
        const ImVec2 tp(b1.x + 14.0f * k, bc.y - ts.y * 0.5f);
        dl->AddText(font, fs, ImVec2(tp.x + 1.0f, tp.y + 1.0f), IM_COL32(0, 0, 0, 170), t); dl->AddText(font, fs, tp, IM_COL32(236, 239, 244, 235), t); }
    // --- dibujo: capsula ---
    dl->AddRectFilled(ImVec2(d0.x, d0.y + 3.0f * k), ImVec2(d1.x, d1.y + 5.0f * k), IM_COL32(0, 0, 0, 70), ch * 0.5f);
    dl->AddRectFilled(d0, d1, IM_COL32(20, 23, 30, 232), ch * 0.5f);
    dl->AddRect(d0, d1, IM_COL32(255, 255, 255, 34), ch * 0.5f, 0, 1.2f * k);
    const bool act[4] = { false, open, MotorUiPlayer(), g_cfg.motorGrid != 0 };
    for (int i = 1; i < 4; i++) {
        const bool hov = hot == i, down = hov && s_tPress == i; const float rr = (c1[i].y - c0[i].y) * 0.5f;
        if (hov || act[i]) dl->AddRectFilled(c0[i], c1[i], act[i] ? IM_COL32(86, 156, 255, down ? 70 : hov ? 56 : 40) : IM_COL32(255, 255, 255, down ? 34 : 20), rr);
        const ImU32 col = act[i] ? acc : hov ? ink : IM_COL32(206, 212, 222, 255);
        const ImVec2 o((c0[i].x + c1[i].x) * 0.5f, c0[i].y + 22.0f * k); const float h2 = 13.0f * k;
        if (s_icon[i]) dl->AddImage((ImTextureID)(uintptr_t)s_icon[i], ImVec2(o.x - h2, o.y - h2), ImVec2(o.x + h2, o.y + h2), ImVec2(0, 0), ImVec2(1, 1), col);
        else IconFallback(dl, i, o, 44.0f * k, col, k);
        const char* t = i == 1 ? "Vista" : kIconTip[i]; const float fs = 13.5f * k; const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, t);
        dl->AddText(font, fs, ImVec2(o.x - ts.x * 0.5f, c1[i].y - ts.y - 7.0f * k), col, t);
    }
    // --- dibujo: tarjeta ---
    if (s_cardA > 0.0f) {
        const float A = s_cardA; auto al = [&](ImU32 c, float m = 1.0f) { const int a = (int)((float)(c >> 24) * A * m); return (c & 0x00ffffffu) | ((ImU32)a << 24); };
        const float rad = 20.0f * k;
        dl->AddRectFilled(ImVec2(k0.x, k0.y + 5.0f * k), ImVec2(k1.x, k1.y + 8.0f * k), al(IM_COL32(0, 0, 0, 80)), rad);
        dl->AddRectFilled(k0, k1, al(IM_COL32(20, 23, 30, 240)), rad);
        dl->AddRect(k0, k1, al(IM_COL32(255, 255, 255, 34)), rad, 0, 1.2f * k);
        { const float tx = (c0[1].x + c1[1].x) * 0.5f, tw = 10.0f * k; dl->AddTriangleFilled(ImVec2(tx - tw, k1.y - 1.0f), ImVec2(tx + tw, k1.y - 1.0f), ImVec2(tx, k1.y + tw), al(IM_COL32(20, 23, 30, 240))); }
        const float fsS = 12.5f * k, fsC = 15.0f * k;
        const float ys[3] = { yV, yA, yC };
        for (int r = 0; r < 3; r++) { char up[40]; const char* nm = MotorViewName(r, -1); int q = 0; for (; nm[q] && q < 38; q++) up[q] = (char)toupper((unsigned char)nm[q]); up[q] = 0;
            dl->AddText(font, fsS, ImVec2(k0.x + cpad + 2.0f * k, ys[r] + 6.0f * k), al(dim), up); }
        // fondo comun de los selectores partidos
        for (int r = 1; r <= 2; r++) { ImVec2 a(0, 0), b(0, 0); bool any = false; for (int i = 0; i < nchip; i++) if (chips[i].row == r) { if (!any) { a = chips[i].a; any = true; } b = chips[i].b; }
            if (any) dl->AddRectFilled(a, b, al(IM_COL32(255, 255, 255, 14)), chipH * 0.5f); }
        const int cur[3] = { MotorViewGet(0), MotorViewGet(1), MotorViewGet(2) };
        for (int i = 0; i < nchip; i++) {
            const Chip& c = chips[i]; const int id = 100 + c.row * 16 + c.op; const bool on = cur[c.row] == c.op, hov = hot == id, down = hov && s_tPress == id;
            const ImVec2 a(c.a.x + (c.row ? 3.0f * k : 0.0f), c.a.y + (c.row ? 3.0f * k : 0.0f)), b(c.b.x - (c.row ? 3.0f * k : 0.0f), c.b.y - (c.row ? 3.0f * k : 0.0f)); const float rr = (b.y - a.y) * 0.5f;
            if (on) dl->AddRectFilled(a, b, al(IM_COL32(86, 156, 255, down ? 255 : 235)), rr);
            else if (c.row == 0) { dl->AddRectFilled(a, b, al(IM_COL32(255, 255, 255, down ? 40 : hov ? 28 : 12)), rr); if (hov) dl->AddRect(a, b, al(IM_COL32(255, 255, 255, 70)), rr, 0, 1.0f * k); }
            else if (hov) dl->AddRectFilled(a, b, al(IM_COL32(255, 255, 255, down ? 40 : 24)), rr);
            const char* t = MotorViewName(c.row, c.op); const ImVec2 ts = font->CalcTextSizeA(fsC, FLT_MAX, 0.0f, t);
            dl->AddText(font, fsC, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), al(on ? IM_COL32(255, 255, 255, 255) : hov ? ink : IM_COL32(206, 212, 222, 255)), t);
        }
    }
}

// (px, py) en pixeles de la ventana del juego
bool MotorGizmoOver(float px, float py) {
    if (Now() - s_drawT.load(std::memory_order_relaxed) > 0.3) return false;
    if (s_dragging.load(std::memory_order_relaxed)) return true;
    if (MotorUiOn()) { const int n = s_hrN.load(); for (int i = 0; i < n && i < 3; i++) if (px >= s_hr[i][0].load() && px <= s_hr[i][2].load() && py >= s_hr[i][1].load() && py <= s_hr[i][3].load()) return true; }   // menu propio: volver, capsula, tarjeta
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
    const ImVec2 C(ds.x - mg - R, mg + R);
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
