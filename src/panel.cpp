// PES21 FX (08/11) - PANEL EN VIVO: servidor HTTP minimo SOLO en 127.0.0.1 (pedido de Agustin: "un entorno visual para revisar
// archivos del juego y probar valores, camaras, etc."). La pagina (PES21FX\panel\index.html, la misma que sirve el visor de
// archivos tools\visor\visor.py) mueve las filas del ini en vivo, maneja la camara libre, lee el log y saca capturas A/B sin
// pasar por el menu Insert.
//
//   [PES21FX] panel_puerto = 21021   (0 = apagado). Solo escucha en 127.0.0.1: nada de afuera de la PC puede entrar.
//   Las peticiones que cambian algo (POST) solo se aceptan sin Origin o con Origin http://127.0.0.1 / http://localhost (otra
//   pagina abierta en el navegador no puede tocar el juego).
//
//   GET  /api/ping                     estado: version, cuadro, menu abierto, bypass, partido
//   GET  /api/params                   live y base de las kParamCount filas (x y z w)
//   GET  /api/menu                     tarjetas del menu (titulo, seccion, interruptor, sliders con rango / opciones)
//   POST /api/set     cuerpo "i c v" por linea (fila, componente 0..3, valor): cambia live (no guarda)
//   POST /api/save                     guarda live en PES21FX.ini (lo mismo que Guardar del menu)
//   POST /api/reload                   recargar config + shaders (en el hilo de render, como F10)
//   POST /api/shaders                  recargar solo los shaders
//   POST /api/bypass?on=0|1            juego original / mod
//   GET  /api/log?from=N               PES21FX.log desde el byte N (cabecera X-Log-Size = tamano total)
//   GET  /api/cam                      camara del juego (ultima aplicada) + camara libre
//   POST /api/cam?x=&y=&z=&yaw=&pitch= pone la camara libre en esa pose (en grados); fov con /api/set 344 2
//   POST /api/shot?name=               captura del cuadro final -> PES21FX\capturas\panel\<name>.jpg
//   GET  /api/shots                    lista de capturas del panel;  GET /capturas/<archivo> la imagen
//   GET  /<archivo>                    archivos estaticos de PES21FX\panel\ (index.html por defecto)
#include <winsock2.h>   // antes de core.h (windows.h): si no, winsock.h choca con winsock2.h
#include <ws2tcpip.h>
#include "core.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

namespace fx {

static std::atomic<bool> s_wantReload{false}, s_wantShaders{false};
static std::mutex s_shotMx; static std::wstring s_shotPath; static std::atomic<int> s_shotState{0};   // 0 nada, 1 pedida, 2 lista, -1 fallo
static std::atomic<bool> s_started{false};
static int s_port = 0;

static std::string JStr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char ch : s) {
        switch (ch) {
        case '"': o += "\\\""; break; case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break; case '\r': break; case '\t': o += "\\t"; break;
        default: if (ch < 0x20) { char b[8]; snprintf(b, 8, "\\u%04x", ch); o += b; } else o += (char)ch;
        }
    }
    return o + "\"";
}
static std::string JNum(float v) {
    if (!(v == v) || v > 3e38f || v < -3e38f) return "0";
    char b[32]; snprintf(b, 32, "%.6g", v); return b;
}
static std::string Narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0'); if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr); return s;
}
static std::wstring Widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0'); if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); return w;
}
static std::string UrlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) { o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
        else o += s[i];
    }
    return o;
}
static std::string QueryArg(const std::string& q, const char* key) {
    std::string k = std::string(key) + "=";
    size_t p = 0;
    while (p < q.size()) {
        size_t e = q.find('&', p); if (e == std::string::npos) e = q.size();
        if (q.compare(p, k.size(), k) == 0) return UrlDecode(q.substr(p + k.size(), e - p - k.size()));
        p = e + 1;
    }
    return "";
}
static bool SafeName(const std::string& n) {
    if (n.empty() || n.size() > 120 || n.find("..") != std::string::npos) return false;
    for (unsigned char ch : n) if (!(isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '/')) return false;
    return n[0] != '/';
}
static bool ReadFileAll(const std::wstring& path, std::string& out, long long from = 0, long long* total = nullptr) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz);
    if (total) *total = sz.QuadPart;
    if (from < 0 || from > sz.QuadPart) from = 0;
    long long n = sz.QuadPart - from;
    if (n > 4 * 1024 * 1024) { from = sz.QuadPart - 4 * 1024 * 1024; n = 4 * 1024 * 1024; }   // maximo 4 MB por pedido
    LARGE_INTEGER p; p.QuadPart = from; SetFilePointerEx(h, p, nullptr, FILE_BEGIN);
    out.resize((size_t)n); DWORD rd = 0;
    BOOL ok = n == 0 || ReadFile(h, &out[0], (DWORD)n, &rd, nullptr);
    out.resize(rd);
    CloseHandle(h);
    return ok != FALSE;
}
static const char* Mime(const std::string& n) {
    auto ends = [&](const char* e) { size_t l = strlen(e); return n.size() >= l && _stricmp(n.c_str() + n.size() - l, e) == 0; };
    if (ends(".html")) return "text/html; charset=utf-8"; if (ends(".js")) return "text/javascript; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8"; if (ends(".json")) return "application/json";
    if (ends(".jpg") || ends(".jpeg")) return "image/jpeg"; if (ends(".png")) return "image/png"; if (ends(".svg")) return "image/svg+xml";
    if (ends(".ttf")) return "font/ttf";
    return "application/octet-stream";
}

static bool OriginOk(const std::string& origin) {
    if (origin.empty()) return true;
    return origin.rfind("http://127.0.0.1", 0) == 0 || origin.rfind("http://localhost", 0) == 0;
}

static void Send(SOCKET s, int code, const char* type, const std::string& body, const std::string& origin, const std::string& extra = "") {
    const char* msg = code == 200 ? "OK" : code == 204 ? "No Content" : code == 400 ? "Bad Request" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : "Error";
    std::string h = "HTTP/1.1 " + std::to_string(code) + " " + msg + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nCache-Control: no-store\r\nConnection: close\r\n";
    if (!origin.empty() && OriginOk(origin)) h += "Access-Control-Allow-Origin: " + origin + "\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nAccess-Control-Expose-Headers: X-Log-Size\r\n";
    h += extra + "\r\n";
    std::string all = h + body;
    const char* p = all.data(); int left = (int)all.size();
    while (left > 0) { int k = send(s, p, left, 0); if (k <= 0) break; p += k; left -= k; }
}

// ---------------------------------------------------------------- respuestas
static std::string ApiPing() {
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    const MatchInfo& m = g_cfg.match;
    std::string o = "{\"mod\":\"PES21FX\",\"params\":" + std::to_string(kParamCount) + ",\"frame\":" + std::to_string(PresentNo()) +
        ",\"menu\":" + (g_menuOpen.load() ? "1" : "0") + ",\"bypass\":" + (g_cfg.bypass ? "1" : "0") + ",\"freecam\":" + (FreeCamActive() ? "1" : "0") +
        ",\"bb\":[" + std::to_string(g_bbW) + "," + std::to_string(g_bbH) + "]" +
        ",\"match\":{\"known\":" + (m.known ? "1" : "0") + ",\"stadium\":" + std::to_string(m.stadium) + ",\"timeofday\":" + std::to_string(m.timeofday) +
        ",\"weather\":" + std::to_string(m.weather) + ",\"season\":" + std::to_string(m.season) + "}" +
        ",\"preset\":" + JStr(g_cfg.presetActive.file) + ",\"dir\":" + JStr(Narrow(g_cfg.baseDir)) + "}";
    return o;
}
static std::string ApiParams() {
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    std::string o; o.reserve(kParamCount * 80);
    o += "{\"n\":" + std::to_string(kParamCount) + ",\"live\":[";
    for (int i = 0; i < kParamCount; i++) { if (i) o += ','; o += '['; for (int c = 0; c < 4; c++) { if (c) o += ','; o += JNum(g_cfg.live[i][c]); } o += ']'; }
    o += "],\"base\":[";
    for (int i = 0; i < kParamCount; i++) { if (i) o += ','; o += '['; for (int c = 0; c < 4; c++) { if (c) o += ','; o += JNum(g_cfg.base[i][c]); } o += ']'; }
    o += "],\"defined\":[";
    for (int i = 0; i < kParamCount; i++) { if (i) o += ','; int m = 0; for (int c = 0; c < 4; c++) if (g_cfg.info[i][c].defined) m |= 1 << c; o += std::to_string(m); }
    o += "],\"comment\":{";
    bool first = true;
    for (int i = 0; i < kParamCount; i++) for (int c = 0; c < 4; c++) {
        const std::string& cm = g_cfg.info[i][c].comment; if (cm.empty()) continue;
        if (!first) o += ','; first = false;
        o += "\"" + std::string(1, "xyzw"[c]) + std::to_string(i) + "\":" + JStr(cm);
    }
    o += "}}";
    return o;
}
static std::string ApiMenu() {
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    std::string o = "{\"lang\":" + std::to_string(g_cfg.lang) + ",\"groups\":[";
    bool fg = true;
    for (const MenuGroup& g : g_cfg.groups) {
        if (!fg) o += ','; fg = false;
        o += "{\"id\":" + JStr(g.id) + ",\"title\":" + JStr(LS(g.title, g.titleEn)) + ",\"section\":" + JStr(LS(g.section, g.sectionEn)) +
             ",\"desc\":" + JStr(LS(g.desc, g.descEn)) + ",\"t\":[" + std::to_string(g.tIdx) + "," + std::to_string(g.tComp) + "],\"special\":" + JStr(g.special) + ",\"sliders\":[";
        bool fs = true;
        for (const MenuSlider& s : g.sliders) {
            if (!fs) o += ','; fs = false;
            o += "{\"i\":" + std::to_string(s.idx) + ",\"c\":" + std::to_string(s.comp) + ",\"label\":" + JStr(LS(s.label, s.labelEn)) +
                 ",\"lo\":" + JNum(s.lo) + ",\"hi\":" + JNum(s.hi) + ",\"fmt\":" + JStr(s.fmt) + ",\"help\":" + JStr(LS(s.help, s.helpEn));
            if (!s.ext.empty()) {
                auto it = g_cfg.clothVals.find(s.ext);
                o += ",\"ext\":" + JStr(s.ext) + ",\"value\":" + JNum(it != g_cfg.clothVals.end() ? it->second : s.def);
            }
            if (!s.options.empty()) {
                o += ",\"options\":[";
                const auto& opts = (g_cfg.lang != 0 && s.optionsEn.size() == s.options.size()) ? s.optionsEn : s.options;
                for (size_t k = 0; k < opts.size(); k++) { if (k) o += ','; o += JStr(opts[k]); }
                o += "],\"optVals\":[";
                for (size_t k = 0; k < s.optVals.size(); k++) { if (k) o += ','; o += JNum(s.optVals[k]); }
                o += "]";
            }
            o += "}";
        }
        o += "]}";
    }
    o += "]}";
    return o;
}
static std::string ApiSet(const std::string& body, const std::string& query) {
    std::string src = body;
    if (src.empty() && !query.empty()) src = QueryArg(query, "i") + " " + QueryArg(query, "c") + " " + QueryArg(query, "v");
    int n = 0, bad = 0;
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    size_t p = 0;
    while (p < src.size()) {
        size_t e = src.find('\n', p); if (e == std::string::npos) e = src.size();
        std::string line = src.substr(p, e - p); p = e + 1;
        int i = -1, c = -1; float v = 0;
        if (sscanf(line.c_str(), "%d %d %f", &i, &c, &v) == 3) {
            if (i >= 0 && i < kParamCount && c >= 0 && c < 4 && v == v) { g_cfg.live[i][c] = v; n++; } else bad++;
        } else {
            // "ext seccion.clave valor": cloth-mod\cloth.ini (el mod de ropa lo relee solo)
            char key[128] = {}; float fv = 0;
            if (sscanf(line.c_str(), "ext %127s %f", key, &fv) == 2) { g_cfg.clothVals[key] = fv; g_cfg.clothDirty = true; g_cfg.clothChangeT = Now(); n++; }
            else if (!line.empty() && line != "\r") bad++;
        }
    }
    return "{\"ok\":" + std::to_string(n) + ",\"bad\":" + std::to_string(bad) + "}";
}
static std::string ApiShots() {
    std::wstring dir = g_cfg.baseDir + L"capturas\\panel\\";
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"*.jpg").c_str(), &fd);
    std::vector<std::string> names;
    if (h != INVALID_HANDLE_VALUE) { do names.push_back(Narrow(fd.cFileName)); while (FindNextFileW(h, &fd)); FindClose(h); }
    std::sort(names.begin(), names.end());
    std::string o = "{\"state\":" + std::to_string(s_shotState.load()) + ",\"files\":[";
    for (size_t i = 0; i < names.size(); i++) { if (i) o += ','; o += JStr(names[i]); }
    return o + "]}";
}

static void Handle(SOCKET s) {
    DWORD to = 4000; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof to);
    std::string req; char buf[8192];
    size_t hdrEnd = std::string::npos;
    while (hdrEnd == std::string::npos && req.size() < 65536) {
        int k = recv(s, buf, sizeof buf, 0); if (k <= 0) return;
        req.append(buf, k); hdrEnd = req.find("\r\n\r\n");
    }
    if (hdrEnd == std::string::npos) return;
    std::string head = req.substr(0, hdrEnd), body = req.substr(hdrEnd + 4);
    std::string method, target;
    { size_t a = head.find(' '), b = head.find(' ', a + 1); if (a == std::string::npos || b == std::string::npos) return; method = head.substr(0, a); target = head.substr(a + 1, b - a - 1); }
    auto header = [&](const char* name) -> std::string {
        std::string lh = head; for (auto& ch : lh) ch = (char)tolower((unsigned char)ch);
        std::string key = std::string("\r\n") + name + ":";
        size_t p = lh.find(key); if (p == std::string::npos) return "";
        size_t e = head.find("\r\n", p + 2); std::string v = head.substr(p + key.size(), e - p - key.size());
        while (!v.empty() && v[0] == ' ') v.erase(0, 1); return v;
    };
    const std::string origin = header("origin");
    const size_t clen = (size_t)std::max(0, atoi(header("content-length").c_str()));
    while (body.size() < clen && body.size() < 1024 * 1024) { int k = recv(s, buf, sizeof buf, 0); if (k <= 0) break; body.append(buf, k); }
    std::string path = target, query;
    { size_t q = target.find('?'); if (q != std::string::npos) { path = target.substr(0, q); query = target.substr(q + 1); } }
    path = UrlDecode(path);
    if (method == "OPTIONS") { Send(s, 204, "text/plain", "", origin); return; }
    if (!OriginOk(origin)) { Send(s, 403, "text/plain", "origen no permitido", origin); return; }
    const bool post = method == "POST";
    const char* J = "application/json";
    if (path == "/api/ping") { Send(s, 200, J, ApiPing(), origin); return; }
    if (path == "/api/params") { Send(s, 200, J, ApiParams(), origin); return; }
    if (path == "/api/menu") { Send(s, 200, J, ApiMenu(), origin); return; }
    if (path == "/api/log") {
        std::string txt; long long total = 0; long long from = _atoi64(QueryArg(query, "from").c_str());
        if (!ReadFileAll(g_cfg.baseDir + L"PES21FX.log", txt, from, &total)) { Send(s, 404, "text/plain", "sin log", origin); return; }
        Send(s, 200, "text/plain; charset=windows-1252", txt, origin, "X-Log-Size: " + std::to_string(total) + "\r\n"); return;
    }
    if (path == "/api/cam") {
        if (post) {
            float pos[3] = { (float)atof(QueryArg(query, "x").c_str()), (float)atof(QueryArg(query, "y").c_str()), (float)atof(QueryArg(query, "z").c_str()) };
            FreeCamSetPose(pos, (float)atof(QueryArg(query, "yaw").c_str()) * 0.0174533f, (float)atof(QueryArg(query, "pitch").c_str()) * 0.0174533f);
            Send(s, 200, J, "{\"ok\":1}", origin); return;
        }
        float gp[3] = {}, gt[3] = {}; int gid = -1, gf = -1; bool have = CamHookLast(gp, gt, &gid, &gf);
        float fp[3] = {}, yaw = 0, pitch = 0; bool on = FreeCamGetPose(fp, &yaw, &pitch);
        char b[640];
        snprintf(b, sizeof b, "{\"game\":{\"ok\":%d,\"pos\":[%s,%s,%s],\"tgt\":[%s,%s,%s],\"id\":%d,\"frame\":%d},\"free\":{\"on\":%d,\"pos\":[%s,%s,%s],\"yaw\":%s,\"pitch\":%s,\"zoom\":%s,\"fov\":%s,\"speed\":%s}}",
            have ? 1 : 0, JNum(gp[0]).c_str(), JNum(gp[1]).c_str(), JNum(gp[2]).c_str(), JNum(gt[0]).c_str(), JNum(gt[1]).c_str(), JNum(gt[2]).c_str(), gid, gf,
            on ? 1 : 0, JNum(fp[0]).c_str(), JNum(fp[1]).c_str(), JNum(fp[2]).c_str(), JNum(yaw * 57.2958f).c_str(), JNum(pitch * 57.2958f).c_str(),
            JNum(g_cfg.live[344][2]).c_str(), JNum(g_cfg.live[345][3]).c_str(), JNum(g_cfg.live[344][1]).c_str());
        Send(s, 200, J, b, origin); return;
    }
    if (path == "/api/shots") { Send(s, 200, J, ApiShots(), origin); return; }
    if (path.rfind("/capturas/", 0) == 0) {
        std::string n = path.substr(10);
        if (!SafeName(n) || n.find('/') != std::string::npos) { Send(s, 400, "text/plain", "nombre", origin); return; }
        std::string data; if (!ReadFileAll(g_cfg.baseDir + L"capturas\\panel\\" + Widen(n), data)) { Send(s, 404, "text/plain", "no existe", origin); return; }
        Send(s, 200, Mime(n), data, origin); return;
    }
    if (post) {
        if (path == "/api/set") { Send(s, 200, J, ApiSet(body, query), origin); return; }
        if (path == "/api/save") { bool ok = SaveConfigValues(); Log("panel: valores guardados (%s)", ok ? "ok" : "fallo"); Send(s, 200, J, ok ? "{\"ok\":1}" : "{\"ok\":0}", origin); return; }
        if (path == "/api/reload") { s_wantReload = true; Send(s, 200, J, "{\"ok\":1}", origin); return; }
        if (path == "/api/shaders") { s_wantShaders = true; Send(s, 200, J, "{\"ok\":1}", origin); return; }
        if (path == "/api/bypass") { { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.bypass = atoi(QueryArg(query, "on").c_str()) != 0; } Send(s, 200, J, g_cfg.bypass ? "{\"bypass\":1}" : "{\"bypass\":0}", origin); return; }
        if (path == "/api/shot") {
            std::string n = QueryArg(query, "name");
            if (n.empty()) { SYSTEMTIME st; GetLocalTime(&st); char b[32]; snprintf(b, 32, "%02d%02d%02d_%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds); n = b; }
            if (!SafeName(n) || n.find('/') != std::string::npos) { Send(s, 400, "text/plain", "nombre", origin); return; }
            std::wstring dir = g_cfg.baseDir + L"capturas"; CreateDirectoryW(dir.c_str(), nullptr); dir += L"\\panel"; CreateDirectoryW(dir.c_str(), nullptr);
            { std::lock_guard<std::mutex> lk(s_shotMx); s_shotPath = dir + L"\\" + Widen(n) + L".jpg"; }
            s_shotState = 1;
            Send(s, 200, J, "{\"ok\":1,\"file\":" + JStr(n + ".jpg") + "}", origin); return;
        }
        Send(s, 404, J, "{\"error\":\"no existe\"}", origin); return;
    }
    // estaticos: PES21FX\panel\...
    std::string n = path == "/" ? "index.html" : path.substr(1);
    if (!SafeName(n)) { Send(s, 400, "text/plain", "nombre", origin); return; }
    std::wstring wn = Widen(n); for (auto& ch : wn) if (ch == L'/') ch = L'\\';
    std::string data;
    if (!ReadFileAll(g_cfg.baseDir + L"panel\\" + wn, data)) { Send(s, 404, "text/plain; charset=utf-8", "No esta PES21FX\\panel\\" + n, origin); return; }
    Send(s, 200, Mime(n), data, origin);
}

static void ServerThread(int port) {
    WSADATA wd; if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) { Log("panel: WSAStartup fallo"); return; }
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { Log("panel: socket fallo"); return; }
    BOOL excl = TRUE; setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof excl);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((u_short)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(ls, (sockaddr*)&a, sizeof a) != 0 || listen(ls, 8) != 0) { Log("panel: no se pudo abrir 127.0.0.1:%d (error %d): panel apagado", port, WSAGetLastError()); closesocket(ls); return; }
    Log("panel: escuchando en http://127.0.0.1:%d/ (solo esta PC)", port);
    for (;;) {
        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) { Sleep(50); continue; }
        __try { Handle(c); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("panel: excepcion atendiendo un pedido (ignorada)"); }
        shutdown(c, SD_BOTH); closesocket(c);
    }
}
static void ServerThreadSafe(int port) { ServerThread(port); }

void PanelStart(int port) {
    if (port <= 0) { Log("panel: apagado (panel_puerto = 0)"); return; }
    if (s_started.exchange(true)) return;
    s_port = port;
    std::thread(ServerThreadSafe, port).detach();
}

// hilo de render (hk_Present): lo que no se puede hacer desde el hilo del servidor
void PanelPresent(ID3D11Texture2D* frame) {
    if (s_wantReload.exchange(false)) { LoadConfig(); ReloadShaders(); Log("panel: config + shaders recargados"); }
    if (s_wantShaders.exchange(false)) { ReloadShaders(); Log("panel: shaders recargados"); }
    if (s_shotState.load() == 1) {
        std::wstring p; { std::lock_guard<std::mutex> lk(s_shotMx); p = s_shotPath; }
        if (!frame) { s_shotState = -1; Log("panel: captura pedida pero no hay copia del cuadro"); return; }
        EnsureComInit();
        s_shotState = AsyncShot(frame, p) ? 2 : -1;
    }
}

} // namespace fx
