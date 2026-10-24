// PES21 FX - vidrio de los menus del juego (24/10), estilo "Liquid Glass" del iPhone.
// La DLL (glass.cpp) lo pone en lugar del PS de los menus 55a003da (textura t8 x color del vertice x g_mulColor + g_addColor,
// alpha test, salida x 0.25) junto con pes_glass_gs.hlsl, que le pasa el rectangulo del elemento en pantalla.
// Para cada elemento (quad) mira su textura en una grilla de 5x5 y decide:
//   - panel translucido y parejo -> vidrio: se ve el fondo (copia de la imagen tomada antes del panel: t21 nitida,
//     t20 desenfocada) con el tinte del panel, refraccion y brillo en el borde y esquinas redondeadas;
//   - panel opaco grande y parejo -> brillo en el borde y arriba, esquinas redondeadas (menos opaco si x191 < 1);
//   - texto, iconos, fotos, degradados, sombras suaves -> igual que el juego.
// La imagen del menu guarda sRGB / 4 (el compuesto final 87552b53 multiplica por 4): todo se mezcla en ese espacio.
// u1: por dibujo (b13.x = numero en la pasada) se anota el rectangulo y lo que se hizo. La DLL lo lee unos cuadros despues
// para saber donde copiar el fondo (antes del primer panel de vidrio y antes de cada panel que pisa algo dibujado despues
// de la ultima copia) y para el log.
//   189: x on | y esmerilado (0 nitido .. 1 desenfocado) | z radio del desenfoque | w tinte del panel (0..1)
//   190: x brillo del borde | y refraccion | z radio de las esquinas (px) | w brillo de arriba
//   191: x opacidad de los paneles opacos (1 = igual) | y tamano minimo para el brillo (px) | z tolerancia "parejo" | w log
Texture2D<float4> inTexture : register(t8);
SamplerState g_sampler : register(s0);
Texture2D<float4> GlassBlur : register(t20);
Texture2D<float4> GlassSharp : register(t21);
SamplerState sGlass : register(s15);
Texture1D<float4> IniParams : register(t120);
cbuffer GlassDraw : register(b13) { uint4 gDraw; };   // x = numero de dibujo en la pasada (>= 1024: no anotar), y = 1 si la mezcla es premultiplicada
RWStructuredBuffer<uint4> gRec : register(u1);

struct PSIn {
  float3 uv : TEXCOORD0;
  float4 mulc : TEXCOORD1;
  float4 addc : TEXCOORD2;
  float4 col : COLOR0;
  nointerpolation float4 rect : TEXCOORD3;
  nointerpolation float4 info : TEXCOORD4;
  float4 pos : SV_Position;
};

float Lum(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float Max3(float3 v) { return max(v.x, max(v.y, v.z)); }
uint Pack2(float2 v) { uint2 u = (uint2)clamp(v, 0.0, 65535.0); return u.x | (u.y << 16); }
float4 Elem(float2 uv, float4 col, float4 mulc, float4 addc) { return inTexture.SampleLevel(g_sampler, uv, 0) * col * mulc + addc; }

float4 main(PSIn i) : SV_Target0 {
  float2 p = i.pos.xy;
  float2 dux = ddx(i.uv.xy), duy = ddy(i.uv.xy);   // el quad es afin: uv lineal en la pantalla
  float4 c = inTexture.Sample(g_sampler, i.uv.xy) * i.col;
  c = c * i.mulc + i.addc;
  if (c.a - i.uv.z < 0) discard;
  float4 orig = c * float4(0.25, 0.25, 0.25, 1.0);

  float4 G0 = IniParams.Load(int2(189, 0)), G1 = IniParams.Load(int2(190, 0)), G2 = IniParams.Load(int2(191, 0));
  uint W, H; GlassSharp.GetDimensions(W, H);
  float2 scr = float2(max(W, 1u), max(H, 1u));
  float2 rmin = float2(i.rect.x * 0.5 + 0.5, 0.5 - 0.5 * i.rect.w) * scr;
  float2 rmax = float2(i.rect.z * 0.5 + 0.5, 0.5 - 0.5 * i.rect.y) * scr;
  float2 sz = rmax - rmin;
  float minSz = min(sz.x, sz.y);
  uint k = gDraw.x;
  bool premul = gDraw.y != 0;
  bool rec = k < 1024 && ((uint)p.x & 3) == 0 && ((uint)p.y & 3) == 0;   // una muestra cada 4x4 pixeles alcanza
  uint flags = 1u;   // 1 dibujado
  if (i.info.x < 0.5 || minSz < 6.0 || W == 0 || G0.x < 0.5) {
    if (rec) { gRec[k].x = Pack2(rmin); gRec[k].y = Pack2(rmax); InterlockedOr(gRec[k].z, flags | (i.info.x > 0.5 ? 2u : 0u)); }
    return orig;
  }
  flags |= 2u;   // 2 rectangulo alineado

#define UVQ(q) (i.uv.xy + dux * ((q).x - p.x) + duy * ((q).y - p.y))
  // grilla de 5x5 sobre el elemento (las mismas muestras para todos sus pixeles: la decision es por elemento)
  float4 g[25];
  [unroll] for (int gy = 0; gy < 5; gy++) {
    [unroll] for (int gx = 0; gx < 5; gx++) {
      float2 q = lerp(rmin, rmax, (float2(gx, gy) + 0.5) * 0.2);
      g[gy * 5 + gx] = Elem(UVQ(q), i.col, i.mulc, i.addc);
    }
  }
  float nIn = 0, aS = 0, lS = 0, adj = 0;
  [unroll] for (int n = 0; n < 25; n++) {
    if (g[n].a > 0.02) { nIn += 1.0; aS += g[n].a; lS += Lum(g[n].rgb); }
  }
  [unroll] for (int ry = 0; ry < 5; ry++) {
    [unroll] for (int rx = 0; rx < 4; rx++) {
      float4 a0 = g[ry * 5 + rx], a1 = g[ry * 5 + rx + 1];
      if (a0.a > 0.02 && a1.a > 0.02) adj = max(adj, max(abs(a0.a - a1.a), Max3(abs(a0.rgb - a1.rgb))));
    }
  }
  [unroll] for (int cy = 0; cy < 4; cy++) {
    [unroll] for (int cx = 0; cx < 5; cx++) {
      float4 b0 = g[cy * 5 + cx], b1 = g[cy * 5 + cx + 5];
      if (b0.a > 0.02 && b1.a > 0.02) adj = max(adj, max(abs(b0.a - b1.a), Max3(abs(b0.rgb - b1.rgb))));
    }
  }
  // sombras / brillos suaves: el alfa cae hacia los cuatro bordes
  float2 mid = (rmin + rmax) * 0.5;
  float eA = max(max(Elem(UVQ(float2(mid.x, lerp(rmin.y, rmax.y, 0.03))), i.col, i.mulc, i.addc).a,
                     Elem(UVQ(float2(mid.x, lerp(rmin.y, rmax.y, 0.97))), i.col, i.mulc, i.addc).a),
                 max(Elem(UVQ(float2(lerp(rmin.x, rmax.x, 0.03), mid.y)), i.col, i.mulc, i.addc).a,
                     Elem(UVQ(float2(lerp(rmin.x, rmax.x, 0.97), mid.y)), i.col, i.mulc, i.addc).a));
  bool soft = g[12].a > 0.05 && eA < 0.5 * g[12].a;
  // la textura entera en el quad (no un pedazo de un atlas o de un panel de 9 partes)
  float2 uvA = UVQ(rmin), uvB = UVQ(rmax);
  float2 uvLo = min(uvA, uvB), uvHi = max(uvA, uvB);
  bool fullUV = uvLo.x < 0.02 && uvLo.y < 0.02 && uvHi.x > 0.98 && uvHi.y > 0.98;

  float aM = aS / max(nIn, 1.0), lM = lS / max(nIn, 1.0);
  bool flat = adj < G2.z && !soft;
  bool rectish = g[0].a > 0.02 && g[4].a > 0.02 && g[20].a > 0.02 && g[24].a > 0.02;
  bool transl = aM < 0.93;
  int touch = (int)(rmin.x <= 1.5) + (int)(rmin.y <= 1.5) + (int)(rmax.x >= scr.x - 1.5) + (int)(rmax.y >= scr.y - 1.5);
  bool frost = flat && nIn >= 13.0 && transl;
  bool sheen = flat && nIn >= 25.0 && !transl && fullUV && minSz >= G2.y && touch < 2;
  float a0 = c.a;
  if (sheen && G2.x < 0.93) { frost = true; sheen = false; a0 = min(a0, G2.x); }   // paneles opacos que se vuelven vidrio
  flags |= (flat ? 4u : 0u) | (transl ? 8u : 0u) | (frost ? 16u : 0u) | (sheen ? 32u : 0u) | (fullUV ? 64u : 0u) | (rectish ? 128u : 0u) | (soft ? 256u : 0u);
  if (rec) {
    gRec[k].x = Pack2(rmin); gRec[k].y = Pack2(rmax);
    InterlockedOr(gRec[k].z, flags);
    gRec[k].w = (uint)(saturate(aM) * 255.0) | ((uint)(saturate(lM) * 255.0) << 8) | ((uint)(saturate(adj) * 255.0) << 16) | ((uint)nIn << 24);
  }
  if (!frost && !sheen) return orig;

  // forma: rectangulo con esquinas redondeadas (solo si el quad es la textura entera y es rectangular); los lados pegados al
  // borde de la pantalla se estiran afuera (sin borde brillante ni esquina ahi)
  bool shape = fullUV && rectish;
  float2 ra = rmin, rb = rmax;
  if (ra.x <= 1.5) ra.x = -8192.0;
  if (ra.y <= 1.5) ra.y = -8192.0;
  if (rb.x >= scr.x - 1.5) rb.x = scr.x + 8192.0;
  if (rb.y >= scr.y - 1.5) rb.y = scr.y + 8192.0;
  float2 cc = (ra + rb) * 0.5, hh = (rb - ra) * 0.5;
  float R = clamp(G1.z, 0.0, 0.5 * minSz);
  float2 dp = p - cc;
  float2 sg = float2(dp.x >= 0 ? 1.0 : -1.0, dp.y >= 0 ? 1.0 : -1.0);
  float2 q2 = abs(dp) - hh + R;
  float d = length(max(q2, 0.0)) + min(max(q2.x, q2.y), 0.0) - R;
  float2 nq = (q2.x > 0.0 || q2.y > 0.0) ? normalize(max(q2, 0.0) + 1e-4) : (q2.x > q2.y ? float2(1, 0) : float2(0, 1));
  float2 nrm = nq * sg;                 // normal hacia afuera
  float e = shape ? -d : 1e4;           // distancia al borde hacia adentro (px)
  float covR = shape ? saturate(0.5 - d) : 1.0;

  // brillos: borde fino iluminado desde arriba a la izquierda (con un reflejo mas debil del lado opuesto), banda interior y
  // un brillo suave en la parte de arriba
  float Wb = clamp(minSz * 0.22, 3.0, 22.0);
  float tE = saturate(e / Wb);
  float hi = 0;
  if (shape) {
    float2 L = float2(-0.5547, -0.8321);
    float lit = saturate(dot(nrm, L)), back = saturate(-dot(nrm, L));
    float rim = 1.0 - smoothstep(0.0, 1.5, e);
    float band = (1.0 - tE) * (1.0 - tE) * (1.0 - tE);
    hi = G1.x * (rim * (0.30 + 0.70 * lit + 0.40 * back) * 0.9 + band * (0.10 + 0.30 * lit) * 0.6);
    if (touch == 0) hi += G1.w * 0.12 * (1.0 - smoothstep(0.0, 0.6, saturate((p.y - rmin.y) / max(sz.y, 1.0))));
  }
  float3 cs = premul ? c.rgb / max(c.a, 1e-3) : c.rgb;   // color del elemento sin premultiplicar (sRGB)

  if (!frost) {   // panel opaco: su color + brillos
    float3 hcS = lerp(float3(1, 1, 1), saturate(cs * 1.25), 0.25);
    float aO = c.a * covR;
    if (aO < 0.002) discard;
    float3 rgbO = cs * 0.25 + hi * hcS * 0.25;
    return float4(premul ? rgbO * aO : rgbO, aO);
  }

  // vidrio: fondo con refraccion cerca del borde (como un lente grueso: se ve lo de afuera) y una dispersion leve de color
  float prof = (1.0 - tE) * (1.0 - tE);
  float2 off = shape ? nrm * (G1.y * Wb * 0.8 * prof) : float2(0, 0);
  float2 su = (p + off) / scr, sc = off * 0.18 / scr;
  float bl = saturate(G0.y);
  float3 bg = lerp(GlassSharp.SampleLevel(sGlass, su, 0).rgb, GlassBlur.SampleLevel(sGlass, su, 0).rgb, bl);
  bg.r = lerp(GlassSharp.SampleLevel(sGlass, su + sc, 0).r, GlassBlur.SampleLevel(sGlass, su + sc, 0).r, bl);
  bg.b = lerp(GlassSharp.SampleLevel(sGlass, su - sc, 0).b, GlassBlur.SampleLevel(sGlass, su - sc, 0).b, bl);
  float lb = Lum(bg);
  bg = max(lerp(lb.xxx, bg, 1.18) * 1.03, 0.0);   // un poco mas de color y luz, como el material de iOS
  float3 glass = lerp(bg, cs * 0.25, saturate(a0 * G0.w));
  float3 hc = lerp(float3(1, 1, 1), saturate(bg * 5.0), 0.25) * 0.25;
  glass += hi * hc;
  float outA = covR * saturate(c.a * 8.0);
  if (outA < 0.002) discard;
  return float4(premul ? glass * outA : glass, outA);
}
