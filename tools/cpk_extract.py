#!/usr/bin/env python3
# Extractor de solo lectura de archivos CRI CPK (PES 2021): lista la TOC y extrae archivos por nombre, por patron o por
# hash en el nombre. No modifica el cpk. Descomprime CRILAYLA. (07/11, pedido del hilo de expresiones: leer las .gani)
# Uso:
#   python cpk_extract.py listar  D:\...\dt13_all.cpk [patron]           (patron = subcadena, sin distinguir mayusculas)
#   python cpk_extract.py extraer D:\...\dt12_g4.cpk destino\ patron [patron2 ...]
#   python cpk_extract.py extraer D:\...\dt12_g4.cpk destino\ --exacto common/anime/.../archivo.gani
import os, struct, sys, zlib

def _utf_decrypt(b):
    # tablas @UTF cifradas (xor rodante): m = 0x5f, t = 0x15
    out = bytearray(len(b)); m, t = 0x5F, 0x15
    for i, c in enumerate(b):
        out[i] = c ^ m; m = (m * t) & 0xFF
    return bytes(out)

def read_utf(data):
    # data empieza en '@UTF' (ya descifrada si hacia falta). Devuelve (nombre_tabla, [dict fila])
    if data[:4] != b'@UTF': raise ValueError('no es @UTF')
    (table_size, rows_off, strings_off, data_off, name_off, ncols, row_len, nrows) = struct.unpack('>IIIIIHHI', data[4:0x20])
    rows_off += 8; strings_off += 8; data_off += 8
    def cstr(off):
        e = data.index(b'\0', strings_off + off); return data[strings_off + off:e].decode('utf-8', 'replace')
    types = {0: ('B', 1), 1: ('b', 1), 2: ('H', 2), 3: ('h', 2), 4: ('I', 4), 5: ('i', 4), 6: ('Q', 8), 7: ('q', 8), 8: ('f', 4), 0xA: ('str', 4), 0xB: ('data', 8)}
    cols = []; p = 0x20
    for _ in range(ncols):
        flag = data[p]; p += 1
        name = cstr(struct.unpack('>I', data[p:p + 4])[0]); p += 4
        typ, stor = flag & 0x0F, flag & 0xF0
        const = None
        if stor == 0x30:   # constante en la cabecera
            fmt, sz = types[typ]
            if fmt == 'str': const = cstr(struct.unpack('>I', data[p:p + 4])[0])
            elif fmt == 'data': o, l = struct.unpack('>II', data[p:p + 8]); const = data[data_off + o:data_off + o + l]
            else: const = struct.unpack('>' + fmt, data[p:p + sz])[0]
            p += sz
        cols.append((name, typ, stor, const))
    rows = []
    for r in range(nrows):
        p = rows_off + r * row_len; row = {}
        for name, typ, stor, const in cols:
            if stor == 0x30: row[name] = const; continue
            if stor == 0x10: row[name] = None; continue   # columna sin datos (zero)
            fmt, sz = types[typ]
            if fmt == 'str': row[name] = cstr(struct.unpack('>I', data[p:p + 4])[0])
            elif fmt == 'data': o, l = struct.unpack('>II', data[p:p + 8]); row[name] = data[data_off + o:data_off + o + l]
            else: row[name] = struct.unpack('>' + fmt, data[p:p + sz])[0]
            p += sz
        rows.append(row)
    return cstr(name_off), rows

def read_section(f, off, tag):
    f.seek(off); hdr = f.read(16)
    if hdr[:4] != tag: raise ValueError('esperaba %r en %X, hay %r' % (tag, off, hdr[:4]))
    size = struct.unpack('<Q', hdr[8:16])[0]
    body = f.read(size)
    if body[:4] != b'@UTF': body = _utf_decrypt(body)
    return read_utf(body)

def crilayla_decompress(buf):
    if buf[:8] != b'CRILAYLA': return buf
    usize, csize = struct.unpack('<II', buf[8:16])
    comp = buf[16:16 + csize]; raw_tail = buf[16 + csize:16 + csize + 0x100]
    out = bytearray(usize); pos = usize - 1   # se escribe de atras para adelante
    bitpos = 0; src = len(comp) * 8 - 1
    def bits(n):
        nonlocal src
        v = 0
        for _ in range(n):
            byte = comp[src >> 3]; v = (v << 1) | ((byte >> (src & 7)) & 1); src -= 1
        return v
    while pos >= 0:
        if bits(1):
            off = bits(13) + 3; length = 3; lvls = (2, 3, 5, 8)
            for i, lb in enumerate(lvls):
                v = bits(lb); length += v
                if v != (1 << lb) - 1: break
            else:
                while True:
                    v = bits(8); length += v
                    if v != 255: break
            for _ in range(length):
                if pos < 0: break
                out[pos] = out[pos + off]; pos -= 1
        else:
            out[pos] = bits(8); pos -= 1
    return bytes(raw_tail) + bytes(out)

def open_cpk(path):
    f = open(path, 'rb')
    _, hdr = read_section(f, 0, b'CPK ')
    h = hdr[0]
    toc_off = h.get('TocOffset') or 0; content_off = h.get('ContentOffset') or 0
    base = min(toc_off, content_off) if toc_off and content_off else (content_off or toc_off)
    files = []
    if toc_off:
        _, rows = read_section(f, toc_off, b'TOC ')
        for r in rows:
            name = ((r.get('DirName') or '') + '/' + (r.get('FileName') or '')).strip('/')
            files.append((name, base + (r.get('FileOffset') or 0), r.get('FileSize') or 0, r.get('ExtractSize') or 0, r.get('ID')))
    itoc = h.get('ItocOffset')
    if not toc_off and itoc:   # cpk sin nombres (solo ids): ITOC
        _, rows = read_section(f, itoc, b'ITOC')
        align = h.get('Align') or 2048; off = content_off
        ids = []
        for r in rows:
            for key in ('DataL', 'DataH'):
                if r.get(key):
                    _, sub = read_utf(r[key])
                    for s in sub: ids.append((s['ID'], s['FileSize'], s.get('ExtractSize') or s['FileSize']))
        for i, fs, es in sorted(ids):
            files.append(('%u' % i, off, fs, es, i)); off += fs + ((align - fs % align) % align)
    return f, files

def extract(f, entry, dest):
    name, off, fs, es, fid = entry
    f.seek(off); data = f.read(fs)
    if data[:8] == b'CRILAYLA': data = crilayla_decompress(data)
    out = os.path.join(dest, name.replace('/', os.sep))
    os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
    open(out, 'wb').write(data)
    return out, len(data), zlib.crc32(data) & 0xFFFFFFFF

def main():
    if len(sys.argv) < 3: print(__doc__ or 'uso: listar|extraer'); return 1
    cmd, path = sys.argv[1], sys.argv[2]
    f, files = open_cpk(path)
    if cmd == 'listar':
        pat = (sys.argv[3] if len(sys.argv) > 3 else '').lower()
        n = 0
        for name, off, fs, es, fid in files:
            if pat in name.lower(): print('%s\t%u\t%u\t%s' % (name, fs, es, fid)); n += 1
        print('%d de %d archivos' % (n, len(files)))
    elif cmd == 'extraer':
        dest = sys.argv[3]; args = sys.argv[4:]
        exact = '--exacto' in args; pats = [a.lower() for a in args if a != '--exacto']
        n = 0
        for e in files:
            nm = e[0].lower()
            if any((nm == p) if exact else (p in nm) for p in pats):
                out, sz, crc = extract(f, e, dest); print('%s\t%u B\tCRC32 %08X' % (out, sz, crc)); n += 1
        print('%d extraidos' % n)
    else: print('comando desconocido'); return 1
    return 0

if __name__ == '__main__': sys.exit(main())
