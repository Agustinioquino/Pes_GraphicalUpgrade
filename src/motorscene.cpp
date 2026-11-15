// (15/11 3icxbh) Motor en tiempo real: lectura de la escena grafica del motor para el editor (luces y conteo de objetos).
// Agustin 15/11 02:41: "que este motor en la pantalla pueda leer todo tipos de luces. y sistemas del juego".
//
// Es la misma lectura que ya hace (y se probo en juego el 11/11, 29 luces) el modulo de Sider MotorTiempoReal.lua:
//   lista global de escenas graficas DAT_14374d958: nodo = escena { +0 siguiente, +0x28 cantidad de listas, +0x30 arreglo de
//   punteros a lista POR TIPO }, lista = { +0 primero, +8 ultimo, +0x10 cuenta }, objeto (fox::gr::SceneObject) = { +0 tabla,
//   +0x18 siguiente, +0x24 flags (bit0 enlazado), +0x28 escena }.
//   Luz (fox::gr::LightSource): +0x38 bloque de datos P, +0x50 nombre (fox::String: [[luz+0x50]] = texto), +0x84 alcance
//   (focos, puntuales, locales); P: +0x40 color RGBA lineal x intensidad, +0x64 flags (bit0 PRENDIDA, bit4 sombra),
//   +0x70 posicion (locales) o direccion (sol).
// Prender / apagar = el bit0 de P+0x64 (dato; asi lo hace el modulo de Sider, probado en juego). Nada de esto llama al juego.
// Todo se lee con memoria comprobada y dentro de __try: si el juego cambia las listas justo en ese momento se descarta la pasada.
#include "core.h"
#include <cstring>
#include <cmath>

namespace fx {

static const uintptr_t kScenes = 0x14374d958ull, kExe0 = 0x140000000ull, kExe1 = 0x148000000ull;
static const uintptr_t kVtModel = 0x142a0a880ull;
static const uintptr_t kVtLight[5] = { 0x142a0ad48ull, 0x142a0add8ull, 0x142a0ae68ull, 0x142a0aef8ull, 0x142a0af88ull };   // Luz, Sol, Local, Punto, Foco
static const uintptr_t kVtData[5] = { 0x142a226a0ull, 0x142a226b0ull, 0x142a226c0ull, 0x142a226d0ull, 0x142a226e0ull };

static inline bool PtrOk(uintptr_t a) { return a >= 0x10000ull && a < 0x7ffffffff000ull && (a & 7) == 0; }
static inline bool Heap(uintptr_t a) { return PtrOk(a) && !(a >= kExe0 && a < kExe1); }
static int LightKind(uintptr_t vt) { for (int i = 0; i < 5; i++) if (vt == kVtLight[i]) return i; return -1; }
static bool DataVt(uintptr_t vt) { for (int i = 0; i < 5; i++) if (vt == kVtData[i]) return true; return false; }

// una pasada; devuelve la cantidad de luces o -1 si hubo que descartarla. Solo datos simples (para poder usar __try)
static int Walk(MotorLight* out, int max, MotorSceneInfo* info) {
    int n = 0; MotorSceneInfo si{};
    __try {
        uintptr_t sc = *(uintptr_t*)kScenes;
        for (int s = 0; s < 16 && Heap(sc); s++, sc = *(uintptr_t*)sc) {
            si.scenes++;
            const uint32_t nl = *(uint32_t*)(sc + 0x28); const uintptr_t arr = *(uintptr_t*)(sc + 0x30);
            if (!Heap(arr) || nl > 256) continue;
            for (uint32_t t = 0; t < nl; t++) {
                const uintptr_t lst = *(uintptr_t*)(arr + 8ull * t); if (!Heap(lst)) continue;
                uintptr_t o = *(uintptr_t*)lst; const uint32_t cnt = *(uint32_t*)(lst + 0x10);
                for (uint32_t i = 0; i < cnt && i < 4096 && Heap(o); i++, o = *(uintptr_t*)(o + 0x18)) {
                    const uintptr_t vt = *(uintptr_t*)o; si.objects++;
                    if (vt == kVtModel) { si.models++; continue; }
                    const int kind = LightKind(vt); if (kind < 0) continue;
                    si.lights++;
                    if (n >= max) continue;
                    const uintptr_t p = *(uintptr_t*)(o + 0x38); if (!Heap(p)) continue;
                    MotorLight& L = out[n]; memset(&L, 0, sizeof L);
                    L.obj = o; L.data = p; L.kind = kind; L.scene = s; L.writable = DataVt(*(uintptr_t*)p);
                    memcpy(L.color, (void*)(p + 0x40), 16); const uint32_t fl = *(uint32_t*)(p + 0x64);
                    L.on = (fl & 1u) != 0; L.shadow = (fl & 0x10u) != 0; memcpy(L.pos, (void*)(p + 0x70), 12);
                    if (kind >= 2) L.range = *(float*)(o + 0x84);
                    const uintptr_t nm = *(uintptr_t*)(o + 0x50); const uintptr_t tx = Heap(nm) ? *(uintptr_t*)nm : 0;
                    int q = 0;
                    if (tx >= 0x10000ull && tx < 0x7ffffffff000ull) for (; q < 39; q++) { const unsigned char ch = *(const unsigned char*)(tx + q); if (ch < 0x20 || ch > 0x7e) break; L.name[q] = (char)ch; }
                    L.name[q] = 0;
                    n++;
                }
            }
        }
    } __except (1) { return -1; }
    if (info) *info = si;
    return n;
}

int MotorSceneLights(MotorLight* out, int max, MotorSceneInfo* info) {
    if (!out || max <= 0) return 0;
    int n = Walk(out, max, info);
    if (n < 0) n = Walk(out, max, info);          // la lista cambio mientras se leia: una vez mas
    static int logged = 0;
    if (n < 0) { if (logged++ < 5) Log("motor (escena): la lectura de la escena grafica fallo dos veces seguidas (se reintenta sola)"); return -1; }
    return n;
}

// prende / apaga una luz ya leida: vuelve a comprobar que sigue siendo la misma antes de escribir el bit
static bool SetOn(uintptr_t obj, uintptr_t data, int kind, bool on) {
    __try {
        if (kind < 0 || kind > 4 || !Heap(obj) || !Heap(data)) return false;
        if (*(uintptr_t*)obj != kVtLight[kind] || (*(uint32_t*)(obj + 0x24) & 1u) == 0 || *(uintptr_t*)(obj + 0x38) != data || !DataVt(*(uintptr_t*)data)) return false;
        uint32_t* f = (uint32_t*)(data + 0x64); const uint32_t v = *f, w = on ? (v | 1u) : (v & ~1u);
        if (w != v) *f = w;
        return true;
    } __except (1) { return false; }
}
bool MotorLightSetOn(const MotorLight& L, bool on) {
    const bool ok = SetOn(L.obj, L.data, L.kind, on);
    static int n = 0; if (n++ < 60) Log("motor (escena): luz \"%s\" %s%s", L.name[0] ? L.name : "(sin nombre)", on ? "prendida" : "apagada", ok ? "" : " -> NO se pudo (ya no esta en la escena)");
    return ok;
}

// ---------------------------------------------------------------- (15/11 o) inspector editable
// Agustin 05:16: "esta parte deberia dejar modificar variables". Se usan los MISMOS setters del motor que usa el juego al leer
// una escena (llamadores en c_1417a3960.c 79020 / 79213 / 101034 / 101296), con el comienzo de cada funcion comprobado:
//   color (todas)  FUN_14169f2e0(luz, float4*)  escribe P+0x40 y llama la ranura +0x78 (la actualizacion de la clase).
//                  Es la misma que llama el modulo de Sider (275 veces seguidas desde otro hilo en la prueba del 11/11, sin cierre).
//   sombra         bit 0x10 de P+0x64 (FUN_14169f7f0 hace solo eso: dato)
//   alcance Punto  FUN_14169f570(luz, r)  [luz+0x84] = r, P+0xb4 = r, atenuacion FUN_14171dd60, ranura +0x78, limites FUN_1416a0900
//   alcance Foco   FUN_14169f5d0(luz, r)  [luz+0x84] = r, P+0xe8 = r, atenuacion FUN_14171dde0, ranura +0x78, limites FUN_1416a09a0
//   posicion Punto FUN_14169f680(luz, float4*) P+0x70 y limites (esfera)   Foco FUN_14169f6d0 (cono)   Sol FUN_14169f3c0 (direccion)
// Que Punto = f570 / f680 y Foco = f5d0 / f6d0 es INFERIDO de los limites que arman (esfera alrededor del punto con el alcance, y
// cono con el giro de la luz +0x90) y del tamano del bloque P. Los argumentos float4 se leen con movaps: van alineados a 16.
// La luz "Local" y la base no tienen alcance ni posicion editables (no conozco su setter): quedan de solo lectura.
static const uintptr_t kFnColor = 0x14169f2e0ull, kFnRangeP = 0x14169f570ull, kFnRangeS = 0x14169f5d0ull,
                       kFnPosP = 0x14169f680ull, kFnPosS = 0x14169f6d0ull, kFnDirSun = 0x14169f3c0ull;
static bool SigOk(uintptr_t fn, const char* hex) {      // compara el comienzo de la funcion con lo leido en la base de Ghidra
    unsigned char want[24]; int n = 0;
    for (const char* p = hex; p[0] && p[1] && n < 24; p += 2) { auto h = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; }; want[n++] = (unsigned char)(h(p[0]) * 16 + h(p[1])); }
    __try { return memcmp((const void*)fn, want, (size_t)n) == 0; } __except (1) { return false; }
}
static int FnOk(int i) {      // 0 color, 1 alcance punto, 2 alcance foco, 3 pos punto, 4 pos foco, 5 direccion sol: 1 = firma buena
    static int s_ok[6] = { -1, -1, -1, -1, -1, -1 };
    if (s_ok[i] < 0) {
        static const uintptr_t fn[6] = { kFnColor, kFnRangeP, kFnRangeS, kFnPosP, kFnPosS, kFnDirSun };
        static const char* const sig[6] = { "488b41380f28020f294040488b0148ff6078", "40534883ec20488b4138488bd9f30f118984000000f30f1188b4000000",
            "40534883ec20488b4138488bd9f30f118984000000f30f1188e8000000", "0f28024c8b41380f280da2a6f0000f59c8410f294070",
            "0f28024c8b41380f280d52a6f0000f59c8410f294070", "0f28024c8b41380f280d62a9f0000f59c8410f294070" };
        static const char* const nm[6] = { "color", "alcance del punto", "alcance del foco", "posicion del punto", "posicion del foco", "direccion del sol" };
        s_ok[i] = SigOk(fn[i], sig[i]) ? 1 : 0;
        Log("motor (escena): funcion de %s 0x%llx %s", nm[i], (unsigned long long)fn[i], s_ok[i] ? "reconocida" : "NO es la que espero: ese dato queda de solo lectura");
    }
    return s_ok[i];
}
// sigue siendo la misma luz, enlazada en su escena, con su bloque de datos conocido y (si la clase lo tiene) su bloque de dibujo
static bool Alive(uintptr_t obj, uintptr_t data, int kind) {
    if (kind < 0 || kind > 4 || !Heap(obj) || !Heap(data)) return false;
    if (*(uintptr_t*)obj != kVtLight[kind] || (*(uint32_t*)(obj + 0x24) & 1u) == 0 || *(uintptr_t*)(obj + 0x38) != data || !DataVt(*(uintptr_t*)data)) return false;
    if (kind == 4 && !Heap(*(uintptr_t*)(obj + 0xd8))) return false;                       // foco: bloque de dibujo [+0xd8]
    if ((kind == 1 || kind == 3) && !Heap(*(uintptr_t*)(obj + 0x90))) return false;        // sol / punto: bloque [+0x90]
    return true;
}
bool MotorLightCan(const MotorLight& L, int what) {
    if (!L.writable) return false;
    switch (what) {
    case 0: return FnOk(0) == 1;
    case 1: return true;
    case 2: return (L.kind == 3 && FnOk(1) == 1) || (L.kind == 4 && FnOk(2) == 1);
    case 3: return (L.kind == 3 && FnOk(3) == 1) || (L.kind == 4 && FnOk(4) == 1) || (L.kind == 1 && FnOk(5) == 1);
    }
    return false;
}
typedef void(__fastcall* PFN_V4)(uintptr_t, const float*); typedef void(__fastcall* PFN_F)(uintptr_t, float);
static bool CallV4(uintptr_t fn, uintptr_t obj, uintptr_t data, int kind, const float* v) {
    alignas(16) float a[4] = { v[0], v[1], v[2], v[3] };
    __try { if (!Alive(obj, data, kind)) return false; ((PFN_V4)fn)(obj, a); return true; } __except (1) { return false; }
}
static bool CallF(uintptr_t fn, uintptr_t obj, uintptr_t data, int kind, float v) {
    __try { if (!Alive(obj, data, kind)) return false; ((PFN_F)fn)(obj, v); return true; } __except (1) { return false; }
}
static bool ReadF(uintptr_t a, float* out) { __try { if (!Heap(a & ~(uintptr_t)7)) return false; *out = *(const float*)a; return true; } __except (1) { return false; } }
static bool Finite(float v, float lim) { return v == v && v > -lim && v < lim; }
bool MotorLightSetColor(const MotorLight& L, const float* c) {
    if (!MotorLightCan(L, 0)) return false;
    for (int i = 0; i < 4; i++) if (!Finite(c[i], 1.0e7f)) return false;
    return CallV4(kFnColor, L.obj, L.data, L.kind, c);
}
bool MotorLightSetShadow(const MotorLight& L, bool on) {
    __try {
        if (!L.writable || !Alive(L.obj, L.data, L.kind)) return false;
        uint32_t* f = (uint32_t*)(L.data + 0x64); const uint32_t v = *f, w = on ? (v | 0x10u) : (v & ~0x10u);
        if (w != v) *f = w;
        return true;
    } __except (1) { return false; }
}
bool MotorLightSetRange(const MotorLight& L, float r) {
    if (!MotorLightCan(L, 2) || !Finite(r, 100000.0f) || r < 0.01f) return false;
    return CallF(L.kind == 3 ? kFnRangeP : kFnRangeS, L.obj, L.data, L.kind, r);
}
bool MotorLightSetPos(const MotorLight& L, const float* p) {
    if (!MotorLightCan(L, 3)) return false;
    for (int i = 0; i < 3; i++) if (!Finite(p[i], 100000.0f)) return false;
    float w = 1.0f; if (!ReadF(L.data + 0x7c, &w)) return false;                       // la cuarta componente queda como estaba
    float v[4] = { p[0], p[1], p[2], w };
    if (L.kind == 1) { const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l < 1.0e-4f) return false; v[0] /= l; v[1] /= l; v[2] /= l; }   // sol: direccion unitaria
    return CallV4(L.kind == 3 ? kFnPosP : L.kind == 4 ? kFnPosS : kFnDirSun, L.obj, L.data, L.kind, v);
}

} // namespace fx
