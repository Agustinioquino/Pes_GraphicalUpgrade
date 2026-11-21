# PES21 FX -> Unreal Engine (15/11/2026, Agustin: "exportar el trabajo ya hecho del cesped para unreal engine", "todo lo que tenga que
# ver con texturas, y los modelos"). Arma una carpeta lista para importar en Unreal:
#   Texturas/  los atlas del mod pasados a PNG en los mapas que usa un material de Unreal (BaseColor, Opacity, Normal, Roughness,
#              Height, Thickness) + los originales tal cual vienen empaquetados (Datos/), para quien quiera rehacer el shader
#   Modelos/   hojas 3D sueltas, macollos, hojas aplastadas, una parcela de 50 cm y una tarjeta para el atlas, en OBJ (1 unidad = 1 cm,
#              Z arriba, como Unreal). La forma sale de la MISMA cuenta que el VS 498b30 (hojas 3D de primerisimo plano)
# Uso: python export_pasto_unreal.py <carpeta textures del mod> <carpeta de salida>
import numpy as np, os, sys, math
from PIL import Image

TEX, OUT = sys.argv[1], sys.argv[2]
NAT_CUT = 28.0 / 60.0          # w296 = 28 mm (PES_NAT_CUT = w296 / 60 mm)

# ---------------------------------------------------------------- texturas
def load(name):
    return np.asarray(Image.open(os.path.join(TEX, name)).convert('RGBA'), np.float32) / 255.0

def save(a, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    a = np.clip(a, 0, 1)
    if a.ndim == 2: Image.fromarray((a * 255 + 0.5).astype(np.uint8), 'L').save(path)
    else: Image.fromarray((a * 255 + 0.5).astype(np.uint8)).save(path)

def normal_from_rg(n):
    x = n[..., 0] * 2 - 1; y = n[..., 1] * 2 - 1
    z = np.sqrt(np.clip(1 - x * x - y * y, 0, 1))
    # convencion de Unreal (DirectX, verde = -Y de la textura): el atlas guarda y "a lo largo, hacia la punta" = arriba de la imagen
    return np.stack([x, y, z], -1) * 0.5 + 0.5

def dilate_color(rgb, a, it=24):
    # el color se extiende a los texeles transparentes (sin halo oscuro al filtrar)
    rgb = rgb.copy(); m = a > 0.3
    for _ in range(it):
        if m.all(): break
        acc = np.zeros_like(rgb); cnt = np.zeros(m.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            mm = np.roll(m, (dy, dx), (0, 1)); acc += np.roll(rgb, (dy, dx), (0, 1)) * mm[..., None]; cnt += mm
        new = (~m) & (cnt > 0)
        rgb[new] = acc[new] / cnt[new][:, None]; m = m | new
    return rgb

def atlas(name, base_fn, gloss_in_b, folder):
    c = load(name + '.dds'); n = load(name + '_n.dds'); a = c[..., 3]
    base = base_fn(c)
    save(dilate_color(base, a), f'{OUT}/Texturas/{folder}/{name}_BaseColor.png')
    save(a, f'{OUT}/Texturas/{folder}/{name}_Opacity.png')
    save(normal_from_rg(n), f'{OUT}/Texturas/{folder}/{name}_Normal.png')
    save(n[..., 2], f'{OUT}/Texturas/{folder}/{name}_Height.png')
    save(n[..., 3], f'{OUT}/Texturas/{folder}/{name}_Thickness.png')
    save(np.clip(2 * c[..., 1], 0, 1), f'{OUT}/Texturas/{folder}/{name}_AO.png')
    rough = 1 - 0.6 * c[..., 2] if gloss_in_b else np.full(a.shape, 0.55, np.float32)
    save(rough, f'{OUT}/Texturas/{folder}/{name}_Roughness.png')
    for nm in (name + '.dds', name + '_n.dds'):
        save(load(nm), f'{OUT}/Texturas/Datos/{nm[:-4]}_crudo.png')

def nat_base(c):   # misma vista que tools/make_grass_natural.py (el juego pone su verde con x456)
    hue = c[..., 2:3]
    base = np.array([0.15, 0.33, 0.08]) * (1 - hue) + np.array([0.19, 0.36, 0.09]) * hue
    return base * (0.45 + 1.1 * c[..., 0:1])
def art_base(c):
    return np.array([0.17, 0.34, 0.085]) * (0.45 + 1.1 * c[..., 0:1])
def deb_base(c):   # bandas de 512: 0 manojo arrancado, 1 terron, 2 polvo, 3 hoja suelta
    W = c.shape[1]; band = (np.arange(W) // (W // 4))[None, :, None]
    col = np.where(band == 0, np.array([0.17, 0.30, 0.09]), np.where(band == 1, np.array([0.36, 0.27, 0.18]),
          np.where(band == 2, np.array([0.45, 0.38, 0.29]), np.array([0.19, 0.33, 0.10]))))
    return col * (0.45 + 1.1 * c[..., 0:1])

def textures():
    atlas('pes_grass_natural', nat_base, False, 'Pasto_natural')
    atlas('pes_grass_blades', art_base, True, 'Pasto_artificial')
    atlas('pes_debris', deb_base, True, 'Terrones')
    t = load('pes_turf_n.dds')
    save(t[..., :3], f'{OUT}/Texturas/Suelo/pes_turf_Normal.png')
    save(t[..., 3], f'{OUT}/Texturas/Suelo/pes_turf_Height.png')
    save(t, f'{OUT}/Texturas/Datos/pes_turf_n_crudo.png')

# ---------------------------------------------------------------- modelos (cuenta del VS 498b30)
def blade(nR=3, nar=False, h=None, wB=None, bnd=0.2, tilt=0.07, twist=0.0, yaw=0.0, root=(0.0, 0.0), cutB=True, cutS=0.0,
          tipMid=0.0, tipW2=0.84, tipW3=None, kink=0.0, crush=None, rA=1.0, wv=0.0, aX=0.0):
    if h is None: h = NAT_CUT * 0.058
    if wB is None: wB = 0.0029 * pow(min(NAT_CUT, 1.0), -0.4) * (0.45 if nar else 1.0)
    if tipW3 is None: tipW3 = 0.78 if cutB else 0.0
    W0 = np.array([math.cos(yaw), math.sin(yaw)]); F0 = np.array([-W0[1], W0[0]]); root = np.array(root)
    kFold = 0.62 if nar else 0.30
    V, UV = [], []
    for row in range(nR):
        t = row / (nR - 1)
        if nar:
            tipN = (0.45 if tipW3 > 0.5 else 0.15) if cutB else 0.0
            wr = (0.78 + 0.22 * t * 4) if t < 0.25 else ((1.0 - 0.10 * (t - 0.25) * 4) if t < 0.5 else
                 ((0.90 - 0.22 * (t - 0.5) * 4) if t < 0.75 else (0.68 + (tipN - 0.68) * (t - 0.75) * 4)))
        else:
            wr = (1.0 - 0.04 * t * 3) if t < 1 / 3 else ((0.96 + (tipW2 - 0.96) * (t * 3 - 1)) if t < 2 / 3 else (tipW2 + (tipW3 - tipW2) * (t * 3 - 2)))
        wh = 0.5 * wB * wr
        psi = yaw + twist * t; Wd = np.array([math.cos(psi), math.sin(psi)]); Fd = np.array([-Wd[1], Wd[0]])
        yy = h * t * (1 - 0.22 * bnd * t); tK = max(t - 0.5, 0.0); yy -= kink * h * 1.6 * tK * tK * (1 + 2 * tK)
        for col in range(3):
            sd = col - 1.0
            y = yy + ((sd * cutS * wB * 0.5 + (1 - abs(sd)) * tipMid * wB * 0.7) if (row == nR - 1 and cutB) else 0.0)
            if crush is None:
                xz = root + F0 * (bnd * h * t * t + tilt * h * t) + Wd * (sd * wh) + Fd * (abs(sd) * kFold * wh)
            else:   # (12/11) modelos del pasto aplastado: kEm borde, kTm tumbada (hacia +x)
                kEm, kTm = crush
                dE = np.array([1.0, 0.0]); c, s = math.cos(aX), math.sin(aX); dT = np.array([dE[0] * c - dE[1] * s, dE[0] * s + dE[1] * c])
                aE = np.array([0.45, 0.95, 1.50]) * rA; aTm = np.array([0.35, 1.47, 1.52])
                sgE = np.array([min(t, 0.40), min(max(t - 0.40, 0), 0.30), max(t - 0.70, 0)])
                sgT = np.array([min(t, 0.08), min(max(t - 0.08, 0), 0.30), max(t - 0.38, 0)])
                cE = h * np.array([sgE @ np.sin(aE), sgE @ np.cos(aE)]); cT = h * np.array([sgT @ np.sin(aTm), sgT @ np.cos(aTm)])
                wvT = math.sin(t * 5.5 + wv * 6.2832) * 0.07 * h * t
                cT[1] = max(cT[1] + 0.006 * h * (0.5 + (1 if t > 0.75 else 0) * min((t - 0.75) / 0.25, 1)), 0.0015 + 0.0035 * t * 0.5)
                cS = root + F0 * (bnd * h * t * t + tilt * h * t)
                pE = root + dE * cE[0]; pT = root + dT * cT[0] + np.array([-dT[1], dT[0]]) * wvT
                cXZ = (cS * (1 - kEm) + pE * kEm) * (1 - kTm) + pT * kTm
                cY = (yy * (1 - kEm) + cE[1] * kEm) * (1 - kTm) + cT[1] * kTm
                dC = dE * (1 - kTm) + dT * kTm; dC /= np.linalg.norm(dC)
                Wl = np.array([-dC[1], dC[0]]) * (1 if Wd @ np.array([-dC[1], dC[0]]) >= 0 else -1)
                WdD = Wd * (1 - kEm) + Wl * kEm; WdD /= np.linalg.norm(WdD) + 1e-9; FdD = np.array([-WdD[1], WdD[0]])
                xz = cXZ + WdD * (sd * wh * (1 + 0.30 * kTm)) + FdD * (abs(sd) * kFold * wh * (1 - 0.85 * kTm))
                y = cY + abs(sd) * kFold * wh * 0.8 * kEm * (1 - 0.85 * kTm)
            V.append((xz[0], xz[1], max(y, 0.0))); UV.append((0.5 + 0.5 * sd, 0.995 - 0.985 * t))
    F = []
    for r in range(nR - 1):
        a = r * 3
        F += [(a, a + 1, a + 4), (a, a + 4, a + 3), (a + 1, a + 2, a + 5), (a + 1, a + 5, a + 4)]
    return np.array(V), np.array(UV), F

def write_obj(path, parts, name):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write(f'# PES21 FX - {name} (1 unidad = 1 cm, Z arriba). Material de dos caras (two sided, masked o foliage)\n')
        base = 1
        for V, UV, F in parts:
            Vc = V * 100.0
            N = np.zeros_like(Vc)
            for a, b, c in F:
                n = np.cross(Vc[b] - Vc[a], Vc[c] - Vc[a]); N[[a, b, c]] += n
            N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-12
            for v in Vc: f.write(f'v {v[0]:.5f} {v[1]:.5f} {v[2]:.5f}\n')
            for uv in UV: f.write(f'vt {uv[0]:.5f} {1 - uv[1]:.5f}\n')
            for n in N: f.write(f'vn {n[0]:.5f} {n[1]:.5f} {n[2]:.5f}\n')
            for tri in F:
                i = [x + base for x in tri]; f.write('f ' + ' '.join(f'{k}/{k}/{k}' for k in i) + '\n')
            base += len(V)

def models():
    M = f'{OUT}/Modelos'
    tips = {'corte_recto': dict(cutB=True, cutS=0.06), 'corte_diagonal': dict(cutB=True, cutS=0.9),
            'corte_desgarrado': dict(cutB=True, cutS=0.2, tipMid=0.6, tipW3=0.88), 'punta_lanza': dict(cutB=False, tipW2=0.84),
            'punta_roma': dict(cutB=False, tipW2=0.80, tipW3=0.30)}
    for nm, kw in tips.items():
        hh = None if kw.get('cutB') else NAT_CUT * 0.058 * 0.75
        write_obj(f'{M}/Hojas/hoja_comun_{nm}.obj', [blade(nR=3, h=hh, **kw)], 'hoja comun (3 filas, como el juego)')
        write_obj(f'{M}/Hojas/hoja_comun_{nm}_HD.obj', [blade(nR=9, h=hh, **kw)], 'hoja comun HD (9 filas)')
        write_obj(f'{M}/Hojas/hoja_angosta_{nm}.obj', [blade(nR=5, nar=True, h=hh, **kw)], 'hoja angosta (5 filas, como el juego)')
        write_obj(f'{M}/Hojas/hoja_angosta_{nm}_HD.obj', [blade(nR=11, nar=True, h=hh, **kw)], 'hoja angosta HD (11 filas)')
    h0 = NAT_CUT * 0.058
    write_obj(f'{M}/Hojas/hoja_doblada_por_la_mitad.obj', [blade(nR=9, bnd=1.1, tilt=0.4, kink=0.8, cutB=False, h=h0 * 0.95)], 'hoja doblada (la punta cae)')
    write_obj(f'{M}/Hojas/hoja_acostada.obj', [blade(nR=9, tilt=1.8, bnd=0.45, h=h0 * 0.8 / math.sqrt(1 + 1.8 ** 2), cutB=False)], 'hoja acostada sobre las otras')
    write_obj(f'{M}/Hojas/hoja_rota_chica.obj', [blade(nR=5, h=h0 * 0.45, wB=0.0029 * 1.36 * 1.3, cutS=1.4, tilt=0.15)], 'hoja rota (corte desparejo)')
    write_obj(f'{M}/Hojas/hoja_alta_escapada.obj', [blade(nR=11, nar=False, h=h0 * 1.8, bnd=0.6, tilt=0.3, cutB=False, wB=0.0029 * 1.36 * 0.85)], 'hoja que se escapo de la cortadora (x439)')
    write_obj(f'{M}/Aplastado/hoja_aplastada_borde.obj', [blade(nR=11, crush=(1.0, 0.0))], 'pasto aplastado: BORDE de la pisada')
    write_obj(f'{M}/Aplastado/hoja_aplastada_tumbada.obj', [blade(nR=11, crush=(1.0, 1.0), wv=0.3, aX=0.2)], 'pasto aplastado: TUMBADA en el centro')
    write_obj(f'{M}/Aplastado/hoja_aplastada_a_medias.obj', [blade(nR=11, crush=(0.5, 0.0))], 'pasto aplastado: doblandose (a medias)')
    rng = np.random.default_rng(7)
    def clump(rootT, nar, nR, rnd):
        parts = []; yaw0 = rnd.random() * 6.2832
        for k in range(3):
            yaw = yaw0 + k * 2.0944 + (rnd.random() - 0.5) * 0.9
            F0 = np.array([-math.sin(yaw), math.cos(yaw)])
            u = rnd.random(); cutB = u < 0.85
            tipkw = dict(cutS=(rnd.random() - 0.5) * 1.1)
            if cutB:
                v = rnd.random()
                if v < 0.34: tipkw['cutS'] *= 0.12
                elif v < 0.76: tipkw['cutS'] = math.copysign(0.35 + 1.2 * abs(tipkw['cutS']), tipkw['cutS'])
                else: tipkw.update(tipMid=math.copysign(0.35 + 0.45 * rnd.random(), rnd.random() - 0.5), tipW3=0.88)
            elif rnd.random() < 0.4: tipkw.update(tipW2=0.80, tipW3=0.30)
            h = h0 * (1 - 0.08 * rnd.random()) if cutB else h0 * (0.42 + 0.5 * rnd.random())
            wB = 0.0029 * (0.65 + 0.7 * rnd.random()) * pow(NAT_CUT, -0.4) * ((0.45 * (0.8 + 0.4 * rnd.random())) if nar else 1.0)
            bnd = 0.55 + 0.35 * rnd.random() if rnd.random() < 0.12 else 0.06 + 0.30 * rnd.random()
            root = np.array(rootT) + F0 * (0.0015 + 0.0025 * rnd.random())
            parts.append(blade(nR=nR, nar=nar, h=h, wB=wB, bnd=bnd, tilt=0.14 * rnd.random(), twist=(rnd.random() - 0.5) * 0.9,
                               yaw=yaw, root=root, cutB=cutB, **tipkw))
        return parts
    write_obj(f'{M}/Macollos/macollo_3_hojas_comunes.obj', clump((0, 0), False, 9, rng), 'macollo de 3 hojas comunes')
    write_obj(f'{M}/Macollos/macollo_3_hojas_angostas.obj', clump((0, 0), True, 11, rng), 'macollo de 3 hojas angostas')
    for nm, n_cl, nar, nR in (('parcela_50cm_comun', 1400, False, 3), ('parcela_50cm_angosta', 1400, True, 5)):
        parts = []; off = rng.random() * 4096
        for tl in range(n_cl):
            r2 = (0.5 + (tl + off) * np.array([0.7548776662, 0.5698402910])) % 1.0
            r2 = (r2 + (rng.random(2) - 0.5) * 0.9 / math.sqrt(tl + 1)) % 1.0
            parts += clump((r2 - 0.5) * 0.5, nar, nR, rng)
        write_obj(f'{M}/Parcelas/{nm}.obj', parts, f'parcela de 50 x 50 cm ({n_cl} macollos, se repite en mosaico)')
    # tarjeta para el atlas (pasto de media distancia): 3 planos cruzados de 12 x 6 cm con una curva suave hacia adelante
    parts = []
    for k in range(3):
        a = k * math.pi / 3; d = np.array([math.cos(a), math.sin(a)]); fw = np.array([-d[1], d[0]])
        V, UV, F = [], [], []
        for r in range(5):
            t = r / 4
            for c in range(3):
                s = c / 2 - 0.5
                p = d * s * 0.12 + fw * 0.012 * t * t
                V.append((p[0], p[1], 0.06 * t)); UV.append((c / 2, 1 - t))
        for r in range(4):
            b = r * 3; F += [(b, b + 1, b + 4), (b, b + 4, b + 3), (b + 1, b + 2, b + 5), (b + 1, b + 5, b + 4)]
        parts.append((np.array(V), np.array(UV), F))
    write_obj(f'{M}/Tarjetas/tarjeta_atlas_3_planos.obj', parts, 'tarjeta de 3 planos para el atlas (UV 0..1 = una tarjeta del atlas)')

if __name__ == '__main__':
    models(); textures(); print('ok')
