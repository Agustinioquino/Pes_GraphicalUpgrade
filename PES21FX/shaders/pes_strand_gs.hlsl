// PES21 FX (08/11 y, Agustin: "creo que estamos planteando mal la geometria de las hebras") HEBRAS v3: cada hebra sola, desde su raiz
// (pes_brow_roots_*_gs). Una hebra por llamada del GS: hasta 16 segmentos (34 vertices x 27 escalares = 918 de 1024) en vez de 5
// (3 hebras por llamada): la curva, el rizo, el pliegue de la piel y la punta afinada se ven suaves de cerca. De lejos baja sola a 4
// segmentos (un segmento cada ~3 px de largo en pantalla). Misma cuenta que el anillo de pes_brow_gs.hlsli y mismo PS
#define PES_STRAND 1
#include "pes_brow_gs.hlsli"
#define PES_SEG_MAX 16

[maxvertexcount((PES_SEG_MAX + 1) * 2)]
void main(point RootOut v[1], inout TriangleStream<GSOut> outS)
{
  float3 root = v[0].r0.xyz, dd = v[0].r1.xyz, n = v[0].r2.xyz, bt = v[0].r3.xyz, cR = v[0].r4.rgb, cT = v[0].r5.rgb;
  float len = v[0].r1.w, widB = v[0].r2.w, liftS = v[0].r3.w, curl = v[0].r4.w, r3 = v[0].r5.w;
  float2 motion = v[0].r6.xy; float kap = v[0].r6.z, clat = v[0].r6.w;
  float stackH = v[0].r7.x, code = v[0].r7.y, widRef = max(v[0].r7.z, 1e-6), lay = v[0].r7.w;
  if (len <= 0.0 || root.z < 0.05) return;
  float4 P75 = IniParams.Load(int2(75, 0));
  float4 HO = IniParams.Load(int2(404, 0)), HV = IniParams.Load(int2(405, 0));
  float vpH = m_viewportSize.y > 16.0 ? m_viewportSize.y : 1080.0;
  float pxR = 2.0 * abs(root.z) / (max(abs(m_projection[1].y), 1e-4) * vpH);
  // segmentos: uno cada ~3 px de largo en pantalla (4..16); y416 = tope (16 por defecto)
  float segMax = clamp(IniParams.Load(int2(416, 0)).y, 4.0, (float)PES_SEG_MAX);
  uint segs = (uint)clamp(round(len / max(pxR * 3.0, 1e-9)), 4.0, segMax);
  float sk = clamp(HV.x, 0.0, 2.5), cz = clamp(HO.z, 0.0, 3.0), tip = clamp(HO.y, 0.02, 1.0);
  [loop] for (uint ring = 0; ring <= segs; ring++) {
    float t = (float)ring / (float)segs;
    float emerge = -0.0003 + 0.00045 * saturate(t * 4.0);
    float3 p = root + n * emerge + dd * len * t + n * len * liftS * (0.35 * sin(t * 3.14159) + 0.1 * t) + bt * len * curl * cz * 0.5 * sin(t * 6.2831853 + r3 * 6.2831853) * t;
    p += n * clamp(0.5 * kap * (len * t) * (len * t), -0.25 * len, 0.6 * len);
    p += bt * clamp(clat, -0.3 * len, 0.3 * len) * 0.55 * sk * t * t;
    p += n * stackH * smoothstep(0.0, 0.45, t);
    // punta afinada como el pelo real: conserva el grosor hasta un tercio y se afina en curva hasta la punta (con 16 segmentos sin escalon)
    float wid = widB * lerp(1.0, tip, smoothstep(0.3, 1.0, t));
    float pxV = 2.0 * abs(p.z) / (max(abs(m_projection[1].y), 1e-4) * vpH);
    float wReal = wid;
    wid = max(wid, pxV * (P75.z > 0.5 ? 1.25 : lerp(0.45, 0.8, saturate(widB / widRef))));
    float cov = P75.z > 0.5 ? 1.0 : saturate(wReal / max(pxV * 1.5, 1e-9));
    float3 V = normalize(-p);
    float3 tanR = normalize(dd + n * liftS * (0.35 * 3.14159 * cos(t * 3.14159) + 0.1));
    float3 sideV = normalize(cross(tanR, V) + 1e-6);   // (v3) el costado sigue la tangente real (antes la direccion de la raiz)
    float3 nH = normalize(n * lerp(1.0, 0.8, lay) + dd * (liftS * 0.6 * t));
    [unroll] for (int s = -1; s <= 1; s += 2) {
      GSOut o;
      float side = (float)s;
      o.pos = ProjV(p + sideV * wid * side);
      o.nrmV = nH;
      o.col = cR; o.col2 = cT;
      o.tw = float3(t, side, clamp(cov, 0.02, 0.98) + code);
      o.motion = motion;
      o.viewV = V;
      o.tanV = tanR;
      o.posV = p;
      outS.Append(o);
    }
  }
  outS.RestartStrip();
}
