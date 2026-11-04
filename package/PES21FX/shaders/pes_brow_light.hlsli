// PES21 FX - (04/11 o, Agustin: "anda mas a fondo con la luz y las sombras, la luz a contraluz, con los giros de la luz del sol y de
// la luz en general") direccion HACIA la luz que de verdad ilumina las hebras, en el espacio de la vista. Antes las hebras usaban
// m_dominantLightDir del G-buffer, que (1) apunta hacia donde VA la luz, no hacia la luz (el brillo, la sombra de contacto y el
// contraluz salian del lado equivocado), (2) en el editor es la luz de estudio y no la LUZ DE PRUEBA (filas 297 / 298) que
// mueve el slider "Rumbo", y (3) en el partido no es el sol del cielo. Ahora, en este orden:
//   - editor con la luz de prueba prendida (x297, sin sol del partido): la misma direccion que pes_ssss_inc (preset o rumbo/altura)
//   - partido con sol: el sol del cielo (fila 312, mundo) llevado a la vista con la matriz de la vista
//   - si no: la luz dominante del juego, dada vuelta
// c0 / c1 / c2 = columnas 3x3 de la matriz de la vista; dom = m_dominantLightDir; vz = z de la vista del punto (signo)
float3 PesBrowLightDir(float3 c0, float3 c1, float3 c2, float3 dom, float vz)
{
  // una sola salida (fxc X4580 en el GS de las pestanas con returns anticipados)
  float4 E = IniParams.Load(int2(297, 0));
  float4 S = IniParams.Load(int2(312, 0));
  bool noSun = IniParams.Load(int2(44, 0)).w < 0.5 && IniParams.Load(int2(255, 0)).w > 0.5;
  int preset = (int)(E.y + 0.5);
  float az = E.z, el = E.w;
  if (preset == 1) { az = 50; el = 45; }
  else if (preset == 2) { az = 70; el = 12; }
  else if (preset == 3) { az = -40; el = 60; }
  else if (preset == 5 || preset == 6) { az = 20; el = 70; }
  else if (preset == 7) { az = 30; el = 78; }
  else if (preset == 8) { az = 165; el = 10; }
  else if (preset == 9) { az = -45; el = 35; }
  float zSgn = vz < 0.0 ? -1.0 : 1.0;
  float ca = cos(radians(az)), sa = sin(radians(az)), ce = cos(radians(el)), se = sin(radians(el));
  float3 Ltest = float3(ce * sa, se, -ce * ca * zSgn);                       // = PesDirL de pes_ssss_inc (ya en la vista)
  bool sky = !noSun && S.w > 0.5 && S.y > 0.0;
  float3 Lw = sky ? S.xyz : -dom;                                            // sol del cielo (hacia el sol) o la dominante dada vuelta
  bool inView = !sky && IniParams.Load(int2(50, 0)).z > 0.5;                 // z50 = 1: la dominante ya viene en la vista
  float3 Lv = inView ? Lw : float3(dot(Lw, c0), dot(Lw, c1), dot(Lw, c2));
  float3 L = (E.x > 0.5 && noSun) ? Ltest : Lv;
  float l2 = dot(L, L);
  return l2 > 0.25 ? L * rsqrt(l2) : float3(0, 0, 0);
}
