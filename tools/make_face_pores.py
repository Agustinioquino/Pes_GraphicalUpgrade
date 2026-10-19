# PES21 FX - textures\pes_face_pores.dds (1024, UV de la cara de PES 2021, mismo layout que pes_face_zones.dds):
#   R = densidad de poros (0 parpados/labios .. 1 nariz/frente),  G = tamano de poro (0 chico .. 1 grande: nariz, barbilla),
#   B = sesgo de rugosidad (0 = mate como mejilla, 1 = zona T brillante),  A = peso de la oclusion de poro
import numpy as np, struct, sys, os
from scipy.ndimage import gaussian_filter
N = 1024
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32) / N
def ell(cx, cy, rx, ry, soft=0.03):
    d = np.sqrt(((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2)
    return np.clip(1 - (d - 1) / soft, 0, 1) * (d < 1 + soft)
def band(cy, h, soft=0.05): return np.clip(1 - (np.abs(yy - cy) - h) / soft, 0, 1)
# zonas (coordenadas tomadas del mapa de zonas: frente 0.5,0.27 / sienes 0.3-0.7,0.33 / nariz 0.5,0.46 / mejillas 0.33|0.67,0.49 / labio sup 0.5,0.68 / barbilla 0.5,0.87)
forehead = ell(0.5, 0.27, 0.19, 0.08)
temples  = np.maximum(ell(0.30, 0.33, 0.06, 0.05), ell(0.70, 0.33, 0.06, 0.05))
nose     = np.maximum(ell(0.5, 0.45, 0.035, 0.09), ell(0.5, 0.55, 0.075, 0.035))   # dorso + punta/alas
cheeks   = np.maximum(ell(0.34, 0.50, 0.075, 0.07), ell(0.66, 0.50, 0.075, 0.07))
uplip    = ell(0.5, 0.68, 0.075, 0.03)
chin     = ell(0.5, 0.86, 0.11, 0.065)
eyes     = np.maximum(ell(0.36, 0.37, 0.075, 0.04), ell(0.64, 0.37, 0.075, 0.04))   # parpados: sin poros
lips     = ell(0.5, 0.60, 0.065, 0.022)
base = 0.45 * np.ones((N, N), np.float32)                                            # resto de la cara: poros finos
dens = base + 0.55 * forehead + 0.35 * temples + 0.6 * nose + 0.25 * cheeks + 0.3 * uplip + 0.4 * chin
dens = np.clip(dens, 0, 1) * (1 - 0.9 * eyes) * (1 - 0.95 * lips)
size = 0.25 + 0.7 * nose + 0.45 * chin + 0.25 * cheeks + 0.15 * forehead
size = np.clip(size, 0, 1)
tzone = np.clip(0.9 * forehead + nose + 0.5 * chin + 0.3 * temples, 0, 1)
ao = np.clip(dens * (0.6 + 0.4 * size), 0, 1)
# variacion organica: manchas de densidad (la piel real no es uniforme)
rng = np.random.default_rng(5)
blot = gaussian_filter(rng.random((64, 64)).astype(np.float32), 1.5, mode='wrap')
blot = np.kron(blot, np.ones((16, 16), np.float32)); blot = gaussian_filter(blot, 8); blot = (blot - blot.mean()) / (blot.std() + 1e-6)
dens = np.clip(dens * (1 + 0.18 * blot), 0, 1)
img = np.stack([dens, size, tzone, ao], -1)
img = gaussian_filter(img, (6, 6, 0))
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
write_dds(os.path.join(out, 'pes_face_pores.dds'), img)
from PIL import Image
Image.fromarray((img[..., :3] * 255).astype(np.uint8)).resize((512, 512)).save(os.path.join(out, 'pes_face_pores_preview.png'))
print('ok')
