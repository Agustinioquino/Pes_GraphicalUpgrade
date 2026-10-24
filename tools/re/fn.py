import sys, struct, subprocess, bisect
sys.path.insert(0, '.')
from dumpmap import *
pb, pd = DATA[".pdata"]
FUN = []
for i in range(0, len(pd) - 12, 12):
    s, e, u = struct.unpack_from("<III", pd, i)
    if s == 0 and e == 0: continue
    FUN.append((BASE + s, BASE + e))
FUN.sort()
STARTS = [f[0] for f in FUN]
def func_of(addr):
    i = bisect.bisect_right(STARTS, addr) - 1
    if i >= 0 and FUN[i][0] <= addr < FUN[i][1]: return FUN[i]
    return None
def dis(start, end):
    tb = TEXT_BASE
    cmd = ["objdump", "-D", "-b", "binary", "-m", "i386:x86-64", "-M", "intel", "--adjust-vma=%#x" % tb,
           "--start-address=%#x" % start, "--stop-address=%#x" % end, D + "_text.bin"]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    lines = [l for l in out.splitlines() if l.strip().startswith("14")]
    return "\n".join(lines)
if __name__ == "__main__":
    a = int(sys.argv[1], 16)
    f = func_of(a)
    print("funcion:", [hex(x) for x in f] if f else None)
    if len(sys.argv) > 2 and sys.argv[2] == "all" and f: print(dis(f[0], f[1]))
    else: print(dis(a - 0x60, a + 0x60))

UNW = {}
for i in range(0, len(pd) - 12, 12):
    s, e, u = struct.unpack_from("<III", pd, i)
    if s == 0 and e == 0: continue
    UNW[BASE + s] = BASE + u
def root_of(addr):
    """sigue la cadena de unwind (UNW_FLAG_CHAININFO) hasta la funcion primaria"""
    f = func_of(addr)
    seen = 0
    while f and seen < 20:
        u = UNW.get(f[0])
        if u is None: return f
        hdr = read(u, 4)
        if hdr is None: return f
        flags = hdr[0] >> 3
        if not (flags & 4): return f
        cnt = hdr[2]
        off = 4 + ((cnt + 1) & ~1) * 2
        s, e, uu = struct.unpack("<III", read(u + off, 12))
        f = (BASE + s, BASE + e)
        seen += 1
    return f
