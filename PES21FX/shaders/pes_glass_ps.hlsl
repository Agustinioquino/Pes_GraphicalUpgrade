// PES21 FX - vidrio del menu Fisico (24/10, v2), estilo "Liquid Glass" del iPhone.
// La DLL (glass.cpp) lo pone en lugar del PS de los menus 55a003da (textura t8 x color del vertice x g_mulColor + g_addColor,
// alpha test, salida x 0.25) junto con pes_glass_gs.hlsl, que ya decidio por elemento (una vez por triangulo) si es vidrio,
// panel opaco con brillo o algo que queda igual (info.y). Aca solo se dibuja:
//   - vidrio: el fondo (copia de la imagen tomada antes del primer panel: t21 nitida, t20 desenfocada) con el tinte del
//     panel, refraccion y brillo en el borde y esquinas redondeadas;
//   - panel opaco: su color con el brillo del borde y de arriba, esquinas redondeadas;
//   - lo demas: exactamente como el juego.
// La imagen del menu guarda sRGB / 4 (el compuesto final 87552b53 multiplica por 4): todo se mezcla en ese espacio.
//   189: x on | y esmerilado (0 nitido .. 1 desenfocado) | z radio del desenfoque | w tinte del panel (0..1)
//   190: x brillo del borde | y refraccion | z radio de las esquinas (px) | w brillo de arriba
Texture2D<float4> inTexture : register(t8);
SamplerState g_sampler : register(s0);
Texture2D<float4> GlassBlur : register(t20);
Texture2D<float4> GlassSharp : register(t21);
SamplerState sGlass : register(s15);
Texture1D<float4> IniParams : register(t120);
cbuffer GlassDraw : register(b13) { uint4 gDraw; };   // y = 1 si la mezcla es premultiplicada

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

float4 main(PSIn i) : SV_Target0 {
  float4 c = inTexture.Sample(g_sampler, i.uv.xy) * i.col;
  c = c * i.mulc + i.addc;
  if (c.a - i.uv.z < 0) discard;
  float4 orig = c * float4(0.25, 0.25, 0.25, 1.0);
  uint flags = (uint)(i.info.y + 0.5);
  bool frost = (flags & 16u) != 0, sheen = (flags & 32u) != 0;
  if (!frost && !sheen) return orig;

  float4 G0 = IniParams.Load(int2(189, 0)), G1 = IniParams.Load(int2(190, 0));
  uint W, H; GlassSharp.GetDimensions(W, H);
  float2 scr = float2(max(W, 1u), max(H, 1u));
  float2 p = i.pos.xy;
  float2 rmin = float2(i.rect.x * 0.5 + 0.5, 0.5 - 0.5 * i.rect.w) * scr;
  float2 rmax = float2(i.rect.z * 0.5 + 0.5, 0.5 - 0.5 * i.rect.y) * scr;
  float2 sz = rmax - rmin;
  float minSz = min(sz.x, sz.y);
  bool premul = gDraw.y != 0;
  bool shape = (flags & 64u) != 0 && (flags & 128u) != 0;   // textura entera y rectangular: forma conocida

  // forma: rectangulo con esquinas redondeadas; los lados pegados al borde de la pantalla se estiran afuera (sin borde ahi)
  float2 ra = rmin, rb = rmax;
  if (ra.x <= 1.5) ra.x = -8192.0;
  if (ra.y <= 1.5) ra.y = -8192.0;
  if (rb.x >= scr.x - 1.5) rb.x = scr.x + 8192.0;
  if (rb.y >= scr.y - 1.5) rb.y = scr.y + 8192.0;
  int touch = (int)(rmin.x <= 1.5) + (int)(rmin.y <= 1.5) + (int)(rmax.x >= scr.x - 1.5) + (int)(rmax.y >= scr.y - 1.5);
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
  // un brillo suave arriba
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
  if (shape) {
    bg.r = lerp(GlassSharp.SampleLevel(sGlass, su + sc, 0).r, GlassBlur.SampleLevel(sGlass, su + sc, 0).r, bl);
    bg.b = lerp(GlassSharp.SampleLevel(sGlass, su - sc, 0).b, GlassBlur.SampleLevel(sGlass, su - sc, 0).b, bl);
  }
  float lb = Lum(bg);
  bg = max(lerp(lb.xxx, bg, 1.18) * 1.03, 0.0);   // un poco mas de color y luz, como el material de iOS
  float a0 = min(c.a, i.info.z);
  float3 glass = lerp(bg, cs * 0.25, saturate(a0 * G0.w));
  float3 hc = lerp(float3(1, 1, 1), saturate(bg * 5.0), 0.25) * 0.25;
  glass += hi * hc;
  float outA = covR * saturate(c.a * 8.0);
  if (outA < 0.002) discard;
  return float4(premul ? glass * outA : glass, outA);
}
