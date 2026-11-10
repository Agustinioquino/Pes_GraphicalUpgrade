// PES21 FX (08/11, Agustin: "todo sigue vibrando") GESTOS EN LA PALETA. La DLL ya no escribe los huesos de la cara en el esqueleto del
// juego (el juego los reescribia a veces despues y la paleta del dibujo alternaba entre lo del juego y lo del mod: vibracion). Publica
// por personaje la diferencia nuevo x base^-1 de cada hueso, en el mundo (expr.cpp DeltaUpload, t115), y el VS la aplica a lo que cada
// influencia del skinning ya calculo: el mismo resultado que escribir el hueso, sin pelear con el juego.
// t115: [0] = (personajes, 0, 0, 0), [1] = (reposo de sk_head en el modelo, 0); por personaje 1 + 33 x 6 float4: (cabeza en el mundo,
// 1 + edad del jugador = valida, 09/11 g; tambien sin paleta, con las diferencias en identidad) y por hueso kDef 3 float4 de este cuadro + 3 del anterior (fila k = imagen del eje k, w = traslacion). Sin buffer: todo 0.
#ifndef PES_BONEDELTA_HLSLI
#define PES_BONEDELTA_HLSLI
Buffer<float4> g_pesBoneD : register(t115);
#define PES_BD_BONES 33
#define PES_BD_STRIDE (1 + PES_BD_BONES * 6)
// el personaje cuya cabeza (en el mundo) cae donde esta la de esta paleta (a menos de 50 cm); sin posicion valida, solo si es el unico
int PesBoneChar(float3 hw)
{
  uint n = (uint)(g_pesBoneD[0].x + 0.5);
  if (n == 0 || n > 32) return -1;
  int best = -1, wild = -1; float bd = 0.25;
  for (uint c = 0; c < n; c++) {
    float4 h = g_pesBoneD[2 + c * PES_BD_STRIDE];
    if (h.w < 0.5) { wild = (int)c; continue; }
    float3 d = hw - h.xyz; float l = dot(d, d);
    if (l < bd) { bd = l; best = (int)c; }
  }
  return best >= 0 ? best : (n == 1 ? wild : -1);
}
float3 PesBoneDeltaP(int ci, uint bone, float3 pw, bool prev)
{
  uint b = 2 + (uint)ci * PES_BD_STRIDE + 1 + bone * 6 + (prev ? 3 : 0);
  float4 R0 = g_pesBoneD[b], R1 = g_pesBoneD[b + 1], R2 = g_pesBoneD[b + 2];
  return pw.x * R0.xyz + pw.y * R1.xyz + pw.z * R2.xyz + float3(R0.w, R1.w, R2.w);
}
float3 PesBoneDeltaV(int ci, uint bone, float3 v, bool prev)
{
  uint b = 2 + (uint)ci * PES_BD_STRIDE + 1 + bone * 6 + (prev ? 3 : 0);
  return v.x * g_pesBoneD[b].xyz + v.y * g_pesBoneD[b + 1].xyz + v.z * g_pesBoneD[b + 2].xyz;
}
#endif
