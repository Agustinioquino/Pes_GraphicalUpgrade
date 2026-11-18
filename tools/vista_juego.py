# Vista previa "como en el juego" de una animacion de pareja (.gani A y B):
# - cuerpo, manos con dedos, cara y pelo = mallas del juego (modelo base del Crear jugador + caras reales)
# - esqueleto con los largos del juego (lugares de los huesos sacados de los fmdl), en la convencion de giro del juego:
#   giro identidad = brazos en cruz (T), piernas y columna derechas. (CONFIRMADO 18/11 con el parado del juego: el brazo
#   gira 85 grados en el cuadro 0; con identidad = pose A la mano izquierda cruzaria el cuerpo a x = -0.20)
# - estatura real de cada uno (escala del modelo) y B delante de A a D x estatura media, girado 180 grados (como el Motor)
# Salida: escena.json para vista_juego.html (dibujo con WebGL en Chromium sin ventana) -> cuadros PNG -> GIF / MP4.
# Uso: vista_juego.py <a.gani> <b.gani> <carpeta modelo_base> <carpeta caras (NNNN_face_high.fmdl ...)> <salida.json>
#      [--caras 7511,4522] [--estaturas 170,185] [--D 5.74]
import sys, os, math, json, base64, struct, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fox'))
import numpy as np
import gani_cuerpo as G
from fmdl import Fmdl, meshes

# ---------------------------------------------------------------- esqueleto del juego (modelo a escala 1 = 1.0 de escala del Motor)
# lugares en la pose de las mallas (pose A), metros, x = izquierda del jugador, y = arriba, z = adelante
BIND = {
    'root': (0, 0, 0), 'hips': (0, 0.992, -0.018), 'spine': (0, 1.096, 0), 'chest': (0, 1.263, 0.014),
    'neck': (0, 1.532, 0.033), 'head': (0, 1.640, 0.054),
    'clavL': (0.03, 1.455, 0.030), 'uarmL': (0.195, 1.467, 0.033), 'farmL': (0.399, 1.262, 0.014), 'handL': (0.603, 1.064, 0.070),
    'thighA': (0.090, 0.960, 0.053), 'kneeA': (0.141, 0.543, 0.088), 'footA': (0.194, 0.107, 0.041),
}
for k in list(BIND):
    if k.endswith('L'): x, y, z = BIND[k]; BIND[k[:-1] + 'R'] = (-x, y, z)
    if k.endswith('A'): x, y, z = BIND[k]; BIND[k[:-1] + 'B'] = (-x, y, z)
PAR = {b: G.S[b][0] for b in G.S if b != 'headc'}
CH = {b: G.S[b][1] for b in G.S if b != 'headc'}
BONES = [b for b in G.ORDER if b != 'headc']
BI = {b: i for i, b in enumerate(BONES)}

def ln(a, b): return float(np.linalg.norm(np.subtract(BIND[b], BIND[a])))
# corrimientos con giro identidad (convencion T): largos del juego, direcciones de la convencion
OFF = {'root': (0, 0, 0), 'hips': BIND['hips']}
for b in ('spine', 'chest', 'neck', 'head'): OFF[b] = tuple(np.subtract(BIND[b], BIND[PAR[b]]))
for s, sg in (('L', 1), ('R', -1)):
    OFF['clav' + s] = tuple(np.subtract(BIND['clav' + s], BIND['chest']))
    OFF['uarm' + s] = (sg * ln('clav' + s, 'uarm' + s), 0, 0)
    OFF['farm' + s] = (sg * ln('uarm' + s, 'farm' + s), 0, 0)
    OFF['hand' + s] = (sg * ln('farm' + s, 'hand' + s), 0, 0)
for p in 'AB':
    OFF['thigh' + p] = tuple(np.subtract(BIND['thigh' + p], BIND['hips']))
    OFF['knee' + p] = (0, -ln('thigh' + p, 'knee' + p), 0)
    OFF['foot' + p] = (0, -ln('knee' + p, 'foot' + p), 0)

def q_from_to(a, b):
    a = np.array(a, float) / np.linalg.norm(a); b = np.array(b, float) / np.linalg.norm(b)
    c = np.cross(a, b); d = float(np.dot(a, b))
    if d < -0.9999: return (0, 0, 1, 0)
    return G.qn((c[0], c[1], c[2], 1 + d))
# giro de cada hueso en la pose de las mallas (convencion T): lleva la direccion de la convencion a la direccion real
RB = {b: (0, 0, 0, 1) for b in BONES}
for s, sg in (('L', 1), ('R', -1)):
    RB['uarm' + s] = q_from_to((sg, 0, 0), np.subtract(BIND['farm' + s], BIND['uarm' + s]))
    RB['farm' + s] = q_from_to((sg, 0, 0), np.subtract(BIND['hand' + s], BIND['farm' + s]))
    RB['hand' + s] = RB['farm' + s]
for p in 'AB':
    RB['thigh' + p] = q_from_to((0, -1, 0), np.subtract(BIND['knee' + p], BIND['thigh' + p]))
    RB['knee' + p] = q_from_to((0, -1, 0), np.subtract(BIND['foot' + p], BIND['knee' + p]))
    # el pie con giro identidad queda plano hacia adelante (como el pie de la malla)

def fk(rot, rootpos, hipspos):
    out = {}
    for b in BONES:
        lq = rot.get(CH[b], (0, 0, 0, 1)) if CH[b] is not None else (0, 0, 0, 1)
        if PAR[b] is None: out[b] = (tuple(rootpos), lq); continue
        pp, pq = out[PAR[b]]
        o = G.add(OFF[b], hipspos) if b == 'hips' else OFF[b]
        out[b] = (G.add(pp, G.qrot(pq, o)), G.qm(pq, lq))
    return out

def qmat3(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])

# ---------------------------------------------------------------- huesos de las mallas -> huesos del esqueleto
def lado(n):
    t = n.split('_')
    return 'L' if 'l' in t[2:] or t[-1] == 'l' else ('R' if 'r' in t[2:] or t[-1] == 'r' else None)
def destino(n):
    """[(hueso, peso)] para un hueso de malla"""
    s = lado(n); P = {'L': 'A', 'R': 'B'}.get(s)
    fixed = {'sk_belly': 'spine', 'sk_chest': 'chest', 'sk_neck': 'neck', 'sk_head': 'head', 'dsk_hip': 'hips',
             'dsk_belly_scale': 'spine', 'dsk_scm': 'neck', 'dsk_neckback': 'neck', 'dsk_back': 'chest'}
    if n in fixed: return [(fixed[n], 1)]
    if n.startswith('skf_'): return [('head', 1)]
    if n.startswith('skh_'): return [('hand' + s, 1)]
    if n.startswith('sk_'):
        k = n[3:].rsplit('_', 1)[0]
        m = {'upperarm': 'uarm', 'forearm': 'farm', 'hand': 'hand', 'thigh': 'thigh', 'leg': 'knee', 'foot': 'foot'}[k]
        return [(m + (s if m in ('uarm', 'farm', 'hand') else P), 1)]
    if s is None:
        if 'belly' in n: return [('spine', 1)]
        if 'sternum' in n: return [('chest', 1)]
        return [('chest', 1)]
    for key, d in (('elbow', [('uarm' + s, .5), ('farm' + s, .5)]), ('wrist', [('farm' + s, .5), ('hand' + s, .5)]),
                   ('forearm', [('farm' + s, 1)]), ('upperarm', [('uarm' + s, 1)]), ('deltoid', [('uarm' + s, .6), ('clav' + s, .4)]),
                   ('sleeve', [('uarm' + s, 1)]), ('underarm', [('uarm' + s, .5), ('chest', .5)]), ('clavicle', [('clav' + s, 1)]),
                   ('trapezius', [('clav' + s, .5), ('chest', .5)]), ('scapula', [('clav' + s, .5), ('chest', .5)]),
                   ('pectoralis', [('chest', 1)]), ('collar', [('chest', 1)]), ('sternum', [('chest', 1)]),
                   ('belly', [('spine', 1)]), ('hem', [('thigh' + P, 1)]), ('pants', [('thigh' + P, .7), ('hips', .3)]),
                   ('hip', [('thigh' + P, .5), ('hips', .5)])):
        if key in n: return d
    return [('chest', 1)]

def pieza(path, mat_ok=None):
    f = Fmdl(path); out = []
    for mi, m in enumerate(meshes(f)):
        if m['pos'] is None or m['faces'] is None: continue
        if mat_ok and not mat_ok(mi, m): continue
        nv = m['nvert']; W = np.zeros((nv, len(BONES)), np.float32)
        if m['w'] is None:
            W[:, BI['chest']] = 1
        else:
            for j in range(4):
                for v in range(nv):
                    w = m['w'][v, j]
                    if w <= 0: continue
                    for b, k in destino(f.bones[m['idx'][v, j]]['name']): W[v, BI[b]] += w * k
        W /= np.maximum(W.sum(1, keepdims=True), 1e-6)
        top = np.argsort(-W, axis=1)[:, :4]; tw = np.take_along_axis(W, top, 1); tw /= np.maximum(tw.sum(1, keepdims=True), 1e-6)
        nrm = m['nrm'][:, :3] if m['nrm'] is not None else np.zeros((nv, 3), np.float32)
        uv = m['uv'] if m['uv'] is not None else np.zeros((nv, 2), np.float32)
        out.append(dict(mesh=mi, mat=m['mat'], pos=m['pos'].astype(np.float32), nrm=nrm.astype(np.float32), uv=uv.astype(np.float32),
                        bi=top.astype(np.float32), bw=tw.astype(np.float32), faces=m['faces'].astype(np.uint32)))
    return out

def b64(a): return base64.b64encode(np.ascontiguousarray(a).tobytes()).decode()

# ---------------------------------------------------------------- texturas
def textura(ftex_path, png_path):
    import ftex
    from PIL import Image
    import io
    f = ftex.Ftex(open(ftex_path, 'rb').read())
    if f.fmt in (2, 3, 4): dds = ftex.dxt_to_dds(f, mip0only=True)
    else:   # 11 = BC7 (cabecera DX10)
        dx = {11: 98, 9: 83, 8: 80}[f.fmt]
        hdr = struct.pack('<4sIIIIIII', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000, f.h, f.w, len(f.mips[0]), 0, 1) + b'\0' * 44
        hdr += struct.pack('<II4sIIIII', 32, 0x4, b'DX10', 0, 0, 0, 0, 0) + struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
        dds = hdr + struct.pack('<IIIII', dx, 3, 0, 1, 0) + f.mips[0]
    im = Image.open(io.BytesIO(dds)); im.load(); im = im.convert('RGBA'); im.thumbnail((1024, 1024)); im.save(png_path)
    return os.path.basename(png_path)

def piel(m, mats, esc, quien, D):
    """vertices y normales de la malla m en el mundo (mats = 20 x 12 del cuadro)"""
    M = mats.reshape(20, 4, 3)                       # columnas: R0 R1 R2 t
    bi = m['bi'].astype(int); bw = m['bw']
    R = np.einsum('vk,vkcr->vrc', bw, M[bi, :3, :]); t = np.einsum('vk,vkr->vr', bw, M[bi, 3, :])
    p = np.einsum('vrc,vc->vr', R, m['pos']) + t; n = np.einsum('vrc,vc->vr', R, m['nrm'])
    p = p * esc
    if quien == 1: p = np.stack([-p[:, 0], p[:, 1], D - p[:, 2]], 1); n = np.stack([-n[:, 0], n[:, 1], -n[:, 2]], 1)
    return p, n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-6)

def revisar(cuerpo, jug, D, paso=2):
    """cuanto se mete un cuerpo en el otro (camiseta + cara contra camiseta + cara), por cuadro: nos quedamos con el peor"""
    duros = [m for m in cuerpo if m['kind'] in ('shirt', 'shorts')]
    sub_ = lambda ms: [dict(m, **{k: m[k][::3] for k in ('pos', 'nrm', 'bi', 'bw')}) for m in ms]
    duros = sub_(duros); res = []
    for f in range(0, jug[0]['n'] + 1, paso):
        PN = []
        for i in (0, 1):
            ms = duros + sub_([m for m in jug[i]['cara'] if m['kind'] == 'face'])
            ps, ns = zip(*[piel(m, jug[i]['frames'][f], jug[i]['escala'], i, D) for m in ms]); PN.append((np.concatenate(ps), np.concatenate(ns)))
        peor = 0.0
        for a, b in ((0, 1), (1, 0)):
            pa = PN[a][0]; pb, nb = PN[b]
            lo = pb.min(0) - 0.05; hi = pb.max(0) + 0.05; sel = np.all((pa > lo) & (pa < hi), 1); pa = pa[sel]
            if not len(pa): continue
            d2 = ((pa[:, None, :] - pb[None, :, :]) ** 2).sum(2); j = d2.argmin(1)
            pen = -np.einsum('vr,vr->v', pa - pb[j], nb[j]); pen[np.sqrt(d2[np.arange(len(pa)), j]) > 0.06] = 0
            peor = max(peor, float(pen.max()))
        res.append((f, peor))
    return res

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a'); ap.add_argument('b'); ap.add_argument('base'); ap.add_argument('caras'); ap.add_argument('salida')
    ap.add_argument('--caras_id', default='7511,4522'); ap.add_argument('--estaturas', default='170,185'); ap.add_argument('--D', type=float, default=5.74)
    ap.add_argument('--revisar', action='store_true'); ap.add_argument('--botas', default='')
    ap.add_argument('--colores', default='0.80,0.16,0.14;0.14,0.30,0.75')
    o = ap.parse_args()
    ids = o.caras_id.split(','); est = [float(x) for x in o.estaturas.split(',')]
    # escala del modelo en el juego: 170 cm -> 0.932, 185 cm -> 1.033 (registro del Motor 17/11) => lineal
    esc = [0.932 + (e - 170) * (1.033 - 0.932) / 15.0 for e in est]
    cols = [[float(x) for x in c.split(',')] for c in o.colores.split(';')]
    outdir = os.path.dirname(os.path.abspath(o.salida))
    B = o.base; W7 = os.path.join(B, ids[0], '#Win')
    piezas_cuerpo = [('shirt', os.path.join(B, ids[0], 'nocloth', '#Win', 'shirt_out.fmdl')),
                     ('shirt', os.path.join(B, ids[0], 'nocloth', '#Win', 'sleeve_short_001.fmdl')),
                     ('skin', os.path.join(W7, 'arm.fmdl')), ('skin', os.path.join(B, 'naked_hand_l.fmdl')), ('skin', os.path.join(B, 'naked_hand_r.fmdl')),
                     ('skin', os.path.join(B, 'torso.fmdl')), ('shorts', os.path.join(W7, 'pants_out_sub.fmdl')),
                     ('shorts', os.path.join(B, ids[0], 'nocloth', '#Win', 'pants_001.fmdl')),
                     ('skin', os.path.join(W7, 'thigh_middle.fmdl')), ('socks', os.path.join(W7, 'socks_middle.fmdl'))]
    cuerpo = []
    for kind, p in piezas_cuerpo:
        if not os.path.exists(p): print('falta', p); continue
        ms = pieza(p)
        if kind in ('socks', 'skin') and ('socks' in p or 'thigh' in p):
            # medias y muslos: los pesos que leemos no siguen a la rodilla (las medias traen solo sk_thigh / sk_foot):
            # se reparten por altura: muslo arriba de la rodilla, canilla entre rodilla y tobillo, pie abajo
            for m in ms:
                y = m['pos'][:, 1]; side = np.where(m['pos'][:, 0] >= 0, 0, 1)
                th = np.array([BI['thighA'], BI['thighB']])[side]; kn = np.array([BI['kneeA'], BI['kneeB']])[side]; ft = np.array([BI['footA'], BI['footB']])[side]
                a = np.clip((y - 0.50) / 0.10, 0, 1); c = np.clip((0.17 - y) / 0.07, 0, 1); b = 1 - a - c
                m['bi'] = np.stack([th, kn, ft, kn], 1).astype(np.float32); m['bw'] = np.stack([a, b, c, 0 * a], 1).astype(np.float32)
        cuerpo += [dict(m, kind=kind) for m in ms]
    # botines (fpk del juego: Asset/model/character/boots/kNNNN; --botas = carpeta con boots.fmdl y ek*_c_L_bsm_alp.ftex). Vienen hechos
    # para pies a +-0.093 m (piernas derechas): se corren a los pies del modelo (+-0.194, piernas en A) y van enteros al hueso del pie
    texc = {}
    if o.botas and os.path.exists(os.path.join(o.botas, 'boots.fmdl')):
        for m in pieza(os.path.join(o.botas, 'boots.fmdl')):
            x = m['pos'][:, 0]; side = np.where(x >= 0, 1.0, -1.0)
            m['pos'] = m['pos'].copy(); m['pos'][:, 0] += side * (BIND['footA'][0] - 0.093)
            m['bi'] = np.stack([np.where(side > 0, BI['footA'], BI['footB'])] * 4, 1).astype(np.float32)
            m['bw'] = np.tile(np.array([1, 0, 0, 0], np.float32), (len(x), 1))
            cuerpo.append(dict(m, kind='boots'))
        import glob
        c = [f for f in glob.glob(os.path.join(o.botas, 'ek*_c_L_bsm_alp.ftex')) if not os.path.basename(f).startswith('ek0051i') and 'i_c_' not in os.path.basename(f)]
        if c: texc['boots'] = textura(c[0], os.path.join(outdir, 'botas_bsm.png'))
    jug = []
    for i in (0, 1):
        g = G.Base(open([o.a, o.b][i], 'rb').read())
        cid = ids[i]; tx = {}
        for t in ('face_bsm_alp', 'hair_parts_bsm_alp'):
            for d in (os.path.join(B, cid, 'cpk', 'sourceimages', '#windx11'),):
                fp = os.path.join(d, t + '.ftex')
                if os.path.exists(fp): tx[t] = textura(fp, os.path.join(outdir, '%s_%s.png' % (cid, t)))
        cara = [dict(m, kind='face' if m['mat'] in (0, 2) else 'eye') for m in pieza(os.path.join(o.caras, cid + '_face_high.fmdl'))]
        pelo = [dict(m, kind='face' if m['mat'] == 0 else 'hair') for m in pieza(os.path.join(o.caras, cid + '_hair_high.fmdl'))]
        frames = []
        for f in range(g.n + 1):
            P = fk(g.rot[f], g.root[f], g.hips[f]); mats = []
            for b in BONES:
                pp, pq = P[b]; R = qmat3(pq) @ qmat3(RB[b]).T
                t = np.array(pp) - R @ np.array(BIND[b])
                mats.append(np.concatenate([R, t[:, None]], 1).T.reshape(-1))   # 4 columnas de 3 (columna mayor)
            frames.append(np.array(mats, np.float32).reshape(-1))
        jug.append(dict(escala=esc[i], estatura=est[i], color=cols[i], tex=tx, cara=cara + pelo, frames=np.array(frames, np.float32), n=g.n))
    escm = 0.5 * (esc[0] + esc[1]); Dm = o.D * escm
    if o.revisar:
        r = revisar(cuerpo, jug, Dm)
        malos = [(f, p) for f, p in r if p > 0.01]
        print('cuerpo dentro del otro (> 1 cm): %d cuadros de %d; peor %.1f cm en el cuadro %d' % (len(malos), len(r), max(p for f, p in r) * 100, max(r, key=lambda x: x[1])[0]))
        tramos = []
        for f, p in malos:
            if tramos and f - tramos[-1][1] <= 4: tramos[-1][1] = f; tramos[-1][2] = max(tramos[-1][2], p)
            else: tramos.append([f, f, p])
        for a, b, p in tramos: print('  cuadros %d-%d: hasta %.1f cm' % (a, b, p * 100))
    def enc(ms): return [dict(kind=m['kind'], pos=b64(m['pos']), nrm=b64(m['nrm']), uv=b64(m['uv']), bi=b64(m['bi']), bw=b64(m['bw']),
                               faces=b64(m['faces']), nv=int(len(m['pos'])), nf=int(len(m['faces']))) for m in ms]
    J = dict(texc=texc, huesos=BONES, bind=[list(BIND[b]) for b in BONES], D=Dm, cuerpo=enc(cuerpo),
             jugadores=[dict(escala=j['escala'], estatura=j['estatura'], color=j['color'], tex=j['tex'], cara=enc(j['cara']),
                             n=j['n'], frames=b64(j['frames'])) for j in jug])
    json.dump(J, open(o.salida, 'w'))
    print('escena', o.salida, os.path.getsize(o.salida), 'B; cuadros', [j['n'] for j in jug], 'escalas', esc, 'D', Dm)

if __name__ == '__main__':
    main()
