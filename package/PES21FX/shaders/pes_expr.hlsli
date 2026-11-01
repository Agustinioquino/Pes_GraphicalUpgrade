// PES21 FX (31/10) - expresiones faciales mas marcadas, en el VS de la cabeza (faab87ee) y el de la cara animada (455eb97b).
// Incluir DESPUES de los cbuffer VSBones (g_vsBone / g_vsPrevBone) e IniParams.
//
// Cada vertice de la cara se mueve con sk_head (la cabeza rigida) y con huesos faciales (skf_*). Lo que un hueso facial le
// agrega al vertice respecto de la cabeza es  d = M_hueso * p - M_cabeza * p  (el gesto). El mod suma (ganancia - 1) * d,
// pesado como en el skinning: con ganancia 1 queda exacto como el juego, con 0 la cara queda quieta, con 1.5 el gesto es un
// 50 % mas grande. En partido (sin animacion facial) d = 0 y no cambia nada.
// - Donde esta sk_head en la paleta lo dice la DLL por malla (expr.cpp, cbuffer b12: x = cabeza, 99 = sin dato -> nada).
// - El cuello y el pecho no son gestos: se saltean el hueso de antes de la cabeza (sk_neck) y, si la cabeza esta en 1 o 2, todos
//   los de antes (sk_chest); los ayudantes del cuello (dsk_*) quedan fuera por la mascara de la cara (frente de la cabeza).
// - Gestos grandes (mandibula abierta, > 1.2 cm) se agrandan menos (340.w); el agregado tiene tope (340.z cm).
// - Por zona (341): cuanto del agregado va a frente / cejas, ojos / parpados (0 por defecto: pestanas y parpados contra el ojo),
//   mejillas / nariz y boca / labios. Las zonas salen de la posicion de reposo (metros, espacio del modelo: ojos en y 1.697).
// - Igual en la posicion del cuadro anterior (vectores de movimiento del TAA) y en normal / tangente.
#ifndef PES_EXPR_HLSLI
#define PES_EXPR_HLSLI

cbuffer PESExprMesh : register(b12)
{
  uint4 g_pesExprMesh;   // x cabeza en la paleta (99 = nada), y hueso maximo usado, z id de la malla
}

float PesExprZoneShare(float3 r, float4 Z)
{
  float ex = abs(r.x) - 0.028, ey = (r.y - 1.697) * 1.4;
  float we = 1.0 - smoothstep(0.012, 0.022, sqrt(ex * ex + ey * ey));     // ojos y parpados
  float wb = smoothstep(1.700, 1.714, r.y) * (1.0 - we);                 // frente y cejas
  float wm = (1.0 - smoothstep(1.646, 1.664, r.y)) * (1.0 - we);         // boca, labios, menton
  float wc = saturate(1.0 - we - wb - wm);                               // mejillas, nariz, surcos
  return wb * Z.x + we * Z.y + wc * Z.z + wm * Z.w;
}

bool PesExprSkip(uint j, uint head)
{
  if (j == head) return true;
  if (head <= 2) return j < head;          // face_high: sk_chest / sk_neck antes de la cabeza
  return j + 1 == head;                    // cara generica (cabeza al final): sk_neck justo antes
}

// p = posicion de reposo usada en el skinning (w = 1), rest = reposo para las zonas; I / W = huesos y pesos del vertice.
// Suma a pos / prev / nrm / tan (salidas del skinning del juego, espacio del objeto).
void PesExprApply(float4 p, float3 rest, uint4 I, float4 W, float3 n, float3 t,
                  inout float3 pos, inout float3 prev, inout float3 nrm, inout float3 tng)
{
  float4 E = IniParams.Load(int2(340, 0));
  if (E.x < 0.5) return;
  uint head = g_pesExprMesh.x;
  if (head > 31) return;
  float4 Z = IniParams.Load(int2(341, 0));
  // mascara: de la base del menton para arriba y no la nuca (01/11: antes cortaba en z 0.06..0.095 y dejaba fuera el fondo
  // de la boca / dientes de la boca 3D: labios agrandados y dientes no -> union rara)
  float face = smoothstep(1.580, 1.610, rest.y) * smoothstep(0.000, 0.035, rest.z);
  float extra = (E.y - 1.0) * PesExprZoneShare(rest, Z) * face;
  if (abs(extra) < 0.001) return;
  float bigW = saturate(E.w);
  float cap = max(E.z, 0.0) * 0.01;

  float4x3 Mh = g_vsBone.m_boneMatrices[head];
  float4x3 Ph = g_vsPrevBone.m_boneMatrices[head];
  float3 ph = mul(p, Mh), pph = mul(p, Ph);
  float3 nh = mul(n, (float3x3)Mh), th = mul(t, (float3x3)Mh);
  float3 dP = 0, dQ = 0, dN = 0, dT = 0;
  [unroll] for (int k = 0; k < 4; k++) {
    uint j = I[k]; float w = W[k];
    if (w <= 0.0 || j > 31 || PesExprSkip(j, head)) continue;
    float4x3 M = g_vsBone.m_boneMatrices[j];
    float3 d = mul(p, M) - ph;
    float s = w * lerp(1.0, bigW, smoothstep(0.012, 0.040, length(d)));
    dP += s * d;
    dQ += s * (mul(p, g_vsPrevBone.m_boneMatrices[j]) - pph);
    dN += s * (mul(n, (float3x3)M) - nh);
    dT += s * (mul(t, (float3x3)M) - th);
  }
  float3 a = extra * dP, b = extra * dQ;
  float la = length(a), lb = length(b);
  if (cap > 0.0) { if (la > cap) a *= cap / la; if (lb > cap) b *= cap / lb; }
  pos += a; prev += b;
  nrm += extra * dN; tng += extra * dT;   // el VS normaliza despues
}
#endif
