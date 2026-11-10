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
Texture2D<float4> inRef   : register(t6);
Texture2D<float>  inMouth : register(t7);   // (04/11 boca) mascara del interior de la boca de la DLL (mouthmask.cpp): 1 = boca   // (01/11) referencia de intensidad de la luz de prueba (1x1, suavizada)
Texture1D<float4> IniParams : register(t120);
SamplerState sPoint : register(s0);
SamplerState sLinear : register(s1);

float LinZ(float2 uv) { return m_projectionParam.z / (inDepth.SampleLevel(sPoint, uv, 0).x - m_projectionParam.w); }
bool IsSkin(float2 uv) { return abs(inMat.SampleLevel(sPoint, uv, 0).z - 0.98039) < 0.003; }
float Lum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// radios del perfil de Burley en unidades de d: inversa de la CDF 1 - e^-x/4 - 3e^(-x/3)/4 en u = (i + 0.5) / 8
static const float kBurleyX[8] = { 0.1305, 0.4299, 0.7976, 1.2651, 1.8883, 2.7792, 4.2184, 7.4616 };

// (09/11) pixel de la boca visible: la mascara de la DLL guarda la profundidad del dibujo de la boca y solo vale si coincide con la final
bool PesMouthAt(float2 px, uint mw, uint mh) {
  if (mw == 0) return false;
  uint2 p = min((uint2)px, uint2(mw - 1, mh - 1));
  float mz = inMouth.Load(int3(p, 0)), dz = inDepth.Load(int3(p, 0)).x;
  return mz > 0.0 && abs(mz - dz) <= 1e-6 + 2e-4 * abs(dz);
}
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
//   x297 prendido | y297 preset (0 manual, 1 dia, 2 tarde, 3 noche, 4 estudio, 5 nublado, 6 lluvia, 7 mediodia, 8 contraluz, 9 noche 2 torres, 10 sol cercano a contraluz, 11 foco de estadio cercano, 12 una luz fuerte y la otra apagada) | z297 rumbo (grados; 0 = desde la camara, 90 = derecha,
//   180 = contraluz) | w297 altura (grados) | x298 intensidad (veces la luz de estudio sobre la piel) | y298 calidez (0 azul
//   .. 1 naranja) | z298 luz de estudio que queda | w298 sombra trazada (0 = sin sombra)
//   x319 icono de las luces en pantalla (DLL, overlay.cpp) | y319 relleno del estudio | z319 recorte del estudio
//   (07/11, Agustin: angulo, sol o foco, volumetrica en el manual) x377 tipo (0 sol lejano, 1 foco) | y377 distancia del foco al
//   jugador (m) | z377 apertura del cono (grados) | w377 luz volumetrica (fuerza) | x378 cielo en el manual | y378 densidad de la bruma
//   (07/11 c) x379 tamano de la luz (grados: 0.27 sol, 5 = ventana / softbox: sombra blanda) | y379 calidad de la sombra (0 rapida 12
//   pasos, 1 normal 24, 2 alta 48) | z379 rebote del suelo | w379 reflector (rebote de pared del lado opuesto, sin sombra) | x380
//   color del reflector (0 blanco, 1 plateado, 2 dorado) | y380 alcance de la sombra (m) | z380 brillo de la luz (x especular)
//   (07/11 d, banco de pruebas) w380 vista de prueba (0 normal, 1 solo luz directa, 2 solo rebote / relleno, 3 solo sombra trazada,
//   4 N.L en falso color, 5 mascara de piel) | x381 exposicion (EV, x2^EV sobre la luz de prueba) | y381 segunda luz libre (fuerza)
//   | z381 su rumbo | w381 su altura
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

// sombra trazada en pantalla (la de contacto de la pasada de luz, sin shadow map).
// (04/11 piel) Agustin: "las sombras tienen baja calidad". Antes: 12 pasos fijos, corte en el primer paso que pegaba (borde en
// escalones de un paso entero) y el mismo ruido todos los cuadros (el TAA no lo podia promediar: borde granulado y quieto).
// Ahora: 24 pasos, el borde se refina con 4 bisecciones entre el ultimo paso libre y el que pego (borde exacto), el ruido cambia
// cada cuadro (el TAA lo promedia en un borde suave y estable) y la luz tiene tamano (cono de ~1.2 grados como un foco de estudio
// grande; el sol real es 0.27 grados): penumbra que se abre con la distancia al que tapa, como en la realidad
// (07/11 c, Agustin: "cambiar la calidad de la luz, pruebas del rebote") tamano de la luz (radio de la penumbra, rad; el sol
// 0.0047), pasos de la traza (calidad) y alcance (m): los fija main desde la fila 379 / 379 antes de trazar
static float g_shRad = 0.0047, g_shLen = 0.45;
static int g_shSteps = 24;
float PesTraceShadow(float3 Pv, float3 L, float rawNL, float zC, float zSgn, float zA, float2 uv, float2 pxy, float shK) {
  if (shK <= 0.001 || rawNL <= 0.02) return 1.0;
  float fr = floor(frac(m_timeParam.x * 0.25) * 4096.0);   // cambia cada cuadro (si el reloj corre)
  float2 px = pxy + 5.588238 * fr;
  float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
  float ign2 = frac(52.9829189 * frac(dot(px + 37.0, float2(0.00583715, 0.06711056))));
  // (04/11 p, Agustin: con el cono de 1.2 grados la sombra de la nariz y la ceja se abria y la cara quedaba plana; la version de
  // las 05:41 con sombra dura se veia mejor) radio del sol real (0.27 grados = 0.0047 rad): borde nitido, sin escalones (biseccion)
  { // penumbra: la direccion de la luz se mueve dentro de un cono chico (radio ~0.0047 rad, el sol)
    float3 up = abs(L.y) < 0.95 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 ta = normalize(cross(up, L)), tb = cross(L, ta);
    float ang = 6.2831853 * ign2, rr = sqrt(frac(ign + 0.618034)) * g_shRad;
    L = normalize(L + (ta * cos(ang) + tb * sin(ang)) * rr);
  }
  float csLen = g_shLen;
  float bias = (0.008 + 0.0018 * zA) * (0.6 + 0.4 / max(rawNL, 0.12));
  float3 Q = Pv + L * bias;
  float occ = 0, tPrev = 0;
  [loop] for (int ci = 0; ci < g_shSteps; ci++) {
    float f = (ci + ign) / (float)g_shSteps;
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
      // biseccion: el punto exacto donde el rayo entra detras de la superficie (borde de la sombra sin escalones)
      float ta0 = tPrev, ta1 = t, dzH = dz;
      [unroll] for (int bi = 0; bi < 4; bi++) {
        float tm = 0.5 * (ta0 + ta1); float3 Rm = Q + L * tm;
        float2 sm = Rm.xy / (Rm.z * m_projectionParam.xy) + m_cameraCenterOffset.xy;
        float dzm = (Rm.z - LinZ(sm * float2(0.5, -0.5) + 0.5)) * zSgn;
        if (dzm > 0.003 + 0.0008 * Rm.z * zSgn) { ta1 = tm; dzH = dzm; } else ta0 = tm;
      }
      float fh = sqrt(saturate(ta1 / csLen));
      occ = (1.0 - 0.5 * fh) * saturate(dzH / (0.2 * thick) + 0.35);
      // (09/11, Agustin: "los cortes de la sombra se ven raros") el pelo (marca 252/255 de 02f1d510 / c75f6047, fila 339) llega
      // al buffer de profundidad como placas solidas: el flequillo tiraba sobre la frente una sombra entera de borde recto y
      // duro. El pelo real deja pasar luz entre las hebras: sombra parcial y blanda, mas tenue cuanto mas lejos esta el mechon
      { float2 sH = (Q.xy + L.xy * ta1) / ((Q.z + L.z * ta1) * m_projectionParam.xy) + m_cameraCenterOffset.xy;
        float mz = inMat.SampleLevel(sPoint, sH * float2(0.5, -0.5) + 0.5, 0).z;
        if (abs(mz - 0.98823529) < 0.002) occ *= 0.45 * (1.0 - 0.5 * fh) * (0.6 + 0.4 * ign); }
      break;
    }
    tPrev = t;
  }
  float2 eM = saturate(min(uv, 1.0 - uv) * 10.0);
  // (09/11, Agustin: corte duro y recto de la frente al costado de la nariz con la luz de costado) con la luz lateral el rayo de
  // un pixel cerca del terminador pasa por detras de la frente / el puente de la nariz (convexos, mas cerca de la camara) y la
  // traza en pantalla los cuenta como oclusor: sombra de borde de regla adentro del lado iluminado. Ahi la caida de la luz ya
  // la da N.L (suave); la sombra trazada entra de a poco (N.L 0.02 -> 0.40) y queda entera para las sombras proyectadas de
  // verdad (nariz sobre la mejilla, menton sobre el cuello), que caen en zonas que miran a la luz
  occ *= smoothstep(0.02, 0.40, rawNL);
  return 1.0 - saturate(occ * saturate(shK) * eM.x * eM.y);
}

// (07/11) LUZ VOLUMETRICA de la luz de prueba (fila 377 w, 377 y): no hay shadow map en el editor, asi que se hace en pantalla:
//  (a) bruma: lo que el aire entre la camara y la superficie dispersa hacia el ojo (Henyey-Greenstein g = 0.55, mas fuerte
//      mirando hacia la luz), con la densidad y378 y el largo del rayo hasta la superficie (o 4 m en el fondo del editor);
//  (b) haces: marcha de 16 pasos desde el pixel hacia la posicion de la luz en pantalla (sol lejano o foco) contando cuanto
//      del camino esta libre de geometria (Mitchell 2007): el jugador tapa la luz y deja rayos alrededor de la silueta.
//      Si la luz queda detras de la camara no hay haces (solo bruma). El ruido cambia por cuadro y lo promedia el TAA.
static float g_volG = -1.0;   // (08/11 0qtsss) fase de la luz volumetrica del partido (z335) cuando el editor la copia; -1 = y385
float3 PesVolumetric(float2 uv, float2 pxy, float3 Pv, float zA, bool geo, float zSgn, float3 Lw, bool spot, float3 Pl, float3 col, float strength, float dens) {
  if (strength <= 0.001) return 0.0;
  float3 rayD = normalize(Pv);
  float3 dvL = Pl - Pv; float dL2 = dot(dvL, dvL);
  float3 toL = (spot && dL2 > 1e-6) ? dvL * rsqrt(dL2) : Lw;   // (08/11) sin normalize de un vector nulo
  // (08/11, Agustin: "la luz volumetrica no funciona") antes la fase de Henyey-Greenstein con g 0.55 sin normalizar dejaba la bruma en
  // ~2 % de la luz y, con la luz detras de la camara (rumbo 0..90), casi nada: no se veia. Ahora la fase va normalizada (1 = aire
  // parejo), g sale de y385 (direccionalidad), la bruma llega a w385 m y los haces tienen su propia fuerza (x385)
  float4 V9 = IniParams.Load(int2(385, 0));
  float g = clamp(g_volG >= 0.0 ? g_volG : (V9.y > 0.0 ? V9.y : 0.35), 0.0, 0.9), cosT = dot(rayD, toL);
  float phase = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cosT, 1e-3), 1.5);   // 1 = isotropo
  float reach = clamp(V9.w > 0.0 ? V9.w : 6.0, 0.5, 40.0);
  float len = geo ? min(length(Pv), reach) : reach;
  float d = max(dens, 0.0) * 0.35;
  float haze = (1.0 - exp(-d * len)) * phase * 0.30;
  // haces en pantalla
  float shafts = 0.0;
  float3 P = spot ? Pl : Lw * 50.0;
  if (P.z * zSgn > 0.2) {
    float2 sL = P.xy / (P.z * m_projectionParam.xy) + m_cameraCenterOffset.xy;
    float2 uvL = sL * float2(0.5, -0.5) + 0.5;
    float fr = floor(frac(m_timeParam.x * 0.25) * 4096.0);
    float ign = frac(52.9829189 * frac(dot(pxy + 5.588238 * fr, float2(0.06711056, 0.00583715))));
    float2 stp = (uvL - uv) / 16.0;
    float acc = 0.0, wsum = 0.0, w = 1.0;
    [unroll] for (int i = 0; i < 16; i++) {
      float2 uvS = uv + stp * (i + ign);
      float free = 1.0;
      if (all(uvS >= 0.0) && all(uvS <= 1.0)) { float zS = LinZ(uvS) * zSgn; if (zS >= 0.1 && zS <= 60.0 && (!spot || zS < P.z * zSgn)) free = 0.0; }
      acc += free * w; wsum += w; w *= 0.90;
    }
    float occ = acc / max(wsum, 1e-3);
    // cerca del centro de la luz los haces son mas fuertes; de lejos en pantalla se desvanecen
    float dl = length((uvL - uv) * float2(1.0, 0.5625));
    shafts = occ * phase * (0.6 + 1.2 * exp(-dl * 3.0)) * (1.0 - exp(-d * (geo ? len : reach))) * 0.35 * (V9.x > 0.0 ? V9.x : 1.0);
  }
  // z385 color de la bruma: 0 = el de la luz, 1 = aire frio azulado (noche, humo de estadio)
  float3 hc = lerp(col, Lum(col) * float3(0.70, 0.82, 1.10), saturate(V9.z));
  return hc * haze * strength + col * shafts * strength;
}

// (08/11) color por temperatura en kelvin (aprox. de Tanner Helland), luminancia 1: 1800 vela, 3200 lampara, 5600 sol / flash, 9000 sombra
float3 PesKelvin(float K) {
  float t = clamp(K, 1000.0, 15000.0) / 100.0;
  float r = t <= 66.0 ? 1.0 : saturate(1.2929 * pow(t - 60.0, -0.1332));
  float gg = t <= 66.0 ? saturate(0.3901 * log(t) - 0.6318) : saturate(1.1298 * pow(t - 60.0, -0.0755));
  float b = t >= 66.0 ? 1.0 : (t <= 19.0 ? 0.0 : saturate(0.5432 * log(t - 10.0) - 1.1962));
  float3 c = float3(r, gg, b);
  return c / max(Lum(c), 1e-4);
}
float3 PesTint(float warm) {   // azul frio .. naranja, luminancia 1
  float3 t = lerp(float3(0.80, 0.88, 1.00), float3(1.00, 0.62, 0.32), saturate(warm));
  return t / max(Lum(t), 1e-4);
}

// (08/11, Agustin: "al rotar no cambia la luz") z378 = 1: la luz queda fija en el MUNDO (rumbo medido en la cancha, altura sobre el
// suelo real); al girar la camara alrededor de la cara la luz se queda donde estaba. 0 = como antes, pegada a la camara. El mundo
// pasa a la vista con las columnas de m_view (igual que la luz real en pes_brow_light.hlsli)
static bool g_lightWorld = false;
float3 PesDirL(float az, float el, float zSgn) {   // rumbo 0 = de frente (hacia la camara), 90 = desde la derecha
  float ca = cos(radians(az)), sa = sin(radians(az)), ce = cos(radians(el)), se = sin(radians(el));
  float3 Lc = float3(ce * sa, se, -ce * ca * zSgn);
  if (g_lightWorld) {
    float3 W = float3(ce * sa, se, ce * ca);
    float3 Lv = float3(dot(W, m_view._m00_m10_m20), dot(W, m_view._m01_m11_m21), dot(W, m_view._m02_m12_m22));
    if (dot(Lv, Lv) > 0.25) Lc = Lv;   // m_view vacio (sin escena): queda la de la camara
  }
  return normalize(Lc);
}

// (04/11 nariz) transmitancia de la piel (perfil de Jimenez, d en mm) y su color (z34): copia de PESSkinTransmittance /
// PESTransColor de pes_common.hlsli (este archivo no lo incluye), para que la luz de prueba use la misma contraluz que el partido
float3 PesSsTransmit(float d) {
  float dd = -d * d;
  return float3(0.233, 0.455, 0.649) * exp(dd / 0.0064) + float3(0.100, 0.336, 0.344) * exp(dd / 0.0484)
       + float3(0.118, 0.198, 0.000) * exp(dd / 0.187)  + float3(0.113, 0.007, 0.007) * exp(dd / 0.567)
       + float3(0.358, 0.004, 0.000) * exp(dd / 1.99)   + float3(0.078, 0.000, 0.000) * exp(dd / 7.41);
}
float3 PesSsTransColor(float3 T, float k) {
  const float3 lw = float3(0.2126, 0.7152, 0.0722), tint = float3(1.0, 0.56, 0.40);
  return lerp(T, dot(T, lw) / dot(tint, lw) * tint, saturate(k));
}

// una luz direccional: difusa (Lambert + borde rojizo de la piel + contraluz por el grosor) y brillo (GGX + lobulo fino)
// (04/11 nariz) Agustin: "algunas caras reaccionan mal al sss y a la luz, ves la sombra de la nariz" (cara pecosa, sol de prueba
// -130 / 14 / 1.6 / 0.62: mejilla junto a la nariz naranja con un corte marcado). Dos causas:
//  1) la contraluz salia SOLO del trm del G-buffer (thick = trm x 1.6): en caras con el trm claro y parejo (la de la captura tiene
//     la mejilla en 0.17 y la nariz en 0.21; Messi 0.14 / 0.19) la zona de la nariz pasaba el umbral y la mejilla brillaba roja como
//     si fuera una oreja. Ahora es la de las pasadas del sol (PesSkTransThick): grosor por la CURVATURA medida en pantalla (oreja,
//     aleta: finas; mejilla, frente: gruesas), el trm solo afina la densidad, y solo en la piel de espaldas a la luz.
//  2) la sombra trazada solo corre con N.L > 0.02 y del otro lado vale 1: contraluz (x sqrt(sh)) y borde rojizo (x lerp(1, sh,
//     0.6)) saltaban de golpe en esa linea. Ahora la contraluz es 0 donde la piel mira a la luz (no necesita la sombra) y el
//     borde usa la sombra con un fundido desde N.L = 0.02, sin escalon. Con la SSSS prendida (w95) el borde rojizo es mas angosto:
//     la difusion en milimetros ya la hace la pasada 2 (igual que en el partido, fila 259 apagada por eso)
// (04/11 nariz b) REGLA FIJA de Agustin: el interior de la boca no cambia. La lengua tiene la marca de piel (w18 = 1) y en el
// G-buffer no hay nada que la distinga de la cara, asi que los pixeles de piel EN UNA CAVIDAD (rodeados de superficie mas cerca de
// la camara en 7 de 8 direcciones, hasta 4 cm: lengua y garganta detras de labios y dientes; tambien las fosas nasales) siguen con
// la formula de antes, identica (legacy = true). thickM no se usa en ese caso
// (08/11 0qtsss, Agustin: "quiero probar bien a fondo el sss, que el modo editar reaccione igual con el preset de dia como lo haria
// en partido, asi como de noche") w411 = 1: en la piel (no en la boca ni cavidades: legacy) la luz de prueba usa el MISMO modelo que
// las pasadas del sol del partido (pes_skinlight.hlsli: PesSkDiffuse / PesSkTrans / PesSkTransThick / PesSkSpec, filas 3, 5, 34,
// 62, 64, 76, 95, 103): terminador pre-integrado por canal con la curvatura, contraluz direccional + por grosor (la mayor) y lobulo
// fino de la piel por zona. Los sliders de SSS del partido cambian lo mismo aca. g_skCurv / g_skMpp los pone main()
static float g_skCurv = 0.0, g_skMpp = 1e-3;
float3 PesMtSoftTerm(float nl, float3 w) {
  w = max(w, 1e-3);
  float3 x = nl.xxx;
  float3 s = (x + w) * (x + w) / (4.0 * w);
  return x >= w ? x : (x > -w ? s : 0.0);
}
void PesOneLightMatch(float3 L, float3 col, float sh, float3 n, float3 V, float rough, float trans, float thickM, float silK, inout float3 dif, inout float3 spec) {
  float rawNL = dot(n, L);
  float4 P3 = IniParams.Load(int2(3, 0)), P5 = IniParams.Load(int2(5, 0)), P34 = IniParams.Load(int2(34, 0)), P64 = IniParams.Load(int2(64, 0));
  float4 P76 = IniParams.Load(int2(76, 0)), P95 = IniParams.Load(int2(95, 0)), Q103 = IniParams.Load(int2(103, 0));
  // difusa (PesSkDiffuse)
  bool uni = P95.w > 0.5;
  float wd = uni ? 1.5 : (P3.x > 0 ? P3.x : 1.0);
  float3 w = saturate(trans) * wd * float3(1.0, 0.6, 0.45);
  { float4 Q62 = IniParams.Load(int2(62, 0)); if (Q62.x > 0.5 && Q62.w > 0.0) w *= lerp(1.0, 0.45 + 1.4 * g_skCurv, saturate(Q62.w)); }
  w = min(w * 0.12, float3(0.22, 0.11, 0.07));
  if (uni) { float rPx = 0.006 * max(P95.x, 0.3) / max(g_skMpp, 1e-6); w *= 1.0 - 0.45 * saturate((rPx - 1.0) / 5.0); }
  float3 d = saturate(PesMtSoftTerm(rawNL, w));
  float shT = lerp(1.0, sh, smoothstep(0.02, 0.12, rawNL));   // la sombra trazada solo existe con N.L > 0.02: sin escalon
  dif += col * d * shT;
  // contraluz (PesSkTrans direccional y PesSkTransThick por grosor, la mayor de las dos, como d0e9 / 7dbbc)
  float thin = saturate(trans * 1.6);
  float3 Lt = normalize(L + n * 0.3);
  float3 tr;
  if (P34.w > 0.5) {
    // (09/11, Agustin: "a contraluz las partes con mas cartilago no son reales como actuaria la piel con la luz") tres cambios:
    // 1) la luz que sale de una lamina fina (oreja, aleta de la nariz) es casi difusa: brilla toda la zona que recibe luz por
    //    detras (back), no solo el punto donde se mira directo al sol (antes pow(V.-L, 4) = mancha chica que seguia a la camara);
    //    queda lobulo hacia adelante (0.6 + 0.6 fwd: el pico mirando al sol es el de antes, el resto de la lamina brilla a la mitad);
    // 2) color: con el grosor mas fino el exp(-(1.15 - thin) sigT) daba (0.86, 0.58, 0.38) = amarillo palido; el cartilago real
    //    deja pasar rojo-naranja saturado (sangre): piso de 0.35 de recorrido -> (0.70, 0.28, 0.10) en lo mas fino;
    // 3) solo cerca de una silueta (silK): una nariz vista de frente tiene el craneo detras y no puede encenderse con el sol de
    //    atras (lo mismo que ya hacia el camino por grosor P64 desde el 04/11)
    float fwd = pow(saturate(dot(V, -Lt)), 4.0);
    float back = saturate(dot(-n, L) * 0.8 + 0.2);
    float3 sigT = float3(1.0, 3.6, 6.5) * (P34.y > 0 ? P34.y : 1.0);
    float3 Tr = exp(-(1.35 - thin) * sigT) * thin;
    // (09/11 g, Agustin: capturas "Desde atras": la oreja casi no se enciende) 0.45..0.85 -> 0.30..0.75: la oreja entera (no solo el
    // borde mas fino) deja pasar la luz roja; la mejilla (trm bajo) sigue sin transmitir
    float thinGate = lerp(smoothstep(0.30, 0.75, thin), 1.0, saturate(P76.y));
    float exitK = back * (0.6 + 0.6 * fwd) * smoothstep(-0.05, 0.25, dot(L, -V)) * lerp(0.2, 1.0, silK);
    // (09/11 c, Agustin eligio "las tres": borde rojizo, poros y mas luz en orejas y nariz a contraluz) x1.6 -> x2.3
    tr = PesSsTransColor(Tr * exitK * thinGate * P5.x * 2.3 * (P76.x > 0.0 ? P76.x : 1.0), P34.z);
  } else tr = PesSsTransColor(thin * smoothstep(0.45, 0.85, thin) * pow(saturate(dot(V, -Lt)), 3.0) * float3(1.0, 0.24, 0.10) * P5.x, P34.z);
  if (P64.x > 0.5 && P64.z > 0.0) {
    float dens = (P64.w > 0 ? P64.w : 30.0) * (1.25 - thin);
    float3 Tt = PesSsTransColor(PesSsTransmit(thickM * dens), P34.z);
    float backL = smoothstep(0.0, 0.35, dot(L, -n)) * (1.0 - saturate(rawNL * 3.0)) * lerp(0.35, 1.0, smoothstep(0.25, 0.75, thin));
    tr = max(tr, Tt * backL * P64.z * 0.8 * silK);   // silK: sin atlas de grosor en el editor, solo cerca de una silueta (entrecejo 04/11)
  }
  dif += col * tr;
  // brillo (PesSkSpec): el lobulo ancho hace de brillo del juego (x w3, Fresnel de piel fila 103) + lobulo fino por zona (y5, w5)
  float3 H = normalize(L + V);
  float nl = saturate(rawNL), nh = saturate(dot(n, H)), nv = saturate(dot(n, V)), vh = saturate(dot(V, H));
  float F = 0.028 + 0.972 * pow(1 - vh, 5);
  float a2 = rough * rough; a2 *= a2;
  float dd = nh * nh * (a2 - 1) + 1;
  float D = a2 / (3.14159 * dd * dd);
  float k = (rough + 1) * (rough + 1) / 8.0;
  float G = (nv / (nv * (1 - k) + k)) * (nl / (nl * (1 - k) + k));
  float3 sp = col * (D * F * G / max(4.0 * nv * nl, 1e-3)) * nl * sh * (P3.w > 0 ? P3.w : 1.0);
  if (Q103.w > 0.5) { float rim = pow(1.0 - nv, 4.0); sp *= lerp(1.0, lerp(0.55, 1.7, rim), saturate(Q103.y)) * (Q103.z > 0 ? Q103.z : 1.0); }
  float rS = lerp(0.18, clamp(rough * 0.45, 0.12, 0.32), saturate(P5.w)); float a2s = rS * rS; a2s *= a2s;
  float dds = nh * nh * (a2s - 1) + 1;
  sp += col * (a2s / (3.14159 * dds * dds)) * F * nl * 0.25 * P5.y * sh;
  spec += sp;
}
void PesOneLight(float3 L, float3 col, float sh, float3 n, float3 V, bool skin, float rough, float trans, float thickM, float silK, bool legacy, inout float3 dif, inout float3 spec) {
  if (skin && !legacy && IniParams.Load(int2(411, 0)).w > 0.5) { PesOneLightMatch(L, col, sh, n, V, rough, trans, thickM, silK, dif, spec); return; }
  float rawNL = dot(n, L);
  // --- difusa: Lambert con un borde apenas envuelto en la piel (el resto lo hace el disco de Burley de la pasada 2) ---
  float wrap = skin ? 0.03 : 0.0;
  float nl = saturate((rawNL + wrap) / (1.0 + wrap));
  dif += col * nl * sh;
  // (27/10 e) borde de la sombra rojizo (envoltura por canal), solo en la franja del terminador
  if (skin && legacy) {   // formula de antes del 04/11 (interior de la boca y cavidades), sin cambios
    float3 wr = float3(0.30, 0.10, 0.05);
    float3 nl3 = saturate((rawNL + wr) / (1.0 + wr));
    dif += col * max(nl3 - nl, 0.0) * 0.8 * lerp(1.0, sh, 0.6);
    float3 Lt = normalize(L + n * 0.3);
    float tv = pow(saturate(dot(V, -Lt)), 3.0);
    float thick = saturate(trans * 1.6);
    float x5 = IniParams.Load(int2(5, 0)).x;
    dif += col * thick * smoothstep(0.2, 0.7, thick) * tv * float3(1.0, 0.24, 0.10) * (x5 > 0 ? x5 : 1.0) * sqrt(sh);
  } else if (skin) {
    bool ssssOn = IniParams.Load(int2(95, 0)).w > 0.5;
    float3 wr = ssssOn ? float3(0.12, 0.04, 0.02) : float3(0.30, 0.10, 0.05);
    float3 nl3 = saturate((rawNL + wr) / (1.0 + wr));
    float shT = lerp(1.0, sh, smoothstep(0.02, 0.12, rawNL));   // sin escalon donde empieza la sombra trazada
    dif += col * max(nl3 - nl, 0.0) * 0.8 * lerp(1.0, shT, 0.6);
    // contraluz por grosor (como PesSkTransThick de las pasadas del sol): oreja y aletas de la nariz si, mejilla no
    float4 P64 = IniParams.Load(int2(64, 0));
    float thin = saturate(trans * 1.6);
    float dens = (P64.w > 0 ? P64.w : 30.0) * (1.25 - thin);
    float3 Tt = PesSsTransColor(PesSsTransmit(thickM * dens), IniParams.Load(int2(34, 0)).z);
    float backL = smoothstep(0.0, 0.35, dot(L, -n)) * (1.0 - saturate(rawNL * 3.0)) * lerp(0.35, 1.0, smoothstep(0.25, 0.75, thin));
    float x5 = IniParams.Load(int2(5, 0)).x;
    dif += col * Tt * backL * silK * (P64.z > 0 ? P64.z : 1.0) * 0.8 * (x5 > 0 ? x5 : 1.0);
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
  if (E.x < 0.5) return o;
  // (09/11 d, Agustin: "el sol cercano tiene que ser un sol real del juego") z393 = 1: la DLL prendio el sol real del motor en el
  // editor (zfx_SolEditor, w378 / w392): lo dibuja la pasada del sol del partido con sus cascadas; aca no se imita ni se baja la
  // luz de estudio (c0 ya trae ese sol)
  // (09/11 e, Agustin: "le falta el ambiente") con el ciclo de dia y noche (z393 = 2 + seno de la altura del sol) el ambiente va
  // con la hora sobre TODA la luz (estudio + sol real): de noche baja a un tercio y se enfria (luna / cielo nocturno), en el
  // amanecer y el atardecer se entibia. De dia queda tal cual: el sol real ya hace el contraste
  { float zR = IniParams.Load(int2(393, 0)).z;
    if (zR > 0.5) {
      if (zR > 1.5) {
        float sE = zR - 2.0;
        float nightK = smoothstep(0.06, -0.20, sE);
        float duskK = (1.0 - nightK) * (1.0 - smoothstep(0.04, 0.40, sE));
        float3 amb = lerp(1.0.xxx, float3(1.10, 0.90, 0.72), duskK);
        amb = lerp(amb, float3(0.26, 0.31, 0.45), nightK);
        o.c = float4(c0 * amb, 1); o.s = float4(s0 * dot(amb, float3(0.3, 0.5, 0.2)), 1);
      }
      return o;
    } }
  if (!noSun) return o;
  float zC = LinZ(uv);
  float zSgn = zC < 0.0 ? -1.0 : 1.0, zA = abs(zC);
  float4 E4 = IniParams.Load(int2(377, 0)), E5 = IniParams.Load(int2(378, 0));
  float4 E6 = IniParams.Load(int2(379, 0)), E7 = IniParams.Load(int2(380, 0));
  float4 E8 = IniParams.Load(int2(381, 0));
  int dbgView = (int)(E7.w + 0.5);
  g_lightWorld = E5.z > 0.5;
  g_shRad = radians(E6.x > 0.0 ? E6.x : 0.27) * 0.5 + 1e-4;   // (07/11 c) radio = la mitad del diametro angular
  g_shSteps = E6.y > 1.5 ? 48 : (E6.y > 0.5 ? 24 : 12);
  g_shLen = clamp(E7.y > 0.0 ? E7.y : 0.45, 0.1, 2.0);
  bool geo = !(zA < 0.1 || zA > 60.0);   // fondo del editor, sin geometria: solo recibe la luz volumetrica
  if (!geo) { float zc = LinZ(float2(0.5, 0.5)); zSgn = (abs(zc) >= 0.1 && abs(zc) <= 60.0) ? (zc < 0.0 ? -1.0 : 1.0) : (m_projectionParam.z < 0.0 ? -1.0 : 1.0); zA = 4.0; zC = zA * zSgn; }
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
  // (08/11, Agustin: "mas opciones de luz, como un sol cercano para los contraluz, un foco de estadio cercano, una fuente de luz
  // fuerte y otra apagada") 10 = sol cercano a contraluz (foco calido a 3 m detras del jugador: borde encendido y caida real),
  // 11 = foco de estadio cercano (torre blanca fria a 5 m, alta, cono de 40 grados), 12 = una luz fuerte y la otra apagada
  // (una sola luz dura, sin relleno de cielo ni suelo y casi sin la luz de estudio del juego: lado en sombra casi negro)
  float pSpotD = 0.0, pSpotA = 0.0;
  if (preset == 10)      { az = 170; el = 20; inten = 1.60; warm = 0.80; keep = 0.35; skyK = 0.08; gndK = 0.05; pSpotD = 3.0; pSpotA = 70.0; }
  else if (preset == 11) { az = -35; el = 45; inten = 1.40; warm = 0.12; keep = 0.30; skyK = 0.04; gndK = 0.03; pSpotD = 5.0; pSpotA = 40.0; }
  else if (preset == 12) { az = 55;  el = 30; inten = 1.80; warm = 0.55; keep = 0.04; skyK = 0.0;  gndK = 0.0; }
  // (04/11 q, Agustin: "en el preset del dia por que no me permites rotar el sol") en los presets el Rumbo gira el sol alrededor del
  // jugador: se suma al rumbo del preset (0 = el del preset)
  if (preset != 0 && preset != 4) az += E.z;
  if (preset == 0 && E5.x > 0.0) skyK = E5.x;   // (07/11) cielo en el manual
  if (preset == 0 && E6.z > 0.0) gndK = E6.z * 0.05 / 0.5;   // (07/11 c) rebote del suelo en el manual (0.5 = como los presets)
  shK *= shMul;
  // (09/11, Agustin: "una opcion de timelapse para fingir que la luz pase un ciclo de dia y noche") w392 = ciclo de dia y noche
  // (1 = 30 s, 2 = 1 min, 3 = 2 min, 4 = 5 min por vuelta). Reloj: w393 lo escribe la DLL en cada cuadro (segundos, vuelve a 0
  // cada hora; los cuatro periodos dividen 3600, sin salto). Amanece a la derecha de la pantalla, mediodia alto y de frente,
  // atardece a la izquierda (calido y bajo), y de noche una luna fria, baja y debil. El color, la fuerza, la luz de estudio que
  // queda y el cielo van con la altura del sol; en el horizonte se funden sol y luna sin salto
  int tlMode = (int)(IniParams.Load(int2(392, 0)).w + 0.5);
  if (tlMode >= 1 && tlMode <= 4) {
    // (10/11, Agustin: "el timelapse en 30s es poco natural, se pierde detalle") vueltas de 2 / 5 / 10 / 20 minutos
    float tlPer = tlMode == 1 ? 120.0 : (tlMode == 2 ? 300.0 : (tlMode == 3 ? 600.0 : 1200.0));
    float tlT = frac(IniParams.Load(int2(393, 0)).w / tlPer);
    float sD = sin(6.2831853 * tlT);                      // > 0 de dia
    float dayK = smoothstep(-0.10, 0.10, sD);
    float sUp = saturate(sD);
    // (09/11 e, Agustin: "no es suave, ni la luz recorre como si fuera un timelapse") la misma orbita que el sol real de la DLL
    // (solluz.cpp): fija en el MUNDO (al girar la camara el sol se queda en el cielo), sale por un costado, arco hasta 65 grados
    // al mediodia y se pone por el otro; de noche la luna hace el mismo arco. En el horizonte la fuerza baja a 0 (abajo), asi el
    // cambio sol -> luna no salta
    float tA = 6.2831853 * tlT, oCa = cos(tA), oSa = sin(tA);
    float3 orb = float3(oCa, oSa * 0.9063078, oSa * 0.4226183);
    if (oSa < 0.0) orb = -orb;
    float orbFade = smoothstep(0.005, 0.20, orb.y);
    az = degrees(atan2(orb.x, orb.z)); el = max(degrees(asin(saturate(orb.y))), 1.0);
    float tWarmDay = lerp(0.98, 0.55, smoothstep(0.0, 0.6, sUp));
    warm  = lerp(0.12, tWarmDay, dayK);
    inten = lerp(0.22, lerp(0.55, 1.25, smoothstep(0.0, 0.5, sUp)), dayK);
    // (10/11, Agustin: "los negros no son negros del todo") de noche casi no queda luz de estudio ni de cielo: manda la luna
    keep  = lerp(0.05, lerp(0.40, 0.55, sUp), dayK);
    skyK  = lerp(0.03, 0.16, dayK); gndK = lerp(0.0, 0.05, dayK) * (0.3 + 0.7 * sUp);
    inten *= lerp(0.04, 1.0, orbFade);
    pSpotD = 0.0; pSpotA = 0.0; g_lightWorld = true;
  }
  bool studio = preset == 4;
  // --- referencia de intensidad: la irradiancia de estudio mas alta sobre la piel, suavizada en el tiempo (pasada 0, t6) ---
  float S = inRef.Load(int3(0, 0, 0)).x;
  if (S <= 1e-6) return o;
  float4 E9 = IniParams.Load(int2(386, 0));   // (08/11) x386 kelvin de la principal, y386 de la segunda (0 = calidez), z386 recorte, w386 relleno (manual)
  float3 sunC = (preset == 0 && E9.x >= 1000.0 ? PesKelvin(E9.x) : PesTint(warm)) * (S * inten);
  // --- geometria en vista, como la pasada de luz 1f623 (posicion desde la profundidad, normal del G-buffer) ---
  float2 sC = uv * float2(2, -2) + float2(-1, 1);
  float2 pp = (sC - m_cameraCenterOffset.xy) * m_projectionParam.xy;
  float3 Pv = float3(pp * zC, zC);
  float3 V = -normalize(float3(pp, 1));
  float3 n = DecodeN(uv);
  // (09/11, Agustin: "una opcion que coloque un sol cercano y permita cambiar sus angulos y posicion") w378 = 1: la luz principal
  // de CUALQUIER preset pasa a ser un sol cercano con posicion: rumbo (z297) y altura (w297) tal cual los sliders (no sumados al
  // preset), a y377 metros del jugador con un cono de z377 grados. Color e intensidad siguen siendo los del preset
  // (09/11 b, Agustin: "la luz de sol cercano no se ve como un sol") antes heredaba el cono del foco (45 grados: recortaba la cara
  // como una linterna), el color y la fuerza del preset (de noche, azul y debil). Un sol es: luz que baña todo (sin cono),
  // calida y fuerte, sombra nitida del tamano del sol (0.53 grados), poco relleno de estudio y el resplandor / los haces en el
  // aire hacia el sol. Rumbo, altura y distancia siguen siendo los sliders
  bool nearSun = E5.w > 0.5;
  if (nearSun) {
    // (09/11 c, Agustin: "que funcione como funciona el sol de los partidos de dia") rayos paralelos como el sol del partido: sin
    // posicion ni distancia (la direccion es la misma en toda la cara); solo rumbo y altura
    az = E.z; el = E.w; pSpotD = 0.0; pSpotA = 0.0;
    warm = 0.72; inten = max(inten, 1.6); keep = min(keep, 0.30); skyK = min(skyK, 0.12);
    g_shRad = radians(0.53) * 0.5;
    sunC = PesTint(warm) * (S * inten);
  }
  // (09/11, Agustin: "que sea mas facil de decidir... una opcion de sol por detras para probar el sss") z392 = de donde viene la
  // luz, en pocas opciones y siempre respecto de la camara (lo que se ve en pantalla): 1 de frente, 2 de costado, 3 desde atras
  // (contraluz: orejas, nariz y borde de la cara encendidos = SSS), 4 desde arriba. 0 = la del preset / los sliders
  { int qd = (int)(IniParams.Load(int2(392, 0)).z + 0.5);
    if (qd >= 1 && qd <= 4 && !(tlMode >= 1 && tlMode <= 4)) {
      g_lightWorld = false;
      if (qd == 1)      { az = 25.0;  el = 20.0; }
      else if (qd == 2) { az = 90.0;  el = 20.0; }
      else if (qd == 3) { az = 165.0; el = 12.0; }
      else              { az = 10.0;  el = 78.0; }
    } }
  float3 Lw = PesDirL(az, el, zSgn), L = Lw;
  // (07/11) foco en el manual: una luz con posicion a y377 m del jugador (el punto del centro de la pantalla) en la direccion del
  // rumbo / altura, cono de z377 grados con borde suave y caida con el cuadrado de la distancia (1 en el jugador)
  bool spot = (preset == 0 && E4.x > 0.5) || pSpotD > 0.0;
  float3 Pl = 0.0, lcol = sunC;
  if (spot) {
    float zCen = LinZ(float2(0.5, 0.5)), zCA = abs(zCen);
    float3 Pc = (zCA >= 0.1 && zCA <= 60.0) ? float3(-m_cameraCenterOffset.xy * m_projectionParam.xy * zCen, zCen) : float3(0, 0, 1.5 * zSgn);
    float distL = clamp(pSpotD > 0.0 ? pSpotD : (E4.y > 0 ? E4.y : 2.0), 0.3, 12.0), ap = clamp(pSpotA > 0.0 ? pSpotA : (E4.z > 0 ? E4.z : 45.0), 5.0, 170.0);
    Pl = Pc + Lw * distL;
    // (08/11) a prueba de NaN: el punto puede caer encima del foco (d = 0) o la profundidad del centro venir rara al cambiar de tipo
    float3 dv = Pl - Pv; float d2 = dot(dv, dv);
    L = (d2 > 1e-4 && d2 < 1e6) ? dv * rsqrt(d2) : Lw;
    d2 = clamp(d2, 0.01, 1e6);
    float ca = cos(radians(ap * 0.5)), cone = smoothstep(ca, lerp(ca, 1.0, 0.35), dot(-L, Lw));
    lcol = sunC * cone * min(distL * distL / d2, 1.0);
    if (nearSun) lcol = sunC;   // (09/11 b) un sol no cae con 1/d2 sobre la cara (eso lo hacia ver como un foco): la posicion solo da la direccion
  }
  bool skin = IsSkin(uv);
  float4 mat = inMat.SampleLevel(sPoint, uv, 0);
  float rough = clamp(mat.x, 0.08, 1.0);
  // (09/11, Agustin: "fingir que esta lloviendo y mojar el rostro") y392 = lluvia en la cara (0..1), con cualquier preset: la
  // pelicula de agua deja la piel lisa (brillo nitido) y mas oscura (el agua llena los poros y baja la dispersion de la
  // superficie); fuera de la piel solo un poco mas lisa. La boca (cavidad / mascara) no se moja aca
  float wetK = saturate(IniParams.Load(int2(392, 0)).y);
  bool wetSkin = false;
  { uint wW = 0, wH = 0; inMouth.GetDimensions(wW, wH);
    bool wMouth = wW > 0 && inMouth.Load(int3(min((uint2)pos.xy, uint2(wW - 1, wH - 1)), 0)) > 0.0;
    if (wMouth) wetK = 0.0; }
  if (wetK > 0.0) { wetSkin = IsSkin(uv); rough = lerp(rough, wetSkin ? 0.12 : max(rough * 0.6, 0.15), wetK * (wetSkin ? 1.0 : 0.5)); }
  uint tq = (uint)(inNrm.SampleLevel(sPoint, uv, 0).z * 1023.0 + 0.5);
  float trans = (float)(tq >> 4) / 63.0;
  // (04/11 nariz) grosor por la curvatura (como PesSkThickCurv del partido, sin atlas en el editor): normales del G-buffer a ~10 mm
  // a cada lado, descartando lo que no es piel o salta de superficie (borde de la nariz sobre la mejilla). curv 1 = radio de 2 cm
  // (04/11 entrecejo) Agustin, luz Manual a contraluz (rumbo -173, altura 4): "linea dura que baja del entrecejo por la nariz".
  // Con la luz detras de la cabeza TODA la cara mira para el otro lado (backL = 1) y la contraluz por curvatura encendia el
  // pliegue entre la nariz y la mejilla y el entrecejo: la curvatura no tenia signo y un pliegue (concavo) contaba como "fino"
  // igual que el borde de la oreja. Ahora: (1) solo curvatura CONVEXA (las normales se abren); (2) la luz solo atraviesa cerca
  // de una silueta (fondo a menos de ~15 mm en pantalla: borde de la oreja, aletas de la nariz de perfil): una nariz vista de
  // frente tiene el craneo detras y no deja pasar el sol de atras
  float thickM = 0.08, silK = 0.0;
  bool legacy = false;
  if (skin) {
    float W, H; inNrm.GetDimensions(W, H);
    float mpp = max(2.0 * abs(m_projectionParam.x) * zA / max(W, 1.0), 1e-6);
    { // cavidad (boca abierta, fosas nasales): en cuantas de 8 direcciones aparece algo 5 mm o mas por delante, a 8..40 mm
      int enc = 0;
      [unroll] for (int di = 0; di < 8; di++) {
        float a = di * 0.78539816;
        float2 dir = float2(cos(a), sin(a)) / float2(W, H);
        bool hit = false;
        [unroll] for (int ri = 0; ri < 4; ri++) {
          float rM = ri == 0 ? 0.008 : (ri == 1 ? 0.016 : (ri == 2 ? 0.026 : 0.040));
          float2 uvS = uv + dir * (rM / mpp);
          if (any(uvS < 0.0) || any(uvS > 1.0)) continue;
          if ((zC - LinZ(uvS)) * zSgn > 0.005) hit = true;
        }
        enc += hit ? 1 : 0;
      }
      legacy = enc >= 7;
      // (04/11 boca) con la mascara de la DLL la lengua se reconoce de verdad (tambien afuera de los labios); la prueba de cavidad
      // queda como respaldo (sin mascara, o fosas nasales)
      uint mmW = 0, mmH = 0; inMouth.GetDimensions(mmW, mmH);
      if (mmW > 0) {   // (09/11) la mascara guarda la profundidad de la boca: solo cuenta si coincide con la del pixel (donde la cara la tapo, no)
        uint2 mmP = min((uint2)pos.xy, uint2(mmW - 1, mmH - 1));
        float mmZ = inMouth.Load(int3(mmP, 0)), mmD = inDepth.Load(int3(mmP, 0)).x;
        if (mmZ > 0.0 && abs(mmZ - mmD) <= 1e-6 + 2e-4 * abs(mmD)) legacy = true;
      }
    }
    float oPx = clamp(0.010 / mpp, 1.0, 32.0);
    float curv = 0.0;
    [unroll] for (int ax = 0; ax < 2; ax++) {
      float2 dpx = (ax == 0 ? float2(oPx, 0) : float2(0, oPx)) / float2(W, H);
      float3 nA = n, nB = n, pA = Pv, pB = Pv;
      [unroll] for (int sd = 0; sd < 2; sd++) {
        float2 uvS = uv + (sd == 0 ? dpx : -dpx);
        if (any(uvS < 0.0) || any(uvS > 1.0) || !IsSkin(uvS)) continue;
        float zS = LinZ(uvS);
        float2 ppS = ((uvS * float2(2, -2) + float2(-1, 1)) - m_cameraCenterOffset.xy) * m_projectionParam.xy;
        float3 PS = float3(ppS * zS, zS);
        if (length(PS - Pv) > 0.025) continue;   // salto de superficie
        if (sd == 0) { nA = DecodeN(uvS); pA = PS; } else { nB = DecodeN(uvS); pB = PS; }
      }
      float dl = length(pA - pB);
      if (dl > 0.002) curv = max(curv, 0.02 * max(dot(nA - nB, pA - pB), 0.0) / (dl * dl));   // solo convexo
    }
    curv = saturate(curv);
    g_skCurv = curv; g_skMpp = mpp;   // (08/11 0qtsss) para la piel como en el partido (w411)
    float thinT = saturate(trans * 1.6);
    thickM = clamp(0.02 / max(curv, 0.15) * lerp(1.2, 0.35, smoothstep(0.45, 0.9, thinT)), 0.004, 0.08);
    // cercania a una silueta: fondo (5 cm o mas por detras, o sin geometria) a 4..16 mm en alguna de 8 direcciones
    [unroll] for (int si = 0; si < 8; si++) {
      float a = si * 0.78539816 + 0.3926991;
      float2 dir = float2(cos(a), sin(a)) / float2(W, H);
      [unroll] for (int ri = 0; ri < 4; ri++) {
        float rM = 0.004 * (ri + 1);
        float2 uvS = uv + dir * (rM / mpp);
        if (any(uvS < 0.0) || any(uvS > 1.0)) continue;
        float zS = LinZ(uvS), zSA = zS * zSgn;
        if (zSA < 0.1 || zSA > 60.0 || (zS - zC) * zSgn > 0.05) silK = max(silK, 1.0 - 0.2 * ri);
      }
    }
  }
  float3 dif = 0, spec = 0;
  float sh = geo ? PesTraceShadow(Pv, L, dot(n, L), zC, zSgn, zA, uv, pos.xy, shK) : 1.0;
  if (geo) PesOneLight(L, lcol, sh, n, V, skin, rough, trans, thickM, silK, legacy, dif, spec);
  // (07/11 d) segunda luz libre en Manual (otra torre, un flash o un contraluz): rumbo / altura propios, misma calidez, con sombra
  if (geo && preset == 0 && E8.y > 0.001) {
    float3 L2f = PesDirL(E8.z, E8.w, zSgn);
    float sh2f = PesTraceShadow(Pv, L2f, dot(n, L2f), zC, zSgn, zA, uv, pos.xy + 43.0, shK);
    float3 c2f = E9.y >= 1000.0 ? PesKelvin(E9.y) * (S * inten) : sunC;
    PesOneLight(L2f, c2f * E8.y, sh2f, n, V, skin, rough, trans, thickM, silK, legacy, dif, spec);
  }
  float3 difDirect = dif, specDirect = spec;   // (07/11 d) para las vistas de prueba: lo de arriba es luz directa; lo que sigue, rebote / relleno
  if (!geo) { dif = 0; spec = 0; }
  else if (studio) {
    float fillK = E3.y, rimK = E3.z;
    if (fillK > 0.001) {   // relleno: del otro lado de la camara, bajo, un poco frio, sin sombra
      float azF = (az >= 0 ? -1.0 : 1.0) * clamp(90.0 - abs(az) * 0.5, 25.0, 70.0);
      PesOneLight(PesDirL(azF, 8.0, zSgn), PesTint(saturate(warm - 0.2)) * (S * inten * fillK), 1.0, n, V, skin, rough, trans, thickM, silK, legacy, dif, spec);
    }
    if (rimK > 0.001) {   // recorte: desde atras, del mismo lado que la principal, alto
      float azR = (az >= 0 ? 1.0 : -1.0) * 150.0;
      float3 LR = PesDirL(azR, 35.0, zSgn);
      float shR = PesTraceShadow(Pv, LR, dot(n, LR), zC, zSgn, zA, uv, pos.xy + 17.0, shK);
      PesOneLight(LR, PesTint(0.45) * (S * inten * rimK), shR, n, V, skin, rough, trans, thickM, silK, legacy, dif, spec);
    }
  } else {
    if (tower2) {   // (04/11) segunda torre del lado opuesto, un poco mas debil, con su propia sombra
      float3 L2 = PesDirL(az + 180.0, el, zSgn);
      float sh2 = PesTraceShadow(Pv, L2, dot(n, L2), zC, zSgn, zA, uv, pos.xy + 29.0, shK);
      PesOneLight(L2, sunC * 0.7, sh2, n, V, skin, rough, trans, thickM, silK, legacy, dif, spec);
    }
    // (08/11, Agustin: "las opciones de luz deberian ser mas extensas") en Manual tambien relleno (w386, del otro lado de la camara,
    // sin sombra) y recorte (z386, desde atras del mismo lado que la principal, con sombra), como en el preset Estudio
    if (preset == 0 && E9.w > 0.001) {
      float azF = (az >= 0 ? -1.0 : 1.0) * clamp(90.0 - abs(az) * 0.5, 25.0, 70.0);
      PesOneLight(PesDirL(azF, 8.0, zSgn), PesTint(saturate(warm - 0.2)) * (S * inten * E9.w), 1.0, n, V, skin, rough, trans, thickM, silK, legacy, dif, spec);
    }
    if (preset == 0 && E9.z > 0.001) {
      float azR = (az >= 0 ? 1.0 : -1.0) * 150.0;
      float3 LR = PesDirL(azR, 35.0, zSgn);
      float shR = PesTraceShadow(Pv, LR, dot(n, LR), zC, zSgn, zA, uv, pos.xy + 23.0, shK);
      PesOneLight(LR, PesTint(0.45) * (S * inten * E9.z), shR, n, V, skin, rough, trans, thickM, silK, legacy, dif, spec);
    }
    // (27/10 g) relleno de cielo: el lado en sombra no queda gris plano; luz difusa del cielo (azulada, de arriba) y un poco de
    // rebote del suelo (calido, de abajo), proporcional al sol. El cielo es mas azul cuanto mas calido (bajo) esta el sol.
    float3 skyC = lerp(float3(0.55, 0.70, 1.00), float3(0.35, 0.50, 1.00), saturate(warm)) * (S * inten) * skyK;
    float3 gndC = float3(1.00, 0.72, 0.50) * (S * inten) * gndK;
    if (preset == 5 || preset == 6) skyC = float3(0.80, 0.85, 0.95) * (S * inten) * skyK;   // cielo cubierto: gris, no azul
    float up = n.y * 0.5 + 0.5;   // normal en vista: y arriba
    // (07/11 c) el rebote del suelo depende de cuanto sol le llega al suelo (sol alto = mas rebote)
    if (preset == 0) gndC *= saturate(sin(radians(max(el, 0.0))) * 1.6 + 0.1);
    // (09/11, "al maximo") oclusion del cielo y del suelo: el relleno llegaba entero a las cuencas de los ojos, debajo de la nariz,
    // debajo del menton y adentro de la oreja, y aplanaba la cara. Horizonte en 8 direcciones a 1.5 / 3.5 / 6 cm en pantalla
    // (lo que esta por delante del pixel tapa parte del cielo); la boca (mascara t7 o cavidad) queda sin oclusion, como antes
    {
      float aW, aH; inNrm.GetDimensions(aW, aH);
      float aMpp = max(2.0 * abs(m_projectionParam.x) * zA / max(aW, 1.0), 1e-6);
      float aoS = 0.0;
      [unroll] for (int ai = 0; ai < 8; ai++) {
        float aa = ai * 0.78539816 + 0.39 * frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715))));
        float2 ad = float2(cos(aa), sin(aa)) / float2(aW, aH);
        float hMax = 0.0;
        [unroll] for (int ri = 0; ri < 3; ri++) {
          float rM = ri == 0 ? 0.015 : (ri == 1 ? 0.035 : 0.06);
          float2 uA = uv + ad * (rM / aMpp);
          if (any(uA < 0.0) || any(uA > 1.0)) continue;
          float h = (zC - LinZ(uA)) * zSgn;
          if (h > 0.0 && h < 0.15) hMax = max(hMax, h / sqrt(h * h + rM * rM));   // seno del angulo de horizonte
        }
        aoS += hMax;
      }
      float skyAO = saturate(1.0 - 0.85 * aoS / 8.0);
      skyAO *= skyAO;
      { uint qW = 0, qH = 0; inMouth.GetDimensions(qW, qH);   // boca intacta: interior y cavidad sin oclusion nueva
        if (legacy || (qW > 0 && inMouth.Load(int3(min((uint2)pos.xy, uint2(qW - 1, qH - 1)), 0)) > 0.0)) skyAO = 1.0; }
      dif += (skyC * up + gndC * (1.0 - up)) * skyAO;
    }
    // (07/11 c) reflector: una pared / pantalla del lado opuesto a la luz, baja, que devuelve parte de la luz sin sombra (como el
    // reflector de un fotografo). Color: blanco (el de la luz), plateado (mas frio y brillante), dorado (calido)
    if (preset == 0 && E6.w > 0.001) {
      float azR = (az >= 0 ? -1.0 : 1.0) * clamp(180.0 - abs(az), 20.0, 160.0) * 0.5;
      int rc = (int)(E7.x + 0.5);
      float3 rcol = rc == 1 ? float3(0.90, 0.96, 1.10) : (rc == 2 ? float3(1.15, 0.90, 0.60) : float3(1, 1, 1));
      float3 LR = PesDirL(azR, 10.0, zSgn);
      float3 dR = 0, sR = 0;
      PesOneLight(LR, sunC * rcol * (E6.w * 0.5), 1.0, n, V, skin, rough, trans, thickM, silK, legacy, dR, sR);
      dif += dR; spec += sR * (rc == 1 ? 1.0 : 0.4);
    }
  }
  // (07/11) luz volumetrica (bruma + haces hacia la luz), tambien sobre el fondo del editor
  // (08/11 0qtsss, Agustin: "que la luz volumetrica agregue niebla o rayos de sol como en el partido") con la volumetrica del editor
  // en 0 (w377) y la del partido prendida (x335), los presets la copian: fuerza de x335 (0.6 -> 0.9), direccionalidad de z335
  float volS = E4.w;
  if (nearSun) volS = max(volS, 0.9);   // (09/11 b) el sol cercano siempre con resplandor y haces
  if (tlMode >= 1 && tlMode <= 4) volS *= lerp(0.1, 1.0, smoothstep(-0.10, 0.10, sin(6.2831853 * frac(IniParams.Load(int2(393, 0)).w / (tlMode == 1 ? 120.0 : (tlMode == 2 ? 300.0 : (tlMode == 3 ? 600.0 : 1200.0)))))));   // (10/11) de noche sin bruma clara (negros negros)
  { float4 VLm = IniParams.Load(int2(335, 0)); if (volS <= 0.001 && preset != 0 && VLm.x > 0.0) { volS = clamp(VLm.x * 1.5, 0.0, 4.0); g_volG = clamp(VLm.z, 0.0, 0.9); } }
  float3 vol = PesVolumetric(uv, pos.xy, Pv, zA, geo, zSgn, Lw, spot, Pl, sunC, volS, E5.y > 0 ? E5.y : 0.5);
  // (07/11 d) vistas de prueba (w380): aislar cada parte para juzgarla sola. Se pintan sobre gris neutro, sin la luz de estudio
  if (dbgView > 0) {
    float3 outc = 0; float3 outs = 0;
    if (dbgView == 1) { outc = difDirect; outs = specDirect; }
    else if (dbgView == 2) { outc = dif - difDirect; outs = spec - specDirect; }
    else if (dbgView == 3) { outc = geo ? sh.xxx * S * 0.5 : 0.0; }
    else if (dbgView == 4) { float nl = dot(n, L); outc = geo ? (nl >= 0 ? lerp(float3(0.1, 0.1, 0.5), float3(1, 1, 0.2), nl) : lerp(float3(0.1, 0.1, 0.5), float3(0.6, 0, 0), -nl)) * S * 0.5 : 0.0; }
        else if (dbgView == 5) { outc = (geo ? (skin ? float3(0.2, 0.9, 0.2) : float3(0.25, 0.25, 0.25)) : 0.0) * S * 0.5; if (legacy && skin) outc = float3(0.9, 0.2, 0.2) * S * 0.5; }
    if (!geo) outc = S * 0.06;   // fondo gris neutro
    o.c = float4(outc + (dbgView <= 2 ? vol : 0.0), 1); o.s = float4(outs, 1);
    return o;
  }
  float ev = exp2(clamp(E8.x, -6.0, 6.0));   // (07/11 d) exposicion de la luz de prueba
  dif *= ev; spec *= ev; vol *= ev;
  // (09/11, Agustin: "la luz no es muy buena, se ve todo muy plano", tambien en el modo Editar) la luz de estudio que queda (keep)
  // es la del EditFace: frontal y pareja, y con 0.55 de ella el lado en sombra nunca bajaba de ~1.3 pasos bajo el lado iluminado.
  // Ahora lo que queda del estudio se apaga hacia el lado contrario a la luz principal (envoltura n.L): mismo brillo del lado de la
  // luz, el lado en sombra recibe ~45-60 % de lo de antes (contraste de retrato ~4:1 en vez de ~2.3:1) sin tocar los presets.
  // El interior de la boca (mascara de la DLL / cavidad) queda exactamente como antes (regla de la boca).
  float keepC = geo ? saturate(keep) : 1.0;
  if (geo) {
    bool mouthPx = legacy;
    uint mW = 0, mH = 0; inMouth.GetDimensions(mW, mH);
    // cualquier valor de la mascara cuenta (> 0): sirve con la mascara 0/1 de antes y con la de profundidad del hilo de la boca
    // (0dlnmc f151b82); donde la cara tapa la boca tambien se deja como antes (de mas, nunca de menos)
    if (mW > 0 && inMouth.Load(int3(min((uint2)pos.xy, uint2(mW - 1, mH - 1)), 0)) > 0.0) mouthPx = true;
    if (!mouthPx) { float wrapK = saturate(dot(n, L) * 0.5 + 0.5); keepC *= lerp(0.45, 1.0, wrapK * wrapK); }
  }
  o.c = float4(c0 * keepC + dif + vol, 1);
  if (E7.z > 0.0) specMul *= E7.z;   // (07/11 c) brillo de la luz en el manual
  if (wetK > 0.0 && geo && !legacy) {   // (09/11) lluvia: brillo de la pelicula de agua y piel apenas mas oscura
    specMul *= 1.0 + (wetSkin ? 1.6 : 0.6) * wetK;
    if (wetSkin) o.c.xyz *= 1.0 - 0.12 * wetK;
  }
  // (09/11, Agustin: "mejora la iluminacion del modo editar al maximo") el brillo de la luz de estudio que queda tambien sigue a la
  // luz principal: antes el lado en sombra conservaba el 55 % del brillo de estudio y se veia plastico / mojado en la sombra.
  // Mismo factor que la difusa (la boca queda igual: keepC no cambia ahi)
  o.s = float4(s0 * keepC + spec * specMul, 1);
  // (08/11) un NaN / infinito aca lo arrastra el TAA a toda la cara cuadro tras cuadro: se queda la luz del juego
  if (any(isnan(o.c.xyz)) || any(isinf(o.c.xyz))) o.c = float4(c0, 1);
  if (any(isnan(o.s.xyz)) || any(isinf(o.s.xyz))) o.s = float4(s0, 1);
  o.c.xyz = min(o.c.xyz, 65000.0); o.s.xyz = min(o.s.xyz, 65000.0);
  return o;
}
#else
// ===================================================================================================================
// Pasada 2: disco de Burley
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float4 P = IniParams.Load(int2(95,0));
  float3 c0 = inLight.SampleLevel(sPoint, uv, 0).xyz;
  if (P.w < 0.5 || !IsSkin(uv)) return float4(c0, 1);
  // (09/11, captura 1744 de Agustin: borde rosado y blanco delante del labio de abajo) la boca (lengua, encias: marca de piel) entraba
  // al disco y su luz se corria a los labios, y la de los labios a la boca. Donde la mascara (con profundidad) dice boca: ni se
  // difumina ni da muestras. La boca queda como sin SSSS.
  uint mmW2 = 0, mmH2 = 0; inMouth.GetDimensions(mmW2, mmH2);
  if (PesMouthAt(pos.xy, mmW2, mmH2)) return float4(c0, 1);
  float z0 = LinZ(uv);
  // (08/11 SSSS) tamano de un pixel en uv desde el tamano real de la textura: ddx/ddy despues del 'return' de arriba (pixeles que no
  // son piel en el mismo cuadrito de 2x2: pestanas, cejas, borde de la cara) es indefinido y cambiaba el ancho del disco al mover la camara
  float2 rb; { uint lw, lh; inLight.GetDimensions(lw, lh); rb = 1.0 / float2(max(lw, 1u), max(lh, 1u)); }
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
  // (09/11 f) rojo 1.0 -> 1.5 mm (verde / azul igual): la entrada ya es irradiancia (sin textura) y el azul / verde no cambian:
  // el rojo viaja mas (medido en piel: rojo ~2.6 veces el verde) sin lavar poros; es el que hace el borde de sombra rojizo y blando de la piel real
  // (09/11 h, Agustin: "el sss no funciona solo como un colorado en texturas") la luz se mueve bajo la piel en los TRES canales:
  // antes el verde (0.42 mm) y el azul (0.26) casi no viajaban y el SSS se leia solo como un tinte rojo en el borde de la sombra.
  // Proporcion de la piel medida (Jensen 2001: rojo 3.67, verde 1.37, azul 0.68 mm de camino libre): el verde llega a la mitad del
  // rojo y el azul a un cuarto, asi la luz ablanda de verdad el sombreado (el terminador se corre hacia la sombra, las arrugas y los
  // poros del relieve se iluminan desde adentro) y el rojo queda solo como lo que mas lejos llega
  float3 dmm = float3(lerp(1.5, 0.85, mel), lerp(0.75, 0.50, mel), lerp(0.40, 0.30, mel)) * widthScale;
  float mmToPx = 0.001 * pxm;
  if (dmm.r * 3.0 * mmToPx < 0.6) return float4(c0, 1);   // de lejos lo resuelve la piel pre-integrada de la pasada de luz
  // (08/11 SSSS) entre 0.6 y 1.6 px el disco entra de a poco: antes se prendia de golpe y la cara cambiaba al acercar / alejar la camara
  float nearK = smoothstep(0.6, 1.6, dmm.r * 3.0 * mmToPx);
  // plano de la superficie: pendiente de la profundidad por pixel (del lado mas suave, para no tomar un borde)
  // (08/11 SSSS) un vecino que no es piel (pestana, ceja, pelo) no da la pendiente: en los cuencos de los ojos la tomaba de la pestana
  float2 uxp = uv + float2(rb.x, 0), uxn = uv - float2(rb.x, 0), uyp = uv + float2(0, rb.y), uyn = uv - float2(0, rb.y);
  float zxp = LinZ(uxp) - z0, zxn = z0 - LinZ(uxn);
  float zyp = LinZ(uyp) - z0, zyn = z0 - LinZ(uyn);
  if (!IsSkin(uxp)) zxp = 1e6; if (!IsSkin(uxn)) zxn = 1e6; if (!IsSkin(uyp)) zyp = 1e6; if (!IsSkin(uyn)) zyn = 1e6;
  float2 slope = float2(abs(zxp) < abs(zxn) ? zxp : zxn, abs(zyp) < abs(zyn) ? zyp : zyn);
  slope = (abs(slope) > 1e5) ? 0.0 : slope;
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
    if (PesMouthAt(uvS / rb, mmW2, mmH2)) continue;   // (09/11) la boca no da muestras a la piel
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
  // (09/11, Agustin: "el sss puede ser mejor", con el ancho de vuelta en 1.41) la mezcla con la luz sin difundir era la misma en
  // los tres canales: con fuerza 0.73 el rojo conservaba un 27 % de borde duro (penumbra sin el tono rojo de la piel real) y el
  // azul se difundia igual que el rojo (poros y arrugas lavados). Ahora la fuerza va por canal como la dispersion real: el rojo
  // casi entero (y95^0.5: borde de sombra rojizo y suave), el verde como el slider y el azul menos (y95^1.5: detalle fino)
  float sK = saturate(P.y);
  float3 mixK = float3(sqrt(sK), sqrt(sK), sK) * nearK;   // (09/11 h) el verde difunde como el rojo (es el que da el brillo de la piel)
  return float4(lerp(c0, blurred, mixK), 1);
}
#endif
