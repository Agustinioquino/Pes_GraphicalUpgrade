// PES21 FX - dispersion subsuperficial en pantalla con el perfil de Burley ("normalized diffusion", Christensen y Burley 2015,
// el de Unreal 5 / Disney). (27/10) Reemplaza a la version separable de d'Eon con sus tres parches de color: un disco 2D de 16
// muestras sobre la irradiancia difusa (lo que la pasada de luz escribio), SOLO en pixeles marcados como piel (material.z =
// 250/255). Las muestras salen por muestreo de importancia del propio perfil (mitad con el rojo, mitad con el verde, mezcla
// MIS), rotadas por pixel y por cuadro (el TAA del juego limpia el ruido). Solo corta la difusion lo que no es piel y lo que
// se aparta del plano de la superficie (no la normal: con poros y arrugas el corte por normal dejaba la piel manchada).
// El ancho en pantalla sale de la profundidad real (mm -> pixeles). La pasada vertical solo copia (pes_ssss_v_ps).
//   t0 irradiancia (entrada), t1 profundidad, t2 G-buffer material, t3 G-buffer normal, t120 parametros
//   x95 ancho (1 = escala real en mm), y95 fuerza, z95 corte por profundidad, w95 on, y100 guiada por el trm
cbuffer cPSScene : register(b2) {
  float4x4 m_projectionView; float4x4 m_projection; float4x4 m_view; float4x4 m_shadowProjection; float4x4 m_shadowProjection2;
  float4 m_eyepos; float4 m_projectionParam; float4 m_viewportSize; float4 m_exposure; float4 m_fogParam[3]; float4 m_fogColor;
  float4 m_cameraCenterOffset; float4 m_shadowMapResolutions;
};
cbuffer cPSSystem : register(b0) { float4 m_param; float4 m_renderInfo; float4 m_renderBuffer; float4 m_dominantLightDir; float4 m_timeParam; };
Texture2D<float4> inLight : register(t0);
Texture2D<float4> inDepth : register(t1);
Texture2D<float4> inMat   : register(t2);
Texture2D<float4> inNrm   : register(t3);
Texture2D<float4> inAlb   : register(t4);   // (20/10) albedo del G-buffer: tono de la piel (melanina)
Texture1D<float4> IniParams : register(t120);
SamplerState sPoint : register(s0);
SamplerState sLinear : register(s1);

float LinZ(float2 uv) { return m_projectionParam.z / (inDepth.SampleLevel(sPoint, uv, 0).x - m_projectionParam.w); }
bool IsSkin(float2 uv) { return abs(inMat.SampleLevel(sPoint, uv, 0).z - 0.98039) < 0.003; }

// radios del perfil de Burley en unidades de d: inversa de la CDF 1 - e^-x/4 - 3e^(-x/3)/4 en u = (i + 0.5) / 8
static const float kBurleyX[8] = { 0.1305, 0.4299, 0.7976, 1.2651, 1.8883, 2.7792, 4.2184, 7.4616 };

float BurleyF(float x) { return exp(-x) + exp(-x / 3.0); }

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
  float4 P = IniParams.Load(int2(95,0));
  float3 c0 = inLight.SampleLevel(sPoint, uv, 0).xyz;
#ifdef SSSS_COPY
  return float4(c0, 1);   // (27/10) el disco 2D se hace entero en la pasada horizontal
#else
  if (P.w < 0.5 || !IsSkin(uv)) return float4(c0, 1);
  float z0 = LinZ(uv);
  float2 rb = abs(float2(ddx(uv.x), ddy(uv.y)));   // tamano de un pixel en uv
  float pxm = 1.0 / max(2.0 * abs(m_projectionParam.x) * z0 * m_renderBuffer.z, 1e-5);   // pixeles por metro (como la composicion)
  // diagnostico: x95 >= 2.95 pinta la piel: verde = pasa por aca, con el ancho en pixeles como brillo
  if (P.x >= 2.95) return float4(0, saturate(6.0 * 0.001 * pxm / 40.0), 0, 1);
  float widthScale = (P.x > 0 ? P.x : 1.0);
  // trm (translucidez del juego, 6 bits en el G-buffer): lo fino (orejas, aletas, labios) difunde algo mas ancho, lo grueso algo menos
  float4 P2 = IniParams.Load(int2(100,0));
  uint tq = (uint)(inNrm.SampleLevel(sPoint, uv, 0).z * 1023.0 + 0.5);
  float thinK = smoothstep(0.08, 0.70, (float)(tq >> 4) / 63.0);
  widthScale *= lerp(1.0, lerp(0.85, 1.2, thinK), saturate(P2.y));
  // perfil por tono de piel (melanina del albedo): en piel clara el rojo viaja ~2.5 veces mas que el verde (penumbra rojiza);
  // con mas melanina la epidermis absorbe y el rojo viaja menos (penumbra marron calida, no roja). Distancias d en mm.
  float alb0L = dot(inAlb.SampleLevel(sPoint, uv, 0).xyz, float3(0.2126, 0.7152, 0.0722));
  float mel = (alb0L > 0.004) ? saturate(log2(0.47 / max(alb0L, 1e-3)) / 2.85) : 0.0;
  float3 dmm = float3(lerp(1.0, 0.62, mel), lerp(0.42, 0.36, mel), lerp(0.26, 0.24, mel)) * widthScale;
  float mmToPx = 0.001 * pxm;
  if (dmm.r * 3.0 * mmToPx < 0.6) return float4(c0, 1);   // de lejos lo resuelve la piel pre-integrada de la pasada de luz
  // plano de la superficie: pendiente de la profundidad por pixel (del lado mas suave, para no tomar un borde)
  float zxp = LinZ(uv + float2(rb.x, 0)) - z0, zxn = z0 - LinZ(uv - float2(rb.x, 0));
  float zyp = LinZ(uv + float2(0, rb.y)) - z0, zyn = z0 - LinZ(uv - float2(0, rb.y));
  float2 slope = float2(abs(zxp) < abs(zxn) ? zxp : zxn, abs(zyp) < abs(zyn) ? zyp : zyn);
  float tolK = P.z > 0.0 ? P.z : 1.0;
  // rotacion por pixel y por cuadro (ruido de gradiente intercalado, Jimenez 2014)
  float rot = 6.2831853 * frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715))) + frac(m_timeParam.z * 7.31));
  float3 acc = 0, wsum = 0;
  [unroll] for (int i = 0; i < 16; i++) {
    bool redSet = (i & 1) == 0;
    float rmm = kBurleyX[i >> 1] * (redSet ? dmm.r : dmm.g);
    float a = rot + i * 2.3999632;                       // angulo aureo: las 16 muestras no se alinean
    float2 off = float2(cos(a), sin(a)) * (rmm * mmToPx);   // en pixeles
    float2 uvS = uv + off * rb;
    if (!IsSkin(uvS)) continue;
    float zS = LinZ(uvS);
    float dzmm = abs(zS - (z0 + dot(slope, off))) * 1000.0;
    float tol = max(1.0, 0.5 * rmm) / tolK;               // la superficie puede curvarse mas lejos del centro
    float depthW = exp(-(dzmm * dzmm) / (tol * tol));
    // peso de cada canal = perfil / pdf de la mezcla (rojo + verde, mitad y mitad): el 1/r de los dos se cancela
    float pdf = 0.5 * BurleyF(rmm / dmm.r) / dmm.r + 0.5 * BurleyF(rmm / dmm.g) / dmm.g;
    float3 w = float3(BurleyF(rmm / dmm.r) / dmm.r, BurleyF(rmm / dmm.g) / dmm.g, BurleyF(rmm / dmm.b) / dmm.b) / max(pdf, 1e-6);
    w *= depthW;
    acc += w * inLight.SampleLevel(sLinear, uvS, 0).xyz;
    wsum += w;
  }
  // el pixel central entra con el peso de una muestra media: sin vecinos validos (borde) queda como estaba
  acc += c0; wsum += 1.0;
  float3 blurred = acc / wsum;
  return float4(lerp(c0, blurred, saturate(P.y)), 1);
#endif
}
