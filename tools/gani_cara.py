# PES21 FX (07/11) - lector de .gani / .mtar / .ask / .frig de Fox Engine (PES 2021) para la CARA: vuelca las pistas de un clip
# con el nombre del hueso (StrCode32, foxhash.py) y los cuadros clave decodificados. Pensado para los archivos que el RC extrae
# de dt13_all.cpk (Face/CharacterAssets) y dt12_g4.cpk (FixDemo/Animations). Formato segun pes-gani (MIT, jhnzbdev):
#   MTAR : u32 sig, u32 count, u16 huesos, u16 canales, 20 pad; count x (u32 hash, i32 id, u32 addr, u32 size)
#   GANI : u32 sig, u32 nodeOff (0x20), u32 size ... cabecera del movimiento en nodeOff + 0x70: u32 grupos, u32 canales,
#          u32 timebase, u32 cuadros, u32 unk, grupos x u32 offset (relativo a la cabecera). Grupo: u32 hash, u8 nCanales, 3 pad,
#          nCanales x (u32 offset relativo a la entrada, i16 indiceCanal, u8 flags, i8 bits).
#   Los hashes de los grupos NO son nombres de huesos (son los mismos en cuerpo y cara); el hueso sale del .frig (mapa_canales).
#   Posiciones en CENTIMETROS y rotaciones locales respecto del reposo del .ask (cara: ejes de skf_head, x arriba, y atras, z derecha).
#   canal: bits 12 / 20 (rotacion): por clave 3 fracciones de `bits`, 3 signos, paso de 8 bits (la ultima sin paso);
#          bits 16 (posicion): 3 half + i8 paso.  rotacion: frac0 = angulo (x pi/2 = medio angulo), frac1/2 = eje L1 (3o = 1-a-b).
# Uso: gani_cara.py clip.gani [--ask face_anim_skel.ask] [--frig face_skel.frig] [--mtar eye_anime_file0.mtar] [--json salida.json]
import sys, struct, math, json, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from foxhash import strcode32, HUESOS

NOMBRES = {strcode32(n): n for n in HUESOS}

class Bits:
    def __init__(self, b): self.b = b; self.n = 0; self.len = len(b) * 8
    def uint(self, k):
        v = 0
        for i in range(k):
            v |= ((self.b[self.n >> 3] >> (self.n & 7)) & 1) << i; self.n += 1
        return v

def half(h): return struct.unpack('<e', struct.pack('<H', h))[0]

def leer_mtar(d):
    sig, count, nb, nc = struct.unpack('<IIHH', d[:12])
    ents = [dict(zip(('hash', 'id', 'addr', 'size'), struct.unpack('<IiII', d[0x20 + 16 * i:0x30 + 16 * i]))) for i in range(count)]
    return {'sig': sig, 'count': count, 'huesos': nb, 'canales': nc, 'entradas': ents}

def leer_gani(g):
    sig, node_off, size = struct.unpack('<III', g[:12])
    m = node_off + 0x70
    grupos, nch, timebase, cuadros, unk = struct.unpack('<5I', g[m:m + 20])
    offs = struct.unpack('<%dI' % grupos, g[m + 20:m + 20 + 4 * grupos])
    canales = []
    for o in offs:
        p = m + o
        th = struct.unpack('<I', g[p:p + 4])[0]; n = g[p + 4]
        for c in range(n):
            q = p + 8 + 8 * c
            a, ix, tf, bs = struct.unpack('<IhBb', g[q:q + 8])
            canales.append({'indice': ix, 'grupo': th, 'flags': tf, 'bits': bs, 'off': q + a})
    orden = sorted(range(len(canales)), key=lambda i: canales[i]['off'])
    for k, i in enumerate(orden):
        fin = canales[orden[k + 1]]['off'] if k + 1 < len(orden) else len(g)
        canales[i]['size'] = fin - canales[i]['off']
    return {'sig': sig, 'node_off': node_off, 'size': size, 'grupos': grupos, 'ncanales': nch, 'timebase': timebase, 'cuadros': cuadros, 'unk': unk, 'canales': canales}

def claves_rot(data, bits):
    B = Bits(data); out = []; f = 0; full = 3 * bits + 3
    while B.n + full <= B.len:
        v = [B.uint(bits) / float(1 << bits) for _ in range(3)]; s = [B.uint(1) for _ in range(3)]
        out.append((f, v, s))
        if B.n + 8 > B.len: break
        st = B.uint(8)
        if st == 0: break
        f += st
    return out

def claves_pos(data):
    out = []; p = 0; f = 0
    while p + 6 <= len(data):
        v = [half(struct.unpack('<H', data[p + 2 * i:p + 2 * i + 2])[0]) for i in range(3)]
        out.append((f, v)); p += 6
        if p >= len(data): break
        st = struct.unpack('b', data[p:p + 1])[0]; p += 1
        if st <= 0: break
        f += st
    return out

def quat(frac, signos):
    a, b = frac[1], frac[2]; c = 1.0 - a - b
    n = math.sqrt(a * a + b * b + c * c); h = frac[0] * math.pi / 2.0
    s = math.sin(h) / n if n > 1e-9 else 0.0
    x, y, z = a * s, b * s, c * s
    return [-x if signos[0] else x, -y if signos[1] else y, -z if signos[2] else z, math.cos(h)]

def leer_ask(d):
    """.ask version 2 (cara): u32 version, u32 n, 12 B cero, luego n registros de 28 B desde 0x14: i32 offset del nombre (relativo
    al registro), i32 padre, float3 posicion LOCAL en metros (ejes del hueso padre: en la cara x = arriba, y = atras, z = derecha
    del jugador), 2 x u32 (0). Sin rotacion: el reposo de animacion es identidad. Version 1 (cuerpo, pes-gani): 44 B con rotacion."""
    ver, count = struct.unpack('<ii', d[:8]); huesos = []
    if ver == 2:
        p = 0x14
        for i in range(count):
            name_off, parent, px, py, pz = struct.unpack('<iifff', d[p:p + 20])
            s = p + name_off; nombre = d[s:d.index(b'\0', s)].decode('ascii', 'replace')
            huesos.append({'nombre': nombre, 'padre': parent, 'pos': [px, py, pz], 'rot': [0.0, 0.0, 0.0, 1.0]}); p += 28
        return {'version': ver, 'huesos': huesos}
    p = 8
    for i in range(count):
        name_off, parent = struct.unpack('<ii', d[p + 4:p + 12])
        pos = struct.unpack('<4f', d[p + 12:p + 28]); rot = struct.unpack('<4f', d[p + 28:p + 44])
        s = p + 4 + name_off; nombre = d[s:d.index(b'\0', s)].decode('ascii', 'replace')
        huesos.append({'nombre': nombre, 'padre': parent, 'pos': list(pos[:3]), 'rot': list(rot)}); p += 44
    return {'version': ver, 'huesos': huesos}

def mapa_canales(unidades, canales):
    """{indiceCanal: ('rot'|'pos', indice de hueso en el .ask, -1 = raiz)}: los canales de rotacion de una unidad van a sus huesos en
    orden; los de posicion al primer hueso. En la cara (face_skel.frig, 32 unidades / 59 canales): unidad 0 raiz (rot + pos),
    1 skf_head (tipo 7), 2 skf_jaw (tipo 4, solo rot), 3..30 un hueso con rot + pos (tipo 5) o solo rot (tipo 4: parpados), 31 NMdriverB."""
    kinds = {c['indice']: ('pos' if c['bits'] == 16 else 'rot') for c in canales}; out = {}
    for u in unidades:
        chs = range(u['ch0'], u['ch0'] + u['ncanales'])
        rots = [c for c in chs if kinds.get(c) == 'rot']; poss = [c for c in chs if kinds.get(c) == 'pos']
        if u['nhuesos'] == 0:
            for c in rots: out[c] = ('rot', -1)
            for c in poss: out[c] = ('pos', -1)
            continue
        for c, b in zip(rots, u['huesos']): out[c] = ('rot', b)
        for c in poss: out[c] = ('pos', u['huesos'][0])
    return out

def leer_frig(d):
    h = struct.unpack('<8I', d[:32]); n_units, n_chan = h[3], h[4]
    offs = struct.unpack('<%dI' % n_units, d[0x20:0x20 + 4 * n_units]); ends = list(offs[1:]) + [h[7]]
    units = []; ch0 = 0
    for o, e in zip(offs, ends):
        u = struct.unpack('<%dH' % ((e - o) // 2), d[o:e])
        kind, _, nch, nb, pb, pu = u[:6]; start = 8 if e - o == 32 else 24
        units.append({'tipo': kind, 'ncanales': nch, 'nhuesos': nb, 'huesoPadre': None if pb == 0xFFFF else pb,
                      'unidadPadre': None if pu == 0xFFFF else pu, 'huesos': list(u[start:start + nb]), 'ch0': ch0, 'bytes': e - o,
                      'crudo': [('%04X' % x) for x in u]}); ch0 += nch
    # hashes de 32 bits que aparezcan dentro (el frig de la cara referencia skf_ por StrCode32)
    hashes = []
    for i in range(0, len(d) - 3):
        v = struct.unpack_from('<I', d, i)[0]
        if v in NOMBRES: hashes.append((i, NOMBRES[v]))
    return {'cabecera': ['%08X' % x for x in h], 'unidades': units, 'ncanales': n_chan, 'hashes': hashes}

def main():
    a = sys.argv[1:]
    if not a: print(__doc__ or 'uso: gani_cara.py clip.gani [--ask] [--frig] [--mtar] [--json]'); return
    def opt(k):
        return a[a.index(k) + 1] if k in a else None
    salida = {}
    if opt('--ask'):
        A = leer_ask(open(opt('--ask'), 'rb').read()); salida['ask'] = A
        print('ask version %d, %d huesos:' % (A['version'], len(A['huesos'])))
        for i, b in enumerate(A['huesos']): print('  %2d %-28s padre %3d pos (%.4f %.4f %.4f) rot (%.3f %.3f %.3f %.3f) StrCode32 %08X' % (i, b['nombre'], b['padre'], *b['pos'], *b['rot'], strcode32(b['nombre'])))
    if opt('--frig'):
        F = leer_frig(open(opt('--frig'), 'rb').read()); salida['frig'] = F
        print('frig cabecera', F['cabecera'], 'canales', F['ncanales'])
        for i, u in enumerate(F['unidades']): print('  unidad %2d tipo %d canales %d huesos %s (padre hueso %s, unidad %s) ch0 %d [%d B] %s' % (i, u['tipo'], u['ncanales'], u['huesos'], u['huesoPadre'], u['unidadPadre'], u['ch0'], u['bytes'], ' '.join(u['crudo'])))
        for off, n in F['hashes']: print('  hash en +0x%X: %s' % (off, n))
    if opt('--mtar'):
        Mt = leer_mtar(open(opt('--mtar'), 'rb').read()); salida['mtar'] = Mt
        print('mtar sig %08X, %d clips, %d huesos, %d canales' % (Mt['sig'], Mt['count'], Mt['huesos'], Mt['canales']))
        for e in Mt['entradas']: print('  hash %08X id %d addr %d size %d %s' % (e['hash'], e['id'], e['addr'], e['size'], NOMBRES.get(e['hash'], '')))
    clip = a[0] if not a[0].startswith('--') else None
    if clip:
        g = open(clip, 'rb').read(); G = leer_gani(g); salida['gani'] = {k: v for k, v in G.items() if k != 'canales'}
        print('%s: sig %08X node_off %d size %d, %d grupos, %d canales, timebase %d, %d cuadros, unk %d' % (os.path.basename(clip), G['sig'], G['node_off'], G['size'], G['grupos'], G['ncanales'], G['timebase'], G['cuadros'], G['unk']))
        pistas = []
        cm = mapa_canales(salida['frig']['unidades'], G['canales']) if 'frig' in salida else {}
        nombresAsk = [b['nombre'] for b in salida['ask']['huesos']] if 'ask' in salida else []
        for c in G['canales']:
            data = g[c['off']:c['off'] + c['size']]
            if c['bits'] == 16:
                k = claves_pos(data); vals = [[f] + [round(x, 5) for x in v] for f, v in k]; tipo = 'pos'
            else:
                k = claves_rot(data, c['bits']); vals = []; prev = None
                for f, v, s in k:
                    q = quat(v, s)
                    if prev is not None and sum(p * x for p, x in zip(prev, q)) < 0: q = [-x for x in q]
                    prev = q; vals.append([f] + [round(x, 6) for x in q])
                tipo = 'rot'
            nombre = NOMBRES.get(c['grupo'], '%08X' % c['grupo'])
            if c['indice'] in cm:
                k_, b_ = cm[c['indice']]; nombre = 'RAIZ' if b_ < 0 else (nombresAsk[b_] if b_ < len(nombresAsk) else 'hueso%d' % b_)
            pistas.append({'indice': c['indice'], 'grupo': nombre, 'flags': c['flags'], 'bits': c['bits'], 'tipo': tipo, 'claves': vals, 'bytes': c['size']})
            print('  canal %3d grupo %-26s flags %d bits %2d %s %4d B, %3d claves, ultima en %d' % (c['indice'], nombre, c['flags'], c['bits'], tipo, c['size'], len(vals), vals[-1][0] if vals else -1))
            for v in vals[:3]: print('      ', v)
        salida['pistas'] = pistas
    if opt('--json'):
        json.dump(salida, open(opt('--json'), 'w'), indent=1); print('json ->', opt('--json'))

if __name__ == '__main__': main()
