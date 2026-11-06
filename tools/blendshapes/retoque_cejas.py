# (04/11, Agustin: "rotarle un poco la curvatura al sonreir en las cejas") giro de la ceja con la sonrisa: la cabeza (adentro) sube,
# la cola (afuera) baja un poco; el medio casi quieto. Sobre la sonrisa que dejo Agustin. Uso: MALLAS=.. python3 retoque_cejas.py <dir B.npy S.npy> <vista.png>
import sys;sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from shapes import *
from render2 import rend
from PIL import Image
d0=sys.argv[1];B=np.load(d0+'/B.npy');S=np.load(d0+'/S.npy')
D=np.zeros_like(U);n=np.zeros(len(U));np.add.at(D,wid,S-B);np.add.at(n,wid,1);D/=n[:,None]
s=Sh()
s.b(P3(0.013,1.716),(9,8,10),(0,1.2,0.1),0)    # cabeza de la ceja arriba
s.b(P3(0.052,1.712),(10,8,12),(0,-0.8,0),0)    # cola un poco abajo
dE=np.minimum(np.linalg.norm((U-EYE)/[1,0.55,1],axis=1),np.linalg.norm((U-EYE*[-1,1,1])/[1,0.55,1],axis=1))
mk=np.clip((dE-0.013)/0.008,0,1)*np.clip((U[:,2]-0.08)/0.03,0,1)
D2=D+s.d*mk[:,None];np.save(d0+'/S2.npy',B+D2[wid])
dd=np.linalg.norm(D2-D,axis=1)*1000;print('cambio max %.2f mm'%dd.max())
a=rend(U+D,cy=1.69);b=rend(U+D2,cy=1.69)
out=Image.new('RGB',(a.width*2,a.height),'white');out.paste(a,(0,0));out.paste(b,(a.width,0));out.save(sys.argv[2])
