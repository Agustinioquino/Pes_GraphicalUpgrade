#!/usr/bin/env python3
"""Gradua lut_Match.ftex (LUT 3D 33^3 RGBA16F del estudio/menus, dt00 Asset/model/light/#windx11) sin mover el blanco ni el negro.
Subida de saturacion, curva de contraste suave centrada en el gris medio y calidez opcional. No toca tamanos: sirve por livecpk.
uso: python lut_grade.py <lut_Match.ftex original> <salida.ftex> [sat=1.10] [contraste=0.12] [calidez=0.0]
"""
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ftex import Ftex

def main():
    if len(sys.argv) < 3: print(__doc__); return 1
    src, dst = sys.argv[1:3]
    sat = float(sys.argv[3]) if len(sys.argv) > 3 else 1.10
    con = float(sys.argv[4]) if len(sys.argv) > 4 else 0.12
    warm = float(sys.argv[5]) if len(sys.argv) > 5 else 0.0
    f = Ftex(open(src, 'rb').read())
    assert (f.fmt, f.w, f.h, f.d) == (12, 33, 33, 33), 'no es el lut_Match esperado'
    a = np.frombuffer(f.mips[0], np.float16).astype(np.float32).reshape(33, 33, 33, 4).copy()
    rgb = a[..., :3]
    lo, hi = rgb[0, 0, 0].copy(), rgb[32, 32, 32].copy()
    t = np.clip((rgb - lo) / (hi - lo), 0, 1)            # 0..1 entre negro y blanco originales
    t = t + con * (t - 0.5) * (1 - np.abs(2 * t - 1)) * 2 # S-curve: extremos fijos
    y = (t * np.array([0.2126, 0.7152, 0.0722], np.float32)).sum(-1, keepdims=True)
    t = y + (t - y) * sat
    t = t * (1 + warm * np.array([0.06, 0.0, -0.06], np.float32))
    out = np.clip(lo + t * (hi - lo), 0, 1)
    a[..., :3] = out
    f.mips[0] = a.astype(np.float16).tobytes()
    open(dst, 'wb').write(f.to_bytes(chunk=65536, small_first=True))
    print('ok', dst, 'sat', sat, 'contraste', con, 'calidez', warm)
    return 0

if __name__ == '__main__':
    sys.exit(main())
