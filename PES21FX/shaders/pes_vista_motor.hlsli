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
//          5 albedo (el color del material, sin luz), 6 profundidad (z buffer en grises: cerca claro, lejos oscuro, escala logaritmica),
//          7 oclusion ambiente, 8 sombra del sol, 9 solo luz (difusa x oclusion + especular, sin texturas), 10 material,
//          11 ESTUDIO (17/11): el color del material con una luz de estudio propia del Motor, fija a la camara (como la vista
//          "solida" de Blender): luz principal arriba a la izquierda, relleno a la derecha, cielo / piso y borde de contraluz.
//          No usa las luces de la escena: los jugadores se ven con volumen aunque la escena del Editar no tenga luces.
//   y429 = alambre (no es de este archivo: lo hace la DLL con el estado de raster)
//   z429 = pantalla partida: 0 = la vista en toda la pantalla; 0.01..0.99 = la vista solo a la IZQUIERDA de esa fraccion
//          del ancho y la imagen normal a la derecha, con una linea en el corte
//   w429 = escala de la vista de profundidad (1 / metros hasta el negro); 0 = la que trae cada composicion
//
// Necesita declarados ANTES del include: IniParams, g_psSystem, g_psScene, g_samplerPoint_Clamp_s, g_samplerLinear_Clamp_s, inGBufferNormal,
// inGBufferMaterial, inAlbedo, inLightDiffuse, inLightSpecular, inDepth, inOcclusion, inShadow, PESDecN y PESLinZ.
// Se llama al FINAL de main: PESVistaMotor(o0, v0.xy, escala de profundidad por defecto).
//
// (17/11) GRILLA DEL PISO DEL MOTOR con profundidad: la DLL pone en t49 la misma grilla que antes dibujaba ENCIMA de toda la imagen
// (motorgrid.cpp, transparente, con la camara del Motor) solo en esta composicion y solo con el Motor abierto. Aca se mezcla donde no
// hay nada dibujado: los jugadores la tapan (con la camara arriba del piso, lo que hay detras de un jugador en la pantalla es el piso de
// atras). Sin la DLL nueva t49 no esta puesta, se lee 0 y no cambia nada.
Texture2D<float4> PESMotorGrid : register(t49);

bool PESVistaVacio(float2 uv)   // true = en ese pixel no hay nada dibujado (o esta a mas de 300 m)
{
  float z = PESLinZ(uv);
  return !(z > 0.02 && z < 300.0);
}
float3 PESVistaEstudio(float2 uv)
{
  float3 A = inAlbedo.SampleLevel(g_samplerPoint_Clamp_s, uv, 0).xyz;
  if (PESVistaVacio(uv)) return A;
  float3 n = PESDecN(uv); n.z = -n.z;           // espacio de vista: x derecha, y arriba, z hacia adelante (como PB en 619b)
  float occ = inOcclusion.SampleLevel(g_samplerPoint_Clamp_s, uv, 0).x;
  const float3 L1 = normalize(float3(-0.55, 0.65, -0.55));   // principal: arriba a la izquierda, del lado de la camara
  const float3 L2 = normalize(float3(0.80, 0.05, -0.60));    // relleno: derecha, mas suave
  float d1 = saturate(dot(n, L1)), d2 = saturate(dot(n, L2));
  float3 amb = lerp(float3(0.16, 0.15, 0.14), float3(0.30, 0.32, 0.36), saturate(0.5 + 0.5 * n.y));   // piso / cielo
  float rim = pow(saturate(1.0 + n.z), 3.0) * saturate(0.4 + 0.6 * n.y);   // borde (de espaldas a la camara), mas arriba que abajo
  float3 H = normalize(L1 + float3(0.0, 0.0, -1.0));
  float sp = pow(saturate(dot(n, H)), 40.0) * 0.10;
  float3 c = A * (amb * (0.55 + 0.45 * occ) + d1 * float3(0.78, 0.75, 0.70) + d2 * float3(0.22, 0.24, 0.28)) + (rim * 0.22 + sp) * float3(0.90, 0.92, 1.0);
  return c;
}
void PESVistaGrilla(inout float4 o0, float2 uv)
{
  float4 g = PESMotorGrid.SampleLevel(g_samplerLinear_Clamp_s, uv, 0);
  if (g.a <= 0.002) return;
  if (!PESVistaVacio(uv)) return;
  o0.xyz = lerp(o0.xyz, g.xyz, saturate(g.a));
}

void PESVistaMotor(inout float4 o0, float2 pix, float escalaZ)
{
  float4 VM = IniParams.Load(int2(429, 0));
  int m = (int)(VM.x + 0.5);
  float2 uv = g_psSystem.m_renderBuffer.zw * pix;
  if (m < 1 || m > 11) { PESVistaGrilla(o0, uv); return; }
  if (VM.z > 0.005 && VM.z < 0.995) {
    float dx = uv.x - VM.z;
    if (abs(dx) < g_psSystem.m_renderBuffer.z) { o0.xyz = float3(1.0, 0.85, 0.2); return; }
    if (dx > 0.0) { PESVistaGrilla(o0, uv); return; }
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
    c = mul((float3x3)g_psScene.m_view, nv) * 0.5 + 0.5;   // vista -> mundo = filas de m_view (como 619b 1150-1153)
  }
  else if (m == 5) c = inAlbedo.SampleLevel(g_samplerPoint_Clamp_s, uv, 0).xyz;
  else if (m == 6) {
    float z = PESLinZ(uv);
    float k = VM.w > 0.0 ? VM.w : escalaZ;
    // (17/11 07:06, Agustin: "la vista de profundidad creo que no es correcta, deberia mostrar el z buffer") antes era lineal con
    // bandas cada 20 cm (parecian rayas sobre el jugador). Ahora es el z buffer en grises, sin bandas: blanco cerca, negro en
    // 1 / k metros, en escala logaritmica para que de cerca (el jugador a 2-4 m) se vean las diferencias de forma.
    float zmax = 1.0 / max(k, 1e-4);
    float g = (z > 0.0) ? saturate(1.0 - log2(1.0 + z) / log2(1.0 + zmax)) : 0.0;
    c = g.xxx;
  }
  else if (m == 7) c = occ;
  else if (m == 8) c = inShadow.SampleLevel(g_samplerPoint_Clamp_s, uv, 0).x;
  else if (m == 9) c = Ld * occ + Ls;
  else if (m == 11) c = PESVistaEstudio(uv);
  else c = inGBufferMaterial.SampleLevel(g_samplerPoint_Clamp_s, uv, 0).xyz;
  o0.xyz = max(c, float3(0.0, 0.0, 0.0));
  PESVistaGrilla(o0, uv);
}
