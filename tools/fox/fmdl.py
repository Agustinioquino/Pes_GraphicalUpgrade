# Lector minimo de FMDL 2.03 (Fox Engine, PES 2021): huesos, grupos de huesos, mallas y pesos por vertice.
# Uso: python3 fmdl.py archivo.fmdl  (lista huesos y que huesos usa cada malla, con cuantos vertices pesa cada uno)
import struct, sys, collections

class Fmdl:
    def __init__(self, path):
        d = self.d = open(path, "rb").read()
        assert d[:4] == b"FMDL", "no es FMDL"
        (_, self.ver, info, self.f0, self.f1, n0, n1, self.s0, self.s0len, self.s1, self.s1len) = struct.unpack_from("<4sfQQQIIIIII", d, 0)
        self.b0 = {}
        o = info
        for i in range(n0):
            t, cnt, off = struct.unpack_from("<HHI", d, o); o += 8
            self.b0[t] = (cnt, self.s0 + off)
        self.b1 = {}
        for i in range(n1):
            t, off, ln = struct.unpack_from("<III", d, o); o += 12
            self.b1[t] = (self.s1 + off, ln)
        self.strings = self._strings()
        self.bones = self._bones()
        self.groups = self._bone_groups()

    def _strings(self):
        out = []
        if 12 not in self.b0 or 3 not in self.b1: return out
        cnt, off = self.b0[12]; base, ln = self.b1[3]
        for i in range(cnt):
            blk, length, so = struct.unpack_from("<HHI", self.d, off + 8 * i)
            out.append(self.d[base + so: base + so + length].decode("ascii", "replace"))
        return out

    def _bones(self):
        out = []
        if 0 not in self.b0: return out
        cnt, off = self.b0[0]
        for i in range(cnt):
            name, parent, bbox, unk = struct.unpack_from("<HhHH", self.d, off + 0x30 * i)
            lp = struct.unpack_from("<4f", self.d, off + 0x30 * i + 0x10)
            wp = struct.unpack_from("<4f", self.d, off + 0x30 * i + 0x20)
            out.append({"name": self.strings[name] if name < len(self.strings) else "#%d" % name, "parent": parent, "local": lp, "world": wp})
        return out

    def _bone_groups(self):
        out = []
        if 5 not in self.b0: return out
        cnt, off = self.b0[5]
        for i in range(cnt):
            unk, n = struct.unpack_from("<HH", self.d, off + 0x44 * i)
            ids = struct.unpack_from("<%dH" % n, self.d, off + 0x44 * i + 4)
            out.append(list(ids))
        return out

if __name__ == "__main__":
    f = Fmdl(sys.argv[1])
    print("version %.2f, bloques s0 %s, s1 %s" % (f.ver, sorted(f.b0), sorted(f.b1)))
    print("%d huesos:" % len(f.bones), " ".join(b["name"] for b in f.bones))
    for i, g in enumerate(f.groups):
        print("grupo %d (%d):" % (i, len(g)), " ".join(f.bones[j]["name"] if j < len(f.bones) else "?%d" % j for j in g))

# ---------------------------------------------------------------- vertices (posicion + pesos)
import numpy as np
def _half(b): return np.frombuffer(b, dtype=np.float16).astype(np.float32)

def meshes(f):
    """lista de mallas: dict(pos Nx3, idx Nx4 (id de hueso del modelo), w Nx4, grupo, nvert)"""
    d = f.d
    out = []
    if 3 not in f.b0: return out
    cnt, off = f.b0[3]
    mfa_cnt, mfa_off = f.b0[9]; mf_cnt, mf_off = f.b0[10]; vf_cnt, vf_off = f.b0[11]; bo_cnt, bo_off = f.b0[14]
    buf_base, _ = f.b1[2]
    bufs = [struct.unpack_from("<IIII", d, bo_off + 16 * i) for i in range(bo_cnt)]
    for m in range(cnt):
        e = d[off + 0x30 * m: off + 0x30 * (m + 1)]
        matinst, bgroup, index, nvert = struct.unpack_from("<HHHH", e, 4)
        nmf, nvf, _, _, fmf, fvf = struct.unpack_from("<BBBBHH", d, mfa_off + 8 * index)
        vfs = [struct.unpack_from("<BBH", d, vf_off + 4 * (fvf + i)) for i in range(nvf)]
        pos = w = ix = None
        vi = 0
        for k in range(nmf):
            bidx, nf, stride, typ, moff = struct.unpack_from("<BBBBI", d, mf_off + 8 * (fmf + k))
            base = buf_base + bufs[bidx][2] + moff
            raw = d[base: base + stride * nvert]
            for usage, fmt, voff in vfs[vi: vi + nf]:
                a = np.frombuffer(raw, dtype=np.uint8).reshape(nvert, stride)[:, voff:]
                if usage == 0 and fmt == 1: pos = np.ascontiguousarray(a[:, :12]).view(np.float32).reshape(nvert, 3)
                elif usage == 1 and fmt == 8: w = a[:, :4].astype(np.float32) / 255.0
                elif usage == 7 and fmt == 9: ix = a[:, :4].astype(np.int32)
            vi += nf
        grp = f.groups[bgroup] if bgroup < len(f.groups) else []
        if ix is not None and grp:
            ids = np.array(grp + [0] * (256 - len(grp)), dtype=np.int32)
            ix = ids[np.clip(ix, 0, 255)]
        # caras: indices u16 locales de la malla en el buffer de indices (tipo 1)
        ff, fc = struct.unpack_from("<II", e, 0x10)
        faces = None
        ib = [b for b in bufs if b[0] == 1]
        if ib and fc:
            faces = np.frombuffer(d, dtype="<u2", count=fc, offset=buf_base + ib[0][2] + 2 * ff).astype(np.int32).reshape(-1, 3)
        out.append({"pos": pos, "w": w, "idx": ix, "group": bgroup, "nvert": nvert, "mat": matinst, "faces": faces})
    return out

def bone_weights(f, m, name):
    """peso del hueso 'name' en cada vertice de la malla m"""
    ids = [i for i, b in enumerate(f.bones) if b["name"] == name]
    if not ids or m["w"] is None: return np.zeros(m["nvert"], np.float32)
    bid = ids[0]
    return ((m["idx"] == bid) * m["w"]).sum(axis=1)
