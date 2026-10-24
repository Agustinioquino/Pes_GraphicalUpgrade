// PES21 FX - tono de piel por jugador (24/10 c): transformacion del color de la piel en Oklab.
// La paleta del juego (6 colores) solo cambia el brillo (mismo matiz y saturacion en los 6): por eso pasar de un color a
// otro con una ganancia por canal se veia como "mas brillo". Ahora la DLL (skintone.cpp) arma por jugador tres numeros a
// partir de una escala de piel real (la luminosidad de los 6 colores del juego y el matiz / saturacion de una escala de
// tonos de piel medida): kL multiplica la luminosidad de Oklab y (m0, m1) giran y escalan el matiz y la saturacion
// (ab' = kC * R(dh) * ab). Asi se conserva el detalle de la textura (poros, venas, rubor) y cambia el tono de verdad.
// Tabla de la DLL (t120): 194 = (entradas, activo, distancia maxima en m, -); por entrada i (hasta 30): 195 + 2i = posicion
// del personaje en el mundo (su raiz), 196 + 2i = (kL, m0, m1, 1 = tono propio / 0 = tapon: un personaje sin tono propio
// cerca de uno con tono, para que no se lo lleve). El VS de la piel elige la entrada mas cercana al dibujo y la pasa al PS en
// o6.w (1000 + i; cualquier otro valor = sin tono propio).
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
  return IniParams.Load(int2(196 + 2 * i, 0)).w > 0.5 ? i : -1;
}
float3 PesSkinParams(int i) { return IniParams.Load(int2(196 + 2 * i, 0)).xyz; }
// para el VS: la entrada mas cercana (en el plano del piso) a un punto del personaje, o -1
int PesSkinNearest(float3 wp) {
  float4 H = IniParams.Load(int2(194, 0));
  int best = -1;
  if (H.y > 0.5 && H.x > 0.5) {
    float bd = H.z * H.z;
    int n = min((int)(H.x + 0.5), 30);
    [loop] for (int i = 0; i < n; i++) {
      float3 p = IniParams.Load(int2(195 + 2 * i, 0)).xyz;
      float2 d = wp.xz - p.xz;
      float d2 = dot(d, d);
      if (d2 < bd) { bd = d2; best = i; }
    }
  }
  return best;
}
#endif
