// PES21 FX - tono de piel por jugador (24/10). Primero un diagnostico para elegir como llega el tono de cada jugador a sus
// dibujos (la piel del cuerpo es una textura por cada uno de los 6 colores del juego, compartida por todos los que tienen
// ese color: hace falta saber, en cada dibujo, de que jugador es). Cada 20 s (hasta 10 cuadros con personajes por sesion)
// se anota UN cuadro entero de dibujos de personajes (hasta 60 lineas): orden, PS / VS, indices, bloques de constantes (PS b4 = material, VS b5 = objeto, VS b6 =
// huesos: buffer + primer constante) y texturas t0 / t1 del PS. Con eso se ve si la cara sale antes que el cuerpo, si los
// bloques se repiten de un cuadro al otro (servirian de clave del jugador) y si el material o la textura son propios de
// cada jugador.
#include "core.h"
#include <d3d11_1.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace fx {

std::atomic<bool> g_skinDiagArmed{false};
static std::atomic<int> s_armFrame{-1};
static int s_caps = 0; static double s_next = 0;
static std::mutex s_mx; static std::vector<std::string> s_lines; static int s_order = 0, s_total = 0;

static bool Interesting(uint64_t ps, uint64_t vs) {
    switch (ps) {
    case 0xaa7d1833f6a11481ULL:   // piel del cuerpo
    case 0x5ce3ed8702271bc1ULL:   // cara
    case 0xce188ae110354aeaULL:   // cara (otra variante)
    case 0x63fab7735eee149cULL:   // boca
    case 0x55297925e0c66085ULL:   // short / medias
    case 0xfccd955fa6f3e8ebULL:   // otra malla del cuerpo
        return true;
    }
    return vs == 0x1c02a92ab4ae3362ULL || vs == 0x1c9bde0e3060b6e0ULL;
}
static void CbOf(ID3D11DeviceContext1* c1, bool ps, UINT slot, void** buf, UINT* first) {
    ID3D11Buffer* b = nullptr; UINT f = 0, n = 0;
    if (ps) c1->PSGetConstantBuffers1(slot, 1, &b, &f, &n); else c1->VSGetConstantBuffers1(slot, 1, &b, &f, &n);
    *buf = b; *first = f;
    if (b) b->Release();
}
static void* ResOf(ID3D11DeviceContext* c, UINT slot) {
    ID3D11ShaderResourceView* v = nullptr; c->PSGetShaderResources(slot, 1, &v);
    if (!v) return nullptr;
    ID3D11Resource* r = nullptr; v->GetResource(&r); v->Release();
    if (r) r->Release();
    return r;
}

void SkinDiagDraw(ID3D11DeviceContext* c, uint64_t ps, uint64_t vs, UINT count) {
    if (PresentNo() != s_armFrame.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(s_mx);
    const int order = s_order++;
    if (!Interesting(ps, vs)) return;
    s_total++;
    if (s_lines.size() >= 60) return;
    ID3D11DeviceContext1* c1 = nullptr;
    void *b4 = nullptr, *b5 = nullptr, *b6 = nullptr; UINT f4 = 0, f5 = 0, f6 = 0;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) {
        CbOf(c1, true, 4, &b4, &f4); CbOf(c1, false, 5, &b5, &f5); CbOf(c1, false, 6, &b6, &f6); c1->Release();
    }
    char b[320];
    snprintf(b, sizeof b, "  #%d %s PS %016llx VS %016llx %u idx | b4 %p+%u b5 %p+%u b6 %p+%u | t0 %p t1 %p", order,
             c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED ? "dif" : "inm", (unsigned long long)ps, (unsigned long long)vs, count,
             b4, f4, b5, f5, b6, f6, ResOf(c, 0), ResOf(c, 1));
    s_lines.push_back(b);
}

void SkinDiagPresent() {
    if (g_cfg.live[184][3] < 0.5f) { g_skinDiagArmed = false; return; }   // w184: diagnostico de dibujos de personajes
    const int f = PresentNo();
    const int armed = s_armFrame.load();
    if (armed >= 0 && f > armed) {   // el cuadro capturado ya paso: al log
        std::vector<std::string> l; int total;
        { std::lock_guard<std::mutex> lk(s_mx); l.swap(s_lines); total = s_total; s_total = 0; }
        if (total > 0) {   // cuadros sin personajes no cuentan
            s_caps++;
            Log("piel: dibujos de personajes del cuadro %d (%d de interes; # = orden entre todos los dibujos del cuadro):", armed, total);
            for (auto& s : l) Log("%s", s.c_str());
        }
        s_armFrame = -1; g_skinDiagArmed = false;
    }
    const double t = Now();
    if (s_armFrame.load() < 0 && s_caps < 10 && t > s_next) {
        s_next = t + 20.0;
        { std::lock_guard<std::mutex> lk(s_mx); s_order = 0; s_total = 0; s_lines.clear(); }
        s_armFrame = f + 1; g_skinDiagArmed = true;
    }
}

} // namespace fx
