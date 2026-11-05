# PES21 FX (03/11) - prueba: pone el mouth.fmdl ORIGINAL de Konami (SHA256 B7B6754B...) en el fpk de TorsoBody, sin tocar
# ninguna otra entrada (byte a byte iguales). Sirve para ver si algo viene de la boca 3D o del modelo de la cara.
# Uso: python konami_mouth_fpk.py <common_package.fpk vivo> <mouth.fmdl de Konami> <salida.fpk>
# Para volver: reponer el fpk vivo desde su backup (no hace falta rearmar nada).
import sys, os, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', '..'), os.path.join(HERE, '..', '..', 'sincamiseta')]
from make_fpk_sin_short import entries
import mouth_fmdl

def sha(b): return hashlib.sha256(b).hexdigest()[:8].upper()

def main(src, konami, dst):
    d = open(src, 'rb').read(); k = open(konami, 'rb').read()
    print('fpk vivo %s SHA256 %s (%d bytes)' % (src, sha(d), len(d)))
    if sha(k) != 'B7B6754B': raise SystemExit('el mouth.fmdl no es el de Konami (%s): no sigo' % sha(k))
    out = mouth_fmdl.fpk_replace(d, '/common/mouth.fmdl', k)
    a = {x['name']: d[x['off']:x['off'] + x['size']] for x in entries(d)}
    b = {x['name']: out[x['off']:x['off'] + x['size']] for x in entries(out)}
    assert a.keys() == b.keys()
    for n in a:
        if n.endswith('/common/mouth.fmdl'): assert b[n] == k
        else: assert a[n] == b[n], n
    open(dst, 'wb').write(out)
    print('salida %s SHA256 %s (%d bytes); solo cambio mouth.fmdl -> B7B6754B, %d entradas iguales' % (dst, sha(out), len(out), len(a) - 1))

if __name__ == '__main__':
    main(*sys.argv[1:4])
