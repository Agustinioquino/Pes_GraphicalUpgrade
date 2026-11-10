#!/usr/bin/env python3
"""Motor en tiempo real (10/11/2026): agrega OBJETOS a una escena de estudio del Editar (Default.fpkd / EditFace.fpkd) para que
los cargue y los dibuje el propio Fox Engine, con sus materiales, sin dibujador propio.

Como: una entidad cobra::rd::StadiumModel por objeto (la misma que usa el juego para cada pieza de un estadio: camarografos,
carteles, tribunas). Su cuerpo (StadiumModelBody::vf7 0x1419054a0) hace FUN_141692630(escena de la entidad) +
FUN_1416927c0(escena, modelo): la MISMA via por la que los focos SpotLight entran a la escena del estudio. Propiedades y
valores copiados de una StadiumModel real (st024_2018_common_cam00.fox2, version 3, 0x190 bytes).

INFERIDO, falta probar en el juego: que la escena del Editar acepte la clase (la DirectionalLight en esta misma escena hizo
que no cargara, 10/11 02:00 UTC). Probar con UN objeto antes de seguir.

El .fmdl se guarda en el .fpk hermano con la ruta que nombra la entidad (asi lo hacen los fpk de estadio). Las texturas las
busca el motor por la ruta que trae el fmdl (cpk / livecpk), no se copian.

uso:
  python escena_objetos.py <X.fpkd entrada> <X.fpkd salida> --fpk-entrada X.fpk --fpk-salida X.fpk \\
      --objeto "/Assets/pes16/model/.../scenes/cosa.fmdl=C:\\ruta\\cosa.fmdl@x,y,z" [--objeto ...]
  (@x,y,z en metros, opcional; por defecto 0,0,0. La boca no se toca: no hay mouth.fmdl ni pes_oral_* en estas escenas.)
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fox2_edit as F
import fpk_edit as P
from editface_sol import base_props

PREFIJO = 'zfx_Obj_'


def add_transform(f, owner, addr, pos):
    f.add_string('TransformEntity')
    e = F.Entity(F.strcode('TransformEntity'), 0, 0x60, addr, [])
    f.set(e, 'owner', 'EntityHandle', owner)
    f.set(e, 'transform_scale', 'Vector3', (1, 1, 1, 0))
    f.set(e, 'transform_rotation_quat', 'Quat', (0, 0, 0, 1))
    f.set(e, 'transform_translation', 'Vector3', (pos[0], pos[1], pos[2], 0))
    return e


def add_objeto(f, n, ruta, pos):
    nombre = '%s%02d' % (PREFIJO, n)
    if f.find(name=nombre): raise SystemExit('ya existe %s: usar la escena sin objetos como entrada' % nombre)
    addr = f.free_address(); taddr = addr + 0x100
    f.add_string('StadiumModel')
    e = F.Entity(F.strcode('StadiumModel'), 3, 0x190, addr, [])
    base_props(f, e, nombre, taddr)
    f.set(e, 'modelFile', 'FilePtr', ruta)
    f.set(e, 'isIsolated', 'Bool', False)
    f.set(e, 'isDynamic', 'Bool', True)
    f.set(e, 'lodFarSize', 'Float', -1.0)
    f.set(e, 'lodNearSize', 'Float', -1.0)
    f.set(e, 'lodPolygonSize', 'Float', 2.0)
    f.set(e, 'color', 'Color', (1, 1, 1, 1))
    f.set(e, 'drawRejectionLevel', 'Int32', 8)
    f.set(e, 'drawMode', 'Int32', 0)
    f.set(e, 'rejectFarRangeShadowCast', 'Int32', 2)
    f.set(e, 'direction', 'Int32', 0)
    f.set(e, 'kind', 'Int32', 9)
    f.set(e, 'demoGroup', 'Int32', 0)
    f.set(e, 'customBits', 'UInt32', 0)
    f.ents.append(e)
    f.ents.append(add_transform(f, addr, taddr, pos))
    f.add_to_dataset(nombre, addr)
    return e


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('entrada'); ap.add_argument('salida')
    ap.add_argument('--fpk-entrada', required=True); ap.add_argument('--fpk-salida', required=True)
    ap.add_argument('--objeto', action='append', required=True)
    a = ap.parse_args(argv)
    pk = P.read(open(a.entrada, 'rb').read())
    fox_path = next(e[0] for e in pk['ents'] if e[0].endswith('.fox2'))
    fox = [e for e in pk['ents'] if e[0] == fox_path][0][1]
    f = F.Fox2.parse(fox)
    assert f.build() == fox, 'el fox2 de entrada no se rearma identico: no se escribe nada'
    n0 = len(f.ents)
    ef = P.read(open(a.fpk_entrada, 'rb').read())
    for i, o in enumerate(a.objeto, 1):
        ruta, _, resto = o.partition('=')
        archivo, _, p = resto.partition('@')
        pos = [float(x) for x in p.split(',')] if p else [0.0, 0.0, 0.0]
        if not ruta.startswith('/Assets/') or not ruta.lower().endswith('.fmdl') or len(pos) != 3:
            raise SystemExit('objeto mal escrito: ' + o)
        low = ruta.lower()
        if 'mouth' in low or 'oral' in low: raise SystemExit('la boca no se toca: ' + ruta)
        blob = open(archivo, 'rb').read()
        if blob[:4] != b'FMDL': raise SystemExit('no es un fmdl: ' + archivo)
        add_objeto(f, i, ruta, pos)
        P.put(ef, ruta, blob)
    out = f.build()
    chk = F.Fox2.parse(out)
    assert chk.build() == out
    for e in chk.ents[n0:]:
        print('agregada 0x%x %s %s' % (e.address, chk.string(e.cls), chk.name_of(e) or ''))
    P.put(pk, fox_path, out)
    open(a.salida, 'wb').write(P.write(pk))
    open(a.fpk_salida, 'wb').write(P.write(ef))
    print('%s y %s escritos (%d entidades)' % (a.salida, a.fpk_salida, len(chk.ents)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
