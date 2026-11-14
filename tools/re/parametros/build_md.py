#!/usr/bin/env python3
import csv, collections, re, os, json
OUT = '/home/claude/re/out'
csv.field_size_limit(10**9)
rows = list(csv.DictReader(open(f'{OUT}/parametros_todos.tsv', encoding='utf-8'), delimiter='\t', quoting=csv.QUOTE_NONE))
st = json.load(open('/tmp/claude-0/-home-user-Pes-GraphicalUpgrade/9c04a2d7-26f7-5e59-8a9a-a2e213e44c11/scratchpad/stats.json'))
CATS = ['luz','cielo_clima','sombras','postprocesado','camara','cesped_estadio','mallas_modelos','huesos_animacion',
        'fisica_ropa','texturas_materiales','gameplay','modos','ui_menus','archivos_io','red_online','sistema']
def origen(x):
    s = x['fuente'].split(';')[0].split(' ')[0]
    if s.startswith('constant'): return 'constant'
    if x['tipo'].startswith('funcion Lua'): return 'lua_func'
    if x['direccion_o_offset'] == 'campo de tabla Lua': return 'lua_campo'
    if s.endswith('.tsv'): return 'reflexion'
    return 'md'
by = collections.defaultdict(list)
for x in rows: by[x['categoria']].append(x)
empty = lambda v: v in ('', '?')
L = []
L.append('# Tabla unica de parametros de PES 2021 (1.07.02, base 0x140000000)\n')
L.append(f'Archivo: `/home/claude/re/out/parametros_todos.tsv` ({len(rows)} filas, una por parametro, separador TAB, UTF-8). '
         'Generado por `/home/claude/re/out/parametros_scripts/build_params.py` (+ `build_md.py` para este documento) a partir de todos los informes de `/home/claude/re/out/`.\n')
L.append('## 1. Columnas\n')
L.append('| Columna | Contenido |\n|---|---|')
for c, d in [('id', '`P00001`..: identificador estable dentro de esta version del archivo'),
             ('categoria', 'una de las 16 categorias pedidas'),
             ('nombre', 'propiedad de reflexion, clave JSON, campo de struct, global o funcion Lua'),
             ('dueno', 'clase / struct / objeto / `global (seccion)`; para constantes `constant::<Clase>`'),
             ('direccion_o_offset', 'offset dentro del objeto (`0x70`, `+0x13004 bit0`, `vp+0x4e8`, `B+0x84`) o VA absoluta `0x14xxxxxxx`'),
             ('tipo', 'tipo Fox / C (con contenedor, tamano de arreglo, enum o clase apuntada)'),
             ('default', 'valor de fabrica (ctor del motor, JSON/.bin de constantes, o volcado/archivo segun la fuente)'),
             ('unidad_rango', 'unidad, rango, valores de enum o, en constantes, los valores de las variantes `.o`'),
             ('significado', 'que hace; incluye `[archivo.md] ...` cuando otro informe agrego datos y `[VERIFICACION: ...]` cuando una revision lo corrigio'),
             ('quien_lo_escribe', 'ctor@dir, registro de reflexion, setter, archivo de datos o funcion que lo escribe'),
             ('quien_lo_lee', 'consumidor / lector conocido; `?` = no identificado'),
             ('acceso_lua', '`si: ...` con la forma de acceso, `no ...`, o `no consta`'),
             ('fuente', 'archivo(s) de informe de donde sale la fila (con seccion para los .md)'),
             ('confianza', 'alta / media / baja segun el informe; `corregida en Verificacion` si una seccion de verificacion la toca')]:
    L.append(f'| `{c}` | {d} |')
L.append('')
L.append('## 2. Conteo por categoria\n')
L.append('| Categoria | Total | Reflexion Fox (reflexion/defaults/schema/lua props) | Constantes JSON/.bin | Funciones Lua | Campos de tablas Lua | Informes .md (memoria, globales, .fox2) | Con acceso Lua | Corregidas en verificacion |')
L.append('|---|---|---|---|---|---|---|---|---|')
tot = collections.Counter()
for c in CATS:
    o = collections.Counter(origen(x) for x in by[c])
    lua = sum(1 for x in by[c] if x['acceso_lua'].startswith('si'))
    cor = sum(1 for x in by[c] if 'corregida' in x['confianza'])
    L.append(f"| {c} | {len(by[c])} | {o['reflexion']} | {o['constant']} | {o['lua_func']} | {o['lua_campo']} | {o['md']} | {lua} | {cor} |")
    tot.update(o); tot['lua'] += lua; tot['cor'] += cor
L.append(f"| **total** | **{len(rows)}** | {tot['reflexion']} | {tot['constant']} | {tot['lua_func']} | {tot['lua_campo']} | {tot['md']} | {tot['lua']} | {tot['cor']} |\n")
L.append('Las constantes de gameplay son tantas porque cada elemento de arreglo es un parametro propio (por ejemplo '
         '`constant::Ballplayer.TurnData[924].kind1`). La version resumida por patron sigue en `constant_gameplay_resumen.tsv`.\n')
L.append('### Filas por informe de origen (primera fuente)\n')
L.append('| Fuente | Filas |\n|---|---|')
for k, v in st['src'].items(): L.append(f'| {k} | {v} |')
L.append('')
L.append('## 3. Como se armo (y que se hizo con lo refutado)\n')
L.append('- **reflexion.tsv** (1766 propiedades de Fox Engine): una fila por (clase, propiedad). Se le sumo `defaults.tsv` (default, unidad, rango, valor en Default.fox2, ctor), '
         'el significado de las tablas de `reflexion.md` y el modo Lua (`rw`/`r`). La verificacion de reflexion.md no encontro errores de clase/nombre/tipo/offset.')
L.append('- **defaults.tsv**: las 11 filas marcadas `[corregido en verificacion]` entran con el valor corregido (GeoTrapScriptModuleCondition, memo de FixDemo*, RouteEvent, '
         'TppAtmosphere.disableSkyCapture = true, expandHorizontalLineColor = false); `fogEnable` sigue sin resolver.')
L.append('- **lua_api.tsv** v2 (con las 42 direcciones de metodos por thunk ya corregidas): las propiedades que no estaban en reflexion entran como filas nuevas '
         '(la clase `?` se resolvio por la funcion de registro); cada campo leido de tabla Lua es una fila; las 851 funciones Lua entran como filas de tipo '
         '`funcion Lua (cfunction)` porque son la via de acceso Lua a los parametros (no son memoria).')
L.append('- **fox_core_schema.tsv**: solo las propiedades que no estaban en reflexion (77); el resto se marca como fuente adicional.')
L.append('- **constant_gameplay.tsv** (25013 valores) + significados escritos a mano de `constant_gameplay.md` + valores de `constant_gameplay_variantes.tsv` en `unidad_rango`. '
         'La unica correccion de su verificacion (FUN_141e66020 no usa el ctx) no cambia ningun parametro.')
L.append('- **Informes .md**: se leyeron todas las tablas de parametros (offset / direccion / global / slot / registro / clave) fuera de las secciones de verificacion, ganchos, '
         'inventarios de clases y preguntas abiertas. Las tablas compuestas ("Propiedades (offset)", "Claves (offset)", "Campos (claves)") se partieron en una fila por campo. '
         'Si el campo ya existia en reflexion (misma clase y nombre u offset) se fusiono en esa fila (significado/escritor/lector agregados con `[archivo.md]`). '
         'Las filas de funciones (`tipo` = func) se descartaron por no ser parametros.')
L.append('- **Verificaciones**: los informes ya reescribieron el cuerpo con las correcciones ("Corregido arriba"), asi que lo que entra es la version corregida. '
         'Ademas, cada fila cuya direccion/nombre aparece en un punto refutado o corregido de la seccion `## Verificacion` de su informe lleva el texto de ese punto en '
         '`significado` (`[VERIFICACION: ...]`) y `confianza = corregida en Verificacion (...)`. Lo refutado sin correccion (p. ej. "g_SharpenStrength = fuerza del sharpen", '
         '"g_EnvSpecular_enable", gancho TimeOfDaySettingsBody vf4, lectores de g_PhMaxRigidBodyNum, "CalcAvgLumInLogSpace leido por 0x141689220") queda solo con el significado corregido '
         'que trae el cuerpo del informe.')
L.append('- Se deduplicaron filas de distintos informes con la misma VA absoluta y el mismo nombre.\n')
L.append('## 4. Huecos\n')
L.append('### 4.1 Campos vacios por categoria (`?` o vacio)\n')
L.append('| Categoria | Sin direccion/offset | Sin default | Sin significado | Sin lector identificado | Sin escritor identificado |')
L.append('|---|---|---|---|---|---|')
for c in CATS:
    b = by[c]
    L.append(f"| {c} | {sum(empty(x['direccion_o_offset']) for x in b)} | {sum(empty(x['default']) for x in b)} | {sum(not x['significado'] for x in b)} | "
             f"{sum(empty(x['quien_lo_lee']) for x in b)} | {sum(empty(x['quien_lo_escribe']) for x in b)} |")
L.append(f"| **total** | {sum(empty(x['direccion_o_offset']) for x in rows)} | {sum(empty(x['default']) for x in rows)} | {sum(not x['significado'] for x in rows)} | "
         f"{sum(empty(x['quien_lo_lee']) for x in rows)} | {sum(empty(x['quien_lo_escribe']) for x in rows)} |\n")
gp = sum(1 for x in rows if x['fuente'].startswith('constant') and 'inferido por nombre' in x['confianza'])
L.append('Notas sobre los huecos:')
L.append('- **Lectores**: para las propiedades de reflexion el lector casi nunca esta identificado (el motor las lee por offset desde el codigo del Body/plugin, sin referencia rip-relativa). '
         'La columna trae la evidencia del default (lineas del C del ctor) cuando la hay; el consumidor real es un hueco para casi todas.')
L.append(f'- **Significado de constantes**: {gp} valores de constantes tienen el significado solo inferido por el nombre de la clave (confianza "significado inferido por nombre").')
L.append('- **Sin direccion**: sobre todo los valores de instancias en archivos .fox2 (focos de Default/EditFace/EditSpike/MatchMenu.fox2, `fox2_luces_y_assets.md`: no tienen offset '
         'porque son datos de archivo) y propiedades vistas solo en tablas de esos .fox2; tambien claves de `cobra::cfg` (`cobra_core.md`) que se conocen por nombre pero sin offset.')
nq = collections.Counter(x['fuente'].split(';')[0].split(' ')[0] for x in rows if x['dueno'] in ('', '?'))
L.append('- **Clases `?`**: ' + ', '.join(f'{v} filas de {k}' for k, v in nq.items()) + ' quedan con `dueno` = `?` (clase sin resolver).')
L.append(f"- **Categorias flacas**: `red_online` ({len(by['red_online'])}), `huesos_animacion` ({len(by['huesos_animacion'])}) y `texturas_materiales` ({len(by['texturas_materiales'])}) tienen pocos parametros de memoria documentados; el rig de huesos, "
         "materiales/texturas y la red se analizaron sobre todo a nivel de clases y ganchos, no campo por campo.")
L.append('- Tablas omitidas a proposito (no son parametros): inventarios de clases/vtables, listas de ganchos y funciones, ids de mensajes de `process_command.md`, '
         'codigos de tipo del formato .fox2, tablas de verificacion.\n')
L.append('### 4.2 Huecos declarados por cada informe (preguntas abiertas / limitaciones, texto literal)\n')
for fn in sorted(os.listdir(OUT)):
    if not fn.endswith('.md') or fn == 'parametros_todos.md': continue
    txt = open(f'{OUT}/{fn}', encoding='utf-8').read()
    secs = re.split(r'(?m)^(?=## )', txt)
    got = [s for s in secs if re.match(r'## .*(preguntas abiertas|queda abierto|limitaci|l[ií]mites|lo que falta|hashes no resueltos)', s.split('\n')[0], re.I)]
    if not got: continue
    L.append(f'#### {fn}\n')
    for s in got:
        body = s.strip().split('\n')
        L.append('**' + body[0].lstrip('# ').strip() + '**\n')
        L.extend(body[1:])
        L.append('')
open(f'{OUT}/parametros_todos.md', 'w', encoding='utf-8').write('\n'.join(L) + '\n')
print(len(L))
