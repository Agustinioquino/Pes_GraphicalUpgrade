# PES21 FX (15/11) - editor de FMDL 2.03 por bloques: lee todos los bloques de la seccion 0 y de la seccion 1, deja cambiarlos
# (agregar cadenas, texturas a un material) y vuelve a escribir el archivo con los offsets corridos. Sin cambios, el archivo sale
# IGUAL byte a byte (se comprueba al cargar). Los bloques se referencian entre si por indice, no por offset: agregar al final es seguro.
import struct

TAM = {0: 0x30, 1: 8, 2: 0x20, 3: 0x30, 4: 0x10, 5: 0x44, 6: 4, 7: 4, 8: 8, 9: 8, 10: 8, 11: 4, 12: 8, 13: 0x20, 14: 0x10,
       16: 0x10, 17: 8, 18: 0x10, 20: 0x80}
A16 = lambda n: (n + 15) & ~15

class FmdlEdit:
    def __init__(self, path):
        d = self.orig = open(path, "rb").read()
        assert d[:4] == b"FMDL"
        self.cab = bytearray(d[:64])
        info, = struct.unpack_from("<Q", d, 8)
        n0, n1, s0, s0len, s1, s1len = struct.unpack_from("<IIIIII", d, 32)
        assert info == 64
        self.orden0, self.b0, self.orden1, self.b1 = [], {}, [], {}
        o = info
        for i in range(n0):
            t, cnt, off = struct.unpack_from("<HHI", d, o); o += 8
            assert t in TAM, "bloque %d sin tamano conocido" % t
            self.orden0.append(t); self.b0[t] = bytearray(d[s0 + off: s0 + off + cnt * TAM[t]])
        for i in range(n1):
            t, off, ln = struct.unpack_from("<III", d, o); o += 12
            self.orden1.append(t); self.b1[t] = bytearray(d[s1 + off: s1 + off + ln])
        assert self.bytes() == d, "el archivo no se rearma igual: formato no previsto"

    def cnt(self, t): return len(self.b0[t]) // TAM[t]

    def bytes(self):
        tabla = bytearray(); sec0 = bytearray()
        for t in self.orden0:
            sec0 += b"\0" * (A16(len(sec0)) - len(sec0))
            tabla += struct.pack("<HHI", t, self.cnt(t), len(sec0)); sec0 += self.b0[t]
        sec0 += b"\0" * (A16(len(sec0)) - len(sec0))
        sec1 = bytearray()
        for t in self.orden1:
            tabla += struct.pack("<III", t, len(sec1), len(self.b1[t])); sec1 += self.b1[t]
        s0 = A16(64 + len(tabla))
        cab = bytearray(self.cab)
        struct.pack_into("<IIIIII", cab, 32, len(self.orden0), len(self.orden1), s0, len(sec0), s0 + len(sec0), len(sec1))
        return bytes(cab) + bytes(tabla) + b"\0" * (s0 - 64 - len(tabla)) + bytes(sec0) + bytes(sec1)

    def save(self, path): open(path, "wb").write(self.bytes())

    # ---- cadenas (bloque 12 de la seccion 0 -> bloque 3 de la seccion 1, terminadas en 0)
    def cadenas(self):
        out = []
        for i in range(self.cnt(12)):
            blk, ln, so = struct.unpack_from("<HHI", self.b0[12], 8 * i)
            out.append(bytes(self.b1[3][so: so + ln]).decode("ascii"))
        return out

    def cadena(self, s):
        """indice de la cadena s; si no esta, la agrega"""
        cs = self.cadenas()
        if s in cs: return cs.index(s)
        so = len(self.b1[3])
        self.b1[3] += s.encode("ascii") + b"\0"
        self.b0[12] += struct.pack("<HHI", 3, len(s), so)
        return len(cs)

    # ---- materiales: bloque 4 (instancia: nombre, -, material, nTex, nPar, primera tex, primer par), 7 (nombre, ref), 6 (archivo, carpeta)
    def instancias(self):
        cs = self.cadenas()
        return [cs[struct.unpack_from("<H", self.b0[4], 16 * i)[0]] for i in range(self.cnt(4))]

    def texturas(self, inst):
        cs = self.cadenas()
        nom, _, mat, ntex, npar, ftex, fpar = struct.unpack_from("<HHHBBHH", self.b0[4], 16 * inst)
        out = []
        for k in range(ntex):
            pn, ref = struct.unpack_from("<HH", self.b0[7], 4 * (ftex + k))
            fn, carp = struct.unpack_from("<HH", self.b0[6], 4 * ref)
            out.append((cs[pn], cs[carp] + cs[fn]))
        return out

    def poner_textura(self, nombre_inst, parametro, archivo, carpeta):
        """agrega (o cambia) la textura 'parametro' de la instancia de material 'nombre_inst'"""
        inst = self.instancias().index(nombre_inst)
        nom, pad, mat, ntex, npar, ftex, fpar = struct.unpack_from("<HHHBBHH", self.b0[4], 16 * inst)
        pn, fn, carp = self.cadena(parametro), self.cadena(archivo), self.cadena(carpeta)
        ref = self.cnt(6)
        self.b0[6] += struct.pack("<HH", fn, carp)
        viejas = [struct.unpack_from("<HH", self.b0[7], 4 * (ftex + k)) for k in range(ntex)]
        nuevas = [(pn, ref)] + [v for v in viejas if v[0] != pn]
        primera = self.cnt(7)
        for v in nuevas: self.b0[7] += struct.pack("<HH", *v)
        struct.pack_into("<HHHBBHH", self.b0[4], 16 * inst, nom, pad, mat, len(nuevas), npar, primera, fpar)
