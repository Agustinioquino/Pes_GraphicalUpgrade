# PES21 FX - genera textures\pes_iris_polar.dds (RGB detalle de albedo, A altura) y pes_iris_polar_n.dds (normal tangente)
# en coordenadas POLARES: u = angulo alrededor del centro del iris (0..1), v = radio (0 = borde de la pupila, 1 = borde del iris).
# El shader de ojos (fccd955f) las lee con el angulo/radio que mide en el mapa de altura del juego, asi sirven para cualquier
# color de ojos, cualquier tamano de ojo y cualquier cara: el color sigue viniendo de la textura del juego.
# v2 (16/11, Agustin: "revisar las texturas, rehacerlas"; "el borde del iris tan oscuro no me gusto"): anatomia del iris real en vez
# de ruido suelto: fibras (trabeculas) que salen de la pupila y se abren, collarete en zigzag a 1/3 del radio, criptas en forma de
# rombo pegadas al collarete (no manchas negras), surcos de contraccion en arcos cortados, borde de la pupila festoneado, y el
# borde de afuera casi sin oscurecer (antes -35 %, ahora -8 %).
import numpy as np, struct, sys, os
W, H = 2048, 512
rng = np.random.default_rng(16)
u = (np.arange(W) + 0.5) / W; v = (np.arange(H) + 0.5) / H
U, V = np.meshgrid(u, v)
FU = np.fft.fftfreq(W)[None, :]; FV = np.fft.fftfreq(H)[:, None]
def filt(su, sv):            # ruido gaussiano periodico: su / sv = tamano del grano en px a lo ancho (angulo) y a lo alto (radio)
    n = rng.standard_normal((H, W))
    f = np.fft.ifft2(np.fft.fft2(n) * np.exp(-2 * np.pi ** 2 * ((su * FU) ** 2 + (sv * FV) ** 2))).real
    return (f - f.mean()) / (f.std() + 1e-9)
def n1(su):                  # ruido 1D periodico en el angulo
    n = rng.standard_normal(W)
    f = np.fft.ifft(np.fft.fft(n) * np.exp(-2 * np.pi ** 2 * (su * np.fft.fftfreq(W)) ** 2)).real
    return (f - f.mean()) / (f.std() + 1e-9)
def shift_u(img, px):        # corre cada pixel en el angulo (px con decimales), con vuelta
    c = np.arange(W)[None, :] + px; c0 = np.floor(c).astype(int); t = c - c0
    return np.take_along_axis(img, c0 % W, 1) * (1 - t) + np.take_along_axis(img, (c0 + 1) % W, 1) * t
def sstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)
# --- collarete: anillo en zigzag que separa la zona de la pupila de la zona ciliar
vc = 0.30 + 0.040 * n1(90) + 0.012 * n1(14)
VC = vc[None, :]
inner = 1 - sstep(-0.03, 0.03, V - VC)                    # 1 = zona de la pupila
# --- fibras: hebras largas que se ondulan y se abren hacia afuera
wav = filt(60, 90) * 9.0 * (0.3 + V) + filt(18, 40) * 2.5
fa = shift_u(filt(1.5, 80), wav); fb = shift_u(filt(4.0, 150), wav * 0.7); fc = shift_u(filt(0.8, 28), wav)
cil = np.tanh(1.3 * (0.55 * fa + 0.35 * fb + 0.30 * fc))  # zona ciliar: hebras gruesas y finas mezcladas
pup = np.tanh(1.3 * (0.75 * filt(1.0, 220) + 0.35 * filt(2.5, 120)))   # zona de la pupila: fibras finas, rectas y parejas
fib = cil * (1 - inner) + pup * inner
ridge = np.exp(-((V - VC) / 0.030) ** 2) * (0.75 + 0.25 * n1(10)[None, :])
# --- criptas: rombos alargados hacia afuera, nacen en el collarete
pit = np.zeros((H, W))
for i in range(44):
    cu = rng.random(); lu = rng.uniform(0.006, 0.014); lv = rng.uniform(0.045, 0.11)
    cv = vc[int(cu * W) % W] + lv * rng.uniform(0.7, 1.3)
    du = np.abs(((U - cu + 0.5) % 1.0) - 0.5) / lu; dv = np.abs(V - cv) / lv
    d = du ** 1.4 + dv ** 1.4
    pit = np.maximum(pit, (1 - sstep(0.35, 1.0, d)) * rng.uniform(0.55, 1.0))
rim = np.clip(sstep(0.0, 0.5, pit) - sstep(0.4, 0.9, pit), 0, 1)        # labio claro alrededor de cada cripta
# --- surcos de contraccion: arcos concentricos cortados, solo en la mitad de afuera
fur = np.zeros((H, W))
for v0, wd in ((0.64, 0.011), (0.76, 0.012), (0.87, 0.010)):
    vf = v0 + 0.014 * n1(120)[None, :]
    fur += np.exp(-((V - vf) / wd) ** 2) * sstep(-0.2, 0.6, n1(160))[None, :]
fur = np.clip(fur, 0, 1) * (1 - pit)
# --- borde de la pupila: festones oscuros
ruff = (1 - sstep(0.0, 0.055, V)) * (0.65 + 0.35 * np.tanh(2 * n1(5)))[None, :]
limb = sstep(0.90, 1.0, V)
env = 0.55 + 0.45 * np.sin(np.clip(V, 0, 1) * np.pi)       # el detalle se apaga hacia los dos bordes
det = 1.0 + 0.20 * fib * env + 0.15 * ridge - 0.30 * pit * (0.8 + 0.2 * fib) + 0.07 * rim - 0.09 * fur - 0.22 * ruff - 0.08 * limb
warm = 0.06 * np.exp(-((V - 0.16) / 0.20) ** 2)            # zona de la pupila apenas mas calida (pigmento)
R = det * (1 + warm); G = det; B = det * (1 - 0.8 * warm)
h = np.clip(0.5 + 0.11 * fib * env + 0.20 * ridge - 0.32 * pit + 0.06 * rim - 0.12 * fur + 0.08 * ruff, 0, 1)
rgba = np.stack([np.clip(R * 0.5, 0, 1), np.clip(G * 0.5, 0, 1), np.clip(B * 0.5, 0, 1), h], -1)
dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * (W / 2.0) * 0.004
dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * (H / 2.0) * 0.004
dy[0, :] = 0; dy[-1, :] = 0
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
from PIL import Image
Image.fromarray((np.clip(rgba[..., :3] * 2 * 0.5, 0, 1) * 255).astype(np.uint8)).save(os.path.join(out, 'pes_iris_polar_preview.png'))
print('ok', rgba.shape, 'media', rgba[..., :3].mean())
