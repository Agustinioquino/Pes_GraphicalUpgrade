# Como se hablan el C++ del juego y la pelicula AFP (punto 5 del Motor)
Leido el 15/11/2026 en el C decompilado + desensamblado. CONFIRMADO / INFERIDO / NO SE como siempre.

## En palabras simples
1. El juego casi nunca "llama funciones" del script: le dice a cada clip que vaya a una ETIQUETA de su linea de tiempo.
   Ejemplo: para mostrar 7 filas reproduce la etiqueta "setNumChoicesAuto_7"; para el cursor, "cursorOver"; para filas alternadas, "color_0" / "color_1".
2. Cuando si necesita llamar una funcion del script (ej. "create" con un numero), usa ExternalInterface: el script registra la funcion con addCallback y el juego la llama por nombre.
3. Para avisarle algo al juego, el script usa ExternalInterface.call (o fscommand).
4. Ademas el juego lee y escribe propiedades de los clips directamente (posicion, visible, color, tamano).
Para que te sirve: una pantalla nueva solo necesita las etiquetas y clips con los nombres que espera la pagina; el script puede ser minimo.

## C++ reconstruido (comentado)
```cpp
// ---- 1) Etiquetas (lo mas usado) ----
// menusystem::Node::vf16 (0x140e4e3d0): nodo->ReproducirEtiqueta(texto, bandera)
//   convierte el texto y llama vf17 -> vf11(+0x58) con el clip (+0x40) (INFERIDO: termina en
//   flash::TraverseFuncGotoPlayLabel / afp_mc_goto_play_label 0x1400bedf0).
void Window_PonerCantidadVisible(Nodo* n, uint8_t k) {        // FUN_140e43ba0
  if (k == 0) k = 1; else if (k > 18) k = 18;
  std::string e = "setNumChoicesAuto_" + std::to_string(k); // "setNumChoicesAuto_1".."_18"
  n->vf16(e.c_str(), 1);                                    // ETIQUETA, no metodo
}
// Otras etiquetas vistas en el C: "color_0"/"color_1" (FUN_140e43ef0), "cursorOut"/"cursorOver"/"cursorDown"/"cursorUp",
// "tex_on"/"tex_off"/"tex_fadeIn" (cargadores), "btn_on"/"btn_off" (en el archivo).

// ---- 2) C++ -> funcion del script (ExternalInterface.addCallback) ----
struct PedidoLlamada { std::string nombre; void* nodos; void* buffer0x620; /*...*/ };
void Pedido_Crear(PedidoLlamada* p, const char* nombre);   // FUN_141f93a80 (arma <invoke name=...>)
void Pedido_AgregarInt(PedidoLlamada* p, int v);           // FUN_141f93c20 (tipo 6)
int  AfpModule_Llamar(Pelicula* peli, PedidoLlamada* p);   // FUN_141f93e40: busca 'nombre' en la lista de
                                                           // callbacks registrados (DAT_14373b340+0x228) y la ejecuta
void Pedido_Borrar(PedidoLlamada* p);                      // FUN_141f93b30
// Ejemplo real (FUN_140e542a0, desplazamiento de una lista):
//   Pedido_Crear(&p, "create"); Pedido_AgregarInt(&p, (int)pos); if (AfpModule_Llamar(peli, &p) >= 0) ...; Pedido_Borrar(&p);
// Deslizador (FUN_140e5c190): el pedido ya armado vive en el nodo (+0xe8), FUN_141f93ce0 cambia el valor y llama.

// ---- Lado del reproductor: metodos nativos de ExternalInterface (FUN_1400d1040) ----
//   id 0x935 addCallback(nombre, funcion): agrega {tipo, nombre, ..., +0x20 funcion} (0x30 B) a DAT_14373b340+0x228.
//   id 0x4e9 call(nombre, args...): arma un arbol "invoke" con los argumentos (bool, int, double, texto)
//            y llama al gancho del anfitrion PTR_FUN_14373b6d0 (FUN_140041ef0); lee la respuesta.
//   fscommand = id 0x43d; FSCommand:fullscreen / showmenu / allowscale / quit los atiende el reproductor
//   ("unknown FSCommand" en FUN_140026420).

// ---- 3) Propiedades de clips (ya documentado: memoria afp-propiedades-rect) ----
//   leer FUN_1400c17b0(clip, 0x1000..0x1035, out), escribir FUN_1400c38c0. 0x1027 = nombre de simbolo.
```

## Lo minimo que tiene que traer una pelicula nueva (INFERIDO de todo lo leido)
1. Clips con simbolo "_window" / "_page" / "_choice" (arman el arbol, ver 20261115_pantallas_menu_carga.cpp.md).
2. Las etiquetas que la pagina va a pedir (si usamos una pagina nativa copiada: "setNumChoicesAuto_N", "color_0/1", "cursorOver"...). Si falta una etiqueta, NO SE si avisa o ignora (el reproductor tiene textos de error "afp_mc_goto_play_label").
3. Script solo si una pagina llama funciones por ExternalInterface (con addCallback de ese nombre). Para una pagina propia del Motor, nosotros decidimos que llamar.

## Lo que falta / NO SE
- Quien pone el gancho PTR_FUN_14373b6d0 (lado juego de ExternalInterface.call): esta en .data, no se lee de la base; sale de la inicializacion del reproductor (buscar con la PC o en el volcado).
- Que hace la pagina con lo que recibe de ExternalInterface.call (nombres que espera).
- Que pasa si falta una etiqueta.
