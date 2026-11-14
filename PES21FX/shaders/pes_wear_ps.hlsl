// PES21 FX - desgaste de la cancha en vivo: pincel gaussiano (se suma al mapa)
// (13/11 tanda 2) devuelve los 4 canales: con una sola salida los otros canales quedaban sin definir y el mapa pes_flat (RGBA16) elige
// el canal con la mascara de la mezcla (R pisadas, G pelota, B pisadas lentas z448)
struct VSOut { float4 pos : SV_Position; float2 d : TEXCOORD0; float k : TEXCOORD1; };
float4 main(VSOut i) : SV_Target0
{
  float r2 = dot(i.d, i.d);
  float v = i.k * exp(-r2 * 2.2) * (r2 < 2.25 ? 1.0 : 0.0);
  return float4(v, v, v, v);
}
