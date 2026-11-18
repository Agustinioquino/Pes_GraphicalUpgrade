# PES21 FX (15/11) - medir, FUERA del juego y en la pose de armado, cada pieza de uniforme (shirt_*, collar_NNN, sleeve_*,
# cap_sleeve_*, referee_collar_*) contra la piel del cuerpo (torso pants_*_sub mallas 1..3, arm*, neck): cuanta piel queda AFUERA
# de la tela (se veria atravesando) y cuanta holgura hay. Sirve para los cuatro casos (con / sin shader, con / sin fisica): es el
# punto de partida comun (modelo contra modelo, sin shader y sin simulacion).
# Uso: python3 medir_ropa_modelos.py <carpeta uniform (#Win)> <carpeta con los fmdl del common_package> [salida.tsv]
import sys, os, glob, re
import numpy as np
from scipy.spatial import cKDTree
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
from fmdl import Fmdl, meshes

STEP = 0.004      # separacion de las muestras sobre la tela (m)
NEAR = 0.03       # la piel cuenta como "debajo de esta tela" si la tela mas cercana esta a menos de esto

def load(path, pick=None):
    P, N, F, off = [], [], [], 0
    for i, m in enumerate(meshes(Fmdl(path))):
        if pick is not None and i not in pick: continue
        if m["pos"] is None or m["faces"] is None or m["nrm"] is None: continue
        P.append(m["pos"].astype(np.float64)); N.append(m["nrm"][:, :3].astype(np.float64)); F.append(m["faces"] + off); off += m["nvert"]
    if not P: return None
    P = np.concatenate(P); N = np.concatenate(N); F = np.concatenate(F)
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)
    return P, N, F

def sample(P, N, F):
    """puntos sobre los triangulos (baricentricos, paso ~STEP) con la normal del archivo interpolada"""
    a, b, c = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
    L = np.maximum.reduce([np.linalg.norm(b - a, axis=1), np.linalg.norm(c - b, axis=1), np.linalg.norm(a - c, axis=1)])
    out_p, out_n = [], []
    for k in np.unique(np.clip(np.ceil(L / STEP).astype(int), 1, 24)):
        sel = np.where(np.clip(np.ceil(L / STEP).astype(int), 1, 24) == k)[0]
        uv = [(i / k, j / k) for i in range(k + 1) for j in range(k + 1 - i)]
        for u, v in uv:
            w = 1 - u - v
            out_p.append(a[sel] * w + b[sel] * u + c[sel] * v)
            n = N[F[sel, 0]] * w + N[F[sel, 1]] * u + N[F[sel, 2]] * v
            out_n.append(n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9))
    return np.concatenate(out_p), np.concatenate(out_n)

def zone(q):
    """zona de un punto de la piel (reposo), como la medicion del juego"""
    x, y, z = abs(q[0]), q[1], q[2]
    sA = (np.array([x, y, z]) - np.array([0.2631, 1.3704, 0.0195])) @ np.array([0.7611, -0.6432, -0.0839])
    if sA > 0.02: return "manga"
    if y > 1.44 and x < 0.11: return "escote"
    if sA > -0.05 and y < 1.40: return "axila"
    if y > 1.42: return "hombro"
    if y > 1.18: return "pecho/espalda"
    return "cintura"

def measure(gar, skin):
    """devuelve por zona: piel debajo de la tela, % afuera, maximo mm; y holgura media mm"""
    gp, gn = sample(*gar)
    tree = cKDTree(gp)
    sp, sn, _ = skin
    d, j = tree.query(sp, distance_upper_bound=NEAR)
    ok = np.isfinite(d)
    if not ok.any(): return {}
    idx = np.where(ok)[0]
    v = sp[idx] - gp[j[idx]]
    sd = (v * gn[j[idx]]).sum(1)                 # > 0: la piel esta del lado de AFUERA de la tela
    face = (sn[idx] * gn[j[idx]]).sum(1)         # la tela mira para el mismo lado que la piel (no es un forro ni la cara de adentro)
    lat = np.sqrt(np.maximum((v * v).sum(1) - sd * sd, 0))
    use = (face > 0.3) & (lat < 0.012)
    res = {}
    zs = np.array([zone(q) for q in sp[idx]])
    for zname in np.unique(zs[use]):
        m = use & (zs == zname)
        out = sd[m] > 0.0005
        res[zname] = (int(m.sum()), 100.0 * out.mean(), 1000.0 * (sd[m][out].max() if out.any() else 0.0), 1000.0 * float(np.mean(-sd[m][~out])) if (~out).any() else 0.0)
    return res

BODY_OF = [(r"shirt_in_tight", "pants_out_sub_tight"), (r"shirt_out_tight", "pants_out_sub_tight"), (r"shirt_in", "pants_in_sub"), (r".*", "pants_out_sub")]

def main():
    udir, bdir = sys.argv[1], sys.argv[2]
    out = open(sys.argv[3], "w") if len(sys.argv) > 3 else None
    skins = {}
    def skin(torso, arm):
        k = (torso, arm)
        if k not in skins:
            parts = [load(os.path.join(bdir, torso + ".fmdl"), (1, 2, 3)), load(os.path.join(bdir, arm + ".fmdl")), load(os.path.join(bdir, "neck.fmdl"))]
            off = 0; P = []; N = []; F = []
            for p in parts:
                if p is None: continue
                P.append(p[0]); N.append(p[1]); F.append(p[2] + off); off += len(p[0])
            skins[k] = (np.concatenate(P), np.concatenate(N), np.concatenate(F))
        return skins[k]
    rows = []
    for path in sorted(glob.glob(os.path.join(udir, "*.fmdl"))):
        name = os.path.basename(path)[:-5]
        if name.startswith("pants") or name.startswith("s_inner"): continue
        g = load(path)
        if g is None: continue
        torso = next(b for pat, b in BODY_OF if re.match(pat, name))
        arm = "arm"
        r = measure(g, skin(torso, arm))
        for z, (n, pct, mx, hol) in sorted(r.items()):
            rows.append((name, z, n, pct, mx, hol))
    hdr = "pieza\tzona\tpuntos_de_piel\tafuera_%\tmaximo_mm\tholgura_media_mm"
    lines = [hdr] + ["%s\t%s\t%d\t%.1f\t%.1f\t%.1f" % r for r in rows]
    (out.write("\n".join(lines) + "\n") if out else print("\n".join(lines)))

if __name__ == "__main__": main()
