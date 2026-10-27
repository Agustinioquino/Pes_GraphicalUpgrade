# PES21 FX (26/10 d) - inclinacion de la MALLA de cada version (base esculpida, robusto, atletico) respecto de la superficie
# original de TorsoBody, en el espacio UV, para el tope del relieve del PS de la piel (aa7d1833, x307):
#   pes_var_torso_tilt_b / _a / _r.dds   (BC5 SNORM, 1024 x 512 = v 0..0.5 del torso)
#   pes_var_arm_tilt_b / _a / _r.dds     (BC5 SNORM, 1024 x 1024 = todo el atlas del brazo)
# Por texel: m = -(N0 . T, N0 . B), con N0 la normal original y (T, B = cross(N, T) * w, N) el marco de la malla de esa version
# (filas 1..9 de los datos del VS). En la convencion de las texturas (rojo = T, verde = B), como el normal map antes del giro
# x286. El PS suma m al normal map (malla + textura = la inclinacion total respecto de la superficie original), la limita con
# una curva suave y le vuelve a restar m: solo se recorta lo que pasa de ~30 grados (las manchas negras de los surcos hondos,
# donde la malla esculpida, la textura y la definicion se sumaban hasta 80 grados), sin tocar el resto del relieve.
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from uvbake import bake, dilate

def _norm(a):
    return a / np.maximum(np.linalg.norm(a, axis=-1, keepdims=True), 1e-9)

def tilt_maps(T, rows, W, H, vmax):
    """T = malla (torso_mesh.load_parts / torso_variants.build) con sus partes; rows = lista por parte de (ROWS, n, 4) (filas de
    make_variants.version_rows). Devuelve dict b / a / r -> (H, W, 2) y la mascara"""
    extras = []
    for p, r in zip(T["parts"], rows):
        extras.append(np.concatenate([p["nrm"][:, :3], r[8, :, :3], r[9, :, :3], r[5, :, :3], r[6, :, :3], r[2, :, :3], r[3, :, :3]], 1))
    G = bake(T, extras, W=W, H=H, vmax=vmax)
    E, M = G["extra"], G["mask"]
    N0 = _norm(E[..., 0:3])
    # signo de la binormal de TorsoBody por texel (bake: Bn = cross(N, T) * w)
    sgn = np.where(np.sum(G["Bn"] * np.cross(G["N"], G["Tg"]), -1) < 0, -1.0, 1.0)[..., None]
    out = {}
    for key, (ni, ti) in (("b", (3, 6)), ("a", (9, 12)), ("r", (15, 18))):
        N = _norm(E[..., ni:ni + 3]); Tt = E[..., ti:ti + 3]
        Tt = _norm(Tt - N * np.sum(Tt * N, -1, keepdims=True))
        B = np.cross(N, Tt) * sgn
        m = -np.stack([np.sum(N0 * Tt, -1), np.sum(N0 * B, -1)], -1)
        out[key] = np.clip(m, -0.95, 0.95) * M[..., None]
    return out, M

def write_tilt(outdir, prefix, maps, mask, write_bc5, mipchain, iters=24):
    stats = []
    for key in ("b", "a", "r"):
        full, _ = dilate(maps[key], mask, iters=iters)
        write_bc5(os.path.join(outdir, "%s_tilt_%s.dds" % (prefix, key)), mipchain(full))
        s = np.degrees(np.arcsin(np.clip(np.linalg.norm(maps[key][mask], axis=-1), 0, 1)))
        stats.append("%s media %.1f, p95 %.1f grados" % (key, s.mean(), np.percentile(s, 95)))
    return "; ".join(stats)
