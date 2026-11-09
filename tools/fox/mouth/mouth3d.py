# PES21 FX (31/10) - boca 3D: arma un mouth.fmdl nuevo a partir del de Konami, con las MISMAS texturas (oral_* o pes_oral_*):
#   - dientes con volumen: uno por cada diente pintado en las tiras del atlas (10 arriba, 12 abajo; limites = papilas de la encia,
#     make_oral.rows_layout). Cada diente es un solido (seccion superelipse) que sale de la tira de Konami en su lugar: la cara de
#     adelante usa la uv del diente pintado, el cuello se mete en la encia, el borde incisal es fino (los caninos en punta) y la
#     cara de atras repite la uv (el esmalte por dentro tambien es diente). Arriba pesan en sk_head, abajo en skf_jaw.
#   - tiras de encia (Konami) detras de los dientes: suavizadas y, donde hay diente pintado, corridas hacia adentro (no pelean
#     con el diente nuevo).
#   - lengua: la de Konami subdividida dos veces y suavizada (mas redonda), surco central, pesa en skf_jaw. Queda en su zona de la
#     uv (u 0.28..0.73, v 0.66..1.0): un shader puede reconocerla por la uv para moverla en las expresiones.
#   - paladar, piso, mejillas: los de Konami subdivididos una vez.
# Uso: python3 mouth3d.py <mouth.fmdl de Konami> <carpeta con oral_bsm.ftex y oral_trm.ftex> <salida mouth.fmdl> [vista.npz]
import sys, os
import numpy as np
from scipy import ndimage as ndi
import scipy.sparse as sp, scipy.sparse.csgraph as cg
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', '..')]
import fmdl, mouth_fmdl, make_oral, throat, molar

MM = 0.001
# (01/11) mordida: con la boca en reposo la tira de arriba termina en y 1.631 y la de abajo en 1.6285 (2.5 mm de luz); los dientes
# se alargan para que cierren: los de arriba bajan 2.4 mm por delante de los de abajo, que suben 0.8 mm (se solapan ~0.7 mm)
EXT_UP, EXT_LO = float(os.environ.get('EXT_UP', '1.7')) * 0.001, 0.7 * 0.001   # (01/11 f) 2.0 -> 1.5 mm: menos sobremordida (Agustin)   # (01/11 b) 2.4 / 0.8 -> 2.0 / 0.7: dientes un poco menos largos (Agustin)
# (01/11 b) mas encia (Agustin): la linea de la encia baja GUM del alto del diente pintado; esa franja de la tira muestra encia
GUM = 0.08   # = make_oral.GUM (01/11 h: 0.18 -> 0.08, dientes mas largos)
KONAMI_BITE = int(os.environ.get('KONAMI_BITE', '1'))   # (02/11 g) Agustin: 'que la mordida sea la misma que la de Konami': el borde de cada diente 3D queda
# justo en el borde del diente pintado de Konami (sin alargar, sin resalte); los premolares chicos crecen hacia la encia, no hacia el borde
OVERJET_LO = 0.7 * MM   # (02/11) 0.9 -> 0.7, parejo   # (01/11 e) los incisivos de abajo se meten 0.9 mm detras de los de arriba
PDEG = int(os.environ.get('PDEG', '1'))   # (03/11) Agustin 'dientes curveados de una manera poco natural': rectos a lo ancho (la forma la da la convexidad), antes 2   # (01/11 f) parche por diente: grado 2 a lo ancho (sigue el arco), recto a lo alto
CUT_STRIP = float(os.environ.get('CUT_STRIP', '0'))   # (01/11 i) la tira se corta a s > GUM + esto (0 = no cortar; 02/11: apagado, ver STRIP_GUM)
STRIP_GUM = float(os.environ.get('STRIP_GUM', '0.3'))   # (02/11) la tira debajo de la encia (s > GUM) pasa a mostrar encia: s' = GUM - esto * (s - GUM)
STRIP_FOLD = float(os.environ.get('STRIP_FOLD', '0.25'))   # (02/11 c) la tira mas alla de la encia (s > GUM) se recoge a este largo hacia el cuello
FOLD_BACK = float(os.environ.get('FOLD_BACK', '1.5'))
REAL_TEETH = int(os.environ.get('REAL_TEETH', '1'))   # (03/11) Agustin: 'basate en una dentadura real': anchos reales por diente
REAL_GAP = float(os.environ.get('REAL_GAP', '0.024'))      # (03/11 v19c) aire entre dientes (fraccion del ancho de cada lado)
REAL_GAP_IC = float(os.environ.get('REAL_GAP_IC', '0.012'))
REAL_AIR = float(os.environ.get('REAL_AIR', '0.2'))      # (03/11 v19c) aire minimo (mm) entre dientes vecinos, mitad de adelante
ARCH_Z = float(os.environ.get('ARCH_Z', '0.125'))          # z (m) aproximado del centro del arco (para saber que es 'adelante')
NECK_N = float(os.environ.get('NECK_N', '0.78'))           # (03/11 v19c) ancho del cuello de incisivos/caninos (fraccion)
LO_OVERBITE = float(os.environ.get('LO_OVERBITE', '2.2'))   # (03/11) mm que los de abajo suben detras de los de arriba (sobremordida)
LO_OVERJET = float(os.environ.get('LO_OVERJET', '1.0'))     # (03/11) mm que los de abajo van detras (resalte)
# anchos mesiodistales reales (mm), del centro hacia atras: arriba IC, I, C, P1, P2; abajo I1, I2, C, P1, P2, M
REAL_W = {True: [8.1, 6.8, 7.7, 7.1, 6.7],   # (04/11) Agustin 'reducir el tamano de las paletas en la boca normal': centrales 8.6 -> 8.1 mm
          False: [5.3, 5.8, 6.9, 7.0, 7.1, 8.5]}
# (04/11) perfiles por jugador (Agustin: 'una boca especifica para Messi', fotos de su sonrisa): MOUTH_PROFILE=messi
# centrales de arriba grandes y cuadrados, laterales angostos, mas cortos y un poco hacia atras (escalon marcado),
# caninos con punta; abajo igual que la comun (casi no se ve)
PROFILE = os.environ.get('MOUTH_PROFILE', '')
PROF = {
    '': dict(ext_up={'IC': -0.5, 'I': -1.1, 'C': -0.5}, ext_p=-0.6, corner_ic=(0.04, 0.09), corner_i_up=(0.06, 0.14), lat_back=0.1, canine=0.26, ext_lo=0.8, corner_lo=(0.03, 0.06), gap_ic=0.006, gap=0.012, back_in=0.05, asym=(0.25, 0.12, 0.8, 0.10)),   # (09/11 d) Agustin 'mejora la boca generica tambien': lo aprendido con Messi (arco de la sonrisa, esquinas redondeadas, linea fina entre dientes, muelas adentro, filas que se separan al sonreir, asimetria) con dientes comunes
    'messi': dict(ext_up={'IC': 0.3, 'I': -0.5, 'C': -0.1}, ext_p=-0.7, corner_ic=(0.04, 0.09), corner_i_up=(0.06, 0.14), lat_back=0.1, canine=0.22, even=False, ext_lo=-0.3, corner_lo=(0.03, 0.06), gap_ic=0.004, gap=0.010, back_in=0.05, asym=(0.22, 0.10, 0.7, 0.10)),   # (09/11 c) Agustin (foto de Messi riendo: 'mira la forma de los dientes y muelas'): arco de la sonrisa natural (centrales largos, laterales mas cortos y apenas detras, canino con punta casi al nivel de los centrales, premolares mas cortos subiendo hacia atras), esquinas redondeadas; dientes individuales con linea fina (gap 0.010 / 0.004), muelas adentro, hueco entre filas
}[PROFILE]
if PROFILE == 'messi': REAL_W = {True: [8.7, 7.0, 7.8, 7.1, 6.7], False: REAL_W[False]}   # (09/11 c) foto riendo: centrales anchos, laterales mas chicos   # (04/11 c) laterales anchos (comun 8.1 / 6.8)

def fit_row(row, log=print, name=''):
    """(03/11 v19c) Agustin: 'ni todos pegados ni separados'. Entre dientes vecinos de una fila (en orden por el arco) se mide
    cuanto se enciman las caras de ADELANTE (lo que se ve; atras convergen con la curva del arco, como en una dentadura real) y
    se angosta solo el lado que mira al vecino (escala hacia su centro) hasta dejar REAL_AIR mm de aire. Arregla los
    premolares de arriba que se encimaban (el arco 3D ahi es mas corto que el ancho real del diente). Tope: 20 % por lado"""
    air = REAL_AIR * MM
    C = [q[0].mean(0) for q in row]
    def front(P, c):   # caras de adelante: hacia afuera del arco (centro del arco en z ARCH_Z)
        o = c - np.array([0.0, c[1], ARCH_Z]); o[1] = 0; o /= max(np.linalg.norm(o), 1e-9)
        t = (P - c) @ o
        return P[t > t.min() + 0.6 * np.ptp(t)]
    fac = [[1.0, 1.0] for _ in row]   # (hacia el anterior, hacia el siguiente)
    U = []
    nfix = 0
    for i in range(len(row) - 1):
        ca, cb = C[i], C[i + 1]
        d = cb - ca; d[1] = 0; L = float(np.linalg.norm(d)); u = d / max(L, 1e-9); U.append(u)
        ea = float(((front(row[i][0], ca) - ca) @ u).max()); eb = float(((cb - front(row[i + 1][0], cb)) @ u).max())
        over = ea + eb + air - L
        if over > 0:
            nfix += 1
            fac[i][1] = max(0.8, 1 - 0.5 * over / max(ea, 1e-6)); fac[i + 1][0] = max(0.8, 1 - 0.5 * over / max(eb, 1e-6))
    for i, q in enumerate(row):
        P, c = q[0], C[i]
        for k, j in ((0, i - 1), (1, i)):
            if not (0 <= j < len(U)) or fac[i][k] >= 1.0: continue
            u = U[j] if k == 1 else -U[j]
            t = (P - c) @ u; side = t > 0
            P[side] -= np.outer(t[side] * (1 - fac[i][k]), u)
    log('  ajuste de la fila %s: %d pares encimados angostados (aire %.2f mm)' % (name, nfix, REAL_AIR))

def real_bounds(M, r, pk, up, log=print):
    """(03/11) limites entre dientes (x del atlas, 0..512) con las proporciones de una dentadura real: se mide el largo de
    la tira a lo largo del arco (a media altura del diente) y se reparte simetrico desde el centro con REAL_W, escalado para
    que entre en el mismo tramo que los dientes pintados"""
    ntt = len(pk) - 1; half = ntt // 2
    xs = np.linspace(pk[0], pk[-1], 400)
    g = np.interp(xs, np.arange(512), r['g']); inc = np.interp(xs, np.arange(512), r['inc'])
    Pm, _ = M(np.stack([xs / 512.0, (0.5 * (g + inc)) / 512.0], 1))
    s = np.r_[0, np.cumsum(np.linalg.norm(np.diff(Pm, axis=0), axis=1))]
    xc = pk[half] if ntt % 2 == 0 else 0.5 * (pk[half] + pk[half + 1])
    sc_ = np.interp(xc, xs, s)
    w = np.array((REAL_W[up] * 2)[:half], float) * MM
    k = min(sc_, s[-1] - sc_) / w.sum()   # escala para que entren (simetrico)
    cum = np.r_[0, np.cumsum(w * k)]
    right = np.interp(sc_ + cum, s, xs); left = np.interp(sc_ - cum, s, xs)
    out = np.r_[left[::-1], right[1:]]
    log('  dentadura real %s: escala %.2f, anchos %s mm' % ('arriba' if up else 'abajo', k, np.round(w * k / MM, 1).tolist()))
    wt = np.r_[(w * k)[::-1], w * k]
    return out, wt

BS_UP = float(os.environ.get('BS_UP', '0.0'))   # (02/11 v17b) donde sale de la encia el diente de arriba (fraccion del alto)
FOLD_S = float(os.environ.get('FOLD_S', '0.35'))   # (02/11 c) mm detras de la encia donde queda la tira recogida
CHEEK = int(os.environ.get('CHEEK', '1'))   # (04/11) paredes de la mejilla a los costados (Agustin: 'que no se note que esta abierto')
THROAT = int(os.environ.get('THROAT', '1'))   # (02/11) fondo de la garganta nuevo (throat.py) en lugar de la pared plana de Konami
SINK = float(os.environ.get('SINK_MM', '2.0' if int(os.environ.get('REAL_TEETH', '1')) else '1.5')) * MM   # (03/11 v19c) 2 mm: la encia abraza el cuello   # (01/11 d) 0.7 -> 1.5 mm: el cuello se mete bien detras de la encia (no asoma por arriba)
PATCH = int(os.environ.get('PATCH', '1'))
MOLARS = int(os.environ.get('MOLARS', '2'))   # (02/11) muelas 3D por lado y por arcada, detras del ultimo premolar (Agustin: 'que las muelas sean 3d')
MOL_DROP_LO = float(os.environ.get('MOL_DROP_LO', '0.8'))   # (04/11) mm que bajan las muelas de abajo (Agustin)
MOL_AIR = float(os.environ.get('MOL_AIR', '0.3')); MOL_S0_UP = float(os.environ.get('MOL_S0_UP', '0.4')); MOL_S0_LO = float(os.environ.get('MOL_S0_LO', '0.4'))   # (04/11) donde arranca la 1a muela detras del ultimo premolar (mm por la encia; negativo = mas adelante)
MOLAR_L = [float(x) for x in os.environ.get('MOLAR_L', '10.0,9.0').split(',')]   # largo mesio-distal (mm) de cada muela
UV_W, UV_V0, UV_V1 = 0.55, 0.15, 0.90   # (01/11 e) el borde translucido de la textura (0.62-0.92) cae adentro   # (01/11 d) parte del diente pintado que usa el diente 3D

def comps(m):
    F = m['faces']; n = len(m['pos'])
    A = sp.coo_matrix((np.ones(F.size), (F.ravel(), np.roll(F, 1, 1).ravel())), shape=(n, n))
    return cg.connected_components(A, directed=False)[1]

# ---------------------------------------------------------------- subdivision (Loop, con borde) de una malla con atributos
def loop(P, F, attrs, smooth=1.0):
    """P (n,3); F (m,3); attrs: lista de arrays (n,k) que se interpolan (uv, pesos...). Devuelve P2, F2, attrs2"""
    n = len(P)
    E = np.sort(np.concatenate([F[:, [0, 1]], F[:, [1, 2]], F[:, [2, 0]]]), 1)
    Eu, inv, cnt = np.unique(E, axis=0, return_inverse=True, return_counts=True)
    inv = inv.ravel()
    ne = len(Eu); boundary_e = cnt == 1
    # vertice nuevo por arista
    Fe = inv.reshape(3, -1).T            # aristas de cada cara: (01, 12, 20)
    opp = np.zeros((ne, 2), np.int64) - 1
    third = np.stack([F[:, 2], F[:, 0], F[:, 1]], 1)
    # (vectorizado) vertice opuesto de cada arista
    ee = Fe.ravel(); oo = third.ravel()
    order = np.argsort(ee, kind='stable'); ee_s = ee[order]; oo_s = oo[order]
    first = np.searchsorted(ee_s, np.arange(ne))
    opp[:, 0] = oo_s[first]
    two = cnt == 2
    opp[two, 1] = oo_s[first[two] + 1]
    a, b = Eu[:, 0], Eu[:, 1]
    def mix_edge(X):
        out = 0.5 * (X[a] + X[b])
        inner = ~boundary_e
        out[inner] = (3 / 8 * (X[a[inner]] + X[b[inner]]) + 1 / 8 * (X[opp[inner, 0]] + X[opp[inner, 1]]))
        return out
    EP = mix_edge(P)
    EP = smooth * EP + (1 - smooth) * 0.5 * (P[a] + P[b])
    # vertices viejos
    nb = [[] for _ in range(n)]; bnb = [[] for _ in range(n)]
    for (x, y), bd in zip(Eu, boundary_e):
        nb[x].append(y); nb[y].append(x)
        if bd: bnb[x].append(y); bnb[y].append(x)
    VP = P.copy()
    for i in range(n):
        if bnb[i]:
            if len(bnb[i]) == 2: VP[i] = 0.75 * P[i] + 0.125 * (P[bnb[i][0]] + P[bnb[i][1]])
        elif nb[i]:
            k = len(nb[i]); beta = 3 / 16 if k == 3 else 3 / (8 * k)
            VP[i] = (1 - k * beta) * P[i] + beta * P[nb[i]].sum(0)
    VP = smooth * VP + (1 - smooth) * P
    P2 = np.concatenate([VP, EP])
    A2 = [np.concatenate([X, 0.5 * (X[a] + X[b])]) for X in attrs]   # atributos: lineal (uv sin deformar)
    e0, e1, e2 = Fe[:, 0] + n, Fe[:, 1] + n, Fe[:, 2] + n
    F2 = np.concatenate([np.stack([F[:, 0], e0, e2], 1), np.stack([F[:, 1], e1, e0], 1),
                         np.stack([F[:, 2], e2, e1], 1), np.stack([e0, e1, e2], 1)])
    return P2, F2, A2

# ---------------------------------------------------------------- normales y tangentes
def vnormals(P, F):
    n = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    N = np.zeros_like(P)
    for k in range(3): np.add.at(N, F[:, k], n)
    return N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)

def tangents(P, N, UV, F):
    p0, p1, p2 = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
    w0, w1, w2 = UV[F[:, 0]], UV[F[:, 1]], UV[F[:, 2]]
    e1, e2 = p1 - p0, p2 - p0; d1, d2 = w1 - w0, w2 - w0
    r = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    r = np.where(np.abs(r) < 1e-12, 1e-12, r)
    T = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / r[:, None]
    Bt = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) / r[:, None]
    TT = np.zeros_like(P); BB = np.zeros_like(P)
    for k in range(3): np.add.at(TT, F[:, k], T); np.add.at(BB, F[:, k], Bt)
    TT = TT - N * (TT * N).sum(1, keepdims=True)
    TT /= np.maximum(np.linalg.norm(TT, axis=1, keepdims=True), 1e-12)
    w = np.sign((np.cross(N, TT) * BB).sum(1)); w[w == 0] = 1
    return TT, w

# ---------------------------------------------------------------- uv -> 3D sobre una tira de Konami
class UVMap:
    def __init__(self, P, N, UV, F):
        self.P, self.N, self.UV, self.F = P, N, UV, F
        a, b, c = UV[F[:, 0]], UV[F[:, 1]], UV[F[:, 2]]
        self.a, self.v0, self.v1 = a, b - a, c - a
        self.den = self.v0[:, 0] * self.v1[:, 1] - self.v1[:, 0] * self.v0[:, 1]
        ok = np.abs(self.den) > 1e-10
        self.F, self.a, self.v0, self.v1, self.den = F[ok], a[ok], self.v0[ok], self.v1[ok], self.den[ok]
    def __call__(self, q):
        q = np.atleast_2d(q)
        d = q[:, None, :] - self.a[None]
        l1 = (d[..., 0] * self.v1[None, :, 1] - self.v1[None, :, 0] * d[..., 1]) / self.den[None]
        l2 = (self.v0[None, :, 0] * d[..., 1] - d[..., 0] * self.v0[None, :, 1]) / self.den[None]
        l0 = 1 - l1 - l2
        inside = np.minimum(np.minimum(l0, l1), l2)
        t = inside.argmax(1); i = np.arange(len(q))
        L = np.stack([l0[i, t], l1[i, t], l2[i, t]], 1)
        f = self.F[t]
        P = (self.P[f] * L[..., None]).sum(1); N = (self.N[f] * L[..., None]).sum(1)
        return P, N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)

def sst(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

# (01/11 d) superficie suave por diente: la tira de Konami es gruesa y su uv no es proporcional al 3D, asi que un contorno
# limpio en uv salia con quiebres en 3D (dientes 'rotos'). Se ajusta un parche polinomico (grado 2 en u y en v) a la tira
# sobre la caja del diente y el diente se arma sobre ese parche.
def local_patch(M, u0, u1, v0, v1, n=9, deg=2, degv=None):
    uu, vv = np.meshgrid(np.linspace(u0, u1, n), np.linspace(v0, v1, n))
    q = np.stack([uu.ravel(), vv.ravel()], 1)
    Ps, Ns = M(q)
    cu, su = 0.5 * (u0 + u1), max(0.5 * (u1 - u0), 1e-6); cv, sv = 0.5 * (v0 + v1), max(0.5 * (v1 - v0), 1e-6)
    def basis(q, du=0, dv=0):
        x = (q[:, 0] - cu) / su; y = (q[:, 1] - cv) / sv
        cols = []
        for i in range(deg + 1):
            for j in range((deg if degv is None else degv) + 1):
                bx = x ** i if du == 0 else (i * x ** (i - 1) / su if i > 0 else 0 * x)
                by = y ** j if dv == 0 else (j * y ** (j - 1) / sv if j > 0 else 0 * y)
                cols.append(bx * by)
        return np.stack(cols, 1)
    C, *_ = np.linalg.lstsq(basis(q), Ps, rcond=None)
    nref = Ns.mean(0)
    def f(q):
        q = np.atleast_2d(q)
        P = basis(q) @ C
        N = np.cross(basis(q, du=1) @ C, basis(q, dv=1) @ C)
        N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
        if (N @ nref).mean() < 0: N = -N
        return P, N
    return f

# ---------------------------------------------------------------- un diente
def tooth(M, uL, uR, vG, vI, kind, upper, na=20, nb=13, ext=0.0, mes=1.0, seed=0, uvbox=None, tw=None):   # (02/11) nb 15 -> 14: lugar para la garganta; (02/11 d) na 22 -> 20, nb 14 -> 13: lugar para las 8 muelas 3D (tope 21845 tri)
    """M: UVMap de la tira. uL/uR: limites en u; vG: v del cuello (encia); vI: v del borde incisal. kind: 'I' incisivo,
    'C' canino, 'P' premolar/molar. mes: lado de cx (+1/-1) que mira al centro de la boca (mesial). Devuelve P, UV, F
    (01/11 e) mas natural (Agustin 'poco naturales'): perfil recto (el parche sigue la tira a lo ancho pero es recto a lo alto:
    antes copiaba la 'S' de la tira subdividida), esquina mesial casi recta y distal redondeada, canino con la punta corrida a
    mesial, cara de adelante convexa con el maximo en el tercio del cuello y dos surcos suaves en los incisivos, y una
    irregularidad chica por diente (largo, profundidad y giro) con semilla fija."""
    rng = np.random.default_rng(1000 + seed)
    # (02/11) Agustin: 'evitar que los dientes se deformen para adentro y queden chuecos': sin giro por diente, y la
    # irregularidad de largo/profundidad mas chica (antes 0.18 / 0.12 mm y giro +-1)
    jit_len, jit_dep, jit_rot = rng.uniform(-0.10, 0.10) * MM, rng.uniform(-0.05, 0.05) * MM, 0.0
    if REAL_TEETH and PROF.get('asym') and kind != 'P':
        # (09/11 d) Agustin: 'le falta un poco de asimetria': incisivos y caninos con largo, profundidad y giro propios
        # (semilla fija por diente) y un lado de la boca apenas mas largo que el otro; chico para no volver a 'chuecos' (02/11)
        aL, aD, aR, aS = PROF['asym']
        jit_len = rng.uniform(-aL, aL) * MM + aS * mes * MM
        jit_dep, jit_rot = rng.uniform(-aD, aD) * MM, rng.uniform(-aR, aR)
    M0 = M
    if PATCH:
        h = vI - vG
        # (02/11) Agustin: las muelas del final se doblaban (el parche de grado 2 seguia la vuelta de la tira en la esquina
        # del arco, y el diente salia con un quiebre). Premolares y muelas: parche PLANO (grado 1 = la orientacion promedio
        # de la tira en el lugar del diente), recto a lo ancho y a lo alto; la forma la ponen la convexidad y la seccion
        deg = 1 if kind == 'P' else PDEG
        M = local_patch(M, uL - 0.1 * (uR - uL), uR + 0.1 * (uR - uL), vG - 0.3 * h, vI, deg=deg, degv=1)   # (01/11 f) todos rectos a lo alto
    gap = (0.012 if kind == 'IC' else 0.035) * (uR - uL)   # (01/11 d) los centrales se tocan
    if REAL_TEETH: gap = (PROF.get('gap_ic', REAL_GAP_IC) if kind == 'IC' else PROF.get('gap', REAL_GAP)) * (uR - uL)   # (03/11 v19c) Agustin: 'ni pegados ni separados': un poco de aire entre dientes
    uL2, uR2 = uL + gap, uR - gap
    uc = 0.5 * (uL2 + uR2)
    b = -0.22 + 1.22 * np.sin(np.linspace(0, 1, nb) * np.pi / 2) ** 1.2   # mas filas cerca del borde incisal
    th = np.linspace(0, 2 * np.pi, na + 1)[:-1]   # 0 = adelante
    pw = {'IC': 3.4, 'I': 2.6, 'C': 2.5, 'P': 2.3}[kind]   # (01/11 e) 4.2 -> 3.4: cuadrados pero no 'baldosa'
    # (02/11) la mitad de atras (lingual) redonda (pw 2): con la seccion cuadrada la cara de atras de las muelas era una
    # placa plana que, vista desde adentro de la boca con la luz de adelante, salia como un bloque negro (Agustin)
    pwv = np.where(np.cos(th) < 0, 2.0, pw)
    cx = np.sign(np.sin(th)) * np.abs(np.sin(th)) ** (2 / pwv)   # a lo ancho (-1..1)
    cy = np.sign(np.cos(th)) * np.abs(np.cos(th)) ** (2 / pwv)   # adelante (+1) / atras (-1)
    Ps = []; UVs = []
    # (01/11) mordida (Agustin): el diente se alarga 'ext' metros mas alla del borde de la tira, en la direccion cuello -> borde
    Pg, _ = M(np.array([[uc, vG]])); Pi, _ = M(np.array([[uc, vI]]))
    dI = (Pi - Pg)[0]; dI /= max(np.linalg.norm(dI), 1e-9)
    # (02/11) 'el diente tiene que estar sobre la encia': el parche ajustado puede quedar corrido de la tira real en el cuello;
    # se ancla el centro del cuello del diente al punto real de la tira (corrimiento parejo de todo el diente)
    Pg0, _ = M0(np.array([[uc, vG + 0.1 * (vI - vG)]])); Pg1, _ = M(np.array([[uc, vG + 0.1 * (vI - vG)]]))
    anchor = (Pg0 - Pg1)[0]
    # (02/11) tamano minimo en 3D: los premolares/muelas pintados del fondo de la tira son chiquitos (abajo 1-3 mm de alto, el
    # ultimo hasta al reves) y salian como astillas hundidas en la encia (Agustin). El eje del diente apunta siempre hacia el
    # borde libre (arriba +y para los de abajo, -y para los de arriba) y el alto / ancho en 3D no bajan de un minimo: el
    # diente se extruye recto por su eje con ese alto (Ht) en vez de seguir la franja pintada
    if (dI[1] > 0) == upper: dI = -dI
    h3d = float(np.linalg.norm(Pi - Pg)); Ht = h3d
    wsc = 1.0
    if kind == 'P':
        Ht = max(h3d, (5.5 if (upper or not KONAMI_BITE) else 4.5) * MM)   # (02/11 g) abajo 4.5: con la mordida de Konami no se cruzan con los de arriba
        PL, _ = M(np.array([[uL2, vG]])); PR, _ = M(np.array([[uR2, vG]]))
        w3d = float(np.linalg.norm(PR - PL)); wsc = max(1.0, 4.8 * MM / max(w3d, 1e-6))
    if tw:   # (03/11) ancho real del diente en 3D (la uv de la tira no es proporcional al 3D a la altura del diente)
        PL, _ = M(np.array([[uL2, 0.5 * (vG + vI)]])); PR, _ = M(np.array([[uR2, 0.5 * (vG + vI)]]))
        w3d = float(np.linalg.norm(PR - PL)); wsc = max(wsc if kind == 'P' else 0.0, (tw * (1 - 2 * gap / (uR - uL))) / max(w3d, 1e-6))
    # uv de la textura: la propia, o la de un diente bien pintado de la misma fila (uvbox = (uL2, uR2, vG, vI)) cuando la
    # propia es una franja de pocos pixeles
    tL, tR, tG, tI = uvbox if uvbox is not None else (uL2, uR2, vG, vI)
    # (02/11 g) con la mordida de Konami el premolar crece hacia la encia: bpos = donde cae cada fila respecto de la linea de la
    # encia pintada (< 0 = debajo de la encia); el hundimiento detras de la encia se calcula con bpos (si no, la parte nueva
    # quedaba por delante de la encia y la tira la cortaba en franjas oscuras)
    off = ((Ht - h3d) / max(Ht, 1e-9)) if (KONAMI_BITE and kind == 'P' and upper) else 0.0
    for bi in b:
        # el borde incisal: canino en punta, incisivos con esquinas apenas redondeadas
        if kind == 'C':
            bmax = 1 - (PROF['canine'] if REAL_TEETH else 0.30) * np.abs(cx - 0.18 * mes) ** 1.35
        else:
            r_m, r_d = {'IC': (0.03, 0.11), 'I': (0.07, 0.24), 'P': (0.14, 0.14)}[kind]
            if REAL_TEETH:   # (03/11) dentadura real: esquinas casi rectas en los incisivos (los de abajo, rectos)
                r_m, r_d = {'IC': PROF['corner_ic'], 'I': PROF['corner_i_up'] if upper else PROF.get('corner_lo', (0.03, 0.06)), 'P': (0.14, 0.14)}[kind]
            rr = np.where(cx * mes > 0, r_m, r_d)
            bmax = 1 - rr * sst(0.5, 1.0, np.abs(cx)) ** 1.6
        bb = bi * bmax if bi > 0 else bi * np.ones_like(cx)
        v = vG + (vI - vG) * bb
        uu = uc + 0.5 * (uR2 - uL2) * cx * wsc
        # cuello mas angosto
        narrow = (0.88 + 0.12 * sst(-0.1, 0.45, bb)) if kind == 'IC' else (0.80 + 0.20 * sst(-0.1, 0.45, bb))
        if REAL_TEETH and kind in ('I', 'C'):   # (03/11) el ancho maximo (contacto) en el tercio incisal; cuello algo angosto
            narrow = NECK_N + (1 - NECK_N) * sst(-0.1, 0.55, bb)   # (03/11 v19c) cuello mas angosto: asoma la papila de encia entre dientes
        uu = uc + (uu - uc) * narrow
        if kind == 'P':
            Pf, Nf = M(np.stack([uu, vG * np.ones_like(uu)], 1))
            Pf = Pf + dI[None, :] * ((bb * Ht - (Ht - h3d)) if (KONAMI_BITE and upper) else bb * Ht)[:, None]   # abajo: desde la encia
        else:
            Pf, Nf = M(np.stack([uu, v], 1))
        Pf = Pf + dI[None, :] * ((ext + jit_len) * sst(0.35, 1.0, bb))[:, None]
        # (01/11 e) resalte (overjet): los incisivos de abajo quedaban 0.4 mm metidos dentro de la cara de atras de los de
        # arriba (franja clara cruzando los centrales). Arriba el borde sale 0.3 mm hacia afuera, abajo entra 0.9 mm
        # (02/11) el resalte era una inclinacion (crecia del cuello al borde) y los de abajo quedaban volcados hacia adentro:
        # ahora es un corrimiento casi parejo de todo el diente (el cuello ya esta hundido en la encia)
        shift = (0.3 * MM * sst(0.0, 0.4, bb)) if upper else (-OVERJET_LO * sst(0.0, 0.4, bb))
        if KONAMI_BITE:
            # adelante sin resalte (como Konami); premolares: los de arriba un poco hacia afuera y los de abajo hacia la lengua, para
            # que la cuspide de afuera de arriba quede por fuera de la de abajo al cerrar (sin cruzarse)
            shift = ((0.5 if upper else -1.0) * MM * sst(0.0, 0.4, bb)) if kind == 'P' else 0.0 * bb
            if REAL_TEETH and kind != 'P' and not upper:
                shift = -LO_OVERJET * MM * sst(0.0, 0.4, bb)   # (03/11) resalte real: los de abajo detras de los de arriba
            if REAL_TEETH and kind == 'I' and upper and PROF['lat_back']:
                shift = -PROF['lat_back'] * MM * sst(0.0, 0.4, bb)   # (04/11) perfil: laterales un poco detras de los centrales
        Pf = Pf + Nf * np.reshape(shift, (-1, 1))
        # espesor (m): grueso en el cuello, fino en el borde
        T0 = {'IC': 6.5, 'I': 6.0, 'C': 7.5, 'P': 7.0}[kind] * MM; T1 = {'IC': 1.1, 'I': 1.0, 'C': 2.0, 'P': 4.5}[kind] * MM   # (02/11) P 8/5.5 -> 7/4.5
        T = T0 + (T1 - T0) * sst(0.0, 1.0, bb) ** 0.8
        if kind == 'P':   # (02/11) altura de contorno lingual: la cara de atras es convexa a lo alto (no una placa)
            T = T + 0.6 * MM * np.exp(-((bb - 0.45) / 0.3) ** 2)
        if kind in ('IC', 'I'):   # (01/11 e) cingulo junto al cuello y fosa (pala) en el medio de la cara de atras
            T = T + 0.4 * MM * np.exp(-((bb - 0.18) / 0.12) ** 2) - 0.35 * MM * np.exp(-(cx / 0.55) ** 2) * sst(0.3, 0.6, bb) * sst(0.95, 0.75, bb)
        # adelante: sobre la tira + abultado del esmalte; el cuello se hunde detras de la encia
        # (01/11 d) el cuello se hunde siguiendo el festoneado de la encia pintada (make_oral.clean_gum: papila de 0.30 del alto,
        # ancho 0.14): al centro del diente la encia corta en bb ~0.12, a los costados (papila) mas abajo. Y la cara de
        # adelante sale siempre 0.3 mm por delante de la tira (antes en las esquinas quedaba al ras y la tira la 'mordia')
        duT = 0.5 * (1 - np.abs(cx)) * (1 - 2 * gap / (uR - uL))
        gum_r = make_oral.GUM_UP if upper else GUM
        # (02/11 v17b) arriba el diente sale de la encia en la linea de Konami (antes 0.12 del alto mas abajo, ~1 mm)
        bs = (BS_UP if upper else 0.12) + (0.30 / (1 - gum_r)) * np.exp(-(duT / 0.14) ** 2)
        bpos = bb - off
        lift = 0.3 * MM * sst(bs - 0.05, bs + 0.10, bpos)
        # (01/11 e) convexidad: maximo en el tercio del cuello (altura de contorno) y se aplana hacia el borde; surcos de
        # desarrollo en los incisivos (entre los 3 lobulos) en la mitad incisal; giro chico del diente (jit_rot) = un lado
        # mas adelante que el otro; profundidad chica por diente (jit_dep)
        prof = sst(0.0, 0.22, bb) * (1 - 0.55 * sst(0.3, 1.0, bb))
        convex = 0.62 * MM * np.clip(1 - cx ** 2, 0, 1) ** 0.8 * prof
        dev = (0.08 * MM * np.exp(-((np.abs(cx) - 0.38) / 0.12) ** 2) * sst(0.35, 0.85, bb)) if kind in ('IC', 'I') else 0 * cx
        rot = 0.25 * MM * jit_rot * cx * sst(0.0, 0.3, bb)
        bulge = convex - dev + rot + jit_dep * sst(0.0, 0.3, bb) + lift - SINK * sst(bs, bs - 0.27, bpos)
        Pf = Pf + anchor[None, :]
        front = Pf + Nf * bulge[:, None]
        # (01/11 d) la cara de atras sale con la normal PROMEDIO del diente, horizontal: con la normal de cada punto, en las muelas (donde la
        # tira dobla fuerte y el diente es grueso) la cara de atras se cruzaba y quedaban astillas
        nm = Nf.mean(0); nm[1] = 0; nm /= max(np.linalg.norm(nm), 1e-9)   # horizontal: hacia adentro del arco
        back = Pf - nm[None, :] * np.reshape(T, (-1, 1))
        y = (cy + 1) / 2   # 1 adelante, 0 atras
        # cara de atras un poco mas angosta (forma de pala)
        P = front * y[:, None] + back * (1 - y[:, None])
        # en el borde incisal la seccion se cierra (el diente termina en filo)
        Ps.append(P)
        # uv: adelante la del diente pintado; atras la misma (espejada al ancho)
        # (01/11 d) uv solo del CENTRO del diente pintado: los bordes de la tira pintada (separaciones oscuras, encia, sombras
        # de Konami) quedaban sobre el borde y el cuello del diente 3D y lo hacian ver desparejo/roto
        tc = 0.5 * (tL + tR)
        ut = tc + (uu - uc) * (tR - tL) / max(uR2 - uL2, 1e-6) / wsc * UV_W
        vt = tG + (tI - tG) * (UV_V0 + (UV_V1 - UV_V0) * np.clip(bb, 0, 1))
        UVs.append(np.stack([ut, vt * np.ones_like(ut)], 1))
    P = np.stack(Ps); UV = np.stack(UVs)                  # (nb, na, ...)
    # tapa del borde incisal: un vertice al centro de la ultima fila
    F = []
    idx = np.arange(nb * na).reshape(nb, na)
    for i in range(nb - 1):
        for j in range(na):
            a, b_, c, d = idx[i, j], idx[i, (j + 1) % na], idx[i + 1, (j + 1) % na], idx[i + 1, j]
            F += [[a, b_, c], [a, c, d]]
    P = P.reshape(-1, 3); UV = UV.reshape(-1, 2)
    cap = P[idx[-1]].mean(0); capuv = UV[idx[-1]].mean(0)
    ci = len(P); P = np.vstack([P, cap]); UV = np.vstack([UV, capuv])
    for j in range(na): F.append([idx[-1, j], idx[-1, (j + 1) % na], ci])
    F = np.array(F)
    # orientar hacia afuera: la normal de las caras de adelante tiene que apuntar como la tira
    n = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    cen = P.mean(0)
    s = np.sign(((P[F].mean(1) - cen) * n).sum(1)).sum()
    if s < 0: F = F[:, ::-1]
    # (01/11) Fox / Konami usa el orden contrario (horario visto desde afuera): sin esto el juego descarta la cara de adelante
    return P, UV, F[:, ::-1]


def build(src_fmdl, texdir, log=print):
    f = fmdl.Fmdl(src_fmdl); m = fmdl.meshes(f)[0]
    grp = f.groups[0]; names = [b['name'] for b in f.bones]
    P, Nk, UV, F = m['pos'], m['nrm'][:, :3], m['uv'], m['faces']
    W = m['w']; IDX = m['idx']
    # pesos densos por hueso (6)
    Wd = np.zeros((len(P), len(names)))
    for k in range(4): np.add.at(Wd, (np.arange(len(P)), IDX[:, k]), W[:, k])
    lab = comps(m)
    def comp_of(uvbox):
        for c in np.unique(lab):
            u = UV[lab == c]
            if (np.abs(u.min(0) - uvbox[:2]) < 0.06).all() and (np.abs(u.max(0) - uvbox[2:]) < 0.06).all(): return c
        raise SystemExit('no encuentro la parte %s' % (uvbox,))
    c_up = comp_of(np.array([0.02, 0.02, 0.98, 0.33])); c_lo = comp_of(np.array([0.02, 0.34, 0.98, 0.65]))
    c_tg = comp_of(np.array([0.30, 0.67, 0.71, 0.98]))
    log('  partes de Konami: %d (dientes arriba %d, abajo %d, lengua %d)' % (lab.max() + 1, c_up, c_lo, c_tg))
    bsm = make_oral.load(os.path.join(texdir, 'oral_bsm.ftex')); trm = make_oral.load(os.path.join(texdir, 'oral_trm.ftex'))
    t512, _ = make_oral.masks(bsm, trm)
    rows = make_oral.rows_layout(t512)
    teeth_mask = ndi.gaussian_filter(t512, 1.0)
    parts = []   # (P, UV, F, Wd)
    def sub(c, times, smooth=1.0, fmask=None):
        sel = np.where(lab == c)[0]; remap = -np.ones(len(P), np.int64); remap[sel] = np.arange(len(sel))
        fm = (lab[F[:, 0]] == c)
        if fmask is not None: fm &= fmask
        Fc = remap[F[fm]]
        Pc, UVc, Wc = P[sel], UV[sel], Wd[sel]
        for _ in range(times):
            Pc, Fc, (UVc, Wc) = loop(Pc, Fc, [UVc, Wc], smooth)
        return Pc, UVc, Fc, Wc
    # ---- tiras (encia), detras de los dientes nuevos
    strip_maps = {}
    for c, r, up in ((c_up, rows[0], True), (c_lo, rows[1], False)):
        Pc, UVc, Fc, Wc = sub(c, 2)
        GUM = r['gum']   # (02/11 v17b) arriba 0: la encia de Konami
        Nc = vnormals(Pc, Fc)
        # orientar: la tira mira hacia adelante (+z) en la zona de los dientes
        if Nc[:, 2].mean() < 0: Nc = -Nc
        strip_maps[c] = UVMap(Pc.copy(), Nc.copy(), UVc, Fc)
        px = np.clip((UVc * 512).astype(int), 0, 511)
        tm = teeth_mask[px[:, 1], px[:, 0]]
        # (01/11 b) mas encia: la textura (make_oral.gum_warp) ya baja la encia GUM del alto del diente; la tira solo se corre
        # hacia atras donde queda diente pintado (s > GUM), no en la franja nueva de encia
        xs = UVc[:, 0] * 512; vv = UVc[:, 1] * 512
        g = np.interp(xs, np.arange(512), r['g']); inc = np.interp(xs, np.arange(512), r['inc'])
        sv = (vv - g) / np.where(np.abs(inc - g) < 1e-6, 1e-6, inc - g)
        inx = (xs >= r['x0'] - 1) & (xs <= r['x1'] + 1)
        tm = tm * np.where(inx, make_oral.sst(GUM, GUM + 0.12, sv), 1.0)
        Pc = Pc - Nc * (0.9 * MM * make_oral.sst(0.3, 0.7, tm))[:, None]
        # (01/11 i) Agustin: 'borrar la otra boca asi no se sobreponen'. Los dientes PINTADOS de Konami de la tira quedaban
        # detras de los 3D y asomaban (gris entre los bordes, de costado). Se sacan las caras de la tira debajo de la encia
        # (s > GUM + papila + margen) en la zona de los dientes: queda solo la encia con sus papilas; el fondo es la boca
        if STRIP_GUM:
            # (02/11) Agustin: 'bloque negro a los dos lados de la muela' con la boca abierta vista desde adentro. Con el corte
            # (CUT_STRIP) quedaban agujeros entre las muelas y pedazos de diente pintado (s entre GUM y GUM + 0.42) que asomaban
            # de costado. Ahora la tira queda ENTERA (sin agujeros) y en la zona de los dientes pintados (s > GUM, doblez
            # incluido) la uv se pliega de vuelta hacia la encia: s' = GUM - STRIP_GUM (s - GUM). Es continua en s = GUM (sin
            # costura) y lo que se ve de la tira entre los dientes 3D, desde cualquier lado, es encia
            tz = inx & (sv > GUM)
            sv2 = np.where(tz, GUM - STRIP_GUM * (sv - GUM), sv)
            vv2 = g + sv2 * (inc - g)
            UVc = UVc.copy(); UVc[:, 1] = np.where(tz, vv2 / 512.0, UVc[:, 1])
            # (02/11 c) Agustin: 'encias por arriba de los dientes': la tira sigue en 3D por delante/detras de las coronas
            # (el diente pintado de Konami y el doblez hacia el paladar / piso), y pintada de encia se veia como una banda
            # rosada sobre la cara de atras de los dientes. Esa parte de la tira (s > GUM) se recoge al cuello: queda a un
            # 20 % de su largo pegada a la encia, detras de los cuellos de los dientes 3D, y el paladar / piso siguen de ahi
            # Solo pasando las puntas de las papilas (s > GUM + 0.35): la franja de la encia y las papilas no se mueven (si no,
            # quedaban astillas de encia por delante de los cuellos)
            tf = inx & (sv > GUM + FOLD_S)
            # solo arriba: abajo la tira recogida cruzaba por delante de los incisivos (lineas oscuras) y desde arriba la de
            # abajo ya queda tapada por los dientes
            if STRIP_FOLD < 1 and tf.any():
                Mg = strip_maps[c]
                s0 = GUM + FOLD_S
                Pg, Ng = Mg(np.stack([UVc[tf, 0], (g[tf] + s0 * (inc[tf] - g[tf])) / 512.0], 1))
                # hacia atras con la normal de la tira EN LA ENCIA (Ng, mira hacia afuera); la del vertice (Nc) en el doblez
                # apunta para arriba / atras y abajo empujaba la tira por delante de los incisivos
                Pg = Pg - Ng * (FOLD_BACK * MM)
                Pc = Pc.copy(); Pc[tf] = Pg + (Pc[tf] - Pg) * STRIP_FOLD
        if CUT_STRIP:
            # solo la zona del diente pintado; el doblez de la tira pasando el borde incisal (conecta con el piso / paladar)
            # queda para no abrir agujeros, pero con la uv de un punto de mucosa oscura del atlas (no muestra bordes pintados)
            dead = inx & (sv > GUM + CUT_STRIP) & (sv <= 1.0)
            fold = inx & (sv > 1.0)
            UVc = UVc.copy(); UVc[fold] = (0.20, 0.75)
            keep = ~dead[Fc].all(1)
            Fc = Fc[keep]
            used = np.unique(Fc); remap = -np.ones(len(Pc), np.int64); remap[used] = np.arange(len(used))
            Pc, UVc, Wc, Fc = Pc[used], UVc[used], Wc[used], remap[Fc]
        parts.append((Pc, UVc, Fc, Wc, 'tira'))
    # ---- dientes
    nteeth = 0; tooth_up = []; best_by_row = [None, None]
    for c, r, up in ((c_up, rows[0], True), (c_lo, rows[1], False)):
        M = strip_maps[c]
        GUM = r['gum']
        pk = np.concatenate([[r['x0']], r['pk'], [r['x1']]]).astype(float)
        ntt = len(pk) - 1; mid = ntt / 2.0
        pk_paint = pk.copy(); row_parts = []
        if REAL_TEETH:
            pk, tw_row = real_bounds(M, r, pk, up, log)
        # (02/11) caja uv de referencia de la fila: el premolar mejor pintado (mas alto en uv); los dientes con menos de 22 px
        # de alto pintado usan esa textura
        best = None
        for i in range(ntt):
            xl, xr = pk[i], pk[i + 1]; xc = 0.5 * (xl + xr)
            gi = np.interp(xc, np.arange(512), r['g']); ii = np.interp(xc, np.arange(512), r['inc'])
            rank = int(abs((i + 0.5) - mid))
            if rank >= 3 and (best is None or abs(ii - gi) > best[0]):
                g2 = (0.035 * (xr - xl))
                best = (abs(ii - gi), ((xl + g2) / 512.0, (xr - g2) / 512.0, (gi + GUM * (ii - gi)) / 512.0, min(ii / 512.0, 0.302) if up else max(ii / 512.0, 0.505)))
        _fr = [np.interp(0.5 * (pk[i] + pk[i + 1]), np.arange(512), r['inc']) for i in range(ntt) if int(abs((i + 0.5) - mid)) < 3 and pk_paint[i + 1] - pk_paint[i] >= 8]
        even_ii = float(np.median(_fr)) if _fr else 0.0
        for i in range(ntt):
            xl, xr = pk[i], pk[i + 1]
            if pk_paint[i + 1] - pk_paint[i] < 8: continue
            xc = 0.5 * (xl + xr)
            gi = np.interp(xc, np.arange(512), r['g']); ii = np.interp(xc, np.arange(512), r['inc'])
            # posicion desde el centro: 0,1 incisivos, 2 canino, el resto premolares/molares
            rank = int(abs((i + 0.5) - mid))
            kind = 'IC' if (rank == 0 and up) else ('I' if rank < 2 else ('C' if rank == 2 else 'P'))
            if PROF.get('even') and rank < 3:   # (04/11 c) Messi: borde incisal parejo (carillas): todos los de adelante al mismo nivel
                ii = even_ii
            vG, vI = (gi + GUM * (ii - gi)) / 512.0, ii / 512.0
            if up: vI = min(vI, 0.302)         # el frente de la tira de arriba termina en v 0.30 (despues dobla hacia atras)
            else: vI = max(vI, 0.505)
            # (01/11 e) curva de la sonrisa: centrales un poco mas largos, laterales mas cortos, caninos con la punta, muelas
            # mas cortas; mesial = hacia el centro de la fila
            dext = ({'IC': -0.1, 'I': -0.5, 'C': -0.2, 'P': -0.6} if up else {'I': 0.0, 'C': 0.25, 'P': -0.3})[kind] * MM
            mes = 1.0 if (i + 0.5) < mid else -1.0
            uvbox = best[1] if (abs(ii - gi) < 22 and best is not None) else None
            lastP = kind == 'P' and rank >= int(mid) - 1
            if REAL_TEETH and (uvbox is None or lastP):
                # (03/11) la textura sigue siendo la del diente pintado de Konami de ese lugar (los limites 3D se movieron)
                # (03/11 v19c) el ultimo premolar usa la del vecino hacia el centro: su franja pintada tiene encia (salia rojizo)
                ip = i + (int(mes) if lastP else 0)
                xl0, xr0 = pk_paint[ip], pk_paint[ip + 1]; xc0 = 0.5 * (xl0 + xr0); g2 = 0.035 * (xr0 - xl0)
                gi0 = np.interp(xc0, np.arange(512), r['g']); ii0 = np.interp(xc0, np.arange(512), r['inc'])
                uvbox = ((xl0 + g2) / 512.0, (xr0 - g2) / 512.0, (gi0 + GUM * (ii0 - gi0)) / 512.0, min(ii0 / 512.0, 0.302) if up else max(ii0 / 512.0, 0.505))
            if REAL_TEETH:
                # (03/11) curva de la sonrisa real: laterales ~0.7 mm mas cortos que los centrales, canino con la punta casi al nivel
                ext = ((PROF.get('ext_p', 0.0) if up else 0.0) if kind == 'P' else (PROF['ext_up'][kind] if up else PROF.get('ext_lo', LO_OVERBITE))) * MM   # (09/11) ext_p: premolares de arriba mas cortos (arco de la sonrisa)
            else:
                ext = ((0.0 if kind == 'P' else (-0.2 if up else 1.0)) * MM if KONAMI_BITE else (EXT_UP if up else EXT_LO) + dext)
            Pt, UVt, Ft = tooth(M, xl / 512.0, xr / 512.0, vG, vI, kind, up, ext=ext, mes=mes, tw=(tw_row[i] if REAL_TEETH else None),
                                seed=nteeth, uvbox=uvbox)
            if os.environ.get('TOOTH_DBG'): log('    diente %s %d %s x %.1f y %.1f z %.1f ancho %.1f' % ('arr' if up else 'abj', i, kind, *(Pt.mean(0) * 1000), np.ptp(Pt[:, 0]) * 1000))
            wt = np.zeros((len(Pt), len(names))); wt[:, names.index('sk_head' if up else 'skf_jaw')] = 1
            row_parts.append([Pt, UVt, Ft, wt, 'diente']); nteeth += 1; tooth_up.append(up)
        if REAL_TEETH: fit_row(row_parts, log, 'arriba' if up else 'abajo')
        parts.extend(tuple(q) for q in row_parts)
        best_by_row[0 if up else 1] = best[1]
    # ---- (02/11) muelas 3D del fondo (Agustin: 'que las muelas sean 3d'): detras del ultimo premolar de cada lado la tira
    # de Konami es solo encia (no hay diente pintado). Se arman MOLARS muelas por lado y por arcada siguiendo la linea de la
    # encia de la tira de abajo (la de arriba ahi dobla hacia el paladar): abajo sobre la encia, arriba a la altura de los
    # premolares de arriba y un poco hacia el carrillo (la arcada de arriba es mas ancha)
    nmol = 0; foot = []; lo_tips = {}; mol_parts = []
    if MOLARS:
        Rl = rows[1]; Ml = strip_maps[c_lo]
        Pstrip_lo = [q[0] for q in parts if q[4] == 'tira'][1]   # la tira de abajo (las tiras van arriba, abajo)
        xs = np.arange(2, 510, 1.0)
        gl = np.interp(xs, np.arange(512), Rl['g']) / 512.0
        line, _ = Ml(np.stack([xs / 512.0, gl], 1))
        teeth_parts = [(p[0], up) for p, up in zip([q for q in parts if q[4] == 'diente'], tooth_up)]
        for up in (False, True):
            uvb = best_by_row[0 if up else 1]
            for side in (-1, 1):
                # el diente de esta arcada mas atras de este lado
                cand = [p for p, u in teeth_parts if u == up and np.sign(p[:, 0].mean()) == side]
                last = min(cand, key=lambda p: p[:, 2].min())
                ztail = last[:, 2].min(); ybot = last[:, 1].min(); ytop = last[:, 1].max()
                sel = (np.sign(line[:, 0]) == side)
                ln = line[sel]; ln = ln[np.argsort(-ln[:, 2])]   # de adelante hacia atras
                ln = ln[ln[:, 2] < ztail + 8.0 * MM]   # (04/11) margen hacia adelante para poder acercar la 1a muela
                seg = np.r_[0, np.cumsum(np.linalg.norm(np.diff(ln[:, [0, 2]], axis=0), axis=1))]
                s_tail = float(np.interp(-(ztail + 0.3 * MM), -ln[:, 2], seg))   # donde la encia pasa por detras del ultimo premolar
                s0 = s_tail + (MOL_S0_UP if up else MOL_S0_LO) * MM
                # (04/11) Agustin: 'parece que falta una muela y estan muy separadas del resto' (habia 1.6-2.9 mm de hueco): la 1a
                # muela se arma, se mide el hueco con el ultimo premolar y se vuelve a armar corrida para dejar MOL_AIR mm
                from scipy.spatial import cKDTree as _KD
                k = 0; retried = 0
                while k < min(MOLARS, len(MOLAR_L)):
                    Lk = MOLAR_L[k] * MM
                    if s0 + Lk > seg[-1]: break
                    pa = np.array([np.interp(s0, seg, ln[:, i]) for i in range(3)]); pb = np.array([np.interp(s0 + Lk, seg, ln[:, i]) for i in range(3)])
                    t = pb - pa; t[1] = 0; t /= np.linalg.norm(t)
                    bd = np.array([-t[2], 0.0, t[0]])
                    if bd[0] * side < 0: bd = -bd     # hacia el carrillo
                    Wb = (10.0 if up else 9.5) * MM
                    mid = 0.5 * (pa + pb)
                    kind = ('up' if up else 'lo') + str(k + 1)
                    tipz = float(molar.fields(kind, np.array([c_[0] for c_ in molar.CUSPS[kind]]), np.array([c_[1] for c_ in molar.CUSPS[kind]]))[0].max())
                    # hacia el carrillo lo justo: la lengua (de costado llega a |x| 17.5 mm) no tiene que atravesarlas
                    if not up:
                        # (02/11 g) el cuello a la altura de la cresta de la encia bajo la muela (mediana de la tira de abajo en la
                        # huella). La encia de atras de Konami sube 1-3 mm sobre los premolares: las muelas siguen esa curva (Spee)
                        Hc = 3.6 * MM
                        cc = mid - bd * (0.5 * Wb - 1.0 * MM)
                        dd = Pstrip_lo - cc[None]
                        foot_m = (np.abs(dd @ t) < 0.5 * Lk) & (np.abs(dd @ bd) < 0.5 * Wb)
                        cc[1] = (np.percentile(Pstrip_lo[foot_m, 1], 50) if foot_m.sum() >= 4 else mid[1]) - 0.4 * MM
                        lo_tips[(side, k)] = cc[1] + Hc + tipz   # (la de arriba se apoya donde estaba antes de bajarla: no baja con ella)
                        cc[1] -= MOL_DROP_LO * MM   # (04/11) Agustin: 'bajar un poco las muelas de abajo'
                    else:
                        # (02/11 g) 'la mordida como la de Konami': arriba y abajo NO se cruzan al cerrar; las cuspides de arriba
                        # quedan 0.2 mm sobre las de abajo de la misma muela, un poco hacia el carrillo
                        Hc = 5.0 * MM
                        cc = mid - bd * (0.5 * Wb - 2.5 * MM)
                        ytip = lo_tips.get((side, k), ybot + 0.4 * MM) + 0.2 * MM
                        cc[1] = ytip + tipz + Hc
                    Pm, UVm, Fm = molar.geometry(cc, t, bd, up, 0.94 * Lk, Wb, Hc, 1.5 * MM, kind, seed=nmol)
                    if k == 0 and retried < 3:
                        retried += 1
                        gap = float(_KD(last).query(Pm)[0].min())
                        if abs(gap - MOL_AIR * MM) > 0.15 * MM:
                            s0 -= gap - MOL_AIR * MM
                            log('  muela 1 %s lado %+d: hueco %.1f mm -> se corre' % ('arriba' if up else 'abajo', side, gap / MM)); continue
                    wm = np.zeros((len(Pm), len(names))); wm[:, names.index('sk_head' if up else 'skf_jaw')] = 1
                    parts.append((Pm, UVm, Fm, wm, 'diente')); nmol += 1; mol_parts.append(Pm)
                    foot.append((cc, t, bd, 0.94 * Lk, Wb, up, Hc + tipz))
                    s0 += Lk + 0.3 * MM; k += 1
        # la tira de Konami detras del ultimo premolar sube como una pared (la encia de atras) y pasaba por adentro de la segunda
        # muela (se veia una franja oscura cruzando la corona): lo que de la tira queda adentro de la huella de una muela, a la
        # altura de la corona, baja (arriba: sube) hasta el cuello de la muela
        moved = 0
        for Pp, _, _, Wp, tg in parts:
            if tg != 'tira': continue
            sup = Wp[:, names.index('sk_head')].mean() > 0.5   # tira de arriba
            for cc, t, bd, Lk, Wb, up, Hc in foot:
                if up != sup: continue
                d = Pp - cc[None]
                db = d @ bd   # del lado de la lengua el margen es mas ancho (la tira sube por ahi)
                inside = (np.abs(d @ t) < 0.5 * Lk + 1.2 * MM) & (db < 0.5 * Wb + 0.8 * MM) & (db > -(0.5 * Wb + 3.0 * MM))
                h = -d[:, 1] if up else d[:, 1]   # altura hacia la corona
                hit = inside & (h > -0.3 * MM) & (h < Hc + 2.0 * MM)
                Pp[hit, 1] = cc[1] + (0.3 * MM if up else -0.3 * MM); moved += int(hit.sum())
        log('  muelas 3D: %d (vertices de la tira bajados al cuello: %d)' % (nmol, moved))
    # ---- lengua: dos subdivisiones, surco central
    Pc, UVc, Fc, Wc = sub(c_tg, 2)
    Nc = vnormals(Pc, Fc)
    if Nc[:, 1].mean() < 0: Nc = -Nc
    xw = np.abs(Pc[:, 0]) / max(np.abs(Pc[:, 0]).max(), 1e-6)
    groove = 0.6 * MM * np.exp(-(xw / 0.12) ** 2) * (Nc[:, 1] > 0.5)
    Pc = Pc - Nc * groove[:, None]
    Wc = np.zeros_like(Wc); Wc[:, names.index('skf_jaw')] = 1
    parts.append((Pc, UVc, Fc, Wc, 'lengua'))
    # ---- el resto, una subdivision
    # (02/11) la pared plana del fondo (2 triangulos de Konami, en la parte de las mejillas) se saca: la reemplaza throat.py
    bw = throat.back_wall_faces(F, P) if THROAT else np.zeros(len(F), bool)
    for c in np.unique(lab):
        if c in (c_up, c_lo, c_tg): continue
        Pc, UVc, Fc, Wc = sub(c, 1, fmask=~bw)
        # (01/11 c) la lengua se reconoce por uv en el VS (hilo de expresiones): u 0.295..0.715, v 0.668..0.99 tiene que ser
        # SOLO lengua. El piso y las mejillas de Konami rozan esa caja (u 0.28..0.314): se corren a u <= 0.279 (zona oscura)
        hit = (UVc[:, 0] > 0.279) & (UVc[:, 0] < 0.5) & (UVc[:, 1] >= 0.66) & (UVc[:, 1] <= 1.0)
        UVc = UVc.copy(); UVc[hit, 0] = 0.279
        parts.append((Pc, UVc, Fc, Wc, 'resto'))
    # ---- (04/11) Agustin: 'ampliar los laterales del modelo que no son dientes para que no se note que esta abierto'. A los
    # costados, entre la tira de arriba (cabeza) y la de abajo (mandibula), no habia nada: con la boca abierta se veia el fondo
    # negro por el costado. Una pared de mejilla por lado (la mucosa del carrillo), un poco por fuera de las dos tiras, desde la
    # altura del premolar hasta el fondo (donde empieza la garganta); arriba pesa en sk_head y abajo en skf_jaw, asi se estira al
    # abrir como una mejilla. Textura: la mucosa pintada de la garganta (throat.UV_BOX), solo su parte clara.
    if CHEEK:
        import throat as _th
        tiras = [q[0] for q in parts if q[4] == 'tira']
        Pu, Pl = tiras[0], tiras[1]
        NZ, NY = 9, 5
        u0, u1, v0, v1 = _th.UV_BOX
        nadd = 0
        for side in (-1, 1):
            zs = np.linspace(0.152, 0.120, NZ)
            cols = []
            for z in zs:
                def outer(Q):
                    s_ = (np.abs(Q[:, 2] - z) < 2.5 * MM) & (np.sign(Q[:, 0]) == side)
                    if not s_.any(): return None
                    q = Q[s_]; xo = np.abs(q[:, 0]).max(); edge = q[np.abs(q[:, 0]) > xo - 2.0 * MM]
                    return xo, float(np.median(edge[:, 1]))
                a, b = outer(Pu), outer(Pl)
                if a is None or b is None: cols = None; break
                cols.append((max(a[0], b[0]) + 1.0 * MM, a[1], b[1]))
            if cols is None: log('  mejilla lado %+d: sin tiras a esa altura, no se agrega' % side); continue
            P = np.zeros((NY, NZ, 3)); UV = np.zeros((NY, NZ, 2)); Wc_ = np.zeros((NY, NZ, len(names)))
            for j, (z, (xo, yt, yb)) in enumerate(zip(zs, cols)):
                for i in range(NY):
                    t_ = i / (NY - 1)
                    bulge = 0.8 * MM * np.sin(np.pi * t_)            # la mejilla se abomba un poco hacia afuera al medio
                    P[i, j] = (side * (xo + bulge), yt + (yb - yt) * t_, z)
                    UV[i, j] = (u0 + (u1 - u0) * (0.15 + 0.7 * j / (NZ - 1)), v0 + (v1 - v0) * (0.10 + 0.35 * t_))
                    Wc_[i, j, names.index('sk_head')] = 1 - t_; Wc_[i, j, names.index('skf_jaw')] = t_
            Fq = []
            for i in range(NY - 1):
                for j in range(NZ - 1):
                    a_, b_, c_, d_ = i * NZ + j, i * NZ + j + 1, (i + 1) * NZ + j, (i + 1) * NZ + j + 1
                    Fq += [(a_, b_, c_), (b_, d_, c_)]
            Fq = np.array(Fq); Pf = P.reshape(-1, 3)
            # la cara visible mira hacia adentro (-side en x); con la convencion de Fox (normal = -cruz) el cruz va hacia +side
            cr = np.cross(Pf[Fq[:, 1]] - Pf[Fq[:, 0]], Pf[Fq[:, 2]] - Pf[Fq[:, 0]])
            if (cr[:, 0] * side).mean() < 0: Fq = Fq[:, [0, 2, 1]]
            parts.append((Pf, UV.reshape(-1, 2), Fq, Wc_.reshape(-1, len(names)), 'mejilla')); nadd += len(Fq)
        log('  mejillas: %d triangulos (paredes a los costados, de la cabeza a la mandibula)' % nadd)
    # ---- (02/11) fondo de la garganta: velo del paladar, pilares, pared de la faringe y uvula (Agustin: 'mejorar el fondo de la
    # garganta, realista, unos pocos vertices mas no hay drama'). La textura la pinta make_oral.py en throat.UV_BOX / UVULA_BOX
    if THROAT:
        Pt, UVt, Ft, wjt = throat.geometry()
        Wt = np.zeros((len(Pt), len(names))); Wt[:, names.index('skf_jaw')] = wjt; Wt[:, names.index('sk_head')] = 1 - wjt
        parts.append((Pt, UVt, Ft, Wt, 'garganta'))
        log('  garganta: %d vertices, %d triangulos (pared vieja: %d caras sacadas)' % (len(Pt), len(Ft), bw.sum()))
    # ---- juntar
    Pa, UVa, Fa, Wa, tag = [], [], [], [], []
    base = 0
    mol_ranges = []
    for Pp, UVp, Fp, Wp, t in parts:
        if any(Pp is q for q in mol_parts): mol_ranges.append((base, base + len(Pp)))
        Pa.append(Pp); UVa.append(UVp); Fa.append(Fp + base); Wa.append(Wp); tag += [t] * len(Pp); base += len(Pp)
    Pa = np.vstack(Pa).astype(np.float32); UVa = np.vstack(UVa).astype(np.float32); Fa = np.vstack(Fa); Wa = np.vstack(Wa)
    # (01/11) caras en el orden de Konami (horario desde afuera): la normal es la opuesta del producto cruz, y el signo de la
    # binormal que guarda Konami es el opuesto del que da tangents() (medido sobre la boca de Konami: N 0.95, T 0.98, w -1 -> +1)
    Na = -vnormals(Pa, Fa)
    # (02/11 g) muelas: los vertices repetidos (costura de la uv del lado de la lengua, borde pared / mesa oclusal) comparten la normal
    for a0, a1 in mol_ranges:
        key = np.round(Pa[a0:a1].astype(np.float64) / 1e-7).astype(np.int64)
        _, inv = np.unique(key, axis=0, return_inverse=True); inv = inv.ravel()
        acc = np.zeros((inv.max() + 1, 3)); np.add.at(acc, inv, Na[a0:a1])
        acc /= np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-9)
        Na[a0:a1] = acc[inv]
    # (02/11 b) el marco de la garganta no comparte vertices con las mejillas / paladar de Konami: en su borde la normal se
    # promedia con la del vertice vecino de Konami mas cercano (< 5 mm) para que no haya escalon de sombreado en la union
    tagA = np.array(tag)
    if THROAT and (tagA == 'garganta').any():
        gi = np.where(tagA == 'garganta')[0][:throat.NR * throat.NC].reshape(throat.NR, throat.NC)
        border = np.unique(np.concatenate([gi[:, 0], gi[:, -1], gi[-1, :]]))
        ri = np.where(tagA == 'resto')[0]
        from scipy.spatial import cKDTree
        dist, j = cKDTree(Pa[ri]).query(Pa[border])
        ok = dist < 5 * MM
        nn = Na[border[ok]] + Na[ri[j[ok]]]
        Na[border[ok]] = nn / np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-9)
    if PROF.get('back_in'):   # (09/11) Agustin: 'al expresarse las muelas sobresalen mucho': premolares y muelas hacia adentro
        dm = np.where(tagA == 'diente')[0]; ax = np.abs(Pa[dm, 0])
        Pa[dm, 0] *= 1 - PROF['back_in'] * sst(0.015, 0.026, ax)
        log('  perfil: dientes de atras hacia adentro hasta %.2f mm' % (PROF['back_in'] * float(ax.max()) * 1000))
    Ta, tw = tangents(Pa, Na, UVa, Fa); tw = -tw
    # 4 huesos por vertice
    order = np.argsort(-Wa, 1)[:, :4]
    w4 = np.take_along_axis(Wa, order, 1); w4 /= np.maximum(w4.sum(1, keepdims=True), 1e-9)
    bidx = np.array([[grp.index(b) for b in row] for row in order])
    log('  dientes 3D: %d; vertices %d, triangulos %d' % (nteeth, len(Pa), len(Fa)))
    # (01/11 h) el juego dibuja la boca con a lo sumo 65535 indices: con la v8/v9 (29176 tri = 87528 indices) no se dibujaba
    # lo que quedaba al final del buffer (lengua, paladar, garganta). Tope: 21845 triangulos
    assert len(Fa) * 3 <= 65535, 'demasiados triangulos para la boca: %d (max 21845)' % len(Fa)
    return dict(pos=Pa, nrm=np.hstack([Na, np.zeros((len(Pa), 1))]), tan=np.hstack([Ta, tw[:, None]]), w=w4, bidx=bidx,
                uv=UVa, faces=Fa, tag=np.array(tag))

def main():
    src, texdir, dst = sys.argv[1:4]
    r = build(src, texdir)
    mouth_fmdl.write(src, dst, r['pos'], r['nrm'], r['tan'], r['w'], r['bidx'], r['uv'], r['faces'])
    import hashlib
    print('  SHA256 %s' % hashlib.sha256(open(dst, 'rb').read()).hexdigest()[:8].upper())
    if len(sys.argv) > 4: np.savez_compressed(sys.argv[4], **r)

if __name__ == '__main__':
    main()
