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
#include "core.h"
#include <d3d11_1.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
void SkinToneSwatch(int tono, unsigned char rgb[3]) {   // para rotulos: el tono en sRGB
    float c[3]; ToneLin(tono, c);
    for (int k = 0; k < 3; k++) { float v = c[k] <= 0.0031308f ? c[k] * 12.92f : 1.055f * powf(c[k], 1.0f / 2.4f) - 0.055f; rgb[k] = (unsigned char)std::clamp((int)lroundf(v * 255.0f), 0, 255); }
}

// ---------------------------------------------------------------- por jugador (piel_jugadores.ini)
struct SkinProf { std::string name; int tono = 0, matiz = 0; };
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
    }
    Log("piel: %d jugadores con tono propio en piel_jugadores.ini", (int)s_skin.size());
}
static void SaveSkin() {
    std::ofstream f(SkinPath(), std::ios::binary | std::ios::trunc); if (!f) { Log("piel: no se pudo escribir piel_jugadores.ini"); return; }
    f << "; PES21 FX - tono de piel por jugador (lo escribe el menu Fisico del juego, pagina Piel).\r\n"
         "; [id] = id del jugador; tono = 1..30 (0 = el color del juego; los 6 colores del juego son 3, 8, 13, 18, 23 y 28),\r\n"
         "; matiz = -10 (oliva) .. +10 (rosado).\r\n";
    for (auto& kv : s_skin) {
        if (kv.second.tono <= 0 && kv.second.matiz == 0) continue;
        f << "\r\n[" << kv.first << "]\r\n";
        if (!kv.second.name.empty()) f << "nombre = " << kv.second.name << "\r\n";
        f << "tono = " << kv.second.tono << "\r\nmatiz = " << kv.second.matiz << "\r\n";
    }
}
bool SkinToneGet(uint32_t id, int* tono, int* matiz) {
    std::lock_guard<std::mutex> lk(s_skinMx);
    if (!s_skinLoaded) LoadSkin();
    auto it = id ? s_skin.find(id) : s_skin.end();
    *tono = it != s_skin.end() ? it->second.tono : 0; *matiz = it != s_skin.end() ? it->second.matiz : 0;
    return it != s_skin.end() && (*tono > 0 || *matiz != 0);
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
// por cuadro: guardado diferido y la vista previa del jugador editado (x185..w185)
void SkinTonePresent() {
    { std::lock_guard<std::mutex> lk(s_skinMx); if (s_skinDirty && Now() - s_skinDirtyT > 0.6) { s_skinDirty = false; SaveSkin(); } }
    float* T = g_cfg.live[185];
    uint32_t id = 0; int gi = 0, tono = 0, matiz = 0; float g[3];
    if (g_cfg.live[184][0] > 0.5f && FisicoEditing(&id) && MusclePhysiqueActive() && (gi = FisicoSkinIndex()) >= 1 &&
        SkinToneGet(id, &tono, &matiz) && SkinToneGain(gi, tono, matiz, g)) {
        const float k = std::clamp(g_cfg.live[184][1] > 0.0f ? g_cfg.live[184][1] : 1.0f, 0.0f, 1.5f);
        for (int c = 0; c < 3; c++) T[c] = 1.0f + (g[c] - 1.0f) * k;
        T[3] = 1.0f;
    } else { T[0] = T[1] = T[2] = 1.0f; T[3] = 0.0f; }
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
