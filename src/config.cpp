// PES21 FX - configuracion: PES21FX.ini (mismo formato que el pes_mod.ini de 3DMigoto: [Constants] y [Key...])
// y PES21FX_rules.ini (reglas por shader: copiar/pasar recursos entre pasadas, parametros por dibujo, eventos).
#include "core.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>
#include <iterator>

namespace fx {

Config g_cfg;
std::mutex g_cfgMutex;

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
static std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

static bool ReadLines(const std::wstring& path, std::vector<std::string>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string l;
    while (std::getline(f, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); out.push_back(l); }
    return true;
}

// "x37" -> idx 37 comp 0 ; "w" -> idx 0 comp 3
static bool ParseParamName(const std::string& n, int& idx, int& comp) {
    if (n.empty()) return false;
    const char* comps = "xyzw";
    const char* p = strchr(comps, tolower((unsigned char)n[0]));
    if (!p) return false;
    comp = int(p - comps);
    std::string rest = n.substr(1);
    if (rest.empty()) { idx = 0; return true; }
    for (char c : rest) if (!isdigit((unsigned char)c)) return false;
    idx = atoi(rest.c_str());
    return idx >= 0 && idx < kParamCount;
}

static int VkFromName(std::string t) {
    std::string u = t; for (auto& c : u) c = (char)toupper((unsigned char)c);
    if (u.rfind("VK_", 0) == 0) u = u.substr(3);
    if (u.size() == 1 && (isalnum((unsigned char)u[0]))) return u[0];
    if (u[0] == 'F' && u.size() <= 3 && isdigit((unsigned char)u[1])) { int n = atoi(u.c_str() + 1); if (n >= 1 && n <= 24) return VK_F1 + n - 1; }
    if (u.rfind("NUMPAD", 0) == 0 && u.size() == 7) return VK_NUMPAD0 + (u[6] - '0');
    static const std::map<std::string, int> m = {
        {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE}, {"HOME", VK_HOME}, {"END", VK_END}, {"PRIOR", VK_PRIOR},
        {"NEXT", VK_NEXT}, {"SPACE", VK_SPACE}, {"TAB", VK_TAB}, {"RETURN", VK_RETURN}, {"ESCAPE", VK_ESCAPE},
        {"UP", VK_UP}, {"DOWN", VK_DOWN}, {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT}, {"OEM_PLUS", VK_OEM_PLUS},
        {"OEM_MINUS", VK_OEM_MINUS}, {"PAUSE", VK_PAUSE}, {"SCROLL", VK_SCROLL}};
    auto it = m.find(u);
    return it == m.end() ? 0 : it->second;
}

static void ParseKey(KeyBind& k, const std::string& v) {
    std::istringstream ss(v); std::string t;
    while (ss >> t) {
        std::string l = Lower(t);
        if (l == "ctrl") k.ctrl = 1; else if (l == "no_ctrl") k.ctrl = 0;
        else if (l == "alt") k.alt = 1; else if (l == "no_alt") k.alt = 0;
        else if (l == "shift") k.shift = 1; else if (l == "no_shift") k.shift = 0;
        else if (l == "no_modifiers") { k.ctrl = k.alt = k.shift = 0; }
        else k.vk = VkFromName(t);
    }
}

static std::vector<float> ParseFloats(const std::string& v) {
    std::vector<float> r; std::string s = v; std::replace(s.begin(), s.end(), ',', ' ');
    std::istringstream ss(s); float f; while (ss >> f) r.push_back(f);
    return r;
}

static std::wstring IniPath() { return g_cfg.baseDir + L"PES21FX.ini"; }

static bool LoadConfigLocked() {
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    Config& c = g_cfg;
    memset(c.base, 0, sizeof(c.base));
    for (auto& a : c.info) for (auto& b : a) b = ParamInfo();
    c.keys.clear(); c.rules.clear(); c.pairRules.clear(); c.vars.clear(); c.groups.clear();
    c.vars["pes_push"] = 0.003f;

    std::vector<std::string> lines;
    if (!ReadLines(IniPath(), lines)) { Log("no existe PES21FX.ini"); return false; }
    std::string section, comment;
    KeyBind* cur = nullptr;
    MenuGroup* grp = nullptr;
    for (auto& raw : lines) {
        std::string l = Trim(raw);
        if (l.empty()) { comment.clear(); continue; }
        if (l[0] == ';') { std::string t = Trim(l.substr(1)); if (cur && cur->label.empty()) cur->label = t; else comment = t; continue; }
        if (l[0] == '[') {
            section = l.substr(1, l.find(']') - 1);
            cur = nullptr; grp = nullptr;
            if (Lower(section).rfind("key", 0) == 0) { c.keys.emplace_back(); cur = &c.keys.back(); cur->name = section; }
            else if (Lower(section).rfind("menu", 0) == 0) { c.groups.emplace_back(); grp = &c.groups.back(); grp->title = section.substr(4); }
            continue;
        }
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Trim(l.substr(0, eq)), v = Trim(l.substr(eq + 1));
        std::string kl = Lower(k);
        if (Lower(section) == "constants") {
            if (kl.rfind("global ", 0) == 0) {   // global $nombre = valor
                std::string n = Trim(k.substr(7)); if (!n.empty() && n[0] == '$') n = n.substr(1);
                c.vars[n] = (float)atof(v.c_str()); continue;
            }
            int idx, comp;
            if (ParseParamName(k, idx, comp)) {
                c.base[idx][comp] = (float)atof(v.c_str());
                c.info[idx][comp].defined = true;
                c.info[idx][comp].comment = comment;
            }
            continue;
        }
        if (Lower(section) == "motor") {   // (20/10) parches en memoria del motor (exepatch.cpp)
            if (kl == "motor") c.motorPatch = atoi(v.c_str()) != 0;
            else if (kl == "sombra_resolucion") c.motorShadowRes = std::clamp(atoi(v.c_str()), 0, 8192);
            else if (kl == "sombra_cascadas") c.motorCascadeScale = std::clamp((float)atof(v.c_str()), 0.0f, 4.0f);
            continue;
        }
        if (Lower(section) == "pes21fx") {
            if (kl == "menu_key") c.menuKey = VkFromName(v);
            else if (kl == "tutorials") c.tutorials = atoi(v.c_str()) != 0;
            else if (kl == "favoritos") c.favorites = v;
            else if (kl == "ui_scale") c.uiScale = std::clamp((float)atof(v.c_str()), 0.7f, 2.0f);
            else if (kl == "compacto") c.compact = atoi(v.c_str()) != 0;
            else if (kl == "auto_captura") c.autoCap = atoi(v.c_str()) != 0;
            else if (kl == "auto_captura_seg") c.autoCapSec = std::clamp((float)atof(v.c_str()), 3.0f, 600.0f);
            else if (kl == "auto_captura_max") c.autoCapMax = std::clamp(atoi(v.c_str()), 1, 2000);
            else if (kl == "auto_captura_ab") c.autoCapAB = atoi(v.c_str()) != 0;
            else if (kl == "exe_dump") c.exeDump = atoi(v.c_str()) != 0;
            else if (kl == "aniso") c.aniso = std::clamp(atoi(v.c_str()), 1, 16);
            else if (kl == "mip_bias") c.mipBias = std::clamp((float)atof(v.c_str()), -2.0f, 2.0f);
            else if (kl == "replace_shaders") c.optReplace = atoi(v.c_str()) != 0;
            else if (kl == "rules") c.optRules = atoi(v.c_str()) != 0;
            else if (kl == "overlay") c.optOverlay = atoi(v.c_str()) != 0;
            else if (kl == "pin_dlls") c.optPin = atoi(v.c_str()) != 0;
            else if (kl == "hooks") c.optHooks = atoi(v.c_str()) != 0;
            else if (kl == "dxgi_hooks") c.optDxgi = atoi(v.c_str()) != 0;
            else if (kl == "present_hook") c.optPresent = atoi(v.c_str()) != 0;
            else if (kl == "context_hooks") c.optCtx = atoi(v.c_str()) != 0;
            else if (kl == "export_hooks") c.optExports = atoi(v.c_str()) != 0;
            else if (kl == "orphan_fix") c.optOrphan = atoi(v.c_str()) != 0;
            else if (kl == "dump_switcher") c.optDump = atoi(v.c_str()) != 0;
            continue;
        }
        if (grp) {
            if (kl == "title") grp->title = v;
            else if (kl == "desc") grp->desc = v;
            else if (kl == "section") grp->section = v;
            else if (kl == "toggle") { int i2, c2; if (ParseParamName(v, i2, c2)) { grp->tIdx = i2; grp->tComp = c2; } }
            else if (kl == "button") {
                std::vector<std::string> parts; std::string cur2;
                for (char ch : v) { if (ch == '|') { parts.push_back(Trim(cur2)); cur2.clear(); } else cur2 += ch; }
                parts.push_back(Trim(cur2));
                if (parts.size() >= 2) { MenuButton b; b.action = Lower(parts[0]); b.label = parts[1]; if (parts.size() >= 3) b.help = parts[2]; grp->buttons.push_back(b); }
            }
            else if (kl == "cslider" || kl == "cchoice") {
                // cslider = seccion.clave | etiqueta | min | max | fabrica | ayuda | formato
                // cchoice = seccion.clave | etiqueta | A;B | fabrica | ayuda
                std::vector<std::string> parts; std::string cur2;
                for (char ch : v) { if (ch == '|') { parts.push_back(Trim(cur2)); cur2.clear(); } else cur2 += ch; }
                parts.push_back(Trim(cur2));
                if (parts.size() >= 2) {
                    MenuSlider sl{0, 0, parts[1], 0.f, 1.f};
                    sl.ext = Lower(parts[0]);
                    if (kl == "cchoice") {
                        if (parts.size() >= 3) { std::string o; for (char ch : parts[2]) { if (ch == ';') { sl.options.push_back(Trim(o)); o.clear(); } else o += ch; } if (!Trim(o).empty()) sl.options.push_back(Trim(o)); }
                        if (parts.size() >= 4) sl.def = (float)atof(parts[3].c_str());
                        if (parts.size() >= 5) sl.help = parts[4];
                        sl.hi = (float)sl.options.size() - 1;
                    } else {
                        if (parts.size() >= 4) { sl.lo = (float)atof(parts[2].c_str()); sl.hi = (float)atof(parts[3].c_str()); }
                        if (parts.size() >= 5) sl.def = (float)atof(parts[4].c_str());
                        if (parts.size() >= 6) sl.help = parts[5];
                        if (parts.size() >= 7 && !parts[6].empty()) sl.fmt = parts[6];
                    }
                    grp->sliders.push_back(sl);
                }
            }
            else if (kl == "slider" || kl == "choice") {
                // slider = x46 | Fuerza | min | max | ayuda | formato      choice = y20 | Modo | A;B;C | ayuda
                std::vector<std::string> parts; std::string cur2;
                for (char ch : v) { if (ch == '|') { parts.push_back(Trim(cur2)); cur2.clear(); } else cur2 += ch; }
                parts.push_back(Trim(cur2));
                int i2, c2;
                if (parts.size() >= 2 && ParseParamName(parts[0], i2, c2)) {
                    MenuSlider sl{i2, c2, parts[1], 0.f, 1.f};
                    if (kl == "choice") {
                        if (parts.size() >= 3) { std::string o; for (char ch : parts[2]) { if (ch == ';') { sl.options.push_back(Trim(o)); o.clear(); } else o += ch; } if (!Trim(o).empty()) sl.options.push_back(Trim(o)); }
                        if (parts.size() >= 4) sl.help = parts[3];
                        sl.lo = 0; sl.hi = (float)sl.options.size() - 1;
                        // "Etiqueta=valor": el selector asigna ese valor en vez del indice
                        { bool anyEq = false; for (auto& o : sl.options) anyEq |= o.find('=') != std::string::npos;
                          if (anyEq) for (auto& o : sl.options) { size_t e = o.rfind('='); if (e != std::string::npos) { sl.optVals.push_back((float)atof(o.substr(e + 1).c_str())); o = Trim(o.substr(0, e)); } else sl.optVals.push_back((float)sl.optVals.size()); } }
                    } else {
                        if (parts.size() >= 4) { sl.lo = (float)atof(parts[2].c_str()); sl.hi = (float)atof(parts[3].c_str()); }
                        if (parts.size() >= 5) sl.help = parts[4];
                        if (parts.size() >= 6 && !parts[5].empty()) sl.fmt = parts[5];
                    }
                    grp->sliders.push_back(sl);
                }
            }
            continue;
        }
        if (cur) {
            if (kl == "key") ParseKey(*cur, v);
            else if (kl == "type") {
                std::string t = Lower(v);
                cur->type = t == "toggle" ? KeyBind::Toggle : t == "cycle" ? KeyBind::Cycle : t == "hold" ? KeyBind::Hold : KeyBind::Activate;
            } else if (kl == "run") cur->run = v;
            else {
                int idx, comp;
                if (ParseParamName(k, idx, comp)) { Assign a{idx, comp, ParseFloats(v), ""}; if (!a.values.empty()) cur->assigns.push_back(a); }
            }
        }
    }
    memcpy(c.live, c.base, sizeof(c.base));
    LoadCloth();
    for (auto& kb : c.keys) if (kb.label.empty()) kb.label = kb.name;

    // ---- reglas ----
    lines.clear();
    if (ReadLines(c.baseDir + L"PES21FX_rules.ini", lines)) {
        Rule* r = nullptr;
        for (auto& raw : lines) {
            std::string l = Trim(raw);
            if (l.empty() || l[0] == ';') continue;
            if (l[0] == '[') {
                r = nullptr;
                std::string s = Trim(l.substr(1, l.find(']') - 1));
                if (Lower(s).rfind("rule ", 0) == 0) {
                    std::string rest = Trim(s.substr(5));
                    size_t vp = Lower(rest).find(" vs ");
                    uint64_t h = _strtoui64(Trim(rest.substr(0, vp)).c_str(), nullptr, 16);
                    if (vp != std::string::npos) {   // (20/10) [rule <ps> vs <vs>]
                        uint64_t vh = _strtoui64(Trim(rest.substr(vp + 4)).c_str(), nullptr, 16);
                        r = &c.pairRules[{h, vh}]; r->hash = h; r->vsCond = vh;
                    } else { r = &c.rules[h]; r->hash = h; }
                }
                continue;
            }
            if (!r) continue;
            size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string k = Lower(Trim(l.substr(0, eq))), v = Trim(l.substr(eq + 1));
            std::istringstream ss(v); RuleAction a{}; std::string arg2;
            if (k == "copy_cb") { a.kind = RuleAction::CopyCB; ss >> a.slot >> a.name; }
            else if (k == "ref_srv") { a.kind = RuleAction::RefSRV; ss >> a.slot >> a.name; }
            else if (k == "cb") { a.kind = RuleAction::BindCB; ss >> a.slot >> a.name; }
            else if (k == "srv") { a.kind = RuleAction::BindSRV; ss >> a.slot >> a.name; }
            else if (k == "param") {
                if (r->vsCond) continue;   // las reglas con VS no tienen juego de parametros propio
                std::string pn; ss >> pn >> arg2;
                a.kind = RuleAction::SetParam;
                if (!ParseParamName(pn, a.idx, a.comp)) continue;
                if (!arg2.empty() && arg2[0] == '$') a.var = arg2.substr(1); else a.value = (float)atof(arg2.c_str());
                r->hasParamOverride = true;
            }
            else if (k == "event" && Lower(v) == "turf") a.kind = RuleAction::EventTurf;
            else if (k == "facegen") { a.kind = RuleAction::FaceGen; ss >> a.slot >> a.idx; if (a.idx <= 0) a.idx = a.slot + 1; }
            else continue;
            r->actions.push_back(a);
        }
        int set = 0;
        for (auto& kv : c.rules) if (kv.second.hasParamOverride) kv.second.paramSet = set++;
    }
    int nr = 0; for (auto& kv : c.rules) nr += (int)kv.second.actions.size();
    for (auto& kv : c.pairRules) nr += (int)kv.second.actions.size();
    Log("config: %d teclas, %d reglas (%d acciones); shaders=%d reglas=%d menu=%d", (int)c.keys.size(), (int)c.rules.size(), nr,
        (int)c.optReplace, (int)c.optRules, (int)c.optOverlay);
    return true;
}

// ---------------------------------------------------------------- cloth-mod\cloth.ini
static std::wstring ClothPath() { return g_cfg.baseDir + L"..\\cloth-mod\\cloth.ini"; }

// lee "clave = valor ; comentario" por seccion -> clothVals["seccion.clave"]
bool LoadCloth() {
    // se llama con g_cfgMutex tomado
    Config& c = g_cfg;
    c.clothPath = ClothPath();
    c.clothVals.clear(); c.clothLoaded.clear(); c.clothDirty = false;
    std::vector<std::string> lines;
    c.clothFound = ReadLines(c.clothPath, lines);
    if (!c.clothFound) { c.clothStatus = "No se encontro cloth-mod\\cloth.ini (mod de ropa no instalado)"; return false; }
    std::string sec;
    for (auto& raw : lines) {
        std::string l = raw; size_t sc = l.find(';'); if (sc != std::string::npos) l = l.substr(0, sc);
        l = Trim(l);
        if (l.empty()) continue;
        if (l[0] == '[') { sec = Lower(Trim(l.substr(1, l.find(']') - 1))); continue; }
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Lower(Trim(l.substr(0, eq))), v = Trim(l.substr(eq + 1));
        if (v.empty()) continue;
        char* end = nullptr; double d = strtod(v.c_str(), &end);
        if (end == v.c_str()) continue;            // valores no numericos (teclas) no se tocan
        c.clothVals[sec + "." + k] = (float)d;
    }
    c.clothLoaded = c.clothVals;
    c.clothStatus = "cloth.ini cargado: los cambios se aplican solos en ~1 segundo";
    return true;
}

// escribe solo las claves que cambiaron, conservando comentarios y el resto del archivo
bool SaveCloth() {
    Config& c = g_cfg;
    if (!c.clothFound) return false;
    std::ifstream f(c.clothPath, std::ios::binary);
    if (!f) return false;
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    bool crlf = all.find("\r\n") != std::string::npos;
    std::vector<std::string> lines; { std::string cur; for (char ch : all) { if (ch == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back(); lines.push_back(cur); cur.clear(); } else cur += ch; } if (!cur.empty()) lines.push_back(cur); }
    int changed = 0;
    for (auto& kv : c.clothVals) {
        auto it = c.clothLoaded.find(kv.first);
        if (it != c.clothLoaded.end() && it->second == kv.second) continue;
        std::string sec = kv.first.substr(0, kv.first.find('.')), key = kv.first.substr(kv.first.find('.') + 1);
        char val[64]; snprintf(val, sizeof(val), "%g", kv.second);
        std::string s; int secLine = -1, lastKey = -1, hit = -1;
        for (int i = 0; i < (int)lines.size(); i++) {
            std::string l = Trim(lines[i]);
            if (!l.empty() && l[0] == '[') { s = Lower(Trim(l.substr(1, l.find(']') - 1))); if (s == sec) secLine = i; continue; }
            if (s != sec) continue;
            std::string body = l; size_t sc = body.find(';'); if (sc != std::string::npos) body = body.substr(0, sc);
            size_t eq = body.find('=');
            if (eq == std::string::npos) continue;
            lastKey = i;
            if (Lower(Trim(body.substr(0, eq))) == key) { hit = i; break; }
        }
        if (hit >= 0) {
            std::string& L = lines[hit];
            size_t sc = L.find(';');
            std::string comment = sc != std::string::npos ? L.substr(sc) : "";
            std::string nl = key + " = " + val;
            if (!comment.empty()) { while (nl.size() < 24) nl += ' '; nl += comment; }
            L = nl;
        } else if (secLine >= 0) {
            lines.insert(lines.begin() + (lastKey >= 0 ? lastKey : secLine) + 1, key + " = " + val + "    ; (PES21 FX)");
        } else {
            lines.push_back(""); lines.push_back("[" + sec + "]"); lines.push_back(key + " = " + val + "    ; (PES21 FX)");
        }
        changed++;
    }
    if (!changed) { c.clothDirty = false; return true; }
    std::ofstream o(c.clothPath, std::ios::binary | std::ios::trunc);
    if (!o) { c.clothStatus = "No se pudo escribir cloth.ini"; return false; }
    for (auto& l : lines) o << l << (crlf ? "\r\n" : "\n");
    o.close();
    c.clothLoaded = c.clothVals;
    c.clothDirty = false;
    // el pelo usa la misma direccion de viento que la ropa
    auto wx = c.clothVals.find("sim.wind_x"), wz = c.clothVals.find("sim.wind_z");
    if (wx != c.clothVals.end() || wz != c.clothVals.end()) {
        float x = wx != c.clothVals.end() ? wx->second : 2.0f, z = wz != c.clothVals.end() ? wz->second : 0.0f;
        float m = (std::max)(fabsf(x), fabsf(z)); if (m > 1e-4f) { c.live[23][0] = x / m; c.live[23][1] = z / m; }
    }
    c.clothStatus = "cloth.ini guardado (" + std::to_string(changed) + " valores): el mod de ropa lo aplica en ~1 s";
    Log("cloth.ini: %d valores cambiados desde el menu", changed);
    return true;
}

bool SaveConfigValues() {
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    std::vector<std::string> lines;
    if (!ReadLines(IniPath(), lines)) return false;
    std::string section;
    for (auto& raw : lines) {
        std::string l = Trim(raw);
        if (l.empty() || l[0] == ';') continue;
        if (l[0] == '[') { section = Lower(l.substr(1, l.find(']') - 1)); continue; }
        if (section != "constants") continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Trim(l.substr(0, eq));
        int idx, comp;
        if (!ParseParamName(k, idx, comp)) continue;
        // (20/10) una clave que no estaba cuando se leyo el ini (se agrego con el juego abierto, sin "Recargar config")
        // conserva el valor del archivo: antes se guardaba el valor vivo (0) y se perdian opciones nuevas
        if (!g_cfg.info[idx][comp].defined) continue;
        char buf[64]; snprintf(buf, sizeof(buf), "%g", g_cfg.live[idx][comp]);
        raw = k + " = " + buf;
        g_cfg.base[idx][comp] = g_cfg.live[idx][comp];
    }
    std::ofstream f(IniPath(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    for (auto& l : lines) f << l << "\r\n";
    for (auto& kb : g_cfg.keys) kb.active = false, kb.cycleIdx = -1;
    Log("valores guardados en PES21FX.ini");
    return true;
}

bool SaveOption(const std::string& key, const std::string& value) {
    std::vector<std::string> lines;
    if (!ReadLines(IniPath(), lines)) return false;
    std::string section; bool done = false; size_t secEnd = std::string::npos;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string l = Trim(lines[i]);
        if (!l.empty() && l[0] == '[') {
            if (section == "pes21fx" && secEnd == std::string::npos) secEnd = i;
            section = Lower(l.substr(1, l.find(']') - 1)); continue;
        }
        if (section != "pes21fx" || l.empty() || l[0] == ';') continue;
        size_t eq = l.find('='); if (eq == std::string::npos) continue;
        if (Lower(Trim(l.substr(0, eq))) == Lower(key)) { lines[i] = key + " = " + value; done = true; }
    }
    if (!done) {
        if (secEnd == std::string::npos) { lines.insert(lines.begin(), {"[PES21FX]", key + " = " + value, ""}); }
        else lines.insert(lines.begin() + secEnd, key + " = " + value);
    }
    std::ofstream f(IniPath(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    for (auto& l : lines) f << l << "\r\n";
    return true;
}

// ---------------------------------------------------------------- teclas
static void ApplyAssign(KeyBind& k, bool on, int cycle) {
    if (on && k.saved.empty()) for (auto& a : k.assigns) k.saved.push_back(g_cfg.live[a.idx][a.comp]);
    for (size_t i = 0; i < k.assigns.size(); i++) {
        auto& a = k.assigns[i];
        if (!on) { if (i < k.saved.size()) g_cfg.live[a.idx][a.comp] = k.saved[i]; continue; }
        size_t vi = cycle >= 0 ? (size_t)cycle % a.values.size() : 0;
        g_cfg.live[a.idx][a.comp] = a.values[vi];
    }
    if (!on) k.saved.clear();
}

static double s_matchStart = -1e9, s_lastTurf = -1e9;

// desde el menu (con g_cfgMutex ya tomado): 0 = alternar, 1 = siguiente valor del ciclo
void KeyActionUnlocked(KeyBind& k, int action) {
    if (action == 1 || k.type == KeyBind::Cycle) { k.cycleIdx++; ApplyAssign(k, true, k.cycleIdx); return; }
    k.active = !k.active; ApplyAssign(k, k.active, -1);
}

void PollKeys(bool focused) {
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    for (auto& k : g_cfg.keys) {
        bool down = focused && k.vk && (GetAsyncKeyState(k.vk) & 0x8000) &&
                    (k.ctrl < 0 || k.ctrl == (int)ctrl) && (k.alt < 0 || k.alt == (int)alt) && (k.shift < 0 || k.shift == (int)shift);
        bool press = down && !k.wasDown, release = !down && k.wasDown;
        k.wasDown = down;
        if (press) {
            if (!k.run.empty()) {
                if (Lower(k.run).find("resetsweat") != std::string::npos) s_matchStart = Now();
            }
            switch (k.type) {
            case KeyBind::Toggle: k.active = !k.active; ApplyAssign(k, k.active, -1); break;
            case KeyBind::Cycle:  k.cycleIdx++; ApplyAssign(k, true, k.cycleIdx); break;
            case KeyBind::Hold:
            case KeyBind::Activate: ApplyAssign(k, true, -1); break;
            }
        } else if (release && k.type == KeyBind::Hold) ApplyAssign(k, false, -1);
    }
}

std::atomic<int> g_matchGen{0};
float g_eyePos[4] = {}; bool g_eyeValid = false;
void OnTurfDrawn() {
    double t = Now();
    if (t - s_lastTurf > 20.0) { s_matchStart = t; FaceGenClear(); g_matchGen++; }   // cancha sin dibujarse 20 s y vuelve = partido nuevo
    s_lastTurf = t;
}

void UpdateTimeParams() {
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    double t = Now();
    if (s_matchStart < -1e8) s_matchStart = t;
    auto& p = g_cfg.live;
    p[10][0] = float((t - s_matchStart) / 60.0);   // minutos reales de partido (sudor)
    p[23][3] = p[23][2];                            // viento del pelo: tiempo anterior
    p[23][2] = float(fmod(t, 10000.0));             // tiempo actual
    p[24][1] = (t - s_lastTurf) < 2.0 ? 1.0f : 0.0f; // en partido
    p[93][3] = (t - s_lastTurf) < 15.0 ? 1.0f : 0.0f; // 19/10: en partido (tolerante): primeros planos sin cancha a la vista siguen contando
    // 19/10: pupila reactiva: brillo de la escena por la luz del sol/focos (m_lightColor0 de cPSLight), adaptado en el tiempo
    // como una pupila real (se contrae rapido, ~0.4 s; se dilata lento, ~3 s). x93 brillo adaptado 0 (noche) .. 1 (sol pleno)
    { static double lastT = t; static float adapt = 0.7f; std::vector<float> L;
      float dt = (float)std::clamp(t - lastT, 0.0, 0.5); lastT = t;
      if (NamedCBRead("LightCB", L) && L.size() >= 8) {
          float lum = 0.2126f * L[4] + 0.7152f * L[5] + 0.0722f * L[6];
          if (lum > 1.0f) {
              float b = std::clamp((log10f(lum) - 2.3f) / 1.6f, 0.0f, 1.0f);   // ~200 (focos) -> 0, ~8000 (sol) -> 1
              float tau = b > adapt ? 0.4f : 3.0f;
              adapt += (b - adapt) * std::min(1.0f, dt / tau);
          }
      }
      p[93][0] = adapt; }
    // (20/10) inyeccion de luz: la DLL publica cada cuadro, para todos los shaders, el sol en espacio MUNDO (111: xyz + altura
    // en grados), su color normalizado (112: rgb con el canal mayor = 1, w = luminancia cruda), la niebla/horizonte del motor y
    // la exposicion (113), y el estado de la escena (114: x suelo automatico 0 normal / 1 humedo (lluvia) / 2 seco, y altura
    // de la camara, z estacion, w 1 = de dia con sol real). Antes cada shader lo derivaba a mano de m_view.
    { std::vector<float> L, S;
      bool lok = NamedCBRead("LightCB", L) && L.size() >= 8, sok = NamedCBRead("SceneCB", S) && S.size() >= 120;
      if (lok && sok) {
          float lx = L[0], ly = L[1], lz = L[2]; float n = sqrtf(lx * lx + ly * ly + lz * lz);
          if (n > 1e-6f) { lx /= n; ly /= n; lz /= n;
              const float* M = &S[32];   // m_view (filas = ejes de la camara en el mundo)
              float wx = lx * M[0] + ly * M[4] + lz * M[8], wy = lx * M[1] + ly * M[5] + lz * M[9], wz = lx * M[2] + ly * M[6] + lz * M[10];
              float wn = sqrtf(wx * wx + wy * wy + wz * wz); if (wn > 1e-6f) { wx /= wn; wy /= wn; wz /= wn; }
              if (wy < 0) { wx = -wx; wy = -wy; wz = -wz; }   // siempre "hacia el sol"
              p[111][0] = wx; p[111][1] = wy; p[111][2] = wz; p[111][3] = asinf(std::clamp(wy, -1.f, 1.f)) * 57.2958f; }
          float cr = L[4], cg = L[5], cb = L[6]; float mx = std::max(cr, std::max(cg, cb));
          if (mx > 1e-4f) { p[112][0] = cr / mx; p[112][1] = cg / mx; p[112][2] = cb / mx; }
          p[112][3] = 0.2126f * cr + 0.7152f * cg + 0.0722f * cb;
          p[113][0] = S[108]; p[113][1] = S[109]; p[113][2] = S[110]; p[113][3] = S[94];   // niebla rgb, exposicion z
          p[114][1] = S[81];                                                             // altura de la camara (m_eyepos.y)
          g_eyePos[0] = S[80]; g_eyePos[1] = S[81]; g_eyePos[2] = S[82]; g_eyePos[3] = 1; g_eyeValid = true;   // (20/10) para el LOD de caras
          bool sunny = (cr / std::max(cb, 1e-4f)) > 1.06f && p[112][3] > 600.0f;         // luz calida y fuerte = sol (de noche: torres frias)
          p[114][3] = sunny ? 1.0f : 0.0f;
      }
      int ground = 0; if (g_cfg.match.known && g_cfg.match.weather == 1) ground = 1;
      p[114][0] = (float)ground; p[114][2] = g_cfg.match.known ? (float)g_cfg.match.season : -1.0f; }
    UpdateClothWet(t);
    UpdateClothFit(t);
}

// ---------------------------------------------------------------- tela mojada -> shader del mod de ropa
// El mod de ropa recompila cloth_sim.hlsl en caliente; PES21 FX le escribe el valor de "mojado" (sudor por reloj de
// partido y lluvia) en la linea marcada PES21FX_LIVE_WET, como mucho cada 20 s y solo si cambio bastante.
// w70 on/off, x70 cuanto moja el sudor al maximo, y70 cuanto moja la lluvia
void UpdateClothWet(double t) {
    static double next = 0; static float last = -1.0f;
    if (t < next) return; next = t + 20.0;
    const auto& p = g_cfg.live;
    if (p[70][3] < 0.5 || !g_cfg.clothFound) return;
    float tmin = p[10][0], t0 = p[9][0], t1 = p[9][1];
    float sweat = (p[8][3] > 0.5 && t1 > t0 && p[24][1] > 0.5) ? std::clamp((tmin - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;
    float wet = sweat * std::clamp(p[70][0], 0.0f, 1.0f);
    if (g_cfg.match.known && g_cfg.match.weather == 1) wet = std::max(wet, std::clamp(p[70][1], 0.0f, 1.0f));
    if (last >= 0 && fabsf(wet - last) < 0.08) return;
    std::wstring path = g_cfg.baseDir + L"..\\cloth-mod\\shaders\\cloth_sim.hlsl";
    std::ifstream f(path, std::ios::binary); if (!f) return;
    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); f.close();
    size_t m = src.find("// PES21FX_LIVE_WET"); if (m == std::string::npos) return;
    size_t ls = src.rfind('\n', m); ls = (ls == std::string::npos) ? 0 : ls + 1;
    char line[96]; snprintf(line, sizeof(line), "#define PES_WET            %.2f   ", wet);
    std::string before = src;
    src.replace(ls, m - ls, line);
    last = wet;
    if (src == before) return;   // 18/10: el archivo ya dice lo mismo: no se reescribe (cada escritura hace recompilar al mod de ropa)
    if (t < 45.0) { next = 45.0; last = -1.0f; return; }   // nunca en el arranque del juego (el mod esta creando sus recursos)
    std::ofstream o(path, std::ios::binary | std::ios::trunc); if (!o) return;
    o << src; o.close();
    Log("ropa: tela mojada = %.2f (sudor %.2f, lluvia %s)", wet, sweat, (g_cfg.match.known && g_cfg.match.weather == 1) ? "si" : "no");
}

// ---------------------------------------------------------------- ajuste al fisico -> shader del mod de ropa
// x78 on/off, y78 holgura en delgados (mm), z78 pegado en anchos (mm), w78 rango de escala de hueso
void UpdateClothFit(double t) {
    static double next = 0; static float last[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    if (t < next) return; next = t + 5.0;
    if (!g_cfg.clothFound) return;
    const auto& p = g_cfg.live;
    float v[10] = { p[78][0] > 0.5f ? 1.0f : 0.0f, std::clamp(p[78][1], 0.0f, 40.0f), std::clamp(p[78][2], 0.0f, 40.0f), std::clamp(p[78][3] > 0 ? p[78][3] : 0.15f, 0.02f, 1.0f), p[79][0] > 0.5f ? 0.0f : 1.0f, std::clamp(p[79][1] > 0 ? p[79][1] : 8.5f, 4.0f, 15.0f), std::clamp(p[79][2], 0.0f, 3.0f), p[69][3] > 0.5f ? 1.0f : 0.0f, std::clamp(p[80][1], 0.0f, 1.0f), std::clamp(p[80][2] > 0 ? p[80][2] : 3.5f, 0.5f, 10.0f) };   // ...; y80 costuras fuerza; z80 ancho (cm)
    bool same = true; for (int i = 0; i < 10; i++) if (fabsf(v[i] - last[i]) > 0.01f) same = false;
    if (same) return;
    std::wstring path = g_cfg.baseDir + L"..\\cloth-mod\\shaders\\cloth_sim.hlsl";
    std::ifstream f(path, std::ios::binary); if (!f) return;
    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); f.close();
    std::string before = src;
    const char* marks[10] = { "// PES21FX_LIVE_FIT_ON", "// PES21FX_LIVE_FIT_THIN", "// PES21FX_LIVE_FIT_FAT", "// PES21FX_LIVE_FIT_RANGE", "// PES21FX_LIVE_CLOTH_FX", "// PES21FX_LIVE_THIGH_REF", "// PES21FX_LIVE_LIMB_GAIN", "// PES21FX_LIVE_STRAIN", "// PES21FX_LIVE_SEAM_LOCK", "// PES21FX_LIVE_SEAM_WIDTH" };
    const char* names[10] = { "PES_FIT_ON  ", "PES_FIT_THIN", "PES_FIT_FAT ", "PES_FIT_RANGE", "PES_CLOTH_FX", "PES_THIGH_REF", "PES_LIMB_GAIN", "PES_STRAIN_ENC", "PES_SEAM_LOCK", "PES_SEAM_WIDTH" };
    int done = 0;
    for (int i = 0; i < 10; i++) {
        size_t m = src.find(marks[i]); if (m == std::string::npos) continue;
        size_t ls = src.rfind('\n', m); ls = (ls == std::string::npos) ? 0 : ls + 1;
        char line[96];
        if (i == 0 || i == 4 || i == 7) snprintf(line, sizeof(line), "#define %s       %d      ", names[i], (int)v[i]);
        else snprintf(line, sizeof(line), "#define %s       %.2f   ", names[i], v[i]);
        src.replace(ls, m - ls, line); done++;
    }
    if (!done) return;
    for (int i = 0; i < 10; i++) last[i] = v[i];
    if (src == before) return;   // 18/10: sin cambios reales no se reescribe (cada escritura hace recompilar al mod de ropa)
    if (t < 45.0) { next = 45.0; for (int i = 0; i < 10; i++) last[i] = -1; return; }   // nunca en el arranque
    std::ofstream o(path, std::ios::binary | std::ios::trunc); if (!o) return;
    o << src; o.close();
    Log("ropa: ajuste al fisico %s (delgado +%.0f mm, ancho -%.0f mm, rango %.2f); mejoras de tela %s", v[0] > 0.5f ? "on" : "off", v[1], v[2], v[3], v[4] > 0.5f ? "activas" : "APAGADAS (version base)");
}

bool LoadConfig() { bool r = LoadConfigLocked(); LoadHairConfig(); InspectReload(); return r; }

// ---------------------------------------------------------------- PES21FX_hair.ini (pelo en hebras)
// [tipos]  nombre = largo_cm, rizo, rigidez, peso, grosor_mm, densidad, brillo, flequillo [, raizR,G,B, puntaR,G,B]
// [cejas]  nombre = grosor, densidad, elevacion, largo      (multiplicadores del grupo "Cejas con hebras")
// [caras]  id = tipo                                          (id = el que muestra el log "pelo: cara <id>" o hair\auto\lista.txt)
// [auto]   default = corto
bool LoadHairConfig() {
    HairCfg h;
    std::vector<std::string> lines;
    bool ok = ReadLines(g_cfg.baseDir + L"PES21FX_hair.ini", lines);
    std::string sec;
    std::map<std::string, HairType> types;
    if (ok) for (auto& raw : lines) {
        std::string l = Trim(raw);
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l[0] == '[') { sec = Lower(Trim(l.substr(1, l.find(']') - 1))); continue; }
        size_t eq = l.find('='); if (eq == std::string::npos) continue;
        std::string k = Lower(Trim(l.substr(0, eq))), v = Trim(l.substr(eq + 1));
        size_t sc = v.find(';'); if (sc != std::string::npos) v = Trim(v.substr(0, sc));
        if (sec == "tipos") {
            std::vector<float> f = ParseFloats(v);
            HairType t = types.count(k) ? types[k] : HairType(); t.name = k;
            if (f.size() > 0) t.largo = f[0]; if (f.size() > 1) t.rizo = f[1]; if (f.size() > 2) t.rigidez = f[2]; if (f.size() > 3) t.peso = f[3];
            if (f.size() > 4) t.grosor = f[4]; if (f.size() > 5) t.densidad = f[5]; if (f.size() > 6) t.brillo = f[6]; if (f.size() > 7) t.flequillo = f[7];
            if (f.size() > 10) { t.raiz[0] = f[8]; t.raiz[1] = f[9]; t.raiz[2] = f[10]; }
            if (f.size() > 13) { t.punta[0] = f[11]; t.punta[1] = f[12]; t.punta[2] = f[13]; }
            types[k] = t;
        } else if (sec == "cejas") {
            std::vector<float> f = ParseFloats(v);
            HairType& t = types[k]; t.name = k;
            if (f.size() > 0) t.cejaGrosor = f[0]; if (f.size() > 1) t.cejaDens = f[1]; if (f.size() > 2) t.cejaElev = f[2]; if (f.size() > 3) t.cejaLargo = f[3];
        } else if (sec == "caras") {
            h.faces[k] = Lower(v);
        } else if (sec == "auto") {
            if (k == "default") h.defType = Lower(v);
        }
    }
    for (auto& kv : types) h.types.push_back(kv.second);
    if (h.types.empty()) { HairType t; t.name = "corto"; h.types.push_back(t); }
    h.loaded = ok;
    { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.hair = h; }
    FaceGenHairChanged();
    Log("pelo: %s (%d tipos, %d caras asignadas, default '%s')", ok ? "PES21FX_hair.ini leido" : "sin PES21FX_hair.ini (tipos por defecto)", (int)h.types.size(), (int)h.faces.size(), h.defType.c_str());
    return ok;
}
const HairType* HairTypeFor(const std::string& id, std::string* outName) {
    // sin candado: se llama desde Present con g_cfg estable; el vector solo cambia en LoadHairConfig
    const HairCfg& h = g_cfg.hair;
    std::string want = h.defType;
    auto it = h.faces.find(Lower(id)); if (it != h.faces.end()) want = it->second;
    const HairType* best = nullptr;
    for (auto& t : h.types) { if (t.name == want) { best = &t; break; } }
    if (!best) for (auto& t : h.types) { if (t.name == h.defType) { best = &t; break; } }
    if (!best && !h.types.empty()) best = &h.types[0];
    if (outName) *outName = best ? best->name : "";
    return best;
}

} // namespace fx
