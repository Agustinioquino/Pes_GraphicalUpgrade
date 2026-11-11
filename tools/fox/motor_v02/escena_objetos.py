#!/usr/bin/env python3
"""Motor en tiempo real (10/11/2026): agrega OBJETOS a una escena de estudio del Editar (Default.fpkd / EditFace.fpkd) para que
los cargue y los dibuje el propio Fox Engine, con sus materiales, sin dibujador propio.

Como: una entidad cobra::rd::StadiumModel por objeto (la misma que usa el juego para cada pieza de un estadio: camarografos,
carteles, tribunas). Su cuerpo (StadiumModelBody::vf15 0x1419064f0) sincroniza (FUN_141906600) y mete el modelo en la escena
de la entidad (FUN_141692630 + FUN_1416927c0): la MISMA via por la que los focos SpotLight entran a la escena del estudio.
Propiedades y valores copiados de StadiumModel reales (st024: cam00 y pitch; version 3, 0x190 bytes). kind / direction =
DEFAULT y demoGroup = NONE como el modelo del pasto.

CONFIRMADO en el juego (prueba 1, 10/11 22:06 UTC): la escena Default del Editar acepta la clase, el motor dibuja el objeto con
su textura real y el .fmdl guardado en el .fpk hermano con ruta propia carga (las texturas salen por la ruta que trae el fmdl).

AYUDAS (prueba 2): una grilla de orientacion (zfx_Grilla_NN) y un marcador por cada foco de la escena (zfx_Luz_NN), hechos con
el mismo modelo estirado o achicado. Van con flags = 6 (entidad no habilitada): FUN_141906600 crea el modelo igual, vf15 lo
mete en la escena igual, y lo deja oculto (modelo +0x1ac = 0xffffffff, +0x1b4 bit0). El modulo de Sider los muestra y los
esconde. INFERIDO (leido en el C, falta verlo en el juego): que una StadiumModel con flags 6 llegue a la lista de la escena
(los 18 SpotLight apagados de esta misma escena van con flags 6 y el juego los crea).

uso:
  python escena_objetos.py <X.fpkd entrada> <X.fpkd salida> --fpk-entrada X.fpk --fpk-salida X.fpk \\
      --objeto "/Assets/pes16/model/.../scenes/cosa.fmdl=C:\\ruta\\cosa.fmdl@x,y,z" [--objeto ...] \\
      [--ayudas "/Assets/pes16/model/.../scenes/cosa.fmdl=C:\\ruta\\cosa.fmdl"]
  @x,y,z en metros (opcional; por defecto 0,0,0); despues se puede agregar *escala (un numero) y #nombre.
  La boca no se toca: no hay mouth.fmdl ni pes_oral_* en estas escenas, y el programa se niega si aparecen.
"""
import argparse, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fox2_edit as F
import fpk_edit as P
from editface_sol import base_props

PREFIJO, PREFIJO_GRILLA, PREFIJO_LUZ = 'zfx_Obj_', 'zfx_Grilla_', 'zfx_Luz_'
SEPARACION_MARCADOR = 0.18        # metros entre el foco y su marcador (hacia afuera): igual que MotorTiempoReal.lua


def add_transform(f, owner, addr, pos, escala=(1, 1, 1)):
    f.add_string('TransformEntity')
    e = F.Entity(F.strcode('TransformEntity'), 0, 0x60, addr, [])
    f.set(e, 'owner', 'EntityHandle', owner)
    f.set(e, 'transform_scale', 'Vector3', (escala[0], escala[1], escala[2], 0))
    f.set(e, 'transform_rotation_quat', 'Quat', (0, 0, 0, 1))
    f.set(e, 'transform_translation', 'Vector3', (pos[0], pos[1], pos[2], 0))
    return e


def add_objeto(f, nombre, ruta, pos, escala=(1, 1, 1), visible=True):
    if f.find(name=nombre): raise SystemExit('ya existe %s: usar la escena sin objetos como entrada' % nombre)
    addr = f.free_address(); taddr = addr + 0x100
    f.add_string('StadiumModel')
    e = F.Entity(F.strcode('StadiumModel'), 3, 0x190, addr, [])
    base_props(f, e, nombre, taddr)
    if not visible: f.set(e, 'flags', 'UInt32', 6)   # el motor lo carga y lo deja oculto (ver arriba)
    f.set(e, 'modelFile', 'FilePtr', ruta)
    f.set(e, 'isIsolated', 'Bool', False)
    f.set(e, 'isDynamic', 'Bool', True)
    f.set(e, 'lodFarSize', 'Float', -1.0)
    f.set(e, 'lodNearSize', 'Float', -1.0)
    f.set(e, 'lodPolygonSize', 'Float', 2.0)
    f.set(e, 'color', 'Color', (1, 1, 1, 1))
    f.set(e, 'drawRejectionLevel', 'Int32', 7)       # NO_REJECT: un objeto chico no se descarta por tamaño
    f.set(e, 'drawMode', 'Int32', 0)
    f.set(e, 'rejectFarRangeShadowCast', 'Int32', 2)
    f.set(e, 'direction', 'Int32', 5)                # DEFAULT
    f.set(e, 'kind', 'Int32', 17)                    # DEFAULT (no STAFF_* / STAND_*: esos los prende y apaga el partido)
    f.set(e, 'demoGroup', 'Int32', 0)
    f.set(e, 'customBits', 'UInt32', 0)
    f.ents.append(e)
    f.ents.append(add_transform(f, addr, taddr, pos, escala))
    f.add_to_dataset(nombre, addr)
    return e


def medir(archivo):
    """caja del modelo en metros (minimo, maximo), leida de los vertices del .fmdl"""
    import numpy as np
    import fmdl
    m = fmdl.meshes(fmdl.Fmdl(archivo))
    pos = [x['pos'] for x in m if x['pos'] is not None and len(x['pos'])]
    if not pos: raise SystemExit('no pude medir %s (sin posiciones legibles): pasar --caja' % archivo)
    p = np.concatenate(pos)
    return [float(v) for v in p.min(axis=0)], [float(v) for v in p.max(axis=0)]


def focos(f):
    """(nombre, posicion) de cada foco de la escena, en el orden del archivo"""
    out = []
    for e in f.ents:
        if f.string(e.cls) not in ('SpotLight', 'PointLight'): continue
        p = e.prop('transform')
        t = f.by_address(p.value()) if p is not None and p.value() else None
        tr = t.prop('transform_translation') if t is not None else None
        if tr is None: continue
        v = tr.value()
        out.append((f.name_of(e) or '', (v[0], v[1], v[2])))
    return out


def pieza(caja, centro, tam):
    """escala y posicion de la entidad para que el modelo ocupe 'tam' metros alrededor de 'centro' (sin girar)"""
    mn, mx = caja
    esc, pos = [], []
    for i in range(3):
        largo = mx[i] - mn[i]
        if not largo > 1e-6: raise SystemExit('el modelo de las ayudas es plano en un eje: usar otro')
        s = tam[i] / largo
        esc.append(s); pos.append(centro[i] - s * (mx[i] + mn[i]) / 2)
    return esc, pos


def add_ayudas(f, ruta, caja, medio, paso, grosor, marcador):
    """grilla en el piso (lineas cada 'paso' metros hasta 'medio' a cada lado; las del centro, el doble de gruesas) y un
    marcador por foco, todos ocultos"""
    n = 0
    k = int(round(medio / paso))
    largo = 2 * k * paso
    for eje in (0, 2):                                       # lineas a lo largo de X (eje 0) y a lo largo de Z (eje 2)
        for i in range(-k, k + 1):
            g = grosor * (2 if i == 0 else 1)
            tam = [g, g, g]; tam[eje] = largo
            centro = [0.0, g / 2, 0.0]; centro[2 - eje] = i * paso
            esc, pos = pieza(caja, centro, tam)
            n += 1
            add_objeto(f, '%s%02d' % (PREFIJO_GRILLA, n), ruta, pos, esc, visible=False)
    lineas = n
    fs = focos(f)
    for j, (nombre, p) in enumerate(fs, 1):
        d = [p[0], p[1] - 1.0, p[2]]                         # hacia afuera del centro de la escena (0, 1, 0)
        l = math.sqrt(sum(x * x for x in d))
        u = [x / l for x in d] if l > 1e-9 else [0.0, 1.0, 0.0]
        centro = [p[i] + u[i] * SEPARACION_MARCADOR for i in range(3)]
        esc, pos = pieza(caja, centro, [marcador] * 3)
        add_objeto(f, '%s%02d' % (PREFIJO_LUZ, j), ruta, pos, esc, visible=False)
    return lineas, len(fs)


def partir(o):
    """RUTA=ARCHIVO[@x,y,z][*escala][#nombre]"""
    ruta, _, resto = o.partition('=')
    resto, _, nombre = resto.partition('#')
    resto, _, esc = resto.partition('*')
    archivo, _, p = resto.partition('@')
    pos = [float(x) for x in p.split(',')] if p else [0.0, 0.0, 0.0]
    escala = float(esc) if esc else 1.0
    if not ruta.startswith('/Assets/') or not ruta.lower().endswith('.fmdl') or len(pos) != 3 or not escala > 0:
        raise SystemExit('objeto mal escrito: ' + o)
    low = (ruta + ' ' + archivo).lower()
    if 'mouth' in low or 'oral' in low: raise SystemExit('la boca no se toca: ' + o)
    blob = open(archivo, 'rb').read()
    if blob[:4] != b'FMDL': raise SystemExit('no es un fmdl: ' + archivo)
    return ruta, archivo, blob, pos, escala, nombre


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('entrada'); ap.add_argument('salida')
    ap.add_argument('--fpk-entrada', required=True); ap.add_argument('--fpk-salida', required=True)
    ap.add_argument('--objeto', action='append', default=[])
    ap.add_argument('--ayudas', help='RUTA=ARCHIVO del modelo con el que se hacen la grilla y los marcadores de luz')
    ap.add_argument('--caja', help='caja del modelo de las ayudas "x0,y0,z0,x1,y1,z1" en metros (por defecto se mide del fmdl)')
    ap.add_argument('--grilla-medio', type=float, default=5.0, help='metros de grilla a cada lado del centro')
    ap.add_argument('--grilla-paso', type=float, default=1.0)
    ap.add_argument('--grilla-grosor', type=float, default=0.004)
    ap.add_argument('--marcador', type=float, default=0.09, help='tamaño del marcador de cada foco, en metros')
    a = ap.parse_args(argv)
    if not a.objeto and not a.ayudas: raise SystemExit('nada que agregar: falta --objeto o --ayudas')
    pk = P.read(open(a.entrada, 'rb').read())
    fox_path = next(e[0] for e in pk['ents'] if e[0].endswith('.fox2'))
    fox = [e for e in pk['ents'] if e[0] == fox_path][0][1]
    f = F.Fox2.parse(fox)
    assert f.build() == fox, 'el fox2 de entrada no se rearma identico: no se escribe nada'
    for e in f.ents:
        if (f.name_of(e) or '').startswith('zfx_'): raise SystemExit('la escena de entrada ya trae %s: usar la base sin agregados' % f.name_of(e))
    n0 = len(f.ents)
    ef = P.read(open(a.fpk_entrada, 'rb').read())
    for i, o in enumerate(a.objeto, 1):
        ruta, archivo, blob, pos, escala, nombre = partir(o)
        add_objeto(f, nombre or '%s%02d' % (PREFIJO, i), ruta, pos, (escala, escala, escala))
        P.put(ef, ruta, blob)
    if a.ayudas:
        ruta, archivo, blob, _, _, _ = partir(a.ayudas)
        if a.caja:
            v = [float(x) for x in a.caja.split(',')]
            if len(v) != 6: raise SystemExit('--caja lleva 6 numeros')
            caja = (v[:3], v[3:])
        else:
            caja = medir(archivo)
        print('modelo de las ayudas: caja %s .. %s m' % tuple(' '.join('%.4f' % x for x in c) for c in caja))
        lineas, marcas = add_ayudas(f, ruta, caja, a.grilla_medio, a.grilla_paso, a.grilla_grosor, a.marcador)
        print('ayudas: %d lineas de grilla y %d marcadores de foco (ocultos: los muestra el modulo)' % (lineas, marcas))
        P.put(ef, ruta, blob)
    out = f.build()
    chk = F.Fox2.parse(out)
    assert chk.build() == out
    cuenta = {}
    for e in chk.ents[n0:]:
        c = chk.string(e.cls); cuenta[c] = cuenta.get(c, 0) + 1
        if c == 'StadiumModel':
            t = chk.by_address(e.prop('transform').value())
            print('agregada 0x%x %-14s flags %d pos %s escala %s' % (e.address, chk.name_of(e), e.prop('flags').value(),
                  ' '.join('%.3f' % x for x in t.prop('transform_translation').value()[:3]),
                  ' '.join('%.4f' % x for x in t.prop('transform_scale').value()[:3])))
    for e in chk.ents:
        low = (chk.name_of(e) or '').lower()
        assert 'mouth' not in low and 'oral' not in low, 'aparece algo de la boca: no se escribe'
    P.put(pk, fox_path, out)
    open(a.salida, 'wb').write(P.write(pk))
    open(a.fpk_salida, 'wb').write(P.write(ef))
    print('%s y %s escritos (%d entidades; agregadas %s)' % (a.salida, a.fpk_salida, len(chk.ents),
          ', '.join('%d %s' % (n, c) for c, n in sorted(cuenta.items()))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
