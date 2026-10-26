// PES21 FX (25/10) - manos de TorsoBody: skinning por cuaterniones duales + formas correctivas por pose.
// Se incluye en los VS de la piel animada (1c02a92a: pasada principal; c6ef0c75: sombras) DESPUES de declarar g_vsBone /
// g_vsPrevBone e IniParams. Solo actua en los dibujos de la mano: la DLL (manos.cpp) enlaza en t126 los datos por vertice de la
// mano que se esta dibujando (la reconoce por la cantidad de indices); en cualquier otro dibujo t126 queda vacio y la mano
// sigue el camino del juego (skinning lineal).
//
// t126 (R32G32B32A32_FLOAT, ancho = vertices de la malla, 8 filas; lo genera v117_handdata.py desde el fmdl de la mano):
//   fila 0: x 1 = vertice de la mano | y cuanto skinning por cuaterniones (0 cerca de la muneca: esos huesos pueden tener escala)
//           z dedo que lo maneja (0 pulgar .. 4 menique) | w dedo vecino con el que se apoya en el puno (-1 ninguno)
//   fila 1 / 2: correctiva del puno (nudillos) en reposo: desplazamiento / cambio de normal, con el cierre del dedo z
//   fila 3 / 4: contacto entre dedos: desplazamiento / normal, con el cierre minimo de z y w
//   fila 5: venas del dorso: xyz cambio de normal, w relieve (m), con el cierre medio de los 4 dedos
//   fila 6, columnas 0..4: por dedo: indices de paleta (metacarpo, falange proximal, falange media) y suma de angulos del puno
// Parametros: x300 prendido | y300 cuaterniones duales (0..1) | z300 nudillos del puno | w300 dedos que se apoyan
//             x301 venas al cerrar | y301 ver: 0 normal, 1 color por cierre (diagnostico, solo pasada principal)
//             (26/10: eran las filas 200 / 201, que la tabla de tonos de piel por jugador pisa en cada cuadro)

Texture2D<float4> g_handData : register(t126);

float3x3 PesBoneR(float4x3 M) { return float3x3(M._m00, M._m10, M._m20, M._m01, M._m11, M._m21, M._m02, M._m12, M._m22); }
float3 PesBoneT(float4x3 M) { return float3(M._m30, M._m31, M._m32); }

float PesJointAngle(int p, int c) {
    float3x3 Rp = PesBoneR(g_vsBone.m_boneMatrices[p]);
    float3x3 Rc = PesBoneR(g_vsBone.m_boneMatrices[c]);
    float3x3 Rr = mul(transpose(Rp), Rc);
    return acos(clamp((Rr[0][0] + Rr[1][1] + Rr[2][2] - 1.0) * 0.5, -1.0, 1.0));
}
float PesFingerCurl(int f) {
    if (f < 0) return 0;
    float4 c = g_handData.Load(int3(f, 6, 0));
    if (c.w < 1e-3) return 0;
    float a = PesJointAngle((int)c.x, (int)c.y) + PesJointAngle((int)c.y, (int)c.z);
    return saturate(a / c.w);
}

float4 PesQuatFromR(float3x3 R) {
    float t = R[0][0] + R[1][1] + R[2][2];
    float4 q;
    if (t > 0) { float s = sqrt(t + 1.0) * 2; q = float4((R[2][1] - R[1][2]) / s, (R[0][2] - R[2][0]) / s, (R[1][0] - R[0][1]) / s, 0.25 * s); }
    else if (R[0][0] > R[1][1] && R[0][0] > R[2][2]) { float s = sqrt(1.0 + R[0][0] - R[1][1] - R[2][2]) * 2; q = float4(0.25 * s, (R[0][1] + R[1][0]) / s, (R[0][2] + R[2][0]) / s, (R[2][1] - R[1][2]) / s); }
    else if (R[1][1] > R[2][2]) { float s = sqrt(1.0 + R[1][1] - R[0][0] - R[2][2]) * 2; q = float4((R[0][1] + R[1][0]) / s, 0.25 * s, (R[1][2] + R[2][1]) / s, (R[0][2] - R[2][0]) / s); }
    else { float s = sqrt(1.0 + R[2][2] - R[0][0] - R[1][1]) * 2; q = float4((R[0][2] + R[2][0]) / s, (R[1][2] + R[2][1]) / s, 0.25 * s, (R[1][0] - R[0][1]) / s); }
    return q;
}
float4 PesQMul(float4 a, float4 b) {
    return float4(a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz), a.w * b.w - dot(a.xyz, b.xyz));
}
float3 PesQRot(float4 q, float3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }

// skinning de un punto con una paleta (cur = 1: g_vsBone, 0: g_vsPrevBone): lineal y por cuaterniones duales, mezclados con kd
void PesSkinBoth(bool cur, float3 p, float3 n, float3 t, uint4 bi, float4 bw, float kd, out float3 op, out float3 on, out float3 ot) {
    float3x3 M = 0; float3 T = 0; float4 Q = 0, Dq = 0, q0 = 0;
    [unroll] for (int k = 0; k < 4; k++) {
        float w = bw[k];
        float4x3 B = cur ? g_vsBone.m_boneMatrices[bi[k]] : g_vsPrevBone.m_boneMatrices[bi[k]];
        float3x3 R = PesBoneR(B); float3 tr = PesBoneT(B);
        M += w * R; T += w * tr;
        float4 q = PesQuatFromR(R); float4 d = 0.5 * PesQMul(float4(tr, 0), q);
        if (k == 0) q0 = q;
        if (dot(q, q0) < 0) { q = -q; d = -d; }
        Q += w * q; Dq += w * d;
    }
    float3 pl = mul(M, p) + T, nl = mul(M, n), tl = mul(M, t);
    float ln = max(length(Q), 1e-6); Q /= ln; Dq /= ln;
    float3 trq = 2.0 * (Q.w * Dq.xyz - Dq.w * Q.xyz + cross(Q.xyz, Dq.xyz));
    float3 pd = PesQRot(Q, p) + trq, nd = PesQRot(Q, n), td = PesQRot(Q, t);
    op = lerp(pl, pd, kd); on = lerp(nl, nd, kd); ot = lerp(tl, td, kd);
}

// true = era un vertice de la mano y ya quedo deformado (posicion / normal / tangente actuales y posicion del cuadro anterior)
bool PesHandSkin(uint vid, float3 p, float3 n, float3 t, uint4 bi, float4 bw, out float3 op, out float3 on, out float3 ot, out float3 opp, out float dbg) {
    op = p; on = n; ot = t; opp = p; dbg = 0;
    float4 P0 = IniParams.Load(int2(300, 0));
    if (P0.x < 0.5) return false;
    uint w, h; g_handData.GetDimensions(w, h);
    if (h < 8 || vid >= w) return false;
    float4 h0 = g_handData.Load(int3(vid, 0, 0));
    if (h0.x < 0.5) return false;
    float4 P1 = IniParams.Load(int2(301, 0));
    int fa = (int)round(h0.z), fb = (int)round(h0.w);
    float ca = PesFingerCurl(fa), cb = PesFingerCurl(fb);
    float cv = 0;
    if (P1.x > 0.001) cv = 0.25 * (PesFingerCurl(1) + PesFingerCurl(2) + PesFingerCurl(3) + PesFingerCurl(4));
    float kf = P0.z * ca, kc = P0.w * min(ca, cb), kv = P1.x * cv;
    float4 r1 = g_handData.Load(int3(vid, 1, 0)), r2 = g_handData.Load(int3(vid, 2, 0));
    float4 r3 = g_handData.Load(int3(vid, 3, 0)), r4 = g_handData.Load(int3(vid, 4, 0)), r5 = g_handData.Load(int3(vid, 5, 0));
    float3 pr = p + kf * r1.xyz + kc * r3.xyz + kv * r5.w * n;
    float3 nr = normalize(n + kf * r2.xyz + kc * r4.xyz + kv * r5.xyz);
    float kd = saturate(P0.y) * h0.y;
    PesSkinBoth(true, pr, nr, t, bi, bw, kd, op, on, ot);
    float3 dn, dt; PesSkinBoth(false, pr, nr, t, bi, bw, kd, opp, dn, dt);
    dbg = max(ca, cb);
    return true;
}
