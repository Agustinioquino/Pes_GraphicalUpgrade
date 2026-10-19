# PES21 FX - textures\pes_skin_micro_n.dds: micro normal de piel en mosaico (1024, periodica): poros de dos tamanos,
# lineas finas de la piel (red de celdas) y rugosidad fina. RGB normal tangente, A = mascara de poro (para brillo).
import numpy as np, struct, sys, os
N = 1024; rng = np.random.default_rng(3)
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
def crater_field(count, rmin, rmax, depth):
    h = np.zeros((N, N), np.float32)
    cx = rng.random(count) * N; cy = rng.random(count) * N; r = rng.uniform(rmin, rmax, count); d = depth * rng.uniform(0.5, 1.0, count)
    for i in range(count):
        R = int(r[i] * 3) + 1; x0 = int(cx[i]); y0 = int(cy[i])
        ys = (np.arange(y0 - R, y0 + R + 1) % N); xs = (np.arange(x0 - R, x0 + R + 1) % N)
        dy = (np.arange(y0 - R, y0 + R + 1) - cy[i])[:, None]; dx = (np.arange(x0 - R, x0 + R + 1) - cx[i])[None, :]
        g = np.exp(-(dx * dx + dy * dy) / (2 * (r[i] * 0.55) ** 2))
        h[np.ix_(ys, xs)] -= d[i] * g
    return h
# poros grandes (zona T) y chicos
h = crater_field(2600, 3.0, 6.5, 1.0) + crater_field(14000, 1.2, 2.6, 0.45)
# red de celdas: distancia al borde de Voronoi periodico (lineas finas hundidas)
pts = rng.random((900, 2)) * N
from scipy.spatial import cKDTree
tiled = np.concatenate([pts + np.array([dx, dy]) * N for dx in (-1, 0, 1) for dy in (-1, 0, 1)])
tree = cKDTree(tiled)
q = np.stack([xx.ravel(), yy.ravel()], 1)
d, _ = tree.query(q, k=2)
edge = (d[:, 1] - d[:, 0]).reshape(N, N)
lines = np.exp(-(edge / 1.3) ** 2)
h -= 0.25 * lines
# rugosidad fina
fine = rng.random((N // 4, N // 4)).astype(np.float32)
fine = np.kron(fine, np.ones((4, 4), np.float32))
from scipy.ndimage import gaussian_filter
fine = gaussian_filter(fine, 1.2, mode='wrap'); fine = (fine - fine.mean()) / (fine.std() + 1e-6)
h += 0.06 * fine
h = gaussian_filter(h, 0.6, mode='wrap')
# normal
s = 1.6
dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * s; dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * s
n = np.stack([-dx, -dy, np.ones_like(h)], -1); n /= np.linalg.norm(n, axis=-1, keepdims=True)
pore = np.clip(-h / 0.8, 0, 1)
img = np.stack([n[..., 0] * 0.5 + 0.5, n[..., 1] * 0.5 + 0.5, n[..., 2] * 0.5 + 0.5, pore], -1)
def write_dds(path, img):
    mips = []; cur = img.astype(np.float32)
    while True:
        mips.append(cur)
        if cur.shape[0] <= 1: break
        cur = (cur[0::2, 0::2] + cur[1::2, 0::2] + cur[0::2, 1::2] + cur[1::2, 1::2]) * 0.25
    hdr = struct.pack('<4sI I I I I I I 11I', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8, img.shape[0], img.shape[1], img.shape[1] * 4, 0, len(mips), *([0] * 11))
    pf = struct.pack('<I I 4s I I I I I', 32, 0x41, b'\0\0\0\0', 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000)
    caps = struct.pack('<I I I I I', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr + pf + caps)
        for m in mips: f.write((np.clip(m, 0, 1) * 255 + 0.5).astype(np.uint8).tobytes())
out = sys.argv[1] if len(sys.argv) > 1 else '.'
write_dds(os.path.join(out, 'pes_skin_micro_n.dds'), img)
from PIL import Image
Image.fromarray((img[..., :3] * 255).astype(np.uint8)[:256, :256]).resize((512, 512)).save(os.path.join(out, 'pes_skin_micro_preview.png'))
print('ok')
