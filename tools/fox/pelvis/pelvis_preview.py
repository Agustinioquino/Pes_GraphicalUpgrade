# Vista previa de la pelvis con el torso (base) y los muslos: arcilla o con texturas (solo el torso / brazos texturados; la
# pelvis y los muslos en color de piel parejo). Uso: python3 pelvis_preview.py <common> <tex> <vardir> <pelvis.npz> <salida.png>
import sys, os, math
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', 'torso'))
from PIL import Image, ImageDraw
import preview_nm as pn
from fmdl import Fmdl, meshes

def flat_tex(alb):
    nm = np.full((8, 8, 2), 0.5, np.float32)
    return (nm, nm, alb)

def main():
    common, texdir, vardir, npz, outp = sys.argv[1:6]
    clay = bool(os.environ.get("PNM_CLAY"))
    D = np.load(npz) if npz.endswith(".npz") else None
    sc = pn.load_scene(common, texdir, vardir, 0.0)
    skin = np.full((8, 8, 4), 1.0, np.float32); skin[..., :3] = (0.60, 0.43, 0.35)
    mt = meshes(Fmdl(os.path.join(common, "thigh_short.fmdl")))[0]
    sc.append(pn.Mesh(mt["pos"].astype(np.float64), mt["nrm"][:, :3].astype(np.float64), mt["tan"].astype(np.float64),
                      mt["uv"].astype(np.float64), mt["faces"], flat_tex(skin), None, "arm"))
    if npz.endswith(".fmdl"):   # la pelvis escrita en el fmdl (malla 0), con las texturas del torso (sus uv caen en el cuadrante libre)
        m0 = meshes(Fmdl(npz))[0]
        texT = sc[0].tex
        sc.append(pn.Mesh(m0["pos"].astype(np.float64), m0["nrm"][:, :3].astype(np.float64), m0["tan"].astype(np.float64),
                          m0["uv"].astype(np.float64), m0["faces"], texT, None, "arm"))
    else:
        sc.append(pn.Mesh(D["P"], D["N"], D["T"], D["UV"], D["F"], flat_tex(skin), None, "arm"))
    views = [(0, 0.0, 0.93, 520, 520, 1500, "frente"), (180, 0.0, 0.93, 520, 520, 1500, "atras"), (90, 0.03, 0.93, 520, 520, 1500, "costado"),
             (145, 0.0, 0.93, 520, 520, 1500, "3/4 atras"), (30, 0.0, 0.93, 520, 520, 1500, "3/4 adelante"), (0, 0.0, 0.92, 520, 520, 1500, "desde abajo", -45)]
    tmp = os.path.join(os.environ.get("SIM_TMP", "/tmp"), "_pp%d" % os.getpid())
    ims = []
    lt = tuple(float(v) for v in os.environ["PNM_LIGHT"].split(",")) if os.environ.get("PNM_LIGHT") else (-0.35, 0.55, 0.76)
    for v in views:
        yaw, cx, cy, W, H, ppm, nmv = v[:7]; pitch = v[7] if len(v) > 7 else 0.0
        ims.append((nmv, pn.render(sc, yaw, W, H, ppm, cx, cy, 0.0, tmp, light=lt, pitch=pitch)))
    cols = 3; rows = (len(ims) + cols - 1) // cols
    W, H = ims[0][1].size
    sheet = Image.new("RGB", (W * cols, (H + 24) * rows), (24, 24, 24)); d = ImageDraw.Draw(sheet)
    for i, (nm, im) in enumerate(ims):
        x, y = (i % cols) * W, (i // cols) * (H + 24)
        d.text((x + 8, y + 6), nm, fill=(230, 230, 230)); sheet.paste(im, (x, y + 24))
    sheet.save(outp); print("ok", outp)

if __name__ == "__main__":
    main()
