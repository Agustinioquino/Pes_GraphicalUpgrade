// PES21 FX - menu en pantalla (Dear ImGui). Tecla Insert (o menu_key en [PES21FX] de PES21FX.ini).
#include "core.h"
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
static ImFont* s_fontBig = nullptr; static ImFont* s_fontBold = nullptr;
static ID3D11ShaderResourceView* s_logo = nullptr; static float s_logoAspect = 1.0f;

void OverlayResize() { if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; } }

static LRESULT CALLBACK MenuProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_menuOpen && s_init) {
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
    Log("menu listo (tecla Insert)");
}

static void DrawGroup(MenuGroup& g, int gid) {
    ImGui::PushID(1000 + gid);
    ImGui::BeginChild("card", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
    bool hasT = g.tIdx >= 0;
    bool on = hasT ? (g_cfg.live[g.tIdx][g.tComp] > 0.5f) : true;
    if (s_fontBold) ImGui::PushFont(s_fontBold);
    if (hasT) { if (ImGui::Checkbox(Utf8(g.title).c_str(), &on)) { float b = g_cfg.base[g.tIdx][g.tComp]; g_cfg.live[g.tIdx][g.tComp] = on ? (b > 0.5f ? b : 1.0f) : 0.0f; } }
    else ImGui::TextUnformatted(Utf8(g.title).c_str());
    if (s_fontBold) ImGui::PopFont();
    if (!g.desc.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.69f, 0.74f, 1)); ImGui::TextWrapped("%s", Utf8(g.desc).c_str()); ImGui::PopStyleColor(); }
    if (!g.sliders.empty()) {
        ImGui::Spacing();
        ImGui::BeginDisabled(!on);
        float w = ImGui::GetContentRegionAvail().x;
        for (auto& sl : g.sliders) {
            ImGui::PushID(&sl);
            float& val = g_cfg.live[sl.idx][sl.comp];
            ImGui::TextUnformatted(Utf8(sl.label).c_str());
            if (!sl.help.empty()) {
                ImGui::SameLine(); ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(380); ImGui::TextUnformatted(Utf8(sl.help).c_str()); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
            }
            ImGui::SetNextItemWidth(w - 70);
            if (!sl.options.empty()) {
                int cur = (int)(val + 0.5f); if (cur < 0) cur = 0; if (cur >= (int)sl.options.size()) cur = (int)sl.options.size() - 1;
                std::string prev = Utf8(sl.options[cur]);
                if (ImGui::BeginCombo("##c", prev.c_str())) {
                    for (int oi = 0; oi < (int)sl.options.size(); oi++) {
                        bool sel = oi == cur;
                        if (ImGui::Selectable(Utf8(sl.options[oi]).c_str(), sel)) val = (float)oi;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::SliderFloat("##s", &val, sl.lo, sl.hi, sl.fmt.c_str());
                if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("Ctrl+clic para escribir un valor exacto");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("<")) val = g_cfg.base[sl.idx][sl.comp];
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Volver al valor guardado (%.3f)", g_cfg.base[sl.idx][sl.comp]);
            ImGui::PopID();
        }
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::PopID();
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
    if (!s_status.empty()) ImGui::TextDisabled("%s", s_status.c_str());
    ImGui::Spacing();

    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        // una pestana por seccion de los grupos
        std::vector<std::string> secs;
        for (auto& g : g_cfg.groups) { std::string sname = g.section.empty() ? "Otros" : g.section; bool f = false; for (auto& x : secs) f |= (x == sname); if (!f) secs.push_back(sname); }
        for (auto& sname : secs) {
            if (ImGui::BeginTabItem(Utf8(sname).c_str())) {
                ImGui::BeginChild("scroll", ImVec2(0, 0), ImGuiChildFlags_None);
                int gid = 0;
                for (auto& g : g_cfg.groups) { std::string s2 = g.section.empty() ? "Otros" : g.section; if (s2 == sname) DrawGroup(g, gid); gid++; }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        if (ImGui::BeginTabItem("Avanzado")) {
            ImGui::BeginChild("scroll2", ImVec2(0, 0), ImGuiChildFlags_None);
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

void OverlayPresent(IDXGISwapChain* sc) {
    if (!g_dev.dev || !g_cfg.optOverlay) return;
    if (!s_init) Init(sc);
    bool down = (GetAsyncKeyState(g_cfg.menuKey) & 0x8000) != 0 && GetForegroundWindow() == s_hwnd;
    if (down && !s_keyWas) g_menuOpen = !g_menuOpen;
    s_keyWas = down;
    if (!g_menuOpen) return;
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
    ImGui::GetIO().MouseDrawCursor = true;
    DrawMenu();
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
