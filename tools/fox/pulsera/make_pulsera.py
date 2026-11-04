# PES21 FX (04/11) - pulsera trenzada de tela (marron oscuro, crema y caramelo) en la muneca IZQUIERDA de un jugador.
# Sale como wristtaping_l.fmdl (el vendaje de muneca izquierda de Konami) para la carpeta del jugador:
#   face\real\<id>\#Win\wristtaping_l.fmdl
#   face\real\<id>\sourceimages\#windx11\pulsera_bsm.ftex / pulsera_nrm.ftex / pulsera_srm.ftex
# Solo ese jugador la usa (las piezas de face\real\<id>\#Win reemplazan a las comunes para ese id) y solo se ve si en su
# Edit tiene el vendaje de muneca izquierda puesto (color blanco: el material de Konami multiplica la textura por el color).
#
# Como se arma (sin herramientas externas):
#   - se parte del wristtaping_l.fmdl de Konami (mismos huesos: sk_forearm_l, sk_hand_l, dsk_forearm_t_l, dsk_forearm_l,
#     dsk_wrist_l, mismo material pes_3ddf_basic_color y mismo formato de vertices) y se reescriben los buffers con la malla
#     nueva (mas vertices que el vendaje: el bloque de buffers se rearma y los indices de los LOD apuntan a la malla entera)
#   - el eje y el centro de la muneca salen de las normales del vendaje; el contorno de la muneca, del arm.fmdl comun
#     (la pulsera queda 1 mm afuera de la piel, un poco suelta como en la foto)
#   - pesos: promedio de los 8 vertices del vendaje mas cercanos (la pulsera sigue al antebrazo y a la mano igual que el vendaje)
#   - seccion redondeada de 9 x 4.4 mm (superelipse), 160 segmentos alrededor x 20 en la seccion = 3381 vertices, 6400 triangulos
#   - texturas 2048 x 256 (u = vuelta entera, v = vuelta de la seccion): cordon marron enroscado en diagonal en toda la pulsera
#     y al medio (corrido hacia un lado, como en la foto) la cadena de ovalos inclinados crema / caramelo; fibras finas en el
#     relieve y en el color; srm de tela encerada (algo de brillo en las fibras altas, mate en los huecos)
#   - la ruta de las texturas se agrega al final de la tabla de textos del fmdl (es el ultimo bloque del archivo)
# NO toca la boca ni nada fuera de estos 4 archivos.
# Uso: python3 make_pulsera.py <wristtaping_l.fmdl de Konami> <arm.fmdl comun> <carpeta accessory_*.ftex de Konami> <id> <salida>
import sys, os, struct, io, math
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', '..'))
import fmdl as F_
from ftex import Ftex

NU, NV = 160, 20                 # segmentos alrededor / en la seccion
HALF_W, HALF_T = 0.0045, 0.0022  # media anchura (a lo largo del brazo) y medio espesor (radial), m
GAP = 0.0010                     # luz entre la piel y la cara interna
TW, TH = 2048, 256               # textura de la trenza (el atlas entero es 2048 x 512)

# ------------------------------------------------------------------ geometria de la muneca
def wrist_frame(tape, arm, back_mm=16.0):
    """centro, eje (hacia la mano), e1 = palma, e2 = pulgar, contorno de la piel (radio por angulo) en el plano de la pulsera.
    El vendaje de Konami cubre 86 mm del antebrazo: su centro no sirve; la pulsera va back_mm antes del final del brazo
    (donde empieza la mano), arriba de la cabeza del cubito como en la foto."""
    m = F_.meshes(tape)[0]
    P = m['pos'].astype(np.float64); N = m['nrm'][:, :3].astype(np.float64)
    ev, evec = np.linalg.eigh(N.T @ N)
    ax = evec[:, 0]                                   # eje del vendaje: perpendicular a todas sus normales
    hand = [i for i, b in enumerate(tape.bones) if b['name'] == 'sk_hand_l'][0]
    wh = ((m['idx'] == hand) * m['w']).sum(1)
    c = P.mean(0)
    if ((P - c) @ ax * wh).sum() < 0: ax = -ax         # el eje apunta a la mano
    c = c + ax * (((P - c) @ ax).mean())
    am = max(F_.meshes(arm), key=lambda q: q['nvert'])
    Q = am['pos'].astype(np.float64)
    ss = (Q - c) @ ax; rr = np.linalg.norm((Q - c) - np.outer(ss, ax), axis=1)
    arm_end = ss[rr < 0.05].max()
    c = c + ax * (arm_end - back_mm / 1000)
    # pulgar hacia +z en la pose del modelo (huesos de hand_l: pulgar z 0.04, menique z -0.02); palma = eje x pulgar
    e2 = np.array([0, 0, 1.0]) - ax * ax[2]; e2 /= np.linalg.norm(e2)
    e1 = np.cross(ax, e2); e1 /= np.linalg.norm(e1)
    ss = (Q - c) @ ax; d = (Q - c) - np.outer(ss, ax)
    sel = (np.abs(ss) < 0.005) & (np.linalg.norm(d, axis=1) < 0.06)
    x = d[sel] @ e1; y = d[sel] @ e2
    ang = np.arctan2(y, x); r = np.hypot(x, y)
    bins = 72; rb = np.full(bins, np.nan)
    k = ((ang + np.pi) / (2 * np.pi) * bins).astype(int) % bins
    for b in range(bins):
        if (k == b).any(): rb[b] = r[k == b].max()
    good = ~np.isnan(rb); idx = np.arange(bins)
    rb = np.interp(idx, np.concatenate([idx[good] - bins, idx[good], idx[good] + bins]), np.tile(rb[good], 3))
    rb = np.convolve(np.tile(rb, 3), np.ones(7) / 7, 'same')[bins:2 * bins]       # suave
    return c, ax, e1, e2, rb, int(sel.sum())

def radius_at(rb, th):
    bins = len(rb); t = (np.asarray(th) + np.pi) / (2 * np.pi) * bins - 0.5
    i0 = np.floor(t).astype(int); f = t - i0
    return rb[i0 % bins] * (1 - f) + rb[(i0 + 1) % bins] * f

# Partes (angulo th alrededor de la muneca: 0 = centro de la palma, +pi/2 = lado del pulgar, -pi/2 = menique).
# Abajo (palma), como en la foto: la trenza termina del lado del menique en una funda negra con un aro dorado y una
# punta de cuero suelta; de ahi un cordon de cuero fino cruza la palma hasta el nudo del lado del pulgar.
TH_SLEEVE = (-0.50, -0.26)        # funda negra
TH_CORD = (-0.30, 0.86)           # cordon de cuero
TH_KNOT = (0.66, 0.95)            # nudo
TH_BRAID = (0.90, 2 * np.pi - 0.46)
TH_RING = -0.22
# zonas de la textura (u0, v0, u1, v1): arriba la trenza, abajo cuero / funda / nudo / dorado
UV_BRAID = (0.0, 0.0, 1.0, 0.5)
UV_CORD = (0.0, 0.5, 0.5, 1.0)
UV_SLEEVE = (0.5, 0.5, 0.7, 1.0)
UV_KNOT = (0.7, 0.5, 0.9, 1.0)
UV_GOLD = (0.9, 0.5, 1.0, 1.0)

class Ctx:
    def __init__(s, c, ax, e1, e2, rb): s.c, s.ax, s.e1, s.e2, s.rb = c, ax, e1, e2, rb
    def radial(s, th): th = np.asarray(th); return np.cos(th)[..., None] * s.e1 + np.sin(th)[..., None] * s.e2
    def tangent(s, th): th = np.asarray(th); return -np.sin(th)[..., None] * s.e1 + np.cos(th)[..., None] * s.e2
    def skin(s, th): return radius_at(s.rb, th)

se = lambda t, p: np.sign(t) * np.abs(t) ** p

def grid_part(P, C, uv, closed_u=False):
    """P (nu+1, nv+1, 3) con la seccion cerrada (ultima columna = primera); C (nu+1, 3) centro de cada seccion"""
    nu, nv = P.shape[0] - 1, P.shape[1] - 1
    Pu = np.gradient(P, axis=0)
    if closed_u:
        Pu = np.roll(P[:-1], -1, 0) - np.roll(P[:-1], 1, 0); Pu = np.concatenate([Pu, Pu[:1]], 0)
    Pv = np.roll(P[:, :-1], -1, 1) - np.roll(P[:, :-1], 1, 1); Pv = np.concatenate([Pv, Pv[:, :1]], 1)
    N = np.cross(Pu, Pv); nl = np.linalg.norm(N, axis=-1, keepdims=True)
    out = P - C[:, None, :]
    N = N / np.maximum(nl, 1e-12)
    if (N * out).sum() < 0: N = -N
    bad = nl[..., 0] < 1e-12                           # puntas cerradas (radio 0): normal = hacia afuera del eje
    if bad.any():
        dirn = Pu / np.maximum(np.linalg.norm(Pu, axis=-1, keepdims=True), 1e-12)
        N[bad] = dirn[bad] * (1 if bad[-1].any() else -1)
    T = Pu - N * (Pu * N).sum(-1, keepdims=True)
    tl = np.linalg.norm(T, axis=-1, keepdims=True); T = np.where(tl > 1e-12, T / np.maximum(tl, 1e-12), np.cross(N, Pv))
    T /= np.maximum(np.linalg.norm(T, axis=-1, keepdims=True), 1e-12)
    U, V = uv
    faces = []
    W1 = nv + 1
    for i in range(nu):
        for j in range(nv):
            a0 = i * W1 + j; a1 = (i + 1) * W1 + j; b0 = a0 + 1; b1 = a1 + 1
            faces += [(a0, a1, b1), (a0, b1, b0)]
    return dict(P=P.reshape(-1, 3), N=N.reshape(-1, 3), T=T.reshape(-1, 3), U=None, V=None,
                Pv=Pv.reshape(-1, 3), faces=np.array(faces, np.int64))

def map_uv(rect, s, t, flip):
    u0, v0, u1, v1 = rect
    if flip: t = 1 - t
    return u0 + (u1 - u0) * s, v0 + (v1 - v0) * t

def finish(part, rect, s, t):
    """uv del rectangulo de la textura con la convencion de Konami: tangente = dP/du y cross(N, T) = -dP/dv"""
    flip = (np.cross(part['N'], part['T']) * part['Pv']).sum() > 0
    part['U'], part['V'] = map_uv(rect, s.ravel(), t.ravel(), flip)
    part['flip'] = flip
    return part

def band(X, th0, th1, nu, nv, hw, ht, lift, rect, p=0.55, taper=0.0):
    """tira alrededor de la muneca de th0 a th1: seccion superelipse (hw a lo largo del brazo, ht radial)"""
    th = np.linspace(th0, th1, nu + 1); ph = np.linspace(0, 2 * np.pi, nv + 1)
    TH_, PH_ = np.meshgrid(th, ph, indexing='ij')
    sc = np.ones_like(TH_)
    if taper:
        f = np.minimum(TH_ - th0, th1 - TH_) / taper; sc = 0.82 + 0.18 * np.clip(f, 0, 1)
    R0 = X.skin(TH_) + GAP + ht + lift
    a = hw * sc * se(np.cos(PH_), p); rr = R0 + ht * sc * se(np.sin(PH_), p)
    P = X.c + X.radial(TH_) * rr[..., None] + X.ax * a[..., None]
    C = X.c + X.radial(th) * (X.skin(th) + GAP + ht + lift)[..., None]
    part = grid_part(P, C, (None, None))
    s_ = (TH_ - th0) / (th1 - th0); t_ = PH_ / (2 * np.pi)
    part['len'] = float(np.mean(X.skin(th)) * (th1 - th0))
    return finish(part, rect, s_, t_)

def tube(path, rad, nv, rect, cap_end=False, lumpy=None):
    """tubo redondo a lo largo de path (n, 3) con radio rad (n,)"""
    n = len(path)
    Tg = np.gradient(path, axis=0); Tg /= np.linalg.norm(Tg, axis=1, keepdims=True)
    ref = np.array([0.3, 0.9, 0.2]); B = np.cross(Tg, ref); B /= np.linalg.norm(B, axis=1, keepdims=True)
    Nn = np.cross(B, Tg)
    ph = np.linspace(0, 2 * np.pi, nv + 1)
    R = rad[:, None] * np.ones((1, nv + 1))
    if lumpy is not None: R = R * lumpy
    if cap_end: R[-1] = 0
    P = path[:, None, :] + (np.cos(ph)[None, :, None] * Nn[:, None, :] + np.sin(ph)[None, :, None] * B[:, None, :]) * R[..., None]
    part = grid_part(P, path, (None, None))
    s_ = np.repeat(np.linspace(0, 1, n)[:, None], nv + 1, 1); t_ = np.repeat(ph[None, :] / (2 * np.pi), n, 0)
    return finish(part, rect, s_, t_)

def build_mesh(c, ax, e1, e2, rb):
    X = Ctx(c, ax, e1, e2, rb)
    parts = []
    braid = band(X, TH_BRAID[0], TH_BRAID[1], 150, NV, HALF_W, HALF_T, 0.0, UV_BRAID)
    parts.append(braid)
    parts.append(band(X, TH_SLEEVE[0], TH_SLEEVE[1], 14, NV, HALF_W + 0.0007, HALF_T + 0.0007, 0.0, UV_SLEEVE, p=0.5, taper=0.06))
    # cordon de cuero: mas cerca de la piel, 1.1 mm de radio, con una leve curva hacia la mano en el medio
    th = np.linspace(TH_CORD[0], TH_CORD[1], 64)
    sag = np.sin(np.pi * (th - TH_CORD[0]) / (TH_CORD[1] - TH_CORD[0]))
    rc = 0.0011
    path = X.c + X.radial(th) * (X.skin(th) + GAP * 0.6 + rc)[:, None] + X.ax * (0.0012 * sag)[:, None]
    parts.append(tube(path, np.full(len(th), rc), 12, UV_CORD))
    # nudo: bola irregular sobre el cordon, del lado del pulgar
    th = np.linspace(TH_KNOT[0], TH_KNOT[1], 22)
    f = (th - TH_KNOT[0]) / (TH_KNOT[1] - TH_KNOT[0])
    rk = 0.0012 + 0.0024 * np.sin(np.pi * np.clip(f, 0, 1)) ** 0.6
    path = X.c + X.radial(th) * (X.skin(th) + GAP + rk * 0.9)[:, None]
    rng = np.random.default_rng(7)
    lump = 1 + 0.18 * np.sin(np.linspace(0, 6 * np.pi, 17))[None, :] * np.sin(np.linspace(0, 9, 22))[:, None] + 0.06 * rng.standard_normal((22, 17))
    lump[:, -1] = lump[:, 0]
    parts.append(tube(path, rk, 16, UV_KNOT, lumpy=lump))
    # punta suelta del cuero: sale de la funda hacia la mano, despegandose de la piel (13 mm)
    t = np.linspace(0, 1, 26)
    th0 = -0.36; base = X.c + X.radial(th0) * (X.skin(th0) + GAP + 2 * HALF_T + 0.0004)
    path = base[None, :] + X.ax[None, :] * (0.0125 * t)[:, None] + X.radial(th0)[None, :] * (0.0035 * t ** 1.6)[:, None] \
        + X.tangent(th0)[None, :] * (0.0015 * t)[:, None]
    parts.append(tube(path, 0.0010 * (1 - 0.25 * t), 12, UV_CORD, cap_end=True))
    # aro dorado alrededor del cordon, junto a la funda
    cr = X.c + X.radial(TH_RING) * (X.skin(TH_RING) + GAP * 0.6 + rc)
    tg = X.tangent(TH_RING); n1 = X.radial(TH_RING); n2 = np.cross(tg, n1)
    ph = np.linspace(0, 2 * np.pi, 25)
    ring = cr + (np.cos(ph)[:, None] * n1 + np.sin(ph)[:, None] * n2) * 0.0016
    parts.append(tube(ring, np.full(25, 0.00045), 8, UV_GOLD))
    # junta todo
    P = []; N = []; T = []; U = []; V = []; Fc = []; off = 0
    for q in parts:
        fc = q['faces']
        fn = np.cross(q['P'][fc[:, 1]] - q['P'][fc[:, 0]], q['P'][fc[:, 2]] - q['P'][fc[:, 0]])
        if (fn * q['N'][fc[:, 0]]).sum() > 0: fc = fc[:, [0, 2, 1]]      # Konami: giro horario de frente
        P.append(q['P']); N.append(q['N']); T.append(q['T']); U.append(q['U']); V.append(q['V']); Fc.append(fc + off)
        off += len(q['P'])
    return dict(pos=np.concatenate(P), nrm=np.concatenate(N), tan=np.concatenate(T),
                uv=np.stack([np.concatenate(U), np.concatenate(V)], 1), faces=np.concatenate(Fc),
                flipv=braid['flip'], braid_len=braid['len'])

def skin_weights(tape, P, k=8):
    """pesos (u8, indices locales del grupo del vendaje) promedio de los k vertices del vendaje mas cercanos"""
    d = tape.d; base = tape.b1[2][0]
    bufs = [struct.unpack_from("<IIII", d, tape.b0[14][1] + 16 * i) for i in range(tape.b0[14][0])]
    n = struct.unpack_from("<H", d, tape.b0[3][1] + 10)[0]
    raw = np.frombuffer(d, np.uint8, count=28 * n, offset=base + bufs[1][2]).reshape(n, 28)
    Pt = np.frombuffer(d, np.float32, count=3 * n, offset=base + bufs[0][2]).reshape(n, 3).astype(np.float64)
    W = np.zeros((n, 8))
    for s in range(4): W[np.arange(n), raw[:, 20 + s]] += raw[:, 16 + s] / 255.0
    out_i = np.zeros((len(P), 4), np.uint8); out_w = np.zeros((len(P), 4), np.uint8)
    for v in range(len(P)):
        dd = np.linalg.norm(Pt - P[v], axis=1); nn = np.argsort(dd)[:k]
        wv = 1.0 / (dd[nn] + 0.003); acc = (W[nn] * wv[:, None]).sum(0) / wv.sum()
        top = np.argsort(-acc)[:4]; ws = acc[top]; ws = ws / ws.sum()
        q = np.round(ws * 255).astype(int); q[0] += 255 - q.sum()
        out_i[v] = top; out_w[v] = np.clip(q, 0, 255)
    return out_i, out_w

# ------------------------------------------------------------------ fmdl
def write_fmdl(src, dst, M, bi, bw, tex_dir, tex_names):
    T = F_.Fmdl(src); d = T.d
    hdr = list(struct.unpack_from("<4sfQQQIIIIII", d, 0))
    info = hdr[2]; n0, n1, s0, s0len, s1, s1len = hdr[5:11]
    s0b = bytearray(d[s0:s0 + s0len])
    def put(fmt, off_abs, *vals): struct.pack_into(fmt, s0b, off_abs - s0, *vals)
    n = len(M['pos']); idx = M['faces'].ravel()
    assert n < 65535
    # vertices: buffer 0 posicion f32x3; buffer 1 normal h4, tangente h4, pesos u8x4, indices u8x4, uv h2 (28 B)
    pos = M['pos'].astype(np.float32).tobytes()
    att = bytearray()
    nrm = np.concatenate([M['nrm'], np.ones((n, 1))], 1).astype(np.float16)
    tan = np.concatenate([M['tan'], np.ones((n, 1))], 1).astype(np.float16)
    uv = M['uv'].astype(np.float16)
    rec = np.zeros((n, 28), np.uint8)
    rec[:, 0:8] = nrm.view(np.uint8).reshape(n, 8); rec[:, 8:16] = tan.view(np.uint8).reshape(n, 8)
    rec[:, 16:20] = bw; rec[:, 20:24] = bi; rec[:, 24:28] = uv.view(np.uint8).reshape(n, 4)
    att = rec.tobytes()
    ind = idx.astype('<u2').tobytes()
    pad = lambda b: b + b'\0' * (-len(b) % 16)
    bufs = pad(pos) + pad(att) + pad(ind)
    o_att = len(pad(pos)); o_ind = o_att + len(pad(att))
    bo_c, bo_o = T.b0[14]
    put("<IIII", bo_o, 0, len(pos), 0, 0)
    put("<IIII", bo_o + 16, 0, len(att), o_att, 0)
    put("<IIII", bo_o + 32, 1, len(ind), o_ind, 0)
    me = T.b0[3][1]
    put("<H", me + 10, n); put("<II", me + 0x10, 0, len(idx))
    lc, lo = T.b0[17]
    for k in range(lc): put("<II", lo + 8 * k, 0, len(idx))
    lo_ = M['pos'].min(0) - 0.002; hi_ = M['pos'].max(0) + 0.002
    bc, bo = T.b0[13]
    for k in range(bc): put("<8f", bo + 32 * k, hi_[0], hi_[1], hi_[2], 1.0, lo_[0], lo_[1], lo_[2], 1.0)
    # textos: se agregan al final la carpeta y los nombres de las texturas
    sb, sl = T.b1[3]; strs = bytearray(d[sb:sb + sl])
    sc, so = T.b0[12]
    def add_str(i, text):
        nonlocal strs
        off = len(strs); strs += text.encode('ascii') + b'\0'
        blk = struct.unpack_from("<H", s0b, so - s0 + 8 * i)[0]
        put("<HHI", so + 8 * i, blk, len(text), off)
    for i, s in enumerate(T.strings):
        if s == '/Assets/pes16/model/character/common/sourceimages/': add_str(i, tex_dir)
        elif s in tex_names: add_str(i, tex_names[s])
    strs = pad(bytes(strs))
    b1_0 = d[T.b1[0][0]:T.b1[0][0] + T.b1[0][1]]; b1_1 = d[T.b1[1][0]:T.b1[1][0] + T.b1[1][1]]
    s1b = pad(b1_0) + pad(b1_1)
    off2 = len(s1b); s1b += bufs; off3 = len(s1b); s1b += strs
    # tabla de bloques de s1 (en el info, despues de las n0 entradas de s0)
    hb = bytearray(d[:s0])
    ent = [(0, 0, len(b1_0)), (1, len(pad(b1_0)), len(b1_1)), (2, off2, len(bufs)), (3, off3, len(strs))]
    for k, (t, o, l) in enumerate(ent):
        struct.pack_into("<III", hb, info + 8 * n0 + 12 * k, t, o, l)
    hdr[10] = len(s1b)
    struct.pack_into("<4sfQQQIIIIII", hb, 0, *hdr)
    gap = d[s0 + s0len:s1]
    out = bytes(hb) + bytes(s0b) + gap + bytes(s1b)
    open(dst, 'wb').write(out)
    return len(out)

# ------------------------------------------------------------------ texturas
def braid_tex(flipv, Lc):
    rng = np.random.default_rng(112448)
    Lc = Lc * 1000                                     # largo de la trenza, mm (para la escala del dibujo)
    u = (np.arange(TW) + 0.5) / TW; v = (np.arange(TH) + 0.5) / TH
    if flipv: v = 1 - v
    U, V = np.meshgrid(u, v)                           # (TH, TW): fila = v
    x = U * Lc                                         # mm a lo largo de la pulsera
    ph = V * 2 * np.pi; cp, sp = np.cos(ph), np.sin(ph)
    a = np.sign(cp) * np.abs(cp) ** 0.55 * HALF_W * 1000   # mm a lo ancho (-4.5..4.5)
    outer = np.clip(sp * 4 + 0.5, 0, 1)                # 1 = cara de afuera
    # loop de la seccion como coordenada continua (para que el cordon de los costados siga por adentro)
    s = ph / (2 * np.pi) * 2 * (2 * HALF_W + 2 * HALF_T) * 1000
    # 1) cordon marron enroscado: crestas diagonales cada 1.55 mm (cada vuelta del cordon), fibras finas a lo largo
    per = 1.55; ang = math.radians(52)
    q = (x * math.cos(ang) + s * math.sin(ang)) / per
    fq = q - np.floor(q)
    ridge = np.sin(np.pi * fq) ** 0.7                  # perfil redondo de cada vuelta
    fib = 0.5 + 0.5 * np.sin(2 * np.pi * (x * -math.sin(ang) + s * math.cos(ang)) / 0.22 + 3 * np.sin(q * 5.1))
    h_cord = 0.65 * ridge + 0.08 * fib * ridge
    # 2) cadena de ovalos crema / caramelo en la cara de afuera, corrida 0.9 mm hacia un lado, inclinados 45 grados
    P_ov = 2.7; a0 = -0.45
    k = np.round(x / P_ov); best_h = np.zeros_like(x); col_id = np.zeros_like(x); along = np.zeros_like(x)
    for dk in (-1, 0, 1):
        kk = k + dk; dx = x - kk * P_ov; da = a - a0
        c45, s45 = math.cos(math.radians(48)), math.sin(math.radians(48))
        lx = dx * c45 + da * s45; ly = -dx * s45 + da * c45
        e = (lx / 2.35) ** 2 + (ly / 1.15) ** 2
        hh = np.sqrt(np.clip(1 - e, 0, 1))
        upd = hh > best_h
        best_h = np.where(upd, hh, best_h); col_id = np.where(upd, kk % 2, col_id); along = np.where(upd, ly, along)
    ov = best_h * outer * (np.abs(a - a0) < 3.0)
    ov_fib = 0.5 + 0.5 * np.sin(2 * np.pi * along / 0.2 + rng.random() * 6)
    h_ov = 1.0 * ov + 0.06 * ov_fib * ov
    inov = np.clip(ov * 6, 0, 1)
    H = np.maximum(h_cord * (1 - inov), h_ov + 0.25 * inov)
    # ruido de fibra / pelusa
    nz = rng.standard_normal((TH, TW))
    nz = (nz + np.roll(nz, 1, 0) + np.roll(nz, -1, 0) + np.roll(nz, 1, 1) + np.roll(nz, -1, 1)) / 5 * 1.6
    H = H + 0.03 * nz
    # color (sRGB 0..1)
    dark = np.array([52, 30, 14]) / 255; dark_hi = np.array([86, 52, 26]) / 255; crev = np.array([14, 6, 3]) / 255
    cream = np.array([206, 168, 128]) / 255; caramel = np.array([142, 98, 58]) / 255
    cord_col = crev + (dark - crev) * np.clip(ridge * 1.4, 0, 1)[..., None] + (dark_hi - dark) * (ridge ** 4 * fib)[..., None]
    ovc = np.where(col_id[..., None] > 0.5, caramel, cream)
    ov_col = ovc * (0.62 + 0.38 * best_h[..., None] ** 0.5) * (0.92 + 0.08 * ov_fib[..., None])
    col = cord_col * (1 - inov[..., None]) + ov_col * inov[..., None]
    col = col * (1 + 0.06 * nz[..., None])
    col = col * (0.55 + 0.45 * outer[..., None] ** 0.3 + 0.0)           # adentro de la pulsera mas oscuro (no le da luz)
    bsm = np.clip(col, 0, 1)
    # normal (x a la derecha de la textura = +u, y hacia arriba de la textura = -v: binormal de Konami)
    sc = 0.9 / 1000 * TW / (Lc / 1000)                 # altura de 0.9 mm
    hx = (np.roll(H, -1, 1) - np.roll(H, 1, 1)) * 0.5 * (0.9 * TW / Lc)
    hy = (np.roll(H, 1, 0) - np.roll(H, -1, 0)) * 0.5 * (0.9 * TH / (2 * (2 * HALF_W + 2 * HALF_T) * 1000))
    n = np.stack([-hx, -hy, np.ones_like(H)], -1); n /= np.linalg.norm(n, axis=-1, keepdims=True)
    # srm: R = brillo, G = rugosidad (tela encerada: fibras altas con algo de brillo, huecos mates)
    hi = np.clip(H, 0, 1.2)
    spec = 0.30 + 0.35 * hi + 0.10 * inov
    rough = 0.86 - 0.20 * hi - 0.06 * inov
    return bsm, n, spec, rough, H

def zone_tex(kind, w, h, rng):
    """zona de w x h texeles (u = a lo largo de la pieza, v = vuelta de la seccion): color, normal, brillo, rugosidad, altura"""
    u = (np.arange(w) + 0.5) / w; v = (np.arange(h) + 0.5) / h
    U, V = np.meshgrid(u, v)
    nz = rng.standard_normal((h, w)); nz = (nz + np.roll(nz, 1, 0) + np.roll(nz, -1, 0) + np.roll(nz, 1, 1) + np.roll(nz, -1, 1)) / 5 * 1.6
    if kind == 'cord':        # cuero marron rojizo, liso con poros finos y un poco de brillo
        Hh = 0.04 * nz + 0.03 * np.sin(2 * np.pi * (U * 40 + V * 3))
        base = np.array([96, 42, 30]) / 255
        col = base * (1 + 0.10 * nz[..., None]) * (0.85 + 0.15 * np.sin(np.pi * V)[..., None])
        spec = 0.55 + 0.1 * nz; rough = 0.48 - 0.05 * nz; amp = 0.3
    elif kind == 'sleeve':    # cinta negra enrollada (vueltas diagonales) con pelusa marron en los bordes
        q = U * 9 + V * 1.5; fq = q - np.floor(q)
        Hh = np.sin(np.pi * fq) ** 0.8 + 0.05 * nz
        col = np.array([20, 20, 22]) / 255 * (0.6 + 0.6 * Hh[..., None])
        edge = np.clip((np.abs(U - 0.5) - 0.40) * 12, 0, 1)
        col = col * (1 - edge[..., None]) + np.array([70, 45, 30]) / 255 * edge[..., None] * (0.7 + 0.3 * nz[..., None])
        spec = 0.45 + 0.25 * Hh; rough = 0.62 - 0.15 * Hh; amp = 0.6
    elif kind == 'knot':      # nudo: cordon oscuro con mechones crema
        q = U * 6 + V * 4; q2 = U * 4 - V * 5
        Hh = 0.5 * np.sin(np.pi * (q - np.floor(q))) + 0.5 * np.sin(np.pi * (q2 - np.floor(q2))) + 0.08 * nz
        tuft = (np.sin(2 * np.pi * (U * 3.3 + 0.3)) * np.sin(2 * np.pi * (V * 2.2)) + 0.25 * nz) > 0.55
        col = np.where(tuft[..., None], np.array([205, 185, 160]) / 255, np.array([38, 22, 14]) / 255) * (0.7 + 0.4 * Hh[..., None])
        spec = 0.35 + 0.2 * Hh; rough = 0.80 - 0.15 * Hh; amp = 0.8
    else:                     # dorado
        Hh = 0.02 * nz
        col = np.array([196, 150, 64]) / 255 * (0.85 + 0.15 * np.sin(np.pi * V)[..., None])
        spec = np.full((h, w), 0.95); rough = np.full((h, w), 0.25); amp = 0.1
    hx = (np.roll(Hh, -1, 1) - np.roll(Hh, 1, 1)) * 0.5 * amp
    hy = (np.roll(Hh, 1, 0) - np.roll(Hh, -1, 0)) * 0.5 * amp
    n = np.stack([-hx, -hy, np.ones_like(Hh)], -1); n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return np.clip(col, 0, 1), n, np.clip(spec, 0, 1), np.clip(rough, 0, 1), Hh

def textures(flipv, Lc):
    b = braid_tex(flipv, Lc)
    rng = np.random.default_rng(5)
    W2, H2 = TW, TH
    out = [np.zeros((2 * H2, W2, 3)), np.zeros((2 * H2, W2, 3)), np.zeros((2 * H2, W2)), np.zeros((2 * H2, W2)), np.zeros((2 * H2, W2))]
    for k in range(5): out[k][:H2] = b[k]
    for kind, rect in (('cord', UV_CORD), ('sleeve', UV_SLEEVE), ('knot', UV_KNOT), ('gold', UV_GOLD)):
        x0, x1 = int(rect[0] * W2), int(rect[2] * W2)
        z = zone_tex(kind, x1 - x0, H2, rng)
        for k in range(5): out[k][H2:, x0:x1] = z[k]
    return out

def write_ftex(template, rgba, out, mips=7):
    from PIL import Image
    T = Ftex(open(template, 'rb').read())
    h, w = rgba.shape[:2]
    T.w, T.h, T.d = w, h, 1
    pf = {2: 'DXT1', 4: 'DXT5'}[T.fmt]
    cur = rgba.astype(np.float32); ms = []
    for i in range(mips):
        if i: cur = 0.25 * (cur[0::2, 0::2] + cur[1::2, 0::2] + cur[0::2, 1::2] + cur[1::2, 1::2])
        im = Image.fromarray(np.clip(np.round(cur * 255), 0, 255).astype(np.uint8), 'RGBA')
        b = io.BytesIO(); im.save(b, format='DDS', pixel_format=pf); dd = b.getvalue()
        bs = 8 if pf == 'DXT1' else 16
        need = max(1, cur.shape[1] // 4) * max(1, cur.shape[0] // 4) * bs
        ms.append(dd[-need:])
    T.mips = ms
    hdr = T.header
    hdr[48:64] = os.urandom(16)                        # hash propio (no el del accesorio de Konami)
    open(out, 'wb').write(T.to_bytes(chunk=65536, small_first=True))
    return T

def main():
    tape_p, arm_p, accdir, pid, outdir = sys.argv[1:6]
    tape = F_.Fmdl(tape_p); arm = F_.Fmdl(arm_p)
    c, ax, e1, e2, rb, nsk = wrist_frame(tape, arm)
    print("muneca: centro %s eje %s, %d vertices de piel, radio %.1f..%.1f mm" % (np.round(c, 4), np.round(ax, 3), nsk, rb.min() * 1000, rb.max() * 1000))
    M = build_mesh(c, ax, e1, e2, rb)
    bi, bw = skin_weights(tape, M['pos'])
    win = os.path.join(outdir, pid, '#Win'); src = os.path.join(outdir, pid, 'sourceimages', '#windx11')
    os.makedirs(win, exist_ok=True); os.makedirs(src, exist_ok=True)
    tex_dir = '/Assets/pes16/model/character/face/real/%s/sourceimages/' % pid
    names = {'accessory_bsm.tga': 'pulsera_bsm.tga', 'accessory_nrm.tga': 'pulsera_nrm.tga', 'accessory_srm.tga': 'pulsera_srm.tga'}
    sz = write_fmdl(tape_p, os.path.join(win, 'wristtaping_l.fmdl'), M, bi, bw, tex_dir, names)
    print("fmdl: %d vertices, %d triangulos, %d bytes" % (len(M['pos']), len(M['faces']), sz))
    bsm, n, spec, rough, H = textures(M['flipv'], M['braid_len'])
    one = np.ones(bsm.shape[:2])
    write_ftex(os.path.join(accdir, 'accessory_bsm.ftex'), np.dstack([bsm, one]), os.path.join(src, 'pulsera_bsm.ftex'))
    nm = np.dstack([one, n[..., 1] * 0.5 + 0.5, 0 * one, n[..., 0] * 0.5 + 0.5])   # DXT5: G = y, A = x (como accessory_nrm)
    write_ftex(os.path.join(accdir, 'accessory_nrm.ftex'), nm, os.path.join(src, 'pulsera_nrm.ftex'))
    write_ftex(os.path.join(accdir, 'accessory_srm.ftex'), np.dstack([spec, rough, 0 * one, one]), os.path.join(src, 'pulsera_srm.ftex'))
    np.savez_compressed(os.path.join(outdir, 'preview_data.npz'), pos=M['pos'], nrm=M['nrm'], uv=M['uv'], faces=M['faces'],
                        bsm=bsm.astype(np.float16), n=n.astype(np.float16), H=H.astype(np.float16), c=c, ax=ax)
    print("texturas en", src)

if __name__ == '__main__':
    main()
