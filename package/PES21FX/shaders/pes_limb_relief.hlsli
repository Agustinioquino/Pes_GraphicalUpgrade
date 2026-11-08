// PES21 FX (08/11) - RELIEVE DE MANOS Y PIERNAS (incluido en el PS de la piel del cuerpo aa7d1833)
// Agustin (08/11): "la idea es que las manos se vean mejor, los brazos y sus uniones, los musculos" y "mejora las manos mas que
// nada y los brazos, asi como los muslos". Los brazos ya tienen grupos musculares, venas y pliegues del codo por textura
// (pes_arm_anat_*, pes_veins_arm); las manos solo venas y pliegues; las piernas nada. Esto agrega, SIN texturas nuevas ni DLL:
//   - mano: tendones extensores del dorso (se marcan con los dedos estirados), cabezas de los nudillos y de la articulacion del
//     medio (mas con el puno cerrado: la piel se estira sobre el hueso), la punta del cubito en la muneca (lado del menique) y los
//     dos pliegues de la muneca en la palma.
//   - pierna: cuadriceps (la gota del vasto interno sobre la rodilla, vasto externo, recto anterior y sus surcos), la banda de
//     afuera del muslo, la separacion de los isquios y sus tendones detras de la rodilla; rotula con su borde, tendon rotuliano,
//     almohadillas de grasa a los costados y la tuberosidad de la tibia; arista de la tibia, tibial anterior y las dos cabezas del
//     gemelo. Pliegues de la rodilla segun la pose: atras se hunden al doblar, adelante (arriba de la rotula) la piel floja de la
//     pierna estirada que se va al doblar. Cada grupo se marca mas con lo que hace la pierna (esfuerzo del jugador, rodilla).
// Todo se calcula en 3D sobre la posicion de REPOSO del vertice (pR, TEXCOORD10; TorsoBody: brazo izquierdo en +x, la derecha es
// el espejo exacto, medido en hand_l / hand_r y thigh_*.fmdl de common_package): no depende de las uv (las 6 mallas del muslo
// tienen uv distintas) y no deja costuras. La pendiente se mide con 4 evaluaciones a lo largo de las direcciones de u y v en
// reposo (sacadas de las derivadas de pantalla de pR y de las uv) y entra en el normal map con la convencion del PS (+x con +u,
// +y con -v), igual que los pliegues de joint_skin.py. Se apaga solo cuando un pixel cubre mas de ~3 mm (de lejos no se ve).
// Fuerza: y305 ("definicion" de musculos con textura, 0.7 = 1) por la definicion general x172. x305 = 0 apaga todo.
#ifndef PES_LIMB_RELIEF_HLSLI
#define PES_LIMB_RELIEF_HLSLI

float PesLrG(float x) { return exp(-x * x); }
float PesLrWin(float s, float a, float b) { return smoothstep(a, a + 0.02, s) * (1.0 - smoothstep(b - 0.02, b, s)); }
float PesLrDth(float th, float c) { return fmod(th - c + 540.0, 360.0) - 180.0; }   // diferencia de angulos en grados, -180..180

// ------------------------------------------------------------------------------------------------------------------ mano
// marco de la mano izquierda en reposo (hand_veins.py frame): W muneca, A muneca -> nudillo del medio, C hacia el pulgar, D dorso
static const float3 PLR_W = float3(0.5958, 1.0782, 0.0630);
static const float3 PLR_A = float3(0.6661, -0.7404, 0.0902);
static const float3 PLR_C = float3(0.0312, 0.1485, 0.9884);
static const float3 PLR_D = float3(0.7452, 0.6556, -0.1220);
static const float  PLR_HW = 0.0365;
// articulaciones (joint_skin.py hand_joints): nudillo (mcp) y la del medio (pip) de indice, medio, anular y menique
static const float3 PLR_MCP[4] = { float3(0.6604, 1.0065, 0.1034), float3(0.6647, 1.0016, 0.0723), float3(0.6574, 1.0008, 0.0540), float3(0.6492, 1.0055, 0.0300) };
static const float3 PLR_PIP[4] = { float3(0.6868, 0.9752, 0.1113), float3(0.6966, 0.9706, 0.0772), float3(0.6863, 0.9731, 0.0452), float3(0.6702, 0.9826, 0.0226) };
static const float  PLR_CK[4] = { 0.858, 0.0, -0.505, -1.142 };   // posicion de cada nudillo a lo ancho (en Hw)
static const float3 PLR_ULNA = float3(0.6126, 1.0870, 0.0387);    // punta del cubito (vertice dorsal mas saliente del lado del menique)

float PesHandH(float3 p, float curl, out float crease) {
    crease = 0.0;
    float3 q = p - PLR_W;
    float sm = dot(q, PLR_A), cm = dot(q, PLR_C), d = dot(q, PLR_D);   // m
    float dors = smoothstep(0.004, 0.012, d);       // dorso: d +0.013..+0.020; palma: -0.010..-0.031
    float palm = 1.0 - smoothstep(-0.016, -0.008, d);
    float h = 0.0;
    float ext = 1.0 - 0.7 * curl;                   // tendones: dedos estirados
    float knk = 0.6 + 0.6 * curl;                   // nudillos: puno cerrado
    [unroll] for (int f = 0; f < 4; f++) {
        // tendon extensor: del centro del dorso de la muneca (a lo ancho, juntos) hasta su nudillo
        float2 a = float2(0.012, 0.30 * PLR_CK[f] * PLR_HW), b = float2(dot(PLR_MCP[f] - PLR_W, PLR_A) - 0.004, PLR_CK[f] * PLR_HW);
        float2 x = float2(sm, cm) - a, ab = b - a;
        float u = saturate(dot(x, ab) / dot(ab, ab));
        float dl = length(x - u * ab);
        h += 0.00042 * ext * PesLrG(dl / 0.0013) * smoothstep(0.0, 0.25, u) * (1.0 - smoothstep(0.85, 1.0, u)) * dors;
        // cabeza del nudillo y de la articulacion del medio (solo del lado del dorso del dedo)
        float3 k = p - PLR_MCP[f]; float kd = dot(k, PLR_D);
        h += 0.00080 * knk * PesLrG(length(k - kd * PLR_D) / 0.0055) * smoothstep(0.0, 0.006, kd);
        float3 j = p - PLR_PIP[f]; float jd = dot(j, PLR_D);
        h += 0.00035 * knk * PesLrG(length(j - jd * PLR_D) / 0.0035) * smoothstep(0.0, 0.004, jd);
    }
    // surcos entre los metacarpianos (entre tendones), mas hondos con los dedos estirados
    float cw = cm / PLR_HW;
    float between = PesLrG((cw - 0.43) / 0.16) + PesLrG((cw + 0.25) / 0.16) + PesLrG((cw + 0.82) / 0.16);
    h -= 0.00022 * ext * between * PesLrWin(sm, 0.035, 0.095) * dors;
    // punta del cubito
    h += 0.00110 * PesLrG(length(p - PLR_ULNA) / 0.0065);
    // pliegues de la muneca en la palma (dos lineas a lo ancho), un poco mas hondos con la mano cerrada
    float wl = PesLrG((sm + 0.004) / 0.0006) + 0.8 * PesLrG((sm - 0.006) / 0.0006);
    float cr = 0.00030 * (0.6 + 0.4 * curl) * wl * palm * PesLrG(cw / 1.2);
    crease = cr;
    return h - cr;
}

// ------------------------------------------------------------------------------------------------------------------ pierna
// eje de la pierna izquierda en reposo (thigh_short.fmdl): centro del muslo arriba, rodilla (entre dsk_knee adelante y
// dsk_kneeback atras) y la canilla abajo; 0 grados = adelante (+z), +90 = afuera (+x), -90 = adentro
static const float3 PLR_LA = float3(0.110, 0.850, 0.062);
static const float3 PLR_LK = float3(0.1525, 0.544, 0.056);
static const float3 PLR_LC = float3(0.178, 0.330, 0.040);

void PesLegFrame(float3 p, out float s, out float th) {
    float3 au = normalize(PLR_LA - PLR_LK), ad = normalize(PLR_LK - PLR_LC);
    float3 q = p - PLR_LK;
    float su = dot(q, au);
    float3 ax = su > 0.0 ? au : ad;
    s = dot(q, ax);                                  // + muslo, - canilla (m desde la rodilla)
    float3 r = q - s * ax;
    float3 fw = normalize(float3(0, 0, 1) - ax.z * ax), lt = normalize(cross(ax, fw));
    if (dot(lt, float3(1, 0, 0)) < 0.0) lt = -lt;
    th = degrees(atan2(dot(r, lt), dot(r, fw)));
}

// flex 0..1 rodilla doblada, eff 0..1 esfuerzo del jugador; crease = pliegues de la pose (para oscurecer el fondo)
float PesLegH(float3 p, float flex, float eff, out float crease) {
    crease = 0.0;
    float s, th; PesLegFrame(p, s, th);
    float kq = 0.55 + 0.45 * eff + 0.25 * (1.0 - flex);   // cuadriceps: esfuerzo y pierna estirada
    float kh = 0.55 + 0.45 * flex + 0.20 * eff;           // isquios: rodilla doblada
    float kc = 0.55 + 0.60 * eff;                         // gemelos: esfuerzo
    float h = 0.0, q = 0.0;
    // cuadriceps
    q += 0.0016 * PesLrG((s - 0.075) / 0.030) * PesLrG(PesLrDth(th, -55.0) / 24.0);          // vasto interno (la gota)
    q += 0.0010 * PesLrG((s - 0.160) / 0.060) * PesLrG(PesLrDth(th, 70.0) / 30.0);           // vasto externo
    q += 0.0008 * PesLrG((s - 0.200) / 0.070) * PesLrG(th / 18.0);                           // recto anterior
    q -= 0.0005 * PesLrG(PesLrDth(th, 32.0) / 7.0) * PesLrWin(s, 0.10, 0.32);                // surco recto / vasto externo
    q -= 0.0004 * PesLrG(PesLrDth(th, -28.0) / 7.0) * PesLrWin(s, 0.12, 0.32);               // surco recto / vasto interno
    float rv = length(float2((s - 0.080) / 0.042, PesLrDth(th, -55.0) / 34.0));
    q -= 0.0004 * PesLrG((rv - 1.0) / 0.22) * smoothstep(0.045, 0.07, s);                    // borde de arriba de la gota
    h += kq * q;
    h -= 0.0006 * PesLrG(PesLrDth(th, 100.0) / 9.0) * PesLrWin(s, 0.06, 0.32);               // banda de afuera del muslo
    // isquios
    float hm = -0.0005 * PesLrG(PesLrDth(th, 180.0) / 9.0) * PesLrWin(s, 0.08, 0.30);        // separacion de los isquios
    hm += 0.0007 * (PesLrG(PesLrDth(th, 140.0) / 10.0) + PesLrG(PesLrDth(th, -140.0) / 10.0)) * PesLrWin(s, -0.005, 0.10);   // tendones detras de la rodilla
    h += kh * hm;
    // rodilla (hueso: siempre igual)
    h -= 0.0005 * PesLrG((s - 0.045) / 0.012) * PesLrG(th / 22.0);                           // hoyuelo arriba de la rotula
    float2 pt = float2((s - 0.008) / 0.022, th / 24.0);
    h += 0.0012 * exp(-dot(pt, pt));                                                         // rotula (pow con base negativa da NaN)
    float rp = length(float2((s - 0.008) / 0.024, th / 26.0));
    h -= 0.0004 * PesLrG((rp - 1.15) / 0.20) * (1.0 - smoothstep(0.02, 0.035, s));          // borde de la rotula (abajo y costados)
    h += 0.0006 * PesLrG(th / 7.0) * PesLrWin(s, -0.065, -0.015);                            // tendon rotuliano
    h += 0.0006 * PesLrG((s + 0.035) / 0.012) * (PesLrG(PesLrDth(th, 22.0) / 9.0) + PesLrG(PesLrDth(th, -22.0) / 9.0));   // grasa a los costados
    h += 0.0005 * PesLrG((s + 0.075) / 0.012) * PesLrG(th / 10.0);                           // tuberosidad de la tibia
    h -= 0.0006 * PesLrG(s / 0.030) * PesLrG(PesLrDth(th, 180.0) / 35.0);                    // hueco de atras de la rodilla
    // canilla y gemelos
    h += 0.0005 * PesLrG(PesLrDth(th, -12.0) / 6.0) * PesLrWin(s, -0.22, -0.09);             // arista de la tibia
    h += 0.0007 * PesLrG((s + 0.12) / 0.05) * PesLrG(PesLrDth(th, 35.0) / 18.0);             // tibial anterior
    float hc = 0.0018 * PesLrG((s + 0.13) / 0.045) * PesLrG(PesLrDth(th, -150.0) / 24.0);    // gemelo interno
    hc += 0.0013 * PesLrG((s + 0.10) / 0.040) * PesLrG(PesLrDth(th, 148.0) / 20.0);          // gemelo externo
    hc -= 0.0005 * PesLrG(PesLrDth(th, 180.0) / 7.0) * PesLrWin(s, -0.20, -0.04);            // entre las dos cabezas
    hc -= 0.0004 * PesLrG(PesLrDth(th, 110.0) / 7.0) * PesLrWin(s, -0.20, -0.07);            // borde de afuera del gemelo
    h += kc * hc;
    // pliegues de la pose
    float tb = PesLrDth(th, 180.0);
    float sb = s + 0.0015 * sin(radians(th) * 3.0);
    float back = (PesLrG((sb - 0.004) / 0.0008) + 0.7 * PesLrG((sb + 0.012) / 0.0008)) * PesLrG(tb / 45.0);
    float cb = 0.00035 * (0.25 + 0.75 * flex) * back;                                        // atras: se hunden al doblar
    float sf = s + 0.0008 * sin(radians(th) * 7.0);
    float fr = pow(max(0.0, cos(6.2831853 * sf / 0.0035)), 5.0) * PesLrG((s - 0.035) / 0.014) * PesLrG(th / 35.0);
    float cf = 0.00015 * (1.0 - 0.85 * flex) * fr;                                           // adelante: piel floja estirada
    crease = cb + cf;
    return h - crease;
}

// kind 1 mano, 2 pierna; a, b = (cierre del puno, 0) o (rodilla, esfuerzo). Suma la pendiente a nxy y devuelve (cavidad, pliegue)
float2 PesLimbRelief(int kind, float3 pr, float a, float b, float3 dPx, float3 dPy, float2 dUx, float2 dUy, float gain, inout float2 nxy) {
    float pix = max(length(dPx), length(dPy));
    float fade = 1.0 - smoothstep(0.0012, 0.0030, pix);
    if (fade <= 0.0 || gain <= 0.0) return 0;
    float det = dUx.x * dUy.y - dUx.y * dUy.x;
    if (abs(det) < 1e-12) return 0;
    float3 Pu = (dUy.y * dPx - dUx.y * dPy) / det, Pv = (dUx.x * dPy - dUy.x * dPx) / det;
    float lu = length(Pu), lv = length(Pv);
    if (lu < 1e-6 || lv < 1e-6) return 0;
    float3 eu = Pu / lu, ev = Pv / lv;
    float3 p = float3(abs(pr.x), pr.y, pr.z);
    // el espejo de x cambia el sentido de u / v en la derecha: las direcciones tambien se espejan
    if (pr.x < 0.0) { eu.x = -eu.x; ev.x = -ev.x; }
    float e = clamp(pix, 0.0002, 0.0012);   // diferencia del tamano del pixel: filtra lo que no entra en un pixel (sin parpadeo)
    float c0, c1, c2, c3, c4, h0, hu, hv;
    if (kind == 1) {
        h0 = PesHandH(p, a, c0);
        hu = (PesHandH(p + e * eu, a, c1) - PesHandH(p - e * eu, a, c2)) / (2.0 * e);
        hv = (PesHandH(p + e * ev, a, c3) - PesHandH(p - e * ev, a, c4)) / (2.0 * e);
    } else {
        h0 = PesLegH(p, a, b, c0);
        hu = (PesLegH(p + e * eu, a, b, c1) - PesLegH(p - e * eu, a, b, c2)) / (2.0 * e);
        hv = (PesLegH(p + e * ev, a, b, c3) - PesLegH(p - e * ev, a, b, c4)) / (2.0 * e);
    }
    float k = 2.2 * gain * fade;
    nxy += clamp(float2(-hu, hv) * k, -0.45, 0.45);
    float l2 = dot(nxy, nxy); if (l2 > 0.94) nxy *= rsqrt(l2 / 0.94);
    return float2(saturate(-h0 / 0.0012) * gain * fade, saturate(c0 / 0.0004) * gain * fade);
}
#endif
