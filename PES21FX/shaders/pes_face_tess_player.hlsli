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
#endif
