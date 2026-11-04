// PES21 FX (04/11 i) - PESTANAS CON HEBRAS (Agustin: "puedes darle hebras a las pestanas?"). Mismo esquema que las cejas v2: la
// DLL vuelve a dibujar la tira de pestanas del juego (VS 66917213 del mod de expresiones: sigue el parpadeo y los gestos, PS del
// juego f0d74b9c) con este GS y el PS de las hebras de ceja (pes_brow_ps: G-buffer con marca de pelo, luz del pelo). Cada
// triangulo de la tira suelta pelos en puntos FIJOS (baricentricas por hash, no bailan) donde la textura de pestanas tiene alfa;
// cada pelo sigue el TRAZO PINTADO de esa pestana (orientacion del alfa alrededor, tensor de estructura) desde el punto hasta la
// punta del trazo (donde el alfa se corta), y se curva un poco hacia afuera. La tira original sigue dibujandose debajo.
// Fila 368: x on | y densidad (1 = normal) | z largo (x) | w grosor (x). Distancia maxima = y75 (la de las cejas).
#define PES_SEG 4
#define PES_PER_INST 4      // 4 x 10 vertices x 23 escalares = 920 (tope 1024)
#define PES_INST 16
cbuffer cVSScene : register(b2)
{
  float4 m_projectionView[4];
  float4 m_projection[4];
  float4 m_view[4];
  float4 m_shadowProjection[4];
  float4 m_shadowProjection2[4];
  float4 m_eyepos;
  float4 m_projectionParam;
  float4 m_viewportSize;
  float4 m_exposure;
  float4 m_fogParam[3];
  float4 m_fogColor;
  float4 m_cameraCenterOffset;
  float4 m_shadowMapResolutions;
}
Texture1D<float4> IniParams : register(t120);
Texture2D<float4> tLash : register(t0);      // difusa de las pestanas (la misma del dibujo; alfa = pestana)
SamplerState sLin : register(s0);

struct LashV {   // salida del VS 66917213 (en este orden)
  float4 pos : SV_Position0;
  float4 c1 : COLOR0;
  float2 uv : TEXCOORD0;
  float2 ww : TEXCOORD9;        // x = w actual, y = w anterior
  float3 tng : TEXCOORD5;
  float3 btg : TEXCOORD6;
  float3 nrm : TEXCOORD7;
  float4 clipCP : TEXCOORD8;    // xy = clip actual, zw = clip del cuadro anterior
};

struct GSOut {   // igual que la entrada de pes_brow_ps.hlsl
  float4 pos : SV_Position;
  float3 nrmV : TEXCOORD0;
  float3 col : TEXCOORD1;
  float2 tw : TEXCOORD2;
  float2 motion : TEXCOORD3;
  float3 viewV : TEXCOORD4;
  float3 col2 : TEXCOORD5;
  float3 tanV : TEXCOORD6;
};

uint PesHash(uint v) { uint s = v * 747796405u + 2891336453u; uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u; return (w >> 22u) ^ w; }
float PesRnd(inout uint s) { s = PesHash(s); return (float)(s & 0xffffff) / 16777216.0; }

float4 ProjV(float3 p) {
  float4 h = float4(p, 1);
  return float4(dot(h, m_projection[0]), dot(h, m_projection[1]), dot(h, m_projection[2]), dot(h, m_projection[3]));
}
// el VS no saca la posicion en la vista: se recupera del clip con la proyeccion (filas x, y, w; 3 x 3)
float3 UnprojV(float4 c) {
  float3 r0 = m_projection[0].xyz, r1 = m_projection[1].xyz, r3 = m_projection[3].xyz;
  float3 b = float3(c.x - m_projection[0].w, c.y - m_projection[1].w, c.w - m_projection[3].w);
  float3 c0 = cross(r1, r3), c1 = cross(r3, r0), c2 = cross(r0, r1);
  float det = dot(r0, c0);
  if (abs(det) < 1e-12) return float3(0, 0, 0);
  return (c0 * b.x + c1 * b.y + c2 * b.z) / det;
}
float LA(float2 uv) { return tLash.SampleLevel(sLin, uv, 0).a; }

[instance(PES_INST)]
[maxvertexcount(PES_PER_INST * (PES_SEG + 1) * 2)]
void main(triangle LashV v[3], uint pid : SV_PrimitiveID, uint inst : SV_GSInstanceID, inout TriangleStream<GSOut> outS)
{
  float4 P = IniParams.Load(int2(368, 0));
  float4 P75 = IniParams.Load(int2(75, 0));
  if (P.x < 0.5) return;
  float3 P0 = UnprojV(v[0].pos), P1 = UnprojV(v[1].pos), P2 = UnprojV(v[2].pos);
  float zc = (P0.z + P1.z + P2.z) / 3.0;
  if (zc < 0.03) return;
  float vpH = m_viewportSize.y > 16.0 ? m_viewportSize.y : 1080.0;
  float ppm = abs(m_projection[1].y) * vpH * 0.5 / zc;
  if (ppm < 1.73 * 540.0 / max(P75.y, 0.5) * 1.5 && P75.z < 2.5) return;   // las pestanas son mas chicas: la mitad de la distancia
  float2 uv0 = v[0].uv, uv1 = v[1].uv, uv2 = v[2].uv;
  float3 e1 = P1 - P0, e2 = P2 - P0;
  float2 d1 = uv1 - uv0, d2 = uv2 - uv0;
  float det = d1.x * d2.y - d2.x * d1.y;
  float uvArea = abs(det) * 0.5;
  if (uvArea < 1e-10) return;
  float3 dPdu = (e1 * d2.y - e2 * d1.y) / det;
  float3 dPdv = (e2 * d1.x - e1 * d2.x) / det;
  float tw, th; tLash.GetDimensions(tw, th);
  float2 tx = 1.0 / float2(max(tw, 1.0), max(th, 1.0));
  // un pelo cada ~30 texeles de tira (y368 = 1); el alfa decide despues si nace
  float keep = saturate(uvArea * tw * th / 30.0 * clamp(P.y, 0.2, 4.0) / (float)(PES_INST * PES_PER_INST));
  float mLen = clamp(P.z, 0.2, 3.0), mWid = clamp(P.w, 0.2, 4.0);
  [loop] for (uint k = 0; k < PES_PER_INST; k++) {
    uint seed = PesHash(pid * 6271u + (inst * PES_PER_INST + k) * 92821u + 41u);
    float r0 = PesRnd(seed), r1 = PesRnd(seed), r2 = PesRnd(seed), r3 = PesRnd(seed), rk = PesRnd(seed);
    if (rk >= keep) continue;
    if (r0 + r1 > 1.0) { r0 = 1.0 - r0; r1 = 1.0 - r1; }
    float3 b = float3(1.0 - r0 - r1, r0, r1);
    float2 uv = uv0 * b.x + uv1 * b.y + uv2 * b.z;
    float a0 = LA(uv);
    // (04/11 j, Agustin: "le faltan las pestanas de abajo") las de abajo estan pintadas mas finas y claras en la tira: umbral 0.5 -> 0.25
    if (a0 < 0.25) continue;                                   // solo sobre una pestana pintada
    // orientacion del trazo: tensor de estructura del alfa en 3 x 3 (el trazo va perpendicular al gradiente)
    float jxx = 0, jxy = 0, jyy = 0;
    [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
      float2 q = uv + float2(x, y) * tx * 1.5;
      float gx = LA(q + float2(tx.x, 0)) - LA(q - float2(tx.x, 0));
      float gy = LA(q + float2(0, tx.y)) - LA(q - float2(0, tx.y));
      jxx += gx * gx; jxy += gx * gy; jyy += gy * gy;
    }
    float th0 = 0.5 * atan2(2.0 * jxy, jxx - jyy) + 1.5707963;
    float2 dirT = float2(cos(th0), sin(th0));                 // a lo largo del trazo (en texeles)
    // largo del trazo hacia cada lado (hasta que el alfa se corta) y punta = el lado que se afina (menos alfa en el mip 3)
    float sP = 0, sN = 0;
    [loop] for (int i = 1; i <= 48; i++) { if (LA(uv + dirT * tx * (float)i) < 0.12) break; sP = (float)i; }
    [loop] for (int j = 1; j <= 48; j++) { if (LA(uv - dirT * tx * (float)j) < 0.12) break; sN = (float)j; }
    float aP = tLash.SampleLevel(sLin, uv + dirT * tx * max(sP, 2.0), 3).a, aN = tLash.SampleLevel(sLin, uv - dirT * tx * max(sN, 2.0), 3).a;
    float sgn = aP <= aN ? 1.0 : -1.0;                        // hacia la punta
    float steps = sgn > 0 ? sP : sN;
    if (steps < 1.0) continue;
    steps = max(steps, 3.0);                                   // las cortas de abajo: al menos 3 texeles
    float2 dUV = dirT * tx * sgn;                              // un texel hacia la punta
    float3 d = dPdu * dUV.x + dPdv * dUV.y;
    float mPerTex = length(d);
    if (mPerTex < 1e-8) continue;
    float3 n = normalize(v[0].nrm * b.x + v[1].nrm * b.y + v[2].nrm * b.z);
    d = normalize(d - n * dot(d, n));
    float3 root = P0 * b.x + P1 * b.y + P2 * b.z;
    if (dot(n, -root) < 0.0) n = -n;                           // la curva va hacia afuera (lado que mira a la camara)
    float len = steps * mPerTex * mLen * (0.9 + 0.2 * r3);
    float3 bt = normalize(cross(n, d));
    float widB = 0.00004 * mWid;                               // ~0.08 mm de ancho total en la raiz
    float4 tc = tLash.SampleLevel(sLin, uv, 0);
    float3 cR = tc.rgb, cT = tc.rgb * float3(1.15, 1.1, 1.05);
    { float layer = lerp(0.75, 1.05, frac(r3 * 5.71 + r2 * 2.13)); cR *= 0.72 * layer; cT *= layer; }   // (04/11 j) sombra entre pelos: raiz tapada + capas
    float4 cp = v[0].clipCP * b.x + v[1].clipCP * b.y + v[2].clipCP * b.z;
    float2 ww = v[0].ww * b.x + v[1].ww * b.y + v[2].ww * b.z;
    float4 q = cp / ww.xxyy;
    q = q * 0.5 + 0.5;
    q = q.zwxy * float4(1, -1, 1, -1) + float4(0, 1, 0, 1);
    float2 motion = q.zw - q.xy;
    float curl = 0.22 + 0.1 * r2;
    [unroll] for (uint ring = 0; ring <= PES_SEG; ring++) {
      float t = (float)ring / (float)PES_SEG;
      float3 p = root + d * len * t + n * len * curl * t * t;  // se curva hacia afuera hacia la punta
      float wid = widB * (1.0 - 0.8 * t);
      float pxV = 2.0 * abs(p.z) / (max(abs(m_projection[1].y), 1e-4) * vpH);
      wid = max(wid, pxV * (P75.z > 0.5 ? 1.25 : 0.4));
      float3 V = normalize(-p);
      float3 tanR = normalize(d + n * curl * 2.0 * t);
      float3 sideV = normalize(cross(tanR, V) + 1e-6);
      [unroll] for (int s = -1; s <= 1; s += 2) {
        GSOut o;
        float side = (float)s;
        o.pos = ProjV(p + sideV * wid * side);
        o.nrmV = normalize(n + bt * side * 0.6);
        o.col = cR; o.col2 = cT;
        o.tw = float2(t, side);
        o.motion = motion;
        o.viewV = V;
        o.tanV = tanR;
        outS.Append(o);
      }
    }
    outS.RestartStrip();
  }
}
