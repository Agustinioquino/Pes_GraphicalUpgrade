# PES21 FX (04/11) - arma los reemplazos de las luces de foco (kSunPass 9-20) con piel: salida de tools/asm2hlsl.py + nombres de
# cbuffer del juego + empalmes de pes_skinlight. Uso: python3 tools/asmspot/mkall.py <carpeta con <hash>.hlsl de asm2hlsl>
import os, sys; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkspot import build
W=sys.argv[1].rstrip('/') + '/'
CREDIT="// Base: tools/asm2hlsl.py sobre el ensamblador del catalogo del mod (traduccion mecanica, identica al juego), con los nombres de\n// los cbuffers del juego. Agustin (03/11): \"la piel en general en todos los menus y modos\".\n\n"
# 68d58aa8: foco solo difusa
build(W+'68d58aa84c0256f5.hlsl','68d58aa84c0256f5-ps_replace.txt',
"// PES 2021 - Luz diferida de FOCO / PUNTO (sin sombra, solo difusa)  [hash 68d58aa84c0256f5]\n"
"// (04/11) Es la luz de estudio del editor (corre x48 por cuadro) y aparece en los focos del partido. No tenia reemplazo: en la\n"
"// piel daba la difusa comun del juego (NdotL envuelto por la translucidez: borde ancho y gris, sin contraluz ni brillo) y la cara\n"
"// del editor no se veia como en el partido. Ahora la piel usa el modelo de las pasadas del sol (pes_skinlight.hlsli): terminador\n"
"// suave por canal, contraluz y el brillo fino de piel (el juego no le da brillo a esta luz: o1 quedaba en 0). Lo demas, identico.\n"+CREDIT,
[('r0.xy = ((v1.xyxx / v1.wwww)).xy;','  float4 skSt = g_SpecularTexture.Sample(g_samplerPoint_Wrap_s, r0.xy);   // (04/11) piel: z marca, x aspereza, y intensidad\n'),
 ('& 15))).w;','  int skMat = (int)r0.w;\n'),
 ('r5.xy = ((r0.yzyy * r5.zzzz)).xy;','  float3 skP = r5.xyz;\n'),
 ('r0.xyz = ((r0.xyzx * r1.zzzz)).xyz;','  float3 skL = r0.xyz;\n'),
 ('r1.yzw = ((r1.yyzw * g_psScene.m_exposure.zzzz)).yzw;','  float3 skC = r1.yzw;\n'),
 ('r2.w = (-(r2.zzzz)).w;','  float3 skN = normalize(r2.xyw);\n  float skCurv = PesSkCurv(skN, skP.z);   // fuera de los if: usa derivadas\n  bool skOn = PesSkIsSkin(skMat, r3.y, skSt.z);\n  float3 skV = -normalize(skP);\n  float skTF = r3.x * r3.y;\n'),
 ('r0.x = ((dot((r0.xyzx).xyz, (r2.xywx).xyz)).xxxx).x;','  float skNL = r0.x;\n'),
 ('r0.xyz = ((r1.yzwy * r0.xxxx)).xyz;','  if (skOn) r0.xyz = (PesSkDiffuse(skNL, r3.y, skTF, skP.z, skCurv) + PesSkTrans(skN, skV, skL, r3.y)) * skC;   // (04/11) piel\n'),
 ('o1.xyzw = (float4(0.0, 0.0, 0.0, asfloat(0x3f800000U))).xyzw;','  if (skOn) o1.xyz = min(PesSkSpec(0.0.xxx, skN, skV, skL, skSt.x, skSt.y, skC * g_psLight.m_lightSourceParam3.y, 1.0), 10000.0.xxx);   // (04/11) brillo de piel\n'),
])
# c33576f1: foco + GGX
build(W+'c33576f12dd818bf.hlsl','c33576f12dd818bf-ps_replace.txt',
"// PES 2021 - Luz diferida de FOCO / PUNTO con brillo GGX (sin sombra)  [hash c33576f12dd818bf]\n"
"// (04/11) Igual que 68d58aa8 mas el especular del juego (GGX con la tabla de Fresnel g_lut1 y luz de esfera). Sin reemplazo\n"
"// hasta hoy: la piel recibia la difusa y el brillo comunes en las luces de estudio del editor y los focos del partido. Ahora la\n"
"// piel usa pes_skinlight (terminador suave, contraluz, brillo de piel y lobulo fino). Lo demas, identico.\n"+CREDIT,
[('r2.xyzw = ((g_SpecularTexture.Sample(','  float skTag = r2.x, skRough = r2.y, skSpecI = r2.z;   // (04/11) piel\n'),
 ('& 15))).w;','  int skMat = (int)r0.w;\n'),
 ('r6.xy = ((r0.yzyy * r6.zzzz)).xy;','  float skZ = r6.z;\n'),
 ('r0.xyz = ((r0.xxxx * r6.xyzx)).xyz;','  float3 skV = -r0.xyz;\n'),
 ('r6.xyz = ((r1.yzwy * r4.wwww)).xyz;','  float3 skL = r6.xyz;\n'),
 ('r3.w = (-(r3.zzzz)).w;','  float3 skN = normalize(r3.xyw);\n  float skCurv = PesSkCurv(skN, skZ);   // fuera de los if: usa derivadas\n  bool skOn = PesSkIsSkin(skMat, r5.y, skTag);\n  float skTF = r5.x * r5.y;\n'),
 ('r0.w = ((dot((r6.xyzx).xyz, (r3.xywx).xyz)).xxxx).w;','  float skNL = r0.w;\n'),
 ('r3.xyz = ((r7.xyzx * r1.zzzz)).xyz;','  if (skOn) r3.xyz = (PesSkDiffuse(skNL, r5.y, skTF, skZ, skCurv) + PesSkTrans(skN, skV, skL, r5.y)) * r7.xyz;   // (04/11) piel\n'),
 ('r0.xyz = ((r2.xzwx * r0.xyzx)).xyz;','  if (skOn) r0.xyz = PesSkSpec(r0.xyz, skN, skV, skL, skRough, skSpecI, r7.xyz, 1.0);   // (04/11) brillo de piel\n'),
])
# 9ce9d846: foco + GGX + sombra
build(W+'9ce9d8463800b399.hlsl','9ce9d8463800b399-ps_replace.txt',
"// PES 2021 - Luz diferida de FOCO / PUNTO con brillo GGX y SOMBRA (12 muestras PCF de t3 con ruido animado)  [hash 9ce9d8463800b399]\n"
"// (04/11) Igual que c33576f1 mas la sombra del foco. Sin reemplazo hasta hoy: la piel recibia la difusa y el brillo comunes en\n"
"// los focos con sombra. Ahora la piel usa pes_skinlight; la sombra del foco se aplica igual que en el original (despues, a la\n"
"// difusa y al brillo). Lo demas, identico.\n"+CREDIT,
[('r2.xyzw = ((g_SpecularTexture.Sample(','  float skTag = r2.x, skRough = r2.y, skSpecI = r2.z;   // (04/11) piel\n'),
 ('r1.y = (float4(0.0, (float)((int)r1.x & 15), 0.0, 0.0)).y;','  int skMat = (int)r1.y;\n'),
 ('r6.xy = ((r1.xwxx * r6.zzzz)).xy;','  float skZ = r6.z;\n'),
 ('r3.w = (-(r3.zzzz)).w;','  float3 skN = normalize(r3.xyw);\n  float skCurv = PesSkCurv(skN, skZ);   // fuera de los if: usa derivadas\n  bool skOn = PesSkIsSkin(skMat, r5.y, skTag);\n  float3 skV = -r7.xyz, skL = r9.xyz;\n  float skTF = r5.x * r5.y;\n'),
 ('r0.y = ((dot((r9.xyzx).xyz, (r3.xywx).xyz)).xxxx).y;','  float skNL = r0.y;\n'),
 ('r3.xyz = ((r10.xyzx * r0.wwww)).xyz;','  if (skOn) r3.xyz = (PesSkDiffuse(skNL, r5.y, skTF, skZ, skCurv) + PesSkTrans(skN, skV, skL, r5.y)) * r10.xyz;   // (04/11) piel\n'),
 ('r0.yzw = ((r2.xxzw * r0.yyzw)).yzw;','  if (skOn) r0.yzw = PesSkSpec(r0.yzw, skN, skV, skL, skRough, skSpecI, r10.xyz, 1.0);   // (04/11) brillo de piel (la sombra del foco va despues)\n'),
])
