// PES21 FX (08/11) - gestos del publico: huesos de la cara (mandibula, ceja izquierda, ceja derecha), hijos del cuello.
// Lo llaman los VS del publico del mod (traduccion exacta del juego con tools/crowd/dxbc2hlsl_bits.py) antes del skinning:
// los huesos actuan en la pose de reposo del modelo y despues el juego aplica su skinning normal (cuello, giro de la cabeza
// por persona, animacion), igual que un hueso hijo del cuello.
//  - pesos: COLOR del vertice (tools/crowd/pesos_cara.py): x = 6/255 marca, y = mandibula, z = ceja izq, w = ceja der.
//    Los modelos del juego traen x = 5/255: no se mueven.
//  - estado de cada hincha: PesCrowdFaceBuf[persona] (publico.cpp, hilo de la CPU): x = giro de la mandibula (rad),
//    y / z = subida de la ceja izq / der (m). persona = indice del juego en instanceBuf (instanceIndexBuf[cb5[9].x + id]).
//  - fila 394: x on (0 = como el juego) | y fuerza de la boca | z fuerza de las cejas | w (libre)
StructuredBuffer<float4> PesCrowdFaceBuf : register(t30);

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
    float3 P = asfloat(vPos.xyz), N = asfloat(vNrm.xyz), T = asfloat(vTan.xyz);
    // mandibula: giro sobre X alrededor de la articulacion (metros del modelo, au00.skl)
    const float3 piv = float3(0.0, 1.650, 0.060);
    float a = clamp(g.x * prm.y, -0.1, 0.45) * wj;
    float s, c; sincos(a, s, c);
    float3 q = P - piv;
    P = piv + float3(q.x, q.y * c - q.z * s, q.y * s + q.z * c);
    N = float3(N.x, N.y * c - N.z * s, N.y * s + N.z * c);
    T = float3(T.x, T.y * c - T.z * s, T.y * s + T.z * c);
    // cejas: traslacion arriba (y un poco adelante, siguiendo la frente)
    float b = clamp(g.y * prm.z, -0.006, 0.015) * wl + clamp(g.z * prm.z, -0.006, 0.015) * wr;
    P += float3(0.0, b, 0.25 * b);
    vPos.xyz = asuint(P); vNrm.xyz = asuint(N); vTan.xyz = asuint(T);
}
