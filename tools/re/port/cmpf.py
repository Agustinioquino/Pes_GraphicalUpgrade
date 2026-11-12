import sys, capstone
import img
A = img.load107(); B = img.load100()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
def dis(im, a, n=40, mx=12):
    out = []
    for i in md.disasm(im.read(a, n * 8), a):
        out.append('%x %-22s %s %s' % (i.address, bytes(i.bytes).hex()[:22], i.mnemonic, i.op_str))
        if len(out) >= mx: break
    return out
for arg in sys.argv[1:]:
    a, d = arg.split(':'); a = int(a, 16); d = int(d, 16)
    fa = A.func(a); fb = B.func(a + d)
    print('==', hex(a), 'func107', fa and (hex(fa[0]), fa[1] - fa[0]), '| 1.00', hex(a + d), fb and (hex(fb[0]), fb[1] - fb[0]))
    la = dis(A, a); lb = dis(B, a + d)
    for x, y in zip(la, lb): print('  %-70s | %s' % (x, y))
