# Normal map de arrugas horneado en Blender (OpenGL, Y+) -> DDS R8G8_UNORM con mips para el PS de la cara (DirectX: Y invertida).
#   python3 wrinkle_dds.py entrada.png salida.dds [tamano=1024] [--sin-invertir] [--grande sonrisa_grande.png]
# (04/11 c) con --grande sale R8G8B8A8_UNORM: RG = la sonrisa, BA = la sonrisa grande (el PS pasa de una a otra de 1 a 1.5 del slider)
import sys,struct,numpy as np
from PIL import Image
src,dst=sys.argv[1],sys.argv[2];size=int(sys.argv[3]) if len(sys.argv)>3 and sys.argv[3].isdigit() else 1024
flip='--sin-invertir' not in sys.argv
def load(fn):
    im=Image.open(fn).convert('RGB').resize((size,size),Image.LANCZOS)
    a=np.asarray(im).astype(np.float32)/255.0*2-1
    q=a[...,:2].copy()
    if flip: q[...,1]=-q[...,1]
    return q
xy=load(src)
big=sys.argv[sys.argv.index('--grande')+1] if '--grande' in sys.argv else None
if big: xy=np.concatenate([xy,load(big)],-1)
nc=xy.shape[-1]
mips=[];cur=xy
while True:
    mips.append(np.clip(np.round((cur*0.5+0.5)*255),0,255).astype(np.uint8))
    if cur.shape[0]==1: break
    cur=cur.reshape(cur.shape[0]//2,2,cur.shape[1]//2,2,nc).mean((1,3))
hdr=struct.pack('<4sIIIIIII44x',b'DDS ',124,0x1|0x2|0x4|0x1000|0x20000,size,size,size*nc,0,len(mips))
pf=struct.pack('<II4sIIIII',32,0x4,b'DX10',0,0,0,0,0)
caps=struct.pack('<IIIII',0x1000|0x400000|0x8,0,0,0,0)
dx10=struct.pack('<IIIII',49 if nc==2 else 28,3,0,1,0)   # R8G8_UNORM / R8G8B8A8_UNORM, TEXTURE2D
with open(dst,'wb') as f:
    f.write(hdr+pf+caps+dx10)
    for m in mips: f.write(m.tobytes())
print(dst,size,len(mips),'mips',nc,'canales','Y invertida' if flip else 'Y tal cual')
