// PES21 FX (27/10 c) - sombra propia de la cabeza: atlas de sombras de alta resolucion (t30, headshadow.cpp) de hasta 3 cabezas.
// Cada cabeza tiene una caja ortografica alineada con el sol (medio lado A.w metros): la DLL vuelve a dibujar en el atlas lo que
// el juego manda a la cascada mas cercana de su pasada de sombras, con esa proyeccion. Aca se lee el atlas con la misma caja en
// espacio de vista y se devuelve la visibilidad (1 = al sol, 0 = en sombra), que multiplica la sombra del shadow map del juego.
//   309 (ini): x prendido, y fuerza, z suavidad (texels del atlas), w sesgo (texels de profundidad)
//   310 (DLL): x cantidad de cabezas, yzw direccion hacia el sol en vista | 316: xyz eje derecha de la caja en vista, w medio lado
//   317..319 (DLL): centro de cada caja en vista (la de m_projection: V = P^-1 PV); 317.w bits: 1 z invertida, 2 / 4 signo de P0 / P5, 8 vista reflejada
#ifndef PES_HEADSHADOW_HLSLI
#define PES_HEADSHADOW_HLSLI
Texture2D<float> PESHeadShadow : register(t30);

// (02/11) vis = visibilidad del atlas; inBox = 1 dentro de la caja de alguna cabeza (con el fundido del borde). Dentro de la caja la
// sombra del mapa del estadio se REEMPLAZA por la del atlas (PESHeadShadowMix): el mapa tiene 2 cm por texel y con sombra_parche 0
// gira con la camara, y en los primeros planos dejaba bloques oscuros corridos en el menton y el cuello (capturas de Agustin 02/11)
float PESHeadShadowAt2(float3 Pv, float nDotL, SamplerState smp, out float inBox) {
  inBox = 0.0;
  float4 U = IniParams.Load(int2(309, 0));
  float4 I = IniParams.Load(int2(310, 0));
  int n = (int)(I.x + 0.5);
  if (U.x < 0.5 || n <= 0) return 1.0;
  float4 A = IniParams.Load(int2(316, 0));
  int hsFl = (int)(IniParams.Load(int2(317, 0)).w + 0.5);   // bit 0 z invertida, bits 1 / 2: signo de P0 / P5 de m_projection
  Pv.x *= (hsFl & 2) ? -1.0 : 1.0; Pv.y *= (hsFl & 4) ? -1.0 : 1.0;
  float3 L = normalize(I.yzw), R = normalize(A.xyz); float hf = max(A.w, 0.1);
  float3 Up = cross(L, R) * ((hsFl & 8) ? -1.0 : 1.0);   // (27/10 f) bit 8: la vista es un reflejo, el producto cruz sale al reves
  // (02/11) penumbra en metros (antes 4x4 en grilla fija de ~1 texel = 1.8 mm: borde duro y escalonado en barba, mandibula y frente,
  // capturas de Agustin): busqueda del oclusor + PCF de 16 muestras en disco de Poisson girado por pixel y por cuadro (el TAA las junta).
  // Radio = z309 x (6 mm + 6 % de la distancia al oclusor), entre 4 mm y 3 cm: z309 = 1 -> ~1.2 cm con el menton a 10 cm del cuello.
  float soft = max(U.z, 0.0);
  float bias = max(U.w, 0.0) / 1024.0 * (0.5 + 1.5 * (1.0 - saturate(nDotL)));   // sesgo: mas en angulo rasante
  float uvPerM = 0.25 / hf;                                                     // la caja (2 hf metros) ocupa medio atlas
  float rot = 6.2831853 * frac(52.9829189 * frac(dot(Pv.xy * 731.0 + Pv.z * 113.0, float2(0.06711056, 0.00583715))) + IniParams.Load(int2(155, 0)).y * 0.618034);
  float2 cs = float2(cos(rot), sin(rot));
  static const float2 kPD[16] = { float2(-0.613, 0.617), float2(0.170, -0.040), float2(-0.299, 0.791), float2(0.645, 0.493),
    float2(-0.651, 0.718), float2(0.421, 0.027), float2(-0.817, -0.271), float2(-0.705, -0.668), float2(0.977, -0.108),
    float2(0.063, -0.966), float2(0.507, -0.856), float2(0.402, 0.744), float2(-0.329, -0.115), float2(-0.051, 0.358),
    float2(0.743, -0.415), float2(-0.290, -0.640) };
  float vis = 1.0;
  [unroll] for (int i = 0; i < 3; i++) {
    if (i >= n) break;
    float4 C = IniParams.Load(int2(317 + i, 0));
    float3 d = Pv - C.xyz;
    float x = dot(d, R) / hf, y = dot(d, Up) / hf, z = dot(d, -L) / (2.0 * hf) + 0.5;
    if (abs(x) > 1.0 || abs(y) > 1.0 || z < 0.0 || z > 1.0) continue;
    float2 uv = float2((x * 0.5 + 0.5) * 0.5 + (i & 1) * 0.5, (0.5 - y * 0.5) * 0.5 + (i >> 1) * 0.5);
    float2 lo = float2((i & 1) * 0.5, (i >> 1) * 0.5) + 1.0 / 2048.0, hi = lo + 0.5 - 2.0 / 2048.0;   // sin salir del cuadro de esta cabeza
    bool rev = (hsFl & 1) != 0;
    // 1) oclusor medio en un disco de 3 cm (el radio maximo del filtro)
    float bSum = 0.0, bN = 0.0;
    [unroll] for (int b = 0; b < 8; b++) {
      float2 o = kPD[b * 2]; o = float2(o.x * cs.x - o.y * cs.y, o.x * cs.y + o.y * cs.x);
      float s = PESHeadShadow.SampleLevel(smp, clamp(uv + o * 0.03 * uvPerM, lo, hi), 0);
      if (rev) s = 1.0 - s;
      if (s + bias < z) { bSum += s; bN += 1.0; }
    }
    float lit = 1.0;
    if (bN > 0.0) {
      float dBlk = max(z - bSum / bN, 0.0) * 2.0 * hf;                         // metros entre la piel y lo que la tapa
      float rM = clamp(soft * (0.006 + 0.06 * dBlk), 0.004, 0.03);
      lit = 0.0;
      [unroll] for (int k = 0; k < 16; k++) {
        float2 o = kPD[k]; o = float2(o.x * cs.x - o.y * cs.y, o.x * cs.y + o.y * cs.x);
        float s = PESHeadShadow.SampleLevel(smp, clamp(uv + o * rM * uvPerM, lo, hi), 0);
        if (rev) s = 1.0 - s;
        lit += (s + bias > z) ? 1.0 : 0.0;
      }
      lit /= 16.0;
    }
    // (02/11) fundido largo (60 % a 100 % del medio lado): entre el atlas y el mapa del estadio no queda corte en el cuello
    float edge = 1.0 - saturate((max(abs(x), abs(y)) - 0.6) / 0.4);
    vis = min(vis, lerp(1.0, lit, edge));
    inBox = max(inBox, edge);
  }
  return lerp(1.0, vis, saturate(U.y));
}
float PESHeadShadowAt(float3 Pv, float nDotL, SamplerState smp) { float ib; return PESHeadShadowAt2(Pv, nDotL, smp, ib); }
// sombra final del sol (rgb) a partir de la del mapa del estadio: fuera de la caja mapa x atlas, dentro solo el atlas
float3 PESHeadShadowMix(float3 mapShadow, float3 Pv, float nDotL, SamplerState smp) {
  float ib; float v = PESHeadShadowAt2(Pv, nDotL, smp, ib);
  float k = ib * saturate(IniParams.Load(int2(309, 0)).y);
  return lerp(mapShadow * v, v.xxx, k);
}
#endif
