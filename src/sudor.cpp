// (13/11, Agustin: "no me gusta como esta el sudor actual ademas que choca con el propio del juego"; "no es lo mismo el sudor que
// las gotas por la lluvia"; "un sudor que remplaze al del juego y sea ... adaptativo con respecto al paso del tiempo del partido,
// el clima, el cansancio del jugador") SUDOR 2: un valor de sudor POR JUGADOR que lleva la DLL y leen los shaders de la cara, la
// piel del cuerpo y la camiseta (pes_sweat2.hlsli). Reemplaza al reloj unico de antes (x10 con x9 / y9) y al sudor del juego.
//
// Modelo (por personaje de personajes.cpp, con memoria todo el partido; se guarda por id de jugador):
//   esfuerzo  e = velocidad del nodo en el piso: 1.2 m/s (caminar) = 0, 7.7 m/s (pique) = 1
//   calor     T sigue a 0.12 + 0.88 e^1.6: sube en ~40 s y baja en ~110 s (el cuerpo tarda en calentarse y en enfriarse: se sigue
//             sudando un rato despues del pique)
//   sudor     S sube con T x clima / tiempo de llenado y se evapora en proporcion a S: cada jugador llega a SU equilibrio
//             (arquero ~0.3, volante ~0.8, el que no para de correr 1). Clima: verano / dia sudan mas, invierno / noche menos; con
//             lluvia se suda algo menos y no se evapora casi nada
// El tiempo es el del JUEGO (fila 157) y solo tiempo NUEVO: la pausa lo congela y una repeticion no suma dos veces. Fuera del
// partido (entretiempo, menus) se seca solo con el reloj real (~90 s): vuelven del vestuario mas secos; un partido nuevo arranca seco.
//
// Filas (kParamCount 493):
//   457  x on, y cantidad, z gotas, w velocidad con que se suda            (ini / menu)
//   458  x secado, y lluvia en la piel, z gotas de lluvia, w camiseta      (ini / menu)
//   459  x sudor de prueba (0 = el del partido), y lluvia de prueba, z diagnostico, w cuanto pesa el clima   (x / y / z nunca se guardan)
//   460  la escribe la DLL: x personajes, y 1, z lluvia 0..1 (cuanto llovio sobre ellos), w sudor medio
//   461..492  la escribe la DLL: x, z en el mundo, sudor 0..1, calor 0..1; ordenados del mas cercano a la camara al mas lejano
#include "core.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace fx {
namespace {
constexpr int kRowHead = 460, kRowFirst = 461, kRowMax = 32;
struct Sw { uint32_t id = 0; int slot = -1; float x = 0, y = 0, z = 0, spd = 0, heat = 0, sweat = 0; bool posOk = false; double seen = 0; };
std::vector<Sw> s_sw;
int s_gen = -1; double s_gMax = -1e18, s_lastAcc = -1.0, s_log = 0.0; float s_rain = 0.0f; unsigned s_tabGen = ~0u;
CharInfo s_info[kCharSlots]; bool s_alive[kCharSlots];

float Climate(const float (*p)[4]) {
    const MatchInfo& m = g_cfg.match; float c = 1.0f;
    if (m.known) {
        if (m.season == 0) c *= 1.15f; else if (m.season == 1) c *= 0.85f;          // verano / invierno
        if (m.timeofday == 0) c *= 1.08f; else if (m.timeofday == 1) c *= 0.92f;    // dia / noche
        if (m.weather == 1) c *= 0.88f;                                             // lluvia: mas fresco
    }
    const float k = p[459][3] > 0.0f ? std::clamp(p[459][3], 0.0f, 2.0f) : 1.0f;
    return 1.0f + (c - 1.0f) * k;
}
}

float SudorMedio() { return g_cfg.live[kRowHead][3]; }   // 0..1, para la tela mojada (UpdateClothWet)

void SudorFrame(float (*p)[4], double t) {   // desde UpdateTimeParams, con g_cfgMutex tomado
    static double tPrev = -1.0;
    const float dtR = tPrev < 0.0 ? 0.0f : (float)std::clamp(t - tPrev, 0.0, 0.1); tPrev = t;
    const bool on = p[457][0] > 0.5f, inMatch = p[93][3] > 0.5f && p[10][0] > 0.0f;
    auto clear = [&]() { for (int i = kRowHead; i < kRowFirst + kRowMax; i++) p[i][0] = p[i][1] = p[i][2] = p[i][3] = 0.0f; };
    if (!on) { clear(); return; }
    // partido nuevo o vuelta del entretiempo: lo que se seco mientras tanto (reloj real, ~90 s)
    if (g_matchGen.load() != s_gen) {
        s_gen = g_matchGen.load(); s_gMax = -1e18;
        const float gap = s_lastAcc < 0.0 ? 1e9f : (float)(t - s_lastAcc), k = expf(-gap / 90.0f);
        for (Sw& s : s_sw) { s.sweat *= k; s.heat *= expf(-gap / 45.0f); s.posOk = false; s.slot = -1; }
        s_rain *= expf(-gap / 240.0f);
        if (gap > 600.0f) s_sw.clear();
    }
    if (!inMatch) { clear(); return; }
    // tiempo del juego, solo el nuevo (como los botines sucios, pitchwear.cpp)
    float dt = dtR;
    if (p[157][3] >= 0.5f) { const double g = (double)p[157][0];
        if (s_gMax < -1e17) s_gMax = g;
        double dNew = g - s_gMax; if (dNew < -50000.0) dNew += 100000.0;
        if (dNew <= 0.0) dt = 0.0f; else { dt = (std::min)(dt, (float)dNew); s_gMax = g; } }
    if (dt > 0.0f) s_lastAcc = t;
    if (CharTableGen() != s_tabGen) { s_tabGen = CharTableGen(); CharSnapshot(s_info, s_alive); for (Sw& s : s_sw) s.slot = -1; }
    const bool rain = p[114][0] > 0.5f && p[114][0] < 1.5f;
    const float clima = Climate(p), speed = p[457][3] > 0.0f ? std::clamp(p[457][3], 0.1f, 4.0f) : 1.0f, dry = p[458][0] > 0.0f ? std::clamp(p[458][0], 0.1f, 4.0f) : 1.0f;
    const float tauFill = 210.0f / speed, tauEvap = 470.0f / dry * (rain ? 2.5f : 1.0f);
    if (dt > 0.0f) s_rain = std::clamp(s_rain + (rain ? dt / 70.0f : -dt / 300.0f), 0.0f, 1.0f);
    // camara (para ordenar): m_view de la copia de SceneCB; sin ella queda el orden de la tabla
    float cam[3] = { 0, 0, 0 }; bool camOk = false;
    { std::vector<float> S; if (NamedCBRead("SceneCB", S, nullptr) && S.size() >= 48) { const float* M = &S[32]; const float T[3] = { M[3], M[7], M[11] };
        cam[0] = -(T[0] * M[0] + T[1] * M[4] + T[2] * M[8]); cam[1] = -(T[0] * M[1] + T[1] * M[5] + T[2] * M[9]); cam[2] = -(T[0] * M[2] + T[1] * M[6] + T[2] * M[10]);
        camOk = (M[0] * M[0] + M[1] * M[1] + M[2] * M[2]) > 0.5f; } }
    struct Out { float x, z, s, h, d2; }; Out out[kCharSlots]; int n = 0; float sum = 0.0f, mx = 0.0f;
    for (int i = 0; i < kCharSlots; i++) {
        if (!s_alive[i] || !s_info[i].node) continue;
        float pos[3]; if (!CharNodeWorldPos(s_info[i].node, pos)) continue;
        if (!(pos[0] == pos[0]) || fabsf(pos[0]) > 500.0f || fabsf(pos[2]) > 500.0f) continue;
        // su registro: el del mismo jugador (id) o, sin id, el de este lugar
        const uint32_t id = s_info[i].known ? s_info[i].id : 0u; Sw* s = nullptr;
        for (Sw& q : s_sw) if ((id && q.id == id) || (!id && !q.id && q.slot == i)) { s = &q; break; }
        if (!s) { if (s_sw.size() >= 96) continue; s_sw.push_back(Sw{}); s = &s_sw.back(); s->id = id; }
        s->slot = i; s->seen = t;
        if (dt > 0.0f) {
            if (s->posOk) {
                const float dx = pos[0] - s->x, dz = pos[2] - s->z, v = sqrtf(dx * dx + dz * dz) / dt;
                if (v < 15.0f) s->spd += (v - s->spd) * (1.0f - expf(-dt / 0.5f));   // mas de 15 m/s: corte o saque
            }
            s->x = pos[0]; s->y = pos[1]; s->z = pos[2]; s->posOk = true;
            const float e = std::clamp((s->spd - 1.2f) / 6.5f, 0.0f, 1.0f), target = 0.12f + 0.88f * powf(e, 1.6f);
            s->heat += (target - s->heat) * (1.0f - expf(-dt / (target > s->heat ? 40.0f : 110.0f)));
            s->sweat += dt * (s->heat * clima / tauFill - s->sweat / tauEvap);
            s->sweat = std::clamp(s->sweat, 0.0f, 1.0f);
        } else if (!s->posOk) { s->x = pos[0]; s->y = pos[1]; s->z = pos[2]; }
        const float cx = pos[0] - cam[0], cy = pos[1] + 1.0f - cam[1], cz = pos[2] - cam[2];
        out[n++] = Out{ pos[0], pos[2], s->sweat, s->heat, camOk ? cx * cx + cy * cy + cz * cz : (float)n };
        sum += s->sweat; mx = (std::max)(mx, s->sweat);
    }
    std::sort(out, out + n, [](const Out& a, const Out& b) { return a.d2 < b.d2; });
    const int m = (std::min)(n, kRowMax);
    for (int i = 0; i < kRowMax; i++) { float* r = p[kRowFirst + i];
        if (i < m) { r[0] = out[i].x; r[1] = out[i].z; r[2] = out[i].s; r[3] = out[i].h; } else r[0] = r[1] = r[2] = r[3] = 0.0f; }
    float* H = p[kRowHead]; H[0] = (float)m; H[1] = 1.0f; H[2] = s_rain; H[3] = n > 0 ? sum / (float)n : 0.0f;
    if (t > s_log) { s_log = t + 60.0;
        Log("sudor 2 (60 s): %d personajes (%d en la tabla), sudor medio %.2f, maximo %.2f, lluvia %.2f, clima x%.2f, reloj del juego %s", n, m, H[3], mx, s_rain, clima, p[157][3] >= 0.5f ? "si" : "no (reloj real)"); }
}
} // namespace fx
