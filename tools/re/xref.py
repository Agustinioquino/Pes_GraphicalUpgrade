import sys, numpy as np
sys.path.insert(0, '.')
from dumpmap import *
def xrefs(target, sec=".text", extra=(0,)):
    """posiciones i (VA de la instruccion aproximada) donde hay un disp32 rip-relativo que apunta a target.
    extra: bytes de inmediato despues del disp (0 = lea/mov reg, 1 = cmp byte imm8, 4 = mov dword imm32)"""
    b, d = DATA[sec]
    out = []
    n = len(d)
    for a in range(4):
        cnt = (n - a) // 4
        arr = np.frombuffer(d, dtype='<i4', count=cnt, offset=a).astype(np.int64)
        idx = a + 4 * np.arange(cnt, dtype=np.int64)
        for k in extra:
            rhs = target - (b + idx + 4 + k)
            hit = np.nonzero(arr == rhs)[0]
            for h in hit: out.append((int(b + idx[h]), k))
    return sorted(out)
if __name__ == "__main__":
    for s in sys.argv[1:]:
        t = int(s, 16)
        print(hex(t), [(hex(p), k) for p, k in xrefs(t)])
