# Ropa 16/11 (d9ybe2): agrega a PES21FX.ini (VIVO, juego cerrado) el interruptor de la fisica de la ropa: x181 (0 prendida,
# 1 apagada = toda la ropa fija al esqueleto, ver tools/patch_cloth_sim_pechera4.py), tecla Shift+3 (# en el teclado
# latinoamericano) y una entrada en el menu Ropa (fisica). Pedido de Agustin: "un boton para apagar las fisicas ... con el numeral".
# Uso: python patch_ini_ropa_apagar.py "<ruta>\PES21FX.ini"
import sys, re
p = sys.argv[1]
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
t = raw.decode('latin1').replace('\r\n', '\n')
if re.search(r'(?m)^x181\s*=', t): sys.exit('ya tiene x181')
def rep(a, b):
    global t
    if t.count(a) != 1: sys.exit('no encuentro (o hay mas de uno): ' + a[:60])
    t = t.replace(a, b)
m = re.search(r'(?m)^x177 = .*\n', t)
if not m: sys.exit('no encuentro x177')
t = t[:m.end()] + ("; (16/11 d9ybe2) x181 = 1: fisica de la ropa apagada, toda la ropa fija al esqueleto como en el juego original (cloth_sim.hlsl\n"
                   "; PES_CLOTH_OFF). Tecla Shift+3 (#) o menu Ropa (fisica) > Apagar la fisica de la ropa. Sin reinicios del simulador.\n"
                   "x181 = 0\n") + t[m.end():]
rep("[KeyPESWrinkleToggle]\n",
    "[KeyPESClothOff]\n"
    "; Shift+3 (# en el teclado latinoamericano): fisica de la ropa apagada / prendida (16/11 d9ybe2)\n"
    "Key = no_ctrl no_alt shift 3\n"
    "type = toggle\n"
    "x181 = 1\n\n"
    "[KeyPESWrinkleToggle]\n")
rep("[MenuClothCompare]\n",
    "[MenuClothOff]\n"
    "section = Ropa (física)\n"
    "title = Apagar la física de la ropa\n"
    "desc = Toda la ropa queda pegada al cuerpo como en el juego original, sin movimiento de tela. Acceso rápido: Shift+3 (#). "
    "No reinicia el simulador, así que sirve también en Editar.\n"
    "toggle = x181\n\n"
    "[MenuClothCompare]\n")
if crlf: t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('latin1'))
print('ok', p)
