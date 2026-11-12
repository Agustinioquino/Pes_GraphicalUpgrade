import struct,numpy as np
def load(fn):
    b=open(fn,'rb').read()
    # find vertex decl: search for entries (off,sem,fmt,cnt,0)
    o=0x558; S={}
    base=0x4b4
    n=struct.unpack_from('<I',b,o+12)[0]
    while True:
        off,sem,fmt,cnt,z=struct.unpack_from('<5I',b,o)
        if cnt!=n or z!=0: break
        S[sem]=(fmt,base+off); o+=20
    sz={5:12,4:8,8:4}
    def get(sem,dt,k): f,a=S[sem]; return np.frombuffer(b,dt,n*k,a).reshape(n,k)
    P=get(2,'<f4',3); N=get(3,'<f4',3); UV=get(7,'<f4',2); UV2=get(8,'<f4',2)
    BI=get(0x12,'u1',4); BW=get(0x11,'<f4',3)
    ia=S[0x11][1]+n*12
    ni=struct.unpack_from('<I',b,0x600+0x1c)[0] if False else None
    return b,n,S,P,N,UV,UV2,BI,BW,ia
