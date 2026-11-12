// PES21 FX - vidrio del menu Fisico (24/10, v2): geometry shader para los dibujos del menu (PS 55a003da con los VS fb68a5a0 /
// 1932d77d). Pasa los vertices sin cambios y, una vez por triangulo (no por pixel: la v1 lo hacia en cada pixel y bajaba
// mucho los cuadros), decide que es el elemento mirando su textura en una grilla de 5x5:
//   - panel translucido y parejo -> vidrio;  - panel opaco grande y parejo (textura entera) -> brillo en el borde;
//   - texto, iconos, fotos, degradados, sombras suaves -> igual que el juego.
// Tambien le pasa al PS el rectangulo del elemento en pantalla (NDC) para las esquinas, el borde y la refraccion.
// La DLL enlaza en el GS la textura del dibujo (t8 / s0, las del PS), la copia del fondo (t21, para el tamano de la pantalla)
// y los parametros (t120).  191: x opacidad de los paneles opacos | y tamano minimo para el brillo (px) | z tolerancia
Texture2D<float4> inTexture : register(t8);
SamplerState g_sampler : register(s0);
Texture2D<float4> GlassSharp : register(t21);
Texture1D<float4> IniParams : register(t120);

struct VSOut {
  float3 uv : TEXCOORD0;
  float4 mulc : TEXCOORD1;
  float4 addc : TEXCOORD2;
  float4 col : COLOR0;
  float4 pos : SV_Position;
};
struct GSOut {
  float3 uv : TEXCOORD0;
  float4 mulc : TEXCOORD1;
  float4 addc : TEXCOORD2;
  float4 col : COLOR0;
  float4 rect : TEXCOORD3;   // NDC: x min, y min, x max, y max
  float4 info : TEXCOORD4;   // x = 1 alineado | y = banderas (4 parejo, 8 translucido, 16 vidrio, 32 brillo, 64 textura entera,
                             // 128 rectangular, 256 suave) | z = alfa del vidrio (paneles opacos que se vuelven vidrio) | w -
  float4 pos : SV_Position;
};

float Max3(float3 v) { return max(v.x, max(v.y, v.z)); }
float4 Elem(float2 uv, float4 col, float4 mulc, float4 addc) { return inTexture.SampleLevel(g_sampler, uv, 0) * col * mulc + addc; }

[maxvertexcount(3)]
void main(triangle VSOut v[3], inout TriangleStream<GSOut> s) {
  float2 p0 = v[0].pos.xy / v[0].pos.w, p1 = v[1].pos.xy / v[1].pos.w, p2 = v[2].pos.xy / v[2].pos.w;
  float2 mn = min(p0, min(p1, p2)), mx = max(p0, max(p1, p2));
  float2 e1 = p1 - p0, e2 = p2 - p0;
  float area = abs(e1.x * e2.y - e1.y * e2.x) * 0.5;
  float bb = (mx.x - mn.x) * (mx.y - mn.y);
  float w0 = abs(v[0].pos.w);
  float persp = abs(v[0].pos.w - v[1].pos.w) + abs(v[0].pos.w - v[2].pos.w);
  bool aligned = bb > 1e-7 && abs(area - 0.5 * bb) < 0.01 * bb && persp < 1e-3 * max(w0, 1e-3);

  uint flags = 0; float glassA = 1.0;
  uint W = 0, H = 0; GlassSharp.GetDimensions(W, H);
  float2 szPx = (mx - mn) * 0.5 * float2(max(W, 1u), max(H, 1u));
  float minSz = min(szPx.x, szPx.y);
  float4 G2 = IniParams.Load(int2(191, 0));
  if (aligned && minSz >= 6.0 && W > 0) {
    // rectangulo de uv del quad (en un quad alineado, cada triangulo tiene las dos esquinas opuestas)
    float2 uvLo = min(v[0].uv.xy, min(v[1].uv.xy, v[2].uv.xy)), uvHi = max(v[0].uv.xy, max(v[1].uv.xy, v[2].uv.xy));
    float4 col = (v[0].col + v[1].col + v[2].col) / 3.0;
    float4 mulc = v[0].mulc, addc = v[0].addc;
    float4 g[25];
    [unroll] for (int gy = 0; gy < 5; gy++) {
      [unroll] for (int gx = 0; gx < 5; gx++) g[gy * 5 + gx] = Elem(lerp(uvLo, uvHi, (float2(gx, gy) + 0.5) * 0.2), col, mulc, addc);
    }
    float nIn = 0, aS = 0, adj = 0;
    [unroll] for (int n = 0; n < 25; n++) { if (g[n].a > 0.02) { nIn += 1.0; aS += g[n].a; } }
    [unroll] for (int ry = 0; ry < 5; ry++) {
      [unroll] for (int rx = 0; rx < 4; rx++) {
        float4 a0 = g[ry * 5 + rx], a1 = g[ry * 5 + rx + 1];
        if (a0.a > 0.02 && a1.a > 0.02) adj = max(adj, max(abs(a0.a - a1.a), Max3(abs(a0.rgb - a1.rgb))));
      }
    }
    [unroll] for (int cy = 0; cy < 4; cy++) {
      [unroll] for (int cx = 0; cx < 5; cx++) {
        float4 b0 = g[cy * 5 + cx], b1 = g[cy * 5 + cx + 5];
        if (b0.a > 0.02 && b1.a > 0.02) adj = max(adj, max(abs(b0.a - b1.a), Max3(abs(b0.rgb - b1.rgb))));
      }
    }
    float2 um = (uvLo + uvHi) * 0.5;
    float eA = max(max(Elem(float2(um.x, lerp(uvLo.y, uvHi.y, 0.03)), col, mulc, addc).a, Elem(float2(um.x, lerp(uvLo.y, uvHi.y, 0.97)), col, mulc, addc).a),
                   max(Elem(float2(lerp(uvLo.x, uvHi.x, 0.03), um.y), col, mulc, addc).a, Elem(float2(lerp(uvLo.x, uvHi.x, 0.97), um.y), col, mulc, addc).a));
    bool soft = g[12].a > 0.05 && eA < 0.5 * g[12].a;
    bool fullUV = uvLo.x < 0.02 && uvLo.y < 0.02 && uvHi.x > 0.98 && uvHi.y > 0.98;
    float aM = aS / max(nIn, 1.0);
    bool flat = adj < G2.z && !soft;
    bool rectish = g[0].a > 0.02 && g[4].a > 0.02 && g[20].a > 0.02 && g[24].a > 0.02;
    bool transl = aM < 0.93;
    float W2 = (float)max(W, 1u), H2 = (float)max(H, 1u);
    float2 rmin = float2(mn.x * 0.5 + 0.5, 0.5 - 0.5 * mx.y) * float2(W2, H2), rmax = float2(mx.x * 0.5 + 0.5, 0.5 - 0.5 * mn.y) * float2(W2, H2);
    int touch = (int)(rmin.x <= 1.5) + (int)(rmin.y <= 1.5) + (int)(rmax.x >= W2 - 1.5) + (int)(rmax.y >= H2 - 1.5);
    bool frost = flat && nIn >= 13.0 && transl;
    bool sheen = flat && nIn >= 25.0 && !transl && fullUV && minSz >= G2.y && touch < 2;
    if (sheen && G2.x < 0.93) { frost = true; sheen = false; glassA = G2.x; }
    flags = 2u | (flat ? 4u : 0u) | (transl ? 8u : 0u) | (frost ? 16u : 0u) | (sheen ? 32u : 0u) | (fullUV ? 64u : 0u) | (rectish ? 128u : 0u) | (soft ? 256u : 0u);
  }
  [unroll] for (int i = 0; i < 3; i++) {
    GSOut o;
    o.uv = v[i].uv; o.mulc = v[i].mulc; o.addc = v[i].addc; o.col = v[i].col;
    o.rect = float4(mn, mx);
    o.info = float4(aligned ? 1.0 : 0.0, (float)flags, glassA, 0);
    o.pos = v[i].pos;
    s.Append(o);
  }
  s.RestartStrip();
}
