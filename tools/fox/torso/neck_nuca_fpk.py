# PES21 FX (31/10 b) - pone en un common_package.fpk de TorsoBody el neck.fmdl con la nuca estirada (neck_nuca.py), sacando el cuello
# actual de ese fpk y el de Konami del otro common_package (livecpk\TorsoBody\Asset\model\#Win, el de 6.6 MB). Para el RC en la PC.
# Uso: python3 neck_nuca_fpk.py <fpk de TorsoBody (character)> <fpk con el cuello de Konami> <salida.fpk> <carpeta temporal>
import sys, os, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'pelvis'), os.path.join(HERE, '..', '..', 'sincamiseta')]
from make_fpk_sin_short import entries
from patch_fpk import patch
import neck_nuca

def take(fpk, name, out):
    d = open(fpk, "rb").read()
    e = [x for x in entries(d) if os.path.basename(x["name"]) == name]
    if len(e) != 1: raise SystemExit("%s: %d entradas %s" % (fpk, len(e), name))
    open(out, "wb").write(d[e[0]["off"]:e[0]["off"] + e[0]["size"]])
    return hashlib.sha256(d).hexdigest()[:8].upper()

def main(src, konami, dst, tmp):
    os.makedirs(os.path.join(tmp, "konami"), exist_ok=True); os.makedirs(os.path.join(tmp, "out"), exist_ok=True)
    print("fuente %s SHA256 %s" % (src, take(src, "neck.fmdl", os.path.join(tmp, "neck.fmdl"))))
    print("konami %s SHA256 %s" % (konami, take(konami, "neck.fmdl", os.path.join(tmp, "konami", "neck.fmdl"))))
    neck_nuca.main(os.path.join(tmp, "neck.fmdl"), os.path.join(tmp, "konami", "neck.fmdl"), os.path.join(tmp, "out", "neck.fmdl"))
    patch(src, dst, [os.path.join(tmp, "out", "neck.fmdl")])
    print("salida %s SHA256 %s" % (dst, hashlib.sha256(open(dst, "rb").read()).hexdigest()[:8].upper()))

if __name__ == "__main__":
    main(*sys.argv[1:5])
