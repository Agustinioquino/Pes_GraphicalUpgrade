#!/usr/bin/env python3
# PES21 FX (08/11) - VISOR: servidor local (solo 127.0.0.1) que abre los archivos del juego para la pagina static\index.html:
# carpetas y .cpk / .fpk (navegacion y extraccion perezosa), .fmdl (mallas + huesos en 3D), .ftex / .dds (texturas y sus canales),
# .gani / .ask / .frig (curvas de la cara), .ini / .lua / .log (texto) y StrCode32. Reusa tools\cpk_extract.py, tools\ftex.py,
# tools\fox\fmdl.py, tools\fox\fpk_edit.py, tools\gani_cara.py y tools\foxhash.py. No escribe en la carpeta del juego: lo que
# extrae va a la carpeta de trabajo (--trabajo, en un disco con espacio). Solo lectura sobre el juego.
#
# Uso (en la PC de Agustin):
#   python tools\visor\visor.py --juego "D:\SteamLibrary\steamapps\common\eFootball PES 2021" --trabajo "B:\VisualPes21" [--puerto 21020]
#   y abrir http://127.0.0.1:21020/   (el panel en vivo del juego es http://127.0.0.1:21021/, lo abre la DLL si esta prendido)
#
# Rutas: una ruta normal del disco, o dentro de un archivo: "D:\...\dt13_all.cpk!common/anime/.../x.gani" (tambien para .fpk).
# Solo se sirven rutas dentro de las raices (--juego, --trabajo, la carpeta del repo y las que se agreguen con --raiz).
import os, sys, io, json, struct, base64, hashlib, argparse, threading, time, mimetypes, urllib.parse, math
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS); sys.path.insert(0, os.path.join(TOOLS, 'fox'))
import cpk_extract, ftex as ftexmod, fpk_edit, foxhash, gani_cara
from fmdl import Fmdl, meshes as fmdl_meshes
import numpy as np
try:
    from PIL import Image
except ImportError:
    Image = None

ARGS = None
ROOTS = []
LOCK = threading.Lock()
CPK_CACHE = {}   # ruta -> (archivo abierto, lista de entradas, dict nombre -> entrada)

def norm(p): return os.path.normcase(os.path.normpath(os.path.abspath(p)))
def dentro(p):
    n = norm(p)
    return any(n == r or n.startswith(r + os.sep) for r in ROOTS)

class Err(Exception):
    def __init__(self, code, msg): self.code = code; self.msg = msg

# ---------------------------------------------------------------- rutas (disco o dentro de cpk / fpk)
def cache_dir(archivo):
    h = hashlib.md5(norm(archivo).encode('utf-8')).hexdigest()[:8]
    return os.path.join(ARGS.trabajo, 'cache', os.path.basename(archivo) + '_' + h)

def abrir_cpk(path):
    with LOCK:
        c = CPK_CACHE.get(norm(path))
        if c: return c
        f, files = cpk_extract.open_cpk(path)
        by = {e[0].lower(): e for e in files}
        CPK_CACHE[norm(path)] = (f, files, by)
        return CPK_CACHE[norm(path)]

def resolver(ruta):
    """ruta del disco o 'archivo!entrada' -> ruta real de un archivo del disco (extrae a la cache si hace falta)"""
    if '!' not in ruta:
        if not dentro(ruta): raise Err(403, 'fuera de las carpetas permitidas: ' + ruta)
        if not os.path.isfile(ruta): raise Err(404, 'no existe: ' + ruta)
        return ruta
    arch, entrada = ruta.split('!', 1)
    arch = resolver(arch)
    entrada = entrada.replace('\\', '/').strip('/')
    if '..' in entrada.split('/'): raise Err(400, 'entrada invalida')
    dest = os.path.join(cache_dir(arch), *entrada.split('/'))
    if os.path.isfile(dest): return dest
    low = arch.lower()
    if low.endswith('.cpk'):
        f, files, by = abrir_cpk(arch)
        e = by.get(entrada.lower())
        if not e: raise Err(404, 'no esta en el cpk: ' + entrada)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with LOCK:
            f.seek(e[1]); data = f.read(e[2])
        if data[:8] == b'CRILAYLA': data = cpk_extract.crilayla_decompress(data)
        open(dest, 'wb').write(data)
        return dest
    if low.endswith('.fpk') or low.endswith('.fpkd'):
        fp = fpk_edit.read(open(arch, 'rb').read())
        for name, blob, _ in fp['ents']:
            if name.replace('\\', '/').strip('/').lower() == entrada.lower():
                os.makedirs(os.path.dirname(dest), exist_ok=True); open(dest, 'wb').write(blob); return dest
        raise Err(404, 'no esta en el fpk: ' + entrada)
    raise Err(400, 'no se sabe abrir ' + arch)

# ---------------------------------------------------------------- respuestas
def api_info():
    return {'juego': ARGS.juego, 'trabajo': ARGS.trabajo, 'repo': os.path.dirname(TOOLS), 'raices': ROOTS, 'panel': 'http://127.0.0.1:%d' % ARGS.panel,
            'pillow': Image is not None}

def api_ls(ruta):
    if not ruta:
        return {'ruta': '', 'carpetas': [{'nombre': r, 'ruta': r} for r in [ARGS.juego, ARGS.trabajo, os.path.dirname(TOOLS)] + ARGS.raiz], 'archivos': []}
    if '!' in ruta or ruta.lower().endswith(('.cpk', '.fpk', '.fpkd')):
        return api_ls_archivo(ruta)
    if not dentro(ruta): raise Err(403, 'fuera de las carpetas permitidas')
    if not os.path.isdir(ruta): raise Err(404, 'no es una carpeta')
    carpetas, archivos = [], []
    for n in sorted(os.listdir(ruta), key=str.lower):
        p = os.path.join(ruta, n)
        try:
            if os.path.isdir(p): carpetas.append({'nombre': n, 'ruta': p})
            else: archivos.append({'nombre': n, 'ruta': p, 'tam': os.path.getsize(p), 'tipo': tipo_de(n)})
        except OSError: pass
    return {'ruta': ruta, 'carpetas': carpetas, 'archivos': archivos}

def tipo_de(n):
    n = n.lower()
    for ext, t in (('.fmdl', 'modelo'), ('.ftex', 'textura'), ('.dds', 'textura'), ('.png', 'imagen'), ('.jpg', 'imagen'), ('.gani', 'animacion'), ('.mtar', 'animacion'),
                   ('.ask', 'esqueleto'), ('.frig', 'esqueleto'), ('.cpk', 'paquete'), ('.fpk', 'paquete'), ('.fpkd', 'paquete'), ('.ini', 'texto'), ('.lua', 'texto'),
                   ('.log', 'texto'), ('.txt', 'texto'), ('.hlsl', 'texto'), ('.hlsli', 'texto'), ('.md', 'texto'), ('.json', 'texto'), ('.fmtr', 'material'), ('.fox2', 'fox2')):
        if n.endswith(ext): return t
    return ''

def api_ls_archivo(ruta):
    """lista el contenido de un cpk / fpk como arbol virtual (un nivel por pedido)"""
    arch, sub = (ruta.split('!', 1) + [''])[:2]
    sub = sub.replace('\\', '/').strip('/')
    arch_real = resolver(arch)
    low = arch_real.lower()
    entradas = []
    if low.endswith('.cpk'):
        f, files, by = abrir_cpk(arch_real)
        entradas = [(e[0], e[3] or e[2]) for e in files]
    else:
        fp = fpk_edit.read(open(arch_real, 'rb').read())
        entradas = [(n.replace('\\', '/').strip('/'), len(b)) for n, b, _ in fp['ents']]
    carpetas, archivos = {}, []
    pre = sub + '/' if sub else ''
    for name, size in entradas:
        if pre and not name.lower().startswith(pre.lower()): continue
        rest = name[len(pre):]
        if '/' in rest:
            d = rest.split('/', 1)[0]
            carpetas[d] = carpetas.get(d, 0) + 1
        else:
            archivos.append({'nombre': rest, 'ruta': arch + '!' + name, 'tam': size, 'tipo': tipo_de(rest)})
    return {'ruta': ruta, 'archivo': arch, 'total': len(entradas),
            'carpetas': [{'nombre': d, 'ruta': arch + '!' + pre + d, 'n': c} for d, c in sorted(carpetas.items(), key=lambda x: x[0].lower())],
            'archivos': sorted(archivos, key=lambda a: a['nombre'].lower())}

def api_buscar(ruta, patron, limite=500):
    arch_real = resolver(ruta.split('!')[0]); pat = patron.lower()
    if arch_real.lower().endswith('.cpk'):
        f, files, by = abrir_cpk(arch_real); src = [(e[0], e[3] or e[2]) for e in files]
    else:
        fp = fpk_edit.read(open(arch_real, 'rb').read()); src = [(n, len(b)) for n, b, _ in fp['ents']]
    out = [{'nombre': n.rsplit('/', 1)[-1], 'ruta': ruta.split('!')[0] + '!' + n, 'tam': s, 'tipo': tipo_de(n)} for n, s in src if pat in n.lower()]
    return {'n': len(out), 'archivos': out[:limite]}

def b64(a, dtype):
    return base64.b64encode(np.ascontiguousarray(a.astype(dtype)).tobytes()).decode('ascii')

def api_fmdl(ruta):
    real = resolver(ruta)
    f = Fmdl(real)
    ms = fmdl_meshes(f)
    out = {'ruta': ruta, 'version': f.ver, 'cadenas': f.strings, 'bloques': {'s0': sorted(f.b0), 's1': sorted(f.b1)},
           'huesos': [{'nombre': b['nombre'] if 'nombre' in b else b['name'], 'padre': b['parent'], 'local': list(b['local']), 'mundo': list(b['world'])} for b in f.bones],
           'grupos': f.groups, 'mallas': []}
    texturas = [s for s in f.strings if s.lower().endswith(('.ftex', '.dds', '.tga', '.png'))]
    out['texturas'] = texturas
    out['materiales'] = api_fmdl_materiales(f)
    for i, m in enumerate(ms):
        if m['pos'] is None: continue
        d = {'i': i, 'nvert': int(m['nvert']), 'grupo': int(m['group']), 'mat': int(m['mat']), 'pos': b64(m['pos'], np.float32)}
        if m['faces'] is not None: d['caras'] = b64(m['faces'], np.uint32); d['ncaras'] = int(len(m['faces']))
        if m['nrm'] is not None: d['nrm'] = b64(m['nrm'][:, :3], np.float32)
        if m['uv'] is not None: d['uv'] = b64(m['uv'], np.float32)
        if m['w'] is not None and m['idx'] is not None:
            d['pesos'] = b64(m['w'], np.float32); d['huesos'] = b64(m['idx'], np.uint16)
            # cuanto pesa cada hueso en la malla (para la lista de la pagina)
            tot = {}
            for k in range(4):
                for bid, w in zip(m['idx'][:, k].tolist(), m['w'][:, k].tolist()):
                    if w > 0: tot[bid] = tot.get(bid, 0.0) + w
            d['pesoPorHueso'] = {str(k): round(v, 2) for k, v in sorted(tot.items(), key=lambda x: -x[1])[:40]}
        bb = m['pos'].min(axis=0).tolist() + m['pos'].max(axis=0).tolist()
        d['caja'] = [float(x) for x in bb]
        out['mallas'].append(d)
    return out

def api_fmdl_materiales(f):
    """instancias de material (bloque 4: 0x10 B: nombre, tipo, nTex, nParam, primera tex, primer param) y sus texturas
    (bloque 6: nombre, ruta; bloque 7: asignacion). Mejor esfuerzo: si el layout no coincide se devuelve lo que se pueda."""
    out = []
    try:
        d = f.d
        if 4 not in f.b0: return out
        cnt, off = f.b0[4]
        tex_cnt, tex_off = f.b0.get(7, (0, 0))
        for i in range(cnt):
            name, typ, ntex, npar, ftex, fpar = struct.unpack_from('<HHBBHH', d, off + 0x10 * i)
            def s(k): return f.strings[k] if k < len(f.strings) else '#%d' % k
            texs = []
            for t in range(ntex):
                if tex_off and ftex + t < tex_cnt:
                    tn, tp = struct.unpack_from('<HH', d, tex_off + 4 * (ftex + t))
                    texs.append({'slot': s(tn), 'textura': s(tp)})
            out.append({'nombre': s(name), 'tipo': s(typ), 'texturas': texs})
    except Exception as e:
        out.append({'error': str(e)})
    return out

def ftex_a_png(real, mip=0, canal='rgb'):
    data = open(real, 'rb').read()
    if data[:4] == b'FTEX':
        ft = ftexmod.Ftex(data)
        info = {'formato': ft.fmt, 'ancho': ft.w, 'alto': ft.h, 'mips': ft.mipCount}
        mip = max(0, min(mip, ft.mipCount - 1))
        w, h = ft.mip_size(mip)
        if ft.fmt == 0:
            arr = np.frombuffer(ft.mips[mip], dtype=np.uint8).reshape(h, w, 4)[:, :, [2, 1, 0, 3]]
            img = Image.fromarray(np.ascontiguousarray(arr), 'RGBA') if Image else None
            dds = None
        elif ft.fmt in ftexmod.BLOCK:
            sub = ftexmod.Ftex.__new__(ftexmod.Ftex); sub.fmt = ft.fmt; sub.w = w; sub.h = h; sub.mips = [ft.mips[mip]]
            dds = ftexmod.dxt_to_dds(sub, True)
            img = Image.open(io.BytesIO(dds)).convert('RGBA') if Image else None
        else:
            raise Err(415, 'formato ftex %d no soportado' % ft.fmt)
    elif data[:4] == b'DDS ':
        info = {'formato': 'dds'}; dds = data
        if not Image: raise Err(415, 'sin Pillow')
        img = Image.open(io.BytesIO(data)).convert('RGBA'); info.update(ancho=img.width, alto=img.height)
    else:
        if not Image: raise Err(415, 'sin Pillow')
        img = Image.open(io.BytesIO(data)).convert('RGBA'); info = {'formato': 'imagen', 'ancho': img.width, 'alto': img.height}; dds = None
    if img is None: raise Err(415, 'sin Pillow no se decodifica; instalar: pip install pillow')
    if canal in ('r', 'g', 'b', 'a'):
        ch = img.getchannel({'r': 'R', 'g': 'G', 'b': 'B', 'a': 'A'}[canal]); img = Image.merge('RGB', (ch, ch, ch))
    elif canal == 'rgb':
        img = img.convert('RGB')
    elif canal == 'normal':   # mapa de normales con el verde al derecho y el azul reconstruido
        a = np.asarray(img).astype(np.float32) / 255.0
        x = a[:, :, 0] * 2 - 1; y = a[:, :, 1] * 2 - 1; z = np.sqrt(np.clip(1 - x * x - y * y, 0, 1))
        img = Image.fromarray((np.stack([x, y, z], -1) * 0.5 + 0.5).clip(0, 1).astype(np.float32).__mul__(255).astype(np.uint8), 'RGB')
    bio = io.BytesIO(); img.save(bio, 'PNG'); return bio.getvalue(), info

def api_ftex_info(ruta):
    data = open(resolver(ruta), 'rb').read()
    if data[:4] != b'FTEX': return {'formato': 'otro', 'tam': len(data)}
    ft = ftexmod.Ftex(data)
    return {'formato': ft.fmt, 'nombreFormato': {0: 'RGBA8', 2: 'DXT1', 3: 'DXT3', 4: 'DXT5'}.get(ft.fmt, '?'), 'ancho': ft.w, 'alto': ft.h, 'profundidad': ft.d,
            'mips': ft.mipCount, 'tam': len(data), 'cabecera': ft.header.hex()}

def api_gani(ruta, ask, frig):
    g = open(resolver(ruta), 'rb').read(); G = gani_cara.leer_gani(g)
    salida = {'gani': {k: v for k, v in G.items() if k != 'canales'}}
    if ask: salida['ask'] = gani_cara.leer_ask(open(resolver(ask), 'rb').read())
    if frig: salida['frig'] = {k: v for k, v in gani_cara.leer_frig(open(resolver(frig), 'rb').read()).items() if k != 'unidades'} | {'unidades': [{k: v for k, v in u.items() if k != 'crudo'} for u in gani_cara.leer_frig(open(resolver(frig), 'rb').read())['unidades']]}
    cm = gani_cara.mapa_canales(gani_cara.leer_frig(open(resolver(frig), 'rb').read())['unidades'], G['canales']) if frig else {}
    nombresAsk = [b['nombre'] for b in salida['ask']['huesos']] if ask else []
    pistas = []
    for c in G['canales']:
        data = g[c['off']:c['off'] + c['size']]
        if c['bits'] == 16:
            k = gani_cara.claves_pos(data); vals = [[f] + [round(x, 5) for x in v] for f, v in k]; tipo = 'pos'
        else:
            k = gani_cara.claves_rot(data, c['bits']); vals = []; prev = None
            for f, v, s in k:
                q = gani_cara.quat(v, s)
                if prev is not None and sum(p * x for p, x in zip(prev, q)) < 0: q = [-x for x in q]
                prev = q; vals.append([f] + [round(x, 6) for x in q])
            tipo = 'rot'
        nombre = gani_cara.NOMBRES.get(c['grupo'], '%08X' % c['grupo']); hueso = None
        if c['indice'] in cm:
            k_, b_ = cm[c['indice']]; hueso = b_; nombre = 'RAIZ' if b_ < 0 else (nombresAsk[b_] if b_ < len(nombresAsk) else 'hueso%d' % b_)
        pistas.append({'indice': c['indice'], 'nombre': nombre, 'hueso': hueso, 'flags': c['flags'], 'bits': c['bits'], 'tipo': tipo, 'claves': vals, 'bytes': c['size']})
    salida['pistas'] = pistas
    return salida

def api_texto(ruta, maximo=2 * 1024 * 1024, cola=False):
    real = resolver(ruta); n = os.path.getsize(real)
    with open(real, 'rb') as f:
        if cola and n > maximo: f.seek(n - maximo)
        data = f.read(maximo)
    try: txt = data.decode('utf-8')
    except UnicodeDecodeError: txt = data.decode('cp1252', 'replace')
    return {'ruta': ruta, 'tam': n, 'recortado': n > maximo, 'texto': txt}

def api_hex(ruta, off=0, n=512):
    real = resolver(ruta); tam = os.path.getsize(real)
    with open(real, 'rb') as f: f.seek(max(0, off)); data = f.read(min(n, 65536))
    return {'ruta': ruta, 'tam': tam, 'off': off, 'hex': data.hex()}

def api_ini(ruta):
    """PES21FX.ini: filas [Constants] (x12 = 1) con sus comentarios anteriores y las tarjetas [Menu...]"""
    t = api_texto(ruta)['texto']; sec = None; consts = {}; menus = []; com = []; cur = None
    for raw in t.splitlines():
        l = raw.strip()
        if not l: com = []; continue
        if l.startswith(';'): com.append(l[1:].strip()); continue
        if l.startswith('['):
            sec = l[1:l.find(']')]; com = []
            if sec.lower().startswith('menu'): cur = {'id': sec[4:], 'claves': {}}; menus.append(cur)
            else: cur = None
            continue
        if '=' not in l: continue
        k, v = [x.strip() for x in l.split('=', 1)]
        if sec and sec.lower() == 'constants':
            consts[k] = {'valor': v, 'comentario': ' '.join(com)}; com = []
        elif cur is not None:
            cur['claves'].setdefault(k, []).append(v)
    return {'constants': consts, 'menus': menus}

# ---------------------------------------------------------------- servidor
class H(BaseHTTPRequestHandler):
    server_version = 'PES21FX-visor/1'
    def log_message(self, fmt, *a):
        if ARGS.verboso: sys.stderr.write('%s %s\n' % (self.address_string(), fmt % a))
    def _send(self, code, body, ctype='application/json', extra=None):
        if isinstance(body, (dict, list)): body = json.dumps(body, ensure_ascii=False).encode('utf-8')
        elif isinstance(body, str): body = body.encode('utf-8')
        self.send_response(code); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store'); self.send_header('Access-Control-Allow-Origin', 'http://127.0.0.1:%d' % ARGS.panel)
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        try:
            u = urllib.parse.urlsplit(self.path); q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
            p = urllib.parse.unquote(u.path); ruta = q.get('ruta', '')
            if p == '/api/info': return self._send(200, api_info())
            if p == '/api/ls': return self._send(200, api_ls(ruta))
            if p == '/api/buscar': return self._send(200, api_buscar(ruta, q.get('q', ''), int(q.get('n', 500))))
            if p == '/api/fmdl': return self._send(200, api_fmdl(ruta))
            if p == '/api/ftex': return self._send(200, api_ftex_info(ruta))
            if p == '/api/png':
                png, info = ftex_a_png(resolver(ruta), int(q.get('mip', 0)), q.get('canal', 'rgb'))
                return self._send(200, png, 'image/png', {'X-Info': json.dumps(info)})
            if p == '/api/gani': return self._send(200, api_gani(ruta, q.get('ask', ''), q.get('frig', '')))
            if p == '/api/texto': return self._send(200, api_texto(ruta, cola=q.get('cola') == '1'))
            if p == '/api/hex': return self._send(200, api_hex(ruta, int(q.get('off', 0)), int(q.get('n', 512))))
            if p == '/api/ini': return self._send(200, api_ini(ruta))
            if p == '/api/hash':
                names = [n for n in q.get('nombre', '').replace(',', ' ').split() if n]
                return self._send(200, {n: '%08X' % foxhash.strcode32(n) for n in names} | {'conocidos': {('%08X' % foxhash.strcode32(n)): n for n in foxhash.HUESOS}})
            if p == '/api/archivo':
                real = resolver(ruta); data = open(real, 'rb').read()
                return self._send(200, data, mimetypes.guess_type(real)[0] or 'application/octet-stream')
            if p == '/api/extraer':   # extrae a la carpeta de trabajo y devuelve la ruta real
                return self._send(200, {'ruta': resolver(ruta)})
            # estaticos
            n = 'index.html' if p == '/' else p.lstrip('/')
            if '..' in n: raise Err(400, 'ruta')
            fp = os.path.join(HERE, 'static', *n.split('/'))
            if not os.path.isfile(fp): raise Err(404, 'no existe ' + n)
            return self._send(200, open(fp, 'rb').read(), mimetypes.guess_type(fp)[0] or 'application/octet-stream')
        except Err as e:
            self._send(e.code, {'error': e.msg})
        except Exception as e:
            import traceback; traceback.print_exc()
            self._send(500, {'error': '%s: %s' % (type(e).__name__, e)})

def main():
    global ARGS, ROOTS
    ap = argparse.ArgumentParser(description='Visor de archivos de PES 2021 (solo lectura, solo 127.0.0.1)')
    ap.add_argument('--juego', default=r'D:\SteamLibrary\steamapps\common\eFootball PES 2021')
    ap.add_argument('--trabajo', default=r'B:\VisualPes21', help='carpeta con espacio para extraer (cache)')
    ap.add_argument('--raiz', action='append', default=[], help='otra carpeta permitida (se puede repetir)')
    ap.add_argument('--puerto', type=int, default=21020)
    ap.add_argument('--panel', type=int, default=21021, help='puerto del panel en vivo de la DLL (panel_puerto del ini)')
    ap.add_argument('--verboso', action='store_true')
    ap.add_argument('--sin-ventana', action='store_true', help='solo el servidor (abrir la pagina en el navegador)')
    ARGS = ap.parse_args()
    os.makedirs(os.path.join(ARGS.trabajo, 'cache'), exist_ok=True)
    ROOTS = [norm(ARGS.juego), norm(ARGS.trabajo), norm(os.path.dirname(TOOLS))] + [norm(r) for r in ARGS.raiz]
    srv = ThreadingHTTPServer(('127.0.0.1', ARGS.puerto), H)
    url = 'http://127.0.0.1:%d/' % ARGS.puerto
    print('visor: %s   juego: %s   trabajo: %s   panel en vivo: http://127.0.0.1:%d/' % (url, ARGS.juego, ARGS.trabajo, ARGS.panel))
    if ARGS.sin_ventana:
        try: srv.serve_forever()
        except KeyboardInterrupt: pass
        return
    # (08/11, Agustin: "como aplicacion y no como una pagina") ventana propia con pywebview (WebView2 de Windows): el servidor
    # corre en un hilo y la app es la ventana; al cerrarla se apaga todo. Sin pywebview: navegador.
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    try:
        import webview
    except ImportError:
        print('sin pywebview (pip install pywebview): se abre en el navegador'); import webbrowser; webbrowser.open(url)
        try:
            while True: time.sleep(3600)
        except KeyboardInterrupt: pass
        return
    w = webview.create_window('PES21 FX Visor', url, width=1500, height=940, min_size=(1000, 650), background_color='#0f1115')
    webview.start(private_mode=False, storage_path=os.path.join(ARGS.trabajo, 'app'))
    srv.shutdown()

if __name__ == '__main__': main()
