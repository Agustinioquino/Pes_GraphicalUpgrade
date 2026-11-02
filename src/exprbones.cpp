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
static bool SafeCopy(void* dst, const void* src, size_t n) {
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
enum { D_JAW = 0, D_HEAD = kND - 2 };

struct Skel {
    const void* skel = nullptr; const char* bones = nullptr; int n = 0;
    void* cur[kND] = {};      // puntero a la transformacion actual de cada hueso reconocido (null = no esta)
    int idx[kND] = {};        // indice del hueso en el arreglo del esqueleto
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
static std::atomic<int> s_applied{0}, s_noJaw{0}, s_badWrite{0}, s_calls{0};
static std::atomic<unsigned> s_lastActive{0};   // cuadro del ultimo giro aplicado o esqueleto con mandibula visto
static int s_logs = 0;

static float Dist2(const float* a, const BoneDef& d) { float x = a[0] - d.x, y = a[1] - d.y, z = a[2] - d.z; return x * x + y * y + z * z; }

// arma la tabla de huesos reconocidos de un esqueleto (una vez, o si cambio el arreglo)
static bool Build(Skel& S, const void* skel) {
    S = Skel(); S.skel = skel;
    void* arr = nullptr; int n = 0;
    if (!ReadPtr((const char*)skel + 0x28, arr) || !SafeCopy(&n, (const char*)skel + 0x30, 4) || n <= 2 || n > 1024) return false;
    S.bones = (const char*)arr; S.n = n;
    // todos los pares (hueso, definicion) a menos de 4 mm, del mas cerca al mas lejos; cada uno se asigna una sola vez
    struct Pair { float d2; int b, d; };
    std::vector<Pair> ps;
    std::vector<Xf> rests((size_t)n);
    std::vector<char> okRest((size_t)n, 0);
    for (int b = 0; b < n; b++) {
        if (!ReadXf(S.bones + (size_t)b * 0xc0 + 0x40, rests[b])) continue;
        okRest[b] = 1;
        for (int d = 0; d < kND; d++) { float d2 = Dist2(rests[b].t, kDef[d]); if (d2 < 0.004f * 0.004f) ps.push_back({ d2, b, d }); }
    }
    std::sort(ps.begin(), ps.end(), [](const Pair& a, const Pair& b) { return a.d2 < b.d2; });
    std::vector<char> usedB((size_t)n, 0); bool usedD[kND] = {};
    for (const Pair& p : ps) {
        if (usedB[p.b] || usedD[p.d]) continue;
        void* cur = nullptr;
        if (!ReadPtr(S.bones + (size_t)p.b * 0xc0 + 0x18, cur)) continue;
        Xf c; if (!ReadXf(cur, c)) continue;
        usedB[p.b] = 1; usedD[p.d] = true;
        S.cur[p.d] = cur; S.rest[p.d] = rests[p.b]; S.idx[p.d] = p.b; S.found++;
    }
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
    const float o = std::clamp(g_cfg.live[347][0], -0.25f, 1.0f);
    std::lock_guard<std::mutex> lk(s_mx);
    Skel* S = Get(skel);
    S->lastFrame = fr;
    if (!S->jawFound) {
        // el arreglo pudo cambiar (otro modelo en el mismo esqueleto): se vuelve a armar cada ~2 s
        if (S->rebuilds < 30 && (fr % 120) == 0) { Build(*S, skel); S->rebuilds++; }
        if (!S->jawFound) { s_noJaw.fetch_add(1, std::memory_order_relaxed); return; }
    }
    Xf jc; if (!ReadXf(S->cur[D_JAW], jc)) { Build(*S, skel); S->rebuilds++; return; }
    s_lastActive.store(fr, std::memory_order_relaxed);
    float deg; if (JawAngle(*S, jc, &deg)) { S->angMin = (std::min)(S->angMin, deg); S->angMax = (std::max)(S->angMax, deg); S->angN++; }
    if (!on || fabsf(o) < 0.001f) return;
    // eje X de la cabeza actual (el reposo del skl mira a +z, x a la izquierda) y pivote = mandibula actual
    Xf hc; if (!ReadXf(S->cur[D_HEAD], hc)) return;
    float c[4], hr[4]; QConj(S->rest[D_HEAD].q, c); QMul(hc.q, c, hr);
    static const float X[3] = { 1.0f, 0.0f, 0.0f }; float ax[3]; QRot(hr, X, ax);
    float ln = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]); if (ln < 0.5f) return; for (float& v : ax) v /= ln;
    const float P[3] = { jc.t[0], jc.t[1], jc.t[2] };
    const float A = -0.40f * o;   // mismo sentido que PesRotX del shader: el menton baja con o > 0
    for (int d = 0; d < kND; d++) {
        if (kDef[d].jaw <= 0.0f || !S->cur[d]) continue;
        Xf x; if (!ReadXf(S->cur[d], x)) continue;
        const float h = 0.5f * A * kDef[d].jaw, sn = sinf(h);
        const float qr[4] = { ax[0] * sn, ax[1] * sn, ax[2] * sn, cosf(h) };
        float v[3] = { x.t[0] - P[0], x.t[1] - P[1], x.t[2] - P[2] }, rv[3], nq[4];
        QRot(qr, v, rv); QMul(qr, x.q, nq);
        float out[7] = { nq[0], nq[1], nq[2], nq[3], P[0] + rv[0], P[1] + rv[1], P[2] + rv[2] };
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
    const int calls = s_calls.exchange(0), ap = s_applied.exchange(0), nj = s_noJaw.exchange(0), bw = s_badWrite.exchange(0);
    if (!calls) return;
    static int dumped = 0;
    if (dumped < 2) for (int i = 0; i < s_nSk && dumped < 2; i++) if (s_sk[i].n > 0 && fr - s_sk[i].lastFrame < 600) { Dump(s_sk[i]); dumped++; }
    if (s_logs < 40) {
        char l[1200]; int k = snprintf(l, sizeof l, "boca por huesos: %d pasos por el cuello, %d giros escritos, %d sin mandibula, %d escrituras fallidas; esqueletos:", calls, ap, nj, bw);
        for (int i = 0; i < s_nSk && k < (int)sizeof l - 120; i++) {
            Skel& S = s_sk[i];
            if (fr - S.lastFrame > 600) continue;
            if (S.angN) k += snprintf(l + k, sizeof l - k, " %p (%d huesos de cara de %d, mandibula %s, gira %.1f..%.1f grados)", S.skel, S.found, S.n, S.jawFound ? "si" : "NO", S.angMin, S.angMax);
            else k += snprintf(l + k, sizeof l - k, " %p (%d huesos de cara de %d, mandibula %s)", S.skel, S.found, S.n, S.jawFound ? "si" : "NO");
            S.angMin = 1e9f; S.angMax = -1e9f; S.angN = 0;
            if (!S.logged && S.found) {   // una vez por esqueleto: que hueso se reconocio (indice en el arreglo)
                S.logged = true;
                char m[900]; int j = snprintf(m, sizeof m, "boca por huesos: esqueleto %p huesos reconocidos:", S.skel);
                for (int d = 0; d < kND && j < (int)sizeof m - 40; d++) if (S.cur[d]) j += snprintf(m + j, sizeof m - j, " %s=%d", kDef[d].name, S.idx[d]);
                Log("%s", m);
            }
        }
        Log("%s", l); s_logs++;
    }
}

}   // namespace fx
