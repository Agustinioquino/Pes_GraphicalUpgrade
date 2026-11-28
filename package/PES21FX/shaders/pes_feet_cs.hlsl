// PES21FX (13/11) x505 BOTINES EN EL PASTO: una vez por cuadro (Present, contexto inmediato) elige los pies que tocan el pasto cerca de la
// camara para el VS del pasto (498b30, t10 pes_feet). Un grupo de 256 hilos, un hilo por lugar del pozo de botines (pitchwear.cpp).
// Lugar (116 float4): 0 sello del cuadro (x gen), 1 (huesos, 1 = hay cuadro anterior), 2..6 b5 c4..c8 (m_world por columnas: x = dot(p, c4);
// c8.x = m_useWeightCount), 7.. b6 desde c0: hueso k = 3 registros desde 7 + 3k, cuadro anterior (g_vsPrevBone, c96) desde 103 + 3k.
// Matriz de piel por columnas: x' = dot(p, r0), y' = dot(p, r1), z' = dot(p, r2); traslacion T = (r0.w, r1.w, r2.w) = el origen del modelo
// (el piso entre los pies) pegado al hueso: para el pie, ~0 apoyado y corrido hacia adentro; la suela va a +-9 cm en x del modelo, del lado
// que dice la cadera del jugador (la misma regla que las pisadas). Solo se ve: nada vuelve a la CPU ni al juego
Buffer<float4> FtRaw : register(t0);
RWStructuredBuffer<float4> FtOut : register(u0);
cbuffer FtCB : register(b0) {
  float4 g0;      // x gen de este cuadro, y lugares, z pies a la vez, w distancia D (m)
  float4 g1;      // xyz ojo, w 1 = valido
  float4 g2;      // x mascara de huesos que son pies, y signo del frente (+1 / -1 / 0 = sin saber), z tau (s de juego), w dt de juego (s)
  float4 g3;      // piso de los huesos 0..3 (m)
  float4 g4;      // x talon, y punta, z radio de la suela, w alcance del borde (m)
  float4 g5;      // x 1 = empezar de cero, y retencion del apoyo (s), z regla del lado (+1 lejos de la cadera, -1 cerca), w caderas
  float4 g6;      // x alto de contacto (m), y m/s de apoyado, z banda en la que se mantiene el lado (m), w corrimiento al costado (m)
  float4 gH[24];  // caderas (x, z) de hasta 48 jugadores
};
#define NS 256
#define SF4 116
#define NL 16
#define NG 16
#define OB 3
#define OL 35
#define OG 115
#define OD 147

groupshared float4 cP[NS];     // suela de este cuadro (x, alto del hueso, z), w distancia al ojo
groupshared float4 cQ[NS];     // suela del cuadro anterior (x, alto, z), w 1 = hay
groupshared float2 cF[NS];     // frente xz (normalizado, 0 = sin saber)
groupshared float2 cT[NS];     // T en el mundo (xz): para reconocer el pie en el cuadro siguiente
groupshared float  cS[NS];     // lado elegido (+1 / -1)
groupshared uint   cK[NS];     // hueso * 256 + lugar: eleccion estable entre repetidos
groupshared float  cYm[NS];    // el alto mas bajo de su grupo
groupshared uint   cKeep[NS];
groupshared uint   cByRank[NL];
groupshared uint   nC, nOver, nNow, nKeep, nNoHip, nSideKeep, nSideFlip, pvN, ghN;
groupshared float  dOutN;
groupshared float2 pvT[NL];
groupshared float4 pvL0[NL];
groupshared float  pvY[NL], pvS[NL];
groupshared float4 gh0[NG], gh1[NG];
groupshared float4 lvAb[NL];
groupshared float  lvY[NL], lvV[NL], lvW[NL];

bool FtOk(float3 w) { return abs(w.x) < 80.0 && abs(w.z) < 50.0 && w.y > -0.08 && w.y < 0.5 && abs(w.x) + abs(w.z) > 0.02; }
float3 FtW(float3 p, float4 c4, float4 c5, float4 c6) { float4 q = float4(p, 1.0); return float3(dot(q, c4), dot(q, c5), dot(q, c6)); }
float3 FtV(float3 v, float4 c4, float4 c5, float4 c6) { float4 q = float4(v, 0.0); return float3(dot(q, c4), dot(q, c5), dot(q, c6)); }
float FtFloor(uint k) { return k == 0 ? g3.x : (k == 1 ? g3.y : (k == 2 ? g3.z : g3.w)); }
float2 FtHip(uint h) { float4 q = gH[h >> 1]; return (h & 1u) != 0u ? q.zw : q.xy; }

[numthreads(NS, 1, 1)]
void main(uint ti : SV_GroupIndex) {
  // 0) lo del cuadro anterior (pies y huellas que se levantan), antes de escribir nada
  float4 h0 = FtOut[0];
  bool okPrev = g5.x < 0.5 && abs(h0.z - 505.0) < 0.5;
  if (ti == 0) {
    nC = 0; nOver = 0; nNow = 0; nKeep = 0; nNoHip = 0; nSideKeep = 0; nSideFlip = 0; dOutN = g0.w + 1.0;
    pvN = okPrev ? (uint)clamp(h0.x + 0.5, 0.0, (float)NL) : 0u;
    ghN = okPrev ? (uint)clamp(h0.y + 0.5, 0.0, (float)NG) : 0u;
  }
  if (ti < NL) {
    pvL0[ti] = FtOut[OL + 5 * ti];
    pvY[ti] = FtOut[OL + 5 * ti + 1].x;
    pvS[ti] = FtOut[OL + 5 * ti + 3].z;
    pvT[ti] = FtOut[OL + 5 * ti + 4].xy;
  }
  if (ti < NG) { gh0[ti] = FtOut[OG + 2 * ti]; gh1[ti] = FtOut[OG + 2 * ti + 1]; }
  GroupMemoryBarrierWithGroupSync();
  // 1) cada lugar de este cuadro: personaje con huesos (c8.x != 0; la pelota no), cada hueso que es pie, al mundo, su lado, cerca del ojo
  if (ti < (uint)g0.y && g1.w > 0.5) {
    uint b = ti * SF4;
    float4 hd = FtRaw[b];
    if (abs(hd.x - g0.x) < 0.5) {
      InterlockedAdd(nNow, 1);
      float4 fl = FtRaw[b + 1], c4 = FtRaw[b + 2], c5 = FtRaw[b + 3], c6 = FtRaw[b + 4], c8 = FtRaw[b + 6];
      uint nB = (uint)clamp(fl.x + 0.5, 0.0, 4.0); bool pv = fl.y > 0.5;
      uint mask = (uint)(g2.x + 0.5), nH = (uint)clamp(g5.w + 0.5, 0.0, 48.0);
      if (c8.x != 0.0) {
        [loop] for (uint k = 0; k < nB; k++) {
          if (((mask >> k) & 1u) == 0u) continue;
          float4 r0 = FtRaw[b + 7 + 3 * k], r1 = FtRaw[b + 8 + 3 * k], r2 = FtRaw[b + 9 + 3 * k];
          float3 T = float3(r0.w, r1.w, r2.w), X = float3(r0.x, r1.x, r2.x), Z = float3(r0.z, r1.z, r2.z);
          if (abs(T.x) + abs(T.y) + abs(T.z) < 1e-4 || abs(T.x) > 200.0 || abs(T.y) > 200.0 || abs(T.z) > 200.0) continue;
          float3 Tw = FtW(T, c4, c5, c6); bool raw = false;
          if (!FtOk(Tw)) { Tw = T; raw = true; }
          if (!FtOk(Tw) || Tw.y > 0.15) continue;
          float3 Xw = raw ? X : FtV(X, c4, c5, c6), Zw = raw ? Z : FtV(Z, c4, c5, c6);
          // la cadera mas cercana (a menos de 1.2 m): sin cadera no se sabe de que lado va el pie y no se dibuja
          float bH = 1.44; float2 H = float2(0.0, 0.0);
          [loop] for (uint h = 0; h < nH; h++) { float2 e = FtHip(h) - Tw.xz; float d2 = dot(e, e); if (d2 < bH) { bH = d2; H = FtHip(h); } }
          if (bH >= 1.44) { InterlockedAdd(nNoHip, 1); continue; }
          // lado: el costado lejos (g5.z = +1) o cerca (-1) de la cadera; cerca del empate se mantiene el del mismo pie en el cuadro anterior
          float v = dot(Xw.xz, Tw.xz - H);
          float sd = g5.z * (v >= 0.0 ? 1.0 : -1.0);
          float bP = 0.0144; int pm = -1;
          for (uint p = 0; p < pvN; p++) { float2 e = pvT[p] - Tw.xz; float d2 = dot(e, e); if (d2 < bP) { bP = d2; pm = (int)p; } }
          if (pm >= 0) {
            if (abs(v) < g6.z) { if (pvS[pm] != sd) InterlockedAdd(nSideKeep, 1); sd = pvS[pm]; }
            else if (pvS[pm] != sd) InterlockedAdd(nSideFlip, 1);
          }
          float3 Sw = Tw + Xw * (g6.w * sd);
          float dE = length(Sw - g1.xyz);
          if (dE > g0.w) continue;
          float2 F = Zw.xz; float lF = length(F); F = lF > 1e-3 ? F / lF : float2(0.0, 0.0);
          float4 Qp = float4(0.0, 0.0, 0.0, 0.0);
          if (pv) {
            float4 p0 = FtRaw[b + 103 + 3 * k], p1 = FtRaw[b + 104 + 3 * k], p2 = FtRaw[b + 105 + 3 * k];
            float3 Tp = float3(p0.w, p1.w, p2.w), Xp = float3(p0.x, p1.x, p2.x);
            float3 Tpw = raw ? Tp : FtW(Tp, c4, c5, c6), Xpw = raw ? Xp : FtV(Xp, c4, c5, c6);
            float3 Spw = Tpw + Xpw * (g6.w * sd);
            if (length(Spw - Sw) < 0.3) Qp = float4(Spw.x, Tpw.y, Spw.z, 1.0);
          }
          uint ci; InterlockedAdd(nC, 1, ci);
          if (ci < NS) { cP[ci] = float4(Sw.x, Tw.y, Sw.z, dE); cQ[ci] = Qp; cF[ci] = F; cT[ci] = Tw.xz; cS[ci] = sd; cK[ci] = k * 256u + ti; }
          else InterlockedAdd(nOver, 1);
        }
      }
    }
  }
  GroupMemoryBarrierWithGroupSync();
  // 2) el mismo pie dos veces (talon y punta del mismo botin, otra pasada del mismo dibujo): queda el de menor (hueso, lugar), con el alto
  //    mas bajo del grupo (no salta de un hueso al otro entre cuadros)
  uint n = min(nC, (uint)NS);
  if (ti < n) {
    uint keep = 1; float4 p = cP[ti]; float ym = p.y;
    for (uint j = 0; j < n; j++) {
      if (j == ti) continue;
      float4 q = cP[j]; float2 e = q.xz - p.xz;
      if (dot(e, e) < 0.0036 && abs(q.y - p.y) < 0.04) { ym = min(ym, q.y); if (cK[j] < cK[ti]) keep = 0; }
    }
    cKeep[ti] = keep; cYm[ti] = ym;
  }
  GroupMemoryBarrierWithGroupSync();
  // 3) los N mas cerca del ojo; dOutN = distancia del primero que no entro (fundido de rango)
  uint maxF = (uint)clamp(g0.z + 0.5, 1.0, (float)NL);
  if (ti < n && cKeep[ti] != 0) {
    float dI = cP[ti].w; uint r = 0;
    for (uint j = 0; j < n; j++) if (cKeep[j] != 0 && (cP[j].w < dI || (cP[j].w == dI && j < ti))) r++;
    InterlockedAdd(nKeep, 1);
    if (r < maxF) cByRank[r] = ti;
    else if (r == maxF) dOutN = dI;
  }
  GroupMemoryBarrierWithGroupSync();
  // 4) pies en vivo: capsula talon -> punta adelantada un cuadro con la paleta anterior del juego (el pasto que la lee es del cuadro que
  //    viene); la de antes (TAA) es la que dibujo el pasto en el cuadro anterior (el mismo pie, reconocido por T)
  uint nL = min(nKeep, maxF);
  float rr = g4.z, reach = g4.w;
  if (ti < nL) {
    uint i = cByRank[ti];
    float4 P = cP[i], Q = cQ[i]; float2 F = cF[i];
    float fl = FtFloor(cK[i] >> 8);
    float3 St = float3(P.x, cYm[i], P.z);
    float3 dS = (Q.w > 0.5 && g2.w > 1e-6) ? P.xyz - Q.xyz : float3(0.0, 0.0, 0.0);
    float3 Sn = St + dS;
    float ysT = St.y - fl, ysN = max(Sn.y - fl, -0.02);
    float v = g2.w > 1e-6 ? length(dS.xz) / g2.w : 0.0;
    float sg = g2.y;
    float hl = sg != 0.0 ? g4.x : 0.5 * (g4.x + g4.y) + 0.02, tl = sg != 0.0 ? g4.y : hl;
    float2 Fs = F * (sg != 0.0 ? sg : 1.0);
    float4 abN = float4(Sn.xz - Fs * hl, Sn.xz + Fs * tl), abT = float4(St.xz - Fs * hl, St.xz + Fs * tl);
    float bP = 0.0144; int pm = -1;
    for (uint p = 0; p < pvN; p++) { float2 e = pvT[p] - cT[i]; float d2 = dot(e, e); if (d2 < bP) { bP = d2; pm = (int)p; } }
    float4 abP = pm >= 0 ? pvL0[pm] : abT; float ysP = pm >= 0 ? pvY[pm] : ysT; float pOk = pm >= 0 ? 1.0 : 0.0;
    float wD = smoothstep(g0.w, g0.w - 2.0, P.w) * saturate((dOutN - P.w) / 0.25);
    float2 dir = length(dS.xz) > 1e-4 ? normalize(dS.xz) * saturate(v / 3.0) : float2(0.0, 0.0);
    float2 cN = 0.5 * (abN.xy + abN.zw), cPv = 0.5 * (abP.xy + abP.zw);
    float R = 0.5 * length(abN.zw - abN.xy) + rr + reach + pOk * length(cN - cPv) + 0.02;
    FtOut[OB + ti] = float4(cN, R * R, rr);
    FtOut[OL + 5 * ti] = abN;
    FtOut[OL + 5 * ti + 1] = float4(ysN, wD, dir);
    FtOut[OL + 5 * ti + 2] = abP;
    FtOut[OL + 5 * ti + 3] = float4(ysP, v, cS[i], pOk);
    FtOut[OL + 5 * ti + 4] = float4(cT[i] + dS.xz, 0.0, 0.0);
    lvAb[ti] = abT; lvY[ti] = ysT; lvV[ti] = v; lvW[ti] = wD;
  }
  AllMemoryBarrierWithGroupSync();
  // 5) huellas que se levantan (el pasto se para despacio): el pie en contacto (suela < 3 cm) deja su capsula. Apoyado (< 0.8 m/s) la
  //    renueva (edad 0); moviendose (barrida, pie que roza las puntas) deja una nueva cada 8 cm. Peso = el del pie al dejarla; retencion
  //    g5.y s y despues exp con tau (s de juego: en pausa no cambia). Tope min(16, 2 x pies a la vez)
  if (ti == 0) {
    float tau = max(g2.z, 0.02), hold = g5.y, dt = g2.w;
    uint gcap = min((uint)NG, 2u * maxF);
    for (uint j0 = 0; j0 < ghN; j0++) gh1[j0].z += dt;
    float ysSum = 0.0, ysCnt = 0.0;
    for (uint r1 = 0; r1 < nL; r1++) {
      bool planted = lvV[r1] < g6.y;
      if (planted) { ysSum += lvY[r1]; ysCnt += 1.0; }
      if (lvY[r1] > g6.x) continue;
      float2 c = 0.5 * (lvAb[r1].xy + lvAb[r1].zw);
      int jm = -1; float dm = 1e9;
      for (uint j1 = 0; j1 < ghN; j1++) { float2 e = c - 0.5 * (gh0[j1].xy + gh0[j1].zw); float d2 = dot(e, e); if (d2 < dm) { dm = d2; jm = (int)j1; } }
      int hit = -1;
      if (planted && jm >= 0 && dm < 0.01) hit = jm;
      else if (!planted && jm >= 0 && dm < 0.0064) continue;
      if (hit < 0 && ghN < gcap) { hit = (int)ghN; ghN++; }
      if (hit < 0) { float wMin = 1e9; for (uint j2 = 0; j2 < ghN; j2++) if (gh1[j2].y < wMin) { wMin = gh1[j2].y; hit = (int)j2; } }
      if (hit >= 0) { gh0[hit] = lvAb[r1]; gh1[hit] = float4(lvY[r1], lvW[r1], 0.0, lvW[r1]); }
    }
    uint m = 0; float4 bb = float4(1e9, 1e9, -1e9, -1e9);
    for (uint j3 = 0; j3 < ghN && m < gcap; j3++) {
      float age = gh1[j3].z, w = gh1[j3].w * (age <= hold ? 1.0 : exp(-(age - hold) / tau));
      float2 cg = 0.5 * (gh0[j3].xy + gh0[j3].zw);
      w *= smoothstep(g0.w, g0.w - 2.0, length(float3(cg.x, 0.0, cg.y) - g1.xyz));
      if (w < 0.05) continue;
      float R = 0.5 * length(gh0[j3].zw - gh0[j3].xy) + rr + reach + 0.02;
      FtOut[OG + 2 * m] = gh0[j3]; FtOut[OG + 2 * m + 1] = float4(gh1[j3].x, w, age, gh1[j3].w);
      FtOut[OB + NL + m] = float4(cg, R * R, rr);
      bb = float4(min(bb.xy, cg - R), max(bb.zw, cg + R));
      m++;
    }
    for (uint r2 = 0; r2 < nL; r2++) { float4 B = FtOut[OB + r2]; float R = sqrt(B.z); bb = float4(min(bb.xy, B.xy - R), max(bb.zw, B.xy + R)); }
    FtOut[0] = float4((float)nL, (float)m, g1.w > 0.5 ? 505.0 : 0.0, g0.x);
    FtOut[1] = float4(reach, rr, tau, hold);
    FtOut[2] = bb;
    FtOut[OD] = float4((float)nNow, (float)nC, (float)nKeep, (float)nOver);
    FtOut[OD + 1] = float4((float)nNoHip, (float)nSideKeep, (float)nSideFlip, ysCnt > 0.0 ? ysSum / ysCnt : -1.0);
  }
}
