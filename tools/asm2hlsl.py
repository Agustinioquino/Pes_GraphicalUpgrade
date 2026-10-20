#!/usr/bin/env python3
"""PES21 FX - traductor mecanico de ensamblador DXBC (shader model 5, el que escribe el catalogo del mod en
catalog\\asm\\<hash>-p.txt / -v.txt) a HLSL compilable, instruccion por instruccion, al estilo de 3DMigoto:

  - cada registro temporal es un float4 que guarda VALORES (como 3DMigoto): los enteros se guardan como su valor en float
    (exacto hasta 2^24). No se guardan bits crudos: el compilador trata asfloat(1) como un denormal, lo aplana a 0 y
    rompe cuentas como la paridad del pixel o el contador de un bucle (probado: fxc dejaba la paridad constante)
  - las comparaciones dejan -1.0 / 0.0 (distinto de 0 = verdadero); 'and' con 0x3f800000 convierte esa mascara en 1.0
  - 'and'/'or' con mascaras grandes en hex (0x7fffffff...) sobre floats verdaderos se hacen con bits
  - mascara de destino + swizzle de origen: el componente p del destino toma el componente swizzle[p] del origen
  - entradas/salidas con la misma firma que el original (los elementos que comparten registro se arman a mano)

Uso: asm2hlsl.py <asm.txt> <salida.hlsl> [--entry main]
El resultado es la base "identica al juego" sobre la que se agregan las mejoras del mod.
"""
import re, struct, sys

COMP = 'xyzw'
# el desensamblador imprime los literales con 6 decimales: estos son los valores exactos de las constantes conocidas
# (si no, por ejemplo 0.000978 en vez de 1/1023 corre un escalon la codificacion de la normal de 10 bits)
EXACT = {
    '0.000978': '0.000977517106', '0.333333': '0.333333343', '0.318310': '0.318309873', '0.693147': '0.693147182',
    '28.853901': '28.8539009', '0.007812': '0.0078125', '0.014648': '0.0146484403', '0.606218': '0.606217802',
    '0.454545': '0.454545468', '0.166667': '0.166666672', '0.666667': '0.666666687', '0.142857': '0.142857149',
    '0.003922': '0.00392156886', '0.000488': '0.000488281250', '0.001953': '0.001953125', '0.003906': '0.00390625',
    '3.141593': '3.14159274', '6.283185': '6.28318548', '1.442695': '1.44269502', '0.159155': '0.159154937',
    '0.002000': '0.002', '0.212600': '0.212599993', '0.715200': '0.715200007', '0.072200': '0.0722000003',
    '0.299000': '0.298999995', '0.587000': '0.587000012', '0.114000': '0.114', '0.980392': '0.980392158',
    '0.960784': '0.960784316', '0.996078': '0.996078432', '0.000100': '9.99999975e-05', '0.000001': '9.99999997e-07',
}

def parse_signature(lines, title):
    """Devuelve lista de (name, index, mask, reg, sysval, fmt, used) de la tabla de firma."""
    out = []; inside = False
    for l in lines:
        if l.startswith('// ' + title):
            inside = True; continue
        if inside:
            if l.strip() in ('//',) and out:
                break
            m = re.match(r'//\s+(\S+)\s+(\d+)\s+([xyzw ]{4})\s+(\d+)\s+(\S+)\s+(\S+)\s*([xyzw ]*)$', l)
            if m:
                name, idx, mask, reg, sv, fmt, used = m.groups()
                out.append((name, int(idx), mask.replace(' ', ''), int(reg), sv, fmt, used.replace(' ', '')))
            elif out and not l.startswith('//'):
                break
    return out

def split_operands(s):
    ops = []; depth = 0; cur = ''
    for ch in s:
        if ch in '([': depth += 1
        if ch in ')]': depth -= 1
        if ch == ',' and depth == 0:
            ops.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip(): ops.append(cur.strip())
    return ops

class Tr:
    def __init__(self, text):
        self.lines = text.splitlines()
        self.body = []; self.decl = []; self.ind = 1
        self.cbs = {}; self.samplers = {}; self.cmpsamplers = set(); self.res = {}; self.icb = None
        self.temps = 0; self.idxtemps = []
        self.isPS = True
        self.ty = {}      # (registro, componente) -> 'f' float | 'i' entero (valor) | 'm' mascara -1/0 (valor)
        self.vty = {}     # lo mismo para las entradas v#
        self.tystack = []; self.loopcheck = []
        self.warns = []

    def warn(self, msg):
        if msg not in self.warns: self.warns.append(msg)

    # ---------------------------------------------------------------- operandos
    def opinfo(self, op):
        s = op.strip(); neg = ab = False
        if s.startswith('-'): neg = True; s = s[1:].strip()
        if s.startswith('|') and s.endswith('|'): ab = True; s = s[1:-1]
        elif s.startswith('|'): ab = True; s = s.strip('|')
        if s.startswith('l('):
            vals = [v.strip() for v in s[2:-1].split(',')]
            if len(vals) == 1: vals = vals * 4
            return {'neg': neg, 'ab': ab, 'kind': 'lit', 'vals': vals, 'txt': op}
        m = re.match(r'^(.*?)(?:\.([xyzw]{1,4}))?$', s)
        rb, swz = m.group(1), m.group(2) or 'xyzw'
        if len(swz) == 1: swz = swz * 4
        elif len(swz) < 4: swz = swz + swz[-1] * (4 - len(swz))
        if re.match(r'^r\d+$', rb): kind, key = 'temp', rb
        elif re.match(r'^x\d+\[', rb): kind, key = 'temp', rb.split('[')[0]
        elif re.match(r'^v\d+$', rb): kind, key = 'in', rb
        elif rb.startswith('icb'): kind, key = 'icb', 'icb'
        else: kind, key = 'mem', rb
        return {'neg': neg, 'ab': ab, 'kind': kind, 'key': key, 'base': self.base(rb), 'swz': swz, 'txt': op}

    @staticmethod
    def litbits(v):
        v = v.strip()
        if v.lower().startswith('0x'): return int(v, 16) & 0xffffffff
        if re.match(r'^-?\d+$', v): return int(v) & 0xffffffff
        return struct.unpack('<I', struct.pack('<f', float(EXACT.get(v.lstrip('-'), v.lstrip('-'))) * (-1 if v.startswith('-') else 1)))[0]

    @staticmethod
    def littype(v):
        v = v.strip()
        if Tr.litbits(v) == 0: return 'z'      # cero: vale para cualquier tipo
        if v.lower().startswith('0x'):
            n = int(v, 16)
            if n == 0xffffffff: return 'm'
            if (n & 0x7f800000) == 0: return 'i'
            return 'f'
        if re.match(r'^-?\d+$', v): return 'm' if int(v) == -1 else 'i'
        return 'f'

    @staticmethod
    def flit(v):
        """Literal en contexto float (valor)."""
        v = v.strip()
        if v.lower().startswith('0x'):
            n = int(v, 16)
            if n == 0xffffffff: return '-1.0'
            if (n & 0x7f800000) == 0: return '%d.0' % n            # entero chico guardado como valor
            return 'asfloat(0x%08xU)' % n
        if re.match(r'^-?\d+$', v): return v + '.0'
        neg = v.startswith('-'); a = v[1:] if neg else v
        a = EXACT.get(a, a)
        return ('-' if neg else '') + a

    @staticmethod
    def bitsf(n):
        f = struct.unpack('<f', struct.pack('<I', n))[0]
        t = '%.9g' % f
        if struct.unpack('<I', struct.pack('<f', float(t)))[0] == n and 'n' not in t:
            return t if ('.' in t or 'e' in t) else t + '.0'
        return 'asfloat(0x%08xU)' % n

    @staticmethod
    def ilit(v, uns):
        n = Tr.litbits(v)
        if uns: return '%dU' % n
        if n >= 0x80000000: n -= 0x100000000
        if n == -0x80000000: return '(int)0x80000000'
        return str(n)

    def ctype(self, info, p):
        k = info['kind']
        if k == 'lit': return self.littype(info['vals'][p])
        c = info['swz'][p]
        if k == 'temp': return self.ty.get((info['key'], c), 'f')
        if k == 'in': return self.vty.get((info['key'], c), 'f')
        if k == 'icb': return 'v'     # la tabla se declara con los valores impresos
        return 'c'                    # constant buffer: bits en memoria

    def comp(self, info, p):
        return '%s.%s' % (info['base'], info['swz'][p])

    def fsrc(self, op, used=range(4), typeless=False):
        """Operando de origen en contexto float: float4. Los registros guardan valores; un float leyendo un entero
        (reinterpretacion de bits, raro) se traduce exacto con asfloat y se avisa."""
        info = self.opinfo(op)
        if info['kind'] == 'lit':
            e = 'float4(%s)' % ', '.join(self.flit(v) for v in info['vals'])
        else:
            e = '%s.%s' % (info['base'], info['swz'])
            if info['kind'] == 'temp' and any(self.ctype(info, p) == 'X' for p in used):
                self.warn('lectura de un registro con tipo distinto segun la rama: %s' % op)
            if not typeless and info['kind'] in ('temp', 'in'):
                bad = [p for p in used if self.ctype(info, p) in ('i', 'm')]
                if bad:
                    self.warn('float leyendo entero (bits): %s' % op)
                    cs = []
                    for p in range(4):
                        c = self.comp(info, p)
                        cs.append('asfloat((int)%s)' % c if p in bad else c)
                    e = 'float4(%s)' % ', '.join(cs)
        if info['ab']: e = 'abs(%s)' % e
        if info['neg']: e = '-(%s)' % e
        return e

    def isrc(self, op, uns=False):
        """Operando de origen en contexto entero: int4 / uint4 (valores; bits solo para floats verdaderos y cbuffers)."""
        info = self.opinfo(op); T = 'uint' if uns else 'int'
        if info['kind'] == 'lit':
            e = '%s4(%s)' % (T, ', '.join(self.ilit(v, uns) for v in info['vals']))
        else:
            kinds = []
            for p in range(4):
                t = self.ctype(info, p)
                if t == 'f':
                    if info['kind'] in ('temp', 'in'): self.warn('entero leyendo float (bits): %s' % op)
                    kinds.append('b')
                elif t == 'c': kinds.append('b')
                else:
                    if t == 'X': self.warn('lectura entera de un registro con tipo distinto segun la rama: %s' % op)
                    kinds.append('v')
            def conv(k, x):
                if k == 'b': return 'as%s(%s)' % (T, x)
                return ('uint4(int4(%s))' if uns else 'int4(%s)') % x if x.count('.') and len(x.split('.')[-1]) == 4 else (('(uint)(int)%s' if uns else '(int)%s') % x)
            if all(k == kinds[0] for k in kinds):
                e = conv(kinds[0], '%s.%s' % (info['base'], info['swz']))
            else:
                e = '%s4(%s)' % (T, ', '.join(conv(kinds[p], self.comp(info, p)) for p in range(4)))
        if info['ab']: e = 'abs(%s)' % e
        if info['neg']: e = '(-%s)' % e
        return e

    def src(self, op, intctx=False, uns=False):
        return self.isrc(op, uns) if intctx else self.fsrc(op)

    def base(self, b):
        b = b.strip()
        m = re.match(r'^(cb\d+|icb|x\d+)\[(.*)\]$', b)
        if m:
            name, idx = m.groups()
            idx = re.sub(r'([rx]\d+)\.([xyzw])', lambda mm: '(int)%s.%s' % (mm.group(1), mm.group(2)), idx)
            return '%s[%s]' % (name, idx)
        return b

    def dst(self, op):
        s = op.strip()
        m = re.match(r'^(.*?)\.([xyzw]{1,4})$', s)
        if not m: return self.base(s), 'xyzw'
        return self.base(m.group(1)), m.group(2)

    def dkey(self, op):
        s = op.strip().split('.')[0]
        if s.startswith('x') and '[' in s: return s.split('[')[0]
        return s

    def emit(self, s):
        self.body.append('  ' * self.ind + s)

    def settype(self, dop, types):
        d, mask = self.dst(dop); key = self.dkey(dop)
        if not (key.startswith('r') or key.startswith('x')): return
        for c in mask:
            p = COMP.index(c)
            self.ty[(key, c)] = types[p] if isinstance(types, (list, tuple)) else types

    def assign(self, dop, expr4, sat=False, kind='f', types=None):
        """expr4: expresion de 4 componentes (float4 / int4 / uint4 segun kind)."""
        d, mask = self.dst(dop)
        if kind == 'i' or kind == 'u':
            expr4 = 'float4(%s)' % expr4
        if sat: expr4 = 'saturate(%s)' % expr4
        self.emit('%s.%s = (%s).%s;' % (d, mask, expr4, mask))
        self.settype(dop, types if types is not None else ('i' if kind in ('i', 'u') else 'f'))

    # ---------------------------------------------------------------- tipos en las ramas
    def br_if(self):
        self.tystack.append({'pre': dict(self.ty), 'then': None})

    def br_else(self):
        top = self.tystack[-1]; top['then'] = dict(self.ty); self.ty = dict(top['pre'])

    def br_endif(self):
        e = self.tystack.pop(); other = e['then'] if e['then'] is not None else e['pre']
        for k in set(self.ty) | set(other):
            a = self.ty.get(k); b = other.get(k)
            if a == b: continue
            if a is None or a == 'z': self.ty[k] = b
            elif b is None or b == 'z': pass
            elif set((a, b)) == set(('i', 'm')): self.ty[k] = 'i'
            else: self.ty[k] = 'X'          # se avisa solo si se lee antes de volver a escribirse

    # ---------------------------------------------------------------- and / or / xor (por componente)
    def bitop(self, op, ops):
        d, mask = self.dst(ops[0])
        A = self.opinfo(ops[1]); B = self.opinfo(ops[2])
        for X in (A, B):
            if X['neg'] or X['ab']: raise SystemExit('modificador en operacion de bits: ' + op)
        if A['kind'] == 'lit' and B['kind'] != 'lit': A, B = B, A
        sym = {'and': '&', 'or': '|', 'xor': '^'}[op]
        ex = ['0.0'] * 4; ty = ['f'] * 4
        for p in range(4):
            if COMP[p] not in mask: continue
            ta = self.ctype(A, p); tb = self.ctype(B, p)
            a = self.comp(A, p) if A['kind'] != 'lit' else self.flit(A['vals'][p])
            if B['kind'] == 'lit':
                n = self.litbits(B['vals'][p]); lt = self.littype(B['vals'][p])
                if op == 'and' and n == 0:
                    ex[p] = '0.0'; ty[p] = 'z'
                elif op == 'and' and n == 0xffffffff:
                    ex[p] = a; ty[p] = ta
                elif ta in ('m', 'i', 'v', 'z', 'X') and lt == 'f':
                    if op == 'and':     # mascara & bits de un float (0x3f800000 = 1.0): mascara ? float : 0
                        if ta != 'm': self.warn('and de entero con bits de float: %s' % ops[1])
                        ex[p] = '((%s != 0) ? %s : 0.0)' % (a, Tr.bitsf(n)); ty[p] = 'f'
                    else:               # mantisa (valor) | exponente -> float armado con bits
                        ex[p] = 'asfloat((uint)(int)%s %s 0x%08xU)' % (a, sym, n); ty[p] = 'f'
                elif ta in ('m', 'i', 'v', 'z', 'X'):
                    ex[p] = '(float)((int)%s %s %s)' % (a, sym, self.ilit(B['vals'][p], False))
                    ty[p] = 'm' if (ta == 'm' and (n == 0xffffffff or op != 'and')) else 'i'
                else:                   # float verdadero (o cbuffer) con mascara de bits
                    keeps = (op == 'and' and (n & 0x7f800000) == 0x7f800000) or (op != 'and' and n in (0x80000000,)) \
                        or (op == 'or' and (n & 0x7f800000) != 0)
                    if keeps:
                        ex[p] = 'asfloat(asuint(%s) %s 0x%08xU)' % (a, sym, n); ty[p] = 'f'
                    else:
                        ex[p] = '(float)(asuint(%s) %s 0x%08xU)' % (a, sym, n); ty[p] = 'i'
            else:
                b = self.comp(B, p)
                if ta in ('m', 'i', 'v', 'z', 'X') and tb in ('m', 'i', 'v', 'z', 'X'):
                    ex[p] = '(float)((int)%s %s (int)%s)' % (a, sym, b)
                    ty[p] = 'm' if (ta == 'm' and tb == 'm') else 'i'
                elif op == 'and' and (ta == 'm') != (tb == 'm') and 'f' in (ta, tb):
                    mk, fv = (a, b) if ta == 'm' else (b, a)
                    ex[p] = '((%s != 0) ? %s : 0.0)' % (mk, fv); ty[p] = 'f'
                else:
                    self.warn('operacion de bits entre floats: %s %s, %s' % (op, ops[1], ops[2]))
                    ex[p] = 'asfloat(asuint(%s) %s asuint(%s))' % (a, sym, b); ty[p] = 'f'
        self.emit('%s.%s = (float4(%s)).%s;' % (d, mask, ', '.join(ex), mask))
        self.settype(ops[0], ty)

    # ---------------------------------------------------------------- instrucciones
    def tr_instr(self, line):
        raw = line.strip()
        if not raw or raw.startswith('//'): return
        m = re.match(r'^([a-z_0-9]+(?:\([^)]*\))*)\s*(.*)$', raw)
        opfull, rest = m.group(1), m.group(2)
        sat = '_sat' in opfull
        op = opfull.replace('_sat', '')
        ops = split_operands(rest) if rest else []
        dmask = lambda i=0: [COMP.index(c) for c in self.dst(ops[i])[1]] if ops[i] != 'null' else []
        F = lambda i, used=None: self.fsrc(ops[i], dmask(0) if used is None else used)
        I = lambda i: self.isrc(ops[i])
        U = lambda i: self.isrc(ops[i], True)

        simple2 = {'add': '(%s + %s)', 'mul': '(%s * %s)', 'div': '(%s / %s)', 'min': 'min(%s, %s)', 'max': 'max(%s, %s)'}
        simple1 = {'sqrt': 'sqrt(%s)', 'rsq': 'rsqrt(%s)', 'exp': 'exp2(%s)', 'log': 'log2(%s)', 'frc': 'frac(%s)',
                   'round_ni': 'floor(%s)', 'round_pi': 'ceil(%s)', 'round_z': 'trunc(%s)', 'round_ne': 'round(%s)', 'rcp': 'rcp(%s)',
                   'deriv_rtx_fine': 'ddx_fine(%s)', 'deriv_rty_fine': 'ddy_fine(%s)', 'deriv_rtx_coarse': 'ddx_coarse(%s)',
                   'deriv_rty_coarse': 'ddy_coarse(%s)', 'deriv_rtx': 'ddx(%s)', 'deriv_rty': 'ddy(%s)'}
        cmpf = {'lt': '<', 'ge': '>=', 'eq': '==', 'ne': '!='}
        cmpi = {'ilt': ('<', 0), 'ige': ('>=', 0), 'ieq': ('==', 0), 'ine': ('!=', 0), 'ult': ('<', 1), 'uge': ('>=', 1)}
        MASK = '? float4(-1.0, -1.0, -1.0, -1.0) : float4(0.0, 0.0, 0.0, 0.0))'
        if op in simple2:
            self.assign(ops[0], simple2[op] % (F(1), F(2)), sat)
        elif op in simple1:
            self.assign(ops[0], simple1[op] % F(1), sat)
        elif op == 'mov':
            info = self.opinfo(ops[1])
            if sat:
                self.assign(ops[0], F(1), sat)
            else:
                self.assign(ops[0], self.fsrc(ops[1], typeless=True), False, types=[self.ctype(info, p) if self.ctype(info, p) in ('f', 'i', 'm', 'z', 'X') else ('i' if self.ctype(info, p) == 'v' else 'f') for p in range(4)])
        elif op == 'mad':
            self.assign(ops[0], '(%s * %s + %s)' % (F(1), F(2), F(3)), sat)
        elif op in ('dp2', 'dp3', 'dp4'):
            n = int(op[2]); sw = COMP[:n]
            a = F(1, range(n)); b = F(2, range(n))
            self.assign(ops[0], '(dot((%s).%s, (%s).%s)).xxxx' % (a, sw, b, sw), sat)
        elif op in cmpf:
            self.assign(ops[0], '((%s %s %s) %s' % (F(1), cmpf[op], F(2), MASK), types='m')
        elif op in cmpi:
            o, uns = cmpi[op]
            a = U(1) if uns else I(1); b = U(2) if uns else I(2)
            self.assign(ops[0], '((%s %s %s) %s' % (a, o, b, MASK), types='m')
        elif op == 'movc':
            c = self.fsrc(ops[1], typeless=True)
            A = self.opinfo(ops[2]); B = self.opinfo(ops[3]); tys = []
            for p in range(4):
                ta = self.ctype(A, p); tb = self.ctype(B, p)
                ta = 'i' if ta == 'v' else ('f' if ta == 'c' else ta); tb = 'i' if tb == 'v' else ('f' if tb == 'c' else tb)
                zA = A['kind'] == 'lit' and self.litbits(A['vals'][p]) == 0
                zB = B['kind'] == 'lit' and self.litbits(B['vals'][p]) == 0
                if ta == tb: tys.append(ta)
                elif zA or ta == 'z': tys.append(tb)
                elif zB or tb == 'z': tys.append(ta)
                elif set((ta, tb)) == set(('i', 'm')): tys.append('i')
                else:
                    if COMP[p] in self.dst(ops[0])[1]: self.warn('movc con tipos distintos: ' + raw)
                    tys.append('f')
            e = '((%s != 0) ? %s : %s)' % (c, self.fsrc(ops[2], typeless=True), self.fsrc(ops[3], typeless=True))
            self.assign(ops[0], e, sat, types=tys)
        elif op in ('and', 'or', 'xor'):
            self.bitop(op, ops)
        elif op == 'not':
            info = self.opinfo(ops[1])
            self.assign(ops[0], '(~%s)' % I(1), kind='i', types=['m' if self.ctype(info, p) == 'm' else 'i' for p in range(4)])
        elif op == 'iadd':
            self.assign(ops[0], '(%s + %s)' % (I(1), I(2)), kind='i')
        elif op == 'imul':   # imul null, dst, a, b
            self.assign(ops[1], '(%s * %s)' % (I(2), I(3)), kind='i')
        elif op == 'imad':
            self.assign(ops[0], '(%s * %s + %s)' % (I(1), I(2), I(3)), kind='i')
        elif op == 'imax':
            self.assign(ops[0], 'max(%s, %s)' % (I(1), I(2)), kind='i')
        elif op == 'imin':
            self.assign(ops[0], 'min(%s, %s)' % (I(1), I(2)), kind='i')
        elif op == 'umax':
            self.assign(ops[0], 'max(%s, %s)' % (U(1), U(2)), kind='u')
        elif op == 'umin':
            self.assign(ops[0], 'min(%s, %s)' % (U(1), U(2)), kind='u')
        elif op == 'ishl':
            self.assign(ops[0], '(%s << (%s & 31))' % (I(1), U(2)), kind='i')
        elif op == 'ishr':
            self.assign(ops[0], '(%s >> (%s & 31))' % (I(1), U(2)), kind='i')
        elif op == 'ushr':
            self.assign(ops[0], '(%s >> (%s & 31))' % (U(1), U(2)), kind='u')
        elif op == 'ineg':
            self.assign(ops[0], '(-%s)' % I(1), kind='i')
        elif op == 'ftoi':
            self.assign(ops[0], 'int4(%s)' % F(1), kind='i')
        elif op == 'ftou':
            self.assign(ops[0], 'uint4(%s)' % F(1), kind='u')
        elif op == 'itof':
            self.assign(ops[0], 'float4(%s)' % I(1), sat)
        elif op == 'utof':
            self.assign(ops[0], 'float4(%s)' % U(1), sat)
        elif op == 'bfi':   # bfi dst, width, offset, src2(insert), src3(base)
            w, o, ins, bse = U(1), U(2), U(3), U(4)
            self.assign(ops[0], 'PESBfi(%s, %s, %s, %s)' % (w, o, ins, bse), kind='u')
        elif op == 'sincos':
            if ops[0] != 'null': self.assign(ops[0], 'sin(%s)' % self.fsrc(ops[2], dmask(0)), sat)
            if ops[1] != 'null': self.assign(ops[1], 'cos(%s)' % self.fsrc(ops[2], dmask(1)), sat)
        elif op.startswith('sample') or op.startswith('ld') or op.startswith('resinfo') or op.startswith('gather'):
            self.tr_sample(op, opfull, ops)
        elif op == 'discard_nz':
            self.emit('if ((%s).x != 0) discard;' % self.fsrc(ops[0], typeless=True))
        elif op == 'discard_z':
            self.emit('if ((%s).x == 0) discard;' % self.fsrc(ops[0], typeless=True))
        elif op == 'if_nz':
            self.emit('if ((%s).x != 0) {' % self.fsrc(ops[0], typeless=True)); self.ind += 1; self.br_if()
        elif op == 'if_z':
            self.emit('if ((%s).x == 0) {' % self.fsrc(ops[0], typeless=True)); self.ind += 1; self.br_if()
        elif op == 'else':
            self.ind -= 1; self.emit('} else {'); self.ind += 1; self.br_else()
        elif op == 'endif':
            self.ind -= 1; self.emit('}'); self.br_endif()
        elif op == 'loop':
            self.emit('[loop] while (true) {'); self.ind += 1
            self.loopcheck.append(dict(self.ty))
        elif op == 'endloop' and self.loopcheck:
            pre = self.loopcheck.pop()
            for k, t in self.ty.items():
                a = pre.get(k)
                if a not in (None, 'z', t) and set((a, t)) != set(('i', 'm')):
                    self.warn('tipo que cambia en el bucle: %s.%s (%s antes, %s al final): revisar' % (k[0], k[1], a, t))
            self.ind -= 1; self.emit('}')
        elif op == 'endloop':
            self.ind -= 1; self.emit('}')
        elif op == 'breakc_nz':
            self.emit('if ((%s).x != 0) break;' % self.fsrc(ops[0], typeless=True))
        elif op == 'breakc_z':
            self.emit('if ((%s).x == 0) break;' % self.fsrc(ops[0], typeless=True))
        elif op == 'break':
            self.emit('break;')
        elif op == 'ret':
            self.emit('return;')
        else:
            raise SystemExit('instruccion no soportada: ' + raw)

    def tr_sample(self, op, opfull, ops):
        dims = re.search(r'\((texture\w+|buffer|raw_buffer|structured_buffer)\)', opfull)
        dim = dims.group(1) if dims else 'texture2d'
        nc = {'texture1d': 1, 'texture2d': 2, 'texture2darray': 3, 'texture3d': 3, 'texturecube': 3, 'texture1darray': 2}.get(dim, 2)
        no = {'texture1d': 1, 'texture2d': 2, 'texture2darray': 2, 'texture3d': 3, 'texture1darray': 1}.get(dim, 2)
        base = op.split('(')[0]
        off = ''
        mo = re.search(r'_aoffimmi(?:_indexable)?\((-?\d+),(-?\d+),(-?\d+)\)', opfull)
        if mo:
            off = ', int%d(%s)' % (no, ', '.join(mo.groups()[:no])) if no > 1 else ', %s' % mo.group(1)
            base = base.replace('_aoffimmi', '')
        if not base.endswith('_indexable') and base + '_indexable' in ('sample_indexable',): pass
        def resswz(r):
            m = re.match(r'^(t\d+)(?:\.([xyzw]{1,4}))?$', r.strip())
            sw = m.group(2) or 'xyzw'
            if len(sw) < 4: sw = sw + sw[-1] * (4 - len(sw))
            return m.group(1), sw
        C = lambda i: self.fsrc(ops[i], range(nc))
        S = lambda i: self.fsrc(ops[i], [0])
        rt = self.res.get(int(ops[2].strip().split('.')[0][1:]), ('', ''))[1] if ops[2].strip().startswith('t') else ''
        rty = 'i' if ('uint' in rt or 'sint' in rt) else 'f'
        if base in ('sample_indexable', 'sample'):
            t, rs = resswz(ops[2]); s = ops[3].strip()
            e = '%s.Sample(%s, (%s).%s%s)' % (t, s, C(1), COMP[:nc], off)
            self.assign(ops[0], '(%s).%s' % (e, rs))
        elif base in ('sample_l_indexable', 'sample_l'):
            t, rs = resswz(ops[2]); s = ops[3].strip()
            e = '%s.SampleLevel(%s, (%s).%s, (%s).x%s)' % (t, s, C(1), COMP[:nc], S(4), off)
            self.assign(ops[0], '(%s).%s' % (e, rs))
        elif base in ('sample_b_indexable', 'sample_b'):
            t, rs = resswz(ops[2]); s = ops[3].strip()
            e = '%s.SampleBias(%s, (%s).%s, (%s).x%s)' % (t, s, C(1), COMP[:nc], S(4), off)
            self.assign(ops[0], '(%s).%s' % (e, rs))
        elif base in ('sample_d_indexable', 'sample_d'):
            t, rs = resswz(ops[2]); s = ops[3].strip()
            e = '%s.SampleGrad(%s, (%s).%s, (%s).%s, (%s).%s%s)' % (t, s, C(1), COMP[:nc], self.fsrc(ops[4], range(no)), COMP[:no], self.fsrc(ops[5], range(no)), COMP[:no], off)
            self.assign(ops[0], '(%s).%s' % (e, rs))
        elif base in ('sample_c_indexable', 'sample_c'):
            t, rs = resswz(ops[2]); s = ops[3].strip(); self.cmpsamplers.add(s)
            e = '(%s.SampleCmp(%s, (%s).%s, (%s).x%s)).xxxx' % (t, s, C(1), COMP[:nc], S(4), off)
            self.assign(ops[0], e)
        elif base in ('sample_c_lz_indexable', 'sample_c_lz'):
            t, rs = resswz(ops[2]); s = ops[3].strip(); self.cmpsamplers.add(s)
            e = '(%s.SampleCmpLevelZero(%s, (%s).%s, (%s).x%s)).xxxx' % (t, s, C(1), COMP[:nc], S(4), off)
            self.assign(ops[0], e)
        elif base in ('ld_indexable', 'ld'):
            t, rs = resswz(ops[2])
            e = '%s.Load(%s.%s%s)' % (t, self.isrc(ops[1]), COMP[:nc + 1], off)
            self.assign(ops[0], '(%s).%s' % (e, rs), types=rty)
        elif base in ('resinfo_indexable', 'resinfo'):
            t, rs = resswz(ops[2]); uns = '_uint' in opfull
            self.need_ri = True
            e = 'PESResInfo%d(%s, (%s).x)' % (nc, t, self.isrc(ops[1], True))
            self.assign(ops[0], '(%s).%s' % (e, rs), types='i' if uns else 'f')
            if not uns: self.warn('resinfo sin _uint (float): revisar')
        else:
            raise SystemExit('muestreo no soportado: ' + opfull)

    # ---------------------------------------------------------------- todo el archivo
    def run(self, entry='main'):
        ins = parse_signature(self.lines, 'Input signature')
        outs = parse_signature(self.lines, 'Output signature')
        for (name, idx, mask, reg, sv, fmt, used) in ins:
            for c in mask:
                if name == 'SV_IsFrontFace': self.vty[('v%d' % reg, c)] = 'm'
                elif fmt in ('uint', 'int'): self.vty[('v%d' % reg, c)] = 'i'
        code = []; started = False
        for l in self.lines:
            if re.match(r'^(ps|vs|cs|gs|hs|ds)_5_0', l): started = True; self.isPS = l.startswith('ps'); continue
            if not started: continue
            code.append(l)
        # declaraciones
        i = 0
        while i < len(code):
            l = code[i].strip()
            if l.startswith('dcl_immediateConstantBuffer'):
                vals = []
                j = i
                txt = ''
                while True:
                    txt += code[j]
                    if '}' in code[j] and code[j].strip().endswith('}') and txt.count('{') == txt.count('}'): break
                    j += 1
                for mm in re.finditer(r'\{\s*([^{}]+?)\s*\}', txt):
                    vals.append([v.strip() for v in mm.group(1).split(',')])
                self.icb = vals; i = j + 1; continue
            if l.startswith('dcl_constantbuffer'):
                m = re.search(r'CB(\d+)\[(\d+)\]', l); self.cbs[int(m.group(1))] = int(m.group(2))
            elif l.startswith('dcl_sampler'):
                m = re.search(r's(\d+), (mode_\w+)', l); self.samplers[int(m.group(1))] = m.group(2)
            elif l.startswith('dcl_resource_'):
                m = re.search(r'dcl_resource_(\w+) \(([^)]*)\) t(\d+)', l)
                self.res[int(m.group(3))] = (m.group(1), m.group(2))
            elif l.startswith('dcl_temps'):
                self.temps = int(l.split()[1])
            elif l.startswith('dcl_indexableTemp'):
                m = re.search(r'x(\d+)\[(\d+)\], (\d+)', l); self.idxtemps.append((int(m.group(1)), int(m.group(2))))
            elif l.startswith('dcl_') or not l:
                pass
            else:
                self.tr_instr(l)
            i += 1
        # cabecera
        h = []
        for n, size in sorted(self.cbs.items()):
            h.append('cbuffer cb%d : register(b%d) { float4 cb%d[%d]; }' % (n, n, n, size))
        for n, mode in sorted(self.samplers.items()):
            t = 'SamplerComparisonState' if mode == 'mode_comparison' else 'SamplerState'
            h.append('%s s%d : register(s%d);' % (t, n, n))
        tmap = {'texture2d': 'Texture2D<float4>', 'texture3d': 'Texture3D<float4>', 'texturecube': 'TextureCube<float4>',
                'texture2darray': 'Texture2DArray<float4>', 'texture1d': 'Texture1D<float4>'}
        for n, (dim, fmt) in sorted(self.res.items()):
            h.append('%s t%d : register(t%d);' % (tmap.get(dim, 'Texture2D<float4>'), n, n))
        if self.icb:
            h.append('static const float4 icb[%d] = { %s };' % (len(self.icb), ', '.join('float4(%s)' % ', '.join(self.flit(x) for x in v) for v in self.icb)))
        h.append('uint4 PESBfi(uint4 w, uint4 o, uint4 ins, uint4 b) { uint4 m = (w >= 32u) ? 0xffffffffu : (((1u << w) - 1u) << o); return ((ins << o) & m) | (b & ~m); }')
        if getattr(self, 'need_ri', False):
            h.append('float4 PESResInfo2(Texture2D<float4> t, uint m) { uint w, hh, l; t.GetDimensions(m, w, hh, l); return float4(w, hh, 0, l); }')
        # firma
        params = []; vregs = {}
        for (name, idx, mask, reg, sv, fmt, used) in ins:
            sem = name + (str(idx) if not name.startswith('SV_') or name in ('SV_Target',) else ('' if idx == 0 else str(idx)))
            if name == 'SV_IsFrontFace':
                params.append('bool i_ff : SV_IsFrontFace'); vregs.setdefault(reg, []).append(('ff', mask, 'i_ff')); continue
            if name == 'SV_Position':
                params.append('float4 i_pos : SV_Position0'); vregs.setdefault(reg, []).append(('pos', mask, 'i_pos')); continue
            ty = 'float%d' % len(mask) if len(mask) > 1 else 'float'
            if fmt == 'uint': ty = ty.replace('float', 'uint')
            if fmt == 'int': ty = ty.replace('float', 'int')
            pn = 'i_%s%d' % (name.lower(), idx)
            params.append('%s %s : %s%d' % (ty, pn, name, idx)); vregs.setdefault(reg, []).append((name, mask, pn))
        for (name, idx, mask, reg, sv, fmt, used) in outs:
            ty = 'float%d' % len(mask) if len(mask) > 1 else 'float'
            if fmt == 'uint': ty = ty.replace('float', 'uint')
            if fmt == 'int': ty = ty.replace('float', 'int')
            if name == 'SV_Target':
                params.append('out %s o%d : SV_Target%d' % (ty, reg, idx))
            elif name == 'SV_Depth':
                params.append('out float oDepth : SV_Depth')
            else:
                params.append('out %s o%d : %s%d' % (ty, reg, name, idx))
        pre = []
        pre.append('float4 %s;' % ', '.join('r%d' % k for k in range(max(self.temps, 1))))
        for (k, n) in self.idxtemps: pre.append('float4 x%d[%d];' % (k, n))
        for reg, els in sorted(vregs.items()):
            pre.append('float4 v%d = 0;' % reg)
            for (name, mask, pn) in els:
                if name == 'ff': pre.append('v%d.x = %s ? -1.0 : 0.0;' % (reg, pn))
                else: pre.append('v%d.%s = %s;' % (reg, mask, pn))
        out = []
        out.append('// generado por tools/asm2hlsl.py (traduccion mecanica, identica al juego; registros con valores)')
        for w in self.warns: out.append('// AVISO: ' + w)
        out += h
        out.append('Texture1D<float4> IniParams : register(t120);')
        out.append('')
        out.append('void %s(' % entry)
        out.append(',\n'.join('  ' + p for p in params) + ')')
        out.append('{')
        out += ['  ' + p for p in pre]
        out += self.body
        out.append('}')
        return '\n'.join(out) + '\n'

if __name__ == '__main__':
    src = open(sys.argv[1], encoding='cp1252', errors='replace').read()
    t = Tr(src); res = t.run()
    open(sys.argv[2], 'w').write(res)
    for w in t.warns: print('AVISO:', w)
    print('ok:', sys.argv[2], len(t.body), 'lineas de codigo')
