# gestos esculpidos por codigo (correctivos que se SUMAN a los huesos del mod) sobre la piel de Messi (3 pedazos)
import sys;sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from weld import *
from mapping import MAPS
import bones_preview as BP
mm=1e-3
G=['sonrisa','grito','enojo','asombro','puchero']
EYE=np.array([0.0305,1.6966,0.145])
def fall(t): t=np.clip(t,0,1); return (1-t*t)**2
class Sh:
    def __init__(s): s.d=np.zeros_like(U)
    def b(s,c,r,vec=(0,0,0),nrm=0.0,sym=True,side=(1.0,1.0)):
        c=np.array(c,float);r=np.array(r if np.ndim(r) else (r,r,r),float)*mm;vec=np.array(vec,float)*mm
        for sg,sc in ((1,side[0]),(-1,side[1])) if sym else ((1,1.0),):
            cc=c*[sg,1,1];vv=vec*[sg,1,1]
            t=np.sqrt((((U-cc)/r)**2).sum(1));f=fall(t)*sc
            s.d+=f[:,None]*vv+(f*nrm*mm)[:,None]*N
            if sym and abs(c[0])<1e-6: break
    def line(s,a,b_,n,r,nrm=0.0,vec=(0,0,0),**k):
        for i in range(n):
            t=i/(n-1);s.b(np.array(a)*(1-t)+np.array(b_)*t,r,vec,nrm,**k)
def surf(x,y):   # z de la piel en (x,y): vertice de adelante mas cercano
    m=U[:,2]>0.09;q=U[m];i=np.argmin((q[:,0]-x)**2+(q[:,1]-y)**2);return q[i,2]
P3=lambda x,y:(x,y,surf(abs(x),y))
def make():
    S={}
    MSIDE=(1.0,1.12)   # Messi: sonrie un poco mas de su lado derecho (x -)
    # ---------------- sonrisa (Duchenne): pomulo lleno, surco nasolabial, ojo achinado, patas de gallo
    s=Sh()
    s.b(P3(0.036,1.660),(18,14,18),(0.3,1.0,0.3),1.6,side=MSIDE)
    s.line(P3(0.019,1.661),P3(0.035,1.617),7,4.5,-0.9,side=MSIDE)
    s.line(P3(0.025,1.662),P3(0.041,1.619),6,7,0.8,side=MSIDE)
    s.b(P3(0.030,1.686),(12,5,10),(0,0.8,0.3),0.3)
    for y in (1.701,1.692): s.b(P3(0.055,y),4,(-0.3,0,0),-0.35)
    s.b(P3(0,1.612),(14,6,10),(0,0,0),-0.3)
    S['sonrisa']=s
    # ---------------- grito (gol): sonrisa fuerte + nariz arrugada + entrecejo + labio de arriba que sube
    s=Sh()
    s.b(P3(0.036,1.660),(18,14,18),(0.4,1.4,0.3),2.0)
    s.line(P3(0.019,1.661),P3(0.036,1.614),7,4.5,-1.3)
    s.line(P3(0.025,1.662),P3(0.042,1.616),6,7,1.1)
    s.line(P3(0.0060,1.700),P3(0.0065,1.716),4,3.5,-0.8)
    s.b(P3(0,1.707),(5,8,6),(0,0,0),0.6)
    s.line(P3(0.007,1.693),P3(0.012,1.686),3,4,-0.6)
    s.b(P3(0.012,1.641),(8,5,8),(0,0.8,0.3),0)
    s.b(P3(0.030,1.686),(12,5,10),(0,1.0,0.3),0.3)
    for y in (1.701,1.692): s.b(P3(0.055,y),4,(-0.35,0,0),-0.5)
    s.b(P3(0,1.585),(15,12,12),(0,-0.5,0),0)
    S['grito']=s
    # ---------------- enojo: arrugas verticales del entrecejo, cejas cargadas, nariz arrugada, labios apretados, menton tenso
    s=Sh()
    s.line(P3(0.0060,1.700),P3(0.0065,1.716),4,3.5,-0.7)
    s.b(P3(0,1.707),(5,8,6),(0,0,0),0.5)
    s.b(P3(0.016,1.712),(10,6,10),(0,-0.4,0),0.6)
    s.line(P3(0.007,1.693),P3(0.012,1.686),3,4,-0.4)
    s.b(P3(0,1.638),(16,3,8),(0,0,0),-0.5)
    s.b(P3(0,1.622),(16,3.5,8),(0,0,0),-0.5)
    s.b(P3(0,1.590),(13,10,12),(0,0.5,0),0.7)
    s.b(P3(0.016,1.664),6,(0.3,0,0),0.5)
    S['enojo']=s
    # ---------------- asombro: arrugas horizontales en la frente, piel del parpado arriba, mejillas estiradas
    s=Sh()
    s.b(P3(0,1.745),(45,18,30),(0,0.8,0),0.3,sym=False)   # frente que sube y se junta (las arrugas finas no entran: la frente tiene aristas de 11 mm)
    s.b(P3(0.031,1.706),(16,6,12),(0,0.9,0),0)
    s.b(P3(0.038,1.650),(18,16,18),(0,-0.6,-0.2),0)
    s.b(P3(0,1.630),(20,8,10),(0,0,0.4),0,sym=False)
    S['asombro']=s
    # ---------------- puchero: menton (mentoniano) arriba con hoyuelos, labio de abajo afuera, comisuras abajo, cejas tristes
    s=Sh()
    s.b(P3(0,1.590),(14,11,12),(0,0.8,0.4),1.4,sym=False)
    s.b(P3(0,1.619),(13,4,8),(0,0.4,1.0),0,sym=False)
    s.b(P3(0.030,1.622),7,(0,-0.9,0),0)
    s.line(P3(0.030,1.619),P3(0.034,1.601),4,3.5,-0.4)
    s.b(P3(0.012,1.714),(9,7,9),(0,0.8,0),0)
    s.b(P3(0,1.728),(15,10,10),(0,0.5,0),0.3,sym=False)
    S['puchero']=s
    # mascaras: borde de los parpados y la linea entre los labios casi no se mueven (los llevan los huesos; la boca no se toca)
    m=np.ones(len(U))
    for sg in (1,-1):
        e=EYE*[sg,1,1];t=np.sqrt(((U[:,0]-e[0])/0.016)**2+((U[:,1]-e[1])/0.0055)**2)
        m*=np.where(U[:,2]>0.12,0.25+0.75*np.clip((t-0.7)/0.6,0,1),1)
    seam=U[loops[1]] if len(loops[1])<len(loops[0]) else U[loops[0]]
    ds=np.sqrt(((U[:,None,:]-seam[None])**2).sum(-1)).min(1)
    m*=0.25+0.75*np.clip((ds-0.0015)/0.0035,0,1)
    m*=np.clip((U[:,2]-0.08)/0.03,0,1)   # nada atras de la cara
    for g in S: S[g].d*=m[:,None]
    return S
def bone_preview(g):
    """vista aproximada de lo que ya mueven los huesos del mod con el gesto en 1 (sin el giro de la mandibula), por malla"""
    out=[]
    for i,M in enumerate(MS):
        mp=MAPS[M['name']];d=np.zeros_like(M['v'])
        for p,n in mp.items():
            o=BP.off[g].get(n)
            if o is not None: d+=M['W'][:,p:p+1]*o[None]
        out.append(d)
    return out
def per_mesh(dU):
    return [dU[wid[off[i]:off[i+1]]] for i in range(len(MS))]
