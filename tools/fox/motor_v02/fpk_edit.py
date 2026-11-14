# PES21 FX (04/11) - leer / rearmar un .fpk de Fox (PES 2021) para AGREGAR o cambiar entradas.
# Formato (medido en common_package.fpk y verificado rearmando byte a byte):
#   0x00 'foxfpk\0win'   0x0A u32 tamano total   0x20 u32 tipo (2)   0x24 u32 entradas   0x28 u32 referencias
#   0x30 entradas de 48 B: u64 off datos, u64 tamano, u64 off nombre, u64 largo nombre, md5(nombre) 16 B
#   luego referencias de 32 B: u64 off nombre, u64 largo nombre, md5 16 B
#   luego los nombres (ascii + \0, seguidos), relleno a 16, y los datos (cada uno alineado a 16)
# Uso: python fpk_edit.py <entrada.fpk>            -> lista las entradas
#      python fpk_edit.py <entrada.fpk> <salida.fpk> -> rearma sin cambios (tiene que salir identico)
import sys, struct, hashlib

def read(d):
    assert d[:6] == b'foxfpk', 'no es un fpk'
    size, = struct.unpack_from('<I', d, 10)
    assert size == len(d), 'tamano distinto del de la cabecera'
    kind, n, nref = struct.unpack_from('<III', d, 32)
    ents, refs = [], []
    for i in range(n):
        doff, dsz, noff, nlen = struct.unpack_from('<QQQQ', d, 48 + 48 * i)
        name = d[noff:noff + nlen].decode('ascii')
        assert hashlib.md5(name.encode('ascii')).digest() == d[48 + 48 * i + 32:48 + 48 * i + 48], name
        ents.append([name, bytes(d[doff:doff + dsz]), doff])
    ro = 48 + 48 * n
    for j in range(nref):
        noff, nlen = struct.unpack_from('<QQ', d, ro + 32 * j)
        refs.append(d[noff:noff + nlen].decode('ascii'))
    return dict(head=bytes(d[:32]), kind=kind, ents=ents, refs=refs)

def write(f):
    """f = read(...) con ents [nombre, datos, (off original, solo para el orden)] y refs [nombre]. Los datos van en el orden
    de su off original (las entradas nuevas, al final)"""
    ents, refs = f['ents'], f['refs']
    n, nref = len(ents), len(refs)
    out = bytearray(f['head']) + struct.pack('<III', f['kind'], n, nref) + b'\0' * 4
    tbl = len(out); out += b'\0' * (48 * n + 32 * nref)
    noffs = []
    for name in [e[0] for e in ents] + refs:
        noffs.append(len(out)); out += name.encode('ascii') + b'\0'
    order = sorted(range(n), key=lambda i: (ents[i][2] if len(ents[i]) > 2 and ents[i][2] is not None else 1 << 62, i))
    doffs = [0] * n
    for i in order:
        out += b'\0' * ((-len(out)) % 16)
        doffs[i] = len(out); out += ents[i][1]
    out += b'\0' * ((-len(out)) % 16)
    for i, e in enumerate(ents):
        nm = e[0].encode('ascii')
        struct.pack_into('<QQQQ', out, tbl + 48 * i, doffs[i], len(e[1]), noffs[i], len(nm))
        out[tbl + 48 * i + 32:tbl + 48 * i + 48] = hashlib.md5(nm).digest()
    for j, r in enumerate(refs):
        nm = r.encode('ascii'); o = tbl + 48 * n + 32 * j
        struct.pack_into('<QQ', out, o, noffs[n + j], len(nm)); out[o + 16:o + 32] = hashlib.md5(nm).digest()
    struct.pack_into('<I', out, 10, len(out))
    return bytes(out)

def put(f, name, blob):
    """cambia la entrada 'name' o la agrega al final"""
    for e in f['ents']:
        if e[0] == name: e[1] = blob; return 'cambiada'
    f['ents'].append([name, blob, None]); return 'agregada'

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read(); f = read(d)
    for e in f['ents']: print('%8d  %s' % (len(e[1]), e[0]))
    for r in f['refs']: print('     ref  %s' % r)
    if len(sys.argv) > 2:
        o = write(f); open(sys.argv[2], 'wb').write(o)
        print('identico' if o == d else 'DISTINTO (%d vs %d bytes)' % (len(o), len(d)))
