// PES21 FX - hebras de ceja (18/10): vertex shader. Una instancia por semilla (celda de la grilla 256x256 de la
// quinta textura); la semilla la escribio el shader de cara en este mismo cuadro (posicion y normal en espacio de
// vista, direccion de crecimiento, color). Cada hebra es una cinta de PES_SEG segmentos orientada a la camara.
// x74 on | y74 densidad (hebras por semilla, 1..3) | z74 largo (cm) | w74 grosor (mm)  |  x75 elevacion | y75 distancia max (m) | z75 diagnostico
#define PES_SEG 4
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
// PES21 (18/10) tipo de pelo de ESTA cara (PES21FX_hair.ini, lo enlaza la DLL en b11; si no esta enlazado todo vale 0 y pesHairMisc.x = 0)
cbuffer PESHair : register(b11)
{
  float4 pesHairA;      // largo_cm, rizo, rigidez, peso
  float4 pesHairB;      // grosor_mm, densidad, brillo, flequillo
  float4 pesHairRoot;   // color raiz rgb, w = 1 si esta definido
  float4 pesHairTip;    // color punta rgb, w = 1 si esta definido
  float4 pesHairBrow;   // ceja: grosor, densidad, elevacion, largo (multiplicadores)
  float4 pesHairMisc;   // x = 1 enlazado, y = 1 mapa de pelo propio del usuario
}
struct Seed { float4 posV; float4 nrmV; float4 dirV; float4 col; };   // posV.w = 1 valida; dirV.w = largo 0..1; col.w = motion packed
StructuredBuffer<Seed> gSeeds : register(t30);
Texture1D<float4> IniParams : register(t120);

float h21(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }

struct VSOut {
  float4 pos : SV_Position;
  float3 nrmV : TEXCOORD0;
  float3 col : TEXCOORD1;
  float2 tw : TEXCOORD2;     // t a lo largo (0 raiz .. 1 punta), u a lo ancho (-1..1)
  float2 motion : TEXCOORD3;
  float3 viewV : TEXCOORD4;
  float3 col2 : TEXCOORD5;   // color de la punta (tipo de pelo)
};

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
  VSOut o = (VSOut)0;
  float4 P74 = IniParams.Load(int2(74,0));
  float4 P75 = IniParams.Load(int2(75,0));
  uint perSeed = (uint)clamp(P74.y, 1.0, 3.0);
  uint sIdx = iid / perSeed, sub = iid % perSeed;
  Seed s = gSeeds[sIdx];
  float3 viewDirFwd = float3(0, 0, 1);
  // diagnostico 3 (18/10): "prueba de dibujo": sin semillas, una grilla de hebras a 6 m delante de la camara.
  // Si esto no se ve, falla el dibujo (shaders/estado); si se ve, fallan las semillas que escribe la cara.
  if (P75.z > 2.5) {
    if (sIdx >= 4096) { o.pos = float4(0, 0, -2, 1); return o; }
    uint gx = sIdx % 64, gy = sIdx / 64;
    s.posV = float4((gx - 32.0) * 0.05, (gy - 32.0) * 0.05, 6.0, 1.0);   // 18/10 b: a 6 m (el plano cercano de las repeticiones llega a 3 m)
    s.nrmV = float4(0, 0, -1, 0);
    s.dirV = float4(1, 0.3, 0, 1.0);
    s.col = float4(1, 0, 1, 0);
  }
  if (P74.x < 0.5 || s.posV.w < 0.5 || s.posV.z > max(P75.y, 0.5)) { o.pos = float4(0, 0, -2, 1); return o; }   // fuera de pantalla
  // geometria de la cinta: 2 vertices por anillo, (PES_SEG+1) anillos -> triangulos en lista: 6 por segmento
  uint seg = vid / 6, k = vid % 6;
  // k: 0=(seg,L) 1=(seg,R) 2=(seg+1,L) 3=(seg+1,L) 4=(seg,R) 5=(seg+1,R)
  uint ring = seg + ((k == 2 || k == 3 || k == 5) ? 1 : 0);
  float side = (k == 1 || k == 4 || k == 5) ? 1.0 : -1.0;
  float t = (float)ring / (float)PES_SEG;
  // variacion por hebra
  float2 hs = float2(sIdx * 0.731 + sub * 17.3, sub * 3.1 + 0.5);
  float r1 = h21(hs), r2 = h21(hs + 2.3), r3 = h21(hs + 7.7);
  float3 n = normalize(s.nrmV.xyz);
  float3 d = s.dirV.xyz - n * dot(s.dirV.xyz, n);
  d = normalize(d + 1e-5);
  // separacion de las hebras extra dentro de la celda (en el plano de la piel)
  float3 bt = normalize(cross(n, d));
  float3 root = s.posV.xyz + n * 0.0003 + (bt * (r1 - 0.5) + d * (r2 - 0.5)) * 0.0022 * (sub > 0 ? 1.0 : 0.3);
  // giro individual de +-18 grados en el plano de la piel
  float ang = (r3 - 0.5) * 0.63;
  float3 dd = normalize(d * cos(ang) + bt * sin(ang));
  // tipo de pelo de esta cara (PES21FX_hair.ini): multiplicadores de largo, grosor, elevacion y rizo
  bool hb = pesHairMisc.x > 0.5;
  bool scalp = s.nrmV.w > 0.5;   // (19/10) semilla de cuero cabelludo: usa los parametros de PELO del tipo (largo cm, rizo, rigidez, peso)
  float4 P76 = IniParams.Load(int2(99,0));   // x99 on, y99 densidad, z99 largo (x), w99 caida (peso)
  float mLen = hb ? max(pesHairBrow.w, 0.05) : 1.0, mWid = hb ? max(pesHairBrow.x, 0.05) : 1.0, mLift = hb ? pesHairBrow.z : 1.0, curl = hb ? saturate(pesHairA.y) : 0.1;
  float len = max(P74.z, 0.1) * 0.01 * mLen * (0.55 + 0.45 * s.dirV.w) * (0.8 + 0.4 * r2);
  float lift = clamp(P75.x * mLift, 0.0, 1.5);
  float3 p; float wid;
  if (!scalp) {
    // curva: sale levantada y se acuesta hacia la punta (como el pelo de ceja real); el rizo tuerce la hebra en el plano de la piel
    p = root + dd * len * t + n * len * lift * (0.35 * sin(t * 3.14159) + 0.1 * t) + bt * len * curl * 0.5 * sin(t * 6.2831853 + r3 * 6.2831853) * t;
    wid = max(P74.w, 0.05) * 0.001 * mWid * (1.0 - 0.85 * t) * 0.5;
  } else {
    // PELO: largo en cm del tipo (3.5 por defecto), sale de la piel segun la rigidez y CAE con el peso hacia abajo (eje Y del
    // mundo en espacio de vista = segunda columna de m_view), con rizo en espiral. De largo relativo por celda (mapa de pelo).
    float lenH = (hb ? max(pesHairA.x, 0.5) : 3.5) * 0.01 * max(P76.z, 0.05) * (0.6 + 0.4 * s.dirV.w) * (0.85 + 0.3 * r2);
    float stiff = hb ? saturate(pesHairA.z) : 0.8, weight = (hb ? saturate(pesHairA.w) : 0.5) * saturate(P76.w);
    float curlH = hb ? saturate(pesHairA.y) : 0.1;
    float3 downV = -normalize(float3(m_view[0].y, m_view[1].y, m_view[2].y));
    // direccion inicial: levantada de la piel (rigidez) + peinado; con el tiempo t cae hacia abajo (peso)
    float3 d0 = normalize(dd + n * (0.25 + 0.6 * stiff));
    float3 dT = normalize(lerp(d0, downV, saturate(weight * t * 1.4)));
    float3 dMid = normalize(d0 + dT);
    // integracion simple en 3 tramos (raiz -> medio -> punta) para que la curva sea suave
    float t1 = saturate(t * 2.0), t2 = saturate(t * 2.0 - 1.0);
    p = root + d0 * lenH * 0.5 * min(t1, 1.0) + dMid * lenH * 0.5 * t2;
    float3 btH = normalize(cross(n, dT) + 1e-5);
    p += (btH * cos(t * 9.0 + r3 * 6.28) + n * sin(t * 9.0 + r3 * 6.28)) * lenH * curlH * 0.18 * t;
    wid = (hb ? max(pesHairB.x, 0.02) : 0.08) * 0.001 * (1.0 - 0.6 * t) * 0.5;
  }
  // ancho minimo en pantalla: una hebra de 0.35 mm a 1 m es menos de medio pixel y el rasterizador no la cubre nunca.
  // Se garantiza ~1.2 px de ancho (2.5 px en diagnostico); la punta se afina en el pixel shader por descarte.
  float vpH = m_viewportSize.y > 16.0 ? m_viewportSize.y : 1080.0;
  float pxV = 2.0 * abs(p.z) / (max(abs(m_projection[1].y), 1e-4) * vpH);   // tamano de un pixel en espacio de vista a esta distancia
  wid = max(wid, pxV * (P75.z > 0.5 ? 1.25 : 0.6));
  float3 V = normalize(-p);                              // hacia la camara (espacio de vista: camara en el origen)
  float3 sideV = normalize(cross(dd, V));
  p += sideV * wid * side;
  float4 clip = float4(dot(float4(p, 1), m_projection[0]), dot(float4(p, 1), m_projection[1]), dot(float4(p, 1), m_projection[2]), dot(float4(p, 1), m_projection[3]));
  o.pos = clip;
  // normal: mezcla de la normal de la piel (raiz) con una normal cilindrica de la hebra
  float3 nCyl = normalize(V * 0.6 + sideV * side * 0.8);
  o.nrmV = normalize(lerp(n, nCyl, 0.65));
  o.col = (hb && pesHairRoot.w > 0.5) ? pesHairRoot.xyz : (scalp ? s.col.xyz * 0.85 : s.col.xyz);
  o.col2 = (hb && pesHairTip.w > 0.5) ? pesHairTip.xyz : o.col * float3(1.1, 1.05, 1.0);
  o.tw = float2(t, side);
  // movimiento: el de la raiz (lo guardo el shader de cara)
  uint mp = asuint(s.col.w);
  o.motion = float2(f16tof32(mp & 0xffff), f16tof32(mp >> 16));
  o.viewV = V;
  return o;
}
