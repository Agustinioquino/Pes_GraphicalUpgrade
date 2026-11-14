// PES21 FX (13/11 sk9ema) - REDES DE LOS ARCOS con el viento unico y mas vida en el gol. Agustin: "de paso que estudias las redes
// podrias mejorar su efecto cuando hay un gol, y el aspecto visual", "su reactividad cuando no pasa nada en el partido".
// Lectura del exe (PES2021.exe 1.07.02; C reconstruido en decompilado/hallazgos/20261113_redes_viento_exe.cpp.md):
//  * cobra::rd::impl::SceneGoalnetNodeImpl, entrada 14 de la vtable (0x141a38d60), una vez por cuadro y por red:
//    si el nodo esta cargado (+0x80 == 7), tiene simulador (+0x190) y malla (+0x198) y el simulador esta ACTIVO (+0x31),
//    lo avanza (0x1419a4320) y copia los puntos a la malla (0x1419a4540). Si no, no hace nada.
//  * el simulador (0x1419a4320) da pasos fijos de 1/60 s; en cada paso: 0x1419e3e60 integra cada punto suelto con
//    0x1419e3d40:  pos += (aceleracion * dt + 0.95 * velocidad) * dt, con aceleracion = (0, -6.0, 0); el 0 de x / z ESTA EN EL
//    CODIGO (ceros armados con xorps: no hay viento); si el punto lo tiro la pelota la velocidad se multiplica por 1.0 (con
//    reactive_nets_physics.lua: 0.99, 0.25 y 0.3);
//    piso en y = 0.03. Despues 2 pasadas (0x143524d04) de restricciones de distancia (XPBD, 0x1419e46d0) + choque con la
//    pelota (radio 0.11) + estiramiento maximo 1.2 (0x143524d00).
//  * se DUERME sola: si en 20 cuadros seguidos casi no se movio (0x1419e3a00), el simulador queda inactivo hasta que la
//    pelota la toca o el arco se mueve (0x1419a4190 prende +0xf0 y +0x31, y 0x1419a3330 la despierta con eso). Por eso "cuando
//    no pasa nada" la red esta muerta.
// La DLL (solo con ese exe y esas firmas):
//  1) engancha 0x1419e3d40 (cada punto suelto, cada paso): despues del juego suma el empuje del viento unico (filas 494 / 495)
//     a la posicion: a = k * v * |v| en el rumbo, con frentes de racha que viajan con el aire (w494) y un poco de aleteo vertical.
//  2) engancha la entrada 14 del nodo: con viento, si la red se durmio la despierta por el mismo camino del juego (+0xf0, +0x31).
//  3) la fisica de la red (peso, rebote, pasadas, estiramiento, hilos) la cambia el modulo de Sider PES21FX_redes.lua
//     (presets juego / reactiva / viva / tv / personal). La DLL solo lee el peso actual para escalar el viento.
// Filas: 499 x viento en la red (0 = nada de esto; 1 = natural), y / z / w libres;
//        500 x viento bajo techo (0 auto, 1 siempre afuera), y viento que queda bajo techo, z / w libres
//        (500 x / y los usa wind.cpp: el viento bajo techo vale para todo, no solo la red).
#include "core.h"
#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace fx {
namespace {
const unsigned kStamp = 0x62787C6A;
const uintptr_t kPointStep = 0x1419e3d40ull;   // integrar un punto de la red
const uintptr_t kNodeVf14 = 0x141a38d60ull;    // SceneGoalnetNodeImpl vf14: paso de la red por cuadro
// 0x1419e3d40: push rbx; sub rsp,70h; movaps [rsp+50h],xmm7; mov rax,[rip+..]   +0x1a: cmp byte [rcx+5Ch],0
const unsigned char kPointSig[14] = { 0x40,0x53,0x48,0x83,0xec,0x70,0x0f,0x29,0x7c,0x24,0x50,0x48,0x8b,0x05 };
const unsigned char kPointSig2[4] = { 0x80,0x79,0x5c,0x00 };
// 0x141a38d60: push rbx; sub rsp,20h; cmp dword [rcx+80h],7; mov rbx,rcx
const unsigned char kNodeSig[16] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x83,0xb9,0x80,0x00,0x00,0x00,0x07,0x48,0x8b,0xd9 };
// constantes que solo lee la red (grep del decompilado: una funcion cada una)
// (13/11 16:35) OJO: el juego SOLO trae 0.95 / 6.0 / 1.0 en el bloque 0x143525554; el mod reactive_nets_physics.lua de Agustin
// (Godmode_ON / DevPlays, lo escribe una vez al iniciar) los cambia a 0.99 / 0.25 / 0.3 y ajusta la rigidez (0x143525590..5ac).
// Por eso la DLL NO supone valores: al engancharse lee los que hay (los del juego o los de su mod) y esos son la base; apagado
// vuelve a esa base. Pasadas y estiramiento (0x143524d00 / d04) no los toca ese mod.
const uintptr_t kGravity = 0x143525558ull;     // juego 6.0, reactive_nets 0.25  (0x1419e3d40)
const uintptr_t kHitKeep = 0x14352555cull;     // juego 1.0, reactive_nets 0.3   (0x1419e3d40, punto tirado por la pelota)
const uintptr_t kIters = 0x143524d04ull;       // 2     (0x1419a4320)
const uintptr_t kStretch = 0x143524d00ull;     // 1.2   (0x1419a2700)
float bG = 6.0f, bH = 1.0f, bS = 1.2f; unsigned bI = 2;   // base leida al engancharse

typedef void (__fastcall* PointStepFn)(void* pt, void* acc, float dt, uintptr_t a4);   // a4: 0x1419e3e60 pasa un cuarto valor en r9 (sin uso visible): se reenvia igual
typedef void (__fastcall* NodeFn)(void* node);
PointStepFn o_point = nullptr; NodeFn o_node = nullptr;
bool s_tried = false, s_ok = false, s_constOk = false;
std::atomic<int> s_pushed{0}, s_wakes{0}, s_nodes{0};

bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (const char*)p + n <= (const char*)mi.BaseAddress + mi.RegionSize;
}
bool SigAt(uintptr_t a, const unsigned char* s, size_t n) { return Readable((const void*)a, n) && memcmp((const void*)a, s, n) == 0; }
// viento de este cuadro (lo arma RedesPresent, lo leen los ganchos en el hilo del juego)
struct NetWind { float ax, az, ay, dx, dz, D, turb, on, up; WindHeight hp; };   // (14/11) up = empuje desde abajo, hp = perfil por altura
NetWind s_w{};

void __fastcall hk_Point(void* pt, void* acc, float dt, uintptr_t a4) {
    o_point(pt, acc, dt, a4);
    const NetWind w = s_w;
    if (w.on < 0.5f || dt <= 0.0f || *((const char*)pt + 0x5c) != 0) return;   // punto fijo (atado al arco)
    const uintptr_t node = *(const uintptr_t*)pt; if (!node) return;
    const unsigned idx = *(const unsigned*)(node + 0x94);
    float* pos = (float*)(node + ((uintptr_t)idx + 5) * 0x10);   // posicion actual (0x1419e2a30)
    // frente de racha que viaja con el aire: el empuje sube y baja a lo largo del rumbo, y llega antes a lo que esta viento arriba
    const float s = pos[0] * w.dx + pos[2] * w.dz - w.D;
    const float g = 1.0f + (0.25f + 0.35f * w.turb) * sinf(0.9f * s) + 0.12f * sinf(2.3f * s + pos[0] * 1.7f + pos[2] * 1.3f);
    const float d2 = dt * dt;
    // (14/11) perfil por altura (fila 508): abajo de la red sopla menos que arriba; la fuerza va con el cuadrado de la velocidad
    const float hk = WindHeightK(w.hp, pos[1] > 0.03f ? pos[1] : 0.03f), hk2 = hk * hk;
    pos[0] += w.ax * g * hk2 * d2; pos[2] += w.az * g * hk2 * d2;
    pos[1] += (w.ay * sinf(1.7f * s + pos[1] * 3.0f) * hk2 + w.up) * d2;
    if (pos[1] < 0.03f) pos[1] = 0.03f;   // el mismo piso del juego
    s_pushed.fetch_add(1, std::memory_order_relaxed);
}

void __fastcall hk_Node(void* nodeP) {
    const uintptr_t node = (uintptr_t)nodeP;
    if (s_w.on > 0.5f && *(const int*)(node + 0x80) == 7) {
        const uintptr_t S = *(const uintptr_t*)(node + 0x190);
        if (S && *(const uintptr_t*)(node + 0x198)) {
            const uintptr_t L = *(const uintptr_t*)(S + 0x28);
            // dormida (simulador inactivo o puntos quietos): el mismo despertar que usa el juego cuando se mueve el arco
            if (L && (*(const char*)(S + 0x31) == 0 || *(const char*)(L + 0x31) != 0)) {
                *(char*)(S + 0xf0) = 1; *(char*)(S + 0x31) = 1; s_wakes.fetch_add(1, std::memory_order_relaxed);
            }
            s_nodes.fetch_add(1, std::memory_order_relaxed);
        }
    }
    o_node(nodeP);
}

void Install() {
    s_tried = true;
    if (!g_cfg.motorPatch) { Log("redes: parches en memoria apagados (motor = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("redes: exe distinto (sello %08X): sin ganchos", (unsigned)nt->FileHeader.TimeDateStamp); return; }
    if (!SigAt(kPointStep, kPointSig, sizeof kPointSig) || !SigAt(kPointStep + 0x1a, kPointSig2, sizeof kPointSig2) || !SigAt(kNodeVf14, kNodeSig, sizeof kNodeSig)) {
        Log("redes: la firma de 0x1419e3d40 / 0x141a38d60 no coincide, se omite"); return; }
    if (MH_CreateHook((void*)kPointStep, (void*)&hk_Point, (void**)&o_point) != MH_OK || MH_CreateHook((void*)kNodeVf14, (void*)&hk_Node, (void**)&o_node) != MH_OK) {
        Log("redes: no se pudo enganchar"); return; }
    MH_EnableHook((void*)kPointStep); MH_EnableHook((void*)kNodeVf14); s_ok = true;
    if (Readable((void*)kGravity, 8) && Readable((void*)kStretch, 8)) {
        bG = *(const float*)kGravity; bH = *(const float*)kHitKeep; bI = *(const unsigned*)kIters; bS = *(const float*)kStretch;
        s_constOk = bG >= 0.05f && bG <= 20.0f && bH >= 0.0f && bH <= 1.5f && bI >= 1 && bI <= 8 && bS >= 1.0f && bS <= 3.0f;
    }
    Log("redes: enganchadas (viento en cada punto, despertar con viento); base de la red: peso %.3f, rebote %.3f, pasadas %u, estiramiento %.3f (%s)%s",
        bG, bH, bI, bS, bG == 6.0f && bH == 1.0f ? "las del juego" : bG == 0.25f && bH == 0.3f ? "las de reactive_nets_physics.lua" : "de otro mod",
        s_constOk ? "" : "; fuera de rango: sin red con mas vida");
}
} // namespace

void RedesPresent() {
    if (!s_tried) Install();
    if (!s_ok) return;
    const float (*p)[4] = g_cfg.live;
    // 1) viento de la red: fila 494 (ya trae rachas, hora, estadio y bajo techo)
    const float k = std::clamp(p[499][0], 0.0f, 3.0f), v = p[494][2];
    NetWind w{};
    if (k > 0.0f && v > 0.0f) {
        // empuje en proporcion al peso BASE de la red (el del juego 6.0 o el de su mod 0.25): asi el viento la mueve igual de
        // "en serio" con o sin reactive_nets. Red real: arrastre / peso ~ 0.015 v2 -> 5 m/s = 0.37 del peso, 10 m/s = 1.5
        const float gNow = s_constOk ? *(const volatile float*)kGravity : bG;   // el peso que haya ahora (preset del Lua)
        const float a = 0.015f * k * v * v * std::clamp(gNow, 0.1f, 10.0f);
        w.ax = a * p[494][0]; w.az = a * p[494][1]; w.ay = 0.15f * a; w.dx = p[494][0]; w.dz = p[494][1]; w.D = p[494][3];
        w.turb = std::clamp(p[495][1], 0.0f, 1.0f); w.on = v * sqrtf(k) > 0.3f ? 1.0f : 0.0f;
        // (14/11) viento desde abajo (x511, solo "Probar el viento" por ahora): mismo arrastre, hacia arriba
        const float u = p[511][0]; w.up = 0.015f * k * u * fabsf(u) * std::clamp(gNow, 0.1f, 10.0f);
        if (fabsf(u) * sqrtf(k) > 0.3f) w.on = 1.0f;
        w.hp = g_windHeight;
    }
    s_w = w;
    // 2) (13/11 16:45) la fisica de la red (peso, rebote, pasadas...) la cambia el modulo de Sider PES21FX_redes.lua (presets,
    //    reemplaza a reactive_nets_physics.lua): Sider antes que DLL. La DLL ya no escribe esas constantes; solo las lee.
    static ULONGLONG next = 0; const ULONGLONG now = GetTickCount64();
    if (now >= next) { next = now + 60000;
        Log("redes: viento %.2f m/s, empuje %.2f m/s2 (%s); en 60 s: %d pasos de red, %d despertares, %d puntos empujados; red ahora: peso %.2f, rebote %.2f, pasadas %u, estiramiento %.2f",
            v, sqrtf(w.ax * w.ax + w.az * w.az), w.on > 0.5f ? "prendido" : "apagado", s_nodes.exchange(0), s_wakes.exchange(0), s_pushed.exchange(0),
            s_constOk ? *(const float*)kGravity : 0.0f, s_constOk ? *(const float*)kHitKeep : 0.0f,
            s_constOk ? *(const unsigned*)kIters : 0u, s_constOk ? *(const float*)kStretch : 0.0f); }
}
} // namespace fx
