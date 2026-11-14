import emu,arc,pickle
E=arc.all_entries()
res=pickle.load(open('json_runs.pkl','rb'))
out={}
for c,d in res.items():
    o=c[0].lower()+c[1:]+'.o'
    if o not in E: print('NO BLOB',c); continue
    f,blob=E[o]
    mem,wr,err,sets=emu.run_bin(int(d['bin'],16),blob)
    out[c]=dict(file=f,o=o,blob=blob,mem=mem,written=wr,err=err,sets=sets)
    print(c,f,o,len(blob),len(wr),len(sets),err)
pickle.dump(out,open('bin_runs.pkl','wb'))
