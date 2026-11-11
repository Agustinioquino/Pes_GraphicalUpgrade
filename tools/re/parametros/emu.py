import sys,struct,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))+'/py')
from unicorn import *
from unicorn.x86_const import *
D="/mnt/user-data/uploads/eFootball PES 2021/PES21FX/dump/"
BASE=0x140000000
SECS=[(0x1000,"_text.bin"),(0x2538000,"_idata.bin"),(0x33A9000,"_text1.bin"),(0x3AD3000,"_pdata.bin"),(0x3CB0000,"_data2.bin"),(0x1B479000,"_data.bin")]
_secs=[(BASE+va,open(D+f,'rb').read()) for va,f in SECS]
def rd(addr,n):
    for b,d in _secs:
        if b<=addr<b+len(d): return d[addr-b:addr-b+n]
def cstr(addr,uc=None):
    s=bytes(uc.mem_read(addr,256)) if uc else rd(addr,256); return s[:s.index(b'\0')].decode('latin1')
PG=0x1000
def up(x): return (x+PG-1)//PG*PG
OBJ=0x10000000; OBJSZ=0x200000
BLOB=0x20000000
STK=0x30000000; STKSZ=0x100000
STRS=0x40000000
RET=0x50000000
H_GET=0x140383d70; H_IDX=(0x140383cb0,0x140383cc0); H_F=0x140384220; H_I=0x140384320; H_B=0x1403841a0; H_S=0x141f1ff90; H_SET=0x1404023c0
def newuc():
    uc=Uc(UC_ARCH_X86,UC_MODE_64)
    for b,d in _secs:
        uc.mem_map(b//PG*PG, up(b+len(d))-b//PG*PG); uc.mem_write(b,d)
    for a,sz in ((OBJ,OBJSZ),(BLOB,0x200000),(STK,STKSZ),(STRS,0x100000),(RET,PG),(0x60000000,0x100000)):
        uc.mem_map(a,sz)
    uc.mem_write(RET,b'\xf4')
    # gs/teb: map page at 0x60000000 for gs
    uc.reg_write(UC_X86_REG_GS_BASE,0x60000000)
    return uc
def doret(uc,val=None,xmm=None):
    rsp=uc.reg_read(UC_X86_REG_RSP); ra=struct.unpack('<Q',uc.mem_read(rsp,8))[0]
    uc.reg_write(UC_X86_REG_RSP,rsp+8); uc.reg_write(UC_X86_REG_RIP,ra)
    if val is not None: uc.reg_write(UC_X86_REG_RAX,val)
    if xmm is not None: uc.reg_write(UC_X86_REG_XMM0,xmm)

def run_json(addr,limit=50_000_000):
    """devuelve (reads, writes): reads=[(k,path,tipo)], writes=[(off,size,bytes,k_last)]"""
    uc=newuc()
    handles=[()]  # handle id -> path tuple
    reads=[]; writes=[]; sets=[]; strptr=[STRS]
    def H(i): return 0x70000000+i*16
    def path_of(h):
        i=(h-0x70000000)//16
        return handles[i] if 0<=i<len(handles) and (h-0x70000000)%16==0 else None
    def newh(p):
        handles.append(p); return H(len(handles)-1)
    def hook(uc,address,size,ud):
        rcx=uc.reg_read(UC_X86_REG_RCX); rdx=uc.reg_read(UC_X86_REG_RDX)
        if address==H_GET:
            p=path_of(rcx); key=cstr(rdx,uc) if rdx else '?'
            doret(uc,newh((p if p is not None else ('?',))+(key,)))
        elif address in H_IDX:
            p=path_of(rcx); doret(uc,newh((p if p is not None else ('?',))+(int(rdx&0xffffffff),)))
        elif address==H_F:
            k=len(reads); reads.append((k,path_of(rcx),'float'))
            doret(uc,0,struct.unpack('<Q',struct.pack('<d',float(k+1)))[0])
        elif address==H_I:
            k=len(reads); reads.append((k,path_of(rcx),'int')); doret(uc,0x00A00000+k)
        elif address==H_B:
            k=len(reads); reads.append((k,path_of(rcx),'bool')); doret(uc,1)
        elif address==H_S:
            k=len(reads); reads.append((k,path_of(rcx),'string'))
            s=('S%d'%k).encode()+b'\0'; a=strptr[0]; uc.mem_write(a,s); strptr[0]+=64; doret(uc,a)
        elif address==H_SET:
            sets.append((rcx-OBJ,cstr(rdx,uc) if STRS<=rdx<STRS+0x100000 else hex(rdx),len(reads)-1)); doret(uc,rcx)
    for a in (H_GET,)+H_IDX+(H_F,H_I,H_B,H_S,H_SET):
        uc.hook_add(UC_HOOK_CODE,hook,begin=a,end=a)
    def wh(uc,access,address,size,value,ud):
        writes.append((address-OBJ,size,value,len(reads)-1))
    uc.hook_add(UC_HOOK_MEM_WRITE,wh,begin=OBJ,end=OBJ+OBJSZ-1)
    rsp=STK+STKSZ-0x1000; uc.mem_write(rsp,struct.pack('<Q',RET))
    uc.reg_write(UC_X86_REG_RSP,rsp); uc.reg_write(UC_X86_REG_RCX,OBJ); uc.reg_write(UC_X86_REG_RDX,H(0))
    err=None
    try: uc.emu_start(addr,RET,count=limit)
    except UcError as e: err='%s at %x'%(e,uc.reg_read(UC_X86_REG_RIP))
    return reads,writes,sets,err

def run_bin(addr,blob,limit=50_000_000):
    uc=newuc(); uc.mem_write(BLOB,blob)
    rsp=STK+STKSZ-0x1000; uc.mem_write(rsp,struct.pack('<Q',RET))
    uc.reg_write(UC_X86_REG_RSP,rsp); uc.reg_write(UC_X86_REG_RCX,OBJ); uc.reg_write(UC_X86_REG_RDX,BLOB)
    # marcar obj con patron para saber que se escribio
    uc.mem_write(OBJ,b'\xcd'*OBJSZ)
    written=set(); sets=[]
    def hs(uc,address,size,ud):
        rcx=uc.reg_read(UC_X86_REG_RCX); rdx=uc.reg_read(UC_X86_REG_RDX)
        try: st=cstr(rdx,uc)
        except Exception: st='?'
        sets.append((rcx-OBJ,st)); doret(uc,rcx)
    uc.hook_add(UC_HOOK_CODE,hs,begin=H_SET,end=H_SET)
    def wh(uc,access,address,size,value,ud):
        for i in range(size): written.add(address-OBJ+i)
    uc.hook_add(UC_HOOK_MEM_WRITE,wh,begin=OBJ,end=OBJ+OBJSZ-1)
    err=None
    try: uc.emu_start(addr,RET,count=limit)
    except UcError as e: err='%s at %x'%(e,uc.reg_read(UC_X86_REG_RIP))
    mx=max(written)+1 if written else 0
    return bytes(uc.mem_read(OBJ,mx)),written,err,sets
