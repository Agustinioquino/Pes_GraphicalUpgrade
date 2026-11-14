// PES21 FX - rebote de luz en pantalla (SSGI) temporal (21/10). Antes el SSGI se calculaba dentro de la composicion (619b)
// con 16 muestras por pixel a resolucion completa y el ruido lo tenia que tapar el TAA del juego. Ahora la DLL, justo antes
// de la composicion, lo calcula a media resolucion con 8 muestras (shaders\pes_ssgi_ps.hlsl) y lo acumula en el tiempo
// reproyectando con la camara del cuadro anterior (pes_ssgi_tp_ps.hlsl): menos ruido, mas radio y menos costo. El resultado
// va a t26 de la composicion, que lo sube a resolucion completa respetando los bordes. 153: x on | y radio extra | z cuadros.
// La DLL publica en 155: x = 1 si el SSGI temporal corrio en el cuadro anterior, y = contador de cuadros (patron que gira).
#include "core.h"
#include <atomic>
#include <mutex>

namespace fx {

static ID3D11VertexShader* s_vs = nullptr; static ID3D11PixelShader* s_ps = nullptr; static ID3D11PixelShader* s_tp = nullptr; static bool s_tried = false;
static ID3D11Texture2D* s_tex[3] = {}; static ID3D11RenderTargetView* s_rtv[3] = {}; static ID3D11ShaderResourceView* s_srv[3] = {};   // 0 = este cuadro, 1/2 = historia
static UINT s_w = 0, s_h = 0, s_hw = 0, s_hh = 0;   // (02/11) s_w/s_h = historia; s_hw/s_hh = este cuadro (media en reconstruccion)
static ID3D11Buffer* s_prev = nullptr; static bool s_prevOk = false;
static ID3D11SamplerState* s_sampP = nullptr; static ID3D11SamplerState* s_sampL = nullptr;
static ID3D11RasterizerState* s_rs = nullptr; static ID3D11DepthStencilState* s_dss = nullptr; static ID3D11BlendState* s_bs = nullptr;
static std::atomic<int> s_lastFrame{-1}; static ID3D11ShaderResourceView* s_last = nullptr;
static std::atomic<int> s_edFullFrame{-100};   // (08/11 0qtsss) ultimo cuadro del editor a resolucion completa
static int s_ping = 0; static std::atomic<int> s_ranFrame{-100}; static int s_counter = 0;
// (01/11) Agustin: 'el SSGI no hace ningun cambio'. Registro cada 30 s: cuantas veces corrio y por que no (motivo de cada salida)
static std::atomic<int> s_nRan{0}, s_nCalls{0}, s_nEd{0}, s_why[8] = {};   // 0 sin shaders, 1 faltan texturas, 2 tamano, 3 msaa/targets, 4 sin cbuffer, 5 cbuffer chico, 6 prev
static double s_logT = 0; static int s_logs = 0;
// (02/11) medidor de GPU del SSGI (pedido del hilo del sol para el antes/despues de la Completa barata): consultas de tiempo alrededor
// de los 2 Draw (paso 1 trazado, paso 2 acumulacion/reconstruccion), como el "pasto GPU"; se leen en Present 3 cuadros despues sin esperar
static const int kSsgiTimers = 6;
struct SsgiTimer { ID3D11Query* dis = nullptr; ID3D11Query* ts[3] = {}; std::atomic<int> tag{-1}; };
static SsgiTimer s_tm[kSsgiTimers]; static std::atomic<int> s_tmState{0}; static std::mutex s_tmMx;
static double s_tmSum[2] = {}, s_tmMax[2] = {}; static int s_tmN = 0, s_tmLost = 0, s_tmDisjoint = 0;
static bool SsgiTimersReady() {
    int st = s_tmState.load(); if (st) return st == 1;
    std::lock_guard<std::mutex> lk(s_tmMx);
    if (s_tmState.load()) return s_tmState.load() == 1;
    D3D11_QUERY_DESC qd = {}; bool ok = g_dev.dev != nullptr;
    for (auto& t : s_tm) { if (!ok) break;
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT; if (FAILED(g_dev.dev->CreateQuery(&qd, &t.dis))) { t.dis = nullptr; ok = false; }
        qd.Query = D3D11_QUERY_TIMESTAMP; for (auto& q : t.ts) if (ok && FAILED(g_dev.dev->CreateQuery(&qd, &q))) { q = nullptr; ok = false; } }
    if (!ok) { for (auto& t : s_tm) { if (t.dis) t.dis->Release(); t.dis = nullptr; for (auto& q : t.ts) { if (q) q->Release(); q = nullptr; } } Log("SSGI GPU: no se pudieron crear las consultas de tiempo (sin medidor)"); }
    s_tmState.store(ok ? 1 : 2); return ok;
}
static SsgiTimer* SsgiTimerClaim(int frame) {
    if (!SsgiTimersReady()) return nullptr;
    for (auto& t : s_tm) { int e = -1; if (t.tag.compare_exchange_strong(e, frame)) {
        if (!t.dis || !t.ts[0] || !t.ts[1] || !t.ts[2]) { t.tag = -1; return nullptr; }
        return &t; } }
    return nullptr;
}
static void SsgiTimersRead(int f) {   // contexto inmediato (Present)
    if (s_tmState.load() != 1 || !g_dev.imm) return;
    for (auto& t : s_tm) { int tg = t.tag.load(); if (tg < 0 || tg > f - 3) continue;
        if (f - tg > 90) { t.tag = -1; s_tmLost++; continue; }
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d = {}; if (g_dev.imm->GetData(t.dis, &d, sizeof d, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        UINT64 v[3] = {}; bool all = true;
        for (int i = 0; i < 3 && all; i++) if (g_dev.imm->GetData(t.ts[i], &v[i], 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) all = false;
        if (!all) continue;
        t.tag = -1;
        if (d.Disjoint || d.Frequency == 0) { s_tmDisjoint++; continue; }
        for (int i = 0; i < 2; i++) { double ms = v[i + 1] >= v[i] ? (double)(v[i + 1] - v[i]) * 1000.0 / (double)d.Frequency : 0.0; s_tmSum[i] += ms; if (ms > s_tmMax[i]) s_tmMax[i] = ms; }
        s_tmN++; }
}

static void EnsureShaders() {
    if (s_tried || !g_dev.dev) return; s_tried = true;
    ID3DBlob* vb = CompileNamedShader(L"pes_fs_vs.hlsl", "vs_5_0");
    ID3DBlob* pb = CompileNamedShader(L"pes_ssgi_ps.hlsl", "ps_5_0");
    ID3DBlob* tb = CompileNamedShader(L"pes_ssgi_tp_ps.hlsl", "ps_5_0");
    if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_vs); vb->Release(); }
    if (pb) { g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_ps); pb->Release(); }
    if (tb) { g_dev.dev->CreatePixelShader(tb->GetBufferPointer(), tb->GetBufferSize(), nullptr, &s_tp); tb->Release(); }
    if (!s_sampP) {
        D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        g_dev.dev->CreateSamplerState(&sd, &s_sampP); sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; g_dev.dev->CreateSamplerState(&sd, &s_sampL);
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; g_dev.dev->CreateRasterizerState(&rd, &s_rs);
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = FALSE; dd.StencilEnable = FALSE; g_dev.dev->CreateDepthStencilState(&dd, &s_dss);
        D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; g_dev.dev->CreateBlendState(&bd, &s_bs);
    }
    Log("ssgi temporal: shaders %s", (s_vs && s_ps && s_tp) ? "listos" : "NO compilaron (ver errores)");
}
void SSGIReloadShaders() {
    if (s_vs) { s_vs->Release(); s_vs = nullptr; } if (s_ps) { s_ps->Release(); s_ps = nullptr; } if (s_tp) { s_tp->Release(); s_tp = nullptr; }
    s_tried = false;
}
static bool EnsureTargets(UINT w, UINT h, UINT cw, UINT ch) {
    if (s_tex[0] && s_w == w && s_h == h && s_hw == cw && s_hh == ch) return true;
    for (int i = 0; i < 3; i++) { if (s_srv[i]) s_srv[i]->Release(); if (s_rtv[i]) s_rtv[i]->Release(); if (s_tex[i]) s_tex[i]->Release(); s_srv[i] = nullptr; s_rtv[i] = nullptr; s_tex[i] = nullptr; }
    D3D11_TEXTURE2D_DESC td{}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int i = 0; i < 3; i++) {
        td.Width = i == 0 ? cw : w; td.Height = i == 0 ? ch : h;
        if (FAILED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_tex[i]))) return false;
        if (FAILED(g_dev.dev->CreateRenderTargetView(s_tex[i], nullptr, &s_rtv[i]))) return false;
        if (FAILED(g_dev.dev->CreateShaderResourceView(s_tex[i], nullptr, &s_srv[i]))) return false;
    }
    s_w = w; s_h = h; s_hw = cw; s_hh = ch; s_prevOk = false;
    Log("ssgi temporal: targets %ux%u RGBA16F (este cuadro %ux%u%s)", w, h, cw, ch, (cw != w) ? ", reconstruccion" : "");
    return true;
}

// Publica el estado para los shaders (lo llama Present, antes de subir los parametros)
void SSGIPresent(int presentNo) {
    float* p = g_cfg.live[155];
    s_counter = (s_counter + 1) % 4096;
    int ran = s_ranFrame.load();
    p[0] = (g_cfg.live[153][0] > 0.5f && ran >= 0 && presentNo - ran <= 2) ? 1.0f : 0.0f;
    p[1] = (float)s_counter;
    // (02/11) z337 = 1 ("Completa"): se calcula a media resolucion un pixel exacto de cada 2x2 (fase que rota) y se reconstruye
    // a resolucion completa en el paso 2: definicion de Completa con el costo de Media. 155.z = 1 + fase (0 = modo viejo)
    p[2] = (g_cfg.live[337][2] > 0.5f) ? (float)(1 + (s_counter & 3)) : 0.0f;
    if (presentNo - s_edFullFrame.load() <= 2) p[2] = 0.0f;   // (08/11 0qtsss) editor a resolucion completa: sin reconstruccion
    SsgiTimersRead(presentNo);
    double t = Now();
    if (s_logs < 40 && t - s_logT >= 30.0) { s_logT = t; s_logs++;
        if (s_tmN > 0 || s_tmLost || s_tmDisjoint) {
            Log("SSGI GPU (30 s): %d cuadros medidos | paso 1 trazado %.2f ms (max %.2f) | paso 2 acumulacion %.2f (max %.2f) | total %.2f ms por cuadro | modo %s %ux%u -> %ux%u | descartadas: reloj %d, listas sin ejecutar %d",
                s_tmN, s_tmN ? s_tmSum[0] / s_tmN : 0.0, s_tmMax[0], s_tmN ? s_tmSum[1] / s_tmN : 0.0, s_tmMax[1], s_tmN ? (s_tmSum[0] + s_tmSum[1]) / s_tmN : 0.0,
                g_cfg.live[337][2] > 0.5f ? (g_cfg.live[155][2] > 0.5f ? "Completa reconstruida" : "Completa") : "Media", s_hw, s_hh, s_w, s_h, s_tmDisjoint, s_tmLost);
            s_tmSum[0] = s_tmSum[1] = s_tmMax[0] = s_tmMax[1] = 0; s_tmN = s_tmLost = s_tmDisjoint = 0; }
        Log("SSGI (30 s): prendido %d, corrio %d de %d llamadas de la composicion (%d en el editor)%s | no corrio: sin shaders %d, faltan texturas %d, tamano %d, msaa/targets %d, sin cbuffer %d, cbuffer chico %d, prev %d | fuerza w153 %.2f, z337 %.0f (%ux%u)",
            g_cfg.live[153][0] > 0.5f ? 1 : 0, s_nRan.exchange(0), s_nCalls.exchange(0), s_nEd.exchange(0), (s_vs && s_ps && s_tp) ? "" : " (SHADERS SIN COMPILAR: ver errores de pes_ssgi)",
            s_why[0].exchange(0), s_why[1].exchange(0), s_why[2].exchange(0), s_why[3].exchange(0), s_why[4].exchange(0), s_why[5].exchange(0), s_why[6].exchange(0),
            g_cfg.live[153][3], g_cfg.live[337][2], s_w, s_h); }
}

// En BeforeDraw de la composicion (619b): calcula y devuelve el SSGI acumulado para t26 (null si esta apagado o fallo)
// (08/11 0qtsss) editor = composicion del editor (f807, fila 411): con y411 a resolucion completa sin reconstruccion (SSGIPresent
// deja 155.z = 0 mientras el editor es lo ultimo que corrio, para que los shaders del SSGI no lean una fase de reconstruccion)
ID3D11ShaderResourceView* SSGIBeforeComposition(ID3D11DeviceContext* c, int frame, bool editor) {
    if (!g_dev.dev || g_cfg.live[153][0] < 0.5f) return nullptr;
    if (s_lastFrame.load() == frame && s_last) return s_last;
    s_nCalls++;
    EnsureShaders(); if (!s_vs || !s_ps || !s_tp) { s_why[0]++; return nullptr; }
    ID3D11ShaderResourceView* in[12] = {}; c->PSGetShaderResources(0, 12, in);
    auto rel = [&]() { for (auto* v : in) if (v) v->Release(); };
    ID3D11ShaderResourceView *depth = in[11], *nrm = in[0], *alb = in[8], *dif = in[9];
    if (!depth || !nrm || !alb || !dif) { s_why[1]++; rel(); return nullptr; }
    ID3D11Resource* res = nullptr; depth->GetResource(&res); if (!res) { rel(); return nullptr; }
    D3D11_TEXTURE2D_DESC td; ((ID3D11Texture2D*)res)->GetDesc(&td); res->Release();
    if (td.Width < 640 || (g_bbW && (td.Width < g_bbW * 3 / 4 || td.Width > g_bbW * 3))) { s_why[2]++; rel(); return nullptr; }
    // (31/10) z337 = 1: a resolucion completa (mas definido, ~4x el costo)
    // (02/11) con 155.z > 0 (reconstruccion) el paso 1 va a media y la historia a completa
    bool recon = g_cfg.live[155][2] > 0.5f;
    const bool edFull = editor && g_cfg.live[411][1] > 0.5f;
    UINT dv = (g_cfg.live[337][2] > 0.5f || edFull) ? 1u : 2u;
    UINT fw = (td.Width + dv - 1) / dv, fh = (td.Height + dv - 1) / dv;
    UINT hw = recon ? (td.Width + 1) / 2 : fw, hh = recon ? (td.Height + 1) / 2 : fh;
    if (td.SampleDesc.Count != 1 || !EnsureTargets(fw, fh, hw, hh)) { s_why[3]++; rel(); return nullptr; }
    ID3D11Buffer* scb = nullptr; c->PSGetConstantBuffers(2, 1, &scb);
    ID3D11Buffer* sys = nullptr; c->PSGetConstantBuffers(0, 1, &sys);
    if (!scb) { s_why[4]++; rel(); if (sys) sys->Release(); return nullptr; }
    D3D11_BUFFER_DESC bd; scb->GetDesc(&bd);
    if (bd.ByteWidth < 480) { s_why[5]++; rel(); scb->Release(); if (sys) sys->Release(); return nullptr; }
    if (s_prev) { D3D11_BUFFER_DESC pd; s_prev->GetDesc(&pd); if (pd.ByteWidth != bd.ByteWidth) { s_prev->Release(); s_prev = nullptr; } }
    if (!s_prev) { D3D11_BUFFER_DESC pd = bd; pd.Usage = D3D11_USAGE_DEFAULT; pd.CPUAccessFlags = 0; pd.MiscFlags = 0; pd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; pd.StructureByteStride = 0; g_dev.dev->CreateBuffer(&pd, nullptr, &s_prev); s_prevOk = false; }
    if (!s_prev) { s_why[6]++; rel(); scb->Release(); if (sys) sys->Release(); return nullptr; }
    if (!s_prevOk) { c->CopyResource(s_prev, scb); s_prevOk = true; }
    // (23/10) corte de camara del motor (camhook.cpp): en este cuadro no hay historia (el paso 2 ve q[21].z = 0 y arranca
    // del promedio de los vecinos) en vez de reproyectar con la camara de la toma anterior
    { static int s_cutSeen = 0; if (CamCutSince(s_cutSeen)) { ZeroBuffer(c, s_prev); g_cutUse[0]++; } }
    // guardar estado
    ID3D11RenderTargetView* oRT[8] = {}; ID3D11DepthStencilView* oDS = nullptr; c->OMGetRenderTargets(8, oRT, &oDS);
    UINT nvp = 8; D3D11_VIEWPORT ovp[8]; c->RSGetViewports(&nvp, ovp);
    ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr; ID3D11HullShader* oHS = nullptr; ID3D11DomainShader* oDSh = nullptr;
    c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr); c->HSGetShader(&oHS, nullptr, nullptr); c->DSGetShader(&oDSh, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL); D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS);
    ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDSS, &oRef);
    ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0; c->OMGetBlendState(&oBS, oBF, &oSM);
    ID3D11SamplerState* oS[2] = {}; c->PSGetSamplers(0, 2, oS);
    ID3D11ShaderResourceView* o120 = nullptr; c->PSGetShaderResources(120, 1, &o120);
    ID3D11Buffer* o3 = nullptr; c->PSGetConstantBuffers(3, 1, &o3);
    ID3D11ShaderResourceView* nulls[4] = {};
    c->VSSetShader(s_vs, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->RSSetState(s_rs); c->OMSetDepthStencilState(s_dss, 0); float bf[4] = {}; c->OMSetBlendState(s_bs, bf, 0xffffffff);
    ID3D11SamplerState* ss[2] = { s_sampP, s_sampL }; c->PSSetSamplers(0, 2, ss);
    D3D11_VIEWPORT vp{ 0, 0, (float)hw, (float)hh, 0, 1 }; c->RSSetViewports(1, &vp);
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->PSSetShaderResources(120, 1, &prm);
    c->PSSetConstantBuffers(2, 1, &scb); if (sys) c->PSSetConstantBuffers(0, 1, &sys);
    SsgiTimer* tm = (g_cfg.live[153][0] > 0.5f) ? SsgiTimerClaim(frame) : nullptr;
    if (tm) { c->Begin(tm->dis); c->End(tm->ts[0]); }
    // 1) SSGI de este cuadro -> tex0
    c->OMSetRenderTargets(1, &s_rtv[0], nullptr);
    ID3D11ShaderResourceView* sv0[4] = { depth, nrm, alb, dif }; c->PSSetShaderResources(0, 4, sv0);
    c->PSSetShader(s_ps, nullptr, 0); c->Draw(3, 0);
    if (tm) c->End(tm->ts[1]);
    // 2) acumulacion: tex0 + historia anterior -> historia nueva
    int hNew = 1 + s_ping, hOld = 1 + (1 - s_ping); s_ping = 1 - s_ping;
    c->PSSetShaderResources(0, 4, nulls); c->OMSetRenderTargets(1, &s_rtv[hNew], nullptr);
    D3D11_VIEWPORT vpF{ 0, 0, (float)fw, (float)fh, 0, 1 }; c->RSSetViewports(1, &vpF);
    ID3D11ShaderResourceView* sv1[3] = { s_srv[0], s_srv[hOld], depth }; c->PSSetShaderResources(0, 3, sv1);
    c->PSSetConstantBuffers(3, 1, &s_prev);
    c->PSSetShader(s_tp, nullptr, 0); c->Draw(3, 0);
    if (tm) { c->End(tm->ts[2]); c->End(tm->dis); }
    c->PSSetShaderResources(0, 4, nulls);
    // la camara de este cuadro queda como "anterior" para el proximo
    c->CopyResource(s_prev, scb);
    // restaurar
    c->OMSetRenderTargets(8, oRT, oDS); for (auto* r : oRT) if (r) r->Release(); if (oDS) oDS->Release();
    c->RSSetViewports(nvp, ovp);
    c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0); c->HSSetShader(oHS, nullptr, 0); c->DSSetShader(oDSh, nullptr, 0);
    if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release(); if (oHS) oHS->Release(); if (oDSh) oDSh->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release(); c->IASetPrimitiveTopology(oTopo);
    c->RSSetState(oRS); if (oRS) oRS->Release();
    c->OMSetDepthStencilState(oDSS, oRef); if (oDSS) oDSS->Release();
    c->OMSetBlendState(oBS, oBF, oSM); if (oBS) oBS->Release();
    c->PSSetSamplers(0, 2, oS); for (auto* s : oS) if (s) s->Release();
    c->PSSetShaderResources(0, 12, in);
    c->PSSetShaderResources(120, 1, &o120); if (o120) o120->Release();
    c->PSSetConstantBuffers(3, 1, &o3); if (o3) o3->Release();
    c->PSSetConstantBuffers(2, 1, &scb); scb->Release(); if (sys) { c->PSSetConstantBuffers(0, 1, &sys); sys->Release(); }
    rel();
    s_lastFrame = frame; s_last = s_srv[hNew]; s_ranFrame = frame; s_nRan++; if (editor) s_nEd++;
    s_edFullFrame = edFull ? frame : -100;
    return s_srv[hNew];
}

} // namespace fx
