# velocidad y aceleracion del giro del pecho / columna / cadera (mundo) por cuadro, para hallar saltos
import sys, math
sys.path.insert(0, '/home/user/Pes_GraphicalUpgrade/tools')
import gani_cuerpo as G
fn = sys.argv[1]; f0 = int(sys.argv[2]); f1 = int(sys.argv[3])
b = G.Base(open(fn, 'rb').read())
P = [G.fk(b.rot[f], b.root[f], b.hips[f]) for f in range(b.n + 1)]
def ang(q0, q1): return G.qangle(G.qm(G.qi(q0), q1))
for nm in sys.argv[4:]:
    v = [0.0] + [ang(P[f - 1][nm][1], P[f][nm][1]) for f in range(1, b.n + 1)]
    # aceleracion como vector: giro relativo entre dos pasos seguidos
    def paso(f): return G.qm(G.qi(P[f - 1][nm][1]), P[f][nm][1])
    a = [0.0, 0.0] + [ang(paso(f - 1), paso(f)) for f in range(2, b.n + 1)]
    pos = [P[f][nm][0] for f in range(b.n + 1)]
    pa = [0.0, 0.0] + [1000 * G.norm(G.sub(G.add(pos[f], pos[f - 2]), G.mul(pos[f - 1], 2.0))) for f in range(2, b.n + 1)]
    print(nm, 'vel grados/cuadro max %.2f media %.2f | acel giro max %.2f media %.2f | acel lugar mm max %.1f media %.1f' % (
        max(v[f0:f1]), sum(v[f0:f1]) / (f1 - f0), max(a[f0:f1]), sum(a[f0:f1]) / (f1 - f0), max(pa[f0:f1]), sum(pa[f0:f1]) / (f1 - f0)))
    top = sorted(range(f0, f1), key=lambda f: -a[f])[:14]
    print('   picos de acel giro:', ' '.join('%d:%.2f' % (f, a[f]) for f in sorted(top)))
    top = sorted(range(f0, f1), key=lambda f: -pa[f])[:14]
    print('   picos de acel lugar:', ' '.join('%d:%.1f' % (f, pa[f]) for f in sorted(top)))
