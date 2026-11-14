// PES21 FX (14/11) - pagina "Sider" del menu Insert (siderui.cpp). Agustin: "puedes leer el sider ini, y que se modifique el menu
// para mostrar lo que esta como modulo lua ... que tenga interfaz, ademas adaptar la interfaz para cada modulo", "como si fueran
// tarjetas", "en el caso de los modulos lua que hicimos nosotros esos si una interfaz visual o con slider adaptable".
//  * Lee sider.ini (las lineas lua.module, prendidas y comentadas) y cada modulo de la carpeta modules: descripcion (los
//    comentarios del principio), version, eventos que registra (ctx.register), teclas (vkey == ...) y su .ini si tiene.
//  * Una tarjeta por modulo. Si el modulo tiene .ini, sus valores se editan ahi (numeros = tiras, 0 / 1 = interruptores) y se
//    guardan en el .ini (lo usa el modulo con su tecla de recarga o en el partido siguiente).
//  * Nuestros modulos (PES21FX_*) llevan tarjeta propia: PES21FX_redes con los presets en tarjetas, red mojada y los valores de
//    [personal] con tiras.
//  * Prender / apagar un modulo cambia sider.ini: queda PENDIENTE y se escribe al cerrar el juego (Sider ya esta cargado), con
//    copia sider.ini.antes_menu_<fecha>.
// Todo con std::filesystem + ImGui: la vista previa de Linux (scratchpad) lo dibuja igual que el juego.
#pragma once
#include <filesystem>
#include <string>

namespace fx {

void SdSetDir(const std::filesystem::path& siderDir);   // carpeta de Sider (la que tiene sider.ini); vacia = no hay Sider
void SdPage(float k, bool anim, double now);             // contenido de la pagina (despues del titulo)
void SdOpen(const std::string& module);                 // abre los ajustes de un modulo (p. ej. desde la busqueda)
int SdPendingCount();                                    // cambios de sider.ini pendientes (para el punto del menu)
int SdModuleCount();                                     // modulos activos (0 si no se leyo)
bool SdApplyPending(std::string* log = nullptr);         // escribe sider.ini con los cambios pendientes (al cerrar el juego)

} // namespace fx
