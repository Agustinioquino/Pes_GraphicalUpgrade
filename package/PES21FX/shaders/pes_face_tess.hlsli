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
#endif
}

#if PES_TESS_HS
// ---------------------------------------------------------------- hull shader
cbuffer PESTessView : register(b13)
{
  float4 g_pesTessVp;   // x ancho, y alto del viewport en pixeles (lo pone la DLL)
}


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
  // mas lejos que la distancia maxima: nada que dividir (el DS tampoco corre nada)
  if (dmin > U.x) ok = false;
  // los tres con peso 0 (triangulo entero en una costura): se queda como esta
  if (p[0].pV.w + p[1].pV.w + p[2].pV.w <= 0.0) ok = false;
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

[domain("tri")]
PesTessVS main(PesTessConst k, float3 b : SV_DomainLocation, const OutputPatch<PesTessCP, 3> p)
{
  PesTessVS o;
  float4 T, U; PesTessParams(T, U);
  // posicion lineal (la del triangulo plano) y la de Phong (proyectada sobre los tres planos tangentes)
  float3 P0 = p[0].pV.xyz, P1 = p[1].pV.xyz, P2 = p[2].pV.xyz;
  float3 N0 = normalize(p[0].o6.xyz), N1 = normalize(p[1].o6.xyz), N2 = normalize(p[2].o6.xyz);
  float3 pl = b.x * P0 + b.y * P1 + b.z * P2;
  float3 pp = b.x * (pl - dot(pl - P0, N0) * N0)
            + b.y * (pl - dot(pl - P1, N1) * N1)
            + b.z * (pl - dot(pl - P2, N2) * N2);
  // (04/11 h, Agustin: "se nota el bajo nivel poligonal en el lagrimal") peso 2 = borde de la abertura del ojo (la DLL lo marca solo en
  // la cara): redondea con x366 (0..1) y su propio tope y366 mm; el relieve no entra ahi (seamR)
  float4 EB = IniParams.Load(int2(366, 0));
  float3 wv = float3(p[0].pV.w, p[1].pV.w, p[2].pV.w);
  float3 isEye = step(1.5, wv);
  float3 wPh = lerp(saturate(wv), saturate(EB.xxx), isEye);
  float eyeMix = dot(b, isEye);
  float seam = saturate(dot(b, wPh));   // 0 en las costuras / bordes de la pieza
  float seamR = saturate(dot(b, wv * (1.0 - isEye)));
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
  float dist = length(pl);
  float fade = 1.0 - smoothstep(U.x - max(U.y, 0.01), U.x, dist);
  float3 d = (pp - pl) * (saturate(T.y) * seam * fade * (T.x > 0.5 ? 1.0 : 0.0));
  float dl = length(d), tope = lerp(max(U.z, 0.0), clamp(EB.y, 0.0, max(U.z, 0.0)), saturate(eyeMix * 2.0)) * 0.001;
  if (dl > tope) d *= tope / max(dl, 1e-6);
  // (04/11 c) relieve desde la quinta textura (fila 359): por la normal interpolada, mm
  float4 R = IniParams.Load(int2(359, 0));   // x fuerza, y hundir surcos (mm), z levantar relieve (mm), w mip
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
      float relL = IniParams.Load(int2(360, 0)).w;
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
#endif   // !PES_TESS_BODY (relieve solo en la cara)
  // (04/11 d) PRUEBA (fila 360.x): infla la piel 1 cm por la normal, sin tope ni distancia, para ver sin duda que la teselacion
  // llega a la imagen (los bordes de cada pieza quedan fijos). Solo para probar: volver a "No".
  if (IniParams.Load(int2(360, 0)).x > 0.5) d += normalize(b.x * N0 + b.y * N1 + b.z * N2) * (0.01 * min(seam, seamR));
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
#if !PES_TESS_BODY
  o.pD = PES_LERP3(pD); o.pD.w = p[0].pD.w;
#endif
  o.pV = float4(pv, seam);
  return o;
}
#endif   // PES_TESS_DS
