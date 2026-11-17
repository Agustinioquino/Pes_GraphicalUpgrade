// ---------------------------------------------------------------- (15/11) grilla de piso del Motor en tiempo real
// Agustin 01:51: "enfocate en el menu en si, sin fondo con grilla". El estudio del Motor no muestra el fondo del Editar: en su
// lugar va un fondo gris oscuro con una grilla de piso en perspectiva (como la de Blender), con el eje X en rojo y el Z en azul.
// Se dibuja en una textura propia (CPU, 1920 x 1080 desde el 15/11 r) con la camara libre del Motor (freecam.cpp: posicion, giro y campo de vision)
// y se entrega como fondo en el mismo dibujo donde antes iba el negro (glass.cpp EditorBgFor): queda DETRAS del jugador y de los
// paneles, no encima. Una vez por cuadro. [PES21FX] motor_grilla = 0 deja el fondo gris sin lineas (15/11 o; antes negro).
#ifndef MOTORGRID_TEST
#include "core.h"
#endif
#include <atomic>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <mutex>
#include <map>

namespace fx {

// (15/11 r, Agustin 19:22 con video: "sobre la grilla la resolucion") antes 960 x 540 estirado a la pantalla: lineas borrosas y
// escalonadas. Ahora a la resolucion de 1920 x 1080 (solo se rehace cuando cambia la camara o las opciones)
static const int kW = 1920, kH = 1080;
static ID3D11Texture2D* s_tex = nullptr; static ID3D11ShaderResourceView* s_srv = nullptr; static bool s_tried = false;
static std::vector<uint32_t> s_px;
static std::atomic<bool> s_fresh{false};       // ya se dibujo en este cuadro
// (15/11 o) relacion de aspecto de la PANTALLA: la textura de 960 x 540 se estira a toda la pantalla; con un monitor 21:9 (el de
// Agustin, 2560 x 1080) la grilla se proyectaba como 16:9 y quedaba estirada de costado (no seguia a la camara como el jugador)
static float s_asp = 16.0f / 9.0f;
// (16/11 b, Agustin 18:21: "el jugador sigue viendose en 2d no 3d") la sombra de contacto iba en el origen, pero en el estudio del
// Motor el jugador NO esta en el origen (video 15.15.42: parado sobre el eje X, ~1-2 m al costado; la mancha quedaba lejos de los pies
// y el jugador parecia pegado). Ahora el centro de la sombra sale de los pies reales: el punto mas bajo del jugador en su profundidad
// (borrada a 0, todo lo que no es 0 es el jugador) proyectado al piso con la misma camara. s_shX / s_shZ = centro en el piso (m)
static float s_shX = 0.0f, s_shZ = 0.0f; static bool s_shOk = false;
static float s_feetU = -1.0f, s_feetV = -1.0f;   // pies en la imagen (0..1), -1 = sin dato

static void MotorDepthDiagTick();
#ifndef MOTORGRID_TEST
static void MotorFeetPoll(ID3D11DeviceContext* ctx);
#else
static void MotorFeetPoll(void*) {}
#endif
void MotorGridPresent() { s_fresh.store(false, std::memory_order_relaxed);
#ifndef MOTORGRID_TEST
    MotorVistaPresent();
    MotorJugPresent();
    MotorDepthDiagTick();
#endif
}

// (17/11) con el jugador en la escena principal (motorvista.cpp) no hay fondo del menu donde poner la grilla: se pinta la misma grilla
// sobre fondo TRANSPARENTE y el editor la dibuja encima de la vista principal (MotorGridOverlay). s_ovl = ese modo.
static bool s_ovl = false;
static inline void Plot(int x, int y, uint32_t rgb, float a) {
    if ((unsigned)x >= (unsigned)kW || (unsigned)y >= (unsigned)kH || a <= 0.0f) return;
    uint32_t& d = s_px[(size_t)y * kW + x]; if (a > 1.0f) a = 1.0f;
    const int ia = (int)(a * 256.0f);
    if (s_ovl) {      // transparente: color sin mezclar con el fondo, la opacidad se acumula
        const int oa = (int)(d >> 24); const int na = std::min(255, oa + (((255 - oa) * ia) >> 8));
        if (oa == 0) { d = ((uint32_t)na << 24) | (rgb & 0xffffffu); return; }
        const int w = na > 0 ? std::min(256, (ia * 255) / std::max(1, na)) : 256;
        const int r0 = (int)(d & 255), g0 = (int)((d >> 8) & 255), b0 = (int)((d >> 16) & 255);
        const int r1 = r0 + ((((int)(rgb & 255)) - r0) * w >> 8), g1 = g0 + ((((int)((rgb >> 8) & 255)) - g0) * w >> 8), b1 = b0 + ((((int)((rgb >> 16) & 255)) - b0) * w >> 8);
        d = ((uint32_t)na << 24) | (uint32_t)r1 | ((uint32_t)g1 << 8) | ((uint32_t)b1 << 16); return;
    }
    const int r = (int)(d & 255), g = (int)((d >> 8) & 255), b = (int)((d >> 16) & 255);
    const int nr = r + ((((int)(rgb & 255)) - r) * ia >> 8), ng = g + ((((int)((rgb >> 8) & 255)) - g) * ia >> 8), nb = b + ((((int)((rgb >> 16) & 255)) - b) * ia >> 8);
    d = 0xff000000u | (uint32_t)nr | ((uint32_t)ng << 8) | ((uint32_t)nb << 16);
}
// linea con suavizado simple (dos pixeles por paso); a0 / a1 = opacidad en cada punta
static void Line(float x0, float y0, float x1, float y1, uint32_t rgb, float a0, float a1) {
    const float dx = x1 - x0, dy = y1 - y0; const float len = std::max(std::fabs(dx), std::fabs(dy));
    if (!(len >= 0.0f) || len > 8000.0f) return;
    const int n = (int)len + 1; const bool steep = std::fabs(dy) > std::fabs(dx);
    for (int i = 0; i <= n; i++) {
        const float t = n > 0 ? (float)i / (float)n : 0.0f; const float x = x0 + dx * t, y = y0 + dy * t, a = a0 + (a1 - a0) * t;
        if (steep) { const int xi = (int)std::floor(x); const float fr = x - (float)xi; Plot(xi, (int)(y + 0.5f), rgb, a * (1.0f - fr)); Plot(xi + 1, (int)(y + 0.5f), rgb, a * fr); }
        else { const int yi = (int)std::floor(y); const float fr = y - (float)yi; Plot((int)(x + 0.5f), yi, rgb, a * (1.0f - fr)); Plot((int)(x + 0.5f), yi + 1, rgb, a * fr); }
    }
}

struct Cam { float pos[3], f[3], r[3], u[3], th; };
// segmento del mundo -> pantalla, recortado contra el plano cercano; la opacidad baja con la distancia
static void Seg(const Cam& c, const float* a, const float* b, uint32_t rgb, float alpha, float fadeDist, float ka = 1.0f, float kb = 1.0f) {
    float pa[3], pb[3];
    for (int k = 0; k < 2; k++) {
        const float* p = k ? b : a; float* o = k ? pb : pa; const float d[3] = { p[0] - c.pos[0], p[1] - c.pos[1], p[2] - c.pos[2] };
        o[0] = d[0] * c.r[0] + d[1] * c.r[1] + d[2] * c.r[2]; o[1] = d[0] * c.u[0] + d[1] * c.u[1] + d[2] * c.u[2]; o[2] = d[0] * c.f[0] + d[1] * c.f[1] + d[2] * c.f[2];
    }
    const float zn = 0.05f;
    if (pa[2] < zn && pb[2] < zn) return;
    if (pa[2] < zn) { const float t = (zn - pa[2]) / (pb[2] - pa[2]); for (int i = 0; i < 3; i++) pa[i] += (pb[i] - pa[i]) * t; }
    if (pb[2] < zn) { const float t = (zn - pb[2]) / (pa[2] - pb[2]); for (int i = 0; i < 3; i++) pb[i] += (pa[i] - pb[i]) * t; }
    const float asp = s_asp, hx = 0.5f * kW, hy = 0.5f * kH;
    float x0 = hx + pa[0] / (pa[2] * c.th * asp) * hx, y0 = hy - pa[1] / (pa[2] * c.th) * hy;
    float x1 = hx + pb[0] / (pb[2] * c.th * asp) * hx, y1 = hy - pb[1] / (pb[2] * c.th) * hy;
    // recorte grueso al rectangulo agrandado (para no recorrer lineas enormes)
    const float lo = -2000.0f, hiX = kW + 2000.0f, hiY = kH + 2000.0f;
    if ((x0 < lo && x1 < lo) || (x0 > hiX && x1 > hiX) || (y0 < lo && y1 < lo) || (y0 > hiY && y1 > hiY)) return;
    if (std::fabs(x0) > 6000.0f || std::fabs(y0) > 6000.0f || std::fabs(x1) > 6000.0f || std::fabs(y1) > 6000.0f) {
        // una punta muy lejos de la pantalla: se parte en tramos y se dibujan solo los que caen cerca
        float m[3]; for (int i = 0; i < 3; i++) m[i] = 0.5f * (a[i] + b[i]);
        const float l2 = (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
        const float km = 0.5f * (ka + kb);
        if (l2 > 0.01f) { Seg(c, a, m, rgb, alpha, fadeDist, ka, km); Seg(c, m, b, rgb, alpha, fadeDist, km, kb); }
        return;
    }
    // ka / kb: opacidad de cada punta del tramo (la grilla la calcula por distancia en el piso); si se recorto contra el plano
    // cercano, la punta recortada toma la de la otra
    const bool ca = pa[2] <= 0.0501f, cb = pb[2] <= 0.0501f; const float qa = ca ? kb : ka, qb = cb ? ka : kb;
    const float f0 = std::clamp(1.0f - pa[2] / fadeDist, 0.0f, 1.0f) * qa, f1 = std::clamp(1.0f - pb[2] / fadeDist, 0.0f, 1.0f) * qb;
    Line(x0, y0, x1, y1, rgb, alpha * f0, alpha * f1);
}

// pinta el fondo y la grilla en s_px con la camara c
static void Blob(const Cam& c, float cxw, float czw, float R, float str) {
    const float asp = s_asp, hx = 0.5f * kW, hy = 0.5f * kH;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int q = 0; q < 16; q++) { const float an = (float)q * 0.3926991f; const float w[3] = { cxw + cosf(an) * R, 0.0f, czw + sinf(an) * R };
        const float d[3] = { w[0] - c.pos[0], w[1] - c.pos[1], w[2] - c.pos[2] };
        const float z = d[0] * c.f[0] + d[1] * c.f[1] + d[2] * c.f[2]; if (z < 0.05f) { x0 = 0; y0 = 0; x1 = (float)kW; y1 = (float)kH; break; }
        const float sx = hx + (d[0] * c.r[0] + d[1] * c.r[1] + d[2] * c.r[2]) / (z * c.th * asp) * hx, sy = hy - (d[0] * c.u[0] + d[1] * c.u[1] + d[2] * c.u[2]) / (z * c.th) * hy;
        x0 = std::min(x0, sx); x1 = std::max(x1, sx); y0 = std::min(y0, sy); y1 = std::max(y1, sy); }
    const int ix0 = std::max(0, (int)x0 - 2), ix1 = std::min(kW - 1, (int)x1 + 2), iy0 = std::max(0, (int)y0 - 2), iy1 = std::min(kH - 1, (int)y1 + 2);
    for (int y = iy0; y <= iy1; y++) { const float b = (hy - (float)y) / hy * c.th;
        for (int x = ix0; x <= ix1; x++) { const float a = ((float)x - hx) / hx * c.th * asp;
            const float dir[3] = { c.f[0] + c.r[0] * a + c.u[0] * b, c.f[1] + c.r[1] * a + c.u[1] * b, c.f[2] + c.r[2] * a + c.u[2] * b };
            if (dir[1] > -1e-5f) continue; const float t = -c.pos[1] / dir[1]; const float gx = c.pos[0] + dir[0] * t, gz = c.pos[2] + dir[2] * t;
            const float dd = sqrtf((gx - cxw) * (gx - cxw) + (gz - czw) * (gz - czw)) / R; if (dd >= 1.0f) continue;
            const float k = (1.0f - dd) * (1.0f - dd) * str;      // mas oscura al centro, se desvanece hacia el borde
            uint32_t& px = s_px[(size_t)y * kW + x]; const int m = (int)((1.0f - k) * 256.0f);
            const uint32_t al = s_ovl ? std::max<uint32_t>(px >> 24, (uint32_t)(k * 255.0f)) : 255u;
            px = (al << 24) | (uint32_t)(((px & 255) * m) >> 8) | ((uint32_t)((((px >> 8) & 255) * m) >> 8) << 8) | ((uint32_t)((((px >> 16) & 255) * m) >> 8) << 16); } }
}
static void GridPaint(const Cam& c) {
    // --- fondo: gris oscuro con un degrade suave hacia arriba ---
    for (int y = 0; y < kH; y++) {
        const float t = (float)y / (float)(kH - 1); const int v = (int)(30.0f + 14.0f * (1.0f - t));
        const uint32_t col = s_ovl ? 0u : (0xff000000u | (uint32_t)v | ((uint32_t)v << 8) | ((uint32_t)(v + 3) << 16));
        uint32_t* row = &s_px[(size_t)y * kW]; for (int x = 0; x < kW; x++) row[x] = col;
    }
    // --- grilla (15/11 o, Agustin 05:14: "al desactivar la grilla del piso todo se pone negro, la grilla a cierta distancia pierde la
    // simetria"). Apagada queda el fondo gris de arriba, sin lineas. Antes era un cuadrado alrededor de la camara que se apagaba por la
    // profundidad de cada punta (interpolada en pantalla): de costado se cortaba en las esquinas del cuadrado y cada linea se apagaba
    // distinto. Ahora: un CIRCULO alrededor de la camara, cada linea partida en tramos cortos y cada punta apagada por su distancia real
    // en el piso (igual para todos lados), y las lineas finas se apagan cuando quedan a menos de ~3 pixeles de su vecina (lejos se
    // juntaban y hacian dibujos raros). Opciones: [PES21FX] motor_grilla_celda (m), motor_grilla_linea_fuerte (cada N celdas, 0 = sin),
    // motor_grilla_intensidad (0..200 %), motor_grilla_ejes, motor_grilla_tamano (m de lado; el circulo es de ese diametro).
    if (g_cfg.motorGrid != 0) {
        const float cell = std::clamp(g_cfg.motorGridCell, 0.1f, 10.0f); const int major = std::clamp(g_cfg.motorGridMajor, 0, 50);
        const float gain = std::clamp((float)g_cfg.motorGridAlpha, 0.0f, 200.0f) / 100.0f;
        // (15/11 q, Agustin 17:28: "que la grilla se renderize con la distancia al maximo") tamano 0 = MAXIMA: hasta 2 km (el horizonte
        // del visor). Para que no cueste: por niveles, como los editores. Nivel 0 = la celda hasta 60 celdas de la camara; nivel 1 = cada
        // M celdas (M = la linea fuerte, o 10) desde ahi hasta 60 x M celdas; nivel 2 = cada M x M... Cada nivel dibuja solo el anillo que
        // el anterior no cubre (sin lineas dobles) y sus tramos son mas largos cuanto mas lejos.
        const bool maxi = g_cfg.motorGridSize <= 0;
        const float Rmax = maxi ? 2000.0f : std::min((float)std::clamp(g_cfg.motorGridSize, 10, 199) * 0.5f, cell * 600.0f);
        const float fade = maxi ? Rmax : std::min(Rmax, (9.0f + 6.0f * std::fabs(c.pos[1]) + 0.45f / c.th) * std::max(1.0f, Rmax / 20.0f));
        const int M = major > 1 ? major : 10;
        const float px = c.pos[0], pz = c.pos[2];
        const float asp = s_asp, hx = 0.5f * kW, hy = 0.5f * kH;
        auto proj = [&](const float* w, float& sx, float& sy) {      // false = detras de la camara
            const float d[3] = { w[0] - c.pos[0], w[1] - c.pos[1], w[2] - c.pos[2] };
            const float z = d[0] * c.f[0] + d[1] * c.f[1] + d[2] * c.f[2]; if (z < 0.05f) return false;
            sx = hx + (d[0] * c.r[0] + d[1] * c.r[1] + d[2] * c.r[2]) / (z * c.th * asp) * hx; sy = hy - (d[0] * c.u[0] + d[1] * c.u[1] + d[2] * c.u[2]) / (z * c.th) * hy; return true; };
        // separacion en pantalla entre esta linea y su vecina (a "st" de distancia por el otro eje), en pixeles
        auto spacing = [&](const float* m, int axis, float st) {
            float n[3] = { m[0], 0.0f, m[2] }; n[axis] += st; float t[3] = { m[0], 0.0f, m[2] }; t[2 - axis] += st * 0.5f;
            float ax, ay, bx, by, tx, ty; if (!proj(m, ax, ay) || !proj(n, bx, by) || !proj(t, tx, ty)) return 1.0e6f;
            float dx = tx - ax, dy = ty - ay; const float l = sqrtf(dx * dx + dy * dy); if (l < 1.0e-4f) return 1.0e6f; dx /= l; dy /= l;
            return std::fabs((bx - ax) * dy - (by - ay) * dx); };
        // (15/11 r, Agustin 18:43: "la grilla sigue desapareciendo en el horizonte") en Maxima no se apaga por distancia hasta el
        // ultimo 15 % del radio de 2 km
        auto fadeAt = [&](float x, float z) { const float d = sqrtf((x - px) * (x - px) + (z - pz) * (z - pz)) / fade;
            const float a = maxi ? std::clamp((1.0f - d) / 0.15f, 0.0f, 1.0f) : std::clamp(1.0f - d, 0.0f, 1.0f); return a * a * (3.0f - 2.0f * a); };
        float rIn = 0.0f, step = cell; long long mult = 1; float Rlv = Rmax;
        auto ringFade = [&](float x, float z) { const float d = sqrtf((x - px) * (x - px) + (z - pz) * (z - pz));
            const float a = std::clamp((Rlv - d) / (0.4f * Rlv), 0.0f, 1.0f); return a * a * (3.0f - 2.0f * a); };
        for (int lv = 0; lv < 5 && rIn < Rmax; lv++, step *= (float)M, mult *= M) {
            const float R = maxi ? std::min(Rmax, step * 60.0f) : Rmax; Rlv = R;
            for (int axis = 0; axis < 2; axis++) {          // 0: lineas de x fija (corren por z, el eje Z azul) / 1: de z fija (el eje X rojo)
                const float pc = axis == 0 ? px : pz, po = axis == 0 ? pz : px;
                const long long k0 = (long long)std::ceil((pc - R) / step), k1 = (long long)std::floor((pc + R) / step);
                for (long long kk = k0; kk <= k1; kk++) {
                    const float v = (float)kk * step, dv = v - pc; const float hc = sqrtf(std::max(0.0f, R * R - dv * dv)); if (hc < 1.0e-3f) continue;
                    const float hi = rIn > std::fabs(dv) ? sqrtf(rIn * rIn - dv * dv) : 0.0f;      // tramo que ya dibujo el nivel anterior
                    const long long kc = kk * mult;      // numero de la linea en celdas
                    const bool ax0 = kk == 0 && g_cfg.motorGridAxes != 0, maj = major > 0 && kc % major == 0;
                    const uint32_t col = ax0 ? (axis == 0 ? 0x00e08c40u : 0x005448eau) : 0x00909090u;
                    const float base = (ax0 ? 0.95f : maj ? 0.50f : 0.24f) * gain; if (base <= 0.003f) continue;
                    // (15/11 s, Agustin 19:28: "la grilla no sigue las lineas correctamente"; video 16.20.38: en el borde de cada nivel las
                    // lineas finas se cortaban de golpe en una curva y arriba seguian solo las gruesas, como si la grilla se partiera).
                    // Las lineas que NO siguen en el nivel siguiente (las que no son multiplo de M) se apagan suave en el ultimo 40 % del
                    // anillo; las que siguen (multiplo de M) pasan al nivel siguiente con la misma intensidad, sin corte
                    const bool ringOut = maxi && R < Rmax && (kk % M) != 0;
                    for (int side = 0; side < 2; side++) {      // los dos lados del anillo (o la linea entera si no hay anillo adentro)
                        float t = side == 0 ? po - hc : po + hi; const float tEnd = side == 0 ? (hi > 0.0f ? po - hi : po + hc) : po + hc;
                        if (side == 1 && hi <= 0.0f) break;
                        while (t < tEnd) {
                            const float dist = std::fabs(dv) + std::fabs(t - po); const float sg = std::clamp(dist * 0.18f, 0.25f, 6.0f * (float)mult); const float t2 = std::min(tEnd, t + sg);
                            float a[3], b[3]; if (axis == 0) { a[0] = b[0] = v; a[2] = t; b[2] = t2; } else { a[2] = b[2] = v; a[0] = t; b[0] = t2; } a[1] = b[1] = 0.0f;
                            float fa = fadeAt(a[0], a[2]), fb = fadeAt(b[0], b[2]);
                            if (ringOut) { fa *= ringFade(a[0], a[2]); fb *= ringFade(b[0], b[2]); }
                            if (fa > 0.003f || fb > 0.003f) {
                                float sp = 1.0f;
                                if (!ax0) { float m[3] = { (a[0] + b[0]) * 0.5f, 0.0f, (a[2] + b[2]) * 0.5f }; const float s1 = spacing(m, axis, step) * (maj && mult < major ? (float)major / (float)mult : 1.0f); sp = std::clamp((s1 - 2.0f) / 4.0f, 0.0f, 1.0f);
                                    // en Maxima las lineas fuertes y las del ultimo nivel no se apagan del todo al juntarse cerca del horizonte:
                                    // se funden en una franja suave en vez de cortarse
                                    if (maxi && (maj || R >= Rmax || lv >= 3)) sp = std::max(sp, 0.30f); }
                                if (sp > 0.003f) Seg(c, a, b, col, base * sp, 1.0e9f, fa, fb);
                            }
                            t = t2;
                        }
                    }
                }
            }
            rIn = R;
            if (!maxi) break;
        }
    }
    // --- (15/11 r, Agustin 19:22: "pq el jugador parece un png") sombra de contacto del jugador sobre el piso: el jugador lo dibuja el
    // visor del Editar en su propia imagen y se pega encima de este fondo, sin tocar el piso. Una mancha oscura suave en el piso
    // (y = 0) alrededor de los pies (el jugador del visor esta en el origen) lo apoya. Por pixel: rayo de la camara contra el piso.
    // (17/11 07:13, Agustin: "los jugadores no parecen estar arriba de una grilla") con la grilla encima (jugadores propios en la escena
    // principal) va una mancha por cada jugador propio, oscura y transparente; con la grilla en la composicion queda debajo de ellos.
    if (c.pos[1] > 0.05f) {
        if (!s_ovl) Blob(c, s_shX, s_shZ, 0.75f, 0.55f);
#ifndef MOTORGRID_TEST
        else for (int i = 0; i < kMotorPjMax; i++) { float p[3], yw, sc; if (MotorPjGet(i, p, &yw, &sc) && fabsf(p[1]) < 0.3f) Blob(c, p[0], p[2], 0.55f * std::clamp(sc, 0.3f, 3.0f), 0.6f); }
#endif
    }
}

#ifndef MOTORGRID_TEST
// fondo del estudio del Motor para este cuadro (nullptr = no se pudo: va el negro)
static ID3D11ShaderResourceView* GridSRV(ID3D11DeviceContext* ctx);
ID3D11ShaderResourceView* MotorGridSRV(ID3D11DeviceContext* ctx) { s_ovl = false; return GridSRV(ctx); }
// (17/11) la grilla sola, transparente, para dibujarla encima de la vista principal (jugador en la escena principal). Sin grilla
// (motor_grilla = 0) no hay nada que dibujar.
ID3D11ShaderResourceView* MotorGridOverlay(ID3D11DeviceContext* ctx) {
    if (g_cfg.motorGrid == 0) return nullptr;
    s_ovl = true; ID3D11ShaderResourceView* v = GridSRV(ctx);
    static int nl = 0; if (v && nl++ == 0) Log("motor (grilla): jugador en la escena principal: la grilla se dibuja encima de la vista principal");
    return v;
}
// (17/11 07:08, Agustin: "por ese problema con el z buffer puede ser lo de la grilla tambien") la grilla iba ENCIMA de toda la imagen
// (ImGui), sin profundidad: tapaba a los jugadores. Ahora la misma textura se pone en t49 de la composicion diferida (619b / f807,
// pes_vista_motor.hlsli PESVistaGrilla) y el shader la mezcla solo donde no hay nada dibujado (z buffer vacio): los jugadores la tapan.
// Si en 0,3 s ninguna composicion la tomo (shader viejo, "Ver original"), se vuelve a dibujar encima como antes.
static std::mutex s_ovMx; static ID3D11ShaderResourceView* s_ovSrv = nullptr; static double s_ovT = -1.0e9; static std::atomic<double> s_ovBoundT{ -1.0e9 };
void MotorGridOverlayStore(ID3D11ShaderResourceView* v) {
    std::lock_guard<std::mutex> lk(s_ovMx);
    if (v) v->AddRef(); if (s_ovSrv) s_ovSrv->Release(); s_ovSrv = v; s_ovT = Now();
}
ID3D11ShaderResourceView* MotorGridCompSRV() {
    std::lock_guard<std::mutex> lk(s_ovMx);
    if (!s_ovSrv || Now() - s_ovT > 0.3) return nullptr;
    s_ovSrv->AddRef(); s_ovBoundT.store(Now(), std::memory_order_relaxed);
    static int nl = 0; if (nl++ == 0) Log("motor (grilla): la grilla va en la composicion (t49) con la profundidad: los jugadores la tapan");
    return s_ovSrv;
}
bool MotorGridCompBound() { return Now() - s_ovBoundT.load(std::memory_order_relaxed) < 0.3; }
// (17/11 b) la camara con la que se dibujo la grilla por ultima vez, para poner cosas en el piso con la MISMA proyeccion (los iconos de
// mover / girar / escalar del jugador, motorgizmo.cpp). u, v = 0..1 de la pantalla.
static std::mutex s_camMx; static Cam s_camLast{}; static float s_camAsp = 16.0f / 9.0f; static double s_camT = -1.0e9;
bool MotorGridProject(const float* w, float* u, float* v) {
    std::lock_guard<std::mutex> lk(s_camMx); if (Now() - s_camT > 0.5) return false; const Cam& c = s_camLast;
    const float d[3] = { w[0] - c.pos[0], w[1] - c.pos[1], w[2] - c.pos[2] };
    const float z = d[0] * c.f[0] + d[1] * c.f[1] + d[2] * c.f[2]; if (z < 0.05f || c.th < 1e-5f) return false;
    const float x = d[0] * c.r[0] + d[1] * c.r[1] + d[2] * c.r[2], y = d[0] * c.u[0] + d[1] * c.u[1] + d[2] * c.u[2];
    *u = 0.5f + 0.5f * x / (z * c.th * s_camAsp); *v = 0.5f - 0.5f * y / (z * c.th); return true;
}
bool MotorGridRay(float u, float v, float* o, float* dir) {
    std::lock_guard<std::mutex> lk(s_camMx); if (Now() - s_camT > 0.5) return false; const Cam& c = s_camLast;
    const float a = (u * 2.0f - 1.0f) * c.th * s_camAsp, b = (1.0f - v * 2.0f) * c.th;
    for (int i = 0; i < 3; i++) { o[i] = c.pos[i]; dir[i] = c.f[i] + c.r[i] * a + c.u[i] * b; }
    return true;
}

// (17/11 07:13, Agustin con video: "al mover el jugador los iconos de abajo se sienten desconectados", "los jugadores no parecen estar
// arriba de una grilla", "la vista ortografica y perspectiva no afecta a los jugadores") con los jugadores en la escena principal la
// grilla y los iconos se proyectaban con la camara que se le manda al visor, pero los jugadores los dibuja la vista principal con SU
// proyeccion: dos camaras distintas, por eso no coincidian. Ahora la camara de la grilla sale de la matriz con la que el juego dibujo la
// escena (SceneCB m_projectionView, la misma con la que la vista de huesos cae justo sobre los jugadores): centro = M^-1 (0, 0, 1, 0),
// adelante / derecha / arriba y campo de vision desproyectando puntos de la pantalla.
static bool Inv4(const double* m, double* o) {
    double a[4][8]; for (int r = 0; r < 4; r++) for (int c = 0; c < 8; c++) a[r][c] = c < 4 ? m[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
    for (int c = 0; c < 4; c++) { int pv = c; for (int r = c + 1; r < 4; r++) if (fabs(a[r][c]) > fabs(a[pv][c])) pv = r;
        if (fabs(a[pv][c]) < 1e-12) return false; if (pv != c) for (int k = 0; k < 8; k++) std::swap(a[pv][k], a[c][k]);
        const double d = a[c][c]; for (int k = 0; k < 8; k++) a[c][k] /= d;
        for (int r = 0; r < 4; r++) if (r != c) { const double f = a[r][c]; if (f != 0.0) for (int k = 0; k < 8; k++) a[r][k] -= f * a[c][k]; } }
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) o[r * 4 + c] = a[r][c + 4]; return true;
}
static bool CamFromScene(Cam& c, float& asp) {
    std::vector<float> S; if (!NamedCBRead("SceneCB", S) || S.size() < 16) return false;
    double M[16], I[16]; for (int i = 0; i < 16; i++) M[i] = S[i];
    if (!Inv4(M, I)) return false;
    auto un = [&](double x, double y, double z, double w, double* o) { double h[4]; for (int r = 0; r < 4; r++) h[r] = I[r * 4] * x + I[r * 4 + 1] * y + I[r * 4 + 2] * z + I[r * 4 + 3] * w;
        if (w == 0.0) { if (fabs(h[3]) < 1e-12) return false; } else if (fabs(h[3]) < 1e-12) return false; for (int k = 0; k < 3; k++) o[k] = h[k] / h[3]; return true; };
    double C[3], p0[3], pr[3], pu[3];
    if (!un(0, 0, 1, 0, C) || !un(0, 0, 0.5, 1, p0) || !un(1, 0, 0.5, 1, pr) || !un(0, 1, 0.5, 1, pu)) return false;
    double f[3] = { p0[0] - C[0], p0[1] - C[1], p0[2] - C[2] }; const double lf = sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]); if (lf < 1e-6 || lf > 1e6) return false;
    for (double& v : f) v /= lf;
    double vr[3] = { pr[0] - C[0], pr[1] - C[1], pr[2] - C[2] }, vu[3] = { pu[0] - C[0], pu[1] - C[1], pu[2] - C[2] };
    const double fr = vr[0] * f[0] + vr[1] * f[1] + vr[2] * f[2], fu = vu[0] * f[0] + vu[1] * f[1] + vu[2] * f[2]; if (fr <= 1e-6 || fu <= 1e-6) return false;
    double r[3], u[3]; for (int k = 0; k < 3; k++) { r[k] = vr[k] / fr - f[k]; u[k] = vu[k] / fu - f[k]; }     // en el plano a distancia 1
    const double tx = sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]), ty = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); if (tx < 1e-5 || ty < 1e-5 || ty > 50.0) return false;
    for (int k = 0; k < 3; k++) { c.pos[k] = (float)C[k]; c.f[k] = (float)f[k]; c.r[k] = (float)(r[k] / tx); c.u[k] = (float)(u[k] / ty); }
    c.th = (float)ty; asp = (float)(tx / ty);
    return asp > 0.3f && asp < 6.0f;
}
static ID3D11ShaderResourceView* GridSRV(ID3D11DeviceContext* ctx) {
    if (!g_dev.dev || !ctx) return nullptr;
    if (!s_tried) {
        s_tried = true;
        D3D11_TEXTURE2D_DESC td{}; td.Width = kW; td.Height = kH; td.MipLevels = td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DYNAMIC; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (SUCCEEDED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_tex)) && s_tex) g_dev.dev->CreateShaderResourceView(s_tex, nullptr, &s_srv);
        s_px.assign((size_t)kW * kH, 0xff000000u);
        Log("motor (grilla): textura de fondo %d x %d %s", kW, kH, s_srv ? "lista" : "NO se pudo crear: queda el fondo negro");
    }
    if (!s_srv) return nullptr;
    if (s_fresh.exchange(true)) return s_srv;
    // --- camara ---
    Cam c{}; float yaw = 3.14159f, pitch = 0.0f;
    if (!FreeCamGetPose(c.pos, &yaw, &pitch)) { c.pos[0] = 0; c.pos[1] = 0.95f; c.pos[2] = 6.5f; yaw = 3.14159f; pitch = 0.0f; }
    const float cp = cosf(pitch), sp = sinf(pitch);
    c.f[0] = cp * sinf(yaw); c.f[1] = sp; c.f[2] = cp * cosf(yaw);
    c.r[0] = -c.f[2]; c.r[1] = 0; c.r[2] = c.f[0]; { const float l = sqrtf(c.r[0] * c.r[0] + c.r[2] * c.r[2]); if (l > 1e-5f) { c.r[0] /= l; c.r[2] /= l; } else { c.r[0] = 1; c.r[2] = 0; } }
    c.u[0] = c.r[1] * c.f[2] - c.r[2] * c.f[1]; c.u[1] = c.r[2] * c.f[0] - c.r[0] * c.f[2]; c.u[2] = c.r[0] * c.f[1] - c.r[1] * c.f[0];
    c.th = tanf(0.5f * std::clamp(FreeCamFovNow(), 0.02f, 2.4f));
    s_asp = (g_bbW > 0 && g_bbH > 0) ? std::clamp((float)g_bbW / (float)g_bbH, 0.5f, 5.0f) : 16.0f / 9.0f;
    // (15/11 s, Agustin 19:28: "la grilla no sigue las lineas correctamente") con jugador cargado se usa la camara tal cual se le mando
    // al visor (freecam.cpp FreeCamWrittenCam): posicion, direccion y arriba reales, campo de vision ya suavizado y el aspecto con el
    // que el juego proyecta al jugador; asi la grilla y el jugador salen de la MISMA proyeccion
    {   float wp[3], wt[3], wu[3], wf = 0.0f, wa = 0.0f;
        if (FreeCamWrittenCam(wp, wt, wu, &wf, &wa)) {
            float f[3] = { wt[0] - wp[0], wt[1] - wp[1], wt[2] - wp[2] }; const float lf = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
            if (lf > 1e-5f) {
                for (int i = 0; i < 3; i++) f[i] /= lf;
                float r[3] = { f[1] * wu[2] - f[2] * wu[1], f[2] * wu[0] - f[0] * wu[2], f[0] * wu[1] - f[1] * wu[0] }; const float lr = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
                if (lr > 1e-5f) {
                    for (int i = 0; i < 3; i++) { r[i] /= lr; c.pos[i] = wp[i]; c.f[i] = f[i]; c.r[i] = r[i]; }
                    c.u[0] = r[1] * f[2] - r[2] * f[1]; c.u[1] = r[2] * f[0] - r[0] * f[2]; c.u[2] = r[0] * f[1] - r[1] * f[0];
                    c.th = tanf(0.5f * std::clamp(wf, 0.02f, 2.4f));
                    if (wa == wa && wa > 0.5f && wa < 5.0f) s_asp = wa;
                    yaw = atan2f(f[0], f[2]); pitch = asinf(std::clamp(f[1], -1.0f, 1.0f));
                    static int nl = 0; if (nl++ < 3) Log("motor (grilla): con la camara del visor: campo de vision %.4f, aspecto %.3f (pantalla %u x %u)", wf, s_asp, g_bbW, g_bbH);
                } } } }
    if (s_ovl) { Cam cs = c; float as = s_asp;     // jugadores en la escena principal: la camara con la que el juego dibujo esta escena
        if (CamFromScene(cs, as)) { c = cs; s_asp = as; yaw = atan2f(c.f[0], c.f[2]); pitch = asinf(std::clamp(c.f[1], -1.0f, 1.0f));
            static int nl = 0; if (nl++ < 3) Log("motor (grilla): camara sacada de la escena del juego: (%.2f, %.2f, %.2f) campo %.4f aspecto %.3f", c.pos[0], c.pos[1], c.pos[2], 2.0f * atanf(c.th), s_asp); }
        else { static int nl = 0; if (nl++ < 3) Log("motor (grilla): no pude sacar la camara de SceneCB: queda la del visor"); } }
    { std::lock_guard<std::mutex> lk(s_camMx); s_camLast = c; s_camAsp = s_asp; s_camT = Now(); }
    if (!s_ovl) MotorFeetPoll(ctx);
    if (!s_ovl && s_feetU >= 0.0f) {   // pies en la imagen -> rayo de la camara -> piso (y = 0)
        const float a = (s_feetU * 2.0f - 1.0f) * c.th * s_asp, b = (1.0f - s_feetV * 2.0f) * c.th;
        const float dir[3] = { c.f[0] + c.r[0] * a + c.u[0] * b, c.f[1] + c.r[1] * a + c.u[1] * b, c.f[2] + c.r[2] * a + c.u[2] * b };
        if (dir[1] < -1e-4f && c.pos[1] > 0.0f) { const float t = -c.pos[1] / dir[1]; const float gx = c.pos[0] + dir[0] * t, gz = c.pos[2] + dir[2] * t;
            if (fabsf(gx) < 50.0f && fabsf(gz) < 50.0f) {
                if (!s_shOk || fabsf(gx - s_shX) + fabsf(gz - s_shZ) > 0.01f) { static int nl = 0; if (nl++ < 6) Log("motor (sombra): pies del jugador en la imagen (%.3f, %.3f) -> piso (%.2f, %.2f) m", s_feetU, s_feetV, gx, gz); }
                s_shX = gx; s_shZ = gz; s_shOk = true; } } }
    // (15/11 o) si la camara y las opciones no cambiaron desde el ultimo dibujo, la textura ya esta bien: no se rehace
    {   struct Key { float p[3], yaw, pitch, th, cell, asp, u[3], sh[2], pj; int on, size, alpha, major, axes, ovl; };
        static Key s_last{}; static bool s_have = false; Key kk{}; memcpy(kk.p, c.pos, sizeof kk.p); kk.yaw = yaw; kk.pitch = pitch; kk.th = c.th; kk.cell = g_cfg.motorGridCell; kk.asp = s_asp; memcpy(kk.u, c.u, sizeof kk.u); kk.sh[0] = s_shX; kk.sh[1] = s_shZ;
        if (s_ovl) for (int i = 0; i < kMotorPjMax; i++) { float p[3], yw, sc; if (MotorPjGet(i, p, &yw, &sc)) kk.pj += (p[0] * 1.37f + p[1] * 2.11f + p[2] * 3.07f + sc) * (float)(i + 1); }   // las sombras siguen a los jugadores
        kk.on = g_cfg.motorGrid; kk.size = g_cfg.motorGridSize; kk.alpha = g_cfg.motorGridAlpha; kk.major = g_cfg.motorGridMajor; kk.axes = g_cfg.motorGridAxes; kk.ovl = s_ovl ? 1 : 0;
        if (s_have && memcmp(&kk, &s_last, sizeof kk) == 0) return s_srv;
        s_last = kk; s_have = true; }
    GridPaint(c);
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(ctx->Map(s_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)) && ms.pData) {
        for (int y = 0; y < kH; y++) memcpy((uint8_t*)ms.pData + (size_t)y * ms.RowPitch, &s_px[(size_t)y * kW], (size_t)kW * 4);
        ctx->Unmap(s_tex, 0);
    }
    return s_srv;
}

// ---- (16/11, Agustin 06:44: "pero no tiene profundidad el jugador") DIAGNOSTICO para meter la grilla en la escena 3D del visor:
// el jugador se pega como imagen encima del fondo con grilla. Para dibujar la grilla con la profundidad del jugador hace falta saber
// que profundidad (DSV) usa el visor, su formato y su borrado (0 = Z invertida). Dentro del Motor, cada 20 s (hasta 4 veces) se mira
// medio segundo: cada combinacion imagen de destino + profundidad que el juego pone (tamano, formato, si es la imagen del jugador que
// despues lee el menu) y con que valor se borra cada profundidad. Solo anota; no cambia nada.
static std::mutex s_ddMx; static std::atomic<bool> s_ddOn{false}; static double s_ddT0 = 0.0; static int s_ddDumps = 0;
struct DdKey { uint32_t rw, rh, rf, dw, dh, df; uintptr_t rres, dres; bool operator<(const DdKey& o) const { return memcmp(this, &o, sizeof *this) < 0; } };
static std::map<DdKey, int> s_ddRt; static std::map<uintptr_t, float> s_ddClear; static std::atomic<uintptr_t> s_ddPlayerImg{0};
void MotorDepthPlayerImage(void* res) { s_ddPlayerImg.store((uintptr_t)res, std::memory_order_relaxed); }
static void DdDesc(ID3D11View* v, uint32_t& w, uint32_t& h, uint32_t& f, uintptr_t& res) {
    w = h = f = 0; res = 0; if (!v) return;
    ID3D11Resource* r = nullptr; v->GetResource(&r); if (!r) return; res = (uintptr_t)r;
    ID3D11Texture2D* t = nullptr; r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t); r->Release();
    if (t) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); w = d.Width; h = d.Height; f = (uint32_t)d.Format; t->Release(); }
}
static std::atomic<bool> s_feetOn{false};                 // Motor abierto (lo pone MotorDepthDiagTick en cada Present)
static ID3D11Resource* s_feetDepth = nullptr; static std::mutex s_feetMx;   // profundidad con la que se dibuja la imagen del jugador
static ID3D11Texture2D* s_feetStage = nullptr; static D3D11_TEXTURE2D_DESC s_feetDesc{}; static bool s_feetPend = false; static double s_feetT = 0.0;
static void FeetNoteRT(ID3D11RenderTargetView* rv, ID3D11DepthStencilView* d) {
    const uintptr_t pi = s_ddPlayerImg.load(std::memory_order_relaxed); if (!pi || !rv || !d) return;
    ID3D11Resource* r = nullptr; rv->GetResource(&r); if (!r) return; const bool mine = (uintptr_t)r == pi; r->Release(); if (!mine) return;
    ID3D11Resource* dr = nullptr; d->GetResource(&dr); if (!dr) return;
    std::lock_guard<std::mutex> lk(s_feetMx);
    if (dr == s_feetDepth) { dr->Release(); return; }
    if (s_feetDepth) s_feetDepth->Release(); s_feetDepth = dr;   // queda con su referencia
    if (s_feetStage) { s_feetStage->Release(); s_feetStage = nullptr; } s_feetPend = false;
    Log("motor (sombra): profundidad del jugador %p", (void*)dr);
}
// cada 0.2 s: copia la profundidad del jugador a una textura de lectura y, uno o mas cuadros despues (sin esperar a la placa), busca
// el punto mas bajo del jugador: el promedio de x de lo que hay en el 3 % de abajo de su caja
static void MotorFeetPoll(ID3D11DeviceContext* ctx) {
    if (!ctx || !g_dev.dev) return;
    std::lock_guard<std::mutex> lk(s_feetMx); if (!s_feetDepth) return;
    const double now = Now();
    if (!s_feetStage) {
        ID3D11Texture2D* t = nullptr; s_feetDepth->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t); if (!t) return;
        D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); t->Release();
        if (d.SampleDesc.Count != 1 || (d.Format != DXGI_FORMAT_R32G8X24_TYPELESS && d.Format != DXGI_FORMAT_R32_TYPELESS) || d.Width < 64 || d.Height < 64) {
            static int nl = 0; if (nl++ < 2) Log("motor (sombra): profundidad %ux%u fmt %d muestras %u: no se lee", d.Width, d.Height, (int)d.Format, d.SampleDesc.Count);
            s_feetDepth->Release(); s_feetDepth = nullptr; return; }
        d.MipLevels = 1; d.ArraySize = 1; d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
        if (FAILED(g_dev.dev->CreateTexture2D(&d, nullptr, &s_feetStage)) || !s_feetStage) { s_feetStage = nullptr; static int nl = 0; if (nl++ < 2) Log("motor (sombra): no se pudo crear la copia de lectura"); return; }
        s_feetDesc = d;
    }
    if (!s_feetPend) { if (now - s_feetT < 0.2) return; ctx->CopyResource(s_feetStage, s_feetDepth); s_feetPend = true; s_feetT = now; return; }
    D3D11_MAPPED_SUBRESOURCE ms{};
    const HRESULT hr = ctx->Map(s_feetStage, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &ms);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return;
    s_feetPend = false; if (FAILED(hr) || !ms.pData) return;
    const UINT W = s_feetDesc.Width, H = s_feetDesc.Height, bpp = s_feetDesc.Format == DXGI_FORMAT_R32_TYPELESS ? 4 : 8;
    int x0 = (int)W, x1 = -1, y0 = (int)H, y1 = -1; long cnt = 0, tot = 0;
    for (UINT y = 0; y < H; y += 2) { const uint8_t* row = (const uint8_t*)ms.pData + (size_t)y * ms.RowPitch;
        for (UINT x = 0; x < W; x += 2) { float z; memcpy(&z, row + (size_t)x * bpp, 4); tot++;
            if (z > 0.0f && z == z) { cnt++; if ((int)x < x0) x0 = (int)x; if ((int)x > x1) x1 = (int)x; if ((int)y < y0) y0 = (int)y; if ((int)y > y1) y1 = (int)y; } } }
    float fu = -1.0f, fv = -1.0f;
    if (cnt > 50 && cnt < tot * 2 / 5 && y1 - y0 > 20) {   // si casi todo tiene profundidad hay piso u otra cosa: no sirve
        const int band = std::max(4, (y1 - y0) * 3 / 100); double sx = 0; long n = 0;
        for (int y = std::max(y0, y1 - band); y <= y1; y += 2) { const uint8_t* row = (const uint8_t*)ms.pData + (size_t)y * ms.RowPitch;
            for (int x = x0; x <= x1; x += 2) { float z; memcpy(&z, row + (size_t)x * bpp, 4); if (z > 0.0f && z == z) { sx += x; n++; } } }
        if (n > 0) { fu = (float)(sx / n + 0.5) / (float)W; fv = (float)(y1 + 1) / (float)H; }
    }
    ctx->Unmap(s_feetStage, 0);
    { static int nl = 0; if (nl++ < 4) Log("motor (sombra): profundidad leida: %ld de %ld puntos con jugador, caja x %d..%d y %d..%d -> pies %s", cnt, tot, x0, x1, y0, y1, fu >= 0.0f ? "si" : "NO (se deja la sombra donde estaba)"); }
    if (fu >= 0.0f) { s_feetU = fu; s_feetV = fv; }
}
void MotorDepthDiagRT(UINT n, ID3D11RenderTargetView* const* v, ID3D11DepthStencilView* d) {
    if (s_feetOn.load(std::memory_order_relaxed) && n > 0 && v && d) FeetNoteRT(v[0], d);
    if (!s_ddOn.load(std::memory_order_relaxed) || !d) return;
    DdKey k{}; DdDesc(n > 0 && v ? v[0] : nullptr, k.rw, k.rh, k.rf, k.rres); DdDesc(d, k.dw, k.dh, k.df, k.dres);
    if (k.dw < 512) return;
    std::lock_guard<std::mutex> lk(s_ddMx); s_ddRt[k]++;
}
void MotorDepthDiagClear(ID3D11DepthStencilView* d, float depth) {
    if (!s_ddOn.load(std::memory_order_relaxed) || !d) return;
    uint32_t w, h, f; uintptr_t res; DdDesc(d, w, h, f, res); if (w < 512) return;
    std::lock_guard<std::mutex> lk(s_ddMx); s_ddClear[res] = depth;
}
static void MotorDepthDiagTick() {
    const double now = Now();
    s_feetOn.store(MotorPageActive(), std::memory_order_relaxed);
    if (!MotorPageActive() || s_ddDumps >= 4) { s_ddOn.store(false); s_ddT0 = 0.0; return; }
    if (s_ddT0 == 0.0) { s_ddT0 = now; return; }
    if (!s_ddOn.load() && now - s_ddT0 > 20.0) { std::lock_guard<std::mutex> lk(s_ddMx); s_ddRt.clear(); s_ddClear.clear(); s_ddOn.store(true); s_ddT0 = now; return; }
    if (s_ddOn.load() && now - s_ddT0 > 0.5) {
        s_ddOn.store(false); s_ddT0 = now; s_ddDumps++;
        std::lock_guard<std::mutex> lk(s_ddMx); const uintptr_t pi = s_ddPlayerImg.load();
        Log("motor (profundidad %d): %d combinaciones imagen + profundidad en 0.5 s (imagen del jugador del menu %p)", s_ddDumps, (int)s_ddRt.size(), (void*)pi);
        int i = 0;
        for (const auto& e : s_ddRt) { if (i++ >= 24) break; const DdKey& k = e.first; auto c = s_ddClear.find(k.dres);
            Log("motor (profundidad %d):  imagen %ux%u fmt %u %p%s | profundidad %ux%u fmt %u %p borrada a %s | %d veces", s_ddDumps, k.rw, k.rh, k.rf, (void*)k.rres, k.rres && k.rres == pi ? " (LA DEL JUGADOR)" : "",
                k.dw, k.dh, k.df, (void*)k.dres, c == s_ddClear.end() ? "-" : (c->second < 0.5f ? "0 (Z invertida)" : "1"), e.second); }
    }
}

#endif
} // namespace fx
