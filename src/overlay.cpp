// PES21 FX - menu en pantalla (Dear ImGui). Tecla Insert (o menu_key en [PES21FX] de PES21FX.ini).
#include "core.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace fx {

std::atomic<bool> g_menuOpen{false};
static bool s_init = false;
static HWND s_hwnd = nullptr;
static WNDPROC s_oldProc = nullptr;
static ID3D11RenderTargetView* s_rtv = nullptr;
static bool s_keyWas = false;
static std::string s_status;

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
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 6.0f;
    ImGui_ImplWin32_Init(s_hwnd);
    ImGui_ImplDX11_Init(g_dev.dev, g_dev.imm);
    s_oldProc = (WNDPROC)SetWindowLongPtrW(s_hwnd, GWLP_WNDPROC, (LONG_PTR)MenuProc);
    s_init = true;
    Log("menu listo (tecla Insert)");
}

static void DrawMenu() {
    ImGui::SetNextWindowSize(ImVec2(560, 640), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(40, 60), ImGuiCond_FirstUseEver);
    bool open = true;
    ImGui::Begin("PES21 FX 0.1", &open);
    if (!open) g_menuOpen = false;
    ImGui::Text("Shaders propios activos: %d   errores: %d", g_replacedCount.load(), g_failedCount.load());
    if (ImGui::Button("Guardar")) s_status = SaveConfigValues() ? "Guardado en PES21FX.ini" : "No se pudo guardar";
    ImGui::SameLine();
    if (ImGui::Button("Recargar shaders")) { ReloadShaders(); s_status = "Shaders recargados"; }
    ImGui::SameLine();
    if (ImGui::Button("Recargar config")) { LoadConfig(); ReloadShaders(); s_status = "Configuracion recargada"; }
    ImGui::SameLine();
    ImGui::Checkbox("Ver original", &g_cfg.bypass);
    if (!s_status.empty()) ImGui::TextDisabled("%s", s_status.c_str());
    ImGui::Separator();

    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (ImGui::CollapsingHeader("Efectos", ImGuiTreeNodeFlags_DefaultOpen)) {
        int id = 0;
        for (auto& k : g_cfg.keys) {
            ImGui::PushID(id++);
            std::string lab = Utf8(k.label);
            if (k.type == KeyBind::Cycle) {
                if (ImGui::Button("Siguiente")) KeyActionUnlocked(k, 1);
                ImGui::SameLine(); ImGui::TextUnformatted(lab.c_str());
            } else if (k.type == KeyBind::Toggle && !k.assigns.empty()) {
                // si la tecla pone 0 (o negativo) es un interruptor de APAGADO; si pone otro valor, lo PRENDE
                bool offSwitch = k.assigns[0].values[0] <= 0.0f;
                bool on = offSwitch ? !k.active : k.active;
                if (ImGui::Checkbox(lab.c_str(), &on)) KeyActionUnlocked(k, 0);
            }
            ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader("Parametros")) {
        std::string lastComment;
        const char* cn = "xyzw";
        for (int i = 0; i < kParamCount; i++) {
            bool any = false;
            for (int c = 0; c < 4; c++) any |= g_cfg.info[i][c].defined;
            if (!any) continue;
            for (int c = 0; c < 4; c++) {
                if (!g_cfg.info[i][c].defined) continue;
                const std::string& cm = g_cfg.info[i][c].comment;
                if (!cm.empty() && cm != lastComment) { ImGui::TextWrapped("%s", Utf8(cm).c_str()); lastComment = cm; }
            }
            for (int c = 0; c < 4; c++) {
                if (!g_cfg.info[i][c].defined) continue;
                char name[16]; if (i) snprintf(name, sizeof(name), "%c%d", cn[c], i); else snprintf(name, sizeof(name), "%c", cn[c]);
                ImGui::SetNextItemWidth(110);
                ImGui::DragFloat(name, &g_cfg.live[i][c], 0.01f, -100.0f, 100.0f, "%.3f");
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }
    }
    {
        std::lock_guard<std::mutex> lk2(g_errMutex);
        if (!g_compileErrors.empty() && ImGui::CollapsingHeader("Errores de compilacion")) {
            for (auto& e : g_compileErrors) ImGui::TextWrapped("%s", e.c_str());
        }
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
