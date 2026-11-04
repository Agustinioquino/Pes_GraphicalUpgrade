// PES21 FX (04/11) - grosor REAL de la piel para la transmision a contraluz, medido con el atlas de sombras propio del mod (t30,
// headshadow.cpp / pes_headshadow.hlsli). El atlas guarda, por cada cabeza cercana, la profundidad a lo largo de la MISMA luz que
// usan las pasadas del sol (fila 310 yzw, sacada del LightCB), a ~1.8 mm por texel, en una caja de 1.8 m alrededor de cabeza,
// hombros, brazos y torso. Grosor = distancia entre lo primero que ve el sol y este pixel (Jimenez 2010), coherente con cualquier
// sol_parche / sombra_parche (el shadow map del juego no: con el receptor girado daba manchas naranjas de borde duro, 04/11).
// Agustin (04/11): "quiero que el shadder lea el grosor de piel real" / "que el sss permita la translucidez de la luz real, y con
// forma real dependiendo la cara y brazos, piernas de la luz".
// (04/11 b) + caja de las PIERNAS del jugador mas cercano (cuarto cuadrante del atlas, fila 363: centro y medio lado propios, mismos
// ejes): la caja de la cabeza se funde desde la cadera para abajo. Y si el atlas esta vacio en ese punto (lo que no entro en la
// cascada que la DLL repite) la caja no cuenta: antes daba 2 mm = contraluz maxima.
// Necesita pes_headshadow.hlsli antes (PESHeadShadow, filas 309/310/316/317..319, 363) y pes_common.hlsli (transmitancia).
#ifndef PES_SKINTHICK_HLSLI
#define PES_SKINTHICK_HLSLI

// una caja: grosor en metros (9 muestras a 1.5 texeles, cada una recortada a 8 cm, piso de 2 mm, como la del shadow map del juego);
// devuelve el peso de la caja (1 adentro, fundido 60-100 % del medio lado, 0 afuera o sin nada dibujado en el atlas en ese punto)
float PESSkThkBox(float3 Pv, float3 C, float hf, int quad, float3 R, float3 Up, float3 L, bool rev, SamplerState smp, out float thk) {
  thk = 0.08;
  float3 d = Pv - C;
  float x = dot(d, R) / hf, y = dot(d, Up) / hf, z = dot(d, -L) / (2.0 * hf) + 0.5;
  if (abs(x) > 1.0 || abs(y) > 1.0 || z < 0.0 || z > 1.0) return 0.0;
  float edge = 1.0 - saturate((max(abs(x), abs(y)) - 0.6) / 0.4);
  float2 q = float2(quad & 1, quad >> 1) * 0.5;
  float2 uv = float2((x * 0.5 + 0.5) * 0.5, (0.5 - y * 0.5) * 0.5) + q;
  float2 lo = q + 1.0 / 2048.0, hi = lo + 0.5 - 2.0 / 2048.0;
  const float tx = 1.0 / 2048.0;
  float t = 0.0, wT = 0.0, sMin = 1.0;
  [unroll] for (int ty = -1; ty <= 1; ty++) [unroll] for (int tx2 = -1; tx2 <= 1; tx2++) {
    float wk = (tx2 == 0 && ty == 0) ? 2.0 : ((tx2 == 0 || ty == 0) ? 1.0 : 0.5);
    float s = PESHeadShadow.SampleLevel(smp, clamp(uv + float2(tx2, ty) * tx * 1.5, lo, hi), 0);
    if (rev) s = 1.0 - s;
    sMin = min(sMin, s);
    t += wk * min(max((z - s) * 2.0 * hf - 0.002, 0.0), 0.08); wT += wk;   // 2 mm de sesgo (un texel)
  }
  if (sMin > 0.9999) return 0.0;   // nada dibujado ahi (valor de limpieza): no hay grosor que medir
  thk = max(t / wT, 0.002);
  return edge;
}

// grosor en metros con la caja de mas peso (cabezas 317..319 o piernas 363); inBox = su peso (0 = afuera de todas)
float PESSkinThickAtlas(float3 Pv, SamplerState smp, out float inBox) {
  inBox = 0.0;
  float thk = 0.08;
  float4 U = IniParams.Load(int2(309, 0));
  float4 I = IniParams.Load(int2(310, 0));
  int n = (int)(I.x + 0.5);
  if (U.x < 0.5 || n <= 0) return thk;
  float4 A = IniParams.Load(int2(316, 0));
  int hsFl = (int)(IniParams.Load(int2(317, 0)).w + 0.5);
  Pv.x *= (hsFl & 2) ? -1.0 : 1.0; Pv.y *= (hsFl & 4) ? -1.0 : 1.0;
  float3 L = normalize(I.yzw), R = normalize(A.xyz); float hf = max(A.w, 0.1);
  float3 Up = cross(L, R) * ((hsFl & 8) ? -1.0 : 1.0);
  bool rev = (hsFl & 1) != 0;
  [unroll] for (int i = 0; i < 3; i++) {
    if (i >= n) break;
    float tk; float e = PESSkThkBox(Pv, IniParams.Load(int2(317 + i, 0)).xyz, hf, i, R, Up, L, rev, smp, tk);
    if (e > inBox) { inBox = e; thk = tk; }
  }
  float4 B = IniParams.Load(int2(363, 0));
  if (B.w > 0.05 && inBox < 0.999) {
    float tk; float e = PESSkThkBox(Pv, B.xyz, B.w, 3, R, Up, L, rev, smp, tk);
    if (e > inBox) { inBox = e; thk = tk; }
  }
  return thk;
}

// (04/11 b) contraluz por grosor real para las pasadas con el modelo de piel compacto (7dbbc): la misma de las 5 grandes del sol
// (densidad del mapa de SSS, perfil de Jimenez, color z34, solo piel de espaldas a la luz). trans = translucidez del G-buffer
float3 PESSkinThickTrans(float thickM, float3 n, float3 L, float rawNdotL, float trans) {
  float4 P64 = IniParams.Load(int2(64, 0));
  float thin = saturate(trans * 1.6);
  float dens = (P64.w > 0 ? P64.w : 30.0) * (1.25 - thin);
  float3 Tt = PESTransColor(PESSkinTransmittance(thickM * dens), IniParams.Load(int2(34, 0)).z);
  float backL = smoothstep(0.0, 0.35, dot(L, -n)) * (1.0 - saturate(rawNdotL * 3.0)) * lerp(0.35, 1.0, smoothstep(0.25, 0.75, thin));
  return Tt * backL * P64.z * 0.8;
}
#endif
