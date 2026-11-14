# PES21 FX - textures\pes_skin_micro_n.dds: micro-relieve de piel en mosaico (1024 px = 22 mm de piel, periodico), como la piel
# real de cerca: red primaria de surcos finos (~1 mm entre surcos, islas casi planas), red secundaria mas fina adentro, arrugas
# finas, y poros escasos y de tamano variado (0.1..0.3 mm) en los cruces. RGB normal tangente, A = poros (fuerte) + surcos (suave).
# (31/10) v2: surcos angostos (0.1 mm), islas planas (antes abombadas = 'empedrado / grietas de barro'), 350 poros por mosaico
# (antes 900, demasiado densos) con radio variado. Los poros grandes de la nariz salen del mismo mosaico a media escala (shader).
import numpy as np, struct, sys, os
from scipy.spatial import cKDTree
from scipy.ndimage import gaussian_filter
N = 1024; rng = np.random.default_rng(11)
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
# deformacion del dominio (ruido suave periodico) para que las islas no sean poligonos regulares
def pnoise(cells, seed, sig):
    r = np.random.default_rng(seed); g = r.random((cells, cells)).astype(np.float32)
    g = np.kron(g, np.ones((N // cells, N // cells), np.float32)); g = gaussian_filter(g, sig, mode='wrap')
    return (g - g.mean()) / (g.std() + 1e-6)
wx = pnoise(16, 21, 18) * 9.0; wy = pnoise(16, 22, 18) * 9.0
q = np.stack([((xx + wx) % N).ravel(), ((yy + wy) % N).ravel()], 1)
def voronoi(count, seed, k=3):
    r = np.random.default_rng(seed); pts = r.random((count, 2)) * N
    tiled = np.concatenate([pts + np.array([dx, dy]) * N for dx in (-1, 0, 1) for dy in (-1, 0, 1)])
    d, _ = cKDTree(tiled).query(q, k=k)
    return [d[:, i].reshape(N, N) for i in range(k)]
# --- islas primarias (red de la piel): bordes = surcos, interior abombado
d1, d2, d3 = voronoi(1100, 1)                   # ~0.65 mm entre surcos (mejilla real 0.4..0.8 mm)
edge = d2 - d1                                   # 0 en el surco
plateau = np.clip(edge / 6.0, 0, 1); plateau = plateau * plateau * (3 - 2 * plateau)   # surco angosto y de perfil suave (sin bisel)
dome = 1.0 - 0.12 * np.clip(d1 / 20.0, 0, 1) ** 1.5   # islas casi planas (apenas abombadas)
h = plateau * dome
h = gaussian_filter(h, 1.6, mode='wrap')
# bultos grandes de baja frecuencia (la piel no es plana)
h += 0.18 * pnoise(32, 31, 6)
# --- sub-islas (mas finas) solo dentro de las islas
s1, s2, _ = voronoi(5000, 2)
sub = np.clip((s2 - s1) / 2.0, 0, 1); sub = sub * sub * (3 - 2 * sub)
h = h + 0.10 * plateau * gaussian_filter(sub, 1.0, mode='wrap')
# --- arrugas finas: lineas direccionales (dos direcciones) de muy baja amplitud
def lines(angle, freq, seed):
    r = np.random.default_rng(seed)
    g = r.random((N // 8, N // 8)).astype(np.float32); g = np.kron(g, np.ones((8, 8), np.float32))
    g = gaussian_filter(g, (6.0, 0.8), mode='wrap')      # alargado en x
    g = (g - g.mean()) / (g.std() + 1e-6)
    # rotar por remuestreo periodico
    c, s = np.cos(angle), np.sin(angle)
    xr = (xx * c - yy * s) % N; yr = (xx * s + yy * c) % N
    return g[yr.astype(int), xr.astype(int)]
h += 0.035 * lines(0.6, 1, 3) + 0.03 * lines(-0.9, 1, 4)
# --- poros: pocos, en cruces de surcos (d3 - d1 chico) elegidos al azar
junction = (d3 - d1) < 4.0
cand = np.argwhere(junction); cand = cand[rng.permutation(len(cand))]
sel = []                                          # separacion minima entre poros (sin cadenas a lo largo del surco)
for c in cand:
    if all(((abs(c[0] - s[0]) + N // 2) % N - N // 2) ** 2 + ((abs(c[1] - s[1]) + N // 2) % N - N // 2) ** 2 > 22 * 22 for s in sel):
        sel.append(c)
        if len(sel) >= 320: break
pore = np.zeros((N, N), np.float32)
for (py, px) in sel:
    R = 12; rad = rng.uniform(2.0, 4.0) * (1.0 + 1.2 * rng.random() ** 3)   # 0.1..0.3 mm, pocos grandes
    ys = np.arange(py - R, py + R + 1) % N; xs = np.arange(px - R, px + R + 1) % N
    dy = (np.arange(-R, R + 1))[:, None]; dx = (np.arange(-R, R + 1))[None, :]
    g = np.exp(-(dx * dx + dy * dy) / (2 * rad * rad)); g = g * g * (3 - 2 * g)   # pozo con fondo y borde definido
    pore[np.ix_(ys, xs)] = np.maximum(pore[np.ix_(ys, xs)], g)
h -= 1.1 * pore
h = gaussian_filter(h, 0.7, mode='wrap')
# --- normal (la altura de surco a isla ~ 1 unidad; la escala la pone el shader)
s = 2.2
dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * s; dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * s
n = np.stack([-dx, -dy, np.ones_like(h)], -1); n /= np.linalg.norm(n, axis=-1, keepdims=True)
occ = np.clip((1.0 - plateau) * 0.3 + pore, 0, 1)     # poros (fuerte) + surcos (suave): oclusion y menos brillo
img = np.stack([n[..., 0] * 0.5 + 0.5, n[..., 1] * 0.5 + 0.5, n[..., 2] * 0.5 + 0.5, occ], -1)
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
Image.fromarray((img[..., :3] * 255).astype(np.uint8)[:512, :512]).save(os.path.join(out, 'pes_skin_micro_preview.png'))
print('ok')
