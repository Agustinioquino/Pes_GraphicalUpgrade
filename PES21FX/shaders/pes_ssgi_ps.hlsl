// PES21 FX - rebote de luz en pantalla (SSGI) temporal, paso 1 (21/10): a media resolucion, antes de la composicion.
// Para cada pixel se toman 8 muestras de la imagen iluminada (albedo x luz difusa) en un disco de radio fijo en metros,
// con un patron que gira cada cuadro; el paso 2 (pes_ssgi_tp_ps.hlsl) las acumula en el tiempo reproyectando con la camara
// del cuadro anterior: 8 muestras por cuadro rinden como ~100 y el radio puede ser mayor sin ruido.
//   t0 profundidad, t1 normal del G-buffer, t2 albedo, t3 luz difusa; b2 escena (registros crudos); t120 parametros.
//   Salida RGBA16F: rgb = luz indirecta (misma escala que el SSGI de la composicion antes de la fuerza w9), a = profundidad lineal.
//   153: x on | y radio extra (x el radio w10) | z cuadros de historia | w -
cbuffer cScene : register(b2) { float4 c[30]; };   // 0-3 proj*vista, 8-10 vista, 20 ojo, 21 projectionParam, 28 cameraCenterOffset
cbuffer cPSSystem : register(b0) { float4 m_param; float4 m_renderInfo; float4 m_renderBuffer; float4 m_dominantLightDir; float4 m_timeParam; };
Texture2D<float4> inDepth : register(t0);
Texture2D<float4> inNrm   : register(t1);
Texture2D<float4> inAlb   : register(t2);
Texture2D<float4> inDif   : register(t3);
Texture1D<float4> IniParams : register(t120);
SamplerState sPoint : register(s0);
SamplerState sLinear : register(s1);

float LinZ(float2 uv) { return c[21].z / (inDepth.SampleLevel(sPoint, uv, 0).x - c[21].w); }
float3 PosV(float2 uv, float z) {
  float2 sC = uv * float2(2, -2) + float2(-1, 1);
  return float3((sC - c[28].xy) * c[21].xy * z, z);
}
float3 DecN(float2 uv) {
  float4 nb = inNrm.SampleLevel(sPoint, uv, 0);
  uint3 q = (uint3)(float3(1023, 1023, 3) * nb.xyw);
  float2 e = float2((q.x << 1) | (q.z >> 1), (q.y << 1) | (q.z & 1)) * 0.00097703957 - 1;
  float3 n = float3(e.xy, 1 - abs(e.x) - abs(e.y));
  if (n.z < 0) n.xy = (1 - abs(e.yx)) * (e.xy >= 0 ? 1 : -1);
  n = normalize(n); n.z = -n.z;
  return n;
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float d0 = inDepth.SampleLevel(sPoint, uv, 0).x;
  if (d0 < 1e-6) return 0.0;                                    // cielo
  float z0 = LinZ(uv);
  if (z0 < 0.1 || z0 > 90.0) return float4(0, 0, 0, max(z0, 0.0));
  float4 P10 = IniParams.Load(int2(10, 0)), P153 = IniParams.Load(int2(153, 0)), P155 = IniParams.Load(int2(155, 0));
  float Rg = (P10.w > 0 ? P10.w : 1.6) * (P153.y > 0 ? P153.y : 1.5);
  float3 Pc = PosV(uv, z0);
  float3 Nc = DecN(uv);
  float pxm = 1.0 / max(2.0 * abs(c[21].x) * z0 * m_renderBuffer.z, 1e-5);   // pixeles (resolucion completa) por metro
  float rPx = min(Rg * pxm, 320.0);
  if (rPx < 3.0) return float4(0, 0, 0, z0);
  float fr0 = frac(P155.y * 0.618034);
  float rot = 6.2831853 * frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715))) + fr0);
  float3 acc = 0;
  [unroll] for (int i = 0; i < 8; i++) {
    float fr = 0.08 + 0.92 * sqrt(frac((i + 0.5) / 8.0 + fr0 * 0.37));
    float ang = i * 2.39996323 + rot;
    float2 uvS = uv + float2(cos(ang), sin(ang)) * fr * rPx * m_renderBuffer.zw;
    if (any(uvS < 0.0) || any(uvS > 1.0)) continue;
    float zS = LinZ(uvS);
    float3 Ps = PosV(uvS, zS);
    float3 dv = Ps - Pc; float dist = length(dv) + 1e-4; float3 dir = dv / dist;
    float cosC = saturate(dot(Nc, dir));
    float cosS = saturate(dot(DecN(uvS), -dir));
    float fall = 1.0 / (1.0 + 4.0 * (dist / Rg) * (dist / Rg));
    float3 Ls = inAlb.SampleLevel(sPoint, uvS, 0).xyz * inDif.SampleLevel(sPoint, uvS, 0).xyz;
    acc += Ls * (cosC * cosS * fall);
  }
  return float4(acc * (2.2 / 8.0), z0);
}
