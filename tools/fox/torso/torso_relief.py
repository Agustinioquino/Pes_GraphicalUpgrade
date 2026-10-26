# PES21 FX (26/10 b) - relieve ESCULPIDO de las variantes a partir del normal map de TorsoBody: la altura que dibuja el normal
# map (las separaciones de los musculos donde las puso el artista: abdominales, pectoral, serratos, costillas) se integra en la
# malla soldada del torso (minimos cuadrados por arista: h_j - h_i = -s . (P_j - P_i), con s = pendiente del normal map en 3D,
# desenfocada a la escala de la malla) y se lleva a la GEOMETRIA: el atletico la tiene varias veces mas honda (musculos con
# volumen y surcos con profundidad de verdad, alineados con las lineas de la textura) y el robusto la pierde (la grasa alisa).
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))

def grid_blur3d_query(P, V, sigma, Q, cell=0.0025):
    """desenfoque gaussiano 3D del campo V (n, k) en los puntos P, leido en los puntos Q (m, 3)"""
    from scipy.ndimage import gaussian_filter
    lo = np.minimum(P.min(0), Q.min(0)) - 4 * sigma; hi = np.maximum(P.max(0), Q.max(0)) + 4 * sigma
    dims = np.ceil((hi - lo) / cell).astype(int) + 2
    k = V.shape[1]; ncell = int(np.prod(dims))
    def splat(X, vals):
        g = (X - lo) / cell; i0 = np.floor(g).astype(int); f = g - i0
        acc = [np.zeros(ncell) for _ in range(len(vals))]
        for dx in (0, 1):
            for dy in (0, 1):
                for dz in (0, 1):
                    w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                    fi = ((i0[:, 0] + dx) * dims[1] + (i0[:, 1] + dy)) * dims[2] + (i0[:, 2] + dz)
                    for c, v in enumerate(vals): acc[c] += np.bincount(fi, w * v, ncell)
        return acc
    acc = splat(P, [V[:, c] for c in range(k)] + [np.ones(len(P))])
    s = sigma / cell
    grids = [gaussian_filter(a.reshape(dims).astype(np.float32), s, mode='constant') for a in acc]
    g = (Q - lo) / cell; i0 = np.floor(g).astype(int); f = g - i0
    out = np.zeros((len(Q), k)); wt = np.zeros(len(Q))
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                ix, iy, iz = i0[:, 0] + dx, i0[:, 1] + dy, i0[:, 2] + dz
                for c in range(k): out[:, c] += w * grids[c][ix, iy, iz]
                wt += w * grids[k][ix, iy, iz]
    return out / np.maximum(wt, 1e-9)[:, None]

def texture_height(T, G, base_xy, sigma=0.005):
    """altura (m) por vertice soldado de T que dibuja el normal map base_xy (H, W, 2) del torso (G = uvbake del torso)"""
    import scipy.sparse as sp
    from scipy.sparse.linalg import spsolve
    ii = np.where(G["mask"])
    P = G["P"][ii].astype(np.float64); Tg = G["Tg"][ii].astype(np.float64); Bn = G["Bn"][ii].astype(np.float64)
    s3 = base_xy[ii][:, :1] * Tg + base_xy[ii][:, 1:2] * Bn          # = -grad h (tangente, 3D)
    Pw = T["Pw"]; E = T["edges"]
    sv = grid_blur3d_query(P, s3, sigma, Pw)
    d = -0.5 * np.sum((sv[E[:, 0]] + sv[E[:, 1]]) * (Pw[E[:, 1]] - Pw[E[:, 0]]), 1)
    m, n = len(E), len(Pw)
    A = sp.csr_matrix((np.concatenate([-np.ones(m), np.ones(m)]), (np.concatenate([np.arange(m), np.arange(m)]), np.concatenate([E[:, 0], E[:, 1]]))), shape=(m, n))
    M = (A.T @ A + 1e-6 * sp.eye(n)).tocsc()
    h = spsolve(M, A.T @ d)
    return h - h.mean()


# factores de la altura del normal map llevada a la geometria (k_g) y del dibujo que queda en la textura, por zona (adelante /
# espalda): (a = lineas, b = volumen visible, k_g = cuanto del volumen va a la malla). El mapa de cada variante queda en
# micro + a * lineas + (b - k_g * costura) * volumen: lo que la malla ya muestra se saca de la textura (si no, el volumen se
# veria dos veces). Normal: adelante todo el volumen pasa a la malla; espalda suave. Atletico: la malla 2.2 veces mas honda.
# Robusto: casi lisa (la grasa).
REGION = {"normal": ((1.00, 1.00, 1.00), (0.30, 1.10, 0.60)),
          "atletico": ((1.35, 1.60, 2.00), (0.60, 1.35, 0.60)),
          "robusto": ((0.50, 0.45, 0.15), (0.08, 0.70, 0.15))}

def region_factors(name, bm, P=None):
    """(a, b, k_g) por punto segun la mascara de espalda bm (0 adelante .. 1 atras). Con P, el atletico vuelve al k_g de la base
    en los costados (serratos / oblicuos: si no, la silueta queda con bultos), arriba de los hombros y el cuello, y en el
    ombligo (si no, un agujero)"""
    (af, bf, kf), (ab, bb, kb) = REGION[name]
    a, b, k = af + (ab - af) * bm, bf + (bb - bf) * bm, kf + (kb - kf) * bm
    if P is not None and name == "atletico":
        from torso_maps import _frame
        from torso_variants import ss, g, NAVEL
        x, y, z, rz, front, back, side = _frame(P)
        (_, _, kf0), (_, _, kb0) = REGION["normal"]
        k0 = kf0 + (kb0 - kf0) * bm
        keep = (1 - 0.6 * side) * (1 - ss(1.43, 1.49, y)) * (1 - g(np.hypot(x, y - NAVEL[1]) / 0.020))
        k = k0 + (k - k0) * keep
    return a, b, k

def relief_hp(T, texdir, hp_sigma=0.04, fname="torsob_nrm.ftex", W=2048, H=1024, vmax=0.5):
    """altura del normal map de TorsoBody por vertice soldado de T (sin lo de mas de ~4 cm), m. Torso: torsob_nrm, v 0..0.5;
    brazos: arm_nrm entero (W = H = 2048, vmax = 1)"""
    from uvbake import bake
    from texio import load_ftex
    from torso_mesh import gauss_matrix
    G = bake(T, None, W=W, H=H, vmax=vmax)
    nm = load_ftex(os.path.join(texdir, fname), mip=1)
    base = nm[:H, :W, :2].astype(np.float64) * 2 - 1
    h = texture_height(T, G, base)
    hs = gauss_matrix(T["Pw"], T["Fw"], 0.006) @ h          # sin los bultitos de la piel (celdas de menos de ~1.2 cm)
    return hs - gauss_matrix(T["Pw"], T["Fw"], hp_sigma) @ h
