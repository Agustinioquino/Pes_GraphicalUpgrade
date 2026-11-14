#!/usr/bin/env python3
"""PES21 FX (09/11) - leer / rearmar un .fox2 binario de Fox Engine (PES 2021) para AGREGAR entidades y cambiar propiedades.

Formato: /mnt/project-files/decompilado/detalle/fox2_formato.md (verificado byte a byte con tools/fox2_dump.py).
Este modulo trabaja a nivel binario: cada propiedad se guarda con sus bytes de datos tal cual, asi que rearmar un archivo
sin cambios da exactamente los mismos bytes (comprobarlo con: python fox2_edit.py archivo.fox2 --roundtrip).

Lo que sabe hacer:
  - parse(bytes) -> Fox2 ; Fox2.build() -> bytes
  - buscar entidades por nombre / clase, leer y cambiar propiedades (Float, Bool, UInt32, Int32, Vector3, Color, Path...)
  - copiar una entidad de OTRO .fox2 (p.ej. GrShadowSettings o TppAtmosphere del estadio) con direcciones nuevas
  - crear entidades nuevas (DirectionalLight + su TransformEntity) y sumarlas al dataList del DataSet
  - mantener la tabla de cadenas (agrega los textos nuevos, ordenada como la de fabrica)
Solo biblioteca estandar.
"""
import struct, sys, os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# ------------------------------------------------------------------ StrCode (CityHash64 1.1 con semillas, 48 bits)
M = 0xFFFFFFFFFFFFFFFF
k0, k1, k2, k3 = 0xc3a5c85c97cb3127, 0xb492b66fbe98f273, 0x9ae16a3b2f90404f, 0xc949d7c7509e6557
def _rot(v, n): return v if n == 0 else ((v >> n) | (v << (64 - n))) & M
def _f64(b, i): return int.from_bytes(b[i:i + 8], 'little')
def _f32(b, i): return int.from_bytes(b[i:i + 4], 'little')
def _smix(v): return v ^ (v >> 47)
def _h16(u, v):
    km = 0x9ddfea08eb382d69
    a = ((u ^ v) * km) & M; a ^= a >> 47
    b = ((v ^ a) * km) & M; b ^= b >> 47
    return (b * km) & M
def _weak(b, i, a, bb):
    w, x, y, z = _f64(b, i), _f64(b, i + 8), _f64(b, i + 16), _f64(b, i + 24)
    a = (a + w) & M; bb = _rot((bb + a + z) & M, 21); c = a
    a = (a + x + y) & M; bb = (bb + _rot(a, 44)) & M
    return ((a + z) & M, (bb + c) & M)
def _city64(b):
    n = len(b)
    if n <= 16:
        if n > 8:
            a = _f64(b, 0); bb = _f64(b, n - 8)
            return (_h16(a, _rot((bb + n) & M, n)) ^ bb) & M
        if n >= 4:
            a = _f32(b, 0)
            return _h16((n + (a << 3)) & M, _f32(b, n - 4))
        if n > 0:
            a, bb, c = b[0], b[n >> 1], b[n - 1]
            y = (a + (bb << 8)) & 0xFFFFFFFF; z = (n + (c << 2)) & 0xFFFFFFFF
            return (_smix(((y * k2) ^ (z * k3)) & M) * k2) & M
        return k2
    if n <= 32:
        a = (_f64(b, 0) * k1) & M; bb = _f64(b, 8); c = (_f64(b, n - 8) * k2) & M; d = (_f64(b, n - 16) * k0) & M
        return _h16((_rot((a - bb) & M, 43) + _rot(c, 30) + d) & M, (a + _rot(bb ^ k3, 20) - c + n) & M)
    if n <= 64:
        z = _f64(b, 24); a = (_f64(b, 0) + (n + _f64(b, n - 16)) * k0) & M
        bb = _rot((a + z) & M, 52); c = _rot(a, 37)
        a = (a + _f64(b, 8)) & M; c = (c + _rot(a, 7)) & M; a = (a + _f64(b, 16)) & M
        vf = (a + z) & M; vs = (bb + _rot(a, 31) + c) & M
        a = (_f64(b, 16) + _f64(b, n - 32)) & M; z = _f64(b, n - 8)
        bb = _rot((a + z) & M, 52); c = _rot(a, 37)
        a = (a + _f64(b, n - 24)) & M; c = (c + _rot(a, 7)) & M; a = (a + _f64(b, n - 16)) & M
        wf = (a + z) & M; ws = (bb + _rot(a, 31) + c) & M
        r = _smix(((vf + ws) * k2 + (wf + vs) * k0) & M)
        return (_smix((r * k0 + vs) & M) * k2) & M
    x = _f64(b, n - 40); y = (_f64(b, n - 16) + _f64(b, n - 56)) & M
    z = _h16((_f64(b, n - 48) + n) & M, _f64(b, n - 24))
    v = _weak(b, n - 64, n, z); w = _weak(b, n - 32, (y + k1) & M, x)
    x = (x * k1 + _f64(b, 0)) & M
    ln = (n - 1) & ~63; i = 0
    while True:
        x = (_rot((x + y + v[0] + _f64(b, i + 8)) & M, 37) * k1) & M
        y = (_rot((y + v[1] + _f64(b, i + 48)) & M, 42) * k1) & M
        x ^= w[1]; y = (y + v[0] + _f64(b, i + 40)) & M
        z = (_rot((z + w[0]) & M, 33) * k1) & M
        v = _weak(b, i, (v[1] * k1) & M, (x + w[0]) & M)
        w = _weak(b, i + 32, (z + w[1]) & M, (y + _f64(b, i + 16)) & M)
        z, x = x, z
        i += 64; ln -= 64
        if ln == 0: break
    return _h16((_h16(v[0], w[0]) + _smix(y) * k1 + z) & M, (_h16(v[1], w[1]) + x) & M)

def strcode(s):
    b = s.encode('utf-8') + b'\0'
    seed1 = ((b[0] << 16) + len(s)) & M if s else len(s)
    return _h16((_city64(b) - k2) & M, seed1) & 0xFFFFFFFFFFFF

HASH_VACIO = 0xB8A0BF169F98

# ------------------------------------------------------------------ tipos
T = dict(Int8=0, UInt8=1, Int16=2, UInt16=3, Int32=4, UInt32=5, Int64=6, UInt64=7, Float=8, Double=9, Bool=10, String=11,
         Path=12, EntityPtr=13, Vector3=14, Vector4=15, Quat=16, Matrix3=17, Matrix4=18, Color=19, FilePtr=20,
         EntityHandle=21, EntityLink=22)
TN = {v: k for k, v in T.items()}
SIZE = {0: 1, 1: 1, 2: 2, 3: 2, 4: 4, 5: 4, 6: 8, 7: 8, 8: 4, 9: 8, 10: 1, 11: 8, 12: 8, 13: 8, 14: 16, 15: 16, 16: 16,
        17: 48, 18: 64, 19: 16, 20: 8, 21: 8, 22: 32}
FMT = {0: 'b', 1: 'B', 2: 'h', 3: 'H', 4: 'i', 5: 'I', 6: 'q', 7: 'Q', 8: 'f', 9: 'd', 10: 'B', 11: 'Q', 12: 'Q', 13: 'Q',
       14: '4f', 15: '4f', 16: '4f', 19: '4f', 20: 'Q', 21: 'Q'}
STATIC, DYNARRAY, STRINGMAP, LIST = 0, 1, 2, 3
HASH_TYPES = (11, 12, 20)            # String, Path, FilePtr: el dato es un StrCode -> tabla de cadenas
ADDR_TYPES = (13, 21)                # EntityPtr, EntityHandle: el dato es el address de otra entidad


def _pad16(n): return (n + 15) & ~15


class Prop:
    __slots__ = ('h', 't', 'c', 'n', 'raw', 'tail')

    def __init__(self, h, t, c, n, raw, tail=b''):
        self.h, self.t, self.c, self.n, self.raw, self.tail = h, t, c, n, raw, tail

    def elem_size(self):
        return SIZE[self.t] + (8 if self.c == STRINGMAP else 0)

    def elems(self):
        """lista de (clave StrCode o None, bytes del elemento)"""
        es, out = self.elem_size(), []
        for i in range(self.n):
            b = self.raw[i * es:(i + 1) * es]
            if self.c == STRINGMAP:
                out.append((struct.unpack_from('<Q', b)[0], b[8:]))
            else:
                out.append((None, b))
        return out

    def set_elems(self, elems):
        parts = []
        for k, b in elems:
            assert len(b) == SIZE[self.t]
            parts.append((struct.pack('<Q', k) if self.c == STRINGMAP else b'') + b)
        self.raw = b''.join(parts); self.n = len(elems); self.tail = b''

    def value(self, i=0):
        b = self.elems()[i][1]
        if self.t == 22:
            pk, ar, nm, ad = struct.unpack_from('<QQQI', b)
            return (pk, ar, nm, ad)
        v = struct.unpack('<' + FMT[self.t], b)
        return v if len(v) > 1 else v[0]

    def build(self):
        data = self.raw + self.tail
        size = 0x20 + _pad16(len(data))
        return (struct.pack('<QBBHHH', self.h, self.t, self.c, self.n, 0x20, size) + b'\0' * 16 + data +
                b'\0' * (size - 0x20 - len(data)))


def pack_value(t, v):
    if t == 22:
        pk, ar, nm, ad = v
        return struct.pack('<QQQII', pk, ar, nm, ad, 0)
    if t == 10:
        return struct.pack('<B', 1 if v else 0)
    if isinstance(v, (tuple, list)):
        v = tuple(v) + (0.0,) * (4 - len(v)) if FMT[t] == '4f' else v
        return struct.pack('<' + FMT[t], *v)
    return struct.pack('<' + FMT[t], v)


class Entity:
    def __init__(self, cls_hash, version, unk1, address, props, head_raw=None):
        self.cls, self.version, self.unk1, self.address, self.props = cls_hash, version, unk1, address, props
        self.head_raw = head_raw     # cabecera original de 0x40 (para rearmar identico); None = nueva

    def prop(self, name):
        h = strcode(name)
        for p in self.props:
            if p.h == h: return p
        return None

    def build(self):
        body = b''.join(p.build() for p in self.props)
        size = 0x40 + len(body)
        if self.head_raw is not None:
            hd = bytearray(self.head_raw)
        else:
            hd = bytearray(0x40)
            struct.pack_into('<H', hd, 0, 0x40); hd[6:10] = b'ent\0'
        struct.pack_into('<I', hd, 2, self.unk1)
        struct.pack_into('<I', hd, 0x0a, self.address)
        struct.pack_into('<H', hd, 0x1a, self.version)
        struct.pack_into('<Q', hd, 0x1c, self.cls)
        struct.pack_into('<HH', hd, 0x24, len(self.props), 0)
        struct.pack_into('<III', hd, 0x28, 0x40, size, size)
        return bytes(hd) + body


class Fox2:
    def __init__(self):
        self.ents, self.strings, self.head_raw, self.foot_raw = [], [], None, None

    # -------------------------------------------------------------- lectura
    @staticmethod
    def parse(d):
        f = Fox2()
        assert d[:4] == b'\xf2box', 'no es un fox2'
        ver, n, stoff, eoff = struct.unpack_from('<IIII', d, 4)
        assert ver == 0x35 and eoff == 0x20
        f.head_raw = bytes(d[:0x20])
        p = eoff
        for _ in range(n):
            hsz, unk1 = struct.unpack_from('<HI', d, p)
            assert hsz == 0x40 and d[p + 6:p + 10] == b'ent\0'
            addr, = struct.unpack_from('<I', d, p + 0x0a)
            cver, = struct.unpack_from('<H', d, p + 0x1a)
            cls, ns, nd, os_, od, esz = struct.unpack_from('<QHHIII', d, p + 0x1c)
            assert nd == 0 and os_ == 0x40
            props, q = [], p + 0x40
            for _ in range(ns):
                h, t, c, cnt, doff, psz = struct.unpack_from('<QBBHHH', d, q)
                es = SIZE[t] + (8 if c == STRINGMAP else 0)
                used = cnt * es
                data = d[q + doff:q + psz]
                props.append(Prop(h, t, c, cnt, bytes(data[:used]), bytes(data[used:]).rstrip(b'\0')))
                q += psz
            assert q == p + esz, 'tamano de entidad'
            f.ents.append(Entity(cls, cver, unk1, addr, props, bytes(d[p:p + 0x40])))
            p += esz
        assert p == stoff
        while True:
            h, = struct.unpack_from('<Q', d, p)
            if h == 0: break
            ln, = struct.unpack_from('<I', d, p + 8)
            f.strings.append((h, bytes(d[p + 12:p + 12 + ln])))
            p += 12 + ln
        f.str_end = p
        f.tail_raw = bytes(d[p:])
        return f

    # -------------------------------------------------------------- escritura
    def build(self):
        body = b''.join(e.build() for e in self.ents)
        st = b''.join(struct.pack('<QI', h, len(s)) + s for h, s in self.strings)
        hd = bytearray(self.head_raw or (b'\xf2box' + b'\0' * 28))
        struct.pack_into('<IIII', hd, 4, 0x35, len(self.ents), 0x20 + len(body), 0x20)
        out = bytes(hd) + body + st + b'\0' * 12        # terminador: hash 0 + largo 0
        out += b'\0' * (_pad16(len(out)) - len(out))
        out += b'\0\0end' + b'\0' * 11
        return out

    # -------------------------------------------------------------- utilidades
    def string(self, h):
        for k, s in self.strings:
            if k == h: return s.decode('utf-8', 'replace')
        return None

    def add_string(self, s):
        h = strcode(s)
        if s == '' or any(k == h for k, _ in self.strings): return h
        self.strings.append((h, s.encode('utf-8')))
        self.strings.sort(key=lambda kv: kv[1])
        return h

    def by_address(self, a):
        for e in self.ents:
            if e.address == a: return e
        return None

    def name_of(self, e):
        p = e.prop('name')
        return self.string(p.value()) if p is not None else None

    def find(self, name=None, cls=None):
        out = []
        for e in self.ents:
            if cls is not None and e.cls != strcode(cls): continue
            if name is not None and self.name_of(e) != name: continue
            out.append(e)
        return out

    def dataset(self):
        return self.find(cls='DataSet')[0]

    def free_address(self):
        a = max(e.address for e in self.ents if e.address < 0x1000000)
        return (a & ~0xff) + 0x100

    def set(self, e, name, t, v, c=STATIC):
        """cambia (o agrega al final) una propiedad de un solo elemento"""
        tid = T[t] if isinstance(t, str) else t
        if tid in HASH_TYPES and isinstance(v, str):
            v = self.add_string(v) if v else HASH_VACIO
        p = e.prop(name)
        if p is None:
            self.add_string(name)
            p = Prop(strcode(name), tid, c, 0, b'')
            e.props.append(p)
        assert p.t == tid, '%s: tipo %s, no %s' % (name, TN.get(p.t), t)
        p.set_elems([(None, pack_value(tid, v))])
        return p

    def add_to_dataset(self, key, address):
        ds = self.dataset()
        p = ds.prop('dataList')
        el = p.elems()
        h = self.add_string(key)
        assert all(k != h for k, _ in el), 'el dataList ya tiene ' + key
        el.append((h, struct.pack('<Q', address)))
        p.set_elems(el)

    def link(self, name, address, arch):
        """EntityLink a una entidad de este mismo archivo (como los de fabrica: paquete vacio, archivo propio, nombre)"""
        return (HASH_VACIO, self.add_string(arch), self.add_string(name), address)

    def copy_from(self, src, ents, remap=None):
        """copia entidades de otro Fox2 con direcciones nuevas. remap: {address_viejo: address_nuevo} (se completa).
        Los EntityPtr/EntityHandle se traducen con remap (0x100 = DataSet se mantiene); los que apuntan a entidades no
        copiadas quedan en 0 (null). Copia los textos de la tabla que usen."""
        remap = {} if remap is None else remap
        remap.setdefault(src.dataset().address, self.dataset().address)
        for e in ents:
            if e.address not in remap:
                remap[e.address] = self.free_address() + 0x100 * (len(remap))
        out = []
        for e in ents:
            props = []
            for p in e.props:
                q = Prop(p.h, p.t, p.c, p.n, p.raw, p.tail)
                self._copy_str(src, p.h)
                el = q.elems()
                nel = []
                for k, b in el:
                    if k is not None: self._copy_str(src, k)
                    if q.t in ADDR_TYPES:
                        a, = struct.unpack('<Q', b)
                        b = struct.pack('<Q', remap.get(a, 0) if a else 0)
                    elif q.t in HASH_TYPES:
                        self._copy_str(src, struct.unpack('<Q', b)[0])
                    elif q.t == 22:
                        pk, ar, nm, ad, z = struct.unpack('<QQQII', b)
                        for hh in (pk, ar, nm): self._copy_str(src, hh)
                        b = struct.pack('<QQQII', pk, ar, nm, remap.get(ad, 0) if ad else 0, z)
                    nel.append((k, b))
                if q.n: q.set_elems(nel)
                props.append(q)
            self._copy_str(src, e.cls)
            ne = Entity(e.cls, e.version, e.unk1, remap[e.address], props, None)
            self.ents.append(ne)
            out.append(ne)
        return out

    def _copy_str(self, src, h):
        if h in (0, HASH_VACIO) or any(k == h for k, _ in self.strings): return
        s = src.string(h)
        if s is not None:
            self.strings.append((h, s.encode('utf-8')))
            self.strings.sort(key=lambda kv: kv[1])


def main(argv):
    if len(argv) < 2:
        print(__doc__); return 1
    d = open(argv[1], 'rb').read()
    f = Fox2.parse(d)
    if '--roundtrip' in argv:
        out = f.build()
        ok = out == d
        print('%s: %d entidades, %d cadenas, rearmado %s' % (argv[1], len(f.ents), len(f.strings),
                                                            'IDENTICO' if ok else 'DISTINTO'))
        if not ok:
            for i, (a, b) in enumerate(zip(out, d)):
                if a != b: print('primer byte distinto en 0x%x' % i); break
            print('largos', len(out), len(d))
        return 0 if ok else 2
    for e in f.ents:
        print('0x%x\t%s\t%s' % (e.address, f.string(e.cls), f.name_of(e)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
