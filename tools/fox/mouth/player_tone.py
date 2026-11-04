# PES21 FX (04/11) - tono de dientes propio por jugador (Agustin: Messi; eligio "zona libre de la textura").
# La DLL enlaza las mismas pes_oral_* a toda boca (regla 63fab773), asi que una textura propia en la cara no se veria. En
# cambio: en una zona del atlas que NINGUNA boca usa (u 0.21-0.78, v 0.29-0.50, medido rasterizando v17c / v19c / Messi) se
# pinta una copia de los dientes de arriba y de abajo con el tono del jugador, y la boca de ese jugador (oral.fmdl) apunta
# ahi. El resto de las texturas queda pixel por pixel igual (solo cambian los mips que tocan la zona).
import numpy as np
from scipy import ndimage as ndi

# (u0, u1, v0, v1): de donde se copia (dientes pintados) -> a donde (zona libre)
UP_SRC, UP_DST = (0.180, 0.828, 0.178, 0.296), (0.222, 0.765, 0.302, 0.420)
LO_SRC, LO_DST = (0.222, 0.776, 0.503, 0.558), (0.222, 0.765, 0.432, 0.487)
FREE = (0.215, 0.772, 0.298, 0.492)   # nada fuera de esto se toca

# tono por perfil: multiplicadores RGB (el shader toma como diente max > 0.45, sat < 0.26, R-G < 0.11: se mantiene)
TONES = {'messi': (1.0, 0.985, 0.95)}   # marfil apenas mas calido (fotos de Agustin)

def remap_uv(uv, upper):
    """uv (n, 2) de un diente de la boca de un jugador con tono -> la copia en la zona libre"""
    s, d = (UP_SRC, UP_DST) if upper else (LO_SRC, LO_DST)
    u = np.clip(uv[:, 0], s[0], s[1]); v = np.clip(uv[:, 1], s[2], s[3])
    return np.stack([d[0] + (u - s[0]) * (d[1] - d[0]) / (s[1] - s[0]), d[2] + (v - s[2]) * (d[3] - d[2]) / (s[3] - s[2])], 1)

def _copy(img, s, d, tone=None):
    H, W = img.shape[:2]
    sx0, sx1, sy0, sy1 = int(s[0] * W), int(np.ceil(s[1] * W)), int(s[2] * H), int(np.ceil(s[3] * H))
    dx0, dx1, dy0, dy1 = int(round(d[0] * W)), int(round(d[1] * W)), int(round(d[2] * H)), int(round(d[3] * H))
    a = img[sy0:sy1, sx0:sx1].astype(np.float64)
    z = ndi.zoom(a, ((dy1 - dy0) / a.shape[0], (dx1 - dx0) / a.shape[1]) + (1,) * (a.ndim - 2), order=1)
    z = z[:dy1 - dy0, :dx1 - dx0]
    if tone is not None: z[..., :3] *= np.array(tone)
    out = img.copy(); out[dy0:dy0 + z.shape[0], dx0:dx0 + z.shape[1]] = np.clip(z, 0, 1)
    return out

def paint(tex, profile, log=print):
    """tex = {'bsm','nrm','srm','trm'} (float 0..1, tamanos distintos): copia los dientes a la zona libre; tono solo en bsm"""
    tone = TONES[profile]
    out = {}
    for k, img in tex.items():
        a = img
        for s, d in ((UP_SRC, UP_DST), (LO_SRC, LO_DST)):
            a = _copy(a, s, d, tone if k == 'bsm' else None)
        out[k] = a
    b = out['bsm']; H, W = b.shape[:2]
    y0, y1, x0, x1 = int(UP_DST[2] * H), int(UP_DST[3] * H), int(UP_DST[0] * W), int(UP_DST[1] * W)
    c = b[y0:y1, x0:x1, :3]; mx = c.max(-1); sat = (mx - c.min(-1)) / np.maximum(mx, 1e-6); rg = c[..., 0] - c[..., 1]
    t = mx > 0.45
    log('  tono %s en la zona libre: diente para el shader %.0f%% de lo claro (sat < 0.26 y R-G < 0.11)' % (
        profile, 100 * ((sat < 0.26) & (rg < 0.11) & t).sum() / max(t.sum(), 1)))
    return out
