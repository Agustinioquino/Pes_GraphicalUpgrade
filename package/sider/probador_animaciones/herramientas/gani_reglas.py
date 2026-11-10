# gani_reglas.py - compara las reglas por NOMBRE de ProbadorAnimaciones 3.1 con el esqueleto real de cada .gani. SOLO LECTURA.
# Uso: python gani_reglas.py "<carpeta content\probador-animaciones>" > gani_reglas_informe.txt   (necesita gani_firmas.py al lado)
import sys, os, re, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gani_firmas as gf
CUERPO, CARA = '1787A4C3', 'C15E13FF'
CAT_ESCENA = {'dml_goal': 3, 'dml_hugset': 3, 'dml_offsidegoal': 3, 'dml_end': 4, 'dml_ent': 5, 'dml_tu': 5, 'dml_md': 6,
              'dml_foul': 7, 'dml_pk': 7, 'dml_change': 7, 'dml_injury': 7, 'dml_assistant': 7, 'dml_menu': 1}
CATS = ['', 'Editar y menus', 'Gameplay (partido)', 'Festejos de gol', 'Ceremonias y final', 'Entrada y previa', 'Liga Master (dml_md)',
        'Arbitro, faltas, penales y cambios', 'Gestos de cara', 'Varias']
def clasificar(rel, de_partido):
    r = '\\' + rel.lower()
    b = r.rsplit('\\', 1)[-1]
    if ('\\hand\\' in r or 'audience' in r or 'standsflag' in r or b.endswith('hand.gani') or b.endswith('_eyes_target.gani')
            or b.endswith('_mobh.gani') or b.startswith('dml_mobh') or b.startswith('dml_prop') or '_enum_dummy' in b):
        return None, None
    tipo = 'cara' if (b.endswith('_face.gani') or '\\face\\' in r) else 'cuerpo'
    if '\\face\\animations\\' in r: return tipo, 8
    if de_partido: return tipo, 2
    if '\\interfaceload\\' in r or '\\interface\\' in r: return tipo, 1
    if 'fixdemo' in r:
        m = re.match(r'^([a-z]+)_([a-z]+)', b)
        if not m or m.group(1) != 'dml': return tipo, 1
        return tipo, CAT_ESCENA.get(m.group(1) + '_' + m.group(2), 9)
    return tipo, 9
def grupo(rel):
    carpeta, _, nombre = rel.rpartition('\\')
    n = nombre.lower()
    if carpeta.lower().endswith('fixdemo\\animations'):
        m = re.match(r'^([a-z]+)_([a-z]+)_([a-z]+)(.*)$', n)
        if m:
            c = re.match(r'^_(\d\d)\d\d', m.group(4))
            return '%s_%s_%s%s' % (m.group(1), m.group(2), m.group(3), ('_' + c.group(1) + '**') if c else '')
        m = re.match(r'^([a-z]+)_([a-z]+)', n)
        return (m.group(1) + '_' + m.group(2)) if m else (re.match(r'^([a-z]+)', n) or [None, 'otras'])[1]
    corta = carpeta.rsplit('\\', 1)[-1]
    if '\\face\\animations' in carpeta.lower():
        m = re.match(r'^([a-z]+)', n)
        return corta + ': ' + (m.group(1) if m else 'otras')
    return corta
def main():
    dir_ = sys.argv[1]
    mal_excl, mal_canal, perezosas = [], [], collections.Counter()
    cats = {'cuerpo': collections.Counter(), 'cara': collections.Counter()}
    grupos = {'cuerpo': collections.defaultdict(collections.Counter), 'cara': collections.defaultdict(collections.Counter)}
    en_lista = collections.Counter()
    vistos = set()
    ej_perez = collections.defaultdict(list)
    for lista in ('cara', 'cuerpo', 'partido'):
        for ruta, rel in (gf.leer_lista(dir_, lista) or []):
            if ruta.lower() in vistos: continue
            vistos.add(ruta.lower())
            d, err = gf.parse(ruta)
            fe = None
            if d:
                fe = gf.crc(tuple((h, tuple(i for i, _t, _f in ps)) for h, _b, ps in d['piezas']))
            tipo, cat = clasificar(rel, lista == 'partido')
            real = 'cuerpo' if fe == CUERPO else ('cara' if fe == CARA else None)
            if tipo is None:
                if real: mal_excl.append('%s (%s)' % (rel, real))
                continue
            en_lista[tipo] += 1
            if real != tipo:
                if real: mal_canal.append('%s (regla %s, real %s)' % (rel, tipo, real))
                perezosas[tipo] += 1
                if len(ej_perez[tipo]) < 12: ej_perez[tipo].append(os.path.basename(rel.replace('\\', '/')))
                continue
            cats[tipo][cat] += 1
            grupos[tipo][cat][grupo(rel)] += 1
    print('# gani_reglas (ProbadorAnimaciones 3.1)')
    print('EXCLUIDAS POR NOMBRE QUE SI SON DE JUGADOR (deberia ser 0): %d' % len(mal_excl))
    for x in mal_excl[:60]: print('   ' + x)
    print('CANAL EQUIVOCADO por nombre (deberia ser 0): %d' % len(mal_canal))
    for x in mal_canal[:40]: print('   ' + x)
    for tipo in ('cuerpo', 'cara'):
        print('%s: en la lista %d, de jugador %d, las saltea el lector %d (ej: %s)' % (
            tipo.upper(), en_lista[tipo], sum(cats[tipo].values()), perezosas[tipo], ', '.join(ej_perez[tipo])))
        for cat in sorted(cats[tipo]):
            g = grupos[tipo][cat]
            print('   %d %s = %d en %d grupos; mayores: %s' % (cat, CATS[cat], cats[tipo][cat], len(g),
                  ' '.join('%s=%d' % kv for kv in g.most_common(8))))
if __name__ == '__main__':
    main()
