// generado por tools/asm2hlsl.py (traduccion mecanica, identica al juego)
cbuffer cb0 : register(b0) { float4 cb0[3]; }
cbuffer cb2 : register(b2) { float4 cb2[30]; }
cbuffer cb3 : register(b3) { float4 cb3[9]; }
cbuffer cb5 : register(b5) { float4 cb5[17]; }
SamplerState s2 : register(s2);
SamplerState s8 : register(s8);
SamplerState s10 : register(s10);
SamplerState s12 : register(s12);
SamplerComparisonState s15 : register(s15);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t8 : register(t8);
Texture2D<float4> t12 : register(t12);
Texture2D<float4> t14 : register(t14);
Texture2D<float4> t15 : register(t15);
static const float4 icb[4] = { float4(1.000000, 0, 0, 0), float4(0, 1.000000, 0, 0), float4(0, 0, 1.000000, 0), float4(0, 0, 0, 1.000000) };
uint4 PESBfi(uint4 w, uint4 o, uint4 ins, uint4 b) { uint4 m = (((1u << w) - 1u) << o); return ((ins << o) & m) | (b & ~m); }
Texture1D<float4> IniParams : register(t120);

void main(
  float4 i_pos : SV_Position0,
  float4 i_texcoord0 : TEXCOORD0,
  float4 i_texcoord1 : TEXCOORD1,
  float4 i_texcoord2 : TEXCOORD2,
  float4 i_texcoord3 : TEXCOORD3,
  float4 i_texcoord4 : TEXCOORD4,
  float4 i_texcoord5 : TEXCOORD5,
  float4 i_texcoord6 : TEXCOORD6,
  float4 i_texcoord7 : TEXCOORD7,
  bool i_ff : SV_IsFrontFace,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11;
  float4 v0 = 0;
  v0.xyzw = i_pos;
  float4 v1 = 0;
  v1.xyzw = i_texcoord0;
  float4 v2 = 0;
  v2.xyzw = i_texcoord1;
  float4 v3 = 0;
  v3.xyzw = i_texcoord2;
  float4 v4 = 0;
  v4.xyzw = i_texcoord3;
  float4 v5 = 0;
  v5.xyzw = i_texcoord4;
  float4 v6 = 0;
  v6.xyzw = i_texcoord5;
  float4 v7 = 0;
  v7.xyzw = i_texcoord6;
  float4 v8 = 0;
  v8.xyzw = i_texcoord7;
  float4 v9 = 0;
  v9.x = i_ff ? asfloat(0xffffffffU) : 0.0;
  r0.xy = ((v0.xyxx + float4(-0.500000, -0.500000, 0.000000, 0.000000))).xy;
  r1.xyzw = ((v2.xyzw / v3.xxyy)).xyzw;
  r1.xyzw = ((r1.xyzw * float4(0.500000, 0.500000, 0.500000, 0.500000) + float4(0.500000, 0.500000, 0.500000, 0.500000))).xyzw;
  r1.xyzw = ((r1.zwxy * float4(1.000000, -1.000000, 1.000000, -1.000000) + float4(0.000000, 1.000000, 0.000000, 1.000000))).xyzw;
  r0.zw = ((-(r1.xxxy) + r1.zzzw)).zw;
  o1.xy = ((r0.zwzz * cb0[2].xyxx)).xy;
  r1.xyzw = ((t0.Sample(s12, (v1.xyxx).xy)).xyzw).xyzw;
  r1.xyz = ((r1.xyzx * v6.wwww)).xyz;
  r0.z = ((r1.wwww + float4(-0.100000, -0.100000, -0.100000, -0.100000))).z;
  r0.z = (((r0.zzzz < float4(0.000000, 0.000000, 0.000000, 0.000000)) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).z;
  if (asuint(r0.zzzz).x != 0) discard;
  r2.xyz = ((-(v4.xyzx) + cb5[11].xyzx)).xyz;
  r0.z = ((dot((r2.xyzx).xyz, (r2.xyzx).xyz)).xxxx).z;
  r0.z = (sqrt(r0.zzzz)).z;
  r0.z = (max(r0.zzzz, float4(0.500000, 0.500000, 0.500000, 0.500000))).z;
  r0.z = ((r0.zzzz + cb5[12].wwww)).z;
  r0.z = ((r0.zzzz * r0.zzzz)).z;
  r0.w = ((float4(1.000000, 1.000000, 1.000000, 1.000000) / r0.zzzz)).w;
  r0.z = ((-(cb5[11].wwww) * r0.zzzz + r0.wwww)).z;
  r0.z = (max(r0.zzzz, float4(0.000000, 0.000000, 0.000000, 0.000000))).z;
  r2.xyz = ((r0.zzzz * cb5[12].xyzx)).xyz;
  r3.xyz = ((t1.Sample(s10, (v1.xyxx).xy)).xywz).xyz;
  r3.x = ((r3.zzzz * r3.xxxx)).x;
  r0.zw = ((r3.xxxy * float4(0.000000, 0.000000, 2.000000, 2.000000) + float4(0.000000, 0.000000, -1.000000, -1.000000))).zw;
  r2.w = ((-(r0.zzzz) * r0.zzzz + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r2.w = ((-(r0.wwww) * r0.wwww + r2.wwww)).w;
  r2.w = (max(r2.wwww, float4(0.000000, 0.000000, 0.000000, 0.000000))).w;
  r2.w = ((r2.wwww + float4(9.99999975e-05, 9.99999975e-05, 9.99999975e-05, 9.99999975e-05))).w;
  r3.x = (rsqrt(r2.wwww)).x;
  r2.w = ((r2.wwww * r3.xxxx)).w;
  r3.x = ((dot((v6.xyzx).xyz, (v6.xyzx).xyz)).xxxx).x;
  r3.x = (rsqrt(r3.xxxx)).x;
  r3.xyz = ((r3.xxxx * v6.xyzx)).xyz;
  r3.w = ((dot((v7.xyzx).xyz, (v7.xyzx).xyz)).xxxx).w;
  r3.w = (rsqrt(r3.wwww)).w;
  r4.xyz = ((r3.wwww * v7.xyzx)).xyz;
  r4.xyz = ((r0.wwww * r4.xyzx)).xyz;
  r3.xyz = ((r3.xyzx * r0.zzzz + r4.xyzx)).xyz;
  r0.z = ((dot((v8.xyzx).xyz, (v8.xyzx).xyz)).xxxx).z;
  r0.z = (rsqrt(r0.zzzz)).z;
  r4.xyz = ((r0.zzzz * v8.xyzx)).xyz;
  r3.xyz = ((r4.xyzx * r2.wwww + r3.xyzx)).xyz;
  r0.z = ((dot((r3.xyzx).xyz, (r3.xyzx).xyz)).xxxx).z;
  r0.z = (rsqrt(r0.zzzz)).z;
  r3.xyz = ((r0.zzzz * r3.xyzx)).xyz;
  r4.xyz = ((-(v4.xyzx) + cb5[9].xyzx)).xyz;
  r0.z = ((dot((r4.xyzx).xyz, (r4.xyzx).xyz)).xxxx).z;
  r0.z = (sqrt(r0.zzzz)).z;
  r0.z = (max(r0.zzzz, float4(0.500000, 0.500000, 0.500000, 0.500000))).z;
  r0.z = ((r0.zzzz + cb5[10].wwww)).z;
  r0.z = ((r0.zzzz * r0.zzzz)).z;
  r0.w = ((float4(1.000000, 1.000000, 1.000000, 1.000000) / r0.zzzz)).w;
  r0.z = ((-(cb5[9].wwww) * r0.zzzz + r0.wwww)).z;
  r0.z = (max(r0.zzzz, float4(0.000000, 0.000000, 0.000000, 0.000000))).z;
  r4.xyz = ((r0.zzzz * cb5[10].xyzx)).xyz;
  r5.xyz = ((-(v4.xyzx) + cb3[3].xyzx)).xyz;
  r0.z = ((dot((r5.xyzx).xyz, (r5.xyzx).xyz)).xxxx).z;
  r0.z = (sqrt(r0.zzzz)).z;
  r0.z = (max(r0.zzzz, float4(0.500000, 0.500000, 0.500000, 0.500000))).z;
  r0.z = ((r0.zzzz + cb3[4].wwww)).z;
  r0.z = ((r0.zzzz * r0.zzzz)).z;
  r0.w = ((float4(1.000000, 1.000000, 1.000000, 1.000000) / r0.zzzz)).w;
  r0.z = ((-(cb3[3].wwww) * r0.zzzz + r0.wwww)).z;
  r0.z = (max(r0.zzzz, float4(0.000000, 0.000000, 0.000000, 0.000000))).z;
  r5.xyz = ((r0.zzzz * cb3[4].xyzx)).xyz;
  r5.xyz = ((r5.xyzx * float4(0.318309873, 0.318309873, 0.318309873, 0.000000))).xyz;
  r6.xyz = ((float4(0.500000, 0.000000, 0.700000, 0.000000) / cb2[29].xxxx)).xyz;
  r0.z = ((cb2[29].xxxx * float4(3.000000, 3.000000, 3.000000, 3.000000))).z;
  r6.w = ((float4(0.500000, 0.500000, 0.500000, 0.500000) / r0.zzzz)).w;
  r0.z = ((float4(1.000000, 1.000000, 1.000000, 1.000000) / cb2[29].xxxx)).z;
  r0.z = ((-(r0.zzzz) * float4(4.000000, 4.000000, 4.000000, 4.000000) + float4(1.000000, 1.000000, 1.000000, 1.000000))).z;
  r0.z = ((r0.zzzz * r0.zzzz)).z;
  r7.x = ((v5.xxxx * cb2[19].xxxx + cb2[16].xxxx)).x;
  r7.y = ((v5.yyyy * cb2[19].xxxx + cb2[17].xxxx)).y;
  r0.w = ((v5.zzzz * cb2[19].xxxx + cb2[18].xxxx)).w;
  r8.x = ((v5.xxxx * cb2[19].yyyy + cb2[16].yyyy)).x;
  r8.y = ((v5.yyyy * cb2[19].yyyy + cb2[17].yyyy)).y;
  r2.w = ((v5.zzzz * cb2[19].yyyy + cb2[18].yyyy)).w;
  r3.w = ((v5.zzzz + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r9.z = ((r3.wwww * cb2[16].wwww)).z;
  r0.w = ((r0.wwww + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r7.z = ((r0.wwww * cb2[16].wwww)).z;
  r0.w = ((r2.wwww + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r8.z = ((r0.wwww * cb2[16].wwww)).z;
  r10.xy = ((v5.xyxx * v5.xyxx)).xy;
  r10.xy = (((r0.zzzz >= r10.xyxx) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).xy;
  r0.w = (((r9.zzzz >= float4(0.000000, 0.000000, 0.000000, 0.000000)) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).w;
  r10.zw = ((r7.xxxy * r7.xxxy)).zw;
  r10.zw = (((r0.zzzz >= r10.zzzw) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).zw;
  r10.xyzw = (asfloat((asuint(r10.xyzw) & uint4(0x3f800000U, 0x3f800000U, 0x3f800000U, 0x3f800000U)))).xyzw;
  r2.w = (((r7.zzzz >= float4(0.000000, 0.000000, 0.000000, 0.000000)) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).w;
  r2.w = (asfloat((asuint(r2.wwww) & uint4(0x3f800000U, 0x3f800000U, 0x3f800000U, 0x3f800000U)))).w;
  r11.xy = ((r8.xyxx * r8.xyxx)).xy;
  r11.xy = (((r0.zzzz >= r11.xyxx) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).xy;
  r11.xy = (asfloat((asuint(r11.xyxx) & uint4(0x3f800000U, 0x3f800000U, 0U, 0U)))).xy;
  r0.z = (((r8.zzzz >= float4(0.000000, 0.000000, 0.000000, 0.000000)) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).z;
  r0.zw = (asfloat((asuint(r0.zzzw) & uint4(0U, 0U, 0x3f800000U, 0x3f800000U)))).zw;
  r3.w = ((r10.yyyy * r10.xxxx)).w;
  r4.w = ((r0.wwww * r3.wwww)).w;
  r5.w = ((r10.wwww * r10.zzzz)).w;
  r2.w = ((r2.wwww * r5.wwww)).w;
  r5.w = ((r11.yyyy * r11.xxxx)).w;
  r0.z = ((r0.zzzz * r5.wwww)).z;
  r5.w = ((-(r3.wwww) * r0.wwww + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r10.x = ((r2.wwww * r5.wwww)).x;
  r2.w = ((-(r2.wwww) * r5.wwww + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r2.w = ((r2.wwww * r5.wwww)).w;
  r5.w = ((r0.zzzz * r2.wwww)).w;
  r9.xy = (v5.xyxx).xy;
  r9.w = (float4(0, 0, 0, 0)).w;
  r7.w = (float4(1.000000, 1.000000, 1.000000, 1.000000)).w;
  r7.xyzw = ((r7.xyzw * r10.xxxx)).xyzw;
  r7.xyzw = ((r9.xyzw * r4.wwww + r7.xyzw)).xyzw;
  r8.w = (float4(2.000000, 2.000000, 2.000000, 2.000000)).w;
  r7.xyzw = ((r8.wxyz * r5.wwww + r7.wxyz)).xyzw;
  r4.w = (max(r7.wwww, float4(9.99999997e-07, 9.99999997e-07, 9.99999997e-07, 9.99999997e-07))).w;
  r0.w = ((r3.wwww * r0.wwww + r10.xxxx)).w;
  r0.z = ((r0.zzzz * r2.wwww + r0.wwww)).z;
  r0.z = (((r0.zzzz == float4(0.000000, 0.000000, 0.000000, 0.000000)) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).z;
  if (asuint(r0.zzzz).x != 0) {
    r7.x = (float4(-1.000000, -1.000000, -1.000000, -1.000000)).x;
  }
  r8.xy = ((r7.yzyy * float4(0.500000, 0.500000, 0.000000, 0.000000) + float4(0.500000, 0.500000, 0.000000, 0.000000))).xy;
  r8.z = ((-(r8.yyyy) + float4(1.000000, 1.000000, 1.000000, 1.000000))).z;
  r0.zw = ((r8.xxxz * float4(0.000000, 0.000000, 1.000000, 0.333333343))).zw;
  r7.yz = ((-(r6.xxwx) * float4(0.000000, 2.000000, 2.000000, 0.000000) + float4(0.000000, 1.000000, 1.000000, 0.000000))).yz;
  r8.xy = (min(r0.zwzz, r7.yzyy)).xy;
  r8.z = ((r7.xxxx * float4(0.333333343, 0.333333343, 0.333333343, 0.333333343) + r8.yyyy)).z;
  r0.zw = ((r6.xxxw + r8.xxxz)).zw;
  r8.xyzw = ((float4(0.606217802, -0.350000, -0.606217802, -0.350000) / cb2[29].xxxx)).xyzw;
  r2.w = ((t8.SampleCmp(s15, (r0.zwzz).xy, (r4.wwww).x)).xxxx).w;
  r3.w = (r2.wwww).w;
  r5.w = (float4(0, 0, 0, 0)).w;
  [loop] while (true) {
    r6.x = (((asint(r5.wwww) >= int4(3, 3, 3, 3)) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).x;
    if (asuint(r6.xxxx).x != 0) break;
    r6.xw = ((r8.xxxy * icb[asint(r5.w) + 0].yyyy)).xw;
    r6.xw = ((icb[asint(r5.w) + 0].xxxx * r6.yyyz + r6.xxxw)).xw;
    r6.xw = ((icb[asint(r5.w) + 0].zzzz * r8.zzzw + r6.xxxw)).xw;
    r6.xw = ((r0.zzzw + r6.xxxw)).xw;
    r6.x = ((t8.SampleCmp(s15, (r6.xwxx).xy, (r4.wwww).x)).xxxx).x;
    r3.w = ((r3.wwww + r6.xxxx)).w;
    r5.w = (asfloat((asint(r5.wwww) + int4(1, 1, 1, 1)))).w;
  }
  r0.z = ((-(r3.wwww) * float4(0.250000, 0.250000, 0.250000, 0.250000) + float4(1.000000, 1.000000, 1.000000, 1.000000))).z;
  r0.w = ((r0.zzzz * r0.zzzz)).w;
  r2.w = (saturate((v3.zzzz * cb3[6].xxxx + cb3[6].yyyy))).w;
  r0.z = ((-(r0.zzzz) * r0.zzzz + float4(1.000000, 1.000000, 1.000000, 1.000000))).z;
  r0.z = ((r0.zzzz * r2.wwww + r0.wwww)).z;
  r0.z = ((r0.zzzz + -(cb3[6].wwww))).z;
  r0.z = (max(r0.zzzz, float4(0.000000, 0.000000, 0.000000, 0.000000))).z;
  r0.w = (((r7.xxxx == float4(-1.000000, -1.000000, -1.000000, -1.000000)) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).w;
  r0.z = (((asuint(r0.wwww) != 0) ? float4(1.000000, 1.000000, 1.000000, 1.000000) : r0.zzzz)).z;
  r6.xyz = (v4.xyzx).xyz;
  r6.w = (float4(1.000000, 1.000000, 1.000000, 1.000000)).w;
  r7.x = ((dot((r6.xyzw).xyzw, (cb5[13].xyzw).xyzw)).xxxx).x;
  r7.y = ((dot((r6.xyzw).xyzw, (cb5[14].xyzw).xyzw)).xxxx).y;
  r7.z = ((dot((r6.xyzw).xyzw, (cb5[15].xyzw).xyzw)).xxxx).z;
  r0.w = ((dot((r6.xyzw).xyzw, (cb5[16].xyzw).xyzw)).xxxx).w;
  r6.xyz = ((r7.xyzx / r0.wwww)).xyz;
  r6.xy = ((r6.xyxx * float4(0.500000, 0.500000, 0.000000, 0.000000))).xy;
  r0.w = (saturate((-(r6.zzzz) + float4(1.000000, 1.000000, 1.000000, 1.000000)))).w;
  r0.w = ((r0.wwww * float4(28.8539009, 28.8539009, 28.8539009, 28.8539009))).w;
  r0.w = (exp2(r0.wwww)).w;
  r6.xy = ((r6.xyxx * float4(1.000000, -1.000000, 0.000000, 0.000000) + float4(0.500000, 0.500000, 0.000000, 0.000000))).xy;
  r6.xy = ((t14.Sample(s10, (r6.xyxx).xy)).xyzw).xy;
  r2.w = (((r6.xxxx < r0.wwww) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).w;
  if (asuint(r2.wwww).x != 0) {
    r2.w = ((-(r6.xxxx) * r6.xxxx + r6.yyyy)).w;
    r0.w = ((r0.wwww + -(r6.xxxx))).w;
    r0.w = ((r0.wwww * r0.wwww + r2.wwww)).w;
    r0.w = ((r2.wwww / r0.wwww)).w;
    r0.w = ((r0.wwww + float4(-0.601000, -0.601000, -0.601000, -0.601000))).w;
    r0.w = (saturate((r0.wwww * float4(2.500000, 2.500000, 2.500000, 2.500000)))).w;
  } else {
    r0.w = (float4(1.000000, 1.000000, 1.000000, 1.000000)).w;
  }
  r0.z = (min(r0.wwww, r0.zzzz)).z;
  r0.w = ((v3.zzzz + -(cb3[8].xxxx))).w;
  r0.w = (saturate((r0.wwww * cb3[8].yyyy))).w;
  r0.w = ((-(r0.wwww) + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r0.w = ((r0.wwww * r0.wwww)).w;
  r6.xyz = ((r0.wwww * cb3[1].xyzx)).xyz;
  r0.w = ((dot((-(cb3[0].xyzx)).xyz, (r3.xyzx).xyz)).xxxx).w;
  r0.w = ((r0.wwww + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r0.w = ((r0.wwww * float4(0.500000, 0.500000, 0.500000, 0.500000))).w;
  r3.xzw = ((r6.xxyz * r0.wwww)).xzw;
  r3.xzw = ((r3.xxzw * r0.zzzz + r5.xxyz)).xzw;
  r3.xzw = ((r4.xxyz * float4(0.318309873, 0.000000, 0.318309873, 0.318309873) + r3.xxzw)).xzw;
  r2.xyz = ((r2.xyzx * float4(0.318309873, 0.318309873, 0.318309873, 0.000000) + r3.xzwx)).xyz;
  r3.xyz = ((cb3[2].xyzx * r3.yyyy + cb3[7].xyzx)).xyz;
  r4.xyz = ((t2.Sample(s2, (v1.zwzz).xy)).xyzw).xyz;
  r0.z = ((dot((float4(0.298999995, 0.587000012, 0.114, 0.000000)).xyz, (r3.xyzx).xyz)).xxxx).z;
  r0.z = (saturate((-(r0.zzzz) * float4(0.001000, 0.001000, 0.001000, 0.001000) + float4(1.000000, 1.000000, 1.000000, 1.000000)))).z;
  r0.z = ((r0.zzzz * float4(2.000000, 2.000000, 2.000000, 2.000000) + float4(1.000000, 1.000000, 1.000000, 1.000000))).z;
  r1.xyz = ((r1.xyzx * r1.xyzx)).xyz;
  r1.xyz = ((r4.xyzx * r1.xyzx)).xyz;
  r2.xyz = ((r2.xyzx * r0.zzzz + r3.xyzx)).xyz;
  r1.xyz = ((r1.xyzx * r2.xyzx)).xyz;
  r0.z = ((r1.wwww * v4.wwww)).z;
  r1.xyz = ((r1.xyzx * cb2[23].zzzz)).xyz;
  r2.xy = ((r0.xyxx / cb0[1].xyxx)).xy;
  r2.xy = ((r2.xyxx * float4(2.000000, -2.000000, 0.000000, 0.000000) + float4(-1.000000, 1.000000, 0.000000, 0.000000))).xy;
  r0.w = (log2(v3.wwww)).w;
  r0.w = ((r0.wwww * cb2[26].yyyy)).w;
  r0.w = ((r0.wwww * float4(0.693147182, 0.693147182, 0.693147182, 0.693147182) + cb2[26].zzzz)).w;
  r0.w = (max(r0.wwww, float4(0.000000, 0.000000, 0.000000, 0.000000))).w;
  r0.w = (min(r0.wwww, cb2[26].wwww)).w;
  r3.xz = ((float4(0.0146484403, 0.000000, 0.0146484403, 0.000000) * r2.xxxx + float4(0.015625, 0.000000, 0.015625, 0.000000))).xz;
  r3.yw = ((cb2[25].xxxx * r2.yyyy + cb2[25].yyyy)).yw;
  r1.w = ((r0.wwww + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r1.w = (max(r1.wwww, float4(0.000000, 0.000000, 0.000000, 0.000000))).w;
  r1.w = (min(r1.wwww, cb2[26].wwww)).w;
  r2.y = (floor(r0.wwww)).y;
  r2.w = (floor(r1.wwww)).w;
  r2.xz = ((r2.yywy * float4(0.031250, 0.000000, 0.031250, 0.000000))).xz;
  r4.xy = (((r2.xzxx >= -(r2.xzxx)) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).xy;
  r4.zw = (frac(abs(r2.xxxz))).zw;
  r4.xy = (((asuint(r4.xyxx) != 0) ? r4.zwzz : -(r4.zwzz))).xy;
  r4.xz = ((r4.xxyx * cb2[25].zzzz)).xz;
  r4.yw = (floor(r2.xxxz)).yw;
  r5.xz = (float4(32.000000, 0, 32.000000, 0)).xz;
  r5.yw = (cb2[25].wwww).yw;
  r3.xyzw = ((r5.xyzw * r4.xyzw + r3.xyzw)).xyzw;
  r0.w = ((r0.wwww + -(r2.yyyy))).w;
  r2.xyzw = ((t12.Sample(s10, (r3.xyxx).xy)).xyzw).xyzw;
  r3.xyzw = ((t12.Sample(s10, (r3.zwzz).xy)).xyzw).xyzw;
  r1.w = ((-(r0.wwww) + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r3.xyzw = ((r0.wwww * r3.xyzw)).xyzw;
  r2.xyzw = ((r1.wwww * r2.xyzw + r3.xyzw)).xyzw;
  r0.xy = ((r0.xyxx * float4(0.125000, 0.125000, 0.000000, 0.000000))).xy;
  r0.xy = (frac(r0.xyxx)).xy;
  r0.x = ((t15.Sample(s8, (r0.xyxx).xy)).wxyz).x;
  r2.xyzw = ((r0.xxxx * float4(0.0078125, 0.0078125, 0.0078125, 0.0078125) + r2.xyzw)).xyzw;
  r0.xyw = ((r1.xyxz * r2.wwww + r2.xyxz)).xyw;
  o0.xyz = ((r0.zzzz * r0.xywx)).xyz;
  o0.w = (r0.zzzz).w;
  o1.zw = (float4(0, 0, 0, 1.000000)).zw;
  return;
}
