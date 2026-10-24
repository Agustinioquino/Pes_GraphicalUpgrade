// PES21 FX - vidrio de los menus del juego (24/10): geometry shader para los dibujos del menu (PS 55a003da con los VS
// fb68a5a0 / 1932d77d). Pasa los vertices sin cambios y le agrega a cada triangulo el rectangulo que ocupa en pantalla
// (NDC) para que pes_glass_ps.hlsl conozca la forma del elemento: esquinas redondeadas, borde brillante y refraccion.
// Un quad del menu son dos triangulos rectangulos: la caja de cada uno es el quad entero.
struct VSOut {
  float3 uv : TEXCOORD0;
  float4 mulc : TEXCOORD1;
  float4 addc : TEXCOORD2;
  float4 col : COLOR0;
  float4 pos : SV_Position;
};
struct GSOut {
  float3 uv : TEXCOORD0;
  float4 mulc : TEXCOORD1;
  float4 addc : TEXCOORD2;
  float4 col : COLOR0;
  float4 rect : TEXCOORD3;   // NDC: x min, y min, x max, y max
  float4 info : TEXCOORD4;   // x = 1 si el triangulo es medio rectangulo alineado con la pantalla (sin perspectiva)
  float4 pos : SV_Position;
};

[maxvertexcount(3)]
void main(triangle VSOut v[3], inout TriangleStream<GSOut> s) {
  float2 p0 = v[0].pos.xy / v[0].pos.w, p1 = v[1].pos.xy / v[1].pos.w, p2 = v[2].pos.xy / v[2].pos.w;
  float2 mn = min(p0, min(p1, p2)), mx = max(p0, max(p1, p2));
  float2 e1 = p1 - p0, e2 = p2 - p0;
  float area = abs(e1.x * e2.y - e1.y * e2.x) * 0.5;
  float bb = (mx.x - mn.x) * (mx.y - mn.y);
  float w0 = abs(v[0].pos.w);
  float persp = abs(v[0].pos.w - v[1].pos.w) + abs(v[0].pos.w - v[2].pos.w);
  float ok = (bb > 1e-7 && abs(area - 0.5 * bb) < 0.01 * bb && persp < 1e-3 * max(w0, 1e-3)) ? 1.0 : 0.0;
  [unroll] for (int i = 0; i < 3; i++) {
    GSOut o;
    o.uv = v[i].uv; o.mulc = v[i].mulc; o.addc = v[i].addc; o.col = v[i].col;
    o.rect = float4(mn, mx);
    o.info = float4(ok, 0, 0, 0);
    o.pos = v[i].pos;
    s.Append(o);
  }
  s.RestartStrip();
}
