# PES21 FX (08/11, Agustin: "mas mejoras en el rostro y las expresiones, asi como transcribir a .gani") - pasa los GESTOS DEL MOD
# (src/exprbones.cpp: sliders Enojo / Sonrisa / Asombro / Grito / Puchero / Hablar / Abrir la boca / Mostrar dientes / cejas) a clips
# .gani de la cara que el JUEGO reproduce solo, sin la DLL (livecpk ...\common\anime\FoxAnim\Face\Animations\Base\).
#
# Las tablas de abajo son COPIA de exprbones.cpp (kPose, kPose2, kOpen, kTeeth, Secondary kOff, geU / geL de EyesFrame, mandibula
# 0.40 rad x apertura, hablar). Si cambian alla, cambiar aca. Ejes de la DLL: cabeza (x izquierda del jugador, y arriba, z adelante),
# metros. Ejes del .gani: skf_head (x arriba, y atras, z derecha), CENTIMETROS (inverso de GaniPToHead / GaniQToHead de exprbones.cpp).
#
# Dos modos:
#   gani_gestos.py --mod  carpeta_konami salida/          clips propios zfx_*.gani (base_normal + el gesto del mod a 1.0)
#   gani_gestos.py --sumar gesto peso clip.gani salida.gani  un clip de Konami + el gesto del mod encima (como el slider en el juego)
# carpeta_konami = donde estan base_normal.gani, face_anim_skel.ask y face_skel.frig (/mnt/project-files/face/gani/).
#
# BOCA (regla fija): no se toca mouth.fmdl / PS / pes_oral_*. Lo unico que mueve la boca es skf_jaw, como los clips de Konami; el giro
# se limita a TOPE_MANDIBULA (pain_brwtrb_shut_eyhc_hard de Konami llega a 19.2 grados). Sin deslizamiento del condilo: skf_jaw no
# tiene canal de posicion en el rig y correr solo los labios los separaria de los dientes.
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gani_cara as GC, gani_escribir as GE

TOPE_MANDIBULA = math.radians(19.0)
CUADROS = 42          # como smil_soft: pose sostenida, el juego mezcla entre clips

# indices de kDef (exprbones.cpp) -> nombre
KDEF = ['skf_jaw', 'skf_lip_b_l', 'skf_lip_b_r', 'skf_lip_b_c', 'skf_doublechin', 'skf_lip_s_l', 'skf_lip_s_r', 'skf_cheek_s_l',
        'skf_cheek_s_r', 'skf_lip_t_l', 'skf_lip_t_c', 'skf_lip_t_r', 'skf_lip_volume', 'skf_nose', 'skf_nosewing_l', 'skf_nosewing_r',
        'skf_nasolabialfold_t_l', 'skf_nasolabialfold_t_r', 'skf_cheek_l', 'skf_cheek_r', 'skf_orbicularisoculi_b_l',
        'skf_orbicularisoculi_b_r', 'skf_eyelid_t_l', 'skf_eyelid_t_r', 'skf_eyelid_b_l', 'skf_eyelid_b_r', 'skf_brow_i_l',
        'skf_brow_i_r', 'skf_brow_o_l', 'skf_brow_o_r', 'skf_glabella']
JAWW = {0: 1.0, 1: 1.0, 2: 1.0, 3: 1.0, 4: 1.0, 5: 0.5, 6: 0.5, 7: 0.5, 8: 0.5}   # kDef[].jaw

K_TEETH = {1: (0.0005, -0.0030, 0.0004), 2: (-0.0005, -0.0030, 0.0004), 3: (0.0, -0.0035, 0.0005), 5: (0.0015, 0.0008, -0.0010),
           6: (-0.0015, 0.0008, -0.0010), 9: (0.0003, 0.0030, 0.0004), 10: (0.0, 0.0035, 0.0005), 11: (-0.0003, 0.0030, 0.0004),
           12: (0.0, 0.0030, 0.0005), 14: (0.0003, 0.0006, 0.0), 15: (-0.0003, 0.0006, 0.0), 16: (0.0004, 0.0010, 0.0), 17: (-0.0004, 0.0010, 0.0)}
K_OPEN = {1: (-0.0012, 0.0, 0.0008), 2: (0.0012, 0.0, 0.0008), 3: (0.0, 0.0, 0.0010), 5: (-0.0030, -0.0004, 0.0010), 6: (0.0030, -0.0004, 0.0010),
          7: (-0.0010, -0.0012, 0.0), 8: (0.0010, -0.0012, 0.0), 9: (-0.0008, 0.0008, 0.0004), 10: (0.0, 0.0010, 0.0005), 11: (0.0008, 0.0008, 0.0004)}
# kPose: d -> (enojo, sonrisa, asombro)
K_POSE = {
    1: ((0, 0, 0), (0.0010, -0.0004, 0), (0, 0, 0)), 2: ((0, 0, 0), (-0.0010, -0.0004, 0), (0, 0, 0)),
    5: ((0.0004, -0.0022, 0), (0.0030, 0.0030, -0.0010), (-0.0015, 0, 0.0005)), 6: ((-0.0004, -0.0022, 0), (-0.0030, 0.0030, -0.0010), (0.0015, 0, 0.0005)),
    7: ((0, -0.0006, 0), (0.0010, 0.0015, 0), (0, 0, 0)), 8: ((0, -0.0006, 0), (-0.0010, 0.0015, 0), (0, 0, 0)),
    9: ((0, 0.0010, 0), (0.0004, 0.0008, 0), (0, 0, 0)), 10: ((0, 0.0008, 0), (0, 0.0004, 0), (0, 0, 0)), 11: ((0, 0.0010, 0), (-0.0004, 0.0008, 0), (0, 0, 0)),
    3: ((0, 0.0006, 0.0003), (0, 0, 0), (0, -0.0005, 0)), 4: ((0, 0.0008, 0.0003), (0, 0, 0), (0, -0.0006, 0)),
    13: ((0, 0.0008, -0.0002), (0, 0.0004, 0), (0, 0, 0)),
    14: ((0.0003, 0.0010, 0), (0.0003, 0.0006, 0), (0, 0, 0)), 15: ((-0.0003, 0.0010, 0), (-0.0003, 0.0006, 0), (0, 0, 0)),
    16: ((0.0002, 0.0008, 0), (0.0005, 0.0010, -0.0004), (0, 0, 0)), 17: ((-0.0002, 0.0008, 0), (-0.0005, 0.0010, -0.0004), (0, 0, 0)),
    26: ((-0.0020, -0.0030, 0.0005), (0, 0, 0), (0.0003, 0.0040, 0)), 27: ((0.0020, -0.0030, 0.0005), (0, 0, 0), (-0.0003, 0.0040, 0)),
    28: ((0, -0.0012, 0), (0, 0, 0), (0, 0.0035, 0)), 29: ((0, -0.0012, 0), (0, 0, 0), (0, 0.0035, 0)),
    30: ((0, -0.0020, -0.0004), (0, 0, 0), (0, 0.0020, 0)),
}
# kPose2: d -> (grito, puchero)
K_POSE2 = {
    1: ((-0.0006, -0.0010, 0), (0.0003, 0.0015, 0.0012)), 2: ((0.0006, -0.0010, 0), (-0.0003, 0.0015, 0.0012)),
    3: ((0, -0.0010, 0), (0, 0.0025, 0.0020)), 4: ((0, 0, 0), (0, 0.0020, 0.0010)),
    5: ((0.0030, -0.0005, -0.0012), (-0.0005, -0.0020, 0)), 6: ((-0.0030, -0.0005, -0.0012), (0.0005, -0.0020, 0)),
    7: ((0.0010, -0.0005, 0), (0, -0.0008, 0)), 8: ((-0.0010, -0.0005, 0), (0, -0.0008, 0)),
    9: ((0.0005, 0.0020, 0), (0, 0.0003, 0.0008)), 10: ((0, 0.0018, 0), (0, 0.0003, 0.0008)), 11: ((-0.0005, 0.0020, 0), (0, 0.0003, 0.0008)),
    13: ((0, 0.0010, -0.0002), (0, 0, 0)), 14: ((0.0004, 0.0012, 0), (0, 0, 0)), 15: ((-0.0004, 0.0012, 0), (0, 0, 0)),
    16: ((0.0005, 0.0012, 0), (0, 0, 0)), 17: ((-0.0005, 0.0012, 0), (0, 0, 0)),
    26: ((-0.0018, 0.0015, 0.0004), (-0.0008, 0.0030, 0)), 27: ((0.0018, 0.0015, 0.0004), (0.0008, 0.0030, 0)),
    28: ((0, -0.0008, 0), (0, -0.0005, 0)), 29: ((0, -0.0008, 0), (0, -0.0005, 0)), 30: ((0, -0.0008, -0.0003), (0, 0.0010, 0)),
}
# Secondary kOff: (d, x, y, z, k): k 0 sonrisa mejilla, 2 sonrisa surco, 3 sonrisa (slider), 4 enojo, 5 asombro, 6 grito, 7 puchero, 1 boca abierta (sin filas)
K_SEC = [(18, 0.0006, 0.0018, 0.0010, 0), (19, -0.0006, 0.0018, 0.0010, 0), (16, 0.0004, 0.0012, -0.0008, 2), (17, -0.0004, 0.0012, -0.0008, 2),
         (18, 0.0008, 0.0022, 0.0008, 3), (19, -0.0008, 0.0022, 0.0008, 3), (20, 0.0002, 0.0009, 0.0004, 3), (21, -0.0002, 0.0009, 0.0004, 3),
         (18, 0.0003, 0.0008, 0.0003, 4), (19, -0.0003, 0.0008, 0.0003, 4), (20, 0.0001, 0.0007, 0.0002, 4), (21, -0.0001, 0.0007, 0.0002, 4),
         (18, 0, -0.0005, 0, 5), (19, 0, -0.0005, 0, 5), (20, 0, -0.0007, -0.0002, 5), (21, 0, -0.0007, -0.0002, 5),
         (18, 0.0010, 0.0020, 0.0006, 6), (19, -0.0010, 0.0020, 0.0006, 6), (20, 0.0003, 0.0014, 0.0005, 6), (21, -0.0003, 0.0014, 0.0005, 6),
         (18, 0, -0.0006, 0, 7), (19, 0, -0.0006, 0, 7), (20, 0, 0.0004, 0, 7), (21, 0, 0.0004, 0, 7)]

def _izq(d): return (d % 2) == 1 if d <= 11 else (d % 2) == 0

def pose(ang=0, smi=0, sur=0, gri=0, puc=0, abrir=0, dientes=0, cejas=0, angulo_cejas=0, talk_open=0, talk_round=0):
    """lo que exprbones.cpp escribe con esos sliders: {d: corrimiento (m, ejes de la cabeza)}, giro de la mandibula (rad, + abre),
    parpados (up: + baja el de arriba, lo: - sube el de abajo; rad)"""
    o = max(min(1.0, abrir), min(1.0, 0.40 * sur + 0.85 * gri + talk_open + 0.60 * max(0.0, smi - 1.0)))
    A = min(0.40 * o, TOPE_MANDIBULA)
    smY = smi if smi <= 1.0 else 1.0 + 0.25 * (smi - 1.0)
    mv = {}
    def add(d, v, k=1.0):
        a = mv.setdefault(d, [0.0, 0.0, 0.0])
        for i in range(3): a[i] += v[i] * k
    for d in range(31):
        if d in K_POSE:
            a, s, u = K_POSE[d]
            add(d, a, ang); add(d, (s[0], s[1] * (smY / smi if smi > 0 and d in (5, 6) else 1.0), s[2]), smi); add(d, u, sur)
        if d in K_POSE2: g, p = K_POSE2[d]; add(d, g, gri); add(d, p, puc)
        if d in K_OPEN: add(d, K_OPEN[d], o)
        if d in K_TEETH: add(d, K_TEETH[d], dientes)
        if d in (5, 6): add(d, ((-1.0 if d == 5 else 1.0) * 0.0012 * talk_round, 0, 0))
        if 26 <= d <= 29: add(d, (0, 0.0040 * cejas, 0))
        if d == 30: add(d, (0, 0.0020 * cejas + 0.0015 * angulo_cejas, 0))
        if d in (26, 27): add(d, (-(1 if _izq(d) else -1) * 0.0006 * angulo_cejas, 0.0035 * angulo_cejas, 0))
        if d in (28, 29): add(d, (0, -0.0012 * angulo_cejas, 0))
    peso = {0: smi, 2: smi, 3: smi, 4: ang, 5: sur, 6: gri, 7: puc}   # 348.x / 348.y = 1
    for d, x, y, z, k in K_SEC:
        if peso.get(k, 0) > 0.001: add(d, (x, y, z), peso[k])
    big = max(smi - 1.0, 0.0)
    up = 0.06 * smi + 0.08 * big + 0.08 * ang - 0.20 * sur + 0.10 * gri + 0.05 * puc
    lo = -(0.06 * smi + 0.07 * big + 0.03 * ang + 0.10 * gri) + 0.04 * sur
    return mv, A, up, lo

# ---- ejes ----
def head_a_ask(v): return (v[1] * 100.0, -v[2] * 100.0, -v[0] * 100.0)   # m (cabeza) -> cm (.gani); inverso de GaniPToHead
def rotz(a): return (0.0, 0.0, math.sin(a / 2), math.cos(a / 2))
def qmul(a, b):
    return (a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2])
def zrot_vec(a, v): c, s = math.cos(a), math.sin(a); return (v[0] * c - v[1] * s, v[0] * s + v[1] * c, v[2])

class Rig:
    def __init__(self, carpeta):
        self.base_bytes = open(os.path.join(carpeta, 'base_normal.gani'), 'rb').read()
        self.ask = GC.leer_ask(open(os.path.join(carpeta, 'face_anim_skel.ask'), 'rb').read())['huesos']
        frig = GC.leer_frig(open(os.path.join(carpeta, 'face_skel.frig'), 'rb').read())
        G = GC.leer_gani(self.base_bytes)
        cm = GC.mapa_canales(frig['unidades'], G['canales'])
        self.nombre = [h['nombre'] for h in self.ask]
        self.canal = {}   # (nombre, 'rot'|'pos') -> indice
        for ix, (k, b) in cm.items():
            if b >= 0: self.canal[(self.nombre[b], k)] = ix
        self.padre = {h['nombre']: (self.nombre[h['padre']] if h['padre'] >= 0 else None) for h in self.ask}
        self.reposo = {h['nombre']: h['pos'] for h in self.ask}

    def pistas_de(self, mv, A, up, lo):
        """{(nombre, tipo): valor relativo}: corrimientos en cm (ejes del padre en el .gani) y giros (cuaternion) a componer"""
        out = {}
        jaw = rotz(-A)   # en el .gani la mandibula abre con giro z negativo (verificado con los clips de Konami)
        if abs(A) > 1e-6: out[('skf_jaw', 'rot')] = jaw
        J = self.reposo['skf_jaw']
        for d, v in mv.items():
            n = KDEF[d]
            if (n, 'pos') not in self.canal: continue   # doublechin / lip_volume no estan en el rig de animacion
            p = head_a_ask(v)
            if self.padre.get(n) == 'skf_jaw': p = zrot_vec(A, p)   # cuelga de la mandibula: llevar a sus ejes
            out[(n, 'pos')] = p
        for d, w in JAWW.items():   # comisuras y mejilla baja: la mitad del giro, alrededor de la mandibula (como la DLL)
            n = KDEF[d]
            if w >= 1.0 or (n, 'pos') not in self.canal or abs(A) < 1e-6: continue
            R = [x * 100 for x in self.reposo[n]]; Jc = [x * 100 for x in J]
            r = [R[i] - Jc[i] for i in range(3)]; rr = zrot_vec(-A * w, r)
            dp = [rr[i] - r[i] for i in range(3)]
            a = out.get((n, 'pos'), (0, 0, 0)); out[(n, 'pos')] = tuple(a[i] + dp[i] for i in range(3))
            out[(n, 'rot')] = rotz(-A * w)
        dU, dL = -math.degrees(up), -math.degrees(lo)   # .gani: arriba menos angulo = baja; abajo mas angulo = sube
        if abs(dU) > 1e-4:
            for n in ('skf_eyelid_t_l', 'skf_eyelid_t_r'): out[(n, 'rot')] = rotz(math.radians(dU))
        if abs(dL) > 1e-4:
            for n in ('skf_eyelid_b_l', 'skf_eyelid_b_r'): out[(n, 'rot')] = rotz(math.radians(dL))
        return out

    def sumar(self, clip_bytes, rel, peso=1.0, cuadros_fijos=None):
        """suma el gesto `rel` (por pistas_de) a cada clave del clip; devuelve los bytes del clip nuevo"""
        G, cr = GE.leer_crudo(clip_bytes); bits = {c['indice']: c['bits'] for c in G['canales']}
        pistas = {}
        for (n, k), v in rel.items():
            ix = self.canal.get((n, k))
            if ix is None: continue
            kind, bs, keys = cr[ix]
            nv = []
            for key in keys:
                if kind == 'pos':
                    f, h = key; p = [GC.half(x) for x in h]
                    nv.append((f, tuple(p[i] + v[i] * peso for i in range(3))))
                else:
                    f, fr, sg = key; q = GC.quat([x / float(1 << bs) for x in fr], sg)
                    ang = 2 * math.acos(max(-1, min(1, v[3]))) * peso; ax = v[:3]; s = math.sqrt(sum(x * x for x in ax))
                    r = (0, 0, 0, 1) if s < 1e-9 else tuple([x / s * math.sin(ang / 2) for x in ax] + [math.cos(ang / 2)])
                    nv.append((f, qmul(r, q)))   # giro del mod sobre el del clip (eje z local en los dos)
            pistas[ix] = nv
        return GE.escribir(clip_bytes, pistas, G['cuadros'], base=cr)

def sostenido(rig, rel):
    """clip propio: base_normal con el gesto, sostenido CUADROS cuadros (una clave por canal, como los de Konami quietos)"""
    b = rig.sumar(rig.base_bytes, rel)
    return bytearray_fijar_cuadros(b, CUADROS)

def bytearray_fijar_cuadros(b, n):
    b = bytearray(b); struct.pack_into('<I', b, GE.MOV + 12, n); return bytes(b)

def hablar(rig, segundos=6.0, fps=30):
    """bucle de habla de la DLL (fila 355.w = 1, velocidad 0.55): mandibula con ritmo de silabas y labios que se redondean; entra y
    sale de cero en las puntas para que el bucle no salte"""
    G, cr = GE.leer_crudo(rig.base_bytes)
    N = int(segundos * fps); pistas = {}
    for f in range(0, N + 1, 2):
        t = f / fps * 0.55
        w = 0.6 * math.sin(t * 2 * math.pi * 4.1) + 0.4 * math.sin(t * 2 * math.pi * 2.3 + 1.3)
        env = max(0.0, 0.40 + 0.60 * math.sin(t * 2 * math.pi * 0.37)) * math.sin(math.pi * f / N) ** 2
        mv, A, up, lo = pose(talk_open=0.30 * max(w, 0.0) * env, talk_round=math.sin(t * 2 * math.pi * 1.7 + 0.6) * env)
        rel = rig.pistas_de(mv, A, up, lo)
        for ix in cr:
            nombre = [k for k, v in rig.canal.items() if v == ix]
            kind, bs, keys = cr[ix]
            if not nombre: continue
            n, k = nombre[0]
            v = rel.get((n, k))
            if kind == 'pos':
                p = [GC.half(x) for x in keys[0][1]]; val = tuple(p[i] + (v[i] if v else 0.0) for i in range(3))
            else:
                q = GC.quat([x / float(1 << bs) for x in keys[0][1]], keys[0][2]); val = qmul(v, q) if v else q
            pistas.setdefault(ix, []).append((f, val))
    # canales que no cambian en todo el bucle: una sola clave
    for ix in list(pistas):
        vals = [v for _, v in pistas[ix]]
        if all(max(abs(a - b) for a, b in zip(vals[0], x)) < 1e-6 for x in vals): pistas[ix] = [pistas[ix][0]]
    return GE.escribir(rig.base_bytes, pistas, N, base=cr)

def animado(rig, N, fn, paso=2):
    """clip animado de N cuadros: fn(f) -> (mv, A, up, lo) en los ejes de la DLL; base_normal + eso en cada clave"""
    G, cr = GE.leer_crudo(rig.base_bytes); pistas = {}
    inv = {v: k for k, v in rig.canal.items()}
    for f in list(range(0, N, paso)) + [N]:
        rel = rig.pistas_de(*fn(f))
        for ix, (kind, bs, keys) in cr.items():
            if ix not in inv: continue
            v = rel.get(inv[ix])
            if kind == 'pos':
                p = [GC.half(x) for x in keys[0][1]]; val = tuple(p[i] + (v[i] if v else 0.0) for i in range(3))
            else:
                q = GC.quat([x / float(1 << bs) for x in keys[0][1]], keys[0][2]); val = qmul(v, q) if v else q
            pistas.setdefault(ix, []).append((f, val))
    for ix in list(pistas):
        vals = [v for _, v in pistas[ix]]
        if all(max(abs(a - b) for a, b in zip(vals[0], x)) < 1e-6 for x in vals): pistas[ix] = [pistas[ix][0]]
    return GE.escribir(rig.base_bytes, pistas, N, base=cr)

def _env(f, N, ent=8, sal=10):   # entra y sale suave (cuadros)
    a = min(1.0, f / float(ent)); b = min(1.0, (N - f) / float(sal)); x = max(0.0, min(a, b)); return x * x * (3 - 2 * x)

def _mv(d):   # {d: (x, y, z) mm, ejes de la cabeza} -> m
    return {k: [c * 0.001 for c in v] for k, v in d.items()}

def entre_respirar(f, N=150):   # dos respiraciones por la nariz: alas de la nariz afuera, labios apenas separados
    r = 0.5 - 0.5 * math.cos(2 * math.pi * f / 75.0); e = _env(f, N, 15, 15) * r
    mv = _mv({14: (0.6, 0.2, 0.1), 15: (-0.6, 0.2, 0.1), 13: (0, 0.15, 0), 7: (0.2, -0.1, 0), 8: (-0.2, -0.1, 0)})
    return {k: [c * e for c in v] for k, v in mv.items()}, math.radians(1.2) * e, 0.0, 0.0

def quieto_cachetes(f, N=54):   # infla los cachetes con la boca cerrada y suelta el aire
    e = _env(f, N, 10, 14)
    mv = _mv({18: (2.0, 0.3, 1.2), 19: (-2.0, 0.3, 1.2), 7: (3.0, 0.0, 1.5), 8: (-3.0, 0.0, 1.5), 16: (1.0, 0, 0.4), 17: (-1.0, 0, 0.4),
              5: (-0.8, 0, 0.8), 6: (0.8, 0, 0.8), 9: (0, -0.6, 0.6), 10: (0, -0.6, 0.8), 11: (0, -0.6, 0.6), 1: (0, 0.6, 0.6), 2: (0, 0.6, 0.6), 3: (0, 0.6, 0.8)})
    return {k: [c * e for c in v] for k, v in mv.items()}, 0.0, 0.04 * e, -0.03 * e

def arranque_soplar(f, N=40):   # sopla con los labios en o, cachetes un poco inflados y ojos entrecerrados
    e = _env(f, N, 6, 14)
    mv = _mv({5: (-3.5, 0, 2.0), 6: (3.5, 0, 2.0), 1: (-0.8, 0.4, 2.2), 2: (0.8, 0.4, 2.2), 3: (0, 0.5, 2.5), 9: (-0.6, -0.3, 2.2), 10: (0, -0.3, 2.5), 11: (0.6, -0.3, 2.2),
              18: (1.2, 0.4, 0.8), 19: (-1.2, 0.4, 0.8), 7: (1.5, 0, 0.8), 8: (-1.5, 0, 0.8), 20: (0.2, 0.7, 0.3), 21: (-0.2, 0.7, 0.3),
              26: (-0.8, -1.0, 0.2), 27: (0.8, -1.0, 0.2), 30: (0, -0.6, 0)})
    return {k: [c * e for c in v] for k, v in mv.items()}, math.radians(2.0) * e, 0.10 * e, -0.06 * e

ENTRE = {'entre_respirar': (entre_respirar, 150), 'quieto_cachetes': (quieto_cachetes, 54), 'arranque_soplar': (arranque_soplar, 40)}

GESTOS = {   # nombre del archivo -> sliders (los de la tarjeta de gestos del menu, a 1.0)
    'zfx_enojo': dict(ang=1.0), 'zfx_sonrisa': dict(smi=1.0), 'zfx_risa': dict(smi=1.4), 'zfx_asombro': dict(sur=1.0),
    'zfx_grito': dict(gri=1.0), 'zfx_puchero': dict(puc=1.0), 'zfx_abrir_boca': dict(abrir=1.0), 'zfx_dientes': dict(dientes=1.0),
    'zfx_cejas_arriba': dict(cejas=1.0), 'zfx_ceno': dict(angulo_cejas=-1.0), 'zfx_preocupado': dict(angulo_cejas=1.0),
}

def main():
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == '--mod':
        rig = Rig(a[1]); os.makedirs(a[2], exist_ok=True)
        for n, kw in GESTOS.items():
            mv, A, up, lo = pose(**kw); b = sostenido(rig, rig.pistas_de(mv, A, up, lo))
            open(os.path.join(a[2], n + '.gani'), 'wb').write(b)
            print('%-22s %5d B  mandibula %5.1f grados, parpado arriba %+5.1f, abajo %+5.1f' % (n, len(b), math.degrees(A), -math.degrees(up), -math.degrees(lo)))
        b = hablar(rig); open(os.path.join(a[2], 'zfx_hablar.gani'), 'wb').write(b); print('%-22s %5d B  bucle 6 s' % ('zfx_hablar', len(b)))
        return
    if len(a) == 3 and a[0] == '--entre':   # gestos "entre medio" de ejemplo (van en PES21FX\anim\face\todos\)
        rig = Rig(a[1]); os.makedirs(a[2], exist_ok=True)
        for n, (fn, N) in ENTRE.items():
            b = animado(rig, N, lambda f: fn(f, N)); open(os.path.join(a[2], n + '.gani'), 'wb').write(b); print('%-22s %5d B  %.1f s' % (n, len(b), N / 30.0))
        return
    if len(a) == 6 and a[0] == '--sumar':
        _, carpeta, gesto, peso, ent, sal = a
        rig = Rig(carpeta); mv, A, up, lo = pose(**GESTOS[gesto])
        b = rig.sumar(open(ent, 'rb').read(), rig.pistas_de(mv, A, up, lo), float(peso)); open(sal, 'wb').write(b)
        print(sal, len(b), 'B'); return
    print('uso: gani_gestos.py --mod carpeta_konami salida/ | --sumar carpeta_konami zfx_gesto peso clip.gani salida.gani')

if __name__ == '__main__': main()
