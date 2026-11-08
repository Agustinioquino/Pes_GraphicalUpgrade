#!/usr/bin/env python3
"""PES21 FX - remera en la mano para los hinchas sin camiseta (pedido de Agustin 08/11: "si no encontras el modelo
crealo vos, con el tono de la piel y la remera en la mano que respete el kit del equipo").
uso: remera_mano.py <audi_model.fpk con pesos de cara> <salida.fpk> [carpeta_vista]

En la malla cercana (LOD 0) de cada au*.fmdl se suman dos remeras chicas (una por mano), grilla de NS x NT puntos con
cara de adelante y de atras (vertices duplicados con la normal dada vuelta):
  - posicion de reposo = la mano (agarre), peso 255 al hueso de la mano (6 izq / 10 der): con los shaders del juego
    nunca se ven porque el COLOR.x cae en el bit 24 de la mascara de partes (int(x * 31) == 24, que el juego siempre
    esconde: posicion 0, triangulos sin area).
  - COLOR: x = 200 + lado (0 izq, 1 der) + 2 * clase de cachete, y = s (a lo largo, 0 agarre .. 255 punta),
    z = t (a lo ancho), w = 255 cara de atras.
  - UV = la del pecho de ESE modelo (punto del frente mas cercano): el PS del juego la pinta con la mascara + paleta del
    hincha (o su atlas), o sea con los colores de su propia camiseta = el kit del equipo.
El VS del mod (pes_crowd_face.hlsli, PesCrowdShirtInHand) la abre y la mueve solo en hinchas sin camiseta: colgando de
la mano, o girando con el brazo cuando la revolea."""
import sys, os, struct
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'fox'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sincamiseta'))
from fmdl import Fmdl
from make_fpk_sin_short import entries
from subdividir_publico import read_mesh, decode, encode, get_attr, rebuild, biggest_mesh, repack, SIZE

NS, NT = 7, 4
# au00.skl (metros del modelo, mira a +z, izquierda = +x)
HAND = {0: (np.array([0.5631, 1.1170, 0.2040]), np.array([0.3991, 1.2620, 0.0138]), 6),
        1: (np.array([-0.5631, 1.1170, 0.2040]), np.array([-0.3991, 1.2620, 0.0138]), 10)}

def grip(side):
    h, f, _ = HAND[side]
    d = h - f; return h + 0.07 * d / np.linalg.norm(d)

def chest_uv(P, UV, dom):
    """uv del frente del pecho para cada (s, t) de la grilla"""
    front = ((dom == 1) | ((dom == 0) & (P[:, 1] > 1.15))) & (P[:, 2] > 0.04) & (np.abs(P[:, 0]) < 0.17)
    if front.sum() < 4: front = (P[:, 1] > 1.2) & (P[:, 1] < 1.45) & (P[:, 2] > 0.0) & (np.abs(P[:, 0]) < 0.2)
    idx = np.nonzero(front)[0]
    out = np.zeros((NS, NT, 2))
    for i in range(NS):
        for j in range(NT):
            x = (j / (NT - 1) - 0.5) * 0.26; y = 1.43 - 0.30 * i / (NS - 1)
            k = idx[np.argmin((P[idx, 0] - x) ** 2 + (P[idx, 1] - y) ** 2 - 0.02 * P[idx, 2])]
            out[i, j] = UV[k]
    return out

def add_shirts(M, cls):
    V = M['nvert']
    vfs = {vf['usage']: vf for vf in M['vfs']}
    P = decode(vfs[0], get_attr(M, vfs[0]))
    UV = decode(vfs[8], get_attr(M, vfs[8]))
    w = decode(vfs[1], get_attr(M, vfs[1])); ix = decode(vfs[7], get_attr(M, vfs[7])).astype(int)
    dom = ix[np.arange(V), np.argmax(w, 1)]
    cuv = chest_uv(P, UV, dom)
    hand_rows = {s: int(np.nonzero(dom == HAND[s][2])[0][0]) for s in (0, 1)}
    rows = []; faces = []
    for side in (0, 1):
        for back in (0, 1):
            base = V + len(rows)
            for i in range(NS):
                for j in range(NT):
                    rows.append((side, back, i, j))
            for i in range(NS - 1):
                for j in range(NT - 1):
                    a = base + i * NT + j; b = a + 1; c = a + NT; d = c + 1
                    faces += ([a, c, b], [b, c, d]) if not back else ([a, b, c], [b, d, c])
    n = len(rows); NV = V + n
    if NV > 65535: raise SystemExit('demasiados vertices')
    streams = []
    for st in M['streams']:
        raw = np.zeros((NV, st['stride']), np.uint8); raw[:V] = st['raw']
        for k, (side, back, i, j) in enumerate(rows): raw[V + k] = st['raw'][hand_rows[side]]
        streams.append(dict(st, raw=raw))
    def put(usage, vals):
        vf = vfs[usage]
        enc = encode(vf, np.asarray(vals, np.float64))
        streams[vf['s']]['raw'][V:, vf['voff']:vf['voff'] + SIZE[vf['fmt']]] = enc
    pos = [grip(s) for (s, b, i, j) in rows]
    put(0, pos)
    put(2, [[0, 0, 1 - 2 * b, 1] for (s, b, i, j) in rows])
    put(14, [[0, -1, 0, 1] for r in rows])
    put(3, [[200 + s + 2 * cls, round(255 * i / (NS - 1)), round(255 * j / (NT - 1)), 255 * b] for (s, b, i, j) in rows])
    put(8, [cuv[i, j] for (s, b, i, j) in rows])
    put(7, [[HAND[s][2], 0, 0, 0] for (s, b, i, j) in rows])
    put(1, [[255, 0, 0, 0] for r in rows])
    NTr = np.concatenate([M['faces'], np.array(faces, np.int64)])
    return NV, NTr, streams, (V, n, len(faces), cuv[NS // 2, NT // 2])

def process(blob, tmp):
    open(tmp, 'wb').write(blob)
    F = Fmdl(tmp); m = biggest_mesh(F); M = read_mesh(F, m)
    vcol = [vf for vf in M['vfs'] if vf['usage'] == 3][0]
    x = decode(vcol, get_attr(M, vcol))[:, 0]
    x = x[(x >= 5.5) & (x <= 7.5)]
    cls = int(np.median(x) > 6.5) if len(x) else 0
    NV, NTr, streams, info = add_shirts(M, cls)
    return rebuild(F, {m: (NV, NTr, streams)}), (m, cls) + info

def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = open(src, 'rb').read(); tmp = dst + '.tmp.fmdl'; items = {}
    for e in entries(d):
        if not e['name'].endswith('.fmdl'): continue
        new, info = process(d[e['off']:e['off'] + e['size']], tmp)
        items[e['name']] = new
        print('%s: malla %d clase %d, %d vert + %d (remeras), %d tri nuevos, uv pecho %s' % (os.path.basename(e['name']), info[0], info[1], info[2], info[3], info[4], np.round(info[5], 3)))
        if len(sys.argv) > 3:
            os.makedirs(sys.argv[3], exist_ok=True); open(os.path.join(sys.argv[3], os.path.basename(e['name'])), 'wb').write(new)
    os.remove(tmp)
    out = repack(d, items); open(dst, 'wb').write(out); print(dst, len(out))

if __name__ == '__main__': main()
