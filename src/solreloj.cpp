// PES21 FX - registro de la hora del dia y del sol del motor (02/11, solo lectura). Con el mapa del exe hecho con Ghidra
// (tools/re/NOTAS.md, "Sol, hora del dia, clima y sombras") se leen, cada 10 s y con cada cambio, los campos de la atmosfera
// del Fox Engine (TppAtmosphere: fecha/hora, latitud/longitud/GMT, direccion del sol, sunLux, skyLightSunScale, intensidad
// final) y los segundos del dia del interpolador de luz por hora (TODLight). No escribe nada en el juego: es la base para
// decidir despues sunLux / skyLightSunScale por hora. Todo protegido por el sello del exe y VirtualQuery en cada puntero.
#include "core.h"
#include <cmath>
#include <algorithm>

namespace fx {

static const unsigned  kExeStampSol = 0x62787C6A;
// (02/11 b) las dos son VARIABLES con un puntero al objeto (mov rcx,[0x1437d4a20] antes de TODLight_SetTime en 0x14195a92b;
// mov rcx,[0x1437d59e8] en 0x141948b42): la primera version leia la variable como si fuera el objeto (0x7C000000 s, 0 atmosferas)
static const uintptr_t kAtmoManagerPtr = 0x1437d59e8ull;   // -> manager: +0xd8 cantidad de atmosferas, +0xe0 arreglo de punteros
static const uintptr_t kTodLightPtr    = 0x1437d4a20ull;   // -> TODLight: +0x10 cantidad de spans, +0x64 segundos del dia (mod 86400)

static bool Readable(const void* p, size_t n) {
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (const char*)p + n <= (const char*)mi.BaseAddress + mi.RegionSize;
}

struct AtmoSnap { short dt[6]; float lat, lon, north, gmt, sun[4], lux, moonLux, skyScale, fin, finMoon; unsigned flags; unsigned long long id; };

static bool s_checked = false, s_ok = false; static double s_next = 0; static int s_lines = 0, s_quiet = 0;
static AtmoSnap s_last[4] = {}; static int s_lastN = -1; static unsigned s_lastSec = 0xffffffffu;

void SolRelojPresent() {
    if (!g_cfg.motorPatch) return;
    double t = Now();
    if (!s_checked) {
        s_checked = true;
        HMODULE mod = GetModuleHandleW(nullptr);
        auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
        s_ok = (uintptr_t)mod == 0x140000000ull && nt->FileHeader.TimeDateStamp == kExeStampSol
            && Readable((void*)kAtmoManagerPtr, 8) && Readable((void*)kTodLightPtr, 8);
        Log("hora del sol: registro %s (exe %08X)", s_ok ? "activo: atmosfera y reloj del motor cada 10 s" : "no disponible (exe distinto)", (unsigned)nt->FileHeader.TimeDateStamp);
    }
    if (!s_ok || s_lines >= 600) return;
    bool due = t >= s_next;

    // reloj del interpolador de luz por hora (TODLight): segundos del dia que el motor usa para mezclar los bloques _day/_night
    const char* tod = *(const char* const volatile*)kTodLightPtr; const char* man = *(const char* const volatile*)kAtmoManagerPtr;
    bool todOk = Readable(tod, 0x70), manOk = Readable(man, 0xe8);
    unsigned sec = todOk ? *(const volatile unsigned*)(tod + 0x64) : 0xfffffffeu; int spans = todOk ? *(const volatile int*)(tod + 0x10) : -1;
    if (todOk && sec >= 86400u) sec = 0xfffffffeu;
    bool secChanged = s_lastSec != 0xffffffffu && sec != 0xfffffffeu && (sec / 60 != s_lastSec / 60);   // cambio de minuto

    int n = manOk ? *(const volatile int*)(man + 0xd8) : 0; const char* const* arr = manOk ? *(const char* const* volatile*)(man + 0xe0) : nullptr;
    if (n < 0 || n > 4) n = 0;
    if (n > 0 && !Readable(arr, (size_t)n * 8)) n = 0;
    AtmoSnap cur[4] = {}; bool changed = n != s_lastN;
    for (int i = 0; i < n; i++) {
        const char* a = arr[i]; if (!Readable(a, 0x3d8)) { changed = true; continue; }
        AtmoSnap& s = cur[i];
        for (int k = 0; k < 6; k++) s.dt[k] = *(const short*)(a + 0x170 + 2 * k);
        s.lon = *(const float*)(a + 0xc0); s.lat = *(const float*)(a + 0xc4); s.north = *(const float*)(a + 0xcc); s.gmt = *(const float*)(a + 0x1d4);
        for (int k = 0; k < 4; k++) s.sun[k] = *(const float*)(a + 0x130 + 4 * k);
        s.lux = *(const float*)(a + 0x140); s.moonLux = *(const float*)(a + 0x160); s.skyScale = *(const float*)(a + 0x1b0);
        s.fin = *(const float*)(a + 0x1c0); s.finMoon = *(const float*)(a + 0x1c4); s.flags = *(const unsigned*)(a + 0x3d0);
        s.id = *(const unsigned long long*)(a + 0x1a0) & 0xffffffffffffull;
        const AtmoSnap& l = s_last[i];
        if (s.dt[3] != l.dt[3] || s.dt[4] != l.dt[4] || s.dt[0] != l.dt[0] || s.dt[1] != l.dt[1] || s.dt[2] != l.dt[2]
            || fabsf(s.lux - l.lux) > 1.0f || fabsf(s.skyScale - l.skyScale) > 0.001f || fabsf(s.lat - l.lat) > 0.01f || fabsf(s.lon - l.lon) > 0.01f || s.id != l.id) changed = true;
    }
    if (!due && !changed && !secChanged) return;
    // sin cambios: cada 10 s las primeras 6 veces, despues una linea por minuto (la primera version dejaba de escribir)
    if (!changed && !secChanged) s_quiet++; else s_quiet = 0;
    s_next = t + (s_quiet > 6 ? 60.0 : 10.0);
    s_lastN = n; for (int i = 0; i < 4; i++) s_last[i] = cur[i]; s_lastSec = sec;

    s_lines++;
    if (sec == 0xfffffffeu) Log("hora del sol: reloj de la luz por hora sin objeto (%p); %d atmosfera(s) (manager %p)%s", (const void*)tod, n, (const void*)man, changed ? " (cambio)" : "");
    else Log("hora del sol: reloj de la luz por hora %02u:%02u:%02u (%u s del dia, %d bloques de hora); %d atmosfera(s)%s",
        sec / 3600, (sec / 60) % 60, sec % 60, sec, spans, n, changed ? " (cambio)" : "");
    for (int i = 0; i < n; i++) {
        const AtmoSnap& s = cur[i]; if (s.dt[0] == 0 && s.lux == 0.0f && s.lat == 0.0f) continue;
        float len = sqrtf(s.sun[0] * s.sun[0] + s.sun[1] * s.sun[1] + s.sun[2] * s.sun[2]);
        float alt = len > 1e-6f ? asinf((std::max)(-1.0f, (std::min)(1.0f, s.sun[1] / len))) * 57.2958f : 0.0f;
        float az = atan2f(s.sun[2], s.sun[0]) * 57.2958f;
        s_lines++;
        Log("hora del sol: atmosfera %d (id %llX): fecha %04d-%02d-%02d %02d:%02d:%02d, GMT %+.1f, latitud %.3f, longitud %.3f, norte %.1f | sol (%.3f, %.3f, %.3f) altura %.1f rumbo %.0f %s | sunLux %.0f, moonLux %.2f, skyLightSunScale %.3f, intensidad final sol %.0f luna %.2f | sucio %08X",
            i, s.id, s.dt[0], s.dt[1], s.dt[2], s.dt[3], s.dt[4], s.dt[5], s.gmt, s.lat, s.lon, s.north,
            s.sun[0], s.sun[1], s.sun[2], alt, az, s.sun[3] >= 0.0f ? "(sube)" : "(baja)", s.lux, s.moonLux, s.skyScale, s.fin, s.finMoon, s.flags);
        // luz direccional que la atmosfera alimenta (Atmo_Sub1 0x14195aac0 escribe +0x110 direccion, +0x1c4 intensidad, +0x1d0 color)
        const char* a = arr[i]; const char* luz = *(const char* const*)(a + 0x188);
        if (Readable(luz, 0x1e0)) {
            const float* d = (const float*)(luz + 0x110); const float* c = (const float*)(luz + 0x1d0);
            s_lines++;
            Log("hora del sol:   luz direccional: direccion (%.3f, %.3f, %.3f), intensidad %.1f, color (%.3f, %.3f, %.3f), activa %d",
                d[0], d[1], d[2], *(const float*)(luz + 0x1c4), c[0], c[1], c[2], (int)*(const unsigned char*)(luz + 0x1dc));
        }
    }
}

} // namespace fx
