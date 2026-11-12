# asocia cada arreglo de bytes del codigo fuente con la direccion de 1.07 donde esta, y lo compara con 1.00
import re, glob, json, sys, os
import img
A = img.load107(); B = img.load100()
res = json.load(open('map5.json'))
MAP = {int(k, 16): int(v[0], 16) for k, v in res.items() if v[0]}
SRC = '/home/user/repo/src/'
FILES = ['camhook.cpp','cmdlistguard.cpp','exepatch.cpp','faceinject.cpp','fisico.cpp','freecam.cpp','hooks.cpp','memwatch.cpp','menumouse.cpp','motorpage.cpp','musculo.cpp','personajes.cpp','rastro.cpp','relwatch.cpp','soleditor.cpp','solreloj.cpp','tiempo.cpp','exprbones.cpp','pitchwear.cpp','skintone.cpp','overlay.cpp','motorlink.cpp','grassdist.cpp','rastro_v4.cpp']
out = []
for f in FILES:
    if not os.path.exists(SRC + f): continue
    t = open(SRC + f, encoding='utf-8', errors='replace').read()
    code = re.sub(r'//[^\n]*', '', t)
    addrs = sorted(set(int(x, 16) for x in re.findall(r'0x14[0-9a-fA-F]{7}(?![0-9a-fA-F])', t)))   # tambien las de los comentarios
    for m in re.finditer(r'unsigned char\s+(\w+)\s*\[\s*\w*\s*\]\s*=\s*\{([^}]*)\}', code):
        name = m.group(1)
        try: b = bytes(int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]{1,2}\b', m.group(2)))
        except ValueError: continue
        if len(b) < 3: continue
        hits = [a for a in addrs if A.read(a, len(b)) == b]
        if not hits:
            # buscar en la funcion de cada direccion del archivo
            for a in addrs:
                fn = A.func(a)
                if fn and fn[1] - fn[0] < 20000:
                    d = A.read(fn[0], fn[1] - fn[0]); p = d.find(b)
                    while p >= 0: hits.append(fn[0] + p); p = d.find(b, p + 1)
            hits = sorted(set(hits))
        out.append((f, name, len(b), hits, b))
        st = []
        for a in hits:
            fn = A.func(a)
            # traducir: direccion exacta o por desplazamiento dentro de la funcion ya ubicada
            a2 = MAP.get(a)
            st.append((hex(a), hex(a2) if a2 else None))
        print(f, name, len(b), st if hits else 'SIN UBICAR')
json.dump([(f, n, l, [hex(x) for x in h], b.hex()) for f, n, l, h, b in out], open('sigs.json', 'w'), indent=0)
