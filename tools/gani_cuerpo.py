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

# ---------------------------------------------------------------- esqueleto DEL JUEGO: hueso -> (padre, canal de giro, corrimiento en reposo, m)
# (18/11) = body_anim_skel.ask del juego (common/anime/FoxAnim/Body/CharacterAssets, dt13_all.cpk; el mismo en FixDemo), con los canales
# de body_skel.frig: 2 sk_root_hip (+3 lugar), 4 dsk_hip, 6/7/8 muslo / pierna / pie izq, 10/11/12 der, 13 sk_belly, 14 pecho, 15 cuello,
# 16 cabeza, 17 sk_shoulder_l, 19 brazo, 20 antebrazo, 21 mano; 22-26 der (5/9/18/23 = lugares fijos). Reposo de animacion = giro
# identidad en todos (brazos en cruz). Antes era un esqueleto aproximado (pecho 0.22, cuello 0.22, antebrazo 0.26, clavicula al medio del
# pecho): en el juego las manos no caian justas y los cuerpos se metian uno en otro. 'hips' = sk_root_hip (1.096 m, de ahi cuelgan la
# columna y dsk_hip), 'pelvis' = dsk_hip (de ahi cuelgan los muslos). tools/vista_juego.py usa esta misma tabla.
UARM, FARM, HANDO = 0.2900, 0.2900, 0.08      # brazo, antebrazo (largos de los corrimientos del juego), muneca -> centro de la palma
MUSLO, CANILLA = 0.4213, 0.4418
OFF_FARM = (0.2887, 0.0, -0.0278); OFF_HAND = (0.2860, 0.0, 0.0479)          # (brazo izquierdo; el derecho con x al reves)
OFF_KNEE = (-0.0001, -0.4198, 0.0351); OFF_FOOT = (-0.0001, -0.4393, -0.0467)  # (pierna izquierda)
S = {
    'root': (None, 0, (0, 0, 0)), 'hips': ('root', 2, (0, 1.0961, 0)), 'pelvis': ('hips', 4, (0, -0.1040, -0.0184)),
    'thighA': ('pelvis', 6, (0.0900, -0.0321, 0.0713)), 'kneeA': ('thighA', 7, OFF_KNEE), 'footA': ('kneeA', 8, OFF_FOOT),
    'thighB': ('pelvis', 10, (-0.0900, -0.0321, 0.0713)), 'kneeB': ('thighB', 11, (0.0001, OFF_KNEE[1], OFF_KNEE[2])), 'footB': ('kneeB', 12, (0.0001, OFF_FOOT[1], OFF_FOOT[2])),
    'spine': ('hips', 13, (0, 0, 0)), 'chest': ('spine', 14, (0, 0.1667, 0.0138)), 'neck': ('chest', 15, (0, 0.2693, 0.0197)),
    'head': ('neck', 16, (0, 0.1080, 0.0209)),
    'headc': ('head', None, (0, 0.09, 0.03)),
    'clavL': ('chest', 17, (0.1051, 0.2043, 0.0197)), 'uarmL': ('clavL', 19, (0.0899, 0, 0)), 'farmL': ('uarmL', 20, OFF_FARM), 'handL': ('farmL', 21, OFF_HAND),
    'clavR': ('chest', 22, (-0.1051, 0.2043, 0.0197)), 'uarmR': ('clavR', 24, (-0.0899, 0, 0)), 'farmR': ('uarmR', 25, (-OFF_FARM[0], 0, OFF_FARM[2])), 'handR': ('farmR', 26, (-OFF_HAND[0], 0, OFF_HAND[2])),
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
    if f >= keys[-1][0]: return keys[-1][1]
    # (18/11 02:18, Agustin: "un poco mas de fluidez en los movimientos y menos roboticidad") antes cada tramo entre dos claves
    # arrancaba y frenaba (suave - suave): el movimiento se detenia en CADA clave. Ahora es una curva que pasa por las claves sin
    # frenar cuando el movimiento sigue para el mismo lado, y solo frena donde cambia de sentido o se queda quieto (curva cubica
    # monotona: no se pasa de largo)
    n = len(keys); i = 0
    while i + 2 < n and keys[i + 1][0] < f: i += 1
    (f0, v0), (f1, v1) = keys[i], keys[i + 1]; h = float(f1 - f0)
    if h <= 0: return v1
    num = isinstance(v0, (int, float))
    def comp(v): return (v,) if num else v
    def pend(k):
        if k <= 0 or k >= n - 1: return (0.0,) * len(comp(v0))
        (fa, va), (fb, vb), (fc_, vc) = keys[k - 1], keys[k], keys[k + 1]; out = []
        for a, b, c in zip(comp(va), comp(vb), comp(vc)):
            if fb <= fa or fc_ <= fb: out.append(0.0); continue
            d0 = (b - a) / float(fb - fa); d1 = (c - b) / float(fc_ - fb)
            out.append(0.0 if d0 * d1 <= 0 else 2.0 * d0 * d1 / (d0 + d1))
        return tuple(out)
    u = (f - f0) / h; m0 = pend(i); m1 = pend(i + 1)
    h00 = 2 * u ** 3 - 3 * u ** 2 + 1; h10 = u ** 3 - 2 * u ** 2 + u; h01 = -2 * u ** 3 + 3 * u ** 2; h11 = u ** 3 - u ** 2
    r = tuple(h00 * a + h10 * h * ma + h01 * b + h11 * h * mb for a, b, ma, mb in zip(comp(v0), comp(v1), m0, m1))
    return r[0] if num else r

class Jugador:
    """giros: {canal: [(cuadro, (x, y, z) grados)]} encima del clip parado; brazos: {'R'|'L': {'w': [(f, peso)], 'k': [(f, objetivo)]}}
    objetivo = dict(t=('yo'|'otro', hueso, (x, y, z) local del hueso) o 'parado', codo=(x,y,z), dedos=(x,y,z), palma=(x,y,z));
    las direcciones del objetivo van en el espacio del mismo hueso del que se cuelga el punto."""
    def __init__(self, nombre, giros, brazos, raiz=None, pies=None):
        self.nombre = nombre; self.giros = giros; self.brazos = brazos
        self.raiz = raiz or [(0, (0.0, 0.0, 0.0))]           # corrimiento de la raiz (m, en mi modelo)
        self.pies = pies or {'A': [(0, 0.0)], 'B': [(0, 0.0)]}   # adelanto de cada pie (m, z); A = izquierdo, B = derecho

# ---------------------------------------------------------------- PESO DE LA CAMINATA (17/11 20:44, Agustin: "al caminar se nota mas que
# nada, no tiene inercia, peso, fluidez"): la cadera, la columna, el pecho, el cuello y la cabeza toman lo que hacen en la caminata del juego
# (dml_end_spectator01_walk02: captura de movimiento) segun la fase del paso, en vez de las curvas armadas a mano. Tambien la subida /
# bajada y el vaiven al costado de la cadera. Fase 0 = despega el talon izquierdo, 0.5 = el derecho (apoyos medidos 56, 101, 146, ...;
# despegue = apoyo siguiente - 26, como PASO_REF).
NB = 48
MARCHA = {}
def marcha_cargar(carpeta):
    b = Base(open(os.path.join(carpeta, 'dml_end_spectator01_walk02.gani'), 'rb').read())
    ho = [30, 75, 120, 165, 207, 255]
    chs = (2, 13, 14, 15, 16, 17, 19, 20, 21)     # (21:00, Agustin: "las manos moviendose, los hombros") + clavicula y brazo izquierdo (el suelto)
    todos = {ch: [] for ch in chs}; bins = {ch: [[] for _ in range(NB)] for ch in chs}; py = [[] for _ in range(NB)]; px = [[] for _ in range(NB)]
    W = [add(b.root[f], b.hips[f]) for f in range(b.n + 1)]
    # (18/11 02:24, Agustin: "la caminata del azul en el torso se ve un poco saltando entre puntos") medido: una vez por ciclo la cadera
    # saltaba 6-10 grados en UN cuadro (y columna, pecho, cuello y cabeza con ella). Causa: los ciclos de la captura duran 42-48 cuadros
    # y se repartian de a un cuadro en 48 casilleros: 3 casilleros (15, 31 y 47) solo recibian el cuadro del ciclo mas largo y los
    # demas el promedio de 4-5 ciclos -> esos 3 quedaban hasta 14 grados lejos de sus vecinos. Ahora cada ciclo se lee en el centro de
    # CADA casillero (entre cuadros): todos los casilleros promedian los 5 ciclos.
    Wd = []
    for f in range(b.n + 1):
        m = [W[g] for g in range(max(0, f - 22), min(b.n, f + 22) + 1)]         # vaiven: contra el promedio de un ciclo
        Wd.append((W[f][0] - sum(v[0] for v in m) / len(m), W[f][1] - sum(v[1] for v in m) / len(m)))
    # y cada ciclo se cierra sobre si mismo antes de promediar (lo que le falta para terminar igual que empezo se reparte parejo en
    # todo el ciclo): sin esto quedaba un saltito de 2 grados en pecho, cuello y cabeza donde el ciclo vuelve a empezar
    for f0, f1 in zip(ho, ho[1:]):
        cq = {ch: qm(qi(_qen(b, ch, f1)), _qen(b, ch, f0)) for ch in chs}
        cw = (Wd[f0][0] - Wd[f1][0], Wd[f0][1] - Wd[f1][1])
        for i in range(NB):
            uc = (i + 0.5) / NB; f = f0 + uc * (f1 - f0); g = int(f); u = f - g; g1 = min(b.n, g + 1)
            for ch in chs: q = _cerrar(_qen(b, ch, f), cq[ch], uc); todos[ch].append(q); bins[ch][i].append(q)
            py[i].append(Wd[g][1] * (1 - u) + Wd[g1][1] * u + cw[1] * (uc - 0.5)); px[i].append(Wd[g][0] * (1 - u) + Wd[g1][0] * u + cw[0] * (uc - 0.5))
    def media(qs):
        r = qs[0]; acc = (0.0, 0.0, 0.0, 0.0)
        for q in qs: acc = add(acc, q if dot(q, r) >= 0 else mul(q, -1.0))
        return qn(acc)
    for ch in chs:
        mq = qi(media(todos[ch])); MARCHA[ch] = [qm(mq, media(bins[ch][i])) for i in range(NB)]
    # el brazo derecho de walk02 va levantado: el derecho es el izquierdo espejado (x -> -x) y medio ciclo corrido (el otro pie)
    for l, r in ((17, 22), (19, 24), (20, 25), (21, 26)):
        MARCHA[r] = [(q[0], -q[1], -q[2], q[3]) for q in (MARCHA[l][(i + NB // 2) % NB] for i in range(NB))]
    MARCHA['y'] = [sum(v) / len(v) for v in py]; MARCHA['x'] = [sum(v) / len(v) for v in px]
    marcha_ciclos(b, ho)
    brazos_reales(carpeta)
    abrazo_cargar(carpeta)

# (18/11 01:39-01:41, Agustin: "la caminata final y la del principio rehacela... con el estilo de captura de movimiento", "viva", "el
# movimiento de los brazos al caminar no me convence") dos cosas quitaban la captura:
# 1) el cuerpo usaba el PROMEDIO de los ciclos de walk02: todos los pasos salian iguales. Ahora cada paso nuestro toma un ciclo real
#    distinto (MARCHA_C: desvio contra el promedio movil de un ciclo, asi no arrastra los giros lentos de la cabeza hacia la tribuna);
#    cerca del cambio de ciclo se funde con el ciclo promedio para que no haya salto.
# 2) el brazo salia de promediar TODO walk02, y en los cuadros 48-216 ese jugador va aplaudiendo con las manos arriba: el vaiven era
#    mezcla de caminar y aplaudir. Ahora el brazo es el brazo izquierdo de walk02 SOLO en los cuadros en que cuelga y se balancea
#    (224-328), con sus giros tal cual (hombro, codo, muneca: BRAZO_C, dos ciclos reales distintos); el derecho es el izquierdo
#    espejado, medio ciclo corrido y con el OTRO ciclo (no quedan iguales).
MARCHA_C = {}; BRAZO_C = []; BRAZO_M = {}
TOPE_CICLO = {2: 5.0, 13: 5.0, 14: 5.0, 15: 3.5, 16: 3.5}     # cuanto puede apartarse un ciclo real del promedio (grados)
def _qmedia(qs):
    r = qs[0]; acc = (0.0, 0.0, 0.0, 0.0)
    for q in qs: acc = add(acc, q if dot(q, r) >= 0 else mul(q, -1.0))
    return qn(acc)
def _cerrar(q, d, u):
    """q de un ciclo en u (0..1) con el resto del cierre d (= fin^-1 * principio) repartido parejo: mitad para cada lado desde el medio"""
    I = (0.0, 0.0, 0.0, 1.0)
    return qm(q, qslerp(I, d, u - 0.5)) if u >= 0.5 else qm(q, qslerp(I, qi(d), 0.5 - u))
def _qen(b, ch, f):
    f = max(0.0, min(float(b.n), f)); i = int(f); j = min(b.n, i + 1); return qslerp(b.rot[i][ch], b.rot[j][ch], f - i)
def marcha_ciclos(b, ho):
    I = (0.0, 0.0, 0.0, 1.0)
    ciclos = list(zip(ho, ho[1:])) + [(255, 300)]
    for ch in (2, 13, 14, 15, 16):
        mov = [_qmedia([b.rot[g][ch] for g in range(max(0, f - 22), min(b.n, f + 22) + 1)]) for f in range(b.n + 1)]
        MARCHA_C[ch] = []
        for f0, f1 in ciclos:
            cyc = []
            for i in range(NB):
                u = (i + 0.5) / NB; f = f0 + u * (f1 - f0); dev = qm(qi(mov[int(round(f))]), _qen(b, ch, f))
                d = qm(qi(MARCHA[ch][i]), dev); a = qangle(d)
                if a > TOPE_CICLO[ch]: d = qslerp(I, d, TOPE_CICLO[ch] / a)
                cyc.append(qm(MARCHA[ch][i], qslerp(I, d, smooth(min(u, 1.0 - u) / 0.12))))
            MARCHA_C[ch].append(cyc)
    def c1(ch, u): return _qen(b, ch, 255 + u * 45.0)
    def c2(ch, u): return qslerp(_qen(b, ch, 300 + u * 46.0), _qen(b, ch, 207 + u * 48.0), smooth((u - 0.40) / 0.15))
    del BRAZO_C[:]
    for fn in (c1, c2): BRAZO_C.append({ch: [fn(ch, (i + 0.5) / NB) for i in range(NB)] for ch in (17, 19, 20, 21)})
    for ch in (17, 19, 20, 21): BRAZO_M[ch] = _qmedia([q for c in BRAZO_C for q in c[ch]])

# (18/11) medido en la vista: el brazo izquierdo de walk02 se balancea abierto (la mano llega a 43-48 cm del medio del cuerpo: ese
# jugador va girado saludando a la tribuna con la otra mano). dml_end_timeup02_lose_walk01 tiene un ciclo entero (cuadros 60.5-95)
# con LOS DOS brazos colgando y balanceandose pegados al cuerpo (mano a 21-31 cm del medio): se usan esos, cada brazo con su propia
# captura (no espejados). El final del ciclo se funde con los cuadros 56-60 (el ciclo anterior) para que cierre sin salto.
BRAZO_REAL = [False]
def brazos_reales(carpeta):
    fn = os.path.join(carpeta, 'dml_end_timeup02_lose_walk01.gani')
    if not os.path.exists(fn): return
    b = Base(open(fn, 'rb').read()); f0, T = 60.5, 34.5
    # (18/11 02:24) el ciclo no cierra solo: entre el cuadro 95 y el 60.5 el brazo viene girando despacio (hombro 27-42 grados, mano
    # 46-78). Antes ese resto se fundia en los ultimos 5 cuadros del ciclo: un latigazo del brazo por ciclo. Ahora el resto se reparte
    # parejo en TODO el ciclo (mitad para cada lado desde el medio): cierra sin ningun tramo rapido.
    I = (0.0, 0.0, 0.0, 1.0); cierre = {ch: qm(qi(_qen(b, ch, f0 + T)), _qen(b, ch, f0)) for ch in (17, 19, 20, 21, 22, 24, 25, 26)}
    def q_(ch, u): return _cerrar(_qen(b, ch, f0 + u * T), cierre[ch], u)
    del BRAZO_C[:]
    BRAZO_C.append({ch: [q_(ch, (i + 0.5) / NB) for i in range(NB)] for ch in (17, 19, 20, 21, 22, 24, 25, 26)})
    for ch in (17, 19, 20, 21, 22, 24, 25, 26): BRAZO_M[ch] = _qmedia(BRAZO_C[0][ch])
    BRAZO_REAL[0] = True

def marcha_c(ch, fase, ciclo):
    cs = MARCHA_C[ch]; c = cs[ciclo % len(cs)]
    t = (fase % 1.0) * NB - 0.5; i = int(math.floor(t)); u = t - i
    return qslerp(c[i % NB], c[(i + 1) % NB], u)

_ESPEJO = {22: 17, 24: 19, 25: 20, 26: 21}
def brazo_marcha(ch, fase, ciclo, amp):
    """giro del canal del brazo (17 19 20 21 izquierdo, 22 24 25 26 derecho) en la caminata del juego; amp = cuanto vaiven (0 = brazo
    colgando en su pose media de la captura, 1 = como la captura)"""
    der = ch in _ESPEJO; cl = _ESPEJO.get(ch, ch)
    if BRAZO_REAL[0]:
        t = (fase % 1.0) * NB - 0.5; i = int(math.floor(t)); u = t - i; c = BRAZO_C[0][ch]; m = BRAZO_M[ch]
        amp *= 1.0 + 0.08 * math.sin(1.7 * ciclo + (2.0 if der else 0.0))      # ningun balanceo sale igual al anterior
        return qm(m, qslerp((0.0, 0.0, 0.0, 1.0), qm(qi(m), qslerp(c[i % NB], c[(i + 1) % NB], u)), amp))
    if der: fase += 0.5; ciclo += 1
    ciclo += int(math.floor(fase)); fase = fase % 1.0
    t = fase * NB - 0.5; i = int(math.floor(t)); u = t - i
    def de(k):     # el indice puede salir del ciclo: sigue en el ciclo vecino (son seguidos en la captura)
        c = ciclo + (k // NB); return BRAZO_C[c % len(BRAZO_C)][cl][k % NB]
    q = qslerp(de(i), de(i + 1), u); m = BRAZO_M[cl]
    q = qm(m, qslerp((0.0, 0.0, 0.0, 1.0), qm(qi(m), q), amp))
    return (q[0], -q[1], -q[2], q[3]) if der else q

# (21:01, Agustin: "le falta ese toque de captura de movimiento"; subio dml_end_timeup01_win_hug01 + _sub01, abrazo de pareja del juego)
# el cuerpo durante nuestro abrazo (cuadros 186-276) toma lo que hacen cadera, columna, pecho, cuello, cabeza y claviculas en el abrazo
# del juego (cuadros 160-250), como diferencia contra su promedio: se mecen, respiran y se acomodan como en la captura. A = el que
# abraza por arriba (_sub01), B = el de abajo (hug01). Los brazos siguen por IK (las manos tienen que quedar en la espalda del otro).
ABRAZO = {}
AB_JUEGO = (160, 250); AB_NUESTRO = (186, 276)
TOPE_AB = {2: 5.0, 13: 6.0, 14: 6.0, 15: 7.0, 16: 8.0, 17: 6.0, 22: 6.0}   # grados maximos de cada hueso
def abrazo_cargar(carpeta):
    for quien, nm in (('A', 'dml_end_timeup01_win_hug01_sub01.gani'), ('B', 'dml_end_timeup01_win_hug01.gani')):
        fn = os.path.join(carpeta, nm)
        if not os.path.exists(fn): continue
        b = Base(open(fn, 'rb').read()); g0, g1 = AB_JUEGO; d = {}
        for ch in (2, 13, 14, 15, 16, 17, 22):
            qs = [b.rot[f][ch] for f in range(g0, g1 + 1)]; r = qs[0]; acc = (0.0, 0.0, 0.0, 0.0)
            for q in qs: acc = add(acc, q if dot(q, r) >= 0 else mul(q, -1.0))
            mq = qi(qn(acc)); dev = [qm(mq, q) for q in qs]
            mx = max(qangle(q) for q in dev); k = min(1.0, TOPE_AB.get(ch, 8.0) / max(mx, 1e-3))   # el giro que trae se acota (la
            d[ch] = [qslerp((0.0, 0.0, 0.0, 1.0), q, k) for q in dev]                                # captura gira toda la pareja)
        ABRAZO[quien] = d

def abrazo_en(quien, f):
    if quien not in ABRAZO: return None
    a0, a1 = AB_NUESTRO
    if not (a0 < f < a1): return None
    env = smooth((f - a0) / 14.0) * smooth((a1 - f) / 14.0)
    i = int(round((f - a0) * (AB_JUEGO[1] - AB_JUEGO[0]) / float(a1 - a0)))
    return env, {ch: v[min(i, len(v) - 1)] for ch, v in ABRAZO[quien].items()}

def marcha_en(clave, fase):
    t = (fase % 1.0) * NB; i = int(t) % NB; j = (i + 1) % NB; u = t - int(t); a = MARCHA[clave][i]; c = MARCHA[clave][j]
    if isinstance(a, float): return a + (c - a) * u
    return qslerp(a, c, u)

def fases(pL, pR, n, w_lleno=24, w_nada=36, borde=14):
    """fase y peso de la caminata por cuadro, a partir de los despegues de talon de los dos pies"""
    ev = sorted([(a, 0.0) for a, _, _, _ in _tramos(pL)] + [(a, 0.5) for a, _, _, _ in _tramos(pR)])
    fa = []; am = []
    for f in range(n + 1):
        if f <= ev[0][0]:
            g0, g1 = ev[0], ev[1]; gap = g1[0] - g0[0]; ph = g0[1] - 0.5 * (g0[0] - f) / gap; a = max(0.0, 1.0 - (g0[0] - f) / float(borde))
        elif f >= ev[-1][0]:
            g0, g1 = ev[-2], ev[-1]; gap = g1[0] - g0[0]; ph = g1[1] + 0.5 * (f - g1[0]) / gap; a = max(0.0, 1.0 - (f - g1[0] - 26) / float(borde)) if f > g1[0] + 26 else 1.0
        else:
            k = max(i for i in range(len(ev)) if ev[i][0] <= f); g0, g1 = ev[k], ev[k + 1]; gap = g1[0] - g0[0]
            d = (g1[1] - g0[1]) % 1.0 or 1.0
            ph = g0[1] + d * (f - g0[0]) / gap; a = 1.0
        a *= max(0.0, min(1.0, (w_nada - gap) / float(w_nada - w_lleno))) * max(0.0, min(1.0, (gap - 10) / 8.0))   # pasos sueltos muy juntos: no es caminata
        fa.append(ph % 1.0); am.append(a)
    sm = []
    for f in range(n + 1):        # el peso cambia despacio (arranca y frena con inercia)
        v = am[max(0, f - 9):f + 10]; sm.append(sum(v) / len(v))
    return fa, sm

# (18/11 02:26, Agustin: el torso salta al caminar, "en la final se nota") segunda causa medida: debajo de la caminata seguia corriendo
# el clip parado, y ese jugador parado cambia el peso de pierna (la raiz va y viene 6.6 cm a 11 mm por cuadro, la cadera gira 1.4
# grados por cuadro). Caminando de costado eso quedaba hacia adelante y atras: la cadera avanzaba a 12-35 mm por cuadro en vez de
# 18-26, sin relacion con los pasos, y en el cuadro 600 (donde el parado pega la vuelta) cambiaba de golpe. Ahora, mientras camina,
# el clip parado queda quieto en la pose que tenia al arrancar (o al frenar): J.quieto(f) = (cuanto, cuadro de esa pose). Solo la raiz,
# la cadera, la columna y el pecho (lo que salta); el cuello y la cabeza del parado siguen vivos como antes.
def base_de(base, J, f):
    q = getattr(J, 'quieto', None)
    if not q: return base.rot[f], base.root[f]
    w, g = q(f)
    if w < 1e-4: return base.rot[f], base.root[f]
    return {ch: (qslerp(v, base.rot[g][ch], w) if ch in (2, 13, 14) else v) for ch, v in base.rot[f].items()}, lerp(base.root[f], base.root[g], w)

def cuerpo(base, J, f):
    rot = dict(base_de(base, J, f)[0])
    fz = getattr(J, 'fase', None)
    if fz:
        ph, a = fz[0][f], fz[1][f] * J.marcha_amp
        if a > 1e-3:
            # (20:50, Agustin: "se mueve mucho la cadera"): la cadera a la mitad de la captura; el torso y la cabeza van 3 cuadros atrasados
            # respecto de los pies (el peso del cuerpo llega despues del paso)
            # (18/11 01:20, Agustin: "quitar los movimientos roboticos desfasando un poco los movimientos entre las partes del cuerpo")
            # la cadera lidera; columna 2 cuadros despues, pecho 3, cuello 5, cabeza 6 (accion superpuesta); cuanto usa cada parte = estilo
            e = getattr(J, 'estilo', None) or dict(cadera=0.45, torso=0.75, cabeza=0.55)
            ci = getattr(J, 'ciclo', None)
            for ch, k, d in ((2, e['cadera'], 0), (13, e['torso'], 2), (14, e['torso'], 3), (15, e['cabeza'] * 1.1, 5), (16, e['cabeza'], 6)):
                g = max(0, f - d); q = marcha_c(ch, fz[0][g], ci[g] + J.ciclo0) if ci else marcha_en(ch, fz[0][g])
                rot[ch] = qm(rot[ch], qslerp((0.0, 0.0, 0.0, 1.0), q, a * k))
    ab = abrazo_en(J.nombre, f)
    if ab:
        env, d = ab
        for ch, q in d.items(): rot[ch] = qm(rot[ch], qslerp((0.0, 0.0, 0.0, 1.0), q, env * 0.85))
    for ch, keys in J.giros.items(): rot[ch] = qm(rot[ch], qeuler(pista(keys, f)))
    fn = getattr(J, 'giros_fn', None)
    if fn:
        for ch, e in fn(f).items(): rot[ch] = qm(rot[ch], qeuler(e))
    return rot

def al_otro(p, D):    # punto del modelo del otro jugador (enfrentado, a D metros) -> mi modelo
    return (-p[0], p[1], D - p[2])
def dir_otro(v): return (-v[0], v[1], -v[2])

# ---------------------------------------------------------------- IK del brazo
_CORR = {}
def _corr(lado, b):
    """giro que lleva el corrimiento real del hueso hijo al eje x (con el signo del lado)"""
    k = (lado, b)
    if k not in _CORR:
        o = unit(S[b + lado][2]); x = (ARM[lado][4], 0.0, 0.0); c = cross(o, x); d = dot(o, x)
        _CORR[k] = qn((c[0], c[1], c[2], 1.0 + d))
    return _CORR[k]
def _corr_pierna(pie, b):
    k = (pie, b)
    if k not in _CORR:
        o = unit(S[b + pie][2]); x = (0.0, -1.0, 0.0); c = cross(o, x); d = dot(o, x)
        _CORR[k] = qn((c[0], c[1], c[2], 1.0 + d))
    return _CORR[k]
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
    # (18/11) en el esqueleto del juego el codo y la muneca no estan justo sobre el eje x del hueso (z -2.8 / +4.8 cm): se gira cada
    # hueso para que su corrimiento real apunte donde el IK lo quiere
    A = _corr(lado, 'farm'); Bq = _corr(lado, 'hand')
    Qu2 = qm(Qu, A); Qf2 = qm(Qf, Bq)
    return qm(qi(qc), Qu2), qm(qi(Qu2), Qf2), qm(qi(Qf2), Qh), E

def giro_y_dobles(q):
    """mano local -> (giro sobre el eje del antebrazo, doblez de la muneca) en grados"""
    if q[3] < 0: q = tuple(-x for x in q)
    tw = math.degrees(2 * math.atan2(q[0], q[3])); twq = qn((q[0], 0, 0, q[3]))
    return tw, qangle(qm(q, qi(twq)))

DIAG = None
def ik_brazo(lado, P, rot, wrist, codo, dedos, palma, prev=None, obst=None):
    """P = fk del cuerpo (con la clavicula ya girada). El codo se gira alrededor del eje hombro -> muneca (hasta 90 grados) para que la
    muneca no tenga que torcerse de mas (en los clips de Konami la mano gira hasta ~75-100 grados); `prev` = el giro elegido en el cuadro
    anterior, para que no salte. Devuelve (q brazo local, q antebrazo local, q mano local, codo, giro del codo elegido)."""
    bc, bu = BONE[lado][0], BONE[lado][1]
    Sh = P[bu][0]; qc = P[bc][1]; dn = unit(sub(wrist, Sh))
    mejor = None
    for o in range(-120, 121, 3) if obst else range(-90, 91, 3):
        mejor = _prueba(lado, Sh, qc, wrist, codo, dedos, palma, prev, obst, dn, o, mejor)
    # (18/11 01:14, Agustin: "todavia se ven brazos atravesando el torso") si abriendo el codo no alcanza, la muneca se corre hacia
    # afuera del cuerpo del otro (de a 2 cm, hasta 12): mejor una mano un poco mas afuera que un brazo adentro del otro
    if obst and mejor[3] > 0.004:
        sal = obst.afuera(wrist)
        for k in range(1, 7):
            w2 = add(wrist, mul(sal, 0.02 * k)); dn2 = unit(sub(w2, Sh))
            for o in range(-120, 121, 6): mejor = _prueba(lado, Sh, qc, w2, codo, dedos, palma, prev, obst, dn2, o, mejor, 300.0 * 0.02 * k)
    if obst and DIAG is not None: DIAG.append(mejor[3])
    return mejor[2] + (mejor[1],)

def _prueba(lado, Sh, qc, wrist, codo, dedos, palma, prev, obst, dn, o, mejor, extra=0.0):
    if True:
        pp = qrot(qaxis(dn, o), codo)
        r = _ik(lado, Sh, qc, wrist, pp, dedos, palma)
        tw, sw = giro_y_dobles(r[2])
        c = abs(tw) + 0.6 * sw + 0.4 * abs(o) + (1.5 * abs(o - prev) if prev is not None else 0.0)
        # (18/11, Agustin: "se atraviesan muchas veces con los brazos") el codo se abre para que brazo y antebrazo no entren en el
        # cuerpo ni en los brazos del otro (1 cm adentro cuesta como 20 grados de muneca); limites humanos: muneca torcida > 80 y
        # doblada > 75 grados cuestan mucho
        pen = obst(Sh, r[3], wrist) if obst else 0.0
        c += 5000.0 * pen + extra
        c += 8.0 * max(0.0, abs(tw) - 80.0) + 8.0 * max(0.0, sw - 75.0)
        if mejor is None or c < mejor[0]: mejor = (c, o, r, pen)
    return mejor

PIERNA = 0.5 * (MUSLO + CANILLA)
def ik_pierna(pie, P, T, Qpie, ref):
    """pierna de dos huesos (muslo libre, rodilla solo en x como en los clips de Konami) hasta el tobillo T; el plano de la rodilla sale
    del muslo del clip parado (ref = su eje x en el mundo), el pie queda con el giro Qpie del clip parado (plano en el piso).
    (18/11) muslo y canilla con sus largos del juego (0.421 / 0.442)"""
    th = 'thigh' + pie; H = P[th][0]; Qh = P['hips'][1]
    d = sub(T, H); L = max(abs(CANILLA - MUSLO) + 0.05, min(norm(d), (MUSLO + CANILLA) * 0.999)); dn = unit(d)
    x = unit(perp(ref, dn))
    al = math.acos(max(-1.0, min(1.0, (MUSLO * MUSLO + L * L - CANILLA * CANILLA) / (2 * MUSLO * L))))      # muslo <-> recta cadera-tobillo
    be = math.acos(max(-1.0, min(1.0, (MUSLO * MUSLO + CANILLA * CANILLA - L * L) / (2 * MUSLO * CANILLA))))  # angulo en la rodilla
    d1 = qrot(qaxis(x, -math.degrees(al)), dn)            # la rodilla va adelante
    yw = mul(d1, -1.0); zw = cross(x, yw)
    Qt = qmat(x, yw, zw)
    qk = qaxis((1, 0, 0), math.degrees(math.pi - be))
    Qs = qm(Qt, qk)
    Qt2 = qm(Qt, _corr_pierna(pie, 'knee')); Qs2 = qm(Qs, _corr_pierna(pie, 'foot'))     # (18/11) rodilla y tobillo del juego no van justo en -y
    Qp = P['pelvis'][1] if 'pelvis' in P else Qh
    return qm(qi(Qp), Qt2), qm(qi(Qt2), Qs2), qm(qi(Qs2), Qpie), norm(d) - L

# (20:55, Agustin: "que sea adaptable para la altura de cada jugador", "mostrame una vista previa con distintas alturas") estatura de
# cada jugador (1 = la del clip). Los puntos de encuentro ('mundo': choque, apreton) quedan a la altura media de los dos y los del otro
# ('otro': la espalda en el abrazo) se toman del cuerpo del otro a su tamano; todo pasado a mi modelo (dividido por mi estatura).
ESCALA = [1.0, 1.0]; YO = [0]
def objetivo_mundo(o, Pyo, Potro, D):
    """punto de la muneca + direcciones en mi modelo"""
    lado_hueso = o['t']
    quien, hueso, loc = lado_hueso
    P = Pyo if quien == 'yo' else Potro
    pos, q = ((0, 0, 0), (0, 0, 0, 1)) if quien == 'mundo' else P[hueso]
    pw = add(pos, qrot(q, loc)); cd = qrot(q, o['codo']); dd = qrot(q, o['dedos']); pd = qrot(q, o['palma'])
    sy = ESCALA[YO[0]]; so = ESCALA[1 - YO[0]]; sm = 0.5 * (sy + so)
    if quien == 'otro': pw = mul(al_otro(mul(pw, so), D * sm), 1.0 / sy); cd = dir_otro(cd); dd = dir_otro(dd); pd = dir_otro(pd)
    elif quien == 'mundo': pw = mul(pw, sm / sy)
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
    for i_, ((f0, o0), (f1, o1)) in enumerate(zip(k, k[1:])):
        if f <= f1:
            u = (f - f0) / float(f1 - f0); a, b = res(o0), res(o1)
            # (18/11 02:18, fluidez) la mano ya no frena en cada clave: si la clave es de paso (antes y despues va a otro lugar) la
            # cruza con velocidad; solo arranca / frena suave donde el objetivo se queda (dos claves seguidas iguales) o en las puntas.
            # El choque de punos (296-340) queda como estaba (Agustin: "el punito esta bien")
            def quieta(j): return j <= 0 or j >= len(k) - 1 or k[j][1] is k[j - 1][1] or k[j][1] is k[j + 1][1]
            if 288 <= f0 and f1 <= 340: s_ = smooth(u)     # el punito y su llegada (desde que suelta el brazo del otro) no se tocan
            else:
                m0 = 0.0 if quieta(i_) else 0.85; m1 = 0.0 if quieta(i_ + 1) else 0.85
                s_ = (u ** 3 - 2 * u ** 2 + u) * m0 + (-2 * u ** 3 + 3 * u ** 2) + (u ** 3 - u ** 2) * m1
            m = mezcla_obj(a, b, s_)
            # (18/11, Agustin: principios de animacion, ARCOS) la muneca no va en linea recta de una clave a la otra: el camino se
            # curva hacia afuera y arriba (como gira el brazo desde el hombro), mas cuanto mas largo el viaje; cero en las claves
            d = sub(b[0], a[0]); L = norm(d)
            if L > 0.12 and not (getattr(o0, 'balanceo', False) and getattr(o1, 'balanceo', False)):
                sg = ARM[lado][4]; bu = qrot(Pyo['root'][1], (sg * 0.55, 0.75, -0.2)); bu = perp(bu, unit(d))
                if norm(bu) > 1e-6: m = (add(m[0], mul(unit(bu), 0.14 * L * math.sin(math.pi * s_))),) + tuple(m[1:])
            return m
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

# (20:44, Agustin: "respetar los principios de la animacion, inercia") resorte por hueso, (canal, frecuencia por cuadro, amortiguacion):
# menos amortiguado y mas lento cuanto mas lejos del cuerpo = el brazo sigue, la mano llega despues y se pasa un poco (seguimiento y
# accion superpuesta); la columna arrastra al cuello y a la cabeza. ANTICIPACION: antes de un movimiento grande el hueso va un poco al reves.
# (20:51, Agustin: "a los brazos les falta un movimiento mas de pendulo, asi como el cuello y la cabeza"): menos amortiguados y mas lentos;
# el cuello y la cabeza siguen sueltos tambien en el abrazo (SIEMPRE_LIBRE: no tocan nada del otro)
RESORTES = ((13, 0.34, 0.80), (14, 0.28, 0.70), (15, 0.20, 0.48), (16, 0.16, 0.42), (17, 0.28, 0.65), (22, 0.28, 0.65),
            (19, 0.22, 0.45), (20, 0.19, 0.42), (21, 0.15, 0.40), (24, 0.22, 0.45), (25, 0.19, 0.42), (26, 0.15, 0.40))
SIEMPRE_LIBRE = (15, 16)
ANTICIPA = {19: 0.22, 24: 0.22, 14: 0.15, 20: 0.12, 25: 0.12}
def filtrar(salida, J, chs=RESORTES):
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
    for ch, w, z in chs:
        src = [salida[f][ch] for f in range(n + 1)]
        for f in range(1, n + 1):
            if dot(src[f - 1], src[f]) < 0: src[f] = tuple(-c for c in src[f])
        k = ANTICIPA.get(ch, 0.0); L = 7
        x = src[0]; v = (0.0, 0.0, 0.0, 0.0)
        for f in range(n + 1):
            y = src[f]
            if k: y = qn(add(y, mul(sub(y, src[min(n, f + L)]), k * libre(min(n, f + L)))))   # se carga al reves de lo que viene
            if dot(x, y) < 0: y = tuple(-c for c in y)
            a = add(mul(sub(y, x), w * w), mul(v, -2.0 * z * w)); v = add(v, a); x = add(x, v)
            m = max(0.45, libre(f)) if ch in SIEMPRE_LIBRE else libre(f); out = qn(lerp(src[f], qn(x), m))
            if m < 1.0: x = lerp(x, src[f], 1.0 - m); v = mul(v, m)
            salida[f][ch] = out

_SUELTO = {}
def brazo_suelto(J, lado):
    k = (id(J), lado)
    if k not in _SUELTO:
        ks = J.brazos[lado]['k']; n = N; v = []
        for f in range(n + 1):
            a = max([x for x in ks if x[0] <= f], key=lambda x: x[0], default=ks[0]); b = min([x for x in ks if x[0] > f], key=lambda x: x[0], default=ks[-1])
            v.append(1.0 if getattr(a[1], 'balanceo', False) and getattr(b[1], 'balanceo', False) else 0.0)
        _SUELTO[k] = [sum(v[max(0, f - 8):f + 9]) / len(v[max(0, f - 8):f + 9]) for f in range(n + 1)]
    return _SUELTO[k]

def puntas(i, f):
    dif = ESCALA[1 - i] - ESCALA[i]
    if dif <= 0.01: return 0.0
    h = min(0.06, 0.55 * (dif - 0.01)) / ESCALA[i]; w = 0.0
    for a, b in ((CHOQUE - 16, CHOQUE + 12), (176, 280)): w = max(w, smooth((f - a) / 10.0) * smooth((b - f) / 12.0))
    return h * w

# (21:07, Agustin: "evita que se vea vibracion en el abrazo") del abrazo al apreton cada hueso de la mitad de arriba se promedia con sus
# 3 cuadros vecinos de cada lado (pesos 1 2 3 4 3 2 1): saca los tirones de cuadro a cuadro del IK y de la captura sin mover las manos
# mas de un par de milimetros (las metas cambian despacio); con entrada y salida suaves
def suavizar(salida, a, b, chs=tuple(range(13, 27)), w=(1, 2, 3, 4, 3, 2, 1)):
    n = len(salida) - 1; h = len(w) // 2
    src = {ch: [salida[f][ch] for f in range(n + 1)] for ch in chs if ch in salida[0]}
    for ch, q in src.items():
        for f in range(1, n + 1):
            if dot(q[f - 1], q[f]) < 0: q[f] = mul(q[f], -1.0)
    for f in range(max(h, a - 8), min(n - h, b + 8) + 1):
        m = smooth((f - (a - 8)) / 8.0) * smooth(((b + 8) - f) / 8.0)
        for ch, q in src.items():
            acc = (0.0, 0.0, 0.0, 0.0)
            for k, wk in enumerate(w): acc = add(acc, mul(q[f + k - h], wk))
            salida[f][ch] = qn(lerp(q[f], qn(acc), m))

# ---------------------------------------------------------------- armado de los dos clips
def _dseg(p, a, b):
    ab = sub(b, a); t = max(0.0, min(1.0, dot(sub(p, a), ab) / max(dot(ab, ab), 1e-9))); return norm(sub(p, add(a, mul(ab, t))))

def hacer_obst(Po, brazos_otro, D):
    """cuanto (m) entran brazo y antebrazo en el torso del otro (elipse medida en la camiseta del juego: 0.20 de ancho a la altura del
    pecho, 0.16 abajo, 0.13 de fondo, centro 3.6 cm delante del hueso del pecho) y en sus brazos (cuadro anterior)"""
    c, q = Po['chest']; qinv = qi(q)
    def fn(Sh, E, Wr):
        pen = 0.0
        for a, b, r, ts in ((Sh, E, 0.055, (0.2, 0.45, 0.7, 0.95)), (E, Wr, 0.045, (0.15, 0.4, 0.65))):
            for t in ts:
                po = al_otro(lerp(a, b, t), D); l = qrot(qinv, sub(po, c))
                if -0.40 < l[1] < 0.28:
                    hw = (0.16 if l[1] < -0.05 else 0.205) + r + 0.01; hd = 0.13 + r + 0.01
                    e = math.sqrt((l[0] / hw) ** 2 + ((l[2] - 0.036) / hd) ** 2)
                    if e < 1.0: pen += (1.0 - e) * min(hw, hd)
                for s0, s1, r2 in brazos_otro:
                    d = _dseg(po, s0, s1)
                    if d < r + r2: pen += r + r2 - d
        return pen
    def afuera(w):
        l = qrot(qinv, sub(al_otro(w, D), c)); d = unit((l[0], 0.0, l[2] - 0.036))
        return dir_otro(qrot(q, d))
    fn.afuera = afuera
    return fn

def despegar_brazos(base, Js, salida, raices):
    """(18/11 01:34, Agustin: "en la caminata final las manos y los brazos atraviesan el torso") medido con las mallas: al caminar la
    mano y el antebrazo entraban hasta 6 cm en la propia cadera / camiseta (el vaiven de la caminata del juego encima del brazo
    colgando). Para cada cuadro se busca cuanto hay que ABRIR el brazo desde el hombro (giro alrededor del eje adelante de la cadera)
    para que codo, antebrazo, muneca, palma y dedos queden fuera del cuerpo (elipse de la camiseta + short, con margen); el angulo se
    suaviza en el tiempo y se aplica al brazo. En los contactos (saludo, abrazo, punos) no se toca."""
    n = len(salida[0]) - 1
    for i, J in enumerate(Js):
        def libre(f):
            for a, b in getattr(J, 'contacto', None) or []:
                if a - 8 <= f <= b + 8: return False
            return True
        for lado in ('L', 'R'):
            clav, ua, fa, ha, sg = ARM[lado]; bc, bu, bf, bh = BONE[lado]; need = [0.0] * (n + 1)
            for f in range(n + 1):
                if not libre(f): continue
                P = fk(salida[i][f], raices[i][f], base.hips[f]); hp, hq = P['hips']; hqi = qi(hq); Sh = P[bu][0]
                E = P[bf][0]; Wr, qh = P[bh]; pal = add(Wr, qrot(qh, (sg * HANDO, 0.0, 0.0))); ded = add(Wr, qrot(qh, (sg * 0.17, 0.0, 0.0)))
                pts = [(E, 0.045), (lerp(E, Wr, 0.5), 0.042), (Wr, 0.038), (pal, 0.042), (ded, 0.022)]
                eje = qrot(hq, (0.0, 0.0, 1.0))
                def adentro(th):
                    q = qaxis(eje, sg * th); peor = 0.0
                    for p_, r_ in pts:
                        l = qrot(hqi, sub(add(Sh, qrot(q, sub(p_, Sh))), hp))
                        if not (-0.42 < l[1] < 0.30): continue
                        hw = (0.19 if l[1] < -0.02 else 0.175) + r_ + 0.012; hd = 0.14 + r_ + 0.012
                        e = math.sqrt((l[0] / hw) ** 2 + ((l[2] - 0.05) / hd) ** 2)
                        if e < 1.0: peor = max(peor, (1.0 - e) * min(hw, hd))
                    return peor
                th = 0.0
                while th < 36.0 and adentro(th) > 0.0: th += 1.5
                # (18/11) el brazo que va suelto es el de la captura del juego: medido con las mallas no entra en el cuerpo (la elipse
                # de aca es mas gorda que la camiseta); solo se abre lo armado a mano (gestos)
                need[f] = th * (1.0 - (brazo_suelto(J, lado)[f] if getattr(J, 'ciclo', None) else 0.0))
            if max(need) <= 0.0: continue
            mx = [max(need[max(0, f - 4):f + 5]) for f in range(n + 1)]                     # se abre antes y se cierra despues
            sm = [sum(mx[max(0, f - 6):f + 7]) / len(mx[max(0, f - 6):f + 7]) for f in range(n + 1)]
            for f in range(n + 1):
                if sm[f] < 0.05: continue
                P = fk(salida[i][f], raices[i][f], base.hips[f]); qc = P[bc][1]; eje = qrot(P['hips'][1], (0.0, 0.0, 1.0))
                salida[i][f][ua] = qn(qm(qm(qi(qc), qm(qaxis(eje, sg * sm[f]), qc)), salida[i][f][ua]))
            print('brazo %s de %s: se abre hasta %.0f grados para no entrar en el propio cuerpo (%d cuadros)' % (
                'izq' if lado == 'L' else 'der', J.nombre, max(sm), sum(1 for x in sm if x > 0.5)))

MUNECA_DOBLA, MUNECA_GIRA = 70.0, 85.0
def mano_humana(q):
    """(18/11) la muneca no dobla mas de MUNECA_DOBLA ni gira mas de MUNECA_GIRA grados (lo de mas se pierde: la mano queda un poco
    menos orientada que lo pedido pero en una pose posible)"""
    if q[3] < 0: q = tuple(-x for x in q)
    twq = qn((q[0], 0.0, 0.0, q[3])); sw = qm(q, qi(twq))
    tw = math.degrees(2 * math.atan2(twq[0], twq[3])); sa = qangle(sw)
    if abs(tw) > MUNECA_GIRA: twq = qaxis((1, 0, 0), math.copysign(MUNECA_GIRA, tw))
    if sa > MUNECA_DOBLA: sw = qslerp((0.0, 0.0, 0.0, 1.0), sw, MUNECA_DOBLA / sa)
    return qn(qm(sw, twq))

def limites(poses):
    """(18/11, Agustin: "que la forma que doblen las manos, brazos sea humanamente posible") codo: solo dobla para su lado, 0-145
    grados; muneca: giro (pronacion) hasta ~85, doblez hasta ~75"""
    for i, ps in enumerate(poses):
        for lado, (fa, ha) in (('izq', (20, 21)), ('der', (25, 26))):
            cod = []; tw_ = []; sw_ = []; malos = []
            for f in range(len(ps)):
                r = ps[f]; q = r[fa]; ang = qangle(q); sgn_ = q[1] * (1 if q[3] >= 0 else -1) * (1 if lado == 'der' else -1)
                tw, sw = giro_y_dobles(r[ha]); cod.append(ang); tw_.append(abs(tw)); sw_.append(sw)
                if ang > 145 or abs(tw) > 90 or sw > 80: malos.append(f)
            print('limites jugador %s brazo %s: codo hasta %.0f, muneca giro hasta %.0f y doblez hasta %.0f grados; cuadros fuera de lo humano: %d%s' % (
                'AB'[i], lado, max(cod), max(tw_), max(sw_), len(malos), (' (%d-%d)' % (malos[0], malos[-1])) if malos else ''))

def armar(base, Js, D):
    n = base.n; salida = [dict() for _ in Js]; info = []; prev = {}; raices = [[] for _ in Js]; self_estira = [0.0]; brazos_prev = {}
    for f in range(n + 1):
        Pp = fk(base.rot[f], base.root[f], base.hips[f])     # el clip parado (su muslo da el plano de la rodilla)
        if f == 0: P0 = Pp                                   # los pies quedan donde estan en el cuadro 0 del parado (fijos, + el paso)
        rots = []; Ps = []
        for i, J in enumerate(Js):
            rp = add(base_de(base, J, f)[1], J.raiz(f) if callable(J.raiz) else pista(J.raiz, f))
            # (20:58, Agustin: "si uno es mas alto, que el otro levante la punta de los pies para alcanzarlo en el choque alto y en el abrazo;
            # con la misma altura eso no pasa") puntas de pie del mas bajo: talon arriba segun la diferencia de estatura (hasta 6 cm)
            tp = puntas(i, f); rp = add(rp, (0.0, tp, 0.0)); raices[i].append(rp)
            r = cuerpo(base, J, f); P = fk(r, rp, base.hips[f])
            for pie, (ct, ck, cp) in (('A', (6, 7, 8)), ('B', (10, 11, 12))):
                T, Qp = pie_en(J.pies[pie], f, P0['foot' + pie][0][1], P0['foot' + pie][1], 1.0 if pie == 'A' else -1.0)
                if tp > 1e-4:     # el pie gira sobre la punta: tobillo arriba y punta abajo
                    yw = math.radians(huella(J.pies[pie], f)[2]); th = math.degrees(math.asin(min(0.9, tp / 0.13)))
                    T = add(T, (0.0, tp, 0.0)); Qp = qm(qaxis((math.cos(yw), 0.0, -math.sin(yw)), th), Qp)
                ref = qrot(qm(P['root'][1], qi(Pp['root'][1])), qrot(Pp['thigh' + pie][1], (1, 0, 0)))   # con el giro de la raiz
                r[ct], r[ck], r[cp], est = ik_pierna(pie, P, T, Qp, ref)
                self_estira[0] = max(self_estira[0], est)
            rots.append(r); Ps.append(fk(r, rp, base.hips[f]))
        fin = []
        for i, J in enumerate(Js):
            r = dict(rots[i]); P = Ps[i]; Po = Ps[1 - i]; YO[0] = i
            cerca = norm(sub(al_otro(P['hips'][0], D), Po['hips'][0])) < 1.4
            obst = hacer_obst(Po, brazos_prev.get(1 - i, []), D) if cerca else None
            for lado in ('R', 'L'):
                w = pista(J.brazos[lado]['w'], f)
                clav, ua, fa, ha, sg = ARM[lado]
                if getattr(J, 'ciclo', None):     # el hombro de la caminata de la captura, ANTES del IK (asi la mano llega igual)
                    e = J.estilo; g = max(0, f - e.get('atraso_' + lado, 0))
                    mcl = BRAZO_M[17]; mcl = BRAZO_M[clav] if BRAZO_REAL[0] else mcl if lado == 'L' else (mcl[0], -mcl[1], -mcl[2], mcl[3])
                    dcl = qm(qi(mcl), brazo_marcha(clav, J.fase[0][g], J.ciclo[g] + J.ciclo0, 1.0))
                    r[clav] = qm(r[clav], qslerp((0.0, 0.0, 0.0, 1.0), dcl, min(1.4, J.fase[1][g] * J.marcha_amp * e['clav'])))
                    P = fk(r, raices[i][f], base.hips[f])
                if w <= 0: continue
                W, cd, dd, pd = brazo_en(J, lado, f, P, Po, D)
                # (18/11 00:51, Agustin: "que los trapecios tambien se muevan") ritmo del hombro: al levantar el brazo por encima de
                # ~55 grados la clavicula sube (hasta 18) y al ir adelante / cruzar se adelanta (hasta 12): el hombro lidera al brazo
                Sh = P[BONE[lado][1]][0]; lf = qrot(qi(P['root'][1]), unit(sub(W, Sh)))
                sube = math.degrees(math.acos(max(-1.0, min(1.0, -lf[1]))))
                up_ = max(0.0, min(18.0, 0.30 * (sube - 55.0))); pro = max(0.0, min(12.0, 9.0 * max(0.0, lf[2]) * min(1.0, sube / 60.0) + 8.0 * max(0.0, -sg * lf[0])))
                r[clav] = qm(r[clav], qeuler((0.0, -sg * pro, sg * up_))); P = fk(r, raices[i][f], base.hips[f])
                qu, qf, qh, E, og = ik_brazo(lado, P, r, W, cd, dd, pd, prev.get((i, lado)), obst)
                # (18/11 01:49, Agustin: "las manos se separan mucho del torso y la pose del abrazo es poco natural") en el abrazo la mano
                # pedia los dedos cruzados a la espalda con el antebrazo llegando de arriba: la muneca no dobla 90 grados y la mano quedaba
                # de canto, con los dedos hacia afuera. Ahora los dedos siguen la linea del antebrazo, acostados sobre la espalda
                wp = J.plano(f) if getattr(J, 'plano', None) else 0.0
                if wp > 0.01:
                    dp = perp(unit(sub(W, E)), unit(pd))
                    if norm(dp) > 0.3:
                        qu, qf, qh, E, og = ik_brazo(lado, P, r, W, cd, vslerp(unit(dd), unit(dp), 0.9 * wp), pd, prev.get((i, lado)), obst)
                prev[(i, lado)] = og
                r[ua] = qslerp(r[ua], qu, w); r[fa] = qslerp(r[fa], qf, w); r[ha] = qslerp(r[ha], qh, w)
            if getattr(J, 'fase', None):     # brazos y hombros de la caminata del juego encima (solo con el brazo colgando; hombros siempre)
                ph = J.fase[0][f]; am = J.fase[1][f] * J.marcha_amp
                if am > 1e-3:
                    # (18/11) desfase entre partes: hombro 2 cuadros despues de la cadera, brazo 3, antebrazo 5, mano 7 (la mano llega
                    # ultima: seguimiento); amplitud segun el estilo de cada jugador
                    e = getattr(J, 'estilo', None) or dict(brazos=1.0, clav=1.0); fa_ = J.fase[0]
                    ci = getattr(J, 'ciclo', None)
                    if not ci:
                        for lado, chs_ in (('L', (19, 20, 21)), ('R', (24, 25, 26))):
                            wb = min(1.3, brazo_suelto(J, lado)[f] * am * e['brazos'])
                            for ch, d in zip(chs_, (3, 5, 7)): r[ch] = qm(r[ch], qslerp((0.0, 0.0, 0.0, 1.0), marcha_en(ch, fa_[max(0, f - d)]), wb))
                        for ch in (17, 22): r[ch] = qm(r[ch], qslerp((0.0, 0.0, 0.0, 1.0), marcha_en(ch, fa_[max(0, f - 2)]), min(1.4, am * e['clav'])))
                if getattr(J, 'ciclo', None):
                    # (18/11 01:41) brazo de la captura tal cual (ver marcha_ciclos): cuando el brazo va suelto sus giros SON los de la
                    # captura (no una capa encima del brazo colgando por IK); parado queda en la pose media de la captura. El desfase
                    # entre hombro, codo y mano ya viene en la captura; el estilo cambia cuanto vaiven y unos cuadros de atraso
                    e = J.estilo; ci = J.ciclo; fa_ = J.fase[0]; I4 = (0.0, 0.0, 0.0, 1.0)
                    for lado, chs_ in (('L', (17, 19, 20, 21)), ('R', (22, 24, 25, 26))):
                        s = brazo_suelto(J, lado)[f]; g = max(0, f - e.get('atraso_' + lado, 0))
                        amp = min(1.25, J.fase[1][g] * e['brazos'] * e.get('brazo_' + lado, 1.0))
                        if s > 1e-3:
                            for ch in chs_[1:]: r[ch] = qslerp(r[ch], brazo_marcha(ch, fa_[g], ci[g] + J.ciclo0, amp), s)
            fin.append(r)
        for i in range(len(Js)):
            salida[i][f] = fin[i]; Pf = fk(fin[i], raices[i][f], base.hips[f])
            brazos_prev[i] = [(Pf[a][0], Pf[b][0], rr) for a, b, rr in (('uarmL', 'farmL', 0.055), ('farmL', 'handL', 0.045), ('uarmR', 'farmR', 0.055), ('farmR', 'handR', 0.045))]
        for i in range(len(Js)): fin[i]['raiz'] = raices[i][f]
    print('piernas: lo que le falto a la pierna mas estirada para llegar al tobillo = %.1f mm' % (self_estira[0] * 1000))
    for i, J in enumerate(Js):
        antes = [{ch: salida[i][f][ch] for ch in (19, 20, 21, 24, 25, 26)} for f in range(n + 1)]
        if getattr(J, 'contacto', None) is not None: filtrar([salida[i][f] for f in range(n + 1)], J)
        if getattr(J, 'ciclo', None):     # el brazo de la captura ya trae su peso: el resorte solo actua en lo armado a mano
            for lado, chs_ in (('L', (19, 20, 21)), ('R', (24, 25, 26))):
                su = brazo_suelto(J, lado)
                for f in range(n + 1):
                    if su[f] > 1e-3:
                        for ch in chs_: salida[i][f][ch] = qslerp(salida[i][f][ch], antes[f][ch], su[f])
        suavizar([salida[i][f] for f in range(n + 1)], 176, 306)
        for f in range(n + 1):
            for ch in (21, 26): salida[i][f][ch] = mano_humana(salida[i][f][ch])
    if os.environ.get('DESPEGAR', '1') != '0': despegar_brazos(base, Js, salida, raices)
    if CORTE: cortar(base, salida, raices); n = base.n
    info = [[fk(salida[i][f], raices[i][f], base.hips[f]) for i in range(len(Js))] for f in range(n + 1)]
    return salida, info

# (18/11 01:49, Agustin: "el abrazo es muy largo") el abrazo quieto dura 28 cuadros menos: del clip armado se sacan los cuadros
# CORTE (en el medio del abrazo, con los pies quietos) y los 6 cuadros de cada lado se funden. fc(f) = cuadro nuevo de un cuadro viejo.
CORTE = (234, 262); FUNDE = 6
def fc(f):
    if not CORTE: return f
    a, b = CORTE; return f if f < a else max(a, f - (b - a))
def cortar(base, salida, raices):
    a, b = CORTE; d = b - a; m = base.n - d
    def w_(g): return 0.0 if g < a - FUNDE else 1.0 if g > a + FUNDE else smooth((g - (a - FUNDE)) / (2.0 * FUNDE))
    for i in range(len(salida)):
        nuevo = {}
        for g in range(m + 1):
            w = w_(g); s1 = salida[i][g]; s2 = salida[i][g + d]
            if w <= 0.0: nuevo[g] = s1
            elif w >= 1.0: nuevo[g] = s2
            else: nuevo[g] = {ch: (lerp(v, s2[ch], w) if ch == 'raiz' else qslerp(v, s2[ch], w)) for ch, v in s1.items()}
        salida[i] = nuevo; raices[i][:] = [nuevo[g]['raiz'] for g in range(m + 1)]
    base.rot = [base.rot[g] if w_(g) < 0.5 else base.rot[g + d] for g in range(m + 1)]
    base.root = [lerp(base.root[g], base.root[g + d], w_(g)) for g in range(m + 1)]
    base.hips = [lerp(base.hips[g], base.hips[g + d], w_(g)) for g in range(m + 1)]
    base.n = m

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
N = 780            # cuadros (26 s; 18/11 Agustin: "que la parte de caminando y charlando sea mas larga", antes 540)
WALK = 2.44        # lo que camina cada uno hasta el otro (desde el lugar del cuadro 0)
PASO = 0.22        # el paso de cada uno para el abrazo (y el de vuelta)
SEP_CAMINA = 0.86   # (18/11) de cadera a cadera al irse caminando juntos (antes 0.66)
SEP_PUNO = 1.02     # (18/11) de cadera a cadera en el choque de punos (despues de dos pasos atras)
CHOQUE = 150       # (18/11 01:17, Agustin) saludo con UNA mano: se agarran la derecha a la altura del pecho y A tira de B al abrazo (antes: choque de las dos manos arriba)
PASO_TIRA, PASO_VIENE = 0.14, 0.30    # paso al abrazo del que tira (B, el azul) y del que es atraido (A); suman 2 x PASO
def campana(f, a, p, b):     # 0 en a, 1 en p, 0 en b (sube y baja suave; a < p < b)
    if f <= a or f >= b: return 0.0
    return smooth((f - a) / float(p - a)) if f <= p else 1.0 - smooth((f - p) / float(b - p))
CHOQUE2 = 312      # (20:13-20:14) apreton de manos al soltar el abrazo; (18/11, Agustin) ahora saludo de punito (choque de punos)

# (18/11 00:41, Agustin: "que la parte de caminando y charlando sea mas larga y que se miren al hablar, asi se ve robotico") charla al
# irse: hablan por turnos (A, B, A, B) y se rien juntos. El que escucha mira al que habla (cabeza + cuello ~45 grados y el pecho
# un poco), asiente de a ratos; el que habla mira al otro y de a ratos adelante (por donde camina). Golpes de la mano desparejos.
HABLA_A = [(392, 446), (566, 620)]
HABLA_B = [(458, 516), (632, 694)]
RISA = 702
RITMO_A = [18, 14, 22, 16, 24, 13, 20]
RITMO_B = [21, 15, 17, 25, 14, 19, 23]

def mirada_caminando(A, sgn):
    """claves (cuadro, (asiente, gira, inclina)) de la cabeza al caminar charlando; gira < 0 = hacia el otro (con sgn)"""
    yo, el = (HABLA_A, HABLA_B) if A else (HABLA_B, HABLA_A)
    def en(r, f): return any(a <= f <= b for a, b in r)
    ks = []; f = 378; k = 0
    while f <= N:
        h = (k * 7 + (3 if A else 5)) % 11
        if en(el, f): y, x = -44 - (h % 3) * 3, (4.0 if h % 3 == 0 else 0.5) - (1.5 if h % 4 == 1 else 0)   # escucha: lo mira y asiente
        elif en(yo, f): y, x = (-40 - h % 4 if h % 5 < 3 else -12), (2.5 if h % 2 else -1.0)            # habla: lo mira, a veces adelante
        elif RISA - 4 <= f <= RISA + 24: y, x = -38, -5.0 if f < RISA + 12 else 2.0                       # se rien mirandose
        else: y, x = (-10 if h % 3 else -30), 0.5                                                        # caminan: adelante, alguna mirada
        ks.append((f, (x, y * sgn, 1.5 * (1 if h % 2 else -1))))
        f += 9 + (h % 5) * 2; k += 1
    return ks

def _arco(keys, f):
    for (f0, z0), (f1, z1) in zip(keys, keys[1:]):
        if f0 < f < f1 and z0 != z1: return math.sin(math.pi * (f - f0) / float(f1 - f0))
    return 0.0

def golpes(f, f0, puntos, cada=20, ataque=0.45):
    """gesto al hablar: la mano va de un punto al siguiente rapido (ataque) y se queda (como los golpes de la voz).
    cada = cuadros entre golpes, o una lista de duraciones (18/11: golpes desparejos, como al hablar de verdad, no robotico)"""
    if f <= f0: return puntos[0]
    if isinstance(cada, (list, tuple)):
        g = f - f0; i = 0
        while g >= cada[i % len(cada)] and i < 400: g -= cada[i % len(cada)]; i += 1
        u = g / float(cada[i % len(cada)])
    else:
        i = int((f - f0) // cada); u = ((f - f0) % cada) / float(cada)
    a = puntos[min(i, len(puntos) - 1)]; b = puntos[min(i + 1, len(puntos) - 1)]
    return lerp(a, b, smooth(min(1.0, u / ataque)))

def _rot2(x, z, deg):     # giro en el piso (como Ry)
    c = math.cos(math.radians(deg)); s_ = math.sin(math.radians(deg)); return (x * c + z * s_, -x * s_ + z * c)

def coreografia(D, quien, pie0):
    A = quien == 'A'; o = 0 if A else 3; sgn = 1.0 if A else -1.0
    fwd = (0, 0, 1); cm = 0.5 * D; C = CHOQUE; C2 = CHOQUE2
    (lx, lz), (rx, rz) = pie0['A'], pie0['B']
    # (20:52, Agustin: "camina con las piernas muy separadas, los pies deberian apuntar con cierto angulo, cada uno distinto"; "parecen dos
    # viejos caminando") caminando los pies van cerca de la linea del medio (como walk02: 13-20 cm entre tobillos, no los 31 del parado) y
    # cada pie apunta un poco afuera, distinto en cada uno; parado vuelven al ancho del clip del juego
    TL, TR = (7.0, -4.5) if A else (4.0, -8.0)
    NW = 0.065 if A else 0.090      # (18/11) estilo propio: A camina mas angosto, B mas ancho
    def L_(dz, x=None, yaw=None): return (lx if x is None else x, lz + dz, TL if yaw is None else yaw)
    def R_(dz, x=None, yaw=None): return (rx if x is None else x, rz + dz, TR if yaw is None else yaw)
    def Lw(dz): return L_(dz, 0.5 * (lx + rx) + NW)
    def Rw(dz): return R_(dz, 0.5 * (lx + rx) - NW)
    # ------- huellas (17/11 19:45, Agustin: "ya existen en el juego animaciones de walk, tomalas de referencia"): el paso sigue la curva
    # del juego (PASO_REF) con su ritmo (un pie cada 22 cuadros, ventana de 26) y una zancada algo mas corta (0.96 m, charlando).
    # Arrancan ya caminando, frenan en los dos ultimos pasos; paso del abrazo, paso atras, media vuelta a la derecha y se van.
    cx = 0.5 * (lx + rx); cz = WALK + 0.5 * (lz + rz); hw = 0.5 * (lx - rx)
    def giro(off, deg): x, z = _rot2(off, 0.0, deg); return (cx + x, cz + z, deg)
    t = 0 if A else 4                                   # B se da vuelta un poco despues
    ow = 0 if A else 8                                  # B llega un poco despues que A (desfasados)
    # (20:50, Agustin: "el problema son los tiempos, un poco de desfase en la caminata") los pasos no van todos iguales: cada uno despega
    # y apoya uno o dos cuadros antes o despues (cada jugador con su propio desfase), salvo los del abrazo y el apreton
    J_ = (0, 2, -1, 1, -2, 1, 0, -1, 2, -1, 1) if A else (1, -1, 2, 0, -2, 1, -1, 2, 0, 1, -1)
    def pasos(ini, lista):    # lista = [(desde, hasta, huella)]: despega el talon en 'desde', queda plano en 'hasta'
        k = [(-100, ini)]; cur = ini; n_ = 0
        for a, b, v in lista:
            # (18/11 01:14, Agustin: "caminan como si estuvieran rengos") el desfase paso a paso dejaba un pie apoyado 15 cuadros y el
            # otro 21: renguera. Ahora el ritmo es parejo en cada uno (el desfase queda ENTRE los dos jugadores: ow y t)
            k += [(a, cur), (b, v)]; cur = v
        return k
    # (20:13, Agustin: "despues del saludo los dos salgan caminando y charlando para un mismo lado") al soltar el abrazo medio paso atras,
    # se dan la mano y giran hacia el mismo lado: A a su izquierda, B a su derecha (en el mundo los dos van hacia +x de A), uno al lado
    # del otro a 0.66 m, B 6 cuadros atrasado. Pie de adelante en el giro: A el izquierdo, B el derecho.
    # (18/11, Agustin: "cuando salen a caminar estan muy juntos") con los cuerpos del juego a 0.66 m se rozan los brazos: SEP_CAMINA
    # entre los dos (de cadera a cadera); el paso atras al soltarse se ajusta para eso
    WB = WALK + 0.5 * (D - 2 * WALK - SEP_CAMINA); cz = WB + 0.5 * (lz + rz); t = 0 if A else 9; s_ = 1.0 if A else -1.0
    # (01:15, Agustin: "la transicion entre el abrazo y el choque de punos... puedan caminar hacia atras un poco") al soltarse dan dos
    # pasos atras hasta SEP_PUNO entre caderas (a un brazo doblado de distancia) y ahi chocan los punos
    WF = WALK + 0.5 * (D - 2 * WALK - SEP_PUNO)
    def lado_(d, cerca, yaw=90.0, ancho=NW):
        pie_izq = cerca if A else not cerca         # A va a su izquierda (cerca = su pie izquierdo); B a su derecha
        return (cx + s_ * d, cz - ancho if cerca else cz + ancho, s_ * yaw + (TL if pie_izq else TR))
    # (18/11 01:39, Agustin: "la forma en la que empiezan a caminar despues del punito es poco natural") antes giraban 90 grados en dos
    # pasos ya largos y los dos casi a la vez. Ahora: la cabeza y el pecho giran primero hacia donde van, el peso pasa al pie de apoyo
    # (la cadera se corre 3.5 cm al lado contrario y baja 1 cm: anticipacion), primer paso corto que abre la cadera (14 cm, 40 grados),
    # segundo 44 cm, tercero ya derecho; los pasos se alargan de a poco (0.14, 0.44, 0.70, 0.88, 0.96). B arranca 9 cuadros despues.
    sal = [(340, 362, lado_(0.14, True, 40.0, hw)), (360, 384, lado_(0.44, False, 78.0)), (382, 408, lado_(0.84, True))]
    d_, a_, cerca_ = 0.84, 404, False
    while a_ < N + 30:      # (18/11) siguen caminando hasta el final del clip (mas largo)
        d_ += 0.48; sal.append((a_, a_ + 26, lado_(d_, cerca_))); cerca_ = not cerca_; a_ += 22
    sal = [(a + t, b + t, v) for a, b, v in sal]
    guia = [x for k, x in enumerate(sal) if k % 2 == 0]; otro_ = [x for k, x in enumerate(sal) if k % 2 == 1]
    # (18/11 00:52, Agustin: "elimina el delay que hay entre que se paran de caminar y se chocan las manos") los dos ultimos pasos se
    # alargan (frenan: desacelerar) y el ultimo apoya justo cuando suben las manos: llegan y chocan en el mismo envion
    # (01:14, Agustin: "no me gusta la pausa que hay cuando se acercan antes de chocar las manos") ritmo parejo hasta el final: toda la
    # llegada va 14 cuadros mas tarde y el ultimo pie apoya en el momento del choque (los brazos suben durante los dos ultimos pasos)
    ow2 = (0 if A else 2) + 14; PH = PASO_VIENE if A else PASO_TIRA
    pa1, pa2 = ((C + 10, C + 26), (C + 24, C + 42)) if A else ((C + 18, C + 34), (C + 30, C + 46))   # A: paso largo, arrastrado; B: se afirma y entra despues
    at1, at2 = ((276, 296), (288, 306)) if A else ((270, 290), (280, 300))                           # al soltarse B retrocede primero
    u1 = (88 + ow2, 114 + ow2); u2 = (110 + ow2, C - 2) if A else (112 + ow2, C)
    pL = pasos(Lw(WALK - 2.66), [(0 + ow2, 26 + ow2, Lw(WALK - 1.70)), (44 + ow2, 70 + ow2, Lw(WALK - 0.74)), (u1[0], u1[1], L_(WALK)),
                                  pa1 + (L_(WALK + PH),), at1 + (L_(WF + 0.04),), (C2 - 4, C2 + 8, L_(WF))] + (guia if A else otro_))
    pR = pasos(Rw(WALK - 3.14), [(-22 + ow2, 4 + ow2, Rw(WALK - 2.18)), (22 + ow2, 48 + ow2, Rw(WALK - 1.22)), (66 + ow2, 92 + ow2, Rw(WALK - 0.32)),
                                  (u2[0], u2[1], R_(WALK)), pa2 + (R_(WALK + PH),), at2 + (R_(WF),)] + (otro_ if A else guia))
    pies = {'A': pL, 'B': pR}
    FZ = fases(pL, pR, N); MAMP = 0.85        # zancada 0.96 contra 1.19 del juego: un poco menos de vaiven
    ox, oz = 0.5 * (lx + rx), 0.5 * (lz + rz)
    def yaw_de(f): return 0.5 * (huella(pL, f)[2] + huella(pR, f)[2]) - 0.5 * (TL + TR)   # sin la apertura de los pies
    def frente(f): y = math.radians(yaw_de(f)); return (math.sin(y), 0.0, math.cos(y))
    def adelanto(f, a, b):     # cuanto adelante esta el pie a del pie b, en la direccion en que mira
        pa = huella(a, f); pb = huella(b, f); return dot((pa[0] - pb[0], 0.0, pa[1] - pb[1]), frente(f))
    def raiz(f):
        xs = []; zs = []
        for g in range(f - 8, f + 9):
            a_ = huella(pL, g); b_ = huella(pR, g); xs.append(0.5 * (a_[0] + b_[0])); zs.append(0.5 * (a_[1] + b_[1]))
        x = sum(xs) / len(xs) - ox; z = sum(zs) / len(zs) - oz
        ph, a = FZ[0][f], FZ[1][f] * MAMP      # subida / bajada y vaiven de la cadera de la caminata del juego (el peso pasa al pie de apoyo)
        sp = abs(adelanto(f, pL, pR))          # cadera mas baja con los pies abiertos (para que la pierna llegue)
        y = -(0.006 + 0.022 * min(1.0, sp / 0.5)) + 0.6 * marcha_en('y', ph) * a
        lat = 0.4 * marcha_en('x', ph) * a; yw = math.radians(yaw_de(f))
        # tiron del saludo (18/11 01:36, Agustin: "no se siente como si el azul jalara con fuerza al otro"): B se afirma y se echa
        # atras (cadera 4.5 cm atras y 2 cm abajo, despues de adelantarse 1.5 cm: anticipacion); A sale arrastrado: el cuerpo se
        # adelanta 7 cm antes de que llegue el pie
        if A: z += 0.07 * campana(f, C + 9, C + 17, C + 30)
        else: z += 0.015 * campana(f, C + 3, C + 8, C + 12) - 0.045 * campana(f, C + 9, C + 16, C + 30); y -= 0.02 * campana(f, C + 9, C + 16, C + 28)
        # arranque de la caminata final: el peso pasa al pie de apoyo antes del primer paso
        x += -s_ * 0.035 * campana(f, 326 + t, 340 + t, 356 + t); y -= 0.012 * campana(f, 328 + t, 339 + t, 350 + t)
        return (x + lat * math.cos(yw), y, z - lat * math.sin(yw))
    def giros_fn(f):          # media vuelta (raiz); cadera que gira con el paso (juego: +-8 grados) y el pecho la sigue casi entera
        r = {0: (0.0, yaw_de(f), 0.0)}      # (20:44) cadera / pecho / cabeza del paso: capa de la caminata del juego (cuerpo())
        if 190 < f < 236:   # (20:51) se aprietan al principio (el vaiven del abrazo sale de la captura del juego: abrazo_en)
            sq = smooth((f - 192) / 10.0) * smooth((236 - f) / 14.0)
            r[13] = (0.8 * sq, 0.0, 0.0); r[14] = (1.2 * sq, 0.0, 0.0)     # (18/11 03:00) la mitad: los torsos se metian 5.9 cm
        # (20:53, Agustin: "al handshake y al chocar le falta impacto, igual al abrazo") golpe: el cuerpo recibe el contacto y rebota
        # (oscilacion que se apaga): choque arriba = pecho y cabeza para atras; abrazo = el pecho se cierra al encontrarse; apreton = sacudida
        def suma(ch, x, y): v = r.get(ch, (0.0, 0.0, 0.0)); r[ch] = (v[0] + x, v[1] + y, v[2])
        if C - 24 < f < C + 2:      # anticipacion del apreton: el hombro derecho va atras con la mano que se carga y el pecho se abre
            k = campana(f, C - 22 + ow // 8, C - 11 + ow // 8, C + 1)
            suma(14, -1.5 * k, -7.0 * k); suma(13, -0.5 * k, -3.0 * k); suma(16, 0.0, 4.0 * k)
        if C < f < C + 36:
            if A:      # arrastrado: el hombro derecho sale adelante (pecho gira a la izquierda), el torso se va adelante y la cabeza queda atras, llega despues
                k = campana(f, C + 9, C + 16, C + 32); k2 = campana(f, C + 10, C + 18, C + 34)
                suma(14, 6.0 * k2, 13.0 * k); suma(13, 4.0 * k2, 4.0 * k)
                h = -1.0 * campana(f, C + 9, C + 13, C + 19) + 0.55 * campana(f, C + 17, C + 23, C + 32); suma(15, 4.0 * h, 0.0); suma(16, 7.0 * h, 0.0)
            else:      # tira: se adelanta apenas (anticipacion), se echa atras y lleva el hombro derecho atras; la cabeza firme
                k0 = campana(f, C + 3, C + 8, C + 12); k = campana(f, C + 9, C + 17, C + 34)
                suma(14, 3.0 * k0 - 7.0 * k, -12.0 * k); suma(13, -3.0 * k, -4.0 * k); suma(16, 3.0 * k, 5.0 * k)
        for f0, ch_amp in ((188, {13: 2.0, 14: 3.0, 16: 3.5}), (C2, {14: 1.5, 16: 2.0})):
            if f0 <= f < f0 + 24:
                k = math.exp(-(f - f0) / 5.0) * math.sin((f - f0) * 0.55 + 0.6) / math.sin(0.6)
                for ch, amp in ch_amp.items():
                    v = r.get(ch, (0.0, 0.0, 0.0)); r[ch] = (v[0] + amp * k, v[1], v[2])
        return r
    # ------- columna / cabeza / claviculas (grados: x adelante, y gira a la izquierda, z inclina a la derecha)
    inc = 9.0 if A else 8.0
    def asiente(f0, f1, amp=3.5, cada=20):   # cabeceos al hablar, con los golpes
        return [(f, (amp * (1 if ((f - f0) // cada) % 2 == 0 else -0.3), 1.5 * math.sin((f - f0) * 0.11), 0)) for f in range(f0, f1, cada // 2)]
    hA = asiente(12, 80) if A else asiente(72, 116)
    # despues del abrazo (comun a los dos, sgn: A 1, B -1): se sueltan mirandose, apreton con un cabeceo, la cabeza adelanta el giro
    # y caminando se miran de costado al hablar (A tiene a B a su derecha, B tiene a A a su izquierda)
    # (18/11 01:35, Agustin: "revisa como se separan al abrazarse, me parece poco natural") antes los dos hacian lo mismo a la vez y
    # rapido (manos a los dos hombros del otro). Ahora se aflojan desde el cuadro 262: el torso se endereza, la cabeza sale del hombro
    # y se miran; la mano de abajo se suelta sola y cae; la de arriba vuelve por el hombro: B le da una palmada en el hombro, A la apoya
    # y la deja resbalar; B da el paso atras primero
    hd = [(262, (10, -20 if A else -24, 14)), (276, (4, -8, 4)), (290, (-2, -3, 1)), (304, (2, -4, 2)), (310, (-4, 0, 0)), (C2 + 2, (-5, 0, 0)), (C2 + 6, (6, 0, 0)), (C2 + 16, (-1, 0, 0)),
          (C2 + 24, (0, 0, 0)), (336 + t, (0, 26 * sgn, 0)), (368 + t, (0, 4 * sgn, 0))] + mirada_caminando(A, sgn)
    # (18/11 01:19, Agustin: "a cada jugador le falta un estilo propio al caminar") postura al caminar: A algo inclinado adelante y
    # compacto; B erguido, pecho afuera
    po13, po14 = (4.5, 0.5) if A else (1.0, -3.5)
    giros = {
        13: [(0, (po13, 0, 0)), (C - 14 + ow // 2, (po13, 0, 0)), (C - 2, (1, 0, 0)), (C + 10, (0, 0, 0)), (182, (inc * 0.6, 0, 0)),
             (234, (inc * 0.6, 2.5 * sgn, 0)), (262, (inc * 0.6, 3.0 * sgn, 0)), (284, (inc * 0.2, 0, 0)), (306, (inc * 0.3, 0, 0)), (C2 + 2, (3, 0, 0)), (336, (1, 0, 0)),
             (360, (po13, 0, 0)), (N, (po13, 0, 0))],
        14: [(0, (po14, 0, 0)), (C - 16, (po14, 0, 0)), (C + 10, (0, 0, 0)), (186, (inc, 0, 0)), (262, (inc, 0, 0)), (284, (inc * 0.25, 0, 0)), (304, (inc * 0.4, 0, 0)), (C2 + 4, (1, 0, 0)), (332 + t, (0, 0, 0)),
             (348 + t, (0, 10 * sgn, 0)), (386 + t, (po14, -8 * sgn, 0)), (N, (po14, -8 * sgn, 0))],
        15: [(0, (0, 0, 0))] + [(f, (v[0] * 0.5, v[1], 0)) for f, v in hA] + [(C - 12 + ow // 2, (0, 0, 0)), (C - 2, (-2, 0, 0)), (C + 8, (-1, 0, 0)),
             (176, (2, -10, 4)), (194, (6, -26, 11)), (262, (6, -24, 11)), (276, (2, -9, 4)), (290, (-1, -3, 1)), (304, (1, -4, 2))] + [(f, (v[0] * 0.5, v[1] * 0.45, 0)) for f, v in hd[4:]],
        16: [(0, (0, 0, 0))] + hA + [(C - 12 + ow // 2, (0, 0, 0)), (C - 2, (-3, 0, 0)), (C + 8, (-1, 0, 0)), (176, (4, -12, 6)),
             (194, (10, -22 if A else -26, 14))] + [(f, (v[0], v[1] * 0.6, v[2])) for f, v in hd],
        22: [(0, (0, 0, 0))],     # (18/11) la clavicula sigue sola al brazo (ritmo del hombro en armar)
        17: [(0, (0, 0, 0))],
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
            return en_raiz(P, (sg * 0.25, 0.92 + 0.02 * max(0.0, d), 0.08 + 0.06 * d), (sg * 0.3, 0, -1), (0, -1, 0.15 + 0.1 * d), (-sg, 0, 0.2))
        fn.balanceo = True     # (21:00) el vaiven lo pone la capa de la caminata del juego (brazos_marcha)
        return fn
    def charla(lado, f0, puntos, cada=20):   # mano adelante, palma arriba y adentro, de golpe en golpe
        sg = ARM[lado][4]
        def fn(f, P, Po):
            dx, dy, dz = golpes(f, f0, puntos, cada)
            return en_raiz(P, (sg * (0.20 + dx), 1.06 + dy, 0.28 + dz), (sg * 0.4, -1, -0.5), (-sg * 0.35, 0.15, 1), (-sg * 0.6, 1, 0.1))
        return fn
    # (18/11 01:35, Agustin: "los gestos al hablar del azul" poco naturales) antes la mano quedaba todo el turno adelante, a la misma
    # altura, con golpecitos de 4-8 cm (como sosteniendo una bandeja). Ahora el gesto va por frases: la mano sube y se abre hacia el
    # otro (palma arriba), marca dos golpes de canto, se abre afuera ("que se yo") y entre frase y frase baja y afloja. Cada turno usa
    # una frase distinta (var). Lugares en el marco de la raiz; x hacia afuera = hacia el otro (caminan lado a lado).
    def frase(lado, f0, f1, var):
        sg = ARM[lado][4]
        def P_(x, y, z, cod, ded, pal): return ((sg * x, y, z), unit((sg * cod[0], cod[1], cod[2])), unit((sg * ded[0], ded[1], ded[2])), unit((sg * pal[0], pal[1], pal[2])))
        bajo = P_(0.21, 1.00, 0.20, (0.5, -1, -0.4), (-0.25, 0.05, 1), (-0.8, 0.6, 0.0))
        ofrece = P_(0.33, 1.13, 0.30, (0.6, -1, -0.3), (0.30, 0.20, 1), (-0.15, 1, 0.05))
        ofrece2 = P_(0.36, 1.10, 0.33, (0.6, -1, -0.3), (0.38, 0.12, 1), (-0.05, 1, 0.1))
        corta = P_(0.20, 1.19, 0.33, (0.5, -1, -0.4), (-0.05, 0.45, 1), (-1, 0.1, 0.0))
        corta2 = P_(0.22, 1.10, 0.36, (0.5, -1, -0.4), (-0.05, 0.25, 1), (-1, 0.1, 0.0))
        abre = P_(0.40, 1.05, 0.22, (0.7, -1, -0.2), (0.55, 0.0, 0.85), (-0.1, 1, 0.2))
        guion = ([(0, bajo), (7, ofrece), (14, ofrece2), (21, corta), (25, corta2), (32, corta), (36, corta2), (45, abre), (53, abre), (62, bajo)],
                 [(0, bajo), (8, corta), (12, corta2), (20, ofrece), (29, ofrece2), (37, abre), (44, bajo), (51, ofrece), (60, ofrece2), (68, bajo)],
                 [(0, bajo), (9, abre), (17, abre), (24, ofrece), (30, ofrece2), (38, corta), (42, corta2), (50, ofrece), (58, bajo)],
                 [(0, bajo), (6, ofrece), (12, ofrece2), (19, bajo), (27, corta), (31, corta2), (38, corta), (42, corta2), (50, abre), (60, abre), (68, bajo)])[var % 4]
        def fn(f, P, Po):
            g = f - f0
            if g <= guion[0][0]: v = guion[0][1]
            elif g >= guion[-1][0]: v = guion[-1][1]
            else:
                for (a, va), (b, vb) in zip(guion, guion[1:]):
                    if g <= b:
                        u = (g - a) / float(b - a); s = smooth(u); arco = 0.025 * math.sin(math.pi * s)
                        v = (add(lerp(va[0], vb[0], s), (0.0, arco, 0.0)), vslerp(va[1], vb[1], s), vslerp(va[2], vb[2], s), vslerp(va[3], vb[3], s)); break
            p = add(v[0], (0.006 * math.sin(f * 0.21 + var), 0.007 * math.sin(f * 0.17 + 1.3 * var), 0.005 * math.sin(f * 0.13)))   # la mano nunca queda clavada
            return en_raiz(P, p, v[1], v[2], v[3])
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
    sobre_hombro = otro('chest', (0.20, 0.35, -0.01), (0.6, 0.9, 0.4), (-0.4, -0.5, -1), (0, -1, 0))
    # (18/11) medido con las mallas del juego: con la mano en el medio de la espalda el antebrazo cortaba el costado del otro (4-5 cm):
    # las manos van mas al costado y apoyadas sobre la camiseta: la espalda de la camiseta esta 9.4 cm detras del hueso del pecho y 7 cm
    # detras del de la columna (medido en shirt_out.fmdl); medido en la vista previa, la palma queda 3-4 cm mas afuera que el objetivo por
    # las capas encima (vaiven del abrazo del juego, suavizado): objetivo casi sobre la camiseta. La mano de abajo va a la cintura
    # (mas baja): asi su brazo pasa por debajo del brazo del otro y no lo cruza
    # (18/11 01:49) codo arriba y hacia mi (apoyado en su hombro), no abierto al costado; la mano llega mas al medio de la espalda
    # y un poco hundida en la camiseta (medido: quedaba a 3-10 cm)
    # el mas bajo (A) no llega al medio de la espalda por arriba del hombro del alto: su mano queda en el omoplato
    # (18/11 02:18, Agustin: "el abrazo se ve raro") los dos hacian el mismo abrazo en espejo y el mas bajo tenia que pasar el brazo
    # por arriba del hombro del alto (estirado, codo arriba). Ahora cada uno abraza segun su altura: el ALTO (B) pasa la derecha por
    # arriba del hombro y la mano llega al omoplato de mas lejos (brazo que rodea, codo cerca), y la izquierda baja a la cintura hasta
    # el medio; el mas BAJO (A) abraza por el medio: la derecha pasa por encima del brazo bajo del alto y apoya en la mitad de la
    # espalda con el codo abajo, la izquierda va por debajo del brazo
    # (18/11 02:40, medido) con la mano del bajo en el medio de la espalda el brazo tendria que atravesar el costado del alto: quedaba
    # a 4-7 cm de la camiseta y con el codo abierto a la altura del hombro. La mano va al omoplato de su lado (hasta donde el brazo
    # llega rodeando) y el codo abajo, pegado
    # (medido con entre.py: la manga derecha del bajo se metia 3-5 cm en la manga izquierda del alto, los brazos se cruzan ahi) el
    # codo del bajo un poco mas arriba y el brazo bajo del alto mas pegado a su costado
    if A: ea = (0.13, -0.04, -0.085); codo_ea = (0.8, -0.7, 0.3); entra_R = otro('chest', (0.33, 0.02, 0.02), (1, -0.5, 0.3), (-0.3, -0.1, -1), (-1, 0, 0.2))
    else: ea = (-0.06, 0.10, -0.096); codo_ea = (0.7, 0.6, 0.5); entra_R = sobre_hombro
    espalda_alta = otro('chest', ea, codo_ea, (-1, -0.6, 0), (0, 0, 1))
    def palmada(k): return otro('chest', (ea[0], ea[1], ea[2] - 0.06 * k), codo_ea, (-1, -0.6, 0), (0, 0, 1))
    al_costado = otro('chest', (0.32, 0.12, -0.02), (1, 0.4, -0.2), (-0.2, -0.8, -0.8), (-1, 0, 0.3))
    bajo_brazo = otro('spine', (-0.33, 0.02, 0.02), (-0.8, -0.8, -0.2), (0.3, 0, -1), (1, 0, 0.3))
    espalda_baja = otro('spine', (-0.06 if A else 0.0, 0.02 if A else -0.03, -0.058 if A else -0.070), (-1, -0.3, 0.3) if A else (-0.6, -1.0, 0.3), (1, 0.15, 0), (0, 0, 1))
    salida_baja = otro('spine', (-0.33, 0.04, 0.02), (-1, -0.6, -0.2), (0.1, -0.6, -0.8), (1, 0, 0.3))
    # apreton de manos formal y bajo (las dos derechas en el medio, a la altura de la cintura): palma hacia adentro (A +x, B tambien en
    # su marco: quedan enfrentadas), dedos adelante, pulgar arriba; tres sacudidas cortas
    def apreton(dy=0.0): return mundo((0.0, 1.02 + dy, cm), (-1, -0.8, -0.4), unit((0.25, -0.15, 1)), (1, 0, 0))
    hombro_R = otro('chest', (0.18, 0.265, 0.06), (1, -0.5, 0.6), (-0.2, 0.3, -1), (0, -1, 0.2))      # mi derecha en su hombro izquierdo
    hombro_L = otro('chest', (-0.18, 0.265, 0.06), (-1, -0.5, 0.6), (0.2, 0.3, -1), (0, -1, 0.2))     # mi izquierda en su hombro derecho
    pre_apreton = mundo((-0.14, 0.97, cm - 0.30), (-1, -0.9, -0.2), unit((0.25, -0.35, 1)), unit((1, 0, 0.2)))
    apreton_ida = mundo((-0.07, 1.06, cm - 0.12), (-1, -0.8, -0.4), unit((0.2, -0.1, 1)), unit((1, 0, 0.2)))
    apreton_suelta = mundo((-0.09, 1.00, cm - 0.15), (-1, -0.8, -0.4), unit((0.15, -0.2, 1)), unit((1, 0, 0.2)))
    # (18/11 00:39, Agustin: "en vez de un apreton de manos sea mas como un saludo de punito comun") choque de punos: las dos derechas
    # cerradas (soft_nigiri) se encuentran en el medio a la altura del pecho bajo, nudillos adelante (dedos hacia el otro), palma abajo
    # y adentro. El centro de la palma queda a PUNO m del medio: nudillos de los dos en el mismo punto. Llega rapido, golpe y rebote.
    PUNO = 0.024     # medido 18/11 con las mallas: con 0.055 quedaban 5-7 cm entre los punos
    def puno(dz=0.0, dy=0.0, dx=0.0):
        return mundo((dx, 1.10 + dy, cm - PUNO - dz), (-1, -0.9, -0.3), unit((0.05, 0.05, 1)), unit((0.7, -0.7, 0)))
    # golpes del que habla (dx hacia afuera, dy arriba, dz adelante, m)
    gA = [(0, 0, 0), (0.04, 0.05, 0.04), (0.0, 0.0, 0.02), (0.06, 0.07, 0.05), (0.02, 0.01, 0.0), (0.05, 0.04, 0.05)]
    gB = [(0, 0, 0), (0.05, 0.06, 0.05), (0.01, 0.02, 0.0), (0.06, 0.08, 0.06), (0.0, -0.01, 0.02), (0.05, 0.05, 0.04), (0.02, 0.02, 0.01)]
    gA2 = [(0, 0, 0), (0.03, 0.06, 0.06), (0.07, 0.03, 0.02), (0.01, 0.0, 0.03), (0.05, 0.08, 0.04), (0.02, 0.02, 0.0), (0.06, 0.05, 0.05)]
    gB2 = [(0, 0, 0), (0.06, 0.04, 0.03), (0.02, 0.07, 0.05), (0.05, 0.01, 0.02), (0.0, 0.03, 0.05), (0.07, 0.06, 0.03), (0.03, 0.0, 0.01), (0.05, 0.05, 0.04)]
    # ------- brazo derecho
    if A:
        # (18/11 01:39, "la caminata del principio rehacela") antes A hacia tres gestos seguidos en 3 s mientras caminaba: ahora camina
        # con el brazo de la captura y solo lo senala una vez (lo reconoce de lejos); B le contesta abriendo la mano
        kR = [(0, balanceo('R')), (40, balanceo('R')), (51, senala('R', 51)), (72, senala('R', 51)), (88, balanceo('R')), (C - 25, balanceo('R'))]
    else:
        kR = [(0, balanceo('R')), (70, balanceo('R')), (81, frase('R', 76, 112, 2)), (104, frase('R', 76, 112, 2)), (118, balanceo('R')), (C - 24, balanceo('R'))]
    # (18/11 01:17, Agustin: "en vez de un choque de las dos manos podria ser un saludo de manos con una y que uno de los dos lo atraiga
    # hacia el para el abrazo") las derechas se agarran arriba, a la altura del pecho (palmas enfrentadas, dedos arriba y adelante, codo
    # abajo): la mano se carga atras y arriba (anticipacion), golpe del agarre, A tira hacia si (B viene con un paso largo) y se sueltan
    # al entrar al abrazo. tira = cuanto se corre el punto hacia A (m)
    # (18/11 01:36) tira = cuanto se corre el agarre hacia B (el azul es el que tira): afloja 4 cm hacia A (toma envion), tiron de
    # 28 cm en 9 cuadros hacia su pecho, y recien ahi se sueltan para abrazarse
    # (medido: tirando derecho al pecho el antebrazo de B entraba 6 cm en su propio cuerpo) B tira hacia su costado derecho (lado)
    def agarre(tira=0.0, dy=0.0, lado=0.0):
        # forma del agarre = la de agarre_exacto (vista_juego.py), buscada con las mallas de las manos: dedos 30 grados sobre la
        # horizontal hacia el otro, palma al costado, cada palma 2.9 cm de su lado, 2 cm adelantada y 3 cm hacia su menique
        # (18/11 02:50, medido con propio.py) con el codo a 48 grados bajo la horizontal el antebrazo entraba 4-6 cm en la propia
        # camiseta durante el agarre: codo mas afuera (37 grados)
        return mundo((sgn * lado - 0.029, 1.26 + dy - 0.016, cm + sgn * tira + 0.032), (-1, -0.75, -0.15), (0.0, 0.5, 0.866), (1, 0, 0))
    # (18/11 02:21, Agustin: "le falta una preparacion corta para la apretada de mano", "una anticipacion en los brazos") la mano se
    # cargaba de pasada, sin detenerse. Ahora el brazo va atras y arriba (codo atras, mano al lado del hombro), se queda 3 cuadros
    # cargado y recien sale al agarre; el hombro derecho va atras con el (giros_fn)
    # (visto en la vista: con la mano al lado del hombro quedaba un puno delante del pecho y el antebrazo entraba 6 cm en el cuerpo)
    # la mano abierta sale al costado y atras a la altura de la cintura (como para chocarla), y de ahi viene en arco al agarre.
    # Las claves van 4-5 cuadros antes de lo que se ve (el brazo suelto se funde y los resortes atrasan)
    # (visto: con la mano a 47 cm al costado el codo subia a la altura del hombro, como un ala, y despues la mano venia pegada al
    # pecho) el brazo baja casi estirado, afuera y atras (28 grados del cuerpo), mano abierta; y entra al agarre desde afuera, en
    # arco (clave de paso `entra`: 20 cm a su derecha y 16 cm antes del punto de encuentro)
    # (visto: a mitad de camino la mano pasaba pegada a la cintura, como "manos en la cadera") todo el arco va mas afuera
    # (medido: con la mano a 45 cm del hombro el codo subia a 1.37 m y quedaba atras, "manos en la cadera"; el brazo va casi
    # estirado, abajo, afuera y atras: el codo queda bajo)
    carga = yo('root', (-0.37, 0.88, -0.20), (-0.5, -0.3, -0.8), unit((-0.3, -0.9, 0.1)), unit((0.5, 0.0, 0.85)))
    entra = mundo((-0.30, 1.15, cm - 0.22), (-1, -0.6, -0.3), unit((0.3, 0.2, 0.9)), unit((1, 0, 0.15)))
    kR += [(C - 15 + ow // 8, carga), (C - 10 + ow // 8, carga), (C - 4, entra), (C, agarre()), (C + 3, agarre(0.0, -0.035)), (C + 8, agarre(-0.04, -0.02)), (C + 17, agarre(0.22, 0.03, 0.13)), (C + 21, agarre(0.24, 0.0, 0.15)),
           (C + 29, entra_R), (194, espalda_alta)]
    # (18/11 02:22, Agustin: "cuando el jugador tira con fuerza ahi deberia empezar el abrazo y que el otro reaccione con un poco de
    # atraso") el azul (B) no espera: suelta la mano en el final del tiron y la pasa de una por arriba del hombro (llega al omoplato en
    # el cuadro 184, antes 194); el rojo (A) sigue como estaba (194): llega 10 cuadros despues
    if not A: kR[-2:] = [(C + 27, entra_R), (C + 34, espalda_alta)]
    pats = [206, 220] if A else [213]
    for p in pats: kR += [(p - 2, espalda_alta), (p + 5, palmada(1)), (p + 12, espalda_alta)]
    # (20:59, Agustin: "la transicion entre el abrazo y el apreton de mano no me gusta") al soltarse las manos se deslizan de la espalda a
    # los hombros del otro y se quedan ahi mientras dan el paso atras (se miran a un brazo de distancia); despues la derecha baja, se
    # carga un poco atras y va al apreton
    hombro_aire = otro('chest', (0.19, 0.31, 0.075), (1, -0.5, 0.6), (-0.2, 0.3, -1), (0, -1, 0.2))
    # la mano vuelve resbalando por arriba del hombro (pegada), sin levantar el codo
    cima_hombro = otro('chest', (0.17, 0.285, -0.035), (1, 0.1, 0.4), (-0.3, -0.2, -1), (0, -1, 0.1))
    # (medido en la vista: con la mano arriba del hombro del otro el codo subia hasta la cabeza) la mano sale de la espalda rodeando el
    # hombro por afuera y queda en el costado del brazo del otro (deltoides), con el codo abajo: B le da dos palmadas ahi, A la apoya
    rodea = otro('chest', (0.27, 0.05, -0.05) if A else (0.25, 0.22, -0.06), (1, -0.2, 0.3), (-0.3, 0.1, -1), (-1, 0, 0.3))
    en_brazo = otro('chest', (0.275, 0.16, 0.02), (1, -0.8, 0.5), (-0.25, 0.25, -1), (-1, 0, 0))
    brazo_aire = otro('chest', (0.31, 0.15, 0.04), (1, -0.8, 0.5), (-0.25, 0.25, -1), (-1, 0, 0))
    if A: suelta_R = [(266, espalda_alta), (272, rodea), (279, en_brazo), (290, en_brazo)]
    else: suelta_R = [(266, espalda_alta), (271, rodea), (277, en_brazo), (281, brazo_aire), (284, en_brazo), (288, brazo_aire)]
    kR += suelta_R + [(302, puno(0.26, -0.06, -0.10)), (C2 - 4, puno(0.10, 0.0, -0.02)), (C2, puno()), (C2 + 2, puno(-0.005)),
           (C2 + 7, puno(0.09, 0.03)), (C2 + 14, puno(0.16, -0.02)), (C2 + 22, apreton_suelta), (C2 + 36, balanceo('R'))]
    if A:
        for k_, (f0, f1) in enumerate(HABLA_A):
            g_ = frase('R', f0, f1, k_); kR += [(f0 - 8, balanceo('R')), (f0 + 3, g_), (f1 - 4, g_), (f1 + 9, balanceo('R'))]
        kR += [(N, balanceo('R'))]
    else: kR += [(N, balanceo('R'))]
    # ------- brazo izquierdo
    if A:
        kL = [(0, balanceo('L')), (C - 20, balanceo('L'))]
    else:
        kL = [(0, balanceo('L')), (C - 16, balanceo('L'))]
    # la mano que no saluda queda suelta (brazo de la captura), no clavada delante de la panza; despues del abrazo cae sola
    # brazo izquierdo: el de B se abre mientras tira (contrapeso, ya buscando al otro) y lo envuelve apenas llega (cuadro 183);
    # el de A queda atras por el tiron (arrastre) y abraza despues (198)
    if A:
        arrastre = yo('root', (0.27, 0.95, -0.15), (0.3, 0.2, -1), unit((0.0, -1, -0.35)), (-1, 0, 0))
        kL += [(C + 7, balanceo('L')), (C + 17, arrastre), (C + 34, bajo_brazo), (198, espalda_baja)]
    else:
        abre = yo('root', (0.37, 1.02, 0.20), (0.8, -0.3, -0.6), unit((0.3, -0.3, 1)), unit((-1, 0, 0.3)))
        kL += [(C + 2, balanceo('L')), (C + 12, abre), (C + 22, bajo_brazo), (C + 33, espalda_baja)]
    kL += [(266, espalda_baja), (276, salida_baja), (294, balanceo('L')), (352 + t, balanceo('L'))]
    if A: kL += [(N, balanceo('L'))]
    else:
        for k_, (f0, f1) in enumerate(HABLA_B):
            g_ = frase('L', f0, f1, k_ + 2); kL += [(f0 - 8, balanceo('L')), (f0 + 3, g_), (f1 - 4, g_), (f1 + 9, balanceo('L'))]
        kL += [(N, balanceo('L'))]
    w = [(0, 1.0), (N, 1.0)]
    J = Jugador('A' if A else 'B', giros, {'R': {'k': kR, 'w': w}, 'L': {'k': kL, 'w': w}}, raiz, pies)
    J.giros_fn = giros_fn; J.fase = FZ; J.marcha_amp = MAMP
    ci = []; c_ = 0
    for f in range(N + 1):
        if f > 0 and FZ[0][f] < FZ[0][f - 1] - 0.5: c_ += 1
        ci.append(c_)
    J.ciclo = ci; J.ciclo0 = 0 if A else 3      # cada uno arranca en otro ciclo de la captura
    # el clip parado queda quieto debajo de las dos caminatas (base_de): en la del principio en la pose con la que frena, en la
    # final en la pose con la que arranca (despues del punito, que no se toca)
    q0 = max([f for f in range(0, 200) if FZ[1][f] > 0.5] or [0]); q1 = 346 + t
    J.quieto = lambda f: (FZ[1][f], q0) if f < 200 else (FZ[1][f] * smooth((f - (332 + t)) / 18.0), q1)
    # estilo de la caminata (cuanto de la captura del juego usa cada parte): A = pasos compactos, poca cadera y brazos cortos;
    # B = hombros que ruedan y brazos amplios
    # brazos: cuanto del vaiven de la captura (el derecho y el izquierdo no van iguales) y cuadros de atraso de cada brazo
    # (la captura de los brazos es de una caminata mas rapida que la nuestra: menos vaiven)
    J.estilo = (dict(cadera=0.38, torso=0.62, cabeza=0.50, brazos=0.70, clav=0.8, brazo_R=0.94, atraso_R=1) if A else
                dict(cadera=0.54, torso=0.98, cabeza=0.62, brazos=0.92, clav=1.1, brazo_L=0.92, atraso_L=2, atraso_R=1))
    J.contacto = [(C - 2, C + 21), (182, 268), (C2 - 1, C2 + 3)]
    # ------- cara y manos (las pone el Motor junto con el clip): (cuadro, gesto de la cara, peso, mano izquierda, mano derecha)
    if A:
        J.pista = [(0, 'smil_soft', 0.7, 'relax', 'relax'), (34, 'smil_talk_blink_soft', 0.85, 'relax', 'normal'),
                   (47, 'smil_talk_blink_soft', 0.9, 'relax', 'pointing'), (76, 'smil_talk_blink_soft', 0.8, 'relax', 'normal'),
                   (90, 'smil_soft', 0.6, 'relax', 'relax'), (118, 'pow_brwnit_wink_r', 1.0, 'relax', 'relax'),
                   (C - 16, 'pow_brwnit_wink_r', 1.0, 'relax', 'hiraki_normal'), (C - 10, 'smil_mho', 1.0, 'relax', 'hiraki_normal'),
                   (C - 5, 'smil_mho', 1.0, 'relax', 'handshake_normal1'), (C + 22, 'smil_soft', 0.8, 'open_full_half', 'open_full_half'),
                   (190, 'smil_shut_eyhc', 0.9, 'open_full_half', 'open_full_half'), (270, 'smil_soft', 0.8, 'relax', 'normal'),
                   (298, 'smil_soft', 0.9, 'relax', 'soft_nigiri'), (C2 + 18, 'smil_soft', 0.8, 'relax', 'relax'),
                   (388, 'smil_talk_blink_soft', 0.85, 'relax', 'hiraki_normal'), (446, 'smil_soft', 0.7, 'relax', 'relax'),
                   (472, 'smil_mho_soft', 0.8, 'relax', 'relax'), (492, 'smil_soft', 0.8, 'relax', 'relax'), (516, 'smil_mho_soft', 0.9, 'relax', 'relax'),
                   (538, 'smil_soft', 0.7, 'relax', 'relax'), (562, 'smil_talk_blink_soft', 0.85, 'relax', 'hiraki_normal'), (620, 'smil_soft', 0.75, 'relax', 'relax'),
                   (644, 'smil_mho_soft', 0.8, 'relax', 'relax'), (664, 'smil_soft', 0.8, 'relax', 'relax'), (RISA, 'smil_mho', 1.0, 'relax', 'relax'),
                   (RISA + 26, 'smil_soft', 0.85, 'relax', 'relax'), (750, 'smil_soft', 0.7, 'relax', 'relax')]
    else:
        J.pista = [(0, 'smil_soft', 0.6, 'relax', 'relax'), (70, 'smil_talk_blink_soft', 0.8, 'hiraki_normal', 'hiraki_normal'),
                   (122, 'smil_soft', 0.7, 'relax', 'relax'), (C - 15, 'smil_soft', 0.7, 'relax', 'hiraki_normal'),
                   (C - 8, 'smil_mho', 1.0, 'relax', 'hiraki_normal'), (C - 4, 'smil_mho', 1.0, 'relax', 'handshake_normal1'),
                   (C + 12, 'smil_mho', 1.0, 'open_full_half', 'handshake_normal1'), (C + 22, 'smil_soft', 0.8, 'open_full_half', 'open_full_half'), (190, 'smil_shut_eyhc', 0.8, 'open_full_half', 'open_full_half'),
                   (270, 'smil_talk_blink_soft', 0.7, 'relax', 'normal'), (298, 'smil_soft', 0.9, 'relax', 'soft_nigiri'),
                   (C2 + 18, 'smil_soft', 0.8, 'relax', 'relax'), (400, 'smil_mho_soft', 0.7, 'relax', 'relax'), (420, 'smil_soft', 0.7, 'relax', 'relax'),
                   (454, 'smil_talk_blink_soft', 0.85, 'hiraki_normal', 'relax'), (516, 'smil_mho_soft', 0.9, 'relax', 'relax'),
                   (538, 'smil_soft', 0.7, 'relax', 'relax'), (572, 'smil_mho_soft', 0.75, 'relax', 'relax'), (594, 'smil_soft', 0.75, 'relax', 'relax'),
                   (628, 'smil_talk_blink_soft', 0.85, 'hiraki_normal', 'relax'), (694, 'smil_soft', 0.8, 'relax', 'relax'), (RISA + 3, 'smil_mho', 1.0, 'relax', 'relax'),
                   (RISA + 28, 'smil_soft', 0.85, 'relax', 'relax'), (750, 'smil_soft', 0.7, 'relax', 'relax')]
    J.pista = cara_y_dedos_vivos(J.pista, A, C, C2)
    mp = {}
    for k in J.pista: mp[fc(k[0])] = (fc(k[0]),) + tuple(k[1:])
    J.pista = [mp[f] for f in sorted(mp)]
    J.plano = lambda f: smooth((f - (C + (29 if A else 23))) / 12.0) * (1.0 - smooth((f - 284) / 10.0))
    return J

# (20:43-20:46, Agustin: "les falta mucho movimiento en los dedos", "faltan movimientos en los ojos y las cejas") encima de las claves de
# arriba: al hablar, en cada golpe la mano se abre y se relaja (poses del juego), y las cejas suben en los acentos; caminando, los dedos
# cambian de a poco entre relajados, normales y apenas cerrados. Fuera de los contactos (choque, abrazo, apreton) y del dedo que senala.
def cara_y_dedos_vivos(pista, A, C, C2):
    def libre(f): return not (C - 12 <= f <= C + 22 or 184 <= f <= 342)
    hablaR = [(a, b - 6, 19) for a, b in HABLA_A] if A else [(78, 110, 20)]
    hablaL = [] if A else [(a, b - 6, 21) for a, b in HABLA_B]
    # (18/11 01:45, Agustin: "a las animaciones faciales tambien le falta ese toque de captura facial") medido: las cejas de acento
    # reemplazaban cada 20 cuadros a la captura del que habla (smil_talk_blink_soft, 207 cuadros reales) y al volver la captura
    # arrancaba otra vez de cero: solo se veian sus primeros 20 cuadros, repetidos. Ahora el que HABLA deja correr la captura
    # entera, sin cortes; las cejas y la media sonrisa pasan al que ESCUCHA, a destiempo (reacciona a lo que oye)
    escucha = ([(92, 116)] + list(HABLA_B)) if A else ([(36, 68)] + list(HABLA_A))
    extra = {'L': [], 'R': []}
    for lado, rangos in (('R', hablaR), ('L', hablaL)):
        for f0, f1, cada in rangos:
            for k, g in enumerate(range(f0, f1, cada)):
                extra[lado] += [(g, 'hiraki_normal' if k % 2 == 0 else 'open_full_half'), (g + 9, 'normal'), (g + 15, 'relax')]
    caminar = [(0, 130), (350, N)]
    for lado, rangos in (('R', hablaR), ('L', hablaL)):
        ocup = [(a - 4, b + 16) for a, b, _ in rangos] + ([(30, 90)] if A and lado == 'R' else [])
        for a, b in caminar:
            # (18/11 01:44, Agustin: "a las manos fuera de las poses tambien dales vida") antes cambiaban cada 17 cuadros justos y
            # las dos manos igual: ahora a destiempo (11 a 29 cuadros), cada mano con su orden, y a veces se abren
            g = a + (0 if lado == 'R' else 9); k = 0 if lado == 'R' else 3
            while g < b:
                if not any(x <= g <= y for x, y in ocup):
                    extra[lado].append((g, ('relax', 'normal', 'relax', 'soft_nigiri', 'normal', 'hiraki_normal', 'relax')[k % 7]))
                g += (13, 23, 17, 29, 11, 19)[(k + (0 if A else 2)) % 6]; k += 1
    cejas = []
    for n_, (f0, f1) in enumerate(escucha):
        g = f0 + 8 + 3 * n_; k = n_ + (0 if A else 1)
        while g + 18 < f1:
            dur = (22, 16, 28)[k % 3]
            cejas.append((g, ('neut_brwup_mho_eyo_soft_l', 'smug_brwup_smirk_l', 'neut_brwup_breth')[k % 3], 0.5)); cejas.append((g + dur, None, None))
            g += dur + (13, 21, 9)[k % 3]; k += 1
    marcos = sorted(set([k[0] for k in pista] + [g for g, _ in extra['L'] + extra['R']] + [g for g, *_ in cejas]))
    out = []
    for f in marcos:
        base = max((k for k in pista if k[0] <= f), key=lambda k: k[0]); exacta = base[0] == f
        cara, peso = base[1], base[2]
        ce = [c for c in cejas if c[0] <= f]
        if ce and ce[-1][1] and not exacta and libre(f): cara, peso = ce[-1][1], ce[-1][2]
        mano = {}
        for lado, col in (('L', 3), ('R', 4)):
            ks = [e for e in extra[lado] if e[0] <= f]
            v = base[col]
            if ks and ks[-1][0] > base[0] and libre(f) and base[col] not in ('pointing', 'handshake_normal1', 'soft_nigiri', 'open_full'): v = ks[-1][1]
            mano[lado] = v
        out.append((f, cara, peso, mano['L'], mano['R']))
    limpio = [out[0]]
    for k in out[1:]:
        if k[1:] != limpio[-1][1:]: limpio.append(k)
    return limpio

ABRAZO_D = 0.58     # (18/11 01:18, Agustin: "hay como mucha distancia entre ambos") antes 0.62; (03:05) 0.56 -> 0.58: con 0.56 los torsos se metian 5-6 cm todo el abrazo
ABRAZO_D_VIEJA = 0.62
def distancia_auto(base):
    """(18/11) distancia entre raices en el abrazo: medida con las mallas del juego (tools/vista_juego.py --revisar). Con 0.42 (pechos de
    0.145) uno se metia 6 cm en el otro: la camiseta sobresale 0.16-0.17 del pecho y el pecho alto va inclinado hacia el otro"""
    return ABRAZO_D

def revisar(info, D):
    """choque: distancia entre las palmas; abrazo: brazos contra el cuerpo del otro (capsula del torso)"""
    def palma(P, lado):
        bh = BONE[lado][3]; W, q = P[bh]; sg = ARM[lado][4]
        return add(W, qrot(q, (sg * HANDO, -0.02, 0)))
    for la, lb in (('R', 'R'),):
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

# (17/11 22:55, Agustin con video: "no coinciden correctamente", "las alturas de los jugadores no se respetan") en el juego los brazos
# miden distinto que este esqueleto aproximado y cada jugador tiene su estatura: las manos no caen justas. Para cada contacto (choque de
# las dos manos, abrazo, apreton) se guarda DONDE tiene que quedar mi muneca respecto del otro: punto de anclaje del otro (su muneca o su
# pecho) + vector en el marco del otro (x = su izquierda, de cadera a cadera; y = arriba; z = adelante), en metros a estatura 1. El
# Motor (motorpj.cpp ContactIK) lo lleva al cuerpo real de cada uno: mide los huesos, escala el vector por el tamano del otro y corrige
# hombro y codo hacia ese punto (en el choque y el apreton cada uno pone la mitad: se encuentran en el medio).
RAMPA_C = 8
def ventanas_contacto():
    C, C2 = CHOQUE, CHOQUE2      # mano: 0 izquierda, 1 derecha; ancla: 0 su muneca izquierda, 1 su muneca derecha, 2 su pecho
    # ancla 3 = su pecho pero SOLO la mano (sin puntas de pie ni agacharse): la mano en el brazo del otro al soltarse del abrazo
    # (sin esto, con estaturas distintas la mano del alto iba a la cabeza del bajo)
    # ancla 4 = agarre exacto de las dos manos derechas (el Motor lo recalcula en cada cuadro: ver agarre_exacto en vista_juego.py)
    return [(fc(C - 2), fc(C + 21), {1: 4}), (fc(182), fc(268), {0: 2, 1: 2}), (fc(276), fc(290), {1: 3}), (fc(C2 - 1), fc(C2 + 3), {1: 1})]

def marco(P):
    x = sub(P['thighA'][0], P['thighB'][0]); x = unit((x[0], 0.0, x[2])); return x, (0.0, 1.0, 0.0), cross(x, (0.0, 1.0, 0.0))

def contactos(info, D, i):
    o = 1 - i; segs = []; datos = []; nmano = {0: 'handL', 1: 'handR'}; nancla = {0: 'handL', 1: 'handR', 2: 'chest', 3: 'chest', 4: 'handR'}
    for f0, f1, mapa in ventanas_contacto():
        for mano, ancla in sorted(mapa.items()):
            a = max(0, f0 - RAMPA_C); b = min(len(info) - 1, f1 + RAMPA_C); off = len(datos)
            for f in range(a, b + 1):
                if f < f0: w = (f - a) / float(f0 - a)
                elif f > f1: w = (b - f) / float(b - f1)
                else: w = 1.0
                w = w * w * (3 - 2 * w)
                Po = info[f][o]; W = al_otro(info[f][i][nmano[mano]][0], D)     # mi muneca en el modelo del otro
                d = sub(W, Po[nancla[ancla]][0]); X, Y, Z = marco(Po)
                datos.append((w, dot(d, X), dot(d, Y), dot(d, Z)))
            segs.append((mano, ancla, a, b - a + 1, off))
    P0 = info[0][i]; cad = lerp(P0['thighA'][0], P0['thighB'][0], 0.5)
    return segs, datos, norm(sub(P0['head'][0], cad))

def pista_inc(Js, nombres, D, info):
    """src/motorpista.inc: la cara y las manos de cada clip propio (las pone el Motor mientras corre el clip) y los contactos"""
    o = ['// generado por tools/gani_cuerpo.py (no editar a mano): cara y manos de los clips propios, por cuadro (30 por segundo)',
         'struct OwnKey { int frame; const char* face; float faceW; const char* handL; const char* handR; };',
         '// contactos: mano (0 izq, 1 der), ancla del otro (0 su muneca izq, 1 der, 2 pecho), primer cuadro, cuadros, lugar en kOwnC_X',
         '// (por cuadro: peso, vector x y z en el marco del otro a estatura 1); torso = cabeza - medio de los muslos en el cuadro 0',
         'struct OwnSeg { int hand; int anchor; int f0; int n; int off; };']
    for i, (J, nm) in enumerate(zip(Js, nombres)):
        o.append('static const OwnKey kOwn_%s[] = {' % J.nombre)
        for k in J.pista: o.append('    { %d, "%s", %.2ff, "%s", "%s" },' % k)
        o.append('};')
        segs, datos, torso = contactos(info, D, i); J.torso_ref = torso
        o.append('static const OwnSeg kOwnS_%s[] = {' % J.nombre)
        for sg in segs: o.append('    { %d, %d, %d, %d, %d },' % sg)
        o.append('};')
        o.append('static const float kOwnC_%s[] = {' % J.nombre)
        for k in range(0, len(datos), 4): o.append('    ' + ' '.join('%.4ff, %.4ff, %.4ff, %.4ff,' % q for q in datos[k:k + 4]))
        o.append('};')
    o.append('struct OwnTrack { const char* file; const OwnKey* keys; int n; const OwnSeg* seg; int nSeg; const float* cdat; float torso; };')
    o.append('static const OwnTrack kOwnTracks[] = {')
    for J, nm in zip(Js, nombres):
        X = J.nombre
        o.append('    { "%s", kOwn_%s, (int)(sizeof kOwn_%s / sizeof kOwn_%s[0]), kOwnS_%s, (int)(sizeof kOwnS_%s / sizeof kOwnS_%s[0]), kOwnC_%s, %.4ff },' % (nm, X, X, X, X, X, X, X, J.torso_ref))
    o.append('};')
    o.append('// pareja: B empieza a D metros delante de A, mirandolo (girado 180); los dos clips van al mismo tiempo')
    o.append('struct OwnPair { const char* a; const char* b; float dist; };')
    o.append('static const OwnPair kOwnPairs[] = { { "%s", "%s", %.2ff } };' % (nombres[0], nombres[1], D))
    return '\n'.join(o) + '\n'

def main():
    a = sys.argv[1:]
    if len(a) < 2: print(__doc__ or 'uso: gani_cuerpo.py carpeta salida [distancia en el abrazo] [--inc src/motorpista.inc]'); return 1
    base = Base(open(os.path.join(a[0], 'dml_ent_aisle_cmn_idle01.gani'), 'rb').read()); marcha_cargar(a[0])
    Dh = float(a[2]) if len(a) > 2 and not a[2].startswith('--') and float(a[2]) > 0 else distancia_auto(base)
    base.extender(N)
    D = round(Dh + 2 * (PASO + WALK), 2)
    print('distancia entre los dos al empezar: %.2f m (choque a %.2f m, abrazo a %.2f m)' % (D, Dh + 2 * PASO, Dh))
    Js = [coreografia(D, 'A', base.pie0), coreografia(D, 'B', base.pie0)]
    poses, info = armar(base, Js, D)
    revisar(info, D)
    limites(poses)
    os.makedirs(a[1], exist_ok=True); nombres = []
    for J, p in zip(Js, poses):
        b = escribir(base, p)
        nm = 'zfx_saludo_abrazo_%s.gani' % J.nombre.lower(); nombres.append(nm)
        fn = os.path.join(a[1], nm); open(fn, 'wb').write(b)
        G = GC.leer_gani(b)
        print('%s: %d B, %d cuadros, %d canales' % (fn, len(b), G['cuadros'], G['ncanales']))
    if '--inc' in a: open(a[a.index('--inc') + 1], 'w', newline='\n').write(pista_inc(Js, nombres, D, info))
    return 0, D

if __name__ == "__main__": r = main(); sys.exit(r[0] if isinstance(r, tuple) else r)
