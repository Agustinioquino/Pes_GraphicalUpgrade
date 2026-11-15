// PES21 FX (04/11 piel) domain shader de la teselacion de la cara para el ATLAS de sombra / grosor de las cabezas: variante b
// (VS 455eb97b, cara animada). Igual que pes_face_tess_b_ds.hlsl pero proyecta con la caja de la cabeza (b12). Ver pes_face_tess.hlsli
#define PES_TESS_V455 1
#define PES_TESS_DS 1
#define PES_TESS_ATLAS 1
#include "pes_face_tess.hlsli"
