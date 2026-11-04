import sys,json,struct;sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from shapes import *
S=make()
NAMES={'1379v':'cabeza_1379v','704v':'mejillas_704v','353v':'menton_353v'}
bin_=bytearray();views=[];accs=[]
def add(arr,comp,typ,target=None,minmax=False):
    a=np.ascontiguousarray(arr);b=a.tobytes()
    while len(bin_)%4: bin_.append(0)
    views.append({"buffer":0,"byteOffset":len(bin_),"byteLength":len(b),**({"target":target} if target else {})});bin_.extend(b)
    acc={"bufferView":len(views)-1,"componentType":comp,"count":int(a.shape[0]) if typ!='SCALAR' else int(a.size),"type":typ}
    if minmax: acc["min"]=a.min(0).tolist();acc["max"]=a.max(0).tolist()
    accs.append(acc);return len(accs)-1
meshes=[];nodes=[]
for i,M in enumerate(MS):
    sl=wid[off[i]:off[i+1]]
    pos=M['v'].astype(np.float32);nrm=N[sl].astype(np.float32);uv=M['uv'].astype(np.float32).copy();uv[:,1]=1-uv[:,1]
    idx=M['f'][:,[0,2,1]].astype(np.uint16).ravel()
    prim={"attributes":{"POSITION":add(pos,5126,'VEC3',34962,True),"NORMAL":add(nrm,5126,'VEC3',34962),"TEXCOORD_0":add(uv,5126,'VEC2',34962)},
          "indices":add(idx,5123,'SCALAR',34963),"targets":[]}
    tn=[]
    for g in G:
        prim["targets"].append({"POSITION":add(S[g].d[sl].astype(np.float32),5126,'VEC3',None,True)});tn.append(g)
    bp=bone_preview(G[0]) and None
    for g in G:
        prim["targets"].append({"POSITION":add(bone_preview(g)[i].astype(np.float32),5126,'VEC3',None,True)});tn.append('huesos_'+g)
    meshes.append({"name":NAMES[M['name']],"primitives":[prim],"weights":[0.0]*len(tn),"extras":{"targetNames":tn}})
    nodes.append({"name":NAMES[M['name']],"mesh":i})
gl={"asset":{"version":"2.0","generator":"PES21FX expresiones"},"scene":0,"scenes":[{"name":"cara_messi","nodes":list(range(len(nodes)))}],
    "nodes":nodes,"meshes":meshes,"accessors":accs,"bufferViews":views,"buffers":[{"byteLength":len(bin_)}]}
js=json.dumps(gl).encode();js+=b' '*((4-len(js)%4)%4)
while len(bin_)%4: bin_.append(0)
out=sys.argv[1]
with open(out,'wb') as f:
    f.write(struct.pack('<III',0x46546C67,2,12+8+len(js)+8+len(bin_)));f.write(struct.pack('<II',len(js),0x4E4F534A));f.write(js)
    f.write(struct.pack('<II',len(bin_),0x004E4942));f.write(bin_)
print(out,12+8+len(js)+8+len(bin_))
