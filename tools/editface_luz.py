#!/usr/bin/env python3
"""Luz del editor de caras (EditFace.fpkd) parcheada en el lugar, para servirla con el livecpk de Sider.

(08/11, Agustin: "dale" a la version con la luz principal mas alta y menos relleno de abajo)
Ver /mnt/project-files/decompilado/detalle/fox2_luces_y_assets.md seccion 8: el .fox2 empieza en 0x90 del .fpkd, el dato de cada
propiedad en offset + 0x20 (los offsets de abajo ya son los del dato dentro del .fox2). Solo cambia floats: no mueve tamanos.

Cambios (preset "alta"):
  SL_Key0000_ver03 (luz principal, lado +X): de 10 a ALTURA grados hacia abajo, apuntando al mismo punto (cruza el eje a 1.19 m),
                                             powerScale 6 -> KEY_PS (la luz llega mas de arriba: menos frente iluminado de lleno)
  SpotLight_Fill0006 (relleno desde abajo):  powerScale 2 -> FILL_PS (recupera la sombra bajo la nariz, el menton y las orbitas)
Antes de escribir se lee cada valor y se compara con el original: si no coincide (otro EditFace, otra version) no se escribe nada.

uso: python editface_luz.py <EditFace.fpkd original> <salida.fpkd> [altura_grados=30] [key_ps=5] [fill_ps=0.8]
"""
import math, struct, sys, zlib

FPK = 0x90
KEY_PS, KEY_POS, KEY_QUAT = 0xff30, 0x10270, 0x10240
FILL_PS = 0xc930
ORIG = {KEY_PS: 6.0, FILL_PS: 2.0}
ORIG_POS = (4.5, 2.1, 2.5)
ORIG_QUAT = (0.542122, 0.411595, -0.34537, 0.646075)


def rd(d, off, n=1):
    return struct.unpack_from('<%df' % n, d, FPK + off)


def wr(d, off, *v):
    struct.pack_into('<%df' % len(v), d, FPK + off, *v)


def qmat(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def mulv(m, v):
    return [sum(m[i][j] * v[j] for j in range(3)) for i in range(3)]


def qmul(a, b):   # a * b (x, y, z, w)
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def main():
    if len(sys.argv) < 3:
        print(__doc__); return 1
    src, dst = sys.argv[1], sys.argv[2]
    alt = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0
    key_ps = float(sys.argv[4]) if len(sys.argv) > 4 else 5.0
    fill_ps = float(sys.argv[5]) if len(sys.argv) > 5 else 0.8
    d = bytearray(open(src, 'rb').read())
    print('original: %u B CRC32 %08X' % (len(d), zlib.crc32(d) & 0xFFFFFFFF))
    if d[:6] != b'foxfpk':
        print('ERROR: no es un .fpkd'); return 2
    # verificacion de los valores originales
    ok = True
    for off, want in ORIG.items():
        got = rd(d, off)[0]
        if abs(got - want) > 1e-4: print('ERROR: 0x%x vale %g (esperado %g)' % (off, got, want)); ok = False
    pos = rd(d, KEY_POS, 3); quat = rd(d, KEY_QUAT, 4)
    if max(abs(a - b) for a, b in zip(pos, ORIG_POS)) > 1e-3: print('ERROR: posicion de la principal', pos); ok = False
    if max(abs(a - b) for a, b in zip(quat, ORIG_QUAT)) > 1e-4: print('ERROR: giro de la principal', quat); ok = False
    if not ok:
        print('no se escribio nada'); return 3
    # direccion del foco = -Y local (verificado: 10 grados abajo, azimut -115)
    m = qmat(quat)
    dirv = mulv(m, [0, -1, 0])
    down0 = math.degrees(math.asin(-dirv[1]))
    horiz = math.hypot(pos[0], pos[2])
    dist = horiz / math.cos(math.radians(down0))
    target = [pos[i] + dirv[i] * dist for i in range(3)]
    # giro alrededor del eje horizontal del foco (+X local) para bajar la direccion de down0 a alt grados
    ax = mulv(m, [1, 0, 0]); n = math.sqrt(sum(a * a for a in ax)); ax = [a / n for a in ax]
    best = None
    for sgn in (1, -1):
        th = math.radians(alt - down0) * sgn
        r = (ax[0] * math.sin(th / 2), ax[1] * math.sin(th / 2), ax[2] * math.sin(th / 2), math.cos(th / 2))
        q2 = qmul(r, quat)
        d2 = mulv(qmat(q2), [0, -1, 0])
        err = abs(math.degrees(math.asin(-d2[1])) - alt)
        if best is None or err < best[0]: best = (err, q2, d2)
    _, q2, d2 = best
    pos2 = [target[i] - d2[i] * dist for i in range(3)]
    print('principal: %.1f -> %.1f grados abajo, posicion (%.2f, %.2f, %.2f) -> (%.2f, %.2f, %.2f), apunta a (%.2f, %.2f, %.2f)' % (
        down0, alt, pos[0], pos[1], pos[2], pos2[0], pos2[1], pos2[2], target[0], target[1], target[2]))
    wr(d, KEY_POS, *pos2)
    wr(d, KEY_QUAT, *q2)
    wr(d, KEY_PS, key_ps)
    wr(d, FILL_PS, fill_ps)
    print('powerScale principal 6 -> %g, relleno de abajo 2 -> %g' % (key_ps, fill_ps))
    open(dst, 'wb').write(d)
    print('salida: %s %u B CRC32 %08X' % (dst, len(d), zlib.crc32(d) & 0xFFFFFFFF))
    return 0


if __name__ == '__main__':
    sys.exit(main())
