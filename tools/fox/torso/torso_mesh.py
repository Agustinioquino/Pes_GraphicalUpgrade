# PES21 FX (26/10) - variantes del torso de TorsoBody: la malla del torso (pants_out_sub / pants_in_sub, mallas 1..3) soldada
# por posicion, sus bordes (costuras con brazos, cuello y short) y utilidades de malla (vecinos, suavizado, normales).
import sys, os, heapq
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
from fmdl import Fmdl, meshes

TORSO_MESHES = (1, 2, 3)   # izquierda, derecha, abajo (la 0 es el short)

TORSO_FILES = {"out": "pants_out_sub", "in": "pants_in_sub", "inner": "pants_out_inner_sub", "tight": "pants_out_sub_tight"}

def load_torso(folder, version="out"):
    """version: 'out' (pants_out_sub, camiseta afuera), 'in' (pants_in_sub), 'inner', 'tight'. Devuelve las mallas y la soldadura."""
    fn = TORSO_FILES.get(version, version)
    T = load_parts(folder, [(fn, i) for i in TORSO_MESHES])
    T["version"] = version
    return T

def load_parts(folder, specs):
    """specs: lista de (archivo fmdl sin extension, indice de malla). Suelda todas por posicion (0.1 mm)."""
    cache = {}
    parts = []
    P, F, off = [], [], 0
    for fn, i in specs:
        if fn not in cache: cache[fn] = meshes(Fmdl(os.path.join(folder, fn + ".fmdl")))
        m = cache[fn][i]
        parts.append(dict(file=fn, index=i, nvert=m["nvert"], off=off, pos=m["pos"].astype(np.float64), nrm=m["nrm"][:, :3].astype(np.float64),
                          tan=m["tan"].astype(np.float64), uv=m["uv"].astype(np.float64), faces=m["faces"], nidx=3 * len(m["faces"])))
        P.append(m["pos"]); F.append(m["faces"] + off); off += m["nvert"]
    P = np.concatenate(P).astype(np.float64); F = np.concatenate(F)
    q = np.round(P / 1e-4).astype(np.int64)
    _, uid, inv = np.unique(q, axis=0, return_index=True, return_inverse=True)
    inv = inv.ravel()
    Pw = P[uid]; Fw = inv[F]
    Fw = Fw[(Fw[:, 0] != Fw[:, 1]) & (Fw[:, 1] != Fw[:, 2]) & (Fw[:, 2] != Fw[:, 0])]
    E = np.sort(np.concatenate([Fw[:, [0, 1]], Fw[:, [1, 2]], Fw[:, [2, 0]]]), 1)
    k, cnt = np.unique(E, axis=0, return_counts=True)
    edges = k
    bnd = np.unique(k[cnt == 1])
    return dict(version="", parts=parts, P=P, inv=inv, Pw=Pw, Fw=Fw, edges=edges, boundary=bnd)

def neighbors(n, edges):
    nb = [[] for _ in range(n)]
    for a, b in edges: nb[a].append(b); nb[b].append(a)
    return nb

def geodesic_from(Pw, edges, seeds):
    """distancia por las aristas desde los vertices semilla (Dijkstra)"""
    nb = neighbors(len(Pw), edges)
    d = np.full(len(Pw), np.inf); h = []
    for s in seeds: d[s] = 0.0; h.append((0.0, int(s)))
    heapq.heapify(h)
    while h:
        dd, a = heapq.heappop(h)
        if dd > d[a]: continue
        for b in nb[a]:
            nd = dd + float(np.linalg.norm(Pw[a] - Pw[b]))
            if nd < d[b]: d[b] = nd; heapq.heappush(h, (nd, b))
    return d

def umbrella_matrix(n, edges):
    """L x = promedio de los vecinos - x (matriz rala)"""
    import scipy.sparse as sp
    r = np.concatenate([edges[:, 0], edges[:, 1]]); c = np.concatenate([edges[:, 1], edges[:, 0]])
    A = sp.csr_matrix((np.ones(len(r)), (r, c)), shape=(n, n))
    deg = np.asarray(A.sum(1)).ravel(); deg[deg == 0] = 1
    W = sp.diags(1.0 / deg) @ A
    return W

def taubin(P, edges, fixed, iters=40, lam=0.5, mu=-0.53):
    W = umbrella_matrix(len(P), edges)
    X = P.copy(); free = np.ones(len(P), bool); free[fixed] = False
    for _ in range(iters):
        for k in (lam, mu):
            L = W @ X - X
            X[free] += k * L[free]
    return X

def vertex_areas(P, F):
    a = 0.5 * np.linalg.norm(np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]]), axis=1)
    A = np.zeros(len(P))
    for k in range(3): np.add.at(A, F[:, k], a / 3.0)
    return A

def gauss_matrix(P, F, sigma):
    """promedio gaussiano por distancia en 3D (pesado por el area de cada vertice): no depende de como esta triangulada la
    malla (el promedio de vecinos por aristas deja rayas en las mallas refinadas de la v12.6: valencias distintas)"""
    import scipy.sparse as sp
    from scipy.spatial import cKDTree
    A = vertex_areas(P, F)
    tree = cKDTree(P)
    pairs = tree.query_pairs(3.0 * sigma, output_type='ndarray')
    i = np.concatenate([pairs[:, 0], pairs[:, 1], np.arange(len(P))])
    j = np.concatenate([pairs[:, 1], pairs[:, 0], np.arange(len(P))])
    d2 = np.sum((P[i] - P[j]) ** 2, 1)
    w = np.exp(-0.5 * d2 / (sigma * sigma)) * A[j]
    W = sp.csr_matrix((w, (i, j)), shape=(len(P), len(P)))
    s = np.asarray(W.sum(1)).ravel(); s[s == 0] = 1
    return sp.diags(1.0 / s) @ W

def vertex_normals(P, F):
    n = np.zeros_like(P)
    fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    for k in range(3): np.add.at(n, F[:, k], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)

def rotate_min(a, b, v):
    """aplica a v la rotacion minima que lleva el unitario a al unitario b (por filas)"""
    c = np.cross(a, b); s = np.linalg.norm(c, axis=1, keepdims=True); co = np.sum(a * b, 1, keepdims=True)
    k = c / np.maximum(s, 1e-12)
    # Rodrigues: v cos + (k x v) sin + k (k . v)(1 - cos)
    out = v * co + np.cross(k, v) * s + k * np.sum(k * v, 1, keepdims=True) * (1 - co)
    return np.where(s > 1e-9, out, v)
