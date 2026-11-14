// prueba suelta de la tela del banderin (g++ -O2 -I src tools/sim/banderin_test.cpp -o /tmp/bt && /tmp/bt)
#include "banderin_sim.h"
#include <cstdio>
#include <chrono>
using namespace fx;
int main() {
    { FlagSim s; s.wrinkle = 0.0f; s.Reset(); double e = 0; for (int i = 0; i < FlagSim::N; i++) { e = std::max(e, (double)std::fabs(s.oZ[i].z - 1)); e = std::max(e, (double)std::fabs(s.oX[i].x - 1)); e = std::max(e, (double)FlagSim::len(FlagSim::sub(s.oP[i], FlagSim::Rest(i % 13, i / 13)))); }
      std::printf("reposo: error maximo de marco / posicion %.6f (0 = normal (0,0,1), eje x (1,0,0), sin arrugas en reposo)\n", e); }
    const float Us[] = { 0.0f, 0.5f, 1.0f, 2.0f, 3.0f, 5.0f, 8.0f, 12.0f, 20.0f };
    for (float U : Us) for (int dir = 0; dir < 2; dir++) {
        FlagSim s; s.Reset();
        FlagSim::V3 w = dir == 0 ? FlagSim::V3{ -U, 0, 0 } : FlagSim::V3{ 0.6f * U, 0, 0.8f * U };
        double maxWr = 0, minNz = 1; double maxStretch = 0, sumDroop = 0, sumAmp = 0, minY = 9, maxSpeed = 0, pinErr = 0; int cnt = 0; float tipPrevZ = 0; double zc = 0; int cross = 0;
        auto t0 = std::chrono::steady_clock::now(); int steps = 0;
        for (int f = 0; f < 60 * 30; f++) {
            steps += s.Step(1.0f / 60.0f, w, 0.5f);
            if (f < 60 * 10) continue;
            for (int v = 0; v < FlagSim::NV; v++) {
                pinErr = std::max(pinErr, (double)FlagSim::len(FlagSim::sub(s.p[FlagSim::I(0, v)], FlagSim::Rest(0, v))));
                for (int u = 0; u + 1 < FlagSim::NU; u++) { double l = FlagSim::len(FlagSim::sub(s.p[FlagSim::I(u + 1, v)], s.p[FlagSim::I(u, v)])) / (FlagSim::ColX(u) - FlagSim::ColX(u + 1)); maxStretch = std::max(maxStretch, l); }
                for (int u = 0; u < FlagSim::NU; u++) { minY = std::min(minY, (double)s.p[FlagSim::I(u, v)].y); maxSpeed = std::max(maxSpeed, (double)FlagSim::len(FlagSim::sub(s.p[FlagSim::I(u, v)], s.q[FlagSim::I(u, v)])) * 120.0); }
            }
            for (int i = 0; i < FlagSim::N; i++) { maxWr = std::max(maxWr, (double)FlagSim::len(FlagSim::sub(s.oP[i], s.p[i]))); double l = FlagSim::len(s.oZ[i]); minNz = std::min(minNz, l); if (i % 13 == 0) pinErr = std::max(pinErr, (double)FlagSim::len(FlagSim::sub(s.oP[i], FlagSim::Rest(0, i / 13)))); }
            // punta del medio: angulo de caida respecto de la horizontal y vaiven de costado al viento
            FlagSim::V3 tip = FlagSim::sub(s.p[FlagSim::I(12, 4)], FlagSim::Rest(0, 4));
            double hor = std::sqrt(tip.x * tip.x + tip.z * tip.z); sumDroop += std::atan2(-tip.y, hor) * 57.2958;
            FlagSim::V3 wd = U > 0 ? FlagSim::mul(w, 1 / U) : FlagSim::V3{ -1, 0, 0 }; double side = tip.x * wd.z - tip.z * wd.x;
            sumAmp += std::fabs(side - zc); zc += (side - zc) * 0.02; if ((side - zc) * tipPrevZ < 0) cross++; tipPrevZ = (float)(side - zc); cnt++;
        }
        double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / (60.0 * 30);
        std::printf("U %5.1f dir %d: caida %6.1f grados, vaiven %.3f m, %.1f Hz, estira %.3f, y min %.2f, vel max %.1f m/s, palo %.6f, arruga max %.4f m, |n| min %.3f, %.1f us/cuadro (%d pasos)\n",
            U, dir, sumDroop / cnt, sumAmp / cnt, cross / 2.0 / 20.0, maxStretch, minY, maxSpeed, pinErr, maxWr, minNz, us, steps);
    }
}
