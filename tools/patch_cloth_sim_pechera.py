# Ropa 15/11 (d9ybe2): parchea EN EL LUGAR un cloth_sim.hlsl (repo o VIVO), sin tocar sus lineas PES21FX_LIVE_*.
# PECHERA (bibs.fmdl / bibs_reserve.fmdl): la fisica de ropa ya la toma como camiseta; con esto queda mas suelta que la
# camiseta (anclaje mas flojo, correa mas larga, menos freno, cuelga unos mm) y no se mete debajo de su superficie de armado
# ni mas cerca de la piel que la camiseta (aire propio en la colision y en la correccion al dibujar).
# Uso: python patch_cloth_sim_pechera.py "<ruta>\cloth_sim.hlsl" [vertices_de_bibs_reserve]   (juego cerrado)
# Necesita el parche "correccion al dibujar" (patch_cloth_sim_dibujo.py) puesto antes.
import sys
p = sys.argv[1]
res = int(sys.argv[2]) if len(sys.argv) > 2 else 0
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
t = raw.decode('latin1').replace('\r\n', '\n')
if 'PES_IS_BIB' in t: sys.exit('ya esta parcheado')
if 'correccion al dibujar' not in t: sys.exit('falta el parche de la correccion al dibujar')
def rep(a, b):
    global t
    if t.count(a) != 1: sys.exit('no encuentro (o hay mas de uno): ' + a[:70])
    t = t.replace(a, b)

rep("struct ParticleRest { float4 posPin; float4 nrm; uint4 skin; };\n",
    "// PES21 FX (15/11, Agustin: \"la fisica de las pecheras deberia ser mas suelta que la remera\", \"que no atraviese la remera\n"
    "// normal y que no se choquen en extremo\"). La pechera (bibs.fmdl, pieza 0; bibs_reserve.fmdl, pieza 8) entra al mod de ropa\n"
    "// como una camiseta sin mangas (sin capsulas). Se reconoce por eso + su cantidad de vertices. Es una tela liviana que cuelga\n"
    "// de los hombros: anclaje a la pose de armado mas flojo, correa mas larga, menos freno y unos mm colgando. Para que no se\n"
    "// meta en lo que tiene abajo (torso o camiseta) no puede quedar mas adentro que su superficie de armado + PES_BIB_AIR, y en\n"
    "// la correccion al dibujar (x256 = 2) queda PES_BIB_SKIN mas lejos de la piel que la camiseta.\n"
    "#define PES_BIB_VERTS0   3542     // bibs.fmdl (visto en cloth.log 15/11: 3542 vertices, 16014 indices)\n"
    "#define PES_BIB_VERTS1   %d        // bibs_reserve.fmdl (0 = todavia sin dato: no se reconoce)\n"
    "// (15/11 b) cloth.log: la pechera quedaba a 39 / 118 / 132 mm de su pose (p50 / p90 / max; la camiseta 26-28 max) y Agustin la\n"
    "// vio \"romper el modelo\": casi toda es tela libre (fija 4 %%, pin ~ 0), el anclaje casi no tiraba y la correa era de 25 cm.\n"
    "// Ahora por altura de la pose de armado: hombros y pecho sujetos, y de ahi para abajo cada vez mas suelta. Tope a la blandura\n"
    "// del anclaje (caida sin sosten ~ 9.8 x alfa) y a la correa. Abajo queda mas suelta que la camiseta, pero en su lugar.\n"
    "#define PES_BIB_Y_HI     1.38     // (m, pose de armado) de aca para arriba: sujeta (hombros, pecho alto)\n"
    "#define PES_BIB_Y_LO     1.08     // (m) de aca para abajo: lo mas suelta (faldon y costados)\n"
    "#define PES_BIB_ALPHA_HI 0.0003   // tope de blandura del anclaje arriba (~3 mm de caida libre)\n"
    "#define PES_BIB_ALPHA_LO 0.004    // idem abajo (~4 cm)\n"
    "#define PES_BIB_TETHER_HI 0.008   // (m) tope de la correa arriba\n"
    "#define PES_BIB_TETHER_LO 0.055   // (m) idem abajo\n"
    "#define PES_BIB_LOOSE(p) smoothstep(0.0, 1.0, saturate((PES_BIB_Y_HI - g_rest[p].posPin.y) / (PES_BIB_Y_HI - PES_BIB_Y_LO)))\n"
    "// (15/11 b, Agustin: \"si bien no se rompe tanto no tiene movimiento ahora\") en los menus (Editar) no hay viento ni empujes del\n"
    "// cuerpo: la tela quedaba inmovil. Brisa leve propia solo en los menus y solo si cloth.ini no trae viento ahi (wind_in_menus = 0).\n"
    "#define PES_MENU_BREEZE  1.0      // (m/s) brisa de los menus; 0 = sin brisa (tela quieta como antes)\n"
    "#define PES_BIB_DAMP     0.70     // freno relativo al cuerpo (<1 = se balancea mas tiempo)\n"
    "#define PES_BIB_HANG     0.006    // (m) cuanto cuelga la tela suelta por debajo de su pose de armado\n"
    "#define PES_BIB_AIR      0.004    // (m) aire minimo sobre su superficie de armado (lo que tiene abajo)\n"
    "#define PES_BIB_SKIN     0.004    // (m) separacion extra de la piel al dibujar, sobre la de la camiseta (y256)\n"
    "#define PES_IS_BIB (g_legCount == 0 && g_capsuleCount == 0 && (g_vertexCount == PES_BIB_VERTS0 || (PES_BIB_VERTS1 > 0 && g_vertexCount == PES_BIB_VERTS1)))\n"
    "struct ParticleRest { float4 posPin; float4 nrm; uint4 skin; };\n" % res)

rep("    float damp = g_damping + g_sleeveDamping * sleeveW;\n",
    "    float damp = g_damping + g_sleeveDamping * sleeveW;\n"
    "    if (PES_IS_BIB) damp *= PES_BIB_DAMP;   // (15/11) pechera: tela liviana\n")

rep("        x += (restT - x) * (1.0 / (1.0 + alpha / hh));\n",
    "        if (PES_IS_BIB) alpha = min(alpha, lerp(PES_BIB_ALPHA_HI, PES_BIB_ALPHA_LO, PES_BIB_LOOSE(p)));   // (15/11 b) pechera: suelta abajo, en su lugar\n"
    "        x += (restT - x) * (1.0 / (1.0 + alpha / hh));\n")

rep("        if (pesFit > 0.0) { float gl = length(s_grav); if (gl > 1e-4) restT += (s_grav / gl) * pesFit * PES_FIT_HANG * fitFree; }\n",
    "        if (pesFit > 0.0) { float gl = length(s_grav); if (gl > 1e-4) restT += (s_grav / gl) * pesFit * PES_FIT_HANG * fitFree; }\n"
    "        if (PES_IS_BIB) { float glb = length(s_grav); if (glb > 1e-4) restT += (s_grav / glb) * PES_BIB_HANG * fitFree; }   // (15/11) la pechera cuelga\n")

rep("        float maxD0 = g_tetherRadius * (1.0 - pin);\n",
    "        float maxD0 = g_tetherRadius * (1.0 - pin);\n"
    "        if (PES_IS_BIB) maxD0 = min(maxD0, lerp(PES_BIB_TETHER_HI, PES_BIB_TETHER_LO, PES_BIB_LOOSE(p)));   // (15/11 b)\n")

rep("        float insetF = inset - min(max(pesFit, 0.0), 0.008);   // PES21 FX: delgado = la tela queda a 'fit' del cuerpo\n",
    "        float insetF = inset - min(max(pesFit, 0.0), 0.008);   // PES21 FX: delgado = la tela queda a 'fit' del cuerpo\n"
    "        if (PES_IS_BIB) insetF = min(insetF, -PES_BIB_AIR * fitFree);   // (15/11) pechera: aire sobre lo que tiene abajo\n")

# pesBodyLift (la primera de las dos lineas 'float m = clamp(s_bodyPrm.y'; la de pesBodyCollide sigue con ', mp =')
rep("    float m = clamp(s_bodyPrm.y * 0.001, 0.0, 0.03);\n    float3 y = y0;\n",
    "    float m = clamp(s_bodyPrm.y * 0.001, 0.0, 0.03) + (PES_IS_BIB ? PES_BIB_SKIN : 0.0);   // (15/11) la pechera va por fuera de la camiseta\n    float3 y = y0;\n")

rep("            s_pesWindOn = pesWOn; s_windHem = pesEffOk ? pesEff.z : 1.0;\n",
    "            s_pesWindOn = pesWOn; s_windHem = pesEffOk ? pesEff.z : 1.0;\n"
    "            if (s_menu && PES_MENU_BREEZE > 0.0 && length(windBase) < 0.1) {   // (15/11 b) brisa leve de los menus, con rachas lentas\n"
    "                windBase = mul(transpose(W), float3(0.92, 0.0, 0.39)) * (PES_MENU_BREEZE * (0.65 + 0.35 * sin(g_time * 0.9) * sin(g_time * 0.37 + 1.0)));\n"
    "                s_windLen = 1.5; }\n")

if crlf: t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('latin1'))
print('ok', p, 'reserva', res)
