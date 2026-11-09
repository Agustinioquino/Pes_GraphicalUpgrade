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
//   350 (ini): x boca por huesos (1 = si; 0 = por shader como antes), y = lo escribe la DLL (1 = activo, 2 = ademas ojos y parpados por huesos: esqueletos con la
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
// (02/11) hash = + 0x10 del hueso del motor (fijo en todas las caras: volcado /mnt/project-files/face/cara_49_huesos.txt); los
// huesos con hash se reconocen SOLO por el hash: el globo del ojo y los dos parpados estan a 1..3 mm uno del otro y el reposo
// cambia de cara a cara, asi que por posicion se confundian
struct BoneDef { const char* name; float x, y, z; float jaw; uint32_t hash = 0; };
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
    { "skf_eyelid_t_l",          0.0280f, 1.6968f, 0.1380f, 0.0f, 0xC15DB1B9u },   // cara 22 (el mas alto del par)
    { "skf_eyelid_t_r",         -0.0280f, 1.6968f, 0.1380f, 0.0f, 0xC9E134E6u },   // cara 25
    { "skf_eyelid_b_l",          0.0280f, 1.6958f, 0.1380f, 0.0f, 0x65E49A52u },   // cara 23
    { "skf_eyelid_b_r",         -0.0280f, 1.6958f, 0.1380f, 0.0f, 0x0769EE8Cu },   // cara 24
    { "skf_brow_i_l",            0.0157f, 1.7129f, 0.1718f, 0.0f },
    { "skf_brow_i_r",           -0.0157f, 1.7129f, 0.1718f, 0.0f },
    { "skf_brow_o_l",            0.0413f, 1.7149f, 0.1573f, 0.0f },
    { "skf_brow_o_r",           -0.0413f, 1.7149f, 0.1573f, 0.0f },
    { "skf_glabella",            0.0000f, 1.7038f, 0.1697f, 0.0f },
    { "skf_eye_l",               0.0315f, 1.6966f, 0.1406f, 0.0f, 0xB9CDBDE1u },   // cara 26: globo del ojo (eye.fmdl paleta [0], peso 1)
    { "skf_eye_r",              -0.0318f, 1.6968f, 0.1410f, 0.0f, 0x721A023Fu },   // cara 27
    // (08/11, Agustin: "me gusta que toque los parpados para un resultado realista, en realidad cuantos mas huesos mejor") los 4 huesos
    // del CUELLO que la piel de la cara y del cuello si pesan (face_rig_111320: neckback 0.58, scm 0.31, clavicle 0.64) y el mod no
    // movia; nombres sacados del hash (tools/foxhash.py): 33 neckback, 34 scm (esternocleidomastoideo), 35 / 36 tendones del cuello
    { "dsk_neckback",            0.0000f, 1.6204f, 0.0443f, 0.0f, 0x3AA8F459u },
    { "dsk_scm",                 0.0000f, 1.5812f, 0.0430f, 0.0f, 0x1D27B492u },
    { "dsk_clavicle_l",          0.0140f, 1.5000f, 0.0961f, 0.0f, 0xE9DD4049u },
    { "dsk_clavicle_r",         -0.0140f, 1.5000f, 0.0961f, 0.0f, 0x2B429207u },
    { "sk_head",                 0.0000f, 1.6401f, 0.0543f, 0.0f },
    { "sk_neck",                 0.0000f, 1.5321f, 0.0335f, 0.0f },
};
static const int kND = (int)(sizeof(kDef) / sizeof(kDef[0]));
// (07/11) clips .gani (gani.cpp): hueso del .ask -> kDef, por nombre; y lo que el clip aporta a Secondary (pomulos / bajo-ojo) y a
// EyesFrame (parpados), que escriben esos huesos por su cuenta
static int s_defToAsk[64]; static bool s_defToAskOk = false;
static void DefToAsk() { for (int d = 0; d < kND; d++) { s_defToAsk[d] = -1; for (int b = 0; b < 31; b++) { const char* n = GaniAskBone(b); if (n && strcmp(n, kDef[d].name) == 0) { s_defToAsk[d] = b; break; } } } s_defToAskOk = true; }
static float s_ganiSec[4][3]; static bool s_ganiSecOn = false;   // 18..21, ejes de la cabeza (m)
static float s_ganiLid[2]; static bool s_ganiLidOn = false;
static float s_extraLid[2]; static bool s_extraLidOn = false;   // (08/11) parpados de vida / esfuerzo / concentracion (filas 395 / 396), misma convencion      // parpado de arriba / abajo: giro extra (rad, + = cierra el de arriba / baja el de abajo)
static void GaniQToHead(const float* q, float* o) { o[0] = -q[2]; o[1] = q[0]; o[2] = -q[1]; o[3] = q[3]; }   // ejes del .ask (x arriba, y atras, z derecha) -> cabeza (x izquierda, y arriba, z adelante)
static void GaniPToHead(const float* p, float* o) { o[0] = -p[2] * 0.01f; o[1] = p[0] * 0.01f; o[2] = -p[1] * 0.01f; }   // cm -> m
static void QScale(const float* q, float w, float* o) {   // giro por w (eje-angulo)
    const float s = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    if (s < 1e-6f) { o[0] = o[1] = o[2] = 0; o[3] = 1; return; }
    const float h = atan2f(s, q[3]) * w, k = sinf(h) / s; o[0] = q[0] * k; o[1] = q[1] * k; o[2] = q[2] * k; o[3] = cosf(h);
}
// (08/11, Agustin: "utiliza un metodo seguro, practico y avanzado de transicion") dos herramientas, las dos acotadas (nunca se disparan):
// RESORTE CRITICAMENTE AMORTIGUADO (SmoothDamp, Unity / Game Programming Gems 4): sigue un valor con velocidad continua, sin rebote, con
// tiempo de respuesta tau; mejor que el filtro exponencial porque arranca y frena suave (C1) y no se queda atras tanto con el mismo tau.
static void Damp(float& x, float& v, float target, float tau, float dt) {
    const float w = 2.0f / (std::max)(tau, 1e-3f), xw = w * dt;
    const float e = 1.0f / (1.0f + xw + 0.48f * xw * xw + 0.235f * xw * xw * xw);
    const float ch = x - target, tmp = (v + w * ch) * dt;
    v = (v - w * tmp) * e; x = target + (ch + tmp) * e;
}
// INERTIALIZACION (Bollo, GDC 2018; Unreal / Ubisoft For Honor): en una transicion no se mezclan dos poses durante un rato, se toma la
// DIFERENCIA entre lo que se venia mostrando (con su velocidad) y la pose nueva, y esa diferencia se apaga con un polinomio de grado 5
// que llega exactamente a cero con velocidad y aceleracion cero en t1 (sin rebote; si la velocidad va al reves del salto, t1 se acorta
// para que no se pase). Asi el clip nuevo se reproduce entero desde el primer cuadro y solo se "hereda" el impulso del anterior.
struct Inert { float x0 = 0, v0 = 0, a0 = 0, A = 0, B = 0, C = 0, t1 = 0; };
static void InertStart(Inert& I, float x0, float v0, float t1) {
    if (fabsf(x0) < 1e-9f && fabsf(v0) < 1e-9f) { I = Inert(); return; }
    if (v0 * x0 < 0.0f) t1 = (std::min)(t1, -5.0f * x0 / v0);   // la velocidad ya va hacia el destino: no pasarse
    t1 = (std::max)(t1, 1e-3f);
    const float t2 = t1 * t1, t3 = t2 * t1, t4 = t3 * t1, t5 = t4 * t1;
    const float a0 = (-8.0f * v0 * t1 - 20.0f * x0) / t2;
    I.x0 = x0; I.v0 = v0; I.a0 = a0; I.t1 = t1;
    I.A = -(a0 * t2 + 6.0f * v0 * t1 + 12.0f * x0) / (2.0f * t5);
    I.B = (3.0f * a0 * t2 + 16.0f * v0 * t1 + 30.0f * x0) / (2.0f * t4);
    I.C = -(3.0f * a0 * t2 + 12.0f * v0 * t1 + 20.0f * x0) / (2.0f * t3);
}
static float InertEval(const Inert& I, float t) {
    if (t >= I.t1 || I.t1 <= 0.0f) return 0.0f;
    const float t2 = t * t, t3 = t2 * t;
    return I.A * t3 * t2 + I.B * t2 * t2 + I.C * t3 + 0.5f * I.a0 * t2 + I.v0 * t + I.x0;
}
static void QLog(const float* q, float* v) {   // cuaternion unitario -> vector eje x angulo (rad)
    float w = q[3]; const float s2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2]; const float s = sqrtf(s2);
    float sg = 1.0f; if (w < 0.0f) { w = -w; sg = -1.0f; }
    if (s < 1e-7f) { v[0] = v[1] = v[2] = 0.0f; return; }
    const float ang = 2.0f * atan2f(s, w), k = sg * ang / s; v[0] = q[0] * k; v[1] = q[1] * k; v[2] = q[2] * k;
}
static void QExp(const float* v, float* q) {
    const float a = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (a < 1e-7f) { q[0] = v[0] * 0.5f; q[1] = v[1] * 0.5f; q[2] = v[2] * 0.5f; q[3] = 1.0f; return; }
    const float k = sinf(0.5f * a) / a; q[0] = v[0] * k; q[1] = v[1] * k; q[2] = v[2] * k; q[3] = cosf(0.5f * a);
}
static float GaniAngZ(const float* q) { return 2.0f * atan2f(q[2], q[3]); }   // giro alrededor del eje z del .ask (derecha): el que abre la mandibula y los parpados
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
// (02/11, Agustin con 2 capturas: "en la vista original la boca respeta la forma de los labios al abrirse, con el shader no")
// al abrir la boca por huesos (corrimiento x apertura, ejes de la cabeza) los labios hacen el ovalo de la boca abierta del juego:
// las comisuras se juntan hacia el medio y van un poco adelante, los costados del labio de abajo se redondean hacia adentro y
// el de arriba acompana apenas; sin esto el giro de la mandibula estiraba las comisuras hacia afuera (boca cuadrada)
static const float kOpen[][3] = {
    { 0, 0, 0 },                       // skf_jaw
    { -0.0012f,  0.0f,     0.0008f },  // lip_b_l: hacia el medio y adelante (labio de abajo redondo)
    {  0.0012f,  0.0f,     0.0008f },  // lip_b_r
    {  0.0f,     0.0f,     0.0010f },  // lip_b_c
    { 0, 0, 0 },                       // doublechin
    { -0.0030f, -0.0004f,  0.0010f },  // lip_s_l: la comisura se junta 3 mm
    {  0.0030f, -0.0004f,  0.0010f },  // lip_s_r
    { -0.0010f, -0.0012f,  0.0f },     // cheek_s_l: la mejilla de abajo se estira hacia abajo
    {  0.0010f, -0.0012f,  0.0f },     // cheek_s_r
    // (04/11, Agustin: "es poco realista la forma de la boca al abrirse") el labio de arriba sube un poco y se despega de los dientes
    { -0.0008f,  0.0008f,  0.0004f },  // lip_t_l
    {  0.0f,     0.0010f,  0.0005f },  // lip_t_c
    {  0.0008f,  0.0008f,  0.0004f },  // lip_t_r
};
static const float* OpenOf(int d) { static const float z[3] = { 0, 0, 0 }; return d < (int)(sizeof(kOpen) / sizeof(kOpen[0])) ? kOpen[d] : z; }
// (03/11, Agustin: "deberias crear un slider para enojo, sonreir, asombrado, hablar") GESTOS DEL MOD por huesos (fila 355: x enojo,
// y sonrisa, z asombro, w hablar). Corrimiento de cada hueso en metros, ejes de la cabeza (x a la izquierda del jugador, y arriba,
// z adelante), por 1.0 de slider. Las mejillas (18/19) de la sonrisa las pone Secondary (x348). El asombro y el habla abren ademas
// la mandibula (como "Abrir la boca")
struct PoseOff { int d; float a[3], s[3], u[3]; };   // a = enojo, s = sonrisa, u = asombro
static const PoseOff kPose[] = {
    {  1, { 0, 0, 0 },                   {  0.0010f, -0.0004f, 0 },        { 0, 0, 0 } },                    // lip_b_l
    {  2, { 0, 0, 0 },                   { -0.0010f, -0.0004f, 0 },        { 0, 0, 0 } },                    // lip_b_r
    {  5, {  0.0004f, -0.0022f, 0 },     {  0.0030f,  0.0030f, -0.0010f }, { -0.0015f, 0, 0.0005f } },       // lip_s_l: comisura
    {  6, { -0.0004f, -0.0022f, 0 },     { -0.0030f,  0.0030f, -0.0010f }, {  0.0015f, 0, 0.0005f } },       // lip_s_r
    {  7, { 0, -0.0006f, 0 },            {  0.0010f,  0.0015f, 0 },        { 0, 0, 0 } },                    // cheek_s_l
    {  8, { 0, -0.0006f, 0 },            { -0.0010f,  0.0015f, 0 },        { 0, 0, 0 } },                    // cheek_s_r
    {  9, { 0,  0.0010f, 0 },            {  0.0004f,  0.0008f, 0 },        { 0, 0, 0 } },                    // lip_t_l
    { 10, { 0,  0.0008f, 0 },            { 0, 0.0004f, 0 },                { 0, 0, 0 } },                    // lip_t_c
    { 11, { 0,  0.0010f, 0 },            { -0.0004f,  0.0008f, 0 },        { 0, 0, 0 } },                    // lip_t_r
    {  3, { 0, 0.0006f, 0.0003f },       { 0, 0, 0 },                      { 0, -0.0005f, 0 } },             // lip_b_c: enojo = labios apretados
    {  4, { 0, 0.0008f, 0.0003f },       { 0, 0, 0 },                      { 0, -0.0006f, 0 } },             // doublechin: enojo = menton tenso
    { 13, { 0, 0.0008f, -0.0002f },      { 0, 0.0004f, 0 },                { 0, 0, 0 } },                    // nose: enojo arruga la nariz
    { 14, {  0.0003f, 0.0010f, 0 },      {  0.0003f,  0.0006f, 0 },        { 0, 0, 0 } },                    // nosewing_l
    { 15, { -0.0003f, 0.0010f, 0 },      { -0.0003f,  0.0006f, 0 },        { 0, 0, 0 } },                    // nosewing_r
    { 16, {  0.0002f, 0.0008f, 0 },      {  0.0005f,  0.0010f, -0.0004f }, { 0, 0, 0 } },                    // nasolabialfold_t_l
    { 17, { -0.0002f, 0.0008f, 0 },      { -0.0005f,  0.0010f, -0.0004f }, { 0, 0, 0 } },                    // nasolabialfold_t_r
    { 26, { -0.0020f, -0.0030f, 0.0005f }, { 0, 0, 0 },                    {  0.0003f, 0.0040f, 0 } },       // brow_i_l: ceno abajo y al medio
    { 27, {  0.0020f, -0.0030f, 0.0005f }, { 0, 0, 0 },                    { -0.0003f, 0.0040f, 0 } },       // brow_i_r
    { 28, { 0, -0.0012f, 0 },            { 0, 0, 0 },                      { 0, 0.0035f, 0 } },              // brow_o_l
    { 29, { 0, -0.0012f, 0 },            { 0, 0, 0 },                      { 0, 0.0035f, 0 } },              // brow_o_r
    { 30, { 0, -0.0020f, -0.0004f },     { 0, 0, 0 },                      { 0, 0.0020f, 0 } },              // glabella
};
static void PoseOf(int d, float ang, float smi, float sur, float* o) {
    o[0] = o[1] = o[2] = 0.0f;
    // (04/11, Agustin con una foto de Messi riendo: "al aumentar la sonrisa ... el angulo de las puntas de la sonrisa no suben tanto,
    // abre mas la boca") pasando 1.0 de Sonrisa las comisuras casi no suben mas (un cuarto); se abren a lo ancho igual y la boca se
    // abre (la mandibula, abajo en ExprBonesFrame)
    const float smY = smi <= 1.0f ? smi : 1.0f + 0.25f * (smi - 1.0f);
    for (const PoseOff& p : kPose) if (p.d == d) { for (int k = 0; k < 3; k++) o[k] = p.a[k] * ang + p.s[k] * ((k == 1 && (d == 5 || d == 6)) ? smY : smi) + p.u[k] * sur; return; }
}
static float s_poseSmile = 0.0f;   // para Secondary (mejillas)
// (04/11, Agustin: "que la sonrisa mueva toda la cara, asi como tambien en el enojado y demas") los sliders de gestos tambien para
// Secondary (pomulos 18/19 y bajo-ojo 20/21): enojo, asombro, grito, puchero; y el perfil de la cara de este cuadro
static float s_poseG[6] = {};   // 0 neutra (z356), 1 enojo, 2 sonrisa, 3 asombro, 4 grito, 5 puchero
static const void* s_poseProf = nullptr;
// (04/11, Agustin: "para messi una expresion de gritar custom ... para una mayor similitud a la vida real"; "la cara neutra me gusta
// la del juego pero si puedes modificarla poco") GESTOS NUEVOS (fila 356: x grito, y puchero) y PERFILES POR CARA (PES21FX\caras.ini):
// por jugador (id de face\real\<id> o parte del nombre) corrimientos propios por hueso para la neutra (z356 dosifica, 0 = la del juego)
// y para cada gesto (se suman a los del mod). En mm, ejes de la cabeza (x a la izquierda del jugador, y arriba, z adelante).
// Solo huesos de labios / nariz / cejas (1..17, 26..30): los parpados y las mejillas tienen otro dueno, la mandibula gira aparte.
struct PoseOff2 { int d; float g[3], p[3]; };   // g = grito, p = puchero
static const PoseOff2 kPose2[] = {
    {  1, { -0.0006f, -0.0010f, 0 },          {  0.0003f,  0.0015f,  0.0012f } },   // lip_b_l
    {  2, {  0.0006f, -0.0010f, 0 },          { -0.0003f,  0.0015f,  0.0012f } },   // lip_b_r
    {  3, { 0, -0.0010f, 0 },                 { 0,  0.0025f,  0.0020f } },          // lip_b_c: puchero = labio de abajo arriba y afuera
    {  4, { 0, 0, 0 },                        { 0,  0.0020f,  0.0010f } },          // doublechin: menton que sube (mentoniano)
    {  5, {  0.0030f, -0.0005f, -0.0012f },   { -0.0005f, -0.0020f, 0 } },          // lip_s_l: grito = comisura afuera y atras
    {  6, { -0.0030f, -0.0005f, -0.0012f },   {  0.0005f, -0.0020f, 0 } },          // lip_s_r
    {  7, {  0.0010f, -0.0005f, 0 },          { 0, -0.0008f, 0 } },                 // cheek_s_l
    {  8, { -0.0010f, -0.0005f, 0 },          { 0, -0.0008f, 0 } },                 // cheek_s_r
    {  9, {  0.0005f,  0.0020f, 0 },          { 0,  0.0003f,  0.0008f } },          // lip_t_l: grito = labio de arriba sube (dientes)
    { 10, { 0,  0.0018f, 0 },                 { 0,  0.0003f,  0.0008f } },          // lip_t_c
    { 11, { -0.0005f,  0.0020f, 0 },          { 0,  0.0003f,  0.0008f } },          // lip_t_r
    { 13, { 0, 0.0010f, -0.0002f },           { 0, 0, 0 } },                        // nose: grito arruga la nariz
    { 14, {  0.0004f,  0.0012f, 0 },          { 0, 0, 0 } },                        // nosewing_l
    { 15, { -0.0004f,  0.0012f, 0 },          { 0, 0, 0 } },                        // nosewing_r
    { 16, {  0.0005f,  0.0012f, 0 },          { 0, 0, 0 } },                        // nasolabialfold_t_l
    { 17, { -0.0005f,  0.0012f, 0 },          { 0, 0, 0 } },                        // nasolabialfold_t_r
    { 26, { -0.0018f,  0.0015f, 0.0004f },    { -0.0008f,  0.0030f, 0 } },          // brow_i_l: grito = juntas y arriba; puchero = triste
    { 27, {  0.0018f,  0.0015f, 0.0004f },    {  0.0008f,  0.0030f, 0 } },          // brow_i_r
    { 28, { 0, -0.0008f, 0 },                 { 0, -0.0005f, 0 } },                 // brow_o_l
    { 29, { 0, -0.0008f, 0 },                 { 0, -0.0005f, 0 } },                 // brow_o_r
    { 30, { 0, -0.0008f, -0.0003f },          { 0,  0.0010f, 0 } },                 // glabella: arruga del entrecejo
};
enum { PG_NEUTRA = 0, PG_ENOJO, PG_SONRISA, PG_ASOMBRO, PG_GRITO, PG_PUCHERO, PG_N };
static const char* const kPgName[PG_N] = { "neutra", "enojo", "sonrisa", "asombro", "grito", "puchero" };
struct FaceProf { char sec[48] = {}; uint32_t id = 0; char name[48] = {}; float off[PG_N][kND][3] = {}; bool has[PG_N] = {}; int nVals = 0; };
static std::vector<FaceProf> s_prof; static unsigned s_profGen = 0; static FILETIME s_profTime = {}; static bool s_profHave = false;
static unsigned s_profCheck = 0xffffffffu;
static bool ProfBoneOk(int d) { return (d >= 1 && d <= 17) || (d >= 26 && d <= 30); }   // los aplica el lazo de la boca
static bool ProfBoneSec(int d) { return d >= 18 && d <= 21; }                           // los aplica Secondary (pomulos, bajo-ojo)
static void ProfTrim(char*& a) { while (*a == ' ' || *a == '\t') a++; char* e = a + strlen(a); while (e > a && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0; }
static void ProfLoad() {   // con s_mx tomado
    const std::wstring path = g_cfg.baseDir + L"caras.ini";
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    const bool have = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) != 0;
    if (have == s_profHave && (!have || CompareFileTime(&fa.ftLastWriteTime, &s_profTime) == 0)) return;
    s_profHave = have; s_profTime = fa.ftLastWriteTime; s_prof.clear(); s_profGen++;
    if (!have) { Log("caras: sin caras.ini (perfiles por cara apagados)"); return; }
    FILE* f = _wfopen(path.c_str(), L"rb"); if (!f) { Log("caras: caras.ini no se pudo abrir"); return; }
    char line[512]; int bad = 0, lineNo = 0; FaceProf* cur = nullptr;
    while (fgets(line, sizeof line, f)) {
        lineNo++;
        char* a = line; if (char* c = strchr(a, ';')) *c = 0; ProfTrim(a); if (!*a) continue;
        if (*a == '[') { char* e = strchr(a, ']'); if (!e) { bad++; continue; } *e = 0; s_prof.emplace_back(); cur = &s_prof.back(); snprintf(cur->sec, sizeof cur->sec, "%s", a + 1); continue; }
        char* eq = strchr(a, '='); if (!eq || !cur) { bad++; continue; }
        *eq = 0; char* k = a; char* v = eq + 1; ProfTrim(k); ProfTrim(v);
        if (!_stricmp(k, "id")) { cur->id = (uint32_t)strtoul(v, nullptr, 10); continue; }
        if (!_stricmp(k, "nombre")) { snprintf(cur->name, sizeof cur->name, "%s", v); continue; }
        if (!_stricmp(k, "boca") || !_stricmp(k, "cara")) continue;   // (04/11) boca propia por jugador: las lee bocajug.cpp
        char* dot = strchr(k, '.'); if (!dot) { bad++; continue; } *dot = 0; const char* bn = dot + 1;
        int g = -1; for (int i = 0; i < PG_N; i++) if (!_stricmp(k, kPgName[i])) g = i;
        int d = -1; for (int i = 0; i < kND; i++) if (!_stricmp(bn, kDef[i].name) || !_stricmp(bn, kDef[i].name + (strncmp(kDef[i].name, "skf_", 4) ? 0 : 4))) d = i;
        float x = 0, y = 0, z = 0;
        if (g < 0 || d < 0 || !(ProfBoneOk(d) || ProfBoneSec(d)) || sscanf(v, "%f %f %f", &x, &y, &z) != 3) { bad++; if (bad <= 6) Log("caras: linea %d no valida: %s.%s", lineNo, k, bn); continue; }
        x = std::clamp(x, -4.0f, 4.0f); y = std::clamp(y, -4.0f, 4.0f); z = std::clamp(z, -4.0f, 4.0f);   // tope 4 mm por hueso
        cur->off[g][d][0] = x * 0.001f; cur->off[g][d][1] = y * 0.001f; cur->off[g][d][2] = z * 0.001f; cur->has[g] = true; cur->nVals++;
    }
    fclose(f);
    for (const FaceProf& p : s_prof) Log("caras: perfil [%s] id %u nombre '%s', %d valores", p.sec, p.id, p.name, p.nVals);
    Log("caras: caras.ini leido, %d perfiles, %d lineas no validas", (int)s_prof.size(), bad);
}
static bool ProfNameHas(const char* hay, const char* needle) {   // contiene, sin mayusculas (ASCII)
    if (!*needle) return false;
    const size_t n = strlen(needle);
    for (const char* h = hay; *h; h++) if (!_strnicmp(h, needle, n)) return true;
    return false;
}
static int ProfFind(uint32_t id, const char* name) {
    for (int i = 0; i < (int)s_prof.size(); i++) if (s_prof[i].id && s_prof[i].id == id) return i;
    for (int i = 0; i < (int)s_prof.size(); i++) if (name[0] && ProfNameHas(name, s_prof[i].name)) return i;
    return -1;
}
static const float* TeethOf(int d) { static const float z[3] = { 0, 0, 0 }; return d < (int)(sizeof(kTeeth) / sizeof(kTeeth[0])) ? kTeeth[d] : z; }
enum { D_JAW = 0, D_LIPS_L = 5, D_LID_TL = 22, D_LID_TR = 23, D_LID_BL = 24, D_LID_BR = 25, D_BROW_IL = 26, D_EYE_L = 31, D_EYE_R = 32, D_NECKB = 33, D_SCM = 34, D_CLAV_L = 35, D_CLAV_R = 36, D_HEAD = kND - 2, D_NECK = kND - 1 };

struct Skel {
    int ganiClip = 0; float ganiT0 = 0.0f;   // (07/11) clip .gani en curso y reloj del juego en que arranco
    float secT = -1.0f, secSm = 0.0f, secOpen = 0.0f; bool secHave = false;   // (08/11) sonrisa y boca abierta MEDIDAS, suavizadas con el reloj del juego
    float clockT = -1.0f, clockK = 1.0f;   // (06/11) reloj del juego del ultimo cuadro de los parpados y cuadros de 60 Hz que pasaron
    const void* skel = nullptr; const char* bones = nullptr; int n = 0;   // esqueleto del cuerpo (clave) y su arreglo
    const void* face = nullptr; const char* fbones = nullptr; int fn = 0;  // (02/11) esqueleto de la CARA: nodo + 0xA0 (49 huesos)
    void* cur[kND] = {};      // puntero a la transformacion actual de cada hueso reconocido (null = no esta)
    int idx[kND] = {};        // indice del hueso en el arreglo del esqueleto
    const char* src[kND] = {};   // (02/11) arreglo de donde salio (cara o cuerpo): para revalidar cada cuadro
    const void* node = nullptr;  // (02/11) SceneCharacterNode con el que se armo
    // (08/11 f) huella de los bloques al armar: vtable del nodo / de los dos esqueletos y los primeros 32 bytes del hueso 0 de cada
    // arreglo. dlmalloc pisa los primeros 16 bytes de un bloque liberado con sus punteros de lista: si algo de esto cambio, el bloque
    // ya no es del juego y NO se escribe (los cierres +0x1ff674a / cuelgue +0x1ff668e del editor podian ser huesos escritos ahi)
    uint64_t vtNode = 0, vtFace = 0, vtSkel = 0, b0Face[4] = {}, b0Body[4] = {};
    Xf rest[kND] = {};
    int found = 0, jawFound = 0;
    unsigned lastFrame = 0; int rebuilds = 0; bool logged = false;
    // diagnostico: giro de la mandibula respecto de la cabeza (grados) en la ventana en curso
    float angMin = 1e9f, angMax = -1e9f; int angN = 0;
    // (02/11) ojos: direccion del ojo respecto de la cabeza en el cuadro anterior (antes de escribir) para ver si el juego los mueve
    float eyePrev[2][3] = {}; bool eyeHave = false; float gameEye = 0.0f;
    // (02/11) lo ultimo que se escribio en cada hueso de ojos / parpados (q escrito y giro aplicado): si el juego no lo reescribe
    // entre cuadros, se deshace el giro anterior antes de aplicar el nuevo (si no, se acumularia y el ojo daria vueltas)
    // (02/11 b, Agustin: "las barbas al abrir la boca tambien vibran") vale para TODOS los huesos que se escriben: se guarda la base
    // (lo que dejo el juego) y lo escrito; si al cuadro siguiente se lee lo escrito (el juego no reescribio, o el Eval del
    // cuello paso dos veces en el mismo cuadro) se parte de la base guardada y no se gira dos veces
    float wq[kND][4] = {}, wt[kND][3] = {}, bq[kND][4] = {}, bt[kND][3] = {}; bool wHave[kND] = {};
    // (08/11) gestos en la paleta (expr_paleta): lo que el mod QUIERE en cada hueso este cuadro (no se escribe en el esqueleto; se publica
    // en `pub` y el VS lo aplica, ver ExprCharPose en core.h)
    float dq[kND][4] = {}, dt[kND][3] = {}; bool dHave[kND] = {}; unsigned dFrame[kND] = {}; ExprCharPose pub;
    int reused = 0;
    // (02/11 i) amplificador POR HUESOS: base del juego leida este cuadro (lo que se guarda como base al escribir), cuadro de la
    // ultima escritura y la cabeza del cuadro para medir el gesto (actual - cabeza rigida)
    float gq[kND][4] = {}, gt[kND][3] = {}; unsigned wFrame[kND] = {};
    float bwHr[kND][4] = {}, bwHc[kND][3] = {}; bool bwHave[kND] = {};   // (08/11) cabeza del cuadro en que se guardo la base (bq / bt)
    bool ampOn = false; float ahc[3] = {}, ahr[4] = { 0, 0, 0, 1 }, aGain[kND] = {}, aCap = 0.012f, aBig = 0.35f;
    // (02/11 l, Agustin: "vibra un poco al abrirse ... la camara lenta") cabeza del cuadro en que el JUEGO escribio cada hueso: en
    // camara lenta el juego no reescribe los huesos de la cara en todos los cuadros (16 mil lecturas con lo del mod puesto) pero la
    // cabeza si se mueve; medir el gesto contra la cabeza de ahora metia ese movimiento en el gesto y lo agrandaba a saltos
    float bHc[kND][3] = {}, bHr[kND][4] = {}; bool bHave[kND] = {}, fresh[kND] = {}; unsigned ahFrame = ~0u;
    bool nWarm[kND] = {}, lidRq[kND] = {}, lidRt[kND] = {}, lqHave[kND] = {}, ltHave[kND] = {}; float lqHr[kND][4] = {}, ltHr[kND][4] = {}, ltHc[kND][3] = {};
    // (02/11 n, Agustin: "al subir la ganancia la cara se deforma pero no en partes") el gesto se medía contra el REPOSO del skl,
    // pero la forma de cada cara (editor de caras del juego) tambien esta en los huesos: se agrandaba la forma de la cara entera.
    // Ahora el gesto se mide contra la cara NEUTRA de este jugador: promedio lento (~10 s) de cada hueso en los ejes de la cabeza
    float nT[kND][3] = {}, nQ[kND][4] = {}; int nN[kND] = {};
    float eyeP = 0.0f; bool eyePHave = false;   // (02/11 o) mirada vertical del JUEGO (hueso del ojo), suavizada
    float eyePT = 0.0f, lidSm = 0.0f; bool lidSmHave = false;   // (04/11) objetivo con histeresis de eyeP y sonrisa medida suavizada para los parpados
    float eyeY = 0.0f, eyeYT = 0.0f, lidX = 0.0f; bool eyeYHave = false, lidXHave = false;
    float animM = 0.0f, animG = 1.0f; bool animHave = false, animGHave = false;   // (08/11) animacion medida con histeresis y compuerta del parpadeo suavizada   // (08/11) mirada de costado del juego (suavizada, con histeresis) y giro de costado de los parpados
    float lidU = 0.0f, lidL = 0.0f; bool lidHave = false;        // (04/11) angulos de los parpados del cuadro anterior (limite de velocidad)
    float seed = 0.37f; bool seedHave = false; float seedCand = -1.0f; int seedCandN = 0;   // (02/11 c) semilla de la mirada, la misma que el VS del globo (PesExprSeed)
    // (02/11 e) sk_head del esqueleto del CUERPO (175 huesos) para la camara: el sk_head del esqueleto de la cara tiene la
    // transformacion actual en (0, 0, 0) (no es espacio del modelo: log 12:46 PC), el del cuerpo si lo es
    void* bHead = nullptr; int bHeadIdx = -1; Xf bHeadRest = {};
    // (04/11) perfil por cara: jugador de este esqueleto (personajes.cpp) y perfil de caras.ini (-1 = ninguno)
    // (08/11) gestos propios del jugador (gani.cpp): copia de la lista, peso suavizado de cada uno y el gesto "entre_" en curso
    std::vector<GaniGest> gg; unsigned ggGen = 0; uint32_t ggPid = 0; float ggW[16] = {}; float ggT = -1.0f; unsigned ggLog = 0;
    int btw = -1; float btwT0 = 0.0f, btwNext = -1.0f; int btwN = 0;
    float spP[3] = {}; float spT = -1.0f, speed = 0.0f, stillT = 0.0f; bool spHave = false;
    bool farOff = false;   // (09/11) lejos de la camara: sin escribir el esqueleto (z391)
    float sl[16] = {}; bool slHave = false; float slT = -1.0f;   // (08/11) sliders de gestos suavizados (w395 Transicion)
    GaniPose gB = {}; bool gBHave = false; float gBT = -1.0f;                         // (08/11) clip .gani mostrado (y397: inertializacion en entrada / salida / cruce / vuelta del bucle)
    GaniPose gPrev = {}; float gPrevDt = 0.0f; Inert gI[31][6]; float gIT0 = -1.0f; int gLastClip = -1; bool gLastOn = false; float gLastF = 0.0f;
    float gsL[kND][3] = {}, gsV[kND][3] = {}; bool gsHave[kND] = {}; float gsDeg = 0.0f, gsDegV = 0.0f; bool gsDegHave = false; float gsT = -1.0f;   // (08/11) gestos del juego suavizados (x397, resorte), ejes de la cabeza
    float slV[16] = {};   // velocidad del resorte de los sliders (w395)
    int tkPhrase = -1; float tkT0 = 0.0f, tkDur = 0.0f, tkPause = 0.0f, tkCh[7] = {}, tkT = -1.0f; unsigned tkN = 0; int tkLast = -1;   // (08/11) hablar por palabras: frase en curso, canales suavizados
    float effort = 0.0f, brPh = 0.0f, anT = -1.0f;                 // (08/11) esfuerzo por velocidad (sube ~1 s, baja ~4 s), fase de la respiracion
   // (08/11) velocidad del jugador (cabeza en el mundo, m/s) y cuanto lleva quieto
    uint32_t pid = 0; bool pKnown = false; int prof = -1; unsigned profGen = 0xffffffffu; unsigned profFrame = 0; bool profLogged = false;
};
static const int kMaxSkel = 48;
static std::mutex s_mx;
static Skel s_sk[kMaxSkel]; static int s_nSk = 0;
static std::atomic<unsigned> s_frame{0};
static std::atomic<int> s_applied{0}, s_noJaw{0}, s_badWrite{0}, s_calls{0}, s_stale{0};
static std::atomic<unsigned> s_lastActive{0};
static std::atomic<unsigned> s_lastEyes{0};   // (02/11) cuadro del ultimo giro de ojos / parpados por huesos
static std::atomic<int> s_eyesW{0}, s_blinks{0};
static std::atomic<int> s_lidAmpSkip{0};   // (04/11) parpados que el amplificador ya no toca
static std::atomic<int> s_lidHeadFix{0}, s_lidHeadFixT{0};   // (04/11) parpados con la base llevada a la cabeza del cuadro
static std::atomic<int> s_far{0};   // (09/11) pasos sin escribir por estar lejos de la camara (z391)
static std::atomic<int> s_neckW{0};   // (08/11) huesos del cuello escritos (tension con grito / enojo / apretar, boca abierta)
static std::atomic<int> s_baseHeadFix{0};   // (08/11) otros huesos con la parte no reescrita llevada a la cabeza del cuadro
static std::atomic<int> s_ampW{0}, s_ampSkip{0};   // (02/11 i) huesos agrandados escritos (amplificador por huesos)   // cuadro del ultimo giro aplicado o esqueleto con mandibula visto
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
        uint32_t hs = 0;
        if (SafeCopy(&hs, bone + 0x10, 4) && hs) {
            bool hit = false;
            for (int d = 0; d < kND; d++) if (kDef[d].hash == hs) { ps.push_back({ -2.0f, b, d }); hit = true; break; }
            if (hit) continue;
        }
        char nm[64];
        if (BoneName(bone, nm, sizeof nm)) {
            for (int d = 0; d < kND; d++) if (!strcmp(nm, kDef[d].name)) { ps.push_back({ -1.0f, b, d }); break; }
            continue;
        }
        for (int d = 0; d < kND; d++) { if (kDef[d].hash) continue; float d2 = Dist2(rests[b].t, kDef[d]); if (d2 < 0.004f * 0.004f) ps.push_back({ d2, b, d }); }
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
    if (S.node) SafeCopy(&S.vtNode, S.node, 8);
    if (S.face) SafeCopy(&S.vtFace, S.face, 8);
    SafeCopy(&S.vtSkel, S.skel, 8);
    if (S.fbones) SafeCopy(S.b0Face, S.fbones, sizeof S.b0Face);
    if (S.bones) SafeCopy(S.b0Body, S.bones, sizeof S.b0Body);
    for (int b = 0; b < S.n && !S.bHead; b++) {   // sk_head del cuerpo (hash FF3D8D77 o nombre)
        const char* bone = S.bones + (size_t)b * 0xc0;
        uint32_t hs = 0; char nm[64];
        const bool isHead = (SafeCopy(&hs, bone + 0x10, 4) && hs == 0xFF3D8D77u) || (BoneName(bone, nm, sizeof nm) && !strcmp(nm, "sk_head"));
        if (!isHead) continue;
        void* cur = nullptr; Xf r;
        if (ReadXf(bone + 0x40, r) && ReadPtr(bone + 0x18, cur)) { S.bHead = cur; S.bHeadIdx = b; S.bHeadRest = r; }
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
    {   // (08/11 f) bloques todavia vivos (ver vtNode)
        uint64_t v = 0, b[4] = {};
        if (!SafeCopy(&v, S.node, 8) || v != S.vtNode) return false;
        if (!SafeCopy(&v, S.face, 8) || v != S.vtFace) return false;
        if (!SafeCopy(&v, S.skel, 8) || v != S.vtSkel) return false;
        if (!SafeCopy(b, S.fbones, sizeof b) || memcmp(b, S.b0Face, sizeof b)) return false;
        if (!SafeCopy(b, S.bones, sizeof b) || memcmp(b, S.b0Body, sizeof b)) return false;
    }
    void* face = nullptr; if (!ReadPtr((const char*)S.node + 0xA0, face) || face != S.face) return false;
    void* arr = nullptr; int n = 0;
    if (!ReadPtr((const char*)S.face + 0x28, arr) || (const char*)arr != S.fbones || !SafeCopy(&n, (const char*)S.face + 0x30, 4) || n != S.fn) return false;
    if (!ReadPtr((const char*)S.skel + 0x28, arr) || (const char*)arr != S.bones || !SafeCopy(&n, (const char*)S.skel + 0x30, 4) || n != S.n) return false;
    if (S.bHead) {
        void* cur = nullptr; int ix = -1; const char* bone = S.bones + (size_t)S.bHeadIdx * 0xc0;
        if (!ReadPtr(bone + 0x18, cur) || cur != S.bHead || !SafeCopy(&ix, bone, 4) || ix != S.bHeadIdx) return false;
    }
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

// (02/11, Agustin: "los ojos vivos es lo que rompe por que no mueve el esqueleto real", "haz que el parpado se mueva, siento que
// los ojos se mueven pero no el parpado") MIRADA y PARPADEO por HUESOS. Antes el VS del globo (69758ab9) giraba la esfera y
// el de la cara (PesBlinkApply) giraba los vertices de los parpados solo en las mallas donde la DLL los habia encontrado: las
// pestanas y la otra malla del parpado quedaban quietas y se abrian ranuras. Ahora se giran skf_eye_l/r (el globo: eye.fmdl
// pesa 1 en ellos) y skf_eyelid_t/b_l/r (todas las mallas de la cara, pestanas incluidas, pesan en ellos), con la misma
// cuenta que el shader (PesGaze / PesBlinkCurve de pes_expr.hlsli): fijaciones de 0.45..1.9 s, sacadas de ~45 ms, deriva y
// microsacadas; parpadeo cada ~5.5 s (cierra 0.08 s, 0.04 quieto, abre 0.18). El parpado de arriba acompana la mirada vertical
// (baja 0.65 del angulo al mirar abajo, sube 0.35 al mirar arriba) y el de abajo un poco (0.2 al mirar abajo). La semilla es
// por personaje (el nodo), asi que cada jugador mira y parpadea a su ritmo. Cuando esto corre, 350.y = 2 y el shader no gira
// ni el globo ni los parpados (no se suma dos veces).
static float Frac(float x) { return x - floorf(x); }
static float SStep(float a, float b, float x);   // definida mas abajo (la usan AnimT / TalkWords)
// (08/11, Agustin: "puedes hacer que calcule por cada fotograma? aunque el gameclock este en cero como pasa en el menu de apariencia")
// RELOJ DE LAS ANIMACIONES de la cara: avanza en cada cuadro. Si el reloj del juego (fila 157, config.cpp) avanzo desde el cuadro
// anterior se toma ese paso (asi la camara lenta de las repeticiones frena tambien los gestos); si esta quieto o en cero (editor,
// menu de apariencia, presentacion) se toma el reloj real, con tope de 1/15 s por cuadro. Hablar, vida, respiracion, transicion,
// parpadeo, gestos propios y clips .gani usan este reloj, nunca el del juego directo.
static float AnimT() {
    static std::mutex mx; static double animT = 0.0, lastGame = -1.0, lastReal = -1.0; static unsigned fr = 0xffffffffu;
    std::lock_guard<std::mutex> lk(mx);
    const unsigned now = s_frame.load(std::memory_order_relaxed);
    if (now != fr) {
        fr = now;
        LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f);
        const double real = (double)c.QuadPart / (double)(f.QuadPart ? f.QuadPart : 1);
        const double game = (double)g_cfg.live[157][0];
        double dG = lastGame < 0.0 ? 0.0 : game - lastGame, dR = lastReal < 0.0 ? 0.0 : real - lastReal;
        lastGame = game; lastReal = real;
        double adv = (dG > 1e-5 && dG < 0.5) ? dG : std::clamp(dR, 0.0, 1.0 / 15.0);
        animT += adv; if (animT > 100000.0) animT -= 100000.0;
    }
    return (float)animT;
}

// (08/11, Agustin: "para el hablar agrega mas opciones de por ejemplo pronunciar palabras, interpola mejor las animaciones") HABLAR POR
// PALABRAS (w396 = 1). Cada frase se pasa a fonemas y cada fonema a un VISEMA (forma de la boca): A (abierta), E (media, ancha),
// I (cerrada, ancha), O (redonda), U (trompa), M (b / p / m: labios juntos), F (f / v: labio de abajo contra los dientes), T (resto de
// consonantes: lengua, boca algo abierta), K (k / g / j / q: mas abierta atras). Cada visema fija 6 canales: mandibula, redondez (- ancha
// .. + redonda), labio de arriba (sube), labio de abajo (baja; - sube contra los dientes), labios adelante, labios apretados. Entre fonemas
// la boca no salta: cada canal va del visema anterior al nuevo con una curva suave (coarticulacion: las consonantes ya anticipan la vocal
// que sigue) y despues pasa por un filtro de ~25 ms. Las frases salen de PES21FX\anim\hablar.txt (una por linea, acentos marcan la silaba
// tonica; si no esta, lista propia de cancha). Entre frases hay pausas de 0.6 a 2.5 s, y la silaba tonica levanta un poco las cejas.
enum { V_REST = 0, V_A, V_E, V_I, V_O, V_U, V_M, V_F, V_T, V_K, V_N };
static const float kVis[V_N][6] = {   // mandibula, redondez, labio arriba, labio abajo, adelante, apretados
    { 0.00f,  0.00f, 0.00f,  0.00f, 0.00f, 0.00f },   // reposo
    { 0.90f, -0.10f, 0.35f,  0.55f, 0.00f, 0.00f },   // a
    { 0.45f, -0.60f, 0.30f,  0.30f, 0.00f, 0.00f },   // e
    { 0.22f, -0.90f, 0.20f,  0.15f, 0.00f, 0.00f },   // i
    { 0.60f,  0.70f, 0.20f,  0.35f, 0.55f, 0.00f },   // o
    { 0.30f,  1.00f, 0.10f,  0.10f, 1.00f, 0.00f },   // u
    { 0.04f,  0.15f, 0.00f,  0.00f, 0.05f, 1.00f },   // m b p
    { 0.18f,  0.10f, 0.15f, -0.45f, 0.00f, 0.30f },   // f v
    { 0.30f, -0.20f, 0.15f,  0.20f, 0.00f, 0.00f },   // t d n l s r
    { 0.38f,  0.00f, 0.10f,  0.25f, 0.00f, 0.00f },   // k g j q
};
struct Pho { uint8_t vis; float dur; bool stress; };   // dur en unidades (vocal 1)
struct Phrase { std::vector<Pho> ph; float units = 0.0f; };
static std::vector<Phrase> s_phr; static bool s_phrLoaded = false; static unsigned s_phrFrame = 0;
static const char* kFrasesCancha[] = {
    "dale dale", "aca aca", "pasala", "vamos vamos", "arbitro!", "solo solo", "atras atras", "cuidado", "bien ahi", "no no no",
    "la mia", "tiempo tiempo", "arriba arriba", "cambio", "sali sali", "cubrime", "al medio", "dame dame", "eh eh eh", "vamos muchachos",
    "tranquilo", "cerra cerra", "marca marca", "fue falta", "mano mano", "tuya tuya", "corre corre", "hombre hombre", "afuera", "que pasa",
};
static void PhraseParse(const char* txt, Phrase& P) {
    // minusculas y acentos (UTF-8 o ANSI) -> vocal tonica; '!' / '?' no cuentan
    std::vector<std::pair<char, bool>> L;   // letra, tonica marcada
    for (const unsigned char* p = (const unsigned char*)txt; *p; p++) {
        unsigned char c = *p; char v = 0; bool st = false;
        if (c == 0xC3 && p[1]) { unsigned char d = p[1]; p++;
            switch (d) { case 0xA1: case 0x81: v = 'a'; st = true; break; case 0xA9: case 0x89: v = 'e'; st = true; break; case 0xAD: case 0x8D: v = 'i'; st = true; break;
                         case 0xB3: case 0x93: v = 'o'; st = true; break; case 0xBA: case 0x9A: case 0xBC: v = 'u'; st = true; break; case 0xB1: case 0x91: v = 'n'; break; default: v = 0; } }
        else if (c == 0xE1 || c == 0xC1) { v = 'a'; st = true; } else if (c == 0xE9 || c == 0xC9) { v = 'e'; st = true; } else if (c == 0xED || c == 0xCD) { v = 'i'; st = true; }
        else if (c == 0xF3 || c == 0xD3) { v = 'o'; st = true; } else if (c == 0xFA || c == 0xDA || c == 0xFC) { v = 'u'; st = true; } else if (c == 0xF1 || c == 0xD1) v = 'n';
        else if (c >= 'A' && c <= 'Z') v = (char)(c + 32); else if (c >= 'a' && c <= 'z') v = (char)c; else if (c == ' ' || c == ',' || c == '.') v = ' ';
        else continue;
        if (v) L.push_back({ v, st });
    }
    // por palabra: si ninguna vocal lleva acento, tonica = penultima vocal si termina en vocal / n / s, si no la ultima
    size_t i = 0;
    while (i < L.size()) {
        if (L[i].first == ' ') { P.ph.push_back({ V_REST, 1.2f, false }); i++; continue; }
        size_t j = i; while (j < L.size() && L[j].first != ' ') j++;
        bool marked = false; std::vector<size_t> vow;
        for (size_t k = i; k < j; k++) { if (strchr("aeiou", L[k].first)) { vow.push_back(k); if (L[k].second) marked = true; } }
        if (!marked && !vow.empty()) { const char last = L[j - 1].first; const bool llana = strchr("aeiouns", last) != nullptr; L[vow.size() >= 2 && llana ? vow[vow.size() - 2] : vow.back()].second = true; }
        for (size_t k = i; k < j; k++) {
            const char c = L[k].first; uint8_t v = V_T; float d = 0.5f;
            switch (c) { case 'a': v = V_A; d = 1.0f; break; case 'e': v = V_E; d = 1.0f; break; case 'i': case 'y': v = V_I; d = 0.9f; break; case 'o': v = V_O; d = 1.0f; break; case 'u': v = V_U; d = 0.9f; break;
                         case 'm': case 'b': case 'p': v = V_M; d = 0.6f; break; case 'f': case 'v': v = V_F; d = 0.6f; break; case 'k': case 'g': case 'j': case 'q': case 'c': case 'x': v = V_K; d = 0.5f; break;
                         case 'h': continue; default: v = V_T; d = 0.5f; }
            if (L[k].second) d *= 1.45f;
            P.ph.push_back({ v, d, L[k].second });
        }
        i = j;
    }
    P.units = 0.0f; for (const Pho& p : P.ph) P.units += p.dur;
}
static void PhrasesLoad() {   // con s_mx tomado; relee cada ~10 s si el archivo cambia de tamano
    const unsigned fr = s_frame.load(std::memory_order_relaxed);
    if (s_phrLoaded && fr - s_phrFrame < 600) return;
    s_phrFrame = fr;
    static size_t lastSize = (size_t)-1; std::vector<uint8_t> t; size_t sz = 0;
    { HANDLE h = CreateFileW((g_cfg.baseDir + L"anim\\hablar.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
      if (h != INVALID_HANDLE_VALUE) { LARGE_INTEGER q{}; if (GetFileSizeEx(h, &q) && q.QuadPart > 0 && q.QuadPart < (1 << 20)) { t.resize((size_t)q.QuadPart); DWORD rd = 0; if (!ReadFile(h, t.data(), (DWORD)t.size(), &rd, nullptr) || rd != t.size()) t.clear(); sz = t.size(); } CloseHandle(h); } }
    if (s_phrLoaded && sz == lastSize) return;
    lastSize = sz; s_phr.clear();
    if (!t.empty()) {
        t.push_back('\n'); std::string line;
        for (uint8_t c : t) {
            if (c == '\n' || c == '\r') { size_t a = line.find_first_not_of(" \t"); if (a != std::string::npos && line[a] != '#' && line[a] != ';') { Phrase P; PhraseParse(line.c_str() + a, P); if (P.units > 0.5f) s_phr.push_back(P); } line.clear(); }
            else if (!(c == 0xEF || c == 0xBB || c == 0xBF) || !line.empty()) line.push_back((char)c);
        }
    }
    const bool own = !s_phr.empty();
    if (!own) for (const char* f : kFrasesCancha) { Phrase P; PhraseParse(f, P); if (P.units > 0.5f) s_phr.push_back(P); }
    if (s_logs < 400) { s_logs++; Log("hablar: %d frases %s", (int)s_phr.size(), own ? "de PES21FX\\anim\\hablar.txt" : "propias (sin anim\\hablar.txt)"); }
    s_phrLoaded = true;
}
// canales de la boca en este cuadro para este personaje: out[0..5] = los 6 del visema, out[6] = acento (silaba tonica). sp = velocidad
// (z354, 0.55 = pausado). Devuelve false si esta en pausa entre frases (boca en reposo)
static bool TalkWords(Skel& S, float tNow, float sp, float out[7]) {
    PhrasesLoad();
    const float unit = 0.095f * 0.55f / (std::max)(sp, 0.2f);   // s por vocal: 0.095 pausado (~3.5 silabas/s), 0.052 a 1.0
    auto rnd = [&](float n) { return Frac(sinf(n * 12.9898f + (float)(S.pid % 9973u) * 0.731f + (float)(((uintptr_t)S.skel >> 6) & 63) * 0.173f) * 43758.5453f); };
    float target[6] = {}, acc = 0.0f; bool talking = false;
    if (!s_phr.empty()) {
        if (S.tkPhrase < 0 || tNow < S.tkT0 || tNow > S.tkT0 + S.tkDur + S.tkPause) {
            int pick = (int)(rnd((float)S.tkN + 0.37f) * (float)s_phr.size()); if (pick >= (int)s_phr.size()) pick = (int)s_phr.size() - 1;
            if (pick == S.tkLast && s_phr.size() > 1) pick = (pick + 1) % (int)s_phr.size();
            S.tkLast = S.tkPhrase = pick; S.tkT0 = tNow; S.tkDur = s_phr[pick].units * unit; S.tkPause = 0.6f + 1.9f * rnd((float)S.tkN + 0.81f); S.tkN++;
        }
        const Phrase& P = s_phr[S.tkPhrase];
        const float u = (tNow - S.tkT0) / unit;   // posicion en unidades
        if (u < P.units) {
            talking = true;
            float a = 0.0f; int k = 0; for (; k < (int)P.ph.size(); k++) { if (u < a + P.ph[k].dur) break; a += P.ph[k].dur; }
            if (k >= (int)P.ph.size()) k = (int)P.ph.size() - 1;
            const Pho& cur = P.ph[k]; const uint8_t prev = k > 0 ? P.ph[k - 1].vis : V_REST; const uint8_t next = k + 1 < (int)P.ph.size() ? P.ph[k + 1].vis : V_REST;
            // destino del fonema: el visema propio; una consonante anticipa un 35 % la vocal que sigue (coarticulacion)
            float dst[6]; const bool cons = cur.vis >= V_M; const float ant = cons && next >= V_A && next <= V_U ? 0.35f : 0.0f;
            for (int c = 0; c < 6; c++) dst[c] = kVis[cur.vis][c] * (1.0f - ant) + kVis[next][c] * ant;
            // transicion: del fonema anterior al actual en el primer 60 % de su duracion, con curva suave
            const float f = SStep(0.0f, 0.6f, (u - a) / (std::max)(cur.dur, 0.05f));
            for (int c = 0; c < 6; c++) target[c] = kVis[prev][c] + (dst[c] - kVis[prev][c]) * f;
            // apretados (m / b / p) solo se ven si tambien se cierra la mandibula
            target[5] *= 1.0f - std::clamp(target[0] * 2.0f, 0.0f, 1.0f);
            acc = cur.stress ? SStep(0.0f, 0.3f, (u - a) / (std::max)(cur.dur, 0.05f)) * (1.0f - SStep(0.7f, 1.0f, (u - a) / (std::max)(cur.dur, 0.05f))) : 0.0f;
        }
    }
    // filtro de ~25 ms por canal (quita el escalon de la curva y suaviza el reposo entre frases)
    const float dt = S.tkT < 0.0f || tNow < S.tkT ? 1.0f : (std::min)(tNow - S.tkT, 0.25f); S.tkT = tNow;
    const float kf = 1.0f - expf(-dt / 0.025f);
    for (int c = 0; c < 6; c++) S.tkCh[c] += (target[c] - S.tkCh[c]) * kf;
    S.tkCh[6] += (acc - S.tkCh[6]) * (1.0f - expf(-dt / 0.06f));
    memcpy(out, S.tkCh, sizeof S.tkCh);
    return talking;
}
static float SStep(float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
static void GazeHash2(float n, float seed, float* o) {
    o[0] = Frac(sinf((n + seed * 17.3f) * 12.9898f) * 43758.5453f);
    o[1] = Frac(sinf((n + seed * 29.7f) * 78.233f) * 43758.5453f);
}
static void GazeTarget(float n, float seed, float* g) {   // (02/11) igual que PesGazeTarget (pes_expr.hlsli): mas natural
    float h[2]; GazeHash2(n, seed, h);
    g[0] = (h[0] - 0.5f) * 2.0f * 0.052f; g[1] = (h[1] - 0.58f) * 2.0f * 0.034f;
    if (Frac(h[0] * 7.1f + h[1] * 3.3f) >= 0.67f) { g[0] *= 0.2f; g[1] *= 0.2f; }
}
// g[0] = de lado, g[1] = arriba (+) / abajo (-), radianes. LA MISMA CUENTA que PesGaze (pes_expr.hlsli)
static void Gaze(float t, float seed, float amp, float* g) {
    g[0] = g[1] = 0.0f;
    {   // (03/11) 354.x ojos en circulos (vueltas por segundo), 354.y radio en grados: igual que PesGaze
        const float* C = g_cfg.live[354];
        if (C[0] > 0.0f) { const float a = t * C[0] * 6.2831853f, r = C[1] * 0.0174533f; g[0] = cosf(a) * r; g[1] = sinf(a) * r; return; }
    }
    if (amp <= 0.0f) return;
    const float tt = t + seed * 23.1f, per = 2.2f, n = floorf(tt / per);
    float h[2]; GazeHash2(n, seed + 0.5f, h);
    const float x = tt - (n * per + h[0] * 1.2f);
    float g0[2], g1[2]; GazeTarget(n - 1.0f, seed, g0); GazeTarget(n, seed, g1);
    const float u = 1.0f - std::clamp(x / 0.070f, 0.0f, 1.0f);
    float k = 1.0f - u * u * u;
    k = k * 0.93f + 0.07f * SStep(0.10f, 0.22f, x);
    for (int i = 0; i < 2; i++) g[i] = g0[i] + (g1[i] - g0[i]) * k;
    g[0] += sinf(tt * 0.63f + seed) * 0.0020f + sinf(tt * 1.37f + seed * 2.1f) * 0.0010f;
    g[1] += sinf(tt * 0.51f + seed * 2.0f) * 0.0015f + sinf(tt * 1.13f + seed * 0.7f) * 0.0008f;
    g[0] *= amp; g[1] *= amp;
}
// (08/11, Agustin: "perfecciona los parpados") parpadeo irregular como el de una persona: ranuras de 4 s, en ~30 % no parpadea (los
// intervalos quedan de 0.5 a ~11 s, ~5.5 s de promedio, como antes) y en ~15 % parpadea dos veces seguidas (0.38 s); cada parpadeo
// cierra un poco distinto (85..100 %). Cierra en 0.08 s, queda 0.04, abre en 0.18. MISMA cuenta en pes_expr.hlsli (PesBlinkCurve)
static float BlinkShape(float x) { if (x < 0.0f || x > 0.30f) return 0.0f; return x < 0.08f ? SStep(0.0f, 0.08f, x) : (x < 0.12f ? 1.0f : 1.0f - SStep(0.12f, 0.30f, x)); }
static float BlinkCurve(float t, float seed) {
    const float per = 4.0f, tt = t + seed * 37.3f, n = floorf(tt / per);
    const float h = Frac(sinf((n + seed * 13.1f) * 12.9898f) * 43758.5453f);
    const float h2 = Frac(sinf((n + seed * 7.7f) * 78.233f) * 43758.5453f);
    if (h2 < 0.30f) return 0.0f;
    const float x = tt - (n * per + h * (per - 0.9f));
    float b = BlinkShape(x); if (h2 > 0.85f) b = (std::max)(b, BlinkShape(x - 0.38f));
    return b * (0.85f + 0.15f * Frac(h * 7.31f));
}
// transformacion actual del hueso SIN el giro del mod: si el cuaternion sigue siendo el que escribimos el cuadro anterior (el
// juego no lo reescribio), se le saca ese giro
static bool ReadBase(Skel& S, int d, Xf& x) {
    if (!ReadXf(S.cur[d], x)) return false;
    if (S.wHave[d]) {
        // (02/11 c) el cuaternion y la traslacion se comparan POR SEPARADO: el juego puede reescribir solo el giro de un hueso y
        // dejar la traslacion que escribimos (o al reves); antes se tomaba esa traslacion como base y se volvia a correr cada
        // cuadro hasta que el juego la reponia: vaiven = la barba y los parpados vibraban (log d116b77: ~6 de 15 huesos por
        // paso se leian con lo del mod todavia puesto)
        const float* w = S.wq[d]; const float* u = S.wt[d];
        const float dq = fabsf(x.q[0] - w[0]) + fabsf(x.q[1] - w[1]) + fabsf(x.q[2] - w[2]) + fabsf(x.q[3] - w[3]);
        const float dt = fabsf(x.t[0] - u[0]) + fabsf(x.t[1] - u[1]) + fabsf(x.t[2] - u[2]);
        bool hit = false, hq = false, ht = false;
        if (dq < 2e-6f) { memcpy(x.q, S.bq[d], sizeof S.bq[d]); hit = hq = true; }
        if (dt < 1e-7f + 1e-6f * (fabsf(u[0]) + fabsf(u[1]) + fabsf(u[2]))) { memcpy(x.t, S.bt[d], sizeof S.bt[d]); hit = ht = true; }
        if (hit) S.reused++;
        // (08/11, Agustin: "los parpados y partes de la boca [vibran] al mover la camara ... debe ser constante asi como ocurre cuando
        // se carga un .gani") la parte que el juego NO reescribio (giro o posicion) se tomaba de la base guardada TAL CUAL, con la cabeza
        // del cuadro en que se guardo; con la camara moviendose la cabeza gira (mira a la camara) y esa parte quedaba un cuadro atras y
        // saltaba al siguiente. Ahora se lleva a la cabeza de este cuadro, como ya se hacia con los parpados (abajo)
        if (hit && !(d >= D_LID_TL && d <= D_LID_BR) && d < D_NECKB && S.bwHave[d] && S.ahFrame == s_frame.load(std::memory_order_relaxed)) {
            float ic[4], D[4]; QConj(S.bwHr[d], ic); QMul(S.ahr, ic, D);
            if (fabsf(D[3]) > 0.94f && fabsf(D[3]) < 0.9999999f) {
                if (hq) { float nq[4]; QMul(D, x.q, nq); memcpy(x.q, nq, sizeof nq); }
                if (ht) { const float v[3] = { x.t[0] - S.bwHc[d][0], x.t[1] - S.bwHc[d][1], x.t[2] - S.bwHc[d][2] }; float r[3]; QRot(D, v, r); for (int k = 0; k < 3; k++) x.t[k] = S.ahc[k] + r[k]; }
                s_baseHeadFix.fetch_add(1, std::memory_order_relaxed);
            }
        }
        S.lidRq[d] = hq; S.lidRt[d] = ht;
        S.fresh[d] = !(hq && ht);   // el juego escribio algo nuevo en este hueso
    } else { S.fresh[d] = true; S.lidRq[d] = S.lidRt[d] = false; }
    memcpy(S.gq[d], x.q, sizeof S.gq[d]); memcpy(S.gt[d], x.t, sizeof S.gt[d]);
    // (04/11, Agustin: los parpados "siguen vibrando al hacer animaciones, por eso en partido se nota") el juego no reescribe los
    // parpados en todos los cuadros: en los que no, la base guardada es la del cuadro en que los escribio, con la cabeza de ESE
    // momento, y el giro del parpado se arma con la cabeza de ahora: con la cabeza en movimiento el parpado quedaba un cuadro atras
    // y saltaba al cuadro siguiente. Ahora esa base se lleva a la cabeza de este cuadro (lo que se guarda sigue siendo lo del juego)
    // (04/11, log 04:58 PC: "llevados a la cabeza 0") el juego nunca deja el parpado entero sin tocar: reescribe el giro O la
    // posicion. Entonces cada parte se lleva por separado, con la cabeza del ultimo cuadro en que el juego escribio ESA parte
    if (d >= D_LID_TL && d <= D_LID_BR && S.ahFrame == s_frame.load(std::memory_order_relaxed)) {
        const bool rq = S.wHave[d] && S.lidRq[d], rt = S.wHave[d] && S.lidRt[d];
        if (rq && S.lqHave[d]) {
            float ic[4], D[4]; QConj(S.lqHr[d], ic); QMul(S.ahr, ic, D);
            if (fabsf(D[3]) > 0.94f) { float nq[4]; QMul(D, x.q, nq); memcpy(x.q, nq, sizeof nq); s_lidHeadFix.fetch_add(1, std::memory_order_relaxed); }
        } else if (!rq) { memcpy(S.lqHr[d], S.ahr, sizeof S.lqHr[d]); S.lqHave[d] = true; }
        if (rt && S.ltHave[d]) {
            float ic[4], D[4]; QConj(S.ltHr[d], ic); QMul(S.ahr, ic, D);
            if (fabsf(D[3]) > 0.94f) {
                const float v[3] = { x.t[0] - S.ltHc[d][0], x.t[1] - S.ltHc[d][1], x.t[2] - S.ltHc[d][2] }; float r[3]; QRot(D, v, r);
                for (int k = 0; k < 3; k++) x.t[k] = S.ahc[k] + r[k];
                s_lidHeadFixT.fetch_add(1, std::memory_order_relaxed);
            }
        } else if (!rt) { memcpy(S.ltHr[d], S.ahr, sizeof S.ltHr[d]); memcpy(S.ltHc[d], S.ahc, sizeof S.ltHc[d]); S.ltHave[d] = true; }
    }
    return true;
}
// escribe q / t nuevos y recuerda la base de la que salieron
static bool PaletteMode(int d) { return g_cfg.optExprPalette && d < D_NECKB; }   // (08/11) huesos de la cara: a la paleta, no al esqueleto
static bool WriteBone(Skel& S, int d, const Xf& base, const float* nq, const float* nt) {
    if (PaletteMode(d)) {   // (08/11, Agustin: "todo sigue vibrando") no se toca el esqueleto: se anota y se publica al final del cuadro
        memcpy(S.dq[d], nq, 4 * sizeof(float)); memcpy(S.dt[d], nt, 3 * sizeof(float)); S.dHave[d] = true; S.dFrame[d] = s_frame.load(std::memory_order_relaxed);
        memcpy(S.bq[d], S.gq[d], sizeof S.bq[d]); memcpy(S.bt[d], S.gt[d], sizeof S.bt[d]);   // la base del juego de este cuadro (ReadBase)
        S.wHave[d] = false; S.wFrame[d] = S.dFrame[d]; (void)base;
        return true;
    }
    float out[7] = { nq[0], nq[1], nq[2], nq[3], nt[0], nt[1], nt[2] };
    // (09/11, Agustin: "al alejar la camara todo vibra", log 06:33 con x350 = 3 y expr_paleta = 0: parpados saltando 40 grados, 26000 huesos
    // por resumen leidos con lo del mod puesto) al escribir el ESQUELETO ya no se escala nada por zona (ExprZoneScale, filas 421 / 422): esa
    // cuenta tomaba como base lo que dejo el juego, que en los jugadores lejanos (cara animada a menos cuadros) era lo nuestro del cuadro
    // anterior, y lo volvia a escalar: realimentacion. El 'control total' queda solo para la paleta (expr_paleta = 1); con x350 = 3 el
    // esqueleto se comporta como "Por huesos + parpados" (2)
    if (!SafeCopy((char*)S.cur[d] + 16, out, 28)) return false;
    Xf chk; if (ReadXf(S.cur[d], chk)) { memcpy(S.wq[d], chk.q, sizeof chk.q[0] * 4); memcpy(S.wt[d], chk.t, sizeof chk.t[0] * 3); }
    else { memcpy(S.wq[d], nq, 4 * sizeof(float)); memcpy(S.wt[d], nt, 3 * sizeof(float)); }
    (void)base;   // (02/11 i) la base que se guarda es SIEMPRE la del juego (ReadBase de este cuadro), no la amplificada
    memcpy(S.bq[d], S.gq[d], sizeof S.bq[d]); memcpy(S.bt[d], S.gt[d], sizeof S.bt[d]); S.wHave[d] = true;
    if (S.ahFrame == s_frame.load(std::memory_order_relaxed)) { memcpy(S.bwHr[d], S.ahr, sizeof S.bwHr[d]); memcpy(S.bwHc[d], S.ahc, sizeof S.bwHc[d]); S.bwHave[d] = true; } else S.bwHave[d] = false;
    S.wFrame[d] = s_frame.load(std::memory_order_relaxed);
    return true;
}
// (02/11 d) devuelve el hueso a su base y deja de seguirlo: si el mod no lo mueve este cuadro NO se escribe nada (antes los
// gestos secundarios reescribian cada cuadro pomulos, cejas y bajo-ojo aunque el corrimiento fuera 0; con 1cbce0f la boca 3D
// se rompia con la boca por huesos y volvia con "Por shader", con 0 giros de mandibula escritos)
static void Release(Skel& S, int d) {
    if (PaletteMode(d)) { if (!(S.dHave[d] && S.dFrame[d] == s_frame.load(std::memory_order_relaxed))) S.dHave[d] = false; return; }   // (08/11) nada escrito: nada que devolver
    if (S.wFrame[d] == s_frame.load(std::memory_order_relaxed) && S.wHave[d]) return;   // (02/11 i) ya escrito este cuadro (amplificador)
    if (!S.wHave[d] || !S.cur[d]) { S.wHave[d] = false; return; }
    Xf x; if (ReadBase(S, d, x)) { float out[7] = { x.q[0], x.q[1], x.q[2], x.q[3], x.t[0], x.t[1], x.t[2] }; SafeCopy((char*)S.cur[d] + 16, out, 28); }
    S.wHave[d] = false;
}
// (02/11 i, Agustin: con el amplificador del shader la boca se abria en grietas y los ojos se desorbitaban; sin el, "las
// expresiones vuelven a ser roboticas") AMPLIFICADOR POR HUESOS: el gesto de cada hueso de la cara (actual menos donde estaria
// si siguiera rigido a la cabeza) se agranda por la ganancia de su zona (fila 340.y x fila 341: cejas, ojos, mejillas, boca)
// y se escribe en el esqueleto. Todas las mallas (cara, piezas, barba, pestanas, boca 3D) usan esos huesos: se mueven juntas.
static void Amp(const Skel& S, int d, Xf& x) {
    if (!S.ampOn || d >= kND - 2) return;
    if (d >= D_LID_TL && d <= D_LID_BR) return;   // (08/11) los parpados nunca se agrandan (tambien via Base() en EyesFrame): el gesto medido del parpado salta entre lo del juego y lo corregido
    if (S.nN[d] < 120) return;   // (02/11 n) todavia sin cara neutra de este jugador
    // (04/11, Agustin: "al abrir el juego con las expresiones activadas" la cara hace una mueca un segundo y se arregla) con 8 cuadros
    // la neutra todavia era la cara de la carga: se agrandaba la diferencia con una neutra mala. Ahora espera 120 cuadros (~2 s) y
    // la ganancia entra de a poco en los 120 siguientes
    const float ramp = std::clamp((float)(S.nN[d] - 120) / 120.0f, 0.0f, 1.0f);
    const float g = 1.0f + (S.aGain[d] - 1.0f) * ramp; if (fabsf(g - 1.0f) < 0.005f) return;
    float gF = 1.0f;   // (09/11) rampa de salida cerca del limite (abajo)
    const float* hcU = S.bHave[d] ? S.bHc[d] : S.ahc; const float* hrU = S.bHave[d] ? S.bHr[d] : S.ahr;   // (02/11 l) cabeza de cuando el juego escribio el hueso
    float rr[3]; QRot(hrU, S.nT[d], rr);
    float e[3] = { (x.t[0] - (hcU[0] + rr[0])) * (g - 1.0f), (x.t[1] - (hcU[1] + rr[1])) * (g - 1.0f), (x.t[2] - (hcU[2] + rr[2])) * (g - 1.0f) };
    {   // (03/11, registro "gira 0..120 grados" en el editor) seguro: si lo que dejo el juego ya esta a mas de 2.5 cm o 30 grados de
        // la cara neutra (cambio de cara, editor girando, lectura rara) no se agranda nada: nunca se suma sobre un hueso fuera de rango
        const float raw = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]) / (fabsf(g - 1.0f) > 1e-4f ? fabsf(g - 1.0f) : 1.0f);
        float q0[4], c0[4], d0[4]; QMul(hrU, S.nQ[d], q0); QConj(q0, c0); QMul(x.q, c0, d0);
        const float ang = 2.0f * acosf((std::min)(fabsf(d0[3]), 1.0f));
        if (raw > 0.025f || ang > 0.52f) { s_ampSkip.fetch_add(1, std::memory_order_relaxed); return; }
        // (09/11, Agustin: "las cejas vibran con las expresiones marcadas") el corte era de golpe: un hueso cerca del limite (cejas
        // levantadas, cabeza girando) se agrandaba un cuadro y al siguiente no -> vaiven. Ahora la ganancia se apaga en rampa
        // desde 1.5 cm / 20 grados hasta el limite
        const float fade = (1.0f - SStep(0.015f, 0.025f, raw)) * (1.0f - SStep(0.35f, 0.52f, ang));
        for (float& v : e) v *= fade;
        gF = fade;
    }
    {   // (02/11 m, Agustin: "las opciones de slider no cambian nada") 340.w gestos grandes: lo que el gesto pasa de 1 cm se agranda
        // solo esa fraccion (como hacia el shader), asi la boca muy abierta no se dispara
        const float gl = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]) / (fabsf(g - 1.0f) > 1e-4f ? fabsf(g - 1.0f) : 1.0f);
        if (gl > 0.01f) { const float k = (0.01f + S.aBig * (gl - 0.01f)) / gl; for (float& v : e) v *= k; }
    }
    const float el = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
    if (el > S.aCap) for (float& v : e) v *= S.aCap / el;
    for (int k = 0; k < 3; k++) x.t[k] += e[k];
    float rq[4], c[4], dq[4]; QMul(hrU, S.nQ[d], rq); QConj(rq, c); QMul(x.q, c, dq);
    if (dq[3] < 0) for (float& v : dq) v = -v;
    const float sn = sqrtf(dq[0] * dq[0] + dq[1] * dq[1] + dq[2] * dq[2]);
    if (sn > 1e-6f) {
        float a = 2.0f * atan2f(sn, dq[3]) * (g - 1.0f) * gF;
        a = std::clamp(a, -0.25f, 0.25f);   // tope ~14 grados de giro extra
        const float h = 0.5f * a, k = sinf(h) / sn, eq[4] = { dq[0] * k, dq[1] * k, dq[2] * k, cosf(h) };
        float nq[4]; QMul(eq, x.q, nq); memcpy(x.q, nq, sizeof nq);
    }
}
// (02/11 n) cara neutra: el hueso que dejo el juego, en los ejes de la cabeza del cuadro en que lo escribio; promedio que arranca
// rapido (primeros 120 cuadros) y despues sigue lento (~10 s a 60 cuadros), asi un gesto de unos segundos no se vuelve neutro
static void Neutral(Skel& S, int d, const Xf& g) {
    float ihr[4]; QConj(S.bHr[d], ihr);
    const float v[3] = { g.t[0] - S.bHc[d][0], g.t[1] - S.bHc[d][1], g.t[2] - S.bHc[d][2] }; float lt[3]; QRot(ihr, v, lt);
    float lq[4]; QMul(ihr, g.q, lq);
    const int n = S.nN[d];
    if (n == 0) { memcpy(S.nT[d], lt, sizeof lt); memcpy(S.nQ[d], lq, sizeof lq); S.nN[d] = 1; return; }
    const float a = n < 120 ? 1.0f / (float)(n + 1) : 1.0f / 600.0f;
    for (int k = 0; k < 3; k++) S.nT[d][k] += (lt[k] - S.nT[d][k]) * a;
    const float dt = lq[0] * S.nQ[d][0] + lq[1] * S.nQ[d][1] + lq[2] * S.nQ[d][2] + lq[3] * S.nQ[d][3];
    const float sg = dt < 0 ? -1.0f : 1.0f; float l2 = 0;
    for (int k = 0; k < 4; k++) { S.nQ[d][k] += (sg * lq[k] - S.nQ[d][k]) * a; l2 += S.nQ[d][k] * S.nQ[d][k]; }
    const float il = l2 > 1e-12f ? 1.0f / sqrtf(l2) : 1.0f; for (float& q : S.nQ[d]) q *= il;
    if (S.nN[d] < 1000000) S.nN[d]++;
    // (04/11) los primeros 60 cuadros de cada jugador (carga, la cara todavia acomodandose) no cuentan: el promedio arranca de nuevo
    if (S.nN[d] == 60 && !S.nWarm[d]) { S.nWarm[d] = true; S.nN[d] = 0; }
}
static bool Base(Skel& S, int d, Xf& x) { if (!ReadBase(S, d, x)) return false; Amp(S, d, x); return true; }
// gira la transformacion de un hueso alrededor de su propia posicion: q nuevo = r * q base (r en el espacio del modelo)
static bool RotInPlace(Skel& S, int d, const float* r) {
    Xf x; if (!Base(S, d, x)) return false;
    float nq[4]; QMul(r, x.q, nq);
    return WriteBone(S, d, x, nq, x.t);
}
static void AxisAngle(const float* ax, float a, float* q) { const float s = sinf(0.5f * a); q[0] = ax[0] * s; q[1] = ax[1] * s; q[2] = ax[2] * s; q[3] = cosf(0.5f * a); }
// posicion de un hueso en los ejes de la cabeza, actual menos reposo (metros): cuanto lo movio la animacion facial
static float HeadSpaceMove(Skel& S, int d, const Xf& hc, const float* hrInv) {
    if (!S.cur[d]) return 0.0f;
    Xf x; if (!ReadBase(S, d, x)) return 0.0f;   // sin lo que haya dejado puesto el mod
    const float v[3] = { x.t[0] - hc.t[0], x.t[1] - hc.t[1], x.t[2] - hc.t[2] }; float r[3]; QRot(hrInv, v, r);
    const Xf& R = S.rest[d]; const Xf& H = S.rest[D_HEAD];
    const float dx = r[0] - (R.t[0] - H.t[0]), dy = r[1] - (R.t[1] - H.t[1]), dz = r[2] - (R.t[2] - H.t[2]);
    return sqrtf(dx * dx + dy * dy + dz * dz);
}
static void EyesFrame(Skel& S, const Xf& hc, const float* hr, float sm) {
    const float amp = std::clamp(g_cfg.live[347][3], 0.0f, 2.0f), blinkAmt = std::clamp(g_cfg.live[342][3], 0.0f, 1.0f);
    // (02/11 c, Agustin con video: "por shader los ojos vivos funcionan bien, con los huesos no se mueven y vibran") el globo NO
    // se escribe: el juego vuelve a poner el hueso del ojo despues del Eval del cuello (control propio de la mirada), asi que lo
    // escrito aca no se veia y alternaba con lo del juego. El globo lo vuelve a girar el VS 69758ab9 (PesGaze) y aca solo se
    // mueven los PARPADOS, con la MISMA mirada que el VS: misma cuenta, mismo reloj (157.x) y misma semilla (la entrada de la
    // tabla de tonos de piel mas cercana al personaje, filas 194..254, como PesExprSeed; 0.37 si no tiene entrada)
    const bool eyes = false, lids = S.cur[D_LID_TL] && S.cur[D_LID_TR];
    if (!lids) return;
    float hrInv[4]; QConj(hr, hrInv);
    // (02/11, Agustin: "veo que los ojos vibran"; con la mirada viva en 0 no vibran) el giro relativo sobre lo que deja el juego
    // vibraba: el juego corrige el ojo desde donde quedo el hueso (control propio de la mirada) y los dos se peleaban cuadro a
    // cuadro. Ahora el globo se escribe ABSOLUTO: reposo del ojo llevado por la cabeza actual + la mirada del mod, sin leer lo
    // que dejo el juego (lo que el juego haga con el ojo se pisa entero, sin realimentacion)
    // la animacion facial del juego (habla, festeja): sin parpadeo del mod (la animacion trae el suyo)
    const float anim = (std::max)(HeadSpaceMove(S, D_LIPS_L, hc, hrInv), HeadSpaceMove(S, D_BROW_IL, hc, hrInv));
    const float t = AnimT();
    // (06/11, Agustin: "la barba sigue temblando asi como la boca al mover la camara o adelantar el paso del tiempo, estas animaciones
    // deberian estar ligadas al game clock") los suavizados de los parpados iban por cuadro dibujado: con otro ritmo de cuadros o con
    // el tiempo del juego acelerado / frenado / en pausa no acompanaban. Ahora cuentan cuadros de 60 Hz del RELOJ DEL JUEGO (157.x)
    {
        const float dt = S.clockT < 0.0f ? 1.0f / 60.0f : t - S.clockT;
        S.clockK = std::clamp(dt * 60.0f, 0.0f, 6.0f);   // en pausa 0: no cambia nada; un salto grande cuenta como 6 cuadros
        S.clockT = t;
    }
    auto ease = [&](float a) { return 1.0f - powf(1.0f - a, S.clockK); };
    {
        const float* H = g_cfg.live[194];
        float wp[3];
        if (H[1] > 0.5f && CharNodeWorldPos(const_cast<void*>(S.node), wp)) {   // la tabla esta escrita (la rehace el Present)
            int best = -1; float bd = H[2] * H[2];
            const int n = (std::min)((int)(H[0] + 0.5f), 20);
            for (int i = 0; i < n; i++) { const float* q = g_cfg.live[195 + 3 * i]; const float dx = wp[0] - q[0], dz = wp[2] - q[2], d2 = dx * dx + dz * dz; if (d2 < bd) { bd = d2; best = i; } }
            // (02/11 e, Agustin: "los parpados vibran a lo lejos y de cerca") la tabla la rehace el Present en otro hilo: leida a
            // mitad de camino daba otra entrada un cuadro si y otro no -> la mirada de los parpados saltaba entre dos semillas.
            // La semilla cambia solo si la misma entrada nueva se ve 30 cuadros seguidos
            const float cand = best >= 0 ? (float)best * 0.6180339f : 0.37f;
            if (!S.seedHave) { S.seed = cand; S.seedHave = true; S.seedCandN = 0; }
            else if (cand != S.seed) {
                if (cand == S.seedCand) { if (++S.seedCandN >= 30) { S.seed = cand; S.seedCandN = 0; } }
                else { S.seedCand = cand; S.seedCandN = 1; }
            } else S.seedCandN = 0;
        } else if (!S.seedHave && H[1] <= 0.5f && H[0] <= 0.5f) { S.seed = 0.37f; }
    }
    const float seed = S.seed;
    float g[2]; Gaze(t, seed, amp, g);
    // (08/11, Agustin: "evitar parpadeos o vibraciones raras") la animacion medida (comisura / ceja contra la cabeza) anda cerca del
    // umbral en las animaciones del juego: con la banda angosta de antes (0.8..1.5 mm, cruda) el parpadeo del mod se cortaba a la mitad
    // o titilaba. Ahora banda 0.8..3 mm (como el shader) con histeresis de 0.3 mm y suavizado de ~6 cuadros del reloj del juego
    {
        if (!S.animHave) { S.animM = anim; S.animHave = true; }
        else if (fabsf(anim - S.animM) > 0.0003f) S.animM = anim - copysignf(0.0003f, anim - S.animM);
        const float gate = 1.0f - SStep(0.0008f, 0.0030f, S.animM);
        S.animG = S.animGHave ? S.animG + (gate - S.animG) * ease(0.16f) : gate; S.animGHave = true;
    }
    const float b = BlinkCurve(t, seed) * blinkAmt * S.animG;
    static const float X[3] = { 1.0f, 0.0f, 0.0f }, Y[3] = { 0.0f, 1.0f, 0.0f };
    float ax[3], ay[3]; QRot(hr, X, ax); QRot(hr, Y, ay);
    // angulo positivo alrededor del eje X de la cabeza = el frente baja (como el menton con la boca: 3e57f2e)
    bool wrote = false;
    if (eyes) {
        float qy[4], qp[4], r[4]; AxisAngle(ay, g[0], qy); AxisAngle(ax, -g[1], qp); QMul(qy, qp, r);
        for (int e = 0; e < 2; e++) {
            const int d = D_EYE_L + e;
            float hq[4], nq[4]; QMul(hr, S.rest[d].q, hq); QMul(r, hq, nq);
            Xf x; if (!ReadXf(S.cur[d], x)) { s_badWrite.fetch_add(1, std::memory_order_relaxed); continue; }
            if (SafeCopy((char*)S.cur[d] + 16, nq, 16)) wrote = true; else s_badWrite.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (lids) {
        // (02/11 o, Agustin: "los ojos al moverse no mueven del todo realista el contorno de los parpados") los parpados seguian
        // solo la mirada del mod (PesGaze), no la del JUEGO (el hueso del ojo que mira la pelota / al companero): se suma el giro
        // vertical del globo del juego respecto de la cabeza (frente del ojo en los ejes de la cabeza; + = mira arriba)
        float gp = 0.0f, gyw = 0.0f; int ne = 0;
        for (int e = 0; e < 2; e++) {
            const int d = D_EYE_L + e; if (!S.cur[d]) continue;
            Xf ex; if (!ReadXf(S.cur[d], ex)) continue;
            float rq[4], c[4], rel[4]; QMul(hr, S.rest[d].q, rq); QConj(rq, c); QMul(ex.q, c, rel);   // giro del ojo sobre el reposo llevado por la cabeza
            static const float Z[3] = { 0.0f, 0.0f, 1.0f }; float fz[3], fw[3], fl[3]; QRot(hr, Z, fz); QRot(rel, fz, fw); QRot(hrInv, fw, fl);
            if (fl[2] > 0.2f) { gp += atan2f(fl[1], fl[2]); gyw += atan2f(fl[0], fl[2]); ne++; }
        }
        if (ne) {
            gp = std::clamp(gp / (float)ne, -0.45f, 0.45f);
            // (04/11, Agustin: "vibran" los parpados, las pestanas y la sombra del ojo, todo lo que va con los huesos de los parpados)
            // la mirada del juego se mide con el hueso del ojo contra la cabeza de ESTE momento, y el juego pone el ojo en otro punto
            // del cuadro: con la cabeza en movimiento la medida temblaba ~1 grado de un cuadro a otro y el parpado la seguia 0.8 veces.
            // Ahora: el objetivo solo cambia si la mirada se corre mas de 1.2 grados (histeresis: los saltos de la mirada del juego
            // pasan, el temblor no) y el parpado llega a el con un suavizado de ~8 cuadros
            if (!S.eyePHave) { S.eyePT = S.eyeP = gp; S.eyePHave = true; }
            else {
                if (fabsf(gp - S.eyePT) > 0.021f) S.eyePT = gp - copysignf(0.021f, gp - S.eyePT);
                S.eyeP += (S.eyePT - S.eyeP) * ease(0.12f);
            }
            // (08/11) lo mismo para la mirada de COSTADO (giro alrededor del eje y de la cabeza; + = hacia su izquierda)
            const float yw = std::clamp(gyw / (float)ne, -0.6f, 0.6f);
            if (!S.eyeYHave) { S.eyeYT = S.eyeY = yw; S.eyeYHave = true; }
            else {
                if (fabsf(yw - S.eyeYT) > 0.021f) S.eyeYT = yw - copysignf(0.021f, yw - S.eyeYT);
                S.eyeY += (S.eyeYT - S.eyeY) * ease(0.12f);
            }
        }
        // (04/11, video de Agustin: "los parpados no acompanan") la mirada del mod (Gaze, la misma cuenta que PesGaze del shader)
        // tiene + = mira ABAJO; la del juego (eyeP) + = arriba: se sumaban con signos distintos y el parpado iba al reves de la mirada del mod
        const float gy = -g[1] + (S.eyePHave ? S.eyeP : 0.0f);
        // de arriba: el parpadeo lo baja 0.64 rad (igual que el shader) y acompana la mirada vertical casi entera (como en una cara
        // real: mirar abajo baja el parpado con el ojo, mirar arriba lo sube)
        // (02/11 d) con la sonrisa (sm) el ojo se achina: el de arriba baja un poco y el de abajo sube (sonrisa "de verdad")
        // (04/11) la sonrisa medida en la comisura salta de un cuadro a otro al hablar: para los parpados va suavizada (~10 cuadros)
        S.lidSm = S.lidSmHave ? S.lidSm + (sm - S.lidSm) * ease(0.10f) : sm; S.lidSmHave = true;
        const float smL = S.lidSm;
        // (04/11, Agustin: "el parpadeo hace que se muevan bastante") parpadeo mas suave: arriba 0.64 -> 0.58, abajo 0.12 -> 0.07
        // (05/11, Agustin: "a la sonrisa le falta achicar los ojos un poco mas") con los parpados por huesos (350.y = 2) el shader no
        // mueve los parpados (PesBlinkApply sale antes) y la forma del ojo de los gestos del mod (Sonrisa, Enojo, Asombro, Grito,
        // Puchero) se perdia: solo quedaba la sonrisa medida en la comisura. Ahora tambien aca, las mismas cuentas que el shader con
        // la sonrisa un ~30 % mas fuerte (a 1.0 el de arriba baja 0.20 rad y el de abajo sube 0.21; pasando 1.0, risa)
        // (09/11) los valores SUAVIZADOS de la Transicion (w395), los mismos que la boca: antes los ojos saltaban al valor nuevo
        // mientras la boca todavia iba llegando
        const float G5[3] = { S.sl[0], S.sl[1], S.sl[2] }, G6[2] = { S.sl[4], S.sl[5] };
        const float gAng = std::clamp(G5[0], 0.0f, 1.5f), gSmi = std::clamp(G5[1], 0.0f, 1.5f), gSur = std::clamp(G5[2], 0.0f, 1.5f);
        const float gGri = std::clamp(G6[0], 0.0f, 1.5f), gPuc = std::clamp(G6[1], 0.0f, 1.5f), gBig = (std::max)(gSmi - 1.0f, 0.0f);
        // (06/11, Agustin: "la sonrisa de messi cierra demasiado los ojos, un poco menos") la sonrisa a la mitad de lo que era (la cara
        // esculpida de Messi ya achina el ojo por su forma)
        // (07/11, Agustin: "baja el achinado") sonrisa y enojo otra vez ~40 % menos (a 1.2 los ojos quedaban casi cerrados)
        const float geU = 0.06f * gSmi + 0.08f * gBig + 0.08f * gAng - 0.20f * gSur + 0.10f * gGri + 0.05f * gPuc;   // > 0 baja el de arriba
        const float geL = -(0.06f * gSmi + 0.07f * gBig + 0.03f * gAng + 0.10f * gGri) + 0.04f * gSur;            // < 0 sube el de abajo
        float up = (std::min)(0.58f * b - gy * (gy < 0.0f ? 0.80f : 0.55f) + 0.04f * smL + geU, 0.66f);   // tope: cerrado, sin pasarse del ojo
        // de abajo: sube un poco al parpadear, baja con la mirada abajo y sube apenas con la mirada arriba
        float lo = std::clamp(-0.07f * b - gy * (gy < 0.0f ? 0.35f : 0.15f) - 0.10f * smL + geL, -0.45f, 0.18f);
        if (s_ganiLidOn) { up = (std::min)(up + s_ganiLid[0], 0.66f); lo = std::clamp(lo + s_ganiLid[1], -0.45f, 0.18f); }   // (07/11) parpados del clip .gani
        if (s_extraLidOn) { up = (std::min)(up + s_extraLid[0], 0.66f); lo = std::clamp(lo + s_extraLid[1], -0.45f, 0.18f); }   // (08/11) vida / esfuerzo / concentracion
        {   // (08/11, Agustin: "un slider para cerrar y abrir los ojos") x391: + cierra (1 = cerrados), - abre de mas (-1 = bien abiertos)
            const float ec = std::clamp(g_cfg.live[391][0], -1.0f, 1.0f);
            if (ec > 0.0f) { up = up + (0.62f - up) * ec; lo = lo + (-0.07f - lo) * ec; }
            else if (ec < 0.0f) { up = up + 0.22f * ec; lo = lo - 0.10f * ec; }
            up = (std::min)(up, 0.66f); lo = std::clamp(lo, -0.45f, 0.18f);
        }
        // (04/11) limite de velocidad fuera del parpadeo: un cambio chico de un cuadro a otro (temblor de la medida) se frena;
        // el parpadeo (b) y los saltos grandes pasan enteros
        if (S.lidHave && b < 0.02f) {
            const float kl = ease(0.35f); auto lim = [kl](float prev, float now) { const float d = now - prev; return fabsf(d) < 0.004f ? prev + d * kl : now; };
            up = lim(S.lidU, up); lo = lim(S.lidL, lo);
        }
        S.lidU = up; S.lidL = lo; S.lidHave = true;
        // (08/11, Agustin: "perfecciona los parpados") en una cara real el contorno de los parpados acompana tambien la mirada de COSTADO:
        // el pliegue se corre con la cornea (el de arriba ~12 %, el de abajo ~8 % del giro del ojo). Antes solo seguian la vertical
        const float gx = g[0] + (S.eyeYHave ? S.eyeY : 0.0f);
        S.lidX = S.lidXHave ? S.lidX + (gx - S.lidX) * ease(0.25f) : gx; S.lidXHave = true;
        float qu[4], ql[4];
        { float a1[4], a2[4]; AxisAngle(ax, up, a1); AxisAngle(ay, 0.12f * S.lidX, a2); QMul(a2, a1, qu); }
        { float a1[4], a2[4]; AxisAngle(ax, lo, a1); AxisAngle(ay, 0.08f * S.lidX, a2); QMul(a2, a1, ql); }
        const int ds[4] = { D_LID_TL, D_LID_TR, D_LID_BL, D_LID_BR };
        for (int i = 0; i < 4; i++) {
            if (!S.cur[ds[i]]) continue;
            if (RotInPlace(S, ds[i], i < 2 ? qu : ql)) wrote = true; else s_badWrite.fetch_add(1, std::memory_order_relaxed);
        }
        if (b > 0.5f) s_blinks.fetch_add(1, std::memory_order_relaxed);
    }
    if (wrote) { s_eyesW.fetch_add(1, std::memory_order_relaxed); s_lastEyes.store(s_frame.load(std::memory_order_relaxed), std::memory_order_relaxed); }
}
}   // namespace

// (02/11 d, Agustin: "le falta mas naturalidad a los gestos de las expresiones") MOVIMIENTOS QUE ACOMPANAN al gesto, como en una
// cara real (el juego mueve solo los huesos de la animacion; el resto de la cara queda quieto y se ve de goma):
//  - sonrisa (las comisuras suben / se abren respecto del reposo): los pomulos suben e inflan, el musculo bajo el ojo sube y
//    (en EyesFrame) el parpado de abajo sube y el de arriba baja apenas: el ojo se achina (sonrisa de Duchenne);
//  - boca muy abierta (grito, del juego o del mod): las cejas y el entrecejo suben un poco.
// Por huesos (todas las mallas juntas, sin cortes). Fuerza: 348.x (musculos virtuales, mejilla) para la sonrisa y 348.z (ceno) para
// las cejas; 0 = como antes.
static float Smile(Skel& S, const Xf& hc, const float* hrInv) {
    float best = 0.0f;
    for (int e = 0; e < 2; e++) {
        const int d = D_LIPS_L + e; if (!S.cur[d]) continue;
        Xf x; if (!ReadBase(S, d, x)) continue;
        const float v[3] = { x.t[0] - hc.t[0], x.t[1] - hc.t[1], x.t[2] - hc.t[2] }; float r[3]; QRot(hrInv, v, r);
        const Xf& R = S.rest[d]; const Xf& H = S.rest[D_HEAD];
        // (02/11 p) contra la cara NEUTRA de este jugador (la forma del editor de caras corre la comisura respecto del reposo)
        const bool nn = S.nN[d] >= 120;   // (04/11) misma espera que el amplificador
        const float rx = nn ? S.nT[d][0] : R.t[0] - H.t[0], ry = nn ? S.nT[d][1] : R.t[1] - H.t[1];
        const float dy = r[1] - ry, dx = (r[0] - rx) * (e == 0 ? 1.0f : -1.0f);
        best = (std::max)(best, std::clamp((dy + 0.5f * dx - 0.0012f) / 0.0045f, 0.0f, 1.0f));   // zona muerta 1.2 mm: la cara quieta del juego no es exacto el reposo
    }
    return best * best * (3.0f - 2.0f * best);
}
static void Secondary(Skel& S, const float* hr, float sm, float open) {
    struct Off { int d; float x, y, z; float k; };   // k: 0 = sonrisa (mejilla), 1 = boca abierta, 2 = sonrisa (surco)
    static const Off kOff[] = {
        { 18,  0.0006f, 0.0018f, 0.0010f, 0 }, { 19, -0.0006f, 0.0018f, 0.0010f, 0 },   // cheek_l / r: pomulo arriba y afuera
        // (02/11 p, Agustin: "los sliders de musculos que no sean de hueso real no afectan") 348.y surco nasolabial POR HUESOS: con la
        // sonrisa el hueso del surco sube, va un poco afuera y atras = el pliegue entre la nariz y la comisura se marca
        { 16,  0.0004f, 0.0012f, -0.0008f, 2 }, { 17, -0.0004f, 0.0012f, -0.0008f, 2 },
        // (04/11, Agustin: "le falta mas naturalidad a la sonrisa") sonrisa de verdad (Duchenne): con el slider Sonrisa (355.y,
        // fijo, no medido: no titila) el pomulo sube mas y el bajo-ojo (orbicularisoculi_b) sube y se adelanta = ojo achinado
        { 18,  0.0008f, 0.0022f, 0.0008f, 3 }, { 19, -0.0008f, 0.0022f, 0.0008f, 3 },
        { 20,  0.0002f, 0.0009f, 0.0004f, 3 }, { 21, -0.0002f, 0.0009f, 0.0004f, 3 },   // (07/11) bajo-ojo 1.6 -> 0.9 mm: menos achinado
        // (04/11) los demas gestos tambien mueven pomulos y bajo-ojo: k 4 enojo (ojo apretado, pomulo tenso), 5 asombro (ojo abierto,
        // pomulo abajo), 6 grito (como una sonrisa fuerte), 7 puchero (pomulo abajo, bajo-ojo un poco arriba: cara triste)
        { 18,  0.0003f,  0.0008f, 0.0003f, 4 }, { 19, -0.0003f,  0.0008f, 0.0003f, 4 },
        { 20,  0.0001f,  0.0007f, 0.0002f, 4 }, { 21, -0.0001f,  0.0007f, 0.0002f, 4 },   // (07/11) enojo: bajo-ojo 1.2 -> 0.7 mm
        { 18, 0, -0.0005f, 0, 5 },              { 19, 0, -0.0005f, 0, 5 },
        { 20, 0, -0.0007f, -0.0002f, 5 },       { 21, 0, -0.0007f, -0.0002f, 5 },
        { 18,  0.0010f,  0.0020f, 0.0006f, 6 }, { 19, -0.0010f,  0.0020f, 0.0006f, 6 },
        { 20,  0.0003f,  0.0014f, 0.0005f, 6 }, { 21, -0.0003f,  0.0014f, 0.0005f, 6 },
        { 18, 0, -0.0006f, 0, 7 },              { 19, 0, -0.0006f, 0, 7 },
        { 20, 0,  0.0004f, 0, 7 },              { 21, 0,  0.0004f, 0, 7 },
        // (02/11 g, Agustin: "parpadean mucho abajo de los ojos") el bajo-ojo (orbicularisoculi_b) ya no sube con la sonrisa: la
        // sonrisa medida en la comisura cambia de un cuadro a otro al hablar / abrir la boca y el bajo-ojo titilaba
        // (02/11 g, Agustin: "los parpados ya no vibran solos pero si cuando la boca se abre") las cejas y el entrecejo que subian
        // con la boca abierta se sacan: el juego los reescribe despues (como los parpados) y ademas el shader del parpadeo mide la
        // ceja para saber si la cara esta animada -> con 1.4 mm de mas el parpadeo se prendia y apagaba = parpados vibrando
    };
    const float ks = std::clamp(g_cfg.live[348][0], 0.0f, 2.0f) * (std::max)(sm, s_poseSmile), ko = std::clamp(g_cfg.live[348][2], 0.0f, 2.0f) * open;
    const float kn = std::clamp(g_cfg.live[348][1], 0.0f, 2.0f) * (std::max)(sm, s_poseSmile);
    // varias filas pueden mover el mismo hueso (pomulo: medido + slider): se suman y se escribe una vez por hueso
    float acc[kND][3] = {}; bool use[kND] = {};
    for (const Off& f : kOff) {
        if (!S.cur[f.d]) continue;
        const float a = f.k == 0 ? ks : f.k == 2 ? kn : f.k == 3 ? s_poseSmile : f.k == 4 ? s_poseG[1] : f.k == 5 ? s_poseG[3] : f.k == 6 ? s_poseG[4] : f.k == 7 ? s_poseG[5] : ko;
        if (a <= 0.001f) continue;
        acc[f.d][0] += f.x * a; acc[f.d][1] += f.y * a; acc[f.d][2] += f.z * a; use[f.d] = true;
    }
    if (s_ganiSecOn) for (int d = 18; d <= 21; d++) {   // (07/11) clip .gani
        if (!S.cur[d]) continue; const float* o = s_ganiSec[d - 18]; if (fabsf(o[0]) + fabsf(o[1]) + fabsf(o[2]) < 1e-7f) continue;
        for (int k = 0; k < 3; k++) acc[d][k] += o[k]; use[d] = true;
    }
    if (const FaceProf* PF = (const FaceProf*)s_poseProf) {   // (04/11) perfil de la cara: pomulos y bajo-ojo por gesto
        for (int d = 18; d <= 21; d++) {
            if (!S.cur[d]) continue;
            for (int g = 0; g < PG_N; g++) if (PF->has[g] && s_poseG[g] > 0.001f) {
                const float* o = PF->off[g][d];
                if (fabsf(o[0]) + fabsf(o[1]) + fabsf(o[2]) < 1e-7f) continue;
                for (int k = 0; k < 3; k++) acc[d][k] += o[k] * s_poseG[g];
                use[d] = true;
            }
        }
    }
    bool done[kND] = {};
    for (const Off& f : kOff) {
        const int d = f.d;
        if (!S.cur[d] || done[d]) continue;   // el mismo hueso en dos filas: una sola escritura
        done[d] = true;
        if (!use[d]) { Release(S, d); continue; }
        Xf x; if (!Base(S, d, x)) continue;
        float nt[3] = { x.t[0], x.t[1], x.t[2] };
        { float mv[3]; QRot(hr, acc[d], mv); for (int k = 0; k < 3; k++) nt[k] += mv[k]; }
        if (!WriteBone(S, d, x, x.q, nt)) s_badWrite.fetch_add(1, std::memory_order_relaxed);
    }
}

// ---- (02/11) la CARA EN EL MUNDO para la camara libre "seguir la cara" (freecam.cpp, fila 346) ----
// Agustin: "una camara cercana a la cara que la siga en las animaciones, fija y centrada en el rostro". Antes la cabeza salia de
// la matriz de sk_head copiada de b6 en el dibujo de la cara (expr.cpp) y llegaba casi en cero e intermitente. Ahora sale del
// esqueleto (el mismo que mueve la boca por huesos, revalidado cada cuadro): sk_head actual (espacio del modelo) -> centro de la
// cara (entre los ojos y la boca, un poco adelante) y los ejes frente / arriba de la cabeza -> mundo con la matriz del nodo.
namespace {
struct HeadPub { const void* node = nullptr; float c[3] = {}, fw[3] = {}, up[3] = {}; double t = 0; };
std::mutex s_hpMx; HeadPub s_hp[8];
std::atomic<int> s_hpOk{0}, s_hpFail{0};
}
static bool Norm3(float* v) { const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (!(l > 1e-4f)) return false; v[0] /= l; v[1] /= l; v[2] /= l; return true; }
static void PublishHead(const Skel& S) {
    if (!S.node || !S.bHead) { s_hpFail.fetch_add(1, std::memory_order_relaxed); return; }
    Xf hc; if (!ReadXf(S.bHead, hc)) { s_hpFail.fetch_add(1, std::memory_order_relaxed); return; }
    // la cabeza de un personaje (de pie, sentado o tirado) nunca esta en el origen del modelo; si lo esta, no es espacio del modelo
    if (fabsf(hc.t[0]) + fabsf(hc.t[1]) + fabsf(hc.t[2]) < 0.05f) {
        if (s_hpFail.fetch_add(1, std::memory_order_relaxed) == 0) Log("camara libre: sk_head del cuerpo en el origen del modelo: sin dato de la cara");
        return;
    }
    float c4[4], hr[4]; QConj(S.bHeadRest.q, c4); QMul(hc.q, c4, hr);   // giro de la cabeza respecto del reposo (el reposo mira a +z)
    // centro de la cara respecto de sk_head en el reposo (face_rig: sk_head 0 / 1.640 / 0.054, ojos 1.697 / 0.138, labios ~1.63)
    static const float kOff[3] = { 0.0f, 0.030f, 0.080f }, Z[3] = { 0.0f, 0.0f, 1.0f }, Y[3] = { 0.0f, 1.0f, 0.0f };
    float o[3], fm[3], um[3]; QRot(hr, kOff, o); QRot(hr, Z, fm); QRot(hr, Y, um);
    const float pm[3] = { hc.t[0] + o[0], hc.t[1] + o[1], hc.t[2] + o[2] };
    HeadPub h; h.node = S.node;
    void* nd = const_cast<void*>(S.node);
    if (!CharNodeToWorld(nd, pm, h.c, true) || !CharNodeToWorld(nd, fm, h.fw, false) || !CharNodeToWorld(nd, um, h.up, false) || !Norm3(h.fw) || !Norm3(h.up)) {
        s_hpFail.fetch_add(1, std::memory_order_relaxed); return;
    }
    h.t = Now();
    std::lock_guard<std::mutex> lk(s_hpMx);
    int at = 0; double oldest = 1e300;
    for (int i = 0; i < 8; i++) { if (s_hp[i].node == S.node) { at = i; break; } if (s_hp[i].t < oldest) { oldest = s_hp[i].t; at = i; } }
    s_hp[at] = h;
    if (s_hpOk.fetch_add(1, std::memory_order_relaxed) == 0)
        Log("camara libre: cara en el mundo desde el esqueleto (%.2f, %.2f, %.2f), frente (%.2f, %.2f, %.2f), arriba (%.2f, %.2f, %.2f); sk_head en el modelo (%.3f, %.3f, %.3f)",
            h.c[0], h.c[1], h.c[2], h.fw[0], h.fw[1], h.fw[2], h.up[0], h.up[1], h.up[2], hc.t[0], hc.t[1], hc.t[2]);
    {   // (02/11 e) cada ~10 s mientras se sigue: para ver si la cabeza se mueve con la animacion
        static double s_lt = 0; static int s_ln = 0; const double now = h.t;
        if (now - s_lt > 10.0 && s_ln < 30) { s_lt = now; s_ln++;
            Log("camara libre: cara (%.3f, %.3f, %.3f) frente (%.2f, %.2f, %.2f) arriba (%.2f, %.2f, %.2f); sk_head cuerpo #%d modelo (%.3f, %.3f, %.3f) q (%.3f, %.3f, %.3f, %.3f), reposo (%.3f, %.3f, %.3f)",
                h.c[0], h.c[1], h.c[2], h.fw[0], h.fw[1], h.fw[2], h.up[0], h.up[1], h.up[2], S.bHeadIdx, hc.t[0], hc.t[1], hc.t[2], hc.q[0], hc.q[1], hc.q[2], hc.q[3],
                S.bHeadRest.t[0], S.bHeadRest.t[1], S.bHeadRest.t[2]); }
    }
}
// la cara a seguir: la del personaje fijado (*lock) si sigue al dia; si no, la que esta mas al centro de la vista de la camara
bool ExprFaceAny() {   // (08/11 e) hay alguna cara con dato fresco (< 0.5 s): la usa la camara libre para saber si la recarga termino
    std::lock_guard<std::mutex> lk(s_hpMx); const double now = Now();
    for (int i = 0; i < 8; i++) if (s_hp[i].node && now - s_hp[i].t < 0.5) return true;
    return false;
}
bool ExprFaceFrame(const float* camPos, const float* camFwd, const void** lock, float c[3], float fw[3], float up[3], double* age) {
    std::lock_guard<std::mutex> lk(s_hpMx);
    const double now = Now();
    int best = -1;
    if (lock && *lock) for (int i = 0; i < 8; i++) if (s_hp[i].node == *lock && now - s_hp[i].t < 0.5) { best = i; break; }
    if (best < 0) {
        float bs = -1e9f;
        for (int i = 0; i < 8; i++) {
            if (!s_hp[i].node || now - s_hp[i].t >= 0.5) continue;
            float d[3] = { s_hp[i].c[0] - camPos[0], s_hp[i].c[1] - camPos[1], s_hp[i].c[2] - camPos[2] };
            const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            const float sc = (l > 1e-3f ? (d[0] * camFwd[0] + d[1] * camFwd[1] + d[2] * camFwd[2]) / l : 1.0f) - 0.002f * l;   // al centro y, a igualdad, cerca
            if (sc > bs) { bs = sc; best = i; }
        }
        if (best < 0) return false;
        if (lock) *lock = s_hp[best].node;
    }
    memcpy(c, s_hp[best].c, 12); memcpy(fw, s_hp[best].fw, 12); memcpy(up, s_hp[best].up, 12);
    if (age) *age = now - s_hp[best].t;
    return true;
}

// en el Eval de la restriccion del cuello (musculo.cpp hk_Eval, clase "cuello"): una vez por personaje y cuadro
// (04/11) que jugador es este esqueleto y que perfil de caras.ini le toca: al armar, al recargar caras.ini y cada ~2 s mientras
// personajes.cpp todavia no sabe quien es. Una linea por jugador nuevo: "caras: jugador <id> '<nombre>' -> perfil ..."
static int s_profLogs = 0;
static void ProfResolve(Skel& S, unsigned fr) {
    if (S.profGen == s_profGen && (S.pKnown || fr - S.profFrame < 60)) return;
    S.profGen = s_profGen; S.profFrame = fr;
    const int slot = CharSlotForSkel(S.skel);
    CharInfo ci; if (slot < 0 || !CharGet(slot, ci) || !ci.known) { S.pKnown = false; S.prof = -1; return; }
    const bool changed = !S.pKnown || S.pid != ci.id;
    S.pKnown = true; S.pid = ci.id;
    const int was = S.prof; S.prof = ProfFind(ci.id, ci.name);
    if ((changed || was != S.prof) && s_profLogs < 80) {
        s_profLogs++;
        if (S.prof >= 0) Log("caras: jugador %u '%s' -> perfil [%s]", ci.id, ci.name, s_prof[S.prof].sec);
        else Log("caras: jugador %u '%s' -> sin perfil", ci.id, ci.name);
    }
}
// (08/11) CUELLO en los gestos (fila 391.y = cuanto, 0 = nada, 1 = normal): al gritar, enojarse o apretar los dientes se tensan el
// esternocleidomastoideo y los tendones del cuello (se marcan adelante y hacia el medio), y con la boca muy abierta la piel de abajo del
// menton baja un poco. Corrimiento en los ejes del CUELLO (sk_neck actual contra su reposo: no siguen a la cabeza cuando gira), en metros.
// Todo con sliders fijos o medidas ya suavizadas (sin temblor); el juego no anima estos huesos en la cara, asi que sin gesto se sueltan
static void Neck(Skel& S, float tension, float open) {
    const float k = std::clamp(g_cfg.live[391][1], 0.0f, 2.0f);
    const float T = std::clamp(tension, 0.0f, 1.5f) * k, O = std::clamp(open, 0.0f, 1.0f) * k;
    float nr[4] = { 0, 0, 0, 1 };
    if (S.cur[D_NECK]) { Xf nc; if (ReadXf(S.cur[D_NECK], nc)) { float c[4]; QConj(S.rest[D_NECK].q, c); QMul(nc.q, c, nr); } }
    else if (S.cur[D_HEAD]) { memcpy(nr, S.ahr, sizeof nr); }
    struct NOff { int d; float t[3], o[3]; };   // t = por tension, o = por boca abierta
    static const NOff kN[] = {
        { D_NECKB,  { 0, 0, -0.0004f },              { 0, -0.0012f, 0.0004f } },   // abajo del menton: baja con la boca abierta
        { D_SCM,    { 0, -0.0003f, 0.0012f },        { 0, -0.0006f, 0.0003f } },   // musculo del cuello: se adelanta tenso
        { D_CLAV_L, {  -0.0009f, 0, 0.0018f },       { 0, 0, 0 } },                // tendones: adelante y hacia el medio
        { D_CLAV_R, {   0.0009f, 0, 0.0018f },       { 0, 0, 0 } },
    };
    for (const NOff& f : kN) {
        const int d = f.d; if (!S.cur[d]) continue;
        const float v[3] = { f.t[0] * T + f.o[0] * O, f.t[1] * T + f.o[1] * O, f.t[2] * T + f.o[2] * O };
        if (fabsf(v[0]) + fabsf(v[1]) + fabsf(v[2]) < 2e-6f) { Release(S, d); continue; }
        Xf x; if (!ReadBase(S, d, x)) continue;
        float mv[3]; QRot(nr, v, mv);
        const float nt[3] = { x.t[0] + mv[0], x.t[1] + mv[1], x.t[2] + mv[2] };
        if (WriteBone(S, d, x, x.q, nt)) s_neckW.fetch_add(1, std::memory_order_relaxed); else s_badWrite.fetch_add(1, std::memory_order_relaxed);
    }
}
static void FrameBody(const void* skel) {
    if (!skel) return;
    s_calls.fetch_add(1, std::memory_order_relaxed);
    const unsigned fr = s_frame.load(std::memory_order_relaxed);
    const bool on = g_cfg.live[350][0] > 0.5f && g_cfg.live[340][0] > 0.5f;
    // (02/11, Agustin: "sacar la lengua de forma realista y mostrar mas los dientes") 350.z sacar la lengua (la lengua la
    // estira el shader, PesTongue; para que salga la boca se abre al menos 0.45 x 350.z), 350.w mostrar los dientes (los
    // labios se separan de los dientes: el de arriba sube, el de abajo baja, las comisuras se abren)
    const float tongueOut = std::clamp(g_cfg.live[350][2], 0.0f, 1.0f), teeth = std::clamp(g_cfg.live[350][3], 0.0f, 1.5f);
    // (03/11, Agustin: "en las animaciones del juego ahora las barbas tambien se ven mal") con x347 negativo (-0.25 en su ini) la
    // DLL cerraba la mandibula de la cara en CADA cuadro, tambien en las animaciones del juego, y la malla de la barba no recibe
    // ese giro (sus huesos salen de otro lado): quedaba delante de la boca. Por huesos ya no se cierra: 0 o mas
    // (08/11, Agustin: "agrega mas sliders de animaciones con mas realismo y ajusta las presentes") los sliders de gestos (355 / 356 /
    // 361 / 362 / 365) se leen crudos aca y pasan por el suavizado w395 (Transicion) mas abajo, cuando ya se tiene el esqueleto
    float slR[16];
    { const float* PZ = g_cfg.live[355]; const float* PX = g_cfg.live[356]; const float* PB = g_cfg.live[361]; const float* PC = g_cfg.live[362];
      for (int i = 0; i < 4; i++) slR[i] = std::clamp(PZ[i], 0.0f, 1.5f);
      for (int i = 0; i < 3; i++) slR[4 + i] = std::clamp(PX[i], 0.0f, 1.5f);
      for (int i = 0; i < 4; i++) { slR[7 + i] = std::clamp(PB[i], -1.5f, 1.5f); slR[11 + i] = std::clamp(PC[i], -1.5f, 1.5f); }
      slR[15] = std::clamp(g_cfg.live[365][0], -1.5f, 1.5f); }
    const bool profOn = g_cfg.live[356][3] > 0.5f;
    std::lock_guard<std::mutex> lk(s_mx);
    if (s_profCheck == 0xffffffffu || fr - s_profCheck >= 300) { s_profCheck = fr; ProfLoad(); }   // caras.ini: al arrancar y cada ~10 s si cambio
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
    Xf jc; if (!ReadBase(*S, D_JAW, jc)) { Build(*S, skel); S->rebuilds++; return; }
    s_lastActive.store(fr, std::memory_order_relaxed);
    // (09/11, Agustin: "no anda todo mal, solo de lejos") ALCANCE por huesos (z391, metros; 0 = sin limite): mas lejos de la camara del motor que
    // esto el mod no escribe el esqueleto de la cara (devuelve lo escrito y sale): la cara ya no se ve a ese tamano y el juego anima a los
    // jugadores lejanos a menos cuadros, donde cada escritura nuestra quedaba un cuadro si y otro no (vaiven)
    {
        const float lim = g_cfg.live[391][2];
        if (lim > 0.5f && S->node && S->bHead) {
            Xf bh; float w[3], cp[3], ct[3];
            if (ReadXf(S->bHead, bh) && CharNodeToWorld(const_cast<void*>(S->node), bh.t, w, true) && EngineCamera(cp, ct, nullptr)) {
                const float dx = w[0] - cp[0], dy = w[1] - cp[1], dz = w[2] - cp[2], dist = sqrtf(dx * dx + dy * dy + dz * dz);
                const float on = S->farOff ? lim * 0.9f : lim * 1.1f;   // histeresis +-10 %: no se prende y apaga en el borde
                S->farOff = dist > on;
                if (S->farOff) { for (int d = 0; d < kND; d++) if (S->wHave[d]) Release(*S, d); s_far.fetch_add(1, std::memory_order_relaxed); return; }
            }
        } else S->farOff = false;
    }
    ProfResolve(*S, fr);
    const FaceProf* PF = profOn && S->prof >= 0 && S->prof < (int)s_prof.size() ? &s_prof[S->prof] : nullptr;
    const float tNow = AnimT();
    // (08/11) w395 TRANSICION: los sliders de gestos no saltan: van al valor nuevo con una constante de 0.25 s x w395 (0 = como antes)
    {
        const float tr = std::clamp(g_cfg.live[395][3], 0.0f, 3.0f);
        const float dt = S->slT < 0.0f || tNow < S->slT ? 1.0f : (std::min)(tNow - S->slT, 0.5f); S->slT = tNow;
        if (!S->slHave || tr < 0.001f) { for (int i = 0; i < 16; i++) { S->sl[i] = slR[i]; S->slV[i] = 0.0f; } }
        else for (int i = 0; i < 16; i++) Damp(S->sl[i], S->slV[i], slR[i], 0.25f * tr, dt);   // resorte critico: arranca y frena suave
        S->slHave = true;
    }
    const float pAng = S->sl[0], pSmi = S->sl[1], pSur = S->sl[2], pTalk = S->sl[3], pGri = S->sl[4], pPuc = S->sl[5], pNeu = S->sl[6];
    s_poseSmile = pSmi;
    // hablar: la mandibula sube y baja con un ritmo de silabas irregular (dos ondas que no se repiten juntas), por personaje distinto
    float talkO = 0.0f, talkRound = 0.0f, talkLip = 0.0f, talkLow = 0.0f, talkProt = 0.0f, talkPress = 0.0f, talkAcc = 0.0f;
    if (pTalk > 0.001f) {
        // (04/11, Agustin: "el de hablar va muy rapido") z354 = velocidad del habla (1 = la de antes, ~4 silabas por segundo;
        // 0 o sin poner = 0.55, mas pausado y con mas tiempo con la boca quieta entre frases)
        const float sp = g_cfg.live[354][2] > 0.01f ? std::clamp(g_cfg.live[354][2], 0.2f, 1.5f) : 0.55f;
        if (g_cfg.live[396][3] > 0.5f) {   // (08/11) w396 = 1: por PALABRAS (visemas, frases de anim\hablar.txt)
            float ch[7]; TalkWords(*S, tNow, sp, ch);
            talkO = pTalk * 0.30f * ch[0]; talkRound = pTalk * ch[1]; talkLip = pTalk * ch[2]; talkLow = pTalk * ch[3];
            talkProt = pTalk * ch[4]; talkPress = pTalk * ch[5]; talkAcc = pTalk * ch[6];
        } else {   // silabas: dos ondas que no se repiten juntas, por personaje distinto
            const float t = (tNow + (float)(((uintptr_t)skel >> 6) & 63) * 0.37f) * sp;
            const float w = 0.6f * sinf(t * 6.2831853f * 4.1f) + 0.4f * sinf(t * 6.2831853f * 2.3f + 1.3f);
            const float env = (std::max)(0.0f, 0.40f + 0.60f * sinf(t * 6.2831853f * 0.37f));   // frases: a veces habla mas, a veces menos, con pausas
            talkO = pTalk * 0.30f * (std::max)(w, 0.0f) * env;
            talkRound = pTalk * sinf(t * 6.2831853f * 1.7f + 0.6f) * env;   // labios que se redondean (o / u) y se estiran (e / i)
            // (08/11, Agustin: "ajusta las presentes para presentar movimientos mas realistas") al hablar no se mueve solo la mandibula: el
            // labio de arriba sube y el de abajo baja con cada silaba (consonantes labiales: se juntan en los valles de la onda)
            talkLip = pTalk * (std::max)(w + 0.15f, 0.0f) * env; talkLow = talkLip * 0.83f;
        }
    }
    const float o = (std::max)((std::max)(std::clamp(g_cfg.live[347][0], 0.0f, 1.0f), 0.45f * tongueOut), (std::min)(1.0f, 0.40f * pSur + 0.85f * pGri + talkO + 0.60f * (std::max)(0.0f, pSmi - 1.0f)));   // (04/11 b, Agustin: "la sonrisa con los dientes cerrados") la sonrisa ya no abre la mandibula (antes 0.12): los labios se separan, los dientes quedan juntos; (04/11 c) pasando 1.0 la sonrisa grande abre la boca (1.5 = 0.30, como en la foto de Messi riendo)
    // (04/11 d) filas 361 / 362, por huesos, de -1.5 a 1.5 (0 = nada): x361 cejas, y361 mordida, z361 mandibula de costado, w361 media
    // sonrisa; x362 cachetes, y362 beso, z362 desprecio, w362 una ceja. (04/11 f) x365 angulo de las cejas (+ sube el inicio)
    // (09/11, Agustin: "la sonrisa parece afectada por la gravedad") pasando 1.0 de Sonrisa la boca se abre con la mandibula y las
    // comisuras / mejillas de abajo (peso 0.5 en la mandibula) bajaban con ella y se juntaban (kOpen): a 1.25 las comisuras quedaban
    // ~2 mm MAS ABAJO que a 1.0 y el labio de abajo colgaba = la sonrisa se "caia". En una risa real las comisuras las sostiene el
    // cigomatico: la parte de la apertura que viene de la sonrisa ya no las arrastra (oSmi = lo que la sonrisa agrega a la apertura)
    const float oRest = (std::max)((std::max)(std::clamp(g_cfg.live[347][0], 0.0f, 1.0f), 0.45f * tongueOut), (std::min)(1.0f, 0.40f * pSur + 0.85f * pGri + talkO));
    const float oSmi = (std::max)(0.0f, o - oRest);
    float xB[8]; for (int i = 0; i < 8; i++) xB[i] = S->sl[7 + i];
    const float xAng = S->sl[15];
    bool xOn = fabsf(xAng) > 0.001f; for (float v : xB) if (fabsf(v) > 0.001f) xOn = true;
    bool poseOn = pAng > 0.001f || pSmi > 0.001f || pSur > 0.001f || pGri > 0.001f || pPuc > 0.001f || fabsf(talkRound) > 0.001f || pTalk > 0.001f || xOn;
    // (08/11, Agustin: "hincharse las mejillas, soplar y entrecerrar un poco los ojos podria ser una animacion cuando el jugador esta
    // quieto o a punto de correr") velocidad del jugador: la cabeza del cuerpo en el mundo (CharNodeToWorld), suavizada con el reloj
    bool arranco = false;
    if (S->node && S->bHead && tNow >= 0.0f) {
        Xf bh; float w[3];
        if (ReadXf(S->bHead, bh) && CharNodeToWorld(const_cast<void*>(S->node), bh.t, w, true)) {
            if (S->spHave && tNow > S->spT + 0.0005f && tNow - S->spT < 0.5f) {
                const float dt = tNow - S->spT, dx = w[0] - S->spP[0], dz = w[2] - S->spP[2];
                const float v = sqrtf(dx * dx + dz * dz) / dt, before = S->speed;
                S->speed += ((std::min)(v, 12.0f) - S->speed) * (1.0f - expf(-dt / 0.25f));
                if (S->speed < 0.5f) S->stillT += dt; else if (S->speed > 1.0f) { if (before <= 1.0f && S->stillT > 1.0f) arranco = true; S->stillT = 0.0f; }
            }
            if (!S->spHave || tNow != S->spT) { memcpy(S->spP, w, sizeof w); S->spT = tNow; S->spHave = true; }
        }
    }
    // (08/11, Agustin: "agrega mas sliders de animaciones con mas realismo") ANIMACIONES REALISTAS, filas 395 / 396, por huesos:
    //   x395 Vida: la cara nunca esta del todo quieta: cejas, comisuras y alas de la nariz se mueven decimas de milimetro, lento y sin
    //        repetirse (tres ondas que no son multiplos), distinto por jugador; los parpados se entrecierran apenas de a ratos
    //   y395 Respiracion: ciclo de ~0.22 Hz quieto y hasta ~1 Hz corriendo: las alas de la nariz se abren al inhalar; corriendo
    //        (> 2 m/s) respira por la boca: los labios se separan y la mandibula baja un poco con cada inhalacion
    //   z395 Esfuerzo: segun la velocidad medida (de 2.5 a 7 m/s), sube en ~1 s y baja en ~4 s al parar: boca algo abierta, cejas
    //        abajo y al medio, ojos entrecerrados, comisuras tensas, alas de la nariz abiertas, cuello tenso
    //   w395 Transicion (arriba): suavizado de los sliders.   x396 Asimetria: el lado izquierdo del gesto mas fuerte y el derecho
    //        mas flojo (15 % a 1.0) y una ceja apenas mas alta: ninguna cara real es simetrica.   y396 Concentracion: cejas abajo
    //        y juntas, entrecejo adelante, parpados entrecerrados, labios apretados, comisuras adentro.   z396 Expresividad: ganancia
    //        de todos los gestos por huesos del mod (1 = como estan; 0 o sin poner = 1).   w396 reservada
    const float nVida = std::clamp(g_cfg.live[395][0], 0.0f, 1.5f), nResp = std::clamp(g_cfg.live[395][1], 0.0f, 1.5f), nEsf = std::clamp(g_cfg.live[395][2], 0.0f, 1.5f);
    const float nAsim = std::clamp(g_cfg.live[396][0], 0.0f, 1.5f), nConc = std::clamp(g_cfg.live[396][1], 0.0f, 1.5f);
    // (09/11, Agustin: "el slider de expresividad esta mal planteado"; en su ini z396 habia quedado en 0.1 y TODOS los gestos salian al 10 %:
    // "los sliders no hacen nada") z396 ya no escala nada; queda reservada hasta definir que tiene que hacer
    const float nExpr = 1.0f;
    float eff = 0.0f, breath = 0.0f, brAmp = 0.0f, lifeBrow = 0.0f, lifeLipX = 0.0f, lifeLipY = 0.0f, lifeNose = 0.0f, lifeLid = 0.0f;
    {
        const float dt = S->anT < 0.0f || tNow < S->anT ? 0.0f : (std::min)(tNow - S->anT, 0.5f); S->anT = tNow;
        const float spd = S->spHave ? S->speed : 0.0f;
        const float target = SStep(2.5f, 7.0f, spd);
        S->effort += (target - S->effort) * (1.0f - expf(-dt / (target > S->effort ? 1.0f : 4.0f)));
        eff = nEsf * S->effort;
        const float rate = (std::min)(0.22f + 0.10f * spd, 1.0f);
        S->brPh = Frac(S->brPh + dt * rate);
        // (09/11, Agustin con video FC27 / eFootball: "a esto me referia") ciclo real: inhala en el 40 % del ciclo, exhala mas lento y queda
        // una pausa corta al final con la boca apenas entreabierta; subida y bajada suaves (sin esquinas)
        { const float ph = S->brPh; breath = ph < 0.40f ? SStep(0.0f, 0.40f, ph) : ph < 0.88f ? 1.0f - SStep(0.40f, 0.88f, ph) : 0.0f; }
        brAmp = nResp < 0.001f ? 0.0f : nResp * (0.45f + 0.55f * SStep(1.0f, 6.0f, spd));   // (09/11) 0 = apagada del todo
        if (nVida > 0.001f) {
            const float ph = (float)(S->pid % 9973u) * 0.0731f;
            auto ns = [&](float f, float p) { return sinf(tNow * 6.2831853f * f + p + ph); };
            lifeBrow = nVida * (0.5f * ns(0.13f, 0.0f) + 0.3f * ns(0.31f, 1.7f) + 0.2f * ns(0.071f, 3.1f));
            lifeLipX = nVida * (0.5f * ns(0.17f, 0.9f) + 0.5f * ns(0.41f, 2.2f));
            lifeLipY = nVida * (0.6f * ns(0.11f, 2.6f) + 0.4f * ns(0.29f, 0.3f));
            lifeNose = nVida * (0.7f * ns(0.19f, 1.4f) + 0.3f * ns(0.47f, 2.9f));
            lifeLid  = nVida * (std::max)(0.0f, 0.6f * ns(0.09f, 1.1f) + 0.4f * ns(0.23f, 0.4f) - 0.3f);
        }
    }
    const bool nLive = nVida > 0.001f || brAmp > 0.001f || eff > 0.001f || nAsim > 0.001f || nConc > 0.001f;
    if (nLive) poseOn = true;
    s_extraLid[0] = 0.05f * lifeLid + 0.07f * eff + 0.08f * nConc;
    s_extraLid[1] = -(0.03f * eff + 0.04f * nConc);
    s_extraLidOn = fabsf(s_extraLid[0]) > 1e-4f || fabsf(s_extraLid[1]) > 1e-4f;
    if (PF && pNeu > 0.001f && PF->has[PG_NEUTRA]) poseOn = true;
    // (07/11, Agustin: "me gustaria que al jugar leas los ganis") clip .gani de la cara (fila 383, gani.cpp): la pose del clip en el
    // RELOJ DEL JUEGO desde que arranco en este personaje; x = clip, y = peso, z = velocidad, w = 0 bucle / 1 una vez
    GaniPose GP; bool ganiOn = false; float gW = 0.0f;
    int gc = 0; float gF = 0.0f;
    {
        if (!s_defToAskOk) DefToAsk();
        gc = (int)(g_cfg.live[383][0] + 0.5f); gW = std::clamp(g_cfg.live[383][1], 0.0f, 1.5f);
        const float gSp = g_cfg.live[383][2] > 0.01f ? std::clamp(g_cfg.live[383][2], 0.1f, 4.0f) : 1.0f; const bool gLoop = g_cfg.live[383][3] < 0.5f;
        const float gt = AnimT();
        if (gc > 0 && gW > 0.001f) {
            if (S->ganiClip != gc || gt < S->ganiT0) { S->ganiClip = gc; S->ganiT0 = gt; }
            bool done = false; ganiOn = GaniSample(gc, (gt - S->ganiT0) * gSp, gLoop, GP, &done);
            if (ganiOn) { gF = (gt - S->ganiT0) * gSp * 30.0f; if (gLoop && GP.frames > 0) gF = fmodf((std::max)(gF, 0.0f), (float)GP.frames); }
        } else { S->ganiClip = 0; gc = 0; }
    }
    // (08/11, Agustin: "transiciones en los gani se puede?", "un metodo seguro, practico y avanzado") INERTIALIZACION del clip: en cada
    // transicion (clip que empieza, termina, cambia, o bucle que da la vuelta) se toma la diferencia entre la pose mostrada (y su velocidad)
    // y la pose nueva, por hueso (3 de posicion + 3 de giro como eje x angulo), y se apaga con el polinomio de grado 5 en y397 s (0.15;
    // 0 = de golpe). El clip nuevo corre entero desde su primer cuadro; mientras queda diferencia el clip sigue mandando (los gestos
    // propios esperan). Pose nueva = clip x peso y383, o reposo si no hay clip
    {
        const float tau = std::clamp(g_cfg.live[397][1], 0.0f, 2.0f);
        const float gt2 = AnimT(); const float dtb = S->gBT < 0.0f || gt2 < S->gBT ? 1.0f / 60.0f : std::clamp(gt2 - S->gBT, 1e-4f, 0.25f); S->gBT = gt2;
        GaniPose T = {};   // pose nueva
        for (int b = 0; b < 31; b++) { T.q[b][3] = 1.0f; if (ganiOn && GP.has[b]) { QScale(GP.q[b], gW, T.q[b]); for (int c = 0; c < 3; c++) T.p[b][c] = GP.p[b][c] * gW; T.has[b] = true; } }
        T.frames = ganiOn ? GP.frames : 1;
        const bool trans = S->gBHave && tau >= 0.001f && (gc != S->gLastClip || ganiOn != S->gLastOn || (ganiOn && gF + 0.5f < S->gLastF));
        if (trans) {
            // velocidad de lo mostrado: (mostrado - anterior) / dt del cuadro anterior
            const float idt = S->gPrevDt > 1e-5f ? 1.0f / S->gPrevDt : 0.0f;
            for (int b = 0; b < 31; b++) {
                float x0[3], v0[3]; for (int c = 0; c < 3; c++) { x0[c] = S->gB.p[b][c] - T.p[b][c]; v0[c] = (S->gB.p[b][c] - S->gPrev.p[b][c]) * idt; }
                float cq[4], dq[4], r0[3]; QConj(T.q[b], cq); QMul(S->gB.q[b], cq, dq); QLog(dq, r0);
                float cp[4], vq[4], rv[3]; QConj(S->gPrev.q[b], cp); QMul(S->gB.q[b], cp, vq); QLog(vq, rv); for (float& v : rv) v *= idt;
                // (08/11 h, Agustin: "quizas el metodo de transicion no es el correcto, enfocate en uno mas seguro, transicion suave") sin
                // velocidad heredada: la diferencia se apaga con el mismo polinomio pero arrancando quieta (ease-out puro): no puede
                // pasarse ni rebotar aunque la velocidad medida venga rara (dt variable, hueso leido con lo del mod puesto)
                (void)v0; (void)rv;
                for (int c = 0; c < 3; c++) { InertStart(S->gI[b][c], x0[c], 0.0f, tau); InertStart(S->gI[b][3 + c], r0[c], 0.0f, tau); }
            }
            S->gIT0 = gt2;
        }
        S->gLastClip = gc; S->gLastOn = ganiOn; S->gLastF = gF;
        S->gPrev = S->gB; S->gPrevDt = dtb;
        bool any = false; const float ti = S->gIT0 < 0.0f ? 1e9f : gt2 - S->gIT0;
        for (int b = 0; b < 31; b++) {
            float off[3], rof[3]; bool rest = false;
            for (int c = 0; c < 3; c++) { off[c] = InertEval(S->gI[b][c], ti); rof[c] = InertEval(S->gI[b][3 + c], ti); if (fabsf(off[c]) > 1e-6f || fabsf(rof[c]) > 1e-5f) rest = true; }
            for (int c = 0; c < 3; c++) S->gB.p[b][c] = T.p[b][c] + off[c];
            if (rest) { float eq[4]; QExp(rof, eq); QMul(eq, T.q[b], S->gB.q[b]); } else memcpy(S->gB.q[b], T.q[b], sizeof T.q[b]);
            const bool live = T.has[b] || fabsf(S->gB.p[b][0]) + fabsf(S->gB.p[b][1]) + fabsf(S->gB.p[b][2]) > 2e-4f || S->gB.q[b][3] < 0.9999995f;
            S->gB.has[b] = live; if (live) any = true;
        }
        S->gB.frames = T.frames;
        if (!S->gBHave) { S->gPrev = S->gB; S->gBHave = true; }
        if (tau >= 0.001f) { ganiOn = any; if (any) { GP = S->gB; gW = 1.0f; } }
    }
    if (ganiOn) poseOn = true;
    s_ganiSecOn = false; s_ganiLidOn = false;
    if (ganiOn) {
        for (int d = 18; d <= 21; d++) { const int ab = s_defToAsk[d]; float ph[3] = {}; if (ab >= 0 && GP.has[ab]) GaniPToHead(GP.p[ab], ph); for (int k = 0; k < 3; k++) s_ganiSec[d - 18][k] = ph[k] * gW; }
        s_ganiSecOn = true;
        // parpados: en el clip van absolutos (base_normal: arriba 25 grados = abierto, abajo -5); el delta contra eso entra en EyesFrame
        float at = 0, ab2 = 0; int nt = 0, nb = 0;
        for (int b = 14; b <= 15; b++) if (GP.has[b]) { at += GaniAngZ(GP.q[b]); nt++; }
        for (int b = 16; b <= 17; b++) if (GP.has[b]) { ab2 += GaniAngZ(GP.q[b]); nb++; }
        if (nt || nb) { s_ganiLid[0] = nt ? -(at / nt - 0.43633f) * gW : 0.0f; s_ganiLid[1] = nb ? -(ab2 / nb + 0.08727f) * gW : 0.0f; s_ganiLidOn = true; }
    }
    s_poseG[0] = pNeu; s_poseG[1] = pAng; s_poseG[2] = pSmi; s_poseG[3] = pSur; s_poseG[4] = pGri; s_poseG[5] = pPuc; s_poseProf = PF;
    if (FreeCamActive() && g_cfg.live[346][0] > 0.5f) PublishHead(*S);   // (02/11) camara libre "seguir la cara"
    float deg = 0.0f; if (JawAngle(*S, jc, &deg)) { S->angMin = (std::min)(S->angMin, deg); S->angMax = (std::max)(S->angMax, deg); S->angN++; }
    // (03/11, Agustin: con x340 = 0 "no cambia nada") al apagar, lo ultimo escrito quedaba puesto: el juego no reescribe todos los
    // huesos de la cara en cada cuadro (editor quieto), asi que la cara seguia deformada. Ahora al apagar se devuelve la base del juego
    if (!on) { for (int d = 0; d < kND - 2; d++) if (S->wHave[d]) Release(*S, d); return; }
    // eje X de la cabeza actual (el reposo del skl mira a +z, x a la izquierda) y pivote = mandibula actual
    Xf hc; if (!ReadXf(S->cur[D_HEAD], hc)) return;
    float c[4], hr[4]; QConj(S->rest[D_HEAD].q, c); QMul(hc.q, c, hr);
    float hrInv[4]; QConj(hr, hrInv);
    {   // (02/11 i) amplificador por huesos: ganancia por zona y escritura de los huesos que el juego mueve
        const float* E = g_cfg.live[340]; const float* Z = g_cfg.live[341];
        S->ampOn = E[1] > 1.005f || E[1] < 0.995f;
        memcpy(S->ahc, hc.t, sizeof S->ahc); memcpy(S->ahr, hr, sizeof S->ahr); S->ahFrame = s_frame.load(std::memory_order_relaxed);
        S->aCap = std::clamp(E[2], 0.1f, 3.0f) * 0.01f; S->aBig = std::clamp(E[3], 0.0f, 1.0f);
        for (int d = 0; d < kND; d++) {
            const float z = d <= 12 ? Z[3] : d <= 19 ? Z[2] : d <= 25 ? Z[1] : d <= 30 ? Z[0] : 0.0f;
            S->aGain[d] = 1.0f + (E[1] - 1.0f) * std::clamp(z, 0.0f, 3.0f);
        }
        for (int d = 0; d < D_EYE_L; d++) {
            if (!S->cur[d]) continue;
            Xf g; if (!ReadBase(*S, d, g)) continue;
            if (S->fresh[d] || !S->bHave[d]) { memcpy(S->bHc[d], hc.t, sizeof S->bHc[d]); memcpy(S->bHr[d], hr, sizeof S->bHr[d]); S->bHave[d] = true; }
            if (S->fresh[d]) Neutral(*S, d, g);   // (02/11 p) la cara neutra se aprende siempre (la usa tambien la sonrisa)
            if (!S->ampOn) continue;
            // (04/11, Agustin: "revisa bien como los huesos de parpados se seleccionan, vibran un poco") los 4 parpados (22..25,
            // por hash) NO se agrandan por huesos: el juego los vuelve a corregir con la mirada DESPUES del Eval del cuello, y el
            // gesto medido salta entre lo del juego y lo corregido (vibraban con la zona "ojos" > 0). Los parpados los lleva el
            // shader (PesBlinkApply sigue la mirada) o EyesFrame con "Por huesos + parpados"
            if (d >= D_LID_TL && d <= D_LID_BR) { s_lidAmpSkip.fetch_add(1, std::memory_order_relaxed); continue; }
            Xf x = g; Amp(*S, d, x);
            const float dt = fabsf(x.t[0] - g.t[0]) + fabsf(x.t[1] - g.t[1]) + fabsf(x.t[2] - g.t[2]);
            const float dq = fabsf(x.q[0] - g.q[0]) + fabsf(x.q[1] - g.q[1]) + fabsf(x.q[2] - g.q[2]) + fabsf(x.q[3] - g.q[3]);
            if (dt < 2e-5f && dq < 2e-5f) { Release(*S, d); continue; }   // el juego no lo mueve: nada que agrandar
            if (WriteBone(*S, d, g, x.q, x.t)) s_ampW.fetch_add(1, std::memory_order_relaxed); else s_badWrite.fetch_add(1, std::memory_order_relaxed);
        }
    }
    const float sm = Smile(*S, hc, hrInv);
    const float openAll = std::clamp((std::max)(o, 0.0f) + SStep(4.0f, 20.0f, deg), 0.0f, 1.0f);   // boca abierta del mod o del juego (grados)
    // (02/11 f, Agustin con video 13:06: "los parpados" siguen vibrando con 6969fd2) los parpados por huesos se pelean con el
    // control de la mirada del juego (que tambien reescribe los parpados despues del Eval, como hacia con skf_eye): vuelven al
    // shader (PesBlinkApply, que ya sigue la mirada vertical), que andaba sin vibrar. EyesFrame queda para cuando se sepa
    // escribir despues del juego
    // (02/11 h, Agustin: "quiero que se usen los por huesos, pero que sea perfecto") 350.x = 2: parpados por HUESOS tambien (para
    // medir con la lectura de la paleta por malla, expr.cpp "parpados por malla", si el giro llega igual a todas las mallas)
    // (08/11, Agustin: "como aplicamos expresiones custom para cada jugador", "que tambien se muestren al expresarse en partido o en
    // animaciones", "gestos que se reproduzcan entre medio como el jugador respirando e hinchando las mejillas") GESTOS PROPIOS DEL
    // JUGADOR (gani.cpp: PES21FX\anim\face\<id>\ y todos\). Cuanto esta haciendo el juego cada gesto se MIDE en los huesos: lo que se
    // corrio cada hueso contra la cara neutra de este jugador (ejes de la cabeza) proyectado sobre lo que mueve el clip del juego de ese
    // gesto (gestos.txt), mas la mandibula. Con ese peso se suma la diferencia entre el clip propio y el del juego: la sonrisa del juego
    // se vuelve la sonrisa propia, en partido, festejos y escenas, sin pisar ningun clip. En el editor (cara quieta) cuentan los sliders
    // de gestos del mod con el mismo nombre. Los "entre_" se reproducen una vez cada 4..12 s, al azar, con la cara quieta.
    if (!ganiOn && S->pKnown) {
        if (S->ggPid != S->pid) { S->ggPid = S->pid; S->ggGen = 0; S->gg.clear(); S->btw = -1; S->btwNext = -1.0f; S->ggLog = 0; for (float& w : S->ggW) w = 0.0f; }
        if (GaniGestGet(S->pid, S->ggGen, S->gg)) { S->btw = -1; for (float& w : S->ggW) w = 0.0f; }
        const int NG = (std::min)((int)S->gg.size(), 16);
        if (NG > 0) {
            const float t = AnimT();
            const float kf = S->ggT < 0.0f ? 1.0f : std::clamp((t - S->ggT) * 60.0f, 0.0f, 6.0f); S->ggT = t;
            const float ea = 1.0f - powf(1.0f - 0.12f, kf);
            float C[kND][3] = {}; bool cOk[kND] = {};
            for (int d = 5; d <= 30; d++) {
                if (d == 12 || (d >= 22 && d <= 25) || !S->cur[d] || S->nN[d] < 120) continue;
                Xf x; if (!ReadBase(*S, d, x)) continue;
                const float v[3] = { x.t[0] - hc.t[0], x.t[1] - hc.t[1], x.t[2] - hc.t[2] }; float r[3]; QRot(hrInv, v, r);
                for (int k = 0; k < 3; k++) C[d][k] = r[k] - S->nT[d][k];
                cOk[d] = true;
            }
            const float jawNow = deg / 57.29578f;
            float wr[16] = {}, sum = 0.0f;
            for (int i = 0; i < NG; i++) {
                const GaniGest& g = S->gg[i]; if (g.between || !g.hasRef) continue;
                float dot = 0.0f, rr = 0.0f;
                for (int d = 5; d <= 30; d++) {
                    if (!cOk[d]) continue; const int ab = s_defToAsk[d]; if (ab < 0) continue;
                    float rh[3]; GaniPToHead(g.rP[ab], rh);
                    for (int k = 0; k < 3; k++) { dot += C[d][k] * rh[k]; rr += rh[k] * rh[k]; }
                }
                const float rj = -g.rZ[1]; dot += 0.0004f * jawNow * rj; rr += 0.0004f * rj * rj;   // mandibula: 2 cm por radian
                if (rr < 1e-8f) continue;
                const float w = std::clamp((std::clamp(dot / rr, 0.0f, 1.0f) - 0.15f) / 0.70f, 0.0f, 1.0f);   // zona muerta: la cara quieta no dispara
                wr[i] = w; sum += w;
            }
            if (sum > 1.0f) for (int i = 0; i < NG; i++) wr[i] /= sum;
            for (int i = 0; i < NG; i++) {   // sliders de gestos del mod (editor): el gesto propio del mismo nombre
                const char* n = S->gg[i].name; float sl = 0.0f;
                if (!_stricmp(n, "sonrisa")) sl = (std::min)(pSmi, 1.0f); else if (!_stricmp(n, "risa")) sl = (std::max)(0.0f, pSmi - 1.0f) * 2.0f;
                else if (!_stricmp(n, "enojo")) sl = pAng; else if (!_stricmp(n, "asombro")) sl = pSur; else if (!_stricmp(n, "grito")) sl = pGri; else if (!_stricmp(n, "puchero")) sl = pPuc;
                wr[i] = (std::max)(wr[i], std::clamp(sl, 0.0f, 1.0f));
            }
            float total = 0.0f;
            for (int i = 0; i < NG; i++) {
                S->ggW[i] += (wr[i] - S->ggW[i]) * ea; total += S->ggW[i];
                if (S->ggW[i] > 0.5f && !(S->ggLog & (1u << i)) && s_logs < 400) { S->ggLog |= 1u << i; s_logs++; Log("gani: jugador %u hace '%s' (peso %.2f): gesto propio", S->pid, S->gg[i].name, S->ggW[i]); }
            }
            float P[31][3] = {}, Z[31] = {}; bool any = false;
            for (int i = 0; i < NG; i++) {
                const float w = S->ggW[i]; const GaniGest& g = S->gg[i]; if (g.between || w < 0.002f) continue;
                for (int b = 0; b < 31; b++) { for (int k = 0; k < 3; k++) P[b][k] += w * g.dP[b][k]; Z[b] += w * g.dZ[b]; }
                any = true;
            }
            {   // "entre_" / "quieto_" / "corriendo_": uno cada tanto con la cara quieta; "arranque_": enseguida al arrancar a correr
                auto rnd = [&](float n) { return Frac(sinf(n * 12.9898f + (float)(S->pid % 9973u) * 0.731f) * 43758.5453f); };
                auto ok = [&](int k) { return k == 1 || (k == 2 && S->spHave && S->stillT > 1.5f) || (k == 4 && S->spHave && S->speed > 3.5f); };
                int nb = 0; for (int i = 0; i < NG; i++) if (S->gg[i].between && ok(S->gg[i].kind)) nb++;
                if (arranco) {
                    int na = 0; for (int i = 0; i < NG; i++) if (S->gg[i].kind == 3) na++;
                    if (na > 0) {
                        int pick = (int)(rnd((float)S->btwN + 0.75f) * na); if (pick >= na) pick = na - 1;
                        for (int i = 0; i < NG; i++) if (S->gg[i].kind == 3 && pick-- == 0) { S->btw = i; break; }
                        S->btwT0 = t; S->btwN++;
                    }
                }
                if (S->btw >= 0 && S->btw < NG) {
                    const GaniGest& g = S->gg[S->btw]; const float tt = t - S->btwT0, dur = (std::max)(g.frames / 30.0f, 0.5f);
                    if (tt < 0.0f || tt > dur) { S->btw = -1; S->btwNext = t + 4.0f + 8.0f * rnd((float)S->btwN); }
                    else {
                        float BP[31][3], BZ[31];
                        if (GaniGestSample(S->pid, g.clip, tt, BP, BZ)) {
                            const float fade = SStep(0.0f, 0.3f, tt) * (1.0f - SStep(dur - 0.3f, dur, tt)) * (1.0f - std::clamp(total * 3.0f, 0.0f, 1.0f));
                            for (int b = 0; b < 31; b++) { for (int k = 0; k < 3; k++) P[b][k] += fade * BP[b][k]; Z[b] += fade * BZ[b]; }
                            any = true;
                        }
                    }
                } else if (nb > 0 && total < 0.08f && t >= 0.0f) {
                    if (S->btwNext < 0.0f || S->btwNext > t + 30.0f) S->btwNext = t + 3.0f + 6.0f * rnd(0.5f);
                    if (t >= S->btwNext) {
                        int pick = (int)(rnd((float)S->btwN + 0.25f) * nb); if (pick >= nb) pick = nb - 1;
                        for (int i = 0; i < NG; i++) if (S->gg[i].between && ok(S->gg[i].kind) && pick-- == 0) { S->btw = i; break; }
                        S->btwT0 = t; S->btwN++;
                    }
                }
            }
            if (any) {
                Z[1] = std::clamp(Z[1], -0.33f, 0.33f);   // la mandibula del gesto propio no pasa de 19 grados (la boca, regla fija)
                for (int b = 0; b < 31; b++) { GP.has[b] = true; memcpy(GP.p[b], P[b], sizeof P[b]); GP.q[b][0] = GP.q[b][1] = GP.q[b][2] = 0.0f; GP.q[b][3] = 1.0f; }
                GP.q[1][2] = sinf(0.5f * Z[1]); GP.q[1][3] = cosf(0.5f * Z[1]); GP.frames = 1;
                ganiOn = true; gW = 1.0f; poseOn = true;
                for (int d = 18; d <= 21; d++) { const int ab = s_defToAsk[d]; float ph[3] = {}; if (ab >= 0) GaniPToHead(P[ab], ph); for (int k = 0; k < 3; k++) s_ganiSec[d - 18][k] = ph[k]; }
                s_ganiSecOn = true;
                s_ganiLid[0] = -0.5f * (Z[14] + Z[15]); s_ganiLid[1] = -0.5f * (Z[16] + Z[17]); s_ganiLidOn = true;
            }
        }
    }
    if (g_cfg.live[350][0] > 1.5f) EyesFrame(*S, hc, hr, sm);
    else for (int d = D_LID_TL; d <= D_LID_BR; d++) Release(*S, d);
    // (08/11, Agustin: "en partidos tambien vibra cuando el jugador deberia estar expresandose") la sonrisa medida en la comisura y la
    // boca abierta medida en la mandibula cambian de un cuadro a otro (el juego anima la cara a 30 cuadros con claves ralas y la cabeza
    // se mueve): pomulos, surco y bajo-ojo las seguian crudas. Ahora pasan por un suavizado de ~6 cuadros de 60 Hz del RELOJ DEL JUEGO y
    // un margen de 0.02 (el temblor no pasa; los gestos de verdad si)
    float smS = sm, openS = openAll;
    {
        const float t = AnimT();
        const float k = S->secT < 0.0f ? 1.0f : std::clamp((t - S->secT) * 60.0f, 0.0f, 6.0f); S->secT = t;
        const float a = 1.0f - powf(1.0f - 0.16f, k);
        if (!S->secHave) { S->secSm = sm; S->secOpen = openAll; S->secHave = true; }
        auto step = [a](float prev, float now) { const float d = now - prev; return fabsf(d) < 0.02f ? prev + d * a * 0.5f : prev + d * a; };
        S->secSm = step(S->secSm, sm); S->secOpen = step(S->secOpen, openAll);
        smS = S->secSm; openS = S->secOpen;
    }
    Secondary(*S, hr, smS, openS);
    Neck(*S, 0.90f * pGri + 0.35f * pAng + 0.45f * (std::max)(xB[1], 0.0f) + (std::max)(0.0f, openS - 0.6f) + 0.40f * eff + 0.15f * nConc, openS);
    if (g_cfg.live[397][0] > 0.001f) poseOn = true;   // (08/11 e) x397 suaviza los gestos del juego aunque no haya gestos propios
    if (fabsf(o) < 0.001f && teeth < 0.001f && !poseOn) { for (int d = 0; d < 18; d++) if (S->wHave[d]) Release(*S, d); for (int d = 26; d <= 30; d++) if (S->wHave[d]) Release(*S, d); return; }   // slider en 0: la boca vuelve a la del juego
    static const float X[3] = { 1.0f, 0.0f, 0.0f }; float ax[3]; QRot(hr, X, ax);
    float ln = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]); if (ln < 0.5f) return; for (float& v : ax) v /= ln;
    const float P[3] = { jc.t[0], jc.t[1], jc.t[2] };
    float A = 0.40f * o * nExpr;
    // (08/11) respiracion por la boca corriendo y esfuerzo: la mandibula baja un poco (0.35 grados por inhalacion, hasta 3 grados de esfuerzo)
    A += 0.05f * eff + 0.0035f * brAmp * breath * (0.5f + 0.5f * SStep(1.5f, 6.0f, S->spHave ? S->speed : 0.0f));   // (09/11 b, video FC27 / eFootball) la mandibula afloja apenas al inhalar (0.2 grados quieto, 0.4 corriendo)
    if (ganiOn && GP.has[1]) A += -GaniAngZ(GP.q[1]) * gW;   // (07/11) mandibula del clip (giro sobre z del .ask = -x de la cabeza)
    // (03/11) tope de la mandibula: lo del juego + lo del mod no pasa de 35 grados (la barba y la cara no dan para mas)
    {   // (04/11 e) mordida: soltar abre la mandibula; apretar la cierra solo lo que el juego la tenga abierta (los dientes no se pasan)
        const float bite = std::clamp(g_cfg.live[361][1], -1.5f, 1.5f);
        if (bite < 0.0f) A += 0.045f * -bite;
        else if (bite > 0.0f) A -= (std::min)(0.045f * bite, (std::max)(0.0f, deg / 57.29578f + (std::max)(A, 0.0f)));
    }
    // (08/11, Agustin: "los gani del juego tambien?") SUAVIZADO DE LOS GESTOS DEL JUEGO (x397): los clips del juego los mezcla el
    // juego (claves a 30 cuadros, cambios de clip de golpe); la DLL no puede cambiar esa mezcla, pero si seguir la pose del juego
    // con un retraso corto (0.10 s x x397) en los ejes de la cabeza y sumar la diferencia como gesto del mod: los saltos entre
    // claves y entre clips se redondean sin que la cara se quede atras cuando la cabeza gira. Mandibula: el angulo del juego suavizado
    float gsK = 0.0f, gsDt = 1.0f / 60.0f; const float gsAmt = std::clamp(g_cfg.live[397][0], 0.0f, 3.0f);
    if (gsAmt > 0.001f) {
        const float dtg = S->gsT < 0.0f || tNow < S->gsT ? 1.0f : (std::min)(tNow - S->gsT, 0.25f); S->gsT = tNow;
        gsK = 1.0f - expf(-dtg / (0.10f * gsAmt));
        if (!S->gsDegHave) { S->gsDeg = deg; S->gsDegV = 0.0f; S->gsDegHave = true; } else Damp(S->gsDeg, S->gsDegV, deg, 0.10f * gsAmt, dtg);
        gsDt = dtg;
        A += (S->gsDeg - deg) / 57.29578f;
    } else { S->gsDegHave = false; for (bool& h : S->gsHave) h = false; }
    if (A > 0.0f) A = (std::min)(A, (std::max)(0.0f, (35.0f - deg) / 57.29578f));    // (02/11, Agustin: "esta al reves") con el signo negativo la boca se cerraba al subir el slider: en el cuaternion del motor el menton baja con el angulo positivo
    for (int d = 0; d < kND; d++) {
        if (!S->cur[d]) continue;
        const float* tv = TeethOf(d); const float* ov = OpenOf(d);
        const float op = (std::max)(o - (d >= 5 && d <= 8 ? oSmi : 0.0f), 0.0f);   // (09/11) comisuras: la risa no las junta ni las baja
        float po[3]; PoseOf(d, pAng, pSmi, pSur, po);
        for (const PoseOff2& p : kPose2) if (p.d == d) { for (int k = 0; k < 3; k++) po[k] += p.g[k] * pGri + p.p[k] * pPuc; break; }
        if (PF && ProfBoneOk(d)) {   // (04/11) perfil de esta cara: neutra (z356) y cada gesto con su slider
            const float w[PG_N] = { pNeu, pAng, pSmi, pSur, pGri, pPuc };
            for (int g = 0; g < PG_N; g++) if (PF->has[g] && w[g] > 0.001f) for (int k = 0; k < 3; k++) po[k] += PF->off[g][d][k] * w[g];
        }
        // (04/11) la mandibula real no gira en el lugar: al abrir, el condilo se desliza adelante y abajo (articulacion
        // temporomandibular): los huesos que siguen a la mandibula bajan y van adelante un poco con la apertura del mod
        if (kDef[d].jaw > 0.0f && op > 0.0f) { po[1] -= 0.0010f * op * kDef[d].jaw; po[2] += 0.0022f * op * kDef[d].jaw; }
        if (d == 5 || d == 6) po[0] += (d == 5 ? -1.0f : 1.0f) * 0.0016f * talkRound;   // hablar: comisuras adentro (redonda) / afuera (ancha)
        if (xOn) {   // (04/11 d) filas 361 / 362 (ejes de la cabeza: x a la izquierda del jugador, y arriba, z adelante; metros por 1.0)
            const bool isL = d <= 11 ? (d % 2) == 1 : (d % 2) == 0;   // izquierdos: lip_b_l 1, lip_s_l 5, cheek_s_l 7, lip_t_l 9, nosewing_l 14, nasolabial_l 16, ..., brow_i_l 26, brow_o_l 28
            const float sl = isL ? 1.0f : -1.0f;
            if (d >= 26 && d <= 29) po[1] += 0.0040f * xB[0];                       // cejas
            if (d == 30) po[1] += 0.0020f * xB[0];                                  // entrecejo
            if (d == 26 || d == 27) { po[1] += 0.0035f * xAng; po[0] -= sl * 0.0006f * xAng; }   // angulo: inicio de la ceja (y un poco al medio)
            if (d == 28 || d == 29) po[1] -= 0.0012f * xAng;                      // angulo: la punta de afuera
            if (d == 30) po[1] += 0.0015f * xAng;                                  // angulo: el entrecejo acompana el inicio
            if (kDef[d].jaw > 0.0f) po[0] += 0.0030f * xB[2] * kDef[d].jaw;   // mandibula de costado
            if (xB[1] > 0.0f) {   // apretar los dientes: maseteros afuera, labios apretados uno contra otro, menton que sube, comisuras tensas
                const float k = xB[1];
                if (d == 7 || d == 8) po[0] += sl * 0.0012f * k;
                if (d == 1 || d == 2 || d == 3) po[1] += 0.0006f * k;
                if (d == 9 || d == 10 || d == 11) po[1] -= 0.0005f * k;
                if (d == 4) { po[1] += 0.0008f * k; po[2] += 0.0005f * k; }
                if (d == 5 || d == 6) po[0] += sl * 0.0008f * k;
            }
            if (d == 5 || d == 6) {                                                 // media sonrisa
                const float side = d == 5 ? (std::max)(xB[3], 0.0f) : (std::max)(-xB[3], 0.0f);
                po[0] += sl * 0.0025f * side; po[1] += 0.0035f * side; po[2] -= 0.0010f * side;
            }
            if ((d == 7 || d == 8) && xB[4] != 0.0f) { po[0] += sl * 0.0030f * xB[4]; po[2] += 0.0010f * xB[4]; }   // cachetes
            if ((d == 16 || d == 17) && xB[4] != 0.0f) po[0] += sl * 0.0015f * xB[4];
            if (d == 5 || d == 6) { po[0] -= sl * 0.0040f * xB[5]; po[2] += 0.0020f * xB[5]; }                    // beso
            if (d == 1 || d == 2 || d == 3 || d == 9 || d == 10 || d == 11) po[2] += 0.0025f * xB[5];
            {   // desprecio: labio de arriba, ala de la nariz y comisura de un lado
                const float side = isL ? (std::max)(xB[6], 0.0f) : (std::max)(-xB[6], 0.0f);
                if (d == 9 || d == 11) po[1] += 0.0030f * side;
                if (d == 14 || d == 15) po[1] += 0.0015f * side;
                if (d == 5 || d == 6) po[1] += 0.0010f * side;
            }
            if (d >= 26 && d <= 29) { const float side = isL ? (std::max)(xB[7], 0.0f) : (std::max)(-xB[7], 0.0f); po[1] += 0.0045f * side; }   // una ceja
        }
        if (d == 9 || d == 10 || d == 11) po[1] += 0.0012f * talkLip;   // (08/11) hablar: labio de arriba sube
        if (d == 1 || d == 2 || d == 3) po[1] -= 0.0012f * talkLow;      // y el de abajo baja (f / v: sube contra los dientes)
        if (d == 12) po[1] += 0.0004f * talkLip;
        if (talkProt > 0.001f || talkPress > 0.001f || talkAcc > 0.001f) {   // (08/11) hablar por palabras
            if (d == 1 || d == 2 || d == 3 || d == 5 || d == 6 || d == 9 || d == 10 || d == 11 || d == 12) po[2] += 0.0022f * talkProt;   // o / u: labios adelante
            if (d == 9 || d == 10 || d == 11) po[1] -= 0.0007f * talkPress;   // m / b / p: labios uno contra otro
            if (d == 1 || d == 2 || d == 3) po[1] += 0.0007f * talkPress;
            if (d == 5 || d == 6) po[0] -= (d == 5 ? 1.0f : -1.0f) * 0.0004f * talkPress;
            if (d >= 26 && d <= 29) po[1] += 0.0009f * talkAcc;              // silaba tonica: cejas apenas arriba
        }
        if (nLive) {   // (08/11) animaciones realistas (filas 395 / 396): ejes de la cabeza, metros
            const bool isL = d <= 11 ? (d % 2) == 1 : (d % 2) == 0; const float sl = isL ? 1.0f : -1.0f;
            const float spd = S->spHave ? S->speed : 0.0f, mouthBr = brAmp * breath * SStep(1.5f, 6.0f, spd);
            // vida
            if (d >= 26 && d <= 29) po[1] += 0.0005f * lifeBrow;
            if (d == 30) po[1] += 0.0003f * lifeBrow;
            if (d == 5 || d == 6) { po[0] += sl * 0.00025f * lifeLipX; po[1] += 0.0002f * lifeLipY; }
            if (d == 14 || d == 15) po[0] += sl * 0.0002f * lifeNose;
            // respiracion
            if (d == 14 || d == 15) { po[0] += sl * 0.0008f * brAmp * breath; po[1] += 0.0002f * brAmp * breath; }   // alas: se abren y suben apenas al inhalar
            if (d == 13) po[2] += 0.0002f * brAmp * breath;
            // (09/11 b, Agustin con video FC27 / eFootball: "mira las animaciones de respiracion en los dos juegos modernos") como en esos juegos:
            // los labios se entreabren apenas al inhalar (0.3 mm arriba / 0.5 mm abajo quieto, el doble corriendo), las mejillas no
            { const float lipBr = brAmp * breath * (0.5f + 0.5f * SStep(1.5f, 6.0f, spd)); (void)mouthBr;
              if (d == 9 || d == 10 || d == 11) po[1] += 0.0003f * lipBr;
              if (d == 1 || d == 2 || d == 3) po[1] -= 0.0005f * lipBr; }
            // esfuerzo
            if (d == 26 || d == 27) { po[1] -= 0.0025f * eff; po[0] -= sl * 0.0008f * eff; }
            if (d == 28 || d == 29) po[1] -= 0.0010f * eff;
            if (d == 30) po[1] -= 0.0015f * eff;
            if (d == 14 || d == 15) po[0] += sl * 0.0008f * eff;
            if (d == 5 || d == 6) { po[0] += sl * 0.0012f * eff; po[1] -= 0.0008f * eff; }
            if (d == 7 || d == 8) po[0] += sl * 0.0008f * eff;
            if (d == 4) po[1] -= 0.0006f * eff;
            // concentracion
            if (d == 26 || d == 27) { po[1] -= 0.0020f * nConc; po[0] -= sl * 0.0008f * nConc; }
            if (d == 28 || d == 29) po[1] -= 0.0006f * nConc;
            if (d == 30) { po[1] -= 0.0012f * nConc; po[2] += 0.0004f * nConc; }
            if (d == 1 || d == 2 || d == 3) po[1] += 0.0005f * nConc;
            if (d == 9 || d == 10 || d == 11) po[1] -= 0.0004f * nConc;
            if (d == 5 || d == 6) po[0] -= sl * 0.0006f * nConc;
            // asimetria: izquierda mas fuerte, derecha mas floja (solo huesos con par), y la ceja izquierda apenas mas alta
            if (nAsim > 0.001f && kDef[d].x != 0.0f) { const float k = 1.0f + sl * 0.15f * nAsim; for (float& v : po) v *= k; if (d == 26 || d == 28) po[1] += 0.0004f * nAsim; }
        }
        if (nExpr != 1.0f) for (float& v : po) v *= nExpr;   // z396 expresividad (los gestos propios .gani no se escalan)
        // (07/11) clip .gani: corrimiento (cm -> m, ejes de la cabeza) y giro local del hueso, por el peso; pomulos / bajo-ojo (18..21) los
        // lleva Secondary y los parpados (22..25) EyesFrame; la mandibula ya entro en A
        float gq[4] = { 0, 0, 0, 1 }; bool grot = false;
        if (ganiOn && d != D_JAW && (d < 18 || (d >= 26 && d <= 30))) { const int ab = s_defToAsk[d]; if (ab >= 0 && GP.has[ab]) {
            float ph[3]; GaniPToHead(GP.p[ab], ph); for (int k = 0; k < 3; k++) po[k] += ph[k] * gW;
            float qh[4], qs[4]; GaniQToHead(GP.q[ab], qh); QScale(qh, gW, qs);
            if (qs[3] < 0.999999f) { float r1[4]; QMul(hr, qs, r1); QMul(r1, hrInv, gq); grot = true; }
        } }
        Xf x; bool xOk = false;
        if (gsK > 0.0f && d != D_JAW && (d < 18 || (d >= 26 && d <= 30))) {   // (08/11) x397: la posicion del juego de este hueso, suavizada
            xOk = Base(*S, d, x);
            if (xOk) {
                const float v[3] = { x.t[0] - hc.t[0], x.t[1] - hc.t[1], x.t[2] - hc.t[2] }; float L[3]; QRot(hrInv, v, L);
                if (!S->gsHave[d]) { memcpy(S->gsL[d], L, sizeof L); memset(S->gsV[d], 0, sizeof S->gsV[d]); S->gsHave[d] = true; }
                else for (int k = 0; k < 3; k++) { Damp(S->gsL[d][k], S->gsV[d][k], L[k], 0.10f * gsAmt, gsDt); const float e = S->gsL[d][k] - L[k]; if (fabsf(e) < 0.02f) po[k] += e; else { S->gsL[d][k] = L[k]; S->gsV[d][k] = 0.0f; } }   // > 2 cm = cambio de cara / jugador: se reengancha
            }
        }
        const float mvv0[3] = { tv[0] * teeth + ov[0] * op + po[0], tv[1] * teeth + ov[1] * op + po[1], tv[2] * teeth + ov[2] * op + po[2] };
        const bool rot = kDef[d].jaw > 0.0f && fabsf(A) > 1e-5f, mv = fabsf(mvv0[0]) + fabsf(mvv0[1]) + fabsf(mvv0[2]) > 1e-6f;
        if (!rot && !mv && !grot) { if ((d < 18 || (d >= 26 && d <= 30)) && S->wHave[d]) Release(*S, d); continue; }   // 18.. = gestos secundarios / parpados (otro dueno)
        if (!xOk && !Base(*S, d, x)) continue;
        float nq[4] = { x.q[0], x.q[1], x.q[2], x.q[3] }, nt[3] = { x.t[0], x.t[1], x.t[2] };
        if (rot) {
            const float Ad = d >= 5 && d <= 8 ? A - 0.40f * oSmi * nExpr : A;   // (09/11) comisuras y mejillas de abajo: sin la apertura de la risa
            const float h = 0.5f * Ad * kDef[d].jaw, sn = sinf(h);
            const float qr[4] = { ax[0] * sn, ax[1] * sn, ax[2] * sn, cosf(h) };
            float v[3] = { x.t[0] - P[0], x.t[1] - P[1], x.t[2] - P[2] }, rv[3];
            QRot(qr, v, rv); QMul(qr, x.q, nq);
            nt[0] = P[0] + rv[0]; nt[1] = P[1] + rv[1]; nt[2] = P[2] + rv[2];
        }
        if (grot) { float q2[4]; QMul(gq, nq, q2); memcpy(nq, q2, sizeof q2); }   // (07/11) giro del clip, en el lugar
        if (mv) {   // corrimiento en los ejes de la cabeza (x a la izquierda del jugador, y arriba, z adelante) -> modelo
            const float hv[3] = { mvv0[0], mvv0[1], mvv0[2] }; float mvv[3];
            QRot(hr, hv, mvv);
            nt[0] += mvv[0]; nt[1] += mvv[1]; nt[2] += mvv[2];
        }
        if (!WriteBone(*S, d, x, nq, nt)) { s_badWrite.fetch_add(1, std::memory_order_relaxed); continue; }
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

// (08/11) GESTOS EN LA PALETA: al final del cuadro de este personaje se publica, por hueso de la cara, lo que dejo el juego (base), lo
// que el mod quiere (nuevo: lo anotado por WriteBone este cuadro; si no, igual a la base) y el reposo, mas la cabeza y la matriz del nodo.
// expr.cpp lo lee en el Present (ExprBonesPoses) y lo sube al VS. Si las expresiones estan apagadas no hay nada anotado: diferencia 0.
static void Publish(Skel& S, unsigned fr) {
    ExprCharPose& P = S.pub; P = ExprCharPose();
    if (!g_cfg.optExprPalette || !S.cur[D_HEAD]) return;
    Xf hc; if (!ReadXf(S.cur[D_HEAD], hc)) return;
    P.frame = fr; P.pid = S.pKnown ? S.pid : 0; P.skel = S.skel;
    memcpy(P.headT, hc.t, sizeof P.headT); memcpy(P.headQ, hc.q, sizeof P.headQ); memcpy(P.headRestT, S.rest[D_HEAD].t, sizeof P.headRestT); memcpy(P.headRestQ, S.rest[D_HEAD].q, sizeof P.headRestQ);
    P.worldOk = S.node && CharNodeMatrix(const_cast<void*>(S.node), P.W);
    float hr[4]; { float c[4]; QConj(S.rest[D_HEAD].q, c); QMul(hc.q, c, hr); }   // (08/11 g) cabeza actual * reposo^-1 (como Neutral)
    for (int d = 0; d < kExprBones && d < kND; d++) {
        if (!S.cur[d]) continue;
        ExprCharPose::Bone& b = P.b[d];
        // (08/11 g, Agustin: control total sobre "toda la expresion (juego + mod)") la cara NEUTRA de este jugador (promedio lento, Neutral())
        // llevada a la cabeza de este cuadro: expr.cpp escala la expresion entera (lo del juego + lo del mod) respecto de ella
        if (S.nN[d] >= 120) { float v[3]; QRot(hr, S.nT[d], v); for (int k = 0; k < 3; k++) b.zt[k] = hc.t[k] + v[k]; QMul(hr, S.nQ[d], b.zq); b.hasZ = true; }
        const bool mv = S.dHave[d] && S.dFrame[d] == fr;
        if (mv) { memcpy(b.bq, S.bq[d], sizeof b.bq); memcpy(b.bt, S.bt[d], sizeof b.bt); memcpy(b.nq, S.dq[d], sizeof b.nq); memcpy(b.nt, S.dt[d], sizeof b.nt); }
        else { Xf x; if (!ReadXf(S.cur[d], x)) continue; memcpy(b.bq, x.q, sizeof b.bq); memcpy(b.bt, x.t, sizeof b.bt); memcpy(b.nq, x.q, sizeof b.nq); memcpy(b.nt, x.t, sizeof b.nt); }
        memcpy(b.rq, S.rest[d].q, sizeof b.rq); memcpy(b.rt, S.rest[d].t, sizeof b.rt);
        b.has = true; b.moved = mv;
    }
    P.ok = true;
}
void ExprBonesFrame(const void* skel) {
    FrameBody(skel);
    if (!skel || !g_cfg.optExprPalette) return;
    std::lock_guard<std::mutex> lk(s_mx);
    for (int i = 0; i < s_nSk; i++) if (s_sk[i].skel == skel) { if (s_sk[i].jawFound) Publish(s_sk[i], s_frame.load(std::memory_order_relaxed)); else s_sk[i].pub.ok = false; break; }
}
int ExprBonesPoses(ExprCharPose* out, int max) {
    std::lock_guard<std::mutex> lk(s_mx);
    const unsigned fr = s_frame.load(std::memory_order_relaxed); int n = 0;
    for (int i = 0; i < s_nSk && n < max; i++) { const ExprCharPose& P = s_sk[i].pub; if (P.ok && fr - P.frame <= 10) out[n++] = P; }
    // (08/11 c, Agustin: un ojo hundido en cuadros alternos al acercar la camara) dos esqueletos publicados con la cabeza en el mismo
    // lugar (el editor arma otra instancia del mismo jugador, o una vieja que todavia no vencio): el VS elegia uno u otro segun el cuadro y
    // las diferencias no eran las mismas. Si dos cabezas estan a menos de 3 cm queda solo la publicada mas nueva
    auto headW = [](const ExprCharPose& P, float* o) { if (!P.worldOk) return false; for (int k = 0; k < 3; k++) o[k] = P.W[k * 4 + 0] * P.headT[0] + P.W[k * 4 + 1] * P.headT[1] + P.W[k * 4 + 2] * P.headT[2] + P.W[k * 4 + 3]; return true; };
    for (int i = 0; i < n; i++) {
        float a[3]; if (!headW(out[i], a)) continue;
        for (int j = i + 1; j < n; j++) {
            float b[3]; if (!headW(out[j], b)) continue;
            const float d2 = (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
            if (d2 > 0.03f * 0.03f) continue;
            if ((int)(out[j].frame - out[i].frame) > 0) out[i] = out[j];   // queda la mas nueva en i
            out[j] = out[--n]; j--; headW(out[i], a);
        }
    }
    return n;
}
// Present (hilo de render): estado para el shader (350.y) y resumen al log
void ExprBonesPresent() {
    GaniPresent();   // (07/11) clips .gani de la cara
    const unsigned fr = s_frame.fetch_add(1) + 1;
    const bool active = fr - s_lastActive.load(std::memory_order_relaxed) < 120;
    const bool eyesOn = fr - s_lastEyes.load(std::memory_order_relaxed) < 120;
    g_cfg.live[350][1] = active ? (eyesOn ? 2.0f : 1.0f) : 0.0f;   // 2 = tambien ojos y parpados por huesos (el shader no los gira)
    static unsigned lastLog = 0;
    if (fr - lastLog < 600) return;   // cada ~10 s
    lastLog = fr;
    std::lock_guard<std::mutex> lk(s_mx);
    const int calls = s_calls.exchange(0), ap = s_applied.exchange(0), nj = s_noJaw.exchange(0), bw = s_badWrite.exchange(0), st = s_stale.exchange(0), ew = s_eyesW.exchange(0), bl = s_blinks.exchange(0), aw = s_ampW.exchange(0), ask = s_ampSkip.exchange(0), lsk = s_lidAmpSkip.exchange(0), lhf = s_lidHeadFix.exchange(0), lhft = s_lidHeadFixT.exchange(0), bhf = s_baseHeadFix.exchange(0), nkw = s_neckW.exchange(0), farN = s_far.exchange(0);
    if (!calls) return;
    int reusedAll = 0; for (int i = 0; i < s_nSk; i++) { reusedAll += s_sk[i].reused; s_sk[i].reused = 0; }
    static int dumped = 0;
    if (dumped < 0) for (int i = 0; i < s_nSk && dumped < 2; i++) if (s_sk[i].n > 0 && fr - s_sk[i].lastFrame < 600) { Dump(s_sk[i]); dumped++; }
    if (s_logs < 40) {
        char l[1200]; int k = snprintf(l, sizeof l, "boca por huesos [vivo x340 %.2f y340 %.2f x347 %.2f x350 %.0f]: %d pasos por el cuello, %d huesos agrandados (amplificador por huesos; %d sin agrandar por fuera de rango), %d giros escritos, %d sin mandibula, %d escrituras fallidas, %d rearmados por cambio de cara; ojos por huesos %d (cuadros con parpado cerrado %d); huesos leidos con el giro del mod todavia puesto %d; esqueletos:", g_cfg.live[340][0], g_cfg.live[340][1], g_cfg.live[347][0], g_cfg.live[350][0], calls, aw, ask, ap, nj, bw, st, ew, bl, reusedAll);
        k += snprintf(l + k, sizeof l - k, " [parpados sin agrandar %d, llevados a la cabeza %d giro / %d posicion]", lsk, lhf, lhft);
        k += snprintf(l + k, sizeof l - k, " [otros huesos llevados a la cabeza %d]", bhf);
        k += snprintf(l + k, sizeof l - k, " [cuello %d]", nkw);
        if (farN) k += snprintf(l + k, sizeof l - k, " [lejos de la camara sin escribir %d]", farN);
        for (int i = 0; i < s_nSk && k < (int)sizeof l - 120; i++) {
            Skel& S = s_sk[i];
            if (fr - S.lastFrame > 600) continue;
            if (S.angN) k += snprintf(l + k, sizeof l - k, " %p (cara %p %d huesos, %d reconocidos, mandibula %s, gira %.1f..%.1f grados)", S.skel, S.face, S.fn, S.found, S.jawFound ? "si" : "NO", S.angMin, S.angMax);
            else k += snprintf(l + k, sizeof l - k, " %p (cara %p %d huesos, %d reconocidos, mandibula %s)", S.skel, S.face, S.fn, S.found, S.jawFound ? "si" : "NO");
            if (S.eyePHave) k += snprintf(l + k, sizeof l - k, " [mirada del juego %.1f grados]", S.eyeP * 57.2958f);
            S.angMin = 1e9f; S.angMax = -1e9f; S.angN = 0;
            if (!S.logged && S.found) {   // una vez por esqueleto: que hueso se reconocio (indice en el arreglo)
                S.logged = true;
                char m[1500]; int j = snprintf(m, sizeof m, "boca por huesos: esqueleto %p huesos reconocidos:", S.skel);
                for (int d = 0; d < kND && j < (int)sizeof m - 40; d++) if (S.cur[d]) j += snprintf(m + j, sizeof m - j, " %s=%d%s", kDef[d].name, S.idx[d], S.src[d] == S.fbones ? "" : "(cuerpo)");   // (04/11) (cuerpo) = salio del esqueleto del cuerpo, no del de la cara
                Log("%s", m);
                if (S.face) {   // todos los nombres del esqueleto de la cara (una vez): para la pose facial propia
                    // (02/11) para separar el globo del ojo del parpado (mismo pivote): por hueso indice / padre / reposo (traslacion y
                    // cuaternion) / hash (+ 0x10), en varias lineas
                    j = snprintf(m, sizeof m, "boca por huesos: esqueleto de la cara %p (%d huesos), hueso: padre t(x y z) q(x y z w) hash:", S.face, S.fn);
                    for (int b = 0; b < S.fn; b++) {
                        Xf r; const char* bone = S.fbones + (size_t)b * 0xc0; uint32_t hs = 0; SafeCopy(&hs, bone + 0x10, 4);
                        void* par = nullptr; int pi = -1;
                        if (ReadPtr(bone + 0x20, par)) { ptrdiff_t dd = (const char*)par - S.fbones; if (dd >= 0 && dd % 0xc0 == 0 && dd / 0xc0 < S.fn) pi = (int)(dd / 0xc0); else pi = -2; }
                        if (!ReadXf(bone + 0x40, r)) memset(&r, 0, sizeof r);
                        if (j > (int)sizeof m - 110) { Log("%s", m); j = snprintf(m, sizeof m, "boca por huesos: (cara, sigue)"); }
                        j += snprintf(m + j, sizeof m - j, " %d:%d t(%.4f %.4f %.4f) q(%.3f %.3f %.3f %.3f) %08X", b, pi, r.t[0], r.t[1], r.t[2], r.q[0], r.q[1], r.q[2], r.q[3], hs);
                    }
                    Log("%s", m);
                }
            }
        }
        Log("%s", l); s_logs++;
    }
}

}   // namespace fx
