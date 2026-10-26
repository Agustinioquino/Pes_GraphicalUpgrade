# PES21 FX (26/10 b) - zonas de la piel del torso de TorsoBody para el PS de la piel (que no parezca arcilla):
#   pes_var_torso_skin_a.dds  BC5 (en 0..1): x = brillo (intensidad especular), y = rugosidad (la r del shader, antes del polinomio)
#   pes_var_torso_skin_b.dds  BC5: x = translucidez (la del g-buffer, como trm.y), y = rubor (0.5 = nada; mas = algo mas rojo)
# torsob_srm / torsob_trm de TorsoBody son parejos (0.896 / 0.698 / 0.143 en todo el torso: piel de arcilla, y la rugosidad
# queda clavada en el tope 0.5608 del shader). Aca: piel mas satinada con zonas grasas (esternon, entre los omoplatos, arriba
# de los hombros, espalda baja) mas brillantes y lisas, costados y abdomen algo mas mates; translucidez mayor donde hay carne
# (costados, rollos, pezones) y menor sobre hueso (esternon, claviculas, columna, espina del omoplato, cresta iliaca). Cerca
# de las costuras (brazos, cuello, short) los valores vuelven a los del resto del cuerpo (DEF), que el PS usa tambien en los
# brazos y las piernas. Los poros y las lineas finas los pone el PS en todo el cuerpo (del normal map), no esta textura.
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from torso_variants import ss, g, NIPPLE, NAVEL
from torso_maps import _frame, Noise

DEF = dict(spec=0.85, rough=0.45, trans=0.30, red=0.5)   # = valores por defecto del PS (aa7d1833), tambien en brazos y piernas
_N = Noise(71, freqs=(6.0, 13.0), n=6)

def zones(P):
    x, y, z, rz, front, back, side = _frame(P)
    oily = np.zeros(x.shape)
    oily = np.maximum(oily, g(x / 0.035) * ss(1.27, 1.32, y) * (1 - ss(1.46, 1.52, y)) * front)        # esternon / pecho alto
    oily = np.maximum(oily, g(x / 0.060) * ss(1.33, 1.38, y) * (1 - ss(1.52, 1.56, y)) * back)         # entre los omoplatos
    oily = np.maximum(oily, ss(1.47, 1.51, y) * g((x - 0.12) / 0.07))                                   # arriba de los hombros
    oily = np.maximum(oily, 0.7 * g(x / 0.05) * ss(1.04, 1.08, y) * (1 - ss(1.20, 1.25, y)) * back)    # espalda baja
    matte = side * ss(1.02, 1.08, y) * (1 - ss(1.30, 1.36, y)) + 0.5 * front * ss(1.06, 1.10, y) * (1 - ss(1.25, 1.28, y))
    nip = g(np.hypot(x - NIPPLE[0], y - NIPPLE[1]) / 0.012) * front
    nav = g(np.hypot(x, y - NAVEL[1]) / 0.010) * front
    bone = np.zeros(x.shape)
    bone = np.maximum(bone, g(x / 0.020) * ss(1.27, 1.30, y) * (1 - ss(1.45, 1.48, y)) * front)        # esternon
    bone = np.maximum(bone, g((y - (1.505 - 0.10 * (x - 0.03))) / 0.010) * ss(0.02, 0.04, x) * (1 - ss(0.15, 0.18, x)) * front)  # claviculas
    bone = np.maximum(bone, g(x / 0.012) * back)                                                        # columna
    bone = np.maximum(bone, 0.7 * g((y - (1.445 + 0.27 * (x - 0.075))) / 0.010) * ss(0.07, 0.09, x) * (1 - ss(0.17, 0.19, x)) * back)   # espina del omoplato
    flesh = side * ss(1.00, 1.04, y) * (1 - ss(1.24, 1.30, y)) + nip
    nz = _N(P)
    spec = DEF["spec"] * (1 + 0.15 * oily - 0.10 * matte - 0.20 * nip - 0.10 * nav) * (1 + 0.05 * nz)
    rough = DEF["rough"] - 0.09 * oily + 0.05 * matte + 0.06 * nip + 0.04 * nav + 0.025 * nz
    trans = DEF["trans"] + 0.08 * flesh + 0.10 * nip - 0.09 * bone + 0.02 * nz
    # rubor suave: pecho alto y centro (vasos cerca de la piel), arriba de los hombros (sol), espalda alta; algo de variacion
    flush = np.maximum(g(x / 0.06) * ss(1.32, 1.38, y) * (1 - ss(1.48, 1.53, y)) * front, 0.8 * ss(1.46, 1.51, y) * g((x - 0.12) / 0.07))
    flush = np.maximum(flush, 0.5 * g(x / 0.08) * ss(1.36, 1.42, y) * back)
    red = DEF["red"] + 0.20 * flush + 0.06 * _N(P * 1.7)
    return np.stack([spec, rough, trans, red], -1)

def build_skin(G):
    """G = uvbake (2048 x 1024, v 0..0.5) con extra[..., 0] = desvanecido de las costuras. Devuelve (H, W, 3) en DEF afuera"""
    H, W = G["mask"].shape
    base = np.array([DEF["spec"], DEF["rough"], DEF["trans"], DEF["red"]])
    out = np.zeros((H, W, 4)); out[...] = base
    ii = np.where(G["mask"])
    z = zones(G["P"][ii])
    f = G["extra"][ii][:, 0:1] if "extra" in G else G["fade"][ii][:, None]
    out[ii] = base + (z - base) * f
    return np.clip(out, 0.0, 1.0)
