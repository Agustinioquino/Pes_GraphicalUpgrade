# PES21 FX (31/10) - vista previa de un mouth.fmdl (rasterizador por software: textura + luz simple), para comparar
# la boca de Konami con la 3D. Uso: python3 preview_mouth.py <salida.png> <bsm (dds/ftex)> <fmdl> [<fmdl> ...]
import sys, os, io
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', '..')]
import fmdl

def load_tex(path):
    from PIL import Image
    if path.endswith('.ftex'):
        import make_oral; a = make_oral.load(path)
    else:
        im = Image.open(path); im.load(); a = np.asarray(im.convert('RGBA'), np.float32) / 255
    return a[..., :3]

def render(P, F, UV, N, tex, yaw, pitch, W=900, H=600, center=(0, 1.632, 0.14), dist=0.11, fov=40, light=(0.3, 0.5, 1.0)):
    c = np.array(center)
    cy, sy, cp, spt = np.cos(yaw), np.sin(yaw), np.cos(pitch), np.sin(pitch)
    eye = c + dist * np.array([sy * cp, spt, cy * cp])
    fwd = (c - eye); fwd /= np.linalg.norm(fwd)
    right = np.cross(fwd, [0, 1, 0]); right /= np.linalg.norm(right); up = np.cross(right, fwd)
    Q = P - eye
    x, y, z = Q @ right, Q @ up, Q @ fwd
    f = 0.5 * H / np.tan(np.radians(fov) / 2)
    sx = W / 2 + f * x / z; sy_ = H / 2 - f * y / z
    img = np.zeros((H, W, 3), np.float32) + 0.05; zb = np.full((H, W), np.inf)
    L = np.array(light, float); L /= np.linalg.norm(L)
    th, tw = tex.shape[:2]
    for t in F:
        xs, ys, zs = sx[t], sy_[t], z[t]
        if (zs <= 1e-4).any(): continue
        x0, x1 = int(max(np.floor(xs.min()), 0)), int(min(np.ceil(xs.max()), W - 1))
        y0, y1 = int(max(np.floor(ys.min()), 0)), int(min(np.ceil(ys.max()), H - 1))
        if x1 < x0 or y1 < y0: continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
        if abs(d) < 1e-9: continue
        l1 = ((gx - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (gy - ys[0])) / d
        l2 = ((xs[1] - xs[0]) * (gy - ys[0]) - (gx - xs[0]) * (ys[1] - ys[0])) / d
        l0 = 1 - l1 - l2
        m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        if not m.any(): continue
        zz = l0 * zs[0] + l1 * zs[1] + l2 * zs[2]
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        m &= zz < sub
        if not m.any(): continue
        # perspectiva correcta
        w0, w1, w2 = l0 / zs[0], l1 / zs[1], l2 / zs[2]; ws = w0 + w1 + w2
        uv = (UV[t[0]] * (w0 / ws)[..., None] + UV[t[1]] * (w1 / ws)[..., None] + UV[t[2]] * (w2 / ws)[..., None])
        nn = (N[t[0]] * (w0 / ws)[..., None] + N[t[1]] * (w1 / ws)[..., None] + N[t[2]] * (w2 / ws)[..., None])
        nn /= np.maximum(np.linalg.norm(nn, axis=-1, keepdims=True), 1e-9)
        tx = np.clip((uv[..., 0] * tw).astype(int), 0, tw - 1); ty = np.clip((uv[..., 1] * th).astype(int), 0, th - 1)
        col = tex[ty, tx] ** 2.2
        vdir = -fwd
        dif = np.abs(nn @ L) * 0.85 + 0.15
        hv = L + vdir; hv /= np.linalg.norm(hv)
        spec = np.abs(nn @ hv) ** 40 * 0.25
        out = (col * dif[..., None] + spec[..., None]) ** (1 / 2.2)
        sub[m] = zz[m]
        img[y0:y1 + 1, x0:x1 + 1][m] = out[m]
    return np.clip(img, 0, 1)

def main():
    from PIL import Image
    out, texp = sys.argv[1], sys.argv[2]
    tex = load_tex(texp)
    rows = []
    for path in sys.argv[3:]:
        f = fmdl.Fmdl(path); m = fmdl.meshes(f)[0]
        P, F, UV = m['pos'], m['faces'], m['uv']; N = m['nrm'][:, :3]
        views = [render(P, F, UV, N, tex, 0.0, 0.05), render(P, F, UV, N, tex, 0.6, 0.15), render(P, F, UV, N, tex, 0.0, 0.9)]
        rows.append(np.concatenate(views, 1))
    Image.fromarray((np.concatenate(rows, 0) * 255).astype(np.uint8)).save(out)

if __name__ == '__main__':
    main()
