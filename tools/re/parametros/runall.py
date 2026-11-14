import emu,pickle,time,sys
res={}
for l in open('classes.txt'):
    p=l.split(); c=p[0].split('::')[1]
    if c in ('ArchiveFile','ConstantFile','ConstantData'): continue
    t=time.time()
    r,w,s,e=emu.run_json(int(p[2],16))
    res[c]=dict(json=p[2],bin=p[3],reads=r,writes=w,sets=s,err=e)
    print(c,len(r),len(w),len(s),e,'%.1fs'%(time.time()-t),flush=True)
pickle.dump(res,open('json_runs.pkl','wb'))
