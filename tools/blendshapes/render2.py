import sys;sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from shapes import *
from PIL import Image,ImageDraw,ImageFont
S=make() if __name__=='__main__' else None
def rend(V,col=None,W=420,H=440,sc=2600,cy=1.655):
    img=Image.new('RGB',(W,H),(255,255,255));d=ImageDraw.Draw(img)
    fn=np.cross(V[WF[:,2]]-V[WF[:,0]],V[WF[:,1]]-V[WF[:,0]]);fn/=np.linalg.norm(fn,axis=1,keepdims=True)+1e-12
    L=np.array([0.35,0.45,0.82]);L/=np.linalg.norm(L)
    order=np.argsort(V[WF].mean(1)[:,2])
    for i in order:
        if fn[i,2]<0: continue
        t=WF[i];sh=0.25+0.75*max(0,fn[i]@L)
        base=np.array([226,196,172]) if col is None else col[i]
        c=tuple(int(x) for x in np.clip(base*sh,0,255))
        d.polygon([(W/2-V[j,0]*sc,H/2-(V[j,1]-cy)*sc) for j in t],fill=c)
    return img
def mesh_to_U(per):   # promedio por vertice soldado
    acc=np.zeros_like(U);n=np.zeros(len(U))
    for i,d in enumerate(per):
        np.add.at(acc,wid[off[i]:off[i+1]],d);np.add.at(n,wid[off[i]:off[i+1]],1)
    return acc/n[:,None]
if __name__=='__main__':
    out=Image.new('RGB',(420*6,440*2),'white');dr=ImageDraw.Draw(out)
    out.paste(rend(U),(0,0));dr.text((10,10),'neutra',fill=(0,0,0))
    for k,g in enumerate(G):
        bon=mesh_to_U(bone_preview(g));cor=S[g].d
        out.paste(rend(U+bon+cor),(420*(k+1),0));dr.text((420*(k+1)+10,10),g+' (huesos + esculpido)',fill=(0,0,0))
        a=np.linalg.norm(cor,axis=1)[WF].mean(1)/0.002
        col=np.stack([200+55*np.clip(a,0,1),200-150*np.clip(a,0,1),200-150*np.clip(a,0,1)],1)
        out.paste(rend(U+bon+cor,col),(420*(k+1),440));dr.text((420*(k+1)+10,450),'rojo = esculpido',fill=(0,0,0))
    out.save(sys.argv[1] if len(sys.argv)>1 else 'gestos.png')
