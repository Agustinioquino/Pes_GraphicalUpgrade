# Ropa 15/11 (d9ybe2), segundo paso de las pecheras: parchea EN EL LUGAR un cloth_sim.hlsl (repo o VIVO) que ya tiene
# patch_cloth_sim_pechera.py, sin tocar sus lineas PES21FX_LIVE_*.
# Datos medidos con el volcado de dibujos de cloth-mod (F7, sesion 16:57 local de Agustin):
#   pechera bibs_pes21fx (hilo pechera v0.5): VS 92900712c8702eb4, 16014 indices, 3542 vertices, DOS capas a 3 mm (la de
#     adentro con la normal hacia adentro), inflada 10 mm sobre la camiseta, que ahora el juego SI dibuja debajo
#   pechera de suplente bibs_reserve: VS 92900712c8702eb4, 6675 indices, 1226 vertices (14720 B de posiciones)
#   campera campera_pes21fx (training_suit): VS 11eda44cab136fa3 (cuello 92900712c8702eb4), cuerpo 19869 indices / 3949 v
# Uso: python patch_cloth_sim_pechera2.py "<ruta>\cloth_sim.hlsl"   (juego cerrado)
import sys
p = sys.argv[1]
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
t = raw.decode('latin1').replace('\r\n', '\n')
if 'PES_IS_JACKET' in t: sys.exit('ya esta parcheado')
if 'PES_IS_BIB' not in t: sys.exit('falta patch_cloth_sim_pechera.py')
def rep(a, b):
    global t
    if t.count(a) != 1: sys.exit('no encuentro (o hay mas de uno): ' + a[:70])
    t = t.replace(a, b)

rep("#define PES_BIB_VERTS1   0        // bibs_reserve.fmdl (0 = todavia sin dato: no se reconoce)\n",
    "#define PES_BIB_VERTS1   1226     // bibs_reserve.fmdl (15/11 c: volcado F7, 6675 indices, 14720 B de posiciones)\n")

i = t.index("#define PES_IS_BIB ")
j = t.index("\n", i) + 1
t = t[:j] + (
    "// (15/11 c) pechera de dos capas: la capa de adentro trae la normal hacia adentro, asi que el choque contra el cuerpo por la\n"
    "// normal la empujaba para el lado contrario (las capas se separaban). La pechera ya no usa ese choque: la normal se orienta\n"
    "// hacia afuera del torso (centro de la prenda, eje vertical) y la tela no puede entrar mas que PES_BIB_IN por debajo de su\n"
    "// propia pose (la pose esta 10 mm por fuera de la camiseta): asi no se mete en la camiseta que ahora se dibuja abajo.\n"
    "#define PES_BIB_IN       0.002    // (m) cuanto puede entrar la pechera por debajo de su pose\n"
    "// (15/11 c, Agustin: \"la campera deberia tener una fisica menor, pero un poco visible\") campera de entrenamiento: tela mas\n"
    "// pesada, anclaje firme, correa corta y mas freno. Se reconoce por su tamano (ninguna camiseta del juego llega a 3900 vertices).\n"
    "#define PES_JK_MINV       3900\n"
    "#define PES_JK_ALPHA      0.0008  // tope de la blandura del anclaje (~8 mm de caida libre)\n"
    "#define PES_JK_TETHER     0.025   // (m) tope de la correa de distancia maxima\n"
    "#define PES_JK_DAMP       1.6     // freno relativo al cuerpo (x)\n"
    "#define PES_IS_JACKET (g_legCount == 0 && !PES_IS_BIB && g_vertexCount >= PES_JK_MINV)\n"
    "// (15/11 c, Agustin: \"revisa los calculos del short que parece que siguen alto\") con la brisa de los menus el short (mas\n"
    "// corto y mas libre abajo) se abria hasta 47-53 mm de su pose: al short le llega una parte de la brisa\n"
    "#define PES_MENU_BREEZE_SHORTS 0.35\n") + t[j:]

rep("    if (PES_IS_BIB) damp *= PES_BIB_DAMP;   // (15/11) pechera: tela liviana\n",
    "    if (PES_IS_BIB) damp *= PES_BIB_DAMP;   // (15/11) pechera: tela liviana\n"
    "    if (PES_IS_JACKET) damp *= PES_JK_DAMP;   // (15/11 c) campera: tela pesada\n")

i = t.index("        if (PES_IS_BIB) alpha = min(alpha")
j = t.index("\n", i) + 1
t = t[:j] + "        if (PES_IS_JACKET) alpha = min(alpha, PES_JK_ALPHA);   // (15/11 c) campera: se mueve poco\n" + t[j:]

i = t.index("        if (PES_IS_BIB) maxD0 = min(maxD0")
j = t.index("\n", i) + 1
t = t[:j] + "        if (PES_IS_JACKET) maxD0 = min(maxD0, PES_JK_TETHER);   // (15/11 c)\n" + t[j:]

rep("        if (PES_IS_BIB) insetF = min(insetF, -PES_BIB_AIR * fitFree);   // (15/11) pechera: aire sobre lo que tiene abajo\n", "")
rep("        if (capN == 0 && sd < -insetF) {\n", "        if (!PES_IS_BIB && capN == 0 && sd < -insetF) {\n")
rep("        } else if (pesFit < 0.0 && sd > 0.0) {\n", "        } else if (!PES_IS_BIB && pesFit < 0.0 && sd > 0.0) {\n")
rep("        if (legTracked) {\n",
    "        if (PES_IS_BIB) {   // (15/11 c) ver PES_BIB_IN\n"
    "            float3 nO = rnrm1;\n"
    "            if (s_tOk) { float3 ro = rest1 - s_tC; ro -= s_tUp * dot(ro, s_tUp); if (dot(nO, ro) < 0.0) nO = -nO; }\n"
    "            float sdB = dot(x - restT, nO);\n"
    "            if (sdB < -PES_BIB_IN) { float3 corrB = (-PES_BIB_IN - sdB) * nO; x += corrB; xPrev += corrB * (1.0 - g_bodyBounce); }\n"
    "        }\n"
    "        if (legTracked) {\n")

rep("* (PES_MENU_BREEZE * (0.65 + 0.35 * sin(g_time * 0.9) * sin(g_time * 0.37 + 1.0)));\n",
    "* (PES_MENU_BREEZE * (g_legCount > 0 ? PES_MENU_BREEZE_SHORTS : 1.0) * (0.65 + 0.35 * sin(g_time * 0.9) * sin(g_time * 0.37 + 1.0)));\n")

if crlf: t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('latin1'))
print('ok', p)
