# PES21 FX (26/10 c) - escribe la pelvis en una copia de pants_*_sub.fmdl, en el lugar de la malla 0 (el short), sin cambiar el
# tamano del archivo ni el de ningun bloque:
#   - malla 0: material = el del torso (torso_mat, piel), alfa 0 (como el torso); mismas cantidades de vertices y de indices
#     (lo que sobra: vertices copia del 0 y triangulos vacios)
#   - huesos que usaba solo el short pasan a ser los del muslo (para que el borde de abajo se mueva igual que el muslo):
#     dsk_hem_ba_l -> sk_thigh_l, dsk_hem_o_fake_l -> dsk_thighmain_l, dsk_hem_i_l -> dsk_thigh_l (y los _r). El nombre se
#     reescribe en el mismo lugar (es mas corto) y la entrada del hueso copia padre / posiciones del de thigh_short.fmdl
#   - cajas (bloque 13) de los huesos renombrados = las de los vertices de la pelvis que pesan en ellos
import struct
import numpy as np

RENAMES = {"dsk_hem_ba_l": "sk_thigh_l", "dsk_hem_o_fake_l": "dsk_thighmain_l", "dsk_hem_i_l": "dsk_thigh_l",
           "dsk_hem_ba_r": "sk_thigh_r", "dsk_hem_o_fake_r": "dsk_thighmain_r", "dsk_hem_i_r": "dsk_thigh_r"}

def f2h(a):
    return np.asarray(a, np.float32).astype(np.float16).tobytes()

def write_pelvis(src, dst, slots, faces, thigh_fmdl=None, log=print):
    """slots: dict pos (n, 3), nrm (n, 3), tan (n, 4: xyz + signo), uv (n, 2), bone (n, 4: nombres), w (n, 4: 0..1).
    faces: (m, 3) indices de slots (ya con el giro de TorsoBody). Devuelve el resumen."""
    import sys, os
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    from fmdl import Fmdl
    F = Fmdl(src); d = bytearray(F.d)
    # ---- malla 0
    mc, mo = F.b0[3]
    e0 = mo
    alpha, shadow = d[e0], d[e0 + 1]
    matinst, bgroup, fmt_idx, nvert = struct.unpack_from("<HHHH", d, e0 + 4)
    first, fcount = struct.unpack_from("<II", d, e0 + 0x10)
    e1 = mo + 0x30
    mat_torso = struct.unpack_from("<H", d, e1 + 4)[0]
    n = len(slots["pos"]); m = len(faces)
    assert n <= nvert, "la pelvis tiene %d vertices, entran %d" % (n, nvert)
    assert 3 * m <= fcount, "la pelvis tiene %d triangulos, entran %d" % (m, fcount // 3)
    struct.pack_into("<H", d, e0 + 4, mat_torso)
    d[e0] = d[e1]; d[e0 + 1] = d[e1 + 1]          # alfa / sombra como el torso
    # ---- formato de vertices de la malla 0
    mfa_c, mfa_o = F.b0[9]; mf_c, mf_o = F.b0[10]; vf_c, vf_o = F.b0[11]; bo_c, bo_o = F.b0[14]
    buf_base, _ = F.b1[2]
    bufs = [struct.unpack_from("<IIII", d, bo_o + 16 * i) for i in range(bo_c)]
    nmf, nvf, _, _, fmf, fvf = struct.unpack_from("<BBBBHH", d, mfa_o + 8 * fmt_idx)
    vfs = [struct.unpack_from("<BBH", d, vf_o + 4 * (fvf + i)) for i in range(nvf)]
    # ---- grupo de huesos de la malla 0 y nombres (con los renombres)
    names = list(F.strings)
    bnames = [b["name"] for b in F.bones]
    newnames = [RENAMES.get(b, b) for b in bnames]
    grp = F.groups[bgroup]
    gnames = [newnames[j] for j in grp]
    gidx = {nm: k for k, nm in enumerate(gnames)}
    # pesos -> indices locales del grupo (u8) y pesos (u8, suma 255)
    BI = np.zeros((nvert, 4), np.uint8); BW = np.zeros((nvert, 4), np.uint8)
    for i in range(n):
        lst = [(slots["bone"][i][k], slots["w"][i][k]) for k in range(4) if slots["w"][i][k] > 1e-4]
        tot = sum(w for _, w in lst)
        ws = [int(round(255 * w / tot)) for _, w in lst]
        if ws: ws[int(np.argmax(ws))] += 255 - sum(ws)
        for k, ((bn, _), wq) in enumerate(zip(lst, ws)):
            if bn not in gidx: raise KeyError("el hueso %s no esta en el grupo de la malla 0" % bn)
            BI[i, k] = gidx[bn]; BW[i, k] = wq
    BI[n:] = BI[0]; BW[n:] = BW[0]
    pos = np.zeros((nvert, 3), np.float32); pos[:n] = slots["pos"]; pos[n:] = slots["pos"][0]
    nrm = np.zeros((nvert, 4), np.float32); nrm[:n, :3] = slots["nrm"]; nrm[n:] = nrm[0]
    tan = np.zeros((nvert, 4), np.float32); tan[:n] = slots["tan"]; tan[n:] = tan[0]
    uv = np.zeros((nvert, 2), np.float32); uv[:n] = slots["uv"]; uv[n:] = uv[0]
    vi = 0
    for k in range(nmf):
        bidx, nf, stride, typ, moff = struct.unpack_from("<BBBBI", d, mf_o + 8 * (fmf + k))
        base = buf_base + bufs[bidx][2] + moff
        for usage, fmt, voff in vfs[vi: vi + nf]:
            for v in range(nvert):
                o = base + stride * v + voff
                if usage == 0 and fmt == 1: d[o:o + 12] = pos[v].tobytes()
                elif usage == 2 and fmt == 6: d[o:o + 8] = f2h(nrm[v])
                elif usage == 14 and fmt == 6: d[o:o + 8] = f2h(tan[v])
                elif usage == 1 and fmt == 8: d[o:o + 4] = BW[v].tobytes()
                elif usage == 7 and fmt == 9: d[o:o + 4] = BI[v].tobytes()
                elif usage in (8, 9) and fmt == 7: d[o:o + 4] = f2h(uv[v])
                else: raise ValueError("elemento de vertice no previsto: uso %d formato %d" % (usage, fmt))
        vi += nf
    # ---- indices
    ib = [b for b in bufs if b[0] == 1][0]
    ib_off = buf_base + ib[2] + 2 * first
    idx = np.zeros(fcount, np.uint16)
    idx[:3 * m] = np.asarray(faces, np.uint16).ravel()
    d[ib_off: ib_off + 2 * fcount] = idx.tobytes()
    # ---- renombres (cadenas y entradas de hueso)
    c12, o12 = F.b0[12]; sbase, slen = F.b1[3]
    tb = {}
    if thigh_fmdl:
        Ft = Fmdl(thigh_fmdl)
        for i, b in enumerate(Ft.bones):
            raw = Ft.d[Ft.b0[0][1] + 0x30 * i: Ft.b0[0][1] + 0x30 * (i + 1)]
            par = struct.unpack_from("<h", raw, 2)[0]
            tb[b["name"]] = (Ft.bones[par]["name"] if par >= 0 else None, raw[16:48])
    b0c, b0o = F.b0[0]
    done = []
    for bi, old in enumerate(bnames):
        new = RENAMES.get(old)
        if not new: continue
        raw = d[b0o + 0x30 * bi: b0o + 0x30 * (bi + 1)]
        sid = struct.unpack_from("<H", raw, 0)[0]
        blk, ln, so = struct.unpack_from("<HHI", d, o12 + 8 * sid)
        assert names[sid] == old and len(new) <= ln
        d[sbase + so: sbase + so + ln] = new.encode("ascii") + b"\0" * (ln - len(new))
        struct.pack_into("<HHI", d, o12 + 8 * sid, blk, len(new), so)
        if new in tb:
            pname, posraw = tb[new]
            par = bnames.index(pname) if pname in bnames else -1
            struct.pack_into("<h", d, b0o + 0x30 * bi + 2, par)
            d[b0o + 0x30 * bi + 16: b0o + 0x30 * bi + 48] = posraw
        done.append("%s -> %s" % (old, new))
    # ---- cajas de los huesos renombrados (vertices de la pelvis con peso en ellos) y la de la malla / modelo
    c13, o13 = F.b0[13]
    for bi, old in enumerate(bnames):
        new = RENAMES.get(old)
        if not new: continue
        bbox_i = struct.unpack_from("<H", d, b0o + 0x30 * bi + 4)[0]
        sel = [i for i in range(n) if any(slots["bone"][i][k] == new and slots["w"][i][k] > 0 for k in range(4))]
        if not sel: continue
        P = slots["pos"][sel]
        mx, mn = P.max(0), P.min(0)
        struct.pack_into("<8f", d, o13 + 32 * bbox_i, mx[0], mx[1], mx[2], 1.0, mn[0], mn[1], mn[2], 1.0)
    open(dst, "wb").write(bytes(d))
    return dict(alpha=(alpha, d[e0]), mat=(matinst, mat_torso), verts=(n, nvert), tris=(m, fcount // 3), renames=done)
