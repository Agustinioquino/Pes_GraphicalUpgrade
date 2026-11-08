// PES21 FX (08/11) - gestos del publico: huesos de la cara (mandibula, ceja izquierda, ceja derecha), hijos del cuello.
// Lo llaman los VS del publico del mod (traduccion exacta del juego con tools/crowd/dxbc2hlsl_bits.py) antes del skinning:
// los huesos actuan en la pose de reposo del modelo y despues el juego aplica su skinning normal (cuello, giro de la cabeza
// por persona, animacion), igual que un hueso hijo del cuello.
//  - pesos: COLOR del vertice (tools/crowd/pesos_cara.py): x = 6/255 marca, y = mandibula, z = ceja izq, w = ceja der.
//    Los modelos del juego traen x = 5/255: no se mueven.
//  - estado de cada hincha: PesCrowdFaceBuf[persona] (publico.cpp, hilo de la CPU): x = giro de la mandibula (rad),
//    y / z = subida de la ceja izq / der (m), w = pulso del grito propio (0..1). persona = indice del juego en instanceBuf.
//  - REACCIONES (08/11 b, pedido de Agustin: "mas de una animacion, con las animaciones que ya hacen como disparador"):
//    se lee la pose del cuerpo que el juego le esta poniendo a ese hincha (animeInstanceBuf t2, huesos de au00.skl) y se
//    elige la cara: brazos arriba / salto = festejo (boca bien abierta, cejas arriba), manos en la cabeza = lamento (boca
//    abierta, cejas muy arriba), brazo adelante a la altura de la cara = protesta (grita al ritmo, ceno fruncido),
//    aplauso (manos juntas adelante) = canta al ritmo de las palmas; sin disparador queda el canto / charla de la CPU.
//  - fila 394: x on (0 = como el juego) | y fuerza de la boca | z fuerza de las cejas | w animo (CPU)
StructuredBuffer<float4> PesCrowdFaceBuf : register(t30);

// posicion animada de un punto de la pose de reposo pegado al hueso i (matriz de skinning float4x3 en filas, como el juego)
float3 PesCrowdBone(uint base, uint i, float3 p)
{
    PESS2_48 m = t2[base + i];
    float4 q = float4(p, 1.0);
    return float3(dot(asfloat(uint4(m.v[0], m.v[1], m.v[2], m.v[3])), q),
                  dot(asfloat(uint4(m.v[4], m.v[5], m.v[6], m.v[7])), q),
                  dot(asfloat(uint4(m.v[8], m.v[9], m.v[10], m.v[11])), q));
}

// x mandibula (rad), y subida de las cejas (m), w peso total de los disparadores (0..1)
float4 PesCrowdReaction(uint persona, float shout)
{
    if (asfloat(cb5[8].x) == 0.0) return 0;                        // sin animacion (el juego dibuja la pose quieta)
    uint base = (t0[persona].v[8] & 255u) * 34u;                   // animId * 34 (17 huesos por cuadro)
    // huesos de au00.skl: 0 panza, 2 cuello, 6 mano izq, 10 mano der (pose de reposo en metros, mira a +z)
    float3 head = PesCrowdBone(base, 2u, float3(0.0, 1.660, 0.050));
    float3 neck = PesCrowdBone(base, 2u, float3(0.0, 1.532, 0.033));
    float3 hl = PesCrowdBone(base, 6u, float3(0.600, 1.090, 0.250));
    float3 hr = PesCrowdBone(base, 10u, float3(-0.600, 1.090, 0.250));
    float3 belly = PesCrowdBone(base, 0u, float3(0.0, 1.096, 0.0));
    float3 fwd = normalize(float3(head.x - neck.x, 0.0, head.z - neck.z) + float3(0.0, 0.0, 0.08));
    float up = smoothstep(0.02, 0.22, max(hl.y, hr.y) - head.y);                     // brazo(s) arriba
    float upBoth = smoothstep(0.00, 0.18, min(hl.y, hr.y) - head.y);
    float jump = smoothstep(0.03, 0.10, belly.y - 1.096);                             // saltando
    float dHead = min(length(hl - head), length(hr - head));
    float onHead = smoothstep(0.24, 0.13, dHead) * (1.0 - upBoth);                    // manos en la cabeza
    float reachL = dot(hl - neck, fwd), reachR = dot(hr - neck, fwd);
    float pointH = max(reachL * smoothstep(0.25, 0.05, abs(hl.y - head.y)), reachR * smoothstep(0.25, 0.05, abs(hr.y - head.y)));
    float protest = smoothstep(0.38, 0.55, pointH) * (1.0 - up) * (1.0 - onHead);     // brazo adelante a la altura de la cara
    float dHands = length(hl - hr);
    float handsFront = smoothstep(0.10, 0.25, 0.5 * (reachL + reachR)) * smoothstep(0.30, 0.10, abs(0.5 * (hl.y + hr.y) - neck.y + 0.15));
    float clap = smoothstep(0.45, 0.25, dHands) * handsFront * (1.0 - up) * (1.0 - onHead);
    float apart = smoothstep(0.06, 0.30, dHands);                                     // palmas separadas = la boca abre

    float cel = max(up * (0.6 + 0.4 * upBoth), jump);
    float wsum = cel + onHead + protest + clap;
    if (wsum <= 0.001) return 0;
    float jaw = cel * (0.26 + 0.12 * shout + 0.06 * jump)
              + onHead * (0.20 + 0.04 * shout)
              + protest * (0.06 + 0.22 * shout)
              + clap * (0.04 + 0.18 * apart);
    float brow = cel * (0.008 + 0.003 * shout) + onHead * 0.013 + protest * -0.004 + clap * 0.003 * apart;
    return float4(jaw / wsum, brow / wsum, 0.0, saturate(wsum));
}

void PesCrowdFace(inout uint4 vPos, inout uint4 vNrm, inout uint4 vTan, uint4 vCol, uint persona)
{
    float4 col = asfloat(vCol);
    if (abs(col.x * 255.0 - 6.0) > 0.5) return;
    float wj = col.y, wl = col.z, wr = col.w;
    if (wj + wl + wr <= 0.0) return;
    float4 prm = IniParams.Load(int2(394, 0));
    if (prm.x <= 0.0) return;
    uint n = 0, st = 0; PesCrowdFaceBuf.GetDimensions(n, st);
    if (persona >= n) return;
    float4 g = PesCrowdFaceBuf[persona];
    // reaccion a la animacion del cuerpo: reemplaza al canto / charla segun cuanto dispara
    float4 r = PesCrowdReaction(persona, saturate(g.w));
    float jawA = lerp(g.x, r.x, r.w);
    float browL = lerp(g.y, r.y * (g.y >= g.z ? 1.0 : 0.85), r.w);
    float browR = lerp(g.z, r.y * (g.z > g.y ? 1.0 : 0.85), r.w);
    float3 P = asfloat(vPos.xyz), N = asfloat(vNrm.xyz), T = asfloat(vTan.xyz);
    // mandibula: giro sobre X alrededor de la articulacion (metros del modelo, au00.skl)
    const float3 piv = float3(0.0, 1.650, 0.060);
    float a = clamp(jawA * prm.y, -0.1, 0.45) * wj;
    float s, c; sincos(a, s, c);
    float3 q = P - piv;
    P = piv + float3(q.x, q.y * c - q.z * s, q.y * s + q.z * c);
    N = float3(N.x, N.y * c - N.z * s, N.y * s + N.z * c);
    T = float3(T.x, T.y * c - T.z * s, T.y * s + T.z * c);
    // cejas: traslacion arriba (y un poco adelante, siguiendo la frente)
    float b = clamp(browL * prm.z, -0.006, 0.015) * wl + clamp(browR * prm.z, -0.006, 0.015) * wr;
    P += float3(0.0, b, 0.25 * b);
    vPos.xyz = asuint(P); vNrm.xyz = asuint(N); vTan.xyz = asuint(T);
}
