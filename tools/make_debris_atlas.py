# PES21 FX - atlas de PARTICULAS de tierra y pasto (textures\pes_debris.dds y pes_debris_n.dds), 2/11/2026. Pedido de Agustin:
# "texturas a las particulas que saltan de tierra y cesped". Mismo formato que el atlas de hojas (4 bandas de 512 x 512 en una
# tira de 2048 x 512, raiz ABAJO v = 1): asi el PS de las hojas (18afff) lo lee sin cambios cuando la DLL lo enlaza en t8 / t9
# solo para el dibujo de los recortes (pitchwear.cpp, pes_clip_vs.hlsl).
#   banda 0: manojo arrancado (3-5 hojas rotas con un poco de raiz y tierra abajo)
#   banda 1: terron de tierra (grumo irregular con piedritas)
#   banda 2: nube de polvo (alfa suave, sin borde duro)
#   banda 3: pedazo de hoja suelto (una hoja rota, doblada)
#   pes_debris.dds   R claridad (0.5 neutro), G oclusion (el PS usa 2*G), B brillo, A alfa
#   pes_debris_n.dds RG normal tangente, B altura abajo->arriba (0..1), A grosor (1 abajo .. 0.3 arriba)
import numpy as np, struct, sys, os
from scipy.ndimage import distance_transform_edt, gaussian_filter, sobel
from PIL import Image, ImageDraw

S = 512; SS = 2   # supermuestreo del dibujo

def canvas(): return Image.new('L', (S * SS, S * SS), 0)
def to_np(im): a = np.asarray(im, np.float32) / 255.0; return a.reshape(S, SS, S, SS).mean((1, 3))

def blade(draw, x0, y0, ang, length, width, bend, seed, val=255):
    rng = np.random.default_rng(seed); n = 40
    pts_l, pts_r = [], []
    for i in range(n + 1):
        t = i / n
        a = ang + bend * t * t * 1.3 + rng.normal(0, 0.01)
        x = x0 + np.sin(a) * length * t; y = y0 - np.cos(a) * length * t
        w = width * (1.0 - 0.85 * t ** 1.6) * (0.75 + 0.25 * np.sin(t * 9 + seed))
        nx, ny = np.cos(a), np.sin(a)
        pts_l.append((x - nx * w, y - ny * w)); pts_r.append((x + nx * w, y + ny * w))
    poly = pts_l + pts_r[::-1]
    draw.polygon([(p[0] * SS, p[1] * SS) for p in poly], fill=val)
    return poly

def blob(draw, cx, cy, r, seed, lobes=7, val=255):
    rng = np.random.default_rng(seed); pts = []
    ph = rng.random(lobes) * 6.28; amp = rng.uniform(0.12, 0.35, lobes)
    for i in range(96):
        a = i / 96 * 6.2832
        rr = r * (1 + sum(amp[k] * np.sin(a * (k + 2) + ph[k]) for k in range(lobes)) / lobes * 2.2)
        pts.append(((cx + np.cos(a) * rr) * SS, (cy + np.sin(a) * rr) * SS))
    draw.polygon(pts, fill=val)

def band_tuft(seed):
    rng = np.random.default_rng(seed)
    im = canvas(); d = ImageDraw.Draw(im)
    nb = rng.integers(3, 6)
    for i in range(nb):
        blade(d, S * 0.5 + rng.uniform(-0.05, 0.05) * S, S * 0.90, rng.uniform(-0.55, 0.55), S * rng.uniform(0.45, 0.8), S * rng.uniform(0.022, 0.04), rng.uniform(-0.6, 0.6), seed * 10 + i)
    cov = to_np(im)
    # raiz / tierra abajo: grumo chico
    im2 = canvas(); d2 = ImageDraw.Draw(im2); blob(d2, S * 0.5, S * 0.9, S * 0.07, seed + 3, val=255)
    soil = to_np(im2)
    return cov, soil

def band_clod(seed):
    im = canvas(); d = ImageDraw.Draw(im); rng = np.random.default_rng(seed)
    blob(d, S * 0.5, S * 0.55, S * 0.27, seed)
    for i in range(6): blob(d, S * (0.5 + rng.uniform(-0.2, 0.2)), S * (0.55 + rng.uniform(-0.18, 0.18)), S * rng.uniform(0.05, 0.11), seed + 11 + i)
    return to_np(im)

def band_dust(seed):
    rng = np.random.default_rng(seed); yy, xx = np.mgrid[0:S, 0:S].astype(np.float32) / S
    a = np.zeros((S, S), np.float32)
    for i in range(14):
        cx, cy = 0.5 + rng.uniform(-0.2, 0.2), 0.52 + rng.uniform(-0.2, 0.2); r = rng.uniform(0.09, 0.2)
        a += np.exp(-((xx - cx) ** 2 + (yy - cy) ** 2) / (2 * r * r)) * rng.uniform(0.35, 0.7)
    a = gaussian_filter(a, 6); a = np.clip(a / a.max(), 0, 1)
    return a

def band_piece(seed):
    im = canvas(); d = ImageDraw.Draw(im); rng = np.random.default_rng(seed)
    blade(d, S * 0.5, S * 0.88, rng.uniform(-0.2, 0.2), S * 0.72, S * 0.05, rng.uniform(-0.9, 0.9), seed)
    cov = to_np(im)
    # punta rota: corte irregular arriba
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32)
    cut = (yy < S * (0.18 + 0.05 * np.sin(xx / S * 40 + seed))).astype(np.float32)
    return cov * (1 - cut)

def shade(cov, kind, seed):
    """R claridad, G oclusion, B brillo, normal RG, altura B, grosor A a partir de la cobertura"""
    rng = np.random.default_rng(seed + 100)
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32) / S
    inside = cov > 0.5
    dist = distance_transform_edt(inside).astype(np.float32) if inside.any() else np.zeros((S, S), np.float32)
    # altura: perfil redondeado hacia el borde (cilindro para hojas, grumo para tierra)
    dmax = max(dist.max(), 1.0)
    h = np.clip(dist / (dmax * 0.55), 0, 1) ** 0.5 if kind == 1 else np.sqrt(np.clip(dist / (dist.max() * 0.6 + 1e-3), 0, 1))
    if kind == 2: h = cov
    # grano (piedritas / grumos en la tierra: dos escalas; vetas finas en las hojas)
    g = gaussian_filter(rng.random((S, S)).astype(np.float32), 1.5 if kind == 1 else 3.0); g = (g - g.mean()) / (g.std() + 1e-6)
    if kind == 1:
        g2 = gaussian_filter(rng.random((S, S)).astype(np.float32), 7.0); g2 = (g2 - g2.mean()) / (g2.std() + 1e-6)
        g = 0.5 * g + 0.8 * g2
    hgrain = h + g * (0.16 if kind == 1 else 0.03)
    gx = sobel(hgrain, 1) / 8.0; gy = sobel(hgrain, 0) / 8.0
    k = 14.0 if kind == 1 else 4.0
    nx = -gx * k; ny = gy * k; nz = np.ones_like(nx); n = np.sqrt(nx * nx + ny * ny + nz * nz); nx /= n; ny /= n
    # claridad: borde mas oscuro, grano, vetas a lo largo en las hojas
    clar = 0.5 + 0.10 * (h - 0.5) + g * (0.08 if kind == 1 else 0.05)
    if kind in (0, 3): clar += 0.05 * np.sin(xx * 160 + seed) * cov
    if kind == 2: clar = 0.5 + 0.04 * g
    occ = 0.5 * (0.55 + 0.45 * (1 - yy)) if kind in (0, 3) else 0.5 * (0.6 + 0.4 * h)   # abajo mas oscuro (el PS usa 2*G)
    gloss = 0.25 + 0.2 * h if kind in (0, 3) else (0.10 if kind == 1 else 0.05)
    t = 1 - yy                                   # altura raiz -> punta
    thick = 1 - 0.7 * t if kind in (0, 3) else (0.9 if kind == 1 else 0.4)
    img = np.stack([clar, occ, np.full((S, S), gloss, np.float32) if np.isscalar(gloss) else gloss, cov], -1)
    nrm = np.stack([nx * 0.5 + 0.5, ny * 0.5 + 0.5, t, np.full((S, S), thick, np.float32) if np.isscalar(thick) else thick], -1)
    return img.astype(np.float32), nrm.astype(np.float32)

def dilate_color(img, cov, it=6):
    """color a los texeles transparentes vecinos para que el filtrado no traiga negro"""
    out = img.copy(); m = cov > 0.02
    for _ in range(it):
        for ch in range(3):
            c = out[..., ch]; blurred = gaussian_filter(c * m, 1.5) / np.maximum(gaussian_filter(m.astype(np.float32), 1.5), 1e-4)
            out[..., ch] = np.where(m, c, blurred)
        m = gaussian_filter(m.astype(np.float32), 1.5) > 0.02
    return out

bands = []
cov0, soil0 = band_tuft(5); img0, nrm0 = shade(np.maximum(cov0, soil0), 0, 5)
# la tierra de la raiz: mas oscura y mate
img0[..., 0] = np.where(soil0 > 0.5, 0.33, img0[..., 0]); img0[..., 2] = np.where(soil0 > 0.5, 0.08, img0[..., 2])
bands.append((img0, nrm0))
c1 = band_clod(9); bands.append(shade(c1, 1, 9))
c2 = band_dust(13); i2, n2 = shade(c2, 2, 13); bands.append((i2, n2))
c3 = band_piece(17); bands.append(shade(c3, 3, 17))
img = np.concatenate([dilate_color(b[0], b[0][..., 3]) for b in bands], 1)
nrm = np.concatenate([b[1] for b in bands], 1)

def half(a): return (a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2]) * 0.25
def mips_coverage(img, alpha_ch, thr=0.3):
    mips = []; cur = img.astype(np.float32); cov0 = (cur[..., alpha_ch] > thr).mean()
    while True:
        mips.append(cur)
        if cur.shape[0] <= 1 or cur.shape[1] <= 1: break
        a = cur[..., alpha_ch:alpha_ch + 1]
        wsum = a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2]
        col = (cur[0::2, 0::2] * a[0::2, 0::2] + cur[1::2, 0::2] * a[1::2, 0::2] + cur[0::2, 1::2] * a[0::2, 1::2] + cur[1::2, 1::2] * a[1::2, 1::2]) / np.maximum(wsum, 1e-4)
        nxt = col.copy(); nxt[..., alpha_ch] = half(cur)[..., alpha_ch]
        if nxt.shape[0] >= 4:
            al = nxt[..., alpha_ch]; lo, hi = 0.5, 4.0
            for _ in range(24):
                mid = 0.5 * (lo + hi)
                if (np.clip(al * mid, 0, 1) > thr).mean() < cov0: lo = mid
                else: hi = mid
            nxt[..., alpha_ch] = np.clip(al * 0.5 * (lo + hi), 0, 1)
        cur = nxt
    return mips
def write_dds(path, mips):
    H, W = mips[0].shape[:2]
    hdr = struct.pack('<4sI I I I I I I 11I', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8, H, W, W * 4, 0, len(mips), *([0] * 11))
    pf = struct.pack('<I I 4s I I I I I', 32, 0x41, b'\0\0\0\0', 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000)
    caps = struct.pack('<I I I I I', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr + pf + caps)
        for m in mips: f.write((np.clip(m, 0, 1) * 255 + 0.5).astype(np.uint8).tobytes())

out = sys.argv[1] if len(sys.argv) > 1 else '.'
write_dds(os.path.join(out, 'pes_debris.dds'), mips_coverage(img, 3))
mipsN = mips_coverage(np.concatenate([nrm[..., :3], img[..., 3:4]], -1), 3)
th = [nrm[..., 3].astype(np.float32)]
while th[-1].shape[0] > 1 and th[-1].shape[1] > 1: th.append(half(th[-1]))
for m, t in zip(mipsN, th): m[..., 3] = t
write_dds(os.path.join(out, 'pes_debris_n.dds'), mipsN)
prev = np.concatenate([img[..., :3] * img[..., 3:4] + (1 - img[..., 3:4]) * 0.08, nrm[..., :3]], 0)
Image.fromarray((np.clip(prev, 0, 1) * 255).astype(np.uint8)).save(os.path.join(out, 'pes_debris_preview.png'))
print('ok cobertura por banda', [round(float((b[0][..., 3] > 0.3).mean()), 3) for b in bands])
