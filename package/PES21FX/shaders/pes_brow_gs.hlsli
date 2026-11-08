// PES21 FX - cejas con hebras v2 (04/11): geometry shader. Las hebras salen de la MALLA de la cara, no de semillas que el
// shader de cara escribia cada cuadro (v1, 18/10: nacian solo donde la cara tenia pixeles -> aparecian y desaparecian con la
// distancia, y en el editor salian cintas gigantes). Ahora la DLL vuelve a dibujar el mismo dibujo de la cara (mismo VS del
// mod, mismos huesos, gestos y expresiones) con este GS: cada triangulo que cae en la zona de ceja del MAPA DE PELO (quinta
// textura, t26: R densidad, G largo, B direccion, A = 1 zona de ceja) suelta hebras en puntos FIJOS del triangulo
// (baricentricas por hash del numero de triangulo): las hebras quedan pegadas a la piel, siguen la frente y los gestos y no
// bailan entre cuadros. La direccion del mapa (en el plano UV) se lleva a 3D con el jacobiano UV -> posicion del triangulo.
// x74 on | y74 densidad (1..3) | z74 largo (cm) | w74 grosor (mm) | x75 elevacion | y75 distancia max (m) | z75 diagnostico
// w75 = -1 invierte la direccion | y77 = 1 tambien fuera del partido (editor, presentaciones)
// El archivo se incluye desde pes_brow_a_gs.hlsl (VS de la cabeza faab87ee) y pes_brow_b_gs.hlsl (VS de la cara animada
// 455eb97b): la entrada del GS tiene que ser IGUAL a la salida de cada VS (mismo orden de semanticas).
#define PES_SEG 5           // (08/11, Agustin: "mejora la calidad") 4 -> 5 segmentos: curva y punta afinada mas suaves de cerca
#define PES_PER_INST 3      // hebras por instancia del GS: 3 x 12 vertices x 24 escalares = 864 (tope 1024)
#define PES_INST 32         // instancias del GS por triangulo -> 96 hebras por triangulo, igual que antes (24 x 4)
cbuffer cVSScene : register(b2)
{
  float4 m_projectionView[4];
  float4 m_projection[4];
  float4 m_view[4];
  float4 m_shadowProjection[4];
  float4 m_shadowProjection2[4];
  float4 m_eyepos;
  float4 m_projectionParam;
  float4 m_viewportSize;
  float4 m_exposure;
  float4 m_fogParam[3];
  float4 m_fogColor;
  float4 m_cameraCenterOffset;
  float4 m_shadowMapResolutions;
}
cbuffer PESHair : register(b11)
{
  float4 pesHairA;      // largo_cm, rizo, rigidez, peso
  float4 pesHairB;      // grosor_mm, densidad, brillo, flequillo
  float4 pesHairRoot;   // color raiz rgb, w = 1 si esta definido
  float4 pesHairTip;    // color punta rgb, w = 1 si esta definido
  float4 pesHairBrow;   // ceja: grosor, densidad, elevacion, largo (multiplicadores)
  float4 pesHairMisc;   // x = 1 enlazado, y = 1 mapa de pelo propio del usuario
  float4 pesBrowCol;    // (04/11 i) color de ceja del jugador (fila 367 / [cejas_cara]): rgb, w = mezcla con el color pintado
  float4 pesBrowBack;   // (04/11 l) color a contraluz (fila 369; lo usa el PS)
  float4 pesBrowX;      // (04/11 l) x = hebras extra del jugador (fila 370)
}
Texture1D<float4> IniParams : register(t120);
#include "pes_face_tess_player.hlsli"   // (04/11 m) teselacion por jugador: la DLL copia el b10 del DS de la cara al GS
#ifdef PES_SHADOW
// (04/11 k) modo SOMBRA DE CONTACTO (pes_browsh_*_gs): la luz principal del dibujo (b0 del PS, la DLL la pasa al GS)
cbuffer cPSSystem : register(b0)
{
  struct { float4 m_param; float4 m_renderInfo; float4 m_renderBuffer; float4 m_dominantLightDir; float4 m_timeParam; } g_psSystem;
}
// direccion HACIA la luz en la vista (como PesBrowAniso del PS: fila 50.z = 0 -> la luz viene en el mundo)
#include "pes_brow_light.hlsli"
float3 PesShadowL(float vz) {   // (04/11 o) la luz real (pes_brow_light.hlsli)
  return PesBrowLightDir(m_view[0].xyz, m_view[1].xyz, m_view[2].xyz, g_psSystem.m_dominantLightDir.xyz, vz);
}
#endif
Texture2D<float4> tFace : register(t0);      // difusa de la cara (la misma del dibujo)
Texture2D<float4> tHairMap : register(t26);  // mapa de pelo de esta cara
Texture2D<float4> tBrow : register(t27);     // (04/11 d) mapa de la ceja: rgb color de sus pelos, a contorno
SamplerState sLin : register(s0);
// (04/11 e, Agustin: "las cejas no salen desde el modelo del rostro o se ven separadas") con la teselacion de la cara la piel
// que se VE es la del DS (pes_face_tess.hlsli: redondeo de Phong + relieve de la quinta textura), no la del triangulo plano.
// Antes la raiz subia un valor FIJO (z358 * 0.6 + z359 mm ~ 1.4 mm) por la normal: donde la piel teselada casi no sube (la
// frente, la ceja) las hebras quedaban flotando ~2 mm. Ahora el GS repite el MISMO corrimiento del DS en el punto de la raiz.
// La DLL pone t121 = parametros SOLO si ese dibujo de la cara esta teselado (si no, queda vacio y Load da 0), y t23 / t24 =
// las texturas del relieve del DS.
Texture1D<float4> TessOn : register(t121);
Texture2D<float4> pesFaceA : register(t23);
Texture2D<float4> pesFaceB : register(t24);

#ifdef PES_BROW_VS_B
struct FaceV {   // salida del VS 455eb97b (cara animada)
  float4 pos : SV_Position0;
  float4 c1 : COLOR0;
  float4 uv4 : TEXCOORD0;
  float4 o3 : TEXCOORD4;
  float4 o4 : TEXCOORD5;
  float4 o5 : TEXCOORD6;
  float4 nrm : TEXCOORD7;
  float4 clipCP : TEXCOORD8;    // xy = clip actual, zw = clip del cuadro anterior
  float2 ww : TEXCOORD9;        // x = w actual, y = w anterior
  float4 pR : TEXCOORD10;
  float4 pD : TEXCOORD11;
  float4 pV : TEXCOORD12;       // posicion en la vista
};
float2 FaceUV(FaceV v) { return v.uv4.xy; }
#else
struct FaceV {   // salida del VS faab87ee (cabeza)
  float4 pos : SV_Position0;
  float4 c1 : COLOR0;
  float2 uv : TEXCOORD0;
  float2 ww : TEXCOORD9;        // x = w actual, y = w anterior
  float4 o3 : TEXCOORD4;
  float4 o4 : TEXCOORD5;
  float4 o5 : TEXCOORD6;
  float4 nrm : TEXCOORD7;
  float4 clipCP : TEXCOORD8;    // xy = clip actual, zw = clip del cuadro anterior
  float4 pR : TEXCOORD10;
  float4 pD : TEXCOORD11;
  float4 pV : TEXCOORD12;       // posicion en la vista
};
float2 FaceUV(FaceV v) { return v.uv; }
#endif

struct GSOut {   // igual que la entrada de pes_brow_ps.hlsl
  float4 pos : SV_Position;
  float3 nrmV : TEXCOORD0;
  float3 col : TEXCOORD1;
  float3 tw : TEXCOORD2;       // x = 0 raiz .. 1 punta, y = lado, z = (06/11) cobertura real del pixel (hebra mas fina que 1 px)
  float2 motion : TEXCOORD3;
  float3 viewV : TEXCOORD4;
  float3 col2 : TEXCOORD5;
  float3 tanV : TEXCOORD6;     // (04/11 e) direccion de la hebra en la vista: brillo en banda (Kajiya-Kay) como el pelo de la cabeza
  float3 posV : TEXCOORD7;     // (08/11 v) posicion en la vista: la pasada propia del pelo (pes_hairfwd, salida de flujo) la usa con el atlas
};

uint PesHash(uint v) { uint s = v * 747796405u + 2891336453u; uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u; return (w >> 22u) ^ w; }
float PesRnd(inout uint s) { s = PesHash(s); return (float)(s & 0xffffff) / 16777216.0; }

// (04/11 b) pelo PINTADO en ese punto de la textura de la cara: oscuro respecto de la piel de alrededor (mip 1 contra mip 5).
// Agustin: "la forma de las cejas esta mal" -> las hebras solo nacen (y solo llegan) donde la ceja esta pintada
float PesLum(float2 uv, float mip) { return dot(tFace.SampleLevel(sLin, uv, mip).rgb, float3(0.2126, 0.7152, 0.0722)); }
// (04/11 d) contorno de la ceja: el mapa de la ceja que la DLL arma UNA vez por cara a resolucion completa (t27, pes_facegen_ps)
float PesPaintedHair(float2 uv) { return tBrow.SampleLevel(sLin, uv, 0).a; }
// (04/11 b) direccion anatomica de la ceja en el plano UV (Agustin: "la orientacion hacia donde van los pelos tambien"):
// la cabeza (junto a la nariz) crece hacia ARRIBA, el cuerpo hacia afuera y un poco arriba, la cola hacia afuera y abajo.
// En UV: afuera = alejarse de u = 0.5, arriba = -v.
float2 PesBrowDirUV(float2 uv) {
  float side = uv.x < 0.5 ? -1.0 : 1.0;
  float a = saturate((abs(uv.x - 0.5) - 0.03) / 0.17);   // 0 cabeza .. 1 cola
  float2 head = normalize(float2(side * 0.3, -1.0));
  float2 body = normalize(float2(side, -0.32));
  float2 tail = normalize(float2(side, 0.28));
  float2 d = lerp(head, body, smoothstep(0.08, 0.35, a));
  d = lerp(d, tail, smoothstep(0.62, 0.95, a));
  return normalize(d);
}

// (08/11 w, Agustin: "ya que tienes hebras para las cejas puedes inyectarla a la barba tambien?") BARBA CON HEBRAS desde la malla de la
// cara donde la barba esta pintada (fila 402: x on, y densidad, z largo mm, w grosor). Plano UV comun de las caras: labios en
// (0.5, 0.595); la barba cubre mejillas bajas, bigote, menton y mandibula. Los labios quedan SIEMPRE fuera (con margen): la boca
// no se toca
float PesEllB(float2 uv, float2 c, float2 r) { return saturate((1.0 - length((uv - c) / r)) * 3.0); }
// (08/11 y, Agustin: "la barba no detecta bien de donde sale") antes se comparaba cada punto con la piel a 0.03 de UV: dentro de una
// barba tupida los vecinos tambien son barba y el centro no salia (solo el borde), y las sombras pintadas (fosas nasales, pliegue
// del menton) si salian. Ahora la referencia es la PIEL LIMPIA de la misma cara (frente, pomulos y dorso de la nariz, la mas clara)
// y la barba es lo bastante mas oscuro que eso; fosas nasales y labios siempre fuera
float PesBeardSkinRef() {
  return max(max(PesLum(float2(0.5, 0.20), 4.0), PesLum(float2(0.5, 0.44), 4.0)),
             max(PesLum(float2(0.33, 0.46), 4.0), PesLum(float2(0.67, 0.46), 4.0)));
}
float PesBeardAmt(float2 uv, float skRef) {
  float zB = PesEllB(uv, float2(0.5, 0.70), float2(0.31, 0.23));
  if (length((uv - float2(0.5, 0.595)) / float2(0.105, 0.05)) < 1.0) return 0.0;   // labios + margen: nunca
  if (length((uv - float2(0.5, 0.515)) / float2(0.075, 0.03)) < 1.0) return 0.0;   // fosas nasales
  float d = 1.0 - PesLum(uv, 1.0) / max(skRef, 0.01);
  return saturate((d - 0.22) * 3.0) * zB;
}
float2 PesBeardDirUV(float2 uv) {
  float sx = uv.x < 0.5 ? -1.0 : 1.0, ax = abs(uv.x - 0.5);
  float2 d = float2(sx * (0.15 + 0.6 * saturate(ax * 3.0)), 1.0);                   // hacia abajo y afuera en las mejillas
  if (uv.y < 0.62 && ax < 0.13) d = float2(sx * 0.8, 1.0);                           // bigote: hacia las comisuras
  return normalize(d);
}

// copia del corrimiento de pes_face_tess.hlsli (main del DS) en las baricentricas b del triangulo; uvT = UV que usa el DS
float3 PesTessDisp(FaceV v[3], float3 b, float2 uvT)
{
  if (TessOn.Load(int2(357, 0)).x < 0.5) return float3(0, 0, 0);
  float4 T = IniParams.Load(int2(357, 0)), U = IniParams.Load(int2(358, 0));
  PesTessApplyPlayer(T, U);   // (04/11 m) los mismos multiplicadores del jugador que el DS
  float3 P0 = v[0].pV.xyz, P1 = v[1].pV.xyz, P2 = v[2].pV.xyz;
  float3 N0 = normalize(v[0].nrm.xyz), N1 = normalize(v[1].nrm.xyz), N2 = normalize(v[2].nrm.xyz);
  float3 pl = b.x * P0 + b.y * P1 + b.z * P2;
  float3 pp = PesTessSmooth(P0, P1, P2, N0, N1, N2, b, IniParams.Load(int2(376, 0)).y);   // (07/11) igual que el DS de la cara (Phong o PN)
  float4 EB = IniParams.Load(int2(366, 0));
  float3 wv = float3(v[0].pV.w, v[1].pV.w, v[2].pV.w);
  float3 isEye = step(1.5, wv);
  float3 wPh = lerp(saturate(wv), saturate(EB.xxx), isEye);
  float eyeMix = dot(b, isEye);
  float seam = saturate(dot(b, wPh));
  // (07/11) peso 1.25 = costura de textura suave (las dos copias con la misma normal y huesos, expr.cpp SeamWeights): se redondea
  // con Phong pero sin nada que lea la textura (relieve, arrugas), porque cada lado de la costura tiene otras uv
  float3 isSoft = step(1.1, wv) - isEye;
  wPh *= 1.0 - isSoft * (1.0 - saturate(IniParams.Load(int2(376, 0)).x));   // x376 = 0: las costuras de textura vuelven a quedar fijas
  seam = saturate(dot(b, wPh));
  float seamR = saturate(dot(b, saturate(wv) * (1.0 - isEye - isSoft)));
  { float nh = clamp(IniParams.Load(int2(360, 0)).z, 0.0, 15.0) * 0.01;
    float ry = b.x * v[0].pR.y + b.y * v[1].pR.y + b.z * v[2].pR.y;
    seam *= smoothstep(1.53, 1.53 + max(nh, 0.01), ry); }
  float fade = 1.0 - smoothstep(U.x - max(U.y, 0.01), U.x, length(pl));
  float3 d = (pp - pl) * (saturate(T.y) * seam * fade * (T.x > 0.5 ? 1.0 : 0.0));
  float dl = length(d), tope = lerp(max(U.z, 0.0), clamp(EB.y, 0.0, max(U.z, 0.0)), saturate(eyeMix * 2.0)) * 0.001;
  if (dl > tope) d *= tope / max(dl, 1e-6);
  float4 R = IniParams.Load(int2(359, 0));
  PesTessApplyRelief(R);
  if (R.x > 0.001 && T.x > 0.5) {
    float lod = clamp(R.w, 0.0, 6.0);
    float4 FB = pesFaceB.SampleLevel(sLin, uvT, lod);
    if (FB.a > 0.5) {
      float4 FA = pesFaceA.SampleLevel(sLin, uvT, lod);
      float hmm = max(R.z, 0.0) * (FB.b + saturate(FA.g * 2.0 - 1.0)) - max(R.y, 0.0) * FA.r;   // (la ceja esta lejos de la boca)
      float3 Nn = normalize(b.x * N0 + b.y * N1 + b.z * N2);
      d += Nn * (clamp(hmm * R.x, -3.0, 3.0) * 0.001 * min(seam, seamR) * fade);
    }
  }
  if (IniParams.Load(int2(360, 0)).x > 0.5) d += normalize(b.x * N0 + b.y * N1 + b.z * N2) * (0.01 * min(seam, seamR));
  return d;
}

float4 ProjV(float3 p) {
  float4 h = float4(p, 1);
  return float4(dot(h, m_projection[0]), dot(h, m_projection[1]), dot(h, m_projection[2]), dot(h, m_projection[3]));
}
#ifdef PES_ATLAS
// (07/11 c, Agustin: "la luz no la afecta de manera correcta, no tiene el volumen exacto") modo ATLAS (pes_browat_*_gs): las hebras se
// dibujan tambien en el atlas de sombra de las cabezas (headshadow.cpp, t30: la sombra propia de la cara y de las hebras en la pasada
// del sol). Cada pelo tapa a los de abajo y a la piel desde el sol: sombra REAL entre hebras (antes solo la sombra de contacto plana).
// Misma cuenta que el DS del atlas (pes_face_tess.hlsli PesAtlasClip): posicion de la vista -> mundo (VV = P^-1 PV) -> caja (b12)
cbuffer PESAtlasBox : register(b12) { float4x4 g_pesAtlasPV; }
void PesAtlasBasis(out float4 cx, out float4 cy, out float4 cw) {
  float4 PV0 = m_projectionView[0], PV1 = m_projectionView[1], PV2 = m_projectionView[2], PV3 = m_projectionView[3];
  float4 P0 = m_projection[0], P2 = m_projection[2], P3 = m_projection[3], P1 = m_projection[1];
  cw = float4(PV0.w, PV1.w, PV2.w, PV3.w);
  cx = (float4(PV0.x, PV1.x, PV2.x, PV3.x) - P2.x * cw - float4(0, 0, 0, P3.x)) / (abs(P0.x) > 1e-6 ? P0.x : 1e-6);
  cy = (float4(PV0.y, PV1.y, PV2.y, PV3.y) - P2.y * cw - float4(0, 0, 0, P3.y)) / (abs(P1.y) > 1e-6 ? P1.y : 1e-6);
}
float4 PesAtlasClip(float3 pv) {
  float4 cx, cy, cw; PesAtlasBasis(cx, cy, cw);
  float3x3 R = float3x3(cx.xyz, cy.xyz, cw.xyz);
  float3 q = pv - float3(cx.w, cy.w, cw.w);
  float3 c0 = cross(R[1], R[2]), c1 = cross(R[2], R[0]), c2 = cross(R[0], R[1]);
  float det = dot(R[0], c0); det = abs(det) > 1e-8 ? det : 1e-8;
  float3 w = (c0 * q.x + c1 * q.y + c2 * q.z) / det;
  float4 c = mul(float4(w, 1.0), g_pesAtlasPV);
  c.z += 9.99999975e-005 * c.w;
  return c;
}
// direccion del sol (eje de profundidad de la caja, columna 2 en memoria) llevada a la vista: la cinta de cada pelo mira al sol
float3 PesAtlasLightV() {
  float4 cx, cy, cw; PesAtlasBasis(cx, cy, cw);
  float3 Lw = normalize(float3(g_pesAtlasPV[0][2], g_pesAtlasPV[1][2], g_pesAtlasPV[2][2]) + 1e-6);
  return normalize(float3(dot(cx.xyz, Lw), dot(cy.xyz, Lw), dot(cw.xyz, Lw)));
}
float PesAtlasTexelM() {   // tamano de un texel del atlas en metros (columna 0 = R / mitad de la caja; cuadrante de 1024 px)
  float invHalf = max(length(float3(g_pesAtlasPV[0][0], g_pesAtlasPV[1][0], g_pesAtlasPV[2][0])), 1e-4);
  return 2.0 / (invHalf * 1024.0);
}
#endif

[instance(PES_INST)]
[maxvertexcount(PES_PER_INST * (PES_SEG + 1) * 2)]
void main(triangle FaceV v[3], uint pid : SV_PrimitiveID, uint inst : SV_GSInstanceID, inout TriangleStream<GSOut> outS)
{
  float4 P74 = IniParams.Load(int2(74,0));
  float4 P75 = IniParams.Load(int2(75,0));
  if (P74.x < 0.5) return;
  float2 uv0 = FaceUV(v[0]), uv1 = FaceUV(v[1]), uv2 = FaceUV(v[2]);
  // 1) descarte rapido: solo triangulos que tocan la franja de las cejas del plano UV comun de las caras
  //    (arcos de pes_facegen_ps.hlsl: centros 0.395 / 0.605, y 0.312, radios 0.105 x 0.030, con margen)
  float2 lo = min(uv0, min(uv1, uv2)), hi = max(uv0, max(uv1, uv2));
  float4 BR = IniParams.Load(int2(402, 0));   // (08/11 w) barba con hebras
  // (08/11 x, Agustin: "mas opciones para las hebras") 404: x grosor de raiz, y grosor de punta (fraccion de la raiz), z curvatura,
  // w desorden | 405: x mechones, y variacion de largo, z variacion de color, w sombra entre hebras (1 = como antes en todo)
  float4 HO = IniParams.Load(int2(404, 0)), HV = IniParams.Load(int2(405, 0));
  bool browTri = !(hi.x < 0.24 || lo.x > 0.76 || hi.y < 0.25 || lo.y > 0.38);
  bool beardTri = BR.x > 0.5 && !(hi.x < 0.18 || lo.x > 0.82 || hi.y < 0.5 || lo.y > 0.9);
  if (!browTri && !beardTri) return;
  // 2) tamano en pantalla (y75 = distancia maxima a zoom normal, medida en pixeles por metro como la v1)
  float3 P0 = v[0].pV.xyz, P1 = v[1].pV.xyz, P2 = v[2].pV.xyz;
  float zc = (P0.z + P1.z + P2.z) / 3.0;
  if (zc < 0.05) return;
  float vpH = m_viewportSize.y > 16.0 ? m_viewportSize.y : 1080.0;
  float ppm = abs(m_projection[1].y) * vpH * 0.5 / zc;
  float ppmMin = 1.73 * 540.0 / max(P75.y, 0.5);
  if (ppm < ppmMin && P75.z < 2.5) return;
  // 3) jacobiano UV -> posicion (en la vista) del triangulo
  float3 e1 = P1 - P0, e2 = P2 - P0;
  float2 d1 = uv1 - uv0, d2 = uv2 - uv0;
  float det = d1.x * d2.y - d2.x * d1.y;
  float uvArea = abs(det) * 0.5;
  if (uvArea < 1e-9) return;
  float3 dPdu = (e1 * d2.y - e2 * d1.y) / det;
  float3 dPdv = (e2 * d1.x - e1 * d2.x) / det;
  // hebras esperadas en este triangulo: densidad por unidad de area UV (y74 = 2 -> ~450 por ceja con el mapa tipico)
  bool hb = pesHairMisc.x > 0.5;
  float dens = clamp(P74.y, 0.25, 3.0) * 70000.0 *   // (07/11 b, Agustin: "falta cantidad") 40000 -> 70000
                   // (06/11, Agustin: "se ven poco pobladas") 25000 -> 40000
                 (hb ? max(pesHairBrow.y, 0.05) : 1.0);
  float keepBase = saturate(uvArea * dens / (float)(PES_INST * PES_PER_INST));
  float skRefB = beardTri ? PesBeardSkinRef() : 0.01;   // (08/11 y) piel limpia de la cara (una vez por triangulo)
  float keepBeard = saturate(uvArea * clamp(BR.y, 0.1, 4.0) * 30000.0 / (float)(PES_INST * PES_PER_INST));
  float mLen = hb ? max(pesHairBrow.w, 0.05) : 1.0, mWid = hb ? max(pesHairBrow.x, 0.05) : 1.0, mLift = hb ? pesHairBrow.z : 1.0;
  float curl = hb ? saturate(pesHairA.y) : 0.1;
  float lift = clamp(P75.x * mLift, 0.0, 1.5) * 0.28;   // (07/11 b) un poco mas levantadas: mas nivel 3D    // (04/11 e) aun mas acostadas: la raiz no se despega de la piel   // (04/11) pegadas a la piel: la v1 se levantaba demasiado
  float flip = P75.w < -0.5 ? -1.0 : 1.0;

  [loop] for (uint k = 0; k < PES_PER_INST; k++) {
    uint seed = PesHash(pid * 7919u + (inst * PES_PER_INST + k) * 104729u + 17u);
    float r0 = PesRnd(seed), r1 = PesRnd(seed), r2 = PesRnd(seed), r3 = PesRnd(seed), rk = PesRnd(seed), re = PesRnd(seed);
    if (r0 + r1 > 1.0) { r0 = 1.0 - r0; r1 = 1.0 - r1; }
    float3 b = float3(1.0 - r0 - r1, r0, r1);
    float2 uv = uv0 * b.x + uv1 * b.y + uv2 * b.z;
    float4 m = tHairMap.SampleLevel(sLin, uv, 0);
    bool isB = beardTri && uv.y > 0.46;
    float bDark = 0.0, skB = 0.01;
    if (isB) {
      skB = skRefB;
      bDark = PesBeardAmt(uv, skB);
      if (bDark < 0.05 || rk >= keepBeard * bDark) continue;
    }
    else if (m.a < 0.97) continue;                      // fuera de la zona de ceja (0.9 = cuero cabelludo, 0 = nada)
    float ph = PesPaintedHair(uv);
    // (06/11, Agustin: "no siguen del todo bien la textura") la raiz nace sobre un TRAZO pintado (pixel mas oscuro que el promedio de
    // su zona en la textura de la cara), no en el hueco claro entre dos trazos: la hebra cae encima del pelo dibujado
    float lF0 = PesLum(uv, 0.0), lF3 = PesLum(uv, 3.0);
    float strk = saturate(0.5 + (lF3 - lF0) / max(lF3, 1e-3) * 5.0);
    // (04/11 i, Agustin: "la cantidad tiene que agregar mas pelo donde falta") con cantidad > 1 la hebra tambien nace en la parte
    // rala / clara de la ceja pintada (umbral del contorno mas bajo) y alli con la misma densidad que en lo tupido: rellena huecos
    float fill = hb ? saturate((pesHairBrow.y - 1.0) * 0.6) : 0.0;
    // (04/11 l, Agustin: "no me gusta la cantidad de pelo y como respeta la textura") la densidad sigue la pintura: tupido donde la
    // ceja pintada es oscura y llena, ralo en el borde claro (antes casi pareja en toda la ceja)
    bool born = ph >= lerp(0.35, 0.12, fill) && rk < keepBase * saturate(m.r + fill + 0.3) * saturate((ph - 0.2) * 2.5 + fill * 1.5) * lerp(0.5, 1.4, strk);   // (07/11 b) umbrales mas blandos: mas pelo
    // (04/11 l, Agustin: "permitirme cargar 400 hebras mas por jugador") x370 = hebras EXTRA en total (las dos cejas, ~0.016 de area UV
    // cada una): nacen sobre todo donde la ceja pintada es rala (rellenan huecos), nunca fuera de la ceja pintada
    float pX = hb ? uvArea * max(pesBrowX.x, 0.0) * 31.0 / (float)(PES_INST * PES_PER_INST) * saturate(1.3 - ph) * 1.6 : 0.0;
    // (08/11 w, Agustin: "no me gusta la opcion de hebras extras para rellenar, le falta algo") las extra nacian sobre todo en el borde
    // ralo (halo de pelos sueltos alrededor de la ceja): ahora siguen la forma de la ceja pintada como las demas (mas donde es tupida)
    // y van en las capas de arriba, con la direccion y el mechon de sus vecinas: suman volumen dentro de la ceja, no pelusa afuera
    pX = hb ? uvArea * max(pesBrowX.x, 0.0) * 31.0 / (float)(PES_INST * PES_PER_INST) * saturate(ph * 1.4) * 1.3 : 0.0;
    bool extra = ph >= 0.25 && re < pX;
    if (!isB && !born && !extra) continue;
    bool isX = extra && !born;
    if (isB) ph = bDark;
    float3 root = P0 * b.x + P1 * b.y + P2 * b.z;
    float3 n = normalize(v[0].nrm.xyz * b.x + v[1].nrm.xyz * b.y + v[2].nrm.xyz * b.z);
    // direccion del peinado: angulo del mapa (B) en el plano UV, llevado a 3D y apoyado sobre la piel
    float ang = m.b * 6.2831853;
    float2 dirUV = isB ? PesBeardDirUV(uv) : normalize(lerp(PesBrowDirUV(uv), float2(cos(ang), sin(ang)), 0.96)) * flip;   // (06/11) 0.9 -> 0.96
      // (04/11 l) 90 % los trazos pintados (Agustin: "que crezcan casi todos los trazos de la textura")
    float3 d = dPdu * dirUV.x + dPdv * dirUV.y;
    float mPerUV = length(d);
    d = d - n * dot(d, n);
    if (dot(d, d) < 1e-12) continue;
    d = normalize(d);
    float3 bt = normalize(cross(n, d));
    // (04/11 l, Agustin: "abajo de las cejas, con animaciones muy extremas" no siguen) la hebra era recta en el plano de la raiz: con
    // un gesto fuerte la piel de abajo se pliega y la punta se hundia. Curvatura de la piel a lo largo del pelo (normales del
    // triangulo deformado): la hebra sube con el pliegue (concavo) y baja a medias en lo convexo, siempre pegada a la piel
    float kap;
    {
      float3 n0 = v[0].nrm.xyz, n1 = v[1].nrm.xyz, n2 = v[2].nrm.xyz;
      float3 f1 = n1 - n0, f2 = n2 - n0;
      float3 dNdu = (f1 * d2.y - f2 * d1.y) / det, dNdv = (f2 * d1.x - f1 * d2.x) / det;
      float3 dNds = (dNdu * dirUV.x + dNdv * dirUV.y) / max(mPerUV, 1e-5);
      kap = clamp(-dot(dNds, d), -60.0, 400.0);   // 1/m: > 0 la piel sube delante del pelo (pliegue)
      kap = kap > 0.0 ? kap : kap * 0.5;
    }
    float ga = (r2 - 0.5) * 0.14 * clamp(HO.w, 0.0, 4.0);                   // giro individual de +-4 grados (06/11: antes +-8, se despeinaban de la textura)
    float3 dd = normalize(d * cos(ga) + bt * sin(ga));
    // (07/11, Agustin: "tenemos que seguir mejorando") MECHONES: en una ceja real los pelos vecinos se juntan en las puntas (grasa
    // de la piel, peinado). Celdas de ~2 mm en UV: la punta de cada hebra se corre hacia el centro de su celda a lo ancho del pelo
    float clat;
    { float cs = 0.007;
      float2 cid = floor(uv / cs);
      uint ch = PesHash((uint)(cid.x + 4096.0) * 73856093u ^ (uint)(cid.y + 4096.0) * 19349663u);
      float2 cc = (cid + 0.5 + (float2((float)(ch & 255u), (float)((ch >> 8) & 255u)) / 255.0 - 0.5) * 0.6) * cs;
      float2 dU = cc - uv;
      clat = dot(dPdu * dU.x + dPdv * dU.y, bt); }
    // apenas fuera de la piel; con la teselacion de la cara prendida (x357) la piel redondeada puede subir hasta z358 mm (tope del
    // corrimiento) + z359 mm (relieve hacia afuera): la raiz sube lo mismo para que la piel no tape las hebras de cerca
#ifdef PES_BROW_VS_B
    float2 uvT = (v[0].uv4.zw * b.x + v[1].uv4.zw * b.y + v[2].uv4.zw * b.z);   // el DS de 455eb lee la UV en TEXCOORD0.zw
#else
    float2 uvT = uv;
#endif
    root += PesTessDisp(v, b, uvT);
    // (06/11, Agustin: "una opcion para alejarlas del modelo o acercarlas") w370 del jugador = distancia a la piel en mm (+ afuera, - adentro)
    if (hb) root += n * clamp(pesBrowX.y, -2.0, 3.0) * 0.001;   // (04/11 e) sobre la piel teselada real (antes 0.4 mm + 1.4 mm fijos por la normal)
    float len;
    if (isB) {
      len = clamp(BR.z, 1.0, 25.0) * 0.001 * (0.8 + 0.4 * r3) * (0.6 + 0.4 * bDark);
      // (08/11 y, Agustin: "deberian seguir los pelos su largo") la hebra no pasa el borde de la barba pintada (como las cejas): si la
      // punta o la mitad caen en piel limpia se acorta, y de cerca se acuesta sobre la piel siguiendo la cara (abajo, kap / liftS)
      float lenUV = len / max(mPerUV, 1e-5);
      if (PesBeardAmt(uv + dirUV * lenUV, skB) < 0.05) len *= (PesBeardAmt(uv + dirUV * lenUV * 0.5, skB) < 0.05) ? 0.4 : 0.7;
    }
    else {
      len = max(P74.z, 0.1) * 0.01 * mLen * (0.55 + 0.45 * m.g) * max(1.0 + 0.4 * (r3 - 0.5) * clamp(HV.y, 0.0, 2.5), 0.3) * 0.8;
      // la hebra no sale de la ceja pintada: si la punta (o la mitad) cae en piel, se acorta
      {
        float lenUV = len / max(mPerUV, 1e-5);
        if (PesPaintedHair(uv + dirUV * lenUV) < 0.2) len *= (PesPaintedHair(uv + dirUV * lenUV * 0.5) < 0.2) ? 0.55 : 0.85;   // (07/11, referencia eFootball de Agustin) el borde de la ceja queda deshilachado: los pelos pasan un poco la pintura
      }
    if (isX) len *= 0.85; }
    float widB = isB ? max(BR.w, 0.05) * 0.001 * 0.3 : max(P74.w, 0.05) * 0.001 * mWid * 0.32 * clamp(HO.x, 0.2, 3.0);
    // (08/11 w, Agustin: "el problema es el grosor... un tick para respetar el grosor de la textura real o bajar el grosor sin perder
    // longitud ni cantidad") x403 = 1: el ancho de cada pelo sale del trazo pintado (trazo marcado y tupido = pelo grueso, trazo
    // suave / ralo = pelo fino, 0.55..1.25 del grosor elegido). Y el grosor ya no decide cuantas hebras quedan ni su largo (abajo)
    float widRef = (isB ? 0.25 : 0.35) * 0.001 * 0.32;   // grosor de referencia: el LOD cuenta hebras con este, no con el elegido
    if (!isB && IniParams.Load(int2(403, 0)).x > 0.5) widB *= lerp(0.55, 1.25, saturate(strk * 0.6 + ph * 0.6));   // (07/11 b) 0.25 -> 0.32: con la normal de tubo cada pelo se lee como cilindro   // (04/11 b) mas finas: 0.35 -> ~0.09 mm de ancho real
#ifndef PES_ATLAS
    // (08/11 v, Agustin: "no me gusta su definicion con el antialiasing") LOD POR HEBRA en vez de tramado por pixel: una hebra mas fina
    // que un pixel se dibujaba a 0.9 px y el PS borraba pixeles con un tramado fijo en PANTALLA; al moverse la cara el tramado no la
    // acompanaba y el TAA lo promediaba a una mancha borrosa. Ahora se descarta la hebra ENTERA (hash propio, estable entre cuadros)
    // con la probabilidad de su cobertura real y las que quedan se dibujan opacas y nitidas (como el LOD de pelo de UE / Frostbite).
    // La sombra de contacto (PES_SHADOW) usa el mismo hash: sombrean exactamente las hebras que se ven
    if (P75.z < 0.5) {
      float pxR = 2.0 * abs(root.z) / (max(abs(m_projection[1].y), 1e-4) * vpH);
      float pk = clamp(max(widB, widRef) / max(pxR * 0.9, 1e-9) * 1.6, 0.4, 1.0);   // (08/11 w) mas fino ya no = menos hebras
      if (frac(r1 * 31.71 + r3 * 17.33 + re * 5.97) > pk) continue;
    }
#endif
#ifdef PES_ATLAS
    // (08/11, Agustin: "las sombras siguen mal") el texel del atlas mide ~1.8 mm (caja de 1.8 m en 1024 px) y cada pelo (~0.17 mm) se
    // dibujaba de ~1 mm: la ceja tapaba el sol como una placa solida (mancha negra en la piel y en las hebras de abajo). Ahora solo
    // entra al atlas la fraccion de pelos que da la cobertura real (ancho real / ancho dibujado): el PCF promedia y la sombra de la
    // ceja sobre la piel y entre capas queda con la opacidad de los pelos de verdad
    if (frac(r0 * 17.31 + r1 * 7.13 + rk * 3.7) > saturate(widB * 1.4 / (PesAtlasTexelM() * 1.1))) continue;
#endif
    // color: el de la ceja pintada en ese punto (raiz) y la punta un poco mas clara, o el del tipo de pelo
    float3 painted = tBrow.SampleLevel(sLin, uv, 0).rgb;   // (04/11 d) el color de los pelos pintados de esa zona (no la piel de al lado)
    float3 cR = (hb && pesHairRoot.w > 0.5) ? pesHairRoot.xyz : painted;
    float3 cT = (hb && pesHairTip.w > 0.5) ? pesHairTip.xyz : painted * float3(1.12, 1.08, 1.04);   // punta apenas mas clara (08/11 v: antes x1.25 y otro x1.35 abajo: puntas naranjas / lavadas)
    // (04/11 o, Agustin: "el color poco natural aunque permita seleccionar el color") el color elegido era plano: ahora conserva la
    // variacion de la ceja pintada (cada pelo mas claro / oscuro que el promedio de su zona) y cada hebra su tono
    float lumP = dot(painted, float3(0.2126, 0.7152, 0.0722)), lumZ = dot(tBrow.SampleLevel(sLin, uv, 3).rgb, float3(0.2126, 0.7152, 0.0722));
    float vari = clamp(lumP / max(lumZ, 1e-3), 0.65, 1.35) * lerp(0.88, 1.1, frac(r2 * 13.7));
    vari = max(1.0 + (vari - 1.0) * clamp(HV.z, 0.0, 3.0), 0.3);   // (08/11 x) z405 variacion de color
    { // (04/11 o, Agustin: "falta el color para las hebras normales que se aplique a todas") fila 373: color de las hebras de TODOS
      // los jugadores (x mezcla con el color pintado, yzw rgb); el color propio del jugador (367) va encima
      float4 G = IniParams.Load(int2(373, 0));
      float kg = saturate(G.x);
      if (kg > 0.001) { cR = lerp(cR, saturate(G.yzw) * vari, kg); cT = lerp(cT, saturate(G.yzw) * vari * float3(1.12, 1.08, 1.04), kg); }
    }
    if (hb && pesBrowCol.w > 0.001) {   // (04/11 i) color elegido para este jugador (menu Cejas con hebras)
      float km = saturate(pesBrowCol.w);
      cR = lerp(cR, pesBrowCol.rgb * vari, km); cT = lerp(cT, pesBrowCol.rgb * vari * float3(1.12, 1.08, 1.04), km);
    }
    // (07/11, referencia eFootball de Agustin: Messi al sol) las puntas son mas claras y calidas (pelo aclarado por el sol) y algunas
    // hebras sueltas mucho mas: al sol la ceja "chispea" pelo por pelo en vez de ser una mancha pareja
    cT *= float3(1.04, 1.02, 0.99) * (frac(r3 * 11.3 + rk * 5.1) < 0.15 ? 1.15 : 1.0);   // (08/11 v) mas sutil
    // (04/11 j, Agustin: "necesitan un sombreado entre pelos mejor") sombra entre pelos: la raiz queda tapada por los vecinos (mas
    // oscura donde la ceja es tupida = contorno alto del mapa) y cada pelo con un tono apenas distinto (se ven capas)
    // (07/11, Agustin: "que tenga geometria y profundidad 3d real") CAPAS: hasta ahora todas las hebras iban a la misma altura sobre
    // la piel (ceja plana). Cada pelo tiene su capa (0 = pegado a la piel .. 1 = arriba de todo); donde la ceja es tupida las capas
    // de arriba cabalgan sobre las de abajo hasta ~1.3 mm (la raiz sigue naciendo en el poro: la altura entra a lo largo del pelo).
    // Las capas de abajo quedan tapadas (mas oscuras) y las de arriba toman la luz: volumen y profundidad reales, con sombra de
    // contacto mas larga para las de arriba (PES_SHADOW usa la altura real)
    float lay = frac(r0 * 5.17 + r1 * 3.71 + r3 * 1.93);
    if (isX) lay = 0.6 + 0.4 * lay;   // (08/11 w) las extra arriba: volumen
    float stackH = (isB ? 0.25 : 1.0) * lay * lay * 0.0012 * saturate(ph * 1.6) * max(mLift, 0.3);   // (08/11 w, Agustin: "se separan de la cara") 2.2 -> 1.2 mm   // (07/11 b) 1.3 -> 2.2 mm: mas profundidad entre capas
    { float occ = lerp(0.96, 0.8, saturate(ph * 1.2)) * lerp(lerp(0.86, 1.02, lay), 1.0, 1.0 - saturate(ph * 1.6));   // abajo tapado, arriba a la luz   // (05/11, Agustin: "se ven muy oscuras con la luz de frente") antes 0.88..0.62
      float layer = lerp(0.88, 1.08, frac(r3 * 7.31 + rk * 3.17));
      float kv = clamp(HV.z, 0.0, 3.0), ks = clamp(HV.w, 0.0, 3.0);   // (08/11 x) z405 tono por hebra, w405 sombra entre hebras
      occ = max(1.0 - (1.0 - occ) * ks, 0.2); layer = max(1.0 + (layer - 1.0) * kv, 0.5);
      cR *= occ * layer; cT *= layer; }
    if (isB) {   // (08/11 w) barba: el color pintado en ese punto, siempre mas oscuro que la piel de alrededor
      float3 pB = tFace.SampleLevel(sLin, uv, 0.5).rgb;
      float lB = dot(pB, float3(0.2126, 0.7152, 0.0722)), capB = 0.5 * skB;
      if (lB > capB) pB *= capB / max(lB, 1e-4);
      float vb = lerp(0.85, 1.1, frac(r2 * 13.7));
      cR = pB * 0.9 * vb; cT = pB * 1.08 * vb; }
    float liftS = (isB ? 0.1 : lift) * clamp(HO.z, 0.0, 3.0);   // (08/11 x) z404 curvatura
    // (08/11 v, Agustin: "el color de las cejas nunca tiene que ser el color de la piel") tope final, despues de los colores elegidos,
    // las puntas y las capas: ni la raiz ni la punta pasan el 60 % del brillo de la piel de la zona (mip 5 de la cara)
    { float skL = PesLum(uv, 5.0), capL = 0.7 * max(skL, 0.02);   // (08/11 w) 0.6 -> 0.7
      float lR = dot(cR, float3(0.2126, 0.7152, 0.0722)), lT = dot(cT, float3(0.2126, 0.7152, 0.0722));
      if (lR > capL) cR *= capL / max(lR, 1e-4);
      if (lT > capL) cT *= capL / max(lT, 1e-4); }
    // vectores de movimiento de la raiz (misma cuenta que el PS de la cara: actual - anterior en UV de pantalla)
    float4 cp = v[0].clipCP * b.x + v[1].clipCP * b.y + v[2].clipCP * b.z;
    float2 ww = v[0].ww * b.x + v[1].ww * b.y + v[2].ww * b.z;
    float4 q = cp / ww.xxyy;
    q = q * 0.5 + 0.5;
    q = q.zwxy * float4(1, -1, 1, -1) + float4(0, 1, 0, 1);
    float2 motion = q.zw - q.xy;
#ifdef PES_SHADOW
    // (04/11 k, Agustin: "no solo brillo, luz real") la hebra proyectada sobre el plano de la piel siguiendo la luz: la sombra que
    // el pelo le hace a la piel de abajo (y a la ceja pintada). Sin luz o con la piel de espaldas a la luz, nada
    float3 Ls = PesShadowL(root.z);
    float nl = dot(Ls, n);
    if (nl < 0.08) continue;
#endif
    [unroll] for (uint ring = 0; ring <= PES_SEG; ring++) {
      float t = (float)ring / (float)PES_SEG;
      // sale levantada y se acuesta hacia la punta (pelo de ceja real); el rizo la tuerce en el plano de la piel
      // (04/11 e, Agustin: "que se sienta natural, la ceja sale desde el modelo o desde adentro del modelo, como el FC") la raiz
      // nace 0.3 mm DENTRO de la piel y atraviesa la superficie en el primer cuarto de la hebra: la piel tapa el arranque y el
      // pelo se ve salir del poro, sin borde ni separacion
      float emerge = -0.0003 + 0.00045 * saturate(t * 4.0);
      float3 p = root + n * emerge + dd * len * t + n * len * liftS * (0.35 * sin(t * 3.14159) + 0.1 * t) + bt * len * curl * clamp(HO.z, 0.0, 3.0) * 0.5 * sin(t * 6.2831853 + r3 * 6.2831853) * t;
      p += n * clamp(0.5 * kap * (len * t) * (len * t), -0.25 * len, 0.6 * len);   // (04/11 l) sigue el pliegue de la piel
      p += bt * clamp(clat, -0.3 * len, 0.3 * len) * 0.55 * clamp(HV.x, 0.0, 2.5) * t * t;                    // (07/11) mechon: la punta va hacia sus vecinos
      p += n * stackH * smoothstep(0.0, 0.45, t);                                       // (07/11) capa: sube sobre las de abajo
      float wid = widB * (1.0 - (1.0 - clamp(HO.y, 0.02, 1.0)) * t);   // (08/11 x) y404 grosor de punta (0.15 = como antes)
      float pxV = 2.0 * abs(p.z) / (max(abs(m_projection[1].y), 1e-4) * vpH);   // un pixel en la vista a esta distancia
      // (06/11, Agustin: "no se ve como un hair strand de un motor moderno") la hebra fina ya no se engorda a 1 px: se dibuja de 1.2 px
      // y el PS descarta pixeles al azar segun la cobertura real (ancho real / dibujado); el TAA lo promedia -> pelo fino y suave
      float wReal = wid;
#ifndef PES_ATLAS
      wid = max(wid, pxV * (P75.z > 0.5 ? 1.25 : lerp(0.6, 0.9, saturate(widB / widRef))));   // (08/11 w) grosor bajo = linea mas fina en pantalla   // (07/11 b, Agustin: "el TAA las desenfoca") 0.6 -> 0.9 px: menos tramado
#endif
      float cov = P75.z > 0.5 ? 1.0 : saturate(wReal / max(pxV * 1.5, 1e-9));   // (08/11 v) z = RESOLUCION de la hebra (1 = 1.5 px o mas de ancho real): el PS ya no trama, la usa para normal / brillo / borde
#ifdef PES_SHADOW
      float hgt = max(dot(p - root, n), 0.0);                                    // altura sobre la piel
      p = p - Ls * (hgt / max(nl, 0.25)) + n * 0.00005;                          // al plano de la piel, siguiendo la luz
      wid = wid * 1.6 + hgt * 0.04;                                              // penumbra que se abre con la altura
#endif
#ifdef PES_ATLAS
      float3 V = PesAtlasLightV();                                              // (07/11 c) la cinta mira al sol
      wid = max(wid, PesAtlasTexelM() * 0.55);                                  // y no mas fina que medio texel del atlas
#else
      float3 V = normalize(-p);
#endif
      float3 tanR = normalize(dd + n * liftS * (0.35 * 3.14159 * cos(t * 3.14159) + 0.1));
      float3 sideV = normalize(cross(dd, V) + 1e-6);
      [unroll] for (int s = -1; s <= 1; s += 2) {
        GSOut o;
        float side = (float)s;
#ifdef PES_ATLAS
        o.pos = PesAtlasClip(p + sideV * wid * side);
#else
        o.pos = ProjV(p + sideV * wid * side);
#endif
        // (04/11 e, Agustin: "que la luz la tome bien el pelo de la ceja") antes la normal miraba a la CAMARA (V * 0.6): con el sol
        // de costado la hebra se iluminaba distinto que la piel de abajo (alambre oscuro o brillante). Ahora es la normal de la piel
        // redondeada solo hacia los costados de la hebra (perpendicular a su direccion, en el plano de la piel): de lejos la ceja
        // toma la misma luz y sombra que la cara, de cerca cada pelo tiene volumen; la punta levantada se inclina un poco hacia
        // donde apunta el pelo
        float3 sideS = normalize(cross(n, dd));
        // (07/11 b, Agustin: "faltan normales") el GS manda la normal del EJE del pelo; el PS arma la normal de TUBO con el lado (tw.y)
        float3 nH = normalize(n * lerp(1.0, 0.8, lay) + dd * (liftS * 0.6 * t));
        o.nrmV = nH;
        o.col = cR; o.col2 = cT;
        o.tw = float3(t, side, clamp(cov, 0.02, 0.98) + 2.0 * (floor(frac(r2 * 7.77 + r3 * 3.33) * 8.0) + 8.0 * floor(lay * 3.999)));   // (08/11) + 16 x capa (0..3) para la sombra entre capas del PS   // (07/11) z = cobertura + 2 x (0..7) tono de brillo de la hebra
        o.motion = motion;
        o.viewV = V;
        o.tanV = tanR;
        o.posV = p;
        outS.Append(o);
      }
    }
    outS.RestartStrip();
  }
}
