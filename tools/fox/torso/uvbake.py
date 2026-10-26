# PES21 FX (26/10) - imagen de geometria del torso en el espacio UV de TorsoBody (torsob_nrm: el torso ocupa v 0..0.47):
# por texel, la posicion de reposo, la normal, la tangente (con el signo de la binormal) y cuanto se mueve el vertice
# (costuras quietas), interpolados del triangulo que cubre ese texel. Se rasteriza con rastid (C).
import sys, os, struct, subprocess
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RAST = os.path.join(HERE, 'rastid')

def rasterize(W, H, tris_xyz, tmp):
    """tris_xyz: (n, 3, 3) en pixeles (x, y, z) -> id (H, W) int32, b0, b1"""
    n = len(tris_xyz)
    with open(tmp + '.in', 'wb') as f:
        f.write(struct.pack('<iii', W, H, n)); f.write(np.ascontiguousarray(tris_xyz, dtype=np.float32).tobytes())
    subprocess.run([RAST, tmp + '.in', tmp + '.out'], check=True)
    raw = np.fromfile(tmp + '.out', dtype=np.uint8)
    idv = raw[:4 * W * H].view(np.int32).reshape(H, W)
    b0 = raw[4 * W * H: 8 * W * H].view(np.float32).reshape(H, W)
    b1 = raw[8 * W * H: 12 * W * H].view(np.float32).reshape(H, W)
    return idv, b0, b1

def bake(T, per_part_extra, W=2048, H=1024, vmax=0.5, tmp=None):
    """T = torso_variants.build(...). per_part_extra: lista (por malla) de arreglos (nvert, k) que tambien se interpolan.
    Devuelve dict: mask (H, W) bool, P, N, Tg, Bn (H, W, 3), extra (H, W, k)"""
    tmp = tmp or os.path.join(os.environ.get('SIM_TMP', '/tmp'), '_uvbake%d' % os.getpid())
    tris = []; owners = []
    for pi, p in enumerate(T["parts"]):
        uv = p["uv"]; f = p["faces"]
        xy = np.stack([uv[:, 0] * W, uv[:, 1] / vmax * H, np.zeros(len(uv))], 1)
        tris.append(xy[f]); owners.append(np.stack([np.full(len(f), pi), np.arange(len(f))], 1))
    tris = np.concatenate(tris); owners = np.concatenate(owners)
    idv, b0, b1 = rasterize(W, H, tris, tmp)
    mask = idv >= 0
    k = per_part_extra[0].shape[1] if per_part_extra else 0
    P = np.zeros((H, W, 3)); N = np.zeros((H, W, 3)); Tg = np.zeros((H, W, 4)); E = np.zeros((H, W, k))
    ii = np.where(mask)
    own = owners[idv[ii]]
    w0 = b0[ii][:, None]; w1 = b1[ii][:, None]; w2 = 1.0 - w0 - w1
    for pi, p in enumerate(T["parts"]):
        s = own[:, 0] == pi
        if not s.any(): continue
        f = p["faces"][own[s, 1]]
        def interp(a):
            return a[f[:, 0]] * w0[s] + a[f[:, 1]] * w1[s] + a[f[:, 2]] * w2[s]
        sel = (ii[0][s], ii[1][s])
        P[sel] = interp(p["pos"]); N[sel] = interp(p["nrm"]); Tg[sel] = interp(p["tan"])
        if k: E[sel] = interp(per_part_extra[pi])
    N /= np.maximum(np.linalg.norm(N, axis=2, keepdims=True), 1e-9)
    t3 = Tg[..., :3] - N * np.sum(Tg[..., :3] * N, 2, keepdims=True)
    t3 /= np.maximum(np.linalg.norm(t3, axis=2, keepdims=True), 1e-9)
    sgn = np.where(Tg[..., 3] < 0, -1.0, 1.0)[..., None]
    Bn = np.cross(N, t3) * sgn          # = VS 1c02a92a: B = cross(N, T) * v3.w
    return dict(mask=mask, P=P, N=N, Tg=t3, Bn=Bn, extra=E, W=W, H=H, vmax=vmax)

def dilate(img, mask, iters=24):
    """rellena los texels vacios con el promedio de los vecinos llenos (crece de a un texel; para el filtrado y los mips)"""
    a = img.copy(); m = mask.copy()
    for _ in range(iters):
        if m.all(): break
        acc = np.zeros_like(a); cnt = np.zeros(m.shape)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1), (-1, 1), (1, -1)):
            sm = np.roll(np.roll(m, dy, 0), dx, 1); sa = np.roll(np.roll(a, dy, 0), dx, 1)
            acc += sa * sm[..., None]; cnt += sm
        new = (~m) & (cnt > 0)
        a[new] = acc[new] / cnt[new][:, None]; m = m | new
    return a, m
