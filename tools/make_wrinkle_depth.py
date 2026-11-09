# PES21 FX (09/11, Agustin: "mejorar las arrugas ... la textura y como se aplica", "las custom y las genericas") PROFUNDIDAD DE LAS
# ARRUGAS POR GESTO en las uv de la cara (las mismas de todas las caras del juego). Reemplaza al mapa unico pes_wrinkle_normal_2048
# (todas las arrugas juntas, curvas de nivel del original agrandado) + mascara de manchas: cada canal es la profundidad de las
# arrugas de UN grupo, dibujadas como surcos con la anatomia (ubicadas sobre el mapa original del juego), y el PS arma de ahi la
# normal, el fondo del surco, el brillo y el SSS (una sola fuente); la teselacion hunde lo mismo.
#   pes_wrinkle_depth_a.dds  R frente (asombro)  G entrecejo + nariz (enojo)  B patas de gallo + bajo el ojo (ojos)  A surco nasogeniano (boca)
#   pes_wrinkle_depth_b.dds  R menton + labio de arriba (boca, puchero)  G marioneta (mandibula)  B cuello  A mejilla bajo el ojo (sonrisa)
# Cada valor es 0 (piel lisa) .. 1 (fondo del surco), perfil gaussiano ancho: el PS lo afina con la fuerza del gesto (la arruga se
# forma: primero hendidura ancha y suave, despues fina y profunda).
#   python3 make_wrinkle_depth.py carpeta_salida [tamano=1024] [--vista fondo.png]
import sys, struct, math
import numpy as np

out = sys.argv[1]
N = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2].isdigit() else 1024
rng = np.random.default_rng(2109)
PX = 1.0 / 1024.0          # anchos en texeles de 1024 (escala con N)
CU = 0.5                   # eje de la cara en u

yy, xx = np.mgrid[0:N, 0:N]
U = (xx + 0.5) / N; V = (yy + 0.5) / N

def vnoise(freq, seed):
    # ruido de valor suave (para cortar y ondular las lineas)
    r = np.random.default_rng(seed); g = r.random((freq + 3, freq + 3))
    fx = U * freq; fy = V * freq; ix = np.floor(fx).astype(int); iy = np.floor(fy).astype(int)
    tx = fx - ix; ty = fy - iy; tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty)
    a = g[iy, ix]; b = g[iy, ix + 1]; c = g[iy + 1, ix]; d = g[iy + 1, ix + 1]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty

def bez(pts, n=160):
    p = np.array(pts, float); t = np.linspace(0, 1, n)[:, None]
    if len(p) == 2: return p[0] * (1 - t) + p[1] * t
    if len(p) == 3: return p[0] * (1 - t) ** 2 + 2 * p[1] * t * (1 - t) + p[2] * t ** 2
    return p[0] * (1 - t) ** 3 + 3 * p[1] * t * (1 - t) ** 2 + 3 * p[2] * t ** 2 * (1 - t) + p[3] * t ** 3

def groove(ch, pts, amp=1.0, sig=2.0, mirror=True, taper=0.25, wob=0.0, seed=0, brk=0.0):
    """surco a lo largo de la curva (dx desde el eje, v); perfil gaussiano de ancho sig texeles (de 1024), puntas que se apagan"""
    for side in ((-1, 1) if mirror else (1,)):
        c = bez(pts)
        c = np.stack([CU + side * c[:, 0], c[:, 1]], 1)
        if wob > 0:   # ondulacion suave a traves de la curva
            tng = np.gradient(c, axis=0); tng /= np.linalg.norm(tng, axis=1, keepdims=True) + 1e-9
            nrm = np.stack([-tng[:, 1], tng[:, 0]], 1)
            ph = np.random.default_rng(seed + (side > 0)).random() * 6.28
            s = np.linspace(0, 1, len(c)); c = c + nrm * (wob * PX * np.sin(s * 9.0 + ph) + wob * 0.5 * PX * np.sin(s * 23.0 + 2 * ph))[:, None]
        sg = sig * PX; R = 4 * sg
        x0 = max(int((c[:, 0].min() - R) * N), 0); x1 = min(int((c[:, 0].max() + R) * N) + 2, N)
        y0 = max(int((c[:, 1].min() - R) * N), 0); y1 = min(int((c[:, 1].max() + R) * N) + 2, N)
        if x1 <= x0 or y1 <= y0: continue
        u = U[y0:y1, x0:x1][..., None]; v = V[y0:y1, x0:x1][..., None]
        d2 = (u - c[:, 0]) ** 2 + (v - c[:, 1]) ** 2
        k = d2.argmin(-1); d = np.sqrt(np.take_along_axis(d2, k[..., None], -1)[..., 0])
        s = k / (len(c) - 1.0)
        tp = np.clip(np.minimum(s, 1 - s) / max(taper, 1e-3), 0, 1); tp = tp * tp * (3 - 2 * tp)
        a = amp * tp
        if brk > 0:   # cortes: la linea se interrumpe a trechos
            r = np.random.default_rng(seed * 7 + (side > 0)); ph = r.random() * 6.28
            a = a * np.clip(0.55 + 0.45 * np.sin(s * 11.0 + ph) + 0.35 * np.sin(s * 5.0 + 2 * ph) - brk * 0.5, 0, 1) ** 0.7
        h = a * np.exp(-(d / sg) ** 2)
        ch[y0:y1, x0:x1] = np.maximum(ch[y0:y1, x0:x1], h)

A = [np.zeros((N, N), np.float32) for _ in range(4)]
B = [np.zeros((N, N), np.float32) for _ in range(4)]

# ---- A.r FRENTE (asombro): 4 lineas largas + 1 corta, suben un poco sobre cada ceja y bajan en el medio, cortadas a trechos
for i, (v0, amp, w) in enumerate([(0.207, 0.75, 0.135), (0.229, 0.95, 0.150), (0.252, 1.0, 0.152), (0.275, 0.85, 0.140), (0.296, 0.5, 0.090)]):
    xs = np.linspace(0, w, 7); vs = v0 - 0.0075 * np.exp(-((xs - 0.055) / 0.035) ** 2) + 0.006 * (xs / 0.15) ** 2
    for side in (-1, 1):
        pts = [(side * xs[j], vs[j]) for j in (0, 2, 4, 6)]
        groove(A[0], pts, amp=amp, sig=2.4, mirror=False, taper=0.18, wob=1.2, seed=10 + i * 2 + (side > 0), brk=0.35)
# ---- A.g ENTRECEJO (enojo): dos verticales que se abren arriba, una tercera corta, la horizontal de la raiz de la nariz y las de los costados
groove(A[1], [(0.0085, 0.367), (0.0095, 0.345), (0.0125, 0.318)], amp=1.0, sig=2.0, taper=0.3, wob=0.6, seed=31)
groove(A[1], [(0.021, 0.358), (0.023, 0.340), (0.026, 0.326)], amp=0.5, sig=1.8, taper=0.35, seed=33)
groove(A[1], [(-0.020, 0.378), (0.0, 0.381), (0.020, 0.378)], amp=0.6, sig=2.2, mirror=False, taper=0.3, seed=35)
groove(A[1], [(-0.016, 0.372), (0.0, 0.374), (0.016, 0.372)], amp=0.35, sig=1.6, mirror=False, taper=0.3, seed=36)
for k, dv in enumerate((0.0, 0.011)):   # lineas de conejo: costados del puente de la nariz, en diagonal
    groove(A[1], [(0.016, 0.398 + dv), (0.026, 0.410 + dv), (0.036, 0.424 + dv)], amp=0.55 - 0.15 * k, sig=1.6, taper=0.35, seed=37 + k)
# ---- A.b OJOS: patas de gallo (abanico desde el angulo de afuera del ojo) y dos arcos bajo el ojo
CX, CV = 0.151, 0.375
for k, (ang, ln, amp) in enumerate([(-34, 0.026, 0.55), (-15, 0.034, 0.85), (2, 0.040, 1.0), (18, 0.036, 0.9), (34, 0.030, 0.7), (48, 0.022, 0.45)]):
    a = math.radians(ang); r0 = 0.010
    p0 = (CX + r0 * math.cos(a), CV + r0 * math.sin(a)); p2 = (CX + (r0 + ln) * math.cos(a + 0.12), CV + (r0 + ln) * math.sin(a + 0.12) + 0.004)
    p1 = ((p0[0] + p2[0]) / 2, (p0[1] + p2[1]) / 2 + 0.002)
    groove(A[2], [p0, p1, p2], amp=amp, sig=1.7, taper=0.4, wob=0.5, seed=41 + k)
groove(A[2], [(0.078, 0.392), (0.110, 0.407), (0.146, 0.394)], amp=0.7, sig=1.9, taper=0.3, wob=0.6, seed=51)
groove(A[2], [(0.085, 0.405), (0.115, 0.421), (0.148, 0.410)], amp=0.45, sig=2.2, taper=0.35, wob=0.6, seed=52)
# ---- A.a SURCO NASOGENIANO (boca / sonrisa): ala de la nariz -> al costado de la comisura, mas una linea paralela en la mejilla y la de la comisura
groove(A[3], [(0.058, 0.497), (0.084, 0.548), (0.099, 0.610), (0.094, 0.664)], amp=1.0, sig=3.2, taper=0.2, wob=0.5, seed=61)
groove(A[3], [(0.060, 0.502), (0.074, 0.530), (0.086, 0.565)], amp=0.45, sig=1.6, taper=0.3, seed=62)   # el borde fino del pliegue
groove(A[3], [(0.112, 0.545), (0.122, 0.580), (0.126, 0.615)], amp=0.35, sig=2.6, taper=0.35, wob=0.6, seed=63)
groove(A[3], [(0.068, 0.630), (0.074, 0.639), (0.080, 0.650)], amp=0.55, sig=1.5, taper=0.4, seed=64)

# ---- B.r MENTON y LABIOS (boca, puchero): surco del menton, rayas verticales del labio de arriba, piel de naranja del menton
groove(B[0], [(-0.042, 0.671), (0.0, 0.683), (0.042, 0.671)], amp=0.85, sig=2.6, mirror=False, taper=0.25, wob=0.6, seed=71)
for k, dx in enumerate((0.009, 0.021, 0.033, 0.045)):
    groove(B[0], [(dx, 0.578 + 0.003 * k), (dx + 0.001, 0.589 + 0.002 * k), (dx + 0.002, 0.598 + 0.001 * k)], amp=0.45 - 0.06 * k, sig=1.2, taper=0.45, seed=72 + k)
peel = vnoise(150, 77) * 0.6 + vnoise(300, 78) * 0.4
chin = np.exp(-(((U - CU) / 0.032) ** 2 + ((V - 0.703) / 0.018) ** 2))
B[0] = np.maximum(B[0], (np.clip((peel - 0.52) / 0.2, 0, 1) * chin * 0.45).astype(np.float32))
# ---- B.g MARIONETA (mandibula): de la comisura hacia abajo
groove(B[1], [(0.069, 0.640), (0.073, 0.668), (0.075, 0.700)], amp=0.75, sig=2.4, taper=0.3, wob=0.5, seed=81)
groove(B[1], [(0.083, 0.665), (0.088, 0.690), (0.090, 0.715)], amp=0.35, sig=2.8, taper=0.35, seed=82)
# ---- B.b CUELLO: anillos horizontales (bajan en el medio) y las dos bandas del platisma
for k, (v0, amp) in enumerate([(0.812, 0.7), (0.845, 0.85), (0.878, 0.6)]):
    xs = np.linspace(0, 0.19, 4); vs = v0 + 0.010 * np.cos(xs / 0.19 * math.pi * 0.5)
    for side in (-1, 1):
        groove(B[2], [(side * x, vs[j]) for j, x in enumerate(xs)], amp=amp, sig=3.0, mirror=False, taper=0.15, wob=1.5, seed=91 + 2 * k + (side > 0), brk=0.2)
groove(B[2], [(0.058, 0.735), (0.062, 0.800), (0.070, 0.900)], amp=0.45, sig=5.0, taper=0.3, seed=97)
# ---- B.a MEJILLA BAJO EL OJO (sonrisa): pliegues diagonales del pomulo que sube
for k in range(3):
    groove(B[3], [(0.118 + 0.008 * k, 0.428 + 0.006 * k), (0.132 + 0.008 * k, 0.437 + 0.006 * k), (0.146 + 0.008 * k, 0.450 + 0.006 * k)], amp=0.5 - 0.1 * k, sig=1.6, taper=0.4, seed=101 + k)

def pack(ch):
    return np.clip(np.round(np.stack(ch, -1) * 255), 0, 255).astype(np.uint8)
def write_dds(fn, img):
    mips = []; cur = img.astype(np.float32)
    while True:
        mips.append(np.clip(np.round(cur), 0, 255).astype(np.uint8))
        if cur.shape[0] == 1: break
        cur = cur.reshape(cur.shape[0] // 2, 2, cur.shape[1] // 2, 2, 4).mean((1, 3))
    h, w = img.shape[:2]
    hdr = struct.pack('<4sIIIIIII44x', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000, h, w, w * 4, 0, len(mips))
    pf = struct.pack('<II4sIIIII', 32, 0x4, b'DX10', 0, 0, 0, 0, 0)
    caps = struct.pack('<IIIII', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    dx10 = struct.pack('<IIIII', 28, 3, 0, 1, 0)   # R8G8B8A8_UNORM (lineal: es profundidad, no color)
    with open(fn, 'wb') as f:
        f.write(hdr + pf + caps + dx10)
        for m in mips: f.write(m.tobytes())
pa, pb = pack(A), pack(B)
write_dds(out + '/pes_wrinkle_depth_a.dds', pa); write_dds(out + '/pes_wrinkle_depth_b.dds', pb)
print('listo', N, 'x', N)
if '--vista' in sys.argv:
    from PIL import Image
    bg = np.asarray(Image.open(sys.argv[sys.argv.index('--vista') + 1]).convert('L').resize((N, N))).astype(np.float32) * 0.45
    cols = [(255, 80, 80), (80, 255, 80), (90, 140, 255), (255, 220, 60), (255, 120, 255), (60, 255, 255), (255, 160, 60), (200, 200, 200)]
    img = np.dstack([bg] * 3)
    for ch, col in zip(A + B, cols):
        img = img * (1 - ch[..., None]) + np.array(col, np.float32) * ch[..., None]
    Image.fromarray(img.clip(0, 255).astype(np.uint8)).save(out + '/vista_arrugas.png')
