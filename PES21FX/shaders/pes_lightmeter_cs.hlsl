// PES21 FX - medidor de luz (20/10). Corre (cada pocos cuadros) justo despues de la composicion diferida, con las mismas
// texturas que ella: por cada pixel de la muestra calcula la luz que recibe la superficie (color final / albedo) y la
// acumula por categoria y por sol / sombra (mascara de sombra del sol):
//   0 = pasto horizontal, 1 = piel, 2 = camisetas
// Con eso la DLL sabe la relacion real sol / sombra que ve el jugador y el color de la sombra, y puede calibrar la luz
// ambiente contra la de un cielo despejado (lightmeter.cpp). Salida: 3 x 2 x 5 uint (cuenta, lum, r, g, b) x 128.
// (21/10) + 5 uint para el rebote del pasto (cancha hasta 150 m): cuenta, albedo r g b x 1024, sol recibido x 1024.
cbuffer cPSScene : register(b2) {
  float4x4 m_projectionView; float4x4 m_projection; float4x4 m_view; float4x4 m_shadowProjection; float4x4 m_shadowProjection2;
  float4 m_eyepos; float4 m_projectionParam; float4 m_viewportSize; float4 m_exposure; float4 m_fogParam[3]; float4 m_fogColor;
  float4 m_cameraCenterOffset; float4 m_shadowMapResolutions;
};
cbuffer MeterCB : register(b0) { uint2 gSize; uint gStride; uint gPad; };
Texture2D<float4> tNormal : register(t0);
Texture2D<float4> tMat    : register(t1);
Texture2D<float4> tAlbedo : register(t2);
Texture2D<float4> tColor  : register(t3);   // salida de la composicion (HDR, antes del tonemap)
Texture2D<float4> tDepth  : register(t4);
Texture2D<float4> tShadow : register(t5);
RWByteAddressBuffer uOut : register(u0);
// (21/10) sombras de nubes: el pasto "al sol" que esta bajo una nube no cuenta (si no, el medidor creeria que la relacion
// sol / sombra bajo y oscureceria la luz ambiente cada vez que pasa una nube)
cbuffer cPSSystem : register(b1) { float4 m_param; float4 m_renderInfo; float4 m_renderBuffer; float4 m_dominantLightDir; float4 m_timeParam; };
Texture1D<float4> IniParams : register(t120);
#include "pes_common.hlsli"

groupshared uint gs[35];   // 30..34: (21/10) cancha para el rebote del pasto (cuenta, albedo r g b, sol recibido)

float3 DecN(float4 nb) {
  uint3 q = (uint3)(float3(1023, 1023, 3) * nb.xyw);
  float2 e = float2((q.x << 1) | (q.z >> 1), (q.y << 1) | (q.z & 1)) * 0.00097703957 - 1;
  float3 n = float3(e.xy, 1 - abs(e.x) - abs(e.y));
  if (n.z < 0) n.xy = (1 - abs(e.yx)) * (e.xy >= 0 ? 1 : -1);
  return normalize(n);
}

[numthreads(16, 16, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
  if (gi < 35) gs[gi] = 0;
  GroupMemoryBarrierWithGroupSync();
  uint2 p = tid.xy * gStride + (gStride >> 1);
  if (p.x < gSize.x && p.y < gSize.y) {
    float d = tDepth.Load(int3(p, 0)).x;
    if (d > 1e-6) {
      float z = m_projectionParam.z / (d - m_projectionParam.w);
      if (z > 0.3 && z < 150.0) {
        bool nearM = z < 45.0;                         // calibracion: cerca, sin niebla que aplane la medida
        float4 nb = tNormal.Load(int3(p, 0));
        int ty = (int)((uint)(nb.z * 1023 + 0.5) & 15);
        float mz = tMat.Load(int3(p, 0)).z;
        int cat = -1;
        // (20/10) pasto: cualquier normal. Cerca de la camara casi todo el cesped son hojas 3D con la normal de costado
        // (antes se exigia normal hacia arriba y no entraba ninguna muestra); lo que importa es el contraste que se ve
        if (ty == 3) cat = 0;
        else if (ty == 0 && abs(mz - 0.98039) < 0.003) cat = 1;
        else if (ty == 0 && abs(mz - 0.9607843) < 0.002) cat = 2;
        // (21/10) rebote del pasto: estadistica de la cancha hasta 150 m en 1 de cada 4 muestras (sobra para un promedio)
        bool turfS = cat == 0 && ((tid.x | tid.y) & 1) == 0;
        if (cat >= 0 && (nearM || turfS)) {
          float sh = saturate(tShadow.Load(int3(p, 0)).x);
          int side = !nearM ? -1 : (sh > 0.9 ? 0 : (sh < 0.1 ? 1 : -1));
          float cT = 1.0;                                // luz del sol que dejan pasar las nubes en este punto
          if (side == 0 || (turfS && sh > 0.02)) {
            PESCloudParams CPm = PESCloudLoad(m_timeParam.z);
            float4 SWm = IniParams.Load(int2(111, 0));
            if (CPm.sh > 0.5 && CPm.shK > 0.001 && SWm.y > 0.03) {
              float2 uvm = (float2(p) + 0.5) / float2(gSize);
              float2 sCm = uvm * float2(2, -2) + float2(-1, 1);
              float3 Pvm = float3((sCm - m_cameraCenterOffset.xy) * m_projectionParam.xy * z, z);
              float3 pWm = m_eyepos.xyz + float3(dot(Pvm, float3(m_view._m00, m_view._m01, m_view._m02)), dot(Pvm, float3(m_view._m10, m_view._m11, m_view._m12)), dot(Pvm, float3(m_view._m20, m_view._m21, m_view._m22)));
              float cRaw = PESCloudShadow(pWm, normalize(SWm.xyz), CPm);
              if (side == 0 && cRaw < 0.85) side = -1;   // el pasto "al sol" bajo una nube no cuenta para la calibracion
              cT = lerp(1.0, cRaw, saturate(CPm.shK));
            }
          }
          float3 alb = tAlbedo.Load(int3(p, 0)).xyz;
          if (turfS) {
            // 30 cuenta, 31..33 albedo (x1024), 34 cuanto sol le llega (sombra del estadio x nubes, x1024)
            InterlockedAdd(gs[30], 1);
            InterlockedAdd(gs[31], (uint)(saturate(alb.r) * 1024.0));
            InterlockedAdd(gs[32], (uint)(saturate(alb.g) * 1024.0));
            InterlockedAdd(gs[33], (uint)(saturate(alb.b) * 1024.0));
            InterlockedAdd(gs[34], (uint)(sh * cT * 1024.0));
          }
          if (side >= 0) {
            float3 col = tColor.Load(int3(p, 0)).xyz;
            float3 E = min(col / max(alb, 0.03), 32.0);   // luz recibida (irradiancia en las unidades del juego)
            float lum = dot(E, float3(0.2126, 0.7152, 0.0722));
            uint b = (uint)(cat * 2 + side) * 5;
            InterlockedAdd(gs[b + 0], 1);
            InterlockedAdd(gs[b + 1], (uint)(lum * 128.0));
            InterlockedAdd(gs[b + 2], (uint)(E.r * 128.0));
            InterlockedAdd(gs[b + 3], (uint)(E.g * 128.0));
            InterlockedAdd(gs[b + 4], (uint)(E.b * 128.0));
          }
        }
      }
    }
  }
  GroupMemoryBarrierWithGroupSync();
  if (gi < 35 && gs[gi] != 0) uOut.InterlockedAdd(gi * 4, gs[gi]);
}
