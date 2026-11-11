#!/usr/bin/env python3
"""PES21 FX - cara con mas poligonos (08/11, Agustin: "mejorar la cantidad de poligonos de las caras ... por ahora la cara de
Messi, una subdivision suave"). Solo archivos (livecpk), sin DLL.

uso: subdividir_cara.py <face.fpk original> <salida.fpk> [--fuerza 1.0] [--niveles 1] [--mallas 0,1,2] [--vista carpeta]
     subdividir_cara.py <face_high.fmdl> <salida.fmdl> ...   (tambien un .fmdl suelto)

Por cada malla de la piel de la cara (face_high.fmdl; la boca por dentro, los dientes y la lengua NUNCA: regla de Agustin):
  - cada triangulo -> 4 por nivel; los vertices viejos NO se mueven (la forma y la silueta quedan)
  - el vertice nuevo de cada arista se redondea con PHONG (punto medio proyectado sobre los planos tangentes de sus dos extremos,
    con sus normales): la misma cuenta que la teselacion del mod (pes_face_tess.hlsli), horneada en el archivo
  - los bordes de cada pieza (arista con un solo triangulo, contado sobre la malla soldada por posicion: union con la pieza de al
    lado, abertura de los ojos, cuello) quedan en el punto medio: las piezas siguen encajando y nada se abre
  - uv: interpolados en cada lado del corte de textura (cada copia del vertice con sus propios uv): la textura cae donde caia
  - normal / tangente: promedio renormalizado; color y demas: promedio
  - pesos de huesos: suma de los dos extremos, los 4 mas fuertes, u8 que suman 255 (los gestos por huesos mueven los nuevos igual)
  - se rearman buffers, entradas de malla y rangos de LOD (bloque 17); el resto del fmdl (esqueleto, materiales) no cambia
--vista escribe un .obj antes / despues de cada malla para mirarlos en Blender (uv incluidos).
"""
import sys, os, struct, argparse
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'crowd'))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'sincamiseta'))
from fmdl import Fmdl
from subdividir_publico import read_mesh, get_attr, decode, encode, rebuild, repack, SIZE
from make_fpk_sin_short import entries

# nombres de material / malla que son la boca por dentro (no se tocan nunca)
BOCA = ('oral', 'mouth', 'teeth', 'tooth', 'tongue', 'gum', 'inner')


def mesh_names(F, m):
    """nombre del material de la malla m (cadena del fmdl), '' si no se puede"""
    try:
        cnt, off = F.b0[3]
        matinst = struct.unpack_from('<H', F.d, off + 0x30 * m + 4)[0]
        if 4 in F.b0:   # instancias de material: nombre en la primera palabra
            c4, o4 = F.b0[4]
            if matinst < c4:
                nm = struct.unpack_from('<H', F.d, o4 + 0x10 * matinst)[0]
                if nm < len(F.strings): return F.strings[nm]
    except Exception:
        pass
    return ''


def subdivide_phong(M, strength):
    V = M['nvert']; T = M['faces']
    attrs = {}
    for vf in M['vfs']:
        attrs[(vf['usage'], vf['fmt'], vf['voff'], vf['s'])] = (vf, decode(vf, get_attr(M, vf)))
    pos_key = [k for k in attrs if k[0] == 0][0]
    nrm_key = [k for k in attrs if k[0] == 2]
    P = attrs[pos_key][1]
    N = attrs[nrm_key[0]][1][:, :3] if nrm_key else None
    if N is not None:
        l = np.linalg.norm(N, axis=1, keepdims=True); l[l < 1e-6] = 1; N = N / l
    # aristas de la malla partida (una copia por lado del corte de uv)
    e_split = np.sort(np.concatenate([T[:, [0, 1]], T[:, [1, 2]], T[:, [2, 0]]]), axis=1)
    ue, einv = np.unique(e_split, axis=0, return_inverse=True)
    einv = einv.reshape(3, -1).T
    E = len(ue)
    # bordes de verdad: sobre la malla soldada por posicion, arista con un solo triangulo
    q = np.round(P / 1e-5).astype(np.int64)
    _, weld, inv = np.unique(q, axis=0, return_index=True, return_inverse=True)
    inv = inv.reshape(-1); W = len(weld)
    tri_w = inv[T]
    all_w = np.sort(np.concatenate([tri_w[:, [0, 1]], tri_w[:, [1, 2]], tri_w[:, [2, 0]]]), axis=1)
    key = all_w[:, 0] * W + all_w[:, 1]
    ukey, cnt = np.unique(key, return_counts=True)
    ew = np.sort(inv[ue], axis=1)
    ekey = ew[:, 0] * W + ew[:, 1]
    boundary = cnt[np.searchsorted(ukey, ekey)] != 2
    # Phong: punto medio proyectado sobre los dos planos tangentes
    Pa, Pb = P[ue[:, 0]], P[ue[:, 1]]
    mid = 0.5 * (Pa + Pb)
    if N is not None:
        Na, Nb = N[ue[:, 0]], N[ue[:, 1]]
        pa = mid - np.sum((mid - Pa) * Na, axis=1, keepdims=True) * Na
        pb = mid - np.sum((mid - Pb) * Nb, axis=1, keepdims=True) * Nb
        ph = 0.5 * (pa + pb)
        Ep = np.where(boundary[:, None], mid, mid + strength * (ph - mid))
    else:
        Ep = mid
    out = {}
    for k, (vf, a) in attrs.items():
        if k == pos_key:
            out[k] = np.concatenate([P, Ep]); continue
        if vf['usage'] in (1, 7): continue
        mid_a = 0.5 * (a[ue[:, 0]] + a[ue[:, 1]])
        if vf['usage'] in (2, 14):
            l = np.linalg.norm(mid_a[:, :3], axis=1, keepdims=True); l[l < 1e-6] = 1
            mid_a[:, :3] /= l; mid_a[:, 3] = a[ue[:, 0], 3]
        out[k] = np.concatenate([a, mid_a])
    kw = [k for k in attrs if k[0] == 1]; ki = [k for k in attrs if k[0] == 7]
    if kw and ki:
        w = attrs[kw[0]][1]; ix = attrs[ki[0]][1].astype(np.int64)
        e_ix = np.concatenate([ix[ue[:, 0]], ix[ue[:, 1]]], axis=1)
        e_w = np.concatenate([w[ue[:, 0]], w[ue[:, 1]]], axis=1) * 0.5
        nb = int(max(ix.max(), 0)) + 1
        acc = np.zeros((E, nb))
        for j in range(8): np.add.at(acc, (np.arange(E), e_ix[:, j]), e_w[:, j])
        top = np.argsort(-acc, axis=1)[:, :4]
        tw = np.take_along_axis(acc, top, 1)
        s = tw.sum(1, keepdims=True); s[s == 0] = 1
        q8 = np.floor(tw / s * 255).astype(np.int64)
        q8[:, 0] += 255 - q8.sum(1)
        top = np.where(q8 > 0, top, 0)
        out[kw[0]] = np.concatenate([w, q8.astype(np.float64)])
        out[ki[0]] = np.concatenate([ix.astype(np.float64), top.astype(np.float64)])
    a, b, c = T[:, 0], T[:, 1], T[:, 2]
    ab, bc, ca = V + einv[:, 0], V + einv[:, 1], V + einv[:, 2]
    NT = np.concatenate([np.stack([a, ab, ca], 1), np.stack([ab, b, bc], 1), np.stack([ca, bc, c], 1), np.stack([ab, bc, ca], 1)])
    NV = V + E
    if NV > 65535: raise SystemExit('demasiados vertices: %d (indices de 16 bits): bajar --niveles' % NV)
    streams = []
    for st in M['streams']:
        raw = np.zeros((NV, st['stride']), np.uint8)
        raw[:V] = st['raw']; raw[V:] = st['raw'][ue[:, 0]]
        streams.append(dict(st, raw=raw))
    for k, (vf, _) in attrs.items():
        if k not in out: continue
        streams[vf['s']]['raw'][:, vf['voff']:vf['voff'] + SIZE[vf['fmt']]] = encode(vf, out[k])
    return NV, NT, streams


def as_mesh(M, NV, NT, streams):
    """vuelve a armar un dict de malla (para otro nivel)"""
    return dict(M, nvert=NV, faces=NT, streams=streams)


def write_obj(path, M):
    attrs = {vf['usage']: decode(vf, get_attr(M, vf)) for vf in M['vfs']}
    P = attrs[0]; UV = attrs.get(8); N = attrs.get(2)
    with open(path, 'w') as f:
        for p in P: f.write('v %.6f %.6f %.6f\n' % tuple(p))
        if UV is not None:
            for t in UV: f.write('vt %.6f %.6f\n' % (t[0], 1 - t[1]))
        if N is not None:
            for n in N: f.write('vn %.5f %.5f %.5f\n' % tuple(n[:3]))
        for a, b, c in M['faces'] + 1:
            if UV is not None: f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' % (a, a, a, b, b, b, c, c, c))
            else: f.write('f %d %d %d\n' % (a, b, c))


def process_fmdl(blob, o, tmp, tag):
    open(tmp, 'wb').write(blob)
    F = Fmdl(tmp)
    cnt, off = F.b0[3]
    pick = set(int(x) for x in o.mallas.split(',')) if o.mallas else None
    newmesh = {}; info = []
    for m in range(cnt):
        M = read_mesh(F, m)
        name = mesh_names(F, m)
        skip = any(b in name.lower() for b in BOCA)
        if pick is not None: skip = skip or m not in pick
        if skip or len(M['faces']) == 0:
            info.append('  malla %d %-24s %5d v %5d tri  (sin tocar%s)' % (m, name, M['nvert'], len(M['faces']), ': boca' if any(b in name.lower() for b in BOCA) else ''))
            continue
        if o.vista:
            os.makedirs(o.vista, exist_ok=True); write_obj(os.path.join(o.vista, '%s_m%d_antes.obj' % (tag, m)), M)
        cur = M
        for lvl in range(o.niveles):
            NV, NT, streams = subdivide_phong(cur, o.fuerza)
            cur = as_mesh(cur, NV, NT, streams)
        newmesh[m] = (cur['nvert'], cur['faces'], cur['streams'])
        if o.vista: write_obj(os.path.join(o.vista, '%s_m%d_despues.obj' % (tag, m)), cur)
        info.append('  malla %d %-24s %5d v %5d tri -> %5d v %5d tri' % (m, name, M['nvert'], len(M['faces']), cur['nvert'], len(cur['faces'])))
    out = rebuild(F, newmesh) if newmesh else blob
    return out, info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--fuerza', type=float, default=1.0, help='cuanto del redondeo Phong (1 = entero, 0 = solo partir)')
    ap.add_argument('--niveles', type=int, default=1)
    ap.add_argument('--mallas', help='indices de malla a subdividir (por defecto todas menos la boca)')
    ap.add_argument('--vista', help='carpeta para .obj antes / despues')
    o = ap.parse_args()
    d = open(o.src, 'rb').read()
    tmp = o.dst + '.tmp.fmdl'
    if d[:4] == b'FMDL':
        out, info = process_fmdl(d, o, tmp, os.path.basename(o.src))
        print(os.path.basename(o.src)); print('\n'.join(info))
        open(o.dst, 'wb').write(out)
    else:
        items = {}
        for e in entries(d):
            if not e['name'].endswith('.fmdl'): continue
            base = os.path.basename(e['name'])
            if any(b in base.lower() for b in BOCA): print('%s: boca, sin tocar' % base); continue
            new, info = process_fmdl(d[e['off']:e['off'] + e['size']], o, tmp, base[:-5])
            items[e['name']] = new
            print('%s (%d -> %d bytes)' % (base, e['size'], len(new))); print('\n'.join(info))
        out = repack(d, items)
        open(o.dst, 'wb').write(out)
        for e in entries(out):
            if e['name'] in items: assert out[e['off']:e['off'] + e['size']] == items[e['name']]
    if os.path.exists(tmp): os.remove(tmp)
    print(o.dst, os.path.getsize(o.dst))


if __name__ == '__main__': main()
