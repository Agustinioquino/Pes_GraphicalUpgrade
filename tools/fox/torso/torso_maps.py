# PES21 FX (26/10) - TEXTURAS de las variantes del torso: mapas normales delta en el espacio UV del torso de TorsoBody
# (torsob_nrm: el torso ocupa v 0..0.47; los mapas cubren v 0..0.5, el PS de la piel lee (u, 2v)).
#
# Cada variante es una altura (m) definida en el cuerpo en reposo con marcas anatomicas medidas en la malla (ombligo, pezones,
# cresta iliaca, costillas, trapecio); por texel se toma la posicion de reposo, la normal y la tangente de la malla (uvbake.py) y
# la pendiente de la altura a lo largo de la tangente T y de la binormal B = cross(N, T) * w (la misma convencion que usa
# torsob_nrm: rojo = +T, verde = +B; medido contra el relieve esculpido de la malla, correlacion verde +0.39 en el frente).
# delta = (-dh/dT, -dh/dB): el PS lo suma al normal map de TorsoBody (como la definicion x172).
# Para que se vean naturales: los pliegues y las lineas ondulan un poco y cambian de profundidad a lo largo (ruido suave), los
# bordes se apagan de a poco y nada es una raya pareja.
#   robusto: pliegue de la panza sobre la cintura del short (y uno corto arriba a los costados), ombligo hundido y horizontal,
#            rollos de los costados, pliegues suaves de la espalda baja, pliegue de abajo del pecho caido, y ESTRIAS finas
#            (costados, espalda baja, bajo vientre, cerca de la axila: ademas un poco mas claras, mascara aparte).
#   atletico: trapecio marcado (borde con el cuello y con el hombro), cinturon de Adonis, serratos, lineas semilunares y linea
#             alba, borde de abajo del pectoral y los erectores de la espalda baja.
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from torso_variants import ss, g, ZC, AX, BZ, NAVEL, NIPPLE, torso_mask

class Noise:
    """ruido suave 3D: suma de senos con direcciones, frecuencias y fases al azar (valor ~ -1..1)"""
    def __init__(self, seed, freqs=(9.0, 17.0, 31.0), n=6):
        r = np.random.default_rng(seed)
        self.terms = []
        for f in freqs:
            for _ in range(n):
                d = r.normal(size=3); d /= np.linalg.norm(d)
                self.terms.append((d * f * 2 * np.pi * r.uniform(0.8, 1.25), r.uniform(0, 2 * np.pi), 1.0 / f))
        self.norm = np.sqrt(sum(a * a for _, _, a in self.terms) / 2.0)
    def __call__(self, P):
        v = np.zeros(P.shape[:-1])
        for k, ph, a in self.terms: v += a * np.sin(P @ k + ph)
        return v / (self.norm * 2.2)

N1, N2, N3 = Noise(11), Noise(23), Noise(37, freqs=(40.0, 70.0), n=5)

def _frame(P):
    x = np.abs(P[..., 0]); y = P[..., 1]; z = P[..., 2]
    dx = x / (AX * AX); dz = (z - ZC) / (BZ * BZ)
    L = np.sqrt(dx * dx + dz * dz) + 1e-6
    rz = dz / L
    front = ss(-0.2, 0.6, rz); back = ss(-0.2, 0.6, -rz); side = 1.0 - np.abs(rz)
    return x, y, z, rz, front, back, side

def mirrorP(P):
    """el ruido no es simetrico (cada lado distinto): se evalua con la X real"""
    return P

def seg_dist(x, y, ax, ay, bx, by):
    """distancia en el plano (x, y) al segmento A-B y posicion a lo largo (0..1)"""
    ux, uy = bx - ax, by - ay; L2 = ux * ux + uy * uy
    t = np.clip(((x - ax) * ux + (y - ay) * uy) / L2, 0.0, 1.0)
    return np.sqrt((x - ax - t * ux) ** 2 + (y - ay - t * uy) ** 2), t

def wobble(P, amp):
    """corrimiento suave (m) para que las lineas no sean rectas"""
    return amp * N1(P * 1.0)

def var(P, lo=0.65, hi=1.25):
    """profundidad que cambia a lo largo (factor lo..hi)"""
    return lo + (hi - lo) * (0.5 + 0.5 * np.clip(N2(P), -1, 1))

# ---------------------------------------------------------------- estrias (robusto)
def _striae_set(seed, n, center, spread, ang_deg, ang_sd, length, side_sign):
    """lista de estrias: (punto medio (x, y, z), direccion en el plano tangente aproximado, largo, ancho, profundidad, curva)"""
    r = np.random.default_rng(seed)
    out = []
    for _ in range(n):
        c = np.array(center) + r.normal(size=3) * np.array(spread)
        a = np.radians(ang_deg + r.normal() * ang_sd)
        out.append(dict(c=c, a=a, L=r.uniform(*length), w=r.uniform(0.0007, 0.0016), d=r.uniform(0.00012, 0.00028),
                        k=r.normal() * 6.0, sgn=side_sign))
    return out

def striae_list():
    S = []
    for sgn, seed in ((1, 101), (-1, 202)):
        # costados / rollos (casi horizontales, bajan un poco hacia adelante): en la superficie del costado se mide en (z, y)
        S += [dict(s, plane="side") for s in _striae_set(seed + 1, 15, (0.148, 1.105, 0.045), (0.004, 0.035, 0.035), -12, 14, (0.025, 0.065), sgn)]
        # espalda baja (horizontales, en (x, y) visto de atras)
        S += [dict(s, plane="back") for s in _striae_set(seed + 2, 9, (0.095, 1.095, -0.040), (0.025, 0.022, 0.0), 4, 10, (0.025, 0.050), sgn)]
        # bajo vientre a los costados (oblicuas hacia la ingle)
        S += [dict(s, plane="front") for s in _striae_set(seed + 3, 7, (0.092, 1.065, 0.160), (0.018, 0.016, 0.0), -38, 12, (0.020, 0.045), sgn)]
        # pecho cerca de la axila (oblicuas hacia la axila)
        S += [dict(s, plane="front") for s in _striae_set(seed + 4, 5, (0.150, 1.345, 0.110), (0.010, 0.020, 0.0), 35, 10, (0.018, 0.040), sgn)]
    return S

def striae_field(P, S):
    """altura (m, negativa) y mascara de color (0..1) de las estrias en los texels P"""
    h = np.zeros(P.shape[:-1]); m = np.zeros(P.shape[:-1])
    X = P[..., 0]; x = np.abs(X); y = P[..., 1]; z = P[..., 2]
    for s in S:
        sel_side = (X * s["sgn"]) > 0
        if s["plane"] == "side":   # coordenadas en la superficie del costado: u = z (adelante), v = y
            u = z - s["c"][2]; v = y - s["c"][1]; near = sel_side & (np.abs(x - s["c"][0]) < 0.03)
        else:                      # adelante / atras: u = x, v = y
            u = x - s["c"][0]; v = y - s["c"][1]; near = sel_side & ((z > 0.06) if s["plane"] == "front" else (z < 0.04))
        ca, sa = np.cos(s["a"]), np.sin(s["a"])
        a = u * ca + v * sa; b = -u * sa + v * ca            # a: a lo largo, b: de costado
        b = b - s["k"] * a * a                              # un poco curva
        L = s["L"]; w = s["w"]
        inside = near & (np.abs(a) < 0.6 * L) & (np.abs(b) < 4 * w)
        if not inside.any(): continue
        ai, bi = a[inside], b[inside]
        taper = (1 - ss(0.25 * L, 0.5 * L, np.abs(ai)))     # puntas finas
        wv = w * (0.7 + 0.6 * taper)
        prof = np.exp(-0.5 * (bi / wv) ** 2) * taper
        h[inside] -= s["d"] * prof
        m[inside] = np.maximum(m[inside], np.exp(-0.5 * (bi / (1.3 * wv)) ** 2) * taper)
    return h, m

_S = None
def height_robust(P, with_mask=False):
    global _S
    x, y, z, rz, front, back, side = _frame(P)
    h = np.zeros(x.shape)
    # 1. pliegue de la panza sobre la cintura del short (sube hacia los costados, ondula, profundidad variable) + rollo arriba
    yc = 1.040 + 0.022 * (x / 0.15) ** 2 + wobble(P, 0.0025)
    dy = y - yc
    h += (-0.0017 * var(P) * g(dy / 0.0055) + 0.0008 * g((dy - 0.014) / 0.010)) * (front + 0.6 * side) * (1 - ss(0.14, 0.17, x))
    #    pliegue corto mas arriba, solo hacia los costados (la panza se dobla al costado)
    yc2 = 1.088 + 0.010 * ((x - 0.11) / 0.04) ** 2 + wobble(P * 1.7, 0.002)
    h += -0.0009 * var(P * 1.3, 0.4, 1.2) * g((y - yc2) / 0.005) * g((x - 0.112) / 0.032) * (front + side)
    # 2. ombligo hundido y estirado en horizontal
    h += -0.0015 * g((y - NAVEL[1]) / 0.0025) * g(x / 0.017) * front
    # 3. rollo de los costados (arriba de la cadera): pliegue irregular + bulto
    yl = 1.050 + 0.012 * back + wobble(P * 1.3, 0.003)
    h += (-0.0018 * var(P * 0.8) * g((y - yl) / 0.0055) + 0.0007 * g((y - yl - 0.015) / 0.010)) * ss(0.10, 0.135, x) * (side + 0.5 * back)
    # 4. pliegues suaves de la espalda baja (no en la columna), cortados e irregulares
    for yb, sd in ((1.105, 3), (1.160, 5)):
        yy = yb + wobble(P * (1.0 + 0.3 * sd), 0.0025)
        h += -0.0010 * var(P * sd, 0.2, 1.3) * g((y - yy) / 0.0045) * back * ss(0.025, 0.05, x) * (1 - ss(0.11, 0.15, x))
    # 5. pecho caido: pliegue de abajo mas bajo y hondo, con el rollo de grasa encima
    yp = 1.252 + 0.018 * ((x - 0.070) / 0.060) ** 2 + wobble(P * 1.5, 0.0015)
    dyp = y - yp
    h += (-0.0021 * var(P * 0.9, 0.8, 1.15) * g(dyp / 0.0060) + 0.0007 * g((dyp - 0.013) / 0.010)) * g((x - 0.070) / 0.052) * front
    # 6. estrias
    if _S is None: _S = striae_list()
    hs, ms = striae_field(P, _S)
    h += hs
    return (h, ms) if with_mask else h

def height_athletic(P):
    x, y, z, rz, front, back, side = _frame(P)
    h = np.zeros(x.shape)
    # 1. cinturon de Adonis: de la cresta iliaca hacia la ingle, apenas curvo y mas hondo arriba; el oblicuo encima (del lado de
    #    afuera / arriba). Distancia CON SIGNO a la linea (sin quiebres: un quiebre o un "si / no" en la altura da un borde filoso)
    ax_, ay_, bx_, by_ = 0.126, 1.088, 0.046, 1.000
    ux, uy = bx_ - ax_, by_ - ay_; Lu = np.hypot(ux, uy)
    xw = x + wobble(P, 0.0015)
    t = ((xw - ax_) * ux + (y - ay_) * uy) / (Lu * Lu)
    ds = ((xw - ax_) * uy - (y - ay_) * ux) / Lu            # > 0 del lado de arriba / afuera
    dc = ds - 0.004 * np.sin(np.pi * np.clip(t, 0, 1))
    h += (-0.0018 * (1.15 - 0.45 * np.clip(t, 0, 1)) * var(P, 0.8, 1.15) * g(dc / 0.0040) + 0.0007 * g((dc - 0.008) / 0.007)) \
         * (front + 0.4 * side) * ss(-0.05, 0.20, t) * (1 - ss(0.85, 1.05, t))
    # 2. serratos: dedos en el costado bajo la axila, bandas que bajan hacia adelante; bultos suaves y distintos entre si
    s = (y - 1.20) + 0.45 * (z - 0.08) + wobble(P * 2.0, 0.0015)
    env = g((z - 0.075) / 0.028) * g((y - 1.255) / 0.030) * ss(0.115, 0.140, x) * (1 - back)
    h += 0.00035 * var(P * 3.0, 0.5, 1.3) * (0.5 + 0.5 * np.cos(2 * np.pi * s / 0.024)) ** 2 * env
    # 3. lineas semilunares (borde de afuera del recto) y linea alba, suaves e irregulares
    xs = 0.076 + 0.012 * ((y - 1.16) / 0.10) ** 2 + wobble(P * 1.4, 0.0015)
    rect = ss(1.065, 1.090, y) * (1 - ss(1.250, 1.275, y)) * front
    h += -0.0008 * var(P * 1.2, 0.5, 1.2) * g((x - xs) / 0.0035) * rect
    h += -0.0006 * var(P * 2.0, 0.6, 1.2) * g((x + wobble(P * 2.3, 0.0008)) / 0.0028) * rect
    # 4. borde de abajo del pectoral (surco y el pecho arriba)
    yp = 1.282 + 0.010 * ((x - 0.075) / 0.050) ** 2 + wobble(P * 1.5, 0.001)
    dyp = y - yp
    h += (-0.0014 * var(P, 0.8, 1.15) * g(dyp / 0.0035) + 0.0005 * g((dyp - 0.010) / 0.008)) * g((x - 0.075) / 0.048) * front
    # 5. trapecio: el borde con el cuello (adelante del trapecio, la "fosa" sobre la clavicula) y la cresta del cuello al hombro
    xt = np.clip(x, 0.05, 0.22)
    ytop = 1.552 - 0.40 * (xt - 0.05) ** 1.0 * 0.95       # linea de arriba del hombro (medida: 1.552 en x 0.04, 1.494 en x 0.22)
    trap_band = ss(0.055, 0.075, x) * (1 - ss(0.19, 0.23, x))
    #    fosa supraclavicular: adelante del trapecio, arriba de la clavicula
    h += -0.0015 * g((z - 0.035) / 0.010) * g((y - (ytop - 0.012)) / 0.012) * trap_band * (1 - back)
    #    masa del trapecio (sube y se redondea) y el surco con el deltoides/hombro
    h += 0.0014 * g((z + 0.005) / 0.020) * g((y - ytop) / 0.020) * g((x - 0.12) / 0.045)
    h += -0.0008 * g((x - 0.195) / 0.008) * g((y - 1.50) / 0.025) * (1 - front)
    #    trapecio medio e inferior en la espalda: su borde (diagonal hacia la columna) marcado
    d2, t2 = seg_dist(x, y, 0.13, 1.47, 0.02, 1.30)
    h += -0.0010 * var(P * 1.1, 0.6, 1.2) * g(d2 / 0.0050) * back * ss(0.05, 0.25, t2) * (1 - ss(0.85, 1.0, t2))
    # 6. espalda baja: erectores a los lados de la columna y el surco de la columna
    lb = ss(1.03, 1.06, y) * (1 - ss(1.22, 1.26, y)) * back
    h += (0.0008 * g((x - 0.032) / 0.012) - 0.0008 * g(x / 0.004)) * lb
    return h

def slopes(hfun, P, Tg, Bn, eps=0.0004):
    """(-dh/dT, -dh/dB) por diferencias centradas en 3D"""
    sT = (hfun(P + eps * Tg) - hfun(P - eps * Tg)) / (2 * eps)
    sB = (hfun(P + eps * Bn) - hfun(P - eps * Bn)) / (2 * eps)
    return np.stack([-sT, -sB], -1)

def build_maps(G, fade_tex, chunk=262144):
    """G = uvbake.bake(...). fade_tex (H, W): cuanto se mueve (costuras quietas). Devuelve (robusto (H, W, 2), atletico (H, W, 2),
    mascara de estrias (H, W))"""
    P, Tg, Bn, M = G["P"], G["Tg"], G["Bn"], G["mask"]
    H, W = M.shape
    k = (torso_mask(P.reshape(-1, 3)).reshape(H, W) * fade_tex * M)
    dR = np.zeros((H, W, 2)); dA = np.zeros((H, W, 2)); ms = np.zeros((H, W))
    ii = np.where(M)
    for a in range(0, len(ii[0]), chunk):
        sl = (ii[0][a:a + chunk], ii[1][a:a + chunk])
        p, t, b = P[sl], Tg[sl], Bn[sl]
        dR[sl] = slopes(height_robust, p, t, b)
        dA[sl] = slopes(height_athletic, p, t, b)
        ms[sl] = height_robust(p, with_mask=True)[1]
    dR *= k[..., None]; dA *= k[..., None]; ms *= k
    return np.clip(dR, -0.9, 0.9), np.clip(dA, -0.9, 0.9), np.clip(ms, 0, 1)
