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
  float4 gPad[5];
}
RWStructuredBuffer<float4> Inst : register(u0);
RWByteAddressBuffer Count : register(u1);   // 18 contadores (uint), uno por ranura
RWByteAddressBuffer Args : register(u2);    // 18 x 5 uints: indices, instancias, primer indice, vertice base, primera instancia

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  int lvl = (int)(gP0.x + 0.5);
  uint nb = (uint)(gP4.y + 0.5);
  uint cardsMax = (uint)(gP4.x + 0.5);
  if (lvl >= 3) {
    // argumentos del dibujo indirecto: una hebra por ranura, instancias = min(contador, tope), indices = tarjetas del subnivel
    uint s = id.x + id.y * 8;
    if (s < 3 * nb) {
      uint b = s % nb;
      uint n = min(Count.Load(s * 4), (uint)(gP1.y + 0.5));
      uint o = s * 20;
      Args.Store(o, (cardsMax >> b) * (uint)(gP3.w + 0.5));
      Args.Store(o + 4, n);
      Args.Store(o + 8, 0u);
      Args.Store(o + 12, 0u);
      Args.Store(o + 16, 0u);
    }
    return;
  }
  if (id.x >= (uint)(gP1.w + 0.5) || id.y >= (uint)(gP2.x + 0.5)) return;
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
  float m = gP2.y;
  if (p < gP0.z * (1.0 - m)) return;
  if (gP0.w > 0.0 && p >= gP0.w * (1.0 + m)) return;
  // fundido entre niveles por cantidad: el nivel saliente queda con lim * k y el entrante con lim * (1 - k) (suman 1)
  float kf = (gP0.z > 0.0 ? smoothstep(gP0.z * (1.0 - m), gP0.z * (1.0 + m), p) : 1.0) * (gP0.w > 0.0 ? 1.0 - smoothstep(gP0.w * (1.0 - m), gP0.w * (1.0 + m), p) : 1.0);
  if (kf < 0.002) return;
  // descarte por pantalla con margen del tamano de la celda (como el VS)
  float px = (abs(cb2[4].x) + abs(cb2[4].z)) * rad, py = (abs(cb2[5].y) + abs(cb2[5].z)) * rad;
  if (cc.x < -w - px || cc.x > w + px || cc.y < -w - py || cc.y > w + py) return;
  // tarjetas que cubren la celda en pantalla
  float pc = clamp(p, 4.0, 2000.0);
  float wpx = max(gP3.x * pc, gP3.y);
  float C = clamp(gP3.z * sqrt(pc / 35.0), 0.05, 2.0);
  float cards = C * (cell * pc) * (cell * pc) / wpx;
  // con la camara baja la celda no se ve de frente: ocupa una banda de (celda * p) x (celda * p * sen(elevacion) + alto de la hoja);
  // tope de capas de hojas por pixel sobre esa banda (si no, en las repeticiones cercanas el solapamiento llegaba a 14-46x)
  {
    float3 eye = cb2[20].xyz;
    float dist = max(length(float3(x, 0.0, z) - eye), 0.5);
    float sinE = clamp(abs(eye.y) / dist, 0.03, 1.0);
    float hpx = max(gP4.z * pc, 1.0);
    float band = (cell * pc) * (cell * pc * sinE + hpx);
    cards = min(cards, gP4.w * band / (wpx * hpx));
  }
  cards = clamp(cards * kf, 0.0, (float)cardsMax);
  if (cards < 0.5) return;
  uint lim = (uint)ceil(cards - 0.0001);           // la ultima tarjeta sale a la altura de la parte fraccionaria (sin saltos al mover la camara)
  float fr = saturate(cards - (float)(lim - 1u));
  uint b = (uint)clamp((int)floor(log2((float)cardsMax / (float)lim)), 0, (int)nb - 1);   // la ranura mas chica con >= tarjetas
  uint slot = (uint)(gP1.z + 0.5) + b;
  uint idx;
  Count.InterlockedAdd(slot * 4, 1u, idx);
  if (idx >= (uint)(gP1.y + 0.5)) return;          // tope de la ranura
  uint rot = ((id.x * 7u + id.y * 13u) ^ (id.x * id.y)) & 3u;
  // z = escala de la celda (1 / 2 / 4) + fraccion de la ultima tarjeta * 0.5
  Inst[slot * (uint)(gP1.y + 0.5) + idx] = float4(x, z, cell / 0.5 + fr * 0.5, (float)(lim * 4u + rot));
}
