#!/usr/bin/env python3
"""PES21 FX - modelos del publico con mas malla (solo archivos, livecpk; sin DLL).
uso: subdividir_publico.py <audi_model.fpk original> <salida.fpk> [fuerza_suavizado=0.6] [carpeta_vista]
Por cada au*.fmdl del fpk:
  - la malla de mas detalle (la LOD cercana; las otras dos no se tocan) se subdivide 1 vez: cada triangulo -> 4
  - posiciones: punto medio + suavizado tipo Loop sobre la malla soldada por posicion (los cortes de UV no se abren),
    mezclado con 'fuerza'; bordes abiertos (punos, cuello) quedan fijos como curva
  - uv / color / normal / tangente: interpolados en cada lado del corte (normal y tangente renormalizadas)
  - pesos: suma de los dos extremos, se quedan los 4 huesos mas fuertes, u8 que suman 255
  - se rearman los buffers de vertices e indices, las entradas de malla, los rangos de LOD (bloque 17) y el fpk
No cambia esqueleto, materiales, texturas ni parametros de instancia (ModelId, ShirtId, FaceId...)."""
import sys, os, struct
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'fox'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sincamiseta'))
from fmdl import Fmdl
from make_fpk_sin_short import entries

A16 = lambda n: (n + 15) & ~15

# ------------------------------------------------------------------ lectura cruda de una malla (todos los atributos)
def read_mesh(F, m):
    d = F.d
    cnt, off = F.b0[3]
    e = off + 0x30 * m
    matinst, bgroup, fidx, nvert = struct.unpack_from('<HHHH', d, e + 4)
    first, fcount = struct.unpack_from('<II', d, e + 0x10)
    nmf, nvf, _, _, fmf, fvf = struct.unpack_from('<BBBBHH', d, F.b0[9][1] + 8 * fidx)
    bo_cnt, bo_off = F.b0[14]
    bufs = [struct.unpack_from('<IIII', d, bo_off + 16 * i) for i in range(bo_cnt)]
    base = F.b1[2][0]
    streams = []   # (buffer index, stride, offset dentro del buffer, bytes nvert x stride)
    seen = {}
    for k in range(nmf):
        bidx, nf, stride, typ, moff = struct.unpack_from('<BBBBI', d, F.b0[10][1] + 8 * (fmf + k))
        key = (bidx, moff)
        if key not in seen:
            a = base + bufs[bidx][2] + moff
            raw = np.frombuffer(d, np.uint8, stride * nvert, a).reshape(nvert, stride).copy()
            seen[key] = len(streams)
            streams.append(dict(bidx=bidx, stride=stride, moff=moff, raw=raw))
    vfs = []
    vi = 0
    for k in range(nmf):
        bidx, nf, stride, typ, moff = struct.unpack_from('<BBBBI', d, F.b0[10][1] + 8 * (fmf + k))
        for usage, fmt, voff in [struct.unpack_from('<BBH', d, F.b0[11][1] + 4 * (fvf + vi + i)) for i in range(nf)]:
            vfs.append(dict(usage=usage, fmt=fmt, voff=voff, s=seen[(bidx, moff)]))
        vi += nf
    ib = [b for b in bufs if b[0] == 1][0]
    faces = np.frombuffer(d, '<u2', fcount, base + ib[2] + 2 * first).astype(np.int64).reshape(-1, 3)
    return dict(entry=e, fidx=fidx, nvert=nvert, first=first, fcount=fcount, streams=streams, vfs=vfs, faces=faces)

SIZE = {1: 12, 6: 8, 7: 4, 8: 4, 9: 4}

def get_attr(M, vf):
    st = M['streams'][vf['s']]
    return st['raw'][:, vf['voff']:vf['voff'] + SIZE[vf['fmt']]]

def decode(vf, a):
    f = vf['fmt']
    if f == 1: return np.ascontiguousarray(a).view(np.float32).reshape(-1, 3).astype(np.float64)
    if f == 6: return np.ascontiguousarray(a).view(np.float16).reshape(-1, 4).astype(np.float64)
    if f == 7: return np.ascontiguousarray(a).view(np.float16).reshape(-1, 2).astype(np.float64)
    return a.astype(np.float64)   # 8 / 9: u8 x4

def encode(vf, v):
    f = vf['fmt']
    if f == 1: return v.astype(np.float32).view(np.uint8).reshape(-1, 12)
    if f in (6, 7): return v.astype(np.float16).view(np.uint8).reshape(-1, SIZE[f])
    return np.clip(np.rint(v), 0, 255).astype(np.uint8)

# ------------------------------------------------------------------ subdivision
def subdivide(M, strength):
    V = M['nvert']; T = M['faces']
    attrs = {}
    for vf in M['vfs']:
        attrs[(vf['usage'], vf['fmt'], vf['voff'], vf['s'])] = (vf, decode(vf, get_attr(M, vf)))
    pos_key = [k for k in attrs if k[0] == 0][0]
    P = attrs[pos_key][1]
    # soldar por posicion
    q = np.round(P / 1e-5).astype(np.int64)
    _, weld, inv = np.unique(q, axis=0, return_index=True, return_inverse=True)
    inv = inv.reshape(-1); W = len(weld)
    # aristas sobre la malla partida (para crear los vertices nuevos) y sobre la soldada (para el suavizado)
    e_split = np.sort(np.concatenate([T[:, [0, 1]], T[:, [1, 2]], T[:, [2, 0]]]), axis=1)
    ue, einv = np.unique(e_split, axis=0, return_inverse=True)
    einv = einv.reshape(3, -1).T          # (ntri, 3): arista 01, 12, 20
    ew = np.sort(inv[ue], axis=1)
    uw, wmap, wcount_inv = np.unique(ew, axis=0, return_index=True, return_inverse=True)
    wcount_inv = wcount_inv.reshape(-1)
    # caras por arista soldada (para ver bordes y vertices opuestos)
    tri_w = inv[T]
    all_w = np.sort(np.concatenate([tri_w[:, [0, 1]], tri_w[:, [1, 2]], tri_w[:, [2, 0]]]), axis=1)
    opp = np.concatenate([tri_w[:, 2], tri_w[:, 0], tri_w[:, 1]])
    key = all_w[:, 0] * W + all_w[:, 1]
    ukey = uw[:, 0] * W + uw[:, 1]
    pos_in = np.searchsorted(ukey, key)
    nface = np.bincount(pos_in, minlength=len(uw))
    Pw = P[weld]
    boundary = nface != 2
    # posicion de la arista soldada (Loop): 3/8 extremos + 1/8 opuestos; borde: punto medio
    opp_sum = np.zeros((len(uw), 3)); np.add.at(opp_sum, pos_in, Pw[opp])
    mid = 0.5 * (Pw[uw[:, 0]] + Pw[uw[:, 1]])
    loop_e = np.where(boundary[:, None], mid, 0.75 * mid + 0.125 * opp_sum)
    Ep = mid + strength * (loop_e - mid)
    # vertices viejos (Loop): interior beta; borde: 3/4 + 1/8 vecinos de borde
    nb_sum = np.zeros((W, 3)); nb_n = np.zeros(W)
    for a, b in ((0, 1), (1, 0)):
        np.add.at(nb_sum, uw[:, a], Pw[uw[:, b]]); np.add.at(nb_n, uw[:, a], 1)
    bnd_sum = np.zeros((W, 3)); bnd_n = np.zeros(W)
    for a, b in ((0, 1), (1, 0)):
        np.add.at(bnd_sum, uw[boundary, a], Pw[uw[boundary, b]]); np.add.at(bnd_n, uw[boundary, a], 1)
    n = np.maximum(nb_n, 1)
    beta = np.where(n > 3, 3.0 / (8 * n), 3.0 / 16)
    loop_v = (1 - n * beta)[:, None] * Pw + beta[:, None] * nb_sum
    on_b = bnd_n > 0
    loop_vb = 0.75 * Pw + 0.125 * bnd_sum
    loop_v = np.where((bnd_n == 2)[:, None], loop_vb, loop_v)
    loop_v = np.where((on_b & (bnd_n != 2))[:, None], Pw, loop_v)   # esquinas raras: fijas
    Vp = Pw + strength * (loop_v - Pw)
    newP_old = Vp[inv]
    newP_edge = Ep[wcount_inv]
    # atributos
    E = len(ue)
    out = {}
    for k, (vf, a) in attrs.items():
        if k == pos_key:
            out[k] = np.concatenate([newP_old, newP_edge]); continue
        if vf['usage'] in (1, 7): continue
        mid_a = 0.5 * (a[ue[:, 0]] + a[ue[:, 1]])
        if vf['usage'] in (2, 14):   # normal / tangente: renormalizar xyz, w (signo) del primero
            l = np.linalg.norm(mid_a[:, :3], axis=1, keepdims=True); l[l < 1e-6] = 1
            mid_a[:, :3] /= l; mid_a[:, 3] = a[ue[:, 0], 3]
        out[k] = np.concatenate([a, mid_a])
    # pesos
    kw = [k for k in attrs if k[0] == 1]; ki = [k for k in attrs if k[0] == 7]
    if kw and ki:
        w = attrs[kw[0]][1]; ix = attrs[ki[0]][1].astype(np.int64)
        e_ix = np.concatenate([ix[ue[:, 0]], ix[ue[:, 1]]], axis=1)
        e_w = np.concatenate([w[ue[:, 0]], w[ue[:, 1]]], axis=1) * 0.5
        # juntar indices repetidos
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
    # caras nuevas
    a, b, c = T[:, 0], T[:, 1], T[:, 2]
    ab, bc, ca = V + einv[:, 0], V + einv[:, 1], V + einv[:, 2]
    NT = np.concatenate([np.stack([a, ab, ca], 1), np.stack([ab, b, bc], 1), np.stack([ca, bc, c], 1), np.stack([ab, bc, ca], 1)])
    NV = V + E
    if NV > 65535: raise SystemExit('demasiados vertices: %d' % NV)
    # rearmar streams
    streams = []
    for si, st in enumerate(M['streams']):
        raw = np.zeros((NV, st['stride']), np.uint8)
        raw[:V] = st['raw']
        raw[V:] = st['raw'][ue[:, 0]]   # bytes de relleno / atributos no conocidos: copia del extremo
        streams.append(dict(st, raw=raw))
    for k, (vf, _) in attrs.items():
        if k not in out: continue
        enc = encode(vf, out[k])
        streams[vf['s']]['raw'][:, vf['voff']:vf['voff'] + SIZE[vf['fmt']]] = enc
    return NV, NT, streams

# ------------------------------------------------------------------ reescritura del fmdl
def rebuild(F, newmesh):
    """newmesh: {indice de malla: (nvert, faces, streams)}; el resto se copia tal cual"""
    d = bytearray(F.d)
    cnt, off = F.b0[3]
    meshes = [read_mesh(F, m) for m in range(cnt)]
    bo_cnt, bo_off = F.b0[14]
    bufs = [list(struct.unpack_from('<IIII', d, bo_off + 16 * i)) for i in range(bo_cnt)]
    # contenidos nuevos por buffer: lista de (malla, stream) en el orden original de offsets
    vb_parts = {i: [] for i in range(bo_cnt) if bufs[i][0] == 0}
    for mi, M in enumerate(meshes):
        st_list = newmesh[mi][2] if mi in newmesh else M['streams']
        for si, st in enumerate(st_list):
            vb_parts[st['bidx']].append((st['moff'], mi, si, st['raw'].tobytes()))
    new_moff = {}
    new_bufdata = {}
    for bi, parts in vb_parts.items():
        parts.sort()
        blob = bytearray()
        for moff, mi, si, raw in parts:
            blob += b'\0' * (A16(len(blob)) - len(blob))
            new_moff[(mi, si)] = len(blob); blob += raw
        blob += b'\0' * (A16(len(blob)) - len(blob))
        new_bufdata[bi] = bytes(blob)
    ibi = [i for i in range(bo_cnt) if bufs[i][0] == 1][0]
    iblob = bytearray(); new_first = {}
    for mi, M in enumerate(meshes):
        faces = newmesh[mi][1] if mi in newmesh else M['faces']
        new_first[mi] = len(iblob) // 2
        iblob += faces.astype('<u2').tobytes()
    iblob += b'\0' * (A16(len(iblob)) - len(iblob))
    new_bufdata[ibi] = bytes(iblob)
    # bloque 10: offsets de cada formato de malla
    for mi, M in enumerate(meshes):
        nmf, nvf, _, _, fmf, fvf = struct.unpack_from('<BBBBHH', d, F.b0[9][1] + 8 * M['fidx'])
        seen = {}
        for k in range(nmf):
            o = F.b0[10][1] + 8 * (fmf + k)
            bidx, nf, stride, typ, moff = struct.unpack_from('<BBBBI', d, o)
            si = [i for i, st in enumerate(M['streams']) if st['bidx'] == bidx and st['moff'] == moff][0]
            struct.pack_into('<I', d, o + 4, new_moff[(mi, si)])
        # entrada de malla
        nv = newmesh[mi][0] if mi in newmesh else M['nvert']
        fc = (len(newmesh[mi][1]) * 3) if mi in newmesh else M['fcount']
        struct.pack_into('<H', d, M['entry'] + 10, nv)
        struct.pack_into('<II', d, M['entry'] + 0x10, new_first[mi], fc)
        # bloque 17: rangos por LOD (relativos a la malla); la subdivision usa todo en todos los niveles
        lodfirst, = struct.unpack_from('<Q', d, M['entry'] + 0x18)
        if mi in newmesh and 17 in F.b0:
            nl = struct.unpack_from('<I', d, F.b0[16][1])[0] if 16 in F.b0 else 1
            for L in range(nl):
                struct.pack_into('<II', d, F.b0[17][1] + 8 * (lodfirst + L), 0, fc)
    # buffers y seccion 1
    new_buf = bytearray(); bstart = {}
    for bi in range(bo_cnt):
        new_buf += b'\0' * (A16(len(new_buf)) - len(new_buf))
        bstart[bi] = len(new_buf); new_buf += new_bufdata[bi]
        struct.pack_into('<II', d, bo_off + 16 * bi + 4, len(new_bufdata[bi]), bstart[bi])
    # seccion 1: rearmar en el orden original de offsets
    info = struct.unpack_from('<Q', d, 8)[0]
    n0, n1 = struct.unpack_from('<II', d, 32)
    b1e = []
    for i in range(n1):
        o = info + 8 * n0 + 12 * i
        t, so, ln = struct.unpack_from('<III', d, o)
        b1e.append([o, t, so, ln])
    s0, s0len, s1, s1len = struct.unpack_from('<IIII', d, 40)
    sec1 = bytearray()
    for e in sorted(b1e, key=lambda x: x[2]):
        o, t, so, ln = e
        sec1 += b'\0' * (A16(len(sec1)) - len(sec1))
        data = bytes(new_buf) if t == 2 else F.d[s1 + so: s1 + so + ln]
        struct.pack_into('<II', d, o + 4, len(sec1), len(data))
        sec1 += data
    sec1 += b'\0' * (A16(len(sec1)) - len(sec1))
    struct.pack_into('<I', d, 52, len(sec1))
    return bytes(d[:s1]) + bytes(sec1)

def biggest_mesh(F):
    cnt, off = F.b0[3]
    fc = [struct.unpack_from('<I', F.d, off + 0x30 * m + 0x14)[0] for m in range(cnt)]
    return int(np.argmax(fc))

def process_fmdl(blob, strength, tmp):
    open(tmp, 'wb').write(blob)
    F = Fmdl(tmp)
    m = biggest_mesh(F)
    M = read_mesh(F, m)
    NV, NT, streams = subdivide(M, strength)
    out = rebuild(F, {m: (NV, NT, streams)})
    return out, (m, M['nvert'], len(M['faces']), NV, len(NT))

def repack(src, items):
    """items: {nombre: bytes nuevos}"""
    d = src
    ents = entries(d)
    first = min(e['off'] for e in ents)
    head = bytearray(d[:first])
    body = bytearray()
    for i, e in enumerate(sorted(ents, key=lambda x: x['off'])):
        blob = items.get(e['name'], d[e['off']:e['off'] + e['size']])
        pos = first + len(body)
        idx = ents.index(e)
        struct.pack_into('<I', head, 48 + 48 * idx, pos)
        struct.pack_into('<I', head, 48 + 48 * idx + 8, len(blob))
        body += blob
        body += b'\0' * (A16(len(body)) - len(body))
    out = head + body
    struct.pack_into('<I', out, 10, len(out))
    return bytes(out)

def main():
    src, dst = sys.argv[1], sys.argv[2]
    strength = float(sys.argv[3]) if len(sys.argv) > 3 else 0.6
    vista = sys.argv[4] if len(sys.argv) > 4 else None
    d = open(src, 'rb').read()
    tmp = dst + '.tmp.fmdl'
    items = {}
    for e in entries(d):
        if not e['name'].endswith('.fmdl'): continue
        new, info = process_fmdl(d[e['off']:e['off'] + e['size']], strength, tmp)
        items[e['name']] = new
        print('%s: malla %d  %d vert / %d tri -> %d vert / %d tri  (%d -> %d bytes)' % (
            os.path.basename(e['name']), info[0], info[1], info[2], info[3], info[4], e['size'], len(new)))
        if vista:
            os.makedirs(vista, exist_ok=True)
            open(os.path.join(vista, os.path.basename(e['name'])), 'wb').write(new)
    os.remove(tmp)
    out = repack(d, items)
    open(dst, 'wb').write(out)
    # verificacion: se releen todas las entradas
    for e in entries(out):
        if e['name'] in items: assert out[e['off']:e['off'] + e['size']] == items[e['name']]
    print(dst, len(out))

if __name__ == '__main__': main()
