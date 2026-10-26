# PES21 FX (26/10 c) - genera las pelvis "sin short" (SinCamiseta, tecla 4): copias de pants_out_sub / pants_in_sub /
# pants_out_inner_sub / pants_out_sub_tight con la malla 0 (el short) cambiada por la pelvis de piel (pelvis_mesh.py), pesos
# soldados al torso y a los muslos, normales de los bordes iguales a las de los anillos, uv en el cuadrante libre de abajo a la
# derecha de la textura del torso (u 0.5..1, v 0.5..1: piel pareja en torso_N_bsm, normal map plano) y tangentes de esas uv.
# Uso: python3 make_pelvis.py <carpeta common de TorsoBody> <carpeta de salida> [npz de la vista previa]
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', 'torso'))
import pelvis_mesh as pm
import pelvis_fmdl as pf

VERSIONS = ("pants_out_sub", "pants_in_sub", "pants_out_inner_sub", "pants_out_sub_tight")
UV_BODY = (0.51, 0.52, 0.40)      # u0, v0, uv por metro
UV_LEG = ((0.51, 0.60), (0.755, 0.60))

def merge_w(parts):
    acc = {}
    for wd, k in parts:
        if k <= 0: continue
        for b, w in wd.items(): acc[b] = acc.get(b, 0.0) + w * k
    lst = sorted(acc.items(), key=lambda t: -t[1])[:4]
    tot = sum(w for _, w in lst) or 1.0
    return [(b, w / tot) for b, w in lst]

def smooth_normals(N, F, fixed, iters=2):
    E = np.concatenate([F[:, [0, 1]], F[:, [1, 2]], F[:, [2, 0]]])
    for _ in range(iters):
        acc = N.copy(); cnt = np.ones(len(N))
        np.add.at(acc, E[:, 0], N[E[:, 1]]); np.add.at(cnt, E[:, 0], 1)
        np.add.at(acc, E[:, 1], N[E[:, 0]]); np.add.at(cnt, E[:, 1], 1)
        Nn = acc / cnt[:, None]
        Nn /= np.linalg.norm(Nn, axis=1, keepdims=True)
        N = np.where(fixed[:, None], N, Nn)
    return N

def build(common, ver):
    top, legL, legR, fp, mp = pm.load_rings(common, ver)
    M = pm.build_pelvis(top, legL, legR, torso_mesh=mp[3])
    P = M["P"]
    F = pm.orient_faces(P, M["F"], pm.outward_dir)
    N = pm.finalize_normals(M, top, legL, legR, F)
    fixed = (M["ref_top"] >= 0) | (M["ref_leg"] >= 0)
    N = smooth_normals(N, F, fixed, iters=2)
    # ---- pesos por vertice soldado
    thT, _ = pm.ring_polar(top["P"])
    W = []
    legs = {+1: legL, -1: legR}
    lc = {sd: legs[sd]["P"].mean(0) for sd in legs}
    def leg_ang(sd, p):
        c = lc[sd]; return np.degrees(np.arctan2(sd * (p[0] - c[0]), p[2] - c[2]))
    angT = {sd: np.array([leg_ang(sd, q) for q in legs[sd]["P"]]) for sd in legs}
    for i in range(len(P)):
        if M["ref_top"][i] >= 0: W.append(merge_w([(top["W"][M["ref_top"][i]], 1.0)])); continue
        sd_i = M["side"][i]
        if M["ref_leg"][i] >= 0: W.append(merge_w([(legs[sd_i]["W"][M["ref_leg"][i]], 1.0)])); continue
        th = np.degrees(np.arctan2(P[i, 0], P[i, 2] - pm.ZC))
        jt = int(np.argmin(np.abs(pm.angdiff(thT, th))))
        wl = float(pm.ss(0.30, 2.0, M["s"][i]))
        kl = float(1.0 / (1.0 + np.exp(-P[i, 0] / 0.012)))           # izquierda .. derecha (suave en la linea media)
        parts = [(top["W"][jt], 1.0 - wl)]
        for sd, ks in ((+1, kl), (-1, 1.0 - kl)):
            if ks < 0.02: continue
            jl = int(np.argmin(np.abs(pm.angdiff(angT[sd], leg_ang(sd, P[i])))))
            parts.append((legs[sd]["W"][jl], wl * ks))
        W.append(merge_w(parts))
    # ---- uv por parte y slots (vertice, parte, uv)
    K = M["K"]; chart = M["chart"]
    def uv_of(v, ch, wrap):
        p = P[v]
        if ch == 0:
            th = np.degrees(np.arctan2(p[0], p[2] - pm.ZC)); u = ((th + 180.0) % 360.0) / 360.0
            if wrap and u < 0.5: u += 1.0
            return (UV_BODY[0] + u * 0.95 * UV_BODY[2], UV_BODY[1] + M["s"][v] * 0.12 * UV_BODY[2])
        sd = +1 if ch == 1 else -1
        l = M["lam"][sd][int(v)]
        if wrap and l < 0.5: l += 1.0
        u0, v0 = UV_LEG[0 if sd > 0 else 1]
        return (u0 + l * 0.60 * UV_BODY[2], v0 + (M["s"][v] - 1.0) * 0.05 * UV_BODY[2])
    slot_key = {}; slots_v = []; slots_uv = []; Fs = []
    for f, ch in zip(F, chart):
        if ch == 0:
            us = [((np.degrees(np.arctan2(P[v, 0], P[v, 2] - pm.ZC)) + 180.0) % 360.0) / 360.0 for v in f]
        else:
            sd = +1 if ch == 1 else -1; us = [M["lam"][sd][int(v)] for v in f]
        wrap = (max(us) - min(us)) > 0.5
        tri = []
        for v in f:
            uv = uv_of(v, ch, wrap and (((np.degrees(np.arctan2(P[v, 0], P[v, 2] - pm.ZC)) + 180.0) % 360.0) / 360.0 if ch == 0 else M["lam"][+1 if ch == 1 else -1][int(v)]) < 0.5)
            key = (int(v), ch, round(uv[0], 5), round(uv[1], 5))
            if key not in slot_key:
                slot_key[key] = len(slots_v); slots_v.append(int(v)); slots_uv.append(uv)
            tri.append(slot_key[key])
        Fs.append(tri)
    Fs = np.array(Fs); sv = np.array(slots_v); UV = np.array(slots_uv)
    # ---- tangentes (de las uv), con el signo de la binormal (convencion de las texturas: B = cross(N, T) * w)
    Ps = P[sv]; Ns = N[sv]
    T = np.zeros((len(sv), 3)); B = np.zeros((len(sv), 3))
    e1 = Ps[Fs[:, 1]] - Ps[Fs[:, 0]]; e2 = Ps[Fs[:, 2]] - Ps[Fs[:, 0]]
    d1 = UV[Fs[:, 1]] - UV[Fs[:, 0]]; d2 = UV[Fs[:, 2]] - UV[Fs[:, 0]]
    det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    r = np.where(np.abs(det) > 1e-12, 1.0 / np.where(np.abs(det) > 1e-12, det, 1.0), 0.0)
    ft = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) * r[:, None]
    fb = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) * r[:, None]
    for k in range(3): np.add.at(T, Fs[:, k], ft); np.add.at(B, Fs[:, k], fb)
    T = T - Ns * np.sum(T * Ns, 1, keepdims=True); T /= np.maximum(np.linalg.norm(T, axis=1, keepdims=True), 1e-12)
    w = np.where(np.sum(np.cross(Ns, T) * B, 1) < 0, -1.0, 1.0)
    slots = dict(pos=Ps, nrm=Ns, tan=np.concatenate([T, w[:, None]], 1), uv=UV,
                 bone=[[b for b, _ in W[v]] + [""] * (4 - len(W[v])) for v in sv],
                 w=[[x for _, x in W[v]] + [0.0] * (4 - len(W[v])) for v in sv])
    return M, P, F, N, slots, Fs

def main():
    common, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    for ver in VERSIONS:
        M, P, F, N, slots, Fs = build(common, ver)
        info = pf.write_pelvis(os.path.join(common, ver + ".fmdl"), os.path.join(outdir, ver + ".fmdl"), slots, Fs,
                               thigh_fmdl=os.path.join(common, "thigh_short.fmdl"))
        print("%s: %d vertices soldados, %d slots (entran %d), %d triangulos (entran %d), material %s, renombres %s" % (
            ver, len(P), info["verts"][0], info["verts"][1], info["tris"][0], info["tris"][1], info["mat"], ", ".join(info["renames"])))
        if ver == "pants_out_sub":
            if len(sys.argv) > 3: np.savez(sys.argv[3], P=slots["pos"], F=Fs, N=slots["nrm"], T=slots["tan"], UV=slots["uv"])
            info = gen_textures(slots, Fs, outdir)
            print("  texturas de la pelvis: cobertura %.1f %%, poros std %.3f, albedo x2 %.3f..%.3f" % (100 * info["cover"], info["dstd"], info["alb"][0], info["alb"][1]))


# ---------------------------------------------------------------- texturas propias de la pelvis (el PS las lee en el cuadrante
# u 0.5..1, v 0.5..1 de la textura del torso, reescalado a 0..1: 2048 x 2048 = ~1600 texeles por metro):
#   pes_var_pelvis_n.dds     BC5 SNORM: poros y lineas finas de la piel (delta del normal map; el normal map del torso ahi es plano)
#   pes_var_pelvis_alb.dds   BC4 UNORM: albedo x (2 v): el tono del torso junto al borde (+3.6 %) y manchitas suaves; pliegues
#   pes_var_pelvis_skin_a/b  BC5: brillo / rugosidad, translucidez / rubor (como pes_var_torso_skin_*)
def gen_textures(slots, Fs, outdir, size=2048):
    from uvbake import bake, dilate
    from texio import write_dds_bc5_snorm, write_dds_bc4_unorm
    from scipy.ndimage import gaussian_filter
    import torso_skin as tsk
    T = {"parts": [dict(pos=slots["pos"], nrm=slots["nrm"], tan=slots["tan"], uv=np.clip((slots["uv"] - 0.5) * 2.0, 0, 1),
                        faces=np.asarray(Fs))]}
    G = bake(T, None, W=size, H=size, vmax=1.0)
    mask = G["mask"]; ii = np.where(mask); Pt = G["P"][ii]
    rng = np.random.default_rng(1234)
    # ---- poros: ruido fino (sigma 0.7 px) y algo de ondulacion (3 px); pendiente en el espacio uv -> delta (rojo = T, verde = B)
    h = gaussian_filter(rng.normal(size=(size, size)), 0.7) * 1.0 + gaussian_filter(rng.normal(size=(size, size)), 3.0) * 2.2
    gy, gx = np.gradient(h)
    d = np.stack([-gx, -gy], -1)
    d *= 0.065 / max(d[mask].std(), 1e-9)
    # zonas (posicion de reposo): raya entre los gluteos, pliegue de abajo del gluteo, ingle
    x, y, z = Pt[:, 0], Pt[:, 1], Pt[:, 2]
    th = np.degrees(np.arctan2(x, z - pm.ZC))
    cleft = pm.g(pm.angdiff(th, 180) / 9) * pm.ss(0.97, 0.92, y)
    fold = pm.g((y - 0.848) / 0.010) * (pm.g(pm.angdiff(th, 150) / 30) + pm.g(pm.angdiff(th, -150) / 30)) * (z < 0.02)
    ingle = pm.ss(0.95, 0.88, y) * (pm.g(pm.angdiff(th, 25) / 10) + pm.g(pm.angdiff(th, -25) / 10)) * (z > 0.08)
    # ---- desvanecido hacia los bordes (cintura y muslos): ahi queda como el torso / el muslo
    fade = pm.ss(0.995, 0.975, y) * pm.ss(0.808, 0.828, y + 0.02 * (np.abs(x) > 0.06))
    # ---- albedo (x 2 v): +3.6 % (el tono del torso junto al borde) con manchas suaves y los pliegues un poco mas oscuros
    blot = gaussian_filter(rng.normal(size=(size, size)), 18.0); blot /= max(blot[mask].std(), 1e-9)
    alb = np.full((size, size), 0.518)
    alb[ii] = 0.518 * (1 + 0.016 * blot[ii] - 0.06 * cleft - 0.05 * fold - 0.03 * ingle)
    # ---- piel por zona (DEF de torso_skin en los bordes)
    D = tsk.DEF
    nz = tsk._N(Pt)
    glute = pm.g((y - 0.90) / 0.05) * (pm.g(pm.angdiff(th, 150) / 30) + pm.g(pm.angdiff(th, -150) / 30))
    front = pm.g(th / 30) * pm.ss(0.97, 0.90, y)
    crease = np.clip(cleft + fold + ingle, 0, 1)
    spec = D["spec"] * (1 - 0.06 * glute + 0.03 * front - 0.10 * crease) * (1 + 0.04 * nz)
    rough = D["rough"] + 0.03 * glute - 0.02 * front + 0.05 * crease + 0.02 * nz
    trans = D["trans"] + 0.04 * glute + 0.02 * front - 0.02 * crease
    red = D["red"] + 0.10 * crease + 0.04 * glute
    sk = np.zeros((size, size, 4)); sk[...] = (D["spec"], D["rough"], D["trans"], D["red"])
    sk[ii] = np.stack([spec, rough, trans, red], -1)
    # cerca de los bordes, todo vuelve a lo del torso / muslo (fade = 1 adentro, 0 en la cintura y en los muslos)
    f = fade[:, None]
    sk[ii] = np.array([D["spec"], D["rough"], D["trans"], D["red"]]) + (sk[ii] - np.array([D["spec"], D["rough"], D["trans"], D["red"]])) * f
    alb[ii] = 0.518 + (alb[ii] - 0.518) * f[:, 0]
    d[ii] *= (0.35 + 0.65 * f)
    def mipchain(a):
        mips = [a.astype(np.float32)]
        while min(mips[-1].shape[:2]) > 1:
            c = mips[-1]; mips.append(0.25 * (c[0::2, 0::2] + c[1::2, 0::2] + c[0::2, 1::2] + c[1::2, 1::2]))
        return mips
    dd, _ = dilate(np.where(mask[..., None], d, 0), mask, iters=24)
    write_dds_bc5_snorm(os.path.join(outdir, "pes_var_pelvis_n.dds"), mipchain(np.clip(dd, -0.9, 0.9)))
    aa, _ = dilate(alb[..., None], mask, iters=24)
    write_dds_bc4_unorm(os.path.join(outdir, "pes_var_pelvis_alb.dds"), mipchain(np.clip(aa[..., 0], 0, 1)))
    for nm, ch in (("pes_var_pelvis_skin_a.dds", (0, 1)), ("pes_var_pelvis_skin_b.dds", (2, 3))):
        kk, _ = dilate(sk[..., list(ch)], mask, iters=24)
        write_dds_bc5_snorm(os.path.join(outdir, nm), mipchain(np.clip(kk, 0, 1)))
    return dict(cover=float(mask.mean()), dstd=float(d[mask].std()), alb=(float(alb[mask].min()), float(alb[mask].max())))

if __name__ == "__main__":
    main()
