// (15/11 m) motorsound.cpp: sonido de la transicion del Motor. Agustin 04:20: "un efecto de sonido de distorcion glitch rapido, uno al
// entrar y uno con un tono un poco distinto al salir".
// El sonido se fabrica en memoria (no hay archivos que instalar) y suena con PlaySound de winmm, que se carga a mano como el mando
// en menuvisual.cpp. Suena en un hilo aparte para no frenar el cuadro la primera vez que Windows abre el audio.
//   entrar (0): barrido que sube (de grave a agudo), mas corto y brillante.
//   salir  (1): el mismo tipo de sonido con el barrido bajando y todo un poco mas grave.
// Como esta hecho (las mismas cuentas en los dos): se corta en pedacitos de 9 a 34 ms; cada pedacito es un tono cuadrado del barrido,
// un golpe de ruido, una repeticion del pedacito anterior (tartamudeo) o un hueco corto. Despues pasa por reduccion de bits y de
// muestras (la distorsion digital), un golpe grave al principio y una saturacion suave. Siempre sale igual (semilla fija).
// [PES21FX] motor_sonido = 1 (0 lo apaga), motor_sonido_volumen = 0..100 (70 de fabrica).
#ifndef MOTORSOUND_TEST
#include "core.h"
#endif
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <mutex>
#include <thread>
#include <algorithm>

namespace fx {

static inline uint32_t SndHash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
static inline float SndRnd(uint32_t& s) { s = SndHash(s + 0x9e3779b9u); return (float)(s & 0xffffff) / 16777215.0f; }

// arma el .wav entero (cabecera RIFF + PCM 16 bits mono 44100 Hz) en out
void MotorGlitchWav(int which, float vol, std::vector<uint8_t>& out) {
    const int SR = 44100; const bool exitS = which != 0;
    const float dur = exitS ? 0.46f : 0.40f, pf = exitS ? 0.74f : 1.0f;
    const int N = (int)(dur * (float)SR);
    std::vector<float> v((size_t)N, 0.0f);
    uint32_t seed = exitS ? 0x5a17e0u : 0x00e47e2u;
    double ph = 0.0, ph2 = 0.0; float lp = 0.0f;
    int i = 0, prevA = 0, prevLen = 0;
    while (i < N) {
        const int len = std::min(N - i, (int)((0.009f + 0.025f * SndRnd(seed)) * (float)SR));
        const float r = SndRnd(seed);
        const int mode = i == 0 ? 0 : r < 0.50f ? 0 : r < 0.72f ? 1 : r < 0.90f ? 2 : 3;      // 0 tono, 1 ruido, 2 repetir, 3 hueco
        const int hold = 1 + (int)(SndRnd(seed) * 9.0f);                                      // reduccion de muestras del pedacito
        const float lev = 10.0f + 40.0f * SndRnd(seed);                                       // niveles (reduccion de bits)
        const float det = 1.0f + (SndRnd(seed) - 0.5f) * 0.5f;                                // cada pedacito un poco desafinado
        const int aStart = i; float held = 0.0f;
        for (int j = 0; j < len; j++, i++) {
            const float x = (float)i / (float)N;
            const float f = exitS ? 1300.0f * pf * std::pow(2.0f, -3.4f * x) : 190.0f * pf * std::pow(2.0f, 3.1f * x);
            float s = 0.0f;
            if (mode == 0) {
                ph += (double)(f * det) / SR; ph2 += (double)(f * det * 0.5f) / SR;
                const float sq = std::fmod(ph, 1.0) < 0.5 ? 1.0f : -1.0f, saw = (float)(2.0 * std::fmod(ph2, 1.0) - 1.0);
                s = 0.62f * sq + 0.38f * saw;
            } else if (mode == 1) {
                const float n = SndRnd(seed) * 2.0f - 1.0f; const float c = std::min(0.95f, f / 2600.0f + 0.12f);
                lp += (n - lp) * c; s = lp * 1.5f;
            } else if (mode == 2 && prevLen > 0) {
                s = v[(size_t)(prevA + (j % prevLen))] * 0.9f;
            }
            if (j % hold == 0) held = s;                                                       // mantener la muestra = menos muestras
            s = std::round(held * lev) / lev;                                                  // menos bits
            const float e = std::min(1.0f, (float)j / 44.0f) * std::min(1.0f, (float)(len - j) / 44.0f);   // sin chasquidos en los cortes
            v[(size_t)i] = s * e;
        }
        if (mode != 2) { prevA = aStart; prevLen = len; }
    }
    double thump = 0.0; float lo = 0.0f;                                                       // lo: paso bajo suave (~9 kHz) para que no raspe
    for (int n = 0; n < N; n++) {
        const float x = (float)n / (float)N, t = (float)n / (float)SR;
        const float env = std::min(1.0f, t / 0.004f) * std::pow(1.0f - x, exitS ? 1.0f : 1.2f);
        thump += (double)((exitS ? 52.0f : 64.0f) * (1.0f - 0.5f * x)) / SR;
        const float th = (float)std::sin(6.283185307 * thump) * std::exp(-t / (exitS ? 0.07f : 0.05f)) * 0.5f;
        lo += (v[(size_t)n] - lo) * 0.72f;
        float s = lo * env * 0.8f + th;
        s = std::tanh(1.8f * s) / std::tanh(1.8f);                                             // saturacion suave
        v[(size_t)n] = s;
    }
    const float g = std::clamp(vol, 0.0f, 1.0f) * 0.5f;
    const uint32_t dataB = (uint32_t)N * 2u; out.assign(44 + dataB, 0);
    auto w32 = [&](size_t o, uint32_t x) { memcpy(&out[o], &x, 4); }; auto w16 = [&](size_t o, uint16_t x) { memcpy(&out[o], &x, 2); };
    memcpy(&out[0], "RIFF", 4); w32(4, 36 + dataB); memcpy(&out[8], "WAVE", 4); memcpy(&out[12], "fmt ", 4); w32(16, 16); w16(20, 1); w16(22, 1);
    w32(24, (uint32_t)SR); w32(28, (uint32_t)SR * 2u); w16(32, 2); w16(34, 16); memcpy(&out[36], "data", 4); w32(40, dataB);
    for (int n = 0; n < N; n++) { const int q = (int)std::lround(std::clamp(v[(size_t)n] * g, -1.0f, 1.0f) * 32767.0f); w16(44 + (size_t)n * 2, (uint16_t)(int16_t)q); }
}

#ifndef MOTORSOUND_TEST
void MotorGlitchSound(int which) {
    if (g_cfg.motorSound == 0) return;
    which = which ? 1 : 0;
    std::thread([which] {
        static std::mutex mx; std::lock_guard<std::mutex> lk(mx);
        static std::vector<uint8_t> wav[2]; static int volMade[2] = { -1, -1 };
        typedef BOOL(WINAPI* PFN_PS)(LPCWSTR, HMODULE, DWORD); static PFN_PS ps = nullptr; static bool tried = false;
        if (!tried) { tried = true; HMODULE w = LoadLibraryW(L"winmm.dll"); if (w) ps = (PFN_PS)GetProcAddress(w, "PlaySoundW");
            Log("motor (sonido): winmm %s", ps ? "lista" : "NO se pudo cargar (sin sonido)"); }
        if (!ps) return;
        const int vol = std::clamp(g_cfg.motorSoundVol, 0, 100);
        if (volMade[which] != vol) { if (volMade[which] >= 0) ps(nullptr, nullptr, 0); MotorGlitchWav(which, (float)vol / 100.0f, wav[which]); volMade[which] = vol; }
        // SND_MEMORY 0x4 | SND_ASYNC 0x1 | SND_NODEFAULT 0x2: el buffer queda vivo (static) mientras suena
        ps((LPCWSTR)wav[which].data(), nullptr, 0x0004 | 0x0001 | 0x0002);
    }).detach();
}
#endif

}  // namespace fx
