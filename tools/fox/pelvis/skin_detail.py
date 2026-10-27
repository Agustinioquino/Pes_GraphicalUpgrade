# PES21 FX (26/10 c) - detalle de piel REAL para la pelvis "sin short", sacado de la piel del torso de TorsoBody (la misma de todo el
# cuerpo): se arma una textura que se repite sin costura (1024 x 1024 = 22.5 cm de piel a la escala del torso, 4550 texeles por
# metro) con parches al azar de zonas lisas del torso (sin la columna, la linea alba, pezones, ombligo, axilas ni el borde de los
# musculos), mezclados conservando la varianza (Heitz y Neyret: (w1 a + w2 b) / sqrt(w1^2 + w2^2)), asi las juntas no se ven:
#   pes_var_pelvis_detail_n.dds   BC5 SNORM: el normal map del torso menos su desenfoque de 0.7 mm (poros y lineas finas)
#   pes_var_pelvis_detail_a.dds   BC4 UNORM: albedo / su desenfoque de 5 mm, dividido 2 (0.5 = sin cambio): manchitas y poros
# El PS de la piel la lee en la pelvis a la misma escala fisica (dos capas cruzadas, ver aa7d1833: pel).
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', 'torso'))

def build(common, texdir, outdir, size=1024, patch=128, step=80, seed=7):
    from scipy.ndimage import gaussian_filter, uniform_filter, minimum_filter
    from texio import load_ftex, write_dds_bc5_snorm, write_dds_bc4_unorm
    from uvbake import bake
    from torso_mesh import load_torso
    from torso_variants import NIPPLE, NAVEL
    nm = load_ftex(os.path.join(texdir, "torsob_nrm.ftex"), mip=0)        # 4096
    al = load_ftex(os.path.join(texdir, "torso_3_bsm.ftex"), mip=0)[..., :3]
    H, W = nm.shape[:2]
    Hs = int(0.47 * H)                                                   # el torso ocupa v 0..0.47
    xy = (nm[:Hs, :, :2].astype(np.float32) * 2 - 1)
    lum = al[:Hs].astype(np.float32).mean(-1)
    micro = xy - np.stack([gaussian_filter(xy[..., k], 3.0) for k in range(2)], -1)
    band = np.stack([gaussian_filter(xy[..., k], 3.0) - gaussian_filter(xy[..., k], 12.0) for k in range(2)], -1)
    bandE = gaussian_filter(np.hypot(band[..., 0], band[..., 1]), 6.0)
    ratio = lum / np.maximum(gaussian_filter(lum, 24.0), 1e-3)
    # zonas buenas: dentro del torso (por la malla), lejos de la linea media, pezones, ombligo, axilas, cuello y del borde de abajo
    T = load_torso(common, "out")
    G = bake(T, None, W=W, H=H // 2, vmax=0.5)                         # mismas filas que la textura (v 0..0.5)
    m = G["mask"][:Hs]; P = G["P"][:Hs]
    x, y = np.abs(P[..., 0]), P[..., 1]
    good = m & (x > 0.035) & (y > 1.03) & (y < 1.47)
    good &= np.hypot(x - NIPPLE[0], y - NIPPLE[1]) > 0.035
    good &= np.hypot(x - NAVEL[0], y - NAVEL[1]) > 0.035
    good &= ~((x > 0.13) & (y > 1.30))                                  # axilas / hombros
    good &= bandE < np.percentile(bandE[good], 55)                      # sin bordes de musculos marcados
    ok = minimum_filter(good.astype(np.uint8), size=patch) > 0          # parche entero adentro
    cy, cx = np.where(ok)
    print("zonas buenas: %.1f %% del torso, %d centros posibles" % (100 * good.sum() / max(m.sum(), 1), len(cy)))
    rng = np.random.default_rng(seed)
    accN = np.zeros((size, size, 2)); accA = np.zeros((size, size)); w2 = np.zeros((size, size))
    t = np.linspace(0, 1, patch)
    win1 = np.sin(np.pi * t) ** 2                                        # coseno levantado (se superponen de a varios)
    win = np.outer(win1, win1)
    hp = patch // 2
    for oy in range(0, size, step):
        for ox in range(0, size, step):
            k = rng.integers(len(cy)); sy, sx = cy[k] - hp, cx[k] - hp
            pn = micro[sy: sy + patch, sx: sx + patch]
            pa = np.log(np.clip(ratio[sy: sy + patch, sx: sx + patch], 0.5, 2.0))
            if rng.random() < 0.5: pn = pn[:, ::-1] * np.array([-1, 1]); pa = pa[:, ::-1]      # espejo (con el signo de x)
            if rng.random() < 0.5: pn = pn[::-1, :] * np.array([1, -1]); pa = pa[::-1, :]
            yy = (np.arange(patch) + oy + rng.integers(-8, 9)) % size
            xx = (np.arange(patch) + ox + rng.integers(-8, 9)) % size
            ix = np.ix_(yy, xx)
            accN[ix] += pn * win[..., None]; accA[ix] += pa * win; w2[ix] += win ** 2
    d_n = accN / np.sqrt(np.maximum(w2, 1e-9))[..., None]
    d_a = accA / np.sqrt(np.maximum(w2, 1e-9))
    # la mezcla conserva la varianza; se lleva al desvio del torso (por si acaso) y sin media
    d_n -= d_n.mean((0, 1)); d_a -= d_a.mean()
    s_src = micro[good].std(0); d_n *= (s_src / np.maximum(d_n.reshape(-1, 2).std(0), 1e-9))
    d_a *= np.log(np.clip(ratio[good], 0.5, 2.0)).std() / max(d_a.std(), 1e-9)
    alb = np.clip(0.5 * np.exp(d_a), 0, 1)
    def mipchain(a):
        mips = [a.astype(np.float32)]
        while min(mips[-1].shape[:2]) > 1:
            c = mips[-1]; mips.append(0.25 * (c[0::2, 0::2] + c[1::2, 0::2] + c[0::2, 1::2] + c[1::2, 1::2]))
        return mips
    write_dds_bc5_snorm(os.path.join(outdir, "pes_var_pelvis_detail_n.dds"), mipchain(np.clip(d_n, -0.9, 0.9)))
    write_dds_bc4_unorm(os.path.join(outdir, "pes_var_pelvis_detail_a.dds"), mipchain(alb))
    return dict(nstd=d_n.reshape(-1, 2).std(0), src_nstd=s_src, astd=float(np.std(2 * alb)), d_n=d_n, alb=alb)

if __name__ == "__main__":
    r = build(sys.argv[1], sys.argv[2], sys.argv[3])
    print("detalle: normal std %s (torso %s), albedo x std %.4f" % (np.round(r["nstd"], 4), np.round(r["src_nstd"], 4), r["astd"]))
