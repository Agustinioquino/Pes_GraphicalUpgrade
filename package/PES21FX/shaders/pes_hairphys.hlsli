// PES21 FX (28/11, Agustin: "revisa las fisicas del pelo ... que no funcionan bien") FISICA DEL PELO CON RESORTE.
// Antes (17/10) la "reaccion del pelo" corria las puntas en contra de lo que la cabeza se movio en el ultimo cuadro: sin memoria
// (al frenar volvia de golpe, sin balanceo), solo con la animacion respecto del cuerpo (correr / girar el cuerpo no contaba), con el
// mismo corrimiento en la posicion anterior (el TAA no se enteraba) y SOLO en el G-buffer: las pasadas de profundidad / sombra
// (81b5930d, f1a25ecd) dibujaban el pelo sin correr.
// Ahora la DLL (expr.cpp HairPhysUpload) simula por personaje un resorte amortiguado de la cabeza EN EL MUNDO (traslacion y giro, con el
// reloj del juego: pausa = quieto, camara lenta = lento) y publica en t113 ("pes_hairphys", reglas de los 4 VS del pelo) cuanto se
// atrasa el pelo: un corrimiento T y un giro M alrededor de la cabeza, del cuadro actual y del anterior. Este archivo lo aplica igual
// en las 4 pasadas del pelo. Incluir DESPUES de cVSObject (g_vsObject) e IniParams.
// t113: [0] = (personajes, 1, 0, 0); por personaje 11 float4: +0 origen del modelo en el mundo (w 1 = valido), +1 cabeza (sk_head) en el
// modelo, +2 T actual (mundo), +3..5 M actual (filas, mundo), +6 T anterior, +7..9 M anterior, +10 cabeza en el modelo del cuadro anterior,
// +11 cabeza en el mundo (para el caso en que el objeto del pelo no comparta el origen del modelo).
// Fila 49: x inercia (1 = natural, mas = mas blando y lento), y limite en cm, z curva raiz -> punta, w on/off.
// Sin la tabla (DLL vieja), sin el personaje o w49 = 0: el pelo queda exacto como el juego.
#ifndef PES_HAIRPHYS_HLSLI
#define PES_HAIRPHYS_HLSLI
Buffer<float4> g_pesHairPhys : register(t113);
#define PES_HP_STRIDE 12

// del mundo al espacio del objeto (m_world = giro * escala pareja), como pes_beard.hlsli
float3 PesHairPhysToObj(float3 dw)
{
  float3 C0 = g_vsObject.m_world._m00_m10_m20, C1 = g_vsObject.m_world._m01_m11_m21, C2 = g_vsObject.m_world._m02_m12_m22;
  float is2 = 1.0 / max(dot(C0, C0), 1e-8);
  return (C0 * dw.x + C1 * dw.y + C2 * dw.z) * is2;
}
float3 PesHairPhysToWorld(float3 vo)
{
  return float3(dot(vo, g_vsObject.m_world._m00_m10_m20), dot(vo, g_vsObject.m_world._m01_m11_m21), dot(vo, g_vsObject.m_world._m02_m12_m22));
}

float3 PesHairPhysToWorldP(float3 po)
{
  float4 p = float4(po, 1.0);
  return float3(dot(p, g_vsObject.m_world._m00_m10_m20_m30), dot(p, g_vsObject.m_world._m01_m11_m21_m31), dot(p, g_vsObject.m_world._m02_m12_m22_m32));
}

// personaje de esta malla: el origen del objeto (m_world) contra el origen publicado de cada personaje (a menos de 30 cm, el mas cerca).
// Sale igual para todos los vertices del dibujo (no depende del vertice): el pelo nunca se reparte entre dos jugadores. Si ningun
// origen coincide (el pelo en otro espacio), la cabeza publicada mas cercana al vertice, a menos de 45 cm.
int PesHairPhysChar(float3 posO)
{
  float4 H = g_pesHairPhys[0];
  uint n = (uint)(H.x + 0.5);
  if (H.y < 0.5 || n == 0 || n > 32) return -1;
  float3 o = float3(g_vsObject.m_world._m30, g_vsObject.m_world._m31, g_vsObject.m_world._m32);
  int best = -1; float bd = 0.09;
  for (uint c = 0; c < n; c++) {
    float4 P = g_pesHairPhys[1 + c * PES_HP_STRIDE];
    if (P.w < 0.5) continue;
    float3 d = o - P.xyz; float l = dot(d, d);
    if (l < bd) { bd = l; best = (int)c; }
  }
  if (best >= 0) return best;
  float3 pw = PesHairPhysToWorldP(posO); bd = 0.2025;
  for (uint k = 0; k < n; k++) {
    float4 P = g_pesHairPhys[1 + k * PES_HP_STRIDE];
    if (P.w < 0.5) continue;
    float3 d = pw - g_pesHairPhys[1 + k * PES_HP_STRIDE + 11].xyz; float l = dot(d, d);
    if (l < bd) { bd = l; best = (int)k; }
  }
  return best;
}

// barba / bigote 3D (mismo VS 2f1af485): no se mueven como el pelo. Zona por la posicion de reposo del vertice (como pes_beard.hlsli:
// ojos a 1.697 m, boca 1.637): delante de la cara y por debajo de los ojos
float PesHairPhysNotBeard(float3 rest)
{
  float front = smoothstep(0.0, 0.03, rest.z) * (1.0 - smoothstep(0.07, 0.09, abs(rest.x)));
  float band = smoothstep(1.52, 1.55, rest.y) * (1.0 - smoothstep(1.675, 1.69, rest.y));
  return 1.0 - front * band;
}

// corrimiento (espacio del objeto) de un punto del pelo: giro M alrededor de la cabeza + T, por el peso del mechon; tope y49 cm;
// lo que entraria en la cabeza (hacia el centro) se saca
float3 PesHairPhysOffset(int c, bool prev, float3 posO, float wgt, float lim)
{
  uint b = 1 + (uint)c * PES_HP_STRIDE;
  float3 hO = g_pesHairPhys[b + (prev ? 10 : 1)].xyz;
  float4 T = g_pesHairPhys[b + (prev ? 6 : 2)];
  float4 M0 = g_pesHairPhys[b + (prev ? 7 : 3)], M1 = g_pesHairPhys[b + (prev ? 8 : 4)], M2 = g_pesHairPhys[b + (prev ? 9 : 5)];
  float3 rw = PesHairPhysToWorld(posO - hO);   // brazo desde la cabeza, en el mundo
  float3 d = float3(dot(M0.xyz, rw), dot(M1.xyz, rw), dot(M2.xyz, rw)) - rw + T.xyz;
  float lr = length(rw);
  if (lr > 1e-4) { float3 rn = rw / lr; float inw = dot(d, rn); if (inw < 0.0) d -= rn * inw * 0.85; }
  d *= wgt;
  float L = length(d);
  if (L > lim) d *= lim / L;
  return PesHairPhysToObj(d);
}

// pasadas con posicion actual y anterior (G-buffer: 2f1af485, db8b4ecb). rest = v0.xyz, w = saturate(v2.x) (peso del mechon)
bool PesHairPhysApply(float3 rest, float w, inout float3 pos, inout float3 prev)
{
  float4 HR = IniParams.Load(int2(49, 0));
  if (HR.w < 0.5 || g_vsObject.m_useWeightCount.x == 0) return false;
  int c = PesHairPhysChar(pos);
  if (c < 0) return false;
  float wgt = pow(saturate(w), max(HR.z, 0.5)) * PesHairPhysNotBeard(rest);
  if (wgt <= 0.0) return true;
  float lim = max(HR.y, 0.1) * 0.01;
  pos += PesHairPhysOffset(c, false, pos, wgt, lim);
  prev += PesHairPhysOffset(c, true, prev, wgt, lim);
  return true;
}
// pasadas de profundidad / sombra (81b5930d, f1a25ecd): solo la posicion actual, el MISMO corrimiento que el G-buffer
void PesHairPhysApplyCur(float3 rest, float w, inout float3 pos)
{
  float4 HR = IniParams.Load(int2(49, 0));
  if (HR.w < 0.5 || g_vsObject.m_useWeightCount.x == 0) return;
  int c = PesHairPhysChar(pos);
  if (c < 0) return;
  float wgt = pow(saturate(w), max(HR.z, 0.5)) * PesHairPhysNotBeard(rest);
  if (wgt <= 0.0) return;
  pos += PesHairPhysOffset(c, false, pos, wgt, max(HR.y, 0.1) * 0.01);
}
#endif
