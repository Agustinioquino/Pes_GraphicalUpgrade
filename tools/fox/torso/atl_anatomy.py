# PES21 FX (27/10) - RELIEVE DEL ATLETICO: altura (m, + = bulto) de la musculatura marcada en 3D sobre el torso, para el normal map
# del atletico (progresivo con su peso en el PS) y sus sombras en el color. Referencia: cuerpo de futbolista muy definido (abdominales
# en 6 bloques con la linea alba y las intersecciones, linea semilunar, borde inferior del pectoral, serratos, oblicuos sobre la
# cresta iliaca; atras: columna con los erectores, trapecio en rombo (borde inferior), espina de la escapula, infraespinoso y redondo
# mayor, borde del dorsal ancho; trapecio alto). Se evalua en 3D (continuo a traves de los cortes de UV) y se lleva a cada texel
# con su marco tangente; cerca del hombro y del cuello baja a 0 (ahi el brazo / la cara no lo tienen).
import numpy as np
from torso_variants import ss, g, ZC, AX, BZ, NAVEL, NIPPLE

def _frame(P):
    x = np.abs(P[..., 0]); y = P[..., 1]; z = P[..., 2]
    dx = x / (AX * AX); dz = (z - ZC) / (BZ * BZ)
    L = np.sqrt(dx * dx + dz * dz) + 1e-6
    rz = dz / L
    return x, y, z, rz, ss(-0.1, 0.5, rz), ss(-0.1, 0.5, -rz)

def seg(x, y, ax, ay, bx, by):
    ux, uy = bx - ax, by - ay; L2 = ux * ux + uy * uy
    t = np.clip(((x - ax) * ux + (y - ay) * uy) / L2, 0.0, 1.0)
    return np.hypot(x - (ax + t * ux), y - (ay + t * uy)), t

def groove(d, w, depth): return -depth * g(d / w)
def ridge(d, w, h): return h * g(d / w)

def height(P):
    """solo lo que el normal map propio no trae marcado: serratos, flanco, la espalda y el trapecio alto (los abdominales y el
    pectoral del atletico salen del mapa propio reforzado, alineados con sus surcos)"""
    x, y, z, rz, front, back = _frame(P)
    h = np.zeros(x.shape)
    yn = NAVEL[1]
    # serratos: 4 dientes en el costado, oblicuos (suben hacia atras); u = arco en el costado (m), + adelante
    u = rz * 0.12
    sideF = ss(0.09, 0.12, x) * (1 - ss(0.45, 0.75, rz)) * ss(-0.45, -0.1, rz)
    for k in range(4):
        y0 = 1.285 - 0.028 * k
        dd, _ = seg(u, y, 0.030, y0 - 0.010, -0.010, y0 + 0.012)
        h += ridge(dd, 0.0070, 0.0013) * sideF * (1 - 0.15 * k)
    # flanco: oblicuo externo sobre la cresta iliaca
    h += ridge(np.hypot((x - 0.125) / 1.2, (y - (yn - 0.005)) / 1.8), 0.03, 0.0018) * ss(-0.5, 0.0, rz) * (1 - ss(0.6, 0.9, rz))
    # ---------- atras (formas anchas y redondeadas)
    h += ridge(np.abs(x - 0.030), 0.014, 0.0020) * back * ss(0.99, 1.04, y) * (1 - ss(1.20, 1.30, y))   # erectores
    h += groove(x, 0.008, 0.0012) * back * ss(1.0, 1.05, y) * (1 - ss(1.38, 1.45, y))                    # columna (spine_n ya la marca)
    # trapecio medio / bajo: masa en rombo y su borde inferior curvo (de la columna en y 1.21 a la escapula)
    xt = np.clip((y - 1.21) / 0.19, 0, 1); xb = 0.090 * np.sin(xt * np.pi / 2) ** 0.8
    inside = ss(0.004, -0.010, x - xb) * ss(1.20, 1.23, y) * (1 - ss(1.44, 1.48, y))
    h += 0.0016 * inside * back
    h += groove(x - xb, 0.009, 0.0014) * back * ss(1.21, 1.25, y) * (1 - ss(1.40, 1.44, y))
    # escapula: infraespinoso y redondo mayor (bultos), espina (cresta suave, corta), borde del dorsal ancho
    h += ridge(np.hypot((x - 0.122) / 1.25, (y - 1.368) / 1.1), 0.028, 0.0022) * back
    h += ridge(np.hypot((x - 0.158) / 1.2, (y - 1.306) / 1.1), 0.019, 0.0018) * back
    d, t = seg(x, y, 0.085, 1.418, 0.170, 1.448); h += ridge(d, 0.008, 0.0010) * back * np.sin(np.pi * t)
    d, t = seg(x, y, 0.178, 1.318, 0.125, 1.115); h += groove(d, 0.009, 0.0014) * back * ss(-0.7, -0.25, rz) * np.sin(np.pi * np.clip(t, 0, 1)) ** 0.5
    # ---------- trapecio alto (del cuello al hombro)
    h += ridge(np.hypot((x - 0.09) / 1.6, (y - 1.505) / 0.8), 0.03, 0.0026) * ss(-0.4, 0.1, -rz)
    return h

def tangent_offsets(G, k=1.0, eps=0.002, fade=None):
    """xy del normal map (marco tangente de cada texel, el del VS) del relieve height(), en los texels de G"""
    m = G["mask"]; ii = np.where(m)
    P = G["P"][ii]; Tg = G["Tg"][ii]; Bn = G["Bn"][ii]
    Tg = Tg / np.maximum(np.linalg.norm(Tg, axis=1, keepdims=True), 1e-9); Bn = Bn / np.maximum(np.linalg.norm(Bn, axis=1, keepdims=True), 1e-9)
    hT = (height(P + eps * Tg) - height(P - eps * Tg)) / (2 * eps)
    hB = (height(P + eps * Bn) - height(P - eps * Bn)) / (2 * eps)
    out = np.zeros(m.shape + (2,), np.float32); hh = np.zeros(m.shape, np.float32)
    w = fade[ii] if fade is not None else 1.0
    out[ii[0], ii[1], 0] = -k * hT * w; out[ii[0], ii[1], 1] = -k * hB * w
    hh[ii] = height(P) * w
    return out, hh
