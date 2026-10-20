// PES21 FX - dispersion subsuperficial en pantalla, SEPARABLE (Jimenez et al., "Separable Subsurface Scattering" / tesis 2012),
// con el perfil de piel de d'Eon (GPU Gems 3 cap. 14: suma de 6 gaussianas por canal, en mm). Una pasada horizontal y una
// vertical sobre la irradiancia difusa (lo que la pasada de luz escribio), SOLO en pixeles marcados como piel en el G-buffer
// (material.z = 250/255). El ancho en pantalla sale de la profundidad real (mm -> pixeles), asi que el borde de la sombra
// se difunde lo que se difundiria en una cara real, de cerca o de lejos.
//   t0 irradiancia (entrada), t1 profundidad, t2 G-buffer material, t3 G-buffer normal, t120 parametros
//   x95 ancho (1 = escala real en mm), y95 fuerza, z95 corte por profundidad, w95 on
cbuffer cPSScene : register(b2) {
  float4x4 m_projectionView; float4x4 m_projection; float4x4 m_view; float4x4 m_shadowProjection; float4x4 m_shadowProjection2;
  float4 m_eyepos; float4 m_projectionParam; float4 m_viewportSize; float4 m_exposure; float4 m_fogParam[3]; float4 m_fogColor;
  float4 m_cameraCenterOffset; float4 m_shadowMapResolutions;
};
cbuffer cPSSystem : register(b0) { float4 m_param; float4 m_renderInfo; float4 m_renderBuffer; float4 m_dominantLightDir; float4 m_timeParam; };
Texture2D<float4> inLight : register(t0);
Texture2D<float4> inDepth : register(t1);
Texture2D<float4> inMat   : register(t2);
Texture2D<float4> inNrm   : register(t3);
Texture2D<float4> inAlb   : register(t4);   // (20/10) albedo del G-buffer: tono de la piel (melanina)
Texture1D<float4> IniParams : register(t120);
SamplerState sPoint : register(s0);
SamplerState sLinear : register(s1);

static const float4 kSSSS[17] = {
  float4(0.00189, 0.00000, 0.00000, -6.0000),
  float4(0.00439, 0.00001, 0.00000, -4.5938),
  float4(0.01267, 0.00008, 0.00000, -3.3750),
  float4(0.03122, 0.00030, 0.00004, -2.3438),
  float4(0.05553, 0.00153, 0.00038, -1.5000),
  float4(0.07867, 0.01765, 0.00205, -0.8438),
  float4(0.09929, 0.09224, 0.05093, -0.3750),
  float4(0.14337, 0.25102, 0.27470, -0.0938),
  float4(0.14591, 0.27437, 0.34380, 0.0000),
  float4(0.14337, 0.25102, 0.27470, 0.0938),
  float4(0.09929, 0.09224, 0.05093, 0.3750),
  float4(0.07867, 0.01765, 0.00205, 0.8438),
  float4(0.05553, 0.00153, 0.00038, 1.5000),
  float4(0.03122, 0.00030, 0.00004, 2.3438),
  float4(0.01267, 0.00008, 0.00000, 3.3750),
  float4(0.00439, 0.00001, 0.00000, 4.5938),
  float4(0.00189, 0.00000, 0.00000, 6.0000),
};

float LinZ(float2 uv) { return m_projectionParam.z / (inDepth.SampleLevel(sPoint, uv, 0).x - m_projectionParam.w); }
float3 DecN(float2 uv) {   // octaedrica con bits bajos en w (misma que PESDecN de la composicion)
  float4 nb = inNrm.SampleLevel(sPoint, uv, 0);
  uint3 q = (uint3)(float3(1023,1023,3) * nb.xyw);
  float2 e = float2((q.x << 1) | (q.z >> 1), (q.y << 1) | (q.z & 1)) * 0.00097703957 - 1;
  float3 n = float3(e.xy, 1 - abs(e.x) - abs(e.y));
  if (n.z < 0) n.xy = (1 - abs(e.yx)) * (e.xy >= 0 ? 1 : -1);
  return normalize(n);
}
bool IsSkin(float2 uv) { return abs(inMat.SampleLevel(sPoint, uv, 0).z - 0.98039) < 0.003; }

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float4 P = IniParams.Load(int2(95,0));
  float3 c0 = inLight.SampleLevel(sPoint, uv, 0).xyz;
  if (P.w < 0.5 || !IsSkin(uv)) return float4(c0, 1);
  float z0 = LinZ(uv);
  float3 n0 = DecN(uv);
  // pixeles por metro a esta profundidad (m_projectionParam.x = 1/(2*proj[0][0]) aprox, igual que en la composicion)
  // tamano de un pixel en uv: del propio triangulo (exacto, no depende de como venga m_viewportSize)
  float2 rb = abs(float2(ddx(uv.x), ddy(uv.y)));
  float pxm = 1.0 / max(2.0 * abs(m_projectionParam.x) * z0 * m_renderBuffer.z, 1e-5);   // misma formula que la composicion
  // diagnostico: x95 >= 2.95 pinta la piel: verde = pasa por aca, con el ancho en pixeles como brillo
  if (P.x >= 2.95) return float4(0, saturate(6.0 * 0.001 * pxm / 40.0), 0, 1);
  float widthScale = (P.x > 0 ? P.x : 1.0);
  float mmToPx = 0.001 * pxm * widthScale;
  float rMaxPx = 6.0 * mmToPx;
  if (rMaxPx < 0.6) return float4(c0, 1);
  float2 dir = SSSS_DIR * rb;
  // x100 = sangrado rojo extra: el canal rojo se difunde mas lejos (muestra propia, mas ancha); w100 = tinte rojizo de la penumbra
  float4 P2 = IniParams.Load(int2(100,0));
  // (20/10, 140) SSS segun el tono de piel: con mas melanina la epidermis absorbe mas y el rojo que viaja por la dermis
  // se ve mucho menos (la penumbra de una piel oscura es marron calida, no roja): menos sangrado rojo, menos tinte y algo
  // menos fuerza; en piel clara queda como antes. z140 = cuanto manda el tono (0 = igual para todos)
  float4 SK = IniParams.Load(int2(140,0));
  float alb0L = dot(inAlb.SampleLevel(sPoint, uv, 0).xyz, float3(0.2126, 0.7152, 0.0722));
  float mel = saturate(log2(0.47 / max(alb0L, 1e-3)) / 2.85);
  float km = SK.x > 0.5 ? mel * saturate(SK.z > 0.0 ? SK.z : 1.0) : 0.0;
  float redW = 1.0 + 1.5 * saturate(P2.x) * (1.0 - 0.7 * km);
  float3 acc = 0, wsum = 0;
  [unroll] for (int i = 0; i < 17; i++) {
    float4 k = kSSSS[i];
    float2 uvS = uv + dir * (k.w * mmToPx);
    float2 uvR = uv + dir * (k.w * mmToPx * redW);
    float3 w = k.xyz;
    if (i != 8) {
      bool sk = IsSkin(uvS);
      float zS = LinZ(uvS);
      float dzmm = abs(zS - z0) * 1000.0;
      float depthW = exp(-dzmm * dzmm * P.z * 0.25);           // mas de ~2 mm de salto en profundidad: casi no mezcla
      float nd = saturate(dot(n0, DecN(uvS))); nd *= nd;
      w *= (sk ? 1.0 : 0.0) * depthW * nd;
      if (redW > 1.01) { bool skR = IsSkin(uvR); float dzR = abs(LinZ(uvR) - z0) * 1000.0; w.r *= (skR ? 1.0 : 0.0) * exp(-dzR * dzR * P.z * 0.25) / max((sk ? 1.0 : 0.0) * depthW, 1e-3); }
    }
    float3 cS = inLight.SampleLevel(sLinear, uvS, 0).xyz;
    if (redW > 1.01) cS.r = inLight.SampleLevel(sLinear, uvR, 0).r;
    acc += w * cS;
    wsum += w;
  }
  float3 blurred = acc / max(wsum, 1e-4);
  // tinte de la penumbra: donde la difusion aclara respecto al pixel (borde de sombra), el rojo gana un poco
  float3 res = lerp(c0, blurred, saturate(P.y) * (1.0 - 0.3 * km));
  float pen = saturate((dot(blurred, 1.0) - dot(c0, 1.0)) * 2.0);
  res.r *= 1.0 + 0.25 * pen * saturate(P2.w) * (1.0 - 0.75 * km);
  res.gb *= 1.0 - 0.08 * pen * saturate(P2.w) * (1.0 - 0.75 * km);
  return float4(res, 1);
}
