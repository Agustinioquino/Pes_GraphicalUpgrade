# PES21 FX (04/11) - boca propia de UN jugador (Agustin: 'una boca especifica para el player id de Messi').
# El juego usa oral.fmdl si viene dentro del face.fpk de la cara (44 caras del Dream Patch ya lo traen); si no, la boca comun
# (mouth.fmdl de common_package). Este script arma la boca 3D con un perfil de dientes (mouth3d.py, MOUTH_PROFILE) y la pone
# como entrada 'oral.fmdl' del face.fpk del jugador; el resto de las entradas (face_high, hair_high, face_diff) queda byte a
# byte igual. Las texturas siguen siendo las comunes (regla 63fab773 -> pes_oral_*). Modelos de Konami/caras: no van al repo.
# Uso: python build_player_oral.py <face.fpk del jugador> <carpeta oral_*.ftex> <salida face.fpk> <mouth.fmdl Konami B7B6754B> <perfil>
import sys, os, hashlib
if len(sys.argv) > 5: os.environ['MOUTH_PROFILE'] = sys.argv[5]   # antes de importar mouth3d (lee el perfil al cargar)
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', '..')]
import fpk_edit, mouth3d, mouth_fmdl

KONAMI = 'B7B6754B'
sha8 = lambda b: hashlib.sha256(b).hexdigest()[:8].upper()

def main(src, texdir, dst, konami, profile):
    d = open(src, 'rb').read(); f = fpk_edit.read(d)
    print('cara %s: %d bytes SHA256 %s; entradas %s' % (src, len(d), sha8(d), ', '.join(e[0] for e in f['ents'])))
    if not any(e[0] == 'face_high.fmdl' for e in f['ents']): raise SystemExit('no parece un face.fpk (sin face_high.fmdl)')
    kb = open(konami, 'rb').read()
    if sha8(kb) != KONAMI: raise SystemExit('%s no es el mouth.fmdl de Konami (%s): no sigo' % (konami, sha8(kb)))
    tmp = dst + '.mouth_konami.fmdl'; open(tmp, 'wb').write(kb)
    print('perfil de dientes: %s' % (mouth3d.PROFILE or 'comun'))
    r = mouth3d.build(tmp, texdir)
    new = mouth_fmdl.write(tmp, dst + '.oral.fmdl', r['pos'], r['nrm'], r['tan'], r['w'], r['bidx'], r['uv'], r['faces'])
    os.remove(tmp)
    old = {e[0]: e[1] for e in f['ents']}
    what = fpk_edit.put(f, 'oral.fmdl', new)
    out = fpk_edit.write(f)
    g = fpk_edit.read(out)
    for e in g['ents']:   # todo lo demas, identico
        if e[0] != 'oral.fmdl': assert e[1] == old[e[0]], e[0]
    assert [e[1] for e in g['ents'] if e[0] == 'oral.fmdl'] == [new]
    open(dst, 'wb').write(out)
    print('oral.fmdl %s: %d bytes SHA256 %s' % (what, len(new), sha8(new)))
    print('salida %s (%d bytes) SHA256 %s' % (dst, len(out), sha8(out)))

if __name__ == '__main__':
    if len(sys.argv) < 6: raise SystemExit(__doc__ if __doc__ else 'uso: ver cabecera')
    main(*sys.argv[1:6])
