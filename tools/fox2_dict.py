#!/usr/bin/env python3
# PES21 FX (08/11) - Arma el diccionario StrCode -> nombre que usa fox2_dump.py (/home/claude/re/strcode32_dict.tsv).
# Fuentes: (a) tablas de cadenas de los .fox2 (hash ya resuelto, no hace falta calcularlo); (b) nombres de clase del RTTI
# (ultimo componente de `fox::GrSpotLight` -> `GrSpotLight`); (c) cadenas tipo identificador de strings_idata.txt y
# strings_otras.txt (nombres de propiedades de las tablas de reflexion, enums, etc.), con variantes (primera letra en
# minuscula, sin prefijos Get/Set/Is/m_); (d) reflexion.tsv si existe; (e) una lista corta de nombres conocidos de Fox2.
# Salida TSV: hash32(8 hex)  nombre  hash48(12 hex)  fuente   -- ordenado por hash32; StrCode32 = 32 bits bajos del StrCode.
# Uso: python3 -I fox2_dict.py [--salida /home/claude/re/strcode32_dict.tsv] [--fox2 glob ...] [--extra archivo_con_nombres]
import sys, os, re, glob, struct, time, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import foxhash

RE = '/home/claude/re'
IDENT = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')
CONOCIDOS = '''name dataSet dataList parent children transform flags transformMatrix inheritTransform visible enable
transform_translation transform_rotation_quat transform_scale shearTransform pivotTransform owner
color intensity position rotation scale direction radius innerRadius outerRadius penumbra attenuation lightSize shadow
castShadow receiveShadow near far width height fovy fov aspect rotationX rotationY rotationZ translation
models skeletonFile sklFile textures texture material materials lod lodCount boundingBox min max
file path filePath packagePath archivePath nameInArchive address entity owner target targets
lightType spotLight pointLight dirLight ambient diffuse specular shadowBias shadowMapSize exposure gamma
reachPoint lumen lux temperature colorDeflection shadowUmbraAngle shadowPenumbraAngle shadowAttenuation
dimmer hasSpecular lightFlag interiorFlag lightGroup hasShadow shadowMode cutoff maxDistance baseDistance
layer group priority order index count size type version id tag tags comment description
timeOfDay time hour minute second sunLux skyLux moonLux sunColor skyColor moonColor
parameter parameters param params value values key keys data dataSetHandle'''.split()


def lee_nombres_tsv(path, col):
    out = []
    if not os.path.exists(path): return out
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for ln in f:
            ln = ln.rstrip('\n')
            if not ln or ln.startswith('#'): continue
            parts = ln.split('\t')
            if len(parts) > col: out.append(parts[col])
    return out


def variantes(nombre):
    yield nombre
    if nombre[:1].isupper() and len(nombre) > 1:
        yield nombre[0].lower() + nombre[1:]
    for pre in ('Get', 'Set', 'Is', 'Has', 'm_', 'm', '_'):
        if nombre.startswith(pre) and len(nombre) > len(pre) + 1 and nombre[len(pre)].isupper():
            r = nombre[len(pre):]
            yield r
            yield r[0].lower() + r[1:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--salida', default=os.path.join(RE, 'strcode32_dict.tsv'))
    ap.add_argument('--fox2', nargs='*', default=['/home/claude/cpk/fpk/*/Assets/**/*.fox2'])
    ap.add_argument('--extra', nargs='*', default=[])
    ap.add_argument('--sin-otras', action='store_true', help='no usar strings_otras.txt')
    a = ap.parse_args()
    t0 = time.time()
    res = {}   # hash48 -> [nombre, {fuentes}]
    colisiones = []

    def pon(h48, nombre, fuente):
        r = res.get(h48)
        if r is None: res[h48] = [nombre, {fuente}]
        elif r[0] == nombre: r[1].add(fuente)
        else: colisiones.append((h48, r[0], nombre))      # mismo StrCode48, distinto texto (no deberia pasar)

    # (a) tablas de cadenas de los .fox2: hash + texto ya presentes
    n_a = 0
    for g in a.fox2:
        for f in glob.glob(g, recursive=True):
            d = open(f, 'rb').read()
            if d[:4] != b'\xf2box': continue
            p = struct.unpack_from('<I', d, 0xC)[0]
            while p + 12 <= len(d):
                h, ln = struct.unpack_from('<QI', d, p)
                if h == 0 and ln == 0: break
                pon(h, d[p + 12:p + 12 + ln].decode('utf-8', 'replace'), 'fox2'); n_a += 1
                p += 12 + ln
    print('(a) fox2: %d entradas' % n_a, file=sys.stderr)

    cand = {}  # nombre -> fuente (el primero gana)

    def cand_add(n, fuente):
        if n and n not in cand: cand[n] = fuente

    for n in CONOCIDOS: cand_add(n, 'conocido')
    # (b) RTTI: ultimo componente, sin plantillas
    for ln in open(os.path.join(RE, 'rtti.txt'), encoding='utf-8', errors='replace'):
        parts = ln.split()
        if len(parts) < 3: continue
        cls = re.sub(r'<.*', '', parts[2])
        last = cls.split('::')[-1]
        if IDENT.match(last): cand_add(last, 'rtti')
        if IDENT.match(cls.replace('::', '_')) and '::' in cls: cand_add(cls, 'rtti_full')
    # (d) reflexion.tsv
    for col in (0, 1, 2):
        for n in lee_nombres_tsv(os.path.join(RE, 'out', 'reflexion.tsv'), col):
            if IDENT.match(n): cand_add(n, 'reflexion')
    for ex in a.extra:
        for ln in open(ex, encoding='utf-8', errors='replace'):
            n = ln.strip()
            if n: cand_add(n, 'extra')
    # (c) cadenas del exe: ultimo campo separado por tabulador
    fuentes = [('idata', os.path.join(RE, 'strings_idata.txt'))]
    if not a.sin_otras: fuentes.append(('otras', os.path.join(RE, 'strings_otras.txt')))
    for tag, path in fuentes:
        if not os.path.exists(path): continue
        for ln in open(path, encoding='utf-8', errors='replace'):
            s = ln.rstrip('\n').split('\t')[-1]
            if len(s) > 80: continue
            if IDENT.match(s):
                cand_add(s, tag)
            elif '::' in s:                      # fox::Clase::metodo -> componentes
                for comp in re.sub(r'<.*', '', s).split('::'):
                    if IDENT.match(comp): cand_add(comp, tag + '_ns')
            elif '.' in s and IDENT.match(s.replace('.', '_')):   # Lua: GrPluginPesShadow.ShadowResolution
                for comp in s.split('.'):
                    if IDENT.match(comp): cand_add(comp, tag + '_punto')
    print('candidatos base: %d (%.1fs)' % (len(cand), time.time() - t0), file=sys.stderr)
    # variantes
    todos = {}
    for n, fu in cand.items():
        for v in variantes(n):
            if v not in todos: todos[v] = fu if v == n else fu + '_var'
    print('candidatos con variantes: %d' % len(todos), file=sys.stderr)
    sc = foxhash.strcode
    i = 0
    for n, fu in todos.items():
        pon(sc(n), n, fu); i += 1
        if i % 100000 == 0: print('  %d hasheados (%.0fs)' % (i, time.time() - t0), file=sys.stderr)
    with open(a.salida, 'w', encoding='utf-8') as f:
        f.write('# StrCode32 (hex)\tnombre\tStrCode48 (hex)\tfuentes (separadas por coma)   -- fox2_dict.py %s; %d entradas\n' % (
            time.strftime('%Y-%m-%d'), len(res)))
        for h48 in sorted(res, key=lambda h: (h & 0xFFFFFFFF, h)):
            n, fu = res[h48]
            f.write('%08X\t%s\t%012X\t%s\n' % (h48 & 0xFFFFFFFF, n, h48, ','.join(sorted(fu))))
    for h48, a1, a2 in colisiones[:20]:
        print('colision StrCode48 %012X: %r vs %r' % (h48, a1, a2), file=sys.stderr)
    print('escrito %s: %d entradas (%d colisiones) en %.0fs' % (a.salida, len(res), len(colisiones), time.time() - t0), file=sys.stderr)


if __name__ == '__main__':
    main()
