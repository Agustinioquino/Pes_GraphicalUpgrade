# PES21 FX (26/10 b) - zonas de la piel del torso de TorsoBody para el PS de la piel (que no parezca arcilla):
#   pes_var_torso_skin_a.dds  BC5 (en 0..1): x = brillo (intensidad especular), y = rugosidad (la r del shader, antes del polinomio)
#   pes_var_torso_skin_b.dds  BC5: x = translucidez (la del g-buffer, como trm.y), y = rubor (0.5 = nada; mas = algo mas rojo)
# torsob_srm / torsob_trm de TorsoBody son parejos (0.896 / 0.698 / 0.143 en todo el torso: piel de arcilla). (26/10 d) La
# rugosidad NO queda en 0.5608: el shader hace max(min(0.5608, r), r con suciedad) = r sin suciedad; 0.5608 es el piso cuando el
# jugador se ensucia. Aca: piel satinada con zonas grasas (esternon, entre los omoplatos, arriba
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

# = valores por defecto del PS (aa7d1833), tambien en brazos y piernas. (26/10 d) rugosidad 0.45 -> 0.66 (TorsoBody: 0.70) y brillo
# 0.85 -> 0.80: con 0.45 el sol y los reflejos del cielo daban piel mojada en todo el cuerpo. Lo que saca lo "arcilla" son las zonas,
# los poros (el PS les suma rugosidad) y la translucidez, no una piel lisa
DEF = dict(spec=0.80, rough=0.66, trans=0.30, red=0.5)
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
    spec = DEF["spec"] * (1 + 0.12 * oily - 0.10 * matte - 0.20 * nip - 0.10 * nav) * (1 + 0.05 * nz)
    rough = DEF["rough"] - 0.06 * oily + 0.04 * matte + 0.04 * nip + 0.03 * nav + 0.02 * nz
    trans = DEF["trans"] + 0.08 * flesh + 0.10 * nip - 0.09 * bone + 0.02 * nz
    # rubor suave: pecho alto y centro (vasos cerca de la piel), arriba de los hombros (sol), espalda alta; algo de variacion
    flush = np.maximum(g(x / 0.06) * ss(1.32, 1.38, y) * (1 - ss(1.48, 1.53, y)) * front, 0.8 * ss(1.46, 1.51, y) * g((x - 0.12) / 0.07))
    flush = np.maximum(flush, 0.5 * g(x / 0.08) * ss(1.36, 1.42, y) * back)
    red = DEF["red"] + 0.20 * flush + 0.06 * _N(P * 1.7)
    out = np.stack([spec, rough, trans, red], -1)
    # (26/10 e) el pezon y la areola: los valores de base (sin menos brillo ni mas translucidez: se veia de otro color)
    from torso_variants import nipple_mask
    nm = nipple_mask(P)[:, None]
    base = np.array([DEF["spec"], DEF["rough"], DEF["trans"], DEF["red"]])
    return base + (out - base) * (1.0 - nm)

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


# ---------------------------------------------------------------- (26/10 c) brazos: pes_var_arm_skin_a/b.dds (todo el atlas del brazo,
# 2048 x 2048; el PS los reconoce por ser cuadrados). arm_srm de TorsoBody tiene solo una mancha en el codo; con la piel real el PS
# usaba DEF en todo el brazo. Codo (atras) mas aspero, opaco y algo mas rojo; adentro del brazo y la muneca mas fina (mas translucida,
# mas palida y lisa); el antebrazo de afuera (el que da al sol) un poco mas aspero; arriba del hombro algo mas graso.
_NA = Noise(83, freqs=(7.0, 15.0), n=6)

def arm_zones(P):
    from torso_variants import arm_frame
    t, th, r = arm_frame(P)
    def win(a, b, c, d, x): return ss(a, b, x) * (1 - ss(c, d, x))
    def ang(th0, w): return g(((th - th0 + 180.0) % 360.0 - 180.0) / w)
    elbow = g((t - 0.185) / 0.016) * ang(95, 38)
    crease = g((t - 0.170) / 0.010) * ang(-90, 35)
    inner = win(0.02, 0.05, 0.15, 0.18, t) * ang(0, 35)
    wrist = win(0.33, 0.36, 0.41, 0.43, t) * ang(-45, 40)
    outer = win(0.19, 0.23, 0.38, 0.41, t) * ang(160, 45)
    oily = (1 - ss(0.04, 0.08, t)) * ang(170, 40)
    nz = _NA(P)
    spec = DEF["spec"] * (1 - 0.20 * elbow + 0.05 * inner - 0.05 * outer + 0.08 * oily) * (1 + 0.05 * nz)
    rough = DEF["rough"] + 0.05 * elbow + 0.03 * crease - 0.03 * inner - 0.02 * wrist + 0.03 * outer - 0.03 * oily + 0.02 * nz
    trans = DEF["trans"] - 0.08 * elbow + 0.04 * crease + 0.08 * inner + 0.08 * wrist + 0.02 * nz
    red = DEF["red"] + 0.18 * elbow + 0.05 * crease - 0.05 * inner + 0.04 * outer + 0.05 * _NA(P * 1.7)
    return np.stack([spec, rough, trans, red], -1)

def build_arm_skin(G):
    """G = uvbake del brazo (2048 x 2048, todo el atlas) con extra[..., 0] = desvanecido de las costuras (hombro, muneca)"""
    H, W = G["mask"].shape
    base = np.array([DEF["spec"], DEF["rough"], DEF["trans"], DEF["red"]])
    out = np.zeros((H, W, 4)); out[...] = base
    ii = np.where(G["mask"])
    z = arm_zones(G["P"][ii])
    f = G["extra"][ii][:, 0:1] if "extra" in G else G["fade"][ii][:, None]
    out[ii] = base + (z - base) * f
    return np.clip(out, 0.0, 1.0)
