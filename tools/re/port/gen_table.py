#!/usr/bin/env python3
# Arma src/exever_100.inc (tabla 1.07.02 -> 1.00 en RVA) desde map5.json y revisa que cada XA(0x...) de src/ este en la tabla.
#   python3 tools/re/port/gen_table.py            -> escribe la tabla y revisa
#   python3 tools/re/port/gen_table.py --check    -> solo revisa
import json, os, re, sys
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.normpath(os.path.join(here, '..', '..', '..'))
BASE = 0x140000000
EXTRA = {0x1416bdcb0: 0x1416b8160}   # lector de GrPluginPlanarShadowCombiner.ShadowQuality (0x1416bdcc0 - 0x10 en el codigo viejo)
m = json.load(open(os.path.join(here, 'map5.json')))
tab = dict(EXTRA)
for k, v in m.items():
    if v[0]: tab[int(k, 16)] = int(v[0], 16)
inc = os.path.join(root, 'src', 'exever_100.inc')
if '--check' not in sys.argv:
    with open(inc, 'w', newline='\n') as f:
        f.write('// Generado por tools/re/port/gen_table.py desde map5.json: RVA en PES2021.exe 1.07.02 (62787C6A) -> RVA en 1.00 (5F5ED4BF).\n// Ordenado por la primera columna (busqueda binaria). No editar a mano.\n')
        for a in sorted(tab): f.write('{ 0x%07x, 0x%07x },\n' % (a - BASE, tab[a] - BASE))
bad = 0; n = 0
for fn in sorted(os.listdir(os.path.join(root, 'src'))):
    if not fn.endswith(('.cpp', '.h')): continue
    for i, line in enumerate(open(os.path.join(root, 'src', fn), encoding='utf-8', errors='replace'), 1):
        for x in re.findall(r'XA\((0x[0-9a-fA-F]+)(?:ull)?\)', line):
            n += 1
            if int(x, 16) not in tab: bad += 1; print('FALTA en la tabla: %s:%d %s' % (fn, i, x))
print('tabla: %d direcciones; %d usos de XA() en src/, %d sin entrada' % (len(tab), n, bad))
sys.exit(1 if bad else 0)
