// PES21 FX (11/11/2026, hilo del Motor en tiempo real) - VISTAS DEL MOTOR en la composicion diferida
// (619b86cb5691cdce = partido, f807e74d5b900a15 = editor). La maneja el "Motor en tiempo real" (modulo de Sider, por la
// memoria compartida de motorlink.cpp) y tambien el menu de la DLL (Diagnostico > Vista del motor).
//
// De donde sale: el Fox Engine trae un shader DeferredRenderingDebug (quedo en GrModelShaders_dx11.fsop) que es la composicion
// con una vista elegible; el juego de venta no tiene con que prenderlo (el interruptor es un 0 fijo en el codigo y los plugins
// de depuracion no estan en el exe). Aca se hacen las mismas cuentas sobre las mismas texturas del G-buffer que ya lee esta
// composicion, y se agregan sombra, solo luz y material.
//
// Fila 429 de IniParams (nunca se guarda en el ini):
//   x429 = vista: 0 imagen normal, 1 luz difusa, 2 luz especular, 3 normales (espacio de vista), 4 normales (mundo),
//          5 albedo (el color del material, sin luz), 6 profundidad (cerca claro, lejos oscuro, con bandas cada 20 cm),
//          7 oclusion ambiente, 8 sombra del sol, 9 solo luz (difusa x oclusion + especular, sin texturas), 10 material
//   y429 = alambre (no es de este archivo: lo hace la DLL con el estado de raster)
//   z429 = pantalla partida: 0 = la vista en toda la pantalla; 0.01..0.99 = la vista solo a la IZQUIERDA de esa fraccion
//          del ancho y la imagen normal a la derecha, con una linea en el corte
//   w429 = escala de la vista de profundidad (1 / metros hasta el negro); 0 = la que trae cada composicion
//
// Necesita declarados ANTES del include: IniParams, g_psSystem, g_psScene, g_samplerPoint_Clamp_s, inGBufferNormal,
// inGBufferMaterial, inAlbedo, inLightDiffuse, inLightSpecular, inDepth, inOcclusion, inShadow, PESDecN y PESLinZ.
// Se llama al FINAL de main: PESVistaMotor(o0, v0.xy, escala de profundidad por defecto).

void PESVistaMotor(inout float4 o0, float2 pix, float escalaZ)
{
  float4 VM = IniParams.Load(int2(429, 0));
  int m = (int)(VM.x + 0.5);
  if (m < 1 || m > 10) return;
  float2 uv = g_psSystem.m_renderBuffer.zw * pix;
  if (VM.z > 0.005 && VM.z < 0.995) {
    float dx = uv.x - VM.z;
    if (abs(dx) < g_psSystem.m_renderBuffer.z) { o0.xyz = float3(1.0, 0.85, 0.2); return; }
    if (dx > 0.0) return;
  }
  float3 c = float3(0.0, 0.0, 0.0);
  float3 Ld = inLightDiffuse.SampleLevel(g_samplerPoint_Clamp_s, uv, 0).xyz;
  float3 Ls = inLightSpecular.SampleLevel(g_samplerPoint_Clamp_s, uv, 0).xyz;
  float occ = inOcclusion.SampleLevel(g_samplerPoint_Clamp_s, uv, 0).x;
  if (m == 1) c = Ld;
  else if (m == 2) c = Ls;
  else if (m == 3) c = PESDecN(uv) * 0.5 + 0.5;
  else if (m == 4) {
    float3 nv = PESDecN(uv); nv.z = -nv.z;
    c = mul(nv, (float3x3)g_psScene.m_view) * 0.5 + 0.5;
  }
  else if (m == 5) c = inAlbedo.SampleLevel(g_samplerPoint_Clamp_s, uv, 0).xyz;
  else if (m == 6) {
    float z = PESLinZ(uv);
    float k = VM.w > 0.0 ? VM.w : escalaZ;
    float g = (z > 0.0) ? saturate(1.0 - z * k) : 0.0;
    c = g * (0.6 + 0.4 * frac(z * 5.0));
  }
  else if (m == 7) c = occ;
  else if (m == 8) c = inShadow.SampleLevel(g_samplerPoint_Clamp_s, uv, 0).x;
  else if (m == 9) c = Ld * occ + Ls;
  else c = inGBufferMaterial.SampleLevel(g_samplerPoint_Clamp_s, uv, 0).xyz;
  o0.xyz = max(c, float3(0.0, 0.0, 0.0));
}
