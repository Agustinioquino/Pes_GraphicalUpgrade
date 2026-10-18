// PES21 FX - pasto extendido: pasto 3D en toda la cancha.
// El juego dibuja parcelas de pasto de 0.5 m (una malla con ~100 variantes de densidad) solo hasta ~15 m de la
// camara. Cuando el juego dibuja su primera parcela del cuadro (todo el estado ya esta puesto: IB/VB, shaders,
// targets, cbuffers), se emiten parcelas propias desde x58 "desde" hasta el fondo de la cancha, con la variante mas
// liviana que el juego uso y escaladas desde nuestro VS (498b...): una parcela cubre 1.5 / 3 / 6 m segun la distancia.
// Solo cambia el cbuffer de objeto (b5) por dibujo (un buffer dinamico grande + VSSetConstantBuffers1 con offset);
// al terminar se restaura el del juego. Descarte por frustum con la matriz del cuadro anterior (lectura de cb2).
//   w58 on/off, x58 densidad, y58 desde (m), z58 hasta (m); x59 alto de las hojas, y59 ancho de las hojas
#include "core.h"
#include <d3d11_1.h>
#include <atomic>
#include <cmath>
#include <vector>

namespace fx {

static const uint64_t kGrassVS = 0x498b30bad256e8edULL;
static const int kMaxPatches = 4096;

static std::atomic<int> s_frame{0};
static std::atomic<int> s_emittedFrame{-1};
static std::atomic<UINT> s_lodCount{0xffffffff}; static UINT s_lodStart = 0; static INT s_lodBase = 0;
static ID3D11Buffer* s_cb = nullptr;
static ID3D11Buffer* s_cb2Stage[3] = {}; static int s_stageW = 0;
static std::atomic<int> s_stageCopied{-1};
static float s_pv[16]; static float s_eye[3]; static bool s_camValid = false;
static int s_lastCount = 0; static double s_lastMs = 0;
int g_grassPatchesLastFrame = 0;
UINT g_bbW = 0, g_bbH = 0;

static void EnsureBuffers() {
    if (!g_dev.dev) return;
    if (!s_cb) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = 288; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(g_dev.dev->CreateBuffer(&d, nullptr, &s_cb))) { Log("pasto extendido: no se pudo crear el cbuffer"); s_cb = nullptr; }
    }
    for (int i = 0; i < 3; i++) if (!s_cb2Stage[i]) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = 480; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        g_dev.dev->CreateBuffer(&d, nullptr, &s_cb2Stage[i]);
    }
}

// la camara del cuadro anterior: se lee en Present el staging que se copio durante el cuadro
void GrassPresent() {
    int f = s_frame.fetch_add(1) + 1;
    int copied = s_stageCopied.load();
    if (copied >= 0 && g_dev.imm && s_cb2Stage[copied]) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_dev.imm->Map(s_cb2Stage[copied], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
            const float* q = (const float*)m.pData;
            for (int i = 0; i < 16; i++) s_pv[i] = q[i];
            s_eye[0] = q[80]; s_eye[1] = q[81]; s_eye[2] = q[82];
            s_camValid = true;
            g_dev.imm->Unmap(s_cb2Stage[copied], 0);
        }
        s_stageCopied = -1;
    }
    g_grassPatchesLastFrame = s_lastCount;
    (void)f;
}

struct Patch { float x, z, cell; int rot; };

static inline bool Visible(float x, float z, float r) {
    if (!s_camValid) return true;
    // clip = PV * (x,0,z,1); filas de m_projectionView tal cual las lee el VS (cb2[0..3])
    const float* M = s_pv;
    float cx = M[0] * x + M[2] * z + M[3];
    float cy = M[4] * x + M[6] * z + M[7];
    float cw = M[12] * x + M[14] * z + M[15];
    // radio de la celda proyectado (aprox.): r * escala de proyeccion / w
    float rw = r * (fabsf(M[0]) + fabsf(M[2]) + fabsf(M[4]) + fabsf(M[6])) * 0.7f;
    if (cw < -r) return false;
    float w = (std::max)(cw, 0.05f);
    if (cx < -w - rw || cx > w + rw) return false;
    if (cy < -w - rw || cy > w + rw) return false;
    return true;
}

static void BuildPatches(std::vector<Patch>& out, float from, float to, float dens) {
    out.clear();
    const float halfL = 52.5f + 1.0f, halfW = 34.0f + 1.0f;
    float ex = s_eye[0], ez = s_eye[2];
    struct Ring { float r0, r1, cell; } rings[3] = { { from, 30.0f, 1.5f / dens }, { 30.0f, 60.0f, 3.0f / dens }, { 60.0f, to, 6.0f / dens } };
    for (auto& rg : rings) {
        if (rg.r1 <= rg.r0) continue;
        float cell = (std::max)(rg.cell, 0.5f);
        // recorrer la grilla de la cancha alineada al origen; solo las celdas cuyo centro cae en el anillo
        float r1sq = rg.r1 * rg.r1, r0sq = rg.r0 * rg.r0;
        int nx = (int)ceilf(2 * halfL / cell), nz = (int)ceilf(2 * halfW / cell);
        for (int iz = 0; iz < nz; iz++) {
            float z = -halfW + (iz + 0.5f) * cell;
            for (int ix = 0; ix < nx; ix++) {
                float x = -halfL + (ix + 0.5f) * cell;
                float dx = x - ex, dz = z - ez, d2 = dx * dx + dz * dz;
                if (d2 < r0sq || d2 >= r1sq) continue;
                if (!Visible(x, z, cell * 0.75f)) continue;
                int rot = ((ix * 7 + iz * 13) ^ (ix * iz)) & 3;
                out.push_back({ x, z, cell, rot });
                if ((int)out.size() >= kMaxPatches) return;
            }
        }
    }
}

// se llama desde el gancho de DrawIndexed antes de cada dibujo con el VS del pasto
void GrassOnDraw(ID3D11DeviceContext* c, const ShaderEntry* vs, UINT count, UINT start, INT base) {
    if (!vs || vs->hash != kGrassVS || g_cfg.bypass || !g_dev.dev) return;
    if (!vs->repl) return;                      // hace falta nuestro VS (lee la escala en cb5[8])
    // variante mas liviana que usa el juego (menos indices)
    UINT prev = s_lodCount.load();
    while (count < prev && !s_lodCount.compare_exchange_weak(prev, count)) {}
    if (count <= s_lodCount.load()) { s_lodStart = start; s_lodBase = base; }
    const float* p58 = g_cfg.live[58]; const float* p59 = g_cfg.live[59];
    if (p58[3] < 0.5f) return;
    // solo la pasada principal (G-buffer del tamano de la pantalla): el pasto tambien se dibuja para camaras secundarias
    if (g_bbW) {
        ID3D11RenderTargetView* rtv = nullptr; c->OMGetRenderTargets(1, &rtv, nullptr);
        if (!rtv) return;
        ID3D11Resource* res = nullptr; rtv->GetResource(&res); rtv->Release();
        ID3D11Texture2D* t = nullptr; UINT w = 0;
        if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); w = d.Width; t->Release(); }
        if (res) res->Release();
        if (w != g_bbW) return;
    }
    int f = s_frame.load();
    int exp = s_emittedFrame.load();
    if (exp == f) return;
    if (!s_emittedFrame.compare_exchange_strong(exp, f)) return;   // otro contexto ya emitio este cuadro
    EnsureBuffers();
    if (!s_cb) return;
    // copiar cb2 (camara) a un staging para el cuadro que viene
    { ID3D11Buffer* cb2 = nullptr; c->VSGetConstantBuffers(2, 1, &cb2);
      if (cb2) { D3D11_BUFFER_DESC d; cb2->GetDesc(&d);
        if (d.ByteWidth >= 480 && s_stageCopied.load() < 0) { int w = s_stageW; s_stageW = (s_stageW + 1) % 3;
          D3D11_BOX b = { 0, 0, 0, 480, 1, 1 }; c->CopySubresourceRegion(s_cb2Stage[w], 0, 0, 0, 0, cb2, 0, &b); s_stageCopied = w; }
        cb2->Release(); } }
    if (s_lodCount.load() == 0xffffffff) return;
    float dens = p58[0] > 0.05f ? p58[0] : 1.0f;
    float from = p58[1] > 1.0f ? p58[1] : 14.0f;
    float to = p58[2] > from ? p58[2] : 120.0f;
    float tall = p59[0] > 0.05f ? p59[0] : 1.3f;
    float wide = p59[1] >= 0.0f ? p59[1] : 1.0f;
    static std::vector<Patch> patches; BuildPatches(patches, from, to, dens);
    s_lastCount = (int)patches.size();
    if (patches.empty()) return;
    // dibujar: por parcela se actualiza nuestro cb5 (UpdateSubresource respeta el orden en el contexto) y se dibuja
    ID3D11Buffer* old = nullptr; c->VSGetConstantBuffers(5, 1, &old);
    ID3D11DeviceContext1* c1 = nullptr; UINT oldFirst = 0, oldNum = 0;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) { ID3D11Buffer* t = nullptr; c1->VSGetConstantBuffers1(5, 1, &t, &oldFirst, &oldNum); if (t) t->Release(); }
    float q[72];
    for (size_t i = 0; i < patches.size(); i++) {
        const Patch& p = patches[i];
        float scale = p.cell / 0.5f;
        float cs = (p.rot == 0) ? 1.f : (p.rot == 2) ? -1.f : 0.f;
        float sn = (p.rot == 1) ? 1.f : (p.rot == 3) ? -1.f : 0.f;
        for (int k = 0; k < 16; k++) q[k] = (k % 5 == 0) ? 1.f : 0.f;   // viewWorld: identidad (no se usa en extendidas)
        float* w = q + 16;                                                 // world: giro en Y + traslacion
        w[0] = cs;  w[1] = 0; w[2] = sn;  w[3] = p.x;
        w[4] = 0;   w[5] = 1; w[6] = 0;   w[7] = 0;
        w[8] = -sn; w[9] = 0; w[10] = cs; w[11] = p.z;
        w[12] = 0;  w[13] = 0; w[14] = 0; w[15] = 1;
        q[32] = scale; q[33] = 1234.5f; q[34] = tall; q[35] = 0.012f * scale * wide;   // cb5[8]: escala, flag, alto, ensanche
        for (int k = 36; k < 72; k++) q[k] = 0.f;
        c->UpdateSubresource(s_cb, 0, nullptr, q, 0, 0);
        c->VSSetConstantBuffers(5, 1, &s_cb);
        c->DrawIndexed(s_lodCount.load(), s_lodStart, s_lodBase);   // pasa por nuestro gancho (t120, reglas)
    }
    if (old) { if (c1 && oldNum) c1->VSSetConstantBuffers1(5, 1, &old, &oldFirst, &oldNum); else c->VSSetConstantBuffers(5, 1, &old); old->Release(); }
    else { ID3D11Buffer* nul = nullptr; c->VSSetConstantBuffers(5, 1, &nul); }
    if (c1) c1->Release();
}

} // namespace fx
