// PES21 FX - banderin del corner: tela propia (14/11/2026, banderin.cpp / banderin_sim.h).
// La DLL simula la tela (grilla de 13 x 9, la misma de la malla "cloth": columna del palo clavada, gravedad + viento unico) y sube
// esto en t111 SOLO de los VS ca62006b9e91c070 (principal) y ad793f5286a2e699 (sombra / profundidad):
//   [i]        posicion del punto i en el espacio del modelo (xyz), w = cuanto se junta la tela ahi (0..1)
//   [128 + i]  a donde fue a parar el eje x de la malla (la tangente en reposo es (1,0,0))
//   [256 + i]  a donde fue a parar el eje z de la malla (la normal en reposo es (0,0,1))
//   [384 + i]  posicion del cuadro anterior (para el desenfoque de movimiento / TAA)
//   [127]      x = 515: el buffer es valido
// i = BLENDINDICES.x = fila * 13 + columna (0..116), el mismo indice con el que el juego lee su textura grabada (VAT).
// Sin enlazar (opcion x514 apagada, viento unico apagado o DLL vieja) se lee 0 y el VS hace exactamente lo del juego.
StructuredBuffer<float4> PESFlag : register(t111);

// es la tela del banderin y hay tela simulada? base = m_materials[1] (PositionBase), anim = m_materials[3] (fps, cuadros, desfase)
bool PESFlagOn(float4 base, float4 anim, uint idx) {
  if (idx > 116u) return false;
  // PositionBase del banderin: comun (-0.6119, 0.8373, -0.1585), estadios (-0.6119, 0.8374, -0.1546); 256 cuadros
  if (abs(base.x + 0.6119) > 0.004 || abs(base.y - 0.8373) > 0.004 || abs(anim.y - 256.0) > 0.5) return false;
  return PESFlag[127].x == 515.0;
}
float3 PESFlagPos(uint idx) { return PESFlag[idx].xyz; }
// P / Q = posicion ahora / en el cuadro anterior; X, Y, Z = a donde van los ejes de la malla (matriz de giro para normal y tangente)
void PESFlagRead(uint idx, out float3 P, out float3 Q, out float3 X, out float3 Y, out float3 Z) {
  P = PESFlag[idx].xyz; Q = PESFlag[384u + idx].xyz;
  X = PESFlag[128u + idx].xyz; Z = PESFlag[256u + idx].xyz;
  Y = cross(Z, X);
}
