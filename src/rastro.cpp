// PES21 FX - rastro del motor (08/11). Agustin: "que el dll extraiga lo necesario para seguir aprendiendo del funcionamiento de
// las mecanicas, animaciones, gameplay, graficos". Cheat Engine no sirve para el partido: los datos del registro del partido son
// "copias por tick" de cobra::jobgraph (Ref+0x18 apunta a una copia que solo es valida mientras un trabajo la tiene tomada), asi
// que leidos de afuera dan ?? o valores sueltos, y congelarlos cierra el juego (11:38). Esto los lee desde ADENTRO, en el momento
// en que son validos, y ademas recupera el registro de depuracion del propio juego. SOLO LECTURA: no escribe nada del juego.
//
// 1) Registro de depuracion del juego. Los programadores dejaron 119 llamadas a un "printf" de depuracion que en la version de
//    venta quedo vacio: PES2021.exe+0x23628c0 (fox::gr::dg::gnu::LogWriter::vf4 = mov [rsp+18h],r8 / mov [rsp+20h],r9 / ret).
//    Las llamadas siguen ejecutandose con sus argumentos: "SetMotion <nombre>(m:%d), startTime, playbackSpeed, duration,
//    transitionType", "SetParametricBlendMotion", "[UpdateBasePosition] SetPlayRate=%f", "Kick Action Decided action[%s] by %s",
//    "[Contact] Start keeper contact. animeNo = %s", "[trap_check] ANIMETABLE : %s", "[GetMoveMode] moveMode=%s", "[Command] ...",
//    "SetLyaer: %s m(%d) ... layerMaskType", "Select:%s (mirror=%s rot=%d)" ... Se engancha esa funcion (MinHook) y cada mensaje
//    cuyo formato este en las cadenas del exe se escribe en PES21FX\rastro\<fecha>\motor.log con el numero de cuadro, el tick del
//    partido y el hilo. Hay llamadas que no son de registro (el compilador unio funciones vacias): se filtran porque su segundo
//    argumento no es una cadena del exe.
// 2) Foto del partido una vez por segundo de juego (54 ticks). Se engancha la liberacion de referencias del registro
//    (PES2021+0xa48ae0) y SOLO cuando la llama el final del tick del partido (match::EndListener, 0x1404279c0..0x140427a80) se
//    copian, antes de liberar, bloques crudos de MatchInfo, MatchEnv, BallInfo, TeamAIInfo[2], PlayerInfo[22], FieldInfo y el
//    bloque por equipo de ModeInfo a partido.bin (registros de tamano fijo, ver partido_formato.txt) y una linea legible a
//    partido.csv (tick, fase, tipo, lluvia, escala de tiempo, sentido de ataque de cada equipo). Con eso se buscan offsets
//    comparando segundos (posicion de la pelota, de cada jugador, resistencia, mentalidad...).
// [PES21FX] rastro = 1 lo prende (0 por defecto), rastro_max_mb = tope de los archivos (defecto 2000: ~0.2 MB por segundo de juego), rastro_fotos = fotos por segundo de juego (1..54, defecto 2). Exe 1.07.02 (62787C6A) solo.
#include "core.h"
#include "MinHook.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <string>

namespace fx {

namespace {
const unsigned kStamp = 0x62787C6A;
const uintptr_t kLogStub = 0x1423628c0ull;           // printf de depuracion vacio (119 llamadas)
const unsigned char kLogSig[16] = { 0x4c,0x89,0x44,0x24,0x18,0x4c,0x89,0x4c,0x24,0x20,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc };
const uintptr_t kRegRelease = 0x140a48ae0ull;        // liberar las referencias tomadas del registro del partido
const unsigned char kRelSig[10] = { 0x80,0xb9,0xb0,0x04,0x00,0x00,0x00,0x4c,0x8b,0xc1 };
const uintptr_t kTickEndLo = 0x1404279c0ull, kTickEndHi = 0x140427a80ull;   // final del tick (EndListener)
const uintptr_t kRegGlobal = 0x143702208ull;         // puntero al registro del partido (0 fuera del partido)
const uintptr_t kRdLo = 0x142538000ull, kRdHi = 0x143a00000ull;            // cadenas del exe (.idata/.rdata del volcado)

using LogFn = void(*)(void*, const char*, ...);
using RelFn = void(*)(void*);
LogFn o_log = nullptr; RelFn o_rel = nullptr;
std::atomic<bool> s_on{false}, s_tried{false};
std::atomic<uint64_t> s_bytes{0}; uint64_t s_cap = 2000ull << 20;
std::atomic<unsigned> s_msgs{0}, s_drop{0}, s_snaps{0};
std::atomic<int> s_tick{-1};
SRWLOCK s_lk = SRWLOCK_INIT;
FILE* s_log = nullptr; FILE* s_bin = nullptr; FILE* s_csv = nullptr;
std::wstring s_dir;
int s_lastSnapTick = -1000000;

bool Rd(const void* p, void* out, size_t n) { __try { memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { memset(out, 0, n); return false; } }
uintptr_t RdPtr(uintptr_t a) { uintptr_t v = 0; if (a < 0x10000) return 0; Rd((const void*)a, &v, 8); return v; }

bool IsExeFormat(const char* f) {
    const uintptr_t a = (uintptr_t)f;
    if (a < kRdLo || a >= kRdHi) return false;
    __try {
        int pct = 0, i = 0;
        for (; i < 400 && f[i]; i++) { unsigned char c = (unsigned char)f[i]; if (c == '%') pct++; else if (c < 0x20 && c != '\n' && c != '\t' && c != '\r') return false; }
        return i >= 3 && (pct > 0 || (i > 0 && f[i - 1] == '\n') || i >= 6);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Put(FILE* f, const void* d, size_t n) {
    if (!f || s_bytes.load(std::memory_order_relaxed) + n > s_cap) { s_drop.fetch_add(1, std::memory_order_relaxed); return; }
    AcquireSRWLockExclusive(&s_lk);
    fwrite(d, 1, n, f);
    ReleaseSRWLockExclusive(&s_lk);
    s_bytes.fetch_add(n, std::memory_order_relaxed);
}

int Fmt(char* buf, size_t cap, const char* fmt, va_list ap) {
    __try { return vsnprintf(buf, cap, fmt, ap); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

void hk_log(void* self, const char* fmt, ...) {
    if (!s_on.load(std::memory_order_relaxed) || !IsExeFormat(fmt)) return;
    char msg[1024];
    va_list ap; va_start(ap, fmt);
    int n = Fmt(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (n < 0) { snprintf(msg, sizeof msg, "(no se pudo formatear) %.200s", fmt); n = (int)strlen(msg); }
    if (n >= (int)sizeof msg) n = (int)sizeof msg - 1;
    while (n > 0 && (msg[n - 1] == '\n' || msg[n - 1] == '\r')) msg[--n] = 0;
    for (int i = 0; i < n; i++) if (msg[i] == '\n' || msg[i] == '\r') msg[i] = ' ';
    char line[1200];
    int m = snprintf(line, sizeof line, "%d\t%d\t%lu\t%s\n", PresentNo(), s_tick.load(std::memory_order_relaxed), GetCurrentThreadId(), msg);
    if (m > 0) Put(s_log, line, (size_t)(m < (int)sizeof line ? m : (int)sizeof line - 1));
    s_msgs.fetch_add(1, std::memory_order_relaxed);
}

// ---- foto del partido (formato en partido_formato.txt)
struct Blk { const char* name; int ref; int idx; uint32_t from, len; };
// ref = offset en el registro donde esta el puntero a los datos (Ref+0x18); para los arreglos, base + idx*0x28
// todas las referencias del registro (36) + los 7 arreglos; ref = offset del puntero a los datos (Ref+0x18), idx = cantidad (paso 0x28)
const Blk kBlks[] = {
    { "Screen2dInfo", 0x18, 0, 0x0, 0x400 },
    { "BallInfo", 0x40, 0, 0x0, 0x2c00 },
    { "CameraSettings", 0x68, 0, 0x0, 0x400 },
    { "ClothJudgeInfo", 0x90, 0, 0x0, 0x400 },
    { "CommandOutput", 0xb8, 0, 0x0, 0x400 },
    { "CornerFlagInfo", 0xe0, 0, 0x0, 0x400 },
    { "ConceptArrangeReleaseInfo", 0x108, 0, 0x0, 0x400 },
    { "VanishingSprayInfo", 0x130, 0, 0x0, 0x400 },
    { "GoalPostInfo", 0x158, 0, 0x0, 0x400 },
    { "CursorInfo", 0x180, 0, 0x0, 0x400 },
    { "DemoInfo", 0x1a8, 0, 0x0, 0x400 },
    { "DemoControl", 0x1d0, 0, 0x0, 0x400 },
    { "FixDemoInfo", 0x1f8, 0, 0x0, 0x400 },
    { "GeneralSettings", 0x220, 0, 0x0, 0x400 },
    { "GPAchievementInfo", 0x248, 0, 0x0, 0x400 },
    { "MatchInfo", 0x270, 0, 0x0, 0x1800 },
    { "ESports", 0x298, 0, 0x0, 0x400 },
    { "MatchEnv", 0x2c0, 0, 0x0, 0x200 },
    { "NoOperationLeaveInfo", 0x2e8, 0, 0x0, 0x400 },
    { "PkMatch", 0x310, 0, 0x0, 0x400 },
    { "PlayData", 0x338, 0, 0x0, 0x400 },
    { "RandomInfo", 0x360, 0, 0x0, 0x400 },
    { "ReplayInfo", 0x388, 0, 0x0, 0x400 },
    { "StepupTutorial", 0x3b0, 0, 0x0, 0x400 },
    { "SystemSettings", 0x3d8, 0, 0x0, 0x400 },
    { "TrainingWork", 0x400, 0, 0x0, 0x400 },
    { "AnalyzeInfo", 0x2968, 0, 0x0, 0x400 },
    { "CameraInfo", 0x2990, 0, 0x0, 0x400 },
    { "CommandInfo", 0x29b8, 0, 0x0, 0x400 },
    { "PadInput", 0x29e0, 0, 0x0, 0x400 },
    { "RagdollInfo", 0x2a08, 0, 0x0, 0x400 },
    { "RecordInfo", 0x2a30, 0, 0x0, 0x400 },
    { "BallAttach", 0x2a58, 0, 0x0, 0x400 },
    { "SituationInfo", 0x2a80, 0, 0x0, 0x400 },
    { "FieldInfo", 0x2aa8, 0, 0x0, 0x200 },
    { "ModeInfo", 0x2ad0, 0, 0x0, 0x400 },
    { "ModeInfo_equipo0", 0x2ad0, 0, 0xc400, 0x2804 },
    { "ModeInfo_equipo1", 0x2ad0, 0, 0xc400 + 0x2804, 0x2804 },
    { "TeamAIInfo", 0x1928, 2, 0x0, 0x1000 },
    { "OrderInfo", 0x1978, 2, 0x0, 0x800 },
    { "AnimeInfo", 0x19c8, 26, 0x0, 0x400 },
    { "PlayerMove", 0x1dd8, 26, 0x0, 0x400 },
    { "DemoPlayerInfo", 0x21e8, 26, 0x0, 0x200 },
    { "PlayerInfo", 0x25f8, 22, 0x0, 0x800 },
};

uint32_t RecSize() {
    uint32_t s = 16;
    for (auto& b : kBlks) s += (b.idx ? b.idx : 1) * (8 + b.len);
    return s;
}

void Snapshot() {
    const uintptr_t reg = RdPtr(kRegGlobal);
    if (!reg) return;
    const uintptr_t mi = RdPtr(reg + 0x270);
    int tick = -1, phase = -1, type = -1;
    if (mi) { Rd((const void*)(mi + 0x1198), &tick, 4); Rd((const void*)(mi + 0x1168), &phase, 4); Rd((const void*)(mi + 0x116c), &type, 4); }
    s_tick.store(tick, std::memory_order_relaxed);
    const int step = 54 / (g_cfg.rastroFps > 0 ? (g_cfg.rastroFps > 54 ? 54 : g_cfg.rastroFps) : 2);
    if (tick < 0 || (tick - s_lastSnapTick < step && tick >= s_lastSnapTick)) return;
    s_lastSnapTick = tick;
    static std::string rec; rec.assign(RecSize(), '\0');
    char* p = &rec[0];
    const uint32_t magic = 0x52545850; /* 'PXTR' */ const uint32_t ms = (uint32_t)GetTickCount();
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
    // linea legible
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

void hk_rel(void* view) {
    const uintptr_t ra = (uintptr_t)_ReturnAddress();
    if (s_on.load(std::memory_order_relaxed) && ra > kTickEndLo && ra < kTickEndHi) {
        __try { Snapshot(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    o_rel(view);
}

bool SigAt(uintptr_t a, const unsigned char* s, size_t n) {
    unsigned char b[32] = {};
    return n <= sizeof b && Rd((const void*)a, b, n) && memcmp(b, s, n) == 0;
}

void WriteFormat() {
    FILE* f = _wfopen((s_dir + L"partido_formato.txt").c_str(), L"wb");
    if (!f) return;
    fprintf(f, "partido.bin: un registro de %u bytes por foto (rastro_fotos por segundo de juego; el juego corre a 54 ticks por segundo), little endian.\r\n", RecSize());
    fprintf(f, "+0 'PXTR' | +4 tick (MatchInfo+0x1198) | +8 fase (MatchInfo+0x1168) | +12 GetTickCount ms\r\n");
    uint32_t off = 16;
    for (auto& b : kBlks) {
        const int cnt = b.idx ? b.idx : 1;
        fprintf(f, "+%u %s x%d: por cada uno 8 bytes = puntero a los datos, despues %u bytes desde datos+0x%x (registro+0x%x%s)\r\n",
            off, b.name, cnt, b.len, b.from, b.ref, cnt > 1 ? ", paso 0x28" : "");
        off += cnt * (8 + b.len);
    }
    fprintf(f, "\r\npartido.csv: cuadro, tick, fase, tipo, lluvia (MatchEnv+0x12), entorno (+0x44), escala de tiempo (+0x5c), sentido de ataque local/visitante (TeamAIInfo+0x254), jugadores con puntero\r\n");
    fprintf(f, "motor.log: cuadro, tick del partido (-1 fuera), hilo, mensaje del registro de depuracion del juego\r\n");
    fclose(f);
}
} // namespace

void RastroPresent() {
    if (s_tried.load(std::memory_order_relaxed) || !g_cfg.rastroOn) return;
    s_tried = true;
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("rastro: exe distinto (sello %08X): apagado", (unsigned)nt->FileHeader.TimeDateStamp); return; }
    s_cap = (uint64_t)(g_cfg.rastroMaxMb > 0 ? g_cfg.rastroMaxMb : 2000) << 20;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t name[48]; swprintf(name, 48, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    s_dir = g_cfg.baseDir + L"rastro\\"; CreateDirectoryW(s_dir.c_str(), nullptr);
    s_dir += name; s_dir += L"\\"; CreateDirectoryW(s_dir.c_str(), nullptr);
    s_log = _wfopen((s_dir + L"motor.log").c_str(), L"wb");
    s_bin = _wfopen((s_dir + L"partido.bin").c_str(), L"wb");
    s_csv = _wfopen((s_dir + L"partido.csv").c_str(), L"wb");
    if (s_log) { setvbuf(s_log, nullptr, _IOFBF, 1 << 20); fputs("cuadro\ttick\thilo\tmensaje\n", s_log); }
    if (s_csv) fputs("cuadro\ttick\tfase\ttipo\tlluvia\tentorno\tescala_tiempo\tsentido_local\tsentido_visita\tjugadores\n", s_csv);
    WriteFormat();
    bool logOk = false, relOk = false;
    if (SigAt(kLogStub, kLogSig, sizeof kLogSig) && MH_CreateHook((void*)kLogStub, (void*)&hk_log, (void**)&o_log) == MH_OK && MH_EnableHook((void*)kLogStub) == MH_OK) logOk = true;
    if (SigAt(kRegRelease, kRelSig, sizeof kRelSig) && MH_CreateHook((void*)kRegRelease, (void*)&hk_rel, (void**)&o_rel) == MH_OK && MH_EnableHook((void*)kRegRelease) == MH_OK) relOk = true;
    s_on = logOk || relOk;
    Log("rastro: registro de depuracion del juego %s, foto del partido %s -> PES21FX\\rastro\\%ls\\ (tope %llu MB)",
        logOk ? "enganchado" : "NO (firma distinta)", relOk ? "enganchada" : "NO (firma distinta)", name, (unsigned long long)(s_cap >> 20));
}

// resumen cada tanto (lo llama el medidor de 10 s) y vaciado al cerrar
void RastroFlush(bool final) {
    if (!s_on.load()) return;
    AcquireSRWLockExclusive(&s_lk);
    if (s_log) fflush(s_log); if (s_bin) fflush(s_bin); if (s_csv) fflush(s_csv);
    ReleaseSRWLockExclusive(&s_lk);
    static unsigned lastMsgs = 0;
    const unsigned m = s_msgs.load();
    if (final || m != lastMsgs) Log("rastro: %u mensajes del juego, %u fotos del partido, %.1f MB, %u descartados por el tope%s",
        m, s_snaps.load(), s_bytes.load() / 1048576.0, s_drop.load(), final ? " (final)" : "");
    lastMsgs = m;
}

} // namespace fx
