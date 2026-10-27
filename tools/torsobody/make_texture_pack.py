# PES21 FX (26/10 e) - paquete de texturas de TorsoBody para compartir (torso, brazos, muslos), en .ftex (listas para el juego) y
# .dds (para abrir y editar):
#   originales/   las .ftex que usa el juego hoy (TorsoBody v11 + brazos y piernas de la v12.6), tal cual, y su .dds
#   mejoradas/    los mapas que cambian para usarlas SIN la DLL de PES21 FX (lo que el shader de PES21 FX hace en vivo, horneado):
#     torsob_nrm, arm_nrm, thigh_nrm   relieve al derecho para el juego (el verde de TorsoBody estaba invertido: los musculos se
#                                      veian iluminados desde abajo), columna marcada en el torso (surco y erectores), union suave
#                                      del brazo con el torso en el hombro y tope suave de la inclinacion (sin manchas negras)
#     torsob_srm, arm_srm, thigh_srm   piel real: brillo y rugosidad por zona (esternon, omoplatos, hombros y espalda baja mas
#                                      grasos, costados mates; codo, adentro del brazo, muneca...) y los poros cortando el brillo
#     torsob_trm, arm_trm              translucidez por zona (mas en la carne, menos sobre hueso), relativa a la de TorsoBody
#   Los albedos (torso_N_bsm, arm_N_bsm, thigh_N_bsm) no cambian: son los de originales/.
# Las texturas son de TorsoBody (con partes del juego): el paquete NO va al repo, se arma en la PC del usuario con sus archivos.
# Uso: python3 make_texture_pack.py <carpeta tex de TorsoBody> <carpeta common de TorsoBody> <carpeta textures de PES21FX> <salida>
import sys, os, io, struct, math
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', 'fox')); sys.path.insert(0, os.path.join(HERE, '..', 'fox', 'torso'))
from ftex import Ftex
from texio import dds_dx10_header

DXGI = {2: 71, 9: 83, 11: 98}            # formato FTEX -> DXGI: BC1, BC5, BC7 (UNORM)
BLOCK = {2: 8, 9: 16, 11: 16}
NAMES = (["torso_%d_bsm" % i for i in range(1, 7)] + ["torsob_nrm", "torsob_srm", "torsob_trm"] +
         ["arm_%d_bsm" % i for i in range(1, 7)] + ["arm_nrm", "arm_srm", "arm_trm"] +
         ["thigh_%d_bsm" % i for i in range(1, 7)] + ["thigh_nrm", "thigh_srm"])
DEF_SPEC, DEF_ROUGH, DEF_TRANS = 0.80, 0.66, 0.30   # = torso_skin.DEF (piel real de PES21 FX)

def ftex_to_dds(F):
    """DDS DX10 con la cadena de mips tal cual (los bloques BCn del ftex)"""
    pitch = (max(1, F.w // 4)) * BLOCK[F.fmt] * max(1, F.h // 4)
    return dds_dx10_header(F.w, F.h, len(F.mips), DXGI[F.fmt], pitch) + b"".join(F.mips)

def decode(F, mip=0):
    """mip -> float32 (H, W, 4) en 0..1 (Pillow lee BC1 / BC5 / BC7 por DDS)"""
    from PIL import Image
    w, h = F.mip_size(mip)
    dds = dds_dx10_header(w, h, 1, DXGI[F.fmt], len(F.mips[mip])) + F.mips[mip]
    im = Image.open(io.BytesIO(dds)); im.load()
    return np.asarray(im.convert("RGBA"), dtype=np.float32) / 255.0

def encode_chain(F, rgba0, gamma_mips=False):
    """rgba0 (H, W, 4) float 0..1 -> mips nuevas del mismo formato que F (caja 2 x 2 para bajar), con etcpak"""
    import etcpak
    mips = []
    cur = rgba0.astype(np.float32)
    for i in range(len(F.mips)):
        w, h = F.mip_size(i)
        if i > 0:
            cur = 0.25 * (cur[0::2, 0::2] + cur[1::2, 0::2] + cur[0::2, 1::2] + cur[1::2, 1::2])
        assert cur.shape[0] == h and cur.shape[1] == w, (i, cur.shape, w, h)
        u8 = np.ascontiguousarray(np.clip(np.round(cur * 255.0), 0, 255).astype(np.uint8))
        if F.fmt == 9: blk = etcpak.compress_bc5(u8.tobytes(), w, h)
        elif F.fmt == 2: blk = etcpak.compress_bc1(u8.tobytes(), w, h)
        elif F.fmt == 11: blk = etcpak.compress_bc7(u8.tobytes(), w, h, None)
        else: raise ValueError(F.fmt)
        assert len(blk) == len(F.mips[i]), (i, len(blk), len(F.mips[i]))
        mips.append(bytes(blk))
    return mips

def upsample(img, H, W):
    """(h, w, k) -> (H, W, k) bilineal (centros de texel)"""
    h, w = img.shape[:2]
    ys = (np.arange(H) + 0.5) * h / H - 0.5; xs = (np.arange(W) + 0.5) * w / W - 0.5
    y0 = np.clip(np.floor(ys).astype(int), 0, h - 1); y1 = np.clip(y0 + 1, 0, h - 1); fy = np.clip(ys - y0, 0, 1)[:, None, None]
    x0 = np.clip(np.floor(xs).astype(int), 0, w - 1); x1 = np.clip(x0 + 1, 0, w - 1); fx = np.clip(xs - x0, 0, 1)[None, :, None]
    a = img[y0][:, x0]; b = img[y0][:, x1]; c = img[y1][:, x0]; d = img[y1][:, x1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy

def read_bc5_snorm_dds(path):
    """mip 0 de un DDS BC5_SNORM de PES21 FX (pes_var_*) -> (H, W, 2) en -1..1"""
    from texio import decode_bc4_snorm
    d = open(path, "rb").read()
    h, w = struct.unpack_from("<II", d, 12)
    nb = (h // 4) * (w // 4)
    blk = np.frombuffer(d, np.uint8, nb * 16, 148).reshape(nb, 16)
    return np.stack([decode_bc4_snorm(blk[:, :8], h, w), decode_bc4_snorm(blk[:, 8:], h, w)], -1).astype(np.float32)

def soft_limit(xy, knee=0.453, lim=0.755, sigma=4.0):
    """tope suave de la inclinacion (= x307 del PS con 49 grados, sin malla esculpida: solo la textura). En el juego el tope
    actua sobre el normal map ya filtrado (mip 1..3 a la distancia de siempre); aca, a 4096, los poros y las lineas finas (la
    banda alta, sobre todo en los muslos) pesan mucho: se limita solo la parte baja (desenfoque de 4 texeles = ~mip 2) y la alta
    vuelve igual"""
    from scipy.ndimage import gaussian_filter
    lo = np.stack([gaussian_filter(xy[..., k], sigma) for k in range(2)], -1)
    hi = xy - lo
    s = np.sqrt(np.sum(lo * lo, -1, keepdims=True))
    f = np.where(s <= knee, s, knee + (lim - knee) * np.tanh((s - knee) / (lim - knee)))
    out = lo * np.where(s > 1e-6, f / np.maximum(s, 1e-6), 1.0) + hi
    l2 = np.sum(out * out, -1, keepdims=True)
    return np.where(l2 > 0.94, out * np.sqrt(0.94 / np.maximum(l2, 1e-9)), out)

def pore_factor(xy, sigma=3.4):
    """hf del PS de la piel: |normal map - su desenfoque (mip +2.5)| / 0.12 (poros y lineas finas)"""
    from scipy.ndimage import gaussian_filter
    bl = np.stack([gaussian_filter(xy[..., k], sigma) for k in range(2)], -1)
    return np.clip(np.sqrt(np.sum((xy - bl) ** 2, -1)) / 0.12, 0, 1)

def arm_bake(common, W=2048):
    """en el atlas del brazo (W x W): islas del brazo suavizadas (1 = brazo, 0 = manos u otra cosa) y el factor de la union con
    el hombro (w293 del PS: el relieve del brazo baja al 55 % en los 3 cm junto a la costura)"""
    from torso_mesh import load_parts
    from uvbake import bake
    from scipy.ndimage import binary_dilation, gaussian_filter
    T = load_parts(common, [("arm", 0)])
    G = bake(T, None, W=W, H=W, vmax=1.0, tmp=os.path.join(os.environ.get("SIM_TMP", "/tmp"), "_pack%d" % os.getpid()))
    M, P = G["mask"], G["P"]
    soft = gaussian_filter(binary_dilation(M, iterations=3).astype(np.float32), 1.5)
    sA = (np.abs(P[..., 0]) - 0.2631) * 0.7611 + (P[..., 1] - 1.3704) * (-0.6432) + (0.02 - 0.0195) * (-0.0839)
    t = np.clip(sA / 0.03, 0, 1)
    k = np.where(M & (np.abs(P[..., 0]) > 0.20) & (P[..., 1] > 1.15), 0.55 + 0.45 * t * t * (3 - 2 * t), 1.0)
    k = gaussian_filter(k.astype(np.float32), 0.7)
    return soft.astype(np.float32), k.astype(np.float32)

def main():
    texdir, common, pkgtex, out = sys.argv[1:5]
    o_ftex = os.path.join(out, "originales", "ftex"); o_dds = os.path.join(out, "originales", "dds")
    m_ftex = os.path.join(out, "mejoradas", "ftex"); m_dds = os.path.join(out, "mejoradas", "dds")
    for d in (o_ftex, o_dds, m_ftex, m_dds): os.makedirs(d, exist_ok=True)
    F = {}
    # ---- originales
    for nm in NAMES:
        raw = open(os.path.join(texdir, nm + ".ftex"), "rb").read()
        F[nm] = Ftex(raw)
        open(os.path.join(o_ftex, nm + ".ftex"), "wb").write(raw)
        open(os.path.join(o_dds, nm + ".dds"), "wb").write(ftex_to_dds(F[nm]))
        print("original %-12s %dx%d fmt %d" % (nm, F[nm].w, F[nm].h, F[nm].fmt))
    def save(nm, mips):
        G = Ftex(open(os.path.join(texdir, nm + ".ftex"), "rb").read())
        G.mips = mips
        open(os.path.join(m_ftex, nm + ".ftex"), "wb").write(G.to_bytes(chunk=65536, small_first=True))   # como las del juego
        open(os.path.join(m_dds, nm + ".dds"), "wb").write(ftex_to_dds(G))
    # ---- normal maps (convencion de la textura: x = R, y = G; se suman en -1..1 y se invierte el verde al final)
    hf = {}
    soft_arm2k, union2k = arm_bake(common, 2048)   # islas del brazo (1) / manos (0); factor de la union con el hombro
    for nm in ("torsob_nrm", "arm_nrm", "thigh_nrm"):
        a = decode(F[nm]); H, W = a.shape[:2]
        xy = a[..., :2] * 2 - 1
        hf[nm] = pore_factor(xy)
        if nm == "torsob_nrm":   # columna marcada (x288 = 1): mapa del torso v 0..0.5
            sp = read_bc5_snorm_dds(os.path.join(pkgtex, "pes_var_torso_spine_n.dds"))
            xy[:H // 2] += upsample(sp, H // 2, W)
        if nm == "arm_nrm":      # union con el torso (w293): junto a la costura del hombro el relieve del brazo baja al 55 %
            xy *= upsample(union2k[..., None], H, W)
        l2 = np.sum(xy * xy, -1, keepdims=True); xy = np.where(l2 > 0.94, xy * np.sqrt(0.94 / np.maximum(l2, 1e-9)), xy)
        xy = soft_limit(xy)
        xy[..., 1] = -xy[..., 1]                     # verde a la convencion del juego (x286)
        rgba = np.zeros((H, W, 4), np.float32); rgba[..., 0] = xy[..., 0] * 0.5 + 0.5; rgba[..., 1] = xy[..., 1] * 0.5 + 0.5; rgba[..., 3] = 1.0
        save(nm, encode_chain(F[nm], rgba))
        print("mejorada %s: |xy| p99 %.2f" % (nm, np.percentile(np.sqrt(np.sum(xy * xy, -1)), 99)))
    # ---- srm (R = brillo, G = rugosidad) y trm (gris = translucidez): zonas de la piel real + poros
    def zones(name, H, W):
        """(H, W, 2) de pes_var_<name>_skin_a / _b (BC5 SNORM pero en 0..1) en el tamano pedido"""
        a = read_bc5_snorm_dds(os.path.join(pkgtex, "pes_var_%s_skin_a.dds" % name))
        b = read_bc5_snorm_dds(os.path.join(pkgtex, "pes_var_%s_skin_b.dds" % name))
        return a, b
    # torso: zonas en v 0..0.5; abajo (no hay torso) el original llevado al nivel de la piel real
    s = decode(F["torsob_srm"]); H, W = s.shape[:2]
    za, zb = zones("torso", H, W)
    za = upsample(za, H // 2, W); zb = upsample(zb, H // 2, W)
    h = hf["torsob_nrm"]
    new = s.copy()
    new[..., 0] = s[..., 0] * (DEF_SPEC / 0.906); new[..., 1] = s[..., 1] + (DEF_ROUGH - 0.698)
    new[:H // 2, :, 0] = za[..., 0] * (1 - 0.35 * h[:H // 2])
    new[:H // 2, :, 1] = za[..., 1] + 0.10 * h[:H // 2]
    save("torsob_srm", encode_chain(F["torsob_srm"], np.clip(new, 0, 1)))
    t = decode(F["torsob_trm"]); Ht, Wt = t.shape[:2]
    ztr = np.full((Ht, Wt), DEF_TRANS, np.float32); ztr[:Ht // 2] = upsample(zb[..., :1], Ht // 2, Wt)[..., 0]
    tr = np.clip(ztr * t[..., 1] / 0.143, 0, 1)
    newt = t.copy(); newt[..., 0] = newt[..., 1] = newt[..., 2] = tr
    save("torsob_trm", encode_chain(F["torsob_trm"], newt))
    print("mejorada torsob_srm / torsob_trm: brillo %.2f..%.2f, rugosidad %.2f..%.2f, translucidez %.2f..%.2f" % (
        np.percentile(new[..., 0], 1), np.percentile(new[..., 0], 99), np.percentile(new[..., 1], 1), np.percentile(new[..., 1], 99),
        np.percentile(tr, 1), np.percentile(tr, 99)))
    # brazos: zonas en las islas del brazo; las manos quedan como TorsoBody (unas brillantes, palma mate)
    s = decode(F["arm_srm"]); H, W = s.shape[:2]
    za, zb = zones("arm", H, W)
    za = upsample(za, H, W); h = hf["arm_nrm"]
    k = upsample(soft_arm2k[..., None], H, W)
    zs = np.stack([za[..., 0] * (1 - 0.35 * h), za[..., 1] + 0.10 * h], -1)
    new = s.copy(); new[..., :2] = s[..., :2] * (1 - k) + zs * k
    save("arm_srm", encode_chain(F["arm_srm"], np.clip(new, 0, 1)))
    t = decode(F["arm_trm"]); Ht, Wt = t.shape[:2]
    zt = upsample(zb[..., :1], Ht, Wt)[..., 0]
    kt = upsample(soft_arm2k[..., None], Ht, Wt)[..., 0]
    ztr = DEF_TRANS * (1 - kt) + zt * kt
    tr = np.clip(ztr * t[..., 1] / 0.143, 0, 1)
    newt = t.copy(); newt[..., 0] = newt[..., 1] = newt[..., 2] = tr
    save("arm_trm", encode_chain(F["arm_trm"], newt))
    print("mejorada arm_srm / arm_trm")
    # muslos: el srm de la v12.6 llevado al nivel de la piel real (mismo detalle)
    s = decode(F["thigh_srm"])
    new = s.copy(); new[..., 0] = s[..., 0] * (DEF_SPEC / 0.839); new[..., 1] = s[..., 1] + (DEF_ROUGH - 0.620)
    save("thigh_srm", encode_chain(F["thigh_srm"], np.clip(new, 0, 1)))
    print("mejorada thigh_srm")

if __name__ == "__main__":
    main()
