# PES21 FX - resume el diagnostico del LOD de caras (x118) de PES21FX.log: por cara, las mallas vistas (indices, VS, PS,
# distancia) y las del cuerpo del mismo jugador. Uso: read_lod.py PES21FX.log
import re, sys, collections

path = sys.argv[1] if len(sys.argv) > 1 else 'PES21FX.log'
faces = collections.OrderedDict()   # id -> ultima linea de resumen
events = []
for ln in open(path, encoding='cp1252', errors='replace'):
    ln = ln.rstrip('\n')
    m = re.match(r'\[(\d\d:\d\d:\d\d)[^\]]*\]\s+(.*)', ln)
    body = m.group(2) if m else ln
    if body.startswith('  cara '):
        fid = body[7:body.index(':')]
        faces[fid] = body
    elif body.startswith('lod caras:'):
        events.append((m.group(1) if m else '', body))

print('eventos:')
for t, b in events:
    if 'caras vistas' in b: continue
    print(' ', t, b)
print()
print('caras (ultimo resumen):')
for fid, b in faces.items():
    parts = b.split(' | ')
    print(' ', parts[0])
    for p in parts[1:]:
        if p.startswith('cuerpo'):
            print('     ', p.split(':')[0] + ':')
            for q in p.split(':', 1)[1].split(';'):
                if q.strip(): print('        ', q.strip())
        else:
            print('     ', p)
# resumen: cuantas cuentas distintas de cara por jugador (= niveles de detalle usados)
print()
for fid, b in faces.items():
    parts = [p for p in b.split(' | ')[1:] if ' idx VS ' in p and not p.startswith('cuerpo')]
    print(f'  {fid}: {len(parts)} malla(s) de cara distintas')
