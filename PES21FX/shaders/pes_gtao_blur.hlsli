// PES21 FX - GTAO: desenfoque bilateral 4x4 (por profundidad) del ruido por pixel; el TAA del juego termina de limpiarlo.
// t0 oclusion cruda (R8), t1 profundidad; b2 escena. BLUR_DIR = (1,0) horizontal / (0,1) vertical.
cbuffer cPSScene : register(b2) {
  float4x4 m_projectionView; float4x4 m_projection; float4x4 m_view; float4x4 m_shadowProjection; float4x4 m_shadowProjection2;
  float4 m_eyepos; float4 m_projectionParam; float4 m_viewportSize; float4 m_exposure; float4 m_fogParam[3]; float4 m_fogColor;
  float4 m_cameraCenterOffset; float4 m_shadowMapResolutions;
};
cbuffer cPSSystem : register(b0) { float4 m_param; float4 m_renderInfo; float4 m_renderBuffer; float4 m_dominantLightDir; float4 m_timeParam; };
Texture2D<float4> inAO : register(t0);
Texture2D<float4> inDepth : register(t1);
SamplerState sPoint : register(s0);
SamplerState sLinear : register(s1);
float LinZ(float2 uv) { return m_projectionParam.z / (inDepth.SampleLevel(sPoint, uv, 0).x - m_projectionParam.w); }
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float z0 = LinZ(uv); float2 px = m_renderBuffer.zw;
  float acc = 0, wsum = 0;
  [unroll] for (int i = -3; i <= 3; i++) {
    float2 uvS = uv + BLUR_DIR * (float)i * px;
    float zS = LinZ(uvS);
    float w = exp(-0.5 * (float)(i * i) / 4.0) * exp(-abs(zS - z0) / max(z0 * 0.02, 0.01));
    acc += inAO.SampleLevel(sPoint, uvS, 0).x * w; wsum += w;
  }
  return acc / max(wsum, 1e-4);
}
