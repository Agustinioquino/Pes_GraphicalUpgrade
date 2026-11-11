#!/usr/bin/env python3
"""defaults.py - valores por defecto de las propiedades de reflexion de Fox Engine (PES2021 1.07.02).

Para cada clase de reflexion.tsv (categorias graficas/gameplay de reflect_doc.py):
  1. vtable de la clase: rtti.txt (offset 0) cuyo metodo 0 es (o salta con E9 a) el getter del EntityInfo
     (columna getter de reflexion_clases.tsv). Eso resuelve los nombres C++ distintos del nombre Fox
     (GrShadowSettings = fox::grx::ShadowSettings, PesExposureSettings = cobra::rd::ExposureSettings, ...).
  2. funciones del C de Ghidra (export/c/*.c) que asignan esa vtable (`*X = &ns__Clase__vftable;`).
  3. emula, en orden, las escrituras `*(T *)(X + N) = valor;` / `X[N] = valor;` / `*(T *)((longlong)X + N)`,
     con alias (`p = X + N`), locales con valor conocido, lecturas de globales (_DAT_/fRam/...) desde el volcado,
     y entra (profundidad 2) en las funciones llamadas con `X + N` como 1er argumento (ctors de subobjetos).
  4. elige el candidato que mas propiedades cubre (el ctor por defecto; el ctor de copia/carga no escribe
     constantes) y decodifica cada propiedad segun su tipo y offset.
Cruza con fox2_txt/Default.txt (valores reales del .fox2). Salida: defaults.tsv y defaults.md.

Uso: PES_DUMP=... python3 defaults.py [out_dir]
"""
import os, re, sys, csv, glob, struct
from collections import defaultdict, OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
_argv = sys.argv
sys.argv = [sys.argv[0]]
import reflect_doc as RD  # cat_of, meaning
sys.argv = _argv
from dumpmap import read, sec_of

RE = "/home/claude/re"
OUT = sys.argv[1] if len(sys.argv) > 1 else RE + "/out"
FOX2 = "/home/claude/cpk/fox2_txt/Default.txt"
CATS = ['luz', 'cielo_clima', 'sombras', 'postprocesado', 'camara', 'cesped_estadio', 'mallas_modelos',
        'huesos_animacion', 'fisica_ropa', 'texturas_materiales', 'gameplay']

TSIZE = {"Int8": 1, "UInt8": 1, "Bool": 1, "Int16": 2, "UInt16": 2, "Int32": 4, "UInt32": 4, "Float": 4,
         "Int64": 8, "UInt64": 8, "Double": 8, "Vector3": 16, "Vector4": 16, "Quat": 16, "Color": 16,
         "WideVector3": 16, "Matrix3": 48, "Matrix4": 64, "String": 8, "Path": 8, "EntityPtr": 8,
         "EntityHandle": 8, "EntityLink": 0x40, "FilePtr": 0x20, "PropertyInfo": 8}
CSIZE = {"undefined": 1, "undefined1": 1, "char": 1, "byte": 1, "bool": 1, "uchar": 1, "sbyte": 1,
         "undefined2": 2, "short": 2, "ushort": 2, "wchar_t": 2, "word": 2,
         "undefined3": 3, "uint3": 3, "undefined4": 4, "int": 4, "uint": 4, "float": 4, "dword": 4,
         "undefined5": 5, "uint5": 5, "undefined6": 6, "uint6": 6, "undefined7": 7,
         "undefined8": 8, "longlong": 8, "ulonglong": 8, "double": 8, "qword": 8, "code": 8, "pointer": 8,
         "undefined16": 16, "float10": 10}


def tsize(t):
    t = t.strip()
    if t.endswith("*"):
        return 8
    return CSIZE.get(t, 8)


# ---------------------------------------------------------------- datos de entrada
rows = list(csv.DictReader(open(RE + "/out/reflexion.tsv"), delimiter="\t"))
clsrows = list(csv.DictReader(open(RE + "/out/reflexion_clases.tsv"), delimiter="\t"))
target = OrderedDict()
for r in rows:
    if RD.cat_of(r["class"]) in CATS:
        target.setdefault(r["class"], []).append(r)
getter = {}
for c in clsrows:
    if c["getter"]:
        getter.setdefault(int(c["getter"], 16), c["class"])
info_of = {c["class"]: c for c in clsrows}


def follow_jmp(a):
    for _ in range(3):
        b = read(a, 5)
        if b and b[0] == 0xE9:
            a = a + 5 + struct.unpack("<i", b[1:5])[0]
        else:
            break
    return a


vt_of = defaultdict(list)  # clase fox -> [(vtable, nombre c++)]
for l in open(RE + "/rtti.txt"):
    p = l.split()
    if len(p) < 6 or p[0] != "V" or p[3] != "0":
        continue
    m0 = int(p[5], 16)
    g = follow_jmp(m0)
    c = getter.get(g) or getter.get(m0)
    if c:
        vt_of[c].append((int(p[1], 16), p[2]))


def sym_of(cpp):
    return cpp.replace("::", "__") + "__vftable"


want = {}  # simbolo -> clase
for c in target:
    for va, cpp in vt_of.get(c, []):
        want[sym_of(cpp)] = c
        want["PTR_FUN_%x" % va] = c
        want["PTR_LAB_%x" % va] = c
        want["PTR_%x" % va] = c

# ---------------------------------------------------------------- indice del C
HDR = re.compile(r"^// ==== (\S+) @ ([0-9a-f]+)\s*$", re.M)
func_loc = {}       # addr -> (archivo, inicio, fin) en texto
texts = {}
cands = defaultdict(list)  # clase -> [(addr, nombre, archivo)]
symrx = re.compile(r"=\s*(?:\([^)]*\))?\s*&(" + "|".join(re.escape(s) for s in sorted(want, key=len, reverse=True))
                   + r")\s*;") if want else None
for fn in sorted(glob.glob(RE + "/export/c/c_*.c")):
    t = open(fn, errors="replace").read()
    texts[fn] = t
    hs = list(HDR.finditer(t))
    for i, h in enumerate(hs):
        e = hs[i + 1].start() if i + 1 < len(hs) else len(t)
        func_loc[int(h.group(2), 16)] = (fn, h.start(), e, h.group(1))
    for m in symrx.finditer(t):
        # funcion que contiene la asignacion
        lo, hi = 0, len(hs) - 1
        pos = m.start()
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if hs[mid].start() <= pos:
                lo = mid
            else:
                hi = mid - 1
        a = int(hs[lo].group(2), 16)
        c = want[m.group(1)]
        if a not in [x[0] for x in cands[c]]:
            cands[c].append((a, hs[lo].group(1), fn))


def func_text(a):
    fn, s, e, name = func_loc[a]
    return texts[fn][s:e], fn, texts[fn].count("\n", 0, s) + 1


# ---------------------------------------------------------------- emulador de escrituras
NUM = r"(?:0x[0-9a-fA-F]+|\d+)"


def ival(s):
    s = s.strip()
    neg = s.startswith("-")
    if neg:
        s = s[1:]
    v = int(s, 16) if s.lower().startswith("0x") else int(s)
    return -v if neg else v


def parse_func(txt):
    """-> (decls{var:type}, statements[(lineno_rel, text)])"""
    i = txt.find("{")
    sig = txt[:i]
    body = txt[i + 1:]
    decls = {}
    sigp = sig[sig.find("(", sig.find("\n", 5)) + 1:] if "(" in sig else ""
    for m in re.finditer(r"([A-Za-z_][\w ]*?\s*\**)\s*(param_\d+)", sigp):
        decls[m.group(2)] = m.group(1).strip()
    stmts = []
    cur = ""
    curln = 0
    base_ln = txt[:i].count("\n")
    for k, line in enumerate(body.split("\n")):
        s = line.strip()
        if not s:
            continue
        if not cur:
            curln = base_ln + k
        cur = (cur + " " + s) if cur else s
        if cur.endswith(";") or cur.endswith("{") or cur.endswith("}") or cur.startswith("//"):
            md = re.match(r"^([A-Za-z_][\w]*(?: [\w]+)*?\s*\**)\s*(\w+)(\s*\[\d+\])?;$", cur)
            if md and md.group(1).split()[0] in CSIZE | {"code": 8, "LPVOID": 8} and "=" not in cur:
                decls[md.group(2)] = md.group(1).strip() + (" [arr]" if md.group(3) else "")
            else:
                stmts.append((curln, cur))
            cur = ""
    return decls, stmts


def gread(addr, n, fl):
    b = read(addr, n)
    if b is None or len(b) < n:
        return None
    if fl:
        return struct.unpack("<f", b[:4])[0] if n == 4 else struct.unpack("<d", b[:8])[0]
    return int.from_bytes(b, "little")


GLOB = re.compile(r"\b_?(?:DAT|UNK)_([0-9a-f]{9})\b|\b([fdu]|uVar)?Ram0*([0-9a-f]{9})\b")


def evalx(expr, locs, fl, size):
    """valor numerico o None. fl: contexto float."""
    e = expr.strip().rstrip(";")
    e = re.sub(r"\((?:undefined\d?|u?longlong|u?int|float|double|char|byte|bool|u?short|code|uint\d|\w+) \*+\)\s*0x0\b", "0", e)
    e = re.sub(r"\((?:undefined\d?|u?longlong|u?int|char|byte|bool|u?short|uint\d)\)", "", e)
    e = re.sub(r"\((?:float|double)\)", "", e)
    if re.fullmatch(r"true", e):
        return 1
    if re.fullmatch(r"false", e):
        return 0
    m = re.fullmatch(r"'\\x([0-9a-fA-F]{2})'", e)
    if m:
        return int(m.group(1), 16)
    if e == "'\\0'":
        return 0
    m = re.fullmatch(r"'(.)'", e)
    if m:
        return ord(m.group(1))
    m = re.fullmatch(r"CONCAT44\((.+),(.+)\)", e)
    if m:
        hi = evalx(m.group(1), locs, False, 4)
        lo = evalx(m.group(2), locs, False, 4)
        if hi is None or lo is None:
            return None
        return ((hi & 0xffffffff) << 32) | (lo & 0xffffffff)

    def gsub(m):
        a = int(m.group(1) or m.group(3), 16)
        pre = m.group(2)
        isf = fl or pre == "f"
        n = 4 if (pre == "f" or (isf and size == 4)) else (8 if pre == "d" else (size if size in (1, 2, 4, 8) else 4))
        if pre == "d":
            isf = True
        v = gread(a, n, isf)
        if v is None:
            raise ValueError
        return repr(v)
    try:
        e2 = GLOB.sub(gsub, e)
    except ValueError:
        return None
    # locales
    def lsub(m):
        v = locs.get(m.group(0))
        if v is None:
            raise ValueError
        return repr(v)
    try:
        e2 = re.sub(r"\b[a-z][A-Za-z]*Var\d+\b|\bauVar\d+\b|\b[a-z]Stack_\w+\b|\blocal_\w+\b", lsub, e2)
    except ValueError:
        return None
    if not re.fullmatch(r"[0-9a-fA-FxX.eE+\-*/() ]+", e2):
        return None
    try:
        v = eval(e2, {"__builtins__": {}})
    except Exception:
        return None
    return v


def tobytes(v, ctype, size):
    if v is None:
        return None
    if ctype == "float" and size == 4:
        return struct.pack("<f", float(v))
    if ctype == "double":
        return struct.pack("<d", float(v))
    if isinstance(v, float):
        if size == 4:
            return struct.pack("<f", v)
        if size == 8:
            return struct.pack("<d", v)
        v = int(v)
    return (int(v) & ((1 << (8 * size)) - 1)).to_bytes(size, "little")


class Emu:
    def __init__(self):
        self.mem = {}     # off -> (byte|None, evid)
        self.calls = []   # (off, callee, evid)
        self.nconst = 0

    def put(self, off, b, evid, size=None):
        if b is None:
            for i in range(size):
                self.mem[off + i] = (None, evid)
        else:
            for i, x in enumerate(b):
                self.mem[off + i] = (x, evid)
            self.nconst += 1


def lhs_resolve(lhs, bases, decls):
    """-> (offset, ctype, size) o None. bases: var -> (off_base, elem_type)"""
    lhs = lhs.strip()
    m = re.fullmatch(r"\*\(([\w ]+?\s*\**)\s*\*\)\s*\((.+)\)", lhs)
    if m:
        ct = m.group(1).strip()
        r = addr_resolve(m.group(2), bases, decls)
        if r is None:
            return None
        return r, ct, tsize(ct)
    m = re.fullmatch(r"\*\(([\w ]+?\s*\**)\s*\*\)\s*(\w+)", lhs)
    if m and m.group(2) in bases:
        ct = m.group(1).strip()
        return bases[m.group(2)][0], ct, tsize(ct)
    m = re.fullmatch(r"\*(\w+)", lhs)
    if m and m.group(1) in bases:
        o, et = bases[m.group(1)]
        return o, et, tsize(et)
    m = re.fullmatch(r"(\w+)\[(" + NUM + r")\]", lhs)
    if m and m.group(1) in bases:
        o, et = bases[m.group(1)]
        return o + ival(m.group(2)) * tsize(et), et, tsize(et)
    m = re.fullmatch(r"\*\(([\w ]+?\s*\**)\s*\*\)\s*&?\(?(\w+)->field\d*_0x([0-9a-f]+)\)?", lhs)
    if m and m.group(2) in bases:
        ct = m.group(1).strip()
        return bases[m.group(2)][0] + int(m.group(3), 16), ct, tsize(ct)
    m = re.fullmatch(r"(\w+)->field\d*_0x([0-9a-f]+)", lhs)
    if m and m.group(1) in bases:
        return bases[m.group(1)][0] + int(m.group(2), 16), "undefined4", None
    return None


def addr_resolve(a, bases, decls):
    a = a.strip()
    while a.startswith("(") and a.endswith(")") and a.count("(") == 1:
        a = a[1:-1].strip()
    m = re.fullmatch(r"\(\s*(longlong|ulonglong|[\w ]+\s*\*+)\s*\)\s*(\w+)\s*([+-])\s*(" + NUM + r")", a)
    if m and m.group(2) in bases:
        cast = m.group(1).strip()
        el = 1 if cast in ("longlong", "ulonglong") else tsize(cast[:-1].strip())
        sgn = -1 if m.group(3) == "-" else 1
        return bases[m.group(2)][0] + sgn * ival(m.group(4)) * el
    m = re.fullmatch(r"(\w+)\s*([+-])\s*(" + NUM + r")", a)
    if m and m.group(1) in bases:
        o, et = bases[m.group(1)]
        sgn = -1 if m.group(2) == "-" else 1
        return o + sgn * ival(m.group(3)) * (tsize(et) if et != "#int" else 1)
    m = re.fullmatch(r"\(\s*(longlong|ulonglong|[\w ]+\s*\*+)\s*\)\s*(\w+)", a)
    if m and m.group(2) in bases:
        return bases[m.group(2)][0]
    if a in bases:
        return bases[a][0]
    return None


def elem_of(t):
    """tipo declarado -> tipo de elemento para aritmetica de punteros ('#int' = aritmetica en bytes)"""
    t = t.replace(" [arr]", "").strip()
    if t.endswith("*"):
        return t[:-1].strip()
    return "#int"


SKIPCALL = set()


def emulate(addr, emu, base_var=None, base_off=0, sym=None, depth=0, ln_base=None):
    if addr not in func_loc:
        return
    txt, fn, ln0 = func_text(addr)
    decls, stmts = parse_func(txt)
    bases = {}
    if base_var:
        bases[base_var] = (base_off, elem_of(decls.get(base_var, "longlong")))
    locs = {}
    started = sym is None
    if sym and not base_var:
        for ln, s in stmts:
            m = re.match(r"^\*?\(?(?:\([^)]*\)\s*)?(\w+)(?:\[0\])?\)?\s*=\s*(?:\([^)]*\))?\s*&" + re.escape(sym) + r"\s*;", s)
            if m:
                base_var = m.group(1)
                bases = {base_var: (0, elem_of(decls.get(base_var, "undefined8 *")))}
                started = True
                break
        if not base_var:
            return
    for ln, s in stmts:
        evid = "%x:%s:%d" % (addr, os.path.basename(fn), ln0 + ln)
        if sym and not started:
            m = re.match(r"^\*?\(?(?:\([^)]*\)\s*)?(\w+)(?:\[0\])?\)?\s*=\s*(?:\([^)]*\))?\s*&" + re.escape(sym) + r"\s*;", s)
            if m:
                bv = m.group(1)
                bases = {bv: (0, elem_of(decls.get(bv, "undefined8 *")))}
                base_var = bv
                started = True
                continue
        elif sym and re.search(r"&" + re.escape(sym) + r"\s*;", s):
            continue
        m = re.fullmatch(r"(\w+)\s*=\s*(.+);", s)
        if m:
            var, rhs = m.group(1), m.group(2).strip()
            if started and var != base_var and not rhs.startswith("*"):
                mc = re.match(r"^\(([\w ]+\s*\*+)\)\s*(.+)$", rhs)
                core = mc.group(2) if mc else rhs
                r = addr_resolve(core, bases, decls)
                if r is not None:
                    bases[var] = (r, elem_of(decls.get(var, mc.group(1) if mc else "undefined8 *")))
                    continue
            if var == base_var:
                continue
            bases.pop(var, None) if started and var in bases else None
            dt = decls.get(var, "")
            v = selfread(rhs, bases, decls, emu, dt or "undefined8", tsize(dt) if dt else 8) if started else None
            if v is None:
                v = evalx(rhs, locs, dt.startswith(("float", "double")), tsize(dt) if dt else 8)
            if v is not None:
                locs[var] = v
            else:
                locs.pop(var, None)
            continue
        if not started:
            continue
        # escritura
        m = re.fullmatch(r"(.+?)\s*=\s*(.+);", s)
        if m and not re.search(r"[=!<>]=|^\s*(if|while|for|return)\b", m.group(1)):
            r = lhs_resolve(m.group(1), bases, decls)
            if r is not None:
                off, ct, sz = r
                if sz is None:
                    sz = 4
                rhs = m.group(2)
                fl = ct.startswith("float") or ct.startswith("double")
                v = selfread(rhs, bases, decls, emu, ct, sz)
                if v is None:
                    v = evalx(rhs, locs, fl, sz)
                if v is None:
                    mm = re.fullmatch(r"\s*(?:\([^)]*\)\s*)?&(\w+)\s*", rhs)
                    if mm:
                        emu.put(off, None, evid + " = &" + mm.group(1), sz)
                    else:
                        emu.put(off, None, evid + " = " + rhs.strip()[:60], sz)
                else:
                    if ct.endswith("*") and sz == 8:
                        ct = "undefined8"
                    emu.put(off, tobytes(v, ct, sz), evid, sz)
                continue
        # llamadas con base+N
        for mc in re.finditer(r"\b(FUN_([0-9a-f]+)|thunk_FUN_([0-9a-f]+)|[A-Za-z_]\w*?)\s*\(\s*([^,()]*(?:\([^()]*\)[^,()]*)*)\s*[,)]", s):
            a0 = mc.group(4)
            r = addr_resolve(a0, bases, decls)
            if r is None:
                continue
            callee = int(mc.group(2) or mc.group(3) or "0", 16) if (mc.group(2) or mc.group(3)) else None
            name = mc.group(1)
            if name in ("if", "while", "for", "return", "sizeof"):
                continue
            emu.calls.append((r, name, evid))
            if callee and depth < 2 and callee in func_loc:
                ctxt = func_text(callee)[0]
                if ctxt.count("\n") < 140:
                    d2, _ = parse_func(ctxt)
                    if "param_1" in d2 and d2["param_1"].endswith("*") or d2.get("param_1", "").startswith("longlong"):
                        emulate(callee, emu, "param_1", r, None, depth + 1)


def selfread(rhs, bases, decls, emu, ct, sz):
    """lectura de un campo ya escrito del mismo objeto (copias tipo param_1[7] = param_1[3])"""
    rhs = rhs.strip()
    if not (rhs.startswith("*") or re.match(r"^\w+\[", rhs)):
        return None
    r = lhs_resolve(rhs, bases, decls)
    if r is None:
        return None
    off, ct2, sz2 = r
    n = sz2 or sz
    bs = []
    for i in range(n):
        x = emu.mem.get(off + i)
        if x is None or x[0] is None:
            return None
        bs.append(x[0])
    b = bytes(bs)
    if ct2.startswith("float") and n == 4:
        return struct.unpack("<f", b)[0] if ct.startswith(("float", "double")) else struct.unpack("<I", b)[0]
    return int.from_bytes(b, "little")


def run_class(c):
    best = None
    res = []
    props = [r for r in target[c] if r["offset"] not in ("", "virtual")]
    offs = {int(r["offset"], 16) for r in props}
    vts = vt_of.get(c, [])
    for a, name, fn in cands.get(c, []):
        for va, cpp in vts:
            sym = sym_of(cpp)
            txt = func_text(a)[0]
            if sym not in txt:
                for alt in ("PTR_FUN_%x" % va, "PTR_LAB_%x" % va, "PTR_%x" % va):
                    if alt in txt:
                        sym = alt
            if sym not in txt:
                continue
            emu = Emu()
            emulate(a, emu, None, 0, sym)
            cover = sum(1 for o in offs if o in emu.mem)
            known = sum(1 for o in offs if o in emu.mem and emu.mem[o][0] is not None)
            score = (known, cover, emu.nconst)
            res.append((score, a, name, emu))
    res.sort(key=lambda x: x[0], reverse=True)
    return res


# ---------------------------------------------------------------- decodificacion
def fmt_f(x):
    if x != x:
        return "NaN"
    if abs(x) >= 1e30:
        return "%.6g" % x
    s = "%.7g" % x
    if "e" not in s and "." in s:
        s = s.rstrip("0").rstrip(".")
    return s


def getb(emu, off, n):
    bs = []
    ev = []
    for i in range(n):
        x = emu.mem.get(off + i)
        if x is None or x[0] is None:
            return None, [x[1] for x in [emu.mem.get(off + j) for j in range(n)] if x]
        bs.append(x[0])
        ev.append(x[1])
    return bytes(bs), ev


def decode(r, emu):
    """-> (default_text, evidencia)"""
    t = r["type"]
    off = int(r["offset"], 16)
    cont = r["container"]
    n = int(r["arraySize"] or 1)
    size = TSIZE.get(t, 8)
    evs = sorted({emu.mem[o][1] for o in range(off, off + max(size, 1)) if o in emu.mem})
    calls = [(o, nm, ev) for o, nm, ev in emu.calls if o == off]
    if cont in ("DynamicArray", "StringMap", "List"):
        if calls or evs:
            return "vacio (contenedor %s inicializado)" % cont, "; ".join(evs[:2] + ["%s(this+0x%x) @%s" % (nm, o, ev) for o, nm, ev in calls[:2]])
        return "vacio (contenedor %s; sin escritura en el ctor)" % cont, ""
    if t in ("String", "Path", "EntityPtr", "EntityHandle", "EntityLink", "FilePtr", "PropertyInfo"):
        b, _ = getb(emu, off, 8)
        ce = ["%s(this+0x%x) @%s" % (nm, o, ev) for o, nm, ev in calls[:2]]
        if b is not None and int.from_bytes(b, "little") == 0:
            return ('""' if t in ("String", "Path") else "null"), "; ".join(evs[:2] + ce)
        if evs and any("= &" in e for e in evs):
            ref = [e.split("= &")[1] for e in evs if "= &" in e][0]
            if t in ("String", "Path"):
                return '"" (puntero a la cadena vacia/compartida %s)' % ref, "; ".join(evs[:2] + ce)
            return "puntero %s" % ref, "; ".join(evs[:2] + ce)
        if calls:
            return ('""' if t in ("String", "Path") else "null/vacio") + " (inicializado por %s)" % calls[0][1], "; ".join(ce)
        if evs:
            return "expr: " + evs[0].split(" = ", 1)[-1], "; ".join(evs[:2])
        return "", ""
    vals = []
    evidence = set()
    for k in range(n):
        o = off + k * size
        if t in ("Vector3", "WideVector3"):
            b, ev = getb(emu, o, 12)
            if b is None:
                b1, ev1 = getb(emu, o, 8)
                vals.append(None)
                evidence.update(ev if ev else [])
                continue
            vals.append("(%s)" % ", ".join(fmt_f(x) for x in struct.unpack("<3f", b)))
        elif t in ("Vector4", "Quat", "Color"):
            b, ev = getb(emu, o, 16)
            if b is None:
                b3, ev3 = getb(emu, o, 12)
                if b3 is not None:
                    vs = struct.unpack("<3f", b3)
                    vals.append(("rgb(%s, ?)" if t == "Color" else "(%s, ?)") % ", ".join(fmt_f(x) for x in vs))
                    evidence.update(ev3)
                    continue
                vals.append(None)
                evidence.update(ev if ev else [])
                continue
            vs = struct.unpack("<4f", b)
            vals.append(("rgba(%s)" if t == "Color" else "(%s)") % ", ".join(fmt_f(x) for x in vs))
        elif t in ("Matrix3", "Matrix4"):
            b, ev = getb(emu, o, size)
            if b is None:
                vals.append(None)
                evidence.update(ev if ev else [])
                continue
            fs = struct.unpack("<%df" % (size // 4), b)
            vals.append("[" + " ".join(fmt_f(x) for x in fs) + "]")
        else:
            b, ev = getb(emu, o, size)
            if b is None:
                vals.append(None)
                evidence.update(ev if ev else [])
                continue
            if t == "Float":
                vals.append(fmt_f(struct.unpack("<f", b)[0]) + " (0x%08x)" % struct.unpack("<I", b)[0])
            elif t == "Double":
                vals.append(fmt_f(struct.unpack("<d", b)[0]))
            elif t == "Bool":
                vals.append("true" if b[0] else "false")
            else:
                signed = t.startswith("Int")
                v = int.from_bytes(b, "little", signed=signed)
                s = str(v)
                if r["enum_values"]:
                    names = dict((int(x.split("=")[1]), x.split("=")[0]) for x in r["enum_values"].split(",") if "=" in x)
                    if v in names:
                        s = "%s (%d)" % (names[v], v)
                elif v > 0xffff or (not signed and size >= 4 and v > 9999):
                    s += " (0x%x)" % v
                vals.append(s)
        evidence.update(ev)
    if all(v is None for v in vals):
        ce = ["%s(this+0x%x) @%s" % (nm, o, ev) for o, nm, ev in calls[:2]]
        if evs:
            raw = [e.split(" = ", 1)[1] for e in evs if " = " in e]
            if raw:
                return "expr: " + raw[0], "; ".join(evs[:2])
            return "parcial", "; ".join(evs[:2])
        if ce:
            return "(inicializado por %s)" % calls[0][1], "; ".join(ce)
        return "", ""
    out = vals[0] if n == 1 else "[" + ", ".join(v or "?" for v in vals) + "]"
    if out is None:
        out = "?"
    return out, "; ".join(sorted(evidence)[:3])


# ---------------------------------------------------------------- unidades y rangos (inferidos)
TYPE_RANGE = {"Int8": "-128..127", "UInt8": "0..255", "Int16": "-32768..32767", "UInt16": "0..65535",
              "Int32": "int32", "UInt32": "uint32", "Bool": "false/true"}


def unit_range(c, p, t, enum):
    """(unidad, rango) heuristico por nombre; todo lo que no sea tipo/enum es inferido por nombre."""
    key = c + "." + p
    if key in MANUAL:
        return MANUAL[key]
    n = p.lower()
    if enum:
        return "enum", "{" + enum.replace(",", ", ") + "}"
    if t == "Bool":
        return "bandera", "false/true"
    if t in ("String", "Path"):
        return "texto", "-"
    if t in ("EntityPtr", "EntityHandle", "EntityLink", "FilePtr"):
        return "referencia", "-"
    if t == "Color":
        return "RGBA lineal", "0..1 por canal (>1 = HDR) [inferido]"
    if t == "Quat":
        return "cuaternion (x,y,z,w)", "unitario [inferido]"
    rules = [
        (r"lux", ("lux", "0..150000 (sol pleno ~100000-130000) [inferido]")),
        (r"lumen", ("lm (lumen)", "0..1e6 [inferido]")),
        (r"candela", ("cd", ">= 0 [inferido]")),
        (r"temperature|kelvin", ("K (temperatura de color)", "1000..20000 [inferido]")),
        (r"latitude", ("grados", "-90..90")), (r"longitude", ("grados", "-180..180")),
        (r"gmt|timedifference", ("horas (huso)", "-12..14 [inferido]")),
        (r"^month|month$", ("mes", "1..12")), (r"^day$|day$", ("dia", "1..31")),
        (r"^year", ("anio", "-")), (r"hour", ("hora", "0..23")),
        (r"minute", ("minuto", "0..59")), (r"second$|^sec", ("segundo", "0..59")),
        (r"fov|angle|degree|yaw|pitch$|roll$|elevation|azimuth", ("grados", "0..180 (fov 1..179) [inferido]")),
        (r"keyvalue", ("valor clave (gris medio de la exposicion automatica)", "0.05..0.5 (0.18 = estandar) [inferido]")),
        (r"luminance", ("luminancia relativa (limite de adaptacion)", "> 0 [inferido]")),
        (r"exposure|^ev|ev$|compensation", ("EV (stops)", "-16..16 [inferido]")),
        (r"flags$|^flags|localflags", ("campo de bits", "mascara [inferido]")),
        (r"blur", ("texels/pixeles de desenfoque", "0..8 [inferido]")),
        (r"repeat", ("repeticiones (tiling UV)", "> 0 [inferido]")),
        (r"anisotropy", ("g de Henyey-Greenstein", "-1..1 [inferido]")),
        (r"exponent", ("exponente (adimensional)", "0..10 [inferido]")),
        (r"opacity|alpha|transparen|blend|ratio|lerp|percent|mix", ("fraccion", "0..1 [inferido]")),
        (r"bias", ("bias de profundidad/vista (unidades del shader)", "-10..10 [inferido]")),
        (r"density|scattering|extinction|coefficient|absorption", ("1/m (coeficiente)", ">= 0 [inferido]")),
        (r"turbidity", ("turbidez (adimensional)", "1..10 [inferido]")),
        (r"speed|velocity", ("m/s", ">= 0 [inferido]")),
        (r"gravity", ("m/s^2", "-")), (r"mass|weight$", ("kg / peso", ">= 0 [inferido]")),
        (r"friction|restitution|damping|dumping|drag|bounce", ("coeficiente", "0..1 [inferido]")),
        (r"stiff|spring|elastic|rigid", ("rigidez (adimensional)", "0..1 [inferido]")),
        (r"time|duration|delay|interval|frame", ("tiempo (s o frames segun uso)", ">= 0 [inferido]")),
        (r"resolution|pixel|texel|texsize|texturesize", ("pixeles", "potencia de 2 [inferido]")),
        (r"count|num|samples|level|lod|index|priority|step|iteration|division|quality", ("entero", ">= 0 [inferido]")),
        (r"radius|range|distance|dist|height|size|length|width|depth|near|far|offset|position|pos$|start|end|"
         r"extent|translation|altitude|bound|margin", ("m (metros Fox)", ">= 0 [inferido]")),
        (r"scale|rate|factor|intensity|power|strength|amount|multiplier|gain|weight|contrast|saturation|"
         r"brightness|gamma|sharp|threshold|attenuation|dimmer", ("factor adimensional", ">= 0 (1 = neutro) [inferido]")),
    ]
    for rx, ur in rules:
        if re.search(rx, n):
            u, rg = ur
            if t in TYPE_RANGE and t not in ("Bool",) and "[inferido]" in rg:
                rg = rg.replace(" [inferido]", "") + "; tipo " + TYPE_RANGE[t] + " [inferido]"
            return u, rg
    if t in TYPE_RANGE:
        return "entero", TYPE_RANGE[t]
    if t in ("Vector3", "Vector4", "WideVector3"):
        return "vector (m o direccion)", "- [inferido]"
    if t in ("Matrix3", "Matrix4"):
        return "matriz", "-"
    return "-", "-"


IDENT = "FUN_1402b5180 (transform identidad estatico: escala 1,1,1,1 / quat 0,0,0,1 / traslacion 0) 0x1436e1bf0"
OVERRIDE = {
    "TransformEntity.transform_scale": ("(1, 1, 1)", "ctor 0x1402bb9f0 copia " + IDENT + "; c_1402583c0.c:72672-72690"),
    "TransformEntity.transform_rotation_quat": ("(0, 0, 0, 1)", "ctor 0x1402bb9f0 copia " + IDENT),
    "TransformEntity.transform_translation": ("(0, 0, 0)", "ctor 0x1402bb9f0 copia " + IDENT),
    "SituationDataSetting.restartMember": ("[10, 9]", "ctor 0x140465e20: bucle `*pcVar3 = '\\n' - i` para i=0,1 (c_140450ae0.c:13923-13928)"),
    "PoseOperatorRootRotParam.rot_p": ("puntero a this+0xa0 (autorreferencia)", "ctor 0x141b120e0: `= param_1 + 0x14` (c_141ae8400.c:30369)"),
    "PoseOperatorRootRotParam.preRot_p": ("puntero a this+0xb0 (autorreferencia)", "ctor 0x141b120e0: `= param_1 + 0x16` (c_141ae8400.c:30371)"),
    "PoseOperatorRootRotParam.flag_p": ("puntero a this+0xc0 (autorreferencia)", "ctor 0x141b120e0: `= param_1 + 0x18` (c_141ae8400.c:30370)"),
    "PhPrimitiveShape.type": ("(copiado del descriptor: *(desc+0x60))", "ctor 0x14186c220: `= *(undefined4 *)(param_1[0x26] + 0x60)` (c_141865b90.c:4460)"),
}
NOCTOR = {
    "Transform": ("ver TransformEntity: escala (1,1,1), rotacion (0,0,0,1), traslacion (0,0,0)", IDENT),
    "TransformRT": ("ver TransformRTEntity: rotacion (0,0,0,1), traslacion 0 (inferido por la identidad de Transform)", IDENT),
    "Rotation": ("quat identidad (0,0,0,1) (inferido: struct valor embebido en Transform)", IDENT),
}
_STRUCT = "(struct valor sin vtable: lo inicializa el dueno; sin ctor propio localizado)"
for _c in ("BoundingBox", "Capsule", "OrientedBoundingBox", "PivotTransform", "ShearTransform", "Sphere"):
    NOCTOR[_c] = (_STRUCT, "registrado con 0x14026bfb0 (tag 0x50004), sin vtable en rtti.txt")
for _c in ("GeoSearchAABB", "GeoSearchCapsule", "GeoSearchDynamic", "GeoSearchLine", "GeoSearchOBB", "GeoSearchPoint",
           "GeoSearchSegment", "GeoSearchSphere", "GeoTrapInfo", "CollisionPoly", "CollisionSkeleton"):
    NOCTOR[_c] = ("(sin ctor localizado: getter registra categoria 0 = clase sin fabrica; ninguna vtable con RTTI apunta a su getter)",
                  "p.ej. GeoSearchSphere getter 0x141662670: FUN_140265bd0(&DAT_143747ee0,0x90,0,0x90001)")

MANUAL = {}


def _m(cls, props, unit, rng):
    for p in props.split():
        MANUAL[cls + "." + p] = (unit, rng)


# TimeOfDaySettings (cobra::rd) -- rangos razonables segun el uso del motor; [inferido] salvo los de calendario
T = "TimeOfDaySettings"
_m(T, "dateTimeHour", "hora local", "0..23")
_m(T, "dateTimeMinute", "minuto", "0..59")
_m(T, "sunLux", "lux (iluminancia del sol)", "0..150000 (sol pleno 100000-130000) [inferido]")
_m(T, "moonLux", "lux (luna)", "0..10 (luna llena ~0.1-1) [inferido]")
_m(T, "skyLightSunScale enlightenOutputScale", "factor adimensional", "0..4 (1 = neutro) [inferido]")
_m(T, "gameExposureCompensation demoExposureCompensation", "EV (stops) sumados a la exposicion automatica", "-16..16 [inferido]")
_m(T, "gameMinExposure gameMaxExposure demoMinExposure demoMaxExposure", "EV (limites de la adaptacion)", "-16..16; 0/0 = sin limites propios (usa GrPluginSettings -16/4) [inferido]")
_m(T, "gameKeyValue demoKeyValue", "valor clave (gris medio)", "0.05..0.5 (0.18 estandar) [inferido]")
_m(T, "gameMaxLuminanceValue demoMaxLuminanceValue", "luminancia maxima de adaptacion (relativa)", "1..1000 [inferido]")
_m(T, "gameInitialExposureValue demoInitialExposureValue", "EV inicial (antes de adaptar)", "-16..16 [inferido]")
_m(T, "acesFilmSlope", "pendiente de la curva ACES filmica", "0.5..1.5 [inferido]")
_m(T, "acesFilmToe acesFilmShoulder", "parametro de la curva ACES (pie/hombro)", "0..1 [inferido]")
_m(T, "acesFilmBlackClip acesFilmWhiteClip", "recorte de negro/blanco ACES", "0..1 (tipico 0..0.1) [inferido]")
_m(T, "whiteTemperature", "K (punto blanco)", "2000..15000 (6500 = D65) [inferido]")
_m(T, "whiteTint", "tinte verde-magenta del balance de blancos", "-1..1 [inferido]")
_m(T, "globalFogDensity globalFogHeightScale", "factor de la niebla global", ">= 0 [inferido]")
_m(T, "startDepth rangeDepthFar rangeDepthNear", "m (distancia de la niebla atmosferica)", ">= 0 [inferido]")
_m(T, "fogAmpFar fogAmpNear whiteness objFogRgbAmpFuncScale objFogHeightAmpMax", "factor de niebla", "0..1 [inferido]")
_m(T, "objFogHeightAmpMaxHeight", "m (altura)", ">= 0 [inferido]")
_m(T, "saoRadius", "m (radio de la oclusion ambiental SAO)", "0.05..2 [inferido]")
_m(T, "saoBias", "bias de SAO (m)", "0..0.2 [inferido]")
_m(T, "saoIntensity saoContrast", "factor SAO", "0..4 [inferido]")
_m(T, "saoBlurSharpness", "nitidez del desenfoque bilateral", "0..64 [inferido]")
_m(T, "modelIntensityTvBox modelIntensityTvLine modelIntensityBillNormal modelIntensityBillMovie modelIntensityAd",
   "multiplicador emisivo de pantallas/carteles", "0..10 (0 = no pisa) [inferido]")
# TppAtmosphere
A = "TppAtmosphere"
_m(A, "rayleighScatteringCoefficient rayleighScatteringCoefficientOfCloudySky", "1/km (beta Rayleigh RGB; valores clasicos 5.8e-3, 13.5e-3, 33.1e-3)", "0..0.1 por canal [inferido]")
_m(A, "mieScatteringCoefficient mieScatteringCoefficientOfCloudySky", "1/km (beta Mie RGB)", "0..0.2 por canal [inferido]")
_m(A, "rayleighHeightScale", "km (altura de escala Rayleigh, 8 km clasico)", "1..20 [inferido]")
_m(A, "mieHeightScale", "m (altura de escala Mie, 1200 m clasico)", "100..5000 [inferido]")
_m(A, "mieAbsorptionRate", "fraccion absorbida por Mie", "0..1 [inferido]")
_m(A, "mieAnisotropy mieAnisotropyOfCloudySky", "g de Henyey-Greenstein", "-1..1 (0.7-0.9 tipico) [inferido]")
_m(A, "northAngle", "grados (rotacion del norte)", "0..360 [inferido]")
_m(A, "latitude", "grados (35.67 = Tokio)", "-90..90")
_m(A, "longitude", "grados (139.73 = Tokio)", "-180..180")
_m(A, "gmtTimeDifference", "horas de huso (9 = JST)", "-12..14")
_m(A, "year", "anio", "1900..2100 [inferido]")
_m(A, "month", "mes", "1..12")
_m(A, "day", "dia", "1..31")
_m(A, "shadowRange shadowRangeExtra hiResShadowRange shadowProjectionRange shadowFadeRange", "m (cascadas de sombra)", ">= 0 [inferido]")
_m(A, "sunLux", "lux", "0..150000 [inferido]")
_m(A, "moonLux starLight", "lux", "0..10 [inferido]")
_m(A, "cloudiness", "fraccion de nubosidad", "0..1 [inferido]")
_m(A, "multiScatteringOrder", "orden de dispersion multiple", "1..10 [inferido]")
_m(A, "numBands", "bandas de armonicos esfericos", "1..4 [inferido]")
_m(A, "interpolateTimeInSecondOfDirLightSteppedMove", "s", ">= 0")
_m(A, "divisonNumOfDirLightSteppedMove", "pasos por dia del movimiento escalonado del sol", ">= 1 [inferido]")
_m(A, "dirLightSunLimitAngle dirLightMoonLimitAngle", "grados de elevacion", "-90..90 [inferido]")
# GrShadowSettings / GrPrecomputeShadowSettings
for S in ("GrShadowSettings", "GrPrecomputeShadowSettings"):
    _m(S, "shadowOpacity maskedShadowOpacity", "opacidad de la sombra", "0..1 [inferido]")
    _m(S, "shadowContrast maskedShadowContrast", "contraste de la sombra", "0..1 [inferido]")
    _m(S, "shadowBlurRadius maskedShadowBlurRadius", "texels de desenfoque", "0..8 [inferido]")
    _m(S, "depthBias", "bias de profundidad", "-0.01..0.01 tipico [inferido]")
    _m(S, "remappingThreshold", "umbral de remapeo VSM (light bleeding)", "0..1 [inferido]")
    _m(S, "temporalSampleCount", "muestras temporales", "1..16 [inferido]")
# Exposicion / plugins
for S in ("PesExposureSettings", "GrPluginSettings"):
    _m(S, "exposureCompensation", "EV", "-16..16 [inferido]")
    _m(S, "minExposure maxExposure", "EV (limites de la adaptacion)", "-16..16 [inferido]")
    _m(S, "keyValue", "valor clave (gris medio)", "0.05..0.5 [inferido]")
    _m(S, "maxLuminanceValue", "luminancia maxima de adaptacion", "1..1000 [inferido]")
    _m(S, "initialExposureValue", "EV inicial", "-16..16 [inferido]")
for S in ("PesPluginSettings", "GrPluginSettings"):
    _m(S, "tonemapSpeed", "velocidad de adaptacion de ojo (1/s)", "0.1..100 [inferido]")
    _m(S, "bloomSize", "radio relativo del bloom", "0..1 [inferido]")
    _m(S, "bloomBrightnessExtraction bloomWhiteOffset", "umbral de extraccion de brillo", "0..10 [inferido]")
    _m(S, "bloomWeight bloomColorCapacity", "peso del bloom en la mezcla", "0..1 [inferido]")
    _m(S, "minDecalArea", "pixeles (area minima del decal en pantalla)", ">= 0 [inferido]")
    _m(S, "flags", "campo de bits (isBloom, isTonemap, isMotionBlur, ... son vistas virtuales de estos bits)", "mascara")
    _m(S, "captureBounceCount", "rebotes de captura de reflejos", "0..4 [inferido]")
    _m(S, "motionBlurConvolutionLevel", "nivel de convolucion del motion blur", "0..4 [inferido]")
_m("TppTonemap", "threshold", "umbral de luminancia", "0..10 [inferido]")
_m("TppTonemap", "range", "rango dinamico", "> 0 [inferido]")
for k in ("default", "firstHalf", "secondHalf", "exFirstHalf", "exSecondHalf", "pk", "end"):
    _m("TimeSlotSettings", k + "Hour", "hora local", "0..23")
    _m("TimeSlotSettings", k + "Minute", "minuto", "0..59")
    _m("TimeSlotSettings", k + "Second", "segundo", "0..59")
for S in ("SpotLight", "PointLight"):
    _m(S, "lumen", "lm (flujo luminoso)", "0..1e6 (estadio 100..10000) [inferido]")
    _m(S, "temperature", "K", "1000..20000 [inferido]")
    _m(S, "colorDeflection", "desvio del color respecto de la temperatura", "-1..1 [inferido]")
    _m(S, "outerRange innerRange lightSize", "m", ">= 0 [inferido]")
    _m(S, "umbraAngle penumbraAngle shadowUmbraAngle shadowPenumbraAngle", "grados (apertura del cono)", "0..180 [inferido]")
    _m(S, "attenuationExponent shadowAttenuationExponent", "exponente de atenuacion", "0..10 [inferido]")
    _m(S, "shadowBias viewBias", "bias de sombra (unidades del shader)", "-10..10 [inferido]")
    _m(S, "dimmer", "atenuador", "0..1 [inferido]")
    _m(S, "powerScale", "multiplicador de potencia", ">= 0 [inferido]")
    _m(S, "LodFarSize LodNearSize", "tamano en pantalla para LOD", ">= 0 [inferido]")
    _m(S, "LodShadowDrawRate", "fraccion de dibujado de sombra por LOD", "0..1 [inferido]")
_m("DirectionalLight", "lux", "lux", "0..150000 [inferido]")
_m("DirectionalLight", "temperature", "K", "1000..20000 [inferido]")
_m("DirectionalLight", "shadowRange shadowRangeExtra hiResShadowRange shadowProjectionRange shadowFadeRange", "m", ">= 0 [inferido]")
_m("TppSky", "cloudCover density diffusion", "fraccion", "0..1 [inferido]")
_m("TppSky", "textureRepeats", "repeticiones de la textura de nubes", ">= 1 [inferido]")
_m("TppGlobalVolumetricFogParam", "density", "1/m", ">= 0 [inferido]")
_m("TppGlobalVolumetricFogParam", "near far", "m", ">= 0 [inferido]")
_m("TppGlobalVolumetricFogParam", "mieAnisotropy", "g de Henyey-Greenstein", "-1..1 [inferido]")
_m("PitchMaterialParameter", "uRepeat vRepeat", "repeticiones UV de la textura del cesped en la cancha", "> 0 [inferido]")
_m("PitchMaterialParameter", "aoMin aoMax aoBottomUp aoHightPower", "factor de AO por altura de la hierba", "0..2 [inferido]")
_m("PitchMaterialParameter", "normalFlatness cameraVectorBlend", "fraccion", "0..1 [inferido]")
_m("PitchMaterialParameter", "repairColor", "color RGB lineal (cesped reparado)", "0..1 [inferido]")

# ---------------------------------------------------------------- Default.fox2
fox2 = defaultdict(lambda: defaultdict(list))  # clase -> prop -> [(entidad, valor)]
if os.path.exists(FOX2):
    curc = curn = None
    for l in open(FOX2, errors="replace"):
        m = re.match(r"^\[\d+\] @0x[0-9a-f]+ addr=0x[0-9a-f]+\s+(\w+) v\d+\s+name=(\S+)", l)
        if m:
            curc, curn = m.group(1), m.group(2).strip('"')
            continue
        m = re.match(r"^    (\w+)\s+(\S+)\s+= (.*)$", l.rstrip("\n"))
        if m and curc:
            fox2[curc][m.group(1)].append((curn, m.group(3).strip()))


def norm_val(v):
    v = v.strip()
    v = re.sub(r" \(0x[0-9a-f]+\)$", "", v)
    v = re.sub(r"^.* \((-?\d+)\)$", r"\1", v)
    return v


def fox2_cmp(c, p, dflt, t):
    lst = fox2.get(c, {}).get(p)
    if not lst:
        return "", ""
    vals = OrderedDict()
    for ent, v in lst:
        vals.setdefault(v, []).append(ent)

    def same(v):
        a = norm_val(dflt)
        b = v
        if t in ("Vector3", "WideVector3") and b.startswith("("):
            b = "(" + ", ".join(b.strip("()").split(", ")[:3]) + ")"
        if t == "Float":
            try:
                return abs(float(a) - float(b)) <= 1e-5 * max(1, abs(float(a)))
            except ValueError:
                return False
        if t in ("Vector3", "Vector4", "Quat", "Color", "WideVector3"):
            fa = re.findall(r"-?[\d.]+(?:e[-+]?\d+)?", a)
            fb = re.findall(r"-?[\d.]+(?:e[-+]?\d+)?", b)
            if fa and len(fa) == len(fb):
                return all(abs(float(x) - float(y)) <= 1e-4 * max(1, abs(float(x))) for x, y in zip(fa, fb))
            return False
        if a.startswith("vacio"):
            return b.strip() in ("[]", "{}") or b.endswith("[0] = []")
        if t in ("String", "Path"):
            return (a.startswith('""') and b == '""')
        if t in ("EntityLink", "EntityPtr", "EntityHandle", "FilePtr"):
            emptyb = b in ("null", "") or b.startswith('link(pkg="", arch="", name="", addr=0x0)') or b == 'file("")' or b == '""'
            return a.startswith("null") and emptyb
        if t == "Bool":
            return a == b
        if a == b:
            return True
        try:
            return int(a) == int(b)
        except ValueError:
            return False
    parts = []
    diff = False
    for v, ents in vals.items():
        if dflt:
            s = same(v)
            diff = diff or not s
        parts.append("%s%s" % (v, (" [%d entidades]" % len(ents)) if len(ents) > 1 else ""))
    flag = ("DIFIERE" if diff else "igual") if dflt else "sin default"
    return "; ".join(parts)[:300], flag


# ---------------------------------------------------------------- propiedades virtuales (PropertyHandler::vf1 = getter)
sys.path.insert(0, os.path.dirname(HERE))
try:
    from foxhash import strcode
except Exception:
    strcode = None


def handler_bits():
    """-> {handler_fn_name: {hash48: (offset, bit, handler_addr, evid)}} leyendo los getters de los PropertyHandler."""
    out = {}
    for a, (fn, st, en, name) in func_loc.items():
        if not re.search(r"PropertyHandler__vf1$", name):
            continue
        txt = texts[fn][st:en]
        ln0 = texts[fn].count("\n", 0, st) + 1
        ms = list(re.finditer(r"[=!]= (0x[0-9a-f]{9,12})\b", txt))
        if "switch(" in txt and "+ 0xc))" in txt:
            ms = list(re.finditer(r"case (0x[0-9a-f]+|\d+):", txt))
        res = {}
        for i, m in enumerate(ms):
            seg = txt[m.end(): ms[i + 1].start() if i + 1 < len(ms) else len(txt)]
            h = ival(m.group(1)) if m.group(0).startswith("case") else int(m.group(1), 16)
            if m.group(0).startswith("case"):
                h = ("idx", h)
            mm = re.search(r"\(char\)\(\*\(\w+ \*\)\((\w+) \+ (0x[0-9a-f]+)\) >> (\d+|0x[0-9a-f]+)\)", seg)
            if mm:
                off, bit = int(mm.group(2), 16), ival(mm.group(3))
            else:
                mm = re.search(r"\*\(\w+ \*\)\((\w+) \+ (0x[0-9a-f]+)\) >> (\d+|0x[0-9a-f]+) & (?:0xffffff01|1)\b", seg)
                if mm:
                    off, bit = int(mm.group(2), 16), ival(mm.group(3))
                else:
                    mm = re.search(r"\*\((?:byte|char|undefined1|uint|undefined4) \*\)\((\w+) \+ (0x[0-9a-f]+)\) & (0x[0-9a-f]+|\d+)\)?(?: != 0)?", seg)
                    if mm and ival(mm.group(3)) & (ival(mm.group(3)) - 1) == 0:
                        off, bit = int(mm.group(2), 16), ival(mm.group(3)).bit_length() - 1
                    else:
                        continue
            lnm = ln0 + txt.count("\n", 0, m.start())
            res[h] = (off, bit, a, "%s:%d" % (os.path.basename(fn), lnm))
        if res:
            out[name] = res
    return out


def virtual_defaults():
    """-> {(clase, prop): (texto_default, evidencia)}"""
    if strcode is None:
        return {}
    hb = handler_bits()
    vv = {}
    for c, props in target.items():
        vps = [r["property"] for r in props if r["offset"] == "virtual"]
        if not vps:
            continue
        hs = {strcode(p): p for p in vps}
        order = sorted([r for r in props if r["kind"] in ("PROP", "PROP_EMB") and r["callsite"]], key=lambda r: int(r["callsite"], 16))
        for i, r in enumerate(order):
            if r["offset"] == "virtual":
                hs[("idx", i)] = r["property"]
        best, bn = None, 0
        for name, res in hb.items():
            nm = name.split("__")[-2] in (c + "PropertyHandler", re.sub(r"^(Gr|Grx|Tpp|Pes|Ph)", "", c) + "PropertyHandler",
                                          re.sub(r"^(Gr|Grx|Tpp|Pes|Ph)", "", c) + "PropertyHandler".replace("Transform", "Transfrom"))
            n = len([h for h in set(res) & set(hs) if not isinstance(h, tuple) or nm])
            n = n * 2 + (1 if nm else 0)
            if n > bn:
                best, bn = name, n
        if best:
            for h, p in hs.items():
                if h in hb[best]:
                    vv[(c, p)] = (best,) + hb[best][h]
    return vv


# ---------------------------------------------------------------- principal
def main():
    global VV, VFOX
    VV = virtual_defaults()
    VFOX = {}
    print("virtuales resueltas", len(VV))
    out_rows = []
    cls_info = OrderedDict()
    for c, props in target.items():
        res = run_class(c)
        best = res[0] if res and res[0][0][1] > 0 else (res[0] if res else None)
        cls_info[c] = (best, res, vt_of.get(c, []))
        for r in props:
            off = r["offset"]
            t = r["type"]
            key = c + "." + r["property"]
            if key in OVERRIDE:
                d, ev = OVERRIDE[key]
            elif off in ("", "virtual") and (c, r["property"]) in VV and best is not None:
                hn, hoff, bit, ha, hev = VV[(c, r["property"])]
                b = best[3].mem.get(hoff + bit // 8)
                fr = [x for x in target[c] if x["offset"] not in ("", "virtual") and int(x["offset"], 16) <= hoff < int(x["offset"], 16) + TSIZE.get(x["type"], 4)]
                fld = fr[0]["property"] if fr else "+0x%x" % hoff
                if b is not None and b[0] is not None:
                    d = ("true" if (b[0] >> (bit % 8)) & 1 else "false") + " (bit %d de %s +0x%x)" % (bit, fld, hoff)
                else:
                    d = "(bit %d de %s +0x%x; sin valor en el ctor)" % (bit, fld, hoff)
                ev = "handler %s 0x%x (%s); ctor 0x%x" % (hn, ha, hev, best[1])
                fl = fox2.get(c, {}).get(fld)
                if fl:
                    vs = []
                    for ent, v in fl:
                        try:
                            vs.append("true" if (int(v, 0) >> bit) & 1 else "false")
                        except ValueError:
                            pass
                    if vs:
                        VFOX[(c, r["property"])] = (", ".join(sorted(set(vs))) + " (de %s = %s)" % (fld, fl[0][1]),
                                                    "igual" if all(v == d.split()[0] for v in vs) else "DIFIERE")
            elif off in ("", "virtual"):
                d, ev = "(virtual: sin campo; lo calcula el PropertyHandler)", ""
            elif t == "embed":
                d, ev = "(struct embebido `%s`: ver las subpropiedades %s_*)" % (r["ptr_class"], r["property"]), ""
            elif best is None:
                if c in NOCTOR:
                    d, ev = NOCTOR[c]
                else:
                    d, ev = "(sin ctor localizado: la clase no tiene vtable con RTTI)", ""
            else:
                d, ev = decode(r, best[3])
                if d == "":
                    d = "(sin escritura en el ctor: 0 si el allocator limpia / heredado)" if best[0][1] else ""
            u, rg = unit_range(c, r["property"], t, r["enum_values"])
            fv, flag = fox2_cmp(c, r["property"], d if d and not re.match(r"\((sin|virtual|inicializado|struct)|expr|parcial", d) else "", t)
            if (c, r["property"]) in VFOX:
                fv, flag = VFOX[(c, r["property"])]
            out_rows.append(OrderedDict([
                ("clase", c), ("propiedad", r["property"]), ("offset", off), ("tipo", t + (
                    "[%s]" % r["arraySize"] if r["arraySize"] not in ("", "1") else "") + (
                    " %s" % r["container"] if r["container"] not in ("StaticArray", "") else "")),
                ("default", d), ("unidad", u), ("rango", rg),
                ("valor_en_Default.fox2", (fv + (" => " + flag if flag else "")) if fv else ""),
                ("evidencia", ("ctor 0x%x %s; " % (best[1], best[2]) if best else "") + ev),
                ("categoria", RD.cat_of(c))]))
    with open(OUT + "/defaults.tsv", "w") as f:
        f.write("\t".join(out_rows[0].keys()) + "\n")
        for r in out_rows:
            f.write("\t".join(str(v).replace("\t", " ").replace("\n", " ") for v in r.values()) + "\n")
    write_md(out_rows, {c: (b[1] if b else None, b[2] if b else None, b[0] if b else None,
                            [(x[1], x[2], x[0]) for x in res], [(hex(v), n) for v, n in vts])
                        for c, (b, res, vts) in cls_info.items()})
    import pickle
    pickle.dump({"rows": out_rows, "cls": {c: (b[1] if b else None, b[2] if b else None, b[0] if b else None,
                                                [(x[1], x[2], x[0]) for x in res], [(hex(v), n) for v, n in vts])
                                           for c, (b, res, vts) in cls_info.items()}},
                open(OUT + "/defaults_meta.pkl", "wb"))
    print("filas", len(out_rows), "clases", len(cls_info))
    nd = sum(1 for r in out_rows if r["default"] and not r["default"].startswith("(sin") and not r["default"].startswith("(virtual"))
    print("con default", nd)



# ---------------------------------------------------------------- informe Markdown
PRIO = ["TimeOfDaySettings", "TppAtmosphere", "GrShadowSettings", "PesExposureSettings", "TppTonemap", "TimeSlotSettings"]
PRIO_RX = r"Grass|Turf|Pitch|Stadium|Sky|Cloud|Fog|Light|Shadow|Camera|Cloth|Bone|Skin"


def md_escape(x):
    return str(x).replace("|", "\\|")


def write_md(out_rows, meta):
    by = OrderedDict()
    for r in out_rows:
        by.setdefault(r["clase"], []).append(r)
    o = []
    w = o.append
    nprops = len(out_rows)
    nval = sum(1 for r in out_rows if r["default"] and not r["default"].startswith("("))
    nvirt = sum(1 for r in out_rows if r["offset"] == "virtual")
    nvres = sum(1 for r in out_rows if r["offset"] == "virtual" and "(bit " in r["default"] and not r["default"].startswith("("))
    ndif = sum(1 for r in out_rows if "DIFIERE" in r["valor_en_Default.fox2"])
    nfox = sum(1 for r in out_rows if r["valor_en_Default.fox2"])
    w("# Valores por defecto, unidades y rangos de las propiedades de Fox Engine (PES2021.exe 1.07.02)\n")
    w("Generado por `tools/re/defaults.py` a partir de `reflexion.tsv` (offsets) y del C de Ghidra (`export/c/*.c`). "
      "Datos completos: `/home/claude/re/out/defaults.tsv` (%d filas, %d clases de las categorias luz, cielo_clima, sombras, "
      "postprocesado, camara, cesped_estadio, mallas_modelos, huesos_animacion, fisica_ropa, texturas_materiales y gameplay).\n"
      % (nprops, len(by)))
    w("Resumen: %d propiedades con valor por defecto leido del constructor (incluye %d virtuales resueltas a un bit de `flags`), "
      "%d virtuales en total, %d propiedades cruzadas con `Default.fox2` y **%d que difieren** del default del motor.\n"
      % (nval, nvres, nvirt, nfox, ndif))
    w(METODO)
    # tabla de diferencias
    w("\n## 2. Lo que `Default.fox2` cambia respecto del default del motor\n")
    w("`Default.fox2` (`/Assets/pes16/model/light/Default.fox2`, volcado en `/home/claude/cpk/fox2_txt/Default.txt`) es el set de "
      "luces del partido: TimeOfDaySettings, TimeOfDay, PesPluginSettings, ColorGrading, 26 SpotLight, ReflectionProbe(Set). "
      "Estas son las propiedades cuyo valor en el archivo NO es el que pone el constructor:\n")
    w("| Clase | Propiedad | Offset | Default (ctor) | En Default.fox2 |")
    w("|---|---|---|---|---|")
    for r in out_rows:
        if "DIFIERE" in r["valor_en_Default.fox2"]:
            w("| %s | `%s` | %s | %s | %s |" % (r["clase"], r["propiedad"], r["offset"], md_escape(r["default"]),
                                               md_escape(r["valor_en_Default.fox2"].replace(" => DIFIERE", ""))[:220]))
    w("\nLecturas utiles:\n")
    w(LECTURAS)
    # prioritarias
    w("\n## 3. Clases prioritarias\n")
    prio = PRIO + [c for c in by if c not in PRIO and re.search(PRIO_RX, c)]
    for c in prio:
        if c in by:
            class_block(w, c, by[c], meta, full=True)
    w("\n## 4. Resto de las clases, por categoria\n")
    for k in CATS:
        cl = [c for c in by if RD.cat_of(c) == k and c not in prio]
        if not cl:
            continue
        w("\n### Categoria `%s`: %s\n" % (k, RD.CATS[k]))
        for c in cl:
            class_block(w, c, by[c], meta, full=False)
    w(LIMITES)
    open(OUT + "/defaults.md", "w").write("\n".join(o) + "\n")


def class_block(w, c, rows, meta, full):
    a, n, sc, res, vts = meta[c]
    ci = info_of.get(c, {})
    lvl = "###" if full else "####"
    w("%s %s\n" % (lvl, c))
    vt = ", ".join("`%s` %s" % (n_, v) for v, n_ in vts) or "ninguna con RTTI"
    if a:
        fn = func_loc.get(a)
        w("Categoria `%s`. Tamano %s, super `%s`. Vtable: %s. **Constructor: `0x%x`** (`%s`, %s), elegido entre %d funciones que "
          "escriben la vtable (puntaje: %d props con valor, %d tocadas, %d escrituras constantes).\n"
          % (RD.cat_of(c), ci.get("size", "?"), ci.get("super", "-") or "-", vt, a, n,
             os.path.basename(fn[0]) if fn else "?", len(res), sc[0], sc[1], sc[2]))
    else:
        w("Categoria `%s`. Tamano %s. Vtable: %s. Sin constructor localizado.\n" % (RD.cat_of(c), ci.get("size", "?"), vt))
    w("| Offset | Propiedad | Tipo | Default | Unidad | Rango plausible | Default.fox2 |")
    w("|---|---|---|---|---|---|---|")
    for r in rows:
        fv = r["valor_en_Default.fox2"]
        if fv:
            fv = ("**" + fv.replace(" => DIFIERE", "") + "** (difiere)") if "DIFIERE" in fv else fv.replace(" => igual", " (igual)")
        w("| %s | `%s` | %s | %s | %s | %s | %s |" % (r["offset"], r["propiedad"], r["tipo"], md_escape(r["default"]),
                                                     md_escape(r["unidad"]), md_escape(r["rango"]), md_escape(fv)[:200]))
    w("")


METODO = r"""
## 1. Metodo (y por que se puede confiar en cada valor)

1. **Vtable de cada clase Fox.** El metodo virtual 0 de una entidad Fox es `GetClassEntityInfo`, y en este binario es un
   `jmp` al getter del `EntityInfo` (la columna `getter` de `reflexion_clases.tsv`). Ejemplo: `fox::grx::ShadowSettings`
   (vtable `0x142a53090`) tiene vf0 = `0x14183de70` = `e9 bb fe ff ff` -> `jmp 0x14183dd30`, que es el getter de
   `GrShadowSettings` (`FUN_140265bd0(&DAT_143783f20,0x98,...)`). Asi se resuelven los nombres C++ que no coinciden con el
   nombre Fox: GrShadowSettings = `fox::grx::ShadowSettings`, PesExposureSettings = `cobra::rd::ExposureSettings`,
   TppTonemap = `tpp::ef::tonemap::Tonemap`, PesPluginSettings = `cobra::rd::PluginSettings`, etc.
2. **Constructor.** En el C de Ghidra se buscan todas las funciones que hacen `*X = &<ns>__<Clase>__vftable;` (ctor, ctor de
   copia/carga, destructor y funciones de creacion con el ctor inlineado, como `FUN_14183dbc0` para GrShadowSettings, que hace
   `FUN_14029cae0(0x98,...)` y escribe los campos sobre el puntero devuelto).
3. **Emulacion de las escrituras.** Para cada candidata se recorren en orden las sentencias `*(T *)(X + N) = v;`,
   `*(T *)((longlong)X + N) = v;`, `X[N] = v;` (escaladas por el tipo del puntero), con alias (`p = X + N`), locales con valor
   conocido, lecturas de globales del volcado (`_DAT_`, `fRam...`) y copias de campos ya escritos del mismo objeto. Se entra
   (hasta 2 niveles) en las funciones llamadas con `X + N` como primer argumento (ctors de String/Path/EntityLink/arrays).
   Las escrituras de 8 bytes se parten en bytes (ej. `param_1[0x3a] = 0x3f8000003f800000` = dos floats 1.0).
4. **Eleccion.** Gana la candidata que da valor a mas propiedades (el ctor de copia/carga copia de `param_2` y no da constantes).
5. **Decodificacion por tipo y offset** de `reflexion.tsv`: Float (con el hex IEEE), Int/UInt, Bool, enum (nombre del valor),
   Vector3/4, Quat, Color. String/Path/EntityLink/FilePtr: `""`/`null` si el ctor los deja en cero o los inicializa con su ctor.
6. **Propiedades virtuales.** Muchas Bool virtuales son bits de un campo `flags`/`localFlags`. Se leen del getter del
   PropertyHandler (`<Clase>PropertyHandler::vf1`), que compara el hash StrCode64 del nombre (`tools/foxhash.py`) o el indice
   de la propiedad (`switch(*(u16*)(PropertyInfo+0xc))`) y devuelve `campo >> bit & 1`. El default sale del bit en el valor del
   ctor. Ejemplo: `cobra::rd::impl::PesPluginSettingsPropertyHandler::vf1` (`0x141951ba0`):
   `hash == 0x96182e40fc73` (= StrCode64("isLightAdaptationFromLACC")) -> `*(uint*)(this+0x88) >> 9 & 1`.
7. **Unidades y rangos.** El ejecutable no guarda limites para estas propiedades (el accesor Lua solo tiene un par de doubles
   1.0/-1.0 iguales para todas). Las unidades salen del nombre, del valor por defecto y del uso conocido (ver NOTAS.md: Sky_SetDateTime,
   Astro_SunPos); los rangos son **plausibles e inferidos** salvo los de calendario/geografia, los enum (lista completa) y los Bool.
   Todo lo inferido lleva `[inferido]`.

### Validacion contra el codigo y contra el archivo real
- `TimeOfDaySettings.sunLux` (+0x148): ctor `FUN_141922ac0` escribe `*(undefined4 *)(param_1 + 0x29) = 0x48127c00`
  (0x29*8 = 0x148; 0x48127c00 = 150000.0). `Default.fox2` tiene sunLux = 150000 -> igual.
- `TppAtmosphere.latitude/longitude` (+0x208/+0x20c) = 35.67 / 139.73 (Tokio), `gmtTimeDifference` = 9 (JST), fecha 2010-12-20:
  el sol del motor por defecto esta calculado para Tokio en diciembre.
- `GrShadowSettings` (ctor inlineado en `FUN_14183dbc0`): method = 1 (PrecomputedVSM), opacidad 1, blur 1, masked 0.85/3,
  temporalSampleCount 4.
- `SpotLight` (ctor `FUN_141811630`): `reachPoint` = `_DAT_143780720 * 5.0` (vector (0,-1,0) * 5) = (0,-5,0), igual que el .fox2.
"""

LECTURAS = r"""- **Exposicion**: el motor arranca sin override (compensacion 0, keyValue 0.18, maxLuminance 32). `Default.fox2` activa
  `overrideGameExposure`/`overrideDemoExposure` con compensacion **-7.1 EV**: el partido se ve con la exposicion automatica
  bajada 7.1 stops respecto del default.
- **Tonemap ACES**: default slope 0.88 / toe 0.55 / shoulder 0.26 / whiteClip 0.04 / whiteTint 0.05; el .fox2 pone toe 0.45,
  shoulder 0.55, whiteClip 0.02, whiteTint 0.075 (curva con mas hombro: altas luces mas comprimidas).
- **PesPluginSettings**: bloom mas chico y mas selectivo en el .fox2 (size 0.3, extraccion 3, peso 0.3 vs 0.4/0.6/0.5),
  adaptacion de ojo 30x mas rapida (tonemapSpeed 30 vs 1), 3 rebotes de captura (vs 2) y `flags` 0xb07 vs 0x907: el .fox2
  prende el bit 9 = `isLightAdaptationFromLACC`.
- **SpotLight**: el default del motor (lumen 10000, umbra 75, penumbra 0, exponente 1.2, sin sombra, bias 0, rango 5 m) no se
  usa en ningun foco: los 26 del .fox2 tienen rango 10 m, exponente 3, bias -2 y lumenes de 100 a 5400.
- **ReflectionProbe**: innerScale 0.8 (vs 0.9) y captureResolution 2 = 128 px (vs 1 = 64 px) en el .fox2.
- **TimeOfDay**: el motor arranca en `timeAdvanceMode` PROGRESSIVE (1); el .fox2 lo pone en NONE (0): la hora no avanza sola
  durante el partido, la fija TimeOfDaySettings/TimeSlotSettings.
"""

LIMITES = r"""
## 5. Limitaciones conocidas
- Clases sin vtable con RTTI (structs valor Transform/Rotation/BoundingBox/Sphere/... y las de busqueda GeoSearch*, GeoTrapInfo,
  CollisionPoly/Skeleton): no tienen ctor propio localizable. Para Transform/Rotation se documenta la identidad que copia
  TransformEntity (`FUN_1402b5180`, global `0x1436e1bf0`).
- Los campos que el ctor no escribe aparecen como "(sin escritura en el ctor...)": quedan con lo que deje el allocator
  (`FUN_14029cae0`, no se verifico si limpia) o con lo que ponga el ctor base.
- Las virtuales que no son un bit de flags (las de fisica Ph*, que reenvian al objeto de Havok/fisica) quedan sin default.
  De TppAtmosphere quedan sin resolver `disableSkyCapture`, `expandHorizontalLineColor` y `fogEnable` (patron distinto en el
  handler); `localFlags` = 0xc3e por defecto.
- El ctor elegido es el de construccion por defecto; si el juego crea la entidad desde un .fox2, los valores del archivo pisan
  estos. Las unidades/rangos `[inferido]` son interpretacion por nombre y uso, no limites que el motor imponga.
"""


if __name__ == "__main__":
    main()
