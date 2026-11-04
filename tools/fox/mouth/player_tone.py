# PES21 FX (04/11) - tono de dientes propio por jugador (Agustin: Messi).
# La DLL enlaza pes_oral_* a toda boca (regla 63fab773). La boca de un jugador con tono lleva un triangulo degenerado de mas
# (cantidad de indices unica, MARKER) y la regla, solo en ese dibujo, enlaza pes_oral_<perfil>_bsm.dds = la textura comun
# con el tono del jugador en los dientes (srv = 0 ... indices=N; la comun lleva no_indices=N). Nadie mas cambia.
# (Descartado: copiar los dientes a una "zona libre" del atlas; la boca plana de Konami de 28 caras usa esa zona.)
import numpy as np

TONES = {'messi': (1.0, 0.985, 0.95)}   # marfil apenas mas calido (fotos de Agustin)
MARKER = {'messi': 64995}               # indices del dibujo de su boca (21664 triangulos comunes + 1 degenerado)

def sst(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def tone_bsm(bsm, profile, log=print):
    """bsm comun (float, h x w x 4) -> con el tono del jugador solo en lo que el shader toma como diente (gris/marfil claro)"""
    c = bsm[..., :3]; mx = c.max(-1); sat = (mx - c.min(-1)) / np.maximum(mx, 1e-6); rg = c[..., 0] - c[..., 1]
    m = sst(0.25, 0.40, mx) * sst(0.40, 0.28, sat) * sst(0.16, 0.10, rg)
    out = bsm.copy()
    out[..., :3] = np.clip(c * (1 + m[..., None] * (np.array(TONES[profile]) - 1)), 0, 1)
    log('  tono %s: %.1f%% de la textura con tono (dientes)' % (profile, 100 * (m > 0.5).mean()))
    return out

def mark_faces(faces, profile):
    """agrega el triangulo degenerado que marca la boca del jugador para la regla de la DLL"""
    f = np.vstack([faces, faces[-1:, [0, 0, 0]]])
    assert len(f) * 3 == MARKER[profile], (len(f) * 3, MARKER[profile])
    return f
