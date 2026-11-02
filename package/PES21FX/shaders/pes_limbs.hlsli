// PES21 FX (26/10, v12.7) - musculos con textura (incluido en el VS de la piel del cuerpo 1c02a92a, despues de pes_hands.hlsli)
// Cuando el musculo trabaja, la piel lo muestra: mas relieve muscular (el relieve grande del normal map, como la definicion x172
// pero solo ahi) y venas en brazos y manos (textures\pes_veins_arm.dds, en t29 del PS).
//   - Brazos y piernas de TorsoBody v12.6: la DLL (manos.cpp) enlaza en t126 los datos de cada malla (textures\pes_limb_*.dds,
//     2 filas): fila 0 = cabecera (t0 tipo 1 brazo / 2 pierna / 3 pierna sin sk_leg; t1 / t2 izquierda / derecha: hueso de
//     arriba, hueso de abajo, 1 / rango de doblez, angulo de reposo; t3..t6 ejes X de reposo; t7 angulo de reposo del brazo con
//     la vertical), fila 1 = por vertice (brazos: biceps, antebrazo, deltoides, venas; piernas: isquios, pantorrilla,
//     cuadriceps, 0). La pose se mide como en musculo.cpp (musculos vivos): codo doblado (~100 grados sobre el reposo = todo),
//     rodilla doblada (1.7 rad), brazo levantado (alejado de la vertical, 1.4 rad).
//   - Manos (datos de pes_hands): las venas del dorso se marcan con el puno apretado (cierre medio de los cuatro dedos).
// Salida: o1.xyz = (definicion, venas, 7) en esos dibujos (el PS de la piel no usa el color del vertice; 7 = marca); en el resto
// del cuerpo o1 queda como el juego. Fila 305: x prendido, y definicion, z venas de los brazos, w venas de las manos.
#ifndef PES_LIMBS_HLSLI
#define PES_LIMBS_HLSLI

float PesLimbFlex(float4 S, float3 Xu, float3 Xl) {
    float3 xu = normalize(mul(PesBoneR(g_vsBone.m_boneMatrices[(int)round(S.x)]), Xu));
    float3 xl = normalize(mul(PesBoneR(g_vsBone.m_boneMatrices[(int)round(S.y)]), Xl));
    return saturate((acos(clamp(dot(xu, xl), -1.0, 1.0)) - S.w) * S.z);
}

// handDone: el dibujo es una mano de TorsoBody (pes_hands); vid / rest: vertice y su posicion de reposo
// (02/11) esfuerzo de ESTE jugador (agitacion de la DLL, musculo.cpp): tabla en t123 con la posicion de cada personaje; se toma
// el mas cercano en el piso (a menos de 1.4 m). -1 = sin tabla (DLL vieja o musculatura viva apagada).
StructuredBuffer<float4> g_effort : register(t123);
float PesLimbEffort() {
    uint ne = 0, st = 0; g_effort.GetDimensions(ne, st);
    if (ne < 2) return -1.0;
    float4 H = g_effort[0];
    if (H.y < 0.5) return -1.0;
    bool skE = g_vsObject.m_useWeightCount.x != 0;
    float3 b0 = float3(g_vsBone.m_boneMatrices[0]._m30, g_vsBone.m_boneMatrices[0]._m31, g_vsBone.m_boneMatrices[0]._m32);
    float3 b0b = float3(g_vsPrevBone.m_boneMatrices[0]._m30, g_vsPrevBone.m_boneMatrices[0]._m31, g_vsPrevBone.m_boneMatrices[0]._m32);
    float3 wp = PesSkinPoint(g_vsObject.m_world, g_vsObject.m_viewWorld, g_vsScene.m_view, b0, b0b, skE);
    int n = min((int)(H.x + 0.5), (int)ne - 1);
    float bd = 1.96, ex = -1.0;
    [loop] for (int i = 0; i < n; i++) {
        float4 E = g_effort[1 + i];
        float2 dd = wp.xz - E.xz;
        float d2 = dot(dd, dd);
        if (d2 < bd) { bd = d2; ex = E.w; }
    }
    return ex;
}
// (02/11 b) muneca fina con la palma hacia adelante (Agustin, captura muneca_fina_20261102_1433). La v1 (cuaterniones duales en
// los huesos de giro) rompio los brazos: dsk_forearm* / dsk_wrist tienen ESCALA. Esta v2 deja el skinning del juego tal cual y
// solo empuja la piel hacia afuera lo que el lineal pierde al mezclar rotaciones distintas: con los cuaterniones de esos huesos
// (sin escala: columnas normalizadas) mezclados con los mismos pesos, |suma| / suma de pesos = cuanto se achica el radio
// (1 sin giro: no cambia nada). Empuje = radio del antebrazo (~3 cm) * (1 / s - 1), con tope de 6 mm. Solo dibujos del brazo
// (datos de pes_limb, tipo 1) y solo los pesos de la mano (paleta 2 / 5) y del giro del antebrazo y la muneca (10..15).
// Devuelve el empuje en metros (a lo largo de la normal ya deformada). x305 musculos con textura, y300 cantidad (0 = apagado).
float PesArmTwistPush(uint vid, uint4 bi, float4 bw, bool cur) {
    float4 L5 = IniParams.Load(int2(305, 0)), P0 = IniParams.Load(int2(300, 0));
    if (L5.x < 0.5 || P0.y <= 0.0) return 0.0;
    uint w = 0, h = 0; g_handData.GetDimensions(w, h);
    if (h != 2 || vid >= w) return 0.0;
    if ((int)round(g_handData.Load(int3(0, 0, 0)).x) != 1) return 0.0;
    float4 Q = 0, q0 = 0; float ws = 0.0; int nb = 0;
    [unroll] for (int k = 0; k < 4; k++) {
        uint b = bi[k]; float wk = bw[k];
        if (wk <= 0.001 || !((b >= 10 && b <= 15) || b == 2 || b == 5)) continue;
        // rotacion sin escala: filas de la matriz del hueso (ejes del hueso, escalados) ortonormalizadas; la norma de la mezcla
        // es la misma con la rotacion o con su transpuesta, asi que da igual la convencion
        float4x3 M = PesBone(cur, b);
        float3 ax = normalize(float3(M._m00, M._m01, M._m02)), ay = float3(M._m10, M._m11, M._m12);
        ay = normalize(ay - dot(ay, ax) * ax);
        float3 az = cross(ax, ay);
        float4 q = PesQuatFromR(float3x3(ax, ay, az)); q /= max(length(q), 1e-6);
        if (nb == 0) q0 = q;
        if (dot(q, q0) < 0) q = -q;
        Q += wk * q; ws += wk; nb++;
    }
    if (nb < 2 || ws < 0.05) return 0.0;
    float sh = clamp(length(Q) / ws, 0.5, 1.0);
    return min(0.03 * (1.0 / sh - 1.0) * saturate(P0.y), 0.006);
}

// (02/11 c) Agustin: "que los brazos se deformen un poco al doblarse por los musculos, como en la vida real". La musculatura viva
// (musculo.cpp, x263) ya engorda parejo los huesos del brazo con el codo; esto agrega la FORMA: el vientre del musculo que trabaja
// se levanta en un bulto localizado (no todo el brazo): biceps con el codo doblado (pico), braquiorradial arriba del antebrazo
// con el codo doblado, flexores del antebrazo con el esfuerzo, triceps con el brazo estirado y esfuerzo, deltoides con el brazo
// levantado. Centros medidos en arm.fmdl de TorsoBody en reposo (brazo izquierdo; el derecho se espeja en x), campana alargada
// a lo largo del brazo. Empuja por la normal ya deformada (no toca el skinning). Tope 7 mm por unidad de x263 contraccion (0 = apagado).
float PesBulge(float3 r, float3 c, float3 ax, float sa, float sp) {
    float3 d = r - c; float al = dot(d, ax); float3 pp = d - al * ax;
    return exp(-(al * al) / (sa * sa) - dot(pp, pp) / (sp * sp));
}
float PesArmBulge(uint vid, float3 rest) {
    float4 L5 = IniParams.Load(int2(305, 0)), M3 = IniParams.Load(int2(263, 0));
    float kc = clamp(M3.x, 0.0, 2.0);
    if (L5.x < 0.5 || kc <= 0.0) return 0.0;
    uint w = 0, h = 0; g_handData.GetDimensions(w, h);
    if (h != 2 || vid >= w) return 0.0;
    float4 H0 = g_handData.Load(int3(0, 0, 0));
    if ((int)round(H0.x) != 1) return 0.0;
    bool left = rest.x > 0.0;
    float4 S = g_handData.Load(int3(left ? 1 : 2, 0, 0));
    float3 Xu = g_handData.Load(int3(left ? 3 : 5, 0, 0)).xyz, Xl = g_handData.Load(int3(left ? 4 : 6, 0, 0)).xyz;
    float flex = PesLimbFlex(S, Xu, Xl);
    float3 xu = normalize(mul(PesBoneR(g_vsBone.m_boneMatrices[(int)round(S.x)]), Xu));
    float4 H7 = g_handData.Load(int3(7, 0, 0));
    float up = saturate((acos(clamp(-xu.y, -1.0, 1.0)) - (left ? H7.x : H7.y)) / 1.4);
    float eff = saturate(PesLimbEffort());
    float4 Wv = g_handData.Load(int3(vid, 1, 0));
    float3 rp = float3(abs(rest.x), rest.y, rest.z);
    const float3 ax = float3(0.7772, -0.6253, 0.0699);
    float fl = smoothstep(0.15, 0.9, flex);
    // (02/11 d, Agustin: "al doblarse un poco mas anchos en los biceps y el antebrazo, y los hombros marcarse al apretar con furia")
    // furia = esfuerzo alto (agitacion del jugador) con el brazo trabajando: el puno no llega al dibujo del brazo (la paleta del
    // brazo no tiene los dedos), asi que se mide por el esfuerzo y la pose
    float fury = smoothstep(0.25, 0.8, eff) * saturate(0.4 + 0.6 * max(fl, up));
    float gB = PesBulge(rp, float3(0.3305, 1.3058, 0.0668), ax, 0.040, 0.032);
    float b = (0.0055 * fl * (0.75 + 0.25 * eff) * gB + 0.0018 * fl) * saturate(2.0 * Wv.x);                                         // biceps: pico + mas ancho
    b += (0.0030 * fl * PesBulge(rp, float3(0.4473, 1.2628, 0.0608), ax, 0.035, 0.025) + 0.0012 * fl) * saturate(2.0 * Wv.y);         // antebrazo: braquiorradial + mas ancho
    b += 0.0030 * max(eff, fury) * saturate(2.0 * Wv.y) * PesBulge(rp, float3(0.4194, 1.2052, 0.0586), ax, 0.040, 0.022);             // flexores (apretar)
    b += 0.0028 * max((1.0 - fl) * eff, 0.6 * fury) * PesBulge(rp, float3(0.3295, 1.3274, -0.0145), ax, 0.038, 0.024);                // triceps
    b += 0.0030 * max(up, fury) * PesBulge(rp, float3(0.3168, 1.3769, 0.0157), ax, 0.032, 0.032);                                     // deltoides (brazo arriba o furia)
    // (02/11 f, Agustin: "al doblarse el antebrazo hacia el biceps el codo deberia notarse mas") el olecranon (la punta del codo)
    // sobresale con la flexion (la piel se estira sobre el hueso) y el pliegue de adentro del codo se hunde un poco
    float kE = clamp(IniParams.Load(int2(166, 0)).w, 0.7, 1.5);   // slider "Codos" (w166): tambien cuanto crece al doblar
    b += 0.0060 * fl * kE * kE * PesBulge(rp, float3(0.3982, 1.2654, -0.0085), ax, 0.020, 0.016);   // punta del codo: crece hacia atras
    b -= 0.0015 * fl * PesBulge(rp, float3(0.3857, 1.2718, 0.0567), ax, 0.014, 0.012);    // pliegue del codo
    return min(b, 0.007) * kc;   // tope 7 mm con x263 = 1 (el doble con 2)
}

// (02/11 e) Agustin: "que la deformacion tenga un poco de inercia". La carne del brazo (vientres del biceps, triceps y antebrazo)
// se queda un poco atras cuando el brazo se mueve rapido: corrimiento contra el movimiento del vertice en este cuadro (posicion
// actual - la del cuadro anterior, menos lo que se movio el hombro: solo el brazo, no el jugador que corre o gira),
// tope 3 mm, mas en el centro del musculo y nada en codo / hombro / muneca. El rebote despues de frenar necesitaria estado entre
// cuadros (DLL); esto es solo el arrastre. x263 contraccion (0 = apagado), y la misma cantidad.
float3 PesArmInertia(uint vid, float3 rest, float3 dpos) {
    float4 L5 = IniParams.Load(int2(305, 0)), M3 = IniParams.Load(int2(263, 0));
    float kc = clamp(M3.x, 0.0, 2.0);
    if (L5.x < 0.5 || kc <= 0.0) return 0.0;
    uint w = 0, h = 0; g_handData.GetDimensions(w, h);
    if (h != 2 || vid >= w) return 0.0;
    if ((int)round(g_handData.Load(int3(0, 0, 0)).x) != 1) return 0.0;
    float4 Wv = g_handData.Load(int3(vid, 1, 0));
    float3 rp = float3(abs(rest.x), rest.y, rest.z);
    const float3 ax = float3(0.7772, -0.6253, 0.0699);
    float m = saturate(2.0 * Wv.x) * PesBulge(rp, float3(0.3305, 1.3058, 0.0668), ax, 0.045, 0.040)
            + 0.8 * PesBulge(rp, float3(0.3295, 1.3274, -0.0145), ax, 0.045, 0.035)
            + 0.6 * saturate(2.0 * Wv.y) * PesBulge(rp, float3(0.4300, 1.2350, 0.0600), ax, 0.050, 0.040);
    m = saturate(m);
    // sin el movimiento del hombro (el jugador que corre / gira): solo el brazo respecto del hombro
    int ib = rest.x > 0.0 ? 0 : 3;   // sk_upperarm_l / _r en la paleta del brazo de TorsoBody
    dpos -= PesBoneT(PesBone(true, ib)) - PesBoneT(PesBone(false, ib));
    float dl = length(dpos);
    if (dl < 1e-5 || m < 0.01) return 0.0;
    return -(dpos / dl) * min(0.06 * dl, 0.003) * m * kc;
}

// (02/11) actCode: lo que hace el brazo para los grupos musculares del PS (doblez del codo, brazo arriba, esfuerzo; 0..7 cada uno),
// empaquetado en un entero 0..511 (igual en todos los vertices de ese brazo: el interpolado no lo cambia)
bool PesLimbOut(bool handDone, uint vid, float3 rest, out float defB, out float veinB, out float actCode) {
    defB = 0.0; veinB = 0.0; actCode = 0.0;
    float4 L5 = IniParams.Load(int2(305, 0));
    if (L5.x < 0.5) return false;
    uint w = 0, h = 0; g_handData.GetDimensions(w, h);
    if (w == 0) return false;
    if (handDone) {   // puno apretado: las venas del dorso
        float cv = 0.25 * (PesFingerCurl(1) + PesFingerCurl(2) + PesFingerCurl(3) + PesFingerCurl(4));
        veinB = smoothstep(0.55, 0.95, cv) * max(L5.w, 0.0);
        return true;
    }
    if (h != 2 || vid >= w) return false;
    float4 H0 = g_handData.Load(int3(0, 0, 0));
    int typ = (int)round(H0.x);
    if (typ < 1 || typ > 3) return false;
    bool left = rest.x > 0.0;
    float4 S = g_handData.Load(int3(left ? 1 : 2, 0, 0));
    float3 Xu = g_handData.Load(int3(left ? 3 : 5, 0, 0)).xyz, Xl = g_handData.Load(int3(left ? 4 : 6, 0, 0)).xyz;
    float flex = PesLimbFlex(S, Xu, Xl);
    float4 Wv = g_handData.Load(int3(vid, 1, 0));
    if (typ == 1) {   // brazo: biceps y antebrazo con el codo, deltoides con el brazo levantado
        float3 xu = normalize(mul(PesBoneR(g_vsBone.m_boneMatrices[(int)round(S.x)]), Xu));
        float4 H7 = g_handData.Load(int3(7, 0, 0));
        float up = saturate((acos(clamp(-xu.y, -1.0, 1.0)) - (left ? H7.x : H7.y)) / 1.4);
        defB = saturate(Wv.x * flex + Wv.y * flex * 0.7 + Wv.z * up);
        // (02/11) la vena sigue el ESFUERZO, no el angulo: al correr el codo va a ~90 grados todo el partido y la vena quedaba
        // siempre marcada. Ahora: agitacion del jugador (velocidad sostenida, se calma en ~14 s) y la pose solo la refuerza.
        // Sin la tabla (DLL vieja), como antes pero a la mitad.
        float pose = saturate(max(flex, 0.6 * up));
        float eff = PesLimbEffort();
        veinB = Wv.w * (eff < 0.0 ? 0.5 * pose : smoothstep(0.2, 0.85, eff) * (0.55 + 0.45 * pose));
        actCode = round(saturate(flex) * 7.0) + 8.0 * round(up * 7.0) + 64.0 * round(saturate(max(eff, 0.0)) * 7.0);
    } else {          // pierna: isquios, pantorrilla y (menos) cuadriceps con la rodilla
        defB = saturate(Wv.x * flex + Wv.y * flex + Wv.z * 0.6 * flex);
    }
    defB *= max(L5.y, 0.0); veinB *= max(L5.z, 0.0);
    return true;
}
#endif
