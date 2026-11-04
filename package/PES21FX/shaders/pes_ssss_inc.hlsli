// PES21 FX - dispersion subsuperficial en pantalla con el perfil de Burley ("normalized diffusion", Christensen y Burley 2015,
// el de Unreal 5 / Disney). (27/10) Reemplaza a la version separable de d'Eon con sus tres parches de color: un disco 2D de 16
// muestras sobre la irradiancia difusa (lo que la pasada de luz escribio), SOLO en pixeles marcados como piel (material.z =
// 250/255). Las muestras salen por muestreo de importancia del propio perfil (mitad con el rojo, mitad con el verde, mezcla
// MIS), rotadas por pixel y por cuadro (el TAA del juego limpia el ruido). Solo corta la difusion lo que no es piel y lo que
// se aparta del plano de la superficie (no la normal: con poros y arrugas el corte por normal dejaba la piel manchada).
// El ancho en pantalla sale de la profundidad real (mm -> pixeles).
// (27/10 b) Dos pasadas: la PRIMERA (pes_ssss_h_ps, SSSS_PRE) copia la luz y, donde no corre el sol del partido (editor, menus
// 3D), suma la LUZ DE PRUEBA del editor (fila 297 / 298: un sol con direccion, color, sombra trazada en pantalla y brillo, para
// mirar las caras con luz de dia o de noche); la SEGUNDA (pes_ssss_v_ps) hace el disco de Burley sobre el resultado.
//   t0 irradiancia (entrada), t1 profundidad, t2 G-buffer material, t3 G-buffer normal, t4 albedo, t5 brillo (t10 del juego),
//   t120 parametros. x95 ancho (1 = escala real en mm), y95 fuerza, z95 corte por profundidad, w95 on, y100 guiada por el trm
cbuffer cPSScene : register(b2) {
  float4x4 m_projectionView; float4x4 m_projection; float4x4 m_view; float4x4 m_shadowProjection; float4x4 m_shadowProjection2;
  float4 m_eyepos; float4 m_projectionParam; float4 m_viewportSize; float4 m_exposure; float4 m_fogParam[3]; float4 m_fogColor;
  float4 m_cameraCenterOffset; float4 m_shadowMapResolutions;
};
cbuffer cPSSystem : register(b0) { float4 m_param; float4 m_renderInfo; float4 m_renderBuffer; float4 m_dominantLightDir; float4 m_timeParam; };
Texture2D<float4> inLight : register(t0);
Texture2D<float4> inDepth : register(t1);
Texture2D<float4> inMat   : register(t2);
Texture2D<float4> inNrm   : register(t3);
Texture2D<float4> inAlb   : register(t4);   // (20/10) albedo del G-buffer: tono de la piel (melanina)
Texture2D<float4> inSpec  : register(t5);   // (27/10 b) luz especular del juego (t10): la luz de prueba le suma su brillo
Texture2D<float4> inRef   : register(t6);   // (01/11) referencia de intensidad de la luz de prueba (1x1, suavizada)
Texture1D<float4> IniParams : register(t120);
SamplerState sPoint : register(s0);
SamplerState sLinear : register(s1);

float LinZ(float2 uv) { return m_projectionParam.z / (inDepth.SampleLevel(sPoint, uv, 0).x - m_projectionParam.w); }
bool IsSkin(float2 uv) { return abs(inMat.SampleLevel(sPoint, uv, 0).z - 0.98039) < 0.003; }
float Lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// radios del perfil de Burley en unidades de d: inversa de la CDF 1 - e^-x/4 - 3e^(-x/3)/4 en u = (i + 0.5) / 8
static const float kBurleyX[8] = { 0.1305, 0.4299, 0.7976, 1.2651, 1.8883, 2.7792, 4.2184, 7.4616 };

float BurleyF(float x) { return exp(-x) + exp(-x / 3.0); }

#ifdef SSSS_REF
// ===================================================================================================================
// (01/11) Pasada 0 de la luz de prueba del editor: referencia de intensidad (la irradiancia de estudio mas alta sobre la piel,
// rejilla de 8x8) en una textura de 1x1 que la DLL mezcla con la del cuadro anterior (blend factor ~0.1). Antes se media en
// cada pixel cada cuadro con 6x6 puntos fijos de la pantalla: al mover la camara los puntos caian o no sobre la piel y toda la
// luz de prueba saltaba de intensidad (Agustin: "parpadea mucho al mover la camara").
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float S = 0, Sall = 0;
  [unroll] for (int gj = 0; gj < 8; gj++) [unroll] for (int gi = 0; gi < 8; gi++) {
    float2 ug = float2((gi + 0.5) / 8.0, (gj + 0.5) / 8.0);
    float l = Lum(inLight.SampleLevel(sPoint, ug, 0).xyz);
    Sall = max(Sall, l); if (IsSkin(ug)) S = max(S, l);
  }
  if (S <= 1e-6) S = Sall;
  return float4(S, Sall, 0, 1);
}
#elif defined(SSSS_PRE)
// ===================================================================================================================
// Pasada 1: copia + luz de prueba del editor (27/10 b)
// En el editor y los menus 3D el juego no corre la pasada del sol: ilumina con un mapa esferico de estudio, plano y de
// frente, y no se puede juzgar la piel (ni el SSS, ni los poros, ni las sombras). Aca se suma un sol de prueba sobre la
// irradiancia (t9) y el brillo (t10) con los mismos datos que usa la pasada de luz real (normal, rugosidad y translucidez
// del G-buffer), una sombra trazada en pantalla (la nariz sobre la mejilla, el menton sobre el cuello) y se atenua la luz
// de estudio para que la sombra se vea. Solo donde no hubo sol hace poco (w44 = 0) y en escena de menu sin cancha (w255 = 1).
//   x297 prendido | y297 preset (0 manual, 1 dia, 2 tarde, 3 noche, 4 estudio, 5 nublado, 6 lluvia, 7 mediodia, 8 contraluz, 9 noche 2 torres) | z297 rumbo (grados; 0 = desde la camara, 90 = derecha,
//   180 = contraluz) | w297 altura (grados) | x298 intensidad (veces la luz de estudio sobre la piel) | y298 calidez (0 azul
//   .. 1 naranja) | z298 luz de estudio que queda | w298 sombra trazada (0 = sin sombra)
//   x319 icono de las luces en pantalla (DLL, overlay.cpp) | y319 relleno del estudio | z319 recorte del estudio
struct PreOut { float4 c : SV_Target0; float4 s : SV_Target1; };

float3 DecodeN(float2 uv) {   // normal del G-buffer (octaedro 10/10 + 2 bits), igual que en la pasada de luz 1f623
  float4 nb = inNrm.SampleLevel(sPoint, uv, 0);
  uint3 q = (uint3)(float3(1023, 1023, 3) * nb.xyw);
  float2 e = float2((q.x << 1) | (q.z >> 1), (q.y << 1) | (q.z & 1)) * 0.00097703957 - 1;
  float3 n = float3(e.xy, 1 - abs(e.x) - abs(e.y));
  if (n.z < 0) n.xy = (1 - abs(e.yx)) * (e.xy >= 0 ? 1 : -1);
  n = normalize(n); n.z = -n.z;
  return n;
}

// sombra trazada en pantalla (12 pasos hacia la luz; la de contacto de la pasada de luz, sin shadow map)
float PesTraceShadow(float3 Pv, float3 L, float rawNL, float zC, float zSgn, float zA, float2 uv, float2 pxy, float shK) {
  if (shK <= 0.001 || rawNL <= 0.02) return 1.0;
  float ign = frac(52.9829189 * frac(dot(pxy, float2(0.06711056, 0.00583715))));
  float csLen = 0.45;
  float bias = (0.008 + 0.0018 * zA) * (0.6 + 0.4 / max(rawNL, 0.12));
  float3 Q = Pv + L * bias;
  float occ = 0;
  [loop] for (int ci = 0; ci < 12; ci++) {
    float f = (ci + ign) / 12.0;
    float t = f * f * csLen;
    float3 R = Q + L * t;
    float RzA = R.z * zSgn;
    if (RzA < 0.1) break;
    float2 s = R.xy / (R.z * m_projectionParam.xy) + m_cameraCenterOffset.xy;
    float2 uvR = s * float2(0.5, -0.5) + 0.5;
    if (any(uvR < 0.0) || any(uvR > 1.0)) break;
    float zS = LinZ(uvR);
    float dz = (R.z - zS) * zSgn;
    float thick = 0.10 + 0.25 * t + 0.0015 * RzA;
    if (dz > 0.003 + 0.0008 * RzA && dz < thick && (zC - zS) * zSgn > 0.03 + 0.002 * zA) {
      occ = (1.0 - 0.5 * f) * saturate(dz / (0.35 * thick));
      break;
    }
  }
  float2 eM = saturate(min(uv, 1.0 - uv) * 10.0);
  return 1.0 - saturate(occ * saturate(shK) * eM.x * eM.y);
}

float3 PesTint(float warm) {   // azul frio .. naranja, luminancia 1
  float3 t = lerp(float3(0.80, 0.88, 1.00), float3(1.00, 0.62, 0.32), saturate(warm));
  return t / max(Lum(t), 1e-4);
}

float3 PesDirL(float az, float el, float zSgn) {   // rumbo 0 = de frente (hacia la camara), 90 = desde la derecha
  float ca = cos(radians(az)), sa = sin(radians(az)), ce = cos(radians(el)), se = sin(radians(el));
  return normalize(float3(ce * sa, se, -ce * ca * zSgn));
}

// una luz direccional: difusa (Lambert + borde rojizo de la piel + contraluz por la translucidez) y brillo (GGX + lobulo fino)
void PesOneLight(float3 L, float3 col, float sh, float3 n, float3 V, bool skin, float rough, float trans, inout float3 dif, inout float3 spec) {
  float rawNL = dot(n, L);
  // --- difusa: Lambert con un borde apenas envuelto en la piel (el resto lo hace el disco de Burley de la pasada 2) ---
  float wrap = skin ? 0.03 : 0.0;
  float nl = saturate((rawNL + wrap) / (1.0 + wrap));
  dif += col * nl * sh;
  // (27/10 e) borde de la sombra rojizo (envoltura por canal), solo en la franja del terminador
  if (skin) {
    float3 wr = float3(0.30, 0.10, 0.05);
    float3 nl3 = saturate((rawNL + wr) / (1.0 + wr));
    dif += col * max(nl3 - nl, 0.0) * 0.8 * lerp(1.0, sh, 0.6);
    // contraluz: orejas y aletas de la nariz dejan pasar luz (translucidez del G-buffer), como en la pasada de luz
    float3 Lt = normalize(L + n * 0.3);
    float tv = pow(saturate(dot(V, -Lt)), 3.0);
    float thick = saturate(trans * 1.6);
    float x5 = IniParams.Load(int2(5, 0)).x;
    dif += col * thick * smoothstep(0.2, 0.7, thick) * tv * float3(1.0, 0.24, 0.10) * (x5 > 0 ? x5 : 1.0) * sqrt(sh);
  }
  // --- brillo: GGX con la rugosidad del G-buffer y, en la piel, el lobulo fino (grasa / sudor) ---
  float3 H = normalize(L + V);
  float nh = saturate(dot(n, H)), nv = saturate(dot(n, V)), vh = saturate(dot(V, H));
  float a2 = rough * rough; a2 *= a2;
  float dd = nh * nh * (a2 - 1) + 1;
  float D = a2 / (3.14159 * dd * dd);
  float F0 = skin ? 0.028 : 0.04;
  float F = F0 + (1 - F0) * pow(1 - vh, 5);
  float k = (rough + 1) * (rough + 1) / 8.0;
  float G = (nv / (nv * (1 - k) + k)) * (nl / (nl * (1 - k) + k));
  spec += col * (D * F * G / max(4.0 * nv * nl, 1e-3)) * nl * sh;
  if (skin) {
    float a2s = 0.18 * 0.18; a2s *= a2s;
    float dds = nh * nh * (a2s - 1) + 1;
    float Ds = a2s / (3.14159 * dds * dds);
    float y5 = IniParams.Load(int2(5, 0)).y;
    spec += col * Ds * F * nl * 0.25 * (y5 > 0 ? y5 : 1.0) * sh;
  }
}

PreOut main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
  PreOut o;
  float3 c0 = inLight.SampleLevel(sPoint, uv, 0).xyz;
  float3 s0 = inSpec.SampleLevel(sPoint, uv, 0).xyz;
  o.c = float4(c0, 1); o.s = float4(s0, 1);
  float4 E = IniParams.Load(int2(297, 0)), E2 = IniParams.Load(int2(298, 0)), E3 = IniParams.Load(int2(319, 0));
  bool noSun = IniParams.Load(int2(44, 0)).w < 0.5 && IniParams.Load(int2(255, 0)).w > 0.5;
  if (E.x < 0.5 || !noSun) return o;
  float zC = LinZ(uv);
  float zSgn = zC < 0.0 ? -1.0 : 1.0, zA = abs(zC);
  if (zA < 0.1 || zA > 60.0) return o;   // fondo del editor, sin geometria
  // --- preset o manual ---
  // (01/11) 4 = estudio: luz principal con los sliders manuales (rumbo, altura, intensidad, calidez, sombra) + relleno del
  // lado opuesto (y319, sin sombra, apenas frio) + recorte desde atras (z319, contorno del pelo, orejas y mandibula), sin
  // relleno de cielo ni rebote del suelo: la luz la controla uno, como en una sesion de fotos de piel
  int preset = (int)(E.y + 0.5);
  float az = E.z, el = E.w, inten = E2.x > 0 ? E2.x : 1.1, warm = E2.y, keep = E2.z, shK = E2.w;
  if (preset == 1) { az = 50; el = 45; inten = 1.1; warm = 0.62; keep = 0.55; }   // (27/10 c) 2.5 quemaba la cara: el tonemap del editor expone para la luz de estudio        // dia: sol alto, calido
  else if (preset == 2) { az = 70; el = 12; inten = 0.9; warm = 0.95; keep = 0.45; }   // tarde: sol bajo, naranja
  else if (preset == 3) { az = -40; el = 60; inten = 0.8; warm = 0.15; keep = 0.35; }  // noche: torre alta, blanca azulada
  // (04/11) presets de clima para comparar con capturas (pedido de Agustin). skyK / gndK = relleno de cielo y suelo (x sol),
  // shMul = cuanto oscurece la sombra trazada, specMul = brillo (piel mojada con lluvia), tower2 = segunda torre con sombra
  float skyK = 0.16, gndK = 0.05, shMul = 1.0, specMul = 1.0; bool tower2 = false;
  if (preset == 5)      { az = 20;  el = 70; inten = 0.28; warm = 0.45; keep = 0.75; skyK = 0.70; gndK = 0.08; shMul = 0.15; }   // nublado: cielo cubierto, luz de arriba casi sin sombra (04/11 b: mas plana, en la captura se parecia al dia)
  else if (preset == 6) { az = 20;  el = 70; inten = 0.20; warm = 0.30; keep = 0.62; skyK = 0.60; gndK = 0.04; shMul = 0.10; specMul = 2.5; }   // lluvia: cubierto, frio, piel mojada
  else if (preset == 7) { az = 30;  el = 78; inten = 1.25; warm = 0.58; keep = 0.50; }   // mediodia: sol casi vertical (cuencas de los ojos oscuras)
  else if (preset == 8) { az = 165; el = 10; inten = 1.00; warm = 0.95; keep = 0.45; }   // atardecer a contraluz: sol bajo detras del jugador
  else if (preset == 9) { az = -45; el = 35; inten = 0.70; warm = 0.15; keep = 0.35; tower2 = true; }   // noche con dos torres opuestas (dos sombras)
  shK *= shMul;
  bool studio = preset == 4;
  // --- referencia de intensidad: la irradiancia de estudio mas alta sobre la piel, suavizada en el tiempo (pasada 0, t6) ---
  float S = inRef.Load(int3(0, 0, 0)).x;
  if (S <= 1e-6) return o;
  float3 sunC = PesTint(warm) * (S * inten);
  // --- geometria en vista, como la pasada de luz 1f623 (posicion desde la profundidad, normal del G-buffer) ---
  float2 sC = uv * float2(2, -2) + float2(-1, 1);
  float2 pp = (sC - m_cameraCenterOffset.xy) * m_projectionParam.xy;
  float3 Pv = float3(pp * zC, zC);
  float3 V = -normalize(float3(pp, 1));
  float3 n = DecodeN(uv);
  float3 L = PesDirL(az, el, zSgn);
  bool skin = IsSkin(uv);
  float4 mat = inMat.SampleLevel(sPoint, uv, 0);
  float rough = clamp(mat.x, 0.08, 1.0);
  uint tq = (uint)(inNrm.SampleLevel(sPoint, uv, 0).z * 1023.0 + 0.5);
  float trans = (float)(tq >> 4) / 63.0;
  float3 dif = 0, spec = 0;
  float sh = PesTraceShadow(Pv, L, dot(n, L), zC, zSgn, zA, uv, pos.xy, shK);
  PesOneLight(L, sunC, sh, n, V, skin, rough, trans, dif, spec);
  if (studio) {
    float fillK = E3.y, rimK = E3.z;
    if (fillK > 0.001) {   // relleno: del otro lado de la camara, bajo, un poco frio, sin sombra
      float azF = (az >= 0 ? -1.0 : 1.0) * clamp(90.0 - abs(az) * 0.5, 25.0, 70.0);
      PesOneLight(PesDirL(azF, 8.0, zSgn), PesTint(saturate(warm - 0.2)) * (S * inten * fillK), 1.0, n, V, skin, rough, trans, dif, spec);
    }
    if (rimK > 0.001) {   // recorte: desde atras, del mismo lado que la principal, alto
      float azR = (az >= 0 ? 1.0 : -1.0) * 150.0;
      float3 LR = PesDirL(azR, 35.0, zSgn);
      float shR = PesTraceShadow(Pv, LR, dot(n, LR), zC, zSgn, zA, uv, pos.xy + 17.0, shK);
      PesOneLight(LR, PesTint(0.45) * (S * inten * rimK), shR, n, V, skin, rough, trans, dif, spec);
    }
  } else {
    if (tower2) {   // (04/11) segunda torre del lado opuesto, un poco mas debil, con su propia sombra
      float3 L2 = PesDirL(az + 180.0, el, zSgn);
      float sh2 = PesTraceShadow(Pv, L2, dot(n, L2), zC, zSgn, zA, uv, pos.xy + 29.0, shK);
      PesOneLight(L2, sunC * 0.7, sh2, n, V, skin, rough, trans, dif, spec);
    }
    // (27/10 g) relleno de cielo: el lado en sombra no queda gris plano; luz difusa del cielo (azulada, de arriba) y un poco de
    // rebote del suelo (calido, de abajo), proporcional al sol. El cielo es mas azul cuanto mas calido (bajo) esta el sol.
    float3 skyC = lerp(float3(0.55, 0.70, 1.00), float3(0.35, 0.50, 1.00), saturate(warm)) * (S * inten) * skyK;
    float3 gndC = float3(1.00, 0.72, 0.50) * (S * inten) * gndK;
    if (preset == 5 || preset == 6) skyC = float3(0.80, 0.85, 0.95) * (S * inten) * skyK;   // cielo cubierto: gris, no azul
    float up = n.y * 0.5 + 0.5;   // normal en vista: y arriba
    dif += skyC * up + gndC * (1.0 - up);
  }
  o.c = float4(c0 * saturate(keep) + dif, 1);
  o.s = float4(s0 * saturate(keep) + spec * specMul, 1);
  return o;
}
#else
// ===================================================================================================================
// Pasada 2: disco de Burley
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float4 P = IniParams.Load(int2(95,0));
  float3 c0 = inLight.SampleLevel(sPoint, uv, 0).xyz;
  if (P.w < 0.5 || !IsSkin(uv)) return float4(c0, 1);
  float z0 = LinZ(uv);
  float2 rb = abs(float2(ddx(uv.x), ddy(uv.y)));   // tamano de un pixel en uv
  float pxm = 1.0 / max(2.0 * abs(m_projectionParam.x) * z0 * m_renderBuffer.z, 1e-5);   // pixeles por metro (como la composicion)
  // diagnostico: x95 >= 2.95 pinta la piel: verde = pasa por aca, con el ancho en pixeles como brillo
  if (P.x >= 2.95) return float4(0, saturate(6.0 * 0.001 * pxm / 40.0), 0, 1);
  float widthScale = (P.x > 0 ? P.x : 1.0);
  // trm (translucidez del juego, 6 bits en el G-buffer): lo fino (orejas, aletas, labios) difunde algo mas ancho, lo grueso algo menos
  float4 P2 = IniParams.Load(int2(100,0));
  uint tq = (uint)(inNrm.SampleLevel(sPoint, uv, 0).z * 1023.0 + 0.5);
  float thinK = smoothstep(0.08, 0.70, (float)(tq >> 4) / 63.0);
  widthScale *= lerp(1.0, lerp(0.85, 1.2, thinK), saturate(P2.y));
  // perfil por tono de piel (melanina del albedo): en piel clara el rojo viaja ~2.5 veces mas que el verde (penumbra rojiza);
  // con mas melanina la epidermis absorbe y el rojo viaja menos (penumbra marron calida, no roja). Distancias d en mm.
  float alb0L = dot(inAlb.SampleLevel(sPoint, uv, 0).xyz, float3(0.2126, 0.7152, 0.0722));
  float mel = (alb0L > 0.004) ? saturate(log2(0.47 / max(alb0L, 1e-3)) / 2.85) : 0.0;
  float3 dmm = float3(lerp(1.0, 0.62, mel), lerp(0.42, 0.36, mel), lerp(0.26, 0.24, mel)) * widthScale;
  float mmToPx = 0.001 * pxm;
  if (dmm.r * 3.0 * mmToPx < 0.6) return float4(c0, 1);   // de lejos lo resuelve la piel pre-integrada de la pasada de luz
  // plano de la superficie: pendiente de la profundidad por pixel (del lado mas suave, para no tomar un borde)
  float zxp = LinZ(uv + float2(rb.x, 0)) - z0, zxn = z0 - LinZ(uv - float2(rb.x, 0));
  float zyp = LinZ(uv + float2(0, rb.y)) - z0, zyn = z0 - LinZ(uv - float2(0, rb.y));
  float2 slope = float2(abs(zxp) < abs(zxn) ? zxp : zxn, abs(zyp) < abs(zyn) ? zyp : zyn);
  float tolK = P.z > 0.0 ? P.z : 1.0;
  // rotacion por pixel y por cuadro (ruido de gradiente intercalado, Jimenez 2014)
  // (27/10 g) 24 muestras (antes 16) y el giro por cuadro solo si x331 > 0.5 (sin TAA el giro por cuadro era grano que titila)
  float rot = 6.2831853 * frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715))) + (IniParams.Load(int2(331, 0)).x > 0.5 ? frac(m_timeParam.z * 7.31) : 0.0));
  float3 acc = 0, wsum = 0;
  [unroll] for (int i = 0; i < 24; i++) {
    bool redSet = (i % 3) != 2;   // dos de cada tres muestras con el radio del rojo (el que mas lejos llega)
    float rmm = kBurleyX[i / 3] * (redSet ? dmm.r : dmm.g);
    float a = rot + i * 2.3999632;                       // angulo aureo: las 16 muestras no se alinean
    float2 off = float2(cos(a), sin(a)) * (rmm * mmToPx);   // en pixeles
    float2 uvS = uv + off * rb;
    if (!IsSkin(uvS)) continue;
    float zS = LinZ(uvS);
    float dzmm = abs(zS - (z0 + dot(slope, off))) * 1000.0;
    float tol = max(1.0, 0.5 * rmm) / tolK;               // la superficie puede curvarse mas lejos del centro
    float depthW = exp(-(dzmm * dzmm) / (tol * tol));
    // peso de cada canal = perfil / pdf de la mezcla (rojo + verde, mitad y mitad): el 1/r de los dos se cancela
    float pdf = 0.667 * BurleyF(rmm / dmm.r) / dmm.r + 0.333 * BurleyF(rmm / dmm.g) / dmm.g;
    float3 w = float3(BurleyF(rmm / dmm.r) / dmm.r, BurleyF(rmm / dmm.g) / dmm.g, BurleyF(rmm / dmm.b) / dmm.b) / max(pdf, 1e-6);
    w *= depthW;
    acc += w * inLight.SampleLevel(sPoint, uvS, 0).xyz;   // (27/10 i) muestra puntual: con la bilineal, en el borde de la cara entraba el fondo claro de al lado (contorno de pixeles brillantes)
    wsum += w;
  }
  // el pixel central entra con el peso de una muestra media: sin vecinos validos (borde) queda como estaba
  acc += c0; wsum += 1.0;
  float3 blurred = acc / wsum;
  return float4(lerp(c0, blurred, saturate(P.y)), 1);
}
#endif
