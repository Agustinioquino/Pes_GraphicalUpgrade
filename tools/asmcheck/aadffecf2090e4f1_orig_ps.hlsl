// generado por tools/asm2hlsl.py (traduccion mecanica, identica al juego)
cbuffer cb0 : register(b0) { float4 cb0[3]; }
cbuffer cb4 : register(b4) { float4 cb4[2]; }
SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
SamplerState s4 : register(s4);
SamplerState s8 : register(s8);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t15 : register(t15);
uint4 PESBfi(uint4 w, uint4 o, uint4 ins, uint4 b) { uint4 m = (((1u << w) - 1u) << o); return ((ins << o) & m) | (b & ~m); }
Texture1D<float4> IniParams : register(t120);

void main(
  float4 i_pos : SV_Position0,
  float4 i_color0 : COLOR0,
  float2 i_texcoord0 : TEXCOORD0,
  float2 i_texcoord9 : TEXCOORD9,
  float3 i_texcoord5 : TEXCOORD5,
  float3 i_texcoord6 : TEXCOORD6,
  float3 i_texcoord7 : TEXCOORD7,
  float4 i_texcoord8 : TEXCOORD8,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3,
  out float4 o4 : SV_Target4)
{
  float4 r0, r1, r2;
  float4 v0 = 0;
  v0.xyzw = i_pos;
  float4 v1 = 0;
  v1.xyzw = i_color0;
  float4 v2 = 0;
  v2.xy = i_texcoord0;
  v2.zw = i_texcoord9;
  float4 v3 = 0;
  v3.xyz = i_texcoord5;
  float4 v4 = 0;
  v4.xyz = i_texcoord6;
  float4 v5 = 0;
  v5.xyz = i_texcoord7;
  float4 v6 = 0;
  v6.xyzw = i_texcoord8;
  r0.xy = ((v0.xyxx + float4(-0.500000, -0.500000, 0.000000, 0.000000))).xy;
  r0.xy = ((r0.xyxx * float4(0.125000, 0.125000, 0.000000, 0.000000))).xy;
  r0.xy = ((t15.Sample(s8, (r0.xyxx).xy)).xwyz).xy;
  r1.y = (v1.wwww).y;
  r2.xyzw = ((t0.Sample(s0, (v2.xyxx).xy)).xyzw).xyzw;
  r1.x = (r2.wwww).x;
  r0.xy = ((-(r0.yxyy) * cb0[0].wwww + r1.xyxx)).xy;
  r0.xy = (((r0.xyxx < float4(0.000000, 0.000000, 0.000000, 0.000000)) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).xy;
  r0.x = (asfloat((asuint(r0.yyyy) | asuint(r0.xxxx)))).x;
  if (asuint(r0.xxxx).x != 0) discard;
  r0.xyzw = ((t4.Sample(s4, (v2.xyxx).xy)).xyzw).xyzw;
  r1.x = ((r0.wwww + float4(-0.250000, -0.250000, -0.250000, -0.250000))).x;
  r1.x = ((-(r1.xxxx) + float4(1.000000, 1.000000, 1.000000, 1.000000))).x;
  r1.x = ((r1.xxxx + -(cb4[1].xxxx))).x;
  r1.x = (saturate((r1.xxxx * float4(4.000000, 4.000000, 4.000000, 4.000000)))).x;
  r1.x = ((-(r1.xxxx) + float4(1.000000, 1.000000, 1.000000, 1.000000))).x;
  r1.y = ((r0.wwww * r1.xxxx)).y;
  r0.w = ((-(r1.xxxx) * r0.wwww + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r0.xyz = ((r0.xyzx * r2.xyzx)).xyz;
  r0.xyz = ((r1.yyyy * r0.xyzx)).xyz;
  o0.xyz = ((r2.xyzx * r0.wwww + r0.xyzx)).xyz;
  o0.w = (float4(1.000000, 1.000000, 1.000000, 1.000000)).w;
  r0.x = ((dot((v3.xyzx).xyz, (v3.xyzx).xyz)).xxxx).x;
  r0.x = (rsqrt(r0.xxxx)).x;
  r0.xyz = ((r0.xxxx * v3.xyzx)).xyz;
  r1.x = ((dot((v4.xyzx).xyz, (v4.xyzx).xyz)).xxxx).x;
  r1.x = (rsqrt(r1.xxxx)).x;
  r1.xyz = ((r1.xxxx * v4.xyzx)).xyz;
  r2.xyz = ((t1.Sample(s1, (v2.xyxx).xy)).xywz).xyz;
  r2.x = ((r2.zzzz * r2.xxxx)).x;
  r2.xy = ((r2.xyxx * float4(2.000000, 2.000000, 0.000000, 0.000000) + float4(-1.000000, -1.000000, 0.000000, 0.000000))).xy;
  r1.xyz = ((r1.xyzx * r2.yyyy)).xyz;
  r0.xyz = ((r0.xyzx * r2.xxxx + r1.xyzx)).xyz;
  r1.x = ((dot((v5.xyzx).xyz, (v5.xyzx).xyz)).xxxx).x;
  r1.x = (rsqrt(r1.xxxx)).x;
  r1.xyz = ((r1.xxxx * v5.xyzx)).xyz;
  r1.w = ((-(r2.xxxx) * r2.xxxx + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r1.w = ((-(r2.yyyy) * r2.yyyy + r1.wwww)).w;
  r1.w = (max(r1.wwww, float4(0.000000, 0.000000, 0.000000, 0.000000))).w;
  r1.w = ((r1.wwww + float4(9.99999975e-05, 9.99999975e-05, 9.99999975e-05, 9.99999975e-05))).w;
  r2.x = (rsqrt(r1.wwww)).x;
  r1.w = ((r1.wwww * r2.xxxx)).w;
  r0.xyz = ((r1.xyzx * r1.wwww + r0.xyzx)).xyz;
  r1.x = ((dot((r0.xyzx).xyz, (r0.xyzx).xyz)).xxxx).x;
  r1.x = (rsqrt(r1.xxxx)).x;
  r0.xyz = ((r0.xyzx * r1.xxxx)).xyz;
  r1.x = ((abs(r0.yyyy) + abs(r0.xxxx))).x;
  r1.x = ((abs(r0.zzzz) + r1.xxxx)).x;
  r0.xy = ((r0.xyxx / r1.xxxx)).xy;
  r0.z = (((float4(0.000000, 0.000000, 0.000000, 0.000000) < -(r0.zzzz)) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).z;
  r1.xy = (((r0.xyxx >= float4(0.000000, 0.000000, 0.000000, 0.000000)) ? float4(-1,-1,-1,-1) : float4(0,0,0,0))).xy;
  r1.xy = (((asuint(r1.xyxx) != 0) ? float4(1.000000, 1.000000, 0, 0) : float4(-1.000000, -1.000000, 0, 0))).xy;
  r1.zw = ((-(abs(r0.yyyx)) + float4(0.000000, 0.000000, 1.000000, 1.000000))).zw;
  r1.xy = ((r1.xyxx * r1.zwzz)).xy;
  r0.xy = (((asuint(r0.zzzz) != 0) ? r0.xyxx : r1.xyxx)).xy;
  r0.xy = ((r0.xyxx * float4(0.500000, 0.500000, 0.000000, 0.000000) + float4(0.500000, 0.500000, 0.000000, 0.000000))).xy;
  r0.xy = ((r0.xyxx * float4(2047.000000, 2047.000000, 0.000000, 0.000000))).xy;
  r0.xy = (asfloat(uint4(r0.xyxx))).xy;
  r0.z = (asfloat(PESBfi(uint4(1U, 1U, 1U, 1U), uint4(1U, 1U, 1U, 1U), asuint(r0.xxxx), uint4(0U, 0U, 0U, 0U)))).z;
  r0.z = (asfloat(PESBfi(uint4(1U, 1U, 1U, 1U), uint4(0U, 0U, 0U, 0U), asuint(r0.yyyy), asuint(r0.zzzz)))).z;
  r0.xy = (asfloat((asuint(r0.xyxx) >> (uint4(1U, 1U, 0U, 0U) & 31)))).xy;
  r1.xyw = (float4(asuint(r0.xyxz))).xyw;
  o1.xyw = ((r1.xyxw * float4(0.000977517106, 0.000977517106, 0.000000, 0.333333343))).xyw;
  o1.z = (float4(0.000977517106, 0.000977517106, 0.000977517106, 0.000977517106)).z;
  r0.x = ((t3.Sample(s3, (v2.xyxx).xy)).xyzw).x;
  o2.z = ((r0.wwww * r0.xxxx)).z;
  r0.xy = ((t2.Sample(s2, (v2.xyxx).xy)).xyzw).xy;
  o2.y = ((r0.wwww * r0.xxxx)).y;
  o2.x = (max(r0.yyyy, float4(0.004000, 0.004000, 0.004000, 0.004000))).x;
  o2.w = (float4(0, 0, 0, 0)).w;
  r0.xyzw = ((v6.xyzw / v2.zzww)).xyzw;
  r0.xyzw = ((r0.xyzw * float4(0.500000, 0.500000, 0.500000, 0.500000) + float4(0.500000, 0.500000, 0.500000, 0.500000))).xyzw;
  r0.xyzw = ((r0.zwxy * float4(1.000000, -1.000000, 1.000000, -1.000000) + float4(0.000000, 1.000000, 0.000000, 1.000000))).xyzw;
  r0.xy = ((-(r0.xyxx) + r0.zwzz)).xy;
  o3.xy = ((r0.xyxx * cb0[2].xyxx)).xy;
  o3.zw = (float4(0, 0, 0, 0)).zw;
  o4.xyzw = (float4(0, 0, 0, 1.000000)).xyzw;
  return;
}
