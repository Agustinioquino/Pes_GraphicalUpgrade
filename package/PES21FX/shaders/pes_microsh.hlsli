// PES21 (13/11/2026, Agustin: "imaginate un sistema de micro sombras mas avanzado en kits, caras, estadios, pastos, de ultima
// generacion") MICRO SOMBRAS v4: SOMBRA DE RELIEVE CON DIRECCION.
// Lo de antes (fila 88) era un oscurecido sin direccion: comparaba la normal de detalle con el promedio de sus vecinas y bajaba la
// luz de todo lo "hundido", igual de los dos lados del poro o del pliegue. Aca cada pixel CAMINA unos milimetros hacia el sol sobre
// la superficie y reconstruye el relieve que tiene delante con las normales de detalle del G-buffer (poros, arrugas, pliegues y
// trama de la tela, juntas de cemento): la pendiente de cada normal respecto de la superficie lisa se va sumando y da la altura del
// relieve a lo largo del camino. Si en algun punto esa altura supera a la linea que va del pixel al sol, el pixel queda tapado.
// Resultado: sombra SOLO del lado contrario a la luz, que se alarga con el sol bajo, desaparece con el sol de frente y gira con el.
// Sirve para cualquier material porque usa lo que ya esta en pantalla (no hace falta que cada material escriba nada).
//   uvC      pixel (0..1)                        P0   posicion de vista del pixel (z hacia adelante)
//   Nd       normal de detalle del pixel         Ns   normal lisa (promedio del anillo: la forma sin el detalle)
//   L        hacia la luz (unitaria, mismo espacio que las normales con z cambiada)
//   R        largo del camino en metros          jit  ruido 0..1 por pixel (lo limpia el TAA)
// Devuelve 1 = al sol, 0 = tapado por su propio relieve. Usa PESDecN / PESLinZ y g_psScene del archivo que lo incluye
float PESMicroShadowDir(float2 uvC, float3 P0, float3 Nd, float3 Ns, float3 L, float R, float jit)
{
  float nsl = dot(Ns, L);
  if (nsl <= 0.02) return 1.0;                       // la forma ya mira lejos de la luz: eso lo resuelve N.L y la sombra del juego
  float3 Lp = L - Ns * nsl;
  float lpl = length(Lp);
  if (lpl < 0.06) return 1.0;                        // luz casi perpendicular a la superficie: ningun relieve chico llega a tapar
  float3 e = Lp / lpl;                               // direccion hacia la luz apoyada en la superficie
  float tanE = nsl / lpl;                            // altura de la luz sobre la superficie (tangente)
  float sPrev = clamp(-dot(Nd, e) / max(dot(Nd, Ns), 0.25), -3.0, 3.0);
  float h = 0.0, tPrev = 0.0, worst = -1.0;
  [unroll] for (int i = 0; i < 8; i++) {
    float t = R * (i + 0.3 + 0.7 * jit) * 0.125;
    float3 Q = P0 + e * t;
    if (Q.z < 0.06) break;
    float2 sQ = Q.xy / (g_psScene.m_projectionParam.xy * Q.z) + g_psScene.m_cameraCenterOffset.xy;
    float2 uvQ = (sQ + float2(1, -1)) * float2(0.5, -0.5);
    if (any(uvQ < 0.0) || any(uvQ > 1.0)) break;
    float zQ = PESLinZ(uvQ);
    if (abs(zQ - Q.z) > 0.6 * R + 0.002) break;      // borde de la pieza u otra superficie: hasta ahi llega el relieve propio
    float3 n = PESDecN(uvQ); n.z = -n.z;
    float s = clamp(-dot(n, e) / max(dot(n, Ns), 0.25), -3.0, 3.0);   // pendiente del relieve hacia la luz en ese punto
    h += 0.5 * (s + sPrev) * (t - tPrev);            // altura acumulada (trapecios)
    worst = max(worst, h / t - tanE);                // cuanto asoma el relieve por encima de la linea al sol
    sPrev = s; tPrev = t;
  }
  return 1.0 - smoothstep(0.0, 0.30, worst);         // penumbra: borde suave en vez de corte
}
