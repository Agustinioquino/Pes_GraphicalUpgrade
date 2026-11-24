// PES21 FX - inspector visual: pixel shader plano (silueta en R8 y "resaltar en magenta" en el G-buffer)
void main(float4 pos : SV_Position, out float4 o0 : SV_Target0, out float4 o1 : SV_Target1, out float4 o2 : SV_Target2, out float4 o3 : SV_Target3, out float4 o4 : SV_Target4)
{ o0 = float4(1, 0, 1, 1); o1 = float4(0.5, 0.5, 0, 0); o2 = float4(0.5, 0.2, 1, 0); o3 = 0; o4 = float4(0, 0, 0, 1); }
