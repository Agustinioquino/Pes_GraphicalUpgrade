# PES21 FX (23/11, hilo banca 8z607v) - arma src/banca_caras.inc con las caras de cada jugador de las escenas horneadas
# (zfx_banca_e<e>_j<j>.gani), sacadas de acciones.json de la vista previa (vista_banca.py): lista (cuadro, clip, peso).
# Clip = indice en la lista fija de 166 caras base del exe (clips_base_indices.txt); los zfx_ de la vista (no existen en el juego)
# van a su par de Konami.
# Uso: caras_inc.py <clips_base_indices.txt> <salida .inc> <acciones.json escena 1> ... <acciones.json escena 4>
import sys, json
idx = {}
for ln in open(sys.argv[1]):
    a = ln.split()
    if len(a) >= 2: idx[a[1]] = int(a[0])
ZFX = {'zfx/zfx_hablar': ('smil_talk_blink_soft', 1.0), 'zfx/zfx_risa': ('smil_mho', 0.9), 'zfx/zfx_cejas_arriba': ('neut_brwup_breth', 1.0),
       'zfx/zfx_asombro': ('neut_brwup_mho_eyo_soft_l', 1.0)}
out = ['// generado por tools/banca/caras_inc.py: no editar a mano', '']
tab = []
for e, fn in enumerate(sys.argv[3:], 1):
    acc = json.load(open(fn))
    fila = []
    for j, a in enumerate(acc):
        ks = []
        for f, cl, w in sorted(a.get('cara', []), key=lambda x: x[0]):
            m = 1.0
            if cl in ZFX: cl, m = ZFX[cl]
            if cl not in idx: print('cara sin indice: %s (escena %d jugador %d): base_normal' % (cl, e, j)); cl, w = 'base_normal', 0.0
            f = max(0, int(f)); w = max(0.0, min(1.0, float(w) * m))
            if ks and ks[-1][0] == f: ks[-1] = (f, idx[cl], int(round(w * 100)))
            else: ks.append((f, idx[cl], int(round(w * 100))))
        if not ks: ks = [(0, idx['base_normal'], 0)]
        name = 'kCara_e%d_j%d' % (e, j)
        out.append('static const FaceKey %s[] = { %s };' % (name, ', '.join('{ %d, %d, %d }' % k for k in ks)))
        fila.append((name, len(ks)))
    tab.append(fila)
out.append('')
out.append('static const FaceKey* const kEscCara[%d][%d] = {' % (len(tab), len(tab[0])))
for fila in tab: out.append('    { %s },' % ', '.join(n for n, _ in fila))
out.append('};')
out.append('static const int kEscCaraN[%d][%d] = {' % (len(tab), len(tab[0])))
for fila in tab: out.append('    { %s },' % ', '.join(str(c) for _, c in fila))
out.append('};')
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
print('ok', sum(c for f in tab for _, c in f), 'claves')
