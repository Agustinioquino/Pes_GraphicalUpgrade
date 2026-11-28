# PES21 FX (16/11) - PESOS del torso de TorsoBody: alisado SUAVE de los saltos de peso (Agustin: "corregi la forma asi como los
# weight", "de manera suave"). Solo los vertices donde el peso difiere mucho del promedio de sus vecinos (rugosidad > UMBRAL) y
# su primer anillo; quedan EXACTOS los bordes (anillos del brazo, del cuello y del short: los comparte con arm / neck) y todo lo
# que esta debajo de Y_MIN (cintura: union con el pantalon). Hasta 4 huesos por vertice, de los del grupo de su malla, suma 255.
# Uso: python3 torso_pesos.py <carpeta con pants_*_sub.fmdl> <salida>
import sys, os, struct, shutil
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..')]
from fmdl import Fmdl, meshes
from torso_mesh import load_parts

UMBRAL, MEZCLA, PASADAS, Y_MIN = 0.045, 0.5, 3, 1.20
TORSO = ("pants_out_sub", "pants_in_sub", "pants_out_inner_sub", "pants_out_sub_tight")

def rugosidad(W, E):
    acc = np.zeros_like(W); cnt = np.zeros(len(W))
    for a, b in ((0, 1), (1, 0)): np.add.at(acc, E[:, a], W[E[:, b]]); np.add.at(cnt, E[:, a], 1)
    prom = acc / np.maximum(cnt, 1)[:, None]
    return np.abs(W - prom).sum(1) / 2, prom

def write_mesh_weights(src, dst, k, w8, i8):
    F = Fmdl(src); d = bytearray(F.d)
    mc, mo = F.b0[3]; e0 = mo + 0x30 * k
    matinst, bgroup, fmt_idx, nvert = struct.unpack_from("<HHHH", d, e0 + 4)
    assert len(w8) == nvert == len(i8)
    mfa_c, mfa_o = F.b0[9]; mf_c, mf_o = F.b0[10]; vf_c, vf_o = F.b0[11]; bo_c, bo_o = F.b0[14]
    buf_base, _ = F.b1[2]
    bufs = [struct.unpack_from("<IIII", d, bo_o + 16 * i) for i in range(bo_c)]
    nmf, nvf, _, _, fmf, fvf = struct.unpack_from("<BBBBHH", d, mfa_o + 8 * fmt_idx)
    vfs = [struct.unpack_from("<BBH", d, vf_o + 4 * (fvf + i)) for i in range(nvf)]
    vi = 0; done = set()
    for kk in range(nmf):
        bidx, nf, stride, typ, moff = struct.unpack_from("<BBBBI", d, mf_o + 8 * (fmf + kk))
        base = buf_base + bufs[bidx][2] + moff
        for usage, fmt, voff in vfs[vi: vi + nf]:
            if usage == 1 and fmt == 8:
                for v in range(nvert): o = base + stride * v + voff; d[o:o + 4] = bytes(w8[v])
                done.add("w")
            elif usage == 7 and fmt == 9:
                for v in range(nvert): o = base + stride * v + voff; d[o:o + 4] = bytes(i8[v])
                done.add("i")
        vi += nf
    assert done == {"w", "i"}, done
    open(dst, "wb").write(bytes(d))

def una(src_dir, fn, dst):
    f = os.path.join(src_dir, fn + ".fmdl"); F = Fmdl(f); ms = meshes(F); nb = len(F.bones)
    T = load_parts(src_dir, [(fn, i) for i in (1, 2, 3)])
    Pw, E, inv = T["Pw"], T["edges"], T["inv"]
    n = len(Pw); W = np.zeros((n, nb)); ok = np.ones((n, nb), bool)
    for p in T["parts"]:
        m = ms[p["index"]]; wd = inv[p["off"]:p["off"] + p["nvert"]]
        ww = np.zeros((p["nvert"], nb))
        for c in range(4): np.add.at(ww, (np.arange(p["nvert"]), m["idx"][:, c]), m["w"][:, c])
        W[wd] = ww
        g = np.zeros(nb, bool); g[F.groups[m["group"]]] = True
        ok[wd] &= g[None, :]
    W0 = W.copy()
    rug, _ = rugosidad(W, E)
    fijo = np.zeros(n, bool); fijo[T["boundary"]] = True; fijo |= Pw[:, 1] < Y_MIN
    # tampoco el primer anillo junto a los bordes
    v = fijo.copy(); v[E[fijo[E[:, 0]], 1]] = True; v[E[fijo[E[:, 1]], 0]] = True; fijo = v
    mask = rug > UMBRAL
    v = mask.copy(); v[E[mask[E[:, 0]], 1]] = True; v[E[mask[E[:, 1]], 0]] = True
    mask = v & ~fijo
    for _ in range(PASADAS):
        _, prom = rugosidad(W, E)
        W[mask] = (1 - MEZCLA) * W[mask] + MEZCLA * prom[mask]
    W[~ok] = 0
    # hasta 4 huesos, cuantizado a 255
    k = np.where(mask)[0]
    top = np.argsort(-W[k], 1)[:, :4]; wt = np.take_along_axis(W[k], top, 1); wt /= wt.sum(1, keepdims=True)
    q = np.floor(wt * 255 + 0.5).astype(int); q[:, 0] += 255 - q.sum(1)
    assert (q >= 0).all() and (q.sum(1) == 255).all()
    Wn = W0.copy(); Wn[k] = 0
    for c in range(4): Wn[k, top[:, c]] += q[:, c] / 255.0
    rug1, _ = rugosidad(Wn, E)
    cambio = np.abs(Wn - W0).sum(1) / 2
    shutil.copyfile(f, dst)
    for p in T["parts"]:
        m = ms[p["index"]]; grp = F.groups[m["group"]]; loc = {g: i for i, g in enumerate(grp)}
        wd = inv[p["off"]:p["off"] + p["nvert"]]
        # datos crudos actuales (se reescriben solo los vertices cambiados)
        raw_w = np.round(m["w"] * 255).astype(np.uint8); raw_i = np.array([[loc[g] for g in r] for r in m["idx"]], np.uint8)
        pos = {int(v): j for j, v in enumerate(k)}
        for a in range(p["nvert"]):
            j = pos.get(int(wd[a]))
            if j is None: continue
            raw_w[a] = q[j]; raw_i[a] = [loc[int(top[j][c] if q[j][c] else top[j][0])] for c in range(4)]
        write_mesh_weights(dst, dst, p["index"], raw_w, raw_i)
    chk = meshes(Fmdl(dst))
    for kk in range(len(ms)):
        for key in ("pos", "nrm", "tan", "uv", "faces"): assert np.array_equal(chk[kk][key], ms[kk][key]), (kk, key)
        assert np.allclose(chk[kk]["w"].sum(1), 1, atol=1e-6)
    sin = [kk for kk in (0,) if not (np.array_equal(chk[kk]["w"], ms[kk]["w"]) and np.array_equal(chk[kk]["idx"], ms[kk]["idx"]))]
    assert not sin
    print("%s: %d vertices con pesos alisados (de %d); cambio medio %.3f, maximo %.3f; rugosidad maxima %.3f -> %.3f, p99 %.3f -> %.3f"
          % (fn, len(k), n, cambio[k].mean(), cambio.max(), rug[~fijo].max(), rug1[~fijo].max(), np.percentile(rug, 99), np.percentile(rug1, 99)))

if __name__ == "__main__":
    src, out = sys.argv[1:3]; os.makedirs(out, exist_ok=True)
    for fn in TORSO:
        if os.path.exists(os.path.join(src, fn + ".fmdl")): una(src, fn, os.path.join(out, fn + ".fmdl"))
