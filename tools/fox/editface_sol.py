#!/usr/bin/env python3
"""Sol REAL del motor en el editor de caras: agrega una luz direccional de Fox (la misma ruta que el sol del partido:
PluginSunlight + PluginShadow con cascadas) a la escena de estudio EditFace.fox2, para servirla por el livecpk LucesEditor.

(09/11, Agustin: "el timelapse tiene que funcionar con un sol real luz de noche real, y el sol cercano tiene que ser un sol
real del juego asi como pasa a la hora de jugar")

Por que funciona (C decompilado, /mnt/project-files/luz_editor/sol_real_editor.md):
  - PluginSunlight::vf8 0x1416ee820 (pasada LIGHTING_SUN) y PluginShadow::vf7 0x1416c66e0 (elige la luz que proyecta) no miran
    el modo de juego: recorren las luces del contexto del viewport (FUN_1416883f0(vp)+0x98) y usan las DirectionalLightSource.
  - En el editor no hay ninguna: EditFace.fox2 solo trae SpotLight. En el partido la crea TppAtmosphere (FUN_141943ce0) desde el
    fox2 de luz del estadio.
  - La entidad fox::grx::DirectionalLight (clase registrada en 0x141816020, version 7, 0x1b0 bytes) crea su
    DirectionalLightSource (FUN_141815620 -> FUN_14169ee20) y la mete en la MISMA escena que los focos (FUN_1416927c0), igual
    que SpotLight (FUN_141815ae0). Sus propiedades bajan a la fuente en FUN_141812350.

Modos:
  direccional (por defecto): agrega DirectionalLight "zfx_SolEditor" + GrShadowSettings (valores del estadio). No hace falta
      ningun archivo mas. La DLL mueve el sol/luna por el timelapse escribiendo la fuente (ver el .md, seccion 'enganche').
  atmosfera: ademas copia TppAtmosphere + CapturePosition_Sky del fox2 de luz de un estadio (--donante), con sus .atsh/.pcsp
      (--donante-fpk, se copian con nombre propio zfx_* dentro de EditFace.fpk) y prende overrideDateTime: el motor calcula
      sol, luna, luz de cielo y la hora como en el partido. Sin la DirectionalLight propia (la atmosfera crea la suya).

La boca no se toca: este archivo solo tiene luces de estudio (no hay mouth.fmdl, ni shaders, ni pes_oral_*).
Nada se sobrescribe en el juego: la salida va al livecpk\\LucesEditor de Sider como hasta ahora (backup antes, juego cerrado).

uso:
  python editface_sol.py <EditFace.fpkd entrada> <EditFace.fpkd salida> [opciones]
     --lux 1500            iluminancia del sol en el modo direccional (exposicion fija del editor -7.1 EV; ver el .md)
     --elev 35 --azim 135  altura y rumbo del sol en grados (rumbo 0 = +Z, 90 = +X)
     --temp 5600           temperatura de color (K)
     --rango 12            shadowRange en metros (el estadio usa 240: en el editor conviene chico = sombra mas fina)
     --sin-luz             crea la luz apagada (enable = false): la DLL la prende
     --modo atmosfera --donante light_stXXX_af.fpkd --donante-fpk light_stXXX_af.fpk
                           --fpk-entrada EditFace.fpk --fpk-salida EditFace.fpk --hora 16 --minuto 0
     --exposicion partido  (solo con atmosfera) exposicion automatica -14..-7 EV como el estadio, para lux reales
"""
import argparse, math, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fox2_edit as F
import fpk_edit as P

FOX2_PATH = '/Assets/pes16/model/light/EditFace.fox2'
NOMBRE_SOL = 'zfx_SolEditor'
ATSH = '/Assets/pes16/model/light/zfx_EditFace_sol.atsh'
PCSP = '/Assets/pes16/model/light/zfx_EditFace_sol.pcsp'


def sun_dir(elev, azim):
    e, a = math.radians(elev), math.radians(azim)
    # direccion HACIA el sol (misma convencion que TppAtmosphere.fixedRisingSunDir: y > 0 = sol arriba del horizonte)
    return (math.cos(e) * math.sin(a), math.sin(e), math.cos(e) * math.cos(a), 0.0)


def add_transform(f, owner_addr, addr):
    ps = []
    f.add_string('TransformEntity')
    e = F.Entity(F.strcode('TransformEntity'), 0, 0x60, addr, ps)
    f.set(e, 'owner', 'EntityHandle', owner_addr)
    f.set(e, 'transform_scale', 'Vector3', (1, 1, 1, 0))
    f.set(e, 'transform_rotation_quat', 'Quat', (0, 0, 0, 1))
    f.set(e, 'transform_translation', 'Vector3', (0, 0, 0, 0))
    return e


def base_props(f, e, name, transform_addr):
    ds = f.dataset().address
    f.set(e, 'name', 'String', name)
    f.set(e, 'dataSet', 'EntityHandle', ds)
    f.set(e, 'parent', 'EntityHandle', 0)
    f.set(e, 'transform', 'EntityPtr', transform_addr)
    f.set(e, 'shearTransform', 'EntityPtr', 0)
    f.set(e, 'pivotTransform', 'EntityPtr', 0)
    p = f.set(e, 'children', 'EntityHandle', 0, F.LIST)
    p.set_elems([])
    f.set(e, 'flags', 'UInt32', 7)


def add_sun(f, a):
    if f.find(name=NOMBRE_SOL):
        raise SystemExit('ya tiene %s: usar el EditFace.fpkd sin el sol como entrada' % NOMBRE_SOL)
    addr = f.free_address(); taddr = addr + 0x100
    f.add_string('DirectionalLight')
    e = F.Entity(F.strcode('DirectionalLight'), 7, 0x1b0, addr, [])
    base_props(f, e, NOMBRE_SOL, taddr)
    # orden = el del registro de la clase (0x141816020); offsets del objeto en reflexion.tsv
    f.set(e, 'enable', 'Bool', not a.sin_luz)                      # +0x130
    f.set(e, 'direction', 'Vector3', sun_dir(a.elev, a.azim))     # +0x140
    f.set(e, 'shadowRange', 'Float', a.rango)                     # +0x150
    f.set(e, 'shadowRangeExtra', 'Float', a.rango / 3)            # +0x154
    f.set(e, 'hiResShadowRange', 'Float', min(3.0, a.rango / 4))  # +0x158 (cascada fina: la cara)
    f.set(e, 'shadowProjectionRange', 'Float', 10.0)              # +0x15c
    f.set(e, 'shadowFadeRange', 'Float', 1.0)                     # +0x160
    f.set(e, 'selfShadowBias', 'Float', -15.0)                    # +0x164 (x0.001 al bajar a la fuente; el del estadio)
    f.set(e, 'isCascadeBlend', 'Bool', True)                      # +0x168
    f.set(e, 'color', 'Color', (1, 1, 1, 1))                      # +0x170
    f.set(e, 'temperature', 'Float', a.temp)                      # +0x180
    f.set(e, 'colorDeflection', 'Float', 0.0)                     # +0x184
    f.set(e, 'lux', 'Float', a.lux)                               # +0x188
    f.set(e, 'lightSize', 'Float', 0.53)                          # +0x18c grados (x pi/180): disco del sol
    f.set(e, 'castShadow', 'Bool', True)                          # +0x190
    f.set(e, 'isBounced', 'Bool', False)                          # +0x191
    f.set(e, 'showObject', 'Bool', False)                         # +0x192
    f.set(e, 'shadowMaskSpecular', 'Float', 0.0)                  # +0x194
    f.set(e, 'shadowOffsetStartAngle', 'Float', 0.0)              # +0x198
    f.set(e, 'shadowOffsetEndAngle', 'Float', 0.0)                # +0x19c
    f.set(e, 'enableDistanceFade', 'Bool', False)                 # +0x1a0
    f.set(e, 'diatanceFade_StartDistance', 'Float', 0.0)          # +0x1a4 (sic, asi se llama en el exe)
    f.set(e, 'distanceFade_FadeoutRange', 'Float', 0.0)           # +0x1a8
    f.ents.append(e)
    f.ents.append(add_transform(f, addr, taddr))
    f.add_to_dataset(NOMBRE_SOL, addr)
    return e


def add_shadow_settings(f):
    if f.find(cls='GrShadowSettings'): return None
    addr = f.free_address()
    f.add_string('GrShadowSettings')
    e = F.Entity(F.strcode('GrShadowSettings'), 0, 0x98, addr, [])
    f.set(e, 'name', 'String', 'GrShadowSettings')
    f.set(e, 'dataSet', 'EntityHandle', f.dataset().address)
    for n, t, v in (('method', 'Int32', 1), ('shadowOpacity', 'Float', 1.0), ('shadowContrast', 'Float', 0.0),
                    ('shadowBlurRadius', 'Float', 2.5), ('maskedShadowOpacity', 'Float', 0.85),
                    ('maskedShadowContrast', 'Float', 0.0), ('maskedShadowBlurRadius', 'Float', 1.0),
                    ('depthBias', 'Float', 0.0001), ('remappingThreshold', 'Float', 0.5),
                    ('temporalSampleCount', 'UInt32', 4)):        # = GrShadowSettings de light_st024_af / st072_af
        f.set(e, n, t, v)
    f.ents.append(e)
    f.add_to_dataset('GrShadowSettings', addr)
    return e


def add_atmosphere(f, a):
    dd = open(a.donante, 'rb').read()
    df = P.read(dd) if dd[:6] == b'foxfpk' else None
    src = F.Fox2.parse([e for e in df['ents'] if e[0].endswith('.fox2')][0][1] if df else dd)
    atm = src.find(cls='TppAtmosphere')
    if not atm: raise SystemExit('el donante no tiene TppAtmosphere')
    atm = atm[0]
    cap = src.by_address(atm.prop('capturePosition').value())
    ents = [atm]
    if cap is not None:
        ents.append(cap)
        tr = src.by_address(cap.prop('transform').value())
        if tr is not None: ents.append(tr)
    old_atsh = src.string(atm.prop('atshFilePtr').value()); old_pcsp = src.string(atm.prop('pcspFilePtr').value())
    new = f.copy_from(src, ents)
    na = new[0]
    f.set(na, 'atshFilePath', 'Path', ATSH); f.set(na, 'atshFilePtr', 'FilePtr', ATSH)
    f.set(na, 'pcspFilePath', 'Path', PCSP); f.set(na, 'pcspFilePtr', 'FilePtr', PCSP)
    f.add_to_dataset('TppAtmosphere', na.address)
    if cap is not None:
        f.add_to_dataset(src.name_of(cap), new[1].address)
    tods = f.find(cls='TimeOfDaySettings')[0]
    f.set(tods, 'overrideDateTime', 'Bool', True)
    f.set(tods, 'dateTimeHour', 'UInt32', a.hora)
    f.set(tods, 'dateTimeMinute', 'UInt32', a.minuto)
    if a.exposicion == 'partido':
        for pre in ('game', 'demo'):
            f.set(tods, pre + 'ExposureCompensation', 'Float', 0.0)
            f.set(tods, pre + 'MinExposure', 'Float', -14.0)
            f.set(tods, pre + 'MaxExposure', 'Float', -7.0)
            f.set(tods, pre + 'InitialExposureValue', 'Float', -11.0)
    return old_atsh, old_pcsp


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('entrada'); ap.add_argument('salida')
    ap.add_argument('--modo', choices=('direccional', 'atmosfera'), default='direccional')
    ap.add_argument('--lux', type=float, default=1500.0)
    ap.add_argument('--elev', type=float, default=35.0)
    ap.add_argument('--azim', type=float, default=135.0)
    ap.add_argument('--temp', type=float, default=5600.0)
    ap.add_argument('--rango', type=float, default=12.0)
    ap.add_argument('--sin-luz', action='store_true')
    ap.add_argument('--donante'); ap.add_argument('--donante-fpk')
    ap.add_argument('--fpk-entrada'); ap.add_argument('--fpk-salida')
    ap.add_argument('--hora', type=int, default=16); ap.add_argument('--minuto', type=int, default=0)
    ap.add_argument('--exposicion', choices=('editor', 'partido'), default='editor')
    a = ap.parse_args(argv)

    d = open(a.entrada, 'rb').read()
    pk = P.read(d) if d[:6] == b'foxfpk' else None
    fox = [e for e in pk['ents'] if e[0] == FOX2_PATH][0][1] if pk else d
    f = F.Fox2.parse(fox)
    assert f.build() == fox, 'el fox2 de entrada no se rearma identico: no se escribe nada'
    if not f.find(name='SL_Key0000_ver03'):
        raise SystemExit('no parece EditFace.fox2 (falta SL_Key0000_ver03)')
    n0 = len(f.ents)

    add_shadow_settings(f)
    if a.modo == 'direccional':
        add_sun(f, a)
    else:
        if a.exposicion == 'editor': print('aviso: con la atmosfera y la exposicion fija del editor el sol real (150000 lux) '
                                           'satura; usar --exposicion partido')
        if not (a.donante and a.donante_fpk and a.fpk_entrada and a.fpk_salida):
            raise SystemExit('--modo atmosfera necesita --donante, --donante-fpk, --fpk-entrada y --fpk-salida')
        old_atsh, old_pcsp = add_atmosphere(f, a)
        don = P.read(open(a.donante_fpk, 'rb').read())
        blobs = {e[0]: e[1] for e in don['ents']}
        if old_atsh not in blobs or old_pcsp not in blobs:
            raise SystemExit('el .fpk donante no trae %s / %s' % (old_atsh, old_pcsp))
        ef = P.read(open(a.fpk_entrada, 'rb').read())
        P.put(ef, ATSH, blobs[old_atsh]); P.put(ef, PCSP, blobs[old_pcsp])
        open(a.fpk_salida, 'wb').write(P.write(ef))
        print('%s: + %s, %s' % (a.fpk_salida, ATSH, PCSP))

    out = f.build()
    chk = F.Fox2.parse(out)                       # se vuelve a leer: tiene que rearmarse identico
    assert chk.build() == out
    for e in chk.ents[n0:]:
        print('agregada 0x%x %s %s' % (e.address, chk.string(e.cls), chk.name_of(e) or ''))
    if pk:
        P.put(pk, FOX2_PATH, out)
        out = P.write(pk)
    open(a.salida, 'wb').write(out)
    print('%s: %d bytes (%d entidades)' % (a.salida, len(out), len(chk.ents)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
