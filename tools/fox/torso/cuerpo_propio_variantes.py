# cuerpo_b (remera adentro / sin remera) y cuerpo_c (remera ajustada afuera): cuerpo_a_N con la malla 3 de pants_in_sub / pants_out_sub_tight
import sys, os, zlib, numpy as np
R="/home/user/Pes_GraphicalUpgrade/tools"; sys.path[:0]=[R+"/fox", R+"/fox/pelvis"]
from fmdl import Fmdl, meshes
from pelvis_fmdl import write_mesh_geom
A, SRC, OUT = sys.argv[1:4]
os.makedirs(OUT, exist_ok=True)
ref = meshes(Fmdl(os.path.join(SRC, "pants_out_sub.fmdl")))
for letra, fuente in (("b", "pants_in_sub"), ("c", "pants_out_sub_tight")):
    ms = meshes(Fmdl(os.path.join(SRC, fuente + ".fmdl")))
    for k in (1, 2, 3):
        for key in ("uv", "w", "idx", "faces"): assert np.array_equal(ms[k][key], ref[k][key]), (fuente, k, key)
    for k in (1, 2): assert np.array_equal(ms[k]["pos"], ref[k]["pos"]) and np.array_equal(ms[k]["nrm"], ref[k]["nrm"])
    for n in range(1, 7):
        a = os.path.join(A, "cuerpo_a_%d.fmdl" % n); dst = os.path.join(OUT, "cuerpo_%s_%d.fmdl" % (letra, n))
        write_mesh_geom(a, dst, 3, ms[3]["pos"], ms[3]["nrm"], ms[3]["tan"])
        chk = meshes(Fmdl(dst)); ca = meshes(Fmdl(a))
        assert np.abs(chk[3]["pos"] - ms[3]["pos"]).max() < 2e-5, np.abs(chk[3]["pos"] - ms[3]["pos"]).max()
        for k in (0, 1, 2):
            for key in ("pos", "nrm", "tan", "uv", "w", "faces"): assert np.array_equal(chk[k][key], ca[k][key]), (k, key)
        for key in ("uv", "w", "faces"): assert np.array_equal(chk[3][key], ca[3][key])
        print("%s  %d B  CRC %08X  nrm dif max %.4f" % (os.path.basename(dst), os.path.getsize(dst), zlib.crc32(open(dst,"rb").read()), np.abs(chk[3]["nrm"][:,:3]-ms[3]["nrm"][:,:3]).max()))
