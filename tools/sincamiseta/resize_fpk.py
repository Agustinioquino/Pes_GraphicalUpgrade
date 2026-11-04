# PES21 FX (04/11) - cambia UNA entrada de un common_package.fpk por un archivo de OTRO tamano: lo de antes queda byte a byte
# igual (mismos offsets: p. ej. mouth.fmdl de la boca, que va antes), y lo de despues se corre (alineado a 16) con sus offsets
# actualizados en la tabla y el tamano total en la cabecera. Para la pulsera (wristtaping_l.fmdl, tools/fox/pulsera).
# Uso: python3 resize_fpk.py <fpk original> <salida.fpk> <archivo>   (reemplaza la entrada con el nombre del archivo)
import sys, os, struct, hashlib
from make_fpk_sin_short import entries

def resize(src, dst, f, log=print):
    d = bytearray(open(src, "rb").read())
    ents = entries(d)
    nm = os.path.basename(f); blob = open(f, "rb").read()
    e = [x for x in ents if x["name"].endswith("/" + nm)]
    if len(e) != 1: raise SystemExit("el fpk no tiene %s (o lo tiene repetido)" % nm)
    e = e[0]
    _, count, _ = struct.unpack_from("<III", d, 32)
    tail_start = e["off"] + e["size"]; tail_start += -tail_start % 16
    pad = lambda b: b + b"\0" * (-len(b) % 16)
    new = bytes(d[:e["off"]]) + pad(blob)
    delta = len(new) - tail_start
    out = bytearray(new + bytes(d[tail_start:]))
    for i in range(count):
        o = 48 + 48 * i
        doff, _, dsz = struct.unpack_from("<III", out, o)
        if doff == e["off"]: struct.pack_into("<I", out, o + 8, len(blob))
        elif doff > e["off"]: struct.pack_into("<I", out, o, doff + delta)
    struct.pack_into("<I", out, 10, len(out))
    open(dst, "wb").write(bytes(out))
    # control: todo lo demas igual
    a = {x["name"]: bytes(d[x["off"]:x["off"] + x["size"]]) for x in ents}
    b = {x["name"]: bytes(out[x["off"]:x["off"] + x["size"]]) for x in entries(out)}
    diff = [k for k in a if a[k] != b[k]]
    log("%s: %d -> %d bytes; entradas distintas: %s" % (nm, e["size"], len(blob), diff))
    return diff

if __name__ == "__main__":
    resize(sys.argv[1], sys.argv[2], sys.argv[3])
