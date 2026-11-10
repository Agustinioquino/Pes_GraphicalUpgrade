# PES21 FX (04/11) - boca propia de UN jugador SIN tocar su cara (Agustin: "que usas para guardar el fisico de cada jugador podrias
# usarlo para guardar el oral o el mouth?"). El face.fpk armado desde la cara de Konami pisaba la cara del Dream Patch (cifrada) y un
# oral.fmdl suelto el juego no lo lee. Este script arma la boca 3D con un perfil de dientes (mouth3d.py, MOUTH_PROFILE) y guarda SOLO
# sus dos buffers de vertices en PES21FX\bocas\<perfil>.vb. La DLL (bocajug.cpp), en el dibujo de la boca comun de ese jugador (su
# cara la reconoce por la textura, caras.ini: boca = <perfil>), cambia los vertices por estos: misma malla (mismos triangulos, pesos
# y huesos que la boca comun instalada), otra forma de dientes. Se comprueba aca que la topologia sea la de la comun.
# Formato .vb: 'PFXB', u32 version 1, u32 vertices, u32 indices, u32 paso0 (12), u32 paso1 (28), u32 suma de los indices (mod 2^32),
# u32 0; despues buffer 0 (posicion float3) y buffer 1 (normal/tangente half4, pesos u8x4, huesos u8x4, uv half2), como el fmdl.
# Uso: python build_player_vb.py <mouth.fmdl Konami B7B6754B> <carpeta oral_*.ftex> <perfil> <salida .vb>
# OJO: si cambia la boca comun (mouth3d.py), hay que rehacer el .vb del jugador con el MISMO commit (si no, la DLL lo descarta:
# compara cantidad de vertices e indices con el dibujo del juego).
import sys, os, struct, hashlib, subprocess, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..')]
import numpy as np

KONAMI = 'B7B6754B'
sha8 = lambda b: hashlib.sha256(b).hexdigest()[:8].upper()

def build(konami, texdir, profile, tmp):
    env = dict(os.environ); env['MOUTH_PROFILE'] = profile
    out = os.path.join(tmp, (profile or 'comun') + '.npz')
    r = subprocess.run([sys.executable, os.path.join(HERE, 'mouth3d.py'), konami, texdir, os.path.join(tmp, 'x.fmdl'), out],
                       env=env, capture_output=True, text=True)
    if r.returncode: raise SystemExit('mouth3d.py (%s) fallo:\n%s' % (profile or 'comun', r.stdout[-2000:] + r.stderr[-2000:]))
    with np.load(out) as z: return {k: z[k] for k in z.files}   # (04/11) cerrar el .npz: en Windows la carpeta temporal no se podia borrar (WinError 32)

def main(konami, texdir, profile, dst):
    kb = open(konami, 'rb').read()
    if sha8(kb) != KONAMI: raise SystemExit('%s no es el mouth.fmdl de Konami (%s): no sigo' % (konami, sha8(kb)))
    import mouth_fmdl
    with tempfile.TemporaryDirectory() as tmp:
        com = build(konami, texdir, '', tmp)
        r = build(konami, texdir, profile, tmp)
    for k in ('faces', 'w', 'bidx'):
        if not np.array_equal(com[k], r[k]): raise SystemExit('el perfil %s no tiene la misma %s que la boca comun: no sirve para la DLL' % (profile, k))
    N, M = len(r['pos']), len(r['faces'])
    b0, b1, _ = mouth_fmdl.streams(r['pos'], r['nrm'], r['tan'], r['w'], r['bidx'], r['uv'])
    assert len(b0) == 12 * N and len(b1) == 28 * N
    isum = int(r['faces'].astype(np.int64).sum()) & 0xffffffff
    blob = b'PFXB' + struct.pack('<7I', 1, N, 3 * M, 12, 28, isum, 0) + b0 + b1
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    open(dst, 'wb').write(blob)
    p = r['pos']
    print('perfil %s: %d vertices, %d indices (misma malla que la comun)' % (profile, N, 3 * M))
    print('huella: suma pos (%.6f, %.4f, %.5f) suma nrm (%.3f, %.3f, %.3f) suma caras %d uv (%.4f, %.4f)' % (
        *p.sum(0), *r['nrm'][:, :3].sum(0), int(r['faces'].astype(np.int64).sum()), *r['uv'].sum(0)))
    print('salida %s: %d bytes SHA256 %s' % (dst, len(blob), sha8(blob)))

if __name__ == '__main__':
    if len(sys.argv) < 5: raise SystemExit('uso: build_player_vb.py <mouth.fmdl Konami> <carpeta oral_*.ftex> <perfil> <salida .vb>')
    main(*sys.argv[1:5])
