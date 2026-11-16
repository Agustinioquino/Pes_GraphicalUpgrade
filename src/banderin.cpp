// PES21 FX - banderin del corner con el viento unico (14/11/2026, hilo del viento sk9ema).
// Agustin: "como le aplicamos el viento global al banderin sin hacer que se separe", "al bajar el viento el movimiento tiene que ser
// poco y caerse la tela", "deberia tener arrugas con normales ademas, y no usar mucho rendimiento".
//
// Como lo dibuja el juego (leido en su PC, solo lectura): los 4 banderines (escena cf_common_pes2019A.fox2, sin giro, en (+-52.48, 0,
// +-33.99)) son el modelo cf_common_2019E_vatB.fmdl (o cf_<estadio>_vat00: misma tela, mismo lugar). La malla "cloth" es una grilla de
// 13 x 9 puntos (BLENDINDICES.x = fila * 13 + columna, columna 12 = pegada al palo) que los VS ca62006b9e91c070 (principal) y
// ad793f5286a2e699 (sombra / profundidad) mueven leyendo dos texturas grabadas (VAT, 128 x 256: 256 cuadros a 30 por segundo). El palo
// es otra malla, fija, con otro shader. El .gani (cf_common_2019A_cflag.gani) es del modelo viejo de huesos y no mueve estos.
//
// Aca: una tela simulada (banderin_sim.h: 117 puntos, columna del palo clavada, gravedad + viento unico de la fila 494) que se sube en
// un buffer chico (pes_flag, t111 de esos dos VS). Los reemplazos de los VS (pes_flag.hlsli) leen de ahi la posicion y el marco para
// la luz en vez de la textura grabada. Sin el buffer (opcion apagada, viento unico apagado, DLL vieja) los VS hacen lo del juego.
// No se toca ningun archivo del juego ni memoria del exe. Costo: ~0.07 ms de CPU por cuadro, y solo mientras hay un banderin en cuadro.
//
// Fila 514 (ini / menu Viento): x tela propia on / off, y cuanto viento le llega, z peso de la tela, w arrugas.
#include "core.h"
#include "banderin_sim.h"
#include <d3d11.h>
#include <atomic>
#include <chrono>

namespace fx {
namespace {
constexpr uint64_t kVsMain = 0xca62006b9e91c070ULL, kVsShadow = 0xad793f5286a2e699ULL;
constexpr int kCols = 128, kRows = 4;                 // [fila * 128 + indice de la malla]: 0 posicion (w arruga), 1 eje x, 2 eje z (normal), 3 posicion anterior
constexpr float kMark = 515.0f;                       // [127].x: el buffer es valido (sin enlazar se lee 0 y el VS hace lo del juego)
FlagSim s_sim;
ID3D11Buffer* s_buf = nullptr; ID3D11ShaderResourceView* s_srv = nullptr; bool s_failed = false;
std::atomic<bool> s_live{false};                      // hay datos subidos de este cuadro o de uno reciente
std::atomic<int> s_seen{-100000}, s_draws{0};         // ultimo Present con un banderin dibujado; dibujos desde el ultimo log
float s_data[kCols * kRows][4] = {};
bool s_havePrev = false;

void Ensure() {
    if (s_buf || s_failed || !g_dev.dev) return;
    D3D11_BUFFER_DESC d = {};
    d.ByteWidth = kCols * kRows * 16; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = 16;
    D3D11_SUBRESOURCE_DATA init = { s_data, 0, 0 };
    if (FAILED(g_dev.dev->CreateBuffer(&d, &init, &s_buf))) { s_failed = true; Log("banderin: no se pudo crear el buffer de la tela"); return; }
    D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
    v.Format = DXGI_FORMAT_UNKNOWN; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; v.Buffer.FirstElement = 0; v.Buffer.NumElements = kCols * kRows;
    if (FAILED(g_dev.dev->CreateShaderResourceView(s_buf, &v, &s_srv))) { s_failed = true; s_buf->Release(); s_buf = nullptr; Log("banderin: no se pudo crear la vista del buffer"); return; }
    NamedSetSRV("pes_flag", s_srv);   // nombre para el inspector; el enlace lo hace BanderinSRVFor desde hooks.cpp
    Log("banderin: tela propia lista (117 puntos, recurso pes_flag en t111 de los VS ca62006b9e91c070 / ad793f5286a2e699)");
}

void Fill() {
    for (int v = 0; v < FlagSim::NV; v++) for (int u = 0; u < FlagSim::NU; u++) {
        const int i = FlagSim::I(u, v), m = FlagSim::MeshIdx(u, v);
        float* P = s_data[m]; float* X = s_data[kCols + m]; float* Z = s_data[2 * kCols + m]; float* Q = s_data[3 * kCols + m];
        if (s_havePrev) { Q[0] = P[0]; Q[1] = P[1]; Q[2] = P[2]; }
        P[0] = s_sim.oP[i].x; P[1] = s_sim.oP[i].y; P[2] = s_sim.oP[i].z; P[3] = s_sim.crease[i];
        X[0] = s_sim.oX[i].x; X[1] = s_sim.oX[i].y; X[2] = s_sim.oX[i].z; X[3] = 0.0f;
        Z[0] = s_sim.oZ[i].x; Z[1] = s_sim.oZ[i].y; Z[2] = s_sim.oZ[i].z; Z[3] = 0.0f;
        if (!s_havePrev) { Q[0] = P[0]; Q[1] = P[1]; Q[2] = P[2]; }
        Q[3] = 0.0f;
    }
    s_havePrev = true;
    s_data[kCols - 1][0] = kMark; s_data[kCols - 1][1] = (float)FlagSim::N; s_data[kCols - 1][2] = s_data[kCols - 1][3] = 0.0f;
}
} // namespace

// hooks.cpp (hilos del juego), antes de cada dibujo con un VS reemplazado: el buffer de la tela para los dos VS del banderin
ID3D11ShaderResourceView* BanderinSRVFor(uint64_t h) {
    if (h != kVsMain && h != kVsShadow) return nullptr;
    s_seen.store(PresentNo(), std::memory_order_relaxed);
    if (h == kVsMain) s_draws.fetch_add(1, std::memory_order_relaxed);
    return s_live.load(std::memory_order_relaxed) ? s_srv : nullptr;
}

void BanderinPresent() {
    const float (*p)[4] = g_cfg.live;
    static double lastGame = -1.0, nextLog = 0.0;
    static int steps = 0, frames = 0; static double usSum = 0.0; static bool wasOn = false;
    // avance del reloj del juego (157.x, como wind.cpp): en pausa no avanza y la tela queda como esta; en camara lenta va lenta
    double g = (double)p[157][0], dG = 0.0;
    if (lastGame >= 0.0) { dG = g - lastGame; if (dG < -50000.0) dG += 100000.0; dG = std::clamp(dG, 0.0, 0.1); }
    lastGame = g;
    // prendido: x514 y el viento unico andando (z353 / w353: sin el no hay dato de viento y el banderin queda como en el juego)
    // (si el ini todavia no trae la fila 514 vale lo de fabrica, como en wind.cpp: 1 / 1 / 1 / 1)
    auto P = [&](int k, float def) { return (!g_cfg.info[514][k].defined && p[514][k] == 0.0f) ? def : p[514][k]; };
    const bool on = P(0, 1.0f) > 0.5f && g_windNow.on;
    if (on != wasOn) { wasOn = on; Log("banderin: tela propia %s (x514 %.0f, viento unico %s)", on ? "prendida" : "apagada: flameo grabado del juego", P(0, 1.0f), g_windNow.on ? "prendido" : "apagado"); }
    if (!on) { s_live.store(false, std::memory_order_relaxed); return; }
    Ensure();
    if (!s_buf || !g_dev.imm) { s_live.store(false, std::memory_order_relaxed); return; }
    // dormida: sin banderin en cuadro hace mas de 2 s no se simula nada (el buffer queda con lo ultimo)
    const int idle = PresentNo() - s_seen.load(std::memory_order_relaxed);
    if (idle > 120 && s_sim.ready) { s_live.store(true, std::memory_order_relaxed); return; }
    // viento en la tela: fila 494 (rumbo en el suelo y m/s a 1.8 m, ya con rachas, hora, estadio y bajo techo) a la altura del
    // banderin (1.4 m) y con su ganancia. Los 4 banderines no estan girados: los ejes del modelo son los del mundo
    const float gain = std::clamp(P(1, 1.0f), 0.0f, 3.0f);
    const float U = (std::max)(p[494][2], 0.0f) * WindHeightK(g_windHeight, 1.4f) * gain;
    const FlagSim::V3 w{ p[494][0] * U, p[511][0] * gain, p[494][1] * U };
    const bool rain = g_windNow.rain;
    s_sim.weight = std::clamp(P(2, 1.0f), 0.4f, 3.0f) * (rain ? 1.3f : 1.0f);   // mojada pesa mas y flamea menos
    s_sim.flutter = rain ? 0.8f : 1.0f;
    s_sim.wrinkle = std::clamp(P(3, 1.0f), 0.0f, 3.0f);
    const auto t0 = std::chrono::steady_clock::now();
    const int n = s_sim.Step((float)dG, w, std::clamp(p[495][1], 0.0f, 1.0f));
    if (dG > 0.0 || !s_havePrev) {
        Fill();
        g_dev.imm->UpdateSubresource(s_buf, 0, nullptr, s_data, 0, 0);
    } else {
        // pausa: el cuadro anterior es el mismo que el actual (sin estela en el desenfoque de movimiento)
        bool ch = false;
        for (int m = 0; m < FlagSim::N; m++) for (int k = 0; k < 3; k++) if (s_data[3 * kCols + m][k] != s_data[m][k]) { s_data[3 * kCols + m][k] = s_data[m][k]; ch = true; }
        if (ch) g_dev.imm->UpdateSubresource(s_buf, 0, nullptr, s_data, 0, 0);
    }
    usSum += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count(); steps += n; frames++;
    s_live.store(true, std::memory_order_relaxed);
    const double now = Now();
    if (now >= nextLog) {
        nextLog = now + 60.0;
        // caida de la punta del medio respecto de la horizontal (90 = colgando, 0 = estirada)
        const FlagSim::V3 tip = FlagSim::sub(s_sim.p[FlagSim::I(FlagSim::NU - 1, FlagSim::NV / 2)], FlagSim::Rest(0, FlagSim::NV / 2));
        const float droop = atan2f(-tip.y, sqrtf(tip.x * tip.x + tip.z * tip.z)) * 57.2958f;
        Log("banderin: viento %.2f m/s en la tela (x%.2f), tela a %.0f grados de caida, peso %.2f%s, arrugas %.2f; en 60 s: %d dibujos, %d cuadros simulados, %d pasos, %.0f us por cuadro",
            U, gain, droop, s_sim.weight, rain ? " (mojada)" : "", s_sim.wrinkle, s_draws.exchange(0), frames, steps, frames ? usSum / frames : 0.0);
        steps = 0; frames = 0; usSum = 0.0;
    }
}
} // namespace fx
