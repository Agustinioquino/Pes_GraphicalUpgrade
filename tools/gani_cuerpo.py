# PES21 FX (17/11, Agustin: "una animacion de jugadores chocando las manos, saludandose y abrazandose, dos ganis para que cada jugador
# tenga una de las dos"; "que no se despeguen del piso") - ESCRITOR de .gani de CUERPO para FixDemoHuman (clips de cinematica del juego).
#
# Formato: el mismo contenedor que la cara (gani_cara.py / gani_escribir.py): 15 grupos, 27 canales, 30 cuadros por segundo, giros de
# 12 bits. Canales (deducidos de los clips de Konami, ver memoria motor-gani-saludo-abrazo-pedido-20261117):
#   0 raiz giro, 1 raiz lugar (cm), 2 cadera giro, 3 cadera lugar (cm), 4 fijo, 5/9/18/23 fijos de 16 bits (no son half: se copian),
#   6-8 pierna izquierda (muslo, rodilla solo x, pie), 10-12 pierna derecha, 13 columna, 14 pecho, 15 cuello, 16 cabeza,
#   17 clavicula izq, 19 brazo, 20 antebrazo (solo y), 21 mano; 22 clavicula der, 24 brazo, 25 antebrazo (solo y), 26 mano.
#   Ejes del modelo: x = izquierda del jugador, y = arriba, z = adelante. Reposo en T con los ejes de todos los huesos iguales a los del
#   modelo: cada cuaternion es el giro LOCAL contra el padre. Palma en reposo = -y (abajo), dedos a lo largo del brazo.
#
# Como arma la animacion:
#   - PIERNAS, CADERA Y RAIZ (canales 0-12): se copian TAL CUAL del clip parado del juego (dml_ent_aisle_cmn_idle01, 300 cuadros), asi
#     los pies quedan donde Konami los puso y no se despegan ni patinan.
#   - Columna / cuello / cabeza / claviculas: el clip parado + giros encima (claves con fundido suave).
#   - Brazos: IK de dos huesos (hombro -> codo -> muneca) hacia un punto, con la direccion del codo y la de la palma; la mano del otro
#     jugador se busca en SU cuerpo (los dos se calculan juntos), asi el choque y el abrazo caen en el mismo lugar en los dos clips.
#   - Esqueleto APROXIMADO (largos medidos contra los clips de Konami: mano parada a la altura de la cadera, cabeza a 1.80 m). En el juego
#     los largos reales pueden variar unos cm.
#
# Uso: gani_cuerpo.py <carpeta con dml_ent_aisle_cmn_idle01.gani> <salida/> [distancia entre jugadores en m, 0 = automatica]
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gani_cara as GC, gani_escribir as GE

# ---------------------------------------------------------------- cuaterniones (x, y, z, w)
def qm(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)
def qi(q): return (-q[0], -q[1], -q[2], q[3])
def qn(q):
    n = math.sqrt(sum(x * x for x in q)) or 1.0
    return tuple(x / n for x in q)
def qrot(q, v):
    x, y, z, w = q; vx, vy, vz = v
    tx = 2 * (y * vz - z * vy); ty = 2 * (z * vx - x * vz); tz = 2 * (x * vy - y * vx)
    return (vx + w * tx + y * tz - z * ty, vy + w * ty + z * tx - x * tz, vz + w * tz + x * ty - y * tx)
def qaxis(ax, deg):
    h = math.radians(deg) / 2; s = math.sin(h); return (ax[0] * s, ax[1] * s, ax[2] * s, math.cos(h))
def qeuler(e):   # grados (x, y, z) -> Ry * Rx * Rz (como PoseApply del Motor)
    return qm(qm(qaxis((0, 1, 0), e[1]), qaxis((1, 0, 0), e[0])), qaxis((0, 0, 1), e[2]))
def qslerp(a, b, t):
    d = sum(x * y for x, y in zip(a, b))
    if d < 0: b = tuple(-x for x in b); d = -d
    if d > 0.9995: return qn(tuple(x + (y - x) * t for x, y in zip(a, b)))
    th = math.acos(d); s = math.sin(th)
    wa = math.sin((1 - t) * th) / s; wb = math.sin(t * th) / s
    return tuple(wa * x + wb * y for x, y in zip(a, b))
def qangle(q): return math.degrees(2 * math.acos(min(1.0, abs(q[3]))))
def qmat(cx, cy, cz):   # columnas = ejes x y z del hueso en el mundo
    m00, m10, m20 = cx; m01, m11, m21 = cy; m02, m12, m22 = cz
    tr = m00 + m11 + m22
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2; return qn(((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s))
    if m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2; return qn((0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s))
    if m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2; return qn(((m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s))
    s = math.sqrt(1.0 + m22 - m00 - m11) * 2; return qn(((m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s))

# ---------------------------------------------------------------- vectores
def add(a, b): return tuple(x + y for x, y in zip(a, b))
def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def mul(a, k): return tuple(x * k for x in a)
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a): return math.sqrt(dot(a, a))
def unit(a):
    n = norm(a) or 1.0; return mul(a, 1.0 / n)
def lerp(a, b, t): return tuple(x + (y - x) * t for x, y in zip(a, b))
def perp(v, axis): return sub(v, mul(axis, dot(v, axis)))
def smooth(t): t = max(0.0, min(1.0, t)); return t * t * (3 - 2 * t)

# ---------------------------------------------------------------- esqueleto aproximado: hueso -> (padre, canal de giro, corrimiento en reposo, m)
UARM, FARM, HANDO = 0.29, 0.26, 0.08          # brazo, antebrazo, muneca -> centro de la palma
S = {
    'root': (None, 0, (0, 0, 0)), 'hips': ('root', 2, (0, 0.98, 0)),
    'thighA': ('hips', 6, (0.09, -0.02, 0)), 'kneeA': ('thighA', 7, (0, -0.44, 0)), 'footA': ('kneeA', 8, (0, -0.44, 0)),
    'thighB': ('hips', 10, (-0.09, -0.02, 0)), 'kneeB': ('thighB', 11, (0, -0.44, 0)), 'footB': ('kneeB', 12, (0, -0.44, 0)),
    'spine': ('hips', 13, (0, 0.10, 0)), 'chest': ('spine', 14, (0, 0.22, 0)), 'neck': ('chest', 15, (0, 0.22, 0)), 'head': ('neck', 16, (0, 0.10, 0)),
    'headc': ('head', None, (0, 0.09, 0.03)),
    'clavL': ('chest', 17, (0.03, 0.16, 0)), 'uarmL': ('clavL', 19, (0.15, 0, 0)), 'farmL': ('uarmL', 20, (UARM, 0, 0)), 'handL': ('farmL', 21, (FARM, 0, 0)),
    'clavR': ('chest', 22, (-0.03, 0.16, 0)), 'uarmR': ('clavR', 24, (-0.15, 0, 0)), 'farmR': ('uarmR', 25, (-UARM, 0, 0)), 'handR': ('farmR', 26, (-FARM, 0, 0)),
}
ORDER = list(S.keys())
ARM = {'R': (22, 24, 25, 26, -1.0), 'L': (17, 19, 20, 21, 1.0)}   # clavicula, brazo, antebrazo, mano, signo del eje del brazo
BONE = {'R': ('clavR', 'uarmR', 'farmR', 'handR'), 'L': ('clavL', 'uarmL', 'farmL', 'handL')}

def fk(rot, rootpos, hipspos):
    out = {}
    for b in ORDER:
        p, ch, off = S[b]
        lq = rot.get(ch, (0, 0, 0, 1)) if ch is not None else (0, 0, 0, 1)
        if p is None: out[b] = (tuple(rootpos), lq); continue
        pp, pq = out[p]
        o = add(off, hipspos) if b == 'hips' else off
        out[b] = (add(pp, qrot(pq, o)), qm(pq, lq))
    return out

# ---------------------------------------------------------------- clip base (parado)
def muestra(keys, f, kind, bs):
    def val(k):
        if kind == 'rot': return tuple(GC.quat([x / float(1 << bs) for x in k[1]], k[2]))
        return tuple(GC.half(h) / 100.0 for h in k[1])
    prev = keys[0]
    for k in keys:
        if k[0] <= f: prev = k; continue
        a = val(prev); b = val(k); t = (f - prev[0]) / float(k[0] - prev[0])
        return qslerp(a, b, t) if kind == 'rot' else lerp(a, b, t)
    return val(prev)

class Base:
    def __init__(self, g):
        self.g = g; self.G, self.cr = GE.leer_crudo(g); self.n = self.G['cuadros']
        self.rot = []; self.root = []; self.hips = []
        for f in range(self.n + 1):
            r = {ix: muestra(keys, f, 'rot', bs) for ix, (k, bs, keys) in self.cr.items() if k == 'rot'}
            self.rot.append(r)
            self.root.append(muestra(self.cr[1][2], f, 'pos', 16)); self.hips.append(muestra(self.cr[3][2], f, 'pos', 16))

# ---------------------------------------------------------------- claves
def pista(keys, f):
    """keys = [(cuadro, valor)], valor = tupla o numero; fundido suave entre claves; antes / despues = la primera / ultima"""
    if f <= keys[0][0]: return keys[0][1]
    for (f0, v0), (f1, v1) in zip(keys, keys[1:]):
        if f <= f1:
            t = smooth((f - f0) / float(f1 - f0))
            if isinstance(v0, (int, float)): return v0 + (v1 - v0) * t
            return lerp(v0, v1, t)
    return keys[-1][1]

class Jugador:
    """giros: {canal: [(cuadro, (x, y, z) grados)]} encima del clip parado; brazos: {'R'|'L': {'w': [(f, peso)], 'k': [(f, objetivo)]}}
    objetivo = dict(t=('yo'|'otro', hueso, (x, y, z) local del hueso) o 'parado', codo=(x,y,z), dedos=(x,y,z), palma=(x,y,z));
    las direcciones del objetivo van en el espacio del mismo hueso del que se cuelga el punto."""
    def __init__(self, nombre, giros, brazos, raiz=None, pies=None):
        self.nombre = nombre; self.giros = giros; self.brazos = brazos
        self.raiz = raiz or [(0, (0.0, 0.0, 0.0))]           # corrimiento de la raiz (m, en mi modelo)
        self.pies = pies or {'A': [(0, 0.0)], 'B': [(0, 0.0)]}   # adelanto de cada pie (m, z); A = izquierdo, B = derecho

def cuerpo(base, J, f):
    rot = dict(base.rot[f])
    for ch, keys in J.giros.items(): rot[ch] = qm(rot[ch], qeuler(pista(keys, f)))
    return rot

def al_otro(p, D):    # punto del modelo del otro jugador (enfrentado, a D metros) -> mi modelo
    return (-p[0], p[1], D - p[2])
def dir_otro(v): return (-v[0], v[1], -v[2])

# ---------------------------------------------------------------- IK del brazo
def _ik(lado, Sh, qc, wrist, pp0, dedos, palma):
    sg = ARM[lado][4]
    d = sub(wrist, Sh); L = norm(d); dn = unit(d)
    L = max(abs(UARM - FARM) + 1e-3, min(L, (UARM + FARM) * 0.999))
    ca = (UARM * UARM + L * L - FARM * FARM) / (2 * UARM * L); sa = math.sqrt(max(0.0, 1 - ca * ca))
    pp = perp(pp0, dn)
    if norm(pp) < 1e-6: pp = perp((0, -1, 0), dn)
    pp = unit(pp)
    E = add(Sh, add(mul(dn, UARM * ca), mul(pp, UARM * sa)))
    W = add(Sh, mul(dn, L))
    d1 = unit(sub(E, Sh)); d2 = unit(sub(W, E))
    phi = math.acos(max(-1.0, min(1.0, dot(d1, d2))))
    zw = perp(d2, d1)
    zw = unit(zw) if norm(zw) > 1e-6 else unit(perp(pp, d1))
    xw = mul(d1, sg)                       # eje x del hueso: brazo derecho apunta a -x en reposo
    yw = cross(zw, xw)
    Qu = qmat(xw, yw, zw)
    th = phi if sg < 0 else -phi           # el codo dobla hacia +z local en los dos lados
    qf = qaxis((0, 1, 0), math.degrees(th))
    Qf = qm(Qu, qf)
    xh = mul(unit(dedos), sg); yh = unit(perp(mul(palma, -1.0), xh)); zh = cross(xh, yh)
    Qh = qmat(xh, yh, zh)
    return qm(qi(qc), Qu), qf, qm(qi(Qf), Qh), E

def giro_y_dobles(q):
    """mano local -> (giro sobre el eje del antebrazo, doblez de la muneca) en grados"""
    if q[3] < 0: q = tuple(-x for x in q)
    tw = math.degrees(2 * math.atan2(q[0], q[3])); twq = qn((q[0], 0, 0, q[3]))
    return tw, qangle(qm(q, qi(twq)))

def ik_brazo(lado, P, rot, wrist, codo, dedos, palma, prev=None):
    """P = fk del cuerpo (con la clavicula ya girada). El codo se gira alrededor del eje hombro -> muneca (hasta 90 grados) para que la
    muneca no tenga que torcerse de mas (en los clips de Konami la mano gira hasta ~75-100 grados); `prev` = el giro elegido en el cuadro
    anterior, para que no salte. Devuelve (q brazo local, q antebrazo local, q mano local, codo, giro del codo elegido)."""
    bc, bu = BONE[lado][0], BONE[lado][1]
    Sh = P[bu][0]; qc = P[bc][1]; dn = unit(sub(wrist, Sh))
    mejor = None
    for o in range(-90, 91, 3):
        pp = qrot(qaxis(dn, o), codo)
        r = _ik(lado, Sh, qc, wrist, pp, dedos, palma)
        tw, sw = giro_y_dobles(r[2])
        c = abs(tw) + 0.6 * sw + 0.4 * abs(o) + (1.5 * abs(o - prev) if prev is not None else 0.0)
        if mejor is None or c < mejor[0]: mejor = (c, o, r)
    return mejor[2] + (mejor[1],)

PIERNA = 0.44
def ik_pierna(pie, P, T, Qpie, ref):
    """pierna de dos huesos (muslo libre, rodilla solo en x como en los clips de Konami) hasta el tobillo T; el plano de la rodilla sale
    del muslo del clip parado (ref = su eje x en el mundo), el pie queda con el giro Qpie del clip parado (plano en el piso)"""
    th = 'thigh' + pie; H = P[th][0]; Qh = P['hips'][1]
    d = sub(T, H); L = max(0.05, min(norm(d), 2 * PIERNA * 0.999)); dn = unit(d)
    x = unit(perp(ref, dn))
    al = math.acos(max(-1.0, min(1.0, L / (2 * PIERNA))))
    d1 = qrot(qaxis(x, -math.degrees(al)), dn)            # la rodilla va adelante
    yw = mul(d1, -1.0); zw = cross(x, yw)
    Qt = qmat(x, yw, zw)
    qk = qaxis((1, 0, 0), math.degrees(2 * al))
    Qs = qm(Qt, qk)
    return qm(qi(Qh), Qt), qk, qm(qi(Qs), Qpie), norm(d) - L

def objetivo_mundo(o, Pyo, Potro, D):
    """punto de la muneca + direcciones en mi modelo"""
    lado_hueso = o['t']
    quien, hueso, loc = lado_hueso
    P = Pyo if quien == 'yo' else Potro
    pos, q = ((0, 0, 0), (0, 0, 0, 1)) if quien == 'mundo' else P[hueso]
    pw = add(pos, qrot(q, loc)); cd = qrot(q, o['codo']); dd = qrot(q, o['dedos']); pd = qrot(q, o['palma'])
    if quien == 'otro': pw = al_otro(pw, D); cd = dir_otro(cd); dd = dir_otro(dd); pd = dir_otro(pd)
    dd = unit(dd); pd = unit(perp(pd, dd))
    return sub(pw, add(mul(dd, HANDO), mul(pd, 0.02))), cd, dd, pd     # muneca = centro de la palma - dedos - grosor

def objetivo_parado(lado, P):
    """muneca, codo, dedos y palma del brazo como esta en el clip parado (para entrar y salir sin salto)"""
    bc, bu, bf, bh = BONE[lado]; sg = ARM[lado][4]
    W, qh = P[bh]; E = P[bf][0]; Sh = P[bu][0]
    return W, sub(E, lerp(Sh, W, 0.5)), mul(qrot(qh, (1, 0, 0)), sg), qrot(qh, (0, -1, 0))

def vslerp(a, b, t):   # direcciones: giro por el arco (con lerp, dos direcciones casi opuestas pasan por cero y la mano salta)
    a = unit(a); b = unit(b); d = max(-1.0, min(1.0, dot(a, b)))
    if d > 0.9995: return unit(lerp(a, b, t))
    th = math.acos(d); s = math.sin(th)
    return unit(add(mul(a, math.sin((1 - t) * th) / s), mul(b, math.sin(t * th) / s)))

def mezcla_obj(a, b, t):
    return (lerp(a[0], b[0], t), vslerp(a[1], b[1], t), vslerp(a[2], b[2], t), vslerp(a[3], b[3], t))

def brazo_en(J, lado, f, Pyo, Potro, D):
    k = J.brazos[lado]['k']
    def res(o): return objetivo_parado(lado, Pyo) if o == 'parado' else objetivo_mundo(o, Pyo, Potro, D)
    if f <= k[0][0]: return res(k[0][1])
    for (f0, o0), (f1, o1) in zip(k, k[1:]):
        if f <= f1: return mezcla_obj(res(o0), res(o1), smooth((f - f0) / float(f1 - f0)))
    return res(k[-1][1])

# ---------------------------------------------------------------- armado de los dos clips
def armar(base, Js, D):
    n = base.n; salida = [dict() for _ in Js]; info = []; prev = {}; raices = [[] for _ in Js]; self_estira = [0.0]
    for f in range(n + 1):
        Pp = fk(base.rot[f], base.root[f], base.hips[f])     # el clip parado: donde estan sus pies
        rots = []; Ps = []
        for i, J in enumerate(Js):
            rp = add(base.root[f], pista(J.raiz, f)); raices[i].append(rp)
            r = cuerpo(base, J, f); P = fk(r, rp, base.hips[f])
            for pie, (ct, ck, cp) in (('A', (6, 7, 8)), ('B', (10, 11, 12))):
                z = pista(J.pies[pie], f); lev = 0.0
                for (f0, z0), (f1, z1) in zip(J.pies[pie], J.pies[pie][1:]):
                    if f0 < f < f1 and z0 != z1: lev = 0.07 * math.sin(math.pi * (f - f0) / float(f1 - f0))
                T = add(Pp['foot' + pie][0], (0.0, lev, z))
                ref = qrot(Pp['thigh' + pie][1], (1, 0, 0))
                r[ct], r[ck], r[cp], est = ik_pierna(pie, P, T, Pp['foot' + pie][1], ref)
                self_estira[0] = max(self_estira[0], est)
            rots.append(r); Ps.append(fk(r, rp, base.hips[f]))
        fin = []
        for i, J in enumerate(Js):
            r = dict(rots[i]); P = Ps[i]; Po = Ps[1 - i]
            for lado in ('R', 'L'):
                w = pista(J.brazos[lado]['w'], f)
                if w <= 0: continue
                clav, ua, fa, ha, _ = ARM[lado]
                W, cd, dd, pd = brazo_en(J, lado, f, P, Po, D)
                qu, qf, qh, E, og = ik_brazo(lado, P, r, W, cd, dd, pd, prev.get((i, lado)))
                prev[(i, lado)] = og
                r[ua] = qslerp(r[ua], qu, w); r[fa] = qslerp(r[fa], qf, w); r[ha] = qslerp(r[ha], qh, w)
            fin.append(r)
        for i in range(len(Js)): salida[i][f] = fin[i]
        for i in range(len(Js)): fin[i]['raiz'] = raices[i][f]
        info.append([fk(fin[i], raices[i][f], base.hips[f]) for i in range(len(Js))])
    print('piernas: lo que le falto a la pierna mas estirada para llegar al tobillo = %.1f mm' % (self_estira[0] * 1000))
    return salida, info

def escribir(base, poses, canales=(6, 7, 8, 10, 11, 12) + tuple(range(13, 27)), saltar=(18, 23)):
    crudo = dict(base.cr); n = base.n
    crudo[1] = ('pos', 16, [(f, GE.p_a_crudo(mul(poses[f]['raiz'], 100.0))) for f in range(n + 1)])   # raiz en cm
    for ch in canales:
        if ch in saltar: continue
        bs = crudo[ch][1]; keys = []; prev = None
        for f in range(n + 1):
            q = poses[f][ch]
            if prev is not None and dot(prev, q) < 0: q = tuple(-x for x in q)
            prev = q; v, s = GE.q_a_crudo(q, bs); keys.append((f, v, s))
        crudo[ch] = ('rot', bs, keys)
    return GE.escribir_crudo(base.g, crudo, n)

# ---------------------------------------------------------------- la coreografia: saludo, choque de manos arriba y abrazo cruzado
# Los dos de frente a D metros. Mano derecha en alto (sobre el hombro izquierdo del otro), izquierda abajo (bajo el brazo derecho del
# otro): asi los dos hacen lo mismo y no se cruzan. A saluda primero y palmea 3 veces; B contesta el saludo y palmea 2.
CHOQUE = 90
PASO = 0.22        # cada uno da un paso de 22 cm para el abrazo (y lo deshace al final)

def coreografia(D, quien):
    A = quien == 'A'; o = 0 if A else 8                       # B arranca el saludo 8 cuadros despues
    up, fwd = (0, 1, 0), (0, 0, 1)
    # ------- giros de columna / cabeza / claviculas (grados x adelante, y gira a la izquierda, z inclina a la derecha)
    inc = 9.0 if A else 8.0
    giros = {
        13: [(0, (0, 0, 0)), (100, (0, 0, 0)), (128, (inc * 0.6, 0, 0)), (170, (inc * 0.6, 2.5 if A else -2.5, 0)), (205, (inc * 0.6, -2.0 if A else 2.0, 0)),
             (220, (inc * 0.5, 0, 0)), (248, (0, 0, 0))],
        14: [(0, (0, 0, 0)), (100, (0, 0, 0)), (128, (inc, 0, 0)), (205, (inc, 0, 0)), (220, (inc * 0.8, 0, 0)), (248, (0, 0, 0))],
        15: [(0, (0, 0, 0)), (20 + o, (-4, 0, 0)), (CHOQUE - 4, (-12, 0, 0)), (CHOQUE + 8, (-6, 0, 0)), (118, (2, -10, 4)), (135, (6, -26, 11)),
             (205, (6, -24, 11)), (228, (0, -6, 0)), (250, (0, 0, 0))],
        16: [(0, (0, 0, 0)), (20 + o, (-6, 0, 0)), (CHOQUE - 4, (-18, 0, 0)), (CHOQUE + 8, (-8, 0, 0)), (118, (4, -12, 6)), (135, (10, -22 if A else -26, 14)),
             (205, (10, -20 if A else -24, 14)), (228, (0, -4, 0)), (250, (-3, 0, 0)), (262, (4, 0, 0)), (275, (0, 0, 0))],
        22: [(0, (0, 0, 0)), (18 + o, (0, 0, 0)), (30 + o, (0, 0, -6)), (78, (0, 0, -6)), (CHOQUE - 2, (0, 0, -14)), (CHOQUE + 6, (0, 0, -12)),
             (125, (0, 10, -8)), (205, (0, 10, -8)), (232, (0, 4, 0)), (252, (0, 0, 0))],
        17: [(0, (0, 0, 0)), (100, (0, 0, 0)), (128, (0, -10, -2)), (205, (0, -10, -2)), (232, (0, -3, 0)), (252, (0, 0, 0))],
    }
    # ------- brazo derecho
    def yo(hueso, p, codo, dedos, palma): return dict(t=('yo', hueso, p), codo=codo, dedos=dedos, palma=palma)
    def otro(hueso, p, codo, dedos, palma): return dict(t=('otro', hueso, p), codo=codo, dedos=dedos, palma=palma)
    cm = 0.5 * D
    def saludo(dx): return yo('root', (-0.42 + dx, 1.70, 0.18), (-0.6, -1, -0.2), (0.25 * dx / 0.07, 1, 0.1), fwd)
    def mundo(p, codo, dedos, palma): return dict(t=('mundo', None, p), codo=codo, dedos=dedos, palma=palma)
    contacto = mundo((0.0, 1.85, cm), (-1, -0.4, -0.3), (0.05, 1, 0.12), fwd)   # en el MUNDO: el mismo punto para los dos
    preparo = yo('root', (-0.28, 1.70, cm * 0.55), (-1, -0.6, -0.3), (0.0, 1, 0.3), fwd)
    rebote = mundo((-0.04, 1.83, cm - 0.06), (-1, -0.4, -0.3), (0.05, 1, 0.2), fwd)
    # hombro izquierdo del otro: punto en el pecho del otro (x = su izquierda, z = su frente, y = arriba del pecho)
    sobre_hombro = otro('chest', (0.20, 0.30, 0.02), (1, 0.8, 0), (-0.4, -0.5, -1), (0, -1, 0))
    espalda_alta = otro('chest', (0.05, 0.13, -0.12), (1, 0.5, -0.3), (-1, -0.35, 0), (0, 0, 1))
    def palmada(k): return otro('chest', (0.05, 0.13, -0.12 - 0.06 * k), (1, 0.5, -0.3), (-1, -0.35, 0), (0, 0, 1))
    al_costado = otro('chest', (0.30, 0.20, -0.02), (1, 0.4, -0.2), (-0.3, -0.6, -1), (-1, 0, 0.3))
    pats = [150, 162, 174] if A else [158, 172]
    def abajo(sg): return yo('root', (sg * 0.25, 0.91, 0.07), (sg * 0.3, 0, -1), (0, -1, 0.1), (-sg, 0, 0))
    kR = [(0, abajo(-1)), (13 + o, abajo(-1)), (32 + o, saludo(0.0))]
    f = 32 + o
    for j in range(3):
        kR += [(f + 5, saludo(0.07)), (f + 10, saludo(-0.05))]; f += 10
    kR += [(66 + o // 2, saludo(0.0)), (76, preparo), (CHOQUE, contacto), (CHOQUE + 5, rebote), (112, sobre_hombro), (130, espalda_alta)]
    for p in pats: kR += [(p - 1, espalda_alta), (p + 3, palmada(1)), (p + 7, espalda_alta)]
    kR += [(208, espalda_alta), (222, al_costado), (240, abajo(-1)), (300, abajo(-1))]
    wR = [(0, 0.0), (10 + o, 0.0), (28 + o, 1.0), (236, 1.0), (258, 0.0)]
    # ------- brazo izquierdo: bajo el brazo derecho del otro, a la espalda baja
    bajo_brazo = otro('spine', (-0.30, 0.10, 0.04), (-0.8, -0.8, -0.2), (0.3, 0, -1), (1, 0, 0.3))
    espalda_baja = otro('spine', (-0.06, 0.12, -0.13), (-1, -0.4, -0.4), (1, 0.15, 0), (0, 0, 1))
    salida_baja = otro('spine', (-0.32, 0.08, 0.0), (-1, -0.6, -0.2), (0.2, -0.3, -1), (1, 0, 0.3))
    kL = [(0, abajo(1)), (102, abajo(1)), (118, bajo_brazo), (132, espalda_baja), (206, espalda_baja), (222, salida_baja), (240, abajo(1)), (300, abajo(1))]
    wL = [(0, 0.0), (98, 0.0), (110, 1.0), (238, 1.0), (258, 0.0)]
    # ------- el paso: pie izquierdo adelante, despues el derecho; la cadera pasa el peso al pie de apoyo y baja 2 cm al caminar
    p = 0 if A else 4; P_ = PASO
    raiz = [(0, (0, 0, 0)), (98 + p, (0, 0, 0)), (106 + p, (-0.03, -0.02, 0.25 * P_)), (119 + p, (0.03, -0.02, 0.75 * P_)), (130 + p, (0, -0.01, P_)),
            (234, (0, -0.01, P_)), (243, (-0.03, -0.02, 0.75 * P_)), (256, (0.03, -0.02, 0.25 * P_)), (268, (0, 0, 0))]
    pies = {'A': [(0, 0.0), (100 + p, 0.0), (114 + p, P_), (236, P_), (250, 0.0)],
            'B': [(0, 0.0), (113 + p, 0.0), (127 + p, P_), (249, P_), (263, 0.0)]}
    return Jugador('A' if A else 'B', giros, {'R': {'k': kR, 'w': wR}, 'L': {'k': kL, 'w': wL}}, raiz, pies)

def distancia_auto(base):
    """los pechos se tocan en el abrazo: distancia entre raices = pecho de A + pecho de B (inclinados) + grosor de los dos"""
    J = coreografia(0.6, 'A'); f = 170
    P = fk(cuerpo(base, J, f), base.root[f], base.hips[f])
    return round(2 * (P['chest'][0][2] + 0.145), 2)

def revisar(info, D):
    """choque: distancia entre las palmas; abrazo: manos contra el cuerpo del otro (capsula del torso)"""
    def palma(P, lado):
        bh = BONE[lado][3]; W, q = P[bh]; sg = ARM[lado][4]
        return add(W, qrot(q, (sg * HANDO, -0.02, 0)))
    pa = palma(info[CHOQUE][0], 'R'); pb = al_otro(palma(info[CHOQUE][1], 'R'), D)
    print('choque en el cuadro %d: palma A (%.2f %.2f %.2f), palma B (%.2f %.2f %.2f), separacion %.1f cm' % (CHOQUE, *pa, *pb, norm(sub(pa, pb)) * 100))
    peor = (9, 0, '')
    for f in range(130, 206):
        for i in (0, 1):
            P = info[f][i]; Po = info[f][1 - i]
            a = al_otro(Po['hips'][0], D); b = al_otro(Po['neck'][0], D)
            for lado in ('R', 'L'):
                for hb in BONE[lado][1:]:
                    p = P[hb][0]; ab = sub(b, a); t = max(0, min(1, dot(sub(p, a), ab) / dot(ab, ab)))
                    d = norm(sub(p, add(a, mul(ab, t))))
                    if d < peor[0]: peor = (d, f, '%s %s' % ('AB'[i], hb))
    print('abrazo: lo mas cerca del eje del torso del otro = %.1f cm (%s, cuadro %d; el torso mide ~12-14 cm de radio)' % (peor[0] * 100, peor[2], peor[1]))
    pies = []
    for i in (0, 1):
        ys = [info[f][i][k][0][1] for f in range(len(info)) for k in ('footA', 'footB')]
        pies.append((min(ys), max(ys)))
    cab = min((norm(sub(info[f][0]['headc'][0], al_otro(info[f][1]['headc'][0], D))), f) for f in range(130, 206))
    hc = sub(info[cab[1]][0]['headc'][0], al_otro(info[cab[1]][1]['headc'][0], D))
    print('abrazo: cabezas a %.1f cm (centro a centro, cuadro %d: x %.1f y %.1f z %.1f; una cabeza mide ~19 cm de ancho)' % (cab[0] * 100, cab[1], hc[0] * 100, hc[1] * 100, hc[2] * 100))
    print('tobillos (alto, m): A %.3f..%.3f  B %.3f..%.3f (iguales al clip parado del juego)' % (*pies[0], *pies[1]))

def main():
    a = sys.argv[1:]
    if len(a) < 2: print(__doc__ or 'uso: gani_cuerpo.py carpeta salida [distancia]'); return 1
    base = Base(open(os.path.join(a[0], 'dml_ent_aisle_cmn_idle01.gani'), 'rb').read())
    Dh = float(a[2]) if len(a) > 2 and float(a[2]) > 0 else distancia_auto(base)
    D = round(Dh + 2 * PASO, 2)
    print('distancia entre los dos jugadores: %.2f m al empezar (en el abrazo %.2f m, despues del paso de cada uno)' % (D, Dh))
    Js = [coreografia(D, 'A'), coreografia(D, 'B')]
    poses, info = armar(base, Js, D)
    revisar(info, D)
    os.makedirs(a[1], exist_ok=True)
    for J, p in zip(Js, poses):
        b = escribir(base, p)
        fn = os.path.join(a[1], 'zfx_saludo_abrazo_%s.gani' % J.nombre.lower())
        open(fn, 'wb').write(b)
        G = GC.leer_gani(b)
        print('%s: %d B, %d cuadros, %d canales' % (fn, len(b), G['cuadros'], G['ncanales']))
    return 0

if __name__ == '__main__': sys.exit(main())
