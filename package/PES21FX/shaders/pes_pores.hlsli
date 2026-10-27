// PES21 FX (27/10 c) - poros triplanares sobre la POSICION DE REPOSO de la malla.
// Antes el mosaico de poros se repetia sobre la UV de la cara (poros estirados donde la UV esta estirada) y despues se midio
// cuanta piel cubre la UV con derivadas en pantalla (cambiaba con la camara). Ahora el mosaico se proyecta desde los tres ejes
// del modelo en reposo (la pose en la que se modelo la cabeza: metros, X izquierda, Y arriba, Z adelante), como una calcomania
// pegada a la piel: un mosaico cada tileM metros en toda la cara, el cuello y las orejas, igual con cualquier camara y sin
// costuras entre la cara (5ce3 / ce188) y la cabeza generica (aa7d). Las tres proyecciones se funden con la normal de la
// malla en reposo (pesos a la cuarta); la perturbacion queda en el espacio del modelo y se lleva al espacio de vista con el
// marco tangente (T = dp/du, B = dp/dv) que sale de las derivadas de la posicion y la UV: queda como un valor mas del normal map.
// El VS de la cabeza (faab87ee / 455eb97b) deja la posicion de reposo en TEXCOORD6.w (x), TEXCOORD5.w (y) y TEXCOORD4.w = 10 + z (27/10 i).
#ifndef PES_PORES_HLSLI
#define PES_PORES_HLSLI

struct PesPores { float2 dT; float a; float var; float2 dbg; float near; };   // dT = perturbacion en el espacio tangente (x = T, y = B)

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
  PesPores o; o.dT = 0; o.a = 0; o.var = 0; o.dbg = 0; o.near = 0;
  if (!hasR) return o;
  // fundido por tamano en pantalla: con el pixel mas grande que ~2 celdas de la piel (22 por mosaico) el relieve ya no se ve
  float pxM = sqrt(length(dPx) * length(dPy));
  o.near = saturate((2.5 - pxM / (tileM / 22.0)) / 1.5);
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
#endif
