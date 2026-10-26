# PES21 FX (26/10) - vista previa de los selectores de hombros del menu Fisico (copia de las cuentas de ComputeFactors para la
# fila 287 de musculo.cpp: inclinacion, tipo y tamano) con sim.py.
# Uso: python3 hombros.py <log del juego con los reposos> <carpeta con los fmdl> <salida.png>   (CASES=caidos,normal,... YAW=0)
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sim
log, folder, out = sys.argv[1:4]
rows = sim.parse_table(); rest = sim.parse_rest(log)
ms = sim.load_meshes(folder); base = [m["pos"] for m in ms]
def F_for(sl=1.0, ty=1, sz=1.0, anch0=0.0):
    F = {k: [0.0, np.zeros(3)] for k in ["M_DELT","M_TRAP","M_COLLAR","M_CLAV","M_SCAP"]}
    def push(k, x, y, z, metros, e):
        d = np.array([x, y, z], float); n = np.linalg.norm(d)
        if n > 1e-6 and e != 0: F[k][1] = F[k][1] + d / n * metros * e
    shSl, shSz = sl - 1, sz - 1
    tyAnch = -0.45 if ty == 2 else 0.50 if ty == 3 else 0.0
    tyE = -0.08 if ty == 2 else 0.04 if ty == 3 else 0.18 if ty == 4 else 0.0
    tyRound = 0.45 if ty == 4 else 0.0
    anch = anch0 + tyAnch + 0.8 * shSz
    F["M_DELT"][0] = tyE + 0.8 * shSz; push("M_DELT", 1, 0.12, 0, 0.030, anch)
    if shSl:
        push("M_DELT", 0.15, 1, 0, 0.040, shSl); push("M_DELT", 1, 0, 0, 0.010, shSl)
        push("M_TRAP", -0.20, 1, 0, 0.020, -shSl); push("M_COLLAR", 0, 1, 0, 0.010, -shSl)
        push("M_CLAV", 0.5, 1, 0, 0.015, shSl); push("M_SCAP", 0, 1, 0, 0.012, shSl)
    if shSz:
        push("M_TRAP", 0.25, 1, -0.35, 0.030, 0.5 * shSz); push("M_CLAV", 0.10, 0.30, 1, 0.025, 0.4 * shSz)
        push("M_SCAP", 0.20, 0.10, -1, 0.030, 0.3 * shSz)
    df = dr = tyRound
    F["M_DELT"][0] += 0.35 * (df + dr)
    push("M_DELT", 0.15, 0, 1, 0.015, df); push("M_DELT", 0.15, 0, -1, 0.015, dr)
    push("M_CLAV", 1, 0, 0, 0.012, anch); push("M_TRAP", 1, 0, 0, 0.012, anch)
    return {k: (v[0], np.asarray(v[1], float)) for k, v in F.items()}
cases = [("caidos 0.7", dict(sl=0.7)), ("normal", dict()), ("rectos 1.3", dict(sl=1.3)),
         ("estrechos", dict(ty=2)), ("anchos", dict(ty=3)), ("redondos", dict(ty=4)), ("chicos 0.7", dict(sz=0.7)), ("grandes 1.3", dict(sz=1.3))]
sel = os.environ.get("CASES")
if sel: cases = [c for c in cases if c[0].split()[0] in sel.split(",")]
yaw = float(os.environ.get("YAW", "0"))
ims, lbs = [], []
for name, kw in cases:
    D = sim.bone_deltas(rows, rest, F_for(**kw))
    mod = sim.deform(ms, D)
    ims.append(sim.render(ms, mod, yaw, W=560, H=330, ppm=620.0, cy=1.40)); lbs.append(name)
sim.sheet(ims, lbs, out)
print("ok", out)
