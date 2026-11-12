# direcciones citadas en los COMENTARIOS de cada modulo (de donde se leyeron los offsets): estan iguales en 1.00?
import re, glob, json, pickle, collections, sys, bisect
import match as M
from match import A, B, insns
pair, cand, detail = pickle.load(open('gdiff.pkl', 'rb'))
HA = pickle.load(open('hashA.pkl', 'rb')); HB = pickle.load(open('hashB.pkl', 'rb'))
cb = collections.Counter(h for h in HB if h)
gf = pickle.load(open('gfuncs.pkl', 'rb'))
SRC = '/home/user/repo/src/'
tot = collections.Counter(); bad = collections.defaultdict(list)
for f in sorted(glob.glob(SRC + '*.cpp')):
    t = open(f, encoding='utf-8', errors='replace').read(); fn = f.split('/')[-1]
    for a in sorted(set(int(x, 16) for x in re.findall(r'0x14[0-9a-fA-F]{7}(?![0-9a-fA-F])', t))):
        s = A.sec(a)
        if not s or s[2] != '.text': continue
        i = bisect.bisect_right(A.fb, a) - 1
        if i < 0 or not (A.fb[i] <= a < A.fe[i]): tot[fn, 'sin-pdata'] += 1; continue
        h = HA[i]
        if h and h in cb: tot[fn, 'identica'] += 1
        elif A.fb[i] in gf:
            sb, num, other, n = gf[A.fb[i]]
            tot[fn, 'distinta'] += 1; bad[fn].append((hex(a), hex(A.fb[i]), hex(sb), [(hex(x), hex(y)) for x, y, _ in num][:6], other))
        else: tot[fn, 'sin-pareja'] += 1; bad[fn].append((hex(a), hex(A.fb[i]), None, None, None))
files = sorted(set(k[0] for k in tot))
for fn in files:
    print(fn, {k[1]: v for k, v in tot.items() if k[0] == fn})
    for b in bad[fn][:14]: print('    ', b)
