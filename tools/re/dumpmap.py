# mapa de secciones del volcado de PES2021.exe 1.07.02 (base 0x140000000)
import os, re, struct
D = os.environ.get("PES_DUMP", "/mnt/user-data/uploads/eFootball PES 2021/PES21FX/dump/")   # carpeta del volcado (exedump.cpp)
BASE = 0x140000000
SECS = [(".text", 0x1000, "_text.bin"), (".idata", 0x2538000, "_idata.bin"), (".text1", 0x33A9000, "_text1.bin"),
        (".pdata", 0x3AD3000, "_pdata.bin"), (".data2", 0x3CB0000, "_data2.bin"), (".data", 0x1B479000, "_data.bin")]
DATA = {}
for n, va, f in SECS:
    DATA[n] = (BASE + va, open(D + f, "rb").read())
def sec_of(addr):
    for n, (b, d) in DATA.items():
        if b <= addr < b + len(d): return n, addr - b
    return None, None
def read(addr, n):
    s, o = sec_of(addr)
    if s is None: return None
    return DATA[s][1][o:o + n]
def find_all(sec, pat):
    b, d = DATA[sec]
    return [b + m.start() for m in re.finditer(re.escape(pat), d)]
def str_addrs(s):
    out = []
    for n in DATA:
        out += find_all(n, s.encode() + b"\0")
    return out
TEXT_BASE, TEXT = DATA[".text"]
def rip_refs(target, sec=".text"):
    """posiciones de instrucciones con [rip+disp32] que apuntan a target (lea/mov/cmp...): busca el disp32"""
    b, d = DATA[sec]
    res = []
    # disp32 = target - (ip_after); ip_after = pos_disp + 4 + k (k = bytes de inmediato despues, 0 para lea/mov)
    for k in (0, 1, 4):
        # recorrer todas las posiciones donde el disp32 encaja: target - (b+i+4+k) == disp
        pass
    arr = d
    # escaneo vectorizado simple
    import numpy as np
    a = np.frombuffer(arr[:len(arr) - (len(arr) % 1)], dtype=np.uint8)
    return res
