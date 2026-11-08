#!/usr/bin/env python3
"""PES21 FX - DDS BC7 (con mips) -> .ftex BC7 del juego, quitando los mips de arriba (4k -> 2k sin recomprimir).
uso: dds_to_ftex_bc7.py <entrada.dds> <salida.ftex> <tipo bsm|nrm|srm|trm> [mips_a_saltar=1]
Cabecera: FTEX 2.03, formato 11 (BC7), byte 28 = tipo (bsm 3, nrm 9, srm 1, trm 1); los 16 bytes finales = md5 del mip 0."""
import sys, struct, hashlib, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ftex import Ftex
TYPE = {'bsm': 3, 'nrm': 9, 'srm': 1, 'trm': 1}

def dds_bc7_mips(d):
    h, w = struct.unpack('<II', d[12:20]); n = struct.unpack('<I', d[28:32])[0]
    assert d[84:88] == b'DX10' and struct.unpack('<I', d[128:132])[0] == 98, 'no es BC7'
    off = 148; mips = []
    for i in range(n):
        mw, mh = max(1, w >> i), max(1, h >> i)
        sz = max(1, (mw + 3) // 4) * max(1, (mh + 3) // 4) * 16
        mips.append(d[off:off + sz]); off += sz
    assert off == len(d), 'DDS con datos de mas/menos'
    return w, h, mips

def make(dds, kind, skip=1):
    w, h, mips = dds_bc7_mips(dds)
    mips = mips[skip:]; w >>= skip; h >>= skip
    hdr = bytearray(64)
    hdr[0:4] = b'FTEX'; hdr[4:8] = bytes.fromhex('5c8f0240')
    struct.pack_into('<HHHH', hdr, 8, 11, w, h, 1)
    hdr[16] = len(mips); hdr[17] = 2; hdr[18] = 0x11; hdr[19] = 1; hdr[20] = 1
    hdr[28] = TYPE[kind]
    hdr[48:64] = hashlib.md5(mips[0]).digest()
    f = Ftex.__new__(Ftex)
    f.header = hdr; f.fmt, f.w, f.h, f.d = 11, w, h, 1; f.mipCount = len(mips); f.mips = mips
    return f.to_bytes(chunk=65536, small_first=True)

if __name__ == '__main__':
    a = sys.argv
    out = make(open(a[1], 'rb').read(), a[3], int(a[4]) if len(a) > 4 else 1)
    open(a[2], 'wb').write(out); print(a[2], len(out))
