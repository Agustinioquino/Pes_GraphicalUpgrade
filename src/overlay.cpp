// PES21 FX - menu en pantalla (Dear ImGui). Tecla Insert (o menu_key en [PES21FX] de PES21FX.ini).
#include "core.h"
#include <set>
#include <cmath>
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>   // (04/11 n) TempInputIsActive: el slider nuevo muestra el numero de ImGui solo al escribirlo con Ctrl+clic
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <DDSTextureLoader.h>
#include <psapi.h>
#include <cstring>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace fx {

std::atomic<bool> g_menuOpen{false};
std::atomic<bool> g_menuTyping{false};   // (31/10) campo de texto del menu activo (freecam.cpp)
static bool s_init = false;
static HWND s_hwnd = nullptr;
static WNDPROC s_oldProc = nullptr;
}
void* fx::g_overlayOldProc = nullptr;
namespace fx {
static ID3D11RenderTargetView* s_rtv = nullptr;
static bool s_keyWas = false;
static std::string s_status;
static std::string s_btnPending;   // accion de un "button =" del ini (se ejecuta fuera de g_cfgMutex)
static ImFont* s_fontBig = nullptr; static ImFont* s_fontBold = nullptr;
static ID3D11ShaderResourceView* s_logo = nullptr; static float s_logoAspect = 1.0f;
// (04/11) el menu se adapta a la resolucion (pedido de Agustin): letras, espacios y anchos crecen con el alto de la pantalla (1080 = 1)
// y las letras base son mas grandes (19 -> 22 px a 1080). ui_scale queda como ajuste fino encima. Las fuentes se rasterizan al tamano
// final (nitidas) y se rehacen si cambia la resolucion
static constexpr float kFontUp = 1.15f;
static float s_fontsH = 0.0f;
static ImGuiStyle s_baseStyle;
static bool s_iconsOk = false;   // (04/11 n) textures\pes_icons.ttf cargada
static float UIK() { return ImGui::GetFontSize() / 19.0f; }   // escala de los anchos fijos del menu (pensados para 19 px)
static void BuildFonts(float H) {
    ImGuiIO& io = ImGui::GetIO();
    const float k = std::clamp(H / 1080.0f, 0.75f, 3.0f) * kFontUp;
    io.Fonts->Clear();
    {   // (08/11) letra del menu: Saira Semi Condensed (OFL, textures\pes_ui*.ttf), condensada y deportiva como la de PES 6
        // (Agustin: "cambia la fuente por una mas estetica y similar a la del pes 6"); si no esta, Segoe UI como antes
        int len = WideCharToMultiByte(CP_UTF8, 0, g_cfg.baseDir.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string dir(len > 0 ? len - 1 : 0, '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, g_cfg.baseDir.c_str(), -1, &dir[0], len, nullptr, nullptr);
        const bool ui = g_cfg.menuFont && GetFileAttributesW((g_cfg.baseDir + L"textures\\pes_ui.ttf").c_str()) != INVALID_FILE_ATTRIBUTES
                     && GetFileAttributesW((g_cfg.baseDir + L"textures\\pes_ui_bold.ttf").c_str()) != INVALID_FILE_ATTRIBUTES;
        const std::string reg = ui ? dir + "textures\\pes_ui.ttf" : std::string("C:\\Windows\\Fonts\\segoeui.ttf");
        const std::string bold = ui ? dir + "textures\\pes_ui_bold.ttf" : std::string("C:\\Windows\\Fonts\\segoeuib.ttf");
        const float up = ui ? 1.12f : 1.0f;   // Saira es un poco mas chica a igual tamano
        if (!io.Fonts->AddFontFromFileTTF(reg.c_str(), std::round(19.0f * k * up), nullptr, io.Fonts->GetGlyphRangesDefault())) io.Fonts->AddFontDefault();
        s_fontBig = io.Fonts->AddFontFromFileTTF(bold.c_str(), std::round(27.0f * k * up), nullptr, io.Fonts->GetGlyphRangesDefault());
        s_fontBold = io.Fonts->AddFontFromFileTTF(bold.c_str(), std::round((ui ? 20.5f : 19.0f) * k * up), nullptr, io.Fonts->GetGlyphRangesDefault());
        Log("menu: letra %s", ui ? "Saira Semi Condensed (textures\\pes_ui.ttf)" : "Segoe UI");
    }
    {   // (04/11 n) iconos del menu (Font Awesome Free, recortado): se mezclan en la letra normal y en la negrita
        static const ImWchar kIcoRanges[] = { 0xe000, 0xf8ff, 0 };
        int len = WideCharToMultiByte(CP_UTF8, 0, g_cfg.baseDir.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string dir(len > 0 ? len - 1 : 0, '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, g_cfg.baseDir.c_str(), -1, &dir[0], len, nullptr, nullptr);
        const std::string path = dir + "textures\\pes_icons.ttf";
        s_iconsOk = GetFileAttributesW((g_cfg.baseDir + L"textures\\pes_icons.ttf").c_str()) != INVALID_FILE_ATTRIBUTES;
        if (s_iconsOk) {
            ImFont* fonts[3] = { io.Fonts->Fonts.Size ? io.Fonts->Fonts[0] : nullptr, s_fontBold, s_fontBig };   // (08/11) + la grande: el icono del titulo de la pagina salia como '?'
            for (ImFont* f : fonts) {
                if (!f) continue;
                ImFontConfig ic; ic.MergeMode = true; ic.PixelSnapH = true; ic.DstFont = f;
                const float px = std::round((f == s_fontBig ? 22.0f : 16.0f) * k);
                ic.GlyphMinAdvanceX = px * 1.15f; ic.GlyphOffset = ImVec2(0, std::round(1.0f * k));
                if (!io.Fonts->AddFontFromFileTTF(path.c_str(), px, &ic, kIcoRanges)) s_iconsOk = false;
            }
        }
        Log("menu: iconos %s", s_iconsOk ? "ok (textures\\pes_icons.ttf)" : "no encontrados (textures\\pes_icons.ttf): el menu va sin iconos");
    }
    ImGui::GetStyle() = s_baseStyle;
    ImGui::GetStyle().ScaleAllSizes(k);
    s_fontsH = H;
    Log("menu: letras para %.0f px de alto (x%.2f)", H, k);
}


// ---------------------------------------------------------------- (11/11) ultima tecla apretada
// Agustin: un cuadrito semitransparente en una esquina con la ultima tecla del teclado. [PES21FX] ultima_tecla = 0 apagado,
// 1 arriba izquierda, 2 arriba derecha, 3 abajo izquierda, 4 abajo derecha. Solo mira el teclado (GetAsyncKeyState) con la
// ventana del juego al frente; no toca lo que recibe el juego.
// (22:48, Agustin: "me referia que aparezca solo en el motor") solo se ve con el Motor en tiempo real abierto: su estudio 3D
// (MotorPageActive, motorpage.cpp) o su panel (MotorPanelOpen). Fuera del Motor no se mira el teclado ni se dibuja nada.
static int s_khVk = 0, s_khN = 0; static double s_khT = -1.0e9; static unsigned char s_khWas[256] = {};
static const char* KeyHudName(int vk, char* buf, size_t n) {
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) { snprintf(buf, n, "%c", (char)vk); return buf; }
    if (vk >= VK_F1 && vk <= VK_F24) { snprintf(buf, n, "F%d", vk - VK_F1 + 1); return buf; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { snprintf(buf, n, "Num %d", vk - VK_NUMPAD0); return buf; }
    switch (vk) {
    case VK_RETURN: return "Enter"; case VK_ESCAPE: return "Esc"; case VK_SPACE: return "Espacio"; case VK_TAB: return "Tab";
    case VK_BACK: return "Retroceso"; case VK_DELETE: return "Supr"; case VK_INSERT: return "Insert"; case VK_HOME: return "Inicio";
    case VK_END: return "Fin"; case VK_PRIOR: return "Re Pag"; case VK_NEXT: return "Av Pag"; case VK_UP: return "Flecha arriba";
    case VK_DOWN: return "Flecha abajo"; case VK_LEFT: return "Flecha izquierda"; case VK_RIGHT: return "Flecha derecha";
    case VK_SHIFT: return "Shift"; case VK_CONTROL: return "Ctrl"; case VK_MENU: return "Alt"; case VK_CAPITAL: return "Bloq Mayus";
    case VK_PAUSE: return "Pausa"; case VK_SNAPSHOT: return "Impr Pant"; case VK_LWIN: case VK_RWIN: return "Windows";
    case VK_ADD: return "Num +"; case VK_SUBTRACT: return "Num -"; case VK_MULTIPLY: return "Num *"; case VK_DIVIDE: return "Num /"; case VK_DECIMAL: return "Num .";
    }
    const UINT sc = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    if (sc && GetKeyNameTextA((LONG)(sc << 16), buf, (int)n) > 0) return buf;
    snprintf(buf, n, "Tecla 0x%02X", vk); return buf;
}
// una vez por cuadro: anota la tecla recien apretada. Devuelve si hay que dibujar el cuadrito
static bool KeyHudPoll() {
    if (g_cfg.keyHud <= 0) return false;
    if (!MotorPageActive() && !MotorPanelOpen()) { if (s_khVk) { s_khVk = 0; s_khN = 0; memset(s_khWas, 0, sizeof s_khWas); } return false; }
    if (s_hwnd && GetForegroundWindow() == s_hwnd) {
        for (int vk = 8; vk < 255; vk++) {
            if (vk >= VK_LSHIFT && vk <= VK_RMENU) continue;            // ya entran como Shift / Ctrl / Alt
            const unsigned char d = (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
            if (d && !s_khWas[vk]) { s_khN = (vk == s_khVk && Now() - s_khT < 1.5) ? s_khN + 1 : 1; s_khVk = vk; s_khT = Now(); }
            s_khWas[vk] = d;
        }
    } else memset(s_khWas, 0, sizeof s_khWas);
    return s_khVk != 0;
}
// (11/11, Agustin 22:29: "que muestre un icono de la tecla") la tecla se dibuja como una tecla de teclado: base oscura, cara clara
// con brillo arriba y el nombre adentro; las flechas van con su triangulo y la barra espaciadora ancha.
static void KeyHudDraw() {
    char nb[64]; const char* nm = KeyHudName(s_khVk, nb, sizeof nb);
    const int arrow = s_khVk == VK_UP ? 1 : s_khVk == VK_DOWN ? 2 : s_khVk == VK_LEFT ? 3 : s_khVk == VK_RIGHT ? 4 : 0;
    ImDrawList* dl = ImGui::GetForegroundDrawList(); const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float k = std::max(1.0f, ds.y / 1080.0f), fs = 24.0f * k, mg = 22.0f * k, pad = 8.0f * k;
    ImFont* f = ImGui::GetFont(); const ImVec2 ts = arrow ? ImVec2(fs, fs) : f->CalcTextSizeA(fs, FLT_MAX, 0.0f, nm);
    const float kh = 50.0f * k;                                                                   // alto de la tecla
    float kw = std::max(kh, ts.x + 26.0f * k); if (s_khVk == VK_SPACE) kw = std::max(kw, 170.0f * k);   // ancho: cuadrada si es una letra
    char rep[16] = ""; if (s_khN > 1) snprintf(rep, sizeof rep, "x%d", s_khN);
    const float fr = 17.0f * k; const ImVec2 rs = rep[0] ? f->CalcTextSizeA(fr, FLT_MAX, 0.0f, rep) : ImVec2(0, 0);
    const float w = pad * 2.0f + kw + (rep[0] ? rs.x + 8.0f * k : 0.0f), h = pad * 2.0f + kh; const int c = std::clamp(g_cfg.keyHud, 1, 4);
    const float x = (c == 1 || c == 3) ? mg : ds.x - mg - w, y = (c <= 2) ? mg : ds.y - mg - h;
    const double age = Now() - s_khT; const float a = age < 1.2 ? 1.0f : (age < 2.0 ? 1.0f - 0.5f * (float)((age - 1.2) / 0.8) : 0.5f);   // recien apretada se ve fuerte; despues queda tenue
    const float push = age < 0.12 ? 2.5f * k : 0.0f;                                              // recien apretada: la tecla baja un poco
    auto A = [a](int v) { return (int)((float)v * a); };
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(8, 12, 20, A(120)), 9.0f * k);                       // fondo semitransparente
    const float kx = x + pad, ky = y + pad, r = 7.0f * k, lip = 6.0f * k;                         // lip = canto de abajo de la tecla
    dl->AddRectFilled(ImVec2(kx, ky + push), ImVec2(kx + kw, ky + kh), IM_COL32(38, 42, 52, A(235)), r);                // base (canto)
    const ImVec2 f0(kx + 2.0f * k, ky + push + 1.0f * k), f1(kx + kw - 2.0f * k, ky + kh - lip + push * 0.6f);         // cara de arriba
    dl->AddRectFilled(f0, f1, IM_COL32(236, 239, 244, A(240)), r - 1.0f * k);
    dl->AddRectFilled(f0, ImVec2(f1.x, f0.y + (f1.y - f0.y) * 0.45f), IM_COL32(255, 255, 255, A(70)), r - 1.0f * k);    // brillo
    dl->AddRect(ImVec2(kx, ky + push), ImVec2(kx + kw, ky + kh), IM_COL32(0, 0, 0, A(170)), r, 0, 1.2f * k);
    const ImVec2 ctr((f0.x + f1.x) * 0.5f, (f0.y + f1.y) * 0.5f); const ImU32 ink = IM_COL32(22, 26, 34, A(255));
    if (arrow) {
        const float q = 9.0f * k; ImVec2 p0, p1, p2;
        if (arrow == 1) { p0 = ImVec2(ctr.x, ctr.y - q); p1 = ImVec2(ctr.x - q, ctr.y + q * 0.7f); p2 = ImVec2(ctr.x + q, ctr.y + q * 0.7f); }
        else if (arrow == 2) { p0 = ImVec2(ctr.x, ctr.y + q); p1 = ImVec2(ctr.x + q, ctr.y - q * 0.7f); p2 = ImVec2(ctr.x - q, ctr.y - q * 0.7f); }
        else if (arrow == 3) { p0 = ImVec2(ctr.x - q, ctr.y); p1 = ImVec2(ctr.x + q * 0.7f, ctr.y + q); p2 = ImVec2(ctr.x + q * 0.7f, ctr.y - q); }
        else { p0 = ImVec2(ctr.x + q, ctr.y); p1 = ImVec2(ctr.x - q * 0.7f, ctr.y - q); p2 = ImVec2(ctr.x - q * 0.7f, ctr.y + q); }
        dl->AddTriangleFilled(p0, p1, p2, ink);
    } else if (s_khVk == VK_SPACE) {
        dl->AddRectFilled(ImVec2(ctr.x - kw * 0.28f, ctr.y + 5.0f * k), ImVec2(ctr.x + kw * 0.28f, ctr.y + 8.0f * k), ink, 1.5f * k);   // la rayita de la barra
        const ImVec2 t2 = f->CalcTextSizeA(fr, FLT_MAX, 0.0f, nm);
        dl->AddText(f, fr, ImVec2(ctr.x - t2.x * 0.5f, ctr.y - t2.y * 0.5f - 6.0f * k), ink, nm);
    } else dl->AddText(f, fs, ImVec2(ctr.x - ts.x * 0.5f, ctr.y - ts.y * 0.5f), ink, nm);
    if (rep[0]) dl->AddText(f, fr, ImVec2(kx + kw + 8.0f * k, y + (h - rs.y) * 0.5f), IM_COL32(255, 255, 255, A(230)), rep);
}

HWND OverlayHwnd() { return s_hwnd; }   // (11/11) menumouse.cpp
void OverlayResize() { if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; } }

static LRESULT CALLBACK MenuProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // (1/11) rueda del mouse para el zoom de la camara libre (mensaje normal o raw input, por si el juego pide NOLEGACY)
    // (11/11) sin camara libre la rueda va a los menus nativos del juego (menumouse.cpp: arriba / abajo)
    if (!g_menuOpen) {
        int wd = 0;
        if (m == WM_MOUSEWHEEL) wd = (int)GET_WHEEL_DELTA_WPARAM(w);
        else if (m == WM_INPUT) {
            RAWINPUT ri{}; UINT sz = sizeof(ri);
            if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 && ri.header.dwType == RIM_TYPEMOUSE &&
                (ri.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)) wd = (int)(short)ri.data.mouse.usButtonData;
        }
        if (wd != 0) { if (FreeCamActive()) FreeCamWheel(wd); else MenuMouseWheel(wd); }
    }
    if ((g_menuOpen || g_inspectOn) && s_init) {
        ImGui_ImplWin32_WndProcHandler(h, m, w, l);
        ImGuiIO& io = ImGui::GetIO();
        bool mouse = m >= WM_MOUSEFIRST && m <= WM_MOUSELAST;
        bool key = (m >= WM_KEYFIRST && m <= WM_KEYLAST) || m == WM_CHAR;
        if ((mouse && io.WantCaptureMouse) || (key && io.WantCaptureKeyboard)) return 0;
        if (g_blockGameKeys && (key || m == WM_INPUT)) {   // (31/10) tampoco teclas por mensajes ni raw input del teclado
            if (m != WM_INPUT) return 0;
            RAWINPUTHEADER rh{}; UINT sz = sizeof(rh);
            if (GetRawInputData((HRAWINPUT)l, RID_HEADER, &rh, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 && rh.dwType == RIM_TYPEKEYBOARD) return DefWindowProcW(h, m, w, l);
        }
    }
    return CallWindowProcW(s_oldProc, h, m, w, l);
}

// los comentarios del ini pueden venir en Latin-1: ImGui necesita UTF-8
static std::string Utf8(const std::string& s) {
    bool valid = true;
    for (size_t i = 0; i < s.size() && valid;) {
        unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size()) { valid = false; break; }
        for (int k = 1; k < n; k++) if (((unsigned char)s[i + k] >> 6) != 2) valid = false;
        i += n;
    }
    if (valid) return s;
    std::string o;
    for (unsigned char c : s) { if (c < 0x80) o += (char)c; else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); } }
    return o;
}

static void Init(IDXGISwapChain* sc) {
    DXGI_SWAP_CHAIN_DESC d; sc->GetDesc(&d);
    s_hwnd = d.OutputWindow;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 10.0f; st.FrameRounding = 6.0f; st.GrabRounding = 6.0f; st.TabRounding = 6.0f; st.PopupRounding = 6.0f;
    st.WindowPadding = ImVec2(18, 16); st.FramePadding = ImVec2(10, 6); st.ItemSpacing = ImVec2(10, 8); st.ItemInnerSpacing = ImVec2(8, 6);
    st.GrabMinSize = 14.0f; st.ScrollbarSize = 14.0f; st.WindowBorderSize = 0.0f; st.FrameBorderSize = 0.0f;
    ImVec4* c = st.Colors;
    const ImVec4 bg(0.07f, 0.08f, 0.10f, 0.96f), panel(0.11f, 0.12f, 0.15f, 1.0f), panelH(0.15f, 0.17f, 0.21f, 1.0f);
    const ImVec4 acc(0.20f, 0.72f, 0.46f, 1.0f), accH(0.28f, 0.82f, 0.55f, 1.0f), accA(0.16f, 0.60f, 0.38f, 1.0f);
    c[ImGuiCol_WindowBg] = bg; c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_PopupBg] = panel;
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.06f, 0.08f, 1.0f);
    c[ImGuiCol_FrameBg] = panel; c[ImGuiCol_FrameBgHovered] = panelH; c[ImGuiCol_FrameBgActive] = panelH;
    c[ImGuiCol_Header] = panel; c[ImGuiCol_HeaderHovered] = panelH; c[ImGuiCol_HeaderActive] = panelH;
    c[ImGuiCol_Button] = panelH; c[ImGuiCol_ButtonHovered] = ImVec4(0.21f, 0.24f, 0.29f, 1.0f); c[ImGuiCol_ButtonActive] = accA;
    c[ImGuiCol_CheckMark] = acc; c[ImGuiCol_SliderGrab] = acc; c[ImGuiCol_SliderGrabActive] = accH;
    c[ImGuiCol_Tab] = panel; c[ImGuiCol_TabHovered] = panelH; c[ImGuiCol_TabSelected] = ImVec4(0.17f, 0.20f, 0.24f, 1.0f);
    c[ImGuiCol_TabDimmed] = panel; c[ImGuiCol_TabDimmedSelected] = ImVec4(0.17f, 0.20f, 0.24f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.25f, 0.30f, 1.0f); c[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.96f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.58f, 0.64f, 1.0f); c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = panelH; c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.3f, 0.33f, 0.38f, 1.0f);
    s_baseStyle = st;
    BuildFonts((float)(d.BufferDesc.Height ? d.BufferDesc.Height : 1080));
    ImGui_ImplWin32_Init(s_hwnd);
    ImGui_ImplDX11_Init(g_dev.dev, g_dev.imm);
    s_oldProc = (WNDPROC)SetWindowLongPtrW(s_hwnd, GWLP_WNDPROC, (LONG_PTR)MenuProc);
    g_overlayOldProc = (void*)s_oldProc;
    Log("WndProc del juego antes del menu: %s", ModOff((void*)s_oldProc).c_str());
    {   // logo del PES Editor en la cabecera del menu
        ID3D11Resource* res = nullptr;
        HRESULT hr = DirectX::CreateDDSTextureFromFile(g_dev.dev, (g_cfg.baseDir + L"textures\\pes_editor_logo.dds").c_str(), &res, &s_logo);
        if (SUCCEEDED(hr) && res) {
            ID3D11Texture2D* t = nullptr;
            if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) {
                D3D11_TEXTURE2D_DESC td; t->GetDesc(&td); s_logoAspect = (float)td.Width / (float)td.Height; t->Release();
            }
            res->Release();
        }
        Log("logo del menu: %s", s_logo ? "ok" : "no encontrado (textures\\pes_editor_logo.dds)");
    }
    s_init = true;
    ImGui::GetIO().FontGlobalScale = g_cfg.uiScale;
    Log("menu listo (tecla Insert)");
}

// --- 19/10: comodidad del menu: busqueda, favoritos, contraer grupos, "solo activos", tamano ---
// (04/11 n) menu nuevo (pedido de Agustin: "mas rapido de acceder a las cosas, mejor organizado, mas practico", "Ctrl+Z", "iconos en
// las categorias"): barra lateral con categorias e iconos, paginas Inicio (Favoritos / Cambiados / Recientes), herramientas y ajustes
// aparte, Deshacer / Rehacer (Ctrl+Z / Ctrl+Y) de los cambios hechos en el menu, Ctrl+S guarda, Ctrl+F busca, Esc limpia la busqueda
static char s_search[96] = "";
static bool s_onlyActive = false;
static bool s_focusSearch = false;
static std::map<std::string, bool> s_collapsed;   // titulo -> contraido (sesion)
static int s_collapseAll = 0;                     // 1 = contraer todo, 2 = expandir todo (se aplica en el proximo dibujo)
static std::set<std::string> s_descOpen;          // (04/11 n) grupos con la descripcion completa ("mas")
static std::string s_sel;                         // (04/11 n) pagina de la barra lateral: "sec:<seccion>", "fav", "chg", "rec", "presets", "tools", "settings", "errors"
static std::vector<std::string> s_recent;         // (04/11 n) titulos de los ultimos grupos tocados, el mas nuevo primero
// (08/11) menu adaptable (Agustin: "mejora un poco el menu, mas simple y con las opciones mas detalladas, un mejor diseno responsivo"):
// el ancho de la ventana decide si la barra lateral va con nombres o solo iconos, si los botones de arriba llevan texto y si las
// tarjetas van en una o dos columnas. La ventana tiene 3 tamanos (boton al lado de la X): ventana, pantalla grande y panel a la derecha.
static bool s_rail = false;                       // barra lateral solo con iconos (ventana angosta)
static bool s_slim = false;                       // botones de arriba solo con iconos (ventana muy angosta)
static int s_winReq = -1;                         // 0 ventana, 1 grande, 2 panel a la derecha: se aplica al proximo Begin
static std::set<std::string> s_moreOpen;          // tarjetas largas con "mas opciones" desplegado
static std::string LowerU(std::string x) { for (auto& ch : x) ch = (char)tolower((unsigned char)ch); return x; }
static bool IsFav(const std::string& title) { return (g_cfg.favorites.find("|" + title + "|") != std::string::npos); }
static void SetFav(const std::string& title, bool on) {
    std::string f = g_cfg.favorites; if (f.empty() || f[0] != '|') f = "|" + f; if (f.back() != '|') f += "|";
    size_t p = f.find("|" + title + "|");
    if (on && p == std::string::npos) f += title + "|";
    if (!on && p != std::string::npos) f.erase(p, title.size() + 1);
    if (f == "|") f.clear();
    g_cfg.favorites = f; SaveOption("favoritos", f);
}
static bool GroupMatches(const MenuGroup& g, const std::string& q) {
    if (q.empty()) return true;
    std::string hay = LowerU(g.title + " " + g.desc + " " + g.section + " " + g.titleEn + " " + g.descEn + " " + g.sectionEn);
    for (auto& sl : g.sliders) hay += " " + LowerU(sl.label + " " + sl.labelEn);
    // todas las palabras de la busqueda tienen que aparecer
    size_t a = 0; while (a < q.size()) { size_t b = q.find(' ', a); if (b == std::string::npos) b = q.size(); std::string w = q.substr(a, b - a); if (!w.empty() && hay.find(w) == std::string::npos) return false; a = b + 1; }
    return true;
}
static bool GroupActive(const MenuGroup& g) { return g.tIdx < 0 || g_cfg.live[g.tIdx][g.tComp] > 0.5f; }
// (04/11 n) el grupo tiene valores distintos de lo guardado en PES21FX.ini (la ropa no cuenta: cloth.ini se escribe solo)
// formato "color" (cejas, 653c864): el slider maneja su componente y las dos siguientes (rojo / verde / azul)
static int SlSpan(const MenuSlider& sl) { return (sl.ext.empty() && sl.fmt == "color" && sl.comp <= 1) ? 3 : 1; }
static bool SlChanged(const MenuSlider& sl) {
    if (!sl.ext.empty()) return false;
    for (int k = 0; k < SlSpan(sl); k++) if (fabsf(g_cfg.live[sl.idx][sl.comp + k] - g_cfg.base[sl.idx][sl.comp + k]) > 1e-4f) return true;
    return false;
}
static void SlRevert(const MenuSlider& sl) { if (sl.ext.empty()) for (int k = 0; k < SlSpan(sl); k++) g_cfg.live[sl.idx][sl.comp + k] = g_cfg.base[sl.idx][sl.comp + k]; }
static bool GroupChanged(const MenuGroup& g) {
    if (g.tIdx >= 0 && fabsf(g_cfg.live[g.tIdx][g.tComp] - g_cfg.base[g.tIdx][g.tComp]) > 1e-4f) return true;
    for (auto& sl : g.sliders) if (SlChanged(sl)) return true;
    return false;
}
static std::string SecKey(const MenuGroup& g) { return g.section.empty() ? "Otros" : g.section; }

// --- (04/11 n) iconos (textures\pes_icons.ttf: recorte de Font Awesome Free 6, licencia en pes_icons_LICENSE.txt) ---
static std::string Ico(unsigned cp) {
    if (!s_iconsOk || !cp) return "";
    char b[5] = {};
    if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); }
    else { b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (char)(0x80 | (cp & 0x3F)); }
    return b;
}
static std::string IcoL(unsigned cp, const char* txt) { std::string i = Ico(cp); return i.empty() ? std::string(txt) : i + "  " + txt; }
enum : unsigned {
    kIcoLight = 0xf0eb, kIcoShadow = 0xf042, kIcoPalette = 0xf53f, kIcoWand = 0xe2ca, kIcoBall = 0xf1e3, kIcoStadium = 0xf66f,
    kIcoFace = 0xf118, kIcoHand = 0xf256, kIcoHair = 0xf0c4, kIcoDrop = 0xf043, kIcoBody = 0xf183, kIcoShirt = 0xf553, kIcoWind = 0xf72e,
    kIcoCamera = 0xf030, kIcoPad = 0xf11b, kIcoDiag = 0xf0f1, kIcoLayers = 0xf5fd, kIcoStar = 0xf005, kIcoPen = 0xf304, kIcoHistory = 0xf1da,
    kIcoSliders = 0xf1de, kIcoTools = 0xf7d9, kIcoGear = 0xf013, kIcoWarn = 0xf071, kIcoSave = 0xf0c7, kIcoUndo = 0xf2ea, kIcoRedo = 0xf2f9,
    kIcoRevert = 0xf0e2, kIcoEye = 0xf06e, kIcoEyeSlash = 0xf070, kIcoReload = 0xf021, kIcoSearch = 0xf002, kIcoX = 0xf00d, kIcoCompress = 0xf066, kIcoExpand = 0xf065,
    kIcoFilter = 0xf0b0, kIcoDot = 0xf111, kIcoHelp = 0xf059, kIcoDown = 0xf078, kIcoRight = 0xf054, kIcoKeys = 0xf11c, kIcoMuscle = 0xf44b, kIcoSeedling = 0xf4d8,
};
// categoria (0 escena, 1 jugadores, 2 otros), orden dentro de ella e icono de cada seccion (la clave es la seccion en espanol)
static void SecInfo(const std::string& s, int& cat, int& ord, unsigned& ico) {
    struct E { const char* pre; int cat, ord; unsigned ico; };
    static const E kE[] = {
        { "Ilumin", 0, 0, kIcoLight }, { "Sombra", 0, 1, kIcoShadow }, { "Color", 0, 2, kIcoPalette }, { "Post", 0, 3, kIcoWand },
        { "Cancha", 0, 4, kIcoBall }, { "Pasto", 0, 5, kIcoSeedling }, { "Estadio", 0, 6, kIcoStadium },   // (11/11 m) Pasto: seccion propia (Agustin: "separado de la cancha y cielo")
        { "Cara", 1, 0, kIcoFace }, { "Piel", 1, 1, kIcoHand }, { "Pelo", 1, 2, kIcoHair }, { "Sudor", 1, 3, kIcoDrop },
        { "Cuerpo", 1, 4, kIcoBody }, { "Kits", 1, 5, kIcoShirt }, { "Ropa", 1, 6, kIcoWind },
        { "Diagn", 2, 9, kIcoDiag },
    };
    for (auto& e : kE) if (s.rfind(e.pre, 0) == 0) { cat = e.cat; ord = e.ord; ico = e.ico; return; }
    const std::string l = LowerU(s);
    if (l.find("mara") != std::string::npos) { cat = 2; ord = 0; ico = kIcoCamera; return; }   // Camara (con o sin tilde)
    if (l.rfind("men", 0) == 0) { cat = 2; ord = 1; ico = kIcoPad; return; }                     // Menus del juego
    cat = 2; ord = 5; ico = kIcoLayers;
}

// --- (04/11 n) Deshacer / Rehacer (Ctrl+Z / Ctrl+Y): cada gesto (un arrastre de slider, un clic, "<<", "Descartar cambios") es un paso.
// Se miran solo los valores que tienen tarjeta en el menu (interruptores y sliders, tambien los de cloth.ini): la DLL escribe otras filas
// sola (sol, camara, medidores) y no tienen que llenar el historial. Todo esto corre con g_cfgMutex tomado.
struct UndoCell { int idx = 0, comp = 0; std::string ext; float from = 0, to = 0; };
struct UndoStep { std::vector<UndoCell> cells; std::string what; };
static std::vector<UndoStep> s_undo, s_redo;
static std::map<std::pair<int, int>, float> s_gesture; static std::map<std::string, float> s_gestureExt;   // valor antes del gesto en curso
static std::vector<float> s_snap; static std::map<std::string, float> s_snapExt;
static int s_undoReq = 0;          // 1 deshacer, 2 rehacer (se aplica al principio del proximo cuadro)
static bool s_revertAllReq = false;
static std::string s_undoMsg;      // texto del ultimo deshacer / rehacer (abajo del todo)
static double s_undoMsgT = 0;
static void PushRecent(const std::string& title) {
    auto it = std::find(s_recent.begin(), s_recent.end(), title);
    if (it != s_recent.end()) s_recent.erase(it);
    s_recent.insert(s_recent.begin(), title);
    if (s_recent.size() > 12) s_recent.resize(12);
}
static const MenuGroup* GroupOfCell(const UndoCell& c) {
    for (auto& g : g_cfg.groups) {
        if (c.ext.empty() && g.tIdx == c.idx && g.tComp == c.comp) return &g;
        for (auto& sl : g.sliders) if (c.ext.empty() ? (sl.ext.empty() && sl.idx == c.idx && sl.comp == c.comp) : sl.ext == c.ext) return &g;
    }
    return nullptr;
}
static void UndoSnapshot() {
    s_snap.assign(&g_cfg.live[0][0], &g_cfg.live[0][0] + kParamCount * 4);
    s_snapExt = g_cfg.clothVals;
}
static void UndoApply(UndoStep& st, bool undo) {
    for (auto& c : st.cells) {
        const float v = undo ? c.from : c.to;
        if (!c.ext.empty()) { g_cfg.clothVals[c.ext] = v; g_cfg.clothDirty = true; g_cfg.clothChangeT = Now(); }
        else g_cfg.live[c.idx][c.comp] = v;
    }
    s_undoMsg = std::string(TRS(undo ? "Deshecho" : "Rehecho")) + ": " + st.what; s_undoMsgT = Now();
}
static void UndoRequests() {
    if (s_undoReq == 1 && !s_undo.empty()) { UndoStep st = s_undo.back(); s_undo.pop_back(); UndoApply(st, true); s_redo.push_back(st); }
    else if (s_undoReq == 2 && !s_redo.empty()) { UndoStep st = s_redo.back(); s_redo.pop_back(); UndoApply(st, false); s_undo.push_back(st); }
    s_undoReq = 0;
}
static void UndoCollect() {
    if ((int)s_snap.size() != kParamCount * 4) return;
    auto look = [](int idx, int comp) {
        if (idx < 0 || idx >= kParamCount || comp < 0 || comp > 3) return;
        const float before = s_snap[idx * 4 + comp];
        if (fabsf(g_cfg.live[idx][comp] - before) > 1e-7f && !s_gesture.count({ idx, comp })) s_gesture[{ idx, comp }] = before;
    };
    for (auto& g : g_cfg.groups) {
        if (g.tIdx >= 0) look(g.tIdx, g.tComp);
        for (auto& sl : g.sliders) {
            if (sl.ext.empty()) { for (int k = 0; k < SlSpan(sl); k++) look(sl.idx, sl.comp + k); continue; }
            auto it = g_cfg.clothVals.find(sl.ext); if (it == g_cfg.clothVals.end()) continue;
            auto ib = s_snapExt.find(sl.ext); const float before = ib == s_snapExt.end() ? sl.def : ib->second;
            if (fabsf(it->second - before) > 1e-7f && !s_gestureExt.count(sl.ext)) s_gestureExt[sl.ext] = before;
        }
    }
    if ((s_gesture.empty() && s_gestureExt.empty()) || ImGui::IsAnyItemActive()) return;   // el gesto sigue (slider agarrado)
    UndoStep st;
    for (auto& kv : s_gesture) { UndoCell c; c.idx = kv.first.first; c.comp = kv.first.second; c.from = kv.second; c.to = g_cfg.live[c.idx][c.comp]; if (fabsf(c.to - c.from) > 1e-7f) st.cells.push_back(c); }
    for (auto& kv : s_gestureExt) { UndoCell c; c.ext = kv.first; c.from = kv.second; c.to = g_cfg.clothVals[kv.first]; if (fabsf(c.to - c.from) > 1e-7f) st.cells.push_back(c); }
    s_gesture.clear(); s_gestureExt.clear();
    if (st.cells.empty()) return;
    std::set<std::string> titles;
    for (auto& c : st.cells) if (const MenuGroup* g = GroupOfCell(c)) {
        if (titles.insert(g->title).second) { PushRecent(g->title); if (!st.what.empty()) st.what += ", "; if (titles.size() <= 3) st.what += Utf8(LS(g->title, g->titleEn)); }
    }
    if (titles.size() > 3) st.what += " (+" + std::to_string(titles.size() - 3) + ")";
    if (st.what.empty()) st.what = TRS("valores");
    // (08/11) cada cambio hecho en el menu queda en el log: si el juego se cierra, el informe del cierre muestra que se toco
    // justo antes (cierre del 08/11 al pasar la luz del editor a Foco: el log no lo decia)
    for (auto& c : st.cells) {
        if (c.ext.empty()) Log("menu: %c%d %g -> %g (%s)", "xyzw"[c.comp & 3], c.idx, c.from, c.to, st.what.c_str());
        else Log("menu: %s %g -> %g (%s)", c.ext.c_str(), c.from, c.to, st.what.c_str());
    }
    s_undo.push_back(std::move(st)); s_redo.clear();
    if (s_undo.size() > 200) s_undo.erase(s_undo.begin());
}
static void UndoClear() { s_undo.clear(); s_redo.clear(); s_gesture.clear(); s_gestureExt.clear(); s_snap.clear(); }
// "Descartar cambios": todos los interruptores y sliders del menu vuelven a lo guardado en PES21FX.ini (se puede deshacer)
static void RevertAll() {
    for (auto& g : g_cfg.groups) {
        if (g.tIdx >= 0) g_cfg.live[g.tIdx][g.tComp] = g_cfg.base[g.tIdx][g.tComp];
        for (auto& sl : g.sliders) SlRevert(sl);
    }
}

// (08/11) texto recortado con "..." al ancho dado (respeta los caracteres UTF-8)
static std::string FitText(const std::string& t, float maxW) {
    if (maxW <= 0.0f || ImGui::CalcTextSize(t.c_str()).x <= maxW) return t;
    std::string s = t;
    while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > maxW) {
        while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
        if (!s.empty()) s.pop_back();
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s + "...";
}
// descripcion corta: la primera oracion (o ~110 letras); el resto con "mas"
static std::string ShortDesc(const std::string& d, bool& cut) {
    cut = false;
    size_t p = d.find(". ");
    // (08/11) "p.ej. " / "aprox. " no terminan la oracion
    while (p != std::string::npos && ((p >= 4 && d.compare(p - 4, 4, "p.ej") == 0) || (p >= 5 && d.compare(p - 5, 5, "aprox") == 0) || (p >= 3 && d.compare(p - 3, 3, "e.g") == 0) || (p >= 3 && d.compare(p - 3, 3, "i.e") == 0))) p = d.find(". ", p + 2);
    if (p != std::string::npos && p + 1 <= 150 && p + 2 < d.size()) { cut = true; return d.substr(0, p + 1); }
    if (d.size() <= 150) return d;
    size_t s = d.rfind(' ', 110); if (s == std::string::npos || s < 60) s = 110;
    cut = true; return d.substr(0, s) + "...";
}

// --- (08/11) vidrio del menu (Agustin: "un estilo como el nuevo glass de Apple o Windows Aero") ---
// Antes de dibujar el menu se copia la imagen del juego y se desenfoca (Kawase doble: 1/2, 1/4, 1/8, 1/16 y de vuelta a 1/4, con un
// poco mas de saturacion como el vidrio de Apple). El fondo de la ventana es esa imagen en su lugar exacto, con un tinte oscuro para
// que se lea, un reflejo arriba, un filo de luz en el borde y una sombra difusa afuera. Solo corre con el menu abierto.
// [PES21FX] menu_vidrio = 0 vuelve al fondo liso.
static const char* kGlassVS =
    "struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "O main(uint id : SV_VertexID) { O o; float2 t = float2((id << 1) & 2, id & 2); o.p = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = t; return o; }\n";
static const char* kGlassDown =
    "Texture2D<float4> s0 : register(t0); SamplerState L : register(s0);\n"
    "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {\n"
    "  float w, h; s0.GetDimensions(w, h); float2 d = 1.0 / float2(w, h);\n"
    "  float3 c = s0.SampleLevel(L, uv, 0).rgb * 4.0;\n"
    "  c += s0.SampleLevel(L, uv + float2(-d.x, -d.y), 0).rgb; c += s0.SampleLevel(L, uv + float2(d.x, -d.y), 0).rgb;\n"
    "  c += s0.SampleLevel(L, uv + float2(-d.x, d.y), 0).rgb;  c += s0.SampleLevel(L, uv + float2(d.x, d.y), 0).rgb;\n"
    "  return float4(c * 0.125, 1); }\n";
static const char* kGlassUp =
    "Texture2D<float4> s0 : register(t0); SamplerState L : register(s0);\n"
    "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {\n"
    "  float w, h; s0.GetDimensions(w, h); float2 d = 1.0 / float2(w, h);\n"
    "  float3 c = s0.SampleLevel(L, uv + float2(-2 * d.x, 0), 0).rgb + s0.SampleLevel(L, uv + float2(2 * d.x, 0), 0).rgb;\n"
    "  c += s0.SampleLevel(L, uv + float2(0, -2 * d.y), 0).rgb + s0.SampleLevel(L, uv + float2(0, 2 * d.y), 0).rgb;\n"
    "  c += (s0.SampleLevel(L, uv + float2(-d.x, -d.y), 0).rgb + s0.SampleLevel(L, uv + float2(d.x, -d.y), 0).rgb\n"
    "      + s0.SampleLevel(L, uv + float2(-d.x, d.y), 0).rgb + s0.SampleLevel(L, uv + float2(d.x, d.y), 0).rgb) * 2.0;\n"
    "  c /= 12.0; float l = dot(c, float3(0.299, 0.587, 0.114)); c = max(0, lerp(l.xxx, c, 1.12));\n"
    "  return float4(c, 1); }\n";
static ID3D11VertexShader* s_gVS = nullptr; static ID3D11PixelShader *s_gDown = nullptr, *s_gUp = nullptr;
static ID3D11SamplerState* s_gSamp = nullptr; static ID3D11RasterizerState* s_gRS = nullptr; static ID3D11DepthStencilState* s_gDSS = nullptr; static ID3D11BlendState* s_gBS = nullptr;
static ID3D11Texture2D* s_gSnap = nullptr; static ID3D11ShaderResourceView* s_gSnapSRV = nullptr;
static constexpr int kGLv = 6;   // 1/2, 1/4, 1/8, 1/16 (bajada), 1/8, 1/4 (subida: la que se usa)
static ID3D11Texture2D* s_gLv[kGLv] = {}; static ID3D11RenderTargetView* s_gLvRTV[kGLv] = {}; static ID3D11ShaderResourceView* s_gLvSRV[kGLv] = {};
static UINT s_gW = 0, s_gH = 0; static DXGI_FORMAT s_gFmt = DXGI_FORMAT_UNKNOWN; static bool s_gTried = false, s_gReady = false;
template<class T> static void GRel(T*& p) { if (p) { p->Release(); p = nullptr; } }
static DXGI_FORMAT TypedFmt(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM; case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM; case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM; default: return f; }
}
static bool GlassEnsure(ID3D11Texture2D* bb) {
    if (!g_cfg.menuGlass || !bb || !g_dev.dev) return false;
    ID3D11Device* d = g_dev.dev;
    if (!s_gTried) {
        s_gTried = true;
        if (ID3DBlob* b = CompileSourceShader("pes_menu_glass_vs", kGlassVS, "vs_5_0")) { d->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_gVS); b->Release(); }
        if (ID3DBlob* b = CompileSourceShader("pes_menu_glass_down", kGlassDown, "ps_5_0")) { d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_gDown); b->Release(); }
        if (ID3DBlob* b = CompileSourceShader("pes_menu_glass_up", kGlassUp, "ps_5_0")) { d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_gUp); b->Release(); }
        D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX; d->CreateSamplerState(&sd, &s_gSamp);
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; d->CreateRasterizerState(&rd, &s_gRS);
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = FALSE; d->CreateDepthStencilState(&dd, &s_gDSS);
        D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; d->CreateBlendState(&bd, &s_gBS);
        Log("menu: vidrio %s", (s_gVS && s_gDown && s_gUp) ? "listo (desenfoque del juego detras del menu)" : "NO compilo: fondo liso");
    }
    if (!s_gVS || !s_gDown || !s_gUp || !s_gSamp) return false;
    D3D11_TEXTURE2D_DESC bd; bb->GetDesc(&bd);
    if (s_gSnap && s_gW == bd.Width && s_gH == bd.Height && s_gFmt == bd.Format) return s_gReady;
    GRel(s_gSnapSRV); GRel(s_gSnap); for (int i = 0; i < kGLv; i++) { GRel(s_gLvSRV[i]); GRel(s_gLvRTV[i]); GRel(s_gLv[i]); }
    s_gReady = false; s_gW = bd.Width; s_gH = bd.Height; s_gFmt = bd.Format;
    D3D11_TEXTURE2D_DESC td{}; td.Width = bd.Width; td.Height = bd.Height; td.MipLevels = 1; td.ArraySize = 1; td.Format = bd.Format; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(d->CreateTexture2D(&td, nullptr, &s_gSnap))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = TypedFmt(bd.Format); sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
    if (FAILED(d->CreateShaderResourceView(s_gSnap, &sv, &s_gSnapSRV))) return false;
    static const UINT kDiv[kGLv] = { 2, 4, 8, 16, 8, 4 };
    for (int i = 0; i < kGLv; i++) {
        D3D11_TEXTURE2D_DESC ld = td; ld.Width = (std::max)(1u, bd.Width / kDiv[i]); ld.Height = (std::max)(1u, bd.Height / kDiv[i]);
        ld.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; ld.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(d->CreateTexture2D(&ld, nullptr, &s_gLv[i])) || FAILED(d->CreateRenderTargetView(s_gLv[i], nullptr, &s_gLvRTV[i])) || FAILED(d->CreateShaderResourceView(s_gLv[i], nullptr, &s_gLvSRV[i]))) return false;
    }
    s_gReady = true;
    Log("menu: vidrio %ux%u (formato %d)", bd.Width, bd.Height, (int)bd.Format);
    return true;
}
static ID3D11ShaderResourceView* GlassSRV() { return (g_cfg.menuGlass && s_gReady) ? s_gLvSRV[kGLv - 1] : nullptr; }
// copia y desenfoque (en el contexto inmediato, en el Present, antes de dibujar el menu); guarda y devuelve el estado que toca
static void GlassBlur(ID3D11Texture2D* bb) {
    if (!s_gReady || !bb) return;
    ID3D11DeviceContext* c = g_dev.imm;
    ID3D11RenderTargetView* oRT[8] = {}; ID3D11DepthStencilView* oDS = nullptr; c->OMGetRenderTargets(8, oRT, &oDS);
    UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; D3D11_VIEWPORT ovp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; c->RSGetViewports(&nvp, ovp);
    ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr; ID3D11HullShader* oHS = nullptr; ID3D11DomainShader* oDSh = nullptr;
    c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr); c->HSGetShader(&oHS, nullptr, nullptr); c->DSGetShader(&oDSh, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL); D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS);
    ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDSS, &oRef);
    ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0; c->OMGetBlendState(&oBS, oBF, &oSM);
    ID3D11SamplerState* oS0 = nullptr; c->PSGetSamplers(0, 1, &oS0);
    ID3D11ShaderResourceView* oT0 = nullptr; c->PSGetShaderResources(0, 1, &oT0);

    D3D11_TEXTURE2D_DESC bd; bb->GetDesc(&bd);
    if (bd.SampleDesc.Count > 1) c->ResolveSubresource(s_gSnap, 0, bb, 0, TypedFmt(bd.Format)); else c->CopyResource(s_gSnap, bb);
    c->VSSetShader(s_gVS, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->RSSetState(s_gRS); c->OMSetDepthStencilState(s_gDSS, 0); float bf[4] = {}; c->OMSetBlendState(s_gBS, bf, 0xffffffff);
    c->PSSetSamplers(0, 1, &s_gSamp);
    auto pass = [&](ID3D11PixelShader* ps, ID3D11ShaderResourceView* src, int dst) {
        ID3D11ShaderResourceView* nul = nullptr; c->PSSetShaderResources(0, 1, &nul);
        c->OMSetRenderTargets(1, &s_gLvRTV[dst], nullptr);
        D3D11_TEXTURE2D_DESC ld; s_gLv[dst]->GetDesc(&ld);
        D3D11_VIEWPORT vp{ 0, 0, (float)ld.Width, (float)ld.Height, 0, 1 }; c->RSSetViewports(1, &vp);
        c->PSSetShaderResources(0, 1, &src); c->PSSetShader(ps, nullptr, 0); c->Draw(3, 0);
    };
    pass(s_gDown, s_gSnapSRV, 0); pass(s_gDown, s_gLvSRV[0], 1); pass(s_gDown, s_gLvSRV[1], 2); pass(s_gDown, s_gLvSRV[2], 3);
    pass(s_gUp, s_gLvSRV[3], 4); pass(s_gUp, s_gLvSRV[4], 5);
    { ID3D11ShaderResourceView* nul = nullptr; c->PSSetShaderResources(0, 1, &nul); }

    c->OMSetRenderTargets(8, oRT, oDS); for (auto* r : oRT) if (r) r->Release(); if (oDS) oDS->Release();
    c->RSSetViewports(nvp, ovp);
    c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0); c->HSSetShader(oHS, nullptr, 0); c->DSSetShader(oDSh, nullptr, 0);
    if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release(); if (oHS) oHS->Release(); if (oDSh) oDSh->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release(); c->IASetPrimitiveTopology(oTopo);
    c->RSSetState(oRS); if (oRS) oRS->Release();
    c->OMSetDepthStencilState(oDSS, oRef); if (oDSS) oDSS->Release();
    c->OMSetBlendState(oBS, oBF, oSM); if (oBS) oBS->Release();
    c->PSSetSamplers(0, 1, &oS0); if (oS0) oS0->Release();
    c->PSSetShaderResources(0, 1, &oT0); if (oT0) oT0->Release();
}
// --- (08/11) animaciones del menu: todo con curvas suaves (sale rapido y frena), por tiempo real ---
static float Ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
static float s_menuVis = 0.0f;             // 0 cerrado .. 1 abierto (aparece y se desvanece)
static double s_pageT = -10.0;             // cuando cambio la pagina (la nueva entra deslizandose)
static std::string s_pageShown;
static std::map<std::string, double> s_cardOpenT;   // cuando se desplego cada tarjeta (el contenido aparece)
static float s_navY = -1.0f, s_navH = 0.0f, s_navTgtY = -1.0f; // barra lateral: el resaltado de la pagina elegida se desliza
// animacion por elemento (pasar el mouse): sube hacia 1 o baja hacia 0 en ~0.12 s
static float HoverAnim(ImGuiID id, bool on, float speed = 9.0f) {
    float* v = ImGui::GetStateStorage()->GetFloatRef(id, 0.0f);
    const float dt = ImGui::GetIO().DeltaTime;
    *v = on ? std::min(1.0f, *v + dt * speed) : std::max(0.0f, *v - dt * speed);
    return Ease(*v);
}
static ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }
// (08/11 d) iconos de vidrio (Agustin: "cambia los iconos por unos mas glass"): brillo difuso detras, sombra corrida 1 px,
// degrade vertical (blanco arriba -> color abajo, como un caramelo de vidrio) y un reflejo fino en la mitad de arriba
static void GlassIcon(ImDrawList* dl, ImVec2 pos, const char* ic, ImVec4 col, float glow = 0.0f, float fs = 0.0f) {
    if (!ic || !*ic) return;
    ImFont* f = ImGui::GetFont(); if (fs <= 0.0f) fs = ImGui::GetFontSize();
    const ImVec2 sz = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, ic);
    const ImVec2 c(pos.x + sz.x * 0.5f, pos.y + sz.y * 0.5f);
    const float a = col.w * ImGui::GetStyle().Alpha;
    if (glow > 0.01f) for (int k = 3; k >= 1; k--) dl->AddCircleFilled(c, sz.y * (0.30f + 0.17f * k), ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.05f * glow * (4 - k))), 24);
    dl->AddText(f, fs, ImVec2(pos.x, pos.y + 1.2f * UIK()), IM_COL32(0, 0, 0, (int)(120 * a)), ic);
    const int v0 = dl->VtxBuffer.Size;
    dl->AddText(f, fs, pos, IM_COL32(255, 255, 255, (int)(255 * a)), ic);
    const ImVec4 top = Mix(col, ImVec4(1, 1, 1, 1), 0.62f), bot = ImVec4(col.x * 0.82f, col.y * 0.82f, col.z * 0.86f, 1);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(pos.x, pos.y + sz.y * 0.08f), ImVec2(pos.x, pos.y + sz.y * 0.92f), ImGui::GetColorU32(ImVec4(top.x, top.y, top.z, 1)), ImGui::GetColorU32(ImVec4(bot.x, bot.y, bot.z, 1)));
    // reflejo: el mismo glifo, blanco y muy suave, solo la mitad de arriba
    dl->PushClipRect(ImVec2(pos.x - 2, pos.y), ImVec2(pos.x + sz.x + 2, pos.y + sz.y * 0.42f), true);
    dl->AddText(f, fs, ImVec2(pos.x, pos.y - 0.6f * UIK()), IM_COL32(255, 255, 255, (int)(70 * a)), ic);
    dl->PopClipRect();
}
// boton de vidrio con un icono: fondo translucido con reflejo arriba y filo de luz; se ilumina al pasar el mouse y late al apretarlo
static bool GlassIconButton(const char* id, unsigned ico, bool on, const char* tip, ImVec2 size = ImVec2(0, 0), ImVec4 col = ImVec4(0.40f, 0.90f, 0.62f, 1)) {
    if (size.x <= 0) size.x = ImGui::GetFrameHeight() * 1.25f; if (size.y <= 0) size.y = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool r = ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const float ha = HoverAnim(ImGui::GetItemID(), hov, 11.0f), pa = HoverAnim(ImGui::GetItemID() + 7, act, 18.0f), oa = HoverAnim(ImGui::GetItemID() + 13, on, 8.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(p.x + size.x, p.y + size.y); const float rr = 7.0f * UIK();
    const ImVec4 base = Mix(ImVec4(1, 1, 1, 0.07f), ImVec4(col.x, col.y, col.z, 0.30f), oa);
    dl->AddRectFilled(p, b, ImGui::GetColorU32(ImVec4(base.x, base.y, base.z, base.w + 0.06f * ha + 0.06f * pa)), rr);
    dl->AddRectFilledMultiColor(ImVec2(p.x + 1, p.y + 1), ImVec2(b.x - 1, p.y + size.y * 0.5f), IM_COL32(255, 255, 255, (int)(26 + 20 * ha)), IM_COL32(255, 255, 255, (int)(26 + 20 * ha)), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    dl->AddRect(p, b, IM_COL32(255, 255, 255, (int)(40 + 50 * ha)), rr, 0, 1.0f);
    const std::string ic = Ico(ico); const float fs = ImGui::GetFontSize() * (1.0f + 0.06f * ha - 0.06f * pa);
    const ImVec2 is = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, ic.c_str());
    GlassIcon(dl, ImVec2(p.x + (size.x - is.x) * 0.5f, p.y + (size.y - is.y) * 0.5f), ic.c_str(), on ? col : Mix(ImVec4(0.80f, 0.84f, 0.90f, 1), col, ha * 0.6f), ha + oa, fs);
    if (hov && tip) ImGui::SetTooltip("%s", tip);
    return r;
}
// el fondo de vidrio de una ventana: imagen desenfocada, tinte, reflejo arriba, filo de luz y sombra afuera
static void GlassBackdrop(ImVec2 a, ImVec2 b, float rounding, float alpha) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    {   // sombra difusa afuera (detras de la ventana)
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        const float s = 3.0f * UIK();
        for (int i = 1; i <= 8; i++) { const float k = 1.0f - i / 9.0f; bg->AddRect(ImVec2(a.x - i * s, a.y - i * s + 4 * UIK()), ImVec2(b.x + i * s, b.y + i * s + 6 * UIK()), IM_COL32(0, 0, 0, (int)(70 * k * k * alpha)), rounding + i * s, 0, s * 1.2f); }
    }
    ImGui::PushClipRect(a, b, false);
    if (ID3D11ShaderResourceView* srv = GlassSRV()) {
        dl->AddImageRounded((ImTextureID)(intptr_t)srv, a, b, ImVec2(a.x / ds.x, a.y / ds.y), ImVec2(b.x / ds.x, b.y / ds.y), IM_COL32(255, 255, 255, (int)(255 * alpha)), rounding);
        const float tint = std::clamp(g_cfg.menuAlpha, 0.5f, 1.0f);   // Opacidad del menu: cuanto oscurece el vidrio (lectura)
        dl->AddRectFilled(a, b, IM_COL32(12, 15, 21, (int)(255 * alpha * (0.30f + 0.50f * (tint - 0.5f) / 0.5f))), rounding);
    } else dl->AddRectFilled(a, b, IM_COL32(18, 20, 26, (int)(255 * alpha * std::clamp(g_cfg.menuAlpha, 0.5f, 1.0f))), rounding);
    {   // reflejo de arriba: luz suave que se apaga hacia abajo
        const float h = std::min(140.0f * UIK(), (b.y - a.y) * 0.35f), y0 = a.y + rounding * 0.6f;
        dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(b.x, y0), IM_COL32(255, 255, 255, (int)(16 * alpha)), rounding, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilledMultiColor(ImVec2(a.x, y0), ImVec2(b.x, y0 + h), IM_COL32(255, 255, 255, (int)(16 * alpha)), IM_COL32(255, 255, 255, (int)(16 * alpha)), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    }
    // filo de luz: todo el borde fino y mas fuerte arriba a la izquierda (como el vidrio grueso)
    dl->AddRect(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f), IM_COL32(255, 255, 255, (int)(34 * alpha)), rounding, 0, 1.0f);
    {
        const float r = rounding, PI = 3.14159265f;
        dl->PathArcTo(ImVec2(a.x + r, a.y + r), r - 0.5f, PI, PI * 1.5f, 10);
        dl->PathLineTo(ImVec2(a.x + (b.x - a.x) * 0.45f, a.y + 0.5f));
        dl->PathStroke(IM_COL32(255, 255, 255, (int)(110 * alpha)), 0, 1.4f * UIK());
        dl->PathArcTo(ImVec2(a.x + r, a.y + r), r - 0.5f, PI * 0.75f, PI, 6);
        dl->PathLineTo(ImVec2(a.x + 0.5f, a.y + r + (b.y - a.y) * 0.18f));
        dl->PathStroke(IM_COL32(255, 255, 255, (int)(60 * alpha)), 0, 1.2f * UIK());
        dl->PathArcTo(ImVec2(b.x - r, b.y - r), r - 0.5f, 0.0f, PI * 0.5f, 10);
        dl->PathStroke(IM_COL32(255, 255, 255, (int)(40 * alpha)), 0, 1.0f);
    }
    ImGui::PopClipRect();
}
// tono de los paneles de adentro: con vidrio dejan ver el fondo (un poco mas claros, translucidos), sin vidrio como antes
static ImVec4 PanelCol(const ImVec4& solid, float glassA) { return GlassSRV() ? ImVec4(solid.x + 0.05f, solid.y + 0.06f, solid.z + 0.08f, glassA) : solid; }

// (08/11) explicacion de la opcion elegida en un selector: busca en la ayuda "<opcion> = ..." u "<opcion>: ..." (empezando la frase o
// despues de ';' ',' '.') y corta en el proximo ';' o fin de oracion. "Nublado = cielo cubierto, luz de arriba" -> "cielo cubierto, luz de arriba"
static std::string OptionExplain(const std::string& help, const std::string& opt) {
    if (help.empty() || opt.empty()) return "";
    const std::string h = LowerU(help), o = LowerU(opt);
    for (size_t p = h.find(o); p != std::string::npos; p = h.find(o, p + 1)) {
        if (p > 0 && !strchr(" ;.,(", h[p - 1])) continue;
        size_t q = p + o.size();
        while (q < h.size() && h[q] == ' ') q++;
        if (q >= h.size() || (h[q] != '=' && h[q] != ':')) continue;
        size_t a = q + 1; while (a < help.size() && help[a] == ' ') a++;
        size_t e = help.size();
        for (size_t k = a; k < help.size(); k++) if (help[k] == ';' || (help[k] == '.' && (k + 1 == help.size() || help[k + 1] == ' '))) { e = k; break; }
        if (e > a) return help.substr(a, e - a);
    }
    return "";
}
static int CurOption(const MenuSlider& sl, float val) {
    int cur = (int)(val + 0.5f); cur = std::clamp(cur, 0, (int)sl.options.size() - 1);
    if (!sl.optVals.empty()) { cur = 0; for (int oi = 1; oi < (int)sl.optVals.size(); oi++) if (fabsf(sl.optVals[oi] - val) < fabsf(sl.optVals[cur] - val)) cur = oi; }
    return cur;
}
static constexpr int kMoreAfter = 6;   // (08/11) tarjetas con mas controles: los primeros 5 a la vista, el resto con "mas opciones"

// (04/11 n) slider nuevo (Agustin: "mejora el diseno de los sliders"): barra fina con relleno, perilla redonda, marca del valor guardado
// y el numero en una cajita aparte que se toca para escribirlo. Por debajo sigue el SliderFloat de ImGui (arrastre, Ctrl+clic, teclado),
// asi que el lugar de la perilla es el mismo que el del agarre de ImGui (2 px de margen + medio agarre a cada lado).
// Devuelve true con clic derecho sobre la barra (el menu de "volver a lo guardado" lo abre quien llama).
static bool PrettySlider(float* v, float lo, float hi, const char* fmt, float width, bool changed, float savedV, const char* tip) {
    ImGuiStyle& st = ImGui::GetStyle();
    const float valW = std::clamp(ImGui::CalcTextSize("-000.000").x + st.FramePadding.x * 2.0f, 62.0f * UIK(), 120.0f * UIK());
    const float trackW = std::max(40.0f * UIK(), width - valW - st.ItemSpacing.x);
    const ImGuiID sid = ImGui::GetID("##trk");
    const bool typing = ImGui::TempInputIsActive(sid);
    if (!typing) {
        const ImVec4 z(0, 0, 0, 0);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, z); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, z); ImGui::PushStyleColor(ImGuiCol_FrameBgActive, z);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, z); ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, z); ImGui::PushStyleColor(ImGuiCol_Text, z);
    }
    ImGui::SetNextItemWidth(trackW);
    // (08/11 d) Agustin: "un slider mas responsivo y con un diseno mas glass". El numero se escribe en la cajita de al lado, asi que
    // el doble clic ya no abre la escritura (NoInput): vuelve a lo guardado. Rueda del mouse = ajuste fino (1% del rango; Shift 0.1%,
    // Ctrl 5%), flechas con el mouse encima igual. Mientras se arrastra, el valor flota sobre la perilla.
    ImGui::SliderFloat("##trk", v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
    if (!typing) ImGui::PopStyleColor(6);
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const bool rclick = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    {   ImGuiIO& io = ImGui::GetIO();
        const float span = hi - lo, step = span * (io.KeyShift ? 0.001f : io.KeyCtrl ? 0.05f : 0.01f);
        float d = 0.0f;
        if (hov) { ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); d += io.MouseWheel * step; }
        if (hov && !act) { if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) d += step; if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) d -= step; }
        if (d != 0.0f) *v = std::clamp(*v + d, std::min(lo, hi), std::max(lo, hi));
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { *v = savedV; ImGui::ClearActiveID(); }
    }
    if (hov && !act && tip) ImGui::SetTooltip("%s", tip);
    if (!typing) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const float gs = std::min(st.GrabMinSize, b.x - a.x - 4.0f);
        const float x0 = a.x + 2.0f + gs * 0.5f, x1 = b.x - 2.0f - gs * 0.5f, cy = std::round((a.y + b.y) * 0.5f);
        auto px = [&](float x) { const float t = hi != lo ? std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f; return x0 + t * (x1 - x0); };
        const float ha = HoverAnim(sid, hov || act, 10.0f), aa = HoverAnim(sid + 1, act, 12.0f);
        const float th = std::max(5.0f, (5.5f + 1.5f * ha) * UIK());
        // la perilla sigue al valor: al arrastrar al instante; si el valor salta (clic en el riel, rueda, deshacer) se desliza en ~60 ms
        float* kxS = ImGui::GetStateStorage()->GetFloatRef(sid + 2, -1e9f);
        const float kxT = px(*v);
        if (act || *kxS < -1e8f || fabsf(*kxS - kxT) < 0.5f) *kxS = kxT;
        else *kxS += (kxT - *kxS) * (1.0f - expf(-ImGui::GetIO().DeltaTime * 38.0f));
        const float kx = *kxS;
        const ImVec4 fill = changed ? ImVec4(0.98f, 0.80f, 0.35f, 1) : ImVec4(0.26f, 0.84f, 0.56f, 1);
        // riel de vidrio: hundido (sombra arriba, luz abajo), translucido
        dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(x1, cy + th * 0.5f), ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.30f + 0.08f * ha)), th * 0.5f);
        dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(x1, cy + th * 0.5f), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.06f + 0.05f * ha)), th * 0.5f);
        dl->AddLine(ImVec2(x0 + th * 0.5f, cy - th * 0.5f + 0.5f), ImVec2(x1 - th * 0.5f, cy - th * 0.5f + 0.5f), IM_COL32(0, 0, 0, 90), 1.0f);
        dl->AddLine(ImVec2(x0 + th * 0.5f, cy + th * 0.5f), ImVec2(x1 - th * 0.5f, cy + th * 0.5f), IM_COL32(255, 255, 255, 34), 1.0f);
        if (kx > x0 + 0.5f) {   // relleno con degrade (mas oscuro al principio, brillante en la punta) y su brillo
            const int v0 = dl->VtxBuffer.Size;
            dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(kx, cy + th * 0.5f), IM_COL32_WHITE, th * 0.5f);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(x0, cy), ImVec2(std::max(kx, x0 + 1.0f), cy),
                ImGui::GetColorU32(ImVec4(fill.x * 0.45f, fill.y * 0.55f, fill.z * 0.60f, 1)), ImGui::GetColorU32(fill));
            dl->AddRectFilled(ImVec2(x0 + 1.0f, cy - th * 0.5f + 1.0f), ImVec2(kx - 1.0f, cy - 0.5f), IM_COL32(255, 255, 255, 46), th * 0.5f, ImDrawFlags_RoundCornersTop);
        }
        if (changed) { const float sx = px(savedV); dl->AddRectFilled(ImVec2(sx - 1.0f, cy - th * 1.3f), ImVec2(sx + 1.0f, cy + th * 1.3f), ImGui::GetColorU32(ImVec4(0.85f, 0.88f, 0.93f, 0.80f)), 1.0f); }
        // perilla de vidrio: halo del color, sombra, cuerpo translucido, reflejo arriba y filo de luz
        const float r = (8.0f + 1.5f * ha + 1.0f * aa) * UIK();
        if (ha > 0.01f) for (int k = 3; k >= 1; k--) dl->AddCircleFilled(ImVec2(kx, cy), r + 2.5f * UIK() * k * ha, ImGui::GetColorU32(ImVec4(fill.x, fill.y, fill.z, 0.07f * ha)), 28);
        dl->AddCircleFilled(ImVec2(kx, cy + 1.5f * UIK()), r + 0.5f, IM_COL32(0, 0, 0, 90), 28);
        dl->AddCircleFilled(ImVec2(kx, cy), r, ImGui::GetColorU32(Mix(ImVec4(0.86f, 0.90f, 0.95f, 0.55f), ImVec4(0.94f, 0.97f, 1.0f, 0.75f), ha)), 28);
        dl->AddCircleFilled(ImVec2(kx, cy), r * 0.55f, ImGui::GetColorU32(ImVec4(fill.x, fill.y, fill.z, 0.85f)), 20);
        dl->PathArcTo(ImVec2(kx, cy), r * 0.70f, 3.14159f * 1.10f, 3.14159f * 1.90f, 14);
        dl->PathStroke(IM_COL32(255, 255, 255, 200), 0, std::max(1.2f, 1.6f * UIK()));
        dl->AddCircle(ImVec2(kx, cy), r, IM_COL32(255, 255, 255, (int)(110 + 90 * ha)), 28, 1.0f);
        if (act) {   // el valor flota sobre la perilla mientras se arrastra
            char vb[48]; snprintf(vb, sizeof vb, fmt, *v);
            const ImVec2 ts = ImGui::CalcTextSize(vb); const float pw = ts.x + 14.0f * UIK(), ph = ts.y + 6.0f * UIK();
            const ImVec2 bp(std::round(kx - pw * 0.5f), std::round(cy - r - ph - 6.0f * UIK() * aa));
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            fg->AddRectFilled(ImVec2(bp.x, bp.y + 2), ImVec2(bp.x + pw, bp.y + ph + 2), IM_COL32(0, 0, 0, (int)(90 * aa)), ph * 0.5f);
            fg->AddRectFilled(bp, ImVec2(bp.x + pw, bp.y + ph), ImGui::GetColorU32(ImVec4(0.12f, 0.15f, 0.20f, 0.92f * aa)), ph * 0.5f);
            fg->AddRect(bp, ImVec2(bp.x + pw, bp.y + ph), ImGui::GetColorU32(ImVec4(fill.x, fill.y, fill.z, 0.8f * aa)), ph * 0.5f, 0, 1.0f);
            fg->AddText(ImVec2(bp.x + 7.0f * UIK(), bp.y + 3.0f * UIK()), ImGui::GetColorU32(ImVec4(1, 1, 1, aa)), vb);
        }
    }
    ImGui::SameLine(0, st.ItemSpacing.x);
    ImGui::SetNextItemWidth(valW);
    float tmp = *v;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.13f, 0.14f, 0.17f, 1));
    if (changed) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.86f, 0.50f, 1));
    if (ImGui::InputFloat("##val", &tmp, 0.0f, 0.0f, fmt)) *v = std::clamp(tmp, std::min(lo, hi), std::max(lo, hi));
    if (changed) ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("%s", TRS("Clic para escribir el valor"));
    return rclick || ImGui::IsItemClicked(ImGuiMouseButton_Right);
}

// (08/11 d) la tarjeta bajo el mouse se ilumina suave: filo de luz verde y un reflejo arriba (aparece y se va en ~0.15 s)
static void CardGlow() {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const bool hv = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const float h = HoverAnim(w->ID + 5, hv, 7.0f);
    if (h < 0.01f) return;
    ImDrawList* dl = w->DrawList; const ImVec2 a = w->Pos, b(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y);
    const float rr = ImGui::GetStyle().FrameRounding;
    dl->PushClipRect(a, b, true);   // (08/11 e) intersecta con el recorte de la pagina: con la tarjeta medio fuera de la vista el filo y el reflejo se dibujaban encima del titulo
    dl->AddRectFilledMultiColor(ImVec2(a.x + 2, a.y + 1), ImVec2(b.x - 2, a.y + std::min(36.0f * UIK(), w->Size.y * 0.5f)), IM_COL32(255, 255, 255, (int)(14 * h)), IM_COL32(255, 255, 255, (int)(14 * h)), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    dl->AddRect(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f), ImGui::GetColorU32(ImVec4(0.45f, 0.92f, 0.66f, 0.38f * h)), rr, 0, 1.2f);
    dl->PopClipRect();
}

static void DrawGroup(MenuGroup& g, int gid) {
    ImGui::PushID(1000 + gid);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, PanelCol(ImVec4(0.11f, 0.12f, 0.15f, 1), 0.50f));   // fondo de la tarjeta ((08/11) con vidrio, translucido)
    ImGui::BeginChild("card", ImVec2(0 * UIK(), 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
    ImGui::PopStyleColor();
    // (04/11 n) dentro de la tarjeta los sliders, casillas y selectores van un tono mas claro (antes eran del mismo color que la tarjeta)
    ImGui::PushStyleColor(ImGuiCol_FrameBg, PanelCol(ImVec4(0.18f, 0.20f, 0.25f, 1), 0.80f)); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, PanelCol(ImVec4(0.22f, 0.25f, 0.30f, 1), 0.88f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.24f, 0.27f, 0.33f, 1));
    bool hasT = g.tIdx >= 0;
    bool on = hasT ? (g_cfg.live[g.tIdx][g.tComp] > 0.5f) : true;
    // contraer / favorito / restablecer (fila de la derecha)
    if (s_collapsed.find(g.title) == s_collapsed.end()) s_collapsed[g.title] = g_cfg.compact;
    bool& col = s_collapsed[g.title];
    const bool changed = GroupChanged(g);
    float availW = ImGui::GetContentRegionAvail().x;
    float rightW = (s_iconsOk ? 64.0f : 96.0f) * UIK();
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::SmallButton(s_iconsOk ? (Ico(col ? kIcoRight : kIcoDown) + "##cx").c_str() : (col ? "+" : "-"))) { col = !col; if (!col) s_cardOpenT[g.title] = Now(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS(col ? "Expandir" : "Contraer"));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    const std::string gTitleFull = Utf8(LS(g.title, g.titleEn)), gDesc = LS(g.desc, g.descEn);
    // (08/11) titulo recortado con "..." si no entra (ventana angosta): no pisa la estrella ni el aviso de cambios
    const float titleMax = availW - rightW - (ImGui::GetCursorPosX() - ImGui::GetCursorStartPos().x) - (hasT ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x : 0.0f) - (changed ? 26.0f * UIK() : 0.0f);
    const std::string gTitle = FitText(gTitleFull, titleMax) + "###tit";
    if (hasT) { if (ImGui::Checkbox(gTitle.c_str(), &on)) { float b = g_cfg.base[g.tIdx][g.tComp], &v = g_cfg.live[g.tIdx][g.tComp]; if (!on) { g.tLast = v; v = 0.0f; } else v = g.tLast > 0.5f ? g.tLast : (b > 0.5f ? b : 1.0f); } }   // (07/11) al prender vuelve el valor que tenia (1.7 = manto), no 1
    else ImGui::TextUnformatted(gTitle.substr(0, gTitle.size() - 6).c_str());
    if (s_fontBold) ImGui::PopFont();
    if (gTitle.size() - 6 != gTitleFull.size() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", gTitleFull.c_str());
    if (changed) {   // (04/11 n) aviso de cambios sin guardar en la tarjeta
        ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", s_iconsOk ? Ico(kIcoDot).c_str() : "*");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Cambiado sin guardar (Ctrl+S guarda, Ctrl+Z deshace)"));
    }
    ImGui::EndGroup();
    ImGui::SameLine(std::max(availW - rightW, ImGui::GetCursorPosX()));
    { bool fav = IsFav(g.title);
      // (08/11) mas simple: la estrella se ve si es favorita o con el mouse encima de la tarjeta; restablecer solo si hay cambios
      const bool cardHov = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      if (fav) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.78f, 0.3f, 1));
      else if (s_iconsOk) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.45f, 0.50f, cardHov ? 1.0f : 0.0f));
      if (ImGui::SmallButton(s_iconsOk ? (Ico(kIcoStar) + "##fav").c_str() : (fav ? "* fav" : "+ fav"))) SetFav(g.title, !fav);
      if (fav || s_iconsOk) ImGui::PopStyleColor();
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS(fav ? "Quitar de Favoritos" : "Agregar a Favoritos"));
      ImGui::SameLine();
      if (!changed) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
      ImGui::BeginDisabled(!changed);
      if (ImGui::SmallButton(s_iconsOk ? (Ico(kIcoRevert) + "##rst").c_str() : "<<")) { for (auto& sl : g.sliders) SlRevert(sl); if (hasT) g_cfg.live[g.tIdx][g.tComp] = g_cfg.base[g.tIdx][g.tComp]; }
      ImGui::EndDisabled();
      if (!changed) ImGui::PopStyleColor();
      ImGui::PopStyleColor();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", TRS("Volver todo el grupo a lo guardado")); }
    if (col) { if (!gDesc.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.52f, 0.56f, 1)); ImGui::TextUnformatted(Utf8(gDesc.substr(0, 90) + (gDesc.size() > 90 ? "..." : "")).c_str()); ImGui::PopStyleColor(); } ImGui::PopStyleColor(3); CardGlow(); ImGui::EndChild(); ImGui::Spacing(); ImGui::PopID(); return; }
    // (08/11) al desplegar, el contenido aparece suave (0.22 s)
    float openA = 1.0f;
    { auto it = s_cardOpenT.find(g.title); if (it != s_cardOpenT.end()) { const float t = (float)(Now() - it->second) / 0.22f; if (t >= 1.0f) s_cardOpenT.erase(it); else openA = Ease(t); } }
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (0.15f + 0.85f * openA));
    if (!gDesc.empty()) {   // (04/11 n) descripcion corta; clic para leerla entera
        const bool open = s_descOpen.count(g.title) != 0;
        bool cut = false; const std::string sd = open ? gDesc : ShortDesc(gDesc, cut);
        const float moreW = (cut || open) ? ImGui::CalcTextSize(TRS("mas...")).x + 2.0f * ImGui::GetStyle().FramePadding.x + ImGui::GetStyle().ItemSpacing.x : 0.0f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(100.0f, ImGui::GetContentRegionAvail().x - moreW));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.69f, 0.74f, 1)); ImGui::TextWrapped("%s", Utf8(sd).c_str()); ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        if (cut || open) {
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) { if (open) s_descOpen.erase(g.title); else s_descOpen.insert(g.title); }
            ImGui::SameLine(); ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.75f, 0.55f, 1));
            if (ImGui::SmallButton(TRS(open ? "menos" : "mas..."))) { if (open) s_descOpen.erase(g.title); else s_descOpen.insert(g.title); }
            ImGui::PopStyleColor(2);
        }
    }
    bool anyExt = false; for (auto& sl : g.sliders) anyExt |= !sl.ext.empty();
    if (anyExt && !g_cfg.clothFound) ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1), "%s", TRS("No se encontro cloth-mod\\cloth.ini"));
    if (!g.sliders.empty()) {
        ImGui::Spacing();
        ImGui::BeginDisabled(!on);
        const float w = ImGui::GetContentRegionAvail().x;
        // (04/11 n) en tarjetas anchas la etiqueta va a la izquierda y el slider en la misma fila (la mitad de alto)
        const bool wide = w > 520.0f * UIK();
        const float labelW = wide ? std::clamp(w * 0.38f, 160.0f * UIK(), 300.0f * UIK()) : 0.0f;
        const float resetW = ImGui::GetFrameHeight() * 1.1f;
        // (08/11) tarjetas largas (Luz de prueba 30 controles, Teselacion 37): los primeros a la vista y el resto con "mas opciones".
        // Con la busqueda, en Cambiados o con [PES21FX] menu_avanzado = 1 se ven todos; un control cambiado sin guardar siempre se ve.
        const bool searching = s_search[0] != 0;
        const bool allOpen = searching || g_cfg.menuAdvanced || s_sel == "chg" || s_moreOpen.count(g.title) || (int)g.sliders.size() <= kMoreAfter;
        int nHidden = 0, slN = 0;
        for (auto& sl : g.sliders) {
            const bool hiddenSl = !allOpen && slN++ >= kMoreAfter - 1 && !(sl.ext.empty() && SlChanged(sl));
            if (hiddenSl) { nHidden++; continue; }
            ImGui::PushID(&sl);
            bool isExt = !sl.ext.empty();
            if (isExt && !g_cfg.clothFound) { ImGui::PopID(); continue; }
            if (isExt && !g_cfg.clothVals.count(sl.ext)) g_cfg.clothVals[sl.ext] = sl.def;
            float& val = isExt ? g_cfg.clothVals[sl.ext] : g_cfg.live[sl.idx][sl.comp];
            float before = val;
            const float savedV = isExt ? (g_cfg.clothLoaded.count(sl.ext) ? g_cfg.clothLoaded[sl.ext] : sl.def) : g_cfg.base[sl.idx][sl.comp];
            const bool slChanged = isExt ? fabsf(savedV - val) > 1e-4f : SlChanged(sl);
            const std::string help = sl.help.empty() ? std::string() : Utf8(LS(sl.help, sl.helpEn));
            auto helpTip = [&]() {
                ImGui::BeginTooltip(); ImGui::PushTextWrapPos(380 * UIK());
                if (!help.empty()) ImGui::TextUnformatted(help.c_str());
                if (slChanged) ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.45f, 1), TRS("Cambiado (sin guardar). Guardado: %.3f"), savedV);
                ImGui::PopTextWrapPos(); ImGui::EndTooltip();
            };
            {   // etiqueta
                const float x0 = ImGui::GetCursorPosX();
                if (wide) ImGui::AlignTextToFramePadding();
                if (slChanged) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.45f, 1));
                if (wide) ImGui::PushTextWrapPos(x0 + labelW - 10.0f * UIK());
                ImGui::TextWrapped("%s", Utf8(LS(sl.label, sl.labelEn)).c_str());
                if (wide) ImGui::PopTextWrapPos();
                if (slChanged) ImGui::PopStyleColor();
                if ((!help.empty() || slChanged) && ImGui::IsItemHovered()) helpTip();
                if (!help.empty() && !wide && g_cfg.menuHelp == 0) { ImGui::SameLine(); ImGui::TextDisabled("(?)"); if (ImGui::IsItemHovered()) helpTip(); }
                if (wide) ImGui::SameLine(x0 + labelW);
            }
            const float itemW = std::max(60.0f * UIK(), (wide ? w - labelW : w) - resetW - ImGui::GetStyle().ItemSpacing.x);
            ImGui::SetNextItemWidth(itemW);
            bool openCtx = false;
            if (!sl.options.empty()) {
                const auto& opts = (g_cfg.lang != 0 && sl.optionsEn.size() == sl.options.size()) ? sl.optionsEn : sl.options;
                const int cur = CurOption(sl, val);
                std::string prev = Utf8(opts[cur]);
                if (ImGui::BeginCombo("##c", prev.c_str())) {
                    const std::string& hlp = LS(sl.help, sl.helpEn);
                    for (int oi = 0; oi < (int)sl.options.size(); oi++) {
                        bool sel = oi == cur;
                        if (ImGui::Selectable(Utf8(opts[oi]).c_str(), sel)) val = sl.optVals.empty() ? (float)oi : sl.optVals[oi];
                        if (ImGui::IsItemHovered()) { const std::string ex = OptionExplain(hlp, opts[oi]); if (!ex.empty()) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(380 * UIK()); ImGui::TextUnformatted(Utf8(ex).c_str()); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); } }   // (08/11) que hace cada opcion
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                openCtx = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            } else if (!isExt && sl.fmt == "color" && sl.comp <= 1) {
                // (04/11 n, Agustin: "el selector de color no es muy preciso") formato "color": un selector de color (rueda + valores)
                // sobre esta componente y las dos siguientes (rojo / verde / azul) en vez de tres sliders
                float* rgb = &g_cfg.live[sl.idx][sl.comp];
                for (int k = 0; k < 3; k++) rgb[k] = rgb[k] < 0.f ? 0.f : rgb[k] > 1.f ? 1.f : rgb[k];
                ImGui::ColorEdit3("##col", rgb, ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Float);
                if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("%s", TRS("Clic en el cuadro de color: rueda de color; Ctrl+clic en un numero para escribirlo"));
            } else {
                // (31/10) AlwaysClamp: el valor escrito con Ctrl+clic tambien queda dentro del rango del slider
                // (x318 "Claridad del ojo" se guardaba en -1.17 / 2.61 con rango 0..1); ademas se recorta lo que
                // venga de afuera (teclas, Avanzado) para que el ini nunca guarde un valor fuera del rango
                if (!isExt && (val < (std::min)(sl.lo, sl.hi) || val > (std::max)(sl.lo, sl.hi))) { float cl = std::clamp(val, (std::min)(sl.lo, sl.hi), (std::max)(sl.lo, sl.hi));   /* (11/11 n) slider al reves (lo > hi) */ Log("menu: %c%d = %g fuera del rango %g..%g del slider '%s': recortado a %g", "xyzw"[sl.comp], sl.idx, val, sl.lo, sl.hi, sl.label.c_str(), cl); val = cl; }
                char tipb[300];   // (08/11) el rango del slider en el aviso
                { char a[40], b[40]; snprintf(a, sizeof a, sl.fmt.c_str(), sl.lo); snprintf(b, sizeof b, sl.fmt.c_str(), sl.hi);
                  snprintf(tipb, sizeof tipb, TRS("Va de %s a %s. Arrastra, o la rueda del mouse para el ajuste fino (Shift: mas fino). Doble clic o clic derecho: volver a lo guardado. Toca el numero para escribirlo."), a, b); }
                openCtx = PrettySlider(&val, sl.lo, sl.hi, sl.fmt.c_str(), itemW, slChanged, savedV, tipb);
            }
            if (openCtx) ImGui::OpenPopup("ctx");
            if (ImGui::BeginPopup("ctx")) {   // (04/11 n) clic derecho sobre el slider
                if (ImGui::MenuItem(TRS("Volver a lo guardado"), nullptr, false, slChanged)) { if (isExt) val = savedV; else SlRevert(sl); }
                if (isExt && ImGui::MenuItem(TRS("Valor de fabrica"))) val = sl.def;
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (slChanged) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                if (ImGui::Button(s_iconsOk ? Ico(kIcoRevert).c_str() : "<", ImVec2(resetW, 0))) { if (isExt) val = savedV; else SlRevert(sl); }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(TRS(isExt ? "Volver al valor que tenia cloth.ini (%.4g); fabrica: %.4g" : "Volver al valor guardado (%.3f)"), savedV, sl.def);
            } else ImGui::Dummy(ImVec2(resetW, ImGui::GetFrameHeight()));
            if (g_cfg.menuHelp > 0) {   // (08/11) explicacion debajo de cada opcion (Ajustes: breve / completa / solo con el mouse)
                std::string line; bool cut = false;
                const std::string& hlp = LS(sl.help, sl.helpEn);
                if (!sl.options.empty() && g_cfg.menuHelp == 1) {   // selector: lo que hace la opcion elegida
                    const auto& opts = (g_cfg.lang != 0 && sl.optionsEn.size() == sl.options.size()) ? sl.optionsEn : sl.options;
                    const std::string& o = opts[CurOption(sl, val)];
                    const std::string ex = OptionExplain(hlp, o);
                    if (!ex.empty()) { line = o + ": " + ex; cut = line.size() < hlp.size(); }
                }
                if (line.empty() && !hlp.empty()) line = g_cfg.menuHelp >= 2 ? hlp : ShortDesc(hlp, cut);
                if (!line.empty()) {
                    ImGui::SetWindowFontScale(0.88f);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.53f, 0.57f, 0.63f, 1));
                    ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - resetW);
                    ImGui::TextUnformatted(Utf8(line).c_str());
                    ImGui::PopTextWrapPos(); ImGui::PopStyleColor();
                    ImGui::SetWindowFontScale(1.0f);
                    if ((cut || slChanged) && ImGui::IsItemHovered()) helpTip();
                    ImGui::Dummy(ImVec2(0, 3.0f * UIK()));
                }
            }
            if (isExt && val != before) { g_cfg.clothDirty = true; g_cfg.clothChangeT = Now(); }
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        if (nHidden > 0 || (!searching && !g_cfg.menuAdvanced && s_moreOpen.count(g.title))) {   // (08/11) "mas opciones" / "menos opciones"
            const bool isOpen = nHidden == 0;
            char mb[96]; snprintf(mb, sizeof mb, isOpen ? TRS("Menos opciones") : TRS("%d opciones mas"), nHidden);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.78f, 0.56f, 1));
            if (ImGui::SmallButton(IcoL(isOpen ? kIcoDown : kIcoRight, mb).c_str())) { if (isOpen) s_moreOpen.erase(g.title); else { s_moreOpen.insert(g.title); s_cardOpenT[g.title] = Now(); } }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS(isOpen ? "Volver a ver solo las principales" : "Los ajustes finos de esta mejora (en Ajustes se pueden ver siempre)"));
        }
    }
    ImGui::PopStyleVar();   // aparicion del contenido
    if (g.special == "musculatura_jugador") { ImGui::Spacing(); MuscleDrawPlayerPanel(); }
    if (!g.buttons.empty()) {
        ImGui::Spacing();
        for (auto& b : g.buttons) {
            ImGui::PushID(&b);
            if (ImGui::Button(Utf8(LS(b.label, b.labelEn)).c_str())) s_btnPending = b.action;
            if (!b.help.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Utf8(LS(b.help, b.helpEn)).c_str());
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }
    ImGui::PopStyleColor(3);
    CardGlow();
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::PopID();
}


// --- tutoriales: una tarjeta al principio de cada pestana (se apagan con "No mostrar mas" -> [PES21FX] tutorials = 0) ---
static std::set<std::string> s_tutDone;   // pestanas con "Entendido" en esta sesion
const char* TutorialForEn(const std::string& sec);   // lang.cpp
const char* TutorialForPt(const std::string& sec);   // lang.cpp
static const char* TutorialFor(const std::string& sec) {
    if (g_cfg.lang == 1) return TutorialForEn(sec);
    if (g_cfg.lang == 2) return TutorialForPt(sec);
    if (sec.rfind("Ilumina", 0) == 0) return
        "Como funciona: cada tarjeta es una mejora de luz con su interruptor (arriba a la derecha) y sus sliders. El (?) al lado de cada slider explica que hace. Ctrl+clic sobre un slider para escribir un numero.\n"
        "Por donde empezar: 1) 'Luz de borde en jugadores' y 'Luz de cielo' son las que mas se notan. 2) 'Atmosfera con sol dinamico' arregla el azul del fondo y el sol apagado en partidos de tarde. 3) 'Rebote de luz' (SSGI) es el mas caro en FPS: si el juego se traba, apagalo.\n"
        "Como probar: abri una repeticion o una cinematica de cerca, move un slider y mira al jugador; los shaders se actualizan al instante. 'Ver original' (arriba) compara con el juego sin el mod.";
    if (sec.rfind("Diagn", 0) == 0) return
        "Que hay aca: herramientas para ver que esta haciendo el mod, no para jugar. 'Vista de diagnostico' pinta informacion sobre la imagen (brujula del sol, grosor de la piel, rebote de luz). 'Probar todo el menu' toca cada grupo y mide si cambia la imagen.\n"
        "Como probar el menu: pausa una repeticion con jugadores de cerca, no toques la camara y apreta 'Probar todo el menu'. Tarda unos 30 segundos y te lista que grupos cambian la imagen en esa escena. 'Sin efecto' no siempre es un error: un grupo de piel no cambia nada si no hay piel en pantalla, y uno de noche no hace nada de dia.\n"
        "Consejo: cuando termines, volve la vista a 'Ninguna'.";
    if (sec.rfind("Sombras", 0) == 0) return
        "Que hay aca: el filtro del shadow map del juego (borde, nitidez, penumbra), las sombras de contacto (las finas que el shadow map no resuelve: cuello, brazo sobre el torso, botin contra el pasto) y los ajustes para el sol en movimiento del Daylight Engine.\n"
        "Por donde empezar: 1) 'Sombras de contacto': subi 'Largo' a 0.8 y mira el brazo sobre el cuerpo. 2) Si ves rayas con el sol bajo, 'Sol dinamico' > 'Sesgo por pendiente' mas alto. 3) Si la sombra flota bajo los pies, bajalo.\n"
        "Como probar: una repeticion con el sol de costado; el cuello bajo el menton es el mejor lugar para ver la diferencia.";
    if (sec.rfind("Piel", 0) == 0) return
        "Que hay aca: como la luz entra y se dispersa en la piel (subsuperficie), el brillo, el color en sombra y a contraluz.\n"
        "Por donde empezar: 1) 'Dispersion' da la piel blanda y natural; si la cara se ve roja o 'cerosa', bajala. 2) El brillo (especular) con cuidado: mucho = plastico. 3) Los cambios se ven mejor de cerca y a contraluz (orejas, nariz).\n"
        "Consejo: ajusta de dia y de noche, la luz de torres es mas fria y muestra otra cosa.";
    if (sec.rfind("Cara", 0) == 0) return
        "Que hay aca: detalle de la cara: arrugas de expresion, poros, ojos, boca y dientes.\n"
        "Por donde empezar: 1) 'Arrugas' se activan al gritar o esforzarse: proba en una celebracion. 2) Los ojos: brillo y humedad; poco es mas creible. 3) Si algo se ve 'pegado' o exagerado, baja la fuerza antes que apagarlo.\n"
        "Como probar: cinematica de gol o primer plano en la repeticion.";
    if (sec.rfind("Sudor", 0) == 0) return
        "Que hay aca: el sudor aparece con el tiempo de partido (reloj propio del mod): brillo, gotas, cuanto tarda y cuanto llega.\n"
        "Por donde empezar: 1) 'Velocidad' define a que minuto se nota. 2) 'Intensidad' el brillo maximo. 3) Ctrl+Shift+F10 reinicia el reloj para probar sin esperar.\n"
        "Consejo: en partidos con lluvia el juego ya moja a los jugadores; no sumes mucho sudor ahi.";
    if (sec.rfind("Pelo", 0) == 0) return
        "Que hay aca: definicion del pelo, brillo en banda (anisotropico, como en pelo real), reaccion al movimiento (inercia) y viento.\n"
        "Por donde empezar: 1) 'Reaccion del pelo' > 'Inercia' 1 = natural; 2 = pelo largo y pesado. 2) 'Brillo de pelo' > 'Fuerza de la banda' 0.6 es natural; si la banda aparece del lado contrario al sol, cambia 'Direccion de la luz' entre Mundo y Vista. 3) El viento del pelo copia la direccion del viento de la ropa.\n"
        "Como probar: un jugador de pelo largo corriendo en una repeticion lenta.";
    if (sec.rfind("Kits", 0) == 0) return
        "Que hay aca: la tela de los kits (tejido, brillo tipo 'sheen', luz envolvente), dorsales y escudos con relieve, y la sombra limpia sobre la ropa simulada.\n"
        "Por donde empezar: 1) 'Tejido next-gen' es el efecto principal: mira la camiseta de cerca. 2) Si aparecen franjas de sombra sobre la ropa que se mueve, 'Sombra limpia sobre los kits' mas alto. 3) El brillo de tela con cuidado: mucho = saten.\n"
        "Como probar: primer plano de la espalda de un jugador (dorsal) con sol de costado.";
    if (sec.rfind("Pasto", 0) == 0) return   // (11/11 m) seccion propia del pasto (antes dentro de Cancha y cielo)
        "Que hay aca: todo el pasto: el del mod (cerca y toda la cancha), el natural (alto en mm, hojas 3D de primerisimo plano y su relleno), color, luz, corte, viento, pisadas y desgaste.\n"
        "Por donde empezar: 1) 'Pasto natural' > 'Alto del pasto (mm)': 28 = cancha profesional. 2) Si de cerca se ve el suelo entre las hojas, subi 'Relleno bajo las hojas 3D' o 'Cantidad de hojas 3D'. 3) El pasto se ve mejor en camara baja.\n"
        "Consejo: si el pasto queda 'empastado', baja la variacion macro.";
    if (sec.rfind("Cancha", 0) == 0) return
        "Que hay aca: la cancha mojada (reflejos), las nubes, el cielo y el sol visible. El pasto tiene su propia seccion (Pasto).\n"
        "Por donde empezar: 1) Los cambios de cielo se notan mas de tarde. 2) Los reflejos de la cancha mojada solo con lluvia.\n"
        "Consejo: el rebote de luz verde del pasto hacia los jugadores esta en Iluminacion.";
    if (sec.rfind("Color", 0) == 0) return
        "Que hay aca: el color final de la imagen: tono, contraste, saturacion, calidez de sombras y luces.\n"
        "Por donde empezar: 1) Cambios chicos: 0.05 ya se nota. 2) Ajusta con una escena de dia y una de noche, no solo una. 3) 'Ver original' para no perder la referencia.\n"
        "Consejo: si tenes un preset de color en otro mod (PostProcessingEditor), no dupliques el mismo cambio aca.";
    if (sec.rfind("Post", 0) == 0) return
        "Que hay aca: efectos sobre la imagen terminada: bloom, nitidez, profundidad de campo, grano, vineta.\n"
        "Por donde empezar: 1) 'Nitidez' es lo que mas se nota en 1080p; en 4K bajala. 2) 'Bloom' con cuidado, lava la imagen. 3) 'Profundidad de campo' solo en repeticiones y cinematicas.\n"
        "Consejo: todo esto se suma a lo que haga DLSS/TAA del juego; si ves halos, baja la nitidez.";
    if (sec.rfind("Ropa", 0) == 0) return
        "Que hay aca: los valores del mod de fisica de ropa (cloth-mod, cloth.ini) editados en vivo: holgura, mangas, short, viento, gravedad, distancia de simulacion.\n"
        "Como funciona: cada slider escribe cloth.ini al soltarlo y el mod de ropa lo relee solo en ~1 s; no hace falta 'Guardar'. El boton '<' vuelve al valor que tenia al abrir el juego.\n"
        "Por donde empezar: 1) 'Viento' > 'Rafagas' mas bajo si la camiseta sube y baja sola. 2) 'Short' > rebote en un salto de cabeza. 3) Si no tenes el mod de ropa, esta pestana avisa y no hace nada.";
    if (sec == "Presets") return
        "Que es un preset: todos los interruptores y sliders del menu guardados para UN estadio. Cuando Sider avisa en que estadio jugas, el mod lo aplica solo; en un estadio sin preset vuelve a tus valores.\n"
        "Para crear uno: 1) juga en el estadio y ajusta lo que quieras. 2) Escribi nombre y autor. 3) 'Guardar preset para este estadio'. Se crea PES21FX\\presets\\stadium_<id>.ini.\n"
        "Para compartir: pasa ese archivo .ini; el otro lo copia en su PES21FX\\presets y listo. 'Aplicar' sirve para probar un preset en cualquier estadio; 'Quitar' vuelve a tus valores.\n"
        "Si dice 'Sin datos del partido': falta el modulo PES21FX_match.lua en Sider (viene en la carpeta sider del zip).";
    if (sec == "Avanzado") return
        "Que hay aca: los efectos por tecla (los mismos atajos del teclado, como botones) y la lista cruda de todos los parametros (x0..w127) que leen los shaders.\n"
        "Para que sirve: 1) probar un valor exacto que no esta en ningun slider. 2) ver que parametro mueve cada tarjeta (el nombre esta en el (?) de cada slider). 3) si algo queda raro, 'Recargar config' arriba vuelve a leer PES21FX.ini.\n"
        "Ojo: lo que cambies aca tambien se guarda con 'Guardar'.";
    return nullptr;
}
static void DrawTutorial(const std::string& sec, const std::string& shown) {
    if (!g_cfg.tutorials || s_tutDone.count(sec)) return;
    const char* t = TutorialFor(sec);
    if (!t) return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.16f, 0.22f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::BeginChild(("tut_" + sec).c_str(), ImVec2(0 * UIK(), 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    ImGui::TextColored(ImVec4(0.85f, 0.68f, 0.28f, 1), "Tutorial: %s", Utf8(shown).c_str());
    if (s_fontBold) ImGui::PopFont();
    std::string txt = t;
    size_t a = 0;
    while (a < txt.size()) {
        size_t b = txt.find('\n', a); if (b == std::string::npos) b = txt.size();
        ImGui::TextWrapped("%s", txt.substr(a, b - a).c_str());
        ImGui::Spacing();
        a = b + 1;
    }
    if (ImGui::Button(TRS("Entendido"), ImVec2(110 * UIK(), 0))) s_tutDone.insert(sec);
    ImGui::SameLine();
    if (ImGui::Button(TRS("No mostrar mas tutoriales"), ImVec2(220 * UIK(), 0))) { g_cfg.tutorials = false; SaveOption("tutorials", "0"); }
    ImGui::SameLine(); ImGui::TextDisabled("%s", TRS("(se vuelven a prender en Ajustes)"));
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

// --- pestana Presets: preset por estadio (se aplica solo cuando Sider informa el estadio) ---
static int s_presetPending = 0; static std::string s_presetPendingFile;
static char s_pName[96] = "", s_pAuthor[64] = "";
static bool s_pOnlyTime = false;
static std::vector<PresetFile> s_presets; static double s_presetsT = 0;
static std::string s_pStatus;
static void DrawPresetsTab() {
    // se llama con g_cfgMutex tomado por DrawMenu; las operaciones de archivo que toman el mutex se difieren
    static int pending = 0; static std::string pendingFile;   // 1 aplicar, 2 borrar, 3 quitar, 4 guardar
    const MatchInfo& m = g_cfg.match;
    ImGui::TextWrapped("%s", TRS("Un preset guarda todos los interruptores y sliders del menu para un estadio. Cuando Sider avisa en que estadio se juega, el mod lo aplica solo; al cambiar a un estadio sin preset vuelve a tus valores."));
    ImGui::Spacing();
    if (!m.known) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1), "%s", TRS("Sin datos del partido."));
        ImGui::TextWrapped("%s", TRS("Falta el modulo de Sider: copiar PES21FX_match.lua a la carpeta modules de Sider y agregar  lua.module = \"PES21FX_match.lua\"  en sider.ini. Hasta que empiece un partido tampoco hay datos."));
    } else if (m.stadium < 0) {
        ImGui::TextDisabled("%s", TRS("Todavia no empezo un partido (sin estadio)."));
    } else {
        ImGui::Text(TRS("Estadio actual: id %d"), m.stadium);
        ImGui::SameLine(); ImGui::TextDisabled("  %s  |  %s", TRS(m.timeofday == 1 ? "noche" : m.timeofday == 0 ? "dia" : "hora ?"), TRS(m.weather == 1 ? "lluvia" : m.weather == 0 ? "despejado" : "clima ?"));
    }
    if (!g_cfg.presetActive.file.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1), TRS("Preset activo: %s"), g_cfg.presetActive.name.empty() ? g_cfg.presetActive.file.c_str() : g_cfg.presetActive.name.c_str());
        if (!g_cfg.presetActive.author.empty()) { ImGui::SameLine(); ImGui::TextDisabled(TRS(" por %s"), g_cfg.presetActive.author.c_str()); }
        ImGui::SameLine();
        if (ImGui::SmallButton(TRS("Quitar (volver a mis valores)"))) pending = 3;
        ImGui::TextDisabled("%s", TRS("Ojo: con un preset activo, el boton Guardar de arriba escribe estos valores como tuyos."));
    } else {
        ImGui::TextDisabled("%s", TRS("Preset activo: ninguno (tus valores)."));
    }
    ImGui::Separator();
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    ImGui::TextUnformatted(TRS("Guardar los valores actuales como preset de este estadio"));
    if (s_fontBold) ImGui::PopFont();
    ImGui::SetNextItemWidth(300 * UIK()); ImGui::InputText(TRS("Nombre"), s_pName, sizeof(s_pName));
    ImGui::SetNextItemWidth(300 * UIK()); ImGui::InputText(TRS("Autor"), s_pAuthor, sizeof(s_pAuthor));
    ImGui::Checkbox(TRS("Solo para esta hora (dia / noche)"), &s_pOnlyTime);
    ImGui::SameLine(); ImGui::TextDisabled("(?)"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Marcado: el preset vale solo de dia o solo de noche (segun el partido actual).\nSin marcar: vale para el estadio a cualquier hora."));
    ImGui::BeginDisabled(m.stadium < 0);
    if (ImGui::Button(TRS("Guardar preset para este estadio"), ImVec2(280 * UIK(), 0))) pending = 4;
    ImGui::EndDisabled();
    if (!s_pStatus.empty()) ImGui::TextDisabled("%s", s_pStatus.c_str());
    ImGui::Separator();
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    ImGui::TextUnformatted(TRS("Presets instalados"));
    if (s_fontBold) ImGui::PopFont();
    ImGui::TextDisabled("%s", TRS("Carpeta PES21FX\\presets. Para compartir uno: pasar el archivo; el otro lo copia ahi y listo."));
    if (Now() - s_presetsT > 2.0) { s_presetsT = Now(); ScanPresets(s_presets); }
    if (s_presets.empty()) ImGui::TextDisabled("%s", TRS("(ninguno)"));
    int id = 0;
    for (auto& p : s_presets) {
        ImGui::PushID(id++);
        bool active = p.file == g_cfg.presetActive.file;
        if (active) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.85f, 0.55f, 1));
        ImGui::Text("%s", p.name.empty() ? p.file.c_str() : p.name.c_str());
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine(); ImGui::TextDisabled(TRS("  %s%s%s  estadio %d%s  (%d valores)"), p.author.empty() ? "" : TRS("por "), p.author.c_str(), p.author.empty() ? "" : "  |", p.stadium, TRS(p.timeofday == 1 ? " noche" : p.timeofday == 0 ? " dia" : ""), (int)p.values.size());
        ImGui::SameLine(ImGui::GetWindowWidth() - 150);
        if (ImGui::SmallButton(TRS("Aplicar"))) { pending = 1; pendingFile = p.file; }
        ImGui::SameLine();
        if (ImGui::SmallButton(TRS("Borrar"))) { pending = 2; pendingFile = p.file; }
        ImGui::PopID();
    }
    // acciones diferidas: se ejecutan despues de soltar g_cfgMutex (ver DrawMenu)
    if (pending) {
        s_presetPending = pending; s_presetPendingFile = pendingFile; pending = 0;
    }
}

// --- (04/11 n) menu nuevo: barra lateral + pagina ---
static int s_toolReq = 0;          // acciones de Herramientas que se hacen sin g_cfgMutex: 1 capturar, 2 detener catalogo, 3 cbuffers, 4 pasto, 5 grabar, 6 detener grabacion
static bool s_saveReq = false;
static void SelectPage(const std::string& key) {
    if (s_sel == key) return;
    s_sel = key; g_cfg.menuSection = key; SaveOption("menu_seccion", key);
}
static void NavHeader(const char* txt) {
    if (s_rail) {   // (08/11) solo iconos: una rayita en lugar del titulo
        ImGui::Dummy(ImVec2(0, 3.0f * UIK()));
        const ImVec2 p = ImGui::GetCursorScreenPos(); const float w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + w * 0.2f, p.y), ImVec2(p.x + w * 0.8f, p.y), IM_COL32(70, 76, 88, 255), 1.0f);
        ImGui::Dummy(ImVec2(0, 4.0f * UIK()));
        return;
    }
    ImGui::Dummy(ImVec2(0, 4.0f * UIK()));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.47f, 0.50f, 0.56f, 1));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f * UIK());
    ImGui::TextUnformatted(txt);
    ImGui::PopStyleColor();
}
static void NavItem(const std::string& key, unsigned ico, const std::string& label, const std::string& right, bool dot) {
    const bool sel = s_sel == key;
    ImGui::PushID(key.c_str());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight();
    // (08/11) el fondo de la pagina elegida lo dibuja DrawMenu y se desliza de un item a otro; el de pasar el mouse aparece suave
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
    const float hh = s_rail ? h * 1.25f : h;
    if (ImGui::Selectable("##nav", false, ImGuiSelectableFlags_None, ImVec2(w, hh))) SelectPage(key);
    ImGui::PopStyleColor(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        const float ha = HoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered() && !sel);
        if (ha > 0.01f) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + hh), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.07f * ha)), 6.0f * UIK());
        if (sel) { s_navTgtY = p.y - ImGui::GetWindowPos().y + ImGui::GetScrollY(); s_navH = hh; }
    }
    const float fs = ImGui::GetFontSize(), ty = p.y + (h - fs) * 0.5f, pad = 8.0f * UIK();
    if (s_rail) {   // (08/11) barra angosta: el icono centrado, el nombre en el aviso
        const float hv = HoverAnim(ImGui::GetItemID() + 3, ImGui::IsItemHovered() || sel, 10.0f);
        const std::string ic = Ico(ico); const ImVec2 is = ImGui::CalcTextSize(ic.c_str());
        GlassIcon(dl, ImVec2(p.x + (w - is.x) * 0.5f, p.y + (hh - fs) * 0.5f - 1.5f * UIK() * hv), ic.c_str(), sel ? ImVec4(0.40f, 0.90f, 0.62f, 1) : Mix(ImVec4(0.72f, 0.76f, 0.82f, 1), ImVec4(0.85f, 0.95f, 0.90f, 1), hv), sel ? 1.0f : hv * 0.6f);   // (08/11 d) vidrio, sube un poco al pasar el mouse
        if (dot) dl->AddCircleFilled(ImVec2(p.x + w - 6.0f * UIK(), p.y + 6.0f * UIK()), 3.5f * UIK(), IM_COL32(242, 204, 89, 255));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s%s", label.c_str(), right.empty() ? "" : "  ", right.c_str());
        ImGui::PopID();
        return;
    }
    const ImU32 cText = ImGui::GetColorU32(sel ? ImVec4(1, 1, 1, 1) : ImVec4(0.86f, 0.88f, 0.91f, 1));
    const ImU32 cDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 cIco = ImGui::GetColorU32(sel ? ImVec4(0.40f, 0.90f, 0.62f, 1) : ImVec4(0.55f, 0.60f, 0.66f, 1));
    const ImVec2 rs = right.empty() ? ImVec2(0, 0) : ImGui::CalcTextSize(right.c_str());
    float x = p.x + pad;
    if (s_iconsOk && ico) { const std::string ic = Ico(ico); const float hv = HoverAnim(ImGui::GetItemID() + 3, ImGui::IsItemHovered(), 10.0f);
        GlassIcon(dl, ImVec2(x + 2.0f * UIK() * hv, ty), ic.c_str(), sel ? ImVec4(0.40f, 0.90f, 0.62f, 1) : Mix(ImVec4(0.55f, 0.60f, 0.66f, 1), ImVec4(0.80f, 0.90f, 0.86f, 1), hv), sel ? 0.9f : 0.0f); (void)cIco; x += fs * 1.55f; }   // (08/11 d) iconos de vidrio
    const float xr = p.x + w - pad - rs.x - (dot ? 12.0f * UIK() : 0.0f);
    const ImVec4 clip(x, p.y, std::max(x, xr - 4.0f * UIK()), p.y + h);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(x, ty), cText, label.c_str(), nullptr, 0.0f, &clip);
    if (dot) dl->AddCircleFilled(ImVec2(p.x + w - pad - rs.x - 7.0f * UIK(), p.y + h * 0.5f), 3.5f * UIK(), IM_COL32(242, 204, 89, 255));
    if (!right.empty()) dl->AddText(ImVec2(p.x + w - pad - rs.x, ty), cDim, right.c_str());
    ImGui::PopID();
}
// barra de cada pagina de tarjetas: solo activos, contraer / expandir. (08/11) Con iconos va a la derecha del titulo.
static bool IconToggle(const char* id, unsigned ico, bool on, const char* tip) { return GlassIconButton(id, ico, on, tip); }   // (08/11 d) de vidrio
static float CardsToolbarW() { return s_iconsOk ? ImGui::GetFrameHeight() * 1.25f * 3 + ImGui::GetStyle().ItemSpacing.x * 2 : 0.0f; }
static void CardsToolbar() {
    if (s_iconsOk) {
        if (IconToggle("solo", kIcoFilter, s_onlyActive, TRS("Solo activos: mostrar solo los grupos con el interruptor prendido"))) s_onlyActive = !s_onlyActive;
        ImGui::SameLine(); if (IconToggle("cont", kIcoCompress, false, TRS("Contraer todo"))) s_collapseAll = 1;
        ImGui::SameLine(); if (IconToggle("exp", kIcoExpand, false, TRS("Expandir todo"))) s_collapseAll = 2;
        return;
    }
    ImGui::Checkbox(TRS("Solo activos"), &s_onlyActive);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Mostrar solo los grupos con el interruptor prendido"));
    ImGui::SameLine(); if (ImGui::SmallButton(TRS("Contraer todo"))) s_collapseAll = 1;
    ImGui::SameLine(); if (ImGui::SmallButton(TRS("Expandir todo"))) s_collapseAll = 2;
}
static void PageTitle(unsigned ico, const std::string& title, const char* sub = nullptr, bool tools = false) {
    const float y0 = ImGui::GetCursorPosY();
    if (s_fontBig) ImGui::PushFont(s_fontBig);
    if (s_iconsOk && ico) {   // (08/11 d) icono de vidrio con brillo
        const std::string ic = Ico(ico); const ImVec2 is = ImGui::CalcTextSize(ic.c_str());
        GlassIcon(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), ic.c_str(), ImVec4(0.40f, 0.90f, 0.62f, 1), 0.8f);
        ImGui::Dummy(is); ImGui::SameLine(); }
    const float tw = tools ? CardsToolbarW() : 0.0f;
    ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - (tw > 0 ? tw + 8.0f * UIK() : 0.0f));
    ImGui::TextWrapped("%s", title.c_str());
    ImGui::PopTextWrapPos();
    if (s_fontBig) ImGui::PopFont();
    const float y1 = ImGui::GetCursorPosY();
    if (tools && s_iconsOk) { ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - tw, y0)); CardsToolbar(); ImGui::SetCursorPosY(y1); }
    if (sub) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); ImGui::TextWrapped("%s", sub); ImGui::PopStyleColor(); }
    if (tools && !s_iconsOk) CardsToolbar();
    ImGui::Dummy(ImVec2(0, 2.0f * UIK()));
}
// --- (08/11) diagnostico automatico (Agustin: "agrega mas opciones de diagnostico automatico") ---
// 1) rendimiento en vivo: cuadros por segundo, el 1% mas lento y tirones (cuadros de mas de 50 ms), medido en cada Present;
// 2) revision: shaders, archivos del menu, valores del ini fuera de rango o sin fila, camara libre guardada prendida, el pasto
//    del mod, otras DLL de la carpeta del juego enganchadas al proceso (Cloth Mod, ReShade...) y la memoria de la placa;
// 3) "Generar informe": una carpeta PES21FX\informes\<fecha>\ con el log, el ini, sider.log, cloth.log, el ultimo cierre y
//    resumen.txt con todo lo de arriba, para mandar de una.
static float s_ft[3600]; static int s_ftN = 0, s_ftI = 0;   // tiempos de cuadro (ms), los ultimos ~60 s
static double s_ftLast = 0;
static void FrameTimeTick() {
    const double t = Now();
    if (s_ftLast > 0) { const float ms = (float)((t - s_ftLast) * 1000.0); if (ms > 0 && ms < 5000) { s_ft[s_ftI] = ms; s_ftI = (s_ftI + 1) % 3600; if (s_ftN < 3600) s_ftN++; } }
    s_ftLast = t;
}
struct PerfStats { float fpsNow = 0, fpsAvg = 0, low1 = 0, worst = 0; int stutters = 0, n = 0; };
static PerfStats PerfNow() {
    PerfStats p; if (s_ftN < 10) return p;
    std::vector<float> v; v.reserve(s_ftN); double sum = 0, sumNow = 0; int nNow = 0;
    for (int k = 0; k < s_ftN; k++) { const float ms = s_ft[(s_ftI - 1 - k + 3600) % 3600]; v.push_back(ms); sum += ms; if (k < 30) { sumNow += ms; nNow++; } if (ms > 50.0f) p.stutters++; }
    std::sort(v.begin(), v.end());
    const size_t i99 = std::min(v.size() - 1, (size_t)(v.size() * 0.99));
    p.n = (int)v.size(); p.fpsAvg = (float)(1000.0 * v.size() / sum); p.fpsNow = nNow ? (float)(1000.0 * nNow / sumNow) : 0; p.low1 = 1000.0f / v[i99]; p.worst = v.back();
    return p;
}
struct DiagItem { int level; std::string text; };   // 0 ok, 1 aviso, 2 problema
static std::vector<DiagItem> s_diag; static double s_diagT = -10;
static std::wstring GameDir() { wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); std::wstring d = exe; return d.substr(0, d.find_last_of(L"\\/") + 1); }
static std::string W2U(const std::wstring& w) { int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string o(n > 0 ? n - 1 : 0, '\0'); if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &o[0], n, nullptr, nullptr); return o; }
static bool FileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static void DiagRun() {   // con g_cfgMutex tomado
    std::vector<DiagItem> d; char b[512];
    {   const int rep = g_replacedCount.load(), fail = g_failedCount.load(); int nErr; { std::lock_guard<std::mutex> lk(g_errMutex); nErr = (int)g_compileErrors.size(); }
        snprintf(b, sizeof b, TRS("Shaders propios: %d activos, %d con error de compilacion"), rep, std::max(fail, nErr)); d.push_back({ (fail || nErr) ? 2 : 0, b }); }
    {   const wchar_t* files[] = { L"textures\\pes_icons.ttf", L"textures\\pes_ui.ttf", L"textures\\pes_ui_bold.ttf", L"PES21FX_menu_en.ini", L"PES21FX_menu_pt.ini", L"PES21FX_rules.ini" };
        std::string miss; for (auto f : files) if (!FileExists(g_cfg.baseDir + f)) { if (!miss.empty()) miss += ", "; miss += W2U(f); }
        if (miss.empty()) d.push_back({ 0, TRS("Archivos del menu: completos") }); else { snprintf(b, sizeof b, TRS("Faltan archivos: %s"), miss.c_str()); d.push_back({ 1, b }); } }
    {   int out = 0, undef = 0; std::string ex, exU;
        for (auto& g : g_cfg.groups) for (auto& sl : g.sliders) {
            if (!sl.ext.empty() || !sl.optVals.empty() || !sl.options.empty()) continue;
            const float v = g_cfg.base[sl.idx][sl.comp]; char nm[16]; snprintf(nm, sizeof nm, "%c%d", "xyzw"[sl.comp], sl.idx);
            if (!g_cfg.info[sl.idx][sl.comp].defined) { if (undef++ < 3) exU += std::string(exU.empty() ? "" : ", ") + nm; continue; }
            if (v < std::min(sl.lo, sl.hi) - 1e-4f || v > std::max(sl.lo, sl.hi) + 1e-4f) { if (out++ < 3) ex += std::string(ex.empty() ? "" : ", ") + nm; }
        }
        if (!out) d.push_back({ 0, TRS("Valores del ini dentro del rango de sus sliders") });
        else { snprintf(b, sizeof b, TRS("%d valores del ini fuera del rango de su slider (%s...): al guardar se recortan"), out, ex.c_str()); d.push_back({ 1, b }); }
        if (undef) { snprintf(b, sizeof b, TRS("%d sliders sin su fila en el ini (%s...): usan 0 hasta que se guarden"), undef, exU.c_str()); d.push_back({ 1, b }); } }
    {   std::vector<std::string> sus; std::string others;
        const std::wstring gd = GameDir(); std::wstring gl = gd; for (auto& ch : gl) ch = (wchar_t)towlower(ch);
        HMODULE mods[1024]; DWORD need = 0;
        if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need)) {
            HMODULE self = nullptr; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&DiagRun, &self);
            for (int i = 0; i < (int)(need / sizeof(HMODULE)) && i < 1024; i++) {
                if (mods[i] == self || mods[i] == GetModuleHandleW(nullptr)) continue;
                wchar_t path[MAX_PATH]; if (!GetModuleFileNameW(mods[i], path, MAX_PATH)) continue;
                std::wstring lp = path; for (auto& ch : lp) ch = (wchar_t)towlower(ch);
                if (lp.rfind(gl, 0) != 0) continue;   // solo las de la carpeta del juego (las de Windows no)
                const std::string nm = W2U(lp.substr(gl.size()));
                if (!others.empty()) others += ", "; others += nm;
                if (nm.find("dsound") != std::string::npos || nm.find("reshade") != std::string::npos || nm.find("dxgi") != std::string::npos || nm.find("d3d") != std::string::npos || nm.find("specialk") != std::string::npos) sus.push_back(nm);
            }
        }
        if (!sus.empty()) { std::string l; for (auto& x : sus) l += (l.empty() ? "" : ", ") + x; snprintf(b, sizeof b, TRS("Otros mods de graficos enganchados: %s. Si hay cierres, probar sin ellos (el Cloth Mod con F5 apagado en el editor)."), l.c_str()); d.push_back({ 1, b }); }
        if (!others.empty()) { snprintf(b, sizeof b, TRS("DLL de la carpeta del juego cargadas: %s"), others.substr(0, 380).c_str()); d.push_back({ 0, b }); } }
    if (g_cfg.base[344][0] > 0.5f) d.push_back({ 1, TRS("La camara libre estaba guardada prendida en el ini (ahora arranca apagada siempre)") });
    else d.push_back({ 0, TRS("Camara libre: arranca apagada") });
    if (g_grassDbg.why && g_grassDbg.patches == 0 && g_cfg.live[93][3] > 0.5f) { snprintf(b, sizeof b, TRS("Pasto del mod sin dibujar en el partido: %s"), g_grassDbg.why); d.push_back({ 1, b }); }
    {   const PerfStats p = PerfNow();
        if (p.n >= 60) {
            snprintf(b, sizeof b, TRS("Rendimiento: %.0f cuadros/s de promedio, el 1%% mas lento a %.0f, %d tirones (>50 ms) en %.0f s"), p.fpsAvg, p.low1, p.stutters, p.n / std::max(1.0f, p.fpsAvg));
            d.push_back({ (p.stutters > 5 || p.low1 < p.fpsAvg * 0.5f) ? 1 : 0, b });
        } }
    {   const std::string gm = GpuMeterSummary(); if (!gm.empty()) d.push_back({ 0, gm.substr(0, 400) }); }
    s_diag.swap(d); s_diagT = Now();
}
static std::string DiagText() {
    std::string t; const char* lv[3] = { "OK     ", "AVISO  ", "PROBLEMA " };
    for (auto& x : s_diag) { t += lv[std::clamp(x.level, 0, 2)]; t += x.text; t += "\r\n"; }
    return t;
}
std::string DiagReportText() { std::lock_guard<std::mutex> lk(g_cfgMutex); DiagRun(); return DiagText(); }   // (08/11) para la prueba automatica
static std::string MakeReport() {   // sin g_cfgMutex
    SYSTEMTIME st; GetLocalTime(&st); wchar_t n[32];
    swprintf(n, 32, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::wstring root = g_cfg.baseDir + L"informes\\"; CreateDirectoryW(root.c_str(), nullptr);
    const std::wstring dir = root + n + L"\\"; if (!CreateDirectoryW(dir.c_str(), nullptr)) return TRS("No se pudo crear la carpeta del informe");
    int nf = 0;
    auto cp = [&](const std::wstring& from, const wchar_t* to) { if (CopyFileW(from.c_str(), (dir + to).c_str(), FALSE)) nf++; };
    cp(g_cfg.baseDir + L"PES21FX.log", L"PES21FX.log"); cp(g_cfg.baseDir + L"PES21FX.anterior.log", L"PES21FX.anterior.log");
    cp(g_cfg.baseDir + L"PES21FX.ini", L"PES21FX.ini"); cp(GameDir() + L"sider.log", L"sider.log");
    if (!g_cfg.clothPath.empty()) { std::wstring cd = g_cfg.clothPath.substr(0, g_cfg.clothPath.find_last_of(L"\\/") + 1); cp(cd + L"cloth.log", L"cloth.log"); cp(g_cfg.clothPath, L"cloth.ini"); }
    {   // el ultimo cierre (sin el .dmp, que pesa cientos de MB)
        std::vector<std::wstring> v; WIN32_FIND_DATAW fd; const std::wstring cr = g_cfg.baseDir + L"cierres\\";
        HANDLE f = FindFirstFileW((cr + L"2*").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) { do { if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) v.push_back(fd.cFileName); } while (FindNextFileW(f, &fd)); FindClose(f); }
        if (!v.empty()) { std::sort(v.begin(), v.end()); const std::wstring last = cr + v.back() + L"\\";
            cp(last + L"PES21FX.log", L"ultimo_cierre_PES21FX.log"); cp(last + L"sider.log", L"ultimo_cierre_sider.log"); }
    }
    std::string sum;
    { std::lock_guard<std::mutex> lk(g_cfgMutex); DiagRun(); sum = DiagText(); }
    sum += "\r\n" + LightMeterSummary() + "\r\n";
    { std::lock_guard<std::mutex> lk2(g_errMutex); for (auto& e : g_compileErrors) sum += "error de shader: " + e + "\r\n"; }
    if (FILE* fp = _wfopen((dir + L"resumen.txt").c_str(), L"wb")) { fwrite(sum.data(), 1, sum.size(), fp); fclose(fp); nf++; }
    Log("informe: PES21FX\\informes\\%ls\\ (%d archivos)", n, nf);
    char b[200]; snprintf(b, sizeof b, TRS("Informe en PES21FX\\informes\\%ls (%d archivos)"), n, nf); return b;
}
static bool s_reportReq = false;
static void DrawDiagPanel() {   // con g_cfgMutex tomado
    if (Now() - s_diagT > 2.0) DiagRun();
    const PerfStats p = PerfNow();
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    ImGui::TextUnformatted(IcoL(kIcoDiag, TRS("Revision automatica")).c_str());
    if (s_fontBold) ImGui::PopFont();
    if (p.n >= 10) {   // medidor en vivo
        ImGui::TextColored(p.fpsNow >= 55 ? ImVec4(0.45f, 0.88f, 0.55f, 1) : p.fpsNow >= 40 ? ImVec4(0.95f, 0.80f, 0.35f, 1) : ImVec4(0.95f, 0.50f, 0.40f, 1), TRS("%.0f cuadros/s ahora"), p.fpsNow);
        ImGui::SameLine(); ImGui::TextDisabled(TRS("   promedio %.0f   1%% mas lento %.0f   peor cuadro %.0f ms   tirones %d"), p.fpsAvg, p.low1, p.worst, p.stutters);
        // grafico de los ultimos ~4 s
        const int n = std::min(s_ftN, 240); float vals[240];
        for (int k = 0; k < n; k++) vals[k] = s_ft[(s_ftI - n + k + 3600) % 3600];
        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.35f, 0.85f, 0.58f, 1)); ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0.25f));
        ImGui::PlotLines("##ft", vals, n, 0, nullptr, 0.0f, 50.0f, ImVec2(ImGui::GetContentRegionAvail().x, 46.0f * UIK()));
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Tiempo de cada cuadro (ms) de los ultimos segundos: abajo es mejor; 16.7 = 60 cuadros/s, picos = tirones"));
    }
    for (auto& x : s_diag) {
        const ImVec4 c = x.level == 0 ? ImVec4(0.45f, 0.88f, 0.55f, 1) : x.level == 1 ? ImVec4(0.95f, 0.80f, 0.35f, 1) : ImVec4(0.95f, 0.50f, 0.40f, 1);
        ImGui::TextColored(c, "%s", s_iconsOk ? Ico(x.level == 0 ? kIcoDot : kIcoWarn).c_str() : (x.level == 0 ? "OK" : "!"));
        ImGui::SameLine(); ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(Utf8(x.text).c_str()); ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    if (ImGui::Button(IcoL(kIcoSave, TRS("Generar informe")).c_str())) s_reportReq = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Junta en PES21FX\\informes\\<fecha> el log, el ini, sider.log, cloth.log, el ultimo cierre y un resumen de esta revision, para mandarlo de una."));
    ImGui::SameLine(); if (ImGui::Button(IcoL(kIcoReload, TRS("Revisar de nuevo")).c_str())) s_diagT = -10;
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
}

static void DrawSectionExtras(const std::string& sname) {
    if (sname.rfind("Ropa", 0) == 0 && !g_cfg.clothStatus.empty()) { ImGui::TextDisabled("%s", g_cfg.clothStatus.c_str()); ImGui::Spacing(); }
    if (sname.rfind("Diagn", 0) == 0) {
        DrawDiagPanel();
        // (08/11) depuracion: las vistas prendidas y un boton para apagarlas todas (nunca se guardan prendidas)
        {   const int nOn = DebugActiveCount();
            if (s_fontBold) ImGui::PushFont(s_fontBold);
            ImGui::TextUnformatted(IcoL(kIcoLayers, TRS("Vistas de depuracion")).c_str());
            if (s_fontBold) ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", TRS("Cascadas del sol en colores, solo la sombra, rebote, piel, pasto, cejas, boca... estan en \"Vista de diagnostico\" (abajo) y en cada tarjeta. Ninguna se guarda prendida: al reiniciar el juego arrancan apagadas."));
            ImGui::PopStyleColor();
            if (nOn) {
                for (int i = 0; i < DebugParamCount(); i++) { const DebugParam& d = DebugParamAt(i); const float v = g_cfg.live[d.idx][d.comp];
                    if (DebugIsOn(d, v)) ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), TRS("prendida: %s (%c%d = %g)"), TRS(d.name), "xyzw"[d.comp], d.idx, v); }
                if (ImGui::Button(IcoL(kIcoX, TRS("Apagar todo el debug")).c_str())) DebugAllOff();
            } else ImGui::TextColored(ImVec4(0.45f, 0.88f, 0.55f, 1), "%s", TRS("Ninguna vista de depuracion prendida"));
            { bool sk = g_cfg.live[426][0] > 0.5f; if (ImGui::Checkbox(TRS("Ver el esqueleto (huesos en escena)"), &sk)) g_cfg.live[426][0] = sk ? 1.0f : 0.0f; }   // (09/11 f)
            {   // (11/11 3icxbh) vistas del motor (fila 429): las mismas que maneja el Motor en tiempo real desde Sider (motorlink.cpp)
                static const char* kVm[] = { "Normal", "Luz difusa", "Luz especular", "Normales", "Normales de mundo", "Albedo (sin luz)", "Profundidad", "Oclusion", "Sombra del sol", "Solo luz (sin texturas)", "Material" };
                int vm = (int)(g_cfg.live[429][0] + 0.5f); if (vm < 0 || vm > 10) vm = 0;
                ImGui::SetNextItemWidth(220.0f * UIK());
                if (ImGui::BeginCombo(TRS("Vista del motor"), TRS(kVm[vm]))) {
                    for (int i = 0; i <= 10; i++) if (ImGui::Selectable(TRS(kVm[i]), i == vm)) g_cfg.live[429][0] = (float)i;
                    ImGui::EndCombo(); }
                bool wf = g_cfg.live[429][1] > 0.5f; if (ImGui::Checkbox(TRS("Vista del motor: alambre"), &wf)) g_cfg.live[429][1] = wf ? 1.0f : 0.0f;
                if (vm > 0) { float ct = g_cfg.live[429][2]; ImGui::SetNextItemWidth(220.0f * UIK());
                    if (ImGui::SliderFloat(TRS("Pantalla partida (0 = toda la pantalla)"), &ct, 0.0f, 1.0f, "%.2f")) g_cfg.live[429][2] = ct; }
            }
            ImGui::Spacing();
        }
        if (s_fontBold) ImGui::PushFont(s_fontBold);
        ImGui::TextUnformatted(IcoL(kIcoDiag, TRS("Prueba automatica")).c_str());
        if (s_fontBold) ImGui::PopFont();
        ImGui::TextWrapped("%s", TRS("Apaga un momento cada efecto prendido, mide cuantos ms cuesta (CPU y placa), si cambia la imagen y saca una foto; despues prende una por una las vistas de depuracion y las fotografia. Al final deja todo como estaba (no guarda nada en el ini) y escribe PES21FX\\diagnostico\\<fecha>\\ con resumen.txt, informe.json y las fotos, para que Claude lo lea. Con la camara quieta (repeticion en pausa o el editor); tarda 2 a 4 minutos. Esc la corta."));
        if (!MenuTestActive()) { if (ImGui::Button(IcoL(kIcoDiag, TRS("Empezar la prueba")).c_str(), ImVec2(0, 0))) MenuTestStart();
            // (11/11) buscar el cuelgue: para el reinicio de la placa al sacar "Ver original" en el partido (menutest.cpp MenuHangStart)
            ImGui::SameLine(); if (ImGui::Button(IcoL(kIcoDiag, TRS("Buscar el cuelgue")).c_str(), ImVec2(0, 0))) MenuHangStart();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Para cuando la placa se reinicia con el mod prendido. Apaga todos los efectos, saca 'Ver original' y los vuelve a prender de a uno cada 4 segundos, anotando cada paso en el log antes de hacerlo. Si la placa se reinicia, el ultimo efecto anotado es el culpable. No guarda nada; con Esc se corta y todo vuelve a como estaba."));
            // (11/11) la busqueda general dio "Cejas con hebras": ahora cual de sus opciones (hebras v3, pasada propia, sombra, barba...)
            ImGui::SameLine(); if (ImGui::Button(IcoL(kIcoDiag, TRS("Buscar dentro de: Cejas con hebras")).c_str(), ImVec2(0, 0))) MenuHangStartGroup(74, 0);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Deja el resto del mod como esta, pone en 0 las opciones de Cejas con hebras, prende la tarjeta y devuelve cada opcion de a una cada 4 segundos, anotando antes cada paso. Si la placa se reinicia, la ultima opcion anotada es la culpable. No guarda nada.")); }
        else { if (ImGui::Button(IcoL(kIcoX, TRS("Cancelar")).c_str(), ImVec2(0, 0))) MenuTestStop();
               ImGui::SameLine(); ImGui::ProgressBar(MenuTestProgress(), ImVec2(std::max(120.0f * UIK(), ImGui::GetContentRegionAvail().x), 0)); }
        ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(Utf8(MenuTestStatus()).c_str()); ImGui::PopTextWrapPos();
        size_t nres = MenuTestResults();
        if (nres && !MenuTestActive()) {
            ImGui::TextDisabled(TRS("ruido de la escena: %.4f (si es alto, la camara se movio: pausa y repeti)"), MenuTestNoise());
            if (ImGui::BeginTable("mt", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImVec2(0, 260.0f * UIK()))) {
                for (size_t i = 0; i < nres; i++) {
                    std::string grp, sec; float df, cpu, gpu; int st;
                    if (!MenuTestResult_(i, grp, sec, df, st, cpu, gpu)) break;
                    if (st >= 2) continue;
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(Utf8(sec).c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(Utf8(grp).c_str());
                    ImGui::TableSetColumnIndex(2);
                    if (st == 1) ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1), TRS("cambia la imagen (%.4f)"), df);
                    else ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1), TRS("sin efecto en esta escena (%.4f)"), df);
                    ImGui::TableSetColumnIndex(3);
                    if (cpu != 0 || gpu != 0) ImGui::TextDisabled(TRS("cuesta %.2f ms CPU / %.2f ms placa"), cpu, gpu);
                }
                ImGui::EndTable();
            }
        }
        ImGui::Spacing();
    }
    if (sname.rfind("Ilumin", 0) == 0) { ImGui::TextDisabled("%s", LightMeterSummary().c_str()); ImGui::Spacing(); }
    if (sname.rfind("Diagn", 0) == 0 || sname.rfind("Debug", 0) == 0) { const std::string gm = GpuMeterSummary();   // (07/11) medidor de VRAM y de ms de GPU (x375)
        if (!gm.empty()) { ImGui::TextWrapped("%s", gm.c_str()); ImGui::Spacing(); } }
    if (sname.rfind("Pasto", 0) == 0) { ImGui::TextDisabled(TRS("Pasto: parcelas lejanas del juego escaladas: %d en 3 s. Extendido: %d parcelas/cuadro (anillos %d/%d/%d), variante %u idx, camara %s (%.0f, %.0f), estado: %s"), g_grassDbg.sparseLast, g_grassDbg.patches, g_grassDbg.ring[0], g_grassDbg.ring[1], g_grassDbg.ring[2], g_grassDbg.lod, g_grassDbg.cam ? "ok" : "no", g_grassDbg.ex, g_grassDbg.ez, g_grassDbg.why ? g_grassDbg.why : "-"); ImGui::Spacing(); }
}
static void SectionLabel(const MenuGroup& g) {
    int cat, ord; unsigned ico; SecInfo(SecKey(g), cat, ord, ico);
    const std::string s = Utf8(g.section.empty() ? std::string(TRS("Otros")) : LS(g.section, g.sectionEn));
    ImGui::TextDisabled("%s", (s_iconsOk ? Ico(ico) + "  " + s : s).c_str());
}
static void EmptyState(const char* txt) { ImGui::Dummy(ImVec2(0, 20.0f * UIK())); ImGui::TextDisabled("%s", txt); }
// (08/11) tarjetas en una o dos columnas: dos si la pagina es ancha (Ajustes: menu_columnas). Reparto alternado (izquierda, derecha,
// izquierda...) para que abrir o cerrar una tarjeta no mueva las demas de columna. Con 'labels' se pone el nombre de la seccion arriba
// de cada tarjeta cuando cambia (paginas que mezclan secciones).
static void DrawCards(const std::vector<int>& ids, bool labels) {
    const float W = ImGui::GetContentRegionAvail().x;
    const bool two = g_cfg.menuCols && W >= 1000.0f * UIK() && ids.size() > 1;
    const int nc = two ? 2 : 1;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 1.5f, cw = two ? (W - gap) * 0.5f : W;
    for (int c = 0; c < nc; c++) {
        if (c) ImGui::SameLine(0, gap);
        if (two) ImGui::BeginChild(c ? "colR" : "colL", ImVec2(cw, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        std::string lastSec;
        for (size_t i = c; i < ids.size(); i += nc) {
            MenuGroup& g = g_cfg.groups[ids[i]];
            if (labels && SecKey(g) != lastSec) { SectionLabel(g); lastSec = SecKey(g); }
            DrawGroup(g, ids[i]);
        }
        if (two) ImGui::EndChild();
    }
}

static void DrawToolsPage() {
    PageTitle(kIcoTools, TRS("Herramientas"), TRS("Para ver que hace el mod y pasarle datos a Claude; no hacen falta para jugar."));
    ImGui::Spacing();
    if (ImGui::CollapsingHeader(TRS("Inspector y catalogo de shaders"), ImGuiTreeNodeFlags_DefaultOpen)) {
        { bool ins = g_inspectOn.load(); if (ins) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.25f, 0.2f, 1));
          if (ImGui::Button(ins ? "Inspector: ON" : "Inspector", ImVec2(130 * UIK(), 0))) { g_inspectOn = !ins; s_status = TRS(ins ? "Inspector apagado" : "Inspector visual: flechas sobre cada parte conocida; clic en una ficha para ver sus texturas"); }
          if (ins) ImGui::PopStyleColor();
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Modo debug visual: marca en pantalla cara, pelo, kit, medias, botines, pasto y cielo, que hace el mod en cada uno y de donde sale cada textura (juego / archivo / generada).")); }
        ImGui::SameLine();
        if (!g_catalogActive) {
            if (ImGui::Button(TRS("Capturar shaders"), ImVec2(160 * UIK(), 0))) s_toolReq = 1;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Anota cada shader que se dibuja (render target, vertices, orden) mientras jugas.\nDetener escribe PES21FX\\catalog\\shaders.txt con una ficha por shader (rol, cbuffers, texturas, uso)\ny catalog\\frame.txt con la linea de tiempo del ultimo cuadro. Pasalo a Claude para que sepa que toca cada uno."));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.25f, 0.2f, 1));
            if (ImGui::Button(TRS("Detener y escribir catalogo"), ImVec2(240 * UIK(), 0))) s_toolReq = 2;
            ImGui::PopStyleColor();
        }
        if (ImGui::Button(TRS("Volcar cbuffers"), ImVec2(160 * UIK(), 0))) s_toolReq = 3;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Escribe catalog\\cbuffers.txt: los valores que el motor le pasa a la composicion y a la luz del sol\n(sol, exposicion, niebla, matrices de sombra) con el nombre de cada campo. Hacelo con un partido en pantalla."));
        ImGui::SameLine();
        if (ImGui::Button(TRS("Volcar pasto"), ImVec2(130 * UIK(), 0))) s_toolReq = 4;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Mide las parcelas de pasto que dibuja el juego en un cuadro (tamano, grilla, cantidad)\ny lo escribe en catalog\\grass.txt. Hacelo con un partido en pantalla."));
        ImGui::SameLine();
        if (!g_recordOn) { if (ImGui::Button(TRS("Grabar partido"), ImVec2(150 * UIK(), 0))) s_toolReq = 5; }
        else { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.25f, 0.2f, 1)); if (ImGui::Button(TRS("Detener grabacion"), ImVec2(170 * UIK(), 0))) s_toolReq = 6; ImGui::PopStyleColor(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Mientras jugas, cada 5 s vuelca pasto y cbuffers numerados en catalog\\rec\\ (con la camara de ese momento)\ny deja la captura de shaders andando. Al terminar: Detener grabacion y despues Detener y escribir catalogo."));
        { std::string cst = g_catalogStatus; if (!cst.empty()) ImGui::TextDisabled("%s", cst.c_str()); }
        ImGui::Spacing();
    }
    if (ImGui::CollapsingHeader(TRS("Efectos por tecla"), ImGuiTreeNodeFlags_DefaultOpen)) {
        int id = 0;
        for (auto& k : g_cfg.keys) {
            ImGui::PushID(id++);
            std::string lab = Utf8(LS(k.label, k.labelEn));
            if (k.type == KeyBind::Cycle) {
                if (ImGui::Button(TRS("Siguiente"))) KeyActionUnlocked(k, 1);
                ImGui::SameLine(); ImGui::TextUnformatted(lab.c_str());
            } else if (k.type == KeyBind::Toggle && !k.assigns.empty()) {
                bool offSwitch = k.assigns[0].values[0] <= 0.0f;
                bool on = offSwitch ? !k.active : k.active;
                if (ImGui::Checkbox(lab.c_str(), &on)) KeyActionUnlocked(k, 0);
            }
            ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader(TRS("Todos los parametros"))) {
        ImGui::TextDisabled("%s", TRS("Los valores crudos que leen los shaders (x0..w372). Lo que cambies aca tambien se guarda con Guardar."));
        std::string lastComment;
        const char* cn = "xyzw";
        for (int i = 0; i < kParamCount; i++) {
            bool any = false;
            for (int c = 0; c < 4; c++) any |= g_cfg.info[i][c].defined;
            if (!any) continue;
            for (int c = 0; c < 4; c++) {
                if (!g_cfg.info[i][c].defined) continue;
                const std::string& cm = g_cfg.info[i][c].comment;
                if (!cm.empty() && cm != lastComment) { ImGui::TextDisabled("%s", Utf8(cm).c_str()); lastComment = cm; }
            }
            for (int c = 0; c < 4; c++) {
                if (!g_cfg.info[i][c].defined) continue;
                char name[16]; if (i) snprintf(name, sizeof(name), "%c%d", cn[c], i); else snprintf(name, sizeof(name), "%c", cn[c]);
                ImGui::SetNextItemWidth(120 * UIK());
                ImGui::DragFloat(name, &g_cfg.live[i][c], 0.01f, -100.0f, 100.0f, "%.3f");
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }
    }
}
static void DrawSettingsPage() {
    PageTitle(kIcoGear, TRS("Ajustes del menu"));
    ImGui::Spacing();
    const float lw = 230.0f * UIK();
    auto row = [&](const char* label) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label); ImGui::SameLine(lw); };
    {   // (27/10) idioma del menu: se guarda en [PES21FX] language
        static const char* const kLangs[3] = { "Espa\xC3\xB1ol", "English", "Portugu\xC3\xAAs" };
        static const char* const kLangKey[3] = { "es", "en", "pt" };
        int lg = (g_cfg.lang >= 0 && g_cfg.lang <= 2) ? g_cfg.lang : 0;
        row("Idioma / Language / Idioma");
        ImGui::SetNextItemWidth(200 * UIK());
        if (ImGui::Combo("##idioma", &lg, kLangs, 3)) { g_cfg.lang = lg; SaveOption("language", kLangKey[lg]); LoadMenuLang(); }
    }
    {   // (31/10) que el juego no reciba las teclas mientras se escribe en el menu: [PES21FX] bloquear_teclado
        const char* kBlk[3] = { TRS("Siempre"), TRS("No mientras escribo"), TRS("No con el menu abierto") };
        int bk = std::clamp(g_cfg.blockKeys, 0, 2);
        row(TRS("El juego recibe las teclas"));
        ImGui::SetNextItemWidth(260 * UIK());
        if (ImGui::Combo("##bloqteclas", &bk, kBlk, 3)) { g_cfg.blockKeys = bk; SaveOption("bloquear_teclado", std::to_string(bk)); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Que el juego no reciba las teclas mientras escribis en el menu (buscador, nombres). 'No al escribir' = solo con un campo de texto activo; 'No con el menu' = mientras el menu esta abierto."));
    }
    {   // (11/11) mouse en los menus nativos del juego: [PES21FX] mouse_menus
        bool mm = g_cfg.menuMouse != 0;
        row(TRS("Mouse en los menus del juego"));
        if (ImGui::Checkbox("##mousemenus", &mm)) { g_cfg.menuMouse = mm ? 1 : 0; SaveOption("mouse_menus", mm ? "1" : "0"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Los menus del juego eligen la fila que esta debajo del mouse. Clic = aceptar, clic derecho = volver, rueda = subir y bajar. Los menus siguen siendo los del juego. Al usar el control la flecha se va sola."));
        if (mm) {   // (11/11 v2) el juego muestra la fila elegida 2 cuadros despues: la flecha espera lo mismo
            int rt = std::clamp(g_cfg.menuMouseDelay, 0, 4);
            row(TRS("Flecha del mouse: retraso (cuadros)"));
            ImGui::SetNextItemWidth(200 * UIK());
            if (ImGui::SliderInt("##mousemenusret", &rt, 0, 4)) g_cfg.menuMouseDelay = rt;
            if (ImGui::IsItemDeactivatedAfterEdit()) SaveOption("mouse_menus_retraso", std::to_string(g_cfg.menuMouseDelay));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("El juego tarda 2 cuadros en marcar la fila elegida. Con 2 la flecha y la fila marcada van juntas; con 0 la flecha va mas pegada a la mano pero la fila la sigue de atras."));
        }
    }
    {   // (11/11) cuadrito con la ultima tecla apretada: [PES21FX] ultima_tecla
        const char* kEsq[5] = { TRS("No"), TRS("Arriba a la izquierda"), TRS("Arriba a la derecha"), TRS("Abajo a la izquierda"), TRS("Abajo a la derecha") };
        int kh = std::clamp(g_cfg.keyHud, 0, 4);
        row(TRS("Ultima tecla apretada (solo en el Motor)"));
        ImGui::SetNextItemWidth(260 * UIK());
        if (ImGui::Combo("##ultimatecla", &kh, kEsq, 5)) { g_cfg.keyHud = kh; SaveOption("ultima_tecla", std::to_string(kh)); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Un icono de tecla semitransparente en una esquina con la ultima tecla del teclado que apretaste. Solo se ve dentro del Motor en tiempo real."));
    }
    {
        row(TRS("Tamano del menu"));
        ImGui::SetNextItemWidth(200 * UIK());
        float sc = g_cfg.uiScale;
        if (ImGui::SliderFloat("##uiscale", &sc, 0.8f, 1.6f, "x%.2f")) { g_cfg.uiScale = sc; ImGui::GetIO().FontGlobalScale = sc; }
        if (ImGui::IsItemDeactivatedAfterEdit()) { char b[32]; snprintf(b, sizeof(b), "%.2f", g_cfg.uiScale); SaveOption("ui_scale", b); }
    }
    ImGui::Spacing();
    {   // (08/11) explicacion debajo de cada opcion
        const char* kHelp[3] = { TRS("Solo al pasar el mouse"), TRS("Breve, debajo de cada opcion"), TRS("Completa, debajo de cada opcion") };
        int hm = std::clamp(g_cfg.menuHelp, 0, 2);
        row(TRS("Explicacion de cada opcion"));
        ImGui::SetNextItemWidth(330 * UIK());
        if (ImGui::Combo("##ayuda", &hm, kHelp, 3)) { g_cfg.menuHelp = hm; SaveOption("menu_ayuda", std::to_string(hm)); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Breve: una linea gris debajo de cada slider con lo que hace (en los selectores, lo que hace la opcion elegida). Completa: el texto entero."));
    }
    {   // (08/11) transparencia
        row(TRS("Opacidad del menu"));
        ImGui::SetNextItemWidth(200 * UIK());
        float a = g_cfg.menuAlpha;
        if (ImGui::SliderFloat("##alfa", &a, 0.5f, 1.0f, "%.2f")) g_cfg.menuAlpha = a;
        if (ImGui::IsItemDeactivatedAfterEdit()) { char b[32]; snprintf(b, sizeof(b), "%.2f", g_cfg.menuAlpha); SaveOption("menu_opacidad", b); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Mas bajo = se ve el juego a traves del fondo del menu."));
    }
    ImGui::Spacing();
    {   // (08/11 d) guardado automatico
        row(TRS("Guardado automatico"));
        ImGui::SetNextItemWidth(330 * UIK());
        const char* opts[3] = { TRS("Apagado (con Guardar o Ctrl+S)"), TRS("Al terminar de tocar (1 s)"), TRS("Cada 10 s si hubo cambios") };
        if (ImGui::BeginCombo("##autog", opts[std::clamp(g_cfg.menuAutoSave, 0, 2)])) {
            for (int k = 0; k < 3; k++) if (ImGui::Selectable(opts[k], g_cfg.menuAutoSave == k)) { g_cfg.menuAutoSave = k; SaveOption("menu_autoguardar", std::to_string(k)); }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Guarda solo los cambios del menu en PES21FX.ini. Deshacer (Ctrl+Z) sigue andando. La camara libre y las vistas de depuracion nunca se guardan prendidas."));
    }
    ImGui::Spacing();
    if (ImGui::Checkbox(TRS("Vidrio: el juego desenfocado detras del menu"), &g_cfg.menuGlass)) SaveOption("menu_vidrio", g_cfg.menuGlass ? "1" : "0");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Estilo vidrio (como Apple o Windows Aero). Apagado: fondo liso. 'Opacidad del menu' regula cuanto se oscurece el vidrio para leer mejor."));
    if (ImGui::Checkbox(TRS("Letra deportiva (Saira, estilo PES 6)"), &g_cfg.menuFont)) { SaveOption("menu_letra", g_cfg.menuFont ? "1" : "0"); s_fontsH = -1.0f; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Apagado: Segoe UI, la letra de Windows."));
    if (ImGui::Checkbox(TRS("Todas las opciones a la vista en las tarjetas largas"), &g_cfg.menuAdvanced)) SaveOption("menu_avanzado", g_cfg.menuAdvanced ? "1" : "0");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TRS("Apagado: las tarjetas con mas de %d controles muestran los principales y el resto con 'opciones mas'."), kMoreAfter);
    if (ImGui::Checkbox(TRS("Dos columnas de tarjetas si la ventana es ancha"), &g_cfg.menuCols)) SaveOption("menu_columnas", g_cfg.menuCols ? "1" : "0");
    if (ImGui::Checkbox(TRS("Tarjetas contraidas al abrir (compacto)"), &g_cfg.compact)) { SaveOption("compacto", g_cfg.compact ? "1" : "0"); s_collapseAll = g_cfg.compact ? 1 : 2; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Abrir los grupos contraidos (solo el titulo y el interruptor); + para desplegar uno"));
    if (ImGui::Checkbox(TRS("Tutoriales al principio de cada seccion"), &g_cfg.tutorials)) { SaveOption("tutorials", g_cfg.tutorials ? "1" : "0"); if (g_cfg.tutorials) s_tutDone.clear(); }
    if (ImGui::Checkbox(TRS("Captura automatica en partido"), &g_cfg.autoCap)) SaveOption("auto_captura", g_cfg.autoCap ? "1" : "0");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TRS("En partido, cada %.0f s guarda una imagen (jpg) en PES21FX\\capturas\\ y los valores de luz de ese momento en capturas\\luz.txt,\npara que Claude calibre la luz sin que tengas que sacar capturas. Tope: %d imagenes por sesion."), g_cfg.autoCapSec, g_cfg.autoCapMax);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    ImGui::TextUnformatted(IcoL(kIcoKeys, TRS("Atajos del menu")).c_str());
    if (s_fontBold) ImGui::PopFont();
    if (ImGui::BeginTable("atajos", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        const char* kA[][2] = {
            { "Insert", "Abrir / cerrar el menu" }, { "Ctrl+Z", "Deshacer el ultimo cambio" }, { "Ctrl+Y  /  Ctrl+Shift+Z", "Rehacer" },
            { "Ctrl+S", "Guardar en PES21FX.ini" }, { "Ctrl+F", "Buscar una opcion" }, { "Esc", "Limpiar la busqueda" },
            { "Ctrl+clic", "Escribir un valor exacto en un slider" }, { "Clic derecho", "Volver un slider a lo guardado" },
        };
        for (auto& a : kA) { ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImVec4(0.85f, 0.68f, 0.28f, 1), "%s", a[0]); ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(TRS(a[1])); }
        ImGui::EndTable();
    }
}

static void DrawMenu() {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 ds = io.DisplaySize;
    // (08/11) tres tamanos (boton al lado de la X, se guarda en [PES21FX] menu_tamano): 0 ventana, 1 casi toda la pantalla,
    // 2 panel a la derecha de todo el alto (en el editor deja la cara del jugador a la vista)
    if (s_winReq < 0) s_winReq = std::clamp(g_cfg.menuSize, 0, 2);
    const ImVec2 winDef(std::min(1080 * UIK(), ds.x * 0.9f), std::min(820 * UIK(), ds.y * 0.88f));
    if (s_winReq >= 0) {
        const ImGuiCond cnd = s_winReq == 0 ? ImGuiCond_Appearing : ImGuiCond_Always;
        if (s_winReq == 0) { ImGui::SetNextWindowSize(winDef, cnd); ImGui::SetNextWindowPos(ImVec2(40, 60), cnd); }
        else if (s_winReq == 1) { ImGui::SetNextWindowSize(ImVec2(ds.x * 0.94f, ds.y * 0.92f), cnd); ImGui::SetNextWindowPos(ImVec2(ds.x * 0.03f, ds.y * 0.04f), cnd); }
        else { const float pw = std::clamp(ds.x * 0.34f, 440.0f * UIK(), 720.0f * UIK()); ImGui::SetNextWindowSize(ImVec2(pw, ds.y * 0.94f), cnd); ImGui::SetNextWindowPos(ImVec2(ds.x - pw - ds.x * 0.01f, ds.y * 0.03f), cnd); }
        s_winReq = -2;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(400 * UIK(), 300 * UIK()), ImVec2(ds.x * 0.97f, ds.y * 0.95f));   // (04/11) nunca mas grande que la pantalla
    const bool glass = GlassSRV() != nullptr;
    ImGui::SetNextWindowBgAlpha(glass ? 0.0f : std::clamp(g_cfg.menuAlpha, 0.5f, 1.0f));   // (08/11) Ajustes: transparencia del menu; con vidrio el fondo lo dibuja GlassBackdrop
    bool open = true;
    const float vis = Ease(s_menuVis); const bool closing = !g_menuOpen;   // (08/11) aparece / se desvanece
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, vis);
    if (glass) ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * UIK());
    ImGui::Begin("##pes21fx", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | (closing ? ImGuiWindowFlags_NoInputs : 0));
    if (glass) ImGui::PopStyleVar();
    if (glass) GlassBackdrop(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y), 16.0f * UIK(), vis);
    {   // (08/11) segun el ancho: barra lateral con nombres o solo iconos, botones con texto o solo iconos
        const float cw = ImGui::GetContentRegionAvail().x;
        s_rail = s_iconsOk && cw < 840.0f * UIK();
        s_slim = s_iconsOk && cw < 640.0f * UIK();
    }
    if (!open) g_menuOpen = false;
    // (04/11 n) casillas, selectores y el buscador fuera de las tarjetas: un tono mas claro que el fondo de la ventana
    ImGui::PushStyleColor(ImGuiCol_FrameBg, PanelCol(ImVec4(0.16f, 0.18f, 0.22f, 1), 0.70f)); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, PanelCol(ImVec4(0.20f, 0.23f, 0.28f, 1), 0.82f));

    // --- atajos de teclado (04/11 n) ---
    const bool typing = io.WantTextInput || closing, busy = ImGui::IsAnyItemActive();
    if (!closing && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) s_saveReq = true;
    if (!closing && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) s_focusSearch = true;
    if (!typing && !busy && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) s_undoReq = io.KeyShift ? 2 : 1;
    if (!typing && !busy && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) s_undoReq = 2;
    if (s_search[0] && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) s_search[0] = 0;

    // lo que hace falta saber para la barra de arriba (corto, con el mutex)
    int nChanged = 0, nErr = 0;
    { std::lock_guard<std::mutex> lk(g_cfgMutex); for (auto& g : g_cfg.groups) nChanged += GroupChanged(g) ? 1 : 0; }
    { std::lock_guard<std::mutex> lk2(g_errMutex); nErr = (int)g_compileErrors.size(); }
    // (08/11 d) guardado automatico (Agustin: "da una opcion de guardado automatico"): cuando los valores dejan de cambiar 1 s (o 10 s)
    // y no se esta arrastrando nada, se guarda solo; tambien al cerrar el menu. La firma ignora la camara libre (x344) y las vistas de
    // depuracion, que SaveConfigValues igual nunca deja prendidas: asi no se guarda una y otra vez por ellas.
    static bool s_autoSaving = false;
    if (g_cfg.menuAutoSave > 0 && !MenuTestActive()) {
        static uint64_t s_sig = 0, s_savedSig = 0; static double s_sigT = 0;
        uint64_t h = 1469598103934665603ull;
        {   std::lock_guard<std::mutex> lk(g_cfgMutex);
            for (int i = 0; i < kParamCount; i++) for (int c = 0; c < 4; c++) {
                if ((i == 344 && c == 0) || FindDebugParam(i, c)) continue;
                uint32_t u; memcpy(&u, &g_cfg.live[i][c], 4); h = (h ^ u) * 1099511628211ull; } }
        if (h != s_sig) { s_sig = h; s_sigT = Now(); }
        const double wait = g_cfg.menuAutoSave == 1 ? 1.0 : 10.0;
        if (nChanged > 0 && h != s_savedSig && !ImGui::IsAnyItemActive() && (Now() - s_sigT > wait || closing)) { s_saveReq = true; s_autoSaving = true; s_savedSig = h; }
    }

    // --- cabecera: logo + nombre + estado de los shaders, X a la derecha ---
    const float headY = ImGui::GetCursorPosY();
    if (s_logo) { const float h = ImGui::GetTextLineHeightWithSpacing() * 2.0f; ImGui::Image((ImTextureID)(intptr_t)s_logo, ImVec2(h * s_logoAspect, h)); ImGui::SameLine(); }
    ImGui::BeginGroup();
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    ImGui::TextColored(ImVec4(0.85f, 0.68f, 0.28f, 1), "%s", s_logo ? "FX  0.2" : "PES21 FX  0.2");
    if (s_fontBold) ImGui::PopFont();
    {
        const int rep = g_replacedCount.load(), fail = g_failedCount.load();
        ImGui::PushStyleColor(ImGuiCol_Text, fail ? ImVec4(0.95f, 0.55f, 0.35f, 1) : ImVec4(0.45f, 0.85f, 0.55f, 1));
        if (s_slim) ImGui::Text("%s  %d", fail ? "!" : "OK", rep); else ImGui::Text(TRS("%s  %d shaders propios activos"), fail ? "!" : "OK", rep);
        ImGui::PopStyleColor();
        if (fail) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1), TRS("   %d con error (ver Errores)"), fail); if (ImGui::IsItemClicked()) SelectPage("errors"); }
    }
    ImGui::EndGroup();
    {   // (08/11) tamano de la ventana + cerrar, arriba a la derecha
        const float bw = 36 * UIK(), sp = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw * 2 - sp);
        ImGui::SetCursorPosY(headY);
        static const unsigned kSzIco[3] = { kIcoCompress, kIcoExpand, kIcoLayers };   // icono del tamano al que pasa el boton
        const int cur = std::clamp(g_cfg.menuSize, 0, 2);
        if (ImGui::Button(s_iconsOk ? (Ico(kSzIco[(cur + 1) % 3]) + "##tam").c_str() : "[ ]", ImVec2(bw, 0))) {
            g_cfg.menuSize = (cur + 1) % 3; s_winReq = g_cfg.menuSize; SaveOption("menu_tamano", std::to_string(g_cfg.menuSize));
        }
        static const char* const kSzTip[3] = { "Pantalla grande", "Panel a la derecha (deja la cara a la vista)", "Ventana" };
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(TRS("Tamano del menu: %s"), TRS(kSzTip[cur]));
        ImGui::SameLine(0, sp);
        if (ImGui::Button(s_iconsOk ? Ico(kIcoX).c_str() : "X", ImVec2(bw, 0))) g_menuOpen = false;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Cerrar (Insert)"));
    }
    ImGui::Spacing();

    // --- barra de arriba: guardar, deshacer, rehacer, descartar, ver original, recargar, buscar ---
    {
        const bool dirty = nChanged > 0;
        if (dirty) { const float pu = 0.5f + 0.5f * sinf((float)Now() * 3.2f);   // (08/11) late suave mientras hay cambios sin guardar
            ImGui::PushStyleColor(ImGuiCol_Button, Mix(ImVec4(0.14f, 0.48f, 0.31f, 1), ImVec4(0.20f, 0.60f, 0.40f, 1), pu)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.66f, 0.44f, 1)); }
        std::string sv = s_slim ? Ico(kIcoSave) : IcoL(kIcoSave, TRS("Guardar"));
        if (dirty) sv += s_slim ? " " + std::to_string(nChanged) : " (" + std::to_string(nChanged) + ")";
        if (ImGui::Button((sv + "###guardar").c_str())) s_saveReq = true;
        if (dirty) ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(dirty ? TRS("Guardar en PES21FX.ini (Ctrl+S). %d grupos con cambios sin guardar.") : TRS("Guardar en PES21FX.ini (Ctrl+S). No hay cambios sin guardar."), nChanged);
        ImGui::SameLine();
        ImGui::BeginDisabled(s_undo.empty());
        if (ImGui::Button(s_iconsOk ? (Ico(kIcoUndo) + "##undo").c_str() : TRS("Deshacer"))) s_undoReq = 1;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { if (s_undo.empty()) ImGui::SetTooltip("%s", TRS("Deshacer (Ctrl+Z): no hay nada para deshacer")); else ImGui::SetTooltip(TRS("Deshacer (Ctrl+Z): %s  (%d pasos)"), s_undo.back().what.c_str(), (int)s_undo.size()); }
        ImGui::SameLine(0, 4.0f * UIK());
        ImGui::BeginDisabled(s_redo.empty());
        if (ImGui::Button(s_iconsOk ? (Ico(kIcoRedo) + "##redo").c_str() : TRS("Rehacer"))) s_undoReq = 2;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { if (s_redo.empty()) ImGui::SetTooltip("%s", TRS("Rehacer (Ctrl+Y): no hay nada para rehacer")); else ImGui::SetTooltip(TRS("Rehacer (Ctrl+Y): %s"), s_redo.back().what.c_str()); }
        ImGui::SameLine();
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button((s_rail ? Ico(kIcoRevert) + "##desc" : IcoL(kIcoRevert, TRS("Descartar"))).c_str())) s_revertAllReq = true;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", TRS("Volver todo el menu a lo guardado en PES21FX.ini (se puede deshacer con Ctrl+Z)"));
        ImGui::SameLine();
        if (s_iconsOk) {   // (08/11) boton que queda marcado mientras se ve el juego original
            ImGui::PushStyleColor(ImGuiCol_Button, g_cfg.bypass ? ImVec4(0.75f, 0.45f, 0.15f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if (ImGui::Button((s_rail ? Ico(kIcoEye) + "##orig" : IcoL(kIcoEye, TRS("Original"))).c_str())) g_cfg.bypass = !g_cfg.bypass;
            ImGui::PopStyleColor();
        } else ImGui::Checkbox(TRS("Ver original"), &g_cfg.bypass);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS(g_cfg.bypass ? "Viendo el juego SIN el mod: clic para volver" : "Ver el juego sin el mod, para comparar"));
        ImGui::SameLine();
        {   // (08/11 e) Agustin: "un boton que apague los menus para tener la vista completa (Apariencia, Movimiento)": x392, nunca se guarda
            const bool hid = g_cfg.live[392][0] > 0.5f;
            ImGui::PushStyleColor(ImGuiCol_Button, hid ? ImVec4(0.75f, 0.45f, 0.15f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if (ImGui::Button((s_rail || s_slim ? (s_iconsOk ? Ico(kIcoEyeSlash) : std::string("[M]")) + "##menus" : IcoL(kIcoEyeSlash, TRS("Menus"))).c_str())) {
                g_cfg.live[392][0] = hid ? 0.0f : 1.0f;
                for (auto& k : g_cfg.keys) if (k.name == "PESHideMenus") k.active = !hid;   // la tecla Alt+Shift+M queda en el mismo estado
                Log("menu: menus del juego %s (x392)", hid ? "visibles" : "ocultos");
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS(hid ? "Menus del juego OCULTOS (Alt+Shift+M): clic para verlos de nuevo" : "Ocultar los menus del juego para ver el visor completo (Apariencia, Movimiento). Alt+Shift+M. No se guarda."));
        }
        ImGui::SameLine();
        if (ImGui::Button(s_iconsOk ? (Ico(kIcoReload) + "###recargar").c_str() : TRS("Recargar"))) ImGui::OpenPopup("recargar_pop");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Recargar shaders o la configuracion"));
        if (ImGui::BeginPopup("recargar_pop")) {
            if (ImGui::MenuItem(TRS("Recargar shaders"))) { ReloadShaders(); s_status = TRS("Shaders recargados"); }
            if (ImGui::MenuItem(TRS("Recargar config"))) { LoadConfig(); ReloadShaders(); UndoClear(); s_status = TRS("Configuracion recargada"); }
            ImGui::TextDisabled("%s", TRS("Recargar config vuelve a leer PES21FX.ini (se pierde lo no guardado)"));
            ImGui::EndPopup();
        }
        if (ImGui::GetContentRegionAvail().x - ImGui::GetItemRectMax().x + ImGui::GetWindowPos().x > 210.0f * UIK()) ImGui::SameLine();   // (08/11) si no entra, el buscador baja a su propia fila
        if (s_focusSearch) { ImGui::SetKeyboardFocusHere(); s_focusSearch = false; }
        ImGui::SetNextItemWidth(std::max(140.0f * UIK(), ImGui::GetContentRegionAvail().x - (s_search[0] ? ImGui::GetFrameHeight() + 6.0f * UIK() : 0.0f)));
        ImGui::InputTextWithHint("##buscar", IcoL(kIcoSearch, TRS("Buscar opcion (Ctrl+F)")).c_str(), s_search, sizeof(s_search));
        if (s_search[0]) { ImGui::SameLine(0, 4.0f * UIK()); if (ImGui::Button(s_iconsOk ? (Ico(kIcoX) + "##limpiar").c_str() : "x", ImVec2(ImGui::GetFrameHeight(), 0))) s_search[0] = 0; }
    }
    ImGui::Spacing();

    const float footH = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        UndoRequests();
        UndoSnapshot();
        if (s_revertAllReq) { RevertAll(); s_revertAllReq = false; s_status = TRS("Cambios descartados (Ctrl+Z para recuperarlos)"); }
        if (s_collapseAll) { for (auto& g : g_cfg.groups) s_collapsed[g.title] = (s_collapseAll == 1); s_collapseAll = 0; }

        // secciones: conteos, cambios, categoria e icono
        struct Sec { std::string key, shown; int cat = 2, ord = 5; unsigned ico = kIcoLayers; int total = 0, active = 0; bool changed = false; };
        std::vector<Sec> secs;
        int nFav = 0;
        for (auto& g : g_cfg.groups) {
            const std::string k = SecKey(g);
            Sec* s = nullptr; for (auto& x : secs) if (x.key == k) { s = &x; break; }
            if (!s) { secs.push_back(Sec()); s = &secs.back(); s->key = k; s->shown = Utf8(g.section.empty() ? std::string(TRS("Otros")) : LS(g.section, g.sectionEn)); SecInfo(k, s->cat, s->ord, s->ico); }
            s->total++; if (g.tIdx < 0 || GroupActive(g)) s->active++; if (GroupChanged(g)) s->changed = true;
            if (IsFav(g.title)) nFav++;
        }
        std::stable_sort(secs.begin(), secs.end(), [](const Sec& a, const Sec& b) { return a.cat != b.cat ? a.cat < b.cat : a.ord < b.ord; });
        // pagina elegida (la de la ultima vez; si ya no existe, la primera seccion)
        if (s_sel.empty()) s_sel = g_cfg.menuSection;
        {
            bool ok = s_sel == "fav" || s_sel == "chg" || s_sel == "rec" || s_sel == "presets" || s_sel == "tools" || s_sel == "settings" || (s_sel == "errors" && nErr > 0);
            for (auto& s : secs) ok |= s_sel == "sec:" + s.key;
            if (!ok) s_sel = secs.empty() ? "presets" : "sec:" + secs[0].key;
        }

        // --- barra lateral ---
        const float navW = s_rail ? ImGui::GetFrameHeight() * 2.1f : std::clamp(ImGui::GetContentRegionAvail().x * 0.24f, 200.0f * UIK(), 270.0f * UIK());
        ImGui::PushStyleColor(ImGuiCol_ChildBg, glass ? ImVec4(0.0f, 0.0f, 0.0f, 0.22f) : ImVec4(0.09f, 0.10f, 0.12f, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * UIK());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f * UIK()));
        if (s_rail) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3.0f * UIK(), 6.0f * UIK()));
        ImGui::BeginChild("nav", ImVec2(navW, -footH), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
        if (s_rail) ImGui::PopStyleVar();
        ImDrawList* navDL = ImGui::GetWindowDrawList();
        navDL->ChannelsSplit(2); navDL->ChannelsSetCurrent(1);   // 0 = resaltado (debajo), 1 = items
        s_navTgtY = -1.0f;
        NavHeader(TRS("INICIO"));
        NavItem("fav", kIcoStar, TRS("Favoritos"), nFav ? std::to_string(nFav) : "", false);
        NavItem("chg", kIcoPen, TRS("Cambiados"), nChanged ? std::to_string(nChanged) : "", nChanged > 0);
        NavItem("rec", kIcoHistory, TRS("Recientes"), s_recent.empty() ? "" : std::to_string(s_recent.size()), false);
        const char* kCat[3] = { TRS("ESCENA"), TRS("JUGADORES"), TRS("OTROS") };
        int lastCat = -1;
        for (auto& s : secs) {
            if (s.cat != lastCat) { NavHeader(kCat[s.cat]); lastCat = s.cat; }
            NavItem("sec:" + s.key, s.ico, s.shown, "", s.changed);   // (08/11) mas simple: sin el conteo (esta en el titulo de la pagina)
        }
        NavHeader(TRS("SISTEMA"));
        NavItem("presets", kIcoSliders, TRS("Presets por estadio"), g_cfg.presetActive.file.empty() ? "" : "ON", false);
        NavItem("tools", kIcoTools, TRS("Herramientas"), g_catalogActive || g_recordOn ? "REC" : "", false);
        NavItem("settings", kIcoGear, TRS("Ajustes"), "", false);
        if (nErr) NavItem("errors", kIcoWarn, TRS("Errores"), std::to_string(nErr), true);
        {   // (08/11) el resaltado de la pagina elegida va hacia su item (~0.15 s)
            navDL->ChannelsSetCurrent(0);
            if (s_navTgtY >= 0.0f) {
                if (s_navY < 0.0f) s_navY = s_navTgtY;
                s_navY += (s_navTgtY - s_navY) * std::min(1.0f, io.DeltaTime * 16.0f);
                const ImVec2 wp = ImGui::GetWindowPos(); const float y = wp.y + s_navY - ImGui::GetScrollY();
                const float x0 = wp.x + ImGui::GetStyle().WindowPadding.x * (s_rail ? 1.0f : 1.0f), x1 = wp.x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
                navDL->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + s_navH), ImGui::GetColorU32(glass ? ImVec4(0.20f, 0.62f, 0.42f, 0.30f) : ImVec4(0.15f, 0.33f, 0.25f, 1)), 7.0f * UIK());
                navDL->AddRectFilled(ImVec2(x0, y + s_navH * 0.22f), ImVec2(x0 + 3.0f * UIK(), y + s_navH * 0.78f), IM_COL32(102, 230, 158, 255), 2.0f * UIK());
            }
            navDL->ChannelsMerge();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // --- pagina ---
        ImGui::BeginChild("page", ImVec2(0, -footH), ImGuiChildFlags_None);
        const std::string q = LowerU(s_search);
        {   // (08/11) al cambiar de pagina la nueva aparece y entra deslizandose desde la derecha (0.2 s)
            const std::string pk = q.empty() ? s_sel : std::string("q");
            if (pk != s_pageShown) { s_pageShown = pk; s_pageT = Now(); }
        }
        const float pe = Ease((float)(Now() - s_pageT) / 0.20f), pIn = (1.0f - pe) * 18.0f * UIK();
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (0.2f + 0.8f * pe));
        if (pIn > 0.5f) ImGui::Indent(pIn);
        if (!q.empty()) {
            int n = 0; for (auto& g : g_cfg.groups) if (GroupMatches(g, q)) n++;
            char sub[128]; snprintf(sub, sizeof sub, TRS("%d grupos coinciden (Esc para limpiar)"), n);
            PageTitle(kIcoSearch, std::string(TRS("Resultados")) + ": \"" + s_search + "\"", sub, true);
            ImGui::BeginChild("cards_res", ImVec2(0, 0), ImGuiChildFlags_None);
            std::vector<int> ids;
            for (int gid = 0; gid < (int)g_cfg.groups.size(); gid++) { const MenuGroup& g = g_cfg.groups[gid]; if (GroupMatches(g, q) && (!s_onlyActive || GroupActive(g))) ids.push_back(gid); }
            DrawCards(ids, true);
            if (!n) EmptyState(TRS("Nada coincide. Proba con otra palabra (busca en titulos, descripciones y sliders)."));
            ImGui::EndChild();
        } else if (s_sel == "fav" || s_sel == "chg" || s_sel == "rec") {
            const bool fav = s_sel == "fav", chg = s_sel == "chg";
            if (fav) PageTitle(kIcoStar, TRS("Favoritos"), TRS("Tus tarjetas marcadas con la estrella, de todas las secciones."), true);
            else if (chg) PageTitle(kIcoPen, TRS("Cambiados"), TRS("Tarjetas con valores distintos de lo guardado. Guardar (Ctrl+S) los deja fijos; Descartar los vuelve atras."), true);
            else PageTitle(kIcoHistory, TRS("Recientes"), TRS("Lo ultimo que tocaste en esta sesion, lo mas nuevo arriba."), true);
            if (!fav && !chg && !s_recent.empty()) { if (ImGui::SmallButton(TRS("Borrar la lista"))) s_recent.clear(); }
            ImGui::BeginChild(("cards_" + s_sel).c_str(), ImVec2(0, 0), ImGuiChildFlags_None);
            std::vector<int> ids;
            if (fav || chg) {
                for (int gid = 0; gid < (int)g_cfg.groups.size(); gid++) { const MenuGroup& g = g_cfg.groups[gid]; if ((fav ? IsFav(g.title) : GroupChanged(g)) && (!s_onlyActive || GroupActive(g))) ids.push_back(gid); }
            } else {
                for (auto& t : s_recent)
                    for (int gid = 0; gid < (int)g_cfg.groups.size(); gid++) if (g_cfg.groups[gid].title == t) { if (!s_onlyActive || GroupActive(g_cfg.groups[gid])) ids.push_back(gid); break; }
            }
            const int n = (int)ids.size();
            DrawCards(ids, true);
            if (!n) EmptyState(fav ? TRS("Todavia no hay favoritos: toca la estrella de una tarjeta para tenerla a mano aca.")
                             : chg ? TRS("No hay cambios sin guardar.") : TRS("Todavia no tocaste nada en esta sesion."));
            ImGui::EndChild();
        } else if (s_sel.rfind("sec:", 0) == 0) {
            const std::string sname = s_sel.substr(4);
            const Sec* s = nullptr; for (auto& x : secs) if (x.key == sname) s = &x;
            char sub[96]; snprintf(sub, sizeof sub, TRS("%d de %d grupos prendidos"), s ? s->active : 0, s ? s->total : 0);
            PageTitle(s ? s->ico : kIcoLayers, s ? s->shown : Utf8(sname), sub, true);
            ImGui::BeginChild(("cards_" + s_sel).c_str(), ImVec2(0, 0), ImGuiChildFlags_None);
            DrawTutorial(sname, s ? s->shown : sname);
            DrawSectionExtras(sname);
            std::vector<int> ids;
            for (int gid = 0; gid < (int)g_cfg.groups.size(); gid++) { const MenuGroup& g = g_cfg.groups[gid]; if (SecKey(g) == sname && (!s_onlyActive || GroupActive(g))) ids.push_back(gid); }
            DrawCards(ids, false);
            ImGui::EndChild();
        } else if (s_sel == "presets") {
            PageTitle(kIcoSliders, TRS("Presets por estadio"));
            ImGui::BeginChild("cards_presets", ImVec2(0, 0), ImGuiChildFlags_None);
            DrawTutorial("Presets", "Presets");
            DrawPresetsTab();
            ImGui::EndChild();
        } else if (s_sel == "tools") {
            ImGui::BeginChild("cards_tools", ImVec2(0, 0), ImGuiChildFlags_None);
            DrawToolsPage();
            ImGui::EndChild();
        } else if (s_sel == "settings") {
            ImGui::BeginChild("cards_settings", ImVec2(0, 0), ImGuiChildFlags_None);
            DrawSettingsPage();
            ImGui::EndChild();
        } else if (s_sel == "errors") {
            PageTitle(kIcoWarn, TRS("Errores de compilacion"), TRS("Shaders propios que no compilaron: el juego usa el original en su lugar."));
            ImGui::BeginChild("cards_errors", ImVec2(0, 0), ImGuiChildFlags_None);
            std::lock_guard<std::mutex> lk2(g_errMutex);
            for (auto& e : g_compileErrors) ImGui::TextWrapped("%s", e.c_str());
            ImGui::EndChild();
        }
        if (pIn > 0.5f) ImGui::Unindent(pIn);
        ImGui::PopStyleVar();
        ImGui::EndChild();
        UndoCollect();
    }

    // --- linea de abajo: ultimo aviso + atajos ---
    {
        std::string msg = (!s_undoMsg.empty() && Now() - s_undoMsgT < 4.0) ? s_undoMsg : s_status;
        ImGui::TextDisabled("%s", msg.c_str());
        const char* hint = TRS("Ctrl+Z deshacer   Ctrl+S guardar   Ctrl+F buscar   Insert cerrar");
        const float hw = ImGui::CalcTextSize(hint).x;
        if (ImGui::GetWindowWidth() - hw - 30.0f * UIK() > ImGui::CalcTextSize(msg.c_str()).x + 40.0f * UIK()) {
            ImGui::SameLine(ImGui::GetWindowWidth() - hw - ImGui::GetStyle().WindowPadding.x);
            ImGui::TextDisabled("%s", hint);
        }
    }
    ImGui::PopStyleColor(2);
    ImGui::End();
    ImGui::PopStyleVar();   // fundido de la ventana

    // acciones que no pueden correr con g_cfgMutex tomado
    if (s_saveReq) { s_saveReq = false; const bool ok = SaveConfigValues(); s_status = TRS(ok ? (s_autoSaving ? "Guardado automatico en PES21FX.ini" : "Guardado en PES21FX.ini") : "No se pudo guardar"); s_autoSaving = false; }
    if (s_reportReq) { s_reportReq = false; s_status = MakeReport(); }   // (08/11)
    if (s_toolReq) {
        const int a = s_toolReq; s_toolReq = 0;
        if (a == 1) CatalogStart();
        else if (a == 2) {
            UINT w = 0, h = 0;
            if (s_rtv) { ID3D11Resource* res = nullptr; s_rtv->GetResource(&res); ID3D11Texture2D* t = nullptr;
                if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); w = d.Width; h = d.Height; t->Release(); }
                if (res) res->Release(); }
            CatalogStop(w, h, AllShaders());
        }
        else if (a == 3) CatalogDumpCBs();
        else if (a == 4) CatalogProbeGrass();
        else if (a == 5) CatalogRecord(true);
        else if (a == 6) CatalogRecord(false);
    }
}

// --- 24/10: menu Fisico del juego (Editar > Jugadores > Fisico): Insert abre una ventana chica con la musculatura de ese
// jugador (la tarjeta "Musculatura por jugador"); "Menu completo" pasa al menu de siempre. Al entrar, un aviso abajo.
static bool s_fullMenu = false;
static void DrawPhysiqueWindow() {
    ImGui::SetNextWindowSize(ImVec2(560 * UIK(), 960 * UIK()), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320 * UIK(), 240 * UIK()), ImVec2(ImGui::GetIO().DisplaySize.x * 0.97f, ImGui::GetIO().DisplaySize.y * 0.95f));   // (26/10 c) diseno nuevo: mapa del cuerpo arriba, filas abajo
    ImGui::SetNextWindowPos(ImVec2(24, 50), ImGuiCond_FirstUseEver);
    bool open = true;
    ImGui::Begin((std::string(TRS("Musculatura del jugador")) + "###fisico").c_str(), &open, ImGuiWindowFlags_NoCollapse);
    if (!open) g_menuOpen = false;
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    ImGui::TextColored(ImVec4(0.85f, 0.68f, 0.28f, 1), "%s", TRS("PES21 FX  -  musculatura"));
    if (s_fontBold) ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 150);
    if (ImGui::Button(TRS("Menu completo"), ImVec2(130 * UIK(), 0))) s_fullMenu = true;
    ImGui::TextDisabled("%s", TRS("Insert: cerrar. Los valores de este jugador se guardan solos."));
    ImGui::Separator();
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        ImGui::BeginChild("fisico_panel", ImVec2(0 * UIK(), 0), ImGuiChildFlags_None);
        MuscleDrawFisicoWindow(s_fontBold, s_fontBig);   // (26/10 c) mapa del cuerpo, pestanas por zona, barras con la marca del juego
        ImGui::EndChild();
    }
    ImGui::End();
}
static void DrawPhysiqueHint() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 28, vp->Pos.y + vp->Size.y - 28), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGui::Begin("##fisico_aviso", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::TextColored(ImVec4(0.85f, 0.68f, 0.28f, 1), "PES21 FX");
    ImGui::SameLine();
    ImGui::TextUnformatted(TRS("Musculatura: ultima fila de la lista"));
    ImGui::SameLine(); ImGui::TextDisabled("%s", TRS("(Insert: ventana del mod)"));
    ImGui::End();
}

// (1/11) aviso en pantalla de la camara libre / camara lenta (pedido de Agustin: saber cuando estan activas y ver las teclas).
// A la izquierda, arriba de los iconos de controles del editor; no recibe entrada.
static void DrawCamaraHud(bool freeCam, float slow) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // a la izquierda, arriba de los iconos de controles del editor (Agustin 1/11)
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 28, vp->Pos.y + vp->Size.y * 0.84f), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##camara_aviso", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    const ImVec4 gold(0.85f, 0.68f, 0.28f, 1);
    if (freeCam) {
        if (s_fontBold) ImGui::PushFont(s_fontBold);
        ImGui::TextColored(gold, "%s", TRS("CAMARA LIBRE"));
        if (s_fontBold) ImGui::PopFont();
        ImGui::SameLine(); ImGui::TextDisabled("  zoom x%.2f   %.1f m/s", g_cfg.live[344][2], g_cfg.live[344][1]);
        ImGui::TextUnformatted(TRS("W / S adelante y atras   A / D izquierda y derecha   Q / E bajar y subir"));
        ImGui::TextUnformatted(TRS("Flechas o mouse (boton derecho): girar   Rueda o Re Pag / Av Pag: zoom   Shift: rapido   Ctrl: lento"));
        ImGui::TextUnformatted(TRS("Inicio: volver a la camara del juego   Alt+Shift+V: apagar"));
        if (g_cfg.live[346][0] > 0.5f) {
            if (g_cfg.live[346][2] > 0.5f) ImGui::TextColored(gold, "%s", TRS("Siguiendo la cara: W / S acercan, A / D giran alrededor, Q / E suben y bajan   Alt+Shift+F: soltar"));
            else ImGui::TextDisabled("%s", TRS("Seguir la cara: esperando ver la cara en el visor del editor   Alt+Shift+F: soltar"));
        } else ImGui::TextDisabled("%s", TRS("Alt+Shift+F: seguir la cara"));
    }
    if (slow != 1.0f) {
        if (freeCam) ImGui::Separator();
        if (s_fontBold) ImGui::PushFont(s_fontBold);
        ImGui::TextColored(gold, "%s", TRS("CAMARA LENTA"));
        if (s_fontBold) ImGui::PopFont();
        ImGui::SameLine(); ImGui::TextDisabled("  x%.2f", slow);
        ImGui::TextUnformatted(TRS("Alt+Shift+L: 1/2, 1/4, 1/10, normal   (Insert > Camara)"));
        if (g_cfg.live[343][3] <= 0.0f && g_cfg.live[343][2] <= 0.5f) ImGui::TextDisabled("%s", TRS("(el visor no esta corriendo: entra al modo editar)"));
    }
    ImGui::End();
}

// (1/11) iconos de la luz de prueba del editor (filas 297 / 298 / 319, pes_ssss_inc.hlsli): pedido de Agustin, ver donde esta
// la luz. La luz es direccional y la camara del visor es casi ortografica (tele): visto desde el jugador, la direccion de la luz
// en vista (x a la derecha, y arriba) se dibuja como un punto alrededor de la cara; delante del jugador = icono lleno y grande,
// detras = chico, transparente y con la linea cortada. Mismos presets que el shader.
static void LightDir(float az, float el, float& x, float& y, float& front) {
    const float d = 3.14159265f / 180.0f;
    x = std::cos(el * d) * std::sin(az * d); y = std::sin(el * d); front = std::cos(el * d) * std::cos(az * d);
}
static ImU32 LightCol(float warm, float a) {
    warm = std::clamp(warm, 0.0f, 1.0f);
    float r = 0.80f + 0.20f * warm, g = 0.88f - 0.26f * warm, b = 1.00f - 0.68f * warm;
    float m = std::max(r, std::max(g, b));
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r / m, g / m, b / m, a));
}
static void DrawOneLightIcon(ImDrawList* dl, ImVec2 c, float R, float az, float el, float warm, const char* name) {
    float x, y, f; LightDir(az, el, x, y, f);
    const bool back = f < 0.0f;
    const float a = back ? 0.45f : 1.0f, rad = back ? 9.0f : 14.0f;
    ImVec2 p(c.x + x * R, c.y - y * R);
    const ImU32 col = LightCol(warm, a), dark = IM_COL32(0, 0, 0, (int)(170 * a));
    // linea desde la cara (cortada si la luz esta detras)
    const int seg = 14;
    for (int i = 0; i < seg; i++) {
        if (back && (i & 1)) continue;
        float t0 = (float)i / seg, t1 = (float)(i + 1) / seg;
        ImVec2 a0(c.x + (p.x - c.x) * t0, c.y + (p.y - c.y) * t0), a1(c.x + (p.x - c.x) * t1, c.y + (p.y - c.y) * t1);
        dl->AddLine(a0, a1, LightCol(warm, a * 0.55f), 1.5f);
    }
    // sol: disco con rayos
    for (int k = 0; k < 8; k++) {
        float an = k * 0.785398f;
        ImVec2 d(std::cos(an), std::sin(an));
        dl->AddLine(ImVec2(p.x + d.x * (rad + 3), p.y + d.y * (rad + 3)), ImVec2(p.x + d.x * (rad + 9), p.y + d.y * (rad + 9)), col, 2.0f);
    }
    dl->AddCircleFilled(p, rad + 1.5f, dark, 24);
    dl->AddCircleFilled(p, rad, col, 24);
    char buf[96]; snprintf(buf, sizeof buf, "%s%s  %.0f / %.0f", TRS(name), back ? TRS(" (detras)") : "", az, el);
    ImVec2 tp(p.x + rad + 12, p.y - 8);
    dl->AddText(ImVec2(tp.x + 1, tp.y + 1), dark, buf);
    dl->AddText(tp, col, buf);
}
static void DrawEditorLightIcons() {
    float E[4], E2[4], E3[4];
    for (int i = 0; i < 4; i++) { E[i] = g_cfg.live[297][i]; E2[i] = g_cfg.live[298][i]; E3[i] = g_cfg.live[319][i]; }
    const int preset = (int)(E[1] + 0.5f);
    float az = E[2], el = E[3], warm = E2[1];
    if (preset == 1) { az = 50; el = 45; warm = 0.62f; }
    else if (preset == 2) { az = 70; el = 12; warm = 0.95f; }
    else if (preset == 3) { az = -40; el = 60; warm = 0.15f; }
    else if (preset == 5) { az = 20; el = 70; warm = 0.50f; }    // (04/11) nublado (igual que pes_ssss_inc.hlsli)
    else if (preset == 6) { az = 20; el = 70; warm = 0.35f; }    // lluvia
    else if (preset == 7) { az = 30; el = 78; warm = 0.58f; }    // mediodia
    else if (preset == 8) { az = 165; el = 10; warm = 0.95f; }   // atardecer a contraluz
    else if (preset == 9) { az = -45; el = 35; warm = 0.15f; }   // noche con dos torres
    else if (preset == 10) { az = 170; el = 20; warm = 0.80f; }  // (08/11) sol cercano a contraluz
    else if (preset == 11) { az = -35; el = 45; warm = 0.12f; }  // (08/11) foco de estadio cercano
    else if (preset == 12) { az = 55; el = 30; warm = 0.55f; }   // (08/11) una luz fuerte y la otra apagada
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // centro: la cara (alto medido en skintone.cpp, fila 192 z/w en pixeles del cuadro) o el centro de la pantalla
    ImVec2 c(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.42f);
    unsigned rw = 0, rh = 0; SSSSRenderSize(rw, rh);
    const float y0 = g_cfg.live[192][2], y1 = g_cfg.live[192][3];
    if (rh > 0 && y1 > y0 + 8.0f && y1 <= (float)rh) c.y = vp->Pos.y + (0.5f * (y0 + y1) / (float)rh) * vp->Size.y;
    const float R = vp->Size.y * 0.30f;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddCircle(c, 5.0f, IM_COL32(255, 255, 255, 120), 16, 1.5f);
    if (preset == 4) {
        if (E3[1] > 0.001f) DrawOneLightIcon(dl, c, R, (az >= 0 ? -1.0f : 1.0f) * std::clamp(90.0f - std::fabs(az) * 0.5f, 25.0f, 70.0f), 8.0f, std::max(warm - 0.2f, 0.0f), "Relleno");
        if (E3[2] > 0.001f) DrawOneLightIcon(dl, c, R, (az >= 0 ? 1.0f : -1.0f) * 150.0f, 35.0f, 0.45f, "Recorte");
        DrawOneLightIcon(dl, c, R, az, el, warm, "Principal");
    } else if (preset == 9) {
        DrawOneLightIcon(dl, c, R, az + 180.0f, el, warm, "Torre 2");
        DrawOneLightIcon(dl, c, R, az, el, warm, "Torre 1");
    } else DrawOneLightIcon(dl, c, R, az, el, warm, "Sol de prueba");
}

// (2/11) brujula del sol (x175, pedido de Agustin: "no me queda claro de donde viene el sol" en las capturas). Toma el sol en el
// mundo que publica la DLL (fila 111, siempre hacia el sol, ya con sol_parche) y la m_view de la escena, y lo dibuja alrededor del
// centro de la pantalla como el icono de la luz del editor: lleno = delante de la camara, chico y cortado = detras. Abajo, texto con la
// elevacion y hacia donde caen las sombras en la imagen (opuesto al sol, en el plano del piso).
static void DrawSunCompass() {
    std::vector<float> S; if (!NamedCBRead("SceneCB", S) || S.size() < 48) return;
    const float* w = g_cfg.live[111]; const float* M = &S[32];   // filas = ejes de la camara en el mundo
    float lx = w[0] * M[0] + w[1] * M[1] + w[2] * M[2], ly = w[0] * M[4] + w[1] * M[5] + w[2] * M[6], lz = w[0] * M[8] + w[1] * M[9] + w[2] * M[10];
    float n = std::sqrt(lx * lx + ly * ly + lz * lz); if (n < 1e-5f) return; lx /= n; ly /= n; lz /= n;
    const float el = std::asin(std::clamp(ly, -1.0f, 1.0f)) * 57.2958f, az = std::atan2(lx, lz) * 57.2958f;   // +z = delante de la camara
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 c(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddCircle(c, 5.0f, IM_COL32(255, 255, 255, 120), 16, 1.5f);
    const bool sunny = g_cfg.live[114][3] > 0.5f;
    DrawOneLightIcon(dl, c, vp->Size.y * 0.30f, az, el, sunny ? 0.75f : 0.2f, sunny ? "Sol" : "Luz principal");
    // sombras en el piso: opuestas al sol; en pantalla, hacia la derecha si el sol esta a la izquierda y hacia la camara si esta al fondo
    const float sx = -lx, sz = -lz; const float ah = std::sqrt(sx * sx + sz * sz);
    char buf[200];
    if (ah > 0.05f) snprintf(buf, sizeof buf, TRS("Sombras hacia %s%s%s  |  sol: altura %.0f, %.0f grados %s de la camara"),
        std::fabs(sx) > 0.3f * ah ? TRS(sx > 0 ? "la derecha" : "la izquierda") : "", (std::fabs(sx) > 0.3f * ah && std::fabs(sz) > 0.3f * ah) ? TRS(" y ") : "",
        std::fabs(sz) > 0.3f * ah ? TRS(sz > 0 ? "el fondo" : "la camara") : "", el, std::fabs(az), TRS(az < 0 ? "a la izquierda" : "a la derecha"));
    else snprintf(buf, sizeof buf, TRS("Sol casi vertical: sombras bajo los pies  |  altura %.0f grados"), el);
    ImVec2 ts = ImGui::CalcTextSize(buf); ImVec2 tp(c.x - ts.x * 0.5f, vp->Pos.y + vp->Size.y * 0.86f);
    dl->AddRectFilled(ImVec2(tp.x - 8, tp.y - 4), ImVec2(tp.x + ts.x + 8, tp.y + ts.y + 4), IM_COL32(0, 0, 0, 150), 4.0f);
    dl->AddText(tp, IM_COL32(255, 240, 200, 255), buf);
}


// (09/11 f, Agustin: "agregar en diagnostico una vista del esqueleto, los huesos en escena, solo eso") y392 = 1: cada personaje cercano con
// sus esqueletos (cuerpo blanco, manos celeste, cara amarillo): un punto por hueso y una linea a su padre. Solo lee la memoria del juego
// (transformacion actual del hueso + 0x18, padre + 0x20; nodo + 0x88 cuerpo, + 0x90 / + 0x98 manos, + 0xA0 cara) y la proyecta con
// m_projectionView de la escena. No escribe nada; nunca se guarda prendida (kDebugParams).
static bool SkReadable(const void* p, size_t n) {
    if (!p || (uintptr_t)p < 0x10000 || (uintptr_t)p > 0x00007fffffffffffull) return false;
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    if (!(mi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static bool SkCopy(void* dst, const void* src, size_t n) {
    if (!SkReadable(src, n)) return false;
    __try { memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void DrawSkeletonView() {
    std::vector<float> S; if (!NamedCBRead("SceneCB", S) || S.size() < 16) return;
    const float* PV = &S[0];   // m_projectionView (mundo -> recorte), como headshadow.cpp
    static CharInfo ci[kCharSlots]; static bool alive[kCharSlots];
    CharSnapshot(ci, alive);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    // (09/11) la escena no siempre ocupa la pantalla: en el editor y en los menus del juego el visor 3D es un rectangulo corrido
    // (p. ej. 1650 x 1440 a la derecha). Se usa el viewport del ultimo dibujo de personaje; sin dato, la pantalla entera.
    float rx = vp->Pos.x, ry = vp->Pos.y, rw = vp->Size.x, rh = vp->Size.y;
    { float cv[4]; if (CharSceneViewport(cv)) { rx = vp->Pos.x + cv[0]; ry = vp->Pos.y + cv[1]; rw = cv[2]; rh = cv[3]; } }
    auto toScreen = [&](const float w[3], ImVec2& o) {
        float c[4]; for (int j = 0; j < 4; j++) c[j] = PV[4 * j] * w[0] + PV[4 * j + 1] * w[1] + PV[4 * j + 2] * w[2] + PV[4 * j + 3];
        if (c[3] <= 0.05f) return false;
        const float nx = c[0] / c[3], ny = c[1] / c[3];
        if (fabsf(nx) > 1.5f || fabsf(ny) > 1.5f) return false;
        o = ImVec2(rx + (nx * 0.5f + 0.5f) * rw, ry + (0.5f - 0.5f * ny) * rh); return true;
    };
    static const int kOff[4] = { 0x88, 0x90, 0x98, 0xA0 };
    static const ImU32 kCol[4] = { IM_COL32(255, 255, 255, 220), IM_COL32(90, 210, 255, 220), IM_COL32(90, 210, 255, 220), IM_COL32(255, 210, 60, 230) };
    int drawn = 0, bones = 0;
    for (int sl = 0; sl < kCharSlots && drawn < 24; sl++) {
        if (!alive[sl] || !ci[sl].node) continue;
        float W[12]; if (!CharNodeMatrix(ci[sl].node, W)) continue;
        for (int r = 0; r < 4; r++) {
            void* rig = nullptr; if (!SkCopy(&rig, (const char*)ci[sl].node + kOff[r], 8) || !rig) continue;
            const char* arr = nullptr; int n = 0;
            if (!SkCopy(&arr, (const char*)rig + 0x28, 8) || !arr || !SkCopy(&n, (const char*)rig + 0x30, 4) || n < 2 || n > 400) continue;
            static ImVec2 pt[400]; static bool ok[400]; static float wp[400][3];
            // (09/11) manos y cara cuelgan de un hueso del cuerpo (rig + 0x120 = rig padre, + 0x128 = hueso de enganche): sus
            // transformaciones estan en el espacio de ese hueso (0x1419f8820 arma sus matrices con la inversa del enganche), asi que
            // se componen con la transformacion actual del hueso de enganche para llevarlas al espacio del modelo
            float A[12]; bool att = false;
            if (r > 0) { const void* prig = nullptr; const char* ab = nullptr; const void* ax = nullptr;
                if (SkCopy(&prig, (const char*)rig + 0x120, 8) && prig && SkCopy(&ab, (const char*)rig + 0x128, 8) && ab && SkCopy(&ax, ab + 0x18, 8) && ax && SkCopy(A, ax, sizeof A)) att = true; }
            for (int b = 0; b < n; b++) {
                ok[b] = false;
                const char* bone = arr + (size_t)b * 0xc0; const void* cur = nullptr; float x[12];
                if (!SkCopy(&cur, bone + 0x18, 8) || !cur || !SkCopy(x, cur, sizeof x)) continue;
                float t[3] = { x[8], x[9], x[10] }; if (!(fabsf(t[0]) < 3.0f && fabsf(t[1]) < 3.0f && fabsf(t[2]) < 3.0f)) continue;
                if (att) {   // modelo = enganche.t + rot(enganche.q, enganche.s * t)
                    const float v0 = A[0] * t[0], v1 = A[1] * t[1], v2 = A[2] * t[2], qx = A[4], qy = A[5], qz = A[6], qw = A[7];
                    const float cx = 2.0f * (qy * v2 - qz * v1), cy = 2.0f * (qz * v0 - qx * v2), cz = 2.0f * (qx * v1 - qy * v0);
                    t[0] = A[8] + v0 + qw * cx + (qy * cz - qz * cy); t[1] = A[9] + v1 + qw * cy + (qz * cx - qx * cz); t[2] = A[10] + v2 + qw * cz + (qx * cy - qy * cx);
                }
                float w[3]; for (int k = 0; k < 3; k++) w[k] = W[4 * k] * t[0] + W[4 * k + 1] * t[1] + W[4 * k + 2] * t[2] + W[4 * k + 3];
                wp[b][0] = t[0]; wp[b][1] = t[1]; wp[b][2] = t[2];
                ok[b] = toScreen(w, pt[b]);
            }
            for (int b = 0; b < n; b++) {
                if (!ok[b]) continue; bones++;
                const char* bone = arr + (size_t)b * 0xc0; uint64_t pv = 0; int par = -1;
                if (SkCopy(&pv, bone + 0x20, 8)) {
                    if (pv < (uint64_t)n) par = (int)pv;
                    else if (pv >= (uint64_t)(uintptr_t)arr && pv < (uint64_t)(uintptr_t)(arr + (size_t)n * 0xc0) && ((pv - (uint64_t)(uintptr_t)arr) % 0xc0) == 0) par = (int)((pv - (uint64_t)(uintptr_t)arr) / 0xc0);
                    else { int32_t pi = (int32_t)(pv & 0xffffffffu); if (pi >= 0 && pi < n) par = pi; }
                }
                if (par >= 0 && par != b && ok[par]) {   // (09/11) los huesos de deformacion (dsk_*) cuelgan lejos de su padre: esa union va tenue
                    const float dx = wp[b][0] - wp[par][0], dy = wp[b][1] - wp[par][1], dz = wp[b][2] - wp[par][2];
                    const bool lejos = dx * dx + dy * dy + dz * dz > 0.35f * 0.35f;
                    dl->AddLine(pt[par], pt[b], lejos ? ((kCol[r] & 0x00ffffffu) | 0x30000000u) : kCol[r], r == 3 ? 1.0f : 1.5f); }
                dl->AddCircleFilled(pt[b], r == 3 ? 1.8f : 2.5f, kCol[r], 8);
            }
        }
        drawn++;
    }
    char buf[120]; snprintf(buf, sizeof buf, TRS("Esqueleto: %d personajes, %d huesos (blanco cuerpo, celeste manos, amarillo cara)"), drawn, bones);
    ImVec2 tp(vp->Pos.x + 16, vp->Pos.y + vp->Size.y - 34);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    dl->AddRectFilled(ImVec2(tp.x - 6, tp.y - 3), ImVec2(tp.x + ts.x + 6, tp.y + ts.y + 3), IM_COL32(0, 0, 0, 150), 4.0f);
    dl->AddText(tp, IM_COL32(255, 240, 200, 255), buf);
}

void OverlayPresent(IDXGISwapChain* sc) {
    if (!g_dev.dev || !g_cfg.optOverlay) return;
    if (!s_init) Init(sc);
    bool down = (GetAsyncKeyState(g_cfg.menuKey) & 0x8000) != 0 && GetForegroundWindow() == s_hwnd;
    if (down && !s_keyWas) g_menuOpen = !g_menuOpen;
    s_keyWas = down;
    const bool fis = MusclePhysiqueActive();
    { static bool s_fisWas = false; if (fis && !s_fisWas) s_fullMenu = false; s_fisWas = fis; }
    // (24/10 b) el aviso de abajo ya no hace falta (la musculatura tiene sus paginas y L1 / R1 en el menu del juego) y tapaba
    // la barra de botones
    const bool hint = false;
    if (!g_menuOpen) { g_blockGameKeys = FreeCamBlocksKeys(); g_menuTyping = false; }   // (31/10) con la camara libre el juego tampoco ve el teclado
    const bool camFree = FreeCamActive() && !MotorPageActive(); const float camSlow = TiempoFactor();   // (1/11) aviso en pantalla
    const bool camHud = !g_menuOpen && (camFree || camSlow != 1.0f);
    const bool lightIcons = g_cfg.live[319][0] > 0.5f && g_cfg.live[297][0] > 0.5f && g_cfg.live[378][2] < 0.5f && SSSSEditorLightAge() < 0.5;   // (08/11) con la luz fija al mundo (z378) el icono (relativo a la camara) mentiria: no se dibuja   // (1/11) iconos de la luz del editor
    const bool sunCompass = g_cfg.live[175][0] > 0.5f && g_cfg.live[93][3] > 0.5f;   // (2/11) brujula del sol, solo en partido
    {   // (08/11) el menu aparece y se desvanece en ~0.16 s
        static double s_visT = 0; const double tn = Now(); const float dt = (float)std::clamp(tn - s_visT, 0.0, 0.1); s_visT = tn;
        s_menuVis = g_menuOpen ? std::min(1.0f, s_menuVis + dt / 0.16f) : std::max(0.0f, s_menuVis - dt / 0.13f);
    }
    const bool menuShown = g_menuOpen || s_menuVis > 0.0f;
    FrameTimeTick();   // (08/11) diagnostico: tiempo de cada cuadro
    const bool skelView = g_cfg.live[426][0] > 0.5f;   // (09/11) la vista del esqueleto se dibuja tambien con el menu cerrado
    float mmx = 0, mmy = 0; const bool mmCur = !g_menuOpen && MenuMouseCursor(&mmx, &mmy);   // (11/11) flecha del mouse sobre los menus del juego
    const bool keyHud = KeyHudPoll();   // (11/11) cuadrito con la ultima tecla apretada
    const bool gizmo = MotorGizmoPoll();   // (11/11) gizmo de camara del Motor (motorgizmo.cpp)
    if (!menuShown && !g_inspectOn && !hint && !camHud && !lightIcons && !sunCompass && !skelView && !mmCur && !keyHud && !gizmo) return;
    if (!s_rtv) {
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) return;
        g_dev.dev->CreateRenderTargetView(bb, nullptr, &s_rtv);
        bb->Release();
        if (!s_rtv) return;
    }
    ID3D11Texture2D* glassBB = nullptr;   // (08/11) vidrio: la imagen del juego de este cuadro, antes del menu
    if (menuShown && g_cfg.menuGlass) {
        ID3D11Resource* r = nullptr; s_rtv->GetResource(&r);
        if (r) { r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&glassBB); r->Release(); }
        if (glassBB && !GlassEnsure(glassBB)) { glassBB->Release(); glassBB = nullptr; }
    }
    {   // (04/11) resolucion cambiada: letras y espacios de nuevo (la textura de las fuentes se rehace en el NewFrame)
        DXGI_SWAP_CHAIN_DESC dd;
        if (SUCCEEDED(sc->GetDesc(&dd)) && dd.BufferDesc.Height > 0 && std::fabs((float)dd.BufferDesc.Height - s_fontsH) > 1.0f) {
            BuildFonts((float)dd.BufferDesc.Height);
            ImGui_ImplDX11_InvalidateDeviceObjects();
        }
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor = g_menuOpen.load();
    if (menuShown) { if (fis && !s_fullMenu && g_menuOpen) DrawPhysiqueWindow(); else if (!fis || s_fullMenu) DrawMenu(); }
    else if (hint) DrawPhysiqueHint();
    if (camHud) DrawCamaraHud(camFree, camSlow);
    if (lightIcons) DrawEditorLightIcons();
    if (sunCompass && !g_menuOpen) DrawSunCompass();
    if (g_cfg.live[426][0] > 0.5f) DrawSkeletonView();   // (09/11 f) vista del esqueleto (Diagnostico)
    if (mmCur) {   // (11/11) el juego esconde el cursor de Windows: una flecha chica donde esta el mouse (los menus son los del juego)
        ImDrawList* dl = ImGui::GetForegroundDrawList(); const float k = std::max(1.0f, ImGui::GetIO().DisplaySize.y / 1080.0f);
        const ImVec2 a(mmx, mmy), b(mmx, mmy + 19.0f * k), c(mmx + 5.2f * k, mmy + 14.6f * k), d(mmx + 13.5f * k, mmy + 13.5f * k);
        dl->AddTriangleFilled(a, b, c, IM_COL32(255, 255, 255, 255)); dl->AddTriangleFilled(a, c, d, IM_COL32(255, 255, 255, 255));
        const ImVec2 pts[4] = { a, b, c, d }; dl->AddPolyline(pts, 4, IM_COL32(0, 0, 0, 255), ImDrawFlags_Closed, 1.4f * k);
    }
    if (gizmo) MotorGizmoDraw();
    if (keyHud) KeyHudDraw();
    InspectDrawOverlay();
    // (31/10) teclas del juego: con 1 se bloquean solo mientras hay un campo de texto activo (buscador, nombres de presets)
    g_menuTyping = g_menuOpen && ImGui::GetIO().WantTextInput;
    g_blockGameKeys = (g_menuOpen && (g_cfg.blockKeys == 2 || (g_cfg.blockKeys == 1 && ImGui::GetIO().WantTextInput))) || FreeCamBlocksKeys();
    if (!s_btnPending.empty()) {
        std::string a = s_btnPending; s_btnPending.clear();
        if (a == "hair_export") { std::string st; FaceGenExportHair(st); s_status = st; }
        else if (a == "brow_save") { s_status = BrowSaveCurrent(); }
        else if (a == "tess_save") { s_status = FaceTessSaveCurrent(); }
        else if (a == "wr_save") { s_status = WrPlayerSaveCurrent(); }   // (10/11) arrugas por jugador   // (04/11 m) teselacion por jugador
        else if (a == "hair_reload") { LoadHairConfig(); s_status = "PES21FX_hair.ini recargado (" + std::to_string(g_cfg.hair.types.size()) + " tipos, " + std::to_string(g_cfg.hair.faces.size()) + " caras asignadas)"; }
        else if (a == "muscle_reset" || a.rfind("muscle_preset_", 0) == 0) {
            // (26/10) tipos de cuerpo para las tarjetas generales (muscle_reset = "Como el juego": todo en 1 y la definicion en 1).
            // Los interruptores y los valores propios de cada jugador (musculatura_jugadores.ini) no se tocan
            const int k = a == "muscle_reset" ? 0 : atoi(a.c_str() + 14);
            if (k >= 0 && k < MusclePresetCount()) {
                { std::lock_guard<std::mutex> lk(g_cfgMutex); MusclePresetApply(0, nullptr, k); if (k == 0) g_cfg.live[172][0] = 1.0f; }
                const std::string nm = MusclePresetName(k);
                s_status = SaveConfigValues() ? "Musculatura general: " + nm + " (guardado). Los jugadores con valores propios siguen igual."
                                              : "Musculatura general: " + nm + " (no se pudo guardar: usa Guardar)";
                Log("musculatura: tarjetas generales -> %s (boton del menu)", nm.c_str());
            } else s_status = "Tipo de cuerpo desconocido: " + a;
        }
        else s_status = "Accion desconocida: " + a;
    }
    if (s_presetPending) {
        int a = s_presetPending; s_presetPending = 0;
        std::string f = s_presetPendingFile, out;
        if (a == 1) s_pStatus = ApplyPresetFile(f) ? "Preset aplicado: " + f : "No se pudo leer " + f;
        else if (a == 2) s_pStatus = DeletePresetFile(f) ? "Borrado: " + f : "No se pudo borrar " + f;
        else if (a == 3) { ClearPreset(); s_pStatus = "Preset quitado: tus valores."; }
        else if (a == 4) s_pStatus = SavePresetForMatch(s_pName, s_pAuthor, s_pOnlyTime, out) ? "Guardado en presets\\" + out : "No se pudo guardar (sin estadio?)";
        s_presetsT = 0;
    }
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        if (g_cfg.clothDirty && Now() - g_cfg.clothChangeT > 0.4) SaveCloth();
    }
    ImGui::Render();
    if (glassBB) { GlassBlur(glassBB); glassBB->Release(); }
    ID3D11RenderTargetView* oldRT[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* oldDS = nullptr;
    g_dev.imm->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRT, &oldDS);
    g_dev.imm->OMSetRenderTargets(1, &s_rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_dev.imm->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRT, oldDS);
    for (auto* r : oldRT) if (r) r->Release();
    if (oldDS) oldDS->Release();
}

LRESULT OverlayWndProc(HWND, UINT, WPARAM, LPARAM, bool& handled) { handled = false; return 0; }

} // namespace fx
