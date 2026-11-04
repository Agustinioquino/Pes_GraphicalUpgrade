# PES21 FX (31/10 b) - cambia entradas de un fpk por archivos de OTRO tamano (patch_fpk.py solo acepta el mismo tamano): los datos
# van seguidos y alineados a 16 despues de los nombres; se rearma corriendo los que vienen despues y la cabecera (tamano total).
# Uso: python3 fpk_replace.py <fpk> <salida.fpk> <archivo> [<archivo> ...]   (cada archivo reemplaza la entrada de su nombre)
import sys, os, struct
from make_fpk_sin_short import entries

def replace(src, dst, files, log=print):
    d = bytearray(open(src, "rb").read())
    for f in files:
        nm = os.path.basename(f); blob = open(f, "rb").read()
        ents = entries(d)
        idx = [i for i, x in enumerate(ents) if x["name"].endswith("/" + nm)]
        if len(idx) != 1: raise SystemExit("el fpk no tiene %s (o lo tiene repetido)" % nm)
        e = ents[idx[0]]
        later = sorted(x["off"] for x in ents if x["off"] > e["off"])
        nxt = later[0] if later else len(d)
        newlen = (len(blob) + 15) // 16 * 16 if later else len(blob)
        delta = (e["off"] + newlen) - nxt
        d = d[:e["off"]] + blob + bytes(newlen - len(blob)) + d[nxt:]
        count, = struct.unpack_from("<I", d, 36)
        for i in range(count):
            o = 48 + 48 * i
            doff, dsz = struct.unpack_from("<I", d, o)[0], struct.unpack_from("<I", d, o + 8)[0]
            if doff == e["off"]: struct.pack_into("<I", d, o + 8, len(blob))
            elif doff > e["off"]: struct.pack_into("<I", d, o, doff + delta)
        struct.pack_into("<I", d, 10, len(d))
        log("  %s reemplazado (%d -> %d bytes)" % (nm, e["size"], len(blob)))
    entries(d)   # comprueba cabecera y nombres
    open(dst, "wb").write(bytes(d))

if __name__ == "__main__":
    replace(sys.argv[1], sys.argv[2], sys.argv[3:])
