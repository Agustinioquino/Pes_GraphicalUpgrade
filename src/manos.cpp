// PES21 FX (25/10) - manos de TorsoBody: skinning por cuaterniones duales y formas correctivas por pose (puno, dedos que se
// apoyan entre si, nudillos, venas que se marcan al cerrar). Todo el calculo esta en el vertex shader (shaders\pes_hands.hlsli,
// incluido en el VS de la piel 1c02a92a y en el de sombras c6ef0c75); la DLL solo reconoce el dibujo de cada mano por su
// cantidad de indices (la malla arm_mat de hand_l / hand_r de TorsoBody: x202 / y202) y le enlaza en t126 los datos por vertice
// de esa mano (textures\pes_hand_l.dds / pes_hand_r.dds, generados con la malla). Cualquier otro dibujo de esos VS no recibe
// t126 y sigue con el skinning del juego. x200 = 0 apaga todo (la mano vuelve exactamente a la del juego).
#include "core.h"
#include <atomic>

namespace fx {

static std::atomic<int> s_handLogs{0};

ID3D11ShaderResourceView* HandDataFor(uint64_t vsHash, UINT indexCount, UINT start, INT base) {
    if (vsHash != 0x1c02a92ab4ae3362ULL && vsHash != 0xc6ef0c7519ec95a5ULL) return nullptr;
    if (g_cfg.live[200][0] < 0.5f) return nullptr;
    UINT left = (UINT)(g_cfg.live[202][0] + 0.5f), right = (UINT)(g_cfg.live[202][1] + 0.5f);
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
