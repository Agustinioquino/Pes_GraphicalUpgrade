# PES21 FX (26/10 d) - SinCamiseta 1.2: common_package.fpk "sin short".
# Las piezas del cuerpo (torso pants_*_sub, brazos, muslos, manos...) NO las pide el juego sueltas: vienen dentro de
# Asset\model\character\#Win\common_package.fpk (el del livecpk "Global Model Efootball 5.0", que arma el instalador de
# TorsoBody) y se carga UNA vez, al abrir el juego. Por eso cambiar pants_*_sub.fmdl en Sider (SinCamiseta 1.1) no hacia nada.
# Este script copia el fpk del usuario y cambia adentro las 4 entradas pants_*_sub.fmdl por las pelvis de piel
# (tools/fox/pelvis/make_pelvis.py): mismo tamano, en el mismo lugar del archivo (el resto queda byte a byte igual; el md5 de
# cada entrada es el del NOMBRE, no cambia). SinCamiseta sirve la copia cuando el short esta oculto (hace falta reiniciar).
# El resultado tiene modelos de TorsoBody / Konami: no se sube al repo, se arma en la PC del usuario con su propio fpk.
# Uso: python3 make_fpk_sin_short.py <common_package.fpk original> <carpeta con las 4 pelvis> <salida.fpk>
import sys, struct, hashlib

NAMES = ("pants_out_sub.fmdl", "pants_in_sub.fmdl", "pants_out_inner_sub.fmdl", "pants_out_sub_tight.fmdl")

def entries(d):
    assert d[:6] == b"foxfpk", "no es un fpk"
    size, = struct.unpack_from("<I", d, 10)
    assert size == len(d), "tamano del fpk distinto del de la cabecera (%d / %d)" % (size, len(d))
    magic, count, refs = struct.unpack_from("<III", d, 32)
    out = []
    for i in range(count):
        o = 48 + 48 * i
        doff, _, dsz, _, noff, _, nlen, _ = struct.unpack_from("<IIIIIIII", d, o)
        name = d[noff:noff + nlen].decode("ascii")
        assert hashlib.md5(name.encode("ascii")).digest() == d[o + 32:o + 48], "md5 del nombre distinto en %s" % name
        out.append(dict(name=name, off=doff, size=dsz))
    return out

def build(src, peldir, dst, log=print):
    d = bytearray(open(src, "rb").read())
    ents = entries(d)
    done = []
    for nm in NAMES:
        e = [x for x in ents if x["name"].endswith("/" + nm)]
        if len(e) != 1: raise SystemExit("el fpk no tiene %s (o lo tiene repetido)" % nm)
        e = e[0]
        blob = open(peldir.rstrip("/\\") + "/" + nm, "rb").read()
        if len(blob) != e["size"]:
            raise SystemExit("%s: la pelvis mide %d y la entrada del fpk %d (el fpk no es el de TorsoBody v11 / v12.6?)" % (nm, len(blob), e["size"]))
        old = bytes(d[e["off"]:e["off"] + e["size"]])
        if old[:4] != blob[:4]: raise SystemExit("%s: no empieza como un fmdl" % nm)
        d[e["off"]:e["off"] + e["size"]] = blob
        done.append("%s (%d bytes, %s)" % (nm, len(blob), "cambiado" if old != blob else "igual"))
    open(dst, "wb").write(bytes(d))
    log("fpk sin short: " + "; ".join(done))
    return done

if __name__ == "__main__":
    build(sys.argv[1], sys.argv[2], sys.argv[3])
