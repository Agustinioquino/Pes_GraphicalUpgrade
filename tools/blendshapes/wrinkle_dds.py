# Normal map de arrugas horneado en Blender (OpenGL, Y+) -> DDS R8G8_UNORM con mips para el PS de la cara (DirectX: Y invertida).
#   python3 wrinkle_dds.py entrada.png salida.dds [tamano=1024] [--sin-invertir]
import sys,struct,numpy as np
from PIL import Image
src,dst=sys.argv[1],sys.argv[2];size=int(sys.argv[3]) if len(sys.argv)>3 and sys.argv[3].isdigit() else 1024
flip='--sin-invertir' not in sys.argv
im=Image.open(src).convert('RGB').resize((size,size),Image.LANCZOS)
a=np.asarray(im).astype(np.float32)/255.0*2-1
xy=a[...,:2].copy()
if flip: xy[...,1]=-xy[...,1]
mips=[];cur=xy
while True:
    mips.append(np.clip(np.round((cur*0.5+0.5)*255),0,255).astype(np.uint8))
    if cur.shape[0]==1: break
    cur=cur.reshape(cur.shape[0]//2,2,cur.shape[1]//2,2,2).mean((1,3))
hdr=struct.pack('<4sIIIIIII44x',b'DDS ',124,0x1|0x2|0x4|0x1000|0x20000,size,size,size*2,0,len(mips))
pf=struct.pack('<II4sIIIII',32,0x4,b'DX10',0,0,0,0,0)
caps=struct.pack('<IIIII',0x1000|0x400000|0x8,0,0,0,0)
dx10=struct.pack('<IIIII',49,3,0,1,0)   # R8G8_UNORM, TEXTURE2D
with open(dst,'wb') as f:
    f.write(hdr+pf+caps+dx10)
    for m in mips: f.write(m.tobytes())
print(dst,size,len(mips),'mips','Y invertida' if flip else 'Y tal cual')
