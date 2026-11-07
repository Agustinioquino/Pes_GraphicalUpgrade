// PES21 FX - pasto v3 (2/11): PS de la PREPASADA DE PROFUNDIDAD de las hojas del mod (w325). La DLL dibuja primero el nivel cercano
// con este PS (solo profundidad: mismo recorte por alfa que 18afff0f79d9aaa5, sin salidas de color) y despues con el PS completo: cada
// pixel se sombrea una sola vez (la hoja de adelante) en vez de hasta 6-10 capas. Debe dar EXACTAMENTE el mismo alfa que 18afff: si se
// cambia el recorte alla, cambiarlo aca.
SamplerState g_samplerLinear_Wrap_s : register(s10);
Texture2D<float4> g_tex_grassDiffuse : register(t7);
Texture2D<float4> PESGrassAtlas : register(t8);
Texture1D<float4> IniParams : register(t120);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  float2 v7 : TEXCOORD6,
  float4 v8 : TEXCOORD7)
{
  if (v2.y < 0) discard;
  float4 Q4 = IniParams.Load(int2(104,0));
  bool pesV3 = Q4.x > 0.5 && v8.w > 0.5;
  bool pesFB = pesV3 && v8.w > 8.5;
  float4 Q5 = IniParams.Load(int2(105,0));
  float2 uvA = v1.xy;
  if (pesV3) {
    if (Q5.w > 0.5) uvA.y = 1.0 - uvA.y;
    float band = clamp(floor(v8.w - (pesFB ? 8.0 : 0.0) - 0.5), 0.0, 3.0);
    uvA.x = (saturate(v1.x) + band) * 0.25;
  }
  float2 dUx = ddx(uvA), dUy = ddy(uvA);
  float a;
  if (pesV3) {
    float shp = lerp(1.0, 0.5, smoothstep(600.0, 1500.0, abs(v4.w)));
    a = PESGrassAtlas.SampleGrad(g_samplerLinear_Wrap_s, uvA, dUx * shp, dUy * shp).w;
    float2 dd = max(abs(dUx), abs(dUy)) * float2(2048.0, 512.0);
    float lod = log2(max(max(dd.x, dd.y), 1.0));
    a = saturate(a * (1.0 + 0.12 * lod));
  } else {
    a = g_tex_grassDiffuse.SampleGrad(g_samplerLinear_Wrap_s, v1.xy, dUx, dUy).w;
  }
  if (a - 0.300000012 < 0) discard;
}
