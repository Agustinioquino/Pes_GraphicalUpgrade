// PES21 FX (27/10) - cuello de las caras reales fundido con el torso de TorsoBody (fila 322; brazos.cpp enlaza torso_N en t29).
// Las caras reales traen un pedazo de cuello que el modulo de Sider (TorsoBody_cuello.lua) corta en el borde del torso y cuya
// textura funde con un color FIJO por tono. Ese color no es el que el torso tiene en pantalla (textura propia del usuario,
// tono propio del jugador, "cuerpo del color de la cara"): quedaba una franja de otro color con el borde marcado.
// Aca, en los PS de la cara (5ce3 / ce188), la franja de los ultimos y322 mm del cuello sobre la union toma el color medio del
// propio torso_N justo abajo de la union (adelante el escote, atras la nuca), conservando el detalle de la cara (poros,
// vello: se corrige el promedio, no el texel), y despues lleva el tono del CUERPO del jugador (no el de la cara). Asi el
// cuello termina en el color exacto con el que se dibuja el torso. x322 prendido, y322 alto de la franja (mm), z322 cuanto,
// w322 = 1 pinta la franja de rojo (diagnostico).
// Borde del torso por angulo alrededor del cuello: serie en cos(k * angulo) ajustada al anillo LOOP de TorsoBody_cuello.lua
// 1.14 (48 puntos; error maximo 3 mm): y = 1.53392 - 0.02134 c1 - 0.0075 c2 + 0.00721 c3 - 0.0029 c4.

// peso de la franja (1 en la union, 0 a 'ramp' metros arriba); rest = posicion de reposo (x, y, z) del vertice
float PesCollarW(float3 rest, float ramp) {
  float3 p = float3(abs(rest.x), rest.y, rest.z);
  if (p.y > 1.62 || p.y < 1.45) return 0.0;
  float2 w = float2(p.x, p.z - 0.045); float r = max(length(w), 1e-4);
  if (r > 0.13) return 0.0;
  float c1 = w.y / r;                                   // coseno del angulo alrededor del eje del cuello (1 = adelante)
  float c2 = 2.0 * c1 * c1 - 1.0, c3 = (4.0 * c1 * c1 - 3.0) * c1, c4 = 2.0 * c2 * c2 - 1.0;
  float yb = 1.53392 - 0.02134 * c1 - 0.0075 * c2 + 0.00721 * c3 - 0.0029 * c4;
  float h = p.y - yb;
  return 1.0 - smoothstep(0.0, max(ramp, 0.003), h);
}
// color medio (lineal) del torso de TorsoBody justo abajo de la union, segun el angulo: escote (u 0.25, v 0.10) y nuca
// (u 0.75, v 0.05) del atlas del torso (pants_out_sub: anillo del cuello medido en el modelo), mip de 128 px
float3 PesCollarTorso(Texture2D<float4> torso, SamplerState s, float3 rest) {
  float2 w = float2(abs(rest.x), rest.z - 0.045); float r = max(length(w), 1e-4); float c1 = w.y / r;
  float3 f = torso.SampleLevel(s, float2(0.25, 0.10), 5.0).xyz, b = torso.SampleLevel(s, float2(0.75, 0.05), 5.0).xyz;
  return lerp(b, f, 0.5 + 0.5 * c1);
}
// promedio de la cara en ese punto (mip de 64 px, ya fundido por el modulo de Sider) -> factor por canal para llevarlo al torso
float3 PesCollarRatio(Texture2D<float4> face, SamplerState s, float2 uv, float3 target) {
  uint fw = 0, fh = 0; face.GetDimensions(fw, fh);
  float mip = max(log2(max((float)fw, 64.0)) - 6.0, 0.0);
  float3 mean = face.SampleLevel(s, uv, mip).xyz;
  return clamp(target / max(mean, 0.002), 0.25, 4.0);
}
