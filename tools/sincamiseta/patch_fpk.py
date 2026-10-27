# PES21 FX (26/10 h) - cambia entradas de un common_package.fpk por archivos del mismo tamano (en el mismo lugar; el resto del
# fpk queda byte a byte igual). Para el torso con el cuello de Konami (pants_*_sub.fmdl + neck.fmdl) en el fpk del Global Model y
# en el "sin short" de SinCamiseta. El resultado tiene modelos de TorsoBody / Konami: se arma en la PC del usuario, no va al repo.
# Uso: python3 patch_fpk.py <fpk original> <salida.fpk> <archivo> [<archivo> ...]   (cada archivo reemplaza la entrada de su nombre)
import sys, os
from make_fpk_sin_short import entries

def patch(src, dst, files, log=print):
    d = bytearray(open(src, "rb").read())
    ents = entries(d)
    for f in files:
        nm = os.path.basename(f)
        e = [x for x in ents if x["name"].endswith("/" + nm)]
        if len(e) != 1: raise SystemExit("el fpk no tiene %s (o lo tiene repetido)" % nm)
        e = e[0]; blob = open(f, "rb").read()
        if len(blob) != e["size"]: raise SystemExit("%s: mide %d y la entrada %d" % (nm, len(blob), e["size"]))
        d[e["off"]:e["off"] + e["size"]] = blob
        log("  %s reemplazado" % nm)
    open(dst, "wb").write(bytes(d))

if __name__ == "__main__":
    patch(sys.argv[1], sys.argv[2], sys.argv[3:])
