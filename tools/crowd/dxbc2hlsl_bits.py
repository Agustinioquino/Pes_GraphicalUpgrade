#!/usr/bin/env python3
"""PES21 FX - traductor EXACTO A NIVEL DE BITS de ensamblador DXBC (vs_5_0 del catalogo del mod) a HLSL.
Hecho para los VS del publico (instanceBuf / animeInstanceBuf: buffers estructurados con uint empaquetados de 32 bits),
que tools/asm2hlsl.py no puede traducir (guarda los enteros como valor float: pierde bits por encima de 2^24).

Modelo: cada registro (r, v, o, cb) es un uint4 con los BITS del original. Las operaciones de float hacen asfloat() de sus
entradas y asuint() del resultado; las de enteros y los mov / movc / cargas copian bits sin pasar por float (sin denormales
aplanados). Asi el resultado es el mismo que el shader del juego, bit por bit (salvo el redondeo propio del compilador).

Uso: dxbc2hlsl_bits.py <asm.txt> <salida.hlsl> [--prologo archivo.hlsli --funcion Nombre]
  --prologo: se incluye el archivo y se llama a Nombre(inout v0..vN, ...) antes de la primera instruccion: ahi el mod cambia
             las entradas (posicion, normal, tangente) antes del skinning del juego. La firma la arma el traductor:
             void Nombre(inout uint4 vPOS, inout uint4 vNRM, inout uint4 vTAN, uint4 vCOL, uint persona)
             (persona = instanceIndexBuf[cb5[9].x + SV_InstanceID], el mismo indice que usa el juego en instanceBuf)
"""
import re, sys, struct

C = 'xyzw'

def lit_bits(s):
    s = s.strip()
    if s.lower().startswith('0x'): return int(s, 16) & 0xffffffff
    if re.match(r'^-?\d+$', s): return int(s) & 0xffffffff
    return struct.unpack('<I', struct.pack('<f', float(s)))[0]

class Op:
    def __init__(self, txt):
        t = txt.strip()
        self.neg = False; self.abs = False
        if t.startswith('-'): self.neg = True; t = t[1:]
        if t.startswith('|') and t.endswith('|'): self.abs = True; t = t[1:-1]
        self.raw = t
        if t.startswith('l('):
            vals = [v for v in t[2:-1].split(',')]
            vals = [lit_bits(v) for v in vals]
            if len(vals) == 1: vals = vals * 4
            self.kind = 'lit'; self.vals = vals; self.swz = 'xyzw'; return
        m = re.match(r'^(cb\d+)\[(\d+)\]\.([xyzw]+)$', t)
        if m: self.kind = 'reg'; self.name = '%s[%s]' % (m.group(1), m.group(2)); self.swz = m.group(3); return
        m = re.match(r'^([rvot]\d+)(?:\.([xyzw]+))?$', t)
        if m: self.kind = 'reg'; self.name = m.group(1); self.swz = m.group(2) or 'xyzw'; return
        if t == 'null': self.kind = 'null'; return
        raise SystemExit('operando no entendido: ' + txt)

    def comp(self, p, dstmask=None):
        """bits del componente que corresponde a la posicion p del destino (swizzle de 4 o de menos letras)"""
        sw = self.swz
        c = sw[p] if len(sw) == 4 else (sw[p] if p < len(sw) else sw[-1])
        if self.kind == 'lit': return '%du' % self.vals[C.index(c)]
        return '%s.%s' % (self.name, c)

def split_ops(s):
    out = []; depth = 0; cur = ''
    for ch in s:
        if ch == '(' : depth += 1
        if ch == ')' : depth -= 1
        if ch == ',' and depth == 0: out.append(cur); cur = ''; continue
        cur += ch
    if cur.strip(): out.append(cur)
    return [o.strip() for o in out]

class Tr:
    def __init__(self, text):
        self.lines = text.split('\n')
        self.body = []; self.ind = 1
        self.res = {}; self.cbs = {}; self.temps = 1; self.structs = {}

    def emit(self, s): self.body.append('  ' * self.ind + s)

    def dst(self, txt):
        o = Op(txt);
        if o.kind == 'null': return None, ''
        return o.name, o.swz

    # ---- fuentes
    def f(self, o, p):   # float del componente p con modificadores
        e = 'asfloat(%s)' % o.comp(p)
        if o.abs: e = 'abs(%s)' % e
        if o.neg: e = '(-%s)' % e
        return e
    def u(self, o, p):   # uint del componente p (neg = negacion entera)
        e = o.comp(p)
        if o.abs: raise SystemExit('abs en operacion entera')
        if o.neg: e = '(0u - %s)' % e
        return e
    def i(self, o, p): return 'asint(%s)' % self.u(o, p)

    def put(self, d, mask, fn, sat=False, isfloat=True):
        """fn(p) -> expresion del componente p (float si isfloat, si no uint)"""
        if d is None: return
        for p, c in enumerate(C):
            if c not in mask: continue
            e = fn(p)
            if isfloat:
                if sat: e = 'saturate(%s)' % e
                e = 'asuint(%s)' % e
            self.emit('%s.%s = %s;' % (d, c, e))

    def putv(self, d, mask, fn, sat=False, isfloat=True):
        """igual pero calcula todo antes de escribir (por si el destino es tambien fuente)"""
        if d is None: return
        tmp = []
        for p, c in enumerate(C):
            if c not in mask: continue
            e = fn(p)
            if isfloat:
                if sat: e = 'saturate(%s)' % e
                e = 'asuint(%s)' % e
            tmp.append((c, e))
        if len(tmp) == 1: self.emit('%s.%s = %s;' % (d, tmp[0][0], tmp[0][1])); return
        self.emit('{ uint4 _t = %s; %s %s = _t; }' % (d, ' '.join('_t.%s = %s;' % (c, e) for c, e in tmp), d))

    def tr(self, line):
        line = line.strip()
        m = re.match(r'^([a-z_0-9]+(?:\([^)]*\))*)\s*(.*)$', line)
        op = m.group(1); rest = m.group(2)
        sat = op.endswith('_sat'); op0 = op[:-4] if sat else op
        if op0.startswith('ld_structured'):
            ops = split_ops(rest); self.ld_structured(op, ops); return
        ops = split_ops(rest)
        O = [Op(x) for x in ops[1:]] if ops else []
        d, mask = self.dst(ops[0]) if ops else (None, '')
        F2 = {'add': '%s + %s', 'mul': '%s * %s', 'min': 'min(%s, %s)', 'max': 'max(%s, %s)', 'div': '%s / %s'}
        if op0 in F2:
            self.putv(d, mask, lambda p: F2[op0] % (self.f(O[0], p), self.f(O[1], p)), sat)
        elif op0 == 'mad':
            self.putv(d, mask, lambda p: 'mad(%s, %s, %s)' % (self.f(O[0], p), self.f(O[1], p), self.f(O[2], p)), sat)
        elif op0 in ('dp2', 'dp3', 'dp4'):
            n = int(op0[2])
            e = ' + '.join('%s * %s' % (self.f(O[0], k), self.f(O[1], k)) for k in range(n))
            self.emit('{ float _d = %s; %s }' % (e, ' '.join('%s.%s = asuint(%s);' % (d, c, 'saturate(_d)' if sat else '_d') for c in mask)))
        elif op0 == 'mov':
            if O[0].neg or O[0].abs or sat: self.putv(d, mask, lambda p: self.f(O[0], p), sat)
            else: self.putv(d, mask, lambda p: O[0].comp(p), isfloat=False)
        elif op0 == 'movc':
            self.putv(d, mask, lambda p: '(%s != 0u) ? %s : %s' % (O[0].comp(p), self.selsrc(O[1], p), self.selsrc(O[2], p)), isfloat=False)
        elif op0 in ('lt', 'ge', 'eq', 'ne'):
            sym = {'lt': '<', 'ge': '>=', 'eq': '==', 'ne': '!='}[op0]
            self.putv(d, mask, lambda p: '((%s %s %s) ? 0xffffffffu : 0u)' % (self.f(O[0], p), sym, self.f(O[1], p)), isfloat=False)
        elif op0 in ('ieq', 'ine', 'ilt', 'ige', 'ult', 'uge'):
            sym = {'ieq': '==', 'ine': '!=', 'ilt': '<', 'ige': '>=', 'ult': '<', 'uge': '>='}[op0]
            g = self.i if op0 in ('ilt', 'ige') else self.u
            self.putv(d, mask, lambda p: '((%s %s %s) ? 0xffffffffu : 0u)' % (g(O[0], p), sym, g(O[1], p)), isfloat=False)
        elif op0 in ('and', 'or', 'xor'):
            sym = {'and': '&', 'or': '|', 'xor': '^'}[op0]
            self.putv(d, mask, lambda p: '(%s %s %s)' % (self.u(O[0], p), sym, self.u(O[1], p)), isfloat=False)
        elif op0 == 'iadd':
            self.putv(d, mask, lambda p: '(%s + %s)' % (self.u(O[0], p), self.u(O[1], p)), isfloat=False)
        elif op0 == 'imad':
            self.putv(d, mask, lambda p: '(%s * %s + %s)' % (self.u(O[0], p), self.u(O[1], p), self.u(O[2], p)), isfloat=False)
        elif op0 in ('imax', 'imin'):
            fn = op0[1:]
            self.putv(d, mask, lambda p: 'asuint(%s(%s, %s))' % (fn, self.i(O[0], p), self.i(O[1], p)), isfloat=False)
        elif op0 in ('umax', 'umin'):
            fn = op0[1:]
            self.putv(d, mask, lambda p: '%s(%s, %s)' % (fn, self.u(O[0], p), self.u(O[1], p)), isfloat=False)
        elif op0 == 'ishl':
            self.putv(d, mask, lambda p: '(%s << (%s & 31u))' % (self.u(O[0], p), self.u(O[1], p)), isfloat=False)
        elif op0 == 'ushr':
            self.putv(d, mask, lambda p: '(%s >> (%s & 31u))' % (self.u(O[0], p), self.u(O[1], p)), isfloat=False)
        elif op0 == 'ishr':
            self.putv(d, mask, lambda p: 'asuint(%s >> (%s & 31u))' % (self.i(O[0], p), self.u(O[1], p)), isfloat=False)
        elif op0 == 'ftoi':
            self.putv(d, mask, lambda p: 'asuint((int)%s)' % self.f(O[0], p), isfloat=False)
        elif op0 == 'ftou':
            self.putv(d, mask, lambda p: '(uint)%s' % self.f(O[0], p), isfloat=False)
        elif op0 == 'utof':
            self.putv(d, mask, lambda p: '(float)%s' % self.u(O[0], p), sat)
        elif op0 == 'itof':
            self.putv(d, mask, lambda p: '(float)%s' % self.i(O[0], p), sat)
        elif op0 == 'ubfe':   # ubfe dst, width, offset, src
            self.putv(d, mask, lambda p: 'PESUbfe(%s, %s, %s)' % (self.u(O[0], p), self.u(O[1], p), self.u(O[2], p)), isfloat=False)
        elif op0 == 'bfi':    # bfi dst, width, offset, insert, base
            self.putv(d, mask, lambda p: 'PESBfi(%s, %s, %s, %s)' % (self.u(O[0], p), self.u(O[1], p), self.u(O[2], p), self.u(O[3], p)), isfloat=False)
        elif op0 == 'udiv':   # udiv q, r, a, b
            q, qm = self.dst(ops[0]); r_, rm = self.dst(ops[1]); A = Op(ops[2]); B = Op(ops[3])
            tmp = []
            for p, c in enumerate(C):
                if q and c in qm: tmp.append('%s.%s = (%s / %s);' % ('_q', c, self.u(A, p), self.u(B, p)))
                if r_ and c in rm: tmp.append('%s.%s = (%s %% %s);' % ('_r', c, self.u(A, p), self.u(B, p)))
            s = '{ uint4 _q = 0, _r = 0; %s ' % ' '.join(tmp)
            if q: s += ' '.join('%s.%s = _q.%s;' % (q, c, c) for c in qm) + ' '
            if r_: s += ' '.join('%s.%s = _r.%s;' % (r_, c, c) for c in rm)
            self.emit(s + ' }')
        elif op0 in ('sqrt', 'rsq', 'frc', 'exp', 'log', 'round_ne', 'round_ni', 'round_pi', 'round_z'):
            fn = {'sqrt': 'sqrt', 'rsq': 'rsqrt', 'frc': 'frac', 'exp': 'exp2', 'log': 'log2', 'round_ne': 'round',
                  'round_ni': 'floor', 'round_pi': 'ceil', 'round_z': 'trunc'}[op0]
            self.putv(d, mask, lambda p: '%s(%s)' % (fn, self.f(O[0], p)), sat)
        elif op0 == 'if_nz':
            self.emit('if (%s != 0u) {' % Op(ops[0]).comp(0)); self.ind += 1
        elif op0 == 'if_z':
            self.emit('if (%s == 0u) {' % Op(ops[0]).comp(0)); self.ind += 1
        elif op0 == 'else':
            self.ind -= 1; self.emit('} else {'); self.ind += 1
        elif op0 == 'endif':
            self.ind -= 1; self.emit('}')
        elif op0 == 'ret':
            self.emit('PES_OUT return;')
        else:
            raise SystemExit('instruccion no soportada: ' + line)

    def selsrc(self, o, p):
        if o.neg or o.abs: return 'asuint(%s)' % self.f(o, p)
        return o.comp(p)

    def ld_structured(self, op, ops):
        stride = int(re.search(r'stride=(\d+)', op).group(1))
        d, mask = self.dst(ops[0]); idx = Op(ops[1]); off = Op(ops[2]); t = Op(ops[3])
        tn = int(t.name[1:])
        self.structs[tn] = stride
        if off.kind != 'lit': raise SystemExit('ld_structured con offset en registro')
        base = off.vals[0] // 4
        tmp = []
        for p, c in enumerate(C):
            if c not in mask: continue
            sc = t.swz[p] if len(t.swz) == 4 else t.swz[min(p, len(t.swz) - 1)]
            tmp.append((c, 't%d[%s].v[%d]' % (tn, idx.comp(0), base + C.index(sc))))
        self.emit('{ uint4 _t = %s; %s %s = _t; }' % (d, ' '.join('_t.%s = %s;' % (c, e) for c, e in tmp), d))

def parse_sig(lines, title):
    out = []; on = False
    for l in lines:
        if l.startswith('// ' + title): on = True; continue
        if on:
            m = re.match(r'^//\s+(\w+)\s+(\d+)\s+([xyzw]+)\s+(\d+)\s+(\w+)\s+(\w+)', l)
            if m: out.append(m.groups()); continue
            if l.strip() in ('//',) and out: break
    return out

def main():
    a = sys.argv
    src = open(a[1], encoding='cp1252', errors='replace').read()
    pro = a[a.index('--prologo') + 1] if '--prologo' in a else None
    fn = a[a.index('--funcion') + 1] if '--funcion' in a else None
    t = Tr(src)
    lines = src.split('\n')
    ins = parse_sig(lines, 'Input signature'); outs = parse_sig(lines, 'Output signature')
    # nombres de los recursos (para el prologo)
    resname = {}
    for m in re.finditer(r'^//\s+(\w+)\s+texture\s+struct\s+r/o\s+t(\d+)', src, re.M): resname[m.group(1)] = int(m.group(2))
    started = False
    for l in lines:
        if re.match(r'^vs_5_0', l): started = True; continue
        if not started or not l.strip() or l.strip().startswith('//'): continue
        s = l.strip()
        if s.startswith('dcl_constantbuffer'):
            m = re.search(r'CB(\d+)\[(\d+)\]', s); t.cbs[int(m.group(1))] = int(m.group(2))
        elif s.startswith('dcl_resource_structured'):
            m = re.search(r't(\d+), (\d+)', s); t.structs[int(m.group(1))] = int(m.group(2))
        elif s.startswith('dcl_temps'):
            t.temps = int(s.split()[1])
        elif s.startswith('dcl_'):
            pass
        else:
            t.tr(s)
    h = ['// generado por tools/crowd/dxbc2hlsl_bits.py (traduccion exacta a nivel de bits del VS del juego)']
    for n, size in sorted(t.cbs.items()):
        h.append('cbuffer cb%d : register(b%d) { uint4 cb%d[%d]; }' % (n, n, n, size))
    for n, stride in sorted(t.structs.items()):
        h.append('struct PESS%d_%d { uint v[%d]; };' % (n, stride, stride // 4))
        h.append('StructuredBuffer<PESS%d_%d> t%d : register(t%d);' % (n, stride, n, n))
    h.append('uint PESUbfe(uint w, uint o, uint s) { w &= 31u; o &= 31u; if (w == 0u) return 0u; if (w + o < 32u) return (s << (32u - w - o)) >> (32u - w); return s >> o; }')
    h.append('uint PESBfi(uint w, uint o, uint ins, uint b) { w &= 31u; o &= 31u; uint m = (((1u << w) - 1u) << o); return ((ins << o) & m) | (b & ~m); }')
    h.append('Texture1D<float4> IniParams : register(t120);')
    if pro: h.append('#include "%s"' % pro)
    params = []; pre = []
    vreg = {}
    for (name, idx, mask, reg, sv, fmt) in ins:
        n = len(mask); ty = ('uint' if fmt == 'uint' else 'int' if fmt == 'int' else 'float') + (str(n) if n > 1 else '')
        pn = 'i_%s%s' % (name.lower(), idx)
        sem = 'SV_InstanceID' if name == 'SV_InstanceID' else ('SV_VertexID' if name == 'SV_VertexID' else '%s%s' % (name, idx))
        params.append('%s %s : %s' % (ty, pn, sem))
        conv = 'asuint(%s)' % pn if fmt == 'float' else ('asuint(%s)' % pn if fmt == 'int' else pn)
        pre.append('v%s.%s = %s;' % (reg, mask, conv)); vreg[name] = int(reg)
    nv = max(int(x[3]) for x in ins) + 1
    for (name, idx, mask, reg, sv, fmt) in outs:
        n = len(mask); ty = 'float' + (str(n) if n > 1 else '')
        sem = 'SV_Position0' if name == 'SV_Position' else '%s%s' % (name, idx)
        params.append('out %s O%s : %s' % (ty, reg, sem))
    outw = ['O%s = asfloat(o%s.%s);' % (reg, reg, mask) for (name, idx, mask, reg, sv, fmt) in outs]
    nout = max(int(x[3]) for x in outs) + 1
    body = []
    body.append('#define PES_OUT %s' % ' '.join(outw))
    body.append('void main(\n  ' + ',\n  '.join(params) + ')')
    body.append('{')
    body.append('  uint4 %s;' % ', '.join('r%d = 0' % k for k in range(max(t.temps, 1))))
    body.append('  uint4 %s;' % ', '.join('v%d = 0' % k for k in range(nv)))
    body.append('  uint4 %s;' % ', '.join('o%d = 0' % k for k in range(nout)))
    body += ['  ' + p for p in pre]
    if pro and fn:
        ti = resname['instanceIndexBuf']
        body.append('  uint pesIdx = t%d[(uint)asfloat(cb5[9].x) + v%d.x].v[0];' % (ti, vreg['SV_InstanceID']))
        body.append('  %s(v%d, v%d, v%d, v%d, pesIdx, v%d, v%d);' % (fn, vreg['POSITION'], vreg['NORMAL'], vreg['TANGENT'], vreg['COLOR'], vreg['BLENDINDICES'], vreg['BLENDWEIGHT']))
    body += t.body
    if not body[-1].strip().endswith('return;'): body.append('  PES_OUT')
    body.append('}')
    open(a[2], 'w').write('\n'.join(h + [''] + body) + '\n')
    print(a[2], len(t.body), 'lineas')

if __name__ == '__main__': main()
