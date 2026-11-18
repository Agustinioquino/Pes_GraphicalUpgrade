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
# lugares en la pose de las mallas (pose A, fmdl del Crear jugador), metros, x = izquierda del jugador, y = arriba, z = adelante.
# Columna, cuello, cabeza y hombros = body_anim_skel.ask (reposo); brazos y piernas = fmdl (en la pose A de las mallas)
BIND = {
    'root': (0, 0, 0), 'hips': (0, 1.0961, 0), 'pelvis': (0, 0.9921, -0.0184), 'spine': (0, 1.0961, 0), 'chest': (0, 1.2628, 0.0138),
    'neck': (0, 1.5321, 0.0335), 'head': (0, 1.6401, 0.0544),
    'clavL': (0.1051, 1.4671, 0.0335), 'uarmL': (0.1950, 1.4671, 0.0335), 'farmL': (0.399, 1.262, 0.014), 'handL': (0.603, 1.064, 0.070),
    'thighA': (0.090, 0.960, 0.053), 'kneeA': (0.141, 0.543, 0.088), 'footA': (0.194, 0.107, 0.041),
}
for k in list(BIND):
    if k.endswith('L'): x, y, z = BIND[k]; BIND[k[:-1] + 'R'] = (-x, y, z)
    if k.endswith('A'): x, y, z = BIND[k]; BIND[k[:-1] + 'B'] = (-x, y, z)
PAR = {b: G.S[b][0] for b in G.S if b != 'headc'}
CH = {b: G.S[b][1] for b in G.S if b != 'headc'}
OFF = {b: G.S[b][2] for b in G.S if b != 'headc'}
BONES = [b for b in G.ORDER if b != 'headc']

def q_from_to(a, b):
    a = np.array(a, float) / np.linalg.norm(a); b = np.array(b, float) / np.linalg.norm(b)
    c = np.cross(a, b); d = float(np.dot(a, b))
    if d < -0.9999: return (0, 0, 1, 0)
    return G.qn((c[0], c[1], c[2], 1 + d))
# giro de cada hueso en la pose de las mallas (reposo de animacion = identidad): lleva el corrimiento de reposo del hijo a donde esta en
# las mallas. La mano sigue al antebrazo (muneca derecha) y el pie queda como en reposo (plano)
RB = {b: (0, 0, 0, 1) for b in BONES}
for s_ in ('L', 'R'):
    RB['uarm' + s_] = q_from_to(OFF['farm' + s_], np.subtract(BIND['farm' + s_], BIND['uarm' + s_]))
    RB['farm' + s_] = q_from_to(OFF['hand' + s_], np.subtract(BIND['hand' + s_], BIND['farm' + s_]))
    RB['hand' + s_] = RB['farm' + s_]
for p_ in 'AB':
    RB['thigh' + p_] = q_from_to(OFF['knee' + p_], np.subtract(BIND['knee' + p_], BIND['thigh' + p_]))
    RB['knee' + p_] = q_from_to(OFF['foot' + p_], np.subtract(BIND['foot' + p_], BIND['knee' + p_]))

# (18/11) dedos (esqueleto de la mano del juego, hand_anim_skel.ask, 19 huesos por mano) y cara (face_anim_skel.ask, 29 huesos skf_):
# van despues de los huesos del cuerpo en la misma tabla de huesos de la vista
HAND_ASK = ['skh_thumb_mata_l', 'skh_thumb_mcp_l', 'skh_thumb_pip_l', 'skh_index_mata_l', 'skh_index_mcp_l', 'skh_index_pip_l', 'skh_index_dip_l',
            'skh_middle_mata_l', 'skh_middle_mcp_l', 'skh_middle_pip_l', 'skh_middle_dip_l', 'skh_pinky_mata_l', 'skh_pinky_mcp_l', 'skh_pinky_pip_l',
            'skh_pinky_dip_l', 'skh_ring_mata_l', 'skh_ring_mcp_l', 'skh_ring_pip_l', 'skh_ring_dip_l']
FACE_ASK = ['skf_jaw', 'skf_brow_o_l', 'skf_brow_o_r', 'skf_brow_i_l', 'skf_brow_i_r', 'skf_nasolabialfold_t_l', 'skf_nasolabialfold_t_r',
            'skf_orbicularisoculi_b_r', 'skf_orbicularisoculi_b_l', 'skf_lip_b_l', 'skf_lip_b_r', 'skf_lip_b_c', 'skf_glabella', 'skf_eyelid_t_r',
            'skf_eyelid_t_l', 'skf_eyelid_b_r', 'skf_eyelid_b_l', 'skf_nose', 'skf_nosewing_l', 'skf_nosewing_r', 'skf_cheek_l', 'skf_cheek_r',
            'skf_cheek_s_l', 'skf_cheek_s_r', 'skf_lip_t_l', 'skf_lip_t_c', 'skf_lip_t_r', 'skf_lip_s_l', 'skf_lip_s_r']
ALLB = BONES + ['hL:' + n for n in HAND_ASK] + ['hR:' + n for n in HAND_ASK] + ['f:' + n for n in FACE_ASK]
NB = len(ALLB)
BI = {b: i for i, b in enumerate(ALLB)}

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
    fixed['dsk_hip'] = 'pelvis'
    if n in fixed: return [(fixed[n], 1)]
    if n.startswith('skf_'): return [('f:' + n, 1)] if n in FACE_ASK else [('head', 1)]
    if n.startswith('skh_'):
        k = 'h%s:%s_l' % (s, n[:-2]) if n[-2:] in ('_l', '_r') else None
        return [(k, 1)] if k in BI else [('hand' + s, 1)]
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
                   ('belly', [('spine', 1)]), ('hem', [('thigh' + P, 1)]), ('pants', [('thigh' + P, .7), ('pelvis', .3)]),
                   ('hip', [('thigh' + P, .5), ('pelvis', .5)])):
        if key in n: return d
    return [('chest', 1)]

def pieza(path, mat_ok=None):
    f = Fmdl(path); out = []
    for mi, m in enumerate(meshes(f)):
        if m['pos'] is None or m['faces'] is None: continue
        if mat_ok and not mat_ok(mi, m): continue
        nv = m['nvert']; W = np.zeros((nv, NB), np.float32)
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

# ---------------------------------------------------------------- ajuste de contactos por altura (lo mismo que ContactIK de motorpj.cpp)
def leer_inc(path):
    """motorpista.inc -> {letra: dict(segs=[(mano, ancla, f0, n, off)], c=[(peso, x, y, z)], torso)}, distancia de la pareja"""
    import re
    t = open(path).read(); out = {}
    for L in ('A', 'B'):
        sg = re.search(r'kOwnS_%s\[\] = \{(.*?)\};' % L, t, re.S).group(1)
        segs = [tuple(int(x) for x in m) for m in re.findall(r'\{ (-?\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+) \}', sg)]
        cs = re.search(r'kOwnC_%s\[\] = \{(.*?)\};' % L, t, re.S).group(1)
        v = [float(x) for x in re.findall(r'(-?\d+\.\d+)f', cs)]
        tor = float(re.search(r'kOwnC_%s, ([\d.]+)f' % L, t).group(1))
        out[L] = dict(segs=segs, c=[v[k:k + 4] for k in range(0, len(v), 4)], torso=tor)
    D = float(re.search(r'kOwnPairs\[\] = \{ \{ "[^"]+", "[^"]+", ([\d.]+)f', t).group(1))
    return out, D

def al_mundo(P, esc, quien, D):
    """fk (modelo) -> mundo: escala del jugador y B delante de A girado 180 grados"""
    qy = (0.0, 1.0, 0.0, 0.0); W = {}
    for b, (p, q) in P.items():
        p = tuple(x * esc for x in p)
        if quien == 1: p = (-p[0], p[1], D - p[2]); q = G.qm(qy, q)
        W[b] = [np.array(p, float), np.array(q, float)]
    return W

def _hijos(b):
    out = [b]
    for c in BONES:
        if PAR.get(c) in out and c not in out: out.append(c)
    return out
HIJOS = {}
def rot_sub(W, b, R, pv):
    """gira el hueso b y todo lo que cuelga de el alrededor del punto pv (como RotSub)"""
    if b not in HIJOS: HIJOS[b] = _hijos(b)
    for c in HIJOS[b]:
        W[c][1] = np.array(G.qm(tuple(R), tuple(W[c][1]))); W[c][0] = pv + np.array(G.qrot(tuple(R), tuple(W[c][0] - pv)))

def q_axis(ax, ang):
    h = 0.5 * ang; s_ = math.sin(h); return np.array([ax[0] * s_, ax[1] * s_, ax[2] * s_, math.cos(h)])
def q_from_to_np(a, b):
    a = a / max(np.linalg.norm(a), 1e-9); b = b / max(np.linalg.norm(b), 1e-9); c = np.cross(a, b); d = float(np.dot(a, b))
    if d < -0.9999:
        t = np.array([0, 1, 0]) if abs(a[0]) > 0.9 else np.array([1, 0, 0]); c = np.cross(a, t); c /= np.linalg.norm(c); return np.array([c[0], c[1], c[2], 0.0])
    q = np.array([c[0], c[1], c[2], 1 + d]); return q / np.linalg.norm(q)

def ik2(W, u, f, h, T, upv):
    S_, E, Wr = W[u][0].copy(), W[f][0].copy(), W[h][0].copy()
    uv = E - S_; vv = Wr - E; st = T - S_
    a = np.linalg.norm(uv); b = np.linalg.norm(vv)
    if a < 1e-3 or b < 1e-3: return
    dT = min(max(np.linalg.norm(st), abs(a - b) + 0.01), (a + b) * 0.995)
    nx = np.cross(uv, vv)
    if np.linalg.norm(nx) < 1e-6:
        back = np.cross(uv, upv); nx = np.cross(back, uv)
        if np.linalg.norm(nx) < 1e-6: return
    nx /= np.linalg.norm(nx)
    beta = math.acos(max(-1, min(1, np.dot(uv, vv) / (a * b))))
    beta2 = math.pi - math.acos(max(-1, min(1, (a * a + b * b - dT * dT) / (2 * a * b))))
    rot_sub(W, f, q_axis(nx, beta2 - beta), E)
    sw = W[h][0] - S_
    rot_sub(W, u, q_from_to_np(sw, st), S_)

MANO = {0: ('uarmL', 'farmL', 'handL'), 1: ('uarmR', 'farmR', 'handR')}
def puntas_con_peso(T, previa=16, hondo=0.65, periodo=15.0, z=0.5, piso=-0.05):
    """(18/11) curva de las puntas de pie con los principios de animacion. ANTICIPACION clara: antes de cada subida el cuerpo baja
    (rodillas) durante `previa` cuadros hasta `hondo` x la altura que viene (con 5.6 cm de subida baja ~3.6 cm), y de ahi sale el
    impulso; OVERSHOOT y SETTLE (resorte: se pasa un poco y se asienta) y PESO al bajar (cae y las rodillas absorben)"""
    n = len(T); G = list(T)
    f = 1
    while f < n:
        if T[f - 1] < 0.004 <= T[f]:                         # empieza una subida
            pico = max(T[f:min(n, f + 30)]); a = max(0, f - previa)
            for g in range(a, f + 4):
                u = (g - a) / float(f + 4 - a); G[g] = min(G[g], -hondo * pico * math.sin(math.pi * u) ** 1.5)
            f += 30
        else: f += 1
    x = 0.0; v = 0.0; w = 2 * math.pi / periodo; out = []
    for f in range(n):
        a = w * w * (G[f] - x) - 2 * z * w * v; v += a; x += v; out.append(max(piso, x))
    return out

def _sube_baja(W, lift, up, sc=1.0):
    """lift > 0: puntas de pie (todo sube, cada pie gira sobre la punta); lift < 0: se agacha (rodillas se doblan, pies quedos)"""
    if lift < -0.001:
        fijo = {ft: (W[ft][0].copy(), W[ft][1].copy()) for ft in ('footA', 'footB')}
        for b in BONES: W[b][0] = W[b][0] + up * lift
        fw = np.cross(W['thighA'][0] - W['thighB'][0], up); fw /= max(np.linalg.norm(fw), 1e-6)
        for th_, kn_, ft in (('thighA', 'kneeA', 'footA'), ('thighB', 'kneeB', 'footB')):
            ik2(W, th_, kn_, ft, fijo[ft][0], fw); W[ft][1] = fijo[ft][1]
    if lift > 0.001:
        for b in BONES: W[b][0] = W[b][0] + up * lift
        xm = W['thighA'][0] - W['thighB'][0]; xm = xm - up * np.dot(xm, up)
        if np.linalg.norm(xm) > 1e-6:
            xm /= np.linalg.norm(xm); ha = 0.08 * sc; tz = 0.14 * sc; Rr = math.hypot(ha, tz); ph = math.atan2(tz, ha)
            th = max(0.0, ph - math.acos(min(1.0, (ha + lift) / Rr)))
            for ft in ('footA', 'footB'): rot_sub(W, ft, q_axis(xm, th), W[ft][0].copy())

def suave_con_peso(T, adelanto=8, periodo=26.0, z=0.75):
    """curva del agacharse del alto: empieza `adelanto` cuadros antes (lidera el movimiento) y llega con un resorte casi sin rebote"""
    n = len(T); x = 0.0; v = 0.0; w = 2 * math.pi / periodo; out = []
    for f in range(n):
        g = max(T[f], T[min(n - 1, f + adelanto)]); a = w * w * (g - x) - 2 * z * w * v; v += a; x += v; out.append(max(0.0, x))
    return out

def contacto_ik(Ws, inc, letras, alturas, f, estado):
    """Ws = [mundo A, mundo B] del cuadro f (se cambian en el lugar). Igual que ContactIK: cada uno mira lo del otro ANTES de corregir"""
    up = np.array([0.0, 1.0, 0.0]); sn = []
    # (18/11 01:18, Agustin: "el jugador alto deberia doblarse para abrazar") el mas alto se agacha (rodillas) y se inclina hacia el
    # otro mientras dura el abrazo (contactos anclados al pecho), segun la diferencia de estatura: 0.4 cm y 0.6 grados por cm de
    # diferencia, hasta 6 cm y 6 grados (0.4 por cm). Se hace ANTES de las fotos: el otro ya lo ve agachado
    for i in (0, 1):
        W = Ws[i]; tr = inc[letras[i]]; st = estado[i]; cw = 0.0
        for (hand, anchor, f0, n, off) in tr['segs']:
            if anchor == 2 and 0 <= f - f0 <= n - 1: cw = max(cw, tr['c'][off + f - f0][0])
        dif = alturas[i] - alturas[1 - i]
        st['agacha_crudo'] = cw * max(0.0, dif)
        ag = st['agacha_forz'] if st.get('agacha_forz') is not None else st['agacha_crudo']       # cm de diferencia x peso, suavizado
        baja = min(0.06, 0.004 * ag); incl = math.radians(min(6.0, 0.4 * ag))
        st['pre'] = (st.get('forz') or 0.0) - baja if st.get('forz') is not None else None
        if incl > 1e-4:
            X = W['thighA'][0] - W['thighB'][0]; X[1] = 0; X /= max(np.linalg.norm(X), 1e-6)
            rot_sub(W, 'spine', q_axis(X, incl), W['spine'][0].copy())
        if st['pre'] is not None: _sube_baja(W, st['pre'], up)
    for i in (0, 1):
        W = Ws[i]; X = W['thighA'][0] - W['thighB'][0]; X[1] = 0; X /= max(np.linalg.norm(X), 1e-6)
        mid = 0.5 * (W['thighA'][0] + W['thighB'][0]); sc = np.linalg.norm(W['head'][0] - mid) / inc[letras[i]]['torso']
        sn.append(dict(hand=[W['handL'][0].copy(), W['handR'][0].copy()], chest=W['chest'][0].copy(), X=X, sc=min(max(sc, 0.6), 1.5)))
    for i in (0, 1):
        W = Ws[i]; o = sn[1 - i]; me = sn[i]; tr = inc[letras[i]]; Z = np.cross(o['X'], up)
        goal = {}; wc = {}; acc = {}
        for (hand, anchor, f0, n, off) in tr['segs']:
            kf = f - f0
            if kf < 0 or kf > n - 1: continue
            c = tr['c'][off + kf]; w = c[0]
            if w <= 0.001: continue
            A = o['hand'][anchor] if anchor < 2 else o['chest']
            sc = o['sc'] if anchor >= 2 else 0.5 * (o['sc'] + me['sc'])
            Tw = A + sc * (c[1] * o['X'] + c[2] * up + c[3] * Z)
            e = Tw - me['hand'][hand]
            if anchor < 2: e *= 0.5
            if np.linalg.norm(e) > 0.40: continue
            a_ = acc.setdefault(hand, [0.0, np.zeros(3), 0.0]); a_[0] += w; a_[1] += w * e
            if anchor != 3: a_[2] += w          # ancla 3: solo la mano (no cuenta para las puntas de pie)
        for hand, (ws, es, wl) in acc.items():   # dos tramos seguidos de la misma mano se funden (antes mandaba el primero)
            goal[hand] = me['hand'][hand] + es / max(1.0, ws); wc[hand] = min(1.0, wl)
        diff = alturas[1 - i] - alturas[i]
        shMax = min(max((diff - 3.0) / 8.0, 0), 1) * 0.2443; tipMax = min(max((diff - 5.0) / 10.0, 0), 1) * 0.06 * me['sc']
        shW = [0.0, 0.0]; liftW = 0.0
        for k, g in goal.items():
            u, fo, h = MANO[k]; Sw, Ew, Hw = W[u][0], W[fo][0], W[h][0]
            L = (np.linalg.norm(Ew - Sw) + np.linalg.norm(Hw - Ew)) * 0.97
            v = g - Sw; over = np.linalg.norm(v) - L
            # (18/11, Agustin: "no veo que messi se ponga de puntitas de pie") el mas bajo sube a las puntas cuando la mano va a la
            # altura del hombro o mas arriba, gradual con la diferencia de estatura y con lo alto que va la mano (no solo si no llega)
            liftW = max(liftW, tipMax * wc[k] * min(max((v[1] + 0.05) / 0.20, 0), 1))
            if over <= 0 or v[1] <= 0: continue
            over *= v[1] / max(np.linalg.norm(v), 1e-3)
            cl = PAR[u]; rc = max(0.05, np.linalg.norm(Sw - W[cl][0]))
            a = min(shMax, math.asin(min(1.0, over / rc))); shW[k] = a; over -= rc * math.sin(a)
            if over > 0: liftW = max(liftW, min(tipMax, over))
        st = estado[i]; kk = min(max((1 / 30.0) * 6.0, 0), 1)
        st['crudo'] = liftW
        if st.get('forz') is not None: st['lift'] = st['forz']      # curva con anticipacion y peso: ya aplicada arriba (st['pre'])
        else:
            st['lift'] += (liftW - st['lift']) * kk; _sube_baja(W, st['lift'], up)
        for k in (0, 1): st['shr'][k] += (shW[k] - st['shr'][k]) * kk
        for k in (0, 1):
            if st['shr'][k] < 0.002: continue
            u = MANO[k][0]; cl = PAR[u]; d_ = W[u][0] - W[cl][0]; ax = np.cross(d_, up)
            if np.linalg.norm(ax) < 1e-6: continue
            rot_sub(W, cl, q_axis(ax / np.linalg.norm(ax), st['shr'][k]), W[cl][0].copy())
        for k, g in goal.items(): ik2(W, *MANO[k], g, up)
        estado[i]['log'] = (len(goal), shW, liftW)

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

def piel(m, mats, esc=None, quien=None, D=None):
    """vertices y normales de la malla m en el mundo (mats = NB x 12 del cuadro, ya en el mundo)"""
    M = mats.reshape(NB, 4, 3)                       # columnas: R0 R1 R2 t
    bi = m['bi'].astype(int); bw = m['bw']
    R = np.einsum('vk,vkcr->vrc', bw, M[bi, :3, :]); t = np.einsum('vk,vkr->vr', bw, M[bi, 3, :])
    p = np.einsum('vrc,vc->vr', R, m['pos']) + t; n = np.einsum('vrc,vc->vr', R, m['nrm'])
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
            ps, ns = zip(*[piel(m, jug[i]['frames'][f]) for m in ms]); PN.append((np.concatenate(ps), np.concatenate(ns)))
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

# ---------------------------------------------------------------- clips de cara y de mano del juego (Face / Hand de dt13_all.cpk)
import gani_cara as GC
class Clip:
    """{hueso del .ask: (claves de giro [(f, q)], claves de lugar [(f, p en m)])}; muestra(t en s) con vuelta al principio"""
    def __init__(self, path, ask, frig):
        g = open(path, 'rb').read(); Gn = GC.leer_gani(g); self.n = max(1, Gn['cuadros'])
        cm = GC.mapa_canales(frig['unidades'], Gn['canales']); self.b = {}
        for c in Gn['canales']:
            if c['indice'] not in cm: continue
            k_, bi = cm[c['indice']]
            if bi < 0: continue
            data = g[c['off']:c['off'] + c['size']]; e = self.b.setdefault(ask[bi], [[], []])
            if c['bits'] == 16: e[1] = [(f, np.array(v) / 100.0) for f, v in GC.claves_pos(data)]
            else:
                prev = None
                for f, v, sg in GC.claves_rot(data, c['bits']):
                    q = np.array(GC.quat(v, sg))
                    if prev is not None and np.dot(prev, q) < 0: q = -q
                    prev = q; e[0].append((f, q))
    def muestra(self, t, loop=True):
        fr = t * 30.0; fr = fr % self.n if loop else min(fr, self.n)
        out = {}
        for b, (R, Pp) in self.b.items():
            q = _interp(R, fr, True) if R else np.array([0, 0, 0, 1.0]); p = _interp(Pp, fr, False) if Pp else np.zeros(3)
            out[b] = (q, p)
        return out
def _interp(K, fr, rot):
    if fr <= K[0][0] or len(K) == 1: return K[0][1]
    for (f0, a), (f1, b) in zip(K, K[1:]):
        if fr <= f1:
            t = (fr - f0) / float(max(1, f1 - f0))
            return np.array(G.qslerp(tuple(a), tuple(b), t)) if rot else a + (b - a) * t
    return K[-1][1]
def mezcla(capas):
    """[(peso, {hueso: (q, p)})] -> {hueso: (q, p)}: suma de giros con peso (desde identidad) y de lugares"""
    out = {}; names = set()
    for w, d in capas: names |= set(d)
    for b in names:
        q = np.zeros(4); p = np.zeros(3); wt = 0.0
        for w, d in capas:
            if w <= 0: continue
            qq, pp = d.get(b, (np.array([0, 0, 0, 1.0]), np.zeros(3)))
            if np.dot(qq, [0, 0, 0, 1.0]) < 0: qq = -qq
            q += w * qq; p += w * pp; wt += w
        if wt < 1.0: q += (1.0 - wt) * np.array([0, 0, 0, 1.0])
        out[b] = (q / max(np.linalg.norm(q), 1e-9), p)
    return out

def kabsch(X, Y):
    """R con Y ~ R X (filas = puntos)"""
    H = np.array(X).T @ np.array(Y); U, S_, Vt = np.linalg.svd(H); d = np.sign(np.linalg.det(Vt.T @ U.T))
    return Vt.T @ np.diag([1, 1, d]) @ U.T

R_FACE = np.array([[0, 0, -1], [1, 0, 0], [0, -1, 0]], float)     # cara (x arriba, y atras, z derecha) -> ejes del modelo
def mat_q(R):
    return np.array(G.qmat(tuple(R[:, 0]), tuple(R[:, 1]), tuple(R[:, 2])))

class Rig:
    """dedos y cara: cadenas del .ask, giro de reposo en la pose de las mallas y lugares de reposo"""
    def __init__(self, cm, base, face_fmdl):
        H = os.path.join(cm, 'Hand'); F = os.path.join(cm, 'Face')
        ha = GC.leer_ask(open(os.path.join(H, 'CharacterAssets', 'hand_anim_skel.ask'), 'rb').read())['huesos']
        self.hask = [b['nombre'] for b in ha]; self.hpar = [b['padre'] for b in ha]; self.hoff = [np.array(b['pos']) for b in ha]
        self.hfrig = GC.leer_frig(open(os.path.join(H, 'CharacterAssets', 'pes_human_hand_141203.frig'), 'rb').read())
        fa = GC.leer_ask(open(os.path.join(F, 'CharacterAssets', 'face_anim_skel.ask'), 'rb').read())['huesos']
        self.fask = [b['nombre'] for b in fa]; self.fpar = [b['padre'] for b in fa]
        self.ffrig = GC.leer_frig(open(os.path.join(F, 'CharacterAssets', 'face_skel.frig'), 'rb').read())
        self.dir = cm; self.cache = {}
        # mano: lugares acumulados en el marco de la mano (.ask) contra los del fmdl (ejes del modelo, pose de las mallas)
        def acum(off, par):
            out = [np.zeros(3)] * len(off)
            for i in range(len(off)): out[i] = off[i] + (out[par[i]] if par[i] >= 0 else 0)
            return out
        self.hcum = acum(self.hoff, self.hpar)
        self.Rh = {}
        for s_, fn in (('L', 'naked_hand_l.fmdl'), ('R', 'naked_hand_r.fmdl')):
            f = Fmdl(os.path.join(base, fn)); loc = {b['name']: np.array(b['local'][:3]) for b in f.bones}
            par = {b['name']: (f.bones[b['parent']]['name'] if b['parent'] >= 0 else None) for b in f.bones}
            Y = {}; X = []; Yl = []
            for i, n in enumerate(self.hask):
                if i == 0: continue
                nn = n[:-2] + ('_l' if s_ == 'L' else '_r')
                if nn not in loc: continue
                y = loc[nn]; q = par[nn]
                while q and q.startswith('skh_'): y = y + loc[q]; q = par[q]
                X.append(self.hcum[i]); Yl.append(y)
            best = None
            for mir in ((1, 1, 1), (1, -1, 1), (1, 1, -1), (-1, 1, 1)) if s_ == 'R' else ((1, 1, 1),):
                Xm = [np.array(mir) * x for x in X]; R = kabsch(Xm, Yl); err = sum(np.linalg.norm(R @ a - b) for a, b in zip(Xm, Yl))
                if best is None or err < best[0]: best = (err, R, mir)
            self.Rh[s_] = (best[1], np.array(best[2], float)); print('mano %s: marco del juego ajustado, error medio %.1f mm (espejo %s)' % (s_, best[0] / len(X) * 1000, best[2]))
        self.face_loc = {}
    def cara_locales(self, face_fmdl):
        if face_fmdl in self.face_loc: return self.face_loc[face_fmdl]
        f = Fmdl(face_fmdl); loc = {b['name']: np.array(b['local'][:3]) for b in f.bones}
        abs_ = {}
        for i, n in enumerate(self.fask):
            abs_[n] = loc.get(n, None)
        # lugares relativos al padre (en el fmdl son relativos a la cabeza)
        rel = {}
        for i, n in enumerate(self.fask):
            if i == 0: rel[n] = np.zeros(3); continue
            a = abs_[n] if abs_[n] is not None else None
            pn = self.fask[self.fpar[i]]
            if a is None:   # sin dato en el fmdl: el del .ask
                rel[n] = np.array(GC.leer_ask(open(os.path.join(self.dir, 'Face', 'CharacterAssets', 'face_anim_skel.ask'), 'rb').read())['huesos'][i]['pos'])
            else:
                pa = abs_.get(pn) if self.fpar[i] > 0 else np.zeros(3)
                rel[n] = a - (pa if pa is not None else 0)
        self.face_loc[face_fmdl] = (abs_, rel); return abs_, rel
    def clip(self, kind, name):
        k = (kind, name)
        if k not in self.cache:
            if kind == 'hand':
                p = os.path.join(self.dir, 'Hand', 'Animations', name + '.gani'); self.cache[k] = Clip(p, self.hask, self.hfrig) if os.path.exists(p) else None
            else:
                p = None
                for sub in ('Base', 'Add', ''):
                    q = os.path.join(self.dir, 'Face', 'Animations', sub, name + '.gani')
                    if os.path.exists(q): p = q; break
                if p is None and os.path.exists('/mnt/project-files/face/gani/%s.gani' % name): p = '/mnt/project-files/face/gani/%s.gani' % name
                self.cache[k] = Clip(p, self.fask, self.ffrig) if p else None
                if p is None: print('falta el gesto', name)
        return self.cache[k]

def leer_pista(path):
    import re
    t = open(path).read(); out = {}
    for L in ('A', 'B'):
        blk = re.search(r'kOwn_%s\[\] = \{(.*?)\};' % L, t, re.S).group(1)
        out[L] = [(int(a), b, float(c), d, e) for a, b, c, d, e in re.findall(r'\{ (\d+), "([^"]*)", ([\d.]+)f, "([^"]*)", "([^"]*)" \}', blk)]
    return out

def capas_pista(keys, fr):
    """como OwnFace: clave actual y anterior con fundido de 4-11 cuadros; cada gesto sigue su tiempo desde que empezo"""
    i = 0
    while i + 1 < len(keys) and keys[i + 1][0] <= fr: i += 1
    k = keys[i]; q = keys[i - 1] if i > 0 else None
    fade = min(max(k[0] - q[0], 4), 11) if q else 1
    a = min(max((fr - k[0]) / fade, 0), 1) if q else 1.0; a = a * a * (3 - 2 * a)
    def desde(j):
        while j > 0 and keys[j - 1][1] == keys[j][1]: j -= 1
        return keys[j][0]
    return k, q, a, desde(i), (desde(i - 1) if q else 0)

def mundo_extra(rig, Pw, esc, keys, fr, face_fmdl, seed):
    """lugar y giro en el mundo de los dedos y de la cara en el cuadro fr (Pw = cuerpo en el mundo)"""
    out = {}
    k, q, a, f0, fq = capas_pista(keys, fr) if keys else (None, None, 1, 0, 0)
    # manos
    for s_, idx in (('L', 3), ('R', 4)):
        Rb, mir = rig.Rh[s_]
        cur = rig.clip('hand', k[idx]) if k else None; prv = rig.clip('hand', q[idx]) if q else None
        lay = []
        if cur: lay.append((a if prv else 1.0, cur.muestra(0)))
        if prv: lay.append((1 - a, prv.muestra(0)))
        pose = mezcla(lay) if lay else {}
        hb = 'hand' + s_; Rf = qmat3(RB[hb]).T @ Rb; Qh = G.qm(tuple(Pw[hb][1]), tuple(mat_q(Rf)))
        Qw = [None] * len(rig.hask); Pp = [None] * len(rig.hask)
        for i, n in enumerate(rig.hask):
            ql = pose.get(n, (np.array([0, 0, 0, 1.0]), None))[0]
            if np.prod(mir) < 0: ql = np.array([-mir[0] * ql[0], -mir[1] * ql[1], -mir[2] * ql[2], ql[3]])   # giro espejado
            if i == 0: Qw[i] = np.array(Qh); Pp[i] = Pw[hb][0]; continue
            pi = rig.hpar[i]; off = mir * rig.hoff[i] * esc
            Pp[i] = Pp[pi] + np.array(G.qrot(tuple(Qw[pi]), tuple(off))); Qw[i] = np.array(G.qm(tuple(Qw[pi]), tuple(ql)))
            out['h%s:%s' % (s_, n)] = (Pp[i], Qw[i])
    # cara
    abs_, rel = rig.cara_locales(face_fmdl)
    lay = []
    if k:
        g = rig.clip('face', k[1]); gq = rig.clip('face', q[1]) if q else None
        w0 = a * min(max(k[2], 0), 1) if g else 0.0; w1 = (1 - a) * min(max(q[2], 0), 1) if gq else 0.0
        if g: lay.append((w0, g.muestra((fr - f0) / 30.0)))
        if gq and w1 > 0.001: lay.append((w1, gq.muestra((fr - fq) / 30.0)))
        wr = max(0.0, 1.0 - w0 - w1); bn = rig.clip('face', 'base_normal')
        if bn and wr > 0.001: lay.append((wr, bn.muestra(fr / 30.0)))
    pose = mezcla(lay) if lay else {}
    per = 3.2 + (seed % 7) * 0.15; ph = (fr / 30.0 + (seed % 97) * 0.031) % per
    bl = rig.clip('face', 'add_eyeblink_single') if ph < 0.26 else None
    add = bl.muestra(ph, loop=False) if bl else {}
    Qf = G.qm(tuple(Pw['head'][1]), tuple(mat_q(R_FACE)))
    Qw = {rig.fask[0]: np.array(Qf)}; Pp = {rig.fask[0]: Pw['head'][0]}
    for i, n in enumerate(rig.fask):
        if i == 0 or n not in FACE_ASK: continue
        pn = rig.fask[rig.fpar[i]]
        ql, pl = pose.get(n, (np.array([0, 0, 0, 1.0]), np.zeros(3)))
        if n in add: ql = np.array(G.qm(tuple(ql), tuple(add[n][0]))); pl = pl + add[n][1]
        off = (rel[n] + pl) * esc
        Pp[n] = Pp[pn] + np.array(G.qrot(tuple(Qw[pn]), tuple(off))); Qw[n] = np.array(G.qm(tuple(Qw[pn]), tuple(ql)))
        out['f:' + n] = (Pp[n], Qw[n])
    return out

def bind_extra(rig, face_fmdl):
    """lugar y giro de reposo (pose de las mallas) de dedos y cara"""
    out = {}
    for s_ in ('L', 'R'):
        Rb, mir = rig.Rh[s_]; hb = BIND['hand' + s_]
        for i, n in enumerate(rig.hask):
            if i == 0: continue
            out['h%s:%s' % (s_, n)] = (np.array(hb) + Rb @ (mir * rig.hcum[i]), Rb)
    abs_, rel = rig.cara_locales(face_fmdl)
    acc = {rig.fask[0]: np.zeros(3)}
    for i, n in enumerate(rig.fask):
        if i == 0 or n not in FACE_ASK: continue
        acc[n] = acc[rig.fask[rig.fpar[i]]] + rel[n]
        out['f:' + n] = (np.array(BIND['head']) + R_FACE @ acc[n], R_FACE)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a'); ap.add_argument('b'); ap.add_argument('base'); ap.add_argument('caras'); ap.add_argument('salida')
    ap.add_argument('--caras_id', default='7511,4522'); ap.add_argument('--estaturas', default='170,185'); ap.add_argument('--D', type=float, default=5.74)
    ap.add_argument('--revisar', action='store_true'); ap.add_argument('--botas', default='')
    ap.add_argument('--cuello', default='', help='collar_NNN.fmdl (uniform/nocloth/#Win)')
    ap.add_argument('--cm', default='', help='carpeta con Face/ y Hand/ (caras_manos.zip)'); ap.add_argument('--inc', default='', help='motorpista.inc del generador')
    ap.add_argument('--sin_ajuste', action='store_true', help='sin el ajuste de contactos por altura (ContactIK)')
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
    if o.cuello: piezas_cuerpo.append(('shirt', o.cuello))
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
    rig = Rig(o.cm, B, None) if o.cm else None
    inc, Dinc = leer_inc(o.inc) if o.inc else (None, None)
    pista = leer_pista(o.inc) if o.inc else None
    Dm = (Dinc if Dinc else o.D) * 0.5 * (esc[0] + esc[1])
    jug = []; gs = []
    for i in (0, 1):
        g = G.Base(open([o.a, o.b][i], 'rb').read()); gs.append(g)
        cid = ids[i]; tx = {}
        for t in ('face_bsm_alp', 'hair_parts_bsm_alp'):
            for d in (os.path.join(B, cid, 'cpk', 'sourceimages', '#windx11'),):
                fp = os.path.join(d, t + '.ftex')
                if os.path.exists(fp): tx[t] = textura(fp, os.path.join(outdir, '%s_%s.png' % (cid, t)))
        ff = os.path.join(o.caras, cid + '_face_high.fmdl')
        cara = [dict(m, kind='face' if m['mat'] in (0, 2) else 'eye') for m in pieza(ff)]
        pelo = [dict(m, kind='face' if m['mat'] == 0 else 'hair') for m in pieza(os.path.join(o.caras, cid + '_hair_high.fmdl'))]
        jug.append(dict(escala=esc[i], estatura=est[i], color=cols[i], tex=tx, cara=cara + pelo, n=g.n, ff=ff,
                        bx=bind_extra(rig, ff) if rig else {}, frames=[]))
    estado = [dict(lift=0.0, shr=[0.0, 0.0]) for _ in (0, 1)]; reg = []
    nf = min(gs[0].n, gs[1].n) + 1; curva = None
    if inc and not o.sin_ajuste:      # primera pasada: cuanto pide subir cada uno en cada cuadro; despues la curva con peso
        crudo = [[], []]; agc = [[], []]
        for f in range(nf):
            Ws = [al_mundo(fk(gs[i].rot[f], gs[i].root[f], gs[i].hips[f]), esc[i], i, Dm) for i in (0, 1)]
            contacto_ik(Ws, inc, 'AB', est, f, estado)
            for i in (0, 1): crudo[i].append(estado[i]['crudo']); agc[i].append(estado[i]['agacha_crudo'])
        curva = [puntas_con_peso(c) for c in crudo]; curva_ag = [suave_con_peso(c) for c in agc]
        estado = [dict(lift=0.0, shr=[0.0, 0.0]) for _ in (0, 1)]
        print('puntas de pie (cm, max / min con la bajada de anticipacion y peso): A %.1f / %.1f, B %.1f / %.1f' % (
            max(curva[0]) * 100, min(curva[0]) * 100, max(curva[1]) * 100, min(curva[1]) * 100))
    for f in range(nf):
        if curva:
            for i in (0, 1): estado[i]['forz'] = curva[i][f]; estado[i]['agacha_forz'] = curva_ag[i][f]
        Ws = [al_mundo(fk(gs[i].rot[f], gs[i].root[f], gs[i].hips[f]), esc[i], i, Dm) for i in (0, 1)]
        if inc and not o.sin_ajuste:
            contacto_ik(Ws, inc, 'AB', est, f, estado)
            if estado[0]['log'][0] or estado[1]['log'][0]: reg.append((f, [round(math.degrees(x)) for x in estado[0]['shr'] + estado[1]['shr']], round(estado[0]['lift'] * 100, 1), round(estado[1]['lift'] * 100, 1)))
        for i in (0, 1):
            J = jug[i]; W = Ws[i]; mats = np.zeros((NB, 12), np.float32)
            ex = mundo_extra(rig, W, esc[i], pista['AB'[i]] if pista else None, f, J['ff'], i * 5 + 3) if rig else {}
            for b in ALLB:
                if b in W: pp, pq = W[b]; Rb_ = qmat3(RB[b]); bp = np.array(BIND[b])
                elif b in ex: pp, pq = ex[b]; bp, Rb_ = J['bx'][b]
                else: continue
                M = esc[i] * qmat3(tuple(pq)) @ np.asarray(Rb_).T; t = np.asarray(pp) - M @ bp
                mats[BI[b]] = np.concatenate([M, t[:, None]], 1).T.reshape(-1)
            J['frames'].append(mats.reshape(-1))
    for J in jug: J['frames'] = np.array(J['frames'], np.float32); J['n'] = len(J['frames']) - 1
    if reg:
        print('ajuste por altura (hombros izq/der A, izq/der B en grados; puntas A / B en cm), algunos cuadros:')
        for r in reg[::max(1, len(reg) // 12)]: print('  ', r)
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
    J = dict(nb=NB, texc=texc, huesos=BONES, bind=[list(BIND[b]) for b in BONES], D=Dm, cuerpo=enc(cuerpo),
             jugadores=[dict(escala=j['escala'], estatura=j['estatura'], color=j['color'], tex=j['tex'], cara=enc(j['cara']),
                             n=j['n'], frames=b64(j['frames'])) for j in jug])
    json.dump(J, open(o.salida, 'w'))
    print('escena', o.salida, os.path.getsize(o.salida), 'B; cuadros', [j['n'] for j in jug], 'escalas', esc, 'D', Dm)

if __name__ == '__main__':
    main()
