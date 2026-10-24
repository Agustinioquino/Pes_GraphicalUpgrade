import sys, re
sys.path.insert(0,'.')
from dumpmap import *
from fn import func_of
def search(rx, sec=".text"):
    b, d = DATA[sec]
    return [(b + m.start(), m.group(0)) for m in re.finditer(rx, d, re.S)]
if __name__ == "__main__":
    pats = {
     "or byte [r+0xf0],0x80": rb"[\x40-\x4f]?\x80[\x88-\x8f]\xf0\x00\x00\x00\x80",
     "or dword [r+0xf0],0x80": rb"[\x40-\x4f]?\x81[\x88-\x8f]\xf0\x00\x00\x00\x80\x00\x00\x00",
     "or dword [r+0xf0],imm (bit7)": rb"[\x40-\x4f]?\x81[\x88-\x8f]\xf0\x00\x00\x00[\x80-\xff]\x00\x00\x00|[\x40-\x4f]?\x81[\x88-\x8f]\xf0\x00\x00\x00[\x80-\xff][\x01\x03]\x00\x00",
     "bts [r+0xf0],7": rb"[\x40-\x4f]?\x0f\xba[\xa8-\xaf]\xf0\x00\x00\x00\x07",
     "and [r+0xf0],~0x180": rb"\x81[\xa0-\xa7]\xf0\x00\x00\x00\x7f\xfe\xff\xff",
    }
    for k, rx in pats.items():
        hits = search(rx)
        print(k, len(hits))
        for a, g in hits[:40]:
            f = func_of(a)
            print("   ", hex(a), g.hex(), "fn", hex(f[0]) if f else None)
