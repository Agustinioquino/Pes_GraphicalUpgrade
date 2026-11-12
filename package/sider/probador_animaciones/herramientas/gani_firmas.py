# gani_firmas.py - mide los .gani del catalogo del ProbadorAnimaciones (PES21 FX, 10/11/2026). SOLO LECTURA.
# Uso:  python gani_firmas.py "<carpeta content\probador-animaciones>" [nombres_800.txt] > informe.txt
# Lee cara.txt, cuerpo.txt y partido.txt (linea "raiz = carpeta" y despues una ruta relativa por linea),
# abre cada .gani y saca su esqueleto: piezas (hash) y, de cada pieza, sus pistas (indice, tipo, formato).
# Formato (confirmado en 9 archivos + muestras de .mtar): magia D2A2FC0B; "MOTION" en +0x50; bloque P = +0x90:
#   P+0 u32 piezas, P+4 u32 pistas, P+0xC u32 cuadros, P+0x14 u32[piezas] offsets relativos a P;
#   pieza: u32 hash, u8 n pistas, u8 bandera, u16 0, y n pistas de 8 B: u32 offset, u16 indice, u8 tipo, u8 formato.
import sys, os, struct, zlib, collections, re

MAGIA = bytes.fromhex('d2a2fc0b')
P = 0x90

def parse(path):
    try:
        with open(path, 'rb') as f:
            b = f.read(0x8000)
    except OSError:
        return None, 'falta'
    if len(b) < 0xA8 or b[:4] != MAGIA:
        return None, 'no es gani'
    if b[0x50:0x56] != b'MOTION' or struct.unpack_from('<I', b, 0x6c)[0] != 0x30:
        return None, 'sin MOTION en +0x50'
    nu, ns, _fl, cuadros = struct.unpack_from('<4I', b, P)
    if nu == 0 or nu > 2000 or P + 0x14 + 4 * nu > len(b):
        return None, 'cabecera rara (piezas %d)' % nu
    offs = struct.unpack_from('<%dI' % nu, b, P + 0x14)
    piezas, total = [], 0
    for o in offs:
        q = P + o
        if q + 8 > len(b):
            return None, 'tabla cortada'
        h, n, band = struct.unpack_from('<IBB', b, q)
        if q + 8 + 8 * n > len(b):
            return None, 'tabla cortada'
        pistas = []
        for k in range(n):
            _off, idx, tipo, fmt = struct.unpack_from('<IHBB', b, q + 8 + 8 * k)
            pistas.append((idx, tipo, fmt))
        total += n
        piezas.append((h, band, tuple(pistas)))
    if total != ns:
        return None, 'pistas %d != cabecera %d' % (total, ns)
    return {'nu': nu, 'ns': ns, 'cuadros': cuadros, 'piezas': piezas}, None

def crc(obj):
    return '%08X' % (zlib.crc32(repr(obj).encode()) & 0xffffffff)

def leer_lista(dir_, nombre):
    out = []
    p = os.path.join(dir_, nombre + '.txt')
    if not os.path.isfile(p):
        return None
    txt = open(p, 'rb').read().decode('utf-8', 'replace').lstrip('﻿')
    raiz = ''
    for line in txt.splitlines():
        l = line.strip()
        m = re.match(r'^raiz\s*=\s*(.+)$', l)
        if m:
            raiz = m.group(1)
            if not raiz.endswith(('\\', '/')):
                raiz += '\\'
        elif l and not l.startswith('#') and l.lower().endswith('.gani'):
            out.append((raiz + l.replace('/', '\\'), l.replace('/', '\\')))
    return out

def grupo(rel, lista):
    low = rel.lower()
    b = os.path.basename(low.replace('\\', '/'))
    if lista == 'partido':
        m = re.match(r'^([a-z]+)', b)
        return 'P:' + (m.group(1) if m else '?')
    carp = 'otra'
    for c, k in (('\\fixdemo\\', 'F'), ('\\interfaceload\\', 'IL'), ('\\interface\\', 'I'), ('\\face\\animations\\base\\', 'FB'),
                 ('\\face\\animations\\add\\', 'FA'), ('\\hand\\', 'H'), ('audience', 'AUD'), ('standsflag', 'SF')):
        if c in '\\' + low:
            carp = k
            break
    m = re.match(r'^([a-z]+)_([a-z]+)', b)
    if m:
        return carp + ':' + m.group(1) + '_' + m.group(2)
    m = re.match(r'^([a-z]+)', b)
    return carp + ':' + (m.group(1) if m else '?')

def main():
    dir_ = sys.argv[1]
    ochocientos = set()
    if len(sys.argv) > 2 and os.path.isfile(sys.argv[2]):
        ochocientos = set(x.strip().lower() for x in open(sys.argv[2], encoding='utf-8') if x.strip())
    rigs = collections.OrderedDict()      # firma de esqueleto -> datos
    errores = collections.Counter()
    ej_err = {}
    grupos = collections.defaultdict(lambda: collections.Counter())   # grupo -> firma -> n
    base_vistos = set()
    tot = collections.Counter()
    refs = {}
    vistos = set()
    for lista in ('cara', 'cuerpo', 'partido'):
        ent = leer_lista(dir_, lista)
        if ent is None:
            print('FALTA la lista %s.txt' % lista)
            continue
        for ruta, rel in ent:
            if ruta.lower() in vistos:
                continue
            vistos.add(ruta.lower())
            tot[lista] += 1
            b = os.path.basename(rel.replace('\\', '/')).lower()
            base_vistos.add(b[:-5])
            d, err = parse(ruta)
            g = grupo(rel, lista)
            if not d:
                errores[err] += 1
                ej_err.setdefault(err, rel)
                grupos[g]['ERROR'] += 1
                continue
            esq = tuple((h, tuple(i for i, _t, _f in ps)) for h, _b, ps in d['piezas'])
            forma = tuple((h, tuple((i, t & 0x0f, f) for i, t, f in ps)) for h, _b, ps in d['piezas'])
            fe = crc(esq)
            r = rigs.get(fe)
            if r is None:
                r = rigs[fe] = {'nu': d['nu'], 'ns': d['ns'], 'n': collections.Counter(), 'ej': [], 'formas': collections.Counter(),
                                'ej_forma': {}, 'piezas': d['piezas'], 'maxidx': max([i for _h, ps in esq for i in ps] or [-1]),
                                'grupos': collections.Counter(), 'tipos': collections.defaultdict(set)}
            r['n'][lista] += 1
            if len(r['ej']) < 4:
                r['ej'].append(rel)
            ff = crc(forma)
            r['formas'][ff] += 1
            r['ej_forma'].setdefault(ff, rel)
            r['grupos'][g] += 1
            for h, _b, ps in d['piezas']:
                for i, t, f in ps:
                    r['tipos'][i].add('%X:%02X' % (t & 0x0f, f))
            grupos[g][fe] += 1
            if b in ('edit_idle_loop.gani', 'edit_idle_loop_face.gani', 'edit_idle.gani', 'edit_idle_face.gani'):
                refs[b + ' (' + lista + ')'] = fe
    print('# gani_firmas: %s' % ', '.join('%s %d' % (k, v) for k, v in tot.items()))
    print('# errores: %s' % (', '.join('%s x%d (ej %s)' % (k, v, ej_err[k]) for k, v in errores.items()) or 'ninguno'))
    print('# referencias: %s' % (', '.join('%s = %s' % kv for kv in sorted(refs.items())) or 'NO ENCONTRE edit_idle_loop*.gani'))
    orden = sorted(rigs.items(), key=lambda kv: -sum(kv[1]['n'].values()))
    print('\n## ESQUELETOS (firma = piezas e indices de pista), de mas a menos usados: %d distintos' % len(orden))
    for i, (fe, r) in enumerate(orden):
        n = sum(r['n'].values())
        if i < 40 or n >= 5:
            top = ' '.join('%s=%d' % kv for kv in r['grupos'].most_common(6))
            print('%s piezas %d pistas %d maxidx %d | %d archivos (%s) | formas %d | ej: %s | grupos: %s' % (
                fe, r['nu'], r['ns'], r['maxidx'], n, ' '.join('%s %d' % kv for kv in r['n'].items()),
                len(r['formas']), ' ; '.join(os.path.basename(x) for x in r['ej'][:3]), top))
    resto = [(fe, r) for i, (fe, r) in enumerate(orden) if not (i < 40 or sum(r['n'].values()) >= 5)]
    if resto:
        print('... y %d esqueletos mas con menos de 5 archivos (%d archivos en total)' % (len(resto), sum(sum(r['n'].values()) for _f, r in resto)))
    print('\n## TABLA COMPLETA de los esqueletos de referencia y de los 6 mas usados (hash: indice=tipo:formato,...)')
    quiero = [fe for fe, _r in orden[:6]] + [v for v in refs.values()]
    hechos = set()
    for fe in quiero:
        if fe in hechos:
            continue
        hechos.add(fe)
        r = rigs[fe]
        print('%s (%d piezas, %d pistas):' % (fe, r['nu'], r['ns']))
        lin = []
        for h, _b, ps in r['piezas'][:60]:
            lin.append('%08X:%s' % (h, ','.join('%d=%s' % (i, '/'.join(sorted(r['tipos'][i]))) for i, _t, _f in ps)))
        for k in range(0, len(lin), 4):
            print('   ' + '  '.join(lin[k:k + 4]))
        if len(r['piezas']) > 60:
            print('   ... (%d piezas mas)' % (len(r['piezas']) - 60))
    print('\n## GRUPOS de nombres (carpeta:comienzo = total [firma=n ...]); F FixDemo, I Interface, IL InterfaceLoad, FB/FA cara, P partido')
    for g in sorted(grupos):
        c = grupos[g]
        print('%s = %d [%s]' % (g, sum(c.values()), ' '.join('%s=%d' % kv for kv in c.most_common(4))))
    if ochocientos:
        hit = sorted(ochocientos & base_vistos)
        print('\n## CRUCE con los %d nombres sin archivo: %d aparecen como .gani del catalogo%s' % (
            len(ochocientos), len(hit), (': ' + ', '.join(hit[:20])) if hit else ''))

if __name__ == '__main__':
    main()
