// Rasterizador minimo para previsualizar mallas: lee triangulos ya proyectados (x, y en pixeles, z mayor = mas cerca,
// normal por vertice) y escribe una imagen en gris con sombreado Lambert + luz de borde. Uso: rast entrada.bin salida.pgm
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
typedef struct { float x, y, z, nx, ny, nz; } V;
int main(int argc, char** argv) {
    if (argc < 3) return 1;
    FILE* f = fopen(argv[1], "rb"); if (!f) return 2;
    int W, H, n; float L[3];
    fread(&W, 4, 1, f); fread(&H, 4, 1, f); fread(&n, 4, 1, f); fread(L, 4, 3, f);
    V* t = (V*)malloc(sizeof(V) * 3 * (size_t)n); fread(t, sizeof(V), 3 * (size_t)n, f); fclose(f);
    float* zb = (float*)malloc(sizeof(float) * W * H); unsigned char* img = (unsigned char*)malloc(W * H);
    for (int i = 0; i < W * H; i++) { zb[i] = -1e30f; img[i] = 40; }
    for (int k = 0; k < n; k++) {
        V* a = &t[3 * k]; V* b = a + 1; V* c = a + 2;
        float minx = fminf(a->x, fminf(b->x, c->x)), maxx = fmaxf(a->x, fmaxf(b->x, c->x));
        float miny = fminf(a->y, fminf(b->y, c->y)), maxy = fmaxf(a->y, fmaxf(b->y, c->y));
        int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= W) x1 = W - 1; if (y1 >= H) y1 = H - 1;
        float den = (b->y - c->y) * (a->x - c->x) + (c->x - b->x) * (a->y - c->y);
        if (fabsf(den) < 1e-12f) continue;
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((b->y - c->y) * (px - c->x) + (c->x - b->x) * (py - c->y)) / den;
            float w1 = ((c->y - a->y) * (px - c->x) + (a->x - c->x) * (py - c->y)) / den;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * a->z + w1 * b->z + w2 * c->z;
            int id = y * W + x;
            if (z <= zb[id]) continue;
            zb[id] = z;
            float nx = w0 * a->nx + w1 * b->nx + w2 * c->nx, ny = w0 * a->ny + w1 * b->ny + w2 * c->ny, nz = w0 * a->nz + w1 * b->nz + w2 * c->nz;
            float nl = sqrtf(nx * nx + ny * ny + nz * nz); if (nl < 1e-9f) nl = 1; nx /= nl; ny /= nl; nz /= nl;
            if (nz < 0) { nx = -nx; ny = -ny; nz = -nz; }   // cara de atras: se ve igual
            float d = nx * L[0] + ny * L[1] + nz * L[2]; if (d < 0) d = 0;
            float rim = powf(1.0f - nz, 3.0f) * 0.35f;
            float v = 0.18f + 0.72f * d + rim; if (v > 1) v = 1;
            img[id] = (unsigned char)(v * 255.0f);
        }
    }
    FILE* o = fopen(argv[2], "wb"); fprintf(o, "P5\n%d %d\n255\n", W, H); fwrite(img, 1, W * H, o); fclose(o);
    return 0;
}
