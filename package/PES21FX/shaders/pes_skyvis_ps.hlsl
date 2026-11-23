// PES21 FX - VISIBILIDAD DEL CIELO DE LEJOS (23/11 ru34vc, fila 534). El GTAO (pes_gtao_ps) mira 60 cm alrededor de cada punto:
// ve los pliegues, pero no el techo de la tribuna, la grada de arriba que tapa a la de abajo, el fondo del banco de suplentes o
// el tunel. Por eso el publico bajo techo y los suplentes recibian el cielo entero, como si estuvieran al aire libre.
// Este pase usa la misma integral de horizontes (Jimenez 2016 / XeGTAO) con un radio de metros (y534, 6 m), a MEDIA resolucion,
// y ademas devuelve la NORMAL DOBLADA: la direccion promedio por donde ese punto todavia ve el cielo (con ella el cielo como luz,
// fila 525, toma el color del pedazo de cielo que de verdad se ve). Lo fino (gente delante de la grada, una hoja de pasto, un
// brazo) no cuenta como muro: lo que esta mucho mas cerca de la camara que el punto tapa solo dentro de un grosor.
//   t0 profundidad (resolucion completa), t1 normal del G-buffer, t120 parametros; b2 escena, b0 sistema.
//   Salida RGBA8: rgb = normal doblada en espacio de vista * 0.5 + 0.5 (convencion de PosV, z hacia adelante), a = visibilidad (1 = cielo entero).
//   El pasto (tipo 3) y el cielo salen con 1 (no se tocan).
cbuffer cPSScene : register(b2) {
  float4x4 m_projectionView; float4x4 m_projection; float4x4 m_view; float4x4 m_shadowProjection; float4x4 m_shadowProjection2;
  float4 m_eyepos; float4 m_projectionParam; float4 m_viewportSize; float4 m_exposure; float4 m_fogParam[3]; float4 m_fogColor;
  float4 m_cameraCenterOffset; float4 m_shadowMapResolutions;
};
cbuffer cPSSystem : register(b0) { float4 m_param; float4 m_renderInfo; float4 m_renderBuffer; float4 m_dominantLightDir; float4 m_timeParam; };
Texture2D<float4> inDepth : register(t0);
Texture2D<float4> inNrm   : register(t1);
Texture1D<float4> IniParams : register(t120);
SamplerState sPoint : register(s0);
SamplerState sLinear : register(s1);

float LinZ(float2 uv) { return m_projectionParam.z / (inDepth.SampleLevel(sPoint, uv, 0).x - m_projectionParam.w); }
float3 PosV(float2 uv, float z) {
  float2 sC = uv * float2(2, -2) + float2(-1, 1);
  return float3((sC - m_cameraCenterOffset.xy) * m_projectionParam.xy * z, z);
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
static const float4 kNone = float4(0.5, 0.5, 0.5, 1.0);

float4 main(float4 pos : SV_Position, float2 uvH : TEXCOORD0) : SV_Target0 {
  float4 P = IniParams.Load(int2(534, 0));
  // uv del centro del texel de media resolucion = uv de la profundidad completa (la VS da 0..1 sobre la vista chica)
  float2 uv = uvH;
  float d0 = inDepth.SampleLevel(sPoint, uv, 0).x;
  if (d0 < 1e-6) return kNone;
  float z0 = LinZ(uv);
  if (z0 < 0.05 || z0 > 400.0) return kNone;
  uint ty = ((uint)(inNrm.SampleLevel(sPoint, uv, 0).z * 1023.0 + 0.5)) & 15u;
  if (ty == 3u) return kNone;   // el pasto queda como esta (lo maneja su hilo)
  float3 Pc = PosV(uv, z0);
  float3 V = -normalize(Pc);
  // normal geometrica de la profundidad (como el GTAO: sin el relieve de los mapas de normales), si no la del G-buffer
  float3 N = DecN(uv);
  { float2 pxn = m_renderBuffer.zw;
    float zL = LinZ(uv - float2(pxn.x, 0)), zR = LinZ(uv + float2(pxn.x, 0)), zU = LinZ(uv - float2(0, pxn.y)), zD = LinZ(uv + float2(0, pxn.y));
    float3 dx = abs(zR - z0) < abs(z0 - zL) ? PosV(uv + float2(pxn.x, 0), zR) - Pc : Pc - PosV(uv - float2(pxn.x, 0), zL);
    float3 dy = abs(zD - z0) < abs(z0 - zU) ? PosV(uv + float2(0, pxn.y), zD) - Pc : Pc - PosV(uv - float2(0, pxn.y), zU);
    float3 Ng = cross(dx, dy);
    float tolN = 0.05 * z0;
    if (dot(Ng, Ng) > 1e-14 && min(abs(zR - z0), abs(z0 - zL)) < tolN && min(abs(zD - z0), abs(z0 - zU)) < tolN) {
      Ng = normalize(Ng); if (dot(Ng, V) < 0) Ng = -Ng; N = Ng; } }
  float radiusM = clamp(P.y > 0.5 ? P.y : 6.0, 1.0, 20.0);
  float pxm = 1.0 / max(2.0 * abs(m_projectionParam.x) * z0 * m_renderBuffer.z, 1e-5);   // pixeles (completos) por metro
  float rPx = clamp(radiusM * pxm, 6.0, 0.35 * m_renderInfo.y);
  if (rPx < 6.5 && radiusM * pxm < 3.0) return kNone;   // el radio entra en menos de 3 pixeles: lo resuelve el GTAO comun
  float2 px = m_renderBuffer.zw;
  // grosor de lo fino: lo que esta delante del punto mas que esto tapa cada vez menos (una persona, un brazo, un poste)
  float thick = 0.9 + 0.01 * z0;
  float depthEps = max(0.01, z0 * 0.002);
  // ruido 4x4 por texel de media resolucion (el desenfoque y el TAA lo limpian); fijo en el tiempo para que no titile
  float2 ip = floor(pos.xy);
  float noiseDir = frac(52.9829189 * frac(dot(ip, float2(0.06711056, 0.00583715))));
  float noiseOff = frac(dot(ip, float2(0.25, 0.5)));
  const int kSlices = 4, kSteps = 8;
  float vis = 0.0;
  float3 bent = 0;
  [unroll] for (int s = 0; s < kSlices; s++) {
    float phi = (s + noiseDir) * (3.14159265 / kSlices);
    float2 dir = float2(cos(phi), sin(phi));
    float3 dirV = normalize(float3(dir.x * m_projectionParam.x, -dir.y * m_projectionParam.y, 0));
    float3 orthoDir = dirV - dot(dirV, V) * V;
    float3 oN = normalize(orthoDir);
    float3 axis = normalize(cross(orthoDir, V));
    float3 projN = N - axis * dot(N, axis);
    float projLen = length(projN);
    float cosN = clamp(dot(projN, V) / max(projLen, 1e-4), -1, 1);
    float n = sign(dot(orthoDir, projN)) * acos(cosN);
    float2 h = float2(-1, -1);
    [unroll] for (int side = 0; side < 2; side++) {
      float sgn = side == 0 ? 1.0 : -1.0;
      float cosH = -1.0;
      [loop] for (int st = 0; st < kSteps; st++) {
        float t = (st + 0.25 + noiseOff * 0.75) / kSteps; t = t * sqrt(t);   // un poco mas de muestras cerca
        float2 uvS = uv + sgn * dir * (t * rPx) * px;
        if (any(uvS < 0.0) || any(uvS > 1.0)) break;
        float dS0 = inDepth.SampleLevel(sPoint, uvS, 0).x;
        if (dS0 < 1e-6) continue;   // cielo: no tapa
        float zS = m_projectionParam.z / (dS0 - m_projectionParam.w);
        float3 dS = PosV(uvS, zS) - Pc; float lenS = length(dS);
        if (lenS < 1e-3) continue;
        if (dot(dS, N) < depthEps) continue;
        float cosS = dot(dS, V) / lenS;
        float w = saturate(1.0 - (lenS / radiusM) * (lenS / radiusM));
        // lo fino delante (mas cerca de la camara que el punto mas alla del grosor) tapa menos
        float front = z0 - zS;
        w *= saturate(1.0 - (front - thick) / max(thick, 0.1));
        cosH = max(cosH, lerp(-1.0, cosS, w));
      }
      h[side] = cosH;
    }
    float h0 = -acos(h.y), h1 = acos(h.x);
    h0 = n + max(h0 - n, -1.5707963); h1 = n + min(h1 - n, 1.5707963);
    float a = 0.25 * (-cos(2.0 * h0 - n) + cos(n) + 2.0 * h0 * sin(n)) + 0.25 * (-cos(2.0 * h1 - n) + cos(n) + 2.0 * h1 * sin(n));
    vis += projLen * a;
    // normal doblada de esta rebanada: la mitad del arco libre entre los dos horizontes, en el plano de la rebanada
    float b = 0.5 * (h0 + h1);
    bent += (V * cos(b) + oN * sin(b)) * projLen * max(a, 0.02);
  }
  vis = saturate(vis / kSlices);
  float3 Bn = dot(bent, bent) > 1e-8 ? normalize(bent) : N;
  // la normal doblada no puede quedar de espaldas a la superficie
  if (dot(Bn, N) < 0.05) Bn = normalize(Bn + N * (0.05 - dot(Bn, N) + 0.05));
  return float4(Bn * 0.5 + 0.5, vis);
}
