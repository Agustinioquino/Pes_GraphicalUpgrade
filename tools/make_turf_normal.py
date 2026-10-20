# PES21 FX - textures\pes_turf_n.dds: normal map en mosaico (1024, periodico) del cesped visto desde arriba, para la base
# de la cancha (reemplaza el relieve de 512 px del juego). Matas de hojas alargadas con orientacion aleatoria (siguiendo
# el peinado), apiladas en varias capas, con ondulacion de baja frecuencia (el suelo no es plano) y pisadas suaves.
# RGB = normal tangente (x derecha, y arriba de la textura, z afuera), A = altura (0 hueco .. 1 punta) para oclusion.
import numpy as np, struct, sys, os
from scipy.ndimage import gaussian_filter

N = 1024; rng = np.random.default_rng(5)
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
h = np.zeros((N, N), np.float32)

def pnoise(cells, seed, sig):
    r = np.random.default_rng(seed); g = r.random((cells, cells)).astype(np.float32)
    g = np.kron(g, np.ones((N // cells, N // cells), np.float32)); g = gaussian_filter(g, sig, mode='wrap')
    return (g - g.mean()) / (g.std() + 1e-6)

# 1) suelo: ondulacion suave + grumos de tierra
h += 0.35 * pnoise(8, 1, 40) + 0.12 * pnoise(32, 2, 10)
# 2) hojas: segmentos alargados (longitud 12-40 px, ancho 2-4 px) en 3 capas, cada capa mas alta
def blades(count, lmin, lmax, wmin, wmax, amp, seed, dirbias):
    r = np.random.default_rng(seed); acc = np.zeros((N, N), np.float32)
    cx = r.random(count) * N; cy = r.random(count) * N
    ang = r.normal(dirbias, 0.9, count); L = r.uniform(lmin, lmax, count); W = r.uniform(wmin, wmax, count)
    for i in range(count):
        dx, dy = np.cos(ang[i]), np.sin(ang[i])
        x0, y0 = int(cx[i] - L[i]) , int(cy[i] - L[i]); x1, y1 = int(cx[i] + L[i]) + 2, int(cy[i] + L[i]) + 2
        sx = xx[0:y1 - y0, 0:x1 - x0] + x0; sy = yy[0:y1 - y0, 0:x1 - x0] + y0
        px, py = sx - cx[i], sy - cy[i]
        t = (px * dx + py * dy) / L[i]            # -1..1 a lo largo
        s = (-px * dy + py * dx)                  # px a lo ancho
        w = W[i] * (1.0 - 0.7 * np.clip(t, 0, 1) ** 1.5) * (0.5 + 0.5 * np.clip(1 + t, 0, 1))   # afinada en la punta
        prof = np.clip(1 - (s / np.maximum(w, 0.3)) ** 2, 0, 1) * np.clip(1 - t * t, 0, 1)
        prof = np.sqrt(prof)
        # pegar con wrap
        ys = (np.arange(y0, y1) % N); xs = (np.arange(x0, x1) % N)
        sub = acc[np.ix_(ys, xs)]
        acc[np.ix_(ys, xs)] = np.maximum(sub, prof * amp)
    return acc
l1 = blades(9000, 10, 26, 1.6, 2.6, 0.55, 11, 0.6)
l2 = blades(7000, 14, 36, 1.8, 3.0, 0.75, 12, 0.4)
l3 = blades(3500, 18, 44, 2.0, 3.4, 1.00, 13, 0.5)
# apilado: las capas de arriba tapan a las de abajo (max), con un poco de suma para densidad
h = h * 0.25 + np.maximum(np.maximum(l1, l2 * 1.05), l3 * 1.1) + 0.15 * (l1 + l2)
# 3) pisadas / zonas apretadas: depresiones suaves y escasas
foot = pnoise(16, 21, 14); h -= 0.25 * np.clip(foot - 1.1, 0, 1)
h = gaussian_filter(h, 0.7, mode='wrap')
h = (h - h.min()) / (h.max() - h.min() + 1e-6)
# normal a partir de la altura (escala vertical: 1 px ~ 4 mm, altura ~ 4 cm -> factor 10)
K = 9.0
gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * K
gy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * K
n = np.stack([-gx, -gy, np.ones_like(h)], -1); n /= np.linalg.norm(n, axis=-1, keepdims=True)
img = np.stack([n[..., 0] * 0.5 + 0.5, n[..., 1] * 0.5 + 0.5, n[..., 2] * 0.5 + 0.5, h], -1)

def write_dds(path, img):
    mips = []; cur = img.astype(np.float32)
    while True:
        mips.append(cur)
        if cur.shape[0] <= 1: break
        cur = (cur[0::2, 0::2] + cur[1::2, 0::2] + cur[0::2, 1::2] + cur[1::2, 1::2]) * 0.25
    H, W = img.shape[:2]
    hdr = struct.pack('<4sI I I I I I I 11I', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8, H, W, W * 4, 0, len(mips), *([0] * 11))
    pf = struct.pack('<I I 4s I I I I I', 32, 0x41, b'\0\0\0\0', 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000)
    caps = struct.pack('<I I I I I', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr + pf + caps)
        for m in mips: f.write((np.clip(m, 0, 1) * 255 + 0.5).astype(np.uint8).tobytes())
out = sys.argv[1] if len(sys.argv) > 1 else '.'
write_dds(os.path.join(out, 'pes_turf_n.dds'), img)
from PIL import Image
Image.fromarray((img[..., :3] * 255).astype(np.uint8)[:512, :512]).save(os.path.join(out, 'pes_turf_n_preview.png'))
Image.fromarray((h * 255).astype(np.uint8)[:512, :512]).save(os.path.join(out, 'pes_turf_h_preview.png'))
print('ok')
