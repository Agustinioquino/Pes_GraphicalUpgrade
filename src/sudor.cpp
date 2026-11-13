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
// Filas (kParamCount 494):
//   457  x on, y cantidad, z gotas, w velocidad con que se suda            (ini / menu)
//   458  x secado, y lluvia en la piel, z gotas de lluvia, w camiseta      (ini / menu)
//   459  x sudor de prueba (0 = el del partido), y lluvia de prueba, z diagnostico, w cuanto pesa el clima   (x / y / z nunca se guardan)
//   460  la escribe la DLL: x personajes, y 1, z lluvia 0..1 (cuanto llovio sobre ellos), w sudor medio
//   461..492  la escribe la DLL: x, z en el mundo, sudor 0..1, calor 0..1; ordenados del mas cercano a la camara al mas lejano
//   493  x cuanto pesa la estamina del juego (1 = natural, 0 = solo la carrera), y..w la escribe la DLL (y jugadores con estamina,
//        z estamina media 0..1, w 1 si se reconocio la posicion de los jugadores del juego)
//
// (13/11, Agustin: "si puede leer la estamina del jugador tambien mejor") ESTAMINA REAL: PlayerInfo[i] del registro del partido
// (reg 0x143702208 + (0xf3 + i) * 0x28, como FUN_140a48fe0) y la posicion de su PlayerMove (reg + (0xbf + i) * 0x28, +0x55c / +0x564,
// como FUN_140a49000 y FUN_1405bebc0). La resistencia esta codificada (Stamina_Pct 0x140a58c10: 4 ranuras rotativas, semilla x3 y un
// hash FNV-1a que si no coincide CIERRA el juego): NO se llama a la funcion del juego (desde Present podria leer una escritura a
// medias y terminar en esa salida); se decodifica aca con la misma cuenta y, si el hash no coincide, se saltea ese jugador ese cuadro.
// Solo lectura (con __try). La posicion del juego se lleva a la del mundo probando ejes / signos / escala hasta que los 22 caen sobre
// los personajes (se repite cada 5 s); sin eso la estamina no se usa y queda solo la carrera.
#include "core.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>

namespace fx {
namespace {
constexpr int kRowHead = 460, kRowFirst = 461, kRowMax = 32;
struct Sw { uint32_t id = 0; int slot = -1; float x = 0, y = 0, z = 0, spd = 0, heat = 0, sweat = 0, fat = 0; bool posOk = false, fatOk = false; double seen = 0; };
std::vector<Sw> s_sw;
int s_gen = -1; double s_gMax = -1e18, s_lastAcc = -1.0, s_log = 0.0; float s_rain = 0.0f; unsigned s_tabGen = ~0u;
CharInfo s_info[kCharSlots]; bool s_alive[kCharSlots];

// ---- estamina del juego ----
constexpr uintptr_t kRegGlobal = 0x143702208ull;
bool SafeRd(uintptr_t a, void* out, size_t n) { if (a < 0x10000) return false; __try { memcpy(out, (const void*)a, n); return true; } __except (1) { return false; } }
uintptr_t SafePtr(uintptr_t a) { uintptr_t v = 0; return SafeRd(a, &v, 8) ? v : 0; }
// valor codificado en base: semilla en base - 8, indice de ranura en base - 4, hash en base - 0x10 (FUN_140a58c10)
bool DecodeVal(uintptr_t pi, uint32_t base, int32_t& out) {
    uint8_t blk[0x20]; if (!SafeRd(pi + base - 0x10, blk, sizeof blk)) return false;
    uint64_t hash; uint32_t seed, idx; memcpy(&hash, blk, 8); memcpy(&seed, blk + 8, 4); memcpy(&idx, blk + 12, 4);
    const uint8_t* raw = blk + 16 + (idx & 3) * 4;
    const uint32_t s3 = seed * 3u;
    const uint8_t k[4] = { (uint8_t)s3, (uint8_t)((s3 + 4u) >> 8), (uint8_t)((s3 + 8u) >> 16), (uint8_t)((s3 + 12u) >> 24) };
    uint8_t b[4]; uint64_t h = 0xcbf29ce484222325ull;
    for (int i = 0; i < 4; i++) { b[i] = (uint8_t)((uint8_t)(raw[i] + k[i]) ^ (uint8_t)(k[i] * 0x3f)); h = (h ^ b[i]) * 0x100000001b3ull; }
    if (h != hash) return false;   // escritura a medias o el formato no es este: no se usa
    memcpy(&out, b, 4); return true;
}
struct GameP { float gx = 0, gz = 0, st = -1.0f; };   // posicion del juego, estamina 0..1 (-1 = no se pudo leer)
int ReadGamePlayers(GameP out[22]) {
    const uintptr_t reg = SafePtr(kRegGlobal); if (!reg) return 0;
    int n = 0;
    for (int i = 0; i < 22; i++) {
        GameP g; const uintptr_t pi = SafePtr(reg + (0xf3 + (uintptr_t)i) * 0x28), pm = SafePtr(reg + (0xbf + (uintptr_t)i) * 0x28);
        if (!pm) continue;
        float xz[3]; if (!SafeRd(pm + 0x55c, xz, sizeof xz) || !(xz[0] == xz[0]) || !(xz[2] == xz[2]) || fabsf(xz[0]) > 1e5f || fabsf(xz[2]) > 1e5f) continue;
        g.gx = xz[0]; g.gz = xz[2];
        int32_t cur = 0, mx = 0;
        if (pi && DecodeVal(pi, 0xd8, cur) && DecodeVal(pi, 0xf8, mx) && mx > 0 && cur >= 0 && cur <= mx * 2) g.st = std::clamp((float)((int64_t)cur * 100 / mx) / 100.0f, 0.0f, 1.0f);
        out[n++] = g;
    }
    return n;
}
// transformacion juego -> mundo: ejes (cambiados o no), signos y escala; se elige la que pone a los jugadores sobre los personajes
struct Xf { bool swap = false; float sx = 1, sz = 1, k = 1; bool ok = false; };
Xf s_xf; double s_xfT = -1e9; int s_xfLog = 0;
void XfApply(const Xf& x, const GameP& g, float& wx, float& wz) { const float a = x.swap ? g.gz : g.gx, b = x.swap ? g.gx : g.gz; wx = a * x.sx * x.k; wz = b * x.sz * x.k; }
void Calibrate(const GameP* gp, int ng, const float (*cp)[2], int nc, double t) {
    if (ng < 6 || nc < 6) return;
    static const float ks[] = { 1.0f, 0.1f, 0.01f, 0.001f, 10.0f, 100.0f };
    Xf best; float bestE = 1e30f; int bestM = 0;
    for (int sw = 0; sw < 2; sw++) for (int sg = 0; sg < 4; sg++) for (float k : ks) {
        Xf x; x.swap = sw != 0; x.sx = (sg & 1) ? -1.0f : 1.0f; x.sz = (sg & 2) ? -1.0f : 1.0f; x.k = k;
        float e = 0.0f; int m = 0;
        for (int i = 0; i < ng; i++) { float wx, wz; XfApply(x, gp[i], wx, wz); float bd = 1e30f;
            for (int j = 0; j < nc; j++) { const float dx = wx - cp[j][0], dz = wz - cp[j][1]; bd = (std::min)(bd, dx * dx + dz * dz); }
            if (bd < 1.0f) m++; e += (std::min)(bd, 9.0f); }
        if (m > bestM || (m == bestM && e < bestE)) { bestM = m; bestE = e; best = x; }
    }
    const bool ok = bestM >= (std::max)(6, (std::min)(ng, nc) * 2 / 3);
    if (ok) { best.ok = true; s_xf = best; } else if (t - s_xfT > 30.0) s_xf.ok = false;
    if (ok) s_xfT = t;
    if (s_xfLog < 8) { s_xfLog++; Log("sudor 2: posicion de los jugadores del juego %s (%d de %d sobre los personajes; ejes %s, signos %+.0f %+.0f, escala %g)", ok ? "reconocida" : "NO reconocida", bestM, ng, best.swap ? "cambiados" : "iguales", best.sx, best.sz, best.k); }
}

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
    // estamina del juego (x493 = cuanto pesa; 0 = no se lee)
    const float stK = std::clamp(p[493][0], 0.0f, 2.0f);
    GameP gp[22]; int ng = stK > 0.0f ? ReadGamePlayers(gp) : 0;
    { static double calT = -1e9;
      if (ng > 0 && t - calT > 5.0) { calT = t; float cp[kCharSlots][2]; int nc = 0; float tmp[3];
          for (int i = 0; i < kCharSlots; i++) if (s_alive[i] && s_info[i].node && CharNodeWorldPos(s_info[i].node, tmp)) { cp[nc][0] = tmp[0]; cp[nc][1] = tmp[2]; nc++; }
          Calibrate(gp, ng, cp, nc, t); } }
    int nSt = 0; float stSum = 0.0f;
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
        }
        // estamina del juego de ESTE jugador (el del juego a menos de 1 m): el cansancio acumulado es el trabajo de todo el partido, asi
        // que marca un piso de sudor (con 60 % de estamina ya esta bien transpirado) y suma calor; se recupera en el entretiempo como en el juego
        float fat = -1.0f;
        if (s_xf.ok && ng > 0) { float bd = 1.0f;
            for (int k = 0; k < ng; k++) { if (gp[k].st < 0.0f) continue; float wx, wz; XfApply(s_xf, gp[k], wx, wz); const float dx = wx - pos[0], dz = wz - pos[2], d2 = dx * dx + dz * dz; if (d2 < bd) { bd = d2; fat = 1.0f - gp[k].st; } } }
        if (fat >= 0.0f) { s->fat += ((fat) - s->fat) * (s->fatOk ? 0.05f : 1.0f); s->fatOk = true; nSt++; stSum += 1.0f - s->fat; }
        float swOut = s->sweat, heatOut = s->heat;
        if (s->fatOk && stK > 0.0f) {
            const float f = std::clamp(s->fat, 0.0f, 1.0f), floorS = std::clamp((f - 0.05f) / 0.40f, 0.0f, 1.0f) * 0.85f * clima;
            swOut = std::clamp(s->sweat + ((std::max)(s->sweat, floorS) - s->sweat) * (std::min)(stK, 1.0f) + (stK > 1.0f ? (stK - 1.0f) * 0.25f * f : 0.0f), 0.0f, 1.0f);
            heatOut = std::clamp(s->heat + 0.35f * f * (std::min)(stK, 1.0f), 0.0f, 1.0f);
        }
        if (dt <= 0.0f && !s->posOk) { s->x = pos[0]; s->y = pos[1]; s->z = pos[2]; }
        const float cx = pos[0] - cam[0], cy = pos[1] + 1.0f - cam[1], cz = pos[2] - cam[2];
        out[n++] = Out{ pos[0], pos[2], swOut, heatOut, camOk ? cx * cx + cy * cy + cz * cz : (float)n };
        sum += swOut; mx = (std::max)(mx, swOut);
    }
    std::sort(out, out + n, [](const Out& a, const Out& b) { return a.d2 < b.d2; });
    const int m = (std::min)(n, kRowMax);
    for (int i = 0; i < kRowMax; i++) { float* r = p[kRowFirst + i];
        if (i < m) { r[0] = out[i].x; r[1] = out[i].z; r[2] = out[i].s; r[3] = out[i].h; } else r[0] = r[1] = r[2] = r[3] = 0.0f; }
    float* H = p[kRowHead]; H[0] = (float)m; H[1] = 1.0f; H[2] = s_rain; H[3] = n > 0 ? sum / (float)n : 0.0f;
    p[493][1] = (float)nSt; p[493][2] = nSt > 0 ? stSum / (float)nSt : 0.0f; p[493][3] = s_xf.ok ? 1.0f : 0.0f;
    if (t > s_log) { s_log = t + 60.0;
        Log("sudor 2 (60 s): %d personajes (%d en la tabla), sudor medio %.2f, maximo %.2f, lluvia %.2f, clima x%.2f, reloj del juego %s, estamina de %d jugadores (media %.0f %%, del juego %d)", n, m, H[3], mx, s_rain, clima, p[157][3] >= 0.5f ? "si" : "no (reloj real)", nSt, 100.0f * p[493][2], ng); }
}
} // namespace fx
