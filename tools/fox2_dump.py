#!/usr/bin/env python3
# PES21 FX (08/11) - Decodificador de .fox2 (Fox Engine, PES 2021; formato "\xf2box" version '5' = el Fox2 binario que
# describe FoxTool de Atvaark). Solo biblioteca estandar. Formato verificado byte a byte sobre los 12 .fox2 de PES 2021
# (ver /home/claude/re/out/fox2_formato.md):
#
#   Cabecera (0x20):  +00 "\xf2box"  +04 u32 version (0x35)  +08 u32 nEntidades  +0C u32 offTablaCadenas
#                     +10 u32 offEntidades (0x20)  +14 12 bytes a cero
#   Entidad (0x40):   +00 u16 tamCabecera (0x40)  +02 u32 unk1 (constante por clase; tam. del objeto en memoria?)  +06 "ent\0"
#                     +0A u32 address (id dentro del archivo; lo referencian EntityPtr/EntityHandle/EntityLink)
#                     +0E u32 0  +12 u32 0  +16 u16 0  +18 u16 0 (version del formato de entidad?)
#                     +1A u16 classVersion (constante por clase: SpotLight 16, TimeOfDaySettings 3, ReflectionProbe 1...)
#                     +1C u64 StrCode nombre de clase  +24 u16 nPropsEstaticas  +26 u16 nPropsDinamicas
#                     +28 u32 offPropsEstaticas (0x40)  +2C u32 offPropsDinamicas  +30 u32 tamEntidad  +34 12 bytes a cero
#   Propiedad (0x20 + datos, redondeado a 16):  +00 u64 StrCode nombre  +08 u8 tipo  +09 u8 contenedor
#                     +0A u16 nElementos  +0C u16 offDatos (0x20)  +0E u16 tamPropiedad  +10 16 bytes a cero
#                     datos: nElementos elementos seguidos; en StringMap cada elemento va precedido de u64 StrCode clave
#   Tabla de cadenas: entradas (u64 StrCode, u32 largo, chars sin NUL), terminador u64 0 (+u32 0), relleno a 16,
#                     pie de 16 bytes: 00 00 'e' 'n' 'd' 00...
#
# Los hashes son StrCode de 48 bits (CityHash64 1.1 con semillas, cortado a 48 bits) guardados en 8 bytes; el StrCode32
# que usan otros formatos son sus 32 bits bajos. Los nombres se resuelven con la tabla de cadenas del propio archivo y con
# el diccionario TSV (/home/claude/re/strcode32_dict.tsv: hash32, nombre, hash48, fuente; lo arma fox2_dict.py).
# Lo que no se resuelve queda como "h_XXXXXXXXXXXX" (12 hex = StrCode48; los 8 ultimos digitos son el StrCode32).
#
# Uso: fox2_dump.py archivo.fox2 [salida.json] [--txt salida.txt] [--dict dic.tsv | --sin-dict] [--quiet]
#      fox2_dump.py --outdir DIR archivo1.fox2 archivo2.fox2 ...   (escribe DIR/<nombre>.json y .txt para cada uno)
import sys, os, struct, json, argparse

DICT_DEFAULT = os.environ.get('FOX2_DICT', '/home/claude/re/strcode32_dict.tsv')
HASH_VACIO = 0xB8A0BF169F98          # StrCode de "" (cadena vacia); no figura en la tabla de cadenas

# ---------------------------------------------------------------- StrCode (CityHash64 1.1 + semillas, 48 bits)
M = 0xFFFFFFFFFFFFFFFF
k0 = 0xc3a5c85c97cb3127; k1 = 0xb492b66fbe98f273; k2 = 0x9ae16a3b2f90404f; k3 = 0xc949d7c7509e6557


def _rot(v, n): return v if n == 0 else ((v >> n) | (v << (64 - n))) & M
def _rot1(v, n): return ((v >> n) | (v << (64 - n))) & M
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


def city64(b):
    n = len(b)
    if n <= 16:
        if n > 8:
            a = _f64(b, 0); c = _f64(b, n - 8)
            return _h16(a, _rot1((c + n) & M, n)) ^ c
        if n >= 4:
            a = _f32(b, 0)
            return _h16((n + (a << 3)) & M, _f32(b, n - 4))
        if n > 0:
            a = b[0]; c = b[n >> 1]; d = b[n - 1]
            y = (a + (c << 8)) & M; z = (n + (d << 2)) & M
            return (_smix(((y * k2) & M) ^ ((z * k3) & M)) * k2) & M
        return k2
    if n <= 32:
        a = (_f64(b, 0) * k1) & M; bb = _f64(b, 8); c = (_f64(b, n - 8) * k2) & M; d = (_f64(b, n - 16) * k0) & M
        return _h16((_rot((a - bb) & M, 43) + _rot(c, 30) + d) & M, (a + _rot((bb ^ k3) & M, 20) - c + n) & M)
    if n <= 64:
        z = _f64(b, 24); a = (_f64(b, 0) + ((n + _f64(b, n - 16)) & M) * k0) & M
        bb = _rot((a + z) & M, 52); c = _rot(a, 37)
        a = (a + _f64(b, 8)) & M; c = (c + _rot(a, 7)) & M; a = (a + _f64(b, 16)) & M
        vf = (a + z) & M; vs = (bb + _rot(a, 31) + c) & M
        a = (_f64(b, 16) + _f64(b, n - 32)) & M; z = _f64(b, n - 8)
        bb = _rot((a + z) & M, 52); c = _rot(a, 37)
        a = (a + _f64(b, n - 24)) & M; c = (c + _rot(a, 7)) & M; a = (a + _f64(b, n - 16)) & M
        wf = (a + z) & M; ws = (bb + _rot(a, 31) + c) & M
        r = _smix((((vf + ws) & M) * k2 + ((wf + vs) & M) * k0) & M)
        return (_smix((r * k0 + vs) & M) * k2) & M
    x = _f64(b, n - 40); y = (_f64(b, n - 16) + _f64(b, n - 56)) & M
    z = _h16((_f64(b, n - 48) + n) & M, _f64(b, n - 24))
    v = _weak(b, n - 64, n, z); w = _weak(b, n - 32, (y + k1) & M, x)
    x = (x * k1 + _f64(b, 0)) & M
    rest = (n - 1) & ~63; i = 0
    while True:
        x = (_rot((x + y + v[0] + _f64(b, i + 8)) & M, 37) * k1) & M
        y = (_rot((y + v[1] + _f64(b, i + 48)) & M, 42) * k1) & M
        x ^= w[1]; y = (y + v[0] + _f64(b, i + 40)) & M
        z = (_rot((z + w[0]) & M, 33) * k1) & M
        v = _weak(b, i, (v[1] * k1) & M, (x + w[0]) & M)
        w = _weak(b, i + 32, (z + w[1]) & M, (y + _f64(b, i + 16)) & M)
        z, x = x, z
        i += 64; rest -= 64
        if rest == 0: break
    return _h16((_h16(v[0], w[0]) + _smix(y) * k1 + z) & M, (_h16(v[1], w[1]) + x) & M)


def strcode(s):
    b = s.encode('utf-8') if isinstance(s, str) else bytes(s)
    seed1 = ((b[0] << 16) + len(b)) if b else 0
    return _h16((city64(b + b'\0') - k2) & M, seed1) & 0xFFFFFFFFFFFF


# ---------------------------------------------------------------- tablas del formato
TIPOS = {0: 'Int8', 1: 'UInt8', 2: 'Int16', 3: 'UInt16', 4: 'Int32', 5: 'UInt32', 6: 'Int64', 7: 'UInt64', 8: 'Float',
         9: 'Double', 10: 'Bool', 11: 'String', 12: 'Path', 13: 'EntityPtr', 14: 'Vector3', 15: 'Vector4', 16: 'Quat',
         17: 'Matrix3', 18: 'Matrix4', 19: 'Color', 20: 'FilePtr', 21: 'EntityHandle', 22: 'EntityLink',
         23: 'PropertyInfo', 24: 'WideVector3'}
CONTENEDORES = {0: 'StaticArray', 1: 'DynamicArray', 2: 'StringMap', 3: 'List'}
# tamano de cada elemento en bytes (None = no observado en PES 2021: se deduce del tamano de la propiedad y se vuelca en hex)
TAM = {0: 1, 1: 1, 2: 2, 3: 2, 4: 4, 5: 4, 6: 8, 7: 8, 8: 4, 9: 8, 10: 1, 11: 8, 12: 8, 13: 8, 14: 16, 15: 16, 16: 16,
       17: 48, 18: 64, 19: 16, 20: 8, 21: 8, 22: 32, 23: None, 24: None}
VERIFICADOS = {1, 4, 5, 8, 10, 11, 12, 13, 14, 15, 16, 19, 20, 21, 22}   # tipos vistos en los 12 archivos de PES 2021
TIPOS_HASH = {11, 12, 20}


def f32(v):
    """float32 -> float con la precision justa (7 cifras) para que el JSON sea legible."""
    return float('%.7g' % v)


class Diccionario:
    def __init__(self, path=None):
        self.h48 = {}; self.h32 = {}; self.n = 0; self.path = path
        if path and os.path.exists(path):
            with open(path, 'r', encoding='utf-8', errors='replace') as f:
                for ln in f:
                    if not ln or ln[0] == '#': continue
                    p = ln.rstrip('\n').split('\t')
                    if len(p) < 2: continue
                    try:
                        if len(p) >= 3 and len(p[2]) == 12:
                            self.h48.setdefault(int(p[2], 16), p[1])
                        else:
                            self.h32.setdefault(int(p[0], 16), p[1])
                        self.n += 1
                    except ValueError:
                        continue

    def nombre(self, h):
        n = self.h48.get(h)
        if n is None and not self.h48:      # diccionario de 2 columnas (solo StrCode32): se acepta por los 32 bits bajos
            n = self.h32.get(h & 0xFFFFFFFF)
        return n


class Fox2:
    def __init__(self, data, nombre='', dic=None, avisos=None):
        self.d = data; self.nombre = nombre; self.dic = dic or Diccionario(None)
        self.avisos = avisos if avisos is not None else []
        self.cadenas = {}          # StrCode -> texto (tabla del archivo)
        self.hashes = {}           # StrCode -> (resuelto: str|None) de todos los campos hash vistos
        self.por_address = {}
        self.leer()

    def aviso(self, msg):
        self.avisos.append(msg)

    # --- resolucion de nombres
    def resolver(self, h, como_cadena=False):
        if h == HASH_VACIO:
            return ''
        s = self.cadenas.get(h)
        if s is None:
            s = self.dic.nombre(h)
        self.hashes[h] = s
        return s if s is not None else 'h_%012X' % h

    # --- lectura
    def leer(self):
        d = self.d
        if len(d) < 0x20 or d[:4] != b'\xf2box':
            raise ValueError('no es un .fox2 ("\\xf2box"): %r' % d[:4])
        self.version, self.n_ent, self.off_cadenas, self.off_ent = struct.unpack_from('<IIII', d, 4)
        if d[0x14:0x20] != b'\0' * 12: self.aviso('cabecera: relleno 0x14-0x1F no es cero: %s' % d[0x14:0x20].hex())
        if self.version != 0x35: self.aviso('version de cabecera inesperada: 0x%x (se esperaba 0x35)' % self.version)
        self.leer_cadenas()
        self.entidades = []
        off = self.off_ent
        while off + 0x40 <= self.off_cadenas:
            e = self.leer_entidad(off)
            self.entidades.append(e)
            off += e['size']
        if off != self.off_cadenas:
            self.aviso('las entidades terminan en 0x%x y la tabla de cadenas empieza en 0x%x' % (off, self.off_cadenas))
        if len(self.entidades) != self.n_ent:
            self.aviso('la cabecera declara %d entidades y se leyeron %d' % (self.n_ent, len(self.entidades)))
        # segunda pasada: nombres y referencias entre entidades
        for e in self.entidades:
            self.por_address[e['address']] = e
        for e in self.entidades:
            e['name'] = None
            for p in e['static_properties']:
                if p['name'] == 'name' and p['type'] == 'String' and p.get('value') is not None:
                    e['name'] = p['value']; break
        for e in self.entidades:
            for p in e['static_properties'] + e['dynamic_properties']:
                self.anotar_refs(p)

    def leer_cadenas(self):
        d = self.d; p = self.off_cadenas; self.lista_cadenas = []
        while p + 12 <= len(d):
            h, ln = struct.unpack_from('<QI', d, p)
            if h == 0 and ln == 0: break
            s = d[p + 12:p + 12 + ln].decode('utf-8', 'replace')
            if strcode(s) != h:
                self.aviso('tabla de cadenas: el hash %012X no es el StrCode de %r (%012X)' % (h, s, strcode(s)))
            self.cadenas[h] = s; self.lista_cadenas.append((h, s))
            p += 12 + ln
        self.fin_cadenas = p
        self.off_end = d.find(b'end\0', p)
        if self.off_end < 0: self.aviso('falta el pie "end"')
        elif d[p:self.off_end] != b'\0' * (self.off_end - p) or d[self.off_end + 4:] != b'\0' * (len(d) - self.off_end - 4):
            self.aviso('bytes no nulos alrededor del pie "end"')

    def leer_entidad(self, off):
        d = self.d
        hs, unk1 = struct.unpack_from('<HI', d, off)
        magia = d[off + 6:off + 10]
        address, z0, z1, z2, ver_ent, cver = struct.unpack_from('<IIIHHH', d, off + 0x0A)
        chash, n_est, n_din, off_est, off_din, tam = struct.unpack_from('<QHHIII', d, off + 0x1C)
        if magia != b'ent\0': raise ValueError('entidad en 0x%x sin marca "ent": %r' % (off, magia))
        if hs != 0x40: self.aviso('entidad 0x%x: tamano de cabecera 0x%x' % (off, hs))
        if (z0, z1, z2) != (0, 0, 0): self.aviso('entidad 0x%x: campos +0E..+17 no nulos: %x %x %x' % (off, z0, z1, z2))
        if ver_ent != 0: self.aviso('entidad 0x%x: campo +18 = %d (siempre 0 en PES 2021)' % (off, ver_ent))
        if d[off + 0x34:off + 0x40] != b'\0' * 12: self.aviso('entidad 0x%x: relleno +34 no nulo' % off)
        if off_est != hs: self.aviso('entidad 0x%x: props estaticas en +0x%x (cabecera 0x%x)' % (off, off_est, hs))
        if tam == 0 or off + tam > self.off_cadenas: raise ValueError('entidad 0x%x: tamano invalido 0x%x' % (off, tam))
        e = {'offset': off, 'address': address, 'class': self.resolver(chash), 'class_hash': '%012X' % chash,
             'class_version': cver, 'unk1': unk1, 'entity_version': ver_ent, 'size': tam,
             'static_count': n_est, 'dynamic_count': n_din,
             'static_properties': self.leer_props(off + off_est, off + off_din, off),
             'dynamic_properties': self.leer_props(off + off_din, off + tam, off)}
        if len(e['static_properties']) != n_est or len(e['dynamic_properties']) != n_din:
            self.aviso('entidad 0x%x: declara %d/%d propiedades y se leyeron %d/%d' % (
                off, n_est, n_din, len(e['static_properties']), len(e['dynamic_properties'])))
        return e

    def leer_props(self, p, fin, ent_off):
        props = []
        while p + 0x20 <= fin:
            nh, tipo, cont, n, off_datos, tam = struct.unpack_from('<QBBHHH', self.d, p)
            if tam < 0x20 or p + tam > fin:
                self.aviso('propiedad en 0x%x: tamano invalido 0x%x' % (p, tam)); break
            if off_datos != 0x20: self.aviso('propiedad 0x%x: datos en +0x%x' % (p, off_datos))
            if self.d[p + 0x10:p + 0x20] != b'\0' * 16: self.aviso('propiedad 0x%x: relleno +10 no nulo' % p)
            if tam % 16: self.aviso('propiedad 0x%x: tamano 0x%x no multiplo de 16' % (p, tam))
            datos = self.d[p + off_datos:p + tam]
            pr = {'offset': p, 'name': self.resolver(nh), 'name_hash': '%012X' % nh,
                  'type': TIPOS.get(tipo, 'Tipo%d' % tipo), 'type_id': tipo,
                  'container': CONTENEDORES.get(cont, 'Cont%d' % cont), 'array_size': n}
            if tipo not in TIPOS: self.aviso('propiedad 0x%x: tipo desconocido %d' % (p, tipo))
            if cont not in CONTENEDORES: self.aviso('propiedad 0x%x: contenedor desconocido %d' % (p, cont))
            self.decodificar(pr, tipo, cont, n, datos, p)
            props.append(pr); p += tam
        if p != fin: self.aviso('propiedades: terminan en 0x%x, region hasta 0x%x' % (p, fin))
        return props

    def decodificar(self, pr, tipo, cont, n, datos, p):
        tam = TAM.get(tipo)
        clave = 8 if cont == 2 else 0
        if tam is None:
            tam = (len(datos) // n - clave) if n else 0
            pr['element_size_guessed'] = tam
            self.aviso('propiedad 0x%x: tipo %s sin tamano conocido, se asume %d bytes' % (p, pr['type'], tam))
        paso = tam + clave
        if n * paso > len(datos):
            self.aviso('propiedad 0x%x: %d elementos de %d bytes no entran en %d bytes de datos' % (p, n, paso, len(datos)))
            n = len(datos) // paso if paso else 0
        resto = datos[n * paso:]
        if resto != b'\0' * len(resto): pr['trailing_bytes'] = resto.hex()
        valores = []; claves = []
        for i in range(n):
            b = datos[i * paso:(i + 1) * paso]
            if clave:
                kh = struct.unpack_from('<Q', b, 0)[0]
                claves.append(self.resolver(kh)); b = b[8:]
            valores.append(self.valor(tipo, b))
        if cont == 2:
            pr['value'] = dict(zip(claves, valores)) if len(set(claves)) == len(claves) else list(zip(claves, valores))
        elif cont == 0 and n == 1:
            pr['value'] = valores[0]
        else:
            pr['value'] = valores

    def valor(self, t, b):
        if t in (0, 2, 4, 6): return int.from_bytes(b, 'little', signed=True)
        if t in (1, 3, 5, 7): return int.from_bytes(b, 'little')
        if t == 8: return f32(struct.unpack('<f', b)[0])
        if t == 9: return struct.unpack('<d', b)[0]
        if t == 10: return bool(b[0]) if len(b) == 1 else b.hex()
        if t in TIPOS_HASH:
            h = struct.unpack('<Q', b)[0]
            return self.resolver(h)
        if t in (13, 21):
            return struct.unpack('<Q', b)[0]      # address; se anota en anotar_refs
        if t in (14, 15, 16, 19, 24) and len(b) == 16:
            x = [f32(v) for v in struct.unpack('<4f', b)]
            if t == 19: return {'r': x[0], 'g': x[1], 'b': x[2], 'a': x[3]}
            return {'x': x[0], 'y': x[1], 'z': x[2], 'w': x[3]}
        if t in (17, 18) and len(b) % 16 == 0:
            return [[f32(v) for v in struct.unpack_from('<4f', b, i)] for i in range(0, len(b), 16)]
        if t == 22 and len(b) == 32:
            pk, ar, nm, addr, pad = struct.unpack('<QQQII', b)
            r = {'packagePath': self.resolver(pk), 'archivePath': self.resolver(ar), 'nameInArchive': self.resolver(nm),
                 'address': addr}
            if pad: r['pad'] = pad
            return r
        return b.hex()

    def anotar_refs(self, pr):
        """EntityPtr/EntityHandle/EntityLink: agrega el nombre y la clase de la entidad referida (si esta en el archivo)."""
        if pr['type_id'] not in (13, 21, 22): return
        def ref(addr):
            e = self.por_address.get(addr)
            if e is None: return {'address': addr, 'ref': None} if addr else None
            return {'address': addr, 'ref': e['name'] if e['name'] is not None else '', 'ref_class': e['class']}
        def conv(v):
            if pr['type_id'] == 22:
                if isinstance(v, dict):
                    r = ref(v['address'])
                    if r and r.get('ref') is not None: v['ref'] = r['ref']; v['ref_class'] = r['ref_class']
                return v
            return ref(v)
        v = pr['value']
        if isinstance(v, dict) and pr['container'] == 'StringMap':
            pr['value'] = {k: conv(x) for k, x in v.items()}
        elif isinstance(v, list) and pr['container'] != 'StaticArray' or (isinstance(v, list) and pr['array_size'] != 1):
            pr['value'] = [conv(x) for x in v]
        else:
            pr['value'] = conv(v)

    # --- salidas
    def estadisticas(self):
        n_props = sum(len(e['static_properties']) + len(e['dynamic_properties']) for e in self.entidades)
        sin = sorted(h for h, s in self.hashes.items() if s is None)
        clases = {}
        for e in self.entidades: clases[e['class']] = clases.get(e['class'], 0) + 1
        return {'entities': len(self.entidades), 'properties': n_props, 'strings_in_table': len(self.cadenas),
                'hash_fields_distinct': len(self.hashes), 'hashes_resolved': len(self.hashes) - len(sin),
                'hashes_unresolved': len(sin), 'unresolved_list': ['%012X' % h for h in sin],
                'classes': clases, 'warnings': list(self.avisos)}

    def a_json(self):
        return {'file': self.nombre, 'size': len(self.d),
                'header': {'magic': 'F2 62 6F 78 ("\\xf2box")', 'version': self.version, 'entity_count': self.n_ent,
                           'string_table_offset': self.off_cadenas, 'entities_offset': self.off_ent,
                           'string_table_end': self.fin_cadenas, 'end_marker_offset': self.off_end},
                'entities': self.entidades,
                'strings': {'%012X' % h: s for h, s in self.lista_cadenas},
                'stats': self.estadisticas()}

    def a_texto(self):
        L = []; st = self.estadisticas()
        L.append('# %s  (%d bytes)  fox2 version 0x%x  entidades %d  tabla de cadenas @0x%x (%d cadenas)  "end" @0x%x' % (
            self.nombre, len(self.d), self.version, self.n_ent, self.off_cadenas, len(self.cadenas), self.off_end))
        L.append('# propiedades %d; hashes distintos %d, resueltos %d, sin resolver %d' % (
            st['properties'], st['hash_fields_distinct'], st['hashes_resolved'], st['hashes_unresolved']))
        for a in self.avisos: L.append('# AVISO: ' + a)
        for i, e in enumerate(self.entidades):
            L.append('')
            L.append('[%d] @0x%05x addr=0x%x  %s v%d  name=%s  (props %d+%d, unk1=0x%x, size=0x%x)' % (
                i, e['offset'], e['address'], e['class'], e['class_version'],
                json.dumps(e['name'], ensure_ascii=False) if e['name'] is not None else '-',
                e['static_count'], e['dynamic_count'], e['unk1'], e['size']))
            for titulo, props in (('', e['static_properties']), ('  -- dinamicas --', e['dynamic_properties'])):
                if titulo and props: L.append(titulo)
                for p in props:
                    L.append('    %-28s %s = %s' % (p['name'], self.tipo_txt(p), self.valor_txt(p)))
        L.append('')
        L.append('# tabla de cadenas (%d):' % len(self.lista_cadenas))
        for h, s in self.lista_cadenas: L.append('#   %012X  %s' % (h, s))
        if st['unresolved_list']:
            L.append('# hashes sin resolver: ' + ' '.join(st['unresolved_list']))
        return '\n'.join(L) + '\n'

    @staticmethod
    def tipo_txt(p):
        c = p['container']
        if c == 'StaticArray' and p['array_size'] == 1: return '%-13s' % p['type']
        return '%-13s' % ('%s<%s>[%d]' % (c, p['type'], p['array_size']))

    def valor_txt(self, p):
        t = p['type_id']
        def uno(v):
            if v is None: return 'null'
            if t in (13, 21) or (t == 22):
                if isinstance(v, dict) and 'packagePath' in v:
                    s = 'link(pkg=%s, arch=%s, name=%s, addr=0x%x' % (json.dumps(v['packagePath']), json.dumps(v['archivePath']),
                                                                      json.dumps(v['nameInArchive']), v['address'])
                    if v.get('ref') is not None: s += ' -> %s %s' % (v.get('ref_class'), json.dumps(v['ref'], ensure_ascii=False))
                    return s + ')'
                if isinstance(v, dict):
                    s = '0x%x' % v['address']
                    if v.get('ref') is not None: s += ' -> %s %s' % (v.get('ref_class'), json.dumps(v['ref'], ensure_ascii=False))
                    elif v['address']: s += ' (fuera del archivo)'
                    return s
                return str(v)
            if t in (14, 15, 16, 24) and isinstance(v, dict): return '(%g, %g, %g, %g)' % (v['x'], v['y'], v['z'], v['w'])
            if t == 19 and isinstance(v, dict): return 'rgba(%g, %g, %g, %g)' % (v['r'], v['g'], v['b'], v['a'])
            if t in (17, 18) and isinstance(v, list): return '[' + '; '.join('(%s)' % ', '.join('%g' % x for x in fila) for fila in v) + ']'
            if isinstance(v, bool): return 'true' if v else 'false'
            if isinstance(v, str): return json.dumps(v, ensure_ascii=False)
            if isinstance(v, float): return '%g' % v
            return str(v)
        v = p['value']
        if p['container'] == 'StringMap':
            items = v.items() if isinstance(v, dict) else v
            return '{ ' + ', '.join('%s: %s' % (json.dumps(k, ensure_ascii=False), uno(x)) for k, x in items) + ' }'
        if isinstance(v, list) and not (p['container'] == 'StaticArray' and p['array_size'] == 1):
            return '[' + ', '.join(uno(x) for x in v) + ']'
        return uno(v)


def nombres_salida(paths):
    """Nombre base de salida por archivo. Si dos .fox2 comparten nombre, cada uno lleva delante su paquete
    (.fpkd / .fpk, o la carpeta padre) salvo que el paquete se llame igual que el archivo."""
    bases = [os.path.splitext(os.path.basename(p))[0] for p in paths]
    rep = {b for b in bases if bases.count(b) > 1}
    out = []; usados = set()
    for p, b in zip(paths, bases):
        n = b
        if b in rep:
            partes = p.replace('\\', '/').split('/')
            paq = next((q for q in partes if q.endswith(('.fpkd', '.fpk'))), None)
            paq = os.path.splitext(paq)[0] if paq else (partes[-2] if len(partes) > 1 else '')
            if paq and paq != b: n = paq + '__' + b
        while n in usados: n += '_'
        usados.add(n); out.append(n)
    return out


def procesar(path, json_out, txt_out, dic, quiet):
    data = open(path, 'rb').read()
    fx = Fox2(data, os.path.basename(path), dic)
    if json_out:
        with open(json_out, 'w', encoding='utf-8') as f:
            json.dump(fx.a_json(), f, ensure_ascii=False, indent=1)
    if txt_out:
        with open(txt_out, 'w', encoding='utf-8') as f:
            f.write(fx.a_texto())
    if not json_out and not txt_out:
        sys.stdout.write(fx.a_texto())
    st = fx.estadisticas()
    if not quiet:
        print('%s: %d entidades, %d propiedades, hashes %d/%d resueltos%s' % (
            os.path.basename(path), st['entities'], st['properties'], st['hashes_resolved'], st['hash_fields_distinct'],
            '' if not fx.avisos else ', %d avisos' % len(fx.avisos)), file=sys.stderr)
    return st


def main(argv=None):
    ap = argparse.ArgumentParser(description='Decodifica archivos .fox2 de Fox Engine (PES 2021) a JSON y texto legible.')
    ap.add_argument('archivos', nargs='+', help='archivo.fox2 [salida.json]  (o varios .fox2 con --outdir)')
    ap.add_argument('--txt', help='salida de texto legible')
    ap.add_argument('--outdir', help='carpeta de salida: escribe <nombre>.json y <nombre>.txt por cada .fox2')
    ap.add_argument('--dict', default=DICT_DEFAULT, help='diccionario TSV de StrCode (defecto %s)' % DICT_DEFAULT)
    ap.add_argument('--sin-dict', action='store_true', help='no usar el diccionario (solo la tabla de cadenas del archivo)')
    ap.add_argument('--quiet', '-q', action='store_true')
    a = ap.parse_args(argv)
    dic = Diccionario(None if a.sin_dict else a.dict)
    if not a.sin_dict and dic.n == 0 and not a.quiet:
        print('aviso: diccionario %s vacio o inexistente' % a.dict, file=sys.stderr)
    if a.outdir:
        os.makedirs(a.outdir, exist_ok=True); tot = {'entities': 0, 'properties': 0, 'hash_fields_distinct': 0, 'hashes_resolved': 0}
        for f, base in zip(a.archivos, nombres_salida(a.archivos)):
            st = procesar(f, os.path.join(a.outdir, base + '.json'), os.path.join(a.outdir, base + '.txt'), dic, a.quiet)
            for k in tot: tot[k] += st[k]
        if not a.quiet: print('total: %(entities)d entidades, %(properties)d propiedades, hashes %(hashes_resolved)d/%(hash_fields_distinct)d' % tot, file=sys.stderr)
        return 0
    fox2 = a.archivos[0]
    json_out = a.archivos[1] if len(a.archivos) > 1 else None
    if len(a.archivos) > 2: ap.error('demasiados argumentos; para varios archivos use --outdir')
    procesar(fox2, json_out, a.txt, dic, a.quiet)
    return 0


if __name__ == '__main__':
    sys.exit(main())
