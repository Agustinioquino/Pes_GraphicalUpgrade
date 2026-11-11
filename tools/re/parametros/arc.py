import struct,glob,os
OFFS={}
def load(f):
    d=open(f,'rb').read(); n,_x=struct.unpack('<II',d[:8]); out={}
    for i in range(n):
        o,s,no=struct.unpack('<III',d[8+12*i:20+12*i])
        name=d[no:d.index(b'\0',no)].decode()
        out[name]=d[o:o+s]; OFFS[(os.path.basename(f),name)]=o
    return out
def all_entries():
    r={}
    for f in sorted(glob.glob(os.path.dirname(__file__)+'/bin/*.bin')):
        for k,v in load(f).items(): r[k]=(os.path.basename(f),v)
    return r
if __name__=='__main__':
    for k,(f,v) in all_entries().items(): print(f,k,len(v))
