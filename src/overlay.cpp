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
    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", std::round(19.0f * k), nullptr, io.Fonts->GetGlyphRangesDefault())) io.Fonts->AddFontDefault();
    s_fontBig = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", std::round(26.0f * k), nullptr, io.Fonts->GetGlyphRangesDefault());
    s_fontBold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", std::round(19.0f * k), nullptr, io.Fonts->GetGlyphRangesDefault());
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

void OverlayResize() { if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; } }

static LRESULT CALLBACK MenuProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // (1/11) rueda del mouse para el zoom de la camara libre (mensaje normal o raw input, por si el juego pide NOLEGACY)
    if (!g_menuOpen && FreeCamActive()) {
        if (m == WM_MOUSEWHEEL) FreeCamWheel((int)GET_WHEEL_DELTA_WPARAM(w));
        else if (m == WM_INPUT) {
            RAWINPUT ri{}; UINT sz = sizeof(ri);
            if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 && ri.header.dwType == RIM_TYPEMOUSE &&
                (ri.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)) FreeCamWheel((int)(short)ri.data.mouse.usButtonData);
        }
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
    kIcoRevert = 0xf0e2, kIcoEye = 0xf06e, kIcoReload = 0xf021, kIcoSearch = 0xf002, kIcoX = 0xf00d, kIcoCompress = 0xf066, kIcoExpand = 0xf065,
    kIcoFilter = 0xf0b0, kIcoDot = 0xf111, kIcoHelp = 0xf059, kIcoDown = 0xf078, kIcoRight = 0xf054, kIcoKeys = 0xf11c, kIcoMuscle = 0xf44b,
};
// categoria (0 escena, 1 jugadores, 2 otros), orden dentro de ella e icono de cada seccion (la clave es la seccion en espanol)
static void SecInfo(const std::string& s, int& cat, int& ord, unsigned& ico) {
    struct E { const char* pre; int cat, ord; unsigned ico; };
    static const E kE[] = {
        { "Ilumin", 0, 0, kIcoLight }, { "Sombra", 0, 1, kIcoShadow }, { "Color", 0, 2, kIcoPalette }, { "Post", 0, 3, kIcoWand },
        { "Cancha", 0, 4, kIcoBall }, { "Estadio", 0, 5, kIcoStadium },
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
    ImGui::SliderFloat("##trk", v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp);
    if (!typing) ImGui::PopStyleColor(6);
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    const bool rclick = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    if (hov && !act && tip) ImGui::SetTooltip("%s", tip);
    if (!typing) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const float gs = std::min(st.GrabMinSize, b.x - a.x - 4.0f);
        const float x0 = a.x + 2.0f + gs * 0.5f, x1 = b.x - 2.0f - gs * 0.5f, cy = std::round((a.y + b.y) * 0.5f);
        auto px = [&](float x) { const float t = hi != lo ? std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f; return x0 + t * (x1 - x0); };
        const float th = std::max(4.0f, 5.0f * UIK()), kx = px(*v);
        const ImVec4 fill = changed ? ImVec4(0.95f, 0.78f, 0.33f, 1) : ImVec4(0.20f, 0.72f, 0.46f, 1);
        dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(x1, cy + th * 0.5f), ImGui::GetColorU32(hov || act ? ImVec4(0.30f, 0.33f, 0.40f, 1) : ImVec4(0.25f, 0.28f, 0.34f, 1)), th * 0.5f);
        if (kx > x0 + 0.5f) dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(kx, cy + th * 0.5f), ImGui::GetColorU32(fill), th * 0.5f);
        if (changed) { const float sx = px(savedV); dl->AddRectFilled(ImVec2(sx - 1.0f, cy - th * 1.3f), ImVec2(sx + 1.0f, cy + th * 1.3f), ImGui::GetColorU32(ImVec4(0.80f, 0.83f, 0.88f, 0.85f)), 1.0f); }
        const float r = (act ? 9.0f : hov ? 8.5f : 7.5f) * UIK();
        dl->AddCircleFilled(ImVec2(kx, cy + 1.0f), r + 1.0f, ImGui::GetColorU32(ImVec4(0, 0, 0, 0.45f)), 24);
        dl->AddCircleFilled(ImVec2(kx, cy), r, ImGui::GetColorU32(ImVec4(0.96f, 0.97f, 0.98f, 1)), 24);
        dl->AddCircleFilled(ImVec2(kx, cy), r * 0.42f, ImGui::GetColorU32(fill), 16);
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

static void DrawGroup(MenuGroup& g, int gid) {
    ImGui::PushID(1000 + gid);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.11f, 0.12f, 0.15f, 1));   // fondo de la tarjeta
    ImGui::BeginChild("card", ImVec2(0 * UIK(), 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
    ImGui::PopStyleColor();
    // (04/11 n) dentro de la tarjeta los sliders, casillas y selectores van un tono mas claro (antes eran del mismo color que la tarjeta)
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.18f, 0.20f, 0.25f, 1)); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.22f, 0.25f, 0.30f, 1));
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
    if (ImGui::SmallButton(s_iconsOk ? (Ico(col ? kIcoRight : kIcoDown) + "##cx").c_str() : (col ? "+" : "-"))) col = !col;
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
    if (col) { if (!gDesc.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.52f, 0.56f, 1)); ImGui::TextUnformatted(Utf8(gDesc.substr(0, 90) + (gDesc.size() > 90 ? "..." : "")).c_str()); ImGui::PopStyleColor(); } ImGui::PopStyleColor(3); ImGui::EndChild(); ImGui::Spacing(); ImGui::PopID(); return; }
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
                if (!isExt && (val < sl.lo || val > sl.hi)) { float cl = val < sl.lo ? sl.lo : sl.hi; Log("menu: %c%d = %g fuera del rango %g..%g del slider '%s': recortado a %g", "xyzw"[sl.comp], sl.idx, val, sl.lo, sl.hi, sl.label.c_str(), cl); val = cl; }
                char tipb[300];   // (08/11) el rango del slider en el aviso
                { char a[40], b[40]; snprintf(a, sizeof a, sl.fmt.c_str(), sl.lo); snprintf(b, sizeof b, sl.fmt.c_str(), sl.hi);
                  snprintf(tipb, sizeof tipb, TRS("Va de %s a %s. Arrastra o toca el numero para escribirlo. Clic derecho: volver a lo guardado."), a, b); }
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
            if (ImGui::SmallButton(IcoL(isOpen ? kIcoDown : kIcoRight, mb).c_str())) { if (isOpen) s_moreOpen.erase(g.title); else s_moreOpen.insert(g.title); }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS(isOpen ? "Volver a ver solo las principales" : "Los ajustes finos de esta mejora (en Ajustes se pueden ver siempre)"));
        }
    }
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
    if (sec.rfind("Cancha", 0) == 0) return
        "Que hay aca: pasto (sombreado entre hojas, zonas secas, variacion), rebote de luz verde del pasto hacia los jugadores, y cielo.\n"
        "Por donde empezar: 1) 'Rebote del pasto' tine un poco de verde la cara de abajo de piernas y short, como en TV. 2) El pasto se ve mejor en camara baja. 3) Los cambios de cielo se notan mas de tarde.\n"
        "Consejo: si el pasto queda 'empastado', baja la variacion macro.";
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
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.33f, 0.25f, 1));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, sel ? ImVec4(0.17f, 0.37f, 0.28f, 1) : ImVec4(0.15f, 0.17f, 0.21f, 1));
    const float hh = s_rail ? h * 1.25f : h;
    if (ImGui::Selectable("##nav", sel, ImGuiSelectableFlags_None, ImVec2(w, hh))) SelectPage(key);
    ImGui::PopStyleColor(2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize(), ty = p.y + (h - fs) * 0.5f, pad = 8.0f * UIK();
    if (s_rail) {   // (08/11) barra angosta: el icono centrado, el nombre en el aviso
        const ImU32 cI = ImGui::GetColorU32(sel ? ImVec4(0.40f, 0.90f, 0.62f, 1) : ImVec4(0.72f, 0.76f, 0.82f, 1));
        const std::string ic = Ico(ico); const ImVec2 is = ImGui::CalcTextSize(ic.c_str());
        dl->AddText(ImVec2(p.x + (w - is.x) * 0.5f, p.y + (hh - fs) * 0.5f), cI, ic.c_str());
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
    if (s_iconsOk && ico) { const std::string ic = Ico(ico); dl->AddText(ImVec2(x, ty), cIco, ic.c_str()); x += fs * 1.55f; }
    const float xr = p.x + w - pad - rs.x - (dot ? 12.0f * UIK() : 0.0f);
    const ImVec4 clip(x, p.y, std::max(x, xr - 4.0f * UIK()), p.y + h);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(x, ty), cText, label.c_str(), nullptr, 0.0f, &clip);
    if (dot) dl->AddCircleFilled(ImVec2(p.x + w - pad - rs.x - 7.0f * UIK(), p.y + h * 0.5f), 3.5f * UIK(), IM_COL32(242, 204, 89, 255));
    if (!right.empty()) dl->AddText(ImVec2(p.x + w - pad - rs.x, ty), cDim, right.c_str());
    ImGui::PopID();
}
// barra de cada pagina de tarjetas: solo activos, contraer / expandir. (08/11) Con iconos va a la derecha del titulo.
static bool IconToggle(const char* id, unsigned ico, bool on, const char* tip) {
    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.16f, 0.45f, 0.31f, 1) : ImVec4(0.16f, 0.18f, 0.22f, 1));
    const bool r = ImGui::Button((Ico(ico) + "##" + id).c_str(), ImVec2(ImGui::GetFrameHeight() * 1.25f, 0));
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return r;
}
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
    if (s_iconsOk && ico) { ImGui::TextColored(ImVec4(0.40f, 0.90f, 0.62f, 1), "%s", Ico(ico).c_str()); ImGui::SameLine(); }
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
static void DrawSectionExtras(const std::string& sname) {
    if (sname.rfind("Ropa", 0) == 0 && !g_cfg.clothStatus.empty()) { ImGui::TextDisabled("%s", g_cfg.clothStatus.c_str()); ImGui::Spacing(); }
    if (sname.rfind("Diagn", 0) == 0) {
        ImGui::TextWrapped("%s", TRS("Prueba automatica: toca cada grupo del menu (su interruptor o su primer deslizador), espera unos cuadros y mide si la imagen cambia. Pausa una repeticion con jugadores de cerca antes de correrla; tarda unos 30 segundos."));
        if (!MenuTestActive()) { if (ImGui::Button(TRS("Probar todo el menu"), ImVec2(180 * UIK(), 0))) MenuTestStart(); }
        else { if (ImGui::Button(TRS("Cancelar"), ImVec2(180 * UIK(), 0))) MenuTestStop(); }
        ImGui::SameLine(); ImGui::TextUnformatted(Utf8(MenuTestStatus()).c_str());
        size_t nres = MenuTestResults();
        if (nres) {
            ImGui::TextDisabled(TRS("ruido de la escena: %.4f (si es alto, la camara se movio: pausa y repeti)"), MenuTestNoise());
            if (ImGui::BeginTable("mt", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                for (size_t i = 0; i < nres; i++) {
                    std::string grp, sec; float df; int st;
                    if (!MenuTestResult_(i, grp, sec, df, st)) break;
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(Utf8(sec).c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(Utf8(grp).c_str());
                    ImGui::TableSetColumnIndex(2);
                    if (st == 1) ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1), TRS("cambia la imagen (%.4f)"), df);
                    else if (st == 0) ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1), TRS("sin efecto en esta escena (%.4f)"), df);
                    else ImGui::TextDisabled("%s", TRS("no se prueba (ropa)"));
                }
                ImGui::EndTable();
            }
        }
        ImGui::Spacing();
    }
    if (sname.rfind("Ilumin", 0) == 0) { ImGui::TextDisabled("%s", LightMeterSummary().c_str()); ImGui::Spacing(); }
    if (sname.rfind("Diagn", 0) == 0 || sname.rfind("Debug", 0) == 0) { const std::string gm = GpuMeterSummary();   // (07/11) medidor de VRAM y de ms de GPU (x375)
        if (!gm.empty()) { ImGui::TextWrapped("%s", gm.c_str()); ImGui::Spacing(); } }
    if (sname.rfind("Cancha", 0) == 0) { ImGui::TextDisabled(TRS("Pasto: parcelas lejanas del juego escaladas: %d en 3 s. Extendido: %d parcelas/cuadro (anillos %d/%d/%d), variante %u idx, camara %s (%.0f, %.0f), estado: %s"), g_grassDbg.sparseLast, g_grassDbg.patches, g_grassDbg.ring[0], g_grassDbg.ring[1], g_grassDbg.ring[2], g_grassDbg.lod, g_grassDbg.cam ? "ok" : "no", g_grassDbg.ex, g_grassDbg.ez, g_grassDbg.why ? g_grassDbg.why : "-"); ImGui::Spacing(); }
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
    ImGui::SetNextWindowBgAlpha(std::clamp(g_cfg.menuAlpha, 0.5f, 1.0f));   // (08/11) Ajustes: transparencia del menu
    bool open = true;
    ImGui::Begin("##pes21fx", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    {   // (08/11) segun el ancho: barra lateral con nombres o solo iconos, botones con texto o solo iconos
        const float cw = ImGui::GetContentRegionAvail().x;
        s_rail = s_iconsOk && cw < 840.0f * UIK();
        s_slim = s_iconsOk && cw < 640.0f * UIK();
    }
    if (!open) g_menuOpen = false;
    // (04/11 n) casillas, selectores y el buscador fuera de las tarjetas: un tono mas claro que el fondo de la ventana
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.18f, 0.22f, 1)); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.23f, 0.28f, 1));

    // --- atajos de teclado (04/11 n) ---
    const bool typing = io.WantTextInput, busy = ImGui::IsAnyItemActive();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) s_saveReq = true;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) s_focusSearch = true;
    if (!typing && !busy && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) s_undoReq = io.KeyShift ? 2 : 1;
    if (!typing && !busy && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) s_undoReq = 2;
    if (s_search[0] && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) s_search[0] = 0;

    // lo que hace falta saber para la barra de arriba (corto, con el mutex)
    int nChanged = 0, nErr = 0;
    { std::lock_guard<std::mutex> lk(g_cfgMutex); for (auto& g : g_cfg.groups) nChanged += GroupChanged(g) ? 1 : 0; }
    { std::lock_guard<std::mutex> lk2(g_errMutex); nErr = (int)g_compileErrors.size(); }

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
        if (dirty) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.52f, 0.34f, 1)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.62f, 0.41f, 1)); }
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
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.10f, 0.12f, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f * UIK());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f * UIK()));
        if (s_rail) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3.0f * UIK(), 6.0f * UIK()));
        ImGui::BeginChild("nav", ImVec2(navW, -footH), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
        if (s_rail) ImGui::PopStyleVar();
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
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // --- pagina ---
        ImGui::BeginChild("page", ImVec2(0, -footH), ImGuiChildFlags_None);
        const std::string q = LowerU(s_search);
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

    // acciones que no pueden correr con g_cfgMutex tomado
    if (s_saveReq) { s_saveReq = false; s_status = TRS(SaveConfigValues() ? "Guardado en PES21FX.ini" : "No se pudo guardar"); }
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
    const bool camFree = FreeCamActive(); const float camSlow = TiempoFactor();   // (1/11) aviso en pantalla
    const bool camHud = !g_menuOpen && (camFree || camSlow != 1.0f);
    const bool lightIcons = g_cfg.live[319][0] > 0.5f && g_cfg.live[297][0] > 0.5f && g_cfg.live[378][2] < 0.5f && SSSSEditorLightAge() < 0.5;   // (08/11) con la luz fija al mundo (z378) el icono (relativo a la camara) mentiria: no se dibuja   // (1/11) iconos de la luz del editor
    const bool sunCompass = g_cfg.live[175][0] > 0.5f && g_cfg.live[93][3] > 0.5f;   // (2/11) brujula del sol, solo en partido
    if (!g_menuOpen && !g_inspectOn && !hint && !camHud && !lightIcons && !sunCompass) return;
    if (!s_rtv) {
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) return;
        g_dev.dev->CreateRenderTargetView(bb, nullptr, &s_rtv);
        bb->Release();
        if (!s_rtv) return;
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
    if (g_menuOpen) { if (fis && !s_fullMenu) DrawPhysiqueWindow(); else DrawMenu(); }
    else if (hint) DrawPhysiqueHint();
    if (camHud) DrawCamaraHud(camFree, camSlow);
    if (lightIcons) DrawEditorLightIcons();
    if (sunCompass && !g_menuOpen) DrawSunCompass();
    InspectDrawOverlay();
    // (31/10) teclas del juego: con 1 se bloquean solo mientras hay un campo de texto activo (buscador, nombres de presets)
    g_menuTyping = g_menuOpen && ImGui::GetIO().WantTextInput;
    g_blockGameKeys = (g_menuOpen && (g_cfg.blockKeys == 2 || (g_cfg.blockKeys == 1 && ImGui::GetIO().WantTextInput))) || FreeCamBlocksKeys();
    if (!s_btnPending.empty()) {
        std::string a = s_btnPending; s_btnPending.clear();
        if (a == "hair_export") { std::string st; FaceGenExportHair(st); s_status = st; }
        else if (a == "brow_save") { s_status = BrowSaveCurrent(); }
        else if (a == "tess_save") { s_status = FaceTessSaveCurrent(); }   // (04/11 m) teselacion por jugador
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
