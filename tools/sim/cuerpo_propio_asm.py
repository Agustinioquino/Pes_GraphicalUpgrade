# PES21 FX (15/11) - comprueba sin el juego el modulo package/sider/cuerpo_propio/CuerpoPropio.lua:
#   1) carga el Lua con solo los globales que da el Sider de Agustin (sin pcall, select, error, next, setmetatable...)
#   2) arma el codigo propio (m.codigo) y lo desensambla: los saltos tienen que caer en 'usar' y 'fin', sin bytes sueltos
#   3) lee una lista de prueba con la misma expresion del modulo
# Uso: python3 tools/sim/cuerpo_propio_asm.py     (necesita lupa y capstone)
import os, sys, capstone
try:
    from lupa import luajit21 as lupa
except ImportError:
    import lupa
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = open(os.path.join(HERE, '..', '..', 'package', 'sider', 'cuerpo_propio', 'CuerpoPropio.lua'), encoding='utf-8').read()
lua = lupa.LuaRuntime(encoding=None, unpack_returned_tuples=True)
logs = []
lua.globals()[b'__log'] = lambda s: logs.append(s)
lua.globals()[b'__src'] = SRC.encode('utf-8')
m = lua.execute(b'''
local env = { assert = assert, io = io, ipairs = ipairs, pairs = pairs, log = __log, math = math, string = string, table = table,
              tonumber = tonumber, tostring = tostring, type = type, os = os, ffi = require("ffi") }
local chunk = assert(loadstring(__src, "CuerpoPropio.lua"))
setfenv(chunk, env)
return chunk()
''')
BASE = 0x13FDC0000
code = bytes(m.codigo(BASE))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
ins = list(md.disasm(code, BASE))
tot = sum(i.size for i in ins)
for i in ins: print('%02x: %-22s %s %s' % (i.address - BASE, i.bytes.hex(), i.mnemonic, i.op_str))
assert tot == len(code), 'bytes sin desensamblar: %d de %d' % (tot, len(code))
starts = {i.address for i in ins}
by = {i.address - BASE: i for i in ins}
fin = [i.address for i in ins if i.mnemonic == 'add' and i.op_str == 'rsp, 0x28'][0]
usar = [i.address for i in ins if i.mnemonic == 'test' and i.op_str == 'ecx, ecx'][0]
poner = [i.address for i in ins if i.mnemonic == 'inc' and '0x10' in i.op_str][0]
saltos = [i for i in ins if i.mnemonic in ('je', 'ja', 'jae', 'jne', 'jmp') and i.op_str.startswith('0x')]
afuera = [i.address for i in ins if i.mnemonic == 'movabs' and i.op_str == 'rax, 0x1437d9af0'][0]
forma = [i.address for i in ins if i.mnemonic == 'mov' and i.op_str == 'r9, qword ptr [r15 + 0x10]'][1]
assert len(saltos) == 15, len(saltos)
for k, i in enumerate(saltos):
    t = int(i.op_str, 16)
    assert t in starts, 'salto al medio de una instruccion'
    assert t == (usar if i.mnemonic == 'jae' else afuera if i.mnemonic == 'jne' else forma if (i.mnemonic == 'jmp' or afuera <= i.address < forma)
                 else poner if k == 14 else fin), 'salto %d a %x' % (k, t - BASE)
txt = '\n'.join(str(i.mnemonic) + ' ' + str(i.op_str) for i in ins)
for need in ('sub rsp, 0x28', 'movabs r10, 0x%x' % (BASE + 0x800), 'cmp eax, 3', 'cmp eax, 0x100000', 'movabs r11, 0x%x' % (BASE + 0x91000),
             'movabs r11, 0x%x' % (BASE + 0x1000), 'movabs r11, 0x%x' % (BASE + 0xc00), 'mov r9, qword ptr [r15 + 0x10]',
             'mov eax, dword ptr [r9 + 0x160]', 'and eax, 7', 'lea eax, [rax + rcx*8]', 'shl eax, 2', 'add eax, edx', 'cmp dword ptr [r9 + 0x64], 0', 'mov edx, dword ptr [r9 + 0x60]', 'movabs rax, 0x1419ab080', 'cmp byte ptr [rax + 0x18], 0', 'mov dword ptr [r10 + 0x24], edx', 'mov rdx, qword ptr [r11 + rax*8]',
             'test byte ptr [r11 + rcx], 1', 'and dword ptr [r9 + 0x44], 0xfffffff7', 'mov rcx, r15', 'movabs rax, 0x141a09fe0', 'call rax',
             'movabs rax, 0x141ea44d0', 'jmp rax', 'mov eax, dword ptr [rbp - 0x5c]', 'mov ecx, dword ptr [rbp - 0x60]'):
    assert need in txt, 'falta: ' + need
assert ins[-1].mnemonic == 'jmp' and ins[-2].op_str.endswith('0x141ea44d0') and ins[-3].op_str == 'rsp, 0x28'
print('codigo OK: %d bytes, %d instrucciones' % (len(code), len(ins)))

# ---------------------------------------------------------------- 4) arranque completo con un Windows de mentira
# Memoria del juego = bytes reales del volcado (exe.py sobre db.2.gbf, si esta: GBF=...); el bloque propio = memoria real del
# proceso reservada cerca de la direccion pedida (mmap), asi el Lua escribe y lee de verdad.
import tempfile, struct
GBF = os.environ.get('GBF')
if not GBF or not os.path.exists(GBF):
    print('sin GBF: no se prueba el arranque'); sys.exit(0)
sys.path.insert(0, '/mnt/project-files/decompilado/herramientas/leer_exe')
import exe
tmp = tempfile.mkdtemp()
LISTA = "# prueba\n* = /Assets/pes16/model/character/parts/naked/scenes/naked_body.fmdl\r\n7511 = /Assets/pes16/model/character/cuerpo/cuerpo_a_#.fmdl\n40352 = no\n  99 =   /Assets/pes16/model/character/cuerpo/cuerpo_a_#.fmdl  \n; 5 = /x\nbasura\n2000000 = /fuera/de/rango.fmdl\n# 6 = /y\n123 = /Assets/pes16/model/character/cuerpo/cuerpo_b_#.fmdl | sin_brazo\n555 = /Assets/pes16/model/character/cuerpo/cuerpo_@_#.fmdl\n"
open(os.path.join(tmp, 'modules\\cuerpo_propio.txt'), 'w', newline='').write(LISTA)   # en Linux la barra de Windows es parte del nombre
parche = {}
def juego(d, n):
    d = int(d); b = bytearray(exe.leer(d, n))
    for k, v in parche.items():
        if d <= k < d + n: b[k - d] = v
    return bytes(b)
def escribir(d, b):
    for i, x in enumerate(bytes(b)): parche[int(d) + i] = x
lua.globals()[b'__juego'] = juego
lua.globals()[b'__escribir'] = escribir
lua.globals()[b'__dir'] = (tmp + '/').encode()
r = lua.execute(b'''
local real = require("ffi")
real.cdef[[ void *mmap(void *a, size_t n, int prot, int flags, int fd, long off); ]]
local bloque = { ini = nil, fin = nil }
local Cf = {}
function Cf.cpo_GetCurrentProcess() return 1 end
function Cf.cpo_VirtualProtect(dir, n, prot, antes) antes[0] = 0x20; return 1 end
function Cf.cpo_FlushInstructionCache() return 1 end
function Cf.cpo_VirtualAlloc(a, n, tipo, prot)
    local p = real.C.mmap(real.cast("void *", a), n, 7, 0x22 + 0x100000, -1, 0)      -- MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE
    local d = tonumber(real.cast("uintptr_t", p))
    if d ~= a then return real.cast("uintptr_t", 0) end
    bloque.ini, bloque.fin = a, a + n
    return real.cast("uintptr_t", a)
end
function Cf.cpo_ReadProcessMemory(h, dir, buf, n, hecho)
    if bloque.ini and dir >= bloque.ini and dir + n <= bloque.fin then real.copy(buf, real.cast("uint8_t *", dir), n)
    else local s = __juego(dir, n); real.copy(buf, s, n) end
    hecho[0] = n; return 1
end
function Cf.cpo_WriteProcessMemory(h, dir, buf, n, hecho) __escribir(dir, real.string(buf, n)); hecho[0] = n; return 1 end
local ffi_falso = { cdef = function() end, new = real.new, cast = real.cast, string = real.string, copy = real.copy, fill = real.fill, C = Cf }
local env = { assert = assert, io = io, ipairs = ipairs, pairs = pairs, log = __log, math = math, string = string, table = table,
              tonumber = tonumber, tostring = tostring, type = type, os = os, ffi = ffi_falso }
local chunk = assert(loadstring(__src, "CuerpoPropio.lua"))
setfenv(chunk, env)
local m = chunk()
local reg = {}
m.init({ sider_dir = __dir, register = function(n, f) reg[n] = f end })
local o1 = m.overlay_on({})
m.key_down({}, 0x32); local o2 = m.overlay_on({})
m.key_down({}, 0x32); m.key_down({}, 0x33); local o3 = m.overlay_on({})
return o1, o2, o3, bloque.ini, (reg.key_down and reg.overlay_on) and 1 or 0
''')
o1, o2, o3, base, regs = r
for l in logs: print('  log:', l.decode() if isinstance(l, bytes) else l)
print(o1.decode()); print(o2.decode()); print(o3.decode())
assert regs == 1 and b"activo" in o1 and b"PRENDIDO" in o1 and b"apagado" in o2 and b"PRENDIDO" in o3 and o1.count(b"\n") == 2 and b"tono 0" in o1
base = int(base)
import ctypes
mem = lambda off, n: ctypes.string_at(base + off, n)
# el desvio
SITIO = 0x141ecd521
new = juego(SITIO, 5)
assert new[0] == 0xE8 and SITIO + 5 + struct.unpack('<i', new[1:])[0] == base, 'el call no apunta al bloque'
assert set(parche) == {SITIO + 1, SITIO + 2, SITIO + 3, SITIO + 4}, 'se escribio fuera del call'
cod = bytes(m.codigo(base))
assert mem(0, len(cod)) == cod and len(cod) < 0x800
on, defecto = struct.unpack('<II', mem(0x800, 8))
p = struct.unpack('<8192Q', mem(0x1000, 0x10000))
ruta = lambda i, tono=1, forma=0: ctypes.string_at(p[(i * 8 + tono) * 4 + forma]).decode() if p[(i * 8 + tono) * 4 + forma] else None
tab = lambda i: mem(0x91000 + i, 1)[0]
band = lambda i: mem(0xc00 + i, 1)[0]
assert on == 1 and ruta(defecto).endswith('naked_body.fmdl') and all(ruta(defecto, t) == ruta(defecto) for t in range(8)), (on, defecto)
assert [ruta(tab(7511), t).rsplit('_', 1)[1] for t in range(8)] == ['1.fmdl', '1.fmdl', '2.fmdl', '3.fmdl', '4.fmdl', '5.fmdl', '6.fmdl', '1.fmdl']
assert tab(99) == tab(7511) and tab(40352) == 255 and tab(5) == 0 and tab(6) == 0 and tab(7512) == 0
assert band(tab(123)) == 1 and band(tab(7511)) == 0 and band(defecto) == 0 and ruta(tab(123), 4).endswith('cuerpo_b_4.fmdl')
assert all(x == 0 or base + 0x11000 <= x < base + 0x91000 for x in p) and len({x for x in p if x}) == 1 + 6 + 6 + 6
assert [ruta(tab(555), 2, f).rsplit('/', 1)[1] for f in range(4)] == ['cuerpo_a_2.fmdl', 'cuerpo_b_2.fmdl', 'cuerpo_c_2.fmdl', 'cuerpo_a_2.fmdl']
assert all(ruta(tab(7511), 3, f) == ruta(tab(7511), 3) for f in range(4))
print('arranque OK: bloque 0x%X, call -> bloque, lista bien cargada (para todos = %s)' % (base, ruta(defecto)))

# ---------------------------------------------------------------- 5) el codigo propio EJECUTADO de verdad
# Se ponen dos funciones de mentira en las direcciones del juego (0x141a09fe0 anota config y ruta; 0x141ea44d0 devuelve 0x1234)
# y se llama al bloque como lo llama el juego: rbp = marco con el id en [rbp-0x60] y el tipo en [rbp-0x5c], r15 = config.
libc = ctypes.CDLL(None, use_errno=True)
libc.mmap.restype = ctypes.c_void_p
libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_long]
def pagina(d):
    r = libc.mmap(d & ~0xfff, 0x1000, 7, 0x22 | 0x100000, -1, 0)
    assert r == d & ~0xfff, 'no pude poner una pagina en 0x%x' % d
ANOTA = base + 0x900
pagina(0x141a09fe0); pagina(0x141ea44d0); pagina(0x1437d9af0); pagina(0x1419ab080)
# lista de kits de mentira: kit 77 = ajustado, kit 78 = normal, otro = no esta. La busqueda de mentira pisa r8-r11 y rcx (como podria la real)
kits = ctypes.create_string_buffer(0x100)
KITS = ctypes.addressof(kits)
struct.pack_into('<Q', kits, 8, 0x1111)                    # lista+8 != 0
struct.pack_into('<Q', kits, 0x28, KITS + 0x40); struct.pack_into('<B', kits, 0x58, 1)      # nodo 77 en +0x20: [+8] = K (+0x40), K+0x18 = 1
struct.pack_into('<Q', kits, 0x68, KITS + 0x80); struct.pack_into('<B', kits, 0x98, 0)      # nodo 78 en +0x60: [+8] = K (+0x80), K+0x18 = 0
def kits_puestos(si): ctypes.memmove(0x1437d9af0, struct.pack('<Q', KITS if si else 0), 8)
kits_puestos(True)
busca = (b'\x4D\x31\xC9\x4D\x31\xD2\x4D\x31\xDB\x31\xC9\x4D\x31\xC0'
         + b'\x83\xFA\x4D\x75\x0B\x48\xB8' + struct.pack('<Q', KITS + 0x20) + b'\xC3'
         + b'\x83\xFA\x4E\x75\x0B\x48\xB8' + struct.pack('<Q', KITS + 0x60) + b'\xC3' + b'\x31\xC0\xC3')
ctypes.memmove(0x1419ab080, busca, len(busca))
anota = b'\x48\x89\xC8\x48\xA3' + struct.pack('<Q', ANOTA) + b'\x48\x89\xD0\x48\xA3' + struct.pack('<Q', ANOTA + 8) + b'\xC3'
ctypes.memmove(0x141a09fe0, anota, len(anota))
ctypes.memmove(0x141ea44d0, b'\xB8\x34\x12\x00\x00\xC3', 6)
TRAMPA = base + 0xA00    # push rbp / push r15 / push rbx / mov rbp, rdi / mov r15, rsi / call rdx / pop rbx / pop r15 / pop rbp / ret
tr = b'\x55\x41\x57\x53\x48\x89\xFD\x49\x89\xF7\xFF\xD2\x5B\x41\x5F\x5D\xC3'
ctypes.memmove(TRAMPA, tr, len(tr))
llamar = ctypes.CFUNCTYPE(ctypes.c_uint64, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)(TRAMPA)
marco = ctypes.create_string_buffer(0x100)
RBP = ctypes.addressof(marco) + 0x80
cfg = ctypes.create_string_buffer(0x20)                    # config: +0x10 -> C
datos = ctypes.create_string_buffer(0x200)                 # C: +0x44 mascara de unidades, +0x160 tono
CONFIG = ctypes.addressof(cfg)
struct.pack_into('<Q', cfg, 0x10, ctypes.addressof(datos))
def caso(tipo, ident, tono=3, afuera=1, kit=0):
    ctypes.memset(ANOTA, 0, 16)
    struct.pack_into('<II', marco, 0x80 - 0x60, ident & 0xffffffff, tipo)
    struct.pack_into('<II', datos, 0x44, 0xffffffff, 0xffffffff)
    struct.pack_into('<I', datos, 0x160, tono)
    struct.pack_into('<II', datos, 0x60, kit, afuera)
    ret = llamar(RBP, CONFIG, base)
    assert ret & 0xffffffff == 0x1234, 'no siguio al destino original'
    c, r = struct.unpack('<QQ', mem(0x900, 16))
    assert c in (0, CONFIG) and (c == 0) == (r == 0)
    masc, otro = struct.unpack_from('<II', datos, 0x44)
    assert otro == 0xffffffff and masc in (0xffffffff, 0xfffffff7)
    return (ctypes.string_at(r).decode().rsplit('/', 1)[1] if r else None), masc == 0xfffffff7
N, C6 = 'naked_body.fmdl', 'cuerpo_a_%d.fmdl'
esperado = [((0, 7511, 3), (C6 % 3, False)), ((3, 99, 6), (C6 % 6, False)), ((0, 7511, 0), (C6 % 1, False)), ((0, 7511, 7), (C6 % 1, False)),
            ((0, 12345, 2), (N, False)), ((1, 40352, 2), (None, False)), ((4, 7511, 2), (None, False)), ((0x13, 123, 2), (None, False)),
            ((0, 0x80000001, 5), (N, False)), ((2, 0, 1), (N, False)), ((1, 123, 4), ('cuerpo_b_4.fmdl', True)), ((0, 123, 0x1f), ('cuerpo_b_1.fmdl', True))]
for a_, e in esperado:
    g = caso(*a_); print('  tipo %2d id %10d tono %2d -> %s%s' % (a_ + (g[0], ' + sin brazo del juego' if g[1] else ''))); assert g == e, (a_, g, e)
paso, puso, ut, ui, utono = struct.unpack('<QQIII', mem(0x808, 28))
assert (paso, puso, ut, ui, utono) == (12, 9, 0, 123, 7), (paso, puso, ut, ui, utono)
# (0.3) forma del torso: afuera / adentro / ajustada, kit que no esta, lista de kits vacia
F = 'cuerpo_%s_%d.fmdl'
formas = [((0, 555, 2, 1, 78), F % ('a', 2)), ((0, 555, 2, 0, 78), F % ('b', 2)), ((0, 555, 2, 0, 77), F % ('b', 2)), ((0, 555, 5, 1, 77), F % ('c', 5)),
          ((0, 555, 4, 1, 5), F % ('a', 4)), ((0, 555, 6, 7, 77), F % ('c', 6)), ((0, 7511, 3, 1, 77), 'cuerpo_a_3.fmdl'), ((0, 123, 4, 0, 77), 'cuerpo_b_4.fmdl')]
for a_, e in formas:
    g = caso(*a_); print('  id %5d tono %d afuera %d kit %2d -> %s' % (a_[1:] + (g[0],))); assert g[0] == e, (a_, g, e)
assert struct.unpack('<I', mem(0x824, 4))[0] == 1          # ultima forma anotada (123 con remera adentro)
kits_puestos(False)
assert caso(0, 555, 5, 1, 77)[0] == F % ('a', 5) and caso(0, 555, 5, 0, 77)[0] == F % ('b', 5)
kits_puestos(True); struct.pack_into('<Q', kits, 8, 0)
assert caso(0, 555, 5, 1, 77)[0] == F % ('a', 5)
struct.pack_into('<Q', kits, 8, 0x1111); struct.pack_into('<Q', kits, 0x28, 0)
assert caso(0, 555, 5, 1, 77)[0] == F % ('a', 5)
struct.pack_into('<Q', kits, 0x28, KITS + 0x40)
assert caso(0, 555, 5, 1, 77)[0] == F % ('c', 5)
ctypes.memmove(base + 0x800, struct.pack('<I', 0), 4)          # apagado (tecla 2)
assert caso(0, 7511) == (None, False) and caso(1, 123) == (None, False)
print('ejecucion OK: 14 casos, contadores bien, siempre sigue al destino original')
