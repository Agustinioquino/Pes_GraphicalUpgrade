# alisado suave de defectos puntuales de la espalda del torso de TorsoBody (rombo hundido de un lado y granitos)
import sys, os, shutil, numpy as np
R="/home/user/Pes_GraphicalUpgrade/tools/fox"; sys.path[:0]=[R, R+"/torso", R+"/pelvis"]
from torso_mesh import load_parts, gauss_matrix, vertex_normals, rotate_min
from pelvis_fmdl import write_mesh_geom
def ss(x): x=np.clip(x,0,1); return x*x*(3-2*x)
ZONAS = [((0.108, 1.300, -0.035), 0.030, 0.060, 0.015), ((0.000, 1.355, -0.057), 0.008, 0.022, 0.008), ((-0.062, 1.372, -0.068), 0.008, 0.022, 0.008)]
def alisar(src_dir, fn, dst):
    T = load_parts(src_dir, [(fn, i) for i in (1,2,3)])
    P, F = T["Pw"].copy(), T["Fw"]
    N0 = vertex_normals(P, F)
    fnr = np.concatenate([p["nrm"] for p in T["parts"]]); sgn = -1.0 if np.mean(np.sum(N0[T["inv"]]*fnr,1))<0 else 1.0
    mov = np.zeros(len(P))
    for c, r0, r1, sg in ZONAS:
        d = np.linalg.norm(P - np.array(c), axis=1); w = 1 - ss((d - r0)/(r1 - r0))
        if w.max() <= 0: continue
        for _ in range(3):
            G = gauss_matrix(P, F, sg); N = vertex_normals(P, F)
            r = np.sum((P - G@P)*N, 1)
            P -= (w*r)[:,None]*N
    D = P - T["Pw"]; amt = np.linalg.norm(D, axis=1)
    N1 = vertex_normals(P, F)
    tmp = dst + ".tmp"; shutil.copyfile(os.path.join(src_dir, fn + ".fmdl"), tmp)
    for p in T["parts"]:
        wd = T["inv"][p["off"]:p["off"]+p["nvert"]]
        nrm4 = np.concatenate([rotate_min(N0[wd], N1[wd], p["nrm"]), np.zeros((p["nvert"],1))], 1)
        tan = p["tan"].copy(); tan[:, :3] = rotate_min(N0[wd], N1[wd], tan[:, :3])
        write_mesh_geom(tmp, tmp, p["index"], P[wd], nrm4, tan)
    os.replace(tmp, dst)
    print("%s: movidos %d vertices, maximo %.1f mm, medio %.2f mm; y minima movida %.3f" % (fn, (amt>1e-5).sum(), amt.max()*1000, amt[amt>1e-5].mean()*1000, T["Pw"][amt>1e-5][:,1].min()))
if __name__ == "__main__":
    src, out = sys.argv[1:3]; os.makedirs(out, exist_ok=True)
    for fn in ("pants_out_sub", "pants_in_sub", "pants_out_inner_sub", "pants_out_sub_tight"): alisar(src, fn, os.path.join(out, fn + ".fmdl"))
