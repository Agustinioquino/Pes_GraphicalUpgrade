# (04/11 c) retoques sobre la sonrisa de siempre (messi5) y la sonrisa grande de Agustin (messi6 2.blend, "la sonrisa mas alta"):
#   - cejas RIGIDAS con la sonrisa (Agustin: "revisa las cejas en el slider de sonrisa, pq sube pero se estiran"): en la franja de la
#     ceja cada columna sube parejo (lo que sube la base de la ceja en esa x) y casi no se corre a los lados; la frente de arriba
#     se estira en su lugar (sin pelo pintado)
#   - sonrisa grande: las comisuras suben lo mismo que en la de siempre (Agustin, foto de Messi riendo: "el angulo de las puntas
#     de la sonrisa no suben tanto, abre mas la boca"; la boca la abre la mandibula por huesos, exprbones.cpp)
# Entrada: B.npy, S5.npy (sonrisa de siempre), S6.npy (sonrisa grande), en ejes del juego y orden de vertices de Blender.
# Salida: S5b.npy, S6b.npy y una vista. Uso: MALLAS=<dir> python3 retoque_sonrisa_grande.py <dir> <vista.png>
import sys;sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from shapes import *
from render2 import rend
from PIL import Image
d0=sys.argv[1];B=np.load(d0+'/B.npy');S5=np.load(d0+'/S5.npy');S6=np.load(d0+'/S6.npy')
assert np.abs(B-P).max()<2e-5
def weld(S):
    D=np.zeros_like(U);n=np.zeros(len(U));np.add.at(D,wid,S-B);np.add.at(n,wid,1);return D/n[:,None]
front=np.clip((U[:,2]-0.11)/0.02,0,1)
ax=np.abs(U[:,0])
# franja de la ceja (Messi: cabeza x 0.8 cm, cola 5.8 cm, alto 1.703..1.724)
mb=fall(np.sqrt(((ax-0.033)/0.034)**2+((U[:,1]-1.7135)/0.0125)**2))*front
base=(U[:,1]>1.703)&(U[:,1]<1.712)&(mb>0.3)   # la base de la ceja manda cuanto sube cada columna
def cejas(D):
    D=D.copy();T=D.copy()
    for i in np.where(mb>0.01)[0]:
        sel=base&(np.sign(U[:,0])==np.sign(U[i,0]))
        w=fall(np.abs(U[sel,0]-U[i,0])/0.008)
        if w.sum()<1e-6: continue
        T[i,1]=(D[sel,1]*w).sum()/w.sum()
        T[i,0]=0.25*(D[sel,0]*w).sum()/w.sum()
    return D+(T-D)*mb[:,None]
D5=weld(S5);D6=weld(S6)
D5b=cejas(D5);D6b=cejas(D6)
# comisuras de la sonrisa grande: a lo alto como la de siempre (a lo ancho y adelante queda la de Agustin)
fc=fall(np.sqrt(((ax-0.026)/0.012)**2+((U[:,1]-1.631)/0.010)**2))*front
D6b[:,1]=D6b[:,1]+(D5b[:,1]-D6b[:,1])*fc*np.clip((D6b[:,1]-D5b[:,1])/1e-4,0,1)   # solo donde la grande sube mas
np.save(d0+'/S5b.npy',B+D5b[wid]);np.save(d0+'/S6b.npy',B+D6b[wid])
for nm,a,b in (('siempre',D5,D5b),('grande',D6,D6b)):
    dd=np.linalg.norm(a-b,axis=1)*1000;print(nm,'cambio max %.2f mm, vertices > 0.3 mm %d'%(dd.max(),(dd>0.3).sum()))
im=[rend(U+D,cy=1.68) for D in (D5,D5b,D6,D6b)]
out=Image.new('RGB',(im[0].width*4,im[0].height),'white')
for i,x in enumerate(im): out.paste(x,(i*x.width,0))
out.save(sys.argv[2])
