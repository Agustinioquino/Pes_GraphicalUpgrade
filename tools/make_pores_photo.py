# PES21 FX - textures\pes_skin_micro_n.dds a partir de una piel FOTOGRAFICA tileable (31/10):
#   python tools/make_pores_photo.py <carpeta con skin_0001_normal_directx_2k.png y skin_0001_ao_2k.jpg> [salida] [tamano]
# Fuente: TextureCan "Human Face Skin Texture (Skin 0001)", CC0 (https://www.texturecan.com/details/574/): red de surcos
# cruzados de piel real (celdas de ~0.45 mm, 124 por mosaico). La foto no trae poros: se le suman pozos de poro procedurales
# (0.1..0.3 mm, separados) para que de cerca se vean los huecos. Un mosaico = 50 mm de piel (PES_PORE_TILE_MM en pes_pores.hlsli).
# RGB = normal tangente (DirectX, verde hacia abajo, como la del generador anterior); A = poros (fuerte) + surcos (suave).
import numpy as np, struct, sys, os
from PIL import Image
src = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else '.'; N = int(sys.argv[3]) if len(sys.argv) > 3 else 1024
TILE_MM = 50.0; pxmm = N / TILE_MM
def load(name, mode):
    im = Image.open(os.path.join(src, name)).convert(mode)
    if im.size[0] != N: im = im.resize((N, N), Image.LANCZOS)
    return np.asarray(im).astype(np.float32) / 255.0
n = load('skin_0001_normal_directx_2k.png', 'RGB') * 2.0 - 1.0
ao = load('skin_0001_ao_2k.jpg', 'L')
nz = np.maximum(n[..., 2], 0.2)
sx = -n[..., 0] / nz; sy = -n[..., 1] / nz          # pendientes de la foto (dh/dx, dh/dy en unidades de pixel)
# --- poros procedurales: pozos gaussianos con separacion minima, tamano 0.1..0.3 mm, pocos grandes
rng = np.random.default_rng(7)
count = int(700 * (TILE_MM * TILE_MM) / 2500.0)
pts = []; tries = 0
while len(pts) < count and tries < count * 60:
    tries += 1; c = rng.random(2) * N
    if all(min(abs(c[0] - p[0]), N - abs(c[0] - p[0])) ** 2 + min(abs(c[1] - p[1]), N - abs(c[1] - p[1])) ** 2 > (1.2 * pxmm) ** 2 for p in pts):
        pts.append(c)
pore = np.zeros((N, N), np.float32); hp = np.zeros((N, N), np.float32)
R = int(0.45 * pxmm) + 2
dy = (np.arange(-R, R + 1))[:, None].astype(np.float32); dx = (np.arange(-R, R + 1))[None, :].astype(np.float32)
for (py, px) in pts:
    rad = pxmm * rng.uniform(0.05, 0.12) * (1.0 + 1.5 * rng.random() ** 3)   # radio (sigma) en px
    oy, ox = py - int(py), px - int(px)
    g = np.exp(-((dx - ox) ** 2 + (dy - oy) ** 2) / (2 * rad * rad)); g = g * g * (3 - 2 * g)
    ys = (np.arange(int(py) - R, int(py) + R + 1)) % N; xs = (np.arange(int(px) - R, int(px) + R + 1)) % N
    pore[np.ix_(ys, xs)] = np.maximum(pore[np.ix_(ys, xs)], g)
depth = 0.9 * pxmm / 20.0            # profundidad del pozo (pendiente comparable a la de los surcos de la foto)
hpore = -depth * pore
gx = (np.roll(hpore, -1, 1) - np.roll(hpore, 1, 1)) * 0.5; gy = (np.roll(hpore, -1, 0) - np.roll(hpore, 1, 0)) * 0.5
sx = sx + gx; sy = sy + gy
nn = np.stack([-sx, -sy, np.ones_like(sx)], -1); nn /= np.linalg.norm(nn, axis=-1, keepdims=True)
furrow = np.clip((1.0 - ao) / 0.4, 0, 1)
occ = np.clip(furrow * 0.3 + pore, 0, 1)
img = np.stack([nn[..., 0] * 0.5 + 0.5, nn[..., 1] * 0.5 + 0.5, nn[..., 2] * 0.5 + 0.5, occ], -1)
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
write_dds(os.path.join(out, 'pes_skin_micro_n.dds'), img)
Image.fromarray((img[..., :3] * 255).astype(np.uint8)[:512, :512]).save(os.path.join(out, 'pes_skin_micro_preview.png'))
print('ok', len(pts), 'poros')
