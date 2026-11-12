// PES21 FX - el sol sobre el pasto (27/10, fila 313), solo en las pasadas del sol y solo en el material tipo 3 (cancha y hojas).
// Referencia: la foto de Agustin (pelota sobre el pasto con sol bajo): cada hoja con su lado al sol y su sombra, la luz que
// atraviesa las hojas amarillo verdosa y el sol bajo dorado.
// x313 sombras entre hojas: trazado corto en pantalla hacia el sol (8 cm) sobre la profundidad de las hojas 3D. Las hojas no
//      escriben el shadow map, asi que sin esto la sombra de una hoja sobre la de atras no existe. Se apaga solo cuando una
//      hoja de ~1 cm ocupa menos de ~1.5 px (lejos: ahi lo hace la oclusion del atlas del pasto)
// y313 la luz que pasa a traves de la hoja (la parte de la translucidez) se tine amarillo verdosa (clorofila)
// z313 sol bajo mas dorado sobre el pasto (de ~10 a ~25 grados de altura)
// w313 on/off. (03/11) w313 entre 1 y 2: 'pasto como manto' (prueba): la luz directa de cada hoja usa su normal mezclada con la de
//      la cancha (w313 - 1 = cuanto). Las hojas 3D tienen normales casi horizontales: con el sol a 39 grados recibian la mitad que
//      el suelo y la cancha al sol quedaba a 0.64x de Ver original (cancha 9, 03/11). 1 = como antes. pass upNL = N cancha . L
// (04/11) z330 / w330: translucidez a contraluz (PESTurfTransmit), fuerza y cuanto queda de lejos
// Se incluye despues de declarar IniParams (lee la fila 111 para la altura del sol).

// (04/11) el sol del CIELO en vista para el pasto. En repeticiones y cortes el motor gira la luz con la camara (rumbo = cielo + 2 x
// (camara + 90), hasta 113 grados en el log del 04/11) y el brillo de las hojas, el contraluz y la sombra entre hojas la seguian (Agustin:
// "when you rotate camera sun obligates grass"). La fila 312 (DLL) trae el sol del cielo en mundo (PESSunStable); se pasa a vista con la
// matriz de la escena (las mismas cuentas que la sombra de las nubes). La sombra del shadow map sigue siendo la del motor
float3 PESTurfSunView(float3 Lv, float4x4 Mv)
{
  float3 vX = float3(Mv._m00, Mv._m01, Mv._m02), vY = float3(Mv._m10, Mv._m11, Mv._m12), vZ = float3(Mv._m20, Mv._m21, Mv._m22);
  float3 Lw = normalize(float3(dot(Lv, vX), dot(Lv, vY), dot(Lv, vZ)));
  float3 Sw = PESSunStable(Lw);
  return normalize(Sw.x * vX + Sw.y * vY + Sw.z * vZ);
}

// difusa del sol sobre el pasto: wrapD es la del motor (directa + envoltura por translucidez)
// trans: translucidez del G-buffer (27/10: las hojas pintadas de cal llevan ~0.1 y no se tinen; las verdes 0.4-1.0)
float3 PESTurfDiffuse(float3 sunC, float rawNdotL, float wrapD, float4 PG, float trans, float upNL)
{
  float dirD = saturate(lerp(rawNdotL, upNL, saturate(PG.w - 1.0)));   // (03/11) manto: hacia la normal de la cancha
  // (06/11, Agustin: "el cesped sigue sin reaccionar a la luz del sol de forma que traspase la luz") la luz que atraviesa la hoja se
  // mide con la N.L propia de la hoja: antes se restaba la directa ya mezclada con el manto (w313 1.7) y la translucidez quedaba en ~0
  float through = max(saturate(wrapD) - saturate(rawNdotL), 0.0);
  float3 leaf = lerp(float3(1.0, 1.0, 1.0), float3(1.0, 0.96, 0.72), saturate(PG.y) * saturate((trans - 0.15) / 0.25));   // (27/10) la mitad: el pasto al sol salia lima
  float4 SW = IniParams.Load(int2(111, 0));
  float sunUp = dot(SW.xyz, SW.xyz) > 0.5 ? SW.y : 1.0;
  float low = 1.0 - smoothstep(0.17, 0.42, sunUp);   // (27/10) solo con el sol bajo de verdad: de ~10 a ~25 grados
  float3 warm = lerp(float3(1.0, 1.0, 1.0), float3(1.07, 1.0, 0.84), saturate(PG.z) * low);
  return (dirD + through * leaf) * sunC * warm;
}

// sombra entre hojas (1 = sin sombra). sC: posicion en pantalla (v0.xy / v0.ww), uvC: uv del G-buffer, Am / Bm: de una a la
// otra (calculados fuera de cualquier if), pix: SV_Position.xy, L: hacia el sol en espacio de vista
float PESTurfMicroShadow(Texture2D<float4> dT, SamplerState sP, float2 sC, float2 uvC, float2 Am, float2 Bm, float2 pix, float3 L,
                         float4 pp, float2 cOff, float rbX, float tim, float k)
{
  float dC = dT.SampleLevel(sP, uvC, 0).x;
  float zC = pp.z / (dC - pp.w);
  float zSgn = zC < 0.0 ? -1.0 : 1.0, zA = abs(zC);   // el espacio de vista del motor tiene z negativa hacia adelante
  if (zA < 0.1 || zA > 40.0) return 1.0;
  float mpp = zA * abs(pp.x) * 2.0 / max(rbX, 1.0);    // metros por pixel
  float fadeT = 1.0 - smoothstep(0.006, 0.02, mpp);
  if (fadeT < 0.01) return 1.0;
  float3 Pv = float3((sC - cOff) * pp.xy * zC, zC);
  float ign = frac(52.9829189 * frac(dot(pix, float2(0.06711056, 0.00583715))) + frac(tim * 7.31 + 0.37));   // rota cada cuadro: el TAA lo promedia
  float3 Q = Pv + L * (0.002 + 0.6 * mpp);
  float occ = 0.0;
  [loop] for (int i = 0; i < 8; i++) {
    float f = (i + ign) / 8.0;
    float t = f * f * 0.08;
    float3 R = Q + L * t;
    if (R.z * zSgn < 0.1) break;
    float2 s = R.xy / (R.z * pp.xy) + cOff;
    float2 uv = Am * s + Bm;
    if (any(uv < 0.0) || any(uv > 1.0)) break;
    float zS = pp.z / (dT.SampleLevel(sP, uv, 0).x - pp.w);
    float dz = (R.z - zS) * zSgn;                         // > 0: el rayo paso por detras de una hoja
    float thick = 0.015 + 0.25 * t + 0.5 * mpp;           // grosor aceptado: una hoja, no un jugador que esta delante
    if (dz > 0.5 * mpp && dz < thick) { occ = 1.0 - 0.6 * f; break; }   // hoja pegada = sombra plena, mas lejos = penumbra
  }
  float2 eM = saturate(min(uvC, 1.0 - uvC) * 10.0);
  // (04/11) antes 1 - occ * k: de cerca (fadeT = 1) el rayo choca con alguna hoja casi siempre y TODA la cancha cercana quedaba a ~0.4x
  // (Agustin: "al acercarme mucho la camara se oscurece"). Ahora la sombra reparte la luz en vez de quitarla: la hoja tapada baja (la hoja
  // de delante deja pasar algo, no es negra) y la que ve el sol sube un poco, asi el promedio queda como de lejos y se ve el contraste
  float kk = fadeT * saturate(k) * eM.x * eM.y;
  // (11/11 g, Agustin: "el sol afectando a las hojas individuales") natural en primerisimo plano (menos de ~1-2 mm por pixel: las hojas 3D):
  // cada hoja tapada queda en la sombra de verdad (0.27, la luz del cielo y el rebote la llenan) y la que ve el sol sube (1.22). Lejos como antes
  float clN = (IniParams.Load(int2(407, 0)).x < 0.5) ? 1.0 - smoothstep(0.0009, 0.0025, mpp) : 0.0;
  return max(1.0 + kk * lerp(0.18 - 0.62 * occ, 0.22 - 0.95 * occ, clN), 0.0);
}

// (04/11) translucidez a contraluz: la luz del sol que ATRAVIESA la hoja hacia la camara (mirando hacia el sol se encienden las puntas).
// La difusa de arriba (through) es la que pasa a la cara de atras en todas las direcciones; esto es el lobulo hacia adelante que depende de
// donde mira la camara. De cerca (pocos mm por pixel) lobulo mas fino y fuerte, con la translucidez por hoja del G-buffer (puntas finas mas,
// cada hoja distinta); de lejos ancho y mas suave (w330). Pedido de Agustin (04/11): "de super buena calidad de cerca y de vistas lejanas
// un poco menos". z330 fuerza (0 = apagado, como antes), w330 cuanto queda de lejos (0..1).
// sC / uvC / dT / sP / pp / cOff / rbX como PESTurfMicroShadow; L hacia el sol en vista; rawNdotL = N.L de la hoja (la cara que mira a la
// camara a contraluz tiene N.L < 0)
float3 PESTurfTransmit(float3 sunC, float2 sC, float2 uvC, Texture2D<float4> dT, SamplerState sP, float4 pp, float2 cOff, float rbX,
                       float3 L, float rawNdotL, float trans, float4 PG)
{
  float4 P330 = IniParams.Load(int2(330, 0));
  float k = clamp(P330.z, 0.0, 3.0);
  if (k < 0.001) return float3(0.0, 0.0, 0.0);
  float dC = dT.SampleLevel(sP, uvC, 0).x;
  float zC = pp.z / (dC - pp.w);
  float zA = abs(zC);
  if (zA < 0.05) return float3(0.0, 0.0, 0.0);
  float mpp = zA * abs(pp.x) * 2.0 / max(rbX, 1.0);
  float nearQ = 1.0 - smoothstep(0.0045, 0.035, mpp);   // 1 = hoja de cerca (4.5 mm por pixel o menos; 04/11 llega ~1.4x mas lejos), 0 = TV y lejos
  float3 V = normalize(float3((sC - cOff) * pp.xy * zC, zC));   // de la camara al punto, como el trazado de la sombra entre hojas
  float VL = dot(V, L);                                  // 1 = mirando hacia el sol
  float ph = saturate(0.5 + 0.5 * VL);
  float p2 = ph * ph, p4 = p2 * p2;
  // (06/11) de lejos el contraluz quedaba en ~5-10 % del sol (lobulo p2 * 0.55, translucidez al cuadrado): en la camara de juego el
  // pasto no se encendia mirando hacia el sol. De lejos: lobulo mas alto con nucleo hacia el sol y la translucidez lineal
  float lobe = lerp(p2 * 0.9 + p4 * p2 * 0.6, p4 * p4 * 1.6 + p4 * 0.35, nearQ);   // de lejos ancho + nucleo (ph^6); de cerca nucleo fino (ph^8) + halo
  float back = lerp(0.75, saturate(0.35 - 0.75 * rawNdotL), nearQ);   // de cerca solo la cara del lado de la sombra; de lejos la normal es la de la cancha
  float tr = saturate((trans - 0.1) / 0.6);              // pintadas (~0.1) nada; verdes 0.4-1.0
  float trK = lerp(tr, tr * tr, nearQ);                  // (06/11) de lejos lineal (una hoja verde 0.4 -> 0.5 en vez de 0.25)
  float3 tint = lerp(float3(1.0, 1.0, 1.0), float3(1.10, 1.0, 0.42), saturate(PG.y));   // clorofila: amarillo verdoso (y313)
  float amt = k * lerp(saturate(P330.w), 1.0, nearQ) * trK * back * lobe;
  // (11/11 h, Agustin: "le falta ese detalle de luz atravesando") natural de cerca: la hoja a contraluz deja pasar 1.7x (lejos igual)
  if (IniParams.Load(int2(407, 0)).x < 0.5) amt *= lerp(1.0, 1.7, nearQ);
  return sunC * tint * amt;
}
