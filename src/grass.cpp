// PES21 FX - pasto extra en toda la cancha (v3, 25/10: emision independiente + lista en la GPU).
// El juego dibuja hojas 3D en una grilla densa de 0.5 m solo cerca de la camara; lejos pone su variante minima cada
// 3 m, y en muchas camaras no dibuja pasto. El mod emite su propio pasto al terminar el dibujo de la BASE de la cancha
// (PS 5e92 / c956, que existe en todas las camaras) con el estado del dibujo de pasto del juego guardado la ultima vez que
// se vio (shaders, layout, buffers, texturas, estados; cb0 / cb4 copiados a buffers propios). Si todavia no se vio ese
// estado, emite como antes: en la primera parcela del juego del cuadro (estado ya puesto).
// La lista de parcelas la arma un compute shader (shaders\pes_grass_cull_cs.hlsl) con la camara del MISMO cuadro: grilla
// fija de la cancha en 3 niveles (celdas de 0.5 / 1 / 2 m); cada celda entra en el nivel que le toca por su tamano en
// pantalla (pixeles por metro), descarte por pantalla y tope fijo por nivel; UN DrawIndexedInstancedIndirect por nivel.
// El nivel lejano no tiene corte de distancia: la cancha entera queda cubierta en cualquier camara.
//   w58 on/off, x58 densidad (corre los umbrales de tamano); x59 alto, y59 ancho, z59 magenta; x61 calidad (malla y topes)
//   x63 (DLL) 1 = el cuadro anterior no emitio (el VS no apaga el pasto del juego); y63 umbral del pasto del juego (px/m,
//   0 = modo por distancia); z63 mitad del alto de pantalla (px)
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
static const uint64_t kGrassPS = 0x18afff0f79d9aaa5ULL;   // PS de las hojas (G-buffer): solo desde ese dibujo se guarda estado y se emite
// (25/10 c) 3 niveles x 6 subniveles de densidad (tarjetas por parcela: 9248 >> b), 8192 parcelas por ranura (con la
// camara cenital todas las celdas de un nivel caen en la misma ranura: a 4K el nivel 0 llega a ~4800 celdas)
static const int kSlotCap = 8192, kBins = 6, kSlots = 18;
static const int kMaxInst = kSlotCap * kSlots;
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
// (25/10) la variante con la cantidad de indices mas cercana al objetivo (antes: percentil de las vistas -> cambiaba
// segun lo que el juego hubiera dibujado antes)
static Variant PickVariantNear(UINT target) {
    std::lock_guard<std::mutex> lk(s_varMutex);
    if (s_variants.empty()) return { s_lodCount.load(), s_lodStart, s_lodBase };
    size_t bi = 0; long long bd = -1;
    for (size_t i = 0; i < s_variants.size(); i++) { long long d = (long long)s_variants[i].count - (long long)target; if (d < 0) d = -d; if (bd < 0 || d < bd) { bd = d; bi = i; } }
    return s_variants[bi];
}
static ID3D11Buffer* s_cb = nullptr;
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
// (25/10 c) mallas para las parcelas del mod (celdas de 0.5 / 1 / 2 m): 2 tramos, raices repartidas en la celda entera y los
// desplazamientos de la hoja (ancho, inclinacion, curva) en metros reales: antes el VS escalaba toda la posicion por la celda y
// las hojas de 2 m quedaban tumbadas 4x
static DenseLevel s_denseExt[3];
static std::mutex s_denseMx;   // (20/10) la malla se arma una vez, bajo llave: el pasto se dibuja desde varios hilos (contextos diferidos)
static std::atomic<int> g_denseDraws{0};
static inline uint16_t F16(float v) {   // float -> half (sin NaN/inf)
    uint32_t x; memcpy(&x, &v, 4); uint32_t sgn = (x >> 16) & 0x8000; int32_t e = ((x >> 23) & 0xff) - 127 + 15; uint32_t m = x & 0x7fffff;
    if (e <= 0) { if (e < -10) return (uint16_t)sgn; m |= 0x800000; uint32_t sh = (uint32_t)(14 - e); return (uint16_t)(sgn | ((m + (1u << (sh - 1))) >> sh)); }
    if (e >= 31) return (uint16_t)(sgn | 0x7bff);
    return (uint16_t)(sgn | (e << 10) | (m >> 13));
}
static float Halton(int i, int b) { float f = 1.0f, r = 0.0f; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; }
static void DenseRelease() { auto rel = [](DenseLevel& L) { if (L.pos) L.pos->Release(); if (L.uv) L.uv->Release(); if (L.ib) L.ib->Release(); L = DenseLevel(); }; for (auto& L : s_dense) rel(L); for (auto& L : s_denseExt) rel(L); s_denseBuilt = false; }
static void BuildDense(float curv) {
    DenseRelease(); if (!g_dev.dev) return;
    const UINT kMaxCards = 9248;                       // hasta 2x las 4624 tarjetas del juego
    uint32_t rs = 0x9E3779B9u; auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs & 0xffffff) / 16777216.0f; };
    for (int lvl = 0; lvl < 5; lvl++) {
        // 0 / 1: las del juego (3 / 2 tramos, parcela de 0.5 m); 2 / 3 / 4: las del mod (2 tramos, celda de 0.5 / 1 / 2 m)
        int segs = lvl == 0 ? 3 : 2; UINT vpc = (UINT)(segs + 1) * 2, ipc = (UINT)segs * 6;
        float rootScale = lvl == 3 ? 2.0f : (lvl == 4 ? 4.0f : 1.0f);
        std::vector<float> pos; pos.reserve(kMaxCards * vpc * 3); std::vector<uint16_t> uv; uv.reserve(kMaxCards * vpc * 2); std::vector<uint32_t> ib; ib.reserve(kMaxCards * ipc);
        rs = 0x9E3779B9u + lvl * 7919;
        for (UINT i = 0; i < kMaxCards; i++) {
            // raiz: secuencia de Halton (bien repartida, sin grilla) con un poco de ruido; las primeras N son un subconjunto parejo
            float rx = ((Halton((int)i + 1, 2) - 0.5f) * 0.5f + (rnd() - 0.5f) * 0.004f) * rootScale, rz = ((Halton((int)i + 1, 3) - 0.5f) * 0.5f + (rnd() - 0.5f) * 0.004f) * rootScale;
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
        DenseLevel& L = lvl < 2 ? s_dense[lvl] : s_denseExt[lvl - 2]; L.cards = kMaxCards; L.vertsPerCard = vpc; L.idxPerCard = ipc;
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
    // (20/10) pasto v5 (x139): usa los manojos curvos del v4 aunque el v4 este apagado (densidad 1.25, curvatura 0.6)
    float P[4] = { g_cfg.live[123][0], g_cfg.live[123][1], g_cfg.live[123][2], g_cfg.live[123][3] };
    if (P[0] < 0.5f) { if (g_cfg.live[139][0] < 0.5f) return false; P[1] = 1.25f; P[2] = 0.6f; P[3] = 0.0f; }
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
    {   // (25/10 c) con el pasto del mod prendido, el lado del juego sigue a Calidad (x61) y Densidad (x58) como el del mod (empalme parejo en 240 px/m)
        if (g_cfg.live[58][3] > 0.5f) { static const float cMulQ[4] = { 0.6f, 1.0f, 1.4f, 1.8f }; int q = std::clamp((int)(g_cfg.live[61][0] + 0.5f), 0, 3);
            float d58 = g_cfg.live[58][0] > 0.05f ? std::clamp(g_cfg.live[58][0], 0.3f, 2.0f) : 1.0f; dens = std::clamp(dens * cMulQ[q] * d58, 0.25f, 2.0f); }
    }
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

// ---------------------------------------------------------------- pasto v3 (25/10): estado del dibujo de pasto del juego
// Todo lo que hace falta para dibujar hojas con el VS 498b + PS 18af desde otro dibujo (la base de la cancha). Los
// cbuffers del juego pueden ser ventanas de un buffer grande que cambia de lugar cada cuadro: cb0 y cb4 del VS y cb0 del
// PS se copian a buffers propios al capturar; cb2 (escena) se toma del dibujo desde el que se emite (misma camara).
struct GrassState {
    const ShaderEntry* vsE = nullptr; const ShaderEntry* psE = nullptr; ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* il = nullptr; D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    ID3D11Buffer* vb[4] = {}; UINT stride[4] = {}, voff[4] = {}; ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_R16_UINT; UINT ibOff = 0;
    ID3D11Buffer* vsCb0 = nullptr; UINT vsCb0F = 0, vsCb0N = 0; ID3D11Buffer* vsCb4 = nullptr; UINT vsCb4F = 0, vsCb4N = 0;
    ID3D11Buffer* psCb0 = nullptr; UINT psCb0F = 0, psCb0N = 0;
    ID3D11ShaderResourceView* vsSrv[7] = {}; ID3D11SamplerState* vsSmp[2] = {};
    ID3D11ShaderResourceView* psSrv = nullptr; ID3D11SamplerState* psSmp = nullptr;
    ID3D11BlendState* bs = nullptr; float bf[4] = { 1, 1, 1, 1 }; UINT bmask = 0xffffffff; ID3D11DepthStencilState* dss = nullptr; UINT sref = 0; ID3D11RasterizerState* rs = nullptr;
    bool valid = false;
    template<class F> void ForEach(F f) {
        f((IUnknown*)vs); f((IUnknown*)ps); f((IUnknown*)il); for (auto* b : vb) f((IUnknown*)b); f((IUnknown*)ib);
        f((IUnknown*)vsCb0); f((IUnknown*)vsCb4); f((IUnknown*)psCb0); for (auto* v : vsSrv) f((IUnknown*)v); for (auto* s : vsSmp) f((IUnknown*)s);
        f((IUnknown*)psSrv); f((IUnknown*)psSmp); f((IUnknown*)bs); f((IUnknown*)dss); f((IUnknown*)rs);
    }
    void AddRefAll() { ForEach([](IUnknown* p) { if (p) p->AddRef(); }); }
    void Release() { ForEach([](IUnknown* p) { if (p) p->Release(); }); *this = GrassState(); }
};
static std::mutex s_stMx; static GrassState s_state;        // ultimo estado visto del dibujo de pasto del juego
static std::atomic<int> s_capFrame{-1};
// copias propias de los cbuffers del juego (ventanas): cb0 del VS (3 float4), cb4 del VS (5 float4), cb0 del PS (5 float4)
static ID3D11Buffer* s_cpVs0 = nullptr; static ID3D11Buffer* s_cpVs4 = nullptr; static ID3D11Buffer* s_cpPs0 = nullptr;
// compute: lista de parcelas en la GPU
static ID3D11ComputeShader* s_cs = nullptr; static bool s_csTried = false;
static ID3D11Buffer* s_cullCb = nullptr;
static ID3D11Buffer* s_instG = nullptr; static ID3D11ShaderResourceView* s_instGSRV = nullptr; static ID3D11UnorderedAccessView* s_instGUAV = nullptr;
static ID3D11Buffer* s_count = nullptr; static ID3D11UnorderedAccessView* s_countUAV = nullptr;
static ID3D11Buffer* s_args = nullptr; static ID3D11UnorderedAccessView* s_argsUAV = nullptr;
static ID3D11Buffer* s_countStage[4] = {}; static int s_countFrame[4] = { -1, -1, -1, -1 };   // (25/10 b) 4 ranuras: se copia solo a una ya leida, asi el Map sin espera no falla siempre
static std::atomic<int> s_emitBase{0}, s_emitGame{0};   // emisiones por origen (para el log)
// (25/10 b) copia propia de la escena (cb2 del VS, 21 float4) para el compute: se copia en la GPU desde la ventana que el juego
// tiene puesta (offset en constantes) en vez de enlazar el buffer del juego con offset al CS. Y una copia staging para el log
// (camara que vio el compute: proyeccion _m11, ojo), para ver desde que camara se armo la lista cuando queda corta.
static ID3D11Buffer* s_sceneCb = nullptr; static ID3D11Buffer* s_sceneStage[4] = {}; static int s_sceneFrame[4] = { -1, -1, -1, -1 };
static std::atomic<int> s_skipNoPs{0};   // dibujos del pasto del juego sin el PS de hojas (prepasada de profundidad / sombras): no se emite ahi
static float s_camM11 = 0, s_camEye[3] = { 0, 0, 0 }; static UINT s_cb2Off = 0, s_cb2N = 0, s_cb2Bytes = 0;

static bool EnsureGPU() {
    if (!g_dev.dev) return false;
    if (!s_csTried) {
        s_csTried = true;
        if (ID3DBlob* b = CompileNamedShader(L"pes_grass_cull_cs.hlsl", "cs_5_0")) {
            if (FAILED(g_dev.dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_cs))) s_cs = nullptr;
            b->Release();
        }
        Log("pasto v3: %s", s_cs ? "compute de parcelas listo" : "no se pudo compilar shaders\\pes_grass_cull_cs.hlsl (ver Errores)");
    }
    if (!s_cs) return false;
    auto cb = [](UINT bytes, ID3D11Buffer** out) { if (*out) return; D3D11_BUFFER_DESC d = {}; d.ByteWidth = bytes; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; g_dev.dev->CreateBuffer(&d, nullptr, out); };
    cb(160, &s_cullCb); cb(64, &s_cpVs0); cb(80, &s_cpVs4); cb(80, &s_cpPs0); cb(352, &s_sceneCb);
    if (!s_instG) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxInst * 16; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = 16;
        if (SUCCEEDED(g_dev.dev->CreateBuffer(&d, nullptr, &s_instG))) {
            D3D11_SHADER_RESOURCE_VIEW_DESC v = {}; v.Format = DXGI_FORMAT_UNKNOWN; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; v.Buffer.FirstElement = 0; v.Buffer.NumElements = kMaxInst;
            if (FAILED(g_dev.dev->CreateShaderResourceView(s_instG, &v, &s_instGSRV))) s_instGSRV = nullptr;
            D3D11_UNORDERED_ACCESS_VIEW_DESC u = {}; u.Format = DXGI_FORMAT_UNKNOWN; u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; u.Buffer.FirstElement = 0; u.Buffer.NumElements = kMaxInst;
            if (FAILED(g_dev.dev->CreateUnorderedAccessView(s_instG, &u, &s_instGUAV))) s_instGUAV = nullptr;
        } else s_instG = nullptr;
    }
    auto raw = [](UINT bytes, UINT misc, ID3D11Buffer** b, ID3D11UnorderedAccessView** u) {
        if (*b) return;
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = bytes; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_UNORDERED_ACCESS; d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS | misc;
        if (FAILED(g_dev.dev->CreateBuffer(&d, nullptr, b))) { *b = nullptr; return; }
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = bytes / 4; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        if (FAILED(g_dev.dev->CreateUnorderedAccessView(*b, &ud, u))) *u = nullptr;
    };
    raw(80, 0, &s_count, &s_countUAV);
    raw(kSlots * 20, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, &s_args, &s_argsUAV);
    for (auto& st : s_countStage) if (!st) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 80; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &st); }
    for (auto& st : s_sceneStage) if (!st) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 352; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &st); }
    return s_cullCb && s_sceneCb && s_cpVs0 && s_cpVs4 && s_cpPs0 && s_instG && s_instGSRV && s_instGUAV && s_count && s_countUAV && s_args && s_argsUAV;
}

// lee el estado del contexto que usa el dibujo del pasto del juego (con referencias; los cbuffers con su ventana)
static void ReadState(ID3D11DeviceContext* c, GrassState& g) {
    c->VSGetShader(&g.vs, nullptr, nullptr); c->PSGetShader(&g.ps, nullptr, nullptr); c->IAGetInputLayout(&g.il); c->IAGetPrimitiveTopology(&g.topo);
    c->IAGetVertexBuffers(0, 4, g.vb, g.stride, g.voff); c->IAGetIndexBuffer(&g.ib, &g.ibFmt, &g.ibOff);
    ID3D11DeviceContext1* c1 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) {
        c1->VSGetConstantBuffers1(0, 1, &g.vsCb0, &g.vsCb0F, &g.vsCb0N); c1->VSGetConstantBuffers1(4, 1, &g.vsCb4, &g.vsCb4F, &g.vsCb4N);
        c1->PSGetConstantBuffers1(0, 1, &g.psCb0, &g.psCb0F, &g.psCb0N); c1->Release();
    } else { c->VSGetConstantBuffers(0, 1, &g.vsCb0); c->VSGetConstantBuffers(4, 1, &g.vsCb4); c->PSGetConstantBuffers(0, 1, &g.psCb0); }
    c->VSGetShaderResources(0, 7, g.vsSrv); c->VSGetSamplers(10, 2, g.vsSmp); c->PSGetShaderResources(7, 1, &g.psSrv); c->PSGetSamplers(10, 1, &g.psSmp);
    c->OMGetBlendState(&g.bs, g.bf, &g.bmask); c->OMGetDepthStencilState(&g.dss, &g.sref); c->RSGetState(&g.rs);
    g.valid = true;
}
static void SetCB(ID3D11DeviceContext* c, ID3D11DeviceContext1* c1, bool ps, UINT slot, ID3D11Buffer* b, UINT first, UINT num) {
    if (c1 && b && num) { if (ps) c1->PSSetConstantBuffers1(slot, 1, &b, &first, &num); else c1->VSSetConstantBuffers1(slot, 1, &b, &first, &num); }
    else { if (ps) c->PSSetConstantBuffers(slot, 1, &b); else c->VSSetConstantBuffers(slot, 1, &b); }
}
// pone el estado (sin los shaders: esos van por BindShadersRaw para que BeforeDraw los vea como el dibujo de pasto del juego)
static void WriteState(ID3D11DeviceContext* c, const GrassState& g) {
    c->IASetInputLayout(g.il); c->IASetPrimitiveTopology(g.topo);
    c->IASetVertexBuffers(0, 4, g.vb, g.stride, g.voff); c->IASetIndexBuffer(g.ib, g.ibFmt, g.ibOff);
    ID3D11DeviceContext1* c1 = nullptr; c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1);
    SetCB(c, c1, false, 0, g.vsCb0, g.vsCb0F, g.vsCb0N); SetCB(c, c1, false, 4, g.vsCb4, g.vsCb4F, g.vsCb4N); SetCB(c, c1, true, 0, g.psCb0, g.psCb0F, g.psCb0N);
    if (c1) c1->Release();
    c->VSSetShaderResources(0, 7, g.vsSrv); c->VSSetSamplers(10, 2, g.vsSmp); c->PSSetShaderResources(7, 1, &g.psSrv); c->PSSetSamplers(10, 1, &g.psSmp);
    c->OMSetBlendState(g.bs, g.bf, g.bmask); c->OMSetDepthStencilState(g.dss, g.sref); c->RSSetState(g.rs);
}
// copia la ventana de un cbuffer del juego a un buffer propio (bytes = tamano del propio)
static void CopyCBWindow(ID3D11DeviceContext* c, ID3D11Buffer* src, UINT first, UINT num, ID3D11Buffer* dst, UINT bytes) {
    if (!src || !dst) return;
    D3D11_BUFFER_DESC d; src->GetDesc(&d); UINT off = first * 16;
    if (off >= d.ByteWidth) return;
    UINT len = (std::min)(bytes, d.ByteWidth - off); if (num) len = (std::min)(len, num * 16);
    len &= ~15u; if (!len) return;
    D3D11_BOX bx = { off, 0, 0, off + len, 1, 1 }; c->CopySubresourceRegion(dst, 0, 0, 0, 0, src, 0, &bx);
}
// guarda el estado del dibujo de pasto del juego en curso (una vez por cuadro) para emitir desde la base de la cancha
static void CaptureState(ID3D11DeviceContext* c, const ShaderEntry* vs, const ShaderEntry* ps) {
    if (!EnsureGPU()) return;
    GrassState n; ReadState(c, n); n.vsE = vs; n.psE = ps;
    CopyCBWindow(c, n.vsCb0, n.vsCb0F, n.vsCb0N, s_cpVs0, 64);
    CopyCBWindow(c, n.vsCb4, n.vsCb4F, n.vsCb4N, s_cpVs4, 80);
    CopyCBWindow(c, n.psCb0, n.psCb0F, n.psCb0N, s_cpPs0, 80);
    // en el estado guardado quedan las copias (ventana entera, sin offset)
    if (n.vsCb0) n.vsCb0->Release(); if (n.vsCb4) n.vsCb4->Release(); if (n.psCb0) n.psCb0->Release();
    n.vsCb0 = s_cpVs0; n.vsCb4 = s_cpVs4; n.psCb0 = s_cpPs0; n.vsCb0F = n.vsCb4F = n.psCb0F = 0; n.vsCb0N = n.vsCb4N = n.psCb0N = 0;
    s_cpVs0->AddRef(); s_cpVs4->AddRef(); s_cpPs0->AddRef();
    GrassState old;
    { std::lock_guard<std::mutex> lk(s_stMx); old = s_state; s_state = n; }
    old.Release();
}

// (25/10) ultimo cuadro en el que el mod emitio parcelas de verdad: si el cuadro anterior no emitio (sin pasto del juego
// en esa camara, pasada secundaria, 0 parcelas), x63 = 1 y el VS no achica el pasto del juego
static std::atomic<int> s_okFrame{-1};
// niveles de la grilla: celda, tope de instancias, objetivo de indices de la malla, ensanche de la hoja
struct Level { float cell; UINT cap; UINT target; float widthMul; };

// umbrales de tamano en pantalla (pixeles por metro): mas = mas cerca. x58 (densidad) los corre: 1.5 = cada nivel llega
// 1.5x mas lejos. T0: hasta aca el pasto del juego (una hoja de 1.2 cm mide >= 2 px); T1 / T2: limites de los niveles.
static void Thresholds(float* T) {
    // (25/10 c) fijos: la densidad (x58) ya no corre los niveles, cambia cuantas tarjetas lleva cada parcela (densidad
    // constante en pantalla, ver GrassEmit). El pasto del juego queda solo pegado a la camara (>= 2000 px/m)
    // (26/10) T0 = 2000: el pasto del juego solo a menos de ~1 m de la camara. Con 240 la zona cercana (pasto denso del
    // juego, ~19 capas, muy oscura) chocaba con la del mod (6 capas): borde visible, pasto irregular y 13 FPS en primeros planos
    T[0] = 2000.0f; T[1] = 110.0f; T[2] = 45.0f;
    T[3] = 15.0f;   // (25/10) limite inferior del nivel lejano cuando el cesped lejano de la base (y61) esta prendido
}

void GrassPresent() {
    { int prev = s_frame.load(); g_cfg.live[63][0] = (s_okFrame.load() == prev) ? 0.0f : 1.0f; }
    int f = s_frame.fetch_add(1) + 1;
    // umbrales para el VS del pasto del juego (y63 = 0: modo viejo por distancia)
    float T[4]; Thresholds(T);
    { bool px = s_cs && g_cfg.live[58][3] > 0.5f; g_cfg.live[63][1] = px ? T[0] : 0.0f; g_cfg.live[63][2] = 0.5f * (float)(g_bbH ? g_bbH : 1080); }
    // contadores de parcelas por nivel (del cuadro anterior, solo para el log)
    if (g_dev.imm) for (int i = 0; i < 4; i++) if (s_countStage[i] && s_countFrame[i] >= 0 && s_countFrame[i] <= f - 3) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_dev.imm->Map(s_countStage[i], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
            const UINT* q = (const UINT*)m.pData; for (int k = 0; k < 3; k++) { int n = 0; for (int b = 0; b < kBins; b++) n += (int)q[k * kBins + b]; g_grassDbg.ring[k] = n; }
            g_dev.imm->Unmap(s_countStage[i], 0); s_countFrame[i] = -1;
            s_lastCount = g_grassDbg.ring[0] + g_grassDbg.ring[1] + g_grassDbg.ring[2]; g_grassDbg.patches = s_lastCount;
        }
    }
    if (g_dev.imm) for (int i = 0; i < 4; i++) if (s_sceneStage[i] && s_sceneFrame[i] >= 0 && s_sceneFrame[i] <= f - 3) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_dev.imm->Map(s_sceneStage[i], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
            const float* q = (const float*)m.pData; s_camM11 = q[5 * 4 + 1]; s_camEye[0] = q[20 * 4]; s_camEye[1] = q[20 * 4 + 1]; s_camEye[2] = q[20 * 4 + 2];
            g_dev.imm->Unmap(s_sceneStage[i], 0); s_sceneFrame[i] = -1;
        }
    }
    g_grassPatchesLastFrame = s_lastCount;
    if (s_vbState.load() == 1 && s_vbStage && g_dev.imm) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_dev.imm->Map(s_vbStage, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
            const unsigned char* b = (const unsigned char*)m.pData; const unsigned short* h = (const unsigned short*)m.pData; const float* fl = (const float*)m.pData;
            auto half = [](unsigned short x) { unsigned s = (x >> 15) & 1, e = (x >> 10) & 31, mt = x & 1023; float v; if (e == 0) v = ldexpf((float)mt, -24); else if (e == 31) v = 0.f; else v = ldexpf((float)(mt | 1024), (int)e - 25); return s ? -v : v; };
            std::string line = "pasto: formato de vertices (stride " + std::to_string(s_vbStride) + ", offset " + std::to_string(s_vbOffset) + ", IB fmt " + std::to_string(s_ibFmt) + ")\n  hex:";
            for (int i = 0; i < 48; i++) { char t[8]; snprintf(t, 8, " %02x", b[i]); line += t; if (i % 12 == 11) line += " |"; }
            line += "\n  como half:"; for (int i = 0; i < 24; i++) { char t[24]; snprintf(t, 24, " %.3f", half(h[i])); line += t; if (i % 6 == 5) line += " |"; }
            line += "\n  como float:"; for (int i = 0; i < 12; i++) { char t[24]; snprintf(t, 24, " %.3f", fl[i]); line += t; if (i % 3 == 2) line += " |"; }
            line += "\n  slot 1 hex:"; for (int i = 256; i < 256 + 32; i++) { char t[8]; snprintf(t, 8, " %02x", b[i]); line += t; if ((i - 256) % 8 == 7) line += " |"; }
            // (20/10) slot 1 = normal half4 @0, tangente half4 @8, uv half2 @16 (stride 20): se listan 12 vertices con su posicion
            line += "\n  vertices (pos | normal | tangente | uv):";
            for (int v = 0; v < 12; v++) { const float* pv = fl + v * 3; const unsigned short* hv = h + 128 + v * 10; char t[200];
                snprintf(t, sizeof t, "\n   %2d: (%.3f %.3f %.3f) | (%.2f %.2f %.2f) | (%.2f %.2f %.2f) | uv (%.3f %.3f)", v, pv[0], pv[1], pv[2], half(hv[0]), half(hv[1]), half(hv[2]), half(hv[4]), half(hv[5]), half(hv[6]), half(hv[8]), half(hv[9])); line += t; }
            line += "\n  indices:"; for (int i = 0; i < 48; i++) { char t[12]; snprintf(t, 12, " %u", (unsigned)h[384 + i]); line += t; if (i % 3 == 2) line += " |"; }
            Log("%s", line.c_str());
            g_dev.imm->Unmap(s_vbStage, 0); s_vbState = 2;
        }
    }
    static double nextLog = 0; double t = Now();
    if (t > nextLog && g_cfg.live[58][3] > 0.5f) { nextLog = t + 3.0;
        Log("pasto v3: llamadas %d, emisiones %d (desde la base %d, desde el pasto del juego %d), parcelas %d (niveles %d/%d/%d), malla cercana %u idx, estado: %s | v4: %d dibujos con manojos curvos | camara del compute: m11 %.2f ojo (%.1f %.1f %.1f) cb2 off %u n %u de %u B, umbrales %.0f/%.0f/%.0f px/m, dibujos del pasto sin PS de hojas %d",
            g_grassDbg.calls, g_grassDbg.emits, s_emitBase.exchange(0), s_emitGame.exchange(0), g_grassDbg.patches, g_grassDbg.ring[0], g_grassDbg.ring[1], g_grassDbg.ring[2], g_grassDbg.lod,
            g_grassDbg.why ? g_grassDbg.why : "-", g_denseDraws.exchange(0), s_camM11, s_camEye[0], s_camEye[1], s_camEye[2], s_cb2Off, s_cb2N, s_cb2Bytes, T[0], T[1], T[2], s_skipNoPs.exchange(0));
        g_grassDbg.calls = 0; g_grassDbg.emits = 0; g_grassDbg.sparseLast = g_grassDbg.sparse; g_grassDbg.sparse = 0; }
}

// emite el pasto del mod en este cuadro. bound = el estado del dibujo de pasto del juego ya esta puesto (se llama desde
// ese dibujo); si no, se pone el guardado y se devuelve el del contexto al terminar.
static void GrassEmit(ID3D11DeviceContext* c, int f, bool bound) {
    const float* p58 = g_cfg.live[58]; const float* p59 = g_cfg.live[59];
    EnsureBuffers();
    if (!s_cb) { Why("sin buffers propios"); return; }
    if (!EnsureGPU()) { Why("sin recursos de GPU para el pasto (ver Errores)"); return; }
    if (s_lodCount.load() == 0xffffffff) { Why("todavia sin variante de malla"); return; }
    GrassState st;
    if (!bound) {
        std::lock_guard<std::mutex> lk(s_stMx);
        if (!s_state.valid) { Why("todavia no se vio el pasto del juego (sin estado para emitir desde la base)"); return; }
        st = s_state; st.AddRefAll();
    }
    g_grassDbg.emits++; if (bound) s_emitGame++; else s_emitBase++;
    float T[4]; Thresholds(T);
    float tall = p59[0] > 0.05f ? p59[0] : 1.3f;
    float wide = p59[1] >= 0.0f ? p59[1] : 1.0f;
    int q = (int)(g_cfg.live[61][0] + 0.5f); q = (std::max)(0, (std::min)(q, 3));
    // (25/10 c) densidad constante en pantalla: cada parcela lleva las tarjetas que hacen falta para cubrir su tamano en
    // pantalla (tarjetas = C(p) * (celda * p)^2 / ancho_px, con p = pixeles por metro; C sube suave hacia la camara), con
    // la malla propia de manojos curvos (orden de Halton: las primeras N tarjetas estan bien repartidas) en 6 subniveles
    // (9248 >> b tarjetas) y un tope exacto por parcela que el VS aplica por SV_VertexID. Calidad (x61): cuanto cubre y el
    // ancho minimo de la hoja en pixeles (hojas de menos de ~1.4 px titilan). Densidad (x58) multiplica C.
    static const float cMul[4] = { 0.6f, 1.0f, 1.4f, 1.8f }, minPxQ[4] = { 1.8f, 1.5f, 1.3f, 1.2f };
    float dens = g_cfg.live[58][0] > 0.05f ? std::clamp(g_cfg.live[58][0], 0.3f, 2.0f) : 1.0f;
    const float cBase = 0.30f * cMul[q] * dens, minPx = minPxQ[q];
    Level L[3] = { { 0.5f, kSlotCap, 0, 1.0f }, { 1.0f, kSlotCap, 0, 1.6f }, { 2.0f, kSlotCap, 0, 2.0f } };
    // (25/10) con el cesped lejano de la base prendido (y61) el nivel 3D lejano se apaga por debajo de T3 px/m y la base lo sigue
    // (25/10 c) el fundido entre niveles es por CANTIDAD de tarjetas en el compute (nivel saliente lim*k, entrante lim*(1-k), mismo
    // margen 0.88 / 1.12 que el pasto del juego), no por altura en el VS: antes en la banda se dibujaban los dos niveles enteros y
    // todas las hojas quedaban a media altura a una distancia fija de la camara
    const bool farBase = g_cfg.live[61][1] > 0.001f; const float loFar = farBase ? T[3] : 0.0f; const float fadeM = 0.12f;
    // malla propia (manojos curvos de 2 tramos: 6 vertices / 12 indices por tarjeta)
    float curv = g_cfg.live[123][0] > 0.5f ? std::clamp(g_cfg.live[123][2], 0.0f, 1.5f) : 0.6f;
    ID3D11Buffer* mPos[3] = {}; ID3D11Buffer* mUv[3] = {}; ID3D11Buffer* mIb[3] = {}; UINT mCards = 0, mIpc = 12, mVpc = 6;
    {
        std::lock_guard<std::mutex> lk(s_denseMx);
        static float triedCurv = -2.0f;   // si la malla no se pudo crear, no se reintenta en cada cuadro (log y CPU)
        if ((s_denseBuilt.load() && fabsf(curv - s_denseCurv) > 0.01f) || (!s_denseBuilt.load() && fabsf(curv - triedCurv) > 0.01f)) { triedCurv = curv; BuildDense(curv); }
        if (s_denseBuilt.load()) for (int i = 0; i < 3; i++) { DenseLevel& D = s_denseExt[i]; mPos[i] = D.pos; mUv[i] = D.uv; mIb[i] = D.ib; mCards = D.cards; mIpc = D.idxPerCard; mVpc = D.vertsPerCard; mPos[i]->AddRef(); mUv[i]->AddRef(); mIb[i]->AddRef(); }
    }
    if (!mPos[0] || !mPos[1] || !mPos[2]) { Why("sin malla propia de manojos (ver Errores)"); if (!bound) st.Release(); return; }
    g_grassDbg.lod = mCards * mIpc;
    const float halfL = 52.5f + 3.0f, halfW = 34.0f + 3.0f, halfH = 0.5f * (float)(g_bbH ? g_bbH : 1080);
    ID3D11DeviceContext1* c1 = nullptr; c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1);
    // estado del contexto que se toca (se devuelve al final)
    GrassState prev; ShaderBind oldSh = {};
    if (!bound) {
        ReadState(c, prev);
        ShaderBind b; b.vs = st.vsE; b.vsObj = st.vs; b.ps = st.psE; b.psObj = st.ps; oldSh = BindShadersRaw(c, b);
        WriteState(c, st);
    }
    ID3D11Buffer* oldCb5 = nullptr; UINT oldF5 = 0, oldN5 = 0;
    if (c1) c1->VSGetConstantBuffers1(5, 1, &oldCb5, &oldF5, &oldN5); else c->VSGetConstantBuffers(5, 1, &oldCb5);
    ID3D11ShaderResourceView* oldT = nullptr; c->VSGetShaderResources(121, 1, &oldT);
    // --- compute: lista de parcelas con la camara de este cuadro (cb2 del VS tal como esta enlazado) ---
    ID3D11Buffer* cb2 = nullptr; UINT f2 = 0, n2 = 0;
    if (c1) c1->VSGetConstantBuffers1(2, 1, &cb2, &f2, &n2); else c->VSGetConstantBuffers(2, 1, &cb2);
    bool ok = cb2 != nullptr;
    if (!ok) Why("sin cb2 (escena) en el VS");
    if (ok) {
        ID3D11ComputeShader* oldCS = nullptr; c->CSGetShader(&oldCS, nullptr, nullptr);
        ID3D11Buffer* oldCB[3] = {}; c->CSGetConstantBuffers(0, 3, oldCB);
        ID3D11UnorderedAccessView* oldUAV[3] = {}; c->CSGetUnorderedAccessViews(0, 3, oldUAV);
        c->CSSetShader(s_cs, nullptr, 0);
        {   // (25/10 b) la escena se copia a un buffer propio (desde la ventana del juego) y el CS la lee sin offset
            D3D11_BUFFER_DESC bd = {}; cb2->GetDesc(&bd);
            UINT off = (c1 && n2) ? f2 * 16 : 0, bytes = 336;
            if (off + bytes > bd.ByteWidth) bytes = off < bd.ByteWidth ? bd.ByteWidth - off : 0;
            s_cb2Off = off; s_cb2N = n2; s_cb2Bytes = bd.ByteWidth;
            if (bytes) { D3D11_BOX bx = { off, 0, 0, off + bytes, 1, 1 }; c->CopySubresourceRegion(s_sceneCb, 0, 0, 0, 0, cb2, 0, &bx); }
            for (int i = 0; i < 4; i++) if (s_sceneStage[i] && s_sceneFrame[i] < 0) { c->CopyResource(s_sceneStage[i], s_sceneCb); s_sceneFrame[i] = f; break; }
            c->CSSetConstantBuffers(2, 1, &s_sceneCb);
        }
        c->CSSetConstantBuffers(0, 1, &s_cullCb);
        UINT zeros[20] = {}; c->UpdateSubresource(s_count, 0, nullptr, zeros, 0, 0);
        ID3D11UnorderedAccessView* uavs[3] = { s_instGUAV, s_countUAV, s_argsUAV }; UINT ini[3] = { (UINT)-1, (UINT)-1, (UINT)-1 };
        c->CSSetUnorderedAccessViews(0, 3, uavs, ini);
        // gP0 (nivel, celda, pLo, pHi) gP1 (mitad alto, tope por ranura, primera ranura del nivel, celdas a lo largo)
        // gP2 (celdas a lo ancho, margen, semi largo, semi ancho) gP3 (ancho de la hoja m, ancho minimo px, C base, indices por tarjeta)
        // gP4 (tarjetas de la malla, subniveles, 0, 0)
        float cull[40];
        for (int k = 0; k < 40; k++) cull[k] = 0.f;
        cull[4] = halfH; cull[5] = (float)kSlotCap; cull[9] = fadeM; cull[10] = halfL; cull[11] = halfW;
        cull[13] = minPx; cull[14] = cBase; cull[15] = (float)mIpc; cull[16] = (float)mCards; cull[17] = (float)kBins;
        // gP4.z alto de la hoja (m), gP4.w tope de capas de hojas por pixel: con la camara baja la celda se ve como una banda angosta y la
        // cuenta de frente ((celda * p)^2) sobraba 14-46x; el tope sigue a Calidad / Densidad
        cull[18] = 0.058f * tall; cull[19] = 6.0f * cMul[q] * dens;
        for (int i = 0; i < 3; i++) {
            UINT gw = (UINT)ceilf(2.0f * halfL / L[i].cell), gh = (UINT)ceilf(2.0f * halfW / L[i].cell);
            cull[0] = (float)i; cull[1] = L[i].cell; cull[2] = i == 2 ? loFar : T[i + 1]; cull[3] = T[i];   // nivel 2: sin limite inferior (toda la cancha) salvo con cesped lejano en la base
            cull[6] = (float)(i * kBins); cull[7] = (float)gw; cull[8] = (float)gh;
            cull[12] = 0.012f * wide * L[i].widthMul * (L[i].cell / 0.5f);   // ancho de la hoja en metros (la malla escala con la celda)
            c->UpdateSubresource(s_cullCb, 0, nullptr, cull, 0, 0);
            c->Dispatch((gw + 7) / 8, (gh + 7) / 8, 1);
        }
        cull[0] = 3.0f; c->UpdateSubresource(s_cullCb, 0, nullptr, cull, 0, 0); c->Dispatch(1, 1, 1);
        ID3D11UnorderedAccessView* nul[3] = {}; c->CSSetUnorderedAccessViews(0, 3, nul, nullptr);
        for (int i = 0; i < 4; i++) if (s_countStage[i] && s_countFrame[i] < 0) { c->CopyResource(s_countStage[i], s_count); s_countFrame[i] = f; break; }
        c->CSSetShader(oldCS, nullptr, 0); c->CSSetConstantBuffers(0, 3, oldCB); c->CSSetUnorderedAccessViews(0, 3, oldUAV, nullptr);
        if (oldCS) oldCS->Release(); for (auto* b : oldCB) if (b) b->Release(); for (auto* u : oldUAV) if (u) u->Release();
        cb2->Release();
        // --- malla propia en la entrada (posicion en el slot 0, uv en el slot 3, indices de 32 bits), una por nivel ---
        ID3D11Buffer* svVb[4] = {}; UINT svSt[4] = {}, svOf[4] = {}; c->IAGetVertexBuffers(0, 4, svVb, svSt, svOf);
        ID3D11Buffer* svIb = nullptr; DXGI_FORMAT svFmt = DXGI_FORMAT_UNKNOWN; UINT svIo = 0; c->IAGetIndexBuffer(&svIb, &svFmt, &svIo);
        int boundMesh = -1;
        // --- dibujos: uno por ranura (nivel x subnivel), indirectos (pasan por nuestro gancho: reglas y t120) ---
        c->VSSetShaderResources(121, 1, &s_instGSRV);
        float cbq[72];
        for (int sl = 0; sl < kSlots; sl++) {
            int i = sl / kBins;
            if (boundMesh != i) { ID3D11Buffer* vbs[4] = { mPos[i], svVb[1], svVb[2], mUv[i] }; UINT sts[4] = { 12, svSt[1], svSt[2], 4 }, ofs2[4] = { 0, svOf[1], svOf[2], 0 };
                c->IASetVertexBuffers(0, 4, vbs, sts, ofs2); c->IASetIndexBuffer(mIb[i], DXGI_FORMAT_R32_UINT, 0); boundMesh = i; }
            for (int k = 0; k < 72; k++) cbq[k] = 0.f;
            // cb5[8]: (vertices por tarjeta, marca, alto, ancho base)  cb5[9]: (magenta, offset de instancias, 1 = raices ya escaladas en la malla, -)
            // cb5[10]: (ancho minimo px, -, multiplicador de ancho, mitad del alto de pantalla)  cb5[11]: (pLo, pHi, 1 = por tamano, margen)
            // pLo / pHi = 0: el VS no funde por altura (el fundido es por cantidad en el compute)
            cbq[32] = (float)mVpc; cbq[33] = 1234.5f; cbq[34] = tall; cbq[35] = 0.012f * wide;
            cbq[36] = p59[2] > 0.5f ? 1.f : 0.f; cbq[37] = (float)(sl * kSlotCap); cbq[38] = 1.0f;
            cbq[40] = minPx; cbq[42] = L[i].widthMul; cbq[43] = halfH;
            cbq[44] = 0.0f; cbq[45] = 0.0f; cbq[46] = 1.0f; cbq[47] = fadeM;
            c->UpdateSubresource(s_cb, 0, nullptr, cbq, 0, 0);
            c->VSSetConstantBuffers(5, 1, &s_cb);
            c->DrawIndexedInstancedIndirect(s_args, (UINT)sl * 20);
        }
        c->IASetVertexBuffers(0, 4, svVb, svSt, svOf); c->IASetIndexBuffer(svIb, svFmt, svIo);
        for (auto* b : svVb) if (b) b->Release(); if (svIb) svIb->Release();
        s_okFrame.store(f);
        Why(bound ? "emitiendo (desde el pasto del juego)" : "emitiendo (desde la base de la cancha)");
    }
    for (int i = 0; i < 3; i++) { mPos[i]->Release(); mUv[i]->Release(); mIb[i]->Release(); }
    // --- devolver el estado ---
    c->VSSetShaderResources(121, 1, &oldT); if (oldT) oldT->Release();
    if (oldCb5) { if (c1 && oldN5) c1->VSSetConstantBuffers1(5, 1, &oldCb5, &oldF5, &oldN5); else c->VSSetConstantBuffers(5, 1, &oldCb5); oldCb5->Release(); }
    else { ID3D11Buffer* nulb = nullptr; c->VSSetConstantBuffers(5, 1, &nulb); }
    if (!bound) {
        WriteState(c, prev);
        BindShadersRaw(c, oldSh); if (oldSh.vsObj) oldSh.vsObj->Release(); if (oldSh.psObj) oldSh.psObj->Release();
        prev.Release(); st.Release();
    }
    if (c1) c1->Release();
}

// cuantos render targets hay puestos (el G-buffer tiene varios; una prepasada de profundidad, 0 o 1)
static int RtvCount(ID3D11DeviceContext* c) {
    ID3D11RenderTargetView* r[8] = {}; c->OMGetRenderTargets(8, r, nullptr);
    int n = 0; for (auto* v : r) if (v) { n++; v->Release(); }
    return n;
}

// pasada principal? (los targets de otro tamano son sombras / reflejos)
static bool MainPass(ID3D11DeviceContext* c) {
    if (!g_bbW) return true;
    ID3D11RenderTargetView* rtv = nullptr; c->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return false;
    ID3D11Resource* res = nullptr; rtv->GetResource(&res); rtv->Release();
    ID3D11Texture2D* t = nullptr; UINT w = 0;
    if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); w = d.Width; t->Release(); }
    if (res) res->Release();
    g_grassDbg.rtW = w;
    return w == g_bbW;
}

void GrassOnDraw(ID3D11DeviceContext* c, const ShaderEntry* vs, const ShaderEntry* ps, UINT count, UINT start, INT base) {
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
    const float* p58 = g_cfg.live[58];
    bool extraOn = p58[3] > 0.5f;
    // con el pasto extra prendido y emitiendo, el escalado de las parcelas lejanas del juego no se usa (lo reemplazan los niveles)
    if ((!extraOn || g_cfg.live[63][0] > 0.5f) && count == s_lodCount.load() && g_cfg.live[60][3] > 0.5f) { t_sparseGrass = true; g_grassDbg.sparse++; }
    if (!MainPass(c)) { if (extraOn) Why("pasada secundaria (target de otro tamano)"); return; }
    // (25/10 b) el juego dibuja su pasto tambien sin el PS de hojas (prepasada de profundidad, pasadas auxiliares con un
    // target del mismo tamano): si se emitiera ahi, las hojas del mod saldrian sin color (invisibles) y el estado guardado
    // para la base tendria un PS nulo. Solo cuenta el dibujo con el PS de hojas del G-buffer.
    if (!ps || (ps->hash != kGrassPS && RtvCount(c) < 2)) { s_skipNoPs++; if (extraOn) Why("dibujo del pasto del juego sin el PS de hojas / sin G-buffer (prepasada): se espera al del G-buffer"); return; }
    int f = s_frame.load();
    // (25/10) estado del dibujo de pasto del juego, una vez por cuadro: con el se emite desde la base de la cancha
    if (extraOn) { int e = s_capFrame.load(); if (e != f && s_capFrame.compare_exchange_strong(e, f)) CaptureState(c, vs, ps); }
    PitchBallNote(c, f);    // (20/10) posicion de la pelota para el pasto aplastado
    PitchClipsDraw(c, f);   // recortes de pasto volando (una vez por cuadro; el estado del pasto ya esta puesto)
    // (20/10) pasto v4: tarjetas curvas propias en vez de las del juego (cerca). (26/10) Con el pasto del mod emitiendo y T0 >= 1000
    // el VS anula casi todas las parcelas del juego: no vale la pena cambiarles la malla (1.4x mas vertices que se anulan igual)
    { float T[4]; Thresholds(T); bool denseSkip = extraOn && T[0] >= 1000.0f && g_cfg.live[63][0] < 0.5f;
      if (!t_grassSub.count && !denseSkip) DensePick(count, &t_grassSub); }
    if (!extraOn) { Why("apagado (w58)"); return; }
    int exp = s_emittedFrame.load();
    if (exp == f) return;
    if (!s_emittedFrame.compare_exchange_strong(exp, f)) return;
    GrassEmit(c, f, true);
}

// (25/10) al terminar el dibujo de la base de la cancha (PS 5e92 / c956): si en este cuadro todavia no se emitio pasto
// del mod, se emite aca con el estado guardado. La base existe en todas las camaras, el pasto del juego no.
void GrassOnBaseDraw(ID3D11DeviceContext* c) {
    if (g_cfg.bypass || !g_dev.dev || g_cfg.live[58][3] < 0.5f) return;
    int f = s_frame.load();
    if (s_emittedFrame.load() == f) return;
    if (!MainPass(c)) return;
    int exp = s_emittedFrame.load();
    if (exp == f) return;
    if (!s_emittedFrame.compare_exchange_strong(exp, f)) return;
    GrassEmit(c, f, false);
}

} // namespace fx
