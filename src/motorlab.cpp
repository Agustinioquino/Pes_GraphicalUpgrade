// (21/11 3icxbh) LABORATORIO DE ANIMACIONES: ventana nueva del Motor para perfeccionar animaciones sobre una banca de verdad.
// Agustin 21/11 20:19-20:20: "que en el motor del juego se pueda cargar una banca de suplentes para probar animaciones con una ventana
// exclusiva con nombres de las animaciones en el juego sobre los modelos"; "modelos reales de una banca de suplentes, jugadores genericos
// de modo editar"; "modelo de banca real, pasto 3d renderizado, jugadores con animaciones y posibilidad de cambiar las animaciones y
// mostrar valores debug en pantalla, para perfeccionar animaciones, todo como una ventana dentro del motor nueva".
//
// El banco y el pasto 3D de verdad solo existen en el partido (el Editar no tiene estadio), asi que la ventana se abre en un partido
// (F8, o Herramientas > Laboratorio de animaciones). Los jugadores son los PROPIOS del Motor (motorpj.cpp: FixDemoHuman, cualquiera de los
// clips de FixDemo, velocidad, pausa, cuadro, modo, cara, editor de huesos), sentados en los asientos que la banca encontro en el modelo
// del banco del estadio (banca.cpp BuildSpots, necesita Estadio > Banco de suplentes = Si). El reloj es el del partido: banca.cpp
// hk_ModeUpd (FUN_141fc70d0, hilo del juego, carga / partido / cinematicas / repeticion) llama MotorLabGameTick -> MotorPjTick.
// Al terminar el partido (destructor de match::Human, banca.cpp) se sueltan todos antes de que el juego borre la escena.
// Jugadores: "genericos del Editar" = el jugador base de Konami (id 0xffff, el del LoadId del visor del Editar; INFERIDO que el juego lo
// arma con el registro vacio, sin cara de nadie) o "suplentes reales" (los convocados del lado elegido, por su id en la base).
// Mientras el laboratorio usa un banco, los suplentes reales de la banca de ese lado se esconden (banca.cpp SubUpdate).
// (21/11 20:43, Agustin: "yo no queria en un partido real queria en el modo editar motor real"; 20:46: "que al momento de cargar la banca
// tambien carguen los jugadores genericos del modo editar") TAMBIEN EN EL MOTOR DEL EDITAR: ahi el banco es un modelo del estadio st024 puesto
// en la escena del Motor (escena\Default.fpkd, StadiumModel, MotorTiempoReal.lua): el banco de la izquierda (center1_target + center1_probe)
// corrido (+11.40, 0, -40.99) para que quede centrado en x = 0 con el borde del foso en z = +1 (los suplentes miran a -z, al centro del
// estudio, donde apuntan los focos). Asientos medidos del fmdl (tools/fox/fmdl.py): asiento a y = 0.13, piso del foso y = -0.46, respaldo
// z = 44.95 del estadio (cadera a 44.83 -> 3.84 en el Editar). Los jugadores los mueve el reloj del visor del Editar (motorjug.cpp hk_Tick),
// como a los del Motor. Pasto real = el pasto del mod en el editor (fila 393, grass.cpp GrassEditorAfterFace) cortado en el borde del foso
// (g_grassEdCutZ -> pes_grass_cull_cs gPad[4].zw).
// Lo que se ve encima de los modelos se proyecta con la camara con la que el juego dibujo la escena (SceneCB m_projectionView, como
// motorgrid.cpp); con la camara libre prendida, si no coincide con ella, con la de la camara libre.
#include "core.h"
#include "imgui.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cctype>

namespace fx {

int BancaLabSpots(int side, int want, float (*out)[4], int max, char* why, size_t wn);   // banca.cpp
int BancaLabRealSubs(int side, uint32_t* ids, char (*names)[40], int max);              // banca.cpp
bool BancaLabInMatch();                                                                  // banca.cpp: el reloj del partido corre
extern std::atomic<int> g_labHideSide;                                                   // banca.cpp: bit 0 local, bit 1 visitante
extern std::atomic<float> g_grassEdCutZ;                                                 // grass.cpp: corte del pasto del editor

namespace {

constexpr int kLabMax = 16;
std::atomic<bool> s_win{false};          // ventana a la vista
std::atomic<bool> s_session{false};      // el laboratorio tiene jugadores (el reloj del partido los mueve)
std::atomic<double> s_tickT{-1.0e9};     // ultima vez que corrio el reloj del partido
bool s_hadGame = false;                   // solo el hilo del juego
std::atomic<int> s_where{0};             // donde se cargo la banca: 0 nada, 1 partido, 2 Motor del Editar
// (Editar) banco st024 en la escena del Motor: ver arriba. Raiz del clip sentado = asiento - 0.40 m (como la banca del partido: asiento
// 0.40 m sobre los pies de las figuras de Konami); con este asiento alto los pies quedan 19 cm sobre el piso del foso (Altura los baja)
constexpr float kEdSeatY = 0.13f - 0.40f, kEdHipZ = 3.84f, kEdYaw = 180.0f, kEdPitchZ = 1.0f, kEdStep = 0.60f;
bool s_grassMine = false; float s_grassWas[2] = {};      // el pasto del editor lo prendio el laboratorio (se devuelve como estaba)
float s_grassR = 14.0f;

// lo de la interfaz (Present)
struct LabPj { int slot = -1; float seat[4] = {}; float off[4] = {}; };   // off: x costado, y altura, z adelante (m), w giro (grados)
LabPj s_pj[kLabMax]; int s_n = 0;
int s_side = 0, s_kind = 0, s_count = 7;
char s_status[200] = "";
bool s_showNames = true, s_showSkel = false, s_showAxes = false, s_showVals = true;
char s_query[64] = ""; std::vector<int> s_filt; std::string s_filtKey = "#"; bool s_onlySit = true;
bool s_f8Was = false;

inline bool Foreground() { HWND h = OverlayHwnd(); return h && GetForegroundWindow() == h; }
const char* ShortClip(const char* f, char* out, size_t n) {
    const char* s = f; if (!strncmp(s, "dml_md_", 7)) s += 7; else if (!strncmp(s, "dml_", 4)) s += 4;
    snprintf(out, n, "%s", s); char* d = strstr(out, ".gani"); if (d) *d = 0; return out;
}
bool IMatch(const char* f, const char* q) {
    if (!q[0]) return true;
    for (; *f; f++) { const char* a = f; const char* b = q; while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; } if (!*b) return true; }
    return false;
}
int ClipOf(const char* file) { const int n = MotorClipCount(); for (int i = 0; i < n; i++) if (!strcmp(MotorClipFile(i), file)) return i; return -1; }

// asientos de fabrica: los clips de sentado de la banca (banca.cpp kClipDef, los que dan la vuelta sin salto)
const char* const kSitClips[3] = { "dml_md_lockerSpeech_001_sit_002.gani", "dml_md_lockerSpeech_001_sit_003.gani", "dml_md_lockerSpeech_001_sit_005.gani" };

// ---- camara para dibujar encima de la escena
struct Cam { float pos[3], f[3], r[3], u[3], th, asp; bool ok; };
bool Inv4(const double* m, double* o) {
    double a[4][8]; for (int r = 0; r < 4; r++) for (int c = 0; c < 8; c++) a[r][c] = c < 4 ? m[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
    for (int c = 0; c < 4; c++) { int pv = c; for (int r = c + 1; r < 4; r++) if (fabs(a[r][c]) > fabs(a[pv][c])) pv = r;
        if (fabs(a[pv][c]) < 1e-12) return false; if (pv != c) for (int k = 0; k < 8; k++) std::swap(a[pv][k], a[c][k]);
        const double d = a[c][c]; for (int k = 0; k < 8; k++) a[c][k] /= d;
        for (int r = 0; r < 4; r++) if (r != c) { const double f = a[r][c]; if (f != 0.0) for (int k = 0; k < 8; k++) a[r][k] -= f * a[c][k]; } }
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) o[r * 4 + c] = a[r][c + 4];
    return true;
}
bool CamFromScene(Cam& c) {      // igual que motorgrid.cpp CamFromScene
    std::vector<float> S; if (!NamedCBRead("SceneCB", S) || S.size() < 16) return false;
    double M[16], I[16]; for (int i = 0; i < 16; i++) M[i] = S[i];
    if (!Inv4(M, I)) return false;
    auto un = [&](double x, double y, double z, double w, double* o) { double h[4]; for (int r = 0; r < 4; r++) h[r] = I[r * 4] * x + I[r * 4 + 1] * y + I[r * 4 + 2] * z + I[r * 4 + 3] * w;
        if (fabs(h[3]) < 1e-12) return false; for (int k = 0; k < 3; k++) o[k] = h[k] / h[3]; return true; };
    double C[3], p0[3], pr[3], pu[3];
    if (!un(0, 0, 1, 0, C) || !un(0, 0, 0.5, 1, p0) || !un(1, 0, 0.5, 1, pr) || !un(0, 1, 0.5, 1, pu)) return false;
    double f[3] = { p0[0] - C[0], p0[1] - C[1], p0[2] - C[2] }; const double lf = sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]); if (lf < 1e-6 || lf > 1e6) return false;
    for (double& v : f) v /= lf;
    double vr[3] = { pr[0] - C[0], pr[1] - C[1], pr[2] - C[2] }, vu[3] = { pu[0] - C[0], pu[1] - C[1], pu[2] - C[2] };
    const double fr = vr[0] * f[0] + vr[1] * f[1] + vr[2] * f[2], fu = vu[0] * f[0] + vu[1] * f[1] + vu[2] * f[2]; if (fr <= 1e-6 || fu <= 1e-6) return false;
    double r[3], u[3]; for (int k = 0; k < 3; k++) { r[k] = vr[k] / fr - f[k]; u[k] = vu[k] / fu - f[k]; }
    const double tx = sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]), ty = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); if (tx < 1e-5 || ty < 1e-5 || ty > 50.0) return false;
    for (int k = 0; k < 3; k++) { c.pos[k] = (float)C[k]; c.f[k] = (float)f[k]; c.r[k] = (float)(r[k] / tx); c.u[k] = (float)(u[k] / ty); }
    c.th = (float)ty; c.asp = (float)(tx / ty);
    return c.asp > 0.3f && c.asp < 6.0f;
}
bool CamFromFree(Cam& c) {
    float yaw = 0, pitch = 0; if (!FreeCamActive() || !FreeCamGetPose(c.pos, &yaw, &pitch)) return false;
    const float cp = cosf(pitch), sp = sinf(pitch);
    c.f[0] = cp * sinf(yaw); c.f[1] = sp; c.f[2] = cp * cosf(yaw);
    c.r[0] = -c.f[2]; c.r[1] = 0; c.r[2] = c.f[0]; { const float l = sqrtf(c.r[0] * c.r[0] + c.r[2] * c.r[2]); if (l < 1e-5f) return false; c.r[0] /= l; c.r[2] /= l; }
    c.u[0] = c.r[1] * c.f[2] - c.r[2] * c.f[1]; c.u[1] = c.r[2] * c.f[0] - c.r[0] * c.f[2]; c.u[2] = c.r[0] * c.f[1] - c.r[1] * c.f[0];
    c.th = tanf(0.5f * std::clamp(FreeCamFovNow(), 0.02f, 2.4f));
    c.asp = (g_bbW > 0 && g_bbH > 0) ? std::clamp((float)g_bbW / (float)g_bbH, 0.5f, 5.0f) : 16.0f / 9.0f;
    return true;
}
Cam s_cam{}; int s_camSrc = 0;   // 1 escena, 2 camara libre
void CamUpdate() {
    Cam a{}, b{}; const bool sa = CamFromScene(a), sb = CamFromFree(b);
    if (sa && sb) { const float d = sqrtf((a.pos[0] - b.pos[0]) * (a.pos[0] - b.pos[0]) + (a.pos[1] - b.pos[1]) * (a.pos[1] - b.pos[1]) + (a.pos[2] - b.pos[2]) * (a.pos[2] - b.pos[2]));
        if (d > 0.5f) { s_cam = b; s_camSrc = 2; s_cam.ok = true; return; } }      // el SceneCB leido es de otra pasada (sombras, reflejo)
    if (sa) { s_cam = a; s_camSrc = 1; s_cam.ok = true; return; }
    if (sb) { s_cam = b; s_camSrc = 2; s_cam.ok = true; return; }
    s_cam.ok = false; s_camSrc = 0;
}
bool Proj(const float* w, ImVec2& o) {
    if (!s_cam.ok) return false; const Cam& c = s_cam; const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float d[3] = { w[0] - c.pos[0], w[1] - c.pos[1], w[2] - c.pos[2] };
    const float z = d[0] * c.f[0] + d[1] * c.f[1] + d[2] * c.f[2]; if (z < 0.05f) return false;
    const float x = d[0] * c.r[0] + d[1] * c.r[1] + d[2] * c.r[2], y = d[0] * c.u[0] + d[1] * c.u[1] + d[2] * c.u[2];
    const float u = 0.5f + 0.5f * x / (z * c.th * c.asp), v = 0.5f - 0.5f * y / (z * c.th);
    o = ImVec2(u * ds.x, v * ds.y); return u > -0.5f && u < 1.5f && v > -0.5f && v < 1.5f;
}

// ---- cargar / sacar
void Apply(int k) {      // lugar del jugador k = asiento + desplazamiento (en el marco del asiento: adelante = hacia donde mira)
    const LabPj& L = s_pj[k]; if (L.slot < 0) return;
    const float a = L.seat[3] * 0.017453292f, fx_ = sinf(a), fz = cosf(a);      // adelante = (sen giro, 0, cos giro), como RootYaw
    const float p[3] = { L.seat[0] + L.off[2] * fx_ + L.off[0] * fz, L.seat[1] + L.off[1], L.seat[2] + L.off[2] * fz - L.off[0] * fx_ };
    MotorPjSet(L.slot, p, L.seat[3] + L.off[3], 1.0f);
}
void GrassOff() {
    g_grassEdCutZ.store(1.0e9f);
    if (!s_grassMine) return; s_grassMine = false;
    std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.live[393][0] = s_grassWas[0]; g_cfg.live[393][1] = s_grassWas[1];
}
void GrassOn() {
    std::lock_guard<std::mutex> lk(g_cfgMutex);
    if (!s_grassMine) { s_grassWas[0] = g_cfg.live[393][0]; s_grassWas[1] = g_cfg.live[393][1]; s_grassMine = true; }
    g_cfg.live[393][0] = 1.0f; g_cfg.live[393][1] = s_grassR;        // x393 nunca se guarda (kDebugParams)
    g_grassEdCutZ.store(kEdPitchZ);
}
void RemoveAll(const char* why) {
    for (int k = 0; k < s_n; k++) if (s_pj[k].slot >= 0) MotorPjRemove(s_pj[k].slot);
    s_n = 0; g_labHideSide.store(0);
    if (s_where.exchange(0) == 2) GrassOff();
    if (why) { snprintf(s_status, sizeof s_status, "%s", why); Log("motor (laboratorio): %s", why); }
}
int EdSpots(int want, float (*out)[4]) {      // del medio hacia afuera, cada 0.60 m (el banco tiene 11.5 m: entran los 16)
    float xs[kLabMax]; want = std::clamp(want, 1, kLabMax);
    for (int k = 0; k < want; k++) xs[k] = (k & 1 ? -1.0f : 1.0f) * (0.5f + (float)(k / 2)) * kEdStep - (want & 1 ? 0.5f * kEdStep : 0.0f);
    std::sort(xs, xs + want);       // de izquierda a derecha (la tabla y los numeros siguen el banco)
    for (int k = 0; k < want; k++) { out[k][0] = xs[k]; out[k][1] = kEdSeatY; out[k][2] = kEdHipZ; out[k][3] = kEdYaw; }
    return want;
}
void LoadBenchEd() {
    float sp[kLabMax][4]; const int n = EdSpots(std::clamp(s_count, 1, kLabMax), sp);
    RemoveAll(nullptr);
    int made = 0;
    for (int k = 0; k < n; k++) {
        const int slot = MotorPjAdd(s_kind == 1 ? 2 : 1, 0);      // 1 = jugador base (generico), 2 = como el del visor del Editar
        if (slot < 0) break;
        LabPj& L = s_pj[s_n++]; L = LabPj{}; L.slot = slot; memcpy(L.seat, sp[k], sizeof L.seat);
        Apply(s_n - 1);
        MotorPjAnimSet(slot, ClipOf(kSitClips[k % 3]), 1.0f, false);
        MotorPjSeek(slot, 0.83f * (float)k);
        made++;
    }
    s_where.store(2); GrassOn();
    MotorPjSelect(s_n ? s_pj[0].slot : -1);
    snprintf(s_status, sizeof s_status, "Cargando %d jugadores en el banco del Motor...", made);
    Log("motor (laboratorio): banca en el Motor del Editar: %d jugadores %s, pasto del editor a %.0f m cortado en z %.2f", made, s_kind == 1 ? "(como el del Editar)" : "(jugador base generico)", s_grassR, kEdPitchZ);
}
void LoadBench() {
    if (!BancaLabInMatch() && MotorPageActive()) { LoadBenchEd(); return; }
    float sp[kLabMax][4]; char why[200] = "";
    const int want = std::clamp(s_count, 1, kLabMax);
    const int n = BancaLabSpots(s_side, want, sp, kLabMax, why, sizeof why);
    if (n <= 0) { snprintf(s_status, sizeof s_status, "%s", why[0] ? why : "no hay asientos"); Log("motor (laboratorio): sin asientos: %s", s_status); return; }
    uint32_t ids[kLabMax] = {}; char nm[kLabMax][40] = {}; int nr = 0;
    if (s_kind == 1) { nr = BancaLabRealSubs(s_side, ids, nm, kLabMax); if (nr <= 0) { snprintf(s_status, sizeof s_status, "No encontre los suplentes de ese equipo en este partido"); return; } }
    RemoveAll(nullptr);
    MotorLightSetReq(0);                         // los juegos de luces del Motor son del Editar
    s_session.store(true);
    const int cnt = s_kind == 1 ? std::min(n, nr) : n;
    int made = 0;
    for (int k = 0; k < cnt; k++) {
        const int slot = s_kind == 1 ? MotorPjAdd(0, ids[k]) : MotorPjAdd(1, 0);
        if (slot < 0) break;
        LabPj& L = s_pj[s_n++]; L = LabPj{}; L.slot = slot; memcpy(L.seat, sp[k], sizeof L.seat);
        Apply(s_n - 1);
        const int c = ClipOf(kSitClips[k % 3]);
        MotorPjAnimSet(slot, c, 1.0f, false);
        MotorPjSeek(slot, 0.83f * (float)k);    // cada uno arranca en otro punto del clip (no se mueven juntos); se acota al largo
        made++;
    }
    g_labHideSide.store(1 << s_side); s_where.store(1);
    MotorPjSelect(s_n ? s_pj[0].slot : -1);
    snprintf(s_status, sizeof s_status, "Cargando %d jugadores en el banco %s...", made, s_side ? "visitante" : "local");
    Log("motor (laboratorio): banca %s: %d asientos, %d jugadores %s", s_side ? "visitante" : "local", n, made, s_kind == 1 ? "(suplentes reales)" : "(jugador base del Editar)");
}
void CamToBench() {
    if (s_n <= 0) return;
    float c[3] = { 0, 0, 0 }; for (int k = 0; k < s_n; k++) for (int i = 0; i < 3; i++) c[i] += s_pj[k].seat[i] / (float)s_n;
    const float a = s_pj[0].seat[3] * 0.017453292f, fx_ = sinf(a), fz = cosf(a);
    const float span = fabsf(s_pj[s_n - 1].seat[0] - s_pj[0].seat[0]) + fabsf(s_pj[s_n - 1].seat[2] - s_pj[0].seat[2]);
    const float dist = std::clamp(1.6f + span * 0.75f, 2.2f, 9.0f);
    const float pos[3] = { c[0] + fx_ * dist, c[1] + 1.45f, c[2] + fz * dist };
    const float yaw = atan2f(-fx_, -fz), pitch = atan2f(c[1] + 0.85f - pos[1], dist);
    FreeCamSetPose(pos, yaw, pitch);             // prende la camara libre si estaba apagada
}

// ---- lista de clips filtrada
void Filter() {
    const std::string key = std::string(s_query) + (s_onlySit ? "|s" : "|t");
    if (key == s_filtKey) return; s_filtKey = key; s_filt.clear();
    const int n = MotorClipCount();
    for (int i = 0; i < n; i++) { const char* f = MotorClipFile(i); if (s_onlySit && !strstr(f, "sit") && !strstr(f, "bench") && !strstr(f, "banca")) continue; if (IMatch(f, s_query)) s_filt.push_back(i); }
}

// ---- dibujo encima de los modelos
void DrawOverlay(const MotorPjInfo& PI) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList(); const ImVec2 ds = ImGui::GetIO().DisplaySize; const float k = std::max(1.0f, ds.y / 1080.0f);
    static MotorEdInfo E;
    for (int q = 0; q < s_n; q++) {
        const int i = s_pj[q].slot; if (i < 0 || PI.st[i] != 3 || PI.hidden[i]) continue;
        const bool sel = PI.sel == i;
        const bool have = MotorEdInfoGet(i, &E);
        float head[3]; bool hok = false;
        if (have) for (int b = 0; b < E.n; b++) if (E.lab[b] == 6) { memcpy(head, E.pos[b], 12); hok = true; }
        if (!hok) { float w[3]; if (!MotorPjWorld(i, w)) continue; head[0] = w[0]; head[1] = w[1] + 1.25f; head[2] = w[2]; }
        if (have && s_showSkel) {
            for (int b = 0; b < E.n; b++) { const int pb = E.par[b]; if (pb < 0 || pb >= E.n) continue;
                ImVec2 a, c; if (!Proj(E.pos[b], a) || !Proj(E.pos[pb], c)) continue;
                const int lb = E.lab[b]; const ImU32 col = (lb >= 7 && lb <= 10) || lb == 15 || lb == 16 || lb == 17 ? IM_COL32(90, 170, 255, 230) : (lb >= 11 && lb <= 14) || lb >= 18 ? IM_COL32(255, 120, 90, 230) : IM_COL32(240, 240, 240, 220);
                dl->AddLine(a, c, col, (sel ? 2.4f : 1.5f) * k); dl->AddCircleFilled(a, 2.4f * k, col); }
        }
        if (s_showAxes) { float w[3]; ImVec2 o, x, z;
            if (MotorPjWorld(i, w)) { float p[3], yw, sc; MotorPjGet(i, p, &yw, &sc); const float a = yw * 0.017453292f;
                const float px[3] = { w[0] + 0.3f * cosf(a), w[1], w[2] - 0.3f * sinf(a) }, pz[3] = { w[0] + 0.3f * sinf(a), w[1], w[2] + 0.3f * cosf(a) };
                if (Proj(w, o) && Proj(px, x) && Proj(pz, z)) { dl->AddLine(o, x, IM_COL32(234, 72, 84, 255), 2.0f * k); dl->AddLine(o, z, IM_COL32(64, 140, 240, 255), 2.0f * k); dl->AddCircleFilled(o, 3.0f * k, IM_COL32(126, 196, 46, 255)); } } }
        if (s_showNames) {
            const float top[3] = { head[0], head[1] + 0.22f, head[2] }; ImVec2 s; if (!Proj(top, s)) continue;
            char cn[96]; ShortClip(PI.clip[i] >= 0 ? MotorClipFile(PI.clip[i]) : MotorClipFile(MotorClipIdle()), cn, sizeof cn);
            char l2[96]; snprintf(l2, sizeof l2, "%d / %d  x%.2f%s", (int)(PI.curT[i] * 30.0f + 0.5f), (int)(PI.lenT[i] * 30.0f + 0.5f), PI.speed[i], PI.paused[i] ? "  pausa" : PI.clipBad[i] ? "  NO CARGO" : !PI.clipOn[i] ? "  cargando" : "");
            char l0[64]; snprintf(l0, sizeof l0, "%d  %s", q + 1, PI.name[i]);
            const ImVec2 t0 = ImGui::CalcTextSize(l0), t1 = ImGui::CalcTextSize(cn), t2 = ImGui::CalcTextSize(l2);
            const float w = std::max(t0.x, std::max(t1.x, t2.x)) + 12.0f * k, h = t0.y * 3.0f + 10.0f * k;
            const ImVec2 a(s.x - w * 0.5f, s.y - h), b(s.x + w * 0.5f, s.y);
            dl->AddRectFilled(a, b, sel ? IM_COL32(40, 70, 120, 215) : IM_COL32(14, 16, 20, 190), 5.0f * k);
            if (sel) dl->AddRect(a, b, IM_COL32(240, 138, 36, 255), 5.0f * k, 0, 1.5f * k);
            dl->AddText(ImVec2(a.x + 6 * k, a.y + 4 * k), IM_COL32(150, 156, 166, 255), l0);
            dl->AddText(ImVec2(a.x + 6 * k, a.y + 4 * k + t0.y), IM_COL32(240, 242, 246, 255), cn);
            dl->AddText(ImVec2(a.x + 6 * k, a.y + 4 * k + t0.y * 2.0f), IM_COL32(240, 190, 110, 255), l2);
            dl->AddLine(ImVec2(s.x, s.y), ImVec2(s.x, s.y + 6 * k), IM_COL32(14, 16, 20, 190), 2.0f * k);
        }
    }
}

// valores del elegido (texto que se puede copiar para pasarlo)
void ValuesText(const MotorPjInfo& PI, int i, char* out, size_t n) {
    out[0] = 0; if (i < 0 || i >= kMotorPjMax || PI.st[i] != 3) return;
    static MotorEdInfo E; const bool have = MotorEdInfoGet(i, &E);
    float w[3] = {}, p[3] = {}, yw = 0, sc = 1; MotorPjWorld(i, w); MotorPjGet(i, p, &yw, &sc);
    char cn[96]; ShortClip(PI.clip[i] >= 0 ? MotorClipFile(PI.clip[i]) : MotorClipFile(MotorClipIdle()), cn, sizeof cn);
    static const char* const kMode[3] = { "bucle", "una vez", "ida y vuelta" };
    size_t o = 0; auto add = [&](const char* fmt, auto... a) { if (o < n) o += snprintf(out + o, n - o, fmt, a...); };
    add("Clip: %s\n", cn);
    add("Tiempo: %.3f / %.3f s   cuadro %d / %d (30 por s)\n", PI.curT[i], PI.lenT[i], (int)(PI.curT[i] * 30.0f + 0.5f), (int)(PI.lenT[i] * 30.0f + 0.5f));
    add("Velocidad x%.2f, %s, modo %s\n", PI.speed[i], PI.paused[i] ? "en pausa" : "corriendo", kMode[std::clamp(PI.mode[i], 0, 2)]);
    add("Lugar puesto: (%.3f, %.3f, %.3f) m, giro %.1f grados, escala %.2f\n", p[0], p[1], p[2], yw, sc);
    add("Raiz animada: (%.3f, %.3f, %.3f) m\n", w[0], w[1], w[2]);
    if (have) {
        float hip = NAN, chest = NAN, hd = NAN, hand[2] = { NAN, NAN }, foot[2] = { NAN, NAN };
        for (int b = 0; b < E.n; b++) { const float y = (E.pos[b][1] - p[1]) * 100.0f;
            switch (E.lab[b]) { case 2: hip = y; break; case 4: chest = y; break; case 6: hd = y; break; case 10: hand[0] = y; break; case 14: hand[1] = y; break; case 17: foot[0] = y; break; case 20: foot[1] = y; break; } }
        add("Alturas sobre el lugar puesto (cm): cadera %.1f, pecho %.1f, cabeza %.1f\n", hip, chest, hd);
        add("  manos izq %.1f / der %.1f, pies izq %.1f / der %.1f\n", hand[0], hand[1], foot[0], foot[1]);
        add("Huesos: %d, tamano del esqueleto %.3f\n", E.n, E.sc);
    }
    add("Camara para dibujar: %s, imagen %.0f cuadros por s\n", s_camSrc == 1 ? "la de la escena" : s_camSrc == 2 ? "la camara libre" : "ninguna", ImGui::GetIO().Framerate);
}

void Window() {
    static MotorPjInfo PI; MotorPjInfoGet(&PI);
    // los del laboratorio que ya no existen (fallaron o se sacaron desde otro lado) salen de la lista
    { int w = 0; for (int q = 0; q < s_n; q++) { const int i = s_pj[q].slot; if (i >= 0 && (PI.st[i] == 0 || PI.st[i] == 4)) continue; s_pj[w++] = s_pj[q]; } s_n = w; }
    const bool inMatch = BancaLabInMatch(), inEd = !inMatch && MotorPageActive();
    const float K = std::max(1.0f, ImGui::GetIO().DisplaySize.y / 1080.0f);
    ImGui::SetNextWindowSize(ImVec2(460 * K, 780 * K), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 480 * K, 60 * K), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Laboratorio de animaciones", &open, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); if (!open) s_win.store(false); return; }
    if (!open) s_win.store(false);
    // --- banca
    ImGui::TextDisabled("%s", inMatch ? "Partido: el reloj del juego corre" : inEd ? "Motor del Editar: banco del estadio en la escena del Motor" : "Abri el Motor en tiempo real del Editar (o entra a un partido) para usar la banca");
    ImGui::SeparatorText("Banca");
    if (inEd) { ImGui::RadioButton("Genericos del Editar", &s_kind, 0); ImGui::SameLine(); ImGui::RadioButton("Como el del Editar", &s_kind, 1); }
    else { ImGui::RadioButton("Banco local", &s_side, 0); ImGui::SameLine(); ImGui::RadioButton("Banco visitante", &s_side, 1);
        ImGui::RadioButton("Genericos del Editar", &s_kind, 0); ImGui::SameLine(); ImGui::RadioButton("Suplentes reales", &s_kind, 1); }
    ImGui::SetNextItemWidth(160 * K); ImGui::SliderInt("Jugadores", &s_count, 1, kLabMax);
    if (inEd) { ImGui::SetNextItemWidth(160 * K); if (ImGui::SliderFloat("Pasto real (m)", &s_grassR, 4.0f, 40.0f, "%.0f") && s_where.load() == 2) GrassOn(); }
    ImGui::BeginDisabled(!inMatch && !inEd); if (ImGui::Button(s_n ? "Volver a cargar la banca" : "Cargar la banca", ImVec2(200 * K, 0))) LoadBench(); ImGui::EndDisabled();
    ImGui::SameLine(); if (ImGui::Button("Sacar todos", ImVec2(110 * K, 0))) { RemoveAll("Jugadores del laboratorio sacados"); s_session.store(false); }
    if (inEd) ImGui::TextDisabled("Los asientos son los del banco st024 de la escena del Motor (escena\\Default.fpkd)");
    if (ImGui::Button("Camara a la banca", ImVec2(200 * K, 0))) CamToBench();
    ImGui::SameLine(); if (ImGui::Button(FreeCamActive() ? "Apagar camara libre" : "Prender camara libre", ImVec2(160 * K, 0))) { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.live[344][0] = FreeCamActive() ? 0.0f : 1.0f; }
    if (s_status[0]) ImGui::TextWrapped("%s", s_status);
    if (PI.msg[0]) ImGui::TextWrapped("%s", PI.msg);
    // --- jugadores
    ImGui::SeparatorText("Jugadores");
    if (ImGui::BeginTable("labpj", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImVec2(0, 170 * K))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 22 * K); ImGui::TableSetupColumn("Animacion"); ImGui::TableSetupColumn("Cuadro", ImGuiTableColumnFlags_WidthFixed, 74 * K); ImGui::TableSetupColumn("Vel", ImGuiTableColumnFlags_WidthFixed, 40 * K);
        ImGui::TableHeadersRow();
        for (int q = 0; q < s_n; q++) { const int i = s_pj[q].slot; ImGui::TableNextRow(); ImGui::TableNextColumn();
            char lb[16]; snprintf(lb, sizeof lb, "%d##r%d", q + 1, q);
            if (ImGui::Selectable(lb, PI.sel == i, ImGuiSelectableFlags_SpanAllColumns)) MotorPjSelect(i);
            ImGui::TableNextColumn(); char cn[96]; ShortClip(PI.clip[i] >= 0 ? MotorClipFile(PI.clip[i]) : MotorClipFile(MotorClipIdle()), cn, sizeof cn);
            ImGui::TextUnformatted(PI.st[i] != 3 ? "cargando..." : cn);
            ImGui::TableNextColumn(); ImGui::Text("%d / %d", (int)(PI.curT[i] * 30.0f + 0.5f), (int)(PI.lenT[i] * 30.0f + 0.5f));
            ImGui::TableNextColumn(); ImGui::Text("%.2f", PI.speed[i]); }
        ImGui::EndTable();
    }
    int si = -1; for (int q = 0; q < s_n; q++) if (s_pj[q].slot == PI.sel) si = q;
    if (si >= 0 && PI.st[PI.sel] == 3) {
        const int i = PI.sel; LabPj& L = s_pj[si];
        ImGui::SeparatorText("Animacion del elegido");
        float sp = PI.speed[i]; bool pa = PI.paused[i];
        ImGui::SetNextItemWidth(200 * K); if (ImGui::SliderFloat("Velocidad", &sp, 0.0f, 2.0f, "x%.2f")) MotorPjAnimSet(i, PI.clip[i], sp, pa);
        ImGui::SameLine(); if (ImGui::Checkbox("Pausa", &pa)) MotorPjAnimSet(i, PI.clip[i], sp, pa);
        int fr = (int)(PI.curT[i] * 30.0f + 0.5f); const int frN = std::max(1, (int)(PI.lenT[i] * 30.0f + 0.5f));
        ImGui::SetNextItemWidth(260 * K); if (ImGui::SliderInt("Cuadro", &fr, 0, frN)) { MotorPjAnimSet(i, PI.clip[i], sp, true); MotorPjSeek(i, (float)fr / 30.0f); }
        if (ImGui::Button("< cuadro")) { MotorPjAnimSet(i, PI.clip[i], sp, true); MotorPjSeek(i, std::max(0.0f, (float)(fr - 1) / 30.0f)); }
        ImGui::SameLine(); if (ImGui::Button("cuadro >")) { MotorPjAnimSet(i, PI.clip[i], sp, true); MotorPjSeek(i, (float)(fr + 1) / 30.0f); }
        ImGui::SameLine(); if (ImGui::Button("Desde el principio")) MotorPjRestart(i);
        int md = std::clamp(PI.mode[i], 0, 2); ImGui::RadioButton("Bucle", &md, 0); ImGui::SameLine(); ImGui::RadioButton("Una vez", &md, 1); ImGui::SameLine(); ImGui::RadioButton("Ida y vuelta", &md, 2);
        if (md != PI.mode[i]) MotorPjModeSet(i, md);
        {   int fc = PI.face[i]; const char* cur = fc >= 0 ? MotorFaceName(fc) : "Neutra del juego";
            ImGui::SetNextItemWidth(260 * K);
            if (ImGui::BeginCombo("Cara", cur)) { if (ImGui::Selectable("Neutra del juego", fc < 0)) MotorPjFaceSet(i, -1, 1.0f, true);
                for (int f = 0; f < MotorFaceCount(); f++) if (ImGui::Selectable(MotorFaceName(f), fc == f)) MotorPjFaceSet(i, f, 1.0f, true);
                ImGui::EndCombo(); } }
        ImGui::SeparatorText("Lugar en el banco");
        bool ch = false; float cm[3] = { L.off[0] * 100.0f, L.off[1] * 100.0f, L.off[2] * 100.0f };
        ImGui::SetNextItemWidth(220 * K); ch |= ImGui::SliderFloat("Altura (cm)", &cm[1], -30.0f, 30.0f, "%.1f");
        ImGui::SetNextItemWidth(220 * K); ch |= ImGui::SliderFloat("Adelante / atras (cm)", &cm[2], -40.0f, 40.0f, "%.1f");
        ImGui::SetNextItemWidth(220 * K); ch |= ImGui::SliderFloat("Costado (cm)", &cm[0], -40.0f, 40.0f, "%.1f");
        ImGui::SetNextItemWidth(220 * K); ch |= ImGui::SliderFloat("Giro (grados)", &L.off[3], -180.0f, 180.0f, "%.1f");
        if (ch) { for (int c = 0; c < 3; c++) L.off[c] = cm[c] * 0.01f; Apply(si); }
        if (ImGui::Button("Lugar del asiento")) { memset(L.off, 0, sizeof L.off); Apply(si); }
        ImGui::SameLine(); { bool vis = !PI.hidden[i]; if (ImGui::Checkbox("Se ve", &vis)) MotorPjShow(i, vis); }
        // --- clips
        ImGui::SeparatorText("Cambiar la animacion");
        ImGui::SetNextItemWidth(250 * K); ImGui::InputTextWithHint("##q", "buscar (sit, bench, idle, talk...)", s_query, sizeof s_query);
        ImGui::SameLine(); ImGui::Checkbox("Solo sentados", &s_onlySit);
        Filter();
        ImGui::TextDisabled("%d animaciones", (int)s_filt.size());
        if (ImGui::BeginChild("clips", ImVec2(0, 190 * K), ImGuiChildFlags_Borders)) {
            ImGuiListClipper cl; cl.Begin((int)s_filt.size());
            while (cl.Step()) for (int r = cl.DisplayStart; r < cl.DisplayEnd; r++) { const int c = s_filt[r]; char cn[112]; ShortClip(MotorClipFile(c), cn, sizeof cn);
                ImGui::PushID(c); if (ImGui::Selectable(cn, PI.clip[i] == c)) MotorPjAnimSet(i, c, sp, false); ImGui::PopID(); }
        }
        ImGui::EndChild();
        if (ImGui::Button("Esta animacion a todos")) for (int q = 0; q < s_n; q++) MotorPjAnimSet(s_pj[q].slot, PI.clip[i], PI.speed[s_pj[q].slot], false);
        ImGui::SameLine(); if (ImGui::Button("Todos desde el principio")) for (int q = 0; q < s_n; q++) MotorPjRestart(s_pj[q].slot);
    } else if (s_n) ImGui::TextDisabled("Elegi un jugador de la lista.");
    // --- imagen: las vistas del Motor en tiempo real (fila 429, pes_vista_motor.hlsli; las mismas del Motor y de Herramientas)
    ImGui::SeparatorText("Vistas del Motor");
    {   static const char* const kVm[] = { "Normal", "Luz difusa", "Luz especular", "Normales", "Normales de mundo", "Albedo (sin luz)", "Profundidad", "Oclusion", "Sombra del sol", "Solo luz (sin texturas)", "Material" };
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        int vm = (int)(g_cfg.live[429][0] + 0.5f); if (vm < 0 || vm > 10) vm = 0;
        ImGui::SetNextItemWidth(220 * K);
        if (ImGui::BeginCombo("Vista", kVm[vm])) { for (int v = 0; v <= 10; v++) if (ImGui::Selectable(kVm[v], v == vm)) g_cfg.live[429][0] = (float)v; ImGui::EndCombo(); }
        ImGui::SameLine(); bool wf = g_cfg.live[429][1] > 0.5f; if (ImGui::Checkbox("Alambre", &wf)) g_cfg.live[429][1] = wf ? 1.0f : 0.0f;
        if (vm > 0) { float ct = g_cfg.live[429][2]; ImGui::SetNextItemWidth(220 * K); if (ImGui::SliderFloat("Pantalla partida", &ct, 0.0f, 1.0f, "%.2f")) g_cfg.live[429][2] = ct; }
    }
    // --- depuracion
    ImGui::SeparatorText("En pantalla");
    ImGui::Checkbox("Nombres y cuadros", &s_showNames); ImGui::SameLine(); ImGui::Checkbox("Esqueleto", &s_showSkel); ImGui::SameLine(); ImGui::Checkbox("Raiz", &s_showAxes); ImGui::SameLine(); ImGui::Checkbox("Valores", &s_showVals);
    if (s_showVals && PI.sel >= 0) {
        static char vt[1400]; ValuesText(PI, PI.sel, vt, sizeof vt);
        if (vt[0]) { ImGui::TextUnformatted(vt); if (ImGui::Button("Copiar valores")) { ImGui::SetClipboardText(vt); Log("motor (laboratorio): valores del jugador %d\n%s", PI.sel + 1, vt); } }
    }
    ImGui::End();
}

} // namespace

// reloj del partido (banca.cpp hk_ModeUpd, hilo del juego)
void MotorLabGameTick() {
    const bool on = s_session.load(std::memory_order_relaxed);
    if (!on && !s_hadGame) return;
    MotorPjTick(on, 0);                          // on = false: suelta a todos (DestroyAll)
    s_hadGame = on; s_tickT.store(Now());
}
// el partido termina (banca.cpp hk_HumanDtor, hilo del juego): soltar antes de que se vaya la escena
void MotorLabMatchEnd() {
    if (!s_session.exchange(false) && !s_hadGame) return;
    MotorPjTick(false, 0); s_hadGame = false; g_labHideSide.store(0); if (s_where.load() == 1) s_where.store(0);
    Log("motor (laboratorio): termino el partido: se sueltan los jugadores del laboratorio");
}
// el laboratorio tiene los jugadores del Motor (motorjug.cpp no los toca con el reloj del Editar)
bool MotorLabOwnsPlayers() { return s_session.load(std::memory_order_relaxed) || Now() - s_tickT.load(std::memory_order_relaxed) < 1.0; }
bool MotorLabWantsInput() { return s_win.load(std::memory_order_relaxed); }
// (21/11) algo del laboratorio se dibuja (ventana, o banca cargada en un partido o en el Motor del Editar)
bool MotorLabShowing() { return s_win.load(std::memory_order_relaxed) || s_session.load(std::memory_order_relaxed) || s_where.load(std::memory_order_relaxed) != 0; }
void MotorLabToggle() { s_win.store(!s_win.load()); Log("motor (laboratorio): ventana %s", s_win.load() ? "abierta" : "cerrada"); }
// Present (overlay.cpp)
void MotorLabDraw() {
    const bool f8 = Foreground() && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (f8 && !s_f8Was) MotorLabToggle(); s_f8Was = f8;
    if (!s_win.load() && !s_session.load() && s_where.load() == 0) return;
    const int wh = s_where.load();
    if (wh == 1 && !s_session.load() && s_n) { s_n = 0; g_labHideSide.store(0); s_where.store(0); }      // el partido termino: la lista queda vacia
    if (wh == 2 && !MotorPageActive()) { s_n = 0; s_where.store(0); GrassOff(); Log("motor (laboratorio): se cerro el Motor: banca del Editar vacia"); }   // el Motor suelta a sus jugadores al cerrarse
    CamUpdate();
    if (s_session.load() || s_where.load() == 2) { static MotorPjInfo PI; MotorPjInfoGet(&PI); DrawOverlay(PI); }
    if (s_win.load()) { ImGui::GetIO().MouseDrawCursor = true; Window(); }
}

} // namespace fx
