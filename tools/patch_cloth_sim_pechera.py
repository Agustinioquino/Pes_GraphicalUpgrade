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
    "#define PES_BIB_PIN      3.0      // anclaje a la pose de armado: veces mas flojo que la camiseta\n"
    "#define PES_BIB_TETHER   1.6      // correa de distancia maxima: veces mas larga\n"
    "#define PES_BIB_DAMP     0.70     // freno relativo al cuerpo (<1 = se balancea mas tiempo)\n"
    "#define PES_BIB_HANG     0.006    // (m) cuanto cuelga la tela suelta por debajo de su pose de armado\n"
    "#define PES_BIB_AIR      0.004    // (m) aire minimo sobre su superficie de armado (lo que tiene abajo)\n"
    "#define PES_BIB_SKIN     0.004    // (m) separacion extra de la piel al dibujar, sobre la de la camiseta (y256)\n"
    "#define PES_IS_BIB (g_legCount == 0 && g_capsuleCount == 0 && (g_vertexCount == PES_BIB_VERTS0 || (PES_BIB_VERTS1 > 0 && g_vertexCount == PES_BIB_VERTS1)))\n"
    "struct ParticleRest { float4 posPin; float4 nrm; uint4 skin; };\n" % res)

rep("    float damp = g_damping + g_sleeveDamping * sleeveW;\n",
    "    float damp = g_damping + g_sleeveDamping * sleeveW;\n"
    "    if (PES_IS_BIB) damp *= PES_BIB_DAMP;   // (15/11) pechera: tela liviana\n")

rep("        float alpha = g_pinCompliance * rel * rel;\n",
    "        float alpha = g_pinCompliance * rel * rel;\n"
    "        if (PES_IS_BIB) alpha *= PES_BIB_PIN;   // (15/11) pechera: mas suelta que la camiseta\n")

rep("        if (pesFit > 0.0) { float gl = length(s_grav); if (gl > 1e-4) restT += (s_grav / gl) * pesFit * PES_FIT_HANG * fitFree; }\n",
    "        if (pesFit > 0.0) { float gl = length(s_grav); if (gl > 1e-4) restT += (s_grav / gl) * pesFit * PES_FIT_HANG * fitFree; }\n"
    "        if (PES_IS_BIB) { float glb = length(s_grav); if (glb > 1e-4) restT += (s_grav / glb) * PES_BIB_HANG * fitFree; }   // (15/11) la pechera cuelga\n")

rep("        float maxD0 = g_tetherRadius * (1.0 - pin);\n",
    "        float maxD0 = g_tetherRadius * (1.0 - pin) * (PES_IS_BIB ? PES_BIB_TETHER : 1.0);\n")

rep("        float insetF = inset - min(max(pesFit, 0.0), 0.008);   // PES21 FX: delgado = la tela queda a 'fit' del cuerpo\n",
    "        float insetF = inset - min(max(pesFit, 0.0), 0.008);   // PES21 FX: delgado = la tela queda a 'fit' del cuerpo\n"
    "        if (PES_IS_BIB) insetF = min(insetF, -PES_BIB_AIR * fitFree);   // (15/11) pechera: aire sobre lo que tiene abajo\n")

# pesBodyLift (la primera de las dos lineas 'float m = clamp(s_bodyPrm.y'; la de pesBodyCollide sigue con ', mp =')
rep("    float m = clamp(s_bodyPrm.y * 0.001, 0.0, 0.03);\n    float3 y = y0;\n",
    "    float m = clamp(s_bodyPrm.y * 0.001, 0.0, 0.03) + (PES_IS_BIB ? PES_BIB_SKIN : 0.0);   // (15/11) la pechera va por fuera de la camiseta\n    float3 y = y0;\n")

if crlf: t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('latin1'))
print('ok', p, 'reserva', res)
