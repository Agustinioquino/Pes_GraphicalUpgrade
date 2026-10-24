// PES21 FX - musculatura v5 (24/10). Ingenieria inversa de PES2021.exe 1.07.02 (sello 62787C6A).
// El esqueleto del motor tiene huesos de deformacion por musculo (dsk_deltoid, dsk_pectoralis, dsk_thighmain, ...) y a
// cada uno lo mueve una "restriccion" de su clase (cobra::rd::DeltoidConstraint, PectConstraint, ...; la lista completa
// hueso -> clase sale de la funcion que arma el rig, 0x141a6cd80). Cada cuadro, CharacterConstraint::Update (vtable[3],
// 0x141a6f2b0) copia la transformacion base del hueso respecto de su hueso destino (escala, rotacion, traslacion) y llama a
// vtable[4], el Eval de la clase, que la ajusta a la pose; despues compone destino * salida y la escribe en el hueso. La DLL
// cambia la entrada [4] de esas vtables (sin tocar codigo; solo si coinciden el sello del exe, el COL de cada vtable y la
// funcion original) y, despues del Eval del juego, cambia la salida.
//
// El reposo de cada hueso sale del propio esqueleto (hueso + 0x40 = transformacion de reposo en el espacio del modelo; hueso
// = restriccion + 0x58, destino = + 0x60). v5 (con los reposos reales del log de la v4): los huesos de reposo SI rotan, y en
// brazos y piernas el eje X del hueso va a lo largo del miembro (Y adelante). Entonces:
//  - brazos, antebrazos, muslos, pantorrillas, short y mangas engordan en los ejes Y y Z del hueso (grosor) alrededor del
//    eje del miembro (ancla = un punto del eje, medido en los modelos): el largo no cambia;
//  - el hombro crece igual en los tres ejes alrededor del centro de la articulacion (eso no depende de los ejes del hueso);
//  - los musculos "de superficie" (pecho, dorsales, trapecio, omoplatos, espalda, abdominales, oblicuos, panza, gluteos)
//    ya no se escalan (en ejes girados la escala corria el borde del pectoral contra el brazo y dejaba un pliegue): se
//    corren hacia afuera, a lo largo de su normal; como los pesos del hueso bajan suave hacia los bordes, queda un bulto
//    redondeado, mas grueso en el centro del musculo;
//  - biceps / triceps y cuadriceps / isquiotibiales: ademas de engordar, el miembro se corre hacia el lado que mas crece.
// El lado (izq / der) sale del entero que guarda el constructor en + 0x80, o del destino.
//   164: x on | y hombros | z biceps | w triceps        165: x antebrazos | y pecho | z espalda | w muslos
//   166: x pantorrillas | y la ropa acompana             167: x general | y trapecio | z omoplatos | w dorsales
//   168: x espalda baja | y abdominales | z oblicuos | w panza
//   169: x gluteos | y cuadriceps | z isquiotibiales | w aductores
//   170: x cuello | y pecho alto | z codos y rodillas acompanan
//   171: x rodillas | y cintura | z caderas | w ancho de hombros
#include "core.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <string>
#include <mutex>
#include <cmath>

namespace fx {

static const unsigned kStamp = 0x62787C6A;

// que sliders mueven a cada hueso (s_fac[mus] se calcula una vez por cuadro)
enum Mus : int {
    M_DELT, M_ARM, M_FORE, M_ELBOW, M_WRIST, M_SLEEVE,
    M_PECT, M_CLAV, M_UNDERARM, M_RIB, M_LAT, M_SCAP, M_TRAP, M_COLLAR, M_BACK, M_LUMB,
    M_ABS, M_OBL, M_BELLY, M_GLUTE, M_THIGH, M_ADD, M_CALF, M_KNEE, M_HEM, M_PANTS, M_NECK,
    M_COUNT
};
// lado: 0 central (sin espejo), 1 entero del constructor en + 0x80 (0 = izquierdo = +X), 2 signo de X del destino en
// reposo, 3 puntero a hueso en + 0x80 (el omoplato guarda ahi sk_shoulder) -> signo de su X en reposo
// ancla (ax, ay, az): punto del modelo en reposo (lado izquierdo; X se espeja) que queda quieto al escalar.
// k: cuanto de la variacion del slider va a cada eje DEL HUESO (factor = 1 + k * e). Brazos y piernas: X = largo (0),
// Y y Z = grosor. Iguales en los tres ejes = crece en redondo (no depende de como este girado el hueso).
struct MusCls { const char* name; uintptr_t vt, col, eval; int mus; int side; float ax, ay, az, kx, ky, kz; };
static const MusCls s_cls[] = {
    // hombros y brazos (anclas sobre el eje del brazo, medido en arm.fmdl de TorsoBody v11)
    { "deltoides",       0x142ab2f18ull, 0x142da5818ull, 0x141a58a80ull, M_DELT,     1, 0.180f, 1.440f,  0.026f, 0.8f, 0.8f, 0.35f }, // centro de la tapa del hombro; Z del hueso = hacia arriba (crece poco: si no, escalon con el trapecio)
    { "brazo",           0x142ab2f50ull, 0x142da58b0ull, 0x141a67300ull, M_ARM,      1, 0.300f, 1.335f,  0.025f, 0.0f, 0.5f, 0.25f }, // pesa sobre todo en la union con el torso (v11): poco
    { "brazo piel",      0x142ab2f88ull, 0x142da5948ull, 0x141a68820ull, M_ARM,      1, 0.310f, 1.330f,  0.025f, 0.0f, 1.0f, 0.6f },
    { "brazo piel 2",    0x142ab2fc0ull, 0x142da59e0ull, 0x141a67e60ull, M_ARM,      1, 0.265f, 1.368f,  0.025f, 0.0f, 0.8f, 0.5f },
    { "manga",           0x142ab2ff8ull, 0x142da5a78ull, 0x141a62e20ull, M_SLEEVE,   1, 0.250f, 1.380f,  0.025f, 0.0f, 1.0f, 0.6f },
    { "antebrazo",       0x142ab3158ull, 0x142da5e08ull, 0x141a5a5a0ull, M_FORE,     2, 0.451f, 1.213f,  0.037f, 0.0f, 1.0f, 1.0f },
    { "antebrazo 2",     0x142ab3120ull, 0x142da5d70ull, 0x141a5a260ull, M_FORE,     1, 0.545f, 1.123f,  0.051f, 0.0f, 1.0f, 1.0f },
    { "codo",            0x142ab30e8ull, 0x142da5cd8ull, 0x141a59560ull, M_ELBOW,    2, 0.400f, 1.258f,  0.030f, 1.0f, 1.0f, 1.0f },
    { "muneca",          0x142ab30b0ull, 0x142da5c40ull, 0x141a694d0ull, M_WRIST,    1, 0.590f, 1.090f,  0.058f, 1.0f, 1.0f, 1.0f },
    // pecho, espalda, abdomen y gluteos: se corren hacia afuera (la direccion y cuanto: UpdateFactors)
    { "pectoral",        0x142ab32a8ull, 0x142da6198ull, 0x141a605c0ull, M_PECT,     1, 0.100f, 1.384f,  0.069f, 0.0f, 0.0f, 0.0f },
    { "pecho alto",      0x142ab3238ull, 0x142da6068ull, 0x141a57e30ull, M_CLAV,     1, 0.100f, 1.485f,  0.050f, 0.0f, 0.0f, 0.0f },
    { "axila",           0x142ab3328ull, 0x142da62c8ull, 0x141a66b80ull, M_UNDERARM, 1, 0.150f, 1.418f,  0.070f, 0.0f, 0.0f, 0.0f },
    { "costado",         0x142ab3270ull, 0x142da6100ull, 0x141a612d0ull, M_RIB,      1, 0.120f, 1.275f,  0.039f, 0.0f, 0.0f, 0.0f },
    { "dorsal",          0x142ab3360ull, 0x142da6360ull, 0x141a66960ull, M_LAT,      1, 0.150f, 1.427f, -0.020f, 0.0f, 0.0f, 0.0f },
    { "omoplato",        0x142ab3398ull, 0x142da63f8ull, 0x141a619d0ull, M_SCAP,     3, 0.110f, 1.443f, -0.020f, 0.0f, 0.0f, 0.0f },
    { "trapecio",        0x142ab3068ull, 0x142da5ba8ull, 0x141a647f0ull, M_TRAP,     1, 0.100f, 1.490f, -0.017f, 0.0f, 0.0f, 0.0f },
    { "base del cuello", 0x142ab3200ull, 0x142da5fd0ull, 0x141a582f0ull, M_COLLAR,   1, 0.060f, 1.510f,  0.020f, 0.0f, 0.0f, 0.0f },
    { "espalda baja",    0x142ab32e0ull, 0x142da6230ull, 0x141a54d70ull, M_BACK,     0, 0.000f, 1.168f, -0.020f, 0.0f, 0.0f, 0.0f },
    { "lumbar",          0x142ab34e0ull, 0x142da6778ull, 0x141a550c0ull, M_LUMB,     1, 0.070f, 1.187f, -0.020f, 0.0f, 0.0f, 0.0f },
    { "abdominal",       0x142ab3470ull, 0x142da6648ull, 0x141a55890ull, M_ABS,      1, 0.070f, 1.151f,  0.130f, 0.0f, 0.0f, 0.0f },
    { "oblicuo",         0x142ab34a8ull, 0x142da66e0ull, 0x141a563c0ull, M_OBL,      1, 0.130f, 1.151f,  0.047f, 0.0f, 0.0f, 0.0f },
    { "panza",           0x142ab3430ull, 0x142da65b0ull, 0x141a57980ull, M_BELLY,    0, 0.000f, 1.178f,  0.140f, 0.0f, 0.0f, 0.0f },
    { "gluteo",          0x142ab3518ull, 0x142da6810ull, 0x141a5c4a0ull, M_GLUTE,    2, 0.080f, 1.010f, -0.020f, 0.0f, 0.0f, 0.0f },
    // piernas (anclas sobre el eje de la pierna, medido en thigh_middle / thigh_short)
    { "muslo",           0x142ab3550ull, 0x142da68a8ull, 0x141a64180ull, M_THIGH,    2, 0.130f, 0.680f,  0.066f, 0.0f, 1.0f, 1.0f },
    { "aductor",         0x142ab3588ull, 0x142da6940ull, 0x141a63d30ull, M_ADD,      1, 0.120f, 0.742f,  0.067f, 0.0f, 0.3f, 0.3f },
    { "pantorrilla",     0x142ab3748ull, 0x142da6e08ull, 0x141a5cf10ull, M_CALF,     2, 0.177f, 0.430f,  0.041f, 0.0f, 0.8f, 0.8f },
    { "rodilla",         0x142ab3780ull, 0x142da6ea0ull, 0x141a5c750ull, M_KNEE,     2, 0.150f, 0.544f,  0.062f, 0.2f, 0.0f, 1.0f },  // X adelante (rotula), Z costado
    { "corva",           0x142ab37b8ull, 0x142da6f38ull, 0x141a5cc80ull, M_KNEE,     2, 0.150f, 0.544f,  0.062f, 0.6f, 0.0f, 1.0f },  // X atras, Z costado
    { "borde del short", 0x142ab35f8ull, 0x142da6a70ull, 0x141a5ad80ull, M_HEM,      1, 0.100f, 0.850f,  0.063f, 0.0f, 1.0f, 1.0f },
    { "short",           0x142ab35c0ull, 0x142da69d8ull, 0x141a5dd30ull, M_PANTS,    1, 0.110f, 1.000f,  0.065f, 0.0f, 1.0f, 1.0f },
    // cuello (scm: ejes del modelo; nuca: X atras, Z costado): engorda alrededor del eje vertical
    { "cuello",          0x142ab31c8ull, 0x142da5f38ull, 0x141a61e40ull, M_NECK,     0, 0.000f, 1.580f,  0.025f, 1.0f, 0.0f, 1.0f },
    { "nuca",            0x142ab3190ull, 0x142da5ea0ull, 0x141a5d3a0ull, M_NECK,     0, 0.000f, 1.560f,  0.025f, 1.0f, 0.0f, 1.0f },
};
static const int kN = (int)(sizeof(s_cls) / sizeof(s_cls[0]));
static const float kArmR = 0.044f;     // medio grosor del brazo de adelante hacia atras (m, arm.fmdl)
static const float kThighR = 0.090f;   // idem muslo (thigh_middle.fmdl, a media altura)

typedef void (__fastcall* EvalFn)(void* self, void* a2, float* out);
static EvalFn s_orig[kN] = {};
static std::atomic<unsigned> s_calls[kN];
static std::atomic<unsigned> s_noRest[kN], s_far[kN];
static bool s_tried = false; static int s_ok = 0;

// e = variacion del slider (factor - 1) para los ejes del hueso; t = corrimiento en el espacio del modelo en reposo
// (metros; lado izquierdo, X se espeja en el derecho)
struct Fac { float e, t[3]; };
static Fac s_fac[M_COUNT] = {};
static bool s_on = false;

static float Gv(float v) { return v < 0.05f ? 1.0f : std::clamp(v, 0.5f, 2.0f); }   // 0 = sin valor en el ini
// suma al corrimiento: direccion (x, y, z) normalizada * metros por unidad de slider * variacion
static void Push(Fac& f, float x, float y, float z, float metros, float e) {
    float n = sqrtf(x * x + y * y + z * z); if (n < 1e-6f || e == 0.0f) return;
    float k = metros * e / n; f.t[0] += x * k; f.t[1] += y * k; f.t[2] += z * k;
}
static void UpdateFactors() {
    const float* A = g_cfg.live[164]; const float* B = g_cfg.live[165]; const float* C = g_cfg.live[166];
    const float* D = g_cfg.live[167]; const float* E = g_cfg.live[168]; const float* H = g_cfg.live[169];
    const float* N = g_cfg.live[170]; const float* Z = g_cfg.live[171];
    Fac f[M_COUNT] = {};
    s_on = A[0] > 0.5f;
    if (s_on) {
        const float G = Gv(D[0]);
        auto m = [G](float g) { return std::clamp(g * G, 0.4f, 2.5f) - 1.0f; };
        const bool ropa = C[1] > 0.5f, trans = N[2] > 0.5f;
        const float esp = Gv(B[2]), mus = Gv(B[3]);
        const float bic = m(Gv(A[2])), tri = m(Gv(A[3])), arm = 0.5f * (bic + tri), armB = kArmR * 0.5f * (bic - tri);
        const float fore = m(Gv(B[0]));
        const float quad = m(mus * Gv(H[1])), ham = m(mus * Gv(H[2])), thigh = 0.5f * (quad + ham), thighB = kThighR * 0.5f * (quad - ham);
        const float calf = m(Gv(C[0])), trap = m(esp * Gv(D[1])), neck = m(Gv(N[0]));
        const float pec = m(Gv(B[1])), clav = m(Gv(B[1]) * Gv(N[1])), dor = m(esp * Gv(D[3])), omo = m(esp * Gv(D[2])), baja = m(esp * Gv(E[0]));
        const float rod = Gv(Z[0]) - 1.0f, cint = Gv(Z[1]) - 1.0f, cad = Gv(Z[2]) - 1.0f, anch = Gv(Z[3]) - 1.0f;
        f[M_DELT].e = m(Gv(A[1]));                         Push(f[M_DELT], 1.0f, 0.12f, 0.0f, 0.030f, anch);
        const float delt = f[M_DELT].e;   // el trapecio, el pecho alto y el omoplato acompanan un poco al hombro (sin escalon)
        Push(f[M_TRAP], 0.6f, 0.8f, 0.0f, 0.012f, delt); Push(f[M_CLAV], 0.7f, 0.5f, 0.3f, 0.010f, delt); Push(f[M_SCAP], 0.5f, 0.3f, -0.6f, 0.008f, delt);
        f[M_ARM].e = arm;                                  f[M_ARM].t[2] = armB;
        f[M_FORE].e = fore;
        f[M_ELBOW].e = trans ? 0.4f * 0.5f * (arm + fore) : 0.0f;
        f[M_WRIST].e = trans ? 0.3f * fore : 0.0f;
        // la ropa solo crece (si se achicara con el musculo, lo que hay debajo y no se achica la atravesaria)
        if (ropa && arm > 0.0f) { f[M_SLEEVE].e = 1.1f * arm; f[M_SLEEVE].t[2] = 1.1f * armB; }
        Push(f[M_PECT], 0.25f, 0.10f, 1.0f, 0.035f, pec);
        Push(f[M_CLAV], 0.10f, 0.30f, 1.0f, 0.025f, clav);  Push(f[M_CLAV], 1.0f, 0.0f, 0.0f, 0.012f, anch);
        Push(f[M_UNDERARM], 0.20f, 0.0f, 1.0f, 0.020f, 0.6f * pec);
        Push(f[M_RIB], 1.0f, 0.0f, 0.10f, 0.025f, 0.6f * dor + 0.3f * cint);
        Push(f[M_LAT], 1.0f, -0.10f, -0.40f, 0.035f, dor);
        Push(f[M_SCAP], 0.20f, 0.10f, -1.0f, 0.030f, omo);
        Push(f[M_TRAP], 0.25f, 1.0f, -0.35f, 0.030f, trap); Push(f[M_TRAP], 1.0f, 0.0f, 0.0f, 0.012f, anch);
        Push(f[M_COLLAR], 0.30f, 1.0f, 0.0f, 0.015f, 0.5f * neck + 0.3f * trap);
        Push(f[M_BACK], 0.0f, 0.0f, -1.0f, 0.025f, baja);
        Push(f[M_LUMB], 0.40f, 0.0f, -1.0f, 0.025f, baja); Push(f[M_LUMB], 1.0f, 0.0f, 0.0f, 0.020f, cint);
        Push(f[M_ABS], 0.10f, 0.0f, 1.0f, 0.020f, m(Gv(E[1])));
        Push(f[M_OBL], 1.0f, 0.0f, 0.15f, 0.030f, m(Gv(E[2]))); Push(f[M_OBL], 1.0f, 0.0f, 0.10f, 0.030f, cint);
        Push(f[M_BELLY], 0.0f, -0.10f, 1.0f, 0.050f, Gv(E[3]) - 1.0f);   // la panza no es musculo: no la mueve "General"
        Push(f[M_GLUTE], 0.25f, -0.25f, -1.0f, 0.030f, m(Gv(H[0]))); Push(f[M_GLUTE], 1.0f, 0.0f, 0.0f, 0.025f, cad);
        f[M_THIGH].e = thigh;                              f[M_THIGH].t[2] = thighB;
        f[M_ADD].e = m(mus * Gv(H[3]));                    Push(f[M_ADD], -1.0f, 0.0f, 0.10f, 0.020f, f[M_ADD].e);
        f[M_CALF].e = calf;                                Push(f[M_CALF], 0.0f, 0.0f, -1.0f, 0.015f, calf);
        f[M_KNEE].e = rod + (trans ? 0.3f * 0.5f * (thigh + calf) : 0.0f);
        // el short es mas ancho que el muslo: con 0.7 de lo que engorda el muslo ya no lo atraviesa (con 1.1 quedaba acampanado)
        if (ropa && thigh > 0.0f) { f[M_HEM].e = 0.7f * thigh; f[M_HEM].t[2] = 0.8f * thighB; f[M_PANTS].e = 0.5f * thigh; }
        if (ropa && cad > 0.0f) Push(f[M_PANTS], 1.0f, 0.0f, 0.0f, 0.020f, cad);
        f[M_NECK].e = neck;
    }
    for (int i = 0; i < M_COUNT; i++) s_fac[i] = f[i];
}

static bool SafeCopy(void* dst, const void* src, size_t n) {
    __try { memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// transformacion del motor (48 bytes): escala [0..2], cuaternion (x, y, z, w) [4..7], traslacion [8..10]
struct Xf { float s[4], q[4], t[4]; };
static bool ReadXf(const void* p, Xf& x) {
    if (!p || !SafeCopy(&x, p, sizeof x)) return false;
    float n = x.q[0] * x.q[0] + x.q[1] * x.q[1] + x.q[2] * x.q[2] + x.q[3] * x.q[3];
    if (!(n > 0.81f && n < 1.21f)) return false;
    for (int k = 0; k < 3; k++) {
        if (!(x.s[k] > 0.3f && x.s[k] < 3.0f) || !(fabsf(x.t[k]) < 3.0f)) return false;
    }
    float r = 1.0f / sqrtf(n); for (int k = 0; k < 4; k++) x.q[k] *= r;
    return true;
}
static bool ReadPtr(const void* p, void*& out) { out = nullptr; return SafeCopy(&out, p, sizeof out) && out; }

// rota v por el cuaternion q = (x, y, z, w) (normalizado); QRotInv rota por el conjugado
static void QRot(const float* q, const float* v, float* o) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float tx = 2.0f * (y * v[2] - z * v[1]), ty = 2.0f * (z * v[0] - x * v[2]), tz = 2.0f * (x * v[1] - y * v[0]);
    o[0] = v[0] + w * tx + (y * tz - z * ty); o[1] = v[1] + w * ty + (z * tx - x * tz); o[2] = v[2] + w * tz + (x * ty - y * tx);
}
static void QRotInv(const float* q, const float* v, float* o) { float c[4] = { -q[0], -q[1], -q[2], q[3] }; QRot(c, v, o); }

// lado del hueso (+1 izquierdo / +X, -1 derecho)
static float SideOfCur(void* self, const float* a2) {   // v3: X del destino actual + X de la base
    float v[2] = {};
    if (!a2 || !SafeCopy(&v[0], a2 + 8, 4) || !SafeCopy(&v[1], (const char*)self + 0x40, 4)) return 1.0f;
    return v[0] + v[1] < 0.0f ? -1.0f : 1.0f;
}
static float SideFor(const MusCls& C, void* self, const Xf* rb, const Xf* rt, const float* a2, int* src) {
    *src = C.side;
    switch (C.side) {
    case 0: return 1.0f;
    case 1: { int v = -1; if (SafeCopy(&v, (const char*)self + 0x80, 4) && (v == 0 || v == 1)) return v == 0 ? 1.0f : -1.0f; break; }
    case 2: if (rt && fabsf(rt->t[0]) > 0.01f) return rt->t[0] > 0.0f ? 1.0f : -1.0f; break;
    case 3: { void* p = nullptr; Xf x; if (ReadPtr((const char*)self + 0x80, p) && ReadXf((const char*)p + 0x40, x) && fabsf(x.t[0]) > 0.01f) return x.t[0] > 0.0f ? 1.0f : -1.0f; break; }
    }
    if (rb && fabsf(rb->t[0]) > 0.01f) { *src = 4; return rb->t[0] > 0.0f ? 1.0f : -1.0f; }
    if (rt && fabsf(rt->t[0]) > 0.01f) { *src = 5; return rt->t[0] > 0.0f ? 1.0f : -1.0f; }
    *src = 6; return SideOfCur(self, a2);
}

// diagnostico: una vez por clase y lado (reposo del hueso y del destino, base, ancla en ejes del hueso)
struct DiagRec { int cls, side, src; bool okB, okT; Xf rb, rt; float baseQ[4], baseT[3], u[3], predErr; };
static const int kDiagMax = 2 * 40;
static std::mutex s_diagMx; static DiagRec s_diag[kDiagMax]; static std::atomic<int> s_diagN{0}; static int s_diagLogged = 0;
static std::atomic<int> s_diagCnt[kN];
static volatile unsigned char s_diagSide[kN] = {};   // bit 0 izquierdo, bit 1 derecho
static void Diag(int i, void* self, float side, int src, bool okB, const Xf& rb, bool okT, const Xf& rt, const float* u) {
    unsigned bit = side > 0 ? 1u : 2u;
    if (s_diagSide[i] & bit) return;   // ya anotado (lectura sin candado: a lo sumo entra uno de mas y vuelve abajo)
    std::lock_guard<std::mutex> lk(s_diagMx);
    if (s_diagSide[i] & bit) return;
    int n = s_diagN.load(); if (n >= kDiagMax) return;
    DiagRec& r = s_diag[n]; r = {}; r.cls = i; r.side = side > 0 ? 1 : -1; r.src = src; r.okB = okB; r.okT = okT; r.rb = rb; r.rt = rt;
    float b[8] = {};
    SafeCopy(b, (const char*)self + 0x30, 32);
    memcpy(r.baseQ, b, 16); memcpy(r.baseT, b + 4, 12);
    if (u) memcpy(r.u, u, 12);
    r.predErr = -1.0f;
    if (okB && okT) {   // la base deberia ser destino_reposo^-1 * hueso_reposo (como la arma el motor en 0x141a6b4f0)
        float d[3] = { rb.t[0] - rt.t[0], rb.t[1] - rt.t[1], rb.t[2] - rt.t[2] }, p[3];
        QRotInv(rt.q, d, p);
        float e = 0; for (int k = 0; k < 3; k++) { float x = p[k] / rt.s[k] - r.baseT[k]; e += x * x; }
        r.predErr = sqrtf(e);
    }
    s_diagSide[i] |= (unsigned char)bit;
    s_diagCnt[i].fetch_add(1);
    s_diagN = n + 1;
}

static bool Idle(const Fac& f) { return f.e == 0.0f && f.t[0] == 0.0f && f.t[1] == 0.0f && f.t[2] == 0.0f; }
static void Apply(int i, void* self, const float* a2, float* out, const Fac fc, bool diag) {
    const MusCls& C = s_cls[i];
    Xf rb{}, rt{}; void* bone = nullptr; void* targ = nullptr;
    bool okB = ReadPtr((const char*)self + 0x58, bone) && ReadXf((const char*)bone + 0x40, rb);
    bool okT = ReadPtr((const char*)self + 0x60, targ) && ReadXf((const char*)targ + 0x40, rt);
    int src = 0;
    float side = SideFor(C, self, okB ? &rb : nullptr, okT ? &rt : nullptr, a2, &src);
    float u[3] = {};
    if (okB) {   // ancla en los ejes del hueso: u = R_reposo^-1 (ancla - pivote_reposo) / escala_reposo
        float a[3] = { C.ax * side - rb.t[0], C.ay - rb.t[1], C.az - rb.t[2] };
        QRotInv(rb.q, a, u);
        for (int k = 0; k < 3; k++) u[k] /= rb.s[k];
    }
    if (diag) Diag(i, self, side, src, okB, rb, okT, rt, okB ? u : nullptr);
    if (!s_on || Idle(fc)) return;
    if (!okB) { s_noRest[i].fetch_add(1, std::memory_order_relaxed); return; }
    if (u[0] * u[0] + u[1] * u[1] + u[2] * u[2] > 4.0f) { s_far[i].fetch_add(1, std::memory_order_relaxed); return; }   // > 2 m: algo no cuadra (un pivote lejano, p. ej. en el piso, igual anda: la cuenta lo compensa)
    // factores por eje del hueso
    float f[3] = { std::clamp(1.0f + C.kx * fc.e, 0.4f, 2.5f), std::clamp(1.0f + C.ky * fc.e, 0.4f, 2.5f), std::clamp(1.0f + C.kz * fc.e, 0.4f, 2.5f) };
    // corrimiento: vector del modelo en reposo (X espejado en el lado derecho) -> ejes del hueso
    float bu[3] = {};
    if (fc.t[0] != 0.0f || fc.t[1] != 0.0f || fc.t[2] != 0.0f) {
        float bm[3] = { fc.t[0] * side, fc.t[1], fc.t[2] };
        QRotInv(rb.q, bm, bu); for (int k = 0; k < 3; k++) bu[k] /= rb.s[k];
    }
    // salida relativa al destino: modelo = destino * salida. Escalar la salida por f deja quieto el pivote; para que quede
    // quieta el ancla, la traslacion de la salida se corre R(q_salida) * (s_salida * ((1 - f) * u + b))
    float v[3], r[3];
    for (int k = 0; k < 3; k++) v[k] = out[k] * ((1.0f - f[k]) * u[k] + bu[k]);
    float q[4] = { out[4], out[5], out[6], out[7] };
    float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (n < 0.5f || n > 2.0f) { r[0] = v[0]; r[1] = v[1]; r[2] = v[2]; }
    else { float kq = 1.0f / sqrtf(n); for (auto& c : q) c *= kq; QRot(q, v, r); }
    out[0] *= f[0]; out[1] *= f[1]; out[2] *= f[2];
    out[8] += r[0]; out[9] += r[1]; out[10] += r[2];
}

template<int I> static void __fastcall hk_Eval(void* self, void* a2, float* out) {
    s_orig[I](self, a2, out);
    s_calls[I].fetch_add(1, std::memory_order_relaxed);
    if (!self || !out) return;
    const Fac fc = s_fac[s_cls[I].mus];
    bool diag = s_diagCnt[I].load(std::memory_order_relaxed) < (s_cls[I].side == 0 ? 1 : 2);
    if (!diag && (!s_on || Idle(fc))) return;
    Apply(I, self, (const float*)a2, out, fc, diag);
}
template<int... Is> struct HookList { static void* const v[sizeof...(Is)]; };
template<int... Is> void* const HookList<Is...>::v[sizeof...(Is)] = { (void*)&hk_Eval<Is>... };
static void* const* Hooks() {
    return HookList<0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31>::v;
}
static_assert(kN == 32, "la lista de ganchos tiene que tener una entrada por musculo");

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}

static void Install() {
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("musculatura: parches en memoria apagados (motor = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("musculatura: exe distinto, sin ganchos"); return; }
    std::string bad;
    for (int i = 0; i < kN; i++) {
        void** vt = (void**)s_cls[i].vt;
        if (!Readable(vt - 1, 6 * sizeof(void*)) || (uintptr_t)vt[-1] != s_cls[i].col || (uintptr_t)vt[4] != s_cls[i].eval) { bad += std::string(" ") + s_cls[i].name; continue; }
        DWORD old = 0;
        if (!VirtualProtect(&vt[4], sizeof(void*), PAGE_READWRITE, &old)) { bad += std::string(" ") + s_cls[i].name + "(protect)"; continue; }
        s_orig[i] = (EvalFn)vt[4];
        InterlockedExchangePointer(&vt[4], Hooks()[i]);
        VirtualProtect(&vt[4], sizeof(void*), old, &old);
        s_ok++;
    }
    Log("musculatura v5: %d de %d huesos de musculo enganchados%s%s", s_ok, kN, bad.empty() ? "" : "; no coinciden:", bad.c_str());
}

void MuscleHookPresent() {
    if (!s_tried) Install();
    if (!s_ok) return;
    UpdateFactors();
    {   // diagnostico (una vez por clase y lado)
        static const char* kSrc[] = { "central", "constructor", "destino", "hombro", "hueso", "destino*", "pose" };
        int n = s_diagN.load();
        for (; s_diagLogged < n; s_diagLogged++) {
            const DiagRec& r = s_diag[s_diagLogged];
            char pe[32]; if (r.predErr < 0) snprintf(pe, sizeof pe, "-"); else snprintf(pe, sizeof pe, "%.1f mm", r.predErr * 1000.0f);
            Log("musculatura: reposo %s %s (lado por %s) | hueso %s t(%.4f %.4f %.4f) q(%.3f %.3f %.3f %.3f) s(%.3f %.3f %.3f) | destino %s t(%.4f %.4f %.4f) q(%.3f %.3f %.3f %.3f) | base t(%.4f %.4f %.4f) q(%.3f %.3f %.3f %.3f), base predicha err %s | ancla en el hueso (%.4f %.4f %.4f)",
                s_cls[r.cls].name, r.side > 0 ? "izq" : "der", kSrc[std::clamp(r.src, 0, 6)],
                r.okB ? "ok" : "ILEGIBLE", r.rb.t[0], r.rb.t[1], r.rb.t[2], r.rb.q[0], r.rb.q[1], r.rb.q[2], r.rb.q[3], r.rb.s[0], r.rb.s[1], r.rb.s[2],
                r.okT ? "ok" : "ILEGIBLE", r.rt.t[0], r.rt.t[1], r.rt.t[2], r.rt.q[0], r.rt.q[1], r.rt.q[2], r.rt.q[3],
                r.baseT[0], r.baseT[1], r.baseT[2], r.baseQ[0], r.baseQ[1], r.baseQ[2], r.baseQ[3], pe, r.u[0], r.u[1], r.u[2]);
        }
    }
    static double s_t = 0; static unsigned s_prev[kN] = {};
    double t = Now();
    if (s_t == 0) { s_t = t; for (int i = 0; i < kN; i++) s_prev[i] = s_calls[i].load(); return; }
    if (t - s_t < 30.0) return;
    unsigned total = 0; int active = 0; std::string quiet, prob; char b[96];
    for (int i = 0; i < kN; i++) {
        unsigned c = s_calls[i].load(), d = c - s_prev[i]; s_prev[i] = c; total += d;
        if (d) active++; else quiet += std::string(" ") + s_cls[i].name;
        unsigned nr = s_noRest[i].exchange(0), fr = s_far[i].exchange(0);
        if (nr || fr) { snprintf(b, sizeof b, " %s(sin reposo %u, lejos %u)", s_cls[i].name, nr, fr); prob += b; }
    }
    const float* A = g_cfg.live[164]; const float* B = g_cfg.live[165]; const float* C = g_cfg.live[166];
    const float* D = g_cfg.live[167]; const float* E = g_cfg.live[168]; const float* H = g_cfg.live[169];
    const float* N = g_cfg.live[170]; const float* Z = g_cfg.live[171];
    if (total)
        Log("musculatura (30 s, %s): %d de %d huesos evaluados, %.0f eval/s | general %.2f hombros %.2f (ancho %.2f) biceps %.2f triceps %.2f antebrazos %.2f pecho %.2f (alto %.2f) espalda %.2f (trapecio %.2f omoplatos %.2f dorsales %.2f baja %.2f) cuello %.2f abdominales %.2f oblicuos %.2f cintura %.2f panza %.2f gluteos %.2f caderas %.2f muslos %.2f (cuadriceps %.2f isquio %.2f aductores %.2f) rodillas %.2f pantorrillas %.2f, ropa %s, codos/rodillas %s%s%s%s%s",
            s_on ? "prendida" : "apagada", active, kN, total / (t - s_t), D[0], A[1], Z[3], A[2], A[3], B[0], B[1], N[1], B[2], D[1], D[2], D[3], E[0], N[0], E[1], E[2], Z[1], E[3],
            H[0], Z[2], B[3], H[1], H[2], H[3], Z[0], C[0], C[1] > 0.5f ? "si" : "no", N[2] > 0.5f ? "si" : "no",
            quiet.empty() ? "" : " | sin llamadas:", quiet.c_str(), prob.empty() ? "" : " | problemas:", prob.c_str());
    s_t = t;
}

} // namespace fx
