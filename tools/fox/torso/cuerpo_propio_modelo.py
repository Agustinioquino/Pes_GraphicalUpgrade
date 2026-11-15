# PES21 FX (15/11) - CUERPO PROPIO, paso 2: el torso de TorsoBody como modelo suelto por tono de piel, fuera del common package.
# De pants_out_sub.fmdl (common_package.fpk instalado) sale cuerpo_a_1..6.fmdl:
#   - el short (malla 0) se junta en un punto: no se dibuja (el short lo sigue poniendo el juego)
#   - torso_mat recibe Base_Tex_SRGB = torso_N_bsm de la carpeta comun (el juego NO le pone la piel a una pieza por ruta)
# Los archivos van a un livecpk (Asset\model\character\cuerpo\#Win\) y los pide el modulo de Sider CuerpoPropio con la ruta
# /Assets/pes16/model/character/cuerpo/cuerpo_a_#.fmdl. Los modelos de TorsoBody / Konami no van al repo: se arman con esto.
# Uso: python3 cuerpo_propio_modelo.py <common_package.fpk> <carpeta de salida> [nombre=cuerpo_a] [fuente=pants_out_sub]
import sys, os, zlib
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'pelvis'), os.path.join(HERE, '..', '..', 'sincamiseta')]
from fmdl import Fmdl, meshes
from fmdl_edit import FmdlEdit
from pelvis_fmdl import write_mesh_geom
from make_fpk_sin_short import entries

CARPETA = "/Assets/pes16/model/character/common/sourceimages/"
PUNTO = np.array([0.0, 1.0, 0.0], np.float32)

def main(fpk, out, nombre="cuerpo_a", fuente="pants_out_sub"):
    d = open(fpk, "rb").read()
    print("fuente %s CRC %08X" % (fpk, zlib.crc32(d)))
    os.makedirs(out, exist_ok=True)
    src = os.path.join(out, "_" + fuente + ".fmdl"); tmp = os.path.join(out, "_sin_short.fmdl")
    e = [e for e in entries(d) if os.path.basename(e["name"]) == fuente + ".fmdl"][0]
    open(src, "wb").write(d[e["off"]:e["off"] + e["size"]])
    ms = meshes(Fmdl(src))
    assert ms[0]["pos"][:, 1].max() < 1.15, "la malla 0 no parece el short"
    write_mesh_geom(src, tmp, 0, np.tile(PUNTO, (ms[0]["nvert"], 1)), ms[0]["nrm"], ms[0]["tan"])
    for n in range(1, 7):
        f = FmdlEdit(tmp)
        assert f.instancias()[1] == "torso_mat" and all(m["mat"] == 1 for m in ms[1:]), "materiales no previstos"
        f.poner_textura("torso_mat", "Base_Tex_SRGB", "torso_%d_bsm.tga" % n, CARPETA)
        dst = os.path.join(out, "%s_%d.fmdl" % (nombre, n)); f.save(dst)
        # comprobacion: se vuelve a leer con el lector de siempre
        g = FmdlEdit(dst); chk = meshes(Fmdl(dst))
        assert g.texturas(1)[0] == ("Base_Tex_SRGB", CARPETA + "torso_%d_bsm.tga" % n) and len(g.texturas(1)) == 4
        assert g.texturas(0) == FmdlEdit(src).texturas(0) and len(chk) == len(ms)
        for k in range(1, len(ms)):
            for key in ("pos", "nrm", "tan", "uv", "w", "idx", "faces"): assert np.array_equal(chk[k][key], ms[k][key]), (k, key)
        assert np.abs(chk[0]["pos"] - PUNTO).max() < 1e-6
        assert [b["name"] for b in Fmdl(dst).bones] == [b["name"] for b in Fmdl(src).bones]
        print("  %s  %d B  CRC %08X  torso_mat: %s" % (os.path.basename(dst), os.path.getsize(dst), zlib.crc32(open(dst, "rb").read()),
                                                      ", ".join(t[0] for t in g.texturas(1))))
    os.remove(src); os.remove(tmp)

if __name__ == "__main__":
    main(*sys.argv[1:])
