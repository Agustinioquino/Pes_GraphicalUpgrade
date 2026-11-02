// PES21 FX - rebote de luz en pantalla (SSGI) temporal, paso 2 (21/10): acumulacion en el tiempo.
// Cada pixel se pasa a mundo con la camara de este cuadro y se proyecta con la del cuadro anterior para leer su historia.
// La historia se descarta si ahi habia otra superficie (profundidad distinta: algo que se movio o aparecio) o si cae fuera
// de pantalla, y se recorta a la media +- 1.5 desvios de los vecinos de este cuadro para que no queden estelas.
//   t0 SSGI de este cuadro (rgb, profundidad), t1 historia (rgb, profundidad); b2 escena de este cuadro, b3 la del anterior.
cbuffer cScene : register(b2) { float4 c[30]; };
cbuffer cPrev  : register(b3) { float4 q[30]; };
Texture2D<float4> inCur  : register(t0);
Texture2D<float4> inHist : register(t1);
Texture1D<float4> IniParams : register(t120);
SamplerState sPoint : register(s0);
SamplerState sLinear : register(s1);

// (02/11) modo reconstruccion (155.z = 1 + fase, "Completa" barata): t0 viene a media resolucion con un pixel exacto de cada bloque
// de 2x2 (el de la fase de este cuadro) y este paso corre a resolucion completa con la profundidad propia (t2). El valor de este
// cuadro sale de los 3x3 vecinos de media resolucion de la misma superficie, pesados por la distancia real a su pixel (filtro
// bilateral: tambien saca el ruido), y el pixel exacto entra con mas peso en la historia, asi la definicion es la de Completa.
Texture2D<float4> inDepthF : register(t2);
static const int2 kSSGIPhase[4] = { int2(0, 0), int2(1, 1), int2(1, 0), int2(0, 1) };

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float ph155 = IniParams.Load(int2(155, 0)).z;
  bool recon = ph155 > 0.5;
  float4 cur; float z;
  float3 m1 = 0, m2 = 0; float n = 0;
  float W, H; inCur.GetDimensions(W, H);
  float2 tx = 1.0 / float2(W, H);
  bool exact = false;
  if (recon) {
    float d = inDepthF.SampleLevel(sPoint, uv, 0).x;
    if (d < 1e-6) return 0.0;                                   // cielo
    z = c[21].z / (d - c[21].w);
    if (z <= 0.0) return 0.0;
    int2 o = kSSGIPhase[((int)(ph155 + 0.5) - 1) & 3];
    int2 pF = (int2)pos.xy, hpx = pF >> 1;
    exact = all(pF - hpx * 2 == o);
    float3 wSum = 0; float wT = 0; float4 nearest = 0; float dBest = 1e9;
    [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
      int2 t = clamp(hpx + int2(x, y), int2(0, 0), int2(W, H) - 1);
      float4 s = inCur.Load(int3(t, 0));
      float2 dp = float2(t * 2 + o - pF);
      float dz = abs(s.w - z);
      if (dz < dBest) { dBest = dz; nearest = s; }
      if (s.w <= 0.0 || dz > 0.05 * z) continue;
      float w = exp(-dot(dp, dp) * (1.0 / 4.5));                  // gaussiana de 1.5 pixeles
      wSum += s.rgb * w; wT += w;
      m1 += s.rgb; m2 += s.rgb * s.rgb; n += 1.0;
    }
    cur = float4(wT > 1e-4 ? wSum / wT : nearest.rgb, z);
    if (n < 0.5) { m1 = cur.rgb; m2 = cur.rgb * cur.rgb; n = 1.0; }
  } else {
    cur = inCur.SampleLevel(sPoint, uv, 0);
    z = cur.w;
    if (z <= 0.0) return 0.0;
    // vecinos de este cuadro (3x3, misma superficie)
    // (02/11) y el valor de este cuadro filtrado con ellos (bilateral gaussiano): cada pixel tiene 7-8 muestras y las del anillo lejano
    // valen mucho (un cartel o un reflector) -> puntos blancos sueltos que cambian cada cuadro ("el rebote vibra mucho", Agustin)
    float3 fS = 0; float fW = 0;
    [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
      float4 s = inCur.SampleLevel(sPoint, uv + float2(x, y) * tx, 0);
      if (abs(s.w - z) > 0.05 * z) continue;
      m1 += s.rgb; m2 += s.rgb * s.rgb; n += 1.0;
      float w = exp(-(x * x + y * y) * 0.5);
      fS += s.rgb * w; fW += w;
    }
    cur.rgb = fS / max(fW, 1e-4);
  }
  m1 /= max(n, 1.0); m2 /= max(n, 1.0);
  if (recon) tx = 1.0 / float2(W * 2.0, H * 2.0);                // la historia esta a resolucion completa
  float3 sd = sqrt(max(m2 - m1 * m1, 0.0));
  // a mundo con la camara actual y de vuelta a pantalla con la anterior
  float2 sC = uv * float2(2, -2) + float2(-1, 1);
  float3 Pv = float3((sC - c[28].xy) * c[21].xy * z, z);
  float3 pW = c[20].xyz + float3(dot(Pv, float3(c[8].x, c[9].x, c[10].x)), dot(Pv, float3(c[8].y, c[9].y, c[10].y)), dot(Pv, float3(c[8].z, c[9].z, c[10].z)));
  float4 hp = float4(pW, 1.0);
  float cw = dot(q[3], hp);
  float4 P153 = IniParams.Load(int2(153, 0));
  float frames = clamp(P153.z > 0 ? P153.z : 12.0, 2.0, 40.0);
  // (23/10) sin historia valida (corte de camara, algo que aparece): en vez del valor de 8 muestras de este cuadro (ruidoso:
  // se veia "recalcular" el rebote en cada corte) se arranca del promedio de los vecinos de la misma superficie
  float a = 0.3; bool hasHist = false;
  float3 hist = m1;
  if (cw > 0.05 && q[21].z != 0.0) {
    float2 ndc = float2(dot(q[0], hp), dot(q[1], hp)) / cw + q[28].xy;
    float2 uvP = ndc * float2(0.5, -0.5) + 0.5;
    if (all(uvP > tx) && all(uvP < 1.0 - tx)) {
      float4 h = inHist.SampleLevel(sLinear, uvP, 0);
      float hz = inHist.SampleLevel(sPoint, uvP, 0).w;
      if (hz > 0.0 && abs(hz - cw) < 0.04 * cw + 0.03) {
        hist = clamp(h.rgb, m1 - 1.5 * sd, m1 + 1.5 * sd);
        a = 1.0 / frames; hasHist = true;
        // reconstruccion: el pixel exacto pesa 2.5 veces y los reconstruidos 0.5 (en 4 cuadros suma lo mismo que antes)
        if (recon) a = exact ? min(1.0, 2.5 * a) : 0.5 * a;
      }
    }
  }
  if (recon && !hasHist) { hist = cur.rgb; }                     // sin historia: el valor reconstruido ya esta filtrado
  return float4(lerp(hist, cur.rgb, a), z);
}
