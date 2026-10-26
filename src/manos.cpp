// PES21 FX (25/10) - manos de TorsoBody: skinning por cuaterniones duales y formas correctivas por pose (puno, dedos que se
// apoyan entre si, nudillos, venas que se marcan al cerrar). Todo el calculo esta en el vertex shader (shaders\pes_hands.hlsli,
// incluido en el VS de la piel 1c02a92a y en el de sombras c6ef0c75); la DLL solo reconoce el dibujo de cada mano por su
// cantidad de indices (la malla arm_mat de hand_l / hand_r de TorsoBody: x302 / y302) y le enlaza en t126 los datos por vertice
// de esa mano (textures\pes_hand_l.dds / pes_hand_r.dds, generados con la malla). Cualquier otro dibujo de esos VS no recibe
// t126 y sigue con el skinning del juego. x300 = 0 apaga todo (la mano vuelve exactamente a la del juego).
// (26/10) Filas 300..302 de t120 (antes 200..202: la tabla de tonos de piel por jugador de skintone.cpp usa 194..254 y las
// borraba en cada cuadro, asi que la tarjeta no se podia prender y la DLL nunca reconocia la mano).
#include "core.h"
#include <atomic>

namespace fx {

static std::atomic<int> s_handLogs{0};

ID3D11ShaderResourceView* HandDataFor(uint64_t vsHash, UINT indexCount, UINT start, INT base) {
    if (vsHash != 0x1c02a92ab4ae3362ULL && vsHash != 0xc6ef0c7519ec95a5ULL) return nullptr;
    UINT left = (UINT)(g_cfg.live[302][0] + 0.5f), right = (UINT)(g_cfg.live[302][1] + 0.5f);
    if (g_cfg.live[300][0] < 0.5f) {   // apagadas: avisar una vez si llega una mano (asi el log dice por que no se deforma)
        static std::atomic<bool> s_offNoted{false};
        if ((indexCount == left || indexCount == right) && !s_offNoted.exchange(true)) Log("manos: llega la mano (%u indices) pero la tarjeta esta apagada (x300 = 0)", indexCount);
        return nullptr;
    }
    const char* f = nullptr;
    if (left && indexCount == left) f = "file:textures\\pes_hand_l.dds";
    else if (right && indexCount == right) f = "file:textures\\pes_hand_r.dds";
    if (!f) return nullptr;
    ID3D11ShaderResourceView* v = FileTextureAny(f);
    if (s_handLogs.load(std::memory_order_relaxed) < 12) {
        s_handLogs++;
        Log("manos: dibujo de la mano %s (VS %016llx, %u indices, inicio %u, base %d): datos %s", indexCount == left ? "izquierda" : "derecha",
            (unsigned long long)vsHash, indexCount, start, base, v ? "enlazados en t126" : "NO CARGADOS");
    }
    return v;
}

} // namespace fx
