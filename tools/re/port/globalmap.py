# mapa global de funciones 1.07 -> 1.00 por hash de la funcion enmascarada; despues, las vecinas sin pareja con el mismo tamano
import hashlib, pickle, sys, collections
import match as M
from match import A, B, insns
def hashes(im):
    out = []
    for s, e in zip(im.fb, im.fe):
        if e - s > 60000: out.append(None); continue
        try:
            lst = insns(im, s, e)
            h = hashlib.md5(b''.join(bytes(x if k else 0 for x, k in zip(b, m)) for _, b, m in lst)).digest()
            if not lst or lst[-1][0] + len(lst[-1][1]) != e: h = hashlib.md5(b'raw' + im.read(s, e - s)).digest()
        except Exception: h = None
        out.append(h)
    return out
which = sys.argv[1]
im = A if which == 'A' else B
pickle.dump(hashes(im), open('hash%s.pkl' % which, 'wb'))
