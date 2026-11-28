# brazos y manos de cada jugador contra SU PROPIO cuerpo (camiseta, short, torso, muslos), por cuadro
import json,base64,numpy as np,sys
sys.path.insert(0,'/home/user/Pes_GraphicalUpgrade/tools')
import vista_juego as V
S=json.load(open(sys.argv[1])); NB=S['nb']; paso=int(sys.argv[2]) if len(sys.argv)>2 else 3
fr=[np.frombuffer(base64.b64decode(j['frames']),np.float32).reshape(-1,NB*12) for j in S['jugadores']]
def dec(m,k,dt,c): return np.frombuffer(base64.b64decode(m[k]),dt).reshape(-1,c)
inv={i:n for n,i in V.BI.items()}
def nombre(b):
    n=inv[b]; return 'mano'+n[1] if n.startswith('h') else n
ARM={'L':set([V.BI[b] for b in ('farmL','handL')]+[i for n,i in V.BI.items() if n.startswith('hL')]),'R':set([V.BI[b] for b in ('farmR','handR')]+[i for n,i in V.BI.items() if n.startswith('hR')])}
TOR=set(V.BI[b] for b in ('hips','pelvis','spine','chest','thighA','thighB','neck'))
arms={'L':[],'R':[]}; body=[]
for m in S['cuerpo']:
    if m['kind'] in ('boots','socks'): continue
    bi=dec(m,'bi',np.float32,4).astype(int); bw=dec(m,'bw',np.float32,4); pos=dec(m,'pos',np.float32,3); nrm=dec(m,'nrm',np.float32,3)
    for k in 'LR':
        sel=np.isin(bi[:,0],list(ARM[k]))&(bw[:,0]>0.6)
        if sel.sum(): arms[k].append(dict(pos=pos[sel][::2],nrm=nrm[sel][::2],bi=bi[sel][::2],bw=bw[sel][::2]))
    sel=np.isin(bi[:,0],list(TOR))&(bw[:,0]>0.5)
    if sel.sum() and m['kind'] in ('shirt','shorts'): body.append(dict(pos=pos[sel][::2],nrm=nrm[sel][::2],bi=bi[sel][::2],bw=bw[sel][::2]))
n=S['jugadores'][0]['n']; f0=int(sys.argv[3]) if len(sys.argv)>3 else 0; out=[]
for f in range(f0,n+1,paso):
    row=[f]
    for i in (0,1):
        b=[V.piel(m,fr[i][f]) for m in body]; pb=np.concatenate([x[0] for x in b]); nb=np.concatenate([x[1] for x in b]); bb=np.concatenate([m['bi'][:,0] for m in body])
        for k in 'LR':
            pa=np.concatenate([V.piel(m,fr[i][f])[0] for m in arms[k]]); ba=np.concatenate([m['bi'][:,0] for m in arms[k]])
            best=np.full(len(pa),1e9); j=np.zeros(len(pa),int)
            for s in range(0,len(pb),3000):
                d2=((pa[:,None,:]-pb[None,s:s+3000,:])**2).sum(2); kk=d2.argmin(1); v=d2[np.arange(len(pa)),kk]; u=v<best; best[u]=v[u]; j[u]=kk[u]+s
            pen=-np.einsum('vr,vr->v',pa-pb[j],nb[j]); pen[np.sqrt(best)>0.06]=0; w=pen.argmax()
            row.append((round(float(pen.max())*100,1), nombre(ba[w]), nombre(bb[j[w]])))
    out.append(row)
for r in out:
    if max(x[0] for x in r[1:])>1.0: print(r[0],' '.join('%s%s %.1f %s>%s'%('AB'[q//2],'LR'[q%2],x[0],x[1],x[2]) if x[0]>1 else '%s%s -'%('AB'[q//2],'LR'[q%2]) for q,x in enumerate(r[1:])))
print('cuadros medidos',len(out),'con algo > 1 cm:',sum(1 for r in out if max(x[0] for x in r[1:])>1.0))
