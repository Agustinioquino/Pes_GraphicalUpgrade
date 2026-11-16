# Ropa 16/11 (d9ybe2), tercer paso de las pecheras: parchea EN EL LUGAR un cloth_sim.hlsl (repo o VIVO) que ya tiene
# patch_cloth_sim_pechera2.py, sin tocar sus lineas PES21FX_LIVE_*.
# Medido en cloth.log (sesion del 15/11 noche, despues de instalar la fisica de las 20:25 UTC):
#  - pechera bibs_pes21fx: 3542 v -> 2048 particulas, 110 fijas / 1865 libres, 1 CAPSULA; suplente bibs_reserve: 1226 v,
#    96 fijas / 881 libres, 1 CAPSULA. PES_IS_BIB pide 0 capsulas (como la pechera vieja bibs.fmdl), asi que las reglas de
#    la pechera no se aplican: se mueven como una camiseta casi suelta. Agustin (16/11): "esta bien que sea asi pero se va
#    para un lado": la soltura queda como esta (PES_IS_BIB sin tocar). Lo que la corre para un lado es la brisa de los
#    menus (PES_MENU_BREEZE), que sopla siempre del mismo costado: en una tela tan suelta el empuje medio la deja corrida.
#    Para las pecheras la brisa va y viene (promedio cero), con la misma fuerza maxima.
#  - cuello de la campera (campera_pes21fx malla 0, mismo VS 92900712c8702eb4 que las pecheras): entraba como una camiseta
#    aparte de 1074 v (LOD1 1100 v), 0 capsulas, 0 fijas / 357 libres: flotaba suelto (Agustin: "la campera se separo el
#    modelo"). Ahora queda fijo al esqueleto como antes (pin 1 y sin restricciones).
# La campera misma sale de cloth.ini (sin 11eda44cab136fa3): el mod no simula su cuerpo (3949 v, "too many particles") y
# solo movia el frente y la capa de encima, que se abrian contra el cuerpo rigido.
# Uso: python patch_cloth_sim_pechera3.py "<ruta>\cloth_sim.hlsl"   (juego cerrado)
import sys
p = sys.argv[1]
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
t = raw.decode('latin1').replace('\r\n', '\n')
if 'PES_IS_COLLAR' in t: sys.exit('ya esta parcheado')
if 'PES_IS_JACKET' not in t: sys.exit('falta patch_cloth_sim_pechera2.py')
def rep(a, b):
    global t
    if t.count(a) != 1: sys.exit('no encuentro (o hay mas de uno): ' + a[:70])
    t = t.replace(a, b)

rep("#define PES_MENU_JUMP    3.0",
    "// (16/11) las pecheras como las arma el mod hoy (pechera 3542 v, suplente 1226 v, con 1 capsula): solo para la brisa\n"
    "#define PES_IS_BIBW (g_legCount == 0 && g_capsuleCount <= 1 && (g_vertexCount == PES_BIB_VERTS0 || g_vertexCount == PES_BIB_VERTS1))\n"
    "// (16/11) cuello de la campera (malla 0 de campera_pes21fx, VS de las pecheras): fijo al esqueleto, sin fisica\n"
    "#define PES_IS_COLLAR (g_legCount == 0 && g_capsuleCount == 0 && (g_vertexCount == 1074 || g_vertexCount == 1100))\n"
    "#define PES_MENU_JUMP    3.0")
# (sin return temprano: con return fxc pierde la cuenta de que el control es uniforme y rechaza las barreras de main, X3663)
rep("float pesPinRest(ParticleRest r) { return (s_flags & 256) ? float((r.skin.w >> 2) & 63) / 63.0 : r.posPin.w; }",
    "float pesPinRest(ParticleRest r) { return PES_IS_COLLAR ? 1.0 : ((s_flags & 256) ? float((r.skin.w >> 2) & 63) / 63.0 : r.posPin.w); }")
rep("    if (s_flags & 256) return float((g_rest[p].skin.w >> 2) & 63) / 63.0;\n    return g_rest[p].posPin.w;\n}\n",
    "    float pn = (s_flags & 256) ? float((g_rest[p].skin.w >> 2) & 63) / 63.0 : g_rest[p].posPin.w;\n"
    "    return PES_IS_COLLAR ? 1.0 : pn;   // (16/11) cuello de la campera: fijo\n}\n")
rep("    float wb = (cn.w & (tuckedPins ? 16u : 4u)) ? 0.0 : 1.0;\n",
    "    float wb = (cn.w & (tuckedPins ? 16u : 4u)) ? 0.0 : 1.0;\n"
    "    if (PES_IS_COLLAR) { wa = 0.0; wb = 0.0; }   // (16/11) cuello de la campera: todo fijo\n")
rep("* (PES_MENU_BREEZE * (0.65 + 0.35 * sin(g_time * 0.9) * sin(g_time * 0.37 + 1.0)));\n",
    "* (PES_MENU_BREEZE * (PES_IS_BIBW ? 0.8 * sin(g_time * 0.9) * (0.6 + 0.4 * sin(g_time * 0.37 + 1.0))   // (16/11) pecheras: va y viene\n"
    "                                   : (0.65 + 0.35 * sin(g_time * 0.9) * sin(g_time * 0.37 + 1.0))));\n")

if crlf: t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('latin1'))
print('ok', p)
