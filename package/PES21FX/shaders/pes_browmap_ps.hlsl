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
  float sk = 0; float3 skC = 0; float aSk = 1.0;
  [unroll] for (int k = 0; k < 8; k++) {
    float a = k * 0.785398;
    float4 cN4 = tDiffuse.SampleLevel(sLin, uv + float2(cos(a) * 0.034, sin(a) * 0.026), 3.0);
    float3 cN = cN4.rgb;
    float lN = LumF(cN);
    if (lN > sk) { sk = lN; skC = cN; aSk = cN4.a; }   // (08/11 v) y el alfa de la piel   // (08/11 v) tambien el COLOR de la piel de alrededor (para separarlo del pelo)
  }
  sk = max(sk, 0.01);
  float4 t1 = tDiffuse.SampleLevel(sLin, uv, 1.0);
  float l1 = LumF(t1.rgb);
  float mask = smoothstep(0.80, 0.58, l1 / sk) * zone;
  // (08/11 v, Agustin: "tenes la textura y su alfa detecta las partes de pelo") el ALFA de face_bsm_alp marca el pelo pintado: donde
  // el alfa se aparta del de la piel de alrededor, eso es pelo. Si la cara trae ese alfa (difiere en la zona), manda el alfa
  // (contorno exacto, sin confundir piel oscura / sombras pintadas con pelo); si el alfa es parejo, queda la oscuridad como antes
  float aD = abs(t1.a - aSk);
  float aMax = aD;
  [unroll] for (int j = 0; j < 4; j++) {
    float2 o = float2(j < 2 ? (j == 0 ? 0.02 : -0.02) : 0.0, j >= 2 ? (j == 2 ? 0.008 : -0.008) : 0.0);
    aMax = max(aMax, abs(tDiffuse.SampleLevel(sLin, uv + o, 1.0).a - aSk));
  }
  float useA = smoothstep(0.08, 0.2, aMax);   // la cara trae mascara de pelo en el alfa
  // (08/11 w, capturas de Agustin 15:20: pelos fuera de la ceja) el alfa de face_bsm_alp NO es una mascara de pelo fiable (en Messi
  // marcaba mucho mas que la ceja): el contorno vuelve a salir solo de la oscuridad de la pintura
  useA = 0.0;
  // color (04/11 e, Agustin: "el color es lo que falla", salian casi negras): antes pesaba al CUADRADO lo oscuro -> ganaba solo el
  // texel mas negro. Ahora: promedio de los texeles de pelo (peso suave, no al cuadrado) mezclado con el promedio de la zona, y
  // se le devuelve el tono (saturacion x1.35) y algo de luz: el pelo de la ceja es castano / rubio translucido, no negro
  // (08/11 v, Agustin: "no me gusta el color que toma de la textura") el texel de la ceja pintada es una MEZCLA de pelo y piel (la
  // pintura es semitransparente y en mip 0 el pelo no llena el texel): el promedio de antes (+30 % de la zona) le pasaba el rosado /
  // naranja de la piel a las hebras. Ahora se separa: texel = a * pelo + (1 - a) * piel (piel = la mas clara de alrededor, con su
  // color) -> pelo = (texel - (1 - a) * piel) / a, y pesa mas donde el pelo cubre mas (a alto)
  float3 acc = 0; float wsum = 0;
  [unroll] for (int y = -2; y <= 2; y++) [unroll] for (int x = -2; x <= 2; x++) {
    float4 c4 = tDiffuse.SampleLevel(sLin, uv + float2(x, y) * texel * 2.0, 0.0);
    float3 c = c4.rgb;
    float al = saturate((1.0 - LumF(c) / sk) / 0.55);          // cobertura del pelo en ese texel (55 % mas oscuro = todo pelo)
    al = lerp(al, saturate(abs(c4.a - aSk) / 0.6), useA);      // (08/11 v) con mascara en el alfa, la cobertura la da el alfa
    float3 hcK = max((c - (1.0 - al) * skC) / max(al, 0.25), 0.0);
    float w = al * al;
    acc += hcK * w; wsum += w;
  }
  // sin pelo pintado alrededor: castano oscuro sacado de la piel (antes el texel x0.8 = color de piel)
  float3 hc = wsum > 1e-3 ? acc / wsum : skC * float3(0.42, 0.33, 0.27);
  // (08/11 w, Agustin: cejas casi negras) la separacion pelo / piel pesada al cuadrado se quedaba con el trazo mas negro: mitad
  // separacion, mitad el promedio de los texeles de pelo de antes, y nunca por debajo del 35 % del brillo de la piel
  { float3 accP = 0; float wP = 0;
    [unroll] for (int y2 = -2; y2 <= 2; y2++) [unroll] for (int x2 = -2; x2 <= 2; x2++) {
      float3 c = tDiffuse.SampleLevel(sLin, uv + float2(x2, y2) * texel * 2.0, 0.0).rgb;
      float w = smoothstep(0.85, 0.55, LumF(c) / sk); accP += c * w; wP += w; }
    if (wP > 1e-3) hc = lerp(hc, accP / wP, 0.5);
    float hlF = LumF(hc), flo = 0.35 * sk;
    if (hlF < flo) hc = hc * (flo / max(hlF, 1e-3)) * 0.7 + skC * float3(0.36, 0.27, 0.2) * 0.3 * (flo / max(LumF(skC * float3(0.36, 0.27, 0.2)), 1e-3)); }
  float hl = LumF(hc);
  hc = max(hl + (hc - hl) * 0.95, 0.0) * 1.1;   // (08/11 v) sin realce de saturacion: el tono real del pelo ya no trae la piel
  // (08/11 v, Agustin: "el color de las cejas nunca tiene que ser el color de la piel") tope: la ceja queda al menos 45 % mas oscura
  // que la piel de alrededor y, si el tono se parece al de la piel, se lleva hacia el castano del pelo
  { float hl2 = LumF(hc), cap = 0.55 * sk;
    if (hl2 > cap) hc *= cap / max(hl2, 1e-4);
    float3 sN = skC / max(LumF(skC), 1e-4), hN = hc / max(LumF(hc), 1e-4);
    float same = 1.0 - saturate(length(hN - sN) * 4.0);
    hc = lerp(hc, LumF(hc) * float3(1.18, 0.95, 0.78), same * 0.6); }
  o3 = float4(hc, mask);
}
void main(float4 pos : SV_Position, float2 uv : TEXCOORD0, out float4 o0 : SV_Target0)
{
  float tw, th; tDiffuse.GetDimensions(tw, th);
  BrowMap(uv, 1.0 / float2(max(tw, 1), max(th, 1)), o0);
}
