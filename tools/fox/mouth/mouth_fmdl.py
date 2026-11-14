# PES21 FX (31/10) - escribe un mouth.fmdl con OTRA cantidad de vertices y triangulos sobre el de Konami (una sola malla,
# mismo material oral_mat, mismos 6 huesos y mismo formato de vertice):
#   buffer 0: posicion float3 (12 B);  buffer 1: normal half4, tangente half4, pesos u8x4, huesos u8x4 (indice del grupo), uv half2 (28 B)
#   buffer 2: indices u16
# Cambia: bloque 1/2 (buffers) y lo que viene despues (strings) se corre; tabla 14 (largo / offset de cada buffer), malla (tabla 3:
# vertices y cantidad de indices), caras por LOD (tabla 17: todos los LOD dibujan la malla entera), cajas (tabla 13) y la cabecera.
# Tambien: fpk_replace(), cambia una entrada de un fpk por un archivo de OTRO tamano (corre las demas, alineadas a 16).
import struct, hashlib
import numpy as np
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import fmdl

def pad16(b): return b + b'\0' * ((-len(b)) % 16)
def pad32(b): return b + b'\0' * ((-len(b)) % 32)   # el de Konami deja los indices alineados a 32

def streams(pos, nrm, tan, w, bidx, uv):
    """los dos buffers de vertices tal como van al fmdl (y a la GPU): 0 = posicion float3 (12 B), 1 = normal/tangente half4, pesos u8x4,
    huesos u8x4, uv half2 (28 B). Devuelve (b0, b1, pesos cuantizados)"""
    N = len(pos)
    wq = np.clip(np.round(np.asarray(w, np.float64) * 255), 0, 255).astype(np.int32)
    wq[:, 0] += 255 - wq.sum(1)                       # que sumen 255 exacto
    assert (wq >= 0).all()
    b0 = np.ascontiguousarray(pos, np.float32).tobytes()
    rec = np.zeros((N, 28), np.uint8)
    rec[:, 0:8] = np.ascontiguousarray(np.asarray(nrm, np.float32).astype(np.float16)).view(np.uint8).reshape(N, 8)
    rec[:, 8:16] = np.ascontiguousarray(np.asarray(tan, np.float32).astype(np.float16)).view(np.uint8).reshape(N, 8)
    rec[:, 16:20] = wq.astype(np.uint8); rec[:, 20:24] = np.asarray(bidx).astype(np.uint8)
    rec[:, 24:28] = np.ascontiguousarray(np.asarray(uv, np.float32).astype(np.float16)).view(np.uint8).reshape(N, 4)
    return b0, rec.tobytes(), wq

def write(src, dst, pos, nrm, tan, w, bidx, uv, faces, log=print):
    """pos (N,3) f32; nrm/tan (N,4) (tan.w = signo de la binormal); w (N,4) 0..1; bidx (N,4) indices DEL GRUPO de huesos (0..5);
    uv (N,2); faces (M,3) indices locales"""
    f = fmdl.Fmdl(src); d = bytearray(f.d)
    N = len(pos); M = len(faces)
    assert N < 65536 and faces.max() < N
    assert f.b0[3][0] == 1, 'se espera una sola malla'
    b0, b1, wq = streams(pos, nrm, tan, w, bidx, uv)
    b2 = np.ascontiguousarray(faces, '<u2').tobytes()
    b2 = pad32(b2)
    buf = pad16(b0) + pad16(b1) + b2
    # tabla 14
    cnt, off = f.b0[14]; assert cnt == 3
    o = 0
    for i, part in enumerate((b0, b1, b2)):
        typ = struct.unpack_from('<I', d, off + 16 * i)[0]
        struct.pack_into('<III', d, off + 16 * i, typ, len(pad16(part)), o); o += len(pad16(part))
    # malla
    cnt, off = f.b0[3]
    struct.pack_into('<H', d, off + 10, N); struct.pack_into('<II', d, off + 0x10, 0, M * 3)
    # caras por LOD (8 B: primer indice, cantidad)
    cnt, off = f.b0[17]
    for i in range(cnt): struct.pack_into('<II', d, off + 8 * i, 0, M * 3)
    # cajas: 0 = malla entera; 1.. = una por hueso del grupo (los vertices que pesan en ese hueso)
    cnt, off = f.b0[13]
    def box(P):
        mx, mn = P.max(0), P.min(0); return struct.pack('<8f', mx[0], mx[1], mx[2], 1.0, mn[0], mn[1], mn[2], 1.0)
    d[off:off + 32] = box(pos)
    grp = f.groups[0]
    for k in range(1, cnt):
        bone = k - 1   # las cajas 1..6 siguen el orden de los huesos (tabla 0: campo bbox)
        sel = ((np.asarray(bidx) == grp.index(bone)) & (wq > 0)).any(1) if bone in grp else np.zeros(N, bool)
        if sel.any(): d[off + 32 * k: off + 32 * (k + 1)] = box(pos[sel])
    # seccion 1: reemplazar el bloque de buffers (tipo 2) y correr lo que sigue
    s1 = f.s1; info = 64
    n0, n1 = struct.unpack_from('<II', d, 32)
    t1 = info + 8 * n0
    ents = [list(struct.unpack_from('<III', d, t1 + 12 * i)) for i in range(n1)]
    k2 = [e[0] for e in ents].index(2)
    old_off, old_len = ents[k2][1], ents[k2][2]
    delta = len(buf) - old_len
    body = bytes(d[s1:])
    new1 = body[:old_off] + buf + body[old_off + old_len:]
    for i, e in enumerate(ents):
        if e[1] > old_off: e[1] += delta
        if i == k2: e[2] = len(buf)
        struct.pack_into('<III', d, t1 + 12 * i, *e)
    out = bytes(d[:s1]) + new1
    out = bytearray(out)
    struct.pack_into('<I', out, 52, len(new1))   # largo de la seccion 1 (ultimo campo de la cabecera)
    open(dst, 'wb').write(bytes(out))
    log('  %s: %d vertices, %d triangulos, %d bytes' % (os.path.basename(dst), N, M, len(out)))
    return bytes(out)

def fpk_replace(src_bytes, name_suffix, blob):
    """fpk (bytes) con la entrada que termina en name_suffix cambiada por blob (cualquier tamano)"""
    d = bytearray(src_bytes)
    magic, count, refs = struct.unpack_from('<III', d, 32)
    ents = []
    for i in range(count):
        o = 48 + 48 * i
        doff, _, dsz = struct.unpack_from('<III', d, o)
        noff, _, nlen = struct.unpack_from('<III', d, o + 16)
        ents.append([i, doff, dsz, d[noff:noff + nlen].decode('ascii')])
    hit = [e for e in ents if e[3].endswith(name_suffix)]
    assert len(hit) == 1, name_suffix
    order = sorted(ents, key=lambda e: e[1])
    first = order[0][1]
    head = bytes(d[:first])
    datas = {e[0]: bytes(d[e[1]:e[1] + e[2]]) for e in ents}
    datas[hit[0][0]] = blob
    out = bytearray(head); newoff = {}
    for e in order:
        out += b'\0' * ((-len(out)) % 16)
        newoff[e[0]] = len(out); out += datas[e[0]]
    out += b'\0' * ((-len(out)) % 16) if len(src_bytes) % 16 == 0 else b''
    for e in ents:
        o = 48 + 48 * e[0]
        struct.pack_into('<I', out, o, newoff[e[0]]); struct.pack_into('<I', out, o + 8, len(datas[e[0]]))
    struct.pack_into('<I', out, 10, len(out))
    return bytes(out)

if __name__ == '__main__':
    # prueba: reescribir la malla de Konami tal cual
    src, dst = sys.argv[1], sys.argv[2]
    f = fmdl.Fmdl(src); m = fmdl.meshes(f)[0]
    grp = f.groups[0]
    raw = np.frombuffer(f.d, np.uint8)
    bidx = np.array([[grp.index(b) for b in row] for row in m['idx']])
    out = write(src, dst, m['pos'], m['nrm'], m['tan'], m['w'], bidx, m['uv'], m['faces'])
    print('identico' if out == f.d else 'DISTINTO')
