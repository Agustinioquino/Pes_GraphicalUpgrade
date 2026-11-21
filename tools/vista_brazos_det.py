import json,base64,numpy as np,sys
sys.path.insert(0,'/home/user/Pes_GraphicalUpgrade/tools')
import vista_juego as V
S=json.load(open(sys.argv[1])); NB=S['nb']
fr=[np.frombuffer(base64.b64decode(j['frames']),np.float32).reshape(-1,NB*12) for j in S['jugadores']]
def dec(m,k,dt,c): return np.frombuffer(base64.b64decode(m[k]),dt).reshape(-1,c)
ms=[]
for m in S['cuerpo']:
    if m['kind'] in ('boots','socks','shorts'): continue
    ms.append(dict(pos=dec(m,'pos',np.float32,3)[::2],nrm=dec(m,'nrm',np.float32,3)[::2],bi=dec(m,'bi',np.float32,4).astype(int)[::2],bw=dec(m,'bw',np.float32,4)[::2]))
inv={i:n for n,i in V.BI.items()}
def nombre(b):
    n=inv[b]; return 'mano'+n[1] if n.startswith('h') else n
for f in map(int,sys.argv[2].split(',')):
    P=[];N=[];B=[]
    for i in (0,1):
        x=[V.piel(m,fr[i][f]) for m in ms]; P.append(np.concatenate([a[0] for a in x])); N.append(np.concatenate([a[1] for a in x])); B.append(np.concatenate([m['bi'][:,0] for m in ms]))
    for i in (0,1):
        pa=P[i]; pb=P[1-i]; nb=N[1-i]; res={}
        best=np.full(len(pa),1e9); j=np.zeros(len(pa),int)
        for s in range(0,len(pb),2500):
            d2=((pa[:,None,:]-pb[None,s:s+2500,:])**2).sum(2); k=d2.argmin(1); v=d2[np.arange(len(pa)),k]; u=v<best; best[u]=v[u]; j[u]=k[u]+s
        pen=-np.einsum('vr,vr->v',pa-pb[j],nb[j]); pen[np.sqrt(best)>0.05]=0
        for v in np.where(pen>0.01)[0]:
            key=(nombre(B[i][v]),nombre(B[1-i][j[v]])); res[key]=max(res.get(key,0),pen[v])
        top=sorted(res.items(),key=lambda x:-x[1])[:6]
        print(f,'ABC'[i],' '.join('%s>%s %.1f'%(a,b,c*100) for (a,b),c in top))
