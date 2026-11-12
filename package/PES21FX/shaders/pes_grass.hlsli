// PES21 FX - pasto v3 (19/10): funciones compartidas entre la base de la cancha (5e92 / c956), las hojas 3D
// (498b VS + 18af PS) y lo que haga falta. Se incluye DESPUES de declarar IniParams.
//   104: x on | y tipo (0 corto, 1 medio, 2 largo) | z alto | w desgaste (0 profesional .. 1 potrero)
//   105: x patron de corte (0 del juego, 1 franjas a lo largo, 2 a lo ancho, 3 damero, 4 diagonal, 5 rombos, 6 circulos)
//        y ancho de la franja (m) | z contraste del corte | w raiz del atlas (0 abajo, 1 arriba)
//   106: x variacion de color por zonas | y sombra entre hojas | z brillo | w relieve por hoja
//   107: x desgaste en las zonas de uso | y amarillo seco | z pelado (tierra) | w depuracion
#ifndef PES_GRASS_HLSLI
#define PES_GRASS_HLSLI

float PESGh21(float2 p) { p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }
float PESGnoise(float2 p) {
  float2 i = floor(p), f = frac(p); f = f * f * (3.0 - 2.0 * f);
  return lerp(lerp(PESGh21(i), PESGh21(i + float2(1, 0)), f.x), lerp(PESGh21(i + float2(0, 1)), PESGh21(i + 1.0), f.x), f.y);
}

// uv de la cancha -> metros desde el centro. La textura de la cancha (4096x2048) cubre unos 140 x 79 m: la cancha
// de 105 x 68 ocupa u 0.125..0.875 y v 0.07..0.93 (medido en el mapa de lineas del juego). u = largo (x del mundo).
float2 PESPitchMeters(float2 uv) { return (uv - 0.5) * float2(140.0, 79.0); }

float PESBandSign(float a, float w) { return frac(floor(a / w) * 0.5) < 0.25 ? 1.0 : -1.0; }

// patron de corte: el "grano" (vector hacia donde esta peinada la hoja, espacio mundo: x largo de la cancha, z ancho,
// y arriba) para el punto pm (metros). gIn = el del juego (textura de grano). lean = inclinacion (0.55 = la del juego)
float3 PESMowGrain(float2 pm, float3 gIn, float pat, float widthM, float lean)
{
  int p = (int)(pat + 0.5);
  if (p <= 0) return gIn;
  float w = max(widthM, 0.5);
  float2 dir = float2(1.0, 0.0); float s = 1.0;
  if (p == 1) { dir = float2(1.0, 0.0); s = PESBandSign(pm.y, w); }                 // franjas a lo largo: bandas apiladas en z
  else if (p == 2) { dir = float2(0.0, 1.0); s = PESBandSign(pm.x, w); }            // a lo ancho: bandas apiladas en x
  else if (p == 3) { dir = normalize(float2(PESBandSign(pm.y, w), PESBandSign(pm.x, w))); }   // damero (como el del juego)
  else if (p == 4) { float2 d = float2(0.70710678, 0.70710678); dir = d; s = PESBandSign(dot(pm, float2(0.70710678, -0.70710678)), w); }
  else if (p == 5) { // rombos: dos cortes diagonales cruzados
    float s1 = PESBandSign(dot(pm, float2(0.70710678, -0.70710678)), w), s2 = PESBandSign(dot(pm, float2(0.70710678, 0.70710678)), w);
    dir = normalize(float2(0.70710678, 0.70710678) * s1 + float2(0.70710678, -0.70710678) * s2); }
  else { float r = max(length(pm), 0.01); dir = float2(-pm.y, pm.x) / r; s = PESBandSign(r, w); }   // circulos concentricos
  float2 g = dir * s * lean;
  return normalize(float3(g.x, 1.0, g.y));
}

// desgaste: wear 0 = cancha profesional .. 1 = potrero. Devuelve x = desgaste de color (seco, 0..1), y = pelado (tierra, 0..1).
// Las manchas van en metros del mundo (wxz) asi la base y las hojas coinciden; las zonas de mas uso (bocas de arco,
// circulo central) se marcan en metros de la cancha (pm).
// live = desgaste en vivo (mapa que pinta la DLL con los jugadores, 0..1): seca y, al maximo, pela (z115)
// (26/10 f) w292: desgaste en vivo mas visible: el mapa sube muy lento (una pasada de un jugador ~0.01, uno parado ~0.02/s; a los pocos
// minutos casi todo queda < 0.15 = 9-15% de paja, imperceptible). 1 - exp(-5 x): 0.1 -> 0.39, 0.15 -> 0.53, 0.3 -> 0.78
// (12/11) x431 desgaste real: curva PROGRESIVA en S (una pasada suelta casi no se ve; donde se insiste sube rapido y despues se satura): 0.1 -> 0.10,
// 0.2 -> 0.35, 0.3 -> 0.65, 0.45 -> 0.97. El mapa ahora sube por las acciones (frenadas, giros, remates, barridas), no por estar parado
float PESWearLiveCurve(float live) {
  if (IniParams.Load(int2(431, 0)).x > 0.5) { float c = saturate(live * 2.2); return c * c * (3.0 - 2.0 * c); }
  return IniParams.Load(int2(292, 0)).w > 0.5 ? 1.0 - exp(-5.0 * saturate(live)) : saturate(live); }
// (12/11, Agustin: "un slider para probar el desgaste en vivo") x437 PRUEBA. (12/11 15:02, Agustin: "desgasta partes de la cancha que no
// se tocaron aun") ya no pinta un partido fijo: AGRANDA al instante el desgaste que ya hay en el mapa del partido (solo donde se piso o se
// jugo), mas en las zonas que mas se gastan (bocas de arco, punto penal, circulo central, laterales) y en parches (ruido). 0 = como esta;
// 1 = lo pisado ~4x mas gastado; 2 = ~9x (lo poco pisado ya seco, lo mucho pelado). No toca el mapa: al volver a 0 queda lo que habia.
// uv = la del mapa (pm se saca de ahi), wxz = metros del mundo
float PESWearTest(float live, float2 uv, float2 wxz) {
  float t = IniParams.Load(int2(437, 0)).x;
  if (t < 0.001 || live < 1e-4) return live;
  float2 pm = PESPitchMeters(uv), a = abs(pm);
  float gm = exp(-((a.x - 49.5) * (a.x - 49.5) / 9.0 + pm.y * pm.y / 30.0));   // boca del arco (area chica)
  float pk = exp(-((a.x - 41.5) * (a.x - 41.5) / 40.0 + pm.y * pm.y / 70.0));  // punto penal / medialuna
  float cc = exp(-(pm.x * pm.x / 70.0 + pm.y * pm.y / 45.0));                  // circulo central
  float wg = 0.45 * exp(-(a.y - 27.0) * (a.y - 27.0) / 30.0) * step(a.x, 52.5);  // carriles de los laterales
  float use = saturate(gm + 0.8 * pk + 0.75 * cc + wg) * step(a.x, 53.5) * step(a.y, 35.0);
  float n = PESGnoise(wxz * 0.7 + 2.3) * 0.6 + PESGnoise(wxz * 2.9 + 8.1) * 0.4;
  float amp = 1.0 + clamp(t, 0.0, 2.0) * (2.5 + 2.0 * use) * (0.7 + 0.6 * n);
  return saturate(live * amp);
}
// (12/11, Agustin: "el brillo del pasto al mover la camara se nota bastante mas que al estar quieto") x436 BRILLO ESTABLE (0 = como antes,
// 1 = natural, 2 = mas filtrado). PESTipS = smoothstep de los efectos de la punta (cera, rocio, punta encendida) que, cuando un pixel ya cubre
// buena parte de la hoja (aa -> 1), pasa a su PROMEDIO sobre la hoja: lo mismo que deja el TAA con la camara quieta, sin prenderse y
// apagarse pixel a pixel al moverla
float PESStableK() { return clamp(IniParams.Load(int2(436, 0)).x, 0.0, 2.0); }
float PESTipS(float a, float b, float x, float aa) { return lerp(smoothstep(a, b, x), saturate(1.0 - 0.5 * (a + b)), aa); }
bool PESWearLiveOn(float4 LW) { return LW.w > 0.5 || IniParams.Load(int2(437, 0)).x > 0.001; }
float2 PESWear(float2 pm, float2 wxz, float wear, float areas, float bald, float live)
{
  float col = 0.0, bare = 0.0;
  if (wear > 0.001) {
    float n1 = PESGnoise(wxz * 0.22 + 3.1), n2 = PESGnoise(wxz * 0.9 + 7.7), n3 = PESGnoise(wxz * 3.3 + 1.3);
    float n = n1 * 0.55 + n2 * 0.3 + n3 * 0.15;
    float2 ga = abs(pm) - float2(52.5 - 5.0, 0.0);
    float goal = exp(-dot(ga * ga, float2(1.0 / 36.0, 1.0 / 81.0)));
    float centre = exp(-dot(pm * pm, float2(1.0 / 81.0, 1.0 / 49.0)));
    float use = saturate(goal + centre * 0.8) * saturate(areas);
    float k = saturate(wear * (0.6 + 0.8 * use));
    col = saturate((n - 0.5 + k * 0.9) * 1.6);
    float thr = 1.0 - k * 0.7 * saturate(bald);
    bare = smoothstep(thr - 0.12, thr + 0.02, n + use * 0.25) * (bald > 0.001 ? 1.0 : 0.0);
  }
  float4 LW = IniParams.Load(int2(115, 0));
  // (27/10 n) tanda 11, y329: desgaste mas visible. El del partido (w115) sube muy lento y el fijo (w104) suele estar en 0: Agustin no lo notaba.
  // Multiplica el seco y el pelado de los dos (base y hojas a la vez, misma funcion)
  float wearK = 1.0 + clamp(IniParams.Load(int2(329, 0)).y, 0.0, 3.0);
  col = saturate(col * wearK); bare = saturate(bare * wearK);
  if (PESWearLiveOn(LW) && live > 0.001) {
    float n4 = PESGnoise(wxz * 2.1 + 4.4);                       // el pisoteo no es parejo
    float lv = saturate(PESWearLiveCurve(live) * (0.8 + 0.4 * n4));
    // (12/11) x431 desgaste real, por etapas: hasta ~40 % solo mas corto (PESWearLiveHeight) y apenas mas apagado; despues seco (verde paja, no
    // amarillo: "el color raro del desgaste") y desde ~80 % la tierra asoma por manchas (el ruido n4 hace el borde irregular)
    if (IniParams.Load(int2(431, 0)).x > 0.5) {
      // (12/11, Agustin: "el desgaste no que se borre el pasto, que cambie su color, su tamano", "tiene que parecer real") el gastado no es
      // una mancha pareja: se rompe en parches de ~10 cm (unas matas aguantan, otras no) y la tierra solo asoma al final, en motas chicas
      // ENTRE las hojas (ruido de ~3 cm), nunca como un claro liso
      float n5 = PESGnoise(wxz * 9.0 + 1.9), n6 = PESGnoise(wxz * 37.0 + 5.3);
      float lvR = saturate(PESWearLiveCurve(live) * (0.60 + 0.50 * n4 + 0.40 * n5));
      col = saturate(col + smoothstep(0.25, 0.85, lvR) * 0.45 * wearK);
      bare = max(bare, smoothstep(0.88 / wearK, 1.0, lvR) * smoothstep(0.35, 0.65, n6) * 0.7 * saturate(LW.z));
    } else {
    col = saturate(col + lv * 0.6 * wearK);   // (02/11 paso 6) 0.9 -> 0.6: con live ~0.1 ya habia 70 % de paja
    bare = max(bare, smoothstep(0.7 / wearK, 1.0, lv) * saturate(LW.z));   // (02/11 paso 6) pelado desde mas desgaste (antes 0.55)
    }
  }
  return float2(col, bare);
}
float PESWearLiveHeight(float live) { float4 LW = IniParams.Load(int2(115, 0));
  if (PESWearLiveOn(LW) && IniParams.Load(int2(431, 0)).x > 0.5) return 1.0 - 0.55 * smoothstep(0.0, 0.6, PESWearLiveCurve(live));   // (12/11) desgaste real: primero mas corto (hasta 45 % del alto)
  return PESWearLiveOn(LW) ? 1.0 - 0.45 * PESWearLiveCurve(live) : 1.0; }

// variacion de color por zonas (manchas de 2-6 m mas verdes / mas palidas, y detalle de 0.5 m): multiplicador rgb
float3 PESGrassZones(float2 wxz, float k)
{
  if (k <= 0.001) return float3(1.0, 1.0, 1.0);
  float m1 = PESGnoise(wxz * 0.045), m2 = PESGnoise(wxz * 0.16 + 5.3), m3 = PESGnoise(wxz * 0.85 + 1.7);
  float macro = (m1 * 0.55 + m2 * 0.3 + m3 * 0.15) - 0.5;
  float hue = m2 - 0.5;
  float3 t = (1.0 + macro * 0.28 * k) * lerp(float3(1.0, 1.0, 1.0), float3(1.10, 1.0, 0.82), hue * 0.9 * k);
  return t;
}

// color seco (amarillo pajizo) y tierra, con la luminancia del pasto
float3 PESDryTint(float3 c, float dry) {
  float l = dot(c, float3(0.2126, 0.7152, 0.0722));
  return lerp(c, l * float3(1.22, 1.05, 0.52), saturate(dry));
}
float3 PESDirt(float2 wxz) {
  float n = PESGnoise(wxz * 6.0 + 2.2) * 0.5 + PESGnoise(wxz * 23.0) * 0.5;
  return float3(0.34, 0.26, 0.16) * (0.75 + 0.5 * n);
}
// (20/10) estado del suelo: x = humedo (0..1), y = seco (0..1). z110: 0 automatico (lluvia del partido via Sider -> humedo),
// 1 normal, 2 humedo, 3 seco; w110 fuerza. La DLL publica en 114.x el estado automatico (0 normal, 1 humedo, 2 seco)
float2 PESGround()
{
  float4 Q = IniParams.Load(int2(110, 0)); float4 L = IniParams.Load(int2(114, 0));
  int m = (int)(Q.z + 0.5); float wet = 0.0, dry = 0.0;
  if (m == 0) { int a = (int)(L.x + 0.5); wet = a == 1 ? 1.0 : 0.0; dry = a == 2 ? 1.0 : 0.0; }
  else if (m == 2) wet = 1.0; else if (m == 3) dry = 1.0;
  float k = Q.w > 0.001 ? saturate(Q.w) : 1.0;
  return float2(wet * k, dry * k);
}
// pasto mojado: mas oscuro y saturado, brillo de agua; seco: pajizo y mate. c albedo, spec = o2.y, rough = o2.x
void PESGroundLook(inout float3 c, inout float spec, inout float rough, float2 g)
{
  if (g.x > 0.001) { c = lerp(c, pow(max(c, 1e-4), 1.25) * 0.72, g.x); spec = lerp(spec, saturate(spec * 2.2 + 0.25), g.x); rough = lerp(rough, max(0.14, rough * 0.6), g.x); }
  if (g.y > 0.001) { c = PESDryTint(c, g.y * 0.4); spec = lerp(spec, spec * 0.7, g.y); }
}
// sol en espacio mundo (lo publica la DLL en 111: xyz hacia el sol, w altura en grados); (0,1,0) si todavia no hay dato
float3 PESSunWorld() { float4 L = IniParams.Load(int2(111, 0)); return dot(L.xyz, L.xyz) > 0.5 ? L.xyz : float3(0.0, 1.0, 0.0); }
// (04/11) sol del cielo en mundo (fila 312 de la DLL, no gira con la camara en repeticiones); si no hay, el de la luz (111)
float3 PESSunSky() { float4 S = IniParams.Load(int2(312, 0)); return (S.w > 0.5 && S.y > 0.0) ? normalize(S.xyz) : PESSunWorld(); }
// (12/11, Agustin, captura 21.20.40: "el pasto super cercano se ve mucho mas brillante que el demas, puedes hacer que el pasto de lejos
// mantenga ese brillo y color") las hojas 3D salian ~1.6-1.9 veces mas claras que las tarjetas y el suelo por un error de cuenta (su
// oclusion iba como 0.84..1 y el atlas la guarda como la mitad: el PS la duplica). Arreglado eso, x433 = BRILLO DEL NATURAL: una sola
// ganancia para el suelo (5e92 / c956), las tarjetas y las hojas 3D (0 / sin la linea = 1.6, lo que tenian las hojas 3D), con hombro suave
// arriba de 0.75 para que el sol fuerte no lo lave. Solo el natural con el pasto v3
bool PESNatOn() { return IniParams.Load(int2(104, 0)).x > 0.5 && IniParams.Load(int2(407, 0)).x < 0.5 && IniParams.Load(int2(412, 0)).z < 0.5; }
float PESNatGain() { if (!PESNatOn()) return 1.0; float g = IniParams.Load(int2(433, 0)).x; return g > 0.05 ? clamp(g, 0.5, 2.5) : 1.6; }
float3 PESNatShoulder(float3 c) { return c - max(c - 0.75, 0.0) * 0.6; }
// (12/11, Agustin: "los efectos de translucidez y de mojado en algunas puntas se mantienen hasta cierta distancia y se actualizan lo mas en
// vivo posible") lo que las hojas de cerca suman por hoja (la luz del sol que atraviesa la lamina a contraluz, las puntas encendidas, el
// brillo humedo de algunas puntas) el suelo lo lleva como PROMEDIO de la copa: mismo tono de cerca y de lejos, calculado en cada cuadro con
// el sol, la camara y la humedad de ese momento. V = del punto al ojo (mundo). pxm = px/m del punto: se apaga por debajo de z433 px/m
// (0 = 10). y433 = cuanto (0 = nada). spec / rough = o2.y / o2.x
void PESNatCanopy(inout float3 c, inout float spec, inout float rough, float3 V, float pxm) {
  float4 P = IniParams.Load(int2(433, 0));
  if (P.y < 0.001 || !PESNatOn()) return;
  float lim = P.z > 0.5 ? clamp(P.z, 2.0, 200.0) : 10.0;
  float kY = saturate(P.y) * smoothstep(lim * 0.5, lim, pxm);
  if (kY < 0.001) return;
  float4 P414 = IniParams.Load(int2(414, 0)), P114 = IniParams.Load(int2(114, 0));
  float day = P114.w > 0.5 ? 1.0 : 0.0;
  float sunK = saturate(P414.w * 1.2) * day;
  float3 S = PESSunSky();
  float back = saturate(0.5 - 0.5 * dot(normalize(V.xz + 1e-5), normalize(S.xz + 1e-5)));   // mirando hacia el sol = contraluz
  float tx = 0.32 * sunK * (0.28 + 0.82 * back) * clamp(IniParams.Load(int2(413, 0)).w, 0.0, 2.0);   // media de la transmision a lo largo de la hoja
  c = lerp(c, c * float3(1.26, 1.36, 0.86), saturate(tx * 0.95) * kY);
  c *= lerp(float3(1.0, 1.0, 1.0), float3(1.05, 1.06, 0.95), 0.5 * clamp(IniParams.Load(int2(295, 0)).z, 0.0, 2.0) * kY * lerp(0.3, 1.0, saturate(P414.w)));   // puntas
  // puntas mojadas (x329: una de cada ~8 hojas, mas con la cancha humeda, en invierno y al amanecer): brillo promedio
  float2 g = PESGround();
  float dewK = (1.0 + 0.8 * g.x) * (1.0 - 0.85 * g.y) * (abs(P114.z - 1.0) < 0.5 ? 1.4 : 1.0);
  float dew = 0.12 * dewK * clamp(IniParams.Load(int2(329, 0)).x, 0.0, 2.0) * kY;
  spec = saturate(spec + 0.45 * dew); rough = lerp(rough, 0.12, saturate(dew));
}
// (25/10) cesped lejano en la base de la cancha: donde una hoja ya no mide en pantalla (mas alla del nivel medio del pasto
// 3D), la base toma el aspecto del cesped visto de lejos en vez de una textura plana: matas claras / oscuras de 8 y 22 cm
// (atenuadas con el tamano del pixel para que no titilen), cobertura segun el angulo de vista (rasante = se ven las puntas,
// mas claras y amarillas; desde arriba = raices en sombra) y un poco mas de saturacion y brillo rasante.
//   c albedo, spec = o2.y, V = hacia el ojo (mundo), wxz = posicion en el mundo, pxm = pixeles por metro,
//   k fuerza (0..1, ya con el fundido por distancia), clump = matas (z61), cover = cobertura (w61)
void PESFarTurf(inout float3 c, inout float spec, float3 V, float2 wxz, float pxm, float k, float clump, float cover)
{
  if (k <= 0.001) return;
  float px = 1.0 / max(pxm, 1.0);                       // metros por pixel
  float f1 = saturate(1.0 - px / 0.10), f2 = saturate(1.0 - px / 0.30);
  float n1 = PESGnoise(wxz * 12.0 + 7.3) - 0.5, n2 = PESGnoise(wxz * 4.5 + 2.9) - 0.5;
  float n3 = PESGnoise(wxz * 1.6 + 11.0) - 0.5;         // manchas de 60 cm: siempre visibles
  float mat = (n1 * 0.45 * f1 + n2 * 0.35 * f2 + n3 * 0.2) * clamp(clump, 0.0, 2.0);
  float graz = 1.0 - saturate(V.y);                     // 0 desde arriba, 1 rasante
  float cov = saturate(0.30 + 0.70 * pow(graz, 0.7)) * clamp(cover, 0.0, 1.5);
  const float3 LW = float3(0.2126, 0.7152, 0.0722);
  float l = dot(c, LW);
  float3 tips = c * float3(1.08, 1.10, 0.90) * 1.08;    // puntas al aire: mas claras y un poco mas amarillas
  float3 roots = c * 0.74;                              // raices en sombra entre hojas
  float3 g = lerp(roots, tips, cov);
  g *= 1.0 + mat * 0.9;
  g = lerp(dot(g, LW).xxx, g, 1.10);                    // algo mas saturado que la textura plana
  c = lerp(c, saturate(g), k);
  spec = lerp(spec, saturate(spec * (1.0 + 0.5 * graz) + 0.03 * graz), k);
}

// (25/10 b) color del pasto, el mismo para la base y las hojas (fila 112): x matiz (giro del tono en grados, -60..60),
// y verde (acerca el tono a un verde de cesped sano con la misma claridad; negativo = lo aleja), z contraste (alrededor
// del gris medio del cesped). Se aplica despues de la correccion del amarillo (29) y antes del desgaste / cesped lejano.
void PESGrassColorGrade(inout float3 c)
{
  float4 C = IniParams.Load(int2(112, 0));
  if (abs(C.x) < 0.01 && abs(C.y) < 0.001 && abs(C.z) < 0.001 && C.w < 0.001) return;
  const float3 LW = float3(0.2126, 0.7152, 0.0722);
  float l = dot(c, LW);
  if (abs(C.x) > 0.01) {
    float3 yiq = float3(l, dot(c, float3(0.596, -0.274, -0.322)), dot(c, float3(0.211, -0.523, 0.312)));
    float a = radians(clamp(C.x, -60.0, 60.0)); float ca = cos(a), sa = sin(a);
    float2 iq = float2(yiq.y * ca - yiq.z * sa, yiq.y * sa + yiq.z * ca);
    c = max(float3(yiq.x + 0.956 * iq.x + 0.621 * iq.y, yiq.x - 0.272 * iq.x - 0.647 * iq.y, yiq.x - 1.106 * iq.x + 1.703 * iq.y), 0.0);
  }
  if (abs(C.y) > 0.001) {
    float3 g = float3(0.19, 0.42, 0.10); g *= l / dot(g, LW);
    c = max(lerp(c, g, clamp(C.y, -1.0, 1.0) * 0.6), 0.0);
  }
  if (abs(C.z) > 0.001) { float k = 1.0 + clamp(C.z, -0.8, 1.0); c = max((c - 0.18) * k + 0.18, 0.0); }
  // (27/10 n) tanda 11, w112 "verde natural": el cesped real al sol con cielo despejado (foto de Agustin, pelota sobre la linea) tiene
  // r/g 0.75-0.80 y b/g 0.3-0.5; el del mod daba r/g 0.40 y b/g 0.25 (verde saturado y frio, "no humano"). El "Verde" (y112) acerca a
  // (0.19, 0.42, 0.10) = r/g 0.45, aun mas saturado. Esto acerca la cromatica a (0.78, 1, 0.42) conservando la luminancia; 1 = casi todo
  if (C.w > 0.001) { float3 t = float3(0.78, 1.0, 0.42); t *= l / dot(t, LW); c = max(lerp(c, t, clamp(C.w, 0.0, 1.0) * 0.85), 0.0); }
}
#endif
