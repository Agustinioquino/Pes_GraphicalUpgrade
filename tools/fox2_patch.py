#!/usr/bin/env python3
"""Parcha floats/enteros/bools de un .fox2 dentro de un .fpkd en el lugar (no cambia tamanos), para servir por livecpk.
Usa el .json de fox2_dump.py para ubicar cada propiedad: dato en (inicio del fox2 en el fpkd) + offset + 0x20.
Verifica el valor original antes de escribir: si no coincide no escribe nada.
uso: python fox2_patch.py <orig.fpkd> <dump.json> <salida.fpkd> Entidad.propiedad=valor [...]
     (Entidad = nombre de la entidad, p. ej. TimeOfDaySettings0.sunLux=80000)
"""
import json, struct, sys

FMT = {'Float': '<f', 'UInt32': '<I', 'Int32': '<i', 'Bool': '<?', 'UInt8': '<B', 'UInt16': '<H'}

def fox2_start(d):
    # entrada 0 de la tabla de archivos del fpk: offset u32 en 0x30+0x0? se busca la firma del fox2
    i = d.find(b'\xf2box')
    if i < 0: raise SystemExit('no hay fox2 en el fpkd')
    return i

def main():
    if len(sys.argv) < 5: print(__doc__); return 1
    src, js, dst, *mods = sys.argv[1:]
    d = bytearray(open(src, 'rb').read()); base = fox2_start(d)
    j = json.load(open(js))
    idx = {}
    for e in j['entities']:
        props = {p['name']: p for p in e['static_properties']}
        nm = props.get('name', {}).get('value')
        if nm: idx[nm] = props
    plan = []
    for m in mods:
        k, v = m.split('='); ent, prop = k.rsplit('.', 1)
        p = idx[ent][prop]; f = FMT[p['type']]
        o = base + p['offset'] + 0x20
        cur = struct.unpack_from(f, d, o)[0]
        if (abs(cur - p['value']) > 1e-4 * max(1, abs(p['value']))) if p['type'] == 'Float' else cur != p['value']:
            raise SystemExit('%s: en el archivo %r, el json dice %r: no coincide, no escribo nada' % (k, cur, p['value']))
        nv = float(v) if p['type'] == 'Float' else (v.lower() in ('1', 'true') if p['type'] == 'Bool' else int(v))
        plan.append((o, f, k, cur, nv))
    for o, f, k, cur, nv in plan:
        struct.pack_into(f, d, o, nv); print('%-45s %s -> %s' % (k, cur, nv))
    open(dst, 'wb').write(d); print('ok', dst, len(d), 'B')
    return 0

if __name__ == '__main__':
    sys.exit(main())
