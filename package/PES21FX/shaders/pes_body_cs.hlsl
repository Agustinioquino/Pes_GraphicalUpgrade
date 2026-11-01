// PES21 FX (25/10) - el cuerpo real para la fisica de la ropa (cuerpo.cpp: lote una vez por cuadro, en Present).
// Cada grupo deforma 64 vertices de un dibujo de la piel del cuerpo (VS 1c02a92a: torso de TorsoBody, brazos, cuello) con los
// huesos de ESE dibujo (copiados en el dibujo: actuales y del cuadro anterior) en el espacio del modelo, igual que el VS de la
// piel, y los deja:
//   - por vertice (anillo u0): posicion (3 x 16 bits en una caja de 3.2 x 4 x 3.2 m), normal (octaedro 2 x 16 bits) y lo que se
//     movio desde el cuadro anterior (3 x half);
//   - en la tabla de su personaje (la eligio pes_body_assign_cs.hlsl): tabla hash de celdas de 2.5 cm con listas de vertices
//     (u1 = siguiente), para que el simulador de la ropa encuentre la piel cercana a cada punto de tela.
// (26/10) variantes robusto / atletico de TorsoBody: la forma grande del torso del jugador (pes_variants.hlsli, t120 enlazado por
// cuerpo.cpp) se suma antes del skinning, asi la tela choca con la panza / el pecho / la espalda que se ven.
// Las constantes tienen que coincidir con cuerpo.cpp, pes_body_assign_cs.hlsl y cloth_sim.hlsl.
#define BODY_CELLS  8192
#define BODY_CELL   0.025
#define BODY_STRIDE (16 + BODY_CELLS)
#define REC_F4      208
#define NONE        0xffffffffu

cbuffer BodyCB : register(b0)
{
    uint g_mode;
    uint g_nrec;
    uint g_stamp;
    uint g_ngroups;
};
StructuredBuffer<uint4> g_sv     : register(t0);   // mallas registradas: 2 x uint4 por vertice (posicion + huesos, normal + pesos)
Buffer<uint4>           g_desc   : register(t1);   // 2 por registro: (lugar, primer vertice propio, vertices, primer vertice del anillo), (malla, tipo: 1 camiseta, ...)
Buffer<float4>          g_rec    : register(t2);   // por lugar: [4..12] b5, [16..207] b6 (hueso i: filas 3i..3i+2 actuales, 96 + 3i.. anteriores)
Buffer<uint2>           g_grp    : register(t3);   // por grupo: registro, primer vertice
RWStructuredBuffer<uint4> g_ov   : register(u0);
RWStructuredBuffer<uint>  g_on   : register(u1);
RWBuffer<uint>          g_tab    : register(u2);
RWBuffer<uint>          g_recTab : register(u3);
Texture1D<float4>       IniParams : register(t120);   // (26/10) parametros del mod (tabla de jugadores y variantes)
#include "pes_skintone.hlsli"
#include "pes_variants.hlsli"

uint BodyHash(int3 c) { return ((uint)c.x * 73856093u ^ (uint)c.y * 19349663u ^ (uint)c.z * 83492791u) & (BODY_CELLS - 1); }
float2 OctEncode(float3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-8);
    float2 o = n.xy;
    if (n.z < 0.0) o = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return o;
}

[numthreads(64, 1, 1)]
void main(uint3 gid : SV_GroupID, uint gi : SV_GroupIndex)
{
    if (gid.x >= g_ngroups) return;
    uint2 gr = g_grp[gid.x];
    uint t = g_recTab[gr.x];
    if (t == NONE) return;
    uint4 d0 = g_desc[2 * gr.x], d1 = g_desc[2 * gr.x + 1];
    const bool shirt = d1.y == 1u;   // (27/10 d) camiseta (solo para la medicion de cuerpo.cpp): se deforma pero no entra en la tabla
    uint v = gr.y + gi;
    if (v >= d0.z) return;
    uint rb = d0.x * REC_F4;
    uint4 a = g_sv[2 * (d0.y + v)], b = g_sv[2 * (d0.y + v) + 1];
    float3 p = asfloat(a.xyz), n = asfloat(b.xyz);
    if (!shirt) {   // (26/10) variante del torso del personaje de esta tabla (su origen en el mundo -> entrada de la tabla de jugadores)
        uint hb = t * BODY_STRIDE;
        float3 org = float3(asfloat(g_tab[hb + 4]), asfloat(g_tab[hb + 5]), asfloat(g_tab[hb + 6]));
        p += PesTorsoField(p, PesVariantT(PesSkinNearest(org)));
    }
    uint4 bi = min(uint4(a.w & 255, (a.w >> 8) & 255, (a.w >> 16) & 255, a.w >> 24), 31u) * 3 + rb + 16;
    float4 bw = float4(b.w & 255, (b.w >> 8) & 255, (b.w >> 16) & 255, b.w >> 24) / 255.0;
    float3 y = p, yp = p, nn = n;
    if (g_rec[rb + 12].x != 0.0) {   // m_useWeightCount
        float4 r0 = g_rec[bi.x] * bw.x + g_rec[bi.y] * bw.y + g_rec[bi.z] * bw.z + g_rec[bi.w] * bw.w;
        float4 r1 = g_rec[bi.x + 1] * bw.x + g_rec[bi.y + 1] * bw.y + g_rec[bi.z + 1] * bw.z + g_rec[bi.w + 1] * bw.w;
        float4 r2 = g_rec[bi.x + 2] * bw.x + g_rec[bi.y + 2] * bw.y + g_rec[bi.z + 2] * bw.z + g_rec[bi.w + 2] * bw.w;
        float4 p4 = float4(p, 1.0);
        y = float3(dot(r0, p4), dot(r1, p4), dot(r2, p4));
        nn = float3(dot(r0.xyz, n), dot(r1.xyz, n), dot(r2.xyz, n));
        float4 q0 = g_rec[96 + bi.x] * bw.x + g_rec[96 + bi.y] * bw.y + g_rec[96 + bi.z] * bw.z + g_rec[96 + bi.w] * bw.w;
        float4 q1 = g_rec[97 + bi.x] * bw.x + g_rec[97 + bi.y] * bw.y + g_rec[97 + bi.z] * bw.z + g_rec[97 + bi.w] * bw.w;
        float4 q2 = g_rec[98 + bi.x] * bw.x + g_rec[98 + bi.y] * bw.y + g_rec[98 + bi.z] * bw.z + g_rec[98 + bi.w] * bw.w;
        // sin huesos del cuadro anterior (paleta en cero) = sin movimiento
        yp = (dot(q0, q0) + dot(q1, q1) + dot(q2, q2) > 1e-6) ? float3(dot(q0, p4), dot(q1, p4), dot(q2, p4)) : y;
    }
    float nl = length(nn);
    nn = nl > 1e-8 ? nn / nl : float3(0, 1, 0);
    float3 vel = y - yp;
    if (!all(isfinite(vel)) || dot(vel, vel) > 0.25) vel = 0;   // teletransporte / corte: sin velocidad
    float3 u = (y - float3(-1.6, -1.0, -1.6)) / float3(3.2, 4.0, 3.2);
    bool inside = all(isfinite(y)) && all(u > 0.0) && all(u < 1.0);
    uint3 q = (uint3)(saturate(u) * 65535.0 + 0.5);
    uint2 o = (uint2)((OctEncode(nn) * 0.5 + 0.5) * 65535.0 + 0.5);
    uint3 h = f32tof16(vel);
    uint ov = d0.w + v;
    g_ov[ov] = uint4(q.x | (q.y << 16), q.z | (o.x << 16), o.y | (h.x << 16), h.y | (h.z << 16));
    uint prev = 0;
    if (inside && !shirt) InterlockedExchange(g_tab[t * BODY_STRIDE + 16 + BodyHash((int3)floor(y / BODY_CELL))], ov + 1, prev);   // 0 = fin de la lista
    g_on[ov] = prev;
}
