// PES21 FX (26/10, v12.6) - unas de los jugadores que traen su propia textura de brazo (caras reales: face\real\<id>\
// sourceimages\#windx11\arm_bsm.ftex; es el atlas de Konami a 2048, con las manos pintadas para la mano de Konami).
// La mano de TorsoBody tiene la placa 3D de cada una en otro lugar (v12.3), asi que en esos jugadores la una pintada queda
// medio en el dedo y la placa muestra piel. PesNailFix (solo en los pixeles de la mano de TorsoBody, y solo si la textura
// de color no es la de TorsoBody) borra la una pintada (la piel de los dos costados, justo afuera de la una, se mezcla
// a lo ancho del dedo) y pinta la placa con la una pintada de esa misma textura (raiz con raiz, borde libre con borde
// libre): cada jugador conserva el color de sus unas. Tablas generadas por v126_nailfix.py desde la malla de las manos
// v12.5 (si cambian las UV de las unas de hand_l / hand_r hay que regenerarlas).
//   NF_N (4 por una pintada, 10): caja uv; centro, radio donde empieza y termina el borrado; semiejes a (raiz -> punta) y b;
//        inversa de [a b]
//   NF_P (7 por placa, 10): caja uv; centro, una pintada, radio^2 donde vale el mapa;
//        10 + 10 coeficientes cubicos uv -> (nv, nun) (nv 0 raiz .. 1 borde libre, nun -1..1; placa = superelipse
//        exponente 2 en la mitad de la raiz y 3 en la del borde libre, como v116_fields.nail_eval)
#ifndef PES_NAILFIX_HLSLI
#define PES_NAILFIX_HLSLI
static const float4 NF_N[40] = {
  // indice mano derecha
  float4(0.285658935, 0.743471892, 0.314905862, 0.773472288),
  float4(0.300282399, 0.75847209, 1.1818764, 1.47287865),
  float4(-0.00441969624, 0.00635017139, 0.00550879521, 0.00383410148),
  float4(-73.835867, 106.086569, 122.289515, 85.1130587),
  // indice mano izquierda
  float4(0.688988793, 0.747311063, 0.710353745, 0.769672201),
  float4(0.699671269, 0.758491632, 1.17234538, 1.448098),
  float4(0.000299761783, 0.00743555235, 0.00707713963, -0.000285312496),
  float4(5.41308092, 134.270773, 141.070746, -5.6872195),
  // anular mano derecha
  float4(0.0530451439, 0.747690029, 0.0762674285, 0.76515519),
  float4(0.0646562862, 0.75642261, 1.23213068, 1.60353978),
  float4(-0.000139905153, 0.00525682784, 0.0071010392, 0.000188986972),
  float4(-5.05916333, 190.094146, 140.724783, 3.74524768),
  // anular mano izquierda
  float4(0.923732571, 0.747690029, 0.946954856, 0.76515519),
  float4(0.935343714, 0.75642261, 1.23213068, 1.60353978),
  float4(0.000139905153, 0.00525682784, 0.0071010392, -0.000188986972),
  float4(5.05916333, 190.094146, 140.724783, -3.74524768),
  // pulgar mano derecha
  float4(0.412486147, 0.747100522, 0.450355865, 0.780301767),
  float4(0.431421006, 0.763701144, 1.14703896, 1.38230128),
  float4(0.00315329067, 0.00767973629, 0.0105447787, -0.00432967369),
  float4(45.7518551, 111.427147, 81.1521162, -33.3209633),
  // pulgar mano izquierda
  float4(0.549655477, 0.747114035, 0.587491915, 0.780282695),
  float4(0.568573696, 0.763698365, 1.14699642, 1.3821907),
  float4(-0.00314374257, 0.00768624511, 0.0105433841, 0.0043123378),
  float4(-45.5869351, 111.457077, 81.2534577, 33.2333864),
  // mayor mano derecha
  float4(0.171025221, 0.758750687, 0.198054794, 0.788321464),
  float4(0.184540008, 0.773536076, 1.18317858, 1.47626431),
  float4(-0.00293665955, 0.00761859797, 0.00621806067, 0.0023968094),
  float4(-44.0496702, 114.278391, 140.018112, 53.9712855),
  // mayor mano izquierda
  float4(0.801125389, 0.758503746, 0.829795756, 0.788734398),
  float4(0.815460573, 0.773619072, 1.17890303, 1.46514788),
  float4(0.00373343888, 0.00716282428, 0.00605068171, -0.00315376302),
  float4(57.2221743, 109.784141, 129.962961, -67.739868),
  // menique mano derecha
  float4(0.0214336009, 0.861805696, 0.0515714485, 0.889003942),
  float4(0.0365025247, 0.875404819, 1.20142772, 1.52371208),
  float4(0.00258328122, 0.0054820921, 0.00730633242, -0.003442903),
  float4(70.3380083, 149.26731, 111.998346, -52.7760603),
  // menique mano izquierda
  float4(0.948428552, 0.861805696, 0.978566399, 0.889003942),
  float4(0.963497475, 0.875404819, 1.20142772, 1.52371208),
  float4(-0.00258328122, 0.0054820921, 0.00730633242, 0.003442903),
  float4(-70.3380083, 149.26731, 111.998346, 52.7760603)
};
static const float4 NF_P[70] = {
  // placa anular mano derecha
  float4(0.0578613281, 0.754394531, 0.0729980469, 0.767089844),
  float4(0.0655538393, 0.760532329, 2, 7.96252452e-05),
  float4(0.51561336, 3.63876222, 101.486553, -1491.15199),
  float4(-207.135068, -435.26755, -19609.5078, -343358.439),
  float4(-34724.0089, -270171.567, -0.00148695843, 160.72574),
  float4(-8.3771099, 187.776942, 1173.3025, 376.520996),
  float4(-282784.506, 14155.2822, -354904.599, 26956.4903),
  // placa anular mano izquierda
  float4(0.926269531, 0.754394531, 0.941650391, 0.767089844),
  float4(0.933964336, 0.760532256, 3, 7.69964915e-05),
  float4(0.517980571, -3.34828171, 102.018614, -1539.60099),
  float4(233.8563, -504.683834, 17057.4367, -352733.623),
  float4(31543.0533, -283564.236, -0.00810035696, 151.984159),
  float4(10.0773804, -29.8302811, 973.446511, -190.483747),
  float4(-151670.47, -107807.853, -158745.061, -27470.131),
  // placa indice mano derecha
  float4(0.291992188, 0.755859375, 0.311767578, 0.770263672),
  float4(0.301367382, 0.762975994, 0, 0.000128934283),
  float4(0.528366081, -3.5014912, 84.3966162, -638.959893),
  float4(-518.032937, -461.573582, 15596.5431, -119755.073),
  float4(-21659.9792, -136056.169, 0.0785827225, 138.125899),
  float4(13.6701229, -995.411944, 330.191422, 351.826582),
  float4(-246532.732, -190504.097, -298062.203, -90644.3239),
  // placa indice mano izquierda
  float4(0.687988281, 0.755859375, 0.707275391, 0.770263672),
  float4(0.698097285, 0.762980087, 1, 0.000125264707),
  float4(0.52943948, 3.40753893, 84.5235634, -648.710799),
  float4(497.989181, -484.815575, -14683.6745, -122542),
  float4(23180.1891, -139402.106, -0.0595524379, 138.067735),
  float4(-16.8692218, 797.440937, 299.256625, -531.472254),
  float4(-246070.588, 229352.978, -269536.571, 124301.229),
  // placa pulgar mano derecha
  float4(0.420166016, 0.76171875, 0.443847656, 0.778320312),
  float4(0.43216022, 0.770197135, 4, 0.000144735897),
  float4(0.495887403, 11.82364, 68.1761145, 245.518928),
  float4(-311.206996, 342.716422, 7001.39335, -16262.7121),
  float4(7149.74179, -6700.55542, 0.0100520907, 112.204253),
  float4(-23.2283633, -23.8169503, -73.870185, -181.635391),
  float4(-157909.79, 38925.8988, -124099.818, 14937.854),
  // placa pulgar mano izquierda
  float4(0.555664062, 0.761962891, 0.579345703, 0.778320312),
  float4(0.567359328, 0.770229502, 5, 0.000146383519),
  float4(0.496435362, -11.6704921, 68.0845267, 246.007402),
  float4(305.159727, 352.656696, -7720.9002, -13620.6034),
  float4(-11663.1337, -5190.27479, -0.015278567, 111.978039),
  float4(23.0339916, 30.6655163, -111.463131, 135.636236),
  float4(-154380.141, -44323.8229, -126883.317, -22822.3511),
  // placa mayor mano derecha
  float4(0.176513672, 0.767822266, 0.196044922, 0.782958984),
  float4(0.185739227, 0.775327765, 6, 0.000125332312),
  float4(0.539037972, 20.8870367, 77.1578752, -890.411057),
  float4(71.4563271, -388.941489, -69301.6558, -108997.174),
  float4(-86673.6287, -74619.628, -0.017129548, 135.88902),
  float4(-43.7812315, 438.789881, 15.9347196, 105.363112),
  float4(-257283.028, 133598.421, -250482.86, 121660.354),
  // placa mayor mano izquierda
  float4(0.803466797, 0.767822266, 0.822998047, 0.782958984),
  float4(0.813781226, 0.775335429, 7, 0.000128021368),
  float4(0.539402411, -20.9447531, 77.6119245, -887.144732),
  float4(-40.1629089, -378.748326, 70013.4824, -108726.482),
  float4(89997.4909, -82148.7463, 0.0163934253, 137.189731),
  float4(40.5290703, -411.933824, -14.0886588, -151.272455),
  float4(-272287.826, -116196.393, -276971.69, -73215.9005),
  // placa menique mano derecha
  float4(0.0329589844, 0.875244141, 0.0471191406, 0.885742188),
  float4(0.0399898088, 0.880242246, 8, 6.55217959e-05),
  float4(0.517664795, 3.08276639, 122.35238, -416.42905),
  float4(249.606503, -919.653894, 37560.2475, -444651.755),
  float4(154965.942, -506527.864, -0.0154599122, 180.952042),
  float4(-27.0952916, 336.157923, 222.93359, 1463.25967),
  float4(-484199.697, 368496.379, -631320.49, 159505.116),
  // placa menique mano izquierda
  float4(0.952636719, 0.875244141, 0.966552734, 0.885986328),
  float4(0.959486254, 0.880245116, 9, 6.78393893e-05),
  float4(0.519354227, -3.13680848, 122.435998, -447.047107),
  float4(-208.258797, -937.728855, -35105.7621, -456537.132),
  float4(-158606.458, -509496.625, -0.013778099, 181.294093),
  float4(26.7560651, 168.617178, 387.517595, -1164.38203),
  float4(-488255.9, -352298.2, -708721.912, -124848.887)
};
static const float NF_FEATHER = 0.070;   // borde suave de la placa (en radios de la placa)
static const float NF_SHRINK = 0.880;    // la placa toma de adentro de la una pintada (sin su contorno)
static const float NF_MARGIN = 0.120;    // la piel se toma un poco mas afuera del borrado

// tex = textura de color del jugador; dx/dy = derivadas de uv (fuera de las ramas); amt = 0..1
float3 PesNailFix(Texture2D<float4> tex, SamplerState smp, float2 uv, float2 dx, float2 dy, float3 col, float amt)
{
  float3 c = col;
  [loop] for (int i = 0; i < 10; i++) {   // unas pintadas -> piel de los costados
    float4 bb = NF_N[4 * i];
    if (uv.x < bb.x || uv.y < bb.y || uv.x > bb.z || uv.y > bb.w) continue;
    float4 cr = NF_N[4 * i + 1], ab = NF_N[4 * i + 2], mi = NF_N[4 * i + 3];
    float2 d = uv - cr.xy;
    float2 l = float2(mi.x * d.x + mi.y * d.y, mi.z * d.x + mi.w * d.y);
    float w = 1.0 - smoothstep(cr.z, cr.w, length(l));
    if (w <= 0.0) continue;
    float R = cr.w + NF_MARGIN;
    float h = sqrt(max(R * R - l.x * l.x, 0.0)) + 1e-4;
    float t = saturate((l.y + h) / (2.0 * h));
    float2 q = cr.xy + l.x * ab.xy;
    float3 pa = tex.SampleGrad(smp, q - h * ab.zw, dx, dy).rgb;
    float3 pb = tex.SampleGrad(smp, q + h * ab.zw, dx, dy).rgb;
    c = lerp(c, lerp(pa, pb, t), w * amt);
  }
  [loop] for (int j = 0; j < 10; j++) {   // placas -> la una pintada de la misma textura
    float4 bb = NF_P[7 * j];
    if (uv.x < bb.x || uv.y < bb.y || uv.x > bb.z || uv.y > bb.w) continue;
    float4 c0 = NF_P[7 * j + 1];
    float2 d = uv - c0.xy;
    if (dot(d, d) > c0.w) continue;   // solo cerca de la placa (el cubico no vale lejos)
    float4 tA = float4(1.0, d.x, d.y, d.x * d.x), tB = float4(d.x * d.y, d.y * d.y, d.x * d.x * d.x, d.x * d.x * d.y);
    float2 tC = float2(d.x * d.y * d.y, d.y * d.y * d.y);
    float4 k0 = NF_P[7 * j + 2], k1 = NF_P[7 * j + 3], k2 = NF_P[7 * j + 4], k3 = NF_P[7 * j + 5], k4 = NF_P[7 * j + 6];
    float nv = dot(tA, k0) + dot(tB, k1) + dot(tC, k2.xy);
    float nun = dot(tA, float4(k2.zw, k3.xy)) + dot(tB, float4(k3.zw, k4.xy)) + dot(tC, k4.zw);
    float s = 2.0 * nv - 1.0;
    float e = s < 0.0 ? sqrt(s * s + nun * nun) : pow(abs(s * s * s) + abs(nun * nun * nun), 1.0 / 3.0);
    float w = 1.0 - smoothstep(1.0 - NF_FEATHER, 1.0 + NF_FEATHER, e);
    if (w <= 0.0) continue;
    int k = (int)(c0.z + 0.5);
    float4 cr = NF_N[4 * k + 1], ab = NF_N[4 * k + 2];
    float2 p = float2(s, nun) * (e / max(length(float2(s, nun)), 1e-5)) * NF_SHRINK;
    float3 nc = tex.SampleGrad(smp, cr.xy + p.x * ab.xy + p.y * ab.zw, dx, dy).rgb;
    c = lerp(c, nc, w * amt);
  }
  return c;
}
#endif
