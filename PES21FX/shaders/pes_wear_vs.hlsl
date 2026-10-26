// PES21 FX - desgaste de la cancha en vivo: un quad por jugador sobre el mapa de desgaste (uv de la cancha)
cbuffer Brushes : register(b0) { float4 hdr; float4 br[48]; }   // hdr.x = cantidad, hdr.yz = tamano de la cancha (m); br = (u, v, intensidad, radio m)
struct VSOut { float4 pos : SV_Position; float2 d : TEXCOORD0; float k : TEXCOORD1; };
VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
  VSOut o;
  float4 b = br[iid];
  float2 corner = float2(vid & 1, (vid >> 1) & 1) * 2.0 - 1.0;          // -1..1
  float2 ruv = float2(b.w / hdr.y, b.w / hdr.z) * 1.5;                     // radio del pincel en uv (1.5 radios de margen)
  float2 uv = b.xy + corner * ruv;
  o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
  o.d = corner * 1.5;                                                       // distancia en radios
  o.k = b.z;
  return o;
}
