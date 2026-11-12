# Gestos del mod como clips .gani (08/11)

Generados con `tools/gani_gestos.py` (rama `claude/project-thread-vamwp7`). El escritor `tools/gani_escribir.py` rearma idénticos, byte a byte, los 7 clips de Konami que hay en `/mnt/project-files/face/gani/`. Estos clips usan el mismo formato y la misma tabla de canales.

## Qué hay

| Archivo | Gesto (sliders de la DLL a 1.0) | Mandíbula |
|---|---|---|
| zfx_enojo | Enojo | 0° |
| zfx_sonrisa | Sonrisa | 0° |
| zfx_risa | Sonrisa 1.4 | 5.5° |
| zfx_asombro | Asombro | 9.2° |
| zfx_grito | Grito | 19° |
| zfx_puchero | Puchero | 0° |
| zfx_abrir_boca | Abrir la boca | 19° (tope) |
| zfx_dientes | Mostrar dientes | 0° |
| zfx_cejas_arriba / zfx_ceno / zfx_preocupado | Cejas x361 / ángulo x365 | 0° |
| zfx_hablar | Hablar (bucle de 6 s, entra y sale de cero) | hasta 2.8° |

`reemplazos/smil_soft.gani` y `reemplazos/angr_brwnit_lorol.gani` son los clips de Konami con el gesto del mod sumado encima, igual que con el slider en el juego. Tienen el mismo nombre, así que el juego los usa solo, sin DLL.

## Boca (regla fija)

No se toca mouth.fmdl, el PS de la boca ni pes_oral_*. Los clips solo mueven huesos skf_. La boca se abre con skf_jaw, igual que en los clips de Konami, y el giro tiene tope en 19°, lo mismo que pain_brwtrb_shut_eyhc_hard de Konami (19.2°). No hay deslizamiento del cóndilo: si solo se corrieran los labios, se separarían de los dientes.

## Instalación (la hace el hilo del césped, con el juego cerrado)

1. Los `zfx_*.gani` van a `PES21FX\anim\face\`. La DLL los lee y se prueban con la fila 383 (x = número de clip, y = peso 1). Como empiezan con "zfx" quedan últimos en el orden alfabético y los números de los clips de Konami no cambian. El log dice "gani: N clips" (N sube en 12).
2. Los `reemplazos/` van a `Dream Patch Sider\livecpk\root\common\anime\FoxAnim\Face\Animations\Base\` **solo con el OK de Agustin**, después de hacer un backup de los dos originales (smil_soft 02D73042, angr_brwnit_lorol 2D7EAA42).

CRC32: ver `crc.txt`.

## Para otros clips

Para pasar cualquier clip de la PC: `python tools/gani_gestos.py --sumar <carpeta con base_normal/ask/frig> zfx_sonrisa 1.0 entrada.gani salida.gani`. El número es el peso del gesto.

## Mejoras de cara sin DLL (propuestas)

1. **Reemplazar más clips de Konami por versiones con gesto** (sonrisas, enojos, festejos `base_d_end_*`), con `--sumar`. Es lo que más se nota en partido y no necesita DLL.
2. **Canal NMdriverB** (pos z del clip): en los clips de Konami vale 8 en enojo, 21 en sonrisa y de 60 a 88 en dolor. Parece ser el que elige o pesa el mapa de arrugas. Escribiéndolo en los clips del mod, las arrugas del juego acompañarían el gesto. Antes hay que probar qué hace con 2 o 3 valores.
3. **Luz del editor (EditFace.fpkd por livecpk)**: hoy es una luz clave de 7200 K, de lado y con mucho relleno de abajo, y por eso la cara se ve plana y azulada. Bajar el relleno y entibiar la clave a unos 5500 K mejora la piel sin DLL (ver `decompilado/detalle/fox2_luces_y_assets.md`).
4. **Parpadeo y mirada (eye_anime_file0.mtar)**: los clips de ojos están dentro de un .mtar. Con un escritor de .mtar (el formato ya está en el lector) se puede usar el parpadeo irregular del mod en el juego base.
