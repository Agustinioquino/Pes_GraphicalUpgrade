import sys,struct,collections
sys.path.insert(0,'/home/user/Pes_GraphicalUpgrade/tools/re')
from dumpmap import read
def cstr(a):
    b=read(a,200); return b[:b.index(b'\0')].decode()
cls=[]
for l in open('classes.txt'):
    p=l.split(); c=p[0].split('::')[1]
    for a in p[1:]: cls.append((int(a,16),c))
cls.sort()
ents=[]
for i in range(0xea):
    pa,fa=struct.unpack('<QQ',read(0x142b54d80+16*i,16))
    best=None
    for a,c in cls:
        if a<=fa and (best is None or a>best[0]): best=(a,c)
    ents.append((i,cstr(pa),fa,best[1] if best else None,fa-best[0] if best else None))
if __name__=='__main__':
    for e in ents: print(e[0],e[1].split('constant/')[1],hex(e[2]),e[3],hex(e[4]))
