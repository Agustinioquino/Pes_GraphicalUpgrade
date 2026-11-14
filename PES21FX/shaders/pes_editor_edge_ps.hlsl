// PES21 FX - (10/11 m042wp) borde del jugador en el visor del editor con el fondo propio (fila 424).
// Agustin: con el fondo negro (x424 = 1) se ve un contorno gris / blanco alrededor del jugador (puntas del pelo, oreja).
// La imagen del jugador (render target del tamano del visor, t8) se dibuja con el PS comun del menu 55a003da encima del
// fondo. Afuera del jugador esa imagen tiene alfa 0 y el color con el que el juego la limpio (gris); en los pixeles del
// borde (pelo fino, suavizado) el color quedo mezclado con ese gris. Con el fondo azul del juego no se notaba; con negro si.
// La DLL (glass.cpp EditorEdgePS) pone este PS en lugar de 55a003da solo para ese dibujo: hace lo mismo que el original
// (t8 x COLOR0 x g_mulColor + g_addColor, alpha test, salida x 0.25) pero antes le resta al borde el color de la limpieza
// (leido en una esquina de arriba de la imagen, donde no hay jugador). Nunca aclara: el pixel queda igual o mas oscuro.
// Solo cambia la imagen del jugador ya terminada: no toca la boca ni ningun shader de la escena.
//   424: x fondo (0 el del juego: este PS no se usa) | z 1 = borde como el juego (sin limpiar)
Texture2D<float4> inTexture : register(t8);
SamplerState g_sampler : register(s0);

float4 main(float3 uv : TEXCOORD0, float4 mulc : TEXCOORD1, float4 addc : TEXCOORD2, float4 col : COLOR0, float4 pos : SV_Position) : SV_Target0 {
  float4 t = inTexture.Sample(g_sampler, uv.xy);
  uint W, H; inTexture.GetDimensions(W, H);
  // color de la limpieza: esquinas de arriba (los hombros pueden llegar a las de abajo); solo si ahi el alfa es ~0
  float4 cL = inTexture.Load(int3(2, 2, 0)), cR = inTexture.Load(int3(max((int)W - 3, 0), 2, 0));
  float3 C = 0; float nC = 0;
  if (cL.a < 0.02) { C += cL.rgb; nC += 1; }
  if (cR.a < 0.02) { C += cR.rgb; nC += 1; }
  C = nC > 0 ? C / nC : 0;
  // cobertura del pixel: su alfa y, si los vecinos son fondo, menos (el suavizado pudo mezclar el color dejando alfa 1)
  float2 px = 1.0 / float2(max(W, 1u), max(H, 1u));
  float aN = 0.25 * (inTexture.SampleLevel(g_sampler, uv.xy + float2(px.x, 0), 0).a + inTexture.SampleLevel(g_sampler, uv.xy - float2(px.x, 0), 0).a
                   + inTexture.SampleLevel(g_sampler, uv.xy + float2(0, px.y), 0).a + inTexture.SampleLevel(g_sampler, uv.xy - float2(0, px.y), 0).a);
  float a = saturate(min(t.a, 0.5 * (t.a + aN)));
  t.rgb = max(t.rgb - C * (1.0 - a), 0.0);

  float4 c = t * col;
  c = c * mulc + addc;
  if (c.a - uv.z < 0) discard;
  return c * float4(0.25, 0.25, 0.25, 1.0);
}
