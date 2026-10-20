#!/usr/bin/env python3
"""PES21 FX - lector/escritor de texturas FTEX (Fox Engine, PES 2021) sin herramientas externas.
Formato (version 2.03): cabecera de 64 bytes ('FTEX', version, formato, ancho, alto, profundidad, mips, ...), tabla de
mips de 16 bytes (offset, tamano descomprimido, tamano comprimido, indice, cantidad de trozos) y, por mip, una tabla de
trozos de 8 bytes (comprimido u16, descomprimido u16, offset u32 desde el inicio del mip) seguida de los trozos zlib.
Formatos de pixel conocidos: 0 = RGBA8 (BGRA), 2 = DXT1, 3 = DXT3, 4 = DXT5."""
import struct, zlib

BLOCK = {2: 8, 3: 16, 4: 16}

class Ftex:
    def __init__(self, data):
        if data[:4] != b'FTEX': raise ValueError('no es FTEX')
        self.header = bytearray(data[:64])
        self.fmt, self.w, self.h, self.d = struct.unpack('<HHHH', data[8:16])
        self.mipCount = data[16]
        self.mips = []
        for i in range(self.mipCount):
            off, usz, csz, idx, cc = struct.unpack('<IIIHH', data[64 + 16 * i:80 + 16 * i])
            chunks = []
            for k in range(cc):
                c, u, o = struct.unpack('<HHI', data[off + 8 * k:off + 8 * k + 8])
                raw = o & 0x80000000
                o &= 0x7fffffff
                blob = data[off + o:off + o + c]
                if raw or c == u and not blob[:1] in (b'\x78',):
                    chunks.append(bytes(blob))
                else:
                    chunks.append(zlib.decompress(blob))
            m = b''.join(chunks)
            if len(m) != usz: raise ValueError('mip %d: %d != %d' % (i, len(m), usz))
            self.mips.append(m)

    def mip_size(self, i):
        return max(1, self.w >> i), max(1, self.h >> i)

    def to_bytes(self, chunk=16384, level=9):
        out = bytearray(self.header)
        struct.pack_into('<HHHH', out, 8, self.fmt, self.w, self.h, self.d)
        out[16] = len(self.mips)
        table = bytearray(16 * len(self.mips))
        body = bytearray()
        base = 64 + len(table)
        for i, m in enumerate(self.mips):
            parts = [m[p:p + chunk] for p in range(0, len(m), chunk)] or [b'']
            comp = [zlib.compress(p, level) for p in parts]
            hdr = bytearray(); o = 8 * len(parts)
            for p, c in zip(parts, comp):
                hdr += struct.pack('<HHI', len(c), len(p), o); o += len(c)
            blob = bytes(hdr) + b''.join(comp)
            struct.pack_into('<IIIHH', table, 16 * i, base + len(body), len(m), len(blob), i, len(parts))
            body += blob
        return bytes(out + table + body)

def dxt_to_dds(f, mip0only=False):
    """DDS (DXT1/3/5) con la cadena de mips, para decodificar con Pillow."""
    fourcc = {2: b'DXT1', 3: b'DXT3', 4: b'DXT5'}[f.fmt]
    mips = f.mips[:1] if mip0only else f.mips
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000 | (0x20000 if len(mips) > 1 else 0)
    hdr = struct.pack('<4sIIIIIII', b'DDS ', 124, flags, f.h, f.w, len(f.mips[0]), 0, len(mips)) + b'\0' * 44
    hdr += struct.pack('<II4sIIIII', 32, 0x4, fourcc, 0, 0, 0, 0, 0)
    hdr += struct.pack('<IIIII', 0x1000 | (0x400008 if len(mips) > 1 else 0), 0, 0, 0, 0)
    return hdr + b''.join(mips)
