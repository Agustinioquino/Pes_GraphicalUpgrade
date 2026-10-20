// PES21 FX - pasto v3 (19/10): funciones compartidas entre la base de la cancha (5e92 / c956), las hojas 3D
// (498b VS + 18af PS) y lo que haga falta. Se incluye DESPUES de declarar IniParams.
//   104: x on | y tipo (0 corto, 1 medio, 2 largo) | z alto | w desgaste (0 profesional .. 1 potrero)
//   105: x patron de corte (0 del juego, 1 franjas a lo largo, 2 a lo ancho, 3 damero, 4 diagonal, 5 rombos, 6 circulos)
//        y ancho de la franja (m) | z contraste del corte | w raiz del atlas (0 abajo, 1 arriba)
//   106: x variacion de color por zonas | y sombra entre hojas | z brillo | w relieve por hoja
//   107: x desgaste en las zonas de uso | y amarillo seco | z pelado (tierra) | w depuracion
#ifndef PES_GRASS_HLSLI
#define PES_GRASS_HLSLI

float PESGh21(float2 p) { p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }
float PESGnoise(float2 p) {
  float2 i = floor(p), f = frac(p); f = f * f * (3.0 - 2.0 * f);
  return lerp(lerp(PESGh21(i), PESGh21(i + float2(1, 0)), f.x), lerp(PESGh21(i + float2(0, 1)), PESGh21(i + 1.0), f.x), f.y);
}

// uv de la cancha -> metros desde el centro. La textura de la cancha (4096x2048) cubre unos 140 x 79 m: la cancha
// de 105 x 68 ocupa u 0.125..0.875 y v 0.07..0.93 (medido en el mapa de lineas del juego). u = largo (x del mundo).
float2 PESPitchMeters(float2 uv) { return (uv - 0.5) * float2(140.0, 79.0); }

float PESBandSign(float a, float w) { return frac(floor(a / w) * 0.5) < 0.25 ? 1.0 : -1.0; }

// patron de corte: el "grano" (vector hacia donde esta peinada la hoja, espacio mundo: x largo de la cancha, z ancho,
// y arriba) para el punto pm (metros). gIn = el del juego (textura de grano). lean = inclinacion (0.55 = la del juego)
float3 PESMowGrain(float2 pm, float3 gIn, float pat, float widthM, float lean)
{
  int p = (int)(pat + 0.5);
  if (p <= 0) return gIn;
  float w = max(widthM, 0.5);
  float2 dir = float2(1.0, 0.0); float s = 1.0;
  if (p == 1) { dir = float2(1.0, 0.0); s = PESBandSign(pm.y, w); }                 // franjas a lo largo: bandas apiladas en z
  else if (p == 2) { dir = float2(0.0, 1.0); s = PESBandSign(pm.x, w); }            // a lo ancho: bandas apiladas en x
  else if (p == 3) { dir = normalize(float2(PESBandSign(pm.y, w), PESBandSign(pm.x, w))); }   // damero (como el del juego)
  else if (p == 4) { float2 d = float2(0.70710678, 0.70710678); dir = d; s = PESBandSign(dot(pm, float2(0.70710678, -0.70710678)), w); }
  else if (p == 5) { // rombos: dos cortes diagonales cruzados
    float s1 = PESBandSign(dot(pm, float2(0.70710678, -0.70710678)), w), s2 = PESBandSign(dot(pm, float2(0.70710678, 0.70710678)), w);
    dir = normalize(float2(0.70710678, 0.70710678) * s1 + float2(0.70710678, -0.70710678) * s2); }
  else { float r = max(length(pm), 0.01); dir = float2(-pm.y, pm.x) / r; s = PESBandSign(r, w); }   // circulos concentricos
  float2 g = dir * s * lean;
  return normalize(float3(g.x, 1.0, g.y));
}

// desgaste: wear 0 = cancha profesional .. 1 = potrero. Devuelve x = desgaste de color (seco, 0..1), y = pelado (tierra, 0..1).
// Las manchas van en metros del mundo (wxz) asi la base y las hojas coinciden; las zonas de mas uso (bocas de arco,
// circulo central) se marcan en metros de la cancha (pm).
float2 PESWear(float2 pm, float2 wxz, float wear, float areas, float bald)
{
  if (wear <= 0.001) return float2(0.0, 0.0);
  float n1 = PESGnoise(wxz * 0.22 + 3.1), n2 = PESGnoise(wxz * 0.9 + 7.7), n3 = PESGnoise(wxz * 3.3 + 1.3);
  float n = n1 * 0.55 + n2 * 0.3 + n3 * 0.15;
  float2 ga = abs(pm) - float2(52.5 - 5.0, 0.0);
  float goal = exp(-dot(ga * ga, float2(1.0 / 36.0, 1.0 / 81.0)));
  float centre = exp(-dot(pm * pm, float2(1.0 / 81.0, 1.0 / 49.0)));
  float use = saturate(goal + centre * 0.8) * saturate(areas);
  float k = saturate(wear * (0.6 + 0.8 * use));
  float col = saturate((n - 0.5 + k * 0.9) * 1.6);
  float thr = 1.0 - k * 0.7 * saturate(bald);
  float bare = smoothstep(thr - 0.12, thr + 0.02, n + use * 0.25) * (bald > 0.001 ? 1.0 : 0.0);
  return float2(col, bare);
}

// variacion de color por zonas (manchas de 2-6 m mas verdes / mas palidas, y detalle de 0.5 m): multiplicador rgb
float3 PESGrassZones(float2 wxz, float k)
{
  if (k <= 0.001) return float3(1.0, 1.0, 1.0);
  float m1 = PESGnoise(wxz * 0.045), m2 = PESGnoise(wxz * 0.16 + 5.3), m3 = PESGnoise(wxz * 0.85 + 1.7);
  float macro = (m1 * 0.55 + m2 * 0.3 + m3 * 0.15) - 0.5;
  float hue = m2 - 0.5;
  float3 t = (1.0 + macro * 0.28 * k) * lerp(float3(1.0, 1.0, 1.0), float3(1.10, 1.0, 0.82), hue * 0.9 * k);
  return t;
}

// color seco (amarillo pajizo) y tierra, con la luminancia del pasto
float3 PESDryTint(float3 c, float dry) {
  float l = dot(c, float3(0.2126, 0.7152, 0.0722));
  return lerp(c, l * float3(1.22, 1.05, 0.52), saturate(dry));
}
float3 PESDirt(float2 wxz) {
  float n = PESGnoise(wxz * 6.0 + 2.2) * 0.5 + PESGnoise(wxz * 23.0) * 0.5;
  return float3(0.34, 0.26, 0.16) * (0.75 + 0.5 * n);
}
// (20/10) estado del suelo: x = humedo (0..1), y = seco (0..1). z110: 0 automatico (lluvia del partido via Sider -> humedo),
// 1 normal, 2 humedo, 3 seco; w110 fuerza. La DLL publica en 114.x el estado automatico (0 normal, 1 humedo, 2 seco)
float2 PESGround()
{
  float4 Q = IniParams.Load(int2(110, 0)); float4 L = IniParams.Load(int2(114, 0));
  int m = (int)(Q.z + 0.5); float wet = 0.0, dry = 0.0;
  if (m == 0) { int a = (int)(L.x + 0.5); wet = a == 1 ? 1.0 : 0.0; dry = a == 2 ? 1.0 : 0.0; }
  else if (m == 2) wet = 1.0; else if (m == 3) dry = 1.0;
  float k = Q.w > 0.001 ? saturate(Q.w) : 1.0;
  return float2(wet * k, dry * k);
}
// pasto mojado: mas oscuro y saturado, brillo de agua; seco: pajizo y mate. c albedo, spec = o2.y, rough = o2.x
void PESGroundLook(inout float3 c, inout float spec, inout float rough, float2 g)
{
  if (g.x > 0.001) { c = lerp(c, pow(max(c, 1e-4), 1.25) * 0.72, g.x); spec = lerp(spec, saturate(spec * 2.2 + 0.25), g.x); rough = lerp(rough, max(0.14, rough * 0.6), g.x); }
  if (g.y > 0.001) { c = PESDryTint(c, g.y * 0.4); spec = lerp(spec, spec * 0.7, g.y); }
}
// sol en espacio mundo (lo publica la DLL en 111: xyz hacia el sol, w altura en grados); (0,1,0) si todavia no hay dato
float3 PESSunWorld() { float4 L = IniParams.Load(int2(111, 0)); return dot(L.xyz, L.xyz) > 0.5 ? L.xyz : float3(0.0, 1.0, 0.0); }
#endif
