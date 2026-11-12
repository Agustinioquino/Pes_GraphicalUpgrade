// PES21 FX - desgaste de la cancha en vivo: pincel gaussiano (se suma al mapa)
struct VSOut { float4 pos : SV_Position; float2 d : TEXCOORD0; float k : TEXCOORD1; };
float main(VSOut i) : SV_Target0
{
  float r2 = dot(i.d, i.d);
  return i.k * exp(-r2 * 2.2) * (r2 < 2.25 ? 1.0 : 0.0);
}
