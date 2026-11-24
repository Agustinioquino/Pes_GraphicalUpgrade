// PES21 (23/11 ru34vc j) 542 LA NOCHE: UNA SOMBRA POR TORRE Y HAZ DE LUZ EN LA BRUMA. Agustin 19:24: "si esas 4 en esta".
// Las pasadas de FOCO / PUNTO (9ce9, d503, c335, 68d5, 142c) corren una vez por luz: de noche, una por torre. El shadow map de cada foco
// tiene pocos texeles por jugador (sombra borrosa o ninguna, segun el foco), asi que:
//  1) SOMBRA POR TORRE: la sombra de contacto v3 (pes_contact3, la misma de las 7 pasadas del sol, con la sombra larga 538 en el pasto)
//     se tira hacia ESTE foco. Cada torre deja su propia sombra en el pasto y en el cuerpo, con el detalle de la pantalla, como en la tele.
//     Fuerza x542 (1 = plena). Usa los mismos ajustes de la fila 52 / 526 / 538 / 539 (en la cara: corta y suave, sin manchas).
//  2) HAZ EN LA BRUMA: luz que el aire devuelve hacia la camara a lo largo del rayo de vista (8 pasos con ruido por pixel, el TAA lo
//     alisa), con el cono, la caida y el color del foco, dispersion hacia adelante (Henyey-Greenstein g 0.45: mirando hacia la torre se
//     ve el haz) y, en los focos con sombra, el shadow map del foco en cada paso (el techo y los jugadores cortan el haz: rayos de luz).
//     Densidad z542 (1 = bruma de noche leve). Va al buffer del brillo (o1), que se suma sin el color del material.
// y542 = distancia minima (m) de la luz a la camara para tocarla (12): las luces de estudio del Editar estan cerca y quedan como antes.
// w542 = haz prendido. Fila sin cargar (DLL anterior): valores de fabrica (1, 12, 1, 1). x542 = 0 y w542 = 0 = como antes.
// Quien incluye: define PES_FOCO_SOMBRA (tiene g_ShadowTexture / g_samplerComparisonLess_Linear_Clmap_s) y/o PES_FOCO_PUNTO
// (luz puntual 142c: caida 1/(d+p1.x)^2 - p1.w, sin cono). Llamar al FINAL de main (fuera de ramas: usa derivadas).
#include "pes_contact3.hlsli"

float PesFocoAtt(float3 S, out float3 l)
{
  float3 d = g_psLight.m_lightSourcePos.xyz - S;
  float d2 = max(dot(d, d), 1e-6), dl = sqrt(d2);
  l = d / dl;
#ifdef PES_FOCO_PUNTO
  float r = max(dl, g_psMaterial.m_materials[7].x) + g_psLight.m_lightSourceParam1.x;
  float r2 = r * r;
  return max(1.0 / r2 - g_psLight.m_lightSourceParam1.w * r2, 0.0);
#else
  float a = min(1.0 / d2 - d2 * g_psLight.m_lightSourceParam1.w, g_psMaterial.m_materials[7].w);
  float c = saturate((dot(g_psLight.m_lightSourceDir.xyz, -l) - g_psLight.m_lightSourceParam2.x) * g_psLight.m_lightSourceParam2.y);
  c = c > 0.0 ? exp2(log2(c) * g_psLight.m_lightSourceParam2.z) : 0.0;
  return max(a, 0.0) * c;
#endif
}

#ifdef PES_FOCO_SOMBRA
// (23/11 ru34vc k) 543 PIEL A CONTRALUZ POR GROSOR REAL EN LOS FOCOS (Agustin 19:24, item 4). De noche no hay atlas de la cabeza (solo
// se arma con el sol), y la contraluz de la oreja, la nariz y los dedos salia del grosor ESTIMADO por la forma y despues la apagaba la
// sombra del propio foco (la oreja se tapa a si misma). Aca se mide el grosor con el shadow map del foco (Jimenez 2010): distancia entre
// lo primero que ve el foco y este pixel. Si lo que tapa es la misma piel fina (menos de z543 cm), la luz que la cruza NO la corta la
// sombra: sale con la transmision por ese grosor (acotada a 0.5-2 veces el de la forma: el texel del foco es grueso), y si lo que tapa
// es otra cosa (otro jugador, la cabeza entera) no pasa luz. Devuelve el peso (0 = sin medida: como antes) y el grosor en metros.
// x543 prendido, y543 fuerza, z543 grosor maximo (cm). Fila sin cargar: (1, 1, 6, 1).
float PesFocoThick(float3 P, float3 L, float2 px, out float thk)
{
  thk = 0.08;
  float4 T = IniParams.Load(int2(543,0));
  if (all(T == 0.0)) T = float4(1.0, 1.0, 6.0, 1.0);
  if (T.x < 0.5 || T.y <= 0.0) return 0.0;
  if (PesSkMouthPx(px)) return 0.0;                  // REGLA de la boca
  float4 c1 = mul(float4(P, 1.0), g_psScene.m_shadowProjection);
  float4 c2 = mul(float4(P + L * 0.1, 1.0), g_psScene.m_shadowProjection);
  if (c1.w <= 1e-4 || c2.w <= 1e-4) return 0.0;
  float iw1 = 1.0 / c1.w, iw2 = 1.0 / c2.w, zw1 = c1.z * iw1, zw2 = c2.z * iw2;
  float den = iw1 - iw2;
  if (abs(den) < 1e-9) return 0.0;                   // proyeccion sin perspectiva: no se mide
  float B = (zw1 - zw2) / den, A = zw1 - B * iw1;    // z/w = A + B/w a lo largo del rayo del foco
  float2 uv = c1.xy * iw1;
  if (any(uv < 0.0) || any(uv > 1.0)) return 0.0;
  float2 tx = 1.0 / max(g_psScene.m_shadowMapResolutions.yy, 1.0);
  float dL = length(g_psLight.m_lightSourcePos.xyz - P), tMin = 1.0;
  [unroll] for (int i = 0; i < 4; i++) {
    float2 o = float2((i & 1) ? 0.5 : -0.5, (i & 2) ? 0.5 : -0.5) * tx;
    float ds = g_ShadowTexture.SampleLevel(g_samplerPoint_Clamp_s, uv + o, 0).x;
    float wS = B / max(abs(ds - A), 1e-7) * sign(ds - A);
    float t = (wS > 0.0 && wS < c1.w * 1.02) ? saturate(1.0 - wS * iw1) * dL : 1.0;   // distancia de lo primero que ve el foco hasta aca (vacio / detras: sin medida)
    tMin = min(tMin, t);
  }
  thk = max(tMin - 0.004, 0.002);                    // 4 mm de sesgo
  float mx = clamp(T.z > 0.0 ? T.z : 6.0, 1.0, 15.0) * 0.01;
  return (1.0 - smoothstep(0.6 * mx, mx, thk)) * saturate(T.y);
}
#endif

void PesFocoNight(float4 v0, float4 v1, float2 px, int mat, float tag, inout float3 dif, inout float3 spe)
{
  float2 sC = v0.xy / v0.ww, uC = v1.xy / v1.ww;
  float2 dS = float2(ddx(sC.x), ddy(sC.y)), dU = float2(ddx(uC.x), ddy(uC.y));
  float2 pxU = abs(dU);
  dS = (abs(dS) < 1e-9) ? 1e-9 : dS;
  float2 Am = dU / dS, Bm = uC - Am * sC;
  float4 N2 = IniParams.Load(int2(542,0));
  if (all(N2 == 0.0)) N2 = float4(1.0, 25.0, 1.0, 1.0);
  float3 Ls = g_psLight.m_lightSourcePos.xyz;
  // (24/11 ru34vc n) Agustin: perdida de FPS. Hay hasta 76 focos por cuadro: solo las TORRES (luces a mas de y542 m, 25) y solo cerca
  // de la camara (40 m) para la sombra; 8 pasos en vez de 16; el haz con 4 pasos y solo en focos con cono.
  if (length(Ls) < clamp(N2.y, 0.0, 200.0)) return;    // luz cerca (estudio del Editar, carteles, luces chicas): como antes
  float jit = frac(52.9829189 * frac(dot(floor(px), float2(0.06711056, 0.00583715))));
  float4 pp = g_psScene.m_projectionParam;
  float dC = g_DepthTexture.SampleLevel(g_samplerPoint_Wrap_s, uC, 0).x;
  float zC = pp.z / (dC - pp.w);
  float3 P = float3((sC - g_psScene.m_cameraCenterOffset.xy) * pp.xy * zC, zC);
  // 1) sombra de esta torre
  [branch] if (N2.x > 0.0 && abs(zC) < 40.0 && IniParams.Load(int2(526,0)).x > 0.5 && IniParams.Load(int2(52,0)).w > 0.5 && max(dif.r, max(dif.g, dif.b)) + max(spe.r, max(spe.g, spe.b)) > 1e-5) {
    float3 Lc = normalize(Ls - P);
    g_cs3Steps = 8.0;
    float cs = PESContactShadow3(sC, uC, Am, Bm, pxU, Lc, mat, tag, jit);
    cs = lerp(1.0, cs, saturate(N2.x));
    dif *= cs; spe *= cs;
  }
  // 2) haz en la bruma
#ifndef PES_FOCO_PUNTO
  [branch] if (N2.w > 0.5 && N2.z > 0.0) {
    float D = length(P);
    D = (D > 0.0 && D < 1e6) ? min(D, 200.0) : 200.0;
    float3 V = (length(P) > 1e-4 && length(P) < 1e6) ? P / length(P) : float3(0, 0, zC < 0.0 ? -1.0 : 1.0);
    float3 acc = 0.0;
    const float g = 0.45, g2 = g * g;
    [loop] for (int i = 0; i < 4; i++) {
      float t = (i + jit) / 4.0 * D;
      float3 S = V * t, l;
      float a = PesFocoAtt(S, l);
      if (a <= 0.0) continue;
      float cT = dot(l, V);                               // mirando hacia la luz = dispersion hacia adelante
      float ph = 0.5 * (1.0 - g2) / (12.5663706 * pow(max(1.0 + g2 - 2.0 * g * cT, 1e-3), 1.5)) + 0.5 * 0.0795775;
      float lit = 1.0;
#ifdef PES_FOCO_SOMBRA
      float4 Sh = mul(float4(S, 1.0), g_psScene.m_shadowProjection);
      if (Sh.w > 1e-4) {
        float3 q = Sh.xyz / Sh.w;
        if (all(q.xy > 0.0) && all(q.xy < 1.0) && q.z > 0.0 && q.z < 1.0) {
          float k = saturate((dot(g_psLight.m_lightSourceDir.xyz, -l) - g_psMaterial.m_materials[6].x) * g_psMaterial.m_materials[6].y);
          lit = 1.0 - g_ShadowTexture.SampleCmpLevelZero(g_samplerComparisonLess_Linear_Clmap_s, q.xy, q.z).x * k * k;
        }
      }
#endif
      acc += a * ph * lit;
    }
    float sig = 0.0008 * clamp(N2.z, 0.0, 4.0);
    float3 hz = acc * (D / 4.0) * sig * g_psLight.m_lightSourceColor.xyz * g_psScene.m_exposure.z * g_psLight.m_lightSourceParam3.x;
    spe = min(spe + max(hz, 0.0), 10000.0);
  }
#endif
}
