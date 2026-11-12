import pickle, collections, bisect, struct, sys, json
import match as M
from match import A, B, insns
import capstone
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
HA = pickle.load(open('hashA.pkl', 'rb')); HB = pickle.load(open('hashB.pkl', 'rb'))
ca = collections.Counter(h for h in HA if h); cb = collections.Counter(h for h in HB if h)
ib = {h: i for i, h in enumerate(HB) if h and cb[h] == 1}
pair = {}   # indice A -> indice B (hash unico en los dos)
for i, h in enumerate(HA):
    if h and ca[h] == 1 and h in ib: pair[i] = ib[h]
print('funciones', len(HA), len(HB), 'parejas unicas', len(pair))
# sin pareja: entre vecinas con pareja, mismo orden
ka = sorted(pair); un = [i for i in range(len(HA)) if i not in pair]
print('sin pareja unica en 1.07:', len(un), '(hash repetido: %d)' % sum(1 for i in un if HA[i] and ca[HA[i]] > 1 and HA[i] in cb))
cand = []
for i in un:
    if HA[i] and HA[i] in cb: continue   # identica pero repetida (ICF / plantillas)
    p = bisect.bisect_left(ka, i)
    if p == 0 or p >= len(ka): continue
    i0, i1 = ka[p - 1], ka[p]; j0, j1 = pair[i0], pair[i1]
    if i1 - i0 == j1 - j0 and j1 > j0:      # mismo numero de funciones entre las dos anclas
        cand.append((i, j0 + (i - i0)))
print('con vecina en la misma posicion:', len(cand))
def one(b, a):
    for x in md.disasm(b, a): return '%s %s' % (x.mnemonic, x.op_str)
changes = collections.Counter(); detail = []
nsame = ndiff = nsize = 0
for i, j in cand:
    sa, ea = A.fb[i], A.fe[i]; sb, eb = B.fb[j], B.fe[j]
    if ea - sa != eb - sb: nsize += 1; detail.append((sa, sb, 'TAM', ea - sa, eb - sb)); continue
    la = insns(A, sa, ea); lb = insns(B, sb, eb); d = []
    for (xa, xb, xm), (ya, yb, ym) in zip(la, lb):
        if bytes(p if k else 0 for p, k in zip(xb, xm)) != bytes(p if k else 0 for p, k in zip(yb, ym)): d.append((xa - sa, one(xb, xa), one(yb, ya)))
    if not d: nsame += 1
    else: ndiff += 1; detail.append((sa, sb, 'INS', d))
print('iguales', nsame, 'distintas (mismo tamano)', ndiff, 'otro tamano', nsize)
pickle.dump((pair, cand, detail), open('gdiff.pkl', 'wb'))
