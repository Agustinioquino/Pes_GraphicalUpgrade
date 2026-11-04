# PES21 FX (04/11) - tono de dientes propio por jugador (Agustin: Messi).
# La DLL enlaza pes_oral_* a toda boca (regla 63fab773). La boca de un jugador con tono lleva un triangulo degenerado de mas
# (cantidad de indices unica, MARKER) y la regla, solo en ese dibujo, enlaza pes_oral_<perfil>_bsm.dds = la textura comun
# con el tono del jugador en los dientes (srv = 0 ... indices=N; la comun lleva no_indices=N). Nadie mas cambia.
# (Descartado: copiar los dientes a una "zona libre" del atlas; la boca plana de Konami de 28 caras usa esa zona.)
import numpy as np

TONES = {'messi': (1.04, 1.04, 1.035)}
# (04/11 e) Agustin: 'no me convence ... los dientes de arriba, el color'. Desde la v25 la comun tiene el marfil de Konami
# (L 0.60); Messi lleva carillas: blanco apenas calido y mas claro. (luminancia mediana, color objetivo); reemplaza TONES/LO_WHITE
WHITE = {'messi': (0.74, (1.0, 0.975, 0.935))}
LUMW = np.array([0.30, 0.59, 0.11], np.float32)   # (04/11 c) mas blancos que la comun (carillas; antes marfil calido 1/.985/.95)
# (04/11) dientes de abajo (fila de abajo del atlas, v 0.36-0.66): mas blancos ('un tono un poco mas blanco para los de abajo')
LO_WHITE = {'messi': 0.0}   # (04/11 e) con WHITE todos los dientes de Messi van parejos   # 0 = como arriba; 1 = gris claro sin tinte
MARKER = {'messi': 64995}               # indices del dibujo de su boca (21664 triangulos comunes + 1 degenerado)

def sst(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def tone_bsm(bsm, profile, log=print):
    """bsm comun (float, h x w x 4) -> con el tono del jugador solo en lo que el shader toma como diente (gris/marfil claro)"""
    c = bsm[..., :3]; mx = c.max(-1); sat = (mx - c.min(-1)) / np.maximum(mx, 1e-6); rg = c[..., 0] - c[..., 1]
    m = sst(0.25, 0.40, mx) * sst(0.40, 0.28, sat) * sst(0.16, 0.10, rg)
    out = bsm.copy()
    t = c * (1 + m[..., None] * (np.array(TONES[profile]) - 1))
    tw = WHITE.get(profile)
    if tw:   # (04/11 e) tono propio: luminancia mediana y color objetivo, detalle propio conservado
        L = (c * LUMW).sum(-1); k = m > 0.5
        med = float(np.median(L[k])) if k.any() else 0.6
        th = np.array(tw[1], np.float32); th = th / (th * LUMW).sum()
        own = c / np.maximum(L, 1e-4)[..., None]
        tt = np.clip((own * 0.3 + th * 0.7) * (L * (tw[0] / med))[..., None], 0, 1)
        t = c * (1 - m[..., None]) + tt * m[..., None]
        lt = (t * LUMW).sum(-1)
        log('  tono %s: dientes L %.3f -> %.3f %s' % (profile, med, float(np.median(lt[k])), np.median(t[k], 0).round(3).tolist()))
    w = LO_WHITE.get(profile, 0.0)
    if w:
        H = bsm.shape[0]; v = (np.arange(H) + 0.5) / H
        lo = (sst(0.36, 0.38, v) * sst(0.66, 0.64, v))[:, None] * m * w
        lum = t.mean(-1, keepdims=True); white = np.clip(lum * 1.06, 0, 1)   # gris claro, apenas calido abajo
        t = t * (1 - lo[..., None]) + np.clip(0.5 * t + 0.5 * white * np.array([1.0, 0.995, 0.98]), 0, 1) * lo[..., None]
    out[..., :3] = np.clip(t, 0, 1)
    log('  tono %s: %.1f%% de la textura con tono (dientes)' % (profile, 100 * (m > 0.5).mean()))
    return out

def mark_faces(faces, profile):
    """agrega el triangulo degenerado que marca la boca del jugador para la regla de la DLL"""
    f = np.vstack([faces, faces[-1:, [0, 0, 0]]])
    assert len(f) * 3 == MARKER[profile], (len(f) * 3, MARKER[profile])
    return f
