#!/usr/bin/env python3
"""PES21 FX - publico mejorado, solo texturas (livecpk, sin DLL).
uso: mejorar_publico.py <carpeta dt00 extraida (con Asset/...)> <salida> 
- au_h_face_bsm / _alp: atlas de caras 1024x2048 -> 2048x4096 (Lanczos + nitidez suave), DXT1 / DXT5 con mips
- au_h_nrm 1024 -> 2048, au_h_srm 512 -> 1024
- au_h_col_bsm / au_l_col_bsm (paleta de ropa 128x32): mas saturacion y mas colores vivos (filas 0..25; las 6 ultimas, colores fijos, no se tocan)
No toca la boca (son texturas del publico)."""
import sys, os, io, colorsys, random
import numpy as np
from PIL import Image, ImageFilter
import etcpak
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from ftex import Ftex, dxt_to_dds
REL = 'Asset/model/bg/common/audi/sourceimages/#windx11/'

def load(f):
    if f.fmt == 0: return Image.frombytes('RGBA', (f.w, f.h), f.mips[0], 'raw', 'BGRA')
    return Image.open(io.BytesIO(dxt_to_dds(f, True))).convert('RGBA')

def mips_of(im, fmt):
    out = []; cur = im
    while True:
        w, h = cur.size
        if fmt in (2, 4):
            pw, ph = max(4, (w + 3) // 4 * 4), max(4, (h + 3) // 4 * 4)
            pad = cur if (pw, ph) == (w, h) else cur.resize((pw, ph), Image.NEAREST)
            rgba = pad.convert('RGBA').tobytes()
        if fmt == 2: out.append(bytes(etcpak.compress_bc1(rgba, pw, ph)))
        elif fmt == 4: out.append(bytes(etcpak.compress_bc3(rgba, pw, ph)))
        else: out.append(cur.convert('RGBA').tobytes('raw', 'BGRA'))
        if (max(w, h) <= 4 if fmt in (2, 4) else min(w, h) == 1): break
        cur = cur.resize((max(1, w // 2), max(1, h // 2)), Image.LANCZOS)
    return out

def build(src, im, fmt):
    f = Ftex.__new__(Ftex)
    f.header = bytearray(src.header)
    f.fmt, f.w, f.h, f.d = fmt, im.size[0], im.size[1], 1
    f.mips = mips_of(im, fmt); f.mipCount = len(f.mips)
    return f.to_bytes(chunk=65536, small_first=True)

def up(im, k, sharp=True):
    r = im.resize((im.size[0] * k, im.size[1] * k), Image.LANCZOS)
    if sharp: r = r.filter(ImageFilter.UnsharpMask(radius=1.6, percent=60, threshold=2))
    return r

def paleta(im, rng):
    a = np.array(im.convert('RGB')).astype(np.float32) / 255
    out = a.copy()
    for y in range(0, 26):
        for x in range(a.shape[1]):
            r, g, b = a[y, x]
            h, s, v = colorsys.rgb_to_hsv(r, g, b)
            if s < 0.18 and 0.12 < v < 0.85 and rng.random() < 0.18:   # grises apagados -> color vivo
                h = rng.random(); s = rng.uniform(0.35, 0.65); v = rng.uniform(0.4, 0.8)
            else:
                s = min(1, s * 1.2 + (0.04 if s > 0.15 else 0)); v = min(1, v * 1.05)
            out[y, x] = colorsys.hsv_to_rgb(h, s, v)
    res = Image.fromarray((out * 255 + 0.5).astype(np.uint8), 'RGB').convert('RGBA')
    res.putalpha(im.split()[3]); return res

def main(root, out):
    src_dir = os.path.join(root, REL); dst = os.path.join(out, REL); os.makedirs(dst, exist_ok=True)
    rng = random.Random(2021)
    jobs = {'au_h_face_bsm': lambda im: up(im, 2), 'au_h_face_bsm_alp': lambda im: up(im, 2),
            'au_h_nrm': lambda im: up(im, 2, False), 'au_h_srm': lambda im: up(im, 2, False),
            'au_h_col_bsm_rgba32': lambda im: paleta(im, rng), 'au_l_col_bsm_rgba32': lambda im: paleta(im, rng)}
    for n, fn in jobs.items():
        src = Ftex(open(os.path.join(src_dir, n + '.ftex'), 'rb').read())
        im = fn(load(src)); data = build(src, im, src.fmt)
        open(os.path.join(dst, n + '.ftex'), 'wb').write(data)
        Ftex(data)   # verifica que se relee
        im.convert('RGB').save(os.path.join(out, n + '_vista.png'))
        print(n, src.w, src.h, '->', im.size, len(data))

if __name__ == '__main__': main(sys.argv[1], sys.argv[2])
