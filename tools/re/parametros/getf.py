import sys,re,glob,bisect
files=sorted(glob.glob('/home/claude/re/export/c/c_*.c'))
starts=[int(f.split('_')[-1][:-2],16) for f in files]
def body(addr):
    a=int(addr,16)
    i=bisect.bisect_right(starts,a)-1
    for j in (i,i+1,i-1):
        if j<0 or j>=len(files): continue
        txt=open(files[j],errors='replace').read()
        m=re.search(r'^// ==== (\S+) @ %x\n'%a,txt,re.M)
        if m:
            e=txt.find('\n// ==== ',m.end())
            return m.group(1),txt[m.start():e if e>0 else None]
    return None,None
if __name__=='__main__':
    n,b=body(sys.argv[1]); print(b)
