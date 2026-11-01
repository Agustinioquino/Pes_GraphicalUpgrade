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
                     'x0': xs[ok][0], 'x1': xs[ok][-1], 'down': down})
    return rows

def tooth_fields(rows, N):
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
        inside = (v >= -0.15) & (v <= 1.1) & (xs >= r['x0'] - 2)[None, :] & (xs <= r['x1'] + 2)[None, :]
        U = np.where(inside, np.broadcast_to(u[None, :], (N, N)), U)
        V = np.where(inside, v, V)
        W = np.where(inside, np.broadcast_to(w[None, :], (N, N)), W)
    return U, V, W

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
    u = np.clip(U, 0, 1); v = np.clip(V, 0, 1)
    # distancia al limite entre dientes en pixeles
    du = np.minimum(u, 1 - u) * np.maximum(W, 1)
    # surco interproximal: fino en el punto de contacto, abierto (troneras) junto a la encia y en el borde incisal
    open_ = 0.6 + 1.6 * sst(0.35, 0.0, v) + 1.8 * sst(0.75, 1.0, v)
    line = np.exp(-(du / (2.2 * open_)) ** 2) * sst(0.0, 0.08, V + 0.05) * sst(1.1, 0.95, V)
    # esquinas incisales redondeadas: la tronera incisal deja ver el fondo de la boca
    cr = 0.22
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
    D *= (1 + 0.015 * lobes + 0.008 * perik + 0.012 * fine)[..., None]
    D *= 1 + cerv[..., None] * np.array([0.015, -0.02, -0.08], np.float32)          # dentina calida en el cuello
    D *= 1 + incis[..., None] * np.array([-0.04, -0.02, 0.03], np.float32)          # borde incisal: esmalte fino, gris azulado
    D *= (1 - 0.32 * line - 0.45 * corner)[..., None]
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
    h = on * (0.9 * crown + 0.35 * lobes + 0.022 * perik + 0.04 * fine - 0.18 * groove - 0.7 * line - 0.6 * corner)
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
    return {'bsm': bsm_o, 'nrm': nrm_o, 'srm': S, 'trm': Tr}, (B, on, line, crown, corner, cerv)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('src'); ap.add_argument('dst'); ap.add_argument('--preview')
    a = ap.parse_args()
    tex, dbg = build(a.src)
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
