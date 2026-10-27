# PES21 FX (26/10 j) - DEFINICION EN EL COLOR PARA CADA TIPO DE CUERPO (normal / atletico / gordo) y detalle en el alfa del bsm.
# El torso_1_bsm propio del usuario tiene "definicion": el color se oscurece en los surcos del musculo (borde del pectoral, linea
# alba, entre los abdominales, columna) y se aclara en lo alto del musculo. Se mide esa relacion en su textura: banda media del
# logaritmo de la luminancia (2 mm .. 2.7 cm) contra la curvatura del relieve (normal map, divergencia de xy, a la misma escala):
#   log L ~ k * curvatura
# y se aplica al CAMBIO de relieve de cada variante de PES21FX: atletico = surcos mas hondos (mapa delta + inclinacion de su malla)
# -> mas definicion; gordo = mas liso (mapa delta + malla, y el PS le baja el relieve de la base un 30 %) -> menos. Sale en
# pes_var_torso_rob_m.dds (RGBA8: R = estrias como antes, G = factor del gordo, B = factor del atletico; 0.5 = sin cambio,
# factor = 1 + (v - 0.5) * 0.8). El PS (x311) los aplica con el peso de la variante.
# Alfa del bsm (los 6 tonos): la curvatura fina (poros, pliegues, < 1 cm) del normal map con la misma k, 0.5 = neutro; el juego
# no lee el alfa del bsm del cuerpo, PES21FX si (x311, z311): se puede retocar a mano (mas oscuro = mas hondo).
# Uso: python3 variant_albedo.py <common (con el cuello de Konami)> <carpeta tex (torso_N_bsm, torsob_nrm)> <carpeta pes_var (+ maps_*.npy)> <salida>
import sys, os, struct
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', '..'), os.path.join(HERE, '..', '..', 'torsobody')]
from scipy.ndimage import gaussian_filter

RANGE = 0.8   # factor = 1 + (v - 0.5) * RANGE

def read_bc5_snorm(path):
    from texio import decode_bc4_snorm
    d = open(path, "rb").read()
    h, w = struct.unpack_from("<II", d, 12)
    n = (h // 4) * (w // 4)
    blk = np.frombuffer(d, np.uint8, count=n * 16, offset=148).reshape(n, 16)
    return np.stack([decode_bc4_snorm(blk[:, :8], h, w), decode_bc4_snorm(blk[:, 8:], h, w)], -1)

def curv(xy, s):
    x = gaussian_filter(xy[..., 0], s); y = gaussian_filter(xy[..., 1], s)
    return np.gradient(x, axis=1) + np.gradient(y, axis=0)

def band(a, s1, s2): return gaussian_filter(a, s1) - gaussian_filter(a, s2)

def down(img, h, w):
    fy = img.shape[0] // h; fx = img.shape[1] // w
    return img[:h * fy, :w * fx].reshape(h, fy, w, fx, -1).mean((1, 3))

def write_rgba8_dds(path, mips):
    from texio import dds_dx10_header
    h, w = mips[0].shape[:2]
    blobs = [np.clip(np.round(m * 255), 0, 255).astype(np.uint8).tobytes() for m in mips]
    with open(path, "wb") as f:
        f.write(dds_dx10_header(w, h, len(mips), 28, w * 4)); f.write(b"".join(blobs))

def main(common, texdir, vardir, outdir):
    from uvbake import bake, dilate
    from torso_mesh import load_torso
    from ftex import Ftex
    from make_texture_pack import decode, encode_chain, upsample
    from make_variants import mipchain
    os.makedirs(outdir, exist_ok=True)
    T = load_torso(common, "out"); G = bake(T, None, W=2048, H=1024, vmax=0.5)
    m = G["mask"]
    from scipy.ndimage import binary_erosion
    core = binary_erosion(m, iterations=12)
    nm = decode(Ftex(open(os.path.join(texdir, "torsob_nrm.ftex"), "rb").read()))
    xy = down(nm[:nm.shape[0] // 2, :, :2] * 2 - 1, 1024, 2048)
    bsm = decode(Ftex(open(os.path.join(texdir, "torso_1_bsm.ftex"), "rb").read()))
    lin = lambda c: np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    L = np.log(np.maximum(down(lin(bsm[:bsm.shape[0] // 2, :, :3]), 1024, 2048).mean(-1), 1e-4))
    # (medido) la definicion del torso_1 es luz de arriba horneada: log L ~ c * (componente y, hacia arriba, de la normal en espacio
    # objeto menos la de la malla), r2 0.18 (2 mm .. 2.7 cm) .. 0.61 (2 .. 11 cm), c 0.26 .. 0.56; la curvatura sola no explica nada
    def objn(xy_):
        z = np.sqrt(np.clip(1 - np.sum(xy_ ** 2, -1), 0, 1))[..., None]
        n = xy_[..., :1] * G["Tg"] + xy_[..., 1:2] * G["Bn"] + z * G["N"]
        return n / np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-9)
    nb = objn(xy)
    Db = band(L, 3, 100); Yb = band(nb[..., 1] - G["N"][..., 1], 3, 100)
    c = float(np.sum(Db[core] * Yb[core]) / np.sum(Yb[core] ** 2))
    print("definicion del torso_1: log L = %.3f x (normal hacia arriba) en 2 mm .. 7 cm; correlacion %.2f" % (c, np.corrcoef(Db[core], Yb[core])[0, 1]))
    c = float(np.clip(c, 0.2, 0.6))
    k = c
    dA = np.load(os.path.join(vardir, "maps_dA.npy")); dR = np.load(os.path.join(vardir, "maps_dR.npy"))
    tA = read_bc5_snorm(os.path.join(vardir, "pes_var_torso_tilt_a.dds")); tR = read_bc5_snorm(os.path.join(vardir, "pes_var_torso_tilt_r.dds"))
    tB = read_bc5_snorm(os.path.join(vardir, "pes_var_torso_tilt_b.dds"))
    fit = lambda t: t if t.shape[0] == 1024 else (down(t, 1024, 2048) if t.shape[0] > 1024 else upsample(t.astype(np.float32), 1024, 2048))
    tA, tR, tB = fit(tA), fit(tR), fit(tB)
    def clamp(v):
        l2 = np.sum(v * v, -1, keepdims=True); return np.where(l2 > 0.94, v * np.sqrt(0.94 / np.maximum(l2, 1e-9)), v)
    na = objn(clamp(xy + dA + (tA - tB))); nr = objn(clamp(xy * 0.7 + dR + (tR - tB)))   # el PS: el gordo un 30 % mas liso
    YA = band(na[..., 1] - nb[..., 1], 3, 100); YR = band(nr[..., 1] - nb[..., 1], 3, 100)
    fA = np.exp(np.clip(c * YA, -0.3, 0.3)); fR = np.exp(np.clip(c * YR, -0.3, 0.3))
    fA = np.where(m, fA, 1.0); fR = np.where(m, fR, 1.0)
    print("atletico: factor del color p1 %.3f p99 %.3f; gordo: p1 %.3f p99 %.3f" % (np.percentile(fA[m], 1), np.percentile(fA[m], 99), np.percentile(fR[m], 1), np.percentile(fR[m], 99)))
    ms = np.load(os.path.join(vardir, "maps_mR.npy"))
    rgba = np.zeros((1024, 2048, 4), np.float32)
    rgba[..., 0] = ms; rgba[..., 1] = 0.5 + (fR - 1) / RANGE; rgba[..., 2] = 0.5 + (fA - 1) / RANGE; rgba[..., 3] = 1
    full, mm = dilate(rgba[..., :3], m, iters=48)
    full[~mm] = (0.0, 0.5, 0.5)
    rgba[..., :3] = full
    write_rgba8_dds(os.path.join(outdir, "pes_var_torso_rob_m.dds"), mipchain(np.clip(rgba, 0, 1)))
    # alfa del bsm: curvatura fina (< 1 cm) con la misma k, en todos los tonos
    # alfa del bsm: lo mismo a escala fina (poros, pliegues: < 8 mm), luz de arriba sobre el relieve fino del normal map
    Yf = band(nb[..., 1] - G["N"][..., 1], 0.0001, 12)
    fa = np.exp(np.clip(c * Yf, -0.25, 0.25)); fa = np.where(core, fa, 1.0)
    print("alfa del bsm: factor p1 %.3f p99 %.3f" % (np.percentile(fa[m], 1), np.percentile(fa[m], 99)))
    a2 = 0.5 + (fa - 1) / RANGE
    for kk in range(1, 7):
        p = os.path.join(texdir, "torso_%d_bsm.ftex" % kk)
        F = Ftex(open(p, "rb").read()); img = decode(F); H, W = img.shape[:2]
        A = np.full((H, W), 0.5, np.float32); A[:H // 2] = upsample(a2[..., None].astype(np.float32), H // 2, W)[..., 0]
        img[..., 3] = np.clip(A, 0, 1)
        F.mips = encode_chain(F, img)
        open(os.path.join(outdir, "torso_%d_bsm.ftex" % kk), "wb").write(F.to_bytes(chunk=65536, small_first=True))
    return k

if __name__ == "__main__":
    main(*sys.argv[1:5])
