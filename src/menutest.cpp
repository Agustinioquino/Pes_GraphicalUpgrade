// PES21 FX - prueba automatica del menu: toca cada grupo (interruptor o primer deslizador) y mide si la imagen
// cambia de verdad. Se corre desde la pestana Diagnostico con el juego en pausa (repeticion detenida).
//   1) se captura la imagen base dos veces (para medir el ruido de la escena)
//   2) por cada grupo: se cambia el valor, se esperan unos cuadros (TAA), se captura, se compara y se restaura
// Resultado: grupos que cambian la imagen, grupos sin efecto en esta escena (puede ser que no haya piel/pasto/etc.
// en pantalla) y grupos que no se prueban (los de la ropa, que escriben cloth.ini y tardan en aplicarse).
#include "core.h"
#include <d3d11_1.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace fx {

struct MenuTestResult { std::string group, section; float diff = 0; int state = 0; };   // 0 sin cambio, 1 cambia, 2 no probado
struct MenuTestState {
    bool active = false;
    int phase = 0;              // 0 base, 1 base2, 2 grupos, 3 fin
    int wait = 0;
    int gi = -1;
    std::vector<float> base, base2;
    float noise = 0;
    int sIdx = -1, sComp = 0; float sSaved = 0;
    std::vector<MenuTestResult> results;
    std::string status;
    int tested = 0, changed = 0, skipped = 0;
};
static MenuTestState s_t;
static ID3D11Texture2D* s_stage = nullptr;
static UINT s_stW = 0, s_stH = 0; static DXGI_FORMAT s_stF = DXGI_FORMAT_UNKNOWN;

const MenuTestState& MenuTestGet() { return s_t; }
bool MenuTestActive() { return s_t.active; }
int  MenuTestCount(int what) { return what == 0 ? s_t.tested : what == 1 ? s_t.changed : s_t.skipped; }
float MenuTestNoise() { return s_t.noise; }
const std::string& MenuTestStatus() { return s_t.status; }
size_t MenuTestResults() { return s_t.results.size(); }
bool MenuTestResult_(size_t i, std::string& group, std::string& section, float& diff, int& state) {
    if (i >= s_t.results.size()) return false;
    group = s_t.results[i].group; section = s_t.results[i].section; diff = s_t.results[i].diff; state = s_t.results[i].state;
    return true;
}

void MenuTestStart() {
    s_t = MenuTestState();
    s_t.active = true; s_t.phase = 0; s_t.wait = 6; s_t.gi = -1;
    s_t.status = "Probando... (no muevas la camara)";
    Log("prueba del menu: inicio (%d grupos)", (int)g_cfg.groups.size());
}
void MenuTestStop() { if (s_t.active) { s_t.active = false; s_t.status = "Prueba cancelada"; } }

static float Lum8(const unsigned char* p, bool bgr) { return bgr ? (0.0722f * p[0] + 0.7152f * p[1] + 0.2126f * p[2]) / 255.f : (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.f; }
static float Half(unsigned short h) {
    unsigned int s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023; float v;
    if (e == 0) v = (float)m / 1024.f * powf(2.f, -14.f); else if (e == 31) v = 1e4f; else v = (1.f + (float)m / 1024.f) * powf(2.f, (float)e - 15.f);
    return s ? -v : v;
}

// muestrea la imagen en una grilla de 48x27 luminancias
static bool Capture(IDXGISwapChain* sc, std::vector<float>& out) {
    if (!g_dev.dev || !g_dev.imm) return false;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) return false;
    D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
    if (!s_stage || s_stW != d.Width || s_stH != d.Height || s_stF != d.Format) {
        if (s_stage) { s_stage->Release(); s_stage = nullptr; }
        D3D11_TEXTURE2D_DESC sd = d; sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0; sd.MipLevels = 1; sd.ArraySize = 1;
        if (FAILED(g_dev.dev->CreateTexture2D(&sd, nullptr, &s_stage))) { bb->Release(); return false; }
        s_stW = d.Width; s_stH = d.Height; s_stF = d.Format;
    }
    if (d.SampleDesc.Count > 1) g_dev.imm->ResolveSubresource(s_stage, 0, bb, 0, d.Format); else g_dev.imm->CopyResource(s_stage, bb);
    bb->Release();
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(g_dev.imm->Map(s_stage, 0, D3D11_MAP_READ, 0, &m))) return false;
    const int GW = 48, GH = 27;
    out.assign(GW * GH, 0.f);
    bool bgr = (d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || d.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS);
    bool rgb8 = (d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || d.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS);
    bool r10 = (d.Format == DXGI_FORMAT_R10G10B10A2_UNORM);
    bool f16 = (d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
    for (int gy = 0; gy < GH; gy++) for (int gx = 0; gx < GW; gx++) {
        // promedio de 4x4 pixeles alrededor del centro de la celda (menos ruido)
        int cx = (int)((gx + 0.5f) * d.Width / GW), cy = (int)((gy + 0.5f) * d.Height / GH);
        float acc = 0; int n = 0;
        for (int oy = -2; oy < 2; oy++) for (int ox = -2; ox < 2; ox++) {
            int x = cx + ox, y = cy + oy; if (x < 0 || y < 0 || x >= (int)d.Width || y >= (int)d.Height) continue;
            const unsigned char* p = (const unsigned char*)m.pData + (size_t)y * m.RowPitch + (size_t)x * (f16 ? 8 : 4);
            float l = 0;
            if (bgr) l = Lum8(p, true); else if (rgb8) l = Lum8(p, false);
            else if (r10) { unsigned int v = *(const unsigned int*)p; l = (0.2126f * (v & 1023) + 0.7152f * ((v >> 10) & 1023) + 0.0722f * ((v >> 20) & 1023)) / 1023.f; }
            else if (f16) { const unsigned short* h = (const unsigned short*)p; l = 0.2126f * Half(h[0]) + 0.7152f * Half(h[1]) + 0.0722f * Half(h[2]); }
            else l = Lum8(p, false);
            acc += l; n++;
        }
        out[gy * GW + gx] = n ? acc / n : 0;
    }
    g_dev.imm->Unmap(s_stage, 0);
    return true;
}
static float Diff(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0;
    double s = 0; for (size_t i = 0; i < a.size(); i++) s += fabs(a[i] - b[i]);
    return (float)(s / a.size());
}

// aplica el cambio de prueba al grupo gi (devuelve false si no se puede probar)
static bool Apply(int gi) {
    MenuGroup& g = g_cfg.groups[gi];
    if (g.tIdx >= 0) {
        s_t.sIdx = g.tIdx; s_t.sComp = g.tComp; s_t.sSaved = g_cfg.live[g.tIdx][g.tComp];
        bool on = s_t.sSaved > 0.5f;
        float b = g_cfg.base[g.tIdx][g.tComp];
        g_cfg.live[g.tIdx][g.tComp] = on ? 0.0f : (b > 0.5f ? b : 1.0f);
        return true;
    }
    for (auto& sl : g.sliders) {
        if (!sl.ext.empty()) continue;
        s_t.sIdx = sl.idx; s_t.sComp = sl.comp; s_t.sSaved = g_cfg.live[sl.idx][sl.comp];
        float mid = 0.5f * (sl.lo + sl.hi);
        if (!sl.optVals.empty()) g_cfg.live[sl.idx][sl.comp] = (fabsf(s_t.sSaved - sl.optVals[0]) < 1e-4f) ? sl.optVals.back() : sl.optVals[0];
        else g_cfg.live[sl.idx][sl.comp] = (s_t.sSaved > mid) ? sl.lo : sl.hi;
        return true;
    }
    return false;
}
static void Restore() { if (s_t.sIdx >= 0) { g_cfg.live[s_t.sIdx][s_t.sComp] = s_t.sSaved; s_t.sIdx = -1; } }

void MenuTestPresent(IDXGISwapChain* sc) {
    if (!s_t.active) return;
    if (s_t.wait > 0) { s_t.wait--; return; }
    std::vector<float> cap;
    if (s_t.phase == 0) {
        if (!Capture(sc, s_t.base)) { s_t.active = false; s_t.status = "No pude leer la imagen (formato no soportado)"; return; }
        s_t.phase = 1; s_t.wait = 6; return;
    }
    if (s_t.phase == 1) {
        Capture(sc, s_t.base2);
        s_t.noise = Diff(s_t.base, s_t.base2);
        Log("prueba del menu: ruido de la escena %.5f", s_t.noise);
        s_t.phase = 2;
    }
    if (s_t.phase == 2) {
        if (s_t.gi >= 0) {   // medir el grupo actual
            Capture(sc, cap);
            MenuGroup& g = g_cfg.groups[s_t.gi];
            MenuTestResult r; r.group = g.title; r.section = g.section; r.diff = Diff(s_t.base, cap);
            float thr = (std::max)(0.0025f, s_t.noise * 3.0f);
            r.state = r.diff > thr ? 1 : 0;
            if (r.state) s_t.changed++;
            s_t.tested++;
            s_t.results.push_back(r);
            Restore();
            // si la escena cambio (la base ya no sirve), se vuelve a tomar
            if (s_t.noise > 0.01f) { s_t.phase = 0; s_t.wait = 4; return; }
        }
        // siguiente grupo
        s_t.gi++;
        while (s_t.gi < (int)g_cfg.groups.size() && !Apply(s_t.gi)) {
            MenuTestResult r; r.group = g_cfg.groups[s_t.gi].title; r.section = g_cfg.groups[s_t.gi].section; r.state = 2;
            s_t.results.push_back(r); s_t.skipped++; s_t.gi++;
        }
        if (s_t.gi >= (int)g_cfg.groups.size()) {
            s_t.active = false; s_t.phase = 3;
            char b[160]; snprintf(b, sizeof(b), "Listo: %d grupos probados, %d cambian la imagen, %d sin efecto en esta escena, %d no probados (ropa)", s_t.tested, s_t.changed, s_t.tested - s_t.changed, s_t.skipped);
            s_t.status = b;
            Log("prueba del menu: %s", b);
            for (auto& r : s_t.results) if (r.state == 0) Log("  sin efecto: %s (%s) dif %.5f", r.group.c_str(), r.section.c_str(), r.diff);
            return;
        }
        char b[160]; snprintf(b, sizeof(b), "Probando %d/%d: %s", s_t.gi + 1, (int)g_cfg.groups.size(), g_cfg.groups[s_t.gi].title.c_str());
        s_t.status = b;
        s_t.wait = 8;
    }
}

} // namespace fx
