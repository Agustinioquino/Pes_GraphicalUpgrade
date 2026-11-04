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
#include <map>
#include <DDSTextureLoader.h>

namespace fx {

namespace {
struct Elem { int slot = -1; UINT off = 0; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; };
struct FaceMesh {
    int id = 0, state = 0, tries = 0;   // 0 copiar indices, 1 leer indices / copiar vertices, 2 leer vertices, 3 listo, 4 descartada
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0, start = 0, count = 0; INT base = 0;
    ID3D11Buffer* vb[4] = {}; UINT stride[4] = {}, off[4] = {};
    Elem pos, idx, wgt, uv;   // (04/11) uv: TEXCOORD0, solo para el volcado
    bool dump = false; std::vector<uint32_t> tri;   // (04/11) volcar esta malla a OBJ (w354 = 1) y sus indices (relativos a vmin)
    ID3D11Buffer* stIB = nullptr; ID3D11Buffer* stVB[4] = {};
    UINT vmin = 0, nv = 0;
    int maxIdx = -1, head = 99;
    ID3D11Buffer* cb = nullptr;   // x cabeza, y indice maximo, z id
    const char* why = "";
    float hc[3] = {}; bool hcOk = false;   // (1/11) centro de la cara en espacio del modelo (vertices que pesan en sk_head): para "seguir la cara"
    UINT lastFrame = 0;   // (01/11) ultimo cuadro en que se dibujo (para echar las mallas viejas sin vaciar toda la tabla)
    int lids[4] = { -1, -1, -1, -1 }; UINT lidCap = 0;   // (02/11 h) parpados en la paleta y ultimo cuadro en que se leyo la paleta
    int shapeOff = -1; uint32_t huella = 0; ID3D11ShaderResourceView* shapeWr = nullptr;   // (04/11) gestos esculpidos (formas\<huella>.bin): primer float4 de esta malla en s_shapeData
    ID3D11Buffer* seamBuf = nullptr; ID3D11ShaderResourceView* seamSrv = nullptr; int seamN = 0;   // (04/11 b) teselacion (facetess.cpp): peso de borde por vertice (t119), solo piel de la cara; seamN = vertices con peso 0
};
std::mutex s_mx;
std::unordered_map<uint64_t, FaceMesh*> s_meshes;
std::vector<FaceMesh*> s_pending;
ID3D11Buffer* s_none = nullptr;   // cabeza 99: el VS no toca la malla (sin dato o descartada)
int s_nextId = 1;
UINT s_frame = 0;   // cuadros (Present) desde el arranque
int s_evicted = 0;   // mallas echadas por viejas (resumen)
std::atomic<int> s_binds{0}, s_bindsNone{0};
std::atomic<int> s_shapeDraws{0};   // (04/11) dibujos con gestos esculpidos
std::atomic<int> s_bindsWait{0}, s_bindsDisc{0}, s_bindsNew{0};   // (01/11) de los 'sin dato': malla en lectura / descartada o no cara / recien vista
int s_hist[4] = {};   // mallas listas con cabeza 0 / 1 / 2..31 / descartadas

uint64_t Fnv(uint64_t h, uint64_t v) { for (int i = 0; i < 8; i++) { h ^= (v >> (8 * i)) & 255; h *= 0x100000001b3ULL; } return h; }
UINT FmtBytes(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: return 16;
    case DXGI_FORMAT_R32G32B32_FLOAT: case DXGI_FORMAT_R32G32B32_UINT: return 12;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R32G32_FLOAT: return 8;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_R10G10B10A2_UNORM: return 4;
    case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_UNORM: return 4;
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
bool ReadUV(const uint8_t* p, DXGI_FORMAT f, float o[2]) {   // (04/11) para el volcado
    o[0] = o[1] = 0;
    switch (f) {
    case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R32G32B32_FLOAT: case DXGI_FORMAT_R32G32B32A32_FLOAT: memcpy(o, p, 8); return true;
    case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16B16A16_FLOAT: for (int i = 0; i < 2; i++) { uint16_t h; memcpy(&h, p + 2 * i, 2); o[i] = Half(h); } return true;
    case DXGI_FORMAT_R16G16_UNORM: for (int i = 0; i < 2; i++) { uint16_t h; memcpy(&h, p + 2 * i, 2); o[i] = h / 65535.0f; } return true;
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
ID3D11Buffer* MakeCB(UINT head, UINT maxIdx, UINT id, UINT mouth = 0, const int* lids = nullptr, UINT jawFull = 0, UINT jawHalf = 0, UINT lipTop = 99, UINT lipUp = 0, const float* eyes = nullptr, const int* mus = nullptr, const UINT* shape = nullptr) {
    if (!g_dev.dev) return nullptr;
    // 48 bytes de uint4 + 2 float4: centros de los ojos (x y z radio) izquierdo (+x, hueso 0) y derecho (hueso 1), para la mirada
    // + uint4 musculos virtuales (01/11 i): comisura izq / der, ceja interna izq / der (99 = no hay)
    // + (04/11) uint4 gestos esculpidos: x primer float4 de la malla en el buffer de formas (t118), y vertices, z primer indice (vmin), w formas (0 = no hay)
    UINT v[28] = { head, maxIdx, id, mouth, 99, 99, 99, 99, jawFull, jawHalf, lipTop, lipUp, 0, 0, 0, 0, 0, 0, 0, 0, 99, 99, 99, 99, 0, 0, 0, 0 };
    if (shape) for (int i = 0; i < 4; i++) v[24 + i] = shape[i];
    if (lids) for (int i = 0; i < 4; i++) v[4 + i] = lids[i] >= 0 ? (UINT)lids[i] : 99u;
    if (eyes) memcpy(v + 12, eyes, 32);
    if (mus) for (int i = 0; i < 4; i++) v[20 + i] = mus[i] >= 0 ? (UINT)mus[i] : 99u;
    D3D11_BUFFER_DESC d{}; d.ByteWidth = 112; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA s{}; s.pSysMem = v;
    ID3D11Buffer* b = nullptr; return SUCCEEDED(g_dev.dev->CreateBuffer(&d, &s, &b)) ? b : nullptr;
}
ID3D11Buffer* Staging(UINT bytes) {
    D3D11_BUFFER_DESC d{}; d.ByteWidth = (bytes + 15) & ~15u; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* b = nullptr; return SUCCEEDED(g_dev.dev->CreateBuffer(&d, nullptr, &b)) ? b : nullptr;
}
void FreeMesh(FaceMesh* m);   // abajo
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

// (04/11, Agustin: gestos "como si fueran esculpidos para messi") VOLCADO de las mallas de la cara tal como las carga el juego
// (la del Dream Patch, no la de Konami): con w354 = 1 (sube de 0 a 1) se vuelven a leer todas las mallas que se dibujen en los
// proximos ~10 s y cada malla de cara (piel, pestanas; no la boca ni los ojos) se escribe en PES21FX\dump\caras\ como OBJ
// (posicion en reposo, en metros, uv, caras) + un .txt con la huella (para reconocerla despues) y huesos / pesos por vertice.
// Sirve para esculpir en Blender las formas de cada gesto (blendshapes) sobre la malla exacta.
std::atomic<UINT> s_dumpUntil{0};
void FreeMesh(FaceMesh* m) {   // (04/11 b) todo lo de una malla que sale de la tabla
    ReleaseIA(m); if (m->cb) m->cb->Release();
    if (m->seamSrv) m->seamSrv->Release(); if (m->seamBuf) m->seamBuf->Release();
    delete m;
}
// (04/11 b, Agustin: "las caras se dividen en partes para que las expresiones funcionen y van por capas") PESOS DE BORDE para la
// teselacion (pes_face_tess.hlsli): 0 en los vertices del borde de la pieza (arista con un solo triangulo: ahi empieza otra pieza
// o termina la malla) y en los duplicados por posicion (costuras duras / de UV: el otro vertice tiene otra normal y el borde
// curvado no coincidiria), 1 en el resto. Con 0 en los dos extremos la arista queda recta en las dos piezas y siguen encajando.
void SeamWeights(const std::vector<uint32_t>& tri, UINT vmin, UINT nv, const std::vector<float>& ps, std::vector<float>& w, int* nSeam) {
    w.assign(nv, 1.0f);
    std::unordered_map<uint64_t, int> edges; edges.reserve(tri.size());
    for (size_t t = 0; t + 2 < tri.size(); t += 3) {
        const uint32_t a = tri[t] - vmin, b = tri[t + 1] - vmin, c = tri[t + 2] - vmin;
        if (a >= nv || b >= nv || c >= nv || a == b || b == c || a == c) continue;
        auto E = [&](uint32_t x, uint32_t y) { if (x > y) std::swap(x, y); edges[((uint64_t)x << 32) | y]++; };
        E(a, b); E(b, c); E(c, a);
    }
    for (auto& kv : edges) if (kv.second == 1) { w[(UINT)(kv.first >> 32)] = 0; w[(UINT)(kv.first & 0xffffffffu)] = 0; }
    std::unordered_map<uint64_t, uint32_t> pos; pos.reserve(nv);
    for (UINT v = 0; v < nv; v++) {   // posicion a 0.05 mm
        const float* P = &ps[(size_t)v * 3];
        const int64_t qx = (int64_t)llroundf(P[0] * 20000.0f), qy = (int64_t)llroundf(P[1] * 20000.0f), qz = (int64_t)llroundf(P[2] * 20000.0f);
        uint64_t h = 0xcbf29ce484222325ULL; h = Fnv(h, (uint64_t)qx); h = Fnv(h, (uint64_t)qy); h = Fnv(h, (uint64_t)qz);
        auto it = pos.find(h);
        if (it == pos.end()) pos[h] = v; else { w[v] = 0; w[it->second] = 0; }
    }
    int n = 0; for (float f : w) if (f == 0) n++;
    if (nSeam) *nSeam = n;
}
uint32_t Huella(const FaceMesh* m, const std::vector<float>& ps) {   // la del volcado: vertices, indices y posiciones a 0.1 mm
    uint64_t h = 0xcbf29ce484222325ULL; h = Fnv(h, m->nv); h = Fnv(h, m->count);
    for (UINT v = 0; v < m->nv; v++) for (int k = 0; k < 3; k++) h = Fnv(h, (uint64_t)(int64_t)llround(ps[(size_t)v * 3 + k] * 10000.0));
    return (uint32_t)(h ^ (h >> 32));
}
// ---- (04/11, Agustin: gestos "como si fueran esculpidos para Messi"; eligio "entre los dos": formas por codigo + retoque en Blender)
// GESTOS ESCULPIDOS POR VERTICE: PES21FX\formas\<HUELLA>.bin (tools/blendshapes/pack_from_blend.py) = para cada vertice de esa
// malla 5 corrimientos (sonrisa, grito, enojo, asombro, puchero; metros, espacio del modelo) que el VS de la cara suma ANTES del
// skinning por el slider de cada gesto (filas 355/356, con w356 "Perfiles por cara"). Se suman a los huesos del mod (correctivos).
// Todas las formas cargadas van juntas en un Buffer<float4> (t118 del VS); el cbuffer de la malla dice donde empieza la suya.
struct ShapePack { bool tried = false; int off = -1; UINT nv = 0; ID3D11ShaderResourceView* wr = nullptr; };   // wr: arrugas de la sonrisa (PS t31)
std::map<uint32_t, ID3D11ShaderResourceView*> s_shapeWr;   // por grupo (formas\\grupo_<n>_sonrisa.dds); nunca se liberan
std::unordered_map<uint32_t, ShapePack> s_shapes;
std::vector<float> s_shapeData;
ID3D11Buffer* s_shapeBuf = nullptr; ID3D11ShaderResourceView* s_shapeSrv = nullptr;
constexpr UINT kShapeN = 5;
int ShapeFind(uint32_t hu, UINT nv) {   // con s_mx tomado
    ShapePack& sp = s_shapes[hu];
    if (sp.tried) return sp.nv == nv ? sp.off : -1;
    // (las arrugas de la malla quedan en sp.wr; ExprShapeWr las da)
    sp.tried = true;
    wchar_t nm[64]; swprintf(nm, 64, L"formas\\%08X.bin", hu);
    FILE* f = _wfopen((g_cfg.baseDir + nm).c_str(), L"rb");
    if (!f) return -1;
    char mg[4] = {}; uint32_t hd[4] = {};
    const bool okh = fread(mg, 1, 4, f) == 4 && fread(hd, 4, 4, f) == 4 && !memcmp(mg, "PFXF", 4) && hd[0] == 1 && hd[2] == kShapeN;
    if (!okh || hd[1] != nv) { fclose(f); Log("expresiones: formas %08X.bin no valido (%s, %u vertices, la malla tiene %u)", hu, okh ? "otra malla" : "cabecera", okh ? hd[1] : 0u, nv); return -1; }
    std::vector<float> d((size_t)nv * kShapeN * 4);
    const bool okd = fread(d.data(), 4, d.size(), f) == d.size(); fclose(f);
    if (!okd) { Log("expresiones: formas %08X.bin cortado", hu); return -1; }
    float mx[kShapeN] = {};
    for (UINT v = 0; v < nv; v++) for (UINT k = 0; k < kShapeN; k++) {
        float* q = &d[((size_t)v * kShapeN + k) * 4];
        for (int c = 0; c < 3; c++) q[c] = std::clamp(std::isfinite(q[c]) ? q[c] : 0.0f, -0.02f, 0.02f);   // tope 2 cm por vertice
        q[3] = 0; mx[k] = (std::max)(mx[k], sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]));
    }
    const size_t off4 = s_shapeData.size() / 4;
    s_shapeData.insert(s_shapeData.end(), d.begin(), d.end());
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = (UINT)(s_shapeData.size() * 4); bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = s_shapeData.data();
    ID3D11Buffer* b = nullptr; ID3D11ShaderResourceView* v = nullptr;
    if (SUCCEEDED(g_dev.dev->CreateBuffer(&bd, &sd, &b))) {
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{}; vd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        vd.Buffer.FirstElement = 0; vd.Buffer.NumElements = (UINT)(s_shapeData.size() / 4);
        if (FAILED(g_dev.dev->CreateShaderResourceView(b, &vd, &v))) { b->Release(); b = nullptr; }
    }
    if (!v) { s_shapeData.resize(off4 * 4); Log("expresiones: formas %08X: no se pudo crear el buffer", hu); return -1; }
    // la vista vieja NO se libera: un dibujo de otro hilo puede tenerla en la mano entre ExprBufferForDraw y VSSetShaderResources
    // (solo pasa al cargar un paquete nuevo: unos pocos KB)
    s_shapeBuf = b; s_shapeSrv = v;
    sp.off = (int)off4; sp.nv = nv;
    if (hd[3]) {   // (04/11) arrugas esculpidas de la sonrisa de esta cara: normal map en las uv de la cara (wrinkle_dds.py)
        auto it = s_shapeWr.find(hd[3]);
        if (it == s_shapeWr.end()) {
            wchar_t wn[80]; swprintf(wn, 80, L"formas\\grupo_%u_sonrisa.dds", hd[3]);
            ID3D11ShaderResourceView* w = nullptr;
            if (GetFileAttributesW((g_cfg.baseDir + wn).c_str()) != INVALID_FILE_ATTRIBUTES && FAILED(DirectX::CreateDDSTextureFromFile(g_dev.dev, (g_cfg.baseDir + wn).c_str(), nullptr, &w))) w = nullptr;
            Log("expresiones: arrugas de la sonrisa %ls: %s", wn, w ? "cargadas" : "no estan o no se pudieron leer");
            it = s_shapeWr.emplace(hd[3], w).first;
        }
        sp.wr = it->second;
    }
    Log("expresiones: formas %08X.bin cargadas (%u vertices; maximo mm: sonrisa %.1f grito %.1f enojo %.1f asombro %.1f puchero %.1f)", hu, nv, mx[0] * 1000, mx[1] * 1000, mx[2] * 1000, mx[3] * 1000, mx[4] * 1000);
    return sp.off;
}
void DumpObj(FaceMesh* m, const std::vector<float>& ps, const std::vector<float>& uvs, const std::vector<uint32_t>& iv, const std::vector<float>& wv, float ylo, float yhi) {
    const std::wstring dir = g_cfg.baseDir + L"dump\\caras\\";
    CreateDirectoryW((g_cfg.baseDir + L"dump").c_str(), nullptr); CreateDirectoryW(dir.c_str(), nullptr);
    double sx = 0, sy = 0, sz = 0; for (UINT v = 0; v < m->nv; v++) { sx += ps[(size_t)v * 3]; sy += ps[(size_t)v * 3 + 1]; sz += ps[(size_t)v * 3 + 2]; }
    uint64_t h = 0xcbf29ce484222325ULL; h = Fnv(h, m->nv); h = Fnv(h, m->count);
    for (UINT v = 0; v < m->nv; v++) for (int k = 0; k < 3; k++) h = Fnv(h, (uint64_t)(int64_t)llround(ps[(size_t)v * 3 + k] * 10000.0));   // a 0.1 mm
    wchar_t name[160]; swprintf(name, 160, L"cara_%uv_%ut_%08X", m->nv, m->count / 3, (unsigned)(h ^ (h >> 32)));
    FILE* f = _wfopen((dir + name + L".obj").c_str(), L"wb");
    if (!f) { Log("expresiones: volcado: no se pudo escribir %ls.obj", name); return; }
    fprintf(f, "# PES21 FX: malla de cara %d tal como la carga el juego (reposo, metros). %u vertices, %u triangulos, alto %.3f..%.3f, cabeza en la paleta %d\n", m->id, m->nv, m->count / 3, ylo, yhi, m->head);
    fprintf(f, "o %ls\n", name);
    for (UINT v = 0; v < m->nv; v++) fprintf(f, "v %.6f %.6f %.6f\n", ps[(size_t)v * 3], ps[(size_t)v * 3 + 1], ps[(size_t)v * 3 + 2]);
    const bool hasUv = uvs.size() == (size_t)m->nv * 2;
    if (hasUv) for (UINT v = 0; v < m->nv; v++) fprintf(f, "vt %.6f %.6f\n", uvs[(size_t)v * 2], 1.0f - uvs[(size_t)v * 2 + 1]);
    for (size_t i = 0; i + 2 < m->tri.size(); i += 3) {
        const UINT a = m->tri[i] - m->vmin + 1, b = m->tri[i + 1] - m->vmin + 1, c = m->tri[i + 2] - m->vmin + 1;
        if (hasUv) fprintf(f, "f %u/%u %u/%u %u/%u\n", a, a, b, b, c, c); else fprintf(f, "f %u %u %u\n", a, b, c);
    }
    fclose(f);
    if (FILE* t = _wfopen((dir + name + L".txt").c_str(), L"wb")) {
        fprintf(t, "huella %08X vertices %u indices %u suma %.6f %.6f %.6f alto %.4f %.4f cabeza %d\n", (unsigned)(h ^ (h >> 32)), m->nv, m->count, sx, sy, sz, ylo, yhi, m->head);
        fprintf(t, "# vertice: hueso:peso x4 (indice en la paleta de 32 del dibujo)\n");
        for (UINT v = 0; v < m->nv; v++) {
            fprintf(t, "%u", v);
            for (int k = 0; k < 4; k++) fprintf(t, " %u:%.3f", iv[(size_t)v * 4 + k], wv[(size_t)v * 4 + k]);
            fprintf(t, "\n");
        }
        fclose(t);
    }
    Log("expresiones: volcado %ls.obj (malla %d, %u vertices, %u triangulos, alto %.3f..%.3f, uv %s, huella %08X)", name, m->id, m->nv, m->count / 3, ylo, yhi, hasUv ? "si" : "no", (unsigned)(h ^ (h >> 32)));
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
        {   // (04/11 b) los indices se guardan siempre (pesos de borde para la teselacion; antes solo con el volcado)
            m->tri.resize(m->count); for (UINT i = 0; i < m->count; i++) m->tri[i] = isz == 2 ? ((const uint16_t*)mp.pData)[i] : ((const uint32_t*)mp.pData)[i]; }
        imm->Unmap(m->stIB, 0); m->stIB->Release(); m->stIB = nullptr;
        if (hi < lo) { Reject(m, "sin indices"); return; }
        m->vmin = lo; m->nv = hi - lo + 1;
        if (m->nv > 65536) { Reject(m, "demasiados vertices"); return; }
        for (int s : { m->pos.slot, m->idx.slot, m->wgt.slot, m->dump ? m->uv.slot : -1 }) {
            if (s < 0 || !m->vb[s] || m->stVB[s]) continue;
            D3D11_BUFFER_DESC d; m->vb[s]->GetDesc(&d);
            UINT used = 1;
            if (m->pos.slot == s) used = (std::max)(used, m->pos.off + FmtBytes(m->pos.fmt));
            if (m->idx.slot == s) used = (std::max)(used, m->idx.off + FmtBytes(m->idx.fmt));
            if (m->wgt.slot == s) used = (std::max)(used, m->wgt.off + FmtBytes(m->wgt.fmt));
            if (m->dump && m->uv.slot == s) used = (std::max)(used, m->uv.off + FmtBytes(m->uv.fmt));
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
        float ylo = 1e9f, yhi = -1e9f, xlo = 1e9f, xhi = -1e9f;
        for (UINT v = 0; v < m->nv && ok; v++) {
            float P[3]; uint32_t* I = &iv[(size_t)v * 4]; float* W = &wv[(size_t)v * 4];
            const uint8_t* pp = (const uint8_t*)mp[m->pos.slot].pData + (size_t)v * m->stride[m->pos.slot] + m->pos.off;
            const uint8_t* pi = (const uint8_t*)mp[m->idx.slot].pData + (size_t)v * m->stride[m->idx.slot] + m->idx.off;
            const uint8_t* pw = (const uint8_t*)mp[m->wgt.slot].pData + (size_t)v * m->stride[m->wgt.slot] + m->wgt.off;
            ok = ReadPos(pp, m->pos.fmt, P) && ReadIdx(pi, m->idx.fmt, I) && ReadW(pw, m->wgt.fmt, W);
            if (!ok) break;
            xs[v] = P[0]; ys[v] = P[1]; zs[v] = P[2]; ylo = (std::min)(ylo, P[1]); yhi = (std::max)(yhi, P[1]); xlo = (std::min)(xlo, P[0]); xhi = (std::max)(xhi, P[0]); memcpy(&ps[(size_t)v * 3], P, 12);
            for (int k = 0; k < 4; k++) if (W[k] > 0.004f && (int)I[k] > mx) mx = (int)I[k];
        }
        // sk_head: el hueso con mas peso en el 25 % mas alto de la malla; uso total por hueso para el log
        float top[32] = {}, all[32] = {}, cx[32] = {}, cy[32] = {}, cz[32] = {}, wmax[32] = {}; int cnt[32] = {};
        float px[32] = {}, py[32] = {}, pz[32] = {};   // (01/11) vertice de mas peso de cada hueso (pico: cerca del hueso, no depende de que parte de la cara trae la malla)
        if (ok && mx >= 0 && mx <= 31) {
            const float cut = yhi - 0.25f * (yhi - ylo);
            for (UINT v = 0; v < m->nv; v++) for (int k = 0; k < 4; k++) {
                const uint32_t b = iv[(size_t)v * 4 + k]; const float w = wv[(size_t)v * 4 + k];
                if (b > 31 || w <= 0.004f) continue;
                all[b] += w; cnt[b]++; if (ys[v] >= cut) top[b] += w;
                cx[b] += w * xs[v]; cy[b] += w * ys[v]; cz[b] += w * zs[v];
                if (w > wmax[b]) { wmax[b] = w; px[b] = xs[v]; py[b] = ys[v]; pz[b] = zs[v]; }
            }
        }
        std::vector<float> uvs;   // (04/11) volcado: uv de cada vertice
        if (ok && m->dump && m->uv.slot >= 0 && mapped[m->uv.slot]) {
            uvs.resize((size_t)m->nv * 2);
            for (UINT v = 0; v < m->nv; v++) ReadUV((const uint8_t*)mp[m->uv.slot].pData + (size_t)v * m->stride[m->uv.slot] + m->uv.off, m->uv.fmt, &uvs[(size_t)v * 2]);
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
                // (02/11 j, Agustin: "la animacion de parpadear sigue rota"; lectura de la paleta: en la malla de 704 v el "parpado de
                // arriba" giraba igual que los de ABAJO de las otras) con un solo candidato por lado se tomaba siempre como el de
                // arriba: en las mallas que solo traen el parpado de abajo, el parpadeo lo cerraba hacia arriba. Ahora cada uno va
                // por la altura de sus vertices respecto del centro del ojo (y 1.6965: arriba > , abajo <)
                const float kEyeY = 1.6965f;
                int hi = -1, lo = -1;
                for (int i = 0; i < nc[sd]; i++) {
                    const int b = cand[sd][i]; const float y = cy[b] / all[b];
                    if (y >= kEyeY) { if (hi < 0 || y > cy[hi] / all[hi]) hi = b; }
                    else { if (lo < 0 || y < cy[lo] / all[lo]) lo = b; }
                }
                lids[sd] = hi; lids[2 + sd] = lo;
            }
        }
        bool mouth = hd <= 31 && !thin && ylo > 1.58f && yhi < 1.68f && (yhi - ylo) < 0.06f;
        // (01/11 h) GLOBO DEL OJO (eye.fmdl comun, VS 69758ab9: 386 v, paleta de 2: [0] skf_eye_l, [1] skf_eye_r, peso 1, alto
        // 1.683..1.711): no es cara para el amplificador (cabeza 99); para la mirada (pes_expr.hlsli PesGazeEye) se ajusta una
        // esfera por hueso (minimos cuadrados: |p|^2 = 2 c.p + k) y se pasan centro y radio en el cbuffer (b12 w = 2).
        float eyes[8] = {}; bool eye = false;
        if (mx <= 1 && ylo > 1.67f && yhi < 1.72f && (yhi - ylo) < 0.04f && m->nv >= 60) {
            eye = true;
            for (int b = 0; b <= mx; b++) {
                double A[4][4] = {}, B[4] = {}; int n = 0;
                for (UINT v = 0; v < m->nv; v++) {
                    float w = 0; for (int k = 0; k < 4; k++) if ((int)iv[(size_t)v * 4 + k] == b) w += wv[(size_t)v * 4 + k];
                    if (w < 0.5f) continue;
                    const double r[4] = { 2.0 * xs[v], 2.0 * ys[v], 2.0 * zs[v], 1.0 }, y = (double)xs[v] * xs[v] + (double)ys[v] * ys[v] + (double)zs[v] * zs[v];
                    for (int i = 0; i < 4; i++) { for (int j = 0; j < 4; j++) A[i][j] += r[i] * r[j]; B[i] += r[i] * y; } n++;
                }
                if (n < 12) { eye = false; break; }
                // Gauss con pivote
                double M[4][5]; for (int i = 0; i < 4; i++) { for (int j = 0; j < 4; j++) M[i][j] = A[i][j]; M[i][4] = B[i]; }
                bool sing = false;
                for (int i = 0; i < 4 && !sing; i++) {
                    int p = i; for (int r2 = i + 1; r2 < 4; r2++) if (fabs(M[r2][i]) > fabs(M[p][i])) p = r2;
                    if (fabs(M[p][i]) < 1e-12) { sing = true; break; }
                    if (p != i) for (int j = 0; j < 5; j++) std::swap(M[i][j], M[p][j]);
                    for (int r2 = 0; r2 < 4; r2++) if (r2 != i) { const double f = M[r2][i] / M[i][i]; for (int j = i; j < 5; j++) M[r2][j] -= f * M[i][j]; }
                }
                if (sing) { eye = false; break; }
                const double cx0 = M[0][4] / M[0][0], cy0 = M[1][4] / M[1][1], cz0 = M[2][4] / M[2][2], k = M[3][4] / M[3][3];
                const double rr = k + cx0 * cx0 + cy0 * cy0 + cz0 * cz0;
                if (rr <= 1e-6 || rr > 0.0004) { eye = false; break; }   // radio 1 mm .. 2 cm
                eyes[b * 4 + 0] = (float)cx0; eyes[b * 4 + 1] = (float)cy0; eyes[b * 4 + 2] = (float)cz0; eyes[b * 4 + 3] = (float)sqrt(rr);
            }
            if (eye) { hd = 99; mouth = false; for (int i = 0; i < 4; i++) lids[i] = -1; }   // el otro ojo no es un parpado
        }
        // (01/11) abrir la boca (pes_expr.hlsli PesJawApply, fila 347): huesos de abajo = centro de sus vertices debajo de la
        // linea de los labios (y 1.560..1.6275, z > 0.07, |x| < 0.05: skf_jaw, skf_lip_b_*, skf_doublechin; face_rig 111320:
        // labio de abajo 1.622..1.624, de arriba 1.634..1.635); comisuras (skf_lip_s, y 1.6275..1.6325, |x| > 0.015) a la mitad.
        // En la boca 3D la mandibula (lengua + dientes de abajo) es el hueso con mas peso fuera de la cabeza con peso 1.
        // (01/11) nariz con la boca (fila 347.y): labio de arriba del medio (skf_lip_t_c, y 1.630..1.642, |x| < 0.008, z > 0.15)
        // (01/11) labio de arriba (fila 347.z, Agustin: "los labios superiores no tienen mucho movimiento"): skf_lip_t_* y
        // skf_lip_volume (centro y 1.6325..1.645, |x| < 0.03, z > 0.14)
        UINT jawFull = 0, jawHalf = 0, lipTop = 99, lipUp = 0;
        // (01/11 i) musculos virtuales (pes_expr.hlsli PesMuscleApply, fila 348): comisuras (skf_lip_s_*, las de "mitad") y cejas
        // internas (skf_brow_i_*: centro y > 1.706, |x| 0.5..2.8 cm, z > 0.14, peso >= 0.6; la mas cercana al medio por lado)
        int mus[4] = { -1, -1, -1, -1 };
        if (hd <= 31 && !thin) {
            int jb = -1;
            for (int b = 0; b <= mx; b++) {
                if (b == hd || all[b] <= 0.5f) continue;
                if (hd <= 2 ? b < hd : b + 1 == hd) continue;   // cuello / pecho (como PesExprSkip)
                const float x = cx[b] / all[b], y = cy[b] / all[b], z = cz[b] / all[b];
                if (mouth && wmax[b] >= 0.9f && (jb < 0 || all[b] > all[jb])) jb = b;
                if (!mouth && fabsf(x) < 0.008f && y >= 1.630f && y < 1.642f && z > 0.15f && wmax[b] >= 0.5f && (lipTop > 31 || all[b] > all[lipTop])) lipTop = (UINT)b;
                if (!mouth && fabsf(x) < 0.03f && y >= 1.6325f && y < 1.645f && z > 0.14f && wmax[b] >= 0.4f) lipUp |= 1u << b;
                if (z < 0.07f || fabsf(x) > 0.055f) continue;
                // (01/11 l) los huesos laterales (skf_cheek_s_*, |x| > 3.5 cm) van a la mitad en los dos lados: antes uno entraba
                // entero y el otro no por 1 mm de centro (1E018004: bit 15 si, 16 no) y la mandibula abria torcida (corte en el menton)
                if (fabsf(x) > 0.035f) { if (y >= 1.560f && y < 1.645f) jawHalf |= 1u << b; continue; }
                if (y >= 1.560f && y < 1.6275f) jawFull |= 1u << b;
                else if (y >= 1.6275f && y < 1.6325f && fabsf(x) > 0.015f) { jawHalf |= 1u << b; const int sd = x > 0 ? 0 : 1; if (mus[sd] < 0 || all[b] > all[mus[sd]]) mus[sd] = b; }
            }
            if (jb >= 0) { jawFull |= 1u << jb; jawHalf &= ~(1u << jb); }
            if (!mouth) for (int b = 0; b <= mx; b++) {
                if (b == hd || all[b] <= 0.5f || wmax[b] < 0.6f) continue;
                const float x = cx[b] / all[b], y = cy[b] / all[b], z = cz[b] / all[b];
                if (y <= 1.706f || fabsf(x) < 0.005f || fabsf(x) > 0.028f || z < 0.14f) continue;
                const int sd = 2 + (x > 0 ? 0 : 1);
                if (mus[sd] < 0 || fabsf(x) < fabsf(cx[mus[sd]] / all[mus[sd]])) mus[sd] = b;
            }
            // (01/11 g) el medio de los labios: el arranque de 6637155 dejo skf_lip_b_c (paleta 28 de 7073 v) fuera de la mascara
            // (su centro cae a la altura de la comisura, no debajo). Entre los huesos del medio (|x| < 8 mm, z > 0.14, y 1.615..1.645,
            // peso >= 0.5) el mas bajo es el labio de abajo (mandibula) y el mas alto el de arriba.
            if (!mouth) {
                int lo = -1, hi = -1;
                for (int b = 0; b <= mx; b++) {
                    if (b == hd || all[b] <= 0.5f || wmax[b] < 0.5f) continue;
                    const float x = cx[b] / all[b], y = cy[b] / all[b], z = cz[b] / all[b];
                    if (fabsf(x) >= 0.008f || z <= 0.14f || y < 1.615f || y >= 1.645f) continue;
                    if (lo < 0 || y < cy[lo] / all[lo]) lo = b;
                    if (hi < 0 || y > cy[hi] / all[hi]) hi = b;
                }
                if (lo >= 0 && hi >= 0 && lo != hi && cy[hi] / all[hi] - cy[lo] / all[lo] > 0.002f) {
                    jawFull |= 1u << lo; jawHalf &= ~(1u << lo); lipUp &= ~(1u << lo);
                    if (lipTop == (UINT)lo || lipTop > 31) lipTop = (UINT)hi;
                    lipUp |= 1u << hi; jawFull &= ~(1u << hi); jawHalf &= ~(1u << hi);
                }
            }
        }
        // (01/11 l, Agustin: "algunas caras se cortan en la nariz y cerca de la barbilla, en caras convertidas") los efectos que
        // mueven la piel por zona (abrir la boca, labio de arriba, nariz, musculos) solo valen en una malla que es la cara ENTERA
        // (alto >= 15 cm, ancho >= 12 cm, >= 1500 vertices): en una cara partida en piezas (nariz o menton aparte) cada pieza
        // tendria otros huesos marcados y se abre un corte entre ellas. La boca 3D (lengua) y el parpadeo no entran aqui.
        const bool whole = !mouth && !eye && (yhi - ylo) >= 0.15f && (xhi - xlo) >= 0.12f && m->nv >= 1500;
        if (!whole) { jawFull = 0; jawHalf = 0; lipTop = 99; lipUp = 0; for (int i = 0; i < 4; i++) mus[i] = -1; }
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
        m->huella = Huella(m, ps);
        m->shapeOff = (hd <= 31 && !eye && !mouth) ? ShapeFind(m->huella, m->nv) : -1;
        if (m->shapeOff >= 0) m->shapeWr = s_shapes[m->huella].wr;
        const UINT shp[4] = { m->shapeOff >= 0 ? (UINT)m->shapeOff : 0u, m->nv, m->vmin, m->shapeOff >= 0 ? kShapeN : 0u };
        m->cb = MakeCB((UINT)m->head, (UINT)mx, (UINT)m->id, eye ? 2u : (mouth ? 1u : 0u), lids, jawFull, jawHalf, lipTop, lipUp, eye ? eyes : nullptr, mus, shp);
        for (int i = 0; i < 4; i++) m->lids[i] = lids[i];
        if (m->dump && hd <= 31 && !eye && !mouth && m->nv >= 200) DumpObj(m, ps, uvs, iv, wv, ylo, yhi);
        // (04/11 b) teselacion de la cara (facetess.cpp): solo la piel de la cara (no boca, no ojo, no pestanas / parpados finos,
        // no piezas chicas): pesos de borde por vertice en un Buffer<float> (t119 del VS)
        if (hd <= 31 && !eye && !mouth && !thin && m->nv >= 200 && g_dev.dev) {
            std::vector<float> w; SeamWeights(m->tri, m->vmin, m->nv, ps, w, &m->seamN);
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth = (UINT)(w.size() * 4); bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = w.data();
            if (SUCCEEDED(g_dev.dev->CreateBuffer(&bd, &sd, &m->seamBuf))) {
                D3D11_SHADER_RESOURCE_VIEW_DESC vd{}; vd.Format = DXGI_FORMAT_R32_FLOAT; vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; vd.Buffer.FirstElement = 0; vd.Buffer.NumElements = (UINT)w.size();
                if (FAILED(g_dev.dev->CreateShaderResourceView(m->seamBuf, &vd, &m->seamSrv))) { m->seamBuf->Release(); m->seamBuf = nullptr; m->seamSrv = nullptr; }
            } else m->seamBuf = nullptr;
        }
        m->tri.clear(); m->tri.shrink_to_fit();
        ReleaseIA(m);
        if (!m->cb) { m->state = 4; m->why = "sin cbuffer"; s_hist[3]++; return; }
        m->state = 3; s_hist[m->head <= 1 ? m->head : m->head > 31 ? 3 : 2]++;
        static int n = 0;
        if (n++ < 80) {   // uso de cada hueso de la paleta (peso total / peso en la parte alta) para comparar con face_rig.py
            char l[1400]; int k = snprintf(l, sizeof l, "expresiones: malla de cara %d (%u indices, %u vertices, alto %.3f..%.3f m): hueso maximo %d -> cabeza en la paleta %d%s, parpados %d %d / %d %d, boca abajo %08X mitad %08X labio %u arriba %08X comisuras %d %d cejas %d %d%s%s |",
                                           m->id, m->count, m->nv, ylo, yhi, mx, m->head, m->head > 31 ? " (no es cara)" : thin ? " (fina: por vertices)" : mouth ? " (boca: lengua)" : "", lids[0], lids[1], lids[2], lids[3], jawFull, jawHalf, lipTop, lipUp, mus[0], mus[1], mus[2], mus[3], eye ? " (OJO)" : "", (!whole && !mouth && !eye && hd <= 31) ? " (pieza: sin boca/nariz/musculos)" : "");
            if (m->seamSrv) k += snprintf(l + k, sizeof l - k, " teselable (%d vertices de borde)", m->seamN);
            if (eye) k += snprintf(l + k, sizeof l - k, " ojos: izq (%.4f %.4f %.4f) r %.4f, der (%.4f %.4f %.4f) r %.4f", eyes[0], eyes[1], eyes[2], eyes[3], eyes[4], eyes[5], eyes[6], eyes[7]);
            for (int b = 0; b <= mx && k < (int)sizeof l - 24; b++) if (all[b] > 0) k += snprintf(l + k, sizeof l - k, " %d:%.0f/%.0f", b, all[b], top[b]);
            Log("%s", l);
            if (!thin && !mouth && !eye && hd <= 31 && m->nv >= 1000) {   // (01/11) picos por hueso, para reconocer cada hueso por su posicion estandar (face_rig)
                k = snprintf(l, sizeof l, "expresiones: malla %d picos (hueso: x y z peso | centro x y z):", m->id);
                for (int b = 0; b <= mx && k < (int)sizeof l - 80; b++) if (all[b] > 0)
                    k += snprintf(l + k, sizeof l - k, " %d:(%.3f %.3f %.3f %.2f|%.3f %.3f %.3f)", b, px[b], py[b], pz[b], wmax[b], cx[b] / all[b], cy[b] / all[b], cz[b] / all[b]);
                Log("%s", l);
            }
        }
    }
}
}   // namespace

// en cada dibujo con la regla "expr = <slot>" (VS de la cabeza / cara): cbuffer de la malla para el VS (null = no enlazar)
static void LidCapture(ID3D11DeviceContext* c, FaceMesh* m);   // (02/11 h) abajo
ID3D11Buffer* ExprBufferForDraw(ID3D11DeviceContext* c, UINT count, UINT start, INT base, const float** headCentroid, int* headIdx, ID3D11ShaderResourceView** shapeSrv, ID3D11ShaderResourceView** shapeWr, ID3D11ShaderResourceView** tessSeam) {
    if (headCentroid) *headCentroid = nullptr;
    if (tessSeam) *tessSeam = nullptr;
    if (shapeSrv) *shapeSrv = nullptr;
    if (shapeWr) *shapeWr = nullptr;
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
        rel(); FaceMesh* m = it->second; m->lastFrame = s_frame;
        if (m->state == 3 && m->cb) {
            s_binds++;
            if (shapeSrv && m->shapeOff >= 0 && s_shapeSrv) { *shapeSrv = s_shapeSrv; s_shapeDraws++; if (shapeWr) *shapeWr = m->shapeWr; }
            if (tessSeam && m->seamSrv) *tessSeam = m->seamSrv;   // (04/11 b) piel de la cara: se puede teselar
            if (m->head <= 31 && (m->lids[0] >= 0 || m->lids[1] >= 0) && s_frame - m->lidCap >= 120) { m->lidCap = s_frame; LidCapture(c, m); }
            if (headCentroid && m->hcOk && m->nv >= 300) { *headCentroid = m->hc; if (headIdx) *headIdx = m->head; } return m->cb; }
        if (m->state == 4) s_bindsDisc++; else s_bindsWait++;
        s_bindsNone++; return s_none;
    }
    if (s_meshes.size() >= 4096) {
        // (01/11) en partido aparecen mallas pasajeras (buffers que el juego rearma: ~50 por segundo) y la tabla se llenaba cada
        // ~40 s; vaciarla entera obligaba a releer las 22 caras cada vez (73 % de los dibujos 'en lectura'). Primero se echan
        // las que no se dibujaron en los ultimos 5 s; solo si no alcanza se empieza de nuevo.
        // (02/11) primero las de mas de 30 s sin dibujarse; si no alcanza, las de mas de 5 s (los LOD de las caras vuelven a aparecer
        // y releerlos costaba 'en lectura' 7.6 % en el partido de Villarreal)
        for (UINT age : { 1800u, 300u }) {
          if (s_meshes.size() < 3072) break;
          for (auto it2 = s_meshes.begin(); it2 != s_meshes.end();) {
            FaceMesh* o = it2->second;
            if (s_frame - o->lastFrame > age) {
                s_pending.erase(std::remove(s_pending.begin(), s_pending.end(), o), s_pending.end());
                FreeMesh(o); it2 = s_meshes.erase(it2); s_evicted++;
            } else ++it2;
          }
        }
        if (s_meshes.size() >= 4096) {   // partido largo / muchas caras: se empieza de nuevo (las cuentas y buffers cambian entre partidos)
            for (auto& kv : s_meshes) FreeMesh(kv.second);
            s_meshes.clear(); s_pending.clear();
        }
    }
    FaceMesh* m = new FaceMesh(); m->id = s_nextId++; m->lastFrame = s_frame;
    m->dump = s_frame < s_dumpUntil.load(std::memory_order_relaxed);
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
            else if (e.idx == 0 && !_stricmp(e.sem.c_str(), "TEXCOORD")) t = &m->uv;
            if (t && e.slot < 4) { t->slot = (int)e.slot; t->off = o; t->fmt = (DXGI_FORMAT)e.fmt; }
        }
        if (m->pos.slot < 0 || m->idx.slot < 0 || m->wgt.slot < 0) { m->state = 4; m->why = "sin POSITION / BLENDINDICES / BLENDWEIGHT"; }
        else if (!m->vb[m->pos.slot] || !m->vb[m->idx.slot] || !m->vb[m->wgt.slot]) { m->state = 4; m->why = "falta un buffer de vertices"; }
    }
    rel();
    s_meshes[key] = m;
    if (m->state == 0) s_pending.push_back(m); else { ReleaseIA(m); s_hist[3]++; }
    s_bindsNew++; s_bindsNone++; return s_none;
}

// ---- (02/11 h, Agustin: "los parpados no toman todos los modelos del rostro") LECTURA DE LA PALETA por malla: cada ~2 s se
// copia la paleta (b6) de cada malla con parpados y en el Present se mide cuanto esta girado cada parpado respecto de sk_head
// (grados alrededor del eje x de la cabeza, + = cierra el de arriba). Si las mallas de la misma cara dan angulos distintos en el
// mismo momento, el giro de los huesos no llega igual a todas (y se sabe cuales).
namespace {
struct LidCap { ID3D11Buffer* st = nullptr; int id = 0, nv = 0, head = 0, lids[4] = {}; UINT frame = 0; int tries = 0; };
std::mutex s_lmx; std::vector<LidCap> s_lcaps; int s_lidLogs = 0; double s_lidLogT = 0;
}
static void LidCapture(ID3D11DeviceContext* c, FaceMesh* m) {   // con s_mx tomado
    if (s_lidLogs >= 400) return;
    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) || !c1) return;
    ID3D11Buffer* b = nullptr; UINT first = 0, num = 0;
    c1->VSGetConstantBuffers1(6, 1, &b, &first, &num); c1->Release();
    if (!b) return;
    D3D11_BUFFER_DESC d; b->GetDesc(&d);
    const UINT a = first * 16, len = 32 * 48;
    if (a + len > d.ByteWidth) { b->Release(); return; }
    ID3D11Buffer* st = Staging(len);
    if (st) {
        D3D11_BOX bx = { a, 0, 0, a + len, 1, 1 }; c->CopySubresourceRegion(st, 0, 0, 0, 0, b, 0, &bx);
        LidCap L; L.st = st; L.id = m->id; L.nv = (int)m->nv; L.head = m->head; L.frame = s_frame; for (int i = 0; i < 4; i++) L.lids[i] = m->lids[i];
        std::lock_guard<std::mutex> lk(s_lmx); if (s_lcaps.size() < 64) s_lcaps.push_back(L); else st->Release();
    }
    b->Release();
}
static void LidPresent(ID3D11DeviceContext* imm) {
    std::lock_guard<std::mutex> lk(s_lmx);
    char line[1024]; int n = 0; line[0] = 0;
    for (size_t i = 0; i < s_lcaps.size();) {
        LidCap& L = s_lcaps[i];
        D3D11_MAPPED_SUBRESOURCE mp; HRESULT hr = imm->Map(L.st, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mp);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING && ++L.tries < 60) { i++; continue; }
        if (SUCCEEDED(hr)) {
            const float* r = (const float*)mp.pData;
            auto R = [&](int s, int k, int j) { return r[s * 12 + k * 4 + j]; };
            char part[160]; int pn = snprintf(part, sizeof part, " | malla %d (%d v, cuadro %u):", L.id, L.nv, L.frame);
            static const char* nm[4] = { "arriba izq", "arriba der", "abajo izq", "abajo der" };
            for (int q = 0; q < 4; q++) {
                const int s = L.lids[q]; if (s < 0 || s > 31) continue;
                float D[3][3];   // D = Rhead^T * Rlid
                for (int x = 0; x < 3; x++) for (int y = 0; y < 3; y++) { float v = 0; for (int k = 0; k < 3; k++) v += R(L.head, k, x) * R(s, k, y); D[x][y] = v; }
                const float ang = atan2f(D[2][1], D[1][1]) * 57.29578f;
                if (pn < (int)sizeof part - 24) pn += snprintf(part + pn, sizeof part - pn, " %s[%d] %.1f", nm[q], s, ang);
            }
            imm->Unmap(L.st, 0);
            if (n + pn < (int)sizeof line - 1) { memcpy(line + n, part, (size_t)pn + 1); n += pn; }
        }
        L.st->Release(); s_lcaps.erase(s_lcaps.begin() + i);
    }
    if (n > 0 && s_lidLogs < 400 && Now() - s_lidLogT > 1.5) { s_lidLogT = Now(); s_lidLogs++; Log("parpados por malla (giro leido de la paleta, grados; mismas caras deberian coincidir):%s", line); }
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
    LidPresent(imm);   // (02/11 h)
    ExprBonesPresent();   // (01/11) boca por huesos: estado para el shader y resumen
    {   // con el candado tomado: los dibujos (otros hilos) leen state / cb y pueden vaciar la tabla. Los Map son DO_NOT_WAIT.
        std::lock_guard<std::mutex> lk(s_mx);
        s_frame++;
        {   // (04/11) volcado de las mallas de la cara: w354 sube de 0 a 1 -> se vacia la tabla y las que se vuelvan a dibujar en ~10 s se vuelcan
            static bool was = false; const bool now = g_cfg.live[354][3] > 0.5f;
            if (now && !was) {
                for (auto& kv : s_meshes) FreeMesh(kv.second);
                s_meshes.clear(); s_pending.clear();
                s_dumpUntil.store(s_frame + 600, std::memory_order_relaxed);
                Log("expresiones: volcado de las mallas de la cara pedido (w354 = 1): se releen las que se dibujen en los proximos 10 s -> PES21FX\\dump\\caras\\");
            }
            was = now;
        }
        // (01/11) primero las mallas que se estan dibujando ahora (las pasajeras que ya no se dibujan van al final), 16 por cuadro
        std::stable_sort(s_pending.begin(), s_pending.end(), [](FaceMesh* a, FaceMesh* b) { return a->lastFrame > b->lastFrame; });
        for (size_t i = 0; i < s_pending.size() && i < 16; i++) Step(imm, s_pending[i]);
        s_pending.erase(std::remove_if(s_pending.begin(), s_pending.end(), [](FaceMesh* m) { return m->state >= 3; }), s_pending.end());
    }
    static int frames = 0;
    if (++frames >= 3600) {
        frames = 0;
        const int b = s_binds.exchange(0), bn = s_bindsNone.exchange(0);
        const int bw = s_bindsWait.exchange(0), bd = s_bindsDisc.exchange(0), bnew = s_bindsNew.exchange(0);
        if (b || bn) { std::lock_guard<std::mutex> lk(s_mx);
            Log("expresiones: %d mallas de cara (cabeza en 0: %d, en 1: %d, en otro: %d, descartadas o no cara %d); dibujos con dato %d, sin dato %d (en lectura %d, descartadas o no cara %d, nuevas %d, apagado %d); echadas por viejas %d; ganancia %.2f",
                (int)s_meshes.size(), s_hist[0], s_hist[1], s_hist[2], s_hist[3], b, bn, bw, bd, bnew, bn - bw - bd - bnew, s_evicted, g_cfg.live[340][1]);
            int np = 0; for (auto& kv : s_shapes) if (kv.second.off >= 0) np++;
            const int sd = s_shapeDraws.exchange(0);
            if (np || sd) Log("expresiones: gestos esculpidos: %d mallas con formas, %d dibujos (sonrisa %.2f grito %.2f enojo %.2f asombro %.2f puchero %.2f, perfiles %s)", np, sd,
                g_cfg.live[355][1], g_cfg.live[356][0], g_cfg.live[355][0], g_cfg.live[355][2], g_cfg.live[356][1], g_cfg.live[356][3] > 0.5f ? "si" : "no"); }
    }
}

}   // namespace fx
