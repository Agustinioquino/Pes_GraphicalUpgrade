// PES21 (08/11) normal map propio de los estampados del kit: numero, nombre, escudo, sponsor y marca.
//
// Como arma el juego las calcomanias (PS 7e64 / a41e / c7ae, el G-buffer de la camiseta):
//  - numero y nombre NO estan en la textura del kit: salen del atlas g_tex_marking (t6) con 4 lecturas
//    (m_materials[3..7] = donde cae cada una en la UV del kit) y se mezclan encima del diffuse.
//    a41e (camiseta sin dorsal) lee en t6 el atlas g_tex_badge (parches de liga / competicion, 2 lecturas).
//  - escudo, sponsor y marca SI estan pintados en la textura del kit; su alfa (g_tex_diffuse.a) es la mascara
//    con la que Konami les saca la trama de tela (pattern 0.85) y les cambia el srm.
//  - la normal de la tela sale solo de g_tex_normal_array (la trama), igual para todo el kit: por eso hoy los
//    estampados se ven pintados sobre la tela, sin canto ni volumen.
//
// Aca se arma un mapa de alturas por estampado, en el espacio de la textura (no de la pantalla: el canto tiene el
// mismo ancho en texeles a cualquier distancia) y se convierte en normal:
//  - vinilo (numero, nombre): meseta lisa con canto redondeado de ~2 texeles, aplana casi toda la trama; si tiene
//    filete de otro color (dos capas de vinilo) el filete marca su propio escalon.
//  - bordado / parche (escudo, marca, parches de liga): canto mas suave, cada zona de color con su propio relieve
//    (las capas del bordado) y, muy de cerca, el hilo en diagonal (puntada satinada). Si el alfa del diffuse cubre
//    una zona grande (sponsor grande, o un kit hecho por la comunidad con alfa en todo) el relieve interior baja
//    solo y queda como estampado plano con canto: nunca se bordea la camiseta entera.
//  - (08/11 b) no todos los escudos son bordados: cada logo se mide en su textura (detalle fino de hilo en todo el
//    logo = bordado: almohadilla, capas, puntadas y sombra al pie; plano por zonas = impreso / termoaplicado: canto
//    fino). El tejido del kit lo empuja (algodon pesado y jacquard -> cosido; poliester liso -> impreso) y en los kits
//    de algodon pesado (retro) numero y nombre son fieltro cosido en vez de vinilo.
// Parametros (fila 38): x38 altura del canto, y38 bordado (relieve interior + hilo), z38 cuanto aplana la trama,
// w38 on/off [Alt+Shift+5]. Todo apagado lejos (kit chico en pantalla) y sin luz del sol (pesFine), como antes.
// Las lecturas van con SampleGrad (gradientes calculados afuera del if) para poder ir dentro del if de w38.

float PesPrLum(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

#if defined(PES_PRINT_MARKING)
// atlas de numero / nombre (7e64, c7ae): mismas 4 lecturas que el original; devuelve (luminancia del color, alfa)
float2 PesPrAtlas(float2 uv, float2 dx, float2 dy)
{
  float4 M3 = g_psMaterial.m_materials[3], M4 = g_psMaterial.m_materials[4], M5 = g_psMaterial.m_materials[5];
  float4 M6 = g_psMaterial.m_materials[6], M7 = g_psMaterial.m_materials[7];
  dx *= 0.5; dy *= 0.5;   // el original lee con SampleBias -1
  // 1) numero
  float2 a1 = float2(uv.x * M4.x + (0.5 - 0.5 * M4.x), 0.627451003 * M4.y * uv.y + M4.z);
  float  k1 = all(a1 >= 0.0) && all(a1 <= 1.0) ? 1.0 : 0.0;
  float2 j1 = float2(0.24 * M4.x, 0.627451003 * M4.y);
  float4 s1 = g_tex_marking.SampleGrad(g_sampler_number_s, saturate(a1) * float2(0.24, 1) + float2(0.64, 0), dx * j1, dy * j1);
  // 2) nombre
  float2 a2 = float2(uv.x * M5.x + (0.5 - 0.5 * M5.x), uv.y * M5.y + M5.z);
  float  k2 = all(a2 >= 0.0) && all(a2 <= 1.0) ? 1.0 : 0.0;
  float2 j2 = float2(0.64 * M5.x, M5.y);
  float4 s2 = g_tex_marking.SampleGrad(g_sampler_number_s, saturate(a2) * float2(0.64, 1), dx * j2, dy * j2);
  // 3) y 4) numero chico / frente
  float3 u1 = float3(uv, 1);
  float2 a3 = saturate(float2(dot(M6.xyz, u1), dot(M7.xyz, u1)));
  float2 g3x = float2(dot(M6.xy, dx), dot(M7.xy, dx)) * float2(0.08, 0.5), g3y = float2(dot(M6.xy, dy), dot(M7.xy, dy)) * float2(0.08, 0.5);
  float4 s3 = g_tex_marking.SampleGrad(g_sampler_number_s, a3 * float2(0.08, 0.5) + float2(0.88, 0.5), g3x, g3y);
  float2 a4 = saturate(uv * M3.xy + M3.zw);
  float2 j4 = M3.xy * float2(0.12, 0.5);
  float4 s4 = g_tex_marking.SampleGrad(g_sampler_number_s, a4 * float2(0.12, 0.5) + float2(0.88, 0), dx * j4, dy * j4);
  float w1 = s1.w * k1, w2 = s2.w * k2, w3 = s3.w, w4 = s4.w;
  float a = max(max(w1, w2), max(w3, w4));
  float l = (PesPrLum(s1.rgb) * w1 + PesPrLum(s2.rgb) * w2 + PesPrLum(s3.rgb) * w3 + PesPrLum(s4.rgb) * w4) / max(w1 + w2 + w3 + w4, 1e-3);
  return float2(l, a);
}
#elif defined(PES_PRINT_BADGE)
// parches de liga (a41e): mismas 2 lecturas del atlas g_tex_badge que el original
float2 PesPrAtlas(float2 uv, float2 dx, float2 dy)
{
  float4 M3 = g_psMaterial.m_materials[3], M4 = g_psMaterial.m_materials[4];
  float4 M5 = g_psMaterial.m_materials[5], M6 = g_psMaterial.m_materials[6];
  float3 u1 = float3(uv, 1);
  float2 a1 = float2(dot(M5.xyz, u1), dot(M6.xyz, u1));
  float  k1 = all(a1 >= 0.0) && all(a1 <= 1.0) ? 1.0 : 0.0;
  float2 g1x = float2(dot(M5.xy, dx), dot(M6.xy, dx)) * float2(0.5, 1), g1y = float2(dot(M5.xy, dy), dot(M6.xy, dy)) * float2(0.5, 1);
  float4 s1 = g_tex_badge.SampleGrad(g_sampler_badge_s, saturate(a1) * float2(0.5, 1) + float2(0.5, 0), g1x, g1y);
  float2 a2 = float2(dot(M3.xyz, u1), dot(M4.xyz, u1));
  float  k2 = all(a2 >= 0.0) && all(a2 <= 1.0) ? 1.0 : 0.0;
  float2 g2x = float2(dot(M3.xy, dx), dot(M4.xy, dx)) * float2(0.5, 1), g2y = float2(dot(M3.xy, dy), dot(M4.xy, dy)) * float2(0.5, 1);
  float4 s2 = g_tex_badge.SampleGrad(g_sampler_badge_s, saturate(a2) * float2(0.5, 1), g2x, g2y);
  float w1 = s1.w * k1, w2 = s2.w * k2;
  float a = max(w1, w2);
  float l = (PesPrLum(s1.rgb) * w1 + PesPrLum(s2.rgb) * w2) / max(w1 + w2, 1e-3);
  return float2(l, a);
}
#endif

// altura del estampado en uv. am = alfa del atlas (vinilo en 7e64/c7ae, parche en a41e), ad = alfa del diffuse
// (escudo / sponsor / marca). guard baja el relieve interior del diffuse cuando la zona pintada es grande.
// e = cuanto de bordado tiene este logo (0 impreso / termoaplicado, 1 bordado), fe = numeros de fieltro cosido (kits retro).
float PesPrHeight(float2 uv, float2 dx, float2 dy, float emb, float guard, float e, float fe, out float am, out float ad)
{
  float4 d = g_tex_diffuse.SampleGrad(g_sampler_diffuse_s, uv, dx, dy);
  float  adB = g_tex_diffuse.SampleGrad(g_sampler_diffuse_s, uv, dx * 4.0, dy * 4.0).w;   // alfa borroso (2 mips mas)
  float2 m = PesPrAtlas(uv, dx, dy);
  am = m.y; ad = d.w;
#if defined(PES_PRINT_BADGE)
  float innerM = 0.6;    // parche bordado
#else
  float innerM = 0.25;   // vinilo: solo el escalon del filete
#endif
  float hv = smoothstep(0.0, 1.0, am);              // meseta con canto redondeado
  // impreso: canto fino y bajo; bordado: el relleno de hilo levanta mas y redondea el borde (almohadilla)
  float hd = lerp(0.6 * smoothstep(0.0, 1.0, ad), 1.1 * smoothstep(0.0, 1.0, adB) * ad, e);
  // relieve interior: impreso apenas marca cada color; bordado sigue cada capa y el hilo que trae pintado la textura
  float inner = innerM * am * m.x * (1.0 + fe) + ad * guard * PesPrLum(d.rgb) * lerp(0.35, 1.3, e);
  return max(hv * (1.0 - 0.3 * fe), hd) + emb * inner * 0.6;
}

// cuanto bordado tiene el logo en este punto: un bordado pintado en la textura tiene detalle fino de hilo en todo el
// logo (la luminancia cambia texel a texel), un estampado impreso es plano por zonas y solo cambia en los bordes de
// color. Se mide con 4 sondas a 2 texeles. kType del kit (tejido que pide el material) lo empuja: algodon pesado
// (retro) y jacquard suelen llevar escudo cosido, poliester liso casi nunca.
float PesPrEmbroidery(float2 uv, float2 dx, float2 dy, float res, float kType)
{
  float s = 2.0 / res;
  float4 c = g_tex_diffuse.SampleGrad(g_sampler_diffuse_s, uv, dx, dy);
  float l0 = PesPrLum(c.rgb);
  float l1 = PesPrLum(g_tex_diffuse.SampleGrad(g_sampler_diffuse_s, uv + float2( s,  s), dx, dy).rgb);
  float l2 = PesPrLum(g_tex_diffuse.SampleGrad(g_sampler_diffuse_s, uv + float2(-s,  s), dx, dy).rgb);
  float l3 = PesPrLum(g_tex_diffuse.SampleGrad(g_sampler_diffuse_s, uv + float2( s, -s), dx, dy).rgb);
  float l4 = PesPrLum(g_tex_diffuse.SampleGrad(g_sampler_diffuse_s, uv + float2(-s, -s), dx, dy).rgb);
  float mean = (l0 + l1 + l2 + l3 + l4) * 0.2;
  float dev = (abs(l0 - mean) + abs(l1 - mean) + abs(l2 - mean) + abs(l3 - mean) + abs(l4 - mean)) * 0.2;
  // un borde de color da desvio en 1-2 sondas; el hilo en todas: pedir desvio parejo (el minimo de los pares)
  float pairs = min(abs(l1 - l4), abs(l2 - l3));
  float e = smoothstep(0.02, 0.07, dev) * smoothstep(0.015, 0.05, pairs + dev * 0.5);
  float kb = (kType > 4.5 && kType < 6.5) ? 0.35 : (kType > 2.5 && kType < 3.5 ? -0.25 : 0.0);   // 5 algodon, 6 jacquard / 3 poliester
  return saturate(e + kb) * c.w;
}

// n = normal de tangente (xy perturbacion, z), cav = oclusion del kit. su / sv = signo de la tangente / binormal
// respecto de +u / +v (medido afuera del if). pesPrint = mascara del original (max de los dos alfas).
void PesPrintRelief(float2 uv, float2 dxu, float2 dyu, float su, float sv, float4 PR, float pesPrint,
                    inout float3 n, inout float cav)
{
  float W, H;
  g_tex_diffuse.GetDimensions(W, H);
  float res = max(W, H);
  float fp = max(length(dxu), length(dyu)) * res;          // texeles del kit por pixel
  float farFade = saturate((16.0 - fp) / 8.0);             // kit diminuto: nada
  if (farFade <= 0.0) return;
  float emb = saturate(PR.y);
  float kType = clamp(round(g_psMaterial.m_materials[1].z), 0.0, 7.0);
  float fe = (kType > 4.5 && kType < 5.5) ? 1.0 : 0.0;    // algodon pesado (retro): numeros de fieltro cosido, no vinilo
  // zona grande pintada con alfa (sponsor grande o kit con alfa en todo): 4 sondas a 24 texeles
  float s = 24.0 / res;
  float g4 = g_tex_diffuse.SampleGrad(g_sampler_diffuse_s, uv + float2(s, 0), dxu, dyu).w;
  g4 = min(g4, g_tex_diffuse.SampleGrad(g_sampler_diffuse_s, uv - float2(s, 0), dxu, dyu).w);
  g4 = min(g4, g_tex_diffuse.SampleGrad(g_sampler_diffuse_s, uv + float2(0, s), dxu, dyu).w);
  g4 = min(g4, g_tex_diffuse.SampleGrad(g_sampler_diffuse_s, uv - float2(0, s), dxu, dyu).w);
  float guard = 1.0 - 0.8 * smoothstep(0.9, 0.99, g4);
  float e = PesPrEmbroidery(uv, dxu, dyu, res, kType) * guard * emb;
  // alturas en el centro y a un paso de 1.25 texeles en u y en v (diferencias hacia adelante)
  float st = 1.25 / res;
  float am, ad, am1, ad1, am2, ad2;
  float h0 = PesPrHeight(uv, dxu, dyu, emb, guard, e, fe, am, ad);
  float hu = PesPrHeight(uv + float2(st, 0), dxu, dyu, emb, guard, e, fe, am1, ad1);
  float hw = PesPrHeight(uv + float2(0, st), dxu, dyu, emb, guard, e, fe, am2, ad2);
  float2 g = float2(hu - h0, hw - h0);                      // altura por paso (0..1 en el canto)
  float k = saturate(PR.x / 1.5) * 1.5 * 0.9 * farFade;     // x38 = 1 -> canto de ~30 grados
  float2 tilt = -g * float2(su, sv) * k;
  // puntadas: solo en lo bordado (y los parches), lineas de 1 texel en diagonal, de cerca (sin aliasing)
  float nearS = saturate((0.6 - fp) / 0.3);
#if defined(PES_PRINT_BADGE)
  float embMask = max(am * emb, ad * e);
#else
  float embMask = max(ad * e, am * fe * emb);
#endif
  if (nearS > 0.0 && embMask > 0.0) {
    float ph = (uv.x + uv.y) * res * 6.2831853;
    float2 t = float2(su, sv) * 0.7071 * cos(ph);
    tilt += t * 0.22 * nearS * embMask;
  }
  // el vinilo es liso (tapa casi toda la trama); la tinta deja ver la trama; el bordado la tapa con hilo
  float flatK = 1.0 - saturate(PR.z) * saturate(0.95 * am * (1.0 - 0.5 * fe) + lerp(0.45, 0.9, e) * ad);
  n = normalize(float3(n.xy * flatK + tilt, max(n.z, 0.001)));
  // sombra al pie del canto (mas marcada en el bordado, que levanta mas); arriba no se oscurece con la trama
  float m = max(am, ad);
  cav *= 1.0 - (0.3 + 0.15 * e) * saturate(length(g) * 2.0) * (1.0 - m) * saturate(PR.x) * farFade;
  cav = lerp(cav, 1.0, saturate(pesPrint) * 0.7);
}
