// (13/11, Agustin) SUDOR 2 (x457): sudor POR JUGADOR que reemplaza al del juego y al reloj unico de antes, y LLUVIA aparte.
//   Sudor: sale de adentro. Primero un brillo fino que arranca en la frente, la nariz y las sienes (mapa de zonas), despues
//          gotitas chicas quietas (perlado) y, con mucho sudor, alguna gota que resbala despacio. Depende de cuanto corrio ESTE
//          jugador, del clima y del tiempo (lo calcula la DLL, sudor.cpp: filas 460..492).
//   Lluvia: viene de afuera. Moja parejo todo lo que mira hacia arriba, con gotas mas grandes que chorrean; no depende del
//          cansancio ni de las zonas.
// Filas: 457 x on, y cantidad, z gotas, w velocidad | 458 x secado, y lluvia en la piel, z gotas de lluvia, w camiseta |
//        459 x sudor de prueba, y lluvia de prueba, z diagnostico (1 sudor / calor, 2 zonas) | 460 cabecera, 461.. = (x, z, sudor, calor)
// Antes de incluir: IniParams y g_psSystem declarados. PES_SW_HAS_SCENE = el shader ya declara cPSScene (b2).
#ifndef PES_SWEAT2_HLSLI
#define PES_SWEAT2_HLSLI
#ifndef PES_SW_HAS_SCENE
cbuffer cPSScene : register(b2)
{
  struct
  {
    float4x4 m_projectionView;
    float4x4 m_projection;
    float4x4 m_view;
    float4x4 m_shadowProjection;
    float4x4 m_shadowProjection2;
    float4 m_eyepos;
    float4 m_projectionParam;
    float4 m_viewportSize;
    float4 m_exposure;
    float4 m_fogParam[3];
    float4 m_fogColor;
    float4 m_cameraCenterOffset;
    float4 m_shadowMapResolutions;
  } g_psScene : packoffset(c0);
}
#endif

bool PesSwOn() { return IniParams.Load(int2(457, 0)).x > 0.5; }

// posicion en el mundo de este pixel: direccion de vista (TEXCOORD4 del VS = -normalize(posicion en vista)) x profundidad lineal
// de SV_Position, y la inversa de m_view (rigida). ok = 0 si b2 no esta enlazado
float3 PesSwWorld(float4 svPos, float3 toEye, out float ok) {
  float4x4 V = g_psScene.m_view;
  ok = dot(V._m00_m10_m20, V._m00_m10_m20) > 0.5 ? 1.0 : 0.0;
  float3 dirV = -normalize(toEye);
  float zA = abs(g_psScene.m_projectionParam.z / (svPos.z - g_psScene.m_projectionParam.w));
  float3 pv = dirV * (zA / max(abs(dirV.z), 1e-3));
  float3 d = pv - float3(V._m30, V._m31, V._m32);
  return float3(dot(d, float3(V._m00, V._m01, V._m02)), dot(d, float3(V._m10, V._m11, V._m12)), dot(d, float3(V._m20, V._m21, V._m22)));
}

// (sudor 0..1, calor 0..1, lluvia 0..1, 1 si encontro a su jugador). La tabla viene ordenada desde la camara: en un primer plano
// el jugador esta en las primeras filas y el bucle corta enseguida. Sin jugador cerca (o sin b2): el sudor medio
float4 PesSwPlayer(float4 svPos, float3 toEye) {
  float4 T = IniParams.Load(int2(459, 0));
  float4 H = IniParams.Load(int2(460, 0));
  float sw = H.w, heat = 0.3 * H.w, hit = 0.0;
  if (H.y > 0.5 && H.x > 0.5) {
    float ok; float3 wp = PesSwWorld(svPos, toEye, ok);
    if (ok > 0.5) {
      int n = min((int)(H.x + 0.5), 32);
      float bd = 1.21;   // 1.1 m en el piso
      [loop] for (int i = 0; i < n; i++) {
        float4 E = IniParams.Load(int2(461 + i, 0));
        float2 dd = wp.xz - E.xy;
        float d2 = dot(dd, dd);
        if (d2 < bd) { bd = d2; sw = E.z; heat = E.w; hit = 1.0; if (d2 < 0.2) break; }
      }
    }
  }
  float rain = H.z;
  if (T.x > 0.001) { sw = saturate(T.x); heat = saturate(T.x); }   // sudor de prueba (tambien en el editor)
  if (T.y > 0.001) rain = saturate(T.y);
  return float4(saturate(sw), saturate(heat), saturate(rain), hit);
}

float psw_h(float2 p) { p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }
float psw_n(float2 p) { float2 i = floor(p), f = frac(p); f = f * f * (3.0 - 2.0 * f);
  return lerp(lerp(psw_h(i), psw_h(i + float2(1, 0)), f.x), lerp(psw_h(i + float2(0, 1)), psw_h(i + 1.0), f.x), f.y); }

// una capa de gotas en celdas de la UV. amount = fraccion de celdas con gota; size = radio relativo; slideK = cuanto resbalan
// (0 quietas); fp = tamano del pixel en celdas (antialiasing: la gota de menos de ~1 pixel se apaga y su humedad media la suma el
// que llama). Devuelve xy = gradiente para la normal, z = mojado, w = cuanto se resolvio (0..1)
float4 PesSwDrops(float2 uv, float cells, float amount, float size, float t, float slideK, float streak, float fp) {
  float2 g = uv * cells, c = floor(g), f = frac(g);
  float r0 = psw_h(c);
  float sz = size * (0.55 + 0.6 * psw_h(c + 73.3));
  float px = 0.16 * sz / max(fp, 1e-5);
  float k = saturate((px - 0.8) / 1.6);
  if (r0 > amount || k <= 0.0) return float4(0, 0, 0, k);
  float r1 = psw_h(c + 17.1), r2 = psw_h(c + 41.7);
  // nace de a poco: la celda recien habilitada tiene una gota chica (sin saltos al subir amount)
  float born = saturate((amount - r0) / max(amount, 1e-3) * 4.0);
  float2 pos = float2(0.3 + 0.4 * r1, 0.35 + 0.3 * r2);
  float slide = 0.0;
  if (slideK > 0.0) {   // se queda quieta un rato largo y despues resbala (ciclo propio de cada gota)
    float cyc = frac(r2 + t * (0.03 + 0.05 * r1) * slideK);
    slide = smoothstep(0.6, 1.0, cyc);
    pos.y = lerp(0.18, 0.92, slide);
    pos.x += sin(cyc * 15.0 + r1 * 6.28) * 0.03 * slide;
    born *= 1.0 - smoothstep(0.93, 1.0, cyc);   // al final del recorrido se pierde
  }
  float2 rad = float2(0.16, 0.17 + 0.09 * slide) * sz * (0.45 + 0.55 * born);
  float2 d = (f - pos) / rad;
  float d2 = dot(d, d);
  float h = saturate(1.0 - d2);
  float2 grad = d2 < 1.0 ? (-2.0 * d / rad) * 0.04 * h : 0.0;   // casquete: pendiente que se apaga en el borde (sin canto duro)
  float wet = smoothstep(0.0, 0.25, h);
  if (streak > 0.0 && slide > 0.0) {   // estela fina por encima de la gota que resbala
    float dx = abs(f.x - pos.x);
    float tr = (f.y < pos.y && f.y > 0.08) ? saturate(1.0 - dx / (0.03 * sz)) * slide * streak * saturate((f.y - 0.08) * 6.0) : 0.0;
    wet = max(wet, tr * 0.7);
    grad.x += (f.x > pos.x ? -1.0 : 1.0) * tr * 0.18;
  }
  return float4(grad * k, wet * k * born, k);
}

struct PesSwOut {
  float film;      // pelicula humeda 0..1 (baja la rugosidad, sube apenas el brillo)
  float drop;      // gota 0..1 (casi espejo)
  float2 grad;     // relieve de las gotas para la normal (espacio tangente)
  float flush;     // calor del jugador 0..1 (rubor)
  float sweat;     // sudor del jugador 0..1 (crudo)
  float rain;      // lluvia sobre este pixel 0..1
};

// PIEL. zone = propension de la zona (mapa de zonas R: frente / nariz / sienes 1, mandibula 0.2; 0.6 si no hay mapa);
// upK = cuanto mira hacia arriba la superficie (-1..1); fpUv = tamano del pixel en UV; scaleUv = cuantas veces entra una cabeza
// (30 cm) en la UV 0..1 (1 en la cara; en brazos y piernas se pasa lo que corresponda)
PesSwOut PesSwSkin(float4 P, float2 uv, float zone, float upK, float fpUv, float scaleUv, float tSec, bool drops) {
  PesSwOut o; o.film = 0; o.drop = 0; o.grad = 0; o.flush = P.y; o.sweat = P.x; o.rain = 0;
  float4 A = IniParams.Load(int2(457, 0));
  float4 B = IniParams.Load(int2(458, 0));
  float amt = A.y > 0.0 ? min(A.y, 2.0) : 1.0, dropK = drops ? saturate(A.z) : 0.0;
  // --- sudor ---
  // cada zona arranca a su tiempo: la frente con 0.08 de sudor, la mandibula recien con 0.45
  float start = lerp(0.50, 0.06, saturate(zone));
  float loc = saturate((P.x * amt - start) / 0.40) * lerp(0.55, 1.0, saturate(zone));
  // la pelicula no es pareja: primero aparece en manchitas (poros que ya largaron), despues se junta
  float patch = psw_n(uv * 90.0 * scaleUv) * 0.6 + psw_n(uv * 310.0 * scaleUv) * 0.4;
  float film = saturate(loc * 1.6 - patch * (1.0 - loc) * 1.1);
  float2 grad = 0.0; float drop = 0.0;
  float dAmt = saturate((loc - 0.35) / 0.65) * dropK;
  if (dAmt > 0.001) {
    // perlado: gotitas de ~0.6 y ~0.35 mm quietas; alguna gota grande que resbala despacio solo con mucho sudor
    float c1 = 230.0 * scaleUv, c2 = 420.0 * scaleUv, c3 = 60.0 * scaleUv;
    float4 a = PesSwDrops(uv, c1, dAmt * 0.55, 1.0, tSec, 0.0, 0.0, fpUv * c1);
    float4 b = PesSwDrops(uv + 0.37, c2, dAmt * 0.45, 0.9, tSec, 0.0, 0.0, fpUv * c2);
    float4 c = PesSwDrops(uv + 0.71, c3, saturate((loc - 0.75) * 4.0) * 0.16 * dropK, 1.3, tSec, 1.0, 0.6, fpUv * c3);
    grad = a.xy + b.xy * 0.7 + c.xy;
    drop = max(max(a.z, b.z * 0.9), c.z);
    // lo que no se resolvio (lejos): su humedad media va a la pelicula, pareja
    film = saturate(film + dAmt * (0.10 * (1.0 - a.w) + 0.06 * (1.0 - b.w)));
  }
  // --- lluvia ---
  float rainK = P.z * saturate(B.y);
  if (rainK > 0.001) {
    float up = saturate(upK * 0.75 + 0.45);                    // arriba 1, vertical 0.45, hacia abajo casi 0
    float rf = rainK * lerp(0.45, 1.0, up);
    float c4 = 42.0 * scaleUv, c5 = 95.0 * scaleUv;
    float rd = drops ? saturate(B.z) * rainK : 0.0;
    float4 e = float4(0, 0, 0, 1), g = float4(0, 0, 0, 1);
    if (rd > 0.001) {
    e = PesSwDrops(uv + 0.13, c4, rd * 0.42 * lerp(0.5, 1.0, up), 1.25, tSec, 2.2, 1.0, fpUv * c4);
    g = PesSwDrops(uv + 0.59, c5, rd * 0.50, 1.0, tSec, 0.0, 0.0, fpUv * c5);
    }
    grad += e.xy * 1.3 + g.xy;
    drop = max(drop, max(e.z, g.z));
    film = max(film, saturate(rf * 0.85 + rd * (0.12 * (1.0 - e.w) + 0.08 * (1.0 - g.w))));
    o.rain = rf;
  }
  o.film = film; o.drop = saturate(drop); o.grad = grad;
  return o;
}

// CAMISETA. uvK = UV del kit (plantilla Konami: frente a la izquierda, espalda a la derecha). Devuelve cuanto esta mojada la
// tela 0..1 en este pixel: la mancha nace en el centro de la espalda, el pecho y las axilas y CRECE con el sudor del jugador
// (borde irregular); la lluvia la moja entera, mas en los hombros
float PesSwShirt(float4 P, float2 uvK, float upK, out float zoneDbg) {
  float4 B = IniParams.Load(int2(458, 0));
  float k = saturate(B.w);
  float s = saturate((P.x - 0.22) / 0.78);                      // la tela tarda mas que la piel en marcarse
  float grow = lerp(0.45, 1.35, s);
  float nz = (psw_n(uvK * 22.0) - 0.5) * 0.22 + (psw_n(uvK * 61.0) - 0.5) * 0.10;
  float m = 0.0;
  m = max(m, 1.0 - length((uvK - float2(0.75, 0.30)) / (float2(0.075, 0.21) * grow)));          // espalda (columna)
  m = max(m, 0.85 * (1.0 - length((uvK - float2(0.25, 0.24)) / (float2(0.065, 0.13) * grow)))); // pecho
  m = max(m, 0.95 * (1.0 - length((uvK - float2(0.08, 0.16)) / (float2(0.05, 0.065) * grow)))); // axilas
  m = max(m, 0.95 * (1.0 - length((uvK - float2(0.42, 0.16)) / (float2(0.05, 0.065) * grow))));
  zoneDbg = saturate(m);
  float wet = smoothstep(0.0, 0.30, m + nz) * saturate(s * 2.5);
  float rain = P.z * saturate(B.y) * lerp(0.55, 1.0, saturate(upK * 0.75 + 0.45));
  return saturate(max(wet * k, rain));
}
#endif
