# PES21 FX (10/11, Agustin: "las de la cara ... tomando de base como se plasman las texturas de arrugas remasterizadas, la escala esta
# mal") ARRUGAS POR GESTO SACADAS DE LAS REMASTERIZADAS. Las de make_wrinkle_depth.py eran surcos dibujados de ~0.9 mm de ancho
# (longitud de onda ~12 texeles de 1024) que cubrian el 0.1-1 % de la cara: lineas finas y oscuras que no seguian las arrugas del
# juego. Aca la profundidad sale de INTEGRAR la normal remasterizada (pes_wrinkle_normal_2048, Poisson por FFT: los mismos pliegues,
# ~25 texeles de 1024 de longitud de onda) y se reparte por gesto con la mascara de zonas del juego (pes_wrinkle_mask_512x1024):
#   a: R frente (+ sienes)  G entrecejo  B ojos  A surco (zona boca, arriba de la comisura)
#   b: R menton (zona boca, abajo)  G marioneta (mandibulas)  B cuello  A (vacio)
# Cada canal 0..1 (fondo = percentil 99.7). Imprime la profundidad en mm por canal (kWrDmmA / kWrDmmB de pes_wrdepth.hlsli) que hace
# que con el gesto entero la pendiente sea la de las remasterizadas (p99 por zona).
#   python3 make_wrinkle_depth_remaster.py carpeta_textures carpeta_salida [--vista png]
import sys, struct, numpy as np
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
def dds(fn, w, h):
    d = open(fn, 'rb').read(); a = np.frombuffer(d, np.uint8, w * h * 4, 128).reshape(h, w, 4).astype(np.float32) / 255
    return a[..., [2, 1, 0, 3]]   # BGRA -> RGBA
N = 2048
n = dds(src + '/pes_wrinkle_normal_2048.dds', N, N)
nx, ny = n[..., 0] * n[..., 3] * 2 - 1, n[..., 1] * 2 - 1   # como el PS (x = R * A, y = G); marco T = +u, B = -v
gu, gv = -nx, ny
fu = np.fft.fftfreq(N)[None, :]; fv = np.fft.fftfreq(N)[:, None]
k2 = (2 * np.pi) ** 2 * (fu ** 2 + fv ** 2); k2[0, 0] = 1
H = (np.conj(2j * np.pi * fu) * np.fft.fft2(gu) + np.conj(2j * np.pi * fv) * np.fft.fft2(gv)) / k2; H[0, 0] = 0
h = np.real(np.fft.ifft2(H))
h -= np.real(np.fft.ifft2(np.fft.fft2(h) * np.exp(-2 * (np.pi * 48.0) ** 2 * (fu ** 2 + fv ** 2))))   # sin la forma grande (48: quedan los bultos de piel juntada)
slope = np.hypot(np.gradient(h, axis=1), np.gradient(h, axis=0))
m = dds(src + '/pes_wrinkle_mask_512x1024.dds', 512, 1024)
up = lambda x: np.asarray(Image.fromarray(x).resize((N, N), Image.BILINEAR))
T = np.stack([up(m[:512, :, c]) for c in range(4)], -1); B = np.stack([up(m[512:, :, c]) for c in range(4)], -1)
# (10/11 c) relieve ENTERO (surcos y bultos), no solo el fondo: con solo max(-h, 0) quedaban las rayas del fondo y se perdia la
# piel juntada que da la forma suave de las remasterizadas. 0 = cresta mas alta de la zona, 1 = fondo; la piel lisa queda en el medio
dep = -h
V = (np.arange(N) + 0.5)[:, None] / N * np.ones((1, N))
s = np.clip((V - 0.600) / 0.03, 0, 1); s = s * s * (3 - 2 * s)
# (10/11 d, Agustin: "esta raro el detalle, asi no se marcan las arrugas en la vida real", "marca distintas zonas", "tres mapas para la
# edad real, cada uno con menos secciones y menor intensidad", "mira FC 27") sin rayitas finas; cada gesto solo en SU zona anatomica
# (la ubicacion de los surcos de make_wrinkle_depth.py, ensanchada: frente, entrecejo, ojos, surco, menton, marioneta, cuello) con la
# forma de las remasterizadas; y pes_wrinkle_age.dds (R joven, G adulto, B veterano): arrugas en reposo, relieve con signo (0.5 = liso).
def blur(x, sg):
    return np.real(np.fft.ifft2(np.fft.fft2(x) * np.exp(-2 * (np.pi * sg) ** 2 * (fu ** 2 + fv ** 2))))
# (10/11 e, Agustin: "ve mas alla, considera el cuello y toda la cabeza, de donde salen las arrugas, en que edad") ZONAS ANATOMICAS de
# toda la cabeza en las uv (centro u = 0.5; ojos v 0.38, boca v 0.61, orejas dx 0.34..0.40, cuello v 0.78..0.97). Las 8 primeras son
# las de los gestos (canales a / b); las 3 ultimas solo salen con la edad. Las orejas, los parpados y los labios quedan afuera.
DX = np.abs((np.arange(N) + 0.5)[None, :] / N - 0.5) * np.ones((N, 1), np.float32)
def sst(x): x = np.clip(x, 0, 1); return x * x * (3 - 2 * x)
def ell(cx, cv, rx, rv): return sst((1.0 - ((DX - cx) / rx) ** 2 - ((V - cv) / rv) ** 2) * 2.0)
def box(x1, v0, v1, sf=0.02): return sst((x1 - DX) / sf) * sst((V - v0) / sf) * sst((v1 - V) / sf)
R = [box(0.19, 0.185, 0.305),                                                   # 0 frente (frontal: lineas horizontales)
     np.maximum(ell(0.0, 0.335, 0.055, 0.045), 0.7 * ell(0.03, 0.40, 0.03, 0.035)),   # 1 entrecejo (corrugador) + costados de la nariz
     np.maximum(ell(0.215, 0.385, 0.05, 0.06), ell(0.115, 0.425, 0.075, 0.025)),      # 2 patas de gallo (orbicular) + bajo el ojo
     ell(0.10, 0.55, 0.04, 0.09),                                               # 3 surco nasogeniano
     ell(0.0, 0.705, 0.075, 0.035),                                             # 4 menton (mentoniano)
     ell(0.10, 0.685, 0.04, 0.06),                                              # 5 marioneta (depresor de la comisura)
     box(0.42, 0.79, 0.97),                                                     # 6 cuello entero: anillos, platisma, costados y nuca
     ell(0.17, 0.50, 0.06, 0.07),                                               # 7 mejilla (pliegues de la sonrisa, por fuera del surco)
     ell(0.315, 0.45, 0.02, 0.08),                                              # 8 delante de la oreja (solo edad)
     ell(0.25, 0.31, 0.04, 0.05),                                               # 9 sien (solo edad)
     ell(0.0, 0.765, 0.16, 0.03)]                                               # 10 papada (solo edad)
kmm = []
D = np.zeros((N, N, 8), np.float32)
for i, z in enumerate(R[:8]):
    act = z > 0.3
    if not act.any(): kmm.append(0.0); continue
    lo, hi = np.percentile(dep[act], 3), np.percentile(dep[act], 99.7)
    d = np.clip((dep - lo) / (hi - lo), 0, 1) * z
    sd = np.hypot(*np.gradient(d ** 1.2)) / 0.275    # 0.275 mm por texel de 2048; gesto entero: perfil ^ (1 + 0.2)
    kmm.append(float(np.percentile(slope[act], 99) / np.percentile(sd[act], 99)))
    D[..., i] = d
# edad (cuando aparece cada una en la vida real): de joven solo los anillos del cuello y un surco leve; de adulto la frente, las patas
# de gallo finas, el entrecejo y el surco; de veterano se suman marioneta, papada, sien, delante de la oreja y todo mas hondo
#               frente entrec ojos  surco menton marion cuello mejilla oreja sien  papada
AW = np.array([[0.08, 0.00, 0.08, 0.25, 0.00, 0.00, 0.15, 0.00, 0.00, 0.00, 0.00],
               [0.35, 0.20, 0.35, 0.50, 0.10, 0.12, 0.35, 0.10, 0.00, 0.10, 0.10],
               [0.70, 0.55, 0.75, 0.80, 0.30, 0.50, 0.65, 0.30, 0.40, 0.35, 0.40]], np.float32)
hd = dep - blur(dep, 12.0)
ages = [sum(AW[a, z] * R[z] * hd for z in range(11)) for a in range(3)]
S = max(np.abs(x).max() for x in ages)
sv = np.hypot(*np.gradient(ages[2] / S)) / 0.275
kage = float(0.30 / np.percentile(sv[R[0] + R[2] + R[3] > 0.3], 99))   # veterano: pendiente p99 0.30 (gesto entero ~0.5)
AGE = np.stack([0.5 + 0.5 * x / S for x in ages] + [np.ones((N, N), np.float32)], -1)
print('kWrAgeMm', round(2 * kage, 3), '(muestra - 0.5) * esto = mm')
def write_dds(fn, img):
    mips = []; cur = img.astype(np.float32)
    while True:
        mips.append(np.clip(np.round(cur), 0, 255).astype(np.uint8))
        if cur.shape[0] == 1: break
        cur = cur.reshape(cur.shape[0] // 2, 2, cur.shape[1] // 2, 2, 4).mean((1, 3))
    hh, ww = img.shape[:2]
    hdr = struct.pack('<4sIIIIIII44x', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000, hh, ww, ww * 4, 0, len(mips))
    pf = struct.pack('<II4sIIIII', 32, 0x4, b'DX10', 0, 0, 0, 0, 0)
    caps = struct.pack('<IIIII', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    dx10 = struct.pack('<IIIII', 28, 3, 0, 1, 0)
    with open(fn, 'wb') as f:
        f.write(hdr + pf + caps + dx10)
        for mm in mips: f.write(mm.tobytes())
q = np.round(D * 255)
write_dds(out + '/pes_wrinkle_depth_a.dds', q[..., :4]); write_dds(out + '/pes_wrinkle_depth_b.dds', q[..., 4:])
write_dds(out + '/pes_wrinkle_age.dds', np.round(np.clip(AGE, 0, 1) * 255))
print('kWrDmmA', np.round(kmm[:4], 2), 'kWrDmmB', np.round(kmm[4:], 2))
if '--vista' in sys.argv:
    cols = np.array([(255, 80, 80), (80, 255, 80), (90, 140, 255), (255, 220, 60), (255, 120, 255), (60, 255, 255), (255, 160, 60), (200, 200, 200)], np.float32)
    img = np.full((N, N, 3), 40.0)
    for i in range(8): img = img * (1 - D[..., i:i + 1]) + cols[i] * D[..., i:i + 1]
    Image.fromarray(img.clip(0, 255).astype(np.uint8)).save(sys.argv[sys.argv.index('--vista') + 1])
