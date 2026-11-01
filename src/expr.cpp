// PES21 FX - expresiones faciales (31/10). La cara se deforma con los huesos faciales del motor (skf_*: cejas, glabela,
// parpados, orbicular, mejillas, surcos, nariz, labios, mandibula, papada), que el VS de la cabeza (faab87ee) y el de la
// cara animada (455eb97b) reciben en una paleta de 32 matrices (b6). El VS del mod (pes_expr.hlsli) agranda el gesto:
// lo que cada hueso facial corre al vertice respecto de la cabeza rigida se multiplica por la ganancia del menu (fila 340/341).
// Para eso hace falta saber en que lugar de la paleta esta sk_head, y eso cambia por malla (face_rig.py sobre 3 caras de
// Dream Patch 21 Faces, 31/10): paleta de 32 (piel de la cara, malla 2/3) -> cabeza en 1; paletas de 14 y 16 (nuca / cuello,
// pestanas) -> cabeza en 2; paleta de 7 (pestanas, malla 6) y la boca comun (6 huesos) -> cabeza en 0; la cara generica
// comun (facial.fmdl) la tiene AL FINAL (6 / 10 / 31). El dibujo no trae nombres, asi que la DLL la deduce de los vertices:
// sk_head es el hueso que mas pesa en la parte de arriba de la malla (el 25 % mas alto: frente, coronilla, encia de arriba).
// En las pestanas gana el parpado superior: no importa, alrededor de los ojos la ganancia por defecto es 1 (ver pes_expr.hlsli).
// La DLL lee una vez por malla (indices del dibujo -> rango de vertices -> POSITION / BLENDINDICES / BLENDWEIGHT, en el
// Present, con copias a staging como cuerpo.cpp) y, en cada dibujo de esos VS (regla "expr = <slot>"), enlaza en el VS un
// cbuffer de 16 bytes: x = indice de la cabeza (99 = sin dato todavia: el VS no toca nada), y = indice maximo usado, z = id,
// w = 1 si es la boca (01/11: el VS mueve la lengua).
#include "core.h"
#include <d3d11_1.h>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace fx {

namespace {
struct Elem { int slot = -1; UINT off = 0; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; };
struct FaceMesh {
    int id = 0, state = 0, tries = 0;   // 0 copiar indices, 1 leer indices / copiar vertices, 2 leer vertices, 3 listo, 4 descartada
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0, start = 0, count = 0; INT base = 0;
    ID3D11Buffer* vb[4] = {}; UINT stride[4] = {}, off[4] = {};
    Elem pos, idx, wgt;
    ID3D11Buffer* stIB = nullptr; ID3D11Buffer* stVB[4] = {};
    UINT vmin = 0, nv = 0;
    int maxIdx = -1, head = 99;
    ID3D11Buffer* cb = nullptr;   // x cabeza, y indice maximo, z id
    const char* why = "";
    float hc[3] = {}; bool hcOk = false;   // (1/11) centro de la cara en espacio del modelo (vertices que pesan en sk_head): para "seguir la cara"
};
std::mutex s_mx;
std::unordered_map<uint64_t, FaceMesh*> s_meshes;
std::vector<FaceMesh*> s_pending;
ID3D11Buffer* s_none = nullptr;   // cabeza 99: el VS no toca la malla (sin dato o descartada)
int s_nextId = 1;
std::atomic<int> s_binds{0}, s_bindsNone{0};
int s_hist[4] = {};   // mallas listas con cabeza 0 / 1 / 2..31 / descartadas

uint64_t Fnv(uint64_t h, uint64_t v) { for (int i = 0; i < 8; i++) { h ^= (v >> (8 * i)) & 255; h *= 0x100000001b3ULL; } return h; }
UINT FmtBytes(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: return 16;
    case DXGI_FORMAT_R32G32B32_FLOAT: case DXGI_FORMAT_R32G32B32_UINT: return 12;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R32G32_FLOAT: return 8;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_R10G10B10A2_UNORM: return 4;
    default: return 0;
    }
}
// indices de hueso (enteros) y pesos (0..1) de un vertice; componentes faltantes en 0
bool ReadIdx(const uint8_t* p, DXGI_FORMAT f, uint32_t o[4]) {
    o[0] = o[1] = o[2] = o[3] = 0;
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_UNORM: for (int i = 0; i < 4; i++) o[i] = p[i]; return true;
    case DXGI_FORMAT_R16G16B16A16_UINT: for (int i = 0; i < 4; i++) { uint16_t h; memcpy(&h, p + 2 * i, 2); o[i] = h; } return true;
    case DXGI_FORMAT_R32G32B32A32_UINT: memcpy(o, p, 16); return true;
    default: return false;
    }
}
float Half(uint16_t h) {
    uint32_t sg = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023, f;
    if (e == 0) { if (!m) f = sg << 31; else { e = 1; while (!(m & 1024)) { m <<= 1; e--; } m &= 1023; f = (sg << 31) | ((e + 112) << 23) | (m << 13); } }
    else if (e == 31) f = (sg << 31) | 0x7f800000u | (m << 13);
    else f = (sg << 31) | ((e + 112) << 23) | (m << 13);
    float r; memcpy(&r, &f, 4); return r;
}
bool ReadPos(const uint8_t* p, DXGI_FORMAT f, float o[3]) {
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32_FLOAT: memcpy(o, p, 12); return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: for (int i = 0; i < 3; i++) { uint16_t h; memcpy(&h, p + 2 * i, 2); o[i] = Half(h); } return true;
    default: return false;
    }
}
bool ReadW(const uint8_t* p, DXGI_FORMAT f, float o[4]) {
    o[0] = o[1] = o[2] = o[3] = 0;
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: for (int i = 0; i < 4; i++) o[i] = p[i] / 255.0f; return true;
    case DXGI_FORMAT_R16G16B16A16_UNORM: for (int i = 0; i < 4; i++) { uint16_t h; memcpy(&h, p + 2 * i, 2); o[i] = h / 65535.0f; } return true;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: memcpy(o, p, 16); return true;
    default: return false;
    }
}
ID3D11Buffer* MakeCB(UINT head, UINT maxIdx, UINT id, UINT mouth = 0, const int* lids = nullptr, UINT jawFull = 0, UINT jawHalf = 0, UINT lipTop = 99) {
    if (!g_dev.dev) return nullptr;
    UINT v[12] = { head, maxIdx, id, mouth, 99, 99, 99, 99, jawFull, jawHalf, lipTop, 0 };
    if (lids) for (int i = 0; i < 4; i++) v[4 + i] = lids[i] >= 0 ? (UINT)lids[i] : 99u;
    D3D11_BUFFER_DESC d{}; d.ByteWidth = 48; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA s{}; s.pSysMem = v;
    ID3D11Buffer* b = nullptr; return SUCCEEDED(g_dev.dev->CreateBuffer(&d, &s, &b)) ? b : nullptr;
}
ID3D11Buffer* Staging(UINT bytes) {
    D3D11_BUFFER_DESC d{}; d.ByteWidth = (bytes + 15) & ~15u; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* b = nullptr; return SUCCEEDED(g_dev.dev->CreateBuffer(&d, nullptr, &b)) ? b : nullptr;
}
void ReleaseIA(FaceMesh* m) {
    if (m->ib) m->ib->Release(); m->ib = nullptr;
    for (auto*& b : m->vb) { if (b) b->Release(); b = nullptr; }
    if (m->stIB) m->stIB->Release(); m->stIB = nullptr;
    for (auto*& b : m->stVB) { if (b) b->Release(); b = nullptr; }
}
void Reject(FaceMesh* m, const char* why) {
    m->state = 4; m->why = why; ReleaseIA(m); s_hist[3]++;
    static int n = 0; if (n++ < 40) Log("expresiones: malla de cara %d descartada (%u indices): %s", m->id, m->count, why);
}

void Step(ID3D11DeviceContext* imm, FaceMesh* m) {
    const UINT isz = m->ibFmt == DXGI_FORMAT_R16_UINT ? 2 : 4;
    if (m->state == 0) {
        D3D11_BUFFER_DESC d; m->ib->GetDesc(&d);
        const UINT a = m->ibOff + m->start * isz, b = a + m->count * isz;
        if (b > d.ByteWidth) { Reject(m, "indices fuera del buffer"); return; }
        if (!(m->stIB = Staging(m->count * isz))) { Reject(m, "sin memoria"); return; }
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
        m->vmin = lo; m->nv = hi - lo + 1;
        if (m->nv > 65536) { Reject(m, "demasiados vertices"); return; }
        for (int s : { m->pos.slot, m->idx.slot, m->wgt.slot }) {
            if (m->stVB[s]) continue;
            D3D11_BUFFER_DESC d; m->vb[s]->GetDesc(&d);
            UINT used = 1;
            if (m->pos.slot == s) used = (std::max)(used, m->pos.off + FmtBytes(m->pos.fmt));
            if (m->idx.slot == s) used = (std::max)(used, m->idx.off + FmtBytes(m->idx.fmt));
            if (m->wgt.slot == s) used = (std::max)(used, m->wgt.off + FmtBytes(m->wgt.fmt));
            used = (std::min)(used, m->stride[s] ? m->stride[s] : 1u);
            const UINT64 a = (UINT64)m->off[s] + (UINT64)((INT64)m->base + lo) * m->stride[s], b = a + (UINT64)(m->nv - 1) * m->stride[s] + used;
            if (!m->stride[s] || b > d.ByteWidth) { Reject(m, "vertices fuera del buffer"); return; }
            if (!(m->stVB[s] = Staging((UINT)(b - a)))) { Reject(m, "sin memoria"); return; }
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
        int mx = -1; bool ok = true;
        std::vector<float> ys(m->nv), xs(m->nv), zs(m->nv), ps((size_t)m->nv * 3); std::vector<uint32_t> iv((size_t)m->nv * 4); std::vector<float> wv((size_t)m->nv * 4);
        float ylo = 1e9f, yhi = -1e9f;
        for (UINT v = 0; v < m->nv && ok; v++) {
            float P[3]; uint32_t* I = &iv[(size_t)v * 4]; float* W = &wv[(size_t)v * 4];
            const uint8_t* pp = (const uint8_t*)mp[m->pos.slot].pData + (size_t)v * m->stride[m->pos.slot] + m->pos.off;
            const uint8_t* pi = (const uint8_t*)mp[m->idx.slot].pData + (size_t)v * m->stride[m->idx.slot] + m->idx.off;
            const uint8_t* pw = (const uint8_t*)mp[m->wgt.slot].pData + (size_t)v * m->stride[m->wgt.slot] + m->wgt.off;
            ok = ReadPos(pp, m->pos.fmt, P) && ReadIdx(pi, m->idx.fmt, I) && ReadW(pw, m->wgt.fmt, W);
            if (!ok) break;
            xs[v] = P[0]; ys[v] = P[1]; zs[v] = P[2]; ylo = (std::min)(ylo, P[1]); yhi = (std::max)(yhi, P[1]); memcpy(&ps[(size_t)v * 3], P, 12);
            for (int k = 0; k < 4; k++) if (W[k] > 0.004f && (int)I[k] > mx) mx = (int)I[k];
        }
        // sk_head: el hueso con mas peso en el 25 % mas alto de la malla; uso total por hueso para el log
        float top[32] = {}, all[32] = {}, cx[32] = {}, cy[32] = {}, cz[32] = {}, wmax[32] = {}; int cnt[32] = {};
        if (ok && mx >= 0 && mx <= 31) {
            const float cut = yhi - 0.25f * (yhi - ylo);
            for (UINT v = 0; v < m->nv; v++) for (int k = 0; k < 4; k++) {
                const uint32_t b = iv[(size_t)v * 4 + k]; const float w = wv[(size_t)v * 4 + k];
                if (b > 31 || w <= 0.004f) continue;
                all[b] += w; cnt[b]++; if (ys[v] >= cut) top[b] += w;
                cx[b] += w * xs[v]; cy[b] += w * ys[v]; cz[b] += w * zs[v]; wmax[b] = (std::max)(wmax[b], w);
            }
        }
        for (int s = 0; s < 4; s++) if (mapped[s]) imm->Unmap(m->stVB[s], 0);
        if (!ok) { Reject(m, "formato de posicion, huesos o pesos desconocido"); return; }
        if (mx < 0 || mx > 31) { Reject(m, "indices de hueso fuera de la paleta de 32"); return; }
        // (01/11) pestanas / parpados (mallas de 1-2 cm de alto): arriba gana el parpado superior (peso 1); ahi sk_head es el
        // hueso que esta en MAS vertices (face_rig: 191 / 125 / 114 / 91 contra 139 / 61 / 57 / 79 del parpado)
        const bool thin = (yhi - ylo) < 0.03f;
        int hd = 0;
        for (int b = 1; b <= mx; b++) if (thin ? cnt[b] > cnt[hd] : top[b] > top[hd]) hd = b;
        // (01/11) una pierna (1440 v, 0.05..0.54 m) tambien se dibuja con faab87ee: lo que no llega a la altura de la cara no es cara
        if (yhi < 1.40f) hd = 99;
        // (01/11) la boca (mouth.fmdl comun, boca 3D: alto 1.613..1.650 m): el VS mueve la lengua (pes_expr.hlsli PesTongue)
        // (01/11) parpados (parpadeo del mod, pes_expr.hlsli PesBlink): huesos con peso fuerte (>= 0.7) cuyo centro de vertices cae
        // en la zona del ojo (x 1.2..4.5 cm del medio, y 1.686..1.709, z > 0.125). Por lado: el mas alto = parpado de arriba,
        // el mas bajo = el de abajo (face_rig: skf_eyelid_t peso 1.0, skf_eyelid_b 0.74..0.85; orbicular 0.2..0.5 y cejas
        // quedan fuera por peso o altura). lids = { arriba izq, arriba der, abajo izq, abajo der } (izq = +x)
        int lids[4] = { -1, -1, -1, -1 };
        if (hd <= 31) {
            int cand[2][8]; int nc[2] = {};
            for (int b = 0; b <= mx; b++) {
                if (b == hd || all[b] <= 0.5f || wmax[b] < 0.7f) continue;
                const float x = cx[b] / all[b], y = cy[b] / all[b], z = cz[b] / all[b];
                if (fabsf(x) < 0.012f || fabsf(x) > 0.045f || y < 1.686f || y > 1.709f || z < 0.125f) continue;
                const int sd = x > 0 ? 0 : 1; if (nc[sd] < 8) cand[sd][nc[sd]++] = b;
            }
            for (int sd = 0; sd < 2; sd++) {
                if (!nc[sd]) continue;
                int hi = cand[sd][0], lo = cand[sd][0];
                for (int i = 1; i < nc[sd]; i++) { const int b = cand[sd][i]; if (cy[b] / all[b] > cy[hi] / all[hi]) hi = b; if (cy[b] / all[b] < cy[lo] / all[lo]) lo = b; }
                lids[sd] = hi; if (lo != hi) lids[2 + sd] = lo;
            }
        }
        const bool mouth = hd <= 31 && !thin && ylo > 1.58f && yhi < 1.68f && (yhi - ylo) < 0.06f;
        // (01/11) abrir la boca (pes_expr.hlsli PesJawApply, fila 347): huesos de abajo = centro de sus vertices debajo de la
        // linea de los labios (y 1.560..1.6275, z > 0.07, |x| < 0.05: skf_jaw, skf_lip_b_*, skf_doublechin; face_rig 111320:
        // labio de abajo 1.622..1.624, de arriba 1.634..1.635); comisuras (skf_lip_s, y 1.6275..1.6325, |x| > 0.015) a la mitad.
        // En la boca 3D la mandibula (lengua + dientes de abajo) es el hueso con mas peso fuera de la cabeza con peso 1.
        // (01/11) nariz con la boca (fila 347.y): labio de arriba del medio (skf_lip_t_c, y 1.630..1.642, |x| < 0.008, z > 0.15)
        UINT jawFull = 0, jawHalf = 0, lipTop = 99;
        if (hd <= 31 && !thin) {
            int jb = -1;
            for (int b = 0; b <= mx; b++) {
                if (b == hd || all[b] <= 0.5f) continue;
                if (hd <= 2 ? b < hd : b + 1 == hd) continue;   // cuello / pecho (como PesExprSkip)
                const float x = cx[b] / all[b], y = cy[b] / all[b], z = cz[b] / all[b];
                if (mouth && wmax[b] >= 0.9f && (jb < 0 || all[b] > all[jb])) jb = b;
                if (!mouth && fabsf(x) < 0.008f && y >= 1.630f && y < 1.642f && z > 0.15f && wmax[b] >= 0.5f && (lipTop > 31 || all[b] > all[lipTop])) lipTop = (UINT)b;
                if (z < 0.07f || fabsf(x) > 0.05f) continue;
                if (y >= 1.560f && y < 1.6275f) jawFull |= 1u << b;
                else if (y >= 1.6275f && y < 1.6325f && fabsf(x) > 0.015f) jawHalf |= 1u << b;
            }
            if (jb >= 0) { jawFull |= 1u << jb; jawHalf &= ~(1u << jb); }
        }
        m->maxIdx = mx; m->head = hd;
        {   // (1/11) centro de la cara: promedio de los vertices que pesan >= 0.9 en sk_head (si no hay, >= 0.5; si no, el 25 % de arriba)
            for (float cut : { 0.9f, 0.5f, -1.0f }) {
                double c[3] = {}; int n = 0;
                for (UINT v = 0; v < m->nv; v++) {
                    float w = 0; for (int k = 0; k < 4; k++) if ((int)iv[(size_t)v * 4 + k] == hd) w += wv[(size_t)v * 4 + k];
                    const bool in = cut > 0 ? w >= cut : ys[v] >= yhi - 0.25f * (yhi - ylo);
                    if (in) { for (int k = 0; k < 3; k++) c[k] += ps[(size_t)v * 3 + k]; n++; }
                }
                if (n >= 20) { for (int k = 0; k < 3; k++) m->hc[k] = (float)(c[k] / n); m->hcOk = true; break; }
            }
        }
        m->cb = MakeCB((UINT)m->head, (UINT)mx, (UINT)m->id, mouth ? 1u : 0u, lids, jawFull, jawHalf, lipTop);
        ReleaseIA(m);
        if (!m->cb) { m->state = 4; m->why = "sin cbuffer"; s_hist[3]++; return; }
        m->state = 3; s_hist[m->head <= 1 ? m->head : m->head > 31 ? 3 : 2]++;
        static int n = 0;
        if (n++ < 80) {   // uso de cada hueso de la paleta (peso total / peso en la parte alta) para comparar con face_rig.py
            char l[1400]; int k = snprintf(l, sizeof l, "expresiones: malla de cara %d (%u indices, %u vertices, alto %.3f..%.3f m): hueso maximo %d -> cabeza en la paleta %d%s, parpados %d %d / %d %d, boca abajo %08X mitad %08X labio %u |",
                                           m->id, m->count, m->nv, ylo, yhi, mx, m->head, m->head > 31 ? " (no es cara)" : thin ? " (fina: por vertices)" : mouth ? " (boca: lengua)" : "", lids[0], lids[1], lids[2], lids[3], jawFull, jawHalf, lipTop);
            for (int b = 0; b <= mx && k < (int)sizeof l - 24; b++) if (all[b] > 0) k += snprintf(l + k, sizeof l - k, " %d:%.0f/%.0f", b, all[b], top[b]);
            Log("%s", l);
        }
    }
}
}   // namespace

// en cada dibujo con la regla "expr = <slot>" (VS de la cabeza / cara): cbuffer de la malla para el VS (null = no enlazar)
ID3D11Buffer* ExprBufferForDraw(ID3D11DeviceContext* c, UINT count, UINT start, INT base, const float** headCentroid, int* headIdx) {
    if (headCentroid) *headCentroid = nullptr;
    if (!g_dev.dev) return nullptr;
    if (!s_none) { std::lock_guard<std::mutex> lk(s_mx); if (!s_none) s_none = MakeCB(99, 0, 0); }
    if (g_cfg.live[340][0] < 0.5f || count < 3) { s_bindsNone++; return s_none; }
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0; c->IAGetIndexBuffer(&ib, &ibFmt, &ibOff);
    ID3D11Buffer* vb[4] = {}; UINT stride[4] = {}, off[4] = {}; c->IAGetVertexBuffers(0, 4, vb, stride, off);
    ID3D11InputLayout* il = nullptr; c->IAGetInputLayout(&il);
    auto rel = [&]() { if (ib) ib->Release(); for (auto*& b : vb) { if (b) b->Release(); b = nullptr; } if (il) il->Release(); ib = nullptr; il = nullptr; };
    if (!ib || !il || (ibFmt != DXGI_FORMAT_R16_UINT && ibFmt != DXGI_FORMAT_R32_UINT)) { rel(); s_bindsNone++; return s_none; }
    uint64_t key = 0xcbf29ce484222325ULL;
    key = Fnv(key, (uint64_t)ib); key = Fnv(key, ibOff); key = Fnv(key, start); key = Fnv(key, count); key = Fnv(key, (uint64_t)(int64_t)base);
    key = Fnv(key, (uint64_t)il); for (int i = 0; i < 4; i++) { key = Fnv(key, (uint64_t)vb[i]); key = Fnv(key, off[i]); key = Fnv(key, stride[i]); }
    std::lock_guard<std::mutex> lk(s_mx);
    auto it = s_meshes.find(key);
    if (it != s_meshes.end()) {
        rel(); FaceMesh* m = it->second;
        if (m->state == 3 && m->cb) { s_binds++; if (headCentroid && m->hcOk && m->nv >= 300) { *headCentroid = m->hc; if (headIdx) *headIdx = m->head; } return m->cb; }
        s_bindsNone++; return s_none;
    }
    if (s_meshes.size() >= 2048) {   // partido largo / muchas caras: se empieza de nuevo (las cuentas y buffers cambian entre partidos)
        for (auto& kv : s_meshes) { ReleaseIA(kv.second); if (kv.second->cb) kv.second->cb->Release(); delete kv.second; }
        s_meshes.clear(); s_pending.clear();
    }
    FaceMesh* m = new FaceMesh(); m->id = s_nextId++;
    m->ib = ib; m->ibFmt = ibFmt; m->ibOff = ibOff; m->start = start; m->count = count; m->base = base;
    for (int i = 0; i < 4; i++) { m->vb[i] = vb[i]; m->stride[i] = stride[i]; m->off[i] = off[i]; vb[i] = nullptr; }
    ib = nullptr;
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
            else if (e.idx == 0 && !_stricmp(e.sem.c_str(), "BLENDINDICES")) t = &m->idx;
            else if (e.idx == 0 && !_stricmp(e.sem.c_str(), "BLENDWEIGHT")) t = &m->wgt;
            if (t && e.slot < 4) { t->slot = (int)e.slot; t->off = o; t->fmt = (DXGI_FORMAT)e.fmt; }
        }
        if (m->pos.slot < 0 || m->idx.slot < 0 || m->wgt.slot < 0) { m->state = 4; m->why = "sin POSITION / BLENDINDICES / BLENDWEIGHT"; }
        else if (!m->vb[m->pos.slot] || !m->vb[m->idx.slot] || !m->vb[m->wgt.slot]) { m->state = 4; m->why = "falta un buffer de vertices"; }
    }
    rel();
    s_meshes[key] = m;
    if (m->state == 0) s_pending.push_back(m); else { ReleaseIA(m); s_hist[3]++; }
    s_bindsNone++; return s_none;
}

// ---- (1/11) posicion de la cabeza en el mundo para "seguir la cara" (freecam.cpp) ----
// En el dibujo de la cara (VS faab87ee) la paleta de huesos va en b6 (VSBones: float4x3 m_boneMatrices[32], con desplazamiento de
// constantes). Se copia la matriz de sk_head (48 bytes) a un buffer de lectura y en el Present siguiente se transforma el centro de
// la cara (espacio del modelo): mundo[k] = registro k . (hc, 1). Una captura por cuadro.
namespace {
struct HeadCap { ID3D11Buffer* st = nullptr; float hc[3] = {}; int tries = 0; };
std::mutex s_hmx; std::vector<HeadCap> s_hcaps; std::atomic<bool> s_hcapThisFrame{false};
float s_headPos[3] = {}; double s_headT = 0; std::atomic<int> s_headOk{0}, s_headFail{0};
}
void ExprCaptureHead(ID3D11DeviceContext* c, const float* hc, int head) {
    if (!g_dev.dev || !hc || head < 0 || head > 31 || s_hcapThisFrame.exchange(true)) return;
    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) || !c1) return;
    ID3D11Buffer* b = nullptr; UINT first = 0, num = 0;
    c1->VSGetConstantBuffers1(6, 1, &b, &first, &num); c1->Release();
    if (!b) { s_headFail++; return; }
    D3D11_BUFFER_DESC d; b->GetDesc(&d);
    const UINT a = first * 16 + (UINT)head * 48;
    if (a + 48 > d.ByteWidth) { b->Release(); s_headFail++; return; }
    ID3D11Buffer* st = Staging(48);
    if (st) {
        D3D11_BOX bx = { a, 0, 0, a + 48, 1, 1 }; c->CopySubresourceRegion(st, 0, 0, 0, 0, b, 0, &bx);
        std::lock_guard<std::mutex> lk(s_hmx);
        HeadCap h; h.st = st; memcpy(h.hc, hc, 12); s_hcaps.push_back(h);
    }
    b->Release();
}
bool ExprHeadWorld(float out[3], double* age) {
    std::lock_guard<std::mutex> lk(s_hmx);
    if (s_headT == 0) return false;
    memcpy(out, s_headPos, 12); if (age) *age = Now() - s_headT; return true;
}
static void HeadPresent(ID3D11DeviceContext* imm) {
    s_hcapThisFrame = false;
    std::lock_guard<std::mutex> lk(s_hmx);
    for (size_t i = 0; i < s_hcaps.size();) {
        HeadCap& h = s_hcaps[i];
        D3D11_MAPPED_SUBRESOURCE mp; HRESULT hr = imm->Map(h.st, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mp);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING && ++h.tries < 60) { i++; continue; }
        if (SUCCEEDED(hr)) {
            const float* r = (const float*)mp.pData; float w[3];
            for (int k = 0; k < 3; k++) w[k] = r[k * 4 + 0] * h.hc[0] + r[k * 4 + 1] * h.hc[1] + r[k * 4 + 2] * h.hc[2] + r[k * 4 + 3];
            imm->Unmap(h.st, 0);
            if (w[0] == w[0] && w[1] == w[1] && w[2] == w[2] && fabsf(w[0]) < 1000 && fabsf(w[1]) < 1000 && fabsf(w[2]) < 1000) {
                memcpy(s_headPos, w, 12); s_headT = Now();
                if (s_headOk++ == 0) Log("expresiones: cabeza en el mundo (%.2f, %.2f, %.2f) desde el centro de la cara (%.3f, %.3f, %.3f)", w[0], w[1], w[2], h.hc[0], h.hc[1], h.hc[2]);
            } else s_headFail++;
        } else s_headFail++;
        h.st->Release(); s_hcaps.erase(s_hcaps.begin() + i);
    }
}

// Present (contexto inmediato): avanza la lectura de hasta 8 mallas nuevas; resumen al log cada ~60 s
void ExprPresent() {
    ID3D11DeviceContext* imm = g_dev.imm; if (!imm) return;
    HeadPresent(imm);
    {   // con el candado tomado: los dibujos (otros hilos) leen state / cb y pueden vaciar la tabla. Los Map son DO_NOT_WAIT.
        std::lock_guard<std::mutex> lk(s_mx);
        for (size_t i = 0; i < s_pending.size() && i < 8; i++) Step(imm, s_pending[i]);
        s_pending.erase(std::remove_if(s_pending.begin(), s_pending.end(), [](FaceMesh* m) { return m->state >= 3; }), s_pending.end());
    }
    static int frames = 0;
    if (++frames >= 3600) {
        frames = 0;
        const int b = s_binds.exchange(0), bn = s_bindsNone.exchange(0);
        if (b || bn) { std::lock_guard<std::mutex> lk(s_mx);
            Log("expresiones: %d mallas de cara (cabeza en 0: %d, en 1: %d, en otro: %d, descartadas o no cara %d); dibujos con dato %d, sin dato %d; ganancia %.2f",
                (int)s_meshes.size(), s_hist[0], s_hist[1], s_hist[2], s_hist[3], b, bn, g_cfg.live[340][1]); }
    }
}

}   // namespace fx
