# PES21 FX - guias de poros por zona en la UV de la cara de PES 2021 (1024, mismo layout que pes_face_zones.dds y que las texturas
# de cara del juego). (04/11 poros v5) posiciones medidas sobre la textura de cara real de Messi (mallas_messi/textura): nacimiento
# del pelo v 0.19, frente v 0.20..0.29, cejas v 0.29..0.32, ojos v 0.36, punta de la nariz v 0.50, boca v 0.62, menton v 0.74,
# borde de la mandibula v 0.83 en el centro (sube hacia los costados, 0.58 debajo de la oreja), cuello debajo. En v4 el "menton"
# estaba en 0.86 (era el cuello) y la frente se metia en las cejas.
#   textures\pes_face_pores.dds  R densidad de poros (0 parpados/labios .. 1 nariz/frente)
#                                G tamano: 0 = poros de mejilla (pes_skin_pores_round), 1 = poros grandes de nariz y menton
#                                  (pes_skin_pores_large); el tamano de cada poro es el real de esa zona, el shader no escala
#                                B sesgo de rugosidad (0 = mate como mejilla, 1 = zona T brillante)
#                                A peso de las ARRUGUITAS cruzadas (parpados, ojeras, patas de gallo: pes_skin_crepe)
#   textures\pes_face_pores2.dds R peso de la FRENTE (pes_skin_forehead: poros finos densos, surcos horizontales)
#                                G sobre las cejas y entrecejo (piel mas gruesa: frente + poros mas grandes)
#                                B peso del CUELLO (pes_skin_neck: pliegues horizontales, cuadriculado, foliculos de barba)
#                                A = 1 - LABIOS (04/11 v6): bermellon de arriba y de abajo medidos sobre la textura de Messi (arriba
#                                  v 0.584..0.616, abajo 0.626..0.656, u 0.435..0.570); la G de pes_face_zones solo tapaba el de
#                                  arriba y el de abajo quedaba sin relieve. Se guarda invertido: la textura vieja (A = 1) = sin labios
import numpy as np, struct, sys, os
from scipy.ndimage import gaussian_filter
N = 1024
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32) / N
def ell(cx, cy, rx, ry, soft=0.03):
    d = np.sqrt(((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2)
    return np.clip(1 - (d - 1) / soft, 0, 1) * (d < 1 + soft)
def both(cx, cy, rx, ry, soft=0.03): return np.maximum(ell(cx, cy, rx, ry, soft), ell(1 - cx, cy, rx, ry, soft))
def sstep(a, b, x): t = np.clip((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)

forehead = ell(0.5, 0.235, 0.21, 0.07, 0.05)
temples  = both(0.27, 0.30, 0.05, 0.06, 0.05)
brow     = np.maximum(both(0.39, 0.283, 0.085, 0.022, 0.04), ell(0.5, 0.31, 0.04, 0.03, 0.04))   # sobre las cejas + entrecejo
nose     = np.maximum(ell(0.5, 0.42, 0.032, 0.07, 0.04), ell(0.5, 0.50, 0.075, 0.035, 0.04))   # dorso + punta/alas
cheeks   = both(0.35, 0.47, 0.08, 0.07, 0.05)
inner_ck = both(0.42, 0.50, 0.04, 0.035, 0.05)                                                   # mejilla junto a la nariz
uplip    = ell(0.5, 0.565, 0.075, 0.025, 0.04)
chin     = ell(0.5, 0.74, 0.09, 0.05, 0.05)
lids     = both(0.37, 0.355, 0.085, 0.035, 0.05)
under    = both(0.37, 0.39, 0.075, 0.025, 0.05)
crow     = both(0.265, 0.36, 0.035, 0.045, 0.05)
lips     = ell(0.5, 0.62, 0.07, 0.025, 0.03)
# cuello: debajo del borde de la mandibula (curva medida: 0.83 al centro, 0.78 a 0.2 del centro, 0.68 a 0.3, 0.55 a 0.38)
dx = np.abs(xx - 0.5)
jawV = 0.83 - 1.1 * dx ** 2 - 6.0 * dx ** 4
neck = sstep(jawV, jawV + 0.045, yy) * (1 - sstep(0.43, 0.47, dx)) * sstep(0.5, 0.56, yy)   # los costados de atras (nuca) quedan afuera

base = 0.5 * np.ones((N, N), np.float32)
dens = base + 0.5 * forehead + 0.3 * temples + 0.5 * nose + 0.25 * cheeks + 0.35 * inner_ck + 0.3 * uplip + 0.4 * chin + 0.3 * brow
dens = np.clip(dens, 0, 1) * (1 - 0.9 * lids) * (1 - 0.95 * lips)
size = np.clip(nose + 0.8 * chin + 0.6 * inner_ck + 0.25 * cheeks + 0.35 * brow, 0, 1)
size *= (1 - 0.8 * np.clip(under + crow + lids, 0, 1))
tzone = np.clip(0.9 * forehead + nose + 0.5 * chin + 0.3 * temples + 0.6 * brow, 0, 1)
crepe = np.clip(np.maximum(np.maximum(lids, 0.85 * under), 0.75 * crow), 0, 1)

# variacion organica de la densidad (la piel real no es uniforme)
rng = np.random.default_rng(5)
blot = gaussian_filter(rng.random((64, 64)).astype(np.float32), 1.5, mode='wrap')
blot = np.kron(blot, np.ones((16, 16), np.float32)); blot = gaussian_filter(blot, 8); blot = (blot - blot.mean()) / (blot.std() + 1e-6)
dens = np.clip(dens * (1 + 0.15 * blot), 0, 1)

fh = np.clip(np.maximum(forehead, 0.6 * temples) * (1 - crepe), 0, 1)
br = np.clip(brow * (1 - crepe), 0, 1)
nk = np.clip(neck * (1 - chin), 0, 1)
lipM = np.maximum(ell(0.502, 0.6015, 0.068, 0.0172, 0.12), ell(0.502, 0.6405, 0.064, 0.0165, 0.12))
lipM = np.clip(lipM * (1 - 0.85 * np.exp(-((yy - 0.621) / 0.0035) ** 2)), 0, 1)   # la comisura / linea de la boca afuera
img1 = gaussian_filter(np.stack([dens, size, tzone, crepe], -1), (6, 6, 0))
img2 = np.concatenate([gaussian_filter(np.stack([fh, br, nk], -1), (6, 6, 0)), gaussian_filter(1 - lipM, 3)[..., None]], -1)

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
write_dds(os.path.join(out, 'pes_face_pores.dds'), img1)
write_dds(os.path.join(out, 'pes_face_pores2.dds'), img2)
try:
    from PIL import Image
    face = sys.argv[2] if len(sys.argv) > 2 else None
    # vista previa: R = poros grandes (G), G = frente + cejas, B = cuello; blanco = arruguitas; sobre la cara si se pasa una
    col = np.stack([img1[..., 1] * (1 - img1[..., 3]), np.clip(img2[..., 0] + img2[..., 1], 0, 1), img2[..., 2]], -1)
    col = np.clip(col + img1[..., 3:4], 0, 1)
    if face:
        f = np.asarray(Image.open(face).convert('RGB').resize((N, N))).astype(np.float32) / 255
        col = f * 0.55 + col * 0.45
    Image.fromarray((col * 255).astype(np.uint8)).resize((512, 512)).save(os.path.join(out, 'pes_face_pores_preview.png'))
except Exception as e:
    print('sin vista previa:', e)
print('ok')
