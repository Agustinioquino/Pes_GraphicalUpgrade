#!/usr/bin/env python3
"""Laboratorio de animaciones en el Motor del Editar (21/11/2026): arma los dos .fmdl del banco del estadio st024 para la escena del Motor.
Saca de st024.fpk center1_target.fmdl y center1_probe.fmdl y deja SIN DIBUJAR (triangulos degenerados en todos los LOD, igual que la banca
1h de la DLL) las mallas que no son del banco: el suelo de afuera de la cancha (nouse_ggx_st024_field, +-80 m) y el anillo del techo
(mortar_a, +-156 m, 30 m de alto). Quedan: bancos (bnch000 / 001 / 010 / 010_1 / 011), la pared y el cesped del foso.
Medido (tools/fox/fmdl.py): banco de la izquierda x -17.4..-5.4, foso desde z 41.99 hasta 45.5, piso del foso y -0.46, asiento y 0.13,
respaldo z 44.95; los suplentes miran a -z.
uso: python3 banca_fmdl.py st024.fpk carpeta_salida
"""
import os, struct, sys, zlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'fox'))
import fpk_edit as P
import fmdl as FM

SACAR = {'center1_target.fmdl': {'nouse_ggx_st024_field_st024_lawn000'}, 'center1_probe.fmdl': {'mortar_a'}}

def mat_names(f):
    c, o = f.b0[4]
    return [f.strings[struct.unpack_from('<H', f.d, o + 0x10 * i)[0]] for i in range(c)]

def degenerar(path, sacar):
    f = FM.Fmdl(path); d = bytearray(f.d); mats = mat_names(f)
    ib = [struct.unpack_from('<IIII', f.d, f.b0[14][1] + 16 * i) for i in range(f.b0[14][0])]
    ib_off = f.b1[2][0] + [b for b in ib if b[0] == 1][0][2]
    lods = struct.unpack_from('<I', f.d, f.b0[16][1])[0] if 16 in f.b0 else 1
    cnt, off = f.b0[3]; hechas = []
    for k in range(cnt):
        e = off + 0x30 * k
        mat, = struct.unpack_from('<H', f.d, e + 4)
        nombre = mats[mat] if mat < len(mats) else '?'
        if nombre not in sacar: continue
        ff, fc = struct.unpack_from('<II', f.d, e + 0x10); fi, = struct.unpack_from('<Q', f.d, e + 0x18)
        lo, hi = 0, fc
        for j in range(lods):
            a, c = struct.unpack_from('<II', f.d, f.b0[17][1] + 8 * (fi + j)); lo = min(lo, a); hi = max(hi, a + c)
        n = (hi - lo) // 3 * 3; io = ib_off + 2 * (ff + lo)
        for t in range(0, n, 3):
            v, = struct.unpack_from('<H', d, io + 2 * t)
            struct.pack_into('<HHH', d, io + 2 * t, v, v, v)
        hechas.append('%s (%d triangulos)' % (nombre, n // 3))
    return bytes(d), hechas

def main():
    src, out = sys.argv[1], sys.argv[2]; os.makedirs(out, exist_ok=True)
    f = P.read(open(src, 'rb').read())
    for e in f['ents']:
        b = e[0].split('/')[-1]
        if b not in SACAR: continue
        tmp = os.path.join(out, b + '.orig'); open(tmp, 'wb').write(e[1])
        d, hechas = degenerar(tmp, SACAR[b]); os.remove(tmp)
        if len(hechas) != len(SACAR[b]): raise SystemExit('%s: no encontre todas las mallas a sacar (%s)' % (b, hechas))
        p = os.path.join(out, 'zfx_banca_' + b); open(p, 'wb').write(d)
        print('%s  %d B  CRC %08X  sin dibujar: %s' % (p, len(d), zlib.crc32(d), ', '.join(hechas)))

if __name__ == '__main__':
    main()
