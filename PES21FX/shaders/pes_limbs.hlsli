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
bool PesLimbOut(bool handDone, uint vid, float3 rest, out float defB, out float veinB) {
    defB = 0.0; veinB = 0.0;
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
        veinB = Wv.w * saturate(max(flex, 0.6 * up));
    } else {          // pierna: isquios, pantorrilla y (menos) cuadriceps con la rodilla
        defB = saturate(Wv.x * flex + Wv.y * flex + Wv.z * 0.6 * flex);
    }
    defB *= max(L5.y, 0.0); veinB *= max(L5.z, 0.0);
    return true;
}
#endif
