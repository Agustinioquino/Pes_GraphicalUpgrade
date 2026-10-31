# PES21 FX (31/10 b) - ACHICAR EL TORSO DE TORSOBODY DE VERDAD: con "Ver original" (y con la fisica de la ropa, que choca con la
# malla real que lee cuerpo.cpp) la malla del torso es mas grande que la camiseta en el pecho alto y las axilas, y el brazo alto
# mas grande que la manga. El shader (fila 320) solo lo tapaba en pantalla. Aca se mete la malla:
#   - torso (pants_out_sub, pants_in_sub, pants_out_inner_sub, pants_out_sub_tight; mallas 1..3): MM mm hacia adentro por la
#     normal (suavizada), MMX en la axila; 0 en las costuras del cuello y del short (fundido en FADE m por la malla: el anillo del
#     cuello queda donde corta TorsoBody_cuello.lua). El anillo del brazo SI se mueve, y el brazo copia ese mismo corrimiento.
#     La panza (debajo de PANZA) queda intacta: se ve cuando el jugador salta y se le levanta la camiseta.
#   - brazo (arm, arm_cap, arm_glove, arm_glove_cap, arm_half; todas sus mallas): en el hombro el corrimiento del torso (sin
#     hueco), en FADE pasa a MMA mm hasta LARGO m por el brazo y se apaga en otros 7 cm (bajo la manga corta; el antebrazo igual).
# Normales y tangentes giradas con la superficie. Mismas mallas, mismos vertices: se reemplazan en el fpk con patch_fpk.py.
# Uso: python3 torso_shrink.py <carpeta con los .fmdl del fpk> <carpeta de salida> [MM=6 pecho] [MMX=12 axila] [MMA=4 brazo] [MMB=3 espalda]
# El resultado es un modelo de TorsoBody: se arma en la PC del usuario (o en scratch), no va al repo.
import sys, os, shutil
import numpy as np
from scipy.spatial import cKDTree
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'pelvis')]
from fmdl import Fmdl, meshes
from torso_mesh import load_parts, geodesic_from, vertex_normals, rotate_min, gauss_matrix
from pelvis_fmdl import write_mesh_geom

FADE = 0.025
LARGO = 0.15
PANZA = (1.18, 1.28)   # debajo de 1.18 m el torso no se toca (panza / cintura); arriba de 1.28 todo
TORSO = ("pants_out_sub", "pants_in_sub", "pants_out_inner_sub", "pants_out_sub_tight")
ARMS = ("arm", "arm_cap", "arm_glove", "arm_glove_cap", "arm_half")

def ss(x): x = np.clip(x, 0, 1); return x * x * (3 - 2 * x)

CA = np.array([0.2631, 1.3704, 0.0195]); NA = np.array([0.7611, -0.6432, -0.0839])   # anillo brazo / torso (pes_skintone.hlsli)

def arm_seam(P, idx):
    q = P[idx].copy(); q[:, 0] = np.abs(q[:, 0])
    return idx[(np.abs((q - CA) @ NA) < 0.02) & (np.linalg.norm(q - CA, axis=1) < 0.10)]

def smooth_normals(T):
    Pw, Fw = T["Pw"], T["Fw"]
    N = vertex_normals(Pw, Fw)
    Ns = gauss_matrix(Pw, Fw, 0.01) @ N             # normal suavizada (1 cm): no sigue las arrugas finas
    Ns /= np.maximum(np.linalg.norm(Ns, axis=1, keepdims=True), 1e-12)
    # las caras de TorsoBody van en el otro sentido (la normal por caras apunta adentro): se orienta con la normal del archivo
    fn = np.concatenate([p["nrm"][:, :3] for p in T["parts"]])
    if np.mean(np.sum(Ns[T["inv"]] * fn, 1)) < 0: Ns = -Ns
    return N, Ns

def write_file(src, dst, T, Q, N, label, amt):
    Fw, inv = T["Fw"], T["inv"]
    N1 = vertex_normals(Q, Fw)
    tmp = dst + ".tmp"; shutil.copyfile(src, tmp)
    for p in T["parts"]:
        w = inv[p["off"]:p["off"] + p["nvert"]]
        nrm4 = np.concatenate([rotate_min(N[w], N1[w], p["nrm"]), np.zeros((p["nvert"], 1))], 1)
        tan = p["tan"].copy(); tan[:, :3] = rotate_min(N[w], N1[w], tan[:, :3])
        write_mesh_geom(tmp, tmp, p["index"], Q[w], nrm4, tan)
    os.replace(tmp, dst)
    print("%s: %d vertices, se meten %d, maximo %.1f mm, media %.1f mm" % (label, len(Q), (amt > 1e-4).sum(), amt.max() * 1000, amt[amt > 1e-4].mean() * 1000))

def main(src_dir, out_dir, mm=6.0, mmx=12.0, mma=4.0, mmb=3.0):
    """mm = torso (pecho), mmb = espalda, mmx = axila (en la parte de abajo del anillo del brazo, 5 cm alrededor), mma = brazo alto"""
    os.makedirs(out_dir, exist_ok=True)
    seam = None   # desplazamiento del anillo del brazo (por posicion), igual en todos los torsos: el brazo lo copia
    for fn in TORSO:
        f = os.path.join(src_dir, fn + ".fmdl")
        if not os.path.exists(f): continue
        T = load_parts(src_dir, [(fn, i) for i in (1, 2, 3)])
        Pw, b = T["Pw"], T["boundary"]
        sa = arm_seam(Pw, b)
        fixed = np.setdiff1d(b, sa)                 # cuello y short quietos; el anillo del brazo se mueve con el brazo
        d = geodesic_from(Pw, T["edges"], fixed)
        # axila: el punto mas bajo del anillo del brazo de cada lado
        ax = []
        for sgn in (-1, 1):
            k = sa[np.sign(Pw[sa, 0]) == sgn]
            if len(k): ax.append(Pw[k[np.argmin(Pw[k, 1])]])
        wx = np.zeros(len(Pw))
        for a in ax: wx = np.maximum(wx, np.exp(-0.5 * np.sum((Pw - a) ** 2, 1) / 0.025 ** 2))
        belly = ss((Pw[:, 1] - PANZA[0]) / (PANZA[1] - PANZA[0]))   # (31/10 b, Agustin) la panza queda como esta: se ve al saltar
        N, Ns = smooth_normals(T)
        front = ss((Ns[:, 2] + 0.3) / 0.6)                          # (31/10 b, Agustin) la espalda no tanto: mmb atras, mm adelante
        base = mmb + (mm - mmb) * front
        amt = (base + (mmx - base) * wx) * 0.001 * ss(d / FADE) * belly
        D = -Ns * amt[:, None]
        if seam is None: seam = (Pw[sa].copy(), D[sa].copy())
        write_file(f, os.path.join(out_dir, fn + ".fmdl"), T, Pw + D, N, fn, amt)
    if seam is None:
        print("sin torso en %s: los brazos quedan igual (el anillo del hombro tiene que moverse junto con el torso)" % src_dir); return
    tree = cKDTree(seam[0])
    for fn in ARMS:
        f = os.path.join(src_dir, fn + ".fmdl")
        if not os.path.exists(f): continue
        n = len(meshes(Fmdl(f))); out = os.path.join(out_dir, fn + ".fmdl"); cur = f
        for k in range(n):   # cada malla sola (arm trae dos casi iguales, una por material: soldadas juntas no tienen borde)
            arm_mesh(src_dir, fn, k, cur, out, tree, seam, mma); cur = out

def arm_mesh(src_dir, fn, k, cur, out, tree, seam, mma):
        T = load_parts(src_dir, [(fn, k)])
        Pw, b = T["Pw"], T["boundary"]
        sh = arm_seam(Pw, b)
        if len(sh) == 0: print("%s %d: sin anillo del hombro (antebrazo), queda igual" % (fn, k)); shutil.copyfile(cur, out) if cur != out else None; return
        dd, ii = tree.query(Pw[sh])
        d = geodesic_from(Pw, T["edges"], sh)
        N, Ns = smooth_normals(T)
        own = mma * 0.001 * (1 - ss((d - LARGO) / 0.07))
        D = -Ns * own[:, None]
        # cerca del hombro: el desplazamiento del anillo del torso mas cercano (por la malla), fundido hacia el propio en FADE
        near = cKDTree(Pw[sh]).query(Pw)[1]
        Dseam = seam[1][ii][near]
        t = ss(d / FADE)[:, None]
        D = Dseam * (1 - t) + D * t
        amt = np.linalg.norm(D, axis=1)
        if len(sh) == 0 or dd.max() > 0.001: print("  OJO %s %d: anillo del hombro (%d) a %.1f mm del torso" % (fn, k, len(sh), dd.max() * 1000 if len(sh) else -1))
        write_file(cur, out, T, Pw + D, N, "%s %d" % (fn, k), amt)

if __name__ == "__main__":
    a = sys.argv[1:]
    main(a[0], a[1], *(float(x) for x in a[2:6]))
