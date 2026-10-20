# PES21 FX - analiza los pares A/B de capturas\ab: para cada grupo, cuanto cambia la imagen al apagarlo (A vs B),
# descontando el ruido (A vs A2: misma escena, mismo estado). Si A y A2 difieren mucho, la camara se movio: par descartado.
# Uso: read_ab.py <carpeta capturas\ab>
import sys, os, glob
import numpy as np
from PIL import Image
d = sys.argv[1] if len(sys.argv) > 1 else '.'
def load(p): return np.asarray(Image.open(p).convert('RGB')).astype(np.float32) / 255
print('%-44s %8s %8s %8s %8s  %s' % ('grupo', 'efecto', 'ruido', 'dLum', 'dContr', 'veredicto'))
for a in sorted(glob.glob(os.path.join(d, '*_A.jpg'))):
    b = a[:-6] + '_B.jpg'; a2 = a[:-6] + '_A2.jpg'
    if not os.path.exists(b) or os.path.getsize(a) < 60000: continue
    A, B = load(a), load(b)
    if A.shape != B.shape: continue
    eff = np.abs(A - B).mean()
    if os.path.exists(a2):
        A2 = load(a2); noise = np.abs(A - A2).mean()
    else:
        noise = float('nan')
    lum = lambda X: 0.2126 * X[..., 0] + 0.7152 * X[..., 1] + 0.0722 * X[..., 2]
    dl, dc = lum(A).mean() - lum(B).mean(), lum(A).std() - lum(B).std()
    name = os.path.basename(a)[7:-6]
    if noise != noise: v = 'sin A2 (version vieja)'
    elif noise > 0.03: v = 'DESCARTADO (camara movida)'
    elif eff < noise * 1.5 + 0.002: v = 'casi sin efecto'
    elif eff < noise * 4: v = 'efecto sutil'
    else: v = 'EFECTO CLARO'
    print('%-44s %8.4f %8.4f %+8.4f %+8.4f  %s' % (name[:44], eff, noise, dl, dc, v))
