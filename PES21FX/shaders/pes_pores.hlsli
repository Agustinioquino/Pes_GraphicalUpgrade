// PES21 FX (27/10 c) - poros triplanares sobre la POSICION DE REPOSO de la malla.
// Antes el mosaico de poros se repetia sobre la UV de la cara (poros estirados donde la UV esta estirada) y despues se midio
// cuanta piel cubre la UV con derivadas en pantalla (cambiaba con la camara). Ahora el mosaico se proyecta desde los tres ejes
// del modelo en reposo (la pose en la que se modelo la cabeza: metros, X izquierda, Y arriba, Z adelante), como una calcomania
// pegada a la piel: un mosaico cada tileM metros en toda la cara, el cuello y las orejas, igual con cualquier camara y sin
// costuras entre la cara (5ce3 / ce188) y la cabeza generica (aa7d). Las tres proyecciones se funden con la normal de la
// malla en reposo (pesos a la cuarta); la perturbacion queda en el espacio del modelo y se lleva al espacio de vista con el
// marco tangente (T = dp/du, B = dp/dv) que sale de las derivadas de la posicion y la UV: queda como un valor mas del normal map.
// El VS de la cabeza (faab87ee / 455eb97b) deja la posicion de reposo en TEXCOORD10.xyz (w = 11) (27/10 j); el VS del cuerpo 1c02a92a tambien (w = 1).
#ifndef PES_PORES_HLSLI
#define PES_PORES_HLSLI
// (31/10) mosaico fotografico (tools/make_pores_photo.py, TextureCan Skin 0001 CC0 + pozos de poro): 50 mm de piel por mosaico
// con y94 = 7 (antes 22 mm con el mosaico procedural). Los fundidos por distancia se miden en mm de piel por pixel.
#define PES_PORE_TILE_MM 50.0

struct PesPores { float2 dT; float a; float var; float2 dbg; float near; float close; };   // dT = perturbacion en el espacio tangente (x = T, y = B); close = primer plano real

float4 PesPoreTap(Texture2D<float4> micro, SamplerState smp, float2 c, float2 gx, float2 gy, float big) {
  float4 m = micro.SampleGrad(smp, frac(c), gx, gy);
  float4 m2 = micro.SampleGrad(smp, frac(c * 0.5 + 0.37), gx * 0.5, gy * 0.5);   // poros grandes (nariz, barbilla): mitad de escala
  return lerp(m, m2, big * 0.7);
}
float PesPoreVar(Texture2D<float4> micro, SamplerState smp, float2 c, float2 gx, float2 gy) {
  // Toksvig (GPU Gems 3): la micro normal promediada en un area mas grande pierde largo = rugosidad extra
  float4 ma = micro.SampleGrad(smp, frac(c), gx * 6.0, gy * 6.0);
  return saturate((1.0 - length(ma.xyz * 2.0 - 1.0)) * 8.0);
}

// pM posicion de reposo (m) y sus derivadas en pantalla; dUx / dUy derivadas de la UV; tileM metros por mosaico; big 0..1 poros
// grandes; hasR = el VS escribio la posicion de reposo. La salida dT va en el espacio tangente del normal map del juego
// (x a lo largo de dp/du = T, y a lo largo de dp/dv = B), como el valor del normal map antes de pasarlo a vista.
PesPores PesTriPores(Texture2D<float4> micro, SamplerState smp, float3 pM, float3 dPx, float3 dPy, float2 dUx, float2 dUy,
                     float tileM, float big, bool hasR) {
  PesPores o; o.dT = 0; o.a = 0; o.var = 0; o.dbg = 0; o.near = 0; o.close = 0;
  if (!hasR) return o;
  // fundido por tamano en pantalla: con el pixel mas grande que ~2.5 mm de piel el relieve ya no se ve
  float pxM = sqrt(length(dPx) * length(dPy));
  float mmPx = pxM / (tileM / PES_PORE_TILE_MM);   // mm de piel por pixel
  o.near = saturate((2.5 - mmPx) / 1.5);
  // (31/10) primer plano real: el poro (0.1..0.3 mm) solo se ve como hueco oscuro cuando ocupa varios pixeles (pixel <= 0.15 mm);
  // a media distancia el mip promedia los poros y quedaban como puntitos grises sueltos (sal y pimienta): ahi la micro normal
  // solo rompe el brillo (rugosidad), como en UE5, y no oscurece la piel
  o.close = saturate((0.5 - mmPx) / 0.35);
  if (o.near <= 0.001) return o;
  // marco de la superficie en reposo: dp/du y dp/dv desde las derivadas, normal geometrica = su producto cruz
  float det = dUx.x * dUy.y - dUx.y * dUy.x; det = abs(det) < 1e-12 ? 1e-12 : det;
  float3 dpdu = (dPx * dUy.y - dPy * dUx.y) / det, dpdv = (dPy * dUx.x - dPx * dUy.x) / det;
  float3 nM = normalize(cross(dpdu, dpdv));
  float3 tM = normalize(dpdu), bM = normalize(dpdv - tM * dot(dpdv, tM));
  float3 wgt = pow(abs(nM), 4.0); wgt /= max(wgt.x + wgt.y + wgt.z, 1e-6);
  float3 q = pM / tileM, qx = dPx / tileM, qy = dPy / tileM;
  // tres proyecciones (cada una corrida para que no se repita el mismo dibujo en los fundidos): X -> (z, y), Y -> (x, z), Z -> (x, y)
  float4 mX = PesPoreTap(micro, smp, q.zy + float2(0.0, 0.31), qx.zy, qy.zy, big);
  float4 mY = PesPoreTap(micro, smp, q.xz + float2(0.57, 0.13), qx.xz, qy.xz, big);
  float4 mZ = PesPoreTap(micro, smp, q.xy, qx.xy, qy.xy, big);
  float3 tX = mX.xyz * 2.0 - 1.0, tY = mY.xyz * 2.0 - 1.0, tZ = mZ.xyz * 2.0 - 1.0;
  // perturbacion en el espacio del modelo: la pendiente (u, v) de cada proyeccion sobre sus dos ejes
  float3 dNm = float3(wgt.y * tY.x + wgt.z * tZ.x, wgt.x * tX.y + wgt.z * tZ.y, wgt.x * tX.x + wgt.y * tY.y);
  dNm -= nM * dot(dNm, nM);   // solo la parte tangente (un surco sigue siendo un surco mirado de cualquier lado)
  o.dT = float2(dot(dNm, tM), dot(dNm, bM));
  o.a = wgt.x * mX.a + wgt.y * mY.a + wgt.z * mZ.a;
  o.var = wgt.x * PesPoreVar(micro, smp, q.zy + float2(0.0, 0.31), qx.zy, qy.zy)
        + wgt.y * PesPoreVar(micro, smp, q.xz + float2(0.57, 0.13), qx.xz, qy.xz)
        + wgt.z * PesPoreVar(micro, smp, q.xy, qx.xy, qy.xy);
  o.dbg = (wgt.x * tX.xy + wgt.y * tY.xy + wgt.z * tZ.xy) * 0.5 + 0.5;
  return o;
}

// ==================================================================================================================================
// (04/11) POROS v4 POR ZONA. Agustin: "no me convence el color ni la forma, ni el tamano de los micro poros; una piel real no tiene
// poros iguales en toda la piel, en los labios por ejemplo o cerca de los ojos". Tres micro relieves hechos para la cara
// (tools/make_skin_micro_zones.py, mosaico de 20 mm) que se mezclan por zona:
//   tR poros redondos de tamanos variados sobre la red de surcos (nariz, mejillas, frente, barbilla), a tres escalas (0.6 / 1 / 1.7)
//      elegidas por el tamano de la zona (z.x: 0 = poros finos de la frente y alrededor de ojos y boca, 1 = nariz)
//   tC arruguitas cruzadas sin poros (z.y: parpados, ojeras, patas de gallo, cuello)
//   tL estrias verticales de los labios (z.z: la mascara de labios del mapa de zonas)
// Misma salida que PesTriPores (dT en el espacio tangente del normal map, a = cavidad, var = varianza para la rugosidad).
#define PES_PORE_TILE_MM_Z 20.0

float4 PesTriTapZ(Texture2D<float4> t, SamplerState smp, float3 q, float3 qx, float3 qy, float3 wgt, float sc, float ofs) {
  float3 qs = q / sc + ofs, qxs = qx / sc, qys = qy / sc;
  float4 mX = t.SampleGrad(smp, frac(qs.zy + float2(0.0, 0.31)), qxs.zy, qys.zy);
  float4 mY = t.SampleGrad(smp, frac(qs.xz + float2(0.57, 0.13)), qxs.xz, qys.xz);
  float4 mZ = t.SampleGrad(smp, frac(qs.xy), qxs.xy, qys.xy);
  float3 tX = mX.xyz * 2.0 - 1.0, tY = mY.xyz * 2.0 - 1.0, tZ = mZ.xyz * 2.0 - 1.0;
  return float4(wgt.y * tY.x + wgt.z * tZ.x, wgt.x * tX.y + wgt.z * tZ.y, wgt.x * tX.x + wgt.y * tY.y,
                wgt.x * mX.a + wgt.y * mY.a + wgt.z * mZ.a);
}
// varianza (Toksvig): largo de la normal promediada en un area 6 veces mayor (el mip no renormaliza)
float PesTriVarZ(Texture2D<float4> t, SamplerState smp, float3 q, float3 qx, float3 qy, float3 wgt, float sc) {
  float3 qs = q / sc, qxs = qx * (6.0 / sc), qys = qy * (6.0 / sc);
  float lX = length(t.SampleGrad(smp, frac(qs.zy + float2(0.0, 0.31)), qxs.zy, qys.zy).xyz * 2.0 - 1.0);
  float lY = length(t.SampleGrad(smp, frac(qs.xz + float2(0.57, 0.13)), qxs.xz, qys.xz).xyz * 2.0 - 1.0);
  float lZ = length(t.SampleGrad(smp, frac(qs.xy), qxs.xy, qys.xy).xyz * 2.0 - 1.0);
  return saturate((1.0 - (wgt.x * lX + wgt.y * lY + wgt.z * lZ)) * 8.0);
}

// ==================================================================================================================================
// (04/11) POROS v5. Agustin: "haz estos normales de mayor calidad con diferencias un sombreado suave o duro, ademas ten en cuenta los
// diferentes poros de la piel, cerca de los ojos, sobre las cejas, en la frente y en el cuello"; "el tamano consideralo vos sabiendo
// como es el tamano de los poros en rostros reales, que el tamano no cambie con la distancia"; "me gusta el enfoque moderno".
//  - TAMANO REAL FIJO: un mosaico = 20 mm de piel siempre (ya no depende de y94 ni de la zona): cada zona tiene su textura con los
//    poros del tamano real de esa zona (tools/make_skin_micro_zones.py): tR mejillas / resto, tG nariz y menton (grandes), tF frente
//    (y sobre las cejas, mezclada con tG), tC parpados / ojeras / patas de gallo, tL labios, tN cuello. Con la distancia el mip
//    promedia el relieve (no lo agranda) y lo que ya no se ve pasa a rugosidad (Toksvig), como en UE5 / HDRP.
//  - zA: x tamano (0 mejilla .. 1 nariz), y arruguitas, z labios, w cuello; zB: x frente, y sobre las cejas
PesPores PesTriPores5(Texture2D<float4> tR, Texture2D<float4> tG, Texture2D<float4> tF, Texture2D<float4> tC, Texture2D<float4> tL,
                      Texture2D<float4> tN, SamplerState smp, float3 pM, float3 dPx, float3 dPy, float2 dUx, float2 dUy,
                      float4 zA, float2 zB, bool hasR) {
  PesPores o; o.dT = 0; o.a = 0; o.var = 0; o.dbg = 0; o.near = 0; o.close = 0;
  if (!hasR) return o;
  const float tileM = PES_PORE_TILE_MM_Z * 0.001;
  float mmPx = sqrt(length(dPx) * length(dPy)) * 1000.0;   // mm de piel por pixel
  o.near = saturate((2.0 - mmPx) / 1.2);    // el relieve se funde con el pixel mas grande que ~2 mm (el mip ya promedia antes)
  o.close = saturate((0.4 - mmPx) / 0.25);  // hueco oscuro del poro solo con el poro de 2 o mas pixeles (pixel <= 0.15 mm)
  if (o.near <= 0.001) return o;
  float det = dUx.x * dUy.y - dUx.y * dUy.x; det = abs(det) < 1e-12 ? 1e-12 : det;
  float3 dpdu = (dPx * dUy.y - dPy * dUx.y) / det, dpdv = (dPy * dUx.x - dPx * dUy.x) / det;
  float3 nM = normalize(cross(dpdu, dpdv));
  float3 tM = normalize(dpdu), bM = normalize(dpdv - tM * dot(dpdv, tM));
  float3 wgt = pow(abs(nM), 4.0); wgt /= max(wgt.x + wgt.y + wgt.z, 1e-6);
  float3 q = pM / tileM, qx = dPx / tileM, qy = dPy / tileM;
  // pesos por zona (suman 1): labios > cuello > arruguitas > frente / cejas > poros (grandes o de mejilla)
  float rem = 1.0;
  float wL = saturate(zA.z);                         rem -= wL;
  float wN = saturate(zA.w) * rem;                   rem -= wN;
  float wC = saturate(zA.y) * rem;                   rem -= wC;
  float wF = saturate(zB.x + 0.5 * zB.y) * rem;      rem -= wF;
  float wG = saturate(zA.x + 0.5 * zB.y) * rem;
  float wR = max(rem - wG, 0.0);
  float4 acc = 0; float var = 0;
  [branch] if (wR > 0.02) { acc += PesTriTapZ(tR, smp, q, qx, qy, wgt, 1.0, 0.0)  * wR; var += PesTriVarZ(tR, smp, q, qx, qy, wgt, 1.0) * wR; }
  [branch] if (wG > 0.02) { acc += PesTriTapZ(tG, smp, q, qx, qy, wgt, 1.0, 0.23) * wG; var += PesTriVarZ(tG, smp, q, qx, qy, wgt, 1.0) * wG; }
  [branch] if (wF > 0.02) { acc += PesTriTapZ(tF, smp, q, qx, qy, wgt, 1.0, 0.61) * wF; var += PesTriVarZ(tF, smp, q, qx, qy, wgt, 1.0) * wF; }
  [branch] if (wC > 0.02) { acc += PesTriTapZ(tC, smp, q, qx, qy, wgt, 1.0, 0.11) * wC; var += PesTriVarZ(tC, smp, q, qx, qy, wgt, 1.0) * wC * 0.6; }
  [branch] if (wL > 0.02) { acc += PesTriTapZ(tL, smp, q, qx, qy, wgt, 1.0, 0.37) * wL; }
  [branch] if (wN > 0.02) { acc += PesTriTapZ(tN, smp, q, qx, qy, wgt, 1.0, 0.83) * wN; var += PesTriVarZ(tN, smp, q, qx, qy, wgt, 1.0) * wN * 0.8; }
  float3 dNm = acc.xyz - nM * dot(acc.xyz, nM);
  o.dT = float2(dot(dNm, tM), dot(dNm, bM));
  o.a = acc.w;
  o.var = var;
  o.dbg = float2(wR + wG + wF, wC + wN);
  return o;
}

// Signo del marco tangente del juego (T, B del VS, en la vista) respecto de dp/du y dp/dv: la perturbacion de los poros se calcula
// contra dp/du y dp/dv; si el modelo trae la bitangente al reves (convencion de verde del normal map), el relieve de los poros quedaba
// espejado en vertical (el hueco iluminado del lado de la luz, se veia como granito). Se mira hacia donde se mueve en la PANTALLA un
// paso a lo largo de T (y de B) y se compara con hacia donde crece u (y v) en la pantalla (derivadas de la UV): sin profundidad ni
// matrices. v3 = direccion de la vista del pixel (cualquier signo y largo); vista con x a la derecha e y hacia arriba.
float2 PesTanSigns(float3 T, float3 B, float3 v3, float2 dUx, float2 dUy) {
  float iz = 1.0 / (abs(v3.z) > 1e-4 ? v3.z : 1e-4);
  float2 mT = float2(T.x - v3.x * T.z * iz, -(T.y - v3.y * T.z * iz));
  float2 mB = float2(B.x - v3.x * B.z * iz, -(B.y - v3.y * B.z * iz));
  float sT = dot(mT, float2(dUx.x, dUy.x)), sB = dot(mB, float2(dUx.y, dUy.y));
  return float2(sT < 0.0 ? -1.0 : 1.0, sB < 0.0 ? -1.0 : 1.0);
}

// Mezcla de normales RNM (Barre-Brisebois / Hill 2012, la de UE5 y HDRP para el detalle): el relieve del poro se REORIENTA sobre la
// normal de la cara en vez de sumarle la pendiente. Antes (suma + normalizar) en las zonas inclinadas del normal map (aletas de la
// nariz, parpados, menton, arrugas) el poro se aplanaba y perdia fuerza. n1 base, n2 detalle, los dos en el espacio tangente.
float3 PesRNM(float3 n1, float3 n2) {
  float3 t = n1 + float3(0.0, 0.0, 1.0), u = n2 * float3(-1.0, -1.0, 1.0);
  return normalize(t * (dot(t, u) / max(t.z, 1e-4)) - u);
}
#endif
