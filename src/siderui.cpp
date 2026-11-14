// PES21 FX (14/11) - pagina "Sider" del menu Insert: ver siderui.h.
#include "siderui.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fx {

const char* TRS(const char* es);
namespace fs = std::filesystem;

// ------------------------------------------------------------------------------------------------------------------------------
// datos
struct SdIniLine {                // una linea de un .ini de modulo
    std::string raw;              // tal cual
    std::string sec, key, val, tail;   // tail = comentario al final de la linea (con su ';')
    bool kv = false;              // es "clave = valor" (activa)
    bool commentedKv = false;     // es "; clave = valor" (comentada: el modulo usa lo de la base)
};
struct SdIni {
    fs::path path; std::vector<SdIniLine> lines; bool ok = false; bool dirty = false; double savedT = -1.0; std::string err;
};
struct SdModule {
    std::string name;             // como esta en sider.ini (p. ej. "PES21FX_redes.lua")
    int line = -1;                // linea de sider.ini
    bool active = false, exists = false, ours = false;
    int pending = -1;             // -1 nada, 0 apagar al cerrar el juego, 1 prender
    std::string desc, version;
    std::vector<std::string> events, keys, tags;
    bool overlay = false, memory = false;
    fs::path ini;                 // vacio = no tiene
    std::size_t bytes = 0;
};
static fs::path s_dir;
static std::vector<std::string> s_siderLines;   // sider.ini tal cual
static std::vector<SdModule> s_mods;
static std::vector<std::pair<std::string, std::string>> s_settings;   // otras claves de sider.ini que vale la pena ver
static bool s_loaded = false; static std::string s_loadErr;
static std::map<std::string, SdIni> s_inis;   // por modulo
static std::string s_open;                    // modulo con los ajustes abiertos
static char s_filter[64] = ""; static int s_show = 0;   // 0 todos, 1 activos, 2 apagados, 3 nuestros, 4 con ajustes

static std::string Trim(const std::string& s) { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
static std::string Lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }
static bool ReadLines(const fs::path& p, std::vector<std::string>& out, std::size_t* bytes = nullptr) {
    std::ifstream f(p, std::ios::binary); if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); std::string all = ss.str(); if (bytes) *bytes = all.size();
    out.clear(); std::string cur;
    for (char c : all) { if (c == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back(); out.push_back(cur); cur.clear(); } else cur.push_back(c); }
    if (!cur.empty()) { if (cur.back() == '\r') cur.pop_back(); out.push_back(cur); }
    return true;
}
static bool WriteLines(const fs::path& p, const std::vector<std::string>& lines) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc); if (!f) return false;
    for (auto& l : lines) f << l << "\r\n";
    return (bool)f;
}

// teclas de Windows -> nombre corto
static std::string VkName(int v) {
    if (v >= 0x30 && v <= 0x39) return std::string(1, (char)v);
    if (v >= 0x41 && v <= 0x5A) return std::string(1, (char)v);
    if (v >= 0x60 && v <= 0x69) return "Num " + std::to_string(v - 0x60);
    if (v >= 0x70 && v <= 0x7B) return "F" + std::to_string(v - 0x6F);
    switch (v) {
    case 0x08: return "Retroceso"; case 0x09: return "Tab"; case 0x0D: return "Enter"; case 0x10: return "Shift"; case 0x11: return "Ctrl";
    case 0x12: return "Alt"; case 0x1B: return "Esc"; case 0x20: return "Espacio"; case 0x21: return "Re Pag"; case 0x22: return "Av Pag";
    case 0x23: return "Fin"; case 0x24: return "Inicio"; case 0x25: return "\xE2\x86\x90"; case 0x26: return "\xE2\x86\x91"; case 0x27: return "\xE2\x86\x92";
    case 0x28: return "\xE2\x86\x93"; case 0x2D: return "Insert"; case 0x2E: return "Supr"; case 0x6A: return "Num *"; case 0x6B: return "Num +";
    case 0x6D: return "Num -"; case 0x6E: return "Num ."; case 0x6F: return "Num /"; case 0xBA: return ";"; case 0xBB: return "+"; case 0xBC: return ",";
    case 0xBD: return "-"; case 0xBE: return "."; case 0xBF: return "/"; case 0xC0: return "\xC3\x91"; case 0xDB: return "["; case 0xDC: return "\\";
    case 0xDD: return "]"; case 0xDE: return "'";
    }
    char b[16]; snprintf(b, sizeof b, "0x%02X", v); return b;
}

// lee el modulo: descripcion (comentarios del principio), version, eventos, teclas, .ini
static void Analyze(SdModule& m) {
    const fs::path mp = s_dir / "modules" / m.name;
    std::vector<std::string> L; m.exists = ReadLines(mp, L, &m.bytes);
    const std::string base = m.name.size() > 4 && Lower(m.name.substr(m.name.size() - 4)) == ".lua" ? m.name.substr(0, m.name.size() - 4) : m.name;
    m.ours = base.rfind("PES21FX", 0) == 0 || base == "MotorTiempoReal" || base == "ProbadorPiezas" || base == "ProbadorAnimaciones";
    if (!m.exists) return;
    // descripcion
    { std::vector<std::string> d; bool block = false;
      for (size_t i = 0; i < L.size() && d.size() < 4; i++) {
          std::string t = Trim(L[i]);
          if (block) { size_t e = t.find("]]"); std::string x = Trim(e == std::string::npos ? t : t.substr(0, e)); if (!x.empty()) d.push_back(x); if (e != std::string::npos) block = false; continue; }
          if (t.rfind("--[[", 0) == 0 || t.rfind("--[=[", 0) == 0) { block = true; std::string x = Trim(t.substr(t.find('[', 3) + 1)); if (!x.empty() && x[0] == '[') x = Trim(x.substr(1)); if (!x.empty() && x != "]]") d.push_back(x); continue; }
          if (t.rfind("--", 0) == 0) { size_t a = t.find_first_not_of("-= "); std::string x = a == std::string::npos ? "" : Trim(t.substr(a)); if (!x.empty()) d.push_back(x); continue; }
          if (t.empty()) { if (!d.empty()) break; continue; }
          break;
      }
      std::string all; for (auto& x : d) { if (!all.empty()) all += ' '; all += x; }
      if (all.size() > 260) { all = all.substr(0, 257); size_t sp = all.find_last_of(' '); if (sp > 200) all = all.substr(0, sp); all += "..."; }
      m.desc = all; }
    static const std::regex reVer("version\\s*=\\s*\"([^\"]+)\""), reReg("register\\s*\\(\\s*\"([a-z_]+)\""), reKey("vkey\\s*==\\s*(0x[0-9a-fA-F]+|[0-9]+)"),
                            reIni("\"([^\"\\\\/]+\\.ini)\"");
    std::set<std::string> ev; std::set<int> keys; std::set<std::string> inis;
    for (auto& l : L) {
        std::smatch mm;
        if (m.version.empty() && std::regex_search(l, mm, reVer)) m.version = mm[1];
        for (auto it = std::sregex_iterator(l.begin(), l.end(), reReg); it != std::sregex_iterator(); ++it) ev.insert((*it)[1]);
        for (auto it = std::sregex_iterator(l.begin(), l.end(), reKey); it != std::sregex_iterator(); ++it) keys.insert((int)strtol((*it)[1].str().c_str(), nullptr, 0));
        for (auto it = std::sregex_iterator(l.begin(), l.end(), reIni); it != std::sregex_iterator(); ++it) inis.insert((*it)[1]);
        if (!m.memory && (l.find("memory.write") != std::string::npos || l.find("memory.pack") != std::string::npos)) m.memory = true;
    }
    m.events.assign(ev.begin(), ev.end());
    for (int k2 : keys) if (k2 > 0 && k2 < 256) m.keys.push_back(VkName(k2));
    m.overlay = ev.count("overlay_on") > 0;
    // .ini: el del mismo nombre, o uno que el codigo nombra y existe al lado
    { fs::path a = s_dir / "modules" / (base + ".ini"); std::error_code ec;
      if (fs::exists(a, ec)) m.ini = a;
      else for (auto& n : inis) { fs::path b = s_dir / "modules" / n; if (fs::exists(b, ec)) { m.ini = b; break; } } }
    // etiquetas
    auto has = [&](const char* e) { return ev.count(e) > 0; };
    if (m.overlay) m.tags.push_back("Overlay");
    if (!m.keys.empty() || has("key_down") || has("key_up")) m.tags.push_back("Teclas");
    if (has("livecpk_make_key") || has("livecpk_get_filepath") || has("livecpk_rewrite") || has("livecpk_data_ready")) m.tags.push_back("Archivos");
    if (has("set_kits")) m.tags.push_back("Camisetas");
    if (has("set_stadium") || has("set_conditions") || has("after_set_conditions") || has("set_stadium_choice")) m.tags.push_back("Estadio y clima");
    if (has("set_teams") || has("set_home_team_for_kits") || has("set_away_team_for_kits")) m.tags.push_back("Equipos");
    if (has("set_match_time") || has("set_tournament_id") || has("get_ball_name")) m.tags.push_back("Partido");
    if (has("trophy_rewrite") || has("set_tournament_id")) m.tags.push_back("Torneo");
    if (m.memory) m.tags.push_back("Memoria del juego");
    if (!m.ini.empty()) m.tags.push_back("Ajustes .ini");
}

static void Load() {
    s_loaded = true; s_loadErr.clear(); s_mods.clear(); s_settings.clear(); s_siderLines.clear(); s_inis.clear();
    if (s_dir.empty()) { s_loadErr = "no se encontro la carpeta de Sider (la que tiene sider.ini al lado del juego)"; return; }
    if (!ReadLines(s_dir / "sider.ini", s_siderLines)) { s_loadErr = "no se pudo leer sider.ini"; return; }
    static const std::regex reMod("^\\s*([;#]*)\\s*lua\\.module\\s*=\\s*\"([^\"]+)\""), reKv("^\\s*([A-Za-z0-9_.\\-]+)\\s*=\\s*(.*)$");
    static const char* kShow[] = { "overlay.vkey.toggle", "overlay.enabled", "overlay.on-from-start", "livecpk.enabled", "lookup-cache.enabled", "lua.enabled",
                                   "luajit.ext.enabled", "match.minutes", "free.side.select", "camera.sliders.max", "camera.dynamic-wide.angle.enabled",
                                   "close.on.exit", "start.minimized", "jit.enabled", "overlay.font-size", "overlay.location", "overlay.background-color" };
    int roots = 0;
    for (size_t i = 0; i < s_siderLines.size(); i++) {
        const std::string& l = s_siderLines[i]; std::smatch mm;
        if (std::regex_search(l, mm, reMod)) {
            SdModule m; m.name = mm[2]; m.line = (int)i; m.active = mm[1].str().empty();
            // la misma linea dos veces (una prendida y otra comentada): vale la prendida
            auto it = std::find_if(s_mods.begin(), s_mods.end(), [&](const SdModule& o) { return Lower(o.name) == Lower(m.name); });
            if (it != s_mods.end()) { if (m.active && !it->active) { it->active = true; it->line = m.line; } continue; }
            s_mods.push_back(m); continue;
        }
        std::string t = Trim(l); if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[') continue;
        if (std::regex_search(t, mm, reKv)) {
            const std::string k = Lower(mm[1]);
            if (k == "cpk.root") { roots++; continue; }
            for (const char* s : kShow) if (k == s) { std::string v = Trim(mm[2]); size_t c = v.find(';'); if (c != std::string::npos && v[0] != '"') v = Trim(v.substr(0, c)); s_settings.push_back({ mm[1], v }); }
        }
    }
    if (roots) s_settings.push_back({ "cpk.root", std::to_string(roots) + " carpetas" });
    for (auto& m : s_mods) Analyze(m);
    std::stable_sort(s_mods.begin(), s_mods.end(), [](const SdModule& a, const SdModule& b) {
        if (a.ours != b.ours) return a.ours; if (a.active != b.active) return a.active; return Lower(a.name) < Lower(b.name); });
}

void SdSetDir(const fs::path& d) { if (d != s_dir) { s_dir = d; s_loaded = false; } }
void SdOpen(const std::string& module) { s_open = module; }
int SdModuleCount() { int n = 0; for (auto& m : s_mods) n += m.active; return n; }
int SdPendingCount() { int n = 0; for (auto& m : s_mods) n += m.pending >= 0; return n; }

bool SdApplyPending(std::string* log) {
    if (s_dir.empty() || SdPendingCount() == 0) return false;
    std::vector<std::string> L; if (!ReadLines(s_dir / "sider.ini", L)) { if (log) *log = "no se pudo leer sider.ini"; return false; }
    // copia antes de tocar
    char stamp[32]; { time_t t = time(nullptr); tm lt{};
#ifdef _WIN32
      localtime_s(&lt, &t);
#else
      localtime_r(&t, &lt);
#endif
      strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", &lt); }
    const fs::path bak = s_dir / (std::string("sider.ini.antes_menu_") + stamp);
    if (!WriteLines(bak, L)) { if (log) *log = "no se pudo escribir la copia " + bak.u8string(); return false; }
    static const std::regex reMod("^(\\s*)([;#]*)\\s*lua\\.module\\s*=\\s*\"([^\"]+)\"(.*)$");
    std::string what;
    for (auto& m : s_mods) {
        if (m.pending < 0) continue;
        bool done = false;
        for (auto& l : L) {
            std::smatch mm; if (!std::regex_match(l, mm, reMod) || Lower(mm[3]) != Lower(m.name)) continue;
            const bool on = mm[2].str().empty();
            if (m.pending == 1 && !on && !done) { l = mm[1].str() + "lua.module = \"" + mm[3].str() + "\"" + mm[4].str(); done = true; }
            else if (m.pending == 0 && on) { l = mm[1].str() + "; lua.module = \"" + mm[3].str() + "\"" + mm[4].str(); done = true; }
            else if (m.pending == 1 && on) done = true;   // ya estaba prendida en otra linea
        }
        what += (m.pending ? " +" : " -") + m.name;
        m.active = m.pending == 1; m.pending = -1;
    }
    if (!WriteLines(s_dir / "sider.ini", L)) { if (log) *log = "no se pudo escribir sider.ini"; return false; }
    if (log) *log = "sider.ini actualizado:" + what + " (copia " + bak.filename().u8string() + ")";
    return true;
}

// ------------------------------------------------------------------------------------------------------------------------------
// .ini de un modulo
static void IniLoad(SdIni& I) {
    std::vector<std::string> L; I.lines.clear(); I.ok = ReadLines(I.path, L); I.dirty = false;
    if (!I.ok) { I.err = "no se pudo leer"; return; }
    static const std::regex reKv("^\\s*([A-Za-z0-9_.\\-]+)\\s*=\\s*([^;]*?)\\s*(;.*)?$"), reCkv("^\\s*;\\s*([A-Za-z0-9_.\\-]+)\\s*=\\s*([^;\\s][^;]*?)\\s*(;.*)?$");
    std::string sec;
    for (auto& l : L) {
        SdIniLine x; x.raw = l; std::string t = Trim(l); std::smatch mm;
        if (!t.empty() && t[0] == '[') { size_t e = t.find(']'); sec = Trim(t.substr(1, e == std::string::npos ? std::string::npos : e - 1)); }
        else if (!t.empty() && t[0] != ';' && t[0] != '#' && std::regex_match(l, mm, reKv)) { x.kv = true; x.key = mm[1]; x.val = mm[2]; x.tail = mm[3]; }
        else if (std::regex_match(l, mm, reCkv)) { x.commentedKv = true; x.key = mm[1]; x.val = mm[2]; x.tail = mm[3]; }
        x.sec = sec; I.lines.push_back(x);
    }
}
static std::string FmtNum(double v) {
    char b[32]; if (fabs(v - std::round(v)) < 1e-6 && fabs(v) < 1e7) snprintf(b, sizeof b, "%.0f", v); else { snprintf(b, sizeof b, "%.4f", v); std::string s = b; while (s.back() == '0') s.pop_back(); if (s.back() == '.') s.pop_back(); return s; }
    return b;
}
static void Rebuild(SdIniLine& x) {
    std::string r = x.key + " = " + x.val;
    if (!x.tail.empty()) { while (r.size() < 26) r += ' '; r += "  " + x.tail; }
    x.raw = x.commentedKv ? "; " + r : r;
}
static bool IniSave(SdIni& I, double now) {
    std::vector<std::string> L; for (auto& x : I.lines) L.push_back(x.raw);
    const bool ok = WriteLines(I.path, L); I.dirty = !ok; I.savedT = ok ? now : -1.0; I.err = ok ? "" : "no se pudo guardar";
    return ok;
}
static SdIniLine* IniFind(SdIni& I, const std::string& sec, const std::string& key, bool commentedToo) {
    for (auto& x : I.lines) if (Lower(x.sec) == Lower(sec) && Lower(x.key) == Lower(key) && (x.kv || (commentedToo && x.commentedKv))) return &x;
    return nullptr;
}
static bool IsNum(const std::string& v, double* out) { if (v.empty()) return false; char* e = nullptr; const double d = strtod(v.c_str(), &e); if (e == v.c_str() || Trim(e).size()) return false; if (out) *out = d; return true; }

// ------------------------------------------------------------------------------------------------------------------------------
// dibujo
static ImU32 Col(float r, float g, float b, float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a)); }
static ImU32 ColA(ImVec4 c, float a) { c.w *= a; return ImGui::ColorConvertFloat4ToU32(c); }
static ImVec4 Mix(ImVec4 a, ImVec4 b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }
static float Ease(ImGuiID id, float target, float speed, bool anim) {
    float* v = ImGui::GetStateStorage()->GetFloatRef(id, target);
    *v = anim ? *v + (target - *v) * std::min(1.0f, ImGui::GetIO().DeltaTime * speed) : target; return *v;
}
static ImVec4 HashCol(const std::string& s) {
    unsigned h = 2166136261u; for (char c : s) { h ^= (unsigned char)c; h *= 16777619u; }
    float r, g, b; ImGui::ColorConvertHSVtoRGB((h % 360) / 360.0f, 0.45f, 0.85f, r, g, b); return ImVec4(r, g, b, 1);
}
// pastilla de texto (etiqueta / tecla)
static float Pill(ImDrawList* dl, ImVec2 p, const char* t, ImVec4 c, float k, bool key = false) {
    const float fs = ImGui::GetFontSize() * 0.84f; const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, t);
    const ImVec2 a = p, b(p.x + ts.x + (key ? 12.0f : 14.0f) * k, p.y + ts.y + 6.0f * k);
    if (key) { dl->AddRectFilled(ImVec2(a.x, a.y + 2.0f * k), ImVec2(b.x, b.y + 2.0f * k), Col(0, 0, 0, 0.45f), 4.0f * k);
               dl->AddRectFilled(a, b, Col(0.92f, 0.93f, 0.96f, 0.14f), 4.0f * k); dl->AddRect(a, b, Col(1, 1, 1, 0.28f), 4.0f * k); }
    else { dl->AddRectFilled(a, b, ColA(c, 0.16f), (b.y - a.y) * 0.5f); dl->AddRect(a, b, ColA(c, 0.35f), (b.y - a.y) * 0.5f); }
    dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + (key ? 6.0f : 7.0f) * k, a.y + 3.0f * k), key ? Col(0.97f, 0.98f, 1, 1) : ColA(Mix(c, ImVec4(1, 1, 1, 1), 0.45f), 1), t);
    return b.x - a.x;
}
// boton redondeado propio
static bool Btn(const char* id, const char* label, bool on, float k, ImVec4 acc, float w = 0.0f) {
    const ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true); const ImVec2 sz(w > 0 ? w : ts.x + 22.0f * k, ts.y + 10.0f * k);
    const ImVec2 p = ImGui::GetCursorScreenPos(); const bool r = ImGui::InvisibleButton(id, sz);
    const bool hv = ImGui::IsItemHovered(), ac = ImGui::IsItemActive(); ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), on ? ColA(acc, ac ? 0.65f : hv ? 0.50f : 0.38f) : Col(1, 1, 1, ac ? 0.20f : hv ? 0.13f : 0.07f), sz.y * 0.5f);
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), on ? ColA(acc, 0.9f) : Col(1, 1, 1, hv ? 0.30f : 0.14f), sz.y * 0.5f);
    dl->AddText(ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + 5.0f * k), Col(0.96f, 0.97f, 0.99f, 1), label, ImGui::FindRenderedTextEnd(label));
    return r;
}
// interruptor
static bool Toggle(const char* id, bool* v, float k, ImVec4 acc, bool anim) {
    const float w = 36.0f * k, h = 18.0f * k; const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool r = ImGui::InvisibleButton(id, ImVec2(w, h)); if (r) *v = !*v;
    const float o = Ease(ImGui::GetItemID(), *v ? 1.0f : 0.0f, 12.0f, anim); ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ColA(Mix(ImVec4(1, 1, 1, ImGui::IsItemHovered() ? 0.16f : 0.10f), ImVec4(acc.x, acc.y, acc.z, 0.85f), o), 1), h * 0.5f);
    dl->AddCircleFilled(ImVec2(p.x + h * 0.5f + (w - h) * o, p.y + h * 0.5f), h * 0.38f, Col(1, 1, 1, 0.95f), 16);
    return r;
}
// tira de valor con el numero adentro: arrastrar, rueda fina, doble clic = escribir
static bool ValStrip(const char* id, double* v, double lo, double hi, bool integer, float w, float k, ImVec4 acc, bool dim) {
    ImGui::PushID(id); bool ch = false; const float h = 20.0f * k; const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGuiID eid = ImGui::GetID("##edit"); bool* editing = ImGui::GetStateStorage()->GetBoolRef(eid, false);
    if (*editing) {
        static char buf[32]; static ImGuiID bufFor = 0;
        if (bufFor != eid) { snprintf(buf, sizeof buf, "%s", FmtNum(*v).c_str()); bufFor = eid; ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(w);
        if (ImGui::InputText("##t", buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsScientific)) { double d; if (IsNum(buf, &d)) { *v = integer ? std::round(d) : d; ch = true; } *editing = false; bufFor = 0; }
        else if (ImGui::IsItemDeactivated()) { *editing = false; bufFor = 0; }
        ImGui::PopID(); return ch;
    }
    ImGui::InvisibleButton("##s", ImVec2(w, h));
    const bool hv = ImGui::IsItemHovered(), ac = ImGui::IsItemActive();
    if (ac && ImGui::GetIO().MouseDelta.x != 0.0f) { const double t = std::clamp((double)(ImGui::GetIO().MousePos.x - p.x) / w, 0.0, 1.0); double nv = lo + (hi - lo) * t; if (integer) nv = std::round(nv); if (nv != *v) { *v = nv; ch = true; } }
    if (hv && ImGui::GetIO().MouseWheel != 0.0f) { ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); double nv = *v + ImGui::GetIO().MouseWheel * (integer ? 1.0 : (hi - lo) * 0.01); if (integer) nv = std::round(nv); *v = nv; ch = true; }
    if (hv && ImGui::IsMouseDoubleClicked(0)) *editing = true;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float t = (float)std::clamp((*v - lo) / (hi - lo), 0.0, 1.0);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(1, 1, 1, hv ? 0.10f : 0.06f), h * 0.5f);
    if (t > 0.0f) dl->AddRectFilled(p, ImVec2(p.x + std::max(h, w * t), p.y + h), ColA(acc, dim ? 0.18f : ac ? 0.55f : 0.40f), h * 0.5f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), ColA(dim ? ImVec4(1, 1, 1, 0.14f) : acc, dim ? 1.0f : 0.55f), h * 0.5f);
    const std::string s = FmtNum(*v); const ImVec2 ts = ImGui::CalcTextSize(s.c_str());
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), dim ? Col(0.70f, 0.73f, 0.78f, 1) : Col(1, 1, 1, 1), s.c_str());
    if (hv) ImGui::SetTooltip("%s", TRS("Arrastrar, rueda para fino, doble clic para escribir"));
    ImGui::PopID(); return ch;
}
// rango razonable para una tira a partir del valor
static void GuessRange(double v, bool integer, double* lo, double* hi) {
    const double a = fabs(v);
    if (integer) { *lo = std::min(0.0, v); *hi = std::max({ 4.0, a * 2.0, v + 4.0 }); return; }
    if (a <= 1.0) { *lo = v < 0 ? -1.0 : 0.0; *hi = std::max(1.0, a * 2.0); return; }
    *lo = v < 0 ? -a * 2.0 : 0.0; *hi = a * 2.0;
}

// --- editor generico de un .ini ---
static void IniEditor(SdIni& I, float k, bool anim, double now, ImVec4 acc) {
    ImDrawList* dl = ImGui::GetWindowDrawList(); (void)dl;
    const float avail = ImGui::GetContentRegionAvail().x, labelW = std::min(220.0f * k, avail * 0.42f), ctlW = avail - labelW - 8.0f * k;
    std::string sec = "\x01"; int n = 0; bool any = false;
    for (size_t i = 0; i < I.lines.size(); i++) {
        SdIniLine& x = I.lines[i]; if (!x.kv) continue; any = true;
        if (x.sec != sec) { sec = x.sec; if (!sec.empty()) { ImGui::Dummy(ImVec2(0, 2.0f * k)); ImGui::TextColored(ImVec4(acc.x, acc.y, acc.z, 0.95f), "[%s]", sec.c_str()); } }
        ImGui::PushID((int)i);
        const float y0 = ImGui::GetCursorPosY();
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(x.key.c_str());
        if (!x.tail.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Trim(x.tail.substr(1)).c_str());
        ImGui::SameLine(labelW); ImGui::SetCursorPosY(y0);
        double d; std::string vv = Trim(x.val); bool quoted = vv.size() >= 2 && vv.front() == '"' && vv.back() == '"';
        if (!quoted && IsNum(vv, &d)) {
            const bool integer = vv.find('.') == std::string::npos && vv.find('e') == std::string::npos;
            if (integer && (d == 0.0 || d == 1.0) && Lower(x.key).find("count") == std::string::npos) {
                bool b = d != 0.0; if (Toggle("##b", &b, k, acc, anim)) { x.val = b ? "1" : "0"; Rebuild(x); I.dirty = true; }
            } else {
                static std::map<ImGuiID, std::pair<double, double>> s_rng; const ImGuiID rid = ImGui::GetID("##rng");   // el rango se fija la primera vez (no cambia al mover)
                if (!s_rng.count(rid)) { double a, b; GuessRange(d, integer, &a, &b); s_rng[rid] = { a, b }; }
                if (ValStrip("##v", &d, s_rng[rid].first, s_rng[rid].second, integer, ctlW, k, acc, false)) { x.val = integer ? FmtNum(std::round(d)) : FmtNum(d); Rebuild(x); I.dirty = true; }
            }
        } else {
            char buf[256]; snprintf(buf, sizeof buf, "%s", x.val.c_str()); ImGui::SetNextItemWidth(ctlW);
            if (ImGui::InputText("##s", buf, sizeof buf)) { x.val = buf; Rebuild(x); I.dirty = true; }
        }
        ImGui::PopID(); n++;
    }
    if (!any) ImGui::TextDisabled("%s", TRS("El .ini no tiene valores para editar (solo comentarios)."));
    (void)n; (void)now;
}

// --- tarjeta propia: PES21FX_redes ---
static void RedesEditor(SdIni& I, float k, bool anim, double now, ImVec4 acc) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x;
    // presets: los del modulo + [preset nombre] del ini
    std::vector<std::pair<std::string, std::string>> P = { { "juego", "la red del juego" }, { "reactiva", "reactive_nets" }, { "viva", "ondula y vuelve" },
                                                            { "tv", "mas pesada" }, { "personal", "tus valores" } };
    for (auto& x : I.lines) { std::string t = Trim(x.raw); if (t.rfind("[preset ", 0) == 0) { std::string n = Trim(t.substr(8, t.find(']') - 8)); if (!n.empty()) P.push_back({ n, "del .ini" }); } }
    SdIniLine* pl = IniFind(I, "", "preset", false); const std::string cur = pl ? Lower(Trim(pl->val)) : "reactiva";
    ImGui::TextDisabled("%s", TRS("Preset de la red"));
    { const int n = (int)P.size(), cols = std::min(n, avail > 620.0f * k ? 7 : 4); const float g = 6.0f * k, bw = (avail - (cols - 1) * g) / cols, h = 52.0f * k;
      for (int i = 0; i < n; i++) {
          if (i % cols) ImGui::SameLine(0, g);
          ImGui::PushID(i); const ImVec2 p = ImGui::GetCursorScreenPos(); const bool on = cur == Lower(P[i].first);
          if (ImGui::InvisibleButton("##pr", ImVec2(bw, h)) && pl) { pl->val = P[i].first; Rebuild(*pl); I.dirty = true; }
          const bool hv = ImGui::IsItemHovered(); const float o = Ease(ImGui::GetItemID(), on ? 1.0f : 0.0f, 10.0f, anim);
          dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + h), ColA(Mix(ImVec4(1, 1, 1, hv ? 0.09f : 0.05f), ImVec4(acc.x, acc.y, acc.z, 0.30f), o), 1), 9.0f * k);
          dl->AddRect(p, ImVec2(p.x + bw, p.y + h), ColA(Mix(ImVec4(1, 1, 1, hv ? 0.35f : 0.14f), acc, o), 1), 9.0f * k, 0, (1.0f + o) * k);
          // red dibujada: cuadricula que cae segun el preset (mas floja = mas curva), se mueve si esta elegida
          { const float x0 = p.x + 10.0f * k, x1 = p.x + bw - 10.0f * k, y0 = p.y + 7.0f * k, hh = h * 0.42f;
            const float sag = (Lower(P[i].first) == "juego" ? 0.15f : Lower(P[i].first) == "tv" ? 0.35f : Lower(P[i].first) == "tensa" ? 0.2f : Lower(P[i].first) == "suelta" ? 0.95f : 0.7f);
            const float wob = anim && on ? 0.12f * sinf((float)now * 3.0f) : 0.0f; const ImU32 nc = ColA(Mix(ImVec4(0.75f, 0.78f, 0.84f, 1), ImVec4(1, 1, 1, 1), o), 0.75f);
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y0), Col(1, 1, 1, 0.85f), 2.0f * k);
            for (int r = 1; r <= 3; r++) { const float yy = y0 + hh * r / 3.0f; ImVec2 pts[9];
                for (int j = 0; j < 9; j++) { const float u = j / 8.0f; pts[j] = ImVec2(x0 + (x1 - x0) * u, yy + hh * (sag + wob) * 0.35f * sinf(u * 3.14159f) * (r / 3.0f)); }
                dl->AddPolyline(pts, 9, nc, 0, 1.0f); }
            for (int j = 0; j <= 6; j++) { const float u = j / 6.0f, x = x0 + (x1 - x0) * u; dl->AddLine(ImVec2(x, y0), ImVec2(x, y0 + hh + hh * (sag + wob) * 0.35f * sinf(u * 3.14159f)), nc, 1.0f); } }
          const std::string& nm = P[i].first; const ImVec2 ts = ImGui::CalcTextSize(nm.c_str());
          dl->AddText(ImVec2(p.x + (bw - ts.x) * 0.5f, p.y + h * 0.62f), on ? Col(1, 1, 1, 1) : Col(0.84f, 0.87f, 0.91f, 1), nm.c_str());
          if (hv) ImGui::SetTooltip("%s", TRS(P[i].second.c_str()));
          ImGui::PopID();
      } }
    ImGui::Dummy(ImVec2(0, 4.0f * k));
    // red mojada
    if (SdIniLine* lw = IniFind(I, "", "lluvia", false)) {
        bool b = Trim(lw->val) != "0"; ImGui::AlignTextToFramePadding();
        if (Toggle("##lluvia", &b, k, acc, anim)) { lw->val = b ? "1" : "0"; Rebuild(*lw); I.dirty = true; }
        ImGui::SameLine(); ImGui::TextUnformatted(TRS("Red mojada con lluvia (pesa 35 % mas)"));
    }
    // [personal]
    ImGui::Dummy(ImVec2(0, 4.0f * k));
    ImGui::TextDisabled("%s", TRS("Tus valores (preset personal): apagados = los de la base"));
    struct PV { const char* key; double lo, hi, def; bool integer; const char* help; };
    static const PV kV[] = {
        { "conservar", 0.90, 1.0, 0.99, false, "velocidad que se conserva por paso: mas = la onda dura mas (juego 0.95)" },
        { "peso", 0.0, 8.0, 0.25, false, "caida en m/s2: mas = cuelga y vuelve con cuerpo (juego 6)" },
        { "golpe", 0.0, 1.0, 0.3, false, "lo que conserva el punto que arrastro la pelota: menos = el golpe se apaga (juego 1)" },
        { "blandura", 0.5, 8.0, 4.0, false, "hilos mas elasticos: la onda recorre la red (juego 1)" },
        { "holgura", 0.95, 1.2, 1.05, false, "red mas suelta; rige desde el partido siguiente (juego 1)" },
        { "pasadas", 1, 6, 2, true, "vueltas de hilos por paso: mas = mas tensa (un poco mas de CPU)" },
        { "estiramiento", 1.0, 1.6, 1.2, false, "cuanto se deja hundir por la pelota" } };
    { SdIniLine* bl = IniFind(I, "personal", "base", false);
      if (bl) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(TRS("Parte de")); ImGui::SameLine();
          static const char* kB[] = { "juego", "reactiva", "viva", "tv" };
          for (int i = 0; i < 4; i++) { ImGui::SameLine(0, 4.0f * k); ImGui::PushID(i + 300);
              if (Btn("##b", kB[i], Lower(Trim(bl->val)) == kB[i], k, acc)) { bl->val = kB[i]; Rebuild(*bl); I.dirty = true; }
              ImGui::PopID(); } } }
    const float labelW = std::min(150.0f * k, avail * 0.3f), stripW = avail - labelW - 44.0f * k;
    for (const PV& v : kV) {
        ImGui::PushID(v.key);
        SdIniLine* x = IniFind(I, "personal", v.key, true);
        const bool on = x && x->kv; double d = v.def; if (x) IsNum(Trim(x->val), &d);
        bool b = on; const float y0 = ImGui::GetCursorPosY();
        if (Toggle("##on", &b, k, acc, anim)) {
            if (b) { if (x) { x->commentedKv = false; x->kv = true; Rebuild(*x); }
                     else {   // no estaba ni comentada: se agrega al final de [personal]
                         size_t at = I.lines.size(); for (size_t i = 0; i < I.lines.size(); i++) if (Lower(I.lines[i].sec) == "personal") at = i + 1;
                         SdIniLine nl; nl.sec = "personal"; nl.key = v.key; nl.val = FmtNum(v.def); nl.kv = true; Rebuild(nl); I.lines.insert(I.lines.begin() + (long)at, nl); }
            } else if (x) { x->kv = false; x->commentedKv = true; Rebuild(*x); }
            I.dirty = true;
        }
        ImGui::SameLine(0, 8.0f * k); ImGui::SetCursorPosY(y0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(v.key);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS(v.help));
        ImGui::SameLine(labelW + 44.0f * k); ImGui::SetCursorPosY(y0);
        if (ValStrip("##v", &d, v.lo, v.hi, v.integer, stripW, k, acc, !on) && (x = IniFind(I, "personal", v.key, true)) != nullptr) {
            x->val = v.integer ? FmtNum(std::round(d)) : FmtNum(d); x->kv = true; x->commentedKv = false; Rebuild(*x); I.dirty = true;
        }
        ImGui::PopID();
    }
}

// --- una tarjeta de modulo ---
static void ModuleCard(SdModule& m, float w, float k, bool anim, double now, ImVec4 acc) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushID(m.name.c_str());
    const ImVec2 p0 = ImGui::GetCursorScreenPos(); const float pad = 12.0f * k;
    dl->ChannelsSplit(2); dl->ChannelsSetCurrent(1);
    ImGui::SetCursorScreenPos(ImVec2(p0.x + pad, p0.y + pad));
    ImGui::BeginGroup(); ImGui::PushTextWrapPos(p0.x + w - pad - ImGui::GetWindowPos().x + ImGui::GetScrollX());
    const float iw = w - 2 * pad;
    // cabecera: insignia con la inicial, nombre, version, estado
    { const ImVec2 c(ImGui::GetCursorScreenPos().x + 17.0f * k, ImGui::GetCursorScreenPos().y + 17.0f * k); const ImVec4 hc = m.ours ? acc : HashCol(m.name);
      dl->AddCircleFilled(c, 17.0f * k, ColA(hc, m.active ? 0.30f : 0.12f), 28); dl->AddCircle(c, 17.0f * k, ColA(hc, m.active ? 0.9f : 0.35f), 28, 1.4f * k);
      std::string ini = m.name.substr(0, std::min<size_t>(2, m.name.size())); if (m.ours) ini = "FX";
      const ImVec2 ts = ImGui::CalcTextSize(ini.c_str()); dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), Col(1, 1, 1, m.active ? 1.0f : 0.6f), ini.c_str());
      ImGui::Dummy(ImVec2(40.0f * k, 34.0f * k)); ImGui::SameLine(0, 4.0f * k);
      ImGui::BeginGroup();
      std::string nm = m.name; if (nm.size() > 4 && Lower(nm.substr(nm.size() - 4)) == ".lua") nm.resize(nm.size() - 4);
      ImGui::TextUnformatted(nm.c_str());
      ImGui::TextDisabled("%s%s%s", m.version.empty() ? "" : "v", m.version.c_str(), m.exists ? "" : TRS("  (no esta en modules)"));
      ImGui::EndGroup();
      // estado a la derecha
      const char* st = m.pending == 1 ? TRS("se prende al cerrar") : m.pending == 0 ? TRS("se apaga al cerrar") : m.active ? TRS("Activo") : TRS("Apagado");
      const ImVec4 sc = m.pending >= 0 ? ImVec4(1.0f, 0.72f, 0.30f, 1) : m.active ? acc : ImVec4(0.6f, 0.62f, 0.66f, 1);
      const float fs = ImGui::GetFontSize() * 0.84f; const ImVec2 ss = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, st);
      const ImVec2 a(p0.x + w - pad - ss.x - 22.0f * k, p0.y + pad + 2.0f * k), b(p0.x + w - pad, a.y + ss.y + 8.0f * k);
      dl->AddRectFilled(a, b, ColA(sc, 0.18f), (b.y - a.y) * 0.5f); dl->AddCircleFilled(ImVec2(a.x + 9.0f * k, (a.y + b.y) * 0.5f), 3.3f * k, ColA(sc, 1), 10);
      dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + 16.0f * k, a.y + 4.0f * k), ColA(Mix(sc, ImVec4(1, 1, 1, 1), 0.4f), 1), st); }
    // descripcion
    if (!m.desc.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.74f, 0.77f, 0.82f, 1)); ImGui::TextWrapped("%s", m.desc.c_str()); ImGui::PopStyleColor(); }
    else if (m.exists) ImGui::TextDisabled("%s", TRS("Sin descripcion en el archivo."));
    // etiquetas y teclas
    { float x = ImGui::GetCursorScreenPos().x; const float x0 = x, y = ImGui::GetCursorScreenPos().y; float yy = y; const float rowH = ImGui::GetFontSize() + 10.0f * k;
      for (auto& t : m.tags) { const char* tt = TRS(t.c_str()); const float fw = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * 0.84f, FLT_MAX, 0, tt).x + 14.0f * k;
          if (x + fw > x0 + iw) { x = x0; yy += rowH; } x += Pill(dl, ImVec2(x, yy), tt, HashCol(t), k) + 5.0f * k; }
      if (!m.keys.empty()) { if (x > x0) x += 6.0f * k;
          for (auto& t : m.keys) { const float fw = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * 0.84f, FLT_MAX, 0, t.c_str()).x + 12.0f * k;
              if (x + fw > x0 + iw) { x = x0; yy += rowH; } x += Pill(dl, ImVec2(x, yy), t.c_str(), ImVec4(1, 1, 1, 1), k, true) + 4.0f * k; } }
      if (!m.tags.empty() || !m.keys.empty()) ImGui::Dummy(ImVec2(iw, yy - y + rowH)); }
    // botones
    { const bool hasIni = !m.ini.empty(); const bool open = s_open == m.name;
      if (hasIni) { if (Btn("##aj", open ? TRS("Cerrar ajustes") : TRS("Ajustes"), open, k, acc)) s_open = open ? "" : m.name; ImGui::SameLine(0, 6.0f * k); }
      const bool willOn = m.pending >= 0 ? m.pending == 1 : m.active;
      if (Btn("##onoff", willOn ? TRS("Apagar") : TRS("Prender"), false, k, acc)) {
          const int want = willOn ? 0 : 1; m.pending = (want == (m.active ? 1 : 0)) ? -1 : want; }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Cambia sider.ini al cerrar el juego (con copia). Sider ya cargo los modulos: el cambio se ve al abrirlo de nuevo."));
      if (m.overlay) { ImGui::SameLine(0, 10.0f * k); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%s", TRS("tiene panel en el overlay de Sider")); } }
    // ajustes abiertos
    if (s_open == m.name && !m.ini.empty()) {
        SdIni& I = s_inis[m.name];
        if (I.path != m.ini) { I = SdIni(); I.path = m.ini; IniLoad(I); }
        ImGui::Dummy(ImVec2(0, 4.0f * k));
        const ImVec2 sp = ImGui::GetCursorScreenPos(); dl->AddLine(sp, ImVec2(sp.x + iw, sp.y), Col(1, 1, 1, 0.10f), 1.0f); ImGui::Dummy(ImVec2(0, 4.0f * k));
        if (!I.ok) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s: %s", m.ini.filename().u8string().c_str(), TRS("no se pudo leer"));
        else {
            ImGui::BeginChild("##ini", ImVec2(iw, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoBackground);
            if (Lower(m.name) == "pes21fx_redes.lua") RedesEditor(I, k, anim, now, acc); else IniEditor(I, k, anim, now, acc);
            ImGui::EndChild();
            // guardado: solo, poco despues de soltar (asi una tira arrastrada no escribe 60 veces por segundo)
            static double s_dirtyT = -1.0;
            if (I.dirty) { if (s_dirtyT < 0) s_dirtyT = now; if (!ImGui::IsMouseDown(0) && now - s_dirtyT > 0.4) { IniSave(I, now); s_dirtyT = -1.0; } } else s_dirtyT = -1.0;
            if (I.savedT > 0 && now - I.savedT < 3.0) ImGui::TextColored(ImVec4(acc.x, acc.y, acc.z, 1), "%s", TRS(Lower(m.name) == "pes21fx_redes.lua" ? "Guardado. Se aplica con la tecla 9 (overlay de Sider) o en el partido siguiente." : "Guardado en el .ini. El modulo lo lee con su tecla de recarga o en el partido siguiente."));
            else if (!I.err.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", TRS(I.err.c_str()));
            else ImGui::TextDisabled("%s %s", TRS("Archivo:"), m.ini.filename().u8string().c_str());
        }
    }
    ImGui::PopTextWrapPos(); ImGui::EndGroup();
    const float hgt = ImGui::GetItemRectMax().y - p0.y + pad;
    dl->ChannelsSetCurrent(0);
    const bool hv = ImGui::IsMouseHoveringRect(p0, ImVec2(p0.x + w, p0.y + hgt));
    const float ha = Ease(ImGui::GetID("##hov"), hv ? 1.0f : 0.0f, 10.0f, anim);
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + hgt), Col(1, 1, 1, (m.active ? 0.050f : 0.028f) + 0.02f * ha), 12.0f * k);
    dl->AddRectFilledMultiColor(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p0.x + w - 1, p0.y + 40.0f * k), Col(1, 1, 1, 0.04f), Col(1, 1, 1, 0.04f), Col(1, 1, 1, 0), Col(1, 1, 1, 0));
    dl->AddRect(p0, ImVec2(p0.x + w, p0.y + hgt), m.pending >= 0 ? Col(1.0f, 0.72f, 0.30f, 0.7f) : ColA(Mix(ImVec4(1, 1, 1, 0.10f), ImVec4(acc.x, acc.y, acc.z, 0.6f), m.ours ? 0.6f : 0.25f * ha), 1), 12.0f * k, 0, 1.0f);
    if (m.ours) dl->AddRectFilled(ImVec2(p0.x, p0.y + 14.0f * k), ImVec2(p0.x + 3.0f * k, p0.y + hgt - 14.0f * k), ColA(acc, 0.9f), 2.0f * k);
    dl->ChannelsMerge();
    ImGui::SetCursorScreenPos(p0); ImGui::Dummy(ImVec2(w, hgt));
    ImGui::PopID();
}

void SdPage(float k, bool anim, double now) {
    if (!s_loaded) Load();
    const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x;
    if (!s_loadErr.empty()) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "%s", TRS(s_loadErr.c_str()));
        if (Btn("##reload0", TRS("Buscar de nuevo"), false, k, acc)) s_loaded = false;
        return;
    }
    // resumen: carpeta, conteos, ajustes de sider.ini
    { int on = 0, off = 0, ours = 0, ini = 0; for (auto& m : s_mods) { on += m.active; off += !m.active; ours += m.ours; ini += !m.ini.empty(); }
      const ImVec2 p = ImGui::GetCursorScreenPos(); const float h = 64.0f * k;
      dl->AddRectFilled(p, ImVec2(p.x + avail, p.y + h), Col(1, 1, 1, 0.04f), 12.0f * k); dl->AddRect(p, ImVec2(p.x + avail, p.y + h), Col(1, 1, 1, 0.10f), 12.0f * k);
      const int vals[4] = { on, off, ours, ini }; const char* lbl[4] = { "activos", "apagados", "nuestros", "con ajustes" };
      const float cw = std::min(120.0f * k, (avail - 24.0f * k) / 6.0f);
      for (int i = 0; i < 4; i++) { const float x = p.x + 14.0f * k + i * cw; char b[16]; snprintf(b, sizeof b, "%d", vals[i]);
          ImFont* f = ImGui::GetFont(); const float fs = ImGui::GetFontSize() * 1.6f;
          dl->AddText(f, fs, ImVec2(x, p.y + 8.0f * k), i == 0 ? ColA(acc, 1) : i == 2 ? ColA(acc, 0.8f) : Col(0.92f, 0.93f, 0.96f, 1), b);
          dl->AddText(ImVec2(x, p.y + 8.0f * k + fs + 2.0f * k), Col(0.62f, 0.66f, 0.72f, 1), TRS(lbl[i])); }
      const std::string dir = s_dir.filename().u8string(); const float x = p.x + 14.0f * k + 4 * cw;
      dl->PushClipRect(ImVec2(x, p.y), ImVec2(p.x + avail - 8.0f * k, p.y + h), true);
      dl->AddText(ImVec2(x, p.y + 12.0f * k), Col(0.92f, 0.93f, 0.96f, 1), dir.c_str());
      dl->AddText(ImVec2(x, p.y + 12.0f * k + ImGui::GetFontSize() + 4.0f * k), Col(0.62f, 0.66f, 0.72f, 1), TRS("sider.ini (solo lectura mientras el juego esta abierto)"));
      dl->PopClipRect();
      ImGui::Dummy(ImVec2(avail, h)); }
    // claves de sider.ini que vale la pena ver
    if (!s_settings.empty()) {
        ImGui::Dummy(ImVec2(0, 2.0f * k));
        float x = ImGui::GetCursorScreenPos().x; const float x0 = x, y = ImGui::GetCursorScreenPos().y; float yy = y; const float rowH = ImGui::GetFontSize() + 10.0f * k;
        for (auto& s : s_settings) { const std::string t = s.first + " = " + s.second; const float fw = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * 0.84f, FLT_MAX, 0, t.c_str()).x + 14.0f * k;
            if (x + fw > x0 + avail) { x = x0; yy += rowH; } x += Pill(dl, ImVec2(x, yy), t.c_str(), ImVec4(0.55f, 0.70f, 0.95f, 1), k) + 5.0f * k; }
        ImGui::Dummy(ImVec2(avail, yy - y + rowH));
    }
    // pendientes
    if (const int np = SdPendingCount()) {
        const ImVec2 p = ImGui::GetCursorScreenPos(); const float h = 34.0f * k;
        dl->AddRectFilled(p, ImVec2(p.x + avail, p.y + h), Col(1.0f, 0.72f, 0.30f, 0.14f), 10.0f * k); dl->AddRect(p, ImVec2(p.x + avail, p.y + h), Col(1.0f, 0.72f, 0.30f, 0.6f), 10.0f * k);
        char b[160]; snprintf(b, sizeof b, TRS("%d cambio(s) de modulos pendientes: se escriben en sider.ini al cerrar el juego (con copia)."), np);
        dl->AddText(ImVec2(p.x + 12.0f * k, p.y + (h - ImGui::GetFontSize()) * 0.5f), Col(1, 0.92f, 0.80f, 1), b);
        ImGui::SetCursorScreenPos(ImVec2(p.x + avail - 110.0f * k, p.y + 5.0f * k));
        if (Btn("##undo", TRS("Deshacer"), false, k, acc, 100.0f * k)) for (auto& m : s_mods) m.pending = -1;
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 4.0f * k)); ImGui::Dummy(ImVec2(0, 0));
    }
    // filtros
    { ImGui::Dummy(ImVec2(0, 2.0f * k));
      static const char* kF[5] = { "Todos", "Activos", "Apagados", "Nuestros", "Con ajustes" };
      for (int i = 0; i < 5; i++) { if (i) ImGui::SameLine(0, 5.0f * k); ImGui::PushID(i + 500); if (Btn("##f", TRS(kF[i]), s_show == i, k, acc)) s_show = i; ImGui::PopID(); }
      ImGui::SameLine(0, 10.0f * k); ImGui::SetNextItemWidth(std::max(120.0f * k, ImGui::GetContentRegionAvail().x - 90.0f * k));
      ImGui::InputTextWithHint("##sdf", TRS("Buscar modulo"), s_filter, sizeof s_filter);
      ImGui::SameLine(0, 6.0f * k); if (Btn("##rl", TRS("Releer"), false, k, acc)) { s_loaded = false; s_open.clear(); }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TRS("Vuelve a leer sider.ini y los modulos (los cambios pendientes se pierden)."));
      ImGui::Dummy(ImVec2(0, 4.0f * k)); }
    // tarjetas: 2 columnas si hay lugar (cada columna se apila sola), la que tiene los ajustes abiertos ocupa todo el ancho
    const std::string flt = Lower(s_filter);
    std::vector<SdModule*> list;
    for (auto& m : s_mods) {
        if (s_show == 1 && !m.active) continue; if (s_show == 2 && m.active) continue; if (s_show == 3 && !m.ours) continue; if (s_show == 4 && m.ini.empty()) continue;
        if (!flt.empty() && Lower(m.name).find(flt) == std::string::npos && Lower(m.desc).find(flt) == std::string::npos) continue;
        list.push_back(&m);
    }
    if (list.empty()) { ImGui::TextDisabled("%s", TRS("Ningun modulo con ese filtro.")); return; }
    const bool two = avail >= 700.0f * k; const float g = 10.0f * k, cw = two ? (avail - g) * 0.5f : avail;
    const ImVec2 o = ImGui::GetCursorScreenPos(); float colY[2] = { o.y, o.y };
    for (SdModule* m : list) {
        if (two && s_open == m->name) {   // ancho completo, debajo de las dos columnas
            const float y = std::max(colY[0], colY[1]); ImGui::SetCursorScreenPos(ImVec2(o.x, y));
            ModuleCard(*m, avail, k, anim, now, acc); colY[0] = colY[1] = ImGui::GetItemRectMax().y + g; continue;
        }
        const int c = two ? (colY[1] < colY[0] ? 1 : 0) : 0;
        ImGui::SetCursorScreenPos(ImVec2(o.x + c * (cw + g), colY[c]));
        ModuleCard(*m, cw, k, anim, now, acc);
        colY[c] = ImGui::GetItemRectMax().y + g;
        if (!two) colY[1] = colY[0];
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, std::max(colY[0], colY[1])));
    ImGui::Dummy(ImVec2(avail, 0));
}

} // namespace fx
