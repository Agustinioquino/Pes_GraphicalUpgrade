import json, struct, collections, sys
import match3 as M3
from match3 import A, B, IA, IB, CA, CBm, map_vt, insns
BASE = 0x140000000
def col_index(im):
    rb, rd, _ = im.secs[1]; out = {}; byname = collections.defaultdict(list)
    import numpy as np
    q = np.frombuffer(rd[:len(rd)//8*8], dtype='<u8')
    for i in np.nonzero((q >= rb) & (q < rb + len(rd)))[0]:
        col = int(q[i]); h = im.read(col, 24)
        if not h or len(h) < 24: continue
        sig, off, cd, td, ch, self_ = struct.unpack('<6I', h)
        if sig != 1 or self_ != col - BASE: continue
        nm = im.read(BASE + td + 16, 200)
        if nm is None or not nm.startswith(b'.?A'): continue
        nm = nm.split(b'\0')[0].decode('latin1')
        if col not in out: out[col] = (nm, off); byname[(nm, off)].append(col)
    return out, byname
CA_, NA = col_index(A); CB_, NB = col_index(B)
res = json.load(open('map3.json')); ctx = json.load(open('/tmp/claude-0/-home-user/b8b9e95d-3ea7-5485-9fdd-79666368441f/scratchpad/addrs.json'))
for k, v in res.items():
    a = int(k, 16)
    if a in CA_:
        key = CA_[a]; la, lb = NA[key], NB.get(key, [])
        if len(la) == len(lb) and lb:
            x = lb[la.index(a)]
            if v[0] and int(v[0], 16) != x: print('CONFLICTO col', k, v, hex(x))
            res[k] = (hex(x), 'rtti-col', key[0])
    # control cruzado de vtables
    if a in CA:
        r = map_vt(a)
        if r and r[0] and v[0] and int(v[0], 16) != r[0]: print('CONFLICTO vt', k, v, hex(r[0]), r[2])
        elif r and r[0] and v[0]: res[k] = (v[0], v[1] + '+rtti', r[2])
json.dump(res, open('map4.json', 'w'), indent=0)
print(collections.Counter(v[1] for v in res.values()))
for k, v in res.items():
    if v[0] is None: print(k, v, ctx[k])
