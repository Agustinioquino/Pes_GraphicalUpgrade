# Previsualiza la musculatura (musculo.cpp) sobre los modelos del cuerpo en la pose de reposo, sin abrir el juego.
# Lee la tabla de clases de src/musculo.cpp, los reposos reales de los huesos del log del juego (lineas "musculatura:
# reposo ...") y los FMDL del cuerpo; deforma los vertices como lo haria el skinning con los huesos escalados y dibuja
# vistas (frente, costado, espalda, 3/4) con un rasterizador chico en C (rast.c).
# Uso: python3 sim.py <log> <carpeta con los fmdl> <salida.png> clave=valor ...   (claves: las de UpdateFactors, p. ej.
#      muslos=1.45 pantorrillas=1.5 gluteos=1.5 pecho=1.5 general=1.2 ...)
import sys, os, re, struct, subprocess, math
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
from fmdl import Fmdl, meshes

REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
RAST = os.environ.get('RAST', os.path.join(HERE, 'rast'))

# clase (nombre en musculo.cpp) -> hueso (sin _l / _r)
BONE = {
    "deltoides": "dsk_deltoid", "brazo": "dsk_upperarm", "brazo piel": "dsk_upperarm_skin", "brazo piel 2": "dsk_upperarm_skin_t",
    "manga": "dsk_sleeve", "antebrazo": "dsk_forearm_t", "antebrazo 2": "dsk_forearm", "codo": "dsk_elbow", "muneca": "dsk_wrist",
    "pectoral": "dsk_pectoralis", "pecho alto": "dsk_clavicle", "axila": "dsk_underarm", "costado": "dsk_sternum",
    "dorsal": "dsk_underarm_ba", "omoplato": "dsk_scapula", "trapecio": "dsk_trapezius", "base del cuello": "dsk_collar",
    "espalda baja": "dsk_back", "lumbar": "dsk_belly_ba", "abdominal": "dsk_belly_f", "oblicuo": "dsk_belly_o",
    "panza": "dsk_belly_scale", "gluteo": "dsk_hip", "muslo": "dsk_thighmain", "aductor": "dsk_thigh", "pantorrilla": "dsk_leg",
    "rodilla": "dsk_knee", "corva": "dsk_kneeback", "borde del short": "dsk_hem", "short": "dsk_pants", "cuello": "dsk_scm",
    "nuca": "dsk_neckback",
}
# hijos que heredan la transformacion del padre (restricciones con destino = ese hueso)
CHILD = {"dsk_hem": ["dsk_hem_f", "dsk_hem_i", "dsk_hem_ba", "dsk_hem_o", "dsk_hem_o_fake", "dsk_hem_i_fake"],
         "dsk_sleeve": ["dsk_sleeve_0%d" % i for i in range(1, 7)]}

def parse_table():
    src = open(os.path.join(REPO, 'src', 'musculo.cpp'), encoding='utf-8').read()
    rows = []
    for m in re.finditer(r'\{ "([^"]+)",\s*0x[0-9a-f]+ull, 0x[0-9a-f]+ull, 0x[0-9a-f]+ull, (M_\w+),\s*(\d), ([^}]*)\}', src):
        nums = [float(x.strip().rstrip('f')) for x in m.group(4).split(',') if x.strip()]
        rows.append(dict(name=m.group(1), mus=m.group(2), side=int(m.group(3)), nums=nums))
    return rows

def parse_rest(log):
    txt = open(log, encoding='cp1252', errors='replace').read()
    rest = {}
    for m in re.finditer(r'reposo (.+?) (izq|der) \(lado por \w+\) \| hueso ok t\(([-\d. ]+)\) q\(([-\d. ]+)\) s\(([-\d. ]+)\)', txt):
        rest[(m.group(1), m.group(2))] = tuple(np.array([float(x) for x in g.split()]) for g in m.group(3, 4, 5))
    return rest

def qrot(q, v):   # q = (x, y, z, w); v: (..., 3)
    u = np.asarray(q[:3]); w = q[3]
    t = 2 * np.cross(u, v)
    return v + w * t + np.cross(u, t)
def qinv(q): return np.array([-q[0], -q[1], -q[2], q[3]])

def factors(S):
    """copia de UpdateFactors() (musculo.cpp): S = valores de los sliders (1 = como el juego). Devuelve {M_X: (e, t[3])}"""
    g = lambda k: min(max(float(S.get(k, 1.0)), 0.5), 2.0)
    G = g('general')
    m = lambda v: min(max(v * G, 0.4), 2.5) - 1.0
    ropa = S.get('ropa', 1) > 0.5; trans = S.get('trans', 1) > 0.5
    esp, mus = g('espalda'), g('muslos')
    bic, tri = m(g('biceps')), m(g('triceps')); arm = 0.5 * (bic + tri); armB = 0.044 * 0.5 * (bic - tri)
    fore = m(g('antebrazos'))
    quad, ham = m(mus * g('cuadriceps')), m(mus * g('isquio')); thigh = 0.5 * (quad + ham); thighB = 0.090 * 0.5 * (quad - ham)
    calf, trap, neck = m(g('pantorrillas')), m(esp * g('trapecio')), m(g('cuello'))
    pec, clav = m(g('pecho')), m(g('pecho') * g('pecho_alto'))
    dor, omo, baja = m(esp * g('dorsales')), m(esp * g('omoplatos')), m(esp * g('baja'))
    rod, cint, cad, anch = g('rodillas') - 1, g('cintura') - 1, g('caderas') - 1, g('ancho') - 1
    F = {k: [0.0, np.zeros(3)] for k in ["M_DELT","M_ARM","M_FORE","M_ELBOW","M_WRIST","M_SLEEVE","M_PECT","M_CLAV","M_UNDERARM",
         "M_RIB","M_LAT","M_SCAP","M_TRAP","M_COLLAR","M_BACK","M_LUMB","M_ABS","M_OBL","M_BELLY","M_GLUTE","M_THIGH","M_ADD",
         "M_CALF","M_KNEE","M_HEM","M_PANTS","M_NECK"]}
    def push(k, x, y, z, metros, e):
        d = np.array([x, y, z], float); n = np.linalg.norm(d)
        if n > 1e-6 and e != 0: F[k][1] = F[k][1] + d / n * metros * e
    F["M_DELT"][0] = m(g('hombros')); push("M_DELT", 1, 0.12, 0, 0.030, anch)
    delt = F["M_DELT"][0]; push("M_TRAP", 0.6, 0.8, 0, 0.012, delt); push("M_CLAV", 0.7, 0.5, 0.3, 0.010, delt); push("M_SCAP", 0.5, 0.3, -0.6, 0.008, delt)
    F["M_ARM"][0] = arm; F["M_ARM"][1] = np.array([0, 0, armB])
    F["M_FORE"][0] = fore
    F["M_ELBOW"][0] = 0.4 * 0.5 * (arm + fore) if trans else 0
    F["M_WRIST"][0] = 0.3 * fore if trans else 0
    if ropa and arm > 0: F["M_SLEEVE"][0] = 1.1 * arm; F["M_SLEEVE"][1] = np.array([0, 0, 1.1 * armB])
    push("M_PECT", 0.25, 0.10, 1, 0.035, pec)
    push("M_CLAV", 0.10, 0.30, 1, 0.025, clav); push("M_CLAV", 1, 0, 0, 0.012, anch)
    push("M_UNDERARM", 0.20, 0, 1, 0.020, 0.6 * pec)
    push("M_RIB", 1, 0, 0.10, 0.025, 0.6 * dor + 0.3 * cint)
    push("M_LAT", 1, -0.10, -0.40, 0.035, dor)
    push("M_SCAP", 0.20, 0.10, -1, 0.030, omo)
    push("M_TRAP", 0.25, 1, -0.35, 0.030, trap); push("M_TRAP", 1, 0, 0, 0.012, anch)
    push("M_COLLAR", 0.30, 1, 0, 0.015, 0.5 * neck + 0.3 * trap)
    push("M_BACK", 0, 0, -1, 0.025, baja)
    push("M_LUMB", 0.40, 0, -1, 0.025, baja); push("M_LUMB", 1, 0, 0, 0.020, cint)
    push("M_ABS", 0.10, 0, 1, 0.020, m(g('abdominales')))
    push("M_OBL", 1, 0, 0.15, 0.030, m(g('oblicuos'))); push("M_OBL", 1, 0, 0.10, 0.030, cint)
    push("M_BELLY", 0, -0.10, 1, 0.050, g('panza') - 1)
    push("M_GLUTE", 0.25, -0.25, -1, 0.030, m(g('gluteos'))); push("M_GLUTE", 1, 0, 0, 0.025, cad)
    F["M_THIGH"][0] = thigh; F["M_THIGH"][1] = np.array([0, 0, thighB])
    F["M_ADD"][0] = m(mus * g('aductores')); push("M_ADD", -1, 0, 0.10, 0.020, F["M_ADD"][0])
    F["M_CALF"][0] = calf; push("M_CALF", 0, 0, -1, 0.015, calf)
    F["M_KNEE"][0] = rod + (0.3 * 0.5 * (thigh + calf) if trans else 0)
    if ropa and thigh > 0:
        F["M_HEM"][0] = 0.7 * thigh; F["M_HEM"][1] = np.array([0, 0, 0.8 * thighB])
        F["M_PANTS"][0] = 0.5 * thigh
    if ropa and cad > 0: push("M_PANTS", 1, 0, 0, 0.020, cad)
    F["M_NECK"][0] = neck
    return {k: (v[0], np.asarray(v[1], float)) for k, v in F.items()}

def bone_deltas(rows, rest, F, override=None):
    """por hueso (nombre con _l / _r): funcion v (N,3) -> desplazamiento (N,3) en la pose de reposo"""
    out = {}
    for r in rows:
        e, T = F.get(r['mus'], (0.0, np.zeros(3)))
        if override and r['name'] in override:
            o = override[r['name']]; e = o[0]; T = np.array(o[1:4]) if len(o) >= 4 else np.zeros(3)
        if e == 0 and not np.any(T): continue
        ax, ay, az, kx, ky, kz = r['nums'][:6]
        sides = [('izq', 1.0, '_l'), ('der', -1.0, '_r')] if r['side'] != 0 else [('izq', 1.0, '')]
        for sname, sx, suf in sides:
            key = (r['name'], sname)
            if key not in rest: continue
            t, q, s = rest[key]
            a = np.array([ax * sx, ay, az])
            u_a = qrot(qinv(q), a - t) / s
            f = np.clip(np.array([1 + kx * e, 1 + ky * e, 1 + kz * e]), 0.4, 2.5)
            b_u = qrot(qinv(q), np.array([T[0] * sx, T[1], T[2]])) / s
            bone = BONE[r['name']] + suf
            def D(v, t=t, q=q, s=s, f=f, u_a=u_a, b_u=b_u):
                u = qrot(qinv(q), v - t) / s
                return qrot(q, ((f - 1) * (u - u_a) + b_u) * s)
            out[bone] = D
            for c in CHILD.get(BONE[r['name']], []): out[c + suf] = D
    return out

def load_meshes(folder, files=("pants_in_sub", "arm", "neck", "thigh_middle")):
    res = []
    for fn in files:
        p = os.path.join(folder, fn + ".fmdl")
        if not os.path.exists(p): continue
        f = Fmdl(p)
        names = [b["name"] for b in f.bones]
        for m in meshes(f):
            if m["faces"] is None or m["pos"] is None or m["idx"] is None or len(m["faces"]) < 10: continue
            res.append(dict(file=fn, pos=m["pos"].astype(np.float64), w=m["w"], idx=m["idx"], names=names, faces=m["faces"]))
    return res

def deform(ms, D):
    out = []
    for m in ms:
        v = m["pos"].copy(); dv = np.zeros_like(v)
        for k in range(4):
            w = m["w"][:, k]
            for bi in np.unique(m["idx"][:, k]):
                nm = m["names"][bi]
                if nm not in D: continue
                sel = (m["idx"][:, k] == bi) & (w > 0)
                if sel.any(): dv[sel] += w[sel, None] * D[nm](v[sel])
        out.append(v + dv)
    return out

def normals(v, f):
    n = np.zeros_like(v)
    fn = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
    for k in range(3): np.add.at(n, f[:, k], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)

def render(ms, verts, yaw_deg, W=420, H=760, ppm=520.0, cy=1.05, tmp=None, cx=0.0):
    tmp = tmp or os.path.join(os.environ.get('SIM_TMP', '/tmp'), '_sim%d' % os.getpid())
    th = math.radians(yaw_deg)
    R = np.array([[math.cos(th), 0, math.sin(th)], [0, 1, 0], [-math.sin(th), 0, math.cos(th)]])   # gira el cuerpo alrededor de Y
    tris = []
    for m, v in zip(ms, verts):
        n = normals(v, m["faces"])
        p = v @ R.T; nn = n @ R.T
        x = W / 2 + (p[:, 0] - cx) * ppm; y = H / 2 - (p[:, 1] - cy) * ppm; z = p[:, 2]
        arr = np.stack([x, y, z, nn[:, 0], -nn[:, 1], nn[:, 2]], axis=1).astype(np.float32)
        tris.append(arr[m["faces"]].reshape(-1, 6))
    allt = np.concatenate(tris)
    L = np.array([0.35, -0.45, 0.82], np.float32); L /= np.linalg.norm(L)
    with open(tmp + '.bin', 'wb') as fo:
        fo.write(struct.pack('<iii', W, H, len(allt) // 3)); fo.write(L.tobytes()); fo.write(allt.tobytes())
    subprocess.run([RAST, tmp + '.bin', tmp + '.pgm'], check=True)
    return Image.open(tmp + '.pgm').convert('L')

def sheet(images, labels, out):
    W = sum(i.width for i in images); H = max(i.height for i in images) + 24
    s = Image.new('L', (W, H), 30)
    from PIL import ImageDraw
    d = ImageDraw.Draw(s); x = 0
    for im, lb in zip(images, labels):
        s.paste(im, (x, 24)); d.text((x + 6, 5), lb, fill=230); x += im.width
    s.save(out)

if __name__ == "__main__":
    log, folder, out = sys.argv[1:4]
    S = {}
    for a in sys.argv[4:]:
        k, v = a.split('='); S[k] = float(v)
    rows = parse_table(); rest = parse_rest(log)
    ms = load_meshes(folder)
    base = [m["pos"] for m in ms]
    D = bone_deltas(rows, rest, factors(S))
    mod = deform(ms, D)
    views = [0, 90, 180, 35]
    ims, lbs = [], []
    for yv in views:
        ims += [render(ms, base, yv), render(ms, mod, yv)]
        lbs += ["juego %d" % yv, "mod %d" % yv]
    sheet(ims, lbs, out)
    print("ok", out, "huesos movidos:", len(D))
