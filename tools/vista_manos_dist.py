import json,base64,numpy as np,sys
sys.path.insert(0,'/home/user/Pes_GraphicalUpgrade/tools')
import vista_juego as V
S=json.load(open(sys.argv[1])); NB=S['nb']
fr=[np.frombuffer(base64.b64decode(j['frames']),np.float32).reshape(-1,NB*12) for j in S['jugadores']]
def dec(m,k,dt,c): return np.frombuffer(base64.b64decode(m[k]),dt).reshape(-1,c)
HL=set([V.BI['handL']]+[V.BI['hL:'+n] for n in V.HAND_ASK]); HR=set([V.BI['handR']]+[V.BI['hR:'+n] for n in V.HAND_ASK])
manos={'L':[],'R':[]}; body=[]
for m in S['cuerpo']:
    if m['kind'] in ('boots','socks','shorts'): continue
    bi=dec(m,'bi',np.float32,4).astype(int); d=dict(pos=dec(m,'pos',np.float32,3)[::2],nrm=dec(m,'nrm',np.float32,3)[::2],bi=bi[::2],bw=dec(m,'bw',np.float32,4)[::2])
    b0=d['bi'][:,0]
    for k,H in (('L',HL),('R',HR)):
        sel=np.isin(b0,list(H))
        if sel.mean()>0.5: manos[k].append(d)
    if m['kind'] in ('shirt',) : body.append(d)
for f in map(int,sys.argv[2].split(',')):
    out=[]
    for i in (0,1):
        pb=np.concatenate([V.piel(m,fr[1-i][f])[0] for m in body])
        for k in 'LR':
            pa=np.concatenate([V.piel(m,fr[i][f])[0] for m in manos[k]])
            d=np.sqrt(((pa[:,None]-pb[None])**2).sum(2)).min(); out.append('%s%s %.1f'%('AB'[i],k,d*100))
    print(f,' '.join(out),'cm de la camiseta del otro')
