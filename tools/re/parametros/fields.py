import pickle,struct
def f2b(x): return struct.unpack('<I',struct.pack('<f',x))[0]
def fields_of(d):
    """lista de dict(k,path,type,off,size,factor)"""
    reads,writes,sets=d['reads'],d['writes'],d['sets']
    byk={}
    for w in writes: byk.setdefault(w[3],[]).append(w)
    setk={}
    for s in sets: setk.setdefault(s[2],[]).append(s)
    used=set(); out=[]
    for k,path,t in reads:
        f=dict(k=k,path=path[1:] if path and path[0]=='?' else path,type=t,off=None,size=None,factor=None,note='')
        cand=byk.get(k,[])
        if t=='float':
            m=[w for w in cand if w[1]==4 and w[2]==f2b(k+1)]
            if m: f['off'],f['size']=m[0][0],4
            elif cand:
                w=cand[0]; f['off'],f['size']=w[0],w[1]
                if w[1]==4:
                    v=struct.unpack('<f',struct.pack('<I',w[2]&0xffffffff))[0]; f['factor']=v/(k+1)
                    f['note']='transformado al cargar (x%.6g)'%f['factor']
                else: f['note']='escrito como %d bytes'%w[1]
        elif t=='int':
            m=[w for w in cand if (w[2]&((1<<(8*w[1]))-1))==((0xA00000+k)&((1<<(8*w[1]))-1))]
            if m: f['off'],f['size']=m[0][0],m[0][1]
            elif cand: f['off'],f['size']=cand[0][0],cand[0][1]; f['note']='valor derivado'
        elif t=='bool':
            if cand: f['off'],f['size']=cand[0][0],cand[0][1]
        elif t=='string':
            if k in setk: f['off']=setk[k][0][0]; f['size']='str'; f['note']='cadena (fox::String)'
            elif cand:
                f['off']=cand[0][0]; f['size']=sum(w[1] for w in cand)
                f['note']='cadena -> enum/char[] (%d bytes escritos)'%f['size']
                if all(w[1]>=4 for w in cand): f['note']='cadena convertida a enum/indice'; f['size']=cand[0][1]
        out.append(f)
    return out
if __name__=='__main__':
    R=pickle.load(open('json_runs.pkl','rb'))
    for c in ('CameraInplay','FeintCommand','TeamEmotion','Rating','TutorialSetting'):
        fs=fields_of(R[c]); print(c,len(fs),sum(1 for f in fs if f['off'] is None))
        for f in fs[:12]: print('  ',f)
