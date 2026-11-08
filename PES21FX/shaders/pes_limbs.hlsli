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
// (08/11) tension de la pierna que reconoce la DLL (musculo.cpp LegMoments: apoyo, empuje, vuelo, frenada, arranque, patada, salto,
// caida) para el personaje mas cercano: fila 1 + 64 + 2 n + lado de t123 (lado 0 = izquierda = +X). Sin la tabla (DLL vieja) -> la del
// shader sola (PesLegAct). Con la tabla: la de la DLL manda y la del shader (velocidad de la rodilla cuadro a cuadro) solo la completa.
float3 PesLegActMix(bool left, float3 shAct) {
    uint ne = 0, st = 0; g_effort.GetDimensions(ne, st);
    if (ne < 2) return shAct;
    float4 H = g_effort[0];
    if (H.y < 0.5 || H.z < 0.5 || ne < 1 + 3 * 64) return shAct;
    bool skE = g_vsObject.m_useWeightCount.x != 0;
    float3 b0 = float3(g_vsBone.m_boneMatrices[0]._m30, g_vsBone.m_boneMatrices[0]._m31, g_vsBone.m_boneMatrices[0]._m32);
    float3 b0b = float3(g_vsPrevBone.m_boneMatrices[0]._m30, g_vsPrevBone.m_boneMatrices[0]._m31, g_vsPrevBone.m_boneMatrices[0]._m32);
    float3 wp = PesSkinPoint(g_vsObject.m_world, g_vsObject.m_viewWorld, g_vsScene.m_view, b0, b0b, skE);
    int n = min((int)(H.x + 0.5), 64);
    float bd = 1.96; int bi = -1;
    [loop] for (int i = 0; i < n; i++) {
        float2 dd = wp.xz - g_effort[1 + i].xz;
        float d2 = dot(dd, dd);
        if (d2 < bd) { bd = d2; bi = i; }
    }
    if (bi < 0) return shAct;
    float3 da = saturate(g_effort[1 + 64 + 2 * bi + (left ? 0 : 1)].xyz);
    return saturate(max(da, 0.5 * shAct));
}
// (08/11 b) tension del brazo de la DLL (musculo.cpp ArmMoments: balanceo, empuje, arriba, lanzamiento, puno, caida, arranque):
// fila 1 + 192 + 2 n + lado de t123 = (biceps, triceps, deltoides, antebrazo). Sin la tabla (DLL vieja) -> -1 (PesArmBulge usa la pose).
float4 PesArmActDll(bool left) {
    uint ne = 0, st = 0; g_effort.GetDimensions(ne, st);
    if (ne < 1 + 5 * 64) return -1.0;
    float4 H = g_effort[0];
    if (H.y < 0.5 || H.w < 0.5) return -1.0;
    bool skE = g_vsObject.m_useWeightCount.x != 0;
    float3 b0 = float3(g_vsBone.m_boneMatrices[0]._m30, g_vsBone.m_boneMatrices[0]._m31, g_vsBone.m_boneMatrices[0]._m32);
    float3 b0b = float3(g_vsPrevBone.m_boneMatrices[0]._m30, g_vsPrevBone.m_boneMatrices[0]._m31, g_vsPrevBone.m_boneMatrices[0]._m32);
    float3 wp = PesSkinPoint(g_vsObject.m_world, g_vsObject.m_viewWorld, g_vsScene.m_view, b0, b0b, skE);
    int n = min((int)(H.x + 0.5), 64);
    float bd = 1.96; int bi = -1;
    [loop] for (int i = 0; i < n; i++) {
        float2 dd = wp.xz - g_effort[1 + i].xz;
        float d2 = dot(dd, dd);
        if (d2 < bd) { bd = d2; bi = i; }
    }
    if (bi < 0) return -1.0;
    return saturate(g_effort[1 + 192 + 2 * bi + (left ? 0 : 1)]);
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
    if (L5.x < 0.5) return 0.0;
    uint w = 0, h = 0; g_handData.GetDimensions(w, h);
    if (h != 2 || vid >= w) return 0.0;
    float4 H0 = g_handData.Load(int3(0, 0, 0));
    if ((int)round(H0.x) != 1) return 0.0;
    // (02/11 g) Agustin: "el slider de codo modifica mucha area y no la punta del codo exactamente". El slider "Codos" (w166) ya no
    // escala el hueso dsk_elbow (musculo.cpp, con x305): aca agranda (o achica) solo la punta, el olecranon, siempre (sin x263)
    float kE = clamp(IniParams.Load(int2(166, 0)).w, 0.7, 1.5);
    float3 rpE = float3(abs(rest.x), rest.y, rest.z);
    const float3 axE = float3(0.7772, -0.6253, 0.0699);
    float bS = 0.0100 * (kE - 1.0) * PesBulge(rpE, float3(0.3982, 1.2654, -0.0085), axE, 0.017, 0.014);
    if (kc <= 0.0) return bS;
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
    // (08/11 b) con la DLL nueva la tension de cada grupo sale de los momentos (PesArmActDll); sin ella, como antes (pose + esfuerzo)
    float4 da = PesArmActDll(left); bool dOk = da.x >= 0.0;
    float kBi = dOk ? saturate(0.5 * fl + 0.7 * da.x) : fl * (0.75 + 0.25 * eff);
    float kFx = dOk ? da.w : max(eff, fury);
    float kTr = dOk ? da.y : max((1.0 - fl) * eff, 0.6 * fury);
    float kDe = dOk ? max(0.6 * up, da.z) : max(up, fury);
    float gB = PesBulge(rp, float3(0.3305, 1.3058, 0.0668), ax, 0.040, 0.032);
    float b = (0.0055 * kBi * gB + 0.0018 * fl) * saturate(2.0 * Wv.x);                                         // biceps: pico + mas ancho
    b += (0.0030 * fl * PesBulge(rp, float3(0.4473, 1.2628, 0.0608), ax, 0.035, 0.025) + 0.0012 * fl) * saturate(2.0 * Wv.y);         // antebrazo: braquiorradial + mas ancho
    b += 0.0030 * kFx * saturate(2.0 * Wv.y) * PesBulge(rp, float3(0.4194, 1.2052, 0.0586), ax, 0.040, 0.022);             // flexores (apretar)
    b += 0.0028 * kTr * PesBulge(rp, float3(0.3295, 1.3274, -0.0145), ax, 0.038, 0.024);                // triceps
    b += 0.0030 * kDe * PesBulge(rp, float3(0.3168, 1.3769, 0.0157), ax, 0.032, 0.032);                                     // deltoides (brazo arriba o furia)
    // (02/11 f, Agustin: "al doblarse el antebrazo hacia el biceps el codo deberia notarse mas") el olecranon (la punta del codo)
    // sobresale con la flexion (la piel se estira sobre el hueso) y el pliegue de adentro del codo se hunde un poco
    // slider "Codos" (kE, arriba): tambien cuanto crece al doblar
    b += 0.0084 * fl * kE * kE * PesBulge(rp, float3(0.3982, 1.2654, -0.0085), ax, 0.020, 0.016);   // punta del codo: crece hacia atras (02/11 g: x1.4, Agustin)
    b -= 0.0015 * fl * PesBulge(rp, float3(0.3857, 1.2718, 0.0567), ax, 0.014, 0.012);    // pliegue del codo
    return min(b, 0.0095) * kc + bS;   // tope 9.5 mm con x263 = 1 (el doble con 2; era 7, sino el codo x1.4 no entraba)
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

// (08/11, Agustin: "sigue mejorando el aspecto visual de el cuerpo ... con mas detalles") VOLUMEN MUSCULAR DE LA PIERNA. El brazo
// ya se infla de verdad al trabajar (PesArmBulge); la pierna solo cambiaba el sombreado. Con la rodilla doblada y el esfuerzo del
// jugador (misma medida que el brazo: PesLimbFlex con los huesos de t126 y la agitacion de musculo.cpp) se levantan el cuadriceps
// (vasto interno sobre la rodilla, vasto externo y recto anterior), los isquios, el gemelo y el tendon de la rodilla, y se hunde
// un poco el hueco de atras de la rodilla. Pesos por vertice de t126 fila 1 (x isquios, y pantorrilla, z cuadriceps), asi que
// nada se mueve donde el hueso manda (rotula, tobillo, costuras con el short y el botin). Eje de la pierna izquierda en reposo
// medido en thigh_short.fmdl (muslo arriba, rodilla, canilla); la derecha es el espejo. x263 contraccion (0 = apagado), tope 7 mm.
static const float3 PLB_A = float3(0.110, 0.850, 0.062);    // muslo arriba
static const float3 PLB_K = float3(0.1525, 0.544, 0.056);   // rodilla
static const float3 PLB_C = float3(0.178, 0.330, 0.040);    // canilla
// (s, th) de la pierna: s + hacia el muslo / - hacia la canilla (m desde la rodilla), th 0 adelante, +90 afuera
void PesLegST(float3 p, out float s, out float th) {
    float3 au = normalize(PLB_A - PLB_K), ad = normalize(PLB_K - PLB_C);
    float3 q = p - PLB_K;
    float3 ax = dot(q, au) > 0.0 ? au : ad;
    s = dot(q, ax);
    float3 r = q - s * ax;
    float3 fw = normalize(float3(0, 0, 1) - ax.z * ax), lt = normalize(cross(ax, fw));
    if (lt.x < 0.0) lt = -lt;
    th = degrees(atan2(dot(r, lt), dot(r, fw)));
}
float PesLegGauss(float s, float th, float s0, float th0, float ss_, float sth) {
    float dth = fmod(th - th0 + 540.0, 360.0) - 180.0;
    float a = (s - s0) / ss_, b = dth / sth;
    return exp(-a * a - b * b);
}
// (08/11, Agustin: "mejora el metodo de plasmar los musculos tensados en las piernas al correr o realizar acciones") TENSION DE CADA
// GRUPO de la pierna 0..1: x cuadriceps, y isquios, z gemelo. Antes solo pesaba el angulo de la rodilla y el esfuerzo sostenido; ahora
// tambien la VELOCIDAD de la rodilla (cuadro actual contra el anterior, g_vsPrevBone): en la carrera el cuadriceps trabaja al apoyar y
// estirar la rodilla con carga, los isquios frenan la pierna al final del vuelo (rodilla que se estira rapido estando doblada) y la
// doblan, y el gemelo empuja al despegar (rodilla que se estira con esfuerzo). Parado, sin esfuerzo, todo queda en su tono base.
float PesLimbFlexB(bool cur, float4 S, float3 Xu, float3 Xl) {
    float3 xu = normalize(mul(PesBoneR(PesBone(cur, (int)round(S.x))), Xu));
    float3 xl = normalize(mul(PesBoneR(PesBone(cur, (int)round(S.y))), Xl));
    return saturate((acos(clamp(dot(xu, xl), -1.0, 1.0)) - S.w) * S.z);
}
float3 PesLegAct(float flex, float flexPrev, float eff) {
    float sp = (flex - flexPrev) / 0.08;              // + doblando, - estirando (~0.08 por cuadro en la carrera)
    float ext = saturate(-sp), bend = saturate(sp);
    float fl = smoothstep(0.12, 0.9, flex);
    float stz = (flex - 0.22) / 0.16;
    float stance = exp(-stz * stz);   // apoyo: rodilla un poco doblada (15..40 grados)
    float qa = saturate(0.20 * eff + eff * (0.45 * stance + 0.45 * ext * (0.4 + 0.6 * fl)) + 0.15 * fl);
    float ha = saturate(0.15 * eff + eff * (0.55 * ext * fl + 0.35 * bend) + 0.35 * fl * (0.4 + 0.6 * eff));
    float ca = saturate(0.30 * eff + eff * 0.60 * ext * (1.0 - 0.5 * fl) + 0.20 * eff * stance);
    return float3(qa, ha, ca);
}
float PesLegBulge(uint vid, float3 rest) {
    float4 L5 = IniParams.Load(int2(305, 0)), M3 = IniParams.Load(int2(263, 0));
    float kc = clamp(M3.x, 0.0, 2.0);
    if (L5.x < 0.5 || kc <= 0.0) return 0.0;
    uint w = 0, h = 0; g_handData.GetDimensions(w, h);
    if (h != 2 || vid >= w) return 0.0;
    float4 H0 = g_handData.Load(int3(0, 0, 0));
    int typ = (int)round(H0.x);
    if (typ < 2 || typ > 3) return 0.0;   // 2 / 3 = pierna (1 = brazo: PesArmBulge)
    bool left = rest.x > 0.0;
    float4 S = g_handData.Load(int3(left ? 1 : 2, 0, 0));
    float3 Xu = g_handData.Load(int3(left ? 3 : 5, 0, 0)).xyz, Xl = g_handData.Load(int3(left ? 4 : 6, 0, 0)).xyz;
    float flex = PesLimbFlex(S, Xu, Xl);          // rodilla doblada 0..1
    float eff = saturate(PesLimbEffort());        // esfuerzo del jugador (-1 sin tabla -> 0)
    float4 Wv = g_handData.Load(int3(vid, 1, 0)); // x isquios, y pantorrilla, z cuadriceps
    float s, th; PesLegST(float3(abs(rest.x), rest.y, rest.z), s, th);
    float fl = smoothstep(0.12, 0.9, flex);
    float3 act = PesLegActMix(left, PesLegAct(flex, PesLimbFlexB(false, S, Xu, Xl), eff));   // (08/11) tension por grupo (momentos de la DLL + velocidad de la rodilla)
    float qa = act.x;
    float b = 0.0;
    b += (0.0042 * qa + 0.0010 * fl) * saturate(2.0 * Wv.z) * PesLegGauss(s, th, 0.075, -55.0, 0.030, 26.0);   // vasto interno (la gota)
    b += (0.0030 * qa + 0.0008 * fl) * saturate(2.0 * Wv.z) * PesLegGauss(s, th, 0.160, 70.0, 0.055, 30.0);    // vasto externo
    b += 0.0026 * qa * saturate(2.0 * Wv.z) * PesLegGauss(s, th, 0.200, 0.0, 0.065, 20.0);                     // recto anterior
    b += (0.0030 * act.y + 0.0012 * fl) * saturate(2.0 * Wv.x) * PesLegGauss(s, th, 0.150, 180.0, 0.060, 40.0);   // isquios
    b += 0.0040 * act.z * saturate(2.0 * Wv.y) * PesLegGauss(s, th, -0.125, -150.0, 0.045, 26.0);   // gemelo interno
    b += 0.0028 * act.z * saturate(2.0 * Wv.y) * PesLegGauss(s, th, -0.100, 148.0, 0.040, 22.0);               // gemelo externo
    b += 0.0016 * fl * PesLegGauss(s, th, -0.030, 0.0, 0.016, 12.0);                                           // tendon de la rodilla al doblar
    b -= 0.0020 * fl * PesLegGauss(s, th, 0.010, 180.0, 0.022, 30.0);                                          // hueco de atras: se cierra
    return clamp(b, -0.0030, 0.0070) * kc;
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
        actCode = 520.0 + round(saturate(cv) * 7.0);   // (02/11 g) mano: 520 + cierre medio de los 4 dedos (0..7); el brazo usa 0..511
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
        // (08/11) relieve 3D de la pierna en el PS / DS (pes_limb_relief.hlsli): 528 + rodilla (0..7) + 8 x esfuerzo (0..7) en z;
        // la tension de cada grupo (PesLegAct) en y = 2 + (cuadriceps + 8 isquios + 64 gemelo) / 1024 (0..7 cada uno; el PS no le
        // aplica venas a la pierna). La definicion general del musculo (x) sigue a la tension de su grupo.
        float effL = saturate(PesLimbEffort());
        float3 act = PesLegActMix(left, PesLegAct(flex, PesLimbFlexB(false, S, Xu, Xl), effL));
        defB = saturate(Wv.x * max(act.y, 0.6 * flex) + Wv.y * max(act.z, 0.6 * flex) + Wv.z * max(act.x, 0.4 * flex)) * max(L5.y, 0.0);
        actCode = 528.0 + round(saturate(flex) * 7.0) + 8.0 * round(effL * 7.0);
        veinB = 2.0 + (round(act.x * 7.0) + 8.0 * round(act.y * 7.0) + 64.0 * round(act.z * 7.0)) / 1024.0;
        return true;
    }
    defB *= max(L5.y, 0.0); veinB *= max(L5.z, 0.0);
    return true;
}
#endif
