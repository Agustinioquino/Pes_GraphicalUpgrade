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
#include <cstring>

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
// (20/10) volcado unico del formato de vertices de la malla de pasto del juego (para armar una malla propia mas densa):
// se copian 512 bytes del VB en el dibujo y se leen en Present
static ID3D11Buffer* s_vbStage = nullptr; static std::atomic<int> s_vbState{0};   // 0 nada, 1 copiado (pendiente de leer), 2 listo
static UINT s_vbStride = 0, s_vbOffset = 0; static UINT s_ibFmt = 0;
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

// ---------------------------------------------------------------- pasto v4 (20/10): malla propia de manojos curvos
// El juego dibuja cada parcela de 0.5 m con tarjetas planas (manojo de ~1.2 x 5.8 cm; 4624 por parcela de cerca, menos
// lejos: 27744 / ~9000 / ~6000 indices). Con x123 la DLL cambia esas tarjetas por las suyas, en el mismo formato de
// vertices (POSITION float3 en el slot 0, TEXCOORD0 half2 en el slot 3): raices propias bien repartidas, 3 tramos (2 en el
// nivel intermedio) y una curva por tarjeta (sale vertical y se dobla), giro, ancho y alto al azar. El VS del juego
// (reemplazado) las trata igual que a las del juego: altura, corte, viento, pisadas, atlas propio.
//   x123 on | y123 densidad (x las del juego) | z123 curvatura | w123 nivel minimo (0 = cerca y medio, 1 = solo cerca)
struct DenseLevel { ID3D11Buffer* pos = nullptr; ID3D11Buffer* uv = nullptr; ID3D11Buffer* ib = nullptr; UINT cards = 0, vertsPerCard = 0, idxPerCard = 0; };
static DenseLevel s_dense[2]; static std::atomic<bool> s_denseBuilt{false}; static float s_denseCurv = -1.0f;
static std::mutex s_denseMx;   // (20/10) la malla se arma una vez, bajo llave: el pasto se dibuja desde varios hilos (contextos diferidos)
static std::atomic<int> g_denseDraws{0};
static inline uint16_t F16(float v) {   // float -> half (sin NaN/inf)
    uint32_t x; memcpy(&x, &v, 4); uint32_t sgn = (x >> 16) & 0x8000; int32_t e = ((x >> 23) & 0xff) - 127 + 15; uint32_t m = x & 0x7fffff;
    if (e <= 0) { if (e < -10) return (uint16_t)sgn; m |= 0x800000; uint32_t sh = (uint32_t)(14 - e); return (uint16_t)(sgn | ((m + (1u << (sh - 1))) >> sh)); }
    if (e >= 31) return (uint16_t)(sgn | 0x7bff);
    return (uint16_t)(sgn | (e << 10) | (m >> 13));
}
static float Halton(int i, int b) { float f = 1.0f, r = 0.0f; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; }
static void DenseRelease() { for (auto& L : s_dense) { if (L.pos) L.pos->Release(); if (L.uv) L.uv->Release(); if (L.ib) L.ib->Release(); L = DenseLevel(); } s_denseBuilt = false; }
static void BuildDense(float curv) {
    DenseRelease(); if (!g_dev.dev) return;
    const UINT kMaxCards = 9248;                       // hasta 2x las 4624 tarjetas del juego
    uint32_t rs = 0x9E3779B9u; auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs & 0xffffff) / 16777216.0f; };
    for (int lvl = 0; lvl < 2; lvl++) {
        int segs = lvl == 0 ? 3 : 2; UINT vpc = (UINT)(segs + 1) * 2, ipc = (UINT)segs * 6;
        std::vector<float> pos; pos.reserve(kMaxCards * vpc * 3); std::vector<uint16_t> uv; uv.reserve(kMaxCards * vpc * 2); std::vector<uint32_t> ib; ib.reserve(kMaxCards * ipc);
        rs = 0x9E3779B9u + lvl * 7919;
        for (UINT i = 0; i < kMaxCards; i++) {
            // raiz: secuencia de Halton (bien repartida, sin grilla) con un poco de ruido; las primeras N son un subconjunto parejo
            float rx = (Halton((int)i + 1, 2) - 0.5f) * 0.5f + (rnd() - 0.5f) * 0.004f, rz = (Halton((int)i + 1, 3) - 0.5f) * 0.5f + (rnd() - 0.5f) * 0.004f;
            float yaw = rnd() * 6.2831853f, h = 0.058f * (0.8f + 0.35f * rnd()), w = 0.012f * (0.8f + 0.4f * rnd());
            float ba = rnd() * 6.2831853f, bend = curv * (0.15f + 0.3f * rnd());     // curva: hacia donde y cuanto (fraccion del alto)
            float ta = rnd() * 6.2831853f, tilt = 0.12f * rnd();                      // inclinacion leve de toda la tarjeta
            float sx = cosf(yaw) * w * 0.5f, sz = sinf(yaw) * w * 0.5f, bx = cosf(ba), bz = sinf(ba), tx = cosf(ta), tz = sinf(ta);
            UINT v0 = (UINT)pos.size() / 3;
            for (int k = 0; k <= segs; k++) {
                float t = (float)k / segs;
                float cx = rx + tx * tilt * h * t + bx * bend * h * t * t, cz = rz + tz * tilt * h * t + bz * bend * h * t * t;
                float cy = h * t * (1.0f - 0.35f * bend * t);                                        // la curva acorta un poco el alto
                float taper = 1.0f - 0.3f * t;
                pos.push_back(cx - sx * taper); pos.push_back(cy); pos.push_back(cz - sz * taper);
                pos.push_back(cx + sx * taper); pos.push_back(cy); pos.push_back(cz + sz * taper);
                float v = 0.995f - 0.985f * t;                                                       // raiz abajo del atlas, punta arriba
                uv.push_back(F16(0.0f)); uv.push_back(F16(v)); uv.push_back(F16(1.0f)); uv.push_back(F16(v));
            }
            for (int k = 0; k < segs; k++) { UINT a = v0 + k * 2, b = a + 1, c2 = a + 2, d = a + 3;
                ib.push_back(a); ib.push_back(b); ib.push_back(c2); ib.push_back(b); ib.push_back(d); ib.push_back(c2); }
        }
        DenseLevel& L = s_dense[lvl]; L.cards = kMaxCards; L.vertsPerCard = vpc; L.idxPerCard = ipc;
        D3D11_BUFFER_DESC d = {}; D3D11_SUBRESOURCE_DATA sd = {};
        d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        d.ByteWidth = (UINT)(pos.size() * 4); sd.pSysMem = pos.data(); if (FAILED(g_dev.dev->CreateBuffer(&d, &sd, &L.pos))) { L.pos = nullptr; }
        d.ByteWidth = (UINT)(uv.size() * 2); sd.pSysMem = uv.data(); if (FAILED(g_dev.dev->CreateBuffer(&d, &sd, &L.uv))) { L.uv = nullptr; }
        d.BindFlags = D3D11_BIND_INDEX_BUFFER; d.ByteWidth = (UINT)(ib.size() * 4); sd.pSysMem = ib.data(); if (FAILED(g_dev.dev->CreateBuffer(&d, &sd, &L.ib))) { L.ib = nullptr; }
        if (!L.pos || !L.uv || !L.ib) { Log("pasto v4: no se pudo crear la malla del nivel %d", lvl); DenseRelease(); return; }
    }
    s_denseBuilt = true; s_denseCurv = curv;
    Log("pasto v4: mallas de manojos curvos listas (%u tarjetas x %u/%u indices, curvatura %.2f)", kMaxCards, s_dense[0].idxPerCard, s_dense[1].idxPerCard, curv);
}
// decide si este dibujo del juego se cambia por la malla propia; llena 'out' (con referencias) y devuelve true
static bool DensePick(UINT count, LodIA* out) {
    const float* P = g_cfg.live[123];
    if (P[0] < 0.5f) return false;
    float curv = std::clamp(P[2], 0.0f, 1.5f);
    if (!s_denseBuilt.load() || fabsf(curv - s_denseCurv) > 0.01f) {
        // solo un hilo arma; los demas no esperan (dibujan las tarjetas del juego este cuadro). Un cambio de curvatura
        // reconstruye solo cuando nadie esta usando la malla: se arma en buffers nuevos y se cambian bajo la llave.
        std::unique_lock<std::mutex> lk(s_denseMx, std::try_to_lock);
        if (!lk.owns_lock()) return false;
        if (!s_denseBuilt.load() || fabsf(curv - s_denseCurv) > 0.01f) BuildDense(curv);
        if (!s_denseBuilt.load()) return false;
    }
    std::lock_guard<std::mutex> lk2(s_denseMx);   // AddRef de la malla bajo la llave (no se libera a mitad de camino)
    if (!s_denseBuilt.load()) return false;
    int lvl; if (count >= 20000) lvl = 0; else if (count >= 8000 && P[3] < 0.5f) lvl = 1; else return false;
    UINT gameCards = count / 6; float dens = P[1] > 0.05f ? std::clamp(P[1], 0.25f, 2.0f) : 1.0f;
    UINT cards = (std::min)((UINT)(gameCards * dens + 0.5f), s_dense[lvl].cards); if (cards < 16) return false;
    DenseLevel& L = s_dense[lvl];
    *out = LodIA();
    out->vb[0] = L.pos; out->stride[0] = 12; out->offset[0] = 0;
    out->vb[3] = L.uv; out->stride[3] = 4; out->offset[3] = 0;
    out->ib = L.ib; out->ibFmt = DXGI_FORMAT_R32_UINT; out->ibOff = 0;
    out->count = cards * L.idxPerCard; out->start = 0; out->base = 0;
    out->AddRefAll(); g_denseDraws++;
    return true;
}
void GrassDenseReset() { DenseRelease(); }

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
    if (s_vbState.load() == 1 && s_vbStage && g_dev.imm) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_dev.imm->Map(s_vbStage, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
            const unsigned char* b = (const unsigned char*)m.pData; const unsigned short* h = (const unsigned short*)m.pData; const float* f = (const float*)m.pData;
            auto half = [](unsigned short x) { unsigned s = (x >> 15) & 1, e = (x >> 10) & 31, mt = x & 1023; float v; if (e == 0) v = ldexpf((float)mt, -24); else if (e == 31) v = 0.f; else v = ldexpf((float)(mt | 1024), (int)e - 25); return s ? -v : v; };
            std::string line = "pasto: formato de vertices (stride " + std::to_string(s_vbStride) + ", offset " + std::to_string(s_vbOffset) + ", IB fmt " + std::to_string(s_ibFmt) + ")\n  hex:";
            for (int i = 0; i < 48; i++) { char t[8]; snprintf(t, 8, " %02x", b[i]); line += t; if (i % 12 == 11) line += " |"; }
            line += "\n  como half:"; for (int i = 0; i < 24; i++) { char t[24]; snprintf(t, 24, " %.3f", half(h[i])); line += t; if (i % 6 == 5) line += " |"; }
            line += "\n  como float:"; for (int i = 0; i < 12; i++) { char t[24]; snprintf(t, 24, " %.3f", f[i]); line += t; if (i % 3 == 2) line += " |"; }
            line += "\n  slot 1 hex:"; for (int i = 256; i < 256 + 32; i++) { char t[8]; snprintf(t, 8, " %02x", b[i]); line += t; if ((i - 256) % 8 == 7) line += " |"; }
            // (20/10) slot 1 = normal half4 @0, tangente half4 @8, uv half2 @16 (stride 20): se listan 12 vertices con su posicion
            line += "\n  vertices (pos | normal | tangente | uv):";
            for (int v = 0; v < 12; v++) { const float* pv = f + v * 3; const unsigned short* hv = h + 128 + v * 10; char t[200];
                snprintf(t, sizeof t, "\n   %2d: (%.3f %.3f %.3f) | (%.2f %.2f %.2f) | (%.2f %.2f %.2f) | uv (%.3f %.3f)", v, pv[0], pv[1], pv[2], half(hv[0]), half(hv[1]), half(hv[2]), half(hv[4]), half(hv[5]), half(hv[6]), half(hv[8]), half(hv[9])); line += t; }
            line += "\n  indices:"; for (int i = 0; i < 48; i++) { char t[12]; snprintf(t, 12, " %u", (unsigned)h[384 + i]); line += t; if (i % 3 == 2) line += " |"; }
            Log("%s", line.c_str());
            g_dev.imm->Unmap(s_vbStage, 0); s_vbState = 2;
        }
    }
    static double nextLog = 0; double t = Now();
    if (t > nextLog && g_cfg.live[58][3] > 0.5f) { nextLog = t + 3.0;
        Log("pasto extra: llamadas %d, emisiones %d, instancias %d (bandas %d/%d/%d), variante %u idx, camara %s (%.1f, %.1f), estado: %s | v4: %d dibujos con manojos curvos",
            g_grassDbg.calls, g_grassDbg.emits, g_grassDbg.patches, g_grassDbg.ring[0], g_grassDbg.ring[1], g_grassDbg.ring[2], g_grassDbg.lod,
            g_grassDbg.cam ? "ok" : "no", g_grassDbg.ex, g_grassDbg.ez, g_grassDbg.why ? g_grassDbg.why : "-", g_denseDraws.exchange(0));
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
    if (s_vbState.load() == 0 && count > 1000) {   // volcado unico del formato de vertices (parcela densa)
        int z = 0; if (s_vbState.compare_exchange_strong(z, 1)) {
            ID3D11Buffer* vbs[4] = {}; UINT strides[4] = {}, offs[4] = {}; c->IAGetVertexBuffers(0, 4, vbs, strides, offs);
            ID3D11Buffer* ib = nullptr; DXGI_FORMAT ifmt = DXGI_FORMAT_UNKNOWN; UINT ioff = 0; c->IAGetIndexBuffer(&ib, &ifmt, &ioff); if (ib) ib->Release();
            ID3D11InputLayout* il = nullptr; c->IAGetInputLayout(&il);
            Log("pasto: layout: %s | VB strides %u %u %u %u (offsets %u %u %u %u)", il ? DescribeInputLayout(il).c_str() : "(sin layout)", strides[0], strides[1], strides[2], strides[3], offs[0], offs[1], offs[2], offs[3]);
            if (il) il->Release();
            s_vbStride = strides[0]; s_vbOffset = offs[0]; s_ibFmt = (UINT)ifmt;
            ID3D11Buffer* vb = vbs[0]; UINT stride = strides[0], off = offs[0];
            if (vb) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 1024; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                if (!s_vbStage) g_dev.dev->CreateBuffer(&d, nullptr, &s_vbStage);
                if (s_vbStage) { D3D11_BOX bx = { off + (UINT)base * stride, 0, 0, off + (UINT)base * stride + 256, 1, 1 }; c->CopySubresourceRegion(s_vbStage, 0, 0, 0, 0, vb, 0, &bx);
                    // segundo slot (normal, tangente, uv: 20 bytes por vertice) a continuacion, 512 bytes = 25 vertices
                    if (vbs[1] && strides[1]) { D3D11_BOX b2 = { offs[1] + (UINT)base * strides[1], 0, 0, offs[1] + (UINT)base * strides[1] + 512, 1, 1 }; c->CopySubresourceRegion(s_vbStage, 0, 256, 0, 0, vbs[1], 0, &b2); }
                    // (20/10) los primeros 128 bytes del IB de este dibujo (64 indices de 16 bits): estructura de las tarjetas
                    ID3D11Buffer* ibd = nullptr; DXGI_FORMAT ifd; UINT iod = 0; c->IAGetIndexBuffer(&ibd, &ifd, &iod);
                    if (ibd) { UINT isz = ifd == DXGI_FORMAT_R16_UINT ? 2 : 4; D3D11_BOX b3 = { iod + start * isz, 0, 0, iod + start * isz + 128, 1, 1 }; c->CopySubresourceRegion(s_vbStage, 0, 768, 0, 0, ibd, 0, &b3); ibd->Release(); } }
                else s_vbState = 2;
            } else s_vbState = 2;
            for (auto* b : vbs) if (b) b->Release();
        } }
    UINT prev = s_lodCount.load();
    while (count < prev && !s_lodCount.compare_exchange_weak(prev, count)) {}
    if (count <= s_lodCount.load()) { s_lodStart = start; s_lodBase = base; }
    NoteVariant(count, start, base);
    const float* p58 = g_cfg.live[58]; const float* p59 = g_cfg.live[59];
    bool extraOn = p58[3] > 0.5f;
    // con el pasto extra prendido, el escalado de las parcelas lejanas del juego no se usa (lo reemplazan las bandas)
    if (!extraOn && count == s_lodCount.load() && g_cfg.live[60][3] > 0.5f) { t_sparseGrass = true; g_grassDbg.sparse++; }
    if (g_bbW) {
        ID3D11RenderTargetView* rtv = nullptr; c->OMGetRenderTargets(1, &rtv, nullptr);
        if (!rtv) return;
        ID3D11Resource* res = nullptr; rtv->GetResource(&res); rtv->Release();
        ID3D11Texture2D* t = nullptr; UINT w = 0;
        if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); w = d.Width; t->Release(); }
        if (res) res->Release();
        g_grassDbg.rtW = w;
        if (w != g_bbW) { if (extraOn) Why("pasada secundaria (target de otro tamano)"); return; }
    }
    PitchClipsDraw(c, s_frame.load());   // recortes de pasto volando (una vez por cuadro; el estado del pasto ya esta puesto)
    if (!t_grassSub.count) DensePick(count, &t_grassSub);   // (20/10) pasto v4: tarjetas curvas propias en vez de las del juego (cerca)
    if (!extraOn) { Why("apagado (w58)"); return; }
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
