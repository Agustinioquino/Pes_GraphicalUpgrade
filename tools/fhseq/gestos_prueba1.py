#!/usr/bin/env python3
# Expresiones 2 - prueba 1 (12/11/2026): tablas de gestos de cara nuevas para entregar por livecpk de Sider.
# Parte de las tablas ORIGINALES (dt13 = las de livecpk\root de Agustin, identicas) y cambia solo lo listado en REGLAS.
# No agrega ni saca tramos en Facebase (misma cantidad de registros, mismo tamano); en Faceadd agrega tramos al final.
# Uso: python3 gestos_prueba1.py <carpeta con los .bin originales de dt13 common/anime/FHSequence/bin> <carpeta de salida>
# Salida de la prueba 1: package/sider/gestos/content/PES21FX-gestos/tablas (Facebase.bin B48D0CDC, Faceadd.bin 2C339C64).
import sys, os, collections
AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
import fhseq_leer as f
ORIG, OUT = sys.argv[1], sys.argv[2]
BASE = [l.split('\t')[1].strip() for l in open(os.path.join(AQUI, 'clips_base_indices.txt'))]
LARGO = {'base_kuisibari_hard': 70, 'base_syucyu_iki_soft': 50, 'pow_brwnit_grit_hard': 42, 'base_kuisibari': 42, 'base_syucyu_iki': 50, 'neut_brwtrb_breth': 66, 'neut_brwnit_deep': 84}
def poner(r, clip, o0=None, o1=None):
    n = r['fin'] - r['inicio']; L = LARGO[clip]
    if o0 is None: o0, o1 = 0, min(n, L - 1)          # mismo ritmo si entra; si el tramo es mas largo que el clip, el clip se estira
    assert 0 <= o0 < o1 <= L, (clip, o0, o1)
    r['clip'] = BASE.index(clip); r['origen_inicio'] = o0; r['origen_fin'] = o1
fb = list(f.facebase_crudo(open(os.path.join(ORIG, 'Facebase.bin'), 'rb').read()))
porid = collections.defaultdict(list)
for r in fb: porid[r['id']].append(r)
cambios = []
def regla(i, n, clip, o0=None, o1=None, solo=None):
    r = porid[i][n]
    if solo is not None and BASE[r['clip']] != solo: raise SystemExit('id %d tramo %d: esperaba %s y hay %s' % (i, n, solo, BASE[r['clip']]))
    antes = '%d-%d %s (%d-%d)' % (r['inicio'], r['fin'], BASE[r['clip']], r['origen_inicio'], r['origen_fin'])
    poner(r, clip, o0, o1); cambios.append('id %5d tramo %d: %s -> %s (%d-%d)' % (i, n, antes, clip, r['origen_inicio'], r['origen_fin']))
# --- patadas medias (grupo 18, 147 animaciones): dientes apretados en el golpe y despues respiracion suave ---
regla(-198, 0, 'base_kuisibari_hard', 0, 42, solo='base_shoot_impact')
regla(-198, 1, 'base_syucyu_iki_soft', 0, 32, solo='base_kutiake_dribble')
# --- patadas largas y remates (grupo 19, 144 animaciones): esfuerzo fuerte en el golpe, apretar, respirar ---
regla(-199, 0, 'pow_brwnit_grit_hard', 8, 42, solo='pow_brwup_unnos_hard')
regla(-199, 1, 'base_kuisibari', 8, 32, solo='base_kutiake_dribble')
regla(-199, 2, 'base_syucyu_iki', 0, 32, solo='base_kutiake_dribble')
# --- pases cortos (grupo 11, id -197): SIN CAMBIOS, quedan para comparar ---
# --- penales: espera con respiracion nerviosa (cejas tensas), carrera con ceno fruncido y respiracion honda, apretar al patear ---
# (intensidad medida contra la cara neutra: base_syucyu 4, neut_brwtrb_breth 13, neut_brwnit_deep 22, base_kuisibari 22)
for i in (2913, 2914, 2915, 2916, 2917, 2918, 2919, 2920, 4384):
    for n in range(len(porid[i])): regla(i, n, 'neut_brwtrb_breth', solo='base_syucyu')
for i in (2926, 2927, 2928, 2929, 2930, 2931, 2932, 2933, 4385):
    for n in range(len(porid[i])):
        ultimo = n == len(porid[i]) - 1 and len(porid[i]) > 1 and n < 2
        regla(i, n, 'base_kuisibari' if ultimo else 'neut_brwnit_deep', solo='base_syucyu')
os.makedirs(OUT, exist_ok=True)
d = f.facebase_escribir(fb); open(os.path.join(OUT, 'Facebase.bin'), 'wb').write(d)
# --- Faceadd: cejas arriba + boca ancha al arrancar los festejos de gol que ya tienen cara propia (prueba de la capa sumada) ---
nombres = {}
for l in open(os.path.join(AQUI, 'caras_por_animacion.tsv'), encoding='utf-8'):
    p = l.rstrip('\n').split('\t')
    if p[0].lstrip('-').isdigit(): nombres[int(p[0])] = (p[1], int(p[2]) if p[2] else None)
fa = list(f.faceadd(open(os.path.join(ORIG, 'Faceadd.bin'), 'rb').read())); n0 = len(fa); yatiene = collections.Counter(r['id'] for r in fa)
agregados = 0
for i in sorted(porid):
    nom, serie = nombres.get(i, ('', None))
    if not nom.startswith('dm_goal_') or not (0 <= i <= 1883) or serie is None or not (105 <= serie <= 2152): continue
    fin = max(r['fin'] for r in porid[i])
    if fin < 45 or yatiene[i] > 2: continue
    fa.append(dict(id=i, serie=serie, inicio=4, fin=min(fin, 60), origen_inicio=0, origen_fin=1, clip=f.ADD.index('add_mayu_up'), peso=0.8, fade_in=8, fade_out=20))
    fa.append(dict(id=i, serie=serie, inicio=10, fin=min(fin, 50), origen_inicio=0, origen_fin=24, clip=f.ADD.index('add_kuti_yoko'), peso=0.5, fade_in=8, fade_out=15))
    agregados += 2
da = f.faceadd_escribir(fa); open(os.path.join(OUT, 'Faceadd.bin'), 'wb').write(da)
print('\n'.join(cambios)); print('Facebase: %d tramos cambiados de %d, %d B' % (len(cambios), len(fb), len(d)))
print('Faceadd: %d tramos originales + %d nuevos en %d festejos, %d B' % (n0, agregados, agregados // 2, len(da)))
