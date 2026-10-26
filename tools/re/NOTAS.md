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
| Rotulos dobles de la barra (24/10 c) | indice -> lugar 12 -> 6, 13 -> 7, 14 -> 11, 15 -> 9, 16 -> 8 (lugares 6..11 = L1, R1, L2, R2, L3, R3); vistos en el juego: 12 = flechas, 14 = palanca derecha ("girar" del visor), 15 = L2 R2 (0x3e001f); 13 = cruceta (visto); 16 (lugar 8) = L1 R1 (confirmado en el juego el 24/10) |

## Tono de piel en los partidos (skintone.cpp, 24/10 c)

| Que | Donde |
|---|---|
| Paleta del juego | kGameSkin 0x14352a910 (6 colores): en Oklab el matiz (~56 grados) y el croma (~0.066) son casi iguales en los 6, solo cambia L: por eso la ganancia por canal se veia como brillo |
| Posicion de cada personaje | SceneCharacterNode + 0x40 = matriz de mundo (0x1419c61e0 es `lea rax, [rcx + 0x40]`), traslacion en + 0x70 |
| De quien es cada dibujo | VS de la piel 1c02a92a y de la cabeza faab87ee: hueso 0 (`m_boneMatrices[0]._m30_m31_m32`) x m_world = punto del personaje; la entrada mas cercana de la tabla (filas 194..254, hasta 30: primero los de tono propio, despues los personajes cercanos a ellos como tapones sin tono) en el plano del piso va al PS en o6.w (1000 + i) |
| Caras con ce188ae1 | se dibujan con el VS 455eb97b (no con faab87ee): tambien lleva el bloque del tono (si no, la cara quedaba con el color del juego y el cuerpo con el tono propio) |
| Punto del dibujo para el tono | el VS no usa m_world (solo m_viewWorld), asi que no se sabe si el motor lo llena: la DLL compara cada 30 s 4 maneras de sacar el punto (hueso 0 x m_world, hueso 0 de la segunda paleta, traslacion de m_world, inversa de m_view x m_viewWorld x hueso 0) contra la posicion de los nodos (fila 3 o columna 3 de la matriz en + 0x40) y usa la que cae sobre los personajes (w194); log "piel: comprobacion del punto" |
| Paleta de colores del juego (24/10 e) | menusystem::ColorPaletteWindow (vtable 0x1427d0350, ctor 0x140e83870): una fila "c_color" (estado 5, componente tipo 8 con el color RGBA en + 0xb4; Ui_SetColor 0x140e81350) la abre sola al decidir (tabla por tipo de fila 0x143707d60: tipo 6 -> 0x140e3caf0 crea la ventana (0x140e83d50) con el componente como dueno y la cuelga del menu con 0x1414a5650 (hijo en + 0x40, lista por + 0x48 / + 0x50, padre en + 0x38)). Paletas en la ventana: [0x22..0x31] pares (puntero, cantidad): p_current, p_skin (6, FTS del juego), p_hair (24), p_same, p_all (72 en 0x1434d1dc0), p_creat (12 creados en 0x1434d1f40), p_eye (11), p_accessory (8); modo en + 0x90 (0 default: current + all + creat; 1 skin; 2 hair; 3 brow; 4 eye; 5 accessory; visibilidad en 0x1427d01c0). vtable[31] le escribe el color elegido al dueno cada cuadro; al cerrarse el menu base llama a vtable[62] (lista, fila). Mensajes 0x1080008 / 0x1080015 / 0x1080016 al padre. El mod le cambia p_all por los 30 tonos |
| Paleta: tabla p_all (24/10 e) | la ventana no pasa por vtable[49] al abrirse: mientras la pagina Piel esta activa el mod escribe en memoria la tabla fija 0x1434d1dc0 (72 colores en grilla de 12 x 6: columnas claro a oscuro, filas rosado fuerte / rosado / neutro / oliva / oliva fuerte, ultima fila 6 del juego + 6 apagados) y la restaura al salir |
| Medicion del color de la piel (24/10 e) | variantes PES_HL_ACC del cuerpo (aa7d1833) y de la cara (5ce3ed87 / ce188ae1) acumulan en u5 (128 bytes): lugares 0..3 marcas del aro, 4 color del cuello del cuerpo (posicion de reposo y 1.47..1.60, |x| < 0.07; el VS 1c02a92a la manda en TEXCOORD9.zw), 5 color de la franja inferior (12 %) de la cara, 6 alto de la cara en pantalla (max(8192 - y), max y); lectura por staging 2 cuadros despues (ring.cpp -> SkinMeasureReport) |
| Ropa y musculatura (24/10 f) | la camiseta la dibuja el PS 0cc62c9ee4d52538 con el VS 455eb97b (el mismo que la cara ce188ae1); el short / medias el PS 55297925 con el VS 1c9bde0e. La escala de la musculatura llega en la matriz de piel A = R S Rb^-1: la ropa usa A (A^T A)^((c - 1) / 2) (pes_cloth.hlsli, Jacobi 3x3) con la marca w163 puesta por la regla del PS |
| Ropa: traslacion (24/10 g) | la musculatura escala alrededor de un ancla en el eje del miembro: cambiar solo la parte lineal de A corria la ropa (L - L') x ancla (varios cm). Ahora t' = t + (L - L') (v - r n) con r = z163 (0.07 m): la ropa queda a r del eje hacia adentro de su normal. La posicion del cuadro anterior (g_vsPrevBone) recibe la misma escala (vectores de movimiento) |
| Solo el torso (24/10 g) | costuras de TorsoBody v11 en reposo (arm.fmdl / neck.fmdl / pants_*_sub.fmdl): brazo = anillo casi plano centro (0.263, 1.370, 0.020), normal (0.761, -0.643, -0.084), radio 4-5 cm (brazo: del lado de afuera y a menos de 12 cm del eje); cuello = anillo inclinado, centro (0, 1.522, 0.055), normal (0, 0.856, 0.516), altura segun el angulo h = 0.00037 - 0.00183 cos t - 0.01279 cos 2t. PesTorsoMask (pes_skintone.hlsli): 99 % del torso = 1, 99 % de brazos y cuello = 0. El VS 1c02a92a la manda en la fraccion de o6.w (1000 + i + 0.4 m); fila del cuerpo w = 2 = solo el torso |

## Luz del editor y de los menus 3D (24/10 g)

| Que | Donde |
|---|---|
| Sin pasada de luz del sol | en el editor / menus 3D / previa no corre ninguna de las 21 variantes de la luz del sol (LightCB y LightCBAlt sin copiar; luz.txt "sin LightCB") |
| Luz del estudio | 6d24904d (VS 3ab11c21): a 960x540 RGBA16F, normal del G-buffer por punto en uv + m_localParam[1].xy (corrimiento por cuadro), mapa esferico g_sphericalMaps (radio = sqrt(0.5 - 0.5 nz)) y acumulacion con inDiffuseAccBuf; despues 753ce71f / a341d740 a la imagen de luz. Reflejos: sondas f9c7a593 (cubos de 36 indices, c8bb7153 marca) |
| Por que el short salia granulado y oscuro | la normal se lee a media resolucion: el detalle fino del kit (trama, relieve x22, pliegues, mip -1 del normal map) no se resuelve y la muestra cambia cada cuadro; con un mapa de estudio con luces chicas el promedio baja (Jensen). La DLL publica w44 = 1 si vio LightCB / LightCBAlt en los ultimos 30 cuadros; sin sol los 4 PS de kits salen como el original (x44 = 1 lo deja) |
| Material de los kits | las luces del juego usan o2.z solo en materiales tipo 1 / 3 (piel especial / pasto); en el tipo 0 (kits) lo ignoran: la marca 0.96 no cambia nada ahi. Para el tipo 0 usan o2.w (id de material -> g_MaterialTexture) |
| GTAO en la composicion | 619b leia PESGtao en (r0.x = oclusion del juego, v): columna equivocada; ahora en uvC. En escenas donde el GTAO no corre (escena mas chica que la pantalla) la DLL enlaza una textura 1x1 = 1 en t27 |
| Aro del Fisico (24/10 g) | el editor presenta ~100 cuadros por segundo y dibuja la escena 3D en ~60 % (TAA 0.6 por cuadro): en los cuadros sin escena el aro (que dependia de que ese cuadro tuviera piel) desaparecia. Ahora un CS en Present (pes_ring_snap_cs.hlsl) pasa a una foto lo que vio el cuadro y deja lo ultimo en los cuadros sin escena; el aro lee la foto |

## Short que "explota" con el mod de ropa en el editor (24/10 h, corregido 25/10)

| Tema | Resultado |
|---|---|
| Donde pasa | solo en los menus (editor, presentacion de equipos) y con los shaders de PES21 FX prendidos o apagados: es la fisica (cloth_sim.hlsl con las mejoras de PES21 FX), no los shaders de dibujo. En el partido no pasa |
| Escena | el editor tiene 6-7 modelos a 1-3 m y el juego la arma a ~10 km del origen. El mod de ropa junta al short la banda del ruedo (es del mismo short: con merge_parts = 0 queda separada, se volvio a 1) |
| Errores encontrados en nuestra parte | volumenes de torso / brazos / piernas: alturas absolutas comparadas con alturas relativas al centro (nunca actuaban) y sumas en enteros de 0.1 mm con posiciones absolutas (lejos del origen se desbordan). La colision con la pierna usaba un plano promedio sin limite de distancia: una pierna de otro dibujo / con huesos viejos a metros tiraba la tela de golpe |
| Arreglo (25/10) | sumas relativas a un punto de la prenda, alturas relativas al centro, pierna lejos (>15 cm) ignorada y tope de 2 cm por sub-paso (tambien en el acople camiseta-short), y en los menus (w255 de la DLL o mundo a >2 km) sin empujes por movimiento del cuerpo |
| Nuestro VS del short | descartado: con el vertice que escribe el mod (A^-1 (x - t)) PesClothScale da x - r (A - A') n, menos de 1 mm |

## La ropa sigue al cuerpo musculoso (fisica, 25/10)

| Tema | Resultado |
|---|---|
| Por que el cuerpo atravesaba la remera | pecho, espalda, abdomen, trapecio, hombros y gluteos se hacen corriendo huesos dsk que la remera no usa: la piel sale y la tela queda. El mod de ropa solo choca con piel real en el short (leg_vs_hash); para la remera no hay opcion |
| Arreglo | la DLL arma una tabla de zonas (capsulas del resaltado del menu Fisico, lado izquierdo, X espejado) con el corrimiento de cada musculo (Fac.t) y el crecimiento del hombro (0.65 e alrededor de la articulacion) y la enlaza en t121 (y los parametros en t120) cuando el mod pone su simulador (se reconoce por "g_legSkin" / "g_boneCache" en la reflexion). cloth_sim.hlsl corre el reposo de cada particula con esa tabla (mezcla pesada, tope 6 cm, en la manga 30 %); los vertices mapeados tambien. z255 = fuerza |
| Limite | usa los valores generales (o los del jugador que se edita en el menu Fisico); los jugadores con valores propios en el partido usan los generales para la ropa |
| Por jugador (26/10) | la tabla t121 sigue con la general en las filas 0..72 y agrega [73] = (personajes, juegos, distancia 1 m), [74 + i] = (posicion del nodo del personaje en el mundo, juego 0 general / 1..8 propio) y los juegos propios desde la fila 106 (73 filas cada uno). Van los jugadores con valores propios y, como tapones con la general, los demas personajes a menos de 2 m de alguno. cloth_sim.hlsl (pesMusPick, al empezar main) usa el juego del personaje mas cercano al origen de la prenda (Inst.world) a menos de 1 m; si no, el general. Antes un jugador con valores propios tenia la piel con los suyos y la remera con los generales: con los generales en 1 la remera no lo seguia y el torso la atravesaba |
| Tarjetas generales como el juego (26/10) | boton "Volver a los valores del juego" en Musculatura (prueba): todos los tamanos, detalles y la definicion en 1 y se guarda; interruptores y musculatura_jugadores.ini quedan. Ojo: "Darle valores propios" arranca copiando los generales (por eso los perfiles hechos antes tienen 1.09 y parecidos en casi todo) |
| Parte de abajo de TorsoBody (26/10) | se descartaba por "vertices fuera del buffer": el ultimo vertice no tiene el paso entero (28) en la ranura 3, pero sus elementos (huesos, pesos) si entran. Ahora se mide hasta el final de los elementos usados (FmtBytes): el abdomen y la espalda baja entran en el choque de la ropa con el cuerpo real |
| Musculatura v7 (26/10) | filas 260..262 (1 = como el juego): deltoides frontal / posterior (la tapa crece 0.35 e y se corre 1.5 cm por unidad adelante / atras), antebrazo alto / bajo (el hueso "antebrazo 2" (ForearmTx, cerca de la muneca) paso a M_FORE2; los dos se multiplican con x165), pecho bajo, dorsales ancho (V), trapecio alto, gluteo levantado, muslo exterior (+0.25 e y corrimiento afuera; el borde del short lo sigue), gemelos. Los de forma no los mueve "General". Perfiles: v[12][4] (PRow 260..262 -> 9..11) |
| Menu Fisico v7 (26/10) | 4 paginas de musculatura por zona (hombros y brazos 14, pecho espalda y cuello 15, abdomen y cadera 7, piernas 8; 17 filas como maximo con el salto) + piel (pagina 5). Pasos -50..+50 de a 0.01 (la piel sigue en -25..+25: sus perfiles guardan pasos). Fila 0 de la primera pagina: selector "Tipo de cuerpo" (A mano + 6 tipos, MusclePresetApply): al elegir uno se rellenan las demas filas (la del selector no, se esta cambiando) |
| Tipos de cuerpo (26/10) | Como el juego, Delgado, Atletico, Fuerte, Corpulento, Culturista (musculo.cpp kPresets: casi todo con "General" y algunos de forma). En el menu Insert: botones en "Musculatura (prueba)" para los generales y combo en "Musculatura por jugador" |
| Musculos vivos (26/10 b) | fila 263 (x contraccion, y respiracion, z agitacion al correr; 1 = normal). En el Eval de cinco clases se lee la transformacion ACTUAL de su destino (hueso + 0x18 -> puntero, mismo formato de 48 bytes) y la de reposo (+ 0x40): brazo (destino = hueso del brazo), antebrazo, muslo, rodilla (destino = pierna) y pectoral (destino = pecho). Codo = angulo entre los ejes X del brazo y del antebrazo menos el del reposo (100 grados mas = del todo); rodilla igual con muslo y pierna (97 grados); brazo afuera = el eje del brazo en el espacio del pecho contra "abajo" (80 grados). Biceps +8 % y hacia adelante, antebrazo +4 %, deltoides +7 %, trapecio 6 mm arriba, isquios +3.5 % y atras, pantorrilla +3 % (por lo doblado). Respiracion por personaje (lugar de personajes.cpp; sin lugar: por puntero del esqueleto): agitacion por la velocidad del nodo (sube en 3 s, baja en 14 s), 0.22 a 0.72 Hz, 2.5 a 7 mm. El log cada 30 s: "musculatura viva ..." con un personaje de muestra |
| Definicion por jugador (26/10 b) | x172 entra en el perfil (v[12]); skintone.cpp pone la definicion propia en w de la fila de la posicion de cada jugador (195 + 3i) y agrega a la tabla a los que la tienen (con tapones alrededor, como el tono). aa7d1833 la toma con la entrada que eligio el VS (o6.w) |
| Tipos propios (26/10 b) | musculatura_tipos.ini (hasta 16; [nombre] + claves como los perfiles). Se guardan desde la tarjeta por jugador ("Guardar como tipo de cuerpo") y salen en todas las listas, tambien en la fila Tipo de cuerpo del juego (15 como maximo ahi). La tarjeta tambien copia los valores de otro jugador |
| Lista generica del menu (26/10 c) | estados de fila (tabla 0x1434ca010, animaciones): 0 c_jump, 1 c_selector, 2 c_slider, 3 c_checkBox, 4 c_onOffSwitch, 5 c_color, 6 c_text, 7 c_textInput, 8 c_playerName, 9 c_teamName, 10 c_subHeader (11 = fin; 12.. = setNumChoicesAuto_N). Estado -> tipo de entrada (0x14273a560): 255, 0, 1, 3, 5, 6, 255, 7, 255, 255, 255 (255 = decidir va a vtable[49]) |
| Menu Fisico por secciones (26/10 c) | 6 paginas de musculatura con subtitulos (c_subHeader): cuerpo, hombros y cuello (tipo de cuerpo, General, Definicion 0..2, "Ver: con la musculatura / como el juego"), brazos, pecho y espalda, abdomen y cadera, piernas, musculos vivos (opciones de todos: musculatura, contraccion, respiracion, agitacion, resaltado, ropa; se guardan en PES21FX.ini 1.5 s despues del ultimo cambio) + piel. El cursor arranca en la primera fila que no es subtitulo. Cada pagina deja una captura en PES21FX\capturas\fisico_p<pagina>.jpg (para revisar el diseno) |
| Ventana del mod en el Fisico (26/10 c) | musculo_ui.cpp (solo ImGui, se prueba sin el juego con un banco de prueba que rasteriza por software): mapa del cuerpo (frente / espalda, cada musculo pintado por su valor; rueda = 0.01), pestanas por zona, barras con la marca del 1, "Comparar con el juego" (mantener). El musculo bajo el mouse late en el modelo (FisicoHighlightRequest) |
| Mangas | el hueso de la manga (SleeveConstraint, destino = brazo principal sin escala) recibia 1.1 x el crecimiento del brazo: la manga (radio ~6 cm) ganaba 1,5 veces los cm del brazo (~4,4 cm). Ahora y255 (0.6). Ojo: la camiseta comun se dibuja con 055b4570 o con la variante del mod de ropa; PesClothScale (x163) solo llega al short y a las medias (1c9bde0e / 455eb97b) |

## Volumenes del simulador y el escote (25/10 b)

| Tema | Resultado |
|---|---|
| Sintoma (v12) | la camiseta se movia menos y el escote quedaba separado del cuello |
| Causa | con las alturas corregidas el torso como volumen empezo a actuar: la rodaja de arriba (cuello + hombros) no es una elipse y el escote quedaba "adentro" (lo empujaba afuera). Brazos / piernas como volumen y el grosor del brazo en la manga tambien empezaron a actuar de verdad (antes el lado de cada manga salia mal) y frenaban la manga |
| Arreglo | PES_TORSO_PUSH / PES_ARM_PUSH = 0 (las medidas siguen: costuras, short), PES_LIMB_SLEEVE = 0 (el grosor del muslo sigue en el short), la musculatura no corre las costuras (pin alto: pesSeamFade) y el vuelo por giro vuelve en los menus |

## La ropa choca con el cuerpo real (cuerpo.cpp, 25/10 c; lote por cuadro 25/10 e)

| Tema | Resultado |
|---|---|
| Idea | (25/10 e) en cada dibujo de la piel del cuerpo (VS 1c02a92a: torso de TorsoBody, brazos, cuello) solo se copian sus constantes (b2 filas 8..11 = m_view, b5 filas 0..8, b6 entero = huesos actuales y anteriores) a un lugar propio (anillo de 1024 x 208 float4). Nada de compute entre los dibujos del juego. En Present, un lote: pes_body_assign_cs.hlsl (personajes) + pes_body_cs.hlsl (piel deformada en el espacio del modelo y tabla hash por personaje: celdas de 2.5 cm, 8192 por tabla, listas enlazadas). El simulador lee t122 (tablas), t123 (vertices) y t124 (siguiente) |
| Mallas | se registran una vez: copia de los indices del dibujo (min / max) y de los vertices (POSITION0, NORMAL0, BLENDINDICES0, BLENDWEIGHT0 segun el input layout anotado), decodificadas en la CPU; se descartan las de mas de 12288 vertices, las que no llegan a 0.95 m de alto (piernas) y las que estan enteras a mas de 0.45 m del eje (manos de TorsoBody: 9443 vertices en x 0.57..0.73) |
| TorsoBody en el juego (25/10 e) | partes con el VS de la piel: brazos 1550 / 1690 vertices (x -0.6..0.6, y 1.07..1.40), torso derecho 4625 (y 1.25..1.56), izquierdo 4727, abajo 5986 (y 0.98..1.25; 34683 indices desde 57567 en el mismo buffer que el short: la DLL lo descartaba por "vertices fuera del buffer", el log ahora dice ranura, paso, desplazamiento y tamano), manos 9443 x 2. Mismo VB que el short del mod de ropa (vb del short = torso) |
| Contextos diferidos | los registros de un contexto diferido pasan a su lista en FinishCommandList y cuentan cuando la lista se ejecuta (ExecuteCommandList, enganchados en vt[114] / vt[58]); los del contexto inmediato cuentan enseguida. Asi el lote del Present solo usa constantes ya copiadas en la GPU |
| Personaje | (25/10 e) el bloque b5 se reusa entre jugadores en el mismo cuadro: no identifica al personaje. Ahora personaje = origen del modelo en el mundo (inversa(m_view) x m_viewWorld): dibujos a menos de 2 cm son del mismo; la misma malla dos veces en el mismo personaje (otra pasada) cuenta una vez; hasta 128 personajes. Cabecera: [0] cuadro (el numero que sube la DLL en ese Present = la fila 257 que vera el simulador), [1] partes, [2] vertices, [3] valida, [4..6] origen en el mundo, [7] primer registro |
| Emparejar | la prenda (Inst.world, la matriz modelo -> mundo del mod) con la tabla del ultimo Present (cuadro == fila 257) cuyo origen esta mas cerca (< 0.5 m). La piel es la del cuadro anterior (el mod simula antes de ejecutar la lista de comandos) = comienzo de la interpolacion de la prenda |
| Por punto de tela | sub-paso 0: el punto vuelve al reposo de la prenda con sus huesos y de ahi al modelo en el momento de la piel; 27 celdas, hasta dos planos tangentes (el mas cercano y otro con distinta normal) guardados en el reposo de la prenda + el movimiento propio de la piel (velocidad de la piel menos la que le da la prenda). Cada sub-paso: separacion y256 (mm), empuje maximo w256 (mm); donde hay piel, el cuerpo que adivina el mod se afloja z256 (cm); contacto inelastico |
| Diagnostico | NO usar UAV extra en el simulador: con u8 declarado el mod de ropa compilaba el shader pero no lo despachaba nunca (la ropa sin fisica). El lote escribe un resumen propio que la DLL lee cada 20 s: "cuerpo (lote del cuadro N): D dibujos de piel -> P personajes, R repetidos, ...; personajes en (x, y, z) k partes" (comparar con "new player #n at (x, y, z)" de cloth.log) |

## Estado al cerrar (25/10 07:05) - kits genericos y cierres

| Tema | Resultado |
|---|---|
| Sintoma | desde la manana del 25/10 los kits normales (p1 / los que no vienen del kitserver) salen genericos del PES 2021; los alternativos del kitserver cargan bien. Cierres en PES2021.exe+0x3fe31d (administrador de memoria del juego: escribe en +0x38 de un bloque sin memoria) |
| Descartado | la DLL: con la de antes de las 5 (94cb6b25, backup_20261025_055937) pasa igual. El sider.ini "limpiado" a las 05:49 (vuelto al de antes, copia en sider.ini.despues-limpieza) tampoco |
| Sospechas | texturas 4K de los brazos de TorsoBody puestas a las 05:42 (livecpk\TorsoBody\...\sourceimages\#windx11\arm_*.ftex, ~83 MB); common_package.fpk + hand_l/r.fmdl de Global Model reempaquetados a las 06:37 (sesion de las manos); el juego abierto desde el .exe sin el Switcher (carpeta save en vez de drmp, 24/10 22:10). MenuUniformes.lua no encuentra su firma en tiempo de ejecucion (en pes_text.bin esta en .text + 0x16829a5) |
| Prueba pendiente | sider.ini con TorsoBody comentado (cpk.root y TorsoBody_cuello.lua); si sigue, reaplicar la opcion en el Switcher |
| Estado en el PC | d3d11.dll = 94cb6b25 (antes de las 5), PES21FX.ini y shaders de pasto/cuerpo de las 05:59; lo nuevo en PES21FX\backup_20261025_0645_antes_de_restaurar. La DLL nueva (lote del cuerpo, 6518e810) esta en build-integracion-ropa-pasto |
| Coordinacion | tres sesiones instalaban en el PC a la vez (pasto, manos, ropa): no instalar en vivo sin avisar |

## Variantes robusto / atletico de TorsoBody (26/10)
- Un valor por jugador: z262 (0.5 robusto .. 1 TorsoBody .. 1.5 atletico; t = 2 (v - 1)). Va en los perfiles de
  musculatura_jugadores.ini, en los tipos de cuerpo (Delgado 1.10, Atletico 1.35, Fuerte 0.90, Corpulento 0.55, Culturista 1.50)
  y en el menu Fisico del juego (primera pagina de Musculatura: selector "Torso y brazos" Robusto 100 % .. Normal (TorsoBody) ..
  Atletico 100 % de a 20 % = z262 de a 0.1; el cuello paso a la pagina "Pecho, espalda y cuello"), en la ventana del mod y en la tarjeta
  "Musculatura: torso y brazos (robusto / atletico)" (x285 prendido, y285 texturas, z285 definicion).
- Modelo: datos por vertice (tools/fox/torso/make_variants.py -> textures\pes_var_<malla>.dds, R32G32B32A32_FLOAT, 7 filas
  por version: reposo, robusto (desplazamiento, normal, tangente), atletico). Mallas reconocidas por cantidad de indices
  (manos.cpp VariantDataFor): torso 26634 / 27201 / 34683 (pants_*_sub 1..3; la 3 tiene 3 versiones: out = inner, in, tight),
  brazos 37458 / 37461 / 34272 / 34278 y las capas de suciedad 36366 / 34275 / 32898. t127 del VS 1c02a92a y c6ef0c75; el
  shader elige la version por la posicion de reposo (< 1 mm) y si no coincide no deforma nada.
  - Forma grande del torso = field() de torso_variants.py (= PesTorsoField en pes_variants.hlsli y pesTorsoField en
    cloth_sim.hlsl): robusto panza 2.8 cm (llena abajo del ombligo, sobre el short), rollos de costados 1.6 cm, espalda baja,
    cintura, pecho caido (la masa baja 1 cm); atletico V de dorsales 1 cm, cintura y panza baja -0.9 cm, pecho lleno, trapecio
    alto 1.2 cm (arriba y atras) y medio 0.6 cm. Marcas medidas en la malla: ombligo (0, 1.061, 0.175), pezones (+-0.059,
    1.303, 0.158), linea de los hombros 1.552 (x 0.04) .. 1.494 (x 0.22), cintura del short 1.00 .. 1.08.
  - Relieve esculpido: promedio gaussiano 3D (sigma 2.5 cm torso / 1.5 cm brazos; el promedio por aristas dejaba rayas en las
    mallas refinadas de la v12.6) y el relieve de banda D - G(D); robusto quita 95 % de los picos y 70 % de los valles, atletico
    suma 25 % / 65 % (brazos 90/60 y 45/90). OJO: las normales calculadas de la malla de TorsoBody salen hacia ADENTRO (orden de
    los triangulos): se orientan con las del fmdl.
  - Costuras (brazos, cuello, short): quietas (desvanecido por distancia geodesica 1.2..4.5 cm desde el borde).
  - Normales / tangentes: las de TorsoBody giradas con el cambio de la normal de la malla soldada.
- Texturas (PS aa7d1833, la DLL las enlaza solo en el torso): t16 robusto / t17 atletico = normal delta (BC5_SNORM 2048x1024,
  el torso esta en v 0..0.5 del atlas -> el PS lee (u, 2v)); t18 = estrias del robusto (BC4, un poco mas claras). Convencion
  de torsob_nrm: rojo = +T, verde = +cross(N, T) * w (medido contra el relieve de la malla: correlacion del verde +0.39).
  Robusto: pliegues de la panza (irregulares), ombligo horizontal, rollos, pliegues de la espalda baja, pliegue del pecho caido,
  ~70 estrias (costados, espalda baja, bajo vientre, axila). Atletico: Adonis (distancia CON SIGNO: un "si/no" en la altura daba
  bordes filosos), serratos suaves, semilunares, linea alba, borde del pectoral, trapecio (fosa supraclavicular, masa, borde de
  atras), erectores. Definicion del normal map: torso x(1 + 0.15 atl - 0.75 rob) y todo el relieve x(1 - 0.3 rob); brazos
  x(1 + 0.20 atl - 0.50 rob); venas (musculos con textura): atletico 0.25 de base, el robusto las tapa 70 %.
- (26/10) RELIEVE AL REVES EN EL JUEGO: el VS 1c02a92a arma B = cross(N, T) * w en la vista (m_viewWorld), que es de mano
  izquierda (det -1) -> en el modelo el juego usa B = -cross(N, T) * w, y los normal maps de TorsoBody (torsob_nrm, arm_nrm,
  thigh_nrm, 4096) traen verde = +cross(N, T) * w: el relieve se veia iluminado desde abajo (biceps y espalda "hacia
  adentro"); con la definicion del atletico se notaba mucho (manchas oscuras). x286 = 1 (opcion "Relieve de TorsoBody" de la
  tarjeta de las variantes): el PS invierte el verde al final (r2.y antes de armar la normal), solo en el cuerpo con normal map
  de 4096 o mas; base, definicion, mapas delta y venas estan todos en la convencion de la textura, asi que se corrigen juntos.
  Las vistas previas (preview_nm.py) usan la convencion de la textura = lo que muestra el juego con x286 = 1.
- Menu Fisico: el selector "Torso y brazos" no resalta nada (el brillo tapaba la variante); circulo / Volver desde cualquier
  pagina del mod vuelve a la primera pestana de Fisico (cursor en "Musculatura  >"); L1 / R1 siguen de a una pagina.
- (26/10) COLUMNA MARCADA (x288, todos los torsos, con o sin variante): torso_maps.height_spine -> textures\
  pes_var_torso_spine_n.dds (BC5_SNORM 2048x1024, make_variants.py --spine-only), t19 del PS de la piel (manos.cpp
  SpineTextureFor: solo en los dibujos del torso, no depende de x285). Surco del sacro a C7 (espalda baja: 4.5 mm de hondo,
  sigma 1 cm; alta: 2.2 mm, 0.65 cm), erectores como dos columnas (4 mm a 3.6 cm de la linea media abajo, 1.4 mm arriba), su
  borde de afuera, apofisis apenas y C7. Linea media de la malla: z -0.036 (lordosis, y 1.07..1.16) .. -0.064 (y 1.43). El PS
  la multiplica por (1 - 0.5 robusto) (1 + 0.25 atletico). Vista previa: PNM_BACK=1 PNM_SPINE=k PNM_LIGHT=x,y,z preview_nm.py.
- (26/10) SELECTORES DE HOMBROS (menu Fisico, pagina 1, y ventana del mod; por jugador): fila 287 = x inclinacion (0.7 muy
  caidos .. 1.3 muy rectos), y tipo (1 normales, 2 estrechos, 3 anchos, 4 redondos), z tamano (0.7 .. 1.3). Van por los
  huesos (ComputeFactors): inclinacion = la tapa del deltoides sube / baja 4 cm por unidad (y se abre 1 cm) y el trapecio
  junto al cuello, la base del cuello al reves; la clavicula y el omoplato acompanan. Tipo: ancho -0.45 / +0.50 (como el
  slider "Ancho de hombros") y deltoides -0.08 / +0.04; redondos = deltoides +0.18 y frontal / posterior +0.45. Tamano:
  deltoides y ancho x0.8 de la variacion, trapecio, clavicula y omoplato acompanan. La ropa los sigue (zonas de la tela).
  En el menu Fisico K_VARIANT es ahora "selector de forma" con tabla de pasos (kLSel); ninguno resalta el cuerpo.
  Simulacion sin el juego: tools/fox/sim/hombros.py (copia de las cuentas; CASES=caidos,normal,rectos,...).
- Brazos: grosor por los huesos (ComputeFactors: biceps +0.05 rob / +0.04 atl, triceps +0.09 / +0.04, antebrazo +0.04 / +0.02,
  deltoides +0.03 / +0.04; la manga acompana con dsk_sleeve).
- Ropa: el reposo de la tela suma pesTorsoField con la t del juego de zonas de la prenda (z de su cabecera en t121; la DLL la
  pone por jugador) y el cuerpo con el que choca (pes_body_cs.hlsl, t120 enlazado por cuerpo.cpp) suma PesTorsoField.
- Filas de t120: 264 = (t general, tabla valida, prendido), 265 + i = t de la entrada i de la tabla de jugadores (skintone.cpp:
  un jugador entra tambien si su variante es distinta de la general); o6.w del VS lleva siempre 0.4 * torso (tambien 999 + ...).
- Vistas previas sin el juego: tools/fox/torso/preview.py (forma, rasterizador gris) y preview_nm.py (con normal map, albedo y
  los mapas delta; PNM_CLOSE=1 = primeros planos de trapecio, costado y pecho).
- (26/10 b/c) ESCULPIDO EN LA MALLA (x289 = 1, "Torso y brazos esculpidos"): textures\pes_var_<malla>.dds tiene 10 filas por
  version (0 reposo, 1..3 robusto, 4..6 atletico, 7..9 BASE: desplazamiento, normal, tangente absolutos). El VS mezcla base ->
  variante con |t| (PesVariantMorph en pes_variants.hlsli; con 7 filas, como antes, no hay base). Con x289 = 0 la base no se usa.
  - Relieve del normal map llevado a la malla (torso_relief.py): la pendiente del mapa en 3D (s3 = nx T + ny B, B = +cross(N, T)
    w) desenfocada 5 mm se integra en la malla soldada por minimos cuadrados (h_j - h_i = -s . (P_j - P_i)), se suaviza 6 mm
    (brazos 8 mm) y se le saca lo de mas de 4 cm: hp, std 2.3 mm, +-7 mm. Integrabilidad (residuo): torso 4.7 % con la
    convencion de arriba (63 % con un canal invertido); brazo 20 % (mismo signo; mas detalle pintado). Por zona (REGION):
    k_g = cuanto de hp va a la malla (normal adelante 1.0 / espalda 0.6; atletico 2.0 / 0.6; robusto 0.15); el mapa de cada
    variante queda micro + a lineas + (b - k_g costura) volumen (torso_base.region_deltas) para no ver el volumen dos veces:
    pes_var_torso_base_n.dds (t20 del PS, v 0..0.5) corrige la base de todos.
  - Esculpido a mano (torso_variants.sculpt_torso): atletico = borde y masa del pectoral, esternon, cinturon de Adonis con los
    oblicuos, dorsales abajo en V, redondo mayor, infraespinoso, rombo del trapecio, borde del omoplato, erectores (dos
    gaussianas espejadas: sin quiebre en la linea media) y el surco de la columna (sigma 1.1 cm). (26/10 c) la linea alba / el
    esternon del atletico quedaban de 7..9 mm de hondo (hp x2 + el relieve de la malla): se rellena el fondo (+4 mm abajo, +2.5
    mm entre los pectorales, sigma 1 cm) y quedan ~5 mm redondeados. Robusto = pliegue de la panza (-7.5 mm), rollos de los
    costados, espalda baja, el de abajo del omoplato y el pecho caido.
  - BRAZOS (26/10 c): marco propio (torso_variants.arm_frame): t a lo largo desde la costura del hombro (codo ~0.18, muneca
    ~0.43; el centro de cada seccion sigue el codo doblado, tabla _ARM_O1/_ARM_O2 medida en arm.fmdl) y angulo th (0 =
    adentro / hacia el torso, -90 = adelante = biceps, 90 = atras = triceps / olecranon, 180 = afuera). El codo esta 5 mm
    corrido hacia atras del eje recto hombro -> muneca: el olecranon mira atras. ARM_ANAT: biceps +3.2 mm, triceps (lateral y
    largo) +2.6, tendon del triceps -1.4, deltoides +1.6 y sus surcos, braquial, surcos de adentro / afuera, olecranon,
    epicondilos, pliegue del codo, braquiorradial +2.5, flexores +2.0, extensores +1.6, borde del cubito; base = ARM_ANAT,
    atletico 1.9 x (+ pico del biceps, herradura, antebrazo), robusto 0.3 x + grasa atras del brazo. hp del brazo con k_g
    (0.8, 0.3, 1.4) y recortado a +-3 mm (tanh): integrado traia pozos (atras del codo, banda arriba entre deltoides y biceps).
    arm_fill rellena lo que la malla de TorsoBody tiene por debajo de la superficie promedio (relieve de banda de 1.5 cm) entre
    t 0.09 y 0.26 (el pozo de atras del codo es de la malla original, no del mapa). El atletico solo amplifica 0.45 x los
    picos y valles de la malla (antes 0.90 los valles: hundia el pozo).
  - Vistas previas: preview_nm.py PNM_FB=1 (frente / espalda / 3/4), PNM_CLAY=1 (solo la malla), PNM_ARM=1 (brazo adelante /
    atras / desde arriba / desde abajo; las vistas aceptan inclinacion), PNM_NOBASE=1 (sin la base = TorsoBody original).
- (26/10 b/c) PIEL REAL (x293; y293 brillo, z293 translucidez): srm / trm de TorsoBody son parejos (brillo 0.9, rugosidad en el
  tope 0.5608 del PS, translucidez 0.143: piel de arcilla). El PS de la piel reemplaza r1.x (brillo) y r1.y (rugosidad) por
  zonas (t21 / t24) cortadas por los poros (hf = diferencia entre el normal map y su mip +2.5: menos brillo, mas aspero) y la
  translucidez del g-buffer por la de la zona; rubor suave en el albedo. Torso: pes_var_torso_skin_a/b.dds (2048 x 1024, v
  0..0.5; esternon, entre omoplatos, hombros y espalda baja mas grasos, costados mates, carne mas translucida, hueso menos).
  (26/10 c) Brazos: pes_var_arm_skin_a/b.dds (todo el atlas del brazo, 2048 x 2048: el PS elige las uv porque son cuadradas;
  manos.cpp SkinTexturesFor las da para las entradas de brazo de kVars): codo atras mas aspero, opaco y rojo, adentro del brazo
  y la muneca mas finos (translucidos, palidos), antebrazo de afuera algo mas aspero, hombro graso. Cerca de las costuras todo
  vuelve a DEF (0.85 / 0.45 / 0.30 / 0.5), el valor del PS sin textura (piernas, manos).
- (26/10 b) UNION DEL BRAZO (w293 = 1): junto a la costura del hombro (plano centro (0.263, 1.370, 0.020), normal (0.761,
  -0.643, -0.084)) el normal map del brazo baja al 55 % en los primeros 3 cm (el del torso es casi liso ahi).
- (26/10 c) CUELLO SIN CAMISETA (TorsoBody_cuello.lua 1.9, copia en tools/torsobody/): FUNDIR otra vez prendido (los cierres al
  salir no eran de ahi: con 1.8d apagado seguian; ahora la memoria del juego se retiene). Nuevo NORMALES_TORSO: el borde del
  cuello de la cara toma la normal del borde del torso (RING_N, 48 = las del cuello generico neck.fmdl en los puntos de LOOP:
  son identicas a las del torso, por eso el generico empalma perfecto) y en los 2 cm de arriba pasa de a poco a las suyas
  (smoothstep; la tangente se endereza). Se calcula ANTES de apply_plan: los parches no se pueden pisar (apply_patches falla
  entero si dos se solapan), asi que a los vertices nuevos se les cambian los valores del plan y a los demas se les escribe
  normal / tangente aparte. Cache _v19 (las caras ya cortadas con 1.8 se vuelven a cortar). Prueba sin el juego:
  tools/torsobody (fengari en node: OJO enteros de 32 bits en fengari, u32 tiene que ir en flotante; math.atan2 / frexp /
  collectgarbage de Lua 5.1 hay que ponerlos).
- (26/10 c) SIN SHORT (SinCamiseta 1.1, tecla 4; tools/sincamiseta/): sirve copias de pants_out_sub / pants_in_sub /
  pants_out_inner_sub / pants_out_sub_tight.fmdl desde Dream Patch Sider\content\sin-camiseta\sin_short\ (el juego guarda
  los modelos: salir y volver a entrar al jugador). Las arma tools/fox/pelvis/make_pelvis.py con los archivos de TorsoBody (NO
  van al repo: tienen el torso de TorsoBody). En cada copia la malla 0 (el short, material uni_pants) pasa a ser la PELVIS:
  - Topologia propia (pelvis_mesh.py): 7 filas del cuerpo (92 = el borde de abajo del torso EXACTO, 82, 74, 68, 64, 62, 60 a
    y 0.876), la ultima partida en dos mitades (F adelante y B atras en x = 0) + un puente de 4 vertices por debajo de la
    entrepierna (y 0.797) = el primer anillo de cada pierna; 2 anillos mas por pierna (por largo de arco) y el borde de arriba
    del muslo (thigh_short, 29 + 29, EXACTO). 692 vertices, 1236 triangulos (entran 790 / 1244; pants_in 804 / 1258): lo que
    sobra son vertices copia y triangulos vacios. Giro como TorsoBody: cross(P1 - P0, P2 - P0) hacia adentro.
  - Forma: radio por angulo alrededor de (x 0, z 0.058): superelipse (n 2.6) que se abre hacia la cadera (0.150 -> 0.184),
    frente liso tipo Ken (sin nada), gluteos (2 masas de 3 cm a th +-150, y 0.902) con la raya del medio, trocanter, ingle
    apenas. Arriba sigue la pendiente del torso (se mezcla en 3/4 del alto). Normales de los bordes = las de los anillos.
  - Pesos: el borde de arriba copia los del torso, el de abajo los del muslo (sk_thigh 65 %, dsk_thighmain 30 %, dsk_hip,
    dsk_thigh); en el medio se mezclan por altura. Los huesos del muslo no estaban en el archivo: los 6 que usaba solo el short
    se renombran en el lugar (pelvis_fmdl.RENAMES: dsk_hem_ba -> sk_thigh, dsk_hem_o_fake -> dsk_thighmain, dsk_hem_i ->
    dsk_thigh; la cadena se reescribe mas corta, la entrada copia padre / posiciones de thigh_short y su caja = los vertices).
  - Material = torso_mat (el tono de piel del jugador viene solo), alfa 0. UV en el cuadrante libre de abajo a la derecha de la
    textura del torso (u, v 0.5..1: piel pareja en torso_N_bsm, normal / srm / trm planos). Texturas propias (PS de la piel,
    reescaladas desde ese cuadrante): pes_var_pelvis_n (t20, poros), pes_var_pelvis_skin_a/b (t21 / t24, zonas) y
    pes_var_pelvis_alb (t23: tono +3.6 % como el borde del torso, manchas suaves, pliegues mas oscuros). manos.cpp PelvisDraw:
    VS de la piel + 3732 o 3774 indices (el short usa otro shader); la presencia de t23 le dice al PS que es la pelvis.
  - Vista previa: tools/fox/pelvis/pelvis_preview.py (arcilla con PNM_CLAY=1, o leyendo el .fmdl escrito).
