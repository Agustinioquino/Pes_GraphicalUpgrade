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
import fmdl, mouth_fmdl, make_oral

MM = 0.001
# (01/11) mordida: con la boca en reposo la tira de arriba termina en y 1.631 y la de abajo en 1.6285 (2.5 mm de luz); los dientes
# se alargan para que cierren: los de arriba bajan 2.4 mm por delante de los de abajo, que suben 0.8 mm (se solapan ~0.7 mm)
EXT_UP, EXT_LO = 2.0 * 0.001, 0.7 * 0.001   # (01/11 b) 2.4 / 0.8 -> 2.0 / 0.7: dientes un poco menos largos (Agustin)
# (01/11 b) mas encia (Agustin): la linea de la encia baja GUM del alto del diente pintado; esa franja de la tira muestra encia
GUM = 0.18   # = make_oral.GUM

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

# ---------------------------------------------------------------- un diente
def tooth(M, uL, uR, vG, vI, kind, upper, na=22, nb=13, ext=0.0):
    """M: UVMap de la tira. uL/uR: limites en u; vG: v del cuello (encia); vI: v del borde incisal. kind: 'I' incisivo,
    'C' canino, 'P' premolar/molar. Devuelve P, UV, F"""
    gap = 0.035 * (uR - uL)
    uL2, uR2 = uL + gap, uR - gap
    uc = 0.5 * (uL2 + uR2)
    b = np.linspace(-0.22, 1.0, nb)
    th = np.linspace(0, 2 * np.pi, na + 1)[:-1]   # 0 = adelante
    pw = {'IC': 4.2, 'I': 2.8, 'C': 2.8, 'P': 2.3}[kind]   # (01/11 b) centrales mas cuadrados
    cx = np.sign(np.sin(th)) * np.abs(np.sin(th)) ** (2 / pw)   # a lo ancho (-1..1)
    cy = np.sign(np.cos(th)) * np.abs(np.cos(th)) ** (2 / pw)   # adelante (+1) / atras (-1)
    Ps = []; UVs = []
    # (01/11) mordida (Agustin): el diente se alarga 'ext' metros mas alla del borde de la tira, en la direccion cuello -> borde
    Pg, _ = M(np.array([[uc, vG]])); Pi, _ = M(np.array([[uc, vI]]))
    dI = (Pi - Pg)[0]; dI /= max(np.linalg.norm(dI), 1e-9)
    for bi in b:
        # el borde incisal: canino en punta, incisivos con esquinas apenas redondeadas
        bmax = 1 - (0.30 * np.abs(cx) ** 1.3 if kind == 'C' else (0.03 * np.abs(cx) ** 6 if kind == 'IC' else 0.10 * np.abs(cx) ** 4))
        bb = bi * bmax if bi > 0 else bi * np.ones_like(cx)
        v = vG + (vI - vG) * bb
        uu = uc + 0.5 * (uR2 - uL2) * cx
        # cuello mas angosto
        narrow = (0.88 + 0.12 * sst(-0.1, 0.45, bb)) if kind == 'IC' else (0.80 + 0.20 * sst(-0.1, 0.45, bb))
        uu = uc + (uu - uc) * narrow
        Pf, Nf = M(np.stack([uu, v], 1))
        Pf = Pf + dI[None, :] * (ext * sst(0.35, 1.0, bb))[:, None]
        # espesor (m): grueso en el cuello, fino en el borde
        T0 = {'IC': 6.5, 'I': 6.5, 'C': 7.5, 'P': 8.5}[kind] * MM; T1 = {'IC': 1.0, 'I': 0.9, 'C': 1.6, 'P': 5.0}[kind] * MM
        T = T0 + (T1 - T0) * sst(0.0, 1.0, bb) ** 0.8
        # adelante: sobre la tira + abultado del esmalte; el cuello se hunde detras de la encia
        bulge = 0.45 * MM * (1 - cx ** 2) * sst(0.0, 0.25, bb) * (1 - 0.6 * sst(0.75, 1.0, bb)) - 0.7 * MM * sst(0.08, -0.18, bb)
        front = Pf + Nf * bulge[:, None]
        back = Pf - Nf * np.reshape(T, (-1, 1))
        y = (cy + 1) / 2   # 1 adelante, 0 atras
        # cara de atras un poco mas angosta (forma de pala)
        P = front * y[:, None] + back * (1 - y[:, None])
        # en el borde incisal la seccion se cierra (el diente termina en filo)
        Ps.append(P)
        # uv: adelante la del diente pintado; atras la misma (espejada al ancho)
        UVs.append(np.stack([uu, v * np.ones_like(uu)], 1))
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
    def sub(c, times, smooth=1.0):
        sel = np.where(lab == c)[0]; remap = -np.ones(len(P), np.int64); remap[sel] = np.arange(len(sel))
        Fc = remap[F[(lab[F[:, 0]] == c)]]
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
            Pt, UVt, Ft = tooth(M, xl / 512.0, xr / 512.0, vG, vI, kind, up, ext=(EXT_UP if up else EXT_LO))
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
    for c in np.unique(lab):
        if c in (c_up, c_lo, c_tg): continue
        Pc, UVc, Fc, Wc = sub(c, 1)
        parts.append((Pc, UVc, Fc, Wc, 'resto'))
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
