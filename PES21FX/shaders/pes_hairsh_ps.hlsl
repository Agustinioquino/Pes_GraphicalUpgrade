// PES21 FX (08/11 v) - SOMBRA DE CONTACTO de las cejas con hebras despues de la luz (Agustin: "mejorar la sombra que plasma el pelo").
// Cada pelo proyectado sobre la piel siguiendo la luz (GS PES_SHADOW, salida de flujo) multiplica la imagen HDR ya iluminada, pero solo
// la parte de LUZ DIRECTA: donde la mascara de sombra del juego dice que el sol no llega (estadio a la sombra, nariz, frente) no
// marca nada; la penumbra se abre con la altura del pelo (GS). Antes multiplicaba el albedo en el G-buffer: oscurecia tambien la luz
// del cielo y la ceja se "dibujaba" sobre la piel aunque no hubiera sol. Mezcla: destino x color de salida.
// (Encabezado compartido con pes_hairfwd_ps.hlsl, el de la pasada propia del pelo:)
// PES21 FX (08/11 v) - PASADA PROPIA DEL PELO (cejas con hebras), pixel shader. Agustin: "piensa en como se plasman los pelos de hebras
// en juegos modernos" + "todos los puntos". Como UE5 (Groom) / Frostbite, el pelo tiene su propio sombreado fuera del G-buffer:
//  1) se dibuja DESPUES de la luz diferida, sumado sobre la imagen HDR con la COBERTURA real de cada hebra (mezcla aditiva: no
//     depende del orden, sin descarte por pixel): las hebras finas quedan suaves y sin escalones, el brillo no centellea.
//  2) luz de PELO con la TANGENTE de la hebra (no una normal): los tres lobulos de Marschner (R = brillo blanco corrido hacia la raiz,
//     TT = luz que atraviesa el pelo a contraluz con su color, TRT = segundo brillo de color), con la aproximacion de Karis (2016).
//  3) sombra en capas: la del estadio (mascara de sombra del juego, t14), la propia de la cabeza y entre hebras (atlas t30, que tiene
//     las hebras dibujadas) y la transmision por el GROSOR REAL de pelo que cruza la luz (Beer-Lambert por fibras).
// El G-buffer de las hebras (pes_brow_ps) queda con la luz difusa (cielo, rebote, sol difuso) y ya no se marca como pelo ni lleva el
// brillo: lo hace esta pasada. w382 = fuerza (0 = apagada, todo como antes).
cbuffer cPSScene : register(b2)
{
  struct
  {
    float4x4 m_projectionView;
    float4x4 m_projection;
    float4x4 m_view;
    float4x4 m_shadowProjection;
    float4x4 m_shadowProjection2;
    float4 m_eyepos;
    float4 m_projectionParam;
    float4 m_viewportSize;
    float4 m_exposure;
    float4 m_fogParam[3];
    float4 m_fogColor;
    float4 m_cameraCenterOffset;
    float4 m_shadowMapResolutions;
  } g_psScene : packoffset(c0);
}
cbuffer PESHairFwd : register(b5)
{
  float4 hfLight;   // rgb = color de la luz principal del cuadro (LightCB), w = fuerza (w382)
  float4 hfInfo;    // x = 1 profundidad invertida, yz = tamano del render, w = 1 hay luz del juego
}
Texture1D<float4> IniParams : register(t120);
Texture2D<float4> tDepth : register(t11);    // profundidad de la escena (la de la composicion)
Texture2D<float4> tShadow : register(t14);   // mascara de sombra del sol del juego (1 = al sol)
SamplerState sPt : register(s15);
#include "pes_headshadow.hlsli"
#include "pes_skinthick.hlsli"
#include "pes_brow_light.hlsli"

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

float LinZ(float d) { return g_psScene.m_projectionParam.z / (d - g_psScene.m_projectionParam.w); }
float4 main(VSOut i) : SV_Target0
{
  float gain = saturate(hfLight.w);
  if (gain <= 0.001 || IniParams.Load(int2(75, 0)).z > 0.5) return float4(1, 1, 1, 1);
  int2 px = int2(i.pos.xy);
  float zS = abs(LinZ(tDepth.Load(int3(px, 0)).x)), zH = abs(LinZ(i.pos.z));
  if (abs(zH - zS) > 0.0015) return float4(1, 1, 1, 1);   // solo sobre la piel (donde hay una hebra encima, la hebra tapa)
  float2 sd; tShadow.GetDimensions(sd.x, sd.y);
  float shP = saturate(tShadow.Load(int3(i.pos.xy * sd / max(hfInfo.yz, 1.0), 0)).x);
  float3 c0 = g_psScene.m_view._m00_m10_m20, c1 = g_psScene.m_view._m01_m11_m21, c2 = g_psScene.m_view._m02_m12_m22;
  float3 L = PesBrowLightDir(c0, c1, c2, float3(0, 0, 0), i.posV.z);
  float vis = shP * (dot(L, L) > 0.25 ? PESHeadShadowAt(i.posV, dot(normalize(i.nrmV), L), sPt) : 0.0);
  float t = i.tw.x;
  float edge = saturate(1.0 - abs(i.tw.y));
  float cov = i.tw.z - 2.0 * floor(i.tw.z * 0.5);
  float k = 0.32 * smoothstep(0.0, 0.85, edge) * (1.0 - 0.55 * t) * lerp(0.45, 1.0, saturate(cov * 2.0));
  float m = 1.0 - k * vis * 0.75 * gain;   // ~75 % de la luz en la piel al sol es directa
  return float4(m, m, m, 1.0);
}
