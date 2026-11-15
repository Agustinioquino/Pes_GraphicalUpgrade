# Ropa 15/11 (d9ybe2): parchea EN EL LUGAR un cloth_sim.hlsl (repo o VIVO), sin tocar sus lineas PES21FX_LIVE_*.
# x256 = 2 -> "corregir al dibujar": la fisica no choca con la piel real; solo la copia que se dibuja se saca afuera de la piel.
# Uso: python patch_cloth_sim_dibujo.py "<ruta>\cloth_sim.hlsl"  (juego cerrado)
import sys
p = sys.argv[1]
raw = open(p, 'rb').read()
nl = b'\r\n' if b'\r\n' in raw else b'\n'
t = raw.decode('latin1').replace('\r\n', '\n')
if 'correccion al dibujar' in t: sys.exit('ya esta parcheado')
a = "            s_bodyWant = PES_BODY && s_bodyPrm.x > 0.5 && g_legCount == 0;\n"
b = "            s_bodyWant = PES_BODY && s_bodyPrm.x > 0.5 && s_bodyPrm.x < 1.5 && g_legCount == 0;   // (15/11) x256 = 2: la fisica no usa la piel (correccion al dibujar)\n"
if t.count(a) != 1: sys.exit('no encuentro s_bodyWant')
t = t.replace(a, b)
a = "            fillBoneCache(gi, 1.0);\n            DeviceMemoryBarrierWithGroupSync();\n        }\n        if (g_mapped) {\n"
if t.count(a) != 1: sys.exit('no encuentro la salida')
add = r'''#if PES_BODY
        // PES21 FX (15/11, Agustin: la ropa choca con el cuerpo real hacia temblar las mangas): correccion al dibujar (x256 = 2).
        // La fisica ya guardo su estado sin saber de la piel real; aca, solo para la copia que se dibuja, el punto de tela que
        // quedo a menos de y256 mm de la piel (o adentro) se saca afuera por el plano de la piel. No vuelve a la fisica: no
        // puede realimentarse ni temblar. Tambien corre en los cuadros sin paso de fisica (reinicio, pausa).
        s_bodyPrm = IniParams.Load(int2(256, 0));
        if (s_bodyPrm.x > 1.5 && g_legCount == 0) {
            if (gi == 0) { gBodyKey = 0; gBodyIdx = 0xffffffffu; }
            s_bv[0][0] = uint2(0, 0); s_bv[0][1] = uint2(0, 0); s_bv[1][0] = uint2(0, 0); s_bv[1][1] = uint2(0, 0);
            GroupMemoryBarrierWithGroupSync();
            uint bodyKeyD = 0;
            if (gi < PES_BODY_TABLES) {
                uint S = (uint)(IniParams.Load(int2(257, 0)).x + 0.5);
                uint tb = gi * PES_BODY_STRIDE;
                uint st = g_bodyTab[tb], nrec = g_bodyTab[tb + 1], nvt = g_bodyTab[tb + 2], okT = g_bodyTab[tb + 3];
                float3 wt = asfloat(uint3(g_bodyTab[tb + 4], g_bodyTab[tb + 5], g_bodyTab[tb + 6]));
                float dd = distance(wt, float3(me.world[0].w, me.world[1].w, me.world[2].w));
                if (okT == 1 && st == S && nvt > 0 && nrec > 0 && all(isfinite(wt)) && dd < PES_BODY_MATCH)
                    bodyKeyD = 0xFFFFFFFFu - (uint)min(dd * 1.0e6, 4.0e9);
                InterlockedMax(gBodyKey, bodyKeyD);
            }
            GroupMemoryBarrierWithGroupSync();
            if (bodyKeyD != 0 && bodyKeyD == gBodyKey) InterlockedMin(gBodyIdx, gi);
            GroupMemoryBarrierWithGroupSync();
            s_bodyTab = gBodyIdx;
            s_bodyTc = 0.0;
            if (s_bodyTab != 0xffffffffu) {
                [loop] for (uint kd = 0; kd < 2; ++kd) {
                    uint pd = gi + kd * THREADS;
                    if (pd < P && pinOf(pd) < 0.999) {
                        float3 rd, nd;
                        restAtC(pd, rd, nd);
                        pesBodyQuery(pd, kd, gx[pd], rd);
                        float3 xd = pesBodyLift(pd, kd, 1.0, gx[pd]);
                        if (all(isfinite(xd))) gx[pd] = xd;
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
#endif
'''
i = t.index(a) + len(a) - len("        if (g_mapped) {\n")
t = t[:i] + add + t[i:]
open(p, 'wb').write(t.replace('\n', nl.decode()).encode('latin1'))
print('ok')
