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
}
Texture1D<float4> IniParams : register(t120);

struct VSOut {
  float4 pos : SV_Position;
  float3 nrmV : TEXCOORD0;
  float3 col : TEXCOORD1;
  float2 tw : TEXCOORD2;
  float2 motion : TEXCOORD3;
  float3 viewV : TEXCOORD4;
  float3 col2 : TEXCOORD5;
};

void main(VSOut i, out float4 o0 : SV_Target0, out float4 o1 : SV_Target1, out float4 o2 : SV_Target2, out float4 o3 : SV_Target3, out float4 o4 : SV_Target4)
{
  float4 P75 = IniParams.Load(int2(75,0));
  float t = i.tw.x;
  // borde de la cinta transparente por descarte (sin alpha blend en el G-buffer): perfil redondeado
  float edge = 1.0 - abs(i.tw.y);
  // con cintas de ~1 px el descarte tiene que ser suave: solo el borde exterior y la punta
  if (P75.z < 0.5 && edge < 0.08 + 0.35 * t * t) discard;
  // color: la raiz mas oscura, la punta un poco mas clara y calida
  float3 col = lerp(i.col, i.col2, t) * (0.55 + 0.6 * t) * float3(1.0, 0.97, 0.94);
  if (P75.z > 0.5) col = float3(1, 0, 1);   // diagnosticos 1 y 3: magenta
  o0 = float4(saturate(col), 1.0);
  // normal + translucidez (hebra: 0.15) empaquetadas como en el shader de cara
  float3 n = normalize(i.nrmV);
  float trans = 0.15;
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
  float rough = 0.62;
  float rc = rough * 0.0361702144 + 0.0904255286;
  rc = rc * rough + 0.180851057; rc = rc * rough + 0.271276593; rc = rc * rough + 0.271276593;
  o2.x = rc * rough + 0.150000006;
  o2.y = 0.30 * (pesHairMisc.x > 0.5 ? saturate(pesHairB.z) / 0.6 : 1.0);   // brillo del tipo de pelo (0.6 = natural)
  o2.z = 1.0;                                   // sin marca de piel ni de kit
  o2.w = g_psMaterial.m_materials[0].x;
  o3.xy = g_psSystem.m_renderBuffer.xy * i.motion;
  o3.zw = 0;
  o4 = float4(0, 0, 0, 1);
}
