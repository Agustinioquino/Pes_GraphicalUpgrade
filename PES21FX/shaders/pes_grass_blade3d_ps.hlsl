// PES21 (14/11, paso 2 del plan de optimizacion del pasto): VARIANTE DEL PS DE LAS HOJAS (18afff) PARA LAS HOJAS 3D de primerisimo plano.
// Es el mismo archivo con PES_BLADE3D = 1: el compilador saca lo que en las hojas 3D no corre (atlas de tarjetas, recortes que vuelan,
// normal por tarjeta, recorte por alfa). La DLL (grass.cpp) la pone solo en los dibujos de las hojas 3D (ranuras 18 / 19) de la pasada de
// color; si no compila, esos dibujos siguen con 18afff como antes. Cambiar 18afff cambia tambien esta variante (la DLL la recompila)
#define PES_BLADE3D 1
#include "18afff0f79d9aaa5-ps_replace.txt"
