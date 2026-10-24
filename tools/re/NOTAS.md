# Ingeniería inversa de PES2021.exe 1.07.02 (sello 62787C6A, base 0x140000000)

Trabajo sobre el volcado de las secciones desempaquetadas que escribe `exedump.cpp` (no se sube al repo: es código de
Konami). Scripts (Python 3 + numpy + objdump; sin capstone):

- `dumpmap.py`: carga las secciones (`.text` 0x1000, `.idata` 0x2538000 = datos de solo lectura y cadenas, `.text1`
  0x33A9000 = datos / RTTI, `.pdata` 0x3AD3000, `.data2`, `.data`). Cambiar `D` a la carpeta del volcado.
- `xref.py`: referencias rip-relativas (disp32) a una dirección. `calls.py`: quién llama / salta a una función y
  punteros absolutos a ella (vtables). `fn.py`: límites de función por `.pdata` (`func_of`, `root_of` sigue los trozos
  encadenados) y desensamblado con objdump. `rtti.py`: vtables de una clase por su nombre RTTI
  (`python3 rtti.py ".?AVAntiAliasing@plugins@dg@gr@fox@@"`). `pat.py`: búsqueda de patrones de bytes.

## Ghidra (descompilador, 23/10)

Ghidra 11.3.2 (Java 21) en modo sin ventana, sobre el mismo volcado: `ghidra/SetupPES.java` arma el programa (importa
`_text.bin` en 0x140001000 como binario crudo x86-64 / Visual Studio, agrega las otras secciones y etiqueta las 837
importaciones de `imports.txt` como `imp_*`); `ghidra/Decomp.java` crea la función en cada dirección (siguiendo el
flujo), le pone nombre y escribe el C. `ghidra/ghd salida.c 0x14198d3b0=Nombre ...` (o `@nombres.txt`).

    analyzeHeadless <proyecto> PES2021 -import <volcado>/_text.bin -loader BinaryLoader -loader-baseAddr 0x140001000 \
      -processor x86:LE:64:default -cspec windows -noanalysis -scriptPath tools/re/ghidra -postScript SetupPES.java <volcado>

`nombres.txt`: funciones ya identificadas (se aplican con `ghd x.c @nombres.txt`).

## Base de datos de jugadores (23/10)

| Qué | Dónde |
|---|---|
| Administrador de la base | puntero global 0x143714240 (`PlayerDB_Get` 0x1414bc550) |
| Registro de un jugador por id | `PlayerDB_Find(*(admin+0x48), id)` 0x1414c0e40 (registros de 0x17c bytes; id en +0x2c) |
| Campos empaquetados del registro | `PlayerRec_GetField(reg, campo, &valor)` 0x1414cc040: 0x2e = altura (byte 0, cm), 0x2f = peso (byte 1, kg), 0x2d = edad, 0x0f..0x27 = habilidades (7 bits), 0x48+ = habilidades especiales (1 bit) |

Falta: la contextura (cuello, hombros, pecho, cintura, brazos, muslos, pantorrillas, largo de piernas: otra tabla, la de
apariencia) y el vínculo entre cada jugador dibujado y su id.

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

## Rig de deformacion (musculo.cpp, 24/10)

| Que | Donde |
|---|---|
| Arma todas las restricciones del cuerpo (hueso dsk_*, destino, lado 0 = _l / 1 = _r, huesos extra) | 0x141a6cd80 (fabricas 0x141a525d0..0x141a54700) |
| Restriccion: hueso / destino | + 0x58 / + 0x60 (punteros a hueso, se llenan en `Constraint_Bind` 0x141a6b4f0 con los indices + 0x50 / + 0x54) |
| Restriccion: base (destino_reposo^-1 * hueso_reposo; la escala es la del hueso) | + 0x20 escala, + 0x30 cuaternion, + 0x40 traslacion |
| Restriccion: lado (entero que pasa la fabrica) | + 0x80 en casi todas (Deltoid, UpperArm*, Sleeve, Forearm, Wrist, Pect, Clavicle, Collar, Rib, Underarm*, Trapezius, Thigh, Pants, Hem, Belly*x); Scapula guarda ahi el puntero a sk_shoulder |
| Hueso del esqueleto (paso 0xc0 en el arreglo del esqueleto + 0x28, cantidad + 0x30) | + 0x18 puntero a la transformacion actual (espacio del modelo), + 0x20 padre, + 0x40 reposo en el espacio del modelo (escala, cuaternion, traslacion), + 0x8c sucio, + 0xa0 angulos (cache, 0x1419f7fe0) |
| Transformacion (48 bytes) / componer A * B | escala [0..2], cuaternion (x, y, z, w) [4..7], traslacion [8..10]; 0x141a69e60: s = sA * sB, q = qA qB, t = tA + R(qA) (sA * tB) |

Hueso -> clase (lado izquierdo; el derecho igual con _r): dsk_deltoid Deltoid, dsk_upperarm UpperArm, dsk_upperarm_skin
UpperArmSkinx, dsk_upperarm_skin_t UpperArmSkinTx, dsk_sleeve Sleeve (dsk_sleeve_01..06 SleeveCollision), dsk_trapezius
Trapezius, dsk_wrist Wrist, dsk_elbow Elbow, dsk_forearm Forearm, dsk_forearm_t ForearmTx, dsk_neckback NeckBack, dsk_scm Scm,
dsk_collar Collar, dsk_clavicle Clavicle, dsk_sternum Rib (el costado de las costillas), dsk_underarm Underarm (axila de
adelante), dsk_underarm_ba UnderarmBack (axila de atras / dorsal), dsk_pectoralis Pect, dsk_scapula Scapula, dsk_pos_belly
BellyPosx (sin pesos: es el destino de los tres siguientes), dsk_belly_scale BellyScale, dsk_belly_f BellyFx (abdominales),
dsk_belly_o BellyOx (oblicuos), dsk_belly_ba BellyBax (lumbares), dsk_back Back, dsk_knee Knee, dsk_kneeback Kneeback,
dsk_pants Pants, dsk_hip_l Hip (parte alta del gluteo; el resto del gluteo pesa a dsk_hip, que no tiene restriccion),
dsk_thighmain ThighMain, dsk_thigh Thigh (aductores), dsk_hem Hem (+ HemFx / HemIx / HemBax / HemODelayx), dsk_leg Leg
(pantorrilla), dsk_foot Foot, dsk_toe Toe, orejas Ear.

Reposos reales (log de la v4, "musculatura: reposo ..."): los huesos SI rotan en reposo. En brazos, antebrazos, muslos,
pantorrillas, short y mangas el eje X del hueso va a lo largo del miembro e Y hacia adelante (los dos lados espejados), asi
que engordar = escalar Y y Z del hueso. El pectoral tiene el pivote 4 cm delante de la piel y ejes girados: escalarlo corria
su borde contra el brazo; por eso los musculos de superficie se corren (traslacion) en vez de escalarse.

Previsualizacion sin el juego: `tools/fox/sim` (sim.py / compare.py / variant.py / exp.py) deforma los FMDL del cuerpo con
la tabla de musculo.cpp y los reposos del log y dibuja vistas con `rast.c` (compilar: `clang -O2 -o tools/fox/sim/rast
tools/fox/sim/rast.c -lm`). Ej.: `python3 compare.py PES21FX.log <carpeta fmdl> salida.png hombro hombros=1.5`.

## Que jugador es cada esqueleto (personajes.cpp, 24/10)

| Que | Donde |
|---|---|
| Crear un personaje (lado del juego): `draw::load::HumanList` vtable 0x142b98c00 (COL 0x142e083e8) [14] | 0x141ecd100 (this, HumanData* e) |
| HumanData | + 8 clave del jugador (u64; 32 bits altos = id con banderas), + 0x54 tipo, + 0xa8 CharacterImpl* (al volver) |
| Id de la cara real | (clave >> 32) & 0x800bffff, formateado "%d" en 0x141ecc5c0 (arbitros "referee%03d"); "/Assets/pes16/model/character/face/" + "real/" + id + "/face.fpk" (0x141a08e20) |
| CharacterImpl (cliente de la escena, 0x50 bytes, 0x1419e0c30) | + 8 handle del nodo; 0x1419e0ff0 arma el comando 0xb (tipo en +4 = 0x50b, handle +8, clave del asset +0x10, config serializada +0x18) |
| Comando 0xb (0x1419c9700) | CharCfg_Leer 0x141a0a490 -> SceneCharNode_Create 0x141a23210 (handle -> nodo + 0x10) -> Init 0x141a23a60 |
| SceneCharacterNode (vtable 0x142aab948, COL 0x142da4618, 0x1b0 bytes) | [0] 0x141a22f50 destructor, [3] 0x141a232b0 -> 0x141a26260 arma el rig: esqueleto principal + 0x88 (+ 0x90 / 0x98 / 0xa0 otros); altura + 0x114 |
| Restriccion -> esqueleto | + 0x10 (lo pone `Skel_AddConstraint` 0x1419f5eb0); es el mismo puntero que nodo + 0x88 |
| Base de datos de jugadores | registro de 0x17c bytes: clave en + 0x2c (u16 lugar, u32 id en + 0x30), nombre UTF-8 en + 0x38 (`PlayerRec_GetName` 0x1414c5d70; si + 0x144 tiene el bit 31 usa el nombre de admin + 0x98) |
| config del personaje + 0x40 | NO es el jugador: es un tipo (0..3; 4 = sin cambio), lo pone 0x141a0aea0 desde 0x141ecc5c0 |
