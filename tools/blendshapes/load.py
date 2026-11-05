import numpy as np,glob,re
D=__import__('os').environ.get('MALLAS','mallas')+'/'
def load(k):
    fn=glob.glob(D+'*'+k+'*.obj')[0];v=[];vt=[];fs=[]
    for l in open(fn):
        if l.startswith('v '):v.append(list(map(float,l.split()[1:4])))
        elif l.startswith('vt '):vt.append(list(map(float,l.split()[1:3])))
        elif l.startswith('f '):fs.append([int(p.split('/')[0])-1 for p in l.split()[1:4]])
    tn=glob.glob(D+'*'+k+'*.txt')[0];L=open(tn).read().splitlines()
    W=np.zeros((len(v),32))
    for l in L[2:]:
        p=l.split();i=int(p[0])
        for q in p[1:]:
            b,w=q.split(':');W[i,int(b)]+=float(w)
    head=int(L[0].split('cabeza')[1])
    return dict(v=np.array(v),uv=np.array(vt),f=np.array(fs),W=W,head=head,name=k)
src=open(__import__('os').path.dirname(__import__('os').path.abspath(__file__))+'/../../src/exprbones.cpp').read()
KD=[(m[0],np.array([float(m[1]),float(m[2]),float(m[3])])) for m in re.findall(r'\{\s*"(sk\w+)",\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f',src)]
