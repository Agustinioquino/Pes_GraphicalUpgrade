# PES21 FX (26/10 h) - COSTURA DEL HOMBRO SIN LINEA EN EL COLOR: torso_N_bsm contra arm_N_bsm.
# Las dos texturas se encuentran en el anillo del hombro (el torso y el brazo comparten esos vertices). A lo largo del anillo
# (por tramos), el color medio del torso en los 1.5 cm junto a la costura y el del brazo en los 1.5 cm de su lado; la
# diferencia (luz lineal, por canal, suavizada a lo largo del anillo) se le suma al TORSO, entera en la costura y nada a
# 5 cm (smoothstep). El brazo no se toca (es el mismo para los 6 tonos de los dos lados). El detalle fino queda.
# Uso: python3 seam_color.py <common> <carpeta con arm_N_bsm.ftex> <torso_N_bsm.ftex> <N> <salida.ftex>
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'fox'), os.path.join(HERE, '..', 'fox', 'torso')]
from ftex import Ftex
from make_texture_pack import decode, encode_chain, upsample

def lin(c): return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
def srgb(c): c = np.clip(c, 0, 1); return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)

_CACHE = {}
def geometry(common):
    """por texel: distancia al anillo del hombro y tramo del anillo mas cercano (torso 2048 x 1024 v 0..0.5, brazo 2048 x 2048)"""
    if common in _CACHE: return _CACHE[common]
    from uvbake import bake
    from torso_mesh import load_torso
    from torso_variants import build_arm
    from scipy.spatial import cKDTree
    import torso_seam
    T = load_torso(common, "out"); A = build_arm(common, "arm")
    ring = torso_seam.shoulder_ring(T, A)
    # tramos: angulo alrededor del eje de cada hombro (x > 0 / x < 0) -> 2 x 36 tramos
    side = ring[:, 0] > 0
    def seg(P):
        d, i = cKDTree(ring).query(P)
        R = ring[i]; s = R[:, 0] > 0
        c = np.where(s[:, None], ring[side].mean(0), ring[~side].mean(0))
        v = R - c
        ang = np.degrees(np.arctan2(v[:, 1], v[:, 2]))
        return d, (np.floor((ang + 180) / 10).astype(int) % 36) + 36 * s
    Gt = bake(T, None, W=2048, H=1024, vmax=0.5); Ga = bake(A, None, W=2048, H=2048, vmax=1.0)
    out = {}
    for nm, G in (("t", Gt), ("a", Ga)):
        ii = np.where(G["mask"]); d = np.full(G["mask"].shape, 9.0); s = np.full(G["mask"].shape, -1)
        dd, ss_ = seg(G["P"][ii]); d[ii] = dd; s[ii] = ss_
        out[nm] = (d, s)
    _CACHE[common] = out
    return out

def fix(common, armdir, torso_path, N, out_path, band=0.015, fade=0.05, log=print):
    from scipy.ndimage import gaussian_filter1d
    g = geometry(common)
    Ft = Ftex(open(torso_path, "rb").read()); t = decode(Ft); H, W = t.shape[:2]
    a = decode(Ftex(open(os.path.join(armdir, "arm_%d_bsm.ftex" % N), "rb").read()))
    tl = lin(t[..., :3]); al = lin(a[..., :3])
    dT, sT = g["t"]; dA, sA = g["a"]
    # muestras a la resolucion de las tablas
    def down(img, h, w):
        f = img.shape[0] // h; return img[:h * f, :w * f].reshape(h, f, w, img.shape[1] // w, -1).mean((1, 3))
    ts = down(tl[:H // 2], *dT.shape); as_ = down(al, *dA.shape)
    mt = np.zeros((72, 3)); ma = np.zeros((72, 3)); ct = np.zeros(72); ca = np.zeros(72)
    selT = (dT < band) & (sT >= 0); selA = (dA < band) & (sA >= 0)
    np.add.at(mt, sT[selT], ts[selT]); np.add.at(ct, sT[selT], 1)
    np.add.at(ma, sA[selA], as_[selA]); np.add.at(ca, sA[selA], 1)
    ok = (ct > 20) & (ca > 20)
    ratio = np.ones((72, 3)); ratio[ok] = (ma[ok] / ca[ok, None]) / np.maximum(mt[ok] / ct[ok, None], 1e-6)
    for s0 in (0, 36):   # suavizado a lo largo de cada anillo (circular)
        r = ratio[s0:s0 + 36]; r[~ok[s0:s0 + 36]] = np.nan
        for c in range(3):
            col = r[:, c]; good = ~np.isnan(col)
            if good.sum() < 3: col[:] = 1; continue
            idx = np.arange(36); col[~good] = np.interp(idx[~good], idx[good], col[good], period=36)
            r[:, c] = gaussian_filter1d(col, 0.5, mode="wrap")
        ratio[s0:s0 + 36] = r
    log("tono %d: torso / brazo en la costura antes: %s" % (N, np.round((mt[ok] / np.maximum(ct[ok, None], 1)).mean(0) / (ma[ok] / np.maximum(ca[ok, None], 1)).mean(0), 3)))
    # campo de correccion en la textura del torso (mitad de arriba)
    w = 1 - np.clip((dT - 0.0) / fade, 0, 1); w = w * w * (3 - 2 * w); w[sT < 0] = 0
    corr = np.ones(dT.shape + (3,)); m = sT >= 0
    corr[m] = 1 + (ratio[sT[m]] - 1) * w[m, None]
    full = np.ones((H, W, 3)); full[:H // 2] = upsample(corr.astype(np.float32), H // 2, W)
    o = t.copy(); o[..., :3] = srgb(tl * full)
    Ft.mips = encode_chain(Ft, o)
    open(out_path, "wb").write(Ft.to_bytes(chunk=65536, small_first=True))
    # control
    t2 = lin(decode(Ftex(open(out_path, "rb").read()))[..., :3]); ts2 = down(t2[:H // 2], *dT.shape)
    mt2 = np.zeros((72, 3)); np.add.at(mt2, sT[selT], ts2[selT])
    log("        despues: %s (por tramo: peor %.3f)" % (np.round((mt2[ok] / np.maximum(ct[ok, None], 1)).mean(0) / (ma[ok] / np.maximum(ca[ok, None], 1)).mean(0), 3),
        np.abs((mt2[ok] / np.maximum(ct[ok, None], 1)) / (ma[ok] / np.maximum(ca[ok, None], 1)) - 1).max()))

if __name__ == "__main__":
    fix(sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5])
