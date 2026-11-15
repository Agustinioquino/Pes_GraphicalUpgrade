# Viento en la ropa 15/11 (sk9ema): parchea EN EL LUGAR el cloth_sim.hlsl VIVO (641ea1c -> ruedo), sin tocar sus lineas PES21FX_LIVE_*.
# Que cambia: (1) con el viento unico el viento vertical ya no sale de cloth.ini (wind_y habia quedado en 296.96 m/s por una
# realimentacion de la DLL) sino solo de la fila 511, con topes; (2) el "viento por debajo de la remera" (w508) actua solo en el
# ruedo de verdad (los ~10 cm de mas abajo de la prenda), sin el aleteo fijo que hacia temblar toda la tela suelta.
# Uso: python patch_cloth_sim_ruedo.py "<juego>\\cloth-mod\\shaders\\cloth_sim.hlsl"  (juego cerrado; copia .antes_ruedo_20261115)
#      agregar --probar al final para solo comprobar que los 9 bloques estan (no escribe nada)
import sys, zlib, shutil
HUNKS = [
# 1) estaticos
[
"static float  s_windHem;     // PES21 FX (14/11): z509 (w508) = cuanto viento le llega por debajo al borde libre (faldon de la remera)\n",
"static float  s_windHem;     // PES21 FX (14/11): z509 (w508) = cuanto viento le llega por debajo al borde libre (faldon de la remera)\n"
"static float  s_hemLow;      // PES21 FX (15/11): altura en la pose de reposo (m) del punto suelto mas bajo de la prenda: de ahi sale el ruedo\n"
],
# 2) compartido del grupo: el punto suelto mas bajo
[
"groupshared uint gBodyIdx;\n",
"groupshared uint gBodyIdx;\n"
"groupshared uint gHemLow;   // PES21 FX (15/11): altura de reposo + 10 m del punto suelto mas bajo (float positivo como uint, para InterlockedMin)\n"
],
# 3) cuanto es ruedo cada punto
[
"void loadParticle(uint p, uint mode, out float3 xPrev, out float3 vel, out float3 restPrev)\n",
"// PES21 FX (15/11): cuanto es RUEDO este punto (0..1): tela suelta (pin bajo) en los ~10 cm de mas abajo de la prenda en la pose\n"
"// de reposo (el faldon de la remera, la boca de la pierna del short). Antes \"borde libre\" era toda la tela con pin < 0.5, o\n"
"// sea casi toda la remera, y el viento por debajo movia el pecho y la espalda.\n"
"float pesHemOf(uint p, float pin)\n"
"{\n"
"    return saturate(1.0 - pin * 2.0) * (1.0 - smoothstep(0.02, 0.12, g_rest[p].posPin.y - s_hemLow));\n"
"}\n"
"void loadParticle(uint p, uint mode, out float3 xPrev, out float3 vel, out float3 restPrev)\n"
],
# 4) viento medio aparte de los remolinos
[
"    float k = g_airDrag, kW = 0.0, pesWs = 0.0;\n    if (any(windBase != 0)) {",
"    float k = g_airDrag, kW = 0.0, pesWs = 0.0;\n    float3 pesSlow = 0;\n    if (any(windBase != 0)) {"
],
[
"        if (s_pesWindOn) { kW = g_windResponse; pesWs = ws; } else k += g_windResponse;\n",
"        if (s_pesWindOn) { kW = g_windResponse; pesWs = ws; pesSlow = slow; } else k += g_windResponse;\n"
],
# 5) el ruedo
[
"        float dn = wn - dot(v, n);\n"
"        if (dn * wn > 0.0) v += n * dn * min(kW * h, 1.0);\n"
"        // (14/11, Agustin: \"que el viento tambien afecte a la remera en la parte de abajo\") el borde libre (faldon, punos) no\n"
"        // solo recibe el viento de frente: el aire que pasa lo arrastra de costado, se mete por debajo y lo hace aletear.\n"
"        // Solo donde la tela esta suelta (pin bajo) y con la fuerza del viento; tampoco frena lo que ya va mas rapido que el aire.\n"
"        const float fr = saturate(1.0 - pin * 2.0) * s_windHem;\n"
"        if (fr > 0.0 && pesWs > 0.05) {\n"
"            const float3 wt = wind - n * wn, vt = v - n * dot(v, n), dt3 = wt - vt;\n"
"            if (dot(dt3, wt) > 0.0) v += dt3 * min(0.35 * kW * h, 1.0) * fr;\n"
"            const float ph = g_time * 13.0 + dot(g_rest[p].posPin.xyz, float3(19.0, 23.0, 31.0)) + float(s) * h * 13.0;\n"
"            v += n * (sin(ph) * 0.7 + sin(ph * 1.71 + 1.3) * 0.3) * (3.0 * pesWs) * fr * h;   // aleteo: ~1 cm a 5 m/s\n"
"            v.y += 0.15 * pesWs * fr * h;                                                     // el aire que entra por debajo lo levanta un poco\n"
"        }\n",
"        float dn = wn - dot(v, n);\n"
"        // (14/11, Agustin: \"que el viento tambien afecte a la remera en la parte de abajo\"; 15/11: \"desde que pusimos que afecte\n"
"        // la parte de abajo se rompio un poco\") el efecto de abajo se aplicaba a TODA la tela suelta, con una onda fija de varios\n"
"        // cm (3 x viento m/s2 a 2 Hz = 9 cm a 5 m/s, no 1 cm) que temblaba aun en calma, y con los remolinos tirando de costado.\n"
"        // Ahora solo el ruedo de verdad (pesHemOf), y sin onda propia: al ruedo el viento le pega mas (el aire entra por debajo:\n"
"        // x1.6 con w508 = 1), el viento MEDIO lo arrastra apenas de costado y lo levanta un poco. El aleteo sale de los remolinos\n"
"        // del propio viento, que viajan con el. Con menos de 0.3 m/s no hay nada; entra de a poco hasta 1 m/s.\n"
"        const float hem = pesHemOf(p, pin) * s_windHem * saturate((pesWs - 0.3) / 0.7);\n"
"        if (dn * wn > 0.0) v += n * dn * min(kW * (1.0 + 0.6 * hem) * h, 1.0);\n"
"        if (hem > 0.0) {\n"
"            const float3 wt = pesSlow - n * dot(pesSlow, n), vt = v - n * dot(v, n), dt3 = wt - vt;\n"
"            if (dot(dt3, wt) > 0.0) v += dt3 * min(0.10 * kW * h, 1.0) * min(hem, 1.0);\n"
"            const float gl = length(s_grav);                                            // arriba = contra la gravedad de la prenda\n"
"            if (gl > 1e-4) v -= (s_grav / gl) * (min(0.10 * pesWs, 1.2) * hem * h);    // el aire que entra por debajo lo levanta un poco\n"
"        }\n"
],
# 6) viento vertical: solo la fila 511 (nunca cloth.ini), con topes; onda de turbulencia con velocidad fija
[
"            if (pesWOn) { const float kC = pesEffOk ? pesEff.y : IniParams.Load(int2(503, 0)).w; pesWU.z *= kC;\n"
"                          windBase = mul(transpose(W), float3(pesWU.x * pesWU.z, g_wind.y + IniParams.Load(int2(511, 0)).x * kC, pesWU.y * pesWU.z)); }\n",
"            // (15/11) el viento vertical ya NO sale de cloth.ini (wind_y): la DLL lo habia dejado en 296.96 m/s al duplicarlo en cada\n"
"            // arranque y la tela salia disparada para arriba. Con el viento unico la vertical es solo la de la fila 511, con tope;\n"
"            // la velocidad total tambien tiene tope (40 m/s) y la onda de turbulencia va a velocidad fija (no depende de cloth.ini)\n"
"            if (pesWOn) { const float kC = pesEffOk ? pesEff.y : IniParams.Load(int2(503, 0)).w; pesWU.z = min(pesWU.z * kC, 40.0);\n"
"                          windBase = mul(transpose(W), float3(pesWU.x * pesWU.z, clamp(IniParams.Load(int2(511, 0)).x * kC, -15.0, 15.0), pesWU.y * pesWU.z));\n"
"                          s_windLen = 2.0; }\n"
],
# 7) el punto suelto mas bajo de la prenda (una vez por cuadro, con las barreras que ya habia)
[
"            if (gi == 0) { gBodyKey = 0; gBodyIdx = 0xffffffffu; gBodyCnt[0] = 0; gBodyCnt[1] = 0; gBodyCnt[2] = 0; }\n"
"            GroupMemoryBarrierWithGroupSync();\n"
"            uint bodyKey = 0;\n",
"            if (gi == 0) { gBodyKey = 0; gBodyIdx = 0xffffffffu; gBodyCnt[0] = 0; gBodyCnt[1] = 0; gBodyCnt[2] = 0; gHemLow = 0x7f7fffffu; }\n"
"            GroupMemoryBarrierWithGroupSync();\n"
"            // (15/11) ruedo: cada hilo aporta la altura de reposo de sus dos puntos si estan sueltos (pin < 0.5)\n"
"            if (gi < P && pinOf(gi) < 0.5) InterlockedMin(gHemLow, asuint(g_rest[gi].posPin.y + 10.0));\n"
"            if (gi + THREADS < P && pinOf(gi + THREADS) < 0.5) InterlockedMin(gHemLow, asuint(g_rest[gi + THREADS].posPin.y + 10.0));\n"
"            uint bodyKey = 0;\n"
],
[
"            s_bodyTab = gBodyIdx;\n"
"            s_bodyTc = 0.0;\n"
"#if PES_BODY_DIAG\n"
"            if (gi == 0 && s_bodyWant) {\n",
"            s_bodyTab = gBodyIdx;\n"
"            s_bodyTc = 0.0;\n"
"            s_hemLow = asfloat(gHemLow) - 10.0;   // (15/11) sin puntos sueltos queda enorme: pesHemOf da 0 igual (pin alto)\n"
"#if PES_BODY_DIAG\n"
"            if (gi == 0 && s_bodyWant) {\n"
]
]
p = sys.argv[1]
b = open(p, 'rb').read()
s = b.decode('latin-1')
crlf = '\r\n' in s
t = s.replace('\r\n', '\n')
if 'pesHemOf' in t: sys.exit('YA TIENE EL RUEDO (pesHemOf): no se toca')
if 'int2(509, 0)' not in t: sys.exit('NO tiene el viento 509 (falta patch_cloth_sim_viento509.py): no se toca')
for k, (a, c) in enumerate(HUNKS):
    m = t.count(a)
    if m != 1: sys.exit('bloque %d encontrado %d veces (debe ser 1): NO se toco nada' % (k + 1, m))
if '--probar' in sys.argv: sys.exit('PRUEBA OK: los %d bloques estan una vez cada uno; no se escribio nada (CRC actual %08X)' % (len(HUNKS), zlib.crc32(b) & 0xffffffff))
for a, c in HUNKS: t = t.replace(a, c)
if crlf: t = t.replace('\n', '\r\n')
bak = p + '.antes_ruedo_20261115'
shutil.copy2(p, bak)
nb = t.encode('latin-1')
open(p, 'wb').write(nb)
print('antes %08X (%d B) -> despues %08X (%d B); %d bloques; copia %s' % (zlib.crc32(b) & 0xffffffff, len(b), zlib.crc32(nb) & 0xffffffff, len(nb), len(HUNKS), bak))
