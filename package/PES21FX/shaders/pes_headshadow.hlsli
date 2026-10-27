// PES21 FX (27/10 c) - sombra propia de la cabeza: atlas de sombras de alta resolucion (t30, headshadow.cpp) de hasta 3 cabezas.
// Cada cabeza tiene una caja ortografica alineada con el sol (medio lado A.w metros): la DLL vuelve a dibujar en el atlas lo que
// el juego manda a la cascada mas cercana de su pasada de sombras, con esa proyeccion. Aca se lee el atlas con la misma caja en
// espacio de vista y se devuelve la visibilidad (1 = al sol, 0 = en sombra), que multiplica la sombra del shadow map del juego.
//   309 (ini): x prendido, y fuerza, z suavidad (texels del atlas), w sesgo (texels de profundidad)
//   310 (DLL): x cantidad de cabezas, yzw direccion hacia el sol en vista | 316: xyz eje derecha de la caja en vista, w medio lado
//   317..319 (DLL): centro de cada caja en vista (la de m_projection: V = P^-1 PV); 317.w bits: 1 z invertida, 2 / 4 signo de P0 / P5
#ifndef PES_HEADSHADOW_HLSLI
#define PES_HEADSHADOW_HLSLI
Texture2D<float> PESHeadShadow : register(t30);

float PESHeadShadowAt(float3 Pv, float nDotL, SamplerState smp) {
  float4 U = IniParams.Load(int2(309, 0));
  float4 I = IniParams.Load(int2(310, 0));
  int n = (int)(I.x + 0.5);
  if (U.x < 0.5 || n <= 0) return 1.0;
  float4 A = IniParams.Load(int2(316, 0));
  int hsFl = (int)(IniParams.Load(int2(317, 0)).w + 0.5);   // bit 0 z invertida, bits 1 / 2: signo de P0 / P5 de m_projection
  Pv.x *= (hsFl & 2) ? -1.0 : 1.0; Pv.y *= (hsFl & 4) ? -1.0 : 1.0;
  float3 L = normalize(I.yzw), R = normalize(A.xyz); float hf = max(A.w, 0.1);
  float3 Up = cross(L, R);
  float rad = max(U.z, 0.0) / 2048.0;                                   // suavidad en texels del atlas
  float bias = max(U.w, 0.0) / 1024.0 * (0.5 + 1.5 * (1.0 - saturate(nDotL)));   // sesgo: mas en angulo rasante
  float vis = 1.0;
  [unroll] for (int i = 0; i < 3; i++) {
    if (i >= n) break;
    float4 C = IniParams.Load(int2(317 + i, 0));
    float3 d = Pv - C.xyz;
    float x = dot(d, R) / hf, y = dot(d, Up) / hf, z = dot(d, -L) / (2.0 * hf) + 0.5;
    if (abs(x) > 1.0 || abs(y) > 1.0 || z < 0.0 || z > 1.0) continue;
    float2 uv = float2((x * 0.5 + 0.5) * 0.5 + (i & 1) * 0.5, (0.5 - y * 0.5) * 0.5 + (i >> 1) * 0.5);
    bool rev = (hsFl & 1) != 0;
    float lit = 0.0;
    [unroll] for (int ty = 0; ty < 4; ty++) [unroll] for (int tx = 0; tx < 4; tx++) {
      float2 o = (float2(tx, ty) - 1.5) * rad;
      float s = PESHeadShadow.SampleLevel(smp, uv + o, 0);
      if (rev) s = 1.0 - s;
      lit += (s + bias > z) ? 1.0 : 0.0;
    }
    lit /= 16.0;
    float edge = 1.0 - saturate((max(abs(x), abs(y)) - 0.85) / 0.15);   // fundido en el borde de la caja
    vis = min(vis, lerp(1.0, lit, edge));
  }
  return lerp(1.0, vis, saturate(U.y));
}
#endif
