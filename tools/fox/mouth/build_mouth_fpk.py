# PES21 FX (31/10) - arma en la PC el common_package.fpk con la boca 3D (mouth3d.py) a partir del fpk instalado (TorsoBody):
# saca mouth.fmdl (tiene que ser el de Konami, SHA256 B7B6754B...), arma la boca nueva y la vuelve a poner (el fpk cambia de
# tamano: las entradas siguientes se corren; el resto queda byte a byte igual). Los modelos de Konami / TorsoBody no van al repo.
# Uso: python build_mouth_fpk.py <common_package.fpk instalado> <carpeta con oral_bsm.ftex y oral_trm.ftex> <salida.fpk>
import sys, os, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', '..'), os.path.join(HERE, '..', '..', 'sincamiseta')]
from make_fpk_sin_short import entries
import mouth3d, mouth_fmdl

KONAMI = 'B7B6754B'

def main(src, texdir, dst):
    d = open(src, 'rb').read()
    print('fuente %s SHA256 %s' % (src, hashlib.sha256(d).hexdigest()[:8].upper()))
    e = [x for x in entries(d) if x['name'].endswith('/common/mouth.fmdl')]
    assert len(e) == 1, 'el fpk no tiene mouth.fmdl'
    blob = d[e[0]['off']:e[0]['off'] + e[0]['size']]
    sha = hashlib.sha256(blob).hexdigest()[:8].upper()
    print('mouth.fmdl de la fuente: %d bytes SHA256 %s' % (len(blob), sha))
    if sha != KONAMI: raise SystemExit('mouth.fmdl no es el de Konami (%s): no sigo' % KONAMI)
    tmp = dst + '.mouth_konami.fmdl'; open(tmp, 'wb').write(blob)
    r = mouth3d.build(tmp, texdir)
    new = mouth_fmdl.write(tmp, dst + '.mouth3d.fmdl', r['pos'], r['nrm'], r['tan'], r['w'], r['bidx'], r['uv'], r['faces'])
    out = mouth_fmdl.fpk_replace(d, '/common/mouth.fmdl', new)
    chk = [x for x in entries(out) if x['name'].endswith('/common/mouth.fmdl')][0]
    assert out[chk['off']:chk['off'] + chk['size']] == new
    open(dst, 'wb').write(out)
    print('mouth.fmdl nuevo: %d bytes SHA256 %s' % (len(new), hashlib.sha256(new).hexdigest()[:8].upper()))
    print('salida %s (%d bytes) SHA256 %s' % (dst, len(out), hashlib.sha256(out).hexdigest()[:8].upper()))
    os.remove(tmp)

if __name__ == '__main__':
    main(*sys.argv[1:4])
