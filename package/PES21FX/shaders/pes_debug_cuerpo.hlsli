// PES21 FX (16/11) - vistas de diagnostico del cuerpo sin camiseta (fila 519, nunca se guarda: kDebugParams).
// Agustin: "una vista debug para el torso para ver a traves, para ver costuras" + "y las texturas?".
//  x519 = 1: el torso se ve a traves (se saltean y519 de cada 4 pixeles en un patron fijo de 2x2; el TAA lo promedia y queda
//            como un torso translucido): se ve lo que queda debajo (el cuello entero de la cara, el principio del brazo).
//  z519 = 1: cuadricula de costuras. Cada pieza de un color (torso azul, brazo verde, mano / pierna amarillo, cara rojo) con una
//            cuadricula de 32 x 32 en la UV de su textura: donde la cuadricula se corta o cambia de tamano hay una costura de textura.
//  z519 = 2: solo la textura. El color de la textura tal cual se lee, sin los retoques de tono del mod (con la luz del juego).
// Lo usan el PS de la piel del cuerpo (aa7d) y el de la cara (5ce3 / ce188). Ningun otro dibujo lo lee.

float4 PesDbgCuerpo() { return IniParams.Load(int2(519, 0)); }

// true = este pixel del torso se saltea (x519 = 1); px = SV_Position.xy
bool PesDbgSeeThrough(float4 D, float2 px) {
  if (D.x < 0.5) return false;
  uint2 q = (uint2)px & 1u;
  float t = (q.x == 0 ? (q.y == 0 ? 0.125 : 0.625) : (q.y == 0 ? 0.875 : 0.375));   // Bayer 2x2
  return t < clamp(D.y > 0.0 ? D.y : 0.5, 0.25, 0.75);
}

// color de la vista z519 (o el que habia si esta apagada); pieza: 0 torso, 1 brazo, 2 mano / pierna, 3 cara
float3 PesDbgColor(float4 D, float3 col, float3 tex, float2 uv, int pieza) {
  int m = (int)(D.z + 0.5);
  if (m == 2) return tex;
  if (m != 1) return col;
  float3 c = pieza == 0 ? float3(0.15, 0.40, 1.0) : pieza == 1 ? float3(0.15, 0.85, 0.25) : pieza == 2 ? float3(0.95, 0.80, 0.10) : float3(0.95, 0.15, 0.10);
  float2 g = frac(uv * 32.0);
  int2 k = (int2)floor(uv * 32.0);
  float ch = ((k.x + k.y) & 1) ? 1.0 : 0.45;
  float2 e = min(g, 1.0 - g) / max(fwidth(uv * 32.0), 1e-4);   // linea fina en el borde de cada celda
  float ln = 1.0 - saturate(min(e.x, e.y) - 0.5);
  return lerp(c * ch, float3(1.0, 1.0, 1.0), ln * 0.85);
}
