// PES21 FX (10/11, Agustin: "hay jugadores que tienen arrugas bakeadas, deberias agregar un slider por jugador para bajar la intensidad
// o la zona asi como la edad") ARRUGAS POR JUGADOR. Igual que la teselacion por jugador (facetess.cpp): el "jugador actual" es la unica
// cara dibujada con la piel de la cara en los ultimos ~30 cuadros (editor, presentacion). Al aparecer, las filas 427 / 428 toman sus
// valores (o los de defecto); si se mueven, se aplican en vivo SOLO a esa cara; "Guardar arrugas de este jugador" los escribe en
// PES21FX_hair.ini [arrugas_cara] id = intensidad, frente y entrecejo, ojos, surco y mejilla, menton y marioneta, cuello, anios de
// piel (+ / -), arrugas de reposo. Cada cara con valores tiene su cbuffer (PS b13 / DS b9, pes_wrdepth.hlsli PESWrPlayer); sin
// valores queda vacio (= como todos). Solo cambia las arrugas del mod: las pintadas en la textura de la cara no se tocan.
#include "core.h"
#include <d3d11_1.h>
#include <mutex>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <cmath>

namespace fx {

namespace {
using Vals = std::vector<float>;
const int kN = 8;
Vals Default() { return Vals{ 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 1.f }; }
std::mutex s_mx;
std::map<std::string, Vals> s_saved, s_live;
std::set<std::string> s_seen;
std::string s_cur;
struct CB { ID3D11Buffer* b = nullptr; Vals v; bool dirty = false; };
std::map<std::string, CB> s_cb;
std::string Low(std::string x) { for (auto& ch : x) ch = (char)tolower((unsigned char)ch); return x; }
std::string Trim(const std::string& x) { size_t a = x.find_first_not_of(" \t\r\n"), b = x.find_last_not_of(" \t\r\n"); return a == std::string::npos ? std::string() : x.substr(a, b - a + 1); }
bool IsDefault(const Vals& v) { const Vals d = Default(); for (int i = 0; i < kN; i++) if (std::fabs(v[i] - d[i]) > 1e-3f) return false; return true; }
bool ValsLocked(const std::string& id, Vals& v) {
    auto it = s_live.find(id); if (it != s_live.end()) { v = it->second; return true; }
    auto jt = s_saved.find(id); if (jt != s_saved.end()) { v = jt->second; return true; }
    return false;
}
void Pack(const Vals& v, float out[12]) { for (int i = 0; i < 8; i++) out[i] = v[i]; out[8] = 1.f; out[9] = out[10] = out[11] = 0.f; }
Vals Cur8() { return Vals{ g_cfg.live[427][0], g_cfg.live[427][1], g_cfg.live[427][2], g_cfg.live[427][3], g_cfg.live[428][0], g_cfg.live[428][1], g_cfg.live[428][2], g_cfg.live[428][3] }; }
}

void WrPlayerSetSaved(const std::map<std::string, std::vector<float>>& m) {   // config.cpp LoadHairConfig
    std::lock_guard<std::mutex> lk(s_mx);
    s_saved.clear();
    for (auto& kv : m) { Vals v = Default(); for (size_t i = 0; i < kv.second.size() && i < (size_t)kN; i++) v[i] = kv.second[i]; s_saved[Low(kv.first)] = v; }
    s_live.clear(); s_cur.clear();
    for (auto& kv : s_cb) { Vals v; if (!ValsLocked(kv.first, v)) v = Default(); if (v != kv.second.v) { kv.second.v = v; kv.second.dirty = true; } }
}

// cbuffer de esta cara (nullptr = sin valores propios: como todos)
ID3D11Buffer* WrPlayerBuffer(const std::string& idIn) {
    if (idIn.empty() || !g_dev.dev) return nullptr;
    const std::string id = Low(idIn);
    std::lock_guard<std::mutex> lk(s_mx);
    s_seen.insert(id);
    auto it = s_cb.find(id);
    if (it != s_cb.end() && it->second.b) return it->second.b;
    Vals v; if (!ValsLocked(id, v)) return nullptr;
    if (s_cb.size() > 256) return nullptr;
    float data[12]; Pack(v, data);
    D3D11_BUFFER_DESC d{}; d.ByteWidth = sizeof(data); d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = data;
    ID3D11Buffer* b = nullptr; if (FAILED(g_dev.dev->CreateBuffer(&d, &sd, &b))) return nullptr;
    CB& e = s_cb[id]; e.b = b; e.v = v; e.dirty = false;
    return b;
}

void WrPlayerPresent() {
    static int frames = 0; static Vals last;
    std::lock_guard<std::mutex> lk(s_mx);
    if (++frames >= 30) { frames = 0;
        std::string nc = s_seen.size() == 1 ? *s_seen.begin() : std::string(); s_seen.clear();
        if (nc != s_cur) { s_cur = nc;
            Vals v = Default();
            const bool own = !nc.empty() && ValsLocked(nc, v);
            for (int i = 0; i < 4; i++) { g_cfg.live[427][i] = v[i]; g_cfg.live[428][i] = v[4 + i]; }
            if (!nc.empty()) Log("arrugas por jugador: jugador actual = cara %s (%s: intensidad x%.2f, frente x%.2f, ojos x%.2f, surco x%.2f, menton x%.2f, cuello x%.2f, piel %+.0f anios, reposo x%.2f)",
                nc.c_str(), own ? "valores propios" : "como todos", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
            last = Cur8(); } }
    Vals now = Cur8();
    if (!s_cur.empty() && now != last) {
        last = now;
        s_live[s_cur] = now;
        auto it = s_cb.find(s_cur); if (it != s_cb.end()) { it->second.v = now; it->second.dirty = true; }
    }
    if (g_dev.imm) for (auto& kv : s_cb) if (kv.second.dirty && kv.second.b) { float data[12]; Pack(kv.second.v, data); g_dev.imm->UpdateSubresource(kv.second.b, 0, nullptr, data, 0, 0); kv.second.dirty = false; }
}

// boton wr_save: PES21FX_hair.ini [arrugas_cara]; todo en defecto = se borra la linea
std::string WrPlayerSaveCurrent() {
    std::string cur; { std::lock_guard<std::mutex> lk(s_mx); cur = s_cur; }
    if (cur.empty()) return "Arrugas: no hay un jugador solo en pantalla (abri el editor con un jugador de cerca)";
    Vals v = Cur8();
    const bool erase = IsDefault(v);
    const std::wstring path = g_cfg.baseDir + L"PES21FX_hair.ini";
    std::vector<std::string> lines; { std::ifstream f(path); std::string ln; while (std::getline(f, ln)) { if (!ln.empty() && ln.back() == '\r') ln.pop_back(); lines.push_back(ln); } }
    char val[220]; snprintf(val, sizeof(val), "%s = %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.0f, %.2f", cur.c_str(), v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    int sec = -1, end = (int)lines.size(), hit = -1;
    for (int i = 0; i < (int)lines.size(); i++) {
        std::string t = Low(Trim(lines[i]));
        if (!t.empty() && t[0] == '[') { if (sec >= 0 && end == (int)lines.size()) end = i; if (t.rfind("[arrugas_cara]", 0) == 0) { sec = i; end = (int)lines.size(); } continue; }
        if (sec >= 0 && i < end) { size_t eq = t.find('='); if (eq != std::string::npos && Trim(t.substr(0, eq)) == Low(cur)) hit = i; }
    }
    if (erase) { if (hit >= 0) lines.erase(lines.begin() + hit); }
    else if (hit >= 0) lines[hit] = val;
    else if (sec >= 0) { int at = end; while (at > sec + 1 && Trim(lines[at - 1]).empty()) at--; lines.insert(lines.begin() + at, val); }
    else { lines.push_back(""); lines.push_back("[arrugas_cara]");
           lines.push_back("; (10/11) arrugas por jugador: id = intensidad, frente y entrecejo, ojos, surco y mejilla, menton y marioneta, cuello (multiplicadores), anios de piel (+ / -), arrugas de reposo; las escribe el boton del menu");
           lines.push_back(val); }
    { std::ofstream f(path, std::ios::binary | std::ios::trunc); if (!f) return "Arrugas: no se pudo escribir PES21FX_hair.ini";
      for (auto& ln : lines) f << ln << "\r\n"; }
    { std::lock_guard<std::mutex> lk(s_mx);
      if (erase) s_saved.erase(cur); else s_saved[cur] = v;
      s_live[cur] = v;
      auto it = s_cb.find(cur); if (it != s_cb.end()) { it->second.v = v; it->second.dirty = true; } }
    Log("arrugas por jugador: %s cara %s: %s", erase ? "borrada (todo en defecto)" : "guardada", cur.c_str(), val);
    char msg[200];
    if (erase) snprintf(msg, sizeof(msg), "Arrugas de la cara %s: todo en defecto, vuelve a las de todos", cur.c_str());
    else snprintf(msg, sizeof(msg), "Arrugas guardadas para la cara %s", cur.c_str());
    return msg;
}

}   // namespace fx
