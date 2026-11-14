#!/usr/bin/env python3
# PES21 FX (08/11 p, Agustin: "subdividir solo donde mas se nota el bajo nivel poligonal, la punta de la nariz, los labios, cerca de
# los ojos, sin romper el modelo como lo haria un juego moderno"; "por ahora centremonos en la cara de messi, una subdivision suave")
#
# Subdivide la malla de la piel de una cara que volco la DLL (caras_hd\mallas\<cara>_<indices>_<inicio>.mesh, caramalla.cpp) y escribe
# <...>_hd.mesh, que la DLL dibuja en lugar de la del juego (caras_hd.ini malla_hd = 1).
#
# Como trabaja (lo que hace un estudio con una malla de juego: subdivision adaptativa sin tocar la silueta de las piezas):
#   * Suelda los vertices por posicion (las costuras de uv / normales son vertices repetidos en el mismo lugar), asi cada arista se
#     decide UNA vez para todas sus copias y no se abren grietas.
#   * Parte solo las aristas donde la cara se ve facetada: el redondeo de Phong de esa arista (cuanto se aparta el punto medio de la
#     recta segun las normales de sus dos puntas) supera --tope_mm, o el angulo entre las normales supera --angulo. En una cara eso es
#     la punta y las alas de la nariz, los labios, los parpados y el borde de los ojos, el menton y las orejas; las mejillas y la frente,
#     que ya son suaves, quedan como estan.
#   * Las aristas del BORDE de la pieza (abertura del ojo, de la boca, cuello, union con el pelo) nunca se parten: el contorno queda
#     identico y encaja con las demas piezas (ojos, boca v16, que NO se tocan).
#   * Triangulo con 1 / 2 / 3 aristas partidas -> 2 / 3 / 4 triangulos (rojo-verde, sin vertices colgados).
#   * Los vertices del juego no se mueven y conservan su numero; los nuevos van despues. Posicion con Phong (alfa --alfa), uv / color /
#     normal / tangente promediados, huesos: la union de los de sus dos origenes (los 4 de mas peso, renormalizados).
#   * Cada vertice nuevo anota sus dos vertices de origen (gestos esculpidos y peso de borde de la teselacion, t114 en el VS).
#
# Uso:  python subdividir_malla.py <cara>_<n>_<inicio>.mesh [--niveles 1] [--tope_mm 0.12] [--angulo 14] [--alfa 0.75] [--todo]
#       [--vista carpeta]   (escribe antes.obj / despues.obj para mirarlos en Blender)
import argparse, os, struct, sys
import numpy as np

# ------------------------------------------------------------------ formatos DXGI: (componentes, tipo, bytes por componente)
FMT = {
    2: (4, 'f32'), 6: (3, 'f32'), 16: (2, 'f32'), 41: (1, 'f32'),
    3: (4, 'u32'), 7: (3, 'u32'), 17: (2, 'u32'), 42: (1, 'u32'),
    10: (4, 'f16'), 34: (2, 'f16'), 54: (1, 'f16'),
    11: (4, 'un16'), 35: (2, 'un16'), 56: (1, 'un16'),
    12: (4, 'u16'), 36: (2, 'u16'), 57: (1, 'u16'),
    13: (4, 'sn16'), 37: (2, 'sn16'), 58: (1, 'sn16'),
    14: (4, 's16'), 38: (2, 's16'),
    28: (4, 'un8'), 29: (4, 'un8'), 87: (4, 'un8'), 88: (4, 'un8'), 49: (2, 'un8'), 61: (1, 'un8'),
    30: (4, 'u8'), 50: (2, 'u8'), 62: (1, 'u8'),
    31: (4, 'sn8'), 51: (2, 'sn8'), 63: (1, 'sn8'),
    32: (4, 's8'), 52: (2, 's8'),
    24: (4, 'un1010102'), 25: (4, 'u1010102'),
}
NP = {'f32': '<f4', 'u32': '<u4', 'f16': '<f2', 'un16': '<u2', 'u16': '<u2', 'sn16': '<i2', 's16': '<i2',
      'un8': 'u1', 'u8': 'u1', 'sn8': 'i1', 's8': 'i1'}
SCALE = {'un16': 65535.0, 'sn16': 32767.0, 'un8': 255.0, 'sn8': 127.0}


def fmt_size(f):
    n, t = FMT[f]
    if t in ('un1010102', 'u1010102'):
        return 4
    return n * np.dtype(NP[t]).itemsize


def is_int(f):
    return FMT[f][1] in ('u32', 'u16', 's16', 'u8', 's8', 'u1010102')


def decode(raw, f):
    """raw: (N, bytes) uint8 -> (N, comps) float64 (normalizados a -1..1 / 0..1 si son norm)"""
    n, t = FMT[f]
    if t in ('un1010102', 'u1010102'):
        v = raw[:, :4].copy().view('<u4')[:, 0].astype(np.uint64)
        out = np.stack([(v >> 0) & 1023, (v >> 10) & 1023, (v >> 20) & 1023, (v >> 30) & 3], 1).astype(np.float64)
        if t == 'un1010102':
            out /= np.array([1023, 1023, 1023, 3.0])
        return out
    a = raw[:, :fmt_size(f)].copy().view(NP[t]).reshape(len(raw), n).astype(np.float64)
    if t in SCALE:
        a = a / SCALE[t]
        if t.startswith('sn'):
            a = np.maximum(a, -1.0)
    return a


def encode(vals, f):
    """(N, comps) float64 -> (N, bytes) uint8"""
    n, t = FMT[f]
    if t in ('un1010102', 'u1010102'):
        v = vals.copy()
        if t == 'un1010102':
            v = v * np.array([1023, 1023, 1023, 3.0])
        v = np.clip(np.rint(v), 0, [1023, 1023, 1023, 3]).astype(np.uint64)
        w = (v[:, 0] | (v[:, 1] << 10) | (v[:, 2] << 20) | (v[:, 3] << 30)).astype('<u4')
        return w.view(np.uint8).reshape(len(vals), 4)
    dt = np.dtype(NP[t])
    v = vals.copy()
    if t in SCALE:
        v = np.rint(v * SCALE[t])
    if dt.kind in 'iu':
        info = np.iinfo(dt)
        v = np.clip(np.rint(v), info.min, info.max)
    return np.ascontiguousarray(v.astype(dt)).view(np.uint8).reshape(len(vals), n * dt.itemsize)


# ------------------------------------------------------------------ archivo .mesh de la DLL
class Mesh:
    pass


def read_dump(path):
    b = open(path, 'rb').read()
    if b[:8] != b'PESMSH01':
        sys.exit('no es un volcado de caramalla (PESMSH01): ' + path)
    o = 8
    def u(k):
        nonlocal o
        v = struct.unpack_from('<%dI' % k, b, o); o += 4 * k
        return v
    m = Mesh()
    ver, m.count, m.start, base, m.vmin, m.nv, m.topo, m.ibfmt, nel, m.mask = u(10)
    m.base = struct.unpack('<i', struct.pack('<I', base))[0]
    m.stride = list(u(4)); m.off = list(u(4))
    m.el = []
    for _ in range(nel):
        sem = b[o:o + 32].split(b'\0')[0].decode('latin-1'); o += 32
        idx, fmt, slot, off = u(4)
        m.el.append(dict(sem=sem.upper(), idx=idx, fmt=fmt, slot=slot, off=off))
    m.tri = np.frombuffer(b, '<u4', m.count, o).astype(np.int64).reshape(-1, 3); o += 4 * m.count
    m.data = {}
    for s in range(4):
        if (m.mask >> s) & 1:
            n = m.nv * m.stride[s]
            m.data[s] = np.frombuffer(b, np.uint8, n, o).reshape(m.nv, m.stride[s]).copy(); o += n
    if o != len(b):
        sys.exit('volcado con largo inesperado (%d de %d bytes)' % (o, len(b)))
    return m


def write_hd(path, m, tri, data, parents):
    nv_new = next(iter(data.values())).shape[0]
    with open(path, 'wb') as f:
        f.write(b'PESMSHHD')
        f.write(struct.pack('<7I', 1, m.count, m.vmin, m.nv, nv_new, tri.size, m.mask))
        f.write(struct.pack('<4I', *[m.stride[s] if (m.mask >> s) & 1 else 0 for s in range(4)]))
        f.write(tri.astype('<u4').tobytes())
        for s in range(4):
            if (m.mask >> s) & 1:
                f.write(np.ascontiguousarray(data[s]).tobytes())
        f.write(np.asarray(parents, dtype='<u4').reshape(-1, 2).tobytes())


def elem(m, sem, idx=0):
    for e in m.el:
        if e['sem'] == sem and e['idx'] == idx and e['fmt'] in FMT and (m.mask >> e['slot']) & 1:
            return e
    return None


def get(data, e):
    return decode(data[e['slot']][:, e['off']:e['off'] + fmt_size(e['fmt'])], e['fmt'])


def put(data, e, rows, vals):
    data[e['slot']][rows, e['off']:e['off'] + fmt_size(e['fmt'])] = encode(vals, e['fmt'])


# ------------------------------------------------------------------ un nivel de subdivision adaptativa
def subdivide(m, data, tri, parents_of, a_alfa, tope, ang, todo):
    pe = elem(m, 'POSITION') or elem(m, 'SV_POSITION')
    if pe is None:
        sys.exit('la malla no tiene POSITION en un formato conocido')
    P = get(data, pe)[:, :3]
    nv = len(P)
    # soldar por posicion
    key = np.round(P / 1e-6).astype(np.int64)
    _, weld, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    inv = inv.reshape(-1)
    Pw = P[weld]
    # normales geometricas (por area) en los vertices soldados: no dependen del formato de NORMAL del juego
    tw = inv[tri]
    fn = np.cross(Pw[tw[:, 1]] - Pw[tw[:, 0]], Pw[tw[:, 2]] - Pw[tw[:, 0]])
    Nw = np.zeros_like(Pw)
    for k in range(3):
        np.add.at(Nw, tw[:, k], fn)
    Nw /= np.maximum(np.linalg.norm(Nw, axis=1, keepdims=True), 1e-20)
    # aristas soldadas y cuantos triangulos las usan
    ew = np.sort(np.stack([tw[:, [0, 1]], tw[:, [1, 2]], tw[:, [2, 0]]], 1).reshape(-1, 2), 1)
    uniq, einv, ecnt = np.unique(ew, axis=0, return_inverse=True, return_counts=True)
    einv = einv.reshape(-1, 3)
    a, b = uniq[:, 0], uniq[:, 1]
    q = 0.5 * (Pw[a] + Pw[b])
    pa = q - np.sum((q - Pw[a]) * Nw[a], 1, keepdims=True) * Nw[a]
    pb = q - np.sum((q - Pw[b]) * Nw[b], 1, keepdims=True) * Nw[b]
    ph = q + a_alfa * (0.5 * (pa + pb) - q)
    off = np.linalg.norm(ph - q, axis=1)
    cosang = np.sum(Nw[a] * Nw[b], 1)
    interior = ecnt == 2
    split = interior & (todo | (off > tope) | (cosang < np.cos(np.radians(ang))))
    deg = (tw[:, 0] == tw[:, 1]) | (tw[:, 1] == tw[:, 2]) | (tw[:, 2] == tw[:, 0])
    # vertice nuevo por (arista soldada, copia de la arista con sus indices reales)
    newv = {}   # (i, j) indices reales ordenados -> nuevo
    src = []    # (i, j, arista soldada)
    def mid(i, j, e):
        k = (i, j) if i < j else (j, i)
        if k not in newv:
            newv[k] = nv + len(src); src.append((k[0], k[1], e))
        return newv[k]
    out = []
    for t in range(len(tri)):
        v = tri[t]
        if deg[t]:
            out.append(v); continue
        s = [split[einv[t, k]] for k in range(3)]
        n = sum(s)
        if n == 0:
            out.append(v); continue
        m01 = mid(v[0], v[1], einv[t, 0]) if s[0] else -1
        m12 = mid(v[1], v[2], einv[t, 1]) if s[1] else -1
        m20 = mid(v[2], v[0], einv[t, 2]) if s[2] else -1
        if n == 3:
            out += [(v[0], m01, m20), (m01, v[1], m12), (m20, m12, v[2]), (m01, m12, m20)]
            continue
        # rotar para que las partidas queden en posiciones fijas (mismo sentido de giro)
        r = [0, 1, 2]; mm = [m01, m12, m20]
        while True:
            if n == 1 and s[r[0]]:
                break
            if n == 2 and s[r[0]] and s[r[1]]:
                break
            r = r[1:] + r[:1]
        A, B, C = v[r[0]], v[(r[0] + 1) % 3], v[(r[0] + 2) % 3]
        mAB = mm[r[0]]; mBC = mm[(r[0] + 1) % 3]
        if n == 1:
            out += [(A, mAB, C), (mAB, B, C)]
        else:
            # cortar el cuadrilatero A-mAB-mBC-C por la diagonal mas corta
            out.append((mAB, B, mBC))
            pA, pC = P[A], P[C]
            pm1 = ph[einv[t, r[0]]]; pm2 = ph[einv[t, (r[0] + 1) % 3]]
            if np.linalg.norm(pA - pm2) <= np.linalg.norm(pC - pm1):
                out += [(A, mAB, mBC), (A, mBC, C)]
            else:
                out += [(A, mAB, C), (mAB, mBC, C)]
    out = np.asarray(out, dtype=np.int64).reshape(-1, 3)
    nn = len(src)
    if nn == 0:
        return data, tri, parents_of, 0
    si = np.array([s[0] for s in src]); sj = np.array([s[1] for s in src]); se = np.array([s[2] for s in src])
    nd = {}
    for s, arr in data.items():
        # bytes sin elemento conocido: copia del primer origen
        nd[s] = np.concatenate([arr, arr[si]], 0)
    rows = np.arange(nv, nv + nn)
    done = set()
    # huesos: indices + pesos juntos
    for e in m.el:
        sem = e['sem']
        if (m.mask >> e['slot']) & 1 == 0 or e['fmt'] not in FMT:
            continue
        if sem == 'BLENDINDICES':
            we = elem(m, 'BLENDWEIGHT', e['idx']) or elem(m, 'BLENDWEIGHTS', e['idx'])
            Ii = get(data, e); Ij = Ii[sj]; Ii = Ii[si]
            if we is None:
                put(nd, e, rows, Ii); done.add(id(e)); continue
            Wi = get(data, we)[si]; Wj = get(data, we)[sj]
            nc = Ii.shape[1]
            Io = np.zeros_like(Ii); Wo = np.zeros((nn, Wi.shape[1]))
            for r in range(nn):
                acc = {}
                for k in range(min(nc, Wi.shape[1])):
                    if Wi[r, k] > 0: acc[int(Ii[r, k])] = acc.get(int(Ii[r, k]), 0) + 0.5 * Wi[r, k]
                    if Wj[r, k] > 0: acc[int(Ij[r, k])] = acc.get(int(Ij[r, k]), 0) + 0.5 * Wj[r, k]
                best = sorted(acc.items(), key=lambda x: -x[1])[:min(nc, Wi.shape[1])]
                tot = sum(w for _, w in best) or 1.0
                for k, (bi, w) in enumerate(best):
                    Io[r, k] = bi; Wo[r, k] = w / tot
            if FMT[we['fmt']][1] in SCALE:   # que los pesos cuantizados sumen exactamente 1
                sc = SCALE[FMT[we['fmt']][1]]
                q = np.rint(Wo * sc); q[:, 0] += sc - q.sum(1); Wo = q / sc
            put(nd, e, rows, Io); put(nd, we, rows, Wo); done.add(id(e)); done.add(id(we))
    for e in m.el:
        if id(e) in done or (m.mask >> e['slot']) & 1 == 0 or e['fmt'] not in FMT:
            continue
        sem = e['sem']
        if sem.startswith('BLENDWEIGHT'):
            continue
        if sem in ('POSITION', 'SV_POSITION') and e is pe:
            full = get(data, e)
            v = full[si].copy(); v[:, :3] = ph[se]
            put(nd, e, rows, v); continue
        if is_int(e['fmt']):
            continue   # enteros sin pesos: los del primer origen (ya copiados)
        vi = get(data, e)[si]; vj = get(data, e)[sj]
        v = 0.5 * (vi + vj)
        if sem in ('NORMAL', 'TANGENT', 'BINORMAL') and v.shape[1] >= 3:
            unorm = FMT[e['fmt']][1].startswith('un')
            x = v[:, :3] * 2 - 1 if unorm else v[:, :3]
            x /= np.maximum(np.linalg.norm(x, axis=1, keepdims=True), 1e-12)
            v[:, :3] = (x + 1) * 0.5 if unorm else x
            if v.shape[1] == 4:
                v[:, 3] = vi[:, 3]   # signo de la bitangente
        put(nd, e, rows, v)
    # origenes en la malla ORIGINAL del juego (con varios niveles: los de mas arriba)
    par = []
    for i, j in zip(si, sj):
        ri = parents_of(i); rj = parents_of(j)
        x = ri[0]; y = rj[0] if rj[0] != x else rj[1]
        par.append((x, y))
    old = parents_of
    m_nv = nv
    newpar = list(par)
    def parents_new(v):
        if v < m_nv:
            return old(v)
        return newpar[v - m_nv]
    return nd, out, parents_new, nn


def write_obj(path, P, tri):
    with open(path, 'w') as f:
        for p in P:
            f.write('v %.6f %.6f %.6f\n' % tuple(p))
        for t in tri:
            f.write('f %d %d %d\n' % (t[0] + 1, t[1] + 1, t[2] + 1))


def main():
    ap = argparse.ArgumentParser(description='cara con mas poligonos: subdivide el volcado de caramalla (.mesh) -> _hd.mesh')
    ap.add_argument('mesh')
    ap.add_argument('--niveles', type=int, default=1, help='pasadas de subdivision (1 o 2)')
    ap.add_argument('--tope_mm', type=float, default=0.12, help='partir la arista si el redondeo de Phong la aparta mas que esto (mm)')
    ap.add_argument('--angulo', type=float, default=14.0, help='o si las normales de sus puntas difieren mas que esto (grados)')
    ap.add_argument('--alfa', type=float, default=0.75, help='redondeo de Phong (0 = punto medio recto, 1 = Phong completo)')
    ap.add_argument('--todo', action='store_true', help='partir todas las aristas interiores (1 -> 4 uniforme)')
    ap.add_argument('--unidad', type=float, default=1.0, help='metros por unidad del modelo (la cara del juego esta en metros)')
    ap.add_argument('--vista', help='carpeta para antes.obj / despues.obj')
    ap.add_argument('--salida', help='archivo de salida (por defecto <entrada>_hd.mesh)')
    a = ap.parse_args()
    m = read_dump(a.mesh)
    data = {s: v.copy() for s, v in m.data.items()}
    tri = m.tri.copy()
    if tri.size and (tri.max() >= m.nv):
        sys.exit('indices fuera de la malla')
    pe = elem(m, 'POSITION')
    print('cara: %d triangulos, %d vertices, layout: %s' % (len(tri), m.nv, ', '.join('%s%d fmt %d slot %d +%d' % (e['sem'], e['idx'], e['fmt'], e['slot'], e['off']) for e in m.el)))
    if a.vista:
        os.makedirs(a.vista, exist_ok=True)
        write_obj(os.path.join(a.vista, 'antes.obj'), get(data, pe)[:, :3], tri)
    parents = lambda v: (v, v)
    tope = a.tope_mm * 0.001 / a.unidad
    for lv in range(max(1, a.niveles)):
        before = len(tri)
        data, tri, parents, nn = subdivide(m, data, tri, parents, a.alfa, tope, a.angulo, a.todo)
        print('nivel %d: %d -> %d triangulos (+%d vertices)' % (lv + 1, before, len(tri), nn))
    nv_new = next(iter(data.values())).shape[0]
    par = [parents(v) for v in range(m.nv, nv_new)]
    out = a.salida or (os.path.splitext(a.mesh)[0] + '_hd.mesh')
    write_hd(out, m, tri, data, par)
    if a.vista:
        write_obj(os.path.join(a.vista, 'despues.obj'), get(data, pe)[:, :3], tri)
    print('escrito %s: %d triangulos, %d vertices (antes %d / %d)' % (out, len(tri), nv_new, m.count // 3, m.nv))


if __name__ == '__main__':
    main()
