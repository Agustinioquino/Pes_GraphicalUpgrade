// PES21 FX - hebras de ceja (18/10): pixel shader. Escribe el G-buffer con el mismo formato que la piel de la cara
// (color, normal octaedrica + translucidez, rugosidad/especular/material, vectores de movimiento), asi recibe la luz,
// las sombras y el SSS de la pasada diferida como todo lo demas. z75 = 1 pinta las hebras de magenta (diagnostico).
cbuffer cPSSystem : register(b0)
{
  struct { float4 m_param; float4 m_renderInfo; float4 m_renderBuffer; float4 m_dominantLightDir; float4 m_timeParam; } g_psSystem;
}
cbuffer cPSMaterial : register(b4)
{
  struct { float4 m_materials[8]; } g_psMaterial;
}
// PES21 (18/10) tipo de pelo de ESTA cara (PES21FX_hair.ini, lo enlaza la DLL en b11; si no esta enlazado todo vale 0 y pesHairMisc.x = 0)
cbuffer PESHair : register(b11)
{
  float4 pesHairA;      // largo_cm, rizo, rigidez, peso
  float4 pesHairB;      // grosor_mm, densidad, brillo, flequillo
  float4 pesHairRoot;   // color raiz rgb, w = 1 si esta definido
  float4 pesHairTip;    // color punta rgb, w = 1 si esta definido
  float4 pesHairBrow;   // ceja: grosor, densidad, elevacion, largo (multiplicadores)
  float4 pesHairMisc;   // x = 1 enlazado, y = 1 mapa de pelo propio del usuario
  float4 pesBrowCol;    // (04/11 i) color de ceja del jugador (lo usa el GS)
  float4 pesBrowBack;   // (04/11 l) color A CONTRALUZ del jugador (fila 369): rgb, w = mezcla (0 = el mismo de frente)
  float4 pesBrowX;      // (04/11 l) x = hebras extra (lo usa el GS)
}
Texture1D<float4> IniParams : register(t120);
// (04/11 e) la escena del PS (la deja enlazada el dibujo de la cara; misma que usa el pelo 02f1d510): matriz de la vista para la luz
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
// (04/11 e, Agustin: "que la luz la tome bien el pelo de hebras de la ceja") MISMO modelo de luz que el pelo de la cabeza
// (02f1d510 PESHairAniso, filas 50 / 51): la normal se lleva hacia la que refleja el sol hacia la camara para ESTA hebra ->
// el GGX de la pasada diferida da la banda de brillo del pelo (Kajiya-Kay) en vez de un punto; y la marca 252/255 hace que
// el sol le sume la luz envolvente, el segundo brillo (TRT, fila 51) con la sombra real y el contraluz (fila 339)
#include "pes_brow_light.hlsli"
float3 PesBrowAniso(float3 N, float3 T, float3 V, float t, float shJ)
{
  float4 HK = IniParams.Load(int2(50,0));
  float4 HK2 = IniParams.Load(int2(51,0));
  if (HK.w < 0.5) return N;
  float3 L = PesBrowLightDir(g_psScene.m_view._m00_m10_m20, g_psScene.m_view._m01_m11_m21, g_psScene.m_view._m02_m12_m22, g_psSystem.m_dominantLightDir.xyz, -V.z);   // (04/11 o) la luz real
  if (dot(L, L) < 0.25) return N;   // escena o luz sin enlazar: normal de antes
  float3 H = normalize(L + V);
  T = normalize(T);
  float sh1 = HK.y + (t - 0.5) * HK2.y + shJ;   // (07/11) cada hebra con su propio corrimiento: el brillo no es una linea pareja
  float3 T1 = normalize(T + N * sh1);
  float3 Nk = H - T1 * dot(H, T1);
  float nkl = length(Nk);
  Nk = nkl > 1e-4 ? Nk / nkl : N;
  if (dot(Nk, V) < 0.05) Nk = N;
  return normalize(lerp(N, Nk, saturate(HK.x) * 0.9));   // (04/11 j) la banda de brillo (R de Marschner) tiene que verse y moverse a lo largo del pelo
}

struct VSOut {
  float4 pos : SV_Position;
  float3 nrmV : TEXCOORD0;
  float3 col : TEXCOORD1;
  float3 tw : TEXCOORD2;       // z = (06/11) cobertura real del pixel
  float2 motion : TEXCOORD3;
  float3 viewV : TEXCOORD4;
  float3 col2 : TEXCOORD5;
  float3 tanV : TEXCOORD6;
};

void main(VSOut i, out float4 o0 : SV_Target0, out float4 o1 : SV_Target1, out float4 o2 : SV_Target2, out float4 o3 : SV_Target3, out float4 o4 : SV_Target4)
{
  float4 P75 = IniParams.Load(int2(75,0));
  float t = i.tw.x;
  // (07/11) tw.z = cobertura + 2 x tono de la hebra (0..7, igual en toda la hebra)
  float sK = floor(i.tw.z * 0.5), covZ = i.tw.z - 2.0 * sK, sJ = sK / 7.0;
  // borde de la cinta transparente por descarte (sin alpha blend en el G-buffer): perfil redondeado
  float edge = 1.0 - abs(i.tw.y);
  // con cintas de ~1 px el descarte tiene que ser suave: solo el borde exterior y la punta
  if (P75.z < 0.5 && edge < 0.08 + 0.35 * t * t) discard;
  // (06/11, Agustin: "no se ve como un hair strand de un motor mas moderno") cobertura estocastica: la hebra mas fina que un pixel
  // se dibuja en 1.2 px y aca se descarta al azar (ruido de gradiente entrelazado que cambia cada cuadro) segun su cobertura real y
  // la punta afinada; el TAA lo promedia -> hebras finas, translucidas en la punta y sin escalones, como el pelo de UE / Frostbite
  if (P75.z < 0.5) {
    // la ceja tiene muchos pelos superpuestos por pixel: la cobertura se escala x3 con piso 0.4 (si no, de lejos casi desaparece)
    float covP = clamp(covZ * 3.0, 0.4, 1.0) * lerp(1.0, 0.45, t * t);
    float ign = frac(52.9829189 * frac(dot(i.pos.xy, float2(0.06711056, 0.00583715))) + frac(g_psSystem.m_timeParam.z * 7.31));
    if (ign > covP) discard;
  }
  // color: la raiz mas oscura, la punta un poco mas clara y calida
  float3 col = lerp(i.col, i.col2, t);   // (04/11 d) el color del mapa de la ceja tal cual (antes se oscurecia / aclaraba aca)   // (04/11 b) antes 0.55 en la raiz: salian negras como alambre
  // (04/11 e, referencia eFootball de Agustin) la raiz queda entre los pelos de al lado y la piel (oclusion suave), la punta sale
  // a la luz: el pelo de la ceja se ve con volumen y brillo arriba, sin la raiz negra de la v1 (0.55)
  col *= lerp(0.95, 1.0, saturate(t * 2.5));   // (04/11 e) 0.82 -> 0.9: con el color nuevo ya no hace falta oscurecer tanto
  if (P75.z > 0.5) col = float3(1, 0, 1);   // diagnosticos 1 y 3: magenta
  // (04/11 j, Agustin: "la luz al cambiar el color no es realista, no traspasa de frente") un pelo no es una superficie opaca: la luz
  // entra, se dispersa entre las hebras y sale con el color del pelo (dispersion multiple). En el G-buffer eso equivale a un albedo
  // mas claro y saturado que el color "pintado" (col^0.8, saturacion x1.15): con luz de frente la ceja toma el tono del pelo
  // (castano, colorado) en vez de quedar como plastico oscuro; el contraluz y el brillo siguen en la pasada del sol (marca de pelo)
  if (P75.z < 0.5) {
    float3 cs = saturate(col);
    // (04/11 o, Agustin: "no me gusta como pasa la luz ni el color poco natural") mucho mas suave: el realce anterior (^0.8, sat x1.15)
    // sumado al de browmap dejaba las cejas naranjas y como iluminadas desde adentro
    float3 ms = pow(cs + 1e-4, 0.85);   // (05/11) mas claras con luz de frente, sin subir la saturacion (no vuelve el naranja)
    float ml = dot(ms, float3(0.2126, 0.7152, 0.0722));
    col = max(ml + (ms - ml) * 1.0, 0.0);
  }
  // (04/11 l, Agustin: "me gustaria poder cambiar el color frente a la luz o normal y a contraluz") con la luz detras de la ceja
  // (mirando hacia el sol) el pelo deja pasar la luz y se ve de otro tono: el color elegido para el contraluz entra segun cuanto
  // esta la camara del lado opuesto a la luz. Las pestanas (sin b11) y x369 = 0 quedan igual
  if (P75.z < 0.5 && pesHairMisc.x > 0.5 && pesBrowBack.w > 0.001) {
    float3 L = PesBrowLightDir(g_psScene.m_view._m00_m10_m20, g_psScene.m_view._m01_m11_m21, g_psScene.m_view._m02_m12_m22, g_psSystem.m_dominantLightDir.xyz, -i.viewV.z);
    if (dot(L, L) > 0.25) {
      // (04/11 o) L = HACIA la luz: contraluz = la luz del otro lado de la ceja respecto de la camara
      float kb = smoothstep(0.05, 0.75, dot(-L, normalize(i.viewV))) * saturate(pesBrowBack.w);
      float3 cb = saturate(pesBrowBack.rgb) * lerp(1.0, 1.15, t);   // la punta deja pasar mas luz
      col = lerp(col, cb, kb);
    }
  }
  o0 = float4(saturate(col), 1.0);
  // normal + translucidez (hebra: 0.15) empaquetadas como en el shader de cara
  float3 n = normalize(i.nrmV);
  n = PesBrowAniso(n, i.tanV, normalize(i.viewV), t, (sJ - 0.5) * 0.16);
  // (04/11) la luz pasa por el pelo: translucidez alta (la punta mas que la raiz) + marca de PELO 252/255 (o2.z, abajo) para
  // que las pasadas del sol le apliquen el contraluz del pelo (fila 339), como el pelo de la cabeza
  float trans = lerp(0.18, 0.4, t);   // (04/11 o) menos luz atravesando: la ceja ya no brilla como pelo fino de la cabeza a contraluz   // (04/11 e) un poco mas de luz atravesando el pelo (la raiz tambien es fina)
  float3 r1 = n; float r1w = trans;
  float4 r0;
  r0.x = r1w * 63 + 0.5;
  r0.y = abs(r1.x) + abs(r1.y) + abs(r1.z);
  float2 oct = r1.xy / r0.y;
  float2 alt = (1 - abs(oct.yx)) * (oct >= 0 ? 1 : -1);
  oct = (r1.z < 0) ? alt : oct;
  oct = oct * 0.5 + 0.5;
  uint2 q = (uint2)(oct * 2047.0);
  uint rx = (uint)r0.x;
  uint2 hi = q >> 1;
  uint lo = ((q.x & 1) << 1) | (q.y & 1);
  o1.xyw = float3(0.000977517106, 0.000977517106, 0.333333343) * float3(hi.x, hi.y, lo);
  o1.z = 0.000977517106 * (float)(rx << 4);
  // rugosidad (misma curva que la cara) y especular bajos: el pelo de ceja es mate con un brillo fino
  // (04/11 j, Agustin: "toma en cuenta como funciona la luz en pelo real, no solo cambian de color") pelo real (Marschner): R = brillo
  // blanco en banda que se corre hacia la raiz y se mueve con la camara / el sol (normal de Kajiya-Kay de arriba + GGX mas liso),
  // TRT = segundo brillo con el color del pelo y TT = contraluz (pasada del sol, marca de pelo), dispersion multiple (albedo de
  // abajo) y sombra entre pelos (GS). Antes el GGX era tan rugoso que la banda no se veia y solo cambiaba el color
  float rough = lerp(0.34, 0.46, sJ);   // (07/11) rugosidad propia de cada hebra (antes 0.4 todas)
  float rc = rough * 0.0361702144 + 0.0904255286;
  rc = rc * rough + 0.180851057; rc = rc * rough + 0.271276593; rc = rc * rough + 0.271276593;
  o2.x = rc * rough + 0.150000006;
  o2.y = 0.32 * lerp(0.65, 1.35, frac(sJ * 5.3)) * lerp(0.75, 1.0, t) * (pesHairMisc.x > 0.5 ? saturate(pesHairB.z) / 0.6 : 1.0);   // (07/11) brillo distinto por hebra (unas brillan mas), la raiz tapada brilla menos   // brillo del tipo de pelo (0.6 = natural)
  o2.z = IniParams.Load(int2(339,0)).w > 0.5 ? 0.98823529 : 1.0;   // (04/11) pelo a contraluz; sin eso como antes (ni piel ni kit)
  o2.w = g_psMaterial.m_materials[0].x;
  o3.xy = g_psSystem.m_renderBuffer.xy * i.motion;
  o3.zw = 0;
  o4 = float4(0, 0, 0, 1);
}
