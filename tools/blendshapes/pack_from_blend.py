# Lee las shape keys esculpidas del .blend de Agustin (objeto cara_messi, importado del .glb de glb.py) y escribe un paquete de
# formas por malla del juego: PES21FX\formas\<HUELLA>.bin (la DLL lo reconoce por la huella del volcado). Correr con bpy:
#   MALLAS=<carpeta del volcado> python pack_from_blend.py <archivo.blend> <carpeta de salida> [gestos...]
# Formato: "PFXF", u32 version 1, u32 vertices, u32 formas (5: sonrisa grito enojo asombro puchero), u32 grupo (GRUPO=<n>: la cara;
# las arrugas del gesto van en formas\grupo_<n>_<gesto>.dds, ver wrinkle_dds.py; 0 = sin arrugas),
# despues vertices x formas x float4 (delta en metros, espacio del modelo del juego; w = 0). Un gesto que no se pide va en 0.
import bpy,sys,os,struct,glob,numpy as np
G=['sonrisa','grito','enojo','asombro','puchero']
blend,outdir=sys.argv[1],sys.argv[2];want=sys.argv[3:] or G
D=os.environ.get('MALLAS','mallas')
bpy.ops.wm.open_mainfile(filepath=blend)
o=bpy.data.objects['cara_messi'];kb=o.data.shape_keys.key_blocks
def co(k): a=np.array([v.co[:] for v in kb[k].data]);return np.stack([a[:,0],a[:,2],-a[:,1]],1)   # Blender Z arriba -> juego Y arriba
base=co('Basis');used=np.zeros(len(base),bool)
os.makedirs(outdir,exist_ok=True)
packs=[]
for k in ('1379v','704v','353v'):
    fo=glob.glob(D+'/*'+k+'*.obj')[0];ft=glob.glob(D+'/*'+k+'*.txt')[0]
    P=np.array([list(map(float,l.split()[1:4])) for l in open(fo) if l.startswith('v ')])
    hu=open(ft).readline().split()[1]
    # cada vertice del juego -> el vertice de la base de Blender en la misma posicion (y que no este usado por otra malla)
    idx=np.zeros(len(P),int)
    for i,p in enumerate(P):
        d=np.linalg.norm(base-p,axis=1);d[used]=1e9;j=int(np.argmin(d))
        assert d[j]<2e-5,(k,i,d[j]);idx[i]=j
    used[idx]=True
    data=np.zeros((len(P),len(G),4),np.float32)
    for s,g in enumerate(G):
        if g in want and g in kb: data[:,s,:3]=(co(g)-base)[idx]
    packs.append((hu,k,P,data))
# los vertices repetidos en el borde entre pedazos se mueven igual (promedio): sin grietas
acc={}
for hu,k,P,data in packs:
    for i,p in enumerate(P): acc.setdefault(tuple(np.round(p*20000).astype(int)),[]).append(data[i].copy())
for hu,k,P,data in packs:
    for i,p in enumerate(P): data[i]=np.mean(acc[tuple(np.round(p*20000).astype(int))],axis=0)
    with open(os.path.join(outdir,hu+'.bin'),'wb') as f:
        f.write(b'PFXF'+struct.pack('<IIII',1,len(P),len(G),int(os.environ.get('GRUPO','0'))));f.write(data.tobytes())
    mx=np.linalg.norm(data[...,:3],axis=2).max(0)*1000
    print(hu,k,len(P),'max mm por gesto',' '.join('%s %.2f'%(g,m) for g,m in zip(G,mx)))
