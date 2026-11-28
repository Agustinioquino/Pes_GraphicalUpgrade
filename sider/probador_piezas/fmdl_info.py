# fmdl_info.py - PES21 FX (10/11/2026). Para ProbadorPiezas v2.3.
# Lee cada .fmdl del catalogo y escribe al lado "<archivo>.fmdl.txt" con sus materiales:
# shader, texturas (lugar = ruta) y parametros (nombre = 4 valores). Solo lee los fmdl; no los cambia.
# Uso:  python fmdl_info.py <carpeta_del_catalogo>      (recorre todo)   |   python fmdl_info.py archivo.fmdl  (imprime)
import struct, sys, os

def leer(path):
    d = open(path, 'rb').read()
    if d[:4] != b'FMDL': raise ValueError('no es FMDL')
    (_, ver, info, f0, f1, n0, n1, s0, s0len, s1, s1len) = struct.unpack_from('<4sfQQQIIIIII', d, 0)
    b0, b1 = {}, {}
    p = info
    for _ in range(n0):
        t, cnt, off = struct.unpack_from('<HHI', d, p); p += 8
        b0[t] = (cnt, s0 + off)
    for _ in range(n1):
        t, off, ln = struct.unpack_from('<III', d, p); p += 12
        b1[t] = (s1 + off, ln)
    strs = []
    if 12 in b0 and 3 in b1:
        cnt, off = b0[12]; base, ln = b1[3]
        for i in range(cnt):
            bt, sl, so = struct.unpack_from('<HHI', d, off + 8 * i)
            strs.append(d[base + so: base + so + sl].decode('latin-1'))
    def s(k): return strs[k] if k < len(strs) else '#%d' % k
    out = []
    if 4 not in b0: return ver, out
    cnt, off = b0[4]
    asg_cnt, asg_off = b0.get(7, (0, 0))
    tex_cnt, tex_off = b0.get(6, (0, 0))
    mat_cnt, mat_off = b0.get(8, (0, 0))
    par_off, par_len = b1.get(0, (0, 0))
    for i in range(cnt):
        name, _pad, mat, ntex, npar, ftex, fpar = struct.unpack_from('<HHHBBHH', d, off + 0x10 * i)
        sh = tec = '?'
        if mat < mat_cnt:
            a, b = struct.unpack_from('<HH', d, mat_off + 4 * mat); sh, tec = s(a), s(b)
        m = {'nombre': s(name), 'shader': sh, 'tecnica': tec, 'tex': [], 'par': []}
        for t in range(ntex):
            if ftex + t >= asg_cnt: break
            n, r = struct.unpack_from('<HH', d, asg_off + 4 * (ftex + t))
            ruta = '#%d' % r
            if r < tex_cnt:
                tn, tp = struct.unpack_from('<HH', d, tex_off + 4 * r); ruta = s(tp) + s(tn)
            m['tex'].append((s(n), ruta))
        for t in range(npar):
            if fpar + t >= asg_cnt: break
            n, r = struct.unpack_from('<HH', d, asg_off + 4 * (fpar + t))
            v = None
            if par_off and 16 * (r + 1) <= par_len: v = struct.unpack_from('<4f', d, par_off + 16 * r)
            m['par'].append((s(n), v))
        out.append(m)
    return ver, out

def texto(path):
    ver, mats = leer(path)
    L = []
    for m in mats:
        L.append('MATERIAL %s | shader %s (%s)' % (m['nombre'], m['shader'], m['tecnica']))
        for n, r in m['tex']: L.append('  tex %s = %s' % (n, r))
        for n, v in m['par']:
            L.append('  par %s = %s' % (n, ' '.join('%g' % round(x, 4) for x in v) if v else '?'))
    if not L: L.append('(sin materiales)')
    return '\r\n'.join(L) + '\r\n'

if __name__ == '__main__':
    a = sys.argv[1]
    if os.path.isfile(a):
        sys.stdout.write(texto(a))
    else:
        ok = mal = 0
        for raiz, _, fs in os.walk(a):
            for f in fs:
                if f.lower().endswith('.fmdl'):
                    p = os.path.join(raiz, f)
                    try:
                        t = texto(p); ok += 1
                    except Exception as e:
                        t = '(no se pudo leer: %s)\r\n' % e; mal += 1
                    open(p + '.txt', 'wb').write(t.encode('latin-1', 'replace'))
        print('listo: %d leidos, %d con error' % (ok, mal))
