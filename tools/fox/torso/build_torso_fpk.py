# PES21 FX (31/10 b) - arma el common_package.fpk de TorsoBody con el torso achicado (torso_shrink.py) desde el fpk instalado:
# saca los .fmdl del torso y de los brazos, los achica y los vuelve a poner (mismo tamano, patch_fpk.py). Para el RC en la PC
# (los modelos de TorsoBody / Konami no van al repo). Necesita numpy y scipy.
# Uso: python3 build_torso_fpk.py <fpk fuente (93FCA0CF)> <salida.fpk> <carpeta temporal> [MM pecho] [MMX axila] [MMA brazo] [MMB espalda]
import sys, os, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'pelvis'), os.path.join(HERE, '..', '..', 'sincamiseta')]
from make_fpk_sin_short import entries
from patch_fpk import patch
import torso_shrink

def main(src, dst, tmp, *mm):
    d = open(src, "rb").read()
    print("fuente %s SHA256 %s" % (src, hashlib.sha256(d).hexdigest()[:8].upper()))
    os.makedirs(os.path.join(tmp, "in"), exist_ok=True)
    names = torso_shrink.TORSO + torso_shrink.ARMS
    for e in entries(d):
        n = os.path.basename(e["name"])
        if n[:-5] in names and n.endswith(".fmdl"): open(os.path.join(tmp, "in", n), "wb").write(d[e["off"]:e["off"] + e["size"]])
    torso_shrink.main(os.path.join(tmp, "in"), os.path.join(tmp, "out"), *(float(x) for x in mm))
    outs = [os.path.join(tmp, "out", f) for f in sorted(os.listdir(os.path.join(tmp, "out"))) if f.endswith(".fmdl") and not f.startswith("arm_half")]
    patch(src, dst, outs)
    print("salida %s SHA256 %s" % (dst, hashlib.sha256(open(dst, "rb").read()).hexdigest()[:8].upper()))

if __name__ == "__main__":
    main(*sys.argv[1:])
