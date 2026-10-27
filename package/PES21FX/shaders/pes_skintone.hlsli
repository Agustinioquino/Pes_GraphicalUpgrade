// PES21 FX - tono de piel por jugador (24/10 c): transformacion del color de la piel en Oklab.
// La paleta del juego (6 colores) solo cambia el brillo (mismo matiz y saturacion en los 6): por eso pasar de un color a
// otro con una ganancia por canal se veia como "mas brillo". Ahora la DLL (skintone.cpp) arma por jugador tres numeros a
// partir de una escala de piel real (la luminosidad de los 6 colores del juego y el matiz / saturacion de una escala de
// tonos de piel medida): kL multiplica la luminosidad de Oklab y (m0, m1) giran y escalan el matiz y la saturacion
// (ab' = kC * R(dh) * ab). Asi se conserva el detalle de la textura (poros, venas, rubor) y cambia el tono de verdad.
// Tabla de la DLL (t120): 194 = (entradas, activo, distancia maxima en m, -); por entrada i (hasta 20): 195 + 3i = posicion
// del personaje en el mundo (su raiz), 196 + 3i = (kL, m0, m1, 1 = tono propio / 0 = tapon: un personaje sin tono propio
// cerca de uno con tono, para que no se lo lleve) para la cara, 197 + 3i = lo mismo para el cuerpo (con la correccion
// "cuerpo como la cara" compuesta; w = 2: "solo el torso", el resto del cuerpo queda como el juego). El VS de la piel elige la entrada mas cercana al dibujo y la pasa al PS en o6.w
// (1000 + i; cualquier otro valor = sin tono propio).
#ifndef PES_SKINTONE_HLSLI
#define PES_SKINTONE_HLSLI

float3 PesLinToOklab(float3 c) {
  c = max(c, 0.0);
  float l = 0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b;
  float m = 0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b;
  float s = 0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b;
  l = pow(max(l, 1e-9), 1.0 / 3.0); m = pow(max(m, 1e-9), 1.0 / 3.0); s = pow(max(s, 1e-9), 1.0 / 3.0);
  return float3(0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s);
}
float3 PesOklabToLin(float3 L) {
  float l_ = L.x + 0.3963377774 * L.y + 0.2158037573 * L.z;
  float m_ = L.x - 0.1055613458 * L.y - 0.0638541728 * L.z;
  float s_ = L.x - 0.0894841775 * L.y - 1.2914855480 * L.z;
  float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
  return float3( 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}
// P = (kL, m0, m1): luminosidad x kL; ab' = (a m0 - b m1, a m1 + b m0)
float3 PesSkinApply(float3 c, float3 P) {
  float3 lab = PesLinToOklab(c);
  lab.x *= P.x;
  lab.yz = float2(lab.y * P.y - lab.z * P.z, lab.y * P.z + lab.z * P.y);
  return saturate(PesOklabToLin(lab));
}
// entrada de la tabla que eligio el VS (o6.w = 1000 + i) si tiene tono propio, o -1
int PesSkinEntry(float code) {
  float4 H = IniParams.Load(int2(194, 0));
  int i = (int)round(code) - 1000;
  if (!(H.y > 0.5 && i >= 0 && i < (int)(H.x + 0.5))) return -1;
  return IniParams.Load(int2(196 + 3 * i, 0)).w > 0.5 ? i : -1;
}
float3 PesSkinParams(int i) { return IniParams.Load(int2(196 + 3 * i, 0)).xyz; }       // cara (y cabeza)
float3 PesSkinParamsBody(int i) { return IniParams.Load(int2(197 + 3 * i, 0)).xyz; }   // cuerpo
// para el VS: un punto del personaje en el mundo. w194 dice como sacarlo (la DLL lo comprueba cada 30 s copiando los bloques
// de constantes de unos dibujos y se queda con la manera que cae sobre algun personaje): 0 = hueso 0 x m_world, 1 = hueso 0
// de la segunda paleta, 2 = traslacion de m_world, 3 = inversa de m_view x m_viewWorld x hueso 0 (no depende de m_world)
float3 PesSkinPoint(float4x4 W, float4x4 VW, float4x4 V, float3 b0, float3 b0b, bool sk) {
  float mode = IniParams.Load(int2(194, 0)).w;
  if (!sk) b0 = float3(0, 0, 0);
  float4 p = float4(b0, 1.0);
  if (mode > 2.5) {
    float3 v = float3(dot(p, VW._m00_m10_m20_m30), dot(p, VW._m01_m11_m21_m31), dot(p, VW._m02_m12_m22_m32));
    float3 d = v - float3(V._m30, V._m31, V._m32);
    return float3(dot(d, float3(V._m00, V._m01, V._m02)), dot(d, float3(V._m10, V._m11, V._m12)), dot(d, float3(V._m20, V._m21, V._m22)));
  }
  if (mode > 0.5 && mode < 1.5 && sk) return b0b;
  if (mode > 0.5) return float3(W._m30, W._m31, W._m32);
  return float3(dot(p, W._m00_m10_m20_m30), dot(p, W._m01_m11_m21_m31), dot(p, W._m02_m12_m22_m32));
}
// para el VS: la entrada mas cercana (en el plano del piso) a un punto del personaje, o -1
int PesSkinNearest(float3 wp) {
  float4 H = IniParams.Load(int2(194, 0));
  int best = -1;
  if (H.y > 0.5 && H.x > 0.5) {
    float bd = H.z * H.z;
    int n = min((int)(H.x + 0.5), 20);
    [loop] for (int i = 0; i < n; i++) {
      float3 p = IniParams.Load(int2(195 + 3 * i, 0)).xyz;
      float2 d = wp.xz - p.xz;
      float d2 = dot(d, d);
      if (d2 < bd) { bd = d2; best = i; }
    }
  }
  return best;
}
// (24/10 g) "solo el torso": cuanto de torso es un punto del cuerpo en reposo (1 en el torso, 0 en brazos, cuello y piernas).
// Costuras medidas en los modelos de TorsoBody v11 (arm.fmdl, neck.fmdl, pants_in/out_sub.fmdl): el anillo donde el brazo se
// une al torso es casi un plano (centro (0.263, 1.370, 0.020), normal (0.761, -0.643, -0.084), radio 4-5 cm); el del cuello
// esta inclinado (adelante mas bajo: 1.478, atras 1.55) y se sigue con su altura segun el angulo alrededor del cuello. Con
// esto el 99 % del torso da 1 y el 99 % de brazos y cuello da 0; en la costura queda una franja de ~1 cm de transicion.
float PesSS(float a, float b, float x) { float t = saturate((x - a) / (b - a)); return t * t * (3.0 - 2.0 * t); }
float PesTorsoMask(float3 v) {
  float3 p = float3(abs(v.x), v.y, v.z);
  const float3 cA = float3(0.2631, 1.3704, 0.0195), nA = float3(0.7611, -0.6432, -0.0839);
  float3 qa = p - cA; float sA = dot(qa, nA); float rA = length(qa - sA * nA);
  float arm = PesSS(-0.004, 0.004, sA) * (1.0 - PesSS(0.11, 0.13, rA));
  const float3 cN = float3(0.0, 1.5223, 0.0552), nN = float3(0.0, 0.8564, 0.5163), e2 = float3(0.0, 0.5163, -0.8564);
  float3 qn = p - cN; float sN = dot(qn, nN); float3 wn = qn - sN * nN;
  float lw = max(length(wn), 1e-5); float ct = dot(wn, e2) / lw;          // coseno del angulo alrededor del cuello
  float hN = 0.00037 - 0.00183 * ct - 0.01279 * (2.0 * ct * ct - 1.0);    // altura del anillo del cuello en ese angulo
  float neck = PesSS(-0.004, 0.004, sN - hN) * (1.0 - PesSS(0.10, 0.12, p.x));
  return (1.0 - arm) * (1.0 - neck) * PesSS(0.86, 0.90, p.y) * (1.0 - PesSS(1.561, 1.569, p.y));
}
// (27/10) torso bajo la camiseta (fila 320): el torso de TorsoBody (y mas con la musculatura / esculpido) es mas grueso que el cuerpo para
// el que el juego hizo las camisetas ajustadas: en el menu del jugador y en partido asomaba por la tela en el escote, el costado y la axila
// (parches de piel sin sombrear). x320 mm que se mete el torso hacia adentro por su normal (fuera del cuello y los brazos, con la misma
// mascara del torso), w320 on. (27/10 c) z320 lo pone la DLL: 1 = hay camisetas en escena; sin camiseta (menu Apariencia / Fisico,
// tecla 4 de Sider) el torso queda entero y no hay escalon en la costura con el brazo y el cuello
void PesTorsoInset(inout float3 mP, float3 mN, float3 rest) {
  float4 T = IniParams.Load(int2(320, 0));
  if (T.w < 0.5 || T.x <= 0.0 || T.z < 0.5) return;
  float k = PesTorsoMask(rest);
  if (k <= 0.0) return;
  float3 n = mN * rsqrt(max(dot(mN, mN), 1e-8));
  mP -= n * (min(T.x, 25.0) * 0.001 * k);
}
// el VS del cuerpo la manda en la parte fraccionaria del codigo del jugador (o6.w = 1000 + i + 0.4 * torso)
float PesSkinTorso(float code) { return saturate((code - round(code)) / 0.4); }
// (24/10 e) medicion del color de la piel (variantes con PES_HL_ACC, solo en el menu Fisico): suma de albedo en punto fijo
// (x 4095) y cantidad de pixeles, por lugar del buffer (slot * 16: r, g, b, n)
#ifdef PES_HL_ACC
void PesSkinAccum(RWByteAddressBuffer buf, uint slot, float3 c) {
  uint3 q = (uint3)(saturate(c) * 4095.0 + 0.5); uint d;
  buf.InterlockedAdd(slot * 16 + 0, q.x, d); buf.InterlockedAdd(slot * 16 + 4, q.y, d); buf.InterlockedAdd(slot * 16 + 8, q.z, d);
  buf.InterlockedAdd(slot * 16 + 12, 1u, d);
}
#endif
#endif
