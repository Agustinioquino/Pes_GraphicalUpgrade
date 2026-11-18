# que parte de la ropa de uno se mete en la del otro (hueso de cada lado), por cuadro
import json,base64,numpy as np,sys
sys.path.insert(0,'/home/user/Pes_GraphicalUpgrade/tools')
import vista_juego as V
S=json.load(open(sys.argv[1])); NB=S['nb']
fr=[np.frombuffer(base64.b64decode(j['frames']),np.float32).reshape(-1,NB*12) for j in S['jugadores']]
def dec(m,k,dt,c): return np.frombuffer(base64.b64decode(m[k]),dt).reshape(-1,c)
inv={i:n for n,i in V.BI.items()}
ms=[]
for m in S['cuerpo']:
    if m['kind'] not in ('shirt','shorts'): continue
    ms.append(dict(pos=dec(m,'pos',np.float32,3)[::3],nrm=dec(m,'nrm',np.float32,3)[::3],bi=dec(m,'bi',np.float32,4).astype(int)[::3],bw=dec(m,'bw',np.float32,4)[::3]))
for f in map(int,sys.argv[2].split(',')):
    PN=[]
    for i in (0,1):
        x=[V.piel(m,fr[i][f]) for m in ms]; PN.append((np.concatenate([a for a,_ in x]),np.concatenate([b for _,b in x]),np.concatenate([m['bi'][:,0] for m in ms])))
    out=[]
    for a,b in ((0,1),(1,0)):
        pa,_,ba=PN[a]; pb,nb,bb=PN[b]
        lo=pb.min(0)-0.05; hi=pb.max(0)+0.05; sel=np.all((pa>lo)&(pa<hi),1); pa=pa[sel]; ba=ba[sel]
        best=np.full(len(pa),1e9); j=np.zeros(len(pa),int)
        for s in range(0,len(pb),3000):
            d2=((pa[:,None,:]-pb[None,s:s+3000,:])**2).sum(2); kk=d2.argmin(1); v=d2[np.arange(len(pa)),kk]; u=v<best; best[u]=v[u]; j[u]=kk[u]+s
        pen=-np.einsum('vr,vr->v',pa-pb[j],nb[j]); pen[np.sqrt(best)>0.06]=0
        res={}
        for k in np.where(pen>0.01)[0]:
            key=(inv[ba[k]],inv[bb[j[k]]]); res[key]=max(res.get(key,0),pen[k])
        out.append(' '.join('%s>%s %.1f'%(x,y,100*v) for (x,y),v in sorted(res.items(),key=lambda t:-t[1])[:5]))
    print(f,'A en B:',out[0],'| B en A:',out[1])
