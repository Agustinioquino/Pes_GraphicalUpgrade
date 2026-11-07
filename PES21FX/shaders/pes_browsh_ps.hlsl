// PES21 FX (04/11 k) - SOMBRA DE CONTACTO de las hebras (cejas y pestanas) sobre la piel. Agustin: "no solo brillo, luz real".
// La DLL dibuja, antes de las hebras, cada pelo proyectado sobre la piel siguiendo la luz principal (pes_browsh_*_gs /
// pes_lashsh_gs); este PS multiplica el albedo del G-buffer (RT0, mezcla multiplicativa; las demas salidas no se escriben):
// la piel debajo de la ceja y el parpado debajo de las pestanas quedan sombreados pelo por pelo segun de donde venga la luz.
// La luz difusa del cielo tambien baja un poco ahi (oclusion de contacto real del pelo). z75 > 0.5 (diagnostico) = sin sombra.
Texture1D<float4> IniParams : register(t120);
struct VSOut {
  float4 pos : SV_Position;
  float3 nrmV : TEXCOORD0;
  float3 col : TEXCOORD1;
  float3 tw : TEXCOORD2;
  float2 motion : TEXCOORD3;
  float3 viewV : TEXCOORD4;
  float3 col2 : TEXCOORD5;
  float3 tanV : TEXCOORD6;
};
float4 main(VSOut i) : SV_Target0
{
  if (IniParams.Load(int2(75, 0)).z > 0.5) return float4(1, 1, 1, 1);
  float t = i.tw.x;
  float edge = saturate(1.0 - abs(i.tw.y));
  float k = 0.42 * smoothstep(0.0, 0.85, edge) * (1.0 - 0.55 * t) * lerp(0.45, 1.0, saturate(i.tw.z * 2.0));   // (06/11) hebra fina = sombra mas suave   // mas fuerte cerca de la raiz (el pelo esta pegado a la piel)
  float m = 1.0 - k;
  return float4(m, m, m, 1.0);
}
