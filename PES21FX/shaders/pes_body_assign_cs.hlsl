// PES21 FX (25/10 e) - el cuerpo real para la fisica de la ropa: a que personaje va cada parte del cuerpo (cuerpo.cpp).
// Un solo grupo, una vez por cuadro (en Present), con todos los dibujos de la piel del cuerpo (VS 1c02a92a) que se ejecutaron
// en el cuadro. De cada uno la DLL copio en el dibujo m_view (b2), el bloque por objeto (b5) y los huesos (b6).
//   - Personaje = origen del modelo en el mundo (inversa(m_view) x m_viewWorld). Los dibujos cuyo origen esta a menos de 2 cm
//     son del mismo personaje (el bloque b5 se reusa entre jugadores en el mismo cuadro: no sirve para separarlos).
//   - La misma malla dos veces en el mismo personaje (otra pasada del dibujo) cuenta una sola vez.
//   - Cada personaje recibe una tabla (hasta 128): cabecera [0] cuadro, [1] partes, [2] vertices, [3] 1 = valida, [4..6] origen
//     en el mundo, [7] primer registro. Las celdas las llena pes_body_cs.hlsl (la DLL limpia las tablas antes).
//   - g_mode 2: diagnostico (no se usa en el juego normal).
// Las constantes tienen que coincidir con cuerpo.cpp, pes_body_cs.hlsl y cloth_sim.hlsl.
#define BODY_TABLES 128
#define BODY_CELLS  8192
#define BODY_STRIDE (16 + BODY_CELLS)
#define REC_F4      208      // float4 por registro: [0..3] m_view (filas 8..11 de b2), [4..12] b5 (filas 0..8), [16..207] b6
#define MAXREC      512
#define SAME_CHAR   0.02     // (m)
#define NONE        0xffffffffu

cbuffer BodyCB : register(b0)
{
    uint g_mode;     // 0 = asignar
    uint g_nrec;     // registros del lote
    uint g_stamp;    // cuadro que vera el simulador de la ropa (fila 257)
    uint g_ngroups;
};
Buffer<uint4>  g_desc   : register(t1);   // 2 por registro: (lugar, primer vertice propio, vertices, primer vertice del anillo), (malla, 0, 0, 0)
Buffer<float4> g_rec    : register(t2);   // REC_F4 por lugar
RWBuffer<uint> g_tab    : register(u2);   // tablas (BODY_TABLES x BODY_STRIDE)
RWBuffer<uint> g_recTab : register(u3);   // por registro: tabla (NONE = no se usa)
RWBuffer<uint> g_diag   : register(u4);   // resumen para el log de la DLL

groupshared float4 gW[MAXREC];   // origen en el mundo, w = 1 valido
groupshared uint   gC[MAXREC];   // primer registro del mismo personaje (NONE = invalido)
groupshared uint   gM[MAXREC];   // malla
groupshared uint   gS[MAXREC];   // 1 = repetido (misma malla, mismo personaje)
groupshared uint   gK[MAXREC];   // tabla del personaje (solo en el primer registro)
groupshared uint   gCount[4];

float3x3 Inverse3(float3x3 m, out float det)
{
    float3 r0 = cross(m[1], m[2]), r1 = cross(m[2], m[0]), r2 = cross(m[0], m[1]);
    det = dot(m[0], r0);
    return transpose(float3x3(r0, r1, r2)) / (abs(det) > 1e-12 ? det : 1.0);
}

[numthreads(64, 1, 1)]
void main(uint gi : SV_GroupIndex)
{
    const uint n = min(g_nrec, MAXREC);
    if (gi < 4) gCount[gi] = 0;
    for (uint i0 = gi; i0 < n; i0 += 64) {
        uint4 d0 = g_desc[2 * i0], d1 = g_desc[2 * i0 + 1];
        uint rb = d0.x * REC_F4;
        float4 v8 = g_rec[rb], v9 = g_rec[rb + 1], v10 = g_rec[rb + 2];
        float3x3 R = float3x3(v8.xyz, v9.xyz, v10.xyz);                        // mundo -> vista (rotacion)
        float3 t = float3(v8.w, v9.w, v10.w);
        float3 vo = float3(g_rec[rb + 4].w, g_rec[rb + 5].w, g_rec[rb + 6].w);   // origen del modelo en la vista (m_viewWorld)
        float det;
        float3x3 Ri = Inverse3(R, det);
        float3 wt = mul(Ri, vo - t);
        bool ok = abs(det) > 1e-6 && all(isfinite(wt)) && d0.z > 0;
        gW[i0] = float4(wt, ok ? 1.0 : 0.0);
        gM[i0] = d1.x;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint i1 = gi; i1 < n; i1 += 64) {
        uint c = NONE;
        if (gW[i1].w > 0.5) {
            [loop] for (uint j0 = 0; j0 <= i1; ++j0)
                if (gW[j0].w > 0.5 && distance(gW[j0].xyz, gW[i1].xyz) < SAME_CHAR) { c = j0; break; }
        }
        gC[i1] = c;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint i2 = gi; i2 < n; i2 += 64) {
        uint s = gC[i2] == NONE ? 1u : 0u;
        if (!s) {
            [loop] for (uint j1 = 0; j1 < i2; ++j1)
                if (gC[j1] == gC[i2] && gM[j1] == gM[i2]) { s = 1u; break; }
        }
        gS[i2] = s;
        uint k = NONE;
        if (gC[i2] == i2) {
            k = 0;
            [loop] for (uint j2 = 0; j2 < i2; ++j2) k += gC[j2] == j2 ? 1u : 0u;
        }
        gK[i2] = k;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint i3 = gi; i3 < n; i3 += 64) {
        uint t = NONE;
        if (!gS[i3] && gC[i3] != NONE) { uint k = gK[gC[i3]]; if (k < BODY_TABLES) t = k; }
        g_recTab[i3] = t;
        uint dummy;
        if (t != NONE) {
            uint4 d0 = g_desc[2 * i3];
            uint tb = t * BODY_STRIDE;
            InterlockedAdd(g_tab[tb + 1], 1u, dummy);
            InterlockedAdd(g_tab[tb + 2], d0.z, dummy);
            if (gC[i3] == i3) {
                g_tab[tb + 0] = g_stamp; g_tab[tb + 3] = 1u;
                g_tab[tb + 4] = asuint(gW[i3].x); g_tab[tb + 5] = asuint(gW[i3].y); g_tab[tb + 6] = asuint(gW[i3].z);
                g_tab[tb + 7] = i3;
                InterlockedAdd(gCount[0], 1u, dummy);
            }
        } else if (gS[i3] && gC[i3] != NONE) InterlockedAdd(gCount[1], 1u, dummy);
        else InterlockedAdd(gCount[2], 1u, dummy);
    }
    GroupMemoryBarrierWithGroupSync();
    // resumen: [0] cuadro, [1] registros, [2] personajes, [3] repetidos, [4] sin tabla / invalidos;
    // [16 + 4k] origen x, y, z (float) y partes del personaje k (k < 8)
    if (gi == 0) { g_diag[0] = g_stamp; g_diag[1] = n; g_diag[2] = gCount[0]; g_diag[3] = gCount[1]; g_diag[4] = gCount[2]; }
    for (uint i4 = gi; i4 < n; i4 += 64) {
        uint k = gK[i4];
        if (gC[i4] == i4 && k < 8) {
            uint parts = 0;
            [loop] for (uint j3 = i4; j3 < n; ++j3) parts += (gC[j3] == i4 && !gS[j3]) ? 1u : 0u;
            g_diag[16 + 4 * k] = asuint(gW[i4].x); g_diag[17 + 4 * k] = asuint(gW[i4].y); g_diag[18 + 4 * k] = asuint(gW[i4].z); g_diag[19 + 4 * k] = parts;
        }
    }
}
