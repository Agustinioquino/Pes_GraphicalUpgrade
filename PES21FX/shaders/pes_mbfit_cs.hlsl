// PES21 FX (13/11 tanda 2, D11) - ajuste de la camara del desenfoque de movimiento (24943e559ce781b9) UNA vez por cuadro.
// Con y449 / z449 cada pixel de la composicion del desenfoque repetia el mismo ajuste de 12 puntos (24 lecturas y unas 300 operaciones por
// pixel, 0.1-0.8 ms en los paneos). La DLL (hooks.cpp, MBFitBeforeDraw) corre esto en un solo hilo justo antes de ese dibujo, en el mismo
// contexto y con sus mismas entradas (t1 velocidad, s8 muestreador puntual, b0 cPSSystem), y el PS lee el resultado en t3. Es el mismo
// codigo del PS, sin cambios. Salida (u0, 2 x 1 RGBA32F): texel 0 = (cT.xy, cAx.xy), texel 1 = (cAy.xy, S = suma de pesos, 4242 = valido;
// un numero raro para que una textura vieja del juego que quede en t3 nunca pase por valida).
cbuffer cPSSystem : register(b0)
{
  struct
  {
    float4 m_param;
    float4 m_renderInfo;
    float4 m_renderBuffer;
    float4 m_dominantLightDir;
    float4 m_timeParam;
  } g_psSystem : packoffset(c0);
}

SamplerState g_samplerPoint_Wrap_s : register(s8);
Texture2D<float4> g_velocityBuffer : register(t1);
RWTexture2D<float4> PesMBFitOut : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
  float2 px = g_psSystem.m_renderBuffer.zw;
  // movimiento de la camara = ajuste afin de la velocidad en una grilla de 12 puntos (4 x 3) de la pantalla: v(d) = t + ax * d.x + ay * d.y
  // (d desde el centro, en altos de pantalla). Primero el ajuste cerrado (la grilla es simetrica), despues una pasada robusta (los puntos que
  // caen en jugadores o en la pelota pesan poco) con la inversa adjunta de la matriz 3x3
  float aspX = g_psSystem.m_renderBuffer.x * px.y;
  float2 mbV[12];
  float2 sV = 0.0, sVx = 0.0, sVy = 0.0;
  [unroll] for (uint gi = 0; gi < 12; gi++) {
    float2 gU = float2(0.125 + 0.25 * (float)(gi % 4u), 0.2 + 0.3 * (float)(gi / 4u));
    float2 gD = (gU - 0.5) * float2(aspX, 1.0);
    mbV[gi] = g_velocityBuffer.SampleLevel(g_samplerPoint_Wrap_s, gU, 0).xy;
    sV += mbV[gi]; sVx += mbV[gi] * gD.x; sVy += mbV[gi] * gD.y;
  }
  float2 cT = sV / 12.0;
  float2 cAx = sVx / max(0.9375 * aspX * aspX, 1e-4);   // suma de d.x^2 de la grilla
  float2 cAy = sVy / 0.72;                              // suma de d.y^2
  float S = 0.0, Sx = 0.0, Sy = 0.0, Sxx = 0.0, Sxy = 0.0, Syy = 0.0;
  float2 B0 = 0.0, B1 = 0.0, B2 = 0.0;
  [unroll] for (uint gj = 0; gj < 12; gj++) {
    float2 gU = float2(0.125 + 0.25 * (float)(gj % 4u), 0.2 + 0.3 * (float)(gj / 4u));
    float2 gD = (gU - 0.5) * float2(aspX, 1.0);
    float2 pr = cT + cAx * gD.x + cAy * gD.y;
    float rr = length(mbV[gj] - pr) / (1.0 + 0.25 * length(pr));
    float wg = 1.0 / (1.0 + rr * rr);
    S += wg; Sx += wg * gD.x; Sy += wg * gD.y;
    Sxx += wg * gD.x * gD.x; Sxy += wg * gD.x * gD.y; Syy += wg * gD.y * gD.y;
    B0 += wg * mbV[gj]; B1 += (wg * gD.x) * mbV[gj]; B2 += (wg * gD.y) * mbV[gj];
  }
  float c00 = Sxx * Syy - Sxy * Sxy, c01 = Sxy * Sy - Sx * Syy, c02 = Sx * Sxy - Sxx * Sy;
  float c11 = S * Syy - Sy * Sy, c12 = Sx * Sy - S * Sxy, c22 = S * Sxx - Sx * Sx;
  float det = S * c00 + Sx * c01 + Sy * c02;
  if (det > 1e-4) {   // si no, queda el ajuste cerrado
    float iD = 1.0 / det;
    cT  = (c00 * B0 + c01 * B1 + c02 * B2) * iD;
    cAx = (c01 * B0 + c11 * B1 + c12 * B2) * iD;
    cAy = (c02 * B0 + c12 * B1 + c22 * B2) * iD;
  }
  PesMBFitOut[uint2(0, 0)] = float4(cT, cAx);
  PesMBFitOut[uint2(1, 0)] = float4(cAy, S, 4242.0);
}
