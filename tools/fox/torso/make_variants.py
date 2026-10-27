# PES21 FX (26/10) - genera los archivos de las variantes robusto / atletico de TorsoBody:
#   textures\pes_var_<malla>.dds    datos por vertice para el VS de la piel (t127; R32G32B32A32_FLOAT, ancho = vertices de la malla,
#                                   7 filas por version del modelo: 0 posicion de reposo (para reconocerla), 1..3 robusto
#                                   (desplazamiento, normal, tangente), 4..6 atletico (igual))
#   textures\pes_var_torso_rob_n.dds / pes_var_torso_atl_n.dds   mapas normales delta del torso (BC5_SNORM, 2048 x 1024 = v 0..0.5)
#   textures\pes_var_torso_spine_n.dds   (26/10) la columna marcada, para todos los torsos (mismo formato; PS t19, x288)
#   textures\pes_var_torso_base_n.dds    (26/10 b) la espalda suave de todos los torsos (torso_base.py; PS t20, x289)
#   textures\pes_var_torso_skin_a/b.dds  (26/10 b) piel del torso por zona (torso_skin.py; PS t21 / t24, x293)
#   textures\pes_var_arm_skin_a/b.dds    (26/10 c) lo mismo en los brazos (todo el atlas del brazo, 2048 x 2048)
#   textures\pes_var_torso_tilt_b/a/r.dds, pes_var_arm_tilt_b/a/r.dds   (26/10 d) inclinacion de la malla de cada version (base,
#                                   atletico, robusto) para el tope del relieve del PS (torso_tilt.py; t5 / t6 / t7, x307)
# Uso: python3 make_variants.py <carpeta common de TorsoBody> <carpeta de salida> [--maps-only|--data-only|--spine-only]
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

ROWS = 10   # (26/10 b) por version: 0 reposo, 1..3 robusto, 4..6 atletico, 7..9 base (desplazamiento, normal, tangente;
            # absolutos: el VS mezcla base -> variante con |t|)

def mipchain(a):
    mips = [a.astype(np.float32)]
    while min(mips[-1].shape[:2]) > 1:
        c = mips[-1]; mips.append(0.25 * (c[0::2, 0::2] + c[1::2, 0::2] + c[0::2, 1::2] + c[1::2, 1::2]))
    return mips

def version_rows(part, dR, nR, tR, dA, nA, tA, dB=None, nB=None, tB=None):
    n = part["nvert"]
    rows = np.zeros((ROWS, n, 4), np.float32)
    rows[0, :, :3] = part["pos"]; rows[0, :, 3] = 1.0
    rows[1, :, :3] = dR; rows[2, :, :3] = nR; rows[3, :, :3] = tR
    rows[4, :, :3] = dA; rows[5, :, :3] = nA; rows[6, :, :3] = tA
    if dB is None: dB, nB, tB = np.zeros((n, 3)), part["nrm"], part["tan"][:, :3]
    rows[7, :, :3] = dB; rows[8, :, :3] = nB; rows[9, :, :3] = tB
    return rows

def variant_rows(T):
    """filas (ROWS, n, 4) por malla de T para su version"""
    dRp = per_vertex(T, T["dR"]); dAp = per_vertex(T, T["dA"])
    nR = morph_normals(T, T["dR"]); nA = morph_normals(T, T["dA"])
    dB = T.get("dB")
    if dB is None: dB = np.zeros_like(T["dR"])
    dBp = per_vertex(T, dB); nB = morph_normals(T, dB)
    return [version_rows(p, dRp[i], nR[i][0], nR[i][1], dAp[i], nA[i][0], nA[i][1], dBp[i], nB[i][0], nB[i][1]) for i, p in enumerate(T["parts"])]

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
    for key in ("dR", "dA", "dB"):
        dW = A.get(key)
        if dW is None: dW = np.zeros_like(A["Pw"])
        n1 = vertex_normals(A["Pw"] + dW, A["Fw"])
        d = dW[iw] * (dist < 0.001)[:, None]
        nn = rotate_min(n0[iw], n1[iw], p["nrm"]); nn /= np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-9)
        tt = rotate_min(n0[iw], n1[iw], p["tan"][:, :3]); tt -= nn * np.sum(tt * nn, 1, keepdims=True)
        tt /= np.maximum(np.linalg.norm(tt, axis=1, keepdims=True), 1e-9)
        out.append((d, nn, tt))
    return version_rows(p, out[0][0], out[0][1], out[0][2], out[1][0], out[1][1], out[1][2], out[2][0], out[2][1], out[2][2]), p["nidx"], float(dist.max())

def join_shoulder(torso_rows, arm_rows_all, log=print):
    """(26/10 k) costura del hombro en las tres formas (base, atletico, gordo): en los vertices que el torso comparte con el brazo,
    el mismo desplazamiento y la MISMA normal de los dos lados (el promedio); cada lado endereza su tangente contra esa normal.
    torso_rows: lista de arreglos (ROWS * versiones, n, 4) de las mallas del torso; arm_rows_all: lista de (ROWS, n, 4) de cada brazo"""
    from scipy.spatial import cKDTree
    ref = arm_rows_all[0]
    tree = cKDTree(ref[0, :, :3])
    nj = 0
    for arr in torso_rows:
        nver = arr.shape[0] // ROWS
        for v in range(nver):
            R = arr[v * ROWS:(v + 1) * ROWS]
            d, j = tree.query(R[0, :, :3]); it = np.where(d < 1e-4)[0]; ja = j[it]
            if not len(it): continue
            for dr, nr, tr in ((1, 2, 3), (4, 5, 6), (7, 8, 9)):
                disp = 0.5 * (R[dr, it, :3] + ref[dr, ja, :3])
                nn = R[nr, it, :3] + ref[nr, ja, :3]; nn /= np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-9)
                R[dr, it, :3] = disp; R[nr, it, :3] = nn
                t = R[tr, it, :3]; t = t - nn * np.sum(t * nn, 1, keepdims=True); R[tr, it, :3] = t / np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-9)
                for A in arm_rows_all:   # (27/10) TODOS los vertices del brazo en ese punto (los cortes de uv del brazo lo repiten)
                    ta_tree = cKDTree(A[0, :, :3])
                    for q, lst in enumerate(ta_tree.query_ball_point(R[0, it, :3], 1e-4)):
                        if not lst: continue
                        ka = np.array(lst)
                        A[dr, ka, :3] = disp[q]; A[nr, ka, :3] = nn[q]
                        ta = A[tr, ka, :3]; ta = ta - nn[q] * np.sum(ta * nn[q], 1, keepdims=True)
                        A[tr, ka, :3] = ta / np.maximum(np.linalg.norm(ta, axis=1, keepdims=True), 1e-9)
            nj += len(it)
    log("  hombro: %d vertices del torso con la misma normal que el brazo (base, atletico, gordo)" % nj)

def main():
    folder, outdir = sys.argv[1], sys.argv[2]
    mode = sys.argv[3] if len(sys.argv) > 3 else ""
    os.makedirs(outdir, exist_ok=True)
    table = []   # (cantidad de indices, archivo, versiones)
    if mode in ("", "--data-only"):
        # ---- torso: las mallas 1 y 2 son iguales en las cuatro versiones; la 3 (abajo) cambia en pants_in_sub y pants_out_sub_tight
        per_mesh = {0: [], 1: [], 2: []}
        bases = {0: [], 1: [], 2: []}
        nidx = {}
        texdir = os.environ.get("TB_TEX", os.path.join(folder, "..", "tex"))
        import torso_tilt
        # (26/10 e) TB_SINSHORT = carpeta con los pants_*_sub.fmdl "sin short" (make_pelvis.py): la parte de abajo del torso tiene
        # otra forma (la de la pelvis), asi que son versiones mas del modelo (el VS elige por vertice la version que coincide)
        sinshort = os.environ.get("TB_SINSHORT")
        srcs = [(folder, ver) for ver in TORSO_VERSIONS] + ([(sinshort, ver) for ver in TORSO_VERSIONS] if sinshort else [])
        for src_folder, ver in srcs:
            # (26/10 e) TB_NO_RELIEF=1: el relieve del normal map NO pasa a la malla del torso (texturas propias pintadas: no son
            # integrables y la malla quedaba con bultos); la malla queda la de TorsoBody (+ lo esculpido a mano de las variantes)
            T = build(src_folder, ver, None if os.environ.get("TB_NO_RELIEF") else texdir)
            rows = variant_rows(T)
            if ver == "out" and src_folder == folder:   # (26/10 d) inclinacion de la malla de cada version para el tope del relieve del PS (x307)
                tm, tmask = torso_tilt.tilt_maps(T, rows, 1024, 512, 0.5)
                print("  pes_var_torso_tilt_b/a/r.dds: " + torso_tilt.write_tilt(outdir, "pes_var_torso", tm, tmask, write_dds_bc5_snorm, mipchain))
            for k in range(3):
                pos = T["parts"][k]["pos"]
                if any(np.abs(pos - b).max() < 1e-5 for b in bases[k]): continue   # version repetida
                r = rows[k]
                if per_mesh[k]:   # los vertices iguales a la primera version llevan sus mismos datos (el VS elige por vertice)
                    same = np.abs(pos - bases[k][0]).max(1) < 1e-6
                    r = r.copy(); r[1:, same] = per_mesh[k][0][1:, same]
                bases[k].append(pos); per_mesh[k].append(r); nidx[k] = T["parts"][k]["nidx"]
            print("torso %s%s: listo" % (ver, " (sin short)" if src_folder != folder else ""))
        torso_arrs = [np.concatenate(per_mesh[k], 0) for k in range(3)]   # (ROWS * versiones, n, 4)
        # ---- brazos (y su capa de suciedad, si la tienen)
        arm_built = [(fn, build_arm(folder, fn, texdir=texdir)) for fn in ARMS]
        arm_rows = [variant_rows(A)[0] for _, A in arm_built]
        join_shoulder(torso_arrs[:2], arm_rows)
        for k in range(3):
            arr = torso_arrs[k]
            name = "pes_var_torso_%d.dds" % (k + 1)
            write_dds_float4(os.path.join(outdir, name), arr)
            table.append((nidx[k], name, len(per_mesh[k])))
            print("  %s: %d vertices, %d versiones, %d indices" % (name, arr.shape[1], len(per_mesh[k]), nidx[k]))
        for (fn, A), rows in zip(arm_built, arm_rows):
            if fn == "arm":    # (26/10 d) la del brazo (las otras mallas del brazo usan el mismo atlas)
                tm, tmask = torso_tilt.tilt_maps(A, [rows], 1024, 1024, 1.0)
                print("  pes_var_arm_tilt_b/a/r.dds: " + torso_tilt.write_tilt(outdir, "pes_var_arm", tm, tmask, write_dds_bc5_snorm, mipchain))
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
    tmpd = os.environ.get("SIM_TMP", outdir)
    if mode in ("", "--maps-only", "--spine-only"):
        # ---- (26/10) columna marcada (todos los torsos)
        from torso_maps import build_spine_map
        T = build(folder, "out")
        G = bake(T, per_vertex(T, T["fade"][:, None]), W=2048, H=1024, vmax=0.5)
        dS = build_spine_map(G)
        full, _ = dilate(dS, G["mask"], iters=48)
        write_dds_bc5_snorm(os.path.join(outdir, "pes_var_torso_spine_n.dds"), mipchain(full))
        np.save(os.path.join(tmpd, "maps_dS.npy"), dS.astype(np.float32))
        print("  pes_var_torso_spine_n.dds: %dx%d, max %.2f" % (dS.shape[1], dS.shape[0], np.abs(dS).max()))
    if mode in ("", "--maps-only"):
        # ---- mapas normales delta del torso
        from torso_maps import build_maps
        import torso_base
        dR, dA, ms = build_maps(G, G["extra"][..., 0])
        # (26/10 b) espalda: la base suave para todos (pes_var_torso_base_n.dds, PS t20) y lo de cada variante encima
        texdir = os.environ.get("TB_TEX", os.path.join(folder, "..", "tex"))
        Bd = torso_base.bands(G, texdir)
        dB, dRb, dAb = torso_base.region_deltas(Bd, G["P"], G["mask"], G["extra"][..., 0])
        # (26/10 e) costuras: el mismo micro relieve (3D) a los dos lados del hombro, y mas suave junto al cuello (torso_seam.py)
        import torso_seam, skin_detail
        from torso_variants import build_arm as _ba, ARM_KG
        from scipy.ndimage import gaussian_filter
        Aa = _ba(folder, "arm")
        Ga = bake(Aa, per_vertex(Aa, Aa["fade"][:, None]), W=2048, H=2048, vmax=1.0)
        Ba = torso_base.bands(Ga, texdir, fname="arm_nrm.ftex")
        dArm = -ARM_KG[0] * Ga["extra"][..., 0:1] * Ba["vol"] * Ga["mask"][..., None]
        # (la textura de detalle sale de la piel de TorsoBody; con un normal map propio sin poros, TB_TEX_DETAIL = carpeta con el original)
        tile = skin_detail.build(folder, os.environ.get("TB_TEX_DETAIL", texdir), outdir)["d_n"]
        tile = np.stack([gaussian_filter(tile[..., c], 0.7, mode="wrap") for c in range(2)], -1)
        ring = [torso_seam.shoulder_ring(T, Aa)]
        amp = 0.5 * (torso_seam.band_amp(G, Bd, dB, ring) + torso_seam.band_amp(Ga, Ba, dArm, ring))
        print("  costura del hombro: micro a 4..7 cm, torso %s, brazo %s -> franja %s" % (np.round(torso_seam.band_amp(G, Bd, dB, ring), 3), np.round(torso_seam.band_amp(Ga, Ba, dArm, ring), 3), np.round(amp, 3)))
        dB, wSeam = torso_seam.stitch(G, Bd, dB, ring, tile, amp=amp, neck=torso_seam.neck_ring(T))
        dArm, wSeamA = torso_seam.stitch(Ga, Ba, dArm, ring, tile, amp=amp)
        dRb *= (1.0 - wSeam)[..., None]; dAb *= (1.0 - wSeam)[..., None]
        dR = np.clip(dR * (1.0 - wSeam)[..., None] + dRb, -0.9, 0.9); dA = np.clip(dA * (1.0 - wSeam)[..., None] + dAb, -0.9, 0.9)
        fullB, _ = dilate(np.clip(dB, -0.9, 0.9), G["mask"], iters=48)
        write_dds_bc5_snorm(os.path.join(outdir, "pes_var_torso_base_n.dds"), mipchain(fullB))
        np.save(os.path.join(tmpd, "maps_dB.npy"), dB.astype(np.float32))
        print("  pes_var_torso_base_n.dds: correccion de la base (espalda suave, volumen en la malla) (max %.2f)" % np.abs(dB).max())
        for nm, d in (("pes_var_torso_rob_n.dds", dR), ("pes_var_torso_atl_n.dds", dA)):
            full, _ = dilate(d, G["mask"], iters=48)
            mips = mipchain(full)
            write_dds_bc5_snorm(os.path.join(outdir, nm), mips)
            print("  %s: %dx%d, %d mips, max %.2f" % (nm, d.shape[1], d.shape[0], len(mips), np.abs(d).max()))
        # (26/10 b) piel del torso (que no parezca arcilla): brillo / rugosidad y translucidez / rubor por zona (torso_skin.py)
        import torso_skin
        sk = torso_skin.build_skin(G)
        for nm, ch in (("pes_var_torso_skin_a.dds", (0, 1)), ("pes_var_torso_skin_b.dds", (2, 3))):
            fullk, _ = dilate(sk[..., list(ch)], G["mask"], iters=48)
            write_dds_bc5_snorm(os.path.join(outdir, nm), mipchain(fullk))
        np.save(os.path.join(tmpd, "maps_skin.npy"), sk.astype(np.float32))
        print("  pes_var_torso_skin_a/b.dds: brillo %.2f..%.2f, rugosidad %.2f..%.2f, translucidez %.2f..%.2f" % (sk[..., 0].min(), sk[..., 0].max(), sk[..., 1].min(), sk[..., 1].max(), sk[..., 2].min(), sk[..., 2].max()))
        # (26/10 b) brazos: el volumen que la malla ya muestra sale de arm_nrm (pes_var_arm_base_n.dds, todo el atlas del brazo);
        # (26/10 e) con la costura del hombro cosida (arriba)
        fullA, _ = dilate(np.clip(dArm, -0.9, 0.9), Ga["mask"], iters=32)
        write_dds_bc5_snorm(os.path.join(outdir, "pes_var_arm_base_n.dds"), mipchain(fullA))
        print("  pes_var_arm_base_n.dds: correccion del brazo (max %.2f)" % np.abs(dArm).max())
        # (26/10 c) piel real de los brazos (codo, adentro del brazo, muneca, antebrazo de afuera, hombro)
        ska = torso_skin.build_arm_skin(Ga)
        for nm, ch in (("pes_var_arm_skin_a.dds", (0, 1)), ("pes_var_arm_skin_b.dds", (2, 3))):
            fullk, _ = dilate(ska[..., list(ch)], Ga["mask"], iters=32)
            write_dds_bc5_snorm(os.path.join(outdir, nm), mipchain(fullk))
        print("  pes_var_arm_skin_a/b.dds: brillo %.2f..%.2f, rugosidad %.2f..%.2f, translucidez %.2f..%.2f" % (ska[..., 0].min(), ska[..., 0].max(), ska[..., 1].min(), ska[..., 1].max(), ska[..., 2].min(), ska[..., 2].max()))
        fullm, _ = dilate(ms[..., None], G["mask"], iters=48)
        write_dds_bc4_unorm(os.path.join(outdir, "pes_var_torso_rob_m.dds"), mipchain(fullm[..., 0]))
        print("  pes_var_torso_rob_m.dds: estrias (mascara de color), cobertura %.2f %%" % (100.0 * (ms > 0.2).mean()))
        np.save(os.path.join(tmpd, "maps_dR.npy"), dR.astype(np.float32)); np.save(os.path.join(tmpd, "maps_dA.npy"), dA.astype(np.float32))
        np.save(os.path.join(tmpd, "maps_mR.npy"), ms.astype(np.float32))

if __name__ == "__main__":
    sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', 'pelvis'))
    main()
