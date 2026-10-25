// PES21 FX (25/10) - la ropa choca con el cuerpo real (TorsoBody: torso, brazos, cuello).
// El simulador del mod de ropa (cloth_sim.hlsl, con las mejoras de PES21 FX) no conoce el cuerpo que hay debajo de la camiseta:
// adivina uno a partir de la propia camiseta en reposo. Con TorsoBody el cuerpo es una malla real (y la musculatura la deforma),
// asi que la tela flotaba en unos lados y el cuerpo la atravesaba en otros. Aca, en cada dibujo de la piel del cuerpo (VS
// 1c02a92a) se despacha un CS propio (shaders\pes_body_cs.hlsl) que deforma esa malla con los huesos de ESE dibujo (los mismos
// que usa el juego para dibujarla, con la musculatura incluida) y deja la piel en el espacio del modelo en una tabla hash por
// personaje. El simulador de la ropa la lee (t122..t124) y empuja cada punto de la tela afuera de la piel.
//
//   - Mallas: la primera vez que se ve una malla de piel se copian sus indices y vertices (posicion, normal, huesos, pesos) a
//     la CPU, se decodifican segun el input layout y, si es del torso / brazos / cuello (por altura y tamano: las manos y las
//     piernas quedan afuera), se suben a una copia propia (t0 del CS).
//   - Registros: por dibujo, los vertices deformados van a un anillo (1 M vertices: alcanza para 2 cuadros); la tabla del
//     personaje (128 tablas en anillo, una por personaje y contexto en el cuadro: el bloque b5 identifica al personaje) se
//     limpia antes del primer dibujo y recibe todas sus partes. La cabecera lleva el cuadro (fila 257) y la posicion del
//     personaje en el mundo, que el simulador compara con la de cada prenda.
//   - Parametros (fila 256): x prendido, y separacion de la tela con la piel (mm), z cuantos cm mas adentro puede ir la tela que
//     el cuerpo que adivina el mod, w empuje maximo por sub-paso (mm). Fila 257.x = numero de cuadro (lo escribe la DLL).
//   - El simulador corre antes que los dibujos del cuadro (el mod despacha la fisica antes de ejecutar la lista de comandos), asi
//     que normalmente usa la piel del cuadro anterior: la lleva al momento de cada sub-paso con los huesos de la propia prenda y
//     le suma lo que la piel se movio por su cuenta (un brazo que se acerca al costado).
#include "core.h"
#include <d3d11_1.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fx {

static const UINT kCells = 8192, kTabStride = 16 + kCells, kTables = 128;
static const UINT kRing = 1u << 20;      // vertices del anillo de registros
static const UINT kStatic = 1u << 18;    // vertices de las mallas registradas
static const UINT kMeshMax = 12288;      // vertices por malla (las manos de TorsoBody tienen 25.900)
static const uint64_t kSkinVS = 0x1c02a92ab4ae3362ULL;

struct Elem { int slot = -1; UINT off = 0; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; };
struct BodyMesh {
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0, start = 0, count = 0; INT base = 0;
    ID3D11Buffer* vb[4] = {}; UINT stride[4] = {}, off[4] = {};
    Elem pos, nrm, idx, wgt;
    int state = 0;                        // 0 nueva, 1 copiando indices, 2 copiando vertices, 3 lista, 4 descartada
    ID3D11Buffer* stIB = nullptr; ID3D11Buffer* stVB[4] = {};
    int tries = 0, id = 0, lastFrame = 0;
    UINT vmin = 0, vmax = 0, nv = 0, sBase = 0;
    float bmin[3] = { 0, 0, 0 }, bmax[3] = { 0, 0, 0 };
    std::string why;
};
struct TabKey { void* ctx; void* cb; UINT first; bool operator==(const TabKey& o) const { return ctx == o.ctx && cb == o.cb && first == o.first; } };
struct TabKeyHash { size_t operator()(const TabKey& k) const { return std::hash<void*>()(k.ctx) ^ (std::hash<void*>()(k.cb) * 31u) ^ ((size_t)k.first * 2654435761u); } };
struct TabUse { UINT tab = 0; std::vector<int> meshes; };

static std::mutex s_mx;
static std::unordered_map<uint64_t, BodyMesh*> s_meshes;
static std::vector<BodyMesh*> s_pending;
static std::unordered_map<TabKey, TabUse, TabKeyHash> s_gen;   // tablas usadas desde el ultimo Present
static UINT s_tabNext = 0, s_ringPos = 0, s_genVerts = 0;
static int s_nextId = 1;
static std::vector<std::pair<UINT, UINT>> s_free;              // huecos de la copia propia (inicio, cantidad)
static std::atomic<int> s_simSeen{-100000};
static std::atomic<int> s_recs{0}, s_tabs{0}, s_skipBudget{0}, s_skipNoTab{0};
static int s_logRecs = 0, s_logFrames = 0, s_logVerts = 0, s_logTabs = 0;

// recursos (dispositivo del juego)
static ID3D11Device* s_dev = nullptr;
static ID3D11ComputeShader* s_cs = nullptr;
static ID3D11Buffer *s_static = nullptr, *s_ringV = nullptr, *s_ringN = nullptr, *s_tab = nullptr, *s_cb = nullptr;
static ID3D11ShaderResourceView *s_staticSRV = nullptr, *s_ringVSRV = nullptr, *s_ringNSRV = nullptr, *s_tabSRV = nullptr;
static ID3D11UnorderedAccessView *s_ringVUAV = nullptr, *s_ringNUAV = nullptr;
static ID3D11UnorderedAccessView* s_tabUAV[kTables] = {};
static std::atomic<bool> s_ready{false};
static bool s_failed = false, s_csTried = false;

void BodySimSeen() { s_simSeen.store(PresentNo(), std::memory_order_relaxed); }

static void CompileCS() {
    if (!s_dev) return;
    ID3D11ComputeShader* cs = nullptr;
    if (ID3DBlob* b = CompileNamedShader(L"pes_body_cs.hlsl", "cs_5_0")) { s_dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &cs); b->Release(); }
    if (!cs) { Log("cuerpo: no se pudo compilar pes_body_cs.hlsl (la ropa no choca con el cuerpo real)"); return; }
    s_cs = cs;   // el anterior se deja vivo (otro hilo podria estar usandolo)
}
void BodyReloadShaders() { std::lock_guard<std::mutex> lk(s_mx); if (s_dev) CompileCS(); }

static bool MakeBuf(UINT bytes, UINT stride, UINT bind, UINT misc, ID3D11Buffer** out) {
    D3D11_BUFFER_DESC d{}; d.ByteWidth = bytes; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = bind; d.MiscFlags = misc; d.StructureByteStride = stride;
    return SUCCEEDED(s_dev->CreateBuffer(&d, nullptr, out)) && *out;
}
static bool CreateResources() {
    s_dev = g_dev.dev;
    const UINT B = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bool ok = MakeBuf(kStatic * 32, 16, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, &s_static) &&
              MakeBuf(kRing * 16, 16, B, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, &s_ringV) &&
              MakeBuf(kRing * 4, 4, B, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, &s_ringN) &&
              MakeBuf(kTables * kTabStride * 4, 0, B, 0, &s_tab);
    if (ok) {
        D3D11_BUFFER_DESC d{}; d.ByteWidth = 16; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ok = SUCCEEDED(s_dev->CreateBuffer(&d, nullptr, &s_cb)) && s_cb;
    }
    if (ok) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.Buffer.FirstElement = 0; sd.Buffer.NumElements = kStatic * 2; ok = ok && SUCCEEDED(s_dev->CreateShaderResourceView(s_static, &sd, &s_staticSRV));
        sd.Buffer.NumElements = kRing; ok = ok && SUCCEEDED(s_dev->CreateShaderResourceView(s_ringV, &sd, &s_ringVSRV));
        ok = ok && SUCCEEDED(s_dev->CreateShaderResourceView(s_ringN, &sd, &s_ringNSRV));
        sd.Format = DXGI_FORMAT_R32_UINT; sd.Buffer.NumElements = kTables * kTabStride; ok = ok && SUCCEEDED(s_dev->CreateShaderResourceView(s_tab, &sd, &s_tabSRV));
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Format = DXGI_FORMAT_UNKNOWN;
        ud.Buffer.FirstElement = 0; ud.Buffer.NumElements = kRing;
        ok = ok && SUCCEEDED(s_dev->CreateUnorderedAccessView(s_ringV, &ud, &s_ringVUAV)) && SUCCEEDED(s_dev->CreateUnorderedAccessView(s_ringN, &ud, &s_ringNUAV));
        ud.Format = DXGI_FORMAT_R32_UINT; ud.Buffer.NumElements = kTabStride;
        for (UINT t = 0; t < kTables && ok; t++) { ud.Buffer.FirstElement = t * kTabStride; ok = SUCCEEDED(s_dev->CreateUnorderedAccessView(s_tab, &ud, &s_tabUAV[t])); }
    }
    if (!ok) { Log("cuerpo: no se pudieron crear los buffers (la ropa no choca con el cuerpo real)"); s_failed = true; return false; }
    // la tabla arranca en cero: ninguna cabecera valida hasta que se escriba
    const UINT z[4] = { 0, 0, 0, 0 };
    for (UINT t = 0; t < kTables; t++) g_dev.imm->ClearUnorderedAccessViewUint(s_tabUAV[t], z);
    s_free.assign(1, { 0u, kStatic });
    CompileCS();
    Log("cuerpo: listo (%u KB de tablas, anillo de %u vertices, copia de %u vertices)%s", kTables * kTabStride * 4 / 1024, kRing, kStatic, s_cs ? "" : " - SIN el CS");
    return true;
}
void BodyClothSRVs(ID3D11ShaderResourceView* out[3]) {
    const bool on = s_ready.load(std::memory_order_acquire);
    out[0] = on ? s_tabSRV : nullptr; out[1] = on ? s_ringVSRV : nullptr; out[2] = on ? s_ringNSRV : nullptr;
}

// ---------------------------------------------------------------- formatos de vertice
static UINT FmtBytes(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT: return 16;
    case DXGI_FORMAT_R32G32B32_FLOAT: case DXGI_FORMAT_R32G32B32_UINT: case DXGI_FORMAT_R32G32B32_SINT: return 12;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM: case DXGI_FORMAT_R16G16B16A16_SINT: case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R32G32_UINT: return 8;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_SNORM: case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_UINT: case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_UNORM: case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R16G16_UINT: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_UINT: return 4;
    default: return 0;
    }
}
static float Half(uint16_t h) {
    uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023, f;
    if (e == 0) { if (!m) f = s << 31; else { e = 1; while (!(m & 1024)) { m <<= 1; e--; } m &= 1023; f = (s << 31) | ((e + 112) << 23) | (m << 13); } }
    else if (e == 31) f = (s << 31) | 0x7f800000u | (m << 13);
    else f = (s << 31) | ((e + 112) << 23) | (m << 13);
    float r; memcpy(&r, &f, 4); return r;
}
// componentes faltantes: (0, 0, 0, 1), como el IA
static bool ReadF(const uint8_t* p, DXGI_FORMAT f, float o[4]) {
    o[0] = o[1] = o[2] = 0.0f; o[3] = 1.0f;
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: memcpy(o, p, 16); return true;
    case DXGI_FORMAT_R32G32B32_FLOAT: memcpy(o, p, 12); return true;
    case DXGI_FORMAT_R32G32_FLOAT: memcpy(o, p, 8); return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: for (int i = 0; i < 4; i++) { uint16_t h; memcpy(&h, p + 2 * i, 2); o[i] = Half(h); } return true;
    case DXGI_FORMAT_R16G16_FLOAT: for (int i = 0; i < 2; i++) { uint16_t h; memcpy(&h, p + 2 * i, 2); o[i] = Half(h); } return true;
    case DXGI_FORMAT_R16G16B16A16_UNORM: for (int i = 0; i < 4; i++) { uint16_t h; memcpy(&h, p + 2 * i, 2); o[i] = h / 65535.0f; } return true;
    case DXGI_FORMAT_R16G16B16A16_SNORM: for (int i = 0; i < 4; i++) { int16_t h; memcpy(&h, p + 2 * i, 2); o[i] = (std::max)(h / 32767.0f, -1.0f); } return true;
    case DXGI_FORMAT_R16G16_UNORM: for (int i = 0; i < 2; i++) { uint16_t h; memcpy(&h, p + 2 * i, 2); o[i] = h / 65535.0f; } return true;
    case DXGI_FORMAT_R16G16_SNORM: for (int i = 0; i < 2; i++) { int16_t h; memcpy(&h, p + 2 * i, 2); o[i] = (std::max)(h / 32767.0f, -1.0f); } return true;
    case DXGI_FORMAT_R8G8B8A8_UNORM: for (int i = 0; i < 4; i++) o[i] = p[i] / 255.0f; return true;
    case DXGI_FORMAT_B8G8R8A8_UNORM: o[0] = p[2] / 255.0f; o[1] = p[1] / 255.0f; o[2] = p[0] / 255.0f; o[3] = p[3] / 255.0f; return true;
    case DXGI_FORMAT_R8G8B8A8_SNORM: for (int i = 0; i < 4; i++) o[i] = (std::max)((int8_t)p[i] / 127.0f, -1.0f); return true;
    case DXGI_FORMAT_R10G10B10A2_UNORM: { uint32_t u; memcpy(&u, p, 4); o[0] = (u & 1023) / 1023.0f; o[1] = ((u >> 10) & 1023) / 1023.0f; o[2] = ((u >> 20) & 1023) / 1023.0f; o[3] = (u >> 30) / 3.0f; return true; }
    default: return false;
    }
}
static bool ReadU(const uint8_t* p, DXGI_FORMAT f, uint32_t o[4]) {
    o[0] = o[1] = o[2] = 0; o[3] = 1;
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_UNORM: for (int i = 0; i < 4; i++) o[i] = p[i]; return true;
    case DXGI_FORMAT_R16G16B16A16_UINT: for (int i = 0; i < 4; i++) { uint16_t h; memcpy(&h, p + 2 * i, 2); o[i] = h; } return true;
    case DXGI_FORMAT_R32G32B32A32_UINT: memcpy(o, p, 16); return true;
    case DXGI_FORMAT_R32G32B32_UINT: memcpy(o, p, 12); return true;
    default: return false;
    }
}

static uint64_t Fnv(uint64_t h, uint64_t v) { for (int i = 0; i < 8; i++) { h ^= (v >> (8 * i)) & 255; h *= 0x100000001b3ULL; } return h; }

static void ReleaseMesh(BodyMesh* m) {
    if (m->ib) m->ib->Release(); m->ib = nullptr;
    for (auto*& b : m->vb) { if (b) b->Release(); b = nullptr; }
    if (m->stIB) m->stIB->Release(); m->stIB = nullptr;
    for (auto*& b : m->stVB) { if (b) b->Release(); b = nullptr; }
}
static bool AllocStatic(UINT n, UINT* out) {
    for (size_t i = 0; i < s_free.size(); i++) if (s_free[i].second >= n) {
        *out = s_free[i].first; s_free[i].first += n; s_free[i].second -= n;
        if (!s_free[i].second) s_free.erase(s_free.begin() + i);
        return true;
    }
    return false;
}
static void FreeStatic(UINT start, UINT n) {
    s_free.push_back({ start, n });
    std::sort(s_free.begin(), s_free.end());
    for (size_t i = 0; i + 1 < s_free.size();) {
        if (s_free[i].first + s_free[i].second == s_free[i + 1].first) { s_free[i].second += s_free[i + 1].second; s_free.erase(s_free.begin() + i + 1); }
        else i++;
    }
}

// ---------------------------------------------------------------- en cada dibujo de la piel del cuerpo
struct CBSlot { ID3D11Buffer* b = nullptr; UINT f = 0, n = 0; };
static void GetCB(ID3D11DeviceContext1* c1, bool vs, UINT slot, CBSlot& s) { if (vs) c1->VSGetConstantBuffers1(slot, 1, &s.b, &s.f, &s.n); else c1->CSGetConstantBuffers1(slot, 1, &s.b, &s.f, &s.n); }
static void SetCSCB(ID3D11DeviceContext1* c1, UINT slot, const CBSlot& s) {
    bool window = false;
    if (s.b) { D3D11_BUFFER_DESC d; s.b->GetDesc(&d); window = s.f > 0 || (s.n > 0 && s.n * 16 < d.ByteWidth); }
    if (window) c1->CSSetConstantBuffers1(slot, 1, &s.b, &s.f, &s.n); else c1->CSSetConstantBuffers(slot, 1, &s.b);
}

void BodyOnDraw(ID3D11DeviceContext* c, UINT count, UINT start, INT base) {
    if (!s_ready.load(std::memory_order_acquire) || !s_cs || count < 3) return;
    if (g_cfg.live[256][0] < 0.5f) return;
    const int frame = PresentNo();
    if (frame - s_simSeen.load(std::memory_order_relaxed) > 300) return;   // el mod de ropa no esta simulando (F5, menus sin ropa)
    ID3D11Device* cd = nullptr; c->GetDevice(&cd); if (cd) cd->Release();
    if (cd != s_dev) return;
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0; c->IAGetIndexBuffer(&ib, &ibFmt, &ibOff);
    ID3D11Buffer* vb[4] = {}; UINT stride[4] = {}, off[4] = {}; c->IAGetVertexBuffers(0, 4, vb, stride, off);
    ID3D11InputLayout* il = nullptr; c->IAGetInputLayout(&il);
    auto releaseIA = [&]() { if (ib) ib->Release(); for (auto* b : vb) if (b) b->Release(); if (il) il->Release(); };
    if (!ib || !il || (ibFmt != DXGI_FORMAT_R16_UINT && ibFmt != DXGI_FORMAT_R32_UINT)) { releaseIA(); return; }
    uint64_t key = 0xcbf29ce484222325ULL;
    key = Fnv(key, (uint64_t)ib); key = Fnv(key, ibOff); key = Fnv(key, start); key = Fnv(key, count); key = Fnv(key, (uint64_t)(int64_t)base);
    key = Fnv(key, (uint64_t)il); for (int i = 0; i < 4; i++) { key = Fnv(key, (uint64_t)vb[i]); key = Fnv(key, off[i]); }
    BodyMesh* m = nullptr;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        auto it = s_meshes.find(key);
        if (it == s_meshes.end()) {
            if (s_meshes.size() >= 512) { releaseIA(); return; }
            m = new BodyMesh(); m->id = s_nextId++; m->lastFrame = frame;
            m->ib = ib; m->ibFmt = ibFmt; m->ibOff = ibOff; m->start = start; m->count = count; m->base = base;
            for (int i = 0; i < 4; i++) { m->vb[i] = vb[i]; m->stride[i] = stride[i]; m->off[i] = off[i]; }
            ib = nullptr; for (auto*& b : vb) b = nullptr;   // las referencias pasan a la malla
            // elementos del input layout (POSITION0, NORMAL0, BLENDINDICES0, BLENDWEIGHT0)
            std::vector<InputElemInfo> els;
            if (!InputLayoutInfo(il, els)) { m->state = 4; m->why = "input layout no anotado"; }
            else {
                UINT run[16] = {};
                for (auto& e : els) {
                    if (e.slot >= 16) continue;
                    const UINT o = (e.off == D3D11_APPEND_ALIGNED_ELEMENT) ? run[e.slot] : e.off;
                    run[e.slot] = o + FmtBytes((DXGI_FORMAT)e.fmt);
                    Elem* t = nullptr;
                    if (e.idx == 0 && !_stricmp(e.sem.c_str(), "POSITION")) t = &m->pos;
                    else if (e.idx == 0 && !_stricmp(e.sem.c_str(), "NORMAL")) t = &m->nrm;
                    else if (e.idx == 0 && !_stricmp(e.sem.c_str(), "BLENDINDICES")) t = &m->idx;
                    else if (e.idx == 0 && !_stricmp(e.sem.c_str(), "BLENDWEIGHT")) t = &m->wgt;
                    if (t && e.slot < 4) { t->slot = (int)e.slot; t->off = o; t->fmt = (DXGI_FORMAT)e.fmt; }
                }
                if (m->pos.slot < 0 || m->idx.slot < 0 || m->wgt.slot < 0) { m->state = 4; m->why = "sin POSITION / BLENDINDICES / BLENDWEIGHT"; }
                else if (!m->vb[m->pos.slot] || !m->vb[m->idx.slot] || !m->vb[m->wgt.slot] || (m->nrm.slot >= 0 && !m->vb[m->nrm.slot])) { m->state = 4; m->why = "falta un buffer de vertices"; }
            }
            s_meshes[key] = m;
            if (m->state == 0) s_pending.push_back(m);
            else ReleaseMesh(m);
            releaseIA();
            return;
        }
        m = it->second;
        m->lastFrame = frame;
    }
    releaseIA();
    if (m->state != 3) return;
    // personaje del dibujo: bloque de constantes por objeto (b5) en este contexto
    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) || !c1) return;
    CBSlot g2, g5, g6; GetCB(c1, true, 2, g2); GetCB(c1, true, 5, g5); GetCB(c1, true, 6, g6);
    auto releaseCB = [&]() { if (g2.b) g2.b->Release(); if (g5.b) g5.b->Release(); if (g6.b) g6.b->Release(); c1->Release(); };
    if (!g2.b || !g5.b || !g6.b) { releaseCB(); return; }
    UINT tab = 0, oBase = 0; bool first = false;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        TabKey k{ c, g5.b, g5.f };
        auto it = s_gen.find(k);
        if (it == s_gen.end()) {
            if (s_gen.size() >= kTables / 2) { s_skipNoTab++; releaseCB(); return; }   // mas de 64 personajes en un cuadro
            TabUse u; u.tab = s_tabNext++ % kTables; it = s_gen.emplace(k, u).first; first = true; s_tabs++;
        }
        TabUse& u = it->second;
        if (std::find(u.meshes.begin(), u.meshes.end(), m->id) != u.meshes.end()) { releaseCB(); return; }   // la misma parte otra vez (otra pasada)
        if (s_genVerts + m->nv > kRing / 2) { s_skipBudget++; releaseCB(); return; }   // el anillo tiene que guardar dos cuadros
        u.meshes.push_back(m->id);
        tab = u.tab;
        if (s_ringPos + m->nv > kRing) s_ringPos = 0;
        oBase = s_ringPos; s_ringPos += m->nv; s_genVerts += m->nv;
    }
    D3D11_MAPPED_SUBRESOURCE mp;
    if (FAILED(c->Map(s_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) { releaseCB(); return; }
    const UINT prm[4] = { m->sBase, m->nv, oBase, (UINT)m->id };
    memcpy(mp.pData, prm, sizeof(prm)); c->Unmap(s_cb, 0);
    // estado del CS del juego (se devuelve igual)
    ID3D11ComputeShader* pcs = nullptr; c->CSGetShader(&pcs, nullptr, nullptr);
    CBSlot p2, p5, p6, p7; GetCB(c1, false, 2, p2); GetCB(c1, false, 5, p5); GetCB(c1, false, 6, p6); GetCB(c1, false, 7, p7);
    ID3D11ShaderResourceView* ps0 = nullptr; ID3D11ShaderResourceView* ps120 = nullptr; c->CSGetShaderResources(0, 1, &ps0); c->CSGetShaderResources(120, 1, &ps120);
    ID3D11UnorderedAccessView* pu[3] = {}; c->CSGetUnorderedAccessViews(0, 3, pu);
    if (first) { const UINT z[4] = { 0, 0, 0, 0 }; c->ClearUnorderedAccessViewUint(s_tabUAV[tab], z); }
    c->CSSetShader(s_cs, nullptr, 0);
    SetCSCB(c1, 2, g2); SetCSCB(c1, 5, g5); SetCSCB(c1, 6, g6);
    { ID3D11Buffer* b = s_cb; c->CSSetConstantBuffers(7, 1, &b); }
    { ID3D11ShaderResourceView* v = s_staticSRV; c->CSSetShaderResources(0, 1, &v); ID3D11ShaderResourceView* pv = g_dev.paramSRV; c->CSSetShaderResources(120, 1, &pv); }
    { ID3D11UnorderedAccessView* u[3] = { s_ringVUAV, s_ringNUAV, s_tabUAV[tab] }; c->CSSetUnorderedAccessViews(0, 3, u, nullptr); }
    c->Dispatch((m->nv + 63) / 64, 1, 1);
    c->CSSetUnorderedAccessViews(0, 3, pu, nullptr);
    c->CSSetShaderResources(0, 1, &ps0); c->CSSetShaderResources(120, 1, &ps120);
    SetCSCB(c1, 2, p2); SetCSCB(c1, 5, p5); SetCSCB(c1, 6, p6); SetCSCB(c1, 7, p7);
    c->CSSetShader(pcs, nullptr, 0);
    if (pcs) pcs->Release(); if (ps0) ps0->Release(); if (ps120) ps120->Release(); for (auto* u : pu) if (u) u->Release();
    for (CBSlot* s : { &p2, &p5, &p6, &p7 }) if (s->b) s->b->Release();
    releaseCB();
    s_recs++;
}

// ---------------------------------------------------------------- registro de mallas (Present, contexto inmediato)
static ID3D11Buffer* Staging(UINT bytes) {
    D3D11_BUFFER_DESC d{}; d.ByteWidth = (bytes + 15) & ~15u; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* b = nullptr; return SUCCEEDED(s_dev->CreateBuffer(&d, nullptr, &b)) ? b : nullptr;
}
static void Reject(BodyMesh* m, const char* why) {
    m->state = 4; m->why = why; ReleaseMesh(m);
    static int n = 0; if (n++ < 40) Log("cuerpo: malla %d descartada (%u indices desde %u): %s", m->id, m->count, m->start, why);
}
static void StepMesh(ID3D11DeviceContext* imm, BodyMesh* m) {
    const UINT isz = m->ibFmt == DXGI_FORMAT_R16_UINT ? 2 : 4;
    if (m->state == 0) {
        D3D11_BUFFER_DESC d; m->ib->GetDesc(&d);
        const UINT a = m->ibOff + m->start * isz, b = a + m->count * isz;
        if (b > d.ByteWidth) { Reject(m, "indices fuera del buffer"); return; }
        if (!(m->stIB = Staging(m->count * isz))) { Reject(m, "sin memoria para la copia"); return; }
        D3D11_BOX bx = { a, 0, 0, b, 1, 1 }; imm->CopySubresourceRegion(m->stIB, 0, 0, 0, 0, m->ib, 0, &bx);
        m->state = 1; m->tries = 0; return;
    }
    if (m->state == 1) {
        D3D11_MAPPED_SUBRESOURCE mp; HRESULT hr = imm->Map(m->stIB, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mp);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) { if (++m->tries > 120) Reject(m, "la copia de los indices no llego"); return; }
        if (FAILED(hr)) { Reject(m, "no se pudieron leer los indices"); return; }
        UINT lo = 0xffffffffu, hi = 0;
        for (UINT i = 0; i < m->count; i++) { UINT v = isz == 2 ? ((const uint16_t*)mp.pData)[i] : ((const uint32_t*)mp.pData)[i]; lo = (std::min)(lo, v); hi = (std::max)(hi, v); }
        imm->Unmap(m->stIB, 0); m->stIB->Release(); m->stIB = nullptr;
        if (hi < lo) { Reject(m, "sin indices"); return; }
        m->vmin = lo; m->vmax = hi; m->nv = hi - lo + 1;
        if (m->nv > kMeshMax) { Reject(m, "demasiados vertices (manos o piernas)"); return; }
        const int slots[4] = { m->pos.slot, m->nrm.slot, m->idx.slot, m->wgt.slot };
        for (int s : slots) {
            if (s < 0 || m->stVB[s]) continue;
            D3D11_BUFFER_DESC d; m->vb[s]->GetDesc(&d);
            const UINT64 a = (UINT64)m->off[s] + (UINT64)((INT64)m->base + lo) * m->stride[s], b = a + (UINT64)m->nv * m->stride[s];
            if (!m->stride[s] || b > d.ByteWidth) { Reject(m, "vertices fuera del buffer"); return; }
            if (!(m->stVB[s] = Staging((UINT)(b - a)))) { Reject(m, "sin memoria para la copia"); return; }
            D3D11_BOX bx = { (UINT)a, 0, 0, (UINT)b, 1, 1 }; imm->CopySubresourceRegion(m->stVB[s], 0, 0, 0, 0, m->vb[s], 0, &bx);
        }
        m->state = 2; m->tries = 0; return;
    }
    if (m->state == 2) {
        D3D11_MAPPED_SUBRESOURCE mp[4] = {}; bool mapped[4] = {};
        for (int s = 0; s < 4; s++) if (m->stVB[s]) {
            HRESULT hr = imm->Map(m->stVB[s], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mp[s]);
            if (FAILED(hr)) {
                for (int k = 0; k < 4; k++) if (mapped[k]) imm->Unmap(m->stVB[k], 0);
                if (hr == DXGI_ERROR_WAS_STILL_DRAWING) { if (++m->tries > 120) Reject(m, "la copia de los vertices no llego"); }
                else Reject(m, "no se pudieron leer los vertices");
                return;
            }
            mapped[s] = true;
        }
        std::vector<uint32_t> out((size_t)m->nv * 8);
        float bmin[3] = { 1e9f, 1e9f, 1e9f }, bmax[3] = { -1e9f, -1e9f, -1e9f }; bool ok = true; int weighted = 0;
        for (UINT v = 0; v < m->nv && ok; v++) {
            float P[4], N[4] = { 0, 1, 0, 0 }, W[4]; uint32_t I[4];
            const uint8_t* pp = (const uint8_t*)mp[m->pos.slot].pData + (size_t)v * m->stride[m->pos.slot] + m->pos.off;
            const uint8_t* pi = (const uint8_t*)mp[m->idx.slot].pData + (size_t)v * m->stride[m->idx.slot] + m->idx.off;
            const uint8_t* pw = (const uint8_t*)mp[m->wgt.slot].pData + (size_t)v * m->stride[m->wgt.slot] + m->wgt.off;
            ok = ReadF(pp, m->pos.fmt, P) && ReadU(pi, m->idx.fmt, I) && ReadF(pw, m->wgt.fmt, W);
            if (ok && m->nrm.slot >= 0) {
                const uint8_t* pn = (const uint8_t*)mp[m->nrm.slot].pData + (size_t)v * m->stride[m->nrm.slot] + m->nrm.off;
                ok = ReadF(pn, m->nrm.fmt, N);
                if (ok && (m->nrm.fmt == DXGI_FORMAT_R8G8B8A8_UNORM || m->nrm.fmt == DXGI_FORMAT_B8G8R8A8_UNORM || m->nrm.fmt == DXGI_FORMAT_R10G10B10A2_UNORM || m->nrm.fmt == DXGI_FORMAT_R16G16B16A16_UNORM))
                    for (int k = 0; k < 3; k++) N[k] = N[k] * 2.0f - 1.0f;   // normales guardadas en 0..1
            }
            if (!ok) break;
            for (int k = 0; k < 3; k++) { bmin[k] = (std::min)(bmin[k], P[k]); bmax[k] = (std::max)(bmax[k], P[k]); }
            const float nl = sqrtf(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
            if (nl > 1e-6f) for (int k = 0; k < 3; k++) N[k] /= nl;
            uint32_t ip = 0, wp = 0;
            for (int k = 0; k < 4; k++) { ip |= ((std::min)(I[k], 255u)) << (8 * k); wp |= ((uint32_t)(std::clamp(W[k], 0.0f, 1.0f) * 255.0f + 0.5f)) << (8 * k); }
            if (wp) weighted++;
            uint32_t* o = &out[(size_t)v * 8];
            memcpy(o, P, 12); o[3] = ip; memcpy(o + 4, N, 12); o[7] = wp;
        }
        for (int s = 0; s < 4; s++) if (mapped[s]) imm->Unmap(m->stVB[s], 0);
        for (auto*& b : m->stVB) { if (b) b->Release(); b = nullptr; }
        if (!ok) { Reject(m, "formato de vertice no soportado"); return; }
        memcpy(m->bmin, bmin, sizeof(bmin)); memcpy(m->bmax, bmax, sizeof(bmax));
        // solo lo que puede tocar la camiseta: torso, brazos y cuello (en reposo, de 0.9 a 1.85 m); piernas y pies afuera
        if (bmax[1] < 0.95f || bmin[1] > 1.85f) { Reject(m, "fuera de la zona de la camiseta"); return; }
        UINT sb = 0;
        if (!AllocStatic(m->nv, &sb)) { Reject(m, "copia propia llena"); return; }
        m->sBase = sb;
        D3D11_BOX bx = { sb * 32, 0, 0, (sb + m->nv) * 32, 1, 1 };
        imm->UpdateSubresource(s_static, 0, &bx, out.data(), 0, 0);
        m->state = 3;
        // las referencias al buffer de indices y de vertices quedan: mientras la malla viva, ningun buffer nuevo reusa su direccion
        static int n = 0;
        if (n++ < 60) Log("cuerpo: malla %d lista: %u vertices (%u indices desde %u), reposo x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f, %d con pesos, formatos pos %d normal %d huesos %d pesos %d",
                          m->id, m->nv, m->count, m->start, bmin[0], bmax[0], bmin[1], bmax[1], bmin[2], bmax[2], weighted, (int)m->pos.fmt, (int)m->nrm.fmt, (int)m->idx.fmt, (int)m->wgt.fmt);
    }
}

void BodyPresent() {
    if (!g_dev.dev || !g_dev.imm || s_failed) return;
    if (!s_ready.load(std::memory_order_acquire)) {
        if (g_dev.dev != s_dev && s_dev) return;   // otro dispositivo: no se mezcla
        std::lock_guard<std::mutex> lk(s_mx);
        if (!CreateResources()) return;
        s_ready.store(true, std::memory_order_release);
    }
    if (g_dev.dev != s_dev) return;
    const int frame = PresentNo();
    std::vector<BodyMesh*> dead;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        s_logVerts += s_genVerts; s_logTabs += (int)s_gen.size();
        s_gen.clear(); s_genVerts = 0;
        // mallas nuevas: hasta 3 pasos por cuadro (cada malla tarda 3 cuadros: indices, vertices, subida)
        int steps = 0;
        for (size_t i = 0; i < s_pending.size() && steps < 3;) {
            BodyMesh* m = s_pending[i]; StepMesh(g_dev.imm, m); steps++;
            if (m->state >= 3) s_pending.erase(s_pending.begin() + i); else i++;
        }
        // mallas que no se dibujan hace 30 s: se sueltan (el juego cambio de modelos)
        if ((frame & 63) == 0) for (auto it = s_meshes.begin(); it != s_meshes.end();) {
            BodyMesh* m = it->second;
            if (frame - m->lastFrame > 1800 && m->state >= 3) {
                if (m->state == 3) FreeStatic(m->sBase, m->nv);
                ReleaseMesh(m); dead.push_back(m); it = s_meshes.erase(it);
            } else ++it;
        }
    }
    for (auto* m : dead) delete m;
    s_logRecs += s_recs.exchange(0); s_logFrames++;
    static double nextLog = 0; const double t = Now();
    if (t > nextLog) {
        nextLog = t + 20.0;
        if (s_logFrames > 0 && (s_logRecs > 0 || frame - s_simSeen.load() < 300)) {
            int ready = 0, rej = 0, pend = 0;
            { std::lock_guard<std::mutex> lk(s_mx); for (auto& kv : s_meshes) { if (kv.second->state == 3) ready++; else if (kv.second->state == 4) rej++; else pend++; } }
            Log("cuerpo: %d mallas de piel listas (%d descartadas, %d en camino); por cuadro %.1f partes de cuerpo en %.1f personajes, %.0f vertices%s%s",
                ready, rej, pend, s_logRecs / (float)s_logFrames, s_logTabs / (float)s_logFrames, s_logVerts / (float)s_logFrames,
                s_skipBudget.load() ? " (anillo lleno: se saltearon partes)" : "", s_skipNoTab.load() ? " (mas de 64 personajes)" : "");
        }
        s_logRecs = s_logFrames = s_logVerts = s_logTabs = 0; s_skipBudget = 0; s_skipNoTab = 0;
    }
}

} // namespace fx
