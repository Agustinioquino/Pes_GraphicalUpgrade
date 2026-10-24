// PES21 FX - aro y punto blanco del musculo elegido en el menu Fisico (24/10 c), como capa de interfaz encima del jugador.
// La DLL (ring.cpp) lo dibuja justo despues del dibujo que pone la imagen del jugador en el menu (PS 765e1433, VS 537a35c9),
// con el mismo quad: TEXCOORD0.xy recorre la imagen del jugador de 0 a 1. La variante de la piel (pes_skin_hl_ps.hlsl) dejo
// en t20 (u5 en su pasada), por marca y lado, la suma de las posiciones de los pixeles visibles cerca de la marca: el
// centro del aro es el promedio y el tamano sale de la cantidad de pixeles. La imagen del menu guarda sRGB / 4.
//   174: x pulso del resaltado | 186: x latido, y desvanecido | 193: viewport de la escena (x, y, ancho, alto)
ByteAddressBuffer gHlAcc : register(t20);
Texture1D<float4> IniParams : register(t120);

float4 main(float4 uv : TEXCOORD0, float4 mc : TEXCOORD1, float4 pos : SV_Position) : SV_Target0 {
  float4 HP = IniParams.Load(int2(186, 0));
  float4 VP = IniParams.Load(int2(193, 0));
  float2 ip = uv.xy * max(VP.zw, 1.0) + VP.xy;          // pixel de la escena que cae aca
  float2 dx = ddx(ip), dy = ddy(ip);
  float sc = max(max(length(dx), length(dy)), 1e-3);     // pixeles de la escena por pixel de pantalla
  float a = 0;
  [unroll] for (int k = 0; k < 4; k++) {
    uint4 acc = gHlAcc.Load4(k * 16);
    if (acc.z >= 24) {
      float2 c = float2(acc.x, acc.y) / (float)acc.z + 0.5;
      float r = sqrt((float)acc.z / 3.14159);           // radio de la mancha visible (pixeles de la escena)
      float d = length(ip - c) / sc;                     // distancia en pixeles de pantalla
      float R = clamp(r * 1.15 / sc + 7.0, 16.0, 150.0) * (0.95 + 0.10 * HP.x);   // late como el de la cara (+- 5 %)
      float ring = 1.0 - smoothstep(1.1, 2.3, abs(d - R));
      float halo = exp(-((d - R) * (d - R)) / 12.0) * 0.30;
      float dotA = 1.0 - smoothstep(3.2, 4.8, d);
      float dotH = exp(-(d * d) / 50.0) * 0.55;
      a = max(a, max(max(ring * (0.70 + 0.30 * HP.x), halo), max(dotA, dotH)));
    }
  }
  a *= saturate(HP.y) * (VP.z > 1.0 ? 1.0 : 0.0);
  if (a < 0.003) discard;
  return float4(float3(0.92, 0.97, 1.0) * 0.25, a);
}
