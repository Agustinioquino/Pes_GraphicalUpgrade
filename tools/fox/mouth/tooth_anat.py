# PES21 FX (09/11) - anatomia de la cara de adelante de los dientes, compartida por el modelo (mouth3d.py, con pocos poligonos:
# solo la forma grande) y el normal horneado (make_oral.py: el detalle fino + lo que el modelo no alcanza a mostrar).
# cx: -1..1 a lo ancho, bb: 0 cuello .. 1 borde, kind 'IC' 'I' 'C' 'P', mes: lado de cx hacia el centro de la boca. Alto en mm.
import numpy as np

def sst(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def macro(cx, bb, kind, mes):
    ax = np.abs(cx); H = np.zeros(np.broadcast(cx, bb).shape)
    if kind in ('IC', 'I'):
        H = H - 0.055 * (np.exp(-((cx - 0.34) / 0.10) ** 2) + np.exp(-((cx + 0.34) / 0.10) ** 2)) * sst(0.30, 0.75, bb)   # surcos de desarrollo
        H = H + 0.030 * np.cos(3 * np.pi * cx / 2) ** 2 * sst(0.82, 0.97, bb) * (1 - ax)                                     # mamelones gastados
    if kind == 'C':
        H = H + 0.07 * np.exp(-((cx - 0.12 * mes) / 0.22) ** 2) * sst(0.15, 0.85, bb)                                       # cresta del canino
        H = H - 0.03 * np.exp(-((ax - 0.50) / 0.12) ** 2) * sst(0.3, 0.9, bb)
    if kind == 'P':
        H = H + 0.06 * np.exp(-(cx / 0.25) ** 2) * sst(0.1, 0.9, bb)                                                        # cresta vestibular
        H = H - 0.03 * np.exp(-((ax - 0.45) / 0.12) ** 2) * sst(0.4, 1.0, bb)
    H = H + 0.045 * np.exp(-((ax - 0.80) / 0.07) ** 2) * sst(0.15, 0.6, bb)                                                # crestas marginales
    H = H - 0.10 * sst(0.86, 1.0, ax)                                                                                     # troneras
    H = H + 0.05 * np.exp(-((bb - 0.12) / 0.07) ** 2)                                                                      # cresta cervical
    H = H - 0.04 * sst(0.90, 1.0, bb)                                                                                     # borde redondeado
    return H * sst(-0.05, 0.10, bb)

def low(cx, bb, kind, mes, n=41):
    """la parte de macro() que el modelo de pocos poligonos alcanza a mostrar (promedio en una ventana de +-0.15 a lo ancho y
    +-0.06 a lo alto): el normal horneado lleva macro() - low() para no repetirla"""
    k = np.linspace(-1, 1, 7); acc = 0.0; wsum = 0.0
    for a in k:
        for b in k:
            w = np.exp(-2 * (a * a + b * b)); acc = acc + w * macro(cx + 0.15 * a, bb + 0.06 * b, kind, mes); wsum += w
    return acc / wsum
