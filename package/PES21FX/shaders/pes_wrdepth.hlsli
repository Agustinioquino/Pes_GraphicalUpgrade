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
static const float4 kWrDmmA = float4(0.40, 0.75, 0.32, 0.75);   // (10/11) entrecejo 0.50 -> 0.75: en el video el enojo a 1.42 casi no se veia
//   // profundidad maxima (mm) por canal: frente, entrecejo, ojos, surco
static const float4 kWrDmmB = float4(0.32, 0.42, 0.50, 0.25);   // menton, marioneta, cuello, mejilla
bool PesWrDOk() { uint w = 0, h = 0; pesWrDA.GetDimensions(w, h); return w > 1; }
// edad del jugador (anios; 0 = no se sabe) -> 0 (20 o menos) .. 1 (38 o mas); sin dato, un adulto joven
float PesWrAgeK(float age, float ageW)
{
  float k = age > 0.5 ? saturate((age - 20.0) / 18.0) : 0.3;
  return saturate(k * ageW);
}
// arrugas en reposo por edad y ganancia de profundidad
void PesWrAge(float ageK, inout float4 wa, inout float4 wb, out float gain)
{
  wa = max(wa, ageK * float4(0.30, 0.15, 0.30, 0.35));
  wb = max(wb, ageK * float4(0.10, 0.20, 0.30, 0.00));
  gain = lerp(0.80, 1.35, ageK);
}
float PesWrD1(float4 ha, float4 hb, float4 pa, float4 pb, float4 ka, float4 kb)
{
  return dot(ka, pow(max(ha, 1e-4), pa)) + dot(kb, pow(max(hb, 1e-4), pb));
}
#ifndef PES_WRD_NO_PS   // (CalculateLevelOfDetail: solo PS; el DS de la teselacion usa PesWrD1 directo)
// d = profundidad (mm) aqui; g = pendiente (mm / mm) en u y en v (v hacia abajo de la textura); dWide = el mismo surco sin afinar
// (zona ancha y suave); dBlur = promedio de los alrededores (2 mips mas arriba): donde es mayor que d, la piel esta juntada entre surcos
void PesWrDepth(SamplerState s, float2 uv, float4 wa, float4 wb, float gain, out float d, out float2 g, out float dWide, out float dBlur)
{
  uint tw = 1, th = 1; pesWrDA.GetDimensions(tw, th);
  float lod = pesWrDA.CalculateLevelOfDetail(s, uv);
  wa = saturate(wa); wb = saturate(wb);
  float4 pa = 1.0 + 2.5 * wa, pb = 1.0 + 2.5 * wb;
  float4 ka = kWrDmmA * pow(wa, 0.8) * gain, kb = kWrDmmB * pow(wb, 0.8) * gain;
  float2 du = float2(1.5 / tw, 0.0), dv = float2(0.0, 1.5 / th);
  float4 a0 = pesWrDA.SampleLevel(s, uv, lod), b0 = pesWrDB.SampleLevel(s, uv, lod);
  d = PesWrD1(a0, b0, pa, pb, ka, kb);
  float xp = PesWrD1(pesWrDA.SampleLevel(s, uv + du, lod), pesWrDB.SampleLevel(s, uv + du, lod), pa, pb, ka, kb);
  float xm = PesWrD1(pesWrDA.SampleLevel(s, uv - du, lod), pesWrDB.SampleLevel(s, uv - du, lod), pa, pb, ka, kb);
  float yp = PesWrD1(pesWrDA.SampleLevel(s, uv + dv, lod), pesWrDB.SampleLevel(s, uv + dv, lod), pa, pb, ka, kb);
  float ym = PesWrD1(pesWrDA.SampleLevel(s, uv - dv, lod), pesWrDB.SampleLevel(s, uv - dv, lod), pa, pb, ka, kb);
  float mm = 1.5 * 0.55 * 1024.0 / (float)tw;   // un texel de 1024 son ~0.55 mm de piel de la cara
  g = float2(xp - xm, yp - ym) / (2.0 * mm);
  dWide = dot(ka, a0) + dot(kb, b0);
  dBlur = dot(ka, pesWrDA.SampleLevel(s, uv, lod + 2.0)) + dot(kb, pesWrDB.SampleLevel(s, uv, lod + 2.0));
}
#endif
// normal del espacio tangente que deja el surco (la misma convencion del mapa de arrugas del juego: x con u, y hacia arriba)
float2 PesWrDNormalXY(float2 g) { return float2(g.x, -g.y); }
#endif
