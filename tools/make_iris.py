# PES21 FX - genera textures\pes_iris_polar.dds (RGB detalle de albedo, A altura) y pes_iris_polar_n.dds (normal tangente)
# en coordenadas POLARES: u = angulo alrededor del centro del iris (0..1), v = radio (0 = pupila, 1 = borde del iris).
# El shader de ojos (fccd955f) las lee con el angulo/radio que saca del mapa de altura del juego, asi sirven para
# cualquier color de ojos y cualquier disposicion de UV: el color sigue viniendo de la textura del juego.
import numpy as np, struct, sys, os
W, H = 2048, 512
rng = np.random.default_rng(7)
u = (np.arange(W) + 0.5) / W; v = (np.arange(H) + 0.5) / H
U, V = np.meshgrid(u, v)                       # V: radio 0..1
def noise1(x, seed):   # ruido 1D periodico en x (0..1), por interpolacion de valores aleatorios
    n = len(x) if np.ndim(x) else 1
    return x
def pnoise(freq_u, freq_v, seed, periodic=True):
    r = np.random.default_rng(seed)
    gu, gv = int(freq_u), int(freq_v)
    g = r.random((gv + 1, gu + 1)).astype(np.float32)
    if periodic: g[:, -1] = g[:, 0]
    x = U * gu; y = V * gv
    x0 = np.floor(x).astype(int) % gu; y0 = np.minimum(np.floor(y).astype(int), gv - 1)
    fx = x - np.floor(x); fy = y - np.floor(y)
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy)
    x1 = (x0 + 1) % (gu + 1); x1 = np.where(x1 == gu, gu, x1)
    a = g[y0, x0]; b = g[y0, x0 + 1]; c = g[y0 + 1, x0]; d = g[y0 + 1, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy
def fbm(fu, fv, seed, oct=4):
    s = np.zeros_like(U); amp = 1; tot = 0
    for i in range(oct):
        s += amp * pnoise(fu * 2 ** i, fv * 2 ** i, seed + i); tot += amp; amp *= 0.5
    return s / tot
# --- fibras radiales: muchas lineas finas en u, moduladas a lo largo de v (se retuercen un poco)
twist = (fbm(6, 3, 11) - 0.5) * 0.06 * V
fib_fine = pnoise(900, 6, 21) * 0.5 + pnoise(400, 4, 22) * 0.3 + pnoise(180, 3, 23) * 0.2
Ut = (U + twist) % 1.0
# re-muestrear fib_fine con el giro: aproximacion por desplazamiento de columnas
shift = (twist * W).astype(int)
fib = np.take_along_axis(fib_fine, (np.arange(W)[None, :] + shift) % W, axis=1)
fib = (fib - fib.mean()) / (fib.std() + 1e-6)
# --- criptas (huecos oscuros, mas cerca del collarete) y collarete (anillo claro en v~0.35)
cry = fbm(40, 8, 31, 3)
cry = np.clip((cry - 0.58) * 6, 0, 1) * np.exp(-((V - 0.5) / 0.25) ** 2)
collar = np.exp(-((V - 0.33) / 0.05) ** 2) * (0.7 + 0.3 * pnoise(60, 2, 41))
# --- surcos de contraccion (anillos concentricos cerca del borde)
furrow = np.exp(-((V - 0.78) / 0.03) ** 2) * (0.5 + 0.5 * pnoise(30, 2, 51)) + np.exp(-((V - 0.9) / 0.025) ** 2) * 0.6
# --- borde de la pupila (ruff): pequenos dientes
ruff = np.clip(1 - V / 0.07, 0, 1) * (0.5 + 0.5 * pnoise(240, 1, 61))
# --- limbo: oscurece suavemente el borde exterior
limb = np.clip((V - 0.86) / 0.14, 0, 1) ** 1.5
# albedo detail (multiplicador, 1.0 = sin cambio): se guarda como valor*0.5 (0.5 = 1.0)
det = 1.0 + 0.22 * fib * (0.6 + 0.4 * np.sin(V * 3.1416)) - 0.45 * cry + 0.25 * collar - 0.12 * furrow - 0.25 * ruff - 0.35 * limb
# leve variacion de tono por zona (mas calido cerca de la pupila, como un ojo real)
warm = np.exp(-(V / 0.3) ** 2) * 0.08
R = det * (1 + warm); G = det; B = det * (1 - warm * 0.8)
# altura (A): fibras en relieve, criptas hundidas, collarete elevado, surcos hundidos
h = 0.5 + 0.10 * fib * (0.5 + 0.5 * np.sin(V * 3.1416)) - 0.35 * cry + 0.2 * collar - 0.15 * furrow + 0.1 * ruff
h = np.clip(h, 0, 1)
rgba = np.stack([np.clip(R * 0.5, 0, 1), np.clip(G * 0.5, 0, 1), np.clip(B * 0.5, 0, 1), h], -1)
# normal tangente a partir de la altura (x = angulo, y = radio); la escala en x se ajusta porque el perimetro es mayor
dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * (W / 2.0) * 0.004
dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * (H / 2.0) * 0.004
nrm = np.stack([-dx, -dy, np.ones_like(h)], -1); nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
nrgba = np.stack([nrm[..., 0] * 0.5 + 0.5, nrm[..., 1] * 0.5 + 0.5, nrm[..., 2] * 0.5 + 0.5, np.ones_like(h)], -1)
def write_dds(path, img):   # RGBA8 sin comprimir, con mips (filtro de caja)
    mips = []; cur = img.astype(np.float32)
    while True:
        mips.append(cur)
        if cur.shape[0] <= 1 or cur.shape[1] <= 1: break
        cur = (cur[0::2, 0::2] + cur[1::2, 0::2] + cur[0::2, 1::2] + cur[1::2, 1::2]) * 0.25
    hdr = struct.pack('<4sI I I I I I I 11I', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8, img.shape[0], img.shape[1], img.shape[1] * 4, 0, len(mips), *([0] * 11))
    pf = struct.pack('<I I 4s I I I I I', 32, 0x41, b'\0\0\0\0', 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000)
    caps = struct.pack('<I I I I I', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr + pf + caps)
        for m in mips: f.write((np.clip(m, 0, 1) * 255 + 0.5).astype(np.uint8).tobytes())
out = sys.argv[1] if len(sys.argv) > 1 else '.'
write_dds(os.path.join(out, 'pes_iris_polar.dds'), rgba)
write_dds(os.path.join(out, 'pes_iris_polar_n.dds'), nrgba)
# vista previa
from PIL import Image
Image.fromarray((np.clip(rgba[..., :3] * 2, 0, 1) * 255).astype(np.uint8)).save(os.path.join(out, 'pes_iris_polar_preview.png'))
print('ok', rgba.shape)
