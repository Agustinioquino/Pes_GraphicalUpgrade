// PES21 FX - expresiones por HUESOS (01/11, Agustin: "abrir la boca deberia ser un mod sider, el shader solo deforma el modelo",
// "me gusta que sea totalmente modificable y 100 % real"). Hasta aca "abrir la boca" (347.x) lo hacia el vertex shader
// girando los vertices que pesan en los huesos que la DLL adivinaba como mandibula (expr.cpp, mascaras por malla): si la
// adivinanza fallaba en una malla (caras convertidas en piezas, barba) aparecia un corte entre mallas. Aca se gira el HUESO:
//
//  * El esqueleto de cada personaje ya se conoce por la musculatura (musculo.cpp: cada cuadro pasa el Eval de las
//    restricciones; la del cuello, dsk_scm, guarda el esqueleto en + 0x10). Huesos: arreglo en esqueleto + 0x28 (paso 0xc0,
//    cantidad en + 0x30); hueso + 0x18 = puntero a la transformacion ACTUAL en el espacio del modelo (48 bytes: escala,
//    cuaternion x y z w, traslacion), + 0x40 = reposo en el espacio del modelo (tools/re/NOTAS.md, "Rig de deformacion").
//  * Los huesos de la cara (skf_*) estan en ese mismo esqueleto y su reposo es el mismo en todas las caras (face.skl comun;
//    /mnt/project-files/face/face_rig_111320.md): se reconocen por la posicion de reposo, no hace falta el nombre.
//  * Cada cuadro, despues del Eval del cuello, la mandibula (skf_jaw) y lo que cuelga de ella (labio de abajo, menton; a la
//    mitad las comisuras y las mejillas bajas) se giran alrededor de la mandibula actual, sobre el eje X de la cabeza
//    actual, el angulo del slider (0.40 rad x 347.x, igual que hacia el shader). Como se escribe la transformacion del
//    hueso, TODAS las mallas que pesan en el (cara, barba, boca 3D, dientes, lengua, caras en piezas) se mueven juntas y
//    parejo, y el shader ya no gira nada (350.y = 1 le avisa: PesJawApply salta el giro).
//  * Diagnostico: cuanto gira la mandibula respecto de la cabeza en el Eval del cuello (min / max en 10 s): si se mueve
//    mientras el jugador habla, la animacion facial del juego ya paso antes de este punto y lo que se escribe aca queda.
//
//   350 (ini): x boca por huesos (1 = si; 0 = por shader como antes), y = lo escribe la DLL (1 = activo: esqueletos con la
//   mandibula encontrada en el ultimo segundo), z / w libres (proximo: pose facial propia: labios, cejas, parpados).
#include "core.h"
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace fx {

namespace {
struct Xf { float s[4], q[4], t[4]; };
// (02/11) antes de leer se consulta la pagina (VirtualQuery): el volcado prueba valores que no son punteros y cada lectura
// mala, aunque la atrape el SEH, queda anotada como excepcion en el registro de cierres (6 en el primer partido)
static bool Readable(const void* p, size_t n) {
    if (!p || (uintptr_t)p < 0x10000 || (uintptr_t)p > 0x00007fffffffffffull) return false;
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    if (!(mi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static bool SafeCopy(void* dst, const void* src, size_t n) {
    if (!Readable(src, n)) return false;
    __try { memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool ReadXf(const void* p, Xf& x) {
    if (!p || !SafeCopy(&x, p, sizeof x)) return false;
    float n = x.q[0] * x.q[0] + x.q[1] * x.q[1] + x.q[2] * x.q[2] + x.q[3] * x.q[3];
    if (!(n > 0.81f && n < 1.21f)) return false;
    for (int k = 0; k < 3; k++) if (!(x.s[k] > 0.3f && x.s[k] < 3.0f) || !(fabsf(x.t[k]) < 3.0f)) return false;
    float r = 1.0f / sqrtf(n); for (int k = 0; k < 4; k++) x.q[k] *= r;
    return true;
}
static bool ReadPtr(const void* p, void*& out) { out = nullptr; return SafeCopy(&out, p, sizeof out) && out; }
static void QRot(const float* q, const float* v, float* o) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float tx = 2.0f * (y * v[2] - z * v[1]), ty = 2.0f * (z * v[0] - x * v[2]), tz = 2.0f * (x * v[1] - y * v[0]);
    o[0] = v[0] + w * tx + (y * tz - z * ty); o[1] = v[1] + w * ty + (z * tx - x * tz); o[2] = v[2] + w * tz + (x * ty - y * tx);
}
static void QMul(const float* a, const float* b, float* o) {   // o = a * b (primero b, despues a)
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
static void QConj(const float* a, float* o) { o[0] = -a[0]; o[1] = -a[1]; o[2] = -a[2]; o[3] = a[3]; }

// huesos de la cara por su reposo en el espacio del modelo (face_rig_111320: cara real de Konami; el .skl es comun).
// jaw = cuanto del giro de la mandibula lleva (1 entero, 0.5 la mitad, 0 nada)
struct BoneDef { const char* name; float x, y, z; float jaw; };
static const BoneDef kDef[] = {
    { "skf_jaw",                 0.0000f, 1.6369f, 0.0795f, 1.0f },
    { "skf_lip_b_l",             0.0185f, 1.6244f, 0.1641f, 1.0f },
    { "skf_lip_b_r",            -0.0185f, 1.6244f, 0.1641f, 1.0f },
    { "skf_lip_b_c",             0.0000f, 1.6220f, 0.1728f, 1.0f },
    { "skf_doublechin",          0.0000f, 1.5816f, 0.1420f, 1.0f },
    { "skf_lip_s_l",             0.0236f, 1.6297f, 0.1615f, 0.5f },
    { "skf_lip_s_r",            -0.0236f, 1.6297f, 0.1615f, 0.5f },
    { "skf_cheek_s_l",           0.0479f, 1.6215f, 0.1396f, 0.5f },
    { "skf_cheek_s_r",          -0.0479f, 1.6215f, 0.1396f, 0.5f },
    { "skf_lip_t_l",             0.0155f, 1.6342f, 0.1720f, 0.0f },
    { "skf_lip_t_c",             0.0000f, 1.6354f, 0.1772f, 0.0f },
    { "skf_lip_t_r",            -0.0155f, 1.6342f, 0.1720f, 0.0f },
    { "skf_lip_volume",          0.0000f, 1.6346f, 0.1772f, 0.0f },
    { "skf_nose",                0.0000f, 1.6863f, 0.1763f, 0.0f },
    { "skf_nosewing_l",          0.0153f, 1.6603f, 0.1765f, 0.0f },
    { "skf_nosewing_r",         -0.0153f, 1.6603f, 0.1765f, 0.0f },
    { "skf_nasolabialfold_t_l",  0.0220f, 1.6493f, 0.1656f, 0.0f },
    { "skf_nasolabialfold_t_r", -0.0220f, 1.6493f, 0.1656f, 0.0f },
    { "skf_cheek_l",             0.0425f, 1.6623f, 0.1526f, 0.0f },
    { "skf_cheek_r",            -0.0425f, 1.6623f, 0.1526f, 0.0f },
    { "skf_orbicularisoculi_b_l",0.0280f, 1.6866f, 0.1518f, 0.0f },
    { "skf_orbicularisoculi_b_r",-0.0280f, 1.6866f, 0.1518f, 0.0f },
    { "skf_eyelid_t_l",          0.0280f, 1.6968f, 0.1380f, 0.0f },
    { "skf_eyelid_t_r",         -0.0280f, 1.6968f, 0.1380f, 0.0f },
    { "skf_eyelid_b_l",          0.0280f, 1.6958f, 0.1380f, 0.0f },
    { "skf_eyelid_b_r",         -0.0280f, 1.6958f, 0.1380f, 0.0f },
    { "skf_brow_i_l",            0.0157f, 1.7129f, 0.1718f, 0.0f },
    { "skf_brow_i_r",           -0.0157f, 1.7129f, 0.1718f, 0.0f },
    { "skf_brow_o_l",            0.0413f, 1.7149f, 0.1573f, 0.0f },
    { "skf_brow_o_r",           -0.0413f, 1.7149f, 0.1573f, 0.0f },
    { "skf_glabella",            0.0000f, 1.7038f, 0.1697f, 0.0f },
    { "sk_head",                 0.0000f, 1.6401f, 0.0543f, 0.0f },
    { "sk_neck",                 0.0000f, 1.5321f, 0.0335f, 0.0f },
};
static const int kND = (int)(sizeof(kDef) / sizeof(kDef[0]));
// (02/11) "mostrar los dientes" (350.w = 1): corrimiento de cada hueso en metros, ejes de la cabeza (x izquierda del jugador)
static const float kTeeth[][3] = {
    { 0, 0, 0 },                    // skf_jaw
    {  0.0005f, -0.0030f, 0.0004f },  // lip_b_l: el labio de abajo baja y se despega un poco de los dientes
    { -0.0005f, -0.0030f, 0.0004f },  // lip_b_r
    {  0.0f,    -0.0035f, 0.0005f },  // lip_b_c
    { 0, 0, 0 },                    // doublechin
    {  0.0015f,  0.0008f, -0.0010f }, // lip_s_l: las comisuras se abren y van atras (mueca)
    { -0.0015f,  0.0008f, -0.0010f }, // lip_s_r
    { 0, 0, 0 }, { 0, 0, 0 },       // cheek_s
    {  0.0003f,  0.0030f, 0.0004f },  // lip_t_l: el labio de arriba sube
    {  0.0f,     0.0035f, 0.0005f },  // lip_t_c
    { -0.0003f,  0.0030f, 0.0004f },  // lip_t_r
    {  0.0f,     0.0030f, 0.0005f },  // lip_volume
    { 0, 0, 0 },                    // nose
    {  0.0003f,  0.0006f, 0.0f },     // nosewing_l: las alas acompanan un poco
    { -0.0003f,  0.0006f, 0.0f },     // nosewing_r
    {  0.0004f,  0.0010f, 0.0f },     // nasolabialfold_t_l
    { -0.0004f,  0.0010f, 0.0f },     // nasolabialfold_t_r
};
static const float* TeethOf(int d) { static const float z[3] = { 0, 0, 0 }; return d < (int)(sizeof(kTeeth) / sizeof(kTeeth[0])) ? kTeeth[d] : z; }
enum { D_JAW = 0, D_HEAD = kND - 2 };

struct Skel {
    const void* skel = nullptr; const char* bones = nullptr; int n = 0;   // esqueleto del cuerpo (clave) y su arreglo
    const void* face = nullptr; const char* fbones = nullptr; int fn = 0;  // (02/11) esqueleto de la CARA: nodo + 0xA0 (49 huesos)
    void* cur[kND] = {};      // puntero a la transformacion actual de cada hueso reconocido (null = no esta)
    int idx[kND] = {};        // indice del hueso en el arreglo del esqueleto
    const char* src[kND] = {};   // (02/11) arreglo de donde salio (cara o cuerpo): para revalidar cada cuadro
    const void* node = nullptr;  // (02/11) SceneCharacterNode con el que se armo
    Xf rest[kND] = {};
    int found = 0, jawFound = 0;
    unsigned lastFrame = 0; int rebuilds = 0; bool logged = false;
    // diagnostico: giro de la mandibula respecto de la cabeza (grados) en la ventana en curso
    float angMin = 1e9f, angMax = -1e9f; int angN = 0;
};
static const int kMaxSkel = 48;
static std::mutex s_mx;
static Skel s_sk[kMaxSkel]; static int s_nSk = 0;
static std::atomic<unsigned> s_frame{0};
static std::atomic<int> s_applied{0}, s_noJaw{0}, s_badWrite{0}, s_calls{0}, s_stale{0};
static std::atomic<unsigned> s_lastActive{0};   // cuadro del ultimo giro aplicado o esqueleto con mandibula visto
static int s_logs = 0;

static float Dist2(const float* a, const BoneDef& d) { float x = a[0] - d.x, y = a[1] - d.y, z = a[2] - d.z; return x * x + y * y + z * z; }

// (02/11, volcado en la PC) hueso del motor (0xc0 bytes): + 0x00 indice, + 0x08 puntero al NOMBRE ("sk_head"), + 0x10 hash,
// + 0x18 transformacion actual, + 0x20 padre, + 0x40 reposo. El SceneCharacterNode tiene + 0x88 esqueleto del cuerpo (175),
// + 0x90 / + 0x98 manos (24), + 0xA0 esqueleto de la CARA (49 huesos, h0 = sk_chest 0, 1.263, 0.014): ahi estan los skf_*.
static bool BoneName(const char* bone, char* out, int n) {
    void* p = nullptr; if (!ReadPtr(bone + 0x08, p)) return false;
    char b[64] = {}; if (!SafeCopy(b, p, 48)) return false;
    int k = 0; while (k < 48 && b[k] >= 0x21 && b[k] < 0x7f) k++;
    if (k < 3 || k >= 48) return false;
    snprintf(out, n, "%.*s", k, b); return true;
}
static bool LooksSkel(const void* q, const char** arr, int* n) {
    void* a = nullptr; int cnt = 0;
    if (!ReadPtr((const char*)q + 0x28, a) || !SafeCopy(&cnt, (const char*)q + 0x30, 4) || cnt < 2 || cnt > 1024) return false;
    Xf r0; if (!ReadXf((const char*)a + 0x40, r0)) return false;
    *arr = (const char*)a; *n = cnt; return true;
}
// reconoce los huesos de un arreglo: por NOMBRE (+ 0x08) y, si no hay nombre, por el reposo (a menos de 4 mm)
static void Match(Skel& S, const char* bones, int n) {
    struct Pair { float d2; int b, d; };
    std::vector<Pair> ps;
    std::vector<Xf> rests((size_t)n);
    for (int b = 0; b < n; b++) {
        const char* bone = bones + (size_t)b * 0xc0;
        if (!ReadXf(bone + 0x40, rests[b])) continue;
        char nm[64];
        if (BoneName(bone, nm, sizeof nm)) {
            for (int d = 0; d < kND; d++) if (!strcmp(nm, kDef[d].name)) { ps.push_back({ -1.0f, b, d }); break; }
            continue;
        }
        for (int d = 0; d < kND; d++) { float d2 = Dist2(rests[b].t, kDef[d]); if (d2 < 0.004f * 0.004f) ps.push_back({ d2, b, d }); }
    }
    std::sort(ps.begin(), ps.end(), [](const Pair& a, const Pair& b) { return a.d2 < b.d2; });
    std::vector<char> usedB((size_t)n, 0);
    for (const Pair& p : ps) {
        if (usedB[p.b] || S.cur[p.d]) continue;
        void* cur = nullptr;
        if (!ReadPtr(bones + (size_t)p.b * 0xc0 + 0x18, cur)) continue;
        Xf c; if (!ReadXf(cur, c)) continue;
        usedB[p.b] = 1;
        S.cur[p.d] = cur; S.rest[p.d] = rests[p.b]; S.idx[p.d] = p.b; S.src[p.d] = bones; S.found++;
    }
}
// arma la tabla de huesos reconocidos de un personaje (una vez, o si cambio el arreglo): primero el esqueleto de la cara
// (nodo + 0xA0, si personajes.cpp ya conoce el nodo de este esqueleto), despues el del cuerpo (sk_head / sk_neck)
static bool Build(Skel& S, const void* skel) {
    S = Skel(); S.skel = skel;
    if (!LooksSkel(skel, &S.bones, &S.n)) return false;
    CharInfo ci[kCharSlots]; bool alive[kCharSlots] = {};
    CharSnapshot(ci, alive);
    void* node = nullptr;
    for (int i = 0; i < kCharSlots; i++) if (alive[i] && ci[i].skel == skel) { node = ci[i].node; break; }
    S.node = node;
    void* face = nullptr;
    if (node && ReadPtr((const char*)node + 0xA0, face) && face != skel && LooksSkel(face, &S.fbones, &S.fn)) { S.face = face; Match(S, S.fbones, S.fn); }
    else { S.face = nullptr; S.fbones = nullptr; S.fn = 0; }
    Match(S, S.bones, S.n);
    S.jawFound = S.cur[D_JAW] && S.cur[D_HEAD] ? 1 : 0;
    return true;
}
static Skel* Get(const void* skel) {   // con s_mx tomado
    for (int i = 0; i < s_nSk; i++) if (s_sk[i].skel == skel) return &s_sk[i];
    int at = s_nSk < kMaxSkel ? s_nSk++ : 0;
    if (s_nSk >= kMaxSkel) {   // lleno: se reemplaza el que hace mas que no se ve
        unsigned oldest = 0xffffffffu;
        for (int i = 0; i < kMaxSkel; i++) if (s_sk[i].lastFrame < oldest) { oldest = s_sk[i].lastFrame; at = i; }
    }
    Build(s_sk[at], skel); s_sk[at].rebuilds = 1;
    return &s_sk[at];
}
// (02/11) ANTES DE ESCRIBIR, cada cuadro: el personaje sigue vivo con el mismo nodo (personajes.cpp), el nodo sigue apuntando
// al mismo esqueleto de la cara, el esqueleto conserva el mismo arreglo y cantidad, y cada hueso usado sigue en su lugar con
// el mismo puntero a la transformacion. Sin esto, al cambiar de jugador o de cara el motor libera el esqueleto de la cara y
// la tabla seguia escribiendo 28 bytes en memoria ya liberada: cierre en el administrador de memoria del juego
// (PES2021.exe+0x3fd6db, dlmalloc, sesion b66dfd3).
static bool StillValid(const Skel& S) {
    if (!S.face || !S.node) return false;
    CharInfo ci[kCharSlots]; bool alive[kCharSlots] = {};
    CharSnapshot(ci, alive);
    bool ok = false;
    for (int i = 0; i < kCharSlots; i++) if (alive[i] && ci[i].skel == S.skel && ci[i].node == S.node) { ok = true; break; }
    if (!ok) return false;
    void* face = nullptr; if (!ReadPtr((const char*)S.node + 0xA0, face) || face != S.face) return false;
    void* arr = nullptr; int n = 0;
    if (!ReadPtr((const char*)S.face + 0x28, arr) || (const char*)arr != S.fbones || !SafeCopy(&n, (const char*)S.face + 0x30, 4) || n != S.fn) return false;
    if (!ReadPtr((const char*)S.skel + 0x28, arr) || (const char*)arr != S.bones || !SafeCopy(&n, (const char*)S.skel + 0x30, 4) || n != S.n) return false;
    for (int d = 0; d < kND; d++) {
        if (!S.cur[d]) continue;
        const char* bone = S.src[d] + (size_t)S.idx[d] * 0xc0;
        void* cur = nullptr; int ix = -1;
        if (!ReadPtr(bone + 0x18, cur) || cur != S.cur[d] || !SafeCopy(&ix, bone, 4) || ix != S.idx[d]) return false;
    }
    return true;
}
// giro de la mandibula respecto de la cabeza, en grados (actual contra reposo)
static bool JawAngle(const Skel& S, const Xf& jc, float* deg) {
    Xf hc; if (!ReadXf(S.cur[D_HEAD], hc)) return false;
    float hr[4], jr[4], c[4], rel[4];
    QConj(S.rest[D_HEAD].q, c); QMul(hc.q, c, hr);     // cabeza: actual * reposo^-1
    QConj(S.rest[D_JAW].q, c);  QMul(jc.q, c, jr);     // mandibula: idem
    QConj(hr, c); QMul(c, jr, rel);                     // mandibula respecto de la cabeza
    float w = fabsf(rel[3]); if (w > 1.0f) w = 1.0f;
    *deg = 2.0f * acosf(w) * 57.29578f;
    return true;
}
}   // namespace

// en el Eval de la restriccion del cuello (musculo.cpp hk_Eval, clase "cuello"): una vez por personaje y cuadro
void ExprBonesFrame(const void* skel) {
    if (!skel) return;
    s_calls.fetch_add(1, std::memory_order_relaxed);
    const unsigned fr = s_frame.load(std::memory_order_relaxed);
    const bool on = g_cfg.live[350][0] > 0.5f && g_cfg.live[340][0] > 0.5f;
    // (02/11, Agustin: "sacar la lengua de forma realista y mostrar mas los dientes") 350.z sacar la lengua (la lengua la
    // estira el shader, PesTongue; para que salga la boca se abre al menos 0.45 x 350.z), 350.w mostrar los dientes (los
    // labios se separan de los dientes: el de arriba sube, el de abajo baja, las comisuras se abren)
    const float tongueOut = std::clamp(g_cfg.live[350][2], 0.0f, 1.0f), teeth = std::clamp(g_cfg.live[350][3], 0.0f, 1.5f);
    const float o = (std::max)(std::clamp(g_cfg.live[347][0], -0.25f, 1.0f), 0.45f * tongueOut);
    std::lock_guard<std::mutex> lk(s_mx);
    Skel* S = Get(skel);
    S->lastFrame = fr;
    if (!S->jawFound) {
        // el arreglo pudo cambiar (otro modelo en el mismo esqueleto): se vuelve a armar cada ~2 s
        if (S->rebuilds < 60 && (fr % 60) == 0) { Build(*S, skel); S->rebuilds++; }
        if (!S->jawFound) { s_noJaw.fetch_add(1, std::memory_order_relaxed); return; }
    }
    if (!StillValid(*S)) {   // cambio de jugador / de cara: no se toca nada este cuadro, se rearma
        s_stale.fetch_add(1, std::memory_order_relaxed);
        Build(*S, skel); S->rebuilds++;
        return;
    }
    Xf jc; if (!ReadXf(S->cur[D_JAW], jc)) { Build(*S, skel); S->rebuilds++; return; }
    s_lastActive.store(fr, std::memory_order_relaxed);
    float deg; if (JawAngle(*S, jc, &deg)) { S->angMin = (std::min)(S->angMin, deg); S->angMax = (std::max)(S->angMax, deg); S->angN++; }
    if (!on || (fabsf(o) < 0.001f && teeth < 0.001f)) return;
    // eje X de la cabeza actual (el reposo del skl mira a +z, x a la izquierda) y pivote = mandibula actual
    Xf hc; if (!ReadXf(S->cur[D_HEAD], hc)) return;
    float c[4], hr[4]; QConj(S->rest[D_HEAD].q, c); QMul(hc.q, c, hr);
    static const float X[3] = { 1.0f, 0.0f, 0.0f }; float ax[3]; QRot(hr, X, ax);
    float ln = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]); if (ln < 0.5f) return; for (float& v : ax) v /= ln;
    const float P[3] = { jc.t[0], jc.t[1], jc.t[2] };
    const float A = 0.40f * o;    // (02/11, Agustin: "esta al reves") con el signo negativo la boca se cerraba al subir el slider: en el cuaternion del motor el menton baja con el angulo positivo
    for (int d = 0; d < kND; d++) {
        if (!S->cur[d]) continue;
        const float* tv = TeethOf(d);
        const bool rot = kDef[d].jaw > 0.0f && fabsf(A) > 1e-5f, mv = teeth > 0.001f && (tv[0] != 0.0f || tv[1] != 0.0f || tv[2] != 0.0f);
        if (!rot && !mv) continue;
        Xf x; if (!ReadXf(S->cur[d], x)) continue;
        float nq[4] = { x.q[0], x.q[1], x.q[2], x.q[3] }, nt[3] = { x.t[0], x.t[1], x.t[2] };
        if (rot) {
            const float h = 0.5f * A * kDef[d].jaw, sn = sinf(h);
            const float qr[4] = { ax[0] * sn, ax[1] * sn, ax[2] * sn, cosf(h) };
            float v[3] = { x.t[0] - P[0], x.t[1] - P[1], x.t[2] - P[2] }, rv[3];
            QRot(qr, v, rv); QMul(qr, x.q, nq);
            nt[0] = P[0] + rv[0]; nt[1] = P[1] + rv[1]; nt[2] = P[2] + rv[2];
        }
        if (mv) {   // corrimiento en los ejes de la cabeza (x a la izquierda del jugador, y arriba, z adelante) -> modelo
            const float hv[3] = { tv[0] * teeth, tv[1] * teeth, tv[2] * teeth }; float mvv[3];
            QRot(hr, hv, mvv);
            nt[0] += mvv[0]; nt[1] += mvv[1]; nt[2] += mvv[2];
        }
        float out[7] = { nq[0], nq[1], nq[2], nq[3], nt[0], nt[1], nt[2] };
        if (!SafeCopy((char*)S->cur[d] + 16, out, 28)) { s_badWrite.fetch_add(1, std::memory_order_relaxed); continue; }
        s_applied.fetch_add(1, std::memory_order_relaxed);
    }
}

// (02/11) VOLCADO de un esqueleto, una vez: en la PC el esqueleto del cuerpo tiene 175 huesos y solo se reconocen sk_head
// (11) y sk_neck (6): los skf_* no estan con reposo en el espacio del modelo. Para ver que hay: el objeto esqueleto (0x80
// bytes), el hueso de la cabeza entero (0xc0 bytes) con los punteros que apunten a texto (nombre?), el indice del padre de
// cada hueso (+ 0x20) y el reposo de todos los huesos de la cabeza para arriba o con traslacion chica (local?)
static bool LooksText(const char* p, char* out, int n) {
    char b[48] = {}; if (!SafeCopy(b, p, 40)) return false;
    int k = 0; while (k < 40 && b[k] >= 0x20 && b[k] < 0x7f) k++;
    if (k < 3 || (k < 40 && b[k] != 0)) return false;
    snprintf(out, n, "%.*s", k, b); return true;
}
static void Dump(const Skel& S) {
    char l[1600]; int k;
    unsigned char h[0x80] = {};
    if (SafeCopy(h, S.skel, sizeof h)) {
        k = snprintf(l, sizeof l, "boca por huesos: volcado esqueleto %p (%d huesos):", S.skel, S.n);
        for (int i = 0; i < 0x80 && k < (int)sizeof l - 8; i++) k += snprintf(l + k, sizeof l - k, "%s%02X", (i % 8) == 0 ? " " : "", h[i]);
        Log("%s", l);
        k = snprintf(l, sizeof l, "boca por huesos: esqueleto %p punteros a texto:", S.skel);
        for (int i = 0; i + 8 <= 0x80; i += 8) { void* q = nullptr; memcpy(&q, h + i, 8); char t[48]; if (q && LooksText((const char*)q, t, sizeof t)) k += snprintf(l + k, sizeof l - k, " +%02X \"%s\"", i, t); }
        Log("%s", l);
    }
    const int hb = S.cur[D_HEAD] ? S.idx[D_HEAD] : 0;
    unsigned char bb[0xc0] = {};
    if (SafeCopy(bb, S.bones + (size_t)hb * 0xc0, sizeof bb)) {
        k = snprintf(l, sizeof l, "boca por huesos: hueso %d (cabeza) %p:", hb, S.bones + (size_t)hb * 0xc0);
        for (int i = 0; i < 0xc0 && k < (int)sizeof l - 8; i++) k += snprintf(l + k, sizeof l - k, "%s%02X", (i % 8) == 0 ? " " : "", bb[i]);
        Log("%s", l);
        k = snprintf(l, sizeof l, "boca por huesos: hueso %d punteros a texto:", hb);
        for (int i = 0; i + 8 <= 0xc0; i += 8) {
            void* q = nullptr; memcpy(&q, bb + i, 8); char t[48];
            if (q && LooksText((const char*)q, t, sizeof t)) k += snprintf(l + k, sizeof l - k, " +%02X \"%s\"", i, t);
            else if (q) { void* q2 = nullptr; if (ReadPtr(q, q2) && LooksText((const char*)q2, t, sizeof t)) k += snprintf(l + k, sizeof l - k, " +%02X->\"%s\"", i, t); }
        }
        Log("%s", l);
    }
    // padre e indice de cada hueso, reposo (traslacion) de los que estan a la altura de la cabeza o con traslacion chica
    k = snprintf(l, sizeof l, "boca por huesos: esqueleto %p reposos (indice/padre:x y z):", S.skel);
    int shown = 0;
    for (int b = 0; b < S.n; b++) {
        Xf r; if (!ReadXf(S.bones + (size_t)b * 0xc0 + 0x40, r)) continue;
        void* par = nullptr; int pi = -1;
        if (ReadPtr(S.bones + (size_t)b * 0xc0 + 0x20, par)) { ptrdiff_t d = (const char*)par - S.bones; if (d >= 0 && d % 0xc0 == 0 && d / 0xc0 < S.n) pi = (int)(d / 0xc0); }
        const float m = fabsf(r.t[0]) + fabsf(r.t[1]) + fabsf(r.t[2]);
        if (!(r.t[1] > 1.50f || m < 0.12f)) continue;
        if (k > (int)sizeof l - 60) { Log("%s", l); k = snprintf(l, sizeof l, "boca por huesos: (sigue)"); }
        k += snprintf(l + k, sizeof l - k, " %d/%d:%.3f %.3f %.3f", b, pi, r.t[0], r.t[1], r.t[2]); shown++;
    }
    k += snprintf(l + k, sizeof l - k, " (%d de %d)", shown, S.n);
    Log("%s", l);
    // el nodo del personaje (personajes.cpp): otros objetos con pinta de esqueleto colgados del nodo (el de la cara?), dos niveles
    CharInfo ci[kCharSlots]; bool alive[kCharSlots] = {};
    CharSnapshot(ci, alive);
    void* node = nullptr;
    for (int i = 0; i < kCharSlots; i++) if (alive[i] && ci[i].skel == S.skel) { node = ci[i].node; break; }
    if (!node) { Log("boca por huesos: esqueleto %p sin nodo de personaje conocido", S.skel); return; }
    auto looksSkel = [](const void* q, int* n, Xf* r0) {
        void* arr = nullptr; int cnt = 0;
        if (!ReadPtr((const char*)q + 0x28, arr) || !SafeCopy(&cnt, (const char*)q + 0x30, 4) || cnt < 2 || cnt > 1024) return false;
        if (!ReadXf((const char*)arr + 0x40, *r0)) return false;
        *n = cnt; return true;
    };
    k = snprintf(l, sizeof l, "boca por huesos: nodo %p (esqueleto principal en +88) objetos con pinta de esqueleto:", node);
    int found = 0;
    for (int off = 0; off + 8 <= 0x400 && k < (int)sizeof l - 100; off += 8) {
        void* q = nullptr; if (!ReadPtr((const char*)node + off, q)) continue;
        int n; Xf r0;
        if (looksSkel(q, &n, &r0)) { k += snprintf(l + k, sizeof l - k, " +%X:%p(%d huesos, h0 %.3f %.3f %.3f)", off, q, n, r0.t[0], r0.t[1], r0.t[2]); found++; continue; }
        for (int o2 = 0; o2 + 8 <= 0x200 && k < (int)sizeof l - 100; o2 += 8) {
            void* q2 = nullptr; if (!ReadPtr((const char*)q + o2, q2) || q2 == node) continue;
            if (looksSkel(q2, &n, &r0)) { k += snprintf(l + k, sizeof l - k, " +%X/+%X:%p(%d huesos, h0 %.3f %.3f %.3f)", off, o2, q2, n, r0.t[0], r0.t[1], r0.t[2]); found++; }
        }
    }
    k += snprintf(l + k, sizeof l - k, " (%d)", found);
    Log("%s", l);
}

// Present (hilo de render): estado para el shader (350.y) y resumen al log
void ExprBonesPresent() {
    const unsigned fr = s_frame.fetch_add(1) + 1;
    const bool active = fr - s_lastActive.load(std::memory_order_relaxed) < 120;
    g_cfg.live[350][1] = active ? 1.0f : 0.0f;
    static unsigned lastLog = 0;
    if (fr - lastLog < 600) return;   // cada ~10 s
    lastLog = fr;
    std::lock_guard<std::mutex> lk(s_mx);
    const int calls = s_calls.exchange(0), ap = s_applied.exchange(0), nj = s_noJaw.exchange(0), bw = s_badWrite.exchange(0), st = s_stale.exchange(0);
    if (!calls) return;
    static int dumped = 0;
    if (dumped < 0) for (int i = 0; i < s_nSk && dumped < 2; i++) if (s_sk[i].n > 0 && fr - s_sk[i].lastFrame < 600) { Dump(s_sk[i]); dumped++; }
    if (s_logs < 40) {
        char l[1200]; int k = snprintf(l, sizeof l, "boca por huesos: %d pasos por el cuello, %d giros escritos, %d sin mandibula, %d escrituras fallidas, %d rearmados por cambio de cara; esqueletos:", calls, ap, nj, bw, st);
        for (int i = 0; i < s_nSk && k < (int)sizeof l - 120; i++) {
            Skel& S = s_sk[i];
            if (fr - S.lastFrame > 600) continue;
            if (S.angN) k += snprintf(l + k, sizeof l - k, " %p (cara %p %d huesos, %d reconocidos, mandibula %s, gira %.1f..%.1f grados)", S.skel, S.face, S.fn, S.found, S.jawFound ? "si" : "NO", S.angMin, S.angMax);
            else k += snprintf(l + k, sizeof l - k, " %p (cara %p %d huesos, %d reconocidos, mandibula %s)", S.skel, S.face, S.fn, S.found, S.jawFound ? "si" : "NO");
            S.angMin = 1e9f; S.angMax = -1e9f; S.angN = 0;
            if (!S.logged && S.found) {   // una vez por esqueleto: que hueso se reconocio (indice en el arreglo)
                S.logged = true;
                char m[1500]; int j = snprintf(m, sizeof m, "boca por huesos: esqueleto %p huesos reconocidos:", S.skel);
                for (int d = 0; d < kND && j < (int)sizeof m - 40; d++) if (S.cur[d]) j += snprintf(m + j, sizeof m - j, " %s=%d", kDef[d].name, S.idx[d]);
                Log("%s", m);
                if (S.face) {   // todos los nombres del esqueleto de la cara (una vez): para la pose facial propia
                    j = snprintf(m, sizeof m, "boca por huesos: esqueleto de la cara %p (%d huesos):", S.face, S.fn);
                    for (int b = 0; b < S.fn && j < (int)sizeof m - 70; b++) { char nm[64]; Xf r; const char* bone = S.fbones + (size_t)b * 0xc0; if (!BoneName(bone, nm, sizeof nm)) snprintf(nm, sizeof nm, "?"); if (ReadXf(bone + 0x40, r)) j += snprintf(m + j, sizeof m - j, " %d:%s(%.3f %.3f %.3f)", b, nm, r.t[0], r.t[1], r.t[2]); else j += snprintf(m + j, sizeof m - j, " %d:%s", b, nm); }
                    Log("%s", m);
                }
            }
        }
        Log("%s", l); s_logs++;
    }
}

}   // namespace fx
