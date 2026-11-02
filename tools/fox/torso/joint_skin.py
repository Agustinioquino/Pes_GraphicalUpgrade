# PES21 FX (02/11 g) - PIEL DE LAS ARTICULACIONES SEGUN LA POSE (canal w de pes_veins_arm.dds: codos y manos)
# Agustin (02/11): "el codo no se ve igual al doblarse que al estar estirado, podrias representarlo con texturas?" y "lo mismo con
# las manos, cambiando no solo su textura de pintado". En un brazo real:
#   - codo estirado: la piel de atras (sobre el olecranon) queda floja y hace pliegues (arcos casi transversales, irregulares);
#     doblado: se estira, los pliegues se van y la punta queda lisa y un poco mas clara. Adelante (la fosa del codo) hay 1-2
#     pliegues finos que con el codo doblado se hunden.
#   - dedos estirados: arrugas transversales en el dorso de las articulaciones (muchas en la del medio, pocas en la de la punta,
#     flojas en los nudillos); con el puno cerrado se estiran y desaparecen y los nudillos quedan lisos y claros. En la palma los
#     pliegues de flexion (dos en la articulacion del medio, uno en la de la punta, el digito-palmar) y las tres lineas de la palma
#     (distal, proximal y la del pulgar) se hunden al cerrar.
# Todo es surco (solo profundidad, nunca bulto), asi que un solo canal alcanza para los dos comportamientos:
#   w = 0.5 + 0.5 * prof / KQ  -> lado de ATRAS / dorso: pliegue que se va al doblar (w > 0.5)
#   w = 0.5 - 0.5 * prof / KQ  -> lado de ADELANTE / palma: pliegue que se hunde al doblar (w < 0.5)
# con prof en "texeles" (profundidad / tamano local del texel), asi el PS (aa7d1833) saca la pendiente con diferencias de un
# texel (4 lecturas de la misma textura t29: no hay que enlazar nada nuevo) sin saber la escala del atlas. Los surcos de los dos
# lados nunca se tocan (siempre hay piel lisa entre medio, w = 0.5).
# El resto de la textura (xyz: las venas) queda igual.
# Uso: python3 joint_skin.py <carpeta con arm.fmdl> <carpeta con hand_l.fmdl y hand_r.fmdl> <pes_veins_arm.dds> <salida>
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from fmdl import Fmdl, meshes
from torso_mesh import load_parts
from uvbake import bake
from torso_variants import arm_frame, ss, g
from hand_veins import frame as hand_frame, mips_rgba

KQ = 2.0          # profundidad maxima en texeles que entra en el canal (el PS usa el mismo numero)
SZ = 2048
OLEC = np.array([0.3982, 1.2654, -0.0085])   # punta del codo (reposo, brazo izquierdo)
FOSSA = np.array([0.3857, 1.2718, 0.0567])   # fosa del codo (adelante)

def crease(p, sharp=6.0):
    """surcos periodicos: 1 en el fondo del surco, 0 en la piel lisa entre surcos (p en periodos)"""
    return np.maximum(0.0, np.cos(2.0 * np.pi * p)) ** sharp

def wnoise(x, y, seed, n=6, f0=120.0, f1=420.0):
    """ruido suave (suma de ondas con direccion y fase al azar), ~ -1..1; x, y en metros"""
    r = np.random.RandomState(seed); out = 0.0
    for i in range(n):
        f = f0 * (f1 / f0) ** (i / max(n - 1, 1)); a = r.uniform(0, 2 * np.pi)
        out = out + np.sin(f * (np.cos(a) * x + np.sin(a) * y) + r.uniform(0, 2 * np.pi)) / np.sqrt(n)
    return out

def line_dist(px, py, pts):
    """distancia (misma unidad) de los puntos (px, py) a una polilinea, y la posicion 0..1 a lo largo de ella"""
    pts = np.asarray(pts, float); best = np.full(px.shape, 1e9); pos = np.zeros(px.shape)
    L = np.r_[0.0, np.cumsum(np.hypot(*np.diff(pts, axis=0).T))]
    for i in range(len(pts) - 1):
        ax_, ay_ = pts[i]; bx, by = pts[i + 1]; dx, dy = bx - ax_, by - ay_; l2 = dx * dx + dy * dy
        u = np.clip(((px - ax_) * dx + (py - ay_) * dy) / l2, 0.0, 1.0)
        d = np.hypot(px - ax_ - u * dx, py - ay_ - u * dy)
        s = d < best; best[s] = d[s]; pos[s] = (L[i] + u[s] * np.sqrt(l2)) / L[-1]
    return best, pos

# ------------------------------------------------------------------------------------------------------------------ codo
def elbow_depth(P):
    """(atras, adelante) profundidad en m"""
    t, th, r = arm_frame(P)
    to, tho, _ = [a[0] for a in arm_frame(OLEC[None])]
    tf, thf, _ = [a[0] for a in arm_frame(FOSSA[None])]
    rr = np.maximum(r, 0.02)
    # atras: arcos alrededor de la punta, aplanados (casi transversales al brazo), irregulares
    x = t - to; y = ((th - tho + 180.0) % 360.0 - 180.0) * np.pi / 180.0 * rr
    # arcos abiertos (no anillos cerrados: se veia una diana): lineas casi transversales que se curvan alrededor de la punta,
    # arriba y abajo hacia lados opuestos, con ondulacion; en la punta misma la piel queda mas lisa
    xw = x + 0.0014 * wnoise(x, y, 3, f0=90, f1=260)
    q = xw - np.sign(xw) * 9.0 * y * y * ss(0.0, 0.006, np.abs(xw))
    lam = 0.0040 * (1.0 + 0.15 * wnoise(x, y, 5, f0=60, f1=150))
    env = g(x / 0.020) * g(y / 0.017) * (0.45 + 0.55 * ss(0.002, 0.008, np.hypot(x, y / 1.3)))
    amp = 0.00042 * (0.7 + 0.3 * wnoise(x, y, 7, f0=150, f1=400))
    back = amp * env * crease(q / lam + 0.5 + 0.15 * wnoise(x, y, 9, f0=200, f1=500), 5.0)
    # cuadriculado fino de la piel floja (dos familias a +-35 grados, 1.4 mm)
    ca, sa = np.cos(np.radians(35)), np.sin(np.radians(35))
    fine = crease((ca * x + sa * y) / 0.0014, 8.0) + crease((ca * x - sa * y) / 0.0014, 8.0)
    back += 0.00010 * g(x / 0.015) * g(y / 0.013) * fine
    # adelante: dos pliegues transversales en la fosa (curvados un poco), el de arriba mas hondo
    xf = t - tf; yf = ((th - thf + 180.0) % 360.0 - 180.0) * np.pi / 180.0 * rr
    envf = g(yf / 0.024)
    front = 0.0
    for c0, dep, w in ((-0.0025, 0.00040, 0.0007), (0.0040, 0.00026, 0.0006), (0.0105, 0.00012, 0.0005)):
        xc = c0 + 6.0 * yf * yf + 0.0006 * wnoise(xf, yf, 11, f0=80, f1=200)
        front = front + dep * envf * g((xf - xc) / w)
    return back, front

# ------------------------------------------------------------------------------------------------------------------ mano
FINGERS = ("index", "middle", "ring", "pinky")

def hand_joints(m, names, side):
    P = m["pos"].astype(float)
    def bw(name):
        ids = [i for i, n in enumerate(names) if n == name]
        return ((m["idx"] == ids[0]) * m["w"]).sum(1) if ids else np.zeros(len(P))
    J = {}
    for fn in FINGERS:
        seg = ["skh_%s_%s_%s" % (fn, b, side) for b in ("mata", "mcp", "pip", "dip")]
        for jn, (a, b) in zip(("mcp", "pip", "dip"), zip(seg[:-1], seg[1:])):
            sel = (bw(a) > 0.25) & (bw(b) > 0.25)
            J[(fn, jn)] = P[sel].mean(0)
        tip = P[bw(seg[3]) > 0.9]
        J[(fn, "tip")] = tip[np.argmax((tip - J[(fn, "dip")]) @ (J[(fn, "dip")] - J[(fn, "pip")]))]
    return J

def hand_depth(P, N, W, A, C, D, L, Hw, cK, J):
    back = np.zeros(len(P)); front = np.zeros(len(P))
    # articulaciones de los dedos: (lambda m, sigma a m, sigma ang, prof m, cantidad de arcos) atras; pliegues de la palma
    BACK = {"mcp": (0.0024, 0.0050, 38.0, 0.00016), "pip": (0.0017, 0.0036, 52.0, 0.00024), "dip": (0.0013, 0.0024, 46.0, 0.00016)}
    for k, fn in enumerate(FINGERS):
        for jn in ("mcp", "pip", "dip"):
            Jc = J[(fn, jn)]
            nxt = J[(fn, {"mcp": "pip", "pip": "dip", "dip": "tip"}[jn])]
            F = nxt - Jc; seglen = np.linalg.norm(F); F /= seglen
            Dj = D - (D @ F) * F; Dj /= np.linalg.norm(Dj); Lj = np.cross(F, Dj)
            rel = P - Jc; a = rel @ F; nd = rel @ Dj; nl = rel @ Lj
            rad = np.hypot(nd, nl); phi = np.degrees(np.arctan2(nl, nd))   # 0 = dorso, 180 = palma
            near = rad < 0.016
            lam, sa_, sph, dep = BACK[jn]
            yy = np.radians(phi) * np.maximum(rad, 0.004)
            sd = 7 * k + {"mcp": 1, "pip": 2, "dip": 3}[jn]
            aw = a + 22.0 * yy * yy + 0.0003 * wnoise(a, yy, 20 + sd, f0=300, f1=900)
            amp = dep * (0.8 + 0.2 * wnoise(a, yy, 40 + sd, f0=400, f1=900))
            off = 0.0 if jn != "mcp" else 0.0015   # los pliegues del nudillo un poco hacia el dedo
            back += near * amp * g((a - off) / sa_) * g(phi / sph) * crease((aw - off) / lam + 0.5, 6.0)
            # palma: pliegues de flexion (mcp: el digito-palmar, mas alla de la articulacion)
            pal = g((np.abs(phi) - 180.0) / 50.0) * near
            if jn == "pip":
                front += pal * (0.00030 * g((a + 0.0009) / 0.00045) + 0.00026 * g((a - 0.0009) / 0.00045))
            elif jn == "dip":
                front += pal * 0.00024 * g((a + 0.0006) / 0.00045)
            else:
                front += pal * 0.00028 * g((a - 0.42 * seglen) / 0.00050)
    # las tres lineas de la palma (s 0 muneca .. 1 nudillo del medio; c +1 lado del indice, en Hw)
    s = ((P - W) @ A) / L; c = ((P - W) @ C) / Hw
    palm = ss(0.1, 0.5, -(N @ D)) * (s < 1.02)
    ci, cm, cr, cp = cK["index"], cK["middle"], cK["ring"], cK["pinky"]
    lines = [
        ([(0.80, cp - 0.25), (0.82, cr), (0.85, cm), (0.93, 0.5 * (ci + cm))], 0.00045),                   # distal (del corazon)
        ([(0.80, ci + 0.30), (0.73, 0.5 * (ci + cm)), (0.67, cr + 0.25), (0.63, cp + 0.10)], 0.00040),       # proximal (de la cabeza)
        ([(0.80, ci + 0.30), (0.63, ci - 0.05), (0.42, ci - 0.25), (0.22, ci - 0.30), (0.06, ci - 0.25)], 0.00042),  # la del pulgar
    ]
    sm = s * L; cmet = c * Hw   # en metros
    for pts, dep in lines:
        pm = [(a * L, b * Hw) for a, b in pts]
        d, u = line_dist(sm, cmet, pm)
        front += palm * dep * g(d / 0.0006) * ss(0.0, 0.12, u) * (1.0 - ss(0.80, 1.0, u)) * (0.85 + 0.15 * wnoise(sm, cmet, 77, f0=200, f1=500))
    return back, front

# ------------------------------------------------------------------------------------------------------------------ salida
def texel_size(G):
    P = G["P"]; m = G["mask"]
    du = np.zeros(m.shape); dv = np.zeros(m.shape)
    ok = m[:, 1:] & m[:, :-1]; d = np.linalg.norm(P[:, 1:] - P[:, :-1], axis=2); du[:, 1:][ok] = d[ok]
    ok2 = m[1:, :] & m[:-1, :]; d2 = np.linalg.norm(P[1:, :] - P[:-1, :], axis=2); dv[1:, :][ok2] = d2[ok2]
    s = np.where((du > 0) & (dv > 0), 0.5 * (du + dv), np.maximum(du, dv))
    med = np.median(s[m & (s > 0)])
    s = np.where((s > 0.25 * med) & (s < 4.0 * med), s, med)   # cortes de uv: el tamano tipico de la isla
    return s, med

def main(arm_folder, hand_folder, veins_dds, outdir):
    os.makedirs(outdir, exist_ok=True)
    d = bytearray(open(veins_dds, "rb").read()); base = 148
    atlas = np.frombuffer(bytes(d), np.uint8, count=SZ * SZ * 4, offset=base).reshape(SZ, SZ, 4).astype(np.float32) / 255.0
    Hd = np.zeros((SZ, SZ), np.float32)        # +atras / -adelante, en texeles
    fits = []
    # codo
    T = load_parts(arm_folder, [("arm", 0)]); G = bake(T, [], W=SZ, H=SZ, vmax=1.0)
    m = G["mask"]; ii = np.where(m); s, med = texel_size(G)
    bk, fr = elbow_depth(G["P"][ii])
    Hd[ii] = (bk - fr) / s[ii]
    print("codo: %d texeles, texel %.3f mm, atras max %.2f mm, adelante max %.2f mm" % (len(ii[0]), 1000 * med, 1000 * bk.max(), 1000 * fr.max()))
    fits.append((G, s, lambda Q, ii=ii: elbow_depth(Q)))
    # manos
    for side in ("l", "r"):
        f = Fmdl(os.path.join(hand_folder, "hand_%s.fmdl" % side)); names = [b["name"] for b in f.bones]; mm = meshes(f)[0]
        uvv = mm["uv"]; px = np.clip((uvv[:, 0] * SZ).astype(int), 0, SZ - 1); py = np.clip((uvv[:, 1] * SZ).astype(int), 0, SZ - 1)
        Nm = mm["nrm"][:, :3].astype(float); Nm /= np.linalg.norm(Nm, axis=1, keepdims=True)
        old = atlas[py, px, 2] > 0.3
        W, A, C, D, L, Hw, cK = hand_frame(mm, names, side, Nm[old].mean(0))
        J = hand_joints(mm, names, side)
        T = load_parts(hand_folder, [("hand_" + side, 0)]); G = bake(T, [], W=SZ, H=SZ, vmax=1.0)
        m = G["mask"]; ii = np.where(m); s, med = texel_size(G)
        bk, fr = hand_depth(G["P"][ii], G["N"][ii], W, A, C, D, L, Hw, cK, J)
        Hd[ii] = (bk - fr) / s[ii]
        print("mano %s: %d texeles, texel %.3f mm, dorso max %.2f mm, palma max %.2f mm, D (%.3f %.3f %.3f)" %
              (side, len(ii[0]), 1000 * med, 1000 * bk.max(), 1000 * fr.max(), *D))
        fits.append((G, s, lambda Q, Nq, a=(W, A, C, D, L, Hw, cK, J): hand_depth(Q, Nq, *a)))
    Hd = np.clip(Hd, -KQ, KQ)
    print("profundidad en texeles: atras max %.2f, adelante max %.2f (KQ %.1f)" % (Hd.max(), -Hd.min(), KQ))
    # orientacion: pendiente del PS (diferencias de un texel en u / v) contra la del marco tangente (como arm_anatomy.py)
    G, s, _ = fits[1]; m = G["mask"]
    inner = m.copy(); inner[1:-1, 1:-1] &= m[:-2, 1:-1] & m[2:, 1:-1] & m[1:-1, :-2] & m[1:-1, 2:]
    ii = np.where(inner[1:-1, 1:-1]); ii = (ii[0] + 1, ii[1] + 1)
    dep = np.abs(Hd)   # con el PS en "todo atras" (signo +) y "todo adelante" (signo -) por igual: profundidad
    gx = 0.5 * (dep[ii[0], ii[1] + 1] - dep[ii[0], ii[1] - 1]); gy = 0.5 * (dep[ii[0] + 1, ii[1]] - dep[ii[0] - 1, ii[1]])
    P = G["P"][ii]; Tg = G["Tg"][ii]; Bn = G["Bn"][ii]; Nn = G["N"][ii]; e = 0.0002
    side = fits[1][2]
    def depth(Q): b, f_ = side(Q, Nn); return b + f_
    sxT = (depth(P + e * Tg) - depth(P - e * Tg)) / (2 * e)   # pendiente real del surco (+ = se hunde hacia T)
    syT = (depth(P + e * Bn) - depth(P - e * Bn)) / (2 * e)
    sel = (np.abs(sxT) + np.abs(syT)) > 0.02
    for nm, a_ in (("x", sxT), ("y", syT)):
        cx = np.corrcoef(a_[sel], gx[sel])[0, 1]; cy = np.corrcoef(a_[sel], gy[sel])[0, 1]
        print("pendiente %s del marco: correlacion con du %.2f, con dv %.2f" % (nm, cx, cy))
    w = np.full((SZ, SZ), 0.5, np.float32)
    w = 0.5 + 0.5 * Hd / KQ
    atlas[..., 3] = w
    mp = mips_rgba(atlas)
    body = b"".join((np.clip(np.round(x * 255), 0, 255).astype(np.uint8)).tobytes() for x in mp)
    open(os.path.join(outdir, "pes_veins_arm.dds"), "wb").write(bytes(d[:base]) + body)
    np.save(os.path.join(outdir, "joint_depth.npy"), Hd)
    return Hd

if __name__ == "__main__":
    main(*sys.argv[1:5])
