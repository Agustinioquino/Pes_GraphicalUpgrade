# PES21 FX (26/10 h) - EL CUELLO DEL TORSO CON LA FORMA DEL DE KONAMI.
# Medido (NOTAS 26/10 g): el cuerpo original del juego (shirtless_body.fmdl) termina el cuello en otra linea que TorsoBody:
# adelante 3 cm mas arriba (sube por el cuello) y 15 mm mas angosto, atras ~9 mm mas abajo; y su pecho alto es ~9 mm mas lleno
# junto al cuello. El cuello de las caras reales esta hecho para llegar EXACTO a ese borde y quedar ADENTRO del cuerpo por
# debajo: asi Konami no corta nada y la union no se ve. TorsoBody baja el escote y afina el pecho alto -> la cara queda afuera.
# Aca las mallas 1 y 2 del torso (iguales en los 4 pants_*_sub) toman esa forma cerca del cuello:
#   1. el anillo del cuello de TorsoBody (48 vertices) va al borde del cuello de Konami en el mismo angulo alrededor del eje
#   2. desplazamiento armonico (Laplace en la malla) de ahi hasta 11 cm (geodesica) del cuello; la costura del hombro no se mueve
#   3. hasta 4 cm del cuello la superficie ES la de Konami (punto mas cercano), y se pasa a la de TorsoBody hasta 10 cm
#   4. normales / tangentes giradas con la malla (rotacion minima de la normal de la malla vieja a la nueva)
# El fmdl de Konami es del usuario (se lee en su PC); los fmdl que salen tienen modelos de TorsoBody / Konami: no van al repo.
# Uso: python3 neck_konami.py <common de TorsoBody> <shirtless_body.fmdl> <carpeta de salida> [vista previa.png]
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'sim'), os.path.join(HERE, '..', 'pelvis')]
from fmdl import Fmdl, meshes
from torso_mesh import load_torso, geodesic_from, vertex_normals, rotate_min, TORSO_FILES

AXZ = 0.045
D_HARM = 0.11      # hasta donde llega el desplazamiento del anillo (geodesica desde el cuello)
D_FULL, D_FADE = 0.04, 0.10   # superficie de Konami entera / hasta donde se mezcla

def ss(a, b, x):
    t = np.clip((np.asarray(x, float) - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)

def weld(P, F):
    q = np.round(P / 1e-4).astype(np.int64)
    _, uid, inv = np.unique(q, axis=0, return_index=True, return_inverse=True); inv = inv.ravel()
    Fw = inv[F]; Fw = Fw[(Fw[:, 0] != Fw[:, 1]) & (Fw[:, 1] != Fw[:, 2]) & (Fw[:, 2] != Fw[:, 0])]
    E = np.sort(np.concatenate([Fw[:, [0, 1]], Fw[:, [1, 2]], Fw[:, [2, 0]]]), 1)
    k, c = np.unique(E, axis=0, return_counts=True)
    return P[uid], Fw, np.unique(k[c == 1])

def load_konami(path, with_normals=False):
    M = meshes(Fmdl(path))
    P = np.concatenate([m["pos"] for m in M]).astype(np.float64); F = []; o = 0
    N = np.concatenate([m["nrm"][:, :3] for m in M]).astype(np.float64)
    for m in M: F.append(m["faces"] + o); o += m["nvert"]
    if not with_normals: return weld(P, np.concatenate(F))
    q = np.round(P / 1e-4).astype(np.int64)
    _, uid, inv = np.unique(q, axis=0, return_index=True, return_inverse=True); inv = inv.ravel()
    Nw = np.zeros((len(uid), 3)); np.add.at(Nw, inv, N); Nw /= np.maximum(np.linalg.norm(Nw, axis=1, keepdims=True), 1e-12)
    Pw, Fw, B = weld(P, np.concatenate(F))
    return Pw, Fw, B, Nw

def phi_of(P): return np.degrees(np.arctan2(P[:, 0], P[:, 2] - AXZ))

class Border:
    """borde del cuello (polilinea cerrada ordenada por angulo): punto en el angulo phi = interseccion con el plano vertical"""
    def __init__(self, B):
        ph = phi_of(B); o = np.argsort(ph); self.B = B[o]; self.ph = ph[o]
    def at(self, phi):
        B, ph = self.B, self.ph; n = len(B)
        i = n - 1 if (phi < ph[0] or phi >= ph[-1]) else int(np.searchsorted(ph, phi, side="right") - 1)
        a, b = B[i], B[(i + 1) % n]
        r = np.radians(phi); c, s = np.cos(r), np.sin(r)
        sa = a[0] * c - (a[2] - AXZ) * s; sb = b[0] * c - (b[2] - AXZ) * s
        t = sa / (sa - sb) if abs(sa - sb) > 1e-15 else 0.0
        return a + np.clip(t, 0, 1) * (b - a)

def closest_on_mesh(P, Q, F, k=24):
    """punto mas cercano de cada P sobre la malla (Q, F) (exacto contra los k triangulos de centro mas cercano)"""
    from scipy.spatial import cKDTree
    A, B, C = Q[F[:, 0]], Q[F[:, 1]], Q[F[:, 2]]
    tree = cKDTree((A + B + C) / 3)
    _, cand = tree.query(P, k=k)
    best = np.full(len(P), np.inf); out = P.copy(); tri = np.zeros(len(P), int)
    for j in range(k):
        t = cand[:, j]
        q = _closest_tri(P, A[t], B[t], C[t])
        d = np.sum((q - P) ** 2, 1)
        m = d < best; best[m] = d[m]; out[m] = q[m]; tri[m] = t[m]
    closest_on_mesh.tri = tri
    return out, np.sqrt(best)

def bary(q, a, b, c):
    v0 = b - a; v1 = c - a; v2 = q - a
    d00 = np.sum(v0 * v0, 1); d01 = np.sum(v0 * v1, 1); d11 = np.sum(v1 * v1, 1); d20 = np.sum(v2 * v0, 1); d21 = np.sum(v2 * v1, 1)
    den = np.maximum(d00 * d11 - d01 * d01, 1e-30)
    v = (d11 * d20 - d01 * d21) / den; w = (d00 * d21 - d01 * d20) / den
    return np.stack([1 - v - w, v, w], 1)

def _closest_tri(p, a, b, c):
    # Ericson, Real-Time Collision Detection 5.1.5 (vectorizado)
    ab = b - a; ac = c - a; ap = p - a
    d1 = np.sum(ab * ap, 1); d2 = np.sum(ac * ap, 1)
    bp = p - b; d3 = np.sum(ab * bp, 1); d4 = np.sum(ac * bp, 1)
    cp = p - c; d5 = np.sum(ab * cp, 1); d6 = np.sum(ac * cp, 1)
    va = d3 * d6 - d5 * d4; vb = d5 * d2 - d1 * d6; vc = d1 * d4 - d3 * d2
    den = np.where(np.abs(va + vb + vc) < 1e-30, 1e-30, va + vb + vc)
    v = vb / den; w = vc / den
    res = a + ab * v[:, None] + ac * w[:, None]
    m = (va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0)
    t = np.clip((d4 - d3) / np.maximum((d4 - d3) + (d5 - d6), 1e-30), 0, 1); res[m] = (b + (c - b) * t[:, None])[m]
    m = (vb <= 0) & (d2 >= 0) & (d6 <= 0)
    t = np.clip(d2 / np.maximum(d2 - d6, 1e-30), 0, 1); res[m] = (a + ac * t[:, None])[m]
    m = (vc <= 0) & (d1 >= 0) & (d3 <= 0)
    t = np.clip(d1 / np.maximum(d1 - d3, 1e-30), 0, 1); res[m] = (a + ab * t[:, None])[m]
    m = (d6 >= 0) & (d5 <= d6); res[m] = c[m]
    m = (d3 >= 0) & (d4 <= d3); res[m] = b[m]
    m = (d1 <= 0) & (d2 <= 0); res[m] = a[m]
    return res

def harmonic(n, edges, fixed_idx, fixed_val, free):
    """resuelve Laplace (pesos uniformes) para los vertices 'free' con los valores fijos dados"""
    import scipy.sparse as sp
    import scipy.sparse.linalg as spl
    deg = np.bincount(edges.ravel(), minlength=n).astype(float)
    Wm = sp.coo_matrix((np.ones(2 * len(edges)), (np.r_[edges[:, 0], edges[:, 1]], np.r_[edges[:, 1], edges[:, 0]])), shape=(n, n)).tocsr()
    L = sp.diags(deg) - Wm
    val = np.zeros((n, 3)); val[fixed_idx] = fixed_val
    fi = np.where(free)[0]
    Lff = L[fi][:, fi]; rhs = -(L[fi] @ val)
    out = val.copy()
    lu = spl.splu(Lff.tocsc())
    for c in range(3): out[fi, c] = lu.solve(rhs[:, c])
    return out

def build(common, konami_path, log=print):
    import torso_seam
    from torso_variants import build_arm
    T = load_torso(common, "out")
    Pw = T["Pw"]; n = len(Pw)
    # anillos
    B = T["boundary"]; ring = B[Pw[B, 1] > 1.45]
    A = build_arm(common, "arm")
    from scipy.spatial import cKDTree
    dA, _ = cKDTree(A["Pw"]).query(Pw); shoulder = np.where(dA < 1e-4)[0]
    QK, FK, BK, NK = load_konami(konami_path, with_normals=True)
    KB = Border(QK[BK[QK[BK, 1] > 1.44]])
    log("borde de Konami: %d vertices, y %.3f .. %.3f; anillo de TorsoBody: %d vertices" % (len(KB.B), KB.B[:, 1].min(), KB.B[:, 1].max(), len(ring)))
    tgt = np.array([KB.at(p) for p in phi_of(Pw[ring])])
    dring = tgt - Pw[ring]
    log("el anillo se mueve: dy %.1f .. %.1f mm, horizontal hasta %.1f mm" % (dring[:, 1].min() * 1000, dring[:, 1].max() * 1000, np.hypot(dring[:, 0], dring[:, 2]).max() * 1000))
    # 2. desplazamiento armonico
    g = geodesic_from(Pw, T["edges"], ring)
    gs = geodesic_from(Pw, T["edges"], shoulder)
    free = (g < D_HARM) & (gs > 0.004)
    free[ring] = False
    fixed = np.where(~free)[0]
    fval = np.zeros((len(fixed), 3)); pos_in_fixed = {v: i for i, v in enumerate(fixed)}
    for k_, v in enumerate(ring): fval[pos_in_fixed[v]] = dring[k_]
    disp = harmonic(n, T["edges"], fixed, fval, free)
    P1 = Pw + disp
    # 3. superficie de Konami cerca del cuello
    wk = (1 - ss(D_FULL, D_FADE, g)) * ss(0.01, 0.04, gs)
    sel = np.where(wk > 1e-3)[0]
    q, dist = closest_on_mesh(P1[sel], QK, FK)
    ok = dist < 0.03
    P2 = P1.copy()
    P2[sel[ok]] += wk[sel[ok], None] * (q[ok] - P1[sel[ok]])
    P2[ring] = tgt
    # normales de Konami (las del archivo, pensadas para coincidir con el cuello de las caras) donde la superficie es la suya
    tri = closest_on_mesh.tri; t3 = FK[tri[ok]]
    bc = bary(q[ok], QK[t3[:, 0]], QK[t3[:, 1]], QK[t3[:, 2]])
    nk = np.einsum("ij,ijk->ik", np.clip(bc, 0, 1), NK[t3]); nk /= np.maximum(np.linalg.norm(nk, axis=1, keepdims=True), 1e-12)
    NKv = np.zeros((n, 3)); WKv = np.zeros(n)
    NKv[sel[ok]] = nk; WKv[sel[ok]] = (1 - ss(0.0, 0.015, g[sel[ok]]))   # (27/10) la normal de Konami solo en 1.5 cm junto al cuello (mas abajo el pecho quedaba blanco: sus normales van con su normal map, no con el de TorsoBody)
    # el anillo: la normal del borde de Konami en ese angulo
    kbn_all = np.concatenate([QK[BK], NK[BK]], 1)[QK[BK, 1] > 1.44]
    phk = phi_of(kbn_all[:, :3]); o_ = np.argsort(phk); phk = phk[o_]; nkb = kbn_all[o_, 3:]; m_ = len(phk)
    for k_, v in enumerate(ring):
        a = phi_of(Pw[v:v + 1])[0]
        i0 = int(np.searchsorted(phk, a, side="right") - 1) % m_; i1 = (i0 + 1) % m_
        span = (phk[i1] - phk[i0]) % 360 or 360; f = ((a - phk[i0]) % 360) / span
        nn = nkb[i0] * (1 - f) + nkb[i1] * f; NKv[v] = nn / np.linalg.norm(nn); WKv[v] = 1.0
    log("superficie de Konami: %d vertices (distancia media %.1f mm, max %.1f; %d lejos, sin tocar)" % (ok.sum(), dist[ok].mean() * 1000, dist[ok].max() * 1000, (~ok).sum()))
    # 4. normales / tangentes por malla
    N0 = vertex_normals(Pw, T["Fw"]); N1 = vertex_normals(P2, T["Fw"])
    out = []
    for p in T["parts"]:
        iv = T["inv"][p["off"]: p["off"] + p["nvert"]]
        nrm = rotate_min(N0[iv], N1[iv], p["nrm"])
        w_ = WKv[iv][:, None]                       # hacia la normal de Konami (misma convencion: hacia afuera)
        nn = nrm + w_ * (NKv[iv] - nrm); nn /= np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-12)
        rot_src = nrm.copy(); nrm = nn
        tan = p["tan"].copy()
        tan[:, :3] = rotate_min(rot_src / np.maximum(np.linalg.norm(rot_src, axis=1, keepdims=True), 1e-12), nn, rotate_min(N0[iv], N1[iv], p["tan"][:, :3]))
        out.append(dict(index=p["index"], pos=P2[iv], nrm=nrm, tan=tan, moved=float(np.abs(P2[iv] - Pw[iv]).max())))
    newring = P2[ring]; ringn = NKv[ring]
    return T, out, P2, ring, newring, ringn, (QK, FK)

def write_all(common, parts, outdir, srcdir=None, log=print):
    """las mallas 1 y 2 nuevas en los 4 pants_*_sub (de 'srcdir' si se da: p. ej. las pelvis sin short, que tienen la malla 3 propia)"""
    from pelvis_fmdl import write_mesh_geom
    os.makedirs(outdir, exist_ok=True)
    for ver, fn in TORSO_FILES.items():
        src = os.path.join(srcdir or common, fn + ".fmdl"); dst = os.path.join(outdir, fn + ".fmdl")
        cur = src
        M = meshes(Fmdl(src))
        for p in parts:
            if p["index"] == 3: continue
            m = M[p["index"]]
            assert m["nvert"] == len(p["pos"]) and np.abs(m["pos"] - meshes(Fmdl(os.path.join(common, fn + ".fmdl")))[p["index"]]["pos"]).max() < 1e-6, "%s malla %d distinta" % (fn, p["index"])
            nrm = p["nrm"]; tan = p["tan"]
            if nrm.shape[1] == 3: nrm = np.concatenate([nrm, m["nrm"][:, 3:4] if m["nrm"].shape[1] > 3 else np.zeros((len(nrm), 1))], 1)
            write_mesh_geom(cur, dst, p["index"], p["pos"], nrm, tan); cur = dst
        log("  %s" % dst)

def neck_fmdl(common, oldring_P, newring, outpath, H=0.06, log=print, ringn=None):
    """cuello generico de TorsoBody (neck.fmdl): los 6 cm de abajo siguen al anillo nuevo (cada angulo: el cambio del anillo,
    entero abajo y nada a H)"""
    from pelvis_fmdl import write_mesh_geom
    src = os.path.join(common, "neck.fmdl")
    M = meshes(Fmdl(src))
    old = Border(oldring_P); new = Border(newring)
    cur = src
    for k, m in enumerate(M):
        P = m["pos"].astype(np.float64); ph = phi_of(P)
        dP = np.array([new.at(a) - old.at(a) for a in ph])
        y0 = np.array([old.at(a)[1] for a in ph])
        h = P[:, 1] - y0
        w = 1 - ss(0.0, H, h); w[h < 0] = 1
        r_old = np.hypot(P[:, 0], P[:, 2] - AXZ)
        o_r = np.array([np.hypot(old.at(a)[0], old.at(a)[2] - AXZ) for a in ph]); n_r = np.array([np.hypot(new.at(a)[0], new.at(a)[2] - AXZ) for a in ph])
        rr = r_old + w * (n_r - o_r)
        ang = np.radians(ph)
        P2 = np.stack([rr * np.sin(ang), P[:, 1] + w * dP[:, 1], AXZ + rr * np.cos(ang)], 1)
        F = m["faces"]
        N0 = vertex_normals(P, F); N1 = vertex_normals(P2, F)
        nrm = m["nrm"].astype(np.float64).copy(); nrm[:, :3] = rotate_min(N0, N1, nrm[:, :3])
        if ringn is not None:   # abajo: la normal del borde del torso nuevo (la de Konami) en ese angulo
            phr = phi_of(newring); o_ = np.argsort(phr); phr = phr[o_]; nr = ringn[o_]; m_ = len(phr)
            i0 = (np.searchsorted(phr, ph, side="right") - 1) % m_; i1 = (i0 + 1) % m_
            span = (phr[i1] - phr[i0]) % 360; span[span == 0] = 360; f = (((ph - phr[i0]) % 360) / span)[:, None]
            tn = nr[i0] * (1 - f) + nr[i1] * f; tn /= np.linalg.norm(tn, axis=1, keepdims=True)
            w2 = (1 - ss(0.0, 0.02, h))[:, None]; w2[h < 0] = 1
            n3 = nrm[:, :3] + w2 * (tn - nrm[:, :3]); n3 /= np.linalg.norm(n3, axis=1, keepdims=True)
            old3 = nrm[:, :3].copy(); nrm[:, :3] = n3
        tan = m["tan"].astype(np.float64).copy(); tan[:, :3] = rotate_min(N0, N1, tan[:, :3])
        if ringn is not None: tan[:, :3] = rotate_min(old3 / np.linalg.norm(old3, axis=1, keepdims=True), nrm[:, :3], tan[:, :3])
        write_mesh_geom(cur, outpath, k, P2, nrm, tan); cur = outpath
        log("  neck.fmdl malla %d: %d vertices, se mueven hasta %.1f mm" % (k, len(P), np.abs(P2 - P).max() * 1000))

def preview(T, P2, K, out):
    import sim
    QK, FK = K
    ms_old = [dict(faces=T["Fw"])]; ms_k = [dict(faces=FK)]
    ims, lbs = [], []
    for yaw in (0, 60, 90, 180):
        for nm, ms, v in (("TorsoBody", ms_old, [T["Pw"]]), ("nuevo", ms_old, [P2]), ("Konami", ms_k, [QK])):
            ims.append(sim.render(ms, v, yaw, W=260, H=300, ppm=900, cy=1.42)); lbs.append("%s %d" % (nm, yaw))
    sim.sheet(ims, lbs, out)

if __name__ == "__main__":
    common, kp, outdir = sys.argv[1:4]
    T, parts, P2, ring, newring, ringn, K = build(common, kp)
    write_all(common, parts, outdir)
    np.savez(os.path.join(outdir, "anillo_nuevo.npz"), old=T["Pw"][ring], new=newring, n=ringn)
    if len(sys.argv) > 4: preview(T, P2, K, sys.argv[4])
