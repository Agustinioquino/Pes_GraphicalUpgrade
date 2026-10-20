#!/usr/bin/env python3
"""PES21 FX - arma 199d37eebb6940ad-ps_replace.txt (banderas y red, 145) sobre la traduccion mecanica del juego.
Uso: python3 tools/mods/199d37eebb6940ad.py   (lee tools/asmcheck/199d37eebb6940ad_orig_ps.hlsl, escribe en package/)
Cada bloque se inserta junto a una linea ancla del original (tiene que aparecer una sola vez)."""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'tools', 'asmcheck', '199d37eebb6940ad_orig_ps.hlsl')
DST = os.path.join(ROOT, 'package', 'PES21FX', 'shaders', '199d37eebb6940ad-ps_replace.txt')

HEADER = r'''// PES 2021 - Tela con luz propia: banderas de corner y del estadio, red del arco (forward, mezcla con alfa)  [hash 199d37eebb6940ad]
// PES21 (20/10, 145). Base: traduccion mecanica del ensamblador (tools/asm2hlsl.py), identica al juego: sol con sombra en
// cascadas (4 muestras) + sombra precalculada (VSM), 3 luces puntuales, ambiente de hemisferio, textura de patron, niebla en
// volumen y salida premultiplicada. El juego ilumina la tela con media Lambert ((N.L + 1) / 2): todo lavado y plano.
// PES21 FX (x145 = 1):
//  - banderas (tela opaca): Lambert de frente con poca envoltura + la luz que ATRAVIESA la tela de espaldas al sol, con el
//    color de la tela (a contraluz se encienden como en TV) + brillo de tela en el borde (y145 transmision, z145 brillo)
//  - red (alfa con agujeros, cobertura media < 0.7): hebras redondas (normal curvada con el perfil del alfa), brillo del
//    nylon a lo largo de la hebra, trenzado helicoidal de cerca y la red que no desaparece a lo lejos (w145)'''

B1 = r'''  // ===== PES21 (20/10, 145): tela de banderas y red =====
  float4 FL = IniParams.Load(int2(145,0));
  bool pesOn = FL.x > 0.5;
  float3 pesNgeo = r3.xyz;                                    // normal del juego (con normal map), mundo
  float3 pesV = normalize(cb2[20].xyz - v4.xyz);               // hacia la camara
  float pesCov = t0.SampleLevel(s12, v1.xy, 6.0).w;           // cobertura media del alfa: red ~0.3, bandera ~1
  bool pesNet = pesCov < 0.7;
  float2 pesTex; t0.GetDimensions(pesTex.x, pesTex.y);
  float3 pesDPx = ddx(v4.xyz), pesDPy = ddy(v4.xyz);           // derivadas fuera de las ramas
  float pesFoot = max(length(pesDPx), length(pesDPy));         // metros por pixel
  float pesLod = log2(max(max(length(ddx(v1.xy * pesTex)), length(ddy(v1.xy * pesTex))), 1e-3));
  float3 pesT = 0; float pesS = 0;
  if (pesOn && pesNet && FL.w > 0.001) {
    // hebras redondas: el perfil del alfa (de un mip mas borroso) dice donde cae el pixel a lo ancho de la hebra y su gradiente
    // la direccion; la normal se curva hacia afuera del centro -> cada hebra tiene lado iluminado y lado en sombra
    float lodB = max(pesLod, 0.0) + 2.0;
    float2 tx = exp2(lodB) / pesTex;
    float a0 = t0.SampleLevel(s12, v1.xy, lodB).w;
    float au = t0.SampleLevel(s12, v1.xy + float2(tx.x, 0.0), lodB).w - t0.SampleLevel(s12, v1.xy - float2(tx.x, 0.0), lodB).w;
    float av = t0.SampleLevel(s12, v1.xy + float2(0.0, tx.y), lodB).w - t0.SampleLevel(s12, v1.xy - float2(0.0, tx.y), lodB).w;
    float3 g = au * normalize(v6.xyz) + av * normalize(v7.xyz);
    float gl = length(g);
    float nearK = saturate(1.0 - (pesLod - 2.0) * 0.5);        // completo de cerca, nada desde lod 4
    if (gl > 1e-4 && nearK > 0.0) {
      float3 gw = g / gl;
      float3 Nv = dot(pesNgeo, pesV) < 0.0 ? -pesNgeo : pesNgeo;
      pesS = sqrt(saturate(1.0 - a0 * 1.4)) * nearK * saturate(FL.w);
      r3.xyz = normalize(Nv * sqrt(saturate(1.0 - pesS * pesS)) - gw * pesS);
      pesT = normalize(cross(Nv, gw));
    }
  }'''

B2 = r'''  float3 pesSunL = r6.xyz * r0.z;                               // sol que llega (color x visibilidad)
  float pesTr = 0.0, pesSh = 0.0;
  if (pesOn && !pesNet) {
    // tela fina (bandera): de frente Lambert con un poco de envoltura; de espaldas al sol la luz la ATRAVIESA (sale del color
    // de la tela: a contraluz la bandera se enciende); brillo de tela en el borde. Antes: media Lambert (todo lavado y plano)
    float3 Lt = -cb3[0].xyz;
    float3 Nv = dot(r3.xyz, pesV) < 0.0 ? -r3.xyz : r3.xyz;
    float ndl = dot(Nv, Lt);
    float front = saturate((ndl + 0.2) / 1.2);
    r0.w = front;
    pesTr = saturate(-ndl) * 0.5 * saturate(FL.y);
    pesSh = pow(1.0 - saturate(dot(Nv, pesV)), 5.0) * front * saturate(FL.z) * 0.6;
  }'''

B3 = r'''  float3 pesAlb = r1.xyz; float pesBoost = r0.z;'''

B4 = r'''  if (pesOn) {
    float lA = dot(pesAlb, float3(0.2126, 0.7152, 0.0722));
    float3 tint = saturate(pesAlb / max(lA, 0.04) * 0.5);
    r1.xyz += pesAlb * tint * pesSunL * (pesTr * pesBoost);                         // luz que atraviesa la tela
    r1.xyz += lerp(pesAlb, lA.xxx, 0.5) * pesSunL * (pesSh * pesBoost);              // brillo de tela
    if (pesNet && FL.w > 0.001 && dot(pesT, pesT) > 0.5) {
      // nylon al sol: brillo a lo largo de la hebra (Kajiya-Kay) y trenzado helicoidal de cerca (periodo 4 mm)
      float3 H = normalize(-cb3[0].xyz + pesV);
      float th = dot(pesT, H);
      r1.xyz += pesSunL * (pow(sqrt(saturate(1.0 - th * th)), 40.0) * 0.12 * saturate(FL.w));
      float fib = saturate(1.0 - pesFoot / 0.0015) * saturate(FL.w);
      float tw = dot(v4.xyz, pesT) / 0.004 + pesS * 0.5;
      r1.xyz *= lerp(1.0, 0.8 + 0.2 * (0.5 + 0.5 * sin(6.2831853 * tw)), fib);
    }
  }'''

B5 = r'''  if (pesOn && pesNet) r0.z = saturate(r0.z * (1.0 + 0.1 * max(pesLod, 0.0)));   // la red no desaparece a lo lejos (el mip promedia el alfa)'''

# (ancla, bloque, 'antes' | 'despues')
STEPS = [
    ('r4.xyz = ((-(v4.xyzx) + cb5[9].xyzx)).xyz;', B1, 'antes'),
    ('r0.w = ((r0.wwww * float4(0.500000, 0.500000, 0.500000, 0.500000))).w;', B2, 'despues'),
    ('r2.xyz = ((r2.xyzx * r0.zzzz + r3.xyzx)).xyz;', B3, 'antes'),
    ('r1.xyz = ((r1.xyzx * r2.xyzx)).xyz;', B4, 'despues'),
    ('r0.z = ((r1.wwww * v4.wwww)).z;', B5, 'despues'),
]

def main():
    lines = open(SRC).read().split('\n')
    assert lines[0].startswith('// generado por tools/asm2hlsl.py'), 'no es la traduccion mecanica'
    lines = HEADER.split('\n') + lines[1:]
    for anchor, block, where in STEPS:
        idx = [i for i, l in enumerate(lines) if l.strip() == anchor]
        if len(idx) != 1: sys.exit('ancla %r: %d apariciones' % (anchor, len(idx)))
        i = idx[0] + (1 if where == 'despues' else 0)
        lines[i:i] = block.split('\n')
    open(DST, 'w', newline='\n').write('\n'.join(lines))
    print('ok:', DST)

if __name__ == '__main__':
    main()
