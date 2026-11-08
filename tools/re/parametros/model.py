import struct
from collections import OrderedDict
def build_tree(fields):
    root=OrderedDict()
    for f in fields:
        node=root; p=f['path']
        for i,key in enumerate(p):
            last=i==len(p)-1
            if last: node[key]=('leaf',f)
            else:
                if key not in node: node[key]=OrderedDict()
                node=node[key]
    return root
SZ={'bool':(1,1),'int':(4,4),'float':(4,4),'string':(4,4)}
def is_array(node): return all(isinstance(k,int) for k in node)
def u32(b,o): return struct.unpack_from('<I',b,o)[0]
def read_leaf(blob,pos,f):
    t=f['type']
    if pos+ (1 if t=='bool' else 4) > len(blob): return None
    if t=='bool': return blob[pos]
    if t=='int': return struct.unpack_from('<i',blob,pos)[0]
    if t=='float': return struct.unpack_from('<f',blob,pos)[0]
    if t=='string':
        o=u32(blob,pos)
        if o==0 or o>=len(blob): return ''
        e=blob.index(b'\0',o); return blob[o:e].decode('latin1')
def layout(node,blob,base,out,inline_obj_in_array=True):
    """recorre un objeto ubicado en blob[base]; out[k]=(pos,valor)"""
    pos=base
    items=list(node.items())
    if is_array(node):
        # elementos contiguos
        for key,ch in items:
            pos=place(ch,blob,pos,out,elem=True)
        return pos
    for key,ch in items:
        pos=place(ch,blob,pos,out)
    return pos
def place(ch,blob,pos,out,elem=False):
    if isinstance(ch,tuple):
        f=ch[1]; s,a=SZ[f['type']]
        pos=(pos+a-1)//a*a
        out[f['k']]=(pos,read_leaf(blob,pos,f)); return pos+s
    # objeto o array: en estructura -> offset u32; elemento de array de objetos -> inline
    if elem and not is_array(ch):
        pos=(pos+3)//4*4; end=layout(ch,blob,pos,out); return (end+3)//4*4
    pos=(pos+3)//4*4
    if pos+4<=len(blob):
        o=u32(blob,pos)
        if o and o<len(blob): layout(ch,blob,o,out)
    return pos+4
def model_values(fields,blob):
    out={}; layout(build_tree(fields),blob,0,out); return out
