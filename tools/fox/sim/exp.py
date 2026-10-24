# experimentos: python3 exp.py <log> <fmdl> <salida> <zona> vistas=a,b clase=e[,bz] ...  (solo esas clases, con esos valores)
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sim, compare
log, folder, out, zone = sys.argv[1:5]
over = {}; views = None; tab = None
for a in sys.argv[5:]:
    k, v = a.split('=')
    if k == 'vistas': views = [int(x) for x in v.split(',')]; continue
    p = [float(x) for x in v.split(',')]; over[k.replace('_', ' ')] = tuple(p)
z = compare.ZONES[zone]; cy, ppm, W, H, vv = z[:5]; CX = z[5] if len(z) > 5 else 0.0
views = views or vv
rows = sim.parse_table(); rest = sim.parse_rest(log)
ms = sim.load_meshes(folder); base = [m["pos"] for m in ms]
F = {}   # todo en cero salvo las clases pedidas
D = sim.bone_deltas(rows, rest, F, override=over)
mod = sim.deform(ms, D)
ims, lbs = [], []
for yv in views:
    ims += [sim.render(ms, base, yv, W, H, ppm, cy, cx=CX), sim.render(ms, mod, yv, W, H, ppm, cy, cx=CX)]
    lbs += ["juego %d" % yv, "mod %d" % yv]
sim.sheet(ims, lbs, out); print("ok", sorted(D)[:8], len(D))
