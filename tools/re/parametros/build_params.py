#!/usr/bin/env python3
# Arma /home/claude/re/out/parametros_todos.tsv a partir de los informes y tsv de /home/claude/re/out/
import csv, re, os, sys, collections
OUT = '/home/claude/re/out'
csv.field_size_limit(10**9)

COLS = ['id','categoria','nombre','dueno','direccion_o_offset','tipo','default','unidad_rango','significado',
        'quien_lo_escribe','quien_lo_lee','acceso_lua','fuente','confianza']
CATS = ['luz','cielo_clima','sombras','postprocesado','camara','cesped_estadio','mallas_modelos','huesos_animacion',
        'fisica_ropa','texturas_materiales','gameplay','modos','ui_menus','archivos_io','red_online','sistema']

KW = [ # (regex, categoria) en orden de prioridad
  (r'sombra|shadow', 'sombras'),
  (r'c[aá]mara|camera|\bfov\b|\bcam\b|fixdemocamera|dmcam', 'camara'),
  (r'tonemap|bloom|exposi|ssao|\bdof\b|motion ?blur|\btaa\b|antialias|sharpen|grading|gamma|brillo|\bhdr\b|post|glow|halo|lens|vignet', 'postprocesado'),
  (r'cielo|\bsky|clima|weather|niebla|\bfog|lluvia|\brain|atmos|hora del d|time ?of ?day|\btod\b|nube|cloud', 'cielo_clima'),
  (r'\bluz|luces|light|\bsun|\bsol\b|lux|lumen|probe|enlighten|envmap|specular|reflej', 'luz'),
  (r'c[eé]sped|turf|pitch|estadio|stadium|p[uú]blico|audience|goalnet|red de arco', 'cesped_estadio'),
  (r'hueso|\bbone|anim|\brig\b|pose|\bik\b|facial', 'huesos_animacion'),
  (r'f[ií]sica|physic|\bph[A-Z ]|ropa|cloth|ragdoll|grav|rigid|wind|viento', 'fisica_ropa'),
  (r'textur|material|ftex|uniform|escudo', 'texturas_materiales'),
  (r'malla|mesh|modelo|model|\blod', 'mallas_modelos'),
  (r'online|\bntl\b|\bkps\b|network|login|steam|\bred\b|livedata|multiplay|http', 'red_online'),
  (r'guardad|savedata|save|settings\.dat|archivo|\bfile|\bfio\b|cpk|paquete|ruta|path|\bio\b|tmpdb', 'archivos_io'),
  (r'men[uú]|\bhud\b|opci[oó]n|pantalla|match2d|radar|vibraci|\bui\b|fade|fundido', 'ui_menus'),
  (r'\bmodo|\bmode\b|scenario|tutorial|exhibition|process|flow|flujo|comando|command', 'modos'),
  (r'partido|match|pelota|\bball|bal[oó]n|jugador|player|gameplay|[aá]rbitro|referee|constant', 'gameplay'),
]
def kwcat(text):
    for rx, c in KW:
        if re.search(rx, text, re.I): return c
    return None

def clean(s):
    if s is None: return ''
    s = str(s).replace('\t',' ').replace('\r',' ').replace('\n',' ').strip()
    return s

def tsv(path):
    with open(path, newline='', encoding='utf-8', errors='replace') as f:
        r = csv.reader(f, delimiter='\t', quoting=csv.QUOTE_NONE)
        hdr = next(r)
        for row in r:
            row += [''] * (len(hdr) - len(row))
            yield dict(zip(hdr, row))

def hx(s):
    s = clean(s)
    if re.fullmatch(r'[0-9a-fA-F]{9}', s) and s.startswith('14'): return '0x' + s.lower()
    return s

rows = []                 # lista de dicts
index = {}                # (dueno_lower, nombre_lower) -> row
index_off = {}            # (dueno_lower, offset) -> row

def norm_off(o):
    o = clean(o).replace('`','').replace('*','').strip()
    m = re.match(r'^\+?\s*(0x[0-9a-fA-F]+)$', o)
    if m: return m.group(1).lower()
    return o.lower()

def add(r, keyname=True):
    for c in COLS: r.setdefault(c, '')
    for c in COLS: r[c] = clean(r[c])
    rows.append(r)
    if keyname and r['dueno'] and r['nombre']:
        index.setdefault((r['dueno'].lower(), r['nombre'].lower()), r)
    o = norm_off(r['direccion_o_offset'])
    if r['dueno'] and re.fullmatch(r'0x[0-9a-f]+', o):
        index_off.setdefault((r['dueno'].lower(), o), r)
    return r

def append(r, col, txt, sep=' | '):
    txt = clean(txt)
    if not txt: return
    if not r[col]: r[col] = txt
    elif txt not in r[col]: r[col] += sep + txt

# ---------------------------------------------------------------- categoria de clases (reflexion.md)
class_cat = {}
cur = None
for line in open(f'{OUT}/reflexion.md', encoding='utf-8'):
    m = re.match(r'^## Categoria `(\w+)`', line)
    if m: cur = m.group(1); continue
    if line.startswith('## Verific'): cur = None
    m = re.match(r'^### (\S+)', line)
    if m and cur: class_cat[m.group(1).strip('`')] = 'sistema' if cur == 'otro' else cur

# significados de reflexion.md por (clase, propiedad)
refl_sig = {}
cls = None
for line in open(f'{OUT}/reflexion.md', encoding='utf-8'):
    if line.startswith('## Verific'): break
    m = re.match(r'^### (\S+)', line)
    if m: cls = m.group(1).strip('`'); continue
    if cls and line.startswith('| +0x'):
        p = [x.strip() for x in line.strip().strip('|').split('|')]
        if len(p) >= 6:
            refl_sig[(cls, p[1].strip('`'))] = p[5]

# ---------------------------------------------------------------- defaults.tsv
defaults = {}
for d in tsv(f'{OUT}/defaults.tsv'):
    defaults[(d['clase'], d['propiedad'])] = d

luacat_map = {'graficos':'postprocesado','camara':'camara','luz/clima':'cielo_clima','partido/gameplay':'gameplay',
              'sistema':'sistema','otros':'sistema'}

# ---------------------------------------------------------------- Lua: propiedades
regfn_class = {}
refl_rows = list(tsv(f'{OUT}/reflexion.tsv'))
for p in refl_rows:
    regfn_class.setdefault(p['regfn'].lower(), p['class'])
lua_props = {}
for l in tsv(f'{OUT}/lua_api.tsv'):
    if l['tipo'] != 'propiedad': continue
    mod = l['modulo']
    m = re.search(r'registrada en 0x([0-9a-f]+)', l['notas'])
    if mod == '?' and m: mod = regfn_class.get(m.group(1), '?')
    lua_props.setdefault((mod, l['funcion']), []).append(l)

def lua_access(mode, luaname):
    mode = (mode or '').lower()
    if not mode: return 'no consta'
    acc = {'rw':'lectura/escritura','r':'solo lectura'}.get(mode, mode)
    return f'si: propiedad de entidad Fox `entidad.{luaname}` ({acc}; modo de registro {mode})'

# ---------------------------------------------------------------- 1. reflexion.tsv
for p in refl_rows:
    if p['kind'] == 'EMBED':  # contenedor de un struct expandido: se deja igual como fila
        pass
    cls = p['class']; prop = p['property']
    d = defaults.get((cls, prop))
    cat = (d['categoria'] if d and d['categoria'] else None) or class_cat.get(cls) or 'sistema'
    if cat == 'otro': cat = 'sistema'
    tipo = p['type']
    if p['container'] and p['container'] != 'StaticArray': tipo += f" [{p['container']}]"
    if p['arraySize'] not in ('', '1', '0'): tipo += f" x{p['arraySize']}"
    if p['enum']: tipo += f" enum {p['enum']}"
    if p['ptr_class']: tipo += f" -> {p['ptr_class']}"
    unid = ''
    if d: unid = ' ; '.join(x for x in [d['unidad'], ('rango ' + d['rango']) if d['rango'] else ''] if x)
    if p['enum_values'] and p['enum_values'] not in unid: unid = (unid + ' ; valores ' + p['enum_values']).strip(' ;')
    sig = refl_sig.get((cls, prop), '')
    if d and d['valor_en_Default.fox2']: sig = (sig + f" [en Default.fox2: {d['valor_en_Default.fox2']}]").strip()
    dueno_txt = cls + (f" (super {p['super']}, tamano {p['class_size']}, modulo {p['category']})" if p['super'] or p['category'] else '')
    writer = f"registro de reflexion {hx(p['regfn'])} (callsite {hx(p['callsite'])}); cargador .fox2 / Lua por reflexion"
    if d and d['evidencia']:
        m = re.search(r'ctor (0x[0-9a-f]+)', d['evidencia'])
        if m: writer = f"ctor@{m.group(1)} (default) ; " + writer
    lua = lua_access(p['mode'], p['lua_name'] or prop)
    if (cls, prop) in lua_props: lua += ' ; confirmado en lua_api.tsv'
    conf = 'alta (clase/nombre/tipo/offset verificados en reflexion.md)'
    if d:
        conf += '; default alta' if 'corregido' not in d['evidencia'] else '; default corregido en verificacion de defaults.md'
    r = add({'categoria':cat,'nombre':prop,'dueno':cls,'direccion_o_offset':p['offset'] if p['offset'] else '',
             'tipo':tipo,'default':d['default'] if d else '','unidad_rango':unid,'significado':sig,
             'quien_lo_escribe':writer,'quien_lo_lee':('evidencia default: ' + d['evidencia']) if d and d['evidencia'] else '',
             'acceso_lua':lua,'fuente':'reflexion.tsv; reflexion.md' + ('; defaults.tsv' if d else ''),'confianza':conf})
    r['_dueno_clase'] = cls
    if p['virtual'] not in ('', '0'): append(r, 'significado', '(propiedad virtual: se accede por getter/setter, no por offset directo)')
    if p['kind'] in ('PROP_EMB', 'EMBED'): append(r, 'significado', f"(fila {p['kind']}: campo de struct embebido)")

# ---------------------------------------------------------------- 2. defaults.tsv sin fila en reflexion
for (cls, prop), d in defaults.items():
    if (cls.lower(), prop.lower()) in index: continue
    add({'categoria':d['categoria'] or class_cat.get(cls,'sistema'),'nombre':prop,'dueno':cls,'direccion_o_offset':d['offset'],
         'tipo':d['tipo'],'default':d['default'],'unidad_rango':' ; '.join(x for x in [d['unidad'], d['rango']] if x),
         'significado':(f"[en Default.fox2: {d['valor_en_Default.fox2']}]" if d['valor_en_Default.fox2'] else ''),
         'quien_lo_escribe':d['evidencia'],'acceso_lua':'no consta','fuente':'defaults.tsv','confianza':'alta (ctor leido en el C)'})

# ---------------------------------------------------------------- 3. lua_api.tsv propiedades no presentes en reflexion
for (mod, prop), ls in lua_props.items():
    if (mod.lower(), prop.lower()) in index:
        continue
    l = ls[0]
    m = re.search(r'offset=(0x[0-9a-f]+)', l['firma_inferida'])
    t = re.search(r'tipo=(\S+)', l['firma_inferida'])
    a = re.search(r'acceso=(\S+)', l['firma_inferida'])
    add({'categoria':luacat_map.get(l['categoria'],'sistema') if mod not in class_cat else class_cat[mod],
         'nombre':prop,'dueno':mod,'direccion_o_offset':m.group(1) if m else '','tipo':t.group(1) if t else '',
         'significado':'propiedad de reflexion vista solo en el analisis Lua',
         'quien_lo_escribe':l['notas'],'acceso_lua':lua_access(a.group(1) if a else '', prop),
         'fuente':'lua_api.tsv','confianza':'media (registro en el C; clase ' + ('resuelta por regfn' if l['modulo']=='?' else 'del registro') + ')'})

# ---------------------------------------------------------------- 4. fox_core_schema.tsv
for s in tsv(f'{OUT}/fox_core_schema.tsv'):
    if s['kind'] != 'PROP': continue
    cls, name = s['class'], s['name']
    if name in ('None', '') : continue
    if (cls.lower(), name.lower()) in index:
        r = index[(cls.lower(), name.lower())]; append(r, 'fuente', 'fox_core_schema.tsv', '; ')
        continue
    off = s['offset'] if re.fullmatch(r'0x[0-9a-fA-F]+', s['offset']) else (('expr: ' + s['offset']) if s['offset'] else '')
    add({'categoria':class_cat.get(cls,'sistema'),'nombre':name,'dueno':cls,'direccion_o_offset':off,
         'tipo':s['type'] + (f" [{s['container']}]" if s['container'] else ''),
         'significado':s['extra'],'quien_lo_escribe':f"registro de reflexion {hx(s['regfn'])}",
         'acceso_lua':'probable (propiedad registrada por reflexion Fox; acceso Lua no confirmado)',
         'fuente':'fox_core_schema.tsv','confianza':'media (sacado del C de Ghidra por patron; clase ' + ('sin resolver' if cls=='?' else 'resuelta') + ')'})

# ---------------------------------------------------------------- 5. lua_api.tsv lecturas de tabla (cada campo es un parametro)
for l in tsv(f'{OUT}/lua_api.tsv'):
    if l['tipo'] != 'lectura_tabla': continue
    m = re.search(r'campos:\s*(.*)', l['notas'])
    if not m: continue
    for campo in [c.strip() for c in m.group(1).split(',') if c.strip()]:
        add({'categoria':kwcat(l['modulo'] + ' ' + campo) or luacat_map.get(l['categoria'],'sistema'),'nombre':campo,'dueno':l['modulo'],
             'direccion_o_offset':'campo de tabla Lua','tipo':'valor Lua','significado':f"campo que lee {l['modulo']} de una tabla Lua ({l['firma_inferida']})",
             'quien_lo_escribe':'script Lua (tabla pasada como argumento)','quien_lo_lee':f"{l['modulo']}@{l['direccion_c']}",
             'acceso_lua':f"si: campo `{campo}` de la tabla que recibe {l['modulo']}",'fuente':'lua_api.tsv','confianza':'alta (getfield en el C)'})

# ---------------------------------------------------------------- 6. lua_api.tsv funciones (puntos de acceso Lua a parametros)
for l in tsv(f'{OUT}/lua_api.tsv'):
    if l['tipo'] != 'funcion': continue
    add({'categoria':kwcat(l['modulo'] + ' ' + l['funcion']) or luacat_map.get(l['categoria'],'sistema'),'nombre':f"{l['modulo']}.{l['funcion']}",'dueno':l['modulo'],
         'direccion_o_offset':l['direccion_c'],'tipo':'funcion Lua (cfunction)','significado':(l['firma_inferida'] + ' ' + l['notas']).strip(),
         'quien_lo_escribe':'script Lua (llamada)','quien_lo_lee':f"cfunction@{l['direccion_c']}",
         'acceso_lua':f"si: `{l['modulo']}.{l['funcion']}(...)`",'fuente':'lua_api.tsv; lua_api.md (v2)',
         'confianza':'alta' if 'thunk' not in l['notas'] else 'media (metodo por thunk; direccion corregida en v2)'}, keyname=False)

# ---------------------------------------------------------------- 7. constant_gameplay (JSON/bin del partido)
cg_theme = {}; cg_sig = {}
theme = None; ccls = None
for line in open(f'{OUT}/constant_gameplay.md', encoding='utf-8'):
    if line.startswith('## Verific') or line.startswith('## 4.'): break
    m = re.match(r'^### (.+)', line)
    if m: theme = m.group(1).strip(); continue
    m = re.match(r'^#### `constant::(\w+)`', line)
    if m: ccls = m.group(1); cg_theme[ccls] = theme; continue
    if ccls and line.startswith('| `'):
        p = [x.strip() for x in line.strip().strip('|').split('|')]
        if len(p) >= 5:
            key = re.sub(r'\s+x\d+$', '', p[0].strip('`').split('`')[0])
            cg_sig[(ccls, key)] = p[4]
def cg_cat(cls):
    t = (cg_theme.get(cls) or '').lower()
    if 'camara' in t: return 'camara'
    if 'pruebas, tutorial' in t: return 'modos'
    return 'gameplay'
variants = collections.defaultdict(list)
for v in tsv(f'{OUT}/constant_gameplay_variantes.tsv'):
    variants[(v['clase'], v['clave'])].append(f"{v['variante(.o)']}={v['valor_variante']}")
for c in tsv(f'{OUT}/constant_gameplay.tsv'):
    cls, key = c['clase'], c['clave']
    pat = re.sub(r'\[\d+\]', '[]', key)
    sig = cg_sig.get((cls, key)) or cg_sig.get((cls, pat)) or c['significado_inferido']
    if c['significado_inferido'] and c['significado_inferido'] != sig: sig = sig + ' (nombre: ' + c['significado_inferido'] + ')'
    if c['nota']: sig += ' [' + c['nota'] + ']'
    var = variants.get((cls, key))
    unid = ''
    if var: unid = f"variantes ({len(var)}): " + ', '.join(var)
    reader = f"lector JSON vf1@{c['lector_json(vf1)']} ; lector bin vf2@{c['lector_bin(vf2)']}"
    if c['usado_por(heuristico)']: reader += ' ; usado por (heuristico): ' + c['usado_por(heuristico)']
    conf = 'alta (offset y valor ' + c['valor_verificado'] + ')'
    if sig.startswith('por nombre'): conf += '; significado inferido por nombre'
    add({'categoria':cg_cat(cls),'nombre':key,'dueno':'constant::' + cls,
         'direccion_o_offset':f"obj+{c['offset']} (id_constante {c['id_constante']}; registro DAT_143802c88 via FUN_141e65580)",
         'tipo':c['tipo'],'default':c['default'],'unidad_rango':unid,'significado':sig,
         'quien_lo_escribe':f"{c['archivo_json']} / {c['archivo_bin']}:{c['entrada_o']}+{c['offset_en_o']} (bin descomprimido +{c['offset_en_bin_descomprimido']}) cargado por vf1/vf2",
         'quien_lo_lee':reader,'acceso_lua':'no (se carga de JSON/.bin de constantes)','fuente':'constant_gameplay.tsv; constant_gameplay.md',
         'confianza':conf})
cg_classes = set('constant::' + c.lower() for c in cg_theme) | set(r['dueno'].lower() for r in rows if r['dueno'].startswith('constant::'))

# ---------------------------------------------------------------- 8. tablas de los informes .md
MD_DEFAULT_CAT = {'camaras':'camara','cobra_core':'sistema','cobra_rd':'sistema','draw':'sistema','fox2_formato':'archivos_io',
  'fox2_luces_y_assets':'luz','fox_core':'sistema','fox_fisica_anim':'fisica_ropa','fox_fx':'cielo_clima','fox_gr':'postprocesado',
  'fox_gr_dg':'postprocesado','match_anim_cam':'camara','match_resto':'gameplay','menu':'ui_menus','mode':'modos',
  'online_save':'red_online','process_command':'modos','resto':'sistema','settings_opciones':'ui_menus','tonemap_sombras_luz':'postprocesado',
  'lua_api':'sistema'}

SKIP_FIRST = re.compile(r'^(clase|clases|familia|gancho|funci[oó]n|funcion|funci[oó]n c|#|paso|tarea|grupo|cadena|tabla|condici[oó]n|'
                        r'ruta|ruta\(s\) de flujo|pantalla|categor[ií]a|par|foco|secci[oó]n \(lector\)|afirmaci[oó]n|familia .*|clase \(parser vf1\)|'
                        r'id|prefijo|clase\.propiedad|propiedad esperado)$', re.I)
LOC_HDR = re.compile(r'offset|^off$|^off\.|direcci|^global$|d[oó]nde|\+$|^save$|slot|byte del|campo de|^bit$|^registro$|^lua$|^clave|^campo$|'
                     r'^propiedad$|^par[aá]metro$|^elemento$|^valor$|^nombre$|^qu[eé]$|^[ií]ndice$|^c[oó]digo$|^objeto$|^dato$|^b\+$|^fila$|^vf$', re.I)

def colrole(h):
    h0 = h.lower().strip()
    if re.search(r'^conf|^confianza|^ok$', h0): return 'confianza'
    if re.search(r'escrito por|^origen|^setter|lo pone|^escribe|pantalla que escribe|^getter / setter|^setter gr|^getter$|^desde$|^ctor$', h0): return 'quien_lo_escribe'
    if re.search(r'consumidor|lectores|lo consume|lo usa|^lee|^lector|^destino|plugin que lo lee|qui[eé]n lo llama|en shader|llamadores|^uso$', h0): return 'quien_lo_lee'
    if re.search(r'^default|^valor|por defecto|^pes \(\.fox2\)|valor en', h0): return 'default'
    if re.search(r'^tipo y rango|^unidad|^rango|^valores|^elecciones|^tama', h0): return 'unidad_rango'
    if re.search(r'^tipo', h0): return 'tipo'
    if re.search(r'direccion del cono', h0): return None
    if re.search(r'^offset|^off$|^off\.|^direcci|^d[oó]nde|\+$|^save$|^slot|^byte del|^bit$|^m[aá]scara$|^registro$|^global$', h0): return 'direccion_o_offset'
    if re.search(r'^significado|^rol$|^detalle|^contenido|qu[eé] hace|^nombre / significado|^efecto|^notas|^para qu|^que es|^uso|^dato$|significado', h0): return 'significado'
    if re.search(r'^nombre$|^propiedad$|^campo$|^clave|^par[aá]metro$|^elemento$|^qu[eé]$|^lua$|^[ií]ndice$|^c[oó]digo$|^fila$|^vf$|^campo de', h0): return 'nombre'
    if re.search(r'^objeto$|^clase$|^due[nñ]o$|^entidad$|^entidad\+$|^visualsetting$', h0): return 'dueno'
    if re.search(r'^evidencia', h0): return 'evidencia'
    return None

IDENT = re.compile(r'`?((?:[A-Za-z_]\w*::)+\w+|[A-Z][a-z0-9]+(?:[A-Z][A-Za-z0-9]*)+|[a-z]+[A-Z]\w+)`?')
def owner_of(h3, h2):
    for h in (h3, h2):
        if not h: continue
        h = re.sub(r'^[\d.]+\s*', '', h)
        m = IDENT.search(re.sub(r'`?(FUN|DAT|LAB|c)_[0-9a-fA-F_]+[.\w]*`?', '', h))
        if m: return re.sub(r'::vf\d+$', '', m.group(1))
        for b in re.findall(r'`([^`]+)`', h):
            if re.match(r'^[A-Za-z_][\w:]*$', b) and not re.match(r'^(FUN|DAT|LAB|c)_', b): return b
        m = re.match(r'^([A-Z]\w+):', h)
        if m: return m.group(1)
    return ''

def strip_md(s):
    s = s.replace('**', '').strip()
    return s

# afirmaciones refutadas/corregidas por archivo -> direcciones que mencionan
refuted = collections.defaultdict(list)  # file -> [(set(addrs), text)]
md_files = sorted(f for f in os.listdir(OUT) if f.endswith('.md') and f != 'parametros_todos.md')
for fn in md_files:
    txt = open(f'{OUT}/{fn}', encoding='utf-8').read()
    m = re.search(r'^## Verific', txt, re.M)
    if not m: continue
    ver = txt[m.start():]
    blocks = re.split(r'\n(?=\s*\d+\. \*\*|- \*\*|\| \d+ \|)', ver)
    for b in blocks:
        b = re.split(r'\n#', b)[0]
        if not re.match(r'\s*(\d+\. \*\*|- \*\*|\| \d+ \|)', b): continue
        if re.search(r'refutad|corregid|falso|no se sostiene|incorrect|impreciso|incompleto|correcci[oó]n', b, re.I) and not re.search(r'^\| \d+ \|[^|]*\|\s*\*?\*?Confirmada\*?\*?\s*\|', b):
            addrs = set(a.lower() for a in re.findall(r'0x1[0-9a-fA-F]{8}', b))
            offs = set(a.lower() for a in re.findall(r'\+0x[0-9a-fA-F]+', b))
            if addrs or offs:
                refuted[fn[:-3]].append((addrs, offs, re.sub(r'\s+', ' ', b.strip())[:400]))

md_stats = collections.Counter()
skipped_tables = collections.Counter()
for fn in md_files:
    base = fn[:-3]
    if base in ('reflexion', 'defaults', 'constant_gameplay', 'lua_api'):
        continue  # ya cubiertos por sus tsv (los significados se unieron arriba)
    lines = open(f'{OUT}/{fn}', encoding='utf-8').read().split('\n')
    h2 = h3 = ''
    i = 0
    while i < len(lines):
        L = lines[i]
        if L.startswith('## '):
            h2 = L[3:].strip(); h3 = ''
            if re.match(r'(verific|correcciones aplicadas)', h2, re.I): break
        elif L.startswith('### ') or L.startswith('#### '):
            h3 = L.lstrip('#').strip()
        if L.startswith('|') and i + 1 < len(lines) and re.match(r'^\|[\s:|-]*-[\s:|-]*$', lines[i+1]):
            hdr = [strip_md(x) for x in L.strip().strip('|').split('|')]
            j = i + 2; body = []
            while j < len(lines) and lines[j].startswith('|'):
                body.append(lines[j]); j += 1
            sect = (h2 + ' ' + h3)
            h0l = hdr[0].strip().lower()
            strong = any(re.search(r'offset|direcci|d[oó]nde|^off\.', h, re.I) for h in hdr)
            allow = h0l in ('menu (pos)', 'off. en entrada', 'hilo', 'aspecto') or (base == 'fox2_luces_y_assets' and h0l in ('#', 'foco'))
            compound = [k for k, h in enumerate(hdr) if re.search(r'propiedades \(offset|offsets / defaults|claves \(offset\)|campos \(claves\)|notas / campos|^propiedades$', h, re.I)]
            if compound and not re.search(r'refutad|ganchos|preguntas abiertas', sect, re.I):
                k = compound[0]
                for bl in body:
                    cells = [x.strip() for x in re.split(r'(?<!\\)\|', bl.strip().strip('|'))]
                    if len(cells) <= k: continue
                    oi = next((q for q, h in enumerate(hdr) if re.search(r'^(clase|setting|restricci)', h, re.I)), 0)
                    own = re.sub(r'\*|`', '', cells[oi] if oi < len(cells) else cells[0]).strip()
                    ownname = re.split(r'[ (]', own)[0] if own else owner_of(h3, h2)
                    if ownname.startswith('constant::') and ('constant::' + ownname[10:]).lower() in cg_classes:
                        md_stats[(base, 'compuesta_ya_en_constant')] += 1; continue
                    pieces = re.split(r',\s*(?![^()\[\]{}]*[)\]}])', re.sub(r'\*\*', '', cells[k]))
                    others = ' ; '.join(f"{hdr[q]}: {cells[q]}" for q in range(1, len(cells)) if q != k and q < len(hdr) and cells[q])
                    for pc in pieces:
                        pc = pc.strip().strip('.').strip()
                        if not pc or len(pc) > 250: continue
                        mo = re.search(r'(\+0x[0-9a-fA-F]+(?:\s*(?:\.\.|/)\s*\+?0x[0-9a-fA-F]+)?)', pc)
                        nm = re.match(r'`?([A-Za-z_][\w\[\]/*{},.]*)`?', pc)
                        dv = re.search(r'=\s*(.+)$', pc)
                        tp = re.match(r'`?[A-Za-z_][\w\[\]/*]*`?\s+([A-Z]\w+)\s+\+0x', pc)
                        tgt = None
                        if nm:
                            tgt = index.get((ownname.lower(), nm.group(1).lower()))
                        rr = {'nombre': nm.group(1) if nm else pc[:60], 'dueno': ownname,
                              'direccion_o_offset': mo.group(1) if mo else '?', 'tipo': tp.group(1) if tp else '',
                              'default': dv.group(1).strip() if dv else '', 'significado': f"(de tabla compuesta: '{pc}') " + others,
                              'categoria': kwcat(h3 + ' ' + own + ' ' + pc) or kwcat(h2) or MD_DEFAULT_CAT.get(base, 'sistema'),
                              'fuente': f"{fn} § {h2}{(' / ' + h3) if h3 else ''}",
                              'confianza': 'segun informe: ' + (cells[-1] if re.match(r'(alta|media|baja|A|M|B)', cells[-1]) else 'sin columna'),
                              'acceso_lua': 'no consta (memoria; acceso por DLL)'}
                        if tgt is not None:
                            append(tgt, 'significado', f"[{fn}] {pc}"); append(tgt, 'fuente', rr['fuente'], '; ')
                            md_stats[(base, 'compuesta_fusionada')] += 1; continue
                        add(rr); md_stats[(base, 'compuesta_nueva')] += 1
                i = j; continue
            ok = (allow or (not SKIP_FIRST.match(h0l) or (h0l == '#' and strong))) and any(LOC_HDR.search(h.strip()) for h in hdr + (['offset'] if allow else [])) \
                 and not any(re.search(r'gancho|para qu[eé]|uso propuesto|uso para la dll|^firma', h, re.I) for h in hdr) \
                 and not re.search(r'refutad|ganchos|preguntas abiertas|l[ií]mites|limitaciones|m[eé]todo|inventario|categorizaci[oó]n de clases|familias|clases por', sect, re.I)
            if not ok:
                skipped_tables[(base, ' | '.join(hdr))] += len(body)
                i = j; continue
            roles = [colrole(h) for h in hdr]
            # columna nombre por defecto: si no hay, la primera columna no-direccion
            if 'nombre' not in roles:
                for k, rr in enumerate(roles):
                    if rr is None: roles[k] = 'nombre'; break
            for bl in body:
                cells = [x.strip() for x in re.split(r'(?<!\\)\|', bl.strip().strip('|'))]
                if len(cells) < 2: continue
                r = {c: '' for c in COLS}
                extra = []
                for k, cval in enumerate(cells):
                    v = cval.replace('\\|', '|')
                    rr = roles[k] if k < len(roles) else None
                    hname = hdr[k] if k < len(hdr) else f'col{k}'
                    if rr in ('evidencia', None) or (rr and r.get(rr)):
                        if v and v not in ('—', '-'): extra.append(f"{hname}: {v}")
                        continue
                    if rr == 'direccion_o_offset' and hname.strip() not in ('Offset', 'Off', 'Dirección', 'Direccion', 'Global', 'Donde', 'Dónde') \
                       and not re.search(r'offset|direcci|global|d[oó]nde', hname, re.I):
                        hn = hname.strip()
                        v = v.replace('`', '')
                        if hn.endswith('+') and v.startswith('+'): v = hn[:-1] + v
                        elif not v.startswith(hn): v = f"{hn} {v}"
                    r[rr] = v
                if extra: r['significado'] = (r['significado'] + ' ; ' if r['significado'] else '') + ' ; '.join(extra)
                if not r['nombre']:
                    r['nombre'] = re.split(r' \(| ;|\. ', r['significado'])[0][:80] if r['significado'] else r['direccion_o_offset']
                r['nombre'] = r['nombre'].replace('**', '').replace('`', '').strip()
                r['direccion_o_offset'] = r['direccion_o_offset'].replace('`', '').replace('**', '').strip()
                if re.match(r'^func', r['tipo'], re.I):
                    md_stats[(base, 'descartada_funcion')] += 1; continue
                own = (r['dueno'].replace('`', '').replace('**', '').strip()) or owner_of(h3, h2)
                if not own:
                    sec = re.sub(r'^[\d.]+\s*', '', h3 or h2)
                    own = ('global (' + sec + ')') if re.match(r'^0x1[0-9a-f]{8}', r['direccion_o_offset']) else ('(' + sec + ')')
                r['dueno'] = own
                cat = kwcat(h3 + ' ' + r['nombre']) or kwcat(h2) or MD_DEFAULT_CAT.get(base, 'sistema')
                # ajustes por archivo
                if base in ('menu', 'settings_opciones') and cat not in ('postprocesado', 'camara', 'sombras', 'archivos_io', 'gameplay', 'red_online'):
                    cat = 'ui_menus'
                r['categoria'] = cat
                conf = r['confianza'] or ''
                conf = re.sub(r'\bA\b', 'alta', re.sub(r'\bM\b', 'media', re.sub(r'\bB\b', 'baja', conf)))
                r['confianza'] = conf or 'segun informe (sin columna de confianza)'
                r['fuente'] = f"{fn} § {h2}{(' / ' + h3) if h3 else ''}"
                # acceso lua
                if re.search(r'\blua\b|GrTools|\.Set[A-Z]|pushcclosure', bl, re.I):
                    r['acceso_lua'] = 'si (el informe lo menciona: ' + (re.search(r'([A-Za-z_]+\.[A-Za-z_]+)', bl).group(1) if re.search(r'([A-Z][A-Za-z_]+\.[A-Z][A-Za-z_]+)', bl) else 'Lua') + ')'
                else:
                    r['acceso_lua'] = 'no consta (memoria; acceso por DLL)'
                # verificacion
                addrs = set(a.lower() for a in re.findall(r'0x1[0-9a-fA-F]{8}', r['direccion_o_offset'] + ' ' + r['nombre'] + ' ' + r['default']))
                offs = set(a.lower() for a in re.findall(r'\+0x[0-9a-fA-F]+', r['direccion_o_offset']))
                for (ra, ro, t) in refuted.get(base, []):
                    hit = (addrs & ra) or (offs and offs & ro and (own.lower() in t.lower() or any(a in t.lower() for a in addrs)))
                    if hit:
                        r['significado'] += ' [VERIFICACION: ' + t + ']'
                        if not r['confianza'].startswith('corregid'):
                            r['confianza'] = 'corregida en Verificacion (' + r['confianza'] + ')'
                        break
                for c in COLS: r[c] = clean(r[c])
                # fusion con filas de reflexion/defaults por (clase, nombre) o (clase, offset)
                nm = r['nombre'].strip('`').strip()
                key = (own.lower(), nm.lower())
                o = norm_off(r['direccion_o_offset'])
                tgt = index.get(key) or (index_off.get((own.lower(), o)) if re.fullmatch(r'0x[0-9a-f]+', o) else None)
                if tgt is not None and tgt.get('_dueno_clase'):
                    for c in ('quien_lo_escribe', 'quien_lo_lee'):
                        append(tgt, c, r[c] and f"[{fn}] {r[c]}")
                    append(tgt, 'significado', f"[{fn}] " + ' '.join(x for x in [r['significado'], ('valor: ' + r['default']) if r['default'] else ''] if x))
                    append(tgt, 'fuente', r['fuente'], '; ')
                    md_stats[(base, 'fusionada')] += 1
                    continue
                # duplicado entre informes (mismo dueno + misma direccion absoluta)
                add(r); md_stats[(base, 'nueva')] += 1
            i = j; continue
        i += 1

# ---------------------------------------------------------------- dedup por direccion absoluta entre informes .md
final = []
seen_abs = {}
for r in rows:
    r.pop('_dueno_clase', None)
    a = norm_off(r['direccion_o_offset'])
    if r['fuente'].split(' ')[0].endswith('.md') and re.fullmatch(r'0x1[0-9a-f]{8}', a) and r['tipo'] != 'funcion Lua (cfunction)':
        k = (a, r['nombre'].lower()[:40])
        if k in seen_abs:
            t = seen_abs[k]
            for c in ('significado', 'quien_lo_escribe', 'quien_lo_lee', 'default', 'tipo', 'unidad_rango'):
                if r[c] and r[c] not in t[c]: append(t, c, f"[{r['fuente'].split(' ')[0]}] {r[c]}")
            append(t, 'fuente', r['fuente'], '; ')
            continue
        seen_abs[k] = r
    final.append(r)

for n, r in enumerate(final, 1):
    r['id'] = f'P{n:05d}'
    if r['categoria'] not in CATS: r['categoria'] = 'sistema'
    for c in COLS:
        if not r[c] and c in ('quien_lo_escribe', 'quien_lo_lee', 'default', 'tipo', 'direccion_o_offset'):
            r[c] = '?'
with open(f'{OUT}/parametros_todos.tsv', 'w', encoding='utf-8', newline='') as f:
    f.write('\t'.join(COLS) + '\n')
    for r in final:
        f.write('\t'.join(clean(r[c]) for c in COLS) + '\n')

import json
cnt = collections.Counter(r['categoria'] for r in final)
src = collections.Counter(r['fuente'].split(';')[0].split(' ')[0] for r in final)
json.dump({'total': len(final), 'cat': cnt, 'src': src, 'md': {f'{a}:{b}': v for (a, b), v in md_stats.items()},
           'skipped': {f'{a} :: {b}': v for (a, b), v in skipped_tables.items()},
           'sin_dir': sum(1 for r in final if r['direccion_o_offset'] in ('?', '')),
           'sin_default': sum(1 for r in final if r['default'] in ('?', '')),
           'sin_sig': sum(1 for r in final if not r['significado']),
           'sin_writer': sum(1 for r in final if r['quien_lo_escribe'] in ('?', '')),
           'sin_reader': sum(1 for r in final if r['quien_lo_lee'] in ('?', '')),
           'cat_sin': {c: dict(dir=sum(1 for r in final if r['categoria']==c and r['direccion_o_offset'] in ('?','')),
                               default=sum(1 for r in final if r['categoria']==c and r['default'] in ('?','')),
                               sig=sum(1 for r in final if r['categoria']==c and not r['significado']),
                               lector=sum(1 for r in final if r['categoria']==c and r['quien_lo_lee'] in ('?','')),
                               lua=sum(1 for r in final if r['categoria']==c and r['acceso_lua'].startswith('si'))) for c in CATS},
           'corregidas': sum(1 for r in final if 'corregid' in r['confianza']),
           }, open('/tmp/claude-0/-home-user-Pes-GraphicalUpgrade/9c04a2d7-26f7-5e59-8a9a-a2e213e44c11/scratchpad/stats.json', 'w'), indent=1, ensure_ascii=False)
print(len(final), cnt)
