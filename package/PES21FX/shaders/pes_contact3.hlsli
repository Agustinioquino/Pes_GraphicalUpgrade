// PES21 (23/11 ru34vc) SOMBRAS DE CONTACTO v3 (x526 = 1; usa la fila 52 como la v2: x fuerza, y largo m, z suavidad, w on)
// Agustin 23/11: "las sombras de contacto estan mal". La v2 (17/10) marchaba 12 pasos con t^2 en METROS: en un primer plano un rayo de
// 35 cm cubre cientos de pixeles y los pasos quedan separados 30-40 px (se saltea dedos, pelo, el borde de la camiseta: sombra cortada
// o con escalones), de lejos los primeros pasos caen en el mismo pixel; la profundidad del rayo se interpolaba lineal (no en 1/z);
// se quedaba con el PRIMER choque (a veces uno debil) y el grosor tenia un corte seco (sombra fantasma con la silueta de lo que esta
// delante, borde duro). La v3, al estilo de las sombras en pantalla de Bend Studio (Days Gone, SIGGRAPH 2023):
//  - el rayo se mide en PIXELES de pantalla: como mucho z526 px (96), repartidos parejo en y526 pasos (16): el mismo detalle cerca y lejos
//  - profundidad del rayo correcta con la perspectiva (se interpola 1/z)
//  - se queda con la oclusion MAS FUERTE del recorrido y con un borde suave del grosor (sin siluetas fantasma)
//  - grosor que crece con la distancia (w526 escala), arranque separado segun la precision de la profundidad
// Devuelve 1 = al sol, 0 = sombra plena. Am / Bm (ndc -> uv) y pxUV (tamano de un pixel en uv) los calcula quien llama FUERA de ramas.
float PESContactShadow3(float2 sC, float2 uvC, float2 Am, float2 Bm, float2 pxUV, float3 Lc, int matType, float skinTag, float jit)
{
  float4 CS = IniParams.Load(int2(52,0)), C3 = IniParams.Load(int2(526,0));
  float csK = max(CS.x, 0.0);
  if (csK <= 0.0) return 1.0;
  float lenM = clamp(CS.y > 0 ? CS.y : 0.35, 0.05, 1.5);
  float soft = saturate(CS.z);
  float nSt = clamp(C3.y > 0 ? C3.y : 16.0, 8.0, 32.0);
  float maxPx = clamp(C3.z > 0 ? C3.z : 96.0, 16.0, 256.0);
  float thkK = C3.w > 0 ? C3.w : 1.0;
  float4 pp = g_psScene.m_projectionParam; float2 cco = g_psScene.m_cameraCenterOffset.xy;
  float dC = g_DepthTexture.SampleLevel(g_samplerPoint_Wrap_s, uvC, 0).x;
  float zC = pp.z / (dC - pp.w);
  float zSgn = zC < 0.0 ? -1.0 : 1.0, zA = abs(zC);
  // (23/11 ru34vc f) 538 SOMBRA LARGA EN EL PASTO (Agustin: la sombra de los jugadores en el pasto "un poco borrosa, dentada, se pierde
  // a lo lejos el detalle"): de lejos el shadow map tiene pocos texeles por jugador. En el pasto el rayo se alarga a y538 metros (toda la
  // sombra de un jugador, no solo el contacto) y hasta z538 px en pantalla, con las hojas sin tapar: la sombra de lo que se ve en pantalla
  // sale con el detalle de la pantalla y se suma (la mas oscura de las dos) a la del shadow map. Se apaga a w538 metros de la camara.
  float4 C8 = IniParams.Load(int2(538,0));
  bool longT = matType == 3 && C8.x > 0.5;
  float farZ = longT ? clamp(C8.w > 1.0 ? C8.w : 120.0, 20.0, 250.0) : 80.0;
  if (zA < 0.1 || zA > farZ) return 1.0;
  bool cloth = (matType == 0) && (abs(skinTag - 0.9607843) < 0.002);
  float len = cloth ? min(lenM, 0.10) : lenM;
  if (longT) { len = max(len, clamp(C8.y > 0.1 ? C8.y : 2.0, 0.3, 6.0)); maxPx = max(maxPx, clamp(C8.z > 8.0 ? C8.z : 160.0, 16.0, 400.0)); }
  float thBase = (cloth ? 0.05 : (longT ? 0.25 : 0.12)) * thkK;   // un jugador visto de frente tiene ~25 cm de grueso detras de lo que se ve
  // (23/11 19:20 ru34vc g) 539 LA CARA. Agustin: "que no sean manchas negras sobre el rostro". En la piel el rayo de 35 cm y el grosor de 12 cm
  // hacian que la nariz, la ceja, el pelo y la oreja dejaran parches enteros a la sombra (la v3 toma lo MAS oscuro del recorrido). En la piel:
  // rayo corto (y539 cm, 3), grosor de 2.5 cm (solo tapan rasgos finos pegados), penumbra ancha y como mucho x539 de oscuridad (0.4): el
  // shadow map y la sombra propia de la cabeza ya hacen las sombras grandes de la cara; esto solo marca el contacto.
  bool skinR = (matType == 0) && (abs(skinTag - 0.98039) < 0.003);
  float4 C9 = IniParams.Load(int2(539,0));
  if (all(C9 == 0.0)) C9 = float4(0.4, 3.0, 0.0, 1.0);   // fila sin cargar (DLL anterior): el ajuste igual va, con sus valores de fabrica
  float skinK = 1.0;
  if (skinR && C9.w > 0.5) {
    len = min(len, clamp(C9.y > 0.2 ? C9.y : 3.0, 0.5, 15.0) * 0.01);
    thBase = 0.025 * thkK;
    soft = max(soft, 0.8);
    skinK = saturate(C9.x >= 0.0 ? C9.x : 0.4);
    if (skinK <= 0.001) return 1.0;
  }
  bool turfSkip = matType == 3 && (IniParams.Load(int2(299,0)).w > 0.5 || longT);
  float3 Pv = float3((sC - cco) * pp.xy * zC, zC);
  float3 P0 = Pv + Lc * (0.004 + 0.0012 * zA);
  float3 P1 = P0 + Lc * len;
  // que el final del rayo no pase detras de la camara
  float z0s = P0.z * zSgn, z1s = P1.z * zSgn;
  if (z1s < 0.05) { float k = (z0s - 0.05) / max(z0s - z1s, 1e-5); P1 = P0 + (P1 - P0) * saturate(k); }
  float2 u0 = Am * (P0.xy / (P0.z * pp.xy) + cco) + Bm;
  float2 u1 = Am * (P1.xy / (P1.z * pp.xy) + cco) + Bm;
  float pxLen = length((u1 - u0) / max(pxUV, 1e-7));
  if (pxLen < 1.0) return 1.0;
  float tMax = min(1.0, maxPx / pxLen);            // el rayo en pantalla no pasa de maxPx pixeles
  float iz0 = 1.0 / P0.z, iz1 = 1.0 / P1.z;
  float occ = 0.0;
  [loop] for (float i = 0.0; i < nSt; i += 1.0) {
    float t = (i + jit) / nSt * tMax;
    float2 uv = lerp(u0, u1, t);
    if (any(uv < 0.0) || any(uv > 1.0)) break;
    float zR = 1.0 / lerp(iz0, iz1, t);            // profundidad del rayo en ese punto de la pantalla
    float zS = pp.z / (g_DepthTexture.SampleLevel(g_samplerPoint_Wrap_s, uv, 0).x - pp.w);
    float zSa = abs(zS);
    float dz = (zR - zS) * zSgn;                   // > 0: el rayo paso por detras de lo que ve la camara
    float bias = 0.002 + 0.0006 * zSa;
    float thick = thBase + 0.004 * zSa;
    if (dz > bias && dz < thick && (zC - zS) * zSgn > 0.02 + 0.0015 * zA) {
      [branch] if (turfSkip) {                     // (como la v2) en la cancha las hojas no tapan la cancha
        uint mtS = (uint)(g_NormalTexture.SampleLevel(g_samplerPoint_Wrap_s, uv, 0).z * 1023 + 0.5) & 15;
        if (mtS == 3) continue;
      }
      float inA = saturate((dz - bias) / max(0.2 * thick, 1e-4));     // entra suave detras del borde visible
      float outA = saturate((thick - dz) / max(0.35 * thick, 1e-4));  // y se va suave al final del grosor (sin silueta fantasma)
      float pen = 1.0 - soft * saturate(t / max(tMax, 1e-4));        // lejos del pixel = penumbra
      if (longT) pen = 1.0 - 0.45 * soft * saturate(t / max(tMax, 1e-4));   // (538) la sombra larga: mas firme, el borde lo suaviza el TAA
      occ = max(occ, inA * outA * pen);
      if (occ > 0.98) break;
    }
  }
  float fade = saturate((farZ - zA) / (0.25 * farZ));
  float2 eM = saturate(min(uvC, 1.0 - uvC) * 10.0);
  return 1.0 - saturate(occ * fade * csK * skinK * eM.x * eM.y);
}
