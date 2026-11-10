// PES21 FX (03/11) - modelo de piel compacto para las pasadas del sol que no tienen el bloque completo de las 7 grandes
// (a748 / b8a2 / d514 / de64 / e9c1 / 1f62 / 230d): d0e9d81c (sol sin sombra: el editor la usa para las caras y en el partido
// cubre una parte de la escena) y 7dbbc922 (sol con mascara de pantalla + nubes). Sin esto, en esas pasadas la piel salia con la
// luz del material comun del juego (borde duro, brillo de cera, sin contraluz): la cara se veia distinta segun el modo.
// Mismas formulas y filas que las 7 grandes (3, 5, 34, 76, 95, 103), sin lo que necesita el shadow map (grosor real, penumbra).
// Agustin (03/11): "la piel en general en todos los menus y modos".
#ifndef PES_SKINLIGHT_HLSLI
#define PES_SKINLIGHT_HLSLI

float3 PesSkSoftTerm(float nl, float3 w) {
  w = max(w, 1e-3);
  float3 x = nl.xxx;
  float3 s = (x + w) * (x + w) / (4.0 * w);
  return x >= w ? x : (x > -w ? s : 0.0);
}

// piel del G-buffer: material 0, translucidez > 0 y la marca 250/255 (o2.z de la cara / el cuerpo)
bool PesSkIsSkin(int matType, float trans, float skinTag) {
  bool on = IniParams.Load(int2(95,0)).w > 0.5 || IniParams.Load(int2(3,0)).z > 0.5;
  return on && matType == 0 && trans > 0.004 && abs(skinTag - 0.98039) < 0.003;
}

// difusa: terminador suave por canal (el rojo entra un poco mas en la sombra); con la SSSS (w95) el borde queda angosto porque
// la difusion de verdad la hace esa pasada en milimetros
// zView = distancia en metros: de cerca la SSSS resuelve la difusion (borde angosto); de lejos (menos de un pixel de SSSS) queda
// la piel pre-integrada entera, como en las 7 grandes
// curvatura en pantalla (cuanto gira la normal por metro), igual que pesCurv de las 7 grandes: radio de 2 cm = 1, frente = 0.2.
// Usa derivadas: llamarla FUERA de los if (flujo uniforme)
float PesSkCurv(float3 n, float zView) {
  float mpp = max(abs(zView) * abs(g_psScene.m_projectionParam.x) * 2.0 / max(g_psScene.m_viewportSize.x, 1.0), 1e-6);
  return saturate(length(fwidth(n)) / mpp * 0.02);
}

// curv: PesSkCurv; con la fila 62 (x on, w fuerza) la difusion sigue la curvatura real (Penner 2011): mas en nariz, orejas y
// labios, menos en frente y mejillas. Es lo que le queda a la piel de lejos, donde la SSSS no llega a un pixel
float3 PesSkDiffuse(float rawNdotL, float trans, float transFloor, float zView, float curv) {
  bool uni = IniParams.Load(int2(95,0)).w > 0.5;
  float4 P3 = IniParams.Load(int2(3,0));
  float wd = uni ? 1.5 : (P3.x > 0 ? P3.x : 1.0);
  float3 w = saturate(trans) * wd * float3(1.0, 0.6, 0.45);
  { float4 Q62 = IniParams.Load(int2(62,0)); if (Q62.x > 0.5 && Q62.w > 0.0) w *= lerp(1.0, 0.45 + 1.4 * curv, saturate(Q62.w)); }
  w = min(w * 0.12, float3(0.22, 0.11, 0.07));
  if (uni) {
    float mpp = max(abs(zView) * abs(g_psScene.m_projectionParam.x) * 2.0 / max(g_psScene.m_viewportSize.x, 1.0), 1e-6);
    float rPx = 0.006 * max(IniParams.Load(int2(95,0)).x, 0.3) / mpp;
    w *= 1.0 - 0.85 * saturate((rPx - 1.0) / 5.0);
  }
  float3 d = saturate(PesSkSoftTerm(rawNdotL, w));
  return max(d, min(transFloor, 0.03).xxx);
}

// contraluz direccional (orejas, aletas de la nariz, labios con el sol detras); sin shadow map
float3 PesSkTrans(float3 n, float3 V, float3 L, float trans) {
  float4 P5 = IniParams.Load(int2(5,0)), P34 = IniParams.Load(int2(34,0)), P76 = IniParams.Load(int2(76,0));
  float sssV2 = P76.x > 0.0 ? P76.x : 1.0;
  float3 Lt = normalize(L + n * 0.3);
  float thin = saturate(trans * 1.6);
  if (P34.w > 0.5) {
    float fwd = pow(saturate(dot(V, -Lt)), 4.0);
    float back = saturate(dot(-n, L));
    float3 sigT = float3(1.0, 3.6, 6.5) * (P34.y > 0 ? P34.y : 1.0);
    float3 Tr = exp(-(1.15 - thin) * sigT) * thin;
    float thinGate = lerp(smoothstep(0.45, 0.85, thin), 1.0, saturate(P76.y));
    return PESTransColor(Tr * fwd * (0.6 + 0.4 * back) * thinGate * P5.x * 1.6 * sssV2, P34.z);
  }
  float3 tO = thin * smoothstep(0.45, 0.85, thin) * pow(saturate(dot(V, -Lt)), 3.0) * float3(1.0, 0.24, 0.10) * P5.x;
  return PESTransColor(tO, P34.z);
}

// (04/11 b) contraluz por GROSOR (la de las 5 grandes del sol): densidad del mapa de SSS, perfil de Jimenez, color z34, y solo la
// piel de espaldas a la luz. Con el grosor del atlas (7dbbc) o el estimado por la forma (PesSkThickCurv, sin atlas)
float3 PesSkTransThick(float thickM, float3 n, float3 L, float rawNdotL, float trans) {
  float4 P64 = IniParams.Load(int2(64, 0));
  float thin = saturate(trans * 1.6);
  float dens = (P64.w > 0 ? P64.w : 30.0) * (1.25 - thin);
  float3 Tt = PESTransColor(PESSkinTransmittance(thickM * dens), IniParams.Load(int2(34, 0)).z);
  float backL = smoothstep(0.0, 0.35, dot(L, -n)) * (1.0 - saturate(rawNdotL * 3.0)) * lerp(0.35, 1.0, smoothstep(0.25, 0.75, thin));
  return Tt * backL * P64.z * 0.8;
}

// (04/11 entrecejo) curvatura CONVEXA para el grosor de la contraluz: Agustin vio una linea dura del entrecejo por la nariz, en el
// editor y en el partido, con la luz de atras. PesSkCurv no tiene signo y el pliegue entre la nariz y la mejilla (concavo) contaba
// como piel fina igual que el borde de la oreja. Aca solo cuenta donde las normales se abren (dn . dP > 0). P = posicion en la vista
// en el mismo espacio que n. Usa derivadas: llamarla FUERA de los if
float PesSkCurvConvex(float3 n, float3 P) {
  float3 dPx = ddx(P), dPy = ddy(P);
  float num = dot(ddx(n), dPx) + dot(ddy(n), dPy);
  float den = dot(dPx, dPx) + dot(dPy, dPy);
  return saturate(0.02 * max(num, 0.0) / max(den, 1e-12));
}

// (04/11 b) grosor ESTIMADO por la forma donde no hay atlas (editor / sin pasada de sombras / jugadores lejanos): el radio de
// curvatura (curv 1 = 2 cm) da el ancho del tejido que cruza la luz (oreja, aleta de la nariz, dedos: finos; frente, mejilla:
// gruesos) y el mapa de SSS del juego lo afina donde la piel es delgada. Metros
float PesSkThickCurv(float trans, float curv) {
  float thin = saturate(trans * 1.6);
  float r = 0.02 / max(curv, 0.15);
  return clamp(r * lerp(1.2, 0.35, smoothstep(0.45, 0.9, thin)), 0.004, 0.08);
}

// brillo: el del juego escalado (w3), con el color del sol y el Fresnel de piel (fila 103), mas el lobulo fino GGX por zona (y5, w5)
float3 PesSkSpec(float3 specGame, float3 n, float3 V, float3 L, float roughG, float specInt, float3 lightC, float shadow) {
  float4 P3 = IniParams.Load(int2(3,0)), P5 = IniParams.Load(int2(5,0)), Q103 = IniParams.Load(int2(103,0));
  float3 r1 = specGame * (P3.w > 0 ? P3.w : 1.0);
  if (Q103.w > 0.5) {
    float rim = pow(1.0 - saturate(dot(n, V)), 4.0);
    float3 sunN = PESSunColor(); float sl = max(dot(sunN, float3(0.3, 0.59, 0.11)), 1e-4); sunN /= sl;
    float lumS = dot(r1, float3(0.3, 0.59, 0.11));
    r1 = lerp(r1, lumS * sunN, saturate(Q103.x));
    r1 *= lerp(1.0, lerp(0.55, 1.7, rim), saturate(Q103.y)) * (Q103.z > 0 ? Q103.z : 1.0);
  }
  float3 H = normalize(L + V);
  float nl = saturate(dot(n, L));
  float rS = PESSkinSharpRough(roughG, P5.w); float a2 = rS * rS; float a4 = a2 * a2;
  float nh = saturate(dot(n, H));
  float dd = nh * nh * (a4 - 1) + 1;
  float D = a4 / (3.14159 * dd * dd);
  float F = 0.028 + 0.972 * pow(1 - saturate(dot(V, H)), 5);
  r1 += D * F * nl * 0.25 * P5.y * lightC * shadow * PESSkinSharpOcc(specInt, P5.w);
  return r1;
}
#ifdef PES_SK_FACESHADOW   // el archivo declara pesMouthMask (t43) y g_DepthTexture antes de incluir
// (10/11) SOMBRA FINA DE CADA FOCO SOBRE LA CARA. Agustin: "todas las luces deberian afectar al sss y las sombras faciales",
// "asi tambien como sombras del estadio o de demas objetos". Los focos (luces de estudio del editor, focos del partido) no
// proyectaban ninguna sombra propia sobre la piel: la nariz no sombreaba la mejilla, ni el pelo la frente, ni el menton el cuello.
// Trazado corto en pantalla desde la piel hacia la luz (como las sombras de contacto del sol, fila 52: w on, x fuerza, y largo,
// z suavidad), con el grosor de los rasgos de una cara. La sombra queda en la luz difusa que despues difunde la SSSS (borde
// difuminado y rojizo). PesSkUvMap usa derivadas: llamarla FUERA de los if
// REGLA de la boca: el interior de la boca no cambia. Sin la mascara de la DLL (t43 vacio) o donde marca boca, no hay sombra nueva
float4 PesSkUvMap(float4 clipP, float4 uvP) {
  float2 sC = clipP.xy / clipP.w, uvC = uvP.xy / uvP.w;
  float2 dS = float2(ddx(sC.x), ddy(sC.y)), dU = float2(ddx(uvC.x), ddy(uvC.y));
  dS = (abs(dS) < 1e-9) ? 1e-9 : dS;
  float2 Am = dU / dS;
  return float4(Am, uvC - Am * sC);
}
float PesSkFaceShadow(float4 AB, float4 clipP, float2 pos, float zC, float3 L, float nl, float3 lightC) {
  float4 CS = IniParams.Load(int2(52,0));
  float zSgn = zC < 0.0 ? -1.0 : 1.0, zA = abs(zC);
  // solo donde hay luz que quitar (con 26 a 48 focos por cuadro en el editor, los que casi no aportan no trazan)
  if (CS.w < 0.5 || CS.x <= 0.0 || nl < 0.03 || zA < 0.1 || zA > 12.0 || dot(lightC, 0.3333.xxx) * nl < 0.001) return 1.0;
  { uint mmW = 0, mmH = 0; pesMouthMask.GetDimensions(mmW, mmH);
    if (mmW == 0) return 1.0;
    uint2 mmP = min((uint2)pos, uint2(mmW - 1, mmH - 1));
    float mmZ = pesMouthMask.Load(int3(mmP, 0)), mmD = g_DepthTexture.Load(int3(mmP, 0)).x;
    if (mmZ > 0.0 && abs(mmZ - mmD) <= 1e-6 + 2e-4 * abs(mmD)) return 1.0; }
  float2 sC = clipP.xy / clipP.w;
  float3 Pv = float3((sC - g_psScene.m_cameraCenterOffset.xy) * g_psScene.m_projectionParam.xy * zC, zC);
  float3 Lc = normalize(L);
  float len = clamp(CS.y > 0.0 ? CS.y : 0.35, 0.05, 0.25);
  float soft = saturate(CS.z);
  float2 px = (sC * 0.5 + 0.5) * g_psScene.m_viewportSize.xy;
  float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
  float bias = (0.004 + 0.0012 * zA) * (0.6 + 0.4 / max(nl, 0.12));
  float3 Q = Pv + Lc * bias;
  float occ = 0.0;
  [loop] for (int ci = 0; ci < 10; ci++) {
    float f = (ci + ign) / 10.0;
    float t = f * f * len;
    float3 R = Q + Lc * t;
    float RzA = R.z * zSgn;
    if (RzA < 0.1) break;
    float2 uv = AB.xy * (R.xy / (R.z * g_psScene.m_projectionParam.xy) + g_psScene.m_cameraCenterOffset.xy) + AB.zw;
    if (any(uv < 0.0) || any(uv > 1.0)) break;
    float zS = g_psScene.m_projectionParam.z / (g_DepthTexture.SampleLevel(g_samplerPoint_Wrap_s, uv, 0).x - g_psScene.m_projectionParam.w);
    float dz = (R.z - zS) * zSgn;
    float thick = 0.035 + 0.25 * t + 0.0015 * RzA;
    if (dz > 0.002 + 0.0006 * RzA && dz < thick && (zC - zS) * zSgn > 0.008 + 0.0015 * zA) {
      occ = (1.0 - soft * f) * saturate(dz / (0.35 * thick));
      break;
    }
  }
  // junto al terminador la sombra la da la propia forma (y la difunde la SSSS): el trazado entra de a poco, sin escalones
  occ *= smoothstep(0.03, 0.22, nl) * saturate((12.0 - zA) / 4.0);
  return 1.0 - saturate(occ * CS.x);
}
#endif
#endif
