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
#include <map>
#include <cstdio>

namespace fx {
WindNow g_windNow;
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
// (13/11, Agustin: "el viento puede ser reactivo al estadio, la hora del dia, la estacion") tabla por estadio:
// PES21FX\viento_estadios.txt, una linea por estadio: "id = reparo ; fuerza ; nombre". reparo 0 (cancha abierta) .. 1 (estadio
// cerrado y techado): cuanto tapan las tribunas; fuerza = viento tipico de la ciudad en m/s a 10 m (4 = promedio; 0 = no cambia).
// Estadio que no esta en la tabla: reparo 0.5, fuerza sin cambio. Se lee una vez.
struct StadiumWind { float shelter = 0.5f, typical = 0.0f; };
std::map<int, StadiumWind> s_stadiums; bool s_stadLoaded = false;
void LoadStadiums() {
    s_stadLoaded = true;
    FILE* f = _wfopen((g_cfg.baseDir + L"viento_estadios.txt").c_str(), L"r");
    if (!f) { Log("viento unico: no hay PES21FX\\viento_estadios.txt (todos los estadios con reparo medio)"); return; }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int id = -1; float sh = 0.5f, ty = 0.0f;
        if (line[0] == ';' || line[0] == '#') continue;
        if (sscanf(line, " %d = %f ; %f", &id, &sh, &ty) >= 2 && id >= 0) { StadiumWind w; w.shelter = std::clamp(sh, 0.0f, 1.0f); w.typical = std::clamp(ty, 0.0f, 15.0f); s_stadiums[id] = w; }
    }
    fclose(f);
    Log("viento unico: %d estadios en viento_estadios.txt", (int)s_stadiums.size());
}
// hora real del partido (reloj del sol del motor): calma al amanecer, pico a media tarde, afloja de noche. 7 h 0.54, 12 h 1.03,
// 16 h 1.29, 19 h 0.99, 22 h 0.65
float HourFactor(float h) { const float d = (h - 15.5f) / 5.0f; return 0.5f + 0.8f * expf(-d * d); }
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
    // z353 viento unico; w353 = el grupo "viento distinto en cada partido" prendido
    if (c.live[353][2] < 0.5f || c.live[353][3] < 0.5f) { for (int k = 0; k < 4; k++) p[494][k] = p[495][k] = p[504][k] = 0.0f; g_windNow.on = false; return; }
    // viento base de este partido
    float tx, tz;
    { auto wx = c.clothVals.find("sim.wind_x"), wz = c.clothVals.find("sim.wind_z");
      if (c.clothFound && (wx != c.clothVals.end() || wz != c.clothVals.end())) {
          tx = wx != c.clothVals.end() ? wx->second : 2.0f; tz = wz != c.clothVals.end() ? wz->second : 0.0f;
      } else { const float a = c.live[84][2] * 0.0174533f, s = 2.0f * std::clamp(c.live[353][0], 0.0f, 3.0f); tx = cosf(a) * s; tz = sinf(a) * s; } }
    // la semilla de las rachas sale del viento del partido (antes de preajustes y rumbo fijo: cambiarlos no hace saltar la racha)
    { const unsigned s = (unsigned)(int)lroundf(tx * 100.0f) * 2654435761u ^ (unsigned)(int)lroundf(tz * 100.0f) * 40503u ^ 0x5bd1e995u; if (s != seed) seed = s; }
    // (13/11 etapa 2b) menu "Viento": fila 501 x preajuste (0 segun el partido, 1 calmo, 2 brisa, 3 ventoso, 4 tormenta),
    // y rumbo fijo, z grados (hacia donde sopla: 0 = hacia el arco de +X, 90 = hacia el costado +Z), w cambio durante el partido;
    // fila 502 x rachas, y largo de las rachas, z turbulencia, w giro del rumbo (1 = natural); fila 503 x reparo manual, y cuanto
    // (si el ini todavia no trae la clave, vale lo de fabrica: un multiplicador en 0 apagaria las rachas sin que nadie lo pidiera)
    auto P = [&](int r, int k, float def) { return (!c.info[r][k].defined && c.live[r][k] == 0.0f) ? def : c.live[r][k]; };
    // cuanto le llega a cada cosa (z503 pasto, w503 ropa; el pelo tiene x24 y la red x499): los shaders leen la fila que sube la DLL,
    // asi que aca se deja el valor seguro si el ini no los trae
    p[503][2] = std::clamp(P(503, 2, 1.0f), 0.0f, 3.0f); p[503][3] = std::clamp(P(503, 3, 1.0f), 0.0f, 3.0f);
    if (!s_stadLoaded) LoadStadiums();
    StadiumWind sw; { auto it = c.match.known ? s_stadiums.find(c.match.stadium) : s_stadiums.end(); if (it != s_stadiums.end()) sw = it->second; }
    const bool shManual = c.live[503][0] > 0.5f;
    if (shManual) sw.shelter = std::clamp(P(503, 1, 0.5f), 0.0f, 1.0f);
    const int preset = std::clamp((int)lroundf(c.live[501][0]), 0, 4);
    struct Pre { float u, gust, turb, veer; };
    static const Pre kPre[5] = { { 0, 1, 1, 1 }, { 0.8f, 0.5f, 0.6f, 0.7f }, { 3.0f, 0.8f, 0.8f, 0.9f }, { 6.5f, 1.2f, 1.1f, 1.2f }, { 11.0f, 1.5f, 1.5f, 1.5f } };
    const Pre& pre = kPre[preset];
    if (preset > 0) {   // la fuerza la pone el preajuste (en la cancha, x la fuerza general); el rumbo sigue siendo el del partido
        float l = sqrtf(tx * tx + tz * tz), ax = 1.0f, az = 0.0f;
        if (l > 1e-4f) { ax = tx / l; az = tz / l; } else { const float a = c.live[84][2] * 0.0174533f; ax = cosf(a); az = sinf(a); }
        const float u = pre.u * std::clamp(c.live[353][0], 0.0f, 3.0f); tx = ax * u; tz = az * u;
    }
    const bool fixedDir = c.live[501][1] > 0.5f;
    if (fixedDir) { const float l = sqrtf(tx * tx + tz * tz), a = c.live[501][2] * 0.0174533f; tx = cosf(a) * l; tz = sinf(a) * l; }
    float envK = 1.0f;
    if (preset > 0) {}   // preajuste: la fuerza elegida es la de la cancha (sin hora, ciudad ni reparo)
    else {
    if (g_solClock.valid) {   // MatchWind ya bajo 0.7 de noche con el dato dia / noche: se reemplaza por la curva de la hora real
        envK *= HourFactor(g_solClock.hour) / ((c.match.known && c.match.timeofday == 1 && c.live[353][3] > 0.5f && c.clothFound) ? 0.7f : 1.0f);
    }
    if (sw.typical > 0.0f) envK *= std::clamp(sw.typical / 4.0f, 0.4f, 2.0f);
    envK *= 1.0f - 0.6f * sw.shelter + 0.3f;   // reparo: 0 -> 1.3, 0.5 (sin dato) -> 1.0, 1 -> 0.7
    }
    tx *= envK; tz *= envK;
    const int season = c.match.known ? c.match.season : -1;   // 0 verano, 1 invierno, otro: primavera / otono o sin dato
    const float seaGust = season == 0 ? 0.9f : season == 1 ? 1.2f : 1.0f, seaTurb = season == 1 ? 1.15f : 1.0f, seaVeer = season == 0 ? 0.8f : season == 1 ? 1.0f : 1.3f;
    const double seaT = (season == 0 ? 10.0 : season == 1 ? 18.0 : 14.0) * std::clamp((double)P(502, 1, 1.0f), 0.3, 3.0);   // largo de la racha (s): verano cortas, invierno sostenidas; y502 la alarga o acorta
    if (!haveBase) { bx = tx; bz = tz; haveBase = true; }
    else { const float k = (float)(std::min)(1.0, dR / 3.0); bx += (tx - bx) * k; bz += (tz - bz) * k; }   // cambio de partido o del menu: gira en ~3 s, sin salto
    const float u0 = sqrtf(bx * bx + bz * bz);
    const bool rain = c.match.known && c.match.weather == 1;
    const float var = std::clamp(c.live[353][1], 0.0f, 1.0f);   // el mismo "azar" del partido: cuanta racha
    const float kGust = std::clamp(P(502, 0, 1.0f), 0.0f, 3.0f) * pre.gust, kTurb = std::clamp(P(502, 2, 1.0f), 0.0f, 3.0f) * pre.turb,
                kVeer = std::clamp(P(502, 3, 1.0f), 0.0f, 3.0f) * pre.veer, kDrift = std::clamp(P(501, 3, 0.5f), 0.0f, 1.0f);
    const float gustK = (rain ? 0.75f : 0.6f) * (0.5f + var) * seaGust * kGust;                 // cuanto mas que el promedio en la racha
    const float turbI = (rain ? 0.40f : 0.25f) * (0.6f + 0.8f * var) * seaTurb * (1.0f + 0.4f * (sw.shelter - 0.5f)) * kTurb;   // detras de la tribuna, mas revuelto
    // el viento en el instante t del reloj del juego. Todo es ruido suave de t: el de dentro de 2 s (proactivo) sale de la misma cuenta
    struct At { double e; float U, dx, dz; };
    auto eval = [&](double t) {
        At r;
        // racha / calma: envolvente 0..1. Dos escalas (14 s y 37 s); el exponente la deja mas tiempo en calma y sube rapido
        double e = 0.5 + 0.5 * (0.7 * Noise1(t / seaT, seed) + 0.3 * Noise1(t / 37.0 + 11.3, seed + 7u));
        e = std::clamp((e - 0.30) / 0.45, 0.0, 1.0); e = e * e * (3.0 - 2.0 * e);
        const double fast = 0.6 * Noise1(t / 2.3 + 3.1, seed + 13u) + 0.4 * Noise1(t / 0.9 + 5.7, seed + 17u);
        // (etapa 2b) el tiempo cambia durante el partido: una curva lenta (~7 min) sube o baja el viento medio hasta 40 % (w501 = 1)
        const double drift = 1.0 + 0.4 * kDrift * Noise1(t / 420.0 + 50.0, seed + 31u);
        r.U = u0 * (float)(drift * (0.55 + (0.45 + gustK) * e) * (1.0 + 0.35 * turbI * fast));
        // rumbo: se corre lento, y un poco mas dentro de la racha
        const double veer = (12.0 * Noise1(t / 41.0 + 20.0, seed + 23u) + 4.0 * (0.4 + 0.6 * e) * Noise1(t / 7.0 + 30.0, seed + 29u)) * (0.5 + var) * seaVeer * kVeer * 0.0174533;
        r.dx = 1.0f; r.dz = 0.0f;
        if (u0 > 1e-4f) { const float cs = (float)cos(veer), sn = (float)sin(veer); r.dx = (bx * cs - bz * sn) / u0; r.dz = (bx * sn + bz * cs) / u0; }
        else r.U = 0.0f;
        r.e = e; return r;
    };
    const At now = eval(tG), soon = eval(tG + 2.0);
    const double e = now.e; float U = now.U; const float dx = now.dx, dz = now.dz;
    // (13/11, Agustin: "hay escenas donde el viento no esta presente, como adentro de una casa o tunel; eso deberia ser leible
    // o adaptable") bajo techo: si en el cuadro no se dibujo el cielo del juego (w159, domo 9adf9d46) NI el pasto de la cancha
    // (vestuario, tunel, menus, estudio), el viento baja en ~1.5 s a lo que diga y500 (0.05 = casi nada) y vuelve en ~1 s al salir.
    // x500 = 1: siempre afuera (como antes).
    static float expo = 1.0f; static bool wasOut = true;
    { const bool out = c.live[500][0] > 0.5f || p[159][3] > 0.5f || PresentNo() - g_grassSeenFrame.load(std::memory_order_relaxed) < 30;
      const float k = (float)(std::min)(1.0, dR / (out ? 1.0 : 1.5)); expo += ((out ? 1.0f : 0.0f) - expo) * k;
      if (out != wasOut) { wasOut = out; Log("viento unico: %s (cielo %s, pasto hace %d cuadros)", out ? "afuera" : "bajo techo: el viento se calma",
          p[159][3] > 0.5f ? "si" : "no", PresentNo() - g_grassSeenFrame.load()); } }
    const float roofK = std::clamp(c.live[500][1], 0.0f, 1.0f) + (1.0f - std::clamp(c.live[500][1], 0.0f, 1.0f)) * expo;
    U *= roofK;
    dist += (double)U * dG; if (dist >= 8192.0) dist -= 8192.0;
    p[494][0] = dx; p[494][1] = dz; p[494][2] = u0 > 1e-4f ? (std::max)(U, 0.01f) : 0.0f; p[494][3] = (float)dist;
    // (etapa 2b) fila 504 (la escribe la DLL): el viento de dentro de 2 s, para lo que es lento (banderas grandes, nubes) y tiene
    // que empezar a moverse antes: x, y rumbo, z velocidad m/s, w racha 0..1
    p[504][0] = soon.dx; p[504][1] = soon.dz; p[504][2] = u0 > 1e-4f ? (std::max)(soon.U * roofK, 0.01f) : 0.0f; p[504][3] = (float)soon.e;
    p[495][0] = (float)e; p[495][1] = std::clamp(turbI * (0.5f + 0.5f * (float)e) * 2.0f, 0.0f, 1.0f); p[495][2] = (float)fmod(tG, 8192.0); p[495][3] = p[157][3];
    { WindNow& n = g_windNow; n.on = true; n.speed = p[494][2]; n.dirDeg = atan2f(dz, dx) * 57.2958f; n.gust = (float)e; n.turb = p[495][1];
      n.outside = roofK; n.base = u0; n.preset = preset; n.fixedDir = fixedDir; n.shelter = sw.shelter; n.shelterManual = shManual;
      n.soonSpeed = p[504][2]; n.soonDirDeg = atan2f(soon.dz, soon.dx) * 57.2958f; n.stadium = c.match.known ? c.match.stadium : -1;
      n.hour = g_solClock.valid ? g_solClock.hour : -1.0f; n.rain = rain; n.season = season; n.fromCloth = c.clothFound; }
    // (el pelo toma el rumbo de la fila 494 en sus shaders; la fila 23 x / y queda como la del ini / cloth.ini)
    if (tReal >= nextLog) { nextLog = tReal + 60.0;
        Log("viento unico: base %.2f m/s rumbo %.0f grados | ahora %.2f m/s rumbo %.0f grados, racha %.2f, turbulencia %.2f, recorrido %.0f m, reloj %s%s, afuera %.2f | hora %.1f (x%.2f), estadio %d reparo %.2f tipico %.1f, estacion %d | preajuste %d, rumbo %s, en 2 s %.2f m/s",
            u0, atan2f(bz, bx) * 57.2958f, U, atan2f(dz, dx) * 57.2958f, (float)e, p[495][1], (float)dist, p[157][3] > 0.5f ? "del juego" : "real", rain ? ", lluvia" : "", expo,
            g_solClock.valid ? g_solClock.hour : -1.0f, g_solClock.valid ? HourFactor(g_solClock.hour) : 1.0f, c.match.known ? c.match.stadium : -1, sw.shelter, sw.typical, season, preset, fixedDir ? "fijo" : "del partido", p[504][2]); }
}
} // namespace fx
