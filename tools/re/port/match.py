import re, struct, json, sys, bisect
import capstone
from capstone import x86 as X
import numpy as np
import img
A = img.load107(); B = img.load100()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
CB, CD, _ = B.secs[0]

def insns(im, start, end):
    """lista de (addr, bytes, mascara) de un tramo de codigo; mascara: 1 = byte fijo, 0 = comodin (desplazamientos que cambian entre versiones)"""
    code = im.read(start, end - start); out = []
    for ins in md.disasm(code, start):
        b = bytes(ins.bytes); m = bytearray(b'\x01' * len(b))
        do, ds = ins.disp_offset, ins.disp_size
        io, isz = ins.imm_offset, ins.imm_size
        rip = any(o.type == X.X86_OP_MEM and o.mem.base == X.X86_REG_RIP for o in ins.operands)
        if rip:
            dv = [o.mem.disp for o in ins.operands if o.type == X.X86_OP_MEM and o.mem.base == X.X86_REG_RIP][0]
            pk = struct.pack('<i', dv); p = b.rfind(pk) if not (ds == 4 and do and b[do:do + 4] == pk) else do
            if p > 0: m[p:p + 4] = b'\0\0\0\0'
        elif ds == 4 and do:
            d = struct.unpack_from('<i', b, do)[0]
            if abs(d) >= 0x100000: m[do:do + 4] = b'\0\0\0\0'      # RVA de tablas de saltos / datos
        if ins.group(X.X86_GRP_CALL) or ins.group(X.X86_GRP_JUMP):
            if isz == 4 and io: m[io:io + 4] = b'\0\0\0\0'
        out.append((ins.address, b, bytes(m)))
    return out
def pattern(lst):
    p = b''
    for a, b, m in lst:
        for x, k in zip(b, m): p += re.escape(bytes([x])) if k else b'.'
    return re.compile(p, re.S)
def find(pat, limit=3):
    r = []
    for m in pat.finditer(CD):
        r.append(CB + m.start())
        if len(r) >= limit: break
    return r
def fixed(lst): return sum(sum(m) for _, _, m in lst)

def map_code(a):
    """direccion de codigo 1.07 -> 1.00. Devuelve (addr100, modo, detalle)"""
    f = A.func(a)
    if not f:
        # sin funcion en .pdata: ventana a partir de la direccion
        lst = insns(A, a, a + 64); f = (a, a + 64)
    else:
        lst = insns(A, f[0], f[1])
    addrs = [x[0] for x in lst]
    if a not in addrs:
        return None, 'no-ins', 'la direccion no cae en un limite de instruccion'
    i = addrs.index(a)
    # 1) funcion entera (hasta 1500 bytes)
    whole = lst
    if f[1] - f[0] <= 6000 and fixed(whole) >= 12:
        r = find(pattern(whole))
        if len(r) == 1: return r[0] + (a - f[0]), 'func', 'funcion identica (%d bytes)' % (f[1] - f[0])
    # 2) ventanas crecientes alrededor de la instruccion
    best = None
    for before, after in ((4, 8), (8, 12), (12, 20), (20, 30), (30, 50), (2, 5), (0, 8), (0, 14), (8, 0), (14, 0), (0, 25), (25, 0)):
        lo = max(0, i - before); hi = min(len(lst), i + after + 1)
        w = lst[lo:hi]
        if fixed(w) < 10: continue
        r = find(pattern(w))
        if len(r) == 1:
            return r[0] + (a - w[0][0]), 'win', 'ventana -%d/+%d instrucciones' % (i - lo, hi - i - 1)
        if len(r) > 1 and best is None: best = ('multi', '%d coincidencias con -%d/+%d' % (len(r), before, after))
    return None, (best[0] if best else 'none'), (best[1] if best else 'sin coincidencia')

if __name__ == '__main__':
    ctx = json.load(open('/tmp/claude-0/-home-user/b8b9e95d-3ea7-5485-9fdd-79666368441f/scratchpad/addrs.json'))
    res = {}
    for k in ctx:
        a = int(k, 16)
        s = A.sec(a)
        if not s: res[k] = (None, 'fuera', ''); continue
        if s[2] != '.text': res[k] = (None, 'dato', s[2]); continue
        r = map_code(a); res[k] = (hex(r[0]) if r[0] else None, r[1], r[2])
        print(k, res[k], ctx[k][0], flush=True)
    json.dump(res, open('code_map.json', 'w'), indent=0)
