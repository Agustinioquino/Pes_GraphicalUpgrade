import json, struct, collections, sys, bisect
import numpy as np
import match3 as M3
from match3 import A, B, CA, CBm, IA, IB, map_vt, vt_len
from match2 import map_data, map_code
RB, RD, _ = A.secs[1]
Q = np.frombuffer(RD[:len(RD)//8*8], dtype='<u8')
VA = sorted(CA)
def vtslots(a):
    """(vtable, slot) de 1.07 cuya entrada apunta a 'a' -> valores de la misma ranura en 1.00"""
    votes = collections.Counter(); n = 0
    for i in np.nonzero(Q == a)[0]:
        p = RB + int(i) * 8; j = bisect.bisect_right(VA, p) - 1
        if j < 0: continue
        v = VA[j]
        if p >= v + 8 * vt_len(A, v, CA): continue
        r = map_vt(p)
        if not r or not r[0]: continue
        votes[B.u64(r[0])] += 1; n += 1
    return votes, n
if __name__ == '__main__':
    res = json.load(open('map4.json'))
    for k in sys.argv[1:]:
        a = int(k, 16)
        v, n, u = map_data(a, 30)
        w, m = vtslots(a)
        print(k, 'llamadas:', {hex(x): c for x, c in v.items()}, '(%d refs, %d resueltas)' % (n, u), '| ranuras de vtable:', {hex(x): c for x, c in w.most_common(4)}, m)
