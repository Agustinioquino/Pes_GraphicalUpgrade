# PES21 FX (08/11) - ESCRITOR de .gani de la CARA (Fox Engine, PES 2021). Hasta aca solo habia lector (gani_cara.py).
#
# Formato (medido byte a byte en 7 clips de Konami: base_normal, add_mayu_up, add_miken_yose, add_eyeblink_single, smil_soft,
# angr_brwnit_lorol, pain_brwtrb_shut_eyhc_hard; el escritor los rearma IDENTICOS, ver --probar):
#   0x00 u32 firma 0BFCA2D2, 0x04 u32 0x20, 0x08 u32 tamano del archivo, 0x20..0x8F nodo "MOTION" (fijo), 0x70 u32 = tamano - 0x90
#   0x90 cabecera del movimiento: u32 grupos (32), u32 canales (59), u32 01000001, u32 ULTIMO CUADRO, u32 cuadros por segundo/3 (10;
#        el juego corre la cara a 30 cuadros), grupos x u32 offset (relativo a 0x90), relleno a 16
#   0x130 grupos: u32 hash, u8 nCanales, u8 5 (todos los canales con una sola clave) / 1 (alguno animado), 2 x 0,
#        nCanales x (u32 offset de los datos relativo a la entrada, i16 indice del canal, u8 flags, i8 bits)
#   0x408 datos de los canales, en orden de indice, cada uno alineado a 2 bytes; el archivo se rellena a 16.
#   rotacion (12 bits): por clave 3 x 12 bits (angulo/pi, eje L1 a, eje L1 b; c = 1-a-b) + 3 bits de signo (x y z), entre claves un paso
#        de 8 bits (cuadros hasta la proxima); bits LSB primero. q = (signos * eje/|eje| * sin(h), cos(h)), h = angulo * pi/2.
#   posicion (16 bits): por clave 3 half (CENTIMETROS, corrimiento contra el reposo del .ask) + u8 paso (no lleva signo: 159 = 0x9F).
#   La ultima clave de cada canal animado cae en el ULTIMO CUADRO. Los canales sin movimiento llevan una sola clave.
#
# Ejes de la cara en el .gani (skf_head): x arriba, y atras, z derecha del jugador. Los parpados van ABSOLUTOS (base_normal: arriba
# 25 grados = abierto, abajo -2.5); el resto corrimiento / giro contra el reposo.
#
# Uso:  gani_escribir.py --probar carpeta_con_ganis          (rearma cada clip desde sus claves y compara byte a byte)
#       como modulo: escribir(plantilla_bytes, pistas, ultimo_cuadro) -> bytes; pistas = {indice: [(cuadro, valor)]}
#       valor = cuaternion (x, y, z, w) para canales de rotacion o (x, y, z) en cm para posicion.
import sys, os, struct, math, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gani_cara as GC

MOV = 0x90

class BitW:
    def __init__(self): self.b = bytearray(); self.n = 0
    def put(self, v, k):
        for i in range(k):
            if (self.n >> 3) >= len(self.b): self.b.append(0)
            if (v >> i) & 1: self.b[self.n >> 3] |= 1 << (self.n & 7)
            self.n += 1

def _pad(b, a):
    while len(b) % a: b.append(0)
    return b

# ---- claves crudas (lo que esta en el archivo) ----
def crudo_rot(data, bits):
    B = GC.Bits(data); out = []; f = 0
    while B.n + 3 * bits + 3 <= B.len:
        v = [B.uint(bits) for _ in range(3)]; s = [B.uint(1) for _ in range(3)]; out.append((f, v, s))
        if B.n + 8 > B.len: break
        st = B.uint(8)
        if st == 0: break
        f += st
    return out

def crudo_pos(data):
    out = []; p = 0; f = 0
    while p + 6 <= len(data):
        out.append((f, list(struct.unpack('<3H', data[p:p + 6])))); p += 6
        if p >= len(data): break
        st = data[p]; p += 1
        if st == 0: break
        f += st
    return out

def enc_rot(keys, bits):
    W = BitW()
    for i, (f, v, s) in enumerate(keys):
        for x in v: W.put(x, bits)
        for x in s: W.put(x, 1)
        if i + 1 < len(keys):
            st = keys[i + 1][0] - f
            assert 0 < st < 256, 'paso de rotacion fuera de rango'
            W.put(st, 8)
    return _pad(W.b, 2)

def enc_pos(keys):
    b = bytearray()
    for i, (f, h) in enumerate(keys):
        b += struct.pack('<3H', *h)
        if i + 1 < len(keys):
            st = keys[i + 1][0] - f
            assert 0 < st < 256, 'paso de posicion fuera de rango'
            b.append(st)
    return _pad(b, 2)

# ---- valores <-> crudo ----
def q_a_crudo(q, bits):
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0; x, y, z, w = x / n, y / n, z / n, w / n
    if w < 0: x, y, z, w = -x, -y, -z, -w
    h = math.acos(max(-1.0, min(1.0, w)))
    full = 1 << bits; top = full - 1
    ang = min(top, int(round(h / (math.pi / 2) * full)))
    L = abs(x) + abs(y) + abs(z)
    if L < 1e-9 or ang == 0: return [0, 0, 0], [0, 0, 0]
    a = int(round(abs(x) / L * full)); b = int(round(abs(y) / L * full))
    a = min(a, top); b = min(b, full - a if a + b > full else b); b = min(b, top)
    return [ang, a, b], [1 if x < 0 else 0, 1 if y < 0 else 0, 1 if z < 0 else 0]

def half_bits(v):
    return struct.unpack('<H', struct.pack('<e', float(v)))[0]

def p_a_crudo(p): return [half_bits(c) for c in p]

# ---- archivo ----
def leer_crudo(g):
    """{indice: ('rot', bits, claves) | ('pos', 16, claves)} + datos de la tabla"""
    G = GC.leer_gani(g); out = {}
    for c in G['canales']:
        data = g[c['off']:c['off'] + c['size']]
        out[c['indice']] = ('pos', 16, crudo_pos(data)) if c['bits'] == 16 else ('rot', c['bits'], crudo_rot(data, c['bits']))
    return G, out

def escribir_crudo(plantilla, crudo, ultimo):
    """rearma un .gani con la tabla de la plantilla (mismos grupos / canales / flags) y las claves crudas dadas"""
    t = bytearray(plantilla)
    grupos, nch = struct.unpack('<II', t[MOV:MOV + 8])
    offs = struct.unpack('<%dI' % grupos, t[MOV + 20:MOV + 20 + 4 * grupos])
    ents = []   # (posicion de la entrada del canal, indice, bits, posicion del byte de estado del grupo)
    for o in offs:
        p = MOV + o; n = t[p + 4]
        for c in range(n):
            q = p + 8 + 8 * c; _, ix, _, bs = struct.unpack('<IhBb', t[q:q + 8]); ents.append((q, ix, bs, p))
    fin_tabla = max(q for q, _, _, _ in ents) + 8
    out = bytearray(t[:fin_tabla]); datos = {}
    for q, ix, bs, _ in sorted(ents, key=lambda e: e[1]):
        k = crudo[ix][2]
        assert k and k[0][0] == 0, 'canal %d: la primera clave va en el cuadro 0' % ix
        datos[ix] = len(out)
        out += enc_pos(k) if bs == 16 else enc_rot(k, bs)
    _pad(out, 16)
    estado = {}
    for q, ix, bs, gp in ents:
        struct.pack_into('<I', out, q, datos[ix] - q)
        estado[gp] = estado.get(gp, False) or len(crudo[ix][2]) > 1
    for gp, anim in estado.items(): out[gp + 5] = 1 if anim else 5
    struct.pack_into('<I', out, 0x08, len(out)); struct.pack_into('<I', out, 0x70, len(out) - MOV)
    struct.pack_into('<I', out, MOV + 12, ultimo)
    return bytes(out)

def escribir(plantilla, pistas, ultimo, base=None):
    """pistas = {indice de canal: [(cuadro, valor), ...]}; los canales que falten salen de `base` (crudo de otro clip, p. ej.
    base_normal) o de la plantilla. Cuadros enteros (30 por segundo), el primero 0 y el ultimo = `ultimo` si es animado."""
    if base is None: base = leer_crudo(plantilla)[1]
    G = GC.leer_gani(plantilla); bits = {c['indice']: c['bits'] for c in G['canales']}
    crudo = dict(base)
    for ix, kv in pistas.items():
        kv = sorted(kv)
        if bits[ix] == 16: crudo[ix] = ('pos', 16, [(f, p_a_crudo(v)) for f, v in kv])
        else: crudo[ix] = ('rot', bits[ix], [(f, *q_a_crudo(v, bits[ix])) for f, v in kv])
    return escribir_crudo(plantilla, crudo, ultimo)

def probar(carpeta):
    ok = True
    for fn in sorted(glob.glob(os.path.join(carpeta, '*.gani'))):
        g = open(fn, 'rb').read(); G, cr = leer_crudo(g)
        r = escribir_crudo(g, cr, G['cuadros'])
        igual = r == g
        ok &= igual
        # ida y vuelta por valores: decodificar a cuaternion / cm y volver a cuantizar
        _, cr2 = leer_crudo(r); peor = 0
        for ix, (k, bs, keys) in cr2.items():
            if k != 'rot': continue
            for f, v, s in keys:
                q = GC.quat([x / float(1 << bs) for x in v], s); v2, s2 = q_a_crudo(q, bs)
                if v[0] == 0: continue
                peor = max(peor, max(abs(a - b) for a, b in zip(v, v2)))
        print('%-36s %6d B  %s  (rotaciones requantizadas: error max %d pasos)' % (os.path.basename(fn), len(g), 'IDENTICO' if igual else 'DISTINTO', peor))
    return ok

if __name__ == '__main__':
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] == '--probar': sys.exit(0 if probar(a[1]) else 1)
    print(__doc__ or 'uso: gani_escribir.py --probar carpeta')
