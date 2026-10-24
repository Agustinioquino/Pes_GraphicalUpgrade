# Ingeniería inversa de PES2021.exe 1.07.02 (sello 62787C6A, base 0x140000000)

Trabajo sobre el volcado de las secciones desempaquetadas que escribe `exedump.cpp` (no se sube al repo: es código de
Konami). Scripts (Python 3 + numpy + objdump; sin capstone):

- `dumpmap.py`: carga las secciones (`.text` 0x1000, `.idata` 0x2538000 = datos de solo lectura y cadenas, `.text1`
  0x33A9000 = datos / RTTI, `.pdata` 0x3AD3000, `.data2`, `.data`). Cambiar `D` a la carpeta del volcado.
- `xref.py`: referencias rip-relativas (disp32) a una dirección. `calls.py`: quién llama / salta a una función y
  punteros absolutos a ella (vtables). `fn.py`: límites de función por `.pdata` (`func_of`, `root_of` sigue los trozos
  encadenados) y desensamblado con objdump. `rtti.py`: vtables de una clase por su nombre RTTI
  (`python3 rtti.py ".?AVAntiAliasing@plugins@dg@gr@fox@@"`). `pat.py`: búsqueda de patrones de bytes.

## Cámara y TAA (camhook.cpp, 23/10)

| Qué | Dónde |
|---|---|
| CameraSystem: aplicar la cámara principal `(param, reiniciar)` | 0x14198d3b0 |
| Actualización por cuadro del CameraSystem | 0x14198e6e0 (llama a 0x14198d3b0 si no está inicializada) |
| Manejador de comandos de cámara (vtable de `CameraCommandHandler@rd@cobra`) | [1] 0x14198d360 (fijar), [3] 0x14198d080 (interpolada: llama a 0x14198d3b0 con reinicio = cambió el id) |
| Parámetro de cámara (0x104 bytes; copia 0x14198ce20) | +0 posición, +0x0c objetivo, +0x18 arriba, +0x24/+0x28 cerca/lejos, +0x2c tipo de proyección, +0x48 continua, +0x88 id, +0xe8 reiniciar (cámaras de render target) |
| Cámara principal global | 0x1437d96b0 (inicializada: byte 0x1437d97c0); cámara de depuración 0x1437d97d0 |
| Banderas de la cámara de render | +0xf0: 0x80 = descartar historia temporal, 0x2 = corte |
| TAA del juego `fox::gr::dg::plugins::AntiAliasing` | vtable 0x142a2b520, [4] = 0x141754850 (vista en r9; cámara = [vista+0x4a0]; rect de la vista +0x57c..+0x588) |
| "Cancel Temporal Pass" / "Temporal Pass" | 0x141754a50 / 0x141755340 |
| Cambio de tamaño de la vista (prende 0x80) | 0x141687940 |
| Cámara de render target (comando 5 de `RenderTargetCommandHandler@rd@cobra`) | 0x14198afd0 (copia +0xe8 a la bandera 0x80) |

## Configuración gráfica (exepatch.cpp, 20/10)

Lectores Lua `GrPluginPesShadow.ShadowResolution` 0x1416b9a10, `GrShadowSettings` 0x1416c70c0,
`GrPluginPlanarShadowCombiner.ShadowQuality` 0x1416bdcb0; resolución de LOD global 0x14374cd94 (función 0x141686380).
