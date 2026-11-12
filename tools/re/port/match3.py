import json, struct, collections, re, sys
import match2 as M2
from match2 import A, B, insns, pattern, fixed, findall, delta_near, map_code, map_data
BASE = 0x140000000
def rtti_index(im):
    """nombre de clase decorado, offset del COL -> [vtables]"""
    rb, rd, _ = im.secs[1]; idx = collections.defaultdict(list); colof = {}
    lo, hi = im.secs[0][0], im.secs[2][0] + 0x800000
    n = len(rd) // 8
    import numpy as np
    q = np.frombuffer(rd[:n * 8], dtype='<u8')
    cand = np.nonzero((q >= rb) & (q < rb + len(rd)))[0]
    for i in cand:
        col = int(q[i])
        h = im.read(col, 24)
        if not h or len(h) < 24: continue
        sig, off, cd, td, ch, self_ = struct.unpack('<6I', h)
        if sig != 1 or self_ != col - BASE: continue
        nm = im.read(BASE + td + 16, 200)
        if nm is None or not nm.startswith(b'.?A'): continue
        nm = nm.split(b'\0')[0].decode('latin1')
        v = rb + (int(i) + 1) * 8
        idx[(nm, off)].append(v); colof[v] = (nm, off)
    return idx, colof
IA, CA = rtti_index(A); IB, CBm = rtti_index(B)
print('vtables', len(CA), len(CBm), file=sys.stderr)
import bisect
VA = sorted(CA)
def vt_len(im, v, col):
    n = 0; cb = im.secs[0][0]; ce = cb + len(im.secs[0][1])
    while True:
        p = im.u64(v + 8 * n)
        if not (cb <= p < ce) or (n > 0 and (v + 8 * n) in col): break
        n += 1
    return n
def map_vt(d):
    i = bisect.bisect_right(VA, d) - 1
    if i < 0: return None
    v = VA[i]; n = vt_len(A, v, CA)
    if d >= v + 8 * max(n, 1): return None
    key = CA[v]; ca = IA[key]; cb_ = IB.get(key, [])
    if len(ca) != len(cb_) or not cb_: return None, 'rtti-ambiguo', '%s: %d vs %d' % (key, len(ca), len(cb_))
    v2 = cb_[ca.index(v)]; n2 = vt_len(B, v2, CBm)
    return v2 + (d - v), 'rtti', '%s+%#x, %d metodos (1.00: %d)' % (key[0], d - v, n, n2)
def map_code2(a):
    r = map_code(a)
    if r[0] is not None: return r
    f = A.func(a); lst = insns(A, f[0], f[1]) if f else insns(A, a, a + 64)
    addrs = [x[0] for x in lst]
    if a not in addrs: return r
    i = addrs.index(a); dp, dn = delta_near(a)
    for bf, af in ((None, None), (20, 30), (8, 12), (4, 8), (2, 5), (0, 6)):
        w = lst if bf is None else lst[max(0, i - bf):min(len(lst), i + af + 1)]
        if fixed(w) < 6: continue
        c = [x + (a - w[0][0]) for x in findall(pattern(w), 200)]
        ex = [x for x in c if x - a in (dp, dn)]
        if len(ex) == 1: return ex[0], 'vecinos-exacto', '%d candidatos; el unico a la distancia de las funciones vecinas (%s)' % (len(c), hex(ex[0] - a))
        if c: return None, 'multi', 'candidatos %s, deltas vecinos %s %s' % ([hex(x - a) for x in c[:8]], dp and hex(dp), dn and hex(dn))
    return r
if __name__ == '__main__':
    ctx = json.load(open('/tmp/claude-0/-home-user/b8b9e95d-3ea7-5485-9fdd-79666368441f/scratchpad/addrs.json'))
    res = json.load(open('map2.json'))
    for k, v in res.items():
        if v[0] is not None: continue
        a = int(k, 16); s = A.sec(a)
        if not s: continue
        if s[2] == '.text': r = map_code2(a)
        else:
            r = map_vt(a)
            if r is None: r = (None, v[1], v[2])
        res[k] = (hex(r[0]) if r[0] else None, r[1], r[2])
        print(k, res[k], ctx[k][0])
    json.dump(res, open('map3.json', 'w'), indent=0)
    print(collections.Counter(v[1] for v in res.values()))
