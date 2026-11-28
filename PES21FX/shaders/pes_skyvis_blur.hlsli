// PES21 FX - visibilidad del cielo de lejos (fila 534): desenfoque bilateral (por profundidad) a media resolucion del ruido 4x4.
// t0 visibilidad RGBA8 (media), t1 profundidad (completa); b2 escena. BLUR_DIR = (1,0) horizontal / (0,1) vertical.
cbuffer cPSScene : register(b2) {
  float4x4 m_projectionView; float4x4 m_projection; float4x4 m_view; float4x4 m_shadowProjection; float4x4 m_shadowProjection2;
  float4 m_eyepos; float4 m_projectionParam; float4 m_viewportSize; float4 m_exposure; float4 m_fogParam[3]; float4 m_fogColor;
  float4 m_cameraCenterOffset; float4 m_shadowMapResolutions;
};
Texture2D<float4> inVis : register(t0);
Texture2D<float4> inDepth : register(t1);
SamplerState sPoint : register(s0);
SamplerState sLinear : register(s1);
float LinZ(float2 uv) { float d = inDepth.SampleLevel(sPoint, uv, 0).x; return d < 1e-6 ? 1e6 : m_projectionParam.z / (d - m_projectionParam.w); }
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float W, H; inVis.GetDimensions(W, H);
  float2 px = 1.0 / float2(W, H);
  float z0 = LinZ(uv);
  float4 acc = 0; float wsum = 0;
  [unroll] for (int i = -3; i <= 3; i++) {
    float2 uvS = uv + BLUR_DIR * (float)i * px;
    float zS = LinZ(uvS);
    float w = exp(-0.5 * (float)(i * i) / 5.0) * exp(-abs(zS - z0) / max(z0 * 0.03, 0.03));
    acc += inVis.SampleLevel(sPoint, uvS, 0) * w; wsum += w;
  }
  return acc / max(wsum, 1e-4);
}
