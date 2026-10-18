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

// tinte calido del sol: 0 = blanco calido, 1 = naranja de atardecer (conserva la luminancia)
float3 PESWarmTint(float3 c, float warm)
{
  const float3 LW = float3(0.2126, 0.7152, 0.0722);
  float3 tint = lerp(float3(1.0, 1.0, 1.0), float3(1.0, 0.72, 0.46), warm);
  return c * tint * (dot(c, LW) / max(dot(c * tint, LW), 1e-4));
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

#endif
