// PES21 FX (26/10) - variantes ROBUSTO / ATLETICO de TorsoBody (torso y brazos). Un valor por jugador t: -1 robusto .. 0 como
// TorsoBody .. +1 atletico (slider "Torso y brazos" del menu Fisico / musculatura por jugador, z262: 0.5 .. 1.5).
//   - Modelo (VS de la piel 1c02a92a y de las sombras c6ef0c75): la DLL (manos.cpp) reconoce las mallas del torso y de los brazos
//     por su cantidad de indices y enlaza en t127 sus datos por vertice (textures\pes_var_<malla>.dds, R32G32B32A32_FLOAT, 7 filas
//     por version del modelo: 0 = posicion de reposo para reconocer la version (pants_out / in / tight), 1..3 = robusto
//     (desplazamiento, normal, tangente), 4..6 = atletico). Se mezclan con |t| ANTES del skinning: la forma grande (panza, rollos,
//     pecho caido / V de la espalda, trapecio, pecho lleno) y el relieve esculpido de la malla (alisado / marcado); normales y
//     tangentes nuevas de la malla deformada. Generado por tools/fox/torso/make_variants.py.
//   - Texturas (PS de la piel aa7d1833): mapas normales delta del torso en t16 (robusto) / t17 (atletico) y las estrias del
//     robusto en t18 (mascara de color); la definicion del normal map baja en el robusto y sube en el atletico (torso y brazos);
//     en los brazos el atletico muestra un poco las venas (textura de venas de los musculos con textura) y el robusto las tapa.
//   - Forma grande para la fisica de la ropa: PesTorsoField (el mismo calculo que field() de torso_variants.py; copia en
//     cloth_sim.hlsl) en el cuerpo con el que choca la tela (pes_body_cs.hlsl).
// Filas de t120 (las escribe la DLL en cada cuadro, skintone.cpp): 264 = (t general, 1 = hay tabla de jugadores, 1 = prendido, -);
// 265 + i = (t de la entrada i de la tabla de tonos de piel por jugador (194..254), -, -, -); 285 = (prendido, texturas,
// definicion, -) del ini. (26/10 b) 289.x = base esculpida (relieve del normal map en la malla + su textura de correccion, t20 del PS).
#ifndef PES_VARIANTS_HLSLI
#define PES_VARIANTS_HLSLI

// t del jugador: la entrada de la tabla de jugadores (o -1 = el general)
float PesVariantT(int se) {
  float4 H = IniParams.Load(int2(264, 0));
  if (H.z < 0.5) return 0.0;
  float t = H.x;
  float4 S = IniParams.Load(int2(194, 0));
  if (H.y > 0.5 && S.y > 0.5 && se >= 0 && se < min((int)(S.x + 0.5), 20)) t = IniParams.Load(int2(265 + se, 0)).x;
  return clamp(t, -1.0, 1.0);
}
// para el PS: el codigo del jugador que manda el VS en o6.w (1000 + entrada + 0.4 * torso; 999 + ... = ninguna)
float PesVariantTCode(float code) {
  int i = (int)round(code) - 1000;
  return PesVariantT(i >= 0 && i < 20 ? i : -1);
}

#ifdef PES_VARIANT_VS
// (27/10 d) cerca del anillo del cuello la variante se apaga: el atletico subia el trapecio y el pecho alto contra la base del cuello
// de la cara (que no se mueve) y el cuello quedaba hundido. Borde del torso por angulo = el mismo de pes_collar.hlsli (TorsoBody_cuello
// 1.14); 1 lejos, 0 en el anillo, fundido en 6 cm hacia abajo y hacia afuera.
float PesVarNeckKeep(float3 rest) {
  float3 q = float3(abs(rest.x), rest.y, rest.z);
  if (q.y < 1.36) return 1.0;
  float2 w = float2(q.x, q.z - 0.045); float r = max(length(w), 1e-4);
  float c1 = w.y / r, c2 = 2.0 * c1 * c1 - 1.0, c3 = (4.0 * c1 * c1 - 3.0) * c1, c4 = 2.0 * c2 * c2 - 1.0;
  float yb = 1.53392 - 0.02134 * c1 - 0.0075 * c2 + 0.00721 * c3 - 0.0029 * c4;
  float dy = max(yb - q.y, 0.0), dr = max(r - 0.075, 0.0);   // abajo del borde y afuera del cuello (radio ~7.5 cm)
  return smoothstep(0.0, 0.06, sqrt(dy * dy + dr * dr));
}
Texture2D<float4> g_varData : register(t127);
// mezcla la variante tv sobre la posicion / normal / tangente de reposo del vertice vid (solo si t127 tiene los datos de esta
// malla y la posicion de reposo coincide con alguna version: si no, no toca nada).
// (26/10 b) datos de 10 filas por version: 0 reposo, 1..3 robusto, 4..6 atletico, 7..9 BASE (el relieve del normal map de
// TorsoBody llevado a la malla, para todos: x289). Las filas de las variantes son absolutas (incluyen la base): se mezcla
// base -> variante con |t|. Con x289 apagado (o datos viejos de 7 filas) se mezcla desde el reposo, como antes.
bool PesVariantMorph(uint vid, float tv, inout float3 p, inout float3 n, inout float3 tg) {
  uint w = 0, h = 0; g_varData.GetDimensions(w, h);
  if (w == 0 || vid >= w) return false;
  uint R = (h % 10u == 0u) ? 10u : 7u;
  if (h < R) return false;
  bool baseOn = R == 10u && IniParams.Load(int2(289, 0)).x > 0.5;
  if (abs(tv) < 0.001 && !baseOn) return false;
  uint nv = min(h / R, 8u);   // (26/10 e) hasta 8 versiones: las 3 del modelo y las "sin short" (torso abajo con la forma de la pelvis)
  uint best = 0; float bd = 1e9;
  [loop] for (uint k = 0; k < nv; k++) {
    float3 b = g_varData.Load(int3(vid, R * k, 0)).xyz - p;
    float d = dot(b, b);
    if (d < bd) { bd = d; best = k; }
  }
  if (bd > 1e-6) return false;   // mas de 1 mm: no es esta malla
  float3 d0 = float3(0, 0, 0), n0 = n, t0 = tg;
  if (baseOn) {
    int rb = (int)(R * best) + 7;
    d0 = g_varData.Load(int3(vid, rb, 0)).xyz; n0 = g_varData.Load(int3(vid, rb + 1, 0)).xyz; t0 = g_varData.Load(int3(vid, rb + 2, 0)).xyz;
  }
  float a = saturate(abs(tv)) * PesVarNeckKeep(p);   // (27/10 d) sin variante junto al cuello
  float3 d1 = d0, n1 = n0, t1 = t0;
  if (a > 0.001) {
    int r = (int)(R * best) + (tv < 0.0 ? 1 : 4);
    d1 = g_varData.Load(int3(vid, r, 0)).xyz; n1 = g_varData.Load(int3(vid, r + 1, 0)).xyz; t1 = g_varData.Load(int3(vid, r + 2, 0)).xyz;
  }
  p += lerp(d0, d1, a);
  n = normalize(lerp(n0, n1, a));
  tg = lerp(t0, t1, a); tg = tg - n * dot(tg, n);
  float tl = dot(tg, tg); tg = tl > 1e-10 ? tg * rsqrt(tl) : tg;
  return true;
}
#endif

// ---- forma grande del torso (m) para un punto de reposo p (= field() de tools/fox/torso/torso_variants.py)
float PesVarG(float u) { return exp(-0.5 * u * u); }
float PesVarSS(float a, float b, float x) { float t = saturate((x - a) / (b - a)); return t * t * (3.0 - 2.0 * t); }
float PesVarTorsoMask(float3 v) {
  float3 p = float3(abs(v.x), v.y, v.z);
  const float3 cA = float3(0.2631, 1.3704, 0.0195), nA = float3(0.7611, -0.6432, -0.0839);
  float3 qa = p - cA; float sA = dot(qa, nA); float rA = length(qa - sA * nA);
  float arm = PesVarSS(-0.004, 0.004, sA) * (1.0 - PesVarSS(0.11, 0.13, rA));
  const float3 cN = float3(0.0, 1.5223, 0.0552), nN = float3(0.0, 0.8564, 0.5163), e2 = float3(0.0, 0.5163, -0.8564);
  float3 qn = p - cN; float sN = dot(qn, nN); float3 wn = qn - sN * nN;
  float lw = max(length(wn), 1e-5); float ct = dot(wn, e2) / lw;
  float hN = 0.00037 - 0.00183 * ct - 0.01279 * (2.0 * ct * ct - 1.0);
  float neck = PesVarSS(-0.004, 0.004, sN - hN) * (1.0 - PesVarSS(0.10, 0.12, p.x));
  return (1.0 - arm) * (1.0 - neck) * PesVarSS(0.86, 0.90, p.y) * (1.0 - PesVarSS(1.561, 1.569, p.y));
}
float3 PesTorsoField(float3 p, float t) {
  if (abs(t) < 0.001) return float3(0, 0, 0);
  const float ZC = 0.058, AX = 0.155, BZ = 0.110;
  float x = abs(p.x), sx = p.x < 0.0 ? -1.0 : 1.0, y = p.y, z = p.z;
  float dx = x / (AX * AX), dz = (z - ZC) / (BZ * BZ);
  float L = sqrt(dx * dx + dz * dz) + 1e-6;
  float rx = dx / L, rz = dz / L;
  float front = PesVarSS(-0.2, 0.6, rz), back = PesVarSS(-0.2, 0.6, -rz), side = 1.0 - abs(rz);
  float m = PesVarTorsoMask(p) * PesVarSS(0.995, 1.035, y);
  if (m <= 0.0) return float3(0, 0, 0);
  float wa = max(t, 0.0), wr = max(-t, 0.0);
  float rad = 0.0, latx = 0.0, dy = 0.0, tr = 0.0;
  if (wr > 0.0) {
    float uy = y > 1.10 ? (y - 1.10) / 0.085 : (y - 1.10) / 0.050;
    rad += wr * 0.028 * PesVarG(uy) * PesVarG(x / 0.11) * pow(front, 1.3);
    rad += wr * 0.016 * PesVarG((y - 1.08) / 0.045) * side * (0.6 + 0.4 * back) * PesVarSS(0.06, 0.13, x);
    rad += wr * 0.008 * PesVarG((y - 1.09) / 0.050) * back;
    rad += wr * 0.010 * PesVarG((y - 1.17) / 0.070) * side;
    rad += wr * 0.005 * PesVarSS(1.04, 1.10, y) * (1.0 - PesVarSS(1.38, 1.46, y));
    rad += wr * 0.009 * PesVarG((x - 0.068) / 0.055) * PesVarG((y - 1.282) / 0.034) * front;
    rad -= wr * 0.002 * PesVarG((x - 0.070) / 0.060) * PesVarG((y - 1.385) / 0.030) * front;
    dy  -= wr * 0.010 * PesVarG((x - 0.068) / 0.055) * PesVarG((y - 1.300) / 0.036) * front;
    rad += wr * 0.004 * PesVarG((x - 0.15) / 0.03) * PesVarG((y - 1.30) / 0.04) * (1.0 - back);
    rad += wr * 0.005 * PesVarG((y - 1.44) / 0.040) * back;
  }
  if (wa > 0.0) {
    latx += wa * 0.010 * PesVarG((y - 1.30) / 0.060) * PesVarSS(0.10, 0.15, x) * PesVarSS(-0.6, 0.2, -rz);
    rad -= wa * 0.009 * PesVarG((y - 1.14) / 0.060) * side;
    rad -= wa * 0.008 * PesVarG((y - 1.08) / 0.050) * front * PesVarG(x / 0.09);
    rad += wa * 0.007 * PesVarG((x - 0.070) / 0.050) * PesVarG((y - 1.335) / 0.040) * front;
    rad -= wa * 0.0035 * PesVarG((x - 0.075) / 0.045) * PesVarG((y - 1.285) / 0.012) * front;
    rad += wa * 0.0060 * PesVarG((x - 0.055) / 0.045) * PesVarG((y - 1.445) / 0.040) * back;
    tr = wa * 0.0120 * PesVarG((x - 0.115) / 0.042) * PesVarG((y - 1.522) / 0.026) * (1.0 - 0.8 * front);
  }
  float3 td = normalize(float3(0.25, 0.90, -0.35));   // el trapecio alto crece hacia arriba y un poco atras
  return float3((rad * rx + latx + tr * td.x) * sx, dy + tr * td.y, rad * rz + tr * td.z) * m;
}
#endif
