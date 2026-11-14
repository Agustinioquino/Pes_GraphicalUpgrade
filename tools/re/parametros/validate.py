import pickle,struct,fields,model
R=pickle.load(open('json_runs.pkl','rb')); B=pickle.load(open('bin_runs.pkl','rb'))
def truth(f,b):
    mem=b['mem']; o=f['off']
    if o is None: return None
    if f['type']=='string':
        for so,st in b['sets']:
            if so==o: return st
        return None
    if o+ (f['size'] if isinstance(f['size'],int) else 4) > len(mem): return None
    if f['type']=='bool': return mem[o]
    if f['type']=='float' and f['size']==4: return struct.unpack_from('<f',mem,o)[0]
    if f['type']=='int':
        sz=f['size']; return int.from_bytes(mem[o:o+sz],'little',signed=True)
    return None
tot=ok=0
for c,b in B.items():
    if b['err']: continue
    fs=fields.fields_of(R[c]); mv=model.model_values(fs,b['blob'])
    bad=[]
    for f in fs:
        t=truth(f,b); m=mv.get(f['k'],(None,None))[1]
        if f['factor'] and isinstance(m,float): m=m*f['factor']
        same = t==m or (isinstance(t,float) and isinstance(m,float) and abs(t-m)<1e-5*max(1,abs(t)))
        if f['type']=='int' and isinstance(m,int) and t is not None and f['size']<4: same = (m & ((1<<(8*f['size']))-1))==(t& ((1<<(8*f['size']))-1))
        tot+=1; ok+=same
        if not same: bad.append((f['path'],f['type'],t,m))
    print(c,len(fs),'mal:',len(bad),bad[:3])
print(ok,tot)
