// PES21 FX - la ropa acompana la musculatura con escala atenuada (24/10 f). La musculatura escala los huesos (los del juego
// no traen escala) y la ropa, al estar mas lejos del eje del hueso que la piel, crecia de mas (mangas infladas). En el VS
// de la camiseta y del short, la matriz de piel ya mezclada A = R S Rb^-1 (escala en el espacio del hueso) pasa a tener la
// escala s^c: A' = A (A^T A)^((c - 1) / 2), con la raiz de la matriz simetrica por Jacobi (3 x 3, 4 barridos).
//   163: x prendido | y c (0.8: la ropa crece el 80 % en potencia; 1 = como la piel) | w = esta malla es ropa (regla del PS)
#ifndef PES_CLOTH_HLSLI
#define PES_CLOTH_HLSLI
float3x3 PesSymPow(float3x3 B, float e) {
  float3x3 V = float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);
  [unroll] for (int sweep = 0; sweep < 4; sweep++) {
    [unroll] for (int k = 0; k < 3; k++) {
      const int p = k == 0 ? 0 : k == 1 ? 0 : 1, q = k == 0 ? 1 : k == 1 ? 2 : 2;
      float apq = B[p][q];
      if (abs(apq) > 1e-7) {
        float theta = (B[q][q] - B[p][p]) / (2.0 * apq);
        float t = sign(theta) / (abs(theta) + sqrt(theta * theta + 1.0)); if (theta == 0.0) t = 1.0;
        float c = rsqrt(t * t + 1.0), s = t * c;
        float3x3 J = float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);
        J[p][p] = c; J[q][q] = c; J[p][q] = s; J[q][p] = -s;
        B = mul(transpose(J), mul(B, J)); V = mul(V, J);
      }
    }
  }
  float3 d = float3(pow(max(B[0][0], 1e-6), e), pow(max(B[1][1], 1e-6), e), pow(max(B[2][2], 1e-6), e));
  float3x3 D = float3x3(d.x, 0, 0, 0, d.y, 0, 0, 0, d.z);
  return mul(V, mul(D, transpose(V)));
}
// r1, r2, r3 = filas de salida (x, y, z) de la matriz de piel mezclada (xyz = 3x3, w = traslacion); v / n = posicion y normal
// del vertice en reposo.
// (24/10 g) la musculatura escala cada hueso alrededor de un ancla sobre el eje del miembro (musculo.cpp), no del origen del
// modelo: cambiar solo la parte lineal (L -> L') corria la ropa (L - L') x ancla, varios cm con el biceps en 1.3 (la manga se
// despegaba del brazo). La ropa esta a unos r metros del eje, hacia adentro de su normal, y lo que se mueve en el eje del
// miembro no escala: p' = p - (L - L') (r n), o sea traslacion t' = t + (L - L') (v - r n). z163 = r (0 = 0.07 m)
void PesClothScale(inout float4 r1, inout float4 r2, inout float4 r3, float3 v, float3 n) {
  float4 C = IniParams.Load(int2(163, 0));
  if (C.x < 0.5 || C.w < 0.5) return;
  float3x3 A = float3x3(r1.xyz, r2.xyz, r3.xyz);
  float3x3 B = mul(transpose(A), A);
  float dev = abs(B[0][0] - 1.0) + abs(B[1][1] - 1.0) + abs(B[2][2] - 1.0) + abs(B[0][1]) + abs(B[0][2]) + abs(B[1][2]);
  if (dev < 2e-3) return;   // sin escala: nada que hacer
  float e = (clamp(C.y, 0.0, 1.0) - 1.0) * 0.5;
  float3x3 M = PesSymPow(B, e);
  float3x3 A2 = mul(A, M);
  float rr = C.z > 0.001 ? C.z : 0.07;
  float3 nn = dot(n, n) > 1e-8 ? normalize(n) : float3(0, 0, 0);
  float3 d = mul(A - A2, v - rr * nn);
  r1.xyz = A2[0]; r2.xyz = A2[1]; r3.xyz = A2[2];
  r1.w += d.x; r2.w += d.y; r3.w += d.z;
}
#endif
