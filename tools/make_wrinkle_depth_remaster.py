# PES21 FX (10/11, Agustin: "las de la cara ... tomando de base como se plasman las texturas de arrugas remasterizadas, la escala esta
# mal") ARRUGAS POR GESTO SACADAS DE LAS REMASTERIZADAS. Las de make_wrinkle_depth.py eran surcos dibujados de ~0.9 mm de ancho
# (longitud de onda ~12 texeles de 1024) que cubrian el 0.1-1 % de la cara: lineas finas y oscuras que no seguian las arrugas del
# juego. Aca la profundidad sale de INTEGRAR la normal remasterizada (pes_wrinkle_normal_2048, Poisson por FFT: los mismos pliegues,
# ~25 texeles de 1024 de longitud de onda) y se reparte por gesto con la mascara de zonas del juego (pes_wrinkle_mask_512x1024):
#   a: R frente (+ sienes)  G entrecejo  B ojos  A surco (zona boca, arriba de la comisura)
#   b: R menton (zona boca, abajo)  G marioneta (mandibulas)  B cuello  A (vacio)
# Cada canal 0..1 (fondo = percentil 99.7). Imprime la profundidad en mm por canal (kWrDmmA / kWrDmmB de pes_wrdepth.hlsli) que hace
# que con el gesto entero la pendiente sea la de las remasterizadas (p99 por zona).
#   python3 make_wrinkle_depth_remaster.py carpeta_textures carpeta_salida [--vista png]
import sys, struct, numpy as np
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
def dds(fn, w, h):
    d = open(fn, 'rb').read(); a = np.frombuffer(d, np.uint8, w * h * 4, 128).reshape(h, w, 4).astype(np.float32) / 255
    return a[..., [2, 1, 0, 3]]   # BGRA -> RGBA
N = 2048
n = dds(src + '/pes_wrinkle_normal_2048.dds', N, N)
nx, ny = n[..., 0] * n[..., 3] * 2 - 1, n[..., 1] * 2 - 1   # como el PS (x = R * A, y = G); marco T = +u, B = -v
gu, gv = -nx, ny
fu = np.fft.fftfreq(N)[None, :]; fv = np.fft.fftfreq(N)[:, None]
k2 = (2 * np.pi) ** 2 * (fu ** 2 + fv ** 2); k2[0, 0] = 1
H = (np.conj(2j * np.pi * fu) * np.fft.fft2(gu) + np.conj(2j * np.pi * fv) * np.fft.fft2(gv)) / k2; H[0, 0] = 0
h = np.real(np.fft.ifft2(H))
h -= np.real(np.fft.ifft2(np.fft.fft2(h) * np.exp(-2 * (np.pi * 48.0) ** 2 * (fu ** 2 + fv ** 2))))   # sin la forma grande (48: quedan los bultos de piel juntada)
slope = np.hypot(np.gradient(h, axis=1), np.gradient(h, axis=0))
m = dds(src + '/pes_wrinkle_mask_512x1024.dds', 512, 1024)
up = lambda x: np.asarray(Image.fromarray(x).resize((N, N), Image.BILINEAR))
T = np.stack([up(m[:512, :, c]) for c in range(4)], -1); B = np.stack([up(m[512:, :, c]) for c in range(4)], -1)
# (10/11 c) relieve ENTERO (surcos y bultos), no solo el fondo: con solo max(-h, 0) quedaban las rayas del fondo y se perdia la
# piel juntada que da la forma suave de las remasterizadas. 0 = cresta mas alta de la zona, 1 = fondo; la piel lisa queda en el medio
dep = -h
V = (np.arange(N) + 0.5)[:, None] / N * np.ones((1, N))
s = np.clip((V - 0.600) / 0.03, 0, 1); s = s * s * (3 - 2 * s)
ch = [dep * np.maximum(T[..., 0], T[..., 1]), dep * T[..., 2], dep * T[..., 3], dep * B[..., 0] * (1 - s),
      dep * B[..., 0] * s, dep * np.maximum(B[..., 1], B[..., 2]), dep * B[..., 3], dep * 0]
# (10/11 b, Agustin: "deben tener mas detalle, como arrugas en Unreal Engine 5") ARRUGUITAS FINAS QUE SIGUEN AL PLIEGUE: ruido
# estirado a lo largo de cada pliegue (convolucion integral de linea sobre la direccion del pliegue, sacada del tensor de estructura
# de la profundidad), pasa altos -> rayitas paralelas de ~1 mm en las paredes y alrededor del pliegue, que se prenden con el mismo
# gesto (envolvente = la profundidad de cada canal). Todo a 2048 (antes 1024: la mitad del detalle).
def blur(x, sg):
    return np.real(np.fft.ifft2(np.fft.fft2(x) * np.exp(-2 * (np.pi * sg) ** 2 * (fu ** 2 + fv ** 2))))
ds = blur(np.maximum(dep, 0), 4.0)
gx, gy = np.gradient(ds, axis=1), np.gradient(ds, axis=0)
jxx, jxy, jyy = blur(gx * gx, 6.0), blur(gx * gy, 6.0), blur(gy * gy, 6.0)
ang = 0.5 * np.arctan2(2 * jxy, jxx - jyy) + np.pi / 2          # direccion del pliegue (perpendicular a la pendiente)
tx, ty = np.cos(ang), np.sin(ang)
rng = np.random.default_rng(1011)
noise = blur(rng.standard_normal((N, N)), 0.9)
yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
def samp(img, x, y):
    x = np.clip(x, 0, N - 1.001); y = np.clip(y, 0, N - 1.001); x0 = x.astype(np.int32); y0 = y.astype(np.int32); fx = x - x0; fy = y - y0
    return (img[y0, x0] * (1 - fx) + img[y0, x0 + 1] * fx) * (1 - fy) + (img[y0 + 1, x0] * (1 - fx) + img[y0 + 1, x0 + 1] * fx) * fy
lic = noise.copy(); wsum = 1.0
for sgn in (1.0, -1.0):
    px, py, dx, dy = xx.copy(), yy.copy(), tx * sgn, ty * sgn
    for k in range(1, 15):
        px += dx; py += dy
        ix, iy = np.clip(px, 0, N - 1).astype(np.int32), np.clip(py, 0, N - 1).astype(np.int32)
        nx2, ny2 = tx[iy, ix], ty[iy, ix]
        flip = (nx2 * dx + ny2 * dy) < 0; dx, dy = np.where(flip, -nx2, nx2), np.where(flip, -ny2, ny2)
        w = np.exp(-0.5 * (k / 8.0) ** 2); lic += w * samp(noise, px, py); wsum += w
lic /= wsum
fine = lic - blur(lic, 4.0); fine /= fine.std() + 1e-6
FINE = 0.015   # alto de las arruguitas (fraccion del fondo del pliegue)
kmm = []
D = np.zeros((N, N, 8), np.float32)
zm = [np.maximum(T[..., 0], T[..., 1]), T[..., 2], T[..., 3], B[..., 0] * (1 - s), B[..., 0] * s, np.maximum(B[..., 1], B[..., 2]), B[..., 3], B[..., 3] * 0]
for i, z in enumerate(zm):
    act = z > 0.3
    if not act.any(): kmm.append(0.0); continue
    lo, hi = np.percentile(dep[act], 3), np.percentile(dep[act], 99.7)
    d = np.clip((dep - lo) / (hi - lo), 0, 1) * np.clip(z, 0, 1)
    sd = np.hypot(*np.gradient(d ** 1.2)) / 0.275    # 0.275 mm por texel de 2048; gesto entero: perfil ^ (1 + 0.2)
    kmm.append(float(np.percentile(slope[act], 99) / np.percentile(sd[act], 99)))
    env = np.clip((blur(np.maximum(dep - np.median(dep[act]), 0) / (hi - lo), 3.0) * z - 0.02) / 0.30, 0, 1); env = env * env * (3 - 2 * env)
    D[..., i] = np.clip(d + FINE * fine * env, 0, 1)
print('kWrDmmA', np.round(kmm[:4], 2), 'kWrDmmB', np.round(kmm[4:], 2))
def write_dds(fn, img):
    mips = []; cur = img.astype(np.float32)
    while True:
        mips.append(np.clip(np.round(cur), 0, 255).astype(np.uint8))
        if cur.shape[0] == 1: break
        cur = cur.reshape(cur.shape[0] // 2, 2, cur.shape[1] // 2, 2, 4).mean((1, 3))
    hh, ww = img.shape[:2]
    hdr = struct.pack('<4sIIIIIII44x', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000, hh, ww, ww * 4, 0, len(mips))
    pf = struct.pack('<II4sIIIII', 32, 0x4, b'DX10', 0, 0, 0, 0, 0)
    caps = struct.pack('<IIIII', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    dx10 = struct.pack('<IIIII', 28, 3, 0, 1, 0)
    with open(fn, 'wb') as f:
        f.write(hdr + pf + caps + dx10)
        for mm in mips: f.write(mm.tobytes())
q = np.round(D * 255)
write_dds(out + '/pes_wrinkle_depth_a.dds', q[..., :4]); write_dds(out + '/pes_wrinkle_depth_b.dds', q[..., 4:])
if '--vista' in sys.argv:
    cols = np.array([(255, 80, 80), (80, 255, 80), (90, 140, 255), (255, 220, 60), (255, 120, 255), (60, 255, 255), (255, 160, 60), (200, 200, 200)], np.float32)
    img = np.full((N, N, 3), 40.0)
    for i in range(8): img = img * (1 - D[..., i:i + 1]) + cols[i] * D[..., i:i + 1]
    Image.fromarray(img.clip(0, 255).astype(np.uint8)).save(sys.argv[sys.argv.index('--vista') + 1])
