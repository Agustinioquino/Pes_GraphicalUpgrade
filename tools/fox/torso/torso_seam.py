# PES21 FX (26/10 e) - COSTURAS del cuerpo de TorsoBody en el espacio de las texturas (hombro: torso <-> brazo; cuello).
# Las dos texturas (torsob_nrm y arm_nrm) se encuentran en el anillo del hombro con el mismo color y las mismas normales de
# malla, pero con DISTINTO detalle: el brazo tiene el doble de relieve y poros mas gruesos, el torso es casi liso ahi; y el
# albedo del brazo es un 4 % mas claro en los 1 .. 6 cm junto a la costura (una franja clara). Se veia la linea del hombro.
# Aca, en una franja de ~4 cm a cada lado de la costura, el detalle de las dos texturas pasa a ser EL MISMO: el micro relieve
# real de la piel del torso (la textura repetible de skin_detail.py) evaluado en 3D (triplanar sobre la posicion de reposo),
# que por construccion es continuo al cruzar la costura, encima del volumen suave de cada mapa (lo grande lo lleva la malla).
# Se escribe dentro de los mapas de correccion que el PS ya suma (pes_var_torso_base_n / pes_var_arm_base_n, t20):
#   final = base + dB + w * (vol + micro3D - (base + dB)),  w = 1 en la costura .. 0 a 4.5 cm
# Junto al cuello (la cara / el cuello del juego, texturas mas lisas) el detalle del torso baja a la mitad en los 2.5 cm de arriba.
# El albedo del brazo (la franja clara) lo corrige el PS de la piel (aa7d1833, w293) por la posicion de reposo.
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

TILE_PER_M = 4550.0 / 1024.0   # la textura de detalle (1024 px) mide 22.5 cm de piel (4550 texeles por metro, la escala del torso)

def ss(a, b, x):
    t = np.clip((np.asarray(x, float) - a) / (b - a), 0.0, 1.0); return t * t * (3 - 2 * t)

def shoulder_ring(T, A):
    """vertices (soldados) del torso que comparte con el brazo (los dos hombros)"""
    from scipy.spatial import cKDTree
    d, _ = cKDTree(A["Pw"]).query(T["Pw"])
    return T["Pw"][d < 1e-4]

def neck_ring(T):
    B = T["Pw"][T["boundary"]]
    return B[B[:, 1] > 1.45]

def _bilinear_wrap(img, u, v):
    H, W = img.shape[:2]
    x = (u * W - 0.5) % W; y = (v * H - 0.5) % H
    x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int); fx = (x - x0)[:, None]; fy = (y - y0)[:, None]
    x1 = (x0 + 1) % W; y1 = (y0 + 1) % H
    return (img[y0, x0] * (1 - fx) + img[y0, x1] * fx) * (1 - fy) + (img[y1, x0] * (1 - fx) + img[y1, x1] * fx) * fy

def micro3d(P, N, Tg, Bn, tile, scale=TILE_PER_M, k=4.0):
    """micro relieve (xy en el marco tangente de la textura: rojo = T, verde = B) continuo en 3D: la textura de detalle 'tile'
    (H, W, 2: pendiente en su propio marco u, v) leida triplanar en la posicion de reposo P (n, 3)"""
    w = np.abs(N) ** k; w /= np.maximum(w.sum(1, keepdims=True), 1e-9)
    planes = (((0, 0, 1), (0, 1, 0), 0), ((1, 0, 0), (0, 0, 1), 1), ((1, 0, 0), (0, 1, 0), 2))   # (eu, ev, eje de la normal)
    s3 = np.zeros_like(P)
    for eu, ev, ax in planes:
        eu = np.array(eu, float); ev = np.array(ev, float)
        u = (P @ eu) * scale; v = (P @ ev) * scale
        xy = _bilinear_wrap(tile, u % 1.0, v % 1.0)
        s3 += (xy[:, :1] * eu + xy[:, 1:2] * ev) * w[:, ax:ax + 1]
    s3 -= N * np.sum(s3 * N, 1, keepdims=True)
    return np.stack([np.sum(s3 * Tg, 1), np.sum(s3 * Bn, 1)], 1)

def stitch(G, Bd, dB, rings, tile, w_in=0.008, w_out=0.045, amp=None, neck=None, log=print):
    """dB (H, W, 2) = correccion del mapa (torso o brazo). rings = lista de (puntos (n, 3)) de las costuras a coser; neck = puntos
    del anillo del cuello (el detalle baja a la mitad en 2.5 cm) o None. amp = desvio del micro en la franja (por canal; None =
    el promedio del micro de los dos lados a 4 .. 6 cm de la costura lo pone quien llama). Devuelve dB nuevo"""
    from scipy.spatial import cKDTree
    mask = G["mask"]; ii = np.where(mask)
    P = G["P"][ii]; N = G["N"][ii]; Tg = G["Tg"][ii]; Bn = G["Bn"][ii]
    base = Bd["base"][ii]; vol = Bd["vol"][ii]
    R = np.concatenate(rings, 0)
    d, _ = cKDTree(R).query(P)
    w = 1.0 - ss(w_in, w_out, d)
    sel = np.where(w > 0.001)[0]
    out = dB.copy()
    if len(sel):
        m3 = micro3d(P[sel], N[sel], Tg[sel], Bn[sel], tile)
        if amp is not None:
            m3 *= amp / np.maximum(m3.std(0), 1e-9)
        cur = base[sel] + dB[ii][sel]
        tgt = vol[sel] + m3
        out[ii[0][sel], ii[1][sel]] += w[sel][:, None] * (tgt - cur)
        log("  costura: %d texeles en la franja, micro std %s, antes %s" % (len(sel), np.round(m3.std(0), 3), np.round((cur - vol[sel]).std(0), 3)))
    if neck is not None and len(neck):
        dn, _ = cKDTree(neck).query(P)
        wn = 1.0 - ss(0.004, 0.025, dn)
        sn = np.where(wn > 0.001)[0]
        if len(sn):
            cur = base[sn] + out[ii][sn]
            out[ii[0][sn], ii[1][sn]] += -0.5 * wn[sn][:, None] * (cur - vol[sn])
            log("  cuello: %d texeles, el detalle baja a la mitad junto al anillo" % len(sn))
    W = np.zeros(mask.shape); W[ii] = w
    return out, W

def band_amp(G, Bd, dB, rings, d0=0.04, d1=0.07):
    """desvio por canal del micro (base + dB - vol) a d0 .. d1 m de las costuras"""
    from scipy.spatial import cKDTree
    ii = np.where(G["mask"]); P = G["P"][ii]
    d, _ = cKDTree(np.concatenate(rings, 0)).query(P)
    s = (d > d0) & (d < d1)
    m = (Bd["base"][ii] + dB[ii] - Bd["vol"][ii])[s]
    return m.std(0)
