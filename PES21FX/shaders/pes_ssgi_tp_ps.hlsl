// PES21 FX - rebote de luz en pantalla (SSGI) temporal, paso 2 (21/10): acumulacion en el tiempo.
// Cada pixel se pasa a mundo con la camara de este cuadro y se proyecta con la del cuadro anterior para leer su historia.
// La historia se descarta si ahi habia otra superficie (profundidad distinta: algo que se movio o aparecio) o si cae fuera
// de pantalla, y se recorta a la media +- 1.5 desvios de los vecinos de este cuadro para que no queden estelas.
//   t0 SSGI de este cuadro (rgb, profundidad), t1 historia (rgb, profundidad); b2 escena de este cuadro, b3 la del anterior.
cbuffer cScene : register(b2) { float4 c[30]; };
cbuffer cPrev  : register(b3) { float4 q[30]; };
Texture2D<float4> inCur  : register(t0);
Texture2D<float4> inHist : register(t1);
Texture1D<float4> IniParams : register(t120);
SamplerState sPoint : register(s0);
SamplerState sLinear : register(s1);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float4 cur = inCur.SampleLevel(sPoint, uv, 0);
  float z = cur.w;
  if (z <= 0.0) return 0.0;
  float W, H; inCur.GetDimensions(W, H);
  float2 tx = 1.0 / float2(W, H);
  // vecinos de este cuadro (3x3, misma superficie)
  float3 m1 = 0, m2 = 0; float n = 0;
  [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
    float4 s = inCur.SampleLevel(sPoint, uv + float2(x, y) * tx, 0);
    if (abs(s.w - z) > 0.05 * z) continue;
    m1 += s.rgb; m2 += s.rgb * s.rgb; n += 1.0;
  }
  m1 /= max(n, 1.0); m2 /= max(n, 1.0);
  float3 sd = sqrt(max(m2 - m1 * m1, 0.0));
  // a mundo con la camara actual y de vuelta a pantalla con la anterior
  float2 sC = uv * float2(2, -2) + float2(-1, 1);
  float3 Pv = float3((sC - c[28].xy) * c[21].xy * z, z);
  float3 pW = c[20].xyz + float3(dot(Pv, float3(c[8].x, c[9].x, c[10].x)), dot(Pv, float3(c[8].y, c[9].y, c[10].y)), dot(Pv, float3(c[8].z, c[9].z, c[10].z)));
  float4 hp = float4(pW, 1.0);
  float cw = dot(q[3], hp);
  float4 P153 = IniParams.Load(int2(153, 0));
  float frames = clamp(P153.z > 0 ? P153.z : 12.0, 2.0, 40.0);
  // (23/10) sin historia valida (corte de camara, algo que aparece): en vez del valor de 8 muestras de este cuadro (ruidoso:
  // se veia "recalcular" el rebote en cada corte) se arranca del promedio de los vecinos de la misma superficie
  float a = 0.3;
  float3 hist = m1;
  if (cw > 0.05 && q[21].z != 0.0) {
    float2 ndc = float2(dot(q[0], hp), dot(q[1], hp)) / cw + q[28].xy;
    float2 uvP = ndc * float2(0.5, -0.5) + 0.5;
    if (all(uvP > tx) && all(uvP < 1.0 - tx)) {
      float4 h = inHist.SampleLevel(sLinear, uvP, 0);
      float hz = inHist.SampleLevel(sPoint, uvP, 0).w;
      if (hz > 0.0 && abs(hz - cw) < 0.04 * cw + 0.03) {
        hist = clamp(h.rgb, m1 - 1.5 * sd, m1 + 1.5 * sd);
        a = 1.0 / frames;
      }
    }
  }
  return float4(lerp(hist, cur.rgb, a), z);
}
