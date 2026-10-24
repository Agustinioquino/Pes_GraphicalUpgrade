import sys, struct
sys.path.insert(0,'.')
from dumpmap import *
def type_desc(name):
    """direcciones de TypeDescriptor cuyo nombre es name (p.ej. '.?AVCameraSystem@rd@cobra@@')"""
    out = []
    for a in str_addrs(name):
        out.append(a - 0x10)   # TypeDescriptor: pVFTable(8) spare(8) name
    return out
def cols_for(td):
    rva = td - BASE
    out = []
    for n in DATA:
        if n == ".text": continue
        b, d = DATA[n]
        pat = struct.pack("<I", rva)
        i = d.find(pat)
        while i >= 0:
            col = b + i - 12
            sig = struct.unpack_from("<I", d, i - 12)[0] if i >= 12 else None
            if sig == 1:
                selfrva = struct.unpack_from("<I", d, i + 8)[0]
                if selfrva == col - BASE: out.append(col)
            i = d.find(pat, i + 1)
    return out
def vtables_for(col):
    pat = struct.pack("<Q", col)
    out = []
    for n in DATA:
        if n == ".text": continue
        out += [a + 8 for a in find_all(n, pat)]
    return out
def vt_entries(vt, n=40):
    res = []
    for i in range(n):
        v = struct.unpack("<Q", read(vt + 8 * i, 8))[0]
        s, _ = sec_of(v)
        if s != ".text": break
        res.append(v)
    return res
def col_name(col):
    tdr = struct.unpack("<I", read(col + 12, 4))[0]
    nm = read(BASE + tdr + 0x10, 200)
    return nm.split(b"\0")[0].decode(errors="replace")
if __name__ == "__main__":
    for nm in sys.argv[1:]:
        for td in type_desc(nm):
            for col in cols_for(td):
                off = struct.unpack("<I", read(col + 4, 4))[0]
                for vt in vtables_for(col):
                    ents = vt_entries(vt)
                    print(nm, "col", hex(col), "off", off, "vtable", hex(vt), "n", len(ents))
                    for i, e in enumerate(ents): print("    [%d] %s" % (i, hex(e)))
