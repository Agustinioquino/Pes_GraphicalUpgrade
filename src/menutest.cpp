// PES21 FX - prueba automatica del menu (v2, 08/11). Agustin: "que le de a un boton y pruebe opciones y los devuelva para que
// vos lo lea". Se corre desde la pestana Diagnostico con la camara quieta (repeticion en pausa o el editor):
//   1) base: dos capturas (ruido de la escena), ms de CPU y de GPU por cuadro y por efecto (medidor), una foto jpg
//   2) por cada grupo PRENDIDO: se apaga su interruptor, se esperan unos cuadros (TAA), se mide lo mismo y se fotografia;
//      la diferencia de ms con la base = lo que cuesta ese efecto; la diferencia de imagen = si hace algo en esta escena.
//      Los grupos sin interruptor cambian su primer deslizador (solo imagen). Ropa, camara y diagnostico no se tocan.
//   3) cada vista de depuracion (cascadas, sombra sola, rebote, piel, pasto...) se prende un momento y se fotografia
//   4) se vuelve todo a como estaba y se escribe PES21FX\diagnostico\<fecha>\ resumen.txt + informe.json + capturas/
// No se guarda nada en el ini (SaveConfigValues se niega mientras corre). Esc o "Cancelar" la cortan (el informe parcial queda).
// (13/11 v3, Agustin: "mejorar la prueba automatica de diagnostico para que te de mas valores de rendimiento en un archivo de texto",
// "valores de cpu, gpu, memoria, ram, almacenamiento, lag", "dos pruebas una de cerca y otra en la camara lejana"): ademas escribe
// rendimiento.txt con el equipo, la escena elegida en el menu, la base de 8 s con todo lo de perfrec.cpp (FPS, tirones, reparto del cuadro
// entre el juego / el mod / el driver / la espera a la placa, CPU y hilos, placa en %, VRAM, RAM, disco), el perfil de la placa por shader
// (40, con las listas diferidas), lo que ahorra cada nivel del pasto del mod (cerca / medio / lejos / hojas 3D), cada grupo con su reparto
// y, si se pide, el juego entero sin el mod ("Ver original", al final y con el informe ya escrito). Una linea por medicion en
// PES21FX\diagnostico\rendimiento_todas.csv para comparar las dos escenas.
#include "core.h"
#include <d3d11_1.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdarg>

namespace fx {

void GpuMeterTotals(double& frameMs, int& frames, double* zoneMs);   // gpumeter.cpp
static const char* const kZoneNm[kGpuZoneN] = { "SSSS piel", "GTAO", "SSGI", "medidor de luz", "ropa contra el cuerpo", "desgaste de la cancha",
    "pasto", "caras", "caras HD", "aro del Fisico", "copia del cuadro", "menu Insert", "vidrio de menus" };

struct Meas { double cpuMs = 0, gpuMs = 0; double zone[kGpuZoneN] = {}; int n = 0, gn = 0; PerfRun ps; };
struct MenuTestResult { std::string group, section, shot; float diff = 0; int state = 0; float cpuSave = 0, gpuSave = 0; float zoneSave[kGpuZoneN] = {}; int kind = 1; PerfRun on, off; bool perf = false; };   // 0 sin cambio, 1 cambia, 2 no probado, 3 apagado (no se mide)
struct Step { int kind = 0; int gi = -1; int idx = -1, comp = 0; float val = 0; std::string label, section; };   // kind 0 base, 1 grupo, 2 vista, 3 perfil de la placa, 4 nivel del pasto (val = bits), 5 sin el mod
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
    int scene = 0; bool withBypass = false, bypassSaved = false, bypassOn = false; double measEndT = 0; PerfWin pw; int profRuns0 = 0, profWait = 0; double profT0 = 0;
    std::string profText; PerfRun baseFinal; char date[32] = {};
};
static MenuTestState s_t;
static ID3D11Texture2D* s_stage = nullptr;
static UINT s_stW = 0, s_stH = 0; static DXGI_FORMAT s_stF = DXGI_FORMAT_UNKNOWN;

// (11/11, hilo reinicio de placa) "buscar el cuelgue": la placa se reinicia (TDR 0x887A0020) al sacar "Ver original" dentro del
// partido. Arranca con todos los interruptores prendidos APAGADOS y el mod prendido (solo lo que no tiene interruptor), y vuelve a
// prender los grupos de a uno, acumulando, con kHangWait segundos entre cada uno. Cada paso queda en PES21FX.log y en
// PES21FX\diagnostico\cuelgue_<fecha>.txt (con fflush) ANTES de aplicarlo: si la placa se reinicia, el ultimo "prendo" antes de
// "PLACA DE VIDEO REINICIADA" es el culpable. No guarda nada en el ini; al terminar (o con Esc / Cancelar) deja todo como estaba.
struct HangStep { int idx = -1, comp = 0; float val = 0, orig = 0; std::string label, section; };   // val = lo que se prende, orig = lo que vuelve al final
struct HangState {
    bool active = false;
    std::vector<HangStep> steps; size_t si = 0;
    double next = 0; bool bypassSaved = false; size_t nGroups = 0;
    FILE* f = nullptr;
};
static HangState s_h;
static const double kHangWait = 4.0, kHangFirst = 8.0;   // segundos por paso (el TDR del driver salta a los ~2 s de una pasada colgada)
static void HangLine(const char* fmt, ...) {
    char b[700]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    Log("buscar cuelgue: %s", b);
    if (s_h.f) { SYSTEMTIME st; GetLocalTime(&st); fprintf(s_h.f, "[%02d:%02d:%02d.%03d] %s\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, b); fflush(s_h.f); }
}
static bool HangSkip(const MenuGroup& g) {   // solo diagnostico, camara y ajustes quedan como estan (la ropa SI se prueba: es su simulacion en la placa)
    return g.section.rfind("Diagn", 0) == 0 || g.section.rfind("C\xe1mara", 0) == 0 || g.section.rfind("Camara", 0) == 0
        || g.section.rfind("Ajustes", 0) == 0 || g.section.rfind("Debug", 0) == 0;
}
static void HangFinish(const char* why) {
    if (!s_h.active) return;
    for (const HangStep& h : s_h.steps) g_cfg.live[h.idx][h.comp] = h.orig;   // todo como estaba (los que faltaban prender tambien)
    g_cfg.bypass = s_h.bypassSaved;
    HangLine("%s (%d de %d grupos prendidos); todo vuelve a como estaba, no se guardo nada", why, (int)s_h.si, (int)s_h.steps.size());
    if (s_h.f) { fclose(s_h.f); s_h.f = nullptr; }
    s_h.active = false;
}
void MenuHangStart() {
    if (s_t.active || s_h.active) return;
    s_h = HangState();
    SYSTEMTIME st; GetLocalTime(&st); wchar_t n[64];
    swprintf(n, 64, L"cuelgue_%04d%02d%02d_%02d%02d%02d.txt", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::wstring root = g_cfg.baseDir + L"diagnostico\\"; CreateDirectoryW(root.c_str(), nullptr);
    s_h.f = _wfopen((root + n).c_str(), L"wb");
    int noToggle = 0, off = 0;
    for (int i = 0; i < (int)g_cfg.groups.size(); i++) {
        const MenuGroup& g = g_cfg.groups[i];
        if (HangSkip(g)) continue;
        if (g.tIdx < 0 || g.tIdx >= kParamCount) { noToggle++; continue; }
        if (g_cfg.live[g.tIdx][g.tComp] < 0.5f) { off++; continue; }
        HangStep h; h.idx = g.tIdx; h.comp = g.tComp; h.val = h.orig = g_cfg.live[g.tIdx][g.tComp]; h.label = g.title; h.section = g.section;
        s_h.steps.push_back(h);
    }
    s_h.nGroups = g_cfg.groups.size(); s_h.bypassSaved = g_cfg.bypass;
    HangLine("inicio: %d grupos prendidos a probar, %d ya apagados (no se tocan), %d sin interruptor (quedan prendidos desde el paso 0); %.0f s por paso",
             (int)s_h.steps.size(), off, noToggle, kHangWait);
    for (const HangStep& h : s_h.steps) g_cfg.live[h.idx][h.comp] = 0.0f;
    HangLine("paso 0: todos esos grupos APAGADOS y \"Ver original\" sacado (mod prendido solo con lo que no tiene interruptor)");
    g_cfg.bypass = false;
    s_h.active = true; s_h.si = 0; s_h.next = Now() + kHangFirst;
    s_t.status = "Buscando el cuelgue: paso 0 (todo apagado, mod prendido). No toques nada; Esc corta.";
}
static void HangPresent() {
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { HangFinish("cortada con Esc"); s_t.status = "Busqueda del cuelgue cortada (Esc). Todo quedo como estaba."; return; }
    if (g_cfg.groups.size() != s_h.nGroups) { HangFinish("cortada: se recargo la configuracion"); s_t.status = "Busqueda cortada: se recargo la configuracion"; return; }
    if (Now() < s_h.next) return;
    if (s_h.si >= s_h.steps.size()) {
        HangFinish("TERMINO SIN CUELGUE: con todos los grupos prendidos de nuevo la placa siguio andando");
        s_t.status = "Listo: no hubo cuelgue con todo prendido de a uno. Registro en PES21FX\\diagnostico\\cuelgue_<fecha>.txt";
        return;
    }
    const HangStep& h = s_h.steps[s_h.si];
    HangLine("paso %d/%d: prendo \"%s\" (%s, %c%d = %g)", (int)s_h.si + 1, (int)s_h.steps.size(), h.label.c_str(), h.section.c_str(), "xyzw"[h.comp], h.idx, h.val);
    g_cfg.live[h.idx][h.comp] = h.val;
    char b[300]; snprintf(b, sizeof b, "Buscando el cuelgue: %d/%d prendi %s. No toques nada; Esc corta.", (int)s_h.si + 1, (int)s_h.steps.size(), h.label.c_str());
    s_t.status = b;
    s_h.si++; s_h.next = Now() + kHangWait;
}
// (11/11) "buscar dentro del grupo": la busqueda general dio "Cejas con hebras" (x74). Con el resto del mod como esta (prendido),
// apaga las opciones de ese grupo (selectores y deslizadores que pueden ir a 0: hebras v3, pasada propia, sombra entre hebras, barba,
// pestanas, borde mixto...), prende el interruptor del grupo y despues devuelve cada opcion de a una, con el mismo registro.
void MenuHangStartGroup(int tIdx, int tComp) {
    if (s_t.active || s_h.active) return;
    int gi = -1; for (int i = 0; i < (int)g_cfg.groups.size(); i++) if (g_cfg.groups[i].tIdx == tIdx && g_cfg.groups[i].tComp == tComp) { gi = i; break; }
    if (gi < 0) { s_t.status = "No encontre el grupo a probar"; return; }
    const MenuGroup& g = g_cfg.groups[gi];
    s_h = HangState();
    SYSTEMTIME st; GetLocalTime(&st); wchar_t n[80];
    swprintf(n, 80, L"cuelgue_grupo_%04d%02d%02d_%02d%02d%02d.txt", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::wstring root = g_cfg.baseDir + L"diagnostico\\"; CreateDirectoryW(root.c_str(), nullptr);
    s_h.f = _wfopen((root + n).c_str(), L"wb");
    HangStep tg; tg.idx = tIdx; tg.comp = tComp; tg.orig = g_cfg.live[tIdx][tComp]; tg.val = 1.0f; tg.label = g.title + " (solo el interruptor, opciones en 0)"; tg.section = g.section;
    std::vector<HangStep> subs;
    for (const MenuSlider& sl : g.sliders) {
        if (!sl.ext.empty() || sl.idx < 0 || sl.idx >= kParamCount || (sl.idx == tIdx && sl.comp == tComp)) continue;
        const float v = g_cfg.live[sl.idx][sl.comp];
        if (fabsf(v) < 1e-6f) continue;                                   // ya en 0: no se toca
        const bool canZero = !sl.optVals.empty() ? std::find(sl.optVals.begin(), sl.optVals.end(), 0.0f) != sl.optVals.end() : (!sl.options.empty() || sl.lo <= 0.0f);
        if (!canZero) continue;
        HangStep h; h.idx = sl.idx; h.comp = sl.comp; h.val = h.orig = v; h.label = sl.label; h.section = g.title; subs.push_back(h);
    }
    s_h.steps.push_back(tg); for (auto& h : subs) s_h.steps.push_back(h);
    s_h.nGroups = g_cfg.groups.size(); s_h.bypassSaved = g_cfg.bypass;
    HangLine("inicio (dentro del grupo \"%s\"): el resto del mod queda como esta; %d opciones a probar; %.0f s por paso", g.title.c_str(), (int)subs.size(), kHangWait);
    g_cfg.live[tIdx][tComp] = 0.0f;
    for (auto& h : subs) { g_cfg.live[h.idx][h.comp] = 0.0f; HangLine("  en 0: \"%s\" (%c%d, era %g)", h.label.c_str(), "xyzw"[h.comp], h.idx, h.orig); }
    HangLine("paso 0: \"Ver original\" sacado, el grupo apagado y sus opciones en 0");
    g_cfg.bypass = false;
    s_h.active = true; s_h.si = 0; s_h.next = Now() + kHangFirst;
    s_t.status = "Buscando dentro del grupo: paso 0. No toques nada; Esc corta.";
}
bool MenuHangActive() { return s_h.active; }

bool MenuTestActive() { return s_t.active || s_h.active; }
int  MenuTestCount(int what) { return what == 0 ? s_t.tested : what == 1 ? s_t.changed : s_t.skipped; }
float MenuTestNoise() { return s_t.noise; }
const std::string& MenuTestStatus() { return s_t.status; }
float MenuTestProgress() { if (s_h.active) return s_h.steps.empty() ? 0.f : (float)s_h.si / (float)s_h.steps.size(); return s_t.steps.empty() ? 0.f : (float)s_t.si / (float)s_t.steps.size(); }
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

void MenuTestStart(int scene, bool withBypass) {
    if (s_h.active) return;
    s_t = MenuTestState();
    s_t.scene = scene; s_t.withBypass = withBypass; s_t.bypassSaved = g_cfg.bypass;
    SYSTEMTIME st; GetLocalTime(&st); wchar_t n[32];
    swprintf(n, 32, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    snprintf(s_t.date, sizeof s_t.date, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::wstring root = g_cfg.baseDir + L"diagnostico\\"; CreateDirectoryW(root.c_str(), nullptr);
    s_t.dir = root + n + L"\\"; CreateDirectoryW(s_t.dir.c_str(), nullptr); CreateDirectoryW((s_t.dir + L"capturas").c_str(), nullptr);
    // pasos: base, grupos (primero los de interruptor prendido), base final, vistas de depuracion
    s_t.steps.push_back({ 0, -1, -1, 0, 0, "base", "" });
    s_t.steps.push_back({ 3, -1, -1, 0, 0, "perfil de la placa por shader", "" });   // (13/11 v3)
    { static const char* const kLv[4] = { "Pasto del mod: sin el nivel 0 (cerca)", "Pasto del mod: sin el nivel 1 (medio)", "Pasto del mod: sin el nivel 2 (lejos)", "Pasto del mod: sin las hojas 3D" };
      for (int k = 0; k < 4; k++) { Step sp; sp.kind = 4; sp.val = (float)(1 << k); sp.label = kLv[k]; sp.section = "Pasto (niveles)"; s_t.steps.push_back(sp); } }
    for (int i = 0; i < (int)g_cfg.groups.size(); i++) {
        const MenuGroup& g = g_cfg.groups[i];
        if (Skippable(g)) { MenuTestResult r; r.group = g.title; r.section = g.section; r.state = 2; s_t.results.push_back(r); s_t.skipped++; continue; }
        if (g.tIdx >= 0 && g.tIdx < kParamCount && g_cfg.live[g.tIdx][g.tComp] < 0.5f) { MenuTestResult r; r.group = g.title; r.section = g.section; r.state = 3; s_t.results.push_back(r); s_t.off++; continue; }
        Step sp; sp.kind = 1; sp.gi = i; sp.label = g.title; sp.section = g.section; s_t.steps.push_back(sp);
    }
    s_t.steps.push_back({ 0, -1, -1, 0, 0, "base final", "" });
    if (withBypass) { Step sp; sp.kind = 5; sp.label = "TODO EL MOD APAGADO (Ver original)"; sp.section = "Juego sin el mod"; s_t.steps.push_back(sp); }
    for (int i = 0; i < DebugParamCount(); i++) { const DebugParam& d = DebugParamAt(i);
        for (int k = 0; k < d.nViews; k++) { Step sp; sp.kind = 2; sp.idx = d.idx; sp.comp = d.comp; sp.val = d.views[k]; sp.label = d.name; sp.section = d.section; s_t.steps.push_back(sp); } }
    s_t.nGroups = g_cfg.groups.size();
    s_t.meterSaved = g_cfg.live[375][0]; g_cfg.live[375][0] = 1.0f;   // el medidor de ms por efecto, solo mientras dura la prueba
    PerfUse(true); GpuShaderProfHold(true);   // (13/11 v3) mediciones del cuadro y del equipo; el perfil por shader solo cuando lo pide la prueba
    g_grassLevelOff.store(0);
    s_t.active = true; s_t.si = 0; s_t.phase = 0; s_t.wait = 20;
    s_t.status = "Probando... (no muevas la camara; Esc corta)";
    Log("prueba automatica: inicio (%d pasos, %d grupos, carpeta diagnostico\\%ls)", (int)s_t.steps.size(), (int)g_cfg.groups.size(), n);
}

static void Restore() { if (s_t.sIdx >= 0) { g_cfg.live[s_t.sIdx][s_t.sComp] = s_t.sSaved; s_t.sIdx = -1; }
    g_grassLevelOff.store(0); if (s_t.bypassOn) { g_cfg.bypass = s_t.bypassSaved; s_t.bypassOn = false; } }
static void WriteReport(bool cancelled, bool partial = false);
static void Finish(bool cancelled) {
    Restore();
    g_cfg.live[375][0] = s_t.meterSaved;
    s_t.active = false;
    PerfUse(false); GpuShaderProfHold(false);
    WriteReport(cancelled);
}
void MenuTestStop() { if (s_h.active) { HangFinish("cortada con Cancelar"); s_t.status = "Busqueda del cuelgue cortada. Todo quedo como estaba."; return; } if (s_t.active) Finish(true); }

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
static void MeasBegin() { s_t.cur = Meas(); s_t.cpu0T = Now(); GpuMeterTotals(s_t.gpu0, s_t.gpu0N, s_t.z0); s_t.pw = PerfBegin(); }
static void MeasEnd(int frames) {
    PerfEnd(s_t.pw); s_t.cur = Meas(); s_t.cur.ps = PerfCompute(s_t.pw); if (s_t.cur.ps.frames > 0) frames = s_t.cur.ps.frames;   // (13/11 v3) la base dura 8 s
    double g = 0, z[kGpuZoneN] = {}; int gn = 0; GpuMeterTotals(g, gn, z);
    s_t.cur.n = frames; s_t.cur.cpuMs = frames ? (Now() - s_t.cpu0T) * 1000.0 / frames : 0;
    s_t.cur.gn = gn - s_t.gpu0N; s_t.cur.gpuMs = s_t.cur.gn > 0 ? (g - s_t.gpu0) / s_t.cur.gn : 0;
    for (int k = 0; k < kGpuZoneN; k++) s_t.cur.zone[k] = frames ? (z[k] - s_t.z0[k]) / frames : 0;
}
static bool Measured(const Step& sp) { return sp.kind == 4 || sp.kind == 5 || (sp.kind == 1 && g_cfg.groups[sp.gi].tIdx >= 0); }
static const int kSettle = 20, kMeasure = 30;   // (08/11 h) asentar 14 -> 20: recompilacion / recarga tras el cambio

void MenuTestPresent(IDXGISwapChain* sc, ID3D11Texture2D* frame) {
    if (s_h.active) { HangPresent(); return; }
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
        if (Measured(sp) && !s_t.refDone) { MeasBegin(); s_t.refDone = true; s_t.phase = 5; s_t.wait = kMeasure; return; }
        s_t.refDone = false;
        if (sp.kind == 1) { if (!Apply(sp.gi)) { MenuTestResult r; r.group = sp.label; r.section = sp.section; r.state = 2; s_t.results.push_back(r); s_t.skipped++; s_t.si++; return; } }
        else if (sp.kind == 2) { s_t.sIdx = sp.idx; s_t.sComp = sp.comp; s_t.sSaved = g_cfg.live[sp.idx][sp.comp]; g_cfg.live[sp.idx][sp.comp] = sp.val; }
        else if (sp.kind == 3) {   // (13/11 v3) perfil de la placa por shader con todo prendido (120 cuadros + lectura)
            s_t.profRuns0 = GpuShaderProfRuns(); GpuShaderProfKick(); s_t.profT0 = Now();
            snprintf(b, sizeof b, "Probando %d/%d: %s (unos segundos)", (int)s_t.si + 1, (int)s_t.steps.size(), sp.label.c_str()); s_t.status = b;
            s_t.phase = 6; return; }
        else if (sp.kind == 4) g_grassLevelOff.store((int)sp.val);
        else if (sp.kind == 5) { WriteReport(false, true); s_t.bypassSaved = g_cfg.bypass; s_t.bypassOn = true; g_cfg.bypass = true; }   // el informe ya queda escrito por si la placa se reinicia
        snprintf(b, sizeof b, "Probando %d/%d: %s", (int)s_t.si + 1, (int)s_t.steps.size(), sp.label.c_str()); s_t.status = b;
        s_t.phase = 1; s_t.wait = sp.kind == 2 ? 10 : sp.kind == 5 ? 2 * kSettle : kSettle; return;
    case 1:   // medir (base, grupos con interruptor, niveles del pasto y sin el mod)
        if (sp.kind == 0 || Measured(sp)) { MeasBegin(); s_t.phase = 2; s_t.wait = kMeasure; s_t.measEndT = sp.kind == 0 ? Now() + 8.0 : 0.0; return; }
        s_t.cur = Meas(); s_t.phase = 3; return;
    case 2: if (s_t.measEndT > 0.0 && Now() < s_t.measEndT) return;
        MeasEnd(kMeasure); s_t.phase = 3; return;
    case 3: {  // capturar
        std::vector<float> cap; Capture(sc, cap);
        wchar_t nm[96];
        if (sp.kind == 2) swprintf(nm, 96, L"vista_%02d_%ls_%g.jpg", (int)s_t.si, Slug(sp.label, 20).c_str(), sp.val);
        else swprintf(nm, 96, L"%03d_%ls.jpg", (int)s_t.si, sp.kind == 0 ? (s_t.si == 0 ? L"base" : L"base_final") : Slug(sp.label).c_str());
        EnsureComInit(); if (frame) AsyncShot(frame, s_t.dir + L"capturas\\" + nm);
        if (sp.kind == 0) {
            if (s_t.si == 0) { s_t.base = cap; s_t.base0 = s_t.cur; }
            else { s_t.base2 = cap; s_t.noise = Diff(s_t.base, s_t.base2); s_t.baseFinal = s_t.cur.ps; }
        } else if (sp.kind == 1 || sp.kind == 4 || sp.kind == 5) {
            MenuTestResult r; r.group = sp.label; r.section = sp.section; r.shot = W2A(nm); r.diff = Diff(s_t.base, cap); r.kind = sp.kind;
            const float thr = (std::max)(0.0025f, s_t.noise * 3.0f);
            r.state = r.diff > thr ? 1 : 0; if (r.state) s_t.changed++; s_t.tested++;
            if (s_t.cur.n) { const Meas& R = s_t.refOk ? s_t.ref : s_t.base0;   // (08/11 h) contra la referencia de este grupo
                r.cpuSave = (float)(R.cpuMs - s_t.cur.cpuMs); r.gpuSave = (float)(R.gpuMs - s_t.cur.gpuMs);
                for (int k = 0; k < kGpuZoneN; k++) r.zoneSave[k] = (float)(R.zone[k] - s_t.cur.zone[k]);
                r.on = R.ps; r.off = s_t.cur.ps; r.perf = R.ps.frames > 0 && s_t.cur.ps.frames > 0; }
            s_t.refOk = false;
            s_t.results.push_back(r);
        } else s_t.views.push_back(sp.label + " = " + std::to_string((int)sp.val) + " -> capturas\\" + W2A(nm));
        s_t.phase = 4; return; }
    case 4:
        Restore(); s_t.si++; s_t.phase = 0; s_t.wait = sp.kind == 5 ? 2 * kSettle : 6; return;
    case 5:   // fin de la referencia del grupo
        MeasEnd(kMeasure); s_t.ref = s_t.cur; s_t.refOk = true; s_t.phase = 0; return;
    case 6:   // (13/11 v3) esperando el perfil por shader
        if (GpuShaderProfRuns() > s_t.profRuns0 || Now() - s_t.profT0 > 45.0) {
            s_t.profText = GpuShaderProfRuns() > s_t.profRuns0 ? GpuShaderProfReport() : std::string("(el perfil por shader no termino en 45 s: el medidor necesita [PES21FX] medidor = 1 en el ini)\r\n");
            s_t.si++; s_t.phase = 0; s_t.wait = 6; }
        return;
    }
}

// ---------------------------------------------------------------- informe
static std::string U8(const std::string& cp) {   // los textos del menu estan en cp1252
    int n = MultiByteToWideChar(1252, 0, cp.c_str(), (int)cp.size(), nullptr, 0); std::wstring w(n, L'\0'); if (n) MultiByteToWideChar(1252, 0, cp.c_str(), (int)cp.size(), &w[0], n);
    int m = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), n, nullptr, 0, nullptr, nullptr); std::string o(m, '\0'); if (m) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), n, &o[0], m, nullptr, nullptr); return o;
}
static std::string J(const std::string& s) { std::string o = "\""; for (unsigned char c : U8(s)) { if (c == '"' || c == '\\') { o += '\\'; o += (char)c; } else if (c < 0x20) o += ' '; else o += (char)c; } return o + "\""; }
// (13/11 v3) rendimiento.txt: todo lo de perfrec.cpp + perfil por shader + niveles del pasto + grupos con su reparto + sin el mod
static void WritePerf(bool cancelled, bool partial) {
    std::string o; char b[1200];
    const char* scene = PerfSceneName(s_t.scene);
    snprintf(b, sizeof b, "RENDIMIENTO - PRUEBA AUTOMATICA%s - %s - escena: %s\r\n", partial ? " (PARCIAL: falta lo que sigue a 'sin el mod')" : cancelled ? " (CORTADA)" : "", s_t.date, scene); o += b;
    o += PerfHardwareText();
    snprintf(b, sizeof b, "pasto del mod en la escena: %d parcelas, %d emisiones%s%s | %d shaders propios activos\r\n\r\n", g_grassDbg.patches, g_grassDbg.emits,
        g_grassDbg.why ? ", motivo: " : "", g_grassDbg.why ? g_grassDbg.why : "", g_replacedCount.load()); o += b;
    o += PerfStatsText(s_t.base0.ps, "BASE (todo como lo tenes, 8 s con la camara quieta)");
    snprintf(b, sizeof b, "cuadro en la placa (marcas de Present a Present): %.2f ms\r\n\r\n", s_t.base0.gpuMs); o += b;
    o += s_t.profText.empty() ? std::string("(sin perfil por shader)\r\n") : s_t.profText; o += "\r\n";
    auto row = [&](const MenuTestResult& r) {
        const PerfRun &A = r.on, &B = r.off;
        snprintf(b, sizeof b, "  %-46.46s ahorra %+6.2f ms del cuadro (%5.1f -> %5.1f FPS) | placa %+6.2f | espera a la placa %+6.2f | mod en dibujos %+5.2f | juego %+5.2f | driver %+5.2f | placa 3D %s | %s\r\n",
            r.group.c_str(), r.cpuSave, A.fps, B.fps, r.gpuSave, A.waitMs - B.waitMs, (A.modImmMs + A.modDefMs + A.hookImmMs + A.hookDefMs) - (B.modImmMs + B.modDefMs + B.hookImmMs + B.hookDefMs),
            A.gameRtMs - B.gameRtMs, (A.drvImmMs + A.drvDefMs) - (B.drvImmMs + B.drvDefMs),
            (A.gpu3d >= 0 && B.gpu3d >= 0) ? (std::to_string((int)lroundf(A.gpu3d)) + " -> " + std::to_string((int)lroundf(B.gpu3d)) + " %").c_str() : "sin dato",
            r.state ? "cambia la imagen" : "sin efecto en la imagen");
        o += b; };
    o += "PASTO DEL MOD POR NIVEL (apagado un momento; ahorra = lo que gana el cuadro sin ese nivel)\r\n";
    for (auto& r : s_t.results) if (r.kind == 4) row(r);
    std::vector<const MenuTestResult*> v; for (auto& r : s_t.results) if (r.kind == 1 && r.state <= 1 && r.perf) v.push_back(&r);
    std::sort(v.begin(), v.end(), [](const MenuTestResult* a, const MenuTestResult* c) { return a->cpuSave > c->cpuSave; });
    o += "\r\nGRUPOS DEL MENU (los prendidos con interruptor, del que mas ahorra al que menos; ahorra = cuadro con el grupo prendido menos apagado)\r\n";
    for (auto* r : v) row(*r);
    for (auto& r : s_t.results) if (r.kind == 5) { o += "\r\nJUEGO SIN EL MOD (Ver original, al final)\r\n"; row(r); o += PerfStatsText(r.off, "sin el mod"); }
    if (s_t.baseFinal.frames) { o += "\r\n"; o += PerfStatsText(s_t.baseFinal, "BASE FINAL (todo prendido de nuevo: si difiere mucho de la base, la escena cambio durante la prueba)"); }
    snprintf(b, sizeof b, "\r\nruido de la imagen entre base y base final: %.4f (> 0.01 = la camara se movio)\r\n", s_t.noise); o += b;
    o += "\r\nCOMO LEERLO: 'esperando en Present' alto = la placa no da abasto (bajar costo de placa sube FPS). 'juego' alto con la placa baja = el procesador\r\n"
         "(el hilo que dibuja del juego) es el limite y achicar el pasto o los efectos no sube los FPS. 'mod en dibujos' = lo que la DLL tarda en cada dibujo.\r\n";
    if (FILE* f = _wfopen((s_t.dir + L"rendimiento.txt").c_str(), L"wb")) { fwrite(o.data(), 1, o.size(), f); fclose(f); }
    if (partial) return;
    std::string rows = PerfCsvRow(s_t.date, scene, "base", s_t.base0.ps);
    for (auto& r : s_t.results) if ((r.kind == 4 || r.kind == 5) && r.perf) rows += PerfCsvRow(s_t.date, scene, r.group.c_str(), r.off);
    for (size_t i = 0; i < v.size() && i < 12; i++) rows += PerfCsvRow(s_t.date, scene, ("sin " + v[i]->group).c_str(), v[i]->off);
    PerfAppendAll(rows);
}
static void WriteReport(bool cancelled, bool partial) {
    if (s_t.dir.empty()) return;
    WritePerf(cancelled, partial);
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
