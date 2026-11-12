# imagenes de las dos versiones del exe (solo lectura): v107 = volcado de memoria 1.07.02 (62787C6A), v100 = exe 1.00 en disco (5F5ED4BF)
import struct, bisect, numpy as np
BASE = 0x140000000
class Img:
    def __init__(s, name): s.name = name; s.secs = []   # (va, bytes, nombre)
    def add(s, va, data, nm): s.secs.append((BASE + va, data, nm))
    def sec(s, a):
        for b, d, n in s.secs:
            if b <= a < b + len(d): return b, d, n
        return None
    def read(s, a, n):
        x = s.sec(a)
        if not x: return None
        b, d, _ = x; return d[a - b:a - b + n]
    def u32(s, a): return struct.unpack('<I', s.read(a, 4))[0]
    def i32(s, a): return struct.unpack('<i', s.read(a, 4))[0]
    def u64(s, a): return struct.unpack('<Q', s.read(a, 8))[0]
    def init_pdata(s, va, data):
        n = len(data) // 12
        a = np.frombuffer(data[:n * 12], dtype='<u4').reshape(-1, 3)
        cb, cd, _ = s.secs[0]
        ok = (a[:, 0] >= cb - BASE) & (a[:, 1] > a[:, 0]) & (a[:, 1] <= cb - BASE + len(cd))
        a = a[ok]; a = a[np.argsort(a[:, 0])]
        s.fb = [int(x) + BASE for x in a[:, 0]]; s.fe = [int(x) + BASE for x in a[:, 1]]
    def func(s, a):
        i = bisect.bisect_right(s.fb, a) - 1
        if i >= 0 and s.fb[i] <= a < s.fe[i]: return s.fb[i], s.fe[i]
        return None
def load107(D='/mnt/user-data/uploads/PES21FX/dump/'):
    im = Img('1.07')
    for nm, va, f in [('.text', 0x1000, '_text.bin'), ('.rdata', 0x2538000, '_idata.bin'), ('.data', 0x33A9000, '_text1.bin')]:
        im.add(va, open(D + f, 'rb').read(), nm)
    im.init_pdata(0x3AD3000, open(D + '_pdata.bin', 'rb').read())
    return im
def load100(path='/mnt/project-files/PES2021 (1).exe'):
    im = Img('1.00'); f = open(path, 'rb')
    def rd(off, n): f.seek(off); return f.read(n)
    im.add(0x1000, rd(0x600, 0x252d800), '.text')
    im.add(0x252f000, rd(0x252de00, 0xe6bc00), '.rdata')
    im.add(0x339b000, rd(0x3399a00, 0x334800), '.data')
    im.init_pdata(0x3ac5000, rd(0x36ce200, 0x1d0e00))
    return im
