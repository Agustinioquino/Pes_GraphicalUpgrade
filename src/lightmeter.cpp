// PES21 FX - medidor de luz y luz ambiente calibrada (20/10).
// 1) MEDIDOR: cada 4 cuadros, justo despues de la composicion diferida (619b), un compute shader (pes_lightmeter_cs.hlsl)
//    mide la luz que recibe cada superficie (color final / albedo) en pasto horizontal, piel y camisetas, separada en
//    sol y sombra con la mascara de sombra del sol. Resultado: la relacion sol / sombra que de verdad ve el jugador y el
//    color de la sombra. Se anota en capturas\luz.txt y se ve en el menu (Iluminacion).
// 2) CALIBRACION: con cielo despejado la luz directa sobre el suelo es sin(altura) / C veces la del cielo (modelo de
//    cielo despejado de ASHRAE, C ~ 0.1); una transmision de TV comprime eso un poco. La DLL ajusta una ganancia de la luz
//    ambiente (volumen del motor + rellenos del mod) para que la relacion medida en el pasto llegue a la del cielo real
//    para esa altura del sol, de a poco y solo de dia con sol. Publica la ganancia en 125.x (la leen los shaders).
//   126: x on | y fuerza (0..1) | z contraste objetivo (1 = TV, 1.6 = fisico) | w cielo tapado por el estadio (0..1)
#include "core.h"
#include <d3d11_1.h>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <algorithm>
#include <cstdio>

namespace fx {

static ID3D11ComputeShader* s_cs = nullptr; static bool s_csTried = false;
static ID3D11Buffer* s_buf = nullptr; static ID3D11UnorderedAccessView* s_uav = nullptr; static ID3D11Buffer* s_cb = nullptr;
static const int kRing = 3;
static ID3D11Buffer* s_stage[kRing] = {}; static std::atomic<int> s_stageFrame[kRing]; static std::atomic<int> s_ringW{0};
static std::atomic<int> s_frame{0}; static std::atomic<int> s_lastRun{-100};
static std::mutex s_srvMx; static std::map<ID3D11Resource*, ID3D11ShaderResourceView*> s_colorSRV;
struct MeterRes { float n[6] = {}, lum[6] = {}, r[6] = {}, g[6] = {}, b[6] = {}; bool valid = false; };
static MeterRes s_res;                       // ultima lectura (promedios)
static float s_gain = 1.0f, s_target = 0.0f, s_ratio[3] = {}, s_tintSh = 0.0f, s_tintLit = 0.0f;
static double s_lastUpd = 0;
// (20/10) exposicion estable: brillo del pasto al sol (despues de la exposicion del motor) promediado corto y largo
static float s_eShort = 0.0f, s_eLong = 0.0f, s_expC = 1.0f; static int s_expMatch = -1; static double s_lastExp = 0;

static bool MeterOn() { return g_cfg.live[126][0] > 0.5f; }   // y126 = 0: solo mide (sin calibrar)
std::atomic<bool> g_meterHook{false};

static void EnsureRes() {
    if (!g_dev.dev) return;
    if (!s_csTried) {
        s_csTried = true;
        ID3DBlob* b = CompileNamedShader(L"pes_lightmeter_cs.hlsl", "cs_5_0");
        if (b) { if (FAILED(g_dev.dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_cs))) s_cs = nullptr; b->Release(); }
        Log("medidor de luz: %s", s_cs ? "shader listo" : "no se pudo compilar shaders\\pes_lightmeter_cs.hlsl");
        for (auto& f : s_stageFrame) f = -1;
    }
    if (!s_buf) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = 128; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_UNORDERED_ACCESS; d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        if (SUCCEEDED(g_dev.dev->CreateBuffer(&d, nullptr, &s_buf))) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC u = {}; u.Format = DXGI_FORMAT_R32_TYPELESS; u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; u.Buffer.NumElements = 32; u.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            if (FAILED(g_dev.dev->CreateUnorderedAccessView(s_buf, &u, &s_uav))) s_uav = nullptr;
        }
    }
    if (!s_cb) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 16; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; g_dev.dev->CreateBuffer(&d, nullptr, &s_cb); }
    for (auto& st : s_stage) if (!st) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 128; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &st); }
}

static DXGI_FORMAT TypedFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    default: return f;
    }
}
static ID3D11ShaderResourceView* ColorSRV(ID3D11Resource* res) {
    std::lock_guard<std::mutex> lk(s_srvMx);
    auto it = s_colorSRV.find(res); if (it != s_colorSRV.end()) return it->second;
    ID3D11Texture2D* t = nullptr; ID3D11ShaderResourceView* v = nullptr;
    if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) {
        D3D11_TEXTURE2D_DESC d; t->GetDesc(&d);
        if (d.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = TypedFormat(d.Format); sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
            if (FAILED(g_dev.dev->CreateShaderResourceView(t, &sd, &v))) v = nullptr;
        }
        t->Release();
    }
    if (s_colorSRV.size() > 8) { for (auto& kv : s_colorSRV) if (kv.second) kv.second->Release(); s_colorSRV.clear(); }
    s_colorSRV[res] = v;   // nullptr tambien (no reintentar). La vista retiene la textura: el puntero no se reutiliza
    return v;
}

// llamado despues del dibujo de la composicion, en el mismo contexto (puede ser diferido)
void LightMeterAfterComposition(ID3D11DeviceContext* c) {
    if (!MeterOn() || !g_dev.dev) return;
    int f = s_frame.load();
    int last = s_lastRun.load(); if (f - last < 4) return;
    if (!s_lastRun.compare_exchange_strong(last, f)) return;
    EnsureRes(); if (!s_cs || !s_uav || !s_cb) return;
    int w = s_ringW.load(); if (!s_stage[w]) return;
    // entradas de la composicion: t0 normal, t1 material, t8 albedo, t11 profundidad, t14 sombra; b2 escena; RT0 = salida
    ID3D11ShaderResourceView* in[15] = {}; c->PSGetShaderResources(0, 15, in);
    ID3D11Buffer* scb = nullptr; c->PSGetConstantBuffers(2, 1, &scb);
    ID3D11RenderTargetView* rtv[8] = {}; ID3D11DepthStencilView* dsv = nullptr; c->OMGetRenderTargets(8, rtv, &dsv);
    ID3D11ShaderResourceView* colorV = nullptr; UINT W = 0, H = 0;
    if (rtv[0]) {
        ID3D11Resource* r = nullptr; rtv[0]->GetResource(&r);
        if (r) { colorV = ColorSRV(r); ID3D11Texture2D* t = nullptr; if (SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); W = d.Width; H = d.Height; t->Release(); } r->Release(); }
    }
    bool ok = colorV && in[0] && in[1] && in[8] && in[11] && in[14] && scb && W > 64 && H > 64 && (g_bbW == 0 || W == g_bbW);
    if (ok) {
        // guardar el estado de compute del juego
        ID3D11ComputeShader* pcs = nullptr; c->CSGetShader(&pcs, nullptr, nullptr);
        ID3D11ShaderResourceView* psrv[6] = {}; c->CSGetShaderResources(0, 6, psrv);
        ID3D11UnorderedAccessView* puav = nullptr; c->CSGetUnorderedAccessViews(0, 1, &puav);
        ID3D11Buffer* pcb0 = nullptr; ID3D11Buffer* pcb2 = nullptr; c->CSGetConstantBuffers(0, 1, &pcb0); c->CSGetConstantBuffers(2, 1, &pcb2);
        // la salida de la composicion sigue enlazada como render target: desenlazar mientras se lee
        c->OMSetRenderTargets(0, nullptr, nullptr);
        const UINT stride = 2;
        UINT cbv[4] = { W, H, stride, 0 }; c->UpdateSubresource(s_cb, 0, nullptr, cbv, 0, 0);
        const UINT z[4] = { 0, 0, 0, 0 }; c->ClearUnorderedAccessViewUint(s_uav, z);
        ID3D11ShaderResourceView* srv[6] = { in[0], in[1], in[8], colorV, in[11], in[14] };
        c->CSSetShader(s_cs, nullptr, 0);
        c->CSSetShaderResources(0, 6, srv);
        c->CSSetUnorderedAccessViews(0, 1, &s_uav, nullptr);
        c->CSSetConstantBuffers(0, 1, &s_cb); c->CSSetConstantBuffers(2, 1, &scb);
        c->Dispatch((W / stride + 15) / 16, (H / stride + 15) / 16, 1);
        // restaurar
        ID3D11ShaderResourceView* nul[6] = {}; c->CSSetShaderResources(0, 6, nul);
        ID3D11UnorderedAccessView* nuav = nullptr; c->CSSetUnorderedAccessViews(0, 1, &nuav, nullptr);
        c->CSSetShader(pcs, nullptr, 0); c->CSSetShaderResources(0, 6, psrv);
        c->CSSetUnorderedAccessViews(0, 1, &puav, nullptr);
        c->CSSetConstantBuffers(0, 1, &pcb0); c->CSSetConstantBuffers(2, 1, &pcb2);
        if (pcs) pcs->Release(); for (auto* v : psrv) if (v) v->Release(); if (puav) puav->Release(); if (pcb0) pcb0->Release(); if (pcb2) pcb2->Release();
        UINT nrt = 0; for (UINT i = 0; i < 8; i++) if (rtv[i]) nrt = i + 1;
        c->OMSetRenderTargets(nrt, rtv, dsv);
        c->CopyResource(s_stage[w], s_buf);
        s_stageFrame[w] = f;
        s_ringW = (w + 1) % kRing;
    }
    for (auto* v : in) if (v) v->Release(); if (scb) scb->Release();
    for (auto* v : rtv) if (v) v->Release(); if (dsv) dsv->Release();
}

// relacion sol / sombra de un cielo despejado sobre el suelo, para la altura del sol (grados)
static float TargetRatio(float elevDeg) {
    const float* P = g_cfg.live[126];
    float turb = g_cfg.live[121][1] > 0.5f ? g_cfg.live[121][1] : 2.5f;
    float C = 0.09f + 0.015f * std::clamp(turb - 2.0f, 0.0f, 6.0f);                 // mas bruma = mas luz difusa
    float s = sinf(std::clamp(elevDeg, 2.0f, 90.0f) * 0.0174533f);
    float k = std::clamp(P[2] > 0.05f ? P[2] : 1.0f, 0.3f, 2.0f) * 0.5f;             // 1 = transmision de TV (comprime 50 %)
    float occl = std::clamp(P[3], 0.0f, 0.8f);                                        // el estadio tapa parte del cielo
    float r = 1.0f + k * s / (C * (1.0f - 0.5f * occl));                              // (20/10) a 40 grados ~4.8 (antes daba 7.5)
    return std::clamp(r, 1.5f, 9.0f);
}

void LightMeterPresent() {
    float* p125 = g_cfg.live[125];
    s_frame++;
    if (!MeterOn()) { g_meterHook = false; s_gain += (1.0f - s_gain) * 0.05f; p125[0] = s_gain; g_cfg.live[128][0] = 1.0f; return; }
    if (!g_dev.imm) return;
    // lectura: el staging mas viejo ya listo
    int best = -1, bestF = 1 << 30;
    for (int i = 0; i < kRing; i++) { int sf = s_stageFrame[i].load(); if (sf >= 0 && sf <= s_frame - 3 && sf < bestF) { best = i; bestF = sf; } }
    if (best >= 0) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_dev.imm->Map(s_stage[best], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
            const uint32_t* u = (const uint32_t*)m.pData; MeterRes r;
            for (int k = 0; k < 6; k++) { float n = (float)u[k * 5]; r.n[k] = n; if (n > 0) { r.lum[k] = u[k * 5 + 1] / 128.0f / n; r.r[k] = u[k * 5 + 2] / 128.0f / n; r.g[k] = u[k * 5 + 3] / 128.0f / n; r.b[k] = u[k * 5 + 4] / 128.0f / n; } }
            r.valid = true; s_res = r;
            g_dev.imm->Unmap(s_stage[best], 0);
            s_stageFrame[best] = -1;
            for (int c = 0; c < 3; c++) s_ratio[c] = (r.n[c * 2] > 150 && r.n[c * 2 + 1] > 150 && r.lum[c * 2 + 1] > 1e-4f) ? r.lum[c * 2] / r.lum[c * 2 + 1] : 0.0f;
            auto tint = [&](int k) { return r.n[k] > 150 ? r.b[k] / (std::max)(r.r[k], 1e-4f) : 0.0f; };
            s_tintLit = tint(0); s_tintSh = tint(1);
        }
    }
    // calibracion (cada ~0.25 s): de dia, con sol real, con pasto al sol y a la sombra en pantalla
    double t = Now();
    const float* P = g_cfg.live[126];
    float k = std::clamp(P[1], 0.0f, 1.0f);
    if (t - s_lastUpd >= 0.25) {
        double dt = (std::min)(t - s_lastUpd, 1.0); s_lastUpd = t;
        float elev = g_cfg.live[111][3]; bool sunny = g_cfg.live[114][3] > 0.5f;
        s_target = TargetRatio(elev);
        float gFull = s_gain;                                   // sin medida util (primer plano, sin sombra en el pasto): se mantiene
        if (!sunny || elev <= 4.0f) gFull = 1.0f;              // de noche / sin sol: la luz del juego
        else if (s_ratio[0] > 1.05f) {
            // la parte del sol no cambia con la ganancia: R - 1 = S / (g A)  ->  g' = g_aplicada (Rm - 1) / (Rt - 1)
            float gApp = p125[0] > 0.05f ? p125[0] : 1.0f;
            float gWant = gApp * (s_ratio[0] - 1.0f) / (s_target - 1.0f);
            // la ganancia aplicada es 1 + (gFull - 1) k  ->  gFull = 1 + (gWant - 1) / k
            gFull = k > 0.05f ? 1.0f + (gWant - 1.0f) / k : 1.0f;
        }
        gFull = std::clamp(gFull, 0.45f, 1.8f);
        float a = (float)(1.0 - exp(-dt / 2.5));
        s_gain += (gFull - s_gain) * a;
        s_gain = std::clamp(s_gain, 0.45f, 1.8f);
    }
    p125[0] = 1.0f + (s_gain - 1.0f) * k;
    p125[1] = s_ratio[0]; p125[2] = s_target; p125[3] = s_tintSh;
    // exposicion estable entre camaras (129: x on, y fuerza, z segundos de memoria): el motor cambia la exposicion en cada
    // toma (primeros planos, repeticiones); una camara de TV expone para la cancha al sol y casi no salta. Se compara el
    // brillo del pasto al sol de esta toma (corto) con el de los ultimos ~40 s (largo) y se corrige la diferencia en el
    // tonemap (128.x). Sin pasto al sol en pantalla (primer plano, tribuna) se mantiene la ultima correccion.
    { const float* E = g_cfg.live[129]; float* o = g_cfg.live[128];
      if (g_matchGen.load() != s_expMatch) { s_expMatch = g_matchGen.load(); s_eShort = s_eLong = 0.0f; s_expC = 1.0f; }
      double dtE = (std::min)(t - s_lastExp, 0.5); s_lastExp = t;
      bool have = s_res.valid && s_res.n[0] > 300 && s_res.lum[0] > 1e-4f;
      if (have) {
          float e = s_res.lum[0];
          if (s_eShort <= 0.0f) s_eShort = e; if (s_eLong <= 0.0f) s_eLong = e;
          float mem = std::clamp(E[2] > 1.0f ? E[2] : 40.0f, 5.0f, 300.0f);
          s_eShort += (e - s_eShort) * (float)(1.0 - exp(-dtE / 0.4));
          s_eLong  += (e - s_eLong)  * (float)(1.0 - exp(-dtE / mem));
      }
      float cT = 1.0f;
      if (E[0] > 0.5f && s_eShort > 1e-4f && s_eLong > 1e-4f) cT = std::clamp(s_eLong / s_eShort, 0.6f, 1.15f);   // (20/10) baja mas de lo que sube: subir quemaba la cancha en algunas camaras
      cT = 1.0f + (cT - 1.0f) * std::clamp(E[1], 0.0f, 1.0f);
      if (!have && E[0] > 0.5f) cT = s_expC;   // sin medida: se mantiene
      s_expC += (cT - s_expC) * (float)(1.0 - exp(-dtE / 0.5));
      o[0] = E[0] > 0.5f ? s_expC : 1.0f; o[1] = s_eShort; o[2] = s_eLong; }
    g_meterHook = true;
}

std::string LightMeterSummary() {
    char b[320];
    if (!s_res.valid) return "medidor: sin datos";
    snprintf(b, sizeof b, "medidor: pasto sol/sombra %.2f (objetivo %.2f) | piel %.2f | camisetas %.2f | sombra b/r %.2f (sol %.2f) | ganancia ambiente %.2f | exposicion %.2f | muestras pasto %.0f/%.0f",
        s_ratio[0], s_target, s_ratio[1], s_ratio[2], s_tintSh, s_tintLit, g_cfg.live[125][0], g_cfg.live[128][0], s_res.n[0], s_res.n[1]);
    return b;
}

void LightMeterReloadShaders() { if (s_cs) { s_cs->Release(); s_cs = nullptr; } s_csTried = false; }

} // namespace fx
