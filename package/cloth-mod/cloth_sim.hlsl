#define MAXP     2048
#define THREADS  1024
#define MAXCAPS  16
#define MAXPARTS 8
// ===== PES21 FX (18/10): fisica de la ropa v2. Se aplica al cargar el juego (el mod compila este archivo al arrancar) =====
// SHORT (se reconoce porque es la unica prenda con seguimiento de piernas):
#define PES_SHORTS_DAMP    0.55   // amortiguacion relativa al cuerpo: <1 = el short sigue balanceandose mas tiempo tras cada paso
#define PES_SHORTS_KICK    1.60   // cuanto responde al salto / caida / arranque (multiplica el impulso del cuerpo)
#define PES_SHORTS_BEND    1.70   // pliegues mas blandos (>1 = la tela se dobla mas facil)
#define PES_SHORTS_FLUTTER 0.12   // aleteo del borde al correr, proporcional a la velocidad del cuerpo (m/s)
// CAMISETA Y SHORT:
#define PES_AIR_SPEED      0.25   // resistencia del aire que crece con la velocidad: la tela se pega al correr y flamea al frenar
#define PES_HEM_FREE       0.35   // el borde libre (sin pin) se amortigua menos que la tela cerca de las costuras
#define PES_FLUTTER_ON     1      // 0 = sin aleteo
// DESLIZAMIENTO POR GRAVEDAD (short con la pierna arriba, manga con el brazo arriba): donde la gravedad queda tangente a la
// superficie del cuerpo, la tela deja de estar "cosida" a su posicion de fabrica y resbala hasta donde le da el largo.
#define PES_GRAVITY_SLIDE  0.70   // 0 = como antes; 1 = resbala al maximo
#define PES_SLIDE_PIN      10.0   // cuanto se afloja el resorte hacia la posicion de fabrica donde resbala
#define PES_SLIDE_TETHER   1.2    // cuanto se alarga la correa de distancia maxima donde resbala
// COLISION PRECISA CON LAS PIERNAS: ademas del plano promedio del mod, cada vertice de la pierna emparejado empuja con su
// propio plano tangente (el muslo doblado deja de atravesar el short), en varias pasadas por sub-paso.
#define PES_LEG_PRECISE    1
#define PES_LEG_ITER       2      // pasadas por sub-paso
#define PES_LEG_REACH      0.06   // (m) solo cuentan los vertices de pierna a menos de esta distancia
// INERCIA POR ZONA: al arrancar, la tela del lado opuesto a la aceleracion (espalda, faldon) se queda atras; al frenar
// se adelanta. Se aplica como impulso extra en la cara que mira en contra de la aceleracion.
#define PES_LAG            0.80
// FALDON AL GIRAR: fuerza centrifuga por la velocidad angular de la prenda (giros de cadera): la tela se abre.
#define PES_SPIN           0.60
// TELA MOJADA (0..1): mas pesada, mas amortiguada y mas pegada al cuerpo. Lo escribe PES21 FX con el sudor/lluvia.
#define PES_WET            0.00   // PES21FX_LIVE_WET
// PES21 FX (18/10): ajuste de la tela al fisico. La escala de los huesos (fisico del editor: ancho de hombros, pecho,
// cintura) se lee de la matriz de skinning de cada particula: >1 cuerpo ancho -> la tela se pega; <1 delgado -> cuelga.
#define PES_FIT_ON         1      // PES21FX_LIVE_FIT_ON
#define PES_FIT_THIN       10.0   // PES21FX_LIVE_FIT_THIN   mm de holgura extra en el cuerpo mas delgado
#define PES_FIT_FAT        6.0    // PES21FX_LIVE_FIT_FAT    mm mas pegada en el cuerpo mas ancho
#define PES_FIT_RANGE      0.15   // PES21FX_LIVE_FIT_RANGE  escala de hueso (+-) con la que se llega al maximo
cbuffer Bones : register(b0) { float4 g_bones[192]; };
cbuffer Obj   : register(b1) { float4 g_obj[18]; };
cbuffer Params : register(b2)
{
    uint   g_vertexCount;
    uint   g_particleCount;
    uint   g_stamp;
    uint   g_flags;
    float  g_h;
    uint   g_substeps;
    uint   g_colorCount;
    uint   g_capsuleCount;
    float  g_stretchCompliance;
    float  g_compressCompliance;
    float  g_bendCompliance;
    float  g_pinCompliance;
    float  g_damping;
    float  g_airDrag;
    float  g_tetherRadius;
    float  g_collisionInset;
    float4 g_gravity;
    float4 g_wind;
    float  g_matchRadius;
    float  g_normalSign;
    float  g_time;
    uint   g_stride0;
    uint   g_stride1;
    uint   g_skinned;
    float  g_capsuleRadiusScale;
    uint   g_part;
    uint   g_partStart;
    uint   g_partCount;
    uint   g_numParts;
    float  g_maxRelSpeed;
    float  g_friction;
    uint   g_coupleOn;
    float  g_sleeveDamping;
    float  g_restAdapt;
    float  g_contactBand;
    float  g_windResponse;
    float  g_windTurbulence;
    uint   g_otherCaps;
    float  g_bodyBounce;
    float  g_legMargin;
    float  g_legMaxRoom;
    uint   g_legCount;
    uint   g_otherVertexCount;
    uint3  g_pad5;
};
struct ParticleRest { float4 posPin; float4 nrm; uint4 skin; };
struct Capsule      { float4 a; float4 b; };
ByteAddressBuffer               g_bindPos   : register(t0);
ByteAddressBuffer               g_nrmTan    : register(t1);
ByteAddressBuffer               g_skin      : register(t2);
StructuredBuffer<uint>          g_r2p       : register(t3);
StructuredBuffer<ParticleRest>  g_rest      : register(t4);
StructuredBuffer<uint4>         g_cons      : register(t5);
StructuredBuffer<uint>          g_colorOff  : register(t6);
StructuredBuffer<uint>          g_adjOff    : register(t7);
StructuredBuffer<uint>          g_adjTri    : register(t8);
StructuredBuffer<uint4>         g_tris      : register(t9);
StructuredBuffer<Capsule>       g_caps      : register(t10);
StructuredBuffer<uint>          g_rpart     : register(t11);
ByteAddressBuffer               g_otherCache: register(t12);
StructuredBuffer<uint4>         g_couple    : register(t13);
ByteAddressBuffer               g_otherSkin : register(t14);
StructuredBuffer<uint>          g_otherPart : register(t15);
StructuredBuffer<Capsule>       g_otherCapsules : register(t16);
StructuredBuffer<uint>          g_p2r       : register(t17);
struct LegV { float4 pos; float4 nrm; uint4 skin; };
StructuredBuffer<LegV>          g_legV      : register(t19);
StructuredBuffer<uint4>         g_legPair   : register(t20);
struct Inst {
    uint stamp, substeps, flags, coupleOn;
    uint otherCaps, stateBase, palBase, cacheBase;
    uint outBase0, outBase1, pnrmBase, otherCacheBase;
    float4 world[3];
    float4 worldPrev[3];
    float4 kick;
};
StructuredBuffer<Inst>          g_inst      : register(t18);
static uint   s_stamp, s_substeps, s_flags, s_coupleOn, s_otherCaps;
static uint   s_stateBase, s_palBase, s_cacheBase, s_outBase0, s_outBase1, s_pnrmBase, s_otherCacheBase;
static float4 s_W[3], s_Wp[3];
static float3 s_kick;
static float3 s_grav;   // PES21 FX: gravedad en el espacio de la prenda (para el deslizamiento)
static float3 s_omega;  // PES21 FX: velocidad angular de la prenda (rad/s, espacio de la prenda)
static float3 s_center; // PES21 FX: origen de la prenda
RWByteAddressBuffer             g_outPos    : register(u0);
RWByteAddressBuffer             g_outNrm    : register(u1);
RWStructuredBuffer<float4>      g_state     : register(u2);
RWStructuredBuffer<float4>      g_pnrm      : register(u3);
RWStructuredBuffer<float4>      g_legSkin   : register(u6);
RWStructuredBuffer<float4>      g_coupleSkin : register(u7);
RWStructuredBuffer<float4>      g_boneCache : register(u4);
RWByteAddressBuffer             g_cache     : register(u5);
StructuredBuffer<float4>        g_pal       : register(t21);
groupshared float3 gx[MAXP];
groupshared uint   gMode;
groupshared uint   gFirstVisit;
groupshared float3 gCapA[MAXCAPS];
groupshared float3 gCapB[MAXCAPS];
groupshared float  gCapR[MAXCAPS];
float3x4 identity34() { return float3x4(1,0,0,0, 0,1,0,0, 0,0,1,0); }
float3x4 mulAffine(float3x4 A, float3x4 B)
{
    float3x3 R = mul((float3x3)A, (float3x3)B);
    float3 tr = mul((float3x3)A, float3(B[0].w, B[1].w, B[2].w)) + float3(A[0].w, A[1].w, A[2].w);
    return float3x4(float4(R[0], tr.x), float4(R[1], tr.y), float4(R[2], tr.z));
}
float3x4 boneSlot(uint slot, uint i, float t)
{
    uint b = slot * 192 + i * 3;
    float3x4 cur = float3x4(g_pal[s_palBase + (b)], g_pal[s_palBase + (b + 1)], g_pal[s_palBase + (b + 2)]);
    bool world = (s_flags & 32) != 0;
    if (world) cur = mulAffine(float3x4(s_W[0], s_W[1], s_W[2]), cur);
    if (!(s_flags & 8) || t >= 1.0) return cur;
    float3x4 prev = float3x4(g_pal[s_palBase + (b + 96)], g_pal[s_palBase + (b + 97)], g_pal[s_palBase + (b + 98)]);
    if (dot(prev[0], prev[0]) + dot(prev[1], prev[1]) + dot(prev[2], prev[2]) < 1e-6) return cur;
    if (world) prev = mulAffine(float3x4(s_Wp[0], s_Wp[1], s_Wp[2]), prev);
    return lerp(prev, cur, t);
}
float4 unpackWeights(uint w) { return float4(w & 255, (w >> 8) & 255, (w >> 16) & 255, w >> 24) / 255.0; }
uint4  unpackIndices(uint i) { return uint4(i & 255, (i >> 8) & 255, (i >> 16) & 255, i >> 24); }
float3x4 skinSlot(uint slot, uint wpk, uint ipk, float t)
{
    if (!g_skinned) return identity34();
    float4 w = unpackWeights(wpk);
    uint4  i = unpackIndices(ipk);
    return boneSlot(slot, i.x, t) * w.x + boneSlot(slot, i.y, t) * w.y + boneSlot(slot, i.z, t) * w.z +
           boneSlot(slot, i.w, t) * w.w;
}
float3x4 boneC(uint slot, uint i)
{
    uint b = s_palBase / 2 + (slot * 32 + i) * 3;
    return float3x4(g_boneCache[b], g_boneCache[b + 1], g_boneCache[b + 2]);
}
float3x4 skinC(uint slot, uint wpk, uint ipk)
{
    if (!g_skinned) return identity34();
    float4 w = unpackWeights(wpk);
    uint4  i = unpackIndices(ipk);
    return boneC(slot, i.x) * w.x + boneC(slot, i.y) * w.y + boneC(slot, i.z) * w.z + boneC(slot, i.w) * w.w;
}
float3x4 skinBound(uint wpk, uint ipk)
{
    if (!g_skinned) return identity34();
    float4 w = unpackWeights(wpk);
    uint4  i = unpackIndices(ipk);
    float3x4 M = 0;
    [unroll] for (uint k = 0; k < 4; ++k)
        M += float3x4(g_bones[i[k] * 3], g_bones[i[k] * 3 + 1], g_bones[i[k] * 3 + 2]) * w[k];
    return M;
}
float3 xform(float3x4 M, float3 p) { return mul((float3x3)M, p) + float3(M[0].w, M[1].w, M[2].w); }
float3x3 inverse3(float3x3 m, out float det)
{
    float3 r0 = cross(m[1], m[2]), r1 = cross(m[2], m[0]), r2 = cross(m[0], m[1]);
    det = dot(m[0], r0);
    return transpose(float3x3(r0, r1, r2)) / det;
}
// PES21 FX: escala volumetrica de la matriz de skinning de la particula (1 = cuerpo de fabrica)
float pesBodyScale(uint p)
{
    ParticleRest r = g_rest[p];
    float3x4 M = skinC(r.skin.z, r.skin.x, r.skin.y);
    float3 c0 = float3(M[0].x, M[1].x, M[2].x), c1 = float3(M[0].y, M[1].y, M[2].y), c2 = float3(M[0].z, M[1].z, M[2].z);
    float d = abs(dot(c0, cross(c1, c2)));
    return pow(max(d, 1e-6), 1.0 / 3.0);
}
// metros de ajuste: + = holgado (delgado), - = pegado (ancho)
float pesFitOf(uint p)
{
#if PES_FIT_ON
    float sc = pesBodyScale(p);
    float r = max(PES_FIT_RANGE, 0.01);
    float f = (sc < 1.0) ? PES_FIT_THIN * 0.001 * saturate((1.0 - sc) / r) : -PES_FIT_FAT * 0.001 * saturate((sc - 1.0) / r);
    if (g_legCount > 0) f *= 0.5;   // el short responde la mitad
    return f;
#else
    return 0.0;
#endif
}
void restAt(uint p, float t, out float3 pos, out float3 nrm)
{
    ParticleRest r = g_rest[p];
    float3x4 M = skinSlot(r.skin.z, r.skin.x, r.skin.y, t);
    pos = xform(M, r.posPin.xyz);
    nrm = normalize(mul((float3x3)M, r.nrm.xyz));
}
void fillBoneCache(uint gi, float t)
{
    for (uint bi = gi; bi < 2 * MAXPARTS * 32; bi += THREADS) {
        float3x4 B = boneSlot(bi / 32, bi % 32, t);
        uint o = s_palBase / 2 + bi * 3;
        g_boneCache[o] = B[0];
        g_boneCache[o + 1] = B[1];
        g_boneCache[o + 2] = B[2];
    }
}
void restAtC(uint p, out float3 pos, out float3 nrm)
{
    ParticleRest r = g_rest[p];
    float3x4 M = skinC(r.skin.z, r.skin.x, r.skin.y);
    pos = xform(M, r.posPin.xyz);
    nrm = normalize(mul((float3x3)M, r.nrm.xyz));
}
float3 rotateBetween(float3 a, float3 b, float3 v)
{
    float3 c = cross(a, b);
    float  d = dot(a, b);
    if (d < -0.9999) return -v;
    return v + cross(c, v) + cross(c, cross(c, v)) / (1.0 + d);
}
uint packHalf2(float a, float b) { return f32tof16(a) | (f32tof16(b) << 16); }
float pinOf(uint p)
{
    if (s_flags & 256) return float((g_rest[p].skin.w >> 2) & 63) / 63.0;
    return g_rest[p].posPin.w;
}
void loadParticle(uint p, uint mode, out float3 xPrev, out float3 vel, out float3 restPrev)
{
    float3 rp, rn;
    restAtC(p, rp, rn);
    restPrev = rp;
    if (mode & 4) {
        gx[p] = rp;
        if (!(s_flags & 64) && (s_flags & 16)) {
            ParticleRest rr = g_rest[p];
            float3 b = asfloat(g_cache.Load3(s_cacheBase + g_p2r[p] * 32));
            float3 xr = xform(skinC(rr.skin.z, rr.skin.x, rr.skin.y), b);
            if (all(isfinite(xr)) && distance(xr, rp) < 0.5) gx[p] = xr;
        }
        vel = 0;
    } else {
        gx[p] = g_state[s_stateBase + (2 * p)].xyz;
        vel = g_state[s_stateBase + (2 * p + 1)].xyz;
    }
    xPrev = gx[p];
}
void predictParticle(uint p, uint s, float h, float3 grav, float3 windBase, float gust, inout float3 xPrev,
                     inout float3 vel, float3 restPrev, out float3 rest1, out float3 rnrm1)
{
    restAtC(p, rest1, rnrm1);
    g_pnrm[s_pnrmBase + (p)] = float4(rest1, 0);
    float pin = pinOf(p);
    xPrev = gx[p];
    float3 vBody = (rest1 - restPrev) / h;
    if (pin >= 0.999) { gx[p] = rest1; vel = vBody; return; }
    float3 v = vel + grav * h;
    if (s == 0) v += s_kick;
    float sleeveW = float((g_rest[p].skin.w >> 8) & 255) / 255.0;
    float damp = g_damping + g_sleeveDamping * sleeveW;
    bool pesShorts = g_legCount > 0;                                   // PES21 FX: esta prenda es el short
    if (pesShorts) { damp *= PES_SHORTS_DAMP; if (s == 0) v += s_kick * (PES_SHORTS_KICK - 1.0); }
    // PES21 FX inercia por zona: la cara que mira en contra de la aceleracion recibe mas impulso (se queda atras)
    if (s == 0) { float kl = length(s_kick); if (kl > 1e-5) v += s_kick * (PES_LAG * saturate(-dot(rnrm1, s_kick / kl)) * saturate(1.0 - pin * 1.5)); }
    // PES21 FX faldon al girar: aceleracion centrifuga w x (w x r)
    { float3 rr = gx[p] - s_center; float3 ac = cross(s_omega, cross(s_omega, rr)); v -= ac * h * PES_SPIN * saturate(1.0 - pin * 1.5); }
    // PES21 FX tela mojada: mas amortiguacion y mas peso
    damp *= 1.0 + 1.5 * PES_WET;
    v += grav * h * (0.6 * PES_WET);
    damp *= lerp(1.0, 1.0 - PES_HEM_FREE, saturate(1.0 - pin * 2.0));  // PES21 FX: el borde libre se frena menos
    float3 vRel = (v - vBody) * exp(-damp * h);
    float rs = length(vRel);
    if (rs > g_maxRelSpeed) vRel *= g_maxRelSpeed / rs;
    v = vBody + vRel;
    float3 n = rnrm1;
    float3 wind = 0;
    float k = g_airDrag;
    if (any(windBase != 0)) {
        float  ws = length(windBase);
        float3 wd = windBase / ws;
        float3 side = cross(wd, float3(0, 1, 0));
        side = dot(side, side) > 1e-4 ? normalize(side) : float3(1, 0, 0);
        float3 up = cross(side, wd);
        float3 r = g_rest[p].posPin.xyz;
        float  tt = g_time + float(s) * h;
        float  sp = clamp(ws * 0.6, 1.0, 4.0);
        float  a1 = dot(r, wd) * 30.0 - tt * 30.0 * sp * 0.25 + dot(r, side) * 9.0;
        float  a2 = dot(r, wd) * 21.0 - tt * 21.0 * sp * 0.25 + dot(r, up) * 13.0 + 1.7;
        float  a3 = dot(r, side) * 26.0 + dot(r, up) * 11.0 - tt * 17.0;
        float  e1 = sin(a1) * 0.6 + sin(a2 * 1.37 + 0.5) * 0.4;
        float  e2 = sin(a2) * 0.7 + sin(a3) * 0.3;
        float  e3 = sin(a3 * 1.21 + 2.0);
        float  T = g_windTurbulence;
        float3 slow = windBase * max(0.0, gust);
        wind = slow + ws * T * (wd * 0.5 * e1 + side * 0.6 * e2 + up * 0.35 * e3);
        k += g_windResponse;
    }
    // PES21 FX: resistencia del aire con la velocidad del cuerpo (correr pega la tela; frenar la deja flamear)
    float pesSpeed = length(vBody);
    k += PES_AIR_SPEED * pesSpeed;
    if (pesShorts && PES_FLUTTER_ON) {
        float3 side = cross(n, float3(0, 1, 0));
        float sl = length(side);
        if (sl > 1e-4) {
            float ph = g_time * 11.0 + dot(g_rest[p].posPin.xyz, float3(23.0, 17.0, 29.0)) + float(s) * h * 11.0;
            v += (side / sl) * sin(ph) * PES_SHORTS_FLUTTER * saturate(pesSpeed * 0.3) * saturate(1.0 - pin * 2.0);
        }
    }
    v -= n * dot(v - wind, n) * min(k * h, 1.0);
    vel = v;
    gx[p] += v * h;
}
void collideParticle(uint p, float t, float h, float hh, inout float3 xPrev, inout float3 vel,
                     inout float3 restPrev, float3 rest1, float3 rnrm1)
{
    float pin = pinOf(p);
    if (pin < 0.999) {
        float3 x = gx[p];
        float rel = (1.0 - pin) / max(pin, 1e-4);
        float alpha = g_pinCompliance * rel * rel;
        // PES21 FX: deslizamiento por gravedad. Componente de la gravedad tangente a la superficie del cuerpo:
        // grande = la tela quiere resbalar (muslo levantado, brazo arriba). Solo tela lejos de las costuras (pin bajo).
        float pesSlide = 0.0;
        { float gl = length(s_grav);
          if (gl > 1e-4) {
            float3 gd = s_grav / gl;
            float3 tg = gd - rnrm1 * dot(gd, rnrm1);
            pesSlide = saturate(length(tg)) * PES_GRAVITY_SLIDE * saturate(1.0 - pin * 1.5);
          } }
        alpha *= 1.0 + pesSlide * PES_SLIDE_PIN;
        // PES21 FX: ajuste al fisico. Cuerpo ancho (fit < 0): el anclaje tira mas y la correa es mas corta (se pega).
        // Cuerpo delgado (fit > 0): anclaje mas suelto, correa mas larga y la tela se mantiene separada del cuerpo.
        float pesFit = pesFitOf(p);
        alpha *= clamp(1.0 + pesFit * 200.0, 0.35, 3.0);
        x += (rest1 - x) * (1.0 / (1.0 + alpha / hh));
        float3 off = x - rest1;
        float maxD0 = g_tetherRadius * (1.0 - pin);
        float maxD = maxD0 * (1.0 + pesSlide * PES_SLIDE_TETHER) * clamp(1.0 + pesFit * 60.0, 0.3, 2.5);
        float dl = length(off);
        if (dl > maxD) x = rest1 + off * (maxD / dl);
        float sd = dot(x - rest1, rnrm1) + g_rest[p].nrm.w;
        uint capN = (g_rest[p].skin.w & 1) ? min(g_capsuleCount, MAXCAPS) : 0;
        float inset = max(g_collisionInset, float(g_rest[p].skin.w >> 24) * 0.001);
        uint4 lp = (s_flags & 128) && g_legCount ? g_legPair[p] : uint4(0xffffffff, 0, 0, 0);
        bool legTracked = lp.x != 0xffffffff;
        if (legTracked && (g_rest[p].skin.w & 1)) inset = max(inset, g_legMaxRoom);
        float insetF = inset - max(pesFit, 0.0);   // PES21 FX: delgado = la tela queda a 'fit' del cuerpo
        if (capN == 0 && sd < -insetF) {
            float3 corr = -(sd + insetF) * rnrm1;
            x += corr;
            xPrev += corr * (1.0 - g_bodyBounce);
        }
        if (legTracked) {
            float3 ly = 0, ln = 0;
            float wsum = 0;
            [loop] for (uint k = 0; k < 8; ++k) {
                uint v = (lp[k >> 1] >> ((k & 1) * 16)) & 0xffff;
                if (v == 0xffff) break;
                uint lo = (s_palBase / (2 * MAXPARTS * 192)) * g_legCount * 2 + v * 2;
                float3 yk = g_legSkin[lo].xyz;
                float3 nk = g_legSkin[lo + 1].xyz;
                if (dot(nk, nk) < 1e-8 || !all(isfinite(yk))) continue;
                float dk = distance(x, yk);
                float wk = 1.0 / (dk * dk + 1e-4);
                ly += yk * wk;
                ln += normalize(nk) * wk;
                wsum += wk;
            }
            if (wsum > 0 && dot(ln, ln) > 1e-8) {
                ly /= wsum;
                ln = normalize(ln);
                float d = dot(x - ly, ln);
                if (d < g_legMargin) {
                    float3 corr = ln * (g_legMargin - d);
                    x += corr;
                    xPrev += corr * (1.0 - g_bodyBounce);
                }
            }
            // PES21 FX: colision precisa. El plano promedio subestima el muslo cuando la rodilla se dobla o el muslo
            // gira: aca cada vertice emparejado empuja con su propio plano tangente (se aplica el mas violado), dos veces.
            if (PES_LEG_PRECISE) {
                [loop] for (uint it = 0; it < PES_LEG_ITER; ++it) {
                    float worst = 0.0; float3 wn = 0;
                    [loop] for (uint k2 = 0; k2 < 8; ++k2) {
                        uint v2 = (lp[k2 >> 1] >> ((k2 & 1) * 16)) & 0xffff;
                        if (v2 == 0xffff) break;
                        uint lo2 = (s_palBase / (2 * MAXPARTS * 192)) * g_legCount * 2 + v2 * 2;
                        float3 yk2 = g_legSkin[lo2].xyz;
                        float3 nk2 = g_legSkin[lo2 + 1].xyz;
                        if (dot(nk2, nk2) < 1e-8 || !all(isfinite(yk2))) continue;
                        float3 dxy = x - yk2;
                        float dk2 = length(dxy);
                        if (dk2 > PES_LEG_REACH) continue;
                        nk2 = normalize(nk2);
                        float dpl = dot(dxy, nk2);
                        // el plano tangente solo vale cerca del vertice: lejos de el, la esfera (distancia al punto)
                        float lateral = length(dxy - nk2 * dpl);
                        // cerca del vertice: su plano tangente. Al costado: solo una esfera chica alrededor del vertice
                        // (ERROR corregido 18/10: la version anterior empujaba desde lejos y hacia globos)
                        float viol; float3 pushN;
                        if (lateral < 0.035) { viol = g_legMargin - dpl; pushN = nk2; }
                        else { viol = g_legMargin * 0.7 - dk2; pushN = dxy / max(dk2, 1e-5); }
                        if (viol > worst) { worst = viol; wn = pushN; }
                    }
                    if (worst <= 0.0) break;
                    float3 corr2 = wn * worst;
                    x += corr2;
                    xPrev += corr2 * (1.0 - g_bodyBounce);
                }
            }
        }
        float contactD = sd + inset - g_collisionInset;
        if (capN > 0) {
            float3 r0 = rest1 - rnrm1 * g_rest[p].nrm.w;
            float best = 1e9;
            uint own = 0;
            for (uint ci = 0; ci < capN; ++ci) {
                float3 ab = gCapB[ci] - gCapA[ci];
                float tt = saturate(dot(r0 - gCapA[ci], ab) / max(dot(ab, ab), 1e-8));
                float d0 = length(r0 - (gCapA[ci] + ab * tt));
                if (d0 < best) { best = d0; own = ci; }
            }
            float3 ab = gCapB[own] - gCapA[own];
            float tt = saturate(dot(x - gCapA[own], ab) / max(dot(ab, ab), 1e-8));
            float3 q = gCapA[own] + ab * tt;
            float3 dq = x - q;
            float dd = length(dq);
            float R = max(min(best - g_collisionInset, gCapR[own] * g_capsuleRadiusScale), 0.0);
            if (dd < R && dd > 1e-6) {
                float3 corr = dq * (R / dd - 1.0);
                x += corr;
                xPrev += corr * (1.0 - g_bodyBounce);
                dd = R;
            }
            contactD = dd - R;
        }
        float contact = contactD <= 0 ? 1.0 : (g_contactBand > 0 ? saturate(1.0 - contactD / g_contactBand) : 0.0);
        if (contact > 0) {
            float3 dRel = (x - xPrev) - (rest1 - restPrev);
            float3 dTan = dRel - rnrm1 * dot(dRel, rnrm1);
            x -= dTan * (min(g_friction * (1.0 + 3.0 * PES_WET), 0.9) * contact);   // PES21 FX: mojada se pega
        }
        if (s_coupleOn) {
            uint4 cd = g_couple[3 * p + 1];
            if (cd.z > 0) {
                uint4 cv = g_couple[3 * p];
                float side = asfloat(cd.x), gap = asfloat(cd.y);
                float3 q = x;
                float3x4 M = identity34();
                bool ok = true;
                if (s_coupleOn == 1) {
                    ParticleRest rr = g_rest[p];
                    M = skinC(rr.skin.z, rr.skin.x, rr.skin.y);
                    float det;
                    float3x3 Mi = inverse3((float3x3)M, det);
                    ok = abs(det) > 1e-8;
                    q = mul(Mi, x - float3(M[0].w, M[1].w, M[2].w));
                }
                if (side < -1.5) {
                    x = rest1;
                    ok = false;
                }
                float bestD = 1e9;
                float3 y = 0, n = 0;
                float wsum = 0;
                for (uint k = 0; k < min(cd.z, 8u) && ok; ++k) {
                    uint v = (cv[k >> 1] >> ((k & 1) * 16)) & 0xffff;
                    float3 yk, nk;
                    if (s_coupleOn == 2 && g_otherVertexCount) {
                        uint co = (s_palBase / (2 * MAXPARTS * 192)) * g_otherVertexCount * 2 + v * 2;
                        yk = g_coupleSkin[co].xyz;
                        nk = g_coupleSkin[co + 1].xyz;
                    } else {
                        yk = asfloat(g_otherCache.Load3(s_otherCacheBase + v * 32));
                        uint2 nn = g_otherCache.Load2(s_otherCacheBase + v * 32 + 16);
                        nk = float3(f16tof32(nn.x), f16tof32(nn.x >> 16), f16tof32(nn.y));
                        if (s_coupleOn == 2) {
                            uint2 sk = g_otherSkin.Load2(v * 8);
                            float3x4 S = skinC(MAXPARTS + min(g_otherPart[v], MAXPARTS - 1), sk.x, sk.y);
                            yk = xform(S, yk);
                            nk = mul((float3x3)S, nk);
                        }
                    }
                    float dk = distance(q, yk);
                    bestD = min(bestD, dk);
                    if (dot(nk, nk) > 1e-6 && all(isfinite(yk))) {
                        float wk = 1.0 / (dk * dk + 1e-4);
                        y += yk * wk;
                        n += normalize(nk) * wk;
                        wsum += wk;
                    }
                }
                if (wsum > 0) y /= wsum;
                if (ok && wsum > 0 && bestD < 0.15 && dot(n, n) > 1e-6 && all(isfinite(y))) {
                    n = normalize(n);
                    float d = side * dot(q - y, n);
                    if (d < gap) {
                        float3 corr = mul((float3x3)M, n) * (side * (gap - d) * 0.5);
                        x += corr;
                        xPrev += corr;
                    }
                }
            }
            bool tucked = cd.z > 0 && asfloat(cd.x) < -1.5;
            if (s_coupleOn == 2 && s_otherCaps > 0 && !tucked) {
                float4 th = asfloat(g_couple[3 * p + 2]);
                for (uint k = 0; k < min(s_otherCaps, 4u); ++k) {
                    if (th[k] <= 0) continue;
                    Capsule cp = g_otherCapsules[k];
                    uint code = uint(cp.b.w + 0.5);
                    float3x4 B = boneC(MAXPARTS + min(code >> 8, MAXPARTS - 1), code & 255);
                    float3 ca = xform(B, cp.a.xyz), cb = xform(B, cp.b.xyz);
                    float3 ab = cb - ca;
                    float tt = dot(x - ca, ab) / max(dot(ab, ab), 1e-8);
                    if (tt < 0 || tt > 1) continue;
                    float3 dq = x - (ca + ab * tt);
                    float dd = length(dq);
                    if (dd < th[k] && dd > 1e-6) {
                        float3 corr = dq * (th[k] / dd - 1.0) * 0.5;
                        x += corr;
                        xPrev += corr;
                    }
                }
            }
        }
        gx[p] = x;
        vel = (x - xPrev) / h;
    }
    restPrev = rest1;
}
void storeParticle(uint p, float3 vel)
{
    g_state[s_stateBase + (2 * p)] = float4(gx[p], 0);
    g_state[s_stateBase + (2 * p + 1)] = float4(vel, 0);
}
[numthreads(THREADS, 1, 1)]
void main(uint gi : SV_GroupIndex, uint3 gid : SV_GroupID)
{
    Inst me = g_inst[gid.x];
    s_stamp = me.stamp; s_substeps = me.substeps; s_flags = me.flags; s_coupleOn = me.coupleOn;
    s_otherCaps = me.otherCaps; s_stateBase = me.stateBase; s_palBase = me.palBase; s_cacheBase = me.cacheBase;
    s_outBase0 = me.outBase0; s_outBase1 = me.outBase1; s_pnrmBase = me.pnrmBase; s_otherCacheBase = me.otherCacheBase;
    s_W[0] = me.world[0]; s_W[1] = me.world[1]; s_W[2] = me.world[2];
    s_Wp[0] = me.worldPrev[0]; s_Wp[1] = me.worldPrev[1]; s_Wp[2] = me.worldPrev[2];
    s_kick = me.kick.xyz;
    uint P = min(g_particleCount, MAXP);
    uint part = min(g_part, MAXPARTS - 1);
    bool preStep = (s_flags & 16) != 0;
    if (gi == 0) gFirstVisit = uint(g_state[s_stateBase + (2 * P + 2 + part)].x) != s_stamp ? 1 : 0;
    GroupMemoryBarrierWithGroupSync();
    if (gi == 0) {
        uint stampIdx = 2 * P + 2;
        bool complete;
        if (preStep) {
            complete = true;
        } else {
            bool secondVisit = gFirstVisit == 0;
            g_state[s_stateBase + (stampIdx + part)] = float4(float(s_stamp), 0, 0, 0);
            bool allParts = true;
            for (uint k = 0; k < g_numParts && k < MAXPARTS; ++k)
                if (k != part && uint(g_state[s_stateBase + (stampIdx + k)].x) != s_stamp) allParts = false;
            complete = allParts || secondVisit;
        }
        float4 hdrA = g_state[s_stateBase + (2 * P)];
        float4 hdrB = g_state[s_stateBase + (2 * P + 1)];
        float3 anchor, an;
        restAt(0, 1.0, anchor, an);
        bool claimed = hdrB.x > 0.5;
        uint lastSeen = uint(hdrB.y);
        bool stale = !claimed || (!preStep && (s_stamp - lastSeen) > 30);
        bool near = distance(anchor, hdrA.yzw) < g_matchRadius;
        bool tracked = preStep ? complete : (stale ? complete : near);
        uint mode = 0;
        if (tracked) {
            mode |= 1;
            bool reset = complete && (stale || (s_flags & 1) != 0 || (preStep && !near));
            bool step = complete && (s_flags & 2) && (uint(hdrA.x) != s_stamp || reset || (s_flags & 512));
            if (step) mode |= 2;
            if (reset) mode |= 4;
            if (step) hdrA.x = float(s_stamp);
            if (complete) hdrA.yzw = anchor;
            g_state[s_stateBase + (2 * P)] = hdrA;
            g_state[s_stateBase + (2 * P + 1)] = float4(1.0, float(s_stamp), 0, 0);
        }
        gMode = mode;
    }
    GroupMemoryBarrierWithGroupSync();
    uint mode = gMode;
    if (mode & 1) {
        fillBoneCache(gi, (mode & 4) ? 1.0 : 0.0);
        DeviceMemoryBarrierWithGroupSync();
        float3 xPrev0 = 0, vel0 = 0, restPrev0 = 0, xPrev1 = 0, vel1 = 0, restPrev1 = 0;
        if (gi < P) loadParticle(gi, mode, xPrev0, vel0, restPrev0);
        if (gi + THREADS < P) loadParticle(gi + THREADS, mode, xPrev1, vel1, restPrev1);
        GroupMemoryBarrierWithGroupSync();
        if ((mode & 2) && !(mode & 4) && s_substeps > 0) {
            float h = g_h;
            float3x3 W = (s_flags & 32) ? float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1)
                                        : float3x3(s_W[0].xyz, s_W[1].xyz, s_W[2].xyz);
            float3 grav = mul(transpose(W), g_gravity.xyz);
            s_grav = grav;   // PES21 FX
            // PES21 FX: velocidad angular de la prenda a partir de la rotacion entre el cuadro anterior y este
            {
              float3x3 R0 = float3x3(s_Wp[0].xyz, s_Wp[1].xyz, s_Wp[2].xyz), R1 = float3x3(s_W[0].xyz, s_W[1].xyz, s_W[2].xyz);
              float3x3 dR = mul(R1, transpose(R0));   // aprox. rotacion incremental (mundo)
              float3 axisW = float3(dR[2][1] - dR[1][2], dR[0][2] - dR[2][0], dR[1][0] - dR[0][1]) * 0.5;
              float dt = max(h * float(s_substeps), 1e-4);
              float3 omegaW = axisW / dt;
              float ol = length(omegaW);
              if (ol > 12.0) omegaW *= 12.0 / ol;   // tope: cortes de camara / teletransportes no son giros
              s_omega = (s_flags & 32) ? omegaW : mul(transpose(W), omegaW);
              s_center = (s_flags & 32) ? float3(s_W[0].w, s_W[1].w, s_W[2].w) : 0;
            }
            float3 windBase = mul(transpose(W), g_wind.xyz);
            for (uint s = 0; s < s_substeps; ++s) {
                float t = float(s + 1) / float(s_substeps);
                float gust = 1.0 + g_wind.w * sin(g_time * 1.7 + s * 0.05) * sin(g_time * 0.63);
                fillBoneCache(gi, t);
                DeviceMemoryBarrierWithGroupSync();
                if (gi < g_capsuleCount && gi < MAXCAPS) {
                    Capsule c = g_caps[gi];
                    uint code = uint(c.b.w + 0.5);
                    float3x4 B = boneC(min(code >> 8, MAXPARTS - 1), code & 255);
                    gCapA[gi] = xform(B, c.a.xyz);
                    gCapB[gi] = xform(B, c.b.xyz);
                    gCapR[gi] = c.a.w;
                }
                float3 rest10 = 0, rnrm10 = 0, rest11 = 0, rnrm11 = 0;
                if (gi < P && !(s_flags & (16 << 12))) predictParticle(gi, s, h, grav, windBase, gust, xPrev0, vel0, restPrev0, rest10, rnrm10);
                if (gi + THREADS < P) predictParticle(gi + THREADS, s, h, grav, windBase, gust, xPrev1, vel1, restPrev1, rest11, rnrm11);
                if ((s_flags & 128) && g_legCount) {
                    uint lb = (s_palBase / (2 * MAXPARTS * 192)) * g_legCount * 2;
                    for (uint lv = gi; lv < g_legCount; lv += THREADS) {
                        LegV L = g_legV[lv];
                        float3x4 S = skinC(MAXPARTS + min(asuint(L.pos.w), MAXPARTS - 1), L.skin.x, L.skin.y);
                        g_legSkin[lb + lv * 2] = float4(xform(S, L.pos.xyz), 0);
                        g_legSkin[lb + lv * 2 + 1] = float4(mul((float3x3)S, L.nrm.xyz), 0);
                    }
                }
                if (s_coupleOn == 2 && g_otherVertexCount) {
                    uint cb = (s_palBase / (2 * MAXPARTS * 192)) * g_otherVertexCount * 2;
                    for (uint ov = gi; ov < g_otherVertexCount; ov += THREADS) {
                        float3 yk = asfloat(g_otherCache.Load3(s_otherCacheBase + ov * 32));
                        uint2 nn = g_otherCache.Load2(s_otherCacheBase + ov * 32 + 16);
                        float3 nk = float3(f16tof32(nn.x), f16tof32(nn.x >> 16), f16tof32(nn.y));
                        uint2 sk = g_otherSkin.Load2(ov * 8);
                        float3x4 S = skinC(MAXPARTS + min(g_otherPart[ov], MAXPARTS - 1), sk.x, sk.y);
                        g_coupleSkin[cb + ov * 2] = float4(xform(S, yk), 0);
                        g_coupleSkin[cb + ov * 2 + 1] = float4(mul((float3x3)S, nk), 0);
                    }
                }
                AllMemoryBarrierWithGroupSync();
                float hh = h * h;
                for (uint c = 0; c < g_colorCount && !(s_flags & (4 << 12)); ++c) {
                    uint a0 = g_colorOff[c], a1 = g_colorOff[c + 1];
                    for (uint i = a0 + gi; i < a1; i += THREADS) {
                        uint4 cn = g_cons[i];
                        bool tuckedPins = (s_flags & 256) != 0;
                        float wa = (cn.w & (tuckedPins ? 8u : 2u)) ? 0.0 : 1.0;
                        float wb = (cn.w & (tuckedPins ? 16u : 4u)) ? 0.0 : 1.0;
                        float3 d = gx[cn.x] - gx[cn.y];
                        float len = length(d);
                        if (len < 1e-7) continue;
                        float L = asfloat(cn.z);
                        float ad = max(g_restAdapt, float((cn.w >> 8) & 255) / 255.0);
                        if (ad > 0)
                            L = lerp(L, distance(g_pnrm[s_pnrmBase + (cn.x)].xyz, g_pnrm[s_pnrmBase + (cn.y)].xyz), ad);
                        float C = len - L;
                        float alpha = (cn.w & 1) == 0 ? (C < 0 ? g_compressCompliance : g_stretchCompliance) : g_bendCompliance * (g_legCount > 0 ? PES_SHORTS_BEND : 1.0);   // PES21 FX
                        float denom = wa + wb + alpha / hh;
                        if (denom < 1e-9) continue;
                        float3 corr = (-C / denom) * (d / len);
                        gx[cn.x] += wa * corr;
                        gx[cn.y] -= wb * corr;
                    }
                    GroupMemoryBarrierWithGroupSync();
                }
                if (gi < P && !(s_flags & (8 << 12))) collideParticle(gi, t, h, hh, xPrev0, vel0, restPrev0, rest10, rnrm10);
                if (gi + THREADS < P) collideParticle(gi + THREADS, t, h, hh, xPrev1, vel1, restPrev1, rest11, rnrm11);
                GroupMemoryBarrierWithGroupSync();
            }
        }
        if (mode & 2) {
            if (gi < P) storeParticle(gi, vel0);
            if (gi + THREADS < P) storeParticle(gi + THREADS, vel1);
        }
        for (uint p = gi; p < P && !(s_flags & (1 << 12)); p += THREADS) {
            float3 n = 0;
            for (uint j = g_adjOff[p]; j < g_adjOff[p + 1]; ++j) {
                uint4 tr = g_tris[g_adjTri[j]];
                n += cross(gx[tr.y] - gx[tr.x], gx[tr.z] - gx[tr.x]);
            }
            n *= g_normalSign;
            g_pnrm[s_pnrmBase + (p)] = float4(n, 0);
        }
        DeviceMemoryBarrierWithGroupSync();
    }
    if (mode & 6) {
        if (!((mode & 4) || ((mode & 2) && s_substeps > 0))) {
            fillBoneCache(gi, 1.0);
            DeviceMemoryBarrierWithGroupSync();
        }
        uint pEnd = min(g_partStart + g_partCount, g_vertexCount);
        for (uint i = gi; i < g_vertexCount && !(s_flags & (2 << 12)); i += THREADS) {
            float3 v  = asfloat(g_bindPos.Load3(i * 12));
            uint4  nt = g_nrmTan.Load4(i * 16);
            uint2  sk = g_skin.Load2(i * 8);
            float3x4 M = skinC(min(g_rpart[i], MAXPARTS - 1), sk.x, sk.y);
            float3x3 A = (float3x3)M;
            float3   tr = float3(M[0].w, M[1].w, M[2].w);
            float3 nB = float3(f16tof32(nt.x), f16tof32(nt.x >> 16), f16tof32(nt.y));
            float3 tB = float3(f16tof32(nt.z), f16tof32(nt.z >> 16), f16tof32(nt.w));
            uint p = g_r2p[i];
            float3 x = gx[min(p, MAXP - 1)];
            float3 nRest = normalize(mul(A, nB));
            float3 tRest = mul(A, tB);
            float3 n = g_pnrm[s_pnrmBase + (p)].xyz;
            n = dot(n, n) > 1e-12 ? normalize(n) : nRest;
            float3 t = rotateBetween(nRest, n, tRest);
            float det;
            float3x3 Ai = inverse3(A, det);
            float3 outV = v;
            uint4 outNT = nt;
            if (abs(det) > 1e-8 && all(isfinite(x))) {
                outV = mul(Ai, x - tr);
                float3 nb = normalize(mul(Ai, n));
                float3 tb = normalize(mul(Ai, t));
                outNT.x = packHalf2(nb.x, nb.y);
                outNT.y = f32tof16(nb.z) | (nt.y & 0xffff0000);
                outNT.z = packHalf2(tb.x, tb.y);
                outNT.w = f32tof16(tb.z) | (nt.w & 0xffff0000);
            }
            g_cache.Store4(s_cacheBase + i * 32, uint4(asuint(outV), 0));
            g_cache.Store4(s_cacheBase + i * 32 + 16, outNT);
            if (i >= g_partStart && i < pEnd) {
                g_outPos.Store3(s_outBase0 + i * g_stride0, asuint(outV));
                g_outNrm.Store4(s_outBase1 + i * g_stride1, outNT);
            }
        }
        return;
    }
    uint vEnd = min(g_partStart + g_partCount, g_vertexCount);
    for (uint i = g_partStart + gi; i < vEnd; i += THREADS) {
        float3 outV;
        uint4 outNT;
        if (mode & 1) {
            outV = asfloat(g_cache.Load3(s_cacheBase + i * 32));
            outNT = g_cache.Load4(s_cacheBase + i * 32 + 16);
        } else {
            outV = asfloat(g_bindPos.Load3(i * 12));
            outNT = g_nrmTan.Load4(i * 16);
        }
        g_outPos.Store3(s_outBase0 + i * g_stride0, asuint(outV));
        g_outNrm.Store4(s_outBase1 + i * g_stride1, outNT);
    }
}
