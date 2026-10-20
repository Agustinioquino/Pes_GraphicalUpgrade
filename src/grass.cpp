// PES21 FX - pasto extra en toda la cancha (v2, instanciado).
// El juego dibuja hojas 3D en una grilla densa de 0.5 m solo cerca de la camara; lejos pone su variante minima cada
// 3 m. Cuando el juego dibuja su primera parcela del cuadro en la pasada principal (IB/VB, shaders, targets y cbuffers
// ya puestos), se emite pasto propio en 3 bandas de distancia: UN dibujo instanciado por banda. La lista de parcelas
// (x, z, escala, giro) va en un StructuredBuffer (t121); la DLL solo descarta con margen amplio (camara de hace un par
// de cuadros) y el VS hace el descarte exacto con la camara del mismo cuadro -> sin popping en los bordes.
// Las bandas y el pasto del juego se funden por altura (crecen / se achican) en vez de cambiar de golpe.
//   w58 on/off, x58 densidad, y58 desde (m), z58 hasta (m); x59 alto, y59 ancho, z59 magenta; x61 calidad
#include "core.h"
#include <d3d11_1.h>
#include <atomic>
#include <cmath>
#include <vector>
#include <mutex>
#include <algorithm>

namespace fx {

static const uint64_t kGrassVS = 0x498b30bad256e8edULL;
static const int kMaxInst = 32768;
struct GInst { float x, z, s, r; };   // instancia: posicion, escala, giro (multiplos de 90 grados)

static std::atomic<int> s_frame{0};
static std::atomic<int> s_emittedFrame{-1};
static std::atomic<UINT> s_lodCount{0xffffffff}; static UINT s_lodStart = 0; static INT s_lodBase = 0;
struct Variant { UINT count, start; INT base; };
static std::mutex s_varMutex; static std::vector<Variant> s_variants;   // variantes de malla vistas (ordenadas por indices)
static void NoteVariant(UINT count, UINT start, INT base) {
    std::lock_guard<std::mutex> lk(s_varMutex);
    for (auto& v : s_variants) if (v.start == start) return;
    if (s_variants.size() >= 160) return;
    s_variants.push_back({ count, start, base });
    std::sort(s_variants.begin(), s_variants.end(), [](const Variant& a, const Variant& b) { return a.count < b.count; });
}
static Variant PickVariantPct(float pct) {
    std::lock_guard<std::mutex> lk(s_varMutex);
    if (s_variants.empty()) return { s_lodCount.load(), s_lodStart, s_lodBase };
    pct = (std::max)(0.0f, (std::min)(pct, 1.0f));
    int i = (int)(pct * (s_variants.size() - 1) + 0.5f);
    return s_variants[i];
}
static ID3D11Buffer* s_cb = nullptr;
static ID3D11Buffer* s_inst = nullptr; static ID3D11ShaderResourceView* s_instSRV = nullptr;
static const int kStages = 4;
static ID3D11Buffer* s_cb2Stage[kStages] = {}; static int s_stageW = 0; static int s_stageFrame[kStages] = { -1, -1, -1, -1 };
static float s_pv[16]; static float s_eye[3]; static bool s_camValid = false;
static int s_lastCount = 0;
int g_grassPatchesLastFrame = 0;
GrassDbg g_grassDbg;
static void Why(const char* w) { g_grassDbg.why = w; }
UINT g_bbW = 0, g_bbH = 0;

static void EnsureBuffers() {
    if (!g_dev.dev) return;
    if (!s_cb) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = 288; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(g_dev.dev->CreateBuffer(&d, nullptr, &s_cb))) { Log("pasto extra: no se pudo crear el cbuffer"); s_cb = nullptr; }
    }
    if (!s_inst) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxInst * 16; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = 16;
        if (SUCCEEDED(g_dev.dev->CreateBuffer(&d, nullptr, &s_inst))) {
            D3D11_SHADER_RESOURCE_VIEW_DESC v = {}; v.Format = DXGI_FORMAT_UNKNOWN; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            v.Buffer.FirstElement = 0; v.Buffer.NumElements = kMaxInst;
            if (FAILED(g_dev.dev->CreateShaderResourceView(s_inst, &v, &s_instSRV))) { s_instSRV = nullptr; Log("pasto extra: sin SRV de instancias"); }
        } else { s_inst = nullptr; Log("pasto extra: no se pudo crear el buffer de instancias"); }
    }
    for (int i = 0; i < kStages; i++) if (!s_cb2Stage[i]) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = 480; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        g_dev.dev->CreateBuffer(&d, nullptr, &s_cb2Stage[i]);
    }
}

// camara para el descarte grueso: el staging mas nuevo que ya este listo (2-3 cuadros de atraso; el VS corrige)
void GrassPresent() {
    int f = s_frame.fetch_add(1) + 1;
    if (g_dev.imm) {
        int best = -1, bestF = -1;
        for (int i = 0; i < kStages; i++) if (s_stageFrame[i] >= 0 && s_stageFrame[i] <= f - 2 && s_stageFrame[i] > bestF) { best = i; bestF = s_stageFrame[i]; }
        if (best >= 0 && s_cb2Stage[best]) {
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(g_dev.imm->Map(s_cb2Stage[best], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
                const float* q = (const float*)m.pData;
                for (int i = 0; i < 16; i++) s_pv[i] = q[i];
                s_eye[0] = q[80]; s_eye[1] = q[81]; s_eye[2] = q[82];
                s_camValid = true;
                g_dev.imm->Unmap(s_cb2Stage[best], 0);
                for (int i = 0; i < kStages; i++) if (s_stageFrame[i] >= 0 && s_stageFrame[i] <= bestF) s_stageFrame[i] = -1;
            }
        }
    }
    g_grassPatchesLastFrame = s_lastCount;
    static double nextLog = 0; double t = Now();
    if (t > nextLog && g_cfg.live[58][3] > 0.5f) { nextLog = t + 3.0;
        Log("pasto extra: llamadas %d, emisiones %d, instancias %d (bandas %d/%d/%d), variante %u idx, camara %s (%.1f, %.1f), estado: %s",
            g_grassDbg.calls, g_grassDbg.emits, g_grassDbg.patches, g_grassDbg.ring[0], g_grassDbg.ring[1], g_grassDbg.ring[2], g_grassDbg.lod,
            g_grassDbg.cam ? "ok" : "no", g_grassDbg.ex, g_grassDbg.ez, g_grassDbg.why ? g_grassDbg.why : "-");
        g_grassDbg.calls = 0; g_grassDbg.emits = 0; g_grassDbg.sparseLast = g_grassDbg.sparse; g_grassDbg.sparse = 0; }
}

// descarte grueso (margen amplio: la camara tiene un par de cuadros de atraso; el VS hace el descarte exacto)
static inline bool RoughVisible(float x, float z, float r) {
    if (!s_camValid) return true;
    const float* M = s_pv;
    float cx = M[0] * x + M[2] * z + M[3];
    float cy = M[4] * x + M[6] * z + M[7];
    float cw = M[12] * x + M[14] * z + M[15];
    if (cw < -4.0f) return false;
    float w = (std::max)(cw, 0.5f) * 1.35f + r * 2.0f;
    return cx > -w && cx < w && cy > -w && cy < w;
}

struct Band { float r0, r1, cell, fadeIn0, fadeIn1, fadeOut0, fadeOut1, widthMul; float pct; float frac; };

// arma la lista de instancias de una banda (grilla de la cancha alineada al origen)
static int BuildBand(GInst* out, int cap, const Band& b) {
    if (cap <= 0) return 0;   // (20/10) sin lugar: antes se escribia una instancia pasada del final del buffer -> 0xc0000005
    const float halfL = 52.5f + 3.0f, halfW = 34.0f + 3.0f;
    float ex = s_eye[0], ez = s_eye[2];
    float rIn = (std::max)(0.0f, b.fadeIn0), rOut = b.fadeOut1;
    float r1sq = rOut * rOut, r0sq = rIn * rIn;
    // solo el rectangulo que contiene el anillo
    float x0 = (std::max)(-halfL, ex - rOut), x1 = (std::min)(halfL, ex + rOut);
    float z0 = (std::max)(-halfW, ez - rOut), z1 = (std::min)(halfW, ez + rOut);
    if (!s_camValid) { x0 = -halfL; x1 = halfL; z0 = -halfW; z1 = halfW; }
    int ix0 = (int)floorf((x0 + halfL) / b.cell), ix1 = (int)ceilf((x1 + halfL) / b.cell);
    int iz0 = (int)floorf((z0 + halfW) / b.cell), iz1 = (int)ceilf((z1 + halfW) / b.cell);
    int n = 0;
    for (int iz = iz0; iz < iz1; iz++) {
        float z = -halfW + (iz + 0.5f) * b.cell;
        for (int ix = ix0; ix < ix1; ix++) {
            float x = -halfL + (ix + 0.5f) * b.cell;
            float dx = x - ex, dz = z - ez, d2 = dx * dx + dz * dz;
            if (s_camValid && (d2 < r0sq || d2 >= r1sq)) continue;
            if (!RoughVisible(x, z, b.cell)) continue;
            if (n >= cap) return n;   // (20/10) el tope se mira ANTES de escribir
            int rot = ((ix * 7 + iz * 13) ^ (ix * iz)) & 3;
            out[n++] = GInst{ x, z, b.cell / 0.5f, (float)rot };
        }
    }
    return n;
}

void GrassOnDraw(ID3D11DeviceContext* c, const ShaderEntry* vs, UINT count, UINT start, INT base) {
    if (!vs || vs->hash != kGrassVS || g_cfg.bypass || !g_dev.dev) return;
    g_grassDbg.calls++;
    if (!vs->repl) { Why("el VS del pasto no tiene reemplazo (ver Errores)"); return; }
    UINT prev = s_lodCount.load();
    while (count < prev && !s_lodCount.compare_exchange_weak(prev, count)) {}
    if (count <= s_lodCount.load()) { s_lodStart = start; s_lodBase = base; }
    NoteVariant(count, start, base);
    const float* p58 = g_cfg.live[58]; const float* p59 = g_cfg.live[59];
    bool extraOn = p58[3] > 0.5f;
    // con el pasto extra prendido, el escalado de las parcelas lejanas del juego no se usa (lo reemplazan las bandas)
    if (!extraOn && count == s_lodCount.load() && g_cfg.live[60][3] > 0.5f) { t_sparseGrass = true; g_grassDbg.sparse++; }
    if (!extraOn) { Why("apagado (w58)"); return; }
    if (g_bbW) {
        ID3D11RenderTargetView* rtv = nullptr; c->OMGetRenderTargets(1, &rtv, nullptr);
        if (!rtv) return;
        ID3D11Resource* res = nullptr; rtv->GetResource(&res); rtv->Release();
        ID3D11Texture2D* t = nullptr; UINT w = 0;
        if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); w = d.Width; t->Release(); }
        if (res) res->Release();
        g_grassDbg.rtW = w;
        if (w != g_bbW) { Why("pasada secundaria (target de otro tamano)"); return; }
    }
    int f = s_frame.load();
    int exp = s_emittedFrame.load();
    if (exp == f) return;
    if (!s_emittedFrame.compare_exchange_strong(exp, f)) return;
    EnsureBuffers();
    if (!s_cb || !s_instSRV) { Why("sin buffers propios"); return; }
    g_grassDbg.emits++;
    // copiar la camara de este cuadro a un staging (se lee 2 cuadros despues para el descarte grueso)
    { ID3D11Buffer* cb2 = nullptr; UINT f2 = 0, n2 = 0;
      ID3D11DeviceContext1* cq = nullptr;
      if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&cq)) && cq) { cq->VSGetConstantBuffers1(2, 1, &cb2, &f2, &n2); cq->Release(); }
      else c->VSGetConstantBuffers(2, 1, &cb2);
      if (cb2) { D3D11_BUFFER_DESC d; cb2->GetDesc(&d); UINT off = f2 * 16;
        if (d.ByteWidth >= off + 480) { int w = s_stageW; s_stageW = (s_stageW + 1) % kStages;
          D3D11_BOX bx = { off, 0, 0, off + 480, 1, 1 }; c->CopySubresourceRegion(s_cb2Stage[w], 0, 0, 0, 0, cb2, 0, &bx); s_stageFrame[w] = f; g_grassDbg.cb2off = off; }
        cb2->Release(); } }
    if (s_lodCount.load() == 0xffffffff) { Why("todavia sin variante de malla"); return; }
    if (!s_camValid) { Why("todavia sin camara (se emite desde el proximo cuadro)"); return; }   // (20/10) sin camara la cancha entera a 0.5 m desborda el buffer
    g_grassDbg.cam = s_camValid; g_grassDbg.ex = s_eye[0]; g_grassDbg.ez = s_eye[2];
    float dens = p58[0] > 0.05f ? p58[0] : 1.0f;
    float from = p58[1] > 1.0f ? p58[1] : 14.0f;
    float to = p58[2] > from + 10.0f ? p58[2] : 120.0f;
    float tall = p59[0] > 0.05f ? p59[0] : 1.3f;
    float wide = p59[1] >= 0.0f ? p59[1] : 1.0f;
    int q = (int)(g_cfg.live[61][0] + 0.5f); q = (std::max)(0, (std::min)(q, 3));
    float qp[4] = { 0.0f, 0.25f, 0.5f, 0.8f };
    // 3 bandas que se solapan 6 m con fundido por altura; la primera entra donde el pasto del juego se apaga
    const float F = 6.0f;
    float m1 = (std::max)(from + 14.0f, 32.0f), m2 = (std::max)(m1 + 20.0f, 60.0f);
    Band bands[3] = {
        { from, m1, 0.5f / sqrtf(dens) < 0.5f ? 0.5f : 0.5f / sqrtf(dens), from, from + F, m1, m1 + F, 1.0f, qp[q], 1.0f },
        { m1, m2, 0.75f / sqrtf(dens), m1, m1 + F, m2, m2 + F, 1.8f, qp[q] * 0.5f, 1.0f },
        { m2, to, 1.0f / sqrtf(dens), m2, m2 + F, to, to + 0.01f, 2.6f, 0.0f, 1.0f },
    };
    for (auto& b : bands) b.cell = (std::max)(b.cell, 0.5f);
    D3D11_MAPPED_SUBRESOURCE mp;
    if (FAILED(c->Map(s_inst, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) { Why("no se pudo escribir el buffer de instancias"); return; }
    GInst* inst = (GInst*)mp.pData;
    int offs[3], cnts[3], total = 0;
    for (int i = 0; i < 3; i++) {
        offs[i] = total;
        cnts[i] = (bands[i].r1 > bands[i].r0) ? BuildBand(inst + total, kMaxInst - total, bands[i]) : 0;
        total += cnts[i]; g_grassDbg.ring[i] = cnts[i];
    }
    c->Unmap(s_inst, 0);
    s_lastCount = total; g_grassDbg.patches = total;
    if (total == 0) { Why("0 parcelas tras el descarte"); return; }
    Why("emitiendo");
    // estado del juego que tocamos: cb5 y t121 del VS
    ID3D11Buffer* old = nullptr; UINT oldFirst = 0, oldNum = 0;
    ID3D11DeviceContext1* c1 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) c1->VSGetConstantBuffers1(5, 1, &old, &oldFirst, &oldNum);
    else c->VSGetConstantBuffers(5, 1, &old);
    ID3D11ShaderResourceView* oldT = nullptr; c->VSGetShaderResources(121, 1, &oldT);
    c->VSSetShaderResources(121, 1, &s_instSRV);
    float cbq[72];
    for (int i = 0; i < 3; i++) {
        if (!cnts[i]) continue;
        const Band& b = bands[i];
        Variant var = PickVariantPct(b.pct);
        if (i == 0) g_grassDbg.lod = var.count;
        for (int k = 0; k < 72; k++) cbq[k] = 0.f;
        // cb5[8]: (sin uso, marca, alto, ancho base)  cb5[9]: (magenta, offset de instancias, fadeIn0, fadeIn1)
        // cb5[10]: (fadeOut0, fadeOut1, multiplicador de ancho, 0)
        cbq[32] = 1.0f; cbq[33] = 1234.5f; cbq[34] = tall; cbq[35] = 0.012f * wide;
        cbq[36] = p59[2] > 0.5f ? 1.f : 0.f; cbq[37] = (float)offs[i]; cbq[38] = b.fadeIn0; cbq[39] = b.fadeIn1;
        cbq[40] = b.fadeOut0; cbq[41] = b.fadeOut1; cbq[42] = b.widthMul;
        c->UpdateSubresource(s_cb, 0, nullptr, cbq, 0, 0);
        c->VSSetConstantBuffers(5, 1, &s_cb);
        c->DrawIndexedInstanced(var.count, (UINT)cnts[i], var.start, var.base, 0);   // pasa por nuestro gancho (t120)
    }
    c->VSSetShaderResources(121, 1, &oldT); if (oldT) oldT->Release();
    if (old) { if (c1 && oldNum) c1->VSSetConstantBuffers1(5, 1, &old, &oldFirst, &oldNum); else c->VSSetConstantBuffers(5, 1, &old); old->Release(); }
    else { ID3D11Buffer* nul = nullptr; c->VSSetConstantBuffers(5, 1, &nul); }
    if (c1) c1->Release();
}

} // namespace fx
