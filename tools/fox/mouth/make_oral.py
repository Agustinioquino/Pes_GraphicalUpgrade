# PES21 FX (31/10) - texturas nuevas de la boca (dientes, encias, lengua) a 2048 sobre la MISMA malla de Konami (mouth.fmdl).
# Parte de las oral_*.ftex del juego (las lee de la carpeta que se le pase; no van al repo) y arma, en el mismo espacio UV:
#   pes_oral_bsm.dds  2048  color: bordes de cada diente nitidos, separacion entre dientes, cuello del diente mas calido (dentina),
#                           borde incisal mas claro y apenas azulado, variacion fina del esmalte, encia con punteado y margen
#   pes_oral_nrm.dds  2048  normal (mismo formato que oral_nrm: X en A, Y en G): corona redondeada por diente, surcos entre
#                           dientes, microrrelieve del esmalte (lineas horizontales finas) y de la encia
#   pes_oral_srm.dds  1024  brillo: el del juego, menos en los surcos y en el cuello del diente (el esmalte no es un espejo)
#   pes_oral_trm.dds  1024  translucidez: la del juego con los bordes de los dientes nitidos
# Todas RGBA8 UNORM (el shader 63fab773 pasa el color a lineal a mano) con mips. Las enlaza la DLL por PES21FX_rules.ini
# ([rule 63fab7735eee149c] srv = 0..3 file:textures\pes_oral_*.dds): si falta un archivo queda la textura del juego.
# Uso: python make_oral.py <carpeta con oral_bsm/nrm/srm/trm.ftex> <carpeta de salida (PES21FX\textures)> [--preview x.png]
import sys, os, io, struct, argparse
import numpy as np
from scipy import ndimage as ndi
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
from ftex import Ftex, dxt_to_dds

def load(path):
    from PIL import Image
    f = Ftex(open(path, 'rb').read())
    im = Image.open(io.BytesIO(dxt_to_dds(f))); im.load()
    return np.asarray(im.convert('RGBA'), dtype=np.float32) / 255.0

def up(a, n):
    """bicubica a n x n (por canal), recortada a 0..1"""
    z = n / a.shape[0]
    return np.clip(np.stack([ndi.zoom(a[..., c], z, order=3, mode='wrap') for c in range(a.shape[2])], -1), 0, 1)

def sst(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def noise(n, sigma, seed, aniso=(1, 1)):
    r = np.random.default_rng(seed).standard_normal((n, n)).astype(np.float32)
    g = ndi.gaussian_filter(r, (sigma * aniso[1], sigma * aniso[0]), mode='wrap')
    return g / (g.std() + 1e-6)

def masks(bsm, trm):
    """dientes / tejido (encia, lengua) / fondo con la misma clasificacion que el shader 63fab773"""
    c = bsm[..., :3]; mx = c.max(-1); sat = (mx - c.min(-1)) / np.maximum(mx, 1e-3); rg = c[..., 0] - c[..., 1]
    t = trm[..., 1]
    teeth = np.clip(sst(0.30, 0.45, mx) * sst(0.38, 0.26, sat) + 0.5 * sst(0.35, 0.60, t) * sst(0.25, 0.40, mx), 0, 1) * sst(0.16, 0.11, rg)
    tissue = (1 - teeth) * sst(0.07, 0.13, rg) * sst(0.13, 0.22, mx)
    return teeth, tissue

def mips_of(a):
    out = [a]
    while out[-1].shape[0] > 1:
        m = out[-1]; h, w = m.shape[:2]
        out.append(m.reshape(h // 2, 2, w // 2, 2, -1).mean((1, 3)))
    return out

def write_rgba8(path, a):
    ms = mips_of(np.clip(a, 0, 1).astype(np.float32))
    h, w = a.shape[:2]
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000
    hdr = struct.pack('<4sIIIIIII', b'DDS ', 124, flags, h, w, w * 4, 0, len(ms)) + b'\0' * 44
    hdr += struct.pack('<II4sIIIII', 32, 0x4, b'DX10', 0, 0, 0, 0, 0)
    hdr += struct.pack('<IIIII', 0x1000 | 0x400008, 0, 0, 0, 0)
    hdr += struct.pack('<IIIII', 28, 3, 0, 1, 0)   # R8G8B8A8_UNORM (no sRGB: el shader convierte), TEXTURE2D
    with open(path, 'wb') as f:
        f.write(hdr)
        for m in ms: f.write((m * 255 + 0.5).astype(np.uint8).tobytes())

def rows_layout(teeth512):
    """filas de dientes del atlas de Konami (512): por fila, borde de la encia y borde incisal por columna y las papilas
    (puntas de encia entre dientes = limites entre dientes). Arriba: encia arriba, incisal abajo; abajo: al reves."""
    from scipy import signal
    te = ndi.gaussian_filter(teeth512, 0.7)
    rows = []
    for y0, y1, down in ((60, 170, True), (230, 310, False)):
        ys = np.arange(y0, y1); g = np.full(512, np.nan); inc = np.full(512, np.nan)
        for x in range(512):
            col = te[y0:y1, x] > 0.5
            if col.sum() >= 3:
                g[x] = ys[col][0] if down else ys[col][-1]; inc[x] = ys[col][-1] if down else ys[col][0]
        ok = ~np.isnan(g)
        xs = np.arange(512)
        gf = np.interp(xs, xs[ok], g[ok]); incf = np.interp(xs, xs[ok], inc[ok])
        gs = ndi.gaussian_filter1d(gf, 1.0)
        pk, _ = signal.find_peaks(gs if down else -gs, prominence=2.5)
        rows.append({'g': ndi.gaussian_filter1d(gf, 2.0), 'inc': ndi.gaussian_filter1d(incf, 3.0), 'pk': pk,
                     'x0': xs[ok][0], 'x1': xs[ok][-1], 'down': down, 'gum': GUM_UP if down else GUM})
    return rows

def tooth_fields(rows, N, vmin=-0.15):
    """campos por diente a resolucion N: u (0..1 a lo ancho del diente), v (0 encia .. 1 incisal), id de diente, ancho"""
    s = N / 512.0
    U = np.full((N, N), -1.0, np.float32); V = np.full((N, N), -1.0, np.float32); W = np.zeros((N, N), np.float32)
    yy = np.arange(N, dtype=np.float32)[:, None]
    for r in rows:
        xs = (np.arange(N) + 0.5) / s - 0.5
        g = np.interp(xs, np.arange(512), r['g']) * s; inc = np.interp(xs, np.arange(512), r['inc']) * s
        pk = np.concatenate([[r['pk'][0] - (r['pk'][1] - r['pk'][0])], r['pk'], [r['pk'][-1] + (r['pk'][-1] - r['pk'][-2])]]).astype(np.float32)
        i = np.clip(np.searchsorted(pk, xs) - 1, 0, len(pk) - 2)
        u = (xs - pk[i]) / (pk[i + 1] - pk[i]); w = (pk[i + 1] - pk[i]) * s
        v = (yy - g[None, :]) / (inc - g)[None, :]
        inside = (v >= vmin) & (v <= 1.1) & (xs >= r['x0'] - 2)[None, :] & (xs <= r['x1'] + 2)[None, :]
        U = np.where(inside, np.broadcast_to(u[None, :], (N, N)), U)
        V = np.where(inside, v, V)
        W = np.where(inside, np.broadcast_to(w[None, :], (N, N)), W)
    return U, V, W

def tooth_ids(rows, N):
    """(01/11 e) id de diente por pixel (-1 fuera) y tipo de cada id ('IC','I','C','P'), con el mismo criterio que mouth3d"""
    s = N / 512.0
    tid = np.full((N, N), -1, np.int32); kinds = []
    xs = (np.arange(N) + 0.5) / s - 0.5
    U, V, W = tooth_fields(rows, N)
    base = 0
    for ri, r in enumerate(rows):
        pk = np.concatenate([[r['x0']], r['pk'], [r['x1']]]).astype(float)
        ntt = len(pk) - 1; mid = ntt / 2.0
        col = np.clip(np.searchsorted(pk, xs) - 1, 0, ntt - 1)
        g = np.interp(xs, np.arange(512), r['g']) * s; inc = np.interp(xs, np.arange(512), r['inc']) * s
        yy = np.arange(N)[:, None]
        lo, hi = np.minimum(g, inc), np.maximum(g, inc)
        span = hi - lo
        inrow = (yy >= (lo - 0.3 * span)[None]) & (yy <= (hi + 0.15 * span)[None]) & ((xs >= r['x0'] - 2) & (xs <= r['x1'] + 2))[None]
        tid = np.where(inrow, base + col[None, :], tid)
        for i in range(ntt):
            rank = int(abs((i + 0.5) - mid)); up = ri == 0
            kinds.append('IC' if (rank == 0 and up) else ('I' if rank < 2 else ('C' if rank == 2 else 'P')))
        base += ntt
    return tid, kinds

TOOTH_FLAT = float(os.environ.get('TOOTH_FLAT', '0.9'))   # (02/11 v18) cuanto se empareja el sombreado horneado de los dientes
GUM = 0.08   # (01/11 h) 0.18 -> 0.08: dientes mas largos del lado de la encia (Agustin "muy cortos"); mouth3d.py usa el mismo valor
# (02/11 v17b) Agustin: 'respetar tambien el tamano de la encia para los dientes de arriba': la fila de arriba deja la linea de
# la encia donde la pinto Konami (sin correrla). Cada fila lleva su valor en rows[i]['gum'] (rows_layout)
GUM_UP = float(os.environ.get('GUM_UP', '0.0'))

def gum_field(rows, N, vmin):
    """valor de 'gum' de la fila de cada pixel (mismo criterio de 'adentro' que tooth_fields; afuera = GUM)"""
    s = N / 512.0
    G = np.full((N, N), GUM, np.float32)
    yy = np.arange(N, dtype=np.float32)[:, None]
    xs = (np.arange(N) + 0.5) / s - 0.5
    for r in rows:
        g = np.interp(xs, np.arange(512), r['g']) * s; inc = np.interp(xs, np.arange(512), r['inc']) * s
        v = (yy - g[None, :]) / (inc - g)[None, :]
        inside = (v >= vmin) & (v <= 1.1) & (xs >= r['x0'] - 2)[None, :] & (xs <= r['x1'] + 2)[None, :]
        G = np.where(inside, np.float32(r.get('gum', GUM)), G)
    return G

def gum_warp(img, rows, gum=None):
    """corre la linea de la encia de las dos tiras: en cada columna, con s = 0 en la encia pintada y 1 en el borde incisal,
    la encia [-gum, 0] se estira a [-gum, gum] y el diente [0, 1] se comprime a [gum, 1] (mapa continuo: el festoneado se
    conserva y el diente pintado queda entero, mas corto)"""
    n = img.shape[0]; sc = n / 512.0
    yy = (np.arange(n) + 0.5) / sc - 0.5
    src = np.repeat(yy[:, None], n, 1)
    xs = (np.arange(n) + 0.5) / sc - 0.5
    for r in rows:
        gr = r.get('gum', GUM) if gum is None else gum
        if gr <= 0: continue
        g = np.interp(xs, np.arange(512), r['g']); inc = np.interp(xs, np.arange(512), r['inc'])
        ins = (xs >= r['x0'] - 2) & (xs <= r['x1'] + 2)
        sv = (yy[:, None] - g[None]) / (inc - g)[None]
        m = ins[None] & (sv >= -gr) & (sv <= 1.0)
        # tramo de la encia: s en [-gum, gum] -> fuente [-gum, 0];  tramo del diente: [gum, 1] -> [0, 1]
        ssrc = np.where(sv < gr, -gr + (sv + gr) * 0.5, (sv - gr) / (1 - gr))
        src = np.where(m, (g[None] + ssrc * (inc - g)[None]), src)
    src = (src + 0.5) * sc - 0.5
    xx = np.repeat(np.arange(n, dtype=np.float64)[None], n, 0)
    out = np.stack([ndi.map_coordinates(img[..., c], [src, xx], order=1, mode='nearest') for c in range(img.shape[2])], -1)
    return out.astype(img.dtype)

def clean_gum(bsm, nrm, rows, tissue0, gum=None):
    """(01/11 d) margen de la encia limpio: gum_warp estira la encia pintada al doble (vetas verticales, 'pelo') y el borde
    pintado de Konami es dentado (bloques del DXT de 512). En la franja [-1.6 gum, gum + papila] el color pasa a ser la encia
    promediada (sin vetas) + punteado fino, y el borde encia/diente es una curva suave por diente: festoneado con la papila
    bajando entre dientes, con el margen apenas mas oscuro. El diente pintado de abajo queda (la boca de Konami lo sigue usando);
    la boca 3D usa solo el centro del diente pintado."""
    N = bsm.shape[0]
    U, V, W = tooth_fields(rows, N, vmin=-1.0)
    if gum is None: gum = gum_field(rows, N, -1.0)
    ins = U >= 0
    u = np.clip(U, 0, 1)
    du = np.minimum(u, 1 - u)
    pap = 0.30 * np.exp(-(du / 0.14) ** 2)                        # papila: baja entre dientes hacia el borde incisal
    edge = gum + pap
    # encia promediada con convolucion normalizada sobre los pixeles de encia (no mezcla color de diente)
    rgb = bsm[..., :3]
    wg = (ins & (V < gum - 0.02) & (V > -0.9)).astype(np.float32)
    def blur(a, sg): return ndi.gaussian_filter(a, sg, mode='wrap')
    num = np.stack([blur(rgb[..., c] * wg, (5, 14)) for c in range(3)], -1); den = blur(wg, (5, 14))[..., None]
    base = num / np.maximum(den, 1e-4)
    base = np.where(den > 0.02, base, rgb)
    st = noise(N, 1.3, 21) * 0.010 + noise(N, 4.0, 22) * 0.018
    gumc = base * (1 + st)[..., None]
    # margen: 0..1 a lo largo de la franja, mas oscuro y un poco mas rojo pegado al diente
    dm = np.clip((edge - V) / 0.10, 0, 1)
    gumc = gumc * (1 - 0.10 * np.exp(-(dm / 0.35) ** 2))[..., None] * (1 + np.array([0.02, -0.03, -0.03], np.float32) * np.exp(-(dm / 0.35) ** 2)[..., None])
    a_gum = sst(edge + 0.012, edge - 0.012, V) * sst(-1.6 * gum - 0.1, -1.6 * gum, V) * ins
    a_gum = ndi.gaussian_filter(a_gum.astype(np.float32), 0.8)
    out = bsm.copy()
    out[..., :3] = rgb * (1 - a_gum[..., None]) + gumc * a_gum[..., None]
    # normal: en la franja nueva, plana + el rodete del margen (la de Konami estirada tenia las mismas vetas)
    nx = nrm[..., 3] * 2 - 1; ny = nrm[..., 1] * 2 - 1
    h = 0.6 * np.exp(-(dm / 0.25) ** 2) + 0.05 * noise(N, 1.3, 23)
    gy, gx = np.gradient(ndi.gaussian_filter(h, 1.0))
    sc = 6.0
    nx2 = nx * (1 - a_gum) + (-gx * sc) * a_gum; ny2 = ny * (1 - a_gum) + (-gy * sc) * a_gum
    nz = np.sqrt(np.clip(1 - nx2 * nx2 - ny2 * ny2, 0.04, 1)); ln = np.sqrt(nx2 * nx2 + ny2 * ny2 + nz * nz)
    nrm = nrm.copy(); nrm[..., 3] = (nx2 / ln) * 0.5 + 0.5; nrm[..., 1] = (ny2 / ln) * 0.5 + 0.5
    return out, nrm

def gum_detail(bsm, nrm, rows):
    """(01/11 g) mas definicion en la encia (Agustin): la de Konami viene de 512 y queda lisa/borrosa a 2048. Sobre todo el
    tejido de las tiras (no lengua ni paladar): punteado de 'cascara de naranja' de la encia adherida (fino, cerca del
    diente), capilares finos rojizos en la mucosa (lejos del diente, mas roja y lisa), linea mucogingival suave, y el relieve
    del punteado y de los capilares en el normal map."""
    N = bsm.shape[0]
    rgb = bsm[..., :3]
    mx = rgb.max(-1); rg = rgb[..., 0] - rgb[..., 1]
    gy, gx = (np.mgrid[0:N, 0:N] + 0.5) / N
    strips = sst(0.665, 0.64, gy)                                   # filas de dientes/encia (arriba de la lengua)
    tis = sst(0.04, 0.10, rg) * sst(0.05, 0.12, mx) * strips
    U, V, W = tooth_fields(rows, N, vmin=-1.6)
    ins = U >= 0
    # distancia al margen (en alto de diente): 0 en el margen, crece hacia la raiz
    dm = np.where(ins, np.clip(gum_field(rows, N, -1.6) - V, 0, 2), 1.0)
    attached = sst(0.75, 0.45, dm)                                   # encia adherida (con punteado) cerca del diente
    mucosa = 1 - attached
    # punteado: hoyuelos finos, mas marcados en la adherida
    st1 = noise(N, 0.8, 41); st2 = noise(N, 1.6, 42)
    pits = np.clip(-st1 - 0.35, 0, None)                            # hoyuelos (solo el lado negativo)
    stipple = 0.6 * pits + 0.25 * st2
    # capilares: lineas finas (ridged noise), anisotropas, en la mucosa y apenas en la adherida
    r1 = 1 - np.abs(noise(N, 3.0, 43, aniso=(1.0, 2.2)))
    r2 = 1 - np.abs(noise(N, 2.0, 44, aniso=(2.0, 1.0)))
    vess = np.clip((np.maximum(r1, 0.8 * r2) - 0.89) / 0.11, 0, 1) ** 1.3
    vess *= sst(-0.3, 0.6, noise(N, 18, 45))                        # por zonas, no en todos lados
    w_v = vess * (0.2 * attached + 0.6 * mucosa)   # (02/11 v18) capilares mas suaves (encia con 'pelos')
    # color: adherida coral mas clara y mate, mucosa algo mas roja y oscura; capilares rojo oscuro
    # nitidez: la base de 512 agrandada queda blanda -> mascara de enfoque + micro variacion
    col = np.clip(rgb + 0.4 * (rgb - np.stack([ndi.gaussian_filter(rgb[..., c], 2.5) for c in range(3)], -1)), 0, 1)
    # (02/11 v18) acabado: el grano salia como sal y pimienta; punteado mas suave y grano a la mitad
    col = col * (1 + 0.015 * noise(N, 0.8, 46))[..., None]
    lum_mod = 1 - 0.06 * ndi.gaussian_filter(stipple, 0.6) * (0.4 + 0.6 * attached)
    col = col * lum_mod[..., None]
    col = col * (1 + mucosa[..., None] * np.array([0.02, -0.07, -0.05], np.float32))
    col = col * (1 + attached[..., None] * np.array([0.03, 0.02, 0.02], np.float32))
    vcol = rgb * np.array([0.78, 0.42, 0.45], np.float32)
    col = col * (1 - 0.7 * w_v[..., None]) + vcol * (0.7 * w_v[..., None])
    a = np.clip(tis, 0, 1)[..., None]
    out = bsm.copy(); out[..., :3] = np.clip(rgb * (1 - a) + col * a, 0, 1)
    # normal: hoyuelos del punteado y capilares apenas en relieve
    h = (-0.9 * pits * (0.35 + 0.65 * attached) + 0.25 * vess * mucosa) * tis
    hy, hx = np.gradient(ndi.gaussian_filter(h, 0.6))
    nx = nrm[..., 3] * 2 - 1; ny = nrm[..., 1] * 2 - 1
    sc = 1.6
    nx2 = nx - hx * sc; ny2 = ny - hy * sc
    nz = np.sqrt(np.clip(1 - nx2 * nx2 - ny2 * ny2, 0.04, 1)); ln = np.sqrt(nx2 * nx2 + ny2 * ny2 + nz * nz)
    nrm = nrm.copy(); nrm[..., 3] = (nx2 / ln) * 0.5 + 0.5; nrm[..., 1] = (ny2 / ln) * 0.5 + 0.5
    return out, nrm

def build(src, N=2048, NS=1024, log=print):
    bsm = load(os.path.join(src, 'oral_bsm.ftex')); nrm = load(os.path.join(src, 'oral_nrm.ftex'))
    srm = load(os.path.join(src, 'oral_srm.ftex')); trm = load(os.path.join(src, 'oral_trm.ftex'))
    log('  origen %dx%d' % bsm.shape[:2])
    t512, _ = masks(bsm, trm)
    rows = rows_layout(t512)
    for k, r in enumerate(rows): log('  fila %d: %d dientes, limites en x = %s' % (k, len(r['pk']) + 1, r['pk'].tolist()))
    B = up(bsm, N); T = up(trm, N); Nn = up(nrm, N)
    teeth0, tissue0 = masks(B, T)
    teeth = ndi.gaussian_filter(teeth0, 1.5)
    U, V, W = tooth_fields(rows, N)
    on = (U >= 0).astype(np.float32) * teeth
    # (02/11 v18) todo el diente pintado cuenta como diente, tambien la parte que Konami pinto gris/oscura (la mitad de los
    # de abajo tapada por los de arriba): la boca 3D la usa entera
    onF = (U >= 0) * sst(-0.03, 0.03, V) * sst(1.0, 0.95, V)
    on = np.maximum(on, ndi.gaussian_filter(onF.astype(np.float32), 1.0))
    u = np.clip(U, 0, 1); v = np.clip(V, 0, 1)
    # distancia al limite entre dientes en pixeles
    du = np.minimum(u, 1 - u) * np.maximum(W, 1)
    # surco interproximal: fino en el punto de contacto, abierto (troneras) junto a la encia y en el borde incisal
    open_ = 0.6 + 1.6 * sst(0.35, 0.0, v) + 1.8 * sst(0.75, 1.0, v)
    line = np.exp(-(du / (2.2 * open_)) ** 2) * sst(0.0, 0.08, V + 0.05) * sst(1.1, 0.95, V)
    # esquinas incisales redondeadas: la tronera incisal deja ver el fondo de la boca
    cr = 0.14   # (01/11 b) 0.22 -> 0.14: esquinas incisales mas cuadradas (Agustin)
    cx = np.clip(cr - np.minimum(u, 1 - u), 0, None) / cr; cy = np.clip(v - (1 - cr), 0, None) / cr
    corner = sst(1.02, 1.3, np.sqrt(cx * cx + cy * cy)) * sst(1.06, 0.98, V)
    # corona: lomo central mas claro / convexo, tres lobulos (mamelones) apenas marcados cerca del borde
    crown = np.sqrt(np.clip(1 - (2 * u - 1) ** 2, 0, 1))
    lobes = np.cos(6 * np.pi * u) * sst(0.55, 0.9, v) * sst(30, 60, W)
    perik = np.sin(v * 2 * np.pi * 34 + noise(N, 6, 7) * 0.8)
    fine = noise(N, 0.9, 1)
    cerv = sst(0.3, 0.0, v); incis = sst(0.7, 1.0, v)
    # --- color: factor sobre el color de Konami (conserva su sombreado general y la clasificacion del shader)
    D = np.ones((N, N, 3), np.float32)
    D *= (1 + 0.07 * (crown - 0.6))[..., None]
    D *= (1 + 0.015 * lobes + 0.003 * perik * cerv + 0.010 * fine)[..., None]   # (02/11 v18) periquimatias solo junto al cuello (se veian rayas)
    D *= 1 + cerv[..., None] * np.array([0.015, -0.02, -0.08], np.float32)          # dentina calida en el cuello
    D *= 1 + incis[..., None] * np.array([-0.04, -0.02, 0.03], np.float32)          # borde incisal: esmalte fino, gris azulado
    D *= (1 - 0.20 * line - 0.30 * corner)[..., None]   # (02/11 v18) linea entre dientes 0.32 -> 0.20 (rayas negras finas)
    rgb = B[..., :3]
    # los bloques de 4x4 del DXT1 de 512 se ven a 2048: dentro de los dientes el color base sale suavizado (el detalle lo pone D)
    soft = np.stack([ndi.gaussian_filter(rgb[..., c], 3.0) for c in range(3)], -1)
    wsm = (0.75 * sst(0.6, 0.95, ndi.minimum_filter(on, 7)))[..., None]
    rgb = rgb * (1 - wsm) + soft * wsm
    out = rgb * (1 + (D - 1) * on[..., None])
    # encia: punteado suave y margen un poco mas oscuro pegado al diente
    tis = np.clip(tissue0 * (1 - teeth), 0, 1)
    marg = np.exp(-ndi.distance_transform_edt(teeth0 < 0.5).astype(np.float32) / 5.0) * (teeth0 < 0.5)
    out = out * (1 + tis * (0.025 * noise(N, 1.0, 4) - 0.07 * marg))[..., None]
    # --- (01/11 c) color mas natural (Agustin): el de Konami es marron apagado. Dientes: marfil claro poco saturado (A2),
    # encia y lengua: rosado coral en vez de marron. Se conserva la luminancia de cada pixel (sombreado y detalle) y se cuida la
    # clasificacion del shader 63fab773 (dientes: max > 0.45, saturacion < 0.26, R-G < 0.11; tejido: R-G > 0.13).
    lumw = np.array([0.30, 0.55, 0.15], np.float32)
    Lp = (out * lumw).sum(-1, keepdims=True)
    # (02/11 v18) acabado (Agustin 'mejor acabado en las texturas'): el diente pintado de Konami trae sombreado horneado
    # (manchas grises en media corona, mas en la fila de abajo) que la boca 3D hereda. Se saca la luminancia de baja
    # frecuencia dentro de cada diente (convolucion normalizada sobre 'on') y se lleva a un nivel parejo; el detalle fino
    # (lobulos, periquimatias, grano) queda. La luz la pone la malla 3D
    if TOOTH_FLAT > 0:
        wq = (on > 0.5).astype(np.float32)
        Lq = Lp[..., 0]
        low = ndi.gaussian_filter(Lq * wq, 5) / np.maximum(ndi.gaussian_filter(wq, 5), 1e-4)
        Lt = float(np.percentile(Lq[wq > 0], 80))
        shape = 1 + 0.06 * (crown - 0.6) + 0.012 * lobes          # el abombado propio (el de Konami se fue con el pasa-bajos)
        ratio = np.clip((Lt * shape / np.maximum(low, 1e-3)) ** TOOTH_FLAT, 0.6, 2.2)
        ratio = 1 + (ratio - 1) * sst(0.3, 0.8, on)
        out = out * ratio[..., None]; Lp = Lp * ratio[..., None]
        log('  dientes: sombreado de Konami emparejado a luminancia %.3f' % Lt)
    gy0, gx0 = (np.mgrid[0:N, 0:N] + 0.5) / N
    tongue_c = (sst(0.27, 0.31, gx0) * sst(0.74, 0.70, gx0) * sst(0.65, 0.69, gy0))[..., None]
    ivory = np.array([1.0, 0.955, 0.86], np.float32); ivory /= (ivory * lumw).sum()
    coral = np.array([1.0, 0.50, 0.50], np.float32); coral /= (coral * lumw).sum()
    tongc = np.array([1.0, 0.56, 0.56], np.float32); tongc /= (tongc * lumw).sum()
    # (01/11 e) mas natural (Agustin 'poco naturales'): el marfil parejo al 75 % los dejaba de plastico. Ahora 65 % y encima
    # variacion real: cuello mas calido/amarillo (dentina), borde incisal translucido gris azulado con los mamelones,
    # caninos con mas croma, laterales apenas mas claros, y un tono chico distinto por diente (semilla fija)
    tid, tkind = tooth_ids(rows, N)
    wT = (on * 0.65)[..., None]
    out = out * (1 - wT) + (Lp * 1.10 * ivory) * wT
    rng = np.random.default_rng(31)
    tint = np.ones((N, N, 3), np.float32)
    for k in np.unique(tid[tid >= 0]):
        kk = tkind[k]
        t = np.array({'IC': [1.0, 1.0, 1.0], 'I': [1.02, 1.02, 1.01], 'C': [1.0, 0.982, 0.945], 'P': [0.995, 0.985, 0.955]}[kk], np.float32)
        t = t * (1 + rng.uniform(-0.025, 0.025)) * np.array([1, 1 + rng.uniform(-0.008, 0.008), 1 + rng.uniform(-0.02, 0.015)], np.float32)
        tint[tid == k] = t
    vv = np.clip(V, 0, 1)
    cerv_c = 1 + sst(0.45, 0.0, vv)[..., None] * np.array([0.0, -0.02, -0.07], np.float32)
    mam = 0.5 + 0.5 * np.cos(6 * np.pi * u)                      # 3 lobulos: la translucidez entra entre ellos
    edge = sst(0.62, 0.92, vv) * (0.5 + 0.5 * (1 - mam) * sst(0.6, 0.85, vv))
    transl = np.array([0.90, 0.93, 0.98], np.float32)
    grade = cerv_c * tint
    grade = grade * (1 - edge[..., None]) + (grade * transl * 0.93) * edge[..., None]
    out = out * (1 + (grade - 1) * on[..., None])
    wG = (tis * 0.55)[..., None] * (1 - tongue_c)
    out = out * (1 - wG) + (Lp * 1.08 * coral) * wG
    wL = (tis * 0.5)[..., None] * tongue_c
    out = out * (1 - wL) + (Lp * 1.05 * tongc) * wL
    bsm_o = np.concatenate([np.clip(out, 0, 1), np.ones_like(out[..., :1])], -1)
    # --- zonas fuera de las tiras (uv del atlas de Konami): lengua y paladar
    gyy, gxx = (np.mgrid[0:N, 0:N] + 0.5) / N
    tongue = sst(0.27, 0.31, gxx) * sst(0.74, 0.70, gxx) * sst(0.65, 0.69, gyy)
    palate = sst(0.75, 0.78, gxx) * sst(0.97, 0.94, gxx) * sst(0.72, 0.75, gyy) * sst(0.97, 0.94, gyy)
    # --- normal (01/11 v2): la de Konami suavizada (sin los bloques del DXT de 512) + relieve propio por zona
    #     (DXT5nm: X en A, Y en G, R = 1). Con la boca 3D la forma del diente la da la malla: el mapa lleva el detalle.
    nx = Nn[..., 3] * 2 - 1; ny = Nn[..., 1] * 2 - 1
    nx = ndi.gaussian_filter(nx, 2.5); ny = ndi.gaussian_filter(ny, 2.5)
    groove = (np.exp(-((u - 1 / 3) / 0.04) ** 2) + np.exp(-((u - 2 / 3) / 0.04) ** 2)) * sst(0.35, 0.8, v) * sst(30, 60, W)
    stipple = noise(N, 1.3, 11); papil = noise(N, 0.7, 12); fung = sst(2.2, 3.0, noise(N, 2.0, 13))
    margin_roll = np.exp(-(ndi.distance_transform_edt(teeth0 < 0.5).astype(np.float32) / 7.0) ** 2) * (teeth0 < 0.5) * (tis > 0.3)
    rugae = np.sin(gyy * 2 * np.pi * 26 + 3.0 * noise(N, 18, 14)) * sst(0.80, 0.86, gyy)
    tcen = np.exp(-((gxx - 0.505) / 0.012) ** 2)
    h = on * (0.9 * crown + 0.35 * lobes + 0.010 * perik * (0.3 + 0.7 * cerv) + 0.04 * fine - 0.18 * groove - 0.7 * line - 0.6 * corner)
    h += tis * (1 - tongue) * (0.07 * stipple + 0.35 * margin_roll)
    h += tongue * (0.10 * papil + 0.12 * fung - 0.45 * tcen)
    h += palate * 0.18 * rugae
    h = ndi.gaussian_filter(h, 0.7)
    gy, gx = np.gradient(h)
    sc = 0.35 / max(np.percentile(np.abs(gx[on > 0.5]), 95), 1e-4)
    sx = np.sign(np.sum(nx * -gx * on) + 1e-9); sy = np.sign(np.sum(ny * -gy * on) + 1e-9)
    log('  signo de la normal: x %+d  y %+d' % (sx, sy))
    nx2 = nx + sx * -gx * sc; ny2 = ny + sy * -gy * sc
    nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0.04, 1))
    ln = np.sqrt(nx2 * nx2 + ny2 * ny2 + nz * nz); nx2 /= ln; ny2 /= ln
    nrm_o = np.stack([np.ones_like(nx2), ny2 * 0.5 + 0.5, np.zeros_like(nx2), nx2 * 0.5 + 0.5], -1)
    # --- srm (01/11 v2) a 1024. Canales (shader 63fab773): x = especular del juego, que el G-buffer guarda como METALICIDAD
    #     (o2.y) -> bajo en todo (el esmalte y la mucosa son dielectricos); y = rugosidad base (o2.x sale de y y de la varianza
    #     de la normal); z = nitidez del reflejo del ambiente (Konami 0, queda).
    S = up(srm, NS); Tr = up(trm, NS)
    def down(a): return ndi.zoom(a, NS / N, order=1)
    onS = down(on); lineS = down(np.maximum(line, corner)); cervS = down(cerv); crownS = down(crown)
    tisS = down(tis * (1 - tongue)); tgS = down(tongue); paS = down(palate)
    rest = np.clip(1 - onS - tisS - tgS - paS, 0, 1)
    stS = down(stipple); paS_n = down(papil); perS = down(perik)
    spec = 0.08 * onS * (1 - 0.6 * lineS) + 0.06 * tisS + 0.05 * tgS + 0.05 * paS + S[..., 0] * 0.5 * rest
    rough = (onS * (0.34 + 0.06 * cervS + 0.25 * lineS - 0.03 * (crownS - 0.6) + 0.015 * perS)
             + tisS * (0.26 + 0.03 * stS) + tgS * (0.50 + 0.08 * paS_n) + paS * 0.36 + rest * S[..., 1])
    S[..., 0] = np.clip(spec, 0, 1); S[..., 1] = np.clip(rough, 0, 1)
    Tr[..., :3] *= (1 - 0.5 * onS * lineS)[..., None]
    stats = {'dientes': float((on > 0.5).mean()), 'color_dientes': out[on > 0.5].mean(0).round(3).tolist(),
             'color_konami': rgb[on > 0.5].mean(0).round(3).tolist(), 'brillo_dientes': round(float(S[..., 0][onS > 0.5].mean()), 3)}
    log('  ' + str(stats))
    bsm_o, nrm_o, S, Tr = (gum_warp(x, rows) for x in (bsm_o, nrm_o, S, Tr))
    bsm_o, nrm_o = clean_gum(bsm_o, nrm_o, rows, tissue0)
    bsm_o, nrm_o = gum_detail(bsm_o, nrm_o, rows)
    bsm_o, nrm_o, S, Tr = paint_throat(bsm_o, nrm_o, S, Tr, tis * (gyy < 0.64), log)
    bsm_o, nrm_o, S, Tr = paint_molars(bsm_o, nrm_o, S, Tr, on * (V > 0.35) * (V < 0.8), log)
    bsm_o = lift_teeth(bsm_o, log)
    bsm_o, S = wet_tissue(bsm_o, S, log)
    bsm_o = konami_tone(bsm_o, log)
    bsm_o = tongue_ao(bsm_o, B, log)
    nrm_o, S, Tr = detail_maps(bsm_o, nrm_o, S, Tr, rows, log)
    return {'bsm': bsm_o, 'nrm': nrm_o, 'srm': S, 'trm': Tr}, (B, on, line, crown, corner, cerv)

TEETH_LIFT = float(os.environ.get('TEETH_LIFT', '1.0'))   # (04/11) 0 = como v18
def lift_teeth(bsm, log=print):
    """(04/11) Agustin: 'aclarar los dientes de abajo y de arriba pero darle una textura realista tambien' (en el juego, con la boca
    abierta, los de abajo y las muelas se veian como un anillo marron). Solo en lo que el shader 63fab773 toma como diente
    (max > 0.45, sat < 0.26, R-G < 0.11, con borde suave): luminancia mas alta (mediana ~0.56 -> ~0.70, la fila de abajo y las
    muelas un poco mas), menos amarillo (marfil claro A1/B1), las luces comprimidas (sin quemar) y el detalle fino conservado
    (lobulos, periquimatias, translucidez incisal: se escala la luminancia, no se pinta plano) + un moteado suave del esmalte.
    En la caja de las muelas los surcos y fosas quedan, pero menos marrones (antes parecian sucios)."""
    if TEETH_LIFT <= 0: return bsm
    import molar
    N = bsm.shape[0]
    c = bsm[..., :3]; mx = c.max(-1); sat = (mx - c.min(-1)) / np.maximum(mx, 1e-6); rg = c[..., 0] - c[..., 1]
    m = sst(0.30, 0.42, mx) * sst(0.32, 0.24, sat) * sst(0.13, 0.10, rg)
    gy, gx = (np.mgrid[0:N, 0:N] + 0.5) / N
    u0, u1, v0, v1 = molar.MOLAR_BOX
    mbox = sst(u0 - 0.004, u0 + 0.004, gx) * sst(u1 + 0.004, u1 - 0.004, gx) * sst(v0 - 0.004, v0 + 0.004, gy) * sst(v1 + 0.004, v1 - 0.004, gy)
    lo = sst(0.36, 0.38, gy) * sst(0.64, 0.62, gy)
    m = np.maximum(m, mbox)                       # en la caja de las muelas todo es diente (los surcos marrones tambien)
    lumw = np.array([0.30, 0.55, 0.15], np.float32)
    L = (c * lumw).sum(-1)
    # en la caja de las muelas: menos contraste marron (surcos / fosas mas suaves, puntas de cuspide igual)
    Lm = ndi.gaussian_filter(L, 6)
    Lb = L * (1 - mbox) + (Lm + (L - Lm) * 0.6) * mbox
    gain = (1.40 + 0.06 * lo + 0.08 * mbox) ** TEETH_LIFT   # (04/11 b) referencia de Agustin (dientes bien blancos)
    Ln = Lb * gain
    Ln = np.where(Ln > 0.84, 0.84 + (Ln - 0.84) * 0.40, Ln)        # luces comprimidas: no quema el borde incisal
    mot = ndi.gaussian_filter(np.random.default_rng(41).standard_normal((N, N)).astype(np.float32), 9)
    mot /= max(float(np.abs(mot).max()), 1e-6)
    Ln = Ln * (1 + 0.025 * mot)                                     # moteado suave del esmalte
    white = np.array([1.0, 0.985, 0.95], np.float32); white /= (white * lumw).sum()
    hue = c / np.maximum(L, 1e-4)[..., None]                       # color propio del pixel (normalizado a luminancia 1)
    tone = hue * 0.30 + white * 0.70                               # mitad hacia el marfil claro: menos amarillo, conserva variacion
    t = np.clip(tone * Ln[..., None], 0, 1)
    out = bsm.copy()
    w = (m * min(TEETH_LIFT, 1.0))[..., None]
    out[..., :3] = c * (1 - w) + t * w
    o = out[..., :3]; omx = o.max(-1); osat = (omx - o.min(-1)) / np.maximum(omx, 1e-6); org = o[..., 0] - o[..., 1]
    k = (m > 0.5) & (mbox < 0.5)
    for nm, mk in (('arriba', k & (gy < 0.36)), ('abajo', k & (lo > 0.5)), ('muelas', mbox > 0.5)):
        if mk.any(): log('  dientes mas claros %s: luminancia mediana %.3f -> %.3f, color %s' % (
            nm, float(np.median(L[mk])), float(np.median((o * lumw).sum(-1)[mk])), o[mk].mean(0).round(3).tolist()))
    kk = m > 0.5
    log('  clasificacion del shader tras aclarar: diente %.1f%% de lo que era diente' % (
        100 * ((omx > 0.45) & (osat < 0.26) & (org < 0.11))[kk & (mbox < 0.5)].mean()))
    return out

KONAMI_TONE = float(os.environ.get('KONAMI_TONE', '1.0'))   # (04/11 d) 0 = como v20
KT_TEETH = np.array([0.580, 0.518, 0.439], np.float32)   # mediana de los dientes de oral_bsm de Konami (filas, clase diente del shader)
KT_GUM = np.array([0.451, 0.286, 0.267], np.float32)     # mediana de la encia de Konami (R-G > 0.13)
def konami_tone(bsm, log=print):
    """(04/11 d) Agustin (captura de Messi, shader vs Ver original): 'en la vista original los dientes se ven mucho mas realistas',
    'cambia la sombra pero no el color de las encias y dientes'. Ver original usa la textura de Konami: dientes marfil apagado
    (luminancia ~0.53) y encia rosa oscuro poco saturada (~0.34). La v20 los tenia en 0.81 y 0.56 (encia rosa chicle, dientes
    blanco papel). Aca: en las filas (sin lengua ni garganta) el tono pasa al de Konami y la luminancia a la suya (dientes un poco
    mas claros, 0.60), con el detalle propio (bordes, surcos, papilas) a 70% de contraste. La clasificacion del shader se conserva."""
    if KONAMI_TONE <= 0: return bsm
    N = bsm.shape[0]
    c = bsm[..., :3]; mx = c.max(-1); sat = (mx - c.min(-1)) / np.maximum(mx, 1e-6); rg = c[..., 0] - c[..., 1]
    gy, gx = (np.mgrid[0:N, 0:N] + 0.5) / N
    tongue = sst(0.645, 0.665, gy) * sst(0.27, 0.30, gx) * sst(0.745, 0.715, gx)
    area = sst(0.02, 0.04, gy) * (1 - tongue) * sst(0.70, 0.66, gy)
    import molar
    u0, u1, v0, v1 = molar.MOLAR_BOX
    mbox = sst(u0 - 0.004, u0 + 0.004, gx) * sst(u1 + 0.004, u1 - 0.004, gx) * sst(v0 - 0.004, v0 + 0.004, gy) * sst(v1 + 0.004, v1 - 0.004, gy)
    mt = np.maximum(sst(0.30, 0.42, mx) * sst(0.32, 0.24, sat) * sst(0.13, 0.10, rg), mbox) * np.maximum(area, mbox)
    mg = (1 - mt) * sst(0.08, 0.13, rg) * sst(0.12, 0.20, mx) * area
    lumw = np.array([0.30, 0.59, 0.11], np.float32)
    L = (c * lumw).sum(-1)
    Lb = ndi.gaussian_filter(L, 4)
    def retone(mask, target_rgb, target_L, hue_k):
        k = mask > 0.5
        med = float(np.median(L[k])) if k.any() else 0.5
        Ln = (Lb + 0.7 * (L - Lb)) * (target_L / max(med, 1e-3))
        th = target_rgb / (target_rgb * lumw).sum()
        own = c / np.maximum(L, 1e-4)[..., None]
        hue = own * (1 - hue_k) + th * hue_k
        return np.clip(hue * Ln[..., None], 0, 1), med
    w = min(KONAMI_TONE, 1.0)
    tT, mT = retone(mt, KT_TEETH, 0.60, 0.6)
    tG, mG = retone(mg, KT_GUM, 0.34, 0.9)
    tG[..., 0] = np.maximum(tG[..., 0], np.minimum(tG[..., 1] + 0.15, 1.0))   # la encia sigue siendo tejido para el shader (R-G > 0.13)
    out = bsm.copy()
    o = c * (1 - w * (mt + mg))[..., None] + w * (tT * mt[..., None] + tG * mg[..., None])
    out[..., :3] = o
    omx = o.max(-1); osat = (omx - o.min(-1)) / np.maximum(omx, 1e-6); org = o[..., 0] - o[..., 1]
    kt = (mt > 0.5) & (mbox < 0.5); kg = mg > 0.5
    lo = (o * lumw).sum(-1)
    log('  tono Konami: dientes L %.3f -> %.3f %s, encia L %.3f -> %.3f %s' % (mT, float(np.median(lo[kt])), np.median(o[kt], 0).round(3).tolist(),
        mG, float(np.median(lo[kg])), np.median(o[kg], 0).round(3).tolist()))
    log('  clasificacion del shader: diente %.1f%%, tejido %.1f%%' % (100 * ((omx > 0.45) & (osat < 0.26) & (org < 0.11))[kt].mean(),
        100 * (org > 0.13)[kg].mean()))
    return out

TONGUE_AO = float(os.environ.get('TONGUE_AO', '1.0'))   # (04/11 f) 0 = sin oclusion en la lengua
def tongue_ao(bsm, B, log=print):
    """(04/11 f) Agustin: a la lengua 'le falta oclusion'. La de Konami trae la luz horneada: la punta (v alto) iluminada y el fondo,
    los costados y lo que queda bajo los dientes oscuros. Esa luminancia (suavizada, normalizada dentro de la caja de la lengua)
    oscurece la lengua nueva: punta 100%, fondo ~50%, mas un borde oscuro a los costados."""
    if TONGUE_AO <= 0: return bsm
    N = bsm.shape[0]
    gy, gx = (np.mgrid[0:N, 0:N] + 0.5) / N
    tongue = sst(0.29, 0.32, gx) * sst(0.725, 0.695, gx) * sst(0.655, 0.68, gy)
    lumw = np.array([0.30, 0.59, 0.11], np.float32)
    Lk = ndi.gaussian_filter((B[..., :3] * lumw).sum(-1), 10)
    k = tongue > 0.5
    lo, hi = np.percentile(Lk[k], 5), np.percentile(Lk[k], 97)
    ao = np.clip((Lk - lo) / max(hi - lo, 1e-4), 0, 1) ** 0.75
    tu = (gx - 0.503) / 0.205
    occ = (0.50 + 0.50 * ao) * (1 - 0.30 * sst(0.7, 1.0, np.abs(tu)))
    tv = (0.99 - gy) / 0.32
    occ *= 1 - 0.45 * sst(0.45, 1.0, tv)   # hacia la garganta (bajo los dientes de arriba) cada vez menos luz
    w = (tongue * min(TONGUE_AO, 1.0))[..., None]
    out = bsm.copy()
    out[..., :3] = bsm[..., :3] * (1 - w) + bsm[..., :3] * occ[..., None] * w
    Lo = (out[..., :3] * lumw).sum(-1)
    log('  lengua con oclusion: luminancia punta %.3f, medio %.3f, fondo %.3f (factor medio %.2f)' % (
        float(np.median(Lo[k & (gy > 0.92)])), float(np.median(Lo[k & (gy > 0.78) & (gy < 0.86)])), float(np.median(Lo[k & (gy < 0.72)])), float(occ[k].mean())))
    return out

TISSUE_LIFT = float(os.environ.get('TISSUE_LIFT', '1.0'))   # (04/11 b) 0 = como v18
def wet_tissue(bsm, srm, log=print):
    """(04/11 b) Agustin (foto de referencia): 'una textura mas asi para la lengua, con su propio reflejo de las encias y la lengua,
    humedad'. Lengua (caja uv u .29-.72, v > .66) y encias / mucosa (lo que el shader toma como tejido: R-G > 0.13): rosado mas
    claro y vivo (la lengua de Konami estaba casi negra, luminancia ~0.16), la lengua con surco central, papilas finas claras
    (filiformes) y algunas fungiformes rojas; y en el srm (canal y = rugosidad) menos rugosas = mas humedas, con brillo que
    varia (las papilas cortan el reflejo). El canal x (metalicidad para el G-buffer) queda bajo. Respeta la clasificacion del shader."""
    if TISSUE_LIFT <= 0: return bsm, srm
    N = bsm.shape[0]; NS = srm.shape[0]
    c = bsm[..., :3]; rg = c[..., 0] - c[..., 1]
    gy, gx = (np.mgrid[0:N, 0:N] + 0.5) / N
    tongue = sst(0.29, 0.32, gx) * sst(0.725, 0.695, gx) * sst(0.655, 0.68, gy)
    tis = sst(0.11, 0.15, rg) * (1 - tongue)
    lumw = np.array([0.30, 0.55, 0.15], np.float32)
    L = (c * lumw).sum(-1)
    out = bsm.copy()
    # encia / mucosa: rosado coral claro, conserva el detalle (escala la luminancia)
    gum_rgb = np.array([0.80, 0.42, 0.42], np.float32); gum_rgb /= (gum_rgb * lumw).sum()
    Lg = float(np.median(L[tis > 0.5])) if (tis > 0.5).any() else 0.3
    Lgn = L * (0.50 / max(Lg, 1e-3)) ** TISSUE_LIFT
    g = np.clip(gum_rgb * np.clip(Lgn, 0, 0.8)[..., None], 0, 1)
    hue = c / np.maximum(L, 1e-4)[..., None]
    g = np.clip((0.55 * gum_rgb + 0.45 * hue) * np.clip(Lgn, 0, 0.8)[..., None], 0, 1)
    out[..., :3] = c * (1 - tis[..., None]) + g * tis[..., None]
    # lengua: base rosada, surco central, papilas
    rng = np.random.default_rng(57)
    tu = (gx - 0.503) / 0.205; tv = (0.99 - gy) / 0.32                    # -1..1 a lo ancho, 0 punta .. 1 fondo
    # (04/11 f) medido en la malla: v alto = adelante (z 0.149 en v 0.95, 0.117 en v 0.68): la punta es v ~0.99 (antes al reves)
    groove = np.exp(-(tu / 0.13) ** 2) * sst(0.08, 0.35, tv) * sst(0.95, 0.6, tv)   # (04/11 f) surco ancho y suave (era una raya dura)
    fil = ndi.gaussian_filter(rng.standard_normal((N, N)).astype(np.float32), 0.7)
    fil = sst(1.2, 2.2, fil / max(float(fil.std()), 1e-6))                 # puntitos claros (filiformes)
    fun = ndi.gaussian_filter(rng.standard_normal((N, N)).astype(np.float32), 1.6)
    fun = sst(2.6, 3.4, fun / max(float(fun.std()), 1e-6)) * (0.4 + 0.6 * sst(0.5, 0.9, np.abs(tu)) + 0.6 * sst(0.25, 0.0, tv))   # rojas, mas en los bordes y la punta
    low = ndi.gaussian_filter(rng.standard_normal((N, N)).astype(np.float32), 25); low /= max(float(np.abs(low).max()), 1e-6)
    tcol = np.array([0.84, 0.46, 0.45], np.float32)
    # (04/11 f) Agustin: 'la textura de la lengua no me convence, le falta oclusion y una textura mas real': celdas de papilas
    # (relieve irregular a dos escalas), saburra blanquecina leve en el centro y atras, bordes y punta mas rojos
    cell = ndi.gaussian_filter(rng.standard_normal((N, N)).astype(np.float32), 2.2); cell /= max(float(cell.std()), 1e-6)
    cell2 = ndi.gaussian_filter(rng.standard_normal((N, N)).astype(np.float32), 5.0); cell2 /= max(float(cell2.std()), 1e-6)
    Lt = 0.44 * (1 + 0.06 * low - 0.16 * groove + 0.045 * np.tanh(cell) + 0.05 * np.tanh(cell2))
    T = tcol / (tcol * lumw).sum() * Lt[..., None]
    coat = np.exp(-(tu / 0.45) ** 2) * sst(0.25, 0.75, tv) * (0.5 + 0.5 * sst(-0.5, 1.0, cell2))
    T = T * (1 - 0.22 * coat[..., None]) + np.array([0.86, 0.74, 0.72], np.float32) * Lt[..., None] * 1.15 * 0.22 * coat[..., None]
    edge = sst(0.6, 0.95, np.abs(tu)) + sst(0.25, 0.0, tv)
    T = T * (1 + np.clip(edge, 0, 1)[..., None] * np.array([0.06, -0.06, -0.04], np.float32))
    T = T * (1 - 0.30 * fil[..., None]) + np.array([0.86, 0.72, 0.70], np.float32) * Lt[..., None] * 1.25 * 0.30 * fil[..., None]
    T = T * (1 - 0.5 * fun[..., None]) + np.array([0.80, 0.26, 0.28], np.float32) * 0.5 * fun[..., None]
    T = np.clip(T, 0, 1)
    out[..., :3] = out[..., :3] * (1 - tongue[..., None]) + T * tongue[..., None]
    o = out[..., :3]; org = o[..., 0] - o[..., 1]
    log('  tejido humedo: encia %s (lum %.3f -> %.3f), lengua %s; tejido para el shader: encia %.1f%%, lengua %.1f%%' % (
        o[tis > 0.5].mean(0).round(3).tolist(), Lg, float(np.median((o * lumw).sum(-1)[tis > 0.5])), o[tongue > 0.5].mean(0).round(3).tolist(),
        100 * (org[tis > 0.5] > 0.13).mean(), 100 * (org[tongue > 0.5] > 0.13).mean()))
    # srm: rugosidad (y) mas baja = humedo; el reflejo lo cortan las papilas y el surco
    def down(a): return ndi.zoom(a, NS / N, order=1)
    tS, gS = down(tongue), down(tis); filS, funS, grS = down(fil), down(fun), down(groove)
    S = srm.copy()
    rough_t = 0.24 + 0.14 * filS + 0.06 * funS - 0.06 * grS
    rough_g = 0.17 + 0.03 * down(ndi.gaussian_filter(rng.standard_normal((N, N)).astype(np.float32), 1.0))
    S[..., 1] = S[..., 1] * (1 - tS - gS) + rough_t * tS + rough_g * gS
    S[..., 0] = S[..., 0] * (1 - tS - gS) + 0.07 * (tS + gS)
    S[..., :3] = np.clip(S[..., :3], 0, 1)
    return out, S

DETAIL = float(os.environ.get('MAP_DETAIL', '1.0'))   # (04/11 c) 0 = como v18
def detail_maps(bsm, nrm, srm, trm, rows, log=print):
    """(04/11 c) Agustin: 'aprovecha mas las texturas de normal map, srm, trm y propiedades del material'. Sobre lo que ya hay:
    NORMAL: lengua con papilas en relieve (filiformes finas + fungiformes redondas) y surco central hundido; encia con punteado
    (piel de naranja) y festoneado mas marcado; esmalte con ondas suaves (periquimatias) y lobulos. SRM (x = metalicidad para el
    G-buffer, se deja baja; y = rugosidad): esmalte liso y brillante en el centro de la corona, mas aspero en el cuello y entre
    dientes; lengua humeda entre papilas. TRM (y): borde incisal y puntas de cuspides mas translucidos (el shader usa trm.y > 0.3
    en las filas para reconocer diente: solo se SUBE en los dientes), encia apenas translucida en el margen fino."""
    if DETAIL <= 0: return nrm, srm, trm
    N = bsm.shape[0]; NS = srm.shape[0]
    c = bsm[..., :3]; mx = c.max(-1); sat = (mx - c.min(-1)) / np.maximum(mx, 1e-6); rg = c[..., 0] - c[..., 1]
    teeth = sst(0.40, 0.50, mx) * sst(0.30, 0.22, sat) * sst(0.12, 0.09, rg)
    gy, gx = (np.mgrid[0:N, 0:N] + 0.5) / N
    tongue = sst(0.29, 0.32, gx) * sst(0.725, 0.695, gx) * sst(0.655, 0.68, gy)
    tis = sst(0.11, 0.15, rg) * (1 - tongue) * (1 - teeth)
    rng = np.random.default_rng(73)
    def nz(sig): a = ndi.gaussian_filter(rng.standard_normal((N, N)).astype(np.float32), sig); return a / max(float(a.std()), 1e-6)
    tu = (gx - 0.505) / 0.21; tv = (gy - 0.66) / 0.34
    fil = sst(0.6, 2.0, nz(0.8)); fun = sst(2.4, 3.2, nz(1.8))
    groove = np.exp(-(tu / 0.07) ** 2) * sst(0.05, 0.25, tv) * sst(1.0, 0.7, tv)
    orange = nz(1.4)
    # alto: lengua (papilas), encia (punteado), dientes (ondas finas + grano)
    perik = np.sin(gy * 2 * np.pi * 420 + 1.5 * nz(10)) * 0.5
    h = tongue * (0.55 * fil + 0.9 * fun - 1.2 * groove) + tis * 0.35 * orange + teeth * (0.12 * perik + 0.10 * nz(0.9))
    h = ndi.gaussian_filter(h, 0.6) * DETAIL
    hy, hx = np.gradient(h)
    nx = nrm[..., 3] * 2 - 1; ny = nrm[..., 1] * 2 - 1
    sc = 0.9
    nx2 = nx - hx * sc; ny2 = ny - hy * sc
    nzz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0.04, 1)); ln = np.sqrt(nx2 * nx2 + ny2 * ny2 + nzz * nzz)
    out_n = nrm.copy(); out_n[..., 3] = (nx2 / ln) * 0.5 + 0.5; out_n[..., 1] = (ny2 / ln) * 0.5 + 0.5
    # srm / trm a su resolucion
    def down(a): return ndi.zoom(a, NS / N, order=1)
    tS, gS, teS = down(tongue), down(tis), down(teeth)
    filS, funS = down(fil), down(fun)
    # centro de la corona (lejos de los bordes del diente) = esmalte pulido; bordes / cuello = mas aspero
    core = down(ndi.gaussian_filter((teeth > 0.5).astype(np.float32), 6))
    S = srm.copy()
    # esmalte pulido en la corona, cuello mas mate (Agustin 09/11 'reflejo real, tambien como textura')
    rough_te = 0.13 + 0.21 * (1 - sst(0.5, 0.9, core)) + 0.02 * down(nz(1.2))
    S[..., 1] = S[..., 1] * (1 - teS) + np.clip(rough_te, 0.10, 0.6) * teS
    S[..., 0] = S[..., 0] * (1 - teS) + (0.07 + 0.03 * sst(0.5, 0.9, core)) * teS
    S[..., 1] = S[..., 1] * (1 - tS) + np.clip(S[..., 1] + 0.05 * filS - 0.04 * funS, 0.1, 0.7) * tS
    S[..., :3] = np.clip(S[..., :3], 0, 1)
    T = trm.copy()
    # translucidez: borde del diente hacia afuera de la mascara (incisal / puntas) un poco mas alto; solo se sube en dientes
    edge = down(np.clip(ndi.gaussian_filter((teeth > 0.5).astype(np.float32), 3) * -1 + 1, 0, 1) * (teeth > 0.3))
    T[..., :3] = np.maximum(T[..., :3], (T[..., :3] * (1 + 0.35 * edge[..., None])) * teS[..., None] + T[..., :3] * (1 - teS[..., None]))
    T[..., :3] = T[..., :3] * (1 - 0.3 * gS[..., None]) + np.maximum(T[..., :3], 0.18) * 0.3 * gS[..., None]
    T[..., :3] = np.clip(T[..., :3], 0, 1)
    log('  detalle de mapas: rugosidad dientes %.2f (pulido) .. %.2f (cuello), lengua %.2f, encia %.2f; trm diente medio %.2f' % (
        float(S[..., 1][(teS > 0.5) & (down(ndi.gaussian_filter((teeth > 0.5).astype(np.float32), 6)) > 0.9)].mean()) if ((teS > 0.5)).any() else 0,
        float(S[..., 1][(teS > 0.5) & (core < 0.6)].mean()) if ((teS > 0.5) & (core < 0.6)).any() else 0,
        float(S[..., 1][tS > 0.5].mean()), float(S[..., 1][gS > 0.5].mean()), float(T[..., 1][teS > 0.5].mean())))
    return out_n, S, T

def paint_throat(bsm, nrm, srm, trm, gum_mask, log=print):
    """(02/11) fondo de la garganta (throat.py): pinta el rectangulo libre del atlas throat.UV_BOX (velo, pilares, pared de la
    faringe, con la oscuridad de lo hondo) y la uvula en throat.UVULA_BOX. Color a partir del promedio de la encia (combina con
    el resto); normal con el pliegue del arco, foliculos y capilares; brillo de mucosa humeda; translucidez la de la encia.
    (02/11 b) los 4 mapas se pintan con un borde de PAD px extra alrededor (el rectangulo tiene 18 px libres): en los mips
    chicos el texel del borde no mezcla con el marron de alrededor."""
    import throat
    N = bsm.shape[0]; NS = srm.shape[0]; PAD = 14
    gum_rgb = bsm[..., :3][gum_mask > 0.5].mean(0)
    log('  garganta: color de encia base %s' % gum_rgb.round(3).tolist())
    def box(b, n):
        u0, u1, v0, v1 = b
        return int(round(u0 * n)), int(round(u1 * n)), int(round(v0 * n)), int(round(v1 * n))
    def put(dst, y0, y1, x0, x1, a, pad):
        """escribe a (recorte y0:y1, x0:x1) con 'pad' px de borde repetido alrededor, sin salirse de la imagen"""
        ap_ = np.pad(a, ((pad, pad), (pad, pad)) + ((0, 0),) * (a.ndim - 2), mode='edge')
        Y0, Y1, X0, X1 = max(y0 - pad, 0), min(y1 + pad, dst.shape[0]), max(x0 - pad, 0), min(x1 + pad, dst.shape[1])
        dst[Y0:Y1, X0:X1] = ap_[Y0 - (y0 - pad):Y1 - (y0 - pad), X0 - (x0 - pad):X1 - (x0 - pad)]
    bsm = bsm.copy(); nrm = nrm.copy(); srm = srm.copy(); trm = trm.copy()
    tr_gum = trm[..., :3][ndi.zoom(gum_mask, NS / N, order=1) > 0.5].mean(0)
    for b, fn in ((throat.UV_BOX, throat.paint), (throat.UVULA_BOX, throat.paint_uvula)):
        x0, x1, y0, y1 = box(b, N)
        if fn is throat.paint:
            ring = np.concatenate([bsm[y0 - 8:y0, x0:x1, :3].reshape(-1, 3), bsm[y1:y1 + 8, x0:x1, :3].reshape(-1, 3),
                                   bsm[y0:y1, x0 - 8:x0, :3].reshape(-1, 3), bsm[y0:y1, x1:x1 + 8, :3].reshape(-1, 3)])
            col, h, ap, depth, rvar = fn(gum_rgb, x1 - x0, y1 - y0, edge_rgb=ring.mean(0))
        else:
            col, h, ap, depth, rvar = fn(gum_rgb, x1 - x0, y1 - y0)
        put(bsm[..., :3], y0, y1, x0, x1, col, PAD); bsm[max(y0 - PAD, 0):y1 + PAD, max(x0 - PAD, 0):x1 + PAD, 3] = 1
        rg = col[..., 0] - col[..., 1]
        log('  garganta: tejido para el shader %.0f%%, banda ambigua (R-G 0.11..0.13) %.2f%%' % (100 * (rg > 0.13).mean(), 100 * ((rg > 0.11) & (rg < 0.13)).mean()))
        hy, hx = np.gradient(ndi.gaussian_filter(h, (1.6, 0.6)))
        sc = 5.0
        nx, ny = -hx * sc, -hy * sc
        nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0.04, 1)); ln = np.sqrt(nx * nx + ny * ny + nz * nz)
        nm = np.stack([np.ones_like(nx), (ny / ln) * 0.5 + 0.5, np.zeros_like(nx), (nx / ln) * 0.5 + 0.5], -1)
        put(nrm, y0, y1, x0, x1, nm, PAD)
        xs0, xs1, ys0, ys1 = box(b, NS)
        z = lambda a: ndi.zoom(a, (float(ys1 - ys0) / a.shape[0], float(xs1 - xs0) / a.shape[1]), order=1) if np.ndim(a) == 2 else np.zeros((ys1 - ys0, xs1 - xs0)) + a
        apS, rvS = z(ap), z(rvar)
        sm = np.stack([0.06 + 0.02 * apS, np.clip(0.22 + 0.10 * apS + rvS, 0, 1), np.zeros_like(apS)], -1)   # mucosa humeda: brilla mas que la encia
        put(srm[..., :3], ys0, ys1, xs0, xs1, sm, PAD // 2)
        put(trm[..., :3], ys0, ys1, xs0, xs1, np.zeros((ys1 - ys0, xs1 - xs0, 3)) + tr_gum, PAD // 2)
    return bsm, nrm, srm, trm

def paint_molars(bsm, nrm, srm, trm, enamel_mask, log=print):
    """(02/11 g) textura propia de las muelas 3D (molar.py) en el cuadrado libre molar.MOLAR_BOX: paredes con esmalte (cuello mas
    amarillo, vetas, periquimatias en la normal) y cara oclusal con surcos y fosas manchados (marron) y puntas de las cuspides
    claras. Color base = promedio del esmalte de los dientes pintados (combina con los otros dientes 3D). Brillo / translucidez:
    los de los dientes; los surcos mas asperos."""
    import molar
    N = bsm.shape[0]; NS = srm.shape[0]; PAD = 10
    m = enamel_mask > 0.5
    en = bsm[..., :3][m].mean(0) if m.any() else np.array([0.80, 0.77, 0.70])
    log('  muelas: esmalte base %s' % np.round(en, 3).tolist())
    u0, u1, v0, v1 = molar.MOLAR_BOX
    x0, x1, y0, y1 = int(round(u0 * N)), int(round(u1 * N)), int(round(v0 * N)), int(round(v1 * N))
    col, h, groove, cerv = molar.paint(x1 - x0, y1 - y0, en)
    def put(dst, y0, y1, x0, x1, a, pad):
        ap_ = np.pad(a, ((pad, pad), (pad, pad)) + ((0, 0),) * (a.ndim - 2), mode='edge')
        Y0, Y1, X0, X1 = max(y0 - pad, 0), min(y1 + pad, dst.shape[0]), max(x0 - pad, 0), min(x1 + pad, dst.shape[1])
        dst[Y0:Y1, X0:X1] = ap_[Y0 - (y0 - pad):Y1 - (y0 - pad), X0 - (x0 - pad):X1 - (x0 - pad)]
    bsm = bsm.copy(); nrm = nrm.copy(); srm = srm.copy(); trm = trm.copy()
    put(bsm[..., :3], y0, y1, x0, x1, col, PAD); bsm[max(y0 - PAD, 0):y1 + PAD, max(x0 - PAD, 0):x1 + PAD, 3] = 1
    rg = col[..., 0] - col[..., 1]; mx = col.max(-1); sat = (mx - col.min(-1)) / np.maximum(mx, 1e-6)
    log('  muelas: diente para el shader %.0f%% (max > 0.45, sat < 0.26, R-G < 0.11), tejido %.1f%%' % (
        100 * ((mx > 0.45) & (sat < 0.26) & (rg < 0.11)).mean(), 100 * (rg > 0.13).mean()))
    hy, hx = np.gradient(ndi.gaussian_filter(h, 0.8))
    sc = 0.6
    nx, ny = -hx * sc, -hy * sc
    nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0.04, 1)); ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    put(nrm, y0, y1, x0, x1, np.stack([np.ones_like(nx), (ny / ln) * 0.5 + 0.5, np.zeros_like(nx), (nx / ln) * 0.5 + 0.5], -1), PAD)
    xs0, xs1, ys0, ys1 = int(round(u0 * NS)), int(round(u1 * NS)), int(round(v0 * NS)), int(round(v1 * NS))
    z = lambda a: ndi.zoom(a, (float(ys1 - ys0) / a.shape[0], float(xs1 - xs0) / a.shape[1]), order=1)
    gS, cS = z(groove), z(cerv)
    put(srm[..., :3], ys0, ys1, xs0, xs1, np.stack([0.08 * (1 - 0.6 * gS), np.clip(0.34 + 0.06 * cS + 0.25 * gS, 0, 1), np.zeros_like(gS)], -1), PAD // 2)
    em = ndi.zoom(enamel_mask, NS / N, order=1) > 0.5
    tr = trm[..., :3][em].mean(0) if em.any() else trm[..., :3].mean((0, 1))
    put(trm[..., :3], ys0, ys1, xs0, xs1, np.zeros((ys1 - ys0, xs1 - xs0, 3)) + tr * (1 - 0.5 * gS)[..., None], PAD // 2)
    return bsm, nrm, srm, trm

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('src'); ap.add_argument('dst'); ap.add_argument('--preview')
    a = ap.parse_args()
    tex, dbg = build(a.src)
    import player_tone   # (04/11) textura de dientes con tono propio por jugador (Messi): pes_oral_<perfil>_bsm.dds aparte
    for prof in player_tone.TONES: tex['%s_bsm' % prof] = player_tone.tone_bsm(tex['bsm'], prof)
    os.makedirs(a.dst, exist_ok=True)
    import hashlib
    for k, v in tex.items():
        p = os.path.join(a.dst, 'pes_oral_%s.dds' % k); write_rgba8(p, v)
        print('  %s  %d bytes  sha256 %s' % (p, os.path.getsize(p), hashlib.sha256(open(p, 'rb').read()).hexdigest()[:8].upper()))
    if a.preview:
        from PIL import Image
        B, teeth, gap, crown, cerv, inc = dbg  # (on, line, crown, corner, cerv)
        c = lambda x: (np.clip(x, 0, 1) * 255).astype(np.uint8)
        rows = [np.concatenate([c(B[..., :3]), c(tex['bsm'][..., :3])], 1),
                np.concatenate([c(np.stack([gap, crown, cerv], -1)), c(np.stack([tex['nrm'][..., 3], tex['nrm'][..., 1], np.ones_like(gap)], -1))], 1)]
        Image.fromarray(np.concatenate(rows, 0)).save(a.preview)

if __name__ == '__main__':
    main()
