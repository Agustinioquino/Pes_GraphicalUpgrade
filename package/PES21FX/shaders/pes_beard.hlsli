// PES21 FX (04/11 c, Agustin: "la barba no acompana del todo" / "la barba es un modelo aparte tiene un skeleton hair") BARBA POR LOS
// HUESOS DE LA CARA, en el VS del pelo (2f1af485). Incluir DESPUES de cVSObject (g_vsObject) e IniParams.
// La DLL (expr.cpp, regla "beard = 13") enlaza cada vertice de la barba a la piel de la cara que tiene debajo (4 vertices mas
// cercanos: sus huesos, pesos y formas esculpidas, t117) y en cada dibujo de esa piel copia su paleta (cuadro actual y anterior) y su
// m_world al pozo (t116, 64 lugares de 200 float4: 0 = id de la piel y su cabeza, 1..96 huesos, 97..192 los del cuadro anterior,
// 193..196 m_world). Aca se busca el lugar de esa piel cuya cabeza cae donde esta la barba (el mismo jugador, a menos de 1.5 cm) y
// se le suma al vertice lo que los huesos de la cara (mandibula, labios, comisuras, sonrisa) corren ese punto respecto de la cabeza.
// Sin enlace (b13 en 0) o sin piel encontrada queda exacto como el juego.
#ifndef PES_BEARD_HLSLI
#define PES_BEARD_HLSLI
cbuffer PESBeard : register(b13)
{
  uint4 g_pesBeard;   // x primer float4 del enlace de esta malla en t117, y vertices, z primer indice (vmin), w 1 = hay enlace
}
Buffer<float4> g_pesBeardPool : register(t116);
Buffer<float4> g_pesBeardBind : register(t117);

float3 PesBeardBoneP(uint s, uint row0, uint j, float4 P)
{
  uint b = s * 200 + row0 + min(j, 31u) * 3;
  return float3(dot(P, g_pesBeardPool[b]), dot(P, g_pesBeardPool[b + 1]), dot(P, g_pesBeardPool[b + 2]));
}

// rest = posicion de reposo del vertice (espacio del modelo), pos / prev = salida del skinning del pelo (espacio del objeto)
void PesBeardFollow(uint vid, float3 rest, inout float3 pos, inout float3 prev)
{
  if (g_pesBeard.w == 0 || g_vsObject.m_useWeightCount.x == 0) return;
  if (vid < g_pesBeard.z) return;
  uint lv = vid - g_pesBeard.z;
  if (lv >= g_pesBeard.y) return;
  uint o = g_pesBeard.x + lv * 9;
  float4 BI = g_pesBeardBind[o], BW = g_pesBeardBind[o + 1], BS = g_pesBeardBind[o + 2];
  float fs = BW.x + BW.y + BW.z + BW.w;
  if (fs <= 0.0) return;
  // formas esculpidas de la piel (las mismas cuentas que PesShapeDelta, con la sonrisa grande de 1 a 1.5)
  float3 sd = 0;
  {
    float4 G5 = IniParams.Load(int2(355, 0)), G6 = IniParams.Load(int2(356, 0));
    if (G6.w > 0.5) {
      float sG = saturate((G5.y - 1.0) * 2.0);
      float w[6] = { saturate(G5.y) * (1.0 - sG), saturate(G6.x), saturate(G5.x), saturate(G5.z), saturate(G6.y), sG };
      [unroll] for (uint k = 0; k < 6; k++) if (w[k] > 0.001) sd += g_pesBeardBind[o + 3 + k].xyz * w[k];
    }
  }
  uint src = (uint)(BS.x + 0.5), hd = (uint)(BS.y + 0.5);
  float4 P = float4(rest, 1.0), Pd = float4(rest + sd, 1.0);
  float3 hw = float3(dot(float4(pos, 1.0), g_vsObject.m_world._m00_m10_m20_m30), dot(float4(pos, 1.0), g_vsObject.m_world._m01_m11_m21_m31), dot(float4(pos, 1.0), g_vsObject.m_world._m02_m12_m22_m32));
  float3 C0 = g_vsObject.m_world._m00_m10_m20, C1 = g_vsObject.m_world._m01_m11_m21, C2 = g_vsObject.m_world._m02_m12_m22;
  float is2 = 1.0 / max(dot(C0, C0), 1e-8);
  [loop] for (uint s = 0; s < 64; s++) {
    float4 H = g_pesBeardPool[s * 200];
    if ((uint)(H.x + 0.5) != src) continue;
    float4 W0 = g_pesBeardPool[s * 200 + 193], W1 = g_pesBeardPool[s * 200 + 194], W2 = g_pesBeardPool[s * 200 + 195];
    float3 hp = PesBeardBoneP(s, 1, hd, P);
    float3 ws = float3(dot(float4(hp, 1.0), W0), dot(float4(hp, 1.0), W1), dot(float4(hp, 1.0), W2));
    if (length(ws - hw) > 0.015) continue;   // otro jugador (u otro cuadro muy distinto)
    float3 d = -fs * hp, dq = -fs * PesBeardBoneP(s, 97, hd, P);
    [unroll] for (uint k = 0; k < 4; k++) if (BW[k] > 0.0) {
      d  += BW[k] * PesBeardBoneP(s, 1, (uint)(BI[k] + 0.5), Pd);
      dq += BW[k] * PesBeardBoneP(s, 97, (uint)(BI[k] + 0.5), Pd);
    }
    // del objeto de la piel al mundo y del mundo al objeto del pelo (m_world = giro * escala pareja)
    float3 dw = float3(dot(d, W0.xyz), dot(d, W1.xyz), dot(d, W2.xyz));
    float3 dwq = float3(dot(dq, W0.xyz), dot(dq, W1.xyz), dot(dq, W2.xyz));
    float3 dO = (C0 * dw.x + C1 * dw.y + C2 * dw.z) * is2, dOq = (C0 * dwq.x + C1 * dwq.y + C2 * dwq.z) * is2;
    if (dot(dO, dO) > 0.0009 || dot(dOq, dOq) > 0.0009) return;   // tope 3 cm (algo no cuadra)
    pos += dO; prev += dOq;
    return;
  }
}
#endif
