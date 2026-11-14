# paleta -> hueso, a mano por pares simetricos (cantidad de vertices) y centroides (x + = izquierda del jugador)
def S(n,a,b,cl):  # a,b = indices; el de x + es _l
    return {a:n+('_l' if cl[a]>0 else '_r'), b:n+('_r' if cl[a]>0 else '_l')}
MAPS={}
import sys;sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from load import *
def cx(M):
    out={}
    for p in range(32):
        w=M['W'][:,p]
        if (w>0.05).sum(): ww=w**4; out[p]=((M['v'][:,0]*ww).sum()/ww.sum())
    return out
def build():
    for k,pairs,single in [
      ('1379v',[('nasolabialfold_t',0,9),('orbicularisoculi_b',1,15),('brow_o',5,16),('eyelid_b',6,14),('eyelid_t',8,21),('brow_i',12,18),('nosewing',13,20)],
               {7:'glabella',22:'nose',4:'sk_head'}),
      ('704v',[('lip_s',8,27),('lip_t',18,28),('lip_b',15,22),('nasolabialfold_t',0,10),('nosewing',14,24),('cheek',13,19),('orbicularisoculi_b',1,17),
               ('cheek_s',2,23),('eyelid_b',6,16),('eyelid_t',9,25),('brow_o',4,20),('brow_i',12,21)],
               {11:'lip_t_c',26:'lip_volume',29:'nose',7:'glabella',5:'skf_jaw',3:'sk_head'}),
      ('353v',[('lip_s',6,13),('lip_b',9,10),('cheek_s',0,11)],{12:'lip_b_c',5:'skf_jaw',1:'doublechin',4:'sk_head'})]:
        M=load(k);c=cx(M);mp={}
        for n,a,b in pairs: mp.update(S(n,a,b,c))
        mp.update(single)
        MAPS[k]={p:(n if n.startswith('sk') else 'skf_'+n) for p,n in mp.items()}
    return MAPS
build()
if __name__=='__main__':
    for k,m in MAPS.items(): print(k,m)
