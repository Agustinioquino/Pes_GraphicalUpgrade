// PES21 FX (27/10) - tono de la textura de brazo de una cara real (brazos.cpp, fila 327).
// La cara real trae arm_bsm.ftex (atlas de Konami, pintado con el tono que eligio el facemaker). Se compara su mip de 64 px
// texel por texel con el mismo mip de arm_1..6_bsm de TorsoBody (mismo mapa UV). Solo cuentan los texeles parecidos a alguno de
// los 6 (piel pintada): lo que Konami deja sin pintar arriba de la manga corta queda afuera. El tono es el de menor distancia
// media (lo elige brazos.cpp al leer el resultado).
// Salida (u0, 10 uint): 0 = texeles que cuentan, 1..6 = suma de distancias a arm_N (x1000), 7..9 = suma del color de la cara
// (sRGB x255, para el log).
Texture2D<float4> gFace : register(t0);
Texture2D<float4> gArm[6] : register(t1);
cbuffer C : register(b0) { uint gMip; uint gUmbral; uint gPad0; uint gPad1; };   // gUmbral: distancia maxima x1000
RWByteAddressBuffer gOut : register(u0);

groupshared uint gs[10];

float3 Perc(float3 c) { return sqrt(saturate(c)); }   // las vistas son sRGB: valores lineales; sqrt ~ escala percibida

[numthreads(16, 16, 1)]
void main(uint3 t : SV_GroupThreadID) {
  uint li = t.y * 16 + t.x;
  if (li < 10) gs[li] = 0;
  GroupMemoryBarrierWithGroupSync();
  uint cnt = 0;
  float acc[6] = { 0, 0, 0, 0, 0, 0 };
  float3 col = 0;
  const float umbral = gUmbral / 1000.0;
  for (uint j = 0; j < 16; j++) {
    int2 p = int2(t.xy * 4 + uint2(j & 3, j >> 2));
    float3 f = Perc(gFace.Load(int3(p, gMip)).rgb);
    float d[6];
    float dm = 1e9;
    [unroll] for (int k = 0; k < 6; k++) { d[k] = length(f - Perc(gArm[k].Load(int3(p, 0)).rgb)); dm = min(dm, d[k]); }
    if (dm < umbral) {
      cnt++;
      [unroll] for (int k2 = 0; k2 < 6; k2++) acc[k2] += d[k2];
      col += f * f;
    }
  }
  uint o;
  InterlockedAdd(gs[0], cnt, o);
  [unroll] for (int k3 = 0; k3 < 6; k3++) InterlockedAdd(gs[1 + k3], (uint)(acc[k3] * 1000.0 + 0.5), o);
  InterlockedAdd(gs[7], (uint)(pow(col.r / max(cnt, 1), 1.0 / 2.2) * 255.0 * cnt + 0.5), o);
  InterlockedAdd(gs[8], (uint)(pow(col.g / max(cnt, 1), 1.0 / 2.2) * 255.0 * cnt + 0.5), o);
  InterlockedAdd(gs[9], (uint)(pow(col.b / max(cnt, 1), 1.0 / 2.2) * 255.0 * cnt + 0.5), o);
  GroupMemoryBarrierWithGroupSync();
  if (li < 10) gOut.Store(li * 4, gs[li]);
}
