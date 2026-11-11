import re,glob,collections,pickle
GET=r'FUN_141e(?:66020|65580)\([^,()]*(?:\([^()]*\))?[^,()]*,(0x[0-9a-f]+|\d+)\)'
parts=[]
for fn in sorted(glob.glob('/home/claude/re/export/c/c_*.c')):
    txt=open(fn,errors='replace').read()
    for part in txt.split('\n// ==== ')[1:]:
        hdr=part[:part.index('\n')]; name=hdr.split(' @ ')[0]; addr=hdr.split(' @ ')[-1].strip()
        parts.append((name,addr,part))
# 1) wrappers
wrap={}
for name,addr,part in parts:
    if 'FUN_141e6' not in part or part.count('\n')>25: continue
    ms=re.findall(r'(\w+) = (?:\([^()]*\))?'+GET,part)
    ids=set(int(i,0) for v,i in ms)
    m2=re.findall(r'return (?:\([^()]*\))?'+GET,part)
    ids|=set(int(i,0) for i in m2)
    if len(ids)==1 and ('return uVar' in part or 'return lVar' in part or m2):
        wrap[name]=next(iter(ids))
print('wrappers',len(wrap))
use=collections.defaultdict(lambda: collections.defaultdict(set)); calls=collections.defaultdict(set)
WR=re.compile(r'(\w+) = (?:\([^()]*\))?(%s)\('%'|'.join(map(re.escape,wrap))) if wrap else None
G=re.compile(r'(\w+) = (?:\([^()]*\))?'+GET)
for name,addr,part in parts:
    if name in wrap: continue
    has=('FUN_141e66020' in part or 'FUN_141e65580' in part) or (WR and any(w in part for w in wrap))
    if not has: continue
    if addr<'141e67000' and addr>='141e64000': continue
    fid='%s@%s'%(name,addr)
    vars_=collections.defaultdict(set)
    for m in G.finditer(part): vars_[m.group(1)].add(int(m.group(2),0))
    if WR:
        for m in WR.finditer(part): vars_[m.group(1)].add(wrap[m.group(2)])
    for w,i in wrap.items():
        if re.search(r'\b%s\('%w,part): calls[i].add(fid)
    for v,ids in vars_.items():
        for i in ids: calls[i].add(fid)
        if len(ids)!=1: continue
        i=next(iter(ids))
        if re.findall(r'\b%s = (?!(?:\([^()]*\))?(?:FUN_141e(?:66020|65580)|%s))'%(re.escape(v),'|'.join(map(re.escape,wrap)) or 'X'),part): continue
        for m in re.finditer(r'\(%s \+ (0x[0-9a-f]+|\d+)\)'%re.escape(v),part):
            use[i][int(m.group(1),0)].add(fid)
        if re.search(r'\*\([^()]*\*\)%s\b|\*%s\b'%(re.escape(v),re.escape(v)),part): use[i][0].add(fid)
pickle.dump(({k:dict(v) for k,v in use.items()},dict(calls),wrap),open('consumers.pkl','wb'))
for i in sorted(calls): print('%d:%d/%d'%(i,len(calls[i]),len(use.get(i,{}))),end='  ')
print()
