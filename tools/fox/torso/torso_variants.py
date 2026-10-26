# PES21 FX (26/10) - variantes del torso de TorsoBody: atletico y robusto.
#
# Tres partes (las tres se mezclan con el mismo valor por jugador t: -1 robusto .. 0 TorsoBody .. +1 atletico):
#   1. FORMA GRANDE: un campo de desplazamiento que depende solo de la posicion de reposo (field(); el mismo calculo esta en
#      shaders\pes_torso_variant.hlsli para la fisica de la ropa: la camiseta NO esta pesada a los huesos del torso (dsk_*),
#      asi que la tela se corre en el simulador igual que la piel, y el cuerpo con el que choca tambien la tiene).
#   2. RELIEVE DEL MODELO: el relieve esculpido de la malla (abdominales, serratos, borde del pecho, costillas) se suaviza en
#      el robusto y se marca en el atletico. Se calcula sobre la malla soldada (las tres mallas del torso sin costuras), con los
#      bordes (brazos, cuello, short) quietos.
#   1 + 2 van por vertice al VS de la piel (textures\pes_torso_<malla>.dds, t127) con las normales / tangentes nuevas de la
#   malla deformada (las de TorsoBody giradas lo mismo que gira la normal de la malla soldada).
#   3. TEXTURAS (mapas normales delta, PS de la piel): ver torso_maps.py.
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from torso_mesh import load_torso, load_parts, umbrella_matrix, gauss_matrix, geodesic_from, vertex_normals, rotate_min

def ss(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)
def g(u): return np.exp(-0.5 * u * u)

# ---------------------------------------------------------------- 1. forma grande (= PesTorsoField en pes_torso_variant.hlsli)
# Medidas de TorsoBody v11 (reposo, m): ombligo (0, 1.061, 0.175), pezones (+-0.059, 1.303, 0.158), escote adelante 1.478,
# cintura del short ~1.00..1.08 (atras mas alta), frente del torso z 0.177 (panza baja) .. 0.165 (cintura) .. 0.171 (costillas),
# espalda -0.037 (lumbar) .. -0.066 (omoplatos), medio ancho 0.150 .. 0.157 hasta y 1.22, 0.175 en y 1.32 (axilas en 1.34).
ZC, AX, BZ = 0.058, 0.155, 0.110   # centro (z) y medio ancho / medio fondo de la seccion del torso

def torso_mask(p):   # = PesTorsoMask (pes_skintone.hlsli): 1 en el torso, 0 en brazos y cuello
    q = np.stack([np.abs(p[:, 0]), p[:, 1], p[:, 2]], axis=1)
    cA = np.array([0.2631, 1.3704, 0.0195]); nA = np.array([0.7611, -0.6432, -0.0839])
    qa = q - cA; sA = qa @ nA; rA = np.linalg.norm(qa - sA[:, None] * nA, axis=1)
    arm = ss(-0.004, 0.004, sA) * (1.0 - ss(0.11, 0.13, rA))
    cN = np.array([0.0, 1.5223, 0.0552]); nN = np.array([0.0, 0.8564, 0.5163]); e2 = np.array([0.0, 0.5163, -0.8564])
    qn = q - cN; sN = qn @ nN; wn = qn - sN[:, None] * nN
    lw = np.maximum(np.linalg.norm(wn, axis=1), 1e-5); ct = (wn @ e2) / lw
    hN = 0.00037 - 0.00183 * ct - 0.01279 * (2.0 * ct * ct - 1.0)
    neck = ss(-0.004, 0.004, sN - hN) * (1.0 - ss(0.10, 0.12, q[:, 0]))
    return (1.0 - arm) * (1.0 - neck) * ss(0.86, 0.90, q[:, 1]) * (1.0 - ss(1.561, 1.569, q[:, 1]))

def field(p, t):
    """desplazamiento grande (m) de cada punto de reposo p (N x 3) para la variante t (-1 robusto .. 1 atletico)"""
    x = np.abs(p[:, 0]); sx = np.where(p[:, 0] < 0, -1.0, 1.0); y = p[:, 1]; z = p[:, 2]
    dx = x / (AX * AX); dz = (z - ZC) / (BZ * BZ)
    L = np.sqrt(dx * dx + dz * dz) + 1e-6
    rx, rz = dx / L, dz / L                       # direccion hacia afuera de la seccion (eliptica)
    front = ss(-0.2, 0.6, rz); back = ss(-0.2, 0.6, -rz); side = 1.0 - np.abs(rz)
    m = torso_mask(p) * ss(0.995, 1.035, y)       # abajo del todo entra en el short
    wa = max(t, 0.0); wr = max(-t, 0.0)
    rad = np.zeros(len(p)); latx = np.zeros(len(p)); dy = np.zeros(len(p))
    if wr > 0:
        # panza: mas llena abajo del ombligo, se apoya en la cintura del short
        uy = np.where(y > 1.10, (y - 1.10) / 0.085, (y - 1.10) / 0.050)
        rad += wr * 0.028 * g(uy) * g(x / 0.11) * front ** 1.3
        # rollos de los costados (arriba de la cadera) y espalda baja
        rad += wr * 0.016 * g((y - 1.08) / 0.045) * side * (0.6 + 0.4 * back) * ss(0.06, 0.13, x)
        rad += wr * 0.008 * g((y - 1.09) / 0.050) * back
        # cintura mas ancha y todo el torso un poco mas grueso
        rad += wr * 0.010 * g((y - 1.17) / 0.070) * side
        rad += wr * 0.005 * ss(1.04, 1.10, y) * (1.0 - ss(1.38, 1.46, y))
        # pecho con grasa, caido y sin forma: la masa baja y adelante, arriba mas plano; costado del pecho y espalda alta
        rad += wr * 0.009 * g((x - 0.068) / 0.055) * g((y - 1.282) / 0.034) * front
        rad -= wr * 0.002 * g((x - 0.070) / 0.060) * g((y - 1.385) / 0.030) * front
        dy -= wr * 0.010 * g((x - 0.068) / 0.055) * g((y - 1.300) / 0.036) * front
        rad += wr * 0.004 * g((x - 0.15) / 0.03) * g((y - 1.30) / 0.04) * (1.0 - back)
        rad += wr * 0.005 * g((y - 1.44) / 0.040) * back
    if wa > 0:
        # espalda en V: los dorsales hacia los costados
        latx += wa * 0.010 * g((y - 1.30) / 0.060) * ss(0.10, 0.15, x) * ss(-0.6, 0.2, -rz)
        # cintura mas fina y panza baja plana
        rad -= wa * 0.009 * g((y - 1.14) / 0.060) * side
        rad -= wa * 0.008 * g((y - 1.08) / 0.050) * front * g(x / 0.09)
        # pectorales mas llenos con el borde de abajo marcado
        rad += wa * 0.007 * g((x - 0.070) / 0.050) * g((y - 1.335) / 0.040) * front
        rad -= wa * 0.0035 * g((x - 0.075) / 0.045) * g((y - 1.285) / 0.012) * front
        # trapecio medio (espalda alta, entre los omoplatos)
        rad += wa * 0.0060 * g((x - 0.055) / 0.045) * g((y - 1.445) / 0.040) * back
    if wa > 0:
        # trapecio alto: arriba de los hombros, del cuello hacia el hombro (crece hacia arriba y un poco atras)
        tr = wa * 0.0120 * g((x - 0.115) / 0.042) * g((y - 1.522) / 0.026) * (1.0 - 0.8 * front)
        trd = np.array([0.25, 0.90, -0.35]); trd /= np.linalg.norm(trd)
    else:
        tr = np.zeros(len(p)); trd = np.zeros(3)
    out = np.zeros_like(p)
    out[:, 0] = (rad * rx + latx + tr * trd[0]) * sx * m
    out[:, 2] = (rad * rz + tr * trd[2]) * m
    out[:, 1] = (dy + tr * trd[1]) * m
    return out

# ---------------------------------------------------------------- 2. relieve del modelo (por vertice)
NIPPLE = np.array([0.0594, 1.3027, 0.1579])
NAVEL = np.array([0.0, 1.0608, 0.1750])

def lap_smooth(W, X, fixed, iters, lam=0.5):
    X = X.copy(); free = np.ones(len(X), bool); free[fixed] = False
    for _ in range(iters): X[free] += lam * ((W @ X - X)[free])
    return X

def detail_masks(Pw):
    x = np.abs(Pw[:, 0]); y = Pw[:, 1]; z = Pw[:, 2]
    zc = 0.058
    front = ss(zc - 0.03, zc + 0.06, z)                              # 1 adelante, 0 atras (con los costados en el medio)
    keep = (1 - g(np.linalg.norm(np.stack([x - NIPPLE[0], y - NIPPLE[1], z - NIPPLE[2]], 1), axis=1) / 0.011)) \
         * (1 - g(np.linalg.norm(np.stack([x - NAVEL[0], y - NAVEL[1], z - NAVEL[2]], 1), axis=1) / 0.016))
    band = ss(1.045, 1.075, y)
    # robusto: abdomen, costados, pecho bajo y algo de la espalda baja / media se alisan
    mR = band * (1 - ss(1.40, 1.46, y)) * (0.55 + 0.45 * front) * keep
    # atletico: abdominales, serratos y borde del pecho se marcan; la espalda un poco
    mA = band * (1 - ss(1.42, 1.48, y)) * (0.40 + 0.60 * front) * keep
    return mR, mA

def build_mesh(T, masks_fn, field_fn=None, sigma=0.025, kR=(0.85, 0.40), kA=(0.20, 0.60), fade_w=(0.012, 0.045), sculpt_fn=None,
               inflate=0.0, hp=None, kg=None):
    """desplazamientos por variante (robusto, atletico) por vertice soldado de la malla T (torso_mesh.load_parts).
    sigma = escala del relieve (m); kR = (picos, valles) del relieve que se quitan en el robusto; kA = (picos, valles) que se
    agregan en el atletico."""
    Pw, Fw, E, B = T["Pw"], T["Fw"], T["edges"], T["boundary"]
    G = gauss_matrix(Pw, Fw, sigma)
    S = G @ Pw
    D1 = Pw - S
    D = D1 - G @ D1                                    # relieve de banda (sin el encogimiento del promedio)
    n = vertex_normals(Pw, Fw)
    nf = np.zeros_like(Pw)                             # normales del fmdl (hacia afuera) en la malla soldada
    for p_ in T["parts"]: np.add.at(nf, T["inv"][p_["off"]: p_["off"] + p_["nvert"]], p_["nrm"])
    if np.sum(n * nf) < 0: n = -n                      # el orden de los triangulos de TorsoBody da la normal hacia adentro
    dn = np.sum(D * n, 1)
    fade = ss(fade_w[0], fade_w[1], geodesic_from(Pw, E, B))   # quieto en las costuras
    mR, mA = masks_fn(Pw)
    pos = ss(-0.0008, 0.0008, dn)                      # 1 en los picos, 0 en los valles (suave en el cruce)
    kr = kR[1] + (kR[0] - kR[1]) * pos
    ka = kA[1] + (kA[0] - kA[1]) * pos
    detR = (-kr * dn * mR)[:, None] * n
    detA = (ka * dn * mA)[:, None] * n
    bigR = field_fn(Pw, -1.0) if field_fn else np.zeros_like(Pw)
    bigA = field_fn(Pw, 1.0) if field_fn else np.zeros_like(Pw)
    if inflate > 0.0:   # (26/10 b) atletico: las panzas de los musculos del modelo mas redondas (relieve positivo desenfocado)
        G2 = gauss_matrix(Pw, Fw, 0.014)
        detA = detA + (inflate * (G2 @ np.maximum(dn, 0.0)) * mA)[:, None] * n
    if sculpt_fn is not None:   # (26/10 b) musculos (atletico) y pliegues (robusto) esculpidos en la malla, a lo largo de la normal
        hR, hA = sculpt_fn(Pw)
        detR = detR + hR[:, None] * n; detA = detA + hA[:, None] * n
    dB = np.zeros_like(Pw)
    if hp is not None:   # (26/10 b) la altura que dibuja el normal map, llevada a la malla (kg = (base, robusto, atletico) por vertice)
        kB, kRr, kAa = kg
        dB = (kB * hp)[:, None] * n
        detR = detR + (kRr * hp)[:, None] * n; detA = detA + (kAa * hp)[:, None] * n
    dR = (bigR + detR) * fade[:, None]
    dA = (bigA + detA) * fade[:, None]
    dB = dB * fade[:, None]
    T.update(S=S, D=D, n=n, dn=dn, fade=fade, mR=mR, mA=mA, detR=detR, detA=detA, bigR=bigR, bigA=bigA, dR=dR, dA=dA, dB=dB)
    return T

def build(folder, version="out", texdir=None):
    """torso (mallas 1..3 de pants_<version>_sub). (26/10 b) con texdir: el relieve del normal map de TorsoBody va a la malla
    (base para todos, mas hondo en el atletico, casi nada en el robusto: torso_relief.REGION)"""
    T = load_torso(folder, version)
    hp = kg = None
    if texdir:
        import torso_relief as tr
        from torso_base import back_mask
        hp = tr.relief_hp(T, texdir)
        bm = back_mask(T["Pw"])
        kg = tuple(tr.region_factors(nm, bm, T["Pw"])[2] for nm in ("normal", "robusto", "atletico"))
    return build_mesh(T, detail_masks, field, kR=(0.95, 0.70), kA=(0.55, 1.00), sculpt_fn=sculpt_torso, inflate=0.8, hp=hp, kg=kg)

# ---------------------------------------------------------------- (26/10 b) esculpido del torso por variante (m, a lo largo de
# la normal). Atletico: volumen real de los musculos (borde y masa del pectoral, esternon, cinturon de Adonis con los oblicuos
# encima, linea alba, y la espalda: dorsales abajo en V, infraespinoso, redondo mayor, omoplato, erectores; la misma anatomia que
# la textura, torso_maps.height_back_athletic, mas grande y mas suave). Robusto: pliegues de grasa reales (la panza sobre el
# short, los rollos de los costados y de la espalda baja, el de debajo del omoplato y el del pecho caido).
def sculpt_torso(P):
    import torso_maps as tm
    x, y, z, rz, front, back, side = tm._frame(P)
    hA = np.zeros(len(P)); hR = np.zeros(len(P))
    # ---- atletico, adelante
    yp = 1.284 + 0.010 * ((x - 0.075) / 0.050) ** 2
    hA += (-0.0035 * g((y - yp) / 0.0075) + 0.0030 * g((y - yp - 0.030) / 0.022)) * g((x - 0.075) / 0.050) * front
    hA += -0.0020 * g(x / 0.009) * ss(1.27, 1.30, y) * (1 - ss(1.43, 1.46, y)) * front
    ax_, ay_, bx_, by_ = 0.126, 1.088, 0.046, 1.000
    ux, uy = bx_ - ax_, by_ - ay_; Lu = np.hypot(ux, uy)
    t = ((x - ax_) * ux + (y - ay_) * uy) / (Lu * Lu); ds = ((x - ax_) * uy - (y - ay_) * ux) / Lu
    on = ss(-0.05, 0.20, t) * (1 - ss(0.85, 1.05, t)) * (front + 0.5 * side)
    hA += (-0.0030 * g(ds / 0.0075) + 0.0035 * g((ds - 0.016) / 0.012)) * on
    hA += -0.0015 * g(x / 0.0065) * ss(1.065, 1.09, y) * (1 - ss(1.25, 1.275, y)) * front
    # ---- atletico, atras: volumenes anchos (la malla tiene aristas de 6.5 mm: nada de surcos finos, esos van en la textura):
    #      dorsales abajo y a los costados (V), redondo mayor e infraespinoso, el rombo del trapecio medio / inferior, el borde del
    #      omoplato, erectores con el surco de la columna, y el valle suave entre dorsales y erectores
    bk = back * ss(1.00, 1.04, y)
    hA += bk * (0.0060 * tm._ell(x, y, 0.130, 1.235, 0.040, 0.090, -0.18)
                + 0.0035 * tm._ell(x, y, 0.140, 1.345, 0.040, 0.016, 0.50)
                + 0.0030 * tm._ell(x, y, 0.128, 1.395, 0.036, 0.028, 0.25)
                + 0.0030 * g(x / 0.045) * ss(1.24, 1.30, y) * (1 - ss(1.47, 1.53, y))
                + 0.0020 * g(tm.seg_dist(x, y, 0.078, 1.452, 0.097, 1.318)[0] / 0.008)
                + 0.0050 * g((x - 0.034) / 0.014) * ss(1.04, 1.08, y) * (1 - ss(1.26, 1.34, y))
                - 0.0030 * g(x / 0.008) * ss(1.04, 1.08, y) * (1 - ss(1.30, 1.40, y))
                - 0.0025 * g(tm.seg_dist(x, y, 0.094, 1.300, 0.076, 1.120)[0] / 0.010))
    # ---- robusto
    yc = 1.040 + 0.022 * (x / 0.15) ** 2
    hR += (-0.0075 * g((y - yc) / 0.0080) + 0.0035 * g((y - yc - 0.020) / 0.014)) * (front + 0.6 * side) * (1 - ss(0.14, 0.17, x))
    yl = 1.050 + 0.012 * back
    hR += (-0.0060 * g((y - yl) / 0.0080) + 0.0030 * g((y - yl - 0.020) / 0.014)) * ss(0.10, 0.135, x) * (side + 0.8 * back)
    for yb in (1.105, 1.160):
        hR += (-0.0030 * g((y - yb) / 0.0070) + 0.0015 * g((y - yb + 0.016) / 0.012)) * back * ss(0.025, 0.05, x) * (1 - ss(0.11, 0.15, x))
    yf = 1.268 + 0.45 * (x - 0.09)
    hR += (-0.0040 * g((y - yf) / 0.0095) + 0.0025 * g((y - yf + 0.020) / 0.015)) * ss(0.07, 0.10, x) * (1 - ss(0.17, 0.19, x)) * back
    ypc = 1.252 + 0.018 * ((x - 0.070) / 0.060) ** 2
    hR += -0.0045 * g((y - ypc) / 0.0080) * g((x - 0.070) / 0.052) * front
    return hR, hA

def arm_masks(Pw):
    """brazos: todo el relieve (deltoides, biceps, triceps, antebrazo); la mano no esta en esta malla"""
    one = np.ones(len(Pw))
    return one, one

# grosor de los brazos con los huesos de la musculatura (= ArmVariant() en src/musculo.cpp): la manga de la camiseta esta pesada a
# dsk_sleeve, que sigue al brazo, asi que acompana sola. Variacion de los factores (como "e" de ComputeFactors) por unidad.
ARM_BONES = {"delt": (0.03, 0.04), "bic": (0.05, 0.04), "tri": (0.09, 0.04), "fore": (0.04, 0.02)}   # (robusto, atletico)
K_ARM_R = 0.044

def arm_bone_factors(t):
    wr, wa = max(-t, 0.0), max(t, 0.0)
    k = {n: r * wr + a * wa for n, (r, a) in ARM_BONES.items()}
    arm = 0.5 * (k["bic"] + k["tri"]); armB = K_ARM_R * 0.5 * (k["bic"] - k["tri"])
    return {"M_DELT": (k["delt"], np.zeros(3)), "M_ARM": (arm, np.array([0.0, 0.0, armB])),
            "M_FORE": (k["fore"], np.zeros(3)), "M_FORE2": (k["fore"], np.zeros(3))}

ARM_KG = (1.0, 0.3, 2.0)   # (26/10 b) relieve del normal map en la malla del brazo: (base, robusto, atletico)

def build_arm(folder, fn="arm", mesh=0, texdir=None):
    T = load_parts(folder, [(fn, mesh)])
    T["version"] = fn
    hp = kg = None
    if texdir:   # (26/10 b) el relieve que dibuja arm_nrm (biceps, triceps, antebrazo) va a la malla, como en el torso
        import torso_relief as tr
        hp = tr.relief_hp(T, texdir, fname="arm_nrm.ftex", W=2048, H=2048, vmax=1.0)
        one = np.ones(len(T["Pw"]))
        kg = tuple(k * one for k in ARM_KG)
    return build_mesh(T, arm_masks, None, sigma=0.015, kR=(0.90, 0.60), kA=(0.45, 0.90), hp=hp, kg=kg)

def per_vertex(T, dW):
    """desplazamiento por vertice soldado -> por vertice de cada malla (lista, en el orden de T['parts'])"""
    out = []
    for p in T["parts"]:
        out.append(dW[T["inv"][p["off"]: p["off"] + p["nvert"]]])
    return out

def morph_normals(T, dW):
    """normales y tangentes de TorsoBody giradas con el cambio de la normal de la malla soldada"""
    Pw, Fw = T["Pw"], T["Fw"]
    n0 = vertex_normals(Pw, Fw); n1 = vertex_normals(Pw + dW, Fw)
    res = []
    for p in T["parts"]:
        iw = T["inv"][p["off"]: p["off"] + p["nvert"]]
        a, b = n0[iw], n1[iw]
        nn = rotate_min(a, b, p["nrm"]); nn /= np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-9)
        tt = rotate_min(a, b, p["tan"][:, :3])
        tt = tt - nn * np.sum(tt * nn, 1, keepdims=True); tt /= np.maximum(np.linalg.norm(tt, axis=1, keepdims=True), 1e-9)
        res.append((nn, tt))
    return res
