# PES21 FX (15/11) - OCULTAR el torso de TorsoBody en un tipo de remera, por modelo (sirve sin shader, sin fisica y sin DLL).
# El juego elige el archivo del cuerpo segun la remera: pants_in_sub (remera adentro del pantalon), pants_out_sub (afuera),
# pants_out_sub_tight (ajustada), pants_out_inner_sub (con camiseta interior). Agustin (15/11): el torso solo hace falta con la
# remera afuera y con la ajustada; con la remera adentro nunca se ve y solo puede atravesar la tela.
# Las mallas del torso (1..3) del archivo pedido se juntan en UN punto dentro del pecho: los triangulos quedan sin area y no se
# dibujan. Mismo archivo, mismo tamano, mismas mallas y huesos: se reemplaza la entrada en el fpk (patch_fpk.py) y se vuelve atras
# con el respaldo. El short (malla 0) no se toca.
# Uso: python3 torso_ocultar.py <common_package.fpk fuente> <salida.fpk> <carpeta temporal> [archivo ...]   (por defecto pants_in_sub)
import sys, os, zlib
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'pelvis'), os.path.join(HERE, '..', '..', 'sincamiseta')]
from fmdl import Fmdl, meshes
from pelvis_fmdl import write_mesh_geom
from make_fpk_sin_short import entries
from patch_fpk import patch

PUNTO = np.array([0.0, 1.35, 0.03], np.float32)   # dentro del pecho
TORSO_MESHES = (1, 2, 3)

def ocultar(src, dst):
    ms = meshes(Fmdl(src))
    cur = src
    for k in TORSO_MESHES:
        m = ms[k]
        assert m["pos"][:, 1].min() > 0.95, "la malla %d no parece del torso" % k
        pos = np.tile(PUNTO, (m["nvert"], 1))
        write_mesh_geom(cur, dst, k, pos, m["nrm"], m["tan"])
        cur = dst
    chk = meshes(Fmdl(dst))
    for k, m in enumerate(chk):
        if k in TORSO_MESHES: assert np.abs(m["pos"] - PUNTO).max() < 1e-6
        else: assert np.array_equal(m["pos"], ms[k]["pos"]), "se movio la malla %d" % k
    assert os.path.getsize(dst) == os.path.getsize(src)

def main(src, dst, tmp, *names):
    names = names or ("pants_in_sub",)
    d = open(src, "rb").read()
    print("fuente %s CRC %08X" % (src, zlib.crc32(d)))
    os.makedirs(os.path.join(tmp, "in"), exist_ok=True); os.makedirs(os.path.join(tmp, "out"), exist_ok=True)
    outs = []
    for e in entries(d):
        n = os.path.basename(e["name"])
        if n.endswith(".fmdl") and n[:-5] in names:
            a = os.path.join(tmp, "in", n); b = os.path.join(tmp, "out", n)
            open(a, "wb").write(d[e["off"]:e["off"] + e["size"]])
            ocultar(a, b); outs.append(b); print("  %s: torso oculto" % n)
    assert len(outs) == len(names), "faltan archivos en el fpk"
    patch(src, dst, outs)
    print("salida %s CRC %08X" % (dst, zlib.crc32(open(dst, "rb").read())))

if __name__ == "__main__":
    main(*sys.argv[1:])
