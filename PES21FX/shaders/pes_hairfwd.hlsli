// PES21 FX (08/11 v) - PASADA PROPIA DEL PELO (cejas con hebras), pixel shader. Agustin: "piensa en como se plasman los pelos de hebras
// en juegos modernos" + "todos los puntos". Como UE5 (Groom) / Frostbite, el pelo tiene su propio sombreado fuera del G-buffer:
//  1) se dibuja DESPUES de la luz diferida, sumado sobre la imagen HDR con la COBERTURA real de cada hebra (mezcla aditiva: no
//     depende del orden, sin descarte por pixel): las hebras finas quedan suaves y sin escalones, el brillo no centellea.
//  2) luz de PELO con la TANGENTE de la hebra (no una normal): los tres lobulos de Marschner (R = brillo blanco corrido hacia la raiz,
//     TT = luz que atraviesa el pelo a contraluz con su color, TRT = segundo brillo de color), con la aproximacion de Karis (2016).
//  3) sombra en capas: la del estadio (mascara de sombra del juego, t14), la propia de la cabeza y entre hebras (atlas t30, que tiene
//     las hebras dibujadas) y la transmision por el GROSOR REAL de pelo que cruza la luz (Beer-Lambert por fibras).
// El G-buffer de las hebras (pes_brow_ps) queda con la luz difusa (cielo, rebote, sol difuso) y ya no se marca como pelo ni lleva el
// brillo: lo hace esta pasada. w382 = fuerza (0 = apagada, todo como antes).
cbuffer cPSScene : register(b2)
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
  } g_psScene : packoffset(c0);
}
cbuffer PESHairFwd : register(b5)
{
  float4 hfLight;   // rgb = color de la luz principal del cuadro (LightCB), w = fuerza (w382)
  float4 hfInfo;    // x = 1 profundidad invertida, yz = tamano del render, w = 1 hay luz del juego
  float4 hfMix;     // (08/11 z3) x = modo mixto (x425), y = borde en px (y425), z = opacidad del borde (z425)
}
Texture1D<float4> IniParams : register(t120);
Texture2D<float4> tDepth : register(t11);    // profundidad de la escena (la de la composicion)
Texture2D<float4> tShadow : register(t14);   // mascara de sombra del sol del juego (1 = al sol)
SamplerState sPt : register(s15);
#ifdef PES_HF_COV
// (08/11 z, Agustin: "se sigue viendo feo, no noto diferencias") COBERTURA REAL: las hebras v3 ya no van al G-buffer (opacas, minimo
// ~1 px: el grosor y las opciones quedaban tapadas por ese minimo). Se dibujan solo aca, con mezcla alfa = ancho real / dibujado:
// un pelo de 0.1 mm se ve fino y semitransparente. Luz difusa = la de la piel de abajo (t9 de la composicion, la luz difusa ya
// calculada en ese pixel) x el color del pelo; brillo = Marschner de abajo
Texture2D<float4> tLightD : register(t9);
#endif
#include "pes_headshadow.hlsli"
#include "pes_skinthick.hlsli"
#include "pes_brow_light.hlsli"

struct VSOut {
  float4 pos : SV_Position;
  float3 nrmV : TEXCOORD0;
  float3 col : TEXCOORD1;
  float3 tw : TEXCOORD2;
  float2 motion : TEXCOORD3;
  float3 viewV : TEXCOORD4;
  float3 col2 : TEXCOORD5;
  float3 tanV : TEXCOORD6;
  float3 posV : TEXCOORD7;
#ifdef PES_HF_COV
  float2 fr : TEXCOORD8;     // (08/11 z3) modo mixto: x = medio ancho del nucleo en px, y = borde agregado en px (pes_hairfwd_vs)
#endif
};

static const float PI = 3.14159265;
float HairG(float B, float th) { return exp(-0.5 * th * th / (B * B)) / (sqrt(2.0 * PI) * B); }
float HairF(float c) { const float F0 = 0.04652; return F0 + (1.0 - F0) * pow(1.0 - saturate(c), 5.0); }   // n = 1.55
// Marschner con la aproximacion de Karis (Physically Based Hair Shading in Unreal, 2016). T = tangente (raiz -> punta), L = hacia la
// luz, V = hacia la camara. visR = sombra para R / TRT (luz de frente), visTT = transmision para TT (luz de atras)
float3 HairBSDF(float3 alb, float rough, float3 L, float3 V, float3 T, float visR, float visTT, float shiftJ)
{
  float4 HB = IniParams.Load(int2(406, 0));   // (08/11 x) x406 brillo primario (R), y406 brillo secundario (TRT)
  float shift = 0.035 + shiftJ;
  float a0 = -2.0 * shift, a1 = shift, a2 = 4.0 * shift;
  float r2 = rough * rough;
  float B0 = r2, B1 = r2 * 0.5, B2 = r2 * 2.0;
  float VoL = dot(V, L);
  float sL = dot(T, L), sV = dot(T, V);
  float thL = asin(clamp(sL, -1.0, 1.0)), thV = asin(clamp(sV, -1.0, 1.0));
  float cD = cos(0.5 * abs(thV - thL));
  float3 Lp = L - sL * T, Vp = V - sV * T;
  float cPhi = dot(Lp, Vp) * rsqrt(dot(Lp, Lp) * dot(Vp, Vp) + 1e-4);
  float cHalf = sqrt(saturate(0.5 + 0.5 * cPhi));
  float3 S = 0;
  // R: reflexion en la cuticula (blanca), corrida hacia la raiz
  { float Mp = HairG(B0 * sqrt(2.0) * cHalf, sL + sV - a0);
    float Np = 0.25 * cHalf;
    float Fp = HairF(sqrt(saturate(0.5 + 0.5 * VoL)));
    S += Mp * Np * Fp * visR * clamp(HB.x, 0.0, 3.0); }
  // TT: atraviesa la fibra (contraluz con el color del pelo)
  { float Mp = HairG(B1, sL + sV - a1);
    float np = 1.19 / cD + 0.36 * cD, a = 1.0 / np;
    float h = cHalf * (1.0 + a * (0.6 - 0.8 * cPhi));
    float f = HairF(cD * sqrt(saturate(1.0 - h * h)));
    float Fp = (1.0 - f) * (1.0 - f);
    float3 Tp = pow(max(alb, 1e-3), 0.5 * sqrt(saturate(1.0 - h * h * a * a)) / cD);
    float Np = exp(-3.65 * cPhi - 3.98);
    S += Mp * Np * Fp * Tp * visTT; }
  // TRT: entra, rebota adentro y sale (segundo brillo con el color del pelo)
  { float Mp = HairG(B2, sL + sV - a2);
    float f = HairF(cD * 0.5);
    float Fp = (1.0 - f) * (1.0 - f) * f;
    float3 Tp = pow(max(alb, 1e-3), 0.8 / cD);
    float Np = exp(17.0 * cPhi - 16.78);
    S += Mp * Np * Fp * Tp * visR * clamp(HB.y, 0.0, 3.0); }
  return S;
}

float LinZ(float d) { return g_psScene.m_projectionParam.z / (d - g_psScene.m_projectionParam.w); }

float4 main(VSOut i) : SV_Target0
{
  float gain = hfLight.w;
  if (gain <= 0.001 || IniParams.Load(int2(75, 0)).z > 0.5) discard;
  int2 px = int2(i.pos.xy);
  // prueba de profundidad a mano contra la escena (las hebras del frente ya escribieron su profundidad en el G-buffer)
  float zS = abs(LinZ(tDepth.Load(int3(px, 0)).x)), zH = abs(LinZ(i.pos.z));
  if (zH > zS + 0.0012) discard;
  float t = i.tw.x;
  float sK = floor(i.tw.z * 0.5), res = saturate(i.tw.z - 2.0 * sK);
  float sL = floor(sK / 8.0), sJ = (sK - 8.0 * sL) / 7.0, layQ = sL / 3.0;
  float3 c0 = g_psScene.m_view._m00_m10_m20, c1 = g_psScene.m_view._m01_m11_m21, c2 = g_psScene.m_view._m02_m12_m22;
  float3 L = PesBrowLightDir(c0, c1, c2, float3(0, 0, 0), i.posV.z);
#ifdef PES_HF_COV
  bool hasL = dot(L, L) >= 0.25; if (!hasL) L = normalize(-i.viewV);
#else
  if (dot(L, L) < 0.25) discard;
#endif
  float3 V = normalize(i.viewV), T = normalize(i.tanV), N = normalize(i.nrmV);
  // cobertura real: ancho real / dibujado (tw.z = resolucion: 1 = 1.5 px); borde redondeado y punta afinada
  float edge = 1.0 - abs(i.tw.y);
  float cov = saturate(max(res * 1.667, 0.3)) * saturate(edge * 3.0) * (1.0 - t * t * t * t);
  float3 alb = saturate(lerp(i.col, i.col2, t));
  alb = pow(alb + 1e-4, 0.85);
  // --- sombra en capas ---
  float nl = dot(N, L);
  // estadio: la mascara del juego en el pixel (luz de frente) y alrededor de la cara (si todo alrededor esta a la sombra, el
  // estadio tapa el sol y no hay contraluz: "en un estadio en sombra el pelo no se ilumina")
  float2 sd; tShadow.GetDimensions(sd.x, sd.y);
  float2 sc = sd / max(hfInfo.yz, 1.0);
  float shP = saturate(tShadow.Load(int3(i.pos.xy * sc, 0)).x);
  float ppm = abs(g_psScene.m_projection._m11) * hfInfo.z * 0.5 / max(zH, 0.05);
  float rad = clamp(0.06 * ppm, 4.0, 200.0), shA = 0.0;
  [unroll] for (int k = 0; k < 8; k++) {
    float a = k * 0.785398;
    float2 q = clamp(i.pos.xy + float2(cos(a), sin(a)) * rad, 0.0, hfInfo.yz - 1.0);
    shA = max(shA, saturate(tShadow.Load(int3(q * sc, 0)).x));
  }
  float hIn = 0.0, thk = PESSkinThickAtlas(i.posV, sPt, hIn);
  float visA = PESHeadShadowAt(i.posV, nl, sPt);   // la cabeza y las otras hebras desde el sol
  float visR = shP * visA;
  float TA = hIn > 0.0 ? exp(-max(thk - 0.002, 0.0) * 300.0) * (1.0 - smoothstep(0.03, 0.06, thk)) : 0.35;
  float visTT = TA * shA;
  // capas de la ceja: las de abajo reciben menos luz (la atraviesan las de arriba)
  float dep = (1.0 - layQ) * (1.0 - 0.6 * t);
  float layT = exp(-0.4 * dep / max(abs(nl), 0.22));
  visR *= lerp(1.0, layT, 0.5);
  float rough = lerp(0.3, 0.42, sJ) + 0.12 * (1.0 - res);
  float3 S = HairBSDF(alb, rough, L, V, T, visR, visTT, (sJ - 0.5) * 0.02);
  float3 lc = hfLight.rgb * g_psScene.m_exposure.z;
#ifdef PES_HF_COV
  float2 uvG = i.pos.xy / max(hfInfo.yz, 1.0);
  float3 irr = tLightD.SampleLevel(sPt, uvG, 0).xyz;
  // (08/11 z3) MODO MIXTO: la hebra ya esta en el G-buffer (pegada, con la luz del juego); aca va su BORDE semitransparente afuera del
  // nucleo y el brillo de pelo encima. Salida premultiplicada (la DLL mezcla ONE / INV_SRC_ALPHA): el brillo suma entero en el nucleo
  float tipF = 1.0 - t * t * t * t;
  float tot = max(i.fr.x + i.fr.y, 1e-3), dpx = abs(i.tw.y) * tot;
  float outF = i.fr.y > 1e-3 ? saturate((dpx - i.fr.x) / i.fr.y) : 1.0;          // 0 en el nucleo .. 1 en el borde de afuera
  float soft = lerp(1.5, 0.6, saturate(IniParams.Load(int2(406, 0)).z / 3.0));    // z406 borde redondeado: mas = borde mas suave
  float fringe = pow(saturate(1.0 - outF), soft);
  float thick = saturate(0.45 + res);                                              // grosor elegido (x403 / x404 / y404): borde mas lleno
  float aD = (dpx <= i.fr.x ? 0.3 : clamp(hfMix.z, 0.0, 1.0) * fringe * thick) * tipF * saturate(gain);
  float3 dif = alb * irr * lerp(1.0, layT, 0.5) * lerp(0.8, 1.0, layQ);
  float3 spc = hasL ? S * lc * saturate(dot(N, L) * 0.5 + 0.75) * gain * saturate(max(res * 1.667, 0.3)) * tipF * saturate(1.0 - outF) : 0.0;   // = brillo de antes en el nucleo
  return float4(max(dif * aD + spc, 0.0), aD);
#else
  float3 o = S * lc * gain * cov * saturate(dot(N, L) * 0.5 + 0.75);
  return float4(max(o, 0.0), 0.0);
#endif
}
