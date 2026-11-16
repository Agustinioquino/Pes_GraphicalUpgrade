# Ropa 15/11 (d9ybe2): parchea EN EL LUGAR un cloth_sim.hlsl (repo o VIVO), sin tocar sus lineas PES21FX_LIVE_*.
# x256 = 2 -> "corregir al dibujar": la fisica no choca con la piel real; solo la copia que se dibuja se saca afuera de la piel.
# Uso: python patch_cloth_sim_dibujo.py "<ruta>\cloth_sim.hlsl"  (juego cerrado)
import sys
p = sys.argv[1]
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
t = raw.decode('latin1').replace('\r\n', '\n')
if 'correccion al dibujar' in t: sys.exit('ya esta parcheado')
def rep(a, b):
    global t
    if t.count(a) != 1: sys.exit('no encuentro: ' + a[:60])
    t = t.replace(a, b)
rep("static uint   s_bodyTab;", "static bool   s_bodySel = false;   // (15/11) la tabla del cuerpo se eligio en este cuadro (correccion al dibujar)\nstatic uint   s_bodyTab;")
rep("        if (s == 0) pesBodyQuery(p, k, x, rest1);   // (25/10) la piel cercana, una vez por cuadro\n"
    "        bool pesBodyHas = ((s_bv[k][0].y | s_bv[k][1].y) & 0x10000u) != 0;\n",
    "        // (15/11) x256 = 2 (correccion al dibujar): la fisica no usa la piel real\n"
    "        if (s == 0 && s_bodyPrm.x < 1.5) pesBodyQuery(p, k, x, rest1);   // (25/10) la piel cercana, una vez por cuadro\n"
    "        bool pesBodyHas = s_bodyPrm.x < 1.5 && ((s_bv[k][0].y | s_bv[k][1].y) & 0x10000u) != 0;\n")
rep("            s_bodyTab = gBodyIdx;\n            s_bodyTc = 0.0;\n", "            s_bodyTab = gBodyIdx;\n            s_bodyTc = 0.0;\n            s_bodySel = true;\n")
rep("            fillBoneCache(gi, 1.0);\n            DeviceMemoryBarrierWithGroupSync();\n        }\n"
    "        if (g_mapped) {\n            for (uint q = gi; q < P; q += THREADS) {\n                float3 rp, rn;\n                restAtC(q, rp, rn);\n                gx[q] -= rp;\n            }\n"
    "            GroupMemoryBarrierWithGroupSync();\n        }\n",
    "            fillBoneCache(gi, 1.0);\n            DeviceMemoryBarrierWithGroupSync();\n        }\n"
    "#if PES_BODY\n"
    "        // PES21 FX (15/11, Agustin: con la ropa chocando con el cuerpo real en la fisica las mangas temblaban): correccion al dibujar\n"
    "        // (x256 = 2). La fisica ya guardo su estado sin saber de la piel real; aca, solo para la copia que se dibuja, el punto de\n"
    "        // tela que quedo a menos de y256 mm de la piel (o adentro) se saca afuera por el plano de la piel. No vuelve a la fisica:\n"
    "        // no puede realimentarse ni temblar. La tabla del cuerpo se eligio en el paso de fisica de este cuadro (s_bodySel).\n"
    "        if (s_bodySel && s_bodyPrm.x > 1.5 && s_bodyTab != 0xffffffffu) {\n"
    "            [loop] for (uint kd = 0; kd < 2; ++kd) {\n"
    "                uint pd = gi + kd * THREADS;\n"
    "                if (pd < P && pinOf(pd) < 0.999) {\n"
    "                    float3 rd, nd;\n"
    "                    restAtC(pd, rd, nd);\n"
    "                    pesBodyQuery(pd, kd, gx[pd], rd);\n"
    "                    float3 xd = pesBodyLift(pd, kd, 1.0, gx[pd]);\n"
    "                    if (all(isfinite(xd))) gx[pd] = xd;\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "#endif\n"
    "        if (g_mapped) {\n            for (uint q = gi; q < P; q += THREADS) {\n                float3 rp, rn;\n                restAtC(q, rp, rn);\n                gx[q] -= rp;\n            }\n        }\n"
    "        GroupMemoryBarrierWithGroupSync();   // (15/11) siempre: la correccion al dibujar tambien escribe gx\n")
if crlf: t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('latin1'))
print('ok')
