// PES21 FX (12/11) - PIEL v2: superficie de la piel en el G-buffer (color, sangre, cavidades, rugosidad y brillo), rehecha de cero
// con todo lo aprendido de las pruebas de Agustin desde el 18/10. Interruptor w124 (0 = la piel de antes, para comparar). El SSS
// (pes_sss_real / pes_ssss_inc), las arrugas, el sudor, las cejas y la boca NO pasan por aca.
//
// Agustin 12/11: "se ven manchas raras y a ciertas distancias es peor"; "deberias rehacer estas secciones de la piel menos el sss,
// con todo lo que ya sabes nuevo y con las pruebas viejas". Lo que se aprendio y como queda:
//  - MANCHAS POR DISTANCIA: cada detalle se apagaba con su propia medida (derivadas de la UV, mip del color, mm de piel) y algunos
//    seguian activos donde ya no se resolvian: el ruido de poros viejo sobre la UV (1400 celdas, 16/10) seguia hasta celdas de
//    0.7 pixel, el realce de marcas comparaba contra un mip relativo (agrandaba manchas grandes de la textura a media distancia), la
//    micro sombra usaba el poro promediado. En v2 TODO se mide en milimetros de piel por pixel (posicion de reposo del VS) con una
//    sola curva por efecto, y lo que no se resuelve pasa a rugosidad (Toksvig), nunca a color.
//  - COLOR: la piel nunca se empuja al rojo (caras naranjas, 21/10 y 27/10): la sangre ABSORBE verde y azul (hemoglobina), no suma
//    rojo; la cavidad oscurece poco el color (gris = rechazado 04/11) y mucho el brillo (oclusion especular de Lagarde, como UE5).
//  - BRILLO: piel = F0 0.028 y rugosidad 0.42 (0.33 en la zona T, aprobado 04/11); labios mas lisos, sudor/lluvia lisos, barba mate.
//    El brillo que se apaga en el fondo de cavidades y poros sale de la oclusion especular con N.V real, no de un factor fijo.
#ifndef PES_SKIN2_HLSLI
#define PES_SKIN2_HLSLI

bool PesSk2On() { return IniParams.Load(int2(124, 0)).w > 0.5; }

// mm de piel por pixel desde la posicion de reposo (metros) y sus derivadas en pantalla; sin reposo (VS sin TEXCOORD10) devuelve 1
float PesSk2MmPx(float3 dPx, float3 dPy, bool hasR) { return hasR ? max(sqrt(length(dPx) * length(dPy)) * 1000.0, 1e-3) : 1.0; }

// detalle fino de la textura (lunares, pecas, venitas): solo lo que mide menos de ~3 mm de piel, y solo mientras ese detalle ocupa
// varios pixeles (mmPx <= 0.25 entero, nada a 0.6). Contraste contra un promedio FIJO de la textura (mip 3 sobre el nivel 0), no
// relativo al mip de pantalla: asi a media distancia no agranda las manchas grandes (ojeras, rubor pintado). Tope +-35 %
float3 PesSk2Marks(Texture2D<float4> tex, SamplerState smp, float2 uv, float3 alb, float lod, float mmPx, float k) {
  float near = saturate((0.6 - mmPx) / 0.35) * saturate(1.5 - lod);
  if (k * near <= 0.001) return alb;
  float3 aB = tex.SampleLevel(smp, uv, max(lod, 0.0) + 3.0).xyz;
  const float3 W = float3(0.2126, 0.7152, 0.0722);
  float rat = (dot(alb, W) + 0.02) / (dot(aB, W) + 0.02);
  return alb * pow(clamp(rat, 0.74, 1.35), 0.8 * k * near);
}

// sangre bajo la piel: la hemoglobina absorbe verde y azul (no suma rojo). b = cantidad (0..1)
float3 PesSk2Blood(float3 alb, float b) { return alb * exp(-saturate(b) * float3(0.0, 0.42, 0.50)); }

// oclusion especular (Lagarde 2014, la de UE5 / Frostbite): cuanto del lobulo especular ve el cielo desde el fondo de una cavidad
float PesSk2SpecOcc(float NoV, float ao, float rough) {
  return saturate(pow(abs(NoV + ao), exp2(-16.0 * rough - 1.0)) - 1.0 + ao);
}

struct PesSk2In {
  float3 alb; float rough; float spec; float thin;
  float sat; float thinWarm; float mapBlood; float flush;   // flush = rubor del esfuerzo (x12 y zonas) ya calculado
  float lipM; float lipW; float3 lipTint;                   // lipM zonas (fila 72), lipW guia de poros (v6, los dos labios)
  float cav; float tiltCav;                                 // cavidades: quinta textura (FA.r * y68) y normal map (x35)
  float specScale; float glossScale; float lipGloss;        // z, y2, x73
  float filmWet; float sweatWet; float fhMask;
  float microVar; float poreA; float poreC; float tZone; float poreK;   // poros v5 (pes_pores), poreK = saturate(x94)
  float NoV;
};

// color (va donde estaba "1) Albedo", antes de las arrugas, que despues tinen el pliegue como siempre)
float3 PesSk2Albedo(PesSk2In I) {
  const float3 W = float3(0.2126, 0.7152, 0.0722);
  float3 a = I.alb;
  float lum = dot(a, W);
  a = max(0.0, lerp(lum.xxx, a, I.sat));
  // sangre: zonas finas (orejas, nariz, labios) un poco, mapa de SSS (zonas irrigadas), rubor del esfuerzo. Como absorcion: el verde
  // y el azul bajan, el rojo queda; la luminancia baja apenas (la piel con sangre es mas oscura, no mas brillante)
  a = PesSk2Blood(a, 0.30 * saturate(I.thin * I.thinWarm) + 0.45 * saturate(I.mapBlood) + I.flush);
  a = lerp(a, a * I.lipTint, I.lipM);
  // cavidades: una sola oclusion (la mas fuerte de las dos fuentes). En el color poco y calido (la luz rebota roja adentro de un
  // pliegue de piel); en el brillo, la oclusion especular de PesSk2Spec
  float ao = 1.0 - saturate(max(I.cav, I.tiltCav));
  a *= lerp(float3(1.0, 1.0, 1.0), float3(0.84, 0.79, 0.78), 1.0 - ao);   // 04/11 pidio definicion en los pliegues: sin llegar al gris
  return saturate(a);
}

// rugosidad (x) e intensidad especular (y) del G-buffer; va donde estaba "2) Especular" (despues de las arrugas). Tambien oscurece
// apenas el color mojado (albMul)
float2 PesSk2Spec(PesSk2In I, out float albMul) {
  float ao = 1.0 - saturate(max(I.cav, I.tiltCav));
  float r = saturate(I.rough * I.glossScale);
  r = lerp(r, min(r, 0.30), I.lipM * I.lipGloss);                   // labios mas lisos
  r = lerp(r, r * 0.45, I.filmWet);                                 // pelicula de sudor / lluvia
  r = lerp(r, max(r, 0.75), I.fhMask);                              // barba / vello pintado: mate
  // piso real de la piel seca (MetaHuman / Weyrich 2006): 0.42, 0.33 en la zona T (grasa). Labios y agua quedan afuera
  float rFloor = lerp(0.42, 0.33, saturate(I.tZone * I.poreK));
  float keep = (1.0 - saturate(max(I.lipM, I.lipW) * 1.5)) * (1.0 - I.sweatWet) * (1.0 - saturate(I.filmWet * 1.5));
  r = lerp(r, max(r, rFloor), keep);
  // detalle que ya no se resuelve -> rugosidad (Toksvig en alfa^2): varianza de la micro normal; el hueco del poro solo de cerca
  { float al = r * r; al = sqrt(saturate(al * al + 0.10 * I.microVar)); r = sqrt(al); }
  r = saturate(r + 0.12 * I.poreC);
  r = lerp(r, max(r, 0.6), (1.0 - ao) * 0.5);                       // fondo del pliegue: piel apretada, mas aspera
  r = lerp(r, 0.05, I.sweatWet);                                    // gota: casi espejo
  float s = saturate(I.spec * I.specScale);
  s = lerp(s, max(s, 0.55), I.lipM * I.lipGloss * 0.8);
  s = lerp(s, max(s, 0.5), I.filmWet * 0.8);
  s *= lerp(0.95, 1.10, I.tZone * I.poreK);                         // grasa de la zona T
  s *= lerp(1.0, 0.88, keep * saturate((rFloor - 0.3) * 6.0));      // lobulo mas ancho = un poco menos de pico (misma energia)
  s *= 1.0 - 0.6 * I.fhMask;
  s *= PesSk2SpecOcc(I.NoV, ao * (1.0 - 0.5 * I.poreC), r);         // cavidades y fondo del poro (de cerca)
  s = lerp(s, max(s, 0.75), I.sweatWet);
  albMul = 1.0 - 0.06 * I.filmWet - 0.10 * I.sweatWet;              // mojado: apenas mas oscuro
  return float2(r, s);
}
#endif
