// PES21 FX (04/11) - grosor REAL de la piel para la transmision a contraluz, medido con el atlas de sombras propio del mod (t30,
// headshadow.cpp / pes_headshadow.hlsli). El atlas guarda, por cada cabeza cercana, la profundidad a lo largo de la MISMA luz que
// usan las pasadas del sol (fila 310 yzw, sacada del LightCB), a ~1.8 mm por texel, en una caja de 1.8 m alrededor de cabeza,
// hombros, brazos y torso. Grosor = distancia entre lo primero que ve el sol y este pixel (Jimenez 2010), coherente con cualquier
// sol_parche / sombra_parche (el shadow map del juego no: con el receptor girado daba manchas naranjas de borde duro, 04/11).
// Agustin (04/11): "quiero que el shadder lea el grosor de piel real" / "que el sss permita la translucidez de la luz real, y con
// forma real dependiendo la cara y brazos, piernas de la luz".
// Necesita pes_headshadow.hlsli antes (PESHeadShadow, filas 309/310/316/317..319).
#ifndef PES_SKINTHICK_HLSLI
#define PES_SKINTHICK_HLSLI

// devuelve el grosor en metros (9 muestras a 1.5 texeles, cada una recortada a 8 cm, piso de 2 mm, como la del shadow map del
// juego); inBox = peso de la caja (1 adentro, fundido 60-100 % del medio lado, 0 afuera o sin atlas este cuadro)
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
  const float tx = 1.0 / 2048.0;
  [unroll] for (int i = 0; i < 3; i++) {
    if (i >= n) break;
    float4 C = IniParams.Load(int2(317 + i, 0));
    float3 d = Pv - C.xyz;
    float x = dot(d, R) / hf, y = dot(d, Up) / hf, z = dot(d, -L) / (2.0 * hf) + 0.5;
    if (abs(x) > 1.0 || abs(y) > 1.0 || z < 0.0 || z > 1.0) continue;
    float edge = 1.0 - saturate((max(abs(x), abs(y)) - 0.6) / 0.4);
    if (edge <= inBox) continue;
    float2 uv = float2((x * 0.5 + 0.5) * 0.5 + (i & 1) * 0.5, (0.5 - y * 0.5) * 0.5 + (i >> 1) * 0.5);
    float2 lo = float2((i & 1) * 0.5, (i >> 1) * 0.5) + 1.0 / 2048.0, hi = lo + 0.5 - 2.0 / 2048.0;
    float t = 0.0, wT = 0.0;
    [unroll] for (int ty = -1; ty <= 1; ty++) [unroll] for (int tx2 = -1; tx2 <= 1; tx2++) {
      float wk = (tx2 == 0 && ty == 0) ? 2.0 : ((tx2 == 0 || ty == 0) ? 1.0 : 0.5);
      float s = PESHeadShadow.SampleLevel(smp, clamp(uv + float2(tx2, ty) * tx * 1.5, lo, hi), 0);
      if (rev) s = 1.0 - s;
      t += wk * min(max((z - s) * 2.0 * hf - 0.002, 0.0), 0.08); wT += wk;   // 2 mm de sesgo (un texel)
    }
    thk = max(t / wT, 0.002);
    inBox = edge;
  }
  return thk;
}
#endif
