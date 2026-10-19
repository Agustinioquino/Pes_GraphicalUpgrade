// PES21 FX - generador de la "quinta textura" de la cara (18/10).
// Se corre UNA vez por cara (la DLL lo dispara cuando aparece una textura de cara nueva) y escribe dos mapas
// en el mismo plano UV que las texturas del juego, derivados de sus 4 texturas:
//   t0 face_bsm_alp (color + alpha de pelo)   t1 face_nrm (normal, x en R*A, y en G)
//   t2 face_srm (R especular, G rugosidad)     t3 face_trm (translucidez en G)
// Salida A (o0): R cavidad (1 = hueco profundo), G curvatura (0.5 plano, >0.5 convexo), B grosor corregido (1 = fino), A vello/pelo
// Salida B (o1): R poros (alta frecuencia del relieve), G zona de brillo (srm), B convexidad grande (orejas/nariz/labios), A = 1 (valido)
Texture2D<float4> tDiffuse : register(t0);
Texture2D<float4> tNormal  : register(t1);
Texture2D<float4> tSrm     : register(t2);
Texture2D<float4> tTrm     : register(t3);
SamplerState sLin : register(s0);

float2 NrmXY(float2 uv, float mip) {
  float4 n = tNormal.SampleLevel(sLin, uv, mip);
  return float2(n.x * n.w, n.y) * 2.0 - 1.0;
}
// divergencia de la normal a una escala (texeles del mip): >0 convexo, <0 concavo
float Div(float2 uv, float mip, float2 texel) {
  float2 d = texel * exp2(mip);
  float dx = NrmXY(uv + float2(d.x, 0), mip).x - NrmXY(uv - float2(d.x, 0), mip).x;
  float dy = NrmXY(uv + float2(0, d.y), mip).y - NrmXY(uv - float2(0, d.y), mip).y;
  // el eje Y del normal map del juego apunta "abajo" en la textura: se compensa
  return (dx - dy) * 0.5;
}
float Ell(float2 uv, float2 c, float2 r, float soft) {
  float d = length((uv - c) / r);
  return saturate(1.0 - (d - (1.0 - soft)) / soft);
}

// Salida C (o2): MAPA DE PELO automatico (mismo formato que el que puede pintar el usuario en PES21FX\hair\<id>.dds):
//   R densidad (0 nada .. 1 maxima), G largo (0..1 del largo del tipo), B direccion de peinado (angulo 0..1 = 0..360 grados
//   en el plano UV: 0 = hacia +U/derecha, 0.25 = hacia +V/abajo, 0.5 = izquierda, 0.75 = arriba), A = 1 en la zona de
//   ceja y 0.9 fuera (casi opaco para que el png se pueda editar; solo lo usa el diagnostico).
// Se calcula por celda de 8x8 texeles (grilla 256x256); el shader de cara decide la semilla con la densidad.
float h21(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
void main(float4 pos : SV_Position, float2 uv : TEXCOORD0, out float4 o0 : SV_Target0, out float4 o1 : SV_Target1, out float4 o2 : SV_Target2)
{
  float w, h; tNormal.GetDimensions(w, h);
  float2 texel = 1.0 / float2(max(w, 1), max(h, 1));

  // --- curvatura a tres escalas ---
  float dFine = Div(uv, 1.0, texel);
  float dMid  = Div(uv, 3.0, texel);
  float dBig  = Div(uv, 5.0, texel);
  float dFine0 = Div(uv, 0.0, texel);
  float cav   = saturate(-(dFine0 * 0.5 + dFine * 0.5 + dMid * 0.35) * 2.2);   // concavo fino = pliegue, ala de nariz, comisura (el medio apenas, para no manchar)
  cav = cav * cav;   // solo los huecos marcados
  float curv  = saturate(0.5 + dMid * 3.0);                            // curvatura media (piel pre-integrada)
  float convB = saturate(dBig * 6.0);                                  // convexidad grande = orejas, punta de nariz, labios

  // --- grosor: translucidez del juego (plana) + anatomia por UV (plano comun de las caras) + convexidad ---
  float trm = tTrm.SampleLevel(sLin, uv, 2.0).y;
  float trmN = saturate((trm - 0.08) * 6.0);
  float anat = 0;
  anat = max(anat, 1.00 * Ell(uv, float2(0.14, 0.46), float2(0.05, 0.11), 0.4));
  anat = max(anat, 1.00 * Ell(uv, float2(0.86, 0.46), float2(0.05, 0.11), 0.4));
  anat = max(anat, 0.80 * Ell(uv, float2(0.50, 0.50), float2(0.06, 0.045), 0.6));
  anat = max(anat, 0.55 * Ell(uv, float2(0.50, 0.44), float2(0.035, 0.07), 0.7));
  anat = max(anat, 0.85 * Ell(uv, float2(0.50, 0.595), float2(0.085, 0.035), 0.5));
  anat = max(anat, 0.65 * Ell(uv, float2(0.40, 0.365), float2(0.07, 0.035), 0.6));
  anat = max(anat, 0.65 * Ell(uv, float2(0.60, 0.365), float2(0.07, 0.035), 0.6));
  anat = max(anat, 0.35 * Ell(uv, float2(0.33, 0.50), float2(0.12, 0.13), 0.8));
  anat = max(anat, 0.35 * Ell(uv, float2(0.67, 0.50), float2(0.12, 0.13), 0.8));
  anat = max(anat, 0.25 * Ell(uv, float2(0.50, 0.26), float2(0.22, 0.10), 0.8));
  anat *= 1.0 - 0.7 * saturate((uv.y - 0.74) * 8.0);
  float thick = saturate(anat * (0.7 + 0.5 * convB) + 0.25 * convB + 0.35 * trmN);

  // --- pelo / vello: alpha del color y alta frecuencia oscura del color ---
  float4 dif0 = tDiffuse.SampleLevel(sLin, uv, 0.0);
  float3 dif3 = tDiffuse.SampleLevel(sLin, uv, 3.0).xyz;
  float3 dif5 = tDiffuse.SampleLevel(sLin, uv, 5.0).xyz;
  float lum0 = dot(dif0.xyz, float3(0.2126, 0.7152, 0.0722));
  float lum3 = dot(dif3, float3(0.2126, 0.7152, 0.0722));
  float lum5 = dot(dif5, float3(0.2126, 0.7152, 0.0722));
  float mx = max(dif0.r, max(dif0.g, dif0.b)), mn = min(dif0.r, min(dif0.g, dif0.b));
  float sat = (mx - mn) / max(mx, 1e-3);
  float ratio = lum0 / max(lum5, 0.005);
  float dark = smoothstep(0.62, 0.38, ratio) * smoothstep(0.15, 0.06, lum0) * smoothstep(0.45, 0.20, sat);   // mismo criterio que el shader de cara
  float hf = abs(lum0 - lum3);
  float hairHF = saturate((hf - 0.035) * 10.0) * smoothstep(0.35, 0.12, lum3);
  float hairA = 1.0 - saturate((dif0.a - 0.90) * 10.0);   // el alpha del color marca las capas de pelo
  float hair = saturate(max(max(dark, hairHF), hairA * 0.8));
  // dentro de la cara (zona central) el alpha no cuenta: solo barba/cejas por color
  float faceZone = Ell(uv, float2(0.5, 0.5), float2(0.33, 0.30), 0.5);
  hair = lerp(hair, max(dark, hairHF), faceZone);

  // --- poros: relieve fino que el SSS de pantalla no debe borrar ---
  float2 n0 = NrmXY(uv, 0.0), n2 = NrmXY(uv, 2.0);
  float pores = saturate(length(n0 - n2) * 6.0) * (1.0 - hair);

  // --- zona de brillo (srm): R especular normalizado contra el promedio de la cara ---
  float4 srm = tSrm.SampleLevel(sLin, uv, 1.0);
  float4 srmAvg = tSrm.SampleLevel(sLin, uv, 6.0);
  float specZone = saturate(0.5 + (srm.r - srmAvg.r) * 1.5);

  o0 = float4(cav * (1.0 - hair), curv, thick * (1.0 - hair), hair);
  o1 = float4(pores, specZone, convB, 1.0);

  // --- semillas de cejas ---
  o2 = float4(0, 0, 0, 0.9);
  float browZoneDbg = 0.0;
  {
    float2 cell = floor(uv * 256.0);
    float2 cuv = (cell + 0.5) / 256.0;
    // zona de cejas (plano UV comun de las caras): dos arcos sobre los ojos
    float browL = Ell(cuv, float2(0.395, 0.312), float2(0.105, 0.030), 0.5);
    float browR = Ell(cuv, float2(0.605, 0.312), float2(0.105, 0.030), 0.5);
    float brow = max(browL, browR);
    browZoneDbg = brow;
    // pelo pintado en la celda: alpha del color (capas de pelo) + oscuridad relativa, en el mip de la celda
    float4 dc = tDiffuse.SampleLevel(sLin, cuv, 3.0);
    float3 dc5 = tDiffuse.SampleLevel(sLin, cuv, 6.0).xyz;
    float lc = dot(dc.xyz, float3(0.2126, 0.7152, 0.0722)), l5 = dot(dc5, float3(0.2126, 0.7152, 0.0722));
    float darkC = smoothstep(0.75, 0.40, lc / max(l5, 0.005));
    float alphaC = 1.0 - saturate((dc.a - 0.90) * 10.0);
    float browHair = brow * saturate(max(darkC, alphaC * 0.8) * 1.3);
    if (browHair > 0.15) {
      float side = cuv.x < 0.5 ? -1.0 : 1.0;
      // direccion: hacia afuera, levemente hacia arriba (UV: -y) siguiendo el arco; ruido de +-15 grados
      float arch = (abs(cuv.x - 0.5) - 0.105) / 0.105;            // -1 en el entrecejo .. +1 en la cola
      float2 dir = normalize(float2(side, -0.45 + 0.55 * saturate(arch + 0.5)));
      float ang = (h21(cell + 1.7) - 0.5) * 0.52;
      float cs = cos(ang), sn = sin(ang);
      dir = float2(dir.x * cs - dir.y * sn, dir.x * sn + dir.y * cs);
      float len = 0.55 + 0.45 * h21(cell + 5.1);
      float angN = frac(atan2(dir.y, dir.x) / 6.2831853 + 1.0);
      o2 = float4(saturate(browHair * 0.95), len, angN, 1.0);
    }
    // zona de ceja: alpha 1 (el diagnostico "semillas sobre la cara" pinta de azul la zona sin semilla)
    if (browZoneDbg > 0.05) o2.a = 1.0;
    // --- (19/10) semillas de CUERO CABELLUDO: pelo con hebras. Fuera de la zona de ceja y de la cara central, donde el
    // color marca pelo (alpha de las capas de pelo o alta frecuencia oscura). Direccion: hacia atras/abajo desde la coronilla
    // (en el plano UV de la cara el pelo sale por arriba y los lados), con ruido. Largo relativo por celda. A = 0.9 (no ceja).
    if (o2.a < 0.95 && browZoneDbg < 0.05) {
      float scalpZone = 1.0 - Ell(cuv, float2(0.5, 0.52), float2(0.30, 0.30), 0.6);   // fuera del ovalo de la cara
      scalpZone *= smoothstep(0.80, 0.70, cuv.y);                                      // no el cuello
      float hairC = saturate(max(alphaC * 0.9, darkC)) * scalpZone;
      if (hairC > 0.25) {
        // direccion: desde la coronilla (0.5, 0.12) hacia afuera y abajo en UV, con ruido de +-25 grados
        float2 dir = normalize(cuv - float2(0.5, 0.12) + float2(0, 0.001));
        float ang = (h21(cell + 3.3) - 0.5) * 0.87;
        float cs = cos(ang), sn = sin(ang);
        dir = float2(dir.x * cs - dir.y * sn, dir.x * sn + dir.y * cs);
        float len = 0.6 + 0.4 * h21(cell + 9.2);
        float angN = frac(atan2(dir.y, dir.x) / 6.2831853 + 1.0);
        o2 = float4(saturate(hairC * 0.9), len, angN, 0.9);
      }
    }
  }
}
