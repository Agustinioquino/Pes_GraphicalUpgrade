// PES21 FX (09/11, Agustin: "mejorar las arrugas ... la textura y como se aplica", "las custom y las genericas que se aplican cuando
// cualquier jugador reacciona o se expresa", "la edad tiene que importar como valor") ARRUGAS POR GESTO CON PROFUNDIDAD.
// Antes: un solo mapa de normales con TODAS las arrugas (el del juego agrandado) que se prendia por zonas con manchas borrosas, de 0 a
// 100 % como una calcomania; lo oscuro del surco salia de la divergencia de las normales (ruido) en 4 capas. Ahora (x15 = 2):
//   t45 pes_wrinkle_depth_a.dds  R frente  G entrecejo + nariz  B patas de gallo + bajo el ojo  A surco nasogeniano
//   t46 pes_wrinkle_depth_b.dds  R menton + labio de arriba  G marioneta  B cuello  A mejilla bajo el ojo
// (tools/make_wrinkle_depth.py: surcos con la anatomia, 0 = liso .. 1 = fondo, perfil gaussiano ancho). Cada gesto marca SOLO sus
// arrugas y la arruga SE FORMA: con poco gesto el perfil queda ancho y bajo (hendidura suave), con el gesto entero se afina y se
// hunde (h ^ (1 + 2.5 w): el ancho baja a ~la mitad). De la misma profundidad salen la normal, el fondo del surco, la piel juntada
// entre surcos (brillo / SSS) y el volumen de la teselacion. La edad (la DLL la lee de la base de datos del juego, el VS la pasa en
// pR.w = 11 + edad / 100) deja arrugas leves en reposo en los veteranos y marca mas los gestos.
#ifndef PES_WRDEPTH_HLSLI
#define PES_WRDEPTH_HLSLI
Texture2D<float4> pesWrDA : register(t45);
Texture2D<float4> pesWrDB : register(t46);
// (10/11 d, Agustin: "tres mapas para la edad real, cada uno con menos secciones y menor intensidad") arrugas en reposo por edad:
// R joven, G adulto, B veterano; relieve con signo (0.5 = liso), (muestra - 0.5) * kWrAgeMm = mm. Sin enlazar (DLL vieja) = nada
Texture2D<float4> pesWrAgeT : register(t47);
static const float kWrAgeMm = 2.262;
static float g_wrAgeK = 0.3;   // edad 0..1 de esta cara (PesWrAge)
// (10/11 h, Agustin: "hay jugadores que tienen arrugas bakeadas ... un slider por jugador para bajar la intensidad o la zona asi como la
// edad") valores de ESTE jugador (arrugasjug.cpp, PES21FX_hair.ini [arrugas_cara]); sin enlazar todo vale 0 y wrPlC.x = 0 = como todos
#ifdef PES_WRD_NO_PS
cbuffer PESWrPlayer : register(b9)
#else
cbuffer PESWrPlayer : register(b13)
#endif
{
  float4 wrPlA;   // intensidad de los gestos, frente + entrecejo, ojos, surco + mejilla
  float4 wrPlB;   // menton + marioneta, cuello, anios de piel (+ / -), arrugas de reposo (edad)
  float4 wrPlC;   // x = 1 si hay valores
}
static float g_wrSeed = 0;   // rasgo propio del jugador 1..7 (0 = sin dato: sin variacion)
static float4 g_wrGA = 1, g_wrGB = 1;   // ganancia por zona segun la edad (PesWrAge)
static float3 g_wrAg = 0;   // mm por mapa (joven, adulto, veterano), lo arma PesWrAge
// (10/11, Agustin: "la escala esta mal", "tomando de base las remasterizadas") texturas nuevas sacadas de la normal remasterizada
// (tools/make_wrinkle_depth_remaster.py): pliegues 2 veces mas anchos y en las zonas del juego; la profundidad da con el gesto entero
// la misma pendiente que las remasterizadas
static const float4 kWrDmmA = float4(3.44, 3.91, 3.08, 3.64);   // (10/11 d) relieve entero (surcos + piel juntada) en su zona anatomica, 2048   // (10/11) entrecejo 0.50 -> 0.75: en el video el enojo a 1.42 casi no se veia
//   // profundidad maxima (mm) por canal: frente, entrecejo, ojos, surco
static const float4 kWrDmmB = float4(2.20, 3.13, 4.32, 2.29);   // menton, marioneta, cuello, mejilla
bool PesWrDOk() { uint w = 0, h = 0; pesWrDA.GetDimensions(w, h); return w > 1; }
float PesWrHash(float i) { return frac(sin((g_wrSeed + 1.0) * 12.9898 + i * 78.233) * 43758.5453); }
// edad del jugador (anios; 0 = no se sabe) -> 0 (20 o menos) .. 1 (38 o mas); sin dato, un adulto joven
float PesWrAgeK(float age, float ageW)
{
  // (10/11 g) la DLL manda edad + rasgo propio del jugador en la fraccion ((1 + 2 n) / 32, n = 1..7)
  g_wrSeed = age > 0.5 ? floor(frac(age + 0.001) * 16.0) : 0.0;
  age = floor(age + 0.001);
  if (ageW <= 0.001) return -1.0;   // (10/11 h, Agustin: "centrate solo en arrugas por expresion sin edad") z15 = 0: SIN EDAD (PesWrAge)
  if (wrPlC.x > 0.5 && abs(wrPlB.z) > 0.5) age = clamp((age > 0.5 ? age : 26.0) + wrPlB.z, 14.0, 60.0);   // anios de piel de este jugador
  float k = age > 0.5 ? saturate((age - 20.0) / 18.0) : 0.3;
  // cada uno envejece distinto: hasta +- 5 anios de piel
  if (g_wrSeed > 0.5) k = saturate(k + (PesWrHash(8.0) - 0.5) * 0.28 * step(0.02, k + 0.2));
  return saturate(k * ageW);
}
// arrugas en reposo por edad y ganancia de profundidad
void PesWrAge(float ageK, inout float4 wa, inout float4 wb, out float gain)
{
  // (10/11 d) las de reposo ya no prenden los gestos: salen de los tres mapas de edad (joven -> adulto -> veterano, ageK 0.25 / 0.6)
  // (10/11 h) sin edad: nada en reposo y todos responden al gesto como un adulto (0.5), igual para cualquier jugador
  bool wrNoAge = ageK < 0.0; if (wrNoAge) ageK = 0.5;
  float j = 1.0 - smoothstep(0.15, 0.35, ageK), v = smoothstep(0.45, 0.75, ageK);
  uint aw = 0, ah = 0; pesWrAgeT.GetDimensions(aw, ah);
  g_wrAg = aw > 1 ? float3(j, saturate(1.0 - j - v), v) * kWrAgeMm * saturate(ageK * 4.0) * (wrNoAge ? 0.0 : 1.0) : 0;
  g_wrAgeK = ageK;
  // (10/11 f, Agustin: "al ejercer animaciones se tienen que marcar de distintas formas dependiendo la accion, la edad") RESPUESTA DE
  // CADA ZONA AL GESTO SEGUN LA EDAD. Piel joven (elastica): un gesto chico no marca nada (umbral) y el gesto entero deja un pliegue
  // ancho y bajo; con la edad el mismo gesto marca antes y mas hondo. No todas las zonas envejecen igual: frente, patas de gallo,
  // marioneta y cuello cambian mucho; el surco nasogeniano y la mejilla se marcan al sonreir a cualquier edad.
  //   zonas a: frente, entrecejo, ojos, surco    b: menton, marioneta, cuello, mejilla
  float4 tA = float4(0.20, 0.12, 0.20, 0.05) * (1.0 - ageK), tB = float4(0.12, 0.22, 0.16, 0.08) * (1.0 - ageK);
  wa = saturate((wa - tA) / (1.0 - tA)); wb = saturate((wb - tB) / (1.0 - tB));
  g_wrGA = lerp(float4(0.65, 0.85, 0.65, 0.95), float4(1.40, 1.25, 1.45, 1.20), ageK);
  g_wrGB = lerp(float4(0.80, 0.60, 0.70, 0.90), float4(1.20, 1.40, 1.40, 1.15), ageK);
  // (10/11 g, Agustin: "la frente o las patas de gallo no son iguales para todos, asi como las mejillas o las orejas") cada jugador
  // marca mas unas zonas que otras (0.55 .. 1.40 por zona, fijo por jugador); sin dato del jugador, todas igual
  if (g_wrSeed > 0.5) {
    g_wrGA *= lerp(0.55, 1.40, float4(PesWrHash(0.0), PesWrHash(1.0), PesWrHash(2.0), PesWrHash(3.0)));
    g_wrGB *= lerp(0.55, 1.40, float4(PesWrHash(4.0), PesWrHash(5.0), PesWrHash(6.0), PesWrHash(7.0)));
    g_wrAg *= lerp(0.70, 1.30, PesWrHash(9.0));
  }
  // SISTEMAS SEPARADOS Y CONECTADOS (10/11 h): gestos (canales a / b, con los pesos de la animacion) y edad (mapas de reposo, g_wrAg)
  // tienen su control cada uno; los une la edad (decide cuanto y como responde cada zona al gesto) y el dibujo: los dos salen del
  // mismo relieve, asi el gesto profundiza la linea que la edad ya dejo marcada en vez de dibujar otra al lado
  if (wrPlC.x > 0.5) {
    g_wrGA *= max(wrPlA.x, 0.0) * max(float4(wrPlA.y, wrPlA.y, wrPlA.z, wrPlA.w), 0.0);
    g_wrGB *= max(wrPlA.x, 0.0) * max(float4(wrPlB.x, wrPlB.x, wrPlB.y, wrPlA.w), 0.0);
    g_wrAg *= max(wrPlB.w, 0.0);
  }
  gain = 1.0;
}
float PesWrD1(float4 ha, float4 hb, float4 pa, float4 pb, float4 ka, float4 kb)
{
  return dot(ka, pow(max(ha, 1e-4), pa)) + dot(kb, pow(max(hb, 1e-4), pb));
}
// gestos + edad en un punto (mm)
float PesWrDS(SamplerState s, float2 uv, float lod, float4 pa, float4 pb, float4 ka, float4 kb)
{
  float d = PesWrD1(pesWrDA.SampleLevel(s, uv, lod), pesWrDB.SampleLevel(s, uv, lod), pa, pb, ka, kb);
  if (any(g_wrAg > 0.0)) d += dot(g_wrAg, pesWrAgeT.SampleLevel(s, uv, lod).rgb - 0.5);
  return d;
}
#ifndef PES_WRD_NO_PS   // (CalculateLevelOfDetail: solo PS; el DS de la teselacion usa PesWrD1 directo)
// d = profundidad (mm) aqui; g = pendiente (mm / mm) en u y en v (v hacia abajo de la textura); dWide = el mismo surco sin afinar
// (zona ancha y suave); dBlur = promedio de los alrededores (2 mips mas arriba): donde es mayor que d, la piel esta juntada entre surcos
void PesWrDepth(SamplerState s, float2 uv, float4 wa, float4 wb, float gain, out float d, out float2 g, out float dWide, out float dBlur)
{
  uint tw = 1, th = 1; pesWrDA.GetDimensions(tw, th);
  float lod = pesWrDA.CalculateLevelOfDetail(s, uv);
  wa = saturate(wa); wb = saturate(wb);
  float wrSh = 0.08 + 0.30 * g_wrAgeK;   // (10/11 f) el pliegue se afina con el gesto: casi nada de joven, mas de veterano
  float4 pa = 1.0 + wrSh * wa, pb = 1.0 + wrSh * wb;
  float4 ka = kWrDmmA * pow(wa, 0.8) * gain * g_wrGA, kb = kWrDmmB * pow(wb, 0.8) * gain * g_wrGB;
  float2 du = float2(1.5 / tw, 0.0), dv = float2(0.0, 1.5 / th);
  float4 a0 = pesWrDA.SampleLevel(s, uv, lod), b0 = pesWrDB.SampleLevel(s, uv, lod);
  d = PesWrDS(s, uv, lod, pa, pb, ka, kb);
  float xp = PesWrDS(s, uv + du, lod, pa, pb, ka, kb), xm = PesWrDS(s, uv - du, lod, pa, pb, ka, kb);
  float yp = PesWrDS(s, uv + dv, lod, pa, pb, ka, kb), ym = PesWrDS(s, uv - dv, lod, pa, pb, ka, kb);
  float mm = 1.5 * 0.55 * 1024.0 / (float)tw;   // un texel de 1024 son ~0.55 mm de piel de la cara
  g = float2(xp - xm, yp - ym) / (2.0 * mm);
  dWide = dot(ka, a0) + dot(kb, b0);
  // (10/11 c) las texturas traen el relieve entero (la piel lisa queda en el medio): surco / cresta se miden contra el promedio de
  // los alrededores (~4 mm, 3 mips mas arriba, con el mismo perfil)
  dBlur = PesWrDS(s, uv, lod + 3.0, pa, pb, ka, kb);
}
#endif
// normal del espacio tangente que deja el surco (la misma convencion del mapa de arrugas del juego: x con u, y hacia arriba)
float2 PesWrDNormalXY(float2 g) { return float2(g.x, -g.y); }
#endif
