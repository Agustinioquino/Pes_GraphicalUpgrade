# PES21 FX - atlas del PASTO NATURAL (11/11, Agustin: "rehace y guarda el pasto natural como un archivo aparte con texturas nuevas
# y comportamiento distinto"). Archivos propios: textures\pes_grass_natural.dds y pes_grass_natural_n.dds (t11 / t12 del PS 18afff,
# por PES21FX_rules.ini). El artificial sigue con pes_grass_blades (tools/make_grass_atlas.py), que NO se toca.
# Diferencias con el atlas viejo (manojos de hojas gordas fundidas en una base maciza, sin dibujo propio):
#   - hojas SUELTAS hasta la raiz: cada hoja nace de su propio punto a lo ancho de la tarjeta (no de un nudo central)
#   - el doble de resolucion por tarjeta (1024 px): la hoja mide 16-50 px de ancho y lleva su dibujo (nervio, venas paralelas,
#     fibras, manchas, borde)
#   - un NUMERO POR HOJA (B) y la ALTURA REAL en la hoja (B del normal): el rocio, las puntas, el tono y el mojado caen hoja por hoja
#   - GROSOR de lamina fina con nervio y venas gruesos (A del normal): a contraluz se enciende toda la lamina y se ven las venas
#   - cancha recien cortada: parte de las hojas largas termina en un CORTE de la cortadora (punta roma en diagonal, borde deshilachado
#     un poco mas claro); las cortas (rebrote) siguen en punta
# Atlas 4096 x 2048 en tarjetas de 512 (11/11 d, Agustin: "que el atlas no sea solo 4 texturas que se repitan"): columna = banda * 2 +
# (variante & 1), fila = tipo * 2 + (variante >> 1) (tipo 0 hoja fina, 1 hoja ancha; x412 = fraccion de anchas): 4 variantes por banda y
# tipo, y el PS espeja la mitad de las tarjetas: 8 dibujos distintos por banda en vez de 1
#   pes_grass_natural.dds    R = claridad (0.5 neutro), G = oclusion raiz->punta (el PS usa 2*G), B = numero de la hoja (0..1), A = alfa
#   pes_grass_natural_n.dds  R = normal a lo ancho (tambien da la posicion a lo ancho de la hoja), G = normal a lo largo,
#                            B = altura en la hoja (0 raiz .. 1 punta), A = grosor (0.25 lamina .. 1 raiz / nervio)
import numpy as np, struct, sys, os
from scipy.ndimage import distance_transform_edt, gaussian_filter

S = 512    # (11/11 d) tarjetas de 512: 8 x 4 = 32 variantes en el mismo 4096 x 2048 (antes 4 x 2 de 1024)
NVAR = 4   # variantes por banda y tipo de hoja: el PS elige una por tarjeta (v8.z) y la puede espejar
# nombre, hojas largas, cortas, largo L, largo S, ancho, inclinacion maxima (grados), caida, semilla
FINA = [
    ('corto',  24, 14, (0.38, 0.66), (0.20, 0.36), (0.022, 0.034), 20, (0.02, 0.10), 103),
    ('medio',  22, 12, (0.55, 0.92), (0.28, 0.48), (0.024, 0.038), 24, (0.05, 0.20), 107),
    ('largo',  20, 10, (0.72, 0.97), (0.34, 0.52), (0.026, 0.040), 26, (0.10, 0.32), 111),
    ('medioB', 22, 12, (0.52, 0.90), (0.26, 0.46), (0.024, 0.037), 22, (0.05, 0.22), 119),
]
ANCHA = [
    ('corto B', 15, 8, (0.38, 0.66), (0.20, 0.36), (0.032, 0.046), 18, (0.03, 0.12), 123),
    ('medio B', 14, 7, (0.55, 0.92), (0.28, 0.48), (0.036, 0.050), 22, (0.06, 0.24), 129),
    ('largo B', 13, 6, (0.72, 0.97), (0.34, 0.52), (0.038, 0.052), 24, (0.12, 0.36), 131),
    ('medio B2', 14, 7, (0.52, 0.90), (0.26, 0.46), (0.034, 0.048), 20, (0.06, 0.26), 137),
]

def noise1(n, seed, sig):
    r = np.random.default_rng(seed); g = r.random(n).astype(np.float32)
    g = gaussian_filter(g, sig, mode='wrap'); return (g - g.mean()) / (g.std() + 1e-6)

def noise2(h, w, seed, sig):
    r = np.random.default_rng(seed); g = r.random((h, w)).astype(np.float32)
    g = gaussian_filter(g, sig, mode='wrap'); return (g - g.mean()) / (g.std() + 1e-6)

def make_tuft(nbL, nbS, lenL, lenS, wid, lean, droopR, seed, wide=False):
    N = S; rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    cov = np.zeros((N, N), np.float32); R = np.full((N, N), 0.5, np.float32); G = np.full((N, N), 0.5, np.float32)
    B = np.full((N, N), 0.5, np.float32); NX = np.zeros((N, N), np.float32); NY = np.zeros((N, N), np.float32)
    T = np.zeros((N, N), np.float32); TH = np.full((N, N), 0.5, np.float32)
    nb = nbL + nbS
    # raices repartidas a lo ancho de la tarjeta (con azar), no en un nudo: hojas sueltas hasta el suelo
    # (11/11 b, Agustin: "le falta mas naturalidad") macollos: 1 a 4 hojas nacen de la misma vaina y se abren distinto; cada hoja con su
    # curva propia (cubica: derecha, en S, o doblada por el peso), ancho aparente distinto (la hoja vista de canto se ve angosta), raiz a
    # distinta altura (unas nacen detras de otras) y un temblor leve a lo largo
    nT = max(3, int(round(nb / 2.3)))
    tslots = (np.arange(nT) + 0.5) / nT; rng.shuffle(tslots)
    troots = 0.08 + 0.84 * np.clip(tslots + rng.uniform(-0.4, 0.4, nT) / nT, 0, 1)
    tlean = rng.uniform(-lean, lean, nT) * 0.7   # (11/11 c) mas erguido, como las fotos de Agustin
    owner = np.sort(rng.integers(0, nT, nb))
    ids = (np.argsort(np.argsort(rng.random(nb))) + 0.5) / nb      # numero por hoja, bien repartido en 0..1
    order = np.argsort(rng.random(nb))
    params = []
    for i in range(nb):
        short = i >= nbL
        tk = owner[i]
        length = rng.uniform(*(lenS if short else lenL)) * N * rng.uniform(0.85, 1.08)
        width = rng.uniform(*wid) * N * (0.85 if short else 1.0) * rng.uniform(0.62, 1.0)   # de canto se ve angosta
        x0 = troots[tk] * N + rng.uniform(-0.012, 0.012) * N; y0 = N + 3.0 + rng.uniform(0.0, 0.06) * N * (1.0 if short else 0.4)
        a = np.radians(tlean[tk] + rng.uniform(-10, 10) + (troots[tk] - 0.5) * 10.0)
        kind = rng.random()
        droopK = rng.uniform(*droopR) * (2.4 if kind < 0.12 else 1.0)            # 12 %: hoja doblada por el peso
        droop = droopK * length * np.sign(a + 1e-3 * rng.uniform(-1, 1))
        tx = x0 + np.sin(a) * length + droop
        lim0, lim1 = 0.03 * N + width, 0.97 * N - width
        tx = min(max(tx, lim0), lim1)
        ty = y0 - np.cos(a) * length * (0.95 - 0.30 * min(abs(tx - x0) / max(length, 1.0), 1.0) - (0.25 if kind < 0.12 else 0.0))
        sK = rng.uniform(-0.10, 0.10) * length if kind > 0.6 else 0.0           # 40 %: curva en S suave
        c1 = (x0 + (tx - x0) * 0.10 + sK, y0 - (y0 - ty) * rng.uniform(0.35, 0.50))
        c2 = (x0 + (tx - x0) * rng.uniform(0.45, 0.75) - sK, y0 - (y0 - ty) * rng.uniform(0.80, 1.05))
        cut = (not short) and kind >= 0.12 and rng.random() < (0.60 if wide else 0.70)   # hoja cortada por la cortadora
        tone = rng.uniform(-0.09, 0.09) + (0.035 if short else 0.0)               # las cortas (rebrote) algo mas claras
        params.append((x0, y0, c1, c2, tx, ty, width, tone, int(rng.integers(0, 100000)), cut, rng.uniform(-1, 1)))
    depth_rank = np.zeros(nb)
    for k, i in enumerate(order): depth_rank[i] = k / max(nb - 1, 1)
    for k, i in enumerate(order):
        x0, y0, c1, c2, tx, ty, width, tone, sd, cut, cutAng = params[i]
        ts = np.linspace(0, 1, 1800).astype(np.float32)
        u = 1 - ts
        px = u ** 3 * x0 + 3 * u * u * ts * c1[0] + 3 * u * ts * ts * c2[0] + ts ** 3 * tx
        py = u ** 3 * y0 + 3 * u * u * ts * c1[1] + 3 * u * ts * ts * c2[1] + ts ** 3 * ty
        px = px + noise1(1800, sd + 5, 90.0) * width * 0.035 * np.sin(ts * np.pi)   # temblor leve a lo largo (la hoja no es una curva perfecta)
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
        # perfil de la hoja: vaina angosta en la raiz, lamina mas ancha a 1/3 y punta afilada
        prof = (0.50 + 0.50 * np.clip(bt / 0.22, 0, 1) ** 0.8) * (1 - bt) ** 0.62 * (1 + 0.18 * np.sin(bt * np.pi))
        if cut:   # punta cortada: la hoja casi no se afina y termina en un corte en diagonal (no en punta)
            prof = (0.50 + 0.50 * np.clip(bt / 0.22, 0, 1) ** 0.8) * (1 - 0.35 * bt) * (1 + 0.12 * np.sin(bt * np.pi))
        w = width * prof * 0.5 + 0.6
        s = np.clip(bs / np.maximum(w, 1e-3), -1, 1)
        alpha = np.clip((w - best) / 1.1, 0, 1)
        if cut:
            alpha = alpha * (bt + 0.03 * cutAng * s < 0.985)          # corte limpio y recto de la cuchilla (algo en diagonal)
        inside = alpha > 0.02
        si = np.clip((s * 0.5 + 0.5) * 255, 0, 255).astype(int); ti = np.clip(bt * 1023, 0, 1023).astype(int)
        nv = (9 if wide else 5) + sd % 3                                         # venas paralelas a cada lado del nervio
        veins = np.cos(s * np.pi * nv) ** 2                                       # 1 sobre la vena
        veinK = np.clip(veins * 2.0 - 1.0, 0, 1) ** 2 * (1.0 - np.abs(s) ** 3)
        fib = noise1(256, sd, 1.2)[si] * 0.035 + noise1(256, sd + 7, 4.0)[si] * 0.03   # fibras finas a lo largo (varian a lo ancho)
        along = noise1(1024, sd + 1, 30.0)[ti] * 0.05 + noise1(1024, sd + 2, 6.0)[ti] * 0.02
        blot = noise2(64, 256, sd + 3, 2.2)[np.clip((s * 0.5 + 0.5) * 63, 0, 63).astype(int), np.clip(bt * 255, 0, 255).astype(int)] * 0.04   # manchas
        edge = -np.clip(np.abs(s) - 0.78, 0, 1) * 0.55
        if cut: edge = edge + np.clip((bt + 0.03 * cutAng * s - 0.940) / 0.040, 0, 1) * 0.20   # el borde del corte se seca y queda mas claro: la punta cortada se nota
        if wide:
            # hoja ancha: canaleta en V (nervio hundido y oscuro), lomos claros, torsion lenta
            tw = np.sin(bt * 5.5 + float(sd % 11) * 0.6) * 0.5
            mid = -np.exp(-(s / 0.16) ** 2) * 0.15 + np.exp(-((np.abs(s) - 0.50) / 0.22) ** 2) * 0.07
            lightness = 0.5 + tone + mid + veinK * 0.075 + fib + along + blot + edge + 0.05 * (bt - 0.3) + 0.04 * tw * s
            nx = -s * 0.80 + np.sign(s) * np.exp(-(s / 0.22) ** 2) * 0.42 + tw * 0.22 + (veins - 0.5) * 0.10 * np.sign(s)
            ny = 0.10 * np.sin(bt * 7.0 + float(sd % 7)) * (0.4 + 0.6 * bt) + tw * 0.12
            lam = 0.30
        else:
            # hoja fina: nervio central saliente y claro, plegada apenas
            mid = np.exp(-(s / 0.13) ** 2) * 0.13
            lightness = 0.5 + tone + mid + veinK * 0.065 + fib + along + blot + edge + 0.06 * (bt - 0.3)
            nx = -s * 0.58 + np.sign(s) * np.exp(-(s / 0.16) ** 2) * 0.24 + (veins - 0.5) * 0.08 * np.sign(s)
            ny = 0.13 * np.sin(bt * 11.0 + float(sd % 7)) * (0.4 + 0.6 * bt)
            lam = 0.26
        ao = (0.40 + 0.60 * np.clip(bt / 0.50, 0, 1) ** 0.8) * (0.85 + 0.15 * depth_rank[i])
        # grosor: lamina fina, nervio y venas gruesos, vaina de la raiz gruesa, punta finisima
        thick = lam + 0.55 * np.exp(-(s / 0.14) ** 2) + 0.16 * veinK + 0.10 * (np.abs(s) > 0.85).astype(np.float32)
        thick = thick * (1.0 - 0.35 * bt) + 0.75 * np.clip(1.0 - bt / 0.18, 0, 1) ** 1.5
        blend = np.clip(alpha, 0, 1)
        def put(dst, val):
            cur = dst[ym0:ym1, xm0:xm1]; dst[ym0:ym1, xm0:xm1] = np.where(inside, cur * (1 - blend) + val * blend, cur)
        put(R, np.clip(lightness, 0, 1)); put(G, np.clip(ao * 0.5, 0, 0.5))
        cur = B[ym0:ym1, xm0:xm1]; B[ym0:ym1, xm0:xm1] = np.where(alpha > 0.3, ids[i], cur)   # el numero no se mezcla en el borde
        put(NX, nx); put(NY, ny); put(T, bt); put(TH, np.clip(thick, 0, 1))
        cov[ym0:ym1, xm0:xm1] = np.maximum(cov[ym0:ym1, xm0:xm1], alpha)
    opaque = cov > 0.3
    _, idx = distance_transform_edt(~opaque, return_indices=True)
    R, G, B, NX, NY, T, TH = [a[idx[0], idx[1]] for a in (R, G, B, NX, NY, T, TH)]
    nz = np.sqrt(np.clip(1 - NX * NX - NY * NY, 0.05, 1)); nl = np.sqrt(NX * NX + NY * NY + nz * nz); NX = NX / nl; NY = NY / nl
    return np.stack([R, G, B, cov], -1), np.stack([NX * 0.5 + 0.5, NY * 0.5 + 0.5, T, TH], -1)

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

def _tile(job):
    typ, bi, k, args = job
    return make_tuft(*args, wide=(typ == 1))

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    from concurrent.futures import ProcessPoolExecutor
    jobs = []
    for typ, bands in ((0, FINA), (1, ANCHA)):
        for bi, b in enumerate(bands):
            for k in range(NVAR):
                args = list(b[1:]); args[-1] = args[-1] + 1009 * k   # otra semilla por variante
                jobs.append((typ, bi, k, args))
    with ProcessPoolExecutor() as ex:
        res = list(ex.map(_tile, jobs))
    img = np.zeros((4 * S, 8 * S, 4), np.float32); nrm = np.zeros((4 * S, 8 * S, 4), np.float32)
    covs = []
    for (typ, bi, k, _), (ti, tn) in zip(jobs, res):
        r0 = (typ * 2 + (k >> 1)) * S; c0 = (bi * 2 + (k & 1)) * S
        img[r0:r0 + S, c0:c0 + S] = ti; nrm[r0:r0 + S, c0:c0 + S] = tn
        covs.append(round(float((ti[..., 3] > 0.3).mean()), 3))
    cov = img[..., 3]
    write_dds(os.path.join(out, 'pes_grass_natural.dds'), mips_coverage(img, 3))
    mipsN = mips_coverage(np.concatenate([nrm[..., :3], cov[..., None]], -1), 3)
    th = [nrm[..., 3].astype(np.float32)]
    while th[-1].shape[0] > 1 and th[-1].shape[1] > 1: th.append(half(th[-1]))
    for m, t in zip(mipsN, th): m[..., 3] = t
    write_dds(os.path.join(out, 'pes_grass_natural_n.dds'), mipsN)
    from PIL import Image
    a = img[..., 3:4]
    # vista previa: como quedaria la hoja (verde por la claridad y el numero de hoja), y el grosor a contraluz
    hue = img[..., 2:3]
    base = np.array([0.15, 0.33, 0.08]) * (1 - hue) + np.array([0.19, 0.36, 0.09]) * hue   # verde de cancha (el juego pone el suyo)
    look = base * (0.45 + 1.1 * img[..., 0:1]) * (0.35 + 1.3 * img[..., 1:2])
    prev = look * a + (1 - a) * np.array([0.10, 0.17, 0.06])
    Image.fromarray((np.clip(prev, 0, 1) * 255).astype(np.uint8)).resize((2048, 1024), Image.LANCZOS).save(os.path.join(out, 'pes_grass_natural_preview.png'))
    print('ok cobertura por tarjeta', covs)
