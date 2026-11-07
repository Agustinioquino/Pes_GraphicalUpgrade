// PES21 FX (04/11, Agustin: "subir la cantidad de poligonos de las caras / teselacion, que se note realista, solo en primeros
// planos, sin deformar demasiado") TESELACION DE LA CARA: en los dibujos de piel de la cara (VS faab87ee / 455eb97b con una malla
// que expr.cpp reconocio como cara: cabeza en la paleta, no boca, no ojo, no pestanas) se ponen un hull y un domain shader
// (shaders\pes_face_tess_*.hlsl) y la topologia pasa a parches de 3 puntos; despues del dibujo se devuelve todo. El HS decide
// el factor por arista segun su largo en pantalla (fila 357) y el DS redondea con teselacion de Phong, respetando los bordes de
// cada pieza (peso de borde por vertice, t119 del VS, calculado en expr.cpp) y un tope en mm (fila 358). Ver pes_face_tess.hlsli.
#include "core.h"
#include <d3d11_1.h>
#include <mutex>
#include <map>
#include <atomic>
#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <cmath>

namespace fx {

static const uint64_t kVsHead = 0xfaab87ee4ef8b783ULL, kVsFace = 0x455eb97bc2c3a958ULL, kVsBody = 0x1c02a92ab4ae3362ULL;   // (04/11 g) cuerpo: manos, brazos, piernas
static std::mutex s_mx;
static bool s_tried = false;
static ID3D11HullShader* s_hs[3][2] = {};     // [variante del VS: 0 cabeza, 1 cara animada, 2 cuerpo][orientacion: 0 cw, 1 ccw]
static ID3D11DomainShader* s_ds[3] = {};
static ID3D11DomainShader* s_dsAtlas[2] = {};   // (04/11 piel) cara teselada en el atlas de las cabezas (headshadow.cpp)
static std::map<uint64_t, ID3D11Buffer*> s_vpCB;   // cbuffer b13 del HS por tamano de viewport (inmutables, pocos)
static std::atomic<int> s_draws{0}, s_skipTopo{0}, s_noShader{0}, s_skipFar{0}, s_drawsBody{0};
static std::atomic<int> s_lastVpW{0}, s_lastVpH{0};
static std::atomic<int> s_relief{0};
static std::atomic<int> s_wrinkle{0};   // (07/11) dibujos con el volumen de las arrugas enlazado al DS
static std::atomic<int> s_noSeam{0}, s_noPrm{0}, s_off{0};   // (04/11 d) dibujos de piel de la cara que NO se teselaron y por que
void FaceTessNote(int why) { (why == 0 ? s_noSeam : why == 1 ? s_noPrm : s_off).fetch_add(1, std::memory_order_relaxed); }   // (04/11 c) dibujos con la quinta textura en el DS (relieve)
static ID3D11SamplerState* s_samp = nullptr;

// ---------------------------------------------------------------- (04/11 m) teselacion por jugador
// Agustin: "la teselacion si puedes ofrecer opciones por jugador". Como las cejas por jugador (facegen.cpp): el "jugador actual" es
// la UNICA cara teselable dibujada en los ultimos ~30 cuadros (editor, presentacion). Al aparecer, las filas 371 / 372 toman sus
// valores (o 1 si no tiene); si se mueven, se aplican en vivo SOLO a esa cara; "Guardar teselacion de este jugador" los escribe en
// PES21FX_hair.ini [teselacion_cara] id = teselar, densidad, suavidad, tope, relieve, surcos, formas, relieve en la luz.
// Cada cara con valores tiene su cbuffer (b10 del HS / DS, pes_face_tess_player.hlsli); sin valores b10 queda vacio (= todo 1).
using TessVals = std::vector<float>;
static const int kTessVals = 8;
static TessVals TessDefault() { return TessVals(kTessVals, 1.f); }
static std::mutex s_pmx;                                  // todo lo de abajo
static std::map<std::string, TessVals> s_pSaved;          // lo leido de [teselacion_cara] (ids en minusculas)
static std::map<std::string, TessVals> s_pLive;           // lo tocado en el menu en esta sesion (aun sin guardar)
static std::set<std::string> s_pSeen;                     // caras teselables desde el ultimo corte
static std::string s_pCur;                                // jugador actual ("" = ninguno o varios)
struct TessFaceCB { ID3D11Buffer* b = nullptr; TessVals v; bool dirty = false; };
static std::map<std::string, TessFaceCB> s_pCB;
static std::atomic<int> s_pBound{0}, s_pOff{0};
static std::string TLower(std::string x) { for (auto& ch : x) ch = (char)tolower((unsigned char)ch); return x; }
static std::string TTrim(const std::string& x) { size_t a = x.find_first_not_of(" \t\r\n"), b = x.find_last_not_of(" \t\r\n"); return a == std::string::npos ? std::string() : x.substr(a, b - a + 1); }
static bool TessIsDefault(const TessVals& v) { for (float f : v) if (std::fabs(f - 1.f) > 1e-3f) return false; return true; }
// con s_pmx tomado: valores de esta cara (lo tocado en el menu o lo guardado); false = sin valores propios
static bool TessValsLocked(const std::string& id, TessVals& v) {
    auto it = s_pLive.find(id); if (it != s_pLive.end()) { v = it->second; return true; }
    auto jt = s_pSaved.find(id); if (jt != s_pSaved.end()) { v = jt->second; return true; }
    return false;
}
static void TessPack(const TessVals& v, float out[12]) {
    for (int i = 0; i < 8; i++) out[i] = i < (int)v.size() ? v[i] : 1.f;
    out[8] = 1.f; out[9] = out[10] = out[11] = 0.f;
}
void FaceTessSetSaved(const std::map<std::string, std::vector<float>>& m) {   // config.cpp LoadHairConfig
    std::lock_guard<std::mutex> lk(s_pmx);
    s_pSaved.clear();
    for (auto& kv : m) { TessVals v = TessDefault(); for (size_t i = 0; i < kv.second.size() && i < (size_t)kTessVals; i++) v[i] = kv.second[i]; s_pSaved[TLower(kv.first)] = v; }
    s_pLive.clear();   // recargar = lo que dice el archivo
    s_pCur.clear();    // el jugador actual se vuelve a detectar y los sliders toman lo guardado
    for (auto& kv : s_pCB) { TessVals v; if (!TessValsLocked(kv.first, v)) v = TessDefault(); if (v != kv.second.v) { kv.second.v = v; kv.second.dirty = true; } }
}
// cbuffer de esta cara para b10 (nullptr = sin valores propios); off = el jugador tiene la teselacion apagada
static ID3D11Buffer* TessFaceBuffer(const std::string& idIn, bool& off) {
    off = false;
    if (idIn.empty()) return nullptr;
    const std::string id = TLower(idIn);   // [teselacion_cara] se lee en minusculas
    std::lock_guard<std::mutex> lk(s_pmx);
    s_pSeen.insert(id);
    auto it = s_pCB.find(id);
    if (it != s_pCB.end() && it->second.b) { off = !it->second.v.empty() && it->second.v[0] < 0.5f; return it->second.b; }
    TessVals v; if (!TessValsLocked(id, v)) return nullptr;
    off = v[0] < 0.5f;
    if (s_pCB.size() > 256) return nullptr;   // no acumular sin fin
    float data[12]; TessPack(v, data);
    D3D11_BUFFER_DESC d{}; d.ByteWidth = sizeof(data); d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = data;
    ID3D11Buffer* b = nullptr; if (FAILED(g_dev.dev->CreateBuffer(&d, &sd, &b))) return nullptr;
    TessFaceCB& e = s_pCB[id]; e.b = b; e.v = v; e.dirty = false;
    return b;
}
// en Present: jugador actual, sliders 371 / 372, cbuffers cambiados
static void TessPlayerPresent() {
    static int frames = 0; static TessVals last;
    auto cur8 = []() { return TessVals{ g_cfg.live[371][0], g_cfg.live[371][1], g_cfg.live[371][2], g_cfg.live[371][3], g_cfg.live[372][0], g_cfg.live[372][1], g_cfg.live[372][2], g_cfg.live[372][3] }; };
    auto set8 = [](const TessVals& v) { for (int i = 0; i < 4; i++) { g_cfg.live[371][i] = v[i]; g_cfg.live[372][i] = v[4 + i]; } };
    std::lock_guard<std::mutex> lk(s_pmx);
    if (++frames >= 30) { frames = 0;
        std::string nc = s_pSeen.size() == 1 ? *s_pSeen.begin() : std::string(); s_pSeen.clear();
        if (nc != s_pCur) { s_pCur = nc;
            TessVals v = TessDefault();
            const bool own = !nc.empty() && TessValsLocked(nc, v);
            set8(v);
            if (!nc.empty()) Log("teselacion por jugador: jugador actual = cara %s (%s: teselar %s, densidad x%.2f, suavidad x%.2f, tope x%.2f, relieve x%.2f, surcos x%.2f, formas x%.2f, relieve en la luz x%.2f)",
                nc.c_str(), own ? "valores propios" : "como todos", v[0] > 0.5f ? "si" : "no", v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
            last = cur8(); } }
    TessVals now = cur8();
    if (!s_pCur.empty() && now != last) {
        last = now;
        s_pLive[s_pCur] = now;
        auto it = s_pCB.find(s_pCur); if (it != s_pCB.end()) { it->second.v = now; it->second.dirty = true; }
    }
    if (g_dev.imm) for (auto& kv : s_pCB) if (kv.second.dirty && kv.second.b) { float data[12]; TessPack(kv.second.v, data); g_dev.imm->UpdateSubresource(kv.second.b, 0, nullptr, data, 0, 0); kv.second.dirty = false; }
}
// "Guardar teselacion de este jugador" (menu Teselacion de la cara): PES21FX_hair.ini [teselacion_cara]; todo en 1 = se borra la linea
std::string FaceTessSaveCurrent() {
    std::string cur; { std::lock_guard<std::mutex> lk(s_pmx); cur = s_pCur; }
    if (cur.empty()) return "Teselacion: no hay un jugador solo en pantalla (abri el editor con un jugador de cerca)";
    TessVals v = { g_cfg.live[371][0] > 0.5f ? 1.f : 0.f, g_cfg.live[371][1], g_cfg.live[371][2], g_cfg.live[371][3], g_cfg.live[372][0], g_cfg.live[372][1], g_cfg.live[372][2], g_cfg.live[372][3] };
    const bool erase = TessIsDefault(v);
    const std::wstring path = g_cfg.baseDir + L"PES21FX_hair.ini";
    std::vector<std::string> lines; { std::ifstream f(path); std::string ln; while (std::getline(f, ln)) { if (!ln.empty() && ln.back() == '\r') ln.pop_back(); lines.push_back(ln); } }
    char val[200]; snprintf(val, sizeof(val), "%s = %.0f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f", cur.c_str(), v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    int sec = -1, end = (int)lines.size(), hit = -1;
    for (int i = 0; i < (int)lines.size(); i++) {
        std::string t = TLower(TTrim(lines[i]));
        if (!t.empty() && t[0] == '[') { if (sec >= 0 && end == (int)lines.size()) end = i; if (t.rfind("[teselacion_cara]", 0) == 0) { sec = i; end = (int)lines.size(); } continue; }
        if (sec >= 0 && i < end) { size_t eq = t.find('='); if (eq != std::string::npos && TTrim(t.substr(0, eq)) == TLower(cur)) hit = i; }
    }
    if (erase) { if (hit >= 0) lines.erase(lines.begin() + hit); }
    else if (hit >= 0) lines[hit] = val;
    else if (sec >= 0) { int at = end; while (at > sec + 1 && TTrim(lines[at - 1]).empty()) at--; lines.insert(lines.begin() + at, val); }
    else { lines.push_back(""); lines.push_back("[teselacion_cara]"); lines.push_back("; (04/11 m) teselacion por jugador: id = teselar (1 / 0), densidad, suavidad, tope, relieve, surcos, formas, relieve en la luz (multiplicadores; los escribe el menu Teselacion de la cara > Guardar)"); lines.push_back(val); }
    { std::ofstream f(path, std::ios::binary | std::ios::trunc); if (!f) return "Teselacion: no se pudo escribir PES21FX_hair.ini";
      for (auto& ln : lines) f << ln << "\r\n"; }
    { std::lock_guard<std::mutex> lk(s_pmx);
      if (erase) s_pSaved.erase(cur); else s_pSaved[cur] = v;
      s_pLive[cur] = v;
      auto it = s_pCB.find(cur); if (it != s_pCB.end()) { it->second.v = v; it->second.dirty = true; } }
    Log("teselacion por jugador: %s cara %s: teselar %s, densidad x%.2f, suavidad x%.2f, tope x%.2f, relieve x%.2f, surcos x%.2f, formas x%.2f, relieve en la luz x%.2f",
        erase ? "borrada (todo en 1)" : "guardada", cur.c_str(), v[0] > 0.5f ? "si" : "no", v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    char msg[200];
    if (erase) snprintf(msg, sizeof(msg), "Teselacion de la cara %s: todo en 1, vuelve a la de todos", cur.c_str());
    else snprintf(msg, sizeof(msg), "Teselacion guardada para la cara %s%s", cur.c_str(), v[0] > 0.5f ? "" : " (apagada)");
    return msg;
}

static void Ensure() {
    if (s_tried || !g_dev.dev) return;
    s_tried = true;
    const wchar_t* hs[3][2] = { { L"pes_face_tess_a_cw_hs.hlsl", L"pes_face_tess_a_ccw_hs.hlsl" }, { L"pes_face_tess_b_cw_hs.hlsl", L"pes_face_tess_b_ccw_hs.hlsl" }, { L"pes_face_tess_c_cw_hs.hlsl", L"pes_face_tess_c_ccw_hs.hlsl" } };
    const wchar_t* ds[3] = { L"pes_face_tess_a_ds.hlsl", L"pes_face_tess_b_ds.hlsl", L"pes_face_tess_c_ds.hlsl" };
    int ok = 0;
    for (int v = 0; v < 3; v++) {
        for (int w = 0; w < 2; w++) if (ID3DBlob* b = CompileNamedShader(hs[v][w], "hs_5_0")) { if (SUCCEEDED(g_dev.dev->CreateHullShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_hs[v][w]))) ok++; b->Release(); }
        if (ID3DBlob* b = CompileNamedShader(ds[v], "ds_5_0")) { if (SUCCEEDED(g_dev.dev->CreateDomainShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_ds[v]))) ok++; b->Release(); }
    }
    const wchar_t* dsA[2] = { L"pes_face_tess_a_atlas_ds.hlsl", L"pes_face_tess_b_atlas_ds.hlsl" };
    int okA = 0;
    for (int v = 0; v < 2; v++) if (ID3DBlob* b = CompileNamedShader(dsA[v], "ds_5_0")) { if (SUCCEEDED(g_dev.dev->CreateDomainShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_dsAtlas[v]))) okA++; b->Release(); }
    Log("teselacion de caras: DS del atlas de las cabezas %d de 2", okA);
    if (!s_samp) { D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX; g_dev.dev->CreateSamplerState(&sd, &s_samp); }
    Log("teselacion de caras: shaders %d de 9 %s (6 de la cara + 3 del cuerpo)", ok, ok == 9 ? "listos" : "(faltan: ver errores de compilacion)");
}

void FaceTessReloadShaders() {
    std::lock_guard<std::mutex> lk(s_mx);
    // los objetos viejos no se liberan: un dibujo de otro hilo puede tenerlos puestos en este momento (igual que los reemplazos)
    for (auto& a : s_hs) for (auto& h : a) h = nullptr;
    for (auto& d : s_ds) d = nullptr;
    for (auto& d : s_dsAtlas) d = nullptr;
    s_tried = false;
}

static ID3D11Buffer* VpCB(float w, float h) {
    const uint64_t key = ((uint64_t)(UINT)w << 32) | (UINT)h;
    std::lock_guard<std::mutex> lk(s_mx);
    auto it = s_vpCB.find(key);
    if (it != s_vpCB.end()) return it->second;
    if (s_vpCB.size() > 64) return nullptr;   // algo raro (viewports que cambian todo el tiempo): no acumular
    const float v[4] = { w, h, w > 0 ? 1.0f / w : 0, h > 0 ? 1.0f / h : 0 };
    D3D11_BUFFER_DESC d{}; d.ByteWidth = 16; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA s{}; s.pSysMem = v;
    ID3D11Buffer* b = nullptr; if (FAILED(g_dev.dev->CreateBuffer(&d, &s, &b))) return nullptr;
    s_vpCB[key] = b; return b;
}

ID3D11DomainShader* FaceTessAtlasDS(int v) {
    if (v < 0 || v > 1) return nullptr;
    std::lock_guard<std::mutex> lk(s_mx); Ensure(); return s_dsAtlas[v];
}

// ---------------------------------------------------------------- (07/11) MEDIDOR DE GPU de la teselacion
// Agustin: "que se mejore el detalle y que no consuma todo el rendimiento". Hasta 4 dibujos teselados por cuadro llevan consultas:
// marca de tiempo antes y despues del dibujo (con la repeticion en el atlas de las cabezas) y estadisticas del pipeline (triangulos que
// entran al HS y triangulos que salen del DS). Se graban en el contexto del dibujo (diferido o no) y se leen en el inmediato al menos 3
// Presents despues, sin esperar. Resumen cada 60 s: ms por dibujo, ms por cuadro estimado (por dibujos por cuadro) y cuanto multiplica
// los triangulos, por separado cara y cuerpo. Todo-o-nada al crear (como el medidor del pasto, sin End sobre consultas nulas).
static const int kTessTimers = 16;
struct TessTimer { ID3D11Query* dis = nullptr; ID3D11Query* t0 = nullptr; ID3D11Query* t1 = nullptr; ID3D11Query* ps = nullptr; std::atomic<int> tag{-1}; int kind = 0; };
static TessTimer s_tt[kTessTimers];
static std::atomic<int> s_ttState{0}, s_ttFrame{0}, s_ttClaimFrame{-1}, s_ttClaimN{0};
static std::mutex s_ttMx;
static double s_ttMs[2] = {}, s_ttMsMax[2] = {}; static unsigned long long s_ttIn[2] = {}, s_ttOut[2] = {}; static int s_ttN[2] = {}, s_ttDisj = 0, s_ttLost = 0;
static bool TessTimersReady() {
    const int st = s_ttState.load(); if (st) return st == 1;
    std::lock_guard<std::mutex> lk(s_ttMx);
    if (s_ttState.load()) return s_ttState.load() == 1;
    bool ok = g_dev.dev != nullptr; D3D11_QUERY_DESC qd = {};
    for (auto& t : s_tt) { if (!ok) break;
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT; if (FAILED(g_dev.dev->CreateQuery(&qd, &t.dis))) { t.dis = nullptr; ok = false; break; }
        qd.Query = D3D11_QUERY_TIMESTAMP; if (FAILED(g_dev.dev->CreateQuery(&qd, &t.t0))) { t.t0 = nullptr; ok = false; break; }
        if (FAILED(g_dev.dev->CreateQuery(&qd, &t.t1))) { t.t1 = nullptr; ok = false; break; }
        qd.Query = D3D11_QUERY_PIPELINE_STATISTICS; if (FAILED(g_dev.dev->CreateQuery(&qd, &t.ps))) { t.ps = nullptr; ok = false; break; }
    }
    if (!ok) { for (auto& t : s_tt) { for (ID3D11Query** q : { &t.dis, &t.t0, &t.t1, &t.ps }) { if (*q) (*q)->Release(); *q = nullptr; } }
               Log("teselacion GPU: no se pudieron crear las consultas (sin medidor)"); }
    s_ttState.store(ok ? 1 : 2);
    return ok;
}
static int TessTimerClaim(int kind) {
    if (!TessTimersReady()) return -1;
    const int f = s_ttFrame.load(std::memory_order_relaxed);
    int cf = s_ttClaimFrame.load(); if (cf != f && s_ttClaimFrame.compare_exchange_strong(cf, f)) s_ttClaimN.store(0);
    if (s_ttClaimN.fetch_add(1) >= 4) return -1;
    for (int i = 0; i < kTessTimers; i++) { int e = -1; TessTimer& t = s_tt[i];
        if (t.tag.compare_exchange_strong(e, -2)) {   // -2 = grabando
            if (!t.dis || !t.t0 || !t.t1 || !t.ps) { t.tag = -1; return -1; }
            t.kind = kind; return i; } }
    return -1;
}
static void TessTimersRead() {   // contexto inmediato, en Present
    const int f = s_ttFrame.load(std::memory_order_relaxed);
    for (auto& t : s_tt) { const int tg = t.tag.load(); if (tg < 0 || tg > f - 3) continue;
        if (f - tg > 90) { t.tag = -1; s_ttLost++; continue; }   // lista diferida que nunca se ejecuto
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d = {};
        if (g_dev.imm->GetData(t.dis, &d, sizeof d, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        UINT64 a = 0, b = 0; D3D11_QUERY_DATA_PIPELINE_STATISTICS ps = {};
        if (g_dev.imm->GetData(t.t0, &a, 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK || g_dev.imm->GetData(t.t1, &b, 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK
            || g_dev.imm->GetData(t.ps, &ps, sizeof ps, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        const int k = t.kind; t.tag = -1;
        if (d.Disjoint || !d.Frequency) { s_ttDisj++; continue; }
        const double ms = b >= a ? (double)(b - a) * 1000.0 / (double)d.Frequency : 0.0;
        s_ttMs[k] += ms; if (ms > s_ttMsMax[k]) s_ttMsMax[k] = ms; s_ttIn[k] += ps.HSInvocations; s_ttOut[k] += ps.CInvocations; s_ttN[k]++;
    }
}

bool FaceTessBegin(ID3D11DeviceContext* c, uint64_t vsHash, ID3D11ShaderResourceView* seam, ID3D11ShaderResourceView* params, FaceTessScope& s, const std::string& faceId) {
    if (!seam || !params || !g_dev.dev) return false;
    const int v = vsHash == kVsHead ? 0 : vsHash == kVsFace ? 1 : vsHash == kVsBody ? 2 : -1;
    if (v < 0) return false;
    // (04/11 m) valores propios de esta cara (y la anota para el "jugador actual" aunque la teselacion este apagada)
    bool pOff = false; ID3D11Buffer* pcb = v < 2 ? TessFaceBuffer(faceId, pOff) : nullptr;
    if (pOff) { s_pOff.fetch_add(1, std::memory_order_relaxed); return false; }
    if (g_cfg.live[v == 2 ? 364 : 357][0] < 0.5f) { if (v < 2) s_off.fetch_add(1, std::memory_order_relaxed); return false; }   // (04/11 g) cuerpo: fila 364
    const int w = g_cfg.live[358][3] > 0.5f ? 1 : 0;
    ID3D11HullShader* hs; ID3D11DomainShader* ds;
    { std::lock_guard<std::mutex> lk(s_mx); Ensure(); hs = s_hs[v][w]; ds = s_ds[v]; }
    if (!hs || !ds) { s_noShader.fetch_add(1, std::memory_order_relaxed); return false; }
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED; c->IAGetPrimitiveTopology(&topo);
    if (topo != D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) { s_skipTopo.fetch_add(1, std::memory_order_relaxed); return false; }
    // viewport del dibujo (el HS mide las aristas en pixeles); la distancia maxima (solo primeros planos) la mira el HS por triangulo
    D3D11_VIEWPORT vp{}; UINT n = 1; c->RSGetViewports(&n, &vp);
    if (n < 1 || vp.Width < 1 || vp.Height < 1) return false;
    ID3D11Buffer* vb = VpCB(vp.Width, vp.Height); if (!vb) return false;
    s_lastVpW = (int)vp.Width; s_lastVpH = (int)vp.Height;
    // b2 (g_vsScene) del VS al DS, respetando la ventana (first / num) si el juego la enlazo con *SetConstantBuffers1
    ID3D11Buffer* b2 = nullptr; UINT first = 0, num = 0; ID3D11DeviceContext1* c1 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) {
        c1->VSGetConstantBuffers1(2, 1, &b2, &first, &num);
        // (04/11 d) ventana solo si es real (VSSetConstantBuffers comun devuelve 0 / 4096: como en hooks.cpp RefCB)
        if (b2) { D3D11_BUFFER_DESC bd; b2->GetDesc(&bd); const bool win = num && (first > 0 || num * 16 < bd.ByteWidth);
                  if (win) c1->DSSetConstantBuffers1(2, 1, &b2, &first, &num); else c1->DSSetConstantBuffers(2, 1, &b2); }
        c1->Release();
    } else { c->VSGetConstantBuffers(2, 1, &b2); if (b2) c->DSSetConstantBuffers(2, 1, &b2); }
    if (b2) b2->Release();
    c->HSSetShader(hs, nullptr, 0); c->DSSetShader(ds, nullptr, 0);
    c->HSSetConstantBuffers(13, 1, &vb);
    c->HSSetConstantBuffers(10, 1, &pcb); c->DSSetConstantBuffers(10, 1, &pcb);   // (04/11 m) por jugador (nullptr = como todos)
    if (pcb) s_pBound.fetch_add(1, std::memory_order_relaxed);
    c->HSSetShaderResources(120, 1, &params); c->DSSetShaderResources(120, 1, &params);
    // (04/11 c) relieve: la quinta textura de la cara (t23 / t24 del PS, la pone la regla facegen antes) tambien al DS
    if (v < 2) { ID3D11ShaderResourceView* fab[2] = {}; c->PSGetShaderResources(23, 2, fab);   // (04/11 g) el cuerpo no tiene quinta textura: sin relieve
      if (fab[0] && fab[1]) { c->DSSetShaderResources(23, 2, fab); if (s_samp) c->DSSetSamplers(0, 1, &s_samp); s.relief = true; s_relief.fetch_add(1, std::memory_order_relaxed); }
      for (auto* v : fab) if (v) v->Release(); }
    // (07/11) volumen de las arrugas (z366 / w366): lo que el PS de la piel usa para las arrugas, tambien al DS: b4 (pesos por zona
    // del motor, con su ventana si la tiene), t5 / t6 (normal y mascara del juego), t20 / t21 (las nuestras), t31 (esculpidas)
    if (v < 2 && (g_cfg.live[366][2] > 0.001f || g_cfg.live[366][3] > 0.001f)) {
      ID3D11ShaderResourceView* w2[2] = {}; c->PSGetShaderResources(5, 2, w2); c->DSSetShaderResources(5, 2, w2); for (auto* x : w2) if (x) x->Release();
      ID3D11ShaderResourceView* w3[2] = {}; c->PSGetShaderResources(20, 2, w3); c->DSSetShaderResources(20, 2, w3); for (auto* x : w3) if (x) x->Release();
      ID3D11ShaderResourceView* w4 = nullptr; c->PSGetShaderResources(31, 1, &w4); c->DSSetShaderResources(31, 1, &w4); if (w4) w4->Release();
      ID3D11Buffer* b4 = nullptr; UINT f4 = 0, n4 = 0; ID3D11DeviceContext1* c4 = nullptr;
      if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c4)) && c4) {
        c4->PSGetConstantBuffers1(4, 1, &b4, &f4, &n4);
        if (b4) { D3D11_BUFFER_DESC bd; b4->GetDesc(&bd); const bool win = n4 && (f4 > 0 || n4 * 16 < bd.ByteWidth);
                  if (win) c4->DSSetConstantBuffers1(4, 1, &b4, &f4, &n4); else c4->DSSetConstantBuffers(4, 1, &b4); }
        c4->Release();
      } else { c->PSGetConstantBuffers(4, 1, &b4); if (b4) c->DSSetConstantBuffers(4, 1, &b4); }
      if (b4) { b4->Release(); s_wrinkle.fetch_add(1, std::memory_order_relaxed); }
      if (!s_samp) { /* sin sampler propio el DS lee con el de defecto */ } else c->DSSetSamplers(0, 1, &s_samp);
    }
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    s.on = true; s.topo = topo; s.v = v;
    s.tm = TessTimerClaim(v == 2 ? 1 : 0);
    if (s.tm >= 0) { TessTimer& t = s_tt[s.tm]; c->Begin(t.dis); c->End(t.t0); c->Begin(t.ps); }
    (v == 2 ? s_drawsBody : s_draws).fetch_add(1, std::memory_order_relaxed);
    return true;
}

void FaceTessEnd(ID3D11DeviceContext* c, FaceTessScope& s) {
    if (!s.on) return;
    s.on = false;
    if (s.tm >= 0 && s.tm < kTessTimers) { TessTimer& t = s_tt[s.tm]; c->End(t.ps); c->End(t.t1); c->End(t.dis); t.tag.store(s_ttFrame.load(std::memory_order_relaxed)); }
    s.tm = -1;
    c->IASetPrimitiveTopology(s.topo);
    c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* nb = nullptr; ID3D11ShaderResourceView* nv = nullptr;
    c->HSSetConstantBuffers(13, 1, &nb); c->DSSetConstantBuffers(2, 1, &nb);
    c->HSSetConstantBuffers(10, 1, &nb); c->DSSetConstantBuffers(10, 1, &nb);
    c->HSSetShaderResources(120, 1, &nv); c->DSSetShaderResources(120, 1, &nv);
    if (s.relief) { ID3D11ShaderResourceView* n2[2] = {}; ID3D11SamplerState* ns = nullptr; c->DSSetShaderResources(23, 2, n2); c->DSSetSamplers(0, 1, &ns); s.relief = false; }
    if (s.v >= 0 && s.v < 2) {   // (07/11) volumen de las arrugas: se suelta siempre (si no se enlazo, poner nulos no cuesta nada)
      ID3D11ShaderResourceView* z2[2] = {}; c->DSSetShaderResources(5, 2, z2); c->DSSetShaderResources(20, 2, z2); c->DSSetShaderResources(31, 1, z2);
      c->DSSetConstantBuffers(4, 1, &nb); ID3D11SamplerState* ns = nullptr; c->DSSetSamplers(0, 1, &ns);
    }
}

void FaceTessPresent() {
    TessPlayerPresent();   // (04/11 m)
    s_ttFrame.fetch_add(1, std::memory_order_relaxed);
    if (s_ttState.load() == 1 && g_dev.imm) TessTimersRead();
    static int frames = 0;
    if (++frames < 3600) return;
    frames = 0;
    {   // (07/11) medidor de GPU
        const char* nm[2] = { "cara", "cuerpo" }; char l[512]; int k = 0;
        const double dpf[2] = { s_draws.load() / 3600.0, s_drawsBody.load() / 3600.0 };   // dibujos teselados por cuadro (antes de vaciar los contadores abajo)
        for (int i = 0; i < 2; i++) { if (!s_ttN[i]) continue;
            const double avg = s_ttMs[i] / s_ttN[i];
            k += snprintf(l + k, sizeof l - k, "%s %.3f ms por dibujo (max %.3f, %d muestras) x %.1f dibujos por cuadro = ~%.2f ms por cuadro, triangulos x%.1f (%.0f -> %.0f por dibujo); ",
                nm[i], avg, s_ttMsMax[i], s_ttN[i], dpf[i], avg * dpf[i], s_ttIn[i] ? (double)s_ttOut[i] / (double)s_ttIn[i] : 0.0, (double)s_ttIn[i] / s_ttN[i], (double)s_ttOut[i] / s_ttN[i]);
            s_ttMs[i] = s_ttMsMax[i] = 0; s_ttIn[i] = s_ttOut[i] = 0; s_ttN[i] = 0; }
        if (k || s_ttDisj || s_ttLost) Log("teselacion GPU (60 s): %s%d sin dato (disjoint), %d perdidas", k ? l : "", s_ttDisj, s_ttLost);
        s_ttDisj = s_ttLost = 0;
    }
    { const int pb = s_pBound.exchange(0), po = s_pOff.exchange(0); size_t ns; { std::lock_guard<std::mutex> lk(s_pmx); ns = s_pSaved.size(); }
      if (pb || po || ns) Log("teselacion por jugador: %d caras guardadas; en 60 s %d dibujos con valores propios, %d sin teselar (apagada para ese jugador)", (int)ns, pb, po); }
    const int d = s_draws.exchange(0), st = s_skipTopo.exchange(0), ns = s_noShader.exchange(0), rl = s_relief.exchange(0), db = s_drawsBody.exchange(0);
    { const int wr = s_wrinkle.exchange(0); if (wr) Log("volumen de las arrugas: %d dibujos con pesos y mascaras en el DS en 60 s (surco %.2f mm, cresta %.2f mm)", wr, g_cfg.live[366][2], g_cfg.live[366][3]); }
    const int nsm = s_noSeam.exchange(0), npr = s_noPrm.exchange(0), off = s_off.exchange(0);
    if (nsm || npr || off) Log("teselacion de caras: dibujos de piel de la cara SIN teselar en 60 s: %d sin pesos de borde (malla en lectura / no teselable), %d sin parametros t120, %d con la tarjeta apagada", nsm, npr, off);
    if (!d && !st && !ns && !db) return;
    Log("teselacion de caras: %d dibujos teselados en 60 s (con relieve %d; sin triangulos sueltos %d, sin shader %d); cuerpo (manos, brazos, piernas): %d dibujos, densidad %.0f, hasta %.1f m, tope %.1f mm; viewport %dx%d; suavidad %.2f, densidad %.1f, factor maximo %.0f, hasta %.2f m (+%.2f), tope %.1f mm, orientacion %s%s; relieve x%.2f surcos %.2f mm relieve %.2f mm mip %.0f",
        d, rl, st, ns, db, g_cfg.live[364][1], g_cfg.live[364][2], g_cfg.live[364][3], s_lastVpW.load(), s_lastVpH.load(), g_cfg.live[357][1], g_cfg.live[357][2], g_cfg.live[357][3], g_cfg.live[358][0], g_cfg.live[358][1], g_cfg.live[358][2], g_cfg.live[358][3] > 0.5f ? "invertida" : "igual", g_cfg.live[360][0] > 0.5f ? " (PRUEBA: cara inflada)" : "", g_cfg.live[359][0], g_cfg.live[359][1], g_cfg.live[359][2], g_cfg.live[359][3]);
}

}   // namespace fx
