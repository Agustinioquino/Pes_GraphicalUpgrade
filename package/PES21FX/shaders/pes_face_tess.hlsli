// PES21 FX (04/11, Agustin: "subir la cantidad de poligonos de las caras / teselacion, que se note realista, solo en primeros
// planos, sin deformar demasiado") TESELACION DE LA CARA: hull + domain shader que la DLL (facetess.cpp) pone detras del VS de la
// cabeza (faab87ee) y de la cara animada (455eb97b) solo en los dibujos de piel de la cara (expr.cpp: cabeza en la paleta, no boca,
// no ojo, no pestanas), con el juego dibujando triangulos sueltos.
//
// - Cuanto se divide lo decide cada ARISTA por su largo en pantalla (z357 pixeles por segmento, w357 tope): de lejos y en el
//   partido queda en 1 (el triangulo original, sin costo de geometria); de cerca cada arista se parte hasta w357 veces. Las dos
//   caras que comparten una arista calculan el mismo factor (mismos dos extremos) -> sin grietas.
// - La forma: teselacion de PHONG (Boubekeur / Alexa 2008): cada punto nuevo se proyecta sobre el plano tangente de los tres
//   vertices y se mezcla con y357. Redondea el menton, la nariz, los pomulos y las orejas donde la malla se ve facetada, sin
//   inventar relieve (las normales de sombreado quedan las del juego): no cambia la cara, le saca las aristas.
// - Las caras vienen en piezas (frente / mejillas / menton en Messi) y por capas: los vertices del BORDE de cada malla (arista
//   con un solo triangulo) y los duplicados (costuras duras / de UV) llevan peso 0 (lo calcula la DLL por malla, t119 en el VS
//   -> TEXCOORD12.w), asi la arista entre dos piezas queda recta en las dos y las piezas siguen encajando.
// - Solo de cerca: con la cara a mas de x358 metros no hay desplazamiento (transicion y358 m) y ademas el corrimiento nunca
//   pasa de z358 mm. La boca por dentro no entra nunca (la DLL no la marca).
// - Las salidas del DS son las del VS en el MISMO orden (los PS del juego leen los registros por orden): la posicion en clip,
//   o7.xy / o8.x (TEXCOORD8 / 9) y el vector a la camara (o3.xyz) se recalculan con la posicion nueva; el resto se interpola.
//
// - (04/11 c, Agustin: "no noto alguna diferencia") RELIEVE: con la fila 359 los puntos nuevos ademas se corren por la normal
//   segun la quinta textura de la cara (facegen.cpp, sacada del normal map del juego: t23 A.r cavidad, A.g curvatura, t24 B.b
//   convexidad grande): surcos, comisuras y arrugas se hunden (y359 mm), labios, nariz, orejas y pomulos suben (z359 mm), por
//   x359 (fuerza, 0 = apagado). Solo donde hay peso de borde y de cerca, como el redondeo.
//
// PES_TESS_V455 = 1: variante del VS 455eb97b (TEXCOORD0 float4, TEXCOORD9 al final); 0: faab87ee (TEXCOORD0 y 9 float2 juntos).
// PES_TESS_CCW = 1: triangulos de salida con la orientacion invertida (w358; si la cara se ve del reves / desaparece).
// PES_TESS_BODY = 1 (04/11 g, Agustin: "teselacion a las manos, brazos y piernas"): variante del VS de la piel del cuerpo
// 1c02a92a (misma salida que faab87ee sin TEXCOORD11): fila 364 (x prendido, y densidad, z distancia maxima m, w tope mm),
// suavidad y357, transicion y358; sin relieve (el cuerpo no tiene quinta textura), sin zona de orejas ni de cuello.
#ifndef PES_TESS_BODY
#define PES_TESS_BODY 0
#endif
#ifndef PES_TESS_ATLAS
#define PES_TESS_ATLAS 0
#endif

Texture1D<float4> IniParams : register(t120);
#include "pes_face_tess_player.hlsli"   // (04/11 m) multiplicadores por jugador (b10)
#if PES_TESS_BODY
#include "pes_limb_relief.hlsli"   // (08/11) relieve de manos, brazos y piernas (w376): alturas en 3D sobre el reposo
// (08/11 n, Agustin: "agrega opciones de teselacion para las manos ... por partes tambien ... en especial en los dedos") opciones POR
// PARTE del cuerpo, multiplicadores sobre lo general (fila 364 / y357 / w376): 417 manos, 419 brazos, 420 piernas (x teselar 1/0,
// y densidad, z relieve, w redondeo); 418 dedos dentro de la mano (x densidad, y redondeo, z forma 0 Phong .. 1 curva cubica PN,
// w relieve de nudillos y pliegues). La parte sale del codigo del miembro (o1.z = 7 + accion / 1200, pes_limbs.hlsli: igual en todo
// el dibujo); los dedos por la posicion de reposo (marco de la mano de pes_limb_relief), por arista con sus dos extremos: sin grietas.
// Fila sin valores (ini viejo) = todo 1 (dedos: forma 0).
int PesBodyPart(float c0z)
{
  if (c0z < 6.5 || c0z > 7.5) return 0;
  float q = round((c0z - 7.0) * 1200.0);
  return q < 511.5 ? 2 : (q > 515.5 && q < 527.5) ? 1 : (q > 527.5 && q < 599.5) ? 3 : 0;   // 1 mano, 2 brazo, 3 pierna
}
float4 PesBodyPartOpt(int part)
{
  if (part == 0) return float4(1.0, 1.0, 1.0, 1.0);
  float4 v = IniParams.Load(int2(part == 1 ? 417 : (part == 2 ? 419 : 420), 0));
  if (dot(abs(v), float4(1, 1, 1, 1)) < 1e-4) return float4(1.0, 1.0, 1.0, 1.0);
  return float4(v.x, clamp(v.y, 0.0, 4.0), clamp(v.z, 0.0, 3.0), clamp(v.w, 0.0, 3.0));
}
float4 PesFingerOpt()
{
  float4 v = IniParams.Load(int2(418, 0));
  if (dot(abs(v), float4(1, 1, 1, 1)) < 1e-4) return float4(1.0, 1.0, 0.0, 1.0);
  return float4(clamp(v.x, 0.0, 4.0), clamp(v.y, 0.0, 3.0), saturate(v.z), clamp(v.w, 0.0, 3.0));
}
// (08/11 q, Agustin: "el modelo contiene un arm fmdl custom asi como unas piernas ... verifica el metodo de aplicar teselacion") el
// codigo del miembro (o1.z) solo existe en las mallas de TorsoBody que la DLL reconoce por su cantidad de indices (manos.cpp kLimbs,
// x302): un brazo / piernas propios de un jugador (Messi: arm.fmdl, thigh_middle, socks del Dream Patch por el hook del global model)
// no lo tienen y quedaban en la parte 0: sin densidad por parte, sin el partido fino de ~1.8 mm y sin relieve (solo un Phong de
// 1.5 mm, invisible). Sin codigo, la parte sale de la POSICION DE REPOSO de cada punto (mismo esqueleto en pose A que TorsoBody):
// mano = mas alla de la muneca por el eje de la mano, pierna = debajo de la cadera, brazo = afuera del hombro; torso / cuello = 0.
// En el HS cada arista decide con sus dos extremos y en el DS con la posicion interpolada: igual a los dos lados, sin grietas.
bool PesHasLimbCode(float c0z) { return c0z > 6.5 && c0z < 7.5; }
int PesBodyPartGeo(float3 r)
{
  float3 pm = float3(abs(r.x), r.y, r.z);
  if (pm.x > 0.45 && dot(pm - PLR_W, PLR_A) > -0.004) return 1;
  if (pm.y < 0.88) return 3;
  if (pm.x > 0.21 && pm.y > 0.95) return 2;
  return 0;
}
// 1 en los dedos (desde los nudillos, ~10 cm de la muneca por el eje de la mano; el pulgar por el costado), 0 en el dorso / palma
float PesFingerK(float3 r)
{
  float3 q = float3(abs(r.x), r.y, r.z) - PLR_W;
  float sm = dot(q, PLR_A), cm = dot(q, PLR_C);
  return max(smoothstep(0.092, 0.112, sm), smoothstep(0.038, 0.048, cm) * smoothstep(0.015, 0.035, sm));
}
#endif

// lo que sale del VS (y entra al HS), en el orden del VS
struct PesTessVS
{
  float4 o0 : SV_Position;
  float4 o1 : COLOR0;
#if PES_TESS_V455
  float4 o2 : TEXCOORD0;
#else
  float2 o2 : TEXCOORD0;
  float2 p2 : TEXCOORD9;
#endif
  float4 o3 : TEXCOORD4;
  float4 o4 : TEXCOORD5;
  float4 o5 : TEXCOORD6;
  float4 o6 : TEXCOORD7;
  float4 o7 : TEXCOORD8;
#if PES_TESS_V455
  float2 o8 : TEXCOORD9;
#endif
  float4 pR : TEXCOORD10;
#if !PES_TESS_BODY
  float4 pD : TEXCOORD11;
#endif
  float4 pV : TEXCOORD12;   // xyz posicion en espacio de la vista (antes de proyectar), w peso de borde (0 = costura, 1 = interior)
};

// punto de control entre el HS y el DS (igual, con la posicion en clip como dato comun)
struct PesTessCP
{
  float4 o0 : PESCLIP;
  float4 o1 : COLOR0;
#if PES_TESS_V455
  float4 o2 : TEXCOORD0;
#else
  float2 o2 : TEXCOORD0;
  float2 p2 : TEXCOORD9;
#endif
  float4 o3 : TEXCOORD4;
  float4 o4 : TEXCOORD5;
  float4 o5 : TEXCOORD6;
  float4 o6 : TEXCOORD7;
  float4 o7 : TEXCOORD8;
#if PES_TESS_V455
  float2 o8 : TEXCOORD9;
#endif
  float4 pR : TEXCOORD10;
#if !PES_TESS_BODY
  float4 pD : TEXCOORD11;
#endif
  float4 pV : TEXCOORD12;
};

struct PesTessConst
{
  float e[3] : SV_TessFactor;
  float i : SV_InsideTessFactor;
  float zs : PESZOOM;   // (08/11 i) factor de la distancia por el zoom (1 = sin zoom); el DS lo usa en el fundido por distancia
};

// (04/11) fuera del bloque del hull shader: el domain shader tambien la usa (sin esto los DS no compilaban)
// (04/11 g) filas de parametros: cara 357 / 358; cuerpo 364 (x prendido, y densidad, z distancia maxima, w tope) con la
// suavidad y357 y la transicion y358 de la cara
void PesTessParams(out float4 T, out float4 U)
{
  T = IniParams.Load(int2(357, 0));   // x on, y suavidad, z densidad, w factor maximo
  U = IniParams.Load(int2(358, 0));   // x distancia maxima (m), y transicion (m), z tope (mm), w orientacion
#if PES_TESS_BODY
  float4 B = IniParams.Load(int2(364, 0));
  T.x = B.x; T.z = B.y; U.x = B.z; U.z = B.w;
#else
  PesTessApplyPlayer(T, U);   // (04/11 m) teselacion por jugador
#endif
}

#if PES_TESS_HS
// ---------------------------------------------------------------- hull shader
cbuffer PESTessView : register(b13)
{
  float4 g_pesTessVp;   // x ancho, y alto del viewport en pixeles (lo pone la DLL)
}
cbuffer cHSScene : register(b2) { float4 hsCb2[8]; }   // (08/11 i) escena del juego (proyeccion en [4..7]); la DLL la enlaza al HS


// (04/11 e) 1 dentro de la zona de la oreja (posicion de reposo en el marco de la cabeza), 0 en el resto de la cara; bordes suaves
float PesTessEar(float3 r)
{
  float ex = smoothstep(0.052, 0.066, abs(r.x));
  float ez = 1.0 - smoothstep(0.080, 0.105, r.z);
  float ey = smoothstep(1.615, 1.635, r.y) * (1.0 - smoothstep(1.725, 1.745, r.y));
  return ex * ez * ey;
}

PesTessConst PesTessPatch(InputPatch<PesTessVS, 3> p)
{
  PesTessConst k;
  float4 T, U; PesTessParams(T, U);
  float f[3] = { 1.0, 1.0, 1.0 };
  bool ok = T.x > 0.5 && p[0].o0.w > 0.01 && p[1].o0.w > 0.01 && p[2].o0.w > 0.01;
  float dmin = min(length(p[0].pV.xyz), min(length(p[1].pV.xyz), length(p[2].pV.xyz)));
  // (08/11 i, Agustin: "la teselacion no se aplica en el cuerpo") con la camara libre con zoom (campo de vision 0.024 rad) la camara
  // queda a 15-40 m y la distancia maxima (z364 15 m, x358 27 m) nunca entraba, aunque el jugador llene la pantalla. La distancia se
  // corrige por el zoom: P11 de la proyeccion del juego (b2, la DLL lo enlaza tambien al HS) contra 6 (campo de vision ~19 grados,
  // una camara de transmision cerrada). Igual para todo el dibujo (sin grietas). Sin zoom o sin b2 (P11 = 0): zs = 1, nada cambia.
  float zs = 1.0;
  { float p11 = abs(hsCb2[5].y); if (p11 > 6.0) zs = 6.0 / p11; }
  k.zs = zs;
  dmin *= zs;
  // mas lejos que la distancia maxima: nada que dividir (el DS tampoco corre nada)
  if (dmin > U.x) ok = false;
  // los tres con peso 0 (triangulo entero en una costura): se queda como esta
  if (p[0].pV.w + p[1].pV.w + p[2].pV.w <= 0.0) ok = false;
#if PES_TESS_BODY
  int bPart = PesBodyPart(p[0].o1.z);
  float4 bOpt = PesBodyPartOpt(bPart);
  const bool bGeo = !PesHasLimbCode(p[0].o1.z);   // (08/11 q) malla propia del jugador: parte por punto
  int3 gP = int3(PesBodyPartGeo(p[0].pR.xyz), PesBodyPartGeo(p[1].pR.xyz), PesBodyPartGeo(p[2].pR.xyz));
  float4 gO0 = PesBodyPartOpt(gP.x), gO1 = PesBodyPartOpt(gP.y), gO2 = PesBodyPartOpt(gP.z);
  if (!bGeo && bOpt.x < 0.5) ok = false;   // (08/11 n) esta parte apagada (417 / 419 / 420 .x = 0)
#endif
  // (04/11 f) los tres puntos por debajo del collar del torso (reposo y < 1.53): nada que dividir (el DS apaga todo ahi)
#if !PES_TESS_BODY
  if (max(p[0].pR.y, max(p[1].pR.y, p[2].pR.y)) < 1.53) ok = false;
#endif
  if (ok) {
    float2 s0 = p[0].o0.xy / p[0].o0.w * 0.5 * g_pesTessVp.xy;
    float2 s1 = p[1].o0.xy / p[1].o0.w * 0.5 * g_pesTessVp.xy;
    float2 s2 = p[2].o0.xy / p[2].o0.w * 0.5 * g_pesTessVp.xy;
    // (04/11 d, Agustin puso 40 creyendo que era mas detalle) z357 = DENSIDAD: mas = mas triangulos; pixeles por segmento = 40 / z
    float px = clamp(40.0 / max(T.z, 0.5), 2.0, 80.0), fmax = clamp(T.w, 1.0, 16.0);
    // (04/11 e, Agustin: "un valor para subir la teselacion de las orejas") y360 = densidad extra en las orejas: zona en reposo
    // (marco de la cabeza: ojos en y 1.697 z 0.138, labios z 0.17): a los costados (|x| > 5-7 cm), detras de los pomulos
    // (z < 8-10 cm) y entre el lobulo y la punta (y 1.62..1.73). Cada arista toma el mayor de sus dos extremos.
#if PES_TESS_BODY
    float em = 1.0; float3 ek = float3(0.0, 0.0, 0.0);
#else
    float em = clamp(IniParams.Load(int2(360, 0)).y, 1.0, 6.0);
    float3 ek = float3(PesTessEar(p[0].pR.xyz), PesTessEar(p[1].pR.xyz), PesTessEar(p[2].pR.xyz));
#endif
    float3 mul3 = lerp(float3(1.0, 1.0, 1.0), float3(em, em, em), float3(max(ek.y, ek.z), max(ek.z, ek.x), max(ek.x, ek.y)));
    // arista k = la opuesta al vertice k (convencion de D3D: e[0] entre los puntos 1 y 2)
    f[0] = clamp(length(s1 - s2) / px * mul3.x, 1.0, min(16.0, fmax * mul3.x));
    f[1] = clamp(length(s2 - s0) / px * mul3.y, 1.0, min(16.0, fmax * mul3.y));
    f[2] = clamp(length(s0 - s1) / px * mul3.z, 1.0, min(16.0, fmax * mul3.z));
#if PES_TESS_BODY
    // (08/11, Agustin: "teselacion a los brazos y manos, piernas") con el relieve del cuerpo (w376) prendido, de cerca (< 1.2..2.2 m)
    // cada arista de mano / brazo / pierna se parte hasta tener ~1.8 mm en el mundo: el detalle (tendones de 1.3 mm, surcos de 3-5 mm)
    // no entra en la malla del juego (aristas de 3-8 mm). Por arista con sus dos extremos: sin grietas. El codigo del miembro
    // (o1.z = 7 + accion / 1200, pes_limbs.hlsli) es el mismo en todo el dibujo.
    {
      float rW = IniParams.Load(int2(376, 0)).w;
      float c0z = p[0].o1.z;
      const bool geoLimb = bGeo && (gP.x + gP.y + gP.z) > 0;
      if (rW > 0.001 && IniParams.Load(int2(305, 0)).x > 0.5 && ((c0z > 6.5 && c0z < 7.5 && round((c0z - 7.0) * 1200.0) < 599.5) || geoLimb)) {
        // (08/11 i, Agustin: "la teselacion de cuerpo no funciona ... hace rato") antes solo a menos de 1.2..2.2 m de la camara: en el
        // editor (camara a 2.5-4 m) y en los primeros planos del partido casi nunca entraba (log: x1.0). Ahora por PANTALLA: cada arista
        // se parte hasta ~1.8 mm en el mundo o hasta 1.5 px en pantalla (lo que de menos partes); queda la distancia maxima z364
        // (corregida por el zoom). Por arista con sus dos extremos (largo en el mundo y en pantalla): sin grietas.
        float3 nk = 1.0;
        if (bGeo) {   // (08/11 q) por arista: alguno de sus dos extremos en mano / brazo / pierna con esa parte prendida
          float3 gk = float3(gP.x > 0 && gO0.x > 0.5, gP.y > 0 && gO1.x > 0.5, gP.z > 0 && gO2.x > 0.5);
          nk = float3(max(gk.y, gk.z), max(gk.z, gk.x), max(gk.x, gk.y));
        }
        float3 lw = float3(length(p[1].pV.xyz - p[2].pV.xyz), length(p[2].pV.xyz - p[0].pV.xyz), length(p[0].pV.xyz - p[1].pV.xyz));
        float3 ls = float3(length(s1 - s2), length(s2 - s0), length(s0 - s1));
        float3 fr = clamp(min(lw / 0.0018, ls / 1.5), 1.0, fmax);
        f[0] = max(f[0], lerp(1.0, fr.x, nk.x));
        f[1] = max(f[1], lerp(1.0, fr.y, nk.y));
        f[2] = max(f[2], lerp(1.0, fr.z, nk.z));
      }
    }
    {   // (08/11 n) densidad por parte (417 / 419 / 420 .y) y de los dedos (418.x, cada arista con el mayor de sus dos extremos)
      float3 dm = float3(bOpt.y, bOpt.y, bOpt.y);
      if (bPart == 1) {
        float fx = PesFingerOpt().x;
        float3 fk = float3(PesFingerK(p[0].pR.xyz), PesFingerK(p[1].pR.xyz), PesFingerK(p[2].pR.xyz));
        dm *= lerp(float3(1.0, 1.0, 1.0), float3(fx, fx, fx), float3(max(fk.y, fk.z), max(fk.z, fk.x), max(fk.x, fk.y)));
      }
      if (bGeo) {   // (08/11 q) densidad por punto (parte apagada = 0) y por arista el mayor de sus dos extremos (0 en los dos = 1, entera)
        float fx = PesFingerOpt().x;
        float3 dv = float3(gO0.x > 0.5 ? gO0.y : 0.0, gO1.x > 0.5 ? gO1.y : 0.0, gO2.x > 0.5 ? gO2.y : 0.0);
        dv *= lerp(float3(1.0, 1.0, 1.0), float3(fx, fx, fx), float3(gP.x == 1 ? PesFingerK(p[0].pR.xyz) : 0.0, gP.y == 1 ? PesFingerK(p[1].pR.xyz) : 0.0, gP.z == 1 ? PesFingerK(p[2].pR.xyz) : 0.0));
        dm = float3(max(dv.y, dv.z), max(dv.z, dv.x), max(dv.x, dv.y));
      }
      f[0] = clamp(f[0] * dm.x, 1.0, 16.0);
      f[1] = clamp(f[1] * dm.y, 1.0, 16.0);
      f[2] = clamp(f[2] * dm.z, 1.0, 16.0);
    }
#endif
    // (07/11, Agustin: "que no consuma todo el rendimiento") ARISTAS DE ESPALDAS: si los dos extremos de una arista miran para el otro
    // lado de la camara (normal contra el vector a la camara < -0.3: nuca, detras de las orejas, la cara del otro lado de la cabeza) esa
    // arista no se parte. Se decide por arista con sus dos extremos, igual en los dos triangulos que la comparten: sin grietas.
    // z376 = 0 apaga este recorte.
    if (IniParams.Load(int2(376, 0)).z > 0.5) {
      float3 aw = float3(dot(normalize(p[0].o6.xyz), -normalize(p[0].pV.xyz)), dot(normalize(p[1].o6.xyz), -normalize(p[1].pV.xyz)), dot(normalize(p[2].o6.xyz), -normalize(p[2].pV.xyz)));
      // (07/11 b) en rampa (no de golpe): cuando la cabeza gira la arista pasa de partida a entera de a poco, sin saltos (vibraba)
      float3 kf = smoothstep(-0.55, -0.15, float3(max(aw.y, aw.z), max(aw.z, aw.x), max(aw.x, aw.y)));
      f[0] = lerp(1.0, f[0], kf.x);
      f[1] = lerp(1.0, f[1], kf.y);
      f[2] = lerp(1.0, f[2], kf.z);
    }
    // fuera de pantalla (los tres puntos del mismo lado): sin division (nada que ver)
    float2 lo = min(min(s0, s1), s2), hi = max(max(s0, s1), s2);
    float2 hv = 0.5 * g_pesTessVp.xy + 2.0;
    if (any(lo > hv) || any(hi < -hv)) { f[0] = 1.0; f[1] = 1.0; f[2] = 1.0; }
  }
  k.e[0] = f[0]; k.e[1] = f[1]; k.e[2] = f[2];
  k.i = (f[0] + f[1] + f[2]) * (1.0 / 3.0);
  return k;
}

[domain("tri")]
[partitioning("fractional_odd")]
#if PES_TESS_CCW
[outputtopology("triangle_ccw")]
#else
[outputtopology("triangle_cw")]
#endif
[outputcontrolpoints(3)]
[patchconstantfunc("PesTessPatch")]
[maxtessfactor(16.0)]
PesTessCP main(InputPatch<PesTessVS, 3> p, uint i : SV_OutputControlPointID)
{
  PesTessCP o;
  o.o0 = p[i].o0;
  o.o1 = p[i].o1;
  o.o2 = p[i].o2;
#if !PES_TESS_V455
  o.p2 = p[i].p2;
#endif
  o.o3 = p[i].o3;
  o.o4 = p[i].o4;
  o.o5 = p[i].o5;
  o.o6 = p[i].o6;
  o.o7 = p[i].o7;
#if PES_TESS_V455
  o.o8 = p[i].o8;
#endif
  o.pR = p[i].pR;
#if !PES_TESS_BODY
  o.pD = p[i].pD;
#endif
  o.pV = p[i].pV;
  return o;
}
#endif   // PES_TESS_HS

#if PES_TESS_DS
// ---------------------------------------------------------------- domain shader
cbuffer cVSScene : register(b2)
{
  struct
  {
    float4x4 m_projectionView;
    float4x4 m_projection;
    float4x4 m_view;
    float4x4 m_shadowProjection;
    float4x4 m_shadowProjection2;
    float4 m_eyepos;
    float4 m_projectionParam;
    float4 m_viewportSize;
    float4 m_exposure;
    float4 m_fogParam[3];
    float4 m_fogColor;
    float4 m_cameraCenterOffset;
    float4 m_shadowMapResolutions;
  } g_vsScene : packoffset(c0);
}

#define PES_LERP3(F) (b.x * p[0].F + b.y * p[1].F + b.z * p[2].F)

#if PES_TESS_ATLAS
// (04/11 piel) variante del DS para el ATLAS de sombra / grosor de las cabezas (headshadow.cpp, t30): la DLL repite el dibujo de
// la cara teselada (mismo VS y HS, mismos factores) sin PS sobre el atlas, con la caja de cada cabeza en b12 (mundo -> atlas, mismo
// formato que el b2 de la repeticion de la pasada de sombras). La posicion teselada esta en la vista de la camara: se lleva al mundo
// con la vista que sale de m_projectionView y m_projection de ESTE cuadro (VV = P^-1 PV, la misma cuenta de headshadow.cpp: P de
// la forma x' = P0 x + P2 z + P3, y' = P5 y + P6 z + P7, w' = z).
cbuffer PESAtlasBox : register(b12)
{
  float4x4 g_pesAtlasPV;   // mundo -> recorte del cuadrante del atlas
}
float4 PesAtlasClip(float3 pv)
{
  float4x4 PV = g_vsScene.m_projectionView, P = g_vsScene.m_projection;
  float4 cw = float4(PV[0][3], PV[1][3], PV[2][3], PV[3][3]);                       // fila w (= z de la vista)
  float4 cx = (float4(PV[0][0], PV[1][0], PV[2][0], PV[3][0]) - P[2][0] * cw - float4(0, 0, 0, P[3][0])) / (abs(P[0][0]) > 1e-6 ? P[0][0] : 1e-6);
  float4 cy = (float4(PV[0][1], PV[1][1], PV[2][1], PV[3][1]) - P[2][1] * cw - float4(0, 0, 0, P[3][1])) / (abs(P[1][1]) > 1e-6 ? P[1][1] : 1e-6);
  float3x3 R = float3x3(cx.xyz, cy.xyz, cw.xyz);
  float3 q = pv - float3(cx.w, cy.w, cw.w);
  // inversa por la adjunta (R puede ser un reflejo: determinante negativo)
  float3 c0 = cross(R[1], R[2]), c1 = cross(R[2], R[0]), c2 = cross(R[0], R[1]);
  float det = dot(R[0], c0); det = abs(det) > 1e-8 ? det : 1e-8;
  float3 w = (c0 * q.x + c1 * q.y + c2 * q.z) / det;
  float4 c = mul(float4(w, 1.0), g_pesAtlasPV);
  c.z += 9.99999975e-005 * c.w;   // el mismo sesgo que el VS de la pasada de sombras del juego (81b5930d: o0.z + 1e-4)
  return c;
}
#endif

// (04/11 c) quinta textura de la cara (la misma del PS de la piel, la DLL la copia al DS)
Texture2D<float4> pesFaceA : register(t23);
Texture2D<float4> pesFaceB : register(t24);
SamplerState pesTessSamp : register(s0);
#if !PES_TESS_BODY
// (08/11 o, Agustin: "mejorar la cantidad de poligonos de las caras ... otra tecnica ademas de la teselacion") ALTURA HD de la piel:
// tools/caras_hd.py integra la normal de 4096 de cada cara en un mapa de altura (<id>_hgt_4k.dds, 1 canal, 0.5 = plano, +-2 mm;
// solo el detalle de menos de ~6 mm). La DLL (facetess.cpp) lo enlaza en t25 cuando esa cara lo tiene; fila 423: x fuerza, y detalle
// (mip), z tope mm, w relieve en la luz. Sin textura (GetDimensions = 0) no hace nada.
Texture2D<float> pesFaceH : register(t25);
// inclina el marco (N, T, B) con la pendiente g (altura por unidad de superficie, en T y en B): Rodrigues, T y B siguen perpendiculares
void PesTessTilt(inout float3 N, inout float3 T, inout float3 B, float2 g, inout bool on)
{
  float3 Nt = normalize(N - T * g.x - B * g.y);
  float3 ax = cross(N, Nt); float sa = length(ax), ca = dot(N, Nt);
  if (sa > 1e-5) {
    ax /= sa;
    T = T * ca + cross(ax, T) * sa + ax * dot(ax, T) * (1.0 - ca);
    B = B * ca + cross(ax, B) * sa + ax * dot(ax, B) * (1.0 - ca);
    N = Nt; on = true;
  }
}
#endif

#if !PES_TESS_BODY
// (07/11, Agustin: "las arrugas en la vida real tienen peso, volumen ... deforman partes del modelo") VOLUMEN DE LAS ARRUGAS: la DLL
// copia al DS lo que el PS de la piel usa para las arrugas (b4 pesos por zona del motor, t5 / t6 normal y mascara del juego,
// t20 / t21 las nuestras, t31 las esculpidas de la sonrisa). Con el mismo peso que el PS y el surco medido ancho (wrFoldW), la piel
// se hunde z366 mm en el surco y sube w366 mm en la cresta: cambia la silueta y la sombra, no solo la luz. Sin nada enlazado = 0.
cbuffer cPSMaterialT : register(b4) { float4 g_tMat[8]; }
Texture2D<float4> tWrAnimN : register(t5);
Texture2D<float4> tWrMaskG : register(t6);
Texture2D<float4> tWrN     : register(t20);
Texture2D<float4> tWrM     : register(t21);
Texture2D<float4> tShapeWr : register(t31);
#define PES_WRD_NO_PS 1
#include "pes_wrdepth.hlsli"   // (09/11 g) arrugas por gesto con profundidad (x15 = 2), las mismas del PS
float4 PesTessWrSampN(bool hi, float2 uv, float l) { return hi ? tWrN.SampleLevel(pesTessSamp, uv, l) : tWrAnimN.SampleLevel(pesTessSamp, uv, l); }
// devuelve (surco, cresta) 0..1 con el peso del gesto
float2 PesTessWrinkle(float2 uv, float age)
{
  float4 P13 = IniParams.Load(int2(13, 0));
  if (P13.w < 0.5) return 0;
  float4 P7 = IniParams.Load(int2(7, 0)), P14 = IniParams.Load(int2(14, 0)), P15 = IniParams.Load(int2(15, 0));
  bool hi = P15.x > 0.5;
  float ys = P15.w > 0 ? 1.0 : -1.0;
  // peso por zona, igual que el PS (5ce3 / ce188 "arrugas animadas")
  float4 mT = hi ? tWrM.SampleLevel(pesTessSamp, uv * float2(1, 0.5), 0) : tWrMaskG.SampleLevel(pesTessSamp, uv * float2(1, 0.5), 0);
  float4 mB = hi ? tWrM.SampleLevel(pesTessSamp, uv * float2(1, 0.5) + float2(0, 0.5), 0) : tWrMaskG.SampleLevel(pesTessSamp, uv * float2(1, 0.5) + float2(0, 0.5), 0);
  float4 wz = float4(P14.x > 0 ? P14.x : 1.0, P14.y > 0 ? P14.y : 1.0, P14.z > 0 ? P14.z : 1.0, P14.w > 0 ? P14.w : 1.0);
  float4 wTop = g_tMat[3];
  float4 wBot = float4(uv.x < 0.5 ? g_tMat[0].y : g_tMat[4].x, g_tMat[4].yzw);
  float smile = sqrt(saturate(max(g_tMat[0].y, g_tMat[4].x)) * saturate(wTop.w));
  wTop *= float4(wz.x, wz.x, wz.y, wz.z * (1.0 + max(P13.z, 0.0) * smile));
  wBot.x *= wz.w;
  float4 XE = IniParams.Load(int2(340, 0)), XZ = IniParams.Load(int2(341, 0));
  if (XE.x > 0.5) { float g = max(XE.y, 0.0);
    wTop *= float4(g * XZ.x, g * XZ.x, g * XZ.x, 1.0 + (g - 1.0) * (XZ.y + XZ.z) * 0.5);
    wBot.x *= 1.0 + (g - 1.0) * XZ.w; }
  wBot += saturate(IniParams.Load(int2(347, 0)).x) * float4(0.35, 0.60, 0.60, 0.30);
  // (09/11 c, Agustin: "con la teselacion puedes hacer las arrugas mas notorias?") los gestos del mod son por huesos y el motor no
  // manda pesos de arrugas: los mismos pesos por slider que el PS (5ce3 / ce188), asi el surco se hunde de verdad con el gesto
  uint swM = 0, shM = 0; tShapeWr.GetDimensions(swM, shM);
  float4 G5a = IniParams.Load(int2(355, 0)), G6a = IniParams.Load(int2(356, 0));
  const bool own = swM > 1 && G6a.w > 0.5;   // cara con arrugas horneadas propias (Messi)
  { float sS = saturate(G5a.y), sS2 = saturate(G5a.y - 1.0), sA = saturate(G5a.x), sU = saturate(G5a.z), sG = saturate(G6a.x);
    if (own) { sS *= 0.75; sS2 *= 0.75; }
    wTop = max(wTop, float4(0.75 * sU + 0.60 * sG, 0.45 * sU, 0.85 * sA + 0.60 * sG, 0.70 * sS + 0.30 * sS2 + 0.40 * sG));
    wBot.x = max(wBot.x, 0.85 * sS + 0.15 * sS2 + 0.60 * sG); }
  // (09/11 g) x15 = 2: el volumen sale de la misma profundidad por gesto que el PS (pes_wrdepth.hlsli), con la edad del jugador:
  // surco = fondo de la arruga ya formada (fina y profunda con el gesto entero), cresta = piel juntada entre surcos
  float2 wrDr = 0; bool wrDon = false;
  { uint dw = 0, dh = 0; pesWrDA.GetDimensions(dw, dh);
    if (P15.x > 1.5 && dw > 1) {
      float4 wa = float4(saturate(wTop.x + 0.5 * wTop.y), wTop.z, wTop.w, wBot.x);
      float4 wb = float4(saturate(0.6 * wBot.x + 0.4 * max(wBot.y, wBot.z)), max(wBot.y, wBot.z), wBot.w, wTop.w);
      float dz = saturate(P15.y), gn = P7.x > 0 ? P7.x : 1.0, cv = max(P13.x, 0.0);
      wa = saturate((wa - dz) / max(1.0 - dz, 0.05) * gn); wb = saturate((wb - dz) / max(1.0 - dz, 0.05) * gn);
      wa = 1.0 - pow(1.0 - wa, 1.0 + cv); wb = 1.0 - pow(1.0 - wb, 1.0 + cv);
      float gain = 1.0; PesWrAge(PesWrAgeK(age, max(P15.z, 0.0)), wa, wb, gain);
      wa = saturate(wa); wb = saturate(wb);
      float4 pa = 1.0 + 2.5 * wa, pb = 1.0 + 2.5 * wb;
      float4 ka = kWrDmmA * pow(wa, 0.8) * gain, kb = kWrDmmB * pow(wb, 0.8) * gain;
      float dd = PesWrD1(pesWrDA.SampleLevel(pesTessSamp, uv, 1), pesWrDB.SampleLevel(pesTessSamp, uv, 1), pa, pb, ka, kb);
      float dB = dot(ka, pesWrDA.SampleLevel(pesTessSamp, uv, 3)) + dot(kb, pesWrDB.SampleLevel(pesTessSamp, uv, 3));
      wrDr = float2(saturate(dd / 0.30), 0.7 * saturate((dB - dd) / 0.10) * saturate(dB / 0.12));
      wrDon = true;
    } }
  float w = saturate(dot(wTop, mT) + dot(wBot, mB));
  float dead = saturate(P15.y);
  w = saturate((w - dead) / max(1.0 - dead, 0.05));
  w = saturate(w * (P7.x > 0 ? P7.x : 1.0));
  w = 1.0 - pow(1.0 - w, 1.0 + max(P13.x, 0.0));
  float2 r = wrDr;
  if (!wrDon && w > 0.002) {
    uint tw = 0, th = 0; if (hi) tWrN.GetDimensions(tw, th); else tWrAnimN.GetDimensions(tw, th);
    if (tw > 1) {
      // el surco ancho del PS (wrFoldW: 6 texeles, un mip mas suave)
      float2 du = float2(6.0 / tw, 0), dv = float2(0, 6.0 / th); float l = 1.0;
      float4 a = PesTessWrSampN(hi, uv + du, l), b = PesTessWrSampN(hi, uv - du, l), c = PesTessWrSampN(hi, uv + dv, l), d = PesTessWrSampN(hi, uv - dv, l);
      float z = (a.x * a.w - b.x * b.w) * 0.5 + ys * (c.y - d.y) * 0.5;
      r = float2(saturate(-z * 2.0), saturate(z * 2.0)) * w;
    }
  }
  // esculpidas de la sonrisa (Messi): slider Sonrisa 355.y con Perfiles por cara 356.w; de 1 a 1.5 pasa a las grandes (BA)
  { float4 G5 = IniParams.Load(int2(355, 0)), G6 = IniParams.Load(int2(356, 0));
    float shW = G6.w > 0.5 ? max(saturate(G5.y), saturate(IniParams.Load(int2(391, 0)).w)) * 2.0 : 0.0;   // (09/11 e) tambien con la sonrisa del motor (w391)   // (09/11 c) horneadas de Messi al doble, como en el PS
    uint sw = 0, sh = 0; tShapeWr.GetDimensions(sw, sh);
    if (shW > 0.001 && sw > 1) {
      float bl = saturate((G5.y - 1.0) * 2.0), wx = 6.0 / sw, wy = 6.0 / sh;
      float4 a = tShapeWr.SampleLevel(pesTessSamp, uv + float2(wx, 0), 2), b = tShapeWr.SampleLevel(pesTessSamp, uv - float2(wx, 0), 2);
      float4 c = tShapeWr.SampleLevel(pesTessSamp, uv + float2(0, wy), 2), d = tShapeWr.SampleLevel(pesTessSamp, uv - float2(0, wy), 2);
      float z = (lerp(a.x, a.z, bl) - lerp(b.x, b.z, bl)) * 0.5 + ys * (lerp(c.y, c.w, bl) - lerp(d.y, d.w, bl)) * 0.5;
      r = saturate(r + float2(saturate(-z * 2.0), saturate(z * 2.0)) * shW);
    } }
  // (09/11 c, Agustin: "en el caso de messi deberias subir la intensidad un poco solo para messi") volumen x1.5 en su cara
  return own ? r * 1.5 : r;
}
#endif

[domain("tri")]
PesTessVS main(PesTessConst k, float3 b : SV_DomainLocation, const OutputPatch<PesTessCP, 3> p)
{
  PesTessVS o;
  float4 T, U; PesTessParams(T, U);
  // posicion lineal (la del triangulo plano) y la de Phong (proyectada sobre los tres planos tangentes)
  float3 P0 = p[0].pV.xyz, P1 = p[1].pV.xyz, P2 = p[2].pV.xyz;
  float3 N0 = normalize(p[0].o6.xyz), N1 = normalize(p[1].o6.xyz), N2 = normalize(p[2].o6.xyz);
  float3 pl = b.x * P0 + b.y * P1 + b.z * P2;
#if PES_TESS_BODY
  // (08/11 k, Agustin: "se ven costuras en la union de las munecas y en otras partes") en el cuerpo siempre Phong: el PN (y376 = 1)
  // empuja hacia afuera el anillo de al lado del borde de cada pieza (mano / antebrazo, pierna / media), que queda fijo, y la union
  // se ve como un pliegue. El PN sigue para la cara.
  // (08/11 n) opciones por parte: en los dedos la forma puede pasar a la curva cubica (418.z; adentro de la mano, lejos de los bordes)
  int bPart = PesBodyPart(p[0].o1.z);
  const bool bGeo = !PesHasLimbCode(p[0].o1.z);   // (08/11 q) malla propia del jugador: parte por la posicion de reposo de este punto
  if (bGeo) bPart = PesBodyPartGeo(PES_LERP3(pR).xyz);
  float4 bOpt = PesBodyPartOpt(bPart), fOpt = PesFingerOpt();
  if (bGeo && bOpt.x < 0.5) bOpt = float4(0.0, 0.0, 0.0, 0.0);   // parte apagada: sin redondeo ni relieve
  float fkD = bPart == 1 ? PesFingerK(PES_LERP3(pR).xyz) : 0.0;
  float3 pp = PesTessSmooth(P0, P1, P2, N0, N1, N2, b, fOpt.z * fkD);
#else
  float3 pp = PesTessSmooth(P0, P1, P2, N0, N1, N2, b, IniParams.Load(int2(376, 0)).y);   // (07/11) Phong o PN (y376)
#endif
  // (04/11 h, Agustin: "se nota el bajo nivel poligonal en el lagrimal") peso 2 = borde de la abertura del ojo (la DLL lo marca solo en
  // la cara): redondea con x366 (0..1) y su propio tope y366 mm; el relieve no entra ahi (seamR)
  float4 EB = IniParams.Load(int2(366, 0));
  float3 wv = float3(p[0].pV.w, p[1].pV.w, p[2].pV.w);
  float3 isEye = step(1.5, wv);
  float3 wPh = lerp(saturate(wv), saturate(EB.xxx), isEye);
  float eyeMix = dot(b, isEye);
  float seam = saturate(dot(b, wPh));   // 0 en las costuras / bordes de la pieza
  // (07/11) peso 1.25 = costura de textura suave (las dos copias con la misma normal y huesos, expr.cpp SeamWeights): se redondea
  // con Phong pero sin nada que lea la textura (relieve, arrugas), porque cada lado de la costura tiene otras uv
  float3 isSoft = step(1.1, wv) - isEye;
  wPh *= 1.0 - isSoft * (1.0 - saturate(IniParams.Load(int2(376, 0)).x));   // x376 = 0: las costuras de textura vuelven a quedar fijas
  seam = saturate(dot(b, wPh));
  float seamR = saturate(dot(b, saturate(wv) * (1.0 - isEye - isSoft)));
  // (04/11 f, Agustin: "en el cuello a veces lo rompe, que se desactive en la parte baja del cuello cerca del collar") el anillo
  // del collar del torso esta en y 1.51..1.56 del reposo (pes_collar.hlsli); de ahi hasta z360 cm mas arriba (defecto 6, hasta la
  // base del menton en 1.58) el corrimiento, el relieve y la inclinacion de la normal se apagan en rampa; el torso no se tesela.
#if !PES_TESS_BODY
  { float nh = clamp(IniParams.Load(int2(360, 0)).z, 0.0, 15.0) * 0.01;
    seam *= smoothstep(1.53, 1.53 + max(nh, 0.01), PES_LERP3(pR).y); }
#else
  // (04/11 g) cuerpo: hombros, cuello y escote (reposo y > 1.40..1.47) quedan como el juego: ahi el torso se mete bajo la camiseta
  // y bajo el cuello de la cara (z352 / x351), un redondeo hacia afuera los haria asomar
  seam *= 1.0 - smoothstep(1.40, 1.47, PES_LERP3(pR).y);
#endif
  float dist = length(pl) * k.zs;   // (08/11 i) distancia corregida por el zoom (HS)
  float fade = 1.0 - smoothstep(U.x - max(U.y, 0.01), U.x, dist);
#if PES_TESS_BODY
  // (08/11 n) redondeo por parte (417 / 419 / 420 .w) y de los dedos (418.y): hasta 1.25 (un poco mas que Phong entero)
  float rk = bOpt.w * lerp(1.0, fOpt.y, fkD);
  float3 d = (pp - pl) * (min(saturate(T.y) * rk, 1.25) * seam * fade * (T.x > 0.5 ? 1.0 : 0.0));
#else
  float3 d = (pp - pl) * (saturate(T.y) * seam * fade * (T.x > 0.5 ? 1.0 : 0.0));
#endif
  float dl = length(d), tope = lerp(max(U.z, 0.0), clamp(EB.y, 0.0, max(U.z, 0.0)), saturate(eyeMix * 2.0)) * 0.001;
#if PES_TESS_BODY
  // (08/11 k) tope del redondeo del cuerpo 1.5 mm aunque w364 diga mas (el ini tiene 3.4): mas que eso deja el pliegue en la union
  // con la pieza de al lado (el borde no se mueve)
  tope = min(tope, 0.0015) * clamp(rk, 1.0, 2.0);   // (08/11 n) con mas redondeo en esa parte, el tope sube igual (hasta 3 mm)
#endif
  if (dl > tope) d *= tope / max(dl, 1e-6);
#if PES_TESS_BODY
  {   // (08/11, Agustin: "teselacion a los brazos y manos, piernas ... sin mucha deformacion") RELIEVE DEL CUERPO: la altura del detalle
      // anatomico (pes_limb_relief.hlsli: mano PesHandH, brazo PesArmH = la de pes_arm_anat_c, pierna PesLegH) desplaza la superficie
      // teselada por la normal, tope 1.5 mm, w376 x la definicion general x172. Bordes de la pieza quietos (seam), se apaga con la distancia.
    float rW = IniParams.Load(int2(376, 0)).w;
    float c0z = p[0].o1.z;
    // (08/11 r, Agustin con 08099d3: "la teselacion deforma las uniones con las manos, asi como con las piernas") el relieve anatomico
    // (y su base) esta medido sobre TorsoBody: sus rampas de borde (muneca, hombro, tobillo) no caen donde terminan las mallas propias
    // del jugador y la piel subia hasta el borde de la pieza. En las mallas propias solo hay teselacion y redondeo (sin relieve).
    if (rW > 0.001 && T.x > 0.5 && IniParams.Load(int2(305, 0)).x > 0.5 && c0z > 6.5 && c0z < 7.5) {
      float q = round((c0z - 7.0) * 1200.0);
      float3 r0 = PES_LERP3(pR).xyz; float3 pm = float3(abs(r0.x), r0.y, r0.z);
      float hh = 0.0, crz = 0.0;
      // (08/11 l, captura de Agustin con la vista de prueba: brazo y mano casi sin amarillo) los surcos (entre musculos del brazo
      // 0.5..0.8 mm, entre metacarpianos, separaciones de la pierna) son la mayor parte del relieve y el tope >= 0 los borraba: el brazo
      // de arriba no se movia nada. Ahora la piel sube una BASE (lo mas hondo de los surcos de esa pieza) y el relieve se suma encima:
      // los surcos quedan en 0 (no entran) y todo lo demas sale; se ve el mismo dibujo que en la luz, siempre hacia afuera. La base se
      // apaga igual que el relieve en los bordes de cada malla (hombro / muneca / tobillo / short), sin escalon con la pieza de al lado.
      float hb = 0.0;
      if (q < 511.5) { hh = PesArmH(pm); hb = 0.0008 * PesArmReliefKeep(pm); }
      else if (q > 515.5 && q < 527.5) { hh = PesHandH(pm, saturate((q - 520.0) / 7.0), crz); hb = 0.0003 * PesHandFade(pm); }
      else if (q > 527.5 && q < 599.5) {   // tension por grupo en o1.y = 2 + (cuadriceps + 8 isquios + 64 gemelo) / 1024
        float na = round((p[0].o1.y - 2.0) * 1024.0);
        float3 act = (p[0].o1.y > 1.99 && p[0].o1.y < 2.6) ? float3(fmod(na, 8.0), fmod(floor(na / 8.0), 8.0), floor(na / 64.0)) / 7.0 : float3(0.3, 0.3, 0.3);
        hh = PesLegH(pm, fmod(q - 528.0, 8.0) / 7.0, saturate(act), crz);
        hb = 0.0006 * PesLegFade(pm);
      }
      float dGr = IniParams.Load(int2(172, 0)).x; dGr = dGr > 0.01 ? clamp(dGr, 0.0, 3.0) : 1.0;
      // (08/11 k) solo hacia AFUERA: manos y brazos tienen una prepasada de profundidad sin teselar (VS c6ef0c75); un surco metido hacia
      // adentro fallaria la prueba de profundidad y dejaria un agujero. Los surcos se marcan en la luz (PS aa7d1833, PesLimbRelief).
      // (08/11 m, Agustin: "no veo diferencias ni estando extremadamente cerca") las alturas de pes_limb_relief son las anatomicas
      // reales (0.3..1.8 mm): con 20 cm de brazo en pantalla eso es menos de un pixel. w376 = 1 ahora las lleva x4 (nudillos ~3 mm,
      // gemelo ~7 mm, separaciones del brazo ~3 mm) con tope 1 cm; el PS sube la luz del relieve en la misma medida.
      float hm = clamp((hh + hb) * 4.0 * clamp(rW, 0.0, 3.0) * dGr * bOpt.z * lerp(1.0, fOpt.w, fkD), 0.0, 0.010);   // (08/11 n) x parte / dedos
      float3 Nr = normalize(b.x * N0 + b.y * N1 + b.z * N2);
      d += Nr * (hm * min(seam, seamR) * fade);
      { float dn = dot(d, Nr); if (dn < 0.0) d -= Nr * dn; }   // el redondeo tampoco mete nada hacia adentro
    }
  }
#endif
  // (04/11 c) relieve desde la quinta textura (fila 359): por la normal interpolada, mm
  float4 R = IniParams.Load(int2(359, 0));   // x fuerza, y hundir surcos (mm), z levantar relieve (mm), w mip
#if !PES_TESS_BODY
  PesTessApplyRelief(R);   // (04/11 m) relieve por jugador
#endif
  bool tilt = false; float3 tiltN = 0, tiltT = 0, tiltB = 0;   // (04/11 d) relieve en la luz
#if !PES_TESS_BODY
  if (R.x > 0.001 && T.x > 0.5) {
#if PES_TESS_V455
    float2 uv = PES_LERP3(o2).zw;   // ce188: UV de la cara en TEXCOORD0.zw
#else
    float2 uv = PES_LERP3(o2).xy;
#endif
    float lod = clamp(R.w, 0.0, 6.0);
    float4 FB = pesFaceB.SampleLevel(pesTessSamp, uv, lod);
    if (FB.a > 0.5) {
      float4 FA = pesFaceA.SampleLevel(pesTessSamp, uv, lod);
      float hmm = max(R.z, 0.0) * (FB.b + saturate(FA.g * 2.0 - 1.0)) - max(R.y, 0.0) * FA.r;
      // (04/11 c, regla de Agustin: la boca por dentro no se toca) alrededor de la abertura de la boca (posicion de reposo, la de
      // expresiones: labios y 1.622..1.635, comisuras |x| 2.5 cm) la piel nunca se hunde hacia adentro; solo puede subir (afuera)
      float3 r0 = PES_LERP3(pR).xyz;
      float2 me = float2(r0.x / 0.034, (r0.y - 1.6285) / 0.016);
      float mouthK = smoothstep(1.0, 1.6, length(me));
      if (r0.z < 0.09) mouthK = 1.0;   // detras de la cara (nuca, orejas): sin zona de boca
      if (hmm < 0.0) hmm *= mouthK;
      float3 Nn = normalize(b.x * N0 + b.y * N1 + b.z * N2);
      d += Nn * (clamp(hmm * R.x, -3.0, 3.0) * 0.001 * min(seam, seamR) * fade);
      // (04/11 d, Agustin: "no hace nada") RELIEVE EN LA LUZ (w360): un corrimiento de 1 mm casi no cambia la imagen de frente
      // porque la luz sale de la normal, no de la posicion. Aca la normal (y con ella la tangente y la bitangente que arman el
      // marco del normal map en el PS) se inclina con la pendiente de la altura, asi el relieve tambien se ve en el sombreado.
      float relL = IniParams.Load(int2(360, 0)).w * PesTessReliefLight();   // (04/11 m) x el del jugador
      if (abs(relL) > 0.001) {
        float e = 2.0 / 1024.0;
        float4 FBu = pesFaceB.SampleLevel(pesTessSamp, uv + float2(e, 0), lod), FAu = pesFaceA.SampleLevel(pesTessSamp, uv + float2(e, 0), lod);
        float4 FBv = pesFaceB.SampleLevel(pesTessSamp, uv + float2(0, e), lod), FAv = pesFaceA.SampleLevel(pesTessSamp, uv + float2(0, e), lod);
        float hu = max(R.z, 0.0) * (FBu.b + saturate(FAu.g * 2.0 - 1.0)) - max(R.y, 0.0) * FAu.r;
        float hv = max(R.z, 0.0) * (FBv.b + saturate(FAv.g * 2.0 - 1.0)) - max(R.y, 0.0) * FAv.r;
        // pendiente: mm de altura por mm de piel (una unidad de UV ~ 250 mm de cara)
        float2 g = float2(hu - hmm, hv - hmm) / (e * 250.0) * R.x * relL * min(seam, seamR) * fade;
        float3 Tn = normalize(b.x * p[0].o4.xyz + b.y * p[1].o4.xyz + b.z * p[2].o4.xyz);
        float3 Bn = normalize(b.x * p[0].o5.xyz + b.y * p[1].o5.xyz + b.z * p[2].o5.xyz);
        float3 Nt = normalize(Nn - Tn * g.x - Bn * g.y);
        // el marco entero gira con la normal (Rodrigues): tangente y bitangente siguen perpendiculares a ella
        float3 ax = cross(Nn, Nt); float sa = length(ax), ca = dot(Nn, Nt);
        if (sa > 1e-5) {
          ax /= sa;
          tiltT = Tn * ca + cross(ax, Tn) * sa + ax * dot(ax, Tn) * (1.0 - ca);
          tiltB = Bn * ca + cross(ax, Bn) * sa + ax * dot(ax, Bn) * (1.0 - ca);
          tiltN = Nt; tilt = true;
        }
      }
    }
  }
  // (08/11 o) DESPLAZAMIENTO desde la altura HD (t25, fila 423): la piel sube y baja con el relieve real de la textura de 4096
  // (poros grandes, pliegues de la nariz, cicatrices) en mm; en la ZONA DE LA BOCA (labios y alrededor, reposo |x| < 5.4 cm,
  // y 1.6285 +- 2.6 cm, donde dibuja la boca 3D) vale 0 en los dos sentidos (regla de Agustin: la boca no se toca, sin excepciones);
  // bordes de la pieza quietos (seam), se apaga con la distancia; y la luz sigue a la forma (w423) como el relieve de la quinta textura.
  { float4 HD = IniParams.Load(int2(423, 0));
    uint hw = 0, hh = 0; pesFaceH.GetDimensions(hw, hh);
    if (HD.x > 0.001 && T.x > 0.5 && hw > 1) {
#if PES_TESS_V455
      float2 uvH = PES_LERP3(o2).zw;
#else
      float2 uvH = PES_LERP3(o2).xy;
#endif
      float lodH = clamp(HD.y, 0.0, 6.0);
      float hmmH = (pesFaceH.SampleLevel(pesTessSamp, uvH, lodH) - 0.5) * 4.0 * HD.x;   // mm (+-2 mm en el archivo)
      float3 r0 = PES_LERP3(pR).xyz;
      float2 me = float2(r0.x / 0.034, (r0.y - 1.6285) / 0.016);
      float mouthK = r0.z < 0.09 ? 1.0 : smoothstep(1.0, 1.6, length(me));   // 0 en la boca y los labios, 1 desde ~1.6 veces la abertura
      float topeH = clamp(HD.z, 0.0, 10.0);
      hmmH = clamp(hmmH, -topeH, topeH);
      float kH = min(seam, seamR) * fade * mouthK;
      float3 NnH = normalize(b.x * N0 + b.y * N1 + b.z * N2);
      d += NnH * (hmmH * 0.001 * kH);
      float relH = max(HD.w, 0.0) * kH;
      if (relH > 0.001) {
        float eH = 2.0 / max((float)hw, 256.0);
        float hu = (pesFaceH.SampleLevel(pesTessSamp, uvH + float2(eH, 0), lodH) - 0.5) * 4.0 * HD.x;
        float hv = (pesFaceH.SampleLevel(pesTessSamp, uvH + float2(0, eH), lodH) - 0.5) * 4.0 * HD.x;
        float2 gH = (float2(hu, hv) - hmmH) / (eH * 250.0) * relH;   // mm por mm (una unidad de UV ~ 250 mm)
        if (!tilt) { tiltN = NnH; tiltT = normalize(b.x * p[0].o4.xyz + b.y * p[1].o4.xyz + b.z * p[2].o4.xyz); tiltB = normalize(b.x * p[0].o5.xyz + b.y * p[1].o5.xyz + b.z * p[2].o5.xyz); }
        PesTessTilt(tiltN, tiltT, tiltB, gH, tilt);
      }
    } }
  // (07/11) volumen de las arrugas del gesto (z366 surco, w366 cresta, mm): la boca y los labios no se tocan (regla de Agustin)
  if (T.x > 0.5 && (EB.z > 0.001 || EB.w > 0.001)) {
#if PES_TESS_V455
    float2 uvW = PES_LERP3(o2).zw;
#else
    float2 uvW = PES_LERP3(o2).xy;
#endif
    float2 fr = PesTessWrinkle(uvW, p[0].pR.w > 10.5 && p[0].pR.w < 12.5 ? (p[0].pR.w - 11.0) * 100.0 : 0.0);   // (09/11 g) edad (pR.w = 11 + edad / 100)
    float3 r0 = PES_LERP3(pR).xyz;
    float2 me = float2(r0.x / 0.034, (r0.y - 1.6285) / 0.016);
    float mouthK = r0.z < 0.09 ? 1.0 : smoothstep(1.0, 1.6, length(me));
    float hW = (clamp(EB.w, 0.0, 2.0) * fr.y - clamp(EB.z, 0.0, 2.0) * fr.x) * mouthK;
    float3 Nn = normalize(b.x * N0 + b.y * N1 + b.z * N2);
    d += Nn * (hW * 0.001 * min(seam, seamR) * fade);
  }
#endif   // !PES_TESS_BODY (relieve solo en la cara)
  // (04/11 d) PRUEBA (fila 360.x): infla la piel 1 cm por la normal, sin tope ni distancia, para ver sin duda que la teselacion
  // llega a la imagen (los bordes de cada pieza quedan fijos). Solo para probar: volver a "No".
  // (08/11 k) x360 = 2: VISTA DE PRUEBA del cuerpo (colores en el PS de la piel, sin inflar)
  { float x360 = IniParams.Load(int2(360, 0)).x;
    if (x360 > 0.5 && x360 < 1.5) d += normalize(b.x * N0 + b.y * N1 + b.z * N2) * (0.01 * min(seam, seamR)); }
  float3 pv = pl + d;

#if PES_TESS_ATLAS
  float4 clip = PesAtlasClip(pv);   // (04/11 piel) cara teselada en el atlas de las cabezas
#else
  float4 clip = mul(float4(pv, 1.0), g_vsScene.m_projection);
#endif
  o.o0 = clip;
  o.o1 = PES_LERP3(o1); o.o1.w = p[0].o1.w;
  o.o2 = PES_LERP3(o2);
#if !PES_TESS_V455
  o.p2 = PES_LERP3(p2); o.p2.x = clip.w;
#endif
  o.o3 = PES_LERP3(o3); o.o3.xyz = -normalize(pv);
  o.o4 = PES_LERP3(o4);
  o.o5 = PES_LERP3(o5);
  o.o6 = PES_LERP3(o6); o.o6.w = p[0].o6.w;   // tono de piel por jugador (1000 + entrada): igual en los tres
  if (tilt) { o.o4.xyz = tiltT; o.o5.xyz = tiltB; o.o6.xyz = tiltN; }   // (04/11 d) relieve en la luz (la bitangente ya trae el signo del VS)
  o.o7 = PES_LERP3(o7); o.o7.xy = clip.xy;
#if PES_TESS_V455
  o.o8 = PES_LERP3(o8); o.o8.x = clip.w;
#endif
#if !PES_TESS_ATLAS
  {
    // (04/11 TAA, Agustin: "con la teselacion y las expresiones todo vibra demasiado") el cuadro anterior (o7.zw / w en TEXCOORD9.y)
    // venia interpolado SIN el corrimiento (Phong + relieve): el vector de movimiento llevaba el corrimiento como si la piel se
    // moviera y el TAA reproyectaba la historia al lugar equivocado -> temblor. Ahora el punto nuevo se mueve en pantalla igual que
    // el triangulo original: anterior = actual corrido - (movimiento del triangulo sin teselar).
    float4 l7 = PES_LERP3(o7);
#if PES_TESS_V455
    float2 lw = PES_LERP3(o8);
#else
    float2 lw = PES_LERP3(p2);
#endif
    float wc = max(lw.x, 1e-4), wp = max(lw.y, 1e-4);
    float2 mot = l7.xy / wc - l7.zw / wp;
    o.o7.zw = (clip.xy / max(clip.w, 1e-4) - mot) * wp;
  }
#endif
  o.pR = PES_LERP3(pR); o.pR.w = p[0].pR.w;
#if PES_TESS_BODY
  // (08/11 k, Agustin: "quieres agregar una vista debug del cuerpo?") x360 = 2: el PS aa7d1833 pinta la piel del cuerpo por cuanto se
  // tesela y cuanto se movio: pR.w = 2 + nivel del factor interior (0..5, igual en todo el parche) + corrimiento / 1.5 mm (0..0.9)
  if (IniParams.Load(int2(360, 0)).x > 1.5) {
    // (08/11 l) escala por duplicacion (antes lineal 1..16: x1..x3.9 salia todo azul y el cuerpo, x5-x6 de promedio, no pasaba
    // del celeste): x1 azul, x2 celeste, x4 verde, x8 naranja, x16 rojo
    float lv = round(saturate(log2(max(k.i, 1.0)) / 4.0) * 5.0), mv = saturate(length(d) / 0.006);
    o.pR.w = 2.0 + lv + 0.9 * mv;
  }
#endif
#if !PES_TESS_BODY
  o.pD = PES_LERP3(pD); o.pD.w = p[0].pD.w;
#endif
  o.pV = float4(pv, seam);
  return o;
}
#endif   // PES_TESS_DS
