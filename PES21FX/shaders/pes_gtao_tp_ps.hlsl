// PES21 FX - GTAO temporal (21/10): la oclusion de este cuadro (ya desenfocada) se acumula con la de los anteriores
// reproyectando con la camara del cuadro anterior (como el SSGI temporal). El patron de muestras gira cada cuadro, asi que
// unos pocos cuadros equivalen a 4-8 veces mas direcciones: oclusion limpia y estable sin subir el costo.
// La historia se descarta donde habia otra superficie (profundidad distinta) y se recorta al rango de los vecinos.
//   t0 oclusion de este cuadro (R8), t1 historia (x oclusion, y profundidad lineal), t2 profundidad; b2 escena, b3 la anterior.
cbuffer cScene : register(b2) { float4 c[30]; };
cbuffer cPrev  : register(b3) { float4 q[30]; };
Texture2D<float4> inAO   : register(t0);
Texture2D<float4> inHist : register(t1);
Texture2D<float4> inDepth : register(t2);
SamplerState sPoint : register(s0);
SamplerState sLinear : register(s1);

float LinZ(float2 uv) { return c[21].z / (inDepth.SampleLevel(sPoint, uv, 0).x - c[21].w); }

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float d0 = inDepth.SampleLevel(sPoint, uv, 0).x;
  float ao = inAO.SampleLevel(sPoint, uv, 0).x;
  if (d0 < 1e-6) return float4(1, 0, 0, 0);
  float z = LinZ(uv);
  float W, H; inAO.GetDimensions(W, H);
  float2 tx = 1.0 / float2(W, H);
  // rango de los vecinos (cruz de 5)
  float mn = ao, mx = ao;
  [unroll] for (int k = 0; k < 4; k++) {
    float2 o = k == 0 ? float2(tx.x, 0) : (k == 1 ? float2(-tx.x, 0) : (k == 2 ? float2(0, tx.y) : float2(0, -tx.y)));
    float a = inAO.SampleLevel(sPoint, uv + o, 0).x; mn = min(mn, a); mx = max(mx, a);
  }
  float2 sC = uv * float2(2, -2) + float2(-1, 1);
  float3 Pv = float3((sC - c[28].xy) * c[21].xy * z, z);
  float3 pW = c[20].xyz + float3(dot(Pv, float3(c[8].x, c[9].x, c[10].x)), dot(Pv, float3(c[8].y, c[9].y, c[10].y)), dot(Pv, float3(c[8].z, c[9].z, c[10].z)));
  float4 hp = float4(pW, 1.0);
  float cw = dot(q[3], hp);
  float res = ao;
  if (cw > 0.05 && q[21].z != 0.0) {
    float2 uvP = (float2(dot(q[0], hp), dot(q[1], hp)) / cw + q[28].xy) * float2(0.5, -0.5) + 0.5;
    if (all(uvP > tx) && all(uvP < 1.0 - tx)) {
      float2 h = inHist.SampleLevel(sLinear, uvP, 0).xy;
      float hz = inHist.SampleLevel(sPoint, uvP, 0).y;
      if (hz > 0.0 && abs(hz - cw) < 0.03 * cw + 0.02) {
        float hc = clamp(h.x, mn - 0.05, mx + 0.05);
        res = lerp(hc, ao, 0.15);       // ~7 cuadros de memoria
      }
    }
  }
  return float4(res, z, 0, 0);
}
