# gani_lector.py - el MISMO lector de esqueleto que trae ProbadorAnimaciones 3.1 (funcion examinar), pasado a Python,
# para probarlo sobre TODO el catalogo antes de instalar el modulo. SOLO LECTURA: no escribe ni cambia nada.
# Uso: python gani_lector.py "<carpeta content\probador-animaciones>" > gani_lector_informe.txt
#      (necesita gani_firmas.py y gani_reglas.py al lado)
# Ultima linea: "RESULTADO: INSTALAR" si el lector acepta exactamente los archivos de jugador (los mismos que la firma
# medida) y ninguno mas; si no, "RESULTADO: NO INSTALAR".
import sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gani_firmas as gf
import gani_reglas as gr
ESQUELETO = {
    'c': "D3C3FF90:0.5,1.6 39BC5D3E:2.0,3.3 E96BC45A:4.0 C917522B:5.3,6.0,7.0 602089FA:8.0 80E81915:9.3,10.0,11.0 519FC0F1:12.0 DAC1DFE3:13.0 96DBCD2D:14.0 BEDBFB94:15.0 E7CE157E:16.0 9A9200CE:17.0,18.3,19.0,20.0 600C67FC:21.0 C555E93B:22.0,23.3,24.0,25.0 A3ED8071:26.0",
    'f': "D3C3FF90:0.5,1.6 D7886F00:2.0,3.3 BF968528:4.0 B3C2A0D4:5.0,6.3 13984EAC:7.0,8.3 A13BA15B:9.0,10.3 079C59E8:11.0,12.3 7503D963:13.0,14.3 D6D21686:15.0,16.3 55011CD8:17.0,18.3 3AA921A1:19.0,20.3 9027ED31:21.0,22.3 16C455FF:23.0,24.3 DE427183:25.0,26.3 2D62FB55:27.0,28.3 A03480FA:29.0 73C2EA62:30.0 59935477:31.0 CCFCAE76:32.0 102F1F71:33.0,34.3 E263223A:35.0,36.3 FB40FE50:37.0,38.3 02767479:39.0,40.3 68D6B887:41.0,42.3 998D15DC:43.0,44.3 67B10931:45.0,46.3 6338490F:47.0,48.3 412160EC:49.0,50.3 87E8F2FF:51.0,52.3 3FBCB898:53.0,54.3 528F1ED6:55.0,56.3 FB5E99C0:57.0,58.3",
}
MAGIA = bytes((0xD2, 0xA2, 0xFC, 0x0B))
def esqueleto(letra):
    orden, de = [], {}
    for it in ESQUELETO[letra].split():
        h, idx = it.split(':')
        orden.append(int(h, 16)); de[int(h, 16)] = idx
    return orden, de
def n32(s, o):
    if o < 0 or o + 4 > len(s): return None
    return s[o] | (s[o + 1] << 8) | (s[o + 2] << 16) | (s[o + 3] << 24)
def examinar(path, ref):
    orden, de = ref
    try:
        with open(path, 'rb') as f:
            s = f.read(0x2000)
            tam = f.seek(0, 2)
    except OSError:
        return False, 'falta el archivo'
    if s[:4] != MAGIA: return False, 'no es un .gani'
    if len(s) < 0xA8 or s[0x50:0x56] != b'MOTION' or n32(s, 0x6C) != 0x30: return False, 'es un .gani de otra clase'
    dice = n32(s, 8)
    if dice is not None and tam < dice: return False, 'esta cortado (pesa %d y su cabecera dice %d)' % (tam, dice)
    np_, nt = n32(s, 0x90), n32(s, 0x94)
    if np_ != len(orden): return False, 'no es de jugador (trae %d piezas y %d pistas)' % (np_, nt)
    pistas = 0
    for i in range(np_):
        o = n32(s, 0xA4 + 4 * i)
        q = 0x90 + (o if o is not None else 0x100000)
        h = n32(s, q)
        n = s[q + 4] if q + 4 < len(s) else None
        if h is None or n is None or n < 1 or q + 8 + 8 * n > len(s): return False, 'no pude leer sus piezas'
        t = []
        for k in range(1, n + 1):
            b = q + 8 * k
            t.append('%d.%X' % (s[b + 4] | (s[b + 5] << 8), s[b + 6] % 16))
        idx = de.get(h)
        if not idx: return False, 'no es de jugador (mueve una pieza que el jugador no tiene)'
        if orden[i] != h: return False, 'no es de jugador (trae las piezas en otro orden)'
        if idx != ','.join(t): return False, 'no es de jugador (una pieza trae otras pistas)'
        pistas += n
    if pistas != nt: return False, 'su cabecera no cierra'
    return True, None
FIRMA = {'cuerpo': '1787A4C3', 'cara': 'C15E13FF'}
def main():
    dir_ = sys.argv[1]
    ref = {'cuerpo': esqueleto('c'), 'cara': esqueleto('f')}
    vistos = set()
    en_lista, acepta, motivos = collections.Counter(), collections.Counter(), collections.Counter()
    rechaza_bueno, acepta_malo, afuera_que_pasa = [], [], []
    pesos = collections.Counter()
    afuera = 0
    for lista in ('cara', 'cuerpo', 'partido'):
        ll = gf.leer_lista(dir_, lista)
        if ll is None:
            print('FALTA LA LISTA %s.txt' % lista); continue
        for ruta, rel in ll:
            if ruta.lower() in vistos: continue
            vistos.add(ruta.lower())
            d, _err = gf.parse(ruta)
            fe = gf.crc(tuple((h, tuple(i for i, _t, _f in ps)) for h, _b, ps in d['piezas'])) if d else None
            try:
                with open(ruta, 'rb') as f:
                    cab = f.read(12); tam = f.seek(0, 2)
                dice = n32(cab, 8)
                pesos[(lista, 'no pude leer' if dice is None else ('igual' if tam == dice else ('PESA MENOS' if tam < dice else 'pesa mas')))] += 1
            except OSError:
                pesos[(lista, 'falta')] += 1
            tipo, _cat = gr.clasificar(rel, lista == 'partido')
            if tipo is None:
                afuera += 1
                for t2 in ('cuerpo', 'cara'):
                    if examinar(ruta, ref[t2])[0]: afuera_que_pasa.append('%s (%s)' % (rel, t2))
                continue
            en_lista[tipo] += 1
            ok, por_que = examinar(ruta, ref[tipo])
            de_jugador = (fe == FIRMA[tipo])
            if ok: acepta[tipo] += 1
            else: motivos[(tipo, por_que.split(' (pesa')[0].split(' (trae')[0])] += 1
            if de_jugador and not ok: rechaza_bueno.append('%s: %s' % (rel, por_que))
            if ok and not de_jugador: acepta_malo.append('%s (firma %s)' % (rel, fe))
    print('# gani_lector (lector de ProbadorAnimaciones 3.1 sobre el catalogo)')
    for tipo in ('cuerpo', 'cara'):
        print('%s: en la lista %d, el lector acepta %d' % (tipo.upper(), en_lista[tipo], acepta[tipo]))
        for (t2, m), n in sorted(motivos.items(), key=lambda kv: -kv[1]):
            if t2 == tipo: print('   rechaza %d: %s' % (n, m))
    print('afuera por el nombre: %d; de esos el lector aceptaria %d' % (afuera, len(afuera_que_pasa)))
    for x in afuera_que_pasa[:40]: print('   ' + x)
    print('PESO del archivo contra lo que dice su cabecera (+8):')
    for (lista, que), n in sorted(pesos.items()): print('   %s: %s = %d' % (lista, que, n))
    print('EL LECTOR RECHAZA UNO QUE LA FIRMA DA DE JUGADOR (deberia ser 0): %d' % len(rechaza_bueno))
    for x in rechaza_bueno[:60]: print('   ' + x)
    print('EL LECTOR ACEPTA UNO QUE LA FIRMA NO DA DE JUGADOR (deberia ser 0): %d' % len(acepta_malo))
    for x in acepta_malo[:60]: print('   ' + x)
    bien = not rechaza_bueno and not acepta_malo and acepta['cuerpo'] > 0 and acepta['cara'] > 0
    print('RESULTADO: ' + ('INSTALAR' if bien else 'NO INSTALAR'))
if __name__ == '__main__':
    main()
