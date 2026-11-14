import struct, math, numpy as np
from hkpack import Pack
QSIZE={0:4,1:5,2:6,3:3,4:2,5:16}
QALIGN={0:4,1:1,2:2,3:1,4:2,5:4}
def a4(o,a): return (o+a-1)//a*a
def cstr(b,o): return b[o:b.index(b'\0',o)].decode()

def quat(q,data,o):
    if q==2:
        c0,c1,c2=struct.unpack_from('<HHH',data,o)
        sh=((c1>>14)&2)|((c0>>15)&1); neg=(c2>>15)&1
        f=0.000043161
        v=[((c&0x7fff)-16383)*f for c in (c0,c1,c2)]
    elif q==1:
        x=int.from_bytes(data[o:o+5],'little')
        f=0.000345436
        v=[(((x>>(12*i))&0xfff)-2049)*f for i in range(3)]
        sh=(x>>36)&3; neg=(x>>38)&1
    elif q==5:
        return list(struct.unpack_from('<4f',data,o))
    elif q==0:
        cv=struct.unpack_from('<I',data,o)[0]
        R=0x3FFFF; rmask=(1<<18)-1
        r=float((cv>>18)&0x3ff)/1023.0; r=1.0-r*r
        phi_theta=float(cv&rmask)
        phi=math.floor(math.sqrt(phi_theta)); theta=0.0
        if phi>0: theta=(math.pi/4)*(phi_theta-phi*phi)/phi; phi=(math.pi/2/511.0)*phi
        mag=math.sqrt(max(0,1-r*r))
        x=math.sin(phi)*math.cos(theta)*mag; y=math.sin(phi)*math.sin(theta)*mag; z=math.cos(phi)*mag; w=r
        if cv&0x10000000: x=-x
        if cv&0x20000000: y=-y
        if cv&0x40000000: z=-z
        if cv&0x80000000: w=-w
        return [x,y,z,w]
    else:
        raise Exception('quant %d'%q)
    w=math.sqrt(max(0.0,1-sum(t*t for t in v)))
    if neg: w=-w
    v.insert(sh,w)
    return v

def findspan(n,p,u,U):
    if u>=U[n+1]: return n
    lo,hi=p,n+1
    # linear search
    for i in range(p,n+1):
        if U[i]<=u<U[i+1]: return i
    return n
def bspline(u,p,U,P):
    n=len(P)-1
    if n==0: return np.array(P[0],float)
    p=min(p,n)
    k=findspan(n,p,u,U)
    d=[np.array(P[j+k-p],float) for j in range(p+1)]
    for r in range(1,p+1):
        for j in range(p,r-1,-1):
            den=U[j+1+k-r]-U[j+k-p]
            al=0 if den==0 else (u-U[j+k-p])/den
            d[j]=(1-al)*d[j-1]+al*d[j]
    return d[p]

def read_vec(data,o,qtype,flags,default):
    # returns (evaluator, newoffset)
    if flags&0xf0:
        nI,deg=struct.unpack_from('<HB',data,o); o+=3
        knots=list(data[o:o+nI+deg+2]); o+=nI+deg+2
        o=a4(o,4)
        mins=[];maxs=[];sp=[]
        for ax in range(3):
            if flags&(0x10<<ax):
                mn,mx=struct.unpack_from('<ff',data,o); o+=8; mins.append(mn);maxs.append(mx);sp.append(True)
            elif flags&(1<<ax):
                v=struct.unpack_from('<f',data,o)[0]; o+=4; mins.append(v);maxs.append(v);sp.append(False)
            else:
                mins.append(default);maxs.append(default);sp.append(False)
        pts=[]
        for i in range(nI+1):
            c=[]
            for ax in range(3):
                if sp[ax]:
                    if qtype==0: q=data[o]/255.0; o+=1
                    else: q=struct.unpack_from('<H',data,o)[0]/65535.0; o+=2
                    c.append(mins[ax]+(maxs[ax]-mins[ax])*q)
                else: c.append(mins[ax])
            pts.append(c)
        o=a4(o,4)
        return ('s',deg,knots,pts),o
    elif flags&0x0f:
        c=[]
        for ax in range(3):
            if flags&(1<<ax): c.append(struct.unpack_from('<f',data,o)[0]); o+=4
            else: c.append(default)
        return ('c',c),o
    return ('c',[default]*3),o

def read_rot(data,o,q,flags):
    if flags&0xf0:
        nI,deg=struct.unpack_from('<HB',data,o); o+=3
        knots=list(data[o:o+nI+deg+2]); o+=nI+deg+2
        o=a4(o,QALIGN[q])
        pts=[]
        for i in range(nI+1): pts.append(quat(q,data,o)); o+=QSIZE[q]
        o=a4(o,4)
        # make hemisphere-consistent
        for i in range(1,len(pts)):
            if np.dot(pts[i],pts[i-1])<0: pts[i]=[-x for x in pts[i]]
        return ('s',deg,knots,pts),o
    elif flags&0x0f:
        o=a4(o,QALIGN[q]); v=quat(q,data,o); o+=QSIZE[q]; o=a4(o,4)
        return ('c',v),o
    return ('c',[0,0,0,1]),o

def ev(e,t):
    if e[0]=='c': return np.array(e[1],float)
    _,deg,knots,pts=e
    return bspline(t,deg,knots,pts)

def load_anim(b):
    p=Pack(b); D=p.D
    anim=[o for o,c in p.objs if c=='hkaSplineCompressedAnimation'][0]
    ntt=struct.unpack_from('<i',b,D(anim)+16)[0]
    nf,nb,mfpb,mqs,bd,bid,fd=struct.unpack_from('<iiiifff',b,D(anim)+40)
    bo,_=p.arr(anim+68); do,dn=p.arr(anim+116)
    blocks=[struct.unpack_from('<I',b,D(bo)+4*i)[0] for i in range(nb)]
    data=b[D(do):D(do)+dn]
    out=np.zeros((nf,ntt,10))
    for bi in range(nb):
        o=blocks[bi]
        masks=[data[o+4*t:o+4*t+4] for t in range(ntt)]
        o=o+4*ntt
        o=a4(o,4)
        tr=[]
        for t in range(ntt):
            qt,pf,rf,sf=masks[t]
            pq=qt&3; rq=(qt>>2)&0xf; sq=(qt>>6)&3
            pe,o=read_vec(data,o,pq,pf,0.0)
            re,o=read_rot(data,o,rq,rf)
            se,o=read_vec(data,o,sq,sf,1.0)
            tr.append((pe,re,se))
        f0=bi*(mfpb-1)
        for f in range(f0,min(nf,f0+mfpb)):
            lt=f-f0
            for t,(pe,re,se) in enumerate(tr):
                q=ev(re,lt); q=q/np.linalg.norm(q)
                out[f,t,:3]=ev(pe,lt); out[f,t,3:7]=q; out[f,t,7:]=ev(se,lt)
    # binding
    bind=[o for o,c in p.objs if c=='hkaAnimationBinding'][0]
    ta,tn=p.arr(bind+16)
    tracks=[struct.unpack_from('<h',b,D(ta)+2*i)[0] for i in range(tn)] if ta is not None else list(range(ntt))
    return out,tracks,fd

def load_skel(b):
    p=Pack(b); D=p.D
    s=[o for o,c in p.objs if c=='hkaSkeleton'][0]
    pa,pn=p.arr(s+12); ba,bn=p.arr(s+24); ra,rn=p.arr(s+36)
    parents=[struct.unpack_from('<h',b,D(pa)+2*i)[0] for i in range(pn)]
    names=[cstr(b,D(p.ptr[ba+8*i][1])) for i in range(bn)]
    ref=np.array([struct.unpack_from('<12f',b,D(ra)+48*i) for i in range(rn)])
    return names,parents,ref
