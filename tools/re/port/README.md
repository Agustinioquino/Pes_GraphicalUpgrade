# Porte de direcciones 1.07.02 -> 1.00 (12/11/2026)

La DLL usa direcciones fijas del `PES2021.exe` 1.07.02 de Steam (sello `62787C6A`). El exe 1.00 (sello `5F5ED4BF`, el de
lanzamiento) tiene el mismo codigo corrido de lugar. Estas herramientas ubicaron en el 1.00 cada direccion que usa la DLL;
el resultado es `map5.json` y de ahi sale `src/exever_100.inc`.

- Entradas: el exe 1.00 en disco (su codigo esta legible: seccion `.trace`) y el volcado de memoria del 1.07.02
  (`PES21FX\dump`, lo escribe la DLL con `exe_dump`). Las rutas estan en `img.py`.
- `match.py` ... `match5.py`: funciones por firma enmascarada (capstone: se tapan los desplazamientos relativos a rip, los
  destinos de call/jmp y los desplazamientos grandes), datos por las referencias que les hace el codigo, vtables y COL por
  RTTI, funciones plegadas por el enlazador (ICF) por sus llamadas y por las ranuras de vtable.
- `verify.py`, `sigs.py`, `vtcheck.py`, `gdiff.py`: comprobaciones (funcion entera identica, firmas de bytes del codigo de la
  DLL contra el 1.00, ranuras de vtable, diferencias de desplazamientos de estructuras).
- `gen_table.py`: escribe `src/exever_100.inc` y revisa que cada `XA(0x...)` de `src/` tenga entrada (`--check` solo revisa).
- `apply_xa.py`: la pasada unica que envolvio las direcciones del codigo en `XA()`.

Ojo: el volcado del 1.07.02 trae ganchos de otros mods puestos en memoria (un `jmp` en los primeros bytes de algunas
funciones); no fiarse de esos bytes.

Diferencias que no son solo de direccion (todas en el codigo, buscar `ExeVer() == 100`):
- EditWork (jugador en edicion): la clave esta en `+0xd0` en 1.00 y en `+0xe0` en 1.07.02 (`fisico.cpp`, `motorpage.cpp`).
- Cuatro firmas con un desplazamiento distinto: `kSunFixOld/New` (`exepatch.cpp`), `kSetCamSig` (`freecam.cpp`),
  `kSetStepSig` y `kBoneSig` (`tiempo.cpp`).
- `.rdata` empieza en `0x14252f000` (`rastro.cpp`) y la seccion de codigo se llama `.trace` (`exever.cpp`).
- No portado: `grassdist.cpp` (distancia del pasto del juego) y `cobertura.cpp` (herramienta de investigacion).
