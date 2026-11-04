// PES21 FX (04/11, Agustin: "subir la cantidad de poligonos de las caras / teselacion, que se note realista, solo en primeros
// planos, sin deformar demasiado") TESELACION DE LA CARA: en los dibujos de piel de la cara (VS faab87ee / 455eb97b con una malla
// que expr.cpp reconocio como cara: cabeza en la paleta, no boca, no ojo, no pestanas) se ponen un hull y un domain shader
// (shaders\pes_face_tess_*.hlsl) y la topologia pasa a parches de 3 puntos; despues del dibujo se devuelve todo. El HS decide
// el factor por arista segun su largo en pantalla (fila 357) y el DS redondea con teselacion de Phong, respetando los bordes de
// cada pieza (peso de borde por vertice, t119 del VS, calculado en expr.cpp) y un tope en mm (fila 358). Ver pes_face_tess.hlsli.
#include "core.h"
#include <d3d11_1.h>
#include <mutex>
#include <map>
#include <atomic>

namespace fx {

static const uint64_t kVsHead = 0xfaab87ee4ef8b783ULL, kVsFace = 0x455eb97bc2c3a958ULL;
static std::mutex s_mx;
static bool s_tried = false;
static ID3D11HullShader* s_hs[2][2] = {};     // [variante del VS][orientacion: 0 cw, 1 ccw]
static ID3D11DomainShader* s_ds[2] = {};
static std::map<uint64_t, ID3D11Buffer*> s_vpCB;   // cbuffer b13 del HS por tamano de viewport (inmutables, pocos)
static std::atomic<int> s_draws{0}, s_skipTopo{0}, s_noShader{0}, s_skipFar{0};
static std::atomic<int> s_lastVpW{0}, s_lastVpH{0};

static void Ensure() {
    if (s_tried || !g_dev.dev) return;
    s_tried = true;
    const wchar_t* hs[2][2] = { { L"pes_face_tess_a_cw_hs.hlsl", L"pes_face_tess_a_ccw_hs.hlsl" }, { L"pes_face_tess_b_cw_hs.hlsl", L"pes_face_tess_b_ccw_hs.hlsl" } };
    const wchar_t* ds[2] = { L"pes_face_tess_a_ds.hlsl", L"pes_face_tess_b_ds.hlsl" };
    int ok = 0;
    for (int v = 0; v < 2; v++) {
        for (int w = 0; w < 2; w++) if (ID3DBlob* b = CompileNamedShader(hs[v][w], "hs_5_0")) { if (SUCCEEDED(g_dev.dev->CreateHullShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_hs[v][w]))) ok++; b->Release(); }
        if (ID3DBlob* b = CompileNamedShader(ds[v], "ds_5_0")) { if (SUCCEEDED(g_dev.dev->CreateDomainShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_ds[v]))) ok++; b->Release(); }
    }
    Log("teselacion de caras: shaders %d de 6 %s", ok, ok == 6 ? "listos" : "(faltan: ver errores de compilacion)");
}

void FaceTessReloadShaders() {
    std::lock_guard<std::mutex> lk(s_mx);
    // los objetos viejos no se liberan: un dibujo de otro hilo puede tenerlos puestos en este momento (igual que los reemplazos)
    for (auto& a : s_hs) for (auto& h : a) h = nullptr;
    for (auto& d : s_ds) d = nullptr;
    s_tried = false;
}

static ID3D11Buffer* VpCB(float w, float h) {
    const uint64_t key = ((uint64_t)(UINT)w << 32) | (UINT)h;
    std::lock_guard<std::mutex> lk(s_mx);
    auto it = s_vpCB.find(key);
    if (it != s_vpCB.end()) return it->second;
    if (s_vpCB.size() > 64) return nullptr;   // algo raro (viewports que cambian todo el tiempo): no acumular
    const float v[4] = { w, h, w > 0 ? 1.0f / w : 0, h > 0 ? 1.0f / h : 0 };
    D3D11_BUFFER_DESC d{}; d.ByteWidth = 16; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA s{}; s.pSysMem = v;
    ID3D11Buffer* b = nullptr; if (FAILED(g_dev.dev->CreateBuffer(&d, &s, &b))) return nullptr;
    s_vpCB[key] = b; return b;
}

bool FaceTessBegin(ID3D11DeviceContext* c, uint64_t vsHash, ID3D11ShaderResourceView* seam, ID3D11ShaderResourceView* params, FaceTessScope& s) {
    if (!seam || !params || !g_dev.dev || g_cfg.live[357][0] < 0.5f) return false;
    const int v = vsHash == kVsHead ? 0 : vsHash == kVsFace ? 1 : -1;
    if (v < 0) return false;
    const int w = g_cfg.live[358][3] > 0.5f ? 1 : 0;
    ID3D11HullShader* hs; ID3D11DomainShader* ds;
    { std::lock_guard<std::mutex> lk(s_mx); Ensure(); hs = s_hs[v][w]; ds = s_ds[v]; }
    if (!hs || !ds) { s_noShader.fetch_add(1, std::memory_order_relaxed); return false; }
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED; c->IAGetPrimitiveTopology(&topo);
    if (topo != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) { s_skipTopo.fetch_add(1, std::memory_order_relaxed); return false; }
    // viewport del dibujo (el HS mide las aristas en pixeles); la distancia maxima (solo primeros planos) la mira el HS por triangulo
    D3D11_VIEWPORT vp{}; UINT n = 1; c->RSGetViewports(&n, &vp);
    if (n < 1 || vp.Width < 1 || vp.Height < 1) return false;
    ID3D11Buffer* vb = VpCB(vp.Width, vp.Height); if (!vb) return false;
    s_lastVpW = (int)vp.Width; s_lastVpH = (int)vp.Height;
    // b2 (g_vsScene) del VS al DS, respetando la ventana (first / num) si el juego la enlazo con *SetConstantBuffers1
    ID3D11Buffer* b2 = nullptr; UINT first = 0, num = 0; ID3D11DeviceContext1* c1 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) {
        c1->VSGetConstantBuffers1(2, 1, &b2, &first, &num);
        if (b2) { if (num) c1->DSSetConstantBuffers1(2, 1, &b2, &first, &num); else c1->DSSetConstantBuffers(2, 1, &b2); }
        c1->Release();
    } else { c->VSGetConstantBuffers(2, 1, &b2); if (b2) c->DSSetConstantBuffers(2, 1, &b2); }
    if (b2) b2->Release();
    c->HSSetShader(hs, nullptr, 0); c->DSSetShader(ds, nullptr, 0);
    c->HSSetConstantBuffers(13, 1, &vb);
    c->HSSetShaderResources(120, 1, &params); c->DSSetShaderResources(120, 1, &params);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    s.on = true; s.topo = topo;
    s_draws.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void FaceTessEnd(ID3D11DeviceContext* c, FaceTessScope& s) {
    if (!s.on) return;
    s.on = false;
    c->IASetPrimitiveTopology(s.topo);
    c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* nb = nullptr; ID3D11ShaderResourceView* nv = nullptr;
    c->HSSetConstantBuffers(13, 1, &nb); c->DSSetConstantBuffers(2, 1, &nb);
    c->HSSetShaderResources(120, 1, &nv); c->DSSetShaderResources(120, 1, &nv);
}

void FaceTessPresent() {
    static int frames = 0;
    if (++frames < 3600) return;
    frames = 0;
    const int d = s_draws.exchange(0), st = s_skipTopo.exchange(0), ns = s_noShader.exchange(0);
    if (!d && !st && !ns) return;
    Log("teselacion de caras: %d dibujos teselados en 60 s (sin triangulos sueltos %d, sin shader %d); viewport %dx%d; suavidad %.2f, %.0f px por segmento, factor maximo %.0f, hasta %.2f m (+%.2f), tope %.1f mm, orientacion %s",
        d, st, ns, s_lastVpW.load(), s_lastVpH.load(), g_cfg.live[357][1], g_cfg.live[357][2], g_cfg.live[357][3], g_cfg.live[358][0], g_cfg.live[358][1], g_cfg.live[358][2], g_cfg.live[358][3] > 0.5f ? "invertida" : "igual");
}

}   // namespace fx
