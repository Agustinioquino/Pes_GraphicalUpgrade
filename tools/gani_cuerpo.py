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
# Unidad de los canales de lugar (1 raiz, 3 cadera): 128 por metro, no cm. Medido 17/11 contra clips del juego con este esqueleto:
# en las caminatas (dml_end_spectator01_walk02, pair01_walk) el pie apoyado solo queda quieto si el recorrido de la raiz se toma
# /128 (con /100 resbala 22 %); en la foto agachado (photo05_pair01) los tobillos quedan en el piso (+6 cm) con /128 y bajo el piso
# (-11 cm) con /100. Las dos medidas dan 1.28-1.29.
UNIDAD = 128.0
def muestra(keys, f, kind, bs):
    def val(k):
        if kind == 'rot': return tuple(GC.quat([x / float(1 << bs) for x in k[1]], k[2]))
        return tuple(GC.half(h) / UNIDAD for h in k[1])
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
        P0 = fk(self.rot[0], self.root[0], self.hips[0])
        self.pie0 = {k: (P0['foot' + k][0][0], P0['foot' + k][0][2]) for k in 'AB'}     # tobillos del cuadro 0 (x, z)
    def extender(self, N):
        """clip mas largo que el parado: el parado se usa de ida y vuelta (300 -> 0 -> 300 ...), sin saltos"""
        m = self.n
        def k(f):
            c = f % (2 * m); return c if c <= m else 2 * m - c
        self.rot = [self.rot[k(f)] for f in range(N + 1)]; self.root = [self.root[k(f)] for f in range(N + 1)]
        self.hips = [self.hips[k(f)] for f in range(N + 1)]; self.n = N

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
    fn = getattr(J, 'giros_fn', None)
    if fn:
        for ch, e in fn(f).items(): rot[ch] = qm(rot[ch], qeuler(e))
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
    def res(o):
        if callable(o): return o(f, Pyo, Potro)
        return objetivo_parado(lado, Pyo) if o == 'parado' else objetivo_mundo(o, Pyo, Potro, D)
    if f <= k[0][0]: return res(k[0][1])
    for (f0, o0), (f1, o1) in zip(k, k[1:]):
        if f <= f1: return mezcla_obj(res(o0), res(o1), smooth((f - f0) / float(f1 - f0)))
    return res(k[-1][1])

# ---------------------------------------------------------------- el pie al caminar: despega con la punta y apoya con el talon
PUNTA = (0.0, -0.06, 0.13); TALON = (0.0, -0.06, -0.05)     # desde el tobillo, en el piso (m)
def _rx(deg): return qaxis((1, 0, 0), deg)
def _ry(deg): return qaxis((0, 1, 0), deg)
# Paso medido en la caminata del juego dml_end_spectator01_walk02.gani (17/11, 6 zancadas de los dos pies, cuadros 1-150, unidad 128/m):
# zancada 1.19 m (un pie de apoyo a apoyo), ciclo 42 cuadros, el pie se mueve 26 de esos 42 (del despegue del talon al pie plano).
# u = 0 despega el talon, ~0.38 se va la punta (punta 62 grados abajo, tobillo 12 cm arriba), ~0.64 pasa plano casi rozando,
# ~0.80 apoya el talon con la punta 26 grados arriba, 1 plano. Columnas: avance (0..1 del recorrido), costado (m, + = hacia afuera),
# alto del tobillo sobre el apoyo (m), punta (grados, + = arriba).
PASO_REF = [  # (u, avance, costado, alto, punta)
    (0.00, -0.006, -0.010, 0.006, -0.9),
    (0.04, -0.003, -0.010, 0.010, -3.0),
    (0.08, 0.003, -0.011, 0.017, -6.4),
    (0.12, 0.011, -0.011, 0.025, -11.2),
    (0.16, 0.023, -0.011, 0.035, -17.7),
    (0.20, 0.041, -0.011, 0.048, -26.3),
    (0.24, 0.066, -0.009, 0.065, -36.2),
    (0.28, 0.103, -0.006, 0.085, -47.4),
    (0.32, 0.150, -0.006, 0.103, -57.9),
    (0.36, 0.201, -0.008, 0.116, -61.9),
    (0.40, 0.254, -0.012, 0.122, -58.2),
    (0.44, 0.311, -0.015, 0.119, -50.4),
    (0.48, 0.372, -0.017, 0.107, -40.7),
    (0.52, 0.440, -0.015, 0.087, -30.2),
    (0.56, 0.513, -0.011, 0.063, -20.2),
    (0.60, 0.592, -0.005, 0.040, -9.9),
    (0.64, 0.676, 0.002, 0.024, 0.3),
    (0.68, 0.760, 0.010, 0.020, 9.0),
    (0.72, 0.837, 0.016, 0.027, 16.3),
    (0.76, 0.900, 0.017, 0.035, 22.0),
    (0.80, 0.943, 0.016, 0.037, 26.0),
    (0.84, 0.966, 0.011, 0.032, 24.2),
    (0.88, 0.980, 0.004, 0.026, 17.2),
    (0.92, 0.991, -0.000, 0.020, 10.2),
    (0.96, 0.999, -0.001, 0.013, 4.4),
    (1.00, 1.003, -0.000, 0.008, 1.4),
]
ZANCADA_REF = 1.19

def _paso_ref(u):
    u = max(0.0, min(1.0, u)); x = u * (len(PASO_REF) - 1); i = min(int(x), len(PASO_REF) - 2); t = x - i
    a = PASO_REF[i]; b = PASO_REF[i + 1]; r0 = PASO_REF[0]; r1 = PASO_REF[-1]; fin = (1.0, 0.0, 0.0, 0.0)
    # sin saltos: en u = 0 todo vale 0 (el pie sigue donde estaba) y en u = 1 queda justo en la huella nueva
    return tuple(a[j] + (b[j] - a[j]) * t - (1 - u) * r0[j] - u * (r1[j] - fin[j - 1]) for j in range(1, 5))

def _tramos(keys):
    for (f0, v0), (f1, v1) in zip(keys, keys[1:]):
        if v0 != v1: yield f0, v0, f1, v1

def huella(keys, f):
    """donde esta el pie en el piso (x, z, giro) siguiendo el avance del paso del juego (sin la altura)"""
    for f0, v0, f1, v1 in _tramos(keys):
        if f0 <= f <= f1:
            pr = _paso_ref((f - f0) / float(f1 - f0))[0]; pr = max(0.0, min(1.0, pr))
            return tuple(a + (b - a) * pr for a, b in zip(v0, v1))
    return pista(keys, f)

def pie_en(keys, f, Ay, Q0, lado=1.0):
    """keys = [(cuadro, (x, z, giro))]: huella del tobillo en el piso (m) y hacia donde apunta el pie (grados, + = izquierda). Un tramo
    con dos valores distintos es un paso: del primer cuadro (despega el talon) al segundo (pie plano en el lugar nuevo). El pie sigue la
    curva del paso del juego (PASO_REF): sube el talon girando sobre la punta, se va la punta, pasa bajo, apoya el talon con la punta
    arriba y baja. Pasos cortos, de costado o hacia atras: la curva se achica (menos alto y menos giro del pie)."""
    for f0, v0, f1, v1 in _tramos(keys):
        if not (f0 <= f <= f1): continue
        u = (f - f0) / float(f1 - f0); pr, lat, h, pit = _paso_ref(u)
        dl = (v1[0] - v0[0], v1[1] - v0[1]); dist = math.hypot(*dl)
        ym = math.radians(0.5 * (v0[2] + v1[2])); fac = (math.sin(ym), math.cos(ym))
        adel = dl[0] * fac[0] + dl[1] * fac[1]
        kk = min(1.0, dist / ZANCADA_REF) ** 0.6 * (1.0 if adel > 0.6 * dist else 0.4)
        prc = max(0.0, min(1.0, pr))
        x = v0[0] + dl[0] * pr; z = v0[1] + dl[1] * pr; yaw = v0[2] + (v1[2] - v0[2]) * smooth(prc)
        yr = math.radians(yaw)     # hacia afuera: lado = 1 pie A (izquierda del jugador), -1 pie B
        x += lado * math.cos(yr) * lat * kk; z -= lado * math.sin(yr) * lat * kk
        th = -pit * kk
        return (x, Ay + h * kk, z), qm(qm(_ry(yaw), _rx(th)), Q0)
    v = pista(keys, f)
    return (v[0], Ay, v[1]), qm(_ry(v[2]), Q0)

def filtrar(salida, J, chs=((19, 0.30), (20, 0.24), (21, 0.17), (24, 0.30), (25, 0.24), (26, 0.17))):
    """PESO: resorte (amortiguado justo) por canal del brazo: el brazo llega un poco despues y la mano despues del antebrazo.
    En los tramos de contacto (J.contacto = [(desde, hasta)]) se apaga con rampas de 6 cuadros para que las manos lleguen exactas."""
    n = len(salida) - 1
    def libre(f):
        c = 0.0
        for a, b in J.contacto:
            if a <= f <= b: return 0.0
            if a - 6 < f < a: c = max(c, (f - (a - 6)) / 6.0)
            if b < f < b + 6: c = max(c, 1.0 - (f - b) / 6.0)
        return 1.0 - c
    for ch, w in chs:
        x = salida[0][ch]; v = (0.0, 0.0, 0.0, 0.0)
        for f in range(n + 1):
            y = salida[f][ch]
            if dot(x, y) < 0: y = tuple(-c for c in y)
            a = add(mul(sub(y, x), w * w), mul(v, -2.0 * w)); v = add(v, a); x = add(x, v)
            m = libre(f); out = qn(lerp(y, x, m))
            if m < 1.0: x = lerp(x, y, 1.0 - m); v = mul(v, m)
            salida[f][ch] = out

# ---------------------------------------------------------------- armado de los dos clips
def armar(base, Js, D):
    n = base.n; salida = [dict() for _ in Js]; info = []; prev = {}; raices = [[] for _ in Js]; self_estira = [0.0]
    for f in range(n + 1):
        Pp = fk(base.rot[f], base.root[f], base.hips[f])     # el clip parado (su muslo da el plano de la rodilla)
        if f == 0: P0 = Pp                                   # los pies quedan donde estan en el cuadro 0 del parado (fijos, + el paso)
        rots = []; Ps = []
        for i, J in enumerate(Js):
            rp = add(base.root[f], J.raiz(f) if callable(J.raiz) else pista(J.raiz, f)); raices[i].append(rp)
            r = cuerpo(base, J, f); P = fk(r, rp, base.hips[f])
            for pie, (ct, ck, cp) in (('A', (6, 7, 8)), ('B', (10, 11, 12))):
                T, Qp = pie_en(J.pies[pie], f, P0['foot' + pie][0][1], P0['foot' + pie][1], 1.0 if pie == 'A' else -1.0)
                ref = qrot(qm(P['root'][1], qi(Pp['root'][1])), qrot(Pp['thigh' + pie][1], (1, 0, 0)))   # con el giro de la raiz
                r[ct], r[ck], r[cp], est = ik_pierna(pie, P, T, Qp, ref)
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
    print('piernas: lo que le falto a la pierna mas estirada para llegar al tobillo = %.1f mm' % (self_estira[0] * 1000))
    for i, J in enumerate(Js):
        if getattr(J, 'contacto', None) is not None: filtrar([salida[i][f] for f in range(n + 1)], J)
    info = [[fk(salida[i][f], raices[i][f], base.hips[f]) for i in range(len(Js))] for f in range(n + 1)]
    return salida, info

def escribir(base, poses, canales=(0, 2, 6, 7, 8, 10, 11, 12) + tuple(range(13, 27)), saltar=(18, 23)):
    crudo = dict(base.cr); n = base.n
    crudo[1] = ('pos', 16, [(f, GE.p_a_crudo(mul(poses[f]['raiz'], UNIDAD))) for f in range(n + 1)])   # raiz (128 por metro)
    crudo[3] = ('pos', 16, [(f, GE.p_a_crudo(mul(base.hips[f], UNIDAD))) for f in range(n + 1)])        # cadera (el parado, de ida y vuelta)
    for ch, (k, bs, keys) in list(crudo.items()):     # fijos (4, 5, 9, 18, 23): la misma clave repetida hasta el ultimo cuadro (paso < 256)
        if ch in (1, 3) or ch in canales and ch not in saltar: continue
        v = keys[-1][1:]; fs = list(range(0, n, 200)) + [n]
        crudo[ch] = (k, bs, [(f,) + tuple(v) for f in fs]) if len(keys) > 1 else (k, bs, keys)
    for ch in canales:
        if ch in saltar: continue
        bs = crudo[ch][1]; keys = []; prev = None
        for f in range(n + 1):
            q = poses[f][ch]
            if prev is not None and dot(prev, q) < 0: q = tuple(-x for x in q)
            prev = q; v, s = GE.q_a_crudo(q, bs); keys.append((f, v, s))
        crudo[ch] = ('rot', bs, keys)
    return GE.escribir_crudo(base.g, crudo, n)

# ---------------------------------------------------------------- la coreografia (17/11 19:32, Agustin): "hablan caminando desde una
# distancia y moviendo las manos, se preparan, chocan ambas manos, se abrazan y se separan"; 19:33 "uno apunta al otro con el dedo
# mientras hablan, despues de abrazarse se separan mas lentamente y natural"; 19:34 "uno de los dos que guine un ojo antes de saludar".
# Los dos se miran de frente a D metros y caminan WALK cada uno. Abrazo cruzado: derecha arriba (sobre el hombro izquierdo del otro),
# izquierda abajo (bajo su brazo derecho): los dos hacen lo mismo y no se cruzan.
N = 540            # cuadros (18 s)
WALK = 2.44        # lo que camina cada uno hasta el otro (desde el lugar del cuadro 0)
PASO = 0.22        # el paso de cada uno para el abrazo (y el de vuelta)
CHOQUE = 150       # choque de las dos manos
CHOQUE2 = 372      # (19:42) choque con una mano al separarse

def _arco(keys, f):
    for (f0, z0), (f1, z1) in zip(keys, keys[1:]):
        if f0 < f < f1 and z0 != z1: return math.sin(math.pi * (f - f0) / float(f1 - f0))
    return 0.0

def golpes(f, f0, puntos, cada=20, ataque=0.45):
    """gesto al hablar: la mano va de un punto al siguiente rapido (ataque) y se queda (como los golpes de la voz)"""
    if f <= f0: return puntos[0]
    i = int((f - f0) // cada); u = ((f - f0) % cada) / float(cada)
    a = puntos[min(i, len(puntos) - 1)]; b = puntos[min(i + 1, len(puntos) - 1)]
    return lerp(a, b, smooth(min(1.0, u / ataque)))

def _rot2(x, z, deg):     # giro en el piso (como Ry)
    c = math.cos(math.radians(deg)); s_ = math.sin(math.radians(deg)); return (x * c + z * s_, -x * s_ + z * c)

def coreografia(D, quien, pie0):
    A = quien == 'A'; o = 0 if A else 3; sgn = 1.0 if A else -1.0
    fwd = (0, 0, 1); cm = 0.5 * D; C = CHOQUE; C2 = CHOQUE2
    (lx, lz), (rx, rz) = pie0['A'], pie0['B']
    def L_(dz, x=None, yaw=0.0): return (lx if x is None else x, lz + dz, yaw)
    def R_(dz, x=None, yaw=0.0): return (rx if x is None else x, rz + dz, yaw)
    # ------- huellas (17/11 19:45, Agustin: "ya existen en el juego animaciones de walk, tomalas de referencia"): el paso sigue la curva
    # del juego (PASO_REF) con su ritmo (un pie cada 22 cuadros, ventana de 26) y una zancada algo mas corta (0.96 m, charlando).
    # Arrancan ya caminando, frenan en los dos ultimos pasos; paso del abrazo, paso atras, media vuelta a la derecha y se van.
    cx = 0.5 * (lx + rx); cz = WALK + 0.5 * (lz + rz); hw = 0.5 * (lx - rx)
    def giro(off, deg): x, z = _rot2(off, 0.0, deg); return (cx + x, cz + z, deg)
    t = 0 if A else 4                                   # B se da vuelta un poco despues
    ow = 0 if A else 8                                  # B llega un poco despues que A (desfasados)
    def pasos(ini, lista):    # lista = [(desde, hasta, huella)]: despega el talon en 'desde', queda plano en 'hasta'
        k = [(-100, ini)]; cur = ini
        for a, b, v in lista: k += [(a, cur), (b, v)]; cur = v
        return k
    pL = pasos(L_(WALK - 2.66), [(0 + ow, 26 + ow, L_(WALK - 1.70)), (44 + ow, 70 + ow, L_(WALK - 0.74)), (88 + ow, 114 + ow, L_(WALK)),
                                  (158, 180, L_(WALK + PASO)), (322, 348, L_(WALK)),
                                  (428 + t, 446 + t, giro(hw, -180.0)), (458 + t, 482 + t, (cx - hw, cz - 0.40, -180.0)),
                                  (500 + t, 526 + t, (cx - hw, cz - 1.35, -180.0))])
    pR = pasos(R_(WALK - 3.14), [(-22 + ow, 4 + ow, R_(WALK - 2.18)), (22 + ow, 48 + ow, R_(WALK - 1.22)), (66 + ow, 92 + ow, R_(WALK - 0.32)),
                                  (114 + ow, 134 + ow, R_(WALK)), (170, 192, R_(WALK + PASO)), (336, 362, R_(WALK)),
                                  (414 + t, 430 + t, giro(-hw, -90.0)), (442 + t, 460 + t, giro(-hw, -180.0)),
                                  (478 + t, 504 + t, (cx + hw, cz - 0.85, -180.0)), (522 + t, 548 + t, (cx + hw, cz - 1.82, -180.0))])
    pies = {'A': pL, 'B': pR}
    ox, oz = 0.5 * (lx + rx), 0.5 * (lz + rz)
    def yaw_de(f): return 0.5 * (huella(pL, f)[2] + huella(pR, f)[2])
    def frente(f): y = math.radians(yaw_de(f)); return (math.sin(y), 0.0, math.cos(y))
    def adelanto(f, a, b):     # cuanto adelante esta el pie a del pie b, en la direccion en que mira
        pa = huella(a, f); pb = huella(b, f); return dot((pa[0] - pb[0], 0.0, pa[1] - pb[1]), frente(f))
    def raiz(f):
        xs = []; zs = []
        for g in range(f - 8, f + 9):
            a_ = huella(pL, g); b_ = huella(pR, g); xs.append(0.5 * (a_[0] + b_[0])); zs.append(0.5 * (a_[1] + b_[1]))
        x = sum(xs) / len(xs) - ox; z = sum(zs) / len(zs) - oz
        sp = abs(adelanto(f, pL, pR))     # cadera mas baja con los pies abiertos, mas alta al pasar (juego: 3 cm)
        y = -(0.008 + 0.030 * min(1.0, sp / 0.5)) + 0.008 * max(_arco(pL, f), _arco(pR, f))
        lat = 0.028 * (_arco(pR, f) - _arco(pL, f)); yw = math.radians(yaw_de(f))     # el peso pasa al pie de apoyo
        return (x + lat * math.cos(yw), y, z - lat * math.sin(yw))
    def giros_fn(f):          # media vuelta (raiz); cadera que gira con el paso (juego: +-8 grados) y el pecho la sigue casi entera
        d = adelanto(f, pL, pR) / 0.48; lr = _arco(pL, f) - _arco(pR, f)
        return {0: (0.0, yaw_de(f), 0.0), 2: (0.0, -8.5 * d, -2.5 * lr), 13: (0.0, 1.0 * d, 1.5 * lr), 14: (0.0, 0.5 * d, 1.0 * lr),
                15: (0.0, 2.0 * d, 0.0), 16: (0.0, 2.5 * d, 0.0)}
    # ------- columna / cabeza / claviculas (grados: x adelante, y gira a la izquierda, z inclina a la derecha)
    inc = 9.0 if A else 8.0
    def asiente(f0, f1, amp=3.5, cada=20):   # cabeceos al hablar, con los golpes
        return [(f, (amp * (1 if ((f - f0) // cada) % 2 == 0 else -0.3), 1.5 * math.sin((f - f0) * 0.11), 0)) for f in range(f0, f1, cada // 2)]
    hA = asiente(12, 80) if A else asiente(72, 116)
    giros = {
        13: [(0, (3, 0, 0)), (C - 14 + ow // 2, (3, 0, 0)), (C - 2, (-3, 0, 0)), (C + 10, (0, 0, 0)), (182, (inc * 0.6, 0, 0)),
             (234, (inc * 0.6, 2.5 * sgn, 0)), (276, (inc * 0.6, -2.0 * sgn, 0)), (300, (inc * 0.45, 0, 0)), (322, (1.5, 0, 0)), (356, (0, 0, 0)),
             (C2 - 4, (2, 4, 0)), (C2 + 6, (0, 0, 0)), (440, (0, 0, 0)), (470, (3, 0, 0))],
        14: [(0, (0, 0, 0)), (C + 10, (0, 0, 0)), (186, (inc, 0, 0)), (276, (inc, 0, 0)), (304, (inc * 0.6, 0, 0)), (330, (0, 0, 0))],
        15: [(0, (0, 0, 0))] + [(f, (v[0] * 0.5, v[1], 0)) for f, v in hA] + [(C - 12 + ow // 2, (0, 0, 0)), (C - 2, (-8, 0, 0)), (C + 8, (-4, 0, 0)),
             (176, (2, -10, 4)), (194, (6, -26, 11)), (276, (6, -24, 11)), (310, (4, -20, 9)), (336, (0, -4, 2)), (352, (0, 0, 0)),
             (C2 - 4, (-4, 0, 0)), (C2 + 8, (0, 0, 0))],
        16: [(0, (0, 0, 0))] + hA + [(C - 12 + ow // 2, (0, 0, 0)), (C - 2, (-12, 0, 0)), (C + 8, (-6, 0, 0)), (176, (4, -12, 6)),
             (194, (10, -22 if A else -26, 14)), (276, (10, -20 if A else -24, 14)), (310, (6, -16, 11)), (336, (-2, -4, 2)), (354, (-4, 0, 0)),
             (C2 - 4, (-6, 0, 0)), (C2 + 8, (0, 0, 0)), (398 + o, (9, 0, 0)), (408 + o, (-2, 0, 0)), (418 + o, (0, 0, 0)),
             (428 + t, (0, -25, 0)), (444 + t, (0, 0, 0))],
        22: [(0, (0, 0, 0)), (C - 12 + ow // 2, (0, 0, 0)), (C - 4, (0, 0, -8)), (C, (0, 0, -12)), (C + 8, (0, 0, -8)), (188, (0, 10, -8)), (276, (0, 10, -8)),
             (314, (0, 5, -2)), (344, (0, 0, 0)), (C2 - 8, (0, 0, -6)), (C2 + 8, (0, 0, -3)), (392, (0, 0, 0))],
        17: [(0, (0, 0, 0)), (C - 12 + ow // 2, (0, 0, 0)), (C - 4, (0, 0, 8)), (C, (0, 0, 12)), (C + 8, (0, 0, 6)), (188, (0, -10, -2)), (276, (0, -10, -2)),
             (314, (0, -5, 0)), (344, (0, 0, 0))],
    }
    # ------- objetivos de los brazos
    def yo(hueso, p, codo, dedos, palma): return dict(t=('yo', hueso, p), codo=codo, dedos=dedos, palma=palma)
    def otro(hueso, p, codo, dedos, palma): return dict(t=('otro', hueso, p), codo=codo, dedos=dedos, palma=palma)
    def mundo(p, codo, dedos, palma): return dict(t=('mundo', None, p), codo=codo, dedos=dedos, palma=palma)
    def en_raiz(P, p, codo, dedos, palma):   # punto y direcciones en el marco de la raiz (gira con la media vuelta)
        r, q = P['root']; return add(r, qrot(q, p)), qrot(q, codo), qrot(q, dedos), qrot(q, palma)
    def balanceo(lado):        # brazo colgando que acompana el paso (adelante cuando va adelante la pierna contraria)
        sg = ARM[lado][4]; mio, opu = (pR, pL) if lado == 'R' else (pL, pR)
        def fn(f, P, Po):
            d = max(-1.0, min(1.0, adelanto(f, opu, mio) / 0.48))
            return en_raiz(P, (sg * 0.25, 0.92 + 0.05 * max(0.0, d), 0.08 + 0.20 * d), (sg * 0.3, 0, -1), (0, -1, 0.15 + 0.5 * d), (-sg, 0, 0.2))
        return fn
    def charla(lado, f0, puntos, cada=20):   # mano adelante, palma arriba y adentro, de golpe en golpe
        sg = ARM[lado][4]
        def fn(f, P, Po):
            dx, dy, dz = golpes(f, f0, puntos, cada)
            return en_raiz(P, (sg * (0.20 + dx), 1.06 + dy, 0.28 + dz), (sg * 0.4, -1, -0.5), (-sg * 0.35, 0.15, 1), (-sg * 0.6, 1, 0.1))
        return fn
    def senala(lado, f0):      # brazo hacia el pecho del otro, dos golpecitos lentos (mano "pointing")
        bu = BONE[lado][1]; sg = ARM[lado][4]
        def fn(f, P, Po):
            S_ = P[bu][0]; c = al_otro(add(Po['chest'][0], qrot(Po['chest'][1], (0, 0.08, 0.12))), D)
            d = unit(sub(c, S_)); L = 0.42 + 0.03 * max(0.0, math.sin((f - f0) * 2 * math.pi / 22.0))
            return add(S_, mul(d, L)), (sg * 0.5, -1, -0.2), d, unit((-sg * 0.7, -0.7, 0))
        return fn
    def prepara(lado): sg = ARM[lado][4]; return yo('root', (sg * 0.25, 1.52, 0.30), (sg * 1, -0.6, -0.3), (sg * 0.15, 1, 0.25), fwd)
    def contacto(lado): sg = ARM[lado][4]; return mundo((sg * 0.20, 1.78, cm), (sg * 1, -0.5, -0.3), (sg * 0.12, 1, 0.12), fwd)
    def rebote(lado): sg = ARM[lado][4]; return mundo((sg * 0.25, 1.72, cm - 0.08), (sg * 1, -0.5, -0.3), (sg * 0.15, 1, 0.2), fwd)
    def abajo(lado): sg = ARM[lado][4]; return yo('root', (sg * 0.25, 0.91, 0.07), (sg * 0.3, 0, -1), (0, -1, 0.1), (-sg, 0, 0))
    sobre_hombro = otro('chest', (0.20, 0.30, 0.02), (1, 0.8, 0), (-0.4, -0.5, -1), (0, -1, 0))
    espalda_alta = otro('chest', (0.05, 0.13, -0.12), (1, 0.5, -0.3), (-1, -0.35, 0), (0, 0, 1))
    def palmada(k): return otro('chest', (0.05, 0.13, -0.12 - 0.06 * k), (1, 0.5, -0.3), (-1, -0.35, 0), (0, 0, 1))
    al_costado = otro('chest', (0.32, 0.12, -0.02), (1, 0.4, -0.2), (-0.2, -0.8, -0.8), (-1, 0, 0.3))
    bajo_brazo = otro('spine', (-0.30, 0.10, 0.04), (-0.8, -0.8, -0.2), (0.3, 0, -1), (1, 0, 0.3))
    espalda_baja = otro('spine', (-0.06, 0.12, -0.13), (-1, -0.4, -0.4), (1, 0.15, 0), (0, 0, 1))
    salida_baja = otro('spine', (-0.33, 0.04, 0.02), (-1, -0.6, -0.2), (0.1, -0.6, -0.8), (1, 0, 0.3))
    # choque con una mano (las dos derechas, a la altura del hombro) y el saludito
    pf1 = unit((0.3, 0, 1))      # palma adelante y un poco hacia adentro (las dos palmas quedan enfrentadas)
    prepara1 = yo('root', (-0.30, 1.48, 0.18), (-1, -0.7, -0.3), (-0.1, 1, 0.3), pf1)
    contacto1 = mundo((0.0, 1.60, cm), (-1, -0.6, -0.3), (0.1, 1, 0.2), pf1)
    rebote1 = mundo((-0.06, 1.56, cm - 0.07), (-1, -0.6, -0.3), (0.0, 1, 0.25), pf1)
    if A:   # saludo militar canchero: la mano a la frente y se va para afuera
        gesto = [(394, yo('root', (-0.12, 1.66, 0.16), (-1, 0, 0.2), (0.6, 0.75, 0.1), (0, -0.3, 1))),
                 (402, yo('root', (-0.12, 1.66, 0.16), (-1, 0, 0.2), (0.6, 0.75, 0.1), (0, -0.3, 1))),
                 (412, yo('root', (-0.34, 1.66, 0.28), (-1, -0.4, 0), (-0.1, 1, 0.3), fwd))]
    else:   # mano levantada con un pequeno vaiven
        gesto = [(398, yo('root', (-0.30, 1.46, 0.18), (-1, -0.8, -0.3), (0, 1, 0.15), fwd)),
                 (404, yo('root', (-0.36, 1.48, 0.18), (-1, -0.8, -0.3), (-0.25, 1, 0.15), fwd)),
                 (410, yo('root', (-0.28, 1.47, 0.18), (-1, -0.8, -0.3), (0.15, 1, 0.15), fwd)),
                 (416, yo('root', (-0.33, 1.46, 0.18), (-1, -0.8, -0.3), (0, 1, 0.15), fwd))]
    # golpes del que habla (dx hacia afuera, dy arriba, dz adelante, m)
    gA = [(0, 0, 0), (0.04, 0.05, 0.04), (0.0, 0.0, 0.02), (0.06, 0.07, 0.05), (0.02, 0.01, 0.0), (0.05, 0.04, 0.05)]
    gB = [(0, 0, 0), (0.05, 0.06, 0.05), (0.01, 0.02, 0.0), (0.06, 0.08, 0.06), (0.0, -0.01, 0.02), (0.05, 0.05, 0.04), (0.02, 0.02, 0.01)]
    # ------- brazo derecho
    if A:
        kR = [(0, balanceo('R')), (12, balanceo('R')), (24, charla('R', 18, gA)), (36, charla('R', 18, gA)), (48, senala('R', 48)), (70, senala('R', 48)),
              (82, charla('R', 70, gA[2:])), (88, charla('R', 70, gA[2:])), (100, balanceo('R')), (C - 20, balanceo('R'))]
    else:
        kR = [(0, balanceo('R')), (66, balanceo('R')), (78, charla('R', 72, gB)), (112, charla('R', 72, gB)), (122, balanceo('R')), (C - 16, balanceo('R'))]
    kR += [(C - 7 + ow // 8, prepara('R')), (C, contacto('R')), (C + 7, rebote('R')), (172, sobre_hombro), (190, espalda_alta)]
    pats = [212, 228, 244] if A else [220, 238]
    for p in pats: kR += [(p - 1, espalda_alta), (p + 4, palmada(1)), (p + 9, espalda_alta)]
    kR += [(278, espalda_alta), (306, al_costado), (336, abajo('R')), (350, abajo('R')), (C2 - 10, prepara1), (C2, contacto1), (C2 + 6, rebote1),
           (386, abajo('R'))] + gesto + [(428, abajo('R')), (436 + t, balanceo('R')), (N, balanceo('R'))]
    # ------- brazo izquierdo
    if A:
        kL = [(0, balanceo('L')), (18, balanceo('L')), (28, charla('L', 22, [(0, 0, 0), (0.03, 0.03, 0.02), (0.0, 0.0, 0.0)], 24)), (40, charla('L', 22, [(0, 0, 0), (0.03, 0.03, 0.02), (0.0, 0.0, 0.0)], 24)),
              (52, balanceo('L')), (C - 20, balanceo('L'))]
    else:
        kL = [(0, balanceo('L')), (72, balanceo('L')), (84, charla('L', 80, [(0.0, 0.0, 0.0), (0.03, 0.04, 0.03), (0.0, 0.01, 0.0), (0.04, 0.05, 0.03)], 22)),
              (110, charla('L', 80, [(0.0, 0.0, 0.0), (0.03, 0.04, 0.03), (0.0, 0.01, 0.0), (0.04, 0.05, 0.03)], 22)), (122, balanceo('L')), (C - 16, balanceo('L'))]
    kL += [(C - 7 + ow // 8, prepara('L')), (C, contacto('L')), (C + 7, rebote('L')), (178, bajo_brazo), (194, espalda_baja),
           (280, espalda_baja), (308, salida_baja), (338, abajo('L')), (430, abajo('L')), (438 + t, balanceo('L')), (N, balanceo('L'))]
    w = [(0, 1.0), (N, 1.0)]
    J = Jugador('A' if A else 'B', giros, {'R': {'k': kR, 'w': w}, 'L': {'k': kL, 'w': w}}, raiz, pies)
    J.giros_fn = giros_fn
    J.contacto = [(C - 3, C + 6), (186, 282), (C2 - 3, C2 + 5)]
    # ------- cara y manos (las pone el Motor junto con el clip): (cuadro, gesto de la cara, peso, mano izquierda, mano derecha)
    if A:
        J.pista = [(0, 'smil_talk_blink_soft', 0.8, 'relax', 'relax'), (20, 'smil_talk_blink_soft', 0.8, 'relax', 'hiraki_normal'),
                   (46, 'smil_talk_blink_soft', 0.9, 'relax', 'pointing'), (74, 'smil_talk_blink_soft', 0.8, 'relax', 'hiraki_normal'),
                   (94, 'smil_soft', 0.6, 'relax', 'relax'), (118, 'pow_brwnit_wink_r', 1.0, 'relax', 'relax'),
                   (C - 10, 'smil_mho', 1.0, 'open_full', 'open_full'), (C + 14, 'smil_soft', 0.8, 'open_full_half', 'open_full_half'),
                   (190, 'smil_shut_eyhc', 0.9, 'open_full', 'open_full'), (280, 'smil_soft', 0.8, 'open_full_half', 'open_full_half'),
                   (318, 'smil_soft', 0.7, 'relax', 'relax'), (C2 - 12, 'smil_mho', 0.9, 'relax', 'open_full'), (C2 + 12, 'smil_smirk', 0.8, 'relax', 'hiraki_normal'),
                   (418, 'smil_soft', 0.6, 'relax', 'relax')]
    else:
        J.pista = [(0, 'smil_soft', 0.6, 'relax', 'relax'), (70, 'smil_talk_blink_soft', 0.8, 'hiraki_normal', 'hiraki_normal'),
                   (122, 'smil_soft', 0.7, 'relax', 'relax'), (C - 8, 'smil_mho', 1.0, 'open_full', 'open_full'),
                   (C + 14, 'smil_soft', 0.8, 'open_full_half', 'open_full_half'), (190, 'smil_shut_eyhc', 0.8, 'open_full', 'open_full'),
                   (280, 'smil_talk_blink_soft', 0.7, 'open_full_half', 'open_full_half'), (318, 'smil_soft', 0.7, 'relax', 'relax'),
                   (C2 - 12, 'smil_mho', 0.9, 'relax', 'open_full'), (C2 + 12, 'smil_soft', 0.9, 'relax', 'hiraki_normal'),
                   (420, 'smil_soft', 0.6, 'relax', 'relax')]
    return J

def distancia_auto(base):
    """los pechos se tocan en el abrazo: distancia entre raices = pecho de A + pecho de B (inclinados) + grosor de los dos"""
    J = coreografia(4.0, 'A', base.pie0); f = 240
    P = fk(cuerpo(base, J, f), base.root[f], base.hips[f])
    return round(2 * (P['chest'][0][2] + 0.145), 2)

def revisar(info, D):
    """choque: distancia entre las palmas; abrazo: brazos contra el cuerpo del otro (capsula del torso)"""
    def palma(P, lado):
        bh = BONE[lado][3]; W, q = P[bh]; sg = ARM[lado][4]
        return add(W, qrot(q, (sg * HANDO, -0.02, 0)))
    for la, lb in (('R', 'L'), ('L', 'R')):
        pa = palma(info[CHOQUE][0], la); pb = al_otro(palma(info[CHOQUE][1], lb), D)
        print('choque cuadro %d: mano %s de A con mano %s de B: separacion %.1f cm (en %.2f %.2f %.2f)' % (CHOQUE, la, lb, norm(sub(pa, pb)) * 100, *pa))
    peor = (9, 0, '')
    for f in range(0, len(info)):
        for i in (0, 1):
            P = info[f][i]; Po = info[f][1 - i]
            a = al_otro(Po['hips'][0], D); b = al_otro(Po['neck'][0], D)
            for lado in ('R', 'L'):
                for hb in BONE[lado][1:3]:
                    p = P[hb][0]; ab = sub(b, a); t = max(0, min(1, dot(sub(p, a), ab) / dot(ab, ab)))
                    d = norm(sub(p, add(a, mul(ab, t))))
                    if d < peor[0]: peor = (d, f, '%s %s' % ('AB'[i], hb))
    print('brazos: lo mas cerca del eje del torso del otro = %.1f cm (%s, cuadro %d; el torso mide ~12-14 cm de radio)' % (peor[0] * 100, peor[2], peor[1]))
    cab = min((norm(sub(info[f][0]['headc'][0], al_otro(info[f][1]['headc'][0], D))), f) for f in range(len(info)))
    print('cabezas: lo mas cerca = %.1f cm (cuadro %d; una cabeza mide ~19 cm de ancho)' % (cab[0] * 100, cab[1]))
    for i in (0, 1):
        ys = [info[f][i][k][0][1] for f in range(len(info)) for k in ('footA', 'footB')]
        pat = 0.0
        for k in ('footA', 'footB'):
            for f in range(1, len(info)):
                a_ = info[f - 1][i][k][0]; b_ = info[f][i][k][0]
                if a_[1] < 0.085 and b_[1] < 0.085: pat = max(pat, norm(sub(a_, b_)))
        print('jugador %s: tobillos de %.3f a %.3f m de alto; lo maximo que se mueve un pie apoyado en un cuadro = %.1f mm' % ('AB'[i], min(ys), max(ys), pat * 1000))

def pista_inc(Js, nombres):
    """src/motorpista.inc: la cara y las manos de cada clip propio (las pone el Motor mientras corre el clip)"""
    o = ['// generado por tools/gani_cuerpo.py (no editar a mano): cara y manos de los clips propios, por cuadro (30 por segundo)',
         'struct OwnKey { int frame; const char* face; float faceW; const char* handL; const char* handR; };']
    for J, nm in zip(Js, nombres):
        o.append('static const OwnKey kOwn_%s[] = {' % J.nombre)
        for k in J.pista: o.append('    { %d, "%s", %.2ff, "%s", "%s" },' % k)
        o.append('};')
    o.append('struct OwnTrack { const char* file; const OwnKey* keys; int n; };')
    o.append('static const OwnTrack kOwnTracks[] = {')
    for J, nm in zip(Js, nombres): o.append('    { "%s", kOwn_%s, (int)(sizeof kOwn_%s / sizeof kOwn_%s[0]) },' % (nm, J.nombre, J.nombre, J.nombre))
    o.append('};')
    return '\n'.join(o) + '\n'

def main():
    a = sys.argv[1:]
    if len(a) < 2: print(__doc__ or 'uso: gani_cuerpo.py carpeta salida [distancia en el abrazo] [--inc src/motorpista.inc]'); return 1
    base = Base(open(os.path.join(a[0], 'dml_ent_aisle_cmn_idle01.gani'), 'rb').read())
    Dh = float(a[2]) if len(a) > 2 and not a[2].startswith('--') and float(a[2]) > 0 else distancia_auto(base)
    base.extender(N)
    D = round(Dh + 2 * (PASO + WALK), 2)
    print('distancia entre los dos al empezar: %.2f m (choque a %.2f m, abrazo a %.2f m)' % (D, Dh + 2 * PASO, Dh))
    Js = [coreografia(D, 'A', base.pie0), coreografia(D, 'B', base.pie0)]
    poses, info = armar(base, Js, D)
    revisar(info, D)
    os.makedirs(a[1], exist_ok=True); nombres = []
    for J, p in zip(Js, poses):
        b = escribir(base, p)
        nm = 'zfx_saludo_abrazo_%s.gani' % J.nombre.lower(); nombres.append(nm)
        fn = os.path.join(a[1], nm); open(fn, 'wb').write(b)
        G = GC.leer_gani(b)
        print('%s: %d B, %d cuadros, %d canales' % (fn, len(b), G['cuadros'], G['ncanales']))
    if '--inc' in a: open(a[a.index('--inc') + 1], 'w', newline='\n').write(pista_inc(Js, nombres))
    return 0, D

if __name__ == "__main__": r = main(); sys.exit(r[0] if isinstance(r, tuple) else r)
