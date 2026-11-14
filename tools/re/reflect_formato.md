## 1. Como registra Fox Engine las propiedades (formato con offsets)

Todo lo que sigue sale del C de Ghidra (`export/c/c_1401e2370.c`, `c_1402583c0.c`) y del desensamblado. Lo cruce con
las llamadas reales de los registradores de TimeOfDaySettings `0x141923f90`, TppAtmosphere `0x141943ea0` y
GrShadowSettings `0x14183de90`.

### 1.1 PropertyInfo (0x48 B), con certeza
La fábrica es `0x14023fb90(tipo, String* nombre, offset, arraySize, contenedor, p6, p7, p8)`. Hace un `switch` sobre el
tipo, de 0 a 0x18, reserva 0x48 B con `0x1403cf460(0x48,8,0x10001)` y llama al ctor del tipo (Int8 = `0x1402348b0`,
UInt8 = `0x140234940`, etc.). Lo que hace el ctor de Int8 es literalmente esto:

```
*(u32*)(p+0x08) = 0 (tipo, 1 en el ctor de UInt8, ...)   *(u32*)(p+0x0c) = -1
String(p+0x10) <- nombre                                 *(u32*)(p+0x2c) &= 0xfffffff0
*(u32*)(p+0x24) = contenedor                             *(u32*)(p+0x28) = p6
*(u64*)(p+0x30) = p7                                     *(u64*)(p+0x38) = p8
*(u64*)(p+0x18) = OFFSET                                 *(u32*)(p+0x20) = arraySize
*(u64*)(p+0x40) = 0
```

| Offset | Tipo | Campo | Evidencia |
|---|---|---|---|
| +0x00 | vtbl | `fox::<T>::PropertyInfoT` (una por tipo) | ctor 0x1402348b0 |
| +0x08 | u32 | **tipo** (0..24, tabla abajo) | cada ctor escribe su constante. 0x140267440 lo relee como `*(u32*)(pi+8)` |
| +0x0c | u16 | índice de la propiedad en la clase (= cantidad de props previas, `EntityInfo+0x20`) | 0x140266f70 |
| +0x0e | u16 | copia del índice, solo si `EntityInfo+0x113` | 0x140266f70 |
| +0x10 | String | nombre. El hash StrCode64 (48 bits) está en `*(+0x10)+0x10` | 0x14026a040 compara `& 0xffffffffffff` |
| **+0x18** | u64 | **offset del campo dentro de la instancia** | 3.er arg. de la fábrica = `param_4` de 0x140266f70 (`r9d`). TimeOfDaySettings: `mov r9d,0x148` antes de `call 0x140266f70` para "sunLux" |
| +0x20 | u32 | arraySize (1 salvo arreglos estáticos: 0x1402672e0) | 4.º arg. de la fábrica |
| +0x24 | u32 | contenedor: 0 StaticArray, 1 DynamicArray, 2 StringMap, 3 List | wrappers 0x140266f70/0x140266d40/0x140267350/0x140267260 |
| +0x28 | u32 | p6, flag (bit 15 de la entrada de tabla; siempre 0 en los registradores vistos) | |
| +0x2c | u32 | flags. **bit 0 = propiedad virtual** (sin memoria: la leen/escriben un PropertyHandler o un bit de `flags`), bits 1-2 = clase de puntero para EntityPtr (\|2, \|6) | 0x140266cf0 hace `\|= 1`. 0x14026ba00 pone además `+0x18 = 0` |
| +0x30 | ptr | p7: `EntityInfo*` de la clase apuntada (EntityPtr) | wrappers 0x140266dc0.. (`r9` = getter de la clase) |
| +0x38 | ptr | p8: `EnumInfo*` (0x14028f330(nombre del enum)) | 0x140267080 |
| +0x40 | ptr | accesor Lua (`0x140263a90` lo enlaza; el accesor apunta de vuelta en +0x08) | 0x140263a90 |

**El offset está en +0x18, no en +0x20.** +0x20 es arraySize. Confirmado de dos maneras:
- 0x140267440 (struct embebido) arma cada subpropiedad con `fábrica(*(u32*)(pi+8), "%s_%s", *(u64*)(pi+0x18) + base, *(u32*)(pi+0x20), *(pi+0x24), *(pi+0x28), *(pi+0x30), *(pi+0x38))`. Es decir, suma la base al campo +0x18 y deja +0x20 como arraySize.
- El ctor guarda el 3.er parámetro (`param_3`, el `r9` del wrapper) en `param_1[3]` = +0x18.

Tipos (switch de 0x14023fb90 y vtables `fox::<T>::PropertyInfoT`):
0 Int8, 1 UInt8, 2 Int16, 3 UInt16, 4 Int32, 5 UInt32, 6 Int64, 7 UInt64, 8 Float, 9 Double, 10 Bool, 11 String, 12 Path,
13 EntityPtr, 14 Vector3, 15 Vector4, 16 Quat, 17 Matrix3, 18 Matrix4, 19 Color, 20 FilePtr, 21 EntityHandle, 22 EntityLink,
23 PropertyInfo, 24 WideVector3.

### 1.2 Las funciones de alta (todas meten el PropertyInfo en el HashMap `EntityInfo+0x20` con `0x1415e53e0`)
| Función | Firma | Qué fija | Llamadores |
|---|---|---|---|
| 0x140266f70 | (info, tipo, String* nombre, offset) | Static, arr 1 | 1195 |
| 0x140266d40 | (info, tipo, nombre, offset) | DynamicArray | 101 |
| 0x140267350 | (info, tipo, nombre, offset) | StringMap | 54 |
| 0x140267260 | (info, tipo, nombre, offset) | List | 3 |
| 0x1402672e0 | (info, tipo, nombre, offset, arraySize) | arreglo estático [N] | 3 |
| 0x140267080 | (info, tipo, nombre, offset, "NombreEnum") | enum (p8 = 0x14028f330(nombre)) | 93 |
| 0x140266dc0 / e20 / f00 / e90 | (info, nombre, offset, EntityInfo* apuntada) | EntityPtr Static/Dyn/StringMap/List | 25/19/10/2 |
| 0x140267120 / 180 / 1f0 | (info, nombre, offset) | FilePtr Static/Dyn/StringMap | 56/4/2 |
| 0x1402673d0 | (info, nombre, offset) | UInt8 StringMap | 5 |
| 0x14023fb90 + 0x140266cf0 | fábrica(tipo, nombre, 0, 1, ...) y luego add(info, pi) | **virtual** (flags \|= 1) | 143 |
| 0x140267440 | (info, EntityInfo* struct, String* prefijo, offsetBase) | struct embebido: copia cada prop del struct como `prefijo_prop` con offset base+off | 11 |
| 0x140299320 | (info, embed, índice, nombre, offset, arraySize, tipo, p8, ptrClass, contenedor, nombreLua, modo, enum, ptrKind, handler, virtual) | **entrada de tabla estática** de 0x38 B en `EntityInfo+0x108`. La procesa después 0x14026ba00 | 18 (Data, DataElement, TransformData, TransformRT, Rotation) |

Entrada de tabla (0x38 B, 0x140299320 / 0x14026ba00): +0x00 nombre, +0x08 nombre Lua, +0x10 modo ("RW"), +0x18 enum, +0x20 u16 offset,
+0x22 u16 arraySize, +0x24 u32 bits: 0-9 índice, 10-14 tipo, 15 p8, 16-17 contenedor, 18 embebido, 19 virtual, 20-21 ptrKind;
+0x28 EntityInfo apuntada, +0x30 handler.

### 1.3 Accesor Lua `0x140267cc0(info, nombre, nombreLua, modo, handler)`
- Busca el PropertyInfo por nombre (`0x14026a040`, hash en `EntityInfo+0x20`). **Si no existe, no hace nada.** Eso le pasa a `TppLightProbeArray.drawRejectionLevels`: tiene accesor pero ninguna propiedad registrada (fila ACCESS_ONLY).
- Parsea el modo: `R` -> leer = 2, `r` -> 1, `W` -> escribir = 2, `w` -> 1. Crea un objeto de 0x40 B (`0x1402996b0`):
  +0x00 String nombreLua, +0x08 PropertyInfo*, +0x10 EntityInfo*, +0x18 handler, +0x28 double 1.0, +0x30 double -1.0, +0x38 flags.
  En los flags: `r` = |1, `R` = |5, `w` = |2, `W` = |10. Lo inserta en `EntityInfo+0x50` y lo enlaza en `PropertyInfo+0x40`.
- Que la mayúscula agregue los bits 4 y 8 (exportado/serializable además de Lua) es una **inferencia**. Modo vacío = sin acceso Lua.

### 1.4 Registro de clase
- `0x14026bf00(&res, "Clase", info)`: inserta en el diccionario global `0x1433a9010`. `0x14026bfb0` hace lo mismo para los structs valor (Transform, AABB...) con tag 0x50004.
- `0x14026cbd0(info, versión)` y `0x14026cbb0(info, superInfo)`.
- `info + 0xb0` = functor de creación.
- Getter dinámico: `0x140265bd0(info, tamaño, categoría, tag)` dentro de un `_Init_thread_header`.
- Getter estático: `0x1402659c0(info, "Clase", super, tamaño, ver, ..., regfn en [rsp+0x40], ...)`.
- Enums: `0x14028f1e0(flag, "Enum", 0)` y `0x14028f4f0(enum, valor, "NOMBRE")`.

### 1.5 Cómo trabaja reflect.py
1. Busca con numpy todos los `call rel32` (E8) hacia las funciones de 1.2-1.4: 6151 sitios en 1188 funciones.
2. Desensambla cada función con objdump (4 procesos, unos 7 s) y la emula linealmente: `mov/lea/xor/add` con inmediatos, `lea [rip]`, slots `[rsp+0x20..0x98]` para los argumentos de pila, y los objetos String temporales (`0x1403d2990` -> `0x1403d2240` -> `0x1403d2000`) para saber el nombre que llega en `r8`.
3. Resuelve el EntityInfo:
   - `rcx` = retorno de un getter, mapeado a la dirección del EntityInfo con el getter.
   - El nombre sale de `0x14026bf00`/`0x14026bfb0`.
   - Las funciones que solo agregan propiedades (FixDemoCameraData...) se resuelven por el getter.
   - Los registradores estáticos (`rcx` de entrada) se resuelven por el regfn guardado en `[rsp+0x40]` de `0x1402659c0`.
4. Expande los structs embebidos de forma recursiva (`transform_rotation_quat`).

**Por qué no alcanzaba el C de Ghidra**: en unas 485 llamadas Ghidra pierde el 4.º argumento. Por ejemplo, `FUN_140266f70(lVar1,0xe,&local_res8)` para "rayleighScatteringCoefficient", donde el asm hace `mov r9d,0x70 ; lea edx,[r9-0x62]` (el tipo 0xe sale de r9). En otras llamadas Ghidra lo desplaza: la pasada anterior tenía TppAtmosphere.gmtTimeDifference en +0x278, cuando el asm dice `mov r9d,0x270`, y month en +0x280 cuando es +0x27c.

Resultado: 1766 filas, 323 clases, **0 propiedades con memoria sin offset, 0 clases '?'**. Son 1752 propiedades: 149 virtuales, 15 expandidas de structs, 10 filas EMBED y 1 ACCESS_ONLY. Hay 22 offsets que difieren de `fox_core_schema.tsv`; revisé varios contra el asm (gmtTimeDifference, month, EnlightenModel.modelFile = `mov r8d,0x130`) y en todos el error estaba en el TSV viejo.

### 1.6 Validación obligatoria (memoria del proyecto)
| Propiedad | Esperado | reflect.py | Sitio de la llamada | OK |
|---|---|---|---|---|
| TppAtmosphere.latitude | +0x208 | +0x208 (Float) | 0x14194458c | sí |
| TppAtmosphere.longitude | +0x20c | +0x20c (Float) | 0x1419445e2 | sí |
| TimeOfDaySettings.sunLux | +0x148 | +0x148 (Float) | 0x141924397 | sí |
| GrShadowSettings.depthBias | +0x8c | +0x8c (Float) | 0x14183e2f1 | sí |

Los cuatro coinciden, sin discrepancias. El offset es relativo al **inicio de la instancia** de la entidad (el objeto Fox, `this`), no al DataBody.

### 1.7 Cómo usarlo desde la DLL
- Con el offset fijo de esta versión: `*(float*)(entidad + 0x148)` = sunLux de una instancia de TimeOfDaySettings.
- Para resolverlo en tiempo de ejecución sin hardcodear: leer el diccionario `0x1433a9010` (solo lectura, ver fox_core.md), tomar el EntityInfo, recorrer el HashMap `EntityInfo+0x20` (+0x24 capacidad, +0x38 cabezas, +0x40 nodos de 0x18 B con la String en `[0]`, +0x48 `PropertyInfo*`) y leer `PropertyInfo+0x18`.
- Las propiedades **virtuales** no tienen offset: van por el PropertyHandler o por un bit de `flags`/`localFlags`. Para tocarlas hay que llamar al setter Lua o cambiar el bit; no hay un campo directo.

### 1.8 Columnas de reflexion.tsv
`class, property, type, offset (hex o "virtual"), arraySize, container, enum, enum_values, mode (R/W Lua), lua_name, virtual,
ptr_class (EntityPtr/embebido), class_size, super, category (módulo Fox), regfn, callsite, kind (PROP / PROP_EMB / EMBED / ACCESS_ONLY)`.

Las frases de "Significado" salen de un glosario escrito a mano para las clases gráficas. El resto se arma a partir del nombre y queda marcado "(por nombre)": es una inferencia, no algo verificado en el código que lo usa.
