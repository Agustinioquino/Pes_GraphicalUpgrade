// PES21 FX (08/11) - GESTOS DEL PUBLICO con huesos de cara reales (pedido de Agustin: "huesos reales por dll" y "que el cpu
// tambien ayude"). Hilo 7nx0cr.
//
// Como esta armado (separado como pidio Agustin: lo tecnico del modelo en Sider, el resto en la DLL):
//  - Sider (livecpk, Asset\model\bg\common\audi\#Win\audi_model.fpk): los 14 au*.fmdl con los pesos de 3 huesos de la cara
//    (mandibula, ceja izquierda, ceja derecha) en el COLOR del vertice (tools/crowd/pesos_cara.py). Sin la DLL se ven como antes.
//  - GPU: los 6 VS del publico reemplazados (traduccion exacta del juego, tools/crowd/dxbc2hlsl_bits.py) llaman a
//    PesCrowdFace (shaders/pes_crowd_face.hlsli) antes del skinning: la mandibula gira y las cejas suben como huesos hijos
//    del cuello; despues el juego aplica su animacion y el giro de la cabeza de cada hincha.
//  - CPU (este archivo): un hilo propio calcula el gesto de cada hincha (hasta kMax) en un buffer float4 (x giro de la
//    mandibula en rad, y / z subida de las cejas en m) y el Present lo sube a la GPU (recurso "pes_crowdface"; la DLL lo pone en t30 de los 6 VS, PublicoSRVFor).
//    El indice es el del juego en instanceBuf: cada hincha tiene siempre el suyo, asi que cada uno tiene ritmo propio.
//
// Fila 394: x on | y fuerza de la boca | z fuerza de las cejas | w animo (0 tranquilo .. 1 normal .. 2 cancha que canta todo)
// (08/11 b) Reacciones: el VS lee la animacion del cuerpo de cada hincha (festejo, lamento, protesta, aplauso) y cambia la
// cara; la CPU le pasa en w el pulso del grito propio de cada uno.
#include "core.h"
#include <thread>
#include <atomic>
#include <vector>
#include <cmath>
#include <chrono>

namespace fx {

static constexpr UINT kMax = 32768;                       // hinchas con gesto propio (512 KB por cuadro)
static ID3D11Buffer* s_buf = nullptr;
static ID3D11ShaderResourceView* s_srv = nullptr;
static bool s_failed = false;
static std::vector<float> s_bank[2];                      // doble buffer: el hilo escribe uno mientras el Present sube el otro
static std::atomic<int> s_ready{-1};                      // banco listo para subir (-1 = ninguno)
static std::atomic<bool> s_run{false};
static std::atomic<double> s_time{0.0};
static std::atomic<float> s_mood{1.0f};
static std::atomic<int> s_uploads{0};
static double s_lastLog = 0;

static inline float Hash01(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (x & 0xffffff) / 16777216.0f;
}
static inline float Smooth(float a, float b, float x) { float t = (x - a) / (b - a); t = t < 0 ? 0 : (t > 1 ? 1 : t); return t * t * (3 - 2 * t); }

// gesto de un hincha en el tiempo t (s). Grupos de 48 indices seguidos cantan juntos (el juego arma las tribunas por bloques),
// cada uno con su tempo; dentro del grupo cada hincha tiene su desfase y su "ganas" que cambian lento.
static void Gesture(uint32_t i, double t, float mood, float* out) {
    const float kTau = 6.2831853f;
    uint32_t g = i / 48u;
    float gTempo = 1.1f + 0.9f * Hash01(g * 7919u + 1);           // golpes por segundo del canto de ese bloque
    float gPhase = Hash01(g * 104729u + 3);
    // el bloque canta a ratos: onda lenta de 20..45 s
    float gPer = 20.0f + 25.0f * Hash01(g * 31u + 5);
    float gSing = Smooth(0.15f, 0.55f, 0.5f + 0.5f * sinf((float)(kTau * (t / gPer + Hash01(g * 17u + 9)))));
    gSing = gSing * (0.35f + 0.65f * (mood > 1 ? 1.0f : mood)) + (mood > 1 ? (mood - 1.0f) * (1.0f - gSing) : 0.0f);
    float my = Hash01(i * 2654435761u + 11);
    float ganas = Smooth(0.2f, 0.8f, 0.5f + 0.5f * sinf((float)(kTau * (t / (9.0f + 14.0f * my) + my))));
    float sing = gSing * (0.45f + 0.55f * ganas);
    // canto: la boca abre en cada golpe (pulso redondeado), con un poco de desfase propio
    float ph = (float)(t * gTempo) + gPhase + 0.06f * (my - 0.5f);
    float pulse = 0.5f + 0.5f * sinf(kTau * ph);
    pulse = pulse * pulse * (3 - 2 * pulse);
    float jaw = sing * (0.10f + 0.20f * pulse);
    // charla / murmullo cuando no canta: boca chica e irregular
    float talkF = 3.0f + 2.5f * Hash01(i * 97u + 13);
    float talk = 0.5f + 0.5f * sinf((float)(kTau * (t * talkF + my))) * sinf((float)(kTau * (t * 0.37 + my * 3.1)));
    float chat = (1.0f - sing) * Smooth(0.55f, 0.95f, Hash01(i * 13u + (uint32_t)(t / 6.0) * 7u)) * 0.07f * talk;
    jaw += chat;
    // cejas: suben con el esfuerzo del canto y de vez en cuando por su cuenta (sorpresa / queja), algo asimetricas
    float lift = sing * (0.0015f + 0.0040f * pulse);
    float evt = Smooth(0.92f, 1.0f, Hash01(i * 7u + (uint32_t)(t / 3.0) * 131u)) * 0.006f;
    float asym = 0.75f + 0.5f * Hash01(i * 5u + 77u);
    out[0] = jaw * (0.7f + 0.6f * mood * 0.5f);
    out[1] = (lift + evt) * asym;
    out[2] = (lift + evt) * (2.0f - asym);
    // pulso del grito propio (lo usan las reacciones del VS: festejo / protesta): golpes de 2.2..4 por segundo, irregulares
    float shF = 2.2f + 1.8f * Hash01(i * 211u + 29u);
    float sh = 0.5f + 0.5f * sinf((float)(kTau * (t * shF + my * 5.3)));
    float shGate = Smooth(0.25f, 0.6f, 0.5f + 0.5f * sinf((float)(kTau * (t * 0.45 + my * 2.7))));
    out[3] = sh * sh * (0.35f + 0.65f * shGate);
}

static void Worker() {
    for (auto& b : s_bank) b.assign(kMax * 4, 0.0f);
    int last = -1; double lastT = -1;
    while (s_run.load()) {
        double t = s_time.load();
        if (t == lastT) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
        lastT = t;
        int b = (last == 0) ? 1 : 0;
        if (s_ready.load() == b) b = 1 - b;           // no pisar el banco que espera subirse
        float mood = s_mood.load();
        float* d = s_bank[b].data();
        for (uint32_t i = 0; i < kMax; i++) Gesture(i, t, mood, d + 4 * i);
        s_ready = b; last = b;
    }
}

static void Ensure() {
    if (s_buf || s_failed || !g_dev.dev) return;
    D3D11_BUFFER_DESC d = {};
    d.ByteWidth = kMax * 16; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = 16;
    std::vector<float> zero(kMax * 4, 0.0f);
    D3D11_SUBRESOURCE_DATA init = { zero.data(), 0, 0 };
    if (FAILED(g_dev.dev->CreateBuffer(&d, &init, &s_buf))) { s_failed = true; Log("publico: no se pudo crear el buffer de gestos"); return; }
    D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
    v.Format = DXGI_FORMAT_UNKNOWN; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; v.Buffer.FirstElement = 0; v.Buffer.NumElements = kMax;
    if (FAILED(g_dev.dev->CreateShaderResourceView(s_buf, &v, &s_srv))) { s_failed = true; Log("publico: no se pudo crear la vista del buffer"); return; }
    NamedSetSRV("pes_crowdface", s_srv);   // nombre para el inspector; el enlace lo hace PublicoSRVFor desde hooks.cpp
    s_run = true;
    std::thread(Worker).detach();   // sin join: vive hasta que se cierra el juego (un std::thread estatico unido al salir termina el proceso)
    Log("publico: gestos de %u hinchas en un hilo de la CPU (recurso pes_crowdface, t30 en los VS del publico)", kMax);
}

// la DLL pone el buffer en t30 de los 6 VS del publico (no se usa PES21FX_rules.ini, que no se toca)
ID3D11ShaderResourceView* PublicoSRVFor(uint64_t h) {
    if (!s_srv || g_cfg.live[394][0] <= 0.0f) return nullptr;
    switch (h) {
    case 0x244144d8b2ef94caULL: case 0x37b31aa5bf052258ULL: case 0x3d8a52c2004d58bbULL:
    case 0x82f14de823c94c87ULL: case 0x8b0ba18c8024bbb1ULL: case 0xec39a17a68dcbac3ULL: return s_srv;
    default: return nullptr;
    }
}

void PublicoPresent() {
    float on = g_cfg.live[394][0];
    if (on <= 0.0f) return;
    Ensure();
    if (!s_buf || !g_dev.imm) return;
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    t = fmod(t, 100000.0);
    s_mood = g_cfg.live[394][3];
    int b = s_ready.exchange(-1);
    if (b >= 0) { g_dev.imm->UpdateSubresource(s_buf, 0, nullptr, s_bank[b].data(), 0, 0); s_uploads++; }
    s_time = t;   // pide el cuadro siguiente al hilo (se sube en el proximo Present: un cuadro de atraso, sin esperas)
    if (t - s_lastLog > 30.0 || t < s_lastLog) {
        s_lastLog = t;
        Log("publico: %d subidas del buffer de gestos en 30 s (boca %.2f, cejas %.2f, animo %.2f)", s_uploads.exchange(0), g_cfg.live[394][1], g_cfg.live[394][2], g_cfg.live[394][3]);
    }
}

}  // namespace fx
