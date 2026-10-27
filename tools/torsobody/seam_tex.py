# PES21 FX (26/10 i) - CORTES DE UV SIN LINEA: torso (torso_N_bsm, torsob_nrm) y brazos (arm_N_bsm, arm_nrm) continuos en 3D.
# En cada corte de UV (vertices soldados con dos uv distintas: los costados y la cintura del torso, el brazo por dentro) y en
# la costura del hombro (torso <-> brazo), las dos texturas tienen colores medios distintos y el normal map de cada lado tiene
# otro "volumen" (lo que tuerce la luz a escala de centimetros): se veia la linea en las dos vistas.
# Aca la parte de baja frecuencia (lo que mide mas de ~1.2 cm) se vuelve continua EN 3D: se reparte en una grilla 3D (celdas de
# 6 mm) lo de todas las texturas juntas (torso y brazo), se suaviza (1.2 cm) y en una franja junto a cada corte la baja
# frecuencia de cada texel (desenfoque en su isla de UV) se cambia por la de la grilla (color: cociente en luz lineal;
# normal map: el vector en espacio objeto con el marco tangente del juego, B = cross(N, T) * w, de vuelta a cada lado). Entero
# en el corte, nada a 6 cm. El detalle fino (poros, lunares) queda.
# Uso: python3 seam_tex.py <common> <carpeta de entrada (torso_N_bsm, arm_N_bsm, torsob_nrm, arm_nrm)> <carpeta de salida>
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'fox'), os.path.join(HERE, '..', 'fox', 'torso')]
from ftex import Ftex
from make_texture_pack import decode, encode_chain, upsample

CELL = 0.006; SIG3 = 0.012; W_IN, W_OUT = 0.01, 0.06

def lin(c): return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
def srgb(c): c = np.clip(c, 0, 1); return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)
def ss(a, b, x): t = np.clip((np.asarray(x, float) - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)

def uv_seams(T):
    """posiciones (soldadas) con mas de una uv"""
    uv = np.concatenate([p["uv"] for p in T["parts"]])
    key = [tuple(x) for x in np.round(uv * 4096).astype(np.int64)]
    seen = {}
    for w, k in zip(T["inv"], key): seen.setdefault(int(w), set()).add(k)
    idx = [w for w, ks in seen.items() if len(ks) > 1]
    return T["Pw"][np.array(idx, int)]

def masked_blur(img, mask, sigma):
    from scipy.ndimage import gaussian_filter
    m = mask.astype(np.float32)
    wb = gaussian_filter(m, sigma)
    out = np.stack([gaussian_filter(img[..., c] * m, sigma) for c in range(img.shape[-1])], -1)
    return out / np.maximum(wb[..., None], 1e-6)

def meters_per_px(G):
    P = G["P"]; m = G["mask"][:, 1:] & G["mask"][:, :-1]
    d = np.linalg.norm(P[:, 1:] - P[:, :-1], axis=-1)[m]
    return float(np.median(d))

class Grid:
    def __init__(self, pts):
        self.lo = pts.min(0) - 0.05; hi = pts.max(0) + 0.05
        self.shape = tuple(np.ceil((hi - self.lo) / CELL).astype(int) + 1)
    def idx(self, P): return np.clip(np.round((P - self.lo) / CELL).astype(int), 0, np.array(self.shape) - 1)
    def field(self, Ps, Vs):
        """Ps / Vs: listas (puntos, valores (n, c)); devuelve la grilla suavizada (normalizada por el peso)"""
        from scipy.ndimage import gaussian_filter
        c = Vs[0].shape[1]
        acc = np.zeros(self.shape + (c,), np.float32); wgt = np.zeros(self.shape, np.float32)
        for P, V in zip(Ps, Vs):
            i = self.idx(P)
            np.add.at(wgt, (i[:, 0], i[:, 1], i[:, 2]), 1.0)
            for k in range(c): np.add.at(acc[..., k], (i[:, 0], i[:, 1], i[:, 2]), V[:, k])
        s = SIG3 / CELL
        wb = gaussian_filter(wgt, s)
        out = np.stack([gaussian_filter(acc[..., k], s) for k in range(c)], -1)
        return out / np.maximum(wb[..., None], 1e-6)
    def sample(self, F, P):
        from scipy.ndimage import map_coordinates
        q = ((P - self.lo) / CELL).T
        return np.stack([map_coordinates(F[..., k], q, order=1, mode="nearest") for k in range(F.shape[-1])], -1)

def down(img, h, w):
    fy = img.shape[0] // h; fx = img.shape[1] // w
    return img[:h * fy, :w * fx].reshape(h, fy, w, fx, -1).mean((1, 3))

def setup(common):
    from uvbake import bake
    from torso_mesh import load_torso, load_parts
    from scipy.spatial import cKDTree
    T = load_torso(common, "out"); A = load_parts(common, [("arm", 0)])
    Gt = bake(T, None, W=2048, H=1024, vmax=0.5); Ga = bake(A, None, W=2048, H=2048, vmax=1.0)
    dA, _ = cKDTree(A["Pw"]).query(T["Pw"]); shoulder = T["Pw"][dA < 1e-4]
    seams = np.concatenate([uv_seams(T), uv_seams(A), shoulder])
    tree = cKDTree(seams)
    import torso_seam
    neck = torso_seam.neck_ring(T)
    ii = np.where(Gt["mask"]); dn = np.full(Gt["mask"].shape, 9.0); dn[ii] = cKDTree(neck).query(Gt["P"][ii])[0]
    Gt["dneck"] = dn; Ga["dneck"] = np.full(Ga["mask"].shape, 9.0)
    for G in (Gt, Ga):
        ii = np.where(G["mask"]); d = np.full(G["mask"].shape, 9.0); d[ii] = tree.query(G["P"][ii])[0]
        G["dseam"] = d; G["w"] = np.where(G["mask"], 1 - ss(W_IN, W_OUT, d), 0.0)
        G["mpp"] = meters_per_px(G)
    grid = Grid(np.concatenate([Gt["P"][Gt["mask"]], Ga["P"][Ga["mask"]]]))
    print("cortes: %d puntos (torso %d, brazo %d, hombro %d); franja: torso %.1f %%, brazo %.1f %%; %.2f / %.2f mm por texel"
          % (len(seams), len(uv_seams(T)), len(uv_seams(A)), len(shoulder), 100 * (Gt["w"] > 0.01).mean(), 100 * (Ga["w"] > 0.01).mean(), Gt["mpp"] * 1000, Ga["mpp"] * 1000))
    return Gt, Ga, grid

def fix_color(Gt, Ga, grid, tpath, apath, tout, aout):
    Ft = Ftex(open(tpath, "rb").read()); Fa = Ftex(open(apath, "rb").read())
    t = decode(Ft); a = decode(Fa)
    H, W = t.shape[:2]; Ha, Wa = a.shape[:2]
    tl = down(lin(t[:H // 2, :, :3]), *Gt["mask"].shape); al = down(lin(a[..., :3]), *Ga["mask"].shape)
    lt = masked_blur(tl, Gt["mask"], SIG3 / Gt["mpp"]); la = masked_blur(al, Ga["mask"], SIG3 / Ga["mpp"])
    F3 = grid.field([Gt["P"][Gt["mask"]], Ga["P"][Ga["mask"]]], [tl[Gt["mask"]], al[Ga["mask"]]])
    res = []
    for G, img, low, F, full, Hh, Ww, top, outp in ((Gt, t, lt, Ft, H // 2, H // 2, W, True, tout), (Ga, a, la, Fa, Ha, Ha, Wa, False, aout)):
        ratio = np.ones(G["mask"].shape + (3,), np.float32)
        ii = np.where(G["w"] > 0.001)
        g3 = grid.sample(F3, G["P"][ii])
        r = g3 / np.maximum(low[ii], 1e-5)
        ratio[ii] = 1 + (np.clip(r, 0.5, 2.0) - 1) * G["w"][ii][:, None]
        up = upsample(ratio, Hh, Ww)
        o = img.copy()
        if top: o[:Hh, :, :3] = srgb(lin(img[:Hh, :, :3]) * up)
        else: o[..., :3] = srgb(lin(img[..., :3]) * up)
        F.mips = encode_chain(F, o)
        open(outp, "wb").write(F.to_bytes(chunk=65536, small_first=True))
        res.append(float(np.abs(r - 1).mean()))
    return res

def fix_normal(Gt, Ga, grid, tpath, apath, tout, aout):
    Ft = Ftex(open(tpath, "rb").read()); Fa = Ftex(open(apath, "rb").read())
    t = decode(Ft); a = decode(Fa)
    H, W = t.shape[:2]; Ha, Wa = a.shape[:2]
    txy = down(t[:H // 2, :, :2] * 2 - 1, *Gt["mask"].shape); axy = down(a[..., :2] * 2 - 1, *Ga["mask"].shape)
    vt = masked_blur(txy, Gt["mask"], SIG3 / Gt["mpp"]); va = masked_blur(axy, Ga["mask"], SIG3 / Ga["mpp"])
    def obj(G, v): return v[..., :1] * G["Tg"] + v[..., 1:2] * G["Bn"]
    F3 = grid.field([Gt["P"][Gt["mask"]], Ga["P"][Ga["mask"]]], [obj(Gt, vt)[Gt["mask"]], obj(Ga, va)[Ga["mask"]]])
    res = []
    for G, img, v, F, Hh, Ww, top, outp in ((Gt, t, vt, Ft, H // 2, W, True, tout), (Ga, a, va, Fa, Ha, Wa, False, aout)):
        dxy = np.zeros(G["mask"].shape + (2,), np.float32)
        ii = np.where(G["w"] > 0.001)
        o3 = grid.sample(F3, G["P"][ii])
        loc = np.stack([np.sum(o3 * G["Tg"][ii], 1), np.sum(o3 * G["Bn"][ii], 1)], 1)
        dxy[ii] = (loc - v[ii]) * G["w"][ii][:, None]
        # junto al corte (1 cm) tambien el detalle de cada lado se va (cada textura tiene el suyo: ahi quedaba el salto)
        wd = np.where(G["mask"], 1 - ss(0.002, 0.012, G["dseam"]), 0.0)
        vlow = np.zeros(G["mask"].shape + (2,), np.float32); vlow[G["mask"]] = v[G["mask"]]
        up = upsample(dxy, Hh, Ww); upw = upsample(wd[..., None].astype(np.float32), Hh, Ww); upv = upsample(vlow, Hh, Ww)
        o = img.copy(); xy = img[..., :2] * 2 - 1
        if top:
            det = xy[:Hh] - upv; xy[:Hh] = xy[:Hh] + up - det * upw
        else:
            det = xy - upv; xy = xy + up - det * upw
        # (cuello) junto al borde del cuello el normal map del torso se aplana: ahi manda la normal de la malla, que es la del
        # borde de Konami = la del cuello de la cara (el modulo del cuello le pone esa misma a la cara)
        wn = np.where(G["mask"], 1 - ss(0.002, 0.012, G["dneck"]), 0.0)
        if wn.max() > 0:
            upn = upsample(wn[..., None].astype(np.float32), Hh, Ww)
            if top: xy[:Hh] *= 1 - upn
            else: xy *= 1 - upn
        l2 = np.sum(xy * xy, -1, keepdims=True); xy = np.where(l2 > 0.94, xy * np.sqrt(0.94 / np.maximum(l2, 1e-9)), xy)
        o[..., :2] = xy * 0.5 + 0.5; o[..., 3] = 1
        F.mips = encode_chain(F, o)
        open(outp, "wb").write(F.to_bytes(chunk=65536, small_first=True))
        res.append(float(np.abs(loc - v[ii]).mean()))
    return res

if __name__ == "__main__":
    common, ind, outd = sys.argv[1:4]
    os.makedirs(outd, exist_ok=True)
    Gt, Ga, grid = setup(common)
    r = fix_normal(Gt, Ga, grid, os.path.join(ind, "torsob_nrm.ftex"), os.path.join(ind, "arm_nrm.ftex"), os.path.join(outd, "torsob_nrm.ftex"), os.path.join(outd, "arm_nrm.ftex"))
    print("normal: cambio medio del volumen en la franja: torso %.3f, brazo %.3f" % tuple(r))
    for k in range(1, 7):
        r = fix_color(Gt, Ga, grid, os.path.join(ind, "torso_%d_bsm.ftex" % k), os.path.join(ind, "arm_%d_bsm.ftex" % k), os.path.join(outd, "torso_%d_bsm.ftex" % k), os.path.join(outd, "arm_%d_bsm.ftex" % k))
        print("tono %d: cambio medio del color en la franja: torso %.3f, brazo %.3f" % ((k,) + tuple(r)))
