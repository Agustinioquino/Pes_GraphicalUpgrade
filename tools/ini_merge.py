#!/usr/bin/env python3
"""Suma a un PES21FX.ini (o a un PES21FX_menu_*.ini) de la PC las lineas NUEVAS del ini del repo,
sin tocar los valores que ya tiene el de la PC (los de Agustin) y respetando su codificacion (bytes
tal cual, cp1252) y su fin de linea (CRLF o LF).

    python tools/ini_merge.py <ini del repo> <ini de la PC> [--excluir w414 x415 ...] [--simular]

Regla: una linea "clave = valor" del repo que no existe en la PC (misma seccion) se inserta justo
despues de la ultima linea del repo anterior a ella que SI esta en la PC, junto con los comentarios
(;) que la preceden en el repo. Las claves que ya existen en la PC se dejan con el valor de la PC.
En los menus la clave es "choice = x416", "slider = y416", "toggle = ...": se compara por tipo + parametro.
Las claves de --excluir nunca se agregan (filas que escribe la DLL y no van al ini).
"""
import re, sys, zlib

def dividir(b):
    eol = b"\r\n" if b"\r\n" in b else b"\n"
    lineas = b.split(b"\n")
    if lineas and lineas[-1] == b"": lineas.pop()
    return [l.rstrip(b"\r") for l in lineas], eol

RE_SEC = re.compile(rb"^\s*\[([^\]]+)\]")
RE_KV = re.compile(rb"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$")
RE_MENU = re.compile(rb"^\s*(choice|slider|toggle|button|label|texto)\s*=\s*([xyzw]?\d+)\b")

def clave(linea, sec):
    """clave logica de una linea (None si es comentario / vacia / seccion)."""
    m = RE_MENU.match(linea)
    if m: return (sec, m.group(1).lower() + b":" + m.group(2))
    m = RE_KV.match(linea)
    if m and not linea.lstrip().startswith(b";"): return (sec, b"kv:" + m.group(1))
    return None

def indexar(lineas):
    sec = b""; idx = {}; secs = []
    for i, l in enumerate(lineas):
        m = RE_SEC.match(l)
        if m: sec = m.group(1); continue
        k = clave(l, sec)
        if k is not None and k not in idx: idx[k] = i
    return idx

def fin_de_seccion(lineas, i):
    """indice de la ultima linea no vacia de la seccion que contiene la linea i."""
    j = i
    while j + 1 < len(lineas) and not RE_SEC.match(lineas[j + 1]): j += 1
    while j > i and lineas[j].strip() == b"": j -= 1
    return j

def main():
    args = sys.argv[1:]
    simular = "--simular" in args
    excluir = set()
    if "--excluir" in args:
        k = args.index("--excluir"); excluir = set(a.encode() for a in args[k + 1:] if not a.startswith("--")); args = args[:k]
    args = [a for a in args if not a.startswith("--")]
    repo_p, pc_p = args[0], args[1]
    repo_b = open(repo_p, "rb").read(); pc_b = open(pc_p, "rb").read()
    repo, _ = dividir(repo_b); pc, eol = dividir(pc_b)
    idx_pc = indexar(pc)
    # recorrer el repo por secciones, acumulando comentarios previos
    sec = b""; pend = []; ultimo_pc = None; inserciones = []  # (pos_pc, [lineas])
    secciones_pc = {m.group(1): i for i, l in enumerate(pc) for m in [RE_SEC.match(l)] if m}
    for l in repo:
        m = RE_SEC.match(l)
        if m:
            sec = m.group(1); pend = []; ultimo_pc = None; continue
        if l.strip().startswith(b";") :
            pend.append(l); continue
        if l.strip() == b"":
            pend = []; continue
        k = clave(l, sec)
        if k is None: pend = []; continue
        if k in idx_pc:
            ultimo_pc = idx_pc[k]; pend = []; continue
        nombre = k[1].split(b":", 1)[1]
        if nombre in excluir: pend = []; continue
        if sec not in secciones_pc:
            print("AVISO: la seccion [%s] no existe en la PC; no se agrega %s" % (sec.decode("latin1"), nombre.decode("latin1"))); pend = []; continue
        pos = ultimo_pc if ultimo_pc is not None else fin_de_seccion(pc, secciones_pc[sec])
        inserciones.append((pos, pend + [l], nombre))
        pend = []
    # aplicar de atras hacia adelante para no mover indices; las que van tras la misma linea conservan el orden
    salida = list(pc)
    for pos, lineas, nombre in sorted(inserciones, key=lambda t: t[0], reverse=True):
        pass
    por_pos = {}
    for pos, lineas, nombre in inserciones: por_pos.setdefault(pos, []).extend(lineas)
    for pos in sorted(por_pos, reverse=True):
        salida[pos + 1:pos + 1] = por_pos[pos]
    for pos, lineas, nombre in inserciones:
        print("+ %s (%d lineas, tras la linea %d de la PC)" % (nombre.decode("latin1"), len(lineas), pos + 1))
    if not inserciones: print("nada que agregar")
    nuevo = eol.join(salida) + eol
    print("CRC32 antes %08X (%d B) -> despues %08X (%d B)" % (zlib.crc32(pc_b), len(pc_b), zlib.crc32(nuevo), len(nuevo)))
    if not simular and inserciones:
        open(pc_p, "wb").write(nuevo); print("escrito", pc_p)

if __name__ == "__main__":
    main()
