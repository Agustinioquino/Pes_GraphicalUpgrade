import json, pickle, collections, bisect, sys
import match3 as M3
from match3 import A, B, CA, CBm, map_vt, vt_len
HA = pickle.load(open('hashA.pkl', 'rb')); HB = pickle.load(open('hashB.pkl', 'rb'))
cb = collections.Counter(h for h in HB if h); gf = pickle.load(open('gfuncs.pkl', 'rb'))
res = json.load(open('map5.json')); ctx = json.load(open('/tmp/claude-0/-home-user/b8b9e95d-3ea7-5485-9fdd-79666368441f/scratchpad/addrs.json'))
MAP = {int(k, 16): int(v[0], 16) for k, v in res.items() if v[0]}
for k, v in res.items():
    a = int(k, 16)
    if a not in CA: continue
    n = vt_len(A, a, CA); b = MAP[a]; nb = vt_len(B, b, CBm)
    st = collections.Counter(); notes = []
    for i in range(n):
        fa = A.u64(a + 8 * i); fb = B.u64(b + 8 * i)
        if fa in MAP and MAP[fa] != fb: notes.append('RANURA %d: %#x esperado %#x pero la tabla de 1.00 tiene %#x' % (i, fa, MAP[fa], fb))
        j = bisect.bisect_right(A.fb, fa) - 1
        if j < 0 or A.fb[j] != fa: st['hoja/sin-pdata'] += 1; continue
        if HA[j] and HA[j] in cb: st['identica'] += 1
        elif fa in gf:
            sb, num, other, nn = gf[fa]; st['distinta'] += 1
            notes.append('ranura %d %#x: %s otras %d' % (i, fa, sorted(set((hex(x), hex(y)) for x, y, _ in num))[:5], other))
        else: st['sin-pareja'] += 1; notes.append('ranura %d %#x sin pareja' % (i, fa))
    print(k, CA[a][0][:60], 'ranuras', n, nb, dict(st), sorted(set(f for f, _ in ctx[k])))
    for x in notes[:8]: print('     ', x)
