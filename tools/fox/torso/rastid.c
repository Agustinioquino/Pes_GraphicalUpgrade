// Rasterizador de ids: lee triangulos proyectados (x, y en pixeles, z mayor = mas cerca) y escribe por pixel el triangulo
// visible y sus coordenadas baricentricas (int32 id o -1, float32 b0, float32 b1). El sombreado (normales, mapas normales,
// albedo) lo hace preview_nm.py con numpy. Uso: rastid entrada.bin salida.bin
// entrada: int32 W, H, n; despues n * 3 vertices de float32 (x, y, z)
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
typedef struct { float x, y, z; } V;
int main(int argc, char** argv) {
    if (argc < 3) return 1;
    FILE* f = fopen(argv[1], "rb"); if (!f) return 2;
    int W, H, n;
    if (fread(&W, 4, 1, f) != 1 || fread(&H, 4, 1, f) != 1 || fread(&n, 4, 1, f) != 1) return 3;
    V* t = (V*)malloc(sizeof(V) * 3 * (size_t)n);
    if (fread(t, sizeof(V), 3 * (size_t)n, f) != 3 * (size_t)n) return 4;
    fclose(f);
    float* zb = (float*)malloc(sizeof(float) * W * H);
    int* id = (int*)malloc(sizeof(int) * W * H);
    float* b0 = (float*)malloc(sizeof(float) * W * H);
    float* b1 = (float*)malloc(sizeof(float) * W * H);
    for (int i = 0; i < W * H; i++) { zb[i] = -1e30f; id[i] = -1; b0[i] = b1[i] = 0; }
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
            int i = y * W + x;
            if (z > zb[i]) { zb[i] = z; id[i] = k; b0[i] = w0; b1[i] = w1; }
        }
    }
    FILE* o = fopen(argv[2], "wb"); if (!o) return 5;
    fwrite(id, 4, (size_t)W * H, o); fwrite(b0, 4, (size_t)W * H, o); fwrite(b1, 4, (size_t)W * H, o);
    fclose(o);
    return 0;
}
