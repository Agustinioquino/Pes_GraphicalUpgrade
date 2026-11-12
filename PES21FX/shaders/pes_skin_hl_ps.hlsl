// PES21 FX - variante de la piel del cuerpo para el menu Fisico (24/10 c): el mismo shader aa7d1833 (reemplazo propio) mas
// la suma, en u5, de donde quedo en la pantalla cada marca del musculo elegido (con [earlydepthstencil]: solo pixeles
// visibles). La DLL (ring.cpp) la pone en lugar de aa7d1833 solo mientras late el resaltado y despues dibuja el aro y el
// punto blanco encima de la imagen del jugador (pes_ring_ps.hlsl), como una capa de interfaz.
#define PES_HL_ACC 1
#include "aa7d1833f6a11481-ps_replace.txt"
