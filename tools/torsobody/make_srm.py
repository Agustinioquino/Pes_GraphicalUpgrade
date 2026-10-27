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
# Uso: python3 make_srm.py <tex de TorsoBody (originales)> <common> <torsob_nrm.ftex nuevo> <torso_N_bsm.ftex nuevo> <salida.ftex> [vista previa.png]
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', 'fox')); sys.path.insert(0, os.path.join(HERE, '..', 'fox', 'torso'))
from ftex import Ftex
from make_texture_pack import decode, encode_chain, upsample, pore_factor, DEF_SPEC, DEF_ROUGH

def build(texdir, common, nrm_path, bsm_path, out, preview=None):
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
    spec = spec * (1 - 0.35 * hf) * (1 - 0.30 * dark) * (1 + 0.04 * light)
    rough = rough + 0.10 * hf + 0.12 * dark - 0.02 * light
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
    r = build(*sys.argv[1:6], preview=sys.argv[6] if len(sys.argv) > 6 else None)
    print("srm: brillo %.2f..%.2f, rugosidad %.2f..%.2f, poros medios %.3f, lunares %.3f %%" % (r["spec"] + r["rough"] + (r["hf"], 100 * r["dark"])))
