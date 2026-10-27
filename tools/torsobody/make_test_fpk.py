# Prueba de TorsoBody_cuello.lua sin el juego: arma un face.fpk de mentira con el cuello generico de TorsoBody (neck.fmdl)
# 15 mm mas abajo y 10 % mas ancho abajo (asi baja sobre el pecho como el de una cara real y hay que cortarlo).
# Uso: python3 make_test_fpk.py <neck.fmdl> <salida face.fpk>   (despues: node test_cuello.js TorsoBody_cuello.lua face.fpk out.fpk)
import sys, os, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'fox'))
from fmdl import Fmdl
f = Fmdl(sys.argv[1]); d = bytearray(f.d)
cnt, off = f.b0[3]; mfa_off = f.b0[9][1]; mf_off = f.b0[10][1]; vf_off = f.b0[11][1]; bo_cnt, bo_off = f.b0[14]
buf_base, _ = f.b1[2]
bufs = [struct.unpack_from("<IIII", d, bo_off + 16 * i) for i in range(bo_cnt)]
AXZ = 0.045
for m in range(cnt):
    e = bytes(d[off + 0x30 * m: off + 0x30 * (m + 1)])
    matinst, bgroup, index, nvert = struct.unpack_from("<HHHH", e, 4)
    nmf, nvf, _, _, fmf, fvf = struct.unpack_from("<BBBBHH", d, mfa_off + 8 * index)
    vfs = [struct.unpack_from("<BBH", d, vf_off + 4 * (fvf + i)) for i in range(nvf)]
    vi = 0
    for k in range(nmf):
        bidx, nf, stride, typ, moff = struct.unpack_from("<BBBBI", d, mf_off + 8 * (fmf + k))
        base = buf_base + bufs[bidx][2] + moff
        for usage, fmt, voff in vfs[vi: vi + nf]:
            if usage == 0 and fmt == 1:
                for v in range(nvert):
                    o = base + stride * v + voff
                    x, y, z = struct.unpack_from("<fff", d, o)
                    s = 1.0 + 0.10 * max(0.0, min(1.0, (1.56 - y) / 0.06))
                    struct.pack_into("<fff", d, o, x * s, y - 0.015, AXZ + (z - AXZ) * s)
        vi += nf
fm = bytes(d)
name = b"/Assets/pes16/model/character/face/real/99999/face_high.fmdl"
hdr = bytearray(48); hdr[0:6] = b"foxfpk"; struct.pack_into("<I", hdr, 36, 1)
ent = bytearray(48); soff = 96; off_ = (soff + len(name) + 15) // 16 * 16
struct.pack_into("<I", ent, 0, off_); struct.pack_into("<I", ent, 8, len(fm)); struct.pack_into("<I", ent, 16, soff); struct.pack_into("<I", ent, 24, len(name))
open(sys.argv[2], "wb").write(bytes(hdr) + bytes(ent) + name + b"\0" * (off_ - soff - len(name)) + fm)
print("face.fpk de prueba: fmdl en", off_)
