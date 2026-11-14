#!/usr/bin/env python3
"""PES21 FX - pesos de los huesos de la cara del publico (mandibula, ceja izquierda, ceja derecha) en los au*.fmdl.
uso: pesos_cara.py <audi_model.fpk> <salida.fpk> [carpeta_vista]

Los pesos van en el COLOR del vertice (u8 x4), que los VS del publico del juego no leen salvo x (marca de vertice oculto:
x = 255 lo esconde). Se escribe:  x = 6 o 7 (marca "modelo con cara" y clase de UV del cachete, ver abajo; el juego trae 5),  y = mandibula,  z = ceja izq,  w = ceja der.
Los VS del mod (shaders/pes_crowd_face.hlsli) solo mueven vertices con x = 6; con la DLL apagada o con shaders del juego
el modelo se ve igual que antes (los pesos no se usan). Los vertices ocultos (x = 255) no se tocan.
Huesos (pose de reposo, metros del modelo; todas las figuras comparten au00.skl, cuello en y 1.532):
  mandibula = giro sobre X con pivote (0, 1.650, 0.060): pesa lo que esta debajo de la linea de la boca y adelante
  cejas     = traslacion hacia arriba: banda de la ceja (y ~1.704) con caida suave hacia la frente
La linea de la boca de los modelos "_mouthOpen" esta mas abajo (1.622 contra 1.633).
(08/11 c) Sin camiseta: el VS pinta torso y brazos con la piel de la cara del propio hincha poniendo en esos vertices la UV
del cachete. Hay dos disposiciones de UV en los 14 modelos: x = 6 -> cachete en KCHEEK[0], x = 7 -> KCHEEK[1]
(las mismas constantes estan en pes_crowd_face.hlsli)."""
KCHEEK = [(0.745, 0.392), (0.805, 0.293)]
import sys, os, struct
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'fox'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sincamiseta'))
from fmdl import Fmdl
from make_fpk_sin_short import entries
from subdividir_publico import read_mesh, decode, get_attr, repack

def ss(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)

def weights(P, mouth_y):
    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    head = ss(1.552, 1.585, y)                       # debajo del menton (cuello) casi nada
    # mandibula: debajo de la boca, adelante del pivote (las orejas / nuca quedan quietas)
    jaw = ss(mouth_y + 0.004, mouth_y - 0.008, y) * ss(0.045, 0.090, z) * head
    jaw *= 1 - ss(0.055, 0.080, np.abs(x))           # los costados de la cara acompanan menos
    # cejas: banda de la ceja, sube suave hacia la frente; solo la cara de adelante
    band = ss(1.694, 1.703, y) * (1 - ss(1.712, 1.748, y)) * ss(0.115, 0.150, z)
    band *= 1 - ss(0.055, 0.072, np.abs(x))
    left = band * ss(0.0, 0.014, x)                  # izquierda del personaje = +x (mira a +z)
    right = band * ss(0.0, 0.014, -x)
    return jaw, left, right

def cheek_class(P, UV):
    sel = (np.abs(P[:, 0] - 0.045) < 0.03) & (np.abs(P[:, 1] - 1.645) < 0.03) & (P[:, 2] > 0.07)
    if not sel.any(): return 0, None
    c = UV[sel].mean(0)
    d = [np.hypot(c[0] - k[0], c[1] - k[1]) for k in KCHEEK]
    return int(np.argmin(d)), c

def paint_fmdl(blob, mouth_y, tmp):
    open(tmp, 'wb').write(blob)
    F = Fmdl(tmp); d = bytearray(blob)
    base = F.b1[2][0]
    bo_cnt, bo_off = F.b0[14]
    bufs = [struct.unpack_from('<IIII', F.d, bo_off + 16 * i) for i in range(bo_cnt)]
    stats = []
    for mi in range(F.b0[3][0]):
        M = read_mesh(F, mi)
        vpos = [vf for vf in M['vfs'] if vf['usage'] == 0][0]
        vcol = [vf for vf in M['vfs'] if vf['usage'] == 3 and vf['fmt'] == 8]
        if not vcol: stats.append('malla %d sin color' % mi); continue
        vcol = vcol[0]
        P = decode(vpos, get_attr(M, vpos))
        vuv = [vf for vf in M['vfs'] if vf['usage'] == 8][0]
        if mi == 0: cls, cuv = cheek_class(P, decode(vuv, get_attr(M, vuv)))
        col = decode(vcol, get_attr(M, vcol)).astype(np.int64)
        jaw, l, r = weights(P, mouth_y)
        hidden = col[:, 0] == 255
        new = col.copy()
        new[~hidden, 0] = 6 + cls
        new[~hidden, 1] = np.rint(jaw[~hidden] * 255)
        new[~hidden, 2] = np.rint(l[~hidden] * 255)
        new[~hidden, 3] = np.rint(r[~hidden] * 255)
        st = M['streams'][vcol['s']]
        a = base + bufs[st['bidx']][2] + st['moff']
        for v in range(M['nvert']):
            o = a + v * st['stride'] + vcol['voff']
            d[o:o + 4] = bytes(int(c) for c in new[v])
        stats.append('malla %d (clase %d, cachete %s): %d vert, boca %d, ceja izq %d, ceja der %d' % (mi, cls, None if cuv is None else tuple(np.round(cuv, 3)), M['nvert'], (jaw > 0.05).sum(), (l > 0.05).sum(), (r > 0.05).sum()))
    return bytes(d), stats

def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = open(src, 'rb').read()
    tmp = dst + '.tmp.fmdl'; items = {}
    for e in entries(d):
        if not e['name'].endswith('.fmdl'): continue
        mouth_y = 1.622 if 'mouthOpen' in e['name'] else 1.633
        new, st = paint_fmdl(d[e['off']:e['off'] + e['size']], mouth_y, tmp)
        assert len(new) == e['size']
        items[e['name']] = new
        print(os.path.basename(e['name']), '|', '; '.join(st))
        if len(sys.argv) > 3:
            os.makedirs(sys.argv[3], exist_ok=True); open(os.path.join(sys.argv[3], os.path.basename(e['name'])), 'wb').write(new)
    os.remove(tmp)
    out = repack(d, items)
    open(dst, 'wb').write(out); print(dst, len(out))

if __name__ == '__main__': main()
