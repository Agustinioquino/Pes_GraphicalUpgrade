// PES21 FX - LOD de caras (20/10). El motor elige en la CPU que malla de cada jugador dibuja (LOD) y a cuales les
// calcula la animacion facial; los "mods de LOD" que parchean bytes del exe no cambian eso. Desde la DLL se puede:
//   1) DIAGNOSTICO (x118): anotar, por cara (identificada por su textura), con cuantos indices se dibuja cada vez,
//      con que VS/PS, que cbuffers lleva el VS (paleta de huesos) y a que distancia de la camara, y volcar un resumen
//      al log cada 20 s. Sirve para confirmar como cambia el motor de malla y si la paleta de huesos es la misma.
//   2) LOD0 FORZADO (y118): la DLL se aprende, por cara, el estado de entrada del dibujo con MAS indices que vio
//      (buffers de vertices e indices, layout, topologia, VS, cuentas) y, cuando esa misma cara vuelve a dibujarse con
//      una malla reducida, le cambia al dibujo los buffers y las cuentas por los de maximo detalle. Solo si el VS es el
//      mismo (o el layout es el mismo objeto): asi la paleta de huesos y el formato de vertice coinciden seguro. La
//      sustitucion tambien se aplica por buffer de vertices (sombras / prepaso de profundidad de la misma malla).
//   No puede inventar expresiones: la animacion facial de un jugador lejano no existe en la GPU (la CPU no la calcula).
#include "core.h"
#include <d3d11_1.h>
#include <unordered_map>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace fx {

void LodIA::Release() {
    for (auto*& b : vb) { if (b) b->Release(); b = nullptr; }
    if (ib) ib->Release(); ib = nullptr;
    if (il) il->Release(); il = nullptr;
    if (vs) vs->Release(); vs = nullptr;
    count = 0;
}
void LodIA::AddRefAll() const { for (auto* b : vb) if (b) b->AddRef(); if (ib) ib->AddRef(); if (il) il->AddRef(); if (vs) vs->AddRef(); }

struct LodVar { UINT count = 0, start = 0; INT base = 0; uint64_t vs = 0, ps = 0; void* vb0 = nullptr; void* ib = nullptr; UINT cb[8] = {}; int draws = 0; int nd = 0; float dmin = 1e9f, dmax = 0; int lastFrame = -1; };
struct LodFace {
    std::string id; ID3D11Resource* key = nullptr; ID3D11Resource* t0 = nullptr;
    std::vector<LodVar> var; LodIA best; int subs = 0, incompat = 0; uint64_t incompatVS = 0;
    float lastDist = -1;      // ultima distancia leida (2 cuadros de atraso)
    bool parts = false;       // dos cuentas distintas con el mismo VS/PS en el mismo cuadro = partes de la malla, no LODs: no sustituir
};
// clave de sustitucion por malla reducida: (VB0, IB, cuenta, inicio, base) -> cara
struct SubKey { void* vb0; void* ib; UINT count, start; INT base; bool operator==(const SubKey& o) const { return vb0 == o.vb0 && ib == o.ib && count == o.count && start == o.start && base == o.base; } };
struct SubKeyHash { size_t operator()(const SubKey& k) const { return std::hash<void*>()(k.vb0) ^ (std::hash<void*>()(k.ib) << 1) ^ (size_t)k.count * 1315423911u ^ (size_t)k.start * 2654435761u ^ (size_t)(unsigned)k.base * 97u; } };

static std::mutex s_mx;
static std::unordered_map<ID3D11Resource*, LodFace*> s_faces;        // por textura clave (t1 de la cara)
static std::unordered_map<ID3D11Resource*, LodFace*> s_byTex;        // t0 y t1 -> cara (para dibujos con otros PS)
static std::unordered_map<SubKey, LodFace*, SubKeyHash> s_sub;      // malla reducida -> cara (sustitucion por buffers)
static std::unordered_map<void*, LodFace*> s_byVB;                  // VB0 de cualquier variante -> cara (prepaso / sombras sin texturas)
static std::vector<LodFace*> s_old; static int s_oldFrame = -1;      // caras del partido anterior (se liberan 90 cuadros despues)
static int s_matchSeen = -1; static int s_frame = 0;
std::atomic<int> g_lodSubs{0}, g_lodFaces{0}; std::atomic<bool> g_lodAny{false};
static double s_nextLog = 0;

// distancia: 16 bytes (traslacion de m_world, bytes 112..127 del b5 del VS) por dibujo de cara a un staging en anillo
static const int kNotes = 64, kRing = 3;
static ID3D11Buffer* s_stage[kRing] = {}; static int s_stageW = 0; static int s_stageN[kRing] = { -1, -1, -1 };
struct Note { LodFace* f; int var; };
static Note s_notes[kRing][kNotes]; static std::atomic<int> s_count{0};

static void EnsureStage() {
    if (!g_dev.dev) return;
    for (int i = 0; i < kRing; i++) if (!s_stage[i]) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kNotes * 16; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        g_dev.dev->CreateBuffer(&d, nullptr, &s_stage[i]);
    }
}
static bool DiagOn() { return g_cfg.live[118][0] > 0.5f; }
static bool ForceOn() { return g_cfg.live[118][1] > 0.5f; }

static void CaptureIA(ID3D11DeviceContext* c, LodIA& L, uint64_t vsHash, uint64_t psHash, UINT count, UINT start, INT base) {
    L.Release();
    c->IAGetVertexBuffers(0, 4, L.vb, L.stride, L.offset);
    c->IAGetIndexBuffer(&L.ib, &L.ibFmt, &L.ibOff);
    c->IAGetInputLayout(&L.il);
    c->IAGetPrimitiveTopology(&L.topo);
    c->VSGetShader(&L.vs, nullptr, nullptr);
    L.vsHash = vsHash; L.psHash = psHash; L.count = count; L.start = start; L.base = base;
    ID3D11Buffer* cbs[8] = {}; c->VSGetConstantBuffers(0, 8, cbs);
    for (int i = 0; i < 8; i++) { L.cb[i] = 0; if (cbs[i]) { D3D11_BUFFER_DESC d; cbs[i]->GetDesc(&d); L.cb[i] = d.ByteWidth; cbs[i]->Release(); } }
}

static void NoteDistance(ID3D11DeviceContext* c, LodFace* f, int var) {
    int w = s_stageW; if (!s_stage[w]) return;
    int i = s_count.fetch_add(1); if (i >= kNotes) return;
    ID3D11Buffer* cb = nullptr; UINT first = 0, num = 0; ID3D11DeviceContext1* c1 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) { c1->VSGetConstantBuffers1(5, 1, &cb, &first, &num); c1->Release(); }
    else c->VSGetConstantBuffers(5, 1, &cb);
    if (!cb) { s_notes[w][i] = { nullptr, 0 }; return; }
    D3D11_BUFFER_DESC d; cb->GetDesc(&d); UINT off = first * 16 + 112;
    if (d.ByteWidth >= off + 16) { D3D11_BOX bx = { off, 0, 0, off + 16, 1, 1 }; c->CopySubresourceRegion(s_stage[w], 0, (UINT)i * 16, 0, 0, cb, 0, &bx); s_notes[w][i] = { f, var }; }
    else s_notes[w][i] = { nullptr, 0 };
    cb->Release();
}

static LodVar& Variant(LodFace* f, UINT count, UINT start, INT base, uint64_t vs, uint64_t ps, void* vb0, void* ib, int* idx) {
    int found = -1;
    for (size_t i = 0; i < f->var.size(); i++) {
        LodVar& v = f->var[i];
        if (v.count == count && v.vs == vs && v.ps == ps && v.vb0 == vb0 && v.start == start && v.base == base) { found = (int)i; continue; }
        // otra cuenta con el mismo VS/PS dibujada en este mismo cuadro: son partes de una malla, no niveles de detalle
        if (v.vs == vs && v.ps == ps && v.count != count && v.lastFrame == s_frame && !f->parts) { f->parts = true; Log("lod caras: cara %s se dibuja en partes (%u y %u indices en el mismo cuadro, VS %016llx): no se sustituye", f->id.empty() ? "?" : f->id.substr(0, 8).c_str(), v.count, count, (unsigned long long)vs); }
    }
    if (found >= 0) { f->var[found].lastFrame = s_frame; *idx = found; return f->var[found]; }
    LodVar v; v.count = count; v.start = start; v.base = base; v.vs = vs; v.ps = ps; v.vb0 = vb0; v.ib = ib; v.lastFrame = s_frame;
    if (f->var.size() < 12) { f->var.push_back(v); if (vb0 && s_byVB.size() < 1024) s_byVB[vb0] = f; }
    *idx = (int)f->var.size() - 1; return f->var.back();
}

// dibujo de una cara con la regla facegen (PS de cara). Devuelve true y llena 'out' si hay que sustituir la geometria.
bool LodNoteFace(ID3D11DeviceContext* c, ID3D11Resource* key, ID3D11Resource* t0, const std::string& id, uint64_t vsHash, uint64_t psHash, UINT count, UINT start, INT base, LodIA* out) {
    if (!key || (!DiagOn() && !ForceOn()) || count < 100) return false;
    EnsureStage();
    ID3D11Buffer* vb0 = nullptr; UINT st = 0, of = 0; c->IAGetVertexBuffers(0, 1, &vb0, &st, &of); if (vb0) vb0->Release();
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT fmt; UINT ioff = 0; c->IAGetIndexBuffer(&ib, &fmt, &ioff); if (ib) ib->Release();
    bool sub = false;
    LodFace* f = nullptr; int vi = 0;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        auto it = s_faces.find(key);
        if (it == s_faces.end()) {
            if (s_faces.size() >= 64) return false;
            f = new LodFace(); f->key = key; f->t0 = t0; s_faces[key] = f; s_byTex[key] = f; if (t0) s_byTex[t0] = f; g_lodFaces = (int)s_faces.size();
        } else f = it->second;
        if (f->id.empty() && !id.empty()) f->id = id;
        LodVar& v = Variant(f, count, start, base, vsHash, psHash, vb0, ib, &vi); v.draws++;
        if (v.draws == 1) { ID3D11Buffer* cbs[8] = {}; c->VSGetConstantBuffers(0, 8, cbs); for (int i = 0; i < 8; i++) if (cbs[i]) { D3D11_BUFFER_DESC d; cbs[i]->GetDesc(&d); v.cb[i] = d.ByteWidth; cbs[i]->Release(); } }
        if (count > f->best.count) {
            CaptureIA(c, f->best, vsHash, psHash, count, start, base);
            if (DiagOn()) Log("lod caras: cara %s: mejor malla vista %u indices (VS %016llx, PS %016llx, VB %p + %u, IB %p)", f->id.empty() ? "?" : f->id.substr(0, 8).c_str(), count, (unsigned long long)vsHash, (unsigned long long)psHash, (void*)f->best.vb[0], base, (void*)f->best.ib);
        } else if (count < f->best.count && f->best.vb[0]) {
            // malla reducida: la registro para la sustitucion por buffers (sombras, prepaso) y decido si sustituir este dibujo
            SubKey k{ vb0, ib, count, start, base }; if (s_sub.size() < 512) s_sub[k] = f;
            if (ForceOn()) {
                ID3D11InputLayout* il = nullptr; c->IAGetInputLayout(&il); if (il) il->Release();
                bool sameVS = f->best.vsHash == vsHash, sameIL = il == f->best.il;
                float maxD = g_cfg.live[118][2];
                bool okCb = true; for (int i = 0; i < 8; i++) if (f->best.cb[i] != v.cb[i]) okCb = false;
                bool okDist = maxD < 0.5f || f->lastDist < 0 || f->lastDist <= maxD;
                if (f->parts || !okDist) { /* partes de malla o mas lejos que el limite: se deja como esta */ }
                else if ((sameVS || (g_cfg.live[118][3] > 0.5f && sameIL)) && okCb) {
                    *out = f->best; out->AddRefAll(); sub = true; f->subs++; g_lodSubs++;
                } else {
                    f->incompat++; if (f->incompatVS != vsHash) { f->incompatVS = vsHash; Log("lod caras: cara %s con %u indices no se puede sustituir por la de %u: %s (VS %016llx vs %016llx)", f->id.empty() ? "?" : f->id.substr(0, 8).c_str(), count, f->best.count, !okCb ? "cbuffers del VS de otro tamano" : (sameIL ? "VS distinto (modo permisivo w118 lo acepta por layout)" : "VS y layout distintos"), (unsigned long long)vsHash, (unsigned long long)f->best.vsHash); }
                }
            }
        }
    }
    NoteDistance(c, f, vi);
    return sub;
}

// cualquier DrawIndexed (solo con diagnostico o LOD0 forzado): dibujos de una cara conocida con otro PS (sombras,
// prepaso, otro nivel de detalle con otro shader) -> se anotan; con LOD0 forzado, si la malla reducida ya se vio con
// la regla de cara, se sustituye por buffers
bool LodNoteAnyDraw(ID3D11DeviceContext* c, uint64_t vsHash, uint64_t psHash, UINT count, UINT start, INT base, LodIA* out) {
    if ((!DiagOn() && !ForceOn()) || count < 100) return false;
    ID3D11Buffer* vb0 = nullptr; UINT st = 0, of = 0; c->IAGetVertexBuffers(0, 1, &vb0, &st, &of); if (vb0) vb0->Release();
    if (!vb0) return false;
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT fmt; UINT ioff = 0; c->IAGetIndexBuffer(&ib, &fmt, &ioff); if (ib) ib->Release();
    bool sub = false;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        if (ForceOn()) {
            auto it = s_sub.find(SubKey{ vb0, ib, count, start, base });
            if (it != s_sub.end() && it->second->best.vb[0]) {
                LodFace* f = it->second;
                ID3D11InputLayout* il = nullptr; c->IAGetInputLayout(&il); if (il) il->Release();
                float maxD = g_cfg.live[118][2]; bool okDist = maxD < 0.5f || f->lastDist < 0 || f->lastDist <= maxD;
                if (!f->parts && okDist && (il == f->best.il || vsHash == f->best.vsHash)) { *out = f->best; out->AddRefAll(); sub = true; f->subs++; g_lodSubs++; }
            }
        }
        if (DiagOn()) {
            // es una cara conocida (por sus texturas t0/t1 del PS)?
            ID3D11ShaderResourceView* in[2] = {}; c->PSGetShaderResources(0, 2, in);
            LodFace* f = nullptr;
            for (int i = 0; i < 2 && !f; i++) if (in[i]) { ID3D11Resource* r = nullptr; in[i]->GetResource(&r); if (r) { auto it = s_byTex.find(r); if (it != s_byTex.end()) f = it->second; r->Release(); } }
            for (auto* v : in) if (v) v->Release();
            if (!f) { auto it = s_byVB.find(vb0); if (it != s_byVB.end()) f = it->second; }   // o por su buffer de vertices (prepaso / sombras: sin texturas)
            if (f) { int vi = 0; LodVar& v = Variant(f, count, start, base, vsHash, psHash, vb0, ib, &vi); v.draws++; if (v.draws == 1) { ID3D11Buffer* cbs[8] = {}; c->VSGetConstantBuffers(0, 8, cbs); for (int i = 0; i < 8; i++) if (cbs[i]) { D3D11_BUFFER_DESC d; cbs[i]->GetDesc(&d); v.cb[i] = d.ByteWidth; cbs[i]->Release(); } } }
        }
    }
    return sub;
}

// aplica el estado de LOD0 (guardando el del juego en 'sv') / lo devuelve
void LodBind(ID3D11DeviceContext* c, const LodIA& L, LodIA& sv) {
    c->IAGetVertexBuffers(0, 4, sv.vb, sv.stride, sv.offset);
    c->IAGetIndexBuffer(&sv.ib, &sv.ibFmt, &sv.ibOff);
    c->IAGetInputLayout(&sv.il); c->IAGetPrimitiveTopology(&sv.topo); c->VSGetShader(&sv.vs, nullptr, nullptr);
    c->IASetVertexBuffers(0, 4, L.vb, L.stride, L.offset);
    c->IASetIndexBuffer(L.ib, L.ibFmt, L.ibOff);
    if (L.il != sv.il) c->IASetInputLayout(L.il);
    if (L.topo != sv.topo) c->IASetPrimitiveTopology(L.topo);
    if (L.vs != sv.vs && L.vs) c->VSSetShader(L.vs, nullptr, 0);
}
void LodRestore(ID3D11DeviceContext* c, LodIA& sv, const LodIA& L) {
    c->IASetVertexBuffers(0, 4, sv.vb, sv.stride, sv.offset);
    c->IASetIndexBuffer(sv.ib, sv.ibFmt, sv.ibOff);
    if (L.il != sv.il) c->IASetInputLayout(sv.il);
    if (L.topo != sv.topo) c->IASetPrimitiveTopology(sv.topo);
    if (L.vs != sv.vs && L.vs) c->VSSetShader(sv.vs, nullptr, 0);
    sv.Release();
}

static std::string Hex8(uint64_t h) { char b[24]; snprintf(b, sizeof b, "%08llx", (unsigned long long)(h >> 32)); return b; }

void LodPresent() {
    s_frame++;
    if (!g_dev.dev || !g_dev.imm) return;
    // partido nuevo: las texturas/buffers cambian de identidad -> olvidar (liberando lo retenido un rato despues)
    int mg = g_matchGen.load();
    if (mg != s_matchSeen) {
        std::lock_guard<std::mutex> lk(s_mx);
        s_matchSeen = mg;
        if (!s_faces.empty()) { for (auto& kv : s_faces) s_old.push_back(kv.second); s_faces.clear(); s_byTex.clear(); s_sub.clear(); s_byVB.clear(); s_oldFrame = s_frame; g_lodFaces = 0; }
    }
    if (!s_old.empty() && s_frame - s_oldFrame > 90) { std::lock_guard<std::mutex> lk(s_mx); for (auto* f : s_old) { f->best.Release(); delete f; } s_old.clear(); }
    g_lodAny = DiagOn() || ForceOn();
    if (!g_lodAny) { s_count = 0; return; }
    // anillo de distancias: este cuadro escribio en w; se lee el de hace 2 cuadros
    int w = s_stageW; int n = std::min(s_count.exchange(0), kNotes);
    s_stageN[w] = n; int rd = (w + 1) % kRing; int pending = s_stageN[rd]; s_stageN[rd] = -1; s_stageW = rd;
    if (pending > 0 && s_stage[rd] && g_eyeValid) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_dev.imm->Map(s_stage[rd], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
            const float* p = (const float*)m.pData;
            std::lock_guard<std::mutex> lk(s_mx);
            for (int i = 0; i < pending; i++) {
                Note& nt = s_notes[rd][i]; if (!nt.f) continue;
                bool alive = false; for (auto& kv : s_faces) if (kv.second == nt.f) { alive = true; break; }
                if (!alive || nt.var < 0 || nt.var >= (int)nt.f->var.size()) continue;
                float dx = p[i * 4] - g_eyePos[0], dy = p[i * 4 + 1] - g_eyePos[1], dz = p[i * 4 + 2] - g_eyePos[2];
                float d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (!(d > 0.01f && d < 500.f)) continue;
                LodVar& v = nt.f->var[nt.var]; v.dmin = std::min(v.dmin, d); v.dmax = std::max(v.dmax, d); v.nd++; nt.f->lastDist = d;
            }
            g_dev.imm->Unmap(s_stage[rd], 0);
        }
    }
    // resumen al log cada 20 s
    double t = Now();
    if (DiagOn() && t >= s_nextLog) {
        s_nextLog = t + 20.0;
        std::lock_guard<std::mutex> lk(s_mx);
        Log("lod caras: %d caras vistas en este partido, %d sustituciones de LOD0%s", (int)s_faces.size(), g_lodSubs.load(), ForceOn() ? "" : " (LOD0 forzado apagado)");
        for (auto& kv : s_faces) {
            LodFace* f = kv.second; std::string line = "  cara " + (f->id.empty() ? std::string("?") : f->id.substr(0, 8)) + ":";
            std::vector<const LodVar*> vs; for (auto& v : f->var) vs.push_back(&v);
            std::sort(vs.begin(), vs.end(), [](const LodVar* a, const LodVar* b) { return a->count > b->count; });
            for (auto* v : vs) {
                char b[256];
                if (v->nd > 0) snprintf(b, sizeof b, " | %u idx VS %s PS %s %.1f..%.1f m x%d", v->count, Hex8(v->vs).c_str(), v->ps ? Hex8(v->ps).c_str() : "sin PS", v->dmin, v->dmax, v->draws);
                else snprintf(b, sizeof b, " | %u idx VS %s PS %s x%d", v->count, Hex8(v->vs).c_str(), v->ps ? Hex8(v->ps).c_str() : "sin PS", v->draws);
                line += b;
                std::string cb; for (int i = 0; i < 8; i++) if (v->cb[i]) { char q[32]; snprintf(q, sizeof q, "%sb%d=%u", cb.empty() ? " [" : " ", i, v->cb[i]); cb += q; }
                if (!cb.empty()) line += cb + "]";
                if (v->vb0 == (void*)f->best.vb[0] && v->count != f->best.count) line += " (mismo VB que LOD0)";
            }
            if (f->parts) line += " | (en partes)";
            if (f->incompat) { char b[64]; snprintf(b, sizeof b, " | incompatibles: %d", f->incompat); line += b; }
            if (f->subs) { char b[64]; snprintf(b, sizeof b, " | sustituidos: %d", f->subs); line += b; }
            Log("%s", line.c_str());
        }
    }
}

} // namespace fx
