// PES21 FX - menu en pantalla (Dear ImGui). Tecla Insert (o menu_key en [PES21FX] de PES21FX.ini).
#include "core.h"
#include <set>
#include <cmath>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <DDSTextureLoader.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace fx {

std::atomic<bool> g_menuOpen{false};
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

void OverlayResize() { if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; } }

static LRESULT CALLBACK MenuProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if ((g_menuOpen || g_inspectOn) && s_init) {
        ImGui_ImplWin32_WndProcHandler(h, m, w, l);
        ImGuiIO& io = ImGui::GetIO();
        bool mouse = m >= WM_MOUSEFIRST && m <= WM_MOUSELAST;
        bool key = (m >= WM_KEYFIRST && m <= WM_KEYLAST) || m == WM_CHAR;
        if ((mouse && io.WantCaptureMouse) || (key && io.WantCaptureKeyboard)) return 0;
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
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 19.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
    s_fontBig = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 26.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
    s_fontBold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 19.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
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
static char s_search[96] = "";
static bool s_onlyActive = false;
static bool s_focusSearch = false;
static std::map<std::string, bool> s_collapsed;   // titulo -> contraido (sesion)
static int s_collapseAll = 0;                     // 1 = contraer todo, 2 = expandir todo (se aplica en el proximo dibujo)
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
    std::string hay = LowerU(g.title + " " + g.desc + " " + g.section);
    for (auto& sl : g.sliders) hay += " " + LowerU(sl.label);
    // todas las palabras de la busqueda tienen que aparecer
    size_t a = 0; while (a < q.size()) { size_t b = q.find(' ', a); if (b == std::string::npos) b = q.size(); std::string w = q.substr(a, b - a); if (!w.empty() && hay.find(w) == std::string::npos) return false; a = b + 1; }
    return true;
}
static bool GroupActive(const MenuGroup& g) { return g.tIdx < 0 || g_cfg.live[g.tIdx][g.tComp] > 0.5f; }

static void DrawGroup(MenuGroup& g, int gid) {
    ImGui::PushID(1000 + gid);
    ImGui::BeginChild("card", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
    bool hasT = g.tIdx >= 0;
    bool on = hasT ? (g_cfg.live[g.tIdx][g.tComp] > 0.5f) : true;
    // contraer / favorito / restablecer (fila de la derecha)
    if (s_collapsed.find(g.title) == s_collapsed.end()) s_collapsed[g.title] = g_cfg.compact;
    bool& col = s_collapsed[g.title];
    float rightW = 96; float availW = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::SmallButton(col ? "+" : "-")) col = !col;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(col ? "Expandir" : "Contraer");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    if (hasT) { if (ImGui::Checkbox(Utf8(g.title).c_str(), &on)) { float b = g_cfg.base[g.tIdx][g.tComp]; g_cfg.live[g.tIdx][g.tComp] = on ? (b > 0.5f ? b : 1.0f) : 0.0f; } }
    else ImGui::TextUnformatted(Utf8(g.title).c_str());
    if (s_fontBold) ImGui::PopFont();
    ImGui::EndGroup();
    ImGui::SameLine(availW - rightW);
    { bool fav = IsFav(g.title);
      if (fav) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.78f, 0.3f, 1));
      if (ImGui::SmallButton(fav ? "* fav" : "+ fav")) SetFav(g.title, !fav);
      if (fav) ImGui::PopStyleColor();
      if (ImGui::IsItemHovered()) ImGui::SetTooltip(fav ? "Quitar de Favoritos" : "Agregar a la pestana Favoritos");
      ImGui::SameLine();
      if (ImGui::SmallButton("<<")) { for (auto& sl : g.sliders) if (sl.ext.empty()) g_cfg.live[sl.idx][sl.comp] = g_cfg.base[sl.idx][sl.comp]; if (hasT) g_cfg.live[g.tIdx][g.tComp] = g_cfg.base[g.tIdx][g.tComp]; }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Volver todo el grupo a lo guardado"); }
    if (col) { if (!g.desc.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.52f, 0.56f, 1)); ImGui::TextUnformatted(Utf8(g.desc.substr(0, 90) + (g.desc.size() > 90 ? "..." : "")).c_str()); ImGui::PopStyleColor(); } ImGui::EndChild(); ImGui::Spacing(); ImGui::PopID(); return; }
    if (!g.desc.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.69f, 0.74f, 1)); ImGui::TextWrapped("%s", Utf8(g.desc).c_str()); ImGui::PopStyleColor(); }
    bool anyExt = false; for (auto& sl : g.sliders) anyExt |= !sl.ext.empty();
    if (anyExt && !g_cfg.clothFound) ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1), "No se encontro cloth-mod\\cloth.ini");
    if (!g.sliders.empty()) {
        ImGui::Spacing();
        ImGui::BeginDisabled(!on);
        float w = ImGui::GetContentRegionAvail().x;
        for (auto& sl : g.sliders) {
            ImGui::PushID(&sl);
            bool isExt = !sl.ext.empty();
            if (isExt && !g_cfg.clothFound) { ImGui::PopID(); continue; }
            if (isExt && !g_cfg.clothVals.count(sl.ext)) g_cfg.clothVals[sl.ext] = sl.def;
            float& val = isExt ? g_cfg.clothVals[sl.ext] : g_cfg.live[sl.idx][sl.comp];
            float before = val;
            { float savedV = isExt ? (g_cfg.clothLoaded.count(sl.ext) ? g_cfg.clothLoaded[sl.ext] : sl.def) : g_cfg.base[sl.idx][sl.comp];
              bool changed = fabsf(savedV - val) > 1e-4f;
              if (changed) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.45f, 1));
              ImGui::TextUnformatted(Utf8(sl.label).c_str());
              if (changed) { ImGui::PopStyleColor(); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cambiado (sin guardar). Guardado: %.3f", savedV); } }
            if (!sl.help.empty()) {
                ImGui::SameLine(); ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(380); ImGui::TextUnformatted(Utf8(sl.help).c_str()); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
            }
            ImGui::SetNextItemWidth(w - 70);
            if (!sl.options.empty()) {
                int cur = (int)(val + 0.5f); if (cur < 0) cur = 0; if (cur >= (int)sl.options.size()) cur = (int)sl.options.size() - 1;
                if (!sl.optVals.empty()) { cur = 0; for (int oi = 1; oi < (int)sl.optVals.size(); oi++) if (fabsf(sl.optVals[oi] - val) < fabsf(sl.optVals[cur] - val)) cur = oi; }
                std::string prev = Utf8(sl.options[cur]);
                if (ImGui::BeginCombo("##c", prev.c_str())) {
                    for (int oi = 0; oi < (int)sl.options.size(); oi++) {
                        bool sel = oi == cur;
                        if (ImGui::Selectable(Utf8(sl.options[oi]).c_str(), sel)) val = sl.optVals.empty() ? (float)oi : sl.optVals[oi];
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::SliderFloat("##s", &val, sl.lo, sl.hi, sl.fmt.c_str());
                if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("Ctrl+clic para escribir un valor exacto");
            }
            ImGui::SameLine();
            float saved = isExt ? (g_cfg.clothLoaded.count(sl.ext) ? g_cfg.clothLoaded[sl.ext] : sl.def) : g_cfg.base[sl.idx][sl.comp];
            if (ImGui::SmallButton("<")) val = saved;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(isExt ? "Volver al valor que tenia cloth.ini (%.4g); fabrica: %.4g" : "Volver al valor guardado (%.3f)", saved, sl.def);
            if (isExt && val != before) { g_cfg.clothDirty = true; g_cfg.clothChangeT = Now(); }
            ImGui::PopID();
        }
        ImGui::EndDisabled();
    }
    if (g.special == "musculatura_jugador") { ImGui::Spacing(); MuscleDrawPlayerPanel(); }
    if (!g.buttons.empty()) {
        ImGui::Spacing();
        for (auto& b : g.buttons) {
            ImGui::PushID(&b);
            if (ImGui::Button(Utf8(b.label).c_str())) s_btnPending = b.action;
            if (!b.help.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Utf8(b.help).c_str());
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::PopID();
}


// --- tutoriales: una tarjeta al principio de cada pestana (se apagan con "No mostrar mas" -> [PES21FX] tutorials = 0) ---
static std::set<std::string> s_tutDone;   // pestanas con "Entendido" en esta sesion
static const char* TutorialFor(const std::string& sec) {
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
static void DrawTutorial(const std::string& sec) {
    if (!g_cfg.tutorials || s_tutDone.count(sec)) return;
    const char* t = TutorialFor(sec);
    if (!t) return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.16f, 0.22f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::BeginChild(("tut_" + sec).c_str(), ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    ImGui::TextColored(ImVec4(0.85f, 0.68f, 0.28f, 1), "Tutorial: %s", Utf8(sec).c_str());
    if (s_fontBold) ImGui::PopFont();
    std::string txt = t;
    size_t a = 0;
    while (a < txt.size()) {
        size_t b = txt.find('\n', a); if (b == std::string::npos) b = txt.size();
        ImGui::TextWrapped("%s", txt.substr(a, b - a).c_str());
        ImGui::Spacing();
        a = b + 1;
    }
    if (ImGui::Button("Entendido", ImVec2(110, 0))) s_tutDone.insert(sec);
    ImGui::SameLine();
    if (ImGui::Button("No mostrar mas tutoriales", ImVec2(220, 0))) { g_cfg.tutorials = false; SaveOption("tutorials", "0"); }
    ImGui::SameLine(); ImGui::TextDisabled("(se vuelven a prender arriba, en 'Tutoriales')");
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
    ImGui::TextWrapped("Un preset guarda todos los interruptores y sliders del menu para un estadio. Cuando Sider avisa en que estadio se juega, el mod lo aplica solo; al cambiar a un estadio sin preset vuelve a tus valores.");
    ImGui::Spacing();
    if (!m.known) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1), "Sin datos del partido.");
        ImGui::TextWrapped("Falta el modulo de Sider: copiar PES21FX_match.lua a la carpeta modules de Sider y agregar  lua.module = \"PES21FX_match.lua\"  en sider.ini. Hasta que empiece un partido tampoco hay datos.");
    } else if (m.stadium < 0) {
        ImGui::TextDisabled("Todavia no empezo un partido (sin estadio).");
    } else {
        ImGui::Text("Estadio actual: id %d", m.stadium);
        ImGui::SameLine(); ImGui::TextDisabled("  %s  |  %s", m.timeofday == 1 ? "noche" : m.timeofday == 0 ? "dia" : "hora ?", m.weather == 1 ? "lluvia" : m.weather == 0 ? "despejado" : "clima ?");
    }
    if (!g_cfg.presetActive.file.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1), "Preset activo: %s", g_cfg.presetActive.name.empty() ? g_cfg.presetActive.file.c_str() : g_cfg.presetActive.name.c_str());
        if (!g_cfg.presetActive.author.empty()) { ImGui::SameLine(); ImGui::TextDisabled(" por %s", g_cfg.presetActive.author.c_str()); }
        ImGui::SameLine();
        if (ImGui::SmallButton("Quitar (volver a mis valores)")) pending = 3;
        ImGui::TextDisabled("Ojo: con un preset activo, el boton Guardar de arriba escribe estos valores como tuyos.");
    } else {
        ImGui::TextDisabled("Preset activo: ninguno (tus valores).");
    }
    ImGui::Separator();
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    ImGui::TextUnformatted("Guardar los valores actuales como preset de este estadio");
    if (s_fontBold) ImGui::PopFont();
    ImGui::SetNextItemWidth(300); ImGui::InputText("Nombre", s_pName, sizeof(s_pName));
    ImGui::SetNextItemWidth(300); ImGui::InputText("Autor", s_pAuthor, sizeof(s_pAuthor));
    ImGui::Checkbox("Solo para esta hora (dia / noche)", &s_pOnlyTime);
    ImGui::SameLine(); ImGui::TextDisabled("(?)"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Marcado: el preset vale solo de dia o solo de noche (segun el partido actual).\nSin marcar: vale para el estadio a cualquier hora.");
    ImGui::BeginDisabled(m.stadium < 0);
    if (ImGui::Button("Guardar preset para este estadio", ImVec2(280, 0))) pending = 4;
    ImGui::EndDisabled();
    if (!s_pStatus.empty()) ImGui::TextDisabled("%s", s_pStatus.c_str());
    ImGui::Separator();
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    ImGui::TextUnformatted("Presets instalados");
    if (s_fontBold) ImGui::PopFont();
    ImGui::TextDisabled("Carpeta PES21FX\\presets. Para compartir uno: pasar el archivo; el otro lo copia ahi y listo.");
    if (Now() - s_presetsT > 2.0) { s_presetsT = Now(); ScanPresets(s_presets); }
    if (s_presets.empty()) ImGui::TextDisabled("(ninguno)");
    int id = 0;
    for (auto& p : s_presets) {
        ImGui::PushID(id++);
        bool active = p.file == g_cfg.presetActive.file;
        if (active) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.85f, 0.55f, 1));
        ImGui::Text("%s", p.name.empty() ? p.file.c_str() : p.name.c_str());
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine(); ImGui::TextDisabled("  %s%s%s  estadio %d%s  (%d valores)", p.author.empty() ? "" : "por ", p.author.c_str(), p.author.empty() ? "" : "  |", p.stadium, p.timeofday == 1 ? " noche" : p.timeofday == 0 ? " dia" : "", (int)p.values.size());
        ImGui::SameLine(ImGui::GetWindowWidth() - 150);
        if (ImGui::SmallButton("Aplicar")) { pending = 1; pendingFile = p.file; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Borrar")) { pending = 2; pendingFile = p.file; }
        ImGui::PopID();
    }
    // acciones diferidas: se ejecutan despues de soltar g_cfgMutex (ver DrawMenu)
    if (pending) {
        s_presetPending = pending; s_presetPendingFile = pendingFile; pending = 0;
    }
}

static void DrawMenu() {
    ImGui::SetNextWindowSize(ImVec2(640, 760), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(40, 60), ImGuiCond_FirstUseEver);
    bool open = true;
    ImGui::Begin("##pes21fx", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    if (!open) g_menuOpen = false;
    // --- cabecera: logo del PES Editor + nombre del mod ---
    float headY = ImGui::GetCursorPosY();
    if (s_logo) {
        float h = 64.0f;
        ImGui::Image((ImTextureID)(intptr_t)s_logo, ImVec2(h * s_logoAspect, h));
        ImGui::SameLine();
        ImGui::SetCursorPosY(headY + 6);
        ImGui::BeginGroup();
        if (s_fontBold) ImGui::PushFont(s_fontBold);
        ImGui::TextColored(ImVec4(0.85f, 0.68f, 0.28f, 1), "FX  0.2");
        if (s_fontBold) ImGui::PopFont();
        ImGui::TextDisabled("Mod grafico  |  Insert: cerrar");
        ImGui::EndGroup();
    } else {
        if (s_fontBig) ImGui::PushFont(s_fontBig);
        ImGui::TextUnformatted("PES21 FX");
        if (s_fontBig) ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("  0.2   |   Insert: cerrar");
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 60);
    ImGui::SetCursorPosY(headY);
    if (ImGui::Button("X", ImVec2(34, 0))) g_menuOpen = false;
    ImGui::Separator();
    int rep = g_replacedCount.load(), fail = g_failedCount.load();
    ImGui::PushStyleColor(ImGuiCol_Text, fail ? ImVec4(0.95f, 0.55f, 0.35f, 1) : ImVec4(0.45f, 0.85f, 0.55f, 1));
    ImGui::Text("%s  %d shaders propios activos", fail ? "!" : "OK", rep);
    ImGui::PopStyleColor();
    if (fail) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1), "   %d con error (ver pestana Errores)", fail); }
    ImGui::Spacing();
    if (ImGui::Button("Guardar", ImVec2(120, 0))) s_status = SaveConfigValues() ? "Guardado en PES21FX.ini" : "No se pudo guardar";
    ImGui::SameLine();
    if (ImGui::Button("Recargar shaders", ImVec2(150, 0))) { ReloadShaders(); s_status = "Shaders recargados"; }
    ImGui::SameLine();
    if (ImGui::Button("Recargar config", ImVec2(140, 0))) { LoadConfig(); ReloadShaders(); s_status = "Configuracion recargada"; }
    ImGui::SameLine();
    ImGui::Checkbox("Ver original", &g_cfg.bypass);
    ImGui::SameLine();
    if (ImGui::Checkbox("Tutoriales", &g_cfg.tutorials)) { SaveOption("tutorials", g_cfg.tutorials ? "1" : "0"); if (g_cfg.tutorials) s_tutDone.clear(); }
    if (!s_status.empty()) ImGui::TextDisabled("%s", s_status.c_str());
    // --- catalogo de shaders: capturar lo que se dibuja y escribir las fichas ---
    if (!g_catalogActive) {
    { bool ins = g_inspectOn.load(); if (ins) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.25f, 0.2f, 1));
      if (ImGui::Button(ins ? "Inspector: ON" : "Inspector", ImVec2(110, 0))) { g_inspectOn = !ins; s_status = ins ? "Inspector apagado" : "Inspector visual: flechas sobre cada parte conocida; clic en una ficha para ver sus texturas"; }
      if (ins) ImGui::PopStyleColor();
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Modo debug visual: marca en pantalla cara, pelo, kit, medias, botines, pasto y cielo, que hace el mod en cada uno y de donde sale cada textura (juego / archivo / generada)."); }
        ImGui::SameLine();
        if (ImGui::Button("Capturar shaders", ImVec2(150, 0))) CatalogStart();
        ImGui::SameLine(); ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Anota cada shader que se dibuja (render target, vertices, orden) mientras jugas.\nDetener escribe PES21FX\\catalog\\shaders.txt con una ficha por shader (rol, cbuffers, texturas, uso)\ny catalog\\frame.txt con la linea de tiempo del ultimo cuadro. Pasalo a Claude para que sepa que toca cada uno.");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.25f, 0.2f, 1));
        if (ImGui::Button("Detener y escribir catalogo", ImVec2(230, 0))) {
            UINT w = 0, h = 0;
            if (s_rtv) { ID3D11Resource* res = nullptr; s_rtv->GetResource(&res); ID3D11Texture2D* t = nullptr;
                if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); w = d.Width; h = d.Height; t->Release(); }
                if (res) res->Release(); }
            CatalogStop(w, h, AllShaders());
        }
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (ImGui::Button("Volcar cbuffers", ImVec2(130, 0))) CatalogDumpCBs();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Escribe catalog\\cbuffers.txt: los valores que el motor le pasa a la composicion y a la luz del sol\n(sol, exposicion, niebla, matrices de sombra) con el nombre de cada campo. Hacelo con un partido en pantalla.");
    ImGui::SameLine();
    if (ImGui::Button("Volcar pasto", ImVec2(110, 0))) CatalogProbeGrass();
    ImGui::SameLine();
    if (!g_recordOn) { if (ImGui::Button("Grabar partido", ImVec2(130, 0))) CatalogRecord(true); }
    else { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.25f, 0.2f, 1)); if (ImGui::Button("Detener grabacion", ImVec2(150, 0))) CatalogRecord(false); ImGui::PopStyleColor(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mientras jugas, cada 5 s vuelca pasto y cbuffers numerados en catalog\\rec\\ (con la camara de ese momento)\ny deja la captura de shaders andando. Al terminar: Detener grabacion y despues Detener y escribir catalogo.");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mide las parcelas de pasto que dibuja el juego en un cuadro (tamano, grilla, cantidad)\ny lo escribe en catalog\\grass.txt. Hacelo con un partido en pantalla.");
    { std::string cst = g_catalogStatus; if (!cst.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", cst.c_str()); } }
    ImGui::Spacing();

    std::lock_guard<std::mutex> lk(g_cfgMutex);
    // --- 19/10: barra de comodidad: buscar (Ctrl+F), solo activos, contraer/expandir, tamano ---
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F) && ImGui::GetIO().KeyCtrl) s_focusSearch = true;
        if (s_focusSearch) { ImGui::SetKeyboardFocusHere(); s_focusSearch = false; }
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##buscar", "Buscar opcion (Ctrl+F)", s_search, sizeof(s_search));
        ImGui::SameLine(); if (ImGui::SmallButton("x")) s_search[0] = 0;
        ImGui::SameLine(); ImGui::Checkbox("Solo activos", &s_onlyActive);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mostrar solo los grupos con el interruptor prendido");
        ImGui::SameLine(); if (ImGui::SmallButton("Contraer todo")) s_collapseAll = 1;
        ImGui::SameLine(); if (ImGui::SmallButton("Expandir todo")) s_collapseAll = 2;
        ImGui::SameLine(); ImGui::SetNextItemWidth(90);
        float sc = g_cfg.uiScale;
        if (ImGui::SliderFloat("##uiscale", &sc, 0.8f, 1.6f, "x%.2f")) { g_cfg.uiScale = sc; ImGui::GetIO().FontGlobalScale = sc; }
        if (ImGui::IsItemDeactivatedAfterEdit()) { char b[32]; snprintf(b, sizeof(b), "%.2f", g_cfg.uiScale); SaveOption("ui_scale", b); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tamano del menu");
        ImGui::SameLine(); if (ImGui::Checkbox("Compacto", &g_cfg.compact)) { SaveOption("compacto", g_cfg.compact ? "1" : "0"); s_collapseAll = g_cfg.compact ? 1 : 2; }
        ImGui::SameLine(); if (ImGui::Checkbox("Captura auto", &g_cfg.autoCap)) SaveOption("auto_captura", g_cfg.autoCap ? "1" : "0");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("En partido, cada %.0f s guarda una imagen (jpg) en PES21FX\\capturas\\ y los valores de luz de ese momento en capturas\\luz.txt,\npara que Claude calibre la luz sin que tengas que sacar capturas. Tope: %d imagenes por sesion.", g_cfg.autoCapSec, g_cfg.autoCapMax);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Abrir los grupos contraidos (solo el titulo y el interruptor); + para desplegar uno");
    }
    if (s_collapseAll) { for (auto& g : g_cfg.groups) s_collapsed[g.title] = (s_collapseAll == 1); s_collapseAll = 0; }
    std::string q = LowerU(s_search);
    if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (!q.empty()) {   // resultados de la busqueda, de todas las pestanas
            if (ImGui::BeginTabItem("Resultados", nullptr, ImGuiTabItemFlags_SetSelected)) {
                ImGui::BeginChild("scrollR", ImVec2(0, 0), ImGuiChildFlags_None);
                int gid = 0, n = 0;
                for (auto& g : g_cfg.groups) { if (GroupMatches(g, q) && (!s_onlyActive || GroupActive(g))) { ImGui::TextDisabled("%s", Utf8(g.section).c_str()); DrawGroup(g, gid); n++; } gid++; }
                if (!n) ImGui::TextDisabled("Nada coincide con \"%s\"", s_search);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        if (!g_cfg.favorites.empty()) {
            if (ImGui::BeginTabItem("Favoritos")) {
                ImGui::BeginChild("scrollF", ImVec2(0, 0), ImGuiChildFlags_None);
                ImGui::TextDisabled("Tus grupos marcados con * fav, de todas las pestanas.");
                int gid = 0;
                for (auto& g : g_cfg.groups) { if (IsFav(g.title) && (!s_onlyActive || GroupActive(g))) { ImGui::TextDisabled("%s", Utf8(g.section).c_str()); DrawGroup(g, gid); } gid++; }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        // una pestana por seccion de los grupos
        std::vector<std::string> secs;
        for (auto& g : g_cfg.groups) { std::string sname = g.section.empty() ? "Otros" : g.section; bool f = false; for (auto& x : secs) f |= (x == sname); if (!f) secs.push_back(sname); }
        for (auto& sname : secs) {
            if (ImGui::BeginTabItem(Utf8(sname).c_str())) {
                ImGui::BeginChild("scroll", ImVec2(0, 0), ImGuiChildFlags_None);
                DrawTutorial(sname);
                if (sname.rfind("Ropa", 0) == 0 && !g_cfg.clothStatus.empty()) { ImGui::TextDisabled("%s", g_cfg.clothStatus.c_str()); ImGui::Spacing(); }
                if (sname.rfind("Diagn", 0) == 0) {
                    ImGui::TextWrapped("Prueba automatica: toca cada grupo del menu (su interruptor o su primer deslizador), espera unos cuadros y mide si la imagen cambia. Pausa una repeticion con jugadores de cerca antes de correrla; tarda unos 30 segundos.");
                    if (!MenuTestActive()) { if (ImGui::Button("Probar todo el menu", ImVec2(180, 0))) MenuTestStart(); }
                    else { if (ImGui::Button("Cancelar", ImVec2(180, 0))) MenuTestStop(); }
                    ImGui::SameLine(); ImGui::TextUnformatted(Utf8(MenuTestStatus()).c_str());
                    size_t nres = MenuTestResults();
                    if (nres) {
                        ImGui::TextDisabled("ruido de la escena: %.4f (si es alto, la camara se movio: pausa y repeti)", MenuTestNoise());
                        if (ImGui::BeginTable("mt", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                            for (size_t i = 0; i < nres; i++) {
                                std::string grp, sec; float df; int st;
                                if (!MenuTestResult_(i, grp, sec, df, st)) break;
                                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(Utf8(sec).c_str());
                                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(Utf8(grp).c_str());
                                ImGui::TableSetColumnIndex(2);
                                if (st == 1) ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1), "cambia la imagen (%.4f)", df);
                                else if (st == 0) ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1), "sin efecto en esta escena (%.4f)", df);
                                else ImGui::TextDisabled("no se prueba (ropa)");
                            }
                            ImGui::EndTable();
                        }
                    }
                    ImGui::Spacing();
                }
                if (sname.rfind("Ilumin", 0) == 0) { ImGui::TextDisabled("%s", LightMeterSummary().c_str()); ImGui::Spacing(); }
                if (sname.rfind("Cancha", 0) == 0) { ImGui::TextDisabled("Pasto: parcelas lejanas del juego escaladas: %d en 3 s. Extendido: %d parcelas/cuadro (anillos %d/%d/%d), variante %u idx, camara %s (%.0f, %.0f), estado: %s", g_grassDbg.sparseLast, g_grassDbg.patches, g_grassDbg.ring[0], g_grassDbg.ring[1], g_grassDbg.ring[2], g_grassDbg.lod, g_grassDbg.cam ? "ok" : "no", g_grassDbg.ex, g_grassDbg.ez, g_grassDbg.why ? g_grassDbg.why : "-"); ImGui::Spacing(); }
                int gid = 0;
                for (auto& g : g_cfg.groups) { std::string s2 = g.section.empty() ? "Otros" : g.section; if (s2 == sname && (!s_onlyActive || GroupActive(g))) DrawGroup(g, gid); gid++; }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        if (ImGui::BeginTabItem("Presets")) {
            ImGui::BeginChild("scrollP", ImVec2(0, 0), ImGuiChildFlags_None);
            DrawTutorial("Presets");
            DrawPresetsTab();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Avanzado")) {
            ImGui::BeginChild("scroll2", ImVec2(0, 0), ImGuiChildFlags_None);
            DrawTutorial("Avanzado");
            if (ImGui::CollapsingHeader("Efectos por tecla", ImGuiTreeNodeFlags_DefaultOpen)) {
                int id = 0;
                for (auto& k : g_cfg.keys) {
                    ImGui::PushID(id++);
                    std::string lab = Utf8(k.label);
                    if (k.type == KeyBind::Cycle) {
                        if (ImGui::Button("Siguiente")) KeyActionUnlocked(k, 1);
                        ImGui::SameLine(); ImGui::TextUnformatted(lab.c_str());
                    } else if (k.type == KeyBind::Toggle && !k.assigns.empty()) {
                        bool offSwitch = k.assigns[0].values[0] <= 0.0f;
                        bool on = offSwitch ? !k.active : k.active;
                        if (ImGui::Checkbox(lab.c_str(), &on)) KeyActionUnlocked(k, 0);
                    }
                    ImGui::PopID();
                }
            }
            if (ImGui::CollapsingHeader("Todos los parametros")) {
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
                        ImGui::SetNextItemWidth(120);
                        ImGui::DragFloat(name, &g_cfg.live[i][c], 0.01f, -100.0f, 100.0f, "%.3f");
                        ImGui::SameLine();
                    }
                    ImGui::NewLine();
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        {
            std::lock_guard<std::mutex> lk2(g_errMutex);
            if (!g_compileErrors.empty() && ImGui::BeginTabItem("Errores")) {
                ImGui::BeginChild("scroll3", ImVec2(0, 0), ImGuiChildFlags_None);
                for (auto& e : g_compileErrors) ImGui::TextWrapped("%s", e.c_str());
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// --- 24/10: menu Fisico del juego (Editar > Jugadores > Fisico): Insert abre una ventana chica con la musculatura de ese
// jugador (la tarjeta "Musculatura por jugador"); "Menu completo" pasa al menu de siempre. Al entrar, un aviso abajo.
static bool s_fullMenu = false;
static void DrawPhysiqueWindow() {
    ImGui::SetNextWindowSize(ImVec2(560, 680), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(40, 60), ImGuiCond_FirstUseEver);
    bool open = true;
    ImGui::Begin("Musculatura del jugador##fisico", &open, ImGuiWindowFlags_NoCollapse);
    if (!open) g_menuOpen = false;
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    ImGui::TextColored(ImVec4(0.85f, 0.68f, 0.28f, 1), "PES21 FX  -  musculatura en Fisico");
    if (s_fontBold) ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 150);
    if (ImGui::Button("Menu completo", ImVec2(130, 0))) s_fullMenu = true;
    ImGui::TextDisabled("Insert: cerrar. Los valores de este jugador se guardan solos.");
    ImGui::Separator();
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        bool on = g_cfg.live[164][0] > 0.5f;
        if (ImGui::Checkbox("Musculatura prendida", &on)) { float b = g_cfg.base[164][0]; g_cfg.live[164][0] = on ? (b > 0.5f ? b : 1.0f) : 0.0f; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("El interruptor de \"Musculatura (prueba)\" (vale para todos; se guarda con Guardar en el menu completo).");
        ImGui::BeginChild("fisico_panel", ImVec2(0, 0), ImGuiChildFlags_None);
        MuscleDrawPlayerPanel();
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
    ImGui::TextUnformatted("Musculatura: ultima fila de la lista");
    ImGui::SameLine(); ImGui::TextDisabled("(Insert: ventana del mod)");
    ImGui::End();
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
    if (!g_menuOpen && !g_inspectOn && !hint) return;
    if (!s_rtv) {
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) return;
        g_dev.dev->CreateRenderTargetView(bb, nullptr, &s_rtv);
        bb->Release();
        if (!s_rtv) return;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor = g_menuOpen.load();
    if (g_menuOpen) { if (fis && !s_fullMenu) DrawPhysiqueWindow(); else DrawMenu(); }
    else if (hint) DrawPhysiqueHint();
    InspectDrawOverlay();
    if (!s_btnPending.empty()) {
        std::string a = s_btnPending; s_btnPending.clear();
        if (a == "hair_export") { std::string st; FaceGenExportHair(st); s_status = st; }
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
