# para cada direccion de codigo ya ubicada: compara la funcion entera de 1.07 con la de 1.00 (instruccion por instruccion, enmascarando desplazamientos)
import json, sys
import match as M
from match import A, B, insns
res = json.load(open('map5.json')); ctx = json.load(open('/tmp/claude-0/-home-user/b8b9e95d-3ea7-5485-9fdd-79666368441f/scratchpad/addrs.json'))
def masked(lst): return [bytes(x if k else 0 for x, k in zip(b, m)) for _, b, m in lst]
out = {}
for k, v in res.items():
    a = int(k, 16)
    if not v[0] or A.sec(a) is None or A.sec(a)[2] != '.text': continue
    b = int(v[0], 16); fa = A.func(a); fb = B.func(b)
    if not fa or not fb: out[k] = ('sin-pdata', ''); continue
    if a - fa[0] != b - fb[0]: out[k] = ('DESFASE', 'offset en la funcion %#x vs %#x' % (a - fa[0], b - fb[0])); continue
    la = masked(insns(A, fa[0], fa[1])); lb = masked(insns(B, fb[0], fb[1]))
    if la == lb: out[k] = ('identica', '%d bytes' % (fa[1] - fa[0])); continue
    # el volcado 1.07 puede tener un gancho en los primeros bytes: comparar desde el byte 16
    ra = A.read(fa[0], fa[1] - fa[0]); rb = B.read(fb[0], fb[1] - fb[0])
    nd = sum(1 for x, y in zip(la, lb) if x != y)
    out[k] = ('DISTINTA', 'tam %d vs %d, %d de %d instrucciones distintas, primera en +%#x' % (fa[1] - fa[0], fb[1] - fb[0], nd + abs(len(la) - len(lb)), len(la), next((sum(len(x) for x in la[:i]) for i, (x, y) in enumerate(zip(la, lb)) if x != y), -1)))
import collections
print(collections.Counter(v[0] for v in out.values()))
for k, v in out.items():
    if v[0] != 'identica': print(k, res[k][0], v, sorted(set(f for f, _ in ctx[k])))
json.dump(out, open('verify.json', 'w'), indent=0)
