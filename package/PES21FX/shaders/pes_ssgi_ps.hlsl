// PES21 FX - rebote de luz en pantalla (SSGI) temporal, paso 1 (21/10): a media resolucion, antes de la composicion.
// Para cada pixel se toman 8 muestras de la imagen iluminada (albedo x luz difusa) en un disco de radio fijo en metros,
// con un patron que gira cada cuadro; el paso 2 (pes_ssgi_tp_ps.hlsl) las acumula en el tiempo reproyectando con la camara
// del cuadro anterior: 8 muestras por cuadro rinden como ~100 y el radio puede ser mayor sin ruido.
//   t0 profundidad, t1 normal del G-buffer, t2 albedo, t3 luz difusa; b2 escena (registros crudos); t120 parametros.
//   Salida RGBA16F: rgb = luz indirecta (misma escala que el SSGI de la composicion antes de la fuerza w9), a = profundidad lineal.
//   153: x on | y radio extra (x el radio w10) | z cuadros de historia | w fuerza propia del acumulado (0 = usa w9)
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

// (27/10) v2 (x315 = 1): estimador fisico del rebote. Cada muestra representa un trozo de superficie del mundo (el area del
// disco en pantalla repartida entre las muestras, pasada a metros con la profundidad de la muestra y su inclinacion respecto de la
// vista) y aporta L x cos x cos x area / (pi d^2), como un factor de forma: un plano brillante que llena medio cielo da el 100 %
// de su luz, nunca mas. La v1 pesaba las muestras con una caida fija y rendia entre 3 y 10 veces menos (por eso casi no se veia).
// Ademas: 8 muestras cerca (radio R) + 4 lejos (anillo R..y315 x R) para el rebote largo de la tribuna y la cancha iluminadas,
// prueba de visibilidad (2 puntos a lo largo del rayo: lo que tapa un jugador no le llega al de atras), sin cielo, y la cancha
// (normal arriba, altura < 0.3 m) queda fuera si esta prendido "Rebote de luz del pasto" (w42), que ya la pone con el verde
// medido del estadio y la sombra del sol del mundo (asi no se cuenta dos veces).
//   315: x v2 on | y alcance lejano (x R, 0 = sin anillo lejano; 4) | z fuerza del anillo lejano (1) | w -
float2 UVofV(float3 P) {
  float2 sC = P.xy / (max(P.z, 1e-3) * c[21].xy) + c[28].xy;
  return float2(sC.x * 0.5 + 0.5, 0.5 - sC.y * 0.5);
}
float3 ToWorldDir(float3 v) { return float3(dot(v, float3(c[8].x, c[9].x, c[10].x)), dot(v, float3(c[8].y, c[9].y, c[10].y)), dot(v, float3(c[8].z, c[9].z, c[10].z))); }

float3 SampleV2(float2 uv, float2 uvS, float3 Pc, float3 Nc, float areaPx, bool skipTurf) {
  if (any(uvS < 0.0) || any(uvS > 1.0)) return 0.0;
  float dS = inDepth.SampleLevel(sPoint, uvS, 0).x;
  if (dS < 1e-6) return 0.0;                                        // cielo: lo pone la luz de cielo (x47)
  float zS = LinZ(uvS);
  if (zS < 0.1) return 0.0;
  float3 Ps = PosV(uvS, zS);
  float3 dv = Ps - Pc; float dist2 = dot(dv, dv); float dist = sqrt(dist2) + 1e-4; float3 dir = dv / dist;
  float cosC = saturate(dot(Nc, dir));
  if (cosC <= 0.0) return 0.0;
  float3 Ns = DecN(uvS);
  float cosS = saturate(dot(Ns, -dir));
  if (cosS <= 0.0) return 0.0;
  if (skipTurf) {
    float3 nW = ToWorldDir(Ns);
    float hW = c[20].y + ToWorldDir(Ps).y;
    if (nW.y > 0.85 && hW < 0.3) return 0.0;                        // la cancha: la pone w42
  }
  // visibilidad: dos puntos del segmento, proyectados; si la escena esta claramente delante, el rebote esta tapado
  [unroll] for (int k = 1; k <= 2; k++) {
    float3 Pm = lerp(Pc, Ps, k / 3.0);
    float2 uvM = UVofV(Pm);
    if (inDepth.SampleLevel(sPoint, uvM, 0).x < 1e-6) continue;     // cielo: no tapa
    float zM = LinZ(uvM);
    if (zM > 0.05 && zM < Pm.z - (0.15 + 0.03 * Pm.z)) return 0.0;
  }
  // area en metros que representa la muestra: pixeles x (metros por pixel a esa profundidad)^2 / inclinacion respecto de la vista
  float mpp = 2.0 * abs(c[21].x) * zS * m_renderBuffer.z;          // metros por pixel (resolucion completa) a la profundidad zS
  float cosV = max(saturate(dot(Ns, -normalize(Ps))), 0.25);
  float area = areaPx * mpp * mpp / cosV;
  float ff = cosC * cosS * area / (3.14159265 * max(dist2, 0.04));
  float3 Ls = inAlb.SampleLevel(sPoint, uvS, 0).xyz * inDif.SampleLevel(sPoint, uvS, 0).xyz;
  return Ls * min(ff, 0.35);                                        // una sola muestra nunca aporta mas de un tercio
}

float4 mainV2(float4 pos, float2 uv, float z0, float Rg, float fr0, float rot, float pxm) {
  float4 P315 = IniParams.Load(int2(315, 0)), P42 = IniParams.Load(int2(42, 0));
  float3 Pc = PosV(uv, z0);
  float3 Nc = DecN(uv);
  bool skipTurf = P42.w > 0.5;
  float rPx = min(Rg * pxm, 320.0);
  float3 acc = 0;
  if (rPx >= 3.0) {
    float aNear = 3.14159265 * rPx * rPx / 8.0;
    [unroll] for (int i = 0; i < 8; i++) {
      float fr = sqrt(frac((i + 0.5) / 8.0 + fr0 * 0.37));
      float ang = i * 2.39996323 + rot;
      acc += SampleV2(uv, uv + float2(cos(ang), sin(ang)) * fr * rPx * m_renderBuffer.zw, Pc, Nc, aNear, skipTurf);
    }
  }
  float kFar = P315.y > 0 ? P315.y : 0.0;
  if (kFar > 1.01) {
    float rIn = Rg * pxm, rOut = min(Rg * kFar * pxm, 640.0);
    if (rOut > rIn + 4.0) {
      float aFar = 3.14159265 * (rOut * rOut - rIn * rIn) / 4.0;
      float3 accF = 0;
      [unroll] for (int j = 0; j < 4; j++) {
        float u = frac((j + 0.5) / 4.0 + fr0 * 0.61);
        float rr = sqrt(rIn * rIn + u * (rOut * rOut - rIn * rIn));
        float ang = j * 1.5707963 + rot * 1.37 + 0.4;
        accF += SampleV2(uv, uv + float2(cos(ang), sin(ang)) * rr * m_renderBuffer.zw, Pc, Nc, aFar, skipTurf);
      }
      acc += accF * (P315.z > 0 ? P315.z : 1.0);
    }
  }
  return float4(acc, z0);
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
  if (rPx < 3.0 && IniParams.Load(int2(315, 0)).x <= 0.5) return float4(0, 0, 0, z0);
  float fr0 = frac(P155.y * 0.618034);
  float rot = 6.2831853 * frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715))) + fr0);
  if (IniParams.Load(int2(315, 0)).x > 0.5) return mainV2(pos, uv, z0, Rg, fr0, rot, pxm);
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
