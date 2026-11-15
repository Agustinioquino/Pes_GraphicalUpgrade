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

} // namespace fx
