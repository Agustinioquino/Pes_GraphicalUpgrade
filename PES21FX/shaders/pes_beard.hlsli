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

// (04/11 i, Agustin: "la barba sigue desconectada al expresar", captura de Messi: el bigote y las comisuras quedan atras de la piel)
// la piel suma, ademas de los huesos del juego, el AMPLIFICADOR del mod (pes_expr.hlsli PesExprApply, fila 340: y340 1.35 = cada
// gesto un 35 % mas, por zona 341) en el VS de la cara; la barba solo seguia los huesos. Las mismas cuentas aca, con la paleta de
// la piel copiada al pozo (con la boca por huesos, 350.y >= 1, el amplificador ya va en los huesos y aca no se suma nada)
float PesBeardZone(float3 r, float4 Z)
{
  float ex = abs(r.x) - 0.028, ey = (r.y - 1.697) * 1.4;
  float we = 1.0 - smoothstep(0.012, 0.022, sqrt(ex * ex + ey * ey));
  float wb = smoothstep(1.700, 1.714, r.y) * (1.0 - we);
  float wm = (1.0 - smoothstep(1.646, 1.664, r.y)) * (1.0 - we);
  float wc = saturate(1.0 - we - wb - wm);
  return wb * Z.x + we * Z.y + wc * Z.z + wm * Z.w;
}
bool PesBeardSkip(uint j, uint head) { if (j == head) return true; if (head <= 2) return j < head; return j + 1 == head; }

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
  // (04/11 l, Agustin: "se buguea y rompe la barba") TODA la barba usa el mismo lugar del pozo: el ultimo que copio la DLL de esa piel
  // (directorio al final del pozo). Antes cada vertice buscaba el suyo entre 64 copias de cuadros distintos y la barba se rompia.
  // Se acepta si la cabeza de esa piel pone el punto a menos de 4 cm de la barba (la barba 3D cuelga de sus propios huesos);
  // con una DLL vieja (sin directorio) o otra cara, nada
  uint sBest = (uint)(g_pesBeardPool[64 * 200 + (src & 255u)].x + 0.5);
  if (sBest >= 64) return;
  if ((uint)(g_pesBeardPool[sBest * 200].x + 0.5) != src) return;
  // (04/11 n, videos de Agustin 16:09: con la boca por huesos (x350 2) la barba baja MAS que el menton y los pelos de los costados se
  // estiran) la barba 3D esta pesada en los huesos de la cara (skf_jaw, labios...): con la boca por huesos el juego ya la abre por su
  // propio skinning y aca se sumaba otra vez el gesto de la piel. Ahora se le quita lo que su skinning ya la corrio respecto de la
  // cabeza (hw - ws0) antes de sumar el de la piel: el resultado es el de la piel una sola vez. Una barba pesada solo en la cabeza
  // tiene hw = ws0 y queda como antes
  float3 corr = 0, corrq = 0;
  {
    float3 hp0 = PesBeardBoneP(sBest, 1, hd, P), hq0 = PesBeardBoneP(sBest, 97, hd, P);
    float4 V0 = g_pesBeardPool[sBest * 200 + 193], V1 = g_pesBeardPool[sBest * 200 + 194], V2 = g_pesBeardPool[sBest * 200 + 195];
    float3 ws0 = float3(dot(float4(hp0, 1.0), V0), dot(float4(hp0, 1.0), V1), dot(float4(hp0, 1.0), V2));
    float3 wq0 = float3(dot(float4(hq0, 1.0), V0), dot(float4(hq0, 1.0), V1), dot(float4(hq0, 1.0), V2));
    if (length(ws0 - hw) > 0.04) return;
    float3 hwq = float3(dot(float4(prev, 1.0), g_vsObject.m_world._m00_m10_m20_m30), dot(float4(prev, 1.0), g_vsObject.m_world._m01_m11_m21_m31), dot(float4(prev, 1.0), g_vsObject.m_world._m02_m12_m22_m32));
    float3 c = (ws0 - hw) * fs, cq = (wq0 - hwq) * fs;
    corr = (C0 * c.x + C1 * c.y + C2 * c.z) * is2; corrq = (C0 * cq.x + C1 * cq.y + C2 * cq.z) * is2;
    if (dot(corrq, corrq) > 0.0016) corrq = corr;   // el cuadro anterior de otro lugar del pozo: mismo arreglo que el actual
  }
  {
    uint s = sBest;
    float4 H = g_pesBeardPool[s * 200];
    float4 W0 = g_pesBeardPool[s * 200 + 193], W1 = g_pesBeardPool[s * 200 + 194], W2 = g_pesBeardPool[s * 200 + 195];
    float3 hp = PesBeardBoneP(s, 1, hd, P);
    float3 d = -fs * hp, dq = -fs * PesBeardBoneP(s, 97, hd, P);
    [unroll] for (uint k = 0; k < 4; k++) if (BW[k] > 0.0) {
      d  += BW[k] * PesBeardBoneP(s, 1, (uint)(BI[k] + 0.5), Pd);
      dq += BW[k] * PesBeardBoneP(s, 97, (uint)(BI[k] + 0.5), Pd);
    }
    {   // (04/11 i) amplificador del mod, igual que en la piel
      float4 E = IniParams.Load(int2(340, 0)), Z = IniParams.Load(int2(341, 0)), R = IniParams.Load(int2(350, 0));
      if (E.x > 0.5 && !(R.x > 0.5 && R.y > 0.5)) {
        float face = smoothstep(1.580, 1.610, rest.y) * smoothstep(0.000, 0.035, rest.z);
        float extra = (E.y - 1.0) * PesBeardZone(rest, Z) * face;
        if (abs(extra) >= 0.001) {
          float bigW = saturate(E.w), cap = max(E.z, 0.0) * 0.01;
          float3 hP = PesBeardBoneP(s, 1, hd, Pd), hQ = PesBeardBoneP(s, 97, hd, Pd), aP = 0, aQ = 0;
          [unroll] for (uint k = 0; k < 4; k++) {
            uint j = (uint)(BI[k] + 0.5);
            if (BW[k] <= 0.0 || j > 31 || PesBeardSkip(j, hd)) continue;
            float3 dk = PesBeardBoneP(s, 1, j, Pd) - hP;
            float sk = BW[k] / fs * lerp(1.0, bigW, smoothstep(0.012, 0.040, length(dk)));
            aP += sk * dk; aQ += sk * (PesBeardBoneP(s, 97, j, Pd) - hQ);
          }
          aP *= extra; aQ *= extra;
          float la = length(aP), lb = length(aQ);
          if (cap > 0.0) { if (la > cap) aP *= cap / la; if (lb > cap) aQ *= cap / lb; }
          d += aP; dq += aQ;
        }
      }
    }
    {   // (04/11 j, captura de Agustin: con la boca abierta los pelos de abajo quedaban sobre los dientes) "Abrir la boca" del mod (347.x,
        // pes_expr.hlsli PesJawApply) gira la piel de abajo en el VS de la cara: la barba hace el mismo giro con las mascaras de esa
        // piel (pozo fila 0 .zw = huesos enteros, fila 197 .xy = a la mitad). Con la boca por huesos (350.y >= 1) ya viene en la paleta
      float o = clamp(IniParams.Load(int2(347, 0)).x, -0.25, 1.0);
      float4 R = IniParams.Load(int2(350, 0));
      if (abs(o) >= 0.001 && !(R.x > 0.5 && R.y > 0.5)) {
        float4 JH = g_pesBeardPool[s * 200 + 197];
        uint full = (uint)(H.z + 0.5) | ((uint)(H.w + 0.5) << 16), half_ = (uint)(JH.x + 0.5) | ((uint)(JH.y + 0.5) << 16);
        if ((full | half_) != 0u) {
          float A = 0.40 * o;
          float3 q = Pd.xyz - float3(0.0, 1.6369, 0.0795);
          [unroll] for (uint k = 0; k < 4; k++) {
            uint j = (uint)(BI[k] + 0.5);
            if (BW[k] <= 0.0 || j > 31) continue;
            float f = (full & (1u << j)) != 0u ? 1.0 : ((half_ & (1u << j)) != 0u ? 0.5 : 0.0);
            if (f <= 0.0) continue;
            float c2 = cos(A * f), s2 = sin(A * f);
            float3 dh = float3(0.0, q.y * c2 - q.z * s2, q.y * s2 + q.z * c2) - float3(0.0, q.y, q.z);
            d  += BW[k] * PesBeardBoneP(s, 1,  j, float4(dh, 0.0));
            dq += BW[k] * PesBeardBoneP(s, 97, j, float4(dh, 0.0));
          }
        }
      }
    }
    // del objeto de la piel al mundo y del mundo al objeto del pelo (m_world = giro * escala pareja)
    float3 dw = float3(dot(d, W0.xyz), dot(d, W1.xyz), dot(d, W2.xyz));
    float3 dwq = float3(dot(dq, W0.xyz), dot(dq, W1.xyz), dot(dq, W2.xyz));
    float3 dO = (C0 * dw.x + C1 * dw.y + C2 * dw.z) * is2, dOq = (C0 * dwq.x + C1 * dwq.y + C2 * dwq.z) * is2;
    if (dot(dO, dO) > 0.0025 || dot(dOq, dOq) > 0.0025) return;   // tope 5 cm (algo no cuadra; la boca abierta del todo corre el menton ~3 cm)
    // (04/11 k) prueba w365 = 1: la barba que SI recibe el enlace sube 1 cm (se ve enseguida que pedazos siguen a la piel y cuales no)
    if (IniParams.Load(int2(365, 0)).w > 0.5) { dO += C1 * (0.01 * is2); dOq += C1 * (0.01 * is2); }
    pos += dO + corr; prev += dOq + corrq;
    return;
  }
}
#endif
