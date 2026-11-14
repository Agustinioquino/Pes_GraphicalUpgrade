#!/usr/bin/env python3
"""reflect.py - vuelca la reflexion de propiedades de Fox Engine de PES2021.exe 1.07.02 (volcado).

Recorre TODAS las funciones que registran propiedades (llamadas directas a los AddProperty de
fox::EntityInfo, al AddAccessor 0x140267cc0, a los registros de enum, a los constructores de
EntityInfo) y emula linealmente sus registros/stack sobre el desensamblado de objdump. No usa el C de
Ghidra para los valores (Ghidra pierde argumentos en ~30 % de las llamadas, p. ej. `lea edx,[r9-0x62]`).

Uso:
  export PES_DUMP=".../dump/"
  nice -n 10 python3 reflect.py [salida.tsv]      (por defecto /home/claude/re/out/reflexion.tsv)

Formato del PropertyInfo (0x48 B, ctor por tipo, fabrica 0x14023fb90(tipo, nombre, offset, arraySize,
contenedor, p6, p7, p8)):
  +0x00 vtbl fox::<T>::PropertyInfoT   +0x08 u32 tipo   +0x0c u16 indice  +0x0e u16 indice2
  +0x10 String nombre  +0x18 u64 OFFSET en el objeto  +0x20 u32 arraySize  +0x24 u32 contenedor
  +0x28 u32 p6 (flag)  +0x2c u32 flags (bit0 = propiedad virtual / via PropertyHandler, sin memoria)
  +0x30 ptr p7 (EntityInfo apuntado, para EntityPtr)  +0x38 ptr p8 (EnumInfo*)  +0x40 ptr 0
"""
import sys, os, re, struct, bisect, subprocess
from collections import defaultdict
from multiprocessing import Pool

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dumpmap import DATA, BASE, read, sec_of, TEXT_BASE, D
import numpy as np

TYPES = ["Int8", "UInt8", "Int16", "UInt16", "Int32", "UInt32", "Int64", "UInt64", "Float", "Double", "Bool",
         "String", "Path", "EntityPtr", "Vector3", "Vector4", "Quat", "Matrix3", "Matrix4", "Color", "FilePtr",
         "EntityHandle", "EntityLink", "PropertyInfo", "WideVector3"]
CONT = {0: "StaticArray", 1: "DynamicArray", 2: "StringMap", 3: "List"}

# AddProperty: dir -> (descripcion de argumentos)
#   cada valor: dict con 'type' (int fijo o registro), 'name' (registro con String*), 'off', 'arr', 'cont', 'enum', 'ptr'
ADDPROP = {
    0x140266f70: dict(type="rdx", name="r8", off="r9", arr=1, cont=0),
    0x140266d40: dict(type="rdx", name="r8", off="r9", arr=1, cont=1),
    0x140267260: dict(type="rdx", name="r8", off="r9", arr=1, cont=3),
    0x140267350: dict(type="rdx", name="r8", off="r9", arr=1, cont=2),
    0x1402672e0: dict(type="rdx", name="r8", off="r9", arr="s20", cont=0),
    0x140267080: dict(type="rdx", name="r8", off="r9", arr=1, cont=0, enum="s20"),
    0x140266dc0: dict(type=13, name="rdx", off="r8", arr=1, cont=0, ptr="r9"),
    0x140266e20: dict(type=13, name="rdx", off="r8", arr=1, cont=1, ptr="r9"),
    0x140266f00: dict(type=13, name="rdx", off="r8", arr=1, cont=2, ptr="r9"),
    0x140266e90: dict(type=13, name="rdx", off="r8", arr=1, cont=3, ptr="r9"),
    0x140267120: dict(type=20, name="rdx", off="r8", arr=1, cont=0),
    0x140267180: dict(type=20, name="rdx", off="r8", arr=1, cont=1),
    0x1402671f0: dict(type=20, name="rdx", off="r8", arr=1, cont=2),
    0x1402673d0: dict(type=1, name="rdx", off="r8", arr=1, cont=2),
}
F_FACTORY = 0x14023fb90      # crea PropertyInfo suelto (luego 0x140266cf0 lo agrega como virtual)
F_ADDVIRT = 0x140266cf0      # agrega PropertyInfo ya creado, flags |= 1 (sin offset: PropertyHandler)
F_EMBED = 0x140267440        # (info, structInfo, String* prefijo, offsetBase): copia props "%s_%s"
F_ACCESS = 0x140267cc0       # (info, nombre, nombreLua, "RW", handler)
F_REGCLASS = 0x14026bf00     # (&res, "Clase", info)
F_META = 0x140299320        # entrada de tabla estatica (0x38 B) en EntityInfo+0x108 (ver doc)
F_REGSTRUCT = 0x14026bfb0    # (&res, "Struct", info): tipos valor (Transform, AABB...) tag 0x50004
F_SUPER = 0x14026cbb0        # (info, superInfo)
F_VERSION = 0x14026cbd0      # (info, version)
F_ENUMNEW = 0x14028f1e0      # (flag, "Enum", 0) -> EnumInfo*
F_ENUMVAL = 0x14028f4f0      # (enum, valor, "NOMBRE")
F_INFO_DYN = 0x140265bd0     # (info, tamano, categoria, tag)
F_INFO_STA = 0x1402659c0     # (info, "Clase", super, tamano, ver, ..., regfn @[rsp+0x40], ...)
F_STR_LIT = 0x1403d2990      # (char*, 2) -> StrCode/String temporal
F_STR_CPY = 0x1403d2240
F_STR_SET = 0x1403d2000
F_STR_NEW = 0x14022ca20      # (String*, char*)
TARGETS = set(ADDPROP) | {F_FACTORY, F_ADDVIRT, F_EMBED, F_ACCESS, F_REGCLASS, F_REGSTRUCT, F_META, F_SUPER, F_VERSION, F_ENUMNEW,
                          F_ENUMVAL, F_INFO_DYN, F_INFO_STA}
PROPLIKE = set(ADDPROP) | {F_FACTORY, F_ADDVIRT, F_EMBED, F_ACCESS, F_REGCLASS, F_REGSTRUCT, F_META, F_SUPER, F_ENUMNEW, F_ENUMVAL,
                           F_INFO_DYN, F_INFO_STA}

# ---------------------------------------------------------------- utilidades del volcado
pb, pd = DATA[".pdata"]
FUN = []
for i in range(0, len(pd) - 12, 12):
    s, e, u = struct.unpack_from("<III", pd, i)
    if s == 0 and e == 0:
        continue
    FUN.append((BASE + s, BASE + e))
FUN.sort()
STARTS = [f[0] for f in FUN]


def func_of(a):
    i = bisect.bisect_right(STARTS, a) - 1
    if i >= 0 and FUN[i][0] <= a < FUN[i][1]:
        return FUN[i]
    return None


def cstr(a, maxlen=200):
    if not isinstance(a, int):
        return None
    b = read(a, maxlen)
    if not b:
        return None
    z = b.find(b"\0")
    if z <= 0:
        return None
    s = b[:z]
    if not all(32 <= c < 127 for c in s):
        return None
    return s.decode()


def call_sites():
    """todas las instrucciones call rel32 (E8) cuyo destino esta en TARGETS"""
    tb, td = DATA[".text"]
    a = np.frombuffer(td, dtype=np.uint8)
    idx = np.nonzero(a[:-5] == 0xE8)[0]
    rel = (a[idx + 1].astype(np.int64) | (a[idx + 2].astype(np.int64) << 8) | (a[idx + 3].astype(np.int64) << 16)
           | (a[idx + 4].astype(np.int64) << 24))
    rel = np.where(rel >= 1 << 31, rel - (1 << 32), rel)
    tgt = tb + idx + 5 + rel
    m = np.isin(tgt, np.array(sorted(TARGETS), dtype=np.int64))
    return [(int(tb + i), int(t)) for i, t in zip(idx[m], tgt[m])]


def objdump(rng):
    s, e = rng
    cmd = ["objdump", "-D", "-b", "binary", "-m", "i386:x86-64", "-M", "intel", "--adjust-vma=%#x" % TEXT_BASE,
           "--start-address=%#x" % s, "--stop-address=%#x" % e, D + "_text.bin"]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    res = []
    for l in out.splitlines():
        p = l.split("\t")
        if len(p) < 3 or not p[0].strip().endswith(":"):
            continue
        try:
            ad = int(p[0].strip()[:-1], 16)
        except ValueError:
            continue
        res.append((ad, p[2].strip()))
    return rng, res


# ---------------------------------------------------------------- emulador lineal minimo
R64 = {}
for b in ["ax", "bx", "cx", "dx"]:
    for n in ["r" + b, "e" + b, b, b[0] + "l", b[0] + "h"]:
        R64[n] = "r" + b
for b in ["si", "di", "bp", "sp"]:
    for n in ["r" + b, "e" + b, b, b + "l"]:
        R64[n] = "r" + b
for k in range(8, 16):
    for suf in ["", "d", "w", "b"]:
        R64["r%d%s" % (k, suf)] = "r%d" % k
VOLATILE = ["rax", "rcx", "rdx", "r8", "r9", "r10", "r11"]
RE_MEM = re.compile(r"^(?:(?:BYTE|WORD|DWORD|QWORD|XMMWORD|YMMWORD)\s+PTR\s+)?(?:\w+:)?\[([^\]]+)\]$")
RE_CMT = re.compile(r"#\s*0x([0-9a-f]+)")


def split_ops(s):
    s = s.split("#")[0].strip()
    out, depth, cur = [], 0, ""
    for c in s:
        if c == "[":
            depth += 1
        elif c == "]":
            depth -= 1
        if c == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


def parse_int(s):
    try:
        return int(s, 0)
    except ValueError:
        return None


def emulate(ins):
    """devuelve lista de eventos (sitio, destino, args) para cada call a TARGETS"""
    regs = {"rcx": ("arg", "rcx"), "rdx": ("arg", "rdx"), "r8": ("arg", "r8"), "r9": ("arg", "r9")}
    stk = {}           # desplazamiento rsp -> valor
    strobj = {}        # ('stk', base, disp) -> literal
    lastlit = [None]
    ev = []

    def val(op, comment_addr):
        op = op.strip()
        if op in R64:
            return regs.get(R64[op])
        v = parse_int(op)
        if v is not None:
            return v
        m = RE_MEM.match(op)
        if m:
            inner = m.group(1).replace(" ", "")
            if inner.startswith("rsp+") or inner == "rsp":
                d = parse_int(inner[4:]) if "+" in inner else 0
                return stk.get(d)
        return None

    def lea_val(inner, comment_addr):
        inner = inner.replace(" ", "")
        if inner.startswith("rip"):
            return comment_addr
        m = re.match(r"^(\w+)([+-]0x[0-9a-f]+)?$", inner)
        if not m or m.group(1) not in R64:
            return None
        base = R64[m.group(1)]
        d = int(m.group(2), 16) if m.group(2) else 0
        if base in ("rsp", "rbp"):
            return ("stk", base, d)
        bv = regs.get(base)
        if isinstance(bv, int):
            return (bv + d) & 0xffffffffffffffff
        if isinstance(bv, tuple) and bv[0] == "stk":
            return ("stk", bv[1], bv[2] + d)
        return None

    for ad, txt in ins:
        cm = RE_CMT.search(txt)
        comment_addr = int(cm.group(1), 16) if cm else None
        parts = txt.split(None, 1)
        if not parts:
            continue
        mn = parts[0]
        if mn in ("rex", "rep", "lock", "bnd", "notrack", "data16", "cs", "ds") and len(parts) > 1:
            parts = parts[1].split(None, 1)
            mn = parts[0]
        ops = split_ops(parts[1]) if len(parts) > 1 else []
        if mn == "call":
            t = parse_int(ops[0]) if ops else None
            if t in TARGETS:
                ev.append((ad, t, {k: regs.get(k) for k in ("rcx", "rdx", "r8", "r9")},
                           {k: v for k, v in stk.items() if 0x20 <= k <= 0x98}, dict(strobj), lastlit[0]))
            # efectos conocidos de funciones de String
            rcx, rdx = regs.get("rcx"), regs.get("rdx")
            ret = ("ret", t, ad)
            if t == F_STR_LIT:
                s = cstr(rcx)
                if s:
                    lastlit[0] = s
                ret = ("str", s)
            elif t == F_STR_CPY:
                ret = rdx if isinstance(rdx, tuple) and rdx[0] == "str" else ret
            elif t == F_STR_SET:
                if isinstance(rcx, tuple) and rcx[0] == "stk":
                    s = rdx[1] if isinstance(rdx, tuple) and rdx[0] == "str" else lastlit[0]
                    strobj[rcx] = s
            elif t == F_STR_NEW:
                s = cstr(rdx)
                if s:
                    lastlit[0] = s
                    if isinstance(rcx, tuple) and rcx[0] == "stk":
                        strobj[rcx] = s
                ret = rcx if isinstance(rcx, tuple) else ret
            for r in VOLATILE:
                regs.pop(r, None)
            regs["rax"] = ret
            continue
        if mn in ("jmp",) and ops and parse_int(ops[0]) in TARGETS:   # tail call
            t = parse_int(ops[0])
            ev.append((ad, t, {k: regs.get(k) for k in ("rcx", "rdx", "r8", "r9")},
                       {k: v for k, v in stk.items() if 0x20 <= k <= 0x98}, dict(strobj), lastlit[0]))
            continue
        if not ops:
            continue
        dst = ops[0]
        if mn in ("mov", "movabs") and len(ops) == 2:
            v = val(ops[1], comment_addr)
            if isinstance(v, int) and ops[1].startswith(("QWORD PTR [rip", "DWORD PTR [rip")):
                v = None
            if dst in R64:
                regs[R64[dst]] = v
                if v is None:
                    regs.pop(R64[dst], None)
            else:
                m = RE_MEM.match(dst)
                if m:
                    inner = m.group(1).replace(" ", "")
                    if inner.startswith("rsp+") or inner == "rsp":
                        d = parse_int(inner[4:]) if "+" in inner else 0
                        if v is None:
                            stk.pop(d, None)
                        else:
                            stk[d] = v
            continue
        if mn == "lea" and len(ops) == 2 and dst in R64:
            m = RE_MEM.match(ops[1])
            v = lea_val(m.group(1), comment_addr) if m else None
            if v is None:
                regs.pop(R64[dst], None)
            else:
                regs[R64[dst]] = v
            continue
        if mn in ("xor", "sub") and len(ops) == 2 and ops[0] == ops[1] and dst in R64:
            regs[R64[dst]] = 0
            continue
        if mn in ("add", "sub", "or", "and", "shl") and len(ops) == 2 and dst in R64:
            a = regs.get(R64[dst])
            b = val(ops[1], comment_addr)
            if isinstance(a, int) and isinstance(b, int):
                r = {"add": a + b, "sub": a - b, "or": a | b, "and": a & b, "shl": a << b}[mn]
                regs[R64[dst]] = r & 0xffffffffffffffff
            else:
                regs.pop(R64[dst], None)
            continue
        if mn == "and" and len(ops) == 2 and parse_int(ops[1]) == 0:
            m = RE_MEM.match(dst)
            if m and m.group(1).replace(" ", "").startswith("rsp+"):
                stk[parse_int(m.group(1).replace(" ", "")[4:])] = 0
            continue
        if mn in ("pop", "inc", "dec", "neg", "not", "movzx", "movsx", "movsxd", "cvttss2si", "movd", "movq",
                  "imul", "shr", "sar", "sete", "setne", "cmovne", "cmove", "xchg", "bsr", "bsf", "adc", "sbb",
                  "or", "xor", "and", "add", "sub", "shl", "rol", "ror") and dst in R64:
            regs.pop(R64[dst], None)
            continue
        # cualquier otra escritura a un registro general
        if dst in R64 and mn not in ("cmp", "test", "push", "bt"):
            regs.pop(R64[dst], None)
    return ev


# ---------------------------------------------------------------- pasada principal
def main():
    outp = sys.argv[1] if len(sys.argv) > 1 else "/home/claude/re/out/reflexion.tsv"
    sites = call_sites()
    chunks = {}
    for s, t in sites:
        f = func_of(s)
        if f and t in PROPLIKE:
            chunks[f] = 1
    rngs = sorted(chunks)
    print("call sites:", len(sites), "chunks:", len(rngs), file=sys.stderr)
    with Pool(4) as p:
        dis = dict(p.map(objdump, rngs, chunksize=8))
    events = []        # (chunk, sitio, destino, regs, stk, strobj, lastlit)
    for r in rngs:
        for e in emulate(dis[r]):
            events.append((r,) + e)
    print("eventos:", len(events), file=sys.stderr)

    # --- 1) getters/constructores de EntityInfo: chunk -> info; info -> tamano/categoria/tag
    getter_info = {}      # direccion de la funcion getter -> info
    info_meta = {}        # info -> dict
    regfn_of_info = {}
    for ch, site, t, rg, st, so, ll in events:
        if t == F_INFO_DYN and isinstance(rg["rcx"], int):
            info = rg["rcx"]
            getter_info.setdefault(ch[0], info)
            info_meta.setdefault(info, {}).update(size=rg["rdx"] if isinstance(rg["rdx"], int) else None,
                                                  category=cstr(rg["r8"]) or "", tag=rg["r9"], getter=ch[0], style="dyn")
        elif t == F_INFO_STA and isinstance(rg["rcx"], int):
            info = rg["rcx"]
            getter_info.setdefault(ch[0], info)
            nm = cstr(rg["rdx"])
            rf = st.get(0x40)
            info_meta.setdefault(info, {}).update(size=rg["r9"] if isinstance(rg["r9"], int) else None, name0=nm,
                                                  getter=ch[0], style="static",
                                                  regfn=rf if isinstance(rf, int) else None, category="")
            if isinstance(rf, int):
                regfn_of_info[rf] = info

    def resolve_info(v, ch):
        """valor de registro -> direccion del EntityInfo"""
        if isinstance(v, int):
            return v
        if isinstance(v, tuple):
            if v[0] == "ret" and v[1] in getter_info:
                return getter_info[v[1]]
            if v[0] == "ret" and v[1] is not None:
                return ("getter", v[1])
            if v[0] == "arg" and v[1] == "rcx":
                return regfn_of_info.get(ch[0], ("argof", ch[0]))
        return None

    # --- 2) nombres de clase
    info_name = {}
    for info, m in info_meta.items():
        if m.get("name0"):
            info_name[info] = m["name0"]
    for ch, site, t, rg, st, so, ll in events:
        if t in (F_REGCLASS, F_REGSTRUCT):
            info = resolve_info(rg["r8"], ch)
            nm = cstr(rg["rdx"])
            if nm and info is not None:
                info_name.setdefault(info, nm)
                info_meta.setdefault(info, {}).setdefault("regfn", ch[0])
                info_meta[info]["regfn_reg"] = ch[0]

    def cname(info):
        if info is None:
            return "?"
        if info in info_name:
            return info_name[info]
        if isinstance(info, tuple):
            return "?(%s %x)" % (info[0], info[1])
        return "?(info %x)" % info

    # --- 3) enums
    enums = {}           # nombre -> [(valor, nombre)]
    enum_by_site = {}
    cur_enum = {}
    for ch, site, t, rg, st, so, ll in events:
        if t == F_ENUMNEW:
            nm = cstr(rg["rdx"]) or "?"
            enums.setdefault(nm, [])
            cur_enum[ch] = nm
        elif t == F_ENUMVAL:
            nm = cur_enum.get(ch)
            if nm is not None:
                enums[nm].append((rg["rdx"] if isinstance(rg["rdx"], int) else "?", cstr(rg["r8"]) or ""))

    # --- 4) propiedades
    props = []           # dicts
    access = defaultdict(dict)   # info -> nombre -> (luaName, modo, handler)
    supers, versions = {}, {}
    pending_factory = {}         # chunk -> ultimo PropertyInfo creado por la fabrica
    GENERIC = {0x14026ba00, 0x140267440, 0x140268d20}   # cuerpo generico del motor (tablas en runtime)
    for ch, site, t, rg, st, so, ll in events:
        if ch[0] in GENERIC:
            continue
        if t in ADDPROP:
            spec = ADDPROP[t]

            def g(k):
                x = spec[k]
                if isinstance(x, int):
                    return x
                if x.startswith("s"):
                    return st.get(int(x[1:], 16))
                return rg[x]
            info = resolve_info(rg["rcx"], ch)
            nref = rg[spec["name"]]
            name = so.get(nref) if isinstance(nref, tuple) else None
            name = name or ll
            ty = g("type")
            off = g("off")
            arr = g("arr")
            en = cstr(g("enum")) if "enum" in spec else ""
            ptr = g("ptr") if "ptr" in spec else None
            props.append(dict(info=info, name=name, type=ty, off=off, arr=arr, cont=spec["cont"], enum=en or "",
                              ptr=ptr, virt=0, regfn=ch[0], site=site, kind="PROP"))
        elif t == F_META:
            # (info, embed, indice, nombre, offset, arraySize, tipo, p8, ptrClass, contenedor, nombreLua, modo,
            #  enum, ptrKind, handler, virtual)
            def sv(k, mask=0xffffffffffffffff):
                x = st.get(k)
                return (x & mask) if isinstance(x, int) else x
            info = resolve_info(rg["rcx"], ch)
            name = cstr(rg["r9"])
            emb = (rg["rdx"] or 0) & 0xff if isinstance(rg["rdx"], int) else 0
            virt = (sv(0x78, 0xff) or 0) & 1
            ty = sv(0x30, 0xffffffff)
            p = dict(info=info, name=name, type="embed" if emb else ty, off=sv(0x20, 0xffff), arr=sv(0x28, 0xffff),
                     cont=sv(0x48, 0xffffffff) or 0, enum=cstr(st.get(0x60)) or "",
                     ptr=resolve_info(st.get(0x40), ch) if st.get(0x40) else None, virt=virt, regfn=ch[0], site=site,
                     kind="EMBED" if emb else "PROP")
            props.append(p)
            mode = cstr(st.get(0x58))
            if name and mode:
                h = st.get(0x70)
                access[info][name] = (cstr(st.get(0x50)) or name, mode, h if isinstance(h, int) and h else None)
        elif t == F_FACTORY:
            nref = rg["rdx"]
            name = (so.get(nref) if isinstance(nref, tuple) else None) or ll
            c = st.get(0x20)
            pending_factory[ch] = dict(name=name, type=rg["rcx"], off=rg["r8"], arr=rg["r9"],
                                       cont=(c & 0xffffffff) if isinstance(c, int) else 0, enum="",
                                       ptr=st.get(0x30), site=site)
        elif t == F_ADDVIRT:
            pf = pending_factory.pop(ch, None)
            if pf:
                pf.update(info=resolve_info(rg["rcx"], ch), virt=1, regfn=ch[0], kind="PROP")
                props.append(pf)
        elif t == F_EMBED:
            info = resolve_info(rg["rcx"], ch)
            nref = rg["r8"]
            name = (so.get(nref) if isinstance(nref, tuple) else None) or ll
            props.append(dict(info=info, name=name, type="embed", off=rg["r9"], arr=1, cont=0, enum="",
                              ptr=resolve_info(rg["rdx"], ch), virt=0, regfn=ch[0], site=site, kind="EMBED"))
        elif t == F_ACCESS:
            info = resolve_info(rg["rcx"], ch)
            nm = cstr(rg["rdx"])
            lua = cstr(rg["r8"])
            mode = cstr(rg["r9"]) or ""
            h = st.get(0x20)
            if nm:
                access[info][nm] = (lua, mode, h if isinstance(h, int) and h else None)
        elif t == F_SUPER:
            supers[resolve_info(rg["rcx"], ch)] = resolve_info(rg["rdx"], ch)
        elif t == F_VERSION:
            versions[resolve_info(rg["rcx"], ch)] = rg["rdx"]

    # --- 5) expandir EMBED (struct embebido: props "%s_%s" con offset base+sub)
    by_info = defaultdict(list)
    for p in props:
        by_info[p["info"]].append(p)
    expanded = []

    def expand(p, depth=0):
        """props del struct embebido, recursivo (Transform -> rotation -> quat)"""
        out = []
        if depth > 4 or not isinstance(p["off"], int):
            return out
        for q in by_info.get(p["ptr"], []):
            if q["kind"] not in ("PROP", "EMBED"):
                continue
            r = dict(q, info=p["info"], name="%s_%s" % (p["name"], q["name"]),
                     off=(p["off"] + q["off"]) if isinstance(q["off"], int) else None,
                     regfn=p["regfn"], site=p["site"], kind="PROP_EMB" if q["kind"] == "PROP" else "EMBED")
            out.append(r)
            if q["kind"] == "EMBED":
                out += expand(r, depth + 1)
        return out
    for p in props:
        expanded.append(p)
        if p["kind"] == "EMBED":
            expanded += expand(p)

    def fmt(v, hexa=True):
        if v is None:
            return ""
        if isinstance(v, int):
            return hex(v) if hexa else str(v)
        return str(v)

    def tname(t):
        if isinstance(t, int) and 0 <= t < len(TYPES):
            return TYPES[t]
        return fmt(t)

    hdr = ["class", "property", "type", "offset", "arraySize", "container", "enum", "enum_values", "mode", "lua_name",
           "virtual", "ptr_class", "class_size", "super", "category", "regfn", "callsite", "kind"]
    rows = []
    for p in expanded:
        info = p["info"]
        a = access.get(info, {}).get(p["name"]) if p["name"] else None
        if a is None and p["kind"] == "PROP_EMB":
            a = access.get(info, {}).get(p["name"].split("_", 1)[0])
        m = info_meta.get(info, {}) if not isinstance(info, tuple) else {}
        ev = enums.get(p["enum"], []) if p["enum"] else []
        sup = supers.get(info)
        ptrc = ""
        if p["kind"] == "EMBED":
            ptrc = cname(p["ptr"])
        elif p.get("ptr") not in (None, 0):
            pi = resolve_info(p["ptr"], None) if isinstance(p["ptr"], tuple) else p["ptr"]
            ptrc = cname(pi) if pi in info_name else fmt(p["ptr"])
        rows.append([cname(info), p["name"] or "?", tname(p["type"]), fmt(p["off"]) if not p["virt"] else "virtual",
                     fmt(p["arr"], False), CONT.get(p["cont"], fmt(p["cont"])), p["enum"],
                     ",".join("%s=%s" % (n, v) for v, n in ev), a[1] if a else "", (a[0] or "") if a else "",
                     str(p["virt"]), ptrc, fmt(m.get("size")), cname(sup) if sup is not None else "",
                     m.get("category", ""), "%x" % p["regfn"], "%x" % p["site"], p["kind"]])
    # props declaradas solo con accesor (sin PropertyInfo en este exe)
    for info, d in access.items():
        known = {p["name"] for p in props if p["info"] == info}
        for nm, (lua, mode, h) in d.items():
            if nm not in known:
                m = info_meta.get(info, {}) if not isinstance(info, tuple) else {}
                sup = supers.get(info)
                rows.append([cname(info), nm, "", "", "", "", "", "", mode, lua or "", "", "",
                             fmt(m.get("size")), cname(sup) if sup is not None else "", m.get("category", ""),
                             "", "", "ACCESS_ONLY"])
    rows.sort(key=lambda r: (r[0], int(r[3], 16) if r[3].startswith("0x") else 1 << 40, r[1]))
    with open(outp, "w") as f:
        f.write("\t".join(hdr) + "\n")
        for r in rows:
            f.write("\t".join(r) + "\n")
    # clases
    cp = outp.replace(".tsv", "_clases.tsv")
    with open(cp, "w") as f:
        f.write("info\tclass\tsize\tsuper\tversion\tcategory\ttag\tgetter\tregfn\tstyle\tn_props\n")
        nprops = defaultdict(int)
        for r in rows:
            nprops[r[0]] += 1
        for info in sorted(set(list(info_meta) + list(info_name)), key=lambda x: str(x)):
            if isinstance(info, tuple):
                continue
            m = info_meta.get(info, {})
            sup = supers.get(info)
            f.write("\t".join([hex(info), cname(info), fmt(m.get("size")), cname(sup) if sup is not None else "",
                               fmt(versions.get(info), False), m.get("category", ""), fmt(m.get("tag")),
                               fmt(m.get("getter")), fmt(m.get("regfn_reg") or m.get("regfn")), m.get("style", ""),
                               str(nprops.get(cname(info), 0))]) + "\n")
    ep = outp.replace(".tsv", "_enums.tsv")
    with open(ep, "w") as f:
        f.write("enum\tvalues\n")
        for k in sorted(enums):
            f.write("%s\t%s\n" % (k, ",".join("%s=%s" % (n, v) for v, n in enums[k])))
    nofs = sum(1 for r in rows if r[17].startswith("PROP") and r[3] == "")
    nq = sum(1 for r in rows if r[0].startswith("?"))
    print("filas:", len(rows), "props:", sum(1 for r in rows if r[17].startswith("PROP")), "sin offset:", nofs,
          "clase ?:", nq, "virtuales:", sum(1 for r in rows if r[3] == "virtual"), file=sys.stderr)


if __name__ == "__main__":
    main()
