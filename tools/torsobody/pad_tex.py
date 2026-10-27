# PES21 FX (27/10) - relleno de las texturas fuera de las islas de UV con el color del texel de adentro mas cercano. Al retocar las
# texturas (seam_tex, tonos, definicion) el relleno viejo quedaba con otro color / otra normal: en los mips mas chicos (y con el
# filtrado) se mezclaba en el borde de cada isla -> una linea clara en cada corte de UV y triangulos chicos de otro color.
# Uso: python3 pad_tex.py torso|arm <common> <carpeta sin short (pelvis) o -> <entrada.ftex> <salida.ftex>
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, '..'), os.path.join(HERE, '..', 'fox'), os.path.join(HERE, '..', 'fox', 'torso')]
from ftex import Ftex
from make_texture_pack import decode, encode_chain
from make_srm import uv_mask

def mask_for(kind, common, sinshort, H, W):
    if kind == "torso":
        m = uv_mask(common, [("pants_out_sub", [1, 2, 3]), ("pants_in_sub", [3]), ("pants_out_sub_tight", [3])], H, W)
        if sinshort and sinshort != "-":
            m |= uv_mask(sinshort, [("pants_out_sub", [0])], H, W)
    else:
        specs = []
        for fn in ("arm", "arm_cap", "arm_glove", "arm_glove_cap"):
            from fmdl import Fmdl, meshes
            n = len(meshes(Fmdl(os.path.join(common, fn + ".fmdl"))))
            specs.append((fn, list(range(n))))
        m = uv_mask(common, specs, H, W)
    from scipy.ndimage import binary_dilation
    return binary_dilation(m, iterations=2)   # 2 texels de borde propios (el rasterizado de los triangulos es justo)

def pad(kind, common, sinshort, src, dst):
    from scipy.ndimage import distance_transform_edt
    F = Ftex(open(src, "rb").read()); img = decode(F); H, W = img.shape[:2]
    m = mask_for(kind, common, sinshort, H, W)
    _, (iy, ix) = distance_transform_edt(~m, return_indices=True)
    out = img[iy, ix]
    F.mips = encode_chain(F, out)
    open(dst, "wb").write(F.to_bytes(chunk=65536, small_first=True))
    return float(m.mean())

if __name__ == "__main__":
    print("islas %.1f %%" % (100 * pad(*sys.argv[1:6])))
