// PES21 FX - GTAO (20/10): oclusion ambiental por horizontes (Jimenez et al. 2016, "Practical Realtime Strategies for
// Accurate Indirect Occlusion"), el HBAO de los motores actuales. Para cada pixel se buscan, en varias direcciones de
// pantalla, los horizontes que tapan el hemisferio alrededor de la normal y se integra la visibilidad pesada por coseno:
// da una oclusion fisicamente correcta (ground truth del hemisferio) y mas fina que el SSAO del juego (480x270 y 8 bits):
// sombra de contacto bajo los pies, entre las piernas, en las axilas y en los pliegues del kit, sin halos.
//   t0 profundidad, t1 normal del G-buffer, t120 parametros; b2 escena. Salida R8: 1 = sin oclusion.
//   132: x on | y mezcla con el SSAO del juego (0 = solo el juego .. 1 = solo GTAO) | z radio (m) | w fuerza (potencia)
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
  n = normalize(n); n.z = -n.z;   // a la convencion de PosV (z hacia adelante)
  return n;
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float4 P = IniParams.Load(int2(132, 0));
  float d0 = inDepth.SampleLevel(sPoint, uv, 0).x;
  if (d0 < 1e-6) return 1.0;
  float z0 = LinZ(uv);
  if (z0 < 0.05 || z0 > 300.0) return 1.0;
  float3 Pc = PosV(uv, z0);
  float3 N = DecN(uv);
  float3 V = -normalize(Pc);
  // radio en pantalla: metros -> pixeles a esta profundidad
  float radiusM = P.z > 0.02 ? P.z : 0.6;
  float pxm = 1.0 / max(2.0 * abs(m_projectionParam.x) * z0 * m_renderBuffer.z, 1e-5);   // pixeles por metro
  float rPx = clamp(radiusM * pxm, 2.0, 160.0);
  float2 px = m_renderBuffer.zw;
  // ruido por pixel (4x4 interleaved) + rotacion por cuadro: el TAA del juego promedia
  float2 ip = pos.xy;
  float noiseDir = frac(52.9829189 * frac(dot(ip, float2(0.06711056, 0.00583715))));
  float noiseOff = frac(dot(ip, float2(0.25, 0.5)) + m_timeParam.z * 7.31 * 0.0);
  const int kSlices = 3, kSteps = 6;
  float vis = 0.0;
  [unroll] for (int s = 0; s < kSlices; s++) {
    float phi = (s + noiseDir) * (3.14159265 / kSlices);
    float2 dir = float2(cos(phi), sin(phi));
    // (21/10) direccion de la rebanada en espacio de vista: un paso en pantalla mueve la posicion en (du * 2 pp.x, -dv * 2 pp.y)
    // (el eje x de la vista del juego va hacia la izquierda de la pantalla: pp.x < 0). Antes se usaba (dir, 0) tal cual: la
    // rebanada quedaba girada 180 grados y los horizontes de cada lado se asignaban al reves -> en superficies inclinadas
    // (y con el relieve del mapa de normales) la oclusion salia con manchas oscuras en forma de curvas de nivel
    float3 dirV = normalize(float3(dir.x * m_projectionParam.x, -dir.y * m_projectionParam.y, 0));
    // plano de la rebanada: proyeccion de la normal (Jimenez 2016, sec. 3.3)
    float3 orthoDir = dirV - dot(dirV, V) * V;
    float3 axis = normalize(cross(orthoDir, V));
    float3 projN = N - axis * dot(N, axis);
    float projLen = length(projN);
    float cosN = clamp(dot(projN, V) / max(projLen, 1e-4), -1, 1);
    float n = sign(dot(orthoDir, projN)) * acos(cosN);   // XeGTAO
    float2 h = float2(-1, -1);   // cos de los horizontes a ambos lados
    [unroll] for (int side = 0; side < 2; side++) {
      float sgn = side == 0 ? 1.0 : -1.0;
      float cosH = -1.0;
      [unroll] for (int st = 0; st < kSteps; st++) {
        float t = (st + 0.3 + noiseOff * 0.7) / kSteps; t = t * t;      // mas muestras cerca
        float2 uvS = uv + sgn * dir * (t * rPx) * px;
        if (any(uvS < 0.0) || any(uvS > 1.0)) break;
        float zS = LinZ(uvS);
        float3 Ps = PosV(uvS, zS);
        float3 dS = Ps - Pc; float lenS = length(dS);
        if (lenS < 1e-4) continue;
        float cosS = dot(dS, V) / lenS;
        // atenuacion con la distancia (falloff): lo muy lejano en profundidad no tapa
        float w = saturate(1.0 - (lenS / (radiusM * 1.5)) * (lenS / (radiusM * 1.5)));
        cosS = lerp(-1.0, cosS, w);
        cosH = max(cosH, cosS);
      }
      h[side] = cosH;
    }
    float h0 = -acos(h.y), h1 = acos(h.x);   // h.x = horizonte hacia +dir, h.y = hacia -dir (XeGTAO: h1 = +dir)
    h0 = n + max(h0 - n, -1.5707963); h1 = n + min(h1 - n, 1.5707963);
    // integral analitica de la visibilidad pesada por coseno
    float a = 0.25 * (-cos(2.0 * h0 - n) + cos(n) + 2.0 * h0 * sin(n)) + 0.25 * (-cos(2.0 * h1 - n) + cos(n) + 2.0 * h1 * sin(n));
    vis += projLen * a;
  }
  vis = saturate(vis / kSlices);
  float strength = P.w > 0.05 ? P.w : 1.0;
  return pow(vis, strength);
}
