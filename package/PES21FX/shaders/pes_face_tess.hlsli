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
  float4 pD : TEXCOORD11;
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
  float4 pD : TEXCOORD11;
  float4 pV : TEXCOORD12;
};

struct PesTessConst
{
  float e[3] : SV_TessFactor;
  float i : SV_InsideTessFactor;
};

#if PES_TESS_HS
// ---------------------------------------------------------------- hull shader
cbuffer PESTessView : register(b13)
{
  float4 g_pesTessVp;   // x ancho, y alto del viewport en pixeles (lo pone la DLL)
}

PesTessConst PesTessPatch(InputPatch<PesTessVS, 3> p)
{
  PesTessConst k;
  float4 T = IniParams.Load(int2(357, 0));   // x on, y suavidad, z pixeles por segmento, w factor maximo
  float4 U = IniParams.Load(int2(358, 0));   // x distancia maxima (m), y transicion (m)
  float f[3] = { 1.0, 1.0, 1.0 };
  bool ok = T.x > 0.5 && p[0].o0.w > 0.01 && p[1].o0.w > 0.01 && p[2].o0.w > 0.01;
  float dmin = min(length(p[0].pV.xyz), min(length(p[1].pV.xyz), length(p[2].pV.xyz)));
  // mas lejos que la distancia maxima: nada que dividir (el DS tampoco corre nada)
  if (dmin > U.x) ok = false;
  // los tres con peso 0 (triangulo entero en una costura): se queda como esta
  if (p[0].pV.w + p[1].pV.w + p[2].pV.w <= 0.0) ok = false;
  if (ok) {
    float2 s0 = p[0].o0.xy / p[0].o0.w * 0.5 * g_pesTessVp.xy;
    float2 s1 = p[1].o0.xy / p[1].o0.w * 0.5 * g_pesTessVp.xy;
    float2 s2 = p[2].o0.xy / p[2].o0.w * 0.5 * g_pesTessVp.xy;
    float px = max(T.z, 2.0), fmax = clamp(T.w, 1.0, 16.0);
    // arista k = la opuesta al vertice k (convencion de D3D: e[0] entre los puntos 1 y 2)
    f[0] = clamp(length(s1 - s2) / px, 1.0, fmax);
    f[1] = clamp(length(s2 - s0) / px, 1.0, fmax);
    f[2] = clamp(length(s0 - s1) / px, 1.0, fmax);
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
  o.pD = p[i].pD;
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

// (04/11 c) quinta textura de la cara (la misma del PS de la piel, la DLL la copia al DS)
Texture2D<float4> pesFaceA : register(t23);
Texture2D<float4> pesFaceB : register(t24);
SamplerState pesTessSamp : register(s0);

[domain("tri")]
PesTessVS main(PesTessConst k, float3 b : SV_DomainLocation, const OutputPatch<PesTessCP, 3> p)
{
  PesTessVS o;
  float4 T = IniParams.Load(int2(357, 0));
  float4 U = IniParams.Load(int2(358, 0));
  // posicion lineal (la del triangulo plano) y la de Phong (proyectada sobre los tres planos tangentes)
  float3 P0 = p[0].pV.xyz, P1 = p[1].pV.xyz, P2 = p[2].pV.xyz;
  float3 N0 = normalize(p[0].o6.xyz), N1 = normalize(p[1].o6.xyz), N2 = normalize(p[2].o6.xyz);
  float3 pl = b.x * P0 + b.y * P1 + b.z * P2;
  float3 pp = b.x * (pl - dot(pl - P0, N0) * N0)
            + b.y * (pl - dot(pl - P1, N1) * N1)
            + b.z * (pl - dot(pl - P2, N2) * N2);
  float seam = saturate(b.x * p[0].pV.w + b.y * p[1].pV.w + b.z * p[2].pV.w);   // 0 en las costuras / bordes de la pieza
  float dist = length(pl);
  float fade = 1.0 - smoothstep(U.x - max(U.y, 0.01), U.x, dist);
  float3 d = (pp - pl) * (saturate(T.y) * seam * fade * (T.x > 0.5 ? 1.0 : 0.0));
  float dl = length(d), tope = max(U.z, 0.0) * 0.001;
  if (dl > tope) d *= tope / max(dl, 1e-6);
  // (04/11 c) relieve desde la quinta textura (fila 359): por la normal interpolada, mm
  float4 R = IniParams.Load(int2(359, 0));   // x fuerza, y hundir surcos (mm), z levantar relieve (mm), w mip
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
      float3 Nn = normalize(b.x * N0 + b.y * N1 + b.z * N2);
      d += Nn * (clamp(hmm * R.x, -3.0, 3.0) * 0.001 * seam * fade);
    }
  }
  float3 pv = pl + d;

  float4 clip = mul(float4(pv, 1.0), g_vsScene.m_projection);
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
  o.o7 = PES_LERP3(o7); o.o7.xy = clip.xy;
#if PES_TESS_V455
  o.o8 = PES_LERP3(o8); o.o8.x = clip.w;
#endif
  o.pR = PES_LERP3(pR); o.pR.w = p[0].pR.w;
  o.pD = PES_LERP3(pD); o.pD.w = p[0].pD.w;
  o.pV = float4(pv, seam);
  return o;
}
#endif   // PES_TESS_DS
