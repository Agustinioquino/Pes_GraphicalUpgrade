// PES21 FX - pasto en toda la cancha v3 (25/10): la lista de parcelas se arma en la GPU con la camara del MISMO cuadro.
// Una hebra por celda de una grilla fija de la cancha (3 niveles: celdas de 0.5 / 1 / 2 m). Cada celda entra en el nivel
// que le toca segun su tamano en pantalla (pixeles por metro en su centro), se descarta fuera del campo de vision y se
// anota en el buffer de instancias (x, z, escala, tope de tarjetas * 4 + giro).
// (25/10 c) densidad constante en pantalla: la celda lleva las tarjetas que cubren su tamano en pantalla,
//   tarjetas = C(p) * (celda * p)^2 / ancho_px, con ancho_px = max(ancho_m * p, ancho minimo) y C(p) = C base * sqrt(p / 35)
//   (un poco mas tupido hacia la camara, como se ve en TV; acotado porque lo cercano ocupa poca pantalla).
// Cada nivel tiene 6 subniveles (ranuras) con 9248 >> b tarjetas de la malla propia (orden de Halton: las primeras N estan
// bien repartidas); la celda va a la ranura mas chica que alcanza y el VS corta las tarjetas que sobran (tope exacto).
// La pasada 3 escribe los argumentos de DrawIndexedInstancedIndirect (18 dibujos) con los contadores.
cbuffer cVSScene : register(b2) { float4 cb2[21]; }   // la escena del juego tal como la ve el VS: vista cb2[8..11], proyeccion cb2[4..7]
cbuffer PESGrassCull : register(b0) {
  float4 gP0;       // x nivel (0, 1, 2; 3 = escribir los argumentos) | y celda (m) | z pLo (px/m) | w pHi (px/m, 0 = sin tope)
  float4 gP1;       // x mitad del alto de la pantalla (px) | y tope de instancias por ranura | z primera ranura del nivel | w celdas a lo largo
  float4 gP2;       // x celdas a lo ancho | y margen del fundido (fraccion) | z semi largo de la grilla (m) | w semi ancho (m)
  float4 gP3;       // x ancho de la hoja (m) | y ancho minimo (px) | z C base | w indices por tarjeta
  float4 gP4;       // x tarjetas de la malla | y subniveles | z alto de la hoja (m) | w tope de capas de hojas por pixel
  float4 gPad[7];   // [0]: x piso de capas por pixel de suelo (nivel 0) | y, z rampa en px/m | w alto cerca / alto
                    // [1]: x margen del fundido en pHi | y, z, w indices por tarjeta de los niveles 0 / 1 / 2
                    // [2]: (nivel 0) x ancho de la hoja en camaras de juego (m) | y ancho con la camara al ras (m) | z, w rampa en px/m
                    // [4]: x 1 = editor | (11/11 m) y relleno del atlas bajo las hojas 3D (y403; la DLL lo manda solo con las hojas 3D prendidas)
                    // [5]: (11/11) HOJAS 3D: x umbral (px/m; 0 = hojas 3D apagadas en los niveles 0..2) | y hojas de la celda mas cercana (la DLL lo ajusta al total) | z tope total de hojas | w indices por hoja
                    // [6]: (12/11) HOJAS ANGOSTAS de primerisimo plano (x432): x umbral (px/m; 0 = sin escalon angosto) | y hojas angostas por hoja comun | z indices por hoja angosta
}
RWStructuredBuffer<float4> Inst : register(u0);
RWByteAddressBuffer Count : register(u1);   // 18 contadores (uint), uno por ranura; [18] = 1 si esta emision se quedo con el cuadro; [20] trozos de hojas 3D (11/11 n), [21] celdas, [22] hojas pedidas;
                                            // (12/11) [23] suma de hojas pedidas x 16 (pasada 5), [24] trozos de hojas angostas (ranura 19)
RWByteAddressBuffer Args : register(u2);    // 18 x 5 uints: indices, instancias, primer indice, vertice base, primera instancia
// (26/10 d) reclamo del cuadro: [0] emisiones ejecutadas desde el ultimo Present (la DLL lo pone en cero en el contexto inmediato en
// cada Present), [1..18] instancias por ranura de la emision que dibujo, [19] _m11 de su camara, [20..22] ojo. Solo la PRIMERA emision
// que la GPU ejecuta en el cuadro dibuja; las demas (grabadas en otra lista para el mismo cuadro) salen con 0 instancias.
RWByteAddressBuffer Claim : register(u3);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  int lvl = (int)(gP0.x + 0.5);
  uint nb = (uint)(gP4.y + 0.5);
  uint cardsMax = (uint)(gP4.x + 0.5);
  if (lvl == 4) {
    // pasada 0 (una hebra): reclamar el cuadro
    if (id.x == 0 && id.y == 0) { uint prev; Claim.InterlockedAdd(0, 1u, prev); Count.Store(72, prev == 0u ? 1u : 0u); }
    return;
  }
  bool active = Count.Load(72) != 0u;
  if (lvl == 3) {   // (11/11) antes >= 3: la pasada 5 (hojas 3D) es de celdas
    // argumentos del dibujo indirecto: una hebra por ranura, instancias = min(contador, tope), indices = tarjetas del subnivel
    uint s = id.x + id.y * 8;
    if (s < 3 * nb) {
      uint b = s % nb;
      uint lv = s / nb;
      float ipc = lv == 0u ? gPad[1].y : (lv == 1u ? gPad[1].z : gPad[1].w);
      if (ipc < 1.0) ipc = gP3.w;
      uint n = active ? min(Count.Load(s * 4), (uint)(gP1.y + 0.5)) : 0u;
      uint o = s * 20;
      Args.Store(o, (cardsMax >> b) * (uint)(ipc + 0.5));
      Args.Store(o + 4, n);
      Args.Store(o + 8, 0u);
      Args.Store(o + 12, 0u);
      Args.Store(o + 16, 0u);
      if (active) {
        Claim.Store(4 + s * 4, n);
        if (s == 0u) { Claim.Store(76, asuint(cb2[5].y)); Claim.Store(80, asuint(cb2[20].x)); Claim.Store(84, asuint(cb2[20].y)); Claim.Store(88, asuint(cb2[20].z));
                       Claim.Store(92, Count.Load(76)); }   // (02/11 paso 10) celdas perdidas por ranura llena (la DLL lo muestra en 'pasto v3')
      }
    } else if (s == 3 * nb) {
      // (11/11) ranura 18: hojas 3D de primerisimo plano. Cada celda lleva hasta 'per' hojas: el tope total se reparte entre las celdas
      // anotadas (el VS corta por celda segun su fundido, tope de la instancia)
      // (11/11 n) cada instancia es un TROZO de 512 hojas de una celda (las celdas cercanas llevan mas trozos que las lejanas): el VS solo
      // procesa las hojas que se dibujan. El tope total lo ajusta la DLL bajando gPad[5].y con lo que leyo de cuadros anteriores ([26]);
      // aca queda un tope duro de 2x el total (corte de camara de lejos a muy cerca) para no colgar la placa
      uint cap = (uint)(gP1.y + 0.5);
      uint tot = (uint)(gPad[5].z + 0.5);
      uint n = active ? min(min(Count.Load(80), cap), (2u * tot + 511u) / 512u) : 0u;
      uint ipcB = (uint)(gPad[5].w + 0.5); if (ipcB == 0u) ipcB = 36u;
      Args.Store(360, 512u * ipcB);
      Args.Store(364, n);
      Args.Store(368, 0u);
      Args.Store(372, 0u);
      Args.Store(376, 0u);
      if (active) { uint cB = Count.Load(84), sB = Count.Load(88);
        Claim.Store(96, cB); Claim.Store(100, cB > 0u ? sB / cB : 0u); Claim.Store(104, sB); Claim.Store(108, n); Claim.Store(112, asuint(gPad[5].y)); }
    } else if (s == 3 * nb + 1) {
      // (12/11) ranura 19: HOJAS ANGOSTAS (escalon de primerisimo plano), trozos de 512 como la 18, con su malla (gPad[6].z indices por hoja)
      uint cap = (uint)(gP1.y + 0.5);
      uint tot = (uint)(gPad[5].z + 0.5);
      uint n = active ? min(min(Count.Load(96), cap), (2u * tot + 511u) / 512u) : 0u;
      uint ipcN = (uint)(gPad[6].z + 0.5); if (ipcN == 0u) ipcN = 48u;
      Args.Store(380, 512u * ipcN);
      Args.Store(384, n);
      Args.Store(388, 0u);
      Args.Store(392, 0u);
      Args.Store(396, 0u);
      if (active) Claim.Store(116, n);
    }
    return;
  }
  if (!active) return;
  if (id.x >= (uint)(gP1.w + 0.5) || id.y >= (uint)(gP2.x + 0.5)) return;
  if (lvl == 5 || lvl == 6) {
    // (11/11, Agustin: "solo de cerca siiiii", "para primerisimos primer plano nada mas") HOJAS 3D: celdas de 0.5 m (la grilla del nivel 0)
    // que miden >= umbral px/m (rampa 0.8x .. 1.25x); en el editor (gPad[4].x) las celdas a menos de 1.6 m del jugador. Las tarjetas siguen
    // abajo como relleno. Tope por celda = fundido x tope de hojas por celda (el VS corta las que sobran)
    // (12/11, Agustin: "al mover la camara o empezar la repeticion se notan como se descargan y cargan partes") DOS pasadas en el MISMO cuadro:
    // la 5 suma las hojas que piden todas las celdas y la 6 las anota escaladas al tope total. Antes el tope se ajustaba con lo leido de la
    // GPU 2-3 cuadros despues y subia de a 15 % por cuadro: en cada corte de camara las celdas se llenaban de a poco
    // (12/11, Agustin: "unas hojas mas angostas nuevas para los primeros planos, despues a cierta distancia las hojas que ya tenemos reduciendo un
    // poco la cantidad poligonal") ESCALONES: desde gPad[6].x px/m (rampa 0.8x .. 1.25x) la celda lleva HOJAS ANGOSTAS (ranura 19, malla de
    // 5 filas, gPad[6].y por cada hoja comun); mas lejos las hojas 3D de siempre con 3 filas (ranura 18). En la rampa las dos a la vez, cada una
    // con su parte (se disuelven una en la otra hoja por hoja, sin salto)
    float cellB = gP0.y;
    float xB = -gP2.z + ((float)id.x + 0.5) * cellB, zB = -gP2.w + ((float)id.y + 0.5) * cellB;
    float4 PB = float4(xB, 0.0, zB, 1.0);
    float4 cvB = float4(dot(PB, cb2[8]), dot(PB, cb2[9]), dot(PB, cb2[10]), dot(PB, cb2[11]));
    float4 ccB = float4(dot(cvB, cb2[4]), dot(cvB, cb2[5]), dot(cvB, cb2[6]), dot(cvB, cb2[7]));
    float radB = cellB * 0.7071 + 0.15;
    if (ccB.w < -radB) return;
    float wB = max(ccB.w, 0.05);
    float pB = abs(cb2[5].y) * gP1.x / wB;
    float thr = gPad[5].x > 1.0 ? gPad[5].x : 1200.0;
    float kB = gPad[4].x > 0.5 ? 1.0 - smoothstep(1.2, 1.6, length(float2(xB, zB))) : smoothstep(thr * 0.8, thr * 1.25, pB);
    if (kB < 0.01) return;
    float pxB = (abs(cb2[4].x) + abs(cb2[4].z)) * radB, pyB = (abs(cb2[5].y) + abs(cb2[5].z)) * radB;
    if (ccB.x < -wB - pxB || ccB.x > wB + pxB || ccB.y < -wB - pyB || ccB.y > wB + pyB) return;
    // (11/11 n, Agustin: "dejarme subir la distancia" + "partes donde los manojos 3D no llegan a tapar") las celdas pegadas a la camara
    // (>= 3x el umbral) llevan todas las hojas; hacia el umbral bajan hasta 35 %: al alejar las hojas 3D no se ralean las de adelante
    float NvB = kB * max(gPad[5].y, 1.0);
    if (gPad[4].x < 0.5) NvB *= lerp(0.35, 1.0, smoothstep(thr, thr * 3.0, pB));
    float kN = (gPad[6].x > 1.0 && gPad[4].x < 0.5) ? smoothstep(gPad[6].x * 0.8, gPad[6].x * 1.25, pB) : 0.0;
    float nMul = gPad[6].y > 0.05 ? gPad[6].y : 1.0;
    float n0 = NvB * kN * nMul, n1 = NvB * (1.0 - kN);   // angostas / comunes
    if (lvl == 5) {   // pasada de suma (x16: cuarto de hoja, entero)
      uint dS; Count.InterlockedAdd(92, (uint)((n0 + n1) * 16.0 + 0.5), dS);
      return;
    }
    // escala al tope total, la misma para todas las celdas del cuadro (sin demora)
    float sumB = (float)Count.Load(92) / 16.0;
    float scB = sumB > gPad[5].z ? gPad[5].z / sumB : 1.0;
    n0 *= scB; n1 *= scB;
    uint capB = (uint)(gP1.y + 0.5), dumB;
    uint rotB = ((id.x * 7u + id.y * 13u) ^ (id.x * id.y)) & 3u;
    Count.InterlockedAdd(84, 1u, dumB);
    [unroll] for (uint lB = 0u; lB < 2u; lB++) {
      float Nq = lB == 0u ? n1 : n0;
      if (Nq < 0.5) continue;
      uint limB = (uint)ceil(Nq - 0.0001); limB = clamp(limB, 1u, 16384u);
      float frB = saturate(Nq - (float)(limB - 1u));
      uint nCh = (limB + 511u) / 512u, idxB;
      Count.InterlockedAdd(lB == 0u ? 80 : 96, nCh, idxB);
      Count.InterlockedAdd(88, limB, dumB);
      // w = (trozo * 32768 + hojas de la celda) * 4 + giro (entero exacto en float hasta 2^24)
      for (uint jB = 0u; jB < nCh; jB++) {
        if (idxB + jB >= capB) break;
        Inst[(3u * nb + lB) * capB + idxB + jB] = float4(xB, zB, 1.0 + frB * 0.5, (float)((jB * 32768u + limB) * 4u + rotB));
      }
    }
    return;
  }
  float cell = gP0.y;
  float x = -gP2.z + ((float)id.x + 0.5) * cell, z = -gP2.w + ((float)id.y + 0.5) * cell;
  float4 P = float4(x, 0.0, z, 1.0);
  float4 cv = float4(dot(P, cb2[8]), dot(P, cb2[9]), dot(P, cb2[10]), dot(P, cb2[11]));
  float4 cc = float4(dot(cv, cb2[4]), dot(cv, cb2[5]), dot(cv, cb2[6]), dot(cv, cb2[7]));
  float rad = cell * 0.7071 + 0.15;
  if (cc.w < -rad) return;                         // detras de la camara
  float w = max(cc.w, 0.05);
  // tamano en pantalla: pixeles por metro en el centro de la celda (el VS funde con el mismo criterio)
  float p = abs(cb2[5].y) * gP1.x / w;
  float m = gP2.y, mH = gPad[1].x > 0.0 ? gPad[1].x : m;   // (26/10 d) margen propio en pHi (nivel 0: 0.12 contra el pasto del juego)
  if (p < gP0.z * (1.0 - m)) return;
  if (gP0.w > 0.0 && p >= gP0.w * (1.0 + mH)) return;
  // fundido entre niveles por cantidad: el nivel saliente queda con lim * k y el entrante con lim * (1 - k) (suman 1)
  float kf = (gP0.z > 0.0 ? smoothstep(gP0.z * (1.0 - m), gP0.z * (1.0 + m), p) : 1.0) * (gP0.w > 0.0 ? 1.0 - smoothstep(gP0.w * (1.0 - mH), gP0.w * (1.0 + mH), p) : 1.0);
  if (kf < 0.002) return;
  // descarte por pantalla con margen del tamano de la celda (como el VS)
  float px = (abs(cb2[4].x) + abs(cb2[4].z)) * rad, py = (abs(cb2[5].y) + abs(cb2[5].z)) * rad;
  if (cc.x < -w - px || cc.x > w + px || cc.y < -w - py || cc.y > w + py) return;
  // tarjetas que cubren la celda en pantalla
  float pc = clamp(p, 4.0, 2000.0);
  // (26/10 e) nivel 0: el ancho que se cuenta sigue la misma rampa que el VS (ancho de juego -> ancho al ras entre gPad[2].z y .w px/m)
  // (02/11 paso 4) ancho continuo en los 3 niveles: gPad[2] se carga una vez fuera del bucle de niveles, vale tambien en 1/2
  float wM = (gPad[2].x > 0.0 && gPad[2].z > 1.0) ? lerp(gPad[2].x, gPad[2].y, smoothstep(gPad[2].z, max(gPad[2].w, gPad[2].z + 1.0), pc)) : gP3.x;
  float wpx = max(wM * pc, gP3.y);
  float C = clamp(gP3.z * sqrt(pc / 35.0), 0.05, 2.0);
  float cards = C * (cell * pc) * (cell * pc) / wpx;
  float kH = 1.0, kD = 1.0;
  // con la camara baja la celda no se ve de frente: ocupa una banda de (celda * p) x (celda * p * sen(elevacion) + alto de la hoja);
  // tope de capas de hojas por pixel sobre esa banda (si no, en las repeticiones cercanas el solapamiento llegaba a 14-46x)
  {
    float3 eye = cb2[20].xyz;
    float dist = max(length(float3(x, 0.0, z) - eye), 0.5);
    // (08/11, Agustin: "reemplazar el pasto ... por este de cerca y eliminar el otro que consume mucho") z58 = alcance del pasto del mod
    // en metros desde la camara (gPad[3].w; 0 = toda la cancha): mas alla no se emiten hojas y la base pone el cesped lejano
    float Dm = gPad[3].w;
    // (08/11 i) gPad[4].x = 1 (editor): el alcance se mide desde el jugador (origen), no desde la camara
    float dR = gPad[4].x > 0.5 ? length(float2(x, z)) : dist;
    if (Dm > 1.0) { if (dR > Dm + rad) return; kD = 1.0 - smoothstep(Dm * 0.7, Dm, dR); }
    float sinE = clamp(abs(eye.y) / dist, 0.03, 1.0);
    // (26/10 d) alto de la hoja cerca de la camara (el VS funde de x59 al alto cercano entre 110 y 250 px/m, igual que aca)
    float hP0 = gPad[2].z > 1.0 ? gPad[2].z : 110.0, hP1 = gPad[2].w > hP0 ? gPad[2].w : 250.0;   // (26/10 e) misma rampa que el VS
    float hM = gP4.z * (gPad[0].w > 0.0 ? lerp(1.0, gPad[0].w, smoothstep(hP0, hP1, pc)) : 1.0);
    // (11/11 m) nivel 0 en la zona de las hojas 3D: el VS 498b30 deja las tarjetas como relleno al 48-90 % del alto (y403): se cuentan con
    // ese alto (69 % en promedio) para que cubran lo mismo (mas tarjetas, mas bajas)
    if (lvl == 0 && gPad[5].x > 1.0 && gPad[4].y > 0.001 && gPad[4].x < 0.5)
      hM *= lerp(1.0, 0.69, smoothstep(gPad[5].x * 0.8, gPad[5].x * 1.25, pc) * saturate(gPad[4].y));
    float hpx = max(hM * pc, 1.0);
    // (02/11 paso 4) recorte por alto en pantalla (gPad[3].y / .z px, por defecto 1.2 / 3.0): una hoja de < 1.2 px de alto no se dibuja,
    // entre 1.2 y 3 px se ralea en rampa; lejos queda el cesped plano de la base (PESFarTurf), que empalma con su kFar
    float hA = gPad[3].y > 0.0 ? gPad[3].y : 1.2, hB = gPad[3].z > hA ? gPad[3].z : 3.0;
    kH = smoothstep(hA, hB, hM * pc);
    float band = (cell * pc) * (cell * pc * sinE + hpx);
    // (02/11 tanda 15, paso 1) tope de capas por pixel mas bajo en las camaras de juego: por debajo de la rampa de cerca (gPad[0].y..z px/m)
    // el tope manda la cuenta (a 200 px/m: 7167 tarjetas por C, ~1500 por el tope) y con Ultra eran 9.6 capas por pixel; ahora capK (gPad[3].x,
    // 0.6 si la DLL no lo manda = 5.8 capas) y sube a 1.0 al llegar a la rampa: el primer plano al ras queda igual
    float capK = lerp(gPad[3].x > 0.0 ? gPad[3].x : 0.6, 1.0, smoothstep(gPad[0].y, gPad[0].z, pc));
    float capB = gP4.w * capK * band;
    // (26/10 d) piso de capas por pixel de SUELO (nivel 0, rampa de gPad[0].y a .z px/m): el tope por banda solo dejaba 6 capas
    // sobre la banda, 3-15x menos hojas por m2 que el pasto denso del juego que habia antes en esa zona
    float Dn = gPad[0].x * smoothstep(gPad[0].y, gPad[0].z, pc);
    float capF = Dn * (cell * pc) * (cell * pc) * sinE;
    cards = min(cards, max(capB, capF) / (wpx * hpx));
  }
  cards = clamp(cards * kf * kH * kD, 0.0, (float)cardsMax);
  if (cards < 0.5) return;
  // (26/10 d) fundido por ventana en el VS (las ultimas 20% de las tarjetas bajan de altura de forma escalonada): la cuenta
  // visible media queda igual a 'cards' (antes solo la ultima tarjeta era fraccionaria: al mover la camara aparecian y
  // desaparecian hojas enteras)
  float Nv = min((cards * 0.2 >= 1.0) ? cards / 0.9 : cards + 0.5, (float)cardsMax);
  uint lim = (uint)ceil(Nv - 0.0001);
  float fr = saturate(Nv - (float)(lim - 1u));
  uint b = (uint)clamp((int)floor(log2((float)cardsMax / (float)lim)), 0, (int)nb - 1);   // la ranura mas chica con >= tarjetas
  uint slot = (uint)(gP1.z + 0.5) + b;
  uint idx;
  Count.InterlockedAdd(slot * 4, 1u, idx);
  if (idx >= (uint)(gP1.y + 0.5)) { Count.InterlockedAdd(76, 1u, idx); return; }   // tope de la ranura; (02/11 paso 10) se cuenta la celda perdida
  uint rot = ((id.x * 7u + id.y * 13u) ^ (id.x * id.y)) & 3u;
  // z = escala de la celda (1 / 2 / 4) + fraccion de la ultima tarjeta * 0.5
  Inst[slot * (uint)(gP1.y + 0.5) + idx] = float4(x, z, cell / 0.5 + fr * 0.5, (float)(lim * 4u + rot));
}
