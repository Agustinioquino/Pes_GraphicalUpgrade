// PES21 FX - vidrio de los menus (24/10): desenfoque del fondo, paso de subida (Kawase doble: 4 en cruz + 4 diagonales x2).
// t0 = nivel mas chico, s1 lineal. z189 = radio del desenfoque.
Texture2D<float4> src : register(t0);
SamplerState sLin : register(s1);
Texture1D<float4> IniParams : register(t120);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float w, h; src.GetDimensions(w, h);
  float o = clamp(IniParams.Load(int2(189, 0)).z, 0.25, 4.0);
  float2 d = o / float2(w, h);
  float4 s = src.SampleLevel(sLin, uv + float2(-d.x, 0), 0);
  s += src.SampleLevel(sLin, uv + float2( d.x, 0), 0);
  s += src.SampleLevel(sLin, uv + float2(0, -d.y), 0);
  s += src.SampleLevel(sLin, uv + float2(0,  d.y), 0);
  s += src.SampleLevel(sLin, uv + float2(-0.5 * d.x, -0.5 * d.y), 0) * 2.0;
  s += src.SampleLevel(sLin, uv + float2( 0.5 * d.x, -0.5 * d.y), 0) * 2.0;
  s += src.SampleLevel(sLin, uv + float2(-0.5 * d.x,  0.5 * d.y), 0) * 2.0;
  s += src.SampleLevel(sLin, uv + float2( 0.5 * d.x,  0.5 * d.y), 0) * 2.0;
  return float4(s.rgb / 12.0, 1.0);
}
