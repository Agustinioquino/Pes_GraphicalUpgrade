# PES21 FX (31/10 b) - RENDIJA EN LA NUCA DE LAS CARAS GENERICAS (linea en U, fondo azul a los lados de la nuca, con y sin Ver
# original): el neck.fmdl de TorsoBody (el de neck_konami / neck_generico) termina atras a y 1.58 y el neck.fmdl de Konami, con el
# que se modelaron las cabezas genericas, sube hasta 1.617: el borde de la cabeza generica queda por encima del cuello. Aca la
# parte de atras del cuello (|angulo| > 90 grados, 0 = adelante) se estira hasta la altura de Konami en ese angulo (el anillo con
# el torso no se mueve) y, subiendo, toma el radio de la superficie de Konami menos 0.5 mm (queda adentro de la cabeza).
# Uso: python3 neck_nuca.py <neck.fmdl actual> <neck.fmdl de Konami> <salida.fmdl>
# El resultado es un modelo de TorsoBody / Konami: se arma en la PC del usuario (o en scratch), no va al repo.
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'pelvis')]
from fmdl import Fmdl, meshes
from torso_mesh import vertex_normals, rotate_min
from pelvis_fmdl import write_mesh_geom

C = np.array([0.0, 0.045])   # eje del cuello (x, z)
def ss(x): x = np.clip(x, 0, 1); return x * x * (3 - 2 * x)
def ang(P): return np.degrees(np.arctan2(P[:, 0], P[:, 2] - C[1]))
def rad(P): return np.hypot(P[:, 0] - C[0], P[:, 2] - C[1])

def per_angle(phi, val, fn, step=5, win=12):
    g = np.arange(-180, 180, step); out = []
    for a in g:
        d = np.abs((phi - a + 180) % 360 - 180)
        v = val[d < win]; out.append(fn(v) if len(v) else np.nan)
    out = np.array(out); ok = ~np.isnan(out)
    return lambda x: np.interp(x, np.r_[g[ok] - 360, g[ok], g[ok] + 360], np.r_[out[ok], out[ok], out[ok]])

def main(src, konami, out):
    K = meshes(Fmdl(konami)); A = np.concatenate([m['pos'] for m in K]).astype(float)
    m = meshes(Fmdl(src))[0]; P = m['pos'].astype(float); F = m['faces']
    phi = ang(P); pa = ang(A)
    ktop = per_angle(pa, A[:, 1], np.max); ctop = per_angle(phi, P[:, 1], np.max); cbot = per_angle(phi, P[:, 1], np.min)
    back = ss((np.abs(phi) - 90) / 30)
    yb, yt, yk = cbot(phi), ctop(phi), ktop(phi)
    s = np.where(yk > yt + 0.001, (yk - yb) / np.maximum(yt - yb, 1e-4), 1.0)
    y2 = yb + (P[:, 1] - yb) * (1 + back * (s - 1))
    Q = P.copy(); Q[:, 1] = y2
    # radio de Konami en (angulo, altura): interpolacion lineal sobre sus vertices (angulo en metros de arco a 5 cm, periodico)
    from scipy.interpolate import LinearNDInterpolator, NearestNDInterpolator
    X = np.concatenate([np.stack([(pa + o) * np.pi / 180 * 0.05, A[:, 1]], 1) for o in (-360, 0, 360)])
    Rv = np.tile(rad(A), 3)
    lin = LinearNDInterpolator(X, Rv); near = NearestNDInterpolator(X, Rv)
    q = np.stack([phi * np.pi / 180 * 0.05, y2], 1)
    rk = lin(q); bad = np.isnan(rk); rk[bad] = near(q[bad])
    r_new = rk - 0.0005
    w = back * ss((y2 - yb) / 0.015) * (s > 1.001)
    r = rad(P) + (r_new - rad(P)) * w
    dirv = np.stack([P[:, 0] - C[0], P[:, 2] - C[1]], 1); dirv /= np.maximum(np.linalg.norm(dirv, axis=1, keepdims=True), 1e-9)
    Q[:, 0] = C[0] + dirv[:, 0] * r; Q[:, 2] = C[1] + dirv[:, 1] * r
    N0 = vertex_normals(P, F); N1 = vertex_normals(Q, F)
    nrm = m['nrm'].astype(float).copy(); nrm[:, :3] = rotate_min(N0, N1, nrm[:, :3])
    tan = m['tan'].astype(float).copy(); tan[:, :3] = rotate_min(N0, N1, tan[:, :3])
    write_mesh_geom(src, out, 0, Q, nrm, tan)
    mv = np.linalg.norm(Q - P, axis=1) * 1000
    print('neck.fmdl: %d vertices, se mueven %d (maximo %.1f mm), anillo %.3f mm, arriba atras %.3f -> %.3f m' %
          (len(P), (mv > 0.1).sum(), mv.max(), mv[np.abs(P[:, 1] - yb) < 0.0005].max(), P[np.abs(phi) > 150, 1].max(), Q[np.abs(phi) > 150, 1].max()))

if __name__ == '__main__':
    main(*sys.argv[1:4])
