// PES21 FX - presets por estadio.
// Sider (modules\PES21FX_match.lua) escribe PES21FX\match.ini con el estadio y las condiciones del partido.
// Aca se lee ese archivo cada ~1 s y, si existe presets\stadium_<id>_<dia|noche>.ini o presets\stadium_<id>.ini,
// se aplican sus [Constants] encima de los valores del usuario (sin tocar PES21FX.ini). Al cambiar de estadio
// sin preset se vuelve a los valores del usuario. Compartir un preset = pasar ese archivo a otra persona.
#include "core.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace fx {

static std::string PTrim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
static std::string PLower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static bool PReadLines(const std::wstring& path, std::vector<std::string>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string l;
    while (std::getline(f, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); out.push_back(l); }
    return true;
}
static bool PParamName(const std::string& n, int& idx, int& comp) {
    if (n.empty()) return false;
    const char* cn = "xyzw"; const char* p = strchr(cn, (char)tolower((unsigned char)n[0]));
    if (!p) return false;
    comp = (int)(p - cn);
    if (n.size() == 1) { idx = 0; return true; }
    for (size_t i = 1; i < n.size(); i++) if (!isdigit((unsigned char)n[i])) return false;
    idx = atoi(n.c_str() + 1);
    return idx >= 0 && idx < kParamCount;
}
static std::wstring PresetDir() { return g_cfg.baseDir + L"presets\\"; }
static std::wstring MatchPath() { return g_cfg.baseDir + L"match.ini"; }
static std::wstring W(const std::string& s) { std::wstring w; for (unsigned char c : s) w.push_back((wchar_t)c); return w; }
static std::string A(const std::wstring& s) { std::string a; for (wchar_t c : s) a.push_back(c < 256 ? (char)c : '?'); return a; }

// lee la cabecera [Preset] y las [Constants] de un archivo de preset
bool ReadPreset(const std::wstring& path, PresetFile& out) {
    std::vector<std::string> lines;
    if (!PReadLines(path, lines)) return false;
    out = PresetFile();
    size_t sl = path.find_last_of(L'\\');
    out.file = A(sl == std::wstring::npos ? path : path.substr(sl + 1));
    std::string sec;
    for (auto& raw : lines) {
        std::string l = PTrim(raw);
        if (l.empty() || l[0] == ';') continue;
        if (l[0] == '[') { sec = PLower(l.substr(1, l.find(']') - 1)); continue; }
        size_t eq = l.find('='); if (eq == std::string::npos) continue;
        std::string k = PLower(PTrim(l.substr(0, eq))), v = PTrim(l.substr(eq + 1));
        if (sec == "preset") {
            if (k == "name") out.name = v; else if (k == "author") out.author = v;
            else if (k == "stadium") out.stadium = atoi(v.c_str());
            else if (k == "timeofday") out.timeofday = (v == "night" || v == "noche" || v == "1") ? 1 : (v == "day" || v == "dia" || v == "0") ? 0 : -1;
        } else if (sec == "constants") {
            int idx, comp; if (!PParamName(k, idx, comp)) continue;
            out.values.push_back({idx, comp, (float)atof(v.c_str())});
        }
    }
    return true;
}

// vuelve a los valores del usuario en las claves que toco el preset activo
static void RestoreUser() {
    for (auto& v : g_cfg.presetKeys) g_cfg.live[v.idx][v.comp] = g_cfg.base[v.idx][v.comp];
    g_cfg.presetKeys.clear();
    g_cfg.presetActive = PresetFile();
}

static void ApplyPresetUnlocked(const PresetFile& p) {
    RestoreUser();
    for (auto& v : p.values) {
        g_cfg.live[v.idx][v.comp] = v.value;
        g_cfg.presetKeys.push_back(v);
    }
    g_cfg.presetActive = p;
    Log("preset aplicado: %s (%s, %d valores)", p.file.c_str(), p.name.c_str(), (int)p.values.size());
}

bool ApplyPresetFile(const std::string& file) {
    PresetFile p;
    if (!ReadPreset(PresetDir() + W(file), p)) return false;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    ApplyPresetUnlocked(p);
    g_cfg.presetManual = true;
    return true;
}

void ClearPreset() {
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    RestoreUser();
    g_cfg.presetManual = true;   // hasta el proximo cambio de estadio no se vuelve a aplicar solo
}

// nombre del archivo que corresponde al partido actual (el mas especifico que exista)
static std::string PresetNameFor(int stadium, int tod) {
    char buf[64];
    if (tod >= 0) {
        snprintf(buf, sizeof(buf), "stadium_%d_%s.ini", stadium, tod == 1 ? "night" : "day");
        if (GetFileAttributesW((PresetDir() + W(buf)).c_str()) != INVALID_FILE_ATTRIBUTES) return buf;
    }
    snprintf(buf, sizeof(buf), "stadium_%d.ini", stadium);
    if (GetFileAttributesW((PresetDir() + W(buf)).c_str()) != INVALID_FILE_ATTRIBUTES) return buf;
    return "";
}

static void ReadMatch(MatchInfo& m) {
    std::vector<std::string> lines; m = MatchInfo();
    if (!PReadLines(MatchPath(), lines)) return;
    m.known = true;
    for (auto& raw : lines) {
        std::string l = PTrim(raw);
        size_t eq = l.find('='); if (l.empty() || l[0] == '[' || l[0] == ';' || eq == std::string::npos) continue;
        std::string k = PLower(PTrim(l.substr(0, eq))); int v = atoi(PTrim(l.substr(eq + 1)).c_str());
        if (k == "stadium") m.stadium = v; else if (k == "timeofday") m.timeofday = v;
        else if (k == "weather") m.weather = v; else if (k == "season") m.season = v; else if (k == "tournament") m.tournament = v;
    }
}

// lista de presets instalados (para la pestana)
void ScanPresets(std::vector<PresetFile>& out) {
    out.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((PresetDir() + L"*.ini").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do { PresetFile p; if (ReadPreset(PresetDir() + fd.cFileName, p)) out.push_back(p); } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const PresetFile& a, const PresetFile& b) { return a.file < b.file; });
}

// cada ~1 s: si cambio match.ini (nuevo partido) se busca el preset del estadio
void PollMatch() {
    static double next = 0; static ULONGLONG lastWrite = 0; static bool first = true;
    double t = Now(); if (t < next) return; next = t + 1.0;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    ULONGLONG w = 0;
    if (GetFileAttributesExW(MatchPath().c_str(), GetFileExInfoStandard, &fa))
        w = ((ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
    if (w == lastWrite && !first) return;
    first = false; lastWrite = w;
    MatchInfo m; ReadMatch(m);
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    bool changed = m.stadium != g_cfg.match.stadium || m.timeofday != g_cfg.match.timeofday;
    g_cfg.match = m;
    if (!m.known) return;
    if (changed) g_cfg.presetManual = false;
    if (g_cfg.presetManual) return;
    std::string f = m.stadium >= 0 ? PresetNameFor(m.stadium, m.timeofday) : "";
    if (f.empty()) { if (!g_cfg.presetKeys.empty()) { RestoreUser(); Log("estadio %d sin preset: valores del usuario", m.stadium); } return; }
    if (f == g_cfg.presetActive.file) return;
    PresetFile p;
    if (ReadPreset(PresetDir() + W(f), p)) ApplyPresetUnlocked(p);
}

// guarda los valores actuales del menu (interruptores y sliders de todos los grupos) como preset del estadio actual
bool SavePresetForMatch(const std::string& name, const std::string& author, bool onlyThisTime, std::string& outFile) {
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (g_cfg.match.stadium < 0) return false;
    CreateDirectoryW(PresetDir().c_str(), nullptr);
    char buf[64];
    int tod = onlyThisTime ? g_cfg.match.timeofday : -1;
    if (tod >= 0) snprintf(buf, sizeof(buf), "stadium_%d_%s.ini", g_cfg.match.stadium, tod == 1 ? "night" : "day");
    else snprintf(buf, sizeof(buf), "stadium_%d.ini", g_cfg.match.stadium);
    outFile = buf;
    std::ofstream f(PresetDir() + W(buf), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "; PES21 FX - preset por estadio. Para compartirlo: copiar este archivo a PES21FX\\presets\\ de otra persona.\r\n";
    f << "[Preset]\r\n";
    f << "name = " << name << "\r\n";
    f << "author = " << author << "\r\n";
    f << "stadium = " << g_cfg.match.stadium << "\r\n";
    if (tod >= 0) f << "timeofday = " << (tod == 1 ? "night" : "day") << "\r\n";
    f << "\r\n[Constants]\r\n";
    std::set<std::pair<int, int>> done;
    const char* cn = "xyzw";
    PresetFile p; p.file = buf; p.name = name; p.author = author; p.stadium = g_cfg.match.stadium; p.timeofday = tod;
    auto emit = [&](int idx, int comp, const std::string& label) {
        if (idx < 0 || !done.insert({idx, comp}).second) return;
        char v[64]; snprintf(v, sizeof(v), "%g", g_cfg.live[idx][comp]);
        char k[16]; if (idx) snprintf(k, sizeof(k), "%c%d", cn[comp], idx); else snprintf(k, sizeof(k), "%c", cn[comp]);
        f << k << " = " << v;
        if (!label.empty()) f << "   ; " << label;
        f << "\r\n";
        p.values.push_back({idx, comp, g_cfg.live[idx][comp]});
    };
    for (auto& g : g_cfg.groups) {
        f << "; -- " << g.title << "\r\n";
        emit(g.tIdx, g.tComp, "on/off");
        for (auto& s : g.sliders) if (s.ext.empty()) emit(s.idx, s.comp, s.label);
    }
    // el preset queda activo tal cual (sus claves son las que se restauran al cambiar de estadio)
    g_cfg.presetKeys = p.values; g_cfg.presetActive = p; g_cfg.presetManual = false;
    Log("preset guardado: %s (%d valores)", buf, (int)p.values.size());
    return true;
}

bool DeletePresetFile(const std::string& file) {
    bool ok = DeleteFileW((PresetDir() + W(file)).c_str()) != 0;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (ok && g_cfg.presetActive.file == file) RestoreUser();
    return ok;
}

} // namespace fx
