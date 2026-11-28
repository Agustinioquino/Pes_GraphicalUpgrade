# El script (ActionScript 3) de las peliculas AFP: como lo corre el exe
Leido el 15/11/2026 en el C decompilado + desensamblado + tablas de .rdata (base Ghidra). Pedido del Motor (3icxbh), punto 4 (a, b, c).
CONFIRMADO = leido en C y en bytes. INFERIDO = deducido. NO SE = falta.

## En palabras simples
1. El script de cada pantalla es ActionScript 3 normal, pero Konami le cambio los numeros de las instrucciones por otros mas cortos (de 0x01 a 0x98).
2. El juego trae adentro una tabla que traduce "instruccion normal de Flash -> numero de Konami". La saque entera: 134 instrucciones con su nombre.
3. Los numeros que acompanan a cada instruccion NO son de 2 bytes fijos: son el formato normal de Flash (7 bits por byte). Ejemplo: "B3 01" = 179.
4. El juego trae 1836 nombres propios (MovieClip, addEventListener, gotoAndPlay...) que el archivo nombra por numero. Tambien los saque.
5. Dato muy util: si una funcion del archivo no tiene la marca "ya traducido", el juego traduce solo las instrucciones normales de Flash al cargar.
Para que te sirve: con esto la PC ya puede desarmar (y despues armar) el codigo de una pantalla.

## Tablas sacadas (archivos)
- /mnt/project-files/decompilado/tablas/afp_abc_opcodes.tsv: interno, opcode AVM2 normal, nombre, operandos, bandera, manejador.
- /mnt/project-files/decompilado/tablas/afp_nombres_internos.tsv: id (0x100..0xd0f) -> nombre interno (1836). Ej. 0x145 numChildren, 0x20c undefined, 0x253 "", 0x305 MovieClip, 0x881 flash.display.
- En el exe: traduccion AVM2 -> interno 0x1429c9b80 (256 B, la devuelve FUN_1400ef040); operandos 0x1429c9c80 (2 B por interno: [0] el mismo numero, [1] bandera); manejadores 0x1429ca2e0 (256 punteros; 0x14016d410 = invalido, 122 huecos); nombres FUN_14009e640 (cubetas 0x1429bf660, indices 0x1429bfb00, textos 0x1429b9900).

## Bandera de operandos (orden de lectura, FUN_1400f1840)
0x20 u8 primero | 0x10 s24 (salto, 3 B) | 0x01 u30 | 0x40 u8 | 0x02 u30 | 0x04 u8 | 0x08 u8.
u30 = varint de Flash (7 bits por byte, bit 0x80 = sigue; FUN_1400f4c70). lookupswitch (interno 0x16): s24 + u30 n + (n+1) s24. newarray2 (0x8a, Konami): u8 tipo + u30 n + n (tipo 1/4) o 2n (tipo 2/3/5/9) bytes.

## Instrucciones propias de Konami (no salen de la tabla)
89 empuja int 0 (CONFIRMADO asm) | 8d abcop_initlocal | 8e local[a]=null | 8f abcop_convertsetlocal | 90 local[a]=pop | 91 local[a]=tope (sin sacar) |
92 abcop_convertduplocal | 93 local[a]=tope | 94 local[b]=local[a] | 95 abcop_convertlocal2local | 96 local[u8]=local[a] | 97 abcop_astypelatesetlocal |
7e variante de getlex + u8 / 7f variante de findproperty + u8 (INFERIDO: mismo camino que 3f / 3e). En las "b sin uso" el segundo u30 se lee y no se usa.
Verificado por textos de error dentro de los manejadores: 01 throw, 24 swap, 2c/32 construct/constructprop, 2e callproperty, 31 constructsuper, 36 applytype ("0x53"), 38 newarray, 3f getlex, 40 setproperty, 45 getproperty, 48 getslot, 49 setslot.

## Constructor de ejemplo del Motor, desarmado
73 getlocal_0 | 28 pushscope | 3e B3 01 findproperty #179 | 89 push 0 | 40 B3 01 setproperty #179 |
3e B4 01 findproperty #180 | 89 | 40 B4 01 setproperty #180 | 3e B5 01 findproperty #181 | 38 00 newarray 0 | 40 B5 01 setproperty #181 | 2f returnvoid
= "a = 0; b = 0; c = [];" (25 B = el largo de la cabecera 0x19).

## Cabecera de cada cuerpo (en el archivo big endian; el juego la usa en memoria ya dada vuelta, INFERIDO)
+0 banderas | +1 ? (0) | +2 max_stack (tamano de la pila) | +3 locales | +4 init_scope_depth / +5 max_scope_depth (INFERIDO por orden AVM2, el ejecutor no los lee) |
+6 u16 numero de metodo (dato del Motor) | +8 u16 donde empieza el codigo (0x0C) | +0xA u16 largo del codigo | si bandera 0x01: +0xC u8 cantidad de excepciones y +0xE u16 donde esta su tabla (10 B: desde, hasta, destino, tipo +6...).
Banderas (+0): 0x01 tiene excepciones | 0x08 ya traducido a numeros internos | 0x10 empieza con getlocal_0 + pushscope (el ejecutor salta esos 2 B y lo hace solo) | 0x20 metodo vacio (73 28 2f) | 0x40 constructor trivial (73 28 73 31 00 2f). 0x10 / 0x20 / 0x40 los pone el juego al cargar (FUN_1400221a0).

## C++ reconstruido (comentado)
```cpp
struct AbcCtx {              // lo arma FUN_140101b20 en la pila; los manejadores reciben &ctx
  void* self; Variant* thisArg; MethodInfo* metodo; Abc* abc; uint8_t* cuerpo;
  uint8_t* inicio; uint8_t* pc;   // +0x28 / +0x30
  uint8_t* fin;                   // +0x38
  int16_t nPila; Variant* pila;   // +0x40 / +0x48 (Variant = 16 B: u16 tipo, u16, 8 B valor)
  Variant* locales;               // +0x68
  Variant excepcion;              // +0xc0
  int estado; bool lanzado;       // +0xd4 / +0xd8
};
// Tipos de Variant vistos: 0 undefined, 2 boolean, 3 int, 0xc null / objeto.
typedef void (*Manejador)(AbcCtx*);
extern const uint8_t  kTraduccion[256];     // 0x1429c9b80: opcode AVM2 -> interno
extern const uint8_t  kOperandos[256][2];   // 0x1429c9c80
extern const Manejador kManejador[256];     // 0x1429ca2e0

// Al cargar el ABC (FUN_1400221a0 -> FUN_1400f1840): traduce si hace falta
void TraducirCuerpo(uint8_t* c) {
  if (c[0] & 0x08) return;                  // ya viene en numeros internos (los de Konami)
  for (uint8_t* p = c + U16(c+8); p < c + U16(c+8) + U16(c+10); ) {
    *p = kTraduccion[*p];                   // reescribe en el lugar
    p = SaltarOperandos(p, kOperandos[*p][1]);
  }
  c[0] |= 0x08;
}

// Ejecutar un metodo (FUN_140101b20)
uint32_t EjecutarMetodo(void* self, Variant* thisArg, MethodInfo* m) {
  uint8_t* c = CuerpoDe(m->abc, m->indice);           // FUN_1400220c0
  AbcCtx x{...};  x.pc = x.inicio = c + U16(c+8);  x.fin = x.inicio + U16(c+10);
  ReservarPila(&x, c[2]);  ReservarLocales(&x, c[3]); // local 0 = this, 1..n = argumentos
  if (c[0] & 0x10) { x.pc += 2; EmpujarScope(&x, thisArg); }
  while (x.pc < x.fin) {
    if (x.lanzado) { /* busca en la tabla de excepciones (c[0] & 1) y salta a 'destino' */ }
    uint8_t op = *x.pc++;
    kManejador[op](&x);                     // cada manejador lee sus operandos desde x.pc
  }
}
```

## Lo que falta / NO SE
- El formato de los pools pre-parseados ya lo tiene la PC (memoria afp-abc-preparseado); no lo volvi a leer.
- Donde se usan las banderas 0x20 / 0x40 (atajo para metodos vacios: INFERIDO).
- El byte +1 de la cabecera.
- Punto 5 (como el C++ llama a un metodo del script y como el script le avisa al C++): sigue.
