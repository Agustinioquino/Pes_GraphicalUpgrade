# PES21 FX (26/10 g) - torso_2..6_bsm.ftex a partir del torso_1_bsm propio del usuario (piel neutra con detalle): el mismo
# detalle llevado al tono de cada torso de TorsoBody. Escala por canal en luz lineal, medida en la franja de 3 cm junto a la
# costura del hombro (asi el torso queda igual que arm_N_bsm ahi). Imprime el color nuevo del borde del cuello de cada tono
# (TEX.TORSO del modulo del cuello).
# Uso: python3 make_tones.py <tex de TorsoBody> <common> <torso_1_bsm.ftex propio> <carpeta de salida>
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'fox'), os.path.join(HERE, '..', 'fox', 'torso')]
from ftex import Ftex
from make_texture_pack import decode, encode_chain
from make_srm import uv_mask, shoulder_dist

def lin(c): return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
def srgb(c): c = np.clip(c, 0, 1); return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)

def neck_band(common, H, W):
    from uvbake import bake
    from torso_mesh import load_torso
    from make_texture_pack import upsample
    import torso_seam
    from scipy.spatial import cKDTree
    T = load_torso(common, "out"); G = bake(T, None, W=2048, H=1024, vmax=0.5)
    d = np.full(G["mask"].shape, 9.0); ii = np.where(G["mask"])
    d[ii] = cKDTree(torso_seam.neck_ring(T)).query(G["P"][ii])[0]
    full = np.full((H, W), 9.0, np.float32); full[:H // 2] = upsample(d[..., None].astype(np.float32), H // 2, W)[..., 0]
    return full < 0.01

if __name__ == "__main__":
    texdir, common, user, out = sys.argv[1:5]
    os.makedirs(out, exist_ok=True)
    U = decode(Ftex(open(user, "rb").read())); H, W = U.shape[:2]
    m = uv_mask(common, [("pants_out_sub", [1, 2, 3])], H, W, erode=8)
    ring = (shoulder_dist(common, H, W) < 0.03) & m
    neck = neck_band(common, H, W) & m
    Ul = lin(U[..., :3]); ur = Ul[ring].mean(0)
    print("cuello del usuario (tono 1): %s" % np.round(srgb(Ul[neck].mean(0)) * 255, 1))
    for k in range(2, 7):
        Ft = Ftex(open(os.path.join(texdir, "torso_%d_bsm.ftex" % k), "rb").read())
        t = lin(decode(Ft)[..., :3])
        f = t[ring].mean(0) / ur
        o = np.ones((H, W, 4), np.float32); o[..., :3] = srgb(Ul * f)
        Ft.mips = encode_chain(Ft, o)
        open(os.path.join(out, "torso_%d_bsm.ftex" % k), "wb").write(Ft.to_bytes(chunk=65536, small_first=True))
        print("tono %d: factor %s, cuello %s" % (k, np.round(f, 3), np.round(srgb(Ul[neck].mean(0) * f) * 255, 1)))
