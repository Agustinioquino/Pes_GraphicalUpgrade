// PES21 FX (04/11 m, Agustin: "la teselacion si puedes ofrecer opciones por jugador") TESELACION POR JUGADOR: multiplicadores
// propios de cada cara sobre la teselacion general (filas 357 / 358 / 359 / 360). La DLL (facetess.cpp) pone este cbuffer en b10
// del HS y del DS de la cara (y lo copia al GS de las cejas, para que la raiz siga a la piel). Los valores salen de
// PES21FX_hair.ini [teselacion_cara] (o de las filas 371 / 372 mientras se tocan en el menu con el jugador solo en pantalla).
// Sin valores para esa cara la DLL deja b10 vacio: se lee 0 y C.x = 0 -> multiplicadores 1 (igual que todos).
#ifndef PES_FACE_TESS_PLAYER
#define PES_FACE_TESS_PLAYER 1
cbuffer PESTessFace : register(b10)
{
  float4 g_pesTessFaceA;   // x teselar este jugador (1 / 0), y densidad, z suavidad, w tope (multiplicadores)
  float4 g_pesTessFaceB;   // x relieve, y hundir surcos, z levantar formas, w relieve en la luz (multiplicadores)
  float4 g_pesTessFaceC;   // x = 1 si la DLL enlazo valores de esta cara
}
float4 PesTessFaceA() { return g_pesTessFaceC.x > 0.5 ? g_pesTessFaceA : float4(1.0, 1.0, 1.0, 1.0); }
float4 PesTessFaceB() { return g_pesTessFaceC.x > 0.5 ? g_pesTessFaceB : float4(1.0, 1.0, 1.0, 1.0); }
// T = fila 357 (x on, y suavidad, z densidad, w factor maximo), U = fila 358 (x distancia, y transicion, z tope mm, w orientacion)
void PesTessApplyPlayer(inout float4 T, inout float4 U)
{
  float4 a = PesTessFaceA();
  T.x *= a.x > 0.5 ? 1.0 : 0.0;
  T.y *= max(a.z, 0.0);
  T.z *= max(a.y, 0.0);
  U.z *= max(a.w, 0.0);
}
// R = fila 359 (x fuerza, y surcos mm, z formas mm, w mip)
void PesTessApplyRelief(inout float4 R)
{
  float4 b = PesTessFaceB();
  R.x *= max(b.x, 0.0); R.y *= max(b.y, 0.0); R.z *= max(b.z, 0.0);
}
float PesTessReliefLight() { return max(PesTessFaceB().w, 0.0); }
// (07/11, Agustin: "que se mejore el detalle") FORMA DEL PUNTO NUEVO: mode < 0.5 = Phong (cada punto proyectado sobre los planos
// tangentes de los tres vertices: redondea la silueta pero deja la curvatura algo plana); mode >= 0.5 = PN (triangulos curvos de
// Vlachos 2001: superficie cubica que pasa por los vertices con sus normales; nariz, menton y pomulos con una curva mas natural).
// En los dos el punto de una arista depende solo de sus dos extremos (posicion + normal): los triangulos vecinos coinciden.
float3 PesTessSmooth(float3 P0, float3 P1, float3 P2, float3 N0, float3 N1, float3 N2, float3 b, float mode)
{
  float3 pl = b.x * P0 + b.y * P1 + b.z * P2;
  if (mode < 0.5)
    return b.x * (pl - dot(pl - P0, N0) * N0) + b.y * (pl - dot(pl - P1, N1) * N1) + b.z * (pl - dot(pl - P2, N2) * N2);
  float3 b210 = (2.0 * P0 + P1 - dot(P1 - P0, N0) * N0) / 3.0;
  float3 b120 = (2.0 * P1 + P0 - dot(P0 - P1, N1) * N1) / 3.0;
  float3 b021 = (2.0 * P1 + P2 - dot(P2 - P1, N1) * N1) / 3.0;
  float3 b012 = (2.0 * P2 + P1 - dot(P1 - P2, N2) * N2) / 3.0;
  float3 b102 = (2.0 * P2 + P0 - dot(P0 - P2, N2) * N2) / 3.0;
  float3 b201 = (2.0 * P0 + P2 - dot(P2 - P0, N0) * N0) / 3.0;
  float3 E = (b210 + b120 + b021 + b012 + b102 + b201) / 6.0, V = (P0 + P1 + P2) / 3.0;
  float3 b111 = E + (E - V) * 0.5;
  float u = b.x, v = b.y, w = b.z;
  return P0 * (u * u * u) + P1 * (v * v * v) + P2 * (w * w * w)
       + b210 * (3.0 * u * u * v) + b120 * (3.0 * u * v * v) + b201 * (3.0 * u * u * w)
       + b021 * (3.0 * v * v * w) + b102 * (3.0 * u * w * w) + b012 * (3.0 * v * w * w)
       + b111 * (6.0 * u * v * w);
}
#endif
