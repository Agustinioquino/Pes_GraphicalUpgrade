# PES21 FX (16/11) - CUERPO PROPIO: pasa la geometria (posicion, normal, tangente) de las mallas 1..3 de los pants_*_sub de una
# carpeta (ya alisados / achicados) a los cuerpo_<letra>_<tono>.fmdl existentes, y tambien sus pesos (torso_pesos.py).
# Mismos vertices, indices y UV.
#   a = pants_out_sub (remera afuera), b = pants_in_sub (adentro / sin remera), c = pants_out_sub_tight (ajustada)
# Uso: python3 cuerpo_propio_geom.py <carpeta con cuerpo_a_1..6 [b, c]> <carpeta con pants_*_sub.fmdl> <salida>
import sys, os, zlib, shutil
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'pelvis')]
from fmdl import Fmdl, meshes
from pelvis_fmdl import write_mesh_geom
from torso_pesos import write_mesh_weights
FUENTE = {"a": "pants_out_sub", "b": "pants_in_sub", "c": "pants_out_sub_tight"}

def main(cuerpos, fuente, out):
    os.makedirs(out, exist_ok=True)
    for letra, fn in FUENTE.items():
        FF = Fmdl(os.path.join(fuente, fn + ".fmdl")); ms = meshes(FF)
        for n in range(1, 7):
            src = os.path.join(cuerpos, "cuerpo_%s_%d.fmdl" % (letra, n))
            if not os.path.exists(src): print("falta", src); continue
            dst = os.path.join(out, os.path.basename(src)); shutil.copyfile(src, dst)
            FA = Fmdl(src); antes = meshes(FA); pes = 0
            nomA = [b["name"] for b in FA.bones]; nomF = [b["name"] for b in FF.bones]
            aA = np.array([nomA.index(x) if x in nomA else -1 for x in nomF])      # hueso de la fuente -> hueso del cuerpo (por nombre)
            for k in (1, 2, 3):
                for key in ("uv", "faces"): assert np.array_equal(antes[k][key], ms[k][key]), (fn, k, key)
                write_mesh_geom(dst, dst, k, ms[k]["pos"], ms[k]["nrm"], ms[k]["tan"])
                loc = {g: i for i, g in enumerate(FA.groups[antes[k]["group"]])}
                gi = aA[ms[k]["idx"]]; assert (gi >= 0).all()
                write_mesh_weights(dst, dst, k, np.round(ms[k]["w"] * 255).astype(np.uint8), np.array([[loc[int(g)] for g in r] for r in gi], np.uint8))
                pes += int(((np.abs(antes[k]["w"] - ms[k]["w"]).max(1) > 1e-4) | (antes[k]["idx"] != gi).any(1)).sum())
            chk = meshes(Fmdl(dst)); mov = 0.0
            for k in (0, 1, 2, 3):
                for key in ("uv", "faces"): assert np.array_equal(chk[k][key], antes[k][key]), (k, key)
                if k: assert np.array_equal(chk[k]["w"], ms[k]["w"]) and np.array_equal(chk[k]["idx"], aA[ms[k]["idx"]])
                else: assert np.array_equal(chk[0]["w"], antes[0]["w"]) and np.array_equal(chk[0]["idx"], antes[0]["idx"])
                if k: assert np.abs(chk[k]["pos"] - ms[k]["pos"]).max() < 2e-5; mov = max(mov, np.abs(chk[k]["pos"] - antes[k]["pos"]).max())
                else: assert np.array_equal(chk[0]["pos"], antes[0]["pos"])
            print("%s  %d B  CRC %08X  cambio maximo %.1f mm, vertices con pesos nuevos %d" % (os.path.basename(dst), os.path.getsize(dst), zlib.crc32(open(dst, "rb").read()), mov * 1000, pes))

if __name__ == "__main__":
    main(*sys.argv[1:4])
