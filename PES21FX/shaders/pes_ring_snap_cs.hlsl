// PES21 FX - foto de las marcas del aro y de la medicion de la piel (24/10 g), en Present y en el orden de la GPU.
// Los dibujos de la piel con la variante (pes_skin_hl_ps / pes_face_hl_ps) suman en gAcc (ring.cpp). Antes el aro de cada
// cuadro leia el buffer del cuadro segun un numero de cuadro de la CPU, y eso fallaba: los dibujos se graban en contextos
// diferidos (el numero no es el del cuadro en que se ejecutan) y el menu presenta mas cuadros que los que dibuja la escena
// 3D (en el editor ~60 %): en los cuadros sin escena no habia marcas y el aro desaparecia (parpadeo).
// Ahora, en cada Present: cada lugar que este cuadro vio (pixeles > 0) pasa a gSnap; el que no, se queda con lo ultimo que
// se vio (la imagen del jugador en pantalla tambien es la ultima que se dibujo) y cuenta cuadros sin verse; una marca que no
// se ve en 20 cuadros se apaga. Despues gAcc vuelve a cero para el cuadro siguiente.
// Lugares (16 bytes cada uno): 0..3 marcas (suma x, suma y, pixeles, -); 4 cuello del cuerpo y 5 franja de abajo de la cara
// (r, g, b, pixeles); 6 alto de la cara (8192 - y minimo, y maximo, -, pixeles); 7 libre. gSnap: los mismos 128 bytes y desde
// el byte 128 un contador de cuadros sin verse por lugar.
RWByteAddressBuffer gAcc : register(u0);
RWByteAddressBuffer gSnap : register(u1);

[numthreads(8, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint k = id.x;
  if (k >= 8) return;
  uint o = k * 16;
  uint4 a = gAcc.Load4(o);
  uint n = (k < 4) ? a.z : a.w;
  uint age = gSnap.Load(128 + k * 4);
  if (n > 0) { gSnap.Store4(o, a); age = 0; }
  else {
    age = min(age + 1, 1000000u);
    if (k < 4 && age > 20) gSnap.Store4(o, uint4(0, 0, 0, 0));
  }
  gSnap.Store(128 + k * 4, age);
  gAcc.Store4(o, uint4(0, 0, 0, 0));
}
