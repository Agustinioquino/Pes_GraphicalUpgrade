// (13/11 sk9ema) VIENTO UNICO. Agustin: "leer el viento como parametro en el juego, rehacer un viento variable, estable, realista,
// proactivo y reactivo y que se pueda conectar con los demas shaders, incluso con la fisica de la ropa".
// El juego base no tiene un viento del mundo (decompilado/hallazgos/20261113_viento_motor.cpp.md): las nubes tienen
// cloudWindInfluence pero la velocidad esta compilada en 0, WindActionParam.airResistanceRate vale 0 y nadie le da direccion, y la
// tela nativa solo convierte el movimiento del jugador en viento (convertMoveToWind). Por eso el viento lo hace esta DLL, UNA vez
// por cuadro, y todos leen lo mismo:
//   fila 494 (la escribe la DLL, no va al ini): x, y = rumbo (vector unitario en X / Z del mundo, hacia donde sopla),
//            z = velocidad ahora en m/s (0 = sin dato: cada shader sigue como antes), w = distancia recorrida por el aire (m, mod 8192)
//   fila 495 (idem): x = racha ahora 0..1 (ya incluida en z494), y = turbulencia 0..1, z = reloj del juego (s, mod 8192),
//            w = 1 siguiendo el reloj del juego (0 = reloj real)
// Base: el viento del partido (MatchWind, fila 353: hora, clima, estacion, azar -> cloth.ini sim.wind_x / wind_z); sin mod de ropa,
// el rumbo de las nubes (z84) y 2 m/s por x353. Encima, sobre el RELOJ DEL JUEGO (fila 157: la pausa lo congela, la camara lenta
// lo frena, la repeticion no salta):
//   - rachas y calmas: una envolvente lenta (~14 s) que va de calma (0.55 x) a racha (hasta 1.6 x), con subidas mas rapidas que las bajadas
//   - el rumbo se va corriendo solo (+-12 grados lento, +-4 rapido); mas con racha
//   - turbulencia chica y rapida sobre la velocidad (mas con lluvia)
//   - todo es ruido suave en funcion del tiempo de juego: no depende de los FPS ni acumula error (estable)
//   - w494 = integral de la velocidad: los frentes de racha de cada shader VIAJAN con el aire y llegan antes a lo que esta viento arriba
// z353 = 1 lo prende (0 = todo como antes: fila 494 en cero).
#include "core.h"
#include <cmath>
#include <algorithm>

namespace {
double Hash1(long long i, unsigned seed) {
    unsigned long long h = (unsigned long long)i * 0x9E3779B97F4A7C15ull ^ ((unsigned long long)seed << 32 | seed);
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return (double)(h >> 11) * (1.0 / 9007199254740992.0);   // 0..1
}
// ruido suave 1D, -1..1, derivada continua
double Noise1(double x, unsigned seed) {
    double f = floor(x); long long i = (long long)f; double u = x - f;
    u = u * u * u * (u * (u * 6.0 - 15.0) + 10.0);
    double a = Hash1(i, seed), b = Hash1(i + 1, seed);
    return (a + (b - a) * u) * 2.0 - 1.0;
}
}

void WindFrame(float (*p)[4], double tReal) {
    Config& c = g_cfg;
    static double lastReal = -1.0, lastGame = -1.0, tG = 0.0, dist = 0.0, nextLog = 0.0;
    static float bx = 0.0f, bz = 0.0f; static bool haveBase = false; static unsigned seed = 1u;
    const double dR = lastReal < 0.0 ? 0.0 : std::clamp(tReal - lastReal, 0.0, 0.25); lastReal = tReal;
    // avance del reloj del juego (157.x se acumula en UpdateTimeParams y da la vuelta en 100000)
    double g = (double)p[157][0], dG = 0.0;
    if (lastGame >= 0.0) { dG = g - lastGame; if (dG < -50000.0) dG += 100000.0; dG = std::clamp(dG, 0.0, 0.5); }
    lastGame = g; tG += dG;
    if (c.live[353][2] < 0.5f) { for (int k = 0; k < 4; k++) p[494][k] = p[495][k] = 0.0f; return; }
    // viento base de este partido
    float tx, tz;
    { auto wx = c.clothVals.find("sim.wind_x"), wz = c.clothVals.find("sim.wind_z");
      if (c.clothFound && (wx != c.clothVals.end() || wz != c.clothVals.end())) {
          tx = wx != c.clothVals.end() ? wx->second : 2.0f; tz = wz != c.clothVals.end() ? wz->second : 0.0f;
      } else { const float a = c.live[84][2] * 0.0174533f, s = 2.0f * std::clamp(c.live[353][0], 0.0f, 3.0f); tx = cosf(a) * s; tz = sinf(a) * s; } }
    if (!haveBase) { bx = tx; bz = tz; haveBase = true; }
    else { const float k = (float)(std::min)(1.0, dR / 3.0); bx += (tx - bx) * k; bz += (tz - bz) * k; }   // cambio de partido o del menu: gira en ~3 s, sin salto
    { const unsigned s = (unsigned)(int)lroundf(tx * 100.0f) * 2654435761u ^ (unsigned)(int)lroundf(tz * 100.0f) * 40503u ^ 0x5bd1e995u; if (s != seed) seed = s; }
    const float u0 = sqrtf(bx * bx + bz * bz);
    const bool rain = c.match.known && c.match.weather == 1;
    const float var = std::clamp(c.live[353][1], 0.0f, 1.0f);   // el mismo "azar" del partido: cuanta racha
    // racha / calma: envolvente 0..1. Dos escalas (14 s y 37 s); el exponente la deja mas tiempo en calma y sube rapido
    double e = 0.5 + 0.5 * (0.7 * Noise1(tG / 14.0, seed) + 0.3 * Noise1(tG / 37.0 + 11.3, seed + 7u));
    e = std::clamp((e - 0.30) / 0.45, 0.0, 1.0); e = e * e * (3.0 - 2.0 * e);
    const float gustK = (rain ? 0.75f : 0.6f) * (0.5f + var);                 // cuanto mas que el promedio en la racha
    const float turbI = (rain ? 0.40f : 0.25f) * (0.6f + 0.8f * var);        // intensidad de turbulencia
    const double fast = 0.6 * Noise1(tG / 2.3 + 3.1, seed + 13u) + 0.4 * Noise1(tG / 0.9 + 5.7, seed + 17u);
    float U = u0 * (float)((0.55 + (0.45 + gustK) * e) * (1.0 + 0.35 * turbI * fast));
    // rumbo: se corre lento, y un poco mas dentro de la racha
    const double veer = (12.0 * Noise1(tG / 41.0 + 20.0, seed + 23u) + 4.0 * (0.4 + 0.6 * e) * Noise1(tG / 7.0 + 30.0, seed + 29u)) * (0.5 + var) * 0.0174533;
    float dx = 1.0f, dz = 0.0f;
    if (u0 > 1e-4f) { const float cs = (float)cos(veer), sn = (float)sin(veer); dx = (bx * cs - bz * sn) / u0; dz = (bx * sn + bz * cs) / u0; }
    else U = 0.0f;
    dist += (double)U * dG; if (dist >= 8192.0) dist -= 8192.0;
    p[494][0] = dx; p[494][1] = dz; p[494][2] = u0 > 1e-4f ? (std::max)(U, 0.01f) : 0.0f; p[494][3] = (float)dist;
    p[495][0] = (float)e; p[495][1] = std::clamp(turbI * (0.5f + 0.5f * (float)e) * 2.0f, 0.0f, 1.0f); p[495][2] = (float)fmod(tG, 8192.0); p[495][3] = p[157][3];
    // el pelo sigue el mismo rumbo (fila 23: x, y; antes quedaba fijo todo el partido)
    if (u0 > 1e-4f) { p[23][0] = dx; p[23][1] = dz; }
    if (tReal >= nextLog) { nextLog = tReal + 60.0;
        Log("viento unico: base %.2f m/s rumbo %.0f grados | ahora %.2f m/s rumbo %.0f grados, racha %.2f, turbulencia %.2f, recorrido %.0f m, reloj %s%s",
            u0, atan2f(bz, bx) * 57.2958f, U, atan2f(dz, dx) * 57.2958f, (float)e, p[495][1], (float)dist, p[157][3] > 0.5f ? "del juego" : "real", rain ? ", lluvia" : ""); }
}
