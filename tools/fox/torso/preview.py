# Vista previa de las variantes del torso (sin el juego): forma grande + relieve del modelo por vertice (torso_variants.py).
# Uso: python3 preview.py <carpeta common de TorsoBody> <salida.png> [t1 t2 ...]   (t: -1 robusto .. 1 atletico)
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..', 'sim'))
import sim
from torso_variants import build, build_arm, per_vertex, arm_bone_factors

def variant_verts(ms, T, t, A=None, rows=None, rest=None):
    """posiciones de todas las mallas para la variante t (T = torso, A = brazos; rows / rest: huesos para el grosor del brazo)"""
    wr, wa = max(-t, 0.0), max(t, 0.0)
    dpv = per_vertex(T, wr * T["dR"] + wa * T["dA"])
    apv = per_vertex(A, wr * A["dR"] + wa * A["dA"]) if A is not None else None
    D = sim.bone_deltas(rows, rest, arm_bone_factors(t)) if (rows is not None and t != 0) else {}
    out = []
    for m in ms:
        v = m["pos"].copy()
        if m["file"] == "pants_%s_sub" % T["version"] and m.get("mi", -1) in (1, 2, 3):
            v = v + dpv[m["mi"] - 1]
        if apv is not None and m["file"] == A["version"] and m.get("mi", -1) == 0:
            v = v + apv[0]
        if D:
            mm = dict(m); mm["pos"] = v; v = sim.deform([mm], D)[0]
        out.append(v)
    return out

def load(folder, version="out"):
    files = ("pants_%s_sub" % version, "arm", "neck", "thigh_short")
    ms = []
    for fn in files:
        p = os.path.join(folder, fn + ".fmdl")
        f = sim.Fmdl(p); names = [b["name"] for b in f.bones]
        for i, m in enumerate(sim.meshes(f)):
            if m["faces"] is None or m["pos"] is None or m["idx"] is None or len(m["faces"]) < 10: continue
            if fn.startswith("arm") and i > 0: continue   # capa de suciedad (en el juego es transparente si no hay barro)
            ms.append(dict(file=fn, mi=i, pos=m["pos"].astype(np.float64), w=m["w"], idx=m["idx"], names=names, faces=m["faces"]))
    return ms

def main():
    folder, outp = sys.argv[1:3]
    args = sys.argv[3:]
    log = None
    if args and not args[0].lstrip('-').replace('.', '').isdigit(): log = args.pop(0)
    ts = [float(a) for a in args] or [-1.0, 0.0, 1.0]
    rows = sim.parse_table() if log else None; rest = sim.parse_rest(log) if log else None
    T = build(folder, "out"); A = build_arm(folder, "arm")
    ms = load(folder, "out")
    ims, lbs = [], []
    for yv in (0, 90, 180, 35):
        for t in ts:
            vs = variant_verts(ms, T, t, A, rows, rest)
            ims.append(sim.render(ms, vs, yv, W=300, H=470, ppm=470, cy=1.24))
            lbs.append("%s %d" % ({-1.0: "robusto", 0.0: "TorsoBody", 1.0: "atletico"}.get(t, "t=%.1f" % t), yv))
    sim.sheet(ims, lbs, outp)
    print("ok", outp)

if __name__ == "__main__":
    main()
