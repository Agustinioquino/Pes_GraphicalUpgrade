import sys;sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from load import *
from collections import Counter,defaultdict
MS=[load(k) for k in ['1379v','704v','353v']]
P=np.concatenate([m['v'] for m in MS]); off=np.cumsum([0]+[len(m['v']) for m in MS])
F=np.concatenate([m['f']+off[i] for i,m in enumerate(MS)])
key={};wid=np.zeros(len(P),int);U=[]
for i,p in enumerate(P):
    k=tuple(np.round(p*20000).astype(int))  # 0.05 mm
    if k not in key: key[k]=len(U);U.append(p)
    wid[i]=key[k]
U=np.array(U);WF=wid[F]
WF=WF[(WF[:,0]!=WF[:,1])&(WF[:,1]!=WF[:,2])&(WF[:,0]!=WF[:,2])]
# vertex normals (outward = reversed winding)
fn=np.cross(U[WF[:,2]]-U[WF[:,0]],U[WF[:,1]]-U[WF[:,0]])
N=np.zeros_like(U)
for j in range(3): np.add.at(N,WF[:,j],fn)
N/=np.linalg.norm(N,axis=1,keepdims=True)+1e-12
E=Counter()
for t in WF:
    for a,b in ((t[0],t[1]),(t[1],t[2]),(t[2],t[0])): E[tuple(sorted((a,b)))]+=1
adj=defaultdict(list)
for (a,b),c in E.items():
    if c==1: adj[a].append(b);adj[b].append(a)
seen=set();loops=[]
for s in adj:
    if s in seen: continue
    st=[s];comp=[]
    while st:
        x=st.pop()
        if x in seen: continue
        seen.add(x);comp.append(x);st+=adj[x]
    loops.append(np.array(comp))
