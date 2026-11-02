# PES21 FX (02/11) - muelas 3D del fondo de la boca (mouth3d.py) y su textura propia (make_oral.py).
# Agustin: 'que las muelas sean 3d' (v16) y 'no me convencen las texturas ni la forma de las muelas de abajo' (v17).
# Las muelas del fondo no estan pintadas en las tiras de Konami (ahi la tira es solo encia), asi que se arman aparte, con una
# forma de muela de verdad: corona mas ancha en el tercio del cuello, cuello angosto, mesa oclusal mas chica que la corona,
# cuspides (abajo 5 en la primera muela y 4 en la segunda, arriba 4), surco central, surcos vestibular / lingual y fosas.
# La textura va en un cuadrado libre del atlas (MOLAR_BOX): arriba la banda de las paredes (vuelta completa, cuello -> borde),
# abajo la cara oclusal vista de arriba; la pinta paint() con el mismo campo de cuspides y surcos que la geometria.
import numpy as np

MM = 0.001
MOLAR_BOX = (0.900, 0.990, 0.633, 0.725)   # u0, u1, v0, v1 en el atlas: libre (fuera de la caja uv de la lengua u .295-.715)
WALL_V = (0.0, 0.46)                       # parte de la caja (en v) para las paredes: 0 = borde oclusal, 0.46 = cuello
OCC_C, OCC_R = (0.5, 0.76), 0.23           # la cara oclusal: centro y radio (en la caja) del cuadrado x, y en -1..1

# cuspides (x mesio-distal, -1 = mesial; y vestibulo-lingual, +1 = hacia el carrillo; alto en mm)
CUSPS = {
    'lo1': [(-0.50, 0.60, 0.95), (0.08, 0.64, 0.85), (0.64, 0.40, 0.60), (-0.45, -0.60, 1.10), (0.40, -0.60, 1.00)],
    'lo2': [(-0.45, 0.60, 0.90), (0.45, 0.60, 0.80), (-0.45, -0.60, 1.00), (0.45, -0.60, 0.90)],
    'up1': [(-0.45, 0.62, 0.95), (0.45, 0.60, 0.85), (-0.35, -0.58, 1.15), (0.52, -0.58, 0.65)],
    'up2': [(-0.42, 0.60, 0.90), (0.42, 0.55, 0.75), (-0.28, -0.58, 1.00), (0.55, -0.55, 0.45)],
}
# surcos vestibular / lingual (posicion en x entre las cuspides)
GROOVES = {'lo1': ([-0.20, 0.38], [-0.02]), 'lo2': ([0.0], [0.0]), 'up1': ([0.0], [0.15]), 'up2': ([0.0], [0.2])}

def sst(e0, e1, x):
    t = np.clip((np.asarray(x, float) - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)

def fields(kind, x, y):
    """alto oclusal (mm, relativo al borde) y mascaras de surco / fosa / punta de cuspide en (x, y) normalizados"""
    r2 = np.clip(x * x + y * y, 0, 1.6)
    base = -0.75 * (1 - np.clip(r2, 0, 1)) ** 1.5 - 0.35 * sst(0.75, 1.15, np.sqrt(r2))   # cuenca al medio; afuera de las cuspides cae hacia la pared
    cusp = np.zeros_like(x); tip = np.zeros_like(x)
    for cx, cy, a in CUSPS[kind]:
        g = np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / 0.13)
        cusp += a * g; tip = np.maximum(tip, np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / 0.03))
    ends = sst(0.95, 0.65, np.abs(x))
    central = np.exp(-(y / 0.11) ** 2) * ends                                    # surco central mesio-distal
    gb, gl = GROOVES[kind]
    side = np.zeros_like(x)
    for gx in gb: side = np.maximum(side, np.exp(-((x - gx) / 0.08) ** 2) * sst(0.05, 0.35, y) * sst(1.05, 0.8, y))
    for gx in gl: side = np.maximum(side, np.exp(-((x - gx) / 0.08) ** 2) * sst(-0.05, -0.35, y) * sst(-1.05, -0.8, y))
    fossa = np.zeros_like(x)
    for fx in (-0.55, 0.0, 0.55): fossa = np.maximum(fossa, np.exp(-((x - fx) ** 2 + y ** 2) / 0.012))
    groove = np.maximum(np.maximum(central, side), fossa)
    h = base + cusp - 0.45 * central - 0.35 * side - 0.30 * fossa
    return h * MM, groove, tip

def geometry(c, t, bdir, up, L, Wb, Hc, sink, kind, seed=0, na=16, nb=6):
    """c: centro del cuello (m); t: mesio-distal (de adelante hacia atras); bdir: hacia el carrillo; up: arcada de arriba
    (la corona apunta hacia abajo); L / Wb: largo y ancho; Hc: alto del cuello al borde oclusal; sink: cuanto se mete en la
    encia. Devuelve P, UV, F (orden de Fox: horario visto desde afuera)."""
    rng = np.random.default_rng(3000 + seed)
    ax = np.array([0.0, -1.0 if up else 1.0, 0.0])
    t = t / np.linalg.norm(t); bdir = bdir / np.linalg.norm(bdir)
    th = np.linspace(0, 2 * np.pi, na + 1) + np.pi                             # con la costura repetida (uv), del lado de la lengua
    pw = 2.5
    cy = np.sign(np.cos(th)) * np.abs(np.cos(th)) ** (2 / pw)                   # +1 carrillo / -1 lengua
    cx = np.sign(np.sin(th)) * np.abs(np.sin(th)) ** (2 / pw)                   # mesio-distal
    jit = 1 + rng.uniform(-0.03, 0.03)
    b = np.linspace(0, 1, nb) ** 0.85
    hh = -sink + (Hc + sink) * b
    # ancho: cuello angosto, maximo en el tercio del cuello (del lado del carrillo) / en el medio (lengua), mesa oclusal ~60 %
    wx = 0.84 + 0.16 * sst(0.0, 0.45, b) - 0.07 * sst(0.6, 1.0, b)
    wy_b = 0.80 + 0.20 * sst(0.0, 0.30, b) - 0.16 * sst(0.45, 1.0, b)
    wy_l = 0.82 + 0.18 * sst(0.0, 0.50, b) - 0.12 * sst(0.60, 1.0, b)
    tilt = (-0.10 if not up else 0.04) * b ** 2                                 # las de abajo se inclinan hacia la lengua
    rim_h, _, _ = fields(kind, cx * 0.98, cy * 0.98)
    rim_h = rim_h - 0.15 * MM
    P = []; UV = []
    du = MOLAR_BOX[1] - MOLAR_BOX[0]; dv = MOLAR_BOX[3] - MOLAR_BOX[2]
    for k in range(nb):
        wy = np.where(cy > 0, wy_b[k], wy_l[k])
        lx = cx * wx[k] * jit; ly = cy * wy + tilt[k]
        h = hh[k] + (rim_h if k == nb - 1 else 0.0) * sst(0.6, 1.0, b[k])
        P.append(c[None] + (lx * L / 2)[:, None] * t[None] + (ly * Wb / 2)[:, None] * bdir[None] + np.reshape(h, (-1, 1)) * ax[None])
        UV.append(np.stack([MOLAR_BOX[0] + du * (th - np.pi) / (2 * np.pi), np.full(na + 1, MOLAR_BOX[2] + dv * (WALL_V[0] + (WALL_V[1] - WALL_V[0]) * (1 - b[k])))], 1))
    # mesa oclusal: anillos hacia adentro sobre el campo de cuspides y surcos. El primero repite el borde de la pared con la uv
    # de la cara oclusal (sin eso, la franja entre la pared y la mesa estiraba la textura de un lado al otro de la caja)
    wt = (wx[-1], wy_b[-1], wy_l[-1])
    P.append(P[-1].copy())
    UV.append(np.stack([MOLAR_BOX[0] + du * (OCC_C[0] + OCC_R * cx * 0.98), MOLAR_BOX[2] + dv * (OCC_C[1] + OCC_R * cy * 0.98)], 1))
    for f in (0.84, 0.62, 0.38, 0.14):
        xn = cx * f; yn = cy * f
        hz, _, _ = fields(kind, xn, yn)
        wy = np.where(cy > 0, wt[1], wt[2])
        lx = xn * wt[0] * jit; ly = yn * wy + tilt[-1]
        P.append(c[None] + (lx * L / 2)[:, None] * t[None] + (ly * Wb / 2)[:, None] * bdir[None] + np.reshape(Hc + hz, (-1, 1)) * ax[None])
        UV.append(np.stack([MOLAR_BOX[0] + du * (OCC_C[0] + OCC_R * xn), MOLAR_BOX[2] + dv * (OCC_C[1] + OCC_R * yn)], 1))
    nr = len(P)
    hz0, _, _ = fields(kind, np.zeros(1), np.zeros(1))
    P.append((c + (L / 2 * 0) * t + (tilt[-1] * Wb / 2) * bdir + (Hc + hz0[0]) * ax)[None])
    UV.append(np.array([[MOLAR_BOX[0] + du * OCC_C[0], MOLAR_BOX[2] + dv * OCC_C[1]]]))
    P = np.vstack(P); UV = np.vstack(UV)
    m = na + 1
    idx = np.arange(nr * m).reshape(nr, m)
    F = []
    for i in range(nr - 1):
        if i == nb - 1: continue   # pared -> copia del borde: mismo lugar, sin caras
        for j in range(na):
            a, b_, cc, d = idx[i, j], idx[i, j + 1], idx[i + 1, j + 1], idx[i + 1, j]
            F += [[a, b_, cc], [a, cc, d]]
    ci = nr * m
    for j in range(na): F.append([idx[-1, j], idx[-1, j + 1], ci])
    F = np.array(F)
    n = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    cen = c + 0.5 * Hc * ax
    if (np.sign(((P[F].mean(1) - cen) * n).sum(1))).sum() < 0: F = F[:, ::-1]
    return P, UV, F[:, ::-1]

def paint(nx_, ny_, enamel, seed=5):
    """textura de la caja MOLAR_BOX (n x n px): color, alto fino (para la normal), mascara de surcos y de puntas.
    Paredes: esmalte con el cuello un poco mas amarillo / opaco y el borde mas claro, vetas verticales suaves.
    Cara oclusal: esmalte algo mas gris, surcos y fosas marron oscuro (manchados), puntas de las cuspides gastadas (claras)."""
    from scipy import ndimage as ndi
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:ny_, 0:nx_].astype(np.float32)
    yy /= (ny_ - 1); xx /= (nx_ - 1)
    def nz(s): g = ndi.gaussian_filter(rng.standard_normal((ny_, nx_)).astype(np.float32), s, mode='wrap'); return g / (g.std() + 1e-6)
    en = np.asarray(enamel, np.float32)
    dentin = en * np.array([1.0, 0.94, 0.80])
    # paredes (v 0 .. WALL_V[1] + margen): b = 1 en el borde oclusal, 0 en el cuello
    wall = yy <= WALL_V[1] + 0.03
    bw = 1 - np.clip((yy - WALL_V[0]) / (WALL_V[1] - WALL_V[0]), 0, 1)
    cervical = sst(0.35, 0.0, bw)
    c_w = en[None, None] * (0.97 + 0.04 * sst(0.5, 1.0, bw))[..., None]
    c_w = c_w * (1 - 0.35 * cervical[..., None]) + dentin[None, None] * 0.92 * (0.35 * cervical[..., None])
    vet = nz(1.2) * 0.5 + nz(0.6) * 0.5
    streak = ndi.gaussian_filter(rng.standard_normal((1, nx_)).astype(np.float32), (0, 2.0), mode='wrap')
    streak = np.repeat(streak / (streak.std() + 1e-6), ny_, 0)
    c_w = c_w * (1 + 0.010 * vet + 0.005 * streak)[..., None]   # (02/11 v18) sin vetas verticales marcadas (parecia madera)
    perik = np.sin(bw * 2 * np.pi * 22 + 0.6 * nz(4.0)) * sst(0.15, 0.6, bw)
    h_w = 0.25 * perik + 0.15 * vet
    # cara oclusal
    X = (xx - OCC_C[0]) / OCC_R; Y = (yy - OCC_C[1]) / OCC_R
    occ = yy > WALL_V[1] + 0.03
    # surcos comunes a todas las muelas (la misma caja para las 8): central ondulado y fosas; las cuspides las da la geometria
    wob = 0.05 * np.sin(X * 7.0 + 1.3) + 0.012 * nz(5.0)
    central = np.exp(-((Y - wob) / 0.075) ** 2) * sst(0.85, 0.55, np.abs(X))
    fossa = np.zeros_like(X)
    for fx in (-0.5, 0.0, 0.5): fossa = np.maximum(fossa, np.exp(-((X - fx) ** 2 + (Y - wob) ** 2) / 0.008))
    side = np.zeros_like(X)
    for gx in (-0.05,):
        side = np.maximum(side, np.exp(-((X - gx - 0.04 * Y) / 0.05) ** 2) * sst(0.08, 0.3, np.abs(Y)) * sst(0.8, 0.55, np.abs(Y)))
    gr = np.maximum(np.maximum(central, fossa), 0.6 * side) * sst(1.1, 0.9, np.sqrt(X * X + Y * Y))
    # (02/11 v18) color: surcos mas suaves y cortados (no un dibujo); el surco de costado solo en el relieve
    grc = ndi.gaussian_filter(np.maximum(central, fossa) * sst(1.1, 0.9, np.sqrt(X * X + Y * Y)), 1.5) * sst(-1.4, 0.2, nz(6.0))
    tip = sst(0.35, 0.75, np.sqrt(X * X + Y * Y)) * sst(1.0, 0.8, np.sqrt(X * X + Y * Y))
    stain = 0.50 * sst(0.10, 0.95, grc)
    c_o = en[None, None] * 0.96 * (1 + 0.012 * nz(2.0))[..., None]
    c_o = c_o * (1 - stain[..., None]) + (en * np.array([0.66, 0.60, 0.50]))[None, None] * stain[..., None]
    c_o = c_o * (1 + 0.04 * tip)[..., None]
    h_o = -1.5 * ndi.gaussian_filter(gr, 1.0) + 0.10 * nz(1.2)
    col = np.where(occ[..., None], c_o, c_w); h = np.where(occ, h_o, h_w); groove = np.where(occ, stain, 0.0)
    return np.clip(col, 0, 1), h.astype(np.float32), groove.astype(np.float32), (cervical * wall).astype(np.float32)
