"""PES21 FX - caras en alta (04/11). Reescala las texturas de cara que la DLL volco en PES21FX\\caras_hd\\originales.

Para cada <id>_bsm.dds / _nrm.dds / _srm.dds / _trm.dds de originales\\ escribe en caras_hd\\:
  <id>_<tex>_4k.dds  (x4 si la original es de 1024, todos los mipmaps)
  <id>_<tex>_2k.dds  (solo si la original es de menos de 2048)
La DLL (caras_hd.cpp) las carga sola: 2048 para todas las caras, 4096 para las cercanas.

Como reescala cada una:
  bsm (color de la piel, face_bsm): IA (4xNomos8kDAT por defecto). El alfa va aparte, sin IA (Lanczos + borde nitido).
  pelo y barba (ids que empiezan con 'p', mismas 4 texturas): color con la IA; el ALFA tambien pasa por la IA pero solo
             aporta el borde fino de cada mechon: la cobertura (el promedio) queda la del original, el pelo no engorda ni se afina.
  srm / trm: son datos, no fotos. Lanczos para los valores + solo el borde fino de la IA canal por canal, asi el promedio
             de cada zona queda igual al original (no cambia el brillo / la translucidez, solo se afila).
  nrm (normal): sin IA. Lanczos + renormalizar en cada mip (el detalle fino lo ponen los poros del mod).
Nunca toca la boca: solo lee los archivos de originales\\ (texturas de la piel de la cara que volco la DLL).

Uso (con el juego cerrado si se usa la GPU):
  python caras_hd.py "D:\\SteamLibrary\\steamapps\\common\\eFootball PES 2021\\PES21FX\\caras_hd" --modelo 4xNomos8kDAT.pth
  (--solo acepta varios ids separados por coma, p.ej. --solo 966b8f46d9e7,p1234abcd5678)
  opciones: --solo <id> (una cara), --rehacer (pisar las ya hechas), --sin-ia (todo Lanczos, para comparar), --tile 256
Necesita: pip install numpy pillow etcpak texture2ddecoder  (+ torch y spandrel para la IA; con CUDA va en segundos)
"""
import argparse, os, struct, sys, time
import numpy as np
from PIL import Image, ImageFilter
import etcpak, texture2ddecoder

# ------------------------------------------------------------------ DDS
DXGI = {71: 'BC1', 72: 'BC1s', 74: 'BC2', 75: 'BC2s', 77: 'BC3', 78: 'BC3s', 80: 'BC4', 81: 'BC4s', 83: 'BC5', 84: 'BC5s',
        98: 'BC7', 99: 'BC7s', 28: 'RGBA8', 29: 'RGBA8s', 87: 'BGRA8', 91: 'BGRA8s',
        70: 'BC1', 73: 'BC2', 76: 'BC3', 79: 'BC4', 82: 'BC5', 97: 'BC7', 27: 'RGBA8', 90: 'BGRA8'}   # *_TYPELESS -> base
OUTFMT = {'BC1': 71, 'BC1s': 72, 'BC3': 77, 'BC3s': 78, 'BC4': 80, 'BC5': 83, 'BC7': 98, 'BC7s': 99}
SRGB_VIEW = {72, 75, 78, 99, 29, 91}


def read_dds(path):
    """-> (rgba uint8 HxWx4 del mip 0, formato base 'BC1'... , es_srgb)"""
    b = open(path, 'rb').read()
    if b[:4] != b'DDS ':
        raise ValueError('no es DDS: ' + path)
    h, w = struct.unpack_from('<II', b, 12)
    fourcc = b[84:88]
    off = 128
    srgb = False
    if fourcc == b'DX10':
        dx = struct.unpack_from('<I', b, 128)[0]
        off = 148
        fmt = DXGI.get(dx)
        if fmt is None:
            raise ValueError('formato DXGI %d sin soporte' % dx)
        srgb = dx in SRGB_VIEW
        fmt = fmt.rstrip('s')
    else:
        fmt = {b'DXT1': 'BC1', b'DXT3': 'BC2', b'DXT5': 'BC3', b'ATI1': 'BC4', b'BC4U': 'BC4', b'ATI2': 'BC5', b'BC5U': 'BC5'}.get(fourcc)
        if fmt is None:
            bits = struct.unpack_from('<I', b, 88)[0]
            if bits == 32:
                fmt = 'BGRA8'
            else:
                raise ValueError('DDS sin soporte (%r)' % fourcc)
    data = b[off:]
    dec = {'BC1': texture2ddecoder.decode_bc1, 'BC3': texture2ddecoder.decode_bc3, 'BC4': texture2ddecoder.decode_bc4,
           'BC5': texture2ddecoder.decode_bc5, 'BC7': texture2ddecoder.decode_bc7}
    if fmt in dec:
        raw = dec[fmt](data, w, h)                                     # BGRA
        a = np.frombuffer(raw, np.uint8).reshape(h, w, 4)[..., [2, 1, 0, 3]].copy()
    elif fmt == 'BC2':
        raise ValueError('BC2 sin soporte')
    elif fmt == 'RGBA8':
        a = np.frombuffer(data[:w * h * 4], np.uint8).reshape(h, w, 4).copy()
    else:
        a = np.frombuffer(data[:w * h * 4], np.uint8).reshape(h, w, 4)[..., [2, 1, 0, 3]].copy()
    if fmt == 'BC4':
        a[..., 1] = a[..., 2] = a[..., 0]; a[..., 3] = 255
    return a, fmt, srgb


def write_dds(path, mips, dxgi):
    """mips: lista de uint8 HxWx4 RGBA (mip 0 primero). Encabezado DX10 con el formato exacto (sRGB o no)."""
    comp = {71: etcpak.compress_bc1, 72: etcpak.compress_bc1, 77: etcpak.compress_bc3, 78: etcpak.compress_bc3,
            80: etcpak.compress_bc4, 83: etcpak.compress_bc5, 98: etcpak.compress_bc7, 99: etcpak.compress_bc7}[dxgi]
    blk = 8 if dxgi in (71, 72, 80) else 16
    h, w = mips[0].shape[:2]
    body = bytearray()
    for m in mips:
        mh, mw = m.shape[:2]
        ph, pw = max(4, (mh + 3) // 4 * 4), max(4, (mw + 3) // 4 * 4)
        if (ph, pw) != (mh, mw):
            m = np.pad(m, ((0, ph - mh), (0, pw - mw), (0, 0)), mode='edge')
        body += comp(np.ascontiguousarray(m).tobytes(), pw, ph)
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000
    pitch = max(1, (w + 3) // 4) * blk * max(1, (h + 3) // 4)
    hdr = struct.pack('<4sIIIIIII', b'DDS ', 124, flags, h, w, pitch, 0, len(mips)) + b'\0' * 44
    hdr += struct.pack('<II4sIIIII', 32, 0x4, b'DX10', 0, 0, 0, 0, 0)
    hdr += struct.pack('<IIIII', 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    hdr += struct.pack('<IIIII', dxgi, 3, 0, 1, 0)
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(hdr); f.write(body)
    os.replace(tmp, path)   # la DLL nunca ve un archivo a medio escribir


# ------------------------------------------------------------------ reescalado
def lanczos(a, size):
    """a: float32 HxWxC 0..1 -> size = (ancho, alto) (el pelo es 2048x1024: no siempre cuadrado)"""
    chans = [np.asarray(Image.fromarray(a[..., c].astype(np.float32), 'F').resize(size, Image.LANCZOS)) for c in range(a.shape[2])]
    return np.stack(chans, -1)


def blur(a, r):
    """gaussiana separable en float (Pillow no filtra imagenes 'F')"""
    k = np.arange(-int(3 * r), int(3 * r) + 1, dtype=np.float32)
    k = np.exp(-k * k / (2 * r * r)); k /= k.sum()
    n = len(k) // 2
    p = np.pad(a, ((n, n), (n, n), (0, 0)), mode='edge')
    t = sum(k[i] * p[i:i + a.shape[0], :, :] for i in range(len(k)))
    return sum(k[i] * t[:, i:i + a.shape[1], :] for i in range(len(k)))


class Ia:
    def __init__(self, path, tile):
        import torch
        from spandrel import ModelLoader
        self.torch = torch
        self.dev = 'cuda' if torch.cuda.is_available() else 'cpu'
        self.m = ModelLoader().load_from_file(path).eval().to(self.dev)
        self.half = self.dev == 'cuda'
        if self.half:
            try:
                self.m = self.m.half()
            except Exception:
                self.half = False
        self.scale = self.m.scale
        self.tile = tile
        print('IA: %s (%s, x%d) en %s' % (os.path.basename(path), self.m.architecture.name, self.scale, self.dev))

    def run(self, rgb):
        """rgb float32 HxWx3 0..1 -> (H*s)x(W*s)x3, por mosaicos con solape"""
        t, s, pad = self.tile, self.scale, 16
        h, w = rgb.shape[:2]
        out = np.zeros((h * s, w * s, 3), np.float32)
        for y in range(0, h, t):
            for x in range(0, w, t):
                y0, x0 = max(0, y - pad), max(0, x - pad)
                y1, x1 = min(h, y + t + pad), min(w, x + t + pad)
                tile = self.torch.from_numpy(np.ascontiguousarray(rgb[y0:y1, x0:x1].transpose(2, 0, 1)))[None].to(self.dev)
                if self.half:
                    tile = tile.half()
                with self.torch.no_grad():
                    r = self.m(tile).float().clamp(0, 1)[0].cpu().numpy().transpose(1, 2, 0)
                yy, xx = (y - y0) * s, (x - x0) * s
                th, tw = min(t, h - y) * s, min(t, w - x) * s
                out[y * s:y * s + th, x * s:x * s + tw] = r[yy:yy + th, xx:xx + tw]
        return out


def up_rgb(rgb, size, ia):
    if ia is None:
        return lanczos(rgb, size)
    r = rgb
    while r.shape[1] < size[0]:         # x4 una vez (1024 -> 4096); si hace falta mas, otra pasada
        r = ia.run(r)
    if (r.shape[1], r.shape[0]) != tuple(size):
        r = lanczos(r, size)
    return r


def up_alpha(a, size):
    """alfa sin IA: Lanczos + borde nitido (pelo / barba pintados)"""
    u = lanczos(a, size)
    sharp = u + (u - blur(u, 2.0)) * 0.8
    return np.clip(sharp, 0, 1)


def up_data(x, size, ia):
    """srm / trm: valores de Lanczos + solo la alta frecuencia de la IA, canal por canal"""
    base = lanczos(x, size)
    if ia is None:
        return base
    out = base.copy()
    for c in range(x.shape[2]):
        if x[..., c].max() - x[..., c].min() < 2 / 255:    # canal constante: nada que afilar
            continue
        g = np.repeat(x[..., c:c + 1], 3, -1)
        ai = up_rgb(g, size, ia).mean(-1, keepdims=True)
        out[..., c:c + 1] = base[..., c:c + 1] + (ai - blur(ai, 3.0))
    return np.clip(out, 0, 1)


def normalize_nrm(n, fmt):
    v = n[..., :3] * 2 - 1
    if fmt == 'BC5':
        v[..., 2] = np.sqrt(np.clip(1 - v[..., 0] ** 2 - v[..., 1] ** 2, 0, 1))
    l = np.sqrt((v ** 2).sum(-1, keepdims=True)) + 1e-6
    n = n.copy(); n[..., :3] = (v / l) * 0.5 + 0.5
    return n


def mips_of(img, kind, fmt):
    """img float32 SxSx4 -> lista uint8 RGBA hasta 1x1 (normal renormalizada en cada nivel)"""
    out = []
    cur = img
    while True:
        m = cur
        if kind == 'nrm':
            m = normalize_nrm(m, fmt)
        out.append((np.clip(m, 0, 1) * 255 + 0.5).astype(np.uint8))
        if cur.shape[0] == 1 and cur.shape[1] == 1:
            break
        s = (max(1, cur.shape[1] // 2), max(1, cur.shape[0] // 2))
        cur = np.stack([np.asarray(Image.fromarray(cur[..., c], 'F').resize(s, Image.BOX)) for c in range(4)], -1)
    return out


def out_format(kind, fmt, srgb):
    if kind == 'nrm':
        return {'BC5': 83, 'BC4': 80, 'BC1': 98, 'BC3': 77}.get(fmt, 98)   # canales de la normal como el original (BC3 = DXT5nm)
    if fmt == 'BC4':
        return 80
    if fmt == 'BC5':
        return 83
    return 99 if srgb else 98


def view_srgb(info_path, slot):
    """formato de la VISTA del juego (caras_hd.cpp lo anota en <id>_info.txt): manda sobre el del archivo"""
    try:
        for line in open(info_path, encoding='latin-1'):
            p = [x.strip() for x in line.split(';')]
            if len(p) >= 6 and p[0] == 't%d' % slot:
                return int(p[5]) in SRGB_VIEW
    except OSError:
        pass
    return None


def process(root, fid, ia, rehacer):
    od = os.path.join(root, 'originales')
    info = os.path.join(od, fid + '_info.txt')
    for slot, kind in enumerate(('bsm', 'nrm', 'srm', 'trm')):
        src = os.path.join(od, '%s_%s.dds' % (fid, kind))
        if not os.path.exists(src):
            continue
        a, fmt, srgb = read_dds(src)
        vs = view_srgb(info, slot)
        if vs is not None:
            srgb = vs
        n = max(a.shape[0], a.shape[1])             # lado mayor (pelo 2048x1024 -> 4096x2048)
        dims = lambda side: (a.shape[1] * side // n, a.shape[0] * side // n)
        sizes = [4096] + ([2048] if n < 2048 else [])
        todo = [s for s in sizes if rehacer or not os.path.exists(os.path.join(root, '%s_%s_%dk.dds' % (fid, kind, s // 1024)))]
        if not todo:
            continue
        t = time.time()
        x = a.astype(np.float32) / 255
        if kind == 'bsm':
            # pelo / barba (id 'p...'): el alfa son los mechones -> tambien con la IA, pero conservando la cobertura de Lanczos
            alpha = up_data(x[..., 3:], dims(4096), ia) if fid.startswith('p') else up_alpha(x[..., 3:], dims(4096))
            big = np.concatenate([up_rgb(x[..., :3], dims(4096), ia), alpha], -1)
        elif kind == 'nrm':
            big = lanczos(x, dims(4096))
        else:
            big = up_data(x, dims(4096), ia)
        dx = out_format(kind, fmt, srgb)
        for s in todo:
            img = big if s == 4096 else np.stack([np.asarray(Image.fromarray(big[..., c], 'F').resize(dims(s), Image.LANCZOS)) for c in range(4)], -1)
            write_dds(os.path.join(root, '%s_%s_%dk.dds' % (fid, kind, s // 1024)), mips_of(img.astype(np.float32), kind, fmt), dx)
        print('  %s_%s: %d px %s%s -> %s (dxgi %d) en %.0f s' % (fid, kind, n, fmt, ' sRGB' if srgb else '', '+'.join('%d' % s for s in todo), dx, time.time() - t))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('carpeta', help='PES21FX\\caras_hd')
    ap.add_argument('--modelo', default='4xNomos8kDAT.pth')
    ap.add_argument('--sin-ia', action='store_true')
    ap.add_argument('--solo')
    ap.add_argument('--rehacer', action='store_true')
    ap.add_argument('--tile', type=int, default=256)
    o = ap.parse_args()
    od = os.path.join(o.carpeta, 'originales')
    ids = sorted({f.split('_')[0] for f in os.listdir(od) if f.endswith('.dds')})
    if o.solo:
        ids = [i for i in ids if i in o.solo.split(',')]
    print('%d caras en %s' % (len(ids), od))
    ia = None if o.sin_ia else Ia(o.modelo, o.tile)
    for i in ids:
        print('cara', i)
        process(o.carpeta, i, ia, o.rehacer)


if __name__ == '__main__':
    main()
