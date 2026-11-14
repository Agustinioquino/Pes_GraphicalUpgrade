# Viento 14/11 (sk9ema 641ea1c): parchea EN EL LUGAR el cloth_sim.hlsl VIVO (279069a -> 641ea1c), sin tocar sus lineas PES21FX_LIVE_*.
# Uso: python patch_cloth_sim_viento509.py "<juego>\\cloth-mod\\shaders\\cloth_sim.hlsl"  (juego cerrado; copia .antes_viento509_20261114)
import sys, zlib, shutil
HUNKS = [
[
"static float  s_windLen; // PES21 FX (13/11): largo del viento de cloth.ini (fijo en el partido): marca la velocidad de la onda de turbulencia\nstatic bool   s_menu;   // PES21 FX (25/10): escena de menu (editor, presentacion): sin empujes por movimiento del cuerpo",
"static float  s_windLen; // PES21 FX (13/11): largo del viento de cloth.ini (fijo en el partido): marca la velocidad de la onda de turbulencia\nstatic bool   s_pesWindOn;   // PES21 FX (14/11): esta prenda usa el viento unico de la DLL (fila 494)\nstatic float  s_windHem;     // PES21 FX (14/11): z509 (w508) = cuanto viento le llega por debajo al borde libre (faldon de la remera)\nstatic bool   s_menu;   // PES21 FX (25/10): escena de menu (editor, presentacion): sin empujes por movimiento del cuerpo"
],
[
"    float3 wind = 0;\n    float k = g_airDrag;\n    if (any(windBase != 0)) {",
"    float3 wind = 0;\n    float k = g_airDrag, kW = 0.0, pesWs = 0.0;\n    if (any(windBase != 0)) {"
],
[
"        wind = slow + ws * T * (wd * 0.5 * e1 + side * 0.6 * e2 + up * 0.35 * e3);\n        k += g_windResponse;\n    }",
"        wind = slow + ws * T * (wd * 0.5 * e1 + side * 0.6 * e2 + up * 0.35 * e3);\n        if (s_pesWindOn) { kW = g_windResponse; pesWs = ws; } else k += g_windResponse;\n    }"
],
[
"    }\n    v -= n * dot(v - wind, n) * min(k * h, 1.0);\n    vel = v;",
"    }\n    if (!s_pesWindOn) v -= n * dot(v - wind, n) * min(k * h, 1.0);\n    else {\n        // PES21 FX (14/11, Agustin: \"que el viento no le quite los valores de inercia y demas\") con el viento unico la resistencia\n        // del aire es la de siempre (g_airDrag + la de correr) pero contra el aire que se mueve, y la respuesta al viento\n        // (wind_response) es de UN solo lado: acelera la tela hasta la velocidad del aire y nunca la frena. Antes wind_response\n        // se sumaba al freno y apagaba la inercia, el giro y el rebote de la tela en cuanto soplaba algo.\n        const float wn = dot(wind, n);\n        v -= n * (dot(v, n) - wn) * min(k * h, 1.0);\n        float dn = wn - dot(v, n);\n        if (dn * wn > 0.0) v += n * dn * min(kW * h, 1.0);\n        // (14/11, Agustin: \"que el viento tambien afecte a la remera en la parte de abajo\") el borde libre (faldon, punos) no\n        // solo recibe el viento de frente: el aire que pasa lo arrastra de costado, se mete por debajo y lo hace aletear.\n        // Solo donde la tela esta suelta (pin bajo) y con la fuerza del viento; tampoco frena lo que ya va mas rapido que el aire.\n        const float fr = saturate(1.0 - pin * 2.0) * s_windHem;\n        if (fr > 0.0 && pesWs > 0.05) {\n            const float3 wt = wind - n * wn, vt = v - n * dot(v, n), dt3 = wt - vt;\n            if (dot(dt3, wt) > 0.0) v += dt3 * min(0.35 * kW * h, 1.0) * fr;\n            const float ph = g_time * 13.0 + dot(g_rest[p].posPin.xyz, float3(19.0, 23.0, 31.0)) + float(s) * h * 13.0;\n            v += n * (sin(ph) * 0.7 + sin(ph * 1.71 + 1.3) * 0.3) * (3.0 * pesWs) * fr * h;   // aleteo: ~1 cm a 5 m/s\n            v.y += 0.15 * pesWs * fr * h;                                                     // el aire que entra por debajo lo levanta un poco\n        }\n    }\n    vel = v;"
],
[
"            float4 pesWU = IniParams.Load(int2(494, 0));\n            bool pesWOn = PES_CLOTH_FX && pesWU.z > 0.0 && IniParams.Load(int2(353, 0)).z > 0.5 && any(g_wind.xyz != 0);\n            // (13/11 etapa 2b) w503 = cuanto viento le llega a la ropa (menu Viento; 1 = real, 0 = sin viento de costado)\n            if (pesWOn) { pesWU.z *= IniParams.Load(int2(503, 0)).w; windBase = mul(transpose(W), float3(pesWU.x * pesWU.z, g_wind.y, pesWU.y * pesWU.z)); }\n            // PES21 FX (25/10): el cuerpo real de este personaje (cuerpo.cpp): de las tablas que la DLL armo en el ultimo Present (una",
"            float4 pesWU = IniParams.Load(int2(494, 0));\n            // (14/11, Agustin: \"en las cinematicas mantenga este valor\") ya no hace falta que cloth.ini tenga viento en este cuadro\n            // (el mod de ropa lo apaga fuera del partido y en las cinematicas): alcanza con que no sea un menu (s_menu, w255 de la\n            // DLL); en el vestuario y el tunel el viento ya lo baja la DLL (bajo techo, y500)\n            bool pesWOn = PES_CLOTH_FX && pesWU.z > 0.0 && IniParams.Load(int2(353, 0)).z > 0.5 && !s_menu;\n            // (13/11 etapa 2b / 14/11) fila 509 (la escribe la DLL, w = 1): y = cuanto viento le llega a la ropa ya con la altura\n            // (w503 x perfil por altura de la fila 508), z = viento por debajo del borde (w508). Sin DLL nueva: w503 y 1 como antes\n            float4 pesEff = IniParams.Load(int2(509, 0));\n            const bool pesEffOk = pesEff.w > 0.5;\n            // x511 (la escribe la DLL) = viento vertical en m/s (+ = desde abajo); hoy solo lo usa \"Probar el viento\" del menu\n            if (pesWOn) { const float kC = pesEffOk ? pesEff.y : IniParams.Load(int2(503, 0)).w; pesWU.z *= kC;\n                          windBase = mul(transpose(W), float3(pesWU.x * pesWU.z, g_wind.y + IniParams.Load(int2(511, 0)).x * kC, pesWU.y * pesWU.z)); }\n            s_pesWindOn = pesWOn; s_windHem = pesEffOk ? pesEff.z : 1.0;\n            // PES21 FX (25/10): el cuerpo real de este personaje (cuerpo.cpp): de las tablas que la DLL armo en el ultimo Present (una"
]
]
p = sys.argv[1]
b = open(p, 'rb').read()
s = b.decode('latin-1')
crlf = '\r\n' in s
t = s.replace('\r\n', '\n')
if 'int2(509, 0)' in t: sys.exit('YA TIENE 509: no se toca')
for k, (a, c) in enumerate(HUNKS):
    m = t.count(a)
    if m != 1: sys.exit('bloque %d encontrado %d veces (debe ser 1): NO se toco nada' % (k + 1, m))
for a, c in HUNKS: t = t.replace(a, c)
if crlf: t = t.replace('\n', '\r\n')
shutil.copy2(p, p + '.antes_viento509_20261114')
nb = t.encode('latin-1')
open(p, 'wb').write(nb)
print('antes %08X (%d B) -> despues %08X (%d B); %d bloques; copia %s' % (zlib.crc32(b) & 0xffffffff, len(b), zlib.crc32(nb) & 0xffffffff, len(nb), len(HUNKS), p + '.antes_viento509_20261114'))
