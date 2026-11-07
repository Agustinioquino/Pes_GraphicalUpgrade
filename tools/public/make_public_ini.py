"""Arma el PES21FX.ini y el PES21FX_rules.ini de la version publica limitada (package_public/PES21FX/).

    python tools/public/make_public_ini.py

Parte de package/PES21FX: apaga diagnosticos, fisica de ropa y lo reservado, saca del ini las tarjetas que la DLL publica
no muestra (src/public.cpp, kHiddenGroups) y quita del rules.ini las texturas de la boca (la version publica no toca la boca).
"""
import os, re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'package', 'PES21FX')
OUT = os.path.join(ROOT, 'package_public', 'PES21FX')

OPTS = {   # [PES21FX] / [Motor]
    'auto_captura': '0', 'auto_captura_ab': '0', 'exe_dump': '0', 'memoria_vigilar': '0', 'medidor': '0',
    'luz_rastrear': '0', 'sol_vigilar': '0', 'volcar_debugp': '0', 'sol_parche': '1', 'sombra_parche': '0',
}
PARAMS = {   # filas de parametros (mismos valores que fija public.cpp + preferencias del paquete)
    'w20': '0',                                   # Definicion de sombras apagada (sombras de postes y jugadores, 05/11)
    'x153': '0', 'x315': '0', 'x335': '0', 'x357': '0', 'x364': '0', 'x371': '0', 'x368': '0',
    'x344': '0', 'x346': '0', 'y343': '0', 'w349': '0', 'x383': '0', 'w383': '0',
    'w70': '0', 'x78': '0', 'x79': '0', 'y80': '0', 'z255': '0', 'x256': '0', 'w352': '0', 'w353': '0',
    'x377': '0', 'w377': '0', 'y378': '0', 'w379': '0', 'x380': '0', 'w380': '0', 'x381': '0', 'y381': '0',
    'x384': '1', 'y384': '0', 'z384': '1',
    # (07/11 Agustin) el paquete arranca sin animaciones de la cara ni pasto en toda la cancha (el usuario los prende en el menu)
    'w58': '0',                                   # Pasto en toda la cancha
    'x340': '0', 'x342': '0', 'y342': '0', 'w342': '0',   # expresiones mas marcadas, lengua, parpadeo
    'y347': '0', 'z347': '0', 'w347': '0',        # nariz con la boca, labio de arriba, mirada viva
    'x348': '0', 'y348': '0', 'z348': '0', 'w348': '0',   # musculos de la cara
    'x350': '0',                                  # abrir la boca: por shader (sin girar huesos)
    'y263': '0',                                  # respiracion
    'w24': '0', 'w69': '0', 'x80': '0',           # viento del pelo y arrugas desde la fisica de la ropa (cloth-mod): fuera
    'z352': '0',                                  # cuello: el paquete lleva TorsoBody_cuello 1.16 (corta la cara); el hundido z352 es para 1.19
}
HIDDEN = {'GITemporal', 'Volumetric', 'FaceTess', 'ExprShow', 'ExprGani', 'CamaraLibre', 'CamaraLenta', 'TorsoOriginal',
          'PerfMeter', 'DebugView', 'DiagHooks', 'HairWind', 'KitWrinkles'}


def make_ini():
    raw = open(os.path.join(SRC, 'PES21FX.ini'), 'rb').read().decode('cp1252')
    nl = '\r\n' if '\r\n' in raw else '\n'
    out, skip, sec, seen = [], False, '', set()
    for line in raw.split(nl):
        m = re.match(r'\s*\[([^\]]+)\]', line)
        if m:
            sec = m.group(1)
            name = sec[4:] if sec.lower().startswith('menu') else None
            skip = False
            if name is not None:
                body = raw[raw.index(line):].split(nl + '[', 1)[0]
                s = re.search(r'^section\s*=\s*(.*)$', body, re.M)
                section = s.group(1).strip() if s else ''
                if name in HIDDEN or section.startswith('Ropa') or (section.startswith('Diagn') and name != 'ShadowPatch'):
                    skip = True
        if skip:
            continue
        kv = re.match(r'(\s*)([A-Za-z_][A-Za-z0-9_]*)(\s*=\s*)(.*)$', line)
        if kv and sec.lower() in ('pes21fx', 'motor') and kv.group(2) in OPTS:
            line = kv.group(1) + kv.group(2) + kv.group(3) + OPTS[kv.group(2)]
            seen.add(kv.group(2))
        elif kv and sec == 'Constants' and kv.group(2) in PARAMS:
            line = kv.group(1) + kv.group(2) + kv.group(3) + PARAMS[kv.group(2)]
            seen.add(kv.group(2))
        out.append(line)
    txt = nl.join(out)
    hdr = ('; PES21FX - VERSION PUBLICA LIMITADA. Trae algunas funciones de las versiones nuevas, no todas; el resto se ira' + nl +
           '; liberando poco a poco. Las opciones reservadas estan fijas en la DLL (cambiarlas aca no las prende).' + nl)
    os.makedirs(OUT, exist_ok=True)
    for fn in ('PES21FX.ini', 'PES21FX.ini.por_defecto'):
        open(os.path.join(OUT, fn), 'wb').write((hdr + txt).encode('cp1252'))
    missing = sorted((set(OPTS) | set(PARAMS)) - seen)
    return missing


def make_rules():
    raw = open(os.path.join(SRC, 'PES21FX_rules.ini'), 'rb').read().decode('cp1252')
    nl = '\r\n' if '\r\n' in raw else '\n'
    out, sec = [], ''
    for line in raw.split(nl):
        m = re.match(r'\s*\[([^\]]+)\]', line)
        if m:
            sec = m.group(1).strip().lower()
        if sec == 'rule 63fab7735eee149c' and ('oral' in line.lower()):
            continue   # boca del juego sin tocar
        out.append(line)
    open(os.path.join(OUT, 'PES21FX_rules.ini'), 'wb').write(nl.join(out).encode('cp1252'))


if __name__ == '__main__':
    miss = make_ini()
    make_rules()
    print('listo;', 'claves que no estaban en el ini:', miss if miss else 'ninguna')
