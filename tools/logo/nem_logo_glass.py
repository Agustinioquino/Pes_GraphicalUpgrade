import numpy as np, struct, sys
from PIL import Image, ImageFilter
from scipy import ndimage as nd
im=np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.float32)/255
r,g,b=im[...,0],im[...,1],im[...,2]
mx=im.max(-1); mn=im.min(-1); sat=(mx-mn)
m=(sat>0.06)|((g>r+0.015)&(g>b+0.01))  # verde oscuro o dorado
m=nd.binary_closing(m,iterations=6); m=nd.binary_fill_holes(m)
lab,n=nd.label(m); sz=nd.sum(m,lab,range(1,n+1)); m=lab==(1+np.argmax(sz))
m=nd.binary_fill_holes(nd.binary_opening(m,iterations=3))
a=Image.fromarray((m*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.5))
a=np.asarray(a).astype(np.float32)/255
ys,xs=np.where(a>0.02); y0,y1,x0,x1=ys.min(),ys.max()+1,xs.min(),xs.max()+1
pad=26; y0=max(0,y0-pad);x0=max(0,x0-pad);y1+=pad;x1+=pad
im=im[y0:y1,x0:x1]; a=a[y0:y1,x0:x1]; H,W=a.shape
# vidrio: brillo superior en diagonal + borde claro + leve transparencia
yy,xx=np.mgrid[0:H,0:W]/np.array([H,W])[:,None,None]
gloss=np.clip(1-(yy*1.0+xx*0.25)/0.5,0,1)**1.5*0.38
band=np.exp(-((yy-0.55*xx-0.12)/0.035)**2)*0.30
inside=nd.distance_transform_edt(a>0.5)
rim=np.exp(-inside/2.5)*0.55
col=im*(1-gloss[...,None])+gloss[...,None]*1.0+band[...,None]
col=col+rim[...,None]*np.array([0.85,1.0,0.9])
# colores glass: menos saturado y hacia un tono frio
lum=col.mean(-1,keepdims=True); col=lum+(col-lum)*0.55; col=col*np.array([0.88,1.0,1.08])+0.05
# contorno de vidrio por fuera de la silueta
outd=nd.distance_transform_edt(a<0.5); ring=(outd>0)&(outd<14)
rt=np.clip(outd/14,0,1)
ringA=np.where(ring,0.22+0.6*np.exp(-((rt-0.95)/0.08)**2)+0.45*np.exp(-((rt-0.08)/0.1)**2),0)
ringC=np.array([0.80,0.93,1.0])
ringC=ringC*(1-gloss[...,None]*0)+gloss[...,None]*0.4
col=np.clip(col,0,1)
alpha=a*np.clip(0.70+0.25*np.exp(-inside/4)+gloss*0.6+band,0,1)
ra=np.clip(ringA*(1+gloss*1.5+band*2),0,1)*(1-a)
A=alpha+ra
col=(col*alpha[...,None]+np.clip(ringC+band[...,None],0,1)*ra[...,None])/np.maximum(A,1e-4)[...,None]
alpha=np.clip(A,0,1)
out=Image.fromarray(np.dstack([col,alpha])*255+0.5 if False else (np.dstack([col,alpha])*255).astype(np.uint8),'RGBA')
h=256; out=out.resize((round(W*h/H),h),Image.LANCZOS)
out.save('nem_logo.png')
w,hh=out.size; px=out.tobytes()
hdr=struct.pack('<4s7I44x8I4I4x',b'DDS ',124,0x100F,hh,w,w*4,0,1,32,0x41,0,32,0xFF,0xFF00,0xFF0000,0xFF000000,0x1000,0,0,0)
open('pes_editor_logo.dds','wb').write(hdr+px); print(w,hh,len(hdr))
