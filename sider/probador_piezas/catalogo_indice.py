# catalogo_indice.py - PES21 FX (10/11/2026). Para ProbadorPiezas v3.0.
# Recorre la carpeta del catalogo, escribe los "<modelo>.fmdl.txt" que falten (materiales, con
# fmdl_info.py, que tiene que estar al lado) y arma indice.txt ordenado y con grupos:
#   [nombre del grupo]
#   ruta\relativa\modelo.fmdl
# Primero lo que es de personaje (prendas), despues publico, objetos y estadio.
# Solo lee los .fmdl; escribe indice.txt y los .txt de al lado. No toca nada del juego.
# Uso:  python catalogo_indice.py <carpeta_del_catalogo>
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fmdl_info

CATEGORIAS = [                      # (nombre, textos que tiene que tener la ruta en minusculas)
    ('personaje: partes sueltas', ('asset\\model\\character\\parts\\',)),
    ('personaje: paquete comun', ('common_package',)),
    ('personaje: uniforme', ('\\character\\uniform\\',)),
    ('personaje: trajes de tecnicos', ('\\character\\face\\real\\',)),
    ('personaje', ('\\character\\',)),
    ('publico y extras', ('\\mob\\',)),
    ('objetos de cinematicas', ('fixdemoobj',)),
    ('objetos de entrenamiento', ('training_prop',)),
    ('objetos', ('\\prop\\',)),
    ('pelotas', ('\\ball\\',)),
    ('estadio y fondos', ('\\bg\\',)),
]
OTROS = 'otros'
MAX_GRUPO = 80                      # un grupo mas grande se parte por la carpeta siguiente


def categoria(rel):
    r = '\\' + rel.lower()
    for i, (nombre, textos) in enumerate(CATEGORIAS):
        if all(t in r for t in textos):
            return i
    return len(CATEGORIAS)


def cuentas(path):
    """huesos y mallas del fmdl (bloques 0 y 3 de la primera seccion), o None si no se puede leer"""
    try:
        with open(path, 'rb') as f:
            d = f.read(0x40)
            if d[:4] != b'FMDL':
                return None
            info, = struct.unpack_from('<Q', d, 8)
            n0, = struct.unpack_from('<I', d, 0x20)
            f.seek(info)
            t = f.read(8 * n0)
        c = {}
        for i in range(n0):
            tipo, cnt, _off = struct.unpack_from('<HHI', t, 8 * i)
            c[tipo] = cnt
        return c.get(0, 0), c.get(3, 0)
    except Exception:
        return None


def partir(nombre, rels):
    """parte un grupo grande por la primera carpeta en la que sus rutas se diferencian"""
    if len(rels) <= MAX_GRUPO:
        return [(nombre, rels)]
    carpetas = [r.lower().split('\\')[:-1] for r in rels]
    d = 0
    while all(len(c) > d for c in carpetas) and len(set(c[d] for c in carpetas)) == 1:
        d += 1
    sub = {}
    for r, c in zip(rels, carpetas):
        sub.setdefault(c[d] if len(c) > d else '', []).append(r)
    if len(sub) < 2:
        return [(nombre, rels)]
    # demasiados grupos chicos no sirven para saltar: los junto de a tandas por orden alfabetico
    claves = sorted(sub)
    out, junta, desde = [], [], None
    for k in claves:
        if len(sub[k]) >= MAX_GRUPO // 2:
            if junta:
                out.append(('%s: %s ... %s' % (nombre, desde, hasta), junta)); junta = []
            out.append(('%s: %s' % (nombre, k or '(raiz)'), sub[k]))
            continue
        if not junta:
            desde = k or '(raiz)'
        junta = junta + sub[k]
        hasta = k or '(raiz)'
        if len(junta) >= MAX_GRUPO:
            out.append(('%s: %s ... %s' % (nombre, desde, hasta), junta)); junta = []
    if junta:
        out.append(('%s: %s ... %s' % (nombre, desde, hasta), junta))
    return out


def main(raiz):
    raiz = os.path.abspath(raiz)
    rels = []
    for carpeta, _, fs in os.walk(raiz):
        for f in fs:
            if f.lower().endswith('.fmdl'):
                rels.append(os.path.relpath(os.path.join(carpeta, f), raiz).replace('/', '\\'))
    rels.sort(key=lambda r: r.lower())
    nuevos = sin_huesos = errores = 0
    for r in rels:
        p = os.path.join(raiz, r.replace('\\', os.sep))
        if os.path.exists(p + '.txt'):
            continue
        c = cuentas(p)
        try:
            t = fmdl_info.texto(p)
        except Exception as e:
            t = '(no se pudo leer: %s)\r\n' % e
            errores += 1
        if c is None:
            cab = '(no pude contar huesos y mallas)'
        elif c[0] == 0:
            cab = 'SIN HUESOS (%d mallas): es un objeto, no una prenda; puede quedar quieto en el piso' % c[1]
            sin_huesos += 1
        else:
            cab = '%d huesos, %d mallas' % c
        open(p + '.txt', 'wb').write((cab + '\r\n' + t).encode('latin-1', 'replace'))
        nuevos += 1
    por_cat = {}
    for r in rels:
        por_cat.setdefault(categoria(r), []).append(r)
    lineas = ['# indice del catalogo para ProbadorPiezas 3.0: [grupo] y debajo sus modelos']
    resumen = []
    for i in sorted(por_cat):
        nombre = CATEGORIAS[i][0] if i < len(CATEGORIAS) else OTROS
        for g, lista in partir(nombre, por_cat[i]):
            lineas.append('[%s]' % g)
            lineas.extend(lista)
        resumen.append('%s %d' % (nombre, len(por_cat[i])))
    open(os.path.join(raiz, 'indice.txt'), 'wb').write(('\r\n'.join(lineas) + '\r\n').encode('latin-1', 'replace'))
    grupos = sum(1 for l in lineas if l.startswith('['))
    print('indice.txt: %d modelos en %d grupos | %s' % (len(rels), grupos, ' | '.join(resumen)))
    print('fichas nuevas: %d (sin huesos: %d, con error: %d)' % (nuevos, sin_huesos, errores))


if __name__ == '__main__':
    main(sys.argv[1])
