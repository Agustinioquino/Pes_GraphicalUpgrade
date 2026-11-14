// (14/11, Agustin, hilo 9vcq5h) REPETICION: barra propia para saltar con el mouse + desenfoque de movimiento corto en los cortes.
//
// Lo que se leyo del exe 1.07.02 (detalle y C++ en decompilado/hallazgos/20261114_repeticion_bufer.cpp.md):
//  * match::Replay (0x6c8 B, vtable 0x142bdfd10, hereda de match::PlaybackController) lleva en +0x50 la LINEA DE TIEMPO del visor:
//      +0x00 int estado (5 = reproduciendo), +0x14 float posicion, +0x18 paso del cuadro, +0x238 u16 cuadro actual,
//      +0x23a u16 largo del anillo, +0x23c u16 primer cuadro, +0x240 u16 ultimo cuadro  (= Replay +0x288 / +0x28a / +0x28c / +0x290)
//  * FUN_14217e800(linea, cuadro) = el SALTO del propio juego (lo usa para volver al principio): recorta al tramo [primero, ultimo],
//    escribe +0x238 = cuadro % largo y +0x14 = cuadro, y llama FUN_14217bf20(linea, 0) que con paso 0 solo pone +0x18 = 0. Hoja: no
//    carga nada ni pasa por ganchos de Sider.
//  * FUN_14217c0b0(linea, mando, ...) = control de la repeticion con el mando; la llama el visor en cada cuadro (gancho hk_rpl de
//    rastro.cpp, hilo del juego). Ahi se aplica el salto pedido con el mouse y se miran los cortes.
//  * Cuadros: unidad activa = [[0x143703558] + 0x6a8], cuadros en unidad+0x40, 0x1fb0 B cada uno. +0x00 float reloj del partido
//    (FUN_140a6c3b0: (int)reloj / 60 = segundos), +0x07 & 0xf parte (2/3 +45, 4/5 +90, 6/7 +105, FUN_1420c6760), +0x24c int16 x3
//    pelota en 1/256 m (0x7fff = cuadro vacio).
//  * CORTE = la pelota salta mas de 3 m entre dos cuadros seguidos (la celebracion no se graba: del gol se pasa al saque del medio).
//
// ini [PES21FX]: repeticion_barra = 1 (0 apagada), repeticion_corte = 1 (0 sin desenfoque), repeticion_corte_ms = 300 (100..1000)
#include "core.h"
#include <imgui.h>
#include <d3d11.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace fx {

namespace {
const uintptr_t kReplayVt = 0x142bdfd10ull;          // match::Replay::vftable (lea de FUN_1420bc0c0 + 0x1c)
const uintptr_t kReplayVf10 = 0x1420c16a0ull;        // su vf10 (estado de la barra): para comprobar la vtable
const uintptr_t kSeek = 0x14217e800ull;              // salto del juego
const unsigned char kSeekSig[10] = { 0x0f,0xb7,0x81,0x3c,0x02,0x00,0x00,0x4c,0x8b,0xc9 };
const uintptr_t kDmGlobal = 0x143703558ull;          // match::DataManagerReplay*
const size_t kFrame = 0x1fb0;
using SeekFn = void(*)(void*, unsigned short);

std::atomic<void*> s_line{nullptr};                  // linea de tiempo que el visor esta usando (hk_rpl)
std::atomic<unsigned long long> s_lineMs{0};
std::atomic<int> s_seekReq{-1};                      // cuadro pedido con el mouse (lo aplica el hilo del juego)
std::atomic<int> s_cutReq{0}; std::atomic<int> s_seekBlur{0};   // s_seekBlur: el salto es un clic (no el arrastre): lleva desenfoque                        // cortes vistos por el hilo del juego (Present los consume)
std::atomic<int> s_ok{-1};                           // -1 sin mirar, 0 firma distinta, 1 bien
// datos de la linea para el dibujo (los copia el hilo del juego en cada llamada)
std::atomic<int> s_cur{0}, s_first{0}, s_last{0}, s_n{1};
unsigned short s_prevCur = 0xffff; const void* s_prevLine = nullptr;

bool Readable(const void* p, size_t n) {
    __try { volatile unsigned char x = 0; for (size_t i = 0; i < n; i += 4096) x ^= ((const unsigned char*)p)[i]; x ^= ((const unsigned char*)p)[n - 1]; (void)x; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Check() {
    int v = s_ok.load(); if (v >= 0) return v == 1;
    bool ok = false;
    __try { ok = memcmp((const void*)kSeek, kSeekSig, sizeof kSeekSig) == 0 && ((const uintptr_t*)kReplayVt)[10] == kReplayVf10; }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    s_ok = ok ? 1 : 0;
    Log("repeticion: barra con mouse y cortes %s", ok ? "listos (salto 0x14217e800, match::Replay 0x142bdfd10)" : "APAGADOS (exe distinto: firma del salto o vtable)");
    return ok;
}
// linea -> objeto match::Replay (la linea esta en +0x50); solo vale el visor de repeticiones
bool IsReplayLine(const void* line) {
    __try { const uintptr_t vt = *(const uintptr_t*)((const char*)line - 0x50); return vt == kReplayVt; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
const unsigned char* Frames(int n) {
    __try {
        const uintptr_t dm = *(const uintptr_t*)kDmGlobal; if (!dm) return nullptr;
        const uintptr_t u = *(const uintptr_t*)(dm + 0x6a8); if (!u) return nullptr;
        const unsigned char* f = *(const unsigned char* const*)(u + 0x40);
        if (!f || !Readable(f, (size_t)n * kFrame)) return nullptr;
        return f;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
bool Ball(const unsigned char* f, int i, float p[3]) {
    const short* b = (const short*)(f + (size_t)i * kFrame + 0x24c);
    if (b[0] == 0x7fff) return false;
    p[0] = b[0] / 256.0f; p[1] = b[1] / 256.0f; p[2] = b[2] / 256.0f; return true;
}
int Wrap(int a, int n) { a %= n; return a < 0 ? a + n : a; }
} // namespace

bool RepeticionWantsHook() { return g_cfg.replayBar != 0 || g_cfg.replayCut != 0; }

// hilo del juego, desde hk_rpl (rastro.cpp) ANTES del control del visor
void RepeticionRplHook(void* line) {
    if (!line || !RepeticionWantsHook() || !Check() || !IsReplayLine(line)) return;
    __try {
        unsigned char* L = (unsigned char*)line;
        const int n = *(const unsigned short*)(L + 0x23a); if (n < 2) return;
        s_line = line; s_lineMs = GetTickCount64();
        const int req = s_seekReq.exchange(-1);
        if (req >= 0 && g_cfg.replayBar) {
            ((SeekFn)kSeek)(line, (unsigned short)Wrap(req, n));
            if (g_cfg.replayCut && s_seekBlur.exchange(0)) s_cutReq.fetch_add(1);
            s_prevLine = nullptr;                    // el salto propio no cuenta como corte de la jugada
        }
        const int cur = *(const unsigned short*)(L + 0x238);
        s_cur = cur; s_first = *(const unsigned short*)(L + 0x23c); s_last = *(const unsigned short*)(L + 0x240); s_n = n;
        if (g_cfg.replayCut && s_prevLine == line && s_prevCur != 0xffff && s_prevCur != cur) {
            const int fw = Wrap(cur - s_prevCur, n), bw = Wrap(s_prevCur - cur, n);
            const int d = std::min(fw, bw), dir = fw <= bw ? 1 : -1;
            if (d <= 6) if (const unsigned char* f = Frames(n)) {
                float a[3], b[3];
                for (int k = 0, i = s_prevCur; k < d; k++) {
                    const int j = Wrap(i + dir, n);
                    if (Ball(f, i, a) && Ball(f, j, b)) {
                        const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
                        if (dx * dx + dy * dy + dz * dz > 9.0f) { s_cutReq.fetch_add(1); break; }
                    }
                    i = j;
                }
            }
        }
        s_prevLine = line; s_prevCur = (unsigned short)cur;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

namespace {
// ---- desenfoque del corte (hilo de Present) ----
ID3D11Texture2D* s_tex[2] = {}; ID3D11ShaderResourceView* s_srv[2] = {}; int s_prevI = 0;   // s_prevI = ultimo cuadro, el otro = cuadro viejo
UINT s_tw = 0, s_th = 0; DXGI_FORMAT s_tf = DXGI_FORMAT_UNKNOWN;
double s_cutT = -1e9; int s_cutSeen = 0; bool s_haveOld = false, s_copyOk = false;
// ---- barra ----
double s_moveT = -1e9; POINT s_lastP{ -32000, -32000 }; bool s_lWas = false, s_drag = false;
float s_mx = -1, s_my = -1; bool s_front = false;

bool ReplayLive() { return GetTickCount64() - s_lineMs.load() < 400 && s_line.load() != nullptr; }
void FreeTex() { for (int i = 0; i < 2; i++) { if (s_srv[i]) s_srv[i]->Release(); if (s_tex[i]) s_tex[i]->Release(); s_srv[i] = nullptr; s_tex[i] = nullptr; } s_tw = s_th = 0; s_haveOld = false; }
bool EnsureTex(const D3D11_TEXTURE2D_DESC& bd) {
    if (s_tex[0] && s_tw == bd.Width && s_th == bd.Height && s_tf == bd.Format) return true;
    FreeTex();
    if (bd.SampleDesc.Count != 1) return false;
    D3D11_TEXTURE2D_DESC d = {}; d.Width = bd.Width; d.Height = bd.Height; d.MipLevels = 1; d.ArraySize = 1; d.Format = bd.Format;
    d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    for (int i = 0; i < 2; i++) {
        if (FAILED(g_dev.dev->CreateTexture2D(&d, nullptr, &s_tex[i])) || FAILED(g_dev.dev->CreateShaderResourceView(s_tex[i], nullptr, &s_srv[i]))) { FreeTex(); return false; }
    }
    s_tw = bd.Width; s_th = bd.Height; s_tf = bd.Format; return true;
}
float Ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
// reloj de un cuadro -> "MM:SS" como la barra del juego (FUN_1420c6760)
bool FrameClock(int i, char* out, size_t cap) {
    const int n = s_n.load(); const unsigned char* f = Frames(n); if (!f || i < 0 || i >= n) return false;
    __try {
        const unsigned char* fr = f + (size_t)i * kFrame;
        if (*(const short*)(fr + 0x24c) == 0x7fff) return false;
        const float t = *(const float*)fr; if (!(t >= 0.0f && t < 1.0e8f)) return false;
        const unsigned s = (unsigned)(long long)t / 60u; int m = (int)(s / 60u); const int sec = (int)(s % 60u);
        const int h = fr[7] & 0xf; m += (h == 2 || h == 3) ? 45 : (h == 4 || h == 5) ? 90 : (h == 6 || h == 7) ? 105 : 0;
        snprintf(out, cap, "%02d:%02d", m, sec); return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// visor de repeticiones abierto (pausa > repeticion): el estado de la barra [[0x143702208] + 0x388] con +7 != 0 y modo != 4
// (match2D::Screen::Replay vf19 0x1420f4580 esconde la pantalla en el otro caso). Las repeticiones automaticas no lo abren
bool ViewerOpen() {
    __try {
        const uintptr_t m = *(const uintptr_t*)0x143702208ull; if (!m) return false;
        const unsigned char* st = *(const unsigned char* const*)(m + 0x388); if (!st) return false;
        return st[7] != 0 && st[0] != 4;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
double s_openT = -1e9; bool s_openWas = false;
// (14/11 07:50, Agustin: "la barra tiene que verse normal, pero ... se amplia al abrir el modo repeticion") la barra arranca en el
// lugar de la del juego (medida en sus capturas a 1920 x 1080: x 860..1764, y 967..980) y, si hay bastante grabado, se agranda con
// una animacion por encima de los paneles (x 104..1816 = bordes de los paneles, y 836) sin salirse de la pantalla
struct BarGeo { float x0, x1, y, h, hit; };
BarGeo Geo(const ImVec2& ds) {
    const float sx = ds.x / 1920.0f, sy = ds.y / 1080.0f, k = std::max(1.0f, sy);
    const int n = std::max(s_n.load(), 1); const float lenS = (float)Wrap(s_last.load() - s_first.load(), n) / 54.0f;
    const float grow = std::clamp((lenS - 15.0f) / 105.0f, 0.0f, 1.0f);          // 15 s o menos: como la del juego; 2 min o mas: ancha
    const float t = (float)std::clamp((Now() - s_openT - 0.25) / 0.45, 0.0, 1.0); const float e = grow * t * t * (3.0f - 2.0f * t);
    BarGeo g;
    g.x0 = (860.0f + (104.0f - 860.0f) * e) * sx; g.x1 = (1764.0f + (1816.0f - 1764.0f) * e) * sx;
    const float h = (4.0f + 6.0f * e) * k; const float yc = (973.5f + (836.0f - 973.5f) * e) * sy;
    g.h = h; g.y = yc - h * 0.5f; g.hit = (14.0f + 10.0f * e) * k; return g;
}
} // namespace

// Present, antes del ImGui de la DLL: copia del cuadro (solo en la repeticion) y estado del mouse. true = hay algo que dibujar
bool RepeticionPresent(IDXGISwapChain* sc) {
    if (!RepeticionWantsHook() || s_ok.load() != 1) { s_drag = false; return false; }
    const bool live = ReplayLive();
    const double now = Now();
    // mouse (pixeles de la ventana)
    HWND h = OverlayHwnd(); POINT p{}; RECT rc{}; s_front = false;
    if (h && GetForegroundWindow() == h && GetCursorPos(&p) && ScreenToClient(h, &p) && GetClientRect(h, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
        s_front = true;
        if (p.x != s_lastP.x || p.y != s_lastP.y) { if (s_lastP.x != -32000 && live) s_moveT = now; s_lastP = p; }
        s_mx = (float)p.x / (float)(rc.right - rc.left); s_my = (float)p.y / (float)(rc.bottom - rc.top);   // 0..1
    }
    const bool lNow = s_front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (lNow && !s_lWas && live) s_moveT = now;
    // corte -> guardar el cuadro anterior como "viejo"
    const int cuts = s_cutReq.load();
    if (cuts != s_cutSeen) { s_cutSeen = cuts; if (s_copyOk && live) { s_prevI ^= 1; s_haveOld = true; s_cutT = now; } }
    const float dur = (float)std::clamp(g_cfg.replayCutMs, 100, 1000) / 1000.0f;
    const bool blur = g_cfg.replayCut && s_haveOld && now - s_cutT < dur;
    s_copyOk = false;
    if (live && g_cfg.replayCut && sc && g_dev.imm) {
        ID3D11Texture2D* bb = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) && bb) {
            D3D11_TEXTURE2D_DESC bd; bb->GetDesc(&bd);
            if (EnsureTex(bd)) { g_dev.imm->CopyResource(s_tex[s_prevI], bb); s_copyOk = true; }
            bb->Release();
        }
    } else if (!live && s_tex[0] && now - s_cutT > 5.0) FreeTex();
    if (!blur) s_haveOld = s_haveOld && now - s_cutT < dur;
    const bool open = live && ViewerOpen();
    if (open && !s_openWas) s_openT = now;
    s_openWas = open;
    const bool bar = open && g_cfg.replayBar && !g_menuOpen.load();
    if (!bar) s_drag = false;
    // clic / arrastre sobre la barra (en proporcion de la ventana; el dibujo usa las mismas)
    if (bar) {
        const ImVec2 ds(1920.0f, 1080.0f); const BarGeo g = Geo(ds);   // en proporcion: Geo escala con ds
        const float mx = s_mx * ds.x, my = s_my * ds.y;
        const bool over = mx >= g.x0 - 8.0f && mx <= g.x1 + 8.0f && fabsf(my - (g.y + g.h * 0.5f)) <= g.hit;
        if (lNow && !s_lWas && over) s_drag = true;
        if (!lNow) s_drag = false;
        if (s_drag) {
            const int n = s_n.load(), a = s_first.load(), len = Wrap(s_last.load() - a, std::max(n, 1));
            const float fr = std::clamp((mx - g.x0) / (g.x1 - g.x0), 0.0f, 1.0f);
            const int want = Wrap(a + (int)lroundf(fr * (float)len), std::max(n, 1));
            static int s_sent = -1; static double s_sentT = 0;
            if (want != s_sent || now - s_sentT > 0.25) {   // mientras se arrastra: un salto por cuadro del mouse
                if (s_sent < 0 || lNow != s_lWas) Log("repeticion: salto con el mouse a %.1f %% (cuadro %d de %d)", fr * 100.0f, want, len);
                if (lNow && !s_lWas) s_seekBlur = 1;
                s_seekReq = want; s_sent = want; s_sentT = now;
            }
            s_moveT = now;
        }
    }
    s_lWas = lNow;
    return bar || blur;
}

// dentro del cuadro de ImGui de la DLL (overlay.cpp)
void RepeticionDraw() {
    const double now = Now();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float dur = (float)std::clamp(g_cfg.replayCutMs, 100, 1000) / 1000.0f;
    // 1) desenfoque de movimiento del corte: el cuadro nuevo barrido + el viejo que se va barrido para el otro lado
    if (g_cfg.replayCut && s_haveOld && s_srv[0] && now - s_cutT < dur) {
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        const float t = (float)((now - s_cutT) / dur), e = Ease(t);
        const int N = 8;
        // nuevo: promedio de N copias corridas hacia la izquierda (llega desde la derecha), cada vez menos
        const float sNew = (1.0f - e) * 0.09f * ds.x;
        ImTextureID cur = (ImTextureID)(uintptr_t)s_srv[s_prevI], old = (ImTextureID)(uintptr_t)s_srv[s_prevI ^ 1];
        for (int i = 1; i < N; i++) {
            const float o = sNew * (float)i / (float)(N - 1);
            const int a = (int)(255.0f / (float)(i + 1) + 0.5f);
            bg->AddImage(cur, ImVec2(o, 0), ImVec2(ds.x + o, ds.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, a));
        }
        // viejo: se va hacia la izquierda, con peso W que cae a 0
        const float W = (1.0f - e) * 0.85f, sOld = (0.03f + e * 0.12f) * ds.x;
        if (W > 0.004f) for (int j = 0; j < N; j++) {
            const float aj = (W / N) / (1.0f - W + (float)(j + 1) * W / N);
            const float o = -sOld * (float)j / (float)(N - 1);
            bg->AddImage(old, ImVec2(o, 0), ImVec2(ds.x + o, ds.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(aj * 255.0f + 0.5f)));
        }
    }
    // 2) barra propia: casi de borde a borde, con el minuto donde apunta el mouse
    if (!g_cfg.replayBar || !ReplayLive() || g_menuOpen.load() || !s_openWas) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = std::max(1.0f, ds.y / 1080.0f);
    const BarGeo g = Geo(ds);
    const float a = std::clamp((float)(now - s_openT) * 4.0f, 0.0f, 1.0f);
    const float ca = std::clamp((float)(3.0 - (now - s_moveT)) * 2.0f, 0.0f, 1.0f);   // la flecha se va si el mouse no se mueve
    auto A = [&](int v) { return (int)(v * a); };
    const int n = std::max(s_n.load(), 1), f0 = s_first.load(), len = std::max(Wrap(s_last.load() - f0, n), 1);
    const float pos = std::clamp((float)Wrap(s_cur.load() - f0, n) / (float)len, 0.0f, 1.0f);
    const float mx = s_mx * ds.x, my = s_my * ds.y;
    const bool over = s_front && mx >= g.x0 - 8.0f && mx <= g.x1 + 8.0f && fabsf(my - (g.y + g.h * 0.5f)) <= g.hit;
    // fondo
    // (14/11 07:52, Agustin: "que el fondo de la barra siga eso") fondo con el mismo estilo de los paneles del juego (gris oscuro
    // translucido, borde fino claro); crece junto con la barra. Sin crecer queda dentro del panel del juego (no se dibuja)
    { const float grownW = (g.x1 - g.x0) / (ds.x * (1764.0f - 860.0f) / 1920.0f) - 1.0f;   // 0 = como la del juego
      const float fa = std::clamp(grownW * 4.0f, 0.0f, 1.0f) * a;
      if (fa > 0.01f) {
          const ImVec2 b0(g.x0 - 16 * k, g.y - 32 * k), b1(g.x1 + 16 * k, g.y + g.h + 12 * k);
          dl->AddRectFilled(b0, b1, IM_COL32(28, 30, 34, (int)(205 * fa)));
          dl->AddRect(b0, b1, IM_COL32(255, 255, 255, (int)(40 * fa)), 0.0f, 0, 1.0f * k);
      } }
    dl->AddRectFilled(ImVec2(g.x0, g.y), ImVec2(g.x1, g.y + g.h), IM_COL32(255, 255, 255, A(60)), g.h * 0.5f);
    const float px = g.x0 + (g.x1 - g.x0) * pos;
    dl->AddRectFilled(ImVec2(g.x0, g.y), ImVec2(px, g.y + g.h), IM_COL32(120, 210, 90, A(230)), g.h * 0.5f);
    dl->AddCircleFilled(ImVec2(px, g.y + g.h * 0.5f), g.h * 0.95f, IM_COL32(255, 255, 255, A(255)));
    char buf[16];
    if (FrameClock(f0, buf, sizeof buf)) dl->AddText(ImVec2(g.x0, g.y - 22 * k), IM_COL32(255, 255, 255, A(200)), buf);
    if (FrameClock(Wrap(f0 + len, n), buf, sizeof buf)) { const ImVec2 ts = ImGui::CalcTextSize(buf); dl->AddText(ImVec2(g.x1 - ts.x, g.y - 22 * k), IM_COL32(255, 255, 255, A(200)), buf); }
    if (over || s_drag) {   // donde caeria el clic: linea fina y minuto exacto
        const float fx = std::clamp(mx, g.x0, g.x1), fr = (fx - g.x0) / (g.x1 - g.x0);
        dl->AddLine(ImVec2(fx, g.y - 6 * k), ImVec2(fx, g.y + g.h + 6 * k), IM_COL32(255, 230, 120, A(255)), 2.0f * k);
        if (FrameClock(Wrap(f0 + (int)lroundf(fr * (float)len), n), buf, sizeof buf)) {
            const ImVec2 ts = ImGui::CalcTextSize(buf);
            const float bx = std::clamp(fx - ts.x * 0.5f, g.x0, g.x1 - ts.x);
            dl->AddRectFilled(ImVec2(bx - 6 * k, g.y - 28 * k), ImVec2(bx + ts.x + 6 * k, g.y - 6 * k), IM_COL32(20, 20, 20, A(230)), 4 * k);
            dl->AddText(ImVec2(bx, g.y - 26 * k), IM_COL32(255, 230, 120, A(255)), buf);
        }
    }
    // flecha del mouse (el juego esconde la de Windows)
    if (s_front && ca > 0.01f) {
        const ImVec2 p0(mx, my), b(mx, my + 19.0f * k), c(mx + 5.2f * k, my + 14.6f * k), d(mx + 13.5f * k, my + 13.5f * k);
        dl->AddTriangleFilled(p0, b, c, IM_COL32(255, 255, 255, (int)(255 * ca))); dl->AddTriangleFilled(p0, c, d, IM_COL32(255, 255, 255, (int)(255 * ca)));
        const ImVec2 pts[4] = { p0, b, c, d }; dl->AddPolyline(pts, 4, IM_COL32(0, 0, 0, (int)(255 * ca)), ImDrawFlags_Closed, 1.4f * k);
    }
}

} // namespace fx
