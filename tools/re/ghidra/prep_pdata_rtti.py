# Precalcula para Ghidra: inicios de funcion (.pdata, sin trozos encadenados) y nombres RTTI (vtables)
import sys, struct, re, os
sys.path.insert(0, "/home/user/Pes_GraphicalUpgrade/tools/re")
from dumpmap import *
out = sys.argv[1]
pb, pd = DATA[".pdata"]
funcs = []
for i in range(0, len(pd) - 11, 12):
    b, e, u = struct.unpack_from("<III", pd, i)
    if b == 0 and e == 0: break
    # unwind info: byte0 = version | flags<<3 ; UNW_FLAG_CHAININFO = 4
    ub = read(BASE + u, 1)
    chained = ub is not None and ((ub[0] >> 3) & 4)
    funcs.append((BASE + b, BASE + e, 1 if chained else 0))
print("pdata", len(funcs), "encadenados", sum(f[2] for f in funcs))
with open(out + "/funcs.txt", "w") as f:
    for b, e, c in funcs: f.write("%x %x %d\n" % (b, e, c))
# RTTI
def demangle(n):
    m = re.match(r"\.\?A[VUT](.*)@@$", n)
    if not m: return None
    s = m.group(1)
    if "?" in s or "$" in s:  # plantillas: nombre simplificado
        s = re.sub(r"\?\$", "", s)
    parts = [p for p in s.split("@") if p]
    nm = "::".join(reversed(parts))
    return re.sub(r"[^A-Za-z0-9_:]", "_", nm)[:200]
tds = {}
for sec in (".idata", ".text1", ".data2", ".data"):
    b, d = DATA[sec]
    for m in re.finditer(rb"\.\?A[VUT][^\x00]{1,400}?@@\x00", d):
        tds[b + m.start() - 0x10] = m.group(0)[:-1].decode(errors="replace")
print("TypeDescriptors", len(tds))
tdrva = {td - BASE: td for td in tds}
cols = {}
for sec in (".idata", ".text1", ".data2", ".data"):
    b, d = DATA[sec]
    for i in range(0, len(d) - 24, 4):
        if d[i] != 1 or d[i+1] or d[i+2] or d[i+3]: continue
        sig, off, cdo, td, cd, me = struct.unpack_from("<IIIIII", d, i)
        if me == b + i - BASE and td in tdrva:
            cols[b + i] = (tdrva[td], off)
print("COLs", len(cols))
colset = set(cols)
vts = []
tb, td_ = DATA[".text"]
for sec in (".idata", ".text1", ".data2", ".data"):
    b, d = DATA[sec]
    for i in range(0, len(d) - 8, 8):
        v = struct.unpack_from("<Q", d, i)[0]
        if v in colset:
            vt = b + i + 8
            ents = []
            for k in range(400):
                x = read(vt + 8 * k, 8)
                if not x: break
                x = struct.unpack("<Q", x)[0]
                if not (tb <= x < tb + len(td_)): break
                if k > 0 and (BASE + 0) and (vt + 8*k - 8 - BASE) and struct.unpack("<Q", read(vt + 8*k, 8))[0] in colset: break
                ents.append(x)
            vts.append((vt, cols[v][0], cols[v][1], ents))
print("vtables", len(vts))
with open(out + "/rtti.txt", "w") as f:
    for vt, td, off, ents in vts:
        nm = demangle(tds[td]) or "cls"
        f.write("V %x %s %d %d %s\n" % (vt, nm, off, len(ents), " ".join("%x" % e for e in ents)))
