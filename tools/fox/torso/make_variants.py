# PES21 FX (26/10) - genera los archivos de las variantes robusto / atletico de TorsoBody:
#   textures\pes_var_<malla>.dds    datos por vertice para el VS de la piel (t127; R32G32B32A32_FLOAT, ancho = vertices de la malla,
#                                   7 filas por version del modelo: 0 posicion de reposo (para reconocerla), 1..3 robusto
#                                   (desplazamiento, normal, tangente), 4..6 atletico (igual))
#   textures\pes_var_torso_rob_n.dds / pes_var_torso_atl_n.dds   mapas normales delta del torso (BC5_SNORM, 2048 x 1024 = v 0..0.5)
# Uso: python3 make_variants.py <carpeta common de TorsoBody> <carpeta de salida> [--maps-only|--data-only]
import sys, os, json
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from torso_variants import build, build_arm, per_vertex, morph_normals
from torso_mesh import load_parts, vertex_normals, rotate_min
from texio import write_dds_float4, write_dds_bc5_snorm, write_dds_bc4_unorm
from uvbake import bake, dilate

TORSO_VERSIONS = ("out", "in", "tight", "inner")
ARMS = ("arm", "arm_cap", "arm_glove", "arm_glove_cap")

def version_rows(part, dR, nR, tR, dA, nA, tA):
    n = part["nvert"]
    rows = np.zeros((7, n, 4), np.float32)
    rows[0, :, :3] = part["pos"]; rows[0, :, 3] = 1.0
    rows[1, :, :3] = dR; rows[2, :, :3] = nR; rows[3, :, :3] = tR
    rows[4, :, :3] = dA; rows[5, :, :3] = nA; rows[6, :, :3] = tA
    return rows

def variant_rows(T):
    """filas (7, n, 4) por malla de T para su version"""
    dRp = per_vertex(T, T["dR"]); dAp = per_vertex(T, T["dA"])
    nR = morph_normals(T, T["dR"]); nA = morph_normals(T, T["dA"])
    return [version_rows(p, dRp[i], nR[i][0], nR[i][1], dAp[i], nA[i][0], nA[i][1]) for i, p in enumerate(T["parts"])]

def dirt_rows(A, folder, fn, mi):
    """capa de suciedad del brazo (otra malla, mismas posiciones): toma el desplazamiento del vertice del brazo mas cercano y
    gira sus propias normales lo mismo"""
    from scipy.spatial import cKDTree
    D = load_parts(folder, [(fn, mi)])
    p = D["parts"][0]
    arm = A["parts"][0]
    tree = cKDTree(arm["pos"]); dist, j = tree.query(p["pos"])
    iw = A["inv"][arm["off"] + j]
    n0 = vertex_normals(A["Pw"], A["Fw"])
    out = []
    for key in ("dR", "dA"):
        dW = A[key]; n1 = vertex_normals(A["Pw"] + dW, A["Fw"])
        d = dW[iw] * (dist < 0.001)[:, None]
        nn = rotate_min(n0[iw], n1[iw], p["nrm"]); nn /= np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-9)
        tt = rotate_min(n0[iw], n1[iw], p["tan"][:, :3]); tt -= nn * np.sum(tt * nn, 1, keepdims=True)
        tt /= np.maximum(np.linalg.norm(tt, axis=1, keepdims=True), 1e-9)
        out.append((d, nn, tt))
    return version_rows(p, out[0][0], out[0][1], out[0][2], out[1][0], out[1][1], out[1][2]), p["nidx"], float(dist.max())

def main():
    folder, outdir = sys.argv[1], sys.argv[2]
    mode = sys.argv[3] if len(sys.argv) > 3 else ""
    os.makedirs(outdir, exist_ok=True)
    table = []   # (cantidad de indices, archivo, versiones)
    if mode != "--maps-only":
        # ---- torso: las mallas 1 y 2 son iguales en las cuatro versiones; la 3 (abajo) cambia en pants_in_sub y pants_out_sub_tight
        per_mesh = {0: [], 1: [], 2: []}
        bases = {0: [], 1: [], 2: []}
        nidx = {}
        for ver in TORSO_VERSIONS:
            T = build(folder, ver)
            rows = variant_rows(T)
            for k in range(3):
                pos = T["parts"][k]["pos"]
                if any(np.abs(pos - b).max() < 1e-5 for b in bases[k]): continue   # version repetida
                r = rows[k]
                if per_mesh[k]:   # los vertices iguales a la primera version llevan sus mismos datos (el VS elige por vertice)
                    same = np.abs(pos - bases[k][0]).max(1) < 1e-6
                    r = r.copy(); r[1:, same] = per_mesh[k][0][1:, same]
                bases[k].append(pos); per_mesh[k].append(r); nidx[k] = T["parts"][k]["nidx"]
            print("torso %s: listo" % ver)
        for k in range(3):
            arr = np.concatenate(per_mesh[k], 0)   # (7 * versiones, n, 4)
            name = "pes_var_torso_%d.dds" % (k + 1)
            write_dds_float4(os.path.join(outdir, name), arr)
            table.append((nidx[k], name, len(per_mesh[k])))
            print("  %s: %d vertices, %d versiones, %d indices" % (name, arr.shape[1], len(per_mesh[k]), nidx[k]))
        # ---- brazos (y su capa de suciedad, si la tienen)
        for fn in ARMS:
            A = build_arm(folder, fn)
            rows = variant_rows(A)[0]
            name = "pes_var_%s.dds" % fn
            write_dds_float4(os.path.join(outdir, name), rows)
            table.append((A["parts"][0]["nidx"], name, 1))
            print("  %s: %d vertices, %d indices, max robusto %.1f mm, atletico %.1f mm" % (name, rows.shape[1], A["parts"][0]["nidx"],
                  np.abs(A["dR"]).max() * 1e3, np.abs(A["dA"]).max() * 1e3))
            from fmdl import Fmdl, meshes
            ms = meshes(Fmdl(os.path.join(folder, fn + ".fmdl")))
            if len(ms) > 1:
                drows, dn, dmax = dirt_rows(A, folder, fn, 1)
                dname = "pes_var_%s_dirt.dds" % fn
                write_dds_float4(os.path.join(outdir, dname), drows)
                table.append((dn, dname, 1))
                print("  %s: %d vertices, %d indices (suciedad; distancia maxima al brazo %.2f mm)" % (dname, drows.shape[1], dn, dmax * 1e3))
        with open(os.path.join(outdir, "pes_var_tabla.json"), "w") as f: json.dump(table, f, indent=1)
    if mode != "--data-only":
        # ---- mapas normales delta del torso
        from torso_maps import build_maps
        T = build(folder, "out")
        G = bake(T, per_vertex(T, T["fade"][:, None]), W=2048, H=1024, vmax=0.5)
        dR, dA, ms = build_maps(G, G["extra"][..., 0])
        def mipchain(a):
            mips = [a.astype(np.float32)]
            while min(mips[-1].shape[:2]) > 1:
                c = mips[-1]; mips.append(0.25 * (c[0::2, 0::2] + c[1::2, 0::2] + c[0::2, 1::2] + c[1::2, 1::2]))
            return mips
        for nm, d in (("pes_var_torso_rob_n.dds", dR), ("pes_var_torso_atl_n.dds", dA)):
            full, _ = dilate(d, G["mask"], iters=48)
            mips = mipchain(full)
            write_dds_bc5_snorm(os.path.join(outdir, nm), mips)
            print("  %s: %dx%d, %d mips, max %.2f" % (nm, d.shape[1], d.shape[0], len(mips), np.abs(d).max()))
        fullm, _ = dilate(ms[..., None], G["mask"], iters=48)
        write_dds_bc4_unorm(os.path.join(outdir, "pes_var_torso_rob_m.dds"), mipchain(fullm[..., 0]))
        print("  pes_var_torso_rob_m.dds: estrias (mascara de color), cobertura %.2f %%" % (100.0 * (ms > 0.2).mean()))
        tmpd = os.environ.get("SIM_TMP", outdir)
        np.save(os.path.join(tmpd, "maps_dR.npy"), dR.astype(np.float32)); np.save(os.path.join(tmpd, "maps_dA.npy"), dA.astype(np.float32))
        np.save(os.path.join(tmpd, "maps_mR.npy"), ms.astype(np.float32))

if __name__ == "__main__":
    sys.path.insert(0, os.path.join(HERE, '..'))
    main()
