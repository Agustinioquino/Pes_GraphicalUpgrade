# Sol real del motor en el editor (09/11/2026)

Pedido de Agustin: "el timelapse tiene que funcionar con un sol real luz de noche real, y el sol cercano tiene que ser un
sol real del juego asi como pasa a la hora de jugar".

## Conclusion

**Camina el camino (a): agregar una luz direccional de Fox a `EditFace.fox2` y servirla por el livecpk `LucesEditor`.**
No hace falta que la DLL fuerce nada del render. El motor no apaga el sol en el editor: el editor simplemente no tiene
ninguna luz direccional, porque su escena de estudio solo trae focos (`SpotLight`).

Herramienta: `tools/fox/editface_sol.py` (rama `claude/project-thread-dknhpb`), sobre `tools/fox/fox2_edit.py`
(lee y rearma `.fox2` binarios; rearma `light_st072_af.fox2` del Camp Nou **identico byte a byte**).

## Evidencia (C decompilado, `/mnt/project-files/decompilado/C/`)

| que | donde | que dice |
|---|---|---|
| Pasada del sol | `PluginSunlight::vf8` 0x1416ee820 | recorre las luces del contexto del viewport (`FUN_1416883f0(vp)+0x98`), se queda con las de tipo `DirectionalLightSource` (`FUN_14169f060`) y por cada una arma `LIGHTING_SUN` y llama a `DirLight_ToView` 0x14171e220. No mira el modo de juego. |
| Que luz proyecta sombra | `PluginShadow::vf7` 0x1416c66e0 | pide al mismo contexto la direccional de prioridad 1 (`FUN_141721d70(ctx,1)`) o 0; si no hay ninguna, `MainExec` 0x1416c2ad0 borra las cascadas. Tampoco mira el modo. |
| Unico corte | las dos | `vp+0x650` bit 18 con paquete modo 2 = viewport sin luces/sombras; es un corte generico de todos los plugins (decenas de usos), no del editor. |
| Quien crea el sol en el partido | `TppAtmosphere` (`FUN_141943ce0`) | crea una `DirectionalLightSource` (`FUN_14169ee20`) y le pasa la direccion y el color globales que calcula la atmosfera (`DAT_1437d5700` / `DAT_1437d56a0`). Vive en `light_stXXX_af.fox2` del estadio (junto con `GrShadowSettings`). |
| Entidad direccional suelta | `fox::grx::DirectionalLight` (registro 0x141816020, version 7, 0x1b0 bytes) | su cuerpo (`FUN_141815620`) crea su `DirectionalLightSource` y la mete en la escena de su DataSet con `FUN_1416927c0`, **igual que un SpotLight** (`FUN_141815ae0`). Por eso entra en el mismo contexto que los focos del EditFace, que ya se ven en el editor. |
| Propiedades -> fuente | `FUN_141812350` | `direction` (+0x140, girada por el transform de la entidad) -> `FUN_14169f3c0`; `color`/`temperature`/`lux` (+0x170/+0x180/+0x188) -> `FUN_141693630` -> `FUN_14169f2e0`; `selfShadowBias` x0.001; `lightSize` en grados; `castShadow` (+0x190); `isCascadeBlend` (+0x168); rangos de sombra +0x150..+0x160. |

En el editor hoy: `EditFace.fox2` = DataSet, PesPluginSettings, TimeOfDaySettings (con `overrideDateTime`, hora,
`sunLux` 150000, `moonLux` 1, ya preparados pero sin atmosfera que los use), TimeOfDay, ColorGrading, 24 focos, probes.
Sin `TppAtmosphere`, sin `DirectionalLight`, sin `GrShadowSettings`.

## La herramienta

```
python tools/fox/editface_sol.py <EditFace.fpkd entrada> <EditFace.fpkd salida> [opciones]
```

Entrada = el `EditFace.fpkd` que ya esta en `<Sider>\livecpk\LucesEditor\Asset\model\light\#Win\` (preset "alta",
F7BE8FA8): los cambios de focos se conservan. Antes de escribir verifica que el fox2 de entrada se rearma identico, y
despues relee la salida.

**Modo `direccional` (por defecto, no necesita ningun archivo mas).** Agrega:
- `DirectionalLight "zfx_SolEditor"` (+ su TransformEntity): `castShadow` si, `isCascadeBlend` si, `lux` 1500,
  5600 K, altura 35 grados, `shadowRange` 12 m / `hiResShadowRange` 3 m (en el estadio son 240 / 35: en el editor
  conviene chico para que la cascada fina caiga en la cara), `selfShadowBias` -15, `lightSize` 0.53 grados.
- `GrShadowSettings` con los valores del estadio (metodo 1, opacidad 1, blur 2.5, 4 muestras temporales).
- Opciones: `--lux`, `--elev`, `--azim`, `--temp`, `--rango`, `--sin-luz` (la deja apagada para que la prenda la DLL).

Lux: la exposicion del editor esta fija (`gameExposureCompensation` -7.1, min = max = 0). El sol real de 150000 lux
saturaria todo; 1500 lux da un sol ~2 veces la key de estudio (unos 600-900 lux en la cara). La relacion con el partido
de dia (-14 EV) es 2^6.9 ~ 120: 150000 lux del partido ~ 1260 lux aca.

**Modo `atmosfera` (el "sol real" completo: sol, luna, luz de cielo y hora como en el partido).**
`--modo atmosfera --donante light_stXXX_af.fpkd --donante-fpk light_stXXX_af.fpk --fpk-entrada EditFace.fpk
--fpk-salida EditFace.fpk --hora 16 --minuto 0 --exposicion partido`
- Copia `TppAtmosphere` + `CapturePosition_Sky` (+ transform) del fox2 de luz de un estadio, prende
  `overrideDateTime` con la hora, y con `--exposicion partido` pone la exposicion automatica -14..-7 EV del estadio.
- Los `.atsh`/`.pcsp` de esa atmosfera van con nombre propio `zfx_EditFace_sol.atsh/.pcsp` dentro de `EditFace.fpk`
  (el hermano que hoy solo trae `Default.rpd`). Hace falta el `.fpk` del estadio: no esta en la nube; esta en la PC
  (`D:\copias pes\Asset\model\bg\...\light\` o se extrae del cpk con `extraer_cpk_grandes.bat`).
- Sin `DirectionalLight` propia: la atmosfera crea la suya.
- Ojo: con la exposicion del partido, de dia los focos de estudio (~1000 lux) casi no se ven al lado del sol (como en
  la realidad); de noche la exposicion sube a -7 y vuelven a mandar los focos + la luna.

Probado en la nube con un `EditFace.fox2` reconstruido desde `fox2_txt/EditFace.json` (mismo tamano, 71184 bytes, mismo
volcado): las dos salidas las lee `fox2_dump.py` sin avisos (67 / 68 entidades, todos los hashes resueltos). No probado en
el juego.

## Enganche para el hilo de la luz (timelapse w392, sol cercano w378)

**Modo direccional** (recomendado primero):
1. Puntero: enganchar `FUN_141815620` (cuerpo de la DirectionalLight al crearse; `rcx` = cuerpo). La entidad es
   `FUN_1402b9e60(cuerpo)`; la fuente, `*(cuerpo+0x88)`. Guardar el cuerpo cuando se crea fuera del partido (en el editor es la unica
   DirectionalLight; el nombre `zfx_SolEditor` sirve para confirmarlo en el log), y validarlo cada
   cuadro por la vtable de `DirectionalLightBody` antes de usarlo (el editor la destruye al salir).
2. Cada cuadro (o cuando cambia el slider): escribir en la entidad `direction` (+0x140, Vector3 hacia el sol),
   `color` (+0x170), `temperature` (+0x180), `lux` (+0x188), `enable` (+0x130) y llamar `FUN_141812350(cuerpo)`: es la
   misma funcion que usa el motor para bajar las propiedades a la fuente (sin escribir la fuente a mano).
3. Sol: la posicion astronomica real la da el propio exe, `Astro_SunPos` 0x1419591d0 (lat/long, fecha, gmt), o la cuenta
   de la DLL. Noche real: la misma luz con la direccion de la luna, `moonLux` (escalado como arriba), `moonColor`
   (0.996, 0.988, 0.843) y ~4100 K; el cruce sol/luna lo hace la atmosfera en el partido con
   `dirLightSunLimitAngle`/`dirLightMoonLimitAngle` (en el estadio 0).
4. Sol cercano (w378): es la misma luz con altura baja (5-15 grados) y `lux` alto; la sombra real sale de las cascadas.

**Modo atmosfera**: el timelapse = cambiar la hora y dejar que el motor haga todo. La atmosfera en ejecucion esta en el
manager `*0x1437d59e8` (+0xd8 cantidad, +0xe0 punteros); en el objeto, fecha/hora en +0x170 (6 shorts) y sucio en
+0x3d0 (memoria `sol-exe-mapa`). Alternativa sin tocar la atmosfera: escribir `dateTimeHour`/`dateTimeMinute` de la
`TimeOfDaySettings` del EditFace (la pasa TOD_Pump 0x14191c7b0 -> Apply2 0x14191c8f0 -> Sky_SetDateTime 0x141949440).

## Lo que queda sin verificar (y como verlo sin pedir pruebas)

- **Signo de `direction`**: el script la pone "hacia el sol" (como `fixedRisingSunDir` de la atmosfera, y > 0 de dia).
  Confirmarlo en el log: en un partido de dia, `DAT_1437d5700` (direccion que la atmosfera le pasa a su fuente) tiene que
  dar y > 0; si da y < 0, la convencion es "desde el sol" y hay que invertirla (`--elev` negativo hasta corregir el script).
- **Que el viewport del editor tenga PluginSunlight/PluginShadow**: los plugins se crean con una fabrica generica
  (`FUN_1416ee2c0`); no hay corte por modo a la vista, pero no esta probado. Log: contar llamadas a 0x1416ee820 y a
  0x1416c66e0 en el editor, y si `plugin+0x60` deja de ser 0 con la luz nueva.
- **La DLL**: sus filtros del sol ("LightCB/ShadowMap solo desde la vista mas grande", f62d17d) y los de pasadas propias
  que rechazan el viewport del editor (1650x1440 vs 2560x1440, cejas) pueden saltear las 5 PS del sol en el editor.
  Revisarlo en el hilo de la luz al conectar.
- **Atmosfera en el editor**: el manager de atmosferas elige por prioridad (estadio 100); el EditFace se descarga al
  salir del editor, pero conviene un log del manager al pasar editor -> partido.

## Reglas

- Boca: el archivo solo tiene luces; no toca `mouth.fmdl`, PS de la boca ni `pes_oral_*.dds`. La boca recibe el sol con el
  mismo shader que en el partido.
- Nada del juego se reemplaza en disco: va por livecpk como el EditFace actual; los archivos nuevos se llaman `zfx_*`.
- Instala el hilo del cesped: backup de `livecpk\LucesEditor`, juego cerrado.
