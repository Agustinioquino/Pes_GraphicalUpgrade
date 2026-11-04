# PES21 FX (04/11) - vista previa de la pulsera (wristtaping_l.fmdl + texturas) sobre el brazo y la mano comunes, en la pose
# del modelo. Rasterizado por software (numpy): color de la textura, normal map con la tangente del fmdl (binormal =
# cross(N, T), convencion de Konami), luz difusa + brillo segun el srm.
# Uso: python3 preview.py <wristtaping_l.fmdl> <carpeta #windx11 con pulsera_*.ftex> <arm.fmdl> <hand_l.fmdl> <salida.png>
import sys, os, io
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', '..'))
import fmdl as F_
from ftex import Ftex, dxt_to_dds
from PIL import Image

def tex(p):
    f = Ftex(open(p, 'rb').read())
    return np.asarray(Image.open(io.BytesIO(dxt_to_dds(f, True))).convert('RGBA'), np.float32) / 255

def render(tris, eye, at, up, W=900, H=900, fov=0.20, light=None):
    f = at - eye; f /= np.linalg.norm(f); r = np.cross(f, up); r /= np.linalg.norm(r); u = np.cross(r, f)
    img = np.ones((H, W, 3)) * np.array([0.16, 0.17, 0.19]); zb = np.full((H, W), np.inf)
    light = light if light is not None else -f * 0.5 + u * 0.7 + r * 0.3
    light = light / np.linalg.norm(light)
    for obj in tris:
        P = obj['pos']; Fc = obj['faces']
        d = P - eye; z = d @ f; x = d @ r / (z * fov); y = d @ u / (z * fov)
        sx = (x * 0.5 + 0.5) * W; sy = (0.5 - y * 0.5) * H
        for t in Fc:
            a, b, c = t
            if min(z[a], z[b], z[c]) <= 0.01: continue
            xs = np.array([sx[a], sx[b], sx[c]]); ys = np.array([sy[a], sy[b], sy[c]])
            x0, x1 = int(max(0, np.floor(xs.min()))), int(min(W - 1, np.ceil(xs.max())))
            y0, y1 = int(max(0, np.floor(ys.min()))), int(min(H - 1, np.ceil(ys.max())))
            if x0 > x1 or y0 > y1: continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
            if abs(den) < 1e-9: continue
            w0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / den
            w1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / den
            w2 = 1 - w0 - w1
            ins = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not ins.any(): continue
            zz = w0 * z[a] + w1 * z[b] + w2 * z[c]
            sub = zb[y0:y1 + 1, x0:x1 + 1]
            ok = ins & (zz < sub)
            if not ok.any(): continue
            W3 = np.stack([w0[ok], w1[ok], w2[ok]], 1)
            n = W3 @ obj['nrm'][[a, b, c]]; n /= np.linalg.norm(n, axis=1, keepdims=True)
            if 'uv' in obj:
                uv = W3 @ obj['uv'][[a, b, c]]
                tg = W3 @ obj['tan'][[a, b, c]]; tg -= n * (tg * n).sum(1, keepdims=True); tg /= np.linalg.norm(tg, axis=1, keepdims=True) + 1e-9
                bt = np.cross(n, tg)
                ti = np.clip((uv[:, 1] * obj['bsm'].shape[0]).astype(int), 0, obj['bsm'].shape[0] - 1)
                tj = np.clip((uv[:, 0] * obj['bsm'].shape[1]).astype(int), 0, obj['bsm'].shape[1] - 1)
                alb = obj['bsm'][ti, tj, :3] ** 2.2
                nm = obj['nrm_t'][ti, tj]; nx = nm[:, 3] * 2 - 1; ny = nm[:, 1] * 2 - 1; nz_ = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0, 1))
                n = tg * nx[:, None] + bt * ny[:, None] + n * nz_[:, None]; n /= np.linalg.norm(n, axis=1, keepdims=True)
                spec = obj['srm'][ti, tj, 0]; rough = obj['srm'][ti, tj, 1]
            else:
                alb = np.broadcast_to(obj['color'], (len(n), 3)); spec = np.full(len(n), 0.3); rough = np.full(len(n), 0.6)
            v = -(f); hv = light + v; hv /= np.linalg.norm(hv)
            dif = np.clip(n @ light, 0, 1) * 0.85 + 0.18 * (0.5 + 0.5 * n @ u)
            sh = 2 / np.maximum(rough, 0.05) ** 4
            sp = spec * 0.25 * np.clip(n @ hv, 0, 1) ** sh * (sh + 2) / 8 * np.clip(n @ light, 0, 1)
            col = alb * dif[:, None] + sp[:, None] * 0.4
            sub[ok] = zz[ok]
            blk = img[y0:y1 + 1, x0:x1 + 1]; blk[ok] = col ** (1 / 2.2)
    return np.clip(img, 0, 1)

def main():
    fm, texdir, arm_p, hand_p, out = sys.argv[1:6]
    B = F_.Fmdl(fm); m = F_.meshes(B)[0]
    obj = dict(pos=m['pos'].astype(np.float64), faces=m['faces'], nrm=m['nrm'][:, :3].astype(np.float64),
               tan=m['tan'][:, :3].astype(np.float64), uv=m['uv'].astype(np.float64),
               bsm=tex(os.path.join(texdir, 'pulsera_bsm.ftex')), nrm_t=tex(os.path.join(texdir, 'pulsera_nrm.ftex')),
               srm=tex(os.path.join(texdir, 'pulsera_srm.ftex')))
    skin = []
    for p in (arm_p, hand_p):
        A = F_.Fmdl(p)
        for q in F_.meshes(A):
            if q['faces'] is None or q['nrm'] is None: continue
            P = q['pos'].astype(np.float64)
            keep = np.linalg.norm(P - obj['pos'].mean(0), axis=1) < 0.12
            fc = q['faces'][keep[q['faces']].all(1)]
            if len(fc): skin.append(dict(pos=P, faces=fc, nrm=q['nrm'][:, :3].astype(np.float64), color=np.array([0.62, 0.45, 0.36])))
    c = obj['pos'].mean(0)
    # eje (pca) y lados
    d = obj['pos'] - c; ev, evec = np.linalg.eigh(d.T @ d); ax = evec[:, 0]
    if ax[1] > 0: ax = -ax
    th = np.array([0, 0, 1.0]) - ax * ax[2]; th /= np.linalg.norm(th); palm = np.cross(ax, th)
    views = [('dorso', -palm), ('palma (cierre)', palm), ('costado pulgar', th), ('cerca del cierre', palm * 0.8 + th * 0.35)]
    ims = []
    for name, dirv in views:
        dist = 0.16 if 'cerca' not in name else 0.09
        eye = c + dirv / np.linalg.norm(dirv) * dist + ax * 0.01
        up = -ax if 'cerca' not in name else -ax
        im = render([obj] + skin, eye, c, up, W=700, H=700, fov=0.32)
        ims.append(Image.fromarray((im * 255).astype(np.uint8)))
    W = Image.new('RGB', (1400, 1400))
    for i, im in enumerate(ims): W.paste(im, ((i % 2) * 700, (i // 2) * 700))
    W.save(out)
    print(out)

if __name__ == '__main__':
    main()
