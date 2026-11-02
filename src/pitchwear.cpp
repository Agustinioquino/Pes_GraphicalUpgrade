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
#include <mutex>

namespace fx {

static const int kMaxPlayers = 48, kStages = 3;
static ID3D11Buffer* s_stage[kStages] = {}; static int s_stageFrame[kStages] = { -1, -1, -1 }; static std::atomic<int> s_stageW{0};
static std::atomic<int> s_count{0};          // dibujos anotados en el cuadro actual
static int s_frame = 0;
static ID3D11Texture2D* s_tex = nullptr; static ID3D11RenderTargetView* s_rtv = nullptr; static ID3D11ShaderResourceView* s_srv = nullptr;
static ID3D11VertexShader* s_vs = nullptr; static ID3D11PixelShader* s_ps = nullptr; static bool s_tried = false;
static ID3D11Buffer* s_cb = nullptr; static ID3D11BlendState* s_bsAdd = nullptr; static ID3D11RasterizerState* s_rs = nullptr; static ID3D11DepthStencilState* s_dss = nullptr;
static std::vector<float> s_prev;           // posiciones del cuadro anterior (x, z)
// (2/11) tanda 14 "reacciones" (pedido de Agustin: mas rastro, mas desgaste por los jugadores, pelota manchada). Fila 177:
//   x manchas de la pelota (verde / tierra / cal; 0 = no, 1 = natural), y frenadas y arranques marcan mas (0..2), z rastro de la
//   pelota segun su velocidad (0..2). El desgaste se duplica en la CPU (s_wearCpu, 256 x 128 celdas de 55 x 62 cm) para saber cuanta
//   tierra hay bajo la pelota sin leer la GPU; la pelota se reconoce entre los dibujos del PS de botines/pelota (aadffecf) porque la
//   traslacion de su m_world coincide con la capsula de la pelota del pasto (s_ball): el indice de ese dibujo queda como "la pelota"
//   y en sus dibujos se enlaza b12 (PESBallC: x 1, y verde, z tierra, w cal).
struct PlayerPrev { float x, z, vx, vz, skid; };
static std::vector<PlayerPrev> s_pl;        // jugadores del cuadro anterior con su velocidad (m/s) y cuanto frenaban (0..1)
static float s_wearCpu[128][256] = {};
static float s_stain[3] = {};               // verde, tierra, cal (0..1)
static ID3D11Buffer* s_ballCb = nullptr;    // b12 del PS de la pelota
static const int kBallSlots = 64;
static ID3D11Buffer* s_bdStage[kStages] = {}; static int s_bdFrame[kStages] = { -1, -1, -1 }; static std::atomic<int> s_bdW{0}; static std::atomic<int> s_bdCount{0};
static UINT s_bdIdx[kStages][kBallSlots] = {};   // indice (vertices) de cada dibujo anotado, por anillo
static std::atomic<UINT> s_ballIdx{0};      // indice de vertices del dibujo de la pelota (0 = aun no)
static int s_ballIdxVotes = 0; static int s_ballLogged = 0;
static void WearCpuAdd(float u, float v, float k, float radM) {
    // mismo pincel que pes_wear_ps.hlsl: k * exp(-r2 * 2.2), r en radios, hasta 1.5 radios
    float cu = u * 256.f, cv = v * 128.f, ru = radM / 140.f * 256.f, rv = radM / 79.f * 128.f;
    int u0 = std::max(0, (int)(cu - 1.5f * ru)), u1 = std::min(255, (int)(cu + 1.5f * ru) + 1), v0 = std::max(0, (int)(cv - 1.5f * rv)), v1 = std::min(127, (int)(cv + 1.5f * rv) + 1);
    for (int j = v0; j <= v1; j++) for (int i = u0; i <= u1; i++) {
        float dx = (i + 0.5f - cu) / std::max(ru, 0.01f), dy = (j + 0.5f - cv) / std::max(rv, 0.01f); float r2 = dx * dx + dy * dy;
        if (r2 < 2.25f) s_wearCpu[j][i] += k * expf(-r2 * 2.2f);
    }
}
static float WearCpuAt(float x, float z) {
    int i = std::clamp((int)((x / 140.f + 0.5f) * 256.f), 0, 255), j = std::clamp((int)((z / 79.f + 0.5f) * 128.f), 0, 127);
    return s_wearCpu[j][i];
}
// distancia (m) a la linea de cal mas cercana de una cancha de 105 x 68 centrada (x a lo largo, z a lo ancho)
static float SegDist(float px, float pz, float ax, float az, float bx, float bz) {
    float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz; float t = l2 > 1e-6f ? std::clamp(((px - ax) * dx + (pz - az) * dz) / l2, 0.f, 1.f) : 0.f;
    float qx = ax + dx * t - px, qz = az + dz * t - pz; return sqrtf(qx * qx + qz * qz);
}
static float LineDist(float x, float z) {
    const float L = 52.5f, W = 34.f; float d = 1e9f;
    auto seg = [&](float ax, float az, float bx, float bz) { d = std::min(d, SegDist(x, z, ax, az, bx, bz)); };
    seg(-L, -W, L, -W); seg(-L, W, L, W); seg(-L, -W, -L, W); seg(L, -W, L, W); seg(0, -W, 0, W);
    d = std::min(d, fabsf(sqrtf(x * x + z * z) - 9.15f));
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float gx = sgn * L, bx = sgn * (L - 16.5f), ax = sgn * (L - 5.5f), px = sgn * (L - 11.f);
        seg(gx, -20.16f, bx, -20.16f); seg(gx, 20.16f, bx, 20.16f); seg(bx, -20.16f, bx, 20.16f);
        seg(gx, -9.16f, ax, -9.16f); seg(gx, 9.16f, ax, 9.16f); seg(ax, -9.16f, ax, 9.16f);
        if (sgn * (x - bx) < 0.f) d = std::min(d, fabsf(sqrtf((x - px) * (x - px) + z * z) - 9.15f));   // semicirculo del area, fuera del area
    }
    return d;
}
static int s_matchGenSeen = -1; static bool s_cleared = false;
static double s_lastT = 0; static int s_logged = 0;
// ---- recortes de pasto (particulas en CPU, dibujadas con el PS de las hojas en la pasada del pasto)
struct Clip { float x, y, z, vx, vy, vz, age, life, size, rot; };
static const int kMaxClips = 2048;
static std::vector<Clip> s_clips;
static ID3D11Buffer* s_clipBuf = nullptr; static ID3D11ShaderResourceView* s_clipSRV = nullptr; static ID3D11VertexShader* s_clipVS = nullptr; static bool s_clipTried = false;
static std::atomic<int> s_clipCount{0}; static std::atomic<int> s_clipFrame{-1};
static float Rand01() { static uint32_t st = 0x9E3779B9u; st ^= st << 13; st ^= st >> 17; st ^= st << 5; return (st & 0xffffff) / 16777216.0f; }
static void ClipSimAndUpload(float dt) {
    if (!g_dev.dev || !g_dev.imm) return;
    if (!s_clipBuf) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxClips * 32; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_SHADER_RESOURCE; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = 16;
        if (SUCCEEDED(g_dev.dev->CreateBuffer(&d, nullptr, &s_clipBuf))) {
            D3D11_SHADER_RESOURCE_VIEW_DESC v = {}; v.Format = DXGI_FORMAT_UNKNOWN; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; v.Buffer.FirstElement = 0; v.Buffer.NumElements = kMaxClips * 2;
            g_dev.dev->CreateShaderResourceView(s_clipBuf, &v, &s_clipSRV);
        }
    }
    if (!s_clipTried) { s_clipTried = true; ID3DBlob* vb = CompileNamedShader(L"pes_clip_vs.hlsl", "vs_5_0");
        if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_clipVS); vb->Release(); }
        Log("recortes de pasto: shader %s", s_clipVS ? "listo" : "NO compilo (ver errores)"); }
    // viento de las nubes (z84 direccion, y84 velocidad)
    float ang = g_cfg.live[84][2] * 0.0174533f, wsp = std::min(std::max(g_cfg.live[84][1], 0.f), 60.f) * 0.03f;
    float wx = cosf(ang) * wsp, wz = sinf(ang) * wsp;
    size_t n = 0;
    for (size_t i = 0; i < s_clips.size(); i++) {
        Clip& c = s_clips[i];
        c.vy -= 7.0f * dt; float drag = 1.0f - 2.2f * dt; c.vx = c.vx * drag + wx * dt; c.vz = c.vz * drag + wz * dt; c.vy *= 1.0f - 1.2f * dt;
        c.x += c.vx * dt; c.y += c.vy * dt; c.z += c.vz * dt; c.age += dt; c.rot += 6.0f * dt;
        if (c.age < c.life && c.y > 0.004f) s_clips[n++] = c;
    }
    s_clips.resize(n);
    if (!s_clipBuf || !s_clipSRV) return;
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_dev.imm->Map(s_clipBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    float* q = (float*)m.pData;
    for (size_t i = 0; i < n; i++) { const Clip& c = s_clips[i]; float fade = std::min(1.0f, (c.life - c.age) * 4.0f);
        q[i * 8 + 0] = c.x; q[i * 8 + 1] = c.y; q[i * 8 + 2] = c.z; q[i * 8 + 3] = c.size * fade;
        q[i * 8 + 4] = c.vx; q[i * 8 + 5] = c.vy; q[i * 8 + 6] = c.vz; q[i * 8 + 7] = c.rot; }
    g_dev.imm->Unmap(s_clipBuf, 0);
    s_clipCount = (int)n;
}
// desde GrassOnDraw (estado del pasto puesto: G-buffer, profundidad, mezcla): un dibujo instanciado con nuestro VS y el PS de las hojas
void PitchClipsDraw(ID3D11DeviceContext* c, int frame) {
    int n = s_clipCount.load(); if (n <= 0 || !s_clipVS || !s_clipSRV || g_cfg.live[116][3] < 0.5f) return;
    s_clipFrame = frame;   // (26/10 d) "una vez por grabacion" lo decide GrassOnDraw (el cuadro de grabacion no identifica el de la GPU)
    ID3D11VertexShader* oVS = nullptr; c->VSGetShader(&oVS, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL);
    ID3D11ShaderResourceView* oT[2] = {}; c->VSGetShaderResources(120, 1, &oT[0]); c->VSGetShaderResources(122, 1, &oT[1]);
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->VSSetShaderResources(120, 1, &prm); c->VSSetShaderResources(122, 1, &s_clipSRV);
    c->VSSetShader(s_clipVS, nullptr, 0); c->IASetInputLayout(nullptr);
    c->DrawInstanced(6, (UINT)n, 0, 0);   // pasa por el gancho: PS de las hojas reemplazado + reglas (atlas t8/t9, t120)
    c->VSSetShader(oVS, nullptr, 0); if (oVS) oVS->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release();
    c->VSSetShaderResources(120, 1, &oT[0]); c->VSSetShaderResources(122, 1, &oT[1]); for (auto* v : oT) if (v) v->Release();
}
void PitchClipsReloadShaders() { if (s_clipVS) { s_clipVS->Release(); s_clipVS = nullptr; } s_clipTried = false; }

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
// ---------------------------------------------------------------- pasto aplastado temporal (20/10)
// Segundo mapa ("pes_flat", R16, mismas uv que el desgaste) que se apaga solo: cada cuadro se multiplica por exp(-dt/tau)
// y se suman pinceles donde estan los jugadores y donde rueda la pelota (con el rastro entre la posicion anterior y la
// actual). Las hojas lo leen para agacharse y abrirse hacia afuera del rastro y la base de la cancha para cambiar el grano
// (pisadas y rastro de la pelota que se ven y se recuperan en ~2 s). La pelota sale del dibujo del pasto del juego:
// cb4[3] y cb4[4] del VS de las hojas son la capsula de la pelota (posicion actual y anterior, radio 0.112 m).
//   136: x on | y fuerza | z recuperacion (s) | w fuerza de la pelota
static ID3D11Texture2D* s_flatTex = nullptr; static ID3D11RenderTargetView* s_flatRTV = nullptr; static ID3D11ShaderResourceView* s_flatSRV = nullptr;
static ID3D11VertexShader* s_fsVS = nullptr; static ID3D11PixelShader* s_zeroPS = nullptr; static bool s_flatTried = false;
static ID3D11BlendState* s_bsDecay = nullptr; static ID3D11Buffer* s_flatCB = nullptr; static ID3D11BlendState* s_bsAddR = nullptr; static ID3D11BlendState* s_bsAddG = nullptr;   // (27/10 n) pinceles al canal R (jugadores) o G (rastro de la pelota)
static ID3D11Buffer* s_ballStage[kStages] = {}; static int s_ballStageFrame[kStages] = { -1, -1, -1 }; static std::atomic<int> s_ballW{0}; static std::atomic<int> s_ballNoted{-1};
static float s_ball[3] = {}, s_ballPrev[3] = {}; static bool s_ballOk = false; static int s_flatFrame = 0; static double s_flatT = 0; static int s_flatMatch = -1;
static void EnsureFlat() {
    if (!g_dev.dev) return;
    if (!s_flatTex) {
        // (27/10 n) tanda 11: R = pisadas de los jugadores (se levanta en z136 s), G = rastro de la pelota (dura z329 s, 30 por defecto):
        // Agustin queria ver por donde fue la pelota; con 2 s de recuperacion no quedaba rastro
        D3D11_TEXTURE2D_DESC td{}; td.Width = 1024; td.Height = 512; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R16G16_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (SUCCEEDED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_flatTex))) {
            g_dev.dev->CreateRenderTargetView(s_flatTex, nullptr, &s_flatRTV); g_dev.dev->CreateShaderResourceView(s_flatTex, nullptr, &s_flatSRV);
            if (s_flatSRV) NamedSetSRV("pes_flat", s_flatSRV);
            if (s_flatRTV && g_dev.imm) { float z[4] = {}; g_dev.imm->ClearRenderTargetView(s_flatRTV, z); }
        }
        D3D11_BLEND_DESC bd{}; auto& rt = bd.RenderTarget[0]; rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_ZERO; rt.DestBlend = D3D11_BLEND_BLEND_FACTOR; rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ZERO; rt.DestBlendAlpha = D3D11_BLEND_BLEND_FACTOR; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        g_dev.dev->CreateBlendState(&bd, &s_bsDecay);
        { D3D11_BLEND_DESC ba{}; auto& r2 = ba.RenderTarget[0]; r2.BlendEnable = TRUE; r2.SrcBlend = D3D11_BLEND_ONE; r2.DestBlend = D3D11_BLEND_ONE; r2.BlendOp = D3D11_BLEND_OP_ADD;
          r2.SrcBlendAlpha = D3D11_BLEND_ONE; r2.DestBlendAlpha = D3D11_BLEND_ONE; r2.BlendOpAlpha = D3D11_BLEND_OP_ADD;
          r2.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED; g_dev.dev->CreateBlendState(&ba, &s_bsAddR);
          r2.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_GREEN; g_dev.dev->CreateBlendState(&ba, &s_bsAddG); }
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxPlayers * 16 + 16; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_dev.dev->CreateBuffer(&d, nullptr, &s_flatCB);
    }
    for (int i = 0; i < kStages; i++) if (!s_ballStage[i]) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 32; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &s_ballStage[i]); }
    if (!s_flatTried) { s_flatTried = true;
        ID3DBlob* vb = CompileNamedShader(L"pes_fs_vs.hlsl", "vs_5_0"); ID3DBlob* pb = CompileNamedShader(L"pes_zero_ps.hlsl", "ps_5_0");
        if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_fsVS); vb->Release(); }
        if (pb) { g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_zeroPS); pb->Release(); }
        Log("pasto aplastado: shaders %s", (s_fsVS && s_zeroPS) ? "listos" : "NO compilaron");
    }
}
// desde GrassOnDraw (una vez por cuadro): la capsula de la pelota (cb4[3], cb4[4] del VS de las hojas) al staging
void PitchBallNote(ID3D11DeviceContext* c, int frame) {
    if (!g_dev.dev || (g_cfg.live[136][0] < 0.5f && !(g_cfg.live[139][0] > 0.5f && g_cfg.live[139][2] > 0.001f))) return;
    s_ballNoted = frame;   // (26/10 d) una vez por grabacion (lo decide GrassOnDraw)
    int w = s_ballW.load(); if (!s_ballStage[w]) return;
    ID3D11Buffer* cb = nullptr; UINT first = 0, num = 0; ID3D11DeviceContext1* c1 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) { c1->VSGetConstantBuffers1(4, 1, &cb, &first, &num); c1->Release(); }
    else c->VSGetConstantBuffers(4, 1, &cb);
    if (!cb) return;
    D3D11_BUFFER_DESC d; cb->GetDesc(&d); UINT off = first * 16 + 48;
    if (d.ByteWidth >= off + 32) { D3D11_BOX bx = { off, 0, 0, off + 32, 1, 1 }; c->CopySubresourceRegion(s_ballStage[w], 0, 0, 0, 0, cb, 0, &bx); s_ballStageFrame[w] = frame; }
    cb->Release();
}
static void PitchFlatPresent() {
    const float* F = g_cfg.live[136];
    bool v5 = g_cfg.live[139][0] > 0.5f && g_cfg.live[139][2] > 0.001f;   // (20/10) pasto v5: las hojas se doblan con el mapa
    if ((F[0] < 0.5f && !v5) || !g_dev.imm) return;
    EnsureFlat(); if (!s_flatRTV || !s_fsVS || !s_zeroPS || !s_vs || !s_ps || !s_flatCB || !s_bsAddR || !s_bsAddG) return;
    double t = Now(); float dt = (float)std::clamp(t - s_flatT, 0.001, 0.1); s_flatT = t; s_flatFrame++;
    if (g_matchGen.load() != s_flatMatch) { s_flatMatch = g_matchGen.load(); float z[4] = {}; g_dev.imm->ClearRenderTargetView(s_flatRTV, z); s_ballOk = false; }
    // pelota: el staging mas viejo ya listo (anillo de 3)
    { int w = s_ballW.load(), rd = (w + 1) % kStages; s_ballW = rd;
      if (s_ballStageFrame[rd] >= 0 && s_ballStage[rd]) {
          D3D11_MAPPED_SUBRESOURCE m;
          if (SUCCEEDED(g_dev.imm->Map(s_ballStage[rd], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
              const float* q = (const float*)m.pData;
              bool ok = fabsf(q[0]) < 70.f && fabsf(q[2]) < 45.f && q[1] > -0.5f && q[1] < 20.f && (fabsf(q[0]) + fabsf(q[2]) > 0.01f);
              if (ok) { if (s_ballOk) { s_ballPrev[0] = s_ball[0]; s_ballPrev[1] = s_ball[1]; s_ballPrev[2] = s_ball[2]; } else { s_ballPrev[0] = q[0]; s_ballPrev[1] = q[1]; s_ballPrev[2] = q[2]; }
                        s_ball[0] = q[0]; s_ball[1] = q[1]; s_ball[2] = q[2]; s_ballOk = true; }
              g_dev.imm->Unmap(s_ballStage[rd], 0);
          }
          s_ballStageFrame[rd] = -1;
      } }
    float brush[kMaxPlayers][4]; int nb = 0; int nbP = 0;   // (27/10 n) los primeros nbP pinceles son de jugadores (canal R), el resto de la pelota (G)
    float k = std::clamp(F[1] > 0.f ? F[1] : 1.0f, 0.f, 2.f);
    // jugadores (ultimas posiciones conocidas del desgaste en vivo): pisada suave
    const float kAct = std::clamp(g_cfg.live[177][1], 0.f, 2.f);   // (2/11) frenadas: pisada mas fuerte y alargada hacia atras
    for (size_t i = 0; i < s_pl.size() && nb < kMaxPlayers - 16; i++) {
        const PlayerPrev& p = s_pl[i]; float sp = sqrtf(p.vx * p.vx + p.vz * p.vz);
        brush[nb][0] = p.x / 140.f + 0.5f; brush[nb][1] = p.z / 79.f + 0.5f; brush[nb][2] = 2.5f * dt * k * (1.f + 2.5f * p.skid * kAct); brush[nb][3] = 0.45f; nb++;
        if (p.skid > 0.2f && kAct > 0.01f && sp > 1.f && nb < kMaxPlayers - 16) {   // marca de frenada: 2 pinceles detras, en la direccion de la que venia
            for (int e = 1; e <= 2 && nb < kMaxPlayers - 16; e++) { float bx = p.x - p.vx / sp * 0.35f * e, bz = p.z - p.vz / sp * 0.35f * e;
                brush[nb][0] = bx / 140.f + 0.5f; brush[nb][1] = bz / 79.f + 0.5f; brush[nb][2] = 2.0f * dt * k * p.skid * kAct; brush[nb][3] = 0.35f; nb++; }
        }
    }
    nbP = nb;
    // pelota en el piso: rastro entre la posicion anterior y la actual
    if (s_ballOk) {
        float onGround = std::clamp(1.0f - (s_ball[1] - 0.12f) / 0.25f, 0.0f, 1.0f);
        // (27/10 n) pique: la pelota venia del aire (> 40 cm) y toca el piso: marca redonda mas fuerte
        if (onGround > 0.5f && s_ballPrev[1] > 0.4f && nb < kMaxPlayers) {
            brush[nb][0] = s_ball[0] / 140.f + 0.5f; brush[nb][1] = s_ball[2] / 79.f + 0.5f; brush[nb][2] = 0.6f * k * (F[3] > 0.f ? F[3] : 1.0f); brush[nb][3] = 0.26f; nb++;
        }
        float dx = s_ball[0] - s_ballPrev[0], dz = s_ball[2] - s_ballPrev[2]; float dist = sqrtf(dx * dx + dz * dz);
        if (dist > 4.0f) dist = 0.0f;   // salto de camara / saque
        int steps = std::clamp((int)(dist / 0.08f) + 1, 1, 15);
        float kb = (F[3] > 0.f ? F[3] : 1.0f) * k * onGround;
        { const float kv = std::clamp(g_cfg.live[177][2], 0.f, 2.f); float spd = std::min(dist / dt / 12.f, 1.5f);   // (2/11) rapida: aplasta mas; lenta: menos
          kb *= std::max(0.25f, 1.f + kv * (spd - 0.5f)); }
        // (2/11) manchas de la pelota (fila 177 x). Agustin: PROGRESIVAS y MUY SUTILES, con los minutos de partido y segun la humedad de la
        // cancha; nada por contacto. La pelota solo "recuerda" por donde rueda (promedios lentos de tierra bajo ella y de cruces de cal) y
        // la mancha crece con el reloj: verde ~ 25 min, tierra ~ 30 min (x2 con cancha humeda), cal ~ 20 min (menos con humedad)
        { const float kS = std::clamp(g_cfg.live[177][0], 0.f, 2.f);
          if (kS > 0.001f) {
              static float wearEma = 0.f, lineEma = 0.f;
              float roll = (dist > 0.005f && dist < 4.f) ? onGround : 0.f;
              if (roll > 0.f) { float a = std::min(1.f, dt / 20.f);   // promedios de ~20 s de rodado
                  wearEma += (std::clamp(WearCpuAt(s_ball[0], s_ball[2]), 0.f, 1.f) - wearEma) * a;
                  lineEma += ((LineDist(s_ball[0], s_ball[2]) < 0.12f ? 1.f : 0.f) - lineEma) * a; }
              float mins = std::max(g_cfg.live[10][0], 0.f);                   // minutos reales de partido (0 fuera del partido)
              float wet = g_cfg.live[114][0] > 0.5f && g_cfg.live[114][0] < 1.5f ? 1.f : 0.f, dry = g_cfg.live[114][0] > 1.5f ? 1.f : 0.f;
              float tg = 0.30f * (1.f - expf(-mins / 25.f)) * (1.f + 0.6f * wet);
              float td = 0.35f * (1.f - expf(-mins / 30.f)) * (0.3f + 0.7f * wearEma) * (1.f + 1.5f * wet + 0.3f * dry);
              float tc = 0.20f * (1.f - expf(-mins / 20.f)) * (0.2f + 0.8f * std::min(lineEma * 6.f, 1.f)) * (1.f - 0.7f * wet);
              float a2 = std::min(1.f, dt / 8.f);                                // se acerca al objetivo en ~8 s: sin saltos
              s_stain[0] += (tg - s_stain[0]) * a2; s_stain[1] += (td - s_stain[1]) * a2; s_stain[2] += (tc - s_stain[2]) * a2;
              if (mins < 0.01f) { wearEma *= 0.99f; lineEma *= 0.99f; }
              for (float& v : s_stain) v = std::clamp(v, 0.f, 1.f);
          } else s_stain[0] = s_stain[1] = s_stain[2] = 0.f; }
        for (int sIdx = 0; sIdx < steps && nb < kMaxPlayers; sIdx++) {
            float a = steps > 1 ? (float)sIdx / (steps - 1) : 1.0f;
            float bx = s_ballPrev[0] + dx * a, bz = s_ballPrev[2] + dz * a;
            brush[nb][0] = bx / 140.f + 0.5f; brush[nb][1] = bz / 79.f + 0.5f; brush[nb][2] = std::min(0.35f, 0.08f + 6.0f * dt) * kb; brush[nb][3] = 0.18f; nb++;
        }
        // pelota rapida al ras: algunos recortes de pasto
        float sp = dist / dt;
        if (onGround > 0.5f && sp > 9.0f && sp < 45.0f && g_cfg.live[116][3] > 0.5f && s_clips.size() < kMaxClips && Rand01() < 0.6f) {
            Clip cp; cp.x = s_ball[0]; cp.z = s_ball[2]; cp.y = 0.02f; float ix = dist > 0 ? dx / dist : 0, iz = dist > 0 ? dz / dist : 0;
            cp.vx = ix * (1.0f + Rand01() * 2.0f) + (Rand01() - 0.5f); cp.vz = iz * (1.0f + Rand01() * 2.0f) + (Rand01() - 0.5f); cp.vy = 1.0f + 2.0f * Rand01();
            cp.age = 0; cp.life = 0.4f + 0.5f * Rand01(); cp.size = 0.02f * (0.6f + 0.8f * Rand01()); cp.rot = Rand01() * 6.2832f; s_clips.push_back(cp);
        }
    }
    ID3D11DeviceContext* c = g_dev.imm;
    ID3D11RenderTargetView* oRT[8] = {}; ID3D11DepthStencilView* oDS = nullptr; c->OMGetRenderTargets(8, oRT, &oDS);
    UINT nvp = 8; D3D11_VIEWPORT ovp[8]; c->RSGetViewports(&nvp, ovp);
    ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr;
    c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL); D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS); ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDSS, &oRef);
    ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0; c->OMGetBlendState(&oBS, oBF, &oSM);
    ID3D11Buffer* oCB = nullptr; c->VSGetConstantBuffers(0, 1, &oCB);
    c->OMSetRenderTargets(1, &s_flatRTV, nullptr); D3D11_VIEWPORT vp{ 0, 0, 1024, 512, 0, 1 }; c->RSSetViewports(1, &vp);
    c->GSSetShader(nullptr, nullptr, 0); c->IASetInputLayout(nullptr);
    c->RSSetState(s_rs); c->OMSetDepthStencilState(s_dss, 0);
    // 1) se apaga de a poco: dest *= exp(-dt / tau)
    float tau = std::clamp(F[2] > 0.1f ? F[2] : 2.0f, 0.3f, 10.0f); float f = expf(-dt / tau);
    // (27/10 n) el rastro de la pelota (canal G) se desvanece en z329 s (0 = 30)
    float tauB = std::clamp(g_cfg.live[329][2] > 0.5f ? g_cfg.live[329][2] : 30.0f, 1.0f, 300.0f); float fB = expf(-dt / tauB); float bfD[4] = { f, fB, fB, fB };
    c->VSSetShader(s_fsVS, nullptr, 0); c->PSSetShader(s_zeroPS, nullptr, 0); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->OMSetBlendState(s_bsDecay, bfD, 0xffffffff); c->Draw(3, 0);
    // 2) pinceles nuevos (suma)
    if (nb > 0) {
        D3D11_MAPPED_SUBRESOURCE mc;
        if (SUCCEEDED(c->Map(s_flatCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) {
            float* cbp = (float*)mc.pData; cbp[0] = (float)nb; cbp[1] = 140.f; cbp[2] = 79.f; cbp[3] = 0.f; memcpy(cbp + 4, brush, nb * 16); c->Unmap(s_flatCB, 0);
            float bf[4] = {};
            c->VSSetShader(s_vs, nullptr, 0); c->PSSetShader(s_ps, nullptr, 0); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            c->VSSetConstantBuffers(0, 1, &s_flatCB);
            // (27/10 n) jugadores al canal R, pelota al canal G (el pincel escribe los 4 canales; la mascara elige)
            // (SV_InstanceID arranca en 0 en cada dibujo: el primer pincel de la pelota va en hdr.w y el VS lo suma)
            if (nbP > 0) { c->OMSetBlendState(s_bsAddR, bf, 0xffffffff); c->DrawInstanced(4, (UINT)nbP, 0, 0); }
            if (nb > nbP) {
                if (SUCCEEDED(c->Map(s_flatCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) {
                    float* cb2 = (float*)mc.pData; cb2[0] = (float)nb; cb2[1] = 140.f; cb2[2] = 79.f; cb2[3] = (float)nbP; memcpy(cb2 + 4, brush, nb * 16); c->Unmap(s_flatCB, 0);
                    c->OMSetBlendState(s_bsAddG, bf, 0xffffffff); c->DrawInstanced(4, (UINT)(nb - nbP), 0, 0);
                }
            }
        }
    }
    c->OMSetRenderTargets(8, oRT, oDS); for (auto* v : oRT) if (v) v->Release(); if (oDS) oDS->Release();
    c->RSSetViewports(nvp, ovp);
    c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0); if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release(); c->IASetPrimitiveTopology(oTopo);
    c->RSSetState(oRS); if (oRS) oRS->Release(); c->OMSetDepthStencilState(oDSS, oRef); if (oDSS) oDSS->Release();
    c->OMSetBlendState(oBS, oBF, oSM); if (oBS) oBS->Release();
    c->VSSetConstantBuffers(0, 1, &oCB); if (oCB) oCB->Release();
    static int logged = 0; if (logged < 2 && s_ballOk) { logged++; Log("pasto aplastado: pelota en (%.1f, %.2f, %.1f), %d pinceles", s_ball[0], s_ball[1], s_ball[2], nb); }
}

void PitchClipsReloadShaders();
void PitchWearReloadShaders() { if (s_vs) { s_vs->Release(); s_vs = nullptr; } if (s_ps) { s_ps->Release(); s_ps = nullptr; } s_tried = false; if (s_fsVS) { s_fsVS->Release(); s_fsVS = nullptr; } if (s_zeroPS) { s_zeroPS->Release(); s_zeroPS = nullptr; } s_flatTried = false; PitchClipsReloadShaders(); }

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

// (2/11) desde BeforeDraw, en cada dibujo del PS de botines / pelota (aadffecf): anota la traslacion de su m_world (b5 fila 3) con el
// indice del dibujo para reconocer la pelota en Present; devuelve el b12 a enlazar si este dibujo ya es "la pelota" (con manchas) o nullptr
ID3D11Buffer* PitchBallDrawNote(ID3D11DeviceContext* c, UINT verts) {
    if (!g_dev.dev || g_cfg.live[177][0] < 0.001f || verts == 0) return nullptr;
    UINT bi = s_ballIdx.load(std::memory_order_relaxed);
    if (bi == 0 || s_ballIdxVotes < 3) {   // todavia buscando: anotar
        int w = s_bdW.load(); if (s_bdStage[w]) { int i = s_bdCount.fetch_add(1);
            if (i < kBallSlots) { ID3D11Buffer* cb = nullptr; UINT first = 0, num = 0; ID3D11DeviceContext1* c1 = nullptr;
                if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) { c1->VSGetConstantBuffers1(5, 1, &cb, &first, &num); c1->Release(); } else c->VSGetConstantBuffers(5, 1, &cb);
                if (cb) { D3D11_BUFFER_DESC d; cb->GetDesc(&d); UINT off = first * 16 + 112;
                    if (d.ByteWidth >= off + 16) { D3D11_BOX bx = { off, 0, 0, off + 16, 1, 1 }; c->CopySubresourceRegion(s_bdStage[w], 0, (UINT)i * 16, 0, 0, cb, 0, &bx); s_bdIdx[w][i] = verts; }
                    cb->Release(); } } }
    }
    return (bi != 0 && verts == bi) ? s_ballCb : nullptr;
}
static void BallDrawPresent() {
    if (!g_dev.dev || !g_dev.imm) return;
    for (int i = 0; i < kStages; i++) if (!s_bdStage[i]) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 16 * kBallSlots; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &s_bdStage[i]); }
    if (!s_ballCb) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 16; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; g_dev.dev->CreateBuffer(&d, nullptr, &s_ballCb); }
    // publicar las manchas (b12: x 1 pelota, y verde, z tierra, w cal), escaladas por x177
    if (s_ballCb) { const float kS = std::clamp(g_cfg.live[177][0], 0.f, 2.f); float v[4] = { 1.f, std::min(1.f, s_stain[0] * kS), std::min(1.f, s_stain[1] * kS), std::min(1.f, s_stain[2] * kS) }; g_dev.imm->UpdateSubresource(s_ballCb, 0, nullptr, v, 0, 0); }
    // anillo de 3 como el desgaste: leer el de hace 2 cuadros y comparar con la pelota del pasto
    int w = s_bdW.load(); int n = std::min(s_bdCount.exchange(0), kBallSlots); s_bdFrame[w] = n > 0 ? 1 : -1;
    int rd = (w + 1) % kStages; int pending = s_bdFrame[rd]; s_bdFrame[rd] = -1; s_bdW = rd;
    if (pending < 0 || !s_bdStage[rd] || !s_ballOk) return;
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_dev.imm->Map(s_bdStage[rd], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) return;
    const float* q = (const float*)m.pData; UINT found = 0; int nf = 0;
    for (int i = 0; i < kBallSlots; i++) { if (!s_bdIdx[rd][i]) continue; float dx = q[i * 4] - s_ball[0], dy = q[i * 4 + 1] - s_ball[1], dz = q[i * 4 + 2] - s_ball[2];
        if (dx * dx + dy * dy + dz * dz < 0.3f * 0.3f) { found = s_bdIdx[rd][i]; nf++; } }
    g_dev.imm->Unmap(s_bdStage[rd], 0); memset(s_bdIdx[rd], 0, sizeof s_bdIdx[rd]);
    if (found) { UINT cur = s_ballIdx.load(); if (cur == found) s_ballIdxVotes = std::min(s_ballIdxVotes + 1, 10); else { s_ballIdx = found; s_ballIdxVotes = 1; }
        if (s_ballLogged < 2 && s_ballIdxVotes == 3) { s_ballLogged++; Log("pelota: dibujo reconocido (indice %u, %d dibujos cerca de la capsula); manchas verde %.2f tierra %.2f cal %.2f", found, nf, s_stain[0], s_stain[1], s_stain[2]); } }
}
void PitchWearPresent() {
    if (!g_dev.dev || !g_dev.imm) return;
    EnsureRes();
    PitchFlatPresent();   // (20/10) pasto aplastado temporal (jugadores + pelota)
    BallDrawPresent();    // (2/11) reconocer el dibujo de la pelota y publicar sus manchas
    const float* P = g_cfg.live[115];
    if (P[3] < 0.5f) { s_count = 0; return; }
    EnsureRes(); if (!s_tex || !s_cb) return;
    // partido nuevo: borrar
    if (g_matchGen.load() != s_matchGenSeen || !s_cleared) { s_matchGenSeen = g_matchGen.load(); float z[4] = {}; g_dev.imm->ClearRenderTargetView(s_rtv, z); s_cleared = true; s_prev.clear(); s_pl.clear();
        memset(s_wearCpu, 0, sizeof s_wearCpu); s_stain[0] = s_stain[1] = s_stain[2] = 0.f; }
    // anillo de 3: este cuadro escribio en w; el siguiente escribe en (w+1); se lee el de hace 2 cuadros = (w+1) (la GPU ya lo termino)
    int w = s_stageW.load(); int n = std::min(s_count.exchange(0), kMaxPlayers);
    s_stageFrame[w] = n > 0 ? s_frame : -1; s_frame++;
    int rd = (w + 1) % kStages;
    double t = Now(); float dt = (float)std::clamp(t - s_lastT, 0.001, 0.1); s_lastT = t;
    ClipSimAndUpload(dt);
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
    std::vector<PlayerPrev> plNow; plNow.reserve(cnt);
    const float kAct = std::clamp(g_cfg.live[177][1], 0.f, 2.f);   // (2/11) frenadas y arranques desgastan mas (y arrastran la marca)
    for (int i = 0; i < cnt; i++) {
        float best = 1e9f; int bi = -1; for (size_t k = 0; k < s_pl.size(); k++) { float dx = s_pl[k].x - pos[i][0], dz = s_pl[k].z - pos[i][1]; float d2 = dx * dx + dz * dz; if (d2 < best) { best = d2; bi = (int)k; } }
        float speed = best < 1e8f ? sqrtf(best) / dt : 0.f;            // m/s
        if (speed > 15.f) { speed = 0.f; bi = -1; }                       // salto de camara / teletransporte
        PlayerPrev pn{ pos[i][0], pos[i][1], 0.f, 0.f, 0.f };
        if (bi >= 0 && speed > 0.f) { pn.vx = (pos[i][0] - s_pl[bi].x) / dt; pn.vz = (pos[i][1] - s_pl[bi].z) / dt;
            float spPrev = sqrtf(s_pl[bi].vx * s_pl[bi].vx + s_pl[bi].vz * s_pl[bi].vz);
            float acc = (speed - spPrev) / dt;                              // m/s2 (negativo = frena)
            float skid = std::clamp((-acc - 5.f) / 12.f, 0.f, 1.f) * std::min(spPrev / 5.f, 1.f) + std::clamp((acc - 8.f) / 12.f, 0.f, 0.6f);
            pn.skid = std::max(skid, s_pl[bi].skid * 0.85f); }            // la marca dura unos cuadros
        float k = (0.25f + 0.75f * std::min(speed / 7.f, 1.f)) * rate * dt * 0.08f * (1.f + 3.f * pn.skid * kAct);   // parado: poco; corriendo: mas; frenando: mucho
        brush[nb][0] = pos[i][0] / 140.f + 0.5f; brush[nb][1] = pos[i][1] / 79.f + 0.5f; brush[nb][2] = k; brush[nb][3] = rad; nb++;
        WearCpuAdd(brush[nb - 1][0], brush[nb - 1][1], k, rad);
        if (pn.skid > 0.2f && kAct > 0.01f && speed > 1.f && nb < kMaxPlayers) {   // arrastre detras del jugador que frena
            float ux = pn.vx / speed, uz = pn.vz / speed, bx = pos[i][0] - ux * 0.4f, bz = pos[i][1] - uz * 0.4f;
            brush[nb][0] = bx / 140.f + 0.5f; brush[nb][1] = bz / 79.f + 0.5f; brush[nb][2] = k * 0.7f; brush[nb][3] = rad * 0.8f; nb++;
            WearCpuAdd(brush[nb - 1][0], brush[nb - 1][1], k * 0.7f, rad * 0.8f);
        }
        plNow.push_back(pn);
    }
    // recortes de pasto que vuelan (20/10): se emiten detras de los jugadores que corren (x116 cantidad, y116 tamano cm, w116 on)
    { const float* C = g_cfg.live[116];
      if (C[3] > 0.5f) {
        float amount = C[0] > 0.f ? C[0] : 1.0f, size = (C[1] > 0.f ? C[1] : 3.0f) * 0.01f;
        for (int i = 0; i < cnt; i++) {
            float best = 1e9f, bx = 0, bz = 0; for (size_t k = 0; k + 1 < s_prev.size(); k += 2) { float dx = pos[i][0] - s_prev[k], dz = pos[i][1] - s_prev[k + 1]; float d2 = dx * dx + dz * dz; if (d2 < best) { best = d2; bx = dx; bz = dz; } }
            float speed = best < 1e8f ? sqrtf(best) / dt : 0.f; if (speed < 2.5f || speed > 15.f) continue;
            float dirx = bx / (speed * dt + 1e-6f), dirz = bz / (speed * dt + 1e-6f);
            float want = amount * std::min(speed / 7.f, 1.5f) * 1.5f; int nEmit = (int)want; if (Rand01() < want - nEmit) nEmit++;
            for (int e = 0; e < nEmit && s_clips.size() < kMaxClips; e++) {
                Clip cp; float r1 = Rand01(), r2 = Rand01(), r3 = Rand01();
                cp.x = pos[i][0] - dirx * (0.1f + 0.3f * r1) + (r2 - 0.5f) * 0.3f; cp.z = pos[i][1] - dirz * (0.1f + 0.3f * r1) + (r3 - 0.5f) * 0.3f; cp.y = 0.03f;
                cp.vx = -dirx * (0.4f + 1.4f * Rand01()) + (Rand01() - 0.5f) * 1.0f; cp.vz = -dirz * (0.4f + 1.4f * Rand01()) + (Rand01() - 0.5f) * 1.0f; cp.vy = 1.2f + 2.6f * Rand01();
                cp.age = 0.f; cp.life = 0.5f + 0.7f * Rand01(); cp.size = size * (0.6f + 0.8f * Rand01()); cp.rot = Rand01() * 6.2832f;
                s_clips.push_back(cp);
            }
        }
      } else s_clips.clear(); }
    s_prev.assign(&pos[0][0], &pos[0][0] + cnt * 2); s_pl.swap(plNow);
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
