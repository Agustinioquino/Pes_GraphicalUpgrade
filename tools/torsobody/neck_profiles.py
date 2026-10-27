# PES21 FX (26/10 f) - forma del cuello de las caras reales (Konami) contra el borde del cuello de los torsos de TorsoBody.
# TorsoBody_cuello.lua 1.10 (PERFIL = true) anota en sider.log, una vez por cara:
#   "TorsoBody cuello: perfil <clave> (radio mm a +3/+2/+1/0/-1 cm del borde / baja mm): phi:r3,r2,r1,r0,r-1/low ..."
# con phi = -180 .. 165 de a 15 grados (atan2(x, z - AXZ): 0 = adelante, 90 = izquierda del jugador... +x), r = distancia
# horizontal desde el eje (0, y, AXZ) a la malla de la cara a esa altura sobre el borde del torso (loop_y(phi) + h).
# Aca: el promedio (mediana) de las caras, el del cuello generico de TorsoBody (neck.fmdl) y el del torso (el borde = LOOP y la
# superficie del torso 1 .. 4 cm por debajo), por angulo. Lo que falta para que el torso siga al cuello de Konami.
# Uso: python3 neck_profiles.py <sider.log> <TorsoBody common> [salida.json]
import sys, os, re, json
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'fox')); sys.path.insert(0, os.path.join(HERE, '..', 'fox', 'torso'))

AXZ = 0.045
PHIS = np.arange(-180, 180, 15)
HS = np.array([0.03, 0.02, 0.01, 0.0, -0.01])

def lua_loop(path=os.path.join(HERE, "TorsoBody_cuello.lua")):
    s = open(path, encoding="latin1").read()
    def table(name):
        body = s[s.index("local %s = {" % name):]
        body = body[:body.index("\n}")]
        return np.array([[float(v) for v in t.split(",")] for t in re.findall(r"\{([-0-9., ]+)\}", body)])
    return table("LOOP"), table("RING_N")

LOOP, RING_N = lua_loop()
LPH = np.degrees(np.arctan2(LOOP[:, 0], LOOP[:, 2] - AXZ))

def loop_y(phi):
    """como en el Lua: interseccion del plano vertical por el eje en el angulo phi con el segmento del borde"""
    phi = float(phi); NL = len(LOOP)
    if phi < LPH[0] or phi >= LPH[-1]: i = NL - 1
    else: i = int(np.searchsorted(LPH, phi, side="right") - 1)
    a, b = LOOP[i], LOOP[(i + 1) % NL]
    r = np.radians(phi); c, s_ = np.cos(r), np.sin(r)
    sa = a[0] * c - (a[2] - AXZ) * s_; sb = b[0] * c - (b[2] - AXZ) * s_
    t = sa / (sa - sb) if abs(sa - sb) > 1e-15 else 0.0
    return a[1] + t * (b[1] - a[1]), np.hypot((a + t * (b - a))[0], (a + t * (b - a))[2] - AXZ)

def parse_log(path):
    faces = {}
    rx = re.compile(r"TorsoBody cuello: perfil (\S+) \(.*?\): (.*)$")
    for line in open(path, encoding="latin1", errors="replace"):
        m = rx.search(line)
        if not m: continue
        key, body = m.group(1), m.group(2).strip()
        if body.startswith("error"): continue
        R = np.full((len(PHIS), len(HS)), np.nan); low = np.full(len(PHIS), np.nan)
        for tok in body.split():
            ph, rest = tok.split(":", 1)
            vals, lo = rest.split("/")
            k = int((int(ph) + 180) // 15)
            for j, v in enumerate(vals.split(",")):
                if v != "-": R[k, j] = float(v) / 1000
            if lo != "-": low[k] = float(lo) / 1000
        faces[key] = dict(R=R, low=low)
    return faces

def ray_radius(P, F, phi, y):
    """distancia mas cercana (> 5 mm) desde (0, y, AXZ) en la direccion horizontal phi a la malla (P, F)"""
    r = np.radians(phi); d = np.array([np.sin(r), 0.0, np.cos(r)]); o = np.array([0.0, y, AXZ])
    a = P[F[:, 0]]; e1 = P[F[:, 1]] - a; e2 = P[F[:, 2]] - a
    p = np.cross(d, e2); det = np.einsum("ij,ij->i", e1, p)
    ok = np.abs(det) > 1e-14
    inv = np.where(ok, 1 / np.where(ok, det, 1), 0)
    t = o - a; u = np.einsum("ij,ij->i", t, p) * inv
    q = np.cross(t, e1); v = (q @ d) * inv; s = np.einsum("ij,ij->i", e2, q) * inv
    hit = ok & (u >= 0) & (u <= 1) & (v >= 0) & (u + v <= 1) & (s > 0.005)
    return float(s[hit].min()) if hit.any() else np.nan

def mesh_profile(P, F, hs):
    out = np.full((len(PHIS), len(hs)), np.nan)
    for i, ph in enumerate(PHIS):
        y0, _ = loop_y(ph)
        for j, h in enumerate(hs):
            out[i, j] = ray_radius(P, F, ph, y0 + h)
    return out

def torso_profiles(common):
    from torso_mesh import load_torso, TORSO_FILES
    res = {}
    for ver in ("out", "in", "inner"):
        if not os.path.exists(os.path.join(common, TORSO_FILES[ver] + ".fmdl")): continue
        T = load_torso(common, ver)
        res[ver] = mesh_profile(T["Pw"], T["Fw"], np.array([-0.0005, -0.01, -0.02, -0.03, -0.04]))
    return res

def generic_neck(common):
    from fmdl import Fmdl, meshes
    p = os.path.join(common, "neck.fmdl")
    if not os.path.exists(p): return None
    M = meshes(Fmdl(p))
    P = np.concatenate([m["pos"] for m in M]).astype(np.float64); off = 0; F = []
    for m in M: F.append(m["faces"] + off); off += m["nvert"]
    return mesh_profile(P, np.concatenate(F), HS)

if __name__ == "__main__":
    log, common = sys.argv[1], sys.argv[2]
    faces = parse_log(log) if os.path.exists(log) else {}
    ring = np.array([loop_y(p)[1] for p in PHIS])
    tor = torso_profiles(common)
    gen = generic_neck(common)
    print("caras con perfil: %d" % len(faces))
    if faces:
        A = np.stack([f["R"] for f in faces.values()])          # (caras, 24, 5)
        med = np.nanmedian(A, 0); q1 = np.nanpercentile(A, 25, 0); q3 = np.nanpercentile(A, 75, 0)
        lowm = np.nanmedian(np.stack([f["low"] for f in faces.values()]), 0)
    print("phi  borde | torso(out) -1 -2 -3 -4 cm | generico +3 +2 +1 0 | caras mediana +3 +2 +1 0 -1 (rango 25-75 en 0) | baja")
    for i, ph in enumerate(PHIS):
        t = tor.get("out")
        s = "%4d %5.1f | %s" % (ph, ring[i] * 1000, " ".join("%5.1f" % (v * 1000) for v in t[i, 1:]) if t is not None else "-")
        if gen is not None: s += " | " + " ".join("%5.1f" % (v * 1000) for v in gen[i, :4])
        if faces: s += " | " + " ".join("%5.1f" % (v * 1000) for v in med[i]) + " (%4.1f-%4.1f) | %5.1f" % (q1[i, 3] * 1000, q3[i, 3] * 1000, lowm[i] * 1000)
        print(s)
    if len(sys.argv) > 3:
        json.dump(dict(phis=PHIS.tolist(), ring=ring.tolist(), torso={k: v.tolist() for k, v in tor.items()},
                       generic=None if gen is None else gen.tolist(),
                       faces={k: dict(R=np.nan_to_num(f["R"], nan=-1).tolist(), low=np.nan_to_num(f["low"], nan=-1).tolist()) for k, f in faces.items()}),
                  open(sys.argv[3], "w"))
