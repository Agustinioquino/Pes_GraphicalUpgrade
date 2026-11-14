# extrae tablas de propiedades (nombre, tipo, offset) de las funciones de registro descompiladas
import sys,re; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from dumpmap import *
def cstr(a):
    try:
        s=read(a,200)
    except Exception: return "?"
    return s.split(b"\0")[0].decode(errors='replace')
src=open(sys.argv[1]).read()
funcs=re.split(r"^// ==== ",src,flags=re.M)[1:]
types={4:"enum",5:"int",8:"float",10:"bool",0x16:"string",1:"?1",2:"?2",3:"?3",6:"?6",7:"?7",9:"?9",0xb:"?b",0xc:"?c",0xd:"?d",0xe:"?e",0xf:"?f",0x10:"?10",0x11:"?11",0x12:"?12",0x13:"?13",0x14:"?14",0x15:"?15"}
for f in funcs:
    head=f.split("\n",1)[0]
    name=None; rows=[]
    m=re.search(r"func_0x00014026bf00\(&puStackX_8,&UNK_([0-9a-f]+)",f)
    cls=cstr(int(m.group(1),16)) if m else "?"
    last=None
    for line in f.split("\n"):
        m=re.search(r"func_0x0001403d2990\(&UNK_([0-9a-f]+),2\)",line)
        if m: last=cstr(int(m.group(1),16)); continue
        m=re.search(r"func_0x00014026(6f70|7080|6d40)\(lVar\d+,(0x[0-9a-f]+|\d+),&puStackX_8(?:,(0x[0-9a-f]+|\d+))?(?:,&UNK_([0-9a-f]+))?",line)
        if m:
            t=int(m.group(2),0); off=int(m.group(3),0) if m.group(3) else -1
            enum=cstr(int(m.group(4),16)) if m.group(4) else ""
            rows.append((off,types.get(t,hex(t)),last,enum)); last=None
    if rows:
        print("== %s  clase %s  (%d props)"%(head.split(" (")[0],cls,len(rows)))
        for off,t,n,e in rows: print("   +0x%03x %-6s %s %s"%(off,t,n,("["+e+"]") if e else ""))
