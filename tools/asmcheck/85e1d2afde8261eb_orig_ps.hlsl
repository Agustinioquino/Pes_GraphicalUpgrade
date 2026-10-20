// generado por tools/asm2hlsl.py (traduccion mecanica, identica al juego; registros con valores)
cbuffer cb0 : register(b0) { float4 cb0[3]; }
SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
SamplerState s8 : register(s8);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t15 : register(t15);
uint4 PESBfi(uint4 w, uint4 o, uint4 ins, uint4 b) { uint4 m = (w >= 32u) ? 0xffffffffu : (((1u << w) - 1u) << o); return ((ins << o) & m) | (b & ~m); }
Texture1D<float4> IniParams : register(t120);

void main(
  float4 i_pos : SV_Position0,
  float4 i_color0 : COLOR0,
  float4 i_texcoord0 : TEXCOORD0,
  float3 i_texcoord5 : TEXCOORD5,
  float3 i_texcoord6 : TEXCOORD6,
  float3 i_texcoord7 : TEXCOORD7,
  float4 i_texcoord8 : TEXCOORD8,
  float2 i_texcoord9 : TEXCOORD9,
  bool i_ff : SV_IsFrontFace,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3,
  out float4 o4 : SV_Target4)
{
  float4 r0, r1, r2, r3, r4;
  float4 v0 = 0;
  v0.xyzw = i_pos;
  float4 v1 = 0;
  v1.xyzw = i_color0;
  float4 v2 = 0;
  v2.xyzw = i_texcoord0;
  float4 v3 = 0;
  v3.xyz = i_texcoord5;
  float4 v4 = 0;
  v4.xyz = i_texcoord6;
  float4 v5 = 0;
  v5.xyz = i_texcoord7;
  float4 v6 = 0;
  v6.xyzw = i_texcoord8;
  float4 v7 = 0;
  v7.xy = i_texcoord9;
  float4 v8 = 0;
  v8.x = i_ff ? -1.0 : 0.0;
  r0.y = (v1.wwww).y;
  r1.xyzw = ((v0.xyxy + float4(-0.500000, -0.500000, -1.000000, -1.000000))).xyzw;
  r0.zw = ((r1.xxxy * float4(0.000000, 0.000000, 0.125000, 0.125000))).zw;
  r1.xy = (float4(int4(r1.zwzz))).xy;
  r1.xy = (float4((float)((int)r1.x & 1), (float)((int)r1.y & 1), 0.0, 0.0)).xy;
  r1.xy = (float4(int4(r1.xyxx))).xy;
  r1.xy = ((r1.xyxx + float4(-0.500000, -0.500000, 0.000000, 0.000000))).xy;
  r2.yz = ((t15.Sample(s8, (r0.zwzz).xy)).yxwz).yz;
  r2.x = ((r2.zzzz * cb0[0].wwww)).x;
  r3.xyzw = ((t0.Sample(s0, (v2.xyxx).xy)).xyzw).xyzw;
  r0.x = (r3.wwww).x;
  o0.xyz = (r3.xyzx).xyz;
  r0.xy = ((r0.xyxx + -(r2.xyxx))).xy;
  r0.xy = (((r0.xyxx < float4(0.000000, 0.000000, 0.000000, 0.000000)) ? float4(-1.0, -1.0, -1.0, -1.0) : float4(0.0, 0.0, 0.0, 0.0))).xy;
  r0.x = (float4((float)((int)r0.y | (int)r0.x), 0.0, 0.0, 0.0)).x;
  if ((r0.xxxx).x != 0) discard;
  o0.w = (float4(1.000000, 1.000000, 1.000000, 1.000000)).w;
  r0.x = (((v8.xxxx != 0) ? float4(1.000000, 1.000000, 1.000000, 1.000000) : float4(-1.000000, -1.000000, -1.000000, -1.000000))).x;
  r0.xyz = ((r0.xxxx * v5.xyzx)).xyz;
  r0.w = ((dot((r0.xyzx).xyz, (r0.xyzx).xyz)).xxxx).w;
  r0.w = (rsqrt(r0.wwww)).w;
  r0.xyz = ((r0.wwww * r0.xyzx)).xyz;
  r0.w = ((dot((v3.xyzx).xyz, (v3.xyzx).xyz)).xxxx).w;
  r0.w = (rsqrt(r0.wwww)).w;
  r2.xyz = ((r0.wwww * v3.xyzx)).xyz;
  r0.w = ((dot((v4.xyzx).xyz, (v4.xyzx).xyz)).xxxx).w;
  r0.w = (rsqrt(r0.wwww)).w;
  r3.xyz = ((r0.wwww * v4.xyzx)).xyz;
  r4.xyz = ((t1.Sample(s1, (v2.zwzz).xy)).xywz).xyz;
  r4.x = ((r4.zzzz * r4.xxxx)).x;
  r1.zw = ((r4.xxxy * float4(0.000000, 0.000000, 2.000000, 2.000000) + float4(0.000000, 0.000000, -1.000000, -1.000000))).zw;
  r3.xyz = ((r1.wwww * r3.xyzx)).xyz;
  r2.xyz = ((r2.xyzx * r1.zzzz + r3.xyzx)).xyz;
  r0.w = ((-(r1.zzzz) * r1.zzzz + float4(1.000000, 1.000000, 1.000000, 1.000000))).w;
  r0.w = ((-(r1.wwww) * r1.wwww + r0.wwww)).w;
  r0.w = (max(r0.wwww, float4(0.000000, 0.000000, 0.000000, 0.000000))).w;
  r0.w = ((r0.wwww + float4(9.99999975e-05, 9.99999975e-05, 9.99999975e-05, 9.99999975e-05))).w;
  r1.z = (rsqrt(r0.wwww)).z;
  r0.w = ((r0.wwww * r1.zzzz)).w;
  r0.xyz = ((r0.xyzx * r0.wwww + r2.xyzx)).xyz;
  r0.w = ((dot((r0.xyzx).xyz, (r0.xyzx).xyz)).xxxx).w;
  r0.w = (rsqrt(r0.wwww)).w;
  r0.xyz = ((r0.wwww * r0.xyzx)).xyz;
  r0.w = ((abs(r0.yyyy) + abs(r0.xxxx))).w;
  r0.w = ((abs(r0.zzzz) + r0.wwww)).w;
  r1.zw = ((r0.xxxy / r0.wwww)).zw;
  r2.xy = (((r1.zwzz >= float4(0.000000, 0.000000, 0.000000, 0.000000)) ? float4(-1.0, -1.0, -1.0, -1.0) : float4(0.0, 0.0, 0.0, 0.0))).xy;
  r2.xy = (((r2.xyxx != 0) ? float4(1.000000, 1.000000, 0.0, 0.0) : float4(-1.000000, -1.000000, 0.0, 0.0))).xy;
  r2.zw = ((-(abs(r1.wwwz)) + float4(0.000000, 0.000000, 1.000000, 1.000000))).zw;
  r2.xy = ((r2.xyxx * r2.zwzz)).xy;
  r0.w = (((float4(0.000000, 0.000000, 0.000000, 0.000000) < -(r0.zzzz)) ? float4(-1.0, -1.0, -1.0, -1.0) : float4(0.0, 0.0, 0.0, 0.0))).w;
  r1.zw = (((r0.wwww != 0) ? r1.zzzw : r2.xxxy)).zw;
  r1.zw = ((r1.zzzw * float4(0.000000, 0.000000, 0.500000, 0.500000) + float4(0.000000, 0.000000, 0.500000, 0.500000))).zw;
  r1.zw = ((r1.zzzw * float4(0.000000, 0.000000, 2047.000000, 2047.000000))).zw;
  r1.zw = (float4(uint4(r1.zzzw))).zw;
  r0.w = (float4(PESBfi(uint4(1U, 1U, 1U, 1U), uint4(1U, 1U, 1U, 1U), uint4(int4(r1.zzzz)), uint4(0U, 0U, 0U, 0U)))).w;
  r0.w = (float4(PESBfi(uint4(1U, 1U, 1U, 1U), uint4(0U, 0U, 0U, 0U), uint4(int4(r1.wwww)), uint4(int4(r0.wwww))))).w;
  r1.zw = (float4((uint4(int4(r1.zzzw)) >> (uint4(0U, 0U, 1U, 1U) & 31)))).zw;
  r2.xy = (float4(uint4(int4(r1.zwzz)))).xy;
  r2.w = (float4(uint4(int4(r0.wwww)))).w;
  o1.xyw = ((r2.xyxw * float4(0.000977517106, 0.000977517106, 0.000000, 0.333333343))).xyw;
  o1.z = (float4(0.000977517106, 0.000977517106, 0.000977517106, 0.000977517106)).z;
  r2.xyz = (ddx_fine(r0.xyzx)).xyz;
  r1.xzw = ((-(r2.xxyz) * r1.xxxx + r0.xxyz)).xzw;
  r2.xyz = (ddy_fine(r1.xzwx)).xyz;
  r1.xyz = ((-(r2.xyzx) * r1.yyyy + r1.xzwx)).xyz;
  r0.w = ((dot((r0.xyzx).xyz, (r1.xyzx).xyz)).xxxx).w;
  r1.xyz = ((r1.xyzx / r0.wwww)).xyz;
  r0.w = ((dot((r0.xyzx).xyz, (v3.xyzx).xyz)).xxxx).w;
  r2.xyz = ((-(r0.xyzx) * r0.wwww + v3.xyzx)).xyz;
  r0.w = ((dot((r2.xyzx).xyz, (r2.xyzx).xyz)).xxxx).w;
  r0.w = (rsqrt(r0.wwww)).w;
  r2.xyz = ((r0.wwww * r2.xyzx)).xyz;
  r3.xyz = ((r0.zxyz * r2.yzxy)).xyz;
  r0.xyz = ((r0.yzxy * r2.zxyz + -(r3.xyzx))).xyz;
  r0.w = ((dot((r1.xyzx).xyz, (r2.xyzx).xyz)).xxxx).w;
  r0.x = ((dot((r1.xyzx).xyz, (r0.xyzx).xyz)).xxxx).x;
  r0.y = (ddy_fine(r0.xxxx)).y;
  r1.xy = (ddx_fine(r0.wxww)).xy;
  r0.x = (ddy_fine(r0.wwww)).x;
  r0.xy = ((abs(r0.xyxx) + abs(r1.xyxx))).xy;
  r0.xy = ((r0.xyxx * float4(0.500000, 0.500000, 0.000000, 0.000000))).xy;
  r0.xy = (min(r0.xyxx, float4(0.300010, 0.300010, 0.000000, 0.000000))).xy;
  r0.xy = ((r0.xyxx * r0.xyxx)).xy;
  r0.xy = ((r0.xyxx + r0.xyxx)).xy;
  r0.x = (max(r0.yyyy, r0.xxxx)).x;
  r0.yz = ((t2.Sample(s2, (v2.xyxx).xy)).zxyw).yz;
  r0.x = ((r0.zzzz * r0.zzzz + r0.xxxx)).x;
  o2.y = (r0.yyyy).y;
  o2.x = (sqrt(r0.xxxx)).x;
  r0.x = ((t3.Sample(s3, (v2.xyxx).xy)).xyzw).x;
  o2.z = (r0.xxxx).z;
  o2.w = (float4(0.0, 0.0, 0.0, 0.0)).w;
  r0.xyzw = ((v6.xyzw / v7.xxyy)).xyzw;
  r0.xyzw = ((r0.xyzw * float4(0.500000, 0.500000, 0.500000, 0.500000) + float4(0.500000, 0.500000, 0.500000, 0.500000))).xyzw;
  r0.xyzw = ((r0.zwxy * float4(1.000000, -1.000000, 1.000000, -1.000000) + float4(0.000000, 1.000000, 0.000000, 1.000000))).xyzw;
  r0.xy = ((-(r0.xyxx) + r0.zwzz)).xy;
  o3.xy = ((r0.xyxx * cb0[2].xyxx)).xy;
  o3.zw = (float4(0.0, 0.0, 0.0, 0.0)).zw;
  o4.xyzw = (float4(0.0, 0.0, 0.0, 1.000000)).xyzw;
  return;
}
