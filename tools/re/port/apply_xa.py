#!/usr/bin/env python3
# Pasada unica (12/11): envuelve en XA() las direcciones fijas de 1.07.02 del codigo (no las de comentarios ni cadenas) y cambia
# la comparacion del sello por ExeStampOk(). Queda como registro de como se hizo; no hace falta volver a correrla.
import re, sys, os
FILES = 'camhook cmdlistguard exepatch faceinject fisico freecam hooks memwatch menumouse motorpage musculo personajes rastro relwatch soleditor solreloj tiempo'.split()
SKIP = {0x140000000, 0x143a00000, 0x146000000, 0x142538000}   # base y limites (se tratan a mano)
LIT = re.compile(r'0x14[0-9a-fA-F]{7}(?:ull|ULL)?(?![0-9a-fA-Fx])')
STAMP = re.compile(r'nt->FileHeader\.TimeDateStamp (!=|==) (kStamp|kExeStamp|kExeStampSol)\b')
def code_fix(seg, cnt):
    seg = seg.replace('0x1416bdcc0 - 0x10', '0x1416bdcb0')
    def r(m):
        t = m.group(0); v = int(t.rstrip('ulUL'), 16)
        if v in SKIP: return t
        cnt[0] += 1
        return 'XA(0x%xull)' % v
    seg = LIT.sub(r, seg)
    def s(m): cnt[1] += 1; return ('!' if m.group(1) == '!=' else '') + 'ExeStampOk((unsigned)nt->FileHeader.TimeDateStamp)'
    return STAMP.sub(s, seg)
def fix_line(line, st, cnt):
    out = []; i = 0; n = len(line); seg = ''
    while i < n:
        c = line[i]
        if st['blk']:
            j = line.find('*/', i)
            if j < 0: out.append(line[i:]); i = n
            else: out.append(line[i:j + 2]); i = j + 2; st['blk'] = False
            continue
        if c == '/' and line[i:i + 2] == '//': out.append(code_fix(seg, cnt)); seg = ''; out.append(line[i:]); i = n; break
        if c == '/' and line[i:i + 2] == '/*': out.append(code_fix(seg, cnt)); seg = ''; st['blk'] = True; out.append('/*'); i += 2; continue
        if c in '"\'':
            j = i + 1
            while j < n and line[j] != c: j += 2 if line[j] == '\\' else 1
            out.append(code_fix(seg, cnt)); seg = ''; out.append(line[i:j + 1]); i = j + 1; continue
        seg += c; i += 1
    out.append(code_fix(seg, cnt))
    return ''.join(out)
root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'src'))
for f in FILES:
    p = os.path.join(root, f + '.cpp'); src = open(p, encoding='utf-8', newline='').read()
    st = {'blk': False}; cnt = [0, 0]
    res = ''.join(fix_line(l, st, cnt) for l in src.splitlines(keepends=True))
    if res != src: open(p, 'w', encoding='utf-8', newline='').write(res)
    print('%-14s %3d direcciones, %d sellos' % (f, cnt[0], cnt[1]))
