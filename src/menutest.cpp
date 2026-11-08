// PES21 FX - prueba automatica del menu (v2, 08/11). Agustin: "que le de a un boton y pruebe opciones y los devuelva para que
// vos lo lea". Se corre desde la pestana Diagnostico con la camara quieta (repeticion en pausa o el editor):
//   1) base: dos capturas (ruido de la escena), ms de CPU y de GPU por cuadro y por efecto (medidor), una foto jpg
//   2) por cada grupo PRENDIDO: se apaga su interruptor, se esperan unos cuadros (TAA), se mide lo mismo y se fotografia;
//      la diferencia de ms con la base = lo que cuesta ese efecto; la diferencia de imagen = si hace algo en esta escena.
//      Los grupos sin interruptor cambian su primer deslizador (solo imagen). Ropa, camara y diagnostico no se tocan.
//   3) cada vista de depuracion (cascadas, sombra sola, rebote, piel, pasto...) se prende un momento y se fotografia
//   4) se vuelve todo a como estaba y se escribe PES21FX\diagnostico\<fecha>\ resumen.txt + informe.json + capturas/
// No se guarda nada en el ini (SaveConfigValues se niega mientras corre). Esc o "Cancelar" la cortan (el informe parcial queda).
#include "core.h"
#include <d3d11_1.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace fx {

void GpuMeterTotals(double& frameMs, int& frames, double* zoneMs);   // gpumeter.cpp
static const char* const kZoneNm[kGpuZoneN] = { "SSSS piel", "GTAO", "SSGI", "medidor de luz", "ropa contra el cuerpo", "desgaste de la cancha",
    "pasto", "caras", "caras HD", "aro del Fisico", "copia del cuadro", "menu Insert", "vidrio de menus" };

struct Meas { double cpuMs = 0, gpuMs = 0; double zone[kGpuZoneN] = {}; int n = 0, gn = 0; };
struct MenuTestResult { std::string group, section, shot; float diff = 0; int state = 0; float cpuSave = 0, gpuSave = 0; float zoneSave[kGpuZoneN] = {}; };   // 0 sin cambio, 1 cambia, 2 no probado, 3 apagado (no se mide)
struct Step { int kind = 0; int gi = -1; int idx = -1, comp = 0; float val = 0; std::string label, section; };   // kind 0 base, 1 grupo, 2 vista
struct MenuTestState {
    bool active = false;
    std::vector<Step> steps; size_t si = 0;
    int phase = 0;              // 0 aplicar, 1 asentar, 2 medir, 3 capturar, 4 restaurar
    int wait = 0;
    std::vector<float> base, base2;
    float noise = 0;
    int sIdx = -1, sComp = 0; float sSaved = 0;
    Meas base0, cur; double cpu0T = 0, gpu0 = 0; int gpu0N = 0; double z0[kGpuZoneN] = {};
    Meas ref; bool refDone = false, refOk = false;   // (08/11 h) referencia medida justo antes de cada grupo
    std::vector<MenuTestResult> results;
    std::vector<std::string> views;   // fotos de las vistas de depuracion
    std::string status;
    int tested = 0, changed = 0, skipped = 0, off = 0;
    float meterSaved = 0; bool meterHad = false;
    std::wstring dir; size_t nGroups = 0;
};
static MenuTestState s_t;
static ID3D11Texture2D* s_stage = nullptr;
static UINT s_stW = 0, s_stH = 0; static DXGI_FORMAT s_stF = DXGI_FORMAT_UNKNOWN;

bool MenuTestActive() { return s_t.active; }
int  MenuTestCount(int what) { return what == 0 ? s_t.tested : what == 1 ? s_t.changed : s_t.skipped; }
float MenuTestNoise() { return s_t.noise; }
const std::string& MenuTestStatus() { return s_t.status; }
float MenuTestProgress() { return s_t.steps.empty() ? 0.f : (float)s_t.si / (float)s_t.steps.size(); }
const std::wstring& MenuTestDir() { return s_t.dir; }
size_t MenuTestResults() { return s_t.results.size(); }
bool MenuTestResult_(size_t i, std::string& group, std::string& section, float& diff, int& state, float& cpuMs, float& gpuMs) {
    if (i >= s_t.results.size()) return false;
    const auto& r = s_t.results[i];
    group = r.group; section = r.section; diff = r.diff; state = r.state; cpuMs = r.cpuSave; gpuMs = r.gpuSave;
    return true;
}

static bool Skippable(const MenuGroup& g) {
    return g.section.rfind("Diagn", 0) == 0 || g.section.rfind("Ropa", 0) == 0 || g.section.rfind("C\xe1mara", 0) == 0 || g.section.rfind("Camara", 0) == 0
        || g.section.rfind("Ajustes", 0) == 0 || g.section.rfind("Debug", 0) == 0;
}
static std::wstring Slug(const std::string& t, size_t max = 28) {
    std::wstring o; for (unsigned char ch : t) { if (o.size() >= max) break; if (isalnum(ch)) o += (wchar_t)tolower(ch); else if (!o.empty() && o.back() != L'_') o += L'_'; }
    while (!o.empty() && o.back() == L'_') o.pop_back(); return o.empty() ? L"x" : o;
}
static std::string W2A(const std::wstring& w) { std::string o; for (wchar_t c : w) o += (c < 128 ? (char)c : '_'); return o; }

void MenuTestStart() {
    s_t = MenuTestState();
    SYSTEMTIME st; GetLocalTime(&st); wchar_t n[32];
    swprintf(n, 32, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::wstring root = g_cfg.baseDir + L"diagnostico\\"; CreateDirectoryW(root.c_str(), nullptr);
    s_t.dir = root + n + L"\\"; CreateDirectoryW(s_t.dir.c_str(), nullptr); CreateDirectoryW((s_t.dir + L"capturas").c_str(), nullptr);
    // pasos: base, grupos (primero los de interruptor prendido), base final, vistas de depuracion
    s_t.steps.push_back({ 0, -1, -1, 0, 0, "base", "" });
    for (int i = 0; i < (int)g_cfg.groups.size(); i++) {
        const MenuGroup& g = g_cfg.groups[i];
        if (Skippable(g)) { MenuTestResult r; r.group = g.title; r.section = g.section; r.state = 2; s_t.results.push_back(r); s_t.skipped++; continue; }
        if (g.tIdx >= 0 && g.tIdx < kParamCount && g_cfg.live[g.tIdx][g.tComp] < 0.5f) { MenuTestResult r; r.group = g.title; r.section = g.section; r.state = 3; s_t.results.push_back(r); s_t.off++; continue; }
        Step sp; sp.kind = 1; sp.gi = i; sp.label = g.title; sp.section = g.section; s_t.steps.push_back(sp);
    }
    s_t.steps.push_back({ 0, -1, -1, 0, 0, "base final", "" });
    for (int i = 0; i < DebugParamCount(); i++) { const DebugParam& d = DebugParamAt(i);
        for (int k = 0; k < d.nViews; k++) { Step sp; sp.kind = 2; sp.idx = d.idx; sp.comp = d.comp; sp.val = d.views[k]; sp.label = d.name; sp.section = d.section; s_t.steps.push_back(sp); } }
    s_t.nGroups = g_cfg.groups.size();
    s_t.meterSaved = g_cfg.live[375][0]; g_cfg.live[375][0] = 1.0f;   // el medidor de ms por efecto, solo mientras dura la prueba
    s_t.active = true; s_t.si = 0; s_t.phase = 0; s_t.wait = 20;
    s_t.status = "Probando... (no muevas la camara; Esc corta)";
    Log("prueba automatica: inicio (%d pasos, %d grupos, carpeta diagnostico\\%ls)", (int)s_t.steps.size(), (int)g_cfg.groups.size(), n);
}

static void Restore() { if (s_t.sIdx >= 0) { g_cfg.live[s_t.sIdx][s_t.sComp] = s_t.sSaved; s_t.sIdx = -1; } }
static void WriteReport(bool cancelled);
static void Finish(bool cancelled) {
    Restore();
    g_cfg.live[375][0] = s_t.meterSaved;
    s_t.active = false;
    WriteReport(cancelled);
}
void MenuTestStop() { if (s_t.active) Finish(true); }

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
        g_cfg.live[g.tIdx][g.tComp] = 0.0f;   // solo se prueban los prendidos: se apagan
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
static void MeasBegin() { s_t.cur = Meas(); s_t.cpu0T = Now(); GpuMeterTotals(s_t.gpu0, s_t.gpu0N, s_t.z0); }
static void MeasEnd(int frames) {
    double g = 0, z[kGpuZoneN] = {}; int gn = 0; GpuMeterTotals(g, gn, z);
    s_t.cur.n = frames; s_t.cur.cpuMs = frames ? (Now() - s_t.cpu0T) * 1000.0 / frames : 0;
    s_t.cur.gn = gn - s_t.gpu0N; s_t.cur.gpuMs = s_t.cur.gn > 0 ? (g - s_t.gpu0) / s_t.cur.gn : 0;
    for (int k = 0; k < kGpuZoneN; k++) s_t.cur.zone[k] = frames ? (z[k] - s_t.z0[k]) / frames : 0;
}
static const int kSettle = 20, kMeasure = 30;   // (08/11 h) asentar 14 -> 20: recompilacion / recarga tras el cambio

void MenuTestPresent(IDXGISwapChain* sc, ID3D11Texture2D* frame) {
    if (!s_t.active) return;
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { MenuTestStop(); Log("prueba automatica: cortada con Esc"); return; }
    if (g_cfg.groups.size() != s_t.nGroups) { MenuTestStop(); s_t.status = "Prueba cortada: se recargo la configuracion"; return; }
    if (s_t.wait > 0) { s_t.wait--; return; }
    if (s_t.si >= s_t.steps.size()) { Finish(false); return; }
    Step& sp = s_t.steps[s_t.si];
    char b[200];
    switch (s_t.phase) {
    case 0:   // (08/11 h) referencia justo antes de cada grupo: el cuadro se va aliviando durante la prueba (la base del principio
              // daba +2 a +3 ms a TODOS los grupos del editor); el costo se mide contra lo que cuesta AHORA con el grupo prendido
        if (sp.kind == 1 && g_cfg.groups[sp.gi].tIdx >= 0 && !s_t.refDone) { MeasBegin(); s_t.refDone = true; s_t.phase = 5; s_t.wait = kMeasure; return; }
        s_t.refDone = false;
        if (sp.kind == 1) { if (!Apply(sp.gi)) { MenuTestResult r; r.group = sp.label; r.section = sp.section; r.state = 2; s_t.results.push_back(r); s_t.skipped++; s_t.si++; return; } }
        else if (sp.kind == 2) { s_t.sIdx = sp.idx; s_t.sComp = sp.comp; s_t.sSaved = g_cfg.live[sp.idx][sp.comp]; g_cfg.live[sp.idx][sp.comp] = sp.val; }
        snprintf(b, sizeof b, "Probando %d/%d: %s", (int)s_t.si + 1, (int)s_t.steps.size(), sp.label.c_str()); s_t.status = b;
        s_t.phase = 1; s_t.wait = sp.kind == 2 ? 10 : kSettle; return;
    case 1:   // medir (solo base y grupos con interruptor)
        if (sp.kind == 0 || (sp.kind == 1 && g_cfg.groups[sp.gi].tIdx >= 0)) { MeasBegin(); s_t.phase = 2; s_t.wait = kMeasure; return; }
        s_t.cur = Meas(); s_t.phase = 3; return;
    case 2: MeasEnd(kMeasure); s_t.phase = 3; return;
    case 3: {  // capturar
        std::vector<float> cap; Capture(sc, cap);
        wchar_t nm[96];
        if (sp.kind == 2) swprintf(nm, 96, L"vista_%02d_%ls_%g.jpg", (int)s_t.si, Slug(sp.label, 20).c_str(), sp.val);
        else swprintf(nm, 96, L"%03d_%ls.jpg", (int)s_t.si, sp.kind == 0 ? (s_t.si == 0 ? L"base" : L"base_final") : Slug(sp.label).c_str());
        EnsureComInit(); if (frame) AsyncShot(frame, s_t.dir + L"capturas\\" + nm);
        if (sp.kind == 0) {
            if (s_t.si == 0) { s_t.base = cap; s_t.base0 = s_t.cur; }
            else { s_t.base2 = cap; s_t.noise = Diff(s_t.base, s_t.base2); }
        } else if (sp.kind == 1) {
            MenuTestResult r; r.group = sp.label; r.section = sp.section; r.shot = W2A(nm); r.diff = Diff(s_t.base, cap);
            const float thr = (std::max)(0.0025f, s_t.noise * 3.0f);
            r.state = r.diff > thr ? 1 : 0; if (r.state) s_t.changed++; s_t.tested++;
            if (s_t.cur.n) { const Meas& R = s_t.refOk ? s_t.ref : s_t.base0;   // (08/11 h) contra la referencia de este grupo
                r.cpuSave = (float)(R.cpuMs - s_t.cur.cpuMs); r.gpuSave = (float)(R.gpuMs - s_t.cur.gpuMs);
                for (int k = 0; k < kGpuZoneN; k++) r.zoneSave[k] = (float)(R.zone[k] - s_t.cur.zone[k]); }
            s_t.refOk = false;
            s_t.results.push_back(r);
        } else s_t.views.push_back(sp.label + " = " + std::to_string((int)sp.val) + " -> capturas\\" + W2A(nm));
        s_t.phase = 4; return; }
    case 4:
        Restore(); s_t.si++; s_t.phase = 0; s_t.wait = 6; return;
    case 5:   // fin de la referencia del grupo
        MeasEnd(kMeasure); s_t.ref = s_t.cur; s_t.refOk = true; s_t.phase = 0; return;
    }
}

// ---------------------------------------------------------------- informe
static std::string U8(const std::string& cp) {   // los textos del menu estan en cp1252
    int n = MultiByteToWideChar(1252, 0, cp.c_str(), (int)cp.size(), nullptr, 0); std::wstring w(n, L'\0'); if (n) MultiByteToWideChar(1252, 0, cp.c_str(), (int)cp.size(), &w[0], n);
    int m = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), n, nullptr, 0, nullptr, nullptr); std::string o(m, '\0'); if (m) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), n, &o[0], m, nullptr, nullptr); return o;
}
static std::string J(const std::string& s) { std::string o = "\""; for (unsigned char c : U8(s)) { if (c == '"' || c == '\\') { o += '\\'; o += (char)c; } else if (c < 0x20) o += ' '; else o += (char)c; } return o + "\""; }
static void WriteReport(bool cancelled) {
    if (s_t.dir.empty()) return;
    std::string sum, js; char b[600];
    const Meas& B = s_t.base0;
    snprintf(b, sizeof b, "PRUEBA AUTOMATICA%s - %d pasos de %d\r\nbase: %.2f ms por cuadro de CPU (%.0f cuadros/s), %.2f ms de GPU (%d cuadros medidos)%s\r\nruido de la escena: %.4f (si es > 0.01 la camara se movio y las diferencias de imagen no sirven)\r\n",
        cancelled ? " (CORTADA)" : "", (int)s_t.si, (int)s_t.steps.size(), B.cpuMs, B.cpuMs > 0 ? 1000.0 / B.cpuMs : 0.0, B.gpuMs, B.gn,
        B.gn ? "" : " (sin ms de GPU: [PES21FX] medidor = 1 en el ini para tenerlos)", s_t.noise);
    sum += b;
    sum += "efectos en la base:"; for (int k = 0; k < kGpuZoneN; k++) if (B.zone[k] > 0.005) { snprintf(b, sizeof b, " %s %.2f ms;", kZoneNm[k], B.zone[k]); sum += b; } sum += "\r\n";
    snprintf(b, sizeof b, "pasto: %d parcelas, %d emisiones%s%s\r\n\r\n", g_grassDbg.patches, g_grassDbg.emits, g_grassDbg.why ? ", motivo: " : "", g_grassDbg.why ? g_grassDbg.why : ""); sum += b;
    // los mas caros primero
    std::vector<const MenuTestResult*> v; for (auto& r : s_t.results) if (r.state <= 1) v.push_back(&r);
    std::sort(v.begin(), v.end(), [](const MenuTestResult* a, const MenuTestResult* c) { return std::max(a->gpuSave, a->cpuSave) > std::max(c->gpuSave, c->cpuSave); });
    sum += "GRUPOS PROBADOS (apagado un momento; costo = lo que se gana al apagarlo, en ms por cuadro)\r\n";
    for (auto* r : v) {
        snprintf(b, sizeof b, "%-9s %-26.26s %-44.44s CPU %+6.2f  GPU %+6.2f  imagen %.4f", r->state ? "CAMBIA" : "SIN EFECTO", r->section.c_str(), r->group.c_str(), r->cpuSave, r->gpuSave, r->diff);
        sum += b; for (int k = 0; k < kGpuZoneN; k++) if (fabsf(r->zoneSave[k]) > 0.05f) { snprintf(b, sizeof b, "  [%s %+.2f]", kZoneNm[k], r->zoneSave[k]); sum += b; }
        sum += "  " + r->shot + "\r\n";
    }
    sum += "\r\nAPAGADOS (no se miden): "; { int n = 0; for (auto& r : s_t.results) if (r.state == 3) { sum += (n++ ? ", " : "") + r.group; } } sum += "\r\n";
    sum += "NO PROBADOS (ropa, camara, diagnostico, ajustes): "; { int n = 0; for (auto& r : s_t.results) if (r.state == 2) { sum += (n++ ? ", " : "") + r.group; } } sum += "\r\n\r\nVISTAS DE DEPURACION\r\n";
    for (auto& x : s_t.views) sum += x + "\r\n";
    std::string diag = DiagReportText(); sum += "\r\nREVISION\r\n" + diag + "\r\n" + GpuMeterSummary() + "\r\n" + LightMeterSummary() + "\r\n";
    { std::lock_guard<std::mutex> lk(g_errMutex); for (auto& e : g_compileErrors) sum += "error de shader: " + e + "\r\n"; }
    // json
    js = "{\n";
    snprintf(b, sizeof b, "  \"cortada\": %s, \"pasos\": %d, \"pasos_total\": %d, \"ruido\": %.5f,\n  \"base\": {\"cpu_ms\": %.3f, \"gpu_ms\": %.3f, \"gpu_cuadros\": %d, \"zonas\": {", cancelled ? "true" : "false", (int)s_t.si, (int)s_t.steps.size(), s_t.noise, B.cpuMs, B.gpuMs, B.gn);
    js += b; for (int k = 0; k < kGpuZoneN; k++) { snprintf(b, sizeof b, "%s\"%s\": %.3f", k ? ", " : "", kZoneNm[k], B.zone[k]); js += b; } js += "}},\n";
    snprintf(b, sizeof b, "  \"pasto\": {\"parcelas\": %d, \"emisiones\": %d, \"motivo\": %s},\n  \"shaders\": {\"activos\": %d, \"fallidos\": %d},\n  \"grupos\": [\n",
        g_grassDbg.patches, g_grassDbg.emits, g_grassDbg.why ? J(g_grassDbg.why).c_str() : "null", g_replacedCount.load(), g_failedCount.load());
    js += b;
    static const char* const kSt[4] = { "sin_efecto", "cambia", "no_probado", "apagado" };
    for (size_t i = 0; i < s_t.results.size(); i++) { const auto& r = s_t.results[i];
        snprintf(b, sizeof b, "    {\"seccion\": %s, \"grupo\": %s, \"estado\": \"%s\", \"imagen\": %.5f, \"cpu_ms\": %.3f, \"gpu_ms\": %.3f, \"captura\": %s, \"zonas\": {",
            J(r.section).c_str(), J(r.group).c_str(), kSt[std::clamp(r.state, 0, 3)], r.diff, r.cpuSave, r.gpuSave, J(r.shot).c_str());
        js += b; bool first = true; for (int k = 0; k < kGpuZoneN; k++) if (fabsf(r.zoneSave[k]) > 0.01f) { snprintf(b, sizeof b, "%s\"%s\": %.3f", first ? "" : ", ", kZoneNm[k], r.zoneSave[k]); js += b; first = false; }
        js += i + 1 < s_t.results.size() ? "}},\n" : "}}\n"; }
    js += "  ],\n  \"vistas\": [\n";
    for (size_t i = 0; i < s_t.views.size(); i++) js += "    " + J(s_t.views[i]) + (i + 1 < s_t.views.size() ? ",\n" : "\n");
    js += "  ],\n  \"revision\": " + J(diag) + "\n}\n";
    if (FILE* f = _wfopen((s_t.dir + L"resumen.txt").c_str(), L"wb")) { fwrite(sum.data(), 1, sum.size(), f); fclose(f); }
    if (FILE* f = _wfopen((s_t.dir + L"informe.json").c_str(), L"wb")) { fwrite(js.data(), 1, js.size(), f); fclose(f); }
    CopyFileW((g_cfg.baseDir + L"PES21FX.ini").c_str(), (s_t.dir + L"PES21FX.ini").c_str(), FALSE);
    snprintf(b, sizeof b, "%s: %d grupos probados, %d cambian la imagen, %d sin efecto, %d apagados, %d no probados, %d vistas. Informe en PES21FX\\diagnostico\\%s",
        cancelled ? "Cortada" : "Listo", s_t.tested, s_t.changed, s_t.tested - s_t.changed, s_t.off, s_t.skipped, (int)s_t.views.size(), W2A(s_t.dir.substr(g_cfg.baseDir.size() + 12)).c_str());
    s_t.status = b;
    Log("prueba automatica: %s", b);
}

} // namespace fx
