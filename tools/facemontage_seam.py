#!/usr/bin/env python3
"""PES21 FX - funde los bordes de las texturas de cara generica (montaje FaceSKIN<tono><variante>Montage) con la textura de
la cabeza de su tono (head_color_<w|y|o|r|d|b>), para que la mascara de la cara no marque linea contra el cuello.
El montaje ocupa el rectangulo u 0.1479..0.8521, v 0.1518..0.8563 del plano de la cabeza (medido registrando las
texturas: correlacion 0.96). En el ultimo ~8% hacia el borde se corrige el color de baja frecuencia hacia el de la cabeza
(conserva el detalle de la piel) y en el ultimo ~2% se llega al color de la cabeza. Solo se reescriben los bloques DXT1 que
cambian: el resto queda identico al original.
Uso: facemontage_seam.py <carpeta_face> <salida> """
import sys, os, io, struct
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, map_coordinates
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ftex import Ftex, dxt_to_dds

RECT = (0.1479, 0.1518, 0.7042, 0.7045)   # u0, v0, escala u, escala v
TONE = 'wyordb'

def decode(f, mip=0):
    sub = Ftex.__new__(Ftex); sub.__dict__ = dict(f.__dict__); w, h = f.mip_size(mip)
    sub.w, sub.h = w, h; sub.mips = [f.mips[mip]]
    im = Image.open(io.BytesIO(dxt_to_dds(sub, True))); im.load()
    return np.asarray(im.convert('RGBA')).astype(np.float32)

def encode_dxt1(rgb):
    im = Image.fromarray(np.clip(rgb + 0.5, 0, 255).astype(np.uint8), 'RGB')
    bio = io.BytesIO(); im.save(bio, 'DDS', pixel_format='DXT1')
    b = bio.getvalue(); off = 128 + (20 if b[84:88] == b'DX10' else 0)
    return b[off:]

def smooth(x):
    return x * x * (3 - 2 * x)

def process(mont_path, head_path, out_path, F=0.08, E=0.022, sigma=6.0):
    fm = Ftex(open(mont_path, 'rb').read()); fh = Ftex(open(head_path, 'rb').read())
    assert fm.fmt == 2 and fh.fmt == 2
    M = decode(fm)[..., :3]; H = decode(fh)[..., :3]
    n = fm.w
    c = (np.arange(n) + 0.5) / n
    my, mx = np.meshgrid(c, c, indexing='ij')
    hu = RECT[0] + RECT[2] * mx; hv = RECT[1] + RECT[3] * my
    Hm = np.stack([map_coordinates(H[..., k], [hv * fh.h - 0.5, hu * fh.w - 0.5], order=1, mode='nearest') for k in range(3)], -1)
    d = np.minimum(np.minimum(mx, 1 - mx), np.minimum(my, 1 - my))
    w1 = 1 - smooth(np.clip(d / F, 0, 1)); w2 = 1 - smooth(np.clip(d / E, 0, 1))
    corr = np.stack([gaussian_filter(Hm[..., k], sigma) - gaussian_filter(M[..., k], sigma) for k in range(3)], -1)
    out0 = M + corr * w1[..., None]
    out0 = out0 * (1 - w2[..., None]) + Hm * w2[..., None]
    stats = {}
    # diferencia de color en el borde antes / despues (media en la franja del 2%)
    band = d < 0.02
    stats['borde_antes'] = float(np.abs(M[band] - Hm[band]).mean()); stats['borde_despues'] = float(np.abs(out0[band] - Hm[band]).mean())
    newmips = []
    for i, old in enumerate(fm.mips):
        w, h = fm.mip_size(i)
        if i == 0: nw, ol = out0, M
        else:
            sz = (w, h)
            nw = np.stack([np.asarray(Image.fromarray(out0[..., k]).resize(sz, Image.BOX)) for k in range(3)], -1)
            ol = np.stack([np.asarray(Image.fromarray(M[..., k]).resize(sz, Image.BOX)) for k in range(3)], -1)
        if w < 4 or h < 4:
            newmips.append(old); continue
        enc = encode_dxt1(nw)
        bw, bh = w // 4, h // 4
        diff = np.abs(nw - ol).reshape(bh, 4, bw, 4, 3).max(axis=(1, 3, 4))
        ob = bytearray(old)
        changed = 0
        for by in range(bh):
            for bx in range(bw):
                if diff[by, bx] >= 0.75:
                    k = (by * bw + bx) * 8
                    ob[k:k + 8] = enc[k:k + 8]; changed += 1
        stats['mip%d' % i] = changed
        newmips.append(bytes(ob))
    fm.mips = newmips
    open(out_path, 'wb').write(fm.to_bytes())
    return stats

if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    for t in range(1, 7):
        head = os.path.join(src, 'common', '#windx11', 'head_color_%s.ftex' % TONE[t - 1])
        for v in range(6):
            name = 'FaceSKIN%d0%dMontage.ftex' % (t, v)
            mp = os.path.join(src, 'montage', 'face_skin', '#windx11', name)
            if not os.path.exists(mp): continue
            od = os.path.join(dst, 'montage', 'face_skin', '#windx11'); os.makedirs(od, exist_ok=True)
            st = process(mp, head, os.path.join(od, name))
            print(name, 'borde %.1f -> %.1f' % (st['borde_antes'], st['borde_despues']), 'bloques mip0 %d' % st['mip0'], flush=True)
