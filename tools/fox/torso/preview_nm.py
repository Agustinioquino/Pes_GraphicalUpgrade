# Vista previa con texturas de las variantes del torso y los brazos (como las veria el PS de la piel): normales por vertice de la
# variante (datos de make_variants), normal map de TorsoBody + definicion + mapa delta, albedo. Rasteriza con rastid (C).
# Uso: python3 preview_nm.py <carpeta common> <carpeta tex de TorsoBody> <carpeta con los pes_var_*> <salida.png>
import sys, os, struct, subprocess, math
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
from fmdl import Fmdl, meshes
from texio import load_ftex
from uvbake import rasterize

def read_dds_float4(path):
    d = open(path, 'rb').read()
    h, w = struct.unpack_from('<II', d, 12)
    return np.frombuffer(d, dtype=np.float32, offset=148).reshape(h, w, 4)

def bilinear(img, u, v):
    H, W = img.shape[:2]
    x = np.clip(u * W - 0.5, 0, W - 1.001); y = np.clip(v * H - 0.5, 0, H - 1.001)
    x0 = x.astype(int); y0 = y.astype(int); fx = (x - x0)[:, None]; fy = (y - y0)[:, None]
    a = img[y0, x0]; b = img[y0, x0 + 1]; c = img[y0 + 1, x0]; d = img[y0 + 1, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy

def blur(img, s):
    from scipy.ndimage import gaussian_filter
    return np.stack([gaussian_filter(img[..., k], s) for k in range(img.shape[2])], -1)

class Mesh:
    def __init__(self, pos, nrm, tan, uv, faces, tex, delta=None, region="torso"):
        self.pos, self.nrm, self.tan, self.uv, self.faces, self.tex, self.delta, self.region = pos, nrm, tan, uv, faces, tex, delta, region

def render(meshes_, yaw, W, H, ppm, cx, cy, t, tmp, light=(-0.35, 0.55, 0.76)):
    th = math.radians(yaw)
    R = np.array([[math.cos(th), 0, math.sin(th)], [0, 1, 0], [-math.sin(th), 0, math.cos(th)]])
    tris, own = [], []
    for mi, m in enumerate(meshes_):
        p = m.pos @ R.T
        xy = np.stack([W / 2 + (p[:, 0] - cx) * ppm, H / 2 - (p[:, 1] - cy) * ppm, p[:, 2]], 1)
        tris.append(xy[m.faces]); own.append(np.stack([np.full(len(m.faces), mi), np.arange(len(m.faces))], 1))
    tris = np.concatenate(tris); own = np.concatenate(own)
    idv, b0, b1 = rasterize(W, H, tris, tmp)
    img = np.full((H, W, 3), 0.13)
    ii = np.where(idv >= 0)
    o = own[idv[ii]]; w0 = b0[ii][:, None]; w1 = b1[ii][:, None]; w2 = 1 - w0 - w1
    L = np.array(light); L /= np.linalg.norm(L)
    wr, wa = max(-t, 0.0), max(t, 0.0)
    for mi, m in enumerate(meshes_):
        s = o[:, 0] == mi
        if not s.any(): continue
        f = m.faces[o[s, 1]]
        I = lambda a: a[f[:, 0]] * w0[s] + a[f[:, 1]] * w1[s] + a[f[:, 2]] * w2[s]
        N = I(m.nrm) @ R.T; N /= np.linalg.norm(N, axis=1, keepdims=True)
        Tg = I(m.tan[:, :3]) @ R.T; Tg -= N * np.sum(Tg * N, 1, keepdims=True); Tg /= np.maximum(np.linalg.norm(Tg, axis=1, keepdims=True), 1e-9)
        B = np.cross(N, Tg) * np.where(I(m.tan[:, 3:4]) < 0, -1.0, 1.0)
        uv = I(m.uv)
        nm, nmLow, alb = m.tex
        xy = bilinear(nm, uv[:, 0], uv[:, 1]) * 2 - 1
        low = bilinear(nmLow, uv[:, 0], uv[:, 1]) * 2 - 1
        # definicion por variante (como el PS): torso 1 + 0.15 atl - 0.75 rob; brazos 1 + 0.20 atl - 0.50 rob
        # (la vista previa usa B = cross(N, T) * w, la convencion de las texturas: es lo que muestra el juego con x286 = 1)
        defM = 1 + (0.15 * wa - 0.75 * wr if m.region == "torso" else 0.20 * wa - 0.50 * wr)
        xy = xy + (defM - 1) * low
        if m.region == "torso": xy = xy * (1 - 0.3 * wr)   # robusto: todo el relieve del normal map un poco mas liso
        if m.delta is not None and t != 0:
            dR, dA = m.delta[0], m.delta[1]
            vv = np.clip(uv[:, 1] * 2, 0, 1)
            xy = xy + wr * bilinear(dR, uv[:, 0], vv) + wa * bilinear(dA, uv[:, 0], vv)
        if m.delta is not None and len(m.delta) > 4 and m.delta[4] is not None:   # (26/10) correccion de la espalda (todos)
            vv = np.clip(uv[:, 1] * 2, 0, 1)
            xy = xy + float(os.environ.get("PNM_BASE", "1")) * bilinear(m.delta[4], uv[:, 0], vv)
        if m.delta is not None and len(m.delta) > 3 and m.delta[3] is not None:   # (26/10) columna marcada (como el PS, x288)
            vv = np.clip(uv[:, 1] * 2, 0, 1)
            ks = float(os.environ.get("PNM_SPINE", "1")) * (1 - 0.5 * wr) * (1 + 0.25 * wa)
            xy = xy + ks * bilinear(m.delta[3], uv[:, 0], vv)
        if os.environ.get("PNM_CLAY"): xy = xy * 0.0   # (26/10 b) solo la malla (sin normal map ni color)
        l2 = np.sum(xy * xy, 1, keepdims=True); xy = np.where(l2 > 0.94, xy * np.sqrt(0.94 / np.maximum(l2, 1e-9)), xy)
        z = np.sqrt(np.clip(1 - np.sum(xy * xy, 1, keepdims=True), 0, 1))
        n = Tg * xy[:, :1] + B * xy[:, 1:2] + N * z; n /= np.linalg.norm(n, axis=1, keepdims=True)
        a = bilinear(alb, uv[:, 0], uv[:, 1])[:, :3]
        a = a ** 2.2
        if os.environ.get("PNM_CLAY"): a = np.full_like(a, 0.42)
        if m.delta is not None and len(m.delta) > 2 and wr > 0:   # estrias: un poco mas claras (como el PS)
            ms = bilinear(m.delta[2][..., None], uv[:, 0], np.clip(uv[:, 1] * 2, 0, 1))
            a = a + (a * np.array([1.07, 1.02, 1.03]) + 0.008 - a) * np.clip(ms * 0.6 * wr, 0, 1)
        dif = np.clip(n @ L, 0, 1)[:, None]
        rim = np.clip(1 - n[:, 2:3], 0, 1) ** 3 * 0.15
        sky = (0.5 + 0.5 * n[:, 1:2]) * 0.22
        col = a * (dif * 1.05 + sky) + rim * 0.3
        img[ii[0][s], ii[1][s]] = np.clip(col, 0, 1) ** (1 / 2.2)
    return Image.fromarray((img * 255).astype(np.uint8))

def load_scene(common, texdir, vardir, t):
    """mallas del torso (pants_out_sub 1..3) y los brazos (arm 0) con la variante t aplicada (posicion, normal, tangente)"""
    nm = load_ftex(os.path.join(texdir, "torsob_nrm.ftex"), mip=1)[..., :2]
    alb = load_ftex(os.path.join(texdir, "torso_3_bsm.ftex"), mip=1)
    nmA = load_ftex(os.path.join(texdir, "arm_nrm.ftex"), mip=1)[..., :2]
    albA = load_ftex(os.path.join(texdir, "arm_3_bsm.ftex"), mip=1)
    texT = (nm, blur(nm, 11), alb); texA = (nmA, blur(nmA, 11), albA)
    dR = np.load(os.path.join(vardir, "maps_dR.npy")); dA = np.load(os.path.join(vardir, "maps_dA.npy"))
    mR = np.load(os.path.join(vardir, "maps_mR.npy")) if os.path.exists(os.path.join(vardir, "maps_mR.npy")) else np.zeros(dR.shape[:2])
    dS = np.load(os.path.join(vardir, "maps_dS.npy")) if os.path.exists(os.path.join(vardir, "maps_dS.npy")) else None
    dB = np.load(os.path.join(vardir, "maps_dB.npy")) if os.path.exists(os.path.join(vardir, "maps_dB.npy")) else None
    out = []
    wr, wa = max(-t, 0.0), max(t, 0.0)
    def apply(m, data):
        pos = m["pos"].astype(np.float64); nrm = m["nrm"][:, :3].astype(np.float64); tan = m["tan"].astype(np.float64)
        if data is not None:
            R = 10 if data.shape[0] % 10 == 0 else 7          # (26/10 b) 10 filas: con la base (7..9)
            nv = data.shape[0] // R
            k = int(np.argmin([np.abs(data[R * v, :, :3] - pos).max() for v in range(nv)]))
            d0, n0, t0 = np.zeros_like(pos), nrm, tan[:, :3]
            if R == 10 and os.environ.get("PNM_NOBASE") is None:
                d0, n0, t0 = data[R * k + 7, :, :3], data[R * k + 8, :, :3], data[R * k + 9, :, :3]
            if t != 0:
                r = R * k + (1 if t < 0 else 4); a = abs(t)
                d1, n1, t1 = data[r, :, :3], data[r + 1, :, :3], data[r + 2, :, :3]
            else:
                a = 0.0; d1, n1, t1 = d0, n0, t0
            pos = pos + d0 * (1 - a) + a * d1
            nrm = n0 * (1 - a) + a * n1; nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
            tt = t0 * (1 - a) + a * t1
            tan = np.concatenate([tt, tan[:, 3:4]], 1)
        return pos, nrm, tan
    M = meshes(Fmdl(os.path.join(common, "pants_out_sub.fmdl")))
    for k in (1, 2, 3):
        data = read_dds_float4(os.path.join(vardir, "pes_var_torso_%d.dds" % k))
        p, n, tg = apply(M[k], data)
        out.append(Mesh(p, n, tg, M[k]["uv"].astype(np.float64), M[k]["faces"], texT, (dR, dA, mR, dS, dB), "torso"))
    MA = meshes(Fmdl(os.path.join(common, "arm.fmdl")))
    data = read_dds_float4(os.path.join(vardir, "pes_var_arm.dds"))
    p, n, tg = apply(MA[0], data)
    out.append(Mesh(p, n, tg, MA[0]["uv"].astype(np.float64), MA[0]["faces"], texA, None, "arm"))
    return out

def arm_bones_offset(scene, t, log):
    """grosor del brazo con los huesos (como musculo.cpp) usando sim.py y los reposos del log"""
    if not log: return
    sys.path.insert(0, os.path.join(HERE, '..', 'sim'))
    import sim
    from torso_variants import arm_bone_factors
    if t == 0: return
    rows = sim.parse_table(); rest = sim.parse_rest(log)
    D = sim.bone_deltas(rows, rest, arm_bone_factors(t))
    f = Fmdl(os.path.join(sys.argv[1], "arm.fmdl")); names = [b["name"] for b in f.bones]; m0 = meshes(f)[0]
    am = scene[-1]
    am.pos = sim.deform([dict(pos=am.pos, w=m0["w"], idx=m0["idx"], names=names)], D)[0]

def main():
    common, texdir, vardir, outp = sys.argv[1:5]
    log = sys.argv[5] if len(sys.argv) > 5 else None
    tmp = os.path.join(os.environ.get("SIM_TMP", "/tmp"), "_pnm%d" % os.getpid())
    views = [(0, 0.06, 1.24, 760, 600, 820, "frente"), (35, 0.10, 1.22, 760, 600, 820, "3/4"), (180, -0.06, 1.24, 760, 600, 820, "espalda"),
             (0, 0.40, 1.24, 760, 420, 1000, "brazo")]
    if os.environ.get("PNM_FB"):   # (26/10 b) frente y espalda enteros
        views = [(0, 0.0, 1.25, 560, 700, 1050, "frente"), (180, 0.0, 1.25, 560, 700, 1050, "espalda"), (40, 0.05, 1.25, 560, 700, 1050, "3/4")]
    if os.environ.get("PNM_SEAM"):   # (26/10) la union del brazo con el torso (hombro, axila, espalda)
        views = [(0, 0.24, 1.37, 560, 520, 2400, "hombro adelante"), (60, 0.20, 1.36, 560, 520, 2400, "hombro costado"),
                 (150, 0.22, 1.37, 560, 520, 2400, "hombro atras"), (90, 0.20, 1.30, 560, 520, 2400, "axila (costado)")]
    if os.environ.get("PNM_BACK"):   # (26/10) la espalda de cerca (columna)
        views = [(180, -0.0, 1.27, 600, 760, 1500, "espalda"), (205, 0.0, 1.27, 600, 760, 1500, "espalda 3/4")]
    if os.environ.get("PNM_CLOSE"):
        views = [(180, -0.05, 1.45, 760, 420, 1400, "trapecio (atras)"), (25, 0.12, 1.47, 760, 420, 1400, "trapecio (adelante)"),
                 (75, 0.03, 1.12, 760, 420, 1500, "costado (estrias)"), (0, 0.03, 1.30, 760, 420, 1400, "pecho")]
    rows = []
    for (yaw, cx, cy, W, H, ppm, nmv) in views:
        ims = []
        for t in ((0.0,) if os.environ.get("PNM_T0") else (-1.0, 0.0, 1.0)):
            sc = load_scene(common, texdir, vardir, t)
            arm_bones_offset(sc, t, log)
            lt = tuple(float(v) for v in os.environ["PNM_LIGHT"].split(",")) if os.environ.get("PNM_LIGHT") else (-0.35, 0.55, 0.76)
            ims.append(render(sc, yaw, W, H, ppm, cx, cy, t, tmp, light=lt))
        rows.append(ims)
    Wt = sum(im.width for im in rows[0]); Ht = sum(r[0].height + 28 for r in rows)
    sheet = Image.new("RGB", (Wt, Ht), (24, 24, 24))
    from PIL import ImageDraw
    d = ImageDraw.Draw(sheet); y = 0
    for r, v in zip(rows, views):
        x = 0
        for im, nm in zip(r, (("TORSOBODY",) if os.environ.get("PNM_T0") else ("ROBUSTO", "TORSOBODY", "ATLETICO"))):
            d.text((x + 10, y + 8), "%s  -  %s" % (nm, v[6]), fill=(230, 230, 230)); sheet.paste(im, (x, y + 28)); x += im.width
        y += r[0].height + 28
    sheet.save(outp)
    print("ok", outp)

if __name__ == "__main__":
    main()
