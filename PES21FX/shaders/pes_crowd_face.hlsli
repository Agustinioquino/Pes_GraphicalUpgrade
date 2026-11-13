// PES21 FX (08/11) - gestos del publico: huesos de la cara (mandibula, ceja izquierda, ceja derecha), hijos del cuello.
// Lo llaman los VS del publico del mod (traduccion exacta del juego con tools/crowd/dxbc2hlsl_bits.py) antes del skinning:
// los huesos actuan en la pose de reposo del modelo y despues el juego aplica su skinning normal (cuello, giro de la cabeza
// por persona, animacion), igual que un hueso hijo del cuello.
//  - pesos: COLOR del vertice (tools/crowd/pesos_cara.py): x = 6/255 marca, y = mandibula, z = ceja izq, w = ceja der.
//    Los modelos del juego traen x = 5/255: no se mueven.
//  - estado de cada hincha: PesCrowdFaceBuf[2 * persona] (publico.cpp, hilo de la CPU): x = giro de la mandibula (rad),
//    y / z = subida de la ceja izq / der (m), w = pulso del grito propio (0..1). persona = indice del juego en instanceBuf.
//  - REACCIONES (08/11 b, pedido de Agustin: "mas de una animacion, con las animaciones que ya hacen como disparador"):
//    se lee la pose del cuerpo que el juego le esta poniendo a ese hincha (animeInstanceBuf t2, huesos de au00.skl) y se
//    elige la cara: brazos arriba / salto = festejo (boca bien abierta, cejas arriba), manos en la cabeza = lamento (boca
//    abierta, cejas muy arriba), brazo adelante a la altura de la cara = protesta (grita al ritmo, ceno fruncido),
//    aplauso (manos juntas adelante) = canta al ritmo de las palmas; sin disparador queda el canto / charla de la CPU.
//  - CUERPO (08/11 c): PesCrowdFaceBuf[2 * persona + 1] = capa de brazos / salto encima de la animacion (PesCrowdBody).
//    La cara esta en PesCrowdFaceBuf[2 * persona].
//  - SIN CAMISETA / REVOLEO (08/11 d): body.w + 10 = sin camiseta (PesCrowdShirtless), body.w = +-2 = revolea con ese brazo.
//  - REMERA EN LA MANO (08/11 e): PesCrowdShirtInHand, con los colores del kit del hincha.
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

// matriz de skinning de un hueso como float3x4 (filas)
float3x4 PesCrowdMat(uint base, uint i)
{
    PESS2_48 m = t2[base + i];
    return float3x4(asfloat(uint4(m.v[0], m.v[1], m.v[2], m.v[3])), asfloat(uint4(m.v[4], m.v[5], m.v[6], m.v[7])),
                    asfloat(uint4(m.v[8], m.v[9], m.v[10], m.v[11])));
}

float3x3 PesInv3(float3x3 m)
{
    float3 a = m[0], b = m[1], c = m[2];
    float3 r0 = cross(b, c), r1 = cross(c, a), r2 = cross(a, b);
    float det = dot(a, r0);
    if (abs(det) < 1e-8) return float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);
    return transpose(float3x3(r0, r1, r2)) / det;
}

float3 PesRotAxis(float3 v, float3 k, float ang)
{
    float s, c; sincos(ang, s, c);
    return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}

// (08/11 c) capa de cuerpo encima de la animacion del juego: brazos que se agitan / levantan y saltos (festejo de gol,
// protesta con el puno, tribuna que canta). Se aplica en la pose de reposo como el desplazamiento que, despues del skinning
// del juego, da el movimiento pedido en la pose animada: p' = p + M^-1 d (M = matriz de skinning mezclada del vertice).
//  body (PesCrowdFaceBuf[2 * persona + 1], CPU): x cuanto levanta / agita (0..1), y fase del agitado (rad),
//  z salto (m), w brazo (-1 solo der, 0 los dos, 1 solo izq)
void PesCrowdBody(inout float3 P, inout float3 N, inout float3 T, uint4 vIdx, float4 vW, uint persona, float4 body)
{
    if (asfloat(cb5[8].x) == 0.0) return;
    uint base = (t0[persona].v[8] & 255u) * 34u;
    float3x4 M = PesCrowdMat(base, vIdx.x) * vW.x + PesCrowdMat(base, vIdx.y) * vW.y + PesCrowdMat(base, vIdx.z) * vW.z + PesCrowdMat(base, vIdx.w) * vW.w;
    float3 X = mul(M, float4(P, 1.0));
    float3 sl = PesCrowdBone(base, 3u, float3(0.105, 1.467, 0.033)), sr = PesCrowdBone(base, 7u, float3(-0.105, 1.467, 0.033));
    float3 ul = PesCrowdBone(base, 4u, float3(0.195, 1.467, 0.033)), ur = PesCrowdBone(base, 8u, float3(-0.195, 1.467, 0.033));
    float3 hl = PesCrowdBone(base, 6u, float3(0.600, 1.090, 0.250)), hr = PesCrowdBone(base, 10u, float3(-0.600, 1.090, 0.250));
    float3 fwd = normalize(cross(sl - sr, float3(0.0, 1.0, 0.0)) + float3(0.0, 0.0, 1e-4));
    // peso de cada brazo en este vertice (hombro a medias)
    float aL = 0.0, aR = 0.0;
    [unroll] for (int k = 0; k < 4; k++) {
        uint bi = vIdx[k]; float w = vW[k];
        aL += (bi >= 4u && bi <= 6u) ? w : (bi == 3u ? 0.5 * w : 0.0);
        aR += (bi >= 8u && bi <= 10u) ? w : (bi == 7u ? 0.5 * w : 0.0);
    }
    // (08/11 d) revoleo: w = +-2 -> ese brazo bien arriba y la mano girando en circulo sobre la cabeza (camiseta revoleada)
    float spinL = body.w > 1.5 ? 1.0 : 0.0, spinR = body.w < -1.5 ? 1.0 : 0.0;
    float sideW = clamp(body.w, -1.0, 1.0);
    float gL = saturate(1.0 + sideW), gR = saturate(1.0 - sideW);
    // cuanto falta para tener el brazo arriba (si la animacion ya lo levanta, se suma poco)
    float upL = saturate((hl.y - ul.y) / 0.45), upR = saturate((hr.y - ur.y) / 0.45);
    float wave = 0.22 * sin(body.y);
    float angL = body.x * gL * ((1.0 - upL) * (spinL > 0.0 ? 1.9 : 1.25) + wave * (1.0 - spinL));
    float angR = body.x * gR * ((1.0 - upR) * (spinR > 0.0 ? 1.9 : 1.25) - wave * (1.0 - spinR));
    float3 d = float3(0.0, body.z, 0.0);
    float3 nW = N, tW = T;
    float3x3 M3 = (float3x3)M;
    const float3 upAx = float3(0.0, 1.0, 0.0);
    if (aL > 0.0 && angL != 0.0) { float3 Xr = ul + PesRotAxis(X - ul, fwd, angL); if (spinL > 0.0) Xr = ul + PesRotAxis(Xr - ul, upAx, body.y); d += (Xr - X) * aL; }
    if (aR > 0.0 && angR != 0.0) { float3 Xr = ur + PesRotAxis(X - ur, fwd, -angR); if (spinR > 0.0) Xr = ur + PesRotAxis(Xr - ur, upAx, -body.y); d += (Xr - X) * aR; }
    if (dot(d, d) <= 0.0) return;
    float3x3 Mi = PesInv3(M3);
    P += mul(Mi, d);
    float ang = angL * aL - angR * aR;    // giro medio de este vertice (normales y tangentes)
    if (ang != 0.0) {
        N = mul(Mi, PesRotAxis(mul(M3, N), fwd, ang));
        T = mul(Mi, PesRotAxis(mul(M3, T), fwd, ang));
    }
}

// (08/11 d) SIN CAMISETA: los vertices de la remera (pecho, hombros, brazos y la panza arriba de la cintura) toman la UV
// del cachete del mismo hincha, asi el juego los pinta con la piel de su propia cara (atlas o paleta, lo que use ese hincha).
// La UV del cachete depende de la disposicion del modelo: COLOR.x = 6 -> clase 0, 7 -> clase 1 (tools/crowd/pesos_cara.py).
static const float2 kPesCheekUV[2] = { float2(0.745, 0.392), float2(0.805, 0.293) };
void PesCrowdShirtless(inout uint4 vUV, float3 P, uint4 vIdx, float4 vW, uint cls)
{
    float s = 0.0;
    [unroll] for (int k = 0; k < 4; k++) {
        uint bi = vIdx[k];
        s += (bi == 1u || bi == 3u || bi == 4u || bi == 5u || bi == 7u || bi == 8u || bi == 9u) ? vW[k] : (bi == 0u ? vW[k] * smoothstep(1.08, 1.16, P.y) : 0.0);
    }
    if (s > 0.5) vUV.xy = asuint(kPesCheekUV[cls]);
}

// (08/11 e) REMERA EN LA MANO (tools/crowd/remera_mano.py): grilla de vertices pegada a la mano (peso 255 al hueso 6 / 10),
// escondida por el juego (COLOR.x = 200..203 -> bit 24 de la mascara de partes). COLOR: x = 200 + lado (0 izq, 1 der)
// + 2 * clase, y = s (agarre 0 .. punta 255), z = t (ancho), w = 255 cara de atras. La UV es la del pecho del mismo modelo:
// el PS la pinta con la mascara + paleta / atlas del hincha = los colores del kit de su equipo.
// Se abre solo en los hinchas sin camiseta: cuelga de la mano, o vuela girando cuando la revolea (body.w = +-2).
// Devuelve true si el vertice es de la remera (ya resuelto: visible o escondido).
bool PesCrowdShirtInHand(inout uint4 vPos, inout uint4 vNrm, inout uint4 vTan, inout uint4 vCol, uint persona, float mk)
{
    if (mk < 199.5 || mk > 203.5) return false;
    float4 col = asfloat(vCol);
    uint code = (uint)(mk + 0.5) - 200u;
    uint side = code & 1u, cls = code >> 1;
    if (asfloat(cb5[8].x) == 0.0) return true;
    float4 prm = IniParams.Load(int2(394, 0));
    if (prm.x <= 0.0) return true;
    uint n = 0, st = 0; PesCrowdFaceBuf.GetDimensions(n, st);
    if (2u * persona + 1u >= n) return true;
    float4 body = PesCrowdFaceBuf[2u * persona + 1u];
    if (body.w <= 5.0) return true;                                  // con camiseta puesta: no hay remera en la mano
    body.w -= 10.0;
    bool spin = side == 0u ? body.w > 1.5 : body.w < -1.5;
    bool other = side == 0u ? body.w < -1.5 : body.w > 1.5;
    if (other) return true;                                          // la revolea con la otra mano
    if (!spin && (side == 0u) != (body.w > 0.5)) return true;        // colgando: una sola mano (der salvo w > 0.5)
    uint base = (t0[persona].v[8] & 255u) * 34u;
    uint hb = side == 0u ? 6u : 10u, ub = side == 0u ? 4u : 8u;
    float sx = side == 0u ? 1.0 : -1.0;
    // agarre y hombro en la pose final (animacion del juego + capa de brazos del mod)
    float3 G = float3(0.6027 * sx, 1.0819, 0.2499), U = float3(0.195 * sx, 1.467, 0.033);
    float3 Nd = 0, Td = 0;
    float3 Gb = G, Ub = U;
    if (body.x > 0.0 || body.z != 0.0) {
        PesCrowdBody(Gb, Nd, Td, uint4(hb, 0, 0, 0), float4(1, 0, 0, 0), persona, body);
        PesCrowdBody(Ub, Nd, Td, uint4(ub, 0, 0, 0), float4(1, 0, 0, 0), persona, body);
    }
    float3x4 Mh = PesCrowdMat(base, hb);
    float3 Hf = mul(Mh, float4(Gb, 1.0));
    float3 Uf = mul(PesCrowdMat(base, ub), float4(Ub, 1.0));
    const float3 up = float3(0.0, 1.0, 0.0);
    float s = col.y, t = col.z;
    float3 dir;
    float flut;
    if (spin) {
        float3 rad = Hf - Uf; rad.y = 0.0;
        rad = dot(rad, rad) > 1e-4 ? normalize(rad) : float3(sx, 0.0, 0.0);
        float3 tang = cross(up, rad) * (side == 0u ? 1.0 : -1.0);   // hacia donde gira la mano
        dir = normalize(rad * 0.85 - tang * 0.6 - up * 0.12);        // la tela va atras del giro, casi horizontal
        flut = 0.10;
    } else {
        dir = normalize(float3(0.06 * sx, -1.0, 0.12));               // cuelga
        flut = 0.04;
    }
    float3 across = normalize(cross(dir, abs(dir.y) > 0.9 ? float3(0.0, 0.0, 1.0) : up));
    float3 nrm = normalize(cross(across, dir));
    float L = 0.62;
    float w = lerp(0.05, 0.36, smoothstep(0.0, 0.35, s));            // agarre finito, despues el ancho de la remera
    // tela tipo bandera / banderin del juez de linea: ondas que corren del agarre a la punta (body.y = fase en [0, 2pi),
    // solo multiplos enteros para que no salte al envolver), la amplitud crece hacia la punta libre y una segunda onda
    // mas corta tuerce el borde. La normal se inclina con la pendiente de la onda (brillo que corre por la tela).
    float ph = (float)(persona % 64u) * 0.61;
    float a1 = 3.0 * body.y - 7.0 * s + 0.9 * t + ph, a2 = 5.0 * body.y - 12.0 * s + 2.6 * t + 2.0 * ph;
    float amp = flut * pow(s, 1.3);
    float wave = amp * (sin(a1) + 0.35 * sin(a2));
    float dwds = flut * (1.3 * pow(max(s, 1e-3), 0.3) * (sin(a1) + 0.35 * sin(a2)) + pow(s, 1.3) * (-7.0 * cos(a1) - 4.2 * cos(a2)));
    float3 X = Hf + dir * (L * s) + across * ((t - 0.5) * w) + nrm * wave - up * (spin ? 0.05 * s * s : 0.0);
    nrm = normalize(nrm - dir * (dwds / L));
    float3x3 M3 = (float3x3)Mh;
    float3x3 Mi = PesInv3(M3);
    float3 P = mul(Mi, X - float3(Mh[0].w, Mh[1].w, Mh[2].w));
    float3 nW = col.w > 0.5 ? -nrm : nrm;
    vPos.xyz = asuint(P);
    vNrm.xyz = asuint(normalize(mul(Mi, nW)));
    vTan.xyz = asuint(normalize(mul(Mi, dir)));
    vCol.x = asuint((6.0 + (float)cls) / 255.0);                     // visible (marca de cuerpo, como el resto del hincha)
    return true;
}

void PesCrowdFace(inout uint4 vPos, inout uint4 vNrm, inout uint4 vTan, inout uint4 vCol, uint persona, uint4 vIdx, uint4 vWgt, inout uint4 vUV)
{
    float4 col = asfloat(vCol);
    float mk = col.x * 255.0;
    if (PesCrowdShirtInHand(vPos, vNrm, vTan, vCol, persona, mk)) return;
    if (mk < 5.5 || mk > 7.5) return;
    uint cls = mk > 6.5 ? 1u : 0u;
    float4 prm = IniParams.Load(int2(394, 0));
    if (prm.x <= 0.0) return;
    uint n = 0, st = 0; PesCrowdFaceBuf.GetDimensions(n, st);
    if (2u * persona + 1u >= n) return;
    float4 body = PesCrowdFaceBuf[2u * persona + 1u];
    if (body.w > 5.0) { body.w -= 10.0; PesCrowdShirtless(vUV, asfloat(vPos.xyz), vIdx, asfloat(vWgt), cls); }   // w + 10 = sin camiseta
    float wj = col.y, wl = col.z, wr = col.w;
    if (wj + wl + wr <= 0.0) {
        if (body.x <= 0.0 && body.z == 0.0) return;
        float3 P0 = asfloat(vPos.xyz), N0 = asfloat(vNrm.xyz), T0 = asfloat(vTan.xyz);
        PesCrowdBody(P0, N0, T0, vIdx, asfloat(vWgt), persona, body);
        vPos.xyz = asuint(P0); vNrm.xyz = asuint(N0); vTan.xyz = asuint(T0);
        return;
    }
    float4 g = PesCrowdFaceBuf[2u * persona];
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
    if (body.x > 0.0 || body.z != 0.0) PesCrowdBody(P, N, T, vIdx, asfloat(vWgt), persona, body);
    vPos.xyz = asuint(P); vNrm.xyz = asuint(N); vTan.xyz = asuint(T);
}
