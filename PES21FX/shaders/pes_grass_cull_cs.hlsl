// PES21 FX - pasto en toda la cancha v3 (25/10): la lista de parcelas se arma en la GPU con la camara del MISMO cuadro.
// Una hebra por celda de una grilla fija de la cancha (3 niveles: celdas de 0.5 / 1 / 2 m). Cada celda entra en el nivel
// que le toca segun su tamano en pantalla (pixeles por metro en su centro), se descarta fuera del campo de vision y se
// anota en el buffer de instancias de su nivel (x, z, escala, giro), con tope fijo por nivel. La pasada 3 escribe los
// argumentos de DrawIndexedInstancedIndirect con los contadores. Sin camara atrasada, sin bucle en la CPU.
cbuffer cVSScene : register(b2) { float4 cb2[21]; }   // la escena del juego tal como la ve el VS: vista cb2[8..11], proyeccion cb2[4..7]
cbuffer PESGrassCull : register(b0) {
  float4 gP0;       // x nivel (0, 1, 2; 3 = escribir los argumentos) | y celda (m) | z pLo (px/m) | w pHi (px/m, 0 = sin tope)
  float4 gP1;       // x mitad del alto de la pantalla (px) | y tope de instancias del nivel | z offset del nivel en el buffer | w celdas a lo largo
  float4 gP2;       // x celdas a lo ancho | y margen del fundido (fraccion) | z semi largo de la grilla (m) | w semi ancho (m)
  float4 gIdx[3];   // por nivel: x indices del dibujo | y primer indice | z vertice base | w (sin uso)
  float4 gCap;      // topes por nivel (x, y, z)
}
RWStructuredBuffer<float4> Inst : register(u0);
RWByteAddressBuffer Count : register(u1);   // 3 contadores (uint) en 0, 4, 8
RWByteAddressBuffer Args : register(u2);    // 3 x 5 uints: indices, instancias, primer indice, vertice base, primera instancia

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  int lvl = (int)(gP0.x + 0.5);
  if (lvl >= 3) {
    // argumentos del dibujo indirecto: una hebra por nivel, instancias = min(contador, tope)
    if (id.x < 3 && id.y == 0) {
      float caps[3] = { gCap.x, gCap.y, gCap.z };
      uint n = min(Count.Load(id.x * 4), (uint)(caps[id.x] + 0.5));
      uint o = id.x * 20;
      Args.Store(o, (uint)(gIdx[id.x].x + 0.5));
      Args.Store(o + 4, n);
      Args.Store(o + 8, (uint)(gIdx[id.x].y + 0.5));
      Args.Store(o + 12, asuint((int)gIdx[id.x].z));
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
  // descarte por pantalla con margen del tamano de la celda (como el VS)
  float px = (abs(cb2[4].x) + abs(cb2[4].z)) * rad, py = (abs(cb2[5].y) + abs(cb2[5].z)) * rad;
  if (cc.x < -w - px || cc.x > w + px || cc.y < -w - py || cc.y > w + py) return;
  uint idx;
  Count.InterlockedAdd((uint)lvl * 4, 1u, idx);
  if (idx >= (uint)(gP1.y + 0.5)) return;          // tope del nivel
  uint rot = ((id.x * 7u + id.y * 13u) ^ (id.x * id.y)) & 3u;
  Inst[(uint)(gP1.z + 0.5) + idx] = float4(x, z, cell / 0.5, (float)rot);
}
