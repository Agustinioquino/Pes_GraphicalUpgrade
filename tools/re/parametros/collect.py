import pickle,fields,model,arc,re,collections
R=pickle.load(open('json_runs.pkl','rb')); E=arc.all_entries()
def lf(c): return c[0].lower()+c[1:]
def key_str(p):
    s=''
    for x in p:
        s+= '[%d]'%x if isinstance(x,int) else ('.' if s else '')+x
    return s
def pat_str(p):
    s=''
    for x in p:
        s+= '[]' if isinstance(x,int) else ('.' if s else '')+x
    return s
def fmt(v,t):
    if v is None: return ''
    if t=='float':
        r='%.6g'%v; return r
    return str(v)
DATA={}
for c,d in R.items():
    fs=fields.fields_of(d)
    o=lf(c)+'.o'
    blob=E[o][1] if o in E else None
    mv=model.model_values(fs,blob) if blob else {}
    rows=[]
    for f in fs:
        pos,v=mv.get(f['k'],(None,None))
        rows.append(dict(f,key=key_str(f['path']),pat=pat_str(f['path']),binpos=pos,val=v,vs=fmt(v,f['type'])))
    DATA[c]=dict(rows=rows,o=o if blob else None,arch=E[o][0] if blob else None)
pickle.dump(DATA,open('data.pkl','wb'))
