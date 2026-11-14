# PES21 FX (31/10) - CUELLO DE LAS CARAS GENERICAS: el neck.fmdl de neck_konami (26/10 h) quedo soldado al anillo nuevo del torso
# (y 1.508) pero al frente / costado, a y 1.50 .. 1.55, 3 .. 9 mm mas adentro que el neck.fmdl de Konami (con el que se modelo la
# cabeza generica, que baja hasta el collar y se superpone): el cuello de las genericas se veia chupado (tambien con Ver original).
# Aca cada vertice del cuello que queda ADENTRO de la superficie vieja va a su punto mas cercano (triangulos), con 0 en el anillo
# (fundido en 6 mm hacia arriba: la soldadura con el torso no se mueve) y solo debajo de y 1.62. Normales / tangentes giradas.
# Uso: python3 neck_generico.py <neck.fmdl actual> <neck.fmdl de Konami> <carpeta common (pants_out_sub)> <salida.fmdl>
# El resultado es un modelo de TorsoBody / Konami: se arma en la PC del usuario (o en scratch), no va al repo.
import sys, os
from collections import defaultdict
import numpy as np
from scipy.spatial import cKDTree
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'pelvis')]
from fmdl import Fmdl, meshes
from torso_mesh import load_torso, vertex_normals, rotate_min
from pelvis_fmdl import write_mesh_geom

def closest_tri(p, A, B, C):
    ab, ac, ap = B - A, C - A, p - A
    d1, d2 = ab @ ap, ac @ ap
    if d1 <= 0 and d2 <= 0: return A
    bp = p - B; d3, d4 = ab @ bp, ac @ bp
    if d3 >= 0 and d4 <= d3: return B
    vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0: return A + ab * (d1 / (d1 - d3))
    cp = p - C; d5, d6 = ab @ cp, ac @ cp
    if d6 >= 0 and d5 <= d6: return C
    vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0: return A + ac * (d2 / (d2 - d6))
    va = d3 * d6 - d5 * d4
    if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0: return B + (C - B) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)))
    den = 1 / (va + vb + vc); return A + ab * vb * den + ac * vc * den

def main(src, konami, common, out):
    K = meshes(Fmdl(konami)); a = np.concatenate([m['pos'] for m in K]).astype(float)
    an = np.concatenate([m['nrm'][:, :3] for m in K]).astype(float); an /= np.linalg.norm(an, axis=1, keepdims=True)
    offs = np.cumsum([0] + [m['nvert'] for m in K])
    Fa = np.concatenate([m['faces'] + offs[i] for i, m in enumerate(K)])
    m = meshes(Fmdl(src))[0]; P = m['pos'].astype(float); F = m['faces']
    T = load_torso(common, 'out'); Pw = T['Pw']; b = T['boundary']; R = Pw[b[Pw[b, 1] > 1.42]]
    phi = lambda X: np.degrees(np.arctan2(X[:, 0], X[:, 2] - 0.045))
    pr = phi(R); o = np.argsort(pr); pr = pr[o]; ry = R[o, 1]
    h = P[:, 1] - np.interp(phi(P), np.r_[pr - 360, pr, pr + 360], np.r_[ry, ry, ry])
    vf = defaultdict(list)
    for fi, f in enumerate(Fa):
        for v in f: vf[v].append(fi)
    t = cKDTree(a); Q = P.copy(); inside = np.zeros(len(P), bool)
    for i, p in enumerate(P):
        best = (1e9, None, None)
        for j in t.query(p, 8)[1]:
            for fi in vf[j]:
                q = closest_tri(p, *a[Fa[fi]]); d = np.linalg.norm(p - q)
                if d < best[0]: best = (d, q, fi)
        _, q, fi = best; nn = an[Fa[fi]].mean(0)
        Q[i] = q; inside[i] = (p - q) @ nn < 0
    w = np.clip(h / 0.006, 0, 1); w = w * w * (3 - 2 * w); w *= (P[:, 1] < 1.62) & inside
    P2 = P + (Q - P) * w[:, None]
    N0 = vertex_normals(P, F); N1 = vertex_normals(P2, F)
    nrm = m['nrm'].astype(float).copy(); nrm[:, :3] = rotate_min(N0, N1, nrm[:, :3])
    tan = m['tan'].astype(float).copy(); tan[:, :3] = rotate_min(N0, N1, tan[:, :3])
    write_mesh_geom(src, out, 0, P2, nrm, tan)
    mv = np.linalg.norm(P2 - P, axis=1) * 1000
    print('neck.fmdl: %d vertices, se mueven %d (maximo %.1f mm), anillo %.3f mm' % (len(P), (mv > 0.1).sum(), mv.max(), mv[h < 0.0005].max()))

if __name__ == '__main__':
    main(*sys.argv[1:5])
