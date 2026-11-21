# Como carga el juego una pantalla de menu (.bin AFP) y que le pide a la pelicula
Leido el 15/11/2026 en el C decompilado + desensamblado (exe.py) + .rdata (base Ghidra). Pedido del Motor (hilo 3icxbh), puntos 1, 2 y 3.
Etiquetas: CONFIRMADO = leido en C y verificado en ensamblador o datos. INFERIDO = deducido, sin probar. NO SE = falta.

## En palabras simples
1. Cada pantalla del menu dice dos cosas al crearse: que archivo cargar (la ruta del .bin) y que pelicula usar de adentro (un nombre).
2. El juego carga el archivo una sola vez y lo comparte entre las pantallas abiertas; cuando se cierra la ultima que lo usa, lo borra de la memoria.
3. La forma de la pantalla (ventana, grupos, botones) NO sale de los nombres p_menu / c_menu_N: sale del nombre de SIMBOLO de cada clip de la pelicula:
   "_window", "_page", "_choice", "uir...".
Ejemplo: una pelicula nueva motorMain con un clip de simbolo "_window", adentro un "_page" y adentro 5 "_choice" = 5 botones que el juego ya sabe recorrer con el mando.
Para que te sirve: el Motor puede tener su pantalla propia (archivo nuevo entregado por Sider) usando la maquinaria nativa del menu.

## C++ reconstruido (comentado)
```cpp
// ---- Impl de menusystem::Window (Window+0x88) ----
struct ArchivoPantalla {          // 0x81 B, vector Impl+0x58..+0x68
  char ruta[128];                 // "cpk_dat/common/menu/mode/editSettingList.bin"
  uint8_t estado;                 // +0x80: 0 pedir, 1 esperando, 2 registrar, 3 listo
};
struct PeliculaPantalla {         // 0x50 B, vector Impl+0x70..+0x80
  std::string nombre;             // "editSettingList" (nombre de la pelicula AFP)
  std::string vacio;              // +0x20
  void* instancia;                // +0x40 nodo de la pelicula creada
  bool principal;                 // +0x48 (la principal arma el arbol de la pagina)
};

// FUN_140e41360: agrega un archivo a cargar (copia hasta 127 caracteres)
void Window_AgregarArchivo(Window* w, const char* ruta);
// FUN_140e41510: agrega una pelicula; si principal=1 y ya hay una principal, NO hace nada
void Window_AgregarPelicula(Window* w, const char* nombre, bool principal);

// Ejemplo real: base de todas las listas del Editar (FUN_140cd30d0)
MenuEditCmnSettingList::MenuEditCmnSettingList(...) {
  Window::Window(...);                                   // FUN_140e3ad70
  Window_AgregarArchivo(this, *(char**)0x1434c9ff0);     // ".../mode/editSettingList.bin"
  Window_AgregarPelicula(this, *(char**)0x1434c9ff8, 1); // "editSettingList"
}
// Una pagina puede cargar VARIOS archivos (matchPlan::MainMenu FUN_1408efcf0):
//   gamePlan.bin + parts/cmnArrowLarge.bin + parts/cmnIconNewLarge.bin, pelicula "gmPlanMainChoiceIconMenu".

// ---- vf8 de Window (FUN_140e45960 -> FUN_140e40c20): cargar, devuelve "todavia no" ----
bool Window_CargarArchivos(Window* w) {
  for (ArchivoPantalla& a : w->impl->archivos) switch (a.estado) {
    case 0: if (Gestor_Pedir(a.ruta, 1)) a.estado = 1; break;      // FUN_140e33650
    case 1: if (Gestor_EstadoArchivo(a.ruta) != 1) a.estado = 2; break; // FUN_140e33600
    case 2: if (Gestor_Registrar(a.ruta)) a.estado = 3; break;      // FUN_140e33a60
  }
  // (CONFIRMADO en asm) Registrar devuelve 0 mientras el archivo no este en estado 2:
  // si el archivo NO existe, la pagina queda "cargando" para siempre (INFERIDO: sin cierre).
}

// ---- Gestor global DAT_1437080b8: cache por RUTA con cuenta de usos ----
RecursoMenu* Gestor_Pedir(const char* ruta, int prio) {              // FUN_140e85290
  RecursoMenu* r = BuscarPorRuta(ruta);                              // FUN_140e85210 (strcmp exacto)
  if (!r) { r = new RecursoMenu(ruta, prio); /*0x98 B, FUN_140e332d0*/ Agregar(r); }
  r->usos++;                                                         // +0x90 (FUN_140e33640)
  return r;   // el pedido real al disco: menusystem::File 0xa8 B (FUN_140e85020), prio 0xc0 / 0x80
}
bool Gestor_Registrar(RecursoMenu* r) {                              // FUN_140e33730
  if (r->file == 0 || r->file->estado != 2 || r->file->datos == 0) return false;
  for (cada archivo del paquete) {
    if (empieza_con(datos, "TXP2")) RegistrarPaqueteAfp(nombre, datos); // FUN_140e85710 -> FUN_141f9c430
    else if (empieza_con(datos, "WFNT")) RegistrarFuente(...);          // thunk_FUN_140e2eea0
    // ".ifs" se saltea
  }
  LiberarBufferCrudo();  return true;
}
// Al cerrar la pagina (FUN_140e41630): borra las peliculas y resta un uso (FUN_140e33710);
// en 0 usos: da de baja paquetes y fuentes (vf2 de cada uno, FUN_140e333e0) y libera.
// => la proxima vez que se abre la pagina el .bin se vuelve a leer (por Sider) si nadie mas lo usa.

// ---- vf9 de Window (FUN_140e45bb0 -> FUN_140e44950): crear peliculas por NOMBRE ----
void* Window_CrearPrincipal(Window* w, const char* nombre) {         // FUN_140e44d50
  Pelicula* p = AfpModule_CrearPorNombre(nombre);                    // FUN_141f93eb0 (0 si no esta)
  if (!p) return 0;
  w->vf26(p);                                                        // ranura +0xd0
  Nodo* raiz = ArmarArbol(p);                                        // FUN_140e5a2e0 -> FUN_140e5a720
  w->impl->pelicula = p;  w->impl->raiz = raiz;                      // +0x88 / +0x90
  w->impl->ventana = Hijo(raiz, 0);                                  // +0x98 = primer "_window"
  w->impl->grupos  = CantidadHijos(w->impl->ventana);                // +0xa4 = cantidad de "_page"
  Window_TiposDeFila(w);                                             // FUN_140e43ef0
}

// ---- Como se arma el arbol (CONFIRMADO, flash::TraverseFuncGetWindow::vf1 = FUN_141fa2e40) ----
// Recorre todos los clips desde "/" y mira la propiedad 0x1027 = NOMBRE DE SIMBOLO de biblioteca
// (pw_get_symbol_name, FUN_1400c0e80). Compara el PRINCIPIO con strncmp (largo verificado en asm):
//   "_window"    -> flash::Window   (FUN_141fa36a0)  ventana (lista Root+0x70)
//   "_page"      -> flash::Page     (FUN_141fa33b0)  grupo dentro de la ventana actual
//   "_choice"    -> flash::Choice   (FUN_141fa3020)  fila / boton dentro del grupo actual
//   "_mask" (exacto, 5) / "_maskend"                 zona de mascara
//   "_duplicate" -> flash::Duplicate (FUN_141fa3170)
//   "uir"...     -> flash::Rect      (FUN_141fa3520) componente; la clase sale del nombre completo
//                   (uirSelector, uirSlider, uirTextInput, uirLoaderTex, uirHelpBox... cada uno con su
//                    menusystem::NodeRect*Def)
// Los nombres de INSTANCIA (p_menu, c_menu_N, selector, setName) NO arman el arbol: los usa el codigo
// propio de cada pagina para buscar hijos por nombre (FUN_140e8fab0).

// ---- Tipo de cada fila (FUN_140e43ef0) ----
void Window_TiposDeFila(Window* w) {
  for (g = 0; g < grupos; g++) for (f = 0; f < CantidadHijos(grupo g); f++) {
    if (grupo->columnas /*+0x18c*/) fila->vf80(alternado ? "color_1" : "color_0"); // etiquetas
    // 9 detectores en 0x143707d60 (los llena FUN_1401d8250): buscan un hijo de tipo de nodo
    //   2 selector, 0x11 deslizador, 0x12, 8, 0x14 (FUN_140e4f5d0(fila+0x98, tipo))
    tipo = primer detector que encuentra, o 0xff (boton comun);
    FUN_140e41fd0(w, tipo, g, f);
  }
}
```

## Pantallas con iconos (punto 3)
- menu::MenuModeChoiceBase (tabla 0x142723ec0, 85 ranuras, ctor FUN_140c8edd0): Window + general/modeMainMenu.bin (pelicula "modeMainMenu") + parts/cmnIconNew.bin. FUN_140c8fed0 / FUN_140c913d0 reproducen "cursorOut" / "cursorOver" en el hijo 1 de la raiz.
- matchPlan::MainMenu (tabla 0x142623a38, 84 ranuras, ctor FUN_1408efcf0, base matchPlan::Base FUN_14090fa20): gamePlan.bin, pelicula "gmPlanMainChoiceIconMenu" + cmnArrowLarge.bin + cmnIconNewLarge.bin. Depende de los datos del plan de juego: no sirve fuera.
- Etiquetas cursorOut / cursorOver / cursorDown / cursorUp tambien en una tabla generica 0x1434d1a10..28 (componente del menusystem, sin leer).
- OJO: gamePlan.bin lo reemplaza Dream Patch y modeMainMenu lo sirve Menu-server: no tocarlos; copiar la idea a un .bin con nombre propio.

## Los 3 pasos para usarlo en el Motor
1. Funcion exacta: una pagina propia = Window (FUN_140e3ad70) con una COPIA de tabla (como la Apariencia del Motor) y, en el gancho de la fabrica, ANTES de la vf8: cambiar Impl+0x58[0].ruta (strcpy, < 128) y Impl+0x70[0].nombre (asignar con FUN_1404023c0(std::string*, char*, largo), la del juego), o en un ctor propio llamar a FUN_140e41360 / FUN_140e41510 con los textos propios.
2. Compilar en nuestra DLL: solo eso (dos textos) + las ranuras propias que ya tiene el Motor. Lo demas (cargar, navegar, sonido) es nativo.
3. Archivo: content\...\common\menu\mode\motorMain.bin servido por Sider (INFERIDO fuerte por el manual de Sider: un archivo que existe solo en la raiz livecpk se entrega; el juego lo pide como "cpk_dat/common/menu/mode/motorMain.bin").

## Lo que falta / NO SE
- Que estado pone el sistema de archivos cuando el archivo no existe (la pagina queda esperando: INFERIDO, probar con un nombre que si exista primero).
- Si una pelicula sin "_window" deja la pagina vacia o la cuelga (las funciones miran punteros nulos: INFERIDO vacia).
- Que exige el C++ de CADA pagina (setNumChoices, setBtn...): es el punto 5 (llamadas a metodos del script).
- Lista completa de componentes uir* y su tipo de nodo.
