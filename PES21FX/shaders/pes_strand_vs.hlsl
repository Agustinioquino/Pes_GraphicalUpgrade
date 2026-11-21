// PES21 FX (08/11 y) hebras v3: VS de paso para las raices (salida de flujo de pes_brow_roots_*_gs, DrawAuto). Cada vertice = una hebra
struct RootIn {
  float4 r0 : TEXCOORD0; float4 r1 : TEXCOORD1; float4 r2 : TEXCOORD2; float4 r3 : TEXCOORD3;
  float4 r4 : TEXCOORD4; float4 r5 : TEXCOORD5; float4 r6 : TEXCOORD6; float4 r7 : TEXCOORD7;
};
RootIn main(RootIn i) { return i; }
