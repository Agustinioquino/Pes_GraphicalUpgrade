// PES21 FX - musculatura v6 (24/10: por jugador). Ingenieria inversa de PES2021.exe 1.07.02 (sello 62787C6A).
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
// v7 (26/10): mas grupos (filas 260..262; 1 = como el juego). Los de forma corren el musculo sin "General":
//   260: x deltoides frontal | y deltoides posterior | z antebrazo alto (cerca del codo) | w antebrazo bajo (cerca de la muneca)
//   261: x pecho bajo | y dorsales: ancho (V) | z trapecio alto | w gluteo levantado
//   262: x muslo exterior | y gemelos | z (26/10) torso y brazos: 0.5 robusto .. 1 TorsoBody .. 1.5 atletico (variantes de TorsoBody:
//        modelo y texturas en los shaders, pes_variants.hlsli; aca solo el grosor de los brazos, ArmVariant)
//   287 (26/10, selectores de hombros del menu Fisico): x inclinacion (0.7 muy caidos .. 1 .. 1.3 muy rectos) | y tipo (1 normales,
//        2 estrechos, 3 anchos, 4 redondos) | z tamano (0.7 .. 1.3: deltoides, ancho, trapecio y clavicula juntos)
// v6: por jugador. personajes.cpp sabe que jugador es cada esqueleto (restriccion + 0x10); un jugador con valores propios
// (PES21FX\musculatura_jugadores.ini, tarjeta "Musculatura por jugador") usa su juego de factores, el resto los del menu.
#include "core.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <string>
#include <mutex>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <vector>
#include <imgui.h>
#include "musculo_ui.h"

namespace fx {

static const unsigned kStamp = 0x62787C6A;

// que sliders mueven a cada hueso (s_fac[mus] se calcula una vez por cuadro)
enum Mus : int {
    M_DELT, M_ARM, M_FORE, M_FORE2, M_ELBOW, M_WRIST, M_SLEEVE,
    M_PECT, M_CLAV, M_UNDERARM, M_RIB, M_LAT, M_SCAP, M_TRAP, M_COLLAR, M_BACK, M_LUMB,
    M_ABS, M_OBL, M_BELLY, M_GLUTE, M_THIGH, M_ADD, M_CALF, M_KNEE, M_HEM, M_PANTS, M_NECK, M_NAPE,
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
    { "antebrazo 2",     0x142ab3120ull, 0x142da5d70ull, 0x141a5a260ull, M_FORE2,    1, 0.545f, 1.123f,  0.051f, 0.0f, 1.0f, 1.0f },
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
    { "nuca",            0x142ab3190ull, 0x142da5ea0ull, 0x141a5d3a0ull, M_NAPE,     0, 0.000f, 1.560f,  0.025f, 1.0f, 0.0f, 1.0f },   // (24/10 g) slider propio
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
static std::atomic<bool> s_compare{false};   // (26/10 c) "Ver: como el juego" en el menu Fisico
void MuscleCompare(bool on) { if (s_compare.exchange(on) != on) Log("musculatura: ver %s", on ? "como el juego (sin musculatura)" : "con la musculatura"); }
bool MuscleCompareOn() { return s_compare.load(); }

// ---------------------------------------------------------------- musculos vivos (26/10 b): contraccion y respiracion
// 263: x contraccion con el movimiento (0 = apagado, 1 = normal, hasta 2), y respiracion (idem), z cuanto se agita al correr.
// Contraccion: el biceps se abulta con el codo doblado (y el antebrazo un poco), el deltoides y el trapecio con el brazo
// levantado, los isquiotibiales y la pantorrilla con la rodilla doblada. Se mide con los huesos del esqueleto del juego (los
// destinos de las restricciones: brazo, antebrazo, muslo, pierna y pecho), ahora contra el reposo.
// Respiracion: pecho, costillas, espalda y panza suben y bajan; mas rapido y mas hondo despues de correr (la velocidad del
// personaje sale de su nodo, la misma posicion que usa el tono de piel por jugador).
struct DynState {
    float upX[2][3], upR[2][3], foX[2][3], foR[2][3];   // eje X del brazo y del antebrazo: ahora y en reposo (0 = izq, 1 = der)
    float thX[2][3], thR[2][3], shX[2][3], shR[2][3];   // muslo y pierna
    float chQ[4], chR[4];                                // pecho: cuaternion ahora y en reposo
    volatile unsigned char upOk[2], foOk[2], thOk[2], shOk[2], chOk;
    float breath, ex, phase, pos[3]; bool posOk;         // respiracion (m, con signo), agitacion 0..1, fase, ultima posicion
};
static const int kDynFb = 64;   // esqueletos que no se sabe de que personaje son (por puntero)
static DynState s_dyn[kCharSlots + kDynFb];
static float s_dynK[3] = { 0, 0, 0 }; static bool s_dynOn = false, s_ropaOn = false; static float s_sleeveK = 0.6f, s_dynT = 0.0f;
static int DynIndex(int slot, const void* skel) {
    if (slot >= 0 && slot < kCharSlots) return slot;
    const uint32_t h = (uint32_t)((uintptr_t)skel >> 6) * 2654435761u;
    return kCharSlots + (int)(h >> 26);
}

static float Gv(float v) { return v < 0.05f ? 1.0f : std::clamp(v, 0.5f, 2.0f); }   // 0 = sin valor en el ini
// (26/10) variante del torso / brazos: valor del slider z262 (0.5 .. 1.5; 0 = sin valor en el ini) -> t (-1 robusto .. 1 atletico)
static float TvOf(float v) { return v < 0.05f ? 0.0f : std::clamp((v - 1.0f) * 2.0f, -1.0f, 1.0f); }
// suma al corrimiento: direccion (x, y, z) normalizada * metros por unidad de slider * variacion
static void Push(Fac& f, float x, float y, float z, float metros, float e) {
    float n = sqrtf(x * x + y * y + z * z); if (n < 1e-6f || e == 0.0f) return;
    float k = metros * e / n; f.t[0] += x * k; f.t[1] += y * k; f.t[2] += z * k;
}
// P = kPRows float4: 164..171, 162 y 260..262 (los del menu, o los de un jugador); on / ropa / trans / vec = interruptores de todos.
// (24/10 g) 162: x axilas | y costados (serratos) | z lumbares | w base del cuello; 166.z nuca, 166.w codos, 170.w munecas.
// vec ("Unir musculos vecinos", x255): los vecinos acompanan a un musculo (el trapecio, el pecho alto y el omoplato al
// hombro; la axila al pecho; el costado a los dorsales; la base del cuello al cuello y al trapecio; los lumbares a la espalda
// baja). Apagado (por defecto) cada slider mueve solo su musculo.
static void ComputeFactors(const float (*P)[4], bool on, bool ropa, bool trans, bool vec, Fac* f) {
    for (int i = 0; i < M_COUNT; i++) f[i] = Fac{};
    if (!on) return;
    const float* A = P[0]; const float* B = P[1]; const float* C = P[2]; const float* D = P[3];
    const float* E = P[4]; const float* H = P[5]; const float* N = P[6]; const float* Z = P[7]; const float* X = P[8];
    const float* Q = P[9]; const float* R = P[10]; const float* S = P[11];   // (26/10) v7
    const float* T = P[13];   // (26/10) hombros: inclinacion, tipo, tamano (fila 287)
    const float G = Gv(D[0]);
    auto m = [G](float g) { return std::clamp(g * G, 0.4f, 2.5f) - 1.0f; };
    const float esp = Gv(B[2]), mus = Gv(B[3]);
    // (26/10) variante robusto / atletico (z262): el grosor de los brazos va por los huesos (la manga, pesada a dsk_sleeve, acompana);
    // el modelo y las texturas del torso y los brazos los cambian los shaders (pes_variants.hlsli). = ARM_BONES de torso_variants.py
    const float vt = TvOf(S[2]), vr = (std::max)(-vt, 0.0f), va = (std::max)(vt, 0.0f);
    auto AV = [vr, va](float r, float a) { return r * vr + a * va; };
    const float bic = m(Gv(A[2])) + AV(0.05f, 0.04f), tri = m(Gv(A[3])) + AV(0.09f, 0.04f), arm = 0.5f * (bic + tri), armB = kArmR * 0.5f * (bic - tri);
    const float fore = m(Gv(B[0]) * Gv(Q[2])) + AV(0.04f, 0.02f), fore2 = m(Gv(B[0]) * Gv(Q[3])) + AV(0.04f, 0.02f);   // (26/10) antebrazo alto / bajo
    const float quad = m(mus * Gv(H[1])), ham = m(mus * Gv(H[2])), thigh = 0.5f * (quad + ham), thighB = kThighR * 0.5f * (quad - ham);
    const float calf = m(Gv(C[0])), trap = m(esp * Gv(D[1])), neck = m(Gv(N[0]));
    const float pec = m(Gv(B[1])), clav = m(Gv(B[1]) * Gv(N[1])), dor = m(esp * Gv(D[3])), omo = m(esp * Gv(D[2])), baja = m(esp * Gv(E[0]));
    // (26/10) hombros (fila 287): tipo y tamano se suman al ancho y al deltoides; la inclinacion sube / baja la tapa del hombro
    // y baja / sube el trapecio junto al cuello (caidos: la linea cuello - hombro mas empinada; rectos: mas horizontal)
    const float shSl = std::clamp(Gv(T[0]), 0.7f, 1.3f) - 1.0f, shSz = std::clamp(Gv(T[2]), 0.7f, 1.3f) - 1.0f;
    const int shTy = std::clamp((int)lroundf(T[1] < 0.5f ? 1.0f : T[1]), 1, 4);
    const float tyAnch = shTy == 2 ? -0.30f : shTy == 3 ? 0.30f : 0.0f;
    const float tyE = shTy == 2 ? -0.08f : shTy == 3 ? 0.04f : shTy == 4 ? 0.05f : 0.0f, tyRound = shTy == 4 ? 0.20f : 0.0f;
    const float rod = Gv(Z[0]) - 1.0f, cint = Gv(Z[1]) - 1.0f, cad = Gv(Z[2]) - 1.0f, anch = Gv(Z[3]) - 1.0f + tyAnch + 0.5f * shSz;
    f[M_DELT].e = m(Gv(A[1])) + AV(0.03f, 0.04f) + tyE + 0.8f * shSz;      Push(f[M_DELT], 1.0f, 0.12f, 0.0f, 0.060f, anch);   // (26/10 b) 3 -> 6 cm por unidad: casi no se notaba
    if (shSl != 0.0f) {
        Push(f[M_DELT], 0.15f, 1.0f, 0.0f, 0.040f, shSl); Push(f[M_DELT], 1.0f, 0.0f, 0.0f, 0.010f, shSl);   // la tapa sube (rectos) y se abre
        Push(f[M_TRAP], -0.20f, 1.0f, 0.0f, 0.020f, -shSl);   // el trapecio junto al cuello baja (rectos) o sube (caidos)
        Push(f[M_COLLAR], 0.0f, 1.0f, 0.0f, 0.010f, -shSl);
        Push(f[M_CLAV], 0.5f, 1.0f, 0.0f, 0.015f, shSl); Push(f[M_SCAP], 0.0f, 1.0f, 0.0f, 0.012f, shSl);
    }
    if (shSz != 0.0f) {   // tamano: el trapecio y la clavicula acompanan al hombro
        Push(f[M_TRAP], 0.25f, 1.0f, -0.35f, 0.030f, 0.5f * shSz); Push(f[M_CLAV], 0.10f, 0.30f, 1.0f, 0.025f, 0.4f * shSz);
        Push(f[M_SCAP], 0.20f, 0.10f, -1.0f, 0.030f, 0.3f * shSz);
    }
    {   // (26/10) deltoides frontal / posterior: la tapa del hombro crece un poco y se corre hacia adelante / atras
        const float df = Gv(Q[0]) - 1.0f + tyRound, dr = Gv(Q[1]) - 1.0f + tyRound;   // (redondos: las dos partes)
        f[M_DELT].e += 0.70f * (df + dr);   // (26/10 b) el doble (casi no se notaban)
        Push(f[M_DELT], 0.15f, 0.0f, 1.0f, 0.035f, df); Push(f[M_DELT], 0.15f, 0.0f, -1.0f, 0.035f, dr);
    }
    const float delt = f[M_DELT].e;   // con "unir vecinos": el trapecio, el pecho alto y el omoplato acompanan un poco al hombro
    if (vec) { Push(f[M_TRAP], 0.6f, 0.8f, 0.0f, 0.012f, delt); Push(f[M_CLAV], 0.7f, 0.5f, 0.3f, 0.010f, delt); Push(f[M_SCAP], 0.5f, 0.3f, -0.6f, 0.008f, delt); }
    f[M_ARM].e = arm;                                  f[M_ARM].t[2] = armB;
    f[M_FORE].e = fore; f[M_FORE2].e = fore2;
    f[M_ELBOW].e = (trans ? 0.4f * 0.5f * (arm + fore) : 0.0f) + (Gv(C[3]) - 1.0f);   // (24/10 g) + codos (166.w; no es musculo: sin "General")
    f[M_WRIST].e = (trans ? 0.3f * fore2 : 0.0f) + (Gv(N[3]) - 1.0f);                 // + munecas (170.w)
    // la ropa solo crece (si se achicara con el musculo, lo que hay debajo y no se achica la atravesaria).
    // (25/10) y255: cuanto del crecimiento del brazo recibe el hueso de la manga. La manga esta mas lejos del eje que la piel
    // (~6 cm contra ~4,4): con 1.1 ganaba 1,5 veces los centimetros del brazo y quedaba inflada; con 0.6 gana lo mismo.
    const float sleeveK = std::clamp(g_cfg.live[255][1], 0.0f, 1.5f);
    if (ropa && arm > 0.0f) { f[M_SLEEVE].e = sleeveK * arm; f[M_SLEEVE].t[2] = sleeveK * armB; }
    Push(f[M_PECT], 0.25f, 0.10f, 1.0f, 0.035f, pec);
    Push(f[M_PECT], 0.10f, -0.50f, 1.0f, 0.022f, Gv(R[0]) - 1.0f);   // (26/10) pecho bajo: la masa abajo y adelante
    Push(f[M_CLAV], 0.10f, 0.30f, 1.0f, 0.025f, clav);  Push(f[M_CLAV], 1.0f, 0.0f, 0.0f, 0.025f, anch);
    Push(f[M_UNDERARM], 0.20f, 0.0f, 1.0f, 0.020f, (vec ? 0.6f * pec : 0.0f) + m(Gv(X[0])));          // axilas
    Push(f[M_RIB], 1.0f, 0.0f, 0.10f, 0.025f, (vec ? 0.6f * dor : 0.0f) + 0.3f * cint + m(Gv(X[1])));  // costados (serratos)
    Push(f[M_LAT], 1.0f, -0.10f, -0.40f, 0.035f, dor);
    Push(f[M_LAT], 1.0f, 0.0f, 0.0f, 0.030f, Gv(R[1]) - 1.0f);        // (26/10) dorsales: ancho (la V, hacia los costados)
    Push(f[M_SCAP], 0.20f, 0.10f, -1.0f, 0.030f, omo);
    Push(f[M_TRAP], 0.25f, 1.0f, -0.35f, 0.030f, trap); Push(f[M_TRAP], 1.0f, 0.0f, 0.0f, 0.025f, anch);
    Push(f[M_TRAP], 0.10f, 1.0f, 0.0f, 0.020f, Gv(R[2]) - 1.0f);      // (26/10) trapecio alto (hacia arriba)
    Push(f[M_COLLAR], 0.30f, 1.0f, 0.0f, 0.015f, (vec ? 0.5f * neck + 0.3f * trap : 0.0f) + m(Gv(X[3])));   // base del cuello
    Push(f[M_BACK], 0.0f, 0.0f, -1.0f, 0.025f, baja);
    Push(f[M_LUMB], 0.40f, 0.0f, -1.0f, 0.025f, (vec ? baja : 0.0f) + m(Gv(X[2]))); Push(f[M_LUMB], 1.0f, 0.0f, 0.0f, 0.020f, cint);   // lumbares
    Push(f[M_ABS], 0.10f, 0.0f, 1.0f, 0.020f, m(Gv(E[1])));
    Push(f[M_OBL], 1.0f, 0.0f, 0.15f, 0.030f, m(Gv(E[2]))); Push(f[M_OBL], 1.0f, 0.0f, 0.10f, 0.030f, cint);
    Push(f[M_BELLY], 0.0f, -0.10f, 1.0f, 0.050f, Gv(E[3]) - 1.0f);   // la panza no es musculo: no la mueve "General"
    Push(f[M_GLUTE], 0.25f, -0.25f, -1.0f, 0.030f, m(Gv(H[0]))); Push(f[M_GLUTE], 1.0f, 0.0f, 0.0f, 0.025f, cad);
    Push(f[M_GLUTE], 0.0f, 1.0f, -0.5f, 0.020f, Gv(R[3]) - 1.0f);     // (26/10) gluteo levantado
    f[M_THIGH].e = thigh;                              f[M_THIGH].t[2] = thighB;
    const float mout = Gv(S[0]) - 1.0f;   // (26/10) muslo exterior (vasto externo): engorda un poco y se corre hacia afuera
    f[M_THIGH].e += 0.25f * mout;                      Push(f[M_THIGH], 1.0f, 0.0f, 0.0f, 0.015f, mout);
    f[M_ADD].e = m(mus * Gv(H[3]));                    Push(f[M_ADD], -1.0f, 0.0f, 0.10f, 0.020f, f[M_ADD].e);
    f[M_CALF].e = calf;                                Push(f[M_CALF], 0.0f, 0.0f, -1.0f, 0.015f, calf);
    const float gem = Gv(S[1]) - 1.0f;    // (26/10) gemelos: la panza de la pantorrilla hacia atras
    f[M_CALF].e += 0.25f * gem;                        Push(f[M_CALF], 0.0f, 0.05f, -1.0f, 0.015f, gem);
    f[M_KNEE].e = rod + (trans ? 0.3f * 0.5f * (thigh + calf) : 0.0f);
    // el short es mas ancho que el muslo: con 0.7 de lo que engorda el muslo ya no lo atraviesa (con 1.1 quedaba acampanado)
    if (ropa && thigh > 0.0f) { f[M_HEM].e = 0.7f * thigh; f[M_HEM].t[2] = 0.8f * thighB; f[M_PANTS].e = 0.5f * thigh; }
    if (ropa && mout > 0.0f) Push(f[M_HEM], 1.0f, 0.0f, 0.0f, 0.012f, mout);
    if (ropa && cad > 0.0f) Push(f[M_PANTS], 1.0f, 0.0f, 0.0f, 0.020f, cad);
    f[M_NECK].e = neck;
    f[M_NAPE].e = m(Gv(N[0]) * Gv(C[2]));   // (24/10 g) nuca: se multiplica con "Cuello" (1 = acompana al cuello)
}

// ---------------------------------------------------------------- por jugador (24/10)
// Un perfil = los sliders de musculatura (164..171) de un jugador; el resto de los jugadores usa los del menu. Los
// interruptores (x164 prendido, y166 la ropa acompana, z170 codos y rodillas) y la definicion (x172, shader) son de todos.
// Cada esqueleto se busca por la restriccion (+ 0x10) en la tabla de personajes.cpp (handle -> id del jugador).
// Archivo: PES21FX\musculatura_jugadores.ini ([id] + claves del menu; lo que falta = 1, como el juego).
static const int kPRows = 14;
struct Profile { std::string name; float v[kPRows][4]; };   // 164..171, (24/10 g) 162 en v[8], (26/10) 260..262 en v[9..11], 172 (definicion) en v[12] y 287 (hombros) en v[13]
static int PRow(int idx) { return idx >= 164 && idx <= 171 ? idx - 164 : idx == 162 ? 8 : idx >= 260 && idx <= 262 ? 9 + (idx - 260) : idx == 172 ? 12 : idx == 287 ? 13 : -1; }
static int PIdx(int row) { return row < 8 ? 164 + row : row == 8 ? 162 : row < 12 ? 260 + (row - 9) : row == 12 ? 172 : 287; }
static std::map<uint32_t, Profile> s_prof;
static std::mutex s_profMx;   // s_prof lo tocan Present (factores, tarjeta, archivo) y el menu Fisico del juego (fisico.cpp)
static bool s_profLoaded = false, s_profDirty = false; static double s_profDirtyT = 0;
static std::string s_profStatus;
static Fac s_slotFac[kCharSlots][M_COUNT];
static std::atomic<unsigned char> s_slotUse[kCharSlots];
static std::atomic<bool> s_anyOwn{false};
static CharInfo s_slotInfo[kCharSlots]; static bool s_slotAlive[kCharSlots] = {}; static unsigned s_slotGen = 0;
static int s_ownOnScreen = 0;
static std::atomic<unsigned> s_skHit{0}, s_skMiss{0};
// los sliders que van por jugador (los de las tarjetas de musculatura menos los interruptores)
static const struct { int idx, comp; } kPlayerParams[] = {
    {164, 1}, {164, 2}, {164, 3}, {165, 0}, {165, 1}, {165, 2}, {165, 3}, {166, 0}, {167, 0}, {167, 1}, {167, 2}, {167, 3},
    {168, 0}, {168, 1}, {168, 2}, {168, 3}, {169, 0}, {169, 1}, {169, 2}, {169, 3}, {170, 0}, {170, 1}, {171, 0}, {171, 1}, {171, 2}, {171, 3},
    {162, 0}, {162, 1}, {162, 2}, {162, 3}, {166, 2}, {166, 3}, {170, 3},   // (24/10 g) detalles
    {260, 0}, {260, 1}, {260, 2}, {260, 3}, {261, 0}, {261, 1}, {261, 2}, {261, 3}, {262, 0}, {262, 1},   // (26/10) v7
    {172, 0},   // (26/10 b) definicion muscular por jugador (el shader la toma de la tabla de jugadores de skintone.cpp)
    {262, 2},   // (26/10) variante del torso y los brazos (robusto / atletico)
    {287, 0}, {287, 1}, {287, 2},   // (26/10) hombros: inclinacion, tipo, tamano
};
static bool IsPlayerParam(int idx, int comp) { for (auto& k : kPlayerParams) if (k.idx == idx && k.comp == comp) return true; return false; }
// rango de cada valor por jugador: la definicion puede ir a 0 (piel lisa); la variante del torso de 0.5 (robusto) a 1.5 (atletico)
static float ClampParam(int idx, int comp, float v) {
    if (idx == 262 && comp == 2) return std::clamp(v, 0.5f, 1.5f);
    if (idx == 287) return comp == 1 ? std::clamp(roundf(v), 1.0f, 4.0f) : std::clamp(v, 0.7f, 1.3f);   // (26/10) hombros
    return std::clamp(v, idx == 172 ? 0.0f : 0.3f, 3.0f);
}
static std::wstring ProfPath() { return g_cfg.baseDir + L"musculatura_jugadores.ini"; }
static void ProfFromGeneral(Profile& p) { for (int r = 0; r < kPRows; r++) for (int c = 0; c < 4; c++) p.v[r][c] = g_cfg.live[PIdx(r)][c]; }
// los parametros generales en el orden de los perfiles (P de ComputeFactors)
static void GeneralRows(float (*L)[4]) { for (int r = 0; r < kPRows; r++) memcpy(L[r], g_cfg.live[PIdx(r)], sizeof(float) * 4); }
static void ProfDirty() { s_profDirty = true; s_profDirtyT = Now(); }
static std::string TrimS(const std::string& x) {
    size_t a = x.find_first_not_of(" \t\r\n"), b = x.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : x.substr(a, b - a + 1);
}
static void LoadUserPresets();   // (26/10 b) tipos de cuerpo propios (mas abajo)
static void LoadProfiles() {
    s_prof.clear(); s_profLoaded = true;
    LoadUserPresets();
    std::ifstream f(ProfPath(), std::ios::binary);
    if (!f) return;
    std::string l; Profile* cur = nullptr;
    while (std::getline(f, l)) {
        l = TrimS(l);
        if (l.empty() || l[0] == ';') continue;
        if (l[0] == '[') {
            size_t e = l.find(']'); std::string id = TrimS(l.substr(1, e == std::string::npos ? std::string::npos : e - 1));
            char* end = nullptr; unsigned long v = strtoul(id.c_str(), &end, 10);
            cur = nullptr;
            if (!id.empty() && end && *end == 0 && v) {
                Profile p; for (auto& row : p.v) for (auto& x : row) x = 1.0f;
                cur = &(s_prof[(uint32_t)v] = p);
            }
            continue;
        }
        if (!cur) continue;
        size_t eq = l.find('='); if (eq == std::string::npos) continue;
        std::string k = TrimS(l.substr(0, eq)), val = TrimS(l.substr(eq + 1));
        if (k == "nombre") { cur->name = val; continue; }
        const char* comps = "xyzw"; const char* pc = k.empty() ? nullptr : strchr(comps, tolower((unsigned char)k[0]));
        if (!pc || k.size() < 2) continue;
        int idx = atoi(k.c_str() + 1), comp = (int)(pc - comps);
        if (PRow(idx) >= 0 && IsPlayerParam(idx, comp)) cur->v[PRow(idx)][comp] = ClampParam(idx, comp, (float)atof(val.c_str()));
    }
    Log("musculatura por jugador: %d jugadores con valores propios en musculatura_jugadores.ini", (int)s_prof.size());
}
static bool SaveProfiles() {
    std::ofstream f(ProfPath(), std::ios::binary | std::ios::trunc);
    if (!f) { s_profStatus = "No se pudo escribir musculatura_jugadores.ini"; return false; }
    f << "; PES21 FX - musculatura por jugador. Lo escribe el menu (Cuerpo > Musculatura por jugador).\r\n"
         "; [id] = id del jugador en el juego (en los que tienen cara real es el de la carpeta face\\real\\<id>).\r\n"
         "; Las claves son las de las tarjetas de musculatura (y164 = hombros, z164 = biceps, ...): 1 = como el juego; lo que\r\n"
         "; falta tambien vale 1 (x172 = definicion muscular propia). Los interruptores (x164, y166, z170) son los de PES21FX.ini.\r\n";
    for (auto& kv : s_prof) {
        f << "\r\n[" << kv.first << "]\r\n";
        if (!kv.second.name.empty()) f << "nombre = " << kv.second.name << "\r\n";
        for (auto& k : kPlayerParams) {
            char b[48]; snprintf(b, sizeof b, "%c%d = %g\r\n", "xyzw"[k.comp], k.idx, kv.second.v[PRow(k.idx)][k.comp]);
            f << b;
        }
    }
    s_profStatus = "Guardado en musculatura_jugadores.ini (" + std::to_string(s_prof.size()) + " jugadores)";
    return true;
}

// valor de un slider para un jugador: el suyo, o el general si no tiene valores propios (lo usa el menu Fisico del juego)
float MuscleProfileGet(uint32_t id, int idx, int comp) {
    if (PRow(idx) < 0 || comp < 0 || comp > 3) return 1.0f;
    std::lock_guard<std::mutex> lk(s_profMx);
    if (!s_profLoaded) LoadProfiles();
    auto it = id ? s_prof.find(id) : s_prof.end();
    return it != s_prof.end() ? it->second.v[PRow(idx)][comp] : g_cfg.live[idx][comp];
}
// cambia un valor del jugador (si no tenia valores propios, arrancan iguales a los generales); se guarda solo
void MuscleProfileSet(uint32_t id, const char* name, int idx, int comp, float v) {
    if (!id || PRow(idx) < 0 || comp < 0 || comp > 3) return;
    std::lock_guard<std::mutex> lk(s_profMx);
    if (!s_profLoaded) LoadProfiles();
    auto it = s_prof.find(id);
    if (it == s_prof.end()) { Profile p; ProfFromGeneral(p); if (name) p.name = name; it = s_prof.emplace(id, p).first; }
    else if (name && *name && it->second.name != name) it->second.name = name;
    it->second.v[PRow(idx)][comp] = ClampParam(idx, comp, v);   // la definicion puede ir a 0 (piel lisa); la variante 0.5 .. 1.5
    ProfDirty();
}

// ---------------------------------------------------------------- tipos de cuerpo (26/10)
// Un tipo de cuerpo pone todos los sliders de musculatura de un jugador (o los generales) en 1 = como el juego y despues
// los que nombra. "General" multiplica a los musculos, asi que los tipos casi solo usan "General" y algunos de forma.
// (26/10 b) tambien la definicion (x172) y los tipos propios: "Guardar como tipo de cuerpo" (tarjeta por jugador) los
// escribe en PES21FX\musculatura_tipos.ini y aparecen despues de los del mod en todas las listas (tambien en el juego).
struct PresetVal { int idx, comp; float v; };
struct MusPreset { const char* name; const char* desc; PresetVal v[16]; };
static const MusPreset kPresets[] = {
    { "Como el juego", "Todo en 1: el cuerpo que hizo Konami.", {} },
    { "Delgado", "Menos musculo y menos cintura.",
      { {167, 0, 0.90f}, {168, 3, 0.95f}, {171, 1, 0.96f}, {170, 0, 0.95f}, {171, 3, 0.97f}, {262, 2, 1.10f} } },
    { "Atl\xC3\xA9tico", "Un poco mas de musculo, abdomen marcado, cintura fina y musculos definidos.",
      { {167, 0, 1.06f}, {164, 1, 1.04f}, {165, 1, 1.04f}, {168, 1, 1.06f}, {171, 1, 0.97f}, {168, 3, 0.92f}, {261, 1, 1.04f}, {262, 1, 1.03f}, {172, 0, 1.25f}, {262, 2, 1.35f} } },
    { "Fuerte", "Mas musculo en todo el cuerpo, cuello y trapecio de luchador.",
      { {167, 0, 1.12f}, {167, 1, 1.06f}, {170, 0, 1.06f}, {171, 1, 1.03f}, {165, 3, 1.04f}, {171, 3, 1.03f}, {172, 0, 1.10f}, {262, 2, 0.90f} } },
    { "Corpulento", "Pesado: tronco y cintura anchos, poca definicion.",
      { {167, 0, 1.10f}, {168, 3, 1.15f}, {171, 1, 1.08f}, {171, 2, 1.06f}, {170, 0, 1.08f}, {168, 1, 0.97f}, {172, 0, 0.75f}, {262, 2, 0.55f} } },
    { "Culturista", "Mucho musculo: hombros redondos, espalda en V, trapecio alto, cintura fina y muy definido.",
      { {167, 0, 1.22f}, {164, 1, 1.06f}, {260, 0, 1.06f}, {260, 1, 1.06f}, {167, 1, 1.12f}, {261, 2, 1.08f}, {261, 1, 1.12f}, {261, 0, 1.06f},
        {171, 1, 0.97f}, {168, 3, 0.90f}, {262, 1, 1.06f}, {262, 0, 1.06f}, {171, 3, 1.05f}, {172, 0, 1.50f}, {262, 2, 1.50f} } },
};
static const int kNB = (int)(sizeof kPresets / sizeof kPresets[0]);   // tipos del mod
static const int kUserMax = 16;
struct UserPreset { char name[48]; float v[kPRows][4]; };
static UserPreset s_user[kUserMax]; static std::atomic<int> s_nUser{0}; static bool s_userLoaded = false;
static std::wstring UserPath() { return g_cfg.baseDir + L"musculatura_tipos.ini"; }
static void LoadUserPresets() {   // con s_profMx tomado
    s_userLoaded = true; int n = 0;
    std::ifstream f(UserPath(), std::ios::binary);
    if (f) {
        std::string l; UserPreset* cur = nullptr;
        while (std::getline(f, l)) {
            l = TrimS(l);
            if (l.empty() || l[0] == ';') continue;
            if (l[0] == '[') {
                size_t e = l.find(']'); std::string nm = TrimS(l.substr(1, e == std::string::npos ? std::string::npos : e - 1));
                cur = nullptr;
                if (!nm.empty() && n < kUserMax) {
                    cur = &s_user[n++]; snprintf(cur->name, sizeof cur->name, "%s", nm.c_str());
                    for (auto& row : cur->v) for (auto& x : row) x = 1.0f;
                }
                continue;
            }
            if (!cur) continue;
            size_t eq = l.find('='); if (eq == std::string::npos) continue;
            std::string k = TrimS(l.substr(0, eq)), val = TrimS(l.substr(eq + 1));
            const char* comps = "xyzw"; const char* pc = k.empty() ? nullptr : strchr(comps, tolower((unsigned char)k[0]));
            if (!pc || k.size() < 2) continue;
            int idx = atoi(k.c_str() + 1), comp = (int)(pc - comps);
            if (PRow(idx) >= 0 && IsPlayerParam(idx, comp)) cur->v[PRow(idx)][comp] = ClampParam(idx, comp, (float)atof(val.c_str()));
        }
    }
    s_nUser = n;
}
static bool SaveUserPresets() {   // con s_profMx tomado
    std::ofstream f(UserPath(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "; PES21 FX - tipos de cuerpo propios (los escribe el menu: Musculatura por jugador > Guardar como tipo de cuerpo).\r\n"
         "; [nombre] + las claves de la musculatura (como en musculatura_jugadores.ini): 1 = como el juego.\r\n";
    for (int i = 0; i < s_nUser.load(); i++) {
        f << "\r\n[" << s_user[i].name << "]\r\n";
        for (auto& k : kPlayerParams) { char b[48]; snprintf(b, sizeof b, "%c%d = %g\r\n", "xyzw"[k.comp], k.idx, s_user[i].v[PRow(k.idx)][k.comp]); f << b; }
    }
    return true;
}
int MusclePresetCount() { return kNB + s_nUser.load(); }
const char* MusclePresetName(int k) { return k >= 0 && k < kNB ? kPresets[k].name : k >= kNB && k < kNB + s_nUser.load() ? s_user[k - kNB].name : ""; }
const char* MusclePresetDesc(int k) { return k >= 0 && k < kNB ? kPresets[k].desc : "Tipo de cuerpo propio (PES21FX\\musculatura_tipos.ini)."; }
static float PresetValue(int k, int idx, int comp) {
    if (k >= kNB) { const int u = k - kNB; return u < s_nUser.load() && PRow(idx) >= 0 ? s_user[u].v[PRow(idx)][comp] : 1.0f; }
    for (const PresetVal& pv : kPresets[k].v) if (pv.idx == idx && pv.comp == comp && pv.v > 0.0f) return pv.v;
    return 1.0f;
}
static void PresetToProfile(Profile& p, int k) {   // con s_profMx tomado
    for (auto& pp : kPlayerParams) p.v[PRow(pp.idx)][pp.comp] = PresetValue(k, pp.idx, pp.comp);
    ProfDirty();
}
static int PresetOfProfile(const Profile& p) {   // con s_profMx tomado
    for (int k = 0; k < MusclePresetCount(); k++) {
        bool ok = true;
        for (auto& pp : kPlayerParams) if (fabsf(p.v[PRow(pp.idx)][pp.comp] - PresetValue(k, pp.idx, pp.comp)) > 0.004f) { ok = false; break; }
        if (ok) return k;
    }
    return -1;
}
// combo "Tipo de cuerpo" del menu (ImGui): devuelve el elegido o -1
static int PresetCombo(const char* label, int cur) {
    int pick = -1;
    if (ImGui::BeginCombo(label, cur >= 0 ? MusclePresetName(cur) : cur == -1 ? "A mano" : "Elegir...")) {
        for (int k = 0; k < MusclePresetCount(); k++) {
            if (k == kNB) ImGui::Separator();
            if (ImGui::Selectable(MusclePresetName(k), k == cur)) pick = k;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", MusclePresetDesc(k));
        }
        ImGui::EndCombo();
    }
    return pick;
}
// id 0 = los generales (el que llama tiene g_cfgMutex); si no, el perfil del jugador (se crea si hace falta)
void MusclePresetApply(uint32_t id, const char* name, int k) {
    if (k < 0 || k >= MusclePresetCount()) return;
    if (!id) { for (auto& pp : kPlayerParams) g_cfg.live[pp.idx][pp.comp] = PresetValue(k, pp.idx, pp.comp); return; }
    std::lock_guard<std::mutex> lk(s_profMx);
    if (!s_profLoaded) LoadProfiles();
    Profile& p = s_prof[id];
    if (name && *name) p.name = name;
    PresetToProfile(p, k);
    Log("musculatura: tipo de cuerpo \"%s\" para %s", MusclePresetName(k), name && *name ? name : "jugador");
}
// que tipo de cuerpo coincide con los valores actuales (-1 = ninguno: valores a mano)
int MusclePresetMatch(uint32_t id) {
    for (int k = 0; k < MusclePresetCount(); k++) {
        bool ok = true;
        for (auto& pp : kPlayerParams) if (fabsf(MuscleProfileGet(id, pp.idx, pp.comp) - PresetValue(k, pp.idx, pp.comp)) > 0.004f) { ok = false; break; }
        if (ok) return k;
    }
    return -1;
}
// (26/10 b) guarda los valores de un perfil como tipo de cuerpo propio (mismo nombre = se reemplaza); con s_profMx tomado
static std::string SaveAsPreset(const float (*v)[4], const char* nameIn) {
    std::string nm = TrimS(nameIn ? nameIn : "");
    for (auto& ch : nm) if (ch == '[' || ch == ']' || ch == '\r' || ch == '\n') ch = ' ';
    if (nm.empty()) return "Ponele un nombre al tipo de cuerpo";
    for (int k = 0; k < kNB; k++) if (nm == kPresets[k].name) return "Ese nombre es de un tipo del mod: elegi otro";
    int u = -1;
    for (int i = 0; i < s_nUser.load(); i++) if (nm == s_user[i].name) u = i;
    if (u < 0) { if (s_nUser.load() >= kUserMax) return "Ya hay 16 tipos propios: borra alguno"; u = s_nUser.load(); }
    snprintf(s_user[u].name, sizeof s_user[u].name, "%s", nm.c_str());
    memcpy(s_user[u].v, v, sizeof s_user[u].v);
    if (u == s_nUser.load()) s_nUser = u + 1;
    return SaveUserPresets() ? "Tipo de cuerpo \"" + nm + "\" guardado en musculatura_tipos.ini" : "No se pudo escribir musculatura_tipos.ini";
}
static void DeleteUserPreset(int u) {   // con s_profMx tomado
    const int n = s_nUser.load();
    if (u < 0 || u >= n) return;
    for (int i = u; i + 1 < n; i++) s_user[i] = s_user[i + 1];
    s_nUser = n - 1;
    SaveUserPresets();
}
// (26/10 b) definicion muscular propia de un jugador (0 = usa la general); la usa skintone.cpp para la tabla del shader
float MuscleDefinitionFor(uint32_t id) {
    if (!id) return 0.0f;
    std::lock_guard<std::mutex> lk(s_profMx);
    if (!s_profLoaded) LoadProfiles();
    auto it = s_prof.find(id);
    if (it == s_prof.end()) return 0.0f;
    const float v = it->second.v[12][0], gen = g_cfg.live[172][0] > 0.01f ? g_cfg.live[172][0] : 1.0f;
    return fabsf(v - gen) > 0.005f ? std::clamp(v, 0.05f, 3.0f) : 0.0f;
}

// (26/10) variante robusto / atletico de un jugador para los shaders (skintone.cpp la pone en la tabla de jugadores: filas 264..284):
// t = -1 robusto .. 1 atletico, el propio o el general; own = distinto del general. 0 con la musculatura apagada o "Ver como el juego".
float MuscleVariantT(uint32_t id, bool* own) {
    if (own) *own = false;
    if (g_cfg.live[164][0] < 0.5f || s_compare.load()) return 0.0f;
    const float gen = TvOf(g_cfg.live[262][2]);
    if (!id) return gen;
    std::lock_guard<std::mutex> lk(s_profMx);
    if (!s_profLoaded) LoadProfiles();
    auto it = s_prof.find(id);
    if (it == s_prof.end()) return gen;
    const float t = TvOf(it->second.v[11][2]);
    if (own) *own = fabsf(t - gen) > 0.004f;
    return t;
}

struct MzOwn { float pos[3]; const Fac* fac; float tv; };   // (26/10) personaje con valores propios (fac, tv = variante) o tapon (nullptr = general)
static const int kMzEnt = 32, kMzSets = 9;   // tienen que coincidir con pesMusPick (cloth_sim.hlsl)
static void MuscleClothPresent(const Fac* fac, bool on, const MzOwn* own, int nOwn, float useTv);   // (25/10) tabla para la fisica de la ropa (mas abajo)
static float s_slotTv[kCharSlots] = {};   // (26/10) variante de los personajes con valores propios (para la ropa)
// (26/10 b) respiracion por personaje (en Present): agitacion por la velocidad del nodo, fase y amplitud
static void UpdateDynamics() {
    const double t = Now();
    static double tPrev = 0; const float dt = tPrev > 0 ? (float)std::clamp(t - tPrev, 0.0, 0.1) : 0.0f; tPrev = t;
    s_dynT = (float)fmod(t, 3600.0);
    const float kb = s_dynK[1], kag = std::clamp(g_cfg.live[263][2], 0.0f, 2.0f);
    for (int i = 0; i < kCharSlots; i++) {
        DynState& d = s_dyn[i];
        float pos[3];
        const bool ok = s_slotAlive[i] && s_slotInfo[i].node && CharNodeWorldPos(s_slotInfo[i].node, pos);
        float target = 0.0f;
        if (ok && d.posOk && dt > 0.0f) {
            const float dx = pos[0] - d.pos[0], dz = pos[2] - d.pos[2], v = sqrtf(dx * dx + dz * dz) / dt;
            if (v < 15.0f) target = std::clamp(kag * (v - 1.5f) / 6.0f, 0.0f, 1.0f);   // mas de 15 m/s: corte o teletransporte
        }
        d.posOk = ok; if (ok) memcpy(d.pos, pos, sizeof pos);
        if (!ok) target = 0.0f;
        const float tau = target > d.ex ? 3.0f : 14.0f;   // se agita rapido y se calma despacio
        if (dt > 0.0f) d.ex += (target - d.ex) * (1.0f - expf(-dt / tau));
        if (d.phase == 0.0f) d.phase = 1.3f * (float)i;
        d.phase = fmodf(d.phase + 6.2831853f * (0.22f + 0.50f * d.ex) * dt, 6.2831853f);   // 13 a 43 respiraciones por minuto
        d.breath = kb * (0.0025f + 0.0045f * d.ex) * sinf(d.phase);
    }
    for (int i = 0; i < kDynFb; i++) {   // sin personaje conocido: respiracion tranquila
        DynState& d = s_dyn[kCharSlots + i];
        d.ex = 0.0f; d.breath = kb * 0.0025f * sinf(6.2831853f * 0.22f * s_dynT + 1.7f * (float)i);
    }
}
static void UpdateFactors() {
    float L[kPRows][4]; GeneralRows(L);   // 164..171, 162 y (26/10) 260..262
    const bool on = L[0][0] > 0.5f && !s_compare.load(), ropa = g_cfg.live[166][1] > 0.5f, trans = g_cfg.live[170][2] > 0.5f, vec = g_cfg.live[255][0] > 0.5f;
    Fac f[M_COUNT];
    ComputeFactors(L, on, ropa, trans, vec, f);
    s_on = on;
    for (int i = 0; i < M_COUNT; i++) s_fac[i] = f[i];
    s_dynK[0] = std::clamp(g_cfg.live[263][0], 0.0f, 2.0f); s_dynK[1] = std::clamp(g_cfg.live[263][1], 0.0f, 2.0f);
    s_ropaOn = ropa; s_sleeveK = std::clamp(g_cfg.live[255][1], 0.0f, 1.5f);
    s_dynOn = on && (s_dynK[0] > 0.0f || s_dynK[1] > 0.0f);
    // por jugador: un juego de factores por cada personaje en pantalla que tenga valores propios
    std::lock_guard<std::mutex> lk(s_profMx);
    if (!s_profLoaded) LoadProfiles();
    unsigned g = CharTableGen();
    if (g != s_slotGen) { s_slotGen = g; CharSnapshot(s_slotInfo, s_slotAlive); }
    if (s_dynOn) UpdateDynamics();
    // en el menu Fisico del juego, un personaje que no paso por HumanList (si el modelo de Editar se arma por otro lado)
    // usa los valores del jugador que se esta editando
    uint32_t editId = 0;
    if (!FisicoEditing(&editId) || !MusclePhysiqueActive()) editId = 0;
    bool any = false; int own = 0, newest = -1;   // newest = el personaje armado mas reciente (el que se ve en el editor)
    for (int i = 0; i < kCharSlots; i++) {
        const Profile* p = nullptr;
        if (s_slotAlive[i] && s_slotInfo[i].known && (newest < 0 || s_slotInfo[i].seq > s_slotInfo[newest].seq)) newest = i;
        uint32_t id = !s_slotAlive[i] ? 0 : s_slotInfo[i].known ? s_slotInfo[i].id : editId;
        if (on && id) { auto it = s_prof.find(id); if (it != s_prof.end()) p = &it->second; }
        if (!p) { s_slotUse[i].store(0, std::memory_order_relaxed); continue; }
        Fac fp[M_COUNT];
        ComputeFactors(p->v, on, ropa, trans, vec, fp);
        for (int k = 0; k < M_COUNT; k++) s_slotFac[i][k] = fp[k];
        s_slotTv[i] = TvOf(p->v[11][2]);
        s_slotUse[i].store(1, std::memory_order_release);
        any = true; own++;
    }
    s_anyOwn = any; s_ownOnScreen = own;
    if (s_profDirty && Now() - s_profDirtyT > 0.6) { s_profDirty = false; SaveProfiles(); }
    // (25/10) tabla para la fisica de la ropa (una sola para todos los que simula el mod): en el menu Fisico, los valores del
    // jugador que se edita; en los demas menus (editor, sin cancha) los del ultimo jugador armado (el que se ve), si tiene
    // valores propios; en el partido, los generales (los jugadores con valores propios usan los generales para la ropa)
    {
        Fac fe[M_COUNT]; const Fac* use = s_fac;
        float useTv = on ? TvOf(g_cfg.live[262][2]) : 0.0f;   // (26/10) variante del juego general (la del editado / el ultimo armado)
        if (editId) { auto it = s_prof.find(editId); if (it != s_prof.end()) { ComputeFactors(it->second.v, on, ropa, trans, vec, fe); use = fe; if (on) useTv = TvOf(it->second.v[11][2]); } }
        else if (newest >= 0 && g_cfg.live[255][3] > 0.5f && s_slotUse[newest].load(std::memory_order_relaxed)) { use = s_slotFac[newest]; useTv = s_slotTv[newest]; }
        // (26/10) en el partido, cada jugador con valores propios lleva su juego de zonas con su posicion (el simulador elige el del
        // personaje mas cercano a la prenda); los demas personajes cerca de alguno de ellos van como "tapones" con el general
        static MzOwn own[kMzEnt]; int nOwn = 0;
        if (use == s_fac && on) {
            static float pos[kCharSlots][3]; static bool hasPos[kCharSlots];
            for (int i = 0; i < kCharSlots; i++) hasPos[i] = s_slotAlive[i] && s_slotInfo[i].node && CharNodeWorldPos(s_slotInfo[i].node, pos[i]);
            int nSet = 0;
            for (int i = 0; i < kCharSlots && nOwn < kMzEnt && nSet < kMzSets - 1; i++)
                if (hasPos[i] && s_slotUse[i].load(std::memory_order_acquire)) {
                    MzOwn& o = own[nOwn++]; memcpy(o.pos, pos[i], sizeof o.pos); o.fac = s_slotFac[i]; o.tv = s_slotTv[i]; nSet++;
                }
            const int nReal = nOwn;
            for (int i = 0; i < kCharSlots && nOwn < kMzEnt && nReal > 0; i++) {
                if (!hasPos[i] || s_slotUse[i].load(std::memory_order_relaxed)) continue;
                bool nearOwn = false;
                for (int j = 0; j < nReal && !nearOwn; j++) {
                    const float dx = pos[i][0] - own[j].pos[0], dy = pos[i][1] - own[j].pos[1], dz = pos[i][2] - own[j].pos[2];
                    nearOwn = dx * dx + dy * dy + dz * dz < 4.0f;
                }
                if (nearOwn) { MzOwn& o = own[nOwn++]; memcpy(o.pos, pos[i], sizeof o.pos); o.fac = nullptr; o.tv = useTv; }
            }
        }
        MuscleClothPresent(use, on, own, nOwn, useTv);
    }
}

// ---------------------------------------------------------------- la ropa sigue al cuerpo en la fisica (25/10)
// Pecho, espalda, abdomen, trapecio, hombros y gluteos se hacen corriendo huesos del cuerpo que la ropa no usa: la piel sale y
// la remera / el short se quedan donde estaban, y el cuerpo los atraviesa. El simulador del mod de ropa (cloth_sim.hlsl, con
// las mejoras de PES21 FX) lee esta tabla en t121 (la enlaza hooks.cpp cuando el mod pone ese shader) y corre el reposo de
// cada particula de tela lo mismo que la piel en esa zona. Zonas = capsulas en el modelo en reposo (lado izquierdo; el shader
// espeja X), las del resaltado del menu Fisico; el shader las ensancha un poco (la tela esta 1-2 cm afuera de la piel).
// Tabla: [0] = (zonas, fuerza z255, -, -); por zona (A, radio), (B, tipo 0 corre V / 1 crece V.x alrededor), (V, en la manga).
struct MusZone { int mus; float ax, ay, az, bx, by, bz, r; int kind; float sleeve; };
static const MusZone kZones[] = {
    { M_PECT,     0.035f, 1.375f, 0.165f,  0.145f, 1.385f, 0.130f,  0.080f, 0, 0.0f },   // pecho
    { M_CLAV,     0.030f, 1.470f, 0.115f,  0.130f, 1.470f, 0.100f,  0.050f, 0, 0.0f },   // pecho alto
    { M_UNDERARM, 0.150f, 1.400f, 0.070f,  0.170f, 1.330f, 0.050f,  0.045f, 0, 0.3f },   // axila
    { M_RIB,      0.140f, 1.320f, 0.080f,  0.150f, 1.200f, 0.070f,  0.050f, 0, 0.0f },   // costado
    { M_LAT,      0.150f, 1.400f, -0.050f, 0.130f, 1.200f, -0.040f, 0.065f, 0, 0.0f },   // dorsal
    { M_SCAP,     0.080f, 1.420f, -0.100f, 0.100f, 1.370f, -0.100f, 0.075f, 0, 0.0f },   // omoplato
    { M_TRAP,     0.040f, 1.570f, -0.025f, 0.150f, 1.500f, -0.020f, 0.055f, 0, 0.0f },   // trapecio
    { M_COLLAR,   0.030f, 1.520f, 0.040f,  0.080f, 1.500f, 0.020f,  0.045f, 0, 0.0f },   // base del cuello
    { M_BACK,     0.035f, 1.180f, -0.100f, 0.035f, 1.040f, -0.090f, 0.075f, 0, 0.0f },   // espalda baja
    { M_LUMB,     0.070f, 1.220f, -0.090f, 0.070f, 1.100f, -0.090f, 0.050f, 0, 0.0f },   // lumbar
    { M_ABS,      0.045f, 1.260f, 0.150f,  0.045f, 1.040f, 0.160f,  0.060f, 0, 0.0f },   // abdominales
    { M_OBL,      0.150f, 1.220f, 0.070f,  0.150f, 1.040f, 0.075f,  0.055f, 0, 0.0f },   // oblicuos
    { M_BELLY,    0.000f, 1.130f, 0.150f,  0.000f, 1.090f, 0.155f,  0.090f, 0, 0.0f },   // panza
    { M_GLUTE,    0.085f, 0.970f, -0.060f, 0.085f, 0.930f, -0.060f, 0.085f, 0, 0.0f },   // gluteo
    { M_ADD,      0.050f, 0.860f, 0.060f,  0.100f, 0.660f, 0.080f,  0.050f, 0, 0.0f },   // aductor (adentro del muslo)
    { M_DELT,     0.205f, 1.455f, 0.030f,  0.215f, 1.445f, 0.030f,  0.075f, 0, 0.3f },   // hombro: corrimiento por "ancho de hombros"
    { M_DELT,     0.180f, 1.440f, 0.026f,  0.180f, 1.440f, 0.026f,  0.090f, 1, 0.3f },   // hombro: crece alrededor de la articulacion
};
static const int kMzMax = 24;
static_assert(sizeof(kZones) / sizeof(kZones[0]) <= kMzMax, "zonas de mas para la tabla de la ropa");
// (26/10) filas: 0..72 general (como antes: [0] = (zonas, fuerza), zonas en 1 + 3k); [73] = (personajes, juegos, distancia
// maxima); [74 + i] = (posicion del personaje en el mundo, juego: 0 general / 1..8 propio); juego s desde 106 + (s - 1) * 73
static const int kMzSetRows = 1 + 3 * kMzMax, kMzRows = kMzSetRows + 1 + kMzEnt + (kMzSets - 1) * kMzSetRows;
static_assert(kMzSetRows == 73 && kMzSetRows + 1 + kMzEnt == 106, "la tabla de la ropa no coincide con cloth_sim.hlsl");
static std::atomic<ID3D11ShaderResourceView*> s_mzSRV{nullptr};
static ID3D11Buffer* s_mzBuf = nullptr; static ID3D11Device* s_mzDev = nullptr;
static float s_mzData[kMzRows][4] = {}; static bool s_mzSent = false;
static int s_mzZones = 0, s_mzOwnLog = -1; static float s_mzMax = 0.0f;
ID3D11ShaderResourceView* MuscleClothSRV() { return s_mzSRV.load(std::memory_order_acquire); }
// las zonas de un juego de factores desde la fila "base" (cabecera) de la tabla; devuelve cuantas. (26/10) z de la cabecera = variante
// del torso de ese juego (cloth_sim: pesTorsoField, la tela con la misma forma grande que la piel)
static int FillZones(const Fac* fac, float (*d)[4], float& mx, float tv) {
    int n = 0;
    for (const MusZone& z : kZones) {
        const Fac& f = fac[z.mus];
        float v[3] = { 0, 0, 0 };
        if (z.kind == 0) { v[0] = f.t[0]; v[1] = f.t[1]; v[2] = f.t[2]; }
        else v[0] = 0.65f * f.e;   // el deltoides crece 0.8 / 0.8 / 0.35 en los ejes del hueso: en promedio 0.65
        const float mag = z.kind == 0 ? sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) : fabsf(v[0]) * z.r;
        if (mag < 1e-5f || n >= kMzMax) continue;
        mx = (std::max)(mx, mag);
        float* A = d[1 + 3 * n]; float* B = d[2 + 3 * n]; float* V = d[3 + 3 * n];
        A[0] = z.ax; A[1] = z.ay; A[2] = z.az; A[3] = z.r;
        B[0] = z.bx; B[1] = z.by; B[2] = z.bz; B[3] = (float)z.kind;
        V[0] = v[0]; V[1] = v[1]; V[2] = v[2]; V[3] = z.sleeve;
        n++;
    }
    d[0][0] = (float)n; d[0][2] = tv;
    return n;
}
static void MuscleClothPresent(const Fac* fac, bool on, const MzOwn* own, int nOwn, float useTv) {
    ID3D11Device* dev = g_dev.dev; ID3D11DeviceContext* imm = g_dev.imm;
    if (!dev || !imm) return;
    if (s_mzDev != dev) {   // (re)crear en el dispositivo del juego; lo viejo se deja vivo (otro hilo podria tenerlo enlazado)
        s_mzDev = dev; s_mzBuf = nullptr; s_mzSent = false;
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth = sizeof(s_mzData); bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = 16;
        ID3D11Buffer* b = nullptr;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &b)) || !b) { Log("ropa: no se pudo crear la tabla de la musculatura"); return; }
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = DXGI_FORMAT_UNKNOWN; sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.FirstElement = 0; sd.Buffer.NumElements = kMzRows;
        ID3D11ShaderResourceView* v = nullptr;
        if (FAILED(dev->CreateShaderResourceView(b, &sd, &v)) || !v) { b->Release(); Log("ropa: no se pudo crear la vista de la tabla de la musculatura"); return; }
        s_mzBuf = b; s_mzSRV.store(v, std::memory_order_release);
    }
    if (!s_mzBuf) return;
    static float d[kMzRows][4]; memset(d, 0, sizeof d);
    int n = 0, sets = 0; float mx = 0.0f;
    const float str = std::clamp(g_cfg.live[255][2], 0.0f, 2.0f);
    if (on && str > 0.0f) {
        n = FillZones(fac, d, mx, useTv);
        float (*ent)[4] = d + kMzSetRows + 1;
        for (int i = 0; i < nOwn && i < kMzEnt; i++) {
            int st = 0;
            if (own[i].fac && sets < kMzSets - 1) { st = ++sets; FillZones(own[i].fac, d + kMzSetRows + 1 + kMzEnt + (st - 1) * kMzSetRows, mx, own[i].tv); }
            ent[i][0] = own[i].pos[0]; ent[i][1] = own[i].pos[1]; ent[i][2] = own[i].pos[2]; ent[i][3] = (float)st;
        }
        d[kMzSetRows][0] = (float)(std::min)(nOwn, kMzEnt); d[kMzSetRows][1] = (float)sets; d[kMzSetRows][2] = 1.0f;
    }
    d[0][1] = str;
    if (s_mzSent && !memcmp(d, s_mzData, sizeof d)) return;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(imm->Map(s_mzBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, d, sizeof d); imm->Unmap(s_mzBuf, 0);
    memcpy(s_mzData, d, sizeof d); s_mzSent = true;
    if (n != s_mzZones || fabsf(mx - s_mzMax) > 0.002f || sets != s_mzOwnLog)
        Log("ropa: la tela sigue a la musculatura en %d zonas (empuje mayor %.1f mm, fuerza %.2f; %d jugadores con valores propios en la cancha, %d personajes en la lista)",
            n, mx * 1000.0f, str, sets, (std::min)(nOwn, kMzEnt));
    s_mzZones = n; s_mzMax = mx; s_mzOwnLog = sets;
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

// ---------------------------------------------------------------- musculos vivos en el Eval (26/10 b)
static bool CurXf(void* bone, Xf& x) { void* p = nullptr; return ReadPtr((const char*)bone + 0x18, p) && ReadXf(p, x); }   // + 0x18 = transformacion actual
static void XAxis(const float* q, float* o) { static const float v[3] = { 1.0f, 0.0f, 0.0f }; QRot(q, v, o); }
static float AngleOf(const float* a, const float* b) { return acosf(std::clamp(a[0] * b[0] + a[1] * b[1] + a[2] * b[2], -1.0f, 1.0f)); }
// 0 = como en reposo (o mas estirado), 1 = doblado del todo
static float ElbowFlex(const DynState& d, int sd) {
    if (!d.upOk[sd] || !d.foOk[sd]) return 0.0f;
    const float now = AngleOf(d.upX[sd], d.foX[sd]), rest = AngleOf(d.upR[sd], d.foR[sd]);
    return std::clamp((now - rest) / (std::max)(1.75f - rest, 0.4f), 0.0f, 1.0f);   // ~100 grados mas que el reposo = del todo
}
static float KneeFlex(const DynState& d, int sd) {
    if (!d.thOk[sd] || !d.shOk[sd]) return 0.0f;
    const float now = AngleOf(d.thX[sd], d.shX[sd]), rest = AngleOf(d.thR[sd], d.shR[sd]);
    return std::clamp((now - rest) / 1.7f, 0.0f, 1.0f);
}
static float ArmRaise(const DynState& d, int sd) {   // el brazo se aleja del costado (hacia adelante, atras o arriba), en el espacio del pecho
    if (!d.upOk[sd] || !d.chOk) return 0.0f;
    static const float down[3] = { 0.0f, -1.0f, 0.0f };
    float ln[3], lr[3], dn[3];
    QRotInv(d.chQ, d.upX[sd], ln); QRotInv(d.chR, d.upR[sd], lr); QRotInv(d.chR, down, dn);
    return std::clamp((AngleOf(ln, dn) - AngleOf(lr, dn)) / 1.4f, 0.0f, 1.0f);
}
static int s_iArm = -2, s_iFore = -1, s_iThigh = -1, s_iKnee = -1, s_iPect = -1;
static void DynFindClasses() {
    if (s_iArm != -2) return;
    auto find = [](const char* n) { for (int i = 0; i < kN; i++) if (!strcmp(s_cls[i].name, n)) return i; return -1; };
    s_iFore = find("antebrazo"); s_iThigh = find("muslo"); s_iKnee = find("rodilla"); s_iPect = find("pectoral"); s_iArm = find("brazo");
}
static std::atomic<unsigned> s_dynMeas{0}, s_dynMeasFail{0};
static void DynAdd(int i, void* self, const float* a2, const void* skel, int slot, Fac& fc) {
    DynFindClasses();
    const MusCls& C = s_cls[i];
    DynState& d = s_dyn[DynIndex(slot, skel)];
    Xf rb{}, rt{}; void* bone = nullptr; void* targ = nullptr;
    const bool okB = ReadPtr((const char*)self + 0x58, bone) && ReadXf((const char*)bone + 0x40, rb);
    const bool okT = ReadPtr((const char*)self + 0x60, targ) && ReadXf((const char*)targ + 0x40, rt);
    int src = 0;
    const float side = SideFor(C, self, okB ? &rb : nullptr, okT ? &rt : nullptr, a2, &src);
    const int sd = side > 0.0f ? 0 : 1;
    // medicion: el destino de estas restricciones es el hueso principal del miembro (o el pecho)
    if (okT && (i == s_iArm || i == s_iFore || i == s_iThigh || i == s_iKnee || i == s_iPect)) {
        Xf cur{};
        if (CurXf(targ, cur)) {
            s_dynMeas.fetch_add(1, std::memory_order_relaxed);
            if (i == s_iPect) { memcpy(d.chQ, cur.q, sizeof d.chQ); memcpy(d.chR, rt.q, sizeof d.chR); d.chOk = 1; }
            else {
                float (*X)[3] = i == s_iArm ? d.upX : i == s_iFore ? d.foX : i == s_iThigh ? d.thX : d.shX;
                float (*R)[3] = i == s_iArm ? d.upR : i == s_iFore ? d.foR : i == s_iThigh ? d.thR : d.shR;
                volatile unsigned char* ok = i == s_iArm ? d.upOk : i == s_iFore ? d.foOk : i == s_iThigh ? d.thOk : d.shOk;
                XAxis(cur.q, X[sd]); XAxis(rt.q, R[sd]); ok[sd] = 1;
            }
        } else s_dynMeasFail.fetch_add(1, std::memory_order_relaxed);
    }
    const float kc = s_dynK[0], b = d.breath, ex = d.ex;
    switch (C.mus) {
    case M_ARM: if (kc > 0.0f) { const float g = kc * 0.08f * ElbowFlex(d, sd); fc.e += g; fc.t[2] += kArmR * 0.5f * g; } break;   // biceps adelante
    case M_SLEEVE: if (kc > 0.0f && s_ropaOn) { const float g = s_sleeveK * kc * 0.08f * ElbowFlex(d, sd); fc.e += g; fc.t[2] += kArmR * 0.5f * g; } break;
    case M_FORE: if (kc > 0.0f) fc.e += kc * 0.04f * ElbowFlex(d, sd); break;
    case M_DELT: if (kc > 0.0f) fc.e += kc * 0.07f * ArmRaise(d, sd); break;
    case M_TRAP: if (kc > 0.0f) Push(fc, 0.0f, 1.0f, 0.0f, 0.006f * kc, ArmRaise(d, sd)); Push(fc, 0.0f, 1.0f, 0.0f, 0.5f * b * ex, 1.0f); break;
    case M_THIGH: if (kc > 0.0f) { const float k = KneeFlex(d, sd); fc.e += kc * 0.035f * k; fc.t[2] -= kThighR * 0.5f * kc * 0.07f * k; } break;   // isquios atras
    case M_HEM: if (kc > 0.0f && s_ropaOn) { const float k = KneeFlex(d, sd); fc.e += 0.7f * kc * 0.035f * k; fc.t[2] -= 0.8f * kThighR * 0.5f * kc * 0.07f * k; } break;
    case M_CALF: if (kc > 0.0f) fc.e += kc * 0.03f * KneeFlex(d, sd); break;
    case M_PECT: Push(fc, 0.10f, 0.35f, 1.0f, b, 1.0f); break;            // respiracion: el pecho sube y sale
    case M_CLAV: Push(fc, 0.0f, 1.0f, 0.6f, 0.8f * b, 1.0f); break;
    case M_RIB: Push(fc, 1.0f, 0.0f, 0.2f, 0.5f * b, 1.0f); break;       // las costillas se abren
    case M_LAT: Push(fc, 1.0f, 0.0f, -0.3f, 0.35f * b, 1.0f); break;
    case M_SCAP: Push(fc, 0.0f, 0.3f, -1.0f, 0.3f * b, 1.0f); break;
    case M_ABS: Push(fc, 0.0f, 0.0f, 1.0f, 0.45f * b, 1.0f); break;      // la panza acompana (diafragma)
    case M_BELLY: Push(fc, 0.0f, -0.10f, 1.0f, 0.55f * b, 1.0f); break;
    default: break;
    }
}
template<int I> static void __fastcall hk_Eval(void* self, void* a2, float* out) {
    s_orig[I](self, a2, out);
    const unsigned nc = s_calls[I].fetch_add(1, std::memory_order_relaxed);
    if (!self || !out) return;
    const int mus = s_cls[I].mus;
    Fac fc = s_fac[mus];
    // por jugador: el esqueleto de la restriccion (+ 0x10) -> personaje -> sus factores (si tiene valores propios)
    const bool own = s_anyOwn.load(std::memory_order_relaxed), dyn = s_dynOn;
    void* skel = nullptr; int slot = -1;
    if (own || dyn || (nc & 255) == 0) {
        skel = *(void* const volatile*)((const char*)self + 0x10);
        slot = CharSlotForSkel(skel);
        if ((nc & 255) == 0) (slot >= 0 ? s_skHit : s_skMiss).fetch_add(1, std::memory_order_relaxed);
        if (own && slot >= 0 && s_slotUse[slot].load(std::memory_order_acquire)) fc = s_slotFac[slot][mus];
    }
    if (dyn) DynAdd(I, self, (const float*)a2, skel, slot, fc);   // (26/10 b) contraccion y respiracion
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

// ---------------------------------------------------------------- menu Fisico del juego (24/10)
// menu::MenuEditPlBodyPhysiqueSetting (Editar > Jugadores > Fisico): vtable 0x142708848 (COL 0x142cd5c40); [0] = 0x141fa9d60 es
// el paso por cuadro (comun a los menus de Editar; solo se cambia en la vtable de Fisico) y *(this + 0x78) + 0x50 = activo.
// Mientras esta en pantalla, Insert abre una ventana chica con la musculatura de ese jugador y abajo aparece un aviso.
static const uintptr_t kPhysVt = 0x142708848ull, kPhysCol = 0x142cd5c40ull, kPhysStep = 0x141fa9d60ull;
static const uintptr_t kBodyVt = 0x142704dd8ull, kBodyCol = 0x142cd50d8ull;   // menu::MenuEditPlBodySetting (el Fisico de Editar)
typedef void* (__fastcall* PhysStepFn)(void* self, void* a2, void* a3, void* a4);
static PhysStepFn o_physStep = nullptr;
static std::atomic<long long> s_physLast{0}, s_physEnter{0};   // QPC del ultimo paso activo / de la entrada
static long long Qpc() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
static double QpcSec(long long d) { static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }(); return (double)d / (double)f.QuadPart; }
static void* __fastcall hk_PhysStep(void* self, void* a2, void* a3, void* a4) {
    bool active = false;
    void* st = nullptr; unsigned char fl = 0;
    if (self && ReadPtr((const char*)self + 0x78, st) && SafeCopy(&fl, (const char*)st + 0x50, 1)) active = fl != 0;
    if (active) {
        long long now = Qpc(), last = s_physLast.load(std::memory_order_relaxed);
        if (!last || QpcSec(now - last) > 0.5) s_physEnter.store(now, std::memory_order_relaxed);
        s_physLast.store(now, std::memory_order_relaxed);
    }
    return o_physStep(self, a2, a3, a4);
}
bool MusclePhysiqueActive() { long long l = s_physLast.load(); return l && QpcSec(Qpc() - l) < 0.4; }
double MusclePhysiqueAge() { long long l = s_physLast.load(); return l ? QpcSec(Qpc() - l) : 1e9; }   // (24/10) segundos desde el ultimo paso activo del menu Fisico
double MusclePhysiqueSince() { long long e = s_physEnter.load(); return e ? QpcSec(Qpc() - e) : 1e9; }

static void Install() {
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("musculatura: parches en memoria apagados (motor = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("musculatura: exe distinto, sin ganchos"); return; }
    std::string bad;
    if (!DiagOn(3)) Log("musculatura: huesos de musculo sin enganchar (w173 = 0, Diagnostico > Ganchos nuevos)");
    for (int i = 0; i < kN && DiagOn(3); i++) {
        void** vt = (void**)s_cls[i].vt;
        if (!Readable(vt - 1, 6 * sizeof(void*)) || (uintptr_t)vt[-1] != s_cls[i].col || (uintptr_t)vt[4] != s_cls[i].eval) { bad += std::string(" ") + s_cls[i].name; continue; }
        DWORD old = 0;
        if (!VirtualProtect(&vt[4], sizeof(void*), PAGE_READWRITE, &old)) { bad += std::string(" ") + s_cls[i].name + "(protect)"; continue; }
        s_orig[i] = (EvalFn)vt[4];
        InterlockedExchangePointer(&vt[4], Hooks()[i]);
        VirtualProtect(&vt[4], sizeof(void*), old, &old);
        s_ok++;
    }
    Log("musculatura v6: %d de %d huesos de musculo enganchados%s%s", s_ok, kN, bad.empty() ? "" : "; no coinciden:", bad.c_str());
    if (DiagOn(1)) {   // menus Fisico del juego (Editar > Jugadores > Fisico = MenuEditPlBodySetting; tambien MenuEditPlBodyPhysiqueSetting)
        int ok = 0;
        const uintptr_t vts[2][2] = { { kBodyVt, kBodyCol }, { kPhysVt, kPhysCol } };
        for (auto& v : vts) {
            void** vt = (void**)v[0]; DWORD old = 0;
            if (Readable(vt - 1, 2 * sizeof(void*)) && (uintptr_t)vt[-1] == v[1] && (uintptr_t)vt[0] == kPhysStep &&
                VirtualProtect(&vt[0], sizeof(void*), PAGE_READWRITE, &old)) {
                o_physStep = (PhysStepFn)kPhysStep;
                InterlockedExchangePointer(&vt[0], (void*)&hk_PhysStep);
                VirtualProtect(&vt[0], sizeof(void*), old, &old);
                ok++;
            }
        }
        Log("musculatura: menu Fisico del juego %s", ok ? "enganchado (Insert ahi = musculatura de ese jugador)" : "NO (vtable distinta)");
    }
    FisicoInstall();   // filas de musculatura dentro del menu Fisico del juego (fisico.cpp)
}

void MuscleHookPresent() {
    if (!s_tried) Install();
    if (!s_ok) return;
    UpdateFactors();
    FisicoHighlightPresent();   // (24/10) brillo que late sobre el musculo de la fila elegida (fisico.cpp)
    {   // entrada / salida del menu Fisico
        static bool s_was = false;
        bool a = MusclePhysiqueActive();
        if (a != s_was) { s_was = a; Log("musculatura: menu Fisico del juego %s", a ? "abierto" : "cerrado"); }
    }
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
    if (s_dynOn) {   // (26/10 b) musculos vivos: un personaje de muestra
        unsigned ms = s_dynMeas.exchange(0), mf = s_dynMeasFail.exchange(0);
        int k = -1; float best = -1.0f;
        for (int i = 0; i < kCharSlots; i++) if (s_slotAlive[i] && s_dyn[i].upOk[0] && s_dyn[i].ex > best) { best = s_dyn[i].ex; k = i; }
        if (k >= 0) {
            const DynState& d = s_dyn[k];
            Log("musculatura viva (contraccion %.2f, respiracion %.2f): %u huesos medidos, %u sin transformacion actual | %s: codo %.0f/%.0f %%, rodilla %.0f/%.0f %%, brazo afuera %.0f/%.0f %% (izq/der), agitacion %.0f %%, respiracion %.1f mm",
                s_dynK[0], s_dynK[1], ms, mf, s_slotInfo[k].name[0] ? s_slotInfo[k].name : "personaje", 100.0f * ElbowFlex(d, 0), 100.0f * ElbowFlex(d, 1),
                100.0f * KneeFlex(d, 0), 100.0f * KneeFlex(d, 1), 100.0f * ArmRaise(d, 0), 100.0f * ArmRaise(d, 1), 100.0f * d.ex, 1000.0f * d.breath);
        } else Log("musculatura viva (contraccion %.2f, respiracion %.2f): %u huesos medidos, %u sin transformacion actual, ningun personaje conocido medido", s_dynK[0], s_dynK[1], ms, mf);
    }
    {
        unsigned hit = s_skHit.exchange(0), miss = s_skMiss.exchange(0);
        if (hit || miss || !s_prof.empty())
            Log("musculatura por jugador: %d guardados, %d personajes en pantalla con valores propios; esqueletos de personaje conocido: %u de %u muestras",
                (int)s_prof.size(), s_ownOnScreen, hit, hit + miss);
    }
    s_t = t;
}

// ---------------------------------------------------------------- tarjeta "Musculatura por jugador" (menu, 24/10)
// Se dibuja dentro de DrawGroup (g_cfgMutex tomado; el hilo es el de Present, el mismo que UpdateFactors).
static std::string U8(const std::string& x) {   // el ini puede venir en Latin-1; los nombres del juego ya son UTF-8
    bool ok = true;
    for (size_t i = 0; i < x.size() && ok;) {
        unsigned char c = (unsigned char)x[i];
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > x.size()) { ok = false; break; }
        for (int k = 1; k < n; k++) if (((unsigned char)x[i + k] >> 6) != 2) ok = false;
        i += n;
    }
    if (ok) return x;
    std::string o;
    for (unsigned char c : x) { if (c < 0x80) o += (char)c; else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); } }
    return o;
}
static uint32_t s_selId = 0; static bool s_selManual = false; static unsigned s_selSeq = 0;
// el ultimo jugador armado (con nombre si hay): para el aviso del menu Fisico
std::string MuscleLatestPlayerName() {
    const CharInfo* best = nullptr;
    for (int i = 0; i < kCharSlots; i++) {
        if (!s_slotAlive[i] || !s_slotInfo[i].known || !s_slotInfo[i].id) continue;
        if (!best || s_slotInfo[i].seq > best->seq) best = &s_slotInfo[i];
    }
    if (!best) return "";
    if (best->name[0]) return best->name;
    return "id " + std::to_string(best->id);
}
// ---------------------------------------------------------------- ventana del menu Fisico con el diseno nuevo (26/10 c, musculo_ui.cpp)
// Los metodos se llaman con s_profMx y g_cfgMutex tomados (no usar MuscleProfileGet / MusclePresetMatch: toman s_profMx).
class FisicoModel : public MusUiModel {
public:
    std::vector<MusUiPlayer> rows; int sel = 0;
    Profile* Own() { if (rows.empty()) return nullptr; auto it = s_prof.find(rows[sel].id); return it == s_prof.end() ? nullptr : &it->second; }
    Profile& Make() {
        Profile* p = Own(); if (p) return *p;
        Profile np; ProfFromGeneral(np); np.name = rows[sel].name; Profile& r = s_prof[rows[sel].id] = np; ProfDirty();
        rows[sel].own = true; return r;
    }
    int PlayerCount() override { return (int)rows.size(); }
    const MusUiPlayer& Player(int i) override { return rows[i]; }
    int Selected() override { return sel; }
    void Select(int i) override { if (i >= 0 && i < (int)rows.size()) { sel = i; s_selId = rows[i].id; s_selManual = true; } }
    bool HasOwn() override { return Own() != nullptr; }
    void MakeOwn() override { Make(); s_profStatus = "Valores propios para " + rows[sel].name + ": arrancan iguales a los generales"; }
    void ClearOwn() override { if (!rows.empty()) { s_prof.erase(rows[sel].id); rows[sel].own = false; ProfDirty(); s_profStatus = "Quitados los valores propios de " + rows[sel].name; } }
    void CopyGeneral() override { Profile& p = Make(); const std::string nm = p.name; ProfFromGeneral(p); p.name = nm; ProfDirty(); }
    float Get(int idx, int comp) override { Profile* p = Own(); return p && PRow(idx) >= 0 ? p->v[PRow(idx)][comp] : g_cfg.live[idx][comp]; }
    float General(int idx, int comp) override { return g_cfg.live[idx][comp]; }
    void Set(int idx, int comp, float v) override { if (PRow(idx) < 0 || rows.empty()) return; Make().v[PRow(idx)][comp] = ClampParam(idx, comp, v); ProfDirty(); }
    bool MuscleOn() override { return g_cfg.live[164][0] > 0.5f; }
    void SetMuscleOn(bool on) override { const float b = g_cfg.base[164][0]; g_cfg.live[164][0] = on ? (b > 0.5f ? b : 1.0f) : 0.0f; FisicoCfgDirty(); }
    float Global(int idx, int comp) override { return g_cfg.live[idx][comp]; }
    void SetGlobal(int idx, int comp, float v) override { g_cfg.live[idx][comp] = v; FisicoCfgDirty(); }
    int TypeCount() override { return MusclePresetCount(); }
    int BuiltinTypes() override { return kNB; }
    const char* TypeName(int k) override { return MusclePresetName(k); }
    const char* TypeDesc(int k) override { return MusclePresetDesc(k); }
    int TypeMatch() override {
        if (Profile* p = Own()) return PresetOfProfile(*p);
        for (int k = 0; k < MusclePresetCount(); k++) {
            bool ok = true;
            for (auto& pp : kPlayerParams) if (fabsf(g_cfg.live[pp.idx][pp.comp] - PresetValue(k, pp.idx, pp.comp)) > 0.004f) { ok = false; break; }
            if (ok) return k;
        }
        return -1;
    }
    void ApplyType(int k) override { if (rows.empty()) return; PresetToProfile(Make(), k); s_profStatus = std::string("Tipo de cuerpo ") + MusclePresetName(k) + " para " + rows[sel].name; }
    std::string SaveAsType(const char* name) override {
        if (Profile* p = Own()) return SaveAsPreset(p->v, name);
        float L[kPRows][4]; GeneralRows(L); return SaveAsPreset(L, name);
    }
    void DeleteUserType(int k) override { if (k >= kNB) { s_profStatus = std::string("Borrado el tipo de cuerpo \"") + MusclePresetName(k) + "\""; DeleteUserPreset(k - kNB); } }
    std::vector<uint32_t> others; std::vector<std::string> otherNames;
    void BuildOthers() {
        others.clear(); otherNames.clear();
        for (auto& kv : s_prof) {
            if (!rows.empty() && kv.first == rows[sel].id) continue;
            others.push_back(kv.first);
            char lb[96]; snprintf(lb, sizeof lb, "%s  (id %u)", kv.second.name.empty() ? "(sin nombre)" : kv.second.name.c_str(), kv.first);
            otherNames.push_back(U8(lb));
        }
    }
    int OtherCount() override { return (int)others.size(); }
    const char* OtherName(int i) override { return otherNames[i].c_str(); }
    void CopyFromOther(int i) override {
        auto it = s_prof.find(others[i]); if (it == s_prof.end()) return;
        Profile& p = Make(); const std::string nm = p.name; memcpy(p.v, it->second.v, sizeof p.v); p.name = nm; ProfDirty();
        s_profStatus = "Copiados los valores de " + (it->second.name.empty() ? std::to_string(it->first) : it->second.name);
    }
    void Highlight(int idx, int comp) override { FisicoHighlightRequest(idx, comp); }
    void Compare(bool hold) override { static bool prev = false; if (hold != prev) { prev = hold; MuscleCompare(hold); } }
    std::string& Status() override { return s_profStatus; }
};
void MuscleDrawFisicoWindow(ImFont* bold, ImFont* big) {
    std::lock_guard<std::mutex> lk(s_profMx);
    if (!s_profLoaded) LoadProfiles();
    if (!CharTrackOk()) { ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1), "No se pudo seguir a los jugadores del motor (ver 'personajes' en PES21FX.log)."); return; }
    static FisicoModel m;
    m.rows.clear();
    // jugadores en pantalla: uno por id (el armado mas reciente); con nombre primero
    struct R { uint32_t id; std::string name; unsigned seq; };
    std::vector<R> rs; bool anyNamed = false;
    for (int i = 0; i < kCharSlots; i++) {
        if (!s_slotAlive[i] || !s_slotInfo[i].known || !s_slotInfo[i].id) continue;
        const CharInfo& c = s_slotInfo[i]; bool found = false;
        for (auto& r : rs) if (r.id == c.id) { found = true; if (c.seq > r.seq) r.seq = c.seq; if (r.name.empty() && c.name[0]) r.name = c.name; }
        if (!found) rs.push_back({ c.id, c.name, c.seq });
        anyNamed |= c.name[0] != 0;
    }
    uint32_t editId = 0;
    if (FisicoEditing(&editId) && editId) {   // en el menu Fisico: el jugador que se edita, aunque su modelo no haya pasado por HumanList
        bool found = false; for (auto& r : rs) if (r.id == editId) found = true;
        if (!found) rs.push_back({ editId, std::string(FisicoEditName()), 0xffffffffu });
    }
    if (anyNamed) rs.erase(std::remove_if(rs.begin(), rs.end(), [](const R& r) { return r.name.empty(); }), rs.end());
    std::sort(rs.begin(), rs.end(), [](const R& a, const R& b) { return a.name != b.name ? a.name < b.name : a.id < b.id; });
    const R* latest = nullptr; for (auto& r : rs) if (!latest || r.seq > latest->seq) latest = &r;
    if (latest && latest->seq != s_selSeq) { s_selSeq = latest->seq; if (!s_selManual) s_selId = latest->id; }
    if (editId) { s_selId = editId; s_selManual = false; }
    m.sel = 0;
    for (auto& r : rs) {
        MusUiPlayer p; p.id = r.id; p.name = U8(r.name.empty() ? std::to_string(r.id) : r.name); p.own = s_prof.count(r.id) != 0;
        if (r.id == s_selId) m.sel = (int)m.rows.size();
        m.rows.push_back(p);
    }
    m.BuildOthers();
    MuscleUiDraw(m, bold, big);
}

void MuscleDrawPlayerPanel() {
    std::lock_guard<std::mutex> lk(s_profMx);
    if (!s_profLoaded) LoadProfiles();
    const ImVec4 warn(0.95f, 0.55f, 0.35f, 1), good(0.45f, 0.85f, 0.55f, 1);
    if (!CharTrackOk()) {
        ImGui::TextColored(warn, "No se pudo seguir a los jugadores del motor (exe distinto o parches apagados: ver 'personajes' en PES21FX.log).");
        return;
    }
    if (g_cfg.live[164][0] < 0.5f) ImGui::TextColored(warn, "La musculatura esta apagada: prende \"Musculatura (prueba)\" para ver los cambios.");
    // jugadores en pantalla: uno por id (el armado mas reciente); con nombre primero
    struct Row { uint32_t id; std::string name; unsigned seq; int slot; };
    std::vector<Row> rows;
    bool anyNamed = false;
    for (int i = 0; i < kCharSlots; i++) {
        if (!s_slotAlive[i] || !s_slotInfo[i].known || !s_slotInfo[i].id) continue;
        const CharInfo& c = s_slotInfo[i];
        bool found = false;
        for (auto& r : rows) if (r.id == c.id) { found = true; if (c.seq > r.seq) { r.seq = c.seq; r.slot = i; } if (r.name.empty() && c.name[0]) r.name = c.name; }
        if (!found) rows.push_back({ c.id, c.name, c.seq, i });
        anyNamed |= c.name[0] != 0;
    }
    if (anyNamed) rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Row& r) { return r.name.empty(); }), rows.end());
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.name != b.name ? a.name < b.name : a.id < b.id; });
    // el seleccionado sigue al ultimo jugador armado (p. ej. el que se elige en Editar), salvo que se haya elegido a mano
    const Row* latest = nullptr;
    for (auto& r : rows) if (!latest || r.seq > latest->seq) latest = &r;
    if (latest && latest->seq != s_selSeq) { s_selSeq = latest->seq; if (!s_selManual) s_selId = latest->id; }
    const Row* sel = nullptr;
    for (auto& r : rows) if (r.id == s_selId) sel = &r;
    if (!sel) { s_selManual = false; sel = latest; s_selId = sel ? sel->id : 0; }
    auto label = [](const Row& r) {
        char b[160]; snprintf(b, sizeof b, "%s  (id %u)%s", r.name.empty() ? "(sin nombre)" : r.name.c_str(), r.id, s_prof.count(r.id) ? "  - valores propios" : "");
        return std::string(b);
    };
    if (!sel) {
        ImGui::TextWrapped("No hay jugadores en pantalla. Entra a Editar > Jugadores y elegi uno (el que se ve aparece aca solo), o abri este menu durante un partido o una repeticion para elegir cualquiera de la cancha.");
    } else {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
        if (ImGui::BeginCombo("Jugador", U8(label(*sel)).c_str())) {
            for (auto& r : rows) {
                bool is = r.id == sel->id;
                if (ImGui::Selectable(U8(label(r)).c_str(), is)) { s_selId = r.id; s_selManual = true; }
                if (is) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Los jugadores que el juego tiene armados ahora. Se elige solo el ultimo que aparecio (en Editar, el que estas editando).");
        auto it = s_prof.find(sel->id);
        if (it == s_prof.end()) {
            ImGui::TextWrapped("Usa los valores generales (los de las tarjetas de musculatura).");
            if (ImGui::Button("Darle valores propios")) {
                Profile p; ProfFromGeneral(p); p.name = sel->name; s_prof[sel->id] = p; ProfDirty();
                s_profStatus = "Valores propios para " + (sel->name.empty() ? std::to_string(sel->id) : sel->name) + ": arrancan iguales a los generales";
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copia los valores generales a este jugador; despues se cambian aca abajo sin tocar al resto.");
            ImGui::SameLine(); ImGui::SetNextItemWidth(170);
            const int pk = PresetCombo("Darle un tipo de cuerpo", -2);
            if (pk >= 0) {
                Profile& np = s_prof[sel->id]; np.name = sel->name; PresetToProfile(np, pk);
                s_profStatus = std::string("Tipo de cuerpo ") + kPresets[pk].name + " para " + (sel->name.empty() ? std::to_string(sel->id) : sel->name);
            }
        } else {
            Profile& p = it->second;
            if (!sel->name.empty() && p.name != sel->name) { p.name = sel->name; ProfDirty(); }
            ImGui::TextColored(good, "Tiene valores propios: se ven al instante y se guardan solos.");
            if (ImGui::Button("Copiar los generales")) { ProfFromGeneral(p); ProfDirty(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Le pone los valores de las tarjetas generales (sigue teniendo valores propios).");
            ImGui::SameLine();
            bool erase = ImGui::Button("Quitar sus valores");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vuelve a usar los valores generales (borra los suyos del archivo).");
            if (erase) { s_prof.erase(it); ProfDirty(); s_profStatus = "Quitados los valores propios de " + (sel->name.empty() ? std::to_string(sel->id) : sel->name); }
            else {
                ImGui::SetNextItemWidth(170);
                const int pk = PresetCombo("Tipo de cuerpo", PresetOfProfile(p));
                if (pk >= 0) PresetToProfile(p, pk);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pone todos sus sliders de un tipo de cuerpo (despues se afinan a mano). 'Como el juego' = todo en 1.");
                // (26/10 b) copiar de otro jugador con valores propios / guardar este cuerpo como tipo propio
                if (s_prof.size() > 1) {
                    ImGui::SameLine(); ImGui::SetNextItemWidth(190);
                    if (ImGui::BeginCombo("##copiar", "Copiar de otro jugador...")) {
                        for (auto& kv : s_prof) {
                            if (kv.first == sel->id) continue;
                            char lb[96]; snprintf(lb, sizeof lb, "%s  (id %u)", kv.second.name.empty() ? "(sin nombre)" : kv.second.name.c_str(), kv.first);
                            if (ImGui::Selectable(U8(lb).c_str())) { const std::string nm = p.name; memcpy(p.v, kv.second.v, sizeof p.v); p.name = nm; ProfDirty();
                                s_profStatus = "Copiados los valores de " + (kv.second.name.empty() ? std::to_string(kv.first) : kv.second.name); }
                        }
                        ImGui::EndCombo();
                    }
                }
                static char s_tipoNom[48] = "";
                ImGui::SetNextItemWidth(170);
                ImGui::InputTextWithHint("##tipo", "nombre del tipo", s_tipoNom, sizeof s_tipoNom);
                ImGui::SameLine();
                if (ImGui::Button("Guardar como tipo de cuerpo")) s_profStatus = SaveAsPreset(p.v, s_tipoNom);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Guarda todos los valores de este jugador como un tipo de cuerpo propio (musculatura_tipos.ini): aparece en las listas de tipos, tambien en el menu Fisico del juego.");
                if (s_nUser.load() > 0) {
                    ImGui::SameLine(); ImGui::SetNextItemWidth(150);
                    if (ImGui::BeginCombo("##borrartipo", "Borrar un tipo propio...")) {
                        int del = -1;
                        for (int u = 0; u < s_nUser.load(); u++) if (ImGui::Selectable(s_user[u].name)) del = u;
                        ImGui::EndCombo();
                        if (del >= 0) { s_profStatus = std::string("Borrado el tipo de cuerpo \"") + s_user[del].name + "\""; DeleteUserPreset(del); }
                    }
                }
                const float w = ImGui::GetContentRegionAvail().x;
                for (auto& g : g_cfg.groups) {
                    if (!g.special.empty() || g.title.rfind("Musculatura", 0) != 0) continue;
                    bool head = false;
                    for (auto& sl : g.sliders) {
                        if (!sl.ext.empty() || !sl.options.empty() || !IsPlayerParam(sl.idx, sl.comp)) continue;
                        if (!head) {
                            std::string t = g.title.rfind("Musculatura: ", 0) == 0 ? g.title.substr(13) : std::string("General");
                            if (!t.empty()) t[0] = (char)toupper((unsigned char)t[0]);
                            ImGui::SeparatorText(U8(t).c_str()); head = true;
                        }
                        float& val = p.v[PRow(sl.idx)][sl.comp];
                        const float gen = g_cfg.live[sl.idx][sl.comp];
                        ImGui::PushID(sl.idx * 4 + sl.comp);
                        bool diff = fabsf(val - gen) > 1e-4f;
                        if (diff) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.45f, 1));
                        ImGui::TextUnformatted(U8(sl.label).c_str());
                        if (diff) ImGui::PopStyleColor();
                        if (!sl.help.empty()) {
                            ImGui::SameLine(); ImGui::TextDisabled("(?)");
                            if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(380); ImGui::TextUnformatted(U8(sl.help).c_str()); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
                        }
                        ImGui::SetNextItemWidth(w - 70);
                        if (ImGui::SliderFloat("##p", &val, sl.lo, sl.hi, sl.fmt.c_str())) ProfDirty();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("<")) { val = gen; ProfDirty(); }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Volver al valor general (%.2f)", gen);
                        ImGui::PopID();
                    }
                }
            }
        }
    }
    if (!s_prof.empty()) {
        ImGui::Spacing();
        char hb[96]; snprintf(hb, sizeof hb, "Jugadores con valores propios (%d)###musjug", (int)s_prof.size());
        if (ImGui::TreeNode(hb)) {
            uint32_t del = 0;
            for (auto& kv : s_prof) {
                ImGui::PushID((int)kv.first);
                ImGui::Text("%s  (id %u)", kv.second.name.empty() ? "(sin nombre)" : U8(kv.second.name).c_str(), kv.first);
                ImGui::SameLine();
                if (ImGui::SmallButton("Quitar")) del = kv.first;
                ImGui::PopID();
            }
            if (del) { s_prof.erase(del); ProfDirty(); }
            ImGui::TreePop();
        }
    }
    if (ImGui::SmallButton("Releer el archivo")) { LoadProfiles(); s_profStatus = "musculatura_jugadores.ini releido (" + std::to_string(s_prof.size()) + " jugadores)"; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vuelve a leer PES21FX\\musculatura_jugadores.ini (si lo editaste a mano).");
    if (!s_profStatus.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", U8(s_profStatus).c_str()); }
}

} // namespace fx
