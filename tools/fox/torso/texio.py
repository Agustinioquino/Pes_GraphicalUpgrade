# PES21 FX (26/10) - texturas para las variantes del torso: lectura de FTEX BC5 / BC7 (Pillow via DDS DX10), escritura de DDS
# DX10 (R32G32B32A32_FLOAT para los datos por vertice, BC5_SNORM con mips para los mapas normales delta) y un codificador BC4
# simple (por bloque: extremos = minimo y maximo, 8 niveles).
import sys, os, struct, io
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
from ftex import Ftex

DXGI = {9: 83, 11: 98, 2: 71}   # formato FTEX -> DXGI (BC5_UNORM, BC7_UNORM, BC1_UNORM)

def dds_dx10_header(w, h, mips, dxgi, pitch_or_linear):
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000 | (0x20000 if mips > 1 else 0)
    hdr = struct.pack('<4sIIIIIII', b'DDS ', 124, flags, h, w, pitch_or_linear, 0, mips) + b'\0' * 44
    hdr += struct.pack('<II4sIIIII', 32, 0x4, b'DX10', 0, 0, 0, 0, 0)
    hdr += struct.pack('<IIIII', 0x1000 | (0x400008 if mips > 1 else 0), 0, 0, 0, 0)
    hdr += struct.pack('<IIIII', dxgi, 3, 0, 1, 0)   # DIMENSION_TEXTURE2D, sin banderas, 1 en el arreglo
    return hdr

def load_ftex(path, mip=0):
    """FTEX -> numpy float32 (H, W, 4) en 0..1 (BC5: R, G; BC7: RGBA)"""
    from PIL import Image
    f = Ftex(open(path, 'rb').read())
    w, h = f.mip_size(mip)
    data = f.mips[mip]
    dds = dds_dx10_header(w, h, 1, DXGI[f.fmt], len(data)) + data
    im = Image.open(io.BytesIO(dds)); im.load()
    a = np.asarray(im.convert('RGBA'), dtype=np.float32) / 255.0
    return a

def write_dds_float4(path, arr):
    """arr: (H, W, 4) float32 -> DDS R32G32B32A32_FLOAT (dxgi 2), sin mips"""
    h, w = arr.shape[:2]
    a = np.ascontiguousarray(arr, dtype=np.float32)
    with open(path, 'wb') as f:
        f.write(dds_dx10_header(w, h, 1, 2, w * 16)); f.write(a.tobytes())

def bc4_snorm_blocks(ch):
    """canal (H, W) en -1..1 -> bloques BC4_SNORM (bytes). H y W multiplos de 4."""
    H, W = ch.shape
    b = ch.reshape(H // 4, 4, W // 4, 4).transpose(0, 2, 1, 3).reshape(-1, 16)
    q = np.clip(np.round(b * 127.0), -127, 127)
    e0 = q.max(1); e1 = q.min(1)                         # e0 > e1: modo de 8 niveles
    same = e0 == e1
    e0 = np.where(same, np.minimum(e0 + 1, 127), e0); e1 = np.where(same & (e0 == 127), e1 - 1, e1)
    e1 = np.where(e0 == e1, e1 - 1, e1)
    lv = np.stack([e0, e1] + [((8 - k) * e0 + (k - 1) * e1) / 7.0 for k in range(2, 8)], 1)   # 8 niveles (orden BC4)
    d = np.abs(b[:, :, None] * 127.0 - lv[:, None, :])
    idx = np.argmin(d, 2).astype(np.uint64)              # (N, 16) indices 0..7
    bits = np.zeros(len(b), np.uint64)
    for i in range(16): bits |= idx[:, i] << np.uint64(3 * i)
    out = np.zeros((len(b), 8), np.uint8)
    out[:, 0] = (e0.astype(np.int16) & 0xff).astype(np.uint8); out[:, 1] = (e1.astype(np.int16) & 0xff).astype(np.uint8)
    for k in range(6): out[:, 2 + k] = ((bits >> np.uint64(8 * k)) & np.uint64(0xff)).astype(np.uint8)
    return out

def write_dds_bc5_snorm(path, rg_mips):
    """rg_mips: lista de (H, W, 2) en -1..1 (mip 0 primero) -> DDS BC5_SNORM (dxgi 84)"""
    h, w = rg_mips[0].shape[:2]
    blobs = []
    for m in rg_mips:
        H, W = m.shape[:2]
        Hp, Wp = max(4, (H + 3) // 4 * 4), max(4, (W + 3) // 4 * 4)
        mm = np.zeros((Hp, Wp, 2), np.float32); mm[:H, :W] = m
        if H < Hp: mm[H:, :W] = m[-1:, :]
        if W < Wp: mm[:, W:] = mm[:, W - 1:W]
        r = bc4_snorm_blocks(mm[..., 0]); g = bc4_snorm_blocks(mm[..., 1])
        blobs.append(np.concatenate([r, g], 1).tobytes())
    with open(path, 'wb') as f:
        f.write(dds_dx10_header(w, h, len(rg_mips), 84, len(blobs[0]))); f.write(b''.join(blobs))

def decode_bc4_snorm(blocks, H, W):
    """para comprobar el codificador: bloques (N, 8) -> canal (H, W) en -1..1"""
    e0 = blocks[:, 0].astype(np.int8).astype(np.float32); e1 = blocks[:, 1].astype(np.int8).astype(np.float32)
    bits = np.zeros(len(blocks), np.uint64)
    for k in range(6): bits |= blocks[:, 2 + k].astype(np.uint64) << np.uint64(8 * k)
    lv = np.where((e0 > e1)[:, None], np.stack([e0, e1] + [((8 - k) * e0 + (k - 1) * e1) / 7.0 for k in range(2, 8)], 1),
                  np.stack([e0, e1] + [((6 - k) * e0 + (k - 1) * e1) / 5.0 for k in range(2, 6)] + [np.full_like(e0, -127), np.full_like(e0, 127)], 1))
    idx = np.stack([((bits >> np.uint64(3 * i)) & np.uint64(7)).astype(np.int64) for i in range(16)], 1)
    v = np.take_along_axis(lv, idx, 1) / 127.0
    return v.reshape(H // 4, W // 4, 4, 4).transpose(0, 2, 1, 3).reshape(H, W)

def bc4_unorm_blocks(ch):
    """canal (H, W) en 0..1 -> bloques BC4_UNORM (bytes). H y W multiplos de 4."""
    H, W = ch.shape
    b = ch.reshape(H // 4, 4, W // 4, 4).transpose(0, 2, 1, 3).reshape(-1, 16)
    q = np.clip(np.round(b * 255.0), 0, 255)
    e0 = q.max(1); e1 = q.min(1)
    e0 = np.where(e0 == e1, np.minimum(e0 + 1, 255), e0); e1 = np.where(e0 == e1, e1 - 1, e1)
    lv = np.stack([e0, e1] + [((8 - k) * e0 + (k - 1) * e1) / 7.0 for k in range(2, 8)], 1)
    idx = np.argmin(np.abs(b[:, :, None] * 255.0 - lv[:, None, :]), 2).astype(np.uint64)
    bits = np.zeros(len(b), np.uint64)
    for i in range(16): bits |= idx[:, i] << np.uint64(3 * i)
    out = np.zeros((len(b), 8), np.uint8)
    out[:, 0] = e0.astype(np.uint8); out[:, 1] = e1.astype(np.uint8)
    for k in range(6): out[:, 2 + k] = ((bits >> np.uint64(8 * k)) & np.uint64(0xff)).astype(np.uint8)
    return out

def write_dds_bc4_unorm(path, mips):
    """mips: lista de (H, W) en 0..1 -> DDS BC4_UNORM (dxgi 80)"""
    h, w = mips[0].shape
    blobs = []
    for m in mips:
        H, W = m.shape
        Hp, Wp = max(4, (H + 3) // 4 * 4), max(4, (W + 3) // 4 * 4)
        mm = np.zeros((Hp, Wp), np.float32); mm[:H, :W] = m
        blobs.append(bc4_unorm_blocks(mm).tobytes())
    with open(path, 'wb') as f:
        f.write(dds_dx10_header(w, h, len(mips), 80, len(blobs[0]))); f.write(b''.join(blobs))
