# PES21 FX (02/11) - DEFINICION DE LOS GRUPOS MUSCULARES DEL BRAZO (pes_arm_anat_n.dds + pes_arm_anat_c.dds, todo el atlas del brazo)
# Agustin (02/11): "mejor definicion no solo de la vena sino del grupo de musculos, y texturas mas reales". La malla ya trae el
# volumen grande (sculpt_arm en torso_variants.py); esto agrega lo que se ve en un brazo de futbolista bajo el sol y que una malla
# de pocos poligonos no puede: las separaciones entre cabezas y grupos (deltoides en 3 cabezas, biceps en 2, herradura del triceps,
# borde del braquiorradial, grupos flexor / extensor del antebrazo, tendones de la muneca) y la cavidad de esos surcos (para el color: un poco mas oscuro y calido, como la piel que se pliega).
# Se evalua en 3D en el marco propio del brazo (arm_frame: continuo a traves de los cortes de uv) y se lleva al marco tangente de
# cada texel (el del VS). El PS (aa7d1833) lo escala con la definicion del jugador y con el musculo que trabaja (lbDef).
# Uso: python3 arm_anatomy.py <carpeta con arm.fmdl> <carpeta de salida>
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from torso_variants import arm_frame, ss, g
from torso_mesh import load_parts
from uvbake import bake, dilate
from texio import write_dds_bc5_snorm, write_dds_bc4_unorm, write_dds_float4

def _b(t, th, t0, th0, st, sth):
    dth = (th - th0 + 180.0) % 360.0 - 180.0
    return g((t - t0) / st) * g(dth / sth)

def _line(t, th, ta, tha, tb, thb, w, r_mm=45.0):
    """distancia (m) a un segmento en el desarrollo del brazo (t, th -> arco con radio ~4.5 cm); w = ancho (m)"""
    rr = r_mm * 1e-3 * np.pi / 180.0
    x = t; y = ((th - tha + 180.0) % 360.0 - 180.0) * rr
    bx, by = tb - ta, ((thb - tha + 180.0) % 360.0 - 180.0) * rr
    L2 = bx * bx + by * by
    u = np.clip(((x - ta) * bx + y * by) / L2, 0.0, 1.0)
    d = np.hypot(x - (ta + u * bx), y - u * by)
    return g(d / w), u

# surcos (profundidad mm, linea t/th de a hasta b, ancho m): separaciones entre cabezas y grupos
GROOVES = [
    (0.55, 0.000, -150, 0.075, -128, 0.0035),   # deltoides: anterior / medio
    (0.55, 0.000, 150, 0.075, 128, 0.0035),     # deltoides: medio / posterior
    (0.45, 0.070, -128, 0.095, 175, 0.0040),    # la V de la insercion del deltoides (adelante)
    (0.45, 0.070, 128, 0.095, 175, 0.0040),     # (atras)
    (0.35, 0.050, -85, 0.135, -88, 0.0030),     # biceps: cabeza corta / larga (surco central)
    (0.50, 0.040, -130, 0.160, -118, 0.0045),   # borde de afuera del biceps (braquial asoma)
    (0.50, 0.030, -45, 0.150, -40, 0.0045),     # borde de adentro del biceps
    (0.45, 0.020, 112, 0.110, 100, 0.0040),     # triceps: cabeza lateral / larga
    (0.40, 0.110, 140, 0.150, 90, 0.0045),      # herradura del triceps (borde de afuera)
    (0.40, 0.110, 60, 0.150, 90, 0.0045),       # herradura (borde de adentro)
    (0.45, 0.195, -125, 0.330, -105, 0.0045),   # braquiorradial / extensores radiales
    (0.40, 0.200, -70, 0.330, -60, 0.0050),     # braquiorradial / flexores (adelante)
    (0.35, 0.210, -15, 0.340, -25, 0.0050),     # flexores: superficial / cubital
    (0.40, 0.210, 160, 0.340, 140, 0.0045),     # extensores: comun / radiales
    (0.35, 0.220, 110, 0.350, 100, 0.0050),     # extensor cubital / ancóneo
]
# tendones de la muneca (alto mm, th, ancho): se marcan desde t 0.33 hasta la muneca
TENDONS = [(0.30, -55, 0.0022), (0.35, -35, 0.0020), (0.22, -18, 0.0018), (0.20, 130, 0.0024), (0.18, 150, 0.0022)]

def height(P):
    """altura (m) del detalle muscular por punto, + = bulto"""
    t, th, r = arm_frame(P)
    h = np.zeros(len(P))
    for dep, ta, tha, tb, thb, w in GROOVES:
        k, u = _line(t, th, ta, tha, tb, thb, w)
        k, u = _line(t, th, ta, tha, tb, thb, 2.4 * w)   # surco ancho y suave (separacion entre vientres, no un corte)
        h -= 1e-3 * 1.4 * dep * k * ss(0.0, 0.2, u) * (1 - ss(0.8, 1.0, u))
    for hi, th0, w in TENDONS:
        k, _ = _line(t, th, 0.330, th0, 0.425, th0, w)
        h += 1e-3 * hi * k * ss(0.33, 0.37, t)
    # vientres algo mas redondos donde la malla es chata (picos suaves que separan los surcos)
    h += 1e-3 * 0.35 * _b(t, th, 0.092, -100, 0.022, 14) + 1e-3 * 0.35 * _b(t, th, 0.088, -70, 0.022, 14)   # 2 cabezas del biceps
    h += 1e-3 * 0.30 * _b(t, th, 0.035, -160, 0.022, 14) + 1e-3 * 0.30 * _b(t, th, 0.035, 175, 0.022, 14) + 1e-3 * 0.30 * _b(t, th, 0.035, 140, 0.022, 14)
    h += 1e-3 * 0.30 * _b(t, th, 0.240, -138, 0.035, 12)   # braquiorradial
    # fibras: estrias muy finas a lo largo del musculo (deltoides en abanico, biceps y antebrazo a lo largo); 0.03 mm, cada ~2.2 mm
    # (sin estrias de fibras: a la distancia de juego hacian muare)
    keep = ss(-0.005, 0.02, t) * (1 - ss(0.41, 0.43, t))   # nada en la costura del hombro ni en la mano
    return h * keep

def groups(P):
    """(02/11) a que grupo pertenece cada punto (0..1): deltoides, biceps + braquial (adelante), triceps (atras), antebrazo.
    El PS marca cada grupo segun lo que hace el brazo (codo doblado, brazo arriba, esfuerzo)."""
    t, th, r = arm_frame(P)
    def win(a0, a1, b0, b1): return ss(a0, a1, t) * (1 - ss(b0, b1, t))
    def ang(c, w):
        d = (th - c + 180.0) % 360.0 - 180.0
        return g(d / w)
    dl = (1 - ss(0.085, 0.115, t)) * (1 - 0.85 * ang(0, 45))
    bi = win(0.035, 0.07, 0.15, 0.185) * np.maximum(ang(-90, 50), 0.8 * ang(-150, 20))
    tr = win(0.02, 0.05, 0.16, 0.19) * ang(95, 55)
    fo = win(0.17, 0.21, 0.40, 0.43)
    G = np.stack([dl, bi, tr, fo], -1)
    s = G.sum(-1, keepdims=True)
    return G / np.maximum(s, 1.0)

def main(folder, outdir):
    T = load_parts(folder, [("arm", 0)])
    G = bake(T, [], W=2048, H=2048, vmax=1.0)
    m = G["mask"]; ii = np.where(m)
    P = G["P"][ii]; Tg = G["Tg"][ii]; Bn = G["Bn"][ii]
    eps = 0.0006
    hT = (height(P + eps * Tg) - height(P - eps * Tg)) / (2 * eps)
    hB = (height(P + eps * Bn) - height(P - eps * Bn)) / (2 * eps)
    sl = np.zeros(m.shape + (2,), np.float32)
    sl[ii[0], ii[1], 0] = -hT; sl[ii[0], ii[1], 1] = -hB
    sl = np.clip(sl * 2.2, -0.6, 0.6)   # pendiente -> xy del normal map (x2.2: los surcos de 0.5 mm se leen a la luz rasante)
    hh = np.zeros(m.shape, np.float32); hh[ii] = height(P)
    cav = np.clip(1.0 + hh / 0.0012, 0.0, 1.0)   # 1 = sin cavidad, 0 = surco de 1.2 mm
    fullS, _ = dilate(sl, m, iters=32); fullC, _ = dilate(cav[..., None], m, iters=32)
    def mips(a):
        out = [a.astype(np.float32)]
        while min(out[-1].shape[:2]) > 1:
            c = out[-1]; out.append(0.25 * (c[0::2, 0::2] + c[1::2, 0::2] + c[0::2, 1::2] + c[1::2, 1::2]))
        return out
    write_dds_bc5_snorm(os.path.join(outdir, "pes_arm_anat_n.dds"), mips(fullS))
    write_dds_bc4_unorm(os.path.join(outdir, "pes_arm_anat_c.dds"), [x[..., 0] for x in mips(fullC)])
    Gg = np.zeros(m.shape + (4,), np.float32); Gg[ii] = groups(P)
    fullG, _ = dilate(Gg, m, iters=32)
    small = fullG.reshape(512, 4, 512, 4, 4).mean((1, 3))
    write_dds_float4(os.path.join(outdir, "pes_arm_anat_g.dds"), small)
    print("pes_arm_anat_n/c.dds: %d texels del brazo, pendiente max %.2f, surco mas hondo %.2f mm" % (len(ii[0]), np.abs(sl).max(), -1000 * hh.min()))
    return sl, cav, m

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
