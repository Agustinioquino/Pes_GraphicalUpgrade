# variantes de la tabla sin tocar el C++: python3 variant.py <log> <fmdl> <salida> <zona> vistas=.. k:clase=kx,ky,kz a:clase=x,y,z slider=valor ...
import sys, os, copy
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sim, compare
log, folder, out, zone = sys.argv[1:5]
S = {}; views = None; kov = {}; aov = {}
for a in sys.argv[5:]:
    k, v = a.split('=')
    if k == 'vistas': views = [int(x) for x in v.split(',')]
    elif k.startswith('k:'): kov[k[2:].replace('_', ' ')] = [float(x) for x in v.split(',')]
    elif k.startswith('a:'): aov[k[2:].replace('_', ' ')] = [float(x) for x in v.split(',')]
    else: S[k] = float(v)
z = compare.ZONES[zone]; cy, ppm, W, H, vv = z[:5]; CX = z[5] if len(z) > 5 else 0.0
views = views or vv
rows = sim.parse_table()
rows2 = copy.deepcopy(rows)
for r in rows2:
    if r['name'] in kov: r['nums'][3:6] = kov[r['name']]
    if r['name'] in aov: r['nums'][0:3] = aov[r['name']]
rest = sim.parse_rest(log)
ms = sim.load_meshes(folder); base = [m["pos"] for m in ms]
F = sim.factors(S)
m1 = sim.deform(ms, sim.bone_deltas(rows, rest, F)); m2 = sim.deform(ms, sim.bone_deltas(rows2, rest, F))
ims, lbs = [], []
for yv in views:
    ims += [sim.render(ms, base, yv, W, H, ppm, cy, cx=CX), sim.render(ms, m1, yv, W, H, ppm, cy, cx=CX), sim.render(ms, m2, yv, W, H, ppm, cy, cx=CX)]
    lbs += ["juego %d" % yv, "tabla %d" % yv, "variante %d" % yv]
sim.sheet(ims, lbs, out); print("ok", out)
