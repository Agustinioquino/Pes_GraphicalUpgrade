# Ropa 16/11 (d9ybe2), tercer paso de las pecheras: parchea EN EL LUGAR un cloth_sim.hlsl (repo o VIVO) que ya tiene
# patch_cloth_sim_pechera2.py, sin tocar sus lineas PES21FX_LIVE_*.
# Medido en cloth.log (sesion del 15/11 noche, despues de instalar la fisica de las 20:25 UTC):
#  - pechera bibs_pes21fx: 3542 v -> 2048 particulas, 110 fijas / 1865 libres, 1 CAPSULA; suplente bibs_reserve: 1226 v,
#    96 fijas / 881 libres, 1 CAPSULA. PES_IS_BIB pedia 0 capsulas (como la pechera vieja bibs.fmdl), asi que las reglas de
#    la pechera nunca se aplicaron: las dos se movian como una camiseta casi suelta (Agustin: "aparece un poco desviado").
#    Las camisetas del juego traen 2 capsulas (mangas) o 0 (sin mangas, 1371 v); las pecheras 1.
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

rep("#define PES_IS_BIB (g_legCount == 0 && g_capsuleCount == 0 && (g_vertexCount == PES_BIB_VERTS0",
    "#define PES_IS_BIB (g_legCount == 0 && g_capsuleCount <= 1 && (g_vertexCount == PES_BIB_VERTS0")
rep("#define PES_MENU_JUMP    3.0",
    "// (16/11) cuello de la campera (malla 0 de campera_pes21fx, VS de las pecheras): fijo al esqueleto, sin fisica\n"
    "#define PES_IS_COLLAR (g_legCount == 0 && g_capsuleCount == 0 && (g_vertexCount == 1074 || g_vertexCount == 1100))\n"
    "#define PES_MENU_JUMP    3.0")
rep("float pesPinRest(ParticleRest r) { return (s_flags & 256) ?",
    "float pesPinRest(ParticleRest r) { if (PES_IS_COLLAR) return 1.0; return (s_flags & 256) ?")
rep("float pinOf(uint p)\n{\n",
    "float pinOf(uint p)\n{\n    if (PES_IS_COLLAR) return 1.0;   // (16/11) cuello de la campera\n")
rep("    bool tuckedPins = (s_flags & 256) != 0;\n    float wa =",
    "    if (PES_IS_COLLAR) return;   // (16/11) cuello de la campera: todo fijo\n"
    "    bool tuckedPins = (s_flags & 256) != 0;\n    float wa =")

if crlf: t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('latin1'))
print('ok', p)
