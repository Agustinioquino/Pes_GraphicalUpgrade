// PES21 FX - Rastro v4: DLL de INVESTIGACION, aparte de la DLL grafica (d3d11). Se inyecta como proxy de dinput8.dll al lado del
// exe y no toca nada del mod grafico ni de la tela. Solo LEE. Junta y mejora el rastro v2 + v3:
//   1. Vuelca el REGISTRO ENTERO del partido cada tick (todos los bloques, objeto completo de cada jugador) -> partido.bin.
//   2. Recupera el registro de depuracion interno del juego (printf vacio PES2021+0x23628c0) -> motor.log, filtrando el ruido.
//   3. Vigia por hardware (DR0-DR2) quien escribe el registro de faltas de MatchControl (el "pitido" del offside) -> falta.log.
//   4. Engancha SetMotion (0x1406f17f0) para saber QUE jugador arranca cada animacion -> anime.log.
//   5. Rastreador de funciones: para cada direccion de la lista [PES21FX] rastro_funcs = ..., pone un int3 al inicio y anota, la
//      primera vez que corre, los argumentos (rcx, rdx, r8, r9, xmm0) y quien la llamo. Sirve para ver que hace una funcion que el
//      codigo quieto no aclara. Solo lee el contexto y restaura el byte: no cambia la ejecucion.
// Config: PES21FX\PES21FX.ini, claves rastro=1, rastro_fotos, rastro_max_mb, rastro_faltas, rastro_anime, rastro_funcs.
// No depende de core.h: todo lo necesario (Log, config, lectura segura) esta aca.
#include <windows.h>
#include <unknwn.h>
#include <objbase.h>
#include <tlhelp32.h>
#include <intrin.h>
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
const uintptr_t kMcVf19     = 0x1404228d0ull;   // MatchControl::vf19, una vez por tick: da el puntero al objeto
const unsigned char kMcSig[16] = { 0x48,0x8b,0xc4,0x48,0x89,0x50,0x10,0x55,0x53,0x56,0x57,0x41,0x56,0x48,0x8b,0xec };

// faltas: registro de la ultima falta en MatchControl +0x2940 (se copia a MatchInfo+0x1584 cada tick)
const uint32_t kWatchOff[3]    = { 0x2944, 0x294c, 0x2930 };
const char* const kWatchName[3] = { "tipo_falta(MI+0x1588)", "infractor(MI+0x1590)", "motivo_detencion(MI+0x1574)" };

// ---------------------------------------------------------------- configuracion propia
struct Cfg {
    std::wstring baseDir;
    bool on = false; int maxMb = 2000, fps = 2; bool falta = true, anime = true;
    std::vector<uintptr_t> funcs;
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
FILE* s_log = nullptr; FILE* s_bin = nullptr; FILE* s_csv = nullptr; FILE* s_falta = nullptr; FILE* s_anm = nullptr; FILE* s_fn = nullptr;
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
    static std::string rec; rec.assign(RecSize(), '\0');
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

// ---------------------------------------------------------------- vigia de faltas por hardware (DR0-DR2)
using Vf19Fn = void(*)(void*, void*);
Vf19Fn o_vf19 = nullptr;
std::atomic<uintptr_t> s_mc{0}, s_armedMc{0};
struct FaltaHit { uint32_t ms; int tick; int slot; uint32_t val; uint32_t tid; uintptr_t rip; uintptr_t ret[12]; };
const int kFaltaN = 4096; FaltaHit s_fh[kFaltaN];
std::atomic<int> s_fhN{0}; int s_fhDone = 0;
uint32_t s_fhLast[3] = { 0xffffffffu, 0xffffffffu, 0xffffffffu }; uintptr_t s_fhLastRip[3] = {};
std::atomic<unsigned> s_fhAll{0};

bool LooksLikeRet(uintptr_t v) {
    if (v < 0x140001000ull || v >= kRdLo) return false;
    unsigned char b[7] = {};
    if (!Rd((const void*)(v - 7), b, 7)) return false;
    return b[2] == 0xe8 || (b[1] == 0xff && b[2] == 0x15) || (b[4] == 0xff && (b[5] & 0x38) == 0x10) ||
           (b[5] == 0xff && (b[6] & 0x38) == 0x10) || (b[3] == 0xff && (b[4] & 0x38) == 0x10) || (b[0] == 0xff && (b[1] & 0x38) == 0x10);
}
// rastreador de funciones por int3 (comparte el VEH); 1 = tipo de hit de falta, 2 = funcion rastreada
struct TrFn { uintptr_t addr; unsigned char orig; std::atomic<unsigned char> state; };   // state 1 armada, 2 ejecutada
std::vector<TrFn*> s_trace;
std::atomic<unsigned> s_trHits{0};
TrFn* FindTrace(uintptr_t a) { for (auto* t : s_trace) if (t->addr == a) return t; return nullptr; }

LONG CALLBACK Veh(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    CONTEXT* cx = ep->ContextRecord;
    if (code == EXCEPTION_SINGLE_STEP) {   // vigia de faltas (hardware)
        const uintptr_t mc = s_armedMc.load(std::memory_order_relaxed);
        if (!mc || !(cx->Dr6 & 7) || cx->Dr0 != mc + kWatchOff[0]) return EXCEPTION_CONTINUE_SEARCH;
        const int slot = (cx->Dr6 & 1) ? 0 : (cx->Dr6 & 2) ? 1 : 2;
        cx->Dr6 = 0; s_fhAll.fetch_add(1, std::memory_order_relaxed);
        uint32_t v = 0; Rd((const void*)(mc + kWatchOff[slot]), &v, 4);
        if (v == s_fhLast[slot] && (uintptr_t)cx->Rip == s_fhLastRip[slot]) return EXCEPTION_CONTINUE_EXECUTION;
        s_fhLast[slot] = v; s_fhLastRip[slot] = (uintptr_t)cx->Rip;
        const int i = s_fhN.fetch_add(1, std::memory_order_relaxed);
        if (i < kFaltaN) {
            FaltaHit& h = s_fh[i]; memset(&h, 0, sizeof h);
            h.ms = (uint32_t)GetTickCount(); h.tick = s_tick.load(std::memory_order_relaxed); h.slot = slot; h.val = v; h.tid = GetCurrentThreadId(); h.rip = (uintptr_t)cx->Rip;
            const uintptr_t* sp = (const uintptr_t*)cx->Rsp; int k = 0;
            for (int j = 0; j < 512 && k < 12; j++) { uintptr_t r = 0; if (!Rd(sp + j, &r, 8)) break; if (LooksLikeRet(r)) h.ret[k++] = r; }
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_BREAKPOINT) {   // rastreador de funciones (int3)
        const uintptr_t a = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
        TrFn* t = FindTrace(a); if (!t) return EXCEPTION_CONTINUE_SEARCH;
        unsigned char st = 1;
        if (t->state.compare_exchange_strong(st, 2)) {
            *(volatile unsigned char*)a = t->orig; FlushInstructionCache(GetCurrentProcess(), (void*)a, 1);
            if (s_fn) {
                double x0 = 0; Rd(&cx->Xmm0, &x0, 8);
                char line[320];
                int n = snprintf(line, sizeof line, "%d\t%s\trcx=%llx rdx=%llx r8=%llx r9=%llx xmm0=%g\t<-%s\n",
                    s_tick.load(std::memory_order_relaxed), ModOff((void*)a).c_str(),
                    (unsigned long long)cx->Rcx, (unsigned long long)cx->Rdx, (unsigned long long)cx->R8, (unsigned long long)cx->R9, x0,
                    ModOff((void*)(*(uintptr_t*)cx->Rsp)).c_str());
                if (n > 0) { AcquireSRWLockExclusive(&s_lk); fwrite(line, 1, n < (int)sizeof line ? n : (int)sizeof line - 1, s_fn); fflush(s_fn); ReleaseSRWLockExclusive(&s_lk); }
            }
            s_trHits.fetch_add(1, std::memory_order_relaxed);
        }
        cx->Rip = (DWORD64)a;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI FaltaArm(void* arg) {
    const uintptr_t mc = (uintptr_t)arg;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te{}; te.dwSize = sizeof te;
    const DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId(); int done = 0, tot = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        tot++;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!th) continue;
        if (SuspendThread(th) != (DWORD)-1) {
            CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(th, &c)) {
                DWORD64 d7 = c.Dr7;
                for (int s = 0; s < 3; s++) { d7 &= ~((DWORD64)3 << (2 * s)); d7 &= ~((DWORD64)0xF << (16 + 4 * s)); d7 |= ((DWORD64)1 << (2 * s)) | ((DWORD64)1 << (16 + 4 * s)) | ((DWORD64)3 << (18 + 4 * s)); }
                c.Dr0 = mc + kWatchOff[0]; c.Dr1 = mc + kWatchOff[1]; c.Dr2 = mc + kWatchOff[2]; c.Dr7 = d7;
                if (SetThreadContext(th, &c)) done++;
            }
            ResumeThread(th);
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
    static int s_logs = 0; if (s_logs++ < 6) Log("rastro: vigia de faltas en MatchControl %p: %d de %d hilos", (void*)mc, done, tot);
    return 0;
}
void FaltaRequestArm(uintptr_t mc) {
    s_armedMc.store(mc, std::memory_order_relaxed);
    for (int s = 0; s < 3; s++) { s_fhLast[s] = 0xffffffffu; s_fhLastRip[s] = 0; }
    if (HANDLE t = CreateThread(nullptr, 0, FaltaArm, (void*)mc, 0, nullptr)) CloseHandle(t);
}
void hk_vf19(void* self, void* a2) {
    const uintptr_t me = (uintptr_t)self;
    if (me && s_mc.load(std::memory_order_relaxed) != me && !(me & 3)) { s_mc.store(me, std::memory_order_relaxed); FaltaRequestArm(me); }
    o_vf19(self, a2);
}
void FaltaFlush() {
    if (!s_falta) return;
    const int n = s_fhN.load() < kFaltaN ? s_fhN.load() : kFaltaN;
    for (; s_fhDone < n; s_fhDone++) {
        const FaltaHit& h = s_fh[s_fhDone];
        fprintf(s_falta, "%u\t%d\t%s\t%u\t%u\t%s", h.ms, h.tick, kWatchName[h.slot], h.val, h.tid, ModOff((void*)h.rip).c_str());
        for (int k = 0; k < 12 && h.ret[k]; k++) fprintf(s_falta, "%s%s", k ? " < " : "\t", ModOff((void*)h.ret[k]).c_str());
        fputs("\n", s_falta);
    }
    fflush(s_falta);
}

// armar los int3 del rastreador de funciones (una vez)
void ArmTrace() {
    for (uintptr_t a : g_cfg.funcs) {
        if (a < kBase || a >= kBase + 0x1c000000ull) { Log("rastro_funcs: %llx fuera del exe, salteada", (unsigned long long)a); continue; }
        unsigned char b = 0; if (!Rd((const void*)a, &b, 1) || b == 0xCC) { Log("rastro_funcs: %llx no legible o ya con int3", (unsigned long long)a); continue; }
        auto* t = new TrFn{ a, b, {1} };
        DWORD old; if (VirtualProtect((void*)a, 1, PAGE_EXECUTE_READWRITE, &old)) {
            *(volatile unsigned char*)a = 0xCC; FlushInstructionCache(GetCurrentProcess(), (void*)a, 1);
            VirtualProtect((void*)a, 1, old, &old);
            s_trace.push_back(t);
        } else { delete t; Log("rastro_funcs: no se pudo armar %llx", (unsigned long long)a); }
    }
    if (!s_trace.empty()) Log("rastro: rastreador de funciones armado en %zu direcciones -> funcs.log", s_trace.size());
}

// ---------------------------------------------------------------- config (PES21FX.ini)
void LoadCfg() {
    // baseDir = carpeta del exe + PES21FX\ ;  ini
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* sl = wcsrchr(exe, L'\\'); if (sl) sl[1] = 0;
    g_cfg.baseDir = std::wstring(exe) + L"PES21FX\\";
    FILE* f = _wfopen((g_cfg.baseDir + L"PES21FX.ini").c_str(), L"rb");
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
        else if (k == "rastro_faltas") g_cfg.falta = atoi(v.c_str()) != 0;
        else if (k == "rastro_anime") g_cfg.anime = atoi(v.c_str()) != 0;
        else if (k == "rastro_funcs") {
            const char* p = v.c_str();
            while (*p) { char* e = nullptr; unsigned long long a = _strtoui64(p, &e, 16); if (e == p) break; if (a) g_cfg.funcs.push_back((uintptr_t)a); p = e; while (*p == ',' || *p == ' ' || *p == '\t') p++; }
        }
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
    s_dir = g_cfg.baseDir + L"rastro\\"; CreateDirectoryW(s_dir.c_str(), nullptr);
    s_dir += name; s_dir += L"\\"; CreateDirectoryW(s_dir.c_str(), nullptr);
    s_log = _wfopen((s_dir + L"motor.log").c_str(), L"wb");
    s_bin = _wfopen((s_dir + L"partido.bin").c_str(), L"wb");
    s_csv = _wfopen((s_dir + L"partido.csv").c_str(), L"wb");
    if (s_log) { setvbuf(s_log, nullptr, _IOFBF, 1 << 20); fputs("cuadro\ttick\thilo\tmensaje\n", s_log); }
    if (s_csv) fputs("cuadro\ttick\tfase\ttipo\tlluvia\tentorno\tescala_tiempo\tsentido_local\tsentido_visita\tjugadores\n", s_csv);
    if (g_cfg.anime) { s_anm = _wfopen((s_dir + L"anime.log").c_str(), L"wb"); if (s_anm) fputs("cuadro\ttick\tjugador\tanim_id\ta2\ta3\ta4\n", s_anm); }
    if (!g_cfg.funcs.empty()) { s_fn = _wfopen((s_dir + L"funcs.log").c_str(), L"wb"); if (s_fn) fputs("tick\tfuncion\targumentos\tllamada_desde\n", s_fn); }
    WriteFormat();
    if (MH_Initialize() != MH_OK) { Log("rastro: MinHook no inicializo"); }
    bool logOk = false, relOk = false, motOk = false;
    if (SigAt(kLogStub, kLogSig, sizeof kLogSig) && MH_CreateHook((void*)kLogStub, (void*)&hk_log, (void**)&o_log) == MH_OK && MH_EnableHook((void*)kLogStub) == MH_OK) logOk = true;
    if (SigAt(kRegRelease, kRelSig, sizeof kRelSig) && MH_CreateHook((void*)kRegRelease, (void*)&hk_rel, (void**)&o_rel) == MH_OK && MH_EnableHook((void*)kRegRelease) == MH_OK) relOk = true;
    if (g_cfg.anime && MH_CreateHook((void*)kSetMotion, (void*)&hk_mot, (void**)&o_mot) == MH_OK && MH_EnableHook((void*)kSetMotion) == MH_OK) motOk = true;
    bool mcOk = false;
    if (g_cfg.falta && SigAt(kMcVf19, kMcSig, sizeof kMcSig)) {
        s_falta = _wfopen((s_dir + L"falta.log").c_str(), L"wb");
        if (s_falta) fputs("ms\ttick\tcampo\tvalor_nuevo\thilo\tinstruccion_que_escribe\tretornos\n", s_falta);
        AddVectoredExceptionHandler(1, Veh);
        mcOk = MH_CreateHook((void*)kMcVf19, (void*)&hk_vf19, (void**)&o_vf19) == MH_OK && MH_EnableHook((void*)kMcVf19) == MH_OK;
    } else if (!g_cfg.funcs.empty()) {
        AddVectoredExceptionHandler(1, Veh);
    }
    if (!g_cfg.funcs.empty()) ArmTrace();
    s_on = logOk || relOk;
    s_installed = true;
    Log("rastro v4: motor %s, foto del partido %s (%u B/foto), animaciones %s, faltas %s, funcs %zu -> %ls (tope %llu MB)",
        logOk ? "ok" : "NO", relOk ? "ok" : "NO", RecSize(), motOk ? "ok" : "no", !g_cfg.falta ? "apagado" : mcOk ? "ok" : "NO", g_cfg.funcs.size(), name, (unsigned long long)(s_cap >> 20));
}
void Flush(bool final) {
    if (!s_on.load()) return;
    AcquireSRWLockExclusive(&s_lk);
    if (s_log) fflush(s_log); if (s_bin) fflush(s_bin); if (s_csv) fflush(s_csv); if (s_anm) fflush(s_anm);
    FaltaFlush();
    if (!final) { const uintptr_t mc = s_mc.load(); if (mc) { static ULONGLONG t = 0; ULONGLONG n = GetTickCount64(); if (n - t > 30000) { t = n; FaltaRequestArm(mc); } } }
    ReleaseSRWLockExclusive(&s_lk);
    Log("rastro: %u mensajes (%u ERROR filtrados), %u fotos, %.1f MB, %u descartados, faltas %u, animaciones %u, funcs %u%s",
        s_msgs.load(), s_skip.load(), s_snaps.load(), s_bytes.load() / 1048576.0, s_drop.load(), s_fhAll.load(), s_anime.load(), s_trHits.load(), final ? " (final)" : "");
}

// __try/__except aislado en su propia funcion (sin objetos C++ que requieran desenrollado): evita C2712
static void SafeInstall() { __try { Install(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }

DWORD WINAPI Worker(void*) {
    LoadCfg();
    { CreateDirectoryW(g_cfg.baseDir.c_str(), nullptr); g_logf = _wfopen((g_cfg.baseDir + L"rastro_dll.log").c_str(), L"wb"); }
    Log("rastro v4 (DLL de investigacion) cargada; rastro=%d", g_cfg.on ? 1 : 0);
    for (int i = 0; i < 1200 && !s_installed.load(); i++) {   // espera a que el exe este listo (hasta 10 min)
        if (RdPtr(kRegGlobal) || i > 20) SafeInstall();
        Sleep(500);
        s_frame.fetch_add(1, std::memory_order_relaxed);
        if (s_installed.load() && (i % 10) == 0) Flush(false);
    }
    while (s_installed.load()) { Sleep(5000); s_frame.fetch_add(10, std::memory_order_relaxed); Flush(false); }
    return 0;
}

} // namespace

// ---------------------------------------------------------------- proxy de dinput8.dll (reenvia al real)
namespace {
HMODULE g_real = nullptr;
using DI8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DllGetClassObject_t = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
using DllCanUnload_t = HRESULT(WINAPI*)();
using DllReg_t = HRESULT(WINAPI*)();
using GetdfDIJoystick_t = LPVOID(WINAPI*)();
DI8Create_t p_DI8Create = nullptr; DllGetClassObject_t p_DllGetClassObject = nullptr; DllCanUnload_t p_DllCanUnload = nullptr;
DllReg_t p_DllRegister = nullptr, p_DllUnregister = nullptr; GetdfDIJoystick_t p_GetdfDIJoystick = nullptr;
void LoadReal() {
    wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH); wcscat_s(sys, L"\\dinput8.dll");
    g_real = LoadLibraryW(sys);
    if (!g_real) return;
    p_DI8Create = (DI8Create_t)GetProcAddress(g_real, "DirectInput8Create");
    p_DllGetClassObject = (DllGetClassObject_t)GetProcAddress(g_real, "DllGetClassObject");
    p_DllCanUnload = (DllCanUnload_t)GetProcAddress(g_real, "DllCanUnloadNow");
    p_DllRegister = (DllReg_t)GetProcAddress(g_real, "DllRegisterServer");
    p_DllUnregister = (DllReg_t)GetProcAddress(g_real, "DllUnregisterServer");
    p_GetdfDIJoystick = (GetdfDIJoystick_t)GetProcAddress(g_real, "GetdfDIJoystick");
}
}

extern "C" {
HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID* o, LPUNKNOWN u) { return p_DI8Create ? p_DI8Create(h, v, r, o, u) : E_FAIL; }
HRESULT WINAPI DllGetClassObject(REFCLSID c, REFIID r, LPVOID* o) { return p_DllGetClassObject ? p_DllGetClassObject(c, r, o) : E_FAIL; }
HRESULT WINAPI DllCanUnloadNow() { return p_DllCanUnload ? p_DllCanUnload() : S_FALSE; }
HRESULT WINAPI DllRegisterServer() { return p_DllRegister ? p_DllRegister() : E_FAIL; }
HRESULT WINAPI DllUnregisterServer() { return p_DllUnregister ? p_DllUnregister() : E_FAIL; }
LPVOID WINAPI GetdfDIJoystick() { return p_GetdfDIJoystick ? p_GetdfDIJoystick() : nullptr; }
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        LoadReal();
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        Flush(true);
    }
    return TRUE;
}
