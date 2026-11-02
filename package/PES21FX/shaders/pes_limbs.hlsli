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

// (02/11) muneca fina con la palma hacia adelante (Agustin, captura muneca_fina_20261102_1433): al girar, el juego reparte el giro
// del antebrazo entre dsk_forearm_t y dsk_forearm (y dsk_wrist / la mano cerca de la muneca); el skinning lineal mezcla dos
// rotaciones distintas y la piel entre ellas se achica (efecto caramelo). En los dibujos del brazo (datos de pes_limb, tipo 1) los
// vertices con peso en esos huesos se deforman por cuaterniones duales (pes_hands.hlsli), que conservan el volumen. Sin giro los
// dos metodos dan lo mismo: solo cambia la pose que gira. Paleta del brazo de TorsoBody: 2 / 5 mano, 10..15 giro del antebrazo y
// muneca. Se prende con x305 (musculos con textura) y y300 (cuaterniones duales de las manos, la misma cantidad).
bool PesArmTwistSkin(uint vid, float3 p, float3 n, float3 t, uint4 bi, float4 bw, out float3 op, out float3 on, out float3 ot, out float3 opp) {
    op = p; on = n; ot = t; opp = p;
    float4 L5 = IniParams.Load(int2(305, 0)), P0 = IniParams.Load(int2(300, 0));
    if (L5.x < 0.5 || P0.x < 0.5 || P0.y <= 0.0) return false;
    uint w = 0, h = 0; g_handData.GetDimensions(w, h);
    if (h != 2 || vid >= w) return false;
    if ((int)round(g_handData.Load(int3(0, 0, 0)).x) != 1) return false;
    float kt = 0.0;
    [unroll] for (int k = 0; k < 4; k++) { uint b = bi[k]; if ((b >= 10 && b <= 15) || b == 2 || b == 5) kt += bw[k]; }
    float kd = saturate(2.0 * kt) * saturate(P0.y);
    if (kd <= 0.0) return false;
    float3 dn, dt;
    PesSkinBoth(true, p, n, t, bi, bw, kd, op, on, ot);
    PesSkinBoth(false, p, n, t, bi, bw, kd, opp, dn, dt);
    return true;
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
