# Torso 15/11 (d9ybe2): parchea EN EL LUGAR pes_skintone.hlsli (repo o VIVO). z327 = cuanto vuelve al tamano original el torso de
# los jugadores CON camiseta simulada (0..1); la ajustada (no simulada) queda como estaba.
# Uso: python patch_skintone_torso_camiseta.py "<juego>\PES21FX\shaders\pes_skintone.hlsli"  (juego cerrado)
import sys
p = sys.argv[1]
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
t = raw.decode('latin1').replace('\r\n', '\n')
if 'int2(327, 0)' in t: sys.exit('ya esta parcheado')
a = "  float k = PesCharHasShirt(org) > 0.5 ? PesArmRestore(rest) : max(saturate(R.x), PesArmRestore(rest));\n"
if t.count(a) != 1: sys.exit('no encuentro PesTorsoRegrow')
b = ("  // (15/11, Agustin: \"evita que se deforme el torso y los brazos\") z327: tambien CON camiseta el torso vuelve a su tamano original\n"
     "  // (la tela simulada se aparta de la piel real al dibujarse, x256 = 2). La camiseta ajustada no se simula: ahi queda el achicado.\n"
     "  float kS = PesArmRestore(rest);\n"
     "  { float z327 = IniParams.Load(int2(327, 0)).z; if (z327 > 0.0 && PesCharShirtType(org) < 3.5) kS = max(kS, saturate(z327)); }\n"
     "  float k = PesCharHasShirt(org) > 0.5 ? kS : max(saturate(R.x), PesArmRestore(rest));\n")
t = t.replace(a, b)
if crlf: t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('latin1'))
print('ok')
