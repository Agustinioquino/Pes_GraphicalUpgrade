# Ropa 16/11 (d9ybe2), cuarto paso: parchea EN EL LUGAR un cloth_sim.hlsl (repo o VIVO) que ya tiene patch_cloth_sim_pechera3.py.
# Agustin (16/11 01:43, videos en Apariencia / Movimiento): "el pantalon ahora no se mueve". Causa (error mio del 15/11,
# patch_cloth_sim_pechera2.py): en menus la tela viajaba pegada al cuerpo si la pose se movia a mas de 3 m/s, para tapar el
# salto de esqueleto del Editar (escala de huesos 1.0 <-> 0.6 de un cuadro a otro, ver cloth.log 15/11). Al correr, la boca de
# la pierna del short pasa los 3 m/s y quedaba quieta. Ahora el salto de esqueleto se reconoce por lo que es: la escala de la
# piel de la particula (determinante de su matriz de piel) cambia mas de PES_MENU_JUMP_SCALE entre el cuadro anterior y este.
# Al correr los huesos solo giran: la escala casi no cambia. En el partido sigue el limite original de 50 m/s.
# Uso: python patch_cloth_sim_pechera4.py "<ruta>\cloth_sim.hlsl"   (juego cerrado)
import sys
p = sys.argv[1]
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
t = raw.decode('latin1').replace('\r\n', '\n')
if 'PES_MENU_JUMP_SCALE' in t: sys.exit('ya esta parcheado')
if 'PES_IS_COLLAR' not in t: sys.exit('falta patch_cloth_sim_pechera3.py')
def rep(a, b):
    global t
    if t.count(a) != 1: sys.exit('no encuentro (o hay mas de uno): ' + a[:70])
    t = t.replace(a, b)

rep("#define PES_MENU_JUMP    3.0      // (m/s) en menus, mas rapido que esto = salto de pose: la tela viaja con el cuerpo\n",
    "#define PES_MENU_JUMP_SCALE 0.35  // (16/11) en menus: cambio relativo del determinante de la piel entre cuadros = salto de esqueleto\n")
# (sin if nuevo: la condicion se arma con ?: y entra en el if que ya existia)
rep("    const float pesJump = s_menu ? PES_MENU_JUMP : 50.0;   // (15/11 c) en menus, un cambio de pose de golpe no es movimiento\n"
    "    if (dot(vBody, vBody) > pesJump * pesJump) {\n",
    "    // (16/11) en menus, el salto de esqueleto (escala de huesos 1.0 <-> 0.6) se reconoce por la escala de la piel, no por la\n"
    "    // velocidad: con 3 m/s las piernas al correr pasaban el limite y el short quedaba quieto\n"
    "    float3x4 pesSk0 = skinSlot(g_rest[p].skin.z, g_rest[p].skin.x, g_rest[p].skin.y, 0.0);\n"
    "    float3x4 pesSk1 = skinSlot(g_rest[p].skin.z, g_rest[p].skin.x, g_rest[p].skin.y, 1.0);\n"
    "    float pesDet0 = abs(determinant((float3x3)pesSk0)), pesDet1 = abs(determinant((float3x3)pesSk1));\n"
    "    bool pesScaleJump = s_menu && abs(pesDet1 - pesDet0) > PES_MENU_JUMP_SCALE * max(max(pesDet0, pesDet1), 1e-6);\n"
    "    if (dot(vBody, vBody) > 2500.0 || pesScaleJump) {\n")
# (16/11, cloth.log 01:38) el VS de las pecheras (92900712c8702eb4, agregado a cloth.ini el 15/11) tambien lo usan brazos y manos
# de algun jugador del menu: entraron como camisetas (6043 v, x -0.58..0.58, y 1.09..1.40, 5 capsulas; 1158 v, y 0.91..1.14,
# 4 capsulas) con todas sus particulas como manga. Quedan fijos al esqueleto como el cuello de la campera.
rep("#define PES_IS_COLLAR (g_legCount == 0 && g_capsuleCount == 0 && (g_vertexCount == 1074 || g_vertexCount == 1100))",
    "#define PES_IS_COLLAR (g_legCount == 0 && ((g_capsuleCount == 0 && (g_vertexCount == 1074 || g_vertexCount == 1100)) || \\\n"
    "                       (g_capsuleCount >= 4 && (g_vertexCount == 6043 || g_vertexCount == 1158))))   // + brazos y manos (16/11)")
if crlf: t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('latin1'))
print('ok', p)
