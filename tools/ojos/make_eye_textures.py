#!/usr/bin/env python3
"""PES21 FX - texturas del OJO del juego rehechas (16/11, Agustin: "rehace los 11 colores de iris en 1024", "dale a las pestanas ese
detalle extra", "usa texturas trm y srm tambien"). Salen .ftex para una carpeta livecpk de Sider (mismo nombre y formato que las
del juego: DXT1 / DXT5), asi mejoran tambien el ojo SIN el shader del mod.
uso: make_eye_textures.py <carpeta con las .ftex/.dds originales> <carpeta de salida>
- eye_color_sub_00..10: 256 -> 1024. Se pasa la textura del juego a polares (centro 0.5, el iris llega a r = 0.245), se conserva su
  color y sus manchas, se agregan hebras finas que 256 px no pueden guardar, se borra el borde poligonal de afuera y se aclara el
  anillo oscuro del borde del iris (Agustin: "el borde del iris tan oscuro no me gusto").
- cm_eyes0_v00_def_srm: 128 plano -> 512 por zonas (cornea lisa, blanco apenas mas aspero con grano fino, borde mojado).
- face_eyelash_nrm / _srm: planas -> relieve redondo de cada pestana sacado del alfa, y brillo propio en el pelo."""
import sys, os, struct, numpy as np
from PIL import Image
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from ftex import Ftex

def blocks(img):                       # (H, W, C) -> (nb, 16, C)
    h, w, c = img.shape
    return img.reshape(h // 4, 4, w // 4, 4, c).transpose(0, 2, 1, 3, 4).reshape(-1, 16, c)
def to565(c):
    r = np.round(c[:, 0] * 31).astype(np.uint32); g = np.round(c[:, 1] * 63).astype(np.uint32); b = np.round(c[:, 2] * 31).astype(np.uint32)
    return (r << 11) | (g << 5) | b
def from565(v):
    return np.stack([((v >> 11) & 31) / 31.0, ((v >> 5) & 63) / 63.0, (v & 31) / 31.0], -1)
def dxt1_blocks(rgb):                  # rgb 0..1 (H, W, 3) -> bytes (8 por bloque), 4 colores, sin alfa
    B = blocks(rgb)
    hi = B.max(1); lo = B.min(1); ins = (hi - lo) / 16.0
    c0 = to565(np.clip(hi - ins, 0, 1)); c1 = to565(np.clip(lo + ins, 0, 1))
    sw = c0 < c1; c0, c1 = np.where(sw, c1, c0), np.where(sw, c0, c1)
    eq = c0 == c1
    p0 = from565(c0); p1 = from565(c1)
    pal = np.stack([p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3], 1)          # (nb, 4, 3)
    d = ((B[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)                  # (nb, 16, 4)
    idx = d.argmin(-1).astype(np.uint32); idx[eq] = 0
    bits = (idx << (2 * np.arange(16, dtype=np.uint32))[None, :]).sum(1).astype(np.uint32)
    out = np.zeros(len(B), dtype=[('c0', '<u2'), ('c1', '<u2'), ('i', '<u4')])
    out['c0'] = c0; out['c1'] = c1; out['i'] = bits
    return out.tobytes()
def dxt5_alpha(a):                     # a 0..1 (H, W) -> (nb, 8) uint8
    B = blocks(a[..., None])[:, :, 0]
    a0 = np.round(B.max(1) * 255).astype(np.int64); a1 = np.round(B.min(1) * 255).astype(np.int64)
    eq = a0 == a1
    w = np.arange(8)
    lv = np.where(w[None, :] == 0, a0[:, None], np.where(w[None, :] == 1, a1[:, None], ((8 - w[None, :]) * a0[:, None] + (w[None, :] - 1) * a1[:, None]) / 7.0))
    idx = np.abs(B[:, :, None] * 255 - lv[:, None, :]).argmin(-1).astype(np.uint64); idx[eq] = 0
    bits = (idx << (3 * np.arange(16, dtype=np.uint64))[None, :]).sum(1).astype(np.uint64)
    out = np.zeros((len(B), 8), np.uint8)
    out[:, 0] = a0; out[:, 1] = a1
    for k in range(6): out[:, 2 + k] = (bits >> np.uint64(8 * k)) & np.uint64(255)
    return out
def encode(img, fmt):                  # img (H, W, 4) 0..1 RGBA; fmt 2 = DXT1, 4 = DXT5
    col = np.frombuffer(dxt1_blocks(img[..., :3]), np.uint8).reshape(-1, 8)
    if fmt == 2: return col.tobytes()
    return np.concatenate([dxt5_alpha(img[..., 3]), col], 1).tobytes()
def mipchain(img):
    m = [img]
    while min(m[-1].shape[:2]) > 4:
        c = m[-1]; m.append((c[0::2, 0::2] + c[1::2, 0::2] + c[0::2, 1::2] + c[1::2, 1::2]) * 0.25)
    return m
def write_ftex(orig_path, out_path, img):
    o = Ftex(open(orig_path, 'rb').read())
    mips = [encode(m, o.fmt) for m in mipchain(img)]
    o.w, o.h = img.shape[1], img.shape[0]; o.mips = mips; o.mipCount = len(mips)
    o.header[16] = len(mips)
    open(out_path, 'wb').write(o.to_bytes(chunk=65536, small_first=True))
    return len(mips)
def load(p): return np.asarray(Image.open(p).convert('RGBA')).astype(np.float64) / 255
def bil(img, x, y, wrapx=False):       # x, y en pixeles (centros en +0.5)
    h, w = img.shape[:2]; fx = x - 0.5; fy = np.clip(y - 0.5, 0, h - 1)
    if not wrapx: fx = np.clip(fx, 0, w - 1)
    x0 = np.floor(fx).astype(int); y0 = np.minimum(np.floor(fy).astype(int), h - 2); tx = (fx - x0)[..., None]; ty = (fy - y0)[..., None]
    X0 = x0 % w if wrapx else np.minimum(x0, w - 2); X1 = (x0 + 1) % w if wrapx else X0 + 1
    return (img[y0, X0] * (1 - tx) + img[y0, X1] * tx) * (1 - ty) + (img[y0 + 1, X0] * (1 - tx) + img[y0 + 1, X1] * tx) * ty
def sstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)
def gnoise(shape, sy, sx, rng):        # ruido gaussiano periodico, grano (sy, sx) px
    h, w = shape; n = rng.standard_normal(shape)
    f = np.fft.ifft2(np.fft.fft2(n) * np.exp(-2 * np.pi ** 2 * ((sx * np.fft.fftfreq(w)[None, :]) ** 2 + (sy * np.fft.fftfreq(h)[:, None]) ** 2))).real
    return (f - f.mean()) / (f.std() + 1e-9)

IRIS_R = 0.245
def iris_1024(src):
    N = 1024; TH, NR = 4096, 1024
    # ampliacion bicubica de la textura del juego + un poco de nitidez (no se pasa por polares: dos remuestreos la emborronaban)
    im = Image.fromarray((src[..., :3] * 255 + 0.5).astype(np.uint8)).resize((N, N), Image.BICUBIC)
    from PIL import ImageFilter
    im = im.filter(ImageFilter.UnsharpMask(radius=3, percent=60, threshold=0))
    out = np.asarray(im).astype(np.float64) / 255
    g = (np.arange(N) + 0.5) / N - 0.5; X, Y = np.meshgrid(g, g)
    r = np.hypot(X, Y); a = np.arctan2(Y, X) % (2 * np.pi); R = r / IRIS_R
    # perfil por radio (promedio de cada anillo), para el borde poligonal y el anillo oscuro
    nb = 256; ri = np.clip((r / 0.5 * nb).astype(int), 0, nb - 1)
    cnt = np.bincount(ri.ravel(), minlength=nb).astype(np.float64)
    prof = np.stack([np.bincount(ri.ravel(), out[..., c].ravel(), minlength=nb) / np.maximum(cnt, 1) for c in range(3)], -1)
    rc = np.clip(r / 0.5 * nb - 0.5, 0, nb - 1.001); i0 = np.floor(rc).astype(int); t = (rc - i0)[..., None]
    ring = prof[i0] * (1 - t) + prof[np.minimum(i0 + 1, nb - 1)] * t
    # fuera del iris la textura no se ve (la tapa la esclerotica): promedio del anillo, sin el borde poligonal
    out = out + (ring - out) * sstep(1.15, 1.6, R)[..., None]
    # anillo oscuro del borde del iris: se acerca a la luz del medio del iris
    lp = (prof * [0.2126, 0.7152, 0.0722]).sum(-1); Rb = (np.arange(nb) + 0.5) / nb * 0.5 / IRIS_R
    ref = lp[(Rb > 0.55) & (Rb < 0.70)].mean()
    gain = 1 + 0.55 * (np.clip(ref / np.maximum(lp, 1e-4), 1.0, 2.2) - 1) * sstep(0.68, 0.92, Rb) * (1 - sstep(1.25, 1.7, Rb))
    out = out * (gain[i0] * (1 - t[..., 0]) + gain[np.minimum(i0 + 1, nb - 1)] * t[..., 0])[..., None]
    # hebras finas nuevas (mas chicas que un pixel de la textura vieja), onduladas, solo dentro del iris
    rng = np.random.default_rng(1024)
    hf = 0.55 * gnoise((NR, TH), 30, 1.6, rng) + 0.45 * gnoise((NR, TH), 13, 0.9, rng)
    wv = gnoise((NR, TH), 40, 50, rng) * 10
    hfc = bil(hf[..., None], a / (2 * np.pi) * TH + bil(wv[..., None], a / (2 * np.pi) * TH, np.clip(r / 0.5, 0, 1) * NR, wrapx=True)[..., 0], np.clip(r / 0.5, 0, 1) * NR, wrapx=True)[..., 0]
    m = sstep(0.36, 0.46, R) * (1 - sstep(0.90, 1.02, R))
    out = np.clip(out * (1 + 0.05 * np.tanh(1.2 * hfc) * m)[..., None], 0, 1)
    return np.concatenate([out, np.ones((N, N, 1))], -1)

def srm_512():
    N = 512; g = (np.arange(N) + 0.5) / N - 0.5; X, Y = np.meshgrid(g, g); R = np.hypot(X, Y) / IRIS_R
    rng = np.random.default_rng(5)
    grain = gnoise((N, N), 2.0, 2.0, rng) * 0.5 + gnoise((N, N), 9, 9, rng) * 0.5
    scl = sstep(1.0, 1.25, R)                               # 0 cornea, 1 esclerotica
    rough = scl * np.clip(0.16 + 0.05 * grain, 0.05, 0.3)   # G: 0 = lo mas liso (como el juego), blanco apenas mas aspero
    rough *= 1 - 0.7 * sstep(1.55, 2.0, R)                  # hacia los parpados: borde mojado, otra vez liso
    return np.stack([np.ones((N, N)), rough, np.ones((N, N)), np.ones((N, N))], -1)

def lash(alp):
    # alfa del juego 512x256 -> relieve: cada pestana es un cilindro (la normal gira a lo ancho del pelo)
    a = alp[..., 3]
    def blur(x, s):
        k = np.exp(-0.5 * (np.arange(-6, 7) / s) ** 2); k /= k.sum()
        x = sum(np.roll(x, i - 6, 1) * k[i] for i in range(13)); return sum(np.roll(x, i - 6, 0) * k[i] for i in range(13))
    b = blur(a, 1.2)
    dx = (np.roll(b, -1, 1) - np.roll(b, 1, 1)); dy = (np.roll(b, -1, 0) - np.roll(b, 1, 0))
    nx = np.clip(-dx * 2.2, -0.75, 0.75); ny = np.clip(-dy * 2.2, -0.75, 0.75)
    cov = sstep(0.02, 0.2, blur(a, 2.0))[..., None]
    # DXT5nm como las del juego: R = 1, G = y, B = 0, A = x
    nrm = np.stack([np.ones_like(a), ny * 0.5 + 0.5, np.zeros_like(a), nx * 0.5 + 0.5], -1)
    flat = np.array([1.0, 127 / 255, 0.0, 127 / 255])
    nrm = flat + (nrm - flat) * cov
    # srm del juego: R 0, G 33/255 plano. Pelo: un poco mas liso en el centro de cada pestana (brillo fino), igual fuera
    srm = np.stack([np.zeros_like(a), 33 / 255 - (13 / 255) * sstep(0.3, 0.9, b), np.zeros_like(a), np.ones_like(a)], -1)
    return nrm, srm

if __name__ == '__main__':
    src, out = sys.argv[1], sys.argv[2]; os.makedirs(out, exist_ok=True)
    names = sorted(f[:-5] for f in os.listdir(src) if f.startswith('eye_color_sub_') and f.endswith('.ftex'))
    for n in names:
        img = iris_1024(load(os.path.join(src, n + '.dds')))
        k = write_ftex(os.path.join(src, n + '.ftex'), os.path.join(out, n + '.ftex'), img)
        Image.fromarray((img[..., :3] * 255 + 0.5).astype(np.uint8)).save(os.path.join(out, n + '.png'))
        print(n, img.shape, 'mips', k)
    s = srm_512(); write_ftex(os.path.join(src, 'cm_eyes0_v00_def_srm.ftex'), os.path.join(out, 'cm_eyes0_v00_def_srm.ftex'), s)
    Image.fromarray((s[..., :3] * 255 + 0.5).astype(np.uint8)).save(os.path.join(out, 'cm_eyes0_v00_def_srm.png'))
    nrm, srm = lash(load(os.path.join(src, 'face_eyelash_alp.dds')))
    write_ftex(os.path.join(src, 'face_eyelash_nrm.ftex'), os.path.join(out, 'face_eyelash_nrm.ftex'), nrm)
    write_ftex(os.path.join(src, 'face_eyelash_srm.ftex'), os.path.join(out, 'face_eyelash_srm.ftex'), srm)
    Image.fromarray((nrm * 255 + 0.5).astype(np.uint8)).save(os.path.join(out, 'face_eyelash_nrm.png'))
    print('ok')
