# (17/11 3icxbh) arma src/motorclips.inc: los clips de CUERPO de FixDemo (animaciones.json del catalogo de cinematicas) y los 166 gestos
# base de la cara (clips_base_indices.txt, en el orden de la lista fija del exe 0x143535520) para el Inspector de jugadores del Motor.
# Uso: python tools/motor/motorclips_gen.py <animaciones.json> <clips_base_indices.txt> src/motorclips.inc
import json, sys
anim, faces, out = sys.argv[1], sys.argv[2], sys.argv[3]
a = json.load(open(anim, encoding='utf-8'))
pre = 'cpk_dat/common/demo/anime/FoxAnim/FixDemo/Animations/'
c = [x for x in a['cinematicas'] if x['tipo'] == 'Cuerpo' and x['ruta'].startswith(pre)]
groups = []
for x in c:
    g = x['grupo'].replace('.gani', '')
    if g not in groups: groups.append(g)
c.sort(key=lambda x: (groups.index(x['grupo'].replace('.gani', '')), x['nombre']))
f = [l.split('\t') for l in open(faces, encoding='utf-8').read().splitlines() if l.strip()]
assert [int(i) for i, _ in f] == list(range(len(f)))
with open(out, 'w', encoding='utf-8', newline='\n') as o:
    o.write('// generado por tools/motor/motorclips_gen.py (no editar a mano)\n')
    o.write('static const char* const kClipGroupKey[] = {\n' + ''.join('    "%s",\n' % g for g in groups) + '};\n')
    o.write('struct ClipRow { const char* file; unsigned char group; };\n')
    o.write('static const ClipRow kClipRows[] = {\n')
    for x in c: o.write('    { "%s", %d },\n' % (x['nombre'], groups.index(x['grupo'].replace('.gani', ''))))
    o.write('};\n')
    o.write('static const char* const kFaceBase[] = {\n' + ''.join('    "%s",\n' % n.strip() for _, n in f) + '};\n')
print(len(c), 'clips,', len(groups), 'grupos,', len(f), 'gestos')
