# PES21 FX (31/10 b) - CARAS GENERICAS UNIDAS COMO EN EL JUEGO: Agustin pidio que las genericas se unan como por defecto en vez
# de seguir estirando el cuello de TorsoBody. Aca va el neck.fmdl de Konami tal cual (el del common_package de
# livecpk\TorsoBody\Asset\model\#Win, con el que se modelaron las cabezas genericas; material skin_head = textura de la cabeza),
# salvo su parte de abajo: Konami lo hacia bajar hasta y 1.385 adentro de su pecho, y el pecho de TorsoBody achicado queda por
# dentro de ese cuello (hasta 5 mm). Los vertices que quedan afuera del torso se meten hasta 1 mm adentro, solo debajo de y 1.53
# (fundido 2 cm hacia arriba): lo que se ve con la cabeza encima queda exacto como en el juego.
# Uso: python3 neck_default.py <neck.fmdl de Konami> <carpeta con pants_out_sub.fmdl (el torso instalado)> <salida.fmdl>
import sys, os
import numpy as np
from scipy.spatial import cKDTree
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'pelvis')]
from fmdl import Fmdl, meshes
from torso_mesh import load_parts, vertex_normals, rotate_min
from pelvis_fmdl import write_mesh_geom

def ss(x): x = np.clip(x, 0, 1); return x * x * (3 - 2 * x)

def main(konami, torso_dir, out):
    T = load_parts(torso_dir, [("pants_out_sub", i) for i in (1, 2, 3)])
    Pw = T["Pw"]; fn = np.concatenate([p["nrm"][:, :3] for p in T["parts"]])
    N = np.zeros_like(Pw); np.add.at(N, T["inv"], fn); N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
    tree = cKDTree(Pw)
    M = meshes(Fmdl(konami)); cur = konami; tot = 0; mx = 0.0
    for k, m in enumerate(M):
        P = m["pos"].astype(float); F = m["faces"]
        d, i = tree.query(P, 4)
        q = Pw[i].mean(1); n = N[i].mean(1); n /= np.linalg.norm(n, axis=1, keepdims=True)
        s = np.sum((P - q) * n, 1)                   # > 0: afuera del torso
        w = 1 - ss((P[:, 1] - 1.53) / 0.02)
        push = np.clip(s + 0.001, 0, None) * w * (d.min(1) < 0.03)
        Q = P - n * push[:, None]
        N0 = vertex_normals(P, F); N1 = vertex_normals(Q, F)
        nrm = m["nrm"].astype(float).copy(); nrm[:, :3] = rotate_min(N0, N1, nrm[:, :3])
        tan = m["tan"].astype(float).copy(); tan[:, :3] = rotate_min(N0, N1, tan[:, :3])
        write_mesh_geom(cur, out, k, Q, nrm, tan); cur = out
        tot += (push > 1e-4).sum(); mx = max(mx, push.max())
    print("neck de Konami: %d mallas, %d vertices metidos adentro del torso (maximo %.1f mm)" % (len(M), tot, mx * 1000))

if __name__ == "__main__":
    main(*sys.argv[1:4])
