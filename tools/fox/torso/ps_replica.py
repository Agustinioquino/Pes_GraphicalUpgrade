# PES21 FX (26/10 d) - replica del PS de la piel (aa7d1833) para el torso y los brazos de TorsoBody, con las texturas TAL CUAL las
# instala el paquete (package/PES21FX/textures: BC5 SNORM / BC4 con sus mips) y las mallas de la variante (datos del VS, filas
# 0..9). Sirve para encontrar que parte del relieve se ve mal en el juego (manchas oscuras, surcos dobles): cada componente se
# puede apagar con una variable de entorno.
#   PSR_DEF     definicion del jugador (x172 o la de su fila; 1 = la del juego)      PSR_T   variante (-1 robusto .. 1 atletico)
#   PSR_NOBASEFIX / PSR_NODEF / PSR_NOVAR / PSR_NOSPINE / PSR_NOMESHBASE / PSR_ORIG (todo como TorsoBody)
# Uso: python3 ps_replica.py <carpeta common> <carpeta tex de TorsoBody> <carpeta textures del paquete> <carpeta datos v8> <salida.png>
import sys, os, struct, math
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
from fmdl import Fmdl, meshes
from texio import load_ftex, decode_bc4_snorm
from uvbake import rasterize

def read_dds(path):
    """DDS DX10 BC5_SNORM (84) / BC4_UNORM (80) / BC4_SNORM (81) / R32G32B32A32 (2) -> lista de mips (H, W, k) float32"""
    d = open(path, 'rb').read()
    h, w = struct.unpack_from('<II', d, 12); mips = max(1, struct.unpack_from('<I', d, 28)[0])
    fmt = struct.unpack_from('<I', d, 128)[0]
    off = 148; out = []
    for m in range(mips):
        H, W = max(1, h >> m), max(1, w >> m)
        if fmt == 2:
            n = H * W * 16; out.append(np.frombuffer(d, np.float32, H * W * 4, off).reshape(H, W, 4)); off += n; continue
        Hp, Wp = max(4, (H + 3) // 4 * 4), max(4, (W + 3) // 4 * 4)
        nb = (Hp // 4) * (Wp // 4)
        if fmt == 84:
            blk = np.frombuffer(d, np.uint8, nb * 16, off).reshape(nb, 16); off += nb * 16
            r = decode_bc4_snorm(blk[:, :8], Hp, Wp); g = decode_bc4_snorm(blk[:, 8:], Hp, Wp)
            out.append(np.stack([r, g], -1)[:H, :W].astype(np.float32))
        elif fmt in (80, 81):
            blk = np.frombuffer(d, np.uint8, nb * 8, off).reshape(nb, 8); off += nb * 8
            if fmt == 81: out.append(decode_bc4_snorm(blk, Hp, Wp)[:H, :W, None].astype(np.float32))
            else: out.append(decode_bc4_unorm(blk, Hp, Wp)[:H, :W, None].astype(np.float32))
        else:
            raise ValueError("formato DXGI %d no previsto (%s)" % (fmt, path))
    return out

def decode_bc4_unorm(blocks, H, W):
    e0 = blocks[:, 0].astype(np.float32); e1 = blocks[:, 1].astype(np.float32)
    bits = np.zeros(len(blocks), np.uint64)
    for k in range(6): bits |= blocks[:, 2 + k].astype(np.uint64) << np.uint64(8 * k)
    lv = np.where((e0 > e1)[:, None], np.stack([e0, e1] + [((8 - k) * e0 + (k - 1) * e1) / 7.0 for k in range(2, 8)], 1),
                  np.stack([e0, e1] + [((6 - k) * e0 + (k - 1) * e1) / 5.0 for k in range(2, 6)] + [np.zeros_like(e0), np.full_like(e0, 255)], 1))
    idx = np.stack([((bits >> np.uint64(3 * i)) & np.uint64(7)).astype(np.int64) for i in range(16)], 1)
    v = np.take_along_axis(lv, idx, 1) / 255.0
    return v.reshape(H // 4, W // 4, 4, 4).transpose(0, 2, 1, 3).reshape(H, W)

def bilinear(img, u, v):
    H, W = img.shape[:2]
    x = np.clip(u * W - 0.5, 0, W - 1.001); y = np.clip(v * H - 0.5, 0, H - 1.001)
    x0 = x.astype(int); y0 = y.astype(int); fx = (x - x0)[:, None]; fy = (y - y0)[:, None]
    a = img[y0, x0]; b = img[y0, x0 + 1]; c = img[y0 + 1, x0]; d = img[y0 + 1, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy

def sample_mip(mips, u, v, lod):
    """muestreo trilineal a nivel lod (escalar)"""
    lod = float(np.clip(lod, 0, len(mips) - 1))
    l0 = int(math.floor(lod)); l1 = min(l0 + 1, len(mips) - 1); f = lod - l0
    a = bilinear(mips[l0], u, v)
    return a if f < 1e-3 or l1 == l0 else a * (1 - f) + bilinear(mips[l1], u, v) * f

def clamp94(xy):
    l2 = np.sum(xy * xy, 1, keepdims=True)
    return np.where(l2 > 0.94, xy * np.sqrt(0.94 / np.maximum(l2, 1e-9)), xy)

def ftex_mips(path, first=1, n=7):
    """mips first..first+n-1 de un FTEX (el 0 de 4096 no hace falta: la vista no llega a esa resolucion)"""
    out = []
    for m in range(first, first + n):
        try: out.append(load_ftex(path, mip=m))
        except Exception: break
    return out

class Scene: pass

def load(common, texdir, pkgtex, datadir, t, meshbase=True):
    S = Scene()
    S.nmT = [m[..., :2] * 2 - 1 for m in ftex_mips(os.path.join(texdir, "torsob_nrm.ftex"))]
    S.nmA = [m[..., :2] * 2 - 1 for m in ftex_mips(os.path.join(texdir, "arm_nrm.ftex"))]
    S.albT = load_ftex(os.path.join(texdir, "torso_3_bsm.ftex"), mip=1)[..., :3]
    S.albA = load_ftex(os.path.join(texdir, "arm_3_bsm.ftex"), mip=1)[..., :3]
    tx = lambda n: read_dds(os.path.join(pkgtex, n))
    S.baseT, S.baseA = tx("pes_var_torso_base_n.dds"), tx("pes_var_arm_base_n.dds")
    S.atl, S.rob, S.spine = tx("pes_var_torso_atl_n.dds"), tx("pes_var_torso_rob_n.dds"), tx("pes_var_torso_spine_n.dds")
    S.meshes = []; S.meshbase = meshbase
    S.tilt = None
    if os.path.exists(os.path.join(pkgtex, "pes_var_torso_tilt_b.dds")):
        S.tilt = {reg: {k: tx("pes_var_%s_tilt_%s.dds" % (reg, k)) for k in "bar"} for reg in ("torso", "arm")}
    def apply(m, data):
        pos = m["pos"].astype(np.float64); nrm = m["nrm"][:, :3].astype(np.float64); tan = m["tan"].astype(np.float64)
        R = 10; nv = data.shape[0] // R
        k = int(np.argmin([np.abs(data[R * v, :, :3] - pos).max() for v in range(nv)]))
        d0, n0, t0 = np.zeros_like(pos), nrm, tan[:, :3]
        if meshbase: d0, n0, t0 = data[R * k + 7, :, :3], data[R * k + 8, :, :3], data[R * k + 9, :, :3]
        if t != 0:
            r = R * k + (1 if t < 0 else 4); a = abs(t)
            d1, n1, t1 = data[r, :, :3], data[r + 1, :, :3], data[r + 2, :, :3]
        else:
            a = 0.0; d1, n1, t1 = d0, n0, t0
        pos = pos + d0 * (1 - a) + a * d1
        nrm = n0 * (1 - a) + a * n1; nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
        tt = t0 * (1 - a) + a * t1
        return pos, nrm, np.concatenate([tt, tan[:, 3:4]], 1)
    M = meshes(Fmdl(os.path.join(common, "pants_out_sub.fmdl")))
    for k in (1, 2, 3):
        data = read_dds(os.path.join(datadir, "pes_var_torso_%d.dds" % k))[0]
        p, n, tg = apply(M[k], data)
        S.meshes.append(dict(pos=p, nrm=n, tan=tg, uv=M[k]["uv"].astype(np.float64), faces=M[k]["faces"], torso=True,
                             nrm0=M[k]["nrm"][:, :3].astype(np.float64), tan0=M[k]["tan"].astype(np.float64)))
    MA = meshes(Fmdl(os.path.join(common, "arm.fmdl")))
    data = read_dds(os.path.join(datadir, "pes_var_arm.dds"))[0]
    p, n, tg = apply(MA[0], data)
    S.meshes.append(dict(pos=p, nrm=n, tan=tg, uv=MA[0]["uv"].astype(np.float64), faces=MA[0]["faces"], torso=False,
                         nrm0=MA[0]["nrm"][:, :3].astype(np.float64), tan0=MA[0]["tan"].astype(np.float64)))
    return S

def ps_xy(S, m, uv, t, lod, env, mt=None):
    """el normal map que arma el PS (espacio tangente, convencion de la textura: rojo = T, verde = cross(N, T) * w)"""
    torso = m["torso"]
    nm = S.nmT if torso else S.nmA
    u, v = uv[:, 0], uv[:, 1]
    xy = sample_mip(nm, u, v, lod - 1)          # nm[0] = mip 1 (2048), como los mapas propios
    if env.get("PSR_ORIG"): return xy
    wa, wr = max(t, 0.0), max(-t, 0.0)
    vt = np.clip(v * 2, 0, 1)
    if not env.get("PSR_NOBASEFIX"):
        if torso: xy = xy + sample_mip(S.baseT, u, vt, lod - 1) * (v < 0.5)[:, None]
        else: xy = xy + sample_mip(S.baseA, u, v, lod - 1)
        xy = clamp94(xy)
    defP = float(env.get("PSR_DEF", "1"))
    kat, kar = float(env.get("PSR_KATL_T", "0.15")), float(env.get("PSR_KATL_A", "0.20"))
    krt, krr = float(env.get("PSR_KROB_T", "0.75")), float(env.get("PSR_KROB_A", "0.50"))
    defM = defP * (1 + (kat * wa - krt * wr if torso else kar * wa - krr * wr))
    if not env.get("PSR_NODEF") and abs(defM - 1) > 0.01:
        mM = sample_mip(nm, u, v, lod + 3.5 - 1)
        xy = clamp94(xy + (defM - 1) * mM)
    if t != 0 and torso:
        xy = xy * (1 - float(env.get("PSR_ROBFLAT", "0.3")) * wr)
        if not env.get("PSR_NOVAR"):
            vd = sample_mip(S.rob if t < 0 else S.atl, u, vt, lod - 1)
            xy = clamp94(xy + vd * abs(t) * (v < 0.5)[:, None])
    if torso and not env.get("PSR_NOSPINE"):
        sd = sample_mip(S.spine, u, vt, lod - 1)
        ska = float(env.get("PSR_SPINE_KA", "0.25"))
        xy = clamp94(xy + sd * (1 - 0.5 * wr) * (1 + ska * wa) * (v < 0.5)[:, None])
    L = float(env.get("PSR_LIMIT", "0"))
    if L > 0:   # (26/10 d) tope suave de la inclinacion del normal map (|xy| -> L tanh(|xy| / L))
        s_ = np.sqrt(np.sum(xy * xy, 1, keepdims=True))
        xy = xy * np.where(s_ > 1e-6, L * np.tanh(s_ / L) / np.maximum(s_, 1e-6), 1.0)
    K2, L2 = float(env.get("PSR_KNEE", "0")), float(env.get("PSR_TOT", "0"))
    if L2 > 0 and mt is not None:   # (26/10 d) tope de la inclinacion TOTAL (malla + textura) respecto de la superficie original
        tot = xy + mt
        s_ = np.sqrt(np.sum(tot * tot, 1, keepdims=True))
        f = np.where(s_ <= K2, s_, K2 + (L2 - K2) * np.tanh((s_ - K2) / max(L2 - K2, 1e-6)))
        xy = tot * np.where(s_ > 1e-6, f / np.maximum(s_, 1e-6), 1.0) - mt
    return xy

def render(S, yaw, W, H, ppm, cx, cy, t, env, light=(-0.35, 0.55, 0.76), tmp="/tmp/_psr", out_xy=False):
    th = math.radians(yaw)
    R = np.array([[math.cos(th), 0, math.sin(th)], [0, 1, 0], [-math.sin(th), 0, math.cos(th)]])
    tris, own = [], []
    for mi, m in enumerate(S.meshes):
        p = m["pos"] @ R.T
        xy = np.stack([W / 2 + (p[:, 0] - cx) * ppm, H / 2 - (p[:, 1] - cy) * ppm, p[:, 2]], 1)
        tris.append(xy[m["faces"]]); own.append(np.stack([np.full(len(m["faces"]), mi), np.arange(len(m["faces"]))], 1))
    tris = np.concatenate(tris); own = np.concatenate(own)
    idv, b0, b1 = rasterize(W, H, tris, tmp)
    img = np.full((H, W, 3), 0.13); mag = np.zeros((H, W)); tilt = np.zeros((H, W)); tilt0 = np.zeros((H, W))
    ii = np.where(idv >= 0)
    o = own[idv[ii]]; w0 = b0[ii][:, None]; w1 = b1[ii][:, None]; w2 = 1 - w0 - w1
    L = np.array(light, float); L /= np.linalg.norm(L)
    for mi, m in enumerate(S.meshes):
        s = o[:, 0] == mi
        if not s.any(): continue
        f = m["faces"][o[s, 1]]
        I = lambda a: a[f[:, 0]] * w0[s] + a[f[:, 1]] * w1[s] + a[f[:, 2]] * w2[s]
        N = I(m["nrm"]) @ R.T; N /= np.linalg.norm(N, axis=1, keepdims=True)
        Tg = I(m["tan"][:, :3]) @ R.T; Tg -= N * np.sum(Tg * N, 1, keepdims=True); Tg /= np.maximum(np.linalg.norm(Tg, axis=1, keepdims=True), 1e-9)
        B = np.cross(N, Tg) * np.where(I(m["tan"][:, 3:4]) < 0, -1.0, 1.0)
        uv = I(m["uv"])
        # nivel de mip como la GPU: texeles por pixel de la textura de 4096 (mip 0)
        uvp = m["uv"] * 4096.0; P3 = m["pos"]
        e = np.linalg.norm(uvp[m["faces"][:, 1]] - uvp[m["faces"][:, 0]], axis=1) / np.maximum(np.linalg.norm(P3[m["faces"][:, 1]] - P3[m["faces"][:, 0]], axis=1), 1e-6)
        tpm = float(np.median(e))                     # texeles por metro
        lod = math.log2(max(tpm / ppm, 1.0)) + float(env.get("PSR_BIAS", "-0.26"))
        N0i = I(m["nrm0"]) @ R.T; N0i /= np.linalg.norm(N0i, axis=1, keepdims=True)
        mt = -np.stack([np.sum(N0i * Tg, 1), np.sum(N0i * B, 1)], 1)   # inclinacion de la malla (variante) en su propio marco
        if env.get("PSR_TILTMAPS") and getattr(S, "tilt", None):   # como el PS: de los mapas (base -> variante con |t|)
            tl = S.tilt["torso" if m["torso"] else "arm"]
            tuv_v = np.clip(uv[:, 1] * 2, 0, 1) if m["torso"] else uv[:, 1]
            mb = sample_mip(tl["b"], uv[:, 0], tuv_v, 0) if S.meshbase else 0.0 * mt
            mv = sample_mip(tl["a" if t > 0 else "r"], uv[:, 0], tuv_v, 0) if t != 0 else mb
            mt = mb + (mv - mb) * abs(t)
            if m["torso"]: mt = mt * (uv[:, 1] < 0.5)[:, None]
        xy = ps_xy(S, m, uv, t, lod, env, mt=mt)
        if env.get("PSR_CLAY"): xy = xy * 0
        z = np.sqrt(np.clip(1 - np.sum(xy * xy, 1, keepdims=True), 0, 1))
        n = Tg * xy[:, :1] + B * xy[:, 1:2] + N * z; n /= np.linalg.norm(n, axis=1, keepdims=True)
        a = bilinear(S.albT if m["torso"] else S.albA, uv[:, 0], uv[:, 1]) ** 2.2
        if env.get("PSR_CLAY"): a = np.full_like(a, 0.42)
        dif = np.clip(n @ L, 0, 1)[:, None]
        sky = (0.5 + 0.5 * n[:, 1:2]) * 0.22
        col = a * (dif * 1.05 + sky)
        img[ii[0][s], ii[1][s]] = np.clip(col, 0, 1) ** (1 / 2.2)
        mag[ii[0][s], ii[1][s]] = np.sqrt(np.sum(xy * xy, 1))
        # inclinacion total respecto de la superficie original (malla de TorsoBody sin el relieve): geometria + textura
        N0 = I(m["nrm0"]) @ R.T; N0 /= np.linalg.norm(N0, axis=1, keepdims=True)
        tilt[ii[0][s], ii[1][s]] = np.degrees(np.arccos(np.clip(np.sum(n * N0, 1), -1, 1)))
        # lo que mostraba TorsoBody: su normal map en su malla (mismo nivel de mip)
        T0 = I(m["tan0"][:, :3]) @ R.T; T0 -= N0 * np.sum(T0 * N0, 1, keepdims=True); T0 /= np.maximum(np.linalg.norm(T0, axis=1, keepdims=True), 1e-9)
        B0 = np.cross(N0, T0) * np.where(I(m["tan0"][:, 3:4]) < 0, -1.0, 1.0)
        xo = sample_mip(S.nmT if m["torso"] else S.nmA, uv[:, 0], uv[:, 1], lod - 1)
        zo = np.sqrt(np.clip(1 - np.sum(xo * xo, 1, keepdims=True), 0, 1))
        no = T0 * xo[:, :1] + B0 * xo[:, 1:2] + N0 * zo; no /= np.linalg.norm(no, axis=1, keepdims=True)
        tilt0[ii[0][s], ii[1][s]] = np.degrees(np.arccos(np.clip(np.sum(no * N0, 1), -1, 1)))
        m["_last"] = (ii[0][s], ii[1][s], n, no)
    if out_xy: return Image.fromarray((img * 255).astype(np.uint8)), mag, tilt, tilt0
    return Image.fromarray((img * 255).astype(np.uint8))

if __name__ == "__main__":
    common, texdir, pkgtex, datadir, outp = sys.argv[1:6]
    t = float(os.environ.get("PSR_T", "1"))
    S = load(common, texdir, pkgtex, datadir, t, meshbase=not os.environ.get("PSR_NOMESHBASE") and not os.environ.get("PSR_ORIG"))
    lt = tuple(float(v) for v in os.environ.get("PSR_LIGHT", "-0.35,0.55,0.76").split(","))
    im = render(S, float(os.environ.get("PSR_YAW", "0")), 760, 940, 1800, 0.0, 1.27, t, os.environ, light=lt,
                tmp=os.path.join(os.environ.get("SIM_TMP", "/tmp"), "_psr%d" % os.getpid()))
    im.save(outp); print("ok", outp)
