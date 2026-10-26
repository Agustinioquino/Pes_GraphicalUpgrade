// PES21 FX - aro y punto blanco del musculo elegido, dibujado en Present sobre la imagen final (24/10 e): pantalla
// completa (pes_fs_vs.hlsl), el pixel de la escena es el de pantalla (la escena del menu Fisico se dibuja en el mismo lugar
// en que se ve: x193..w193 = viewport, x185..w185 = rectangulo en pantalla). Lee las marcas que anoto ESTE cuadro (ring.cpp), asi no se atrasa al mover la camara.
ByteAddressBuffer gHlAcc : register(t20);
Texture1D<float4> IniParams : register(t120);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float4 HP = IniParams.Load(int2(186, 0));
  float4 VP = IniParams.Load(int2(193, 0));
  float4 S = IniParams.Load(int2(185, 0));               // rectangulo de pantalla donde se ve la escena (ring.cpp)
  if (pos.x < S.x || pos.y < S.y || pos.x > S.x + S.z || pos.y > S.y + S.w) discard;
  float2 sc2 = VP.zw / max(S.zw, 1.0);                   // pixeles de la escena por pixel de pantalla
  float2 ip = (pos.xy - S.xy) * sc2 + VP.xy;             // pixel de la escena que cae aca
  float sc = max(max(sc2.x, sc2.y), 1e-3);
  float a = 0;
  [unroll] for (int k = 0; k < 4; k++) {
    uint4 acc = gHlAcc.Load4(k * 16);
    if (acc.z >= 24) {
      float2 c = float2(acc.x, acc.y) / (float)acc.z + 0.5;
      float r = sqrt((float)acc.z / 3.14159);           // radio de la mancha visible (pixeles de la escena)
      float d = length(ip - c) / sc;                     // distancia en pixeles de pantalla
      float R = clamp(r * 1.15 / sc + 7.0, 16.0, 150.0) * (0.95 + 0.10 * HP.x);   // late como el de la cara (+- 5 %)
      // (24/10 d) semitransparente y con el latido lento (HP.x sube y baja en w183 segundos), sin parpadeo
      float ring = 1.0 - smoothstep(1.1, 2.3, abs(d - R));
      float halo = exp(-((d - R) * (d - R)) / 12.0) * 0.18;
      float dotA = 1.0 - smoothstep(3.2, 4.8, d);
      float dotH = exp(-(d * d) / 50.0) * 0.40;
      a = max(a, max(max(ring * (0.40 + 0.25 * HP.x), halo), max(dotA * (0.65 + 0.25 * HP.x), dotH)));
    }
  }
  a *= saturate(HP.y) * (VP.z > 1.0 ? 1.0 : 0.0);
  if (a < 0.003) discard;
  return float4(float3(0.92, 0.97, 1.0), a);   // imagen final (ya en sRGB)
}
