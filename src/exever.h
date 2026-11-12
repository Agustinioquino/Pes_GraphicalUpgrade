// Version del PES2021.exe cargado y traduccion de direcciones (12/11).
// La DLL se escribio contra el exe 1.07.02 de Steam (sello 62787C6A). El exe 1.00 (sello 5F5ED4BF, el de lanzamiento y el de las
// copias sin Steam) tiene el mismo codigo corrido de lugar: cada direccion fija del codigo pasa por XA(), que en 1.07.02 la devuelve
// igual y en 1.00 la busca en la tabla exever_100.inc (armada funcion por funcion con tools/re/port).
#pragma once
#include <cstdint>
#include <cstddef>

namespace fx {

int ExeVer();                        // 107 = 1.07.02, 100 = 1.00, 0 = otro exe
uintptr_t XA(uintptr_t a107);        // direccion de 1.07.02 -> la misma cosa en el exe cargado (0 si en 1.00 no esta en la tabla)
bool ExeStampOk(unsigned stamp);     // el sello es de un exe soportado (1.00 solo con exe_100 = 1 y la tabla completa)
bool ExeOk();                        // base 0x140000000 + sello soportado
bool ExeCodeSection(uintptr_t* lo, size_t* size);   // seccion de codigo del exe (".text"; en 1.00 se llama ".trace")
void ExeVerLog();                    // una linea en el log con la version detectada

}
