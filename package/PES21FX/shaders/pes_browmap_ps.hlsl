// PES21 FX (04/11 d) - MAPA DE LA CEJA por cara, a la mitad de la resolucion de la textura de la cara (1024 con caras de 2048).
// Lo dibuja la DLL una vez por cara (facegen.cpp, junto con la quinta textura) y lo leen las hebras de ceja (pes_brow_gs, t27).
Texture2D<float4> tDiffuse : register(t0);
SamplerState sLin : register(s0);
float Ell(float2 uv, float2 c, float2 r, float soft) {
  float d = length((uv - c) / r);
  return saturate(1.0 - (d - (1.0 - soft)) / soft);
}
// Salida: MAPA DE LA CEJA (Agustin: "aparecen pelos fuera de donde deberian" y
// "falta el color de las cejas correcto"). A = contorno de la ceja pintada (0..1): oscuridad del texel (mip 1) contra la piel de
// alrededor (la mas clara de 8 puntos a ~1.2 cm, mip 3), solo en la franja de las cejas y sin el pliegue del parpado.
// RGB = color de los pelos pintados: promedio de los texeles de pelo de 5x5 alrededor (mip 0) pesado por lo oscuros que son.
float LumF(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
void BrowMap(float2 uv, float2 texel, out float4 o3) {
  o3 = float4(0, 0, 0, 0);
  // franja de las cejas: generosa (el contorno real lo da la pintura); nada por debajo de v = 0.36 (parpado)
  float zone = max(Ell(uv, float2(0.395, 0.312), float2(0.13, 0.05), 0.35), Ell(uv, float2(0.605, 0.312), float2(0.13, 0.05), 0.35));
  zone *= smoothstep(0.365, 0.345, uv.y);
  if (zone < 0.01) return;
  float sk = 0;
  [unroll] for (int k = 0; k < 8; k++) {
    float a = k * 0.785398;
    sk = max(sk, LumF(tDiffuse.SampleLevel(sLin, uv + float2(cos(a) * 0.034, sin(a) * 0.026), 3.0).rgb));
  }
  sk = max(sk, 0.01);
  float l1 = LumF(tDiffuse.SampleLevel(sLin, uv, 1.0).rgb);
  float mask = smoothstep(0.80, 0.58, l1 / sk) * zone;
  // color (04/11 e, Agustin: "el color es lo que falla", salian casi negras): antes pesaba al CUADRADO lo oscuro -> ganaba solo el
  // texel mas negro. Ahora: promedio de los texeles de pelo (peso suave, no al cuadrado) mezclado con el promedio de la zona, y
  // se le devuelve el tono (saturacion x1.35) y algo de luz: el pelo de la ceja es castano / rubio translucido, no negro
  float3 acc = 0, accAll = 0; float wsum = 0;
  [unroll] for (int y = -2; y <= 2; y++) [unroll] for (int x = -2; x <= 2; x++) {
    float3 c = tDiffuse.SampleLevel(sLin, uv + float2(x, y) * texel * 2.0, 0.0).rgb;
    float w = smoothstep(0.85, 0.55, LumF(c) / sk);
    acc += c * w; wsum += w; accAll += c;
  }
  float3 hc = wsum > 1e-3 ? acc / wsum : tDiffuse.SampleLevel(sLin, uv, 0.0).rgb * 0.8;
  hc = lerp(hc, accAll / 25.0, 0.3);
  float hl = LumF(hc);
  hc = max(hl + (hc - hl) * 1.1, 0.0) * 1.15;   // (05/11) brillo de vuelta a x1.15 (se veian muy oscuras de frente)   // (04/11 o) antes x1.35 / x1.15: tono demasiado naranja
  o3 = float4(hc, mask);
}
void main(float4 pos : SV_Position, float2 uv : TEXCOORD0, out float4 o0 : SV_Target0)
{
  float tw, th; tDiffuse.GetDimensions(tw, th);
  BrowMap(uv, 1.0 / float2(max(tw, 1), max(th, 1)), o0);
}
