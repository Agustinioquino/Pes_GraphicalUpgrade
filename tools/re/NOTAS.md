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
| Menu Fisico del juego (`menu::MenuEditPlBodyPhysiqueSetting`) | vtable 0x142708848 (COL 0x142cd5c40), [0] = 0x141fa9d60 paso por cuadro (comun a los menus de Editar), [3] = 0x140c33870 destructor; *(this + 0x78) + 0x50 = activo; fabrica 0x140c338c0 (tabla 0x14266c3b8 "Edit/Player/EditPlBodyPhysiqueSetting") |

## Menu Fisico del juego por dentro (fisico.cpp, 24/10)

| Que | Donde |
|---|---|
| Editar > Jugadores > Fisico | `menu::MenuEditPlBodySetting`, vtable 0x142704dd8 (COL 0x142cd50d8), fabrica 0x140c2ded0; 13 filas (altura +155 cm, peso +30 kg y 11 campos de apariencia) |
| Tablas del Fisico | textos 0x142704d10, campos 0x142704d48 (descriptor `App_FieldDesc` 0x141e5f470, tabla 0x143529110 de 0x30 bytes: setter + 0x10, min + 0x18, max + 0x20) |
| vtable (menus de Editar jugador) | [49] decidir (base 0x140cd35d0), [50] atras, [52] restablecer, [62] valor cambiado (lee el selector) / [63] valor nuevo (fila, valor), [76] armar filas, [78] cantidad de filas (Fisico: 13), [81] texto del titulo |
| Lista generica "editSettingList" (cpk_dat/common/menu/mode/editSettingList.bin) | hasta 17 filas; PlBase_SetRow 0x140cd39f0 (menu, estado, fila): estado 0 c_jump, 1 c_selector, 2 c_slider, 3 c_checkBox, 4 c_onOffSwitch, 5 c_color, 6 c_text, 7 c_textInput, 8 c_playerName, 9 c_teamName, 10 c_subHeader (estado -> tipo 0x14273a560: 255, 0, 1, 3, 5, 6, 255, 7) |
| Filas visibles | PlBase_SetNumRows 0x140cd3680 (menu, n; -1 = vtable[78]) -> animacion "setNumChoicesAuto_n" + rango del cursor (0x140e44410) |
| Entrada del menu base | 0x140e3ec40: arriba/abajo vtable[45]/[46]; izquierda/derecha 0x140e3c770 (selector: tipo 0/9; barra: tipo 1, componente 0x11) -> vtable[63] + [62]; botones vtable[70](pad, n): 0 decidir (0x140e3c660 por tipo de fila; tipo 255 -> vtable[49]), 1 atras -> [50], 3 triangulo -> [52] |
| UI | hijo i de un nodo: vector + 0x170 (0x140e8fa80); componente por nombre: (+ 0x98)->vtable[1](contenedor, "setName" / "selector" / "headline"); por tipo 0x140e4f5d0 (contenedor, tipo, n); texto 0x140e4b030 (componente, std::string*, 0, 0) (suele estar desviada por Sider) |
| Selector | limpiar 0x140e54140, agregar 0x140e53ee0 (char*), elegir 0x140e54180, actual 0x140e540f0 (+ 0xd8) |
| Barra (componente 0x11) | rango 0x140e5bf80 (barra, max, min, valor): max + 0xc8, min + 0xcc, valor + 0xd0; poner valor 0x140e5c190 |
| Cursor / titulo | 0x140e42300 (menu, lista, fila) / titulo: "headline" del objeto en controlador + 0x90, 0x140e56ca0 (char*) o 0x140e56c10 (id) |
| Jugador editado | EditWork (0x1438040a8) + 0xe0 = clave (id en los 32 bits altos) |
| Botones del menu (L1 / R1 y demas) | boton -> codigo de pad: tabla 0x1427bcdf0 (0x24, 0x23, 0x13, 0x16, 0x1c, 0x1b, 0x19, 0x1a, 0x17, 0x18, 0x1d, 0x1e); 0 decidir -> vtable[49] por tipo, 1 atras [50], 2 cuadrado [51], 3 triangulo [52], 6 L1 [53] (sonido 7), 7 R1 [54] (sonido 7), 8 L2 [55] (sonido 0xb), 9 R2 [56] (sonido 0xb), 10 / 11 L3 / R3 [57] / [58] (6..9 confirmados en el juego el 24/10: con 8 / 9 la pagina cambiaba con el zoom); modo por boton en controlador + 0x120 + n * 4 (Menu_SetBtnMode 0x140e433d0 (menu, n, modo): 0 apagado, 2 recien apretado); el Fisico solo prende 0, 1 y 3 ([53]..[56] son 0x141b42640, que no hace nada; el zoom L2 / R2 lo lee el visor 3D por su cuenta) |
| Rotulos de la barra de abajo | Menu_v23 0x140e43500 (menu, n, char*; "" lo borra): std::string en controlador + 0x168 + n * 0x20, + 0xf8 = 1 la redibuja; Menu_v24 0x140e43460 (menu, n, id de texto) |

## Dibujo de los menus del juego (glass.cpp, 24/10)

| Que | Donde |
|---|---|
| Imagen del menu | 1920x1080 RGB10A2 con profundidad; guarda sRGB / 4: el PS del menu multiplica la salida x 0.25 y el compuesto final 87552b53 (t0 inColor) hace x 4 + sRGB -> lineal hacia la RGBA8 |
| PS del menu | 55a003da3f888a81: t8 inTexture (s0 g_sampler) x COLOR0 x g_mulColor (TEXCOORD1) + g_addColor (TEXCOORD2); alpha test contra g_alpharef (TEXCOORD0.z); con VS fb68a5a0 (2D: g_projection) y 1932d77d (con g_transform, m_world y cVSWork); quads de 4 / 6 indices (hasta 18 vertices) |
| Otros en la misma imagen | cdff1e1b (VS 0ad638e3): textura x color sin x 0.25 (mascaras, ~17 por cuadro, mezcladas con los quads); 765e1433 (VS 537a35c9): el ultimo dibujo de la pasada, t8 x t9 + t10 |
| Contexto | la pasada del menu se graba en un contexto diferido (el mismo que las sombras del jugador del menu) |
| Vidrio v2 (24/10 c) | la v1 bajaba mucho los cuadros: UAV por dibujo (la placa esperaba al anterior), 25 lecturas por pixel para decidir y una copia del fondo por cada corte de la pasada (los cdff1e1b la cortaban). v2: el GS decide una vez por triangulo (t8 / s0 copiados del PS al GS), sin UAV; una copia por pasada, antes del primer dibujo con textura chica (w < 1024 o h < 512); la pasada se corta solo por otra imagen o 6 ms sin dibujos del menu |
| Imagen del jugador en el Fisico | 765e1433 (VS 537a35c9: TEXCOORD0, TEXCOORD1, SV_Position) dibuja la escena (t8, del tamano del viewport de la piel: 1238x1080) al final de la pasada: el aro del musculo se dibuja con el mismo quad justo despues (ring.cpp) |
| Rotulos dobles de la barra (24/10 c) | 12 = flechas izq / der, 14 = L1 R1 (el visor lo usa con el texto 0x3e0020 "girar"), 15 = L2 R2 (0x3e001f), 16 = palanca R (0x3800ec) |

## Tono de piel en los partidos (skintone.cpp, 24/10 c)

| Que | Donde |
|---|---|
| Paleta del juego | kGameSkin 0x14352a910 (6 colores): en Oklab el matiz (~56 grados) y el croma (~0.066) son casi iguales en los 6, solo cambia L: por eso la ganancia por canal se veia como brillo |
| Posicion de cada personaje | SceneCharacterNode + 0x40 = matriz de mundo (0x1419c61e0 es `lea rax, [rcx + 0x40]`), traslacion en + 0x70 |
| De quien es cada dibujo | VS de la piel 1c02a92a y de la cabeza faab87ee: hueso 0 (`m_boneMatrices[0]._m30_m31_m32`) x m_world = punto del personaje; la entrada mas cercana de la tabla (filas 194..254, hasta 30: primero los de tono propio, despues los personajes cercanos a ellos como tapones sin tono) en el plano del piso va al PS en o6.w (1000 + i) |
| Caras con ce188ae1 | se dibujan con el VS 455eb97b (no con faab87ee): tambien lleva el bloque del tono (si no, la cara quedaba con el color del juego y el cuerpo con el tono propio) |
| Punto del dibujo para el tono | el VS no usa m_world (solo m_viewWorld), asi que no se sabe si el motor lo llena: la DLL compara cada 30 s 4 maneras de sacar el punto (hueso 0 x m_world, hueso 0 de la segunda paleta, traslacion de m_world, inversa de m_view x m_viewWorld x hueso 0) contra la posicion de los nodos (fila 3 o columna 3 de la matriz en + 0x40) y usa la que cae sobre los personajes (w194); log "piel: comprobacion del punto" |
