// PES21 FX (25/10) - la ropa choca con el cuerpo real (TorsoBody: torso, brazos, cuello).
// El simulador del mod de ropa (cloth_sim.hlsl, con las mejoras de PES21 FX) no conoce el cuerpo que hay debajo de la camiseta:
// adivina uno a partir de la propia camiseta en reposo. Con TorsoBody el cuerpo es una malla real (y la musculatura la deforma),
// asi que la tela flotaba en unos lados y el cuerpo la atravesaba en otros. Aca la piel del cuerpo (VS 1c02a92a) de cada
// personaje se deforma con los huesos de su dibujo (los mismos que usa el juego, con la musculatura incluida) y queda en el
// espacio del modelo en una tabla hash por personaje. El simulador de la ropa la lee (t122..t124) y empuja la tela afuera.
//
//   - Mallas: la primera vez que se ve una malla de piel se copian sus indices y vertices (posicion, normal, huesos, pesos) a
//     la CPU, se decodifican segun el input layout y, si es del torso / brazos / cuello (por altura y lugar: manos y piernas
//     quedan afuera), se suben a una copia propia (t0 de los CS).
//   - (25/10 e) En cada dibujo solo se COPIAN sus constantes (m_view de b2, el bloque por objeto b5 y los huesos b6) a un lugar
//     propio: nada de compute entre los dibujos del juego. Los registros de cada contexto diferido pasan a su lista de comandos
//     (FinishCommandList) y cuentan cuando la lista se ejecuta (ExecuteCommandList): en Present se procesa todo lo ejecutado en
//     el cuadro de una vez: pes_body_assign_cs.hlsl agrupa los dibujos por personaje (origen del modelo en el mundo a menos de
//     2 cm: el bloque b5 se reusa entre jugadores en el mismo cuadro, no sirve para separarlos), saca las pasadas repetidas de
//     la misma malla y da una tabla a cada personaje; pes_body_cs.hlsl deforma todos los vertices y llena las tablas.
//   - Cabecera de cada tabla: [0] cuadro (el que vera el simulador: fila 257), [1] partes, [2] vertices, [3] 1 = valida, [4..6]
//     origen del personaje en el mundo (el simulador lo compara con el de cada prenda), [7] primer registro.
//   - Parametros (fila 256): x prendido, y separacion de la tela con la piel (mm), z cuantos cm mas adentro puede ir la tela que
//     el cuerpo que adivina el mod, w empuje maximo por sub-paso (mm). Fila 257.x = numero de cuadro (lo escribe la DLL).
//   - El simulador corre antes que los dibujos del cuadro, asi que usa la piel del cuadro anterior: la lleva al momento de cada
//     sub-paso con los huesos de la propia prenda y le suma lo que la piel se movio por su cuenta (un brazo que se acerca).
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
static const UINT kRing = 1u << 20;      // vertices deformados por cuadro (maximo)
static const UINT kStatic = 1u << 18;    // vertices de las mallas registradas
static const UINT kMeshMax = 12288;      // vertices por malla
static const UINT kSlots = 1024;         // lugares para las constantes de los dibujos (anillo: varios cuadros)
static const UINT kRecF4 = 208;          // float4 por lugar: [0..3] m_view (b2 filas 8..11), [4..12] b5 (filas 0..8), [16..207] b6
static const UINT kMaxRec = 512;         // dibujos por cuadro (tienen que coincidir con pes_body_assign_cs.hlsl)
static const UINT kMaxGroups = kRing / 64 + kMaxRec;
static const UINT kDiag = 64;
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
struct Rec { UINT slot, sBase, nv, mesh; };   // un dibujo de la piel: lugar de sus constantes y su malla

static std::mutex s_mx;
static std::unordered_map<uint64_t, BodyMesh*> s_meshes;
static std::vector<BodyMesh*> s_pending;
static std::unordered_map<ID3D11DeviceContext*, std::vector<Rec>> s_pendCtx;   // contexto diferido -> hasta FinishCommandList
static std::unordered_map<ID3D11CommandList*, std::vector<Rec>> s_pendList;    // lista de comandos -> hasta ExecuteCommandList
static std::vector<Rec> s_execRecs;                                             // ejecutados: van al lote del proximo Present
static UINT s_slotNext = 0;
static int s_nextId = 1;
static std::vector<std::pair<UINT, UINT>> s_free;              // huecos de la copia propia (inicio, cantidad)
static std::atomic<int> s_simSeen{-100000};
static std::atomic<int> s_skipWin{0};
static int s_logRecs = 0, s_logFrames = 0, s_logVerts = 0, s_logBatches = 0, s_dropRec = 0, s_dropRing = 0, s_dropList = 0;

// recursos (dispositivo del juego)
static ID3D11Device* s_dev = nullptr;
static ID3D11ComputeShader *s_csSkin = nullptr, *s_csAssign = nullptr;
static ID3D11Buffer *s_static = nullptr, *s_ringV = nullptr, *s_ringN = nullptr, *s_tab = nullptr, *s_cb = nullptr;
static ID3D11Buffer *s_recData = nullptr, *s_desc = nullptr, *s_grp = nullptr, *s_recTab = nullptr, *s_diag = nullptr, *s_diagStage = nullptr;
static ID3D11ShaderResourceView *s_staticSRV = nullptr, *s_ringVSRV = nullptr, *s_ringNSRV = nullptr, *s_tabSRV = nullptr;
static ID3D11ShaderResourceView *s_recSRV = nullptr, *s_descSRV = nullptr, *s_grpSRV = nullptr;
static ID3D11UnorderedAccessView *s_ringVUAV = nullptr, *s_ringNUAV = nullptr, *s_tabUAV = nullptr, *s_recTabUAV = nullptr, *s_diagUAV = nullptr;
static int s_diagState = 0, s_diagFrame = 0;
static std::atomic<bool> s_ready{false};
static bool s_failed = false;

void BodySimSeen() { s_simSeen.store(PresentNo(), std::memory_order_relaxed); }

// (25/10 e) diagnostico: las instancias que ve el simulador de la ropa (t18, Inst de cloth_sim.hlsl: 160 bytes, matriz de mundo
// en + 48). Cada 20 s se copian las de hasta 8 despachos del cuadro para comparar su origen con el de los personajes del lote.
std::atomic<bool> g_bodyDiagArmed{false};
static const UINT kInstBytes = 160, kInstMax = 16;
static ID3D11Buffer* s_instStage[8] = {};
static UINT s_instGroups[8] = {}, s_instStride[8] = {};
static int s_instN = 0, s_instFrame = 0, s_instState = 0;
void BodyNoteClothDispatch(ID3D11DeviceContext* c, UINT groups) {
    if (s_instN >= 8 || !s_dev || c->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
    ID3D11ShaderResourceView* v = nullptr; c->CSGetShaderResources(18, 1, &v);
    if (!v) return;
    ID3D11Resource* r = nullptr; v->GetResource(&r); v->Release();
    if (!r) return;
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN; r->GetType(&dim);
    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) {
        ID3D11Buffer* b = (ID3D11Buffer*)r; D3D11_BUFFER_DESC d; b->GetDesc(&d);
        const int k = s_instN;
        if (!s_instStage[k]) {
            D3D11_BUFFER_DESC sd{}; sd.ByteWidth = kInstBytes * kInstMax; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(s_dev->CreateBuffer(&sd, nullptr, &s_instStage[k]))) s_instStage[k] = nullptr;
        }
        const UINT bytes = (std::min)(d.ByteWidth, kInstBytes * kInstMax) / kInstBytes * kInstBytes;
        if (s_instStage[k] && bytes) {
            D3D11_BOX bx = { 0, 0, 0, bytes, 1, 1 };
            c->CopySubresourceRegion(s_instStage[k], 0, 0, 0, 0, b, 0, &bx);
            s_instGroups[k] = (std::min)(groups, bytes / kInstBytes); s_instStride[k] = d.StructureByteStride;
            s_instN++;
        }
    }
    r->Release();
}
static void ReadInstances(ID3D11DeviceContext* imm) {
    char l[1600]; int k = snprintf(l, sizeof l, "ropa (lo que ve el simulador): %d despachos", s_instN);
    for (int i = 0; i < s_instN && k < (int)sizeof l - 200; i++) {
        D3D11_MAPPED_SUBRESOURCE mp;
        if (!s_instStage[i] || FAILED(imm->Map(s_instStage[i], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mp))) { k += snprintf(l + k, sizeof l - k, " | #%d sin leer", i); continue; }
        k += snprintf(l + k, sizeof l - k, " | #%d (%u prendas, paso %u):", i, s_instGroups[i], s_instStride[i]);
        for (UINT j = 0; j < s_instGroups[i] && j < 4 && k < (int)sizeof l - 120; j++) {
            const uint8_t* p = (const uint8_t*)mp.pData + j * kInstBytes;
            uint32_t flags; float w[12]; memcpy(&flags, p + 8, 4); memcpy(w, p + 48, 48);
            k += snprintf(l + k, sizeof l - k, " [banderas 0x%x origen (%.2f, %.2f, %.2f) escala %.3f]", flags, w[3], w[7], w[11], sqrtf(w[0] * w[0] + w[4] * w[4] + w[8] * w[8]));
        }
        imm->Unmap(s_instStage[i], 0);
    }
    Log("%s", l);
}

static ID3D11ComputeShader* CompileOne(const wchar_t* file) {
    ID3D11ComputeShader* cs = nullptr;
    if (ID3DBlob* b = CompileNamedShader(file, "cs_5_0")) { s_dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &cs); b->Release(); }
    if (!cs) Log("cuerpo: no se pudo compilar %ls (la ropa no choca con el cuerpo real)", file);
    return cs;
}
static void CompileCS() {
    if (!s_dev) return;
    // los anteriores se dejan vivos (el lote del Present podria estar usandolos en otro hilo: no, pero no cuesta nada)
    if (ID3D11ComputeShader* a = CompileOne(L"pes_body_assign_cs.hlsl")) s_csAssign = a;
    if (ID3D11ComputeShader* b = CompileOne(L"pes_body_cs.hlsl")) s_csSkin = b;
}
void BodyReloadShaders() { std::lock_guard<std::mutex> lk(s_mx); if (s_dev) CompileCS(); }

static bool MakeBuf(UINT bytes, UINT stride, UINT bind, UINT misc, ID3D11Buffer** out, D3D11_USAGE usage = D3D11_USAGE_DEFAULT) {
    D3D11_BUFFER_DESC d{}; d.ByteWidth = bytes; d.Usage = usage; d.BindFlags = bind; d.MiscFlags = misc; d.StructureByteStride = stride;
    if (usage == D3D11_USAGE_DYNAMIC) d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(s_dev->CreateBuffer(&d, nullptr, out)) && *out;
}
static bool TypedSRV(ID3D11Buffer* b, DXGI_FORMAT f, UINT n, ID3D11ShaderResourceView** out) {
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Format = f; sd.Buffer.FirstElement = 0; sd.Buffer.NumElements = n;
    return SUCCEEDED(s_dev->CreateShaderResourceView(b, &sd, out)) && *out;
}
static bool TypedUAV(ID3D11Buffer* b, DXGI_FORMAT f, UINT n, ID3D11UnorderedAccessView** out) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Format = f; ud.Buffer.FirstElement = 0; ud.Buffer.NumElements = n;
    return SUCCEEDED(s_dev->CreateUnorderedAccessView(b, &ud, out)) && *out;
}
static bool CreateResources() {
    s_dev = g_dev.dev;
    const UINT SU = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bool ok = MakeBuf(kStatic * 32, 16, D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, &s_static) &&
              MakeBuf(kRing * 16, 16, SU, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, &s_ringV) &&
              MakeBuf(kRing * 4, 4, SU, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, &s_ringN) &&
              MakeBuf(kTables * kTabStride * 4, 0, SU, 0, &s_tab) &&
              MakeBuf(kSlots * kRecF4 * 16, 0, D3D11_BIND_SHADER_RESOURCE, 0, &s_recData) &&
              MakeBuf(kMaxRec * 32, 0, D3D11_BIND_SHADER_RESOURCE, 0, &s_desc, D3D11_USAGE_DYNAMIC) &&
              MakeBuf(kMaxGroups * 8, 0, D3D11_BIND_SHADER_RESOURCE, 0, &s_grp, D3D11_USAGE_DYNAMIC) &&
              MakeBuf(kMaxRec * 4, 0, D3D11_BIND_UNORDERED_ACCESS, 0, &s_recTab) &&
              MakeBuf(kDiag * 4, 0, D3D11_BIND_UNORDERED_ACCESS, 0, &s_diag) &&
              MakeBuf(16, 0, D3D11_BIND_CONSTANT_BUFFER, 0, &s_cb, D3D11_USAGE_DYNAMIC);
    if (ok) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.Buffer.FirstElement = 0; sd.Buffer.NumElements = kStatic * 2; ok = ok && SUCCEEDED(s_dev->CreateShaderResourceView(s_static, &sd, &s_staticSRV));
        sd.Buffer.NumElements = kRing; ok = ok && SUCCEEDED(s_dev->CreateShaderResourceView(s_ringV, &sd, &s_ringVSRV));
        ok = ok && SUCCEEDED(s_dev->CreateShaderResourceView(s_ringN, &sd, &s_ringNSRV));
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Format = DXGI_FORMAT_UNKNOWN;
        ud.Buffer.FirstElement = 0; ud.Buffer.NumElements = kRing;
        ok = ok && SUCCEEDED(s_dev->CreateUnorderedAccessView(s_ringV, &ud, &s_ringVUAV)) && SUCCEEDED(s_dev->CreateUnorderedAccessView(s_ringN, &ud, &s_ringNUAV));
        ok = ok && TypedSRV(s_tab, DXGI_FORMAT_R32_UINT, kTables * kTabStride, &s_tabSRV) && TypedUAV(s_tab, DXGI_FORMAT_R32_UINT, kTables * kTabStride, &s_tabUAV) &&
             TypedSRV(s_recData, DXGI_FORMAT_R32G32B32A32_FLOAT, kSlots * kRecF4, &s_recSRV) &&
             TypedSRV(s_desc, DXGI_FORMAT_R32G32B32A32_UINT, kMaxRec * 2, &s_descSRV) &&
             TypedSRV(s_grp, DXGI_FORMAT_R32G32_UINT, kMaxGroups, &s_grpSRV) &&
             TypedUAV(s_recTab, DXGI_FORMAT_R32_UINT, kMaxRec, &s_recTabUAV) &&
             TypedUAV(s_diag, DXGI_FORMAT_R32_UINT, kDiag, &s_diagUAV);
    }
    if (ok) {
        D3D11_BUFFER_DESC sd{}; sd.ByteWidth = kDiag * 4; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(s_dev->CreateBuffer(&sd, nullptr, &s_diagStage))) s_diagStage = nullptr;
    }
    if (!ok) { Log("cuerpo: no se pudieron crear los buffers (la ropa no choca con el cuerpo real)"); s_failed = true; return false; }
    // la tabla arranca en cero: ninguna cabecera valida hasta que se escriba
    const UINT z[4] = { 0, 0, 0, 0 };
    g_dev.imm->ClearUnorderedAccessViewUint(s_tabUAV, z);
    g_dev.imm->ClearUnorderedAccessViewUint(s_diagUAV, z);
    s_free.assign(1, { 0u, kStatic });
    CompileCS();
    Log("cuerpo: listo (%u KB de tablas, %u vertices por cuadro, copia de %u vertices, %u lugares de constantes)%s", kTables * kTabStride * 4 / 1024, kRing, kStatic, kSlots,
        (s_csSkin && s_csAssign) ? "" : " - SIN los CS");
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
// ventana enlazada de un cbuffer (ID3D11DeviceContext1: el juego enlaza ventanas de buffers grandes): desplazamiento y largo
static bool CBWindow(const CBSlot& s, UINT need, UINT* off) {
    if (!s.b) return false;
    D3D11_BUFFER_DESC d; s.b->GetDesc(&d);
    UINT o = s.f * 16, len = d.ByteWidth;
    if (s.n && (o > 0 || s.n * 16 < d.ByteWidth)) len = o < d.ByteWidth ? (std::min)(s.n * 16, d.ByteWidth - o) : 0;
    if (len < need || o + need > d.ByteWidth) return false;
    *off = o; return true;
}
static void CopyPart(ID3D11DeviceContext* c, ID3D11Buffer* src, UINT srcOff, UINT bytes, UINT dstOff) {
    D3D11_BOX bx = { srcOff, 0, 0, srcOff + bytes, 1, 1 };
    c->CopySubresourceRegion(s_recData, 0, dstOff, 0, 0, src, 0, &bx);
}

void BodyOnDraw(ID3D11DeviceContext* c, UINT count, UINT start, INT base) {
    if (!s_ready.load(std::memory_order_acquire) || count < 3) return;
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
    key = Fnv(key, (uint64_t)il); for (int i = 0; i < 4; i++) { key = Fnv(key, (uint64_t)vb[i]); key = Fnv(key, off[i]); key = Fnv(key, stride[i]); }
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
    // constantes del dibujo: m_view (b2 filas 8..11), bloque por objeto (b5 filas 0..8) y huesos (b6: actuales + anteriores)
    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) || !c1) return;
    CBSlot g2, g5, g6;
    c1->VSGetConstantBuffers1(2, 1, &g2.b, &g2.f, &g2.n); c1->VSGetConstantBuffers1(5, 1, &g5.b, &g5.f, &g5.n); c1->VSGetConstantBuffers1(6, 1, &g6.b, &g6.f, &g6.n);
    c1->Release();
    UINT o2 = 0, o5 = 0, o6 = 0;
    if (CBWindow(g2, 192, &o2) && CBWindow(g5, 144, &o5) && CBWindow(g6, 3072, &o6)) {
        UINT slot;
        { std::lock_guard<std::mutex> lk(s_mx); slot = s_slotNext++ % kSlots; }
        const UINT dst = slot * kRecF4 * 16;
        CopyPart(c, g2.b, o2 + 128, 64, dst);
        CopyPart(c, g5.b, o5, 144, dst + 64);
        CopyPart(c, g6.b, o6, 3072, dst + 256);
        const Rec r{ slot, m->sBase, m->nv, (UINT)m->id };
        std::lock_guard<std::mutex> lk(s_mx);
        std::vector<Rec>& list = (c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED) ? s_pendCtx[c] : s_execRecs;
        if (list.size() < 4096) list.push_back(r); else s_dropRec++;
    } else s_skipWin++;
    for (CBSlot* s : { &g2, &g5, &g6 }) if (s->b) s->b->Release();
}
// (25/10 e) los registros de un contexto diferido quedan en su lista de comandos; cuentan cuando la lista se ejecuta
void BodyOnFinish(ID3D11DeviceContext* c, ID3D11CommandList* l) {
    if (!s_ready.load(std::memory_order_acquire) || !l) return;
    std::lock_guard<std::mutex> lk(s_mx);
    auto it = s_pendCtx.find(c);
    if (it == s_pendCtx.end() || it->second.empty()) { s_pendList.erase(l); return; }
    if (s_pendList.size() >= 256) { s_pendList.clear(); s_dropList++; }   // listas que nunca se ejecutaron
    s_pendList[l] = std::move(it->second);
    it->second.clear();
}
void BodyOnExecute(ID3D11DeviceContext* c, ID3D11CommandList* l) {
    if (!s_ready.load(std::memory_order_acquire) || !l) return;
    std::lock_guard<std::mutex> lk(s_mx);
    auto it = s_pendList.find(l);
    if (it == s_pendList.end()) return;
    // una lista ejecutada dentro de otro contexto diferido pasa a ese contexto (cuenta cuando se ejecute la de afuera)
    std::vector<Rec>& dst = (c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED) ? s_pendCtx[c] : s_execRecs;
    dst.insert(dst.end(), it->second.begin(), it->second.end());
    s_pendList.erase(it);
}

// ---------------------------------------------------------------- registro de mallas (Present, contexto inmediato)
static ID3D11Buffer* Staging(UINT bytes) {
    D3D11_BUFFER_DESC d{}; d.ByteWidth = (bytes + 15) & ~15u; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* b = nullptr; return SUCCEEDED(s_dev->CreateBuffer(&d, nullptr, &b)) ? b : nullptr;
}
static void Reject(BodyMesh* m, const char* why) {
    m->state = 4; m->why = why; ReleaseMesh(m);
    static int n = 0; if (n++ < 40) Log("cuerpo: malla %d descartada (%u indices desde %u, base %d): %s", m->id, m->count, m->start, m->base, why);
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
            if (!m->stride[s] || b > d.ByteWidth) {
                char w[240]; snprintf(w, sizeof w, "vertices fuera del buffer (ranura %d: paso %u, desplazamiento %u, vertices %u..%u, el buffer tiene %u bytes = %u vertices; elementos pos %d normal %d huesos %d pesos %d)",
                                      s, m->stride[s], m->off[s], lo, hi, d.ByteWidth, m->stride[s] ? (d.ByteWidth - (std::min)(d.ByteWidth, m->off[s])) / m->stride[s] : 0,
                                      m->pos.slot, m->nrm.slot, m->idx.slot, m->wgt.slot);
                Reject(m, w); return;
            }
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
        // (25/10 e) las manos (al costado, lejos del eje: x 0.57..0.73 en el reposo de TorsoBody) tampoco: la manga no llega
        if (bmin[0] > 0.45f || bmax[0] < -0.45f) { Reject(m, "mano (la manga no llega)"); return; }
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

// ---------------------------------------------------------------- lote del cuadro (Present, contexto inmediato)
// todo lo que se ejecuto en el cuadro: tablas limpias, personajes (assign) y piel (skin) en dos despachos seguidos
static bool RunBatch(ID3D11DeviceContext* imm, std::vector<Rec>& recs, int frame) {
    D3D11_MAPPED_SUBRESOURCE mp;
    if (FAILED(imm->Map(s_desc, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) return false;
    uint32_t* d = (uint32_t*)mp.pData;
    static std::vector<uint32_t> grp; grp.clear();
    UINT n = 0, ring = 0;
    for (const Rec& r : recs) {
        if (n >= kMaxRec) { s_dropRec++; continue; }
        if (ring + r.nv > kRing) { s_dropRing++; continue; }
        uint32_t* e = d + 8 * n;
        e[0] = r.slot; e[1] = r.sBase; e[2] = r.nv; e[3] = ring; e[4] = r.mesh; e[5] = e[6] = e[7] = 0;
        for (UINT v0 = 0; v0 < r.nv; v0 += 64) { grp.push_back(n); grp.push_back(v0); }
        ring += r.nv; n++;
    }
    imm->Unmap(s_desc, 0);
    const UINT ng = (UINT)(grp.size() / 2);
    if (!n || !ng || ng > kMaxGroups) return false;
    if (FAILED(imm->Map(s_grp, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) return false;
    memcpy(mp.pData, grp.data(), grp.size() * 4); imm->Unmap(s_grp, 0);
    if (FAILED(imm->Map(s_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) return false;
    const UINT prm[4] = { 0u, n, (UINT)frame, ng };
    memcpy(mp.pData, prm, sizeof(prm)); imm->Unmap(s_cb, 0);
    // estado del CS del contexto inmediato (se devuelve igual)
    ID3D11ComputeShader* pcs = nullptr; imm->CSGetShader(&pcs, nullptr, nullptr);
    ID3D11Buffer* pcb = nullptr; imm->CSGetConstantBuffers(0, 1, &pcb);
    ID3D11ShaderResourceView* psrv[4] = {}; imm->CSGetShaderResources(0, 4, psrv);
    ID3D11UnorderedAccessView* puav[5] = {}; imm->CSGetUnorderedAccessViews(0, 5, puav);
    const UINT z[4] = { 0, 0, 0, 0 };
    imm->ClearUnorderedAccessViewUint(s_tabUAV, z);
    { ID3D11Buffer* b = s_cb; imm->CSSetConstantBuffers(0, 1, &b); }
    // personajes
    { ID3D11ShaderResourceView* v[4] = { nullptr, s_descSRV, s_recSRV, nullptr }; imm->CSSetShaderResources(0, 4, v); }
    { ID3D11UnorderedAccessView* u[5] = { nullptr, nullptr, s_tabUAV, s_recTabUAV, s_diagUAV }; imm->CSSetUnorderedAccessViews(0, 5, u, nullptr); }
    imm->CSSetShader(s_csAssign, nullptr, 0);
    imm->Dispatch(1, 1, 1);
    // piel
    { ID3D11UnorderedAccessView* u[5] = { s_ringVUAV, s_ringNUAV, s_tabUAV, s_recTabUAV, nullptr }; imm->CSSetUnorderedAccessViews(0, 5, u, nullptr); }
    { ID3D11ShaderResourceView* v[4] = { s_staticSRV, s_descSRV, s_recSRV, s_grpSRV }; imm->CSSetShaderResources(0, 4, v); }
    imm->CSSetShader(s_csSkin, nullptr, 0);
    imm->Dispatch(ng, 1, 1);
    imm->CSSetUnorderedAccessViews(0, 5, puav, nullptr);
    imm->CSSetShaderResources(0, 4, psrv);
    imm->CSSetConstantBuffers(0, 1, &pcb);
    imm->CSSetShader(pcs, nullptr, 0);
    if (pcs) pcs->Release(); if (pcb) pcb->Release();
    for (auto* v : psrv) if (v) v->Release(); for (auto* u : puav) if (u) u->Release();
    s_logRecs += (int)n; s_logVerts += (int)ring; s_logBatches++;
    return true;
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
    const int frame = PresentNo();   // el numero que la DLL sube en este Present (fila 257): el que vera el simulador
    std::vector<BodyMesh*> dead;
    std::vector<Rec> recs;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        recs.swap(s_execRecs);
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
    s_logFrames++;
    bool ran = false;
    if (!recs.empty() && s_csSkin && s_csAssign && g_cfg.live[256][0] > 0.5f) ran = RunBatch(g_dev.imm, recs, frame);
    // resumen del lote (personajes encontrados y donde estan): copia cada 20 s, lectura 2 cuadros despues
    if (s_diagState == 1 && s_diagStage && frame >= s_diagFrame + 2) {
        D3D11_MAPPED_SUBRESOURCE mp;
        HRESULT hr = g_dev.imm->Map(s_diagStage, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mp);
        if (SUCCEEDED(hr)) {
            uint32_t q[kDiag]; memcpy(q, mp.pData, sizeof(q)); g_dev.imm->Unmap(s_diagStage, 0);
            s_diagState = 0;
            if (q[1] > 0) {
                char l[900]; int k = snprintf(l, sizeof l, "cuerpo (lote del cuadro %u): %u dibujos de piel -> %u personajes, %u repetidos (otra pasada), %u sin tabla", q[0], q[1], q[2], q[3], q[4]);
                for (UINT c = 0; c < 8 && c < q[2] && k < (int)sizeof l - 80; c++) {
                    float w[3]; memcpy(w, &q[16 + 4 * c], 12);
                    k += snprintf(l + k, sizeof l - k, "%s(%.2f, %.2f, %.2f) %u partes", c ? "; " : "; personajes en ", w[0], w[1], w[2], q[19 + 4 * c]);
                }
                Log("%s", l);
            }
        } else if (hr != DXGI_ERROR_WAS_STILL_DRAWING) s_diagState = 0;
    }
    // instancias del simulador copiadas en el cuadro anterior: se dejan de copiar y se leen 2 cuadros despues
    if (s_instState == 1 && frame >= s_instFrame + 1) { g_bodyDiagArmed = false; s_instState = 2; }
    if (s_instState == 2 && frame >= s_instFrame + 3) { if (s_instN) ReadInstances(g_dev.imm); s_instState = 0; }
    static double nextLog = 0; const double t = Now();
    if (t > nextLog && (ran || s_logBatches > 0 || frame - s_simSeen.load() < 300)) {
        nextLog = t + 20.0;
        if (s_instState == 0 && frame - s_simSeen.load() < 300) { s_instN = 0; s_instFrame = frame; s_instState = 1; g_bodyDiagArmed = true; }
        if (ran && s_diagStage && s_diagState == 0) { g_dev.imm->CopyResource(s_diagStage, s_diag); s_diagState = 1; s_diagFrame = frame; }
        int ready = 0, rej = 0, pend = 0;
        { std::lock_guard<std::mutex> lk(s_mx); for (auto& kv : s_meshes) { if (kv.second->state == 3) ready++; else if (kv.second->state == 4) rej++; else pend++; } }
        Log("cuerpo: %d mallas de piel listas (%d descartadas, %d en camino); por cuadro %.1f dibujos de piel, %.0f vertices (%d lotes en %d cuadros)%s%s%s%s",
            ready, rej, pend, s_logBatches ? s_logRecs / (float)s_logBatches : 0.0f, s_logBatches ? s_logVerts / (float)s_logBatches : 0.0f, s_logBatches, s_logFrames,
            s_dropRec ? " (mas de 512 dibujos: se saltearon)" : "", s_dropRing ? " (mas de 1 M de vertices: se saltearon)" : "",
            s_skipWin.load() ? " (dibujos con constantes incompletas)" : "", s_dropList ? " (listas de comandos sin ejecutar)" : "");
        s_logRecs = s_logFrames = s_logVerts = s_logBatches = 0; s_dropRec = s_dropRing = s_dropList = 0; s_skipWin = 0;
    }
}

} // namespace fx
