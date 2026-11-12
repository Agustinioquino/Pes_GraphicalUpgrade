import sys, re
import match2 as M2
from match2 import A, B, insns, pattern, CB, CD, delta_near
for arg in sys.argv[1:]:
    a = int(arg, 16); dp, dn = delta_near(a)
    lst = insns(A, a, a + 96); w = []
    for x in lst:
        w.append(x)
        if x[1] in (b'\xc3',) or x[1][0] in (0xe9, 0xc2): break
    pat = pattern(w); n = sum(len(x[1]) for x in w)
    lo = a + min(dp, dn) - 0x300; hi = a + max(dp, dn) + 0x300
    c = [CB + m.start() for m in pat.finditer(CD, lo - CB, hi - CB)]
    print(hex(a), 'bytes', n, 'deltas', hex(dp), hex(dn), 'cand', [(hex(x), hex(x - a)) for x in c])
