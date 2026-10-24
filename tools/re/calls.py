import sys, numpy as np
sys.path.insert(0, '.')
from dumpmap import *
from xref import xrefs
from fn import func_of
def callers(t):
    out = []
    for p, k in xrefs(t):
        op = read(p - 1, 1)[0]
        if op in (0xE8, 0xE9):
            out.append((p - 1, "call" if op == 0xE8 else "jmp"))
        else:
            out.append((p, "ref%02x" % op))
    return out
def ptr_refs(t):
    """punteros absolutos de 64 bits a t en todas las secciones de datos"""
    pat = struct.pack("<Q", t)
    out = []
    for n in DATA:
        if n == ".text": continue
        out += [(n, a) for a in find_all(n, pat)]
    return out
if __name__ == "__main__":
    for s in sys.argv[1:]:
        t = int(s, 16)
        print("==", hex(t))
        for p, kind in callers(t):
            f = func_of(p)
            print("  ", kind, hex(p), "en", hex(f[0]) if f else None)
        for n, a in ptr_refs(t):
            print("   ptr en", n, hex(a))
