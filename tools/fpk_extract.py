#!/usr/bin/env python3
# Extractor de solo lectura de paquetes Fox Engine .fpk / .fpkd (PES 2021). No modifica nada.
# Formato (verificado en dt00_x64.cpk, 08/11/2026): cabecera "foxfpk" + tipo (' ' o 'd') + "win" + tamano (u32, 0x0a),
# version u32 en 0x20 (= 2), cantidad de archivos u32 en 0x24, cantidad de referencias u32 en 0x28; entradas desde 0x30 de
# 0x30 bytes: offset de datos u64, tamano u64, offset de la ruta u64, largo de la ruta u64, md5 (16). Despues de las entradas
# vienen las referencias (ruta de otro fpk del que depende): offset u64 + largo u64.
# Uso:
#   python fpk_extract.py listar  archivo.fpkd
#   python fpk_extract.py extraer archivo.fpkd destino\            (crea destino\<ruta interna>)
import os, struct, sys

def open_fpk(path):
    d = open(path, 'rb').read()
    if d[:6] != b'foxfpk': raise ValueError('no es un fpk: ' + path)
    kind = chr(d[6]) if d[6] != 0 else ' '
    size = struct.unpack_from('<I', d, 0x0a)[0]
    ver, nfiles, nrefs = struct.unpack_from('<III', d, 0x20)
    files = []
    p = 0x30
    for i in range(nfiles):
        off, sz, poff, plen = struct.unpack_from('<QQQQ', d, p)
        md5 = d[p + 0x20:p + 0x30]
        name = d[poff:poff + plen].decode('utf-8', 'replace')
        files.append((name, off, sz, md5.hex()))
        p += 0x30
    refs = []
    for i in range(nrefs):
        poff, plen = struct.unpack_from('<QQ', d, p)
        refs.append(d[poff:poff + plen].decode('utf-8', 'replace'))
        p += 0x10
    return d, dict(kind=kind, size=size, version=ver), files, refs

def main():
    if len(sys.argv) < 3: print(__doc__); return 1
    cmd, path = sys.argv[1], sys.argv[2]
    d, hdr, files, refs = open_fpk(path)
    if cmd == 'listar':
        print('fpk%s version %d tamano %d: %d archivos, %d referencias' % (hdr['kind'].strip(), hdr['version'], hdr['size'], len(files), len(refs)))
        for name, off, sz, md5 in files: print('%s\t%u\t%u\t%s' % (name, off, sz, md5))
        for r in refs: print('ref\t%s' % r)
    elif cmd == 'extraer':
        dest = sys.argv[3]
        for name, off, sz, md5 in files:
            out = os.path.join(dest, name.lstrip('/').replace('/', os.sep))
            os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
            open(out, 'wb').write(d[off:off + sz])
            print('%s\t%u' % (out, sz))
        if refs:
            with open(os.path.join(dest, '_referencias_%s.txt' % os.path.basename(path)), 'w') as f:
                f.write('\n'.join(refs) + '\n')
    else:
        print(__doc__); return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
