# PES21 FX (31/10 b) - pone en el common_package.fpk de TorsoBody el neck.fmdl de Konami (neck_default.py: unido como en el juego,
# con la parte de abajo metida en el torso de ESE fpk). El fmdl cambia de tamano: fpk_replace.py rearma el fpk. Para el RC en la PC.
# Uso: python3 neck_default_fpk.py <fpk de TorsoBody (character)> <fpk con el cuello de Konami (Asset\model\#Win)> <salida.fpk> <tmp>
import sys, os, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'pelvis'), os.path.join(HERE, '..', '..', 'sincamiseta')]
from make_fpk_sin_short import entries
from fpk_replace import replace
import neck_default

def take(fpk, name, out):
    d = open(fpk, "rb").read()
    e = [x for x in entries(d) if os.path.basename(x["name"]) == name]
    if len(e) != 1: raise SystemExit("%s: %d entradas %s" % (fpk, len(e), name))
    b = d[e[0]["off"]:e[0]["off"] + e[0]["size"]]; open(out, "wb").write(b)
    return "%s (%s %s)" % (hashlib.sha256(d).hexdigest()[:8].upper(), name, hashlib.sha256(b).hexdigest()[:8].upper())

def main(src, konami, dst, tmp):
    for sub in ("torso", "konami", "out"): os.makedirs(os.path.join(tmp, sub), exist_ok=True)
    print("fuente %s" % take(src, "pants_out_sub.fmdl", os.path.join(tmp, "torso", "pants_out_sub.fmdl")))
    print("konami %s" % take(konami, "neck.fmdl", os.path.join(tmp, "konami", "neck.fmdl")))
    neck_default.main(os.path.join(tmp, "konami", "neck.fmdl"), os.path.join(tmp, "torso"), os.path.join(tmp, "out", "neck.fmdl"))
    replace(src, dst, [os.path.join(tmp, "out", "neck.fmdl")])
    print("salida %s SHA256 %s" % (dst, hashlib.sha256(open(dst, "rb").read()).hexdigest()[:8].upper()))

if __name__ == "__main__":
    main(*sys.argv[1:5])
