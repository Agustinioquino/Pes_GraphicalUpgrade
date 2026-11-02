// PES21 FX (31/10) - expresiones faciales mas marcadas, en el VS de la cabeza (faab87ee) y el de la cara animada (455eb97b).
// Incluir DESPUES de los cbuffer VSBones (g_vsBone / g_vsPrevBone) e IniParams.
//
// Cada vertice de la cara se mueve con sk_head (la cabeza rigida) y con huesos faciales (skf_*). Lo que un hueso facial le
// agrega al vertice respecto de la cabeza es  d = M_hueso * p - M_cabeza * p  (el gesto). El mod suma (ganancia - 1) * d,
// pesado como en el skinning: con ganancia 1 queda exacto como el juego, con 0 la cara queda quieta, con 1.5 el gesto es un
// 50 % mas grande. En partido (sin animacion facial) d = 0 y no cambia nada.
// - Donde esta sk_head en la paleta lo dice la DLL por malla (expr.cpp, cbuffer b12: x = cabeza, 99 = sin dato -> nada).
// - El cuello y el pecho no son gestos: se saltean el hueso de antes de la cabeza (sk_neck) y, si la cabeza esta en 1 o 2, todos
//   los de antes (sk_chest); los ayudantes del cuello (dsk_*) quedan fuera por la mascara de la cara (frente de la cabeza).
// - Gestos grandes (mandibula abierta, > 1.2 cm) se agrandan menos (340.w); el agregado tiene tope (340.z cm).
// - Por zona (341): cuanto del agregado va a frente / cejas, ojos / parpados (0 por defecto: pestanas y parpados contra el ojo),
//   mejillas / nariz y boca / labios. Las zonas salen de la posicion de reposo (metros, espacio del modelo: ojos en y 1.697).
// - Igual en la posicion del cuadro anterior (vectores de movimiento del TAA) y en normal / tangente.
#ifndef PES_EXPR_HLSLI
#define PES_EXPR_HLSLI

cbuffer PESExprMesh : register(b12)
{
  uint4 g_pesExprMesh;   // x cabeza en la paleta (99 = nada), y hueso maximo usado, z id de la malla
  uint4 g_pesExprLids;   // (01/11) parpados en la paleta: x arriba izq (+x), y arriba der, z abajo izq, w abajo der (99 = no hay)
  uint4 g_pesExprJaw;    // (01/11) abrir la boca: x mascara de huesos de abajo (mandibula, labio de abajo, papada), y comisuras (la mitad), z labio de arriba del medio (99 = no hay), w mascara del labio de arriba
  // (01/11 h) ojos (VS 69758ab9): centro (xyz) y radio del globo izquierdo (+x, hueso 0 del eye.fmdl) y derecho (hueso 1); radio 0 = no hay
  float4 g_pesExprEye[2];
  uint4 g_pesExprMus;    // (01/11 i) musculos virtuales: x comisura izq (+x), y comisura der, z ceja interna izq, w ceja interna der (99 = no hay)
}

float PesExprZoneShare(float3 r, float4 Z)
{
  float ex = abs(r.x) - 0.028, ey = (r.y - 1.697) * 1.4;
  float we = 1.0 - smoothstep(0.012, 0.022, sqrt(ex * ex + ey * ey));     // ojos y parpados
  float wb = smoothstep(1.700, 1.714, r.y) * (1.0 - we);                 // frente y cejas
  float wm = (1.0 - smoothstep(1.646, 1.664, r.y)) * (1.0 - we);         // boca, labios, menton
  float wc = saturate(1.0 - we - wb - wm);                               // mejillas, nariz, surcos
  return wb * Z.x + we * Z.y + wc * Z.z + wm * Z.w;
}

bool PesExprSkip(uint j, uint head)
{
  if (j == head) return true;
  if (head <= 2) return j < head;          // face_high: sk_chest / sk_neck antes de la cabeza
  return j + 1 == head;                    // cara generica (cabeza al final): sk_neck justo antes
}

// p = posicion de reposo usada en el skinning (w = 1), rest = reposo para las zonas; I / W = huesos y pesos del vertice.
// Suma a pos / prev / nrm / tan (salidas del skinning del juego, espacio del objeto).
void PesExprApply(float4 p, float3 rest, uint4 I, float4 W, float3 n, float3 t,
                  inout float3 pos, inout float3 prev, inout float3 nrm, inout float3 tng)
{
  float4 E = IniParams.Load(int2(340, 0));
  if (E.x < 0.5) return;
  uint head = g_pesExprMesh.x;
  if (head > 31) return;
  float4 Z = IniParams.Load(int2(341, 0));
  // mascara: de la base del menton para arriba y no la nuca (01/11: antes cortaba en z 0.06..0.095 y dejaba fuera el fondo
  // de la boca / dientes de la boca 3D: labios agrandados y dientes no -> union rara)
  float face = smoothstep(1.580, 1.610, rest.y) * smoothstep(0.000, 0.035, rest.z);
  float extra = (E.y - 1.0) * PesExprZoneShare(rest, Z) * face;
  if (abs(extra) < 0.001) return;
  float bigW = saturate(E.w);
  float cap = max(E.z, 0.0) * 0.01;

  float4x3 Mh = g_vsBone.m_boneMatrices[head];
  float4x3 Ph = g_vsPrevBone.m_boneMatrices[head];
  float3 ph = mul(p, Mh), pph = mul(p, Ph);
  float3 nh = mul(n, (float3x3)Mh), th = mul(t, (float3x3)Mh);
  float3 dP = 0, dQ = 0, dN = 0, dT = 0;
  [unroll] for (int k = 0; k < 4; k++) {
    uint j = I[k]; float w = W[k];
    if (w <= 0.0 || j > 31 || PesExprSkip(j, head)) continue;
    float4x3 M = g_vsBone.m_boneMatrices[j];
    float3 d = mul(p, M) - ph;
    float s = w * lerp(1.0, bigW, smoothstep(0.012, 0.040, length(d)));
    dP += s * d;
    dQ += s * (mul(p, g_vsPrevBone.m_boneMatrices[j]) - pph);
    dN += s * (mul(n, (float3x3)M) - nh);
    dT += s * (mul(t, (float3x3)M) - th);
  }
  float3 a = extra * dP, b = extra * dQ;
  float la = length(a), lb = length(b);
  if (cap > 0.0) { if (la > cap) a *= cap / la; if (lb > cap) b *= cap / lb; }
  pos += a; prev += b;
  nrm += extra * dN; tng += extra * dT;   // el VS normaliza despues
}

// (01/11) lengua de la boca 3D (hilo de la boca, mouth.fmdl 7AA745CB: 677 vertices 100 % skf_jaw; reposo x -0.018..0.018,
// y 1.625..1.646, z 0.116..0.160; uv u 0.298..0.708 de lado a lado, v 0.671 raiz -> 0.92 punta, > 0.92 la cara de abajo de la
// punta). La DLL marca la malla de la boca (b12 w = 1). Se corre la posicion de REPOSO antes del skinning (sigue a la mandibula):
//  - con la boca abierta (J = cuanto se separa skf_jaw de la cabeza en la punta, 0..1) el lomo baja y se aplana y la punta va
//    un poco adelante y abajo, detras de los dientes de abajo (como al gritar);
//  - con la boca abierta, ademas, una onda chica de la raiz a la punta y un vaiven de lado (reloj del juego, fila 157: la pausa
//    la congela y la camara lenta la frena).
// Fila 342: x fuerza (0 = quieta, como el juego), y movimiento vivo (0..2), z velocidad (0.5..2).
float3 PesTongue(float3 rest, float2 uv, uint jaw)
{
  if (g_pesExprMesh.w != 1u) return 0;
  uint head = g_pesExprMesh.x;
  if (head > 31 || jaw > 31 || jaw == head) return 0;
  if (uv.x < 0.295 || uv.x > 0.715 || uv.y < 0.668 || uv.y > 0.99) return 0;
  if (abs(rest.x) > 0.022 || rest.y < 1.615 || rest.y > 1.652 || rest.z < 0.110 || rest.z > 0.165) return 0;
  float4 T = IniParams.Load(int2(342, 0));
  // (02/11, Agustin: "un slider para sacar la lengua de forma realista") 350.z: la lengua se estira hacia afuera desde la raiz
  // (que queda pegada a la garganta), pasa por encima de los dientes de abajo, se afina y la punta cae sobre el labio de
  // abajo. Sigue a la mandibula (la DLL abre la boca al menos 0.45 x 350.z cuando la boca va por huesos).
  float L = saturate(IniParams.Load(int2(350, 0)).z);
  if (T.x <= 0.0 && L <= 0.0) return 0;
  T.x = max(T.x, 0.0);
  // apertura: la punta de la lengua llevada por la mandibula contra llevada por la cabeza
  float4 tip = float4(0.0, 1.6274, 0.1596, 1.0);
  float J = saturate(length(mul(tip, g_vsBone.m_boneMatrices[jaw]) - mul(tip, g_vsBone.m_boneMatrices[head])) / 0.012);   // (01/11 b) se mueve ya con la boca a medio abrir
  J = saturate(J + max(IniParams.Load(int2(347, 0)).x, 0.0));   // (01/11 e) la boca abierta del slider tambien cuenta
  float s = saturate((uv.y - 0.678) / (0.920 - 0.678));          // 0 raiz .. 1 punta (y la cara de abajo de la punta)
  float lado = (uv.x - 0.503) / 0.205;                            // -1 .. 1 de lado a lado
  float3 o = 0;
  // forma con la boca abierta: lomo abajo (mas en el medio del largo), punta adelante y abajo
  // (01/11 b, Agustin: "que se mueva mas, como la mandibula") todo x2..3
  // (01/11 c) con 6.5 mm la lengua atravesaba el piso de la boca y se despegaba de la garganta: la raiz queda fija (pegada a
  // la garganta / piso, que no se mueven con esto) y el lomo baja hasta 3.5 mm; la punta no pasa los dientes de abajo
  float raiz = smoothstep(0.0, 0.30, s);
  o.y -= J * 0.0035 * sin(3.14159 * saturate(s * 0.9 + 0.05)) * raiz;
  o.y -= J * 0.0012 * s * s;
  o.z += J * 0.0012 * s * s;
  // vivo: onda de la raiz a la punta y vaiven de lado, solo con la boca abierta
  float t = IniParams.Load(int2(157, 0)).x * max(T.z, 0.1);
  // (01/11 d, Agustin: "la lengua baila mucho dentro de la boca") onda a ~40 % y mas lenta; solo con la boca bien abierta (J^2)
  float vivo = J * J * saturate(T.y * 0.5) * 2.0;
  o.y += vivo * 0.0008 * s * raiz * sin(6.2832 * 0.8 * t - 2.0 * s);
  o.z += vivo * 0.0003 * s * s * sin(6.2832 * 0.5 * t + 0.7);   // la punta va y viene
  o.x += vivo * 0.0005 * s * s * sin(6.2832 * 0.35 * t + 1.3);
  o.y += vivo * 0.0002 * lado * s * raiz * sin(6.2832 * 0.35 * t + 1.3);   // se inclina un poco hacia el lado al que va la punta
  o *= T.x * (1.0 - 0.7 * L);   // afuera, el lomo ya no se hunde
  if (L > 0.0) {
    float sx = pow(s, 1.3);
    float3 q = 0;
    q.z += L * 0.030 * sx;                                      // hasta 3 cm afuera en la punta
    q.y += L * 0.0018 * sin(3.14159 * s) * raiz;                // el medio se levanta para pasar sobre los dientes de abajo
    q.y -= L * 0.0060 * s * s;                                  // la punta cae sobre el labio de abajo
    // (02/11, Agustin: "deberia respetarse un poco mas su ancho") antes se afinaba 30 % en la punta: ahora apenas 8 %
    q.x -= rest.x * 0.08 * L * s * s;                           // se afina apenas en la punta
    q.y -= (rest.y - 1.636) * 0.15 * L * s;                     // y se aplana un poco (menos alto)
    q.x += L * 0.0012 * s * s * sin(6.2832 * 0.4 * t + 0.5) * saturate(T.y);   // la punta se mueve apenas de lado (movimiento vivo)
    o += q;
  }
  return o;
}

// (01/11 h, Agustin: "a los ojos les falta vida") MIRADA VIVA, fila 347.w (0 = como el juego, 1 = normal, 2 = inquieto).
// Un ojo real nunca esta quieto: fija la mirada 0.4..2 s y salta (sacada, ~40 ms) a otro punto, con una deriva lenta en el
// medio y microsacadas de ~0.2 grados. Devuelve (giro horizontal, giro vertical) en radianes, para el mismo reloj y la misma
// semilla (por jugador) en el VS del globo del ojo (girar la esfera) y en el de la cara (el parpado de arriba acompana
// la mirada vertical: baja ~ 2/3 del angulo al mirar abajo, sube 1/3 al mirar arriba).
float2 PesGazeHash2(float n, float seed)
{
  float a = frac(sin((n + seed * 17.3) * 12.9898) * 43758.5453);
  float b = frac(sin((n + seed * 29.7) * 78.233) * 43758.5453);
  return float2(a, b);
}
float2 PesGazeTarget(float n, float seed)
{
  float2 h = PesGazeHash2(n, seed);
  // horizontal +-4.5 grados, vertical -3.5..+2.5 (se mira mas hacia abajo que hacia arriba); 1 de cada 4 fijaciones vuelve al centro
  float2 g = float2((h.x - 0.5) * 2.0 * 0.078, (h.y - 0.58) * 2.0 * 0.052);
  float back = step(0.75, frac(h.x * 7.1 + h.y * 3.3));
  return lerp(g, g * 0.25, back);
}
float2 PesGaze(float t, float seed, float amp)
{
  if (amp <= 0.0) return 0;
  // fijaciones de largo variable: la cuenta avanza con una tabla de largos 0.45..1.9 s (suma 1.2 s de media)
  float tt = t + seed * 23.1;
  float per = 1.2;
  float n = floor(tt / per);
  float h = PesGazeHash2(n, seed + 0.5).x;
  float start = n * per + h * 0.6;              // cuando salta dentro de esta ventana
  float x = tt - start;                         // tiempo desde la sacada (negativo = todavia la fijacion anterior)
  float2 g0 = PesGazeTarget(n - 1.0, seed), g1 = PesGazeTarget(n, seed);
  float k = smoothstep(0.0, 0.045, x);          // sacada ~45 ms (con sobrepaso chico)
  k = k * (1.0 + 0.12 * sin(3.14159 * saturate(x / 0.09)));
  float2 g = lerp(g0, g1, saturate(k) + (k - saturate(k)));
  // deriva lenta + microsacadas
  g += float2(sin(tt * 1.7 + seed) * 0.0030 + sin(tt * 23.0 + seed * 3.0) * 0.0012,
              sin(tt * 1.3 + seed * 2.0) * 0.0022 + sin(tt * 19.0 + seed) * 0.0010);
  return g * amp;
}
// semilla por jugador: la misma cuenta que el parpadeo (tabla de tonos de piel por la posicion del hueso 0 en el mundo)
float PesExprSeed()
{
  bool sk = g_vsObject.m_useWeightCount.x != 0;
  float3 b0 = float3(g_vsBone.m_boneMatrices[0]._m30, g_vsBone.m_boneMatrices[0]._m31, g_vsBone.m_boneMatrices[0]._m32);
  float3 b0b = float3(g_vsPrevBone.m_boneMatrices[0]._m30, g_vsPrevBone.m_boneMatrices[0]._m31, g_vsPrevBone.m_boneMatrices[0]._m32);
  int se = PesSkinNearest(PesSkinPoint(g_vsObject.m_world, g_vsObject.m_viewWorld, g_vsScene.m_view, b0, b0b, sk));
  return se >= 0 ? (float)se * 0.6180339 : 0.37;
}
// giro de la mirada aplicado a un punto respecto del centro del ojo: g.x gira alrededor del eje y (mirar a los lados; +x = a la
// izquierda del jugador = a su derecha en pantalla), g.y alrededor del eje x (+ = arriba)
float3 PesGazeRot(float3 q, float2 g)
{
  float cy = cos(g.x), sy = sin(g.x), cx = cos(g.y), sx = sin(g.y);
  float3 r = float3(q.x * cy + q.z * sy, q.y, -q.x * sy + q.z * cy);
  return float3(r.x, r.y * cx - r.z * sx, r.y * sx + r.z * cx);
}
// GLOBO DEL OJO (VS 69758ab9): gira los vertices del ojo alrededor de su centro (reposo, antes del skinning). Centro por ojo:
// el de la DLL si lo tiene (g_pesExprEye), si no (+-0.0295, 1.6968, 0.138) como el parpadeo.
float3 PesGazeEye(float3 rest, float3 center, float t, float seed)
{
  float amp = IniParams.Load(int2(347, 0)).w;
  if (amp <= 0.0) return rest;
  float2 g = PesGaze(t, seed, amp);
  return center + PesGazeRot(rest - center, g);
}
// (01/11) parpadeo del mod. El juego casi no parpadea fuera de las escenas: cada jugador parpadea cada 2..6 s (al azar por
// ciclo, semilla = su entrada de la tabla de tonos de piel, igual para la piel, las pestanas y los parpados de la misma cara),
// 0.3 s: cierra en 0.08, queda 0.04, abre en 0.18. Los parpados (la DLL los encuentra por malla, g_pesExprLids) giran en el
// reposo alrededor del centro del ojo (x +-0.028, y 1.6968, z 0.138; eje x): el de arriba baja, el de abajo sube un poco; los
// pesos del rig hacen el fundido (como el parpadeo del juego). Si el juego ya esta cerrando ese parpado, el del mod se aparta.
// 342.w = cierre (0 = sin parpadeo del mod, 1 = cierra entero).
float PesBlinkCurve(float t, float seed)
{
  const float per = 5.5;   // (01/11 d) antes 4: un parpadeo cada 5.5 s en promedio
  float tt = t + seed * 37.3;
  float n = floor(tt / per);
  float h = frac(sin((n + seed * 13.1) * 12.9898) * 43758.5453);
  float x = tt - (n * per + h * (per - 0.4));
  if (x < 0.0 || x > 0.30) return 0.0;
  return x < 0.08 ? smoothstep(0.0, 0.08, x) : (x < 0.12 ? 1.0 : 1.0 - smoothstep(0.12, 0.30, x));
}
float3 PesRotX(float3 q, float c, float s) { return float3(q.x, q.y * c - q.z * s, q.y * s + q.z * c); }

void PesBlinkApply(float4 p, float3 rest, uint4 I, float4 W, float3 n,
                   inout float3 pos, inout float3 prev, inout float3 nrm)
{
  float amt = IniParams.Load(int2(342, 0)).w;
  if (amt <= 0.0) return;
  uint4 L = g_pesExprLids;
  if (L.x > 31 && L.y > 31) return;
  if (rest.y < 1.670 || rest.y > 1.725 || rest.z < 0.110 || abs(rest.x) > 0.060) return;
  uint head = g_pesExprMesh.x;
  // (01/11 l, Agustin: "el parpadeo en una animacion facial no deberia ocurrir") si el juego esta animando la cara (la comisura
  // o la ceja interna se movieron mas de 1.5 mm respecto de la cabeza) el parpadeo del mod se apaga (la animacion trae el suyo)
  float anim = 0.0;
  if (head <= 31) {
    float4 Pc = float4(0.0236, 1.6297, 0.1615, 1.0), Pb = float4(0.0157, 1.7129, 0.1718, 1.0);
    uint cj = g_pesExprMus.x, bj = g_pesExprMus.z;
    if (cj <= 31) anim = max(anim, length(mul(Pc, g_vsBone.m_boneMatrices[cj]) - mul(Pc, g_vsBone.m_boneMatrices[head])));
    if (bj <= 31) anim = max(anim, length(mul(Pb, g_vsBone.m_boneMatrices[bj]) - mul(Pb, g_vsBone.m_boneMatrices[head])));
    amt *= 1.0 - smoothstep(0.0008, 0.0015, anim);
    if (amt <= 0.0) return;
  }
  // semilla por jugador (la misma cuenta que el tono de piel al final del VS)
  bool sk = g_vsObject.m_useWeightCount.x != 0;
  float3 b0 = float3(g_vsBone.m_boneMatrices[0]._m30, g_vsBone.m_boneMatrices[0]._m31, g_vsBone.m_boneMatrices[0]._m32);
  float3 b0b = float3(g_vsPrevBone.m_boneMatrices[0]._m30, g_vsPrevBone.m_boneMatrices[0]._m31, g_vsPrevBone.m_boneMatrices[0]._m32);
  int se = PesSkinNearest(PesSkinPoint(g_vsObject.m_world, g_vsObject.m_viewWorld, g_vsScene.m_view, b0, b0b, sk));
  float seed = se >= 0 ? (float)se * 0.6180339 : 0.37;
  float t = IniParams.Load(int2(157, 0)).x;
  float b = PesBlinkCurve(t, seed) * saturate(amt);
  float bp = PesBlinkCurve(t - 1.0 / 60.0, seed) * saturate(amt);   // cuadro anterior (vectores de movimiento)
  // (01/11 h) el parpado acompana la mirada vertical (347.w): abajo baja 0.65 del angulo, arriba sube 0.35 (solo el de arriba)
  // (02/11, Agustin: "los ojos vivos es lo que rompe, no mueve el esqueleto real") el parpado ya NO sigue la mirada por shader:
  // solo se movia en las mallas donde la DLL encontro el parpado y se separaba de las pestanas / la otra malla. Vuelve cuando
  // la mirada y los parpados vayan por los huesos del esqueleto de la cara (exprbones.cpp)
  float gz = 0.0;
  float gy = 0.0, gyp = 0.0;
  if (gz > 0.0) { gy = PesGaze(t, seed, gz).y; gyp = PesGaze(t - 1.0 / 60.0, seed, gz).y; }
  if (b <= 0.0 && bp <= 0.0 && abs(gy) < 0.0005 && abs(gyp) < 0.0005) return;
  float3 C = float3(rest.x >= 0.0 ? 0.028 : -0.028, 1.6968, 0.138);
  float3 q = p.xyz - C;
  [unroll] for (int k = 0; k < 4; k++) {
    uint j = I[k]; float w = W[k];
    if (w <= 0.0 || j > 31) continue;
    float a = 0.0;
    bool up = (j == L.x || j == L.y), lo = (j == L.z || j == L.w);
    if (!up && !lo) continue;
    float4x3 M = g_vsBone.m_boneMatrices[j];
    float g = 0.0;   // el juego ya cierra este parpado: punto de la orilla con el hueso contra con la cabeza
    if (head <= 31) { float4 Q = float4(C + float3(0.0, up ? 0.008 : -0.006, 0.008), 1.0); g = saturate(length(mul(Q, M) - mul(Q, g_vsBone.m_boneMatrices[head])) / 0.006); }
    float A = (up ? 0.50 : -0.10) * (1.0 - g);   // (01/11 d, Agustin: "los ojos se cierran mucho") antes 0.68 / -0.14   // orilla de arriba ~ +27 grados abierta -> ~ -12; la de abajo -22 -> ~ -14
    // angulo total del parpado: parpadeo + seguimiento de la mirada (el de arriba: -gy * 0.65 si mira abajo, -gy * 0.35 arriba;
    // el de abajo sube un poco al mirar abajo). El sentido: A > 0 cierra el de arriba (baja), A < 0 el de abajo (sube).
    // (02/11, video de Agustin: ranuras azules bajo el ojo al mover la mirada) el de abajo ya no acompana la mirada: solo se
    // movia en las mallas donde la DLL encontro el parpado y se separaba de las pestanas / la otra malla del parpado
    float fu = up ? (gy < 0.0 ? 0.65 : 0.35) : 0.0, fup = up ? (gyp < 0.0 ? 0.65 : 0.35) : 0.0;
    float a1 = A * b + (up ? -gy * fu : gy * fu) * (1.0 - g), a0 = A * bp + (up ? -gyp * fup : gyp * fup) * (1.0 - g);
    float c1 = cos(a1), s1 = sin(a1), c0 = cos(a0), s0 = sin(a0);
    float3 d1 = PesRotX(q, c1, s1) - q, d0 = PesRotX(q, c0, s0) - q;
    pos  += w * mul(d1, (float3x3)M);
    prev += w * mul(d0, (float3x3)g_vsPrevBone.m_boneMatrices[j]);
    nrm  += w * mul(PesRotX(n, c1, s1) - n, (float3x3)M);
  }
}

// (01/11 e, Agustin: "un slider para que el personaje abra la boca por completo") fila 347.x: gira en el reposo, antes del
// skinning, los vertices de los huesos de abajo (mandibula, labio de abajo, papada; la DLL los marca por malla en g_pesExprJaw)
// alrededor de la bisagra de skf_jaw (face_rig 111320: 0, 1.6369, 0.0795; eje x). Se suma a lo que ya haga el juego.
// 1 = ~23 grados (boca abierta del todo), negativo = cierra un poco. Las comisuras van a la mitad; el resto del peso (labio de
// arriba, mejillas) no se mueve, asi que la piel entre los dos se estira como en la boca abierta del juego.
void PesJawApply(float4 p, uint4 I, float4 W, float3 n,
                 inout float3 pos, inout float3 prev, inout float3 nrm)
{
  // (01/11 e, Agustin: "que la nariz se mueva un poquito mas con la boca ... las fosas nasales") 347.y: las alas y las fosas
  // nasales (x 0.6..2.8 cm del medio, y ~1.655) siguen la mitad del gesto del labio de arriba del medio (skf_lip_t_c: sonrisa,
  // mueca, labio que sube) y se abren hacia afuera con ese gesto y con la boca abierta (como al respirar fuerte o gritar).
  float4 Rj = IniParams.Load(int2(347, 0));
  float o = clamp(Rj.x, -0.25, 1.0);
  uint lt = g_pesExprJaw.z, hh = g_pesExprMesh.x;
  // (01/11 f, Agustin: "los labios superiores no tienen mucho movimiento") 347.z: el labio de arriba (skf_lip_t_*, lip_volume;
  // la DLL los marca en g_pesExprJaw.w) suma este tanto mas de su propio gesto contra la cabeza (sube / baja / se cierra
  // sobre el de abajo). Tope 6 mm.
  if (Rj.z > 0.0 && hh <= 31 && g_pesExprJaw.w != 0u) {
    float4x3 Mh0 = g_vsBone.m_boneMatrices[hh], Ph0 = g_vsPrevBone.m_boneMatrices[hh];
    float3 ph0 = mul(p, Mh0), pq0 = mul(p, Ph0), du = 0, dv = 0;
    [unroll] for (int k = 0; k < 4; k++) {
      uint j = I[k]; float w = W[k];
      if (w <= 0.0 || j > 31 || (g_pesExprJaw.w & (1u << j)) == 0u) continue;
      du += w * (mul(p, g_vsBone.m_boneMatrices[j]) - ph0);
      dv += w * (mul(p, g_vsPrevBone.m_boneMatrices[j]) - pq0);
    }
    du *= Rj.z; dv *= Rj.z;
    float lu = length(du), lv = length(dv);
    if (lu > 0.006) du *= 0.006 / lu; if (lv > 0.006) dv *= 0.006 / lv;
    pos += du; prev += dv;
  }
  if (Rj.y > 0.0 && lt <= 31 && hh <= 31) {
    // (01/11 g, Agustin: "mas suave y tomando mas las fosas nasales hasta arriba") zona: alas y costados de la nariz desde la
    // fosa (y ~1.650) hasta arriba del ala (y ~1.690), x 0.4..3.2 cm del medio; bordes anchos (sin escalon) y el peso baja
    // de a poco hacia arriba. Seguimiento 0.35 del labio (antes 0.5) con rodilla suave, apertura de las alas mas chica.
    // (01/11 l, Agustin: "algunas caras se cortan en la nariz") la zona ya no deja quieta la punta (antes |x| < 2 mm): toda la
    // punta y las alas siguen, con el borde de afuera ancho; solo la apertura de las alas queda en las alas
    float ax = abs(p.x);
    float zx = 1.0 - smoothstep(0.024, 0.038, ax);
    float wing = smoothstep(0.006, 0.014, ax);
    float zy = smoothstep(1.645, 1.656, p.y) * (1.0 - smoothstep(1.668, 1.694, p.y));
    float nm = zx * zy * smoothstep(0.11, 0.15, p.z) * saturate(Rj.y);
    if (nm > 0.0) {
      float4x3 Mh = g_vsBone.m_boneMatrices[hh], Ph = g_vsPrevBone.m_boneMatrices[hh];
      float3 dl = mul(p, g_vsBone.m_boneMatrices[lt]) - mul(p, Mh);
      float3 dq = mul(p, g_vsPrevBone.m_boneMatrices[lt]) - mul(p, Ph);
      float L = length(dl), Lq = length(dq);
      dl *= 1.0 / (1.0 + L / 0.006); dq *= 1.0 / (1.0 + Lq / 0.006);   // rodilla suave: gestos chicos casi enteros, grandes acotados
      float sx = p.x >= 0.0 ? 1.0 : -1.0;
      float fl = 0.20 * length(dl) + 0.0010 * max(o, 0.0), flq = 0.20 * length(dq) + 0.0010 * max(o, 0.0);   // apertura de las alas
      pos  += nm * (0.35 * dl + wing * mul(float3(sx * fl, 0.0, 0.0), (float3x3)Mh));
      prev += nm * (0.35 * dq + wing * mul(float3(sx * flq, 0.0, 0.0), (float3x3)Ph));
    }
  }
  if (abs(o) < 0.001) return;
  // (01/11 l) boca por HUESOS: la DLL gira skf_jaw y lo que cuelga de el en el esqueleto (exprbones.cpp; 350.x pedido, 350.y
  // = la DLL confirma que lo esta haciendo): entonces el giro ya viene en el skinning y aca no se gira nada mas
  float4 Rb = IniParams.Load(int2(350, 0));
  if (Rb.x > 0.5 && Rb.y > 0.5) return;
  uint full = g_pesExprJaw.x, half_ = g_pesExprJaw.y;
  if ((full | half_) == 0u) return;
  float A = 0.40 * o;
  float c = cos(A), s = sin(A);
  float3 C = float3(0.0, 1.6369, 0.0795);
  float3 q = p.xyz - C;
  float3 d = PesRotX(q, c, s) - q, dn = PesRotX(n, c, s) - n;
  [unroll] for (int k = 0; k < 4; k++) {
    uint j = I[k]; float w = W[k];
    if (w <= 0.0 || j > 31) continue;
    uint bit = 1u << j;
    float f = (full & bit) != 0u ? 1.0 : ((half_ & bit) != 0u ? 0.5 : 0.0);
    if (f <= 0.0) continue;
    if (f < 1.0) { float c2 = cos(A * f), s2 = sin(A * f); float3 dh = PesRotX(q, c2, s2) - q; pos += w * mul(dh, (float3x3)g_vsBone.m_boneMatrices[j]); prev += w * mul(dh, (float3x3)g_vsPrevBone.m_boneMatrices[j]); nrm += w * mul(PesRotX(n, c2, s2) - n, (float3x3)g_vsBone.m_boneMatrices[j]); continue; }
    pos  += w * mul(d, (float3x3)g_vsBone.m_boneMatrices[j]);
    prev += w * mul(d, (float3x3)g_vsPrevBone.m_boneMatrices[j]);
    nrm  += w * mul(dn, (float3x3)g_vsBone.m_boneMatrices[j]);
  }
}
// (01/11 i, Agustin: "inyectar mas huesos ... la malla se deforma con mas realismo y la luz se proyecta") MUSCULOS VIRTUALES,
// fila 348. El juego no anima huesos nuevos, asi que se calculan en el VS a partir de lo que ya mueve: cuanto subio / salio
// la comisura (skf_lip_s) y cuanto bajo / entro la ceja interna (skf_brow_i), y con eso se levanta o hunde la piel de reposo
// a lo largo de la normal, con el gradiente del bulto sumado a la normal para que la luz lo marque:
//  - x mejilla: con la sonrisa el pomulo se infla (bulto de 3 mm centrado en (+-4.0, 166.0) cm) y sube un poco;
//  - y surco nasolabial: la piel se hunde 1.5 mm a lo largo de la linea ala de la nariz -> comisura y hace cresta 0.9 mm
//    por fuera (el pliegue real de la sonrisa);
//  - z ceno: al fruncir, dos crestas verticales a +-6.5 mm del medio (1.2 mm) y un surco en el medio (0.6 mm) entre las cejas.
float PesMusGesture(uint j, uint hh, float4 P, bool prev)
{
  if (j > 31) return 0.0;
  float3 d = prev ? mul(P, g_vsPrevBone.m_boneMatrices[j]) - mul(P, g_vsPrevBone.m_boneMatrices[hh])
                  : mul(P, g_vsBone.m_boneMatrices[j]) - mul(P, g_vsBone.m_boneMatrices[hh]);
  // en espacio del objeto; +y arriba, x hacia afuera segun el lado del pivote
  float outw = P.x >= 0.0 ? d.x : -d.x;
  return d.y + 0.5 * outw;
}
void PesMuscleApply(float4 p, float3 rest, uint4 I, float4 W, float3 n,
                    inout float3 pos, inout float3 prev, inout float3 nrm)
{
  float4 Mu = IniParams.Load(int2(348, 0));
  if (Mu.x <= 0.0 && Mu.y <= 0.0 && Mu.z <= 0.0) return;
  uint hh = g_pesExprMesh.x;
  if (hh > 31) return;
  float face = smoothstep(0.115, 0.145, rest.z);
  if (face <= 0.0) return;
  int sd = rest.x >= 0.0 ? 0 : 1;
  float sx = rest.x >= 0.0 ? 1.0 : -1.0;
  float x = abs(rest.x), y = rest.y;
  uint corner = sd == 0 ? g_pesExprMus.x : g_pesExprMus.y;
  uint brow = sd == 0 ? g_pesExprMus.z : g_pesExprMus.w;
  float4 Pc = float4(sx * 0.0236, 1.6297, 0.1615, 1.0), Pb = float4(sx * 0.0157, 1.7129, 0.1718, 1.0);
  // sonrisa: la comisura sube / sale (6 mm = entera); ceno: la ceja interna baja / entra (4 mm = entero)
  float smile = saturate(PesMusGesture(corner, hh, Pc, false) / 0.006), smileP = saturate(PesMusGesture(corner, hh, Pc, true) / 0.006);
  float frown = saturate(-PesMusGesture(brow, hh, Pb, false) / 0.004), frownP = saturate(-PesMusGesture(brow, hh, Pb, true) / 0.004);
  if (smile <= 0.001 && smileP <= 0.001 && frown <= 0.001 && frownP <= 0.001) return;
  // (01/11 k, captura de Agustin: "corte" azul en la union del ala de la nariz y rayas en la mandibula) la altura NO va
  // por la normal del vertice: en las aristas duras (ala de la nariz, menton) el modelo trae el mismo punto dos veces con
  // normales distintas y cada copia se iba para otro lado (se abria la malla y se veia el fondo). Va por una direccion
  // fija de la cabeza, "hacia afuera" de la mejilla / de la frente, igual para todas las copias del punto.
  float3 nd = normalize(float3(sx * 0.55, -0.12, 0.83));   // mejilla y surco: afuera y adelante
  float3 nf = normalize(float3(sx * 0.10, 0.12, 0.99));    // ceno: casi adelante
  float h = 0, hp = 0;           // altura a lo largo de nd (m), este cuadro / anterior
  float hf = 0, hfp = 0;         // idem a lo largo de nf (ceno)
  float2 gr = 0;                 // gradiente de h en (x hacia afuera, y) para inclinar la normal
  float up = 0, upP = 0;         // subida de la mejilla
  // ---- mejilla
  if (Mu.x > 0.0) {
    float ex = (x - 0.040) / 0.016, ey = (y - 1.660) / 0.014;
    float g = exp(-(ex * ex + ey * ey));
    float a = Mu.x * 0.0030;
    h += a * smile * g; hp += a * smileP * g;
    gr += a * smile * g * float2(-2.0 * ex / 0.016, -2.0 * ey / 0.014);
    up += 0.4 * a * smile * g; upP += 0.4 * a * smileP * g;
  }
  // ---- surco nasolabial: linea del ala de la nariz (1.53, 166.03) a la comisura (2.36, 162.97), prolongada 3 mm
  if (Mu.y > 0.0) {
    float2 A = float2(0.0153, 1.6603), B = float2(0.0236, 1.6297);
    float2 u = normalize(B - A); float L = length(B - A);
    float2 lat = float2(u.y, -u.x);                 // hacia afuera (+x)
    float2 q = float2(x, y) - A;
    float s = dot(q, u) / L, l = dot(q, lat);
    float along = smoothstep(-0.15, 0.1, s) * (1.0 - smoothstep(0.95, 1.2, s));
    // (01/11 l, capturas de Agustin: el surco salia como una raya fina y oscura) surco ancho (7 mm) y de 0.7 mm, cresta 8 mm
    // y 0.4 mm, y la inclinacion de la normal a la mitad
    float eg = l / 0.0035, er = (l - 0.0075) / 0.004;
    float groove = exp(-eg * eg), ridge = exp(-er * er);
    float prof = (-0.0007 * groove + 0.0004 * ridge) * Mu.y * along;
    float dprof = (-0.0007 * groove * (-2.0 * eg / 0.0035) + 0.0004 * ridge * (-2.0 * er / 0.004)) * Mu.y * along * 0.5;
    h += prof * smile; hp += prof * smileP;
    gr += dprof * smile * lat;
  }
  // ---- ceno
  if (Mu.z > 0.0) {
    float ey = (y - 1.708) / 0.011, ex1 = (x - 0.0065) / 0.0045, ex0 = x / 0.0035;
    float gy = exp(-ey * ey), ridge = exp(-ex1 * ex1) * gy, groove = exp(-ex0 * ex0) * gy;
    float a = Mu.z * (0.0009 * ridge - 0.0004 * groove);
    float dax = Mu.z * (0.0009 * ridge * (-2.0 * ex1 / 0.0045) - 0.0004 * groove * (-2.0 * ex0 / 0.0035)) * 0.5;
    float day = Mu.z * (0.0009 * ridge - 0.0004 * groove) * (-2.0 * ey / 0.011) * 0.5;
    hf += a * frown; hfp += a * frownP;
    gr += float2(dax, day) * frown;
  }
  h *= face; hp *= face; hf *= face; hfp *= face; gr *= face; up *= face; upP *= face;
  float3 off = nd * h + nf * hf + float3(0.0, up, 0.0), offP = nd * hp + nf * hfp + float3(0.0, upP, 0.0);
  pos  += mul(off, (float3x3)g_vsBone.m_boneMatrices[hh]);
  prev += mul(offP, (float3x3)g_vsPrevBone.m_boneMatrices[hh]);
  // la normal se inclina en contra del gradiente (campo de altura sobre la piel, que mira casi a +z)
  float3 dn = -float3(sx * gr.x, gr.y, 0.0);
  nrm += mul(dn, (float3x3)g_vsBone.m_boneMatrices[hh]);
}
#endif
