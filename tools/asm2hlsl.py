#!/usr/bin/env python3
"""PES21 FX - traductor mecanico de ensamblador DXBC (shader model 5, el que escribe el catalogo del mod en
catalog\\asm\\<hash>-p.txt / -v.txt) a HLSL compilable, instruccion por instruccion, al estilo de 3DMigoto:

  - cada registro temporal es un float4; las instrucciones enteras trabajan con los bits (asint/asuint/asfloat)
  - las comparaciones dejan -1.0 / 0.0 (bits distintos de 0 = verdadero), como 3DMigoto
  - mascara de destino + swizzle de origen: el componente p del destino toma el componente swizzle[p] del origen
  - entradas/salidas con la misma firma que el original (los elementos que comparten registro se arman a mano)

Uso: asm2hlsl.py <asm.txt> <salida.hlsl> [--entry main]
El resultado es la base "identica al juego" sobre la que se agregan las mejoras del mod.
"""
import re, sys

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

    # ---------------------------------------------------------------- operandos
    def literal(self, s, intctx, uns=False):
        vals = [v.strip() for v in s[2:-1].split(',')]
        if len(vals) == 1: vals = vals * 4
        if intctx:
            # en contexto entero, los literales son enteros (o bits en hexadecimal)
            def iv(v):
                if v.lower().startswith('0x'): return v + 'U'
                return str(int(float(v))) + ('U' if uns else '')
            if uns or any(v.lower().startswith('0x') for v in vals):
                e = 'uint4(%s)' % ', '.join(iv(v) if not (uns and v.startswith('-')) else '(uint)(%s)' % iv(v) for v in vals)
                return e if uns else 'asint(%s)' % e
            return 'int4(%s)' % ', '.join(iv(v) for v in vals)
        def fv(v):
            if v.lower().startswith('0x'): return 'asfloat(%sU)' % v
            return EXACT.get(v.lstrip('-') , v.lstrip('-')) if not v.startswith('-') else '-' + EXACT.get(v[1:], v[1:])
        return 'float4(%s)' % ', '.join(fv(v) for v in vals)

    def src(self, op, intctx=False, uns=False):
        """Operando de origen como expresion float4 (o int4/uint4 en contexto entero)."""
        neg = False; ab = False; s = op.strip()
        if s.startswith('-'): neg = True; s = s[1:].strip()
        if s.startswith('|') and s.endswith('|'): ab = True; s = s[1:-1]
        elif s.startswith('|'):
            # |r0.x| con swizzle afuera no ocurre en fxc; por las dudas
            ab = True; s = s.strip('|')
        if s.startswith('l('):
            e = self.literal(s, intctx, uns)
        else:
            m = re.match(r'^(.*?)(?:\.([xyzw]{1,4}))?$', s)
            base, swz = m.group(1), m.group(2) or 'xyzw'
            if len(swz) == 1: swz = swz * 4
            elif len(swz) < 4: swz = swz + swz[-1] * (4 - len(swz))
            base = self.base(base)
            e = '%s.%s' % (base, swz)
            if intctx: e = ('asuint(%s)' if uns else 'asint(%s)') % e
        if ab: e = 'abs(%s)' % e
        if neg: e = '-(%s)' % e
        return e

    def base(self, b):
        b = b.strip()
        m = re.match(r'^(cb\d+|icb|x\d+)\[(.*)\]$', b)
        if m:
            name, idx = m.groups()
            idx = re.sub(r'([rx]\d+)\.([xyzw])', lambda mm: 'asint(%s.%s)' % (mm.group(1), mm.group(2)), idx)
            return '%s[%s]' % (name, idx)
        return b

    def dst(self, op):
        s = op.strip()
        m = re.match(r'^(.*?)\.([xyzw]{1,4})$', s)
        if not m: return self.base(s), 'xyzw'
        return self.base(m.group(1)), m.group(2)

    def emit(self, s):
        self.body.append('  ' * self.ind + s)

    def assign(self, dop, expr4, sat=False, kind='f'):
        """expr4: expresion de 4 componentes (float4 / int4 / uint4 segun kind)."""
        d, mask = self.dst(dop)
        if kind == 'i' or kind == 'u':
            expr4 = 'asfloat(%s)' % expr4
        if sat: expr4 = 'saturate(%s)' % expr4
        self.emit('%s.%s = (%s).%s;' % (d, mask, expr4, mask))

    # ---------------------------------------------------------------- instrucciones
    def tr_instr(self, line):
        raw = line.strip()
        if not raw or raw.startswith('//'): return
        m = re.match(r'^([a-z_0-9]+(?:\([^)]*\))*)\s*(.*)$', raw)
        opfull, rest = m.group(1), m.group(2)
        sat = '_sat' in opfull
        op = opfull.replace('_sat', '')
        ops = split_operands(rest) if rest else []
        F = lambda i: self.src(ops[i])
        I = lambda i: self.src(ops[i], True)
        U = lambda i: self.src(ops[i], True, True)

        simple2 = {'add': '(%s + %s)', 'mul': '(%s * %s)', 'div': '(%s / %s)', 'min': 'min(%s, %s)', 'max': 'max(%s, %s)'}
        simple1 = {'mov': '%s', 'sqrt': 'sqrt(%s)', 'rsq': 'rsqrt(%s)', 'exp': 'exp2(%s)', 'log': 'log2(%s)', 'frc': 'frac(%s)',
                   'round_ni': 'floor(%s)', 'round_pi': 'ceil(%s)', 'round_z': 'trunc(%s)', 'round_ne': 'round(%s)', 'rcp': 'rcp(%s)',
                   'deriv_rtx_fine': 'ddx_fine(%s)', 'deriv_rty_fine': 'ddy_fine(%s)', 'deriv_rtx_coarse': 'ddx_coarse(%s)',
                   'deriv_rty_coarse': 'ddy_coarse(%s)', 'deriv_rtx': 'ddx(%s)', 'deriv_rty': 'ddy(%s)'}
        cmpf = {'lt': '<', 'ge': '>=', 'eq': '==', 'ne': '!='}
        cmpi = {'ilt': ('<', 0), 'ige': ('>=', 0), 'ieq': ('==', 0), 'ine': ('!=', 0), 'ult': ('<', 1), 'uge': ('>=', 1)}
        if op in simple2:
            self.assign(ops[0], simple2[op] % (F(1), F(2)), sat)
        elif op in simple1:
            self.assign(ops[0], simple1[op] % F(1), sat)
        elif op == 'mad':
            self.assign(ops[0], '(%s * %s + %s)' % (F(1), F(2), F(3)), sat)
        elif op in ('dp2', 'dp3', 'dp4'):
            n = int(op[2]); sw = COMP[:n]
            a = F(1); b = F(2)
            self.assign(ops[0], '(dot((%s).%s, (%s).%s)).xxxx' % (a, sw, b, sw), sat)
        elif op in cmpf:
            self.assign(ops[0], '((%s %s %s) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))' % (F(1), cmpf[op], F(2)))
        elif op in cmpi:
            o, uns = cmpi[op]
            a = U(1) if uns else I(1); b = U(2) if uns else I(2)
            self.assign(ops[0], '((%s %s %s) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))' % (a, o, b))
        elif op == 'movc':
            self.assign(ops[0], '((asuint(%s) != 0) ? %s : %s)' % (F(1), F(2), F(3)), sat)
        elif op == 'and':
            self.assign(ops[0], '(%s & %s)' % (U(1), U(2)), kind='u')
        elif op == 'or':
            self.assign(ops[0], '(%s | %s)' % (U(1), U(2)), kind='u')
        elif op == 'xor':
            self.assign(ops[0], '(%s ^ %s)' % (U(1), U(2)), kind='u')
        elif op == 'not':
            self.assign(ops[0], '(~%s)' % U(1), kind='u')
        elif op == 'iadd':
            self.assign(ops[0], '(%s + %s)' % (I(1), I(2)), kind='i')
        elif op == 'imul':   # imul null, dst, a, b
            self.assign(ops[1], '(%s * %s)' % (I(2), I(3)), kind='i')
        elif op == 'imax':
            self.assign(ops[0], 'max(%s, %s)' % (I(1), I(2)), kind='i')
        elif op == 'imin':
            self.assign(ops[0], 'min(%s, %s)' % (I(1), I(2)), kind='i')
        elif op == 'ishl':
            self.assign(ops[0], '(%s << (%s & 31))' % (I(1), U(2)), kind='i')
        elif op == 'ishr':
            self.assign(ops[0], '(%s >> (%s & 31))' % (I(1), U(2)), kind='i')
        elif op == 'ushr':
            self.assign(ops[0], '(%s >> (%s & 31))' % (U(1), U(2)), kind='u')
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
            self.need_bfi = True
        elif op == 'sincos':
            if ops[0] != 'null': self.assign(ops[0], 'sin(%s)' % F(2), sat)
            if ops[1] != 'null': self.assign(ops[1], 'cos(%s)' % F(2), sat)
        elif op.startswith('sample') or op.startswith('ld') or op.startswith('resinfo') or op.startswith('gather'):
            self.tr_sample(op, opfull, ops)
        elif op == 'discard_nz':
            self.emit('if (asuint(%s).x != 0) discard;' % self.src(ops[0]))
        elif op == 'discard_z':
            self.emit('if (asuint(%s).x == 0) discard;' % self.src(ops[0]))
        elif op == 'if_nz':
            self.emit('if (asuint(%s).x != 0) {' % self.src(ops[0])); self.ind += 1
        elif op == 'if_z':
            self.emit('if (asuint(%s).x == 0) {' % self.src(ops[0])); self.ind += 1
        elif op == 'else':
            self.ind -= 1; self.emit('} else {'); self.ind += 1
        elif op == 'endif':
            self.ind -= 1; self.emit('}')
        elif op == 'loop':
            self.emit('[loop] while (true) {'); self.ind += 1
        elif op == 'endloop':
            self.ind -= 1; self.emit('}')
        elif op == 'breakc_nz':
            self.emit('if (asuint(%s).x != 0) break;' % self.src(ops[0]))
        elif op == 'breakc_z':
            self.emit('if (asuint(%s).x == 0) break;' % self.src(ops[0]))
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
        base = op.split('(')[0]
        def resswz(r):
            m = re.match(r'^(t\d+)(?:\.([xyzw]{4}))?$', r.strip())
            return m.group(1), (m.group(2) or 'xyzw')
        if base in ('sample_indexable', 'sample'):
            t, rs = resswz(ops[2]); s = ops[3].strip()
            e = '%s.Sample(%s, (%s).%s)' % (t, s, self.src(ops[1]), COMP[:nc])
            self.assign(ops[0], '(%s).%s' % (e, rs))
        elif base in ('sample_l_indexable', 'sample_l'):
            t, rs = resswz(ops[2]); s = ops[3].strip()
            e = '%s.SampleLevel(%s, (%s).%s, (%s).x)' % (t, s, self.src(ops[1]), COMP[:nc], self.src(ops[4]))
            self.assign(ops[0], '(%s).%s' % (e, rs))
        elif base in ('sample_b_indexable', 'sample_b'):
            t, rs = resswz(ops[2]); s = ops[3].strip()
            e = '%s.SampleBias(%s, (%s).%s, (%s).x)' % (t, s, self.src(ops[1]), COMP[:nc], self.src(ops[4]))
            self.assign(ops[0], '(%s).%s' % (e, rs))
        elif base in ('sample_d_indexable', 'sample_d'):
            t, rs = resswz(ops[2]); s = ops[3].strip()
            e = '%s.SampleGrad(%s, (%s).%s, (%s).%s, (%s).%s)' % (t, s, self.src(ops[1]), COMP[:nc], self.src(ops[4]), COMP[:nc], self.src(ops[5]), COMP[:nc])
            self.assign(ops[0], '(%s).%s' % (e, rs))
        elif base in ('sample_c_indexable', 'sample_c'):
            t, rs = resswz(ops[2]); s = ops[3].strip(); self.cmpsamplers.add(s)
            e = '(%s.SampleCmp(%s, (%s).%s, (%s).x)).xxxx' % (t, s, self.src(ops[1]), COMP[:nc], self.src(ops[4]))
            self.assign(ops[0], e)
        elif base in ('sample_c_lz_indexable', 'sample_c_lz'):
            t, rs = resswz(ops[2]); s = ops[3].strip(); self.cmpsamplers.add(s)
            e = '(%s.SampleCmpLevelZero(%s, (%s).%s, (%s).x)).xxxx' % (t, s, self.src(ops[1]), COMP[:nc], self.src(ops[4]))
            self.assign(ops[0], e)
        elif base in ('ld_indexable', 'ld'):
            t, rs = resswz(ops[2])
            e = '%s.Load(asint(%s).%s)' % (t, self.src(ops[1]), COMP[:nc + 1])
            self.assign(ops[0], '(%s).%s' % (e, rs))
        else:
            raise SystemExit('muestreo no soportado: ' + opfull)

    # ---------------------------------------------------------------- todo el archivo
    def run(self, entry='main'):
        ins = parse_signature(self.lines, 'Input signature')
        outs = parse_signature(self.lines, 'Output signature')
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
            h.append('static const float4 icb[%d] = { %s };' % (len(self.icb), ', '.join('float4(%s)' % ', '.join(v) for v in self.icb)))
        h.append('uint4 PESBfi(uint4 w, uint4 o, uint4 ins, uint4 b) { uint4 m = (((1u << w) - 1u) << o); return ((ins << o) & m) | (b & ~m); }')
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
                if name == 'ff': pre.append('v%d.x = %s ? asfloat(0xffffffffU) : 0.0;' % (reg, pn))
                else: pre.append('v%d.%s = %s;' % (reg, mask, pn))
        out = []
        out.append('// generado por tools/asm2hlsl.py (traduccion mecanica, identica al juego)')
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
    print('ok:', sys.argv[2], len(t.body), 'lineas de codigo')
