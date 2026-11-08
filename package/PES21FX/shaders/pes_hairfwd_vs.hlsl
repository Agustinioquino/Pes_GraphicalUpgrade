// PES21 FX (08/11 v) - PASADA PROPIA DEL PELO (cejas con hebras), vertex shader. Lee la salida de flujo (stream output) que dejo el GS de
// las hebras al dibujarse en el G-buffer (pes_brow_gs.hlsli, mismo cuadro): cada vertice ya viene en el clip de la camara de este cuadro.
// La DLL (facegen.cpp HairFwdAfterComposition) lo dibuja con DrawAuto despues de la composicion diferida, antes del TAA.
// (08/11 z3, Agustin: "me gusta la idea de transparencia pero ya no funcionan bien las hebras" / "mejora lo nuevo para que funcione con
// lo viejo") MODO MIXTO (x425): la hebra sigue en el G-buffer (pegada a la cara, luz y profundidad del juego) y aca solo se le agrega un
// BORDE semitransparente: el VS corre cada vertice y425 px hacia afuera (desde el centro de la hebra, sin depender del signo del costado)
// y le pasa al PS el ancho del nucleo y del borde en pixeles (fr)
cbuffer cPSScene : register(b2) { float4 scPV[4]; float4 scProj[4]; }   // la DLL pone aca el b2 del PS (misma proyeccion que el GS)
cbuffer PESHairFwd : register(b5) { float4 hfLight; float4 hfInfo; float4 hfMix; }   // hfMix: x modo mixto, y borde px, z opacidad
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
  float2 fr : TEXCOORD8;     // x = medio ancho del nucleo (px), y = borde agregado (px); 0 = sin modo mixto
};
VSOut main(VSIn v)
{
  VSOut o;
  o.pos = v.pos; o.nrmV = v.nrmV; o.col = v.col; o.tw = v.tw; o.motion = v.motion;
  o.viewV = v.viewV; o.col2 = v.col2; o.tanV = v.tanV; o.posV = v.posV;
  o.fr = float2(0, 0);
  if (hfMix.x > 0.5 && v.pos.w > 1e-5) {
    float4 h = float4(v.posV, 1);
    float4 cC = float4(dot(h, scProj[0]), dot(h, scProj[1]), dot(h, scProj[2]), dot(h, scProj[3]));
    float2 res = max(hfInfo.yz, 1.0);
    if (cC.w > 1e-5) {
      float2 dPx = (v.pos.xy / v.pos.w - cC.xy / cC.w) * res * 0.5;   // del centro de la hebra a este vertice, en pixeles
      float core = length(dPx);
      float ext = max(hfMix.y, 0.0);
      if (core > 1e-4 && ext > 0.0) {
        float2 nd = (dPx / core) * ext * 2.0 / res;
        o.pos.xy += nd * v.pos.w;
        o.fr = float2(core, ext);
      }
    }
  }
  return o;
}
