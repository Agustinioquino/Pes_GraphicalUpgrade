import sys, capstone
import match as M
from match import A, B, insns
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
def one(b, a): 
    for i in md.disasm(b, a): return '%s %s' % (i.mnemonic, i.op_str)
for arg in sys.argv[1:]:
    a, b = [int(x, 16) for x in arg.split(':')]
    fa = A.func(a); fb = B.func(b)
    la = insns(A, fa[0], fa[1]); lb = insns(B, fb[0], fb[1]); n = 0
    print('==', hex(a), hex(b))
    for (xa, xb, xm), (ya, yb, ym) in zip(la, lb):
        if bytes(p if k else 0 for p, k in zip(xb, xm)) != bytes(p if k else 0 for p, k in zip(yb, ym)):
            print('  +%#x %-40s | %s' % (xa - fa[0], one(xb, xa), one(yb, ya))); n += 1
            if n > 12: break
