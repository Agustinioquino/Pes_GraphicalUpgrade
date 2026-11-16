# Ropa 16/11 (d9ybe2): genera la grilla de tela libre de la campera para patch_cloth_sim_campera.py (lista GRID).
# Entrada: campera_mallas.npz (hilo pechera: malla{i}_pos n x 3 en metros, pose de armado, y arriba, z adelante; malla{i}_idx).
# El cuerpo simulado es la malla 4. Una celda de 2 cm es libre si su centro (y su espejo en x) esta a mas de TH de la superficie
# de las otras mallas (0, 1, 2, 3, 5, 6; triangulos muestreados cada 4 mm) y |x| < 0.19 (sin mangas).
# Uso: python -I gen_campera_mascara.py campera_mallas.npz > grid_words.txt   (necesita numpy y scipy)
import sys
import numpy as np
from scipy.spatial import cKDTree
z = np.load(sys.argv[1])
P = {i: z['malla%d_pos' % i].astype(np.float64) for i in range(7)}
I = {i: z['malla%d_idx' % i] for i in range(7)}
def sample(i, step=0.004):
    pos = P[i]; out = [pos]
    for tr in I[i].reshape(-1, 3):
        a, b, c = pos[tr]
        n = int(np.ceil(max(np.linalg.norm(b - a), np.linalg.norm(c - a), np.linalg.norm(c - b)) / step))
        if n < 2: continue
        u, v = np.meshgrid(np.arange(n + 1), np.arange(n + 1)); m = (u + v) <= n
        out.append(a + np.outer(u[m] / n, b - a) + np.outer(v[m] / n, c - a))
    return np.concatenate(out)
T = cKDTree(np.concatenate([sample(i) for i in (0, 1, 2, 3, 5, 6)]))
X0, Y0, Z0, C = -0.38, 0.95, -0.13, 0.02
NX, NY, NZ = 39, 35, 17
TH = 0.035
G = np.stack(np.meshgrid(X0 + C * np.arange(NX), Y0 + C * np.arange(NY), Z0 + C * np.arange(NZ), indexing='ij'), -1).reshape(-1, 3)
d1, _ = T.query(G); d2, _ = T.query(G * np.array([-1, 1, 1]))
bits = ((d1 > TH) & (d2 > TH) & (np.abs(G[:, 0]) < 0.19)).astype(np.uint32)   # indice = (ix*NY + iy)*NZ + iz
words = [0] * ((len(bits) + 31) // 32)
for i in np.nonzero(bits)[0]: words[i >> 5] |= 1 << (int(i) & 31)
print(','.join('0x%08x' % w for w in words))
