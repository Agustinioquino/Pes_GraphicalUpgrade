# PES21 FX - atlas de hojas de pasto (textures\pes_grass_blades.dds y pes_grass_blades_n.dds): 4 manojos de 512x512 en
# una tira de 2048x512 (banda 0 corto, 1 medio, 2 largo, 3 medio B). El juego dibuja cada "parcela" de pasto con cientos
# de tarjetas cruzadas que muestran todas la misma imagen de 128x128: un manojo de hojas. Aca se generan manojos mas
# detallados, con la raiz ABAJO (v = 1) y la punta arriba (v = 0); el PS elige la banda por tipo de pasto y por parcela.
#   pes_grass_blades.dds    R = variacion de claridad (0.5 = neutro; nervio central mas claro, bordes mas oscuros,
#                               vetas finas), G = oclusion raiz->punta (el juego usa 2*G), B = brillo (gloss), A = alfa
#   pes_grass_blades_n.dds  RG = normal tangente (x a lo ancho de la tarjeta, y a lo largo), B = altura t (0 raiz .. 1 punta),
#                               A = grosor (1 raiz .. 0.3 punta) para la translucidez
# Los mips conservan la cobertura del alfa (si no, de lejos el pasto "se evapora") y el color se dilata a los texeles
# transparentes para que el filtrado no traiga negro a los bordes.
import numpy as np, struct, sys, os
from scipy.ndimage import distance_transform_edt, gaussian_filter

S = 512
BANDS = [
    # nombre,  hojas largas, cortas, largo L, largo S, ancho, abanico (grados), caida, semilla
    ('corto',  18, 10, (0.40, 0.72), (0.22, 0.40), (0.026, 0.048), 32, (0.03, 0.12), 3),
    ('medio',  15,  9, (0.58, 0.98), (0.30, 0.52), (0.030, 0.062), 40, (0.06, 0.25), 7),
    ('largo',  13,  7, (0.75, 1.00), (0.35, 0.55), (0.034, 0.070), 45, (0.15, 0.45), 11),
    ('medioB', 16,  8, (0.55, 0.95), (0.28, 0.50), (0.030, 0.060), 38, (0.06, 0.28), 19),
]

def noise1(n, seed, sig):
    r = np.random.default_rng(seed); g = r.random(n).astype(np.float32)
    g = gaussian_filter(g, sig, mode='wrap'); return (g - g.mean()) / (g.std() + 1e-6)

def make_tuft(nbL, nbS, lenL, lenS, wid, fan, droopR, seed):
    N = S; rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    cov = np.zeros((N, N), np.float32); R = np.full((N, N), 0.5, np.float32); G = np.full((N, N), 0.5, np.float32)
    B = np.full((N, N), 0.5, np.float32); NX = np.zeros((N, N), np.float32); NY = np.zeros((N, N), np.float32)
    T = np.zeros((N, N), np.float32); TH = np.full((N, N), 0.5, np.float32)
    nb = nbL + nbS
    order = np.argsort(rng.random(nb))
    angles = np.concatenate([np.linspace(-fan, fan, nbL) + rng.uniform(-4, 4, nbL), rng.uniform(-fan - 10, fan + 10, nbS)])
    params = []
    for i in range(nb):
        a = np.radians(angles[i]); short = i >= nbL
        length = rng.uniform(*(lenS if short else lenL)) * N
        width = rng.uniform(*wid) * N * (0.85 if short else 1.0)
        x0 = N * 0.5 + rng.uniform(-0.05, 0.05) * N; y0 = N * 1.0 + 4
        droop = rng.uniform(*droopR) * length * np.sign(a + 1e-3)
        cx, cy = x0 + np.sin(a) * length * 0.55, y0 - np.cos(a) * length * 0.55
        tx, ty = x0 + np.sin(a) * length + droop, y0 - np.cos(a) * length * (0.92 - 0.25 * abs(np.sin(a)))
        params.append((x0, y0, cx, cy, tx, ty, width, rng.uniform(-0.08, 0.08), int(rng.integers(0, 10000))))
    depth_rank = np.zeros(nb)
    for k, i in enumerate(order): depth_rank[i] = k / max(nb - 1, 1)
    for k, i in enumerate(order):
        x0, y0, cx, cy, tx, ty, width, tone, sd = params[i]
        ts = np.linspace(0, 1, 900).astype(np.float32)
        px = (1 - ts) ** 2 * x0 + 2 * (1 - ts) * ts * cx + ts ** 2 * tx
        py = (1 - ts) ** 2 * y0 + 2 * (1 - ts) * ts * cy + ts ** 2 * ty
        dx = np.gradient(px); dy = np.gradient(py); ln = np.hypot(dx, dy) + 1e-6; dx /= ln; dy /= ln
        xm0, xm1 = int(max(0, px.min() - width - 4)), int(min(N, px.max() + width + 4))
        ym0, ym1 = int(max(0, py.min() - width - 4)), int(min(N, py.max() + width + 4))
        if xm1 <= xm0 or ym1 <= ym0: continue
        sx = xx[ym0:ym1, xm0:xm1]; sy = yy[ym0:ym1, xm0:xm1]
        best = np.full(sx.shape, 1e9, np.float32); bt = np.zeros(sx.shape, np.float32); bs = np.zeros(sx.shape, np.float32)
        rad = int(width * 0.6 + 5)
        for j in range(len(ts)):
            ax0, ax1 = int(max(xm0, px[j] - rad)) - xm0, int(min(xm1, px[j] + rad + 1)) - xm0
            ay0, ay1 = int(max(ym0, py[j] - rad)) - ym0, int(min(ym1, py[j] + rad + 1)) - ym0
            if ax1 <= ax0 or ay1 <= ay0: continue
            wx = sx[ay0:ay1, ax0:ax1]; wy = sy[ay0:ay1, ax0:ax1]
            d = np.hypot(wx - px[j], wy - py[j]); sub = best[ay0:ay1, ax0:ax1]; m = d < sub
            best[ay0:ay1, ax0:ax1] = np.where(m, d, sub)
            bt[ay0:ay1, ax0:ax1] = np.where(m, ts[j], bt[ay0:ay1, ax0:ax1])
            side = (wx - px[j]) * dy[j] - (wy - py[j]) * dx[j]
            bs[ay0:ay1, ax0:ax1] = np.where(m, side, bs[ay0:ay1, ax0:ax1])
        w = width * (1 - bt) ** 0.75 * (1 + 0.25 * np.sin(bt * np.pi)) * 0.5 + 0.5
        s = np.clip(bs / np.maximum(w, 1e-3), -1, 1)
        alpha = np.clip((w - best) / 1.1, 0, 1)
        inside = alpha > 0.02
        vein = np.exp(-(s / 0.22) ** 2) * 0.16 - (np.abs(s) > 0.75) * (np.abs(s) - 0.75) * 0.5
        streak = noise1(256, sd, 3.0)[np.clip((s * 0.5 + 0.5) * 255, 0, 255).astype(int)] * 0.045
        along = noise1(512, sd + 1, 14.0)[np.clip(bt * 511, 0, 511).astype(int)] * 0.05
        lightness = 0.5 + tone + vein + streak + along + 0.06 * (bt - 0.3)
        ao = (0.40 + 0.60 * np.clip(bt / 0.55, 0, 1) ** 0.8) * (0.85 + 0.15 * depth_rank[i])   # raiz 0.34 .. punta 1 (antes 0.17: visto desde arriba quedaba todo hundido)
        gloss = (1.0 - 0.55 * s * s) * (0.55 + 0.45 * np.clip(bt, 0, 1)) * (0.85 + 0.15 * depth_rank[i])
        nx = -s * 0.55 + np.sign(s) * np.exp(-(s / 0.18) ** 2) * 0.25
        ny = 0.14 * np.sin(bt * 11.0 + float(sd % 7)) * (0.4 + 0.6 * bt)
        thick = 1.0 - 0.7 * bt ** 1.2
        blend = np.clip(alpha, 0, 1)
        def put(dst, val):
            cur = dst[ym0:ym1, xm0:xm1]; dst[ym0:ym1, xm0:xm1] = np.where(inside, cur * (1 - blend) + val * blend, cur)
        put(R, np.clip(lightness, 0, 1)); put(G, np.clip(ao * 0.5, 0, 0.5)); put(B, np.clip(gloss, 0, 1))
        put(NX, nx); put(NY, ny); put(T, bt); put(TH, thick)
        cov[ym0:ym1, xm0:xm1] = np.maximum(cov[ym0:ym1, xm0:xm1], alpha)
    opaque = cov > 0.3
    _, idx = distance_transform_edt(~opaque, return_indices=True)
    R, G, B, NX, NY, T, TH = [a[idx[0], idx[1]] for a in (R, G, B, NX, NY, T, TH)]
    nz = np.sqrt(np.clip(1 - NX * NX - NY * NY, 0.05, 1)); nl = np.sqrt(NX * NX + NY * NY + nz * nz); NX = NX / nl; NY = NY / nl
    return np.stack([R, G, B, cov], -1), np.stack([NX * 0.5 + 0.5, NY * 0.5 + 0.5, T, TH], -1)

tufts = [make_tuft(*b[1:]) for b in BANDS]
img = np.concatenate([t[0] for t in tufts], 1)
nrm = np.concatenate([t[1] for t in tufts], 1)
cov = img[..., 3]

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
write_dds(os.path.join(out, 'pes_grass_blades.dds'), mips_coverage(img, 3))
# normal: mips ponderados por el alfa de la hoja; A = grosor con promedio simple
mipsN = mips_coverage(np.concatenate([nrm[..., :3], cov[..., None]], -1), 3)
th = [nrm[..., 3].astype(np.float32)]
while th[-1].shape[0] > 1 and th[-1].shape[1] > 1: th.append(half(th[-1]))
for m, t in zip(mipsN, th): m[..., 3] = t
write_dds(os.path.join(out, 'pes_grass_blades_n.dds'), mipsN)
from PIL import Image
prev = np.concatenate([img[..., :3] * img[..., 3:4] + (1 - img[..., 3:4]) * 0.08, nrm[..., :3]], 0)
Image.fromarray((np.clip(prev, 0, 1) * 255).astype(np.uint8)).save(os.path.join(out, 'pes_grass_blades_preview.png'))
print('ok cobertura por banda', [round(float((t[0][..., 3] > 0.3).mean()), 3) for t in tufts])
