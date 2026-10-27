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
//   x63 (DLL) 1 = el mod no esta dibujando (latcheado desde lo ejecutado en la GPU; el VS no apaga el pasto del juego); y63 umbral
//   del pasto del juego (px/m, 0 = modo por distancia); z63 mitad del alto de pantalla (px)
#include "core.h"
#include <d3d11_1.h>
#include <atomic>
#include <cmath>
#include <vector>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <map>

namespace fx {

static const uint64_t kGrassVS = 0x498b30bad256e8edULL;
static const uint64_t kGrassPS = 0x18afff0f79d9aaa5ULL;   // PS de las hojas (G-buffer): solo desde ese dibujo se guarda estado y se emite
// (25/10 c) 3 niveles x 6 subniveles de densidad (tarjetas por parcela: 9248 >> b), 8192 parcelas por ranura (con la
// camara cenital todas las celdas de un nivel caen en la misma ranura: a 4K el nivel 0 llega a ~4800 celdas)
static const int kSlotCap = 8192, kBins = 6, kSlots = 18;
static const int kMaxInst = kSlotCap * kSlots;
struct GInst { float x, z, s, r; };   // instancia: posicion, escala, giro (multiplos de 90 grados)

static std::atomic<int> s_frame{0};   // numero de Present (lo sube GrassFrameBoundary)
// (26/10 d) una emision por GRABACION (contexto / lista de comandos), no por cuadro de Present: los dibujos se graban en
// contextos diferidos y su lista se ejecuta en otro intervalo de Present, asi que el contador de Present leido al grabar no
// identifica el cuadro de la GPU (quedaban cuadros con 0 emisiones = cancha pelada un cuadro, y otros con 2). Cada grabacion
// lleva una emision; la GPU decide cual de las que ejecuta en un cuadro dibuja de verdad (compuerta en el compute: la primera
// que llega tras el reinicio de Present, ver GrassFrameBoundary). Las demas salen con 0 instancias.
struct GRec { bool emitted = false, captured = false, clips = false, ball = false, base = false; int pres = -1; int turfN = 0; };   // turfN (26/10 g): dibujos de la base en esta grabacion   // base (26/10 f): ya se dibujo la base de la cancha en esta grabacion
static std::mutex s_recMx; static std::unordered_map<ID3D11DeviceContext*, GRec> s_rec;
struct GList { int recPres; };
static std::unordered_map<ID3D11CommandList*, GList> s_lists;   // listas terminadas con una emision adentro (hasta que se ejecutan)
static std::atomic<int> s_recDeferred{0}, s_recImmediate{0};   // emisiones grabadas por tipo de contexto (log)
static int s_lagHist[3] = {}, s_execHist[3] = {};              // grabacion -> ejecucion (Presents: 0 / 1 / 2+), emisiones ejecutadas por cuadro (0 / 1 / 2+)
static int s_framesRead = 0;
static bool RecClaim(ID3D11DeviceContext* c, bool GRec::*f) {
    std::lock_guard<std::mutex> lk(s_recMx); GRec& r = s_rec[c]; int now = s_frame.load();
    if (r.pres >= 0 && now - r.pres > 4) r = GRec();   // contexto cuyo FinishCommandList no se vio: se suelta solo
    if (r.*f) return false;
    r.*f = true; r.pres = now; return true;
}
static void RecUnclaim(ID3D11DeviceContext* c, bool GRec::*f) { std::lock_guard<std::mutex> lk(s_recMx); auto it = s_rec.find(c); if (it != s_rec.end()) it->second.*f = false; }
static bool RecHas(ID3D11DeviceContext* c, bool GRec::*f) { std::lock_guard<std::mutex> lk(s_recMx); auto it = s_rec.find(c); return it != s_rec.end() && it->second.*f && it->second.pres >= 0 && s_frame.load() - it->second.pres <= 4; }
// FinishCommandList: lo grabado pasa a la lista; el contexto arranca de cero
void GrassOnFinishCL(ID3D11DeviceContext* c, ID3D11CommandList* l) {
    std::lock_guard<std::mutex> lk(s_recMx);
    auto it = s_rec.find(c); if (it == s_rec.end()) return;
    if (l && it->second.emitted && s_lists.size() < 64) s_lists[l] = { it->second.pres };
    s_rec.erase(it);
}
// ExecuteCommandList: cuantos Presents pasaron desde la grabacion (log); una lista anidada en otro contexto diferido le deja su emision
void GrassOnExecuteCL(ID3D11DeviceContext* c, ID3D11CommandList* l) {
    std::lock_guard<std::mutex> lk(s_recMx);
    auto it = s_lists.find(l); if (it == s_lists.end()) return;
    int lag = s_frame.load() - it->second.recPres; s_lagHist[lag <= 0 ? 0 : (lag == 1 ? 1 : 2)]++;
    if (c != g_dev.imm) { GRec& r = s_rec[c]; r.emitted = true; r.pres = s_frame.load(); }
    s_lists.erase(it);
}
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
static std::atomic<float> s_adapt{1.0f};   // (27/10 o) densidad adaptativa por FPS (fila 333)
static float Halton(int i, int b) { float f = 1.0f, r = 0.0f; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; }
static void DenseRelease() { auto rel = [](DenseLevel& L) { if (L.pos) L.pos->Release(); if (L.uv) L.uv->Release(); if (L.ib) L.ib->Release(); L = DenseLevel(); }; for (auto& L : s_dense) rel(L); for (auto& L : s_denseExt) rel(L); s_denseBuilt = false; }
static void BuildDense(float curv) {
    DenseRelease(); if (!g_dev.dev) return;
    const UINT kMaxCards = 9248;                       // hasta 2x las 4624 tarjetas del juego
    uint32_t rs = 0x9E3779B9u; auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs & 0xffffff) / 16777216.0f; };
    for (int lvl = 0; lvl < 5; lvl++) {
        // 0 / 1: las del juego (3 / 2 tramos, parcela de 0.5 m); 2 / 3 / 4: las del mod (2 tramos, celda de 0.5 / 1 / 2 m)
        // (26/10 d) niveles 1 / 2 del mod de 1 tramo: a <= 110 px/m la curva del vertice medio no llega a 0.3 px y costaba 1.3 M vertices en la camara de TV
        int segs = lvl == 0 ? 3 : (lvl >= 3 ? 1 : 2); UINT vpc = (UINT)(segs + 1) * 2, ipc = (UINT)segs * 6;
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
        if (g_cfg.live[58][3] > 0.5f) { static const float cMulQ[5] = { 0.6f, 1.0f, 1.4f, 1.8f, 2.4f }; int q = std::clamp((int)(g_cfg.live[61][0] + 0.5f), 0, 4);
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
static std::atomic<int> s_capPres{-1};                      // Present en el que se guardo (edad del estado para la sombra plana t5)
static std::atomic<int> s_t5Same{0}, s_t5Diff{0};           // (26/10 d) la textura de sombra plana (t5) del dibujo vivo vs la guardada
// copias propias de los cbuffers del juego (ventanas): cb0 del VS (3 float4), cb4 del VS (5 float4), cb0 del PS (5 float4)
static ID3D11Buffer* s_cpVs0 = nullptr; static ID3D11Buffer* s_cpVs4 = nullptr; static ID3D11Buffer* s_cpPs0 = nullptr;
static ID3D11Buffer* s_cpVs2 = nullptr;   // copia de la escena del VS (b2) del dibujo de la base, para emitir despues con la misma camara
// compute: lista de parcelas en la GPU
static ID3D11ComputeShader* s_cs = nullptr; static bool s_csTried = false;
static ID3D11Buffer* s_cullCb = nullptr;
static ID3D11Buffer* s_instG = nullptr; static ID3D11ShaderResourceView* s_instGSRV = nullptr; static ID3D11UnorderedAccessView* s_instGUAV = nullptr;
static ID3D11Buffer* s_count = nullptr; static ID3D11UnorderedAccessView* s_countUAV = nullptr;
static ID3D11Buffer* s_args = nullptr; static ID3D11UnorderedAccessView* s_argsUAV = nullptr;
// (26/10 d) reclamo del cuadro en la GPU: 96 B (u3). [0] emisiones ejecutadas desde el ultimo Present, [1..18] instancias por ranura de la
// emision que dibujo, [19] _m11 de su camara, [20..22] ojo. Se copia a un staging y se pone en cero en el contexto inmediato en cada Present:
// lo que se lee 3 cuadros despues es la verdad del cuadro ejecutado (los staging viejos se copiaban dentro de la lista diferida y el Map sin
// espera devolvia datos viejos)
static const int kClaimStages = 6;
static ID3D11Buffer* s_claim = nullptr; static ID3D11UnorderedAccessView* s_claimUAV = nullptr;
static ID3D11Buffer* s_claimStage[kClaimStages] = {}; static int s_claimTag[kClaimStages] = { -1, -1, -1, -1, -1, -1 };
static std::atomic<int> s_emitBase{0}, s_emitGame{0};   // emisiones por origen (para el log)
// (27/10 l) tanda 9a: con que estados salieron las hojas (log cada 3 s): stencil de la base, RT4 sin escribir, color del suelo (copia de RT0),
// sin ninguno (RT4 = 0: la hoja pierde la luz difusa del estadio); y dibujos del pasto del juego que no emitieron por no tener la base debajo (x299)
static std::atomic<int> s_emitSt{0}, s_emitRt4{0}, s_emitSnap{0}, s_emitBare{0}, s_skipNoBase{0};
// (25/10 b) copia propia de la escena (cb2 del VS, 21 float4) para el compute: se copia en la GPU desde la ventana que el juego
// tiene puesta (offset en constantes) en vez de enlazar el buffer del juego con offset al CS.
static ID3D11Buffer* s_sceneCb = nullptr;
static std::atomic<int> s_skipNoPs{0};   // dibujos del pasto del juego sin el PS de hojas (prepasada de profundidad / sombras): no se emite ahi
static std::atomic<int> s_offPass{0}; static UINT s_offW = 0, s_offH = 0, s_offRtv = 0;   // (26/10 d) dibujos del pasto del juego en pasadas de otro tamano (sombras): tamano del DSV
static float s_camM11 = 0, s_camEye[3] = { 0, 0, 0 }; static UINT s_cb2Off = 0, s_cb2N = 0, s_cb2Bytes = 0;
// (26/10 d) x63 latcheado: 1 = el mod no esta dibujando (el VS deja el pasto del juego). Antes se calculaba por cuadro con etiquetas
// de grabacion y oscilaba (cuadros sueltos con el pasto denso del juego sin cortar + DensePick); ahora sale de lo ejecutado en la GPU
// con histeresis: 6 cuadros leidos sin emision -> 1, 4 cuadros con emision -> 0
static std::atomic<bool> s_missing{true}; static int s_zeroRun = 0, s_okRun = 0, s_framesNoGrass = 0;

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
    cb(160, &s_cullCb); cb(64, &s_cpVs0); cb(80, &s_cpVs4); cb(80, &s_cpPs0); cb(352, &s_sceneCb); cb(512, &s_cpVs2);
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
    raw(96, 0, &s_claim, &s_claimUAV);
    for (auto& st : s_claimStage) if (!st) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 96; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &st); }
    return s_cullCb && s_sceneCb && s_cpVs0 && s_cpVs4 && s_cpPs0 && s_instG && s_instGSRV && s_instGUAV && s_count && s_countUAV && s_args && s_argsUAV && s_claim && s_claimUAV;
}

// (26/10 f) pasto tanda 6: estados propios de las hojas del mod.
// Sombras: el juego reparte el sol en dos pasadas por cuadro (e9c13241 con el mapa de sombras, d0e9d81c sin sombra), casi seguro
// por el stencil: el pasto del juego trae su sombra plana y va a la pasada sin sombra. Las hojas del mod se dibujaban con el estado
// del pasto del juego y en las camaras abiertas tapan la cancha entera: sin la sombra del estadio ni la de los jugadores (y con la
// sombra plana prendida o apagada segun la camara: "al acercar cambia la sombra"). x290: las hojas llevan la profundidad del pasto
// del juego y el stencil de la base de la cancha, asi el sol las ilumina como al suelo. y290: las hojas no escriben RT4 (luz
// horneada del estadio que dejo la base debajo). x291: lejos el color de la hoja se mezcla con el del suelo (alfa de o0, 18af).
static std::mutex s_bsMx;
static thread_local bool t_gameGB = false;   // (26/10 g) GrassOnDraw: el dibujo en curso es el pasto del juego en el G-buffer
static ID3D11DepthStencilState* s_baseDss = nullptr; static UINT s_baseSref = 0; static bool s_baseDssOk = false;   // ultimo dibujo de la base
static ID3D11Device* s_hyDev = nullptr;
// caches chicos (4 entradas cada uno): la emision del mod y el pasto del juego piden combinaciones distintas en el mismo cuadro
struct HyDss { ID3D11DepthStencilState *out = nullptr, *g = nullptr, *b = nullptr; };
struct HyBs { ID3D11BlendState *out = nullptr, *src = nullptr; int mode = -1; };
static HyDss s_hyD[4]; static HyBs s_hyBl[4]; static int s_hyDNext = 0, s_hyBNext = 0;
static ID3D11Buffer* s_ggCb = nullptr;   // (26/10 g) b12 del VS en el pasto del juego: x = 1 si va con el stencil de la base (sin sombra plana)
// (26/10 g) copia de RT0 (el suelo ya dibujado) para el color lejano de las hojas (x291): el PS hace lerp(suelo, hoja, pesCf) sin mezcla
// por alfa. Con la mezcla por alfa cada capa de hojas volvia a mezclar sobre la anterior (con 4 capas el suelo quedaba en 6-50 % segun el
// orden, que cambia en cada cuadro: ruido y titileo entre 40 y 140 px/m)
static ID3D11Texture2D* s_r0Tex = nullptr; static ID3D11ShaderResourceView* s_r0Srv = nullptr; static D3D11_TEXTURE2D_DESC s_r0Desc = {};
static DXGI_FORMAT s_r0Fmt = DXGI_FORMAT_UNKNOWN; static UINT s_r0Mip = 0; static bool s_r0Failed = false;
// (26/10 g) emision diferida: las hojas se dibujan despues del ULTIMO dibujo de la base de la grabacion (antes: despues del primero; si la
// cancha viene en varios dibujos, las partes dibujadas despues quedaban debajo de las hojas sin escribir RT0 / RT4 = suelo sin color ni luz).
// Se guarda la pasada (targets, viewports, stencil de la base y una copia de la escena del VS b2) y se emite antes del siguiente dibujo que
// no sea de la base, antes de cambiar los targets / limpiar la profundidad / terminar o ejecutar la lista
struct BasePend {
    ID3D11RenderTargetView* rtv[8] = {}; ID3D11DepthStencilView* dsv = nullptr;
    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {}; UINT nvp = 0;
    D3D11_RECT sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {}; UINT nsc = 0;
    ID3D11DepthStencilState* dss = nullptr; UINT sref = 0; int pres = -1; int turfN = 0; bool b2 = false;
    void Release() { for (auto*& v : rtv) { if (v) v->Release(); v = nullptr; } if (dsv) dsv->Release(); dsv = nullptr; if (dss) dss->Release(); dss = nullptr; }
};
static std::mutex s_pendMx; static std::unordered_map<ID3D11DeviceContext*, BasePend> s_pend;
std::atomic<int> g_grassPendN{0};
static std::atomic<int> s_flushWhy[6]; static std::atomic<int> s_flushOther{0}, s_pendLost{0}, s_turfAfter{0}, s_turfMaxRec{0};   // log (estaticos: arrancan en 0)
static thread_local bool t_inEmit = false;   // dentro de GrassEmit: sus propios dibujos no disparan la emision pendiente
static void HyDropLocked() {
    for (auto& e : s_hyD) { if (e.out) e.out->Release(); if (e.g) e.g->Release(); if (e.b) e.b->Release(); e = HyDss(); }
    for (auto& e : s_hyBl) { if (e.out) e.out->Release(); if (e.src) e.src->Release(); e = HyBs(); }
    if (s_baseDss) s_baseDss->Release(); s_baseDss = nullptr; s_baseDssOk = false;
    if (s_ggCb) s_ggCb->Release(); s_ggCb = nullptr;
    if (s_r0Srv) s_r0Srv->Release(); if (s_r0Tex) s_r0Tex->Release(); s_r0Srv = nullptr; s_r0Tex = nullptr; s_r0Failed = false;
}
static D3D11_DEPTH_STENCIL_DESC DssDesc(ID3D11DepthStencilState* s) {
    D3D11_DEPTH_STENCIL_DESC d = {};
    if (s) { s->GetDesc(&d); return d; }
    d.DepthEnable = TRUE; d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; d.DepthFunc = D3D11_COMPARISON_LESS;   // estado por defecto de D3D11
    d.StencilEnable = FALSE; d.StencilReadMask = d.StencilWriteMask = 0xff;
    d.FrontFace.StencilFailOp = d.FrontFace.StencilDepthFailOp = d.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP; d.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    d.BackFace = d.FrontFace;
    return d;
}
// (26/10 g) el stencil de la base es otro que el del pasto del juego? Si es el mismo, el estado hibrido no cambia la pasada del sol que
// ilumina las hojas y no hay que apagarles la sombra plana (revision: sin esto se perdian las sombras de los jugadores)
static bool StencilDiffers(ID3D11DepthStencilState* g, UINT gr, ID3D11DepthStencilState* b, UINT br) {
    if (g == b && gr == br) return false;
    D3D11_DEPTH_STENCIL_DESC a = DssDesc(g), z = DssDesc(b);
    if (!a.StencilEnable && !z.StencilEnable) return false;
    return gr != br || a.StencilEnable != z.StencilEnable || a.StencilReadMask != z.StencilReadMask || a.StencilWriteMask != z.StencilWriteMask ||
           memcmp(&a.FrontFace, &z.FrontFace, sizeof(a.FrontFace)) != 0 || memcmp(&a.BackFace, &z.BackFace, sizeof(a.BackFace)) != 0;
}
static void DescDSS(char* out, size_t n, ID3D11DepthStencilState* s, UINT ref) {
    D3D11_DEPTH_STENCIL_DESC d = DssDesc(s);
    snprintf(out, n, "%sprof %d escribe %d func %d | stencil %d lee %02x escribe %02x ref %u | frente func %d pasa %d falla %d falla-prof %d | atras func %d pasa %d",
        s ? "" : "(por defecto) ", (int)d.DepthEnable, (int)d.DepthWriteMask, (int)d.DepthFunc, (int)d.StencilEnable, d.StencilReadMask, d.StencilWriteMask, ref,
        (int)d.FrontFace.StencilFunc, (int)d.FrontFace.StencilPassOp, (int)d.FrontFace.StencilFailOp, (int)d.FrontFace.StencilDepthFailOp, (int)d.BackFace.StencilFunc, (int)d.BackFace.StencilPassOp);
}
// profundidad del pasto del juego (g) + stencil de la base (b); devuelve con referencia (o nullptr)
static ID3D11DepthStencilState* BladeDSS(ID3D11DepthStencilState* g, ID3D11DepthStencilState* b) {
    if (!g_dev.dev) return nullptr;
    std::lock_guard<std::mutex> lk(s_bsMx);
    if (s_hyDev != g_dev.dev) { HyDropLocked(); s_hyDev = g_dev.dev; }
    for (auto& e : s_hyD) if (e.out && e.g == g && e.b == b) { e.out->AddRef(); return e.out; }
    D3D11_DEPTH_STENCIL_DESC dg = DssDesc(g), db = DssDesc(b);
    dg.StencilEnable = db.StencilEnable; dg.StencilReadMask = db.StencilReadMask; dg.StencilWriteMask = db.StencilWriteMask;
    dg.FrontFace = db.FrontFace; dg.BackFace = db.BackFace;
    ID3D11DepthStencilState* n = nullptr;
    if (FAILED(g_dev.dev->CreateDepthStencilState(&dg, &n)) || !n) return nullptr;
    HyDss& e = s_hyD[s_hyDNext]; s_hyDNext = (s_hyDNext + 1) & 3;
    if (e.out) e.out->Release(); if (e.g) e.g->Release(); if (e.b) e.b->Release();
    e.out = n; e.g = g; e.b = b; if (g) g->AddRef(); if (b) b->AddRef();   // la referencia evita que otro objeto en la misma direccion engane al cache
    n->AddRef(); return n;
}
// mezcla de las hojas: mode 1 = sin escribir RT4, 2 = RT0 sin su alfa (el PS mezcla con la copia del suelo); devuelve con referencia
static ID3D11BlendState* BladeBlend(ID3D11BlendState* src, int mode) {
    if (!mode || !g_dev.dev) return nullptr;
    std::lock_guard<std::mutex> lk(s_bsMx);
    if (s_hyDev != g_dev.dev) { HyDropLocked(); s_hyDev = g_dev.dev; }
    for (auto& e : s_hyBl) if (e.out && e.src == src && e.mode == mode) { e.out->AddRef(); return e.out; }
    D3D11_BLEND_DESC d = {};
    if (src) src->GetDesc(&d);
    else for (auto& r : d.RenderTarget) { r.SrcBlend = r.SrcBlendAlpha = D3D11_BLEND_ONE; r.DestBlend = r.DestBlendAlpha = D3D11_BLEND_ZERO; r.BlendOp = r.BlendOpAlpha = D3D11_BLEND_OP_ADD; r.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; }
    if (!d.IndependentBlendEnable) { for (int i = 1; i < 8; i++) d.RenderTarget[i] = d.RenderTarget[0]; d.IndependentBlendEnable = TRUE; }
    if (mode & 1) d.RenderTarget[4].RenderTargetWriteMask = 0;
    if (mode & 2) { auto& r = d.RenderTarget[0]; r.BlendEnable = FALSE;   // (26/10 g) el PS ya mezcla con la copia del suelo (t10); el alfa de RT0 queda el de la base
        r.RenderTargetWriteMask &= (D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE); }
    ID3D11BlendState* n = nullptr;
    if (FAILED(g_dev.dev->CreateBlendState(&d, &n)) || !n) return nullptr;
    HyBs& e = s_hyBl[s_hyBNext]; s_hyBNext = (s_hyBNext + 1) & 3;
    if (e.out) e.out->Release(); if (e.src) e.src->Release();
    e.out = n; e.src = src; if (src) src->AddRef(); e.mode = mode;
    n->AddRef(); return n;
}
// (26/10 g) copia del RT0 puesto (el suelo ya dibujado) a una textura propia; devuelve su vista con referencia o null
static ID3D11ShaderResourceView* SnapshotRT0(ID3D11DeviceContext* c) {
    if (!g_dev.dev) return nullptr;
    ID3D11RenderTargetView* rtv = nullptr; c->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return nullptr;
    D3D11_RENDER_TARGET_VIEW_DESC rd; rtv->GetDesc(&rd);
    ID3D11Resource* res = nullptr; rtv->GetResource(&res); rtv->Release();
    ID3D11Texture2D* t = nullptr; if (res) { res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t); res->Release(); }
    if (!t) return nullptr;
    D3D11_TEXTURE2D_DESC td; t->GetDesc(&td);
    if (rd.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || td.SampleDesc.Count != 1 || td.ArraySize != 1 || (td.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)) { t->Release(); return nullptr; }
    ID3D11Texture2D* dst = nullptr; ID3D11ShaderResourceView* out = nullptr;
    {
        std::lock_guard<std::mutex> lk(s_bsMx);
        if (s_hyDev != g_dev.dev) { HyDropLocked(); s_hyDev = g_dev.dev; }
        const bool same = s_r0Desc.Width == td.Width && s_r0Desc.Height == td.Height && s_r0Desc.MipLevels == td.MipLevels && s_r0Desc.Format == td.Format && s_r0Fmt == rd.Format && s_r0Mip == rd.Texture2D.MipSlice;
        if (!same) {
            if (s_r0Srv) s_r0Srv->Release(); if (s_r0Tex) s_r0Tex->Release(); s_r0Srv = nullptr; s_r0Tex = nullptr; s_r0Failed = false;
            s_r0Desc = td; s_r0Fmt = rd.Format; s_r0Mip = rd.Texture2D.MipSlice;
            D3D11_TEXTURE2D_DESC nd = td; nd.Usage = D3D11_USAGE_DEFAULT; nd.BindFlags = D3D11_BIND_SHADER_RESOURCE; nd.CPUAccessFlags = 0; nd.MiscFlags = 0;
            if (SUCCEEDED(g_dev.dev->CreateTexture2D(&nd, nullptr, &s_r0Tex)) && s_r0Tex) {
                D3D11_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = rd.Format; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MostDetailedMip = rd.Texture2D.MipSlice; sd.Texture2D.MipLevels = 1;
                if (FAILED(g_dev.dev->CreateShaderResourceView(s_r0Tex, &sd, &s_r0Srv))) s_r0Srv = nullptr;
            } else s_r0Tex = nullptr;
            if (!s_r0Tex || !s_r0Srv) { s_r0Failed = true; Log("pasto tanda 6: no se pudo crear la copia del suelo (RT0 %ux%u formato %d / vista %d): las hojas lejanas quedan con su color", td.Width, td.Height, (int)td.Format, (int)rd.Format); }
        }
        if (!s_r0Failed && s_r0Tex && s_r0Srv) { dst = s_r0Tex; dst->AddRef(); out = s_r0Srv; out->AddRef(); }
    }
    if (dst) { c->CopyResource(dst, t); dst->Release(); }
    t->Release();
    return out;
}
// (26/10 f) diagnostico: estado de profundidad / stencil de los dibujos que reparten el sol (lo llama BeforeDraw), 3 veces cada uno
void GrassDiagState(ID3D11DeviceContext* c, uint64_t psHash) {
    const char* tag = psHash == 0xe9c13241d7bc9625ULL ? "sol con sombra (e9c13241)" : psHash == 0xd0e9d81c09b3da66ULL ? "sol sin sombra (d0e9d81c)"
        : psHash == 0x5e92f2343848915aULL ? "base de la cancha (5e92)" : psHash == 0xc9564b9b155f0a49ULL ? "base de la cancha (c956)"
        : psHash == 0x8d8f4e5fc80fc684ULL ? "base de la cancha (8d8f)" : psHash == 0x91d1b2f17d4f7671ULL ? "base de la cancha (91d1)" : nullptr;
    if (!tag) return;
    static std::mutex mx; static std::map<uint64_t, std::pair<int, double>> seen;
    double tn = Now();
    { std::lock_guard<std::mutex> lk(mx); auto& e = seen[psHash]; if (e.first >= 3 || tn < e.second) return; e.first++; e.second = tn + 20.0; }
    ID3D11DepthStencilState* d = nullptr; UINT r = 0; c->OMGetDepthStencilState(&d, &r);
    char b[400]; DescDSS(b, sizeof(b), d, r); if (d) d->Release();
    ID3D11RenderTargetView* rt[8] = {}; c->OMGetRenderTargets(8, rt, nullptr); int nrt = 0; for (auto* v : rt) if (v) { nrt++; v->Release(); }
    Log("pasto tanda 6: estado de %s (%s, %d targets): %s", tag, c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED ? "diferido" : "inmediato", nrt, b);
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
    { static std::atomic<int> nLog{0}; if (nLog.fetch_add(1) < 2) {   // (26/10 f) diagnostico: profundidad / stencil y mezcla del pasto del juego
        char b[400]; DescDSS(b, sizeof(b), n.dss, n.sref);
        std::string m = "pasto tanda 6: estado del pasto del juego: "; m += b; m += " | mezcla:";
        if (!n.bs) m += " (por defecto)";
        else { D3D11_BLEND_DESC bd; n.bs->GetDesc(&bd); char t[128]; snprintf(t, sizeof(t), " a2c %d indep %d", (int)bd.AlphaToCoverageEnable, (int)bd.IndependentBlendEnable); m += t;
            for (int i = 0; i < 5; i++) { const auto& r = bd.RenderTarget[bd.IndependentBlendEnable ? i : 0]; snprintf(t, sizeof(t), " | RT%d mezcla %d src %d dst %d mascara %x", i, (int)r.BlendEnable, (int)r.SrcBlend, (int)r.DestBlend, (unsigned)r.RenderTargetWriteMask); m += t; } }
        Log("%s", m.c_str()); } }
    GrassState old;
    { std::lock_guard<std::mutex> lk(s_stMx); old = s_state; s_state = n; }
    // (26/10 d) diagnostico: la textura de sombra plana (t5) del dibujo vivo es la misma que la guardada la vez anterior?
    // (si cambia por cuadro, la emision desde la base usa una vieja y la sombra de los jugadores sobre las hojas sale corrida)
    if (old.valid) {
        ID3D11Resource* a = nullptr; ID3D11Resource* b = nullptr;
        if (old.vsSrv[5]) old.vsSrv[5]->GetResource(&a); if (n.vsSrv[5]) n.vsSrv[5]->GetResource(&b);
        if (a == b) s_t5Same++; else s_t5Diff++;
        if (a) a->Release(); if (b) b->Release();
    }
    old.Release();
    s_capPres.store(s_frame.load());
}

// niveles de la grilla: celda, tope de instancias, objetivo de indices de la malla, ensanche de la hoja
struct Level { float cell; UINT cap; UINT target; float widthMul; };

// umbrales de tamano en pantalla (pixeles por metro): mas = mas cerca. x58 (densidad) los corre: 1.5 = cada nivel llega
// 1.5x mas lejos. T0: hasta aca el pasto del juego (una hoja de 1.2 cm mide >= 2 px); T1 / T2: limites de los niveles.
static void Thresholds(float* T) {
    // (25/10 c) fijos: la densidad (x58) ya no corre los niveles, cambia cuantas tarjetas lleva cada parcela (densidad
    // constante en pantalla, ver GrassEmit). El pasto del juego queda solo pegado a la camara (>= 2000 px/m)
    // (26/10) T0 = 2000: el pasto del juego solo a menos de ~1 m de la camara. Con 240 la zona cercana (pasto denso del
    // juego, ~19 capas, muy oscura) chocaba con la del mod (6 capas): borde visible, pasto irregular y 13 FPS en primeros planos
    // (26/10 f) x294: T0 regulable desde el menu (Agustin: "el cambio al pasto de cerca solo pasa muy cerca"). (26/10 g) 1000 por defecto
    // (el doble de distancia; con 1000 o mas sigue sin la malla densa, ver DensePick): el pasto del juego ahora recibe los estados de las
    // hojas del mod (w290, GrassGameStates), sin el borde oscuro de cuando T0 era 240
    const float t0 = g_cfg.live[294][0];
    T[0] = t0 >= 200.0f ? (std::min)(t0, 6000.0f) : 1000.0f; T[1] = 110.0f; T[2] = 45.0f;
    T[3] = 8.0f;    // (25/10) limite inferior del nivel lejano cuando el cesped lejano de la base (y61) esta prendido. (26/10 d) 15 -> 8:
                    // con 15 las camaras abiertas quedaban sin hojas 3D en el tercio lejano (anillo fijo a 60-75 m); a 8-15 px/m
                    // son 50-90 tarjetas por celda de 2 m, barato
}

// (26/10 d) limite del cuadro: en hk_Present ANTES de UploadParams (asi x63 que se decide aca llega al t120 del cuadro que
// empieza, no uno despues). Lee el reclamo de los cuadros ya ejecutados, copia el de este y lo reinicia en el contexto inmediato
// (la primera emision que la GPU ejecute despues dibuja; las otras del mismo cuadro salen vacias), y decide x63 con histeresis.
void GrassFrameBoundary(UINT flags) {
    if (flags & DXGI_PRESENT_TEST) return;
    int f = s_frame.fetch_add(1) + 1;
    const bool on = g_cfg.live[58][3] > 0.5f;
    if (g_dev.imm && s_claim && s_claimUAV) {
        for (int i = 0; i < kClaimStages; i++) if (s_claimStage[i] && s_claimTag[i] >= 0 && s_claimTag[i] <= f - 3) {
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(g_dev.imm->Map(s_claimStage[i], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
                const UINT* q = (const UINT*)m.pData; int e = (int)q[0];
                s_execHist[e <= 0 ? 0 : (e == 1 ? 1 : 2)]++; s_framesRead++;
                if (e >= 1) {
                    for (int k = 0; k < 3; k++) { int n = 0; for (int b = 0; b < kBins; b++) n += (int)q[1 + k * kBins + b]; g_grassDbg.ring[k] = n; }
                    s_lastCount = g_grassDbg.ring[0] + g_grassDbg.ring[1] + g_grassDbg.ring[2]; g_grassDbg.patches = s_lastCount;
                    memcpy(&s_camM11, &q[19], 4); memcpy(&s_camEye[0], &q[20], 4); memcpy(&s_camEye[1], &q[21], 4); memcpy(&s_camEye[2], &q[22], 4);
                    s_okRun++; s_zeroRun = 0;
                } else { s_zeroRun++; s_okRun = 0; if (on) s_framesNoGrass++; }
                g_dev.imm->Unmap(s_claimStage[i], 0); s_claimTag[i] = -1;
            }
        }
        for (int i = 0; i < kClaimStages; i++) if (s_claimStage[i] && s_claimTag[i] < 0) { g_dev.imm->CopyResource(s_claimStage[i], s_claim); s_claimTag[i] = f; break; }
        UINT z[4] = {}; g_dev.imm->ClearUnorderedAccessViewUint(s_claimUAV, z);
    }
    {   // el contexto inmediato arranca el cuadro de cero; listas con emision que nunca se ejecutaron se olvidan
        std::lock_guard<std::mutex> lk(s_recMx);
        if (g_dev.imm) s_rec.erase(g_dev.imm);
        for (auto it = s_rec.begin(); it != s_rec.end();) { if (it->second.pres >= 0 && f - it->second.pres > 120) it = s_rec.erase(it); else ++it; }   // contextos que ya no existen
        for (auto it = s_lists.begin(); it != s_lists.end();) { if (f - it->second.recPres > 8) it = s_lists.erase(it); else ++it; }
    }
    {   // (26/10 g) bases pendientes que nunca se emitieron (el inmediato arranca de cero; contextos abandonados): se sueltan
        std::lock_guard<std::mutex> lk(s_pendMx);
        for (auto it = s_pend.begin(); it != s_pend.end();) {
            if (it->first == g_dev.imm || f - it->second.pres > 4) { it->second.Release(); it = s_pend.erase(it); g_grassPendN--; s_pendLost++; } else ++it;
        }
    }
    {   // x63 latcheado (ver s_missing)
        bool operational = on && s_cs && s_denseBuilt.load();
        bool missing = s_missing.load();
        if (!operational) missing = true;
        else if (missing) { if (s_okRun >= 4) missing = false; }
        else if (s_zeroRun >= 6) missing = true;
        s_missing.store(missing);
        g_cfg.live[63][0] = missing ? 1.0f : 0.0f;
    }
    // umbrales para el VS del pasto del juego (y63 = 0: modo viejo por distancia)
    float T[4]; Thresholds(T);
    { bool px = s_cs && on; g_cfg.live[63][1] = px ? T[0] : 0.0f; g_cfg.live[63][2] = 0.5f * (float)(g_bbH ? g_bbH : 1080); }
    g_grassPatchesLastFrame = s_lastCount;
}

void GrassPresent() {
    float T[4]; Thresholds(T);
    // (27/10 o) densidad adaptativa (fila 333): la DLL mide el tiempo entre Presents y, si los FPS quedan por debajo del objetivo x333,
    // baja la densidad del pasto del mod (C base y piso de capas) hasta el minimo y333; cuando sobran FPS la vuelve a subir despacio.
    // Es la unica "cache" que paga: el costo del pasto es dibujar millones de triangulos chicos cada cuadro (camara, jugadores y luz
    // cambian), no calcular la lista de parcelas (el compute cuesta decimas de ms). 0 = apagado.
    { static double lastT = -1.0; static float ema = 0.0f; static double nextAdj = 0.0, nextLogA = 0.0; double tn = Now();
      if (lastT >= 0.0) { float dt = (float)std::clamp(tn - lastT, 0.0005, 0.5); ema = ema <= 0.0f ? dt : ema + (dt - ema) * 0.1f; }
      lastT = tn;
      float target = g_cfg.live[333][0], floorA = g_cfg.live[333][1] > 0.05f ? std::clamp(g_cfg.live[333][1], 0.2f, 1.0f) : 0.4f;
      if (target < 20.0f || (int)(g_cfg.live[61][0] + 0.5f) >= 4) { s_adapt = 1.0f; nextAdj = 0.0; }   // (27/10 q) apagada o Calidad Ultra
      else if (tn >= nextAdj && ema > 0.0f) { nextAdj = tn + 0.5;
          float fps = 1.0f / ema; float a = s_adapt.load();
          if (fps < target * 0.97f) a = (std::max)(floorA, a * 0.93f);
          else if (fps > target * 1.10f) a = (std::min)(1.0f, a * 1.06f);   // (27/10 q) 1.03 -> 1.06: volvia al 100 % en 15 s, ahora en 8
          s_adapt = a;
          if (tn >= nextLogA && g_cfg.live[58][3] > 0.5f) { nextLogA = tn + 10.0; Log("pasto adaptativo: FPS %.0f (objetivo %.0f) -> densidad x%.2f (minimo %.2f)", fps, target, a, floorA); } } }
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
        static int lastPres = 0; int pres = s_frame.load(); int dp = pres - lastPres; lastPres = pres;
        int eh[3], lh[3]; { std::lock_guard<std::mutex> lk(s_recMx); for (int k = 0; k < 3; k++) { eh[k] = s_execHist[k]; lh[k] = s_lagHist[k]; s_execHist[k] = 0; s_lagHist[k] = 0; } }
        int fr = s_framesRead; s_framesRead = 0; int fng = s_framesNoGrass; s_framesNoGrass = 0;
        int age = s_capPres.load() >= 0 ? pres - s_capPres.load() : -1;
        Log("pasto v3: llamadas %d, emisiones grabadas %d (desde la base %d, desde el pasto del juego %d; en contextos diferidos %d, inmediato %d) | GPU: %d Presents, %d cuadros leidos, emisiones ejecutadas por cuadro 0:%d 1:%d 2+:%d (cuadros sin pasto del mod %d), grabacion->ejecucion 0:%d 1:%d 2+:%d Presents, x63 %s | parcelas %d (niveles %d/%d/%d), malla cercana %u idx, estado: %s | v4: %d dibujos con manojos curvos | camara del compute: m11 %.2f ojo (%.1f %.1f %.1f) cb2 off %u n %u de %u B, umbrales %.0f/%.0f/%.0f px/m, dibujos del pasto sin PS de hojas %d, en pasadas de otro tamano %d (DSV %ux%u, RTVs %u) | estado guardado hace %d Presents, t5 igual %d / distinta %d | base: hasta %d dibujos por grabacion, %d despues de las hojas; emision diferida por dibujo %d / targets %d / limpieza %d / fin %d / ejecucion %d / ClearState %d, con otros targets %d, perdidas %d | estados de las hojas: stencil base %d, sin RT4 %d, color suelo %d, sin nada %d; pasto del juego sin base debajo (x299) %d",
            g_grassDbg.calls, g_grassDbg.emits, s_emitBase.exchange(0), s_emitGame.exchange(0), s_recDeferred.exchange(0), s_recImmediate.exchange(0),
            dp, fr, eh[0], eh[1], eh[2], fng, lh[0], lh[1], lh[2], s_missing.load() ? "1 (mod sin dibujar: pasto del juego entero)" : "0",
            g_grassDbg.patches, g_grassDbg.ring[0], g_grassDbg.ring[1], g_grassDbg.ring[2], g_grassDbg.lod,
            g_grassDbg.why ? g_grassDbg.why : "-", g_denseDraws.exchange(0), s_camM11, s_camEye[0], s_camEye[1], s_camEye[2], s_cb2Off, s_cb2N, s_cb2Bytes, T[0], T[1], T[2], s_skipNoPs.exchange(0),
            s_offPass.exchange(0), s_offW, s_offH, s_offRtv, age, s_t5Same.exchange(0), s_t5Diff.exchange(0),
            s_turfMaxRec.exchange(0), s_turfAfter.exchange(0), s_flushWhy[0].exchange(0), s_flushWhy[1].exchange(0), s_flushWhy[2].exchange(0), s_flushWhy[3].exchange(0),
            s_flushWhy[4].exchange(0), s_flushWhy[5].exchange(0), s_flushOther.exchange(0), s_pendLost.exchange(0),
            s_emitSt.exchange(0), s_emitRt4.exchange(0), s_emitSnap.exchange(0), s_emitBare.exchange(0), s_skipNoBase.exchange(0));
        g_grassDbg.calls = 0; g_grassDbg.emits = 0; g_grassDbg.sparseLast = g_grassDbg.sparse; g_grassDbg.sparse = 0; }
}

// emite el pasto del mod en este cuadro. bound = el estado del dibujo de pasto del juego ya esta puesto (se llama desde
// ese dibujo); si no, se pone el guardado y se devuelve el del contexto al terminar.
// Devuelve true si los dibujos quedaron grabados (la grabacion ya lleva su emision).
static bool GrassEmit(ID3D11DeviceContext* c, bool bound, const BasePend* pend = nullptr) {
    struct InEmit { bool sv; InEmit() : sv(t_inEmit) { t_inEmit = true; } ~InEmit() { t_inEmit = sv; } } inEmit_;
    const float* p58 = g_cfg.live[58]; const float* p59 = g_cfg.live[59];
    EnsureBuffers();
    if (!s_cb) { Why("sin buffers propios"); return false; }
    if (!EnsureGPU()) { Why("sin recursos de GPU para el pasto (ver Errores)"); return false; }
    if (s_lodCount.load() == 0xffffffff) { Why("todavia sin variante de malla"); return false; }
    GrassState st;
    if (!bound) {
        std::lock_guard<std::mutex> lk(s_stMx);
        if (!s_state.valid) { Why("todavia no se vio el pasto del juego (sin estado para emitir desde la base)"); return false; }
        st = s_state; st.AddRefAll();
    }
    g_grassDbg.emits++; if (bound) s_emitGame++; else s_emitBase++;
    bool emitted = false;
    float T[4]; Thresholds(T);
    float tall = p59[0] > 0.05f ? p59[0] : 1.3f;
    float wide = p59[1] >= 0.0f ? p59[1] : 1.0f;
    int q = (int)(g_cfg.live[61][0] + 0.5f); q = (std::max)(0, (std::min)(q, 4));
    // (27/10 q) Calidad 4 = Ultra (pedido de Agustin: "cargar todas las parcelas como antes y el pasto al maximo"): cobertura 2.4x, hoja
    // minima de 1 px, todas las capas de muy cerca, hojas 3D hasta el fondo de la cancha (sin el limite del cesped lejano) y sin densidad adaptativa
    const bool ultra = q == 4;
    // (25/10 c) densidad constante en pantalla: cada parcela lleva las tarjetas que hacen falta para cubrir su tamano en
    // pantalla (tarjetas = C(p) * (celda * p)^2 / ancho_px, con p = pixeles por metro; C sube suave hacia la camara), con
    // la malla propia de manojos curvos (orden de Halton: las primeras N tarjetas estan bien repartidas) en 6 subniveles
    // (9248 >> b tarjetas) y un tope exacto por parcela que el VS aplica por SV_VertexID. Calidad (x61): cuanto cubre y el
    // ancho minimo de la hoja en pixeles (hojas de menos de ~1.4 px titilan). Densidad (x58) multiplica C.
    static const float cMul[5] = { 0.6f, 1.0f, 1.4f, 1.8f, 2.4f }, minPxQ[5] = { 1.8f, 1.5f, 1.3f, 1.2f, 1.0f };
    float dens = g_cfg.live[58][0] > 0.05f ? std::clamp(g_cfg.live[58][0], 0.3f, 2.0f) : 1.0f;
    if (!ultra) dens *= s_adapt.load();   // (27/10 o) densidad adaptativa por FPS (fila 333, GrassPresent); Ultra la ignora
    // (27/10 9c) x325: ancho minimo en px a mano (0 = el de la calidad). Con la camara quieta el TAA del juego promedia las hojas de
    // ~1.2 px con el suelo y el pasto se vuelve liso; moviendo, la historia se descarta y vuelven nitidas: hojas de 2 px no se pierden
    const float cBase = 0.30f * cMul[q] * dens, minPx = g_cfg.live[325][0] > 0.5f ? std::clamp(g_cfg.live[325][0], 1.f, 4.f) : minPxQ[q];
    Level L[3] = { { 0.5f, kSlotCap, 0, 1.0f }, { 1.0f, kSlotCap, 0, 1.6f }, { 2.0f, kSlotCap, 0, 2.0f } };
    // (25/10) con el cesped lejano de la base prendido (y61) el nivel 3D lejano se apaga por debajo de T3 px/m y la base lo sigue
    // (25/10 c) el fundido entre niveles es por CANTIDAD de tarjetas en el compute (nivel saliente lim*k, entrante lim*(1-k), mismo
    // margen 0.88 / 1.12 que el pasto del juego), no por altura en el VS: antes en la banda se dibujaban los dos niveles enteros y
    // todas las hojas quedaban a media altura a una distancia fija de la camara
    // (26/10 d) margen del fundido entre niveles 0.12 -> 0.25 en T1 / T2 (la pendiente de tarjetas por cuadro al mover la camara baja a la
    // mitad); el empalme con el pasto del juego en T0 sigue en 0.12 (el VS del juego corta con 0.88 / 1.12 fijos)
    const bool farBase = g_cfg.live[61][1] > 0.001f && !ultra;   // (27/10 q) Ultra: hojas 3D en toda la cancha const float loFar = farBase ? T[3] : 0.0f; const float fadeM = 0.25f, fadeT0 = 0.12f;
    // malla propia (manojos curvos: nivel 0 de 2 tramos, niveles 1 / 2 de 1 tramo)
    float curv = g_cfg.live[123][0] > 0.5f ? std::clamp(g_cfg.live[123][2], 0.0f, 1.5f) : 0.6f;
    ID3D11Buffer* mPos[3] = {}; ID3D11Buffer* mUv[3] = {}; ID3D11Buffer* mIb[3] = {}; UINT mCards = 0, mIpc[3] = { 12, 6, 6 }, mVpc[3] = { 6, 4, 4 };
    {
        std::lock_guard<std::mutex> lk(s_denseMx);
        static float triedCurv = -2.0f;   // si la malla no se pudo crear, no se reintenta en cada cuadro (log y CPU)
        if ((s_denseBuilt.load() && fabsf(curv - s_denseCurv) > 0.01f) || (!s_denseBuilt.load() && fabsf(curv - triedCurv) > 0.01f)) { triedCurv = curv; BuildDense(curv); }
        if (s_denseBuilt.load()) for (int i = 0; i < 3; i++) { DenseLevel& D = s_denseExt[i]; mPos[i] = D.pos; mUv[i] = D.uv; mIb[i] = D.ib; mCards = D.cards; mIpc[i] = D.idxPerCard; mVpc[i] = D.vertsPerCard; mPos[i]->AddRef(); mUv[i]->AddRef(); mIb[i]->AddRef(); }
    }
    if (!mPos[0] || !mPos[1] || !mPos[2]) { Why("sin malla propia de manojos (ver Errores)"); if (!bound) st.Release(); return false; }
    g_grassDbg.lod = mCards * mIpc[0];
    // (26/10 d) nivel 0 mas fino: hoja de 3 mm de ensanche (la malla ya trae 1.2 cm de ancho propio; antes 1.2 + 1.2 cm sin
    // afinar = cintas), alto 1.0 cerca de la camara (x59 queda para lo lejano) y un piso de capas de hojas por pixel de
    // suelo (D) que crece de 150 a 400 px/m: el tope por banda (K = 6) dejaba 3-15x menos hojas por m2 que el pasto del juego
    // (26/10 e) reporte de 4ae2c94: en las camaras de juego (110-300 px/m) las hojas de 3 mm quedaban por debajo del pixel (alfombra
    // borrosa, "feo") y el nivel 0 costaba 33 FPS con la camara baja (7 capas de hojas por pixel de pantalla). Ahora el nivel 0 tiene
    // dos extremos por px/m: camaras de juego (hoja de 12 mm de ensanche, alto x59, tope de 4 capas sobre la banda) y camara al ras
    // (>= 700 px/m: 4 mm, alto 1.0, piso de 6 capas por pixel de suelo); entre 300 y 700 px/m se funde (VS cb5[12], CS gPad[2])
    // (26/10 f) y294 / z294: rampa de las hojas finas de cerca regulable (antes fija 300 / 700 px/m: el cambio a las hojas finas, altas y
    // tupidas de cerca recien se veia con la camara casi pegada al suelo); por defecto 200 / 450
    const float pR0 = g_cfg.live[294][1] >= 50.0f ? (std::min)(g_cfg.live[294][1], 3000.0f) : 300.0f;
    const float pR1 = g_cfg.live[294][2] >= pR0 + 20.0f ? (std::min)(g_cfg.live[294][2], 6000.0f) : (std::max)(pR0 * 2.3f, pR0 + 100.0f);
    const float wGame = 0.012f * wide, wClose = 0.004f * wide, pRamp0 = pR0, pRamp1 = pR1;
    // (27/10 m) tanda 10, w328: capas de hojas por pixel de suelo en el primer plano (0 = 6). Es el costo del plano cercano (25-30 FPS con la
    // camara al ras): 4 recorta un tercio de las hojas de muy cerca sin tocar las camaras de juego
    const float dNear = ultra ? 10.0f : (g_cfg.live[328][3] >= 1.0f ? std::clamp(g_cfg.live[328][3], 2.0f, 10.0f) : 6.0f);   // (27/10 q) Ultra: todas las capas
    const float wEff0 = wGame, tallNear = 1.0f, Dfloor = dNear * cMul[q] * dens;
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
        ID3D11UnorderedAccessView* oldUAV[4] = {}; c->CSGetUnorderedAccessViews(0, 4, oldUAV);
        c->CSSetShader(s_cs, nullptr, 0);
        {   // (25/10 b) la escena se copia a un buffer propio (desde la ventana del juego) y el CS la lee sin offset
            D3D11_BUFFER_DESC bd = {}; cb2->GetDesc(&bd);
            UINT off = (c1 && n2) ? f2 * 16 : 0, bytes = 336;
            if (off + bytes > bd.ByteWidth) bytes = off < bd.ByteWidth ? bd.ByteWidth - off : 0;
            s_cb2Off = off; s_cb2N = n2; s_cb2Bytes = bd.ByteWidth;
            if (bytes) { D3D11_BOX bx = { off, 0, 0, off + bytes, 1, 1 }; c->CopySubresourceRegion(s_sceneCb, 0, 0, 0, 0, cb2, 0, &bx); }
            c->CSSetConstantBuffers(2, 1, &s_sceneCb);
        }
        c->CSSetConstantBuffers(0, 1, &s_cullCb);
        UINT zeros[20] = {}; c->UpdateSubresource(s_count, 0, nullptr, zeros, 0, 0);
        ID3D11UnorderedAccessView* uavs[4] = { s_instGUAV, s_countUAV, s_argsUAV, s_claimUAV }; UINT ini[4] = { (UINT)-1, (UINT)-1, (UINT)-1, (UINT)-1 };
        c->CSSetUnorderedAccessViews(0, 4, uavs, ini);
        // gP0 (nivel, celda, pLo, pHi) gP1 (mitad alto, tope por ranura, primera ranura del nivel, celdas a lo largo)
        // gP2 (celdas a lo ancho, margen, semi largo, semi ancho) gP3 (ancho de la hoja m, ancho minimo px, C base, indices por tarjeta)
        // gP4 (tarjetas de la malla, subniveles, alto de la hoja, tope de capas) gPad[0] (piso de capas D, p0, p1 de la rampa, alto cerca / alto)
        // gPad[1] (margen del fundido en pHi, indices por tarjeta de los niveles 0 / 1 / 2)
        float cull[40];
        for (int k = 0; k < 40; k++) cull[k] = 0.f;
        cull[4] = halfH; cull[5] = (float)kSlotCap; cull[9] = fadeM; cull[10] = halfL; cull[11] = halfW;
        cull[13] = minPx; cull[14] = cBase; cull[15] = (float)mIpc[0]; cull[16] = (float)mCards; cull[17] = (float)kBins;
        // gP4.z alto de la hoja (m), gP4.w tope de capas de hojas por pixel: con la camara baja la celda se ve como una banda angosta y la
        // cuenta de frente ((celda * p)^2) sobraba 14-46x; el tope sigue a Calidad / Densidad
        cull[18] = 0.058f * tall; cull[19] = 4.0f * cMul[q] * dens;   // (26/10 e) tope de capas 6 -> 4 (camara baja: menos solapamiento)
        cull[21] = pRamp0; cull[22] = pRamp1; cull[23] = tallNear / tall;
        cull[25] = (float)mIpc[0]; cull[26] = (float)mIpc[1]; cull[27] = (float)mIpc[2];
        cull[28] = wGame; cull[29] = wClose; cull[30] = pRamp0; cull[31] = pRamp1;   // gPad[2]: rampa del ancho del nivel 0 (como el VS)
        // (26/10 d) reclamo del cuadro: la primera emision que la GPU ejecuta despues del Present se queda con el cuadro (Count[18] = 1);
        // las demas pasan por las celdas sin anotar nada y dibujan 0 instancias
        cull[0] = 4.0f; c->UpdateSubresource(s_cullCb, 0, nullptr, cull, 0, 0); c->Dispatch(1, 1, 1);
        for (int i = 0; i < 3; i++) {
            UINT gw = (UINT)ceilf(2.0f * halfL / L[i].cell), gh = (UINT)ceilf(2.0f * halfW / L[i].cell);
            cull[0] = (float)i; cull[1] = L[i].cell; cull[2] = i == 2 ? loFar : T[i + 1]; cull[3] = T[i];   // nivel 2: sin limite inferior (toda la cancha) salvo con cesped lejano en la base
            cull[6] = (float)(i * kBins); cull[7] = (float)gw; cull[8] = (float)gh;
            cull[12] = i == 0 ? wEff0 : 0.012f * wide * L[i].widthMul * (L[i].cell / 0.5f);   // ancho de la hoja en metros (nivel 0: ancho efectivo proyectado; lejos la malla escala con la celda)
            cull[15] = (float)mIpc[i];
            cull[20] = i == 0 ? Dfloor : 0.0f; cull[24] = i == 0 ? fadeT0 : fadeM;
            c->UpdateSubresource(s_cullCb, 0, nullptr, cull, 0, 0);
            c->Dispatch((gw + 7) / 8, (gh + 7) / 8, 1);
        }
        cull[0] = 3.0f; c->UpdateSubresource(s_cullCb, 0, nullptr, cull, 0, 0); c->Dispatch(1, 1, 1);
        ID3D11UnorderedAccessView* nul[4] = {}; c->CSSetUnorderedAccessViews(0, 4, nul, nullptr);
        c->CSSetShader(oldCS, nullptr, 0); c->CSSetConstantBuffers(0, 3, oldCB); c->CSSetUnorderedAccessViews(0, 4, oldUAV, nullptr);
        if (oldCS) oldCS->Release(); for (auto* b : oldCB) if (b) b->Release(); for (auto* u : oldUAV) if (u) u->Release();
        cb2->Release();
        // --- malla propia en la entrada (posicion en el slot 0, uv en el slot 3, indices de 32 bits), una por nivel ---
        ID3D11Buffer* svVb[4] = {}; UINT svSt[4] = {}, svOf[4] = {}; c->IAGetVertexBuffers(0, 4, svVb, svSt, svOf);
        ID3D11Buffer* svIb = nullptr; DXGI_FORMAT svFmt = DXGI_FORMAT_UNKNOWN; UINT svIo = 0; c->IAGetIndexBuffer(&svIb, &svFmt, &svIo);
        int boundMesh = -1;
        // --- dibujos: uno por ranura (nivel x subnivel), indirectos (pasan por nuestro gancho: reglas y t120) ---
        c->VSSetShaderResources(121, 1, &s_instGSRV);
        // (26/10 f) estados propios de las hojas (ver BladeDSS / BladeBlend): sombra del sol como el suelo, luz horneada y color del suelo lejos
        ID3D11DepthStencilState* svDss = nullptr; UINT svSref = 0; c->OMGetDepthStencilState(&svDss, &svSref);
        ID3D11BlendState* svBs = nullptr; float svBf[4] = {}; UINT svBm = 0xffffffff; c->OMGetBlendState(&svBs, svBf, &svBm);
        bool stRoute = false, farBl = false; int bmSet = 0;
        if (g_cfg.live[290][0] > 0.5f) {
            ID3D11DepthStencilState* bD = nullptr; UINT bR = 0; bool have = false;
            if (!bound) { bD = pend ? pend->dss : prev.dss; bR = pend ? pend->sref : prev.sref; if (bD) bD->AddRef(); have = true; }   // el estado de la base (guardado si la emision es diferida)
            else { std::lock_guard<std::mutex> lk(s_bsMx); if (s_baseDssOk && s_hyDev == g_dev.dev) { bD = s_baseDss; bR = s_baseSref; if (bD) bD->AddRef(); have = true; } }
            if (have && StencilDiffers(svDss, svSref, bD, bR)) if (ID3D11DepthStencilState* h = BladeDSS(svDss, bD)) { c->OMSetDepthStencilState(h, bR); h->Release(); stRoute = true; }
            if (bD) bD->Release();
        }
        ID3D11ShaderResourceView* snap = nullptr; ID3D11ShaderResourceView* oldT10 = nullptr; bool t10Set = false;
        { int bm = 0; const bool baseUnder = !bound || RecHas(c, &GRec::base);   // sin la base dibujada antes no hay suelo con que mezclar ni luz en RT4
          if (g_cfg.live[290][1] > 0.5f && baseUnder) bm |= 1;
          if (g_cfg.live[291][0] > 0.5f && baseUnder && (snap = SnapshotRT0(c)) != nullptr) bm |= 2;   // (26/10 g) sin la copia del suelo no hay mezcla
          if (bm) if (ID3D11BlendState* nb = BladeBlend(svBs, bm)) { c->OMSetBlendState(nb, svBf, svBm); nb->Release(); farBl = (bm & 2) != 0; bmSet = bm; }
          if (farBl) { c->PSGetShaderResources(10, 1, &oldT10); c->PSSetShaderResources(10, 1, &snap); t10Set = true; } }
        if (stRoute) s_emitSt++; if (bmSet & 1) s_emitRt4++; if (bmSet & 2) s_emitSnap++; if (!stRoute && !bmSet) s_emitBare++;
        { static std::atomic<int> nLog[3]; const int route = bound ? 0 : (pend ? 2 : 1);   // (27/10 l) dos lineas por camino (antes las 3 se iban en el primero)
          if (nLog[route].fetch_add(1) < 2) {
            char b[400] = "-"; if (stRoute) { ID3D11DepthStencilState* d = nullptr; UINT r = 0; c->OMGetDepthStencilState(&d, &r); DescDSS(b, sizeof(b), d, r); if (d) d->Release(); }
            Log("pasto tanda 6: hojas del mod (%s): stencil de la base %s, mezcla %d (1 = sin RT4, 2 = color con el suelo): %s", bound ? "desde el pasto del juego" : (pend ? "desde la base, diferida" : "desde la base"), stRoute ? "si" : "no", bmSet, b); } }
        float cbq[72];
        for (int sl = 0; sl < kSlots; sl++) {
            int i = sl / kBins;
            if (boundMesh != i) { ID3D11Buffer* vbs[4] = { mPos[i], svVb[1], svVb[2], mUv[i] }; UINT sts[4] = { 12, svSt[1], svSt[2], 4 }, ofs2[4] = { 0, svOf[1], svOf[2], 0 };
                c->IASetVertexBuffers(0, 4, vbs, sts, ofs2); c->IASetIndexBuffer(mIb[i], DXGI_FORMAT_R32_UINT, 0); boundMesh = i; }
            for (int k = 0; k < 72; k++) cbq[k] = 0.f;
            // cb5[8]: (vertices por tarjeta, marca, alto, ancho base)  cb5[9]: (magenta, offset de instancias, 1 = raices ya escaladas en la malla, -)
            // cb5[10]: (ancho minimo px, -, multiplicador de ancho, mitad del alto de pantalla)  cb5[11]: (pLo, pHi, 1 = por tamano, margen)
            // pLo / pHi = 0: el VS no funde por altura (el fundido es por cantidad en el compute)
            // cb5[9].w: 1 = el estado guardado (sombra plana t5) es reciente; emitiendo desde la base con un estado de hace mas de 30
            // Presents (camaras sin pasto del juego) la sombra plana no se aplica a las hojas del mod (seria de otro momento)
            // cb5[10].y: alto de las hojas cerca de la camara (el VS funde de x59 a este entre 110 y 250 px/m)
            cbq[32] = (float)mVpc[i]; cbq[33] = 1234.5f; cbq[34] = tall; cbq[35] = i == 0 ? wGame : 0.012f * wide;
            cbq[36] = p59[2] > 0.5f ? 1.f : 0.f; cbq[37] = (float)(sl * kSlotCap); cbq[38] = 1.0f;
            cbq[39] = (bound || (s_capPres.load() >= 0 && s_frame.load() - s_capPres.load() <= 30)) ? 1.0f : 0.0f;
            cbq[40] = minPx; cbq[41] = tallNear; cbq[42] = L[i].widthMul; cbq[43] = halfH;
            cbq[44] = 0.0f; cbq[45] = 0.0f; cbq[46] = 1.0f; cbq[47] = fadeM;
            // cb5[12] (26/10 e, solo nivel 0): ancho con la camara al ras, rampa p0 / p1 en px/m (alto y ancho se funden con ella)
            cbq[48] = i == 0 ? wClose : 0.0f; cbq[49] = i == 0 ? pRamp0 : 0.0f; cbq[50] = i == 0 ? pRamp1 : 0.0f;
            // cb5[12].w (26/10 f): 1 = hojas con el stencil de la base (el sol con sombra las ilumina: sin sombra plana), + 2 = color mezclado con el suelo
            cbq[51] = (stRoute ? 1.0f : 0.0f) + (farBl ? 2.0f : 0.0f);
            cbq[52] = L[i].cell;   // (26/10 g) lado de la celda: el raleo por desgaste mira un punto al azar de la celda, no su esquina
            c->UpdateSubresource(s_cb, 0, nullptr, cbq, 0, 0);
            c->VSSetConstantBuffers(5, 1, &s_cb);
            c->DrawIndexedInstancedIndirect(s_args, (UINT)sl * 20);
        }
        c->IASetVertexBuffers(0, 4, svVb, svSt, svOf); c->IASetIndexBuffer(svIb, svFmt, svIo);
        for (auto* b : svVb) if (b) b->Release(); if (svIb) svIb->Release();
        c->OMSetDepthStencilState(svDss, svSref); c->OMSetBlendState(svBs, svBf, svBm);
        if (svDss) svDss->Release(); if (svBs) svBs->Release();
        if (t10Set) c->PSSetShaderResources(10, 1, &oldT10);
        if (oldT10) oldT10->Release(); if (snap) snap->Release();
        emitted = true;
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
    return emitted;
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
    t_gameGB = false;
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
    if ((!extraOn || s_missing.load()) && count == s_lodCount.load() && g_cfg.live[60][3] > 0.5f) { t_sparseGrass = true; g_grassDbg.sparse++; }
    if (!MainPass(c)) {
        if (extraOn) {
            Why("pasada secundaria (target de otro tamano)");
            // (26/10 d) diagnostico: el pasto del juego tambien se dibuja en la pasada de sombras del sol? (el VS lo corta igual que en
            // el G-buffer; si esta ahi, el corte decide los oclusores). Se anota el tamano del DSV una vez cada 64 dibujos.
            int n = s_offPass.fetch_add(1);
            if ((n & 63) == 0) {
                ID3D11RenderTargetView* r[8] = {}; ID3D11DepthStencilView* dsv = nullptr; c->OMGetRenderTargets(8, r, &dsv);
                UINT nr = 0; for (auto* v : r) if (v) { nr++; v->Release(); } s_offRtv = nr;
                if (dsv) { ID3D11Resource* res = nullptr; dsv->GetResource(&res); dsv->Release();
                    ID3D11Texture2D* t = nullptr; if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); s_offW = d.Width; s_offH = d.Height; t->Release(); }
                    if (res) res->Release(); }
            }
        }
        return;
    }
    // (25/10 b) el juego dibuja su pasto tambien sin el PS de hojas (prepasada de profundidad, pasadas auxiliares con un
    // target del mismo tamano): si se emitiera ahi, las hojas del mod saldrian sin color (invisibles) y el estado guardado
    // para la base tendria un PS nulo. Solo cuenta el dibujo con el PS de hojas del G-buffer.
    if (!ps || (ps->hash != kGrassPS && RtvCount(c) < 2)) { s_skipNoPs++; if (extraOn) Why("dibujo del pasto del juego sin el PS de hojas / sin G-buffer (prepasada): se espera al del G-buffer"); return; }
    if (ps->hash == kGrassPS) t_gameGB = true;   // (26/10 g) dibujo del pasto del juego en el G-buffer: GrassGameStates le pone los estados de las hojas
    const int f = s_frame.load();
    // (25/10) estado del dibujo de pasto del juego, una vez por grabacion: con el se emite desde la base de la cancha
    if (extraOn && RecClaim(c, &GRec::captured)) CaptureState(c, vs, ps);
    if (RecClaim(c, &GRec::ball)) PitchBallNote(c, f);     // (20/10) posicion de la pelota para el pasto aplastado
    if (RecClaim(c, &GRec::clips)) PitchClipsDraw(c, f);   // recortes de pasto volando (una vez por grabacion; el estado del pasto ya esta puesto)
    // (20/10) pasto v4: tarjetas curvas propias en vez de las del juego (cerca). (26/10) Con el pasto del mod emitiendo y T0 >= 1000
    // el VS anula casi todas las parcelas del juego: no vale la pena cambiarles la malla (1.4x mas vertices que se anulan igual)
    { float T[4]; Thresholds(T); bool denseSkip = extraOn && T[0] >= 1000.0f && !s_missing.load();
      if (!t_grassSub.count && !denseSkip) DensePick(count, &t_grassSub); }
    if (!extraOn) { Why("apagado (w58)"); return; }
    // (27/10 l) tanda 9a, x299: si en esta grabacion la base de la cancha todavia no se dibujo, no se emite desde el pasto del juego: esas hojas
    // salian sin RT4 protegido (escribian 0 = sin la luz difusa / horneada del estadio) y sin el color del suelo (no hay copia de RT0), y si la GPU
    // las ejecutaba primero se quedaban con el cuadro. La emision desde la base (w294, despues del ultimo dibujo de la base) las dibuja bien
    if (g_cfg.live[299][0] > 0.5f && g_cfg.live[290][1] > 0.5f && !RecHas(c, &GRec::base)) { s_skipNoBase++; Why("pasto del juego sin la base debajo: emite la base (x299)"); return; }
    if (!RecClaim(c, &GRec::emitted)) return;
    if (!GrassEmit(c, true)) { RecUnclaim(c, &GRec::emitted); return; }
    if (c == g_dev.imm) s_recImmediate++; else s_recDeferred++;
}

// (25/10) al terminar el dibujo de la base de la cancha (PS 5e92 / c956): si en esta grabacion todavia no se emitio pasto
// del mod, se emite con el estado guardado. La base existe en todas las camaras, el pasto del juego no.
// (26/10 g) con w294 = 1 la emision espera al siguiente dibujo que no sea de la base (ver BasePend / GrassBaseFlush)
static bool IsTurfPS(uint64_t h) { return h == 0x5e92f2343848915aULL || h == 0xc9564b9b155f0a49ULL || h == 0x8d8f4e5fc80fc684ULL || h == 0x91d1b2f17d4f7671ULL; }
void GrassOnBaseDraw(ID3D11DeviceContext* c) {
    if (g_cfg.bypass || !g_dev.dev || g_cfg.live[58][3] < 0.5f) return;
    if (!MainPass(c)) return;
    if (RtvCount(c) < 2) return;   // (26/10 d) solo el G-buffer (una pasada auxiliar del mismo tamano se quedaria con el cuadro)
    // (26/10 f) stencil de la base para las hojas (x290) y "la base ya esta dibujada" (mezcla con el suelo, x291)
    ID3D11DepthStencilState* d = nullptr; UINT r = 0; c->OMGetDepthStencilState(&d, &r);
    { std::lock_guard<std::mutex> lk(s_bsMx);
      if (s_hyDev != g_dev.dev) { HyDropLocked(); s_hyDev = g_dev.dev; }
      if (s_baseDss) s_baseDss->Release(); s_baseDss = d; s_baseSref = r; s_baseDssOk = true; if (d) d->AddRef(); }
    RecClaim(c, &GRec::base);
    int nTurf = 0; bool done = false;
    { std::lock_guard<std::mutex> lk(s_recMx); GRec& g = s_rec[c]; nTurf = ++g.turfN; done = g.emitted; }
    { int m = s_turfMaxRec.load(); while (nTurf > m && !s_turfMaxRec.compare_exchange_weak(m, nTurf)) {} }
    if (done) { s_turfAfter++; if (d) d->Release(); return; }   // otra parte de la base despues de las hojas (queda debajo: log)
    if (g_cfg.live[294][3] < 0.5f || !EnsureGPU() || !s_cpVs2) {   // w294 = 0: como antes, enseguida despues del primer dibujo de la base
        if (d) d->Release();
        if (!RecClaim(c, &GRec::emitted)) return;
        if (!GrassEmit(c, false)) { RecUnclaim(c, &GRec::emitted); return; }
        if (c == g_dev.imm) s_recImmediate++; else s_recDeferred++;
        return;
    }
    std::lock_guard<std::mutex> lk(s_pendMx);
    auto it = s_pend.find(c);
    if (it == s_pend.end()) { it = s_pend.emplace(c, BasePend()).first; g_grassPendN++; }
    BasePend& p = it->second;
    p.turfN = nTurf;
    if (p.pres < 0) {   // la primera parte de la base fija la pasada (las siguientes son de la misma pasada: si no, el cambio de targets ya emitio)
        c->OMGetRenderTargets(8, p.rtv, &p.dsv);
        p.nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetViewports(&p.nvp, p.vp);
        p.nsc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetScissorRects(&p.nsc, p.sc);
        p.dss = d; d = nullptr; p.sref = r;
        ID3D11Buffer* b2 = nullptr; UINT f2 = 0, n2 = 0; ID3D11DeviceContext1* c1 = nullptr;
        if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) { c1->VSGetConstantBuffers1(2, 1, &b2, &f2, &n2); c1->Release(); }
        else c->VSGetConstantBuffers(2, 1, &b2);
        if (b2) { CopyCBWindow(c, b2, f2, n2, s_cpVs2, 512); b2->Release(); p.b2 = true; }
        p.pres = s_frame.load();
    }
    if (d) d->Release();
}

// (26/10 g) emite la base pendiente de este contexto. why: 0 dibujo (psHash = su PS; los de la base no emiten), 1 cambio de targets
// (nRtv / rtv / dsv = los nuevos: si son los mismos no se emite), 2 limpieza de la profundidad, 3 fin de la grabacion, 4 ejecucion de una lista,
// 5 ClearState
void GrassBaseFlush(ID3D11DeviceContext* c, int why, uint64_t psHash, UINT nRtv, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv) {
    if (t_inEmit || g_grassPendN.load(std::memory_order_relaxed) <= 0) return;
    if (why == 0 && IsTurfPS(psHash)) return;
    BasePend p;
    {
        std::lock_guard<std::mutex> lk(s_pendMx);
        auto it = s_pend.find(c); if (it == s_pend.end()) return;
        if (why == 1) {   // mismos targets (el juego vuelve a poner los de la pasada): sigue pendiente
            bool same = dsv == it->second.dsv;
            for (UINT i = 0; i < 8 && same; i++) same = (i < nRtv ? rtv[i] : nullptr) == it->second.rtv[i];
            if (same) return;
        }
        p = it->second; s_pend.erase(it); g_grassPendN--;
    }
    if (g_cfg.bypass || !g_dev.dev || g_cfg.live[58][3] < 0.5f || !RecClaim(c, &GRec::emitted)) { p.Release(); return; }
    t_inEmit = true;
    // la pasada de la base: si los targets cambiaron sin pasar por el gancho (raro), se ponen los de la base y se devuelve todo lo que el
    // runtime suelta al ponerlos (vistas de los shaders, UAVs)
    ID3D11RenderTargetView* cur[8] = {}; ID3D11DepthStencilView* curD = nullptr; c->OMGetRenderTargets(8, cur, &curD);
    bool same = curD == p.dsv; for (int i = 0; i < 8 && same; i++) same = cur[i] == p.rtv[i];
    struct Bank { ID3D11ShaderResourceView* v[3][D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {}; ID3D11UnorderedAccessView* u[D3D11_1_UAV_SLOT_COUNT] = {}; };
    std::unique_ptr<Bank> bank;
    if (!same) {
        s_flushOther++;
        bank.reset(new Bank());
        c->PSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, bank->v[0]); c->VSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, bank->v[1]);
        c->CSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, bank->v[2]);
        for (auto* v : cur) if (v) v->Release(); if (curD) curD->Release();
        c->OMGetRenderTargetsAndUnorderedAccessViews(8, cur, &curD, 0, D3D11_PS_CS_UAV_REGISTER_COUNT, bank->u);
        c->OMSetRenderTargets(8, p.rtv, p.dsv);
    }
    D3D11_VIEWPORT svVp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; UINT nSvVp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetViewports(&nSvVp, svVp);
    D3D11_RECT svSc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; UINT nSvSc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetScissorRects(&nSvSc, svSc);
    ID3D11Buffer* svB2 = nullptr; UINT svF2 = 0, svN2 = 0; ID3D11DeviceContext1* c1 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) c1->VSGetConstantBuffers1(2, 1, &svB2, &svF2, &svN2); else c->VSGetConstantBuffers(2, 1, &svB2);
    if (p.nvp) c->RSSetViewports(p.nvp, p.vp);
    c->RSSetScissorRects(p.nsc, p.nsc ? p.sc : nullptr);
    if (p.b2) c->VSSetConstantBuffers(2, 1, &s_cpVs2);   // la camara de la base, congelada al dibujarla
    if (GrassEmit(c, false, &p)) { if (c == g_dev.imm) s_recImmediate++; else s_recDeferred++; if (why >= 0 && why < 6) s_flushWhy[why]++; }
    else RecUnclaim(c, &GRec::emitted);
    if (c1 && svB2 && svN2) c1->VSSetConstantBuffers1(2, 1, &svB2, &svF2, &svN2); else c->VSSetConstantBuffers(2, 1, &svB2);
    if (svB2) svB2->Release(); if (c1) c1->Release();
    c->RSSetViewports(nSvVp, nSvVp ? svVp : nullptr); c->RSSetScissorRects(nSvSc, nSvSc ? svSc : nullptr);
    if (bank) {
        UINT keep[D3D11_1_UAV_SLOT_COUNT]; for (auto& k : keep) k = (UINT)-1;
        UINT nr = 0; for (int i = 0; i < 8; i++) if (cur[i]) nr = i + 1;
        c->OMSetRenderTargetsAndUnorderedAccessViews(nr, cur, curD, nr, D3D11_PS_CS_UAV_REGISTER_COUNT - nr, bank->u + nr, keep);
        c->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, bank->v[0]); c->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, bank->v[1]);
        c->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, bank->v[2]);
        for (auto& st : bank->v) for (auto* v : st) if (v) v->Release();
        for (auto* u : bank->u) if (u) u->Release();
    }
    for (auto* v : cur) if (v) v->Release(); if (curD) curD->Release();
    t_inEmit = false;
    p.Release();
}

// (26/10 g) el pasto del juego (cerca de la camara, hasta T0 = x294) con los mismos estados que las hojas del mod (w290): stencil de la
// base (x290: la pasada del sol con mapa de sombras lo ilumina como al suelo; el VS le apaga la sombra plana) y RT4 sin escribir (y290:
// queda la luz del estadio de la base). Sin esto, bajar x294 dejaba una zona alrededor de la camara con otra sombra y otra luz
void GrassGameStates(ID3D11DeviceContext* c, GrassStScope& s) {
    const bool gb = t_gameGB; t_gameGB = false;
    if (!gb || g_cfg.bypass || !g_dev.dev || g_cfg.live[58][3] < 0.5f || g_cfg.live[290][3] < 0.5f) return;
    const bool st = g_cfg.live[290][0] > 0.5f, rt4 = g_cfg.live[290][1] > 0.5f && RecHas(c, &GRec::base);   // RT4 solo con la base ya debajo
    if (!st && !rt4) return;
    c->OMGetDepthStencilState(&s.dss, &s.ref); c->OMGetBlendState(&s.bs, s.bf, &s.bm); s.on = true;
    bool stOk = false;
    if (st) {
        ID3D11DepthStencilState* bD = nullptr; UINT bR = 0; bool have = false;
        { std::lock_guard<std::mutex> lk(s_bsMx); if (s_baseDssOk && s_hyDev == g_dev.dev) { bD = s_baseDss; bR = s_baseSref; if (bD) bD->AddRef(); have = true; } }
        if (have && StencilDiffers(s.dss, s.ref, bD, bR)) if (ID3D11DepthStencilState* h = BladeDSS(s.dss, bD)) {
            ID3D11Buffer* fl = nullptr;
            { std::lock_guard<std::mutex> lk(s_bsMx);
              if (s_hyDev != g_dev.dev) { HyDropLocked(); s_hyDev = g_dev.dev; }
              if (!s_ggCb) { const float one[4] = { 1.f, 0.f, 0.f, 0.f }; D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                  D3D11_SUBRESOURCE_DATA sd = { one, 0, 0 }; g_dev.dev->CreateBuffer(&bd, &sd, &s_ggCb); }
              fl = s_ggCb; if (fl) fl->AddRef(); }
            if (fl) {   // sin la marca el VS dejaria la sombra plana encima del mapa de sombras: no se cambia el stencil
                c->VSGetConstantBuffers(12, 1, &s.vsB12); c->VSSetConstantBuffers(12, 1, &fl); fl->Release(); s.b12 = true;
                c->OMSetDepthStencilState(h, bR); stOk = true;
            }
            h->Release();
        }
        if (bD) bD->Release();
    }
    bool bmOk = false;
    if (rt4) if (ID3D11BlendState* nb = BladeBlend(s.bs, 1)) { c->OMSetBlendState(nb, s.bf, s.bm); nb->Release(); bmOk = true; }
    { static std::atomic<int> nLog{0}; if (nLog.fetch_add(1) < 2) Log("pasto tanda 6: pasto del juego con los estados de las hojas: stencil de la base %s, RT4 sin escribir %s", stOk ? "si" : "no", bmOk ? "si" : "no"); }
}
void GrassGameStatesRestore(ID3D11DeviceContext* c, GrassStScope& s) {
    if (!s.on) return;
    c->OMSetDepthStencilState(s.dss, s.ref); c->OMSetBlendState(s.bs, s.bf, s.bm);
    if (s.b12) { c->VSSetConstantBuffers(12, 1, &s.vsB12); if (s.vsB12) s.vsB12->Release(); }
    if (s.dss) s.dss->Release(); if (s.bs) s.bs->Release();
    s = GrassStScope();
}

} // namespace fx
