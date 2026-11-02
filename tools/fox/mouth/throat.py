# PES21 FX (02/11) - fondo de la garganta (Agustin: 'mejorar el fondo de la garganta ... realista, unos pocos vertices mas no
# hay drama'). La boca de Konami termina atras en un rectangulo plano (2 triangulos, z ~0.119, x +-0.034, y 1.6255..1.6484)
# con un pedazo marron oscuro del atlas. Se reemplaza por:
#   - velo del paladar + pilares: cortina en el borde de la boca, con el arco del istmo (borde del velo arriba, pilares a los
#     costados) apenas abultado hacia adelante;
#   - pared de atras de la faringe: adentro del arco, 11-14 mm mas atras, con el centro mas hondo; abajo (detras de la
#     lengua) baja y se oscurece (la garganta sigue hacia abajo);
#   - uvula: una gotita colgando del medio del velo.
# Grilla de 21 x 13 (columnas alineadas con los pilares, una fila justo en el borde del arco) + uvula de 8 x 7: 330 vertices,
# ~580 triangulos. La uv de la grilla va a un rectangulo libre del atlas (el que usaba la pared vieja + el hueco de la U de
# las mejillas) y make_oral.py pinta ahi la garganta con la misma grilla (paint()).
import numpy as np

MM = 0.001
# columnas (x, m): se aprietan cerca de los pilares (|x| = 0.016) para que el borde del arco quede definido
_half = [0.0, 0.003, 0.0065, 0.010, 0.0135, 0.016, 0.0178, 0.020, 0.024, 0.029, 0.0348]   # (02/11 b) 33.7 -> 34.8: pasa detras de la mejilla
XS = np.array([-x for x in _half[:0:-1]] + _half)                 # 21 columnas, de -0.0337 a +0.0337
NC = len(XS)
PIL = 0.016                                                      # x de los pilares
NB, NA = 9, 4                                                    # filas abajo del arco (sin contarlo; la NB-1 es un pliegue a 1 mm del arco) y arriba
NR = NB + 1 + NA                                                 # 13 filas: 0 abajo ... NB = borde del arco ... NR-1 arriba
Y_BOT, Y_TOP = 1.6205, 1.6492                                    # abajo baja un poco mas que la pared vieja (boca abierta)
Y_ARCH_C, Y_ARCH_P = 1.6410, 1.6340                              # borde del velo al centro / en los pilares (arco de ~9 mm)
Z_AT = lambda y: 0.1181 + (np.clip(y, 1.6255, 1.6484) - 1.6255) / 0.0229 * 0.0020   # plano de la pared vieja
DEEP = 19.0 * MM                                                 # pared de atras de la faringe (desde el plano del marco)
REC = 8.0 * MM                                                   # el velo es un techo que baja hacia atras: su borde queda REC detras del paladar
UV_BOX = (0.107, 0.223, 0.707, 0.843)                            # u0, u1, v0, v1 en el atlas (libre)
UVULA_BOX = (0.004, 0.026, 0.705, 0.840)                         # columna libre a la izquierda del atlas

def arch_y(x):
    a = np.clip(np.abs(x) / PIL, 0, 1)
    return Y_ARCH_C - (Y_ARCH_C - Y_ARCH_P) * a ** 1.5

def classes():
    """por vertice de la grilla (NR, NC): ap = adentro del arco (pared de atras), k = distancia en pasos al borde del arco
    (0 en el borde), rim = 1 en el borde (velo/pilares)"""
    ci = np.arange(NC)[None, :].repeat(NR, 0); ri = np.arange(NR)[:, None].repeat(NC, 1)
    inner = np.abs(XS) < PIL - 1e-6
    ap = inner[None, :] & (ri < NB)
    pc = np.where(np.isclose(np.abs(XS), PIL))[0]                 # columnas de los pilares
    dc = np.min(np.abs(ci[..., None] - pc[None, None, :]), -1)
    dr = NB - ri
    k = np.where(ap, np.minimum(dc, dr), 0).astype(float)
    k[NB - 1, :] = np.minimum(k[NB - 1, :], 0.5)                  # pliegue del arco: fila a 1 mm del borde, media profundidad
    rim = ((np.isclose(np.abs(XS), PIL)[None, :] & (ri <= NB)) | (inner[None, :] & (ri == NB))).astype(float)
    return ap, k, rim

def geometry():
    """P (NR*NC + uvula, 3), UV, F (orden de Fox: horario visto desde adelante), wjaw (0..1, el resto sk_head)"""
    ap, k, rim = classes()
    P = np.zeros((NR, NC, 3)); UV = np.zeros((NR, NC, 2)); wj = np.zeros((NR, NC))
    u0, u1, v0, v1 = UV_BOX
    for c, x in enumerate(XS):
        h = arch_y(x)
        ys = np.concatenate([Y_BOT + (h - Y_BOT) * (np.arange(NB) / (NB - 1)) ** 0.8 * (h - 1.0 * MM - Y_BOT) / (h - Y_BOT), [h],
                             h + (Y_TOP - h) * (np.arange(1, NA + 1) / NA)])
        for r, y in enumerate(ys):
            z = Z_AT(y)
            ax = abs(x) / PIL
            # (02/11 b) el istmo es un arco de puerta: angosto arriba (~25 mm entre pilares a la altura del velo) y ancho abajo
            # (~38 mm donde los pilares llegan a la lengua): las columnas de adentro (|x| < 24 mm) se escalan por fila; XS / uv /
            # classes no cambian (la textura y el rim siguen alineados)
            sx = 0.78 + 0.40 * (1 - min(r, NB) / NB)
            xp = x * (1 + (sx - 1) * np.clip((0.024 - abs(x)) / 0.008, 0, 1))
            inner = np.clip((0.024 - abs(x)) / 0.006, 0, 1)
            if ap[r, c]:
                kk = k[r, c]
                # profundidad continua desde el borde del arco (kk = 1) hasta el fondo (kk >= 3): un tubo redondeado, no una caja;
                # arranca detras del borde del velo (REC) y llega a DEEP
                e = min(kk / 3.0, 1.0); e = e * e * (3 - 2 * e)
                d = REC + (DEEP - REC) * (0.32 + 0.68 * e) + 3.0 * MM * (1 - ax ** 2) * e   # centro mas hondo (tubo)
                d += 7.0 * MM * (1 - r / NB) ** 2 * e                    # abajo sigue hacia la garganta
                z -= d
                if kk == 1 and 2 <= r <= 6: z += 1.8 * MM * np.sin(np.pi * (r - 2) / 4)   # amigdala: abulta entre los pilares
                if kk == 1 and r == NB - 1: y -= 0.4 * MM                 # el velo dobla hacia atras por debajo
                if r == 0: y -= 6.5 * MM * (1 - ax ** 2); z -= 4.0 * MM * (1 - ax ** 2)   # hipofaringe: baja detras de la raiz de la lengua
            else:
                # cortina: abultada hacia adelante en el borde del arco (pliegue del velo y de los pilares), plana en el marco
                edge = 1.0 if (c in (0, NC - 1) or r == NR - 1 or r == 0) else 0.0
                z += 0.9 * MM * rim[r, c] * (1 - edge)
                # (02/11 b) el marco pasa por detras de las tiras de la encia de Konami (z 0.1196..0.1235 en las esquinas de arriba)
                if edge or (abs(x) > 0.018 and r >= NB - 1): z -= 0.8 * MM
                # el velo es un techo que baja hacia atras (REC en el borde, 0 en la union con el paladar duro); los pilares van
                # del frente (abajo) hacia atras (arriba)
                rec = REC * (1 - (r - NB) / NA) if r >= NB else REC * (0.35 + 0.65 * (r / NB) ** 1.5)
                z -= rec * (1 - edge) * inner
            P[r, c] = (xp, y, z)
            UV[r, c] = (u0 + (u1 - u0) * c / (NC - 1), v0 + (v1 - v0) * (1 - r / (NR - 1)))
            t = (y - Y_BOT) / (Y_TOP - Y_BOT)
            # pesos: el marco sigue a las mejillas de Konami (abajo mandibula, arriba cabeza); la pared de atras es de la cabeza
            side = np.clip((abs(x) - 0.024) / 0.0097, 0, 1)
            wj[r, c] = np.clip(1 - t, 0, 1) ** 1.3 * (0.25 + 0.75 * side) * (1.0 if not ap[r, c] else 0.5)
            # (02/11 b) las 3 filas de abajo siguen al piso de Konami (mandibula 1.0): al abrir la boca el piso baja ~14 mm y si
            # la pared se quedaba con la cabeza aparecia un agujero negro detras de la raiz de la lengua
            if r < 3: wj[r, c] = max(wj[r, c], 1.0 - r / 3.0)
    idx = np.arange(NR * NC).reshape(NR, NC)
    F = []
    for r in range(NR - 1):
        for c in range(NC - 1):
            a, b, cc, d = idx[r, c], idx[r, c + 1], idx[r + 1, c + 1], idx[r + 1, c]
            # diagonal hacia el centro (simetrica)
            if XS[c] + XS[c + 1] < 0: F += [[a, b, d], [b, cc, d]]
            else: F += [[a, b, cc], [a, cc, d]]
    F = np.array(F)
    P = P.reshape(-1, 3); UV = UV.reshape(-1, 2); wj = wj.reshape(-1)
    # ---- uvula
    na, nr = 8, 7
    top = np.array([0.0, Y_ARCH_C + 0.6 * MM, Z_AT(Y_ARCH_C) - REC - 0.6 * MM])   # cuelga del borde del velo (retrasado REC)
    L = 9.5 * MM                                                  # (02/11 b) 6.2 -> 9.5 mm, base 5.2 mm: uvula adulta; la punta queda sobre el dorso de la lengua abierta
    Pu, UVu = [], []
    uu0, uu1, uv0, uv1 = UVULA_BOX
    for i in range(nr):
        s = i / (nr - 1)                                              # 0 arriba, 1 abajo
        rad = (2.6 - 0.9 * s + 0.45 * np.sin(np.pi * s) ** 2) * MM * (1 - 0.5 * s ** 6)
        c = top + np.array([0, -L * s, -2.5 * MM * s ** 1.5])          # cuelga y se va un poco hacia atras
        for j in range(na):
            th = 2 * np.pi * j / na
            Pu.append(c + rad * np.array([np.sin(th), 0, np.cos(th)]))
            UVu.append((uu0 + (uu1 - uu0) * (0.5 + 0.45 * np.sin(th)), uv0 + (uv1 - uv0) * s))
    tip = top + np.array([0, -L - 1.3 * MM, -2.5 * MM]); Pu.append(tip); UVu.append((0.5 * (uu0 + uu1), uv1))
    Pu = np.array(Pu); UVu = np.array(UVu)
    Fu = []
    base = len(P)
    for i in range(nr - 1):
        for j in range(na):
            a, b = base + i * na + j, base + i * na + (j + 1) % na
            c, d = base + (i + 1) * na + (j + 1) % na, base + (i + 1) * na + j
            Fu += [[a, b, c], [a, c, d]]
    ti = base + len(Pu) - 1
    for j in range(na): Fu.append([base + (nr - 1) * na + j, base + (nr - 1) * na + (j + 1) % na, ti])
    Fu = np.array(Fu)
    # orientacion: la grilla mira hacia adelante (+z); la uvula hacia afuera de su eje
    n = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    if n[:, 2].mean() < 0: F = F[:, ::-1]
    Pall = np.vstack([P, Pu])
    nu = np.cross(Pall[Fu[:, 1]] - Pall[Fu[:, 0]], Pall[Fu[:, 2]] - Pall[Fu[:, 0]])
    axis = Pall[Fu].mean(1) - np.array([0, 1, 0]) * 0 - top
    axis[:, 1] = 0
    if (nu * axis).sum(1).mean() < 0: Fu = Fu[:, ::-1]
    Fall = np.vstack([F, Fu])
    wall = np.concatenate([wj, np.zeros(len(Pu))])
    # Fox / Konami: horario visto desde afuera
    return Pall, np.vstack([UV, UVu]), Fall[:, ::-1], wall

def back_wall_faces(F, P):
    """caras de la pared plana de Konami (las 2 del rectangulo de atras, z < 0.1205, de punta a punta)"""
    c = P[F]
    return (c[..., 2].max(1) < 0.1205) & (np.abs(c[..., 0]).min(1) > 0.033)

# ---------------------------------------------------------------- textura (la usa make_oral.py)
def _sst(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def fields(n_u, n_v):
    """campos sobre el rectangulo UV_BOX a n_u x n_v pixeles (fila 0 = v0 = arriba de la grilla): ap (0..1), rim, depth (m),
    yrel (0 abajo, 1 arriba), xrel (-1..1)"""
    from scipy import ndimage as ndi
    ap, k, rim = classes()
    Pg, _, _, _ = geometry()
    P = Pg[:NR * NC].reshape(NR, NC, 3)
    depth = Z_AT(P[..., 1]) - P[..., 2]
    def up(a):
        a = a[::-1]                                                  # fila 0 de la imagen = arriba (v0)
        yy = np.linspace(0, NR - 1, n_v); xx = np.linspace(0, NC - 1, n_u)
        Y, X = np.meshgrid(yy, xx, indexing='ij')
        return ndi.map_coordinates(a.astype(float), [Y, X], order=1, mode='nearest')
    return up(ap.astype(float)), up(rim), up(depth), up(np.linspace(0, 1, NR)[:, None].repeat(NC, 1)), up(XS[None, :].repeat(NR, 0) / XS.max())

def paint(gum_rgb, n_u, n_v, seed=71, edge_rgb=None):
    """color (n_v, n_u, 3), alto para el normal (n_v, n_u), ap, depth y variacion de rugosidad del rectangulo de la garganta.
    gum_rgb: color de la encia del atlas (la mucosa del velo sale de ahi para que combine); edge_rgb: color del atlas alrededor
    del rectangulo (el marco se funde a eso en los bordes).
    (02/11 b) revision: ruido isotropico en mm (el rectangulo tiene ~3.5 px/mm a lo ancho y ~10 px/mm a lo alto), velo mas
    oscuro y rojo que la encia con la luz centrada en el borde del arco (AO cromatica), pared de la faringe 'otro' para el
    shader desde el pliegue del arco (oscura, acromatica) con foliculos grandes, rafe medio, amigdalas con criptas y pilar
    posterior; capilares verticales en el velo."""
    from scipy import ndimage as ndi
    ap, rim, depth, yrel, xrel = fields(n_u, n_v)
    def nz(sig, s, an=(2.0, 0.7)):
        r = np.random.default_rng(s).standard_normal((n_v, n_u)); g = ndi.gaussian_filter(r, (sig * an[0], sig * an[1]), mode='reflect')
        return g / (g.std() + 1e-6)
    gum = np.asarray(gum_rgb, float)
    velum = gum * np.array([0.86, 0.62, 0.66])                       # velo/pilares: mucosa mas roja y oscura que la encia
    wall = gum * np.array([0.95, 0.56, 0.60])                        # pared de la faringe: rojo saturado (la AO la oscurece)
    col = velum * (1 - ap[..., None]) + wall * ap[..., None]
    col = col * (1 + 0.10 * rim[..., None])                          # el borde del arco agarra luz
    # el velo se enrojece hacia el borde del arco (cerca de la union con el paladar duro es mas palido)
    col = col * (1 + (1 - ap)[..., None] * _sst(1.0, 0.6, yrel)[..., None] * np.array([0.0, -0.08, -0.06]))
    # capilares del velo: finos, corren del paladar al borde (verticales), mas en los pilares
    v1 = np.clip((1 - np.abs(nz(1.0, seed + 1, (8.0, 1.8))) - 0.84) / 0.16, 0, 1)
    v2 = np.clip((1 - np.abs(nz(1.0, seed + 5, (3.4, 1.2))) - 0.90) / 0.10, 0, 1)
    vess = np.clip(v1 + 0.6 * v2, 0, 1) * (1 - ap) * (0.6 + 0.4 * np.abs(xrel))
    col = col * (1 - 0.45 * vess[..., None]) + (gum * np.array([0.72, 0.30, 0.34])) * (0.45 * vess[..., None])
    # pared de atras: foliculos linfoides (granitos claros de ~1 mm), rafe medio, pilar posterior (banda clara) y amigdalas
    foll = np.clip(nz(1.0, seed + 2, (10.0, 3.5)) - 1.6, 0, None) * ap
    col = col * (1 + 0.28 * foll[..., None])
    col = col * (1 - 0.06 * np.exp(-(xrel / 0.06) ** 2) * ap)[..., None]
    band = ap * np.clip(1 - np.abs((np.abs(xrel) - 0.33) / 0.05), 0, 1) * (yrel < 0.9)
    col = col * (1 + 0.15 * band[..., None])
    tonsil = np.exp(-((np.abs(xrel) - 0.40) / 0.07) ** 2 - ((yrel - 0.42) / 0.16) ** 2) * ap
    crypt = np.clip(nz(1.0, seed + 6, (3.0, 1.0)) - 1.6, 0, None) * tonsil
    col = col * (1 + 0.12 * tonsil[..., None])
    col = col * (1 - 0.25 * crypt[..., None]) + (gum * np.array([0.80, 0.72, 0.72])) * (0.25 * crypt[..., None])
    col = col * (1 + 0.04 * nz(0.8, seed + 3))[..., None]
    # oscuridad: en el marco (velo/pilares) la luz esta en el borde del arco y se apaga hacia el paladar y las mejillas, CROMATICA
    # (el rojo se mantiene: sigue siendo tejido para el shader); en la pared (ap) ACROMATICA y fuerte desde el pliegue del arco,
    # asi toda la pared queda 'otro' (sin banda de cambio de clase en el medio) y los dientes se recortan contra el fondo
    ao_m = 0.38 + 0.62 * np.exp(-((yrel - NB / (NR - 1.0)) / 0.22) ** 2) * (1 - 0.45 * _sst(0.55, 1.0, np.abs(xrel)))
    ao_w = np.clip(0.10 + 0.30 * np.exp(-(depth - 11.0 * MM) / (7.0 * MM)), 0, 0.40)   # a 0.40 queda R-G ~0.09: 'otro' desde el pliegue
    down = _sst(0.55, 0.0, yrel) * ap * (1 - 0.5 * np.abs(xrel))
    ao_w = ao_w * (1 - 0.6 * down)
    aps = _sst(0.35, 0.65, ap)                                       # transicion velo -> pared en ~3 px (no en toda la celda)
    fac = (1 - aps)[..., None] * np.stack([ao_m ** 0.6, ao_m, ao_m], -1) + aps[..., None] * ao_w[..., None]
    col = col * fac
    if edge_rgb is not None:                                         # los bordes del marco se funden al atlas de alrededor
        f = np.maximum(np.maximum(_sst(0.94, 1.0, yrel), _sst(0.94, 1.0, np.abs(xrel))), _sst(0.06, 0.0, yrel) * (1 - ap)) * 0.6
        col = col * (1 - f[..., None]) + np.asarray(edge_rgb, float) * f[..., None]
    h = (0.55 * rim - 0.30 * np.exp(-(xrel / 0.06) ** 2) * (1 - ap) * _sst(0.60, 0.75, yrel) + 0.15 * vess
         + 0.25 * nz(1.0, seed + 4, (6.0, 2.0)) + 0.8 * foll + 0.6 * tonsil + 0.3 * band)
    rvar = 0.03 * nz(1.0, seed + 7, (4.0, 1.4))
    return np.clip(col, 0, 1), h, ap, depth, rvar

def paint_uvula(gum_rgb, n_u, n_v, seed=73):
    from scipy import ndimage as ndi
    gum = np.asarray(gum_rgb, float)
    def nz(an, s):
        r = np.random.default_rng(s).standard_normal((n_v, n_u)); g = ndi.gaussian_filter(r, an, mode='reflect'); return g / (g.std() + 1e-6)
    g = nz((9.0, 1.0), seed)
    v2 = np.clip((1 - np.abs(nz((14.0, 1.2), seed + 1)) - 0.90) / 0.10, 0, 1)
    s = np.linspace(0, 1, n_v)[:, None]
    col = gum[None, None, :] * np.array([0.86, 0.62, 0.66]) * (1 + 0.04 * g)[..., None] * (1 - 0.08 * s)[..., None]
    col = col * (1 + 0.10 * _sst(0.85, 1.0, s)[..., None] * np.array([0.0, 0.12, 0.10]))   # punta apenas mas palida
    col = col * (1 - 0.35 * v2[..., None]) + (gum * np.array([0.72, 0.30, 0.34])) * (0.35 * v2[..., None])
    return np.clip(col, 0, 1), 0.05 * g + 0.1 * v2, np.zeros((n_v, n_u)), np.zeros((n_v, n_u)), 0.0 * g
