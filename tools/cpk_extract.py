# PES21 FX (04/11) - listar / sacar archivos de un .cpk de CRI (PES 2021, p. ej. DreamPatch_Faces_00.cpk o dt80) sin tocarlo.
# Para la boca propia de Messi: su face.fpk no esta suelto en ningun livecpk, viene dentro de un cpk.
# Solo lee el cpk. Soporta tablas @UTF cifradas (xor) y archivos comprimidos CRILAYLA.
# Uso:
#   python cpk_extract.py list <archivo.cpk> [texto]           -> lista las rutas que contienen 'texto' (con tamano)
#   python cpk_extract.py get <archivo.cpk> <ruta exacta> <salida>   -> saca un archivo (ruta como la muestra 'list')
#   python cpk_extract.py find <texto> <cpk1> [cpk2 ...]         -> en que cpk esta (primero al ultimo)
import sys, struct

def unxor(b):
    out = bytearray(b); m, t = 0x655f, 0x4115
    for i in range(len(out)):
        out[i] ^= m & 0xff; m = (m * t) & 0xffffffff
    return bytes(out)

def utf(b):
    """tabla @UTF (big endian) -> lista de filas (dict)"""
    if b[:4] != b'@UTF':
        b = unxor(b)
        if b[:4] != b'@UTF': raise ValueError('no es una tabla @UTF')
    size, = struct.unpack_from('>I', b, 4); base = 8
    _, rows_off, str_off, data_off, name_off, ncol, rowlen, nrow = struct.unpack_from('>HHIIIHHI', b, 8)
    def cstr(o):
        o += base + str_off; e = b.index(b'\0', o); return b[o:e].decode('utf-8', 'replace')
    fmt = {0: ('>B', 1), 1: ('>b', 1), 2: ('>H', 2), 3: ('>h', 2), 4: ('>I', 4), 5: ('>i', 4), 6: ('>Q', 8), 7: ('>q', 8),
           8: ('>f', 4), 0xA: ('>I', 4), 0xB: ('>II', 8)}
    def val(typ, o):
        f, n = fmt[typ]
        v = struct.unpack_from(f, b, o)
        if typ == 0xA: return cstr(v[0]), n
        if typ == 0xB: return b[base + data_off + v[0]:base + data_off + v[0] + v[1]], n
        return v[0], n
    cols, o = [], 32
    for _ in range(ncol):
        flags = b[o]; nm, = struct.unpack_from('>I', b, o + 1); o += 5
        st, typ = flags & 0xF0, flags & 0x0F
        if typ not in fmt: raise ValueError('tipo de columna %x' % typ)
        const = None
        if st == 0x30: const, n = val(typ, o); o += n
        cols.append((cstr(nm), st, typ, const))
    rows = []
    for r in range(nrow):
        p = base + rows_off + r * rowlen; row = {}
        for nm, st, typ, const in cols:
            if st == 0x50: row[nm], n = val(typ, p); p += n
            elif st == 0x30: row[nm] = const
            else: row[nm] = None
        rows.append(row)
    return rows

def crilayla(src):
    """descompresion CRILAYLA (la de CRI): datos de atras para adelante + cabecera sin comprimir de 0x100"""
    usize, hoff = struct.unpack_from('<II', src, 8)
    raw = src[0x10 + hoff:0x10 + hoff + 0x100]
    out = bytearray(usize); pos = usize - 1
    inp = len(src) - 0x100 - 1; bitpool = 0; bits_left = 0
    def get(n):
        nonlocal inp, bitpool, bits_left
        v = 0
        while n:
            if bits_left == 0:
                bitpool = src[inp]; bits_left = 8; inp -= 1
            take = min(bits_left, n)
            v = (v << take) | ((bitpool >> (bits_left - take)) & ((1 << take) - 1))
            bits_left -= take; n -= take
        return v
    end = usize
    while pos >= 0:
        if get(1):
            back = get(13) + 3; ln = 3; lvls = (2, 3, 5, 8); i = 0
            while True:
                n = get(lvls[min(i, 3)]); ln += n
                if n != (1 << lvls[min(i, 3)]) - 1: break
                i += 1
            for _ in range(ln):
                out[pos] = out[pos + back]; pos -= 1
                if pos < 0: break
        else:
            out[pos] = get(8); pos -= 1
    return raw + bytes(out)

class Cpk:
    def __init__(self, path):
        self.f = open(path, 'rb')
        h = self.f.read(16)
        if h[:4] != b'CPK ': raise SystemExit('%s no es un cpk' % path)
        n, = struct.unpack_from('<Q', h, 8)
        hdr = utf(self.f.read(n))[0]
        self.toc_off = hdr.get('TocOffset'); self.content_off = hdr.get('ContentOffset')
        if not self.toc_off: raise SystemExit('cpk sin TOC (solo ITOC): no soportado')
        self.f.seek(self.toc_off); th = self.f.read(16)
        if th[:4] != b'TOC ': raise SystemExit('no encuentro el TOC')
        n, = struct.unpack_from('<Q', th, 8)
        self.rows = utf(self.f.read(n))
        self.base = min(self.toc_off, self.content_off) if self.content_off else self.toc_off
    def path(self, r):
        d = r.get('DirName') or ''
        return (d + '/' if d else '') + (r.get('FileName') or '')
    def list(self, sub=''):
        sub = sub.lower().replace('\\', '/')
        return [(self.path(r), r.get('ExtractSize') or r.get('FileSize'), r) for r in self.rows if sub in self.path(r).lower()]
    def get(self, r):
        self.f.seek(self.base + r['FileOffset']); b = self.f.read(r['FileSize'])
        if b[:8] == b'CRILAYLA': b = crilayla(b)
        es = r.get('ExtractSize')
        if es and len(b) != es: raise SystemExit('tamano extraido %d distinto de ExtractSize %d' % (len(b), es))
        return b

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'list':
        c = Cpk(sys.argv[2])
        for p, n, _ in c.list(sys.argv[3] if len(sys.argv) > 3 else ''): print('%10d  %s' % (n, p))
    elif cmd == 'get':
        c = Cpk(sys.argv[2]); want = sys.argv[3].replace('\\', '/')
        hit = [r for p, n, r in c.list() if p == want]
        if len(hit) != 1: raise SystemExit('no esta (o repetido): %s' % want)
        b = c.get(hit[0]); open(sys.argv[4], 'wb').write(b)
        import hashlib
        print('%s -> %s (%d bytes, SHA256 %s, empieza %r)' % (want, sys.argv[4], len(b), hashlib.sha256(b).hexdigest()[:8].upper(), b[:8]))
    elif cmd == 'find':
        for cp in sys.argv[3:]:
            try:
                for p, n, _ in Cpk(cp).list(sys.argv[2]): print('%s  %10d  %s' % (cp, n, p))
            except SystemExit as e: print('%s: %s' % (cp, e))
