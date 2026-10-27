// PES21 FX - el sol sobre el pasto (27/10, fila 313), solo en las pasadas del sol y solo en el material tipo 3 (cancha y hojas).
// Referencia: la foto de Agustin (pelota sobre el pasto con sol bajo): cada hoja con su lado al sol y su sombra, la luz que
// atraviesa las hojas amarillo verdosa y el sol bajo dorado.
// x313 sombras entre hojas: trazado corto en pantalla hacia el sol (8 cm) sobre la profundidad de las hojas 3D. Las hojas no
//      escriben el shadow map, asi que sin esto la sombra de una hoja sobre la de atras no existe. Se apaga solo cuando una
//      hoja de ~1 cm ocupa menos de ~1.5 px (lejos: ahi lo hace la oclusion del atlas del pasto)
// y313 la luz que pasa a traves de la hoja (la parte de la translucidez) se tine amarillo verdosa (clorofila)
// z313 sol bajo mas dorado sobre el pasto (de ~10 a ~37 grados de altura)
// w313 on/off
// Se incluye despues de declarar IniParams (lee la fila 111 para la altura del sol).

// difusa del sol sobre el pasto: wrapD es la del motor (directa + envoltura por translucidez)
// trans: translucidez del G-buffer (27/10: las hojas pintadas de cal llevan ~0.1 y no se tinen; las verdes 0.4-1.0)
float3 PESTurfDiffuse(float3 sunC, float rawNdotL, float wrapD, float4 PG, float trans)
{
  float dirD = saturate(rawNdotL);
  float through = max(saturate(wrapD) - dirD, 0.0);
  float3 leaf = lerp(float3(1.0, 1.0, 1.0), float3(1.0, 0.93, 0.45), saturate(PG.y) * saturate((trans - 0.15) / 0.25));
  float4 SW = IniParams.Load(int2(111, 0));
  float sunUp = dot(SW.xyz, SW.xyz) > 0.5 ? SW.y : 1.0;
  float low = 1.0 - smoothstep(0.17, 0.60, sunUp);
  float3 warm = lerp(float3(1.0, 1.0, 1.0), float3(1.07, 1.0, 0.84), saturate(PG.z) * low);
  return (dirD + through * leaf) * sunC * warm;
}

// sombra entre hojas (1 = sin sombra). sC: posicion en pantalla (v0.xy / v0.ww), uvC: uv del G-buffer, Am / Bm: de una a la
// otra (calculados fuera de cualquier if), pix: SV_Position.xy, L: hacia el sol en espacio de vista
float PESTurfMicroShadow(Texture2D<float4> dT, SamplerState sP, float2 sC, float2 uvC, float2 Am, float2 Bm, float2 pix, float3 L,
                         float4 pp, float2 cOff, float rbX, float tim, float k)
{
  float dC = dT.SampleLevel(sP, uvC, 0).x;
  float zC = pp.z / (dC - pp.w);
  float zSgn = zC < 0.0 ? -1.0 : 1.0, zA = abs(zC);   // el espacio de vista del motor tiene z negativa hacia adelante
  if (zA < 0.1 || zA > 40.0) return 1.0;
  float mpp = zA * abs(pp.x) * 2.0 / max(rbX, 1.0);    // metros por pixel
  float fadeT = 1.0 - smoothstep(0.006, 0.02, mpp);
  if (fadeT < 0.01) return 1.0;
  float3 Pv = float3((sC - cOff) * pp.xy * zC, zC);
  float ign = frac(52.9829189 * frac(dot(pix, float2(0.06711056, 0.00583715))) + frac(tim * 7.31 + 0.37));   // rota cada cuadro: el TAA lo promedia
  float3 Q = Pv + L * (0.002 + 0.6 * mpp);
  float occ = 0.0;
  [loop] for (int i = 0; i < 8; i++) {
    float f = (i + ign) / 8.0;
    float t = f * f * 0.08;
    float3 R = Q + L * t;
    if (R.z * zSgn < 0.1) break;
    float2 s = R.xy / (R.z * pp.xy) + cOff;
    float2 uv = Am * s + Bm;
    if (any(uv < 0.0) || any(uv > 1.0)) break;
    float zS = pp.z / (dT.SampleLevel(sP, uv, 0).x - pp.w);
    float dz = (R.z - zS) * zSgn;                         // > 0: el rayo paso por detras de una hoja
    float thick = 0.015 + 0.25 * t + 0.5 * mpp;           // grosor aceptado: una hoja, no un jugador que esta delante
    if (dz > 0.5 * mpp && dz < thick) { occ = 1.0 - 0.6 * f; break; }   // hoja pegada = sombra plena, mas lejos = penumbra
  }
  float2 eM = saturate(min(uvC, 1.0 - uvC) * 10.0);
  return 1.0 - saturate(occ * fadeT * saturate(k) * eM.x * eM.y);
}
