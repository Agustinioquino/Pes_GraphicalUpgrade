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
// contenido de los bloques (para ver si hay algo propio de cada jugador): por dibujo, b5 del VS (m_viewWorld, m_world,
// m_useWeightCount, m_localParam[9] = 288 bytes) en + 0 y b4 del PS (m_materials[8] = 128 bytes) en + 384
static const int kSlots = 24, kSlotBytes = 512;
static ID3D11Buffer* s_stage = nullptr;
struct SlotInfo { int order; uint64_t ps; bool b5, b4; };
static SlotInfo s_slot[kSlots]; static int s_nSlots = 0, s_readFrame = -1, s_readTries = 0;

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
static ID3D11Buffer* CbOf(ID3D11DeviceContext1* c1, bool ps, UINT slot, UINT* first) {   // con AddRef
    ID3D11Buffer* b = nullptr; UINT f = 0, n = 0;
    if (ps) c1->PSGetConstantBuffers1(slot, 1, &b, &f, &n); else c1->VSGetConstantBuffers1(slot, 1, &b, &f, &n);
    *first = f;
    return b;
}
static bool CopyBlock(ID3D11DeviceContext* c, ID3D11Buffer* src, UINT first, UINT bytes, UINT dstOff) {
    if (!src || !s_stage) return false;
    D3D11_BUFFER_DESC d; src->GetDesc(&d);
    const UINT off = first * 16;
    if (d.ByteWidth < off + bytes || (d.Usage == D3D11_USAGE_STAGING)) return false;
    D3D11_BOX bx = { off, 0, 0, off + bytes, 1, 1 };
    c->CopySubresourceRegion(s_stage, 0, dstOff, 0, 0, src, 0, &bx);
    return true;
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
    ID3D11Buffer *b4 = nullptr, *b5 = nullptr, *b6 = nullptr; UINT f4 = 0, f5 = 0, f6 = 0;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) {
        b4 = CbOf(c1, true, 4, &f4); b5 = CbOf(c1, false, 5, &f5); b6 = CbOf(c1, false, 6, &f6); c1->Release();
    }
    char b[320];
    snprintf(b, sizeof b, "  #%d %s PS %016llx VS %016llx %u idx | b4 %p+%u b5 %p+%u b6 %p+%u | t0 %p t1 %p", order,
             c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED ? "dif" : "inm", (unsigned long long)ps, (unsigned long long)vs, count,
             (void*)b4, f4, (void*)b5, f5, (void*)b6, f6, ResOf(c, 0), ResOf(c, 1));
    s_lines.push_back(b);
    if (s_nSlots < kSlots && s_stage) {   // copia del contenido para leerlo dos cuadros despues
        SlotInfo& si = s_slot[s_nSlots];
        si.order = order; si.ps = ps;
        si.b5 = CopyBlock(c, b5, f5, 288, (UINT)s_nSlots * kSlotBytes);
        si.b4 = CopyBlock(c, b4, f4, 128, (UINT)s_nSlots * kSlotBytes + 384);
        s_nSlots++;
    }
    if (b4) b4->Release(); if (b5) b5->Release(); if (b6) b6->Release();
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
        if (total > 0 && s_nSlots > 0) { s_readFrame = f + 2; s_readTries = 0; } else s_nSlots = 0;
    }
    if (s_readFrame >= 0 && f >= s_readFrame && g_dev.imm && s_stage) {   // contenido de los bloques
        D3D11_MAPPED_SUBRESOURCE m;
        HRESULT hr = g_dev.imm->Map(s_stage, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
        if (SUCCEEDED(hr)) {
            for (int i = 0; i < s_nSlots; i++) {
                const float* q = (const float*)((const char*)m.pData + (size_t)i * kSlotBytes);
                char l[900]; int n = snprintf(l, sizeof l, "  #%d PS %08llx", s_slot[i].order, (unsigned long long)(s_slot[i].ps >> 32));
                if (s_slot[i].b5) {
                    n += snprintf(l + n, sizeof l - n, " | mundo t(%.2f %.2f %.2f) peso %.0f/%.0f | local", q[28], q[29], q[30], q[32], q[35]);
                    for (int k = 36; k < 72 && n < 700; k++) n += snprintf(l + n, sizeof l - n, (k % 4 == 0) ? " [%.3g" : (k % 4 == 3 ? " %.3g]" : " %.3g"), q[k]);
                }
                if (s_slot[i].b4) {
                    const float* mq = q + 96; n += snprintf(l + n, sizeof l - n, " | material");
                    for (int k = 0; k < 32 && n < 880; k++) n += snprintf(l + n, sizeof l - n, (k % 4 == 0) ? " [%.3g" : (k % 4 == 3 ? " %.3g]" : " %.3g"), mq[k]);
                }
                Log("%s", l);
            }
            g_dev.imm->Unmap(s_stage, 0);
            s_readFrame = -1; s_nSlots = 0;
        } else if (++s_readTries > 20) { s_readFrame = -1; s_nSlots = 0; }
    }
    const double t = Now();
    if (s_armFrame.load() < 0 && s_readFrame < 0 && s_caps < 10 && t > s_next) {
        s_next = t + 20.0;
        if (!s_stage && g_dev.dev) {
            D3D11_BUFFER_DESC d = {}; d.ByteWidth = kSlots * kSlotBytes; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            g_dev.dev->CreateBuffer(&d, nullptr, &s_stage);
        }
        { std::lock_guard<std::mutex> lk(s_mx); s_order = 0; s_total = 0; s_lines.clear(); s_nSlots = 0; }
        s_armFrame = f + 1; g_skinDiagArmed = true;
    }
}

} // namespace fx
