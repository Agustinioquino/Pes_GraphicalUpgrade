# PES21 FX (26/10) - correccion del relieve de la ESPALDA de TorsoBody para todos los torsos (pes_var_torso_base_n.dds) y las
# capas que usan las variantes. El normal map de TorsoBody (torsob_nrm) tiene casi todo el relieve en lineas finas (las
# separaciones entre musculos: surcos tallados sobre panzas planas) y en la espalda esas lineas son duras (los dorsales quedaron
# arriba, junto a la axila). Se separa el mapa en bandas, en 3D sobre la superficie (sin costuras de UV):
#   micro = poros y lineas de piel (< ~1 mm, desenfoque en UV de 3 texeles)
#   lineas = la banda media (las separaciones de los musculos, ~1 mm .. ~1.2 cm)
#   volumen = lo grande (desenfoque gaussiano 3D de 1.2 cm de las pendientes: la forma de cada zona)
# y la espalda queda  micro + k_lineas * lineas + k_vol * volumen  (normal: lineas al 35 %, volumen al 120 %). El robusto la
# alisa mas (torso_maps la suma a su delta) y el atletico le pone los musculos de la espalda bien ubicados (height_athletic).
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
from torso_variants import ss, torso_mask
from texio import load_ftex

def back_mask(P):
    """1 en la espalda (atras de los costados), 0 adelante; incluye la parte de atras de los hombros y el trapecio"""
    from torso_maps import _frame
    x, y, z, rz, front, back, side = _frame(P)
    m = ss(-0.15, 0.45, -rz)                     # de los costados hacia atras
    m = np.maximum(m, ss(1.47, 1.52, y) * ss(-0.5, 0.2, -rz))   # arriba de los hombros (trapecio) aunque mire hacia arriba
    return m * ss(0.995, 1.04, y)

def grid_blur3d(P, V, W, sigma, cell=0.003):
    """desenfoque gaussiano 3D de un campo vectorial V (n, k) definido en puntos P (n, 3) con pesos W (n): se reparte en una
    grilla (trilineal), se desenfoca y se vuelve a leer en los mismos puntos. Devuelve (n, k)"""
    from scipy.ndimage import gaussian_filter
    lo = P.min(0) - 4 * sigma; hi = P.max(0) + 4 * sigma
    dims = np.ceil((hi - lo) / cell).astype(int) + 2
    g = (P - lo) / cell
    i0 = np.floor(g).astype(int); f = g - i0
    k = V.shape[1]
    acc = np.zeros((k + 1,) + tuple(dims), np.float32)
    ncell = int(np.prod(dims))
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2]) * W
                fi = ((i0[:, 0] + dx) * dims[1] + (i0[:, 1] + dy)) * dims[2] + (i0[:, 2] + dz)
                for c in range(k): acc[c] += np.bincount(fi, w * V[:, c], ncell).reshape(dims).astype(np.float32)
                acc[k] += np.bincount(fi, w, ncell).reshape(dims).astype(np.float32)
    s = sigma / cell
    for c in range(k + 1): acc[c] = gaussian_filter(acc[c], s, mode='constant')
    out = np.zeros((len(P), k)); wt = np.zeros(len(P))
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                ix, iy, iz = i0[:, 0] + dx, i0[:, 1] + dy, i0[:, 2] + dz
                for c in range(k): out[:, c] += w * acc[c][ix, iy, iz]
                wt += w * acc[k][ix, iy, iz]
    return out / np.maximum(wt, 1e-9)[:, None]

def bands(G, texdir, sig_vol=0.012, fname="torsob_nrm.ftex"):
    """bandas del normal map de TorsoBody en los texeles del torso (2048 x 1024, v 0..0.5; o el brazo, 2048 x 2048): dict con
    base, b1 (sin micro), vol (volumen 3D), en (H, W, 2) componentes de la textura (rojo = T, verde = B = cross(N, T) * w)"""
    from scipy.ndimage import gaussian_filter
    H, W = G["mask"].shape
    nm = load_ftex(os.path.join(texdir, fname), mip=1)          # 2048 x 2048: las filas 0..H-1
    base = nm[:H, :W, :2].astype(np.float64) * 2 - 1
    M = G["mask"].astype(np.float64)
    b1 = np.zeros_like(base)
    den = gaussian_filter(M, 3.0)
    for c in range(2): b1[..., c] = gaussian_filter(base[..., c] * M, 3.0) / np.maximum(den, 1e-6)
    ii = np.where(G["mask"])
    P = G["P"][ii]; T = G["Tg"][ii]; B = G["Bn"][ii]
    s3 = base[ii][:, :1] * T + base[ii][:, 1:2] * B                        # pendiente en 3D (tangente)
    vb = grid_blur3d(P, s3, np.ones(len(P)), sig_vol)
    vol = np.zeros_like(base)
    vol[ii] = np.stack([np.sum(vb * T, 1), np.sum(vb * B, 1)], 1)
    return dict(base=base, b1=b1, vol=vol)

# espalda de cada variante: (lineas, volumen). Normal = suave (como las referencias), robusto = casi sin marcas (la grasa
# alisa), atletico = mas lineas y mas volumen (ademas de los musculos de height_back_athletic)
BACK = {"normal": (0.30, 1.10), "robusto": (0.08, 0.70), "atletico": (0.60, 1.35)}

def back_soft(Bd, P, mask, k_lines=0.30, k_vol=1.10):
    """delta (H, W, 2) que deja la espalda en micro + k_lineas * lineas + k_vol * volumen"""
    base, b1, vol = Bd["base"], Bd["b1"], Bd["vol"]
    micro = base - b1; lines = b1 - vol
    soft = micro + k_lines * lines + k_vol * vol
    bm = np.zeros(mask.shape)
    ii = np.where(mask); bm[ii] = back_mask(P[ii]) * torso_mask(P[ii])
    return (soft - base) * bm[..., None], bm

def back_deltas(Bd, P, mask):
    """(base, robusto, atletico): la correccion de la espalda para todos (normal) y lo que cada variante suma encima"""
    dN, _ = back_soft(Bd, P, mask, *BACK["normal"])
    dRb, _ = back_soft(Bd, P, mask, *BACK["robusto"])
    dAb, _ = back_soft(Bd, P, mask, *BACK["atletico"])
    return dN, dRb - dN, dAb - dN

def region_deltas(Bd, P, mask, fade):
    """(26/10 b) con el relieve del normal map llevado a la malla (torso_relief.REGION): la textura de cada variante queda en
    micro + a * lineas + (b - k_g * costura) * volumen. Devuelve (correccion de la base para todos, delta del robusto, delta del
    atletico), (H, W, 2); fade = desvanecido de las costuras (H, W) (la malla no se mueve en las costuras)"""
    import torso_relief as tr
    base, b1, vol = Bd["base"], Bd["b1"], Bd["vol"]
    micro = base - b1; lines = b1 - vol
    ii = np.where(mask)
    bm = np.zeros(mask.shape); bm[ii] = back_mask(P[ii])
    tm = np.zeros(mask.shape); tm[ii] = torso_mask(P[ii])
    out = {}
    for nm in ("normal", "robusto", "atletico"):
        a, b, kg = np.zeros(mask.shape), np.zeros(mask.shape), np.zeros(mask.shape)
        a[ii], b[ii], kg[ii] = tr.region_factors(nm, bm[ii], P[ii])
        M = micro + a[..., None] * lines + (b - kg * fade)[..., None] * vol
        out[nm] = (M - base) * tm[..., None]
    return out["normal"], out["robusto"] - out["normal"], out["atletico"] - out["normal"]
