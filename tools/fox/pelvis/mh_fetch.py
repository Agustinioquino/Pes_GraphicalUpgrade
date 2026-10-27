# PES21 FX (26/10 e) - trae la malla base de MakeHuman (CC0 1.0: https://github.com/makehumancommunity/makehuman, LICENSE.md,
# "The license for the bundled assets") con los targets de hombre joven, y guarda solo la zona de la pelvis (y 0.45 .. 1.35 m,
# sin los brazos) en mh_pelvis_cc0.npz: la forma anatomica de la que sale la pelvis "sin short" (pelvis_surface.py).
# Uso: python3 mh_fetch.py [carpeta de descarga]   (el npz queda al lado de este script)
import sys, os, urllib.request
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RAW = "https://raw.githubusercontent.com/makehumancommunity/makehuman/master/makehuman/data/"
FILES = ["3dobjs/base.obj"] + ["targets/macrodetails/%s-male-young.target" % e for e in ("caucasian", "african", "asian")]

def load_obj(p):
    V, F, G, g = [], [], [], None
    for line in open(p, encoding="utf-8", errors="replace"):
        if line.startswith("v "): V.append([float(x) for x in line.split()[1:4]])
        elif line.startswith("g "): g = line.split()[1]
        elif line.startswith("f "): F.append([int(t.split("/")[0]) - 1 for t in line.split()[1:]]); G.append(g)
    return np.array(V), F, G

def load_target(p, n):
    D = np.zeros((n, 3))
    for line in open(p, encoding="utf-8", errors="replace"):
        if not line.strip() or line.startswith("#"): continue
        a = line.split(); D[int(a[0])] = [float(a[1]), float(a[2]), float(a[3])]
    return D

def main():
    dl = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "_mh_dl")
    os.makedirs(dl, exist_ok=True)
    for f in FILES:
        dst = os.path.join(dl, os.path.basename(f))
        if not os.path.exists(dst):
            print("bajando", f); urllib.request.urlretrieve(RAW + f, dst)
    V, F, G = load_obj(os.path.join(dl, "base.obj"))
    n = len(V)
    V = V + sum(load_target(os.path.join(dl, "%s-male-young.target" % e), n) for e in ("caucasian", "african", "asian")) / 3.0
    Q = np.array([f for f, g in zip(F, G) if g == "body"])          # solo el cuerpo (sin ojos ni ayudantes), quads
    T = np.concatenate([Q[:, [0, 1, 2]], Q[:, [0, 2, 3]]])
    P = V * 0.1; P[:, 1] -= P[Q.ravel(), 1].min()                  # decimetros -> metros, pies en y = 0
    ok = np.all((P[T][:, :, 1] > 0.45) & (P[T][:, :, 1] < 1.35) & (np.abs(P[T][:, :, 0]) < 0.24), axis=1)
    T = T[ok]; used = np.unique(T.ravel()); rm = -np.ones(len(P), int); rm[used] = np.arange(len(used))
    out = os.path.join(HERE, "mh_pelvis_cc0.npz")
    np.savez_compressed(out, P=P[used].astype(np.float32), T=rm[T].astype(np.int32),
                        nota="MakeHuman base mesh + male young targets (CC0 1.0, makehumancommunity.org), zona de la pelvis, metros")
    print("%s: %d vertices, %d triangulos" % (out, len(used), len(T)))

if __name__ == "__main__":
    main()
