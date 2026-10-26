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
static void LinToLCh(const float c[3], float* L, float* C, float* hDeg) {
    float l = 0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2];
    float m = 0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2];
    float s = 0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2];
    l = cbrtf(l); m = cbrtf(m); s = cbrtf(s);
    *L = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    const float a = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    const float b = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
    if (C) *C = sqrtf(a * a + b * b);
    if (hDeg) *hDeg = atan2f(b, a) * 180.0f / 3.14159265f;
}
static void GameOklabL(int idx, float* L) { float c[3]; GameLin(idx, c); LinToLCh(c, L, nullptr, nullptr); }
static void ToneOklab(int t, float* L, float* C, float* hDeg) {
    t = std::clamp(t, 1, kSkinTones);
    const float p = (float)(t - 3) / 5.0f;   // 0 = color 1 del juego, 5 = color 6
    int a = std::clamp((int)floorf(p), 0, 4); const float f = p - (float)a;
    float LA, LB; GameOklabL(a + 1, &LA); GameOklabL(a + 2, &LB);
    float Lv = LA + (LB - LA) * f;
    if (p > 5.0f) Lv = LB + (LB - LA) * (p - 5.0f) * 2.2f;   // (24/10 e) mas alla del color 6 baja mas rapido (tonos 29 y 30 bien oscuros)
    *L = std::clamp(Lv, 0.15f, 0.99f);
    // croma y matiz (grados) por posicion: muy claro poco saturado, medio oliva / canela, oscuro marron rojizo
    static const float kT[8] = { 1, 3, 8, 13, 18, 23, 28, 30 };
    // (24/10 c) medios un poco mas saturados (canela / caramelo en vez de caqui); la paleta del juego tiene croma ~0.07
    static const float kC[8] = { 0.036f, 0.048f, 0.064f, 0.075f, 0.074f, 0.066f, 0.054f, 0.046f };
    static const float kH[8] = { 62.0f, 63.0f, 68.0f, 69.0f, 64.0f, 56.0f, 48.0f, 46.0f };
    int j = 0; while (j < 6 && (float)t > kT[j + 1]) j++;
    const float u = std::clamp(((float)t - kT[j]) / (kT[j + 1] - kT[j]), 0.0f, 1.0f);
    *C = kC[j] + (kC[j + 1] - kC[j]) * u; *hDeg = kH[j] + (kH[j + 1] - kH[j]) * u;
}
// del color del juego del jugador (su textura esta hecha con ese color: L, croma y matiz medidos de la paleta) al tono
// elegido + matiz: (kL, m0, m1). El tono es absoluto: el mismo numero se ve igual en todos los jugadores, sea cual sea su
// color del juego (antes se movia desde la posicion del color del juego en la escala y dos jugadores con el mismo tono
// quedaban distintos); tono 0 = el color del juego (solo el matiz)
// (24/10 d) rgb != 0: color propio (0xRRGGBB, de la paleta del juego) en vez del tono de la escala. (24/10 e) luz / sat:
// ajuste fino (-25..25); matiz ahora -25..25 (0.6 grados por paso)
static bool ProfOwn(const SkinToneProf& q);   // abajo
static void SrgbLCh(uint32_t rgb, float* L, float* C, float* h) {
    float c[3] = { Lin((float)((rgb >> 16) & 255)), Lin((float)((rgb >> 8) & 255)), Lin((float)(rgb & 255)) }; LinToLCh(c, L, C, h);
}
static void Params(float kL, float kC, float dh, float k, float out[3]) {
    k = std::clamp(k, 0.0f, 1.5f);
    kL = 1.0f + (kL - 1.0f) * k; kC = std::max(0.0f, 1.0f + (kC - 1.0f) * k); dh *= k;
    const float r = dh * 3.14159265f / 180.0f;
    out[0] = kL; out[1] = kC * cosf(r); out[2] = kC * sinf(r);
}
bool SkinToneParams(int gameIdx, const SkinToneProf& q, float k, float out[3], float outBody[3]) {
    out[0] = 1.0f; out[1] = 1.0f; out[2] = 0.0f; outBody[0] = 1.0f; outBody[1] = 1.0f; outBody[2] = 0.0f;
    if (!ProfOwn(q) || gameIdx < 1) return false;
    float LB, CB, HB, LT, CT, HT;
    { float c[3]; GameLin(std::clamp(gameIdx, 1, 6), c); LinToLCh(c, &LB, &CB, &HB); }
    if (q.rgb) { SrgbLCh(q.rgb, &LT, &CT, &HT); LT = std::clamp(LT, 0.15f, 0.99f); }
    else if (q.tono > 0) ToneOklab(q.tono, &LT, &CT, &HT); else { LT = LB; CT = CB; HT = HB; }
    LT *= 1.0f + 0.012f * (float)std::clamp(q.luz, -25, 25);
    CT *= 1.0f + 0.02f * (float)std::clamp(q.sat, -25, 25);
    float kL = LT / std::max(LB, 0.05f), kC = CT / std::max(CB, 0.005f), dh = HT - HB;
    const int mz = std::clamp(q.matiz, -25, 25);
    dh -= 0.6f * (float)mz;                                   // + rosado (mas rojo), - oliva (mas amarillo-verde)
    kC *= mz >= 0 ? 1.0f + 0.005f * (float)mz : 1.0f + 0.006f * (float)mz;
    Params(kL, kC, dh, k, out);
    // cuerpo como la cara: lo que hay que hacerle al color medido del cuerpo para llegar al medido de la cara, compuesto
    // con lo de arriba (la cara recibe solo lo de arriba)
    if (q.igualar && q.cuerpo && q.cara) {
        float Lb, Cb, Hb, Lf, Cf, Hf; SrgbLCh(q.cuerpo, &Lb, &Cb, &Hb); SrgbLCh(q.cara, &Lf, &Cf, &Hf);
        float dhf = Hf - Hb; while (dhf > 180.0f) dhf -= 360.0f; while (dhf < -180.0f) dhf += 360.0f;
        const float kLf = std::clamp(Lf / std::max(Lb, 0.05f), 0.5f, 2.0f), kCf = std::clamp(Cf / std::max(Cb, 0.005f), 0.3f, 3.0f);
        float corr[3]; Params(kLf, kCf, std::clamp(dhf, -40.0f, 40.0f), 1.0f, corr);
        outBody[0] = out[0] * corr[0];
        outBody[1] = out[1] * corr[1] - out[2] * corr[2];   // (m0 + i m1) * (c0 + i c1)
        outBody[2] = out[1] * corr[2] + out[2] * corr[1];
    } else { outBody[0] = out[0]; outBody[1] = out[1]; outBody[2] = out[2]; }
    return true;
}
void SkinToneSwatch(int tono, unsigned char rgb[3]) {   // para rotulos: el tono en sRGB (el de la escala en Oklab)
    float c[3];
    {
        float L, C, h; ToneOklab(tono, &L, &C, &h);
        const float r = h * 3.14159265f / 180.0f, a = C * cosf(r), b = C * sinf(r);
        const float l_ = L + 0.3963377774f * a + 0.2158037573f * b, m_ = L - 0.1055613458f * a - 0.0638541728f * b, s_ = L - 0.0894841775f * a - 1.2914855480f * b;
        const float l3 = l_ * l_ * l_, m3 = m_ * m_ * m_, s3 = s_ * s_ * s_;
        c[0] = std::clamp(4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3, 0.0f, 1.0f);
        c[1] = std::clamp(-1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3, 0.0f, 1.0f);
        c[2] = std::clamp(-0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3, 0.0f, 1.0f);
    }
    for (int k = 0; k < 3; k++) { float v = c[k] <= 0.0031308f ? c[k] * 12.92f : 1.055f * powf(c[k], 1.0f / 2.4f) - 0.055f; rgb[k] = (unsigned char)std::clamp((int)lroundf(v * 255.0f), 0, 255); }
}

// ---------------------------------------------------------------- por jugador (piel_jugadores.ini)
// juego: color de piel del juego (1..6, 0 = no se sabe); rgb: color propio elegido en la paleta del juego (0xRRGGBB, 0 = ninguno;
// manda sobre el tono); luz / sat: ajuste fino -25..25; igualar: el cuerpo toma el color de la cara (cuerpo / cara = colores
// medidos en el menu Fisico, 0xRRGGBB en sRGB)
struct SkinProf : SkinToneProf { std::string name; };
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
        else if (k == "matiz") cur->matiz = std::clamp(atoi(v.c_str()), -25, 25);
        else if (k == "juego") cur->juego = std::clamp(atoi(v.c_str()), 0, 6);
        else if (k == "rgb") cur->rgb = (uint32_t)strtoul(v.c_str(), nullptr, 16) & 0xffffffu;
        else if (k == "luz") cur->luz = std::clamp(atoi(v.c_str()), -25, 25);
        else if (k == "sat") cur->sat = std::clamp(atoi(v.c_str()), -25, 25);
        else if (k == "igualar") cur->igualar = atoi(v.c_str()) != 0 ? 1 : 0;
        else if (k == "zona") cur->zona = std::clamp(atoi(v.c_str()), 0, 2);
        else if (k == "cuerpo") cur->cuerpo = (uint32_t)strtoul(v.c_str(), nullptr, 16) & 0xffffffu;
        else if (k == "cara") cur->cara = (uint32_t)strtoul(v.c_str(), nullptr, 16) & 0xffffffu;
    }
    Log("piel: %d jugadores con tono propio en piel_jugadores.ini", (int)s_skin.size());
}
static void SaveSkin() {
    std::ofstream f(SkinPath(), std::ios::binary | std::ios::trunc); if (!f) { Log("piel: no se pudo escribir piel_jugadores.ini"); return; }
    f << "; PES21 FX - tono de piel por jugador (lo escribe el menu Fisico del juego, pagina Piel).\r\n"
         "; [id] = id del jugador; tono = 1..30 (0 = el color del juego; los 6 colores del juego son 3, 8, 13, 18, 23 y 28),\r\n"
         "; matiz = -25 (oliva) .. +25 (rosado), luz y sat = -25..+25 (ajuste fino); juego = color de piel del juego del jugador\r\n"
         "; (1..6, lo anota el menu Fisico); rgb = RRGGBB (hex): color propio elegido en la paleta del juego (manda sobre el tono);\r\n"
         "; igualar = 1: el cuerpo toma el color de la cara (cuerpo y cara = colores medidos en la base del cuello, hex sRGB);\r\n"
         "; zona = a que se aplica el tono: 0 cara y cuerpo, 1 solo el cuerpo, 2 solo el torso (brazos, cuello y piernas como el juego).\r\n";
    for (auto& kv : s_skin) {
        const SkinProf& q = kv.second;
        if (q.tono <= 0 && q.matiz == 0 && q.luz == 0 && q.sat == 0 && !q.rgb && !q.igualar && !q.zona) continue;
        f << "\r\n[" << kv.first << "]\r\n";
        if (!q.name.empty()) f << "nombre = " << q.name << "\r\n";
        f << "tono = " << q.tono << "\r\nmatiz = " << q.matiz << "\r\n";
        if (q.luz) f << "luz = " << q.luz << "\r\n";
        if (q.sat) f << "sat = " << q.sat << "\r\n";
        char h[16];
        if (q.rgb) { snprintf(h, sizeof h, "%06X", q.rgb & 0xffffffu); f << "rgb = " << h << "\r\n"; }
        if (q.igualar) f << "igualar = 1\r\n";
        if (q.zona) f << "zona = " << q.zona << "\r\n";
        if (q.cuerpo) { snprintf(h, sizeof h, "%06X", q.cuerpo & 0xffffffu); f << "cuerpo = " << h << "\r\n"; }
        if (q.cara) { snprintf(h, sizeof h, "%06X", q.cara & 0xffffffu); f << "cara = " << h << "\r\n"; }
        if (q.juego > 0) f << "juego = " << q.juego << "\r\n";
    }
}
static bool ProfOwn(const SkinToneProf& q) { return q.tono > 0 || q.matiz != 0 || q.luz != 0 || q.sat != 0 || q.rgb != 0 || (q.igualar && q.cuerpo && q.cara); }
bool SkinToneGetProf(uint32_t id, SkinToneProf& out) {
    std::lock_guard<std::mutex> lk(s_skinMx);
    if (!s_skinLoaded) LoadSkin();
    auto it = id ? s_skin.find(id) : s_skin.end();
    if (it == s_skin.end()) { out = SkinToneProf(); return false; }
    out = it->second;
    return ProfOwn(out);
}
void SkinToneSetProf(uint32_t id, const char* name, const SkinToneProf& q) {   // los campos que elige el usuario (no juego / cuerpo / cara)
    if (!id) return;
    std::lock_guard<std::mutex> lk(s_skinMx);
    if (!s_skinLoaded) LoadSkin();
    SkinProf& p = s_skin[id];
    if (name && *name) p.name = name;
    p.tono = std::clamp(q.tono, 0, kSkinTones); p.matiz = std::clamp(q.matiz, -25, 25); p.luz = std::clamp(q.luz, -25, 25); p.sat = std::clamp(q.sat, -25, 25);
    p.rgb = q.rgb & 0xffffffu; p.igualar = q.igualar ? 1 : 0; p.zona = std::clamp(q.zona, 0, 2);
    s_skinDirty = true; s_skinDirtyT = Now();
}
bool SkinToneGet(uint32_t id, int* tono, int* matiz, uint32_t* rgb) {
    SkinToneProf q; const bool own = SkinToneGetProf(id, q);
    *tono = q.tono; *matiz = q.matiz; if (rgb) *rgb = q.rgb;
    return own;
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
void SkinToneSet(uint32_t id, const char* name, int tono, int matiz, uint32_t rgb) {
    SkinToneProf q; SkinToneGetProf(id, q);
    q.tono = tono; q.matiz = matiz; q.rgb = rgb;
    SkinToneSetProf(id, name, q);
}
// por cuadro: guardado diferido y la tabla de jugadores con tono propio y los tapones (194..254; ver pes_skintone.hlsli)
// posicion del personaje: matriz de mundo del nodo en + 0x40; la traslacion en + 0x70 (fila 3) o en + 0x4c / 0x5c / 0x6c
// (columna 3) segun como la guarde el motor: lo decide la comprobacion de abajo (s_nodeLayout)
static std::atomic<int> s_nodeLayout{0}, s_posMode{0};
static bool NodeMat(void* node, float m[16]) {
    if (!node) return false;
    __try { memcpy(m, (const char*)node + 0x40, 64); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
static bool NodePosL(const float m[16], int layout, float out[3]) {
    if (layout == 0) { out[0] = m[12]; out[1] = m[13]; out[2] = m[14]; } else { out[0] = m[3]; out[1] = m[7]; out[2] = m[11]; }
    for (int i = 0; i < 3; i++) if (!(out[i] == out[i]) || fabsf(out[i]) > 2000.0f) return false;
    return true;
}
static bool NodePos(void* node, float out[3]) {
    float m[16]; return NodeMat(node, m) && NodePosL(m, s_nodeLayout.load(), out);
}
bool CharNodeWorldPos(void* node, float out[3]) { return NodePos(node, out); }   // (26/10) la usa musculo.cpp (ropa por jugador)
static void ToneCheckPresent(const CharInfo* ci, const bool* alive);   // abajo
static double s_tabLog = 0;
// (24/10 e) medicion "cuerpo como la cara": ring.cpp acumula en el menu Fisico el color de la base del cuello del cuerpo
// (lugar 4) y de la franja de abajo de la cara (lugar 5) y el alto de la cara (lugar 6); aca se promedia y se guarda en el
// perfil del jugador editado (cada 0.5 s si cambio)
std::atomic<bool> g_skinMeasure{false};
static bool s_measureWanted = false; static float s_faceY[2] = { 0, 0 }; static double s_measT = 0;
static uint32_t MeanSrgb(const uint32_t* q) {   // (sum r, sum g, sum b, n) en punto fijo x 4095, lineal -> 0xRRGGBB sRGB
    const float n = (float)q[3]; uint32_t out = 0;
    for (int k = 0; k < 3; k++) {
        float v = std::clamp((float)q[k] / (4095.0f * n), 0.0f, 1.0f);
        v = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
        out = (out << 8) | (uint32_t)std::clamp((int)lroundf(v * 255.0f), 0, 255);
    }
    return out;
}
void SkinMeasureReport(const uint32_t* body, const uint32_t* face, uint32_t yMinInv, uint32_t yMax, uint32_t faceN) {
    if (faceN > 0 && yMinInv > 0) { s_faceY[0] = (float)(8192u - yMinInv); s_faceY[1] = (float)yMax; }
    const double t = Now();
    if (!g_skinMeasure || body[3] < 150 || face[3] < 150 || t < s_measT) return;
    s_measT = t + 0.5;
    const uint32_t cb = MeanSrgb(body), cf = MeanSrgb(face);
    uint32_t eid = 0; if (!FisicoEditing(&eid) || !eid) return;
    std::lock_guard<std::mutex> lk(s_skinMx);
    if (!s_skinLoaded) LoadSkin();
    auto it = s_skin.find(eid); if (it == s_skin.end() || !it->second.igualar) return;
    auto diff = [](uint32_t a, uint32_t b) { int d = 0; for (int k = 0; k < 3; k++) d = (std::max)(d, abs((int)((a >> (8 * k)) & 255) - (int)((b >> (8 * k)) & 255))); return d; };
    if (diff(cb, it->second.cuerpo) <= 1 && diff(cf, it->second.cara) <= 1) return;
    it->second.cuerpo = cb; it->second.cara = cf; s_skinDirty = true; s_skinDirtyT = t;
    static int n = 0; if (n++ < 30) Log("piel: medido cuerpo %06X (%u px) y cara %06X (%u px, alto %.0f..%.0f) del jugador %u", cb, body[3], cf, face[3], s_faceY[0], s_faceY[1], eid);
}
// (24/10 c) la tabla lleva primero a los jugadores con tono propio y despues, como "tapones" (tono del juego), a los demas
// personajes que estan cerca de alguno de ellos: si no, en un corner o una marca personal el VS del rival encontraria como
// mas cercana la entrada del jugador con tono propio y se pintaria con su tono
static const int kTabMax = 20;   // filas 195..254 (3 por entrada: posicion, cara, cuerpo)
void SkinTonePresent() {
    { std::lock_guard<std::mutex> lk(s_skinMx); if (s_skinDirty && Now() - s_skinDirtyT > 0.6) { s_skinDirty = false; SaveSkin(); } }
    for (int r = 194; r < 195 + 3 * kTabMax; r++) for (int c = 0; c < 4; c++) g_cfg.live[r][c] = 0.0f;
    // (26/10) variantes robusto / atletico de TorsoBody (pes_variants.hlsli): 264 = (t general, 1 = la tabla vale, prendido, -) y
    // 265 + i = t de la entrada i (los tapones llevan el general). x285 = 0 apaga las variantes.
    for (int r = 264; r < 265 + kTabMax; r++) for (int c = 0; c < 4; c++) g_cfg.live[r][c] = 0.0f;
    const bool varOn = g_cfg.live[285][0] > 0.5f;
    const float varGen = varOn ? MuscleVariantT(0, nullptr) : 0.0f;
    g_cfg.live[264][0] = varGen; g_cfg.live[264][2] = varOn ? 1.0f : 0.0f;
    // (26/10 b) la tabla tambien lleva la definicion muscular propia de cada jugador (w de la fila de la posicion; 0 = la general):
    // un jugador entra si tiene tono propio (x184 prendido), definicion propia o (26/10) variante propia
    const bool toneOn = g_cfg.live[184][0] > 0.5f, defOn = g_cfg.live[164][0] > 0.5f;
    if (!toneOn && !defOn) return;
    static CharInfo ci[kCharSlots]; static bool alive[kCharSlots];
    CharSnapshot(ci, alive);
    uint32_t eid = 0; const bool fis = FisicoEditing(&eid) && MusclePhysiqueActive();
    const int fgi = fis ? FisicoSkinIndex() : 0;
    s_measureWanted = false;
    const float k = g_cfg.live[184][1] > 0.0f ? g_cfg.live[184][1] : 1.0f;
    // en el menu Fisico hay un solo personaje: cualquier distancia sirve (asi la vista previa no depende de la comprobacion)
    const float maxD = fis ? 10000.0f : 1.4f;
    struct Ent { float pos[3], P[3], PB[3]; bool own; float d2; uint32_t id; int gi, tono, matiz, zona; float def; bool need; float tv; };
    static Ent e[kCharSlots]; int ne = 0, nOwn = 0;
    for (int sl = 0; sl < kCharSlots; sl++) {
        if (!alive[sl] || !ci[sl].node) continue;
        Ent& x = e[ne];
        if (!NodePos(ci[sl].node, x.pos)) continue;
        x.P[0] = 1.0f; x.P[1] = 1.0f; x.P[2] = 0.0f; x.PB[0] = 1.0f; x.PB[1] = 1.0f; x.PB[2] = 0.0f; x.own = false; x.d2 = 1e30f; x.id = ci[sl].known ? ci[sl].id : 0; x.gi = x.tono = x.matiz = x.zona = 0;
        x.def = 0.0f; x.need = false; x.tv = varGen;
        if (!x.id && fis) x.id = eid;   // (26/10 b) el modelo del menu Fisico sin HumanList: el jugador que se edita
        if (x.id && defOn) x.def = MuscleDefinitionFor(x.id);
        bool tvOwn = false;
        if (x.id && varOn) x.tv = MuscleVariantT(x.id, &tvOwn);   // (26/10) variante propia del torso / brazos
        if (x.id && toneOn) {
            if (fis && x.id == eid && fgi >= 1) SkinToneSetGame(x.id, fgi);   // recordar el color del juego de este jugador
            SkinToneProf q; const bool has = SkinToneGetProf(x.id, q);
            if (fis && x.id == eid) s_measureWanted = q.igualar != 0;
            if (has) {
                x.tono = q.tono; x.matiz = q.matiz;
                x.gi = (fis && x.id == eid && fgi >= 1) ? fgi : SkinToneGame(x.id);
                x.own = SkinToneParams(x.gi, q, k, x.P, x.PB);
                x.zona = std::clamp(q.zona, 0, 2);
                if (x.own && x.zona >= 1) { x.P[0] = 1.0f; x.P[1] = 1.0f; x.P[2] = 0.0f; }   // (24/10 g) solo el cuerpo / el torso: la cara como el juego
            }
        }
        x.need = x.own || x.def > 0.0f || tvOwn;
        if (x.need) nOwn++;
        ne++;
    }
    int n = 0, nBlk = 0; char line[1024]; int ln = 0; line[0] = 0;
    auto put = [&](const Ent& x) {
        float* A = g_cfg.live[195 + 3 * n]; float* B = g_cfg.live[196 + 3 * n]; float* C = g_cfg.live[197 + 3 * n];
        A[0] = x.pos[0]; A[1] = x.pos[1]; A[2] = x.pos[2]; A[3] = x.def;   // (26/10 b) definicion propia (0 = la general)
        B[0] = x.P[0]; B[1] = x.P[1]; B[2] = x.P[2]; B[3] = x.own ? 1.0f : 0.0f;
        C[0] = x.PB[0]; C[1] = x.PB[1]; C[2] = x.PB[2]; C[3] = (x.own && x.zona == 2) ? 2.0f : B[3];   // (24/10 g) 2 = solo el torso
        g_cfg.live[265 + n][0] = x.need ? x.tv : varGen;   // (26/10) variante (los tapones, la general)
        n++;
    };
    if (nOwn > 0) {
        for (int i = 0; i < ne && n < kTabMax; i++) if (e[i].need) {
            put(e[i]);
            if (ln < 900) ln += snprintf(line + ln, sizeof line - ln, " | %u (color %d, tono %d, matiz %d, definicion %.2f, variante %+.2f) en (%.1f, %.1f, %.1f) kL %.2f m %.2f %.2f", e[i].id, e[i].gi, e[i].tono, e[i].matiz, e[i].def, e[i].tv, e[i].pos[0], e[i].pos[1], e[i].pos[2], e[i].P[0], e[i].P[1], e[i].P[2]);
        }
        // tapones: los demas, por cercania al jugador con tono propio mas proximo (solo los que pueden confundirse)
        const float lim = (2.0f * maxD) * (2.0f * maxD);
        for (int i = 0; i < ne; i++) {
            if (e[i].need) continue;
            for (int j = 0; j < ne; j++) if (e[j].need) {
                const float dx = e[i].pos[0] - e[j].pos[0], dz = e[i].pos[2] - e[j].pos[2];
                e[i].d2 = (std::min)(e[i].d2, dx * dx + dz * dz);
            }
        }
        while (n < kTabMax) {
            int b = -1;
            for (int i = 0; i < ne; i++) if (!e[i].need && e[i].d2 < lim && (b < 0 || e[i].d2 < e[b].d2)) b = i;
            if (b < 0) break;
            put(e[b]); e[b].d2 = 1e30f; nBlk++;
        }
    }
    g_cfg.live[194][0] = (float)n; g_cfg.live[194][1] = n > 0 ? 1.0f : 0.0f; g_cfg.live[194][2] = maxD; g_cfg.live[194][3] = (float)s_posMode.load();
    g_cfg.live[264][1] = n > 0 ? 1.0f : 0.0f;
    g_skinMeasure = fis && s_measureWanted;
    g_cfg.live[192][2] = s_faceY[0]; g_cfg.live[192][3] = s_faceY[1];
    const double t = Now();
    if (n > 0 && t > s_tabLog) { s_tabLog = t + 30.0; Log("piel: tabla de tonos, %d con tono propio y %d cerca sin tono (de %d personajes; punto %d, nodo %d)%s", n - nBlk, nBlk, ne, s_posMode.load(), s_nodeLayout.load(), line); }
    if (ne > 0) ToneCheckPresent(ci, alive);
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

// ---------------------------------------------------------------- (24/10 c) comprobacion del punto de cada dibujo
// El VS de la piel elige el jugador por la distancia entre un punto del dibujo y la posicion de cada personaje (su nodo). Para
// no depender de suposiciones, cada 30 s (con personajes en pantalla) se copian en un cuadro los bloques de constantes de
// hasta 8 dibujos de piel (m_viewWorld, m_world y m_useWeightCount de b5, el hueso 0 de las dos paletas de b6 y m_view de b2)
// y 2 cuadros despues se calcula el punto de 4 maneras (como en pes_skintone.hlsli: 0 = hueso 0 x m_world, 1 = hueso 0 de la
// segunda paleta, 2 = traslacion de m_world, 3 = vista inversa x m_viewWorld x hueso 0) contra las dos formas de leer la
// posicion del nodo: se queda la combinacion con mas dibujos a menos de 0.6 m de algun personaje (w194 y s_nodeLayout).
std::atomic<bool> g_toneChkArmed{false};
static std::atomic<int> s_chkFrame{-1};
static const int kChkSlots = 8, kChkBytes = 320;
static ID3D11Buffer* s_chkStage = nullptr;
static std::mutex s_chkMx; static int s_chkN = 0; static uint64_t s_chkPS[kChkSlots], s_chkVS[kChkSlots];
static int s_chkRead = -1, s_chkTries = 0; static double s_chkNext = 0; static int s_chkLogs = 0;
static float s_chkNode[kCharSlots][16]; static int s_chkNodes = 0;
static bool CopyTo(ID3D11DeviceContext* c, ID3D11Buffer* src, UINT srcOff, UINT bytes, UINT dstOff) {
    if (!src || !s_chkStage) return false;
    D3D11_BUFFER_DESC d; src->GetDesc(&d);
    if (d.ByteWidth < srcOff + bytes || d.Usage == D3D11_USAGE_STAGING) return false;
    D3D11_BOX bx = { srcOff, 0, 0, srcOff + bytes, 1, 1 };
    c->CopySubresourceRegion(s_chkStage, 0, dstOff, 0, 0, src, 0, &bx);
    return true;
}
void SkinToneCheckDraw(ID3D11DeviceContext* c, uint64_t ps, uint64_t vs) {
    if (vs != 0x1c02a92ab4ae3362ULL && vs != 0xfaab87ee4ef8b783ULL && vs != 0x455eb97bc2c3a958ULL) return;
    if (PresentNo() != s_chkFrame.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(s_chkMx);
    if (s_chkN >= kChkSlots || !s_chkStage) return;
    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) || !c1) return;
    UINT f5 = 0, f6 = 0, f2 = 0;
    ID3D11Buffer* b5 = CbOf(c1, false, 5, &f5); ID3D11Buffer* b6 = CbOf(c1, false, 6, &f6); ID3D11Buffer* b2 = CbOf(c1, false, 2, &f2);
    c1->Release();
    const UINT o = (UINT)s_chkN * kChkBytes;
    // [0, 144) b5 c0..c8 | [144, 192) hueso 0 paleta 1 | [192, 240) hueso 0 paleta 2 | [240, 288) b2 c8..c10 (m_view)
    const bool ok = CopyTo(c, b5, f5 * 16, 144, o) && CopyTo(c, b6, f6 * 16, 48, o + 144) && CopyTo(c, b6, f6 * 16 + 1536, 48, o + 192) &&
                    CopyTo(c, b2, f2 * 16 + 128, 48, o + 240);
    if (b5) b5->Release(); if (b6) b6->Release(); if (b2) b2->Release();
    if (ok) { s_chkPS[s_chkN] = ps; s_chkVS[s_chkN] = vs; s_chkN++; }
}
// punto del dibujo de la manera "mode" con los bloques copiados (q: floats del lugar)
static void ChkPoint(const float* q, int mode, float out[3]) {
    const float* VW = q; const float* W = q + 16; const float sk = q[32];
    const float* B1 = q + 36; const float* B2 = q + 48; const float* V = q + 60;
    float b0[3] = { 0, 0, 0 };
    if (sk != 0.0f) { b0[0] = B1[3]; b0[1] = B1[7]; b0[2] = B1[11]; }
    if (mode == 1 && sk != 0.0f) { out[0] = B2[3]; out[1] = B2[7]; out[2] = B2[11]; return; }
    if (mode == 2 || mode == 1) { out[0] = W[3]; out[1] = W[7]; out[2] = W[11]; return; }
    if (mode == 3) {
        float v[3], d[3];
        for (int k = 0; k < 3; k++) v[k] = VW[4 * k] * b0[0] + VW[4 * k + 1] * b0[1] + VW[4 * k + 2] * b0[2] + VW[4 * k + 3];
        for (int k = 0; k < 3; k++) d[k] = v[k] - V[4 * k + 3];
        for (int j = 0; j < 3; j++) out[j] = V[j] * d[0] + V[4 + j] * d[1] + V[8 + j] * d[2];
        return;
    }
    for (int k = 0; k < 3; k++) out[k] = W[4 * k] * b0[0] + W[4 * k + 1] * b0[1] + W[4 * k + 2] * b0[2] + W[4 * k + 3];
}
static void ToneCheckPresent(const CharInfo* ci, const bool* alive) {
    const int f = PresentNo();
    if (s_chkRead >= 0 && f >= s_chkRead && g_dev.imm && s_chkStage) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_dev.imm->Map(s_chkStage, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
            int nd; { std::lock_guard<std::mutex> lk(s_chkMx); nd = s_chkN; s_chkN = 0; }
            int hits[4][2] = {}; float best[4][2][kChkSlots];
            for (int i = 0; i < nd; i++) {
                const float* q = (const float*)((const char*)m.pData + (size_t)i * kChkBytes);
                for (int md = 0; md < 4; md++) {
                    float pt[3]; ChkPoint(q, md, pt);
                    for (int L = 0; L < 2; L++) {
                        float bd = 1e30f;
                        for (int j = 0; j < s_chkNodes; j++) {
                            float np[3]; if (!NodePosL(s_chkNode[j], L, np)) continue;
                            const float dx = pt[0] - np[0], dz = pt[2] - np[2]; bd = (std::min)(bd, dx * dx + dz * dz);
                        }
                        best[md][L][i] = sqrtf(bd);
                        if (bd < 0.36f) hits[md][L]++;
                    }
                }
            }
            // la combinacion actual se mantiene si sigue entre las mejores; otra la reemplaza si gana dos comprobaciones seguidas
            int bm = -1, bl = 0, bh = 0;
            const int cm = s_posMode.load(), cl = s_nodeLayout.load();
            for (int md = 0; md < 4; md++) for (int L = 0; L < 2; L++) if (hits[md][L] > bh) { bh = hits[md][L]; bm = md; bl = L; }
            if (bm >= 0 && hits[cm][cl] == bh) { bm = cm; bl = cl; }
            const bool good = nd > 0 && bm >= 0 && bh >= 2 && bh * 2 >= nd;
            static int candM = -1, candL = -1, candN = 0;
            if (good && (bm != cm || bl != cl)) {
                if (bm == candM && bl == candL) candN++; else { candM = bm; candL = bl; candN = 1; }
                if (candN >= 2) { s_posMode = bm; s_nodeLayout = bl; candN = 0; candM = candL = -1; }
            } else if (good) { candN = 0; candM = candL = -1; }
            if (nd > 0 && s_chkLogs < 12) {
                s_chkLogs++;
                char l[1400]; int n = snprintf(l, sizeof l, "piel: comprobacion del punto de %d dibujos de piel (%d personajes): coinciden a menos de 0.6 m", nd, s_chkNodes);
                static const char* kM[4] = { "hueso x mundo", "paleta 2", "mundo", "vista inversa" };
                for (int md = 0; md < 4; md++) n += snprintf(l + n, sizeof l - n, " | %s %d/%d", kM[md], hits[md][0], hits[md][1]);
                n += snprintf(l + n, sizeof l - n, " -> %s (punto %d, nodo %d)", !good ? "ninguna alcanza, sigo con" : (bm == s_posMode.load() && bl == s_nodeLayout.load()) ? "uso" : "otra gana (se cambia si se repite), sigo con", s_posMode.load(), s_nodeLayout.load());
                if (nd > 0) {
                    const float* q = (const float*)m.pData; float pt[3];
                    n += snprintf(l + n, sizeof l - n, " | #0 = PS %08llx VS %08llx, pesos %.0f", (unsigned long long)(s_chkPS[0] >> 32), (unsigned long long)(s_chkVS[0] >> 32), q[32]);
                    for (int md = 0; md < 4 && n < 1200; md++) { ChkPoint(q, md, pt); n += snprintf(l + n, sizeof l - n, " | #0 %s (%.2f, %.2f, %.2f) a %.2f m", kM[md], pt[0], pt[1], pt[2], best[md][s_nodeLayout.load()][0]); }
                    float np[3];
                    if (s_chkNodes > 0 && NodePosL(s_chkNode[0], 0, np) && n < 1300) n += snprintf(l + n, sizeof l - n, " | nodo 0 fila (%.2f, %.2f, %.2f)", np[0], np[1], np[2]);
                    if (s_chkNodes > 0 && NodePosL(s_chkNode[0], 1, np) && n < 1360) n += snprintf(l + n, sizeof l - n, " columna (%.2f, %.2f, %.2f)", np[0], np[1], np[2]);
                }
                Log("%s", l);
            }
            g_dev.imm->Unmap(s_chkStage, 0);
            s_chkRead = -1;
        } else if (++s_chkTries > 20) { std::lock_guard<std::mutex> lk(s_chkMx); s_chkN = 0; s_chkRead = -1; }
    }
    const int armed = s_chkFrame.load();
    if (armed >= 0 && f > armed) {   // el cuadro capturado ya paso: leer 2 cuadros despues
        s_chkFrame = -1; g_toneChkArmed = false;
        int nd; { std::lock_guard<std::mutex> lk(s_chkMx); nd = s_chkN; }
        if (nd > 0) { s_chkRead = f + 2; s_chkTries = 0; }
    }
    const double t = Now();
    if (s_chkFrame.load() < 0 && s_chkRead < 0 && t > s_chkNext && g_dev.dev) {
        s_chkNext = t + 30.0;
        if (!s_chkStage) {
            D3D11_BUFFER_DESC d = {}; d.ByteWidth = kChkSlots * kChkBytes; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            g_dev.dev->CreateBuffer(&d, nullptr, &s_chkStage);
        }
        s_chkNodes = 0;
        for (int sl = 0; sl < kCharSlots && s_chkNodes < kCharSlots; sl++) if (alive[sl] && ci[sl].node && NodeMat(ci[sl].node, s_chkNode[s_chkNodes])) s_chkNodes++;
        if (s_chkNodes > 0 && s_chkStage) { std::lock_guard<std::mutex> lk(s_chkMx); s_chkN = 0; s_chkFrame = f + 1; g_toneChkArmed = true; }
    }
}

} // namespace fx
