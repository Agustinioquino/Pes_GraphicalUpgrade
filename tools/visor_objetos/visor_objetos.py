#!/usr/bin/env python3
# Visor de objetos (PES21 FX, 10/11/2026): muestra en 3D, sin el juego, los .fmdl del catalogo (B:\VisualPes21\catalogo_fmdl)
# con sus texturas .ftex sacadas de los .cpk del juego y de las carpetas livecpk de Sider. Solo lee: no escribe nada en la
# carpeta del juego ni en la de Sider; lo unico que escribe es su cache (indice de cpk y texturas en PNG) en la carpeta de trabajo.
# Uso: VisorObjetos.exe  (o python visor_objetos.py [--catalogo ...] [--juego ...] [--trabajo ...])
import os, sys, io, re, json, struct, hashlib, argparse, threading, time, zlib, urllib.parse, base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

AQUI = os.path.dirname(os.path.abspath(__file__))
BASE = getattr(sys, '_MEIPASS', AQUI)                    # dentro del exe (PyInstaller) los archivos van en _MEIPASS
for p in (os.path.join(AQUI, '..'), os.path.join(AQUI, '..', 'fox')):
    if os.path.isdir(p): sys.path.insert(0, os.path.abspath(p))
import numpy as np
from PIL import Image
import cpk_extract
from ftex import Ftex
from fmdl import Fmdl, meshes as fmdl_meshes

JUEGO = r'D:\SteamLibrary\steamapps\common\eFootball PES 2021'
LOCK = threading.Lock()
ARGS = None
ESTADO = {'indice': 'sin empezar', 'cpks': 0, 'texturas': 0}
IDX = {'ruta': {}, 'nombre': {}}                         # 'asset/.../#windx11/x.ftex' -> fuente ; 'x.ftex' -> [rutas]
CPKS = {}

# ---------------------------------------------------------------- donde buscar
def raices_livecpk():
    """carpetas cpk.root activas del sider.ini (las que el juego usa antes que los cpk), en el mismo orden"""
    out = []
    for ini in (os.path.join(ARGS.juego, 'Dream Patch Sider', 'sider.ini'), os.path.join(ARGS.juego, 'sider.ini')):
        if not os.path.isfile(ini): continue
        base = os.path.dirname(ini)
        for linea in open(ini, 'r', encoding='utf-8', errors='replace'):
            m = re.match(r'\s*cpk\.root\s*=\s*"?([^";\r\n]+)"?', linea)
            if not m: continue
            r = m.group(1).strip()
            r = r if os.path.isabs(r) else os.path.normpath(os.path.join(base, r))
            if os.path.isdir(r): out.append(r)
        break
    return out

def lista_cpk():
    out = []
    for sub in ('download', 'Data', 'data'):
        d = os.path.join(ARGS.juego, sub)
        if os.path.isdir(d):
            out += sorted(os.path.join(d, n) for n in os.listdir(d) if n.lower().endswith('.cpk'))
        if sub == 'Data' and out: break
    return list(dict.fromkeys(os.path.normcase(p) for p in out))

def armar_indice():
    try:
        os.makedirs(ARGS.trabajo, exist_ok=True)
        cache_p = os.path.join(ARGS.trabajo, 'indice_cpk.json')
        try: cache = json.load(open(cache_p, 'r', encoding='utf-8'))
        except Exception: cache = {}
        nuevo = {}
        ruta, nombre = {}, {}
        def poner(clave, fuente):
            if clave in ruta: return                     # gana el primero (livecpk antes que cpk, como el juego)
            ruta[clave] = fuente
            nombre.setdefault(clave.rsplit('/', 1)[-1], []).append(clave)
        ESTADO['indice'] = 'leyendo carpetas livecpk'
        for r in raices_livecpk():
            for carpeta, _, fs in os.walk(r):
                for f in fs:
                    if f.lower().endswith(('.ftex', '.dds')):
                        full = os.path.join(carpeta, f)
                        poner(os.path.relpath(full, r).replace('\\', '/').lower(), ['disco', full])
        cpks = lista_cpk()
        for i, c in enumerate(cpks):
            ESTADO['indice'] = 'leyendo %s (%d de %d)' % (os.path.basename(c), i + 1, len(cpks))
            st = os.stat(c); firma = '%d_%d' % (st.st_size, int(st.st_mtime))
            ent = cache.get(c)
            if not ent or ent.get('firma') != firma:
                try:
                    f, files = cpk_extract.open_cpk(c); f.close()
                    ent = {'firma': firma, 'tex': [[n, o, s] for n, o, s, _e, _id in files if n.lower().endswith(('.ftex', '.dds'))]}
                except Exception as e:
                    ent = {'firma': firma, 'tex': [], 'error': str(e)}
            nuevo[c] = ent
            for n, o, s in ent['tex']:
                poner(n.lower(), ['cpk', c, o, s])
            ESTADO['cpks'] = i + 1
        try: json.dump(nuevo, open(cache_p, 'w', encoding='utf-8'))
        except Exception: pass
        IDX['ruta'], IDX['nombre'] = ruta, nombre
        ESTADO['texturas'] = len(ruta)
        ESTADO['indice'] = 'listo'
    except Exception as e:
        import traceback; traceback.print_exc()
        ESTADO['indice'] = 'error: %s' % e

def leer_fuente(fu):
    if fu[0] == 'disco': return open(fu[1], 'rb').read()
    _, c, o, s = fu
    with LOCK:
        f = CPKS.get(c) or open(c, 'rb'); CPKS[c] = f
        f.seek(o); data = f.read(s)
    if data[:8] == b'CRILAYLA': data = cpk_extract.crilayla_decompress(data)
    return data

# ---------------------------------------------------------------- catalogo
CATEGORIAS = [   # las mismas que catalogo_indice.py (ProbadorPiezas) para que los grupos coincidan
    ('personaje: partes sueltas', ('asset\\model\\character\\parts\\',)), ('personaje: paquete comun', ('common_package',)),
    ('personaje: uniforme', ('\\character\\uniform\\',)), ('personaje: trajes de tecnicos', ('\\character\\face\\real\\',)),
    ('personaje', ('\\character\\',)), ('publico y extras', ('\\mob\\',)), ('objetos de cinematicas', ('fixdemoobj',)),
    ('objetos de entrenamiento', ('training_prop',)), ('objetos', ('\\prop\\',)), ('pelotas', ('\\ball\\',)),
    ('estadio y fondos', ('\\bg\\',))]

def cuentas(path):
    try:
        with open(path, 'rb') as f:
            d = f.read(0x40)
            if d[:4] != b'FMDL': return None
            info, = struct.unpack_from('<Q', d, 8); n0, = struct.unpack_from('<I', d, 0x20)
            f.seek(info); t = f.read(8 * n0)
        c = {struct.unpack_from('<H', t, 8 * i)[0]: struct.unpack_from('<H', t, 8 * i + 2)[0] for i in range(n0)}
        return c.get(0, 0), c.get(3, 0)
    except Exception:
        return None

def api_lista():
    raiz = ARGS.catalogo
    if not os.path.isdir(raiz): return {'error': 'no existe la carpeta del catalogo: ' + raiz, 'grupos': []}
    existe = {}
    for carpeta, _, fs in os.walk(raiz):
        for f in fs:
            if f.lower().endswith('.fmdl'):
                rel = os.path.relpath(os.path.join(carpeta, f), raiz).replace('/', '\\')
                existe[rel.lower()] = rel
    grupos, vistos = [], set()
    ind = os.path.join(raiz, 'indice.txt')
    if os.path.isfile(ind):                             # los grupos que arma catalogo_indice.py (los mismos del ProbadorPiezas)
        g = None
        for linea in open(ind, 'r', encoding='utf-8', errors='replace'):
            linea = linea.strip()
            if not linea or linea.startswith('#'): continue
            if linea.startswith('[') and linea.endswith(']'):
                g = {'nombre': linea[1:-1], 'modelos': []}; grupos.append(g); continue
            rel = existe.get(linea.replace('/', '\\').lower())
            if rel and rel.lower() not in vistos:
                if g is None: g = {'nombre': 'catalogo', 'modelos': []}; grupos.append(g)
                g['modelos'].append(rel); vistos.add(rel.lower())
    resto = sorted((r for k, r in existe.items() if k not in vistos), key=str.lower)
    if resto:
        por = {}
        for r in resto:
            low = '\\' + r.lower()
            i = next((k for k, (_n, ts) in enumerate(CATEGORIAS) if all(t in low for t in ts)), len(CATEGORIAS))
            por.setdefault(i, []).append(r)
        for i in sorted(por):
            grupos.append({'nombre': (CATEGORIAS[i][0] if i < len(CATEGORIAS) else 'otros') + (' (fuera del indice)' if grupos else ''),
                           'modelos': por[i]})
    out = []
    for g in grupos:
        ms = []
        for r in g['modelos']:
            c = cuentas(os.path.join(raiz, *r.split('\\')))
            ms.append({'r': r, 'h': c[0] if c else -1})
        out.append({'nombre': g['nombre'], 'modelos': ms})
    return {'catalogo': raiz, 'grupos': out, 'total': sum(len(g['modelos']) for g in out)}

# ---------------------------------------------------------------- modelo
def materiales(f):
    """instancias de material (bloque 4, 16 B: nombre, -, material, nTex, nParam, primera, primer param), sus texturas
    (bloque 7 = (ranura, indice en bloque 6); bloque 6 = (archivo, carpeta)) y el tipo de material (bloque 8 = (nombre, tecnica))"""
    d, s = f.d, f.strings
    def S(k): return s[k] if 0 <= k < len(s) else ''
    out = []
    if 4 not in f.b0: return out
    cnt, off = f.b0[4]
    tex = [struct.unpack_from('<HH', d, f.b0[6][1] + 4 * i) for i in range(f.b0[6][0])] if 6 in f.b0 else []
    par = [struct.unpack_from('<HH', d, f.b0[7][1] + 4 * i) for i in range(f.b0[7][0])] if 7 in f.b0 else []
    mat = [struct.unpack_from('<HH', d, f.b0[8][1] + 4 * i) for i in range(f.b0[8][0])] if 8 in f.b0 else []
    for i in range(cnt):
        nom, _p, mi, nt, npar, ft, fp = struct.unpack_from('<HHHBBHH', d, off + 0x10 * i)
        texs = []
        for k in range(nt):
            if ft + k < len(par):
                ranura, ti = par[ft + k]
                if ti < len(tex):
                    texs.append({'ranura': S(ranura), 'archivo': S(tex[ti][0]), 'carpeta': S(tex[ti][1])})
        m = mat[mi] if mi < len(mat) else (0, 0)
        out.append({'nombre': S(nom), 'shader': S(m[0]), 'tecnica': S(m[1]), 'texturas': texs})
    return out

def ruta_juego(carpeta, archivo):
    """'/Assets/pes16/model/x/sourceimages/' + 'a_bsm.tga' -> 'asset/model/x/sourceimages/#windx11/a_bsm.ftex'"""
    c = carpeta.replace('\\', '/').strip('/').lower()
    c = re.sub(r'^assets?/(pes\d+/)?', 'asset/', c)
    stem = archivo.rsplit('/', 1)[-1].lower().rsplit('.', 1)[0]
    return c + '/#windx11/' + stem + '.ftex', stem + '.ftex'

def buscar_textura(carpeta, archivo):
    clave, nom = ruta_juego(carpeta, archivo)
    if clave in IDX['ruta']: return clave, 'exacta'
    sin = clave.replace('/#windx11/', '/')
    if sin in IDX['ruta']: return sin, 'exacta'
    cands = IDX['nombre'].get(nom, []) or IDX['nombre'].get(nom[:-5] + '.dds', [])
    if not cands: return None, 'no esta en los cpk ni en livecpk'
    def comun(r):
        k = 0
        while k < min(len(r), len(clave)) and r[k] == clave[k]: k += 1
        return k
    mejor = max(cands, key=comun)
    return mejor, 'por nombre (%d con ese nombre)' % len(cands)

def b64(a, dt): return base64.b64encode(np.ascontiguousarray(a.astype(dt)).tobytes()).decode('ascii')

def api_modelo(rel):
    full = os.path.normpath(os.path.join(ARGS.catalogo, *rel.replace('/', '\\').split('\\')))
    if not full.lower().startswith(os.path.normpath(ARGS.catalogo).lower() + os.sep) or not os.path.isfile(full):
        raise ValueError('no esta en el catalogo: ' + rel)
    f = Fmdl(full)
    mats = materiales(f)
    for m in mats:
        for t in m['texturas']:
            t['ruta'], t['como'] = buscar_textura(t['carpeta'], t['archivo'])
    out = {'r': rel, 'bytes': len(f.d), 'huesos': len(f.bones), 'materiales': mats, 'mallas': [], 'avisos': []}
    for i, m in enumerate(fmdl_meshes(f)):
        if m['pos'] is None:
            out['avisos'].append('malla %d: posicion en un formato que el visor no lee (se salta)' % i); continue
        if m['faces'] is None or not len(m['faces']):
            out['avisos'].append('malla %d: sin caras' % i); continue
        fc = m['faces']
        if fc.max() >= m['nvert']:
            out['avisos'].append('malla %d: caras fuera de rango (se salta)' % i); continue
        d = {'i': i, 'mat': int(m['mat']), 'nvert': int(m['nvert']), 'ncaras': int(len(fc)),
             'pos': b64(m['pos'], np.float32), 'caras': b64(fc, np.uint32)}
        if m['nrm'] is not None: d['nrm'] = b64(m['nrm'][:, :3], np.float32)
        if m['uv'] is not None: d['uv'] = b64(m['uv'], np.float32)
        out['mallas'].append(d)
    return out

# ---------------------------------------------------------------- texturas
DXGI = {11: 98, 8: 80, 9: 83, 10: 95}                    # 11 = BC7 (visto en ftex de caras). 8/9/10 = BC4/BC5/BC6H: INFERIDO
FOURCC = {2: b'DXT1', 3: b'DXT3', 4: b'DXT5'}
TOPE = 1024

def ftex_a_png(data):
    ft = Ftex(data)
    i = 0
    while i + 1 < len(ft.mips) and max(ft.mip_size(i)) > TOPE: i += 1
    w, h = ft.mip_size(i)
    mip = ft.mips[i]
    if ft.fmt == 0:
        im = Image.frombytes('RGBA', (w, h), mip[:w * h * 4], 'raw', 'BGRA')
    else:
        hdr = struct.pack('<4sIIIIIII', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000, h, w, len(mip), 0, 1) + b'\0' * 44
        if ft.fmt in FOURCC:
            hdr += struct.pack('<II4sIIIII', 32, 0x4, FOURCC[ft.fmt], 0, 0, 0, 0, 0) + struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
        elif ft.fmt in DXGI:
            hdr += struct.pack('<II4sIIIII', 32, 0x4, b'DX10', 0, 0, 0, 0, 0) + struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
            hdr += struct.pack('<IIIII', DXGI[ft.fmt], 3, 0, 1, 0)
        else:
            raise ValueError('formato ftex %d sin decodificar' % ft.fmt)
        im = Image.open(io.BytesIO(hdr + mip)); im.load()
    return im.convert('RGBA'), ft.fmt, ft.w, ft.h

def api_textura(clave):
    fu = IDX['ruta'].get(clave)
    if not fu: raise KeyError(clave)
    h = hashlib.md5((clave + json.dumps(fu)).encode('utf-8')).hexdigest()[:16]
    dest = os.path.join(ARGS.trabajo, 'texturas', h + '.png')
    if os.path.isfile(dest): return open(dest, 'rb').read()
    data = leer_fuente(fu)
    if data[:4] == b'FTEX':
        im, *_ = ftex_a_png(data)
    else:
        im = Image.open(io.BytesIO(data)); im.load(); im = im.convert('RGBA')
        if max(im.size) > TOPE: im.thumbnail((TOPE, TOPE))
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    b = io.BytesIO(); im.save(b, 'PNG', compress_level=1)
    open(dest, 'wb').write(b.getvalue())
    return b.getvalue()

# ---------------------------------------------------------------- servidor (solo 127.0.0.1)
TIPOS = {'.html': 'text/html; charset=utf-8', '.js': 'application/javascript', '.css': 'text/css', '.png': 'image/png', '.txt': 'text/plain'}

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _send(self, code, body, tipo='application/json; charset=utf-8'):
        if not isinstance(body, (bytes, bytearray)): body = json.dumps(body, ensure_ascii=False).encode('utf-8')
        self.send_response(code); self.send_header('Content-Type', tipo); self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store'); self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = dict(urllib.parse.parse_qsl(u.query)); p = u.path
        try:
            if p == '/api/estado': return self._send(200, dict(ESTADO, catalogo=ARGS.catalogo, juego=ARGS.juego, trabajo=ARGS.trabajo))
            if p == '/api/lista': return self._send(200, api_lista())
            if p == '/api/modelo': return self._send(200, api_modelo(q['r']))
            if p == '/api/textura': return self._send(200, api_textura(q['k']), 'image/png')
            if p == '/': p = '/index.html'
            f = os.path.normpath(os.path.join(BASE, 'static', p.lstrip('/')))
            if f.startswith(os.path.normpath(os.path.join(BASE, 'static'))) and os.path.isfile(f):
                return self._send(200, open(f, 'rb').read(), TIPOS.get(os.path.splitext(f)[1], 'application/octet-stream'))
            self._send(404, {'error': 'no existe'})
        except KeyError as e: self._send(404, {'error': 'no encontrado: %s' % e})
        except Exception as e:
            import traceback; traceback.print_exc(); self._send(500, {'error': str(e)})

def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument('--catalogo', default=r'B:\VisualPes21\catalogo_fmdl')
    ap.add_argument('--juego', default=JUEGO)
    ap.add_argument('--trabajo', default=r'B:\VisualPes21\visor_objetos')
    ap.add_argument('--puerto', type=int, default=21030)
    ap.add_argument('--navegador', action='store_true', help='abrir en el navegador en vez de ventana propia')
    ARGS = ap.parse_args()
    try:
        os.makedirs(ARGS.trabajo, exist_ok=True)
        log = open(os.path.join(ARGS.trabajo, 'visor_objetos.log'), 'a', encoding='utf-8', buffering=1)
        if sys.stdout is None or getattr(sys, 'frozen', False): sys.stdout = sys.stderr = log
        print('---', time.strftime('%Y-%m-%d %H:%M:%S'), 'catalogo', ARGS.catalogo, 'juego', ARGS.juego)
    except Exception: pass
    threading.Thread(target=armar_indice, daemon=True).start()
    srv = ThreadingHTTPServer(('127.0.0.1', ARGS.puerto), H)
    url = 'http://127.0.0.1:%d/' % srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    if not ARGS.navegador:
        try:
            import webview
            webview.create_window('Visor de objetos - PES21 FX', url, width=1500, height=900)
            webview.start(); return
        except Exception as e:
            print('sin ventana propia (%s): se abre en el navegador' % e)
    import webbrowser; webbrowser.open(url)
    try:
        while True: time.sleep(3600)
    except KeyboardInterrupt: pass

if __name__ == '__main__':
    main()
