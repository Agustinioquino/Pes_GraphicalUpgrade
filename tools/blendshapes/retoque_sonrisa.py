# (04/11) retoque por codigo de la sonrisa de Agustin (messi3.blend) con lo que pidio:
#   "caida mas natural, mostrar mas los dientes de arriba, achicar los labios inferiores, levantar un poco mas las cejas, inflar los cachetes"
# Entrada: B.npy / S.npy (basis y sonrisa de cara_messi en ejes del juego, orden de vertices de Blender = 1379 + 704 + 353)
# Salida: S2.npy (sonrisa nueva) + vista antes / despues. Uso: MALLAS=<dir> python3 retoque_sonrisa.py <dir con B.npy S.npy> <vista.png>
import sys;sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from shapes import *
from render2 import rend
from PIL import Image
d0=sys.argv[1]
B=np.load(d0+'/B.npy');S=np.load(d0+'/S.npy')
assert len(B)==len(P), (len(B),len(P))
err=np.abs(B-P).max();print('basis vs volcado max mm %.3f'%(err*1000))
piece=np.zeros(len(P),int);piece[off[1]:off[2]]=1;piece[off[2]:]=2
# delta por vertice soldado (promedio)
D=np.zeros_like(U);n=np.zeros(len(U));np.add.at(D,wid,S-B);np.add.at(n,wid,1);D/=n[:,None]
pw=np.zeros((len(U),3));np.add.at(pw,(wid,piece),1)   # en que pedazos esta cada vertice soldado
up=pw[:,1]>0;lo=pw[:,2]>0
s=Sh()
# 1) dientes de arriba: el labio de arriba sube (mas en el centro, nada en las comisuras), apenas atras
m1=up&(U[:,1]>1.6315)&(U[:,1]<1.650)&(U[:,2]>0.155)
f1=fall(np.abs(U[:,0])/0.024)*fall((U[:,1]-1.633)/0.014)   # la nariz (1.652 para arriba) no se toca
s.d[m1]+=(f1[m1,None]*np.array([0,1.9,-0.35])*mm)
# 2) labio de abajo mas fino: la parte de adelante va atras y la de abajo sube (se enrolla hacia los dientes)
m2=lo&(U[:,1]>1.612)&(U[:,1]<1.6335)&(U[:,2]>0.155)
f2=fall(np.abs(U[:,0])/0.025)*fall(np.abs(U[:,1]-1.625)/0.011)
s.d[m2]+=(f2[m2,None]*np.array([0,0.45,-0.9])*mm)
# 3) cejas un poco mas arriba (cola de la ceja un poco menos)
s.b(P3(0.024,1.719),(16,9,14),(0.1,1.3,0.1),0)
s.b(P3(0.046,1.716),(12,8,12),(0,0.7,0),0)
# 4) cachetes inflados (pomulo y cachete adelante, hacia afuera)
s.b(P3(0.040,1.650),(17,15,17),(0.25,0.35,0.2),1.5,side=(1.0,1.08))
# 5) (Agustin, foto de Messi sonriendo) al lado de la nariz se infla (el cachete sube contra el ala de la nariz) y se marca la arruga
#    al lado de la boca (surco nasolabial hondo, con el cachete abultado afuera del surco)
s.b(P3(0.021,1.656),(8,7,8),(0.2,0.6,0),1.4,side=(1.0,1.08))
s.line(P3(0.0165,1.657),P3(0.031,1.625),8,3.2,-1.1,side=(1.0,1.1))
s.line(P3(0.0225,1.656),P3(0.037,1.626),7,5.5,1.0,side=(1.0,1.1))
s.b(P3(0.033,1.621),(4,5,4),(0,0,0),-0.6,side=(1.0,1.1))   # hoyuelo al final del surco, junto a la comisura
# mascara: borde de los parpados casi quieto (lo llevan los huesos), nada atras de la cara
dE=np.minimum(np.linalg.norm((U-EYE)/[1,0.55,1],axis=1),np.linalg.norm((U-EYE*[-1,1,1])/[1,0.55,1],axis=1))
mk=np.clip((dE-0.013)/0.008,0,1)*np.clip((U[:,2]-0.08)/0.03,0,1)
D2=D.copy()
# 6) caida mas natural: se suaviza la forma (no la piel) en mejillas / surco / comisuras, sin tocar la linea de los labios
nb=[[] for _ in range(len(U))]
for t in WF:
    for a,b in ((t[0],t[1]),(t[1],t[2]),(t[2],t[0])): nb[a].append(b);nb[b].append(a)
nb=[np.unique(x) for x in nb]
reg=fall(np.linalg.norm((U-[0,1.640,0.17])/[0.065,0.045,0.08],axis=1))*np.where(np.abs(U[:,1]-1.633)<0.004,0.0,1.0)*mk
for it in range(6):
    avg=np.array([D2[x].mean(0) if len(x) else D2[i] for i,x in enumerate(nb)])
    D2=D2+(avg-D2)*(0.5*reg)[:,None]
D2=D2+s.d*mk[:,None]   # los retoques despues del suavizado (si no, se borra el surco)
S2=B+D2[wid]
np.save(d0+'/S2.npy',S2)
dd=np.linalg.norm(D2-D,axis=1)*1000;print('cambio max %.2f mm, vertices >0.3 mm %d; sonrisa max antes %.2f despues %.2f'%(dd.max(),(dd>0.3).sum(),np.linalg.norm(D,axis=1).max()*1000,np.linalg.norm(D2,axis=1).max()*1000))
a=rend(U+D);b=rend(U+D2)
c=np.clip(dd/2.5,0,1);col=np.stack([200+55*c,200-120*c,200-160*c],1)
cf=rend(U,col=col[WF].mean(1))
out=Image.new('RGB',(a.width*3,a.height),'white');out.paste(a,(0,0));out.paste(b,(a.width,0));out.paste(cf,(2*a.width,0));out.save(sys.argv[2])
