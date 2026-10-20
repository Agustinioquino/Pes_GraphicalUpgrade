// PES21 FX - desgaste de la cancha en tiempo real (20/10), como en FC 24: un mapa de desgaste (R16, 1024x512, en uv de
// la cancha: 140 x 79 m) que la DLL va pintando con las posiciones de los jugadores durante el partido. Cada dibujo de
// short / medias (PS 5529...) copia la traslacion de m_world (cVSObject b5, fila 3) a un buffer de staging; en Present se
// leen las posiciones (2 cuadros de atraso), se estima la velocidad por el cuadro anterior y se dibujan pinceles
// gaussianos sumados sobre el mapa. El mapa se publica como recurso con nombre "pes_wear" (reglas: srv = 18 pes_wear en
// la base de la cancha, srv = 8 pes_wear en el VS de las hojas) y los shaders lo suman al desgaste del pasto v3.
//   x115 velocidad del desgaste, y115 radio del pincel (m), z115 pelado al maximo, w115 on/off. Se borra al empezar
//   otro partido (deteccion de "partido nuevo" del reloj del cesped).
#include "core.h"
#include <d3d11_1.h>
#include <atomic>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>

namespace fx {

static const int kMaxPlayers = 48, kStages = 3;
static ID3D11Buffer* s_stage[kStages] = {}; static int s_stageFrame[kStages] = { -1, -1, -1 }; static std::atomic<int> s_stageW{0};
static std::atomic<int> s_count{0};          // dibujos anotados en el cuadro actual
static int s_frame = 0;
static ID3D11Texture2D* s_tex = nullptr; static ID3D11RenderTargetView* s_rtv = nullptr; static ID3D11ShaderResourceView* s_srv = nullptr;
static ID3D11VertexShader* s_vs = nullptr; static ID3D11PixelShader* s_ps = nullptr; static bool s_tried = false;
static ID3D11Buffer* s_cb = nullptr; static ID3D11BlendState* s_bsAdd = nullptr; static ID3D11RasterizerState* s_rs = nullptr; static ID3D11DepthStencilState* s_dss = nullptr;
static std::vector<float> s_prev;           // posiciones del cuadro anterior (x, z)
static int s_matchGenSeen = -1; static bool s_cleared = false;
static double s_lastT = 0; static int s_logged = 0;

static void EnsureRes() {
    if (!g_dev.dev) return;
    for (int i = 0; i < kStages; i++) if (!s_stage[i]) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxPlayers * 16; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        g_dev.dev->CreateBuffer(&d, nullptr, &s_stage[i]);
    }
    if (!s_tex) {
        D3D11_TEXTURE2D_DESC td{}; td.Width = 1024; td.Height = 512; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R16_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (SUCCEEDED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_tex))) {
            g_dev.dev->CreateRenderTargetView(s_tex, nullptr, &s_rtv); g_dev.dev->CreateShaderResourceView(s_tex, nullptr, &s_srv);
            if (s_srv) NamedSetSRV("pes_wear", s_srv);
        }
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxPlayers * 16 + 16; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_dev.dev->CreateBuffer(&d, nullptr, &s_cb);
        D3D11_BLEND_DESC bd{}; auto& rt = bd.RenderTarget[0]; rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_ONE; rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_ONE; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        g_dev.dev->CreateBlendState(&bd, &s_bsAdd);
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; g_dev.dev->CreateRasterizerState(&rd, &s_rs);
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = FALSE; g_dev.dev->CreateDepthStencilState(&dd, &s_dss);
    }
    if (!s_tried) { s_tried = true;
        ID3DBlob* vb = CompileNamedShader(L"pes_wear_vs.hlsl", "vs_5_0"); ID3DBlob* pb = CompileNamedShader(L"pes_wear_ps.hlsl", "ps_5_0");
        if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_vs); vb->Release(); }
        if (pb) { g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_ps); pb->Release(); }
        Log("desgaste en vivo: shaders %s", (s_vs && s_ps) ? "listos" : "NO compilaron (ver errores)");
    }
}
void PitchWearReloadShaders() { if (s_vs) { s_vs->Release(); s_vs = nullptr; } if (s_ps) { s_ps->Release(); s_ps = nullptr; } s_tried = false; }

// en el dibujo del short / medias: copiar la traslacion de m_world (b5 del VS, fila 3 = bytes 112..127) al staging del cuadro
void PitchWearNotePlayer(ID3D11DeviceContext* c) {
    if (!g_dev.dev || g_cfg.live[115][3] < 0.5f) return;
    int w = s_stageW.load(); if (!s_stage[w]) return;
    int i = s_count.fetch_add(1); if (i >= kMaxPlayers) return;
    ID3D11Buffer* cb = nullptr; UINT first = 0, num = 0; ID3D11DeviceContext1* c1 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) { c1->VSGetConstantBuffers1(5, 1, &cb, &first, &num); c1->Release(); }
    else c->VSGetConstantBuffers(5, 1, &cb);
    if (!cb) return;
    D3D11_BUFFER_DESC d; cb->GetDesc(&d); UINT off = first * 16 + 112;
    if (d.ByteWidth >= off + 16) { D3D11_BOX bx = { off, 0, 0, off + 16, 1, 1 }; c->CopySubresourceRegion(s_stage[w], 0, (UINT)i * 16, 0, 0, cb, 0, &bx); }
    cb->Release();
}

void PitchWearPresent() {
    if (!g_dev.dev || !g_dev.imm) return;
    const float* P = g_cfg.live[115];
    if (P[3] < 0.5f) { s_count = 0; return; }
    EnsureRes(); if (!s_tex || !s_cb) return;
    // partido nuevo: borrar
    if (g_matchGen.load() != s_matchGenSeen || !s_cleared) { s_matchGenSeen = g_matchGen.load(); float z[4] = {}; g_dev.imm->ClearRenderTargetView(s_rtv, z); s_cleared = true; s_prev.clear(); }
    // anillo de 3: este cuadro escribio en w; el siguiente escribe en (w+1); se lee el de hace 2 cuadros = (w+1) (la GPU ya lo termino)
    int w = s_stageW.load(); int n = std::min(s_count.exchange(0), kMaxPlayers);
    s_stageFrame[w] = n > 0 ? s_frame : -1; s_frame++;
    int rd = (w + 1) % kStages;
    double t = Now(); float dt = (float)std::clamp(t - s_lastT, 0.001, 0.1); s_lastT = t;
    int pending = s_stageFrame[rd]; s_stageFrame[rd] = -1; s_stageW = rd;
    if (pending < 0 || !s_stage[rd]) return;
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_dev.imm->Map(s_stage[rd], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) return;
    const float* q = (const float*)m.pData; int cnt = 0; float pos[kMaxPlayers][2];
    for (int i = 0; i < kMaxPlayers && cnt < kMaxPlayers; i++) {
        float x = q[i * 4 + 0], y = q[i * 4 + 1], z = q[i * 4 + 2];
        if (!(fabsf(x) < 80.f && fabsf(z) < 50.f && fabsf(y) < 5.f)) continue;
        if (fabsf(x) < 0.01f && fabsf(z) < 0.01f) continue;
        bool dup = false; for (int k = 0; k < cnt; k++) if (fabsf(pos[k][0] - x) < 0.05f && fabsf(pos[k][1] - z) < 0.05f) { dup = true; break; }
        if (dup) continue;
        pos[cnt][0] = x; pos[cnt][1] = z; cnt++;
    }
    g_dev.imm->Unmap(s_stage[rd], 0);
    if (cnt == 0) return;
    // velocidad: el mas cercano del cuadro anterior
    float brush[kMaxPlayers][4]; int nb = 0;
    float rate = P[0] > 0.f ? P[0] : 1.0f, rad = std::clamp(P[1], 0.3f, 2.0f);
    for (int i = 0; i < cnt; i++) {
        float best = 1e9f; for (size_t k = 0; k + 1 < s_prev.size(); k += 2) { float dx = s_prev[k] - pos[i][0], dz = s_prev[k + 1] - pos[i][1]; float d2 = dx * dx + dz * dz; if (d2 < best) best = d2; }
        float speed = best < 1e8f ? sqrtf(best) / dt : 0.f;            // m/s
        if (speed > 15.f) speed = 0.f;                                     // salto de camara / teletransporte
        float k = (0.25f + 0.75f * std::min(speed / 7.f, 1.f)) * rate * dt * 0.08f;   // parado: poco; corriendo: mas
        brush[nb][0] = pos[i][0] / 140.f + 0.5f; brush[nb][1] = pos[i][1] / 79.f + 0.5f; brush[nb][2] = k; brush[nb][3] = rad; nb++;
    }
    s_prev.assign(&pos[0][0], &pos[0][0] + cnt * 2);
    if (!s_vs || !s_ps) return;
    // subir pinceles y dibujar
    D3D11_MAPPED_SUBRESOURCE mc; if (FAILED(g_dev.imm->Map(s_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) return;
    float* cbp = (float*)mc.pData; cbp[0] = (float)nb; cbp[1] = 140.f; cbp[2] = 79.f; cbp[3] = 0.f;
    memcpy(cbp + 4, brush, nb * 16); g_dev.imm->Unmap(s_cb, 0);
    ID3D11DeviceContext* c = g_dev.imm;
    ID3D11RenderTargetView* oRT[8] = {}; ID3D11DepthStencilView* oDS = nullptr; c->OMGetRenderTargets(8, oRT, &oDS);
    UINT nvp = 8; D3D11_VIEWPORT ovp[8]; c->RSGetViewports(&nvp, ovp);
    ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr;
    c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL); D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS); ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDSS, &oRef);
    ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0; c->OMGetBlendState(&oBS, oBF, &oSM);
    ID3D11Buffer* oCB = nullptr; c->VSGetConstantBuffers(0, 1, &oCB);
    c->OMSetRenderTargets(1, &s_rtv, nullptr); D3D11_VIEWPORT vp{ 0, 0, 1024, 512, 0, 1 }; c->RSSetViewports(1, &vp);
    c->VSSetShader(s_vs, nullptr, 0); c->PSSetShader(s_ps, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0);
    c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    c->RSSetState(s_rs); c->OMSetDepthStencilState(s_dss, 0); float bf[4] = {}; c->OMSetBlendState(s_bsAdd, bf, 0xffffffff);
    c->VSSetConstantBuffers(0, 1, &s_cb);
    c->DrawInstanced(4, (UINT)nb, 0, 0);
    // restaurar
    c->OMSetRenderTargets(8, oRT, oDS); for (auto* v : oRT) if (v) v->Release(); if (oDS) oDS->Release();
    c->RSSetViewports(nvp, ovp);
    c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0); if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release(); c->IASetPrimitiveTopology(oTopo);
    c->RSSetState(oRS); if (oRS) oRS->Release(); c->OMSetDepthStencilState(oDSS, oRef); if (oDSS) oDSS->Release();
    c->OMSetBlendState(oBS, oBF, oSM); if (oBS) oBS->Release();
    c->VSSetConstantBuffers(0, 1, &oCB); if (oCB) oCB->Release();
    if (s_logged < 3 && nb > 0) { s_logged++; Log("desgaste en vivo: %d jugadores pintados (ej. %.1f, %.1f)", nb, pos[0][0], pos[0][1]); }
}

} // namespace fx
