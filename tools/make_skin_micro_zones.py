# PES21 FX (04/11) - micro relieve de la piel de la CARA por zonas (poros v5).
# v4 (Agustin: "no me convence el color ni la forma, ni el tamano de los micro poros; una piel real no tiene poros iguales en toda
# la piel, en los labios por ejemplo o cerca de los ojos"): tres relieves por zona.
# v5 (Agustin: "haz estos normales de mayor calidad con diferencias un sombreado suave o duro, ademas ten en cuenta los diferentes
# poros de la piel, cerca de los ojos, sobre las cejas, en la frente y en el cuello"; "el tamano consideralo vos sabiendo como es el
# tamano de los poros en rostros reales, que el tamano no cambie con la distancia"):
#   - TAMANO REAL FIJO: un mosaico = 20 mm de piel siempre (el shader ya no escala con y94 ni por zona): cada zona tiene su propia
#     textura con sus poros del tamano medido en pieles reales (diametro de la boca del poro):
#       nariz / barbilla / centro de las mejillas  0.10 .. 0.40 mm (sebaceos, ~200 por cm2)       pes_skin_pores_large.dds
#       mejillas / sienes / resto de la cara       0.07 .. 0.25 mm (~110 por cm2) + foliculos de vello de 0.03..0.05 mm
#                                                                                                  pes_skin_pores_round.dds
#       frente / sobre las cejas                   0.05 .. 0.16 mm muy densos (~190 por cm2), red de surcos estirada en
#                                                  horizontal (lineas de tension de la frente) + lineas finas horizontales
#                                                                                                  pes_skin_forehead.dds
#       parpados / ojeras / patas de gallo         arruguitas cruzadas finas, foliculos minimos     pes_skin_crepe.dds
#       labios (piel de afuera)                    estrias verticales                               pes_skin_lips.dds
#       cuello                                     pliegues horizontales anchos y suaves + cuadriculado fino + poros chicos
#                                                  y foliculos de la barba                          pes_skin_neck.dds
#   - SOMBREADO DURO o SUAVE segun el rasgo: el poro es un cuenco de pared empinada y borde marcado (sombreado duro: la luz corta
#     de golpe en el borde); los surcos, las lineas de la frente y los pliegues del cuello son valles redondeados (sombreado suave)
#   - MAYOR CALIDAD: todo se calcula al doble de resolucion (2048 = 0.01 mm por pixel) y la normal se promedia a 1024 (sin
#     escalones en las paredes del poro, sin aliasing); los mips no se renormalizan (largo < 1 = varianza para la rugosidad, Toksvig)
# Formato: RGB = normal tangente (x = +columna, y = +fila de la imagen, como v4), A = cavidad (0..1: fondo del poro / surco) que el
# shader usa para la oclusion especular y la rugosidad del fondo. Todo periodico (el mosaico no tiene costura).
#   python tools/make_skin_micro_zones.py <carpeta de salida> [N]
import numpy as np, struct, sys, os
from scipy.spatial import cKDTree
from scipy.ndimage import gaussian_filter

out = sys.argv[1] if len(sys.argv) > 1 else '.'
NO = int(sys.argv[2]) if len(sys.argv) > 2 else 1024     # resolucion de salida
SS = 2                                                    # supermuestreo
N = NO * SS
TILE_MM = 20.0
PX = N / TILE_MM                                          # pixeles (de calculo) por mm
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)


def write_dds(path, img):
    mips = []; cur = img.astype(np.float32)
    while True:
        mips.append(cur)
        if cur.shape[0] <= 1: break
        # (v6) filtro gaussiano antes de bajar a la mitad (no solo caja 2x2): con la caja un poro de ~1 texel quedaba como un punto
        # blanco / negro suelto segun cayera en el borde del texel (granito, "baja calidad" a la distancia del editor)
        if cur.shape[0] >= 8: cur = gaussian_filter(cur, (0.75, 0.75, 0), mode='wrap')
        cur = (cur[0::2, 0::2] + cur[1::2, 0::2] + cur[0::2, 1::2] + cur[1::2, 1::2]) * 0.25
        # la normal promediada NO se renormaliza: su largo < 1 es la varianza (Toksvig) que el shader convierte en rugosidad
    hdr = struct.pack('<4sI I I I I I I 11I', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8, img.shape[0], img.shape[1], img.shape[1] * 4, 0, len(mips), *([0] * 11))
    pf = struct.pack('<I I 4s I I I I I', 32, 0x41, b'\0\0\0\0', 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000)
    caps = struct.pack('<I I I I I', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr + pf + caps)
        for m in mips: f.write((np.clip(m, 0, 1) * 255 + 0.5).astype(np.uint8).tobytes())


def to_normal(h_mm, cav):
    """h en mm (negativo = hundido). Pendiente con diferencias centrales de 4o orden al doble de resolucion, normal promediada a
    la resolucion de salida (cada texel guarda el promedio de 4 normales: el borde duro del poro queda antialiaseado)"""
    def d(a, ax):
        return (8.0 * (np.roll(a, -1, ax) - np.roll(a, 1, ax)) - (np.roll(a, -2, ax) - np.roll(a, 2, ax))) / 12.0 * PX
    sx = d(h_mm, 1); sy = d(h_mm, 0)
    nn = np.stack([-sx, -sy, np.ones_like(sx)], -1); nn /= np.linalg.norm(nn, axis=-1, keepdims=True)
    img = np.concatenate([nn * 0.5 + 0.5, np.clip(cav, 0, 1)[..., None]], -1).astype(np.float32)
    for _ in range(int(np.log2(SS))):
        img = (img[0::2, 0::2] + img[1::2, 0::2] + img[0::2, 1::2] + img[1::2, 1::2]) * 0.25
    return img


def band_noise(rng, f0_mm, bw, theta=None, ang_bw=0.25):
    """ruido periodico de banda angosta: frecuencia f0 (ciclos por mm), ancho relativo bw; con theta solo esa direccion de
    FRECUENCIA (rad; las lineas quedan perpendiculares a ella)"""
    w = rng.standard_normal((N, N)).astype(np.float32)
    F = np.fft.fft2(w)
    fy = np.fft.fftfreq(N)[:, None] * PX; fx = np.fft.fftfreq(N)[None, :] * PX
    fr = np.sqrt(fx * fx + fy * fy) + 1e-6
    H = np.exp(-((fr - f0_mm) / (bw * f0_mm)) ** 2)
    if theta is not None:
        a = np.arctan2(fy, fx)
        dd = np.angle(np.exp(1j * (a - theta)))
        dd = np.minimum(np.abs(dd), np.pi - np.abs(dd))
        H = H * np.exp(-(dd / ang_bw) ** 2)
    r = np.real(np.fft.ifft2(F * H)).astype(np.float32)
    return r / (r.std() + 1e-6)


def line_dist(b, width_mm, soft=False):
    """lineas de ancho parejo en los cruces por cero de b. Perfil duro (gauss angosto) o suave (valle redondeado ancho)"""
    gx = (np.roll(b, -1, 1) - np.roll(b, 1, 1)) * 0.5 * PX; gy = (np.roll(b, -1, 0) - np.roll(b, 1, 0)) * 0.5 * PX
    dmm = np.abs(b) / (np.sqrt(gx * gx + gy * gy) + 1e-3)
    if soft: return 1.0 / (1.0 + (dmm / width_mm) ** 2) ** 1.5     # valle redondeado con faldas largas: sombreado suave
    return np.exp(-(dmm / width_mm) ** 2)


def low_noise(rng, scale_mm):
    w = rng.standard_normal((N, N)).astype(np.float32)
    r = gaussian_filter(w, scale_mm * PX, mode='wrap')
    return (r - r.mean()) / (r.std() + 1e-6)


def voronoi_furrows(rng, cell_mm, width_mm, stretch=1.0):
    """red poligonal de surcos de la piel (periodica): distancia al borde de celda = (F2 - F1) / 2. stretch > 1 estira las
    celdas en horizontal (frente: lineas de tension). Perfil suave (valle redondeado)"""
    n = int((TILE_MM / cell_mm) ** 2)
    p = rng.random((n, 2)) * N
    tiles = np.concatenate([p + np.array([dy, dx]) * N for dx in (-1, 0, 1) for dy in (-1, 0, 1)])
    sc = np.array([stretch ** 0.5, 1.0 / stretch ** 0.5], np.float32)   # (y, x): x se comprime en la metrica = celdas anchas
    tree = cKDTree(tiles * sc)
    q = np.stack([yy.ravel(), xx.ravel()], -1) * sc
    d, _ = tree.query(q, k=2, workers=-1)
    e = (d[:, 1] - d[:, 0]).reshape(N, N).astype(np.float32) * 0.5 / PX
    prof = 1.0 / (1.0 + (e / width_mm) ** 2) ** 1.5
    var = 0.55 + 0.45 * np.clip(low_noise(rng, cell_mm * 1.5) * 0.5 + 0.5, 0, 1)
    return prof * var


def poisson(rng, count, min_mm):
    pts = []; tries = 0; cs = min_mm * PX; md2 = cs * cs
    grid = {}; G = int(np.ceil(N / cs))
    while len(pts) < count and tries < count * 40:
        tries += 1; c = rng.random(2) * N
        gi = (int(c[0] // cs), int(c[1] // cs)); ok = True
        for a in (-1, 0, 1):
            for b in (-1, 0, 1):
                for p in grid.get(((gi[0] + a) % G, (gi[1] + b) % G), ()):
                    dy = abs(c[0] - p[0]); dx = abs(c[1] - p[1]); dy = min(dy, N - dy); dx = min(dx, N - dx)
                    if dy * dy + dx * dx < md2: ok = False; break
                if not ok: break
            if not ok: break
        if ok: pts.append(c); grid.setdefault(gi, []).append(c)
    return np.array(pts).reshape(-1, 2)


def pits(rng, pts, rad_mm, depth_mm, aspect, angle, wall=3.0, collar=0.0, rim=0.0):
    """poros: cuenco de fondo redondeado y pared empinada (wall alto = borde duro); collar = embudo suave alrededor (poros
    sebaceos grandes de la nariz); rim = labio apenas levantado"""
    h = np.zeros((N, N), np.float32); cav = np.zeros((N, N), np.float32)
    for (py, px), r, dmm, asp, ang in zip(pts, rad_mm, depth_mm, aspect, angle):
        R = r * PX; W = int(R * (2.6 if collar > 0 else 1.6) * asp) + 3
        oy, ox = py - int(py), px - int(px)
        dy = np.arange(-W, W + 1, dtype=np.float32)[:, None] - oy; dx = np.arange(-W, W + 1, dtype=np.float32)[None, :] - ox
        c, s = np.cos(ang), np.sin(ang)
        u = (dx * c + dy * s) / asp; v = -dx * s + dy * c
        rr = np.sqrt(u * u + v * v) / max(R, 0.5)
        bowl = np.where(rr < 1.0, 1.0 - np.clip(rr, 0, 1) ** wall, 0.0)                 # duro: casi plano abajo, pared al borde
        fun = collar * np.exp(-((rr - 1.0) / 0.55) ** 2) * (rr >= 1.0) + collar * (rr < 1.0)   # embudo suave
        hh = -dmm * np.maximum(bowl, 0) - dmm * 0.35 * fun + dmm * rim * np.exp(-((rr - 1.12) / 0.2) ** 2)
        ys = (np.arange(int(py) - W, int(py) + W + 1)) % N; xs = (np.arange(int(px) - W, int(px) + W + 1)) % N
        blk = np.ix_(ys, xs)
        h[blk] = np.minimum(h[blk], hh)
        cav[blk] = np.maximum(cav[blk], np.clip(bowl, 0, 1) ** 0.5 + 0.35 * fun * (rr >= 1.0))
    return h, cav


def lognorm(rng, n, med_mm, sig, lo, hi):
    return np.exp(rng.normal(np.log(med_mm), sig, n)).clip(lo, hi)


def pore_set(rng, density_cm2, min_mm, med_r, sig, lo, hi, depth_k, asp_hi, wall, collar=0.0, rim=0.0):
    pts = poisson(rng, int(density_cm2 * TILE_MM * TILE_MM / 100.0), min_mm)
    n = len(pts)
    rad = lognorm(rng, n, med_r, sig, lo, hi)
    dep = rad * rng.uniform(depth_k[0], depth_k[1], n)
    asp = rng.uniform(1.0, asp_hi, n); ang = rng.uniform(0, np.pi, n)
    h, c = pits(rng, pts, rad, dep, asp, ang, wall, collar, rim)
    return h, c, n


def surface(rng, amp_mm, f=5.0):
    """textura fina de la superficie (la piel no es plastico): suave"""
    return band_noise(rng, f, 0.6) * amp_mm


results = {}

# ------------------------------------------------------------------ 1) mejillas / sienes / resto: poros medianos + foliculos
rng = np.random.default_rng(51)
fur = voronoi_furrows(rng, 0.5, 0.022)
fur *= np.clip(low_noise(rng, 0.35) * 0.9 + 0.4, 0, 1)                # surcos cortados (no barro seco)
meso = voronoi_furrows(rng, 1.0, 0.085) * np.clip(low_noise(rng, 0.9) * 0.5 + 0.75, 0, 1)   # (v6) red gruesa: se ve a 0.15 mm/px
h = -0.012 * fur - 0.036 * meso + surface(rng, 0.0012)
hp, cp, n1 = pore_set(rng, 110, 0.75, 0.06, 0.32, 0.035, 0.125, (0.6, 0.95), 1.4, wall=3.2, collar=0.22, rim=0.04)
hf, cf, n1f = pore_set(rng, 260, 0.38, 0.02, 0.2, 0.014, 0.03, (0.5, 0.8), 1.15, wall=2.2)
h = np.minimum(np.minimum(h, hp), hf)
cav = np.clip(np.maximum(cp, cf * 0.6) + fur * 0.15 + meso * 0.2, 0, 1)
results['pes_skin_pores_round.dds'] = to_normal(h, cav)
print('mejillas: poros', n1, 'foliculos', n1f)

# ------------------------------------------------------------------ 2) nariz / barbilla: poros sebaceos grandes, embudo suave
rng = np.random.default_rng(52)
fur = voronoi_furrows(rng, 0.7, 0.025) * np.clip(low_noise(rng, 0.5) * 0.8 + 0.3, 0, 1)
meso = voronoi_furrows(rng, 1.2, 0.08) * np.clip(low_noise(rng, 1.0) * 0.5 + 0.7, 0, 1)
h = -0.006 * fur - 0.016 * meso + surface(rng, 0.0008)                 # piel grasa y tensa: surcos flojos, mas lisa entre poros
hp, cp, n2 = pore_set(rng, 200, 0.5, 0.095, 0.4, 0.05, 0.2, (0.5, 0.85), 1.5, wall=3.6, collar=0.35)
hf, cf, n2f = pore_set(rng, 120, 0.45, 0.025, 0.2, 0.016, 0.035, (0.5, 0.8), 1.1, wall=2.2)
h = np.minimum(np.minimum(h, hp), hf)
cav = np.clip(np.maximum(cp, cf * 0.5) + fur * 0.1 + meso * 0.15, 0, 1)
results['pes_skin_pores_large.dds'] = to_normal(h, cav)
print('nariz: poros', n2)

# ------------------------------------------------------------------ 3) frente / sobre las cejas: poros finos densos, surcos horizontales
rng = np.random.default_rng(53)
fur = voronoi_furrows(rng, 0.45, 0.02, stretch=2.4) * np.clip(low_noise(rng, 0.4) * 0.9 + 0.4, 0, 1)
lines = np.zeros((N, N), np.float32)
for f0, k in ((0.9, 1.0), (1.5, 0.5)):                                 # lineas finas horizontales cada ~1.1 mm y ~0.7 mm
    b = band_noise(rng, f0, 0.35, theta=np.pi / 2, ang_bw=0.18)        # frecuencia vertical = lineas HORIZONTALES
    seg = np.clip(low_noise(rng, 1.2) * 0.9 + 0.35, 0, 1)
    lines = np.maximum(lines, line_dist(b, 0.03, soft=True) * seg * k)
meso = voronoi_furrows(rng, 1.0, 0.08, stretch=2.4) * np.clip(low_noise(rng, 1.0) * 0.5 + 0.75, 0, 1)
h = -0.010 * fur - 0.030 * meso - 0.010 * lines + surface(rng, 0.001)
hp, cp, n3 = pore_set(rng, 190, 0.55, 0.045, 0.3, 0.025, 0.08, (0.6, 0.95), 1.25, wall=3.0, rim=0.05)
h = np.minimum(h, hp)
cav = np.clip(cp + fur * 0.15 + meso * 0.2 + lines * 0.12, 0, 1)
results['pes_skin_forehead.dds'] = to_normal(h, cav)
print('frente: poros', n3)

# ------------------------------------------------------------------ 4) parpados / ojeras / patas de gallo: arruguitas cruzadas suaves
rng = np.random.default_rng(54)
c = np.zeros((N, N), np.float32)
for th, k in ((np.deg2rad(90 + 18), 1.0), (np.deg2rad(90 - 24), 0.8), (np.deg2rad(90 + 60), 0.35)):
    b = band_noise(rng, 2.0, 0.6, theta=th, ang_bw=0.35)              # lineas cada ~0.5 mm, onduladas
    seg = np.clip(low_noise(rng, 0.5) * 1.1 + 0.1, 0, 1)
    c = np.maximum(c, line_dist(b, 0.014, soft=True) * seg * k)
h = -0.012 * c + surface(rng, 0.0006, 7.0)
hf, cf, n4 = pore_set(rng, 120, 0.5, 0.015, 0.2, 0.01, 0.025, (0.4, 0.7), 1.0, wall=2.0)
h = np.minimum(h, hf)
cav = np.clip(c * 0.55 + cf * 0.5, 0, 1)
results['pes_skin_crepe.dds'] = to_normal(h, cav)
print('parpados: foliculos', n4)

# ------------------------------------------------------------------ 5) labios (piel de afuera): estrias verticales
rng = np.random.default_rng(55)
l = np.zeros((N, N), np.float32)
for f0, k in ((0.65, 1.0), (1.1, 0.5)):   # (v6) estrias cada ~0.8 mm y ~0.45 mm (2 cruces por cero por periodo)
    b = band_noise(rng, f0, 0.3, theta=0.0, ang_bw=0.12)              # frecuencia horizontal = lineas VERTICALES
    seg = np.clip(low_noise(rng, 1.6) * 0.35 + 0.8, 0, 1)            # (v6) parejas en todo el labio
    l = np.maximum(l, line_dist(b, 0.06 / k ** 0.5, soft=True) * seg * k)
crack = line_dist(band_noise(rng, 1.2, 0.5, theta=np.pi / 2, ang_bw=0.4), 0.01) * np.clip(low_noise(rng, 0.7) * 0.8 - 0.1, 0, 1)
h = -0.04 * l - 0.008 * crack + surface(rng, 0.0008)
cav = np.clip(l * 0.7 + crack * 0.3, 0, 1)
results['pes_skin_lips.dds'] = to_normal(h, cav)
print('labios: ok')

# ------------------------------------------------------------------ 6) cuello: pliegues horizontales suaves + cuadriculado fino
rng = np.random.default_rng(56)
fold = np.zeros((N, N), np.float32)
for f0, k in ((0.45, 1.0), (0.8, 0.45)):                               # pliegues cada ~2.2 mm y ~1.2 mm, ondulados, por tramos
    b = band_noise(rng, f0, 0.3, theta=np.pi / 2, ang_bw=0.15)
    seg = np.clip(low_noise(rng, 2.5) * 0.9 + 0.4, 0, 1)
    fold = np.maximum(fold, line_dist(b, 0.09, soft=True) * seg * k)
x = np.zeros((N, N), np.float32)
for th, k in ((np.deg2rad(90 + 35), 1.0), (np.deg2rad(90 - 35), 0.9)):   # cuadriculado de la piel del cuello
    b = band_noise(rng, 1.8, 0.6, theta=th, ang_bw=0.35)
    x = np.maximum(x, line_dist(b, 0.016, soft=True) * np.clip(low_noise(rng, 0.6) + 0.3, 0, 1) * k)
h = -0.02 * fold - 0.012 * x + surface(rng, 0.001)
hp, cp, n6 = pore_set(rng, 70, 0.8, 0.04, 0.3, 0.025, 0.07, (0.5, 0.8), 1.2, wall=2.8)
hb, cb, n6b = pore_set(rng, 40, 1.0, 0.035, 0.15, 0.028, 0.05, (0.8, 1.1), 1.0, wall=3.5)   # foliculos de la barba
h = np.minimum(np.minimum(h, hp), hb)
cav = np.clip(np.maximum(cp, cb) + fold * 0.3 + x * 0.2, 0, 1)
results['pes_skin_neck.dds'] = to_normal(h, cav)
print('cuello: poros', n6, 'foliculos barba', n6b)

for k, im in results.items():
    write_dds(os.path.join(out, k), im)

try:
    from PIL import Image
    S = 320   # 6.25 mm de piel
    order = ['pes_skin_pores_large.dds', 'pes_skin_pores_round.dds', 'pes_skin_forehead.dds', 'pes_skin_crepe.dds', 'pes_skin_lips.dds', 'pes_skin_neck.dds']
    def shade(im):
        n = im[:S, :S, :3] * 2 - 1
        L = np.array([-0.55, -0.45, 0.70]); L /= np.linalg.norm(L)       # luz rasante desde arriba a la izquierda
        d = np.clip((n * L).sum(-1) / np.maximum(np.linalg.norm(n, axis=-1), 1e-3), 0, 1)
        return np.repeat(d[..., None], 3, -1) * np.array([0.95, 0.78, 0.68])
    prev = np.concatenate([np.concatenate([shade(results[k]), results[k][:S, :S, :3], np.repeat(results[k][:S, :S, 3:], 3, -1)], 0) for k in order], 1)
    Image.fromarray((np.clip(prev, 0, 1) * 255).astype(np.uint8)).save(os.path.join(out, 'pes_skin_micro_zones_preview.png'))
except Exception as e:
    print('sin vista previa:', e)
