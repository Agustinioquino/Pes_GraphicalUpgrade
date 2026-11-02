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
import fmdl, mouth_fmdl, make_oral, throat

MM = 0.001
# (01/11) mordida: con la boca en reposo la tira de arriba termina en y 1.631 y la de abajo en 1.6285 (2.5 mm de luz); los dientes
# se alargan para que cierren: los de arriba bajan 2.4 mm por delante de los de abajo, que suben 0.8 mm (se solapan ~0.7 mm)
EXT_UP, EXT_LO = float(os.environ.get('EXT_UP', '1.7')) * 0.001, 0.7 * 0.001   # (01/11 f) 2.0 -> 1.5 mm: menos sobremordida (Agustin)   # (01/11 b) 2.4 / 0.8 -> 2.0 / 0.7: dientes un poco menos largos (Agustin)
# (01/11 b) mas encia (Agustin): la linea de la encia baja GUM del alto del diente pintado; esa franja de la tira muestra encia
GUM = 0.08   # = make_oral.GUM (01/11 h: 0.18 -> 0.08, dientes mas largos)
OVERJET_LO = 0.9 * MM   # (01/11 e) los incisivos de abajo se meten 0.9 mm detras de los de arriba
PDEG = int(os.environ.get('PDEG', '2'))   # (01/11 f) parche por diente: grado 2 a lo ancho (sigue el arco), recto a lo alto
CUT_STRIP = float(os.environ.get('CUT_STRIP', '0'))   # (01/11 i) la tira se corta a s > GUM + esto (0 = no cortar; 02/11: apagado, ver STRIP_GUM)
STRIP_GUM = float(os.environ.get('STRIP_GUM', '0.3'))   # (02/11) la tira debajo de la encia (s > GUM) pasa a mostrar encia: s' = GUM - esto * (s - GUM)
THROAT = int(os.environ.get('THROAT', '1'))   # (02/11) fondo de la garganta nuevo (throat.py) en lugar de la pared plana de Konami
SINK = 1.5 * MM   # (01/11 d) 0.7 -> 1.5 mm: el cuello se mete bien detras de la encia (no asoma por arriba)
PATCH = int(os.environ.get('PATCH', '1'))
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
def tooth(M, uL, uR, vG, vI, kind, upper, na=22, nb=14, ext=0.0, mes=1.0, seed=0):   # (02/11) nb 15 -> 14: lugar para la garganta (tope 21845 tri)
    """M: UVMap de la tira. uL/uR: limites en u; vG: v del cuello (encia); vI: v del borde incisal. kind: 'I' incisivo,
    'C' canino, 'P' premolar/molar. mes: lado de cx (+1/-1) que mira al centro de la boca (mesial). Devuelve P, UV, F
    (01/11 e) mas natural (Agustin 'poco naturales'): perfil recto (el parche sigue la tira a lo ancho pero es recto a lo alto:
    antes copiaba la 'S' de la tira subdividida), esquina mesial casi recta y distal redondeada, canino con la punta corrida a
    mesial, cara de adelante convexa con el maximo en el tercio del cuello y dos surcos suaves en los incisivos, y una
    irregularidad chica por diente (largo, profundidad y giro) con semilla fija."""
    rng = np.random.default_rng(1000 + seed)
    jit_len, jit_dep, jit_rot = rng.uniform(-0.18, 0.18) * MM, rng.uniform(-0.12, 0.12) * MM, rng.uniform(-1.0, 1.0)
    if PATCH:
        h = vI - vG
        M = local_patch(M, uL - 0.1 * (uR - uL), uR + 0.1 * (uR - uL), vG - 0.3 * h, vI, deg=PDEG, degv=1)   # (01/11 f) todos rectos a lo alto; a lo ancho PDEG
    gap = (0.012 if kind == 'IC' else 0.035) * (uR - uL)   # (01/11 d) los centrales se tocan
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
    for bi in b:
        # el borde incisal: canino en punta, incisivos con esquinas apenas redondeadas
        if kind == 'C':
            bmax = 1 - 0.30 * np.abs(cx - 0.18 * mes) ** 1.35
        else:
            r_m, r_d = {'IC': (0.03, 0.11), 'I': (0.07, 0.24), 'P': (0.14, 0.14)}[kind]
            rr = np.where(cx * mes > 0, r_m, r_d)
            bmax = 1 - rr * sst(0.5, 1.0, np.abs(cx)) ** 1.6
        bb = bi * bmax if bi > 0 else bi * np.ones_like(cx)
        v = vG + (vI - vG) * bb
        uu = uc + 0.5 * (uR2 - uL2) * cx
        # cuello mas angosto
        narrow = (0.88 + 0.12 * sst(-0.1, 0.45, bb)) if kind == 'IC' else (0.80 + 0.20 * sst(-0.1, 0.45, bb))
        uu = uc + (uu - uc) * narrow
        Pf, Nf = M(np.stack([uu, v], 1))
        Pf = Pf + dI[None, :] * ((ext + jit_len) * sst(0.35, 1.0, bb))[:, None]
        # (01/11 e) resalte (overjet): los incisivos de abajo quedaban 0.4 mm metidos dentro de la cara de atras de los de
        # arriba (franja clara cruzando los centrales). Arriba el borde sale 0.3 mm hacia afuera, abajo entra 0.9 mm
        shift = (0.3 * MM * sst(0.4, 1.0, bb)) if upper else (-OVERJET_LO * sst(0.3, 1.0, bb))
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
        bs = 0.12 + (0.30 / (1 - GUM)) * np.exp(-(duT / 0.14) ** 2)
        lift = 0.3 * MM * sst(bs - 0.05, bs + 0.10, bb)
        # (01/11 e) convexidad: maximo en el tercio del cuello (altura de contorno) y se aplana hacia el borde; surcos de
        # desarrollo en los incisivos (entre los 3 lobulos) en la mitad incisal; giro chico del diente (jit_rot) = un lado
        # mas adelante que el otro; profundidad chica por diente (jit_dep)
        prof = sst(0.0, 0.22, bb) * (1 - 0.55 * sst(0.3, 1.0, bb))
        convex = 0.62 * MM * np.clip(1 - cx ** 2, 0, 1) ** 0.8 * prof
        dev = (0.08 * MM * np.exp(-((np.abs(cx) - 0.38) / 0.12) ** 2) * sst(0.35, 0.85, bb)) if kind in ('IC', 'I') else 0 * cx
        rot = 0.25 * MM * jit_rot * cx * sst(0.0, 0.3, bb)
        bulge = convex - dev + rot + jit_dep * sst(0.0, 0.3, bb) + lift - SINK * sst(bs, bs - 0.27, bb)
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
        ut = uc + (uu - uc) * UV_W
        vt = vG + (vI - vG) * (UV_V0 + (UV_V1 - UV_V0) * np.clip(bb, 0, 1))
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
    nteeth = 0
    for c, r, up in ((c_up, rows[0], True), (c_lo, rows[1], False)):
        M = strip_maps[c]
        pk = np.concatenate([[r['x0']], r['pk'], [r['x1']]]).astype(float)
        ntt = len(pk) - 1; mid = ntt / 2.0
        for i in range(ntt):
            xl, xr = pk[i], pk[i + 1]
            if xr - xl < 8: continue
            xc = 0.5 * (xl + xr)
            gi = np.interp(xc, np.arange(512), r['g']); ii = np.interp(xc, np.arange(512), r['inc'])
            # posicion desde el centro: 0,1 incisivos, 2 canino, el resto premolares/molares
            rank = int(abs((i + 0.5) - mid))
            kind = 'IC' if (rank == 0 and up) else ('I' if rank < 2 else ('C' if rank == 2 else 'P'))
            vG, vI = (gi + GUM * (ii - gi)) / 512.0, ii / 512.0
            if up: vI = min(vI, 0.302)         # el frente de la tira de arriba termina en v 0.30 (despues dobla hacia atras)
            else: vI = max(vI, 0.505)
            # (01/11 e) curva de la sonrisa: centrales un poco mas largos, laterales mas cortos, caninos con la punta, muelas
            # mas cortas; mesial = hacia el centro de la fila
            dext = ({'IC': -0.1, 'I': -0.5, 'C': -0.2, 'P': -0.6} if up else {'I': 0.0, 'C': 0.25, 'P': -0.3})[kind] * MM
            mes = 1.0 if (i + 0.5) < mid else -1.0
            Pt, UVt, Ft = tooth(M, xl / 512.0, xr / 512.0, vG, vI, kind, up, ext=(EXT_UP if up else EXT_LO) + dext, mes=mes,
                                seed=nteeth)
            wt = np.zeros((len(Pt), len(names))); wt[:, names.index('sk_head' if up else 'skf_jaw')] = 1
            parts.append((Pt, UVt, Ft, wt, 'diente')); nteeth += 1
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
    for Pp, UVp, Fp, Wp, t in parts:
        Pa.append(Pp); UVa.append(UVp); Fa.append(Fp + base); Wa.append(Wp); tag += [t] * len(Pp); base += len(Pp)
    Pa = np.vstack(Pa).astype(np.float32); UVa = np.vstack(UVa).astype(np.float32); Fa = np.vstack(Fa); Wa = np.vstack(Wa)
    # (01/11) caras en el orden de Konami (horario desde afuera): la normal es la opuesta del producto cruz, y el signo de la
    # binormal que guarda Konami es el opuesto del que da tangents() (medido sobre la boca de Konami: N 0.95, T 0.98, w -1 -> +1)
    Na = -vnormals(Pa, Fa)
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
