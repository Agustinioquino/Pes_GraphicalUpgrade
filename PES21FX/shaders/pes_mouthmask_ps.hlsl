// PES21 FX (04/11) - mascara del interior de la boca (mouthmask.cpp). La DLL repite el dibujo de la boca (PS 63fab773, que NO se
// toca) con este PS sobre una textura R8 del tamano del G-buffer: 1 donde se ve la boca (dientes, encias, lengua, garganta).
// La entrada es la misma que la del PS de la boca (mismo VS: la firma tiene que coincidir); solo se escribe un 1.
// La leen la luz de prueba del editor (pes_ssss_inc.hlsli, t7) y la luz del sol d0e9 / 7dbbc (t43): donde hay boca, la formula
// de antes, identica (regla de Agustin: el interior de la boca no cambia).
float4 main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float2 w2 : TEXCOORD9,
  float4 v3 : TEXCOORD4,
  float4 v4 : TEXCOORD5,
  float4 v5 : TEXCOORD6,
  float4 v6 : TEXCOORD7,
  float4 v7 : TEXCOORD8) : SV_Target0
{
  return float4(1, 1, 1, 1);
}
