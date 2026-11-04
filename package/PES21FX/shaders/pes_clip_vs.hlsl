// PES21 FX - recortes de pasto volando (20/10): cada particula (CPU, pitchwear.cpp) es un pedacito de hoja orientado segun
// su velocidad, dibujado con el PS de las hojas del juego (18afff) en la misma pasada que el pasto: queda en el G-buffer,
// con luz, sombra y el mismo atlas. Salidas = las del VS del pasto (498b), para que el PS las entienda.
cbuffer cVSScene : register(b2) { float4 cb2[21]; }
cbuffer cVSMaterial : register(b4) { float4 cb4[5]; }
SamplerState g_samplerLinear_Wrap_s : register(s10);
Texture2D<float4> g_tex_base : register(t4);
Texture1D<float4> IniParams : register(t120);
StructuredBuffer<float4> PESClips : register(t122);   // 2 por particula: (x, y, z, tamano), (vx, vy, vz, giro)

struct VSOut {
  float4 o0 : SV_Position0; float4 o1 : TEXCOORD0; float4 o2 : TEXCOORD1; float4 o3 : TEXCOORD2; float4 o4 : TEXCOORD3;
  float4 o5 : TEXCOORD4; float4 o6 : TEXCOORD5; float4 o7 : TEXCOORD6; float4 o8 : TEXCOORD7;
};
VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
  VSOut o;
  float4 p = PESClips[iid * 2], v = PESClips[iid * 2 + 1];
  // (2/11) v.w = giro + 10 * tipo (0 manojo arrancado, 1 terron de tierra, 2 polvo, 3 pedazo de hoja): bandas del atlas de particulas
  // textures\pes_debris.dds que la DLL enlaza en t8 / t9 solo en este dibujo
  float kind = clamp(floor(v.w / 10.0 + 0.001), 0.0, 3.0); float rot = v.w - kind * 10.0;
  bool lump = kind == 1.0 || kind == 2.0;
  uint k = vid % 6;
  float cx = (k == 1 || k == 4 || k == 5) ? 1.0 : 0.0, cy = (k == 2 || k == 3 || k == 5) ? 1.0 : 0.0;
  // eje de la hoja: su velocidad (si vuela) girando sobre si misma; ancho perpendicular a la camara
  float3 eye = cb2[20].xyz; float3 toCam = normalize(eye - p.xyz);
  float3 axis = normalize(v.xyz + float3(0.0, 0.3, 0.0) + 1e-3);
  float3 side = normalize(cross(axis, toCam) + 1e-4);
  float cr = cos(rot), sr = sin(rot);
  float3 axisR = normalize(axis * cr + cross(side, axis) * sr);   // giro alrededor del ancho
  float len = max(p.w, 0.002), wid = len * (lump ? 1.0 : 0.22);   // terrones y polvo: tarjeta cuadrada
  float3 world = p.xyz + side * ((cx - 0.5) * wid) + axisR * (cy * len);
  float4 wp = float4(world, 1.0);
  float4 vp = float4(dot(wp, cb2[8]), dot(wp, cb2[9]), dot(wp, cb2[10]), dot(wp, cb2[11]));
  float4 cp = float4(dot(vp, cb2[4]), dot(vp, cb2[5]), dot(vp, cb2[6]), dot(vp, cb2[7]));
  o.o0 = cp;
  // tarjeta entera de la banda del tipo (raiz abajo, v = 1)
  o.o1 = float4(cx, 1.0 - cy, 1.0, lump ? 0.9 : 0.55);
  o.o2 = float4(world, 0.0);
  // color: la base de la cancha en ese punto (misma cuenta que el VS del pasto: mosaico 32 x 24)
  float2 puv = world.xz * cb4[1].xy + cb4[1].zw;
  float4 b = g_tex_base.SampleLevel(g_samplerLinear_Wrap_s, puv * float2(32.0, 24.0), 2.0);
  float3 col = saturate(b.xyz * float3(0.92, 1.0, 0.85));
  float lum = dot(b.xyz, float3(0.3, 0.59, 0.11));
  if (kind == 1.0) col = float3(0.34, 0.25, 0.15) * (0.7 + 0.9 * lum);        // tierra: marron, con la claridad del suelo
  else if (kind == 2.0) col = float3(0.55, 0.48, 0.36) * (0.8 + 0.8 * lum);   // polvo: tierra clara
  o.o3 = float4(col, 0.6);
  // normal: mezcla de "arriba" y hacia la camara (pedacito suelto, se ilumina parejo)
  float3 nW = normalize(float3(0.0, 1.0, 0.0) * 0.6 + toCam * 0.5);
  o.o4 = float4(dot(nW, cb2[8].xyz), dot(nW, cb2[9].xyz), dot(nW, cb2[10].xyz), 0.0);
  o.o5 = float4(0.3, 0.3, 0.0, 0.0);
  o.o6 = float4(cp.xy, dot(wp, cb2[16]), dot(wp, cb2[17]));
  o.o7 = float4(cp.w, dot(wp, cb2[19]), 9.0, 9.0);   // (04/11) zw: raiz en pantalla (9 = sin raiz, las particulas no toman el suelo)
  o.o8 = float4(kind == 1.0 ? 1.0 : 0.0, 0.0, frac(iid * 0.618), 1.0 + kind);   // pelado si es tierra, al azar por particula, banda = tipo (atlas de particulas)
  return o;
}
