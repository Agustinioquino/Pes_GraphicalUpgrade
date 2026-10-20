// PES21 FX - funciones compartidas (se incluye con #include "pes_common.hlsli" DESPUES de declarar IniParams)
// Curva del dia, perfil de transmision de la piel y tamano angular del sol.
#ifndef PES_COMMON_HLSLI
#define PES_COMMON_HLSLI

// ---------------------------------------------------------------- curva del dia (slots 65..67)
// x65 on | y65 sol al amanecer | z65 sol a media manana | w65 sol al mediodia      (multiplicadores de intensidad)
// x66 ambiente amanecer | y66 ambiente mediodia | z66 niebla amanecer | w66 niebla mediodia
// x67 calidez al amanecer | y67 "hora azul" (sol en el horizonte) | z67 angulo del amanecer (grados) | w67 angulo del mediodia
struct PESDay { float sun; float amb; float fog; float warm; float blue; float t; float on; };

PESDay PESDayCurve(float sinElev, float dayK)
{
  PESDay d; d.sun = 1; d.amb = 1; d.fog = 1; d.warm = 0; d.blue = 0; d.t = 1; d.on = 0;
  float4 A = IniParams.Load(int2(65,0));
  if (A.x < 0.5 || dayK <= 0.001) return d;
  float4 B = IniParams.Load(int2(66,0));
  float4 C = IniParams.Load(int2(67,0));
  float elev = degrees(asin(saturate(sinElev)));
  float a0 = clamp(C.z, 1.0, 40.0);
  float a1 = clamp(C.w, a0 + 5.0, 90.0);
  float t = saturate((elev - a0) / (a1 - a0));
  float ts = t * t * (3.0 - 2.0 * t);
  float sun = (t < 0.5) ? lerp(A.y, A.z, smoothstep(0.0, 0.5, t)) : lerp(A.z, A.w, smoothstep(0.5, 1.0, t));
  // con el sol pegado al horizonte la luz directa se apaga de a poco (atmosfera)
  sun *= lerp(0.55, 1.0, smoothstep(0.0, a0, elev));
  d.on = dayK;
  d.t = ts;
  d.sun = lerp(1.0, max(sun, 0.0), dayK);
  d.amb = lerp(1.0, max(lerp(B.x, B.y, ts), 0.0), dayK);
  d.fog = lerp(1.0, max(lerp(B.z, B.w, ts), 0.0), dayK);
  d.warm = saturate(C.x) * (1.0 - ts) * dayK;
  d.blue = saturate(C.y) * (1.0 - smoothstep(0.0, a0, elev)) * dayK;
  return d;
}

// contraste de dia (20/10): x109 (0..1) sube el sol y baja la luz ambiente a la vez, solo con sol real (kSun):
// en las capturas automaticas el lado en sombra de los jugadores quedaba casi tan claro como el lado al sol
float PESDaySunMul(float kSun) { float k = saturate(IniParams.Load(int2(109,0)).x) * saturate(kSun); return 1.0 + 0.5 * k; }
// (20/10) ganancia de la luz ambiente calibrada por el medidor de luz de la DLL (125.x; 1 = sin cambio)
float PESAmbGain() { float g = IniParams.Load(int2(125,0)).x; return g > 0.05 ? g : 1.0; }
float PESDayAmbMul(float kSun) { float k = saturate(IniParams.Load(int2(109,0)).x) * saturate(kSun); return 1.0 - 0.4 * k; }

// tinte calido del sol: 0 = blanco calido, 1 = naranja de atardecer (conserva la luminancia)
float3 PESWarmTint(float3 c, float warm)
{
  const float3 LW = float3(0.2126, 0.7152, 0.0722);
  float3 tint = lerp(float3(1.0, 1.0, 1.0), float3(1.0, 0.72, 0.46), warm);
  return c * tint * (dot(c, LW) / max(dot(c * tint, LW), 1e-4));
}
// PES21 atmosfera fisica (20/10): transmitancia del sol por la atmosfera (Rayleigh + Mie con la masa de aire de
// Kasten-Young), normalizada al cenit: con el sol alto no cambia el color del archivo de luz y, al bajar, vira a naranja
// y luego a rojo como de verdad (la curva del dia a mano solo llega a naranja). Solo el color: la intensidad la sigue
// llevando la curva del dia. 121: x on | y turbiedad (2 aire limpio .. 8 bruma) | z fuerza
float3 PESAtmoSunTint(float sinElev, float turb)
{
  float e = saturate(sinElev); float elevDeg = degrees(asin(e));
  float m = 1.0 / (e + 0.50572 * pow(elevDeg + 6.07995, -1.6364));          // masa de aire relativa (1 al cenit, ~38 en el horizonte)
  float3 tauR = float3(5.8e-6, 13.5e-6, 33.1e-6) * 8000.0;                   // Rayleigh vertical (680 / 550 / 440 nm)
  float3 tauM = (0.02 * max(turb - 1.0, 0.0)) * float3(0.92, 1.0, 1.1);      // Mie (aerosoles): casi gris
  float3 T = exp(-(tauR + tauM) * m);
  float3 Tz = exp(-(tauR + tauM));
  return T / max(Tz, 1e-4);
}
float3 PESSunTintFinal(float3 c, float warm, float sinElev, float kSun)
{
  float4 AT = IniParams.Load(int2(121,0));
  if (AT.x < 0.5) return PESWarmTint(c, warm);
  const float3 LW = float3(0.2126, 0.7152, 0.0722);
  float3 Tn = PESAtmoSunTint(sinElev, AT.y > 0.5 ? AT.y : 2.5);
  float3 t = c * Tn * (dot(c, LW) / max(dot(c * Tn, LW), 1e-4));
  return lerp(c, t, saturate(AT.z > 0.0 ? AT.z : 1.0) * saturate(kSun));
}
// tinte azulado de la luz ambiente en la hora azul
float3 PESBlueTint(float3 c, float blue)
{
  const float3 LW = float3(0.2126, 0.7152, 0.0722);
  float3 tint = lerp(float3(1.0, 1.0, 1.0), float3(0.72, 0.82, 1.0), blue);
  return c * tint * (dot(c, LW) / max(dot(c * tint, LW), 1e-4));
}

// ---------------------------------------------------------------- piel: transmision (perfil de Jimenez 2010)
// d = grosor que atraviesa la luz, ya escalado (0 = nada, ~1 = ya casi no pasa luz). Devuelve cuanta luz pasa por canal.
float3 PESSkinTransmittance(float d)
{
  float dd = -d * d;
  return float3(0.233, 0.455, 0.649) * exp(dd / 0.0064)
       + float3(0.100, 0.336, 0.344) * exp(dd / 0.0484)
       + float3(0.118, 0.198, 0.000) * exp(dd / 0.187)
       + float3(0.113, 0.007, 0.007) * exp(dd / 0.567)
       + float3(0.358, 0.004, 0.000) * exp(dd / 1.99)
       + float3(0.078, 0.000, 0.000) * exp(dd / 7.41);
}

// ---------------------------------------------------------------- sol con tamano angular (Karis 2013)
// La rugosidad efectiva crece con el disco del sol; se devuelve la rugosidad nueva y la normalizacion de energia.
float PESSunWidenAlpha(float alpha, float sunDeg, out float energy)
{
  float r = radians(max(sunDeg, 0.0)) * 0.5;
  float a2 = saturate(alpha + 0.5 * r);
  energy = (alpha * alpha) / max(a2 * a2, 1e-6);
  return a2;
}

// ---------------------------------------------------------------- nubes volumetricas (slots 83..86)
// x83 on | y83 cobertura 0..1 | z83 densidad | w83 altura de la base (m)
// x84 espesor (m) | y84 viento (m/s) | z84 direccion del viento (grados) | w84 tamano de las nubes (m)
// x85 luz del sol en la nube | y85 luz ambiente en la nube | z85 sombra de nubes en el suelo 0..1 | w85 pasos (calidad)
// x86 forma: 0 cumulo .. 1 estrato | y86 invertir eje vertical | z86 invertir adelante/atras | w86 tiempo propio (0 = reloj del juego)
float PESHash1(float3 p)
{
  p = frac(p * 0.3183099 + float3(0.1, 0.17, 0.23));
  p *= 17.0;
  return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float PESNoise3(float3 x)
{
  float3 i = floor(x); float3 f = frac(x); f = f * f * (3.0 - 2.0 * f);
  return lerp(lerp(lerp(PESHash1(i + float3(0,0,0)), PESHash1(i + float3(1,0,0)), f.x),
                   lerp(PESHash1(i + float3(0,1,0)), PESHash1(i + float3(1,1,0)), f.x), f.y),
              lerp(lerp(PESHash1(i + float3(0,0,1)), PESHash1(i + float3(1,0,1)), f.x),
                   lerp(PESHash1(i + float3(0,1,1)), PESHash1(i + float3(1,1,1)), f.x), f.y), f.z);
}
float PESFbm3(float3 p, int oct)
{
  float a = 0.5, s = 0.0, n = 0.0;
  [loop] for (int i = 0; i < oct; i++) { s += a * PESNoise3(p); n += a; p = p * 2.03 + 11.7; a *= 0.5; }
  return s / max(n, 1e-4);
}
struct PESCloudParams { float on; float cover; float dens; float hBase; float thick; float3 wind; float scale; float shape; float t; };
PESCloudParams PESCloudLoad(float tGame)
{
  PESCloudParams c;
  float4 A = IniParams.Load(int2(83,0)), B = IniParams.Load(int2(84,0)), D = IniParams.Load(int2(86,0));
  c.on = A.x; c.cover = saturate(A.y); c.dens = A.z > 0 ? A.z : 1.0;
  c.hBase = A.w > 100.0 ? A.w : 1500.0;
  c.thick = B.x > 100.0 ? B.x : 1200.0;
  float ang = radians(B.z);
  c.wind = float3(cos(ang), 0.0, sin(ang)) * max(B.y, 0.0);
  c.scale = B.w > 100.0 ? B.w : 2500.0;
  c.shape = saturate(D.x);
  c.t = D.w > 0.0 ? D.w : tGame;
  return c;
}
// densidad en un punto del mundo (m): 0 = aire, ~1 = nube densa. hN = altura relativa dentro de la capa (0 base .. 1 tope)
float PESCloudDensity(float3 pW, PESCloudParams c, int oct)
{
  float hN = saturate((pW.y - c.hBase) / c.thick);
  float3 q = (pW + c.wind * c.t) / c.scale;
  q += float3(0.0, c.t * 0.004, c.t * 0.002);   // evolucion lenta de la forma (turbulencia), ademas del viento
  q.y *= lerp(2.5, 1.2, c.shape);                 // cumulos: mas altos que anchos; estratos: capas chatas
  float base = PESFbm3(q, oct);
  // perfil vertical: base plana y tope redondeado (cumulo) o capa uniforme (estrato)
  float prof = lerp(smoothstep(0.0, 0.12, hN) * (1.0 - smoothstep(0.45, 1.0, hN)), smoothstep(0.0, 0.08, hN) * (1.0 - smoothstep(0.7, 1.0, hN)), c.shape);
  float cov = lerp(0.72, 0.30, c.cover);          // umbral: mas cobertura = umbral mas bajo
  float d = saturate((base - cov) / max(1.0 - cov, 0.05)) * prof;
  // erosion en los bordes con detalle fino
  if (d > 0.001 && oct >= 4) { float det = PESFbm3(q * 6.3 + 3.1, 2); d = saturate(d - (1.0 - d) * det * 0.35); }
  return d * c.dens;
}
// sombra de nubes sobre un punto del mundo: transmitancia 0..1 a lo largo de la direccion del sol (2 muestras en la capa)
float PESCloudShadow(float3 pW, float3 sunW, PESCloudParams c)
{
  if (c.on < 0.5 || sunW.y < 0.03) return 1.0;
  float tb = (c.hBase - pW.y) / sunW.y;
  float3 p0 = pW + sunW * (tb + c.thick * 0.25 / sunW.y);
  float3 p1 = pW + sunW * (tb + c.thick * 0.60 / sunW.y);
  float3 p2 = pW + sunW * (tb + c.thick * 0.85 / sunW.y);
  float d = PESCloudDensity(p0, c, 3) + PESCloudDensity(p1, c, 3) + PESCloudDensity(p2, c, 3);
  return exp(-d * 3.0);   // 18/10 b: mas opaca (antes 1.6 con 2 muestras: casi no se notaba)
}


// ---------------------------------------------------------------- tono de piel (20/10, 140)
// Indice de melanina 0 (piel muy clara) .. 1 (piel muy oscura) desde el albedo lineal de la piel: la luminancia de la piel
// real va de ~0.47 (muy clara) a ~0.065 (muy oscura); en escala logaritmica, como la percibe el ojo.
float PESMelanin(float3 alb) {
  float L = dot(alb, float3(0.2126, 0.7152, 0.0722));
  return saturate(log2(0.47 / max(L, 1e-3)) / 2.85);
}
// Lugar de la piel ("skin locus"): todas las pieles humanas caen en una franja angosta de cromaticidad (R > G > B, matiz
// ~20-25 grados) que se corre de rosado-palido a marron-calido con la melanina (mediciones de reflectancia, p. ej. la base
// de Weyrich 2006). Las texturas del juego y la luz del estadio a veces la sacan de esa franja (naranja, amarillo, gris
// verdoso). Se acerca la cromaticidad al lugar de la piel PARA ESA MISMA CLARIDAD (el tono propio de cada jugador se
// conserva) y solo en pixeles que ya parecen piel (barba, cejas y pelo pintados quedan como estan).
float3 PESSkinLocus(float3 alb, float k) {
  float sum = alb.r + alb.g + alb.b;
  if (sum < 0.03 || k <= 0.0) return alb;
  float L = dot(alb, float3(0.2126, 0.7152, 0.0722));
  float m = PESMelanin(alb);
  const float3 cL = float3(0.437, 0.310, 0.253), cM = float3(0.493, 0.301, 0.206), cD = float3(0.522, 0.289, 0.189);
  float3 tgt = m < 0.5 ? lerp(cL, cM, m * 2.0) : lerp(cM, cD, m * 2.0 - 1.0);
  float3 c = alb / sum;
  float w = saturate(k) * saturate(1.0 - length(c - tgt) / 0.15);
  float3 o = lerp(c, tgt, w) * sum;
  return o * (L / max(dot(o, float3(0.2126, 0.7152, 0.0722)), 1e-4));
}
#endif
