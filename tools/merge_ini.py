# PES21 FX - fusiona el PES21FX.ini del repo (plantilla: grupos del menu, teclas, comentarios) con el del PC del usuario
# (valores guardados desde el menu). Uso: merge_ini.py <del PC> <del repo> <salida>
#   [Constants] y [PES21FX]: valor del PC si la clave existe alla, si no el del repo; las claves que solo estan en el PC
#   se agregan al final de la seccion. Todo lo demas ([Menu...], [Key...]) sale del repo. Salida en cp1252 con CRLF.
import sys, re

def read(p): return open(p, 'rb').read().decode('cp1252').replace('\r\n', '\n').split('\n')

def sections(lines):
    out = []; cur = None
    for l in lines:
        t = l.strip()
        if t.startswith('['):
            cur = (t[1:t.find(']')].strip().lower(), []); out.append(cur)
        elif cur is None: out.append((None, [l])) if not out or out[-1][0] is not None else out[-1][1].append(l)
        else: cur[1].append(l)
    return out

def keyvals(body):
    d = {}
    for l in body:
        t = l.strip()
        if not t or t.startswith(';') or t.startswith('#') or '=' not in t: continue
        k, v = t.split('=', 1); d[k.strip()] = v.strip()
    return d

# (20/10) --set clave=valor: fuerza un valor aunque el PC tenga otro (cambios deliberados del mod)
force = {}
args = [a for a in sys.argv[1:]]
while '--set' in args:
    i = args.index('--set'); k, v = args[i + 1].split('=', 1); force[k.strip()] = v.strip(); del args[i:i + 2]
sys.argv = [sys.argv[0]] + args
dev, rep = sections(read(sys.argv[1])), sections(read(sys.argv[2]))
devmap = {}
for name, body in dev:
    if name: devmap.setdefault(name, {}).update(keyvals(body))
merge_secs = {'constants', 'pes21fx'}
out = []; stats = {'kept': 0, 'new': 0, 'devonly': 0}
for name, body in rep:
    if name is None: out.extend(body); continue
    out.append('[' + ('Constants' if name == 'constants' else 'PES21FX' if name == 'pes21fx' else body and name) + ']' if False else None)
    out.pop()
    # el titulo original de la seccion (con mayusculas) esta en la primera linea del repo: lo recuperamos del texto
    out.append(None)  # marcador, se reemplaza abajo
    idx = len(out) - 1
    if name in merge_secs and name in devmap:
        dv = devmap[name]; seen = set(); nb = []
        for l in body:
            t = l.strip()
            if t and not t.startswith(';') and '=' in t:
                k, v = t.split('=', 1); k = k.strip()
                if k in force:
                    seen.add(k); stats['forced'] = stats.get('forced', 0) + 1
                    nb.append('%s = %s' % (k, force[k])); continue
                if k in dv:
                    seen.add(k)
                    if dv[k] != v.strip(): stats['kept'] += 1
                    nb.append('%s = %s' % (k, dv[k])); continue
                stats['new'] += 1
            nb.append(l)
        extra = [k for k in dv if k not in seen]
        if extra:
            while nb and nb[-1].strip() == '': nb.pop()
            nb.append('; valores guardados en el PC que ya no estan en la plantilla del mod')
            for k in extra: nb.append('%s = %s' % (k, dv[k])); stats['devonly'] += 1
            nb.append('')
        out[idx] = ('[%s]' % name, nb)
    else:
        out[idx] = ('[%s]' % name, body)

# reconstruir con los nombres de seccion originales del repo
rep_names = [l.strip() for l in read(sys.argv[2]) if l.strip().startswith('[')]
res = []; si = 0
for item in out:
    if isinstance(item, tuple):
        res.append(rep_names[si]); si += 1; res.extend(item[1])
    else: res.append(item)
txt = '\r\n'.join(res)
if not txt.endswith('\r\n'): txt += '\r\n'
open(sys.argv[3], 'wb').write(txt.encode('cp1252'))
print('ok: %d valores del PC conservados (distintos del repo), %d claves nuevas del repo, %d solo del PC, %d forzados' % (stats['kept'], stats['new'], stats['devonly'], stats.get('forced', 0)))
