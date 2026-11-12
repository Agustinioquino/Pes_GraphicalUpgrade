#!/usr/bin/env python3
# Lector de las tablas de secuencias de cara y manos de PES 2021 (dt13_all.cpk common/anime/FHSequence/bin/*.bin).
# OJO: "serie" (entrada +0x6e) NO es el largo: es un numero de orden de la secuencia (crece con el id). Los tiempos de los tramos van en cuadros a 30 por segundo.
# Formato leido del cargador del exe FUN_141edd3e0 (12/11/2026) y comprobado contra los archivos reales.
# Uso: python3 fhseq_leer.py <carpeta con los .bin> [id de animacion]
import struct, sys, os, collections
ADD = "add_ago_open add_d_me_mihiraku add_eyeblink_single add_kuti add_kuti_toji add_kuti_yoko add_kuti_yoko_close add_mayu_down add_mayu_up add_me_toji add_miken_yose old_add_kuchipaku old_add_kuchisubome old_add_metoji old_add_mihiraki".split()
def faceadd(d):      # 12 B: w0 = id+164 (11 b) | serie-105 (11 b) | fin-11 (10 b); w1 = inicio (10) | finOrigen-1 (6) | fadeOut (6) | clip-2 (4) | fadeIn (4) | - | inicioOrigen (1); w2 = peso maximo (float)
    for i in range(len(d) // 12):
        w0, w1 = struct.unpack_from('<II', d, i * 12); mw = struct.unpack_from('<f', d, i * 12 + 8)[0]
        yield dict(id=(w0 & 0x7ff) - 0xa4, serie=((w0 >> 11) & 0x7ff) + 0x69, inicio=w1 & 0x3ff, fin=(w0 >> 22) + 0xb, origen_inicio=w1 >> 31, origen_fin=((w1 >> 10) & 0x3f) + 1,
                   clip=((w1 >> 22) & 0xf) + 2, peso=mw, fade_in=(w1 >> 26) & 0xf, fade_out=(w1 >> 16) & 0x3f)
def facebase(d):     # 12 B: w0 = id+216 (13 b) | serie (12 b); w1 = finOrigen-1 (10) | fin+7 (10) | inicio+27 (10); w2 = inicioOrigen (10) | clip (8)
    for i in range(len(d) // 12):
        w0, w1, w2 = struct.unpack_from('<III', d, i * 12)
        yield dict(id=(w0 & 0x1fff) - 0xd8, serie=(w0 >> 13) & 0xfff, inicio=((w1 >> 20) & 0x3ff) - 0x1b, fin=((w1 >> 10) & 0x3ff) - 7, origen_inicio=w2 & 0x3ff, origen_fin=(w1 & 0x3ff) + 1, clip=(w2 >> 10) & 0xff)
def seqinfo(d):      # 8 B: w0 = id+216 (13 b) | serie (12 b) | grupo (5 b) | b30 | b31; w1 = b0 | b1
    for i in range(len(d) // 8):
        w0, w1 = struct.unpack_from('<II', d, i * 8)
        yield dict(id=(w0 & 0x1fff) - 0xd8, serie=(w0 >> 13) & 0xfff, grupo=(w0 >> 25) & 0x1f, b30=(w0 >> 30) & 1, b31=w0 >> 31, tiene_base=(w1 >> 1) & 1, b0=w1 & 1)
def hitframes(d):    # 8 B: w0 = id+216 (13 b) | serie-17 (12 b); w1 = cuadro del golpe (1 byte)
    for i in range(len(d) // 8):
        w0, w1 = struct.unpack_from('<II', d, i * 8)
        yield dict(id=(w0 & 0x1fff) - 0xd8, serie=((w0 >> 13) & 0xfff) + 0x11, golpe=w1 & 0xff)
if __name__ == '__main__':
    c = sys.argv[1]; rd = lambda n: open(os.path.join(c, n), 'rb').read()
    fb = list(facebase(rd('Facebase.bin'))); fa = list(faceadd(rd('Faceadd.bin'))); si = list(seqinfo(rd('Sequenceinfo.bin'))); hf = list(hitframes(rd('Hitframes.bin')))
    if len(sys.argv) > 2:
        a = int(sys.argv[2])
        for nom, t in (('base', fb), ('sumado', fa), ('info', si), ('golpe', hf)):
            for r in t:
                if r['id'] == a: print(nom, r)
    else:
        print('Facebase', len(fb), 'tramos,', len({r['id'] for r in fb}), 'animaciones; Faceadd', len(fa), 'tramos,', len({r['id'] for r in fa}), 'animaciones; Sequenceinfo', len(si), '; Hitframes', len(hf))
        print('sumados usados:', collections.Counter(ADD[r['clip']] for r in fa).most_common())

# ---- escritura (12/11/2026): inversa exacta de faceadd() / facebase(); 'resto*' guarda los bits que el cargador no lee ----
def faceadd_escribir(filas):
    out = bytearray()
    for r in filas:
        a = r['id'] + 0xa4; l = r['serie'] - 0x69; f = r['fin'] - 0xb; c = r['clip'] - 2; of = r['origen_fin'] - 1
        if not (0 <= a < 0x800 and 0 <= l < 0x800 and 0 <= f < 0x400 and 0 <= r['inicio'] < 0x400 and 0 <= of < 0x40 and 0 <= r['fade_out'] < 0x40 and 0 <= c < 0x10 and 0 <= r['fade_in'] < 0x10 and r['origen_inicio'] in (0, 1)):
            raise ValueError('fuera de rango: %r' % (r,))
        w0 = a | l << 11 | f << 22
        w1 = r['inicio'] | of << 10 | r['fade_out'] << 16 | c << 22 | r['fade_in'] << 26 | r.get('resto1', 0) << 30 | r['origen_inicio'] << 31
        out += struct.pack('<IIf', w0, w1, r['peso'])
    return bytes(out)
def facebase_crudo(d):   # como facebase() pero con los bits sin leer, para poder reescribir igual
    for i, r in enumerate(facebase(d)):
        w0, w1, w2 = struct.unpack_from('<III', d, i * 12)
        r['resto0'] = w0 >> 25; r['resto1'] = w1 >> 30; r['resto2'] = w2 >> 18
        yield r
def facebase_escribir(filas):
    out = bytearray()
    for r in filas:
        a = r['id'] + 0xd8; i = r['inicio'] + 0x1b; f = r['fin'] + 7; of = r['origen_fin'] - 1
        if not (0 <= a < 0x2000 and 0 <= r['serie'] < 0x1000 and 0 <= i < 0x400 and 0 <= f < 0x400 and 0 <= of < 0x400 and 0 <= r['origen_inicio'] < 0x400 and 0 <= r['clip'] < 0x100):
            raise ValueError('fuera de rango: %r' % (r,))
        out += struct.pack('<III', a | r['serie'] << 13 | r.get('resto0', 0) << 25, of | f << 10 | i << 20 | r.get('resto1', 0) << 30, r['origen_inicio'] | r['clip'] << 10 | r.get('resto2', 0) << 18)
    return bytes(out)
