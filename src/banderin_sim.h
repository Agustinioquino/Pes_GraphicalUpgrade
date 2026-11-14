// PES21 FX - banderin del corner: tela propia (14/11/2026, hilo del viento, pedido de Agustin: "al bajar el viento el movimiento
// tiene que ser poco y caerse la tela", "deberia tener arrugas con normales ademas, y no usar mucho rendimiento").
//
// El juego NO simula el banderin: los 4 de las esquinas (cf_common_2019E_vatB.fmdl y los cf_<estadio>_vat00, malla "cloth", grilla de
// 13 x 9) leen la posicion de cada punto de dos texturas grabadas (VAT, 256 cuadros a 30 por segundo) en los VS ca62006b9e91c070 /
// ad793f5286a2e699. Siempre el mismo flameo, con la tela estirada, haya o no viento.
//
// Aca va una tela de verdad, chica (13 x 9 = 117 puntos, los mismos de la malla): la columna del palo esta CLAVADA (no se mueve nunca:
// la tela no se puede separar del palo), la gravedad la tira hacia abajo y el viento unico (fila 494) la empuja y la hace flamear.
// Sin cabeceras de Windows: se compila tambien suelto para probarlo (tools/sim/banderin_test.cpp).
#pragma once
#include <cmath>
#include <algorithm>

namespace fx {
struct FlagSim {
    static constexpr int NU = 13, NV = 9, N = NU * NV;   // u: 0 = palo .. 12 = punta libre; v: 0 = abajo .. 8 = arriba
    // medidas de la malla del juego (espacio del modelo, m; leidas del .fmdl): la tela en reposo es un rectangulo en el plano z = 0 que
    // sale del palo hacia -x. Las columnas no estan a la misma distancia (0.0505 cerca del palo, 0.047 en la punta)
    static constexpr float kPoleX = 0.0035f, kPoleR = 0.0235f;   // eje y radio del palo (caja de la malla "pole": x -0.018..0.025, z +-0.015)
    static constexpr float kH = 1.0f / 120.0f;                    // paso fijo
    static float ColX(int u) { static const float x[NU] = { -0.0182f, -0.0687f, -0.1193f, -0.1698f, -0.2204f, -0.2709f, -0.3214f, -0.3720f, -0.4229f, -0.4701f, -0.5174f, -0.5647f, -0.6119f }; return x[u]; }
    static float RowY(int v) { static const float y[NV] = { 1.2275f, 1.2703f, 1.3131f, 1.3559f, 1.3987f, 1.4415f, 1.4843f, 1.5272f, 1.5699f }; return y[v]; }
    struct V3 { float x, y, z; };
    V3 p[N], q[N];        // posicion ahora / en el paso anterior
    V3 nrm[N];            // normal de cada punto (para el arrastre del aire)
    // salida para el shader (con las arrugas finas ya puestas): posicion, a donde fue a parar el eje x de la malla y el eje z (la normal)
    V3 oP[N], oX[N], oZ[N];
    float crease[N];      // 0..1 cuanto se junta la tela en ese punto
    float acc = 0.0f;     // tiempo sin simular
    double clock = 0.0;   // reloj propio (s)
    bool ready = false;

    // parametros (los pone la DLL desde el menu)
    float weight = 1.0f;      // peso de la tela (1 = tela liviana de banderin)
    float flutter = 1.0f;     // cuanto flamea
    float wrinkle = 1.0f;     // arrugas finas

    static int I(int u, int v) { return v * NU + u; }
    static int MeshIdx(int u, int v) { return v * NU + (NU - 1 - u); }   // BLENDINDICES.x de la malla: fila * 13 + columna, columna 12 = palo
    static V3 Rest(int u, int v) { return { ColX(u), RowY(v), 0.0f }; }
    static V3 add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    static V3 sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    static V3 mul(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
    static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    static float len(V3 a) { return std::sqrt(dot(a, a)); }
    static V3 norm(V3 a, V3 fb) { float l = len(a); return l > 1e-7f ? mul(a, 1.0f / l) : fb; }

    void Reset() {
        for (int v = 0; v < NV; v++) for (int u = 0; u < NU; u++) { p[I(u, v)] = q[I(u, v)] = Rest(u, v); crease[I(u, v)] = 0.0f; }
        acc = 0.0f; ready = true; Frames(); Output(0.0f, 1.0f);
    }

    // normal de cada punto. En reposo du = (-1,0,0), dv = (0,1,0): cross(dv, du) = (0,0,1) = la normal de la malla del juego
    static void Normals(const V3* P, V3* Nn, V3* Tx) {
        for (int v = 0; v < NV; v++) for (int u = 0; u < NU; u++) {
            const int i = I(u, v);
            const V3 du = sub(P[I((std::min)(u + 1, NU - 1), v)], P[I((std::max)(u - 1, 0), v)]);
            const V3 dv = sub(P[I(u, (std::min)(v + 1, NV - 1))], P[I(u, (std::max)(v - 1, 0))]);
            const V3 n = norm(cross(dv, du), V3{ 0, 0, 1 });
            Nn[i] = n;
            // eje x de la malla (+x = hacia el palo = -du), perpendicular a la normal aunque la tela se cizalle
            if (Tx) Tx[i] = norm(sub(mul(du, -1.0f), mul(n, -dot(du, n))), V3{ 1, 0, 0 });
        }
    }
    void Frames() {
        Normals(p, nrm, nullptr);
        for (int v = 0; v < NV; v++) for (int u = 0; u < NU; u++) {
            const int i = I(u, v), u0 = (std::max)(u - 1, 0), u1 = (std::min)(u + 1, NU - 1), v0 = (std::max)(v - 1, 0), v1 = (std::min)(v + 1, NV - 1);
            // arruga: cuanto se acercaron los vecinos respecto del reposo (tela juntada = pliegues)
            const float cu = 1.0f - len(sub(p[I(u1, v)], p[I(u0, v)])) / (ColX(u0) - ColX(u1));
            const float cv = 1.0f - len(sub(p[I(u, v1)], p[I(u, v0)])) / (RowY(v1) - RowY(v0));
            const float c = std::clamp((std::max)(cu, cv) * 4.0f, 0.0f, 1.0f);
            crease[i] += (c - crease[i]) * 0.25f;
        }
    }

    // salida: posiciones con las arrugas finas + marco para la luz. U = viento (m/s): con la tela tensa las arrugas casi desaparecen.
    // alpha = cuanto del paso en curso ya paso (0..1): se dibuja entre el paso anterior y el actual, asi el movimiento sale parejo con
    // cualquier cantidad de cuadros por segundo (a 144 hay cuadros con un paso y cuadros sin ninguno)
    void Output(float U, float alpha) {
        // pliegues que salen en abanico desde la esquina de arriba del palo (de ahi cuelga la tela): crestas a lo largo de los radios.
        // La amplitud crece con la distancia a la esquina (cerca del palo no hay nada: la costura no se mueve) y con lo floja que este
        const float slack = std::clamp(1.15f - U / 5.5f, 0.18f, 1.0f), t = (float)clock - (1.0f - alpha) * kH;
        const float A = 0.011f * wrinkle;
        V3 r[N], rn[N];
        for (int i = 0; i < N; i++) r[i] = add(q[i], mul(sub(p[i], q[i]), alpha));
        Normals(r, rn, nullptr);
        for (int v = 0; v < NV; v++) for (int u = 0; u < NU; u++) {
            const int i = I(u, v);
            float w = 0.0f;
            if (u > 0 && A > 0.0f) {
                const float dx = ColX(0) - ColX(u), dy = RowY(NV - 1) - RowY(v), rr = std::sqrt(dx * dx + dy * dy), th = std::atan2(dy, dx);
                const float fade = std::clamp(dx / 0.12f, 0.0f, 1.0f) * std::clamp(rr / 0.30f, 0.0f, 1.0f);
                const float amp = A * fade * (0.35f * slack + 0.9f * crease[i]);
                w = amp * (std::sin(9.0f * th + 0.5f * std::sin(0.6f * t) + 6.0f * rr) + 0.55f * std::sin(15.0f * th - 11.0f * rr + 1.9f + 0.4f * std::sin(0.43f * t + 1.0f)));
            }
            oP[i] = add(r[i], mul(rn[i], w));
        }
        Normals(oP, oZ, oX);
    }

    void Dist(int a, int b, float rest, float k, bool pinA) {
        V3 d = sub(p[b], p[a]); const float l = len(d); if (l < 1e-7f) return;
        const float e = (l - rest) / l * k;
        if (pinA) { p[b] = sub(p[b], mul(d, e)); }
        else { p[a] = add(p[a], mul(d, e * 0.5f)); p[b] = sub(p[b], mul(d, e * 0.5f)); }
    }

    // un paso fijo. w = viento en la tela (m/s, ejes del modelo = ejes del mundo: los 4 banderines no estan girados), turb 0..1
    void Sub(V3 w, float turb) {
        const float h = kH, U = len(w);
        const V3 wd = U > 1e-4f ? mul(w, 1.0f / U) : V3{ -1, 0, 0 };
        const V3 side = norm(cross(V3{ 0, 1, 0 }, wd), V3{ 0, 0, 1 });   // de costado al viento
        // arrastre: empuje de frente (cn) y roce a lo largo de la tela (ct): el roce es lo que la sostiene estirada contra la gravedad
        const float cn = 1.6f / weight, ct = 0.6f / weight, g = 9.81f;   // medido (tools/sim/banderin_test.cpp): 1 m/s cuelga, 2 m/s a media asta (56 grados de caida), 3 m/s 18 grados, 5 m/s estirada
        const float Uc = (std::min)(U, 14.0f);   // tope de la fuerza (estabilidad)
        const float ft = flutter * (0.35f + 0.65f * turb);
        clock += h;
        const float t = (float)clock;
        Normals(p, nrm, nullptr);
        for (int v = 0; v < NV; v++) for (int u = 1; u < NU; u++) {
            const int i = I(u, v);
            const V3 vel = mul(sub(p[i], q[i]), 1.0f / h);
            // remolinos que viajan por la tela con el aire (los que hacen flamear una bandera): ondas de costado y algo verticales
            const float s = ColX(0) - ColX(u), row = (float)v / (NV - 1);
            const float c = 0.7f * Uc;   // velocidad de la onda
            const float a1 = std::sin(10.5f * (s - c * t) + 1.3f * row), a2 = std::sin(23.0f * (s - c * t) + 2.9f * row + 1.7f), a3 = std::sin(5.2f * (s - 0.55f * c * t) - 0.8f * row + 0.6f);
            const float lat = Uc * ft * (0.42f * a1 + 0.2f * a2 + 0.3f * a3);
            const float ver = Uc * ft * 0.12f * std::sin(7.1f * (s - c * t) + 4.0f * row + 2.2f);
            // aire casi quieto: un vaiven minimo para que no quede de piedra
            const float calm = 0.05f * std::sin(0.7f * t + 2.0f * row) + 0.03f * std::sin(1.9f * t + 5.0f * s);
            const V3 air = add(mul(wd, Uc), add(mul(side, lat + calm), V3{ 0, ver + w.y, 0 }));
            const V3 vr = sub(air, vel);
            const float sp = len(vr), vn = dot(vr, nrm[i]);
            const V3 vt = sub(vr, mul(nrm[i], vn));
            const float edge = 1.0f + 0.25f * (float)u / (NU - 1);   // la punta libre recibe algo mas (borde de fuga)
            V3 a = add(mul(nrm[i], cn * sp * vn * edge), mul(vt, ct * sp));
            a.y -= g;
            const float damp = 1.0f - 1.4f * h;   // aire quieto: el pendulo se frena en un par de segundos
            const V3 np = add(add(p[i], mul(sub(p[i], q[i]), damp)), mul(a, h * h));
            q[i] = p[i]; p[i] = np;
        }
        // costuras: la tela no se estira (pasadas de distancia); la columna u = 0 no se toca nunca
        const float rv = RowY(1) - RowY(0);
        const float kb = 0.06f, ks = 0.55f;
        for (int it = 0; it < 5; it++) {
            for (int v = 0; v < NV; v++) for (int u = 0; u + 1 < NU; u++) Dist(I(u, v), I(u + 1, v), ColX(u) - ColX(u + 1), 1.0f, u == 0);
            for (int v = 0; v + 1 < NV; v++) for (int u = 1; u < NU; u++) Dist(I(u, v), I(u, v + 1), RowY(v + 1) - RowY(v), 1.0f, false);
            for (int v = 0; v + 1 < NV; v++) for (int u = 0; u + 1 < NU; u++) {
                const float ru = ColX(u) - ColX(u + 1), dv = RowY(v + 1) - RowY(v), rd = std::sqrt(ru * ru + dv * dv);
                Dist(I(u, v), I(u + 1, v + 1), rd, ks, u == 0);
                if (u == 0) { V3 d = sub(p[I(0, v + 1)], p[I(1, v)]); const float l = len(d); if (l > 1e-7f) p[I(1, v)] = add(p[I(1, v)], mul(d, (l - rd) / l * ks)); }
                else Dist(I(u + 1, v), I(u, v + 1), rd, ks, false);
            }
            // cuerpo: vecinos de a dos (resiste a doblarse en angulo)
            for (int v = 0; v < NV; v++) for (int u = 0; u + 2 < NU; u++) Dist(I(u, v), I(u + 2, v), ColX(u) - ColX(u + 2), kb, u == 0);
            for (int v = 0; v + 2 < NV; v++) for (int u = 1; u < NU; u++) Dist(I(u, v), I(u, v + 2), 2 * rv, kb, false);
            // riendas al palo (long range attachments): ningun punto puede quedar mas lejos de su costura que el largo de tela que
            // los separa. Es lo que impide que la tela se estire con viento fuerte o al caer, con pocas pasadas (barato)
            for (int v = 0; v < NV; v++) for (int u = 1; u < NU; u++) {
                V3& x = p[I(u, v)];
                for (int k = 0; k < 3; k++) {
                    const int pv = k == 0 ? v : k == 1 ? 0 : NV - 1; if (k && pv == v) continue;
                    const float du_ = ColX(0) - ColX(u), dv_ = RowY(v) - RowY(pv), maxd = std::sqrt(du_ * du_ + dv_ * dv_) * 1.01f;
                    const V3 a = Rest(0, pv); const V3 d = sub(x, a); const float l = len(d);
                    if (l > maxd) x = add(a, mul(d, maxd / l));
                }
            }
            // el palo: la tela no lo atraviesa
            for (int v = 0; v < NV; v++) for (int u = 1; u < NU; u++) {
                V3& x = p[I(u, v)]; const float dx = x.x - kPoleX, dz = x.z, d2 = dx * dx + dz * dz, r = kPoleR + 0.004f;
                if (d2 < r * r) { const float d = std::sqrt(d2); if (d > 1e-5f) { x.x = kPoleX + dx / d * r; x.z = dz / d * r; } else x.x = kPoleX - r; }
            }
        }
        // seguro: si algo se rompio (NaN o un punto lejos), se vuelve al reposo
        for (int i = 0; i < N; i++) { const V3 d = sub(p[i], Rest(i % NU, i / NU)); if (!(dot(d, d) < 4.0f)) { Reset(); return; } }
    }

    // avanza dt segundos de juego (0 = pausa: la tela queda como esta) y deja la salida lista. Devuelve los pasos hechos
    int Step(float dt, V3 w, float turb) {
        if (!ready) Reset();
        if (!(dt > 0.0f)) return 0;
        acc += (std::min)(dt, 0.1f);
        int n = 0;
        while (acc >= kH && n < 8) { Sub(w, turb); acc -= kH; n++; }
        if (n == 8) acc = 0.0f;
        if (!ready) return n;
        if (n) Frames();
        Output(len(w), std::clamp(acc / kH, 0.0f, 1.0f));
        return n;
    }
};
} // namespace fx
