// PES21 FX (10/11) - SUBSURFACE SCATTERING REAL, version nueva (Agustin: "crealo como una version totalmente nueva como subsurface
// scattering real"; "no me gusta como detecta a la piel, como se lee con la luz, como la luz le afecta, como las sombras le afectan").
// Pasada 2 de ssss.cpp. Es la dispersion en pantalla de Unreal 5 / Disney (Christensen y Burley 2015; Golubev 2018), escrita de cero:
//
//  1) QUE ES PIEL: la marca del G-buffer Y que no sea pelo pintado en la textura (PesSkinK: barba, bigote, cejas), ni el interior de
//     la boca (mascara con profundidad). El pelo no se difunde ni presta luz.
//  2) LA LUZ VIAJA EN 3D, no en la pantalla: cada muestra se pesa con el perfil de difusion evaluado en la DISTANCIA REAL en
//     milimetros entre los dos puntos de la superficie (posiciones reconstruidas con la profundidad). No hay plano ni tolerancias:
//     lo que esta lejos de verdad (otra parte de la cara detras de un borde, el cuello bajo el menton) recibe poco porque el perfil
//     cae con la distancia; de costado (piel rasante) la luz no se estira de mas.
//  3) CUANTO VIAJA CADA COLOR sale del color de la piel en ese punto (camino libre medio / s(albedo)), canal por canal: piel clara
//     difunde mas, piel oscura menos; el rojo llega ~2 veces mas lejos que el verde y ~4 que el azul.
//  4) LA SOMBRA: lo que se difunde es la luz que llega (irradiancia, con la sombra adentro, sin la textura). El borde de una sombra
//     se ablanda y se enrojece solo, del ancho que da la piel, y los poros y arrugas de la textura quedan nitidos (la composicion
//     multiplica por el albedo despues). El brillo (especular) no pasa por aca.
//  5) ENERGIA: los pesos se normalizan por canal: no aclara ni oscurece, solo mueve la luz.
//  Muestreo: 30 muestras por pixel (10 por canal) repartidas segun el propio perfil (importancia), giradas por pixel y, con x331, por
//  cuadro (el TAA las junta). Fila 95: w on, x ancho (1 = real), y fuerza (1 = real). y100 = lo fino difunde algo mas ancho.

// perfil de Burley sin constantes: R(r) = (e^(-r/d) + e^(-r/3d)) / (d r)
float3 PesSssR(float r, float3 d) { r = max(r, 1e-3); return (exp(-r / d) + exp(-r / (3.0 * d))) / (d * r); }
float3 PesSssPos(float2 uv, float z) {
  float2 pp = ((uv * float2(2, -2) + float2(-1, 1)) - m_cameraCenterOffset.xy) * m_projectionParam.xy;
  return float3(pp * z, z);
}
static const float kSssX[7] = { 0.1235, 0.4046, 0.7455, 1.1713, 1.7253, 2.4885, 3.6408 };   // (10/11 c) perfil cortado en 4.5 d (83 % de la energia): la cola lejana casi no pesa y sus lecturas dispersas eran lo caro   // (10/11 b) 7 por canal: en el editor la pasada costaba 3.5 ms con 10   // cuantiles del perfil (r / d)

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float4 P = IniParams.Load(int2(95,0));
  float3 c0 = inLight.SampleLevel(sPoint, uv, 0).xyz;
  if (P.w < 0.5 || !IsSkin(uv)) return float4(c0, 1);
  uint mmW2 = 0, mmH2 = 0; inMouth.GetDimensions(mmW2, mmH2);
  if (PesMouthAt(pos.xy, mmW2, mmH2)) return float4(c0, 1);   // el interior de la boca queda como sin SSS (regla de la boca)
  float skin0 = PesSkinK(uv);
  if (skin0 <= 0.0) return float4(c0, 1);
  float z0 = LinZ(uv);
  float2 rb; { uint lw, lh; inLight.GetDimensions(lw, lh); rb = 1.0 / float2(max(lw, 1u), max(lh, 1u)); }
  float pxm = 1.0 / max(2.0 * abs(m_projectionParam.x) * abs(z0) * m_renderBuffer.z, 1e-5);   // pixeles por metro a esta distancia
  if (P.x >= 2.95) return float4(0, saturate(6.0 * 0.001 * pxm / 40.0), 0, 1);   // diagnostico: por donde pasa
  // alcance por canal desde el albedo (mm)
  float widthScale = (P.x > 0 ? P.x : 1.0);
  uint tq = (uint)(inNrm.SampleLevel(sPoint, uv, 0).z * 1023.0 + 0.5);
  float thinK = smoothstep(0.08, 0.70, (float)(tq >> 4) / 63.0);
  widthScale *= lerp(1.0, lerp(0.9, 1.25, thinK), saturate(IniParams.Load(int2(100,0)).y));
  float3 alb = saturate(inAlb.SampleLevel(sPoint, uv, 0).xyz);
  float3 sA = 1.85 - alb + 7.0 * pow(abs(alb - 0.8), 3.0);
  float3 dmm = (dot(alb, 1.0) > 0.012) ? float3(1.66, 1.17, 0.80) / max(sA, 0.5) : float3(1.5, 0.75, 0.40);
  dmm = max(dmm * widthScale, 0.05);
  float mmToPx = 0.001 * pxm;
  float reach = dmm.r * 3.0 * mmToPx;
  if (reach < 0.6) return float4(c0, 1);   // de lejos el disco no llega a un pixel: lo resuelve la piel pre-integrada de las luces
  float nearK = smoothstep(0.6, 1.6, reach);
  float3 P0 = PesSssPos(uv, z0);
  float rot = 6.2831853 * frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715))) + (IniParams.Load(int2(331, 0)).x > 0.5 ? frac(m_timeParam.z * 7.31) : 0.0));
  // el pixel del centro: una muestra a la distancia de medio pixel (el perfil diverge en 0)
  float rC = 0.35 / max(mmToPx, 1e-4);
  float3 acc = 0, wsum = 0;
  { float3 pdfC = PesSssR(rC, dmm); float3 wC = PesSssR(rC, dmm) / max(dot(pdfC, 1.0 / 3.0), 1e-9); acc = wC * c0; wsum = wC; }
  // (10/11 c) de muy cerca (camara libre con zoom) el disco pasaba de 200 px y cada lectura caia lejos de la anterior (3.5 ms):
  // el disco no pasa de 56 px; mas cerca que eso se encoge todo junto (misma forma)
  float capS = min(1.0, 56.0 / max(3.6408 * dmm.r * mmToPx, 1e-4));
  [unroll] for (int i = 0; i < 21; i++) {
    int ch = i % 3;
    float dch = ch == 0 ? dmm.r : (ch == 1 ? dmm.g : dmm.b);
    float rmm = kSssX[i / 3] * dch * capS;
    float a = rot + i * 2.3999632;
    float2 uvS = uv + float2(cos(a), sin(a)) * (rmm * mmToPx) * rb;
    if (any(uvS < 0.0) || any(uvS > 1.0) || !IsSkin(uvS)) continue;
    float skS = PesSkinK(uvS);
    if (skS <= 0.0) continue;
    float dRaw = inDepth.SampleLevel(sPoint, uvS, 0).x;   // una sola lectura de profundidad: posicion y prueba de boca visible
    if (mmW2 > 0) { float mz = inMouth.Load(int3(min((uint2)(uvS / rb), uint2(mmW2 - 1, mmH2 - 1)), 0)); if (mz > 0.0 && abs(mz - dRaw) <= 1e-6 + 2e-4 * abs(dRaw)) continue; }
    float3 Ps = PesSssPos(uvS, m_projectionParam.z / (dRaw - m_projectionParam.w));
    float r3 = max(length(Ps - P0) * 1000.0, rmm);          // distancia real entre los dos puntos de la piel, mm
    float pdf = dot(PesSssR(rmm / capS, dmm), 1.0 / 3.0);          // con que densidad se eligio este punto (mezcla de los tres canales)
    float3 w = PesSssR(r3, dmm) / max(pdf, 1e-9) * skS;
    acc += w * inLight.SampleLevel(sPoint, uvS, 0).xyz;
    wsum += w;
  }
  float3 sss = acc / max(wsum, 1e-6);
  float k = saturate(P.y) * nearK * skin0;
  float3 o = lerp(c0, sss, k);
  if (any(isnan(o)) || any(isinf(o))) o = c0;
  return float4(o, 1);
}
