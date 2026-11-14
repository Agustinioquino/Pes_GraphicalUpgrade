import struct
class Pack:
    def __init__(s,b):
        s.b=b
        assert b[:8]==bytes.fromhex('57e0e05710c0c010')
        nsec=struct.unpack_from('<i',b,0x14)[0]
        s.secs={}
        for i in range(nsec):
            o=0x40+48*i
            nm=b[o:o+20].split(b'\0')[0].decode()
            ab,lf,gf,vf,ex,im,end=struct.unpack_from('<7I',b,o+20)
            s.secs[nm]=dict(i=i,abs=ab,lf=lf,gf=gf,vf=vf,ex=ex,im=im,end=end)
        s.secl=sorted(s.secs.values(),key=lambda x:x['i'])
        cn=s.secs['__classnames__']; s.cls={}
        o=cn['abs']
        while o<cn['abs']+cn['lf']:
            sig=struct.unpack_from('<I',b,o)[0]
            if sig==0xffffffff: break
            o+=5; e=b.index(b'\0',o); s.cls[o-cn['abs']]=b[o:e].decode(); o=e+1
        d=s.secs['__data__']; s.d=d; A=d['abs']
        s.ptr={}
        o=A+d['lf']
        while o+8<=A+d["gf"]:
            src,dst=struct.unpack_from('<ii',b,o); o+=8
            if src==-1: continue
            s.ptr[src]=('d',dst)
        o=A+d['gf']
        while o+12<=A+d["vf"]:
            src,sec,dst=struct.unpack_from('<iii',b,o); o+=12
            if src==-1: continue
            s.ptr[src]=(sec,dst)
        s.objs=[]
        o=A+d['vf']
        while o+12<=A+d["ex"]:
            src,sec,dst=struct.unpack_from('<iii',b,o); o+=12
            if src==-1: continue
            s.objs.append((src,s.cls.get(dst,'?')))
    def D(s,off): return s.d['abs']+off
    def arr(s,off):  # hkArray at data offset
        p=s.ptr.get(off); size=struct.unpack_from('<i',s.b,s.D(off)+4)[0]
        return (p[1] if p else None), size
