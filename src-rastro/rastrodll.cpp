// PES21 FX - Rastro v4: DLL de INVESTIGACION (rastro_v4.dll), aparte de la DLL grafica (d3d11). La carga un modulo Lua de Sider (rastro_v4.lua).
// exe y no toca nada del mod grafico ni de la tela. Solo LEE. Junta y mejora el rastro v2 + v3:
//   1. Vuelca el REGISTRO ENTERO del partido cada tick (todos los bloques, objeto completo de cada jugador) -> partido.bin.
//   2. Recupera el registro de depuracion interno del juego (printf vacio PES2021+0x23628c0) -> motor.log, filtrando el ruido.
//   3. (11/11) VIGIA PASIVO DEL OFFSIDE: solo lee memoria desde el hilo propio (MatchInfo, el juez de la IA y su bloque) y anota
//      cada cambio + el momento entero cuando se cobra una falta. Sin ganchos. Anda solo, sin teclas (vigia = 0 lo apaga).
//   4. Engancha SetMotion (0x1406f17f0) para saber QUE jugador arranca cada animacion -> anime.log.
// (11/11) BORRADOS el vigia por registros de depuracion (DR0-DR2) y el rastreador por int3: tras prenderlos en un partido la placa
// de video se reinicio (TDR). Los ganchos viejos (1, 2 y 4) quedan APAGADOS salvo rastro_ganchos = 1 en rastro_v4.ini.
// Config PROPIA: PES21FX\rastro_v4.ini (no PES21FX.ini: la DLL grafica lee ahi su propio "rastro" y se pisarian), salida PES21FX\rastro_v4\<fecha>\. Claves vigia, rastro=1, rastro_ganchos, rastro_fotos, rastro_max_mb, rastro_anime.
// No depende de core.h: todo lo necesario (Log, config, lectura segura) esta aca.
#include <windows.h>
#include <psapi.h>
#include <intrin.h>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>
#include "MinHook.h"

namespace {

// ---------------------------------------------------------------- constantes del exe (volcado de memoria, sello 62787C6A)
const unsigned  kStamp      = 0x62787C6A;
const uintptr_t kBase       = 0x140000000ull;
const uintptr_t kLogStub    = 0x1423628c0ull;   // printf de depuracion vacio del juego (119 llamadas)
const unsigned char kLogSig[11] = { 0x4c,0x89,0x44,0x24,0x18,0x4c,0x89,0x4c,0x24,0x20,0xc3 };
const uintptr_t kRegRelease = 0x140a48ae0ull;   // liberar las referencias del registro del partido (una vez por tick)
const unsigned char kRelSig[10] = { 0x80,0xb9,0xb0,0x04,0x00,0x00,0x00,0x4c,0x8b,0xc1 };
const uintptr_t kTickEndLo  = 0x1404279c0ull, kTickEndHi = 0x140427a80ull;   // rango del final del tick (EndListener)
const uintptr_t kRegGlobal  = 0x143702208ull;   // puntero al registro del partido (0 fuera del partido)
const uintptr_t kRdLo       = 0x142538000ull, kRdHi = 0x143a00000ull;        // cadenas del exe
const uintptr_t kSetMotion  = 0x1406f17f0ull;   // arranque de una animacion (que jugador)
// ---------------------------------------------------------------- configuracion propia
struct Cfg {
    std::wstring baseDir;
    bool on = false, vigia = true, ganchos = false; int maxMb = 2000, fps = 2; bool anime = false, motor = true, registro = true;   // (10/11 c) cada gancho se apaga por separado para aislar cual molesta
    int autoSeg = 0;
} g_cfg;

FILE* g_logf = nullptr;
void Log(const char* fmt, ...) {
    if (!g_logf) return;
    char b[1024]; va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (n < 0) return; if (n >= (int)sizeof b) n = (int)sizeof b - 1;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_logf, "[%02d:%02d:%02d] %.*s\n", st.wHour, st.wMinute, st.wSecond, n, b);
    fflush(g_logf);
}

// ---------------------------------------------------------------- lectura segura
bool Rd(const void* p, void* out, size_t n) { __try { memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { memset(out, 0, n); return false; } }
uintptr_t RdPtr(uintptr_t a) { uintptr_t v = 0; if (a < 0x10000) return 0; Rd((const void*)a, &v, 8); return v; }

std::string ModOff(void* pv) {
    const uintptr_t p = (uintptr_t)pv;
    if (p >= kBase && p < kBase + 0x1c000000ull) { char b[32]; snprintf(b, sizeof b, "PES2021+0x%llx", (unsigned long long)(p - kBase)); return b; }
    HMODULE m = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)pv, &m) && m) {
        wchar_t w[MAX_PATH]; if (GetModuleFileNameW(m, w, MAX_PATH)) {
            const wchar_t* s = wcsrchr(w, L'\\'); s = s ? s + 1 : w;
            char nm[64]; int i = 0; for (; s[i] && i < 63; i++) nm[i] = (char)s[i]; nm[i] = 0;
            char b[96]; snprintf(b, sizeof b, "%s+0x%llx", nm, (unsigned long long)(p - (uintptr_t)m)); return b;
        }
    }
    char b[32]; snprintf(b, sizeof b, "0x%llx", (unsigned long long)p); return b;
}

bool SigAt(uintptr_t a, const unsigned char* s, size_t n) {
    unsigned char b[32] = {};
    return n <= sizeof b && Rd((const void*)a, b, n) && memcmp(b, s, n) == 0;
}

// ---------------------------------------------------------------- estado comun
std::atomic<bool> s_on{false}, s_tried{false};
std::atomic<uint64_t> s_bytes{0}; uint64_t s_cap = 2000ull << 20;
std::atomic<unsigned> s_msgs{0}, s_drop{0}, s_snaps{0}, s_skip{0}, s_anime{0};
std::atomic<int> s_tick{-1};
std::atomic<int> s_frame{0};
SRWLOCK s_lk = SRWLOCK_INIT;
FILE* s_log = nullptr; FILE* s_bin = nullptr; FILE* s_csv = nullptr; FILE* s_anm = nullptr;
std::wstring s_dir;
int s_lastSnapTick = -1000000;
int PresentNo() { return s_frame.load(std::memory_order_relaxed); }

void Put(FILE* f, const void* d, size_t n) {
    if (!f || s_bytes.load(std::memory_order_relaxed) + n > s_cap) { s_drop.fetch_add(1, std::memory_order_relaxed); return; }
    AcquireSRWLockExclusive(&s_lk);
    fwrite(d, 1, n, f);
    ReleaseSRWLockExclusive(&s_lk);
    s_bytes.fetch_add(n, std::memory_order_relaxed);
}

// ---------------------------------------------------------------- motor.log (registro de depuracion del juego)
using LogFn = void(*)(void*, const char*, ...);
LogFn o_log = nullptr;

bool IsExeFormat(const char* f) {
    const uintptr_t a = (uintptr_t)f;
    if (a < kRdLo || a >= kRdHi) return false;
    __try {
        int pct = 0, i = 0;
        for (; i < 400 && f[i]; i++) { unsigned char c = (unsigned char)f[i]; if (c == '%') pct++; else if (c < 0x20 && c != '\n' && c != '\t' && c != '\r') return false; }
        return i >= 3 && (pct > 0 || (i > 0 && f[i - 1] == '\n') || i >= 6);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int Fmt(char* buf, size_t cap, const char* fmt, va_list ap) { __try { return vsnprintf(buf, cap, fmt, ap); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; } }

void hk_log(void* self, const char* fmt, ...) {
    if (!s_on.load(std::memory_order_relaxed) || !IsExeFormat(fmt)) return;
    char msg[1024]; va_list ap; va_start(ap, fmt); int n = Fmt(msg, sizeof msg, fmt, ap); va_end(ap);
    if (n < 0) { snprintf(msg, sizeof msg, "(no se pudo formatear) %.200s", fmt); n = (int)strlen(msg); }
    if (n > 13 && memcmp(msg, "[GetMoveMode]", 13) == 0 && strstr(msg, "=ERROR")) { s_skip.fetch_add(1, std::memory_order_relaxed); return; }
    if (n >= (int)sizeof msg) n = (int)sizeof msg - 1;
    while (n > 0 && (msg[n - 1] == '\n' || msg[n - 1] == '\r')) msg[--n] = 0;
    for (int i = 0; i < n; i++) if (msg[i] == '\n' || msg[i] == '\r') msg[i] = ' ';
    char line[1200];
    int m = snprintf(line, sizeof line, "%d\t%d\t%lu\t%s\n", PresentNo(), s_tick.load(std::memory_order_relaxed), GetCurrentThreadId(), msg);
    if (m > 0) Put(s_log, line, (size_t)(m < (int)sizeof line ? m : (int)sizeof line - 1));
    s_msgs.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------- foto del partido (registro entero)
struct Blk { const char* name; int ref; int idx; uint32_t from, len; };
// v4: bloques enteros. ref = offset en el registro del puntero a los datos (Ref+0x18). idx>0 = arreglo, paso 0x28.
const Blk kBlks[] = {
    { "Screen2dInfo", 0x18, 0, 0x0, 0x800 },  { "BallInfo", 0x40, 0, 0x0, 0x2c00 },  { "CameraSettings", 0x68, 0, 0x0, 0x800 },
    { "ClothJudgeInfo", 0x90, 0, 0x0, 0x800 },{ "CommandOutput", 0xb8, 0, 0x0, 0x800 },{ "CornerFlagInfo", 0xe0, 0, 0x0, 0x400 },
    { "ConceptArrangeReleaseInfo", 0x108, 0, 0x0, 0x800 }, { "VanishingSprayInfo", 0x130, 0, 0x0, 0x400 },
    { "GoalPostInfo", 0x158, 0, 0x0, 0x400 }, { "CursorInfo", 0x180, 0, 0x0, 0x800 }, { "DemoInfo", 0x1a8, 0, 0x0, 0x800 },
    { "DemoControl", 0x1d0, 0, 0x0, 0x400 },  { "FixDemoInfo", 0x1f8, 0, 0x0, 0x800 },{ "GeneralSettings", 0x220, 0, 0x0, 0x400 },
    { "GPAchievementInfo", 0x248, 0, 0x0, 0x400 }, { "MatchInfo", 0x270, 0, 0x0, 0x2000 }, { "ESports", 0x298, 0, 0x0, 0x400 },
    { "MatchEnv", 0x2c0, 0, 0x0, 0x400 },      { "NoOperationLeaveInfo", 0x2e8, 0, 0x0, 0x400 }, { "PkMatch", 0x310, 0, 0x0, 0x800 },
    { "PlayData", 0x338, 0, 0x0, 0x1000 },     { "RandomInfo", 0x360, 0, 0x0, 0x400 }, { "ReplayInfo", 0x388, 0, 0x0, 0x800 },
    { "StepupTutorial", 0x3b0, 0, 0x0, 0x800 },{ "SystemSettings", 0x3d8, 0, 0x0, 0x400 }, { "TrainingWork", 0x400, 0, 0x0, 0x800 },
    { "AnalyzeInfo", 0x2968, 0, 0x0, 0x800 },  { "CameraInfo", 0x2990, 0, 0x0, 0x400 },{ "CommandInfo", 0x29b8, 0, 0x0, 0x800 },
    { "PadInput", 0x29e0, 0, 0x0, 0x400 },     { "RagdollInfo", 0x2a08, 0, 0x0, 0x800 },{ "RecordInfo", 0x2a30, 0, 0x0, 0x800 },
    { "BallAttach", 0x2a58, 0, 0x0, 0x400 },   { "SituationInfo", 0x2a80, 0, 0x0, 0x800 }, { "FieldInfo", 0x2aa8, 0, 0x0, 0x400 },
    { "ModeInfo", 0x2ad0, 0, 0x0, 0x400 },     { "ModeInfo_equipo0", 0x2ad0, 0, 0xc400, 0x2804 }, { "ModeInfo_equipo1", 0x2ad0, 0, 0xc400 + 0x2804, 0x2804 },
    { "TeamAIInfo", 0x1928, 2, 0x0, 0x1000 },  { "OrderInfo", 0x1978, 2, 0x0, 0x800 },
    { "JugadorEntero", 0x19c8, 25, 0x0, 0x3f40 },   // objeto completo de cada jugador y arbitros (0..24)
};
uint32_t RecSize() { uint32_t s = 16; for (auto& b : kBlks) s += (b.idx ? b.idx : 1) * (8 + b.len); return s; }

void BuildRecord(std::string& rec, uintptr_t reg, int tick, int phase) {
    rec.assign(RecSize(), '\0');
    char* p = &rec[0];
    const uint32_t magic = 0x52545850; const uint32_t ms = (uint32_t)GetTickCount();
    memcpy(p, &magic, 4); memcpy(p + 4, &tick, 4); memcpy(p + 8, &phase, 4); memcpy(p + 12, &ms, 4); p += 16;
    for (auto& b : kBlks) {
        const int cnt = b.idx ? b.idx : 1;
        for (int i = 0; i < cnt; i++) {
            const uintptr_t d = RdPtr(reg + b.ref + (uintptr_t)i * 0x28);
            memcpy(p, &d, 8); p += 8;
            if (d) Rd((const void*)(d + b.from), p, b.len);
            p += b.len;
        }
    }
}

void Snapshot() {
    const uintptr_t reg = RdPtr(kRegGlobal);
    if (!reg) return;
    const uintptr_t mi = RdPtr(reg + 0x270);
    int tick = -1, phase = -1, type = -1;
    if (mi) { Rd((const void*)(mi + 0x1198), &tick, 4); Rd((const void*)(mi + 0x1168), &phase, 4); Rd((const void*)(mi + 0x116c), &type, 4); }
    s_tick.store(tick, std::memory_order_relaxed);
    const int step = 54 / (g_cfg.fps > 0 ? (g_cfg.fps > 54 ? 54 : g_cfg.fps) : 2);
    if (tick < 0 || (tick - s_lastSnapTick < step && tick >= s_lastSnapTick)) return;
    s_lastSnapTick = tick;
    static std::string rec; BuildRecord(rec, reg, tick, phase);
    Put(s_bin, rec.data(), rec.size());
    unsigned char rain = 0, env = 0; float tscale = 0; signed char dir0 = 0, dir1 = 0;
    if (uintptr_t me = RdPtr(reg + 0x2c0)) { Rd((const void*)(me + 0x12), &rain, 1); Rd((const void*)(me + 0x44), &env, 1); Rd((const void*)(me + 0x5c), &tscale, 4); }
    if (uintptr_t t0 = RdPtr(reg + 0x1928)) Rd((const void*)(t0 + 0x254), &dir0, 1);
    if (uintptr_t t1 = RdPtr(reg + 0x1950)) Rd((const void*)(t1 + 0x254), &dir1, 1);
    int live = 0; for (int i = 0; i < 22; i++) if (RdPtr(reg + 0x25f8 + (uintptr_t)i * 0x28)) live++;
    char line[256];
    int n = snprintf(line, sizeof line, "%d\t%d\t%d\t%d\t%u\t%u\t%.4f\t%d\t%d\t%d\n", PresentNo(), tick, phase, type, rain, env, tscale, dir0, dir1, live);
    if (n > 0) Put(s_csv, line, (size_t)n);
    s_snaps.fetch_add(1, std::memory_order_relaxed);
}

using RelFn = void(*)(void*);
RelFn o_rel = nullptr;
void hk_rel(void* view) {
    const uintptr_t ra = (uintptr_t)_ReturnAddress();
    if (s_on.load(std::memory_order_relaxed) && ra > kTickEndLo && ra < kTickEndHi) { __try { Snapshot(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    o_rel(view);
}

// ---------------------------------------------------------------- SetMotion: que jugador arranca cada animacion
using MotFn = void*(*)(void*, void*, void*, void*);
MotFn o_mot = nullptr;
int PlayerIndexOf(uintptr_t obj) {   // mapear puntero del objeto animacion a indice 0..24 via el registro
    const uintptr_t reg = RdPtr(kRegGlobal); if (!reg || !obj) return -1;
    for (int i = 0; i < 25; i++) { const uintptr_t a = RdPtr(reg + 0x19c8 + (uintptr_t)i * 0x28); if (a && (a == obj || obj == a)) return i; }
    return -1;
}
void* hk_mot(void* self, void* a2, void* a3, void* a4) {
    if (s_on.load(std::memory_order_relaxed) && g_cfg.anime && s_anm) {
        int anim = -1; Rd((const void*)((uintptr_t)self + 0x18), &anim, 2);
        const int idx = PlayerIndexOf((uintptr_t)self);
        char line[160];
        int n = snprintf(line, sizeof line, "%d\t%d\t%d\t%d\t%p\t%p\t%p\n", PresentNo(), s_tick.load(std::memory_order_relaxed), idx, anim & 0xffff, a2, a3, a4);
        if (n > 0) { AcquireSRWLockExclusive(&s_lk); fwrite(line, 1, n, s_anm); ReleaseSRWLockExclusive(&s_lk); }
        s_anime.fetch_add(1, std::memory_order_relaxed);
    }
    return o_mot(self, a2, a3, a4);
}

// ---------------------------------------------------------------- vigia pasivo del offside (11/11)
// Reemplaza al vigia por registros de depuracion (DR0-DR2) y al rastreador por int3, que se BORRARON: el 10/11 la placa de
// video se reinicio (TDR 0x887A0020) 5 s despues de prenderlos en un partido. Este vigia SOLO LEE memoria desde el hilo propio
// de la DLL: no engancha funciones, no pone excepciones, no suspende ni toca ningun otro hilo, no escribe en el juego.
// Mira tres cosas cada ~2 ms dentro de un partido y anota cada cambio en PES21FX\vigia_offside\<fecha>\vigia.log:
//   MI     = MatchInfo+0x1554 (estado de detencion + registro de falta; tipo de falta en +0x34 del bloque, 1 = offside)
//   JUEZ   = match::ai::Judge (lo encuentra buscando su vtable en la memoria del proceso): el "pendiente" que arma
//            FUN_14050f7f0 (juez+8 = hay pendiente, +0xc/+0x10 esperas, +0x14.. = infractor, victima, ..., +0x28 tipo, +0x34 posicion)
//   BLOQUE = el bloque de trabajo del juez (juez+0x40b8), de donde MatchControl copia a MatchInfo
// Cuando aparece un pendiente o un tipo de falta distinto de 0 guarda en eventos.bin: los ultimos 256 ticks (pelota, posicion
// de los 22, tablas del mundo de la IA W+0x24700..0x25600 con las mascaras de "en posicion adelantada"), el juez, el bloque y
// una foto entera del registro del partido.
const uintptr_t kJudgeVt  = 0x1425ce1a0ull;   // vtable de match::ai::Judge (rtti.txt)
const uintptr_t kJudgeBlk = 0x40b8;           // juez (X+0x908) -> bloque del juez (X+0x49c0)
constexpr int kVgJ = 4, kVgRingN = 256, kVgMaxEv = 60, kVgMaxEvChico = 300, kVgMaxLines = 300000;   // 60 offsides enteros (~1,8 MB c/u) + 300 faltas livianas
struct VgRing { int tick; uint32_t ms; unsigned char ball[0x40]; float pos[22][3]; unsigned char w[0xF00]; };
struct VgJudge { uintptr_t a; bool strong, hj, hb; unsigned char jg[0xa8], bk[0x70]; };
struct Vg {
    bool dead = false; FILE* log = nullptr; FILE* bin = nullptr;
    uintptr_t reg = 0; VgJudge j[kVgJ] = {}; int nj = 0;
    bool scanning = false; uintptr_t scanNext = 0; ULONGLONG scanT = 0; int scans = 0; uint64_t scanBytes = 0;
    unsigned char mi[0x70] = {}; bool hmi = false;
    uintptr_t w = 0; bool wWarn = false, hmk = false; unsigned char mk[0x28] = {}; float ln[4] = {};
    int tick = -1, lastTick = -1, fotoAt = -1; VgRing* ring = nullptr; int ringPos = 0, ringCnt = 0;
    int events = 0, eventsFull = 0, lines = 0, secLines = 0, drop = 0; ULONGLONG secT = 0; unsigned faults = 0;
} g_vg;

void VgLine(const char* fmt, ...) {
    if (!g_vg.log) return;
    const ULONGLONG now = GetTickCount64();
    if (now - g_vg.secT >= 1000) { if (g_vg.drop) fprintf(g_vg.log, "(%d renglones salteados en 1 s)\n", g_vg.drop); g_vg.secT = now; g_vg.secLines = 0; g_vg.drop = 0; }
    if (g_vg.lines >= kVgMaxLines || g_vg.secLines >= 200) { g_vg.drop++; return; }
    g_vg.lines++; g_vg.secLines++;
    char b[1500]; va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (n < 0) return; if (n >= (int)sizeof b) n = (int)sizeof b - 1;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_vg.log, "%02d:%02d:%02d.%03d\t%d\t%.*s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, g_vg.tick, n, b);
}
void VgHex(char* out, size_t cap, const unsigned char* d, int n) {
    static const char* hx = "0123456789abcdef"; size_t k = 0;
    for (int i = 0; i < n && k + 3 < cap; i++) { if (i && !(i & 3)) out[k++] = ' '; out[k++] = hx[d[i] >> 4]; out[k++] = hx[d[i] & 15]; }
    out[k] = 0;
}
int VgI(const unsigned char* d, int off) { int v; memcpy(&v, d + off, 4); return v; }
float VgF(const unsigned char* d, int off) { float v; memcpy(&v, d + off, 4); return v; }

bool VgOpen() {
    if (g_vg.log) return true;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t name[48]; swprintf(name, 48, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::wstring dir = g_cfg.baseDir + L"vigia_offside\\"; CreateDirectoryW(dir.c_str(), nullptr);
    dir += name; dir += L"\\"; CreateDirectoryW(dir.c_str(), nullptr);
    g_vg.log = _wfopen((dir + L"vigia.log").c_str(), L"wb");
    g_vg.bin = _wfopen((dir + L"eventos.bin").c_str(), L"wb");
    g_vg.ring = (VgRing*)VirtualAlloc(nullptr, sizeof(VgRing) * kVgRingN, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_vg.log || !g_vg.bin || !g_vg.ring) { Log("vigia: no se pudo abrir la carpeta de salida o reservar memoria: apagado"); g_vg.dead = true; return false; }
    fputs("hora\ttick\tque\n", g_vg.log);
    Log("vigia de offside: anotando en vigia_offside\\%ls", name);
    return true;
}

// busca la vtable del juez en la memoria privada de lectura/escritura (nunca paginas guardia ni memoria mapeada del driver),
// y solo en las paginas que YA estan en uso (QueryWorkingSetEx): leer una pagina reservada sin usar la haria ocupar memoria.
int VgScanRange(uintptr_t a, uintptr_t e, uintptr_t* found, int n, int cap) {
    __try {
        for (uintptr_t p = a; p + 8 <= e; p += 8)
            if (*(const uintptr_t*)p == kJudgeVt) { if (n < cap) found[n++] = p; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}
int VgScanChunk(uintptr_t a, uintptr_t e, uintptr_t* found, int cap) {   // a alineado a pagina, e - a <= 8 MB
    static PSAPI_WORKING_SET_EX_INFORMATION ws[2048];
    const size_t np = (size_t)((e - a + 0xfff) >> 12);
    if (!np || np > 2048 || (a & 0xfff)) return 0;
    for (size_t i = 0; i < np; i++) { ws[i].VirtualAddress = (PVOID)(a + i * 0x1000); ws[i].VirtualAttributes.Flags = 0; }
    if (!QueryWorkingSetEx(GetCurrentProcess(), ws, (DWORD)(np * sizeof ws[0]))) return 0;   // sin el dato no se lee nada
    int n = 0;
    for (size_t i = 0; i < np; ) {
        if (!ws[i].VirtualAttributes.Valid) { i++; continue; }
        size_t k = i; while (k < np && ws[k].VirtualAttributes.Valid) k++;
        const uintptr_t ra = a + i * 0x1000, re = a + k * 0x1000;
        n = VgScanRange(ra, re < e ? re : e, found, n, cap);
        i = k;
    }
    return n;
}
void VgAddJudge(uintptr_t p) {
    const uintptr_t out = RdPtr(p + 0x3c0), vt2 = RdPtr(p + 0x3c8);
    const bool strong = out == p + kJudgeBlk;
    if (!strong && !(vt2 >= kRdLo && vt2 < kRdHi)) return;
    for (int i = 0; i < g_vg.nj; i++) if (g_vg.j[i].a == p) return;
    if (g_vg.nj >= kVgJ) {   // lleno: uno con bloque propio le gana a uno dudoso
        int w = -1; if (strong) for (int i = 0; i < g_vg.nj; i++) if (!g_vg.j[i].strong) { w = i; break; }
        if (w < 0) return;
        for (int k = w; k + 1 < g_vg.nj; k++) g_vg.j[k] = g_vg.j[k + 1];
        g_vg.nj--;
    }
    VgJudge& j = g_vg.j[g_vg.nj]; memset(&j, 0, sizeof j); j.a = p; j.strong = strong;
    VgLine("JUEZ%d encontrado en %p (%s; salida %p)", g_vg.nj, (void*)p, strong ? "bloque propio" : "sin bloque propio todavia", (void*)out);
    g_vg.nj++;
}
void VgScanStep() {
    uint64_t budget = 8ull << 20; int queries = 0;   // 8 MB y 1024 consultas por paso (con 2 MB tardo 174 s en 7 GB)
    while (budget > 0 && queries++ < 1024) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (g_vg.scanNext >= 0x7ffffffe0000ull || !VirtualQuery((const void*)g_vg.scanNext, &mbi, sizeof mbi)) {
            g_vg.scanning = false;
            VgLine("busqueda del juez terminada: %d encontrados, %llu MB leidos en %llu ms", g_vg.nj, (unsigned long long)(g_vg.scanBytes >> 20), (unsigned long long)(GetTickCount64() - g_vg.scanT));
            g_vg.scanT = GetTickCount64();
            return;
        }
        const uintptr_t base = (uintptr_t)mbi.BaseAddress, end = base + mbi.RegionSize;
        if (end <= g_vg.scanNext) { g_vg.scanning = false; return; }
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE) {
            const uintptr_t a = g_vg.scanNext > base ? g_vg.scanNext : base;
            const uintptr_t e = (end - a > budget) ? a + (uintptr_t)budget : end;
            uintptr_t f[16]; const int n = VgScanChunk(a, e, f, 16);
            for (int i = 0; i < n; i++) VgAddJudge(f[i]);
            budget -= (e - a); g_vg.scanBytes += (e - a); g_vg.scanNext = e;
        } else g_vg.scanNext = end;
    }
}
// W = match::registry::AnalyzeInfo, bloque del registro en reg+0x2968 (FUN_140464090 lo crea; adentro tiene las tres
// AnalyzeDetail*, cuyas vtables sirven para saber que es el). Se prueba el puntero tal cual y corrido 8 (cabecera de la reserva).
const uintptr_t kVtAnInstr = 0x1425a65a0ull, kVtAnLink = 0x1425a65c0ull;
bool VgIsWorld(uintptr_t w) { return w && RdPtr(w + 0x25cf0) == kVtAnInstr && RdPtr(w + 0x25e00) == kVtAnLink; }
uintptr_t VgWorld() {
    if (!g_vg.reg) return 0;
    if (g_vg.w && VgIsWorld(g_vg.w)) return g_vg.w;
    const uintptr_t p = RdPtr(g_vg.reg + 0x2968); uintptr_t w = 0;
    if (VgIsWorld(p)) w = p; else if (p && VgIsWorld(p + 8)) w = p + 8;
    if (w && w != g_vg.w) VgLine("mundo (AnalyzeInfo) en %p (registro+0x2968 = %p)", (void*)w, (void*)p);
    else if (!w && !g_vg.wWarn && p) { g_vg.wWarn = true; VgLine("registro+0x2968 = %p no tiene la forma de AnalyzeInfo: sin mascaras", (void*)p); }
    g_vg.w = w; return w;
}
uintptr_t VgAnyJudge() { for (int i = 0; i < g_vg.nj; i++) if (g_vg.j[i].strong) return g_vg.j[i].a; return g_vg.nj ? g_vg.j[0].a : 0; }

void BuildRecord(std::string& rec, uintptr_t reg, int tick, int phase);
void VgBin(const void* d, size_t n) { if (g_vg.bin) fwrite(d, 1, n, g_vg.bin); }
void VgChunk(const char tag[4], const void* d, uint32_t n) { VgBin(tag, 4); VgBin(&n, 4); if (n) VgBin(d, n); }
void VgFoto(const char tag[4]) {
    static std::string rec; int phase = -1;
    if (uintptr_t mi = RdPtr(g_vg.reg + 0x270)) Rd((const void*)(mi + 0x1168), &phase, 4);
    BuildRecord(rec, g_vg.reg, g_vg.tick, phase);
    VgChunk(tag, rec.data(), (uint32_t)rec.size());
}
// eventos.bin = bloques {4 letras, u32 largo, datos}: EVNT (cabecera), RING, JUEZ, BLOQ, MUND, FOTO, FOT2 (foto 60 ticks despues).
// full (offside): con RING y FOTO. Las demas faltas: solo JUEZ, BLOQ y MUND.
void VgEvent(const char* what, int ji, bool full) {
    if (full && g_vg.eventsFull >= kVgMaxEv) full = false;
    if (g_vg.events >= kVgMaxEvChico) { if (g_vg.events == kVgMaxEvChico) { VgLine("EVENTO (%s): tope de %d eventos guardados, no se guardan mas", what, kVgMaxEvChico); g_vg.events++; } return; }
    const int n = ++g_vg.events; if (full) g_vg.eventsFull++;
    struct { int n, tick; uint32_t ms; int ringCnt, ringEntry; char what[44]; } h{};
    h.n = n; h.tick = g_vg.tick; h.ms = (uint32_t)GetTickCount(); h.ringCnt = full ? g_vg.ringCnt : 0; h.ringEntry = (int)sizeof(VgRing); strncpy(h.what, what, sizeof h.what - 1);
    VgChunk("EVNT", &h, sizeof h);
    if (full) { VgBin("RING", 4); uint32_t sz = (uint32_t)(sizeof(VgRing) * g_vg.ringCnt); VgBin(&sz, 4); }
    if (full) for (int i = 0; i < g_vg.ringCnt; i++) VgBin(&g_vg.ring[(g_vg.ringPos - g_vg.ringCnt + i + 2 * kVgRingN) % kVgRingN], sizeof(VgRing));
    const uintptr_t judge = (ji >= 0 && ji < g_vg.nj) ? g_vg.j[ji].a : VgAnyJudge();
    static unsigned char buf[0x1B00];
    if (judge) {
        Rd((const void*)judge, buf, 0x3f0); VgChunk("JUEZ", buf, 0x3f0);
        const uintptr_t blk = judge + kJudgeBlk;   // fijo: juez+0x3c0 solo vale dentro de la llamada del juego
        { static unsigned char b2[0xd10]; Rd((const void*)blk, b2, sizeof b2); VgChunk("BLOQ", b2, sizeof b2); }
    }
    if (const uintptr_t w = VgWorld()) {
        Rd((const void*)(w + 0x24600), buf, 0x1B00); VgChunk("MUND", buf, 0x1B00);
        unsigned m[8] = {}; Rd((const void*)(w + 0x253c4), m, sizeof m);
        VgLine("EVENTO %d mundo %p: mascaras adelantados equipo0 %x %x %x %x | equipo1 %x %x %x %x", n, (void*)w, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7]);
    }
    if (full) { VgFoto("FOTO"); g_vg.fotoAt = g_vg.tick + 60; }
    VgLine(full ? "EVENTO %d guardado (%s): %d ticks anteriores + foto del registro" : "EVENTO %d guardado (%s)", n, what, g_vg.ringCnt);
    fflush(g_vg.bin); fflush(g_vg.log);
    Log("vigia: evento %d (%s) en el tick %d", n, what, g_vg.tick);
}
void VgBlockLine(const char* name, int idx, const unsigned char* b) {
    char hx[260], nm[16]; VgHex(hx, sizeof hx, b, 0x70);
    if (idx < 0) snprintf(nm, sizeof nm, "%s", name); else snprintf(nm, sizeof nm, "%s%d", name, idx);
    VgLine("%s estado=%d pos=(%.2f %.2f %.2f) motivo=%d tipo_falta=%d clase=%d infractor=%d victima=%d | %s", nm,
        VgI(b, 0), VgF(b, 8), VgF(b, 0xc), VgF(b, 0x10), VgI(b, 0x20), VgI(b, 0x34), VgI(b, 0x38), VgI(b, 0x3c), VgI(b, 0x40), hx);
}
void VgPend(char* out, size_t cap, const unsigned char* j, int o) {   // pendiente en juez+o (0x14 o 0x58), forma de FUN_14050f7f0
    snprintf(out, cap, "{infractor=%d victima=%d cursor=%d %d detencion=%d tipo=%d %d pos=(%.2f %.2f %.2f)}",
        VgI(j, o), VgI(j, o + 4), VgI(j, o + 8), VgI(j, o + 0xc), VgI(j, o + 0x10), VgI(j, o + 0x14), VgI(j, o + 0x18), VgF(j, o + 0x20), VgF(j, o + 0x24), VgF(j, o + 0x28));
}
bool VgStep() {
    const uintptr_t reg = RdPtr(kRegGlobal);
    if (!reg) {
        if (g_vg.reg) { VgLine("fin del partido (registro en 0)"); if (g_vg.log) fflush(g_vg.log); }
        g_vg.reg = 0; g_vg.w = 0; g_vg.wWarn = false; g_vg.hmk = false; g_vg.nj = 0; g_vg.scanning = false; g_vg.hmi = false; g_vg.lastTick = -1; g_vg.ringCnt = 0; g_vg.scans = 0; g_vg.fotoAt = -1;
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    if (reg != g_vg.reg) {
        if (!VgOpen()) return false;
        g_vg.reg = reg; g_vg.w = 0; g_vg.wWarn = false; g_vg.hmk = false; g_vg.nj = 0; g_vg.hmi = false; g_vg.lastTick = -1; g_vg.ringCnt = 0; g_vg.fotoAt = -1;
        g_vg.scanning = true; g_vg.scanNext = 0x10000; g_vg.scanT = now; g_vg.scans = 1; g_vg.scanBytes = 0;
        VgLine("partido nuevo: registro %p; buscando al juez", (void*)reg);
    }
    const uintptr_t mi = RdPtr(reg + 0x270);
    if (!mi) return true;
    int tick = -1; Rd((const void*)(mi + 0x1198), &tick, 4); g_vg.tick = tick;
    // jueces: validar, buscar
    for (int i = 0; i < g_vg.nj; i++) if (RdPtr(g_vg.j[i].a) != kJudgeVt) {
        VgLine("JUEZ%d ya no esta en %p", i, (void*)g_vg.j[i].a);
        for (int k = i; k + 1 < g_vg.nj; k++) g_vg.j[k] = g_vg.j[k + 1];
        g_vg.nj--; i--;
    }
    if (g_vg.scanning) VgScanStep();
    else if (g_vg.nj == 0 && g_vg.scans < 4 && now - g_vg.scanT > 20000) { g_vg.scanning = true; g_vg.scanNext = 0x10000; g_vg.scanT = now; g_vg.scans++; g_vg.scanBytes = 0; }
    // MatchInfo
    unsigned char cur[0x70];
    if (Rd((const void*)(mi + 0x1554), cur, sizeof cur)) {
        if (!g_vg.hmi || memcmp(cur, g_vg.mi, sizeof cur) != 0) {
            const int was = g_vg.hmi ? VgI(g_vg.mi, 0x34) : 0;
            VgBlockLine("MI", -1, cur);
            const bool first = !g_vg.hmi; memcpy(g_vg.mi, cur, sizeof cur); g_vg.hmi = true;
            if (!first && VgI(cur, 0x34) != 0 && VgI(cur, 0x34) != was) VgEvent(VgI(cur, 0x34) == 1 ? "MatchInfo: tipo de falta 1 (offside)" : "MatchInfo: tipo de falta distinto de 0", -1, VgI(cur, 0x34) == 1);
        }
    }
    for (int i = 0; i < g_vg.nj; i++) {
        VgJudge& j = g_vg.j[i];
        if (!j.strong && RdPtr(j.a + 0x3c0) == j.a + kJudgeBlk) { j.strong = true; VgLine("JUEZ%d ahora apunta a su bloque propio", i); }
        unsigned char jg[0xa8];
        if (Rd((const void*)j.a, jg, sizeof jg)) {
            // +0xc y +0x10 son esperas que bajan cada tick: no cuentan como cambio
            const bool ch = !j.hj || jg[8] != j.jg[8] || memcmp(jg + 0x14, j.jg + 0x14, sizeof jg - 0x14) != 0;
            if (ch) {
                char a[200], b[200], hx[400]; VgPend(a, sizeof a, jg, 0x14); VgPend(b, sizeof b, jg, 0x58); VgHex(hx, sizeof hx, jg + 8, 0xa0);
                VgLine("JUEZ%d pendiente=%d espera=%d cuenta=%d A%s B%s ultimo=%d | %s", i, jg[8], VgI(jg, 0xc), VgI(jg, 0x10), a, b, VgI(jg, 0xa0), hx);
                const bool nuevo = j.hj && jg[8] && (!j.jg[8] || VgI(jg, 0x14) != VgI(j.jg, 0x14) || VgI(jg, 0x28) != VgI(j.jg, 0x28));
                memcpy(j.jg, jg, sizeof jg); j.hj = true;
                if (nuevo) VgEvent(VgI(jg, 0x28) == 1 ? "juez: pendiente nuevo tipo 1 (offside)" : "juez: pendiente nuevo", i, VgI(jg, 0x28) == 1);
            } else { memcpy(j.jg + 0xc, jg + 0xc, 8); }
        }
        const uintptr_t blk = j.a + kJudgeBlk;
        unsigned char bk[0x70];
        if (Rd((const void*)blk, bk, sizeof bk) && (!j.hb || memcmp(bk, j.bk, sizeof bk) != 0)) {
            const int was = j.hb ? VgI(j.bk, 0x34) : 0; const bool first = !j.hb;
            VgBlockLine("BLOQUE", i, bk); memcpy(j.bk, bk, sizeof bk); j.hb = true;
            if (!first && VgI(bk, 0x34) != 0 && VgI(bk, 0x34) != was) VgEvent(VgI(bk, 0x34) == 1 ? "bloque del juez: tipo de falta 1 (offside)" : "bloque del juez: tipo de falta distinto de 0", i, false);
        }
    }
    // marcas de "en posicion adelantada": W+0x253bc = tick del ultimo calculo por equipo, W+0x253c4 = mascaras [equipo][clase 0..3]
    // (0 = mas de 4 m adelantado, 1 = 1 a 4 m, 2 = menos de 1 m, 3 = habilitado por menos de 1 m), W+0x254ec = linea usada
    if (const uintptr_t w = VgWorld()) {
        unsigned char mk[0x28]; float ln[4] = {};
        if (Rd((const void*)(w + 0x253bc), mk, sizeof mk) && Rd((const void*)(w + 0x254ec), ln, 8)) {
            Rd((const void*)(w + 0x58), &ln[2], 4); Rd((const void*)(w + 0x44), &ln[3], 4);   // la linea que cuenta para un equipo es la del rival (W+0x44+rival*0x14)
            if (!g_vg.hmk || memcmp(mk, g_vg.mk, sizeof mk) != 0) {
                const bool algo = memcmp(mk + 8, g_vg.mk + 8, sizeof mk - 8) != 0 || !g_vg.hmk;   // solo el tick cambio: no se anota
                memcpy(g_vg.mk, mk, sizeof mk); g_vg.hmk = true;
                if (algo) VgLine("MARCA calculo en ticks %d %d | equipo0 %x %x %x %x linea %.2f (ahora %.2f) | equipo1 %x %x %x %x linea %.2f (ahora %.2f)",
                    VgI(mk, 0), VgI(mk, 4), VgI(mk, 8), VgI(mk, 0xc), VgI(mk, 0x10), VgI(mk, 0x14), ln[0], ln[2],
                    VgI(mk, 0x18), VgI(mk, 0x1c), VgI(mk, 0x20), VgI(mk, 0x24), ln[1], ln[3]);
            }
        }
    }
    // anillo: una muestra por tick
    if (tick != g_vg.lastTick && g_vg.ring) {
        g_vg.lastTick = tick;
        VgRing& r = g_vg.ring[g_vg.ringPos]; memset(&r, 0, sizeof r);
        r.tick = tick; r.ms = (uint32_t)GetTickCount();
        if (const uintptr_t ball = RdPtr(reg + 0x40)) Rd((const void*)ball, r.ball, sizeof r.ball);
        for (int i = 0; i < 22; i++) if (const uintptr_t p = RdPtr(reg + 0x1dd8 + (uintptr_t)i * 0x28)) Rd((const void*)(p + 0x55c), r.pos[i], 12);
        if (const uintptr_t w = VgWorld()) Rd((const void*)(w + 0x24700), r.w, sizeof r.w);
        g_vg.ringPos = (g_vg.ringPos + 1) % kVgRingN; if (g_vg.ringCnt < kVgRingN) g_vg.ringCnt++;
        if (g_vg.fotoAt >= 0 && (tick >= g_vg.fotoAt || tick < g_vg.fotoAt - 600)) { g_vg.fotoAt = -1; VgFoto("FOT2"); fflush(g_vg.bin); }
    }
    return true;
}
bool VgStepSafe() {
    if (g_vg.dead) return false;
    __try { return VgStep(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { if (++g_vg.faults > 50) { g_vg.dead = true; Log("vigia: demasiadas lecturas fallidas, apagado"); } return false; }
}
void VgFlush() { if (g_vg.log) fflush(g_vg.log); if (g_vg.bin) fflush(g_vg.bin); }

// ---------------------------------------------------------------- config (PES21FX.ini)
void LoadCfg() {
    // baseDir = carpeta del exe + PES21FX\ ;  ini
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* sl = wcsrchr(exe, L'\\'); if (sl) sl[1] = 0;
    g_cfg.baseDir = std::wstring(exe) + L"PES21FX\\";
    FILE* f = _wfopen((g_cfg.baseDir + L"rastro_v4.ini").c_str(), L"rb");
    if (!f) return;
    char buf[4096];
    while (fgets(buf, sizeof buf, f)) {
        std::string s(buf);
        auto h = s.find(';'); if (h != std::string::npos) s = s.substr(0, h);
        auto eq = s.find('='); if (eq == std::string::npos) continue;
        std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        auto trim = [](std::string& x) { size_t a = x.find_first_not_of(" \t\r\n"); size_t b = x.find_last_not_of(" \t\r\n"); x = (a == std::string::npos) ? "" : x.substr(a, b - a + 1); };
        trim(k); trim(v); for (auto& c : k) c = (char)tolower((unsigned char)c);
        if (k == "rastro") g_cfg.on = atoi(v.c_str()) != 0;
        else if (k == "rastro_max_mb") { int x = atoi(v.c_str()); g_cfg.maxMb = x < 10 ? 10 : (x > 100000 ? 100000 : x); }
        else if (k == "rastro_fotos") { int x = atoi(v.c_str()); g_cfg.fps = x < 1 ? 1 : (x > 54 ? 54 : x); }
        else if (k == "vigia") g_cfg.vigia = atoi(v.c_str()) != 0;
        else if (k == "rastro_ganchos") g_cfg.ganchos = atoi(v.c_str()) != 0;
        else if (k == "rastro_motor") g_cfg.motor = atoi(v.c_str()) != 0;
        else if (k == "rastro_registro") g_cfg.registro = atoi(v.c_str()) != 0;
        else if (k == "rastro_anime") g_cfg.anime = atoi(v.c_str()) != 0;
        else if (k == "rastro_auto_seg") { int x = atoi(v.c_str()); g_cfg.autoSeg = x < 0 ? 0 : (x > 36000 ? 36000 : x); }
    }
    fclose(f);
}

void WriteFormat() {
    FILE* f = _wfopen((s_dir + L"partido_formato.txt").c_str(), L"wb");
    if (!f) return;
    fprintf(f, "partido.bin: un registro de %u bytes por foto, little endian.\r\n", RecSize());
    fprintf(f, "+0 'PXTR' | +4 tick | +8 fase | +12 ms\r\n");
    uint32_t off = 16;
    for (auto& b : kBlks) { const int cnt = b.idx ? b.idx : 1; fprintf(f, "+%u %s x%d: 8 bytes puntero + %u bytes desde datos+0x%x (registro+0x%x%s)\r\n", off, b.name, cnt, b.len, b.from, b.ref, cnt > 1 ? ", paso 0x28" : ""); off += cnt * (8 + b.len); }
    fclose(f);
}

// ---------------------------------------------------------------- arranque / instalacion
std::atomic<bool> s_installed{false};
void Install() {
    if (s_tried.exchange(true) || !g_cfg.on) return;
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != kBase || nt->FileHeader.TimeDateStamp != kStamp) { Log("rastro: exe distinto (sello %08X): apagado", (unsigned)nt->FileHeader.TimeDateStamp); return; }
    s_cap = (uint64_t)(g_cfg.maxMb > 0 ? g_cfg.maxMb : 2000) << 20;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t name[48]; swprintf(name, 48, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    s_dir = g_cfg.baseDir + L"rastro_v4\\"; CreateDirectoryW(s_dir.c_str(), nullptr);
    s_dir += name; s_dir += L"\\"; CreateDirectoryW(s_dir.c_str(), nullptr);
    s_log = _wfopen((s_dir + L"motor.log").c_str(), L"wb");
    s_bin = _wfopen((s_dir + L"partido.bin").c_str(), L"wb");
    s_csv = _wfopen((s_dir + L"partido.csv").c_str(), L"wb");
    if (s_log) { setvbuf(s_log, nullptr, _IOFBF, 1 << 20); fputs("cuadro\ttick\thilo\tmensaje\n", s_log); }
    if (s_csv) fputs("cuadro\ttick\tfase\ttipo\tlluvia\tentorno\tescala_tiempo\tsentido_local\tsentido_visita\tjugadores\n", s_csv);
    if (g_cfg.anime) { s_anm = _wfopen((s_dir + L"anime.log").c_str(), L"wb"); if (s_anm) fputs("cuadro\ttick\tjugador\tanim_id\ta2\ta3\ta4\n", s_anm); }
    WriteFormat();
    { const MH_STATUS mi = MH_Initialize(); if (mi != MH_OK && mi != MH_ERROR_ALREADY_INITIALIZED) Log("rastro: MinHook no inicializo"); }
    bool logOk = false, relOk = false, motOk = false;
    if (g_cfg.motor && SigAt(kLogStub, kLogSig, sizeof kLogSig) && MH_CreateHook((void*)kLogStub, (void*)&hk_log, (void**)&o_log) == MH_OK && MH_EnableHook((void*)kLogStub) == MH_OK) logOk = true;
    if (g_cfg.registro && SigAt(kRegRelease, kRelSig, sizeof kRelSig) && MH_CreateHook((void*)kRegRelease, (void*)&hk_rel, (void**)&o_rel) == MH_OK && MH_EnableHook((void*)kRegRelease) == MH_OK) relOk = true;
    if (g_cfg.anime && MH_CreateHook((void*)kSetMotion, (void*)&hk_mot, (void**)&o_mot) == MH_OK && MH_EnableHook((void*)kSetMotion) == MH_OK) motOk = true;
    s_on = true;
    s_installed = true;
    Log("rastro v4 (ganchos viejos): motor %s, foto del partido %s (%u B/foto), animaciones %s -> %ls (tope %llu MB)",
        logOk ? "ok" : "NO", relOk ? "ok" : "NO", RecSize(), motOk ? "ok" : "no", name, (unsigned long long)(s_cap >> 20));
}
void Flush(bool final) {
    VgFlush();
    if (!s_on.load()) return;
    AcquireSRWLockExclusive(&s_lk);
    if (s_log) fflush(s_log); if (s_bin) fflush(s_bin); if (s_csv) fflush(s_csv); if (s_anm) fflush(s_anm);
    ReleaseSRWLockExclusive(&s_lk);
    Log("rastro: %u mensajes (%u ERROR filtrados), %u fotos, %.1f MB, %u descartados, animaciones %u%s",
        s_msgs.load(), s_skip.load(), s_snaps.load(), s_bytes.load() / 1048576.0, s_drop.load(), s_anime.load(), final ? " (final)" : "");
}

// __try/__except aislado en su propia funcion (sin objetos C++ que requieran desenrollado): evita C2712
static void SafeInstall() { __try { Install(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }

// (11/11) El vigia de offside anda SOLO desde que hay partido (solo lee memoria). Ctrl+Shift+F10 deja una MARCA numerada
// (pitido corto) en vigia.log. Ctrl+Shift+F9 ya NO prende nada salvo que rastro_v4.ini diga rastro_ganchos = 1 (ganchos viejos:
// motor.log, foto del partido, animaciones); prenderlos en un partido el 10/11 termino en un reinicio de la placa de video.
static bool KeyCombo(int vk) {
    DWORD pid = 0; HWND fg = GetForegroundWindow(); if (fg) GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId()) return false;
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) && (GetAsyncKeyState(vk) & 0x8000);
}
DWORD WINAPI Worker(void*) {
    LoadCfg();
    { CreateDirectoryW(g_cfg.baseDir.c_str(), nullptr); g_logf = _wfopen((g_cfg.baseDir + L"rastro_dll.log").c_str(), L"wb"); }
    g_cfg.vigia = g_cfg.vigia && g_cfg.on;   // sin rastro_v4.ini o con rastro = 0 no corre nada
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);   // nunca le saca tiempo al juego
    const bool viejos = g_cfg.on && g_cfg.ganchos;
    Log("rastro v4 cargado: vigia de offside %s (solo lectura, sin ganchos); ganchos viejos %s", g_cfg.vigia ? "PRENDIDO" : "apagado (vigia = 0)", viejos ? "disponibles con Ctrl+Shift+F9 (rastro_ganchos = 1)" : "apagados");
    if (g_cfg.vigia) {   // las direcciones del vigia son de este exe exacto
        HMODULE mod = GetModuleHandleW(nullptr);
        auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
        if ((uintptr_t)mod != kBase || nt->FileHeader.TimeDateStamp != kStamp) { Log("vigia: exe distinto (sello %08X): apagado", (unsigned)nt->FileHeader.TimeDateStamp); g_cfg.vigia = false; }
    }
    if (!g_cfg.vigia && !viejos) return 0;
    const ULONGLONG t0 = GetTickCount64(); ULONGLONG tKey = t0, tFlush = t0;
    FILE* marks = nullptr; int nMark = 0; bool f10Was = false, f9Was = false;
    for (;;) {
        const bool inMatch = g_cfg.vigia && VgStepSafe();
        Sleep(inMatch ? 2 : 100);
        const ULONGLONG now = GetTickCount64();
        if (now - tKey >= 100) {
            tKey = now;
            const bool f9 = KeyCombo(VK_F9), f10 = KeyCombo(VK_F10);
            if (f9 && !f9Was && !viejos) Log("rastro: Ctrl+Shift+F9 no hace nada: los ganchos viejos estan apagados. El vigia de offside anda solo.");
            if (viejos && !s_tried.load()) {
                const bool autoOn = g_cfg.autoSeg > 0 && now - t0 > (ULONGLONG)g_cfg.autoSeg * 1000 && RdPtr(kRegGlobal);
                if ((f9 && !f9Was) || autoOn) {
                    Log("rastro: prendiendo los ganchos viejos por %s (%s)", autoOn ? "rastro_auto_seg" : "Ctrl+Shift+F9", RdPtr(kRegGlobal) ? "dentro de un partido" : "fuera de un partido");
                    SafeInstall();
                    if (s_installed.load()) {
                        Beep(600, 120); Beep(1000, 160);
                        marks = _wfopen((s_dir + L"marcas.log").c_str(), L"wb");
                        if (marks) { fputs("marca\thora\ttick\n", marks); fflush(marks); }
                    } else { Beep(300, 400); Log("rastro: no se pudieron prender los ganchos viejos (ver lineas anteriores)"); }
                }
            }
            if (f10 && !f10Was) {
                SYSTEMTIME lt; GetLocalTime(&lt); nMark++;
                if (marks) { fprintf(marks, "%d\t%02d:%02d:%02d.%03d\t%d\n", nMark, lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds, s_tick.load()); fflush(marks); }
                VgLine("MARCA %d (Ctrl+Shift+F10)", nMark);
                Log("rastro: MARCA %d", nMark); Beep(1400, 70);
            }
            f9Was = f9; f10Was = f10;
        }
        if (now - tFlush >= 5000) { tFlush = now; s_frame.fetch_add(10, std::memory_order_relaxed); Flush(false); }
    }
}

} // namespace

// ---------------------------------------------------------------- entrada
#ifdef RASTRO_V4_EN_GRAFICA
// (10/11 d) Dentro de la DLL grafica (d3d11.dll): un modulo aparte hace que "DreamPatch Switcher.dll" se descargue sola y el
// juego no arranque. Mismo codigo, misma configuracion propia (rastro_v4.ini), igual de APAGADO hasta Ctrl+Shift+F9.
namespace fx {
void RastroV4Start() {
    static std::atomic<bool> once{false};
    if (once.exchange(true)) return;
    if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) CloseHandle(t);
}
void RastroV4Flush(bool final) { if (g_logf) Flush(final); }
}
#else
// (10/11) Ya NO es proxy de dinput8.dll: en la PC de Agustin ya existe un dinput8.dll de otro mod (y dsound.dll es la fisica de ropa).
// La carga un modulo Lua de Sider (rastro_v4.lua, LoadLibrary por ffi): no reemplaza ni pisa ninguna DLL.
BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        Flush(true);
    }
    return TRUE;
}
#endif
