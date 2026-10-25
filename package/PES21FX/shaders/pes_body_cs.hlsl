// PES21 FX (25/10) - el cuerpo real para la fisica de la ropa (cuerpo.cpp lo despacha en cada dibujo de la piel del cuerpo,
// VS 1c02a92a: torso de TorsoBody, brazos, cuello).
// Deforma la malla registrada (posicion de reposo, normal, huesos y pesos: copia propia de la malla del juego) con los huesos de
// ESE dibujo (b6: actuales y del cuadro anterior) en el espacio del modelo, igual que el VS de la piel, y la deja en un registro:
//   - por vertice (anillo u0): posicion (3 x 16 bits en una caja de 3.2 x 4 x 3.2 m), normal (octaedro 2 x 16 bits) y lo que se
//     movio desde el cuadro anterior (3 x half);
//   - la tabla del personaje (u2: cabecera + celdas): una tabla hash de celdas de 2.5 cm con listas de vertices (u1 = siguiente),
//     para que el simulador de la ropa encuentre la piel cercana a cada punto de tela en el espacio del modelo.
// La cabecera lleva el cuadro (fila 257 de los parametros) y la posicion del personaje en el mundo (para emparejar cada prenda
// con su cuerpo). Las constantes tienen que coincidir con cuerpo.cpp y con cloth_sim.hlsl.
#define BODY_CELLS 8192
#define BODY_CELL  0.025

cbuffer cVSScene : register(b2) { float4 g_sc[12]; };     // m_view = registros 8..11 (columnas: dot(p, c8) = x en la vista)
cbuffer cVSObject : register(b5) { float4 g_ob[9]; };     // m_viewWorld 0..3, m_world 4..7, m_useWeightCount 8
cbuffer VSBones : register(b6) { float4 g_bn[192]; };      // hueso i: filas 3i..3i+2 (actuales), 96 + 3i.. (cuadro anterior)
cbuffer BodyCB : register(b7)
{
    uint g_sBase;    // primer vertice de la malla en la copia propia (2 uint4 por vertice)
    uint g_nv;       // vertices de la malla
    uint g_oBase;    // primer vertice del registro en el anillo
    uint g_mesh;     // numero de malla (diagnostico)
};
Texture1D<float4> IniParams : register(t120);
StructuredBuffer<uint4> g_sv : register(t0);
RWStructuredBuffer<uint4> g_ov : register(u0);
RWStructuredBuffer<uint> g_on : register(u1);
RWBuffer<uint> g_tab : register(u2);                       // la tabla de este personaje: [0..15] cabecera, [16..] celdas

uint BodyHash(int3 c) { return ((uint)c.x * 73856093u ^ (uint)c.y * 19349663u ^ (uint)c.z * 83492791u) & (BODY_CELLS - 1); }
float2 OctEncode(float3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-8);
    float2 o = n.xy;
    if (n.z < 0.0) o = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return o;
}
float3x3 Inverse3(float3x3 m, out float det)
{
    float3 r0 = cross(m[1], m[2]), r1 = cross(m[2], m[0]), r2 = cross(m[0], m[1]);
    det = dot(m[0], r0);
    return transpose(float3x3(r0, r1, r2)) / (abs(det) > 1e-12 ? det : 1.0);
}

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint v = tid.x;
    if (v == 0) {
        // cabecera: cuadro, registros, vertices, valida, posicion del personaje en el mundo (origen del modelo)
        float3 vo = float3(g_ob[0].w, g_ob[1].w, g_ob[2].w);                       // origen del modelo en la vista
        float3x3 R = float3x3(g_sc[8].xyz, g_sc[9].xyz, g_sc[10].xyz);             // mundo -> vista (rotacion)
        float3 t = float3(g_sc[8].w, g_sc[9].w, g_sc[10].w);
        float det; float3x3 Ri = Inverse3(R, det);
        float3 wt = mul(Ri, vo - t);
        bool ok = abs(det) > 1e-6 && all(isfinite(wt));
        g_tab[0] = (uint)(IniParams.Load(int2(257, 0)).x + 0.5);
        uint dummy;
        InterlockedAdd(g_tab[1], 1u, dummy);
        InterlockedAdd(g_tab[2], g_nv, dummy);
        g_tab[3] = ok ? 1u : 2u;
        g_tab[4] = asuint(wt.x); g_tab[5] = asuint(wt.y); g_tab[6] = asuint(wt.z);
        g_tab[7] = g_mesh;
    }
    if (v >= g_nv) return;
    uint4 a = g_sv[2 * (g_sBase + v)], b = g_sv[2 * (g_sBase + v) + 1];
    float3 p = asfloat(a.xyz), n = asfloat(b.xyz);
    uint4 bi = min(uint4(a.w & 255, (a.w >> 8) & 255, (a.w >> 16) & 255, a.w >> 24), 31u) * 3;
    float4 bw = float4(b.w & 255, (b.w >> 8) & 255, (b.w >> 16) & 255, b.w >> 24) / 255.0;
    float3 y = p, yp = p, nn = n;
    if (g_ob[8].x != 0.0) {
        float4 r0 = g_bn[bi.x] * bw.x + g_bn[bi.y] * bw.y + g_bn[bi.z] * bw.z + g_bn[bi.w] * bw.w;
        float4 r1 = g_bn[bi.x + 1] * bw.x + g_bn[bi.y + 1] * bw.y + g_bn[bi.z + 1] * bw.z + g_bn[bi.w + 1] * bw.w;
        float4 r2 = g_bn[bi.x + 2] * bw.x + g_bn[bi.y + 2] * bw.y + g_bn[bi.z + 2] * bw.z + g_bn[bi.w + 2] * bw.w;
        float4 p4 = float4(p, 1.0);
        y = float3(dot(r0, p4), dot(r1, p4), dot(r2, p4));
        nn = float3(dot(r0.xyz, n), dot(r1.xyz, n), dot(r2.xyz, n));
        float4 q0 = g_bn[96 + bi.x] * bw.x + g_bn[96 + bi.y] * bw.y + g_bn[96 + bi.z] * bw.z + g_bn[96 + bi.w] * bw.w;
        float4 q1 = g_bn[97 + bi.x] * bw.x + g_bn[97 + bi.y] * bw.y + g_bn[97 + bi.z] * bw.z + g_bn[97 + bi.w] * bw.w;
        float4 q2 = g_bn[98 + bi.x] * bw.x + g_bn[98 + bi.y] * bw.y + g_bn[98 + bi.z] * bw.z + g_bn[98 + bi.w] * bw.w;
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
    uint ov = g_oBase + v;
    g_ov[ov] = uint4(q.x | (q.y << 16), q.z | (o.x << 16), o.y | (h.x << 16), h.y | (h.z << 16));
    uint prev = 0;
    if (inside) InterlockedExchange(g_tab[16 + BodyHash((int3)floor(y / BODY_CELL))], ov + 1, prev);   // 0 = fin de la lista
    g_on[ov] = prev;
}
