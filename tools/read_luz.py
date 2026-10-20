# PES21 FX - resume PES21FX\capturas\luz.txt (lo escribe la DLL en partido cada N segundos, junto con un jpg):
# por captura, elevacion y azimut del sol, color del sol (LightCB), exposicion, niebla y parametros del mod.
# Uso: read_luz.py <luz.txt> [salida.csv]   -> tabla ordenada por elevacion + promedios por franja de elevacion
import sys, re, csv, math
rows = []
pat = re.compile(r"(\d\d:\d\d:\d\d) \| (\S+) \| sol elev=([-\d.]+) az=([-\d.]+) dir=\(([^)]*)\) color0=\(([^)]*)\) color1=\(([^)]*)\) \| exposure=\(([^)]*)\) fog=\(([^)]*)\) fogP=\(([^)]*)\) \| mod: (.*)")
for l in open(sys.argv[1], encoding='cp1252', errors='replace'):
    m = pat.match(l.strip())
    if not m: continue
    t, name, el, az, d, c0, c1, ex, fog, fogP, mod = m.groups()
    c0 = [float(x) for x in c0.split()]; c1 = [float(x) for x in c1.split()]
    ex = [float(x) for x in ex.split()]; fog = [float(x) for x in fog.split()]
    lum = 0.2126 * c0[0] + 0.7152 * c0[1] + 0.0722 * c0[2]
    warm = c0[0] / max(c0[2], 1e-3)
    rows.append(dict(hora=t, jpg=name, elev=float(el), az=float(az), sol_r=c0[0], sol_g=c0[1], sol_b=c0[2], sol_lum=lum, sol_rb=warm,
                     amb_r=c1[0], amb_g=c1[1], amb_b=c1[2], exp0=ex[0], exp1=ex[1], exp2=ex[2], fog_r=fog[0], fog_g=fog[1], fog_b=fog[2], mod=mod))
rows.sort(key=lambda r: r['elev'])
print('%d capturas' % len(rows))
print('%-9s %6s %5s | %18s %7s %6s | %18s | %10s | %18s' % ('hora', 'elev', 'az', 'sol rgb', 'lum', 'r/b', 'amb rgb', 'exp z', 'niebla rgb'))
for r in rows:
    print('%-9s %6.1f %5.0f | %5.0f %5.0f %5.0f %7.0f %6.2f | %5.0f %5.0f %5.0f | %10.4g | %5.3f %5.3f %5.3f  %s' % (
        r['hora'], r['elev'], r['az'], r['sol_r'], r['sol_g'], r['sol_b'], r['sol_lum'], r['sol_rb'], r['amb_r'], r['amb_g'], r['amb_b'], r['exp2'], r['fog_r'], r['fog_g'], r['fog_b'], r['jpg']))
# promedios por franja de elevacion (0-10, 10-20, ...)
bands = {}
for r in rows:
    k = int(max(r['elev'], 0) // 10) * 10
    bands.setdefault(k, []).append(r)
print('\nfranjas de elevacion (promedios):')
for k in sorted(bands):
    b = bands[k]; n = len(b)
    avg = lambda f: sum(x[f] for x in b) / n
    print('  %2d-%2d deg: n=%d  sol lum %.0f  r/b %.2f  amb lum %.0f  exp z %.4g  niebla (%.3f %.3f %.3f)' % (
        k, k + 10, n, avg('sol_lum'), avg('sol_rb'), 0.2126 * avg('amb_r') + 0.7152 * avg('amb_g') + 0.0722 * avg('amb_b'), avg('exp2'), avg('fog_r'), avg('fog_g'), avg('fog_b')))
if len(sys.argv) > 2:
    with open(sys.argv[2], 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else ['hora']); w.writeheader(); w.writerows(rows)
    print('csv:', sys.argv[2])
