# Comparaciones de cerca para afinar la musculatura: python3 compare.py <log> <fmdl> <salida.png> <zona> clave=valor ...
# zona: piernas | rodillas | pecho | hombros | espalda | gluteos | brazos | todo
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sim
ZONES = {   # (centro y, pixeles por metro, ancho, alto, vistas)
    "piernas": (0.80, 900, 520, 640, [0, 90, 180]),
    "rodillas": (0.60, 1500, 520, 520, [0, 90, 180, 30]),
    "pecho": (1.35, 1300, 600, 520, [0, 30, 60, 90]),
    "hombros": (1.40, 1200, 760, 520, [0, 45, 90, 180]),
    "espalda": (1.30, 1000, 620, 640, [180, 150, 120]),
    "gluteos": (0.95, 1200, 520, 600, [180, 135, 90]),
    "brazos": (1.25, 900, 520, 640, [90, 0, 270]),
    "todo": (1.05, 520, 420, 760, [0, 90, 180, 35]),
    "hombro": (1.40, 2400, 520, 480, [0, 40], 0.18),
    "rodilla": (0.56, 2400, 520, 520, [0, 90], 0.14),
}
if __name__ == "__main__":
    log, folder, out, zone = sys.argv[1:5]
    S = {}; only = None
    for a in sys.argv[5:]:
        k, v = a.split('=')
        if k == 'vistas': only = [int(x) for x in v.split(',')]; continue
        S[k] = float(v)
    z = ZONES[zone]; cy, ppm, W, H, views = z[:5]; CX = z[5] if len(z) > 5 else 0.0
    if only: views = only
    rows = sim.parse_table(); rest = sim.parse_rest(log)
    ms = sim.load_meshes(folder)
    base = [m["pos"] for m in ms]
    mod = sim.deform(ms, sim.bone_deltas(rows, rest, sim.factors(S)))
    ims, lbs = [], []
    for yv in views:
        ims += [sim.render(ms, base, yv, W, H, ppm, cy, cx=CX), sim.render(ms, mod, yv, W, H, ppm, cy, cx=CX)]
        lbs += ["juego %d" % yv, "mod %d" % yv]
    sim.sheet(ims, lbs, out)
    print("ok", out)
