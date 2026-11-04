# PES21 FX (04/11) - micro relieve de la piel de la CARA por zonas (poros v4). Agustin: "no me convence el color ni la forma, ni el
# tamano de los micro poros; una piel real no tiene poros iguales en toda la piel, en los labios por ejemplo o cerca de los ojos".
# Antes: una sola foto de piel del cuerpo (surcos cruzados) repetida igual en toda la cara. Ahora tres micro relieves hechos para la
# cara, cada uno con su forma, que el shader mezcla por zona (pes_pores.hlsli PesTriPoresZ):
#   pes_skin_pores_round.dds  poros redondos/ovalados de tamanos variados (log-normal) sentados en la red poligonal de surcos de la
#                             piel (celdas de ~0.45 mm): nariz, mejillas, frente, barbilla (el shader los agranda / achica por zona)
#   pes_skin_crepe.dds        arruguitas finas cruzadas (dos familias a +-20 grados de la horizontal) sin poros visibles, algun
#                             poro minimo: parpados, debajo de los ojos, patas de gallo, cuello
#   pes_skin_lips.dds         estrias verticales de los labios (onduladas, de largo y hondura variables) + micro grietas horizontales
# Un mosaico = 20 mm de piel (PES_PORE_TILE_MM en pes_pores.hlsli), 1024 px (~0.02 mm por pixel). Todo se genera periodico (el
# mosaico no tiene costura). Formato igual al anterior: RGB = normal tangente (DirectX, verde hacia abajo), A = cavidad (0..1).
#   python tools/make_skin_micro_zones.py <carpeta de salida> [N]
import numpy as np, struct, sys, os
from scipy.spatial import cKDTree
from scipy.ndimage import gaussian_filter

out = sys.argv[1] if len(sys.argv) > 1 else '.'
N = int(sys.argv[2]) if len(sys.argv) > 2 else 1024
TILE_MM = 20.0
PX = N / TILE_MM                       # pixeles por mm
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)


def write_dds(path, img):
    mips = []; cur = img.astype(np.float32)
    while True:
        mips.append(cur)
        if cur.shape[0] <= 1: break
        cur = (cur[0::2, 0::2] + cur[1::2, 0::2] + cur[0::2, 1::2] + cur[1::2, 1::2]) * 0.25
        # la normal promediada NO se renormaliza: su largo < 1 es la varianza (Toksvig) que el shader convierte en rugosidad
    hdr = struct.pack('<4sI I I I I I I 11I', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8, img.shape[0], img.shape[1], img.shape[1] * 4, 0, len(mips), *([0] * 11))
    pf = struct.pack('<I I 4s I I I I I', 32, 0x41, b'\0\0\0\0', 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000)
    caps = struct.pack('<I I I I I', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr + pf + caps)
        for m in mips: f.write((np.clip(m, 0, 1) * 255 + 0.5).astype(np.uint8).tobytes())


def to_normal(h, cav):
    # h en pixeles de altura (negativo = hundido); pendientes periodicas
    sx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5
    sy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5
    nn = np.stack([-sx, -sy, np.ones_like(sx)], -1); nn /= np.linalg.norm(nn, axis=-1, keepdims=True)
    return np.concatenate([nn * 0.5 + 0.5, np.clip(cav, 0, 1)[..., None]], -1).astype(np.float32)


def band_noise(rng, f0_mm, bw, theta=None, aniso=1.0, ang_bw=0.25):
    """ruido periodico de banda angosta: frecuencia f0 (1/mm), ancho relativo bw; con theta, solo esa orientacion (rad)"""
    w = rng.standard_normal((N, N)).astype(np.float32)
    F = np.fft.fft2(w)
    fy = np.fft.fftfreq(N)[:, None] * PX; fx = np.fft.fftfreq(N)[None, :] * PX     # ciclos por mm
    fr = np.sqrt(fx * fx + fy * fy) + 1e-6
    H = np.exp(-((fr - f0_mm) / (bw * f0_mm)) ** 2)
    if theta is not None:
        a = np.arctan2(fy, fx)
        d = np.angle(np.exp(1j * (a - theta)))                 # direccion de la FRECUENCIA (perpendicular a las lineas)
        d = np.minimum(np.abs(d), np.pi - np.abs(d))
        H = H * np.exp(-(d / ang_bw) ** 2)
    r = np.real(np.fft.ifft2(F * H)).astype(np.float32)
    return r / (r.std() + 1e-6)


def line_dist(b, width_mm):
    """lineas finas de ancho parejo en los cruces por cero del ruido b: distancia ~ |b| / |grad b| (en mm)"""
    gx = (np.roll(b, -1, 1) - np.roll(b, 1, 1)) * 0.5 * PX; gy = (np.roll(b, -1, 0) - np.roll(b, 1, 0)) * 0.5 * PX
    d = np.abs(b) / (np.sqrt(gx * gx + gy * gy) + 1e-3)
    return np.exp(-(d / width_mm) ** 2)


def low_noise(rng, scale_mm):
    w = rng.standard_normal((N, N)).astype(np.float32)
    r = gaussian_filter(w, scale_mm * PX, mode='wrap')
    return (r - r.mean()) / (r.std() + 1e-6)


def voronoi_furrows(rng, cell_mm, width_mm):
    """red poligonal de surcos de la piel (periodica): distancia al borde de celda = F2 - F1"""
    n = int((TILE_MM / cell_mm) ** 2)
    p = rng.random((n, 2)) * N
    tiles = np.concatenate([p + np.array([dx, dy]) * N for dx in (-1, 0, 1) for dy in (-1, 0, 1)])
    tree = cKDTree(tiles)
    q = np.stack([yy.ravel(), xx.ravel()], -1)
    d, _ = tree.query(q, k=2)
    e = (d[:, 1] - d[:, 0]).reshape(N, N) * 0.5 / PX       # mm hasta el borde
    prof = np.exp(-(e / width_mm) ** 2)
    var = 0.55 + 0.45 * np.clip(low_noise(rng, cell_mm * 1.5) * 0.5 + 0.5, 0, 1)   # unos surcos mas hondos que otros
    return prof * var, tiles


def splat_pits(rng, pts, rad_mm, depth_px, aspect, angle, rim=0.12):
    h = np.zeros((N, N), np.float32); cav = np.zeros((N, N), np.float32)
    for (py, px), r, d, asp, ang in zip(pts, rad_mm, depth_px, aspect, angle):
        R = r * PX; W = int(R * 1.6 * asp) + 2
        oy, ox = py - int(py), px - int(px)
        dy = np.arange(-W, W + 1, dtype=np.float32)[:, None] - oy; dx = np.arange(-W, W + 1, dtype=np.float32)[None, :] - ox
        c, s = np.cos(ang), np.sin(ang)
        u = (dx * c + dy * s) / asp; v = -dx * s + dy * c
        rr = np.sqrt(u * u + v * v) / max(R, 0.5)
        pit = np.where(rr < 1.0, np.clip(1.0 - rr, 0.0, 1.0) ** 1.6, 0.0)                      # embudo: hondo en el centro, borde suave
        ring = rim * np.exp(-((rr - 1.05) / 0.25) ** 2)                       # labio apenas levantado alrededor del poro
        ys = (np.arange(int(py) - W, int(py) + W + 1)) % N; xs = (np.arange(int(px) - W, int(px) + W + 1)) % N
        blk = np.ix_(ys, xs)
        h[blk] = np.minimum(h[blk], -d * pit) + d * ring * (h[blk] > -1e-3)
        cav[blk] = np.maximum(cav[blk], pit)
    return h, cav


def poisson(rng, count, min_mm):
    pts = []; tries = 0; md2 = (min_mm * PX) ** 2
    grid = {}; cs = min_mm * PX
    while len(pts) < count and tries < count * 40:
        tries += 1; c = rng.random(2) * N
        gi = (int(c[0] // cs), int(c[1] // cs)); ok = True; G = int(np.ceil(N / cs))
        for a in (-1, 0, 1):
            for b in (-1, 0, 1):
                for p in grid.get(((gi[0] + a) % G, (gi[1] + b) % G), ()):
                    dy = abs(c[0] - p[0]); dx = abs(c[1] - p[1]); dy = min(dy, N - dy); dx = min(dx, N - dx)
                    if dy * dy + dx * dx < md2: ok = False; break
                if not ok: break
            if not ok: break
        if ok: pts.append(c); grid.setdefault(gi, []).append(c)
    return np.array(pts)


# ------------------------------------------------------------------ 1) poros redondos sobre la red de surcos
rng = np.random.default_rng(41)
fur, _ = voronoi_furrows(rng, 0.55, 0.018)
fur *= np.clip(low_noise(rng, 0.35) * 0.9 + 0.35, 0, 1)                # surcos cortados: la red no se ve como barro seco
h1 = -0.6 * fur                                                       # surcos: ~0.01 mm de hondo
fine = band_noise(rng, 4.0, 0.5) * 0.12                               # textura fina de la superficie (no lisa como plastico)
h1 += fine
pts = poisson(rng, int(0.85 * (TILE_MM / 0.55) ** 2), 0.42)
n = len(pts)
rad = np.exp(rng.normal(np.log(0.075), 0.35, n)).clip(0.035, 0.2)   # radio de la boca del poro (mm): 0.07..0.4 mm de diametro
dep = (rad * PX) * rng.uniform(0.5, 0.9, n)                           # hondura ~ ancho (embudo)
asp = rng.uniform(1.0, 1.45, n); ang = rng.uniform(0, np.pi, n)
hp, cp = splat_pits(rng, pts, rad, dep, asp, ang)
h1 = np.minimum(h1, hp) + 0.0
cav1 = np.clip(cp * 0.95 + fur * 0.12, 0, 1)
img1 = to_normal(h1, cav1)
write_dds(os.path.join(out, 'pes_skin_pores_round.dds'), img1)
print('poros redondos:', n)

# ------------------------------------------------------------------ 2) arruguitas cruzadas (parpados, ojeras, cuello)
rng = np.random.default_rng(42)
c = np.zeros((N, N), np.float32)
for th, k in ((np.deg2rad(90 + 18), 1.0), (np.deg2rad(90 - 24), 0.8), (np.deg2rad(90 + 60), 0.35)):
    b = band_noise(rng, 1.7, 0.6, theta=th, ang_bw=0.35)              # lineas cada ~0.6 mm, onduladas
    seg = np.clip(low_noise(rng, 0.6) * 1.1 + 0.1, 0, 1)              # tramos: las arrugas cortan y siguen
    c = np.maximum(c, line_dist(b, 0.022) * seg * k)
h2 = -1.3 * c + band_noise(rng, 6.0, 0.5) * 0.08
pts2 = poisson(rng, int(0.25 * (TILE_MM / 0.6) ** 2), 0.5)
n2 = len(pts2)
hp2, cp2 = splat_pits(rng, pts2, np.full(n2, 0.025) * rng.uniform(0.8, 1.3, n2), np.full(n2, 0.025 * PX * 0.5), np.ones(n2), np.zeros(n2), rim=0.0)
h2 = np.minimum(h2, hp2)
cav2 = np.clip(c * 0.6 + cp2 * 0.6, 0, 1)
write_dds(os.path.join(out, 'pes_skin_crepe.dds'), to_normal(h2, cav2))
print('arruguitas: ok, poros minimos', n2)

# ------------------------------------------------------------------ 3) estrias verticales de los labios
rng = np.random.default_rng(43)
l = np.zeros((N, N), np.float32)
for f0, k in ((1.6, 1.0), (2.6, 0.55)):                                # surcos cada ~0.6 mm y otros mas finos
    b = band_noise(rng, f0, 0.3, theta=0.0, ang_bw=0.12)              # frecuencia horizontal = lineas VERTICALES
    seg = np.clip(low_noise(rng, 1.6) * 0.8 + 0.45, 0, 1)
    l = np.maximum(l, line_dist(b, 0.035 / k ** 0.5) * seg * k)
crack = line_dist(band_noise(rng, 1.2, 0.5, theta=np.pi / 2, ang_bw=0.4), 0.012) * np.clip(low_noise(rng, 0.7) * 0.8 - 0.1, 0, 1)
h3 = -1.6 * l - 0.5 * crack + band_noise(rng, 5.0, 0.5) * 0.05
cav3 = np.clip(l * 0.7 + crack * 0.3, 0, 1)
write_dds(os.path.join(out, 'pes_skin_lips.dds'), to_normal(h3, cav3))
print('labios: ok')

try:
    from PIL import Image
    prev = np.concatenate([np.concatenate([im[:384, :384, :3], np.repeat(im[:384, :384, 3:], 3, -1)], 0)
                           for im in (img1, to_normal(h2, cav2), to_normal(h3, cav3))], 1)
    Image.fromarray((prev * 255).astype(np.uint8)).save(os.path.join(out, 'pes_skin_micro_zones_preview.png'))
except Exception as e:
    print('sin vista previa:', e)
