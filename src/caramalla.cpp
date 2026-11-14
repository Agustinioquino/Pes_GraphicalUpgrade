// PES21 FX (08/11 p, Agustin: "la idea con la teselacion es mejorar la cantidad de poligonos de las caras, asi como los juegos modernos
// ... cargar el modelo de mayor calidad"; "por ahora centremonos en la cara de messi, una subdivision suave"; "subdividir solo donde mas
// se nota el bajo nivel poligonal: la punta de la nariz, los labios, cerca de los ojos, sin romper el modelo")
// CARA CON MAS POLIGONOS. La cara de Messi viene dentro del cpk cifrado del Dream Patch, asi que no se toca ningun archivo del juego:
//   1) VOLCADO (caras_hd.ini malla_volcar = 1): la primera vez que se dibuja la piel de una cara (PS 5ce3ed87 / ce188ae1, nunca la
//      boca 63fab773) se guardan sus indices y TODOS sus vertices tal cual estan en la placa, con el formato de la entrada (layout),
//      en PES21FX\caras_hd\mallas\<cara>_<indices>_<inicio>.mesh. Copias a staging en el Present (contexto inmediato), en 2 pasos.
//   2) SUBDIVISION fuera del juego (tools/fox/face/subdividir_malla.py): parte las aristas donde la malla se ve facetada (nariz,
//      labios, parpados) con redondeo de Phong, sin mover los vertices del juego ni los bordes de cada pieza; los vertices nuevos van
//      DESPUES de los viejos (los viejos conservan su numero) y cada uno anota sus dos vertices de origen. -> <...>_hd.mesh
//   3) DIBUJO (malla_hd = 1): cuando el juego dibuja esa cara con esa misma cuenta de indices, la DLL le cambia los buffers por los
//      de la malla subdividida (como el LOD0 forzado: LodBind / LodRestore, mismo VS, mismo layout, mismos huesos). Indices R32 con
//      el mismo numero de vertice del juego (vmin + local) y base -vmin: SV_VertexID sigue siendo el del juego para los vertices
//      viejos, asi las expresiones (t118), el peso de borde de la teselacion (t119) y todo lo que va por vertice siguen igual; los
//      nuevos los toman de sus origenes (t114, pes_expr.hlsli PesHdParent). Tambien en el prepaso / sombras con los mismos buffers.
// La boca (mouth.fmdl, PS 63fab773, pes_oral_*) no pasa por aca: solo la piel de la cara.
#include "core.h"
#include <d3d11_1.h>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <tuple>
#include <cstdio>

namespace fx {

static ID3D11Buffer* Staging(UINT bytes) {
    D3D11_BUFFER_DESC d{}; d.ByteWidth = (bytes + 15) & ~15u; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* b = nullptr; return SUCCEEDED(g_dev.dev->CreateBuffer(&d, nullptr, &b)) ? b : nullptr;
}

static std::wstring Dir() { return g_cfg.baseDir + L"caras_hd\\mallas\\"; }
static std::wstring W(const std::string& a) { return std::wstring(a.begin(), a.end()); }
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static std::mutex s_mx;
static bool s_volcar = false, s_hdOn = false; static double s_iniNext = 0;
std::atomic<bool> g_caraMallaAny{false};   // hay alguna malla HD cargada (dibujos sin cara: prepaso / sombras)

typedef std::pair<std::string, UINT> Key;   // cara + cuenta de indices del dibujo
struct Job {
    std::string id; UINT count = 0, start = 0; INT base = 0; D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0;
    ID3D11Buffer* vb[4] = {}; UINT stride[4] = {}, off[4] = {}; UINT mask = 0;
    std::vector<InputElemInfo> el;
    int state = 0, tries = 0; ID3D11Buffer* stIB = nullptr; ID3D11Buffer* stVB[4] = {}; UINT stBytes[4] = {};
    std::vector<uint32_t> loc; UINT vmin = 0, nv = 0;
    void Free() {
        if (ib) ib->Release(); ib = nullptr; for (auto*& b : vb) { if (b) b->Release(); b = nullptr; }
        if (stIB) stIB->Release(); stIB = nullptr; for (auto*& b : stVB) { if (b) b->Release(); b = nullptr; }
    }
};
struct Hd {
    std::string id; UINT count = 0; bool tried = false, ok = false; double retry = 0;
    ID3D11Buffer* ib = nullptr; ID3D11Buffer* vb[4] = {}; UINT stride[4] = {}; UINT mask = 0;
    ID3D11Buffer* parBuf = nullptr; ID3D11ShaderResourceView* par = nullptr;
    UINT newCount = 0, vmin = 0, nvOld = 0, nvNew = 0; INT base = 0;
};
struct BufKey { void* vb0; void* ib; UINT count, start; INT base;
    bool operator<(const BufKey& o) const { return std::tie(vb0, ib, count, start, base) < std::tie(o.vb0, o.ib, o.count, o.start, o.base); } };
static std::map<Key, int> s_seen;            // volcado: 1 en cola, 2 escrito / ya estaba, 3 fallo
static std::vector<Job*> s_jobs;
static std::map<Key, Hd*> s_hd;
static std::map<BufKey, Hd*> s_byBuf;        // buffers del juego de la cara -> malla HD (prepaso / sombras, sin cara identificada)
static std::set<UINT> s_counts;              // cuentas de indices con malla HD (filtro rapido)
static int s_matchSeen = -1;
static std::atomic<int> s_stSwap{0}, s_stSwapAny{0}, s_stReject{0};
static double s_nextLog = 0;

static void ReadIni() {
    const std::wstring ini = g_cfg.baseDir + L"caras_hd\\caras_hd.ini";
    const bool v = GetPrivateProfileIntW(L"caras_hd", L"malla_volcar", 0, ini.c_str()) != 0;
    const bool h = GetPrivateProfileIntW(L"caras_hd", L"malla_hd", 0, ini.c_str()) != 0;
    if (v != s_volcar || h != s_hdOn) Log("cara con mas poligonos: volcar mallas %s, malla HD %s (caras_hd.ini malla_volcar / malla_hd)", v ? "SI" : "no", h ? "SI" : "no");
    s_volcar = v; s_hdOn = h;
}

static bool SlotsMatch(ID3D11Buffer* const vbs[4], const UINT st[4], const Hd* h) {
    for (int i = 0; i < 4; i++) {
        const bool g = vbs[i] != nullptr, m = (h->mask >> i) & 1;
        if (g && !m) return false;                 // el juego lee un slot que la malla HD no trae: se leeria con otro numero de vertice
        if (g && st[i] != h->stride[i]) return false;
    }
    return true;
}
static void Fill(const Hd* h, ID3D11Buffer* const vbs[4], LodIA* out) {
    *out = LodIA();
    for (int i = 0; i < 4; i++) if (vbs[i] && h->vb[i]) { out->vb[i] = h->vb[i]; out->vb[i]->AddRef(); out->stride[i] = h->stride[i]; out->offset[i] = 0; }
    out->ib = h->ib; out->ib->AddRef(); out->ibFmt = DXGI_FORMAT_R32_UINT; out->ibOff = 0;
    out->count = h->newCount; out->start = 0; out->base = h->base;
}

// piel de la cara (regla Expr del VS, con la cara identificada por la regla del PS). true = dibujar con la malla HD (out, par en t114)
bool CaraMallaDraw(ID3D11DeviceContext* c, const std::string& id, UINT count, UINT start, INT base, LodIA* out, ID3D11ShaderResourceView** par) {
    *par = nullptr;
    if (count < 300 || count % 3) return false;
    if (s_iniNext == 0) { std::lock_guard<std::mutex> lk(s_mx); if (s_iniNext == 0) { ReadIni(); s_iniNext = Now() + 2.0; } }
    if (!s_volcar && !s_hdOn) return false;
    ID3D11Buffer* vbs[4] = {}; UINT st[4] = {}, of[4] = {}; c->IAGetVertexBuffers(0, 4, vbs, st, of);
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; UINT ioff = 0; c->IAGetIndexBuffer(&ib, &fmt, &ioff);
    bool r = false;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        if (id.empty()) {   // sin cara (prepaso con el mismo VS): por los buffers
            auto it = s_byBuf.find({ vbs[0], ib, count, start, base });
            if (it != s_byBuf.end() && it->second->ok && SlotsMatch(vbs, st, it->second)) { Fill(it->second, vbs, out); *par = it->second->par; r = true; s_stSwapAny++; }
        } else {
            const Key k(id, count);
            if (s_volcar && !s_seen.count(k) && ib && vbs[0]) {
                D3D11_PRIMITIVE_TOPOLOGY tp; c->IAGetPrimitiveTopology(&tp);
                ID3D11InputLayout* il = nullptr; c->IAGetInputLayout(&il);
                Job* j = new Job(); j->id = id; j->count = count; j->start = start; j->base = base; j->topo = tp;
                j->ib = ib; ib->AddRef(); j->ibFmt = fmt; j->ibOff = ioff;
                for (int i = 0; i < 4; i++) { j->vb[i] = vbs[i]; if (vbs[i]) { vbs[i]->AddRef(); j->mask |= 1u << i; } j->stride[i] = st[i]; j->off[i] = of[i]; }
                const bool lay = il && InputLayoutInfo(il, j->el); if (il) il->Release();
                if (tp != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST || !lay || (fmt != DXGI_FORMAT_R16_UINT && fmt != DXGI_FORMAT_R32_UINT)) {
                    Log("cara con mas poligonos: la cara %s (%u indices) no se vuelca: %s", id.c_str(), count, !lay ? "layout no anotado" : tp != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST ? "no es lista de triangulos" : "indices raros");
                    j->Free(); delete j; s_seen[k] = 3;
                } else { s_seen[k] = 1; s_jobs.push_back(j); }
            }
            if (s_hdOn) {
                auto it = s_hd.find(k);
                if (it == s_hd.end()) { Hd* h = new Hd(); h->id = id; h->count = count; s_hd[k] = h; }
                else if (it->second->ok) {
                    Hd* h = it->second;
                    if (SlotsMatch(vbs, st, h)) {
                        Fill(h, vbs, out); *par = h->par; r = true; s_stSwap++;
                        if (s_byBuf.size() < 256) s_byBuf[{ vbs[0], ib, count, start, base }] = h;
                    } else s_stReject++;
                }
            }
        }
    }
    for (auto* b : vbs) if (b) b->Release(); if (ib) ib->Release();
    return r;
}
// cualquier otro DrawIndexed (sombras, prepaso con otro VS): misma malla del juego -> misma malla HD (sin t114: sin gestos esculpidos)
bool CaraMallaAnyDraw(ID3D11DeviceContext* c, UINT count, UINT start, INT base, LodIA* out) {
    if (!g_caraMallaAny.load(std::memory_order_relaxed)) return false;
    { std::lock_guard<std::mutex> lk(s_mx); if (!s_counts.count(count)) return false; }
    ID3D11ShaderResourceView* par = nullptr;
    return CaraMallaDraw(c, std::string(), count, start, base, out, &par);
}

// ---------------------------------------------------------------- volcado (Present, contexto inmediato)
static void DumpReject(Job* j, const char* why) { Log("cara con mas poligonos: no se pudo volcar la cara %s (%u indices): %s", j->id.c_str(), j->count, why); j->state = 9; }
static void WriteDump(Job* j, D3D11_MAPPED_SUBRESOURCE* mp) {
    CreateDirectoryW((g_cfg.baseDir + L"caras_hd\\").c_str(), nullptr); CreateDirectoryW(Dir().c_str(), nullptr);
    char nm[160]; snprintf(nm, sizeof nm, "%s_%u_%u.mesh", j->id.c_str(), j->count, j->start);
    const std::wstring p = Dir() + W(nm);
    FILE* f = _wfopen(p.c_str(), L"wb"); if (!f) { DumpReject(j, "no se pudo crear el archivo"); return; }
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    fwrite("PESMSH01", 1, 8, f);
    u32(1); u32(j->count); u32(j->start); u32((uint32_t)j->base); u32(j->vmin); u32(j->nv); u32((uint32_t)j->topo); u32((uint32_t)j->ibFmt);
    u32((uint32_t)j->el.size()); u32(j->mask);
    for (int i = 0; i < 4; i++) u32(j->stride[i]); for (int i = 0; i < 4; i++) u32(j->off[i]);
    for (auto& e : j->el) { char s[32] = {}; strncpy(s, e.sem.c_str(), 31); fwrite(s, 1, 32, f); u32(e.idx); u32(e.fmt); u32(e.slot); u32(e.off); }
    fwrite(j->loc.data(), 4, j->loc.size(), f);
    std::vector<uint8_t> pad;
    for (int s = 0; s < 4; s++) if (j->stVB[s]) {
        const size_t want = (size_t)j->nv * j->stride[s];
        fwrite(mp[s].pData, 1, (std::min)(want, (size_t)j->stBytes[s]), f);
        if (want > j->stBytes[s]) { pad.assign(want - j->stBytes[s], 0); fwrite(pad.data(), 1, pad.size(), f); }
    }
    fclose(f);
    std::string lay; for (auto& e : j->el) { char b[64]; snprintf(b, sizeof b, "%s%u/%u@%u+%u ", e.sem.c_str(), e.idx, e.fmt, e.slot, e.off); lay += b; }
    Log("cara con mas poligonos: volcada la cara %s: %u indices, %u vertices (primer vertice %u), slots %x, layout %s-> caras_hd\\mallas\\%s",
        j->id.c_str(), j->count, j->nv, j->vmin, j->mask, lay.c_str(), nm);
    j->state = 8;
}
static void DumpStep(ID3D11DeviceContext* imm, Job* j) {
    const UINT isz = j->ibFmt == DXGI_FORMAT_R16_UINT ? 2 : 4;
    if (j->state == 0) {
        char nm[160]; snprintf(nm, sizeof nm, "%s_%u_%u.mesh", j->id.c_str(), j->count, j->start);
        if (Exists(Dir() + W(nm))) { j->state = 8; return; }   // ya volcada (otra sesion)
        D3D11_BUFFER_DESC d; j->ib->GetDesc(&d);
        const UINT a = j->ibOff + j->start * isz, b = a + j->count * isz;
        if (b > d.ByteWidth) { DumpReject(j, "indices fuera del buffer"); return; }
        if (!(j->stIB = Staging(j->count * isz))) { DumpReject(j, "sin memoria"); return; }
        D3D11_BOX bx = { a, 0, 0, b, 1, 1 }; imm->CopySubresourceRegion(j->stIB, 0, 0, 0, 0, j->ib, 0, &bx);
        j->state = 1; j->tries = 0; return;
    }
    if (j->state == 1) {
        D3D11_MAPPED_SUBRESOURCE mp; HRESULT hr = imm->Map(j->stIB, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mp);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) { if (++j->tries > 120) DumpReject(j, "la copia de los indices no llego"); return; }
        if (FAILED(hr)) { DumpReject(j, "no se pudieron leer los indices"); return; }
        UINT lo = 0xffffffffu, hi = 0; j->loc.resize(j->count);
        for (UINT i = 0; i < j->count; i++) { const UINT v = isz == 2 ? ((const uint16_t*)mp.pData)[i] : ((const uint32_t*)mp.pData)[i]; j->loc[i] = v; lo = (std::min)(lo, v); hi = (std::max)(hi, v); }
        imm->Unmap(j->stIB, 0); j->stIB->Release(); j->stIB = nullptr;
        if (hi < lo) { DumpReject(j, "sin indices"); return; }
        j->vmin = lo; j->nv = hi - lo + 1;
        if (j->nv > 200000) { DumpReject(j, "demasiados vertices"); return; }
        for (auto& v : j->loc) v -= lo;
        for (int s = 0; s < 4; s++) {
            if (!j->vb[s] || !j->stride[s]) continue;
            D3D11_BUFFER_DESC d; j->vb[s]->GetDesc(&d);
            const INT64 a = (INT64)j->off[s] + ((INT64)j->base + lo) * j->stride[s];
            INT64 b = a + (INT64)j->nv * j->stride[s];
            if (a < 0 || a + (INT64)(j->nv - 1) * j->stride[s] >= (INT64)d.ByteWidth) { DumpReject(j, "vertices fuera del buffer"); return; }
            b = (std::min)(b, (INT64)d.ByteWidth);   // el ultimo vertice puede no ocupar todo el paso
            j->stBytes[s] = (UINT)(b - a);
            if (!(j->stVB[s] = Staging(j->stBytes[s]))) { DumpReject(j, "sin memoria"); return; }
            D3D11_BOX bx = { (UINT)a, 0, 0, (UINT)b, 1, 1 }; imm->CopySubresourceRegion(j->stVB[s], 0, 0, 0, 0, j->vb[s], 0, &bx);
        }
        j->state = 2; j->tries = 0; return;
    }
    if (j->state == 2) {
        D3D11_MAPPED_SUBRESOURCE mp[4] = {}; bool mapped[4] = {};
        for (int s = 0; s < 4; s++) if (j->stVB[s]) {
            HRESULT hr = imm->Map(j->stVB[s], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mp[s]);
            if (FAILED(hr)) {
                for (int k = 0; k < 4; k++) if (mapped[k]) imm->Unmap(j->stVB[k], 0);
                if (hr == DXGI_ERROR_WAS_STILL_DRAWING) { if (++j->tries > 120) DumpReject(j, "la copia de los vertices no llego"); }
                else DumpReject(j, "no se pudieron leer los vertices");
                return;
            }
            mapped[s] = true;
        }
        WriteDump(j, mp);
        for (int s = 0; s < 4; s++) if (mapped[s]) imm->Unmap(j->stVB[s], 0);
    }
}

// ---------------------------------------------------------------- carga de la malla HD
static bool ReadAll(const std::wstring& p, std::vector<uint8_t>& out) {
    FILE* f = _wfopen(p.c_str(), L"rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 256 * 1024 * 1024) { fclose(f); return false; }
    out.resize((size_t)n); const bool ok = fread(out.data(), 1, out.size(), f) == out.size(); fclose(f); return ok;
}
static void LoadHd(Hd* h) {
    h->tried = true;
    char pat[160]; snprintf(pat, sizeof pat, "%s_%u_*_hd.mesh", h->id.c_str(), h->count);
    WIN32_FIND_DATAW fd; HANDLE fh = FindFirstFileW((Dir() + W(pat)).c_str(), &fd);
    if (fh == INVALID_HANDLE_VALUE) { h->retry = Now() + 10.0; return; }   // esta cara no tiene malla HD (lo normal): se vuelve a mirar en 10 s
    const std::wstring p = Dir() + fd.cFileName; FindClose(fh);
    std::vector<uint8_t> b; if (!ReadAll(p, b)) { Log("cara con mas poligonos: no se pudo leer la malla HD de %s", h->id.c_str()); return; }
    size_t o = 8; auto u32 = [&](uint32_t& v) { if (o + 4 > b.size()) return false; memcpy(&v, &b[o], 4); o += 4; return true; };
    uint32_t ver = 0, oc = 0, vmin = 0, nvOld = 0, nvNew = 0, nc = 0, mask = 0, st[4] = {};
    bool ok = b.size() > 8 && memcmp(b.data(), "PESMSHHD", 8) == 0 && u32(ver) && ver == 1 && u32(oc) && u32(vmin) && u32(nvOld) && u32(nvNew) && u32(nc) && u32(mask);
    for (int i = 0; i < 4 && ok; i++) ok = u32(st[i]);
    if (!ok || oc != h->count || nvNew < nvOld || nc % 3 || !nc || !mask) { Log("cara con mas poligonos: malla HD de %s invalida (cabecera)", h->id.c_str()); return; }
    size_t need = (size_t)nc * 4; for (int s = 0; s < 4; s++) if ((mask >> s) & 1) need += (size_t)nvNew * st[s];
    need += (size_t)(nvNew - nvOld) * 8;
    if (o + need != b.size()) { Log("cara con mas poligonos: malla HD de %s invalida (largo %zu, esperado %zu)", h->id.c_str(), b.size(), o + need); return; }
    std::vector<uint32_t> idx(nc); memcpy(idx.data(), &b[o], (size_t)nc * 4); o += (size_t)nc * 4;
    for (auto& v : idx) { if (v >= nvNew) { Log("cara con mas poligonos: malla HD de %s invalida (indice %u)", h->id.c_str(), v); return; } v += vmin; }
    ID3D11Device* dev = g_dev.dev;
    D3D11_BUFFER_DESC d{}; D3D11_SUBRESOURCE_DATA sd{};
    d.ByteWidth = nc * 4; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_INDEX_BUFFER; sd.pSysMem = idx.data();
    if (FAILED(dev->CreateBuffer(&d, &sd, &h->ib))) { Log("cara con mas poligonos: sin memoria (indices)"); return; }
    for (int s = 0; s < 4; s++) if ((mask >> s) & 1) {
        d.ByteWidth = nvNew * st[s]; d.BindFlags = D3D11_BIND_VERTEX_BUFFER; sd.pSysMem = &b[o]; o += (size_t)nvNew * st[s];
        if (FAILED(dev->CreateBuffer(&d, &sd, &h->vb[s]))) { Log("cara con mas poligonos: sin memoria (vertices)"); return; }
        h->stride[s] = st[s];
    }
    if (nvNew > nvOld) {
        const uint32_t* pa = (const uint32_t*)&b[o];
        for (size_t i = 0; i < (size_t)(nvNew - nvOld) * 2; i++) if (pa[i] >= nvOld) { Log("cara con mas poligonos: malla HD de %s invalida (origen %u)", h->id.c_str(), pa[i]); return; }
        d.ByteWidth = (nvNew - nvOld) * 8; d.BindFlags = D3D11_BIND_SHADER_RESOURCE; sd.pSysMem = pa;
        if (FAILED(dev->CreateBuffer(&d, &sd, &h->parBuf))) return;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{}; vd.Format = DXGI_FORMAT_R32G32_UINT; vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; vd.Buffer.NumElements = nvNew - nvOld;
        if (FAILED(dev->CreateShaderResourceView(h->parBuf, &vd, &h->par))) return;
    }
    h->mask = mask; h->newCount = nc; h->vmin = vmin; h->nvOld = nvOld; h->nvNew = nvNew; h->base = -(INT)vmin; h->ok = true;
    s_counts.insert(h->count); g_caraMallaAny = true;
    Log("cara con mas poligonos: malla HD de la cara %s cargada: %u -> %u triangulos, %u -> %u vertices (%ls)", h->id.c_str(), oc / 3, nc / 3, nvOld, nvNew, fd.cFileName);
}
static void FreeHd(Hd* h) {
    for (auto*& v : h->vb) { if (v) DeferRelease(v); v = nullptr; }
    if (h->ib) DeferRelease(h->ib); if (h->par) DeferRelease(h->par); if (h->parBuf) DeferRelease(h->parBuf);
    h->ib = nullptr; h->par = nullptr; h->parBuf = nullptr; h->ok = false;
}

void CaraMallaPresent() {
    if (!g_dev.dev || !g_dev.imm) return;
    std::lock_guard<std::mutex> lk(s_mx);
    const double t = Now();
    if (t > s_iniNext) {
        const bool was = s_hdOn; ReadIni(); s_iniNext = t + 2.0;
        if (was && !s_hdOn) { for (auto& kv : s_hd) { FreeHd(kv.second); delete kv.second; } s_hd.clear(); s_byBuf.clear(); s_counts.clear(); g_caraMallaAny = false; }
    }
    const int mg = g_matchGen.load();
    if (mg != s_matchSeen) { s_matchSeen = mg; s_byBuf.clear(); }   // partido nuevo: los buffers del juego cambian
    for (size_t i = 0; i < s_jobs.size(); ) {
        Job* j = s_jobs[i]; DumpStep(g_dev.imm, j);
        if (j->state >= 8) { s_seen[{ j->id, j->count }] = j->state == 8 ? 2 : 3; j->Free(); delete j; s_jobs.erase(s_jobs.begin() + i); }
        else i++;
    }
    int loads = 0;
    for (auto& kv : s_hd) {
        Hd* h = kv.second; if (h->tried && !h->ok && h->retry > 0 && t > h->retry) { h->tried = false; h->retry = 0; }
        if (!h->tried && loads < 1) { LoadHd(h); loads++; }   // una por cuadro
    }
    if (t > s_nextLog && (s_stSwap || s_stSwapAny || s_stReject)) {
        s_nextLog = t + 20.0;
        Log("cara con mas poligonos: %d dibujos de la cara con la malla HD, %d de prepaso / sombras, %d sin cambiar (slots o pasos distintos)", s_stSwap.exchange(0), s_stSwapAny.exchange(0), s_stReject.exchange(0));
    }
}

}  // namespace fx
