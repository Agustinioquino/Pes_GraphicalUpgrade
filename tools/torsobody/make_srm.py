# PES21 FX (26/10 e) - torsob_srm.ftex realista para un torso de TorsoBody con texturas propias (color y normal map nuevos):
#   R = brillo (especular), G = rugosidad, B = 0 (BC1, 4096 x 4096, mips como el original).
# No es un gris parejo (el de TorsoBody: 0.906 / 0.698 en todo el torso = piel de arcilla): sale de la anatomia y de las dos
# texturas:
#   - zonas de la piel real (tools/fox/torso/torso_skin.zones, por posicion en el cuerpo): esternon, entre los omoplatos, arriba
#     de los hombros y espalda baja mas grasos (brillan mas, mas lisos); costados y abdomen mas mates; pezon y ombligo opacos
#   - poros y lineas finas del normal map NUEVO (su banda fina): cortan el brillo y suben la rugosidad (como el PS de la piel)
#   - el color: lunares y manchas oscuras (mas oscuro que su entorno de 5 mm) mas mates y rugosos; zonas mas claras y lisas
#     (sin poros en el normal map) apenas mas satinadas
#   - abajo de v 0.5 (lo que no es el torso) queda el srm original llevado al mismo nivel
# (26/10 e) El torsob_srm solo lo usa la vista ORIGINAL del juego (con PES21 FX la piel real del PS manda: x293): va al nivel de
# TorsoBody (brillo 0.906, rugosidad 0.698 de base, no al de la piel real) y en los 3.5 cm junto a la costura del hombro pasa
# al del brazo de TorsoBody (arm_srm: 0.82 / 0.698), si no, el brazo brillaba distinto y se veia la linea.
# Tambien: add_pores = el normal map propio + los poros y lineas finas del de TorsoBody (el propio casi no tiene: 8 veces menos
# micro relieve que el del brazo; se veia la costura del hombro y la piel quedaba de plastico).
# Uso: python3 make_srm.py <tex de TorsoBody (originales)> <common> <torsob_nrm.ftex nuevo> <torso_N_bsm.ftex nuevo> <salida.ftex> [vista previa.png]
#      python3 make_srm.py --poros <tex de TorsoBody> <common> <torsob_nrm.ftex propio> <salida torsob_nrm.ftex>
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', 'fox')); sys.path.insert(0, os.path.join(HERE, '..', 'fox', 'torso'))
from ftex import Ftex
from make_texture_pack import decode, encode_chain, upsample, pore_factor, DEF_SPEC, DEF_ROUGH

TB_SPEC, TB_ROUGH, ARM_SPEC, ARM_ROUGH = 0.906, 0.698, 0.819, 0.698   # srm de TorsoBody (torso / brazo)

def island_mask(common, H, W, erode=6):
    """texeles adentro de las islas del torso (v 0..0.5 de la textura), sin el borde"""
    from uvbake import bake
    from torso_mesh import load_torso
    from scipy.ndimage import binary_erosion
    T = load_torso(common, "out")
    G = bake(T, None, W=2048, H=1024, vmax=0.5)
    m = np.zeros((H, W), bool)
    m[:H // 2] = upsample(G["mask"].astype(np.float32)[..., None], H // 2, W)[..., 0] > 0.5
    m[:H // 2] = binary_erosion(m[:H // 2], iterations=erode)
    return m

def add_pores(texdir, common, nrm_path, out, k=1.0, sigma=3.0):
    """normal map propio + micro relieve (< ~1 mm) del de TorsoBody, solo adentro de las islas del torso"""
    from scipy.ndimage import gaussian_filter
    Fn = Ftex(open(nrm_path, "rb").read()); Ft = Ftex(open(os.path.join(texdir, "torsob_nrm.ftex"), "rb").read())
    nm = decode(Fn); tb = decode(Ft)
    H, W = nm.shape[:2]
    txy = tb[..., :2] * 2 - 1
    micro = txy - np.stack([gaussian_filter(txy[..., c], sigma) for c in range(2)], -1)
    inside = island_mask(common, H, W).astype(np.float32)
    inside = gaussian_filter(inside, 2.0)
    xy = nm[..., :2] * 2 - 1 + k * micro * inside[..., None]
    l2 = np.sum(xy * xy, -1, keepdims=True); xy = np.where(l2 > 0.94, xy * np.sqrt(0.94 / np.maximum(l2, 1e-9)), xy)
    rgba = np.zeros((H, W, 4), np.float32); rgba[..., :2] = xy * 0.5 + 0.5; rgba[..., 3] = 1
    Ft.mips = encode_chain(Ft, rgba)
    open(out, "wb").write(Ft.to_bytes(chunk=65536, small_first=True))
    own = nm[..., :2] * 2 - 1
    hp = lambda a: (a - np.stack([gaussian_filter(a[..., c], sigma) for c in range(2)], -1))[:H // 2][inside[:H // 2] > 0.5].std()
    return hp(own), hp(xy), hp(txy)

def shoulder_dist(common, H, W):
    """distancia (m) de cada texel del torso (v 0..0.5) a la costura del hombro"""
    from uvbake import bake
    from torso_mesh import load_torso
    from torso_variants import build_arm
    from scipy.spatial import cKDTree
    import torso_seam
    T = load_torso(common, "out")
    A = build_arm(common, "arm")
    ring = torso_seam.shoulder_ring(T, A)
    G = bake(T, None, W=2048, H=1024, vmax=0.5)
    d = np.full(G["mask"].shape, 9.0); ii = np.where(G["mask"])
    d[ii] = cKDTree(ring).query(G["P"][ii])[0]
    full = np.full((H, W), 9.0, np.float32)
    full[:H // 2] = upsample(d[..., None].astype(np.float32), H // 2, W)[..., 0]
    return full

def build(texdir, common, nrm_path, bsm_path, out, preview=None, level="juego"):
    from scipy.ndimage import gaussian_filter
    from uvbake import bake
    from torso_mesh import load_torso
    import torso_skin
    Fn = Ftex(open(nrm_path, "rb").read()); Fb = Ftex(open(bsm_path, "rb").read())
    Fs = Ftex(open(os.path.join(texdir, "torsob_srm.ftex"), "rb").read())
    nm = decode(Fn); al = decode(Fb)
    H, W = nm.shape[:2]
    # ---- zonas por posicion (2048 x 1024 = v 0..0.5, el torso) y su desvanecido en las costuras
    T = load_torso(common, "out")
    from torso_variants import ss
    from torso_mesh import geodesic_from
    fade = ss(0.012, 0.045, geodesic_from(T["Pw"], T["edges"], T["boundary"]))   # costuras: ahi los valores de base
    G = bake(T, [fade[T["inv"][p["off"]: p["off"] + p["nvert"]]][:, None] for p in T["parts"]], W=2048, H=1024, vmax=0.5)
    sk = torso_skin.build_skin(G)                          # (1024, 2048, 4): brillo, rugosidad, translucidez, rubor
    za = upsample(sk[..., :2], H // 2, W)
    # ---- poros y lineas finas del normal map nuevo
    xy = nm[..., :2] * 2 - 1
    hf = pore_factor(xy)                                    # 0..1 (|xy - desenfoque| / 0.12)
    from scipy.ndimage import binary_erosion
    inside = np.zeros((H, W), bool); inside[:H // 2] = upsample(G["mask"].astype(np.float32)[..., None], H // 2, W)[..., 0] > 0.5
    inside[:H // 2] = binary_erosion(inside[:H // 2], iterations=6)
    hf[:H // 2] *= inside[:H // 2]                          # el borde de las islas (salto del mapa al relleno) no son poros
    # ---- el color: lunares / manchas (oscuro respecto de su entorno) y variacion suave
    lum = al[..., :3].mean(-1)
    ratio = lum / np.maximum(gaussian_filter(lum, 12.0), 1e-3)
    dark = np.clip((0.90 - ratio) / 0.25, 0, 1)              # 1 en un lunar
    light = np.clip((ratio - 1.06) / 0.08, 0, 1)             # brillos / zonas claras
    spec = np.empty((H, W), np.float32); rough = np.empty((H, W), np.float32)
    spec[:H // 2] = za[..., 0]; rough[:H // 2] = za[..., 1]
    s0 = decode(Fs)
    spec[H // 2:] = s0[H // 2:, :, 0] * (DEF_SPEC / 0.906); rough[H // 2:] = s0[H // 2:, :, 1] + (DEF_ROUGH - 0.698)
    kh = 0.12 if level == "juego" else 0.35                 # (vista original: el mapa a 4096 tiene poros fuertes, hf ~0.4 de media)
    spec = spec * (1 - kh * hf) * (1 - 0.30 * dark) * (1 + 0.04 * light)
    rough = rough + (kh / 3.5) * hf + 0.12 * dark - 0.02 * light
    if level == "juego":   # (26/10 e) al nivel de TorsoBody (vista original) y al del brazo junto al hombro
        from torso_variants import ss
        # el promedio del torso queda en el de TorsoBody (los poros del mapa a 4096 cortan mucho: hf medio ~0.4)
        ins = island_mask(common, H, W)
        spec = spec * (TB_SPEC / max(float(spec[ins].mean()), 1e-3)); rough = rough + (TB_ROUGH - float(rough[ins].mean()))
        w = 1.0 - ss(0.004, 0.035, shoulder_dist(common, H, W))
        spec = spec + (ARM_SPEC - spec) * w; rough = rough + (ARM_ROUGH - rough) * w
    rgba = np.zeros((H, W, 4), np.float32); rgba[..., 0] = np.clip(spec, 0, 1); rgba[..., 1] = np.clip(rough, 0, 1); rgba[..., 3] = 1
    Fs.mips = encode_chain(Fs, rgba)
    open(out, "wb").write(Fs.to_bytes(chunk=65536, small_first=True))
    if preview:
        from PIL import Image
        im = np.concatenate([rgba[::4, ::4, 0], rgba[::4, ::4, 1]], 1)
        Image.fromarray((np.clip(im, 0, 1) * 255).astype(np.uint8)).save(preview)
    return dict(spec=(float(np.percentile(spec[:H // 2], 1)), float(np.percentile(spec[:H // 2], 99))),
                rough=(float(np.percentile(rough[:H // 2], 1)), float(np.percentile(rough[:H // 2], 99))), hf=float(hf[:H // 2].mean()), dark=float((dark[:H // 2] > 0.5).mean()))

if __name__ == "__main__":
    if sys.argv[1] == "--poros":
        a, b, c = add_pores(*sys.argv[2:6])
        print("poros: micro relieve propio %.4f -> %.4f (TorsoBody %.4f)" % (a, b, c)); sys.exit()
    r = build(*sys.argv[1:6], preview=sys.argv[6] if len(sys.argv) > 6 else None)
    print("srm: brillo %.2f..%.2f, rugosidad %.2f..%.2f, poros medios %.3f, lunares %.3f %%" % (r["spec"] + r["rough"] + (r["hf"], 100 * r["dark"])))
