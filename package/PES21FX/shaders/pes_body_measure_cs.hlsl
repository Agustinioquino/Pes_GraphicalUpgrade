// PES21 FX (27/10 d) - medicion: cuanto asoma el torso real por fuera de la camiseta (cuerpo.cpp, fila 329).
// En el mismo lote que pes_body_cs.hlsl, los dibujos de la camiseta (PS 7e64 / a41e / c7ae) tambien se deforman con sus huesos
// (sin entrar en la tabla del cuerpo: el simulador de la ropa no los ve). Aca, para cada vertice de la camiseta, se busca la
// piel mas cercana de su personaje (la tabla del cuerpo, a menos de 3 cm) y la distancia con signo por la normal de esa piel:
// negativa = el torso queda afuera de la tela en ese punto. Es la camiseta en su lugar de fabrica (sin la fisica del mod de ropa)
// y la piel sin el x320 del VS. Una vez cada 20 s; resumen por zona (reposo de la camiseta) para el log:
//   zona z: [8z + 0] puntos con piel cerca, +1 torso afuera, +2 afuera mas de 3 mm, +3 afuera mas que x320, +4 maximo (x100 mm),
//   +5 suma de lo que asoma (x100 mm), +6 distancia media tela-piel donde no asoma (x100 mm, suma)
// Las constantes tienen que coincidir con cuerpo.cpp y pes_body_cs.hlsl.
#define BODY_CELLS  8192
#define BODY_CELL   0.025
#define BODY_STRIDE (16 + BODY_CELLS)
#define NONE        0xffffffffu
#define REACH       0.03
#define WALK        48

cbuffer BodyCB : register(b0)
{
    uint g_mode;
    uint g_nrec;
    uint g_stamp;
    uint g_ngroups;
};
StructuredBuffer<uint4>  g_sv     : register(t0);   // mallas registradas (reposo)
Buffer<uint4>            g_desc   : register(t1);   // 2 por registro: (lugar, primer vertice propio, vertices, primer vertice del anillo), (malla, tipo, 0, 0)
Buffer<uint2>            g_grp    : register(t3);   // por grupo: registro, primer vertice
StructuredBuffer<uint4>  g_ov     : register(t4);   // anillo: vertices deformados
Buffer<uint>             g_tab    : register(t5);   // tablas del cuerpo
StructuredBuffer<uint>   g_on     : register(t6);   // siguiente en la celda
Texture1D<float4>        IniParams : register(t120);
RWBuffer<uint>           g_recTab : register(u1);   // por registro: tabla del personaje
RWBuffer<uint>           g_meas   : register(u0);
RWBuffer<uint>           g_need   : register(u2);   // (31/10) mapa en el reposo del cuerpo: cuanto hay que hundir la piel (x100 mm, maximo)

// (31/10) torso bajo la camiseta "preciso" (fila 320, y320 = 2): grilla en el reposo de TorsoBody (|x| 0..0.48, y 0.84..1.64,
// z -0.20..0.20; celdas de 1.5 / 1.25 / 1.25 cm). Tiene que coincidir con cuerpo.cpp y pes_skintone.hlsli (PesTorsoNeed)
#define NEED_NX 32
#define NEED_NY 64
#define NEED_NZ 32
int3 NeedCell(float3 r) { return (int3)floor((float3(abs(r.x), r.y, r.z) - float3(0.0, 0.84, -0.20)) / float3(0.015, 0.0125, 0.0125)); }
void NeedPut(int3 c, uint v)
{
    if (any(c < 0) || c.x >= NEED_NX || c.y >= NEED_NY || c.z >= NEED_NZ || v == 0) return;
    uint o; InterlockedMax(g_need[(c.z * NEED_NY + c.y) * NEED_NX + c.x], v, o);
}
// registro (y su vertice) de un vertice del anillo: los registros estan en orden de anillo (d0.w creciente)
float3 RingRest(uint ri)
{
    uint lo = 0, hi = g_nrec;
    [loop] for (uint k = 0; k < 10 && hi - lo > 1; ++k) { uint mid = (lo + hi) >> 1; if (g_desc[2 * mid].w <= ri) lo = mid; else hi = mid; }
    uint4 d = g_desc[2 * lo];
    return asfloat(g_sv[2 * (d.y + (ri - d.w))].xyz);
}

uint BodyHash(int3 c) { return ((uint)c.x * 73856093u ^ (uint)c.y * 19349663u ^ (uint)c.z * 83492791u) & (BODY_CELLS - 1); }
float3 OctDecode(float2 o)
{
    float3 n = float3(o, 1.0 - abs(o.x) - abs(o.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return normalize(n + 1e-12);
}
void RingVertex(uint i, out float3 y, out float3 n)
{
    uint4 V = g_ov[i];
    y = float3(V.x & 0xffff, V.x >> 16, V.y & 0xffff) / 65535.0 * float3(3.2, 4.0, 3.2) - float3(1.6, 1.0, 1.6);
    n = OctDecode(float2(V.y >> 16, V.z & 0xffff) / 65535.0 * 2.0 - 1.0);
}
// zona por el reposo de la camiseta (espacio del modelo de TorsoBody: brazos en T a los costados)
uint Zone(float3 p)
{
    float ax = abs(p.x);
    if (ax >= 0.24) return 2;                                   // manga
    if (ax < 0.13) return p.y > 1.47 ? 0u : (p.y > 1.10 ? 4u : 5u);   // escote / pecho y espalda / abajo
    if (p.y > 1.38) return 1;                                   // hombros
    if (p.y > 1.22) return 3;                                   // axila
    return 5;                                                   // costado / abajo
}

[numthreads(64, 1, 1)]
void main(uint3 gid : SV_GroupID, uint gi : SV_GroupIndex)
{
    if (gid.x >= g_ngroups) return;
    uint2 gr = g_grp[gid.x];
    uint4 d0 = g_desc[2 * gr.x], d1 = g_desc[2 * gr.x + 1];
    if (d1.y != 1u) return;   // solo camisetas
    uint t = g_recTab[gr.x];
    if (t == NONE) return;
    uint v = gr.y + gi;
    if (v >= d0.z) return;
    float3 rest = asfloat(g_sv[2 * (d0.y + v)].xyz);
    float3 ys, ns;
    RingVertex(d0.w + v, ys, ns);
    uint tb = t * BODY_STRIDE + 16;
    int3 c0 = (int3)floor(ys / BODY_CELL) - 1;
    float best = REACH * REACH, dist = 0.0;
    bool found = false;
    uint eBest = 0;
    [loop] for (uint ci = 0; ci < 27; ++ci) {
        int3 c = c0 + int3(ci % 3, (ci / 3) % 3, ci / 9);
        uint e = g_tab[tb + BodyHash(c)];
        [loop] for (uint it = 0; it < WALK && e != 0; ++it) {
            float3 y, n;
            RingVertex(e - 1, y, n);
            float3 dv = ys - y;
            float d2 = dot(dv, dv);
            if (d2 < best) { best = d2; dist = dot(dv, n); found = true; eBest = e - 1; }
            e = g_on[e - 1];
        }
    }
    if (!found) return;
    uint z = Zone(rest) * 8, o;
    float inset = max(IniParams.Load(int2(320, 0)).x, 0.0) * 0.001;
    InterlockedAdd(g_meas[z + 0], 1u, o);
    if (dist < 0.0) {
        uint pen = (uint)min(-dist * 100000.0, 1.0e6);   // x100 mm
        InterlockedAdd(g_meas[z + 1], 1u, o);
        if (dist < -0.003) InterlockedAdd(g_meas[z + 2], 1u, o);
        if (dist < -inset) InterlockedAdd(g_meas[z + 3], 1u, o);
        InterlockedMax(g_meas[z + 4], pen, o);
        InterlockedAdd(g_meas[z + 5], pen, o);
        // (31/10) al mapa: en la celda del reposo de esa piel lo que asoma, y en las vecinas un poco menos (sin huecos entre puntos)
        int3 c = NeedCell(RingRest(eBest));
        [loop] for (uint ni = 0; ni < 27; ++ni) {
            int3 dc = int3(ni % 3, (ni / 3) % 3, ni / 9) - 1;
            uint m = (uint)(abs(dc.x) + abs(dc.y) + abs(dc.z));
            NeedPut(c + dc, m == 0 ? pen : (uint)(pen * (m == 1 ? 0.85 : (m == 2 ? 0.7 : 0.55))));
        }
    } else InterlockedAdd(g_meas[z + 6], (uint)min(dist * 100000.0, 1.0e6), o);
}
