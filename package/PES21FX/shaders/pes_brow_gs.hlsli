// PES21 FX - cejas con hebras v2 (04/11): geometry shader. Las hebras salen de la MALLA de la cara, no de semillas que el
// shader de cara escribia cada cuadro (v1, 18/10: nacian solo donde la cara tenia pixeles -> aparecian y desaparecian con la
// distancia, y en el editor salian cintas gigantes). Ahora la DLL vuelve a dibujar el mismo dibujo de la cara (mismo VS del
// mod, mismos huesos, gestos y expresiones) con este GS: cada triangulo que cae en la zona de ceja del MAPA DE PELO (quinta
// textura, t26: R densidad, G largo, B direccion, A = 1 zona de ceja) suelta hebras en puntos FIJOS del triangulo
// (baricentricas por hash del numero de triangulo): las hebras quedan pegadas a la piel, siguen la frente y los gestos y no
// bailan entre cuadros. La direccion del mapa (en el plano UV) se lleva a 3D con el jacobiano UV -> posicion del triangulo.
// x74 on | y74 densidad (1..3) | z74 largo (cm) | w74 grosor (mm) | x75 elevacion | y75 distancia max (m) | z75 diagnostico
// w75 = -1 invierte la direccion | y77 = 1 tambien fuera del partido (editor, presentaciones)
// El archivo se incluye desde pes_brow_a_gs.hlsl (VS de la cabeza faab87ee) y pes_brow_b_gs.hlsl (VS de la cara animada
// 455eb97b): la entrada del GS tiene que ser IGUAL a la salida de cada VS (mismo orden de semanticas).
#define PES_SEG 4
#define PES_PER_INST 5      // hebras por instancia del GS: 5 x 10 vertices x 20 escalares = 1000 (tope 1024)
#define PES_INST 12         // instancias del GS por triangulo -> hasta 60 hebras por triangulo
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
cbuffer PESHair : register(b11)
{
  float4 pesHairA;      // largo_cm, rizo, rigidez, peso
  float4 pesHairB;      // grosor_mm, densidad, brillo, flequillo
  float4 pesHairRoot;   // color raiz rgb, w = 1 si esta definido
  float4 pesHairTip;    // color punta rgb, w = 1 si esta definido
  float4 pesHairBrow;   // ceja: grosor, densidad, elevacion, largo (multiplicadores)
  float4 pesHairMisc;   // x = 1 enlazado, y = 1 mapa de pelo propio del usuario
}
Texture1D<float4> IniParams : register(t120);
Texture2D<float4> tFace : register(t0);      // difusa de la cara (la misma del dibujo)
Texture2D<float4> tHairMap : register(t26);  // mapa de pelo de esta cara
SamplerState sLin : register(s0);

#ifdef PES_BROW_VS_B
struct FaceV {   // salida del VS 455eb97b (cara animada)
  float4 pos : SV_Position0;
  float4 c1 : COLOR0;
  float4 uv4 : TEXCOORD0;
  float4 o3 : TEXCOORD4;
  float4 o4 : TEXCOORD5;
  float4 o5 : TEXCOORD6;
  float4 nrm : TEXCOORD7;
  float4 clipCP : TEXCOORD8;    // xy = clip actual, zw = clip del cuadro anterior
  float2 ww : TEXCOORD9;        // x = w actual, y = w anterior
  float4 pR : TEXCOORD10;
  float4 pD : TEXCOORD11;
  float4 pV : TEXCOORD12;       // posicion en la vista
};
float2 FaceUV(FaceV v) { return v.uv4.xy; }
#else
struct FaceV {   // salida del VS faab87ee (cabeza)
  float4 pos : SV_Position0;
  float4 c1 : COLOR0;
  float2 uv : TEXCOORD0;
  float2 ww : TEXCOORD9;        // x = w actual, y = w anterior
  float4 o3 : TEXCOORD4;
  float4 o4 : TEXCOORD5;
  float4 o5 : TEXCOORD6;
  float4 nrm : TEXCOORD7;
  float4 clipCP : TEXCOORD8;    // xy = clip actual, zw = clip del cuadro anterior
  float4 pR : TEXCOORD10;
  float4 pD : TEXCOORD11;
  float4 pV : TEXCOORD12;       // posicion en la vista
};
float2 FaceUV(FaceV v) { return v.uv; }
#endif

struct GSOut {   // igual que la entrada de pes_brow_ps.hlsl
  float4 pos : SV_Position;
  float3 nrmV : TEXCOORD0;
  float3 col : TEXCOORD1;
  float2 tw : TEXCOORD2;
  float2 motion : TEXCOORD3;
  float3 viewV : TEXCOORD4;
  float3 col2 : TEXCOORD5;
};

uint PesHash(uint v) { uint s = v * 747796405u + 2891336453u; uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u; return (w >> 22u) ^ w; }
float PesRnd(inout uint s) { s = PesHash(s); return (float)(s & 0xffffff) / 16777216.0; }

float4 ProjV(float3 p) {
  float4 h = float4(p, 1);
  return float4(dot(h, m_projection[0]), dot(h, m_projection[1]), dot(h, m_projection[2]), dot(h, m_projection[3]));
}

[instance(PES_INST)]
[maxvertexcount(PES_PER_INST * (PES_SEG + 1) * 2)]
void main(triangle FaceV v[3], uint pid : SV_PrimitiveID, uint inst : SV_GSInstanceID, inout TriangleStream<GSOut> outS)
{
  float4 P74 = IniParams.Load(int2(74,0));
  float4 P75 = IniParams.Load(int2(75,0));
  if (P74.x < 0.5) return;
  float2 uv0 = FaceUV(v[0]), uv1 = FaceUV(v[1]), uv2 = FaceUV(v[2]);
  // 1) descarte rapido: solo triangulos que tocan la franja de las cejas del plano UV comun de las caras
  //    (arcos de pes_facegen_ps.hlsl: centros 0.395 / 0.605, y 0.312, radios 0.105 x 0.030, con margen)
  float2 lo = min(uv0, min(uv1, uv2)), hi = max(uv0, max(uv1, uv2));
  if (hi.x < 0.24 || lo.x > 0.76 || hi.y < 0.25 || lo.y > 0.38) return;
  // 2) tamano en pantalla (y75 = distancia maxima a zoom normal, medida en pixeles por metro como la v1)
  float3 P0 = v[0].pV.xyz, P1 = v[1].pV.xyz, P2 = v[2].pV.xyz;
  float zc = (P0.z + P1.z + P2.z) / 3.0;
  if (zc < 0.05) return;
  float vpH = m_viewportSize.y > 16.0 ? m_viewportSize.y : 1080.0;
  float ppm = abs(m_projection[1].y) * vpH * 0.5 / zc;
  float ppmMin = 1.73 * 540.0 / max(P75.y, 0.5);
  if (ppm < ppmMin && P75.z < 2.5) return;
  // 3) jacobiano UV -> posicion (en la vista) del triangulo
  float3 e1 = P1 - P0, e2 = P2 - P0;
  float2 d1 = uv1 - uv0, d2 = uv2 - uv0;
  float det = d1.x * d2.y - d2.x * d1.y;
  float uvArea = abs(det) * 0.5;
  if (uvArea < 1e-9) return;
  float3 dPdu = (e1 * d2.y - e2 * d1.y) / det;
  float3 dPdv = (e2 * d1.x - e1 * d2.x) / det;
  // hebras esperadas en este triangulo: densidad por unidad de area UV (y74 = 2 -> ~450 por ceja con el mapa tipico)
  bool hb = pesHairMisc.x > 0.5;
  float dens = clamp(P74.y, 0.25, 3.0) * 25000.0 * (hb ? max(pesHairBrow.y, 0.05) : 1.0);
  float keepBase = saturate(uvArea * dens / (float)(PES_INST * PES_PER_INST));
  float mLen = hb ? max(pesHairBrow.w, 0.05) : 1.0, mWid = hb ? max(pesHairBrow.x, 0.05) : 1.0, mLift = hb ? pesHairBrow.z : 1.0;
  float curl = hb ? saturate(pesHairA.y) : 0.1;
  float lift = clamp(P75.x * mLift, 0.0, 1.5) * 0.35;   // (04/11) pegadas a la piel: la v1 se levantaba demasiado
  float flip = P75.w < -0.5 ? -1.0 : 1.0;

  [loop] for (uint k = 0; k < PES_PER_INST; k++) {
    uint seed = PesHash(pid * 7919u + (inst * PES_PER_INST + k) * 104729u + 17u);
    float r0 = PesRnd(seed), r1 = PesRnd(seed), r2 = PesRnd(seed), r3 = PesRnd(seed), rk = PesRnd(seed);
    if (r0 + r1 > 1.0) { r0 = 1.0 - r0; r1 = 1.0 - r1; }
    float3 b = float3(1.0 - r0 - r1, r0, r1);
    float2 uv = uv0 * b.x + uv1 * b.y + uv2 * b.z;
    float4 m = tHairMap.SampleLevel(sLin, uv, 0);
    if (m.a < 0.97) continue;                      // fuera de la zona de ceja (0.9 = cuero cabelludo, 0 = nada)
    if (rk >= keepBase * saturate(m.r)) continue;  // densidad del mapa
    float3 root = P0 * b.x + P1 * b.y + P2 * b.z;
    float3 n = normalize(v[0].nrm.xyz * b.x + v[1].nrm.xyz * b.y + v[2].nrm.xyz * b.z);
    // direccion del peinado: angulo del mapa (B) en el plano UV, llevado a 3D y apoyado sobre la piel
    float ang = m.b * 6.2831853;
    float3 d = (dPdu * cos(ang) + dPdv * sin(ang)) * flip;
    d = d - n * dot(d, n);
    if (dot(d, d) < 1e-12) continue;
    d = normalize(d);
    float3 bt = normalize(cross(n, d));
    float ga = (r2 - 0.5) * 0.5;                    // giro individual de +-14 grados
    float3 dd = normalize(d * cos(ga) + bt * sin(ga));
    root += n * 0.0004;                             // apenas fuera de la piel (con la teselacion la piel sube algo)
    float len = max(P74.z, 0.1) * 0.01 * mLen * (0.55 + 0.45 * m.g) * (0.8 + 0.4 * r3);
    float widB = max(P74.w, 0.05) * 0.001 * mWid * 0.5;
    // color: el de la ceja pintada en ese punto (raiz) y la punta un poco mas clara, o el del tipo de pelo
    float3 painted = tFace.SampleLevel(sLin, uv, 2.0).rgb;
    float3 cR = (hb && pesHairRoot.w > 0.5) ? pesHairRoot.xyz : painted;
    float3 cT = (hb && pesHairTip.w > 0.5) ? pesHairTip.xyz : cR * float3(1.1, 1.05, 1.0);
    // vectores de movimiento de la raiz (misma cuenta que el PS de la cara: actual - anterior en UV de pantalla)
    float4 cp = v[0].clipCP * b.x + v[1].clipCP * b.y + v[2].clipCP * b.z;
    float2 ww = v[0].ww * b.x + v[1].ww * b.y + v[2].ww * b.z;
    float4 q = cp / ww.xxyy;
    q = q * 0.5 + 0.5;
    q = q.zwxy * float4(1, -1, 1, -1) + float4(0, 1, 0, 1);
    float2 motion = q.zw - q.xy;
    [unroll] for (uint ring = 0; ring <= PES_SEG; ring++) {
      float t = (float)ring / (float)PES_SEG;
      // sale levantada y se acuesta hacia la punta (pelo de ceja real); el rizo la tuerce en el plano de la piel
      float3 p = root + dd * len * t + n * len * lift * (0.35 * sin(t * 3.14159) + 0.1 * t) + bt * len * curl * 0.5 * sin(t * 6.2831853 + r3 * 6.2831853) * t;
      float wid = widB * (1.0 - 0.85 * t);
      float pxV = 2.0 * abs(p.z) / (max(abs(m_projection[1].y), 1e-4) * vpH);   // un pixel en la vista a esta distancia
      wid = max(wid, pxV * (P75.z > 0.5 ? 1.25 : 0.6));                         // nunca menos de ~1 px de ancho total
      float3 V = normalize(-p);
      float3 sideV = normalize(cross(dd, V) + 1e-6);
      [unroll] for (int s = -1; s <= 1; s += 2) {
        GSOut o;
        float side = (float)s;
        o.pos = ProjV(p + sideV * wid * side);
        float3 nCyl = normalize(V * 0.6 + sideV * side * 0.8);
        o.nrmV = normalize(lerp(n, nCyl, 0.65));
        o.col = cR; o.col2 = cT;
        o.tw = float2(t, side);
        o.motion = motion;
        o.viewV = V;
        outS.Append(o);
      }
    }
    outS.RestartStrip();
  }
}
