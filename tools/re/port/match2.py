import re, struct, json, sys, bisect, collections
import numpy as np
import match as M
from match import A, B, insns, pattern, fixed, CB, CD
import img
AB, AD, _ = A.secs[0]
def findall(pat, limit=64):
    r = []
    for m in pat.finditer(CD):
        r.append(CB + m.start())
        if len(r) >= limit: break
    return r
_fcache = {}
def map_func_unique(fi):
    """funcion i de 1.07 -> inicio en 1.00 si la funcion entera (enmascarada) aparece una sola vez"""
    if fi in _fcache: return _fcache[fi]
    s, e = A.fb[fi], A.fe[fi]; r = None
    if 24 <= e - s <= 4000:
        lst = insns(A, s, e)
        if lst and lst[-1][0] + len(lst[-1][1]) == e and fixed(lst) >= 16:
            c = findall(pattern(lst), 2)
            if len(c) == 1: r = c[0]
    _fcache[fi] = r; return r
def delta_near(a):
    """desplazamiento (1.00 - 1.07) de las funcion vecinas con coincidencia unica: (delta_anterior, delta_siguiente)"""
    i = bisect.bisect_right(A.fb, a) - 1; out = []
    for step in (-1, 1):
        j = i + step; d = None
        for _ in range(60):
            if j < 0 or j >= len(A.fb): break
            m = map_func_unique(j)
            if m is not None: d = m - A.fb[j]; break
            j += step
        out.append(d)
    return out
def map_code(a):
    r = M.map_code(a)
    if r[0] is not None: 
        return r
    # ambiguo o sin coincidencia: candidatos + vecinos
    f = A.func(a)
    lst = insns(A, f[0], f[1]) if f else insns(A, a, a + 64)
    addrs = [x[0] for x in lst]
    if a not in addrs: return r
    i = addrs.index(a); dp, dn = delta_near(a)
    ds = [d for d in (dp, dn) if d is not None]
    if not ds: return r
    tries = [(None, None)] if f and f[1] - f[0] <= 6000 else []
    tries += [(30, 50), (20, 30), (12, 20), (8, 12), (4, 8), (2, 5), (0, 8), (8, 0)]
    for bf, af in tries:
        if bf is None: w = lst
        else: w = lst[max(0, i - bf):min(len(lst), i + af + 1)]
        if fixed(w) < 8: continue
        c = findall(pattern(w))
        if not c: continue
        cand = [x + (a - w[0][0]) for x in c]
        lo, hi = a + min(ds) - 0x400, a + max(ds) + 0x400
        inr = [x for x in cand if lo <= x <= hi]
        if len(inr) == 1:
            return inr[0], 'vecinos', '%d candidatos, 1 entre las funciones vecinas (delta %s)' % (len(cand), '/'.join(hex(d) for d in ds))
    return None, r[1], r[2] + ' (delta vecinos %s)' % '/'.join(hex(d) for d in ds)

# ---- datos: referencias rip-relativas en el codigo de 1.07
_arr = np.frombuffer(AD, dtype=np.uint8)
def _targets(k):
    n = len(AD) - 4
    v = (_arr[0:n].astype(np.int64) | (_arr[1:n+1].astype(np.int64) << 8) | (_arr[2:n+2].astype(np.int64) << 16) | (_arr[3:n+3].astype(np.int64) << 24))
    v = np.where(v >= 2**31, v - 2**32, v)
    return v + np.arange(n, dtype=np.int64) + AB + 4 + k
T = {k: _targets(k) for k in (0, 1, 4)}
def refs(d):
    """instrucciones de 1.07 con operando [rip+disp] == d: lista de (addr_instruccion, offset_del_disp)"""
    out = []
    for k, t in T.items():
        for p in np.nonzero(t == d)[0][:400]:
            pa = AB + int(p); f = A.func(pa)
            if not f: continue
            for ia, b, m in insns(A, f[0], f[1]):
                if ia <= pa < ia + len(b):
                    if m[pa - ia:pa - ia + 4] == b'\0\0\0\0' and ia + len(b) == pa + 4 + k: out.append((ia, pa - ia, len(b)))
                    break
    return out
def map_data(d, maxrefs=12):
    rs = refs(d); votes = collections.Counter(); used = 0
    for ia, off, ln in rs[:maxrefs]:
        m = map_code(ia)
        if m[0] is None: continue
        disp = B.i32(m[0] + off); votes[m[0] + ln + disp] += 1; used += 1
    return votes, len(rs), used

if __name__ == '__main__':
    ctx = json.load(open('/tmp/claude-0/-home-user/b8b9e95d-3ea7-5485-9fdd-79666368441f/scratchpad/addrs.json'))
    res = {}
    for k in ctx:
        a = int(k, 16); s = A.sec(a)
        if not s: res[k] = (None, 'fuera', ''); 
        elif s[2] == '.text':
            r = map_code(a); res[k] = (hex(r[0]) if r[0] else None, r[1], r[2])
        else:
            v, n, u = map_data(a)
            if len(v) == 1: x = list(v)[0]; res[k] = (hex(x), 'dato', '%s: %d refs, %d resueltas, todas de acuerdo' % (s[2], n, u))
            elif len(v) > 1: res[k] = (None, 'dato-conflicto', str({hex(x): c for x, c in v.items()}))
            else: res[k] = (None, 'dato-sinref', '%s: %d refs' % (s[2], n))
        print(k, res[k], ctx[k][0], flush=True)
    json.dump(res, open('map2.json', 'w'), indent=0)
