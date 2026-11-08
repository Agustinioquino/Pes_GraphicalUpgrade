#!/usr/bin/env python3
"""Base del cesped (PES21FX\\textures\\pasto_editor\\vs4.dds, 1024x1024 BGRA) a 2048x2048 sin costuras, con detalle fino sintetizado
(ruido fractal periodico con el mismo espectro de la textura original) + variacion de color a escala media (parches mas secos /
mas verdes) y brillo por hoja. Conserva la cabecera DDS, el orden de canales y el color medio; el alfa se sube y se le suma el mismo detalle.
uso: python pasto_base_hd.py <vs4.dds> <salida.dds> [tam=2048] [detalle=1.0] [parches=1.0] [semilla=7]
"""
import sys, struct
import numpy as np
from PIL import Image

def fnoise(n, rng, slope):
    """ruido periodico (envuelve en los bordes) con espectro 1/f^slope, media 0, desvio 1"""
    f = np.fft.fftfreq(n)
    r = np.sqrt(f[:, None] ** 2 + f[None, :] ** 2); r[0, 0] = 1
    s = np.fft.fft2(rng.standard_normal((n, n))) / r ** slope
    s[0, 0] = 0
    x = np.real(np.fft.ifft2(s)); return (x - x.mean()) / x.std()

def main():
    if len(sys.argv) < 3: print(__doc__); return 1
    src, dst = sys.argv[1:3]
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 2048
    det = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
    pat = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0
    rng = np.random.default_rng(int(sys.argv[6]) if len(sys.argv) > 6 else 7)
    raw = open(src, 'rb').read(); hdr = bytearray(raw[:128])
    w = struct.unpack('<I', hdr[16:20])[0]; h = struct.unpack('<I', hdr[12:16])[0]
    a = np.frombuffer(raw[128:128 + w * h * 4], np.uint8).reshape(h, w, 4).astype(np.float32)
    # subir de tamano con wrap (rellenar para que el bicubico no cree costura)
    pad = 8
    ap = np.pad(a, ((pad, pad), (pad, pad), (0, 0)), mode='wrap')
    big = np.stack([np.asarray(Image.fromarray(np.clip(ap[..., c], 0, 255).astype(np.uint8)).resize(
        ((w + 2 * pad) * n // w, (h + 2 * pad) * n // h), Image.BICUBIC), np.float32) for c in range(4)], -1)
    p2 = pad * n // w; big = big[p2:p2 + n, p2:p2 + n]
    mean = big.reshape(-1, 4).mean(0)
    # detalle fino: banda alta (hojas), solo en luminancia + un poco de verde/amarillo
    fine = fnoise(n, rng, 0.6) * 0.6 + fnoise(n, rng, 1.2) * 0.4
    mid = fnoise(n, rng, 1.8)                  # parches de media escala (decenas de cm)
    macro = fnoise(n, rng, 2.4)                # variacion lenta
    lum = 1 + 0.09 * det * fine + 0.05 * pat * mid
    out = big * lum[..., None]
    # parches secos (mas amarillos/rojizos = mas R, menos G) y frescos
    dry = np.clip(mid * 0.6 + macro * 0.6, -2, 2) * pat
    out[..., 2] *= 1 + 0.05 * dry          # R (orden BGRA)
    out[..., 1] *= 1 - 0.025 * dry         # G
    out[..., 0] *= 1 - 0.04 * dry          # B
    out[..., 3] = big[..., 3] + 14 * det * fine + 8 * pat * mid
    out = np.clip(out, 0, 255)
    # recentrar medias al original
    out[..., :3] *= (mean[:3] / out[..., :3].reshape(-1, 3).mean(0))
    out[..., 3] += mean[3] - out[..., 3].mean()
    out = np.clip(np.rint(out), 0, 255).astype(np.uint8)
    struct.pack_into('<II', hdr, 12, n, n); struct.pack_into('<I', hdr, 20, n * 4)
    open(dst, 'wb').write(bytes(hdr) + out.tobytes())
    print('ok', dst, n, 'medias', out.reshape(-1, 4).mean(0).round(1), 'orig', mean.round(1))
    return 0

if __name__ == '__main__':
    sys.exit(main())
