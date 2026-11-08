// PES21 FX - lector y reproductor de .gani de la CARA (07/11, Agustin: "quisiera saber si podriamos crear archivos .gani para que
// el juego las lea de manera mas sencilla" / "me gustaria que al jugar leas los ganis").
//
// Los gestos de la cara del juego son clips .gani de Fox Engine sobre el rig face_skel.frig (31 huesos skf_* + NMdriverB, 59
// canales). Estan sueltos en el Dream Patch (livecpk\root\common\anime\FoxAnim\Face\: 167 base_* / smil_* / angr_* / pain_* y 15
// aditivos add_*) y en dt13_all.cpk. Formato (verificado con tools/gani_cara.py sobre base_normal / smil_soft / add_eyeblink_single):
//   u32 magia D2A2FC0B, u32 nodeOff (0x20), u32 tamano ... "MOTION" ... cabecera del movimiento en nodeOff + 0x70:
//   u32 grupos (32), u32 canales (59), u32 banderas, u32 cuadros, u32 fps? (10) y grupos x u32 offset (relativo a la cabecera).
//   Grupo: u32 hash (generico, NO es el hueso), u8 nCanales, 3 B, nCanales x (u32 offset relativo a la entrada, i16 indice del
//   canal, u8 banderas, i8 bits). Canal de rotacion (12 bits): por clave 3 fracciones de 12 bits (angulo x pi/2 = medio angulo, dos
//   partes del eje en norma L1, la tercera = 1 - a - b), 3 signos, paso de 8 bits hasta la clave siguiente (la ultima sin paso).
//   Canal de posicion (16 bits): 3 half en CENTIMETROS + paso i8. Todo LOCAL al reposo del hueso (face_anim_skel.ask: reposo
//   identidad, padre skf_head salvo los labios de abajo que cuelgan de skf_jaw). Ejes locales: x arriba, y atras, z derecha del
//   jugador (en los ejes de la cabeza del mod: y, -z, -x). 30 cuadros por segundo (parpadeo = 8 cuadros).
//   El hueso de cada canal lo da face_skel.frig: unidad 0 raiz (canales 0, 1), 1 skf_head (2, 3), 2 skf_jaw (4, solo giro),
//   3..30 un hueso con giro + posicion (los 4 parpados solo giro), 31 NMdriverB (57, 58). Aca va fijo (kAskBones / kChanBone).
//
// Los clips se leen de PES21FX\anim\face\*.gani y de las carpetas listadas en PES21FX\anim\face\carpetas.txt (una por linea, la
// del Dream Patch: ...\livecpk\root\common\anime\FoxAnim\Face\), con dos niveles de subcarpetas (Animations\Add, Animations\Base). Son archivos de Konami: no van al repo, se leen de donde estan. Fila 383 (ini): x = numero de clip (0 = ninguno,
// 1.. en orden alfabetico; el nombre se escribe en el registro al cambiar), y = peso, z = velocidad, w = 0 bucle / 1 una vez.
// exprbones.cpp (ExprBonesFrame) pide la pose del clip en el reloj del juego y la escribe en los huesos.
#include "core.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace fx {

namespace {
const int kAskN = 31;
const char* const kAskBones[kAskN] = {
    "skf_head", "skf_jaw", "skf_brow_o_l", "skf_brow_o_r", "skf_brow_i_l", "skf_brow_i_r", "skf_nasolabialfold_t_l",
    "skf_nasolabialfold_t_r", "skf_orbicularisoculi_b_r", "skf_orbicularisoculi_b_l", "skf_lip_b_l", "skf_lip_b_r", "skf_lip_b_c",
    "skf_glabella", "skf_eyelid_t_r", "skf_eyelid_t_l", "skf_eyelid_b_r", "skf_eyelid_b_l", "skf_nose", "skf_nosewing_l",
    "skf_nosewing_r", "skf_cheek_l", "skf_cheek_r", "skf_cheek_s_l", "skf_cheek_s_r", "skf_lip_t_l", "skf_lip_t_c", "skf_lip_t_r",
    "skf_lip_s_l", "skf_lip_s_r", "NMdriverB" };
// canal -> (hueso del .ask, 0 giro / 1 posicion); -1 = raiz
struct ChanMap { int bone; int kind; };
static ChanMap ChanOf(int ch) {
    if (ch < 2) return { -1, ch };                 // raiz
    if (ch < 4) return { 0, ch - 2 };              // skf_head
    if (ch == 4) return { 1, 0 };                  // skf_jaw (solo giro)
    if (ch < 29) { const int k = ch - 5; return { 2 + k / 2, k % 2 }; }   // huesos 2..13 (giro + posicion)
    if (ch < 33) return { 14 + (ch - 29), 0 };     // parpados 14..17 (solo giro)
    if (ch < 57) { const int k = ch - 33; return { 18 + k / 2, k % 2 }; }  // huesos 18..29
    if (ch < 59) return { 30, ch - 57 };           // NMdriverB
    return { -2, 0 };
}

struct RotKey { float f; float q[4]; };
struct PosKey { float f; float p[3]; };
struct Track { std::vector<RotKey> rot; std::vector<PosKey> pos; };
struct Clip { std::string name; std::wstring path; int frames = 0; Track tr[kAskN]; bool ok = false; bool loaded = false; };

std::mutex s_mx;
std::vector<Clip> s_clips;          // orden alfabetico
std::vector<std::wstring> s_dirs;
bool s_scanned = false; unsigned s_lastScan = 0;
std::atomic<int> s_badFiles{0}, s_samples{0};
int s_loggedClip = -1;

struct Bits { const uint8_t* b; size_t n = 0, len; Bits(const uint8_t* p, size_t bytes) : b(p), len(bytes * 8) {}
    uint32_t u(int k) { uint32_t v = 0; for (int i = 0; i < k; i++) { v |= (uint32_t)((b[n >> 3] >> (n & 7)) & 1) << i; n++; } return v; } };
static float Half(uint16_t h) {
    const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023; float v;
    if (e == 0) v = ldexpf((float)m, -24); else if (e == 31) v = m ? NAN : INFINITY; else v = ldexpf((float)(m | 1024), (int)e - 25);
    return s ? -v : v;
}
static void DecRot(const uint8_t* d, size_t n, int bits, std::vector<RotKey>& out) {
    Bits B(d, n); float f = 0; const size_t full = 3 * bits + 3;
    while (B.n + full <= B.len) {
        float v[3]; for (int i = 0; i < 3; i++) v[i] = (float)B.u(bits) / (float)(1u << bits);
        int s[3]; for (int i = 0; i < 3; i++) s[i] = (int)B.u(1);
        const float a = v[1], b = v[2], c = 1.0f - a - b, nn = sqrtf(a * a + b * b + c * c), h = v[0] * 1.5707963f;
        const float sn = nn > 1e-9f ? sinf(h) / nn : 0.0f;
        RotKey k; k.f = f; k.q[0] = a * sn * (s[0] ? -1.0f : 1.0f); k.q[1] = b * sn * (s[1] ? -1.0f : 1.0f); k.q[2] = c * sn * (s[2] ? -1.0f : 1.0f); k.q[3] = cosf(h);
        if (!out.empty()) { const RotKey& p = out.back(); if (p.q[0] * k.q[0] + p.q[1] * k.q[1] + p.q[2] * k.q[2] + p.q[3] * k.q[3] < 0) for (float& x : k.q) x = -x; }
        out.push_back(k);
        if (B.n + 8 > B.len) break;
        const uint32_t st = B.u(8); if (st == 0) break; f += (float)st;
    }
}
static void DecPos(const uint8_t* d, size_t n, std::vector<PosKey>& out) {
    size_t p = 0; float f = 0;
    while (p + 6 <= n) {
        PosKey k; k.f = f; for (int i = 0; i < 3; i++) { uint16_t h; memcpy(&h, d + p + 2 * i, 2); k.p[i] = Half(h); }
        out.push_back(k); p += 6;
        if (p >= n) break;
        const uint8_t st = d[p]; p++; if (st == 0) break; f += (float)st;   // (08/11) paso sin signo (pain_brwtrb...: 159 = 0x9F); con int8 se perdia la clave final
    }
}
static bool Parse(const std::vector<uint8_t>& g, Clip& c) {
    if (g.size() < 0x100) return false;
    uint32_t sig, nodeOff; memcpy(&sig, g.data(), 4); memcpy(&nodeOff, g.data() + 4, 4);
    if (sig != 0xD2A2FC0Bu && sig != 0x0BFCA2D2u) { /* magia distinta: se intenta igual */ }
    const size_t m = nodeOff + 0x70; if (m + 20 > g.size()) return false;
    uint32_t groups, nch, flags, frames, unk; memcpy(&groups, g.data() + m, 4); memcpy(&nch, g.data() + m + 4, 4); memcpy(&flags, g.data() + m + 8, 4); memcpy(&frames, g.data() + m + 12, 4); memcpy(&unk, g.data() + m + 16, 4);
    if (groups == 0 || groups > 64 || nch != 59 || frames == 0 || frames > 100000) return false;
    if (m + 20 + 4 * groups > g.size()) return false;
    struct Ch { int idx; int bits; size_t off; size_t size; };
    std::vector<Ch> chs;
    for (uint32_t gi = 0; gi < groups; gi++) {
        uint32_t o; memcpy(&o, g.data() + m + 20 + 4 * gi, 4); const size_t p = m + o; if (p + 8 > g.size()) return false;
        const int n = g[p + 4];
        for (int ci = 0; ci < n; ci++) {
            const size_t q = p + 8 + 8 * ci; if (q + 8 > g.size()) return false;
            uint32_t a; int16_t ix; memcpy(&a, g.data() + q, 4); memcpy(&ix, g.data() + q + 4, 2); const int8_t bs = (int8_t)g[q + 7];
            chs.push_back({ ix, bs, q + a, 0 });
        }
    }
    std::vector<int> order(chs.size()); for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return chs[a].off < chs[b].off; });
    for (size_t k = 0; k < order.size(); k++) { const size_t end = k + 1 < order.size() ? chs[order[k + 1]].off : g.size(); Ch& c0 = chs[order[k]]; c0.size = end > c0.off ? end - c0.off : 0; if (c0.off > g.size()) return false; }
    for (const Ch& ch : chs) {
        const ChanMap cm = ChanOf(ch.idx); if (cm.bone < 0 || cm.bone >= kAskN || ch.size == 0) continue;
        if (cm.kind == 0 && (ch.bits == 12 || ch.bits == 20)) DecRot(g.data() + ch.off, ch.size, ch.bits, c.tr[cm.bone].rot);
        else if (cm.kind == 1 && ch.bits == 16) DecPos(g.data() + ch.off, ch.size, c.tr[cm.bone].pos);
    }
    c.frames = (int)frames; c.ok = true; return true;
}
static bool LeerArchivo(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{}; if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (1 << 22)) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart); DWORD rd = 0; const bool ok = ReadFile(h, out.data(), (DWORD)out.size(), &rd, nullptr) && rd == out.size(); CloseHandle(h); return ok;
}
static void Load(Clip& c) {   // con s_mx tomado
    c.loaded = true; std::vector<uint8_t> g;
    if (!LeerArchivo(c.path, g) || !Parse(g, c)) { c.ok = false; s_badFiles.fetch_add(1, std::memory_order_relaxed); Log("gani: no se pudo leer %s", c.name.c_str()); return; }
}
static void Scan() {   // con s_mx tomado
    s_scanned = true;
    std::vector<Clip> found;
    std::vector<std::wstring> dirs; const std::wstring base = g_cfg.baseDir + L"anim\\face\\"; dirs.push_back(base);
    {   // carpetas.txt: una carpeta por linea (ANSI / UTF-8 sin acentos)
        std::vector<uint8_t> t; if (LeerArchivo(base + L"carpetas.txt", t)) {
            std::string s(t.begin(), t.end()); size_t p = 0;
            while (p < s.size()) { size_t e = s.find('\n', p); if (e == std::string::npos) e = s.size(); std::string l = s.substr(p, e - p); p = e + 1;
                while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.pop_back(); size_t b0 = 0; while (b0 < l.size() && (l[b0] == ' ' || l[b0] == '\t')) b0++; l = l.substr(b0);
                if (l.empty() || l[0] == ';' || l[0] == '#') continue;
                std::wstring w; int n = MultiByteToWideChar(CP_UTF8, 0, l.c_str(), (int)l.size(), nullptr, 0); if (n <= 0) continue; w.resize(n); MultiByteToWideChar(CP_UTF8, 0, l.c_str(), (int)l.size(), &w[0], n);
                if (w.back() != L'\\' && w.back() != L'/') w += L'\\'; dirs.push_back(w); } }
    }
    {   // (07/11) un nivel de subcarpetas: en el Dream Patch los clips estan en Face\Animations\Add y Face\Animations\Base
        std::vector<std::wstring> sub;
        for (const std::wstring& d : dirs) {
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((d + L"*").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) continue;
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
                const std::wstring s1 = d + fd.cFileName + L"\\"; sub.push_back(s1);
                WIN32_FIND_DATAW fd2; HANDLE h2 = FindFirstFileW((s1 + L"*").c_str(), &fd2); if (h2 == INVALID_HANDLE_VALUE) continue;
                do { if ((fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd2.cFileName[0] != L'.') sub.push_back(s1 + fd2.cFileName + L"\\"); } while (FindNextFileW(h2, &fd2));
                FindClose(h2);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        for (const std::wstring& s1 : sub) dirs.push_back(s1);
    }
    for (const std::wstring& d : dirs) {
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((d + L"*.gani").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring wn = fd.cFileName; std::string n; int k = WideCharToMultiByte(CP_UTF8, 0, wn.c_str(), -1, nullptr, 0, nullptr, nullptr); if (k <= 0) continue; n.resize(k - 1); WideCharToMultiByte(CP_UTF8, 0, wn.c_str(), -1, &n[0], k, nullptr, nullptr);
            if (n.size() > 5) n.resize(n.size() - 5);   // sin ".gani"
            bool dup = false; for (const Clip& c : found) if (c.name == n) { dup = true; break; } if (dup) continue;
            Clip c; c.name = n; c.path = d + wn; found.push_back(std::move(c));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(found.begin(), found.end(), [](const Clip& a, const Clip& b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
    // conserva lo ya cargado
    for (Clip& c : found) for (Clip& o : s_clips) if (o.loaded && o.name == c.name && o.path == c.path) { c = std::move(o); break; }
    const bool changed = found.size() != s_clips.size();
    s_clips = std::move(found); s_dirs = dirs;
    if (changed || s_lastScan == 0) Log("gani: %d clips de la cara en %d carpetas (PES21FX\\anim\\face y carpetas.txt)", (int)s_clips.size(), (int)dirs.size());
}
static void Sample(const std::vector<RotKey>& k, float f, float* q) {
    if (k.empty()) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
    if (k.size() == 1 || f <= k[0].f) { memcpy(q, k[0].q, 16); return; }
    size_t i = 0; while (i + 2 < k.size() && k[i + 1].f < f) i++;
    const RotKey& a = k[i]; const RotKey& b = k[i + 1];
    const float t = std::clamp((f - a.f) / (std::max)(b.f - a.f, 1e-3f), 0.0f, 1.0f); float l2 = 0;
    for (int j = 0; j < 4; j++) { q[j] = a.q[j] + (b.q[j] - a.q[j]) * t; l2 += q[j] * q[j]; }
    const float il = l2 > 1e-12f ? 1.0f / sqrtf(l2) : 1.0f; for (int j = 0; j < 4; j++) q[j] *= il;
}
static void Sample(const std::vector<PosKey>& k, float f, float* p) {
    if (k.empty()) { p[0] = p[1] = p[2] = 0; return; }
    if (k.size() == 1 || f <= k[0].f) { memcpy(p, k[0].p, 12); return; }
    size_t i = 0; while (i + 2 < k.size() && k[i + 1].f < f) i++;
    const PosKey& a = k[i]; const PosKey& b = k[i + 1];
    const float t = std::clamp((f - a.f) / (std::max)(b.f - a.f, 1e-3f), 0.0f, 1.0f);
    for (int j = 0; j < 3; j++) p[j] = a.p[j] + (b.p[j] - a.p[j]) * t;
}
}   // namespace

int GaniCount() { std::lock_guard<std::mutex> lk(s_mx); if (!s_scanned) Scan(); return (int)s_clips.size(); }
const char* GaniName(int clip) { std::lock_guard<std::mutex> lk(s_mx); if (!s_scanned) Scan(); return clip >= 1 && clip <= (int)s_clips.size() ? s_clips[clip - 1].name.c_str() : nullptr; }

// pose del clip `clip` (1..) en el segundo `t` desde su arranque: giros (local, ejes del .ask) y posiciones (cm) por hueso del .ask;
// loop = vuelve a empezar al terminar, si no queda en el ultimo cuadro. Devuelve false si no hay clip. `done` = termino (sin bucle).
bool GaniSample(int clip, float t, bool loop, GaniPose& out, bool* done) {
    std::lock_guard<std::mutex> lk(s_mx);
    if (!s_scanned) Scan();
    if (clip < 1 || clip > (int)s_clips.size()) return false;
    Clip& c = s_clips[clip - 1];
    if (!c.loaded) Load(c);
    if (!c.ok) return false;
    if (s_loggedClip != clip) { s_loggedClip = clip; int nb = 0; for (int b = 0; b < kAskN; b++) if (c.tr[b].rot.size() > 1 || c.tr[b].pos.size() > 1) nb++; Log("gani: clip %d = %s (%d cuadros, %.2f s, %d huesos animados)", clip, c.name.c_str(), c.frames, c.frames / 30.0f, nb); }
    float f = t * 30.0f; const float F = (float)c.frames;
    if (done) *done = false;
    if (loop) { if (F > 0.5f) f = fmodf((std::max)(f, 0.0f), F); }
    else if (f >= F) { f = F; if (done) *done = true; }
    for (int b = 0; b < kAskN; b++) {
        const Track& tr = c.tr[b];
        out.has[b] = !tr.rot.empty() || !tr.pos.empty();
        Sample(tr.rot, f, out.q[b]); Sample(tr.pos, f, out.p[b]);
    }
    out.frames = c.frames; s_samples.fetch_add(1, std::memory_order_relaxed);
    return true;
}
const char* GaniAskBone(int b) { return b >= 0 && b < kAskN ? kAskBones[b] : nullptr; }

// ---- (08/11, Agustin: "como aplicamos expresiones custom para cada jugador", "que tambien se muestren al expresarse en partido o en
// animaciones", "si despues quiero agregar mas jugadores o expresiones deberia funcionar", "gestos que se reproduzcan entre medio como
// el jugador respirando e hinchando las mejillas") GESTOS PROPIOS POR JUGADOR ----
//   PES21FX\anim\face\<id del jugador>\<gesto>.gani  y  PES21FX\anim\face\todos\<gesto>.gani (para cualquiera; el del jugador gana).
//   <gesto> se empareja con un clip del juego por PES21FX\anim\face\gestos.txt ("sonrisa = smil_soft", una por linea; sin archivo o sin
//   linea valen los de kDefRef): cuando el juego hace ese gesto (se mide en los huesos, exprbones.cpp) la DLL suma la diferencia entre
//   el clip propio y el del juego, en la medida en que el juego lo este haciendo. Los archivos que empiezan con "entre_" (respirar,
//   inflar los cachetes, ...) se reproducen una vez cada tanto, al azar, con la cara quieta, sumados a lo que haga el juego.
//   Todo por nombre de archivo: agregar jugadores o gestos = agregar archivos (y una linea en gestos.txt si el gesto es nuevo).
namespace {
struct DefRef { const char* gest; const char* clip; };
const DefRef kDefRef[] = {
    { "sonrisa", "smil_soft" }, { "risa", "smil_mho" }, { "enojo", "angr_brwnit_soft" }, { "asombro", "supris_brwup_mho_eyo_hard" },
    { "grito", "base_goal_hoe" }, { "puchero", "pow_brwnit_puck_hard" }, { "dolor", "pain_brwtrb_shut_eyhc_hard" },
    { "preocupado", "neut_brwtrb_lorol" }, { "neutra", "neut" },
};
struct PSet { uint32_t pid = 0; unsigned gen = 0; unsigned scanned = 0; std::vector<Clip> clips; std::vector<GaniGest> info; };
std::vector<PSet> s_psets; unsigned s_psetGen = 1;
std::vector<std::pair<std::string, std::string>> s_refMap; bool s_refLoaded = false;
static void LoadRefMap() {   // con s_mx tomado
    s_refLoaded = true; s_refMap.clear();
    for (const DefRef& d : kDefRef) s_refMap.push_back({ d.gest, d.clip });
    std::vector<uint8_t> t; if (!LeerArchivo(g_cfg.baseDir + L"anim\\face\\gestos.txt", t)) return;
    std::string s(t.begin(), t.end()); size_t p = 0; int n = 0;
    while (p < s.size()) {
        size_t e = s.find('\n', p); if (e == std::string::npos) e = s.size(); std::string l = s.substr(p, e - p); p = e + 1;
        const size_t c = l.find_first_of(";#"); if (c != std::string::npos) l.resize(c);
        const size_t eq = l.find('='); if (eq == std::string::npos) continue;
        auto trim = [](std::string x) { while (!x.empty() && (x.back() == ' ' || x.back() == '\t' || x.back() == '\r')) x.pop_back(); size_t b = 0; while (b < x.size() && (x[b] == ' ' || x[b] == '\t')) b++; x = x.substr(b);
            if (x.size() > 5 && _stricmp(x.c_str() + x.size() - 5, ".gani") == 0) x.resize(x.size() - 5); return x; };
        const std::string g = trim(l.substr(0, eq)), r = trim(l.substr(eq + 1)); if (g.empty() || r.empty()) continue;
        bool found = false; for (auto& m : s_refMap) if (_stricmp(m.first.c_str(), g.c_str()) == 0) { m.second = r; found = true; }
        if (!found) s_refMap.push_back({ g, r }); n++;
    }
    Log("gani: gestos.txt %d lineas", n);
}
static Clip* FindClip(const char* name) {   // clip del juego por nombre (lista de Scan)
    for (Clip& c : s_clips) if (_stricmp(c.name.c_str(), name) == 0) { if (!c.loaded) Load(c); return c.ok ? &c : nullptr; }
    return nullptr;
}
static void PoseAt(const Clip& c, float f, float P[kAskN][3], float Z[kAskN]) {   // posiciones (cm) y giro z (rad) de cada hueso
    for (int b = 0; b < kAskN; b++) { float q[4], p[3]; Sample(c.tr[b].rot, f, q); Sample(c.tr[b].pos, f, p); memcpy(P[b], p, 12); Z[b] = 2.0f * atan2f(q[2], q[3]); }
}
static void BasePose(float P[kAskN][3], float Z[kAskN]) {
    if (Clip* b = FindClip("base_normal")) { PoseAt(*b, 0.0f, P, Z); return; }
    memset(P, 0, sizeof(float) * kAskN * 3); memset(Z, 0, sizeof(float) * kAskN); Z[14] = Z[15] = 0.43633f; Z[16] = Z[17] = -0.08727f;
}
static void ListDir(const std::wstring& d, std::vector<Clip>& out) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((d + L"*.gani").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring wn = fd.cFileName; std::string n; int k = WideCharToMultiByte(CP_UTF8, 0, wn.c_str(), -1, nullptr, 0, nullptr, nullptr); if (k <= 0) continue; n.resize(k - 1); WideCharToMultiByte(CP_UTF8, 0, wn.c_str(), -1, &n[0], k, nullptr, nullptr);
        if (n.size() > 5) n.resize(n.size() - 5);
        bool dup = false; for (const Clip& c : out) if (_stricmp(c.name.c_str(), n.c_str()) == 0) { dup = true; break; } if (dup) continue;   // el del jugador va primero y gana
        Clip c; c.name = n; c.path = d + wn; out.push_back(std::move(c));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static void BuildSet(PSet& S) {   // con s_mx tomado
    if (!s_scanned) Scan(); if (!s_refLoaded) LoadRefMap();
    const std::wstring base = g_cfg.baseDir + L"anim\\face\\";
    std::vector<Clip> cl; ListDir(base + std::to_wstring(S.pid) + L"\\", cl); const size_t nOwn = cl.size(); ListDir(base + L"todos\\", cl);
    std::string sig; for (const Clip& c : cl) sig += c.name + "|";
    std::string old; for (const Clip& c : S.clips) old += c.name + "|";
    if (S.gen && sig == old) return;
    S.clips = std::move(cl); S.info.clear(); S.gen = ++s_psetGen;
    float BP[kAskN][3], BZ[kAskN]; BasePose(BP, BZ);
    for (size_t i = 0; i < S.clips.size(); i++) {
        Clip& c = S.clips[i]; Load(c); if (!c.ok) continue;
        GaniGest g{}; strncpy_s(g.name, c.name.c_str(), _TRUNCATE); g.clip = (int)i; g.frames = c.frames;
        g.between = _strnicmp(c.name.c_str(), "entre_", 6) == 0;
        float OP[kAskN][3], OZ[kAskN]; PoseAt(c, 0.5f * (float)c.frames, OP, OZ);
        float RP[kAskN][3], RZ[kAskN]; memcpy(RP, BP, sizeof RP); memcpy(RZ, BZ, sizeof RZ);
        if (!g.between) for (const auto& m : s_refMap) if (_stricmp(m.first.c_str(), c.name.c_str()) == 0) {
            if (Clip* r = FindClip(m.second.c_str())) { PoseAt(*r, 0.5f * (float)r->frames, RP, RZ); g.hasRef = true; }
            else Log("gani: gesto '%s' -> clip del juego '%s' no encontrado (carpetas.txt / gestos.txt)", c.name.c_str(), m.second.c_str());
            break;
        }
        for (int b = 0; b < kAskN; b++) {
            for (int k = 0; k < 3; k++) { g.dP[b][k] = OP[b][k] - RP[b][k]; g.rP[b][k] = RP[b][k] - BP[b][k]; }
            g.dZ[b] = OZ[b] - RZ[b]; g.rZ[b] = RZ[b] - BZ[b];
        }
        if (!g.between && !g.hasRef) Log("gani: gesto '%s' sin clip del juego para comparar: solo con los sliders de gestos", c.name.c_str());
        S.info.push_back(g);
    }
    Log("gani: jugador %u: %d gestos propios (%d de la carpeta del jugador, resto de todos\\)", S.pid, (int)S.info.size(), (int)nOwn);
}
}   // namespace

bool GaniGestGet(uint32_t pid, unsigned& gen, std::vector<GaniGest>& out) {
    std::lock_guard<std::mutex> lk(s_mx);
    PSet* S = nullptr; for (PSet& p : s_psets) if (p.pid == pid) { S = &p; break; }
    if (!S) { if (s_psets.size() > 64) s_psets.erase(s_psets.begin()); s_psets.push_back(PSet{}); S = &s_psets.back(); S->pid = pid; BuildSet(*S); }
    if (gen == S->gen) return false;
    gen = S->gen; out = S->info; return true;
}
// pose de un gesto "entre_" en el segundo t (sin bucle) menos base_normal: posiciones cm y giro z (rad)
bool GaniGestSample(uint32_t pid, int clip, float t, float P[31][3], float Z[31]) {
    std::lock_guard<std::mutex> lk(s_mx);
    for (PSet& S : s_psets) if (S.pid == pid) {
        if (clip < 0 || clip >= (int)S.clips.size() || !S.clips[clip].ok) return false;
        const Clip& c = S.clips[clip]; const float f = (std::min)(t * 30.0f, (float)c.frames);
        float BP[kAskN][3], BZ[kAskN]; BasePose(BP, BZ); PoseAt(c, f, P, Z);
        for (int b = 0; b < kAskN; b++) { for (int k = 0; k < 3; k++) P[b][k] -= BP[b][k]; Z[b] -= BZ[b]; }
        s_samples.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void GaniPresent() {
    static unsigned fr = 0; fr++;
    if (fr % 600 != 0) return;   // cada ~10 s: carpetas nuevas / clips nuevos
    std::lock_guard<std::mutex> lk(s_mx);
    if (g_cfg.live[383][0] > 0.5f || !s_scanned) { const unsigned prev = (unsigned)s_clips.size(); Scan(); s_lastScan = fr; (void)prev; }
    if (fr % 1800 == 0) { LoadRefMap(); for (PSet& p : s_psets) BuildSet(p); }   // (08/11) cada ~30 s: gestos de jugadores nuevos / gestos.txt
    const int smp = s_samples.exchange(0), bad = s_badFiles.exchange(0);
    if (smp || bad) Log("gani: %d poses en 10 s%s", smp, bad ? " (archivos ilegibles, ver arriba)" : "");
}

}   // namespace fx
