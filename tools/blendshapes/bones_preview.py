# offsets de huesos (m, ejes de la cabeza) por gesto a slider 1 para Messi: kPose + kPose2 + Secondary + caras.ini
import re,numpy as np
src=open(__import__('os').path.dirname(__import__('os').path.abspath(__file__))+'/../../src/exprbones.cpp').read()
names=[m for m in re.findall(r'\{\s*"(sk\w+)",\s*-?[\d.]+f',src)]
num=lambda s:[float(x.rstrip('f')) for x in re.findall(r'-?[\d.]+f?',s)]
G=['enojo','sonrisa','asombro','grito','puchero']
off={g:{} for g in G}
def add(g,d,v):
    n=names[d]; off[g][n]=off[g].get(n,np.zeros(3))+np.array(v)
blk=src[src.index('static const PoseOff kPose[]'):src.index('static void PoseOf')]
for m in re.finditer(r'\{\s*(\d+),\s*\{([^}]*)\},\s*\{([^}]*)\},\s*\{([^}]*)\}\s*\}',blk):
    d=int(m[1]);
    for g,s in zip(['enojo','sonrisa','asombro'],m.groups()[1:]):
        v=num(s); v=v if len(v)==3 else [0,0,0]; add(g,d,v)
blk=src[src.index('static const PoseOff2 kPose2[]'):src.index('enum { PG_NEUTRA')]
for m in re.finditer(r'\{\s*(\d+),\s*\{([^}]*)\},\s*\{([^}]*)\}\s*\}',blk):
    d=int(m[1])
    for g,s in zip(['grito','puchero'],m.groups()[1:]):
        v=num(s); v=v if len(v)==3 else [0,0,0]; add(g,d,v)
blk=src[src.index('static const Off kOff[] = {',src.index('static void Secondary')):]
blk=blk[:blk.index('};')]
km={3:'sonrisa',4:'enojo',5:'asombro',6:'grito',7:'puchero'}
for m in re.finditer(r'\{\s*(\d+),\s*(-?[\d.]+)f?,\s*(-?[\d.]+)f?,\s*(-?[\d.]+)f?,\s*(\d)\s*\}',blk):
    k=int(m[5])
    if k in km: add(km[k],int(m[1]),[float(m[2]),float(m[3]),float(m[4])])
for l in open(__import__('os').path.dirname(__import__('os').path.abspath(__file__))+'/../../package/PES21FX/caras.ini'):
    l=l.split(';')[0].strip()
    m=re.match(r'(\w+)\.(\w+)\s*=\s*(.*)',l)
    if m and m[1] in G:
        add(m[1],names.index('skf_'+m[2]),[float(x)/1000 for x in m[3].split()])
if __name__=='__main__':
    for g in G: print(g,{k[4:]:(v*1000).round(1).tolist() for k,v in off[g].items() if abs(v).sum()>0})
