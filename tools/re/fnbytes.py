# (27/10) Para la PC (python sin objdump): python -I fnbytes.py <PES21FX\dump> salida.txt <direcciones hex>; en la nube: base64 -d | objdump -D -b binary -m i386:x86-64 -M intel --adjust-vma=<inicio>
# Solo lectura: busca en el volcado del exe la funcion que contiene cada direccion (por .pdata) y escribe sus bytes en base64.
import struct, base64, sys, os
D = sys.argv[1]                        # carpeta PES21FX\dump
OUT = sys.argv[2]                      # archivo de salida
ADDRS = [int(a, 16) for a in sys.argv[3:]]
BASE = 0x140000000
text = open(os.path.join(D, '_text.bin'), 'rb').read(); TVA = 0x1000
pdata = open(os.path.join(D, '_pdata.bin'), 'rb').read()
ents = [struct.unpack_from('<III', pdata, i) for i in range(0, len(pdata) - 11, 12)]
ents = [e for e in ents if e[0] and e[1] > e[0]]
with open(OUT, 'w') as f:
    for a in ADDRS:
        rva = a - 1 - BASE
        hit = [e for e in ents if e[0] <= rva < e[1]]
        if not hit:
            f.write('%x: sin funcion en .pdata\n' % a); continue
        b, e, u = hit[0]
        lo = max(b, rva - 0x1800); hi = min(e, rva + 0x400)
        data = text[lo - TVA:hi - TVA]
        f.write('%x: funcion %x-%x, bytes %x-%x (%d)\n' % (a, BASE + b, BASE + e, BASE + lo, BASE + hi, len(data)))
        s = base64.b64encode(data).decode()
        for i in range(0, len(s), 120): f.write(s[i:i + 120] + '\n')
        f.write('fin %x\n' % a)
print('listo', OUT, os.path.getsize(OUT))
