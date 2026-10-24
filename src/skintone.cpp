// PES21 FX - tono de piel por jugador (24/10). Primero un diagnostico para elegir como llega el tono de cada jugador a sus
// dibujos (la piel del cuerpo es una textura por cada uno de los 6 colores del juego, compartida por todos los que tienen
// ese color: hace falta saber, en cada dibujo, de que jugador es). Cada 20 s (hasta 10 cuadros con personajes por sesion)
// se anota UN cuadro entero de dibujos de personajes (hasta 60 lineas): orden, PS / VS, indices, bloques de constantes (PS b4 = material, VS b5 = objeto, VS b6 =
// huesos: buffer + primer constante) y texturas t0 / t1 del PS. Con eso se ve si la cara sale antes que el cuerpo, si los
// bloques se repiten de un cuadro al otro (servirian de clave del jugador) y si el material o la textura son propios de
// cada jugador.
//
// El tono (24/10): 30 tonos (los 6 del juego quedan en 3, 8, 13, 18, 23 y 28; 4 intermedios entre cada par, 2 mas claros y 2
// mas oscuros) y un matiz (-10 oliva .. +10 rosado). La piel se tine en los shaders (piel del cuerpo aa7d1833, cara 5ce3ed87
// y ce188ae1) con ganancia = tono elegido / tono del color del juego del jugador (los 6 colores del juego: tabla 0x14352a910,
// los mismos de la paleta del menu). Por jugador en PES21FX\piel_jugadores.ini. Vista previa: en el menu Fisico del juego
// (pagina "Piel") el tinte del jugador editado va en x185..z185 (w185 = 1); en los partidos falta saber de quien es cada
// dibujo (para eso es el diagnostico de arriba).
//
// (24/10 c) Dos cambios. 1) Color realista: la paleta del juego solo cambia el brillo (los 6 colores tienen el mismo matiz y
// la misma saturacion en Oklab), por eso la ganancia por canal se veia como "mas brillo". Ahora cada tono tiene la
// luminosidad de la escala del juego y el matiz / saturacion de una escala de piel medida (claro = menos saturado y mas
// rosado-amarillo, medio = oliva / canela, oscuro = marron rojizo): por jugador va (kL, m0, m1) para pes_skintone.hlsli
// (luminosidad x kL, matiz y saturacion girados y escalados en Oklab), tambien al velo de borde del material.
// 2) En los partidos: cada cuadro se arma una tabla (t120: 194..254) con la posicion de cada jugador con tono propio (raiz
// del personaje: SceneCharacterNode + 0x40 = matriz de mundo, 0x1419c61e0 la devuelve; traslacion en + 0x70) y su (kL, m0,
// m1). Los VS de la piel (1c02a92a) y de la cabeza (faab87ee) eligen la entrada mas cercana al hueso 0 del dibujo en el
// plano del piso y la pasan al PS. El color del juego del jugador se guarda (juego = 1..6) cada vez que se lo ve en el menu
// Fisico; la vista previa del menu Fisico usa la misma tabla.
#include "core.h"
#include <d3d11_1.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace fx {

// ---------------------------------------------------------------- paleta y ganancia
static const unsigned char kGameSkin[6][3] = { { 255, 210, 179 }, { 224, 179, 146 }, { 194, 149, 116 }, { 163, 120, 90 }, { 133, 94, 66 }, { 102, 69, 46 } };
static float Lin(float c) { c /= 255.0f; return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
static void GameLin(int idx, float o[3]) { idx = std::clamp(idx, 1, 6) - 1; for (int k = 0; k < 3; k++) o[k] = Lin(kGameSkin[idx][k]); }
// tono 1..30 en lineal (el 3 + 5 * (i - 1) es el color i del juego)
static void ToneLin(int t, float o[3]) {
    t = std::clamp(t, 1, kSkinTones);
    const float p = (float)(t - 3) / 5.0f;   // posicion en la escala del juego (0 = color 1, 5 = color 6)
    int a = (int)floorf(p); a = std::clamp(a, 0, 4);
    const float f = p - (float)a;            // fuera de [0, 5] extrapola (mas claro / mas oscuro)
    float A[3], B[3]; GameLin(a + 1, A); GameLin(a + 2, B);
    for (int k = 0; k < 3; k++) o[k] = std::clamp(A[k] + (B[k] - A[k]) * f, 0.004f, 1.0f);
}
// matiz: + rosado (mas rojo), - oliva (menos rojo y azul, algo mas verde); conserva la luminancia
static void ApplyHue(float c[3], int m) {
    const float l0 = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
    if (m > 0) { c[0] *= 1.0f + 0.03f * m; c[1] *= 1.0f - 0.01f * m; c[2] *= 1.0f - 0.005f * m; }
    else if (m < 0) { const float k = (float)-m; c[0] *= 1.0f - 0.02f * k; c[1] *= 1.0f + 0.01f * k; c[2] *= 1.0f - 0.03f * k; }
    const float l1 = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
    if (l1 > 1e-5f) for (int k = 0; k < 3; k++) c[k] = std::clamp(c[k] * l0 / l1, 0.0f, 1.0f);
}
bool SkinToneGain(int gameIdx, int tono, int matiz, float out[3]) {
    out[0] = out[1] = out[2] = 1.0f;
    if (tono <= 0 && matiz == 0) return false;
    float base[3], tgt[3]; GameLin(gameIdx, base);
    if (tono > 0) ToneLin(tono, tgt); else for (int k = 0; k < 3; k++) tgt[k] = base[k];
    ApplyHue(tgt, std::clamp(matiz, -10, 10));
    for (int k = 0; k < 3; k++) out[k] = std::clamp(tgt[k] / std::max(base[k], 1e-4f), 0.25f, 4.0f);
    return true;
}
// (24/10 c) tono en Oklab: luminosidad de la escala del juego (posiciones 3, 8, ..., 28 = colores 1..6; afuera se extrapola
// con el paso de los extremos) y croma / matiz de una escala de piel medida, por posicion
static void GameOklabL(int idx, float* L) {
    float c[3]; GameLin(idx, c);
    float l = 0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2];
    float m = 0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2];
    float s = 0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2];
    l = cbrtf(l); m = cbrtf(m); s = cbrtf(s);
    *L = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
}
static void ToneOklab(int t, float* L, float* C, float* hDeg) {
    t = std::clamp(t, 1, kSkinTones);
    const float p = (float)(t - 3) / 5.0f;   // 0 = color 1 del juego, 5 = color 6
    int a = std::clamp((int)floorf(p), 0, 4); const float f = p - (float)a;
    float LA, LB; GameOklabL(a + 1, &LA); GameOklabL(a + 2, &LB);
    *L = std::clamp(LA + (LB - LA) * f, 0.15f, 0.99f);
    // croma y matiz (grados) por posicion: muy claro poco saturado, medio oliva / canela, oscuro marron rojizo
    static const float kT[8] = { 1, 3, 8, 13, 18, 23, 28, 30 };
    static const float kC[8] = { 0.036f, 0.046f, 0.058f, 0.066f, 0.066f, 0.060f, 0.050f, 0.042f };
    static const float kH[8] = { 62.0f, 63.0f, 68.0f, 70.0f, 63.0f, 54.0f, 47.0f, 48.0f };
    int j = 0; while (j < 6 && (float)t > kT[j + 1]) j++;
    const float u = std::clamp(((float)t - kT[j]) / (kT[j + 1] - kT[j]), 0.0f, 1.0f);
    *C = kC[j] + (kC[j + 1] - kC[j]) * u; *hDeg = kH[j] + (kH[j + 1] - kH[j]) * u;
}
// de tono "base" (el color del juego del jugador, en la posicion 5 * juego - 2) a tono elegido + matiz: (kL, m0, m1)
bool SkinToneParams(int gameIdx, int tono, int matiz, float k, float out[3]) {
    out[0] = 1.0f; out[1] = 1.0f; out[2] = 0.0f;
    if ((tono <= 0 && matiz == 0) || gameIdx < 1) return false;
    const int base = 5 * std::clamp(gameIdx, 1, 6) - 2;
    float LB, CB, HB, LT, CT, HT;
    ToneOklab(base, &LB, &CB, &HB);
    ToneOklab(tono > 0 ? tono : base, &LT, &CT, &HT);
    float kL = LT / std::max(LB, 0.05f), kC = CT / std::max(CB, 0.005f), dh = HT - HB;
    const int mz = std::clamp(matiz, -10, 10);
    dh -= 1.5f * (float)mz;                                   // + rosado (mas rojo), - oliva (mas amarillo-verde)
    kC *= mz >= 0 ? 1.0f + 0.012f * (float)mz : 1.0f + 0.015f * (float)mz;
    k = std::clamp(k, 0.0f, 1.5f);
    kL = 1.0f + (kL - 1.0f) * k; kC = std::max(0.0f, 1.0f + (kC - 1.0f) * k); dh *= k;
    const float r = dh * 3.14159265f / 180.0f;
    out[0] = kL; out[1] = kC * cosf(r); out[2] = kC * sinf(r);
    return true;
}
void SkinToneSwatch(int tono, unsigned char rgb[3]) {   // para rotulos: el tono en sRGB
    float c[3]; ToneLin(tono, c);
    for (int k = 0; k < 3; k++) { float v = c[k] <= 0.0031308f ? c[k] * 12.92f : 1.055f * powf(c[k], 1.0f / 2.4f) - 0.055f; rgb[k] = (unsigned char)std::clamp((int)lroundf(v * 255.0f), 0, 255); }
}

// ---------------------------------------------------------------- por jugador (piel_jugadores.ini)
struct SkinProf { std::string name; int tono = 0, matiz = 0, juego = 0; };   // juego: color de piel del juego (1..6, 0 = no se sabe)
static std::map<uint32_t, SkinProf> s_skin; static std::mutex s_skinMx;
static bool s_skinLoaded = false, s_skinDirty = false; static double s_skinDirtyT = 0;
static std::wstring SkinPath() { return g_cfg.baseDir + L"piel_jugadores.ini"; }
static std::string Trim2(const std::string& x) { size_t a = x.find_first_not_of(" \t\r\n"), b = x.find_last_not_of(" \t\r\n"); return a == std::string::npos ? std::string() : x.substr(a, b - a + 1); }
static void LoadSkin() {
    s_skin.clear(); s_skinLoaded = true;
    std::ifstream f(SkinPath(), std::ios::binary); if (!f) return;
    std::string l; SkinProf* cur = nullptr;
    while (std::getline(f, l)) {
        l = Trim2(l); if (l.empty() || l[0] == ';') continue;
        if (l[0] == '[') { char* e = nullptr; unsigned long v = strtoul(l.c_str() + 1, &e, 10); cur = (v && e && *e == ']') ? &s_skin[(uint32_t)v] : nullptr; continue; }
        if (!cur) continue;
        size_t eq = l.find('='); if (eq == std::string::npos) continue;
        std::string k = Trim2(l.substr(0, eq)), v = Trim2(l.substr(eq + 1));
        if (k == "nombre") cur->name = v;
        else if (k == "tono") cur->tono = std::clamp(atoi(v.c_str()), 0, kSkinTones);
        else if (k == "matiz") cur->matiz = std::clamp(atoi(v.c_str()), -10, 10);
        else if (k == "juego") cur->juego = std::clamp(atoi(v.c_str()), 0, 6);
    }
    Log("piel: %d jugadores con tono propio en piel_jugadores.ini", (int)s_skin.size());
}
static void SaveSkin() {
    std::ofstream f(SkinPath(), std::ios::binary | std::ios::trunc); if (!f) { Log("piel: no se pudo escribir piel_jugadores.ini"); return; }
    f << "; PES21 FX - tono de piel por jugador (lo escribe el menu Fisico del juego, pagina Piel).\r\n"
         "; [id] = id del jugador; tono = 1..30 (0 = el color del juego; los 6 colores del juego son 3, 8, 13, 18, 23 y 28),\r\n"
         "; matiz = -10 (oliva) .. +10 (rosado); juego = color de piel del juego del jugador (1..6, lo anota el menu Fisico).\r\n";
    for (auto& kv : s_skin) {
        if (kv.second.tono <= 0 && kv.second.matiz == 0) continue;
        f << "\r\n[" << kv.first << "]\r\n";
        if (!kv.second.name.empty()) f << "nombre = " << kv.second.name << "\r\n";
        f << "tono = " << kv.second.tono << "\r\nmatiz = " << kv.second.matiz << "\r\n";
        if (kv.second.juego > 0) f << "juego = " << kv.second.juego << "\r\n";
    }
}
bool SkinToneGet(uint32_t id, int* tono, int* matiz) {
    std::lock_guard<std::mutex> lk(s_skinMx);
    if (!s_skinLoaded) LoadSkin();
    auto it = id ? s_skin.find(id) : s_skin.end();
    *tono = it != s_skin.end() ? it->second.tono : 0; *matiz = it != s_skin.end() ? it->second.matiz : 0;
    return it != s_skin.end() && (*tono > 0 || *matiz != 0);
}
int SkinToneGame(uint32_t id) {
    std::lock_guard<std::mutex> lk(s_skinMx);
    if (!s_skinLoaded) LoadSkin();
    auto it = id ? s_skin.find(id) : s_skin.end();
    return it != s_skin.end() ? it->second.juego : 0;
}
static void SkinToneSetGame(uint32_t id, int gi) {
    if (!id || gi < 1 || gi > 6) return;
    std::lock_guard<std::mutex> lk(s_skinMx);
    if (!s_skinLoaded) LoadSkin();
    auto it = s_skin.find(id);
    if (it == s_skin.end() || it->second.juego == gi) return;
    it->second.juego = gi; s_skinDirty = true; s_skinDirtyT = Now();
}
void SkinToneSet(uint32_t id, const char* name, int tono, int matiz) {
    if (!id) return;
    std::lock_guard<std::mutex> lk(s_skinMx);
    if (!s_skinLoaded) LoadSkin();
    SkinProf& p = s_skin[id];
    if (name && *name) p.name = name;
    p.tono = std::clamp(tono, 0, kSkinTones); p.matiz = std::clamp(matiz, -10, 10);
    s_skinDirty = true; s_skinDirtyT = Now();
}
// por cuadro: guardado diferido y la tabla de jugadores con tono propio y los tapones (194..254; ver pes_skintone.hlsli)
static bool NodePos(void* node, float out[3]) {
    if (!node) return false;
    __try { memcpy(out, (const char*)node + 0x70, 12); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    for (int i = 0; i < 3; i++) if (!(out[i] == out[i]) || fabsf(out[i]) > 2000.0f) return false;
    return true;
}
static double s_tabLog = 0;
// (24/10 c) la tabla lleva primero a los jugadores con tono propio y despues, como "tapones" (tono del juego), a los demas
// personajes que estan cerca de alguno de ellos: si no, en un corner o una marca personal el VS del rival encontraria como
// mas cercana la entrada del jugador con tono propio y se pintaria con su tono
static const int kTabMax = 30;   // filas 195..254
void SkinTonePresent() {
    { std::lock_guard<std::mutex> lk(s_skinMx); if (s_skinDirty && Now() - s_skinDirtyT > 0.6) { s_skinDirty = false; SaveSkin(); } }
    float* T = g_cfg.live[185]; T[0] = T[1] = T[2] = 1.0f; T[3] = 0.0f;   // (la vista previa global de antes ya no se usa)
    for (int r = 194; r < 195 + 2 * kTabMax; r++) for (int c = 0; c < 4; c++) g_cfg.live[r][c] = 0.0f;
    if (g_cfg.live[184][0] < 0.5f) return;
    static CharInfo ci[kCharSlots]; static bool alive[kCharSlots];
    CharSnapshot(ci, alive);
    uint32_t eid = 0; const bool fis = FisicoEditing(&eid) && MusclePhysiqueActive();
    const int fgi = fis ? FisicoSkinIndex() : 0;
    const float k = g_cfg.live[184][1] > 0.0f ? g_cfg.live[184][1] : 1.0f;
    const float maxD = 1.4f;
    struct Ent { float pos[3], P[3]; bool own; float d2; uint32_t id; int gi, tono, matiz; };
    static Ent e[kCharSlots]; int ne = 0, nOwn = 0;
    for (int sl = 0; sl < kCharSlots; sl++) {
        if (!alive[sl] || !ci[sl].node) continue;
        Ent& x = e[ne];
        if (!NodePos(ci[sl].node, x.pos)) continue;
        x.P[0] = 1.0f; x.P[1] = 1.0f; x.P[2] = 0.0f; x.own = false; x.d2 = 1e30f; x.id = ci[sl].known ? ci[sl].id : 0; x.gi = x.tono = x.matiz = 0;
        if (x.id) {
            if (fis && x.id == eid && fgi >= 1) SkinToneSetGame(x.id, fgi);   // recordar el color del juego de este jugador
            if (SkinToneGet(x.id, &x.tono, &x.matiz)) {
                x.gi = (fis && x.id == eid && fgi >= 1) ? fgi : SkinToneGame(x.id);
                x.own = SkinToneParams(x.gi, x.tono, x.matiz, k, x.P);
            }
        }
        if (x.own) nOwn++;
        ne++;
    }
    int n = 0, nBlk = 0; char line[1024]; int ln = 0; line[0] = 0;
    auto put = [&](const Ent& x) {
        float* A = g_cfg.live[195 + 2 * n]; float* B = g_cfg.live[196 + 2 * n];
        A[0] = x.pos[0]; A[1] = x.pos[1]; A[2] = x.pos[2]; A[3] = 0.0f;
        B[0] = x.P[0]; B[1] = x.P[1]; B[2] = x.P[2]; B[3] = x.own ? 1.0f : 0.0f;
        n++;
    };
    if (nOwn > 0) {
        for (int i = 0; i < ne && n < kTabMax; i++) if (e[i].own) {
            put(e[i]);
            if (ln < 900) ln += snprintf(line + ln, sizeof line - ln, " | %u (color %d, tono %d, matiz %d) en (%.1f, %.1f, %.1f) kL %.2f m %.2f %.2f", e[i].id, e[i].gi, e[i].tono, e[i].matiz, e[i].pos[0], e[i].pos[1], e[i].pos[2], e[i].P[0], e[i].P[1], e[i].P[2]);
        }
        // tapones: los demas, por cercania al jugador con tono propio mas proximo (solo los que pueden confundirse)
        const float lim = (2.0f * maxD) * (2.0f * maxD);
        for (int i = 0; i < ne; i++) {
            if (e[i].own) continue;
            for (int j = 0; j < ne; j++) if (e[j].own) {
                const float dx = e[i].pos[0] - e[j].pos[0], dz = e[i].pos[2] - e[j].pos[2];
                e[i].d2 = (std::min)(e[i].d2, dx * dx + dz * dz);
            }
        }
        while (n < kTabMax) {
            int b = -1;
            for (int i = 0; i < ne; i++) if (!e[i].own && e[i].d2 < lim && (b < 0 || e[i].d2 < e[b].d2)) b = i;
            if (b < 0) break;
            put(e[b]); e[b].d2 = 1e30f; nBlk++;
        }
    }
    g_cfg.live[194][0] = (float)n; g_cfg.live[194][1] = n > 0 ? 1.0f : 0.0f; g_cfg.live[194][2] = maxD;
    const double t = Now();
    if (n > 0 && t > s_tabLog) { s_tabLog = t + 30.0; Log("piel: tabla de tonos, %d con tono propio y %d cerca sin tono (de %d personajes)%s", n - nBlk, nBlk, ne, line); }
}

std::atomic<bool> g_skinDiagArmed{false};
static std::atomic<int> s_armFrame{-1};
static int s_caps = 0; static double s_next = 0;
static std::mutex s_mx; static std::vector<std::string> s_lines; static int s_order = 0, s_total = 0;
// contenido de los bloques (para ver si hay algo propio de cada jugador): por dibujo, b5 del VS (m_viewWorld, m_world,
// m_useWeightCount, m_localParam[9] = 288 bytes) en + 0 y b4 del PS (m_materials[8] = 128 bytes) en + 384
static const int kSlots = 24, kSlotBytes = 512;
static ID3D11Buffer* s_stage = nullptr;
struct SlotInfo { int order; uint64_t ps; bool b5, b4; };
static SlotInfo s_slot[kSlots]; static int s_nSlots = 0, s_readFrame = -1, s_readTries = 0;

static bool Interesting(uint64_t ps, uint64_t vs) {
    switch (ps) {
    case 0xaa7d1833f6a11481ULL:   // piel del cuerpo
    case 0x5ce3ed8702271bc1ULL:   // cara
    case 0xce188ae110354aeaULL:   // cara (otra variante)
    case 0x63fab7735eee149cULL:   // boca
    case 0x55297925e0c66085ULL:   // short / medias
    case 0xfccd955fa6f3e8ebULL:   // otra malla del cuerpo
        return true;
    }
    return vs == 0x1c02a92ab4ae3362ULL || vs == 0x1c9bde0e3060b6e0ULL;
}
static ID3D11Buffer* CbOf(ID3D11DeviceContext1* c1, bool ps, UINT slot, UINT* first, UINT* num = nullptr) {   // con AddRef
    ID3D11Buffer* b = nullptr; UINT f = 0, n = 0;
    if (ps) c1->PSGetConstantBuffers1(slot, 1, &b, &f, &n); else c1->VSGetConstantBuffers1(slot, 1, &b, &f, &n);
    *first = f; if (num) *num = n;
    return b;
}
static bool CopyBlock(ID3D11DeviceContext* c, ID3D11Buffer* src, UINT first, UINT bytes, UINT dstOff) {
    if (!src || !s_stage) return false;
    D3D11_BUFFER_DESC d; src->GetDesc(&d);
    const UINT off = first * 16;
    if (d.ByteWidth < off + bytes || (d.Usage == D3D11_USAGE_STAGING)) return false;
    D3D11_BOX bx = { off, 0, 0, off + bytes, 1, 1 };
    c->CopySubresourceRegion(s_stage, 0, dstOff, 0, 0, src, 0, &bx);
    return true;
}
static void* ResOf(ID3D11DeviceContext* c, UINT slot) {
    ID3D11ShaderResourceView* v = nullptr; c->PSGetShaderResources(slot, 1, &v);
    if (!v) return nullptr;
    ID3D11Resource* r = nullptr; v->GetResource(&r); v->Release();
    if (r) r->Release();
    return r;
}

void SkinDiagDraw(ID3D11DeviceContext* c, uint64_t ps, uint64_t vs, UINT count) {
    if (PresentNo() != s_armFrame.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(s_mx);
    const int order = s_order++;
    ID3D11DeviceContext1* c1 = nullptr;
    ID3D11Buffer *b4 = nullptr, *b5 = nullptr, *b6 = nullptr; UINT f4 = 0, f5 = 0, f6 = 0, n6 = 0;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) {
        b4 = CbOf(c1, true, 4, &f4); b5 = CbOf(c1, false, 5, &f5); b6 = CbOf(c1, false, 6, &f6, &n6); c1->Release();
    }
    // de interes: los shaders de piel / cara / kit conocidos, o cualquier dibujo con paleta de huesos de personaje en b6
    // (cbuffer de 3072 bytes: 32 + 32 matrices 4x3, la de este cuadro y la del anterior)
    bool bones = false;
    if (b6) { D3D11_BUFFER_DESC d; b6->GetDesc(&d); bones = d.ByteWidth == 3072 || n6 * 16 == 3072; }
    if (!Interesting(ps, vs) && !bones) { if (b4) b4->Release(); if (b5) b5->Release(); if (b6) b6->Release(); return; }
    s_total++;
    if (s_lines.size() >= 60) { if (b4) b4->Release(); if (b5) b5->Release(); if (b6) b6->Release(); return; }
    char b[320];
    snprintf(b, sizeof b, "  #%d %s PS %016llx VS %016llx %u idx | b4 %p+%u b5 %p+%u b6 %p+%u | t0 %p t1 %p", order,
             c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED ? "dif" : "inm", (unsigned long long)ps, (unsigned long long)vs, count,
             (void*)b4, f4, (void*)b5, f5, (void*)b6, f6, ResOf(c, 0), ResOf(c, 1));
    s_lines.push_back(b);
    if (s_nSlots < kSlots && s_stage) {   // copia del contenido para leerlo dos cuadros despues
        SlotInfo& si = s_slot[s_nSlots];
        si.order = order; si.ps = ps;
        si.b5 = CopyBlock(c, b5, f5, 288, (UINT)s_nSlots * kSlotBytes);
        si.b4 = CopyBlock(c, b4, f4, 128, (UINT)s_nSlots * kSlotBytes + 384);
        s_nSlots++;
    }
    if (b4) b4->Release(); if (b5) b5->Release(); if (b6) b6->Release();
}

void SkinDiagPresent() {
    if (g_cfg.live[184][3] < 0.5f) { g_skinDiagArmed = false; return; }   // w184: diagnostico de dibujos de personajes
    const int f = PresentNo();
    const int armed = s_armFrame.load();
    if (armed >= 0 && f > armed) {   // el cuadro capturado ya paso: al log
        std::vector<std::string> l; int total;
        { std::lock_guard<std::mutex> lk(s_mx); l.swap(s_lines); total = s_total; s_total = 0; }
        if (total > 0) {   // cuadros sin personajes no cuentan
            s_caps++;
            Log("piel: dibujos de personajes del cuadro %d (%d de interes; # = orden entre todos los dibujos del cuadro):", armed, total);
            for (auto& s : l) Log("%s", s.c_str());
        }
        s_armFrame = -1; g_skinDiagArmed = false;
        if (total > 0 && s_nSlots > 0) { s_readFrame = f + 2; s_readTries = 0; } else s_nSlots = 0;
    }
    if (s_readFrame >= 0 && f >= s_readFrame && g_dev.imm && s_stage) {   // contenido de los bloques
        D3D11_MAPPED_SUBRESOURCE m;
        HRESULT hr = g_dev.imm->Map(s_stage, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
        if (SUCCEEDED(hr)) {
            for (int i = 0; i < s_nSlots; i++) {
                const float* q = (const float*)((const char*)m.pData + (size_t)i * kSlotBytes);
                char l[900]; int n = snprintf(l, sizeof l, "  #%d PS %08llx", s_slot[i].order, (unsigned long long)(s_slot[i].ps >> 32));
                if (s_slot[i].b5) {
                    n += snprintf(l + n, sizeof l - n, " | mundo t(%.2f %.2f %.2f) peso %.0f/%.0f | local", q[28], q[29], q[30], q[32], q[35]);
                    for (int k = 36; k < 72 && n < 700; k++) n += snprintf(l + n, sizeof l - n, (k % 4 == 0) ? " [%.3g" : (k % 4 == 3 ? " %.3g]" : " %.3g"), q[k]);
                }
                if (s_slot[i].b4) {
                    const float* mq = q + 96; n += snprintf(l + n, sizeof l - n, " | material");
                    for (int k = 0; k < 32 && n < 880; k++) n += snprintf(l + n, sizeof l - n, (k % 4 == 0) ? " [%.3g" : (k % 4 == 3 ? " %.3g]" : " %.3g"), mq[k]);
                }
                Log("%s", l);
            }
            g_dev.imm->Unmap(s_stage, 0);
            s_readFrame = -1; s_nSlots = 0;
        } else if (++s_readTries > 20) { s_readFrame = -1; s_nSlots = 0; }
    }
    const double t = Now();
    if (s_armFrame.load() < 0 && s_readFrame < 0 && s_caps < 10 && t > s_next) {
        s_next = t + 20.0;
        if (!s_stage && g_dev.dev) {
            D3D11_BUFFER_DESC d = {}; d.ByteWidth = kSlots * kSlotBytes; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            g_dev.dev->CreateBuffer(&d, nullptr, &s_stage);
        }
        { std::lock_guard<std::mutex> lk(s_mx); s_order = 0; s_total = 0; s_lines.clear(); s_nSlots = 0; }
        s_armFrame = f + 1; g_skinDiagArmed = true;
    }
}

} // namespace fx
