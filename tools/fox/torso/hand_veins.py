# PES21 FX (02/11) - VENAS DEL DORSO DE LA MANO, rediseno (Agustin: "las venas al cerrar se ven bien raras": eran dos barras rectas
# y parejas en el dorso). Se dibuja la red venosa dorsal como es: venas metacarpianas que salen entre los nudillos (finas, se apagan
# hacia los dedos), se juntan en el arco venoso dorsal (curvo, a media mano) y salen por los costados hacia la muneca (cefalica del
# lado del pulgar, basilica del lado del menique), con una rama del pliegue pulgar-indice y un puente entre metacarpianas. Cada vena
# ondula un poco, cambia de grosor y de intensidad (no todas se marcan igual), y el relieve es redondeado (tubo bajo la piel).
# Se evalua en 3D sobre la malla de la mano en reposo (hand_l / hand_r.fmdl, malla 0) en un plano del dorso armado con los nudillos
# y la muneca (los saca de los pesos de los huesos), asi que no depende de los cortes de uv.
# Salidas:
#   - pes_veins_arm.dds (RGBA8, 2048, con mips): SOLO se reescriben los texeles de las dos manos (el resto del atlas del brazo queda
#     igual): xy = pendiente (0.5 = plano), z = mascara de color de la vena (PS aa7d1833, t29, con w305 y el puno cerrado)
#   - pes_hand_l.dds / pes_hand_r.dds (datos por vertice de pes_hands.hlsli, t126): solo la fila 5 (venas del dorso con el puno:
#     xyz cambio de normal, w relieve en m; VS 1c02a92a con x301)
# Uso: python3 hand_veins.py <carpeta con hand_l.fmdl y hand_r.fmdl> <pes_veins_arm.dds> <pes_hand_l.dds> <pes_hand_r.dds> <salida>
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from fmdl import Fmdl, meshes
from torso_mesh import load_parts
from uvbake import bake

H_TEX = 0.00050     # alto del relieve de la vena para la textura (m)
H_VTX = 0.00035     # alto del relieve geometrico con el puno (fila 5; el maximo que ya tenia)
SLOPE_MAX = 0.45    # tope de la pendiente en la textura (la anterior llegaba a 0.58 con barras parejas)

def frame(m, names, side, old_dorsal):
    """muneca W, eje de la mano A (muneca -> nudillo del medio), C (hacia el pulgar), D (dorso), largo L, nudillos en c"""
    P = m["pos"].astype(float)
    def bw(name):
        ids = [i for i, n in enumerate(names) if n == name]
        return ((m["idx"] == ids[0]) * m["w"]).sum(1) if ids else np.zeros(len(P))
    wr = bw("dsk_wrist_" + side) + bw("dsk_forearm_" + side) + bw("sk_forearm_" + side) + bw("dsk_forearm_t_" + side)
    W = P[wr > 0.6].mean(0)
    K = {}
    for fn in ("index", "middle", "ring", "pinky"):
        sel = (bw("skh_%s_mata_%s" % (fn, side)) > 0.25) & (bw("skh_%s_mcp_%s" % (fn, side)) > 0.25)
        K[fn] = P[sel].mean(0)
    A = K["middle"] - W; L = np.linalg.norm(A); A /= L
    C = K["index"] - K["pinky"]; C -= (C @ A) * A; C /= np.linalg.norm(C)
    D = np.cross(A, C)
    # el dorso: del lado donde estaban las venas viejas (texturas del 26/10, siempre en el dorso)
    if old_dorsal @ D < 0: D = -D
    Hw = 0.5 * np.linalg.norm((K["index"] - K["pinky"]) - ((K["index"] - K["pinky"]) @ A) * A)
    cK = {k: ((v - W) @ C) / Hw for k, v in K.items()}
    return W, A, C, D, L, Hw, cK

def network(cK, seed):
    """venas en (s, c): s 0 muneca .. 1 nudillo del medio, c +1 lado del indice (pulgar). (puntos, radio mm, intensidad, punta)"""
    ci, cm, cr, cp = cK["index"], cK["middle"], cK["ring"], cK["pinky"]
    a, b, d = 0.5 * (ci + cm), 0.5 * (cm + cr), 0.5 * (cr + cp)
    # (las metacarpianas del 2o y 3er espacio se juntan en un tronco que cruza hacia la cefalica; la del 4o espacio baja a la
    # basilica; un arco bajo y tenue une los dos lados; la del pliegue pulgar-indice alimenta la cefalica)
    J = (0.45, 0.5 * (a + b) + 0.05)
    V = [
        ([(0.86, a), (0.71, a - 0.02), (0.56, a - 0.08), J], 1.6, 0.60, 1),                                   # 2o espacio
        ([(0.88, b), (0.73, b + 0.03), (0.57, b + 0.12), J], 1.6, 0.62, 1),                                   # 3er espacio
        ([J, (0.37, ci - 0.35), (0.29, ci - 0.02), (0.22, ci + 0.22)], 1.9, 0.72, 0),                          # tronco -> cefalica
        ([(0.86, d), (0.70, d - 0.04), (0.53, d - 0.14), (0.38, cp + 0.05), (0.24, cp - 0.12)], 1.5, 0.55, 1), # 4o espacio
        ([(0.66, ci + 0.20), (0.48, ci + 0.26), (0.32, ci + 0.25), (0.22, ci + 0.22)], 1.6, 0.58, 1),          # pliegue pulgar-indice
        ([(0.22, ci + 0.22), (0.08, ci + 0.17), (-0.05, ci + 0.12), (-0.25, ci + 0.08)], 2.2, 0.78, 0),        # cefalica
        ([(0.24, cp - 0.12), (0.10, cp - 0.20), (-0.04, cp - 0.22), (-0.25, cp - 0.18)], 2.0, 0.66, 0),        # basilica
        ([(0.38, cp + 0.05), (0.31, d + 0.30), (0.32, b - 0.05), (0.37, ci - 0.35)], 1.4, 0.42, 0),            # arco (bajo y tenue)
    ]
    rng = np.random.default_rng(seed)
    out = []
    for pts, r, k, tip in V:
        pts = np.array(pts, float)
        # curva suave por los puntos (Catmull-Rom) muestreada fina
        P = np.concatenate([pts[:1], pts, pts[-1:]])
        seg = []
        for i in range(len(pts) - 1):
            p0, p1, p2, p3 = P[i], P[i + 1], P[i + 2], P[i + 3]
            t = np.linspace(0, 1, 40, endpoint=False)[:, None]
            seg.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3))
        seg.append(pts[-1:]); Q = np.concatenate(seg)
        u = np.linspace(0, 1, len(Q))
        ph = rng.uniform(0, 2 * np.pi, 3)
        out.append(dict(Q=Q, u=u, r=r * rng.uniform(0.92, 1.08), k=k * rng.uniform(0.9, 1.1), tip=tip, ph=ph))
    return out

def height(Puv, net, L, Hw):
    """Puv: (n, 2) en metros (u a lo largo, v de costado). Devuelve alto 0..1 (perfil de tubo) e intensidad de color 0..1"""
    h = np.zeros(len(Puv)); col = np.zeros(len(Puv))
    for v in net:
        Q = v["Q"] * np.array([L, Hw])
        # ondulacion: corrimiento de costado que cambia a lo largo de la vena (1 mm)
        tng = np.gradient(Q, axis=0); tng /= np.maximum(np.linalg.norm(tng, axis=1, keepdims=True), 1e-9)
        nrm = np.stack([-tng[:, 1], tng[:, 0]], 1)
        s = np.cumsum(np.r_[0, np.linalg.norm(np.diff(Q, axis=0), axis=1)])
        wig = 0.0007 * np.sin(s / 0.011 + v["ph"][0]) + 0.0004 * np.sin(s / 0.0047 + v["ph"][1])
        Q = Q + nrm * wig[:, None]
        # grosor e intensidad a lo largo: varia un poco y se afina / apaga hacia la punta de los dedos
        u = v["u"]
        rad = 1e-3 * v["r"] * (1 + 0.12 * np.sin(s / 0.008 + v["ph"][2]))
        ink = v["k"] * (1 + 0.15 * np.sin(s / 0.013 + v["ph"][1]))
        if v["tip"]: rad *= 0.55 + 0.45 * u; ink *= np.clip(u * 1.8, 0, 1) ** 1.2
        for i0 in range(0, len(Puv), 40000):
            X = Puv[i0:i0 + 40000]
            dd = np.linalg.norm(X[:, None, :] - Q[None, :, :], axis=2)
            j = dd.argmin(1); dmin = dd[np.arange(len(X)), j]
            x = np.clip(dmin / rad[j], 0, 1)
            prof = np.sqrt(np.clip(1 - x * x, 0, 1)) * (0.5 + 0.5 * np.cos(np.pi * x))   # tubo redondo con borde suave
            h[i0:i0 + 40000] = np.maximum(h[i0:i0 + 40000], prof * np.clip(ink[j], 0, 1.2) / 0.85)
            col[i0:i0 + 40000] = np.maximum(col[i0:i0 + 40000], (1 - x) ** 1.5 * np.clip(ink[j], 0, 1))
    return np.clip(h, 0, 1.2), np.clip(col, 0, 1)

def make_field(W, A, C, D, L, Hw, net, uK):
    def field(P, N=None):
        R = P - W
        u = R @ A; v = R @ C
        h, col = height(np.stack([u, v], 1), net, L, Hw)
        keep = np.clip((u + 0.03) / 0.02, 0, 1) * np.clip((uK - 0.004 - u) / 0.01, 0, 1)
        if N is not None: keep = keep * np.clip(((N @ D) - 0.20) / 0.30, 0, 1)
        return h * keep, col * keep
    return field

def mips_rgba(a):
    out = [a.astype(np.float32)]
    while min(out[-1].shape[:2]) > 1:
        c = out[-1]; out.append(0.25 * (c[0::2, 0::2] + c[1::2, 0::2] + c[0::2, 1::2] + c[1::2, 1::2]))
    return out

def main(folder, veins_dds, hand_l_dds, hand_r_dds, outdir):
    os.makedirs(outdir, exist_ok=True)
    d = bytearray(open(veins_dds, "rb").read())
    base = 148
    atlas = np.frombuffer(bytes(d), np.uint8, count=2048 * 2048 * 4, offset=base).reshape(2048, 2048, 4).astype(np.float32) / 255.0
    for side, hdds, seed in (("l", hand_l_dds, 11), ("r", hand_r_dds, 23)):
        f = Fmdl(os.path.join(folder, "hand_%s.fmdl" % side)); names = [b["name"] for b in f.bones]
        m = meshes(f)[0]
        uvv = m["uv"]; px = np.clip((uvv[:, 0] * 2048).astype(int), 0, 2047); py = np.clip((uvv[:, 1] * 2048).astype(int), 0, 2047)
        Nm = m["nrm"][:, :3].astype(float); Nm /= np.linalg.norm(Nm, axis=1, keepdims=True)
        old = atlas[py, px, 2] > 0.5
        W, A, C, D, L, Hw, cK = frame(m, names, side, Nm[old].mean(0))
        net = network(cK, seed)
        uK = L * 1.0
        field = make_field(W, A, C, D, L, Hw, net, uK)
        # --- textura: texeles de la malla 0 de esta mano
        T = load_parts(folder, [("hand_" + side, 0)])
        G = bake(T, [], W=2048, H=2048, vmax=1.0)
        mk = G["mask"]; ii = np.where(mk)
        P = G["P"][ii]; N = G["N"][ii]; Tg = G["Tg"][ii]; Bn = G["Bn"][ii]
        eps = 0.0003
        h0, col = field(P, N)
        hT = (field(P + eps * Tg, N)[0] - field(P - eps * Tg, N)[0]) / (2 * eps) * H_TEX
        hB = (field(P + eps * Bn, N)[0] - field(P - eps * Bn, N)[0]) / (2 * eps) * H_TEX
        sx = np.clip(-hT, -SLOPE_MAX, SLOPE_MAX); sy = np.clip(-hB, -SLOPE_MAX, SLOPE_MAX)
        # texeles de esta mano (y un borde de 3 texeles para el filtrado): primero se limpian las venas viejas
        from scipy.ndimage import binary_dilation
        big = binary_dilation(mk, iterations=3)
        atlas[big, 0] = 0.5; atlas[big, 1] = 0.5; atlas[big, 2] = 0.0
        atlas[ii[0], ii[1], 0] = 0.5 + 0.5 * sx; atlas[ii[0], ii[1], 1] = 0.5 + 0.5 * sy; atlas[ii[0], ii[1], 2] = col
        print("mano %s: %d texeles, venas en %d, pendiente max %.2f, largo %.1f cm, ancho de nudillos %.1f cm" %
              (side, len(ii[0]), int((col > 0.05).sum()), max(np.abs(sx).max(), np.abs(sy).max()), 100 * L, 200 * Hw))
        # --- fila 5 de los datos de la mano (por vertice)
        hb = bytearray(open(hdds, "rb").read())
        nv = m["nvert"]
        H = np.frombuffer(bytes(hb), np.float32, count=nv * 11 * 4, offset=base).reshape(11, nv, 4).copy()
        Pv = m["pos"].astype(float); Nv = m["nrm"][:, :3].astype(float); Nv /= np.linalg.norm(Nv, axis=1, keepdims=True)
        Tv = m["tan"][:, :3].astype(float); Tv -= (Tv * Nv).sum(1, keepdims=True) * Nv; Tv /= np.maximum(np.linalg.norm(Tv, axis=1, keepdims=True), 1e-9)
        Bv = np.cross(Nv, Tv)
        hv, _ = field(Pv, Nv)
        gT = (field(Pv + eps * Tv, Nv)[0] - field(Pv - eps * Tv, Nv)[0]) / (2 * eps) * H_VTX
        gB = (field(Pv + eps * Bv, Nv)[0] - field(Pv - eps * Bv, Nv)[0]) / (2 * eps) * H_VTX
        dn = -(gT[:, None] * Tv + gB[:, None] * Bv)
        dn *= np.minimum(1.0, 0.3 / np.maximum(np.linalg.norm(dn, axis=1, keepdims=True), 1e-9))
        H[5, :, :3] = dn; H[5, :, 3] = H_VTX * hv
        hb[base:base + H.nbytes] = H.astype(np.float32).tobytes()
        open(os.path.join(outdir, os.path.basename(hdds)), "wb").write(hb)
        print("  fila 5: %d vertices con vena (antes %d), relieve max %.2f mm" % (int((hv > 0.02).sum()), 168, 1000 * H[5, :, 3].max()))
    # mips y escritura (mismo encabezado DX10 R8G8B8A8)
    mp = mips_rgba(atlas)
    body = b"".join((np.clip(np.round(x * 255), 0, 255).astype(np.uint8)).tobytes() for x in mp)
    open(os.path.join(outdir, "pes_veins_arm.dds"), "wb").write(bytes(d[:base]) + body)
    return atlas

if __name__ == "__main__":
    main(*sys.argv[1:6])
