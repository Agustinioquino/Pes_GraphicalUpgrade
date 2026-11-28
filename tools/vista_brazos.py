# brazos (piel del brazo, manos, mangas) de cada uno contra el cuerpo del otro (torso, camiseta, brazos), por cuadro
import json,base64,numpy as np,sys
sys.path.insert(0,'/home/user/Pes_GraphicalUpgrade/tools')
import vista_juego as V
S=json.load(open(sys.argv[1])); NB=S['nb']; paso=int(sys.argv[2]) if len(sys.argv)>2 else 2
fr=[np.frombuffer(base64.b64decode(j['frames']),np.float32).reshape(-1,NB*12) for j in S['jugadores']]
def dec(m,k,dt,c): return np.frombuffer(base64.b64decode(m[k]),dt).reshape(-1,c)
ARMB=set(V.BI[b] for b in ('uarmL','farmL','handL','uarmR','farmR','handR'))|set(i for n,i in V.BI.items() if n.startswith('h'))
arms=[];body=[]
for m in S['cuerpo']:
    if m['kind'] in ('boots','socks','shorts'): continue
    bi=dec(m,'bi',np.float32,4).astype(int); bw=dec(m,'bw',np.float32,4); pos=dec(m,'pos',np.float32,3); nrm=dec(m,'nrm',np.float32,3)
    isarm=np.array([bi[v,0] in ARMB and bw[v,0]>0.4 for v in range(len(bi))])
    for sel,dst in ((isarm,arms),(~isarm,body)):
        if sel.sum()==0: continue
        d=dict(pos=pos[sel][::2],nrm=nrm[sel][::2],bi=bi[sel][::2],bw=bw[sel][::2]); dst.append(d)
# brazos del otro tambien cuentan como cuerpo duro
n=S['jugadores'][0]['n']; out=[]
for f in range(0,n+1,paso):
    PA=[];PB=[]
    for i in (0,1):
        a=[V.piel(m,fr[i][f]) for m in arms]; b=[V.piel(m,fr[i][f]) for m in body+arms]
        PA.append(np.concatenate([x[0] for x in a])); PB.append((np.concatenate([x[0] for x in b]),np.concatenate([x[1] for x in b])))
    peor=[0,0]
    for i in (0,1):
        pa=PA[i]; pb,nb=PB[1-i]; lo=pb.min(0)-.05; hi=pb.max(0)+.05; pa=pa[np.all((pa>lo)&(pa<hi),1)]
        if not len(pa): continue
        best=np.full(len(pa),1e9); j=np.zeros(len(pa),int)
        for s in range(0,len(pb),3000):
            d2=((pa[:,None,:]-pb[None,s:s+3000,:])**2).sum(2); k=d2.argmin(1); v=d2[np.arange(len(pa)),k]; u=v<best; best[u]=v[u]; j[u]=k[u]+s
        pen=-np.einsum('vr,vr->v',pa-pb[j],nb[j]); pen[np.sqrt(best)>0.05]=0; peor[i]=float(pen.max())
    out.append((f,peor))
json.dump(out,open(sys.argv[1]+'.brazos.json','w'))
mal=[(f,p) for f,p in out if max(p)>0.01]
print('cuadros con brazo dentro del otro (>1 cm):',len(mal),'de',len(out))
# tramos
tr=[];
for f,p in mal:
    if tr and f-tr[-1][1]<=paso: tr[-1][1]=f; tr[-1][2]=max(tr[-1][2],p[0]); tr[-1][3]=max(tr[-1][3],p[1])
    else: tr.append([f,f,p[0],p[1]])
for a,b,x,y in tr: print('  %d-%d: brazos A %.1f cm, brazos B %.1f cm'%(a,b,x*100,y*100))
