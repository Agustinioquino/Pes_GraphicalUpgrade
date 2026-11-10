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
constexpr UINT kCbU = 36;   // (08/11) uints del cbuffer b12 (PESExprMesh): 28 + 8 (hueso de cada lugar de la paleta, 4 por uint)
struct FaceMesh {
    int id = 0, state = 0, tries = 0;   // 0 copiar indices, 1 leer indices / copiar vertices, 2 leer vertices, 3 listo, 4 descartada
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0, start = 0, count = 0; INT base = 0;
    ID3D11Buffer* vb[4] = {}; UINT stride[4] = {}, off[4] = {};
    Elem pos, idx, wgt, uv, nrm;   // (07/11) nrm: NORMAL0, para suavizar las costuras de textura (SeamWeights)
      // (04/11) uv: TEXCOORD0, solo para el volcado
    bool dump = false; std::vector<uint32_t> tri;   // (04/11) volcar esta malla a OBJ (w354 = 1) y sus indices (relativos a vmin)
    ID3D11Buffer* stIB = nullptr; ID3D11Buffer* stVB[4] = {};
    UINT vmin = 0, nv = 0;
    int maxIdx = -1, head = 99;
    ID3D11Buffer* cb = nullptr;   // x cabeza, y indice maximo, z id
    const char* why = "";
    float hc[3] = {}; bool hcOk = false;   // (1/11) centro de la cara en espacio del modelo (vertices que pesan en sk_head): para "seguir la cara"
    UINT lastFrame = 0;   // (01/11) ultimo cuadro en que se dibujo (para echar las mallas viejas sin vaciar toda la tabla)
    int lids[4] = { -1, -1, -1, -1 }; UINT lidCap = 0;   // (02/11 h) parpados en la paleta y ultimo cuadro en que se leyo la paleta
    // (08/11) gestos en la paleta: hueso kDef (exprbones.cpp) de cada lugar de la paleta de esta malla (255 = ninguno), leido una vez de
    // la paleta real (b6) y emparejado con los huesos del personaje; slotCap = ultimo cuadro en que se pidio la lectura
    uint8_t slotBone[32]; bool slotsOk = false; UINT slotCap = 0; int slotTries = 0;
    float slotC[32][4] = {};   // (08/11 f) centro de los vertices de cada lugar de la paleta (espacio del modelo, metros; w = suma de pesos): el emparejado mira la geometria
    uint8_t slotPend[32]; bool slotPendOk = false;   // (08/11 b) tabla de la lectura anterior: se acepta solo si dos lecturas seguidas dan la misma
    FaceMesh() { memset(slotBone, 255, sizeof slotBone); memset(slotPend, 255, sizeof slotPend); }
    int shapeOff = -1; uint32_t huella = 0; ID3D11ShaderResourceView* shapeWr = nullptr;   // (04/11) gestos esculpidos (formas\<huella>.bin): primer float4 de esta malla en s_shapeData
    ID3D11Buffer* seamBuf = nullptr; ID3D11ShaderResourceView* seamSrv = nullptr; int seamN = 0;   // (04/11 b) teselacion (facetess.cpp): peso de borde por vertice (t119), solo piel de la cara; seamN = vertices con peso 0
    int seamSoft = -1;   // (07/11) vertices de costuras de textura que ahora se redondean (-1 = sin normales leidas)
    int seamEye = 0;   // (04/11 h) vertices del borde de la abertura del ojo (peso 2: redondeo propio, fila 366)
    ID3D11Buffer* cbTess = nullptr;   // (08/11) cbuffer neutro (sin expresiones) para teselar con x340 = 0
    bool body = false;   // (04/11 g) piel del cuerpo (VS 1c02a92a): solo pesos de borde para la teselacion, sin nada de cara
    UINT cbv[kCbU] = {}; std::vector<float> xferPs; uint64_t xferIb = 0; int xferUsed = 0; bool xferGate = false; ID3D11Buffer* cbNoShape = nullptr; ID3D11Buffer* cbTeeth = nullptr;
    // (08/11, Agustin: "con esto puedes mejorar las arrugas tambien") PESOS DE ARRUGAS DEL JUEGO suavizados: cada cuadro se copia el
    // cbuffer de material del PS de esta piel (b4, m_materials[0].y / [3] / [4] = pesos por zona que manda el motor segun la pose), en el
    // Present se lee la copia lista (1-3 cuadros atras) y se la sigue con el mismo resorte que los huesos (fila 397.x); el resultado va al
    // PS en b12 (PESWrSmooth) y el shader lo mezcla por z397. Asi las arrugas no saltan a 30 cuadros cuando la cara ya va suave
    ID3D11Buffer* wrSt[3] = {}; int wrStFrame[3] = { -1, -1, -1 }; int wrStI = 0; UINT wrCapFrame = 0; int wrReadFrame = -1;
    float wrW[9] = {}, wrV[9] = {}; bool wrHave = false; double wrT = 0; ID3D11Buffer* wrCb = nullptr;
    bool beard = false, beardSrc = false; int geoId = 0, teethHits = 0; bool teethOn = false; ID3D11Buffer* idBuf = nullptr; std::vector<float> beardPs; int beardTries = 0; UINT seqAt = 0; std::vector<int> beardUsed; int beardRebinds = 0; bool beardOjo = false;   // (04/11 e) seqAt: orden del ultimo dibujo (barba: para elegir la piel dibujada junto a ella)   // (04/11 c) barba: malla del pelo enlazada a la piel / piel que le pasa su paleta   // (04/11 c) lo que lleva el cbuffer y, si espera formas pasadas de la piel, sus vertices
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
UINT s_drawSeq = 0, s_packSeq = 0, s_packFrame = 0xffffffffu; struct GateE { int id = 0; UINT seq = 0; }; GateE s_gateRing[16]; UINT s_gateAt = 0; GateE s_gateOk[32]; UINT s_gateOkAt = 0; int s_gateOn = 0, s_gateOff = 0, s_teethOn = 0;   // (04/11 d) mallas pegadas a una piel con formas
thread_local bool t_beardDraw = false;   // (04/11 c) el dibujo en curso es del VS del pelo (BeardForDraw)
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
ID3D11Buffer* MakeCB(UINT head, UINT maxIdx, UINT id, UINT mouth = 0, const int* lids = nullptr, UINT jawFull = 0, UINT jawHalf = 0, UINT lipTop = 99, UINT lipUp = 0, const float* eyes = nullptr, const int* mus = nullptr, const UINT* shape = nullptr, UINT* keep = nullptr) {
    if (!g_dev.dev) return nullptr;
    // 48 bytes de uint4 + 2 float4: centros de los ojos (x y z radio) izquierdo (+x, hueso 0) y derecho (hueso 1), para la mirada
    // + uint4 musculos virtuales (01/11 i): comisura izq / der, ceja interna izq / der (99 = no hay)
    // + (04/11) uint4 gestos esculpidos: x primer float4 de la malla en el buffer de formas (t118), y vertices, z primer indice (vmin), w formas (0 = no hay)
    UINT v[kCbU] = { head, maxIdx, id, mouth, 99, 99, 99, 99, jawFull, jawHalf, lipTop, lipUp, 0, 0, 0, 0, 0, 0, 0, 0, 99, 99, 99, 99, 0, 0, 0, 0 };
    for (UINT i = 28; i < kCbU; i++) v[i] = 0xFFFFFFFFu;   // (08/11) sin huesos emparejados todavia
    if (shape) for (int i = 0; i < 4; i++) v[24 + i] = shape[i];
    if (lids) for (int i = 0; i < 4; i++) v[4 + i] = lids[i] >= 0 ? (UINT)lids[i] : 99u;
    if (eyes) memcpy(v + 12, eyes, 32);
    if (mus) for (int i = 0; i < 4; i++) v[20 + i] = mus[i] >= 0 ? (UINT)mus[i] : 99u;
    if (keep) memcpy(keep, v, sizeof v);
    D3D11_BUFFER_DESC d{}; d.ByteWidth = kCbU * 4; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA s{}; s.pSysMem = v;
    ID3D11Buffer* b = nullptr; return SUCCEEDED(g_dev.dev->CreateBuffer(&d, &s, &b)) ? b : nullptr;
}
ID3D11Buffer* MakeCBRaw(const UINT* v) {
    if (!g_dev.dev) return nullptr;
    D3D11_BUFFER_DESC d{}; d.ByteWidth = kCbU * 4; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = v;
    ID3D11Buffer* b = nullptr; return SUCCEEDED(g_dev.dev->CreateBuffer(&d, &sd, &b)) ? b : nullptr;
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
    ReleaseIA(m); if (m->cb) m->cb->Release(); if (m->cbTess) m->cbTess->Release();
    if (m->seamSrv) m->seamSrv->Release(); if (m->seamBuf) m->seamBuf->Release();
    for (auto* b : m->wrSt) if (b) b->Release(); if (m->wrCb) m->wrCb->Release();   // (08/11) pesos de arrugas
    if (m->cbNoShape) m->cbNoShape->Release(); if (m->cbTeeth) m->cbTeeth->Release(); if (m->idBuf) m->idBuf->Release();   // (04/11 c) cbuffers sin formas / de los dientes y id de la barba
    delete m;
}
// (04/11 b, Agustin: "las caras se dividen en partes para que las expresiones funcionen y van por capas") PESOS DE BORDE para la
// teselacion (pes_face_tess.hlsli): 0 en los vertices del borde de la pieza (arista con un solo triangulo: ahi empieza otra pieza
// o termina la malla) y en los duplicados por posicion (costuras duras / de UV: el otro vertice tiene otra normal y el borde
// curvado no coincidiria), 1 en el resto. Con 0 en los dos extremos la arista queda recta en las dos piezas y siguen encajando.
// (04/11 h, Agustin: "se nota el bajo nivel poligonal en el lagrimal") eyeTag: en la cara, los vertices del BORDE de la pieza que caen
// en la abertura del ojo (reposo |x| 1.0..5.0 cm, y 1.680..1.715, z > 0.10) y que no son duplicados de otra pieza llevan 2.0 en vez de 0:
// el shader (fila 366) los deja redondear con su propio tope (lagrimal y contorno de los parpados)
bool ReadNrm(const uint8_t* p, DXGI_FORMAT f, float o[4]) {   // (07/11) solo para comparar normales entre vertices de la misma malla
    o[0] = o[1] = o[2] = 0.0f; o[3] = 1.0f;
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: memcpy(o, p, 16); return true;
    case DXGI_FORMAT_R32G32B32_FLOAT: memcpy(o, p, 12); return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: for (int i = 0; i < 4; i++) { uint16_t h; memcpy(&h, p + 2 * i, 2); o[i] = Half(h); } return true;
    case DXGI_FORMAT_R16G16B16A16_SNORM: for (int i = 0; i < 4; i++) { int16_t h; memcpy(&h, p + 2 * i, 2); o[i] = (std::max)(h / 32767.0f, -1.0f); } return true;
    case DXGI_FORMAT_R16G16B16A16_UNORM: for (int i = 0; i < 4; i++) { uint16_t h; memcpy(&h, p + 2 * i, 2); o[i] = h / 65535.0f * 2.0f - 1.0f; } return true;
    case DXGI_FORMAT_R8G8B8A8_SNORM: for (int i = 0; i < 4; i++) o[i] = (std::max)((int8_t)p[i] / 127.0f, -1.0f); return true;
    case DXGI_FORMAT_R8G8B8A8_UNORM: for (int i = 0; i < 4; i++) o[i] = p[i] / 255.0f * 2.0f - 1.0f; return true;
    case DXGI_FORMAT_B8G8R8A8_UNORM: o[0] = p[2] / 255.0f * 2.0f - 1.0f; o[1] = p[1] / 255.0f * 2.0f - 1.0f; o[2] = p[0] / 255.0f * 2.0f - 1.0f; return true;
    case DXGI_FORMAT_R10G10B10A2_UNORM: { uint32_t u; memcpy(&u, p, 4); o[0] = (u & 1023) / 1023.0f * 2.0f - 1.0f; o[1] = ((u >> 10) & 1023) / 1023.0f * 2.0f - 1.0f; o[2] = ((u >> 20) & 1023) / 1023.0f * 2.0f - 1.0f; return true; }
    default: return false;
    }
}
// PESOS DE BORDE de la teselacion (t119): 0 = el vertice no se corre (borde de la pieza), 1 = se redondea, 1.25 = costura de textura suave
// (redondeo sin relieve), 2 = borde del ojo (fila 366).
// (07/11, Agustin: "no en todas las caras se ve bien") antes todo vertice duplicado (costura de textura: misma posicion, otra uv) iba a
// 0, y como las dos copias tienen indices distintos sus aristas contaban como borde: cada costura de uv quedaba recta y facetada a lo
// largo de la cara. Ahora las aristas se cuentan por POSICION (grupo de copias) y un grupo cuyas copias tienen la misma normal y los
// mismos huesos (nrm / iv / wv) se redondea: el punto de Phong de una arista solo depende de sus dos extremos (posicion + normal), asi
// que los dos lados de la costura caen en el mismo lugar y no se abre. Los grupos con normales distintas (arista dura) y los bordes de
// verdad (arista con un solo triangulo: union con otra pieza, abertura del ojo, cuello) siguen en 0. Sin normales: como antes.
void SeamWeights(const std::vector<uint32_t>& tri, UINT vmin, UINT nv, const std::vector<float>& ps, std::vector<float>& w, int* nSeam, bool eyeTag = false, int* nEye = nullptr,
                 const float* nrm = nullptr, const uint32_t* iv = nullptr, const float* wv = nullptr, int* nSoft = nullptr) {
    w.assign(nv, 1.0f);
    // grupos por posicion a 0.05 mm (gid = primer vertice del grupo)
    std::vector<uint32_t> gid(nv), gsz(nv, 0); std::vector<uint8_t> hard(nv, 0);
    {
        std::unordered_map<uint64_t, uint32_t> pos; pos.reserve(nv);
        for (UINT v = 0; v < nv; v++) {
            const float* P = &ps[(size_t)v * 3];
            const int64_t qx = (int64_t)llroundf(P[0] * 20000.0f), qy = (int64_t)llroundf(P[1] * 20000.0f), qz = (int64_t)llroundf(P[2] * 20000.0f);
            uint64_t h = 0xcbf29ce484222325ULL; h = Fnv(h, (uint64_t)qx); h = Fnv(h, (uint64_t)qy); h = Fnv(h, (uint64_t)qz);
            auto it = pos.find(h);
            if (it == pos.end()) { pos[h] = v; gid[v] = v; }
            else {
                const uint32_t r = it->second; gid[v] = r;
                bool same = nrm && iv && wv;
                if (same) {
                    const float* a = &nrm[(size_t)r * 3]; const float* b = &nrm[(size_t)v * 3];
                    const float la = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), lb = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
                    same = la > 1e-4f && lb > 1e-4f && (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb) > 0.9995f;   // ~1.8 grados
                    for (int k = 0; same && k < 4; k++) {
                        const float wa = wv[(size_t)r * 4 + k], wb = wv[(size_t)v * 4 + k];
                        if (fabsf(wa - wb) > 0.01f || (wa > 0.004f && iv[(size_t)r * 4 + k] != iv[(size_t)v * 4 + k])) same = false;
                    }
                }
                if (!same) hard[r] = 1;
            }
            gsz[gid[v]]++;
        }
    }
    // aristas por grupo: la costura de uv (dos copias de cada extremo) cuenta dos triangulos, no es borde
    std::unordered_map<uint64_t, int> edges; edges.reserve(tri.size());
    for (size_t t = 0; t + 2 < tri.size(); t += 3) {
        const uint32_t a = tri[t] - vmin, b = tri[t + 1] - vmin, c = tri[t + 2] - vmin;
        if (a >= nv || b >= nv || c >= nv) continue;
        const uint32_t ga = gid[a], gb = gid[b], gc = gid[c];
        if (ga == gb || gb == gc || ga == gc) continue;
        auto E = [&](uint32_t x, uint32_t y) { if (x > y) std::swap(x, y); edges[((uint64_t)x << 32) | y]++; };
        E(ga, gb); E(gb, gc); E(gc, ga);
    }
    std::vector<uint8_t> border(nv, 0);
    for (auto& kv : edges) if (kv.second == 1) { border[(UINT)(kv.first >> 32)] = 1; border[(UINT)(kv.first & 0xffffffffu)] = 1; }
    int ne = 0, ns = 0;
    for (UINT v = 0; v < nv; v++) {
        const uint32_t g = gid[v];
        if (border[g] || hard[g]) { w[v] = 0; continue; }
        if (gsz[g] > 1) { w[v] = 1.25f; ns++; }   // costura suave (pes_face_tess.hlsli: Phong si, relieve no)
    }
    if (eyeTag) for (UINT v = 0; v < nv; v++) {   // borde de la abertura del ojo (sin copias): peso 2
        const uint32_t g = gid[v];
        if (!border[g] || hard[g] || gsz[g] > 1) continue;
        const float* P = &ps[(size_t)v * 3]; const float ax = fabsf(P[0]);
        if (ax >= 0.010f && ax <= 0.050f && P[1] >= 1.680f && P[1] <= 1.715f && P[2] > 0.10f) { w[v] = 2.0f; ne++; }
    }
    if (nEye) *nEye = ne;
    if (nSoft) *nSoft = nrm ? ns : -1;
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
constexpr UINT kShapeN = 6;   // (04/11 c) sonrisa grito enojo asombro puchero + sonrisa grande (el tope del slider Sonrisa, messi6 de Agustin)
// (04/11 c, Agustin: "con la sonrisa no se mueven todas las partes del ojo y modelos") las pestanas, la sombra del ojo y las demas
// mallas de la cara que no tienen formas propias se quedaban quietas mientras la piel se movia: ahora toman la forma de la piel mas
// cercana (vertices de la piel que tienen formas, a menos de 1.5 cm; se apaga entre 6 y 15 mm). La boca y el globo del ojo no.
// Solo mallas del MISMO modelo que esa piel (mismo buffer de indices): todas las caras estan en el mismo lugar del espacio del
// modelo y las pestanas de otro jugador tomarian la forma de Messi
struct ShapeSrc { std::vector<float> p; size_t off4 = 0; UINT nv = 0; uint32_t hu = 0; uint64_t ib = 0; };   // ib: buffer de indices de la ultima vez que se vio esa piel
std::vector<ShapeSrc> s_src;
// (04/11 i, cuelgue 08:17 PC: 65 GB privados en el partido) en partido el juego rearma cientos de mallas por segundo ("nuevas 64380"
// en 60 s) y a cada una se le pasaban formas / se enlazaba la barba: s_shapeData / s_beardData crecian sin fin y CADA vez se creaba
// un buffer nuevo con todo lo anterior sin liberar el viejo (crecimiento cuadratico). Ahora: (1) los datos iguales (la misma malla
// rearmada) reusan el mismo lugar del buffer, sin subir nada; (2) tope de 48 MB para formas y 24 MB para barbas (log una vez);
// (3) los buffers viejos se liberan 120 cuadros despues (el dibujo de otro hilo que los tenia en la mano ya termino, y uno que
// sigue enlazado lo retiene D3D11)
constexpr size_t kShapeCapF = (48u << 20) / 4, kBeardCapF = (24u << 20) / 4;
std::unordered_map<uint64_t, size_t> s_shapeDedup, s_beardDedup;   // huella de los datos -> primer float4
int s_dedupHits = 0, s_capHits = 0;
struct Retired { IUnknown* b; IUnknown* v; UINT frame; }; std::vector<Retired> s_retired;
uint64_t HashF(const std::vector<float>& d) {
    uint64_t h = 1469598103934665603ull; const unsigned char* p = (const unsigned char*)d.data();
    for (size_t i = 0, n = d.size() * 4; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h ^ (uint64_t)d.size();
}
void Retire(IUnknown* b, IUnknown* v) { if (b || v) s_retired.push_back({ b, v, s_frame }); }
void RetiredPresent() {   // con s_mx tomado, una vez por cuadro
    for (auto it = s_retired.begin(); it != s_retired.end();) {
        if (s_frame - it->frame > 120) { if (it->v) it->v->Release(); if (it->b) it->b->Release(); it = s_retired.erase(it); } else ++it;
    }
}
// agrega d a data (o reusa un lugar con los mismos datos); devuelve el primer float4, -1 si se paso del tope / fallo la subida
template <class Up> long long AppendPool(std::vector<float>& data, std::unordered_map<uint64_t, size_t>& dd, size_t cap, const std::vector<float>& d, Up up, const char* what) {
    const uint64_t h = HashF(d);
    auto it = dd.find(h);
    if (it != dd.end() && (it->second * 4 + d.size()) <= data.size() && !memcmp(&data[it->second * 4], d.data(), d.size() * 4)) { s_dedupHits++; return (long long)it->second; }
    if (data.size() + d.size() > cap) {
        if (s_capHits++ == 0) Log("expresiones: OJO tope de memoria de %s (%u MB): no se pasan mas hasta reiniciar el juego", what, (unsigned)(cap * 4 >> 20));
        return -1;
    }
    const size_t off4 = data.size() / 4;
    data.insert(data.end(), d.begin(), d.end());
    if (!up()) { data.resize(off4 * 4); return -1; }
    dd[h] = off4;
    return (long long)off4;
}
bool ShapeUpload() {   // con s_mx tomado: rehace el buffer con todo s_shapeData (el viejo se libera 120 cuadros despues)
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = (UINT)(s_shapeData.size() * 4); bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = s_shapeData.data();
    ID3D11Buffer* b = nullptr; ID3D11ShaderResourceView* v = nullptr;
    if (SUCCEEDED(g_dev.dev->CreateBuffer(&bd, &sd, &b))) {
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{}; vd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        vd.Buffer.FirstElement = 0; vd.Buffer.NumElements = (UINT)(s_shapeData.size() / 4);
        if (FAILED(g_dev.dev->CreateShaderResourceView(b, &vd, &v))) { b->Release(); b = nullptr; }
    }
    if (!v) return false;
    Retire(s_shapeBuf, s_shapeSrv);
    s_shapeBuf = b; s_shapeSrv = v; return true;
}
// formas de una malla sin paquete propio, sacadas de la piel cercana; -1 si no hay piel con formas cerca
// (04/11 d, log 05:39: solo la malla 75 (94 v) era del mismo buffer; las pestanas (29 y 71 v) vienen en otro) una malla chica
// (<= 3000 v) de otro buffer tambien la toma si el 85 % de sus vertices esta a menos de 6 mm de esa piel; como todas las caras
// estan en el mismo lugar del modelo, esas formas solo se enlazan cuando la malla se dibuja pegada (en el mismo cuadro, a pocos
// dibujos) a una piel con formas propias: las pestanas de otro jugador no se tocan (ExprBufferForDraw, xferGate)
// devuelve -2 si no hay pieles nuevas desde la ultima vez (nada que rehacer)
int ShapeXfer(FaceMesh* m, const float* ps, uint64_t ib) {   // con s_mx tomado
    if (m->nv > 20000) return -1;
    bool same = false; for (const ShapeSrc& S : s_src) if (S.ib == ib) same = true;
    if (!same && m->nv > 3000) return -1;
    int used = 0; for (const ShapeSrc& S : s_src) if (!same || S.ib == ib) used++;
    if (used == m->xferUsed) return -2;
    m->xferUsed = used;
    std::vector<float> d((size_t)m->nv * kShapeN * 4, 0.0f);
    int nCerca = 0, nPegado = 0; float mx = 0.0f;
    for (UINT v = 0; v < m->nv; v++) {
        const float* P = ps + (size_t)v * 3;
        struct Nb { float d2; const ShapeSrc* s; UINT i; } nb[4]; int nn = 0;
        for (const ShapeSrc& S : s_src) if (!same || S.ib == ib) for (UINT i = 0; i < S.nv; i++) {
            const float* Q = &S.p[(size_t)i * 3];
            const float dx = P[0] - Q[0], dy = P[1] - Q[1], dz = P[2] - Q[2], d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > 0.015f * 0.015f) continue;
            if (nn < 4) nb[nn++] = { d2, &S, i };
            else { int w = 0; for (int k = 1; k < 4; k++) if (nb[k].d2 > nb[w].d2) w = k; if (d2 < nb[w].d2) nb[w] = { d2, &S, i }; }
        }
        if (!nn) continue;
        float dmin = 1e9f; for (int k = 0; k < nn; k++) dmin = (std::min)(dmin, nb[k].d2); dmin = sqrtf(dmin);
        if (dmin < 0.006f) nPegado++;
        const float t = std::clamp((dmin - 0.006f) / 0.009f, 0.0f, 1.0f), fall = 1.0f - t * t * (3.0f - 2.0f * t);
        if (fall <= 0.0f) continue;
        float ws = 0.0f, w[4];
        for (int k = 0; k < nn; k++) { const float e = nb[k].d2 + 1e-8f; w[k] = 1.0f / (e * e); ws += w[k]; }   // 1/d^4: gana la mas cercana
        for (UINT g = 0; g < kShapeN; g++) for (int c = 0; c < 3; c++) {
            float a = 0.0f;
            for (int k = 0; k < nn; k++) a += w[k] * s_shapeData[((nb[k].s->off4 + (size_t)nb[k].i * kShapeN + g) * 4) + c];
            d[((size_t)v * kShapeN + g) * 4 + c] = a / ws * fall;
        }
        nCerca++;
        const float* q = &d[(size_t)v * kShapeN * 4]; mx = (std::max)(mx, sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]));
    }
    if (nCerca == 0 || mx < 1e-4f) return -1;
    if (!same && nPegado < (int)(0.85f * (float)m->nv)) return -1;
    m->xferGate = !same;
    const long long o4 = AppendPool(s_shapeData, s_shapeDedup, kShapeCapF, d, ShapeUpload, "formas");
    if (o4 < 0) return -1;
    const size_t off4 = (size_t)o4;
    static int nLog = 0; if (nLog < 60) nLog++; else return (int)off4;   // (04/11 i) el log tambien tiene tope
    Log("expresiones: formas pasadas de la piel a la malla %d (%08X, %u vertices, %d cerca de la piel, %d pegados; sonrisa maximo %.1f mm%s)", m->id, m->huella, m->nv, nCerca, nPegado, mx * 1000, same ? "" : "; otro buffer: solo junto a su piel");
    return (int)off4;
}
// cbuffer con las formas pasadas (y, si van solo junto a su piel, el mismo sin formas para cuando no)
void ShapeXferCB(FaceMesh* m, int off) {
    UINT v[kCbU]; memcpy(v, m->cbv, sizeof v); v[24] = (UINT)off; v[25] = m->nv; v[26] = m->vmin; v[27] = kShapeN;
    ID3D11Buffer* b = MakeCBRaw(v); if (!b) return;
    if (m->xferGate && !m->cbNoShape) { UINT z[kCbU]; memcpy(z, m->cbv, sizeof z); z[27] = 0; m->cbNoShape = MakeCBRaw(z); if (!m->cbNoShape) { b->Release(); return; } }
    memcpy(m->cbv, v, sizeof v); m->cb = b; m->shapeOff = off;   // el cbuffer viejo no se libera (otro hilo lo puede estar por enlazar)
}
void ShapeXferPending() {   // con s_mx tomado: llego piel con formas nueva, las mallas que esperaban la toman
    for (auto& kv : s_meshes) {
        FaceMesh* m = kv.second;
        if (m->state != 3 || m->xferPs.empty()) continue;
        const int off = ShapeXfer(m, m->xferPs.data(), m->xferIb);
        if (off < 0) continue;
        ShapeXferCB(m, off);
    }
}
int ShapeFind(uint32_t hu, UINT nv, const float* ps, uint64_t ib) {   // con s_mx tomado
    ShapePack& sp = s_shapes[hu];
    if (sp.tried) { if (sp.nv == nv && sp.off >= 0) for (ShapeSrc& S : s_src) if (S.hu == hu) S.ib = ib; return sp.nv == nv ? sp.off : -1; }
    // (las arrugas de la malla quedan en sp.wr; ExprShapeWr las da)
    sp.tried = true;
    wchar_t nm[64]; swprintf(nm, 64, L"formas\\%08X.bin", hu);
    FILE* f = _wfopen((g_cfg.baseDir + nm).c_str(), L"rb");
    if (!f) return -1;
    char mg[4] = {}; uint32_t hd[4] = {};
    const bool okh = fread(mg, 1, 4, f) == 4 && fread(hd, 4, 4, f) == 4 && !memcmp(mg, "PFXF", 4) && hd[0] == 1 && (hd[2] == kShapeN || hd[2] == 5);   // (04/11 c) los paquetes de 5 formas siguen valiendo (sin sonrisa grande)
    if (!okh || hd[1] != nv) { fclose(f); Log("expresiones: formas %08X.bin no valido (%s, %u vertices, la malla tiene %u)", hu, okh ? "otra malla" : "cabecera", okh ? hd[1] : 0u, nv); return -1; }
    std::vector<float> d((size_t)nv * kShapeN * 4, 0.0f);
    bool okd = true;
    { std::vector<float> r((size_t)nv * hd[2] * 4); okd = fread(r.data(), 4, r.size(), f) == r.size();
      for (UINT v = 0; okd && v < nv; v++) memcpy(&d[(size_t)v * kShapeN * 4], &r[(size_t)v * hd[2] * 4], hd[2] * 16); }
    fclose(f);
    if (!okd) { Log("expresiones: formas %08X.bin cortado", hu); return -1; }
    float mx[kShapeN] = {};
    for (UINT v = 0; v < nv; v++) for (UINT k = 0; k < kShapeN; k++) {
        float* q = &d[((size_t)v * kShapeN + k) * 4];
        for (int c = 0; c < 3; c++) q[c] = std::clamp(std::isfinite(q[c]) ? q[c] : 0.0f, -0.02f, 0.02f);   // tope 2 cm por vertice
        q[3] = 0; mx[k] = (std::max)(mx[k], sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]));
    }
    const long long o4 = AppendPool(s_shapeData, s_shapeDedup, kShapeCapF, d, ShapeUpload, "formas");
    if (o4 < 0) { Log("expresiones: formas %08X: no se pudo crear el buffer", hu); return -1; }
    const size_t off4 = (size_t)o4;
    sp.off = (int)off4; sp.nv = nv;
    { ShapeSrc src; src.p.assign(ps, ps + (size_t)nv * 3); src.off4 = off4; src.nv = nv; src.hu = hu; src.ib = ib; s_src.push_back(std::move(src)); }
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
    Log("expresiones: formas %08X.bin cargadas (%u vertices; maximo mm: sonrisa %.1f grito %.1f enojo %.1f asombro %.1f puchero %.1f sonrisa grande %.1f)", hu, nv, mx[0] * 1000, mx[1] * 1000, mx[2] * 1000, mx[3] * 1000, mx[4] * 1000, mx[5] * 1000);
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

// ---- (04/11 c, Agustin: "la barba no acompana del todo" / "la barba es un modelo aparte tiene un skeleton hair") BARBA POR LOS
// HUESOS DE LA CARA. La barba (hair_parts, VS del pelo 2f1af485) solo pesa en la cabeza: no sigue la mandibula, los labios ni la
// sonrisa. Cada vertice de la barba se enlaza (una vez, en la CPU) a los 4 vertices mas cercanos de la piel de la cara (mismo espacio
// del modelo; los pedazos de la cara comparten buffer) y toma sus huesos y pesos (y sus formas esculpidas). En cada dibujo de esa
// piel la DLL copia su paleta (b6: cuadro actual y anterior) y su m_world (b5) a un pozo en la GPU (t116, 64 lugares, sin leer nada
// en la CPU); el VS del pelo (pes_beard.hlsli) busca el lugar de su piel cuya cabeza cae donde esta la barba (mismo jugador) y le
// suma el gesto: lo que los huesos de la cara corren el punto respecto de la cabeza.
struct BeardSrc { int id = 0; uint64_t ib = 0; UINT nv = 0; int head = 0; int shapeOff = -1; bool jaw = false; std::vector<float> p, w; std::vector<uint8_t> j; };
std::vector<BeardSrc> s_bsrc; size_t s_bsrcBytes = 0;
// (04/11 d, arranque 06:53: "0 paletas copiadas") la piel se reconoce por su GEOMETRIA (buffer, primer vertice, vertices), no por el id de
// la malla: la misma piel tiene varias entradas en la tabla (otra pasada con otro input layout, LOD, mallas echadas y vueltas a leer) y la
// barba quedaba enlazada a una que no se dibujaba
std::map<uint64_t, int> s_geoIds; std::vector<int> s_geoUsed;
int GeoId(uint64_t ib, UINT vmin, UINT nv) { const uint64_t k = Fnv(Fnv(Fnv(0xcbf29ce484222325ULL, ib), vmin), nv); auto it = s_geoIds.find(k); if (it != s_geoIds.end()) return it->second; const int id = (int)s_geoIds.size() + 1; s_geoIds[k] = id; return id; }
// (04/11 e, log 07:44: "0 paletas" con 27 mallas marcadas) la barba se enlazaba a la piel MAS CERCANA en el espacio del modelo, y todas las caras
// leidas (otros jugadores, pantallas anteriores) estan en el mismo lugar: quedaba pegada a una piel que ya no se dibuja. Ahora se elige entre las
// pieles dibujadas en los ultimos cuadros, la mas cercana en el orden de dibujo a la barba (el mismo jugador dibuja la cara y el pelo juntos)
struct GeoDrawn { UINT frame = 0, seq = 0; }; std::map<int, GeoDrawn> s_geoDrawn;
int s_poolFail[4] = {};   // sin pozo, sin id, sin b5/b6, b5/b6 fuera de rango
std::vector<float> s_beardData; ID3D11Buffer* s_beardBuf = nullptr; ID3D11ShaderResourceView* s_beardSrv = nullptr;
ID3D11Buffer* s_pool = nullptr; ID3D11ShaderResourceView* s_poolSrv = nullptr; UINT s_poolAt = 0;
constexpr UINT kPoolSlots = 64, kSlotF4 = 208, kBeardF4 = 9;   // (08/11) 200 -> 208: 200..203 hueso kDef de cada lugar de la paleta de la piel (pes_beard.hlsli)
// (04/11 l, Agustin: "se buguea y rompe la barba") el VS de la barba elegia, vertice por vertice, cualquier lugar del pozo con esa piel
// (64 copias de distintos cuadros / pasadas): vertices vecinos tomaban paletas de cuadros distintos y la barba se rompia. Ahora al
// final del pozo hay un directorio (256 float4, por geometria & 255): x = el ULTIMO lugar copiado de esa piel; toda la barba usa ese
constexpr UINT kPoolDir = 256;
ID3D11Buffer* s_slotBuf[kPoolSlots] = {};
int s_beardCopies = 0, s_beardDraws = 0;
// (04/11 e, arranque 07:44: "0 paletas copiadas" con 27 mallas marcadas y 0 fallas = ninguna piel marcada se dibuja) que pieles de cara se
// dibujan de verdad (geometria, vertices) contra las que usa la barba, para ver por que no coinciden
struct GeoSeen { int geo, id; UINT nv; int n; bool mk; };
GeoSeen s_geoSeen[16]; int s_geoSeenN = 0, s_skinNoGeo = 0, s_skinNoGeoNv = 0;
bool BeardUpload() {
    if (s_beardData.empty() || !g_dev.dev) return false;
    D3D11_BUFFER_DESC d{}; d.ByteWidth = (UINT)(s_beardData.size() * 4); d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = s_beardData.data();
    ID3D11Buffer* b = nullptr; if (FAILED(g_dev.dev->CreateBuffer(&d, &sd, &b))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; v.Buffer.NumElements = (UINT)(s_beardData.size() / 4);
    ID3D11ShaderResourceView* sv = nullptr; if (FAILED(g_dev.dev->CreateShaderResourceView(b, &v, &sv))) { b->Release(); return false; }
    Retire(s_beardBuf, s_beardSrv);   // (04/11 i) se liberan 120 cuadros despues (antes nunca: 65 GB en el partido)
    s_beardBuf = b; s_beardSrv = sv;
    return true;
}
void BeardKeepSrc(FaceMesh* m, const std::vector<float>& ps, const std::vector<uint32_t>& iv, const std::vector<float>& wv, bool jaw) {   // con s_mx tomado
    m->geoId = GeoId((uint64_t)m->ib, m->vmin, m->nv);
    if (std::find(s_geoUsed.begin(), s_geoUsed.end(), m->geoId) != s_geoUsed.end()) m->beardSrc = true;
    for (BeardSrc& b : s_bsrc) if (b.id == m->geoId) { b.jaw = b.jaw || jaw; return; }
    const size_t by = (size_t)m->nv * (12 + 16 + 4);
    if (s_bsrcBytes + by > (32u << 20)) return;
    BeardSrc b; b.id = m->geoId; b.ib = (uint64_t)m->ib; b.nv = m->nv; b.head = m->head; b.shapeOff = m->xferGate ? -1 : m->shapeOff; b.jaw = jaw;
    b.p = ps; b.w = wv; b.j.resize(iv.size()); for (size_t i = 0; i < iv.size(); i++) b.j[i] = (uint8_t)(std::min)(iv[i], 31u);
    s_bsrcBytes += by; s_bsrc.push_back(std::move(b));
    static int nl = 0; if (nl++ < 60) Log("expresiones: piel para la barba: malla %d, geometria %d (%u vertices, primer vertice %u, cabeza %d%s%s)", m->id, m->geoId, m->nv, m->vmin, m->head, m->shapeOff >= 0 ? ", con formas" : "", jaw ? ", abre la boca" : ", pieza sin abrir la boca");
}
bool BeardBind(FaceMesh* m, const std::vector<float>& ps) {   // con s_mx tomado
    if (s_bsrc.empty() || m->nv == 0) return false;
    // 1) que cara: la que mas vertices de la barba tiene a menos de 12 mm (muestra de ~64 vertices), por buffer (los pedazos de una cara)
    std::map<uint64_t, int> cov; std::map<uint64_t, double> dsum;
    const UINT stp = (std::max)(1u, m->nv / 64);
    for (UINT v = 0; v < m->nv; v += stp) {
        const float* q = &ps[(size_t)v * 3];
        std::map<uint64_t, float> best;
        for (const BeardSrc& S : s_bsrc) {
            float b2 = 1e9f;
            for (UINT u = 0; u < S.nv; u++) { const float* r = &S.p[(size_t)u * 3]; const float dx = q[0] - r[0], dy = q[1] - r[1], dz = q[2] - r[2]; b2 = (std::min)(b2, dx * dx + dy * dy + dz * dz); }
            auto it = best.find(S.ib); if (it == best.end() || b2 < it->second) best[S.ib] = b2;
        }
        for (auto& kv : best) { const float d = sqrtf(kv.second); if (d < 0.012f) { cov[kv.first]++; dsum[kv.first] += d; } }
    }
    const int ns = (int)((m->nv + stp - 1) / stp);
    // grupos (buffers de piel) dibujados hace poco: el mas cercano en el orden de dibujo a la barba; si ninguno, el de mas cobertura
    std::map<uint64_t, UINT> nearDraw;   // ib -> distancia en el orden de dibujo
    for (const BeardSrc& S : s_bsrc) {
        auto g = s_geoDrawn.find(S.id); if (g == s_geoDrawn.end() || s_frame - g->second.frame > 4) continue;
        const UINT dq = g->second.seq > m->seqAt ? g->second.seq - m->seqAt : m->seqAt - g->second.seq;
        auto it = nearDraw.find(S.ib); if (it == nearDraw.end() || dq < it->second) nearDraw[S.ib] = dq;
    }
    uint64_t gib = 0; int gc = 0; double gd = 1e9; UINT gq = 0xffffffffu; bool recent = false;
    for (auto& kv : cov) {
        if (kv.second * 2 < ns) continue;
        auto nq = nearDraw.find(kv.first); if (nq == nearDraw.end()) continue;
        const double md = dsum[kv.first] / kv.second;
        if (nq->second < gq || (nq->second == gq && md < gd)) { gib = kv.first; gc = kv.second; gd = md; gq = nq->second; recent = true; }
    }
    if (!recent) for (auto& kv : cov) { const double md = dsum[kv.first] / kv.second; if (kv.second > gc || (kv.second == gc && md < gd)) { gib = kv.first; gc = kv.second; gd = md; } }
    if (gc * 2 < ns) {   // (04/11 d, Agustin: "el head shield del pelo no se mueve con los huesos") que malla del pelo no se enlaza y por que
        if (m->beardTries == 0 || m->beardTries == 14) Log("expresiones: pelo %d (%u vertices) sin enlazar a la piel (intento %d): muestra %d / %d a menos de 12 mm (hace falta la mitad)", m->id, m->nv, m->beardTries + 1, gc, ns);
        return false;
    }
    // (04/11 m, Agustin: "la barba sigue sin seguir ... con otros modelos de barba y el de messi tambien pero es menos notorio";
    // log 13:59 jugador 5e32: la barba de 6102 v quedo entera en la geometria 18 de 341 v) la cara de algunos jugadores viene en
    // pedazos de buffers distintos y la barba solo miraba el buffer elegido; ademas un pedazo que no es la cara entera no tiene las
    // mascaras de "abrir la boca" (whole) y lo que se enlazaba a el no abria. Ahora entran tambien los otros buffers de piel dibujados
    // a menos de 4 dibujos del elegido (el mismo jugador) y, por vertice, un pedazo sin "abrir la boca" pierde contra uno que si abre
    // salvo que este 6 mm mas cerca
    std::vector<uint64_t> cand{ gib };
    if (recent) for (auto& kv : nearDraw) if (kv.first != gib && kv.second <= gq + 4 && cov.count(kv.first)) cand.push_back(kv.first);
    // 2) por vertice: la pieza de la piel con el vertice mas cercano y sus 4 vecinos mas cercanos (1/d^4); se apaga de 8 a 25 mm
    std::vector<float> d((size_t)m->nv * kBeardF4 * 4, 0.0f);
    int nOk = 0, nJaw = 0; float dmax = 0;
    std::vector<int> used;
    for (UINT v = 0; v < m->nv; v++) {
        const float* q = &ps[(size_t)v * 3];
        const BeardSrc* bs = nullptr; struct Nb { float d2; UINT i; } nb[4] = { { 1e9f, 0 }, { 1e9f, 0 }, { 1e9f, 0 }, { 1e9f, 0 } };
        float bestAll = 1e9f, bestEff = 1e9f;
        for (const BeardSrc& S : s_bsrc) {
            if (std::find(cand.begin(), cand.end(), S.ib) == cand.end()) continue;
            Nb loc[4] = { { 1e9f, 0 }, { 1e9f, 0 }, { 1e9f, 0 }, { 1e9f, 0 } };
            for (UINT u = 0; u < S.nv; u++) {
                const float* r = &S.p[(size_t)u * 3]; const float dx = q[0] - r[0], dy = q[1] - r[1], dz = q[2] - r[2]; const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 >= loc[3].d2) continue;
                int k = 3; while (k > 0 && loc[k - 1].d2 > d2) { loc[k] = loc[k - 1]; k--; } loc[k] = { d2, u };
            }
            const float eff = sqrtf(loc[0].d2) + (S.jaw ? 0.0f : 0.006f);
            if (eff < bestEff) { bestEff = eff; bestAll = loc[0].d2; bs = &S; memcpy(nb, loc, sizeof nb); }
        }
        if (!bs) continue;
        const float dn = sqrtf(bestAll);
        // (04/11 k, Agustin: "la barba no sigue el menton y se deforma al abrir la boca") la barba 3D tiene pelos largos: los que
        // quedaban a mas de 8 mm de la piel seguian menos y a 25 mm nada (13 % de la barba de Messi), y la barba se estiraba. Ahora
        // siguen enteros hasta 30 mm y se apagan recien a 60 mm
        const float fade = dn <= 0.020f ? 1.0f : dn >= 0.045f ? 0.0f : 1.0f - (dn - 0.020f) / 0.025f;   // (04/11 l) 20 / 45 mm
        if (fade <= 0.0f) continue;
        float bw[32] = {}, ws = 0;
        float sh[kShapeN][3] = {};
        for (const Nb& n : nb) {
            if (n.d2 >= 1e8f) continue;
            const float w = 1.0f / (std::max)(n.d2 * n.d2, 1e-16f); ws += w;
            for (int k = 0; k < 4; k++) bw[bs->j[(size_t)n.i * 4 + k]] += w * bs->w[(size_t)n.i * 4 + k];
            if (bs->shapeOff >= 0) for (UINT g = 0; g < kShapeN; g++) for (int c = 0; c < 3; c++) sh[g][c] += w * s_shapeData[((bs->shapeOff + (size_t)n.i * kShapeN + g) * 4) + c];
        }
        if (ws <= 0) continue;
        int top[4] = { -1, -1, -1, -1 };
        for (int b = 0; b < 32; b++) { if (bw[b] <= 0) continue; int k = 4; while (k > 0 && (top[k - 1] < 0 || bw[top[k - 1]] < bw[b])) k--; if (k < 4) { for (int t = 3; t > k; t--) top[t] = top[t - 1]; top[k] = b; } }
        float tw = 0; for (int k = 0; k < 4; k++) if (top[k] >= 0) tw += bw[top[k]];
        if (tw <= 0) continue;
        float* o = &d[(size_t)v * kBeardF4 * 4];
        for (int k = 0; k < 4; k++) { o[k] = top[k] >= 0 ? (float)top[k] : 0.0f; o[4 + k] = top[k] >= 0 ? bw[top[k]] / tw * fade : 0.0f; }
        o[8] = (float)bs->id; o[9] = (float)bs->head;
        for (UINT g = 0; g < kShapeN; g++) for (int c = 0; c < 3; c++) o[12 + g * 4 + c] = sh[g][c] / ws * fade;
        nOk++; dmax = (std::max)(dmax, dn); if (bs->jaw) nJaw++;
        if (std::find(used.begin(), used.end(), bs->id) == used.end()) used.push_back(bs->id);
    }
    if (!nOk) return false;
    const long long o4 = AppendPool(s_beardData, s_beardDedup, kBeardCapF, d, BeardUpload, "barbas");
    if (o4 < 0) return false;
    const size_t off4 = (size_t)o4;
    for (int g : used) if (std::find(s_geoUsed.begin(), s_geoUsed.end(), g) == s_geoUsed.end()) s_geoUsed.push_back(g);
    int nMark = 0; for (auto& kv : s_meshes) if (kv.second->geoId && std::find(used.begin(), used.end(), kv.second->geoId) != used.end()) { kv.second->beardSrc = true; nMark++; }
    UINT cv[kCbU] = { (UINT)off4, m->nv, m->vmin, 1u }; for (UINT i = 28; i < kCbU; i++) cv[i] = 0xFFFFFFFFu;
    m->cb = MakeCBRaw(cv);   // (el cbuffer viejo de un re-enlace no se libera: otro hilo puede tenerlo en la mano)
    m->beardUsed = used; m->beardOjo = !recent;   // (04/11 l) enlazada sin piel dibujada junto: se reintenta
    char ug[96]; int uk = 0; ug[0] = 0; for (size_t i = 0; i < used.size() && uk < 80; i++) uk += snprintf(ug + uk, sizeof ug - uk, "%s%d", i ? "," : "", used[i]);
    static int nLogB = 0;
    if (nLogB < 80 && ++nLogB) Log("expresiones: barba %d (%u vertices) enlazada a la piel de la cara (%d piezas: geometrias %s, %d mallas de la tabla, %s, %d de %u vertices a menos de 25 mm, muestra %d / %d a menos de 12 mm, %.1f mm de promedio; %d buffers de piel, %d vertices en piel que abre la boca)", m->id, m->nv, (int)used.size(), ug, nMark, recent ? "piel dibujada junto a la barba" : "OJO: ninguna piel dibujada cerca, la mas cercana", nOk, m->nv, gc, ns, gd * 1000.0, (int)cand.size(), nJaw);
    return m->cb != nullptr;
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
        for (int s : { m->pos.slot, m->idx.slot, m->wgt.slot, m->nrm.slot, m->dump ? m->uv.slot : -1 }) {
            if (s < 0 || !m->vb[s] || m->stVB[s]) continue;
            D3D11_BUFFER_DESC d; m->vb[s]->GetDesc(&d);
            UINT used = 1;
            if (m->pos.slot == s) used = (std::max)(used, m->pos.off + FmtBytes(m->pos.fmt));
            if (m->idx.slot == s) used = (std::max)(used, m->idx.off + FmtBytes(m->idx.fmt));
            if (m->wgt.slot == s) used = (std::max)(used, m->wgt.off + FmtBytes(m->wgt.fmt));
            if (m->nrm.slot == s) used = (std::max)(used, m->nrm.off + FmtBytes(m->nrm.fmt));
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
        // (07/11) normales del modelo: las costuras de textura con la misma normal y los mismos huesos se redondean igual de los dos lados
        std::vector<float> nrv;
        if (ok && m->nrm.slot >= 0 && mapped[m->nrm.slot]) {
            nrv.resize((size_t)m->nv * 3);
            for (UINT v = 0; v < m->nv; v++) { float q[4]; if (!ReadNrm((const uint8_t*)mp[m->nrm.slot].pData + (size_t)v * m->stride[m->nrm.slot] + m->nrm.off, m->nrm.fmt, q)) { nrv.clear(); break; }
                memcpy(&nrv[(size_t)v * 3], q, 12); }
        }
        for (int s = 0; s < 4; s++) if (mapped[s]) imm->Unmap(m->stVB[s], 0);
        if (!ok) { Reject(m, "formato de posicion, huesos o pesos desconocido"); return; }
        if (m->body) {
            // (04/11 g, Agustin: "teselacion a las manos, brazos y piernas") PIEL DEL CUERPO: solo los pesos de borde (bordes de la pieza y
            // vertices duplicados = 0) en t119 y el cbuffer con vertices / primer indice (g_pesExprShape.y / .z, con el base del dibujo);
            // nada de cabeza, parpados ni formas. La paleta del cuerpo puede pasar de 32 huesos: no importa aca.
            std::vector<float> w; SeamWeights(m->tri, m->vmin, m->nv, ps, w, &m->seamN, false, nullptr, nrv.empty() ? nullptr : nrv.data(), iv.data(), wv.data(), &m->seamSoft);
            // (08/11 r, Agustin: "esas uniones tienen que ser perfectas ... en las uniones del rostro no se ven") UNIONES DEL CUERPO: el borde
            // de cada pieza (muneca con la mano, tobillo con la media, hombro) queda fijo, pero el anillo de al lado se redondeaba entero y la
            // union se veia como un pliegue. Como en la cara (que se apaga de a poco hacia el cuello), el peso sube en rampa desde el borde:
            // 0 en el borde, entero a 2.5 cm (distancia en el reposo al vertice de borde mas cercano). Las dos piezas quedan quietas en la union.
            {
                std::vector<UINT> bv; for (UINT v = 0; v < m->nv; v++) if (w[v] == 0.0f) bv.push_back(v);
                const float R = 0.025f; int nr = 0;
                if (!bv.empty() && (uint64_t)bv.size() * m->nv < 80000000ull) for (UINT v = 0; v < m->nv; v++) {
                    if (w[v] == 0.0f) continue;
                    const float* P = &ps[(size_t)v * 3]; float d2 = R * R;
                    for (UINT b : bv) { const float* Q = &ps[(size_t)b * 3]; const float dx = P[0] - Q[0], dy = P[1] - Q[1], dz = P[2] - Q[2]; const float e = dx * dx + dy * dy + dz * dz; if (e < d2) d2 = e; }
                    if (d2 >= R * R) continue;
                    const float t = sqrtf(d2) / R, k = t * t * (3.0f - 2.0f * t);
                    w[v] = (std::max)(k, 0.001f) * (w[v] > 1.0f ? 1.0f : w[v]); nr++;
                }
                static int nl = 0; if (nl++ < 40) Log("teselacion del cuerpo: malla %d: %d vertices en la rampa de 2.5 cm de los bordes (union sin pliegue)", m->id, nr);
            }
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth = (UINT)(w.size() * 4); bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = w.data();
            if (SUCCEEDED(g_dev.dev->CreateBuffer(&bd, &sd, &m->seamBuf))) {
                D3D11_SHADER_RESOURCE_VIEW_DESC vd{}; vd.Format = DXGI_FORMAT_R32_FLOAT; vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; vd.Buffer.FirstElement = 0; vd.Buffer.NumElements = (UINT)w.size();
                if (FAILED(g_dev.dev->CreateShaderResourceView(m->seamBuf, &vd, &m->seamSrv))) { m->seamBuf->Release(); m->seamBuf = nullptr; m->seamSrv = nullptr; }
            } else m->seamBuf = nullptr;
            const UINT shp[4] = { 0u, m->nv, m->vmin, 0u };   // igual que la cara (PesTessSeam: vid - vmin)
            m->head = 99; m->maxIdx = mx < 0 ? 0 : mx;
            m->cb = MakeCB(99, (UINT)m->maxIdx, (UINT)m->id, 0, nullptr, 0, 0, 99, 0, nullptr, nullptr, shp, m->cbv);
            m->tri.clear(); m->tri.shrink_to_fit(); ReleaseIA(m);
            if (!m->cb || !m->seamSrv) { m->state = 4; m->why = m->cb ? "sin pesos de borde" : "sin cbuffer"; s_hist[3]++; return; }
            m->state = 3;
            static int nb = 0;
            if (nb < 80) { nb++; Log("teselacion del cuerpo: malla %d: %u vertices, %u triangulos, alto %.2f..%.2f m, ancho %.2f m: teselable (%d vertices de borde, %d en costuras suaves)", m->id, m->nv, m->count / 3, ylo, yhi, xhi - xlo, m->seamN, m->seamSoft); }
            return;
        }
        if (mx < 0 || mx > 31) { Reject(m, "indices de hueso fuera de la paleta de 32"); return; }
        if (m->beard) {   // (04/11 c) malla del VS del pelo: se enlaza a la piel de la cara (si todavia no se leyo la piel, se reintenta)
            ReleaseIA(m);
            if (BeardBind(m, ps)) { m->state = 3; m->beardPs = std::move(ps); return; }   // (04/11 e) los vertices quedan para re-enlazar
            m->beardPs = std::move(ps); m->state = 5; m->why = "pelo sin piel de cara cerca (todavia)"; return;
        }
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
        if (hd <= 31) for (int b = 0; b <= mx && b < 32; b++) if (all[b] > 0.5f) { m->slotC[b][0] = cx[b] / all[b]; m->slotC[b][1] = cy[b] / all[b]; m->slotC[b][2] = cz[b] / all[b]; m->slotC[b][3] = all[b]; }
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
        // (alto >= 15 cm, ancho >= 12 cm, >= 500 vertices): en una cara partida en piezas (nariz o menton aparte) cada pieza
        // tendria otros huesos marcados y se abre un corte entre ellas. La boca 3D (lengua) y el parpadeo no entran aqui.
        // (08/11 f, log de fc4255a: la cara HD de Messi es una malla de 1379 vertices, alto 1.385..1.808 m, y salia como "pieza": con
        // "Por shader" la boca no abria) el tope de vertices baja de 1500 a 500: las piezas de nariz / menton ya quedan fuera por el alto
        const bool whole = !mouth && !eye && (yhi - ylo) >= 0.15f && (xhi - xlo) >= 0.12f && m->nv >= 500;
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
        const uint64_t ibKey = (uint64_t)m->ib;
        m->shapeOff = (hd <= 31 && !eye && !mouth) ? ShapeFind(m->huella, m->nv, ps.data(), ibKey) : -1;
        if (m->shapeOff >= 0) m->shapeWr = s_shapes[m->huella].wr;
        else if (hd <= 31 && !eye && !mouth) {   // (04/11 c) sin formas propias: las de la piel cercana del mismo modelo (ahora o cuando llegue)
            const int xo = ShapeXfer(m, ps.data(), ibKey); m->shapeOff = xo >= 0 ? xo : -1;
            // se guardan los vertices por si llega otro pedazo de esa piel despues (la cara viene en 3 pedazos): solo mallas chicas,
            // con tope total de ~24 MB
            static size_t kept = 0;
            if (m->nv <= 20000 && kept + ps.size() * 4 < (24u << 20)) { m->xferPs = ps; m->xferIb = ibKey; kept += ps.size() * 4; }
        }
        if (hd <= 31 && !eye && !mouth && !thin && m->nv >= 300 && m->nv <= 20000) BeardKeepSrc(m, ps, iv, wv, (jawFull | jawHalf) != 0u);   // (04/11 c) piel de cara: la barba se enlaza a ella
        const UINT shp[4] = { m->shapeOff >= 0 ? (UINT)m->shapeOff : 0u, m->nv, m->vmin, m->shapeOff >= 0 ? kShapeN : 0u };
        m->cb = MakeCB((UINT)m->head, (UINT)mx, (UINT)m->id, eye ? 2u : (mouth ? 1u : 0u), lids, jawFull, jawHalf, lipTop, lipUp, eye ? eyes : nullptr, mus, shp, m->cbv);
        if (m->cb && m->xferGate && m->shapeOff >= 0) { UINT z[kCbU]; memcpy(z, m->cbv, sizeof z); z[27] = 0; m->cbNoShape = MakeCBRaw(z); }
        for (int i = 0; i < 4; i++) m->lids[i] = lids[i];
        if (m->dump && hd <= 31 && !eye && !mouth && m->nv >= 200) DumpObj(m, ps, uvs, iv, wv, ylo, yhi);
        // (04/11 b) teselacion de la cara (facetess.cpp): solo la piel de la cara (no boca, no ojo, no pestanas / parpados finos,
        // no piezas chicas): pesos de borde por vertice en un Buffer<float> (t119 del VS)
        if (hd <= 31 && !eye && !mouth && !thin && m->nv >= 200 && g_dev.dev) {
            std::vector<float> w; SeamWeights(m->tri, m->vmin, m->nv, ps, w, &m->seamN, true, &m->seamEye, nrv.empty() ? nullptr : nrv.data(), iv.data(), wv.data(), &m->seamSoft);
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
        if (m->shapeOff >= 0 && m->xferPs.empty()) ShapeXferPending();   // (04/11 c) llego un pedazo de piel con formas: las mallas de ese modelo la toman
        static int n = 0;
        if (n++ < 80) {   // uso de cada hueso de la paleta (peso total / peso en la parte alta) para comparar con face_rig.py
            char l[1400]; int k = snprintf(l, sizeof l, "expresiones: malla de cara %d (%u indices, %u vertices, alto %.3f..%.3f m): hueso maximo %d -> cabeza en la paleta %d%s, parpados %d %d / %d %d, boca abajo %08X mitad %08X labio %u arriba %08X comisuras %d %d cejas %d %d%s%s |",
                                           m->id, m->count, m->nv, ylo, yhi, mx, m->head, m->head > 31 ? " (no es cara)" : thin ? " (fina: por vertices)" : mouth ? " (boca: lengua)" : "", lids[0], lids[1], lids[2], lids[3], jawFull, jawHalf, lipTop, lipUp, mus[0], mus[1], mus[2], mus[3], eye ? " (OJO)" : "", (!whole && !mouth && !eye && hd <= 31) ? " (pieza: sin boca/nariz/musculos)" : "");
            if (m->seamSrv) k += snprintf(l + k, sizeof l - k, " teselable (%d vertices de borde, %d en el borde del ojo, %d en costuras suaves)", m->seamN, m->seamEye, m->seamSoft);
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
static void WrinkleCapture(ID3D11DeviceContext* c, FaceMesh* m);   // (08/11) pesos de arrugas del juego, abajo
static void SlotCapture(ID3D11DeviceContext* c, FaceMesh* m); static void SlotPresent(ID3D11DeviceContext* imm); static void DeltaUpload(ID3D11DeviceContext* imm);   // (08/11) gestos en la paleta, abajo
// (04/11 c) barba: la paleta (b6, actual + anterior) y el m_world (b5) de esta piel a un lugar del pozo, con su id y su cabeza adelante
static void BeardPoolCopy(ID3D11DeviceContext* c, FaceMesh* m) {   // con s_mx tomado
    if (!s_pool) {
        D3D11_BUFFER_DESC d{}; d.ByteWidth = (kPoolSlots * kSlotF4 + kPoolDir) * 16; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<float> z((size_t)(kPoolSlots * kSlotF4 + kPoolDir) * 4, 0.0f); for (UINT i = 0; i < kPoolDir; i++) z[((size_t)kPoolSlots * kSlotF4 + i) * 4] = 99.0f; D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = z.data();
        if (FAILED(g_dev.dev->CreateBuffer(&d, &sd, &s_pool))) { s_pool = nullptr; s_poolFail[0]++; return; }
        D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; v.Buffer.NumElements = kPoolSlots * kSlotF4 + kPoolDir;
        if (FAILED(g_dev.dev->CreateShaderResourceView(s_pool, &v, &s_poolSrv))) { s_poolSrv = nullptr; }
    }
    if (!s_poolSrv) { s_poolFail[0]++; return; }
    if (!m->idBuf) {
        // (04/11 j, captura de Agustin: pelos de la barba sobre los dientes con la boca abierta) tambien las mascaras de "abrir la boca"
        // de esta piel (huesos de abajo enteros / comisuras a la mitad, b12 g_pesExprJaw.xy) partidas en 16 bits: la barba gira igual
        const UINT jf = m->cbv[8], jh = m->cbv[9];
        float h[24] = { (float)m->geoId, (float)m->head, (float)(jf & 0xFFFFu), (float)(jf >> 16), (float)(jh & 0xFFFFu), (float)(jh >> 16), 0.0f, 0.0f };
        for (int i = 0; i < 16; i++) h[8 + i] = (float)(m->slotsOk ? m->slotBone[2 * i] + 256 * m->slotBone[2 * i + 1] : 255 + 256 * 255);   // (08/11) 2 lugares por float
        D3D11_BUFFER_DESC d{}; d.ByteWidth = 96; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = h;
        if (FAILED(g_dev.dev->CreateBuffer(&d, &sd, &m->idBuf))) { m->idBuf = nullptr; s_poolFail[1]++; return; }
    }
    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) || !c1) { s_poolFail[2]++; return; }
    ID3D11Buffer* b6 = nullptr; ID3D11Buffer* b5 = nullptr; UINT f6 = 0, n6 = 0, f5 = 0, n5 = 0;
    c1->VSGetConstantBuffers1(6, 1, &b6, &f6, &n6); c1->VSGetConstantBuffers1(5, 1, &b5, &f5, &n5); c1->Release();
    if (b6 && b5) {
        D3D11_BUFFER_DESC d6, d5; b6->GetDesc(&d6); b5->GetDesc(&d5);
        const UINT a6 = f6 * 16, a5 = f5 * 16 + 64;
        if (a6 < d6.ByteWidth && a5 + 64 <= d5.ByteWidth) {
            const UINT slot = s_poolAt++ % kPoolSlots, base = slot * kSlotF4 * 16;
            const UINT len6 = (std::min)(192u * 16u, d6.ByteWidth - a6) & ~15u;
            D3D11_BOX x6 = { a6, 0, 0, a6 + len6, 1, 1 }; c->CopySubresourceRegion(s_pool, 0, base + 16, 0, 0, b6, 0, &x6);
            D3D11_BOX x5 = { a5, 0, 0, a5 + 64, 1, 1 }; c->CopySubresourceRegion(s_pool, 0, base + 193 * 16, 0, 0, b5, 0, &x5);
            D3D11_BOX xh = { 0, 0, 0, 16, 1, 1 }; c->CopySubresourceRegion(s_pool, 0, base, 0, 0, m->idBuf, 0, &xh);
            D3D11_BOX xj = { 16, 0, 0, 32, 1, 1 }; c->CopySubresourceRegion(s_pool, 0, base + 197 * 16, 0, 0, m->idBuf, 0, &xj);   // (04/11 j)
            D3D11_BOX xs = { 32, 0, 0, 96, 1, 1 }; c->CopySubresourceRegion(s_pool, 0, base + 200 * 16, 0, 0, m->idBuf, 0, &xs);   // (08/11) huesos de la paleta
            if (!s_slotBuf[slot]) {   // (04/11 l) directorio: este lugar es el ultimo de esta piel
                const float sv[4] = { (float)slot, 0.0f, 0.0f, 0.0f };
                D3D11_BUFFER_DESC sb{}; sb.ByteWidth = 16; sb.Usage = D3D11_USAGE_IMMUTABLE; sb.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA ss{}; ss.pSysMem = sv;
                if (FAILED(g_dev.dev->CreateBuffer(&sb, &ss, &s_slotBuf[slot]))) s_slotBuf[slot] = nullptr;
            }
            if (s_slotBuf[slot]) { D3D11_BOX xd = { 0, 0, 0, 16, 1, 1 }; c->CopySubresourceRegion(s_pool, 0, (kPoolSlots * kSlotF4 + ((UINT)m->geoId & (kPoolDir - 1))) * 16, 0, 0, s_slotBuf[slot], 0, &xd); }
            s_beardCopies++;
        } else s_poolFail[3]++;
    } else s_poolFail[2]++;
    if (b6) b6->Release(); if (b5) b5->Release();
}
bool BeardForDraw(ID3D11DeviceContext* c, UINT count, UINT start, INT base, ID3D11Buffer** cb, ID3D11ShaderResourceView** pool, ID3D11ShaderResourceView** bind) {
    *cb = nullptr; *pool = nullptr; *bind = nullptr;
    t_beardDraw = true;
    ID3D11Buffer* b = ExprBufferForDraw(c, count, start, base);
    t_beardDraw = false;
    if (!b || b == s_none) return false;
    std::lock_guard<std::mutex> lk(s_mx);
    *cb = b; *pool = s_poolSrv; *bind = s_beardSrv;
    return s_poolSrv && s_beardSrv;
}
ID3D11Buffer* ExprBufferForDraw(ID3D11DeviceContext* c, UINT count, UINT start, INT base, const float** headCentroid, int* headIdx, ID3D11ShaderResourceView** shapeSrv, ID3D11ShaderResourceView** shapeWr, ID3D11ShaderResourceView** tessSeam, bool body, ID3D11Buffer** wrCb, bool skinPs) {
    if (headCentroid) *headCentroid = nullptr;
    if (tessSeam) *tessSeam = nullptr;
    if (shapeSrv) *shapeSrv = nullptr;
    if (shapeWr) *shapeWr = nullptr;
    if (!g_dev.dev) return nullptr;
    if (!s_none) { std::lock_guard<std::mutex> lk(s_mx); if (!s_none) s_none = MakeCB(99, 0, 0); }
    // (08/11, Agustin: "la teselacion se aplica bien, no con las expresiones") con las expresiones apagadas (x340 = 0) la piel de la cara
    // igual se lee y se tesela: cbuffer neutro (cabeza 99, sin formas) con vertices / primer indice para los pesos de borde
    const bool exprOff = !body && g_cfg.live[340][0] < 0.5f;
    if (count < 3) { s_bindsNone++; return s_none; }   // (04/11 g) el cuerpo no depende de las expresiones
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0; c->IAGetIndexBuffer(&ib, &ibFmt, &ibOff);
    ID3D11Buffer* vb[4] = {}; UINT stride[4] = {}, off[4] = {}; c->IAGetVertexBuffers(0, 4, vb, stride, off);
    ID3D11InputLayout* il = nullptr; c->IAGetInputLayout(&il);
    auto rel = [&]() { if (ib) ib->Release(); for (auto*& b : vb) { if (b) b->Release(); b = nullptr; } if (il) il->Release(); ib = nullptr; il = nullptr; };
    if (!ib || !il || (ibFmt != DXGI_FORMAT_R16_UINT && ibFmt != DXGI_FORMAT_R32_UINT)) { rel(); s_bindsNone++; return s_none; }
    uint64_t key = 0xcbf29ce484222325ULL;
    key = Fnv(key, (uint64_t)ib); key = Fnv(key, ibOff); key = Fnv(key, start); key = Fnv(key, count); key = Fnv(key, (uint64_t)(int64_t)base);
    key = Fnv(key, (uint64_t)il); for (int i = 0; i < 4; i++) { key = Fnv(key, (uint64_t)vb[i]); key = Fnv(key, off[i]); key = Fnv(key, stride[i]); }
    if (t_beardDraw) key = Fnv(key, 0xbea5dULL);
    std::lock_guard<std::mutex> lk(s_mx);
    auto it = s_meshes.find(key);
    if (it != s_meshes.end()) {
        rel(); FaceMesh* m = it->second; m->lastFrame = s_frame;
        if (m->beard) { m->seqAt = s_drawSeq; if (exprOff) return s_none; if (m->state == 3 && m->cb) { s_beardDraws++; return m->cb; } return nullptr; }
        if (exprOff) {
            if (m->state == 3 && m->seamSrv && tessSeam) {
                if (!m->cbTess) { const UINT shp[4] = { 0u, m->nv, m->vmin, 0u }; m->cbTess = MakeCB(99, (UINT)(m->maxIdx < 0 ? 0 : m->maxIdx), (UINT)m->id, 0, nullptr, 0, 0, 99, 0, nullptr, nullptr, shp); }
                if (m->cbTess) { *tessSeam = m->seamSrv; return m->cbTess; }
            }
            s_bindsNone++; return s_none;
        }
        if (m->state == 3 && m->cb) {
            s_binds++;
            if (m->beardSrc) BeardPoolCopy(c, m);
            if (m->head <= 31 && m->cbv[3] == 0 && m->nv >= 300) {   // (04/11 e) piel de cara dibujada (registro de la barba)
                if (!m->geoId) { s_skinNoGeo++; s_skinNoGeoNv = (int)m->nv; }
                else { int i = 0; for (; i < s_geoSeenN; i++) if (s_geoSeen[i].geo == m->geoId) { s_geoSeen[i].n++; break; }
                       if (i == s_geoSeenN && s_geoSeenN < 16) s_geoSeen[s_geoSeenN++] = { m->geoId, m->id, m->nv, 1, m->beardSrc }; }
            }
            const UINT seq = ++s_drawSeq;
            if (m->geoId) { GeoDrawn& gdn = s_geoDrawn[m->geoId]; gdn.frame = s_frame; gdn.seq = seq; }   // (04/11 e) piel dibujada ahora
            if (m->shapeOff >= 0 && !m->xferGate) {   // piel con formas propias: las mallas pegadas de antes en este cuadro quedan habilitadas
                s_packSeq = seq; s_packFrame = s_frame;
                for (const GateE& g : s_gateRing) if (g.id && g.seq + 16 >= seq && g.seq <= seq) s_gateOk[s_gateOkAt++ % 32] = { g.id, s_frame };   // seq del ok = cuadro
            }
            if (m->xferGate && m->shapeOff >= 0) {
                s_gateRing[s_gateAt++ % 16] = { m->id, seq };
                bool ok = s_packFrame == s_frame && seq - s_packSeq <= 16;   // dibujada poco despues de su piel
                for (const GateE& g : s_gateOk) if (g.id == m->id && s_frame - g.seq <= 2) ok = true;   // o poco antes (cuadro anterior)
                // (04/11 d) como la boca: despues de 8 veces junto a su piel queda habilitada (si no, iba y venia entre pasadas: vibraba)
                if (ok && !m->teethOn && ++m->teethHits >= 8) m->teethOn = true;
                if (m->teethOn) ok = true;
                // (08/11, Agustin: "la teselacion se aplica bien, no con las expresiones") la malla pegada a una piel con formas, cuando se
                // dibuja sin sus formas (lejos de su piel en el orden), tambien se tesela: los pesos de borde no dependen de las formas
                if (!ok) { s_gateOff++; if (tessSeam && m->seamSrv) *tessSeam = m->seamSrv; if (m->head <= 31 && (m->lids[0] >= 0 || m->lids[1] >= 0) && s_frame - m->lidCap >= 120) { m->lidCap = s_frame; LidCapture(c, m); } return m->cbNoShape ? m->cbNoShape : s_none; }
                s_gateOn++;
            }
            // (04/11 c, Agustin: "en la sonrisa deberias estirar los dientes horizontalmente un poco asi como bajar los dientes un
            // poco"; OK de Agustin para tocar la boca SOLO con la sonrisa) la boca que se dibuja justo despues de una piel con formas
            // propias (la cara esculpida: Messi) lleva la marca de dientes con sonrisa (b12 g_pesExprShape.x = 1; pes_expr.hlsli
            // PesTeethSmile). Sin sonrisa el shader no mueve nada; las otras bocas quedan como siempre
            // (04/11 d, Agustin: "la boca de messi si vibra bastante") la marca iba y venia segun el orden de los dibujos de cada pasada
            // (una pasada con los dientes corridos y otra sin): ahora, despues de 8 veces junto a su piel, esa boca queda marcada
            if (m->cbv[3] == 1 && !m->teethOn && s_packFrame == s_frame && seq - s_packSeq <= 4 && ++m->teethHits >= 8) m->teethOn = true;
            if (m->cbv[3] == 1 && m->teethOn) {
                if (!m->cbTeeth) { UINT z[kCbU]; memcpy(z, m->cbv, sizeof z); z[24] = 1; z[25] = 0; z[26] = 0; z[27] = 0; m->cbTeeth = MakeCBRaw(z); }
                if (m->cbTeeth) { s_teethOn++; if (headCentroid && m->hcOk && m->nv >= 300) { *headCentroid = m->hc; if (headIdx) *headIdx = m->head; } return m->cbTeeth; }
            }
            if (shapeSrv && m->shapeOff >= 0 && s_shapeSrv) { *shapeSrv = s_shapeSrv; s_shapeDraws++; if (shapeWr) *shapeWr = m->shapeWr; }
            if (tessSeam && m->seamSrv) *tessSeam = m->seamSrv;   // (04/11 b) piel de la cara: se puede teselar
            if (m->head <= 31 && (m->lids[0] >= 0 || m->lids[1] >= 0) && s_frame - m->lidCap >= 120) { m->lidCap = s_frame; LidCapture(c, m); }
            if (m->head <= 31 && !m->slotsOk && m->slotTries < 40 && g_cfg.optExprPalette && s_frame - m->slotCap >= 45) { m->slotCap = s_frame; m->slotTries++; SlotCapture(c, m); }   // (08/11)
            if (wrCb && skinPs && m->head <= 31 && m->nv >= 300 && g_cfg.live[397][2] > 0.0f) { WrinkleCapture(c, m); if (m->wrHave && m->wrCb) *wrCb = m->wrCb; }   // (08/11) pesos de arrugas suavizados
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
        if (s_meshes.size() >= 4096) {
            // (04/11 i, cuelgue 08:17) antes se vaciaba entera: todas las caras se releian y se les volvian a pasar formas / enlazar la
            // barba en bucle. Ahora se echan las mas viejas hasta 3072, nunca las dibujadas en los ultimos 3 cuadros
            std::vector<std::pair<UINT, decltype(s_meshes.begin())>> old; old.reserve(s_meshes.size());
            for (auto it2 = s_meshes.begin(); it2 != s_meshes.end(); ++it2) if (s_frame - it2->second->lastFrame > 3) old.push_back({ it2->second->lastFrame, it2 });
            std::sort(old.begin(), old.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
            size_t need = s_meshes.size() - 3072;
            for (size_t k = 0; k < old.size() && k < need; k++) {
                FaceMesh* o = old[k].second->second;
                s_pending.erase(std::remove(s_pending.begin(), s_pending.end(), o), s_pending.end());
                FreeMesh(o); s_meshes.erase(old[k].second); s_evicted++;
            }
            static int nFull = 0; if (nFull < 20) { nFull++; Log("expresiones: tabla llena (4096 mallas): echadas las %d mas viejas", (int)(std::min)(old.size(), need)); }
        }
    }
    FaceMesh* m = new FaceMesh(); m->id = s_nextId++; m->lastFrame = s_frame; m->beard = t_beardDraw; m->seqAt = s_drawSeq;
    m->dump = !body && s_frame < s_dumpUntil.load(std::memory_order_relaxed);
    m->body = body;
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
            else if (e.idx == 0 && !_stricmp(e.sem.c_str(), "NORMAL")) t = &m->nrm;
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
// ---- (08/11) pesos de arrugas del juego suavizados (ver FaceMesh::wrSt) ----
static void WrinkleCapture(ID3D11DeviceContext* c, FaceMesh* m) {   // con s_mx tomado; en el dibujo de la piel (PS 5ce3 / ce188)
    if (m->wrCapFrame == s_frame) return; m->wrCapFrame = s_frame;
    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) || !c1) return;
    ID3D11Buffer* b = nullptr; UINT first = 0, num = 0;
    c1->PSGetConstantBuffers1(4, 1, &b, &first, &num); c1->Release();
    if (!b) return;
    D3D11_BUFFER_DESC d; b->GetDesc(&d);
    const UINT a = first * 16, len = 8 * 16;   // m_materials[8]
    if (a + len <= d.ByteWidth) {
        const int j = m->wrStI % 3;
        if (!m->wrSt[j]) m->wrSt[j] = Staging(len);
        if (m->wrSt[j]) { D3D11_BOX bx = { a, 0, 0, a + len, 1, 1 }; c->CopySubresourceRegion(m->wrSt[j], 0, 0, 0, 0, b, 0, &bx); m->wrStFrame[j] = (int)s_frame; m->wrStI++; }
    }
    b->Release();
}
static int s_wrLogs = 0; static double s_wrLogT = 0;
static void WrinklePresent(ID3D11DeviceContext* imm) {   // con s_mx tomado (Present)
    const bool on = g_cfg.live[397][2] > 0.0f;
    const float x = g_cfg.live[397][0]; const float tau = x > 0.0f ? 0.10f * x : 0.05f;   // mismo tiempo que los huesos (397.x); sin suavizado de huesos, 50 ms
    const double now = Now(); int nUp = 0;
    for (auto& kv : s_meshes) {
        FaceMesh* m = kv.second;
        if (m->wrStI == 0) continue;
        if (!on || s_frame - m->lastFrame > 120) { if (m->wrHave && s_frame - m->lastFrame > 600) m->wrHave = false; continue; }
        // la copia mas nueva que ya este lista y sea posterior a la leida
        float f[32]; bool got = false;
        for (int k = 1; k <= 3 && !got; k++) {
            const int j = (m->wrStI - k + 3 * 1024) % 3;
            if (!m->wrSt[j] || m->wrStFrame[j] < 0 || m->wrStFrame[j] <= m->wrReadFrame) break;
            D3D11_MAPPED_SUBRESOURCE mp;
            if (SUCCEEDED(imm->Map(m->wrSt[j], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mp))) { memcpy(f, mp.pData, sizeof f); imm->Unmap(m->wrSt[j], 0); m->wrReadFrame = m->wrStFrame[j]; got = true; }
        }
        if (!got) continue;
        const float tgt[9] = { f[1], f[12], f[13], f[14], f[15], f[16], f[17], f[18], f[19] };   // [0].y, [3].xyzw, [4].xyzw
        bool bad = false; for (float v : tgt) if (!(v == v) || fabsf(v) > 64.0f) bad = true;
        if (bad) continue;
        float dt = (float)(now - m->wrT); m->wrT = now;
        if (!m->wrHave || dt > 1.0f || dt <= 0.0f) { memcpy(m->wrW, tgt, sizeof tgt); memset(m->wrV, 0, sizeof m->wrV); m->wrHave = true; }
        else {
            if (dt > 1.0f / 15.0f) dt = 1.0f / 15.0f;
            // resorte criticamente amortiguado (SmoothDamp), igual que Damp() de exprbones.cpp
            const float om = 2.0f / (tau > 0.001f ? tau : 0.001f), xx = om * dt, e = 1.0f / (1.0f + xx + 0.48f * xx * xx + 0.235f * xx * xx * xx);
            for (int i = 0; i < 9; i++) { const float ch = m->wrW[i] - tgt[i], tmp = (m->wrV[i] + om * ch) * dt; m->wrV[i] = (m->wrV[i] - om * tmp) * e; m->wrW[i] = tgt[i] + (ch + tmp) * e; }
        }
        if (!m->wrCb) { D3D11_BUFFER_DESC d{}; d.ByteWidth = 48; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; if (FAILED(g_dev.dev->CreateBuffer(&d, nullptr, &m->wrCb))) { m->wrCb = nullptr; continue; } }
        const float up[12] = { m->wrW[0], 1.0f, tau, 0.0f, m->wrW[1], m->wrW[2], m->wrW[3], m->wrW[4], m->wrW[5], m->wrW[6], m->wrW[7], m->wrW[8] };
        imm->UpdateSubresource(m->wrCb, 0, nullptr, up, 0, 0); nUp++;
        if (s_wrLogs < 6 && now - s_wrLogT > 10.0) { s_wrLogT = now; s_wrLogs++;
            Log("arrugas: malla %d pesos del juego (0.y %.2f | 3 %.2f %.2f %.2f %.2f | 4 %.2f %.2f %.2f %.2f) suavizados tau %.3f s", m->id, tgt[0], tgt[1], tgt[2], tgt[3], tgt[4], tgt[5], tgt[6], tgt[7], tgt[8], tau); }
    }
    (void)nUp;
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

// ---- (08/11, Agustin: "todo sigue vibrando") GESTOS EN LA PALETA ----
// exprbones.cpp ya no escribe los huesos de la cara: publica por personaje la base del juego, lo nuevo y el reposo (ExprCharPose). Aca:
//  1) por malla, UNA vez, se lee la paleta real (b6) y se empareja cada lugar con un hueso del personaje: la matriz del lugar j relativa a
//     la de la cabeza (Mh^-1 Mj) tiene que ser Bind_h H^-1 B_d Bind_d^-1 para el hueso d (reposo y pose del esqueleto, espacio del modelo);
//     el personaje se elige por la posicion de la cabeza en el mundo. El resultado va al cbuffer b12 (g_pesExprSlots) y al pozo de la barba;
//  2) cada cuadro se sube a t115 la diferencia nuevo x base^-1 de cada hueso, llevada al mundo con la matriz del nodo (y la del cuadro
//     anterior para los vectores de movimiento). El VS la aplica a cada influencia (pes_expr.hlsli PesBoneDeltaApply).
namespace {
struct Aff { float A[3][3]; float t[3]; };   // columna: out = A p + t
static void AffQ(const float* q, const float* t, Aff& o) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    o.A[0][0] = 1 - 2 * (y * y + z * z); o.A[0][1] = 2 * (x * y - z * w);     o.A[0][2] = 2 * (x * z + y * w);
    o.A[1][0] = 2 * (x * y + z * w);     o.A[1][1] = 1 - 2 * (x * x + z * z); o.A[1][2] = 2 * (y * z - x * w);
    o.A[2][0] = 2 * (x * z - y * w);     o.A[2][1] = 2 * (y * z + x * w);     o.A[2][2] = 1 - 2 * (x * x + y * y);
    for (int k = 0; k < 3; k++) o.t[k] = t[k];
}
static void AffIdentity(Aff& o) { memset(&o, 0, sizeof o); o.A[0][0] = o.A[1][1] = o.A[2][2] = 1.0f; }
static bool AffInv(const Aff& a, Aff& o) {
    const float (*m)[3] = a.A;
    const float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabsf(det) < 1e-12f) return false;
    const float id = 1.0f / det;
    o.A[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * id; o.A[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * id; o.A[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * id;
    o.A[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * id; o.A[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * id; o.A[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * id;
    o.A[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * id; o.A[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * id; o.A[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * id;
    for (int i = 0; i < 3; i++) o.t[i] = -(o.A[i][0] * a.t[0] + o.A[i][1] * a.t[1] + o.A[i][2] * a.t[2]);
    return true;
}
static void AffMul(const Aff& a, const Aff& b, Aff& o) {   // o = a * b (primero b)
    Aff r;
    for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) r.A[i][j] = a.A[i][0] * b.A[0][j] + a.A[i][1] * b.A[1][j] + a.A[i][2] * b.A[2][j]; r.t[i] = a.A[i][0] * b.t[0] + a.A[i][1] * b.t[1] + a.A[i][2] * b.t[2] + a.t[i]; }
    o = r;
}
static void AffPoint(const Aff& a, const float* p, float* o) { for (int i = 0; i < 3; i++) o[i] = a.A[i][0] * p[0] + a.A[i][1] * p[1] + a.A[i][2] * p[2] + a.t[i]; }
// (08/11, log del editor: "mallas emparejadas 0 / sin emparejar 3277") el float4x3 del VS va empaquetado por COLUMNAS: cada hueso son
// 3 registros (48 bytes) y el registro k es la columna k de la matriz (_m0k _m1k _m2k _m3k, asi lo lee el VS: dot con (p, 1)), o sea
// la fila k de la matriz de columna: out_k = A[k][0..2] . p + t[k]. Antes se leia traspuesta y la traslacion salia del registro 3,
// que es el primero del hueso SIGUIENTE: ninguna malla podia emparejar y los gestos por paleta no llegaban a nada
static void AffFromPalette(const float* r, Aff& o) {
    for (int k = 0; k < 3; k++) { for (int i = 0; i < 3; i++) o.A[k][i] = r[k * 4 + i]; o.t[k] = r[k * 4 + 3]; }
}
static void AffFromW(const float* W, Aff& o) { for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) o.A[i][j] = W[i * 4 + j]; o.t[i] = W[i * 4 + 3]; } }
static float AffDist(const Aff& a, const Aff& b) {
    float e = 0; for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) e += fabsf(a.A[i][j] - b.A[i][j]); e += 20.0f * fabsf(a.t[i] - b.t[i]); }
    return e;
}
static bool PoseHeadWorld(const ExprCharPose& P, float* o) { if (!P.worldOk) return false; Aff W; AffFromW(P.W, W); AffPoint(W, P.headT, o); return true; }
// el personaje de esta paleta: cabeza de la paleta en el mundo contra la cabeza publicada de cada personaje (a menos de 1 m el mas cerca;
// sin matriz de nodo, solo si es el unico)
static int PoseFor(const ExprCharPose* ps, int n, const float* hw) {
    int best = -1, wild = -1, nWild = 0; float bd = 1.0f;
    for (int i = 0; i < n; i++) {
        float h[3]; if (!PoseHeadWorld(ps[i], h)) { wild = i; nWild++; continue; }
        const float d = sqrtf((h[0] - hw[0]) * (h[0] - hw[0]) + (h[1] - hw[1]) * (h[1] - hw[1]) + (h[2] - hw[2]) * (h[2] - hw[2]));
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) return best;
    return (n == 1 && nWild == 1) ? wild : -1;
}
constexpr int D_LID_TL_ = 22, D_LID_BR_ = 25, D_EYE_L_ = 31, D_EYE_R_ = 32;   // kDef de exprbones.cpp (parpados 22..25: arriba izq, arriba der, abajo izq, abajo der; ojos 31 / 32)
struct SlotCap { ID3D11Buffer* st = nullptr; int id = 0, head = 0, maxIdx = 31; UINT frame = 0; int tries = 0; int lids[4] = { -1, -1, -1, -1 }; float c[32][4] = {}; };
std::mutex s_scmx; std::vector<SlotCap> s_scaps; int s_slotLogs = 0; std::atomic<int> s_slotOk{0}, s_slotFail{0}, s_slotAmb{0}, s_slotGeo{0};
}
static void SlotCapture(ID3D11DeviceContext* c, FaceMesh* m) {   // con s_mx tomado: copia la paleta entera (32 x float4x3) a lectura
    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) || !c1) return;
    ID3D11Buffer* b = nullptr; UINT first = 0, num = 0;
    c1->VSGetConstantBuffers1(6, 1, &b, &first, &num); c1->Release();
    if (!b) return;
    D3D11_BUFFER_DESC d; b->GetDesc(&d);
    const UINT a = first * 16, len = 32 * 48;
    if (a + len > d.ByteWidth) { b->Release(); return; }
    if (ID3D11Buffer* st = Staging(len)) {
        D3D11_BOX bx = { a, 0, 0, a + len, 1, 1 }; c->CopySubresourceRegion(st, 0, 0, 0, 0, b, 0, &bx);
        SlotCap L; L.st = st; L.id = m->id; L.head = m->head; L.maxIdx = m->maxIdx < 0 ? 31 : (std::min)(m->maxIdx, 31); L.frame = s_frame;
        for (int i = 0; i < 4; i++) L.lids[i] = m->lids[i];
        memcpy(L.c, m->slotC, sizeof L.c);
        std::lock_guard<std::mutex> lk(s_scmx); if (s_scaps.size() < 64) s_scaps.push_back(L); else st->Release();
    }
    b->Release();
}
static void SlotPresent(ID3D11DeviceContext* imm) {
    // sin s_scmx mientras se toma s_mx (SlotCapture toma s_scmx con s_mx tomado: el orden inverso trabaria): se saca la lista, se procesa y
    // lo que sigue en la GPU vuelve a la lista
    std::vector<SlotCap> caps; { std::lock_guard<std::mutex> lk(s_scmx); caps.swap(s_scaps); }
    if (caps.empty()) return;
    std::vector<SlotCap> again;
    ExprCharPose ps[32]; int np = -1;
    for (size_t i = 0; i < caps.size(); i++) {
        SlotCap& L = caps[i];
        D3D11_MAPPED_SUBRESOURCE mp; HRESULT hr = imm->Map(L.st, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mp);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING && ++L.tries < 60) { again.push_back(L); continue; }
        if (SUCCEEDED(hr)) {
            if (np < 0) np = ExprBonesPoses(ps, 32);
            const float* r = (const float*)mp.pData;
            uint8_t slot[32]; memset(slot, 255, sizeof slot); int matched = 0, who = -1; float worst = 0;
            if (np > 0 && L.head >= 0 && L.head <= 31) {
                Aff Mh; AffFromPalette(r + L.head * 12, Mh); Aff MhI;
                float hw[3];
                if (AffInv(Mh, MhI)) {
                    AffPoint(Mh, ps[0].headRestT, hw);
                    who = PoseFor(ps, np, hw);
                }
                if (who >= 0) {
                    const ExprCharPose& P = ps[who];
                    Aff K[kExprBones]; bool kOk[kExprBones] = {};
                    Aff Bh, BhI, H, HI; AffQ(P.headRestQ, P.headRestT, Bh); AffQ(P.headQ, P.headT, H);
                    if (AffInv(H, HI) && AffInv(Bh, BhI)) {
                        for (int d = 0; d < kExprBones; d++) {
                            const ExprCharPose::Bone& b = P.b[d]; if (!b.has) continue;
                            Aff B, R, RI; AffQ(b.bq, b.bt, B); AffQ(b.rq, b.rt, R); if (!AffInv(R, RI)) continue;
                            Aff t1, t2; AffMul(HI, B, t1); AffMul(t1, RI, t2); AffMul(Bh, t2, K[d]); kOk[d] = true;   // Bind_h H^-1 B_d Bind_d^-1
                        }
                        // (08/11 b, Agustin: con la camara libre muy cerca un ojo quedaba cerrado y hundido, "es por los huesos") reglas
                        // para que un lugar nunca tome el hueso equivocado:
                        //  - los globos de los ojos (skf_eye) no se emparejan en la piel: su giro es la mirada, no un gesto de la piel;
                        //  - los PARPADOS solo van en los lugares que la geometria de esta malla marco como parpado (lids, mismo lado y
                        //    arriba / abajo): tienen el mismo pivote que el ojo y entre ellos, y el emparejado por matriz los puede cruzar;
                        //  - cada hueso en un solo lugar (el de menor error); los demas quedan sin gesto
                        // (08/11 g, Agustin: "los parpados no reaccionan al movimiento como antes, al igual que la barba") el emparejado SIMPLE del
                        // mediodia (cbb4a21), el que se vio bien: cada lugar toma el hueso de pose mas parecida si el segundo queda lejos. Las reglas
                        // de la tarde (5ab438b..f312ab1: parpados solo en lugares de parpado, uno a uno, doble lectura, geometria) quedan detras de
                        // expr_paleta_blindaje = 1 para probarlas aparte
                        const bool blind = g_cfg.optExprBlindaje;
                        // (09/11, Agustin: "no hacen nada, no funciona"; log d1ec707 / 4e2b2c8: 0 mallas emparejadas, 27069 dudosos) con la cara
                        // QUIETA (editor, jugador sin gesto) todas las matrices relativas son ~identidad: la pose no distingue un hueso de otro
                        // (best y second casi iguales) y el emparejado simple queda vacio, con cualquier tope. Entonces, cuando la pose es dudosa,
                        // decide la GEOMETRIA: el hueso cuyo reposo esta mas cerca del centro de los vertices del lugar (< 2.5 cm; la pose solo
                        // confirma con error < 0.25). Parpados: solo en los lugares que la malla marco como parpado (mismo pivote que el ojo).
                        // La mandibula va al lugar con mas vertices en la parte baja del medio (menton / quijada: su reposo esta 8 cm atras)
                        int jawSlotS = -1;
                        if (!blind) for (int j = 0; j <= L.maxIdx; j++) {
                            if (j == L.head || L.c[j][3] <= 0.5f) continue;
                            if (fabsf(L.c[j][0]) > 0.035f || L.c[j][1] < 1.55f || L.c[j][1] > 1.632f || L.c[j][2] < 0.07f) continue;
                            if (jawSlotS < 0 || L.c[j][3] > L.c[jawSlotS][3]) jawSlotS = j;
                        }
                        if (!blind) for (int j = 0; j <= L.maxIdx; j++) {
                            if (j == L.head) continue;
                            Aff Mj, A; AffFromPalette(r + j * 12, Mj); AffMul(MhI, Mj, A);
                            float best = 1e9f, second = 1e9f; int bd = -1;
                            for (int d = 0; d < kExprBones; d++) { if (!kOk[d]) continue; const float e = AffDist(A, K[d]); if (e < best) { second = best; best = e; bd = d; } else if (e < second) second = e; }
                            if (!(bd >= 0 && best < 0.10f && second > 2.0f * best)) {   // pose dudosa (cara quieta): geometria
                                int lidQ = -1; for (int q = 0; q < 4; q++) if (L.lids[q] == j) lidQ = q;
                                int g = -1; float gmin = 1e9f;
                                if (j == jawSlotS && kOk[0]) { g = 0; gmin = 0.0f; }
                                else if (lidQ >= 0) { const int ld = D_LID_TL_ + lidQ; if (kOk[ld]) { g = ld; gmin = 0.0f; } }   // lids[]: arriba izq, arriba der, abajo izq, abajo der = kDef 22..25
                                else if (L.c[j][3] > 0.5f) for (int d = 1; d < kExprBones; d++) {
                                    if (!kOk[d] || d == D_EYE_L_ || d == D_EYE_R_ || (d >= D_LID_TL_ && d <= D_LID_BR_)) continue;
                                    const float* rt = P.b[d].rt; const float dx = L.c[j][0] - rt[0], dy = L.c[j][1] - rt[1], dz = L.c[j][2] - rt[2];
                                    const float dd = sqrtf(dx * dx + dy * dy + dz * dz); if (dd < gmin) { gmin = dd; g = d; }
                                }
                                if (g >= 0 && gmin < 0.025f && AffDist(A, K[g]) < 0.25f) { slot[j] = (uint8_t)g; matched++; worst = (std::max)(worst, AffDist(A, K[g])); s_slotGeo++; continue; }
                                // (09/11, log 05:47 con tope 0.20: en la malla de 30 lugares 12 de 18 pares iban al hueso 30, la glabela, con error
                                // 0.10..0.20) con la pose dudosa y la geometria sin candidato (lugar lejos de todo hueso de la cara: pelo, oreja,
                                // cuello) el lugar queda SIN gesto: tomar el hueso de pose mas parecida movia partes que no eran
                                if (L.c[j][3] > 0.5f) { if (bd >= 0 && best < 0.25f) s_slotAmb++; continue; }
                            }
                            // (08/11 g, log 15:43: 15 lugares "emparejados" con error 0.241, justo bajo el tope de 0.25, en cada malla de 30 lugares, y
                            // 4 de 27 en las demas: pares como 17>0 o 7>4 que no eran) un emparejado real da un error chico (0.00..0.05 en los
                            // de la mandibula); con 0.25 entraban huesos equivocados y cada pieza movia partes distintas (uniones rotas). Tope 0.10:
                            // lo dudoso queda sin gesto (la pieza sigue al juego) en vez de mover lo que no es
                            // (08/11 h, log de d1ec707: 0 mallas emparejadas / 1845, 27069 dudosos: con 0.10 tampoco entraban los pares BUENOS, que en el log de
                            // las 15:43 daban 0.09..0.145; los falsos 0.21..0.25) tope 0.20 por defecto, [PES21FX] expr_paleta_error para probar sin recompilar
                            const float tope = g_cfg.optExprPaletteErr > 0.001f ? g_cfg.optExprPaletteErr : 0.20f;
                            if (bd >= 0 && best < tope && second > 2.0f * best) { slot[j] = (uint8_t)bd; matched++; worst = (std::max)(worst, best); }
                            else if (bd >= 0 && best < 0.25f) s_slotAmb++;
                        }
                        float err[32]; int alt[32]; float altE[32]; for (int j = 0; j < 32; j++) { err[j] = 1e9f; alt[j] = -1; altE[j] = 1e9f; }
                        if (blind) {
                        // (08/11 f) la MANDIBULA (skf_jaw) esta muy atras (z 8 cm): el centro de sus vertices (menton, quijada) queda mas cerca
                        // de la papada o los labios de abajo. Su lugar es el que mas vertices tiene en la parte baja del medio de la cara (face_rig:
                        // jaw 312 vertices, el siguiente 151)
                        int jawSlot = -1;
                        for (int j = 0; j <= L.maxIdx; j++) {
                            if (j == L.head || L.c[j][3] <= 0.5f) continue;
                            if (fabsf(L.c[j][0]) > 0.035f || L.c[j][1] < 1.55f || L.c[j][1] > 1.632f || L.c[j][2] < 0.07f) continue;
                            if (jawSlot < 0 || L.c[j][3] > L.c[jawSlot][3]) jawSlot = j;
                        }
                        for (int j = 0; j <= L.maxIdx; j++) {
                            if (j == L.head) continue;
                            Aff Mj, A; AffFromPalette(r + j * 12, Mj); AffMul(MhI, Mj, A);
                            int lidQ = -1; for (int q = 0; q < 4; q++) if (L.lids[q] == j) lidQ = q;
                            // (08/11 f, Agustin: "se buguean, rompen, se muestran partes cualquier cosa"; log de fc4255a: 61 lugares emparejados
                            // y 421 sin emparejar, 218 dudosos) con la cara quieta TODAS las matrices relativas son casi la identidad: la pose sola
                            // no distingue un hueso de otro y el emparejado salia al azar o vacio, distinto en cada malla. Ahora manda la GEOMETRIA:
                            // el centro de los vertices de este lugar (espacio del modelo) contra el reposo de cada hueso; solo compiten los huesos
                            // a menos de 1.5 cm del mas cercano (y a menos de 5 cm), y la pose decide entre ellos o confirma
                            const bool hasC = L.c[j][3] > 0.5f && lidQ < 0;
                            float gd[kExprBones]; float gmin = 1e9f; int gbest = -1;
                            for (int d = 0; d < kExprBones; d++) {
                                gd[d] = 1e9f; if (!hasC || !kOk[d] || d == 0 || d == D_EYE_L_ || d == D_EYE_R_ || (d >= D_LID_TL_ && d <= D_LID_BR_)) continue;   // la mandibula va aparte (jawSlot)
                                const float* rt = P.b[d].rt; const float dx = L.c[j][0] - rt[0], dy = L.c[j][1] - rt[1], dz = L.c[j][2] - rt[2];
                                gd[d] = sqrtf(dx * dx + dy * dy + dz * dz); if (gd[d] < gmin) { gmin = gd[d]; gbest = d; }
                            }
                            const bool geo = hasC && gbest >= 0 && gmin < 0.05f;
                            float best = 1e9f, second = 1e9f; int bd = -1, sd2 = -1;
                            for (int d = 0; d < kExprBones; d++) {
                                if (!kOk[d] || d == D_EYE_L_ || d == D_EYE_R_) continue;
                                const bool isLid = d >= D_LID_TL_ && d <= D_LID_BR_;
                                if (isLid != (lidQ >= 0)) continue;   // lugar de parpado <-> hueso de parpado
                                if (isLid && d != D_LID_TL_ + (lidQ & 1) + 2 * (lidQ >> 1)) continue;   // lids = { arriba izq, arriba der, abajo izq, abajo der }
                                if (geo && d != 0 && gd[d] > gmin + 0.015f) continue;   // (08/11 f) lejos en el modelo: no puede ser este lugar
                                const float e = AffDist(A, K[d]); if (e < best) { second = best; sd2 = bd; best = e; bd = d; } else if (e < second) { second = e; sd2 = d; }
                            }
                            // (08/11 f) entre los cercanos la pose no decide (cara quieta): el mas cercano en el modelo
                            int gpick = (j == jawSlot && kOk[0]) ? 0 : gbest;   // la mandibula: su lugar propio
                            if (gpick >= 0 && (geo || gpick == 0) && bd >= 0 && best < 0.25f && !(second > 2.0f * best) && AffDist(A, K[gpick]) < 0.25f) { sd2 = bd == gpick ? sd2 : bd; second = best; bd = gpick; best = AffDist(A, K[gpick]); s_slotGeo++; }
                            const bool okLid = lidQ >= 0 && bd >= 0 && best < 0.25f;   // el parpado ya viene elegido por la geometria
                            // (08/11 c, Agustin: "vibra cerca de la boca tambien": rayas en las comisuras) dos huesos con la misma pose (skf_lip_t_c /
                            // skf_lip_volume, que el juego mueve juntos) dejaban el lugar SIN emparejar en unas mallas y emparejado en otras: la piel y
                            // la boca 3D o la barba se movian distinto en la union. Si los dos candidatos son el mismo lugar para el juego (sus poses
                            // coinciden), se toma el mejor igual
                            const bool twin = sd2 >= 0 && kOk[sd2] && AffDist(K[bd], K[sd2]) < 0.06f && (!geo || (bd != 0 && sd2 != 0 && fabsf(gd[sd2] - gd[bd]) < 0.006f));   // (08/11 f) con la cara quieta todas las poses coinciden: gemelos solo si tambien estan en el mismo lugar del modelo
                            const bool geoOk = (geo || gpick == 0) && gpick >= 0 && bd == gpick && best < 0.25f;
                            if (bd >= 0 && (okLid || geoOk || (best < 0.25f && (second > 2.0f * best || twin)))) { slot[j] = (uint8_t)bd; err[j] = best; if (twin) { alt[j] = sd2; altE[j] = second; } }
                            else if (bd >= 0 && best < 0.25f) s_slotAmb++;
                        }
                        for (int j = 0; j < 32; j++) if (slot[j] != 255) for (int k = 0; k < 32; k++) if (k != j && slot[k] == slot[j] && err[k] <= err[j]) {
                            // el gemelo pierde su primer hueso: toma el segundo si nadie lo usa
                            bool used = alt[j] < 0; for (int q = 0; q < 32 && !used; q++) if (slot[q] == alt[j]) used = true;
                            if (!used) { slot[j] = (uint8_t)alt[j]; err[j] = altE[j]; alt[j] = -1; } else slot[j] = 255;
                            break; }
                        for (int j = 0; j < 32; j++) if (slot[j] != 255) { matched++; worst = (std::max)(worst, err[j]); }
                        }   // blind
                    }
                }
            }
            imm->Unmap(L.st, 0);
            FaceMesh* m = nullptr;
            { std::lock_guard<std::mutex> lm(s_mx); for (auto& kv : s_meshes) if (kv.second->id == L.id) { m = kv.second; break; }
              bool confirmed = false;
              if (m && matched >= 3 && !m->slotsOk) {
                  confirmed = !g_cfg.optExprBlindaje || (m->slotPendOk && memcmp(m->slotPend, slot, sizeof slot) == 0);   // (08/11 g) doble lectura solo con blindaje
                  memcpy(m->slotPend, slot, sizeof slot); m->slotPendOk = true;
                  if (!confirmed) m->slotCap = s_frame - 30;   // la segunda lectura enseguida (~15 cuadros)
              }
              if (m && confirmed) {
                  memcpy(m->slotBone, slot, sizeof slot); m->slotsOk = true;
                  for (int j = 0; j < 32; j++) { UINT& u = m->cbv[28 + j / 4]; u &= ~(0xFFu << ((j & 3) * 8)); u |= (UINT)slot[j] << ((j & 3) * 8); }
                  // cbuffers nuevos con la tabla (los viejos no se liberan: otro hilo puede tenerlos en la mano)
                  if (ID3D11Buffer* nb = MakeCBRaw(m->cbv)) m->cb = nb;
                  if (m->cbNoShape) { UINT z[kCbU]; memcpy(z, m->cbv, sizeof z); z[27] = 0; if (ID3D11Buffer* nb = MakeCBRaw(z)) m->cbNoShape = nb; }
                  if (m->cbTeeth) { UINT z[kCbU]; memcpy(z, m->cbv, sizeof z); z[24] = 1; z[25] = 0; z[26] = 0; z[27] = 0; if (ID3D11Buffer* nb = MakeCBRaw(z)) m->cbTeeth = nb; }
                  m->idBuf = nullptr;   // la barba: se rearma con la tabla en la proxima copia al pozo
                  s_slotOk++;
              } else if (m && matched < 3) s_slotFail++;
            }
            if (s_slotLogs < 120) {
                s_slotLogs++;
                char line[900]; int n = snprintf(line, sizeof line, "paleta: malla %d (cabeza %d, %d lugares): personaje %s, %d lugares emparejados (peor %.3f):", L.id, L.head, L.maxIdx + 1, who >= 0 ? "si" : (np > 0 ? "NO (cabeza lejos)" : "ninguno publicado"), matched, worst);
                for (int j = 0; j <= L.maxIdx && n < (int)sizeof line - 12; j++) if (slot[j] != 255) n += snprintf(line + n, sizeof line - n, " %d>%d", j, slot[j]);
                Log("%s", line);
            }
        }
        L.st->Release();
    }
    if (!again.empty()) { std::lock_guard<std::mutex> lk(s_scmx); for (const SlotCap& L : again) if (s_scaps.size() < 64) s_scaps.push_back(L); else L.st->Release(); }
}
// tabla para el VS (t115): [0] = (personajes, 0, 0, 0), [1] = (reposo de sk_head, 0); por personaje 1 + 33 x 6 float4:
// (cabeza en el mundo, 1 = valida) y por hueso 3 float4 de la diferencia de este cuadro + 3 del anterior (fila k = imagen del eje k, w = traslacion)
namespace {
constexpr int kRowZonas = 421;   // (08/11 d) filas 421 / 422: x350 = 3 'Por huesos: control total' (ganancia por zona y topes)
static std::atomic<int> s_bdClamp{0}, s_bdClampR{0};   // (08/11 b) huesos recortados por el tope
constexpr int kBdStride = 1 + kExprBones * 6, kBdChars = 32, kBdF4 = 2 + kBdChars * kBdStride;
ID3D11Buffer* s_bdBuf = nullptr; ID3D11ShaderResourceView* s_bdSrv = nullptr; bool s_bdAny = false;
struct PrevD { unsigned frame = 0; float D[kExprBones][12]; bool has[kExprBones]; };
std::unordered_map<const void*, PrevD> s_prevD;
std::atomic<int> s_bdUploads{0}, s_bdCharsN{0}, s_bdMoved{0};
static void PackAff(const Aff& a, float* o) {   // 3 float4: fila k = columna k de A (imagen del eje k) + traslacion en w
    for (int k = 0; k < 3; k++) { o[k * 4 + 0] = a.A[0][k]; o[k * 4 + 1] = a.A[1][k]; o[k * 4 + 2] = a.A[2][k]; o[k * 4 + 3] = a.t[k]; }
}
}
static void DeltaUpload(ID3D11DeviceContext* imm) {
    // (09/11 g) tambien sin paleta (expr_paleta = 0): se publican solo la cabeza y la edad de cada personaje (arrugas por edad,
    // pes_wrdepth.hlsli) con todas las diferencias en identidad (el VS no cambia nada de la malla)
    if (!g_dev.dev) return;
    const bool pal = g_cfg.optExprPalette;
    if (!s_bdBuf) {
        D3D11_BUFFER_DESC d{}; d.ByteWidth = kBdF4 * 16; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_SHADER_RESOURCE; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_dev.dev->CreateBuffer(&d, nullptr, &s_bdBuf))) { s_bdBuf = nullptr; return; }
        D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; v.Buffer.NumElements = kBdF4;
        if (FAILED(g_dev.dev->CreateShaderResourceView(s_bdBuf, &v, &s_bdSrv))) { s_bdSrv = nullptr; s_bdBuf->Release(); s_bdBuf = nullptr; return; }
    }
    ExprCharPose ps[kBdChars]; const int np = ExprBonesPoses(ps, kBdChars);
    static std::vector<float> f; f.assign((size_t)kBdF4 * 4, 0.0f);
    f[0] = (float)np;
    if (np > 0) for (int k = 0; k < 3; k++) f[4 + k] = ps[0].headRestT[k];
    int moved = 0;
    for (int i = 0; i < np; i++) {
        const ExprCharPose& P = ps[i]; float* base = f.data() + (size_t)(2 + i * kBdStride) * 4;
        float hw[3]; const bool hv = PoseHeadWorld(P, hw);
        // (09/11 g) w = 1 + edad del jugador (0 = no se sabe; >= 0.5 sigue siendo "posicion valida" para PesBoneChar)
        // (10/11, Agustin: "la frente o las patas de gallo no son iguales para todos") + rasgo propio del jugador en la fraccion: (1 + 2 n) / 32,
        // n = 1..7 sale del id (0 = no se sabe); el PS lo separa de la edad (pes_wrdepth.hlsli PesWrAgeK)
        int age = 0; float wrSeed = 0.0f; { const int sl = CharSlotForSkel(P.skel); CharInfo ci; if (sl >= 0 && CharGet(sl, ci) && ci.known && ci.key) { age = CharPlayerAge(ci.key);
            if (age > 0) { uint32_t hsh = (uint32_t)(ci.key >> 32) * 2654435761u; wrSeed = (1.0f + 2.0f * (float)(1 + (hsh >> 13) % 7)) / 32.0f; } } }
        base[0] = hv ? hw[0] : 0; base[1] = hv ? hw[1] : 0; base[2] = hv ? hw[2] : 0; base[3] = hv ? 1.0f + (float)age + wrSeed : 0.0f;
        Aff W, WI; bool wOk = false; if (P.worldOk) { AffFromW(P.W, W); wOk = AffInv(W, WI); }
        PrevD& pv = s_prevD[P.skel]; const bool pvOk = pv.frame + 1 == P.frame || pv.frame == P.frame;   // el cuadro anterior de este personaje (si no, la de ahora)
        PrevD cur; cur.frame = P.frame;
        for (int d = 0; d < kExprBones; d++) {
            float* o = base + 4 + d * 24; Aff Dw; AffIdentity(Dw); cur.has[d] = false;
            const ExprCharPose::Bone& b = P.b[d];
            // (08/11 g, Agustin eligio "toda la expresion (juego + mod)") con control total (x350 = 3) la ganancia por zona y el tope se
            // aplican sobre la expresion ENTERA respecto de la cara neutra de este jugador (b.zq / zt), tambien en los huesos que el mod no
            // toco este cuadro (la animacion del juego se escala); en los otros modos, solo sobre lo que agrega el mod
            const bool fullZ = g_cfg.live[350][0] > 2.5f && b.hasZ;
            if (pal && b.has && (b.moved || fullZ)) {
                Aff N, B, BI, Dm; AffQ(b.nq, b.nt, N); AffQ(b.bq, b.bt, B);
                // (08/11 b) tope por hueso: el gesto del mod nunca corre un hueso de la cara mas de 40 grados ni 2 cm de lo que dejo el juego
                // (si algo sale de rango -lectura rara, cara nueva- se recorta en vez de tirar la piel)
                // (08/11 d, Agustin: "se podria crear otra mas extensiva y con mayor control?") x350 = 3 "Por huesos: control total": la
                // diferencia de cada hueso (giro eje-angulo y corrimiento) se escala por zona (fila 421: x mandibula, y labios y comisuras,
                // z parpados y ojeras, w cejas y frente; fila 422: x mejillas y nariz, y todo junto) y el tope sale de la fila 422 (z grados,
                // w mm). Con los otros modos: ganancia 1 y tope 40 grados / 2 cm
                float nq2[4] = { b.nq[0], b.nq[1], b.nq[2], b.nq[3] }, nt2[3] = { b.nt[0], b.nt[1], b.nt[2] };
                { const int cz = ExprZoneScale(d, fullZ ? b.zq : b.bq, fullZ ? b.zt : b.bt, nq2, nt2); if (cz & 1) s_bdClampR++; if (cz & 2) s_bdClamp++; }
                AffQ(nq2, nt2, N);
                if (AffInv(B, BI)) { AffMul(N, BI, Dm); if (wOk) { Aff t1; AffMul(W, Dm, t1); AffMul(t1, WI, Dw); } else Dw = Dm; cur.has[d] = true; moved++; }
            }
            PackAff(Dw, o);
            if (pvOk && pv.has[d]) memcpy(o + 12, pv.D[d], 12 * sizeof(float)); else memcpy(o + 12, o, 12 * sizeof(float));
            memcpy(cur.D[d], o, 12 * sizeof(float));
        }
        pv = cur;
    }
    if (s_prevD.size() > 256) s_prevD.clear();
    D3D11_MAPPED_SUBRESOURCE mp;
    if (SUCCEEDED(imm->Map(s_bdBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) { memcpy(mp.pData, f.data(), f.size() * sizeof(float)); imm->Unmap(s_bdBuf, 0); s_bdAny = true; s_bdUploads++; s_bdCharsN = np; s_bdMoved = moved; }
    static UINT lastLog = 0;
    if (s_frame - lastLog >= 1800 && s_slotLogs < 400) { lastLog = s_frame; Log("paleta: %d personajes publicados, %d huesos con gesto este cuadro, mallas emparejadas %d / sin emparejar %d, lugares dudosos %d (decididos por la geometria %d), huesos recortados por el tope: giro %d / corrimiento %d [x350 %.0f]", np, moved, s_slotOk.load(), s_slotFail.load(), s_slotAmb.load(), s_slotGeo.load(), s_bdClampR.exchange(0), s_bdClamp.exchange(0), g_cfg.live[350][0]); }
}
// (08/11 d) ganancia por zona y topes por hueso del gesto del mod (x350 = 3 "control total": filas 421 / 422; otros modos: ganancia 1,
// tope 40 grados / 2 cm). Cambia n (giro nq / posicion nt) para que la diferencia con la base del juego b quede escalada y recortada.
// Devuelve 1 si se recorto el giro, 2 el corrimiento (3 ambos). La usan la paleta (DeltaUpload) y la escritura directa al esqueleto (expr_paleta = 0)
int ExprZoneScale(int d, const float* bq, const float* bt, float* nq2, float* nt2) {
    int clip = 0;   // 1 = giro recortado, 2 = corrimiento recortado
    const bool full = g_cfg.live[350][0] > 2.5f;
    if (!full && !g_cfg.optExprBlindaje) return 0;   // (08/11 g) sin control total ni blindaje: como al mediodia, sin tope ni ganancia
    float g = 1.0f, kMax = 0.70f, kMaxT = 0.02f;
    if (full) {
        const float* A = g_cfg.live[kRowZonas]; const float* Bz = g_cfg.live[kRowZonas + 1];
        const float zg = (d == 0) ? A[0] : (d <= 6 || (d >= 9 && d <= 12)) ? A[1] : (d >= 20 && d <= 25) ? A[2] : (d >= 26 && d <= 30) ? A[3] : Bz[0];
        g = std::clamp(zg, 0.0f, 2.0f) * std::clamp(Bz[1], 0.0f, 2.0f);
        kMax = std::clamp(Bz[2], 5.0f, 90.0f) / 57.29578f; kMaxT = std::clamp(Bz[3], 1.0f, 50.0f) * 0.001f;
    }
    float dq[4], bc[4] = { -bq[0], -bq[1], -bq[2], bq[3] };
    dq[3] = nq2[3] * bc[3] - nq2[0] * bc[0] - nq2[1] * bc[1] - nq2[2] * bc[2];
    dq[0] = nq2[3] * bc[0] + nq2[0] * bc[3] + nq2[1] * bc[2] - nq2[2] * bc[1];
    dq[1] = nq2[3] * bc[1] - nq2[0] * bc[2] + nq2[1] * bc[3] + nq2[2] * bc[0];
    dq[2] = nq2[3] * bc[2] + nq2[0] * bc[1] - nq2[1] * bc[0] + nq2[2] * bc[3];
    if (dq[3] < 0) for (float& v : dq) v = -v;
    const float sn = sqrtf(dq[0] * dq[0] + dq[1] * dq[1] + dq[2] * dq[2]), ang = 2.0f * atan2f(sn, dq[3]);
    float tgt = ang * g; if (tgt > kMax) { tgt = kMax; clip |= 1; }
    if (sn > 1e-6f && fabsf(tgt - ang) > 1e-6f) {   // n = dq escalado * b
        const float h = 0.5f * tgt, k = sinf(h) / sn, e[4] = { dq[0] * k, dq[1] * k, dq[2] * k, cosf(h) };
        nq2[3] = e[3] * bq[3] - e[0] * bq[0] - e[1] * bq[1] - e[2] * bq[2];
        nq2[0] = e[3] * bq[0] + e[0] * bq[3] + e[1] * bq[2] - e[2] * bq[1];
        nq2[1] = e[3] * bq[1] - e[0] * bq[2] + e[1] * bq[3] + e[2] * bq[0];
        nq2[2] = e[3] * bq[2] + e[0] * bq[1] - e[1] * bq[0] + e[2] * bq[3];
    }
    float dx = (nt2[0] - bt[0]) * g, dy = (nt2[1] - bt[1]) * g, dz = (nt2[2] - bt[2]) * g; const float dl = sqrtf(dx * dx + dy * dy + dz * dz);
    // (08/11 f) el tope de corrimiento cuenta aparte lo que el hueso se corre por girar alrededor de otro punto: los labios, el menton y
    // la papada giran con la mandibula (pivote a 6..12 cm) y al abrir la boca 20 grados se corren ~3 cm sin que nada este mal. El log
    // de fc4255a mostraba 146..426 huesos recortados cada 30 s con x350 = 2 (tope 2 cm): eran esos. Brazo de palanca maximo 12 cm
    const float kMaxTr = kMaxT + 0.12f * (std::min)(tgt, kMax);
    if (dl > kMaxTr) { const float k = kMaxTr / dl; dx *= k; dy *= k; dz *= k; clip |= 2; }
    nt2[0] = bt[0] + dx; nt2[1] = bt[1] + dy; nt2[2] = bt[2] + dz;
    return clip;
}
ID3D11ShaderResourceView* ExprBoneDeltaSrv() { return s_bdAny ? s_bdSrv : nullptr; }   // (09/11 g) tambien sin paleta: cabeza y edad (diferencias en identidad)

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
    SlotPresent(imm);   // (08/11) gestos en la paleta: emparejar lugares de la paleta con huesos
    DeltaUpload(imm);   // (08/11) y subir las diferencias de este cuadro al VS (t115)
    {   // con el candado tomado: los dibujos (otros hilos) leen state / cb y pueden vaciar la tabla. Los Map son DO_NOT_WAIT.
        std::lock_guard<std::mutex> lk(s_mx);
        s_frame++;
        WrinklePresent(imm);   // (08/11) pesos de arrugas del juego suavizados
        RetiredPresent();   // (04/11 i)
        {   // (04/11) volcado de las mallas de la cara: w354 sube de 0 a 1 -> se vacia la tabla y las que se vuelvan a dibujar en ~10 s se vuelcan
            static bool was = false; const bool now = g_cfg.live[354][3] > 0.5f;
            if (now && !was) {
                for (auto& kv : s_meshes) FreeMesh(kv.second);
                s_meshes.clear(); s_pending.clear(); s_bsrc.clear(); s_bsrcBytes = 0;
                s_dumpUntil.store(s_frame + 600, std::memory_order_relaxed);
                Log("expresiones: volcado de las mallas de la cara pedido (w354 = 1): se releen las que se dibujen en los proximos 10 s -> PES21FX\\dump\\caras\\");
            }
            was = now;
        }
        // (01/11) primero las mallas que se estan dibujando ahora (las pasajeras que ya no se dibujan van al final), 16 por cuadro
        std::stable_sort(s_pending.begin(), s_pending.end(), [](FaceMesh* a, FaceMesh* b) { return a->lastFrame > b->lastFrame; });
        for (size_t i = 0; i < s_pending.size() && i < 16; i++) Step(imm, s_pending[i]);
        s_pending.erase(std::remove_if(s_pending.begin(), s_pending.end(), [](FaceMesh* m) { return m->state >= 3; }), s_pending.end());
        // (04/11 c) barbas que se leyeron antes que su piel: se reintenta cada ~2 s (hasta 15 veces) mientras se sigan dibujando
        if ((s_frame % 120) == 7) {
            int n = 0;
            for (auto& kv : s_meshes) {
                FaceMesh* b = kv.second;
                if (b->state != 5 || b->beardPs.empty() || s_frame - b->lastFrame > 120 || n >= 2) continue;
                n++;
                if (BeardBind(b, b->beardPs)) b->state = 3;
                else if (++b->beardTries >= 15) { b->state = 4; b->why = "pelo: no hay piel de cara debajo"; b->beardPs.clear(); b->beardPs.shrink_to_fit(); }
            }
            // (04/11 e) barba enlazada a una piel que ya no se dibuja (otro jugador, otra pantalla) mientras la barba si: se vuelve a enlazar
            // a la piel que se dibuja ahora (hasta 10 veces por barba)
            int nr = 0;
            for (auto& kv : s_meshes) {
                FaceMesh* b = kv.second;
                if (!b->beard || b->state != 3 || b->beardPs.empty() || b->beardUsed.empty() || s_frame - b->lastFrame > 4 || b->beardRebinds >= 10 || nr >= 2) continue;
                bool live = false;
                for (int g : b->beardUsed) { auto it = s_geoDrawn.find(g); if (it != s_geoDrawn.end() && s_frame - it->second.frame <= 120) { live = true; break; } }
                // (04/11 l, log 11:27: las barbas 3D de Messi 106 / 107 quedaron en pieles de la escena anterior, "OJO: ninguna piel
                // dibujada cerca", y como esas pieles se seguian dibujando en otro lado nunca se re-enlazaban) la que se enlazo sin piel
                // dibujada junto tambien se vuelve a enlazar
                if (live && !b->beardOjo) continue;
                nr++; b->beardRebinds++;
                Log("expresiones: barba %d: su piel no se dibuja hace mas de 2 s, se vuelve a enlazar (vez %d)", b->id, b->beardRebinds);
                BeardBind(b, b->beardPs);
            }
        }
        if (s_beardDraws || s_beardCopies) { static UINT lastB = 0; if (s_frame - lastB >= 3600) { lastB = s_frame; int nSrc = 0; for (auto& kv : s_meshes) if (kv.second->beardSrc) nSrc++;
            Log("expresiones: barba por huesos: %d dibujos de barba, %d paletas de piel copiadas (~60 s; %d mallas de piel marcadas, %d geometrias; fallas: pozo %d, id %d, b5/b6 %d, rango %d)", s_beardDraws, s_beardCopies, nSrc, (int)s_geoUsed.size(), s_poolFail[0], s_poolFail[1], s_poolFail[2], s_poolFail[3]); char gl[400]; int gk = 0; gl[0] = 0; for (int i = 0; i < s_geoSeenN && gk < 360; i++) gk += snprintf(gl + gk, sizeof gl - gk, " %d(%u v, malla %d, %d%s)", s_geoSeen[i].geo, s_geoSeen[i].nv, s_geoSeen[i].id, s_geoSeen[i].n, s_geoSeen[i].mk ? ", marcada" : "");
            Log("expresiones: barba por huesos: pieles de cara dibujadas por geometria:%s; sin geometria %d dibujos (ultima %d v); geometrias de la barba: %d", gl[0] ? gl : " ninguna", s_skinNoGeo, s_skinNoGeoNv, (int)s_geoUsed.size());
            s_geoSeenN = 0; s_skinNoGeo = 0;
            s_beardDraws = s_beardCopies = 0; for (int& f : s_poolFail) f = 0; } }
    }
    static int frames = 0;
    if (++frames >= 3600) {
        frames = 0;
        const int b = s_binds.exchange(0), bn = s_bindsNone.exchange(0);
        const int bw = s_bindsWait.exchange(0), bd = s_bindsDisc.exchange(0), bnew = s_bindsNew.exchange(0);
        if (b || bn) { std::lock_guard<std::mutex> lk(s_mx);
            Log("expresiones: %d mallas de cara (cabeza en 0: %d, en 1: %d, en otro: %d, descartadas o no cara %d); dibujos con dato %d, sin dato %d (en lectura %d, descartadas o no cara %d, nuevas %d, apagado %d); echadas por viejas %d; ganancia %.2f",
                (int)s_meshes.size(), s_hist[0], s_hist[1], s_hist[2], s_hist[3], b, bn, bw, bd, bnew, bn - bw - bd - bnew, s_evicted, g_cfg.live[340][1]);
            Log("expresiones: memoria de formas %u KB, barbas %u KB; reusadas %d, tope %d, buffers por liberar %d", (unsigned)(s_shapeData.size() * 4 >> 10), (unsigned)(s_beardData.size() * 4 >> 10), s_dedupHits, s_capHits, (int)s_retired.size());   // (04/11 i)
            int np = 0; for (auto& kv : s_shapes) if (kv.second.off >= 0) np++;
            const int sd = s_shapeDraws.exchange(0);
            if (np || sd) Log("expresiones: gestos esculpidos: %d mallas con formas, %d dibujos (sonrisa %.2f grito %.2f enojo %.2f asombro %.2f puchero %.2f, perfiles %s)", np, sd,
                g_cfg.live[355][1], g_cfg.live[356][0], g_cfg.live[355][0], g_cfg.live[355][2], g_cfg.live[356][1], g_cfg.live[356][3] > 0.5f ? "si" : "no");
            if (s_gateOn || s_gateOff) { Log("expresiones: mallas pegadas (pestanas de otro buffer): %d dibujos con formas, %d sin (lejos de su piel)", s_gateOn, s_gateOff); s_gateOn = s_gateOff = 0; }
            if (s_teethOn) { Log("expresiones: boca de la cara esculpida (dientes con la sonrisa): %d dibujos", s_teethOn); s_teethOn = 0; } }
    }
}

}   // namespace fx
