// PES21 FX (08/11 v) - PASADA PROPIA DEL PELO (cejas con hebras), vertex shader. Lee la salida de flujo (stream output) que dejo el GS de
// las hebras al dibujarse en el G-buffer (pes_brow_gs.hlsli, mismo cuadro): cada vertice ya viene en el clip de la camara de este cuadro.
// La DLL (facegen.cpp HairFwdAfterComposition) lo dibuja con DrawAuto despues de la composicion diferida, antes del TAA.
struct VSIn {
  float4 pos : POSITION;
  float3 nrmV : TEXCOORD0;
  float3 col : TEXCOORD1;
  float3 tw : TEXCOORD2;
  float2 motion : TEXCOORD3;
  float3 viewV : TEXCOORD4;
  float3 col2 : TEXCOORD5;
  float3 tanV : TEXCOORD6;
  float3 posV : TEXCOORD7;
};
struct VSOut {
  float4 pos : SV_Position;
  float3 nrmV : TEXCOORD0;
  float3 col : TEXCOORD1;
  float3 tw : TEXCOORD2;
  float2 motion : TEXCOORD3;
  float3 viewV : TEXCOORD4;
  float3 col2 : TEXCOORD5;
  float3 tanV : TEXCOORD6;
  float3 posV : TEXCOORD7;
};
VSOut main(VSIn v)
{
  VSOut o;
  o.pos = v.pos; o.nrmV = v.nrmV; o.col = v.col; o.tw = v.tw; o.motion = v.motion;
  o.viewV = v.viewV; o.col2 = v.col2; o.tanV = v.tanV; o.posV = v.posV;
  return o;
}
