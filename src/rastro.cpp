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
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <string>
#include <tlhelp32.h>

namespace fx {

std::atomic<int> g_simTick{-1};   // (12/11) tick del partido para el desgaste real (pitchwear.cpp), lo escribe hk_rel al final de cada tick
std::atomic<unsigned long long> g_replayMs{0};   // (12/11 16:45) visor de repeticiones reproduciendo (ver hk_rpl)
std::atomic<int> g_replayHook{0}, g_replayCalls{0}, g_replayPlay{0};
std::atomic<unsigned long long> g_replayMoveMs{0};   // (14/11 g) ultima llamada del visor con el cursor avanzando, sin depender de y448 (camara libre / lenta en la repeticion)
std::atomic<int> g_replayCallsV2{0}, g_replayPlayV2{0}, g_replayMoveV2{0};   // (13/11 tanda 2) y448: llamadas / estado 5 / con el cursor avanzando (log "repeticiones v2" de pitchwear.cpp)

std::string ModOff(void* p);

namespace {
const unsigned kStamp = 0x62787C6A;
const uintptr_t kLogStub = 0x1423628c0ull;           // printf de depuracion vacio (119 llamadas)
const unsigned char kLogSig[16] = { 0x4c,0x89,0x44,0x24,0x18,0x4c,0x89,0x4c,0x24,0x20,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc };
const uintptr_t kRegRelease = 0x140a48ae0ull;        // liberar las referencias tomadas del registro del partido
const unsigned char kRelSig[10] = { 0x80,0xb9,0xb0,0x04,0x00,0x00,0x00,0x4c,0x8b,0xc1 };
const uintptr_t kTickEndLo = 0x1404279c0ull, kTickEndHi = 0x140427a80ull;   // final del tick (EndListener)
const uintptr_t kRegGlobal = 0x143702208ull;         // puntero al registro del partido (0 fuera del partido)
const uintptr_t kRdLo = 0x142538000ull, kRdHi = 0x143a00000ull;            // cadenas del exe (.idata/.rdata del volcado)
// (12/11 16:45, Agustin: "en la repeticion?, tenemos el slowmoprobe en lua modulos") REPETICION: FUN_14217c0b0 = control de la repeticion con
// el mando (avance / retroceso / palanca, velocidad en +0x1c). La llama FUN_14217ea90 (actualizacion del objeto de repeticion, al lado de
// match::ReplayObjectListener) en cada cuadro del visor, antes de FUN_14217bf20 (que solo avanza con el estado [0] == 5 = reproduciendo).
// Devuelve dos floats en RAX (CONCAT44). SlowMoServer.lua le parchea los mulss de +0x83 / +0xa4 / +0xd2, no el prologo (bytes del exe
// 1.07.02 leidos de su disco: mov [rsp+8],rbx / mov [rsp+18],rsi / push rdi / sub rsp,40 / mov rdi,rcx)
const uintptr_t kReplayCtl = 0x14217c0b0ull;
const unsigned char kReplaySig[18] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0xf9 };
using RplFn = uint64_t(*)(void*, unsigned, unsigned char);
RplFn o_rpl = nullptr;
// (13/11 tanda 2) y448 REPETICIONES v2: en su log todas las llamadas del partido en vivo venian con el estado 5 (~54 por segundo, el tick
// del partido), asi que "reproduciendo" no distinguia nada. Con y448 solo cuenta si el cursor (+0x14) cambio desde la llamada anterior
// del mismo objeto o el paso (+0x18) no es cero [offsets del decompilado: a confirmar con el log "repeticiones v2"]. y448 0 = como antes
struct RplCur { const void* obj; unsigned cur; };
RplCur s_rplCur[4] = {}; int s_rplCurNext = 0;
bool RplCursorMoved(const void* obj, unsigned cur) {
    for (int i = 0; i < 4; i++) if (s_rplCur[i].obj == obj) { const bool m = s_rplCur[i].cur != cur; s_rplCur[i].cur = cur; return m; }
    s_rplCur[s_rplCurNext].obj = obj; s_rplCur[s_rplCurNext].cur = cur; s_rplCurNext = (s_rplCurNext + 1) & 3; return false;   // primera vez: sin dato
}
// (14/11 g, Agustin: "las opciones de avanzar o pausar no funcionan ... deberia acceder a las opciones reales de pausa, avanzar")
// MODO FOTO EN LA REPETICION: el reloj del partido no mueve el visor, asi que la pausa / avance / pasos del modo foto se aplican a la
// LINEA DE TIEMPO del visor con el salto del propio juego FUN_14217e800(linea, cuadro) (leido por el hilo de la repeticion 9vcq5h:
// recorta a [primero, ultimo], escribe +0x238 y +0x14 y pone el paso en 0). Despues del control del visor: una posicion propia que avanza
// lo que diga el modo foto por llamada (0 en pausa, 1 = normal) y, si el juego la corrio, se vuelve a ella.
bool SigAt(uintptr_t a, const unsigned char* s, size_t n);   // mas abajo (mismo espacio de nombres)
bool Rd(const void* p, void* out, size_t n);
const uintptr_t kRplSeek = 0x14217e800ull;
const unsigned char kRplSeekSig[10] = { 0x0f,0xb7,0x81,0x3c,0x02,0x00,0x00,0x4c,0x8b,0xc9 };
using RplSeekFn = void(*)(void*, unsigned short);
int s_seekOk = -1; const void* s_pfLine = nullptr; double s_pf = 0.0; int s_pfLogs = 0;
// constante de la velocidad lenta del visor: la que lee el mulss del exe (0.5 / -0.5 originales, o la de SlowMoServer.lua si la cambio)
float RplSpeedConst(uintptr_t mulss, float def) {
    static const unsigned char kMulss[4] = { 0xf3, 0x0f, 0x59, 0x35 };
    if (!SigAt(mulss, kMulss, 4)) return def;
    int disp = 0; if (!Rd((const void*)(mulss + 4), &disp, 4)) return def;
    const uintptr_t a = mulss + 8 + (intptr_t)disp;
    float v = 0.0f; if (!Rd((const void*)a, &v, 4)) return def;
    return (v == v && fabsf(v) >= 0.005f && fabsf(v) <= 4.0f) ? v : def;
}
void PhotoReplayCtl(void* line) {
    if (!line || !FreeCamActive()) { s_pfLine = nullptr; return; }
    const float ph = FreeCamPhotoScale(); const int dpad = FreeCamReplayDpad();
    if (ph <= 0.0f && dpad == 0) { s_pfLine = nullptr; return; }
    if (s_seekOk < 0) { s_seekOk = SigAt(kRplSeek, kRplSeekSig, sizeof kRplSeekSig) ? 1 : 0; Log("modo foto en la repeticion: salto del visor %s", s_seekOk ? "listo (0x14217e800)" : "NO (firma distinta)"); }
    if (!s_seekOk) return;
    __try {
        if (*(const int*)line != 5) { s_pfLine = nullptr; return; }
        const float pos = *(const float*)((const char*)line + 0x14);
        if (!(pos >= 0.0f && pos < 65536.0f)) return;
        if (s_pfLine != line) { s_pfLine = line; s_pf = pos; }
        float spd = *(const float*)((const char*)line + 0x1c); if (!(spd > 0.0f && spd < 10.0f)) spd = 1.0f;   // velocidad del visor (1 = normal)
        const FreeCamPadInfo pi = FreeCamPadState();
        // como el visor del juego: cruceta derecha / izquierda mantenida = lento adelante / atras (0x14217C133 / 0x14217C154, lo que
        // ponga SlowMoServer); cruz = pausa; cuadrado (u O) en pausa = avanzar lento; si no, los pasos del modo foto (L1 + cruceta)
        double rate;
        if (dpad > 0 || (pi.paused && pi.advancing)) rate = spd * RplSpeedConst(0x14217C133ull, 0.5f);
        else if (dpad < 0) rate = spd * RplSpeedConst(0x14217C154ull, -0.5f);
        else if (pi.paused) rate = 0.0;
        else rate = (double)std::clamp(ph, 0.0f, 4.0f);
        s_pf = std::max(0.0, s_pf + rate);
        const int want = (int)s_pf;
        if (want != (int)pos) {
            ((RplSeekFn)kRplSeek)(line, (unsigned short)want);
            const float now = *(const float*)((const char*)line + 0x14);
            if ((int)now != want) s_pf = now;   // el juego lo recorto (principio / final de lo grabado)
            if (s_pfLogs < 6) { s_pfLogs++; Log("modo foto en la repeticion: cuadro %d (x%.2f%s)", want, rate, pi.paused ? ", pausa" : ""); }
        }
        g_replayMoveMs.store(GetTickCount64(), std::memory_order_relaxed);   // la camara libre sigue viendo la repeticion aunque este quieta
    } __except (EXCEPTION_EXECUTE_HANDLER) { s_pfLine = nullptr; }
}
uint64_t hk_rpl(void* obj, unsigned pad, unsigned char p3) {
    g_replayCalls.fetch_add(1, std::memory_order_relaxed); g_replayCallsV2.fetch_add(1, std::memory_order_relaxed);
    const bool v2 = GrassLive(448, 1) > 0.5f;
    __try { if (obj && *(const int*)obj == 5) {
        if (!v2) { g_replayMs.store(GetTickCount64(), std::memory_order_relaxed); g_replayPlay.fetch_add(1, std::memory_order_relaxed); }   // y448 0: como antes
        const unsigned cur = *(const unsigned*)((const char*)obj + 0x14), stp = *(const unsigned*)((const char*)obj + 0x18);
        const bool mv = RplCursorMoved(obj, cur) || (stp & 0x7fffffffu) != 0u;
        g_replayPlayV2.fetch_add(1, std::memory_order_relaxed); if (mv) { g_replayMoveV2.fetch_add(1, std::memory_order_relaxed); g_replayMoveMs.store(GetTickCount64(), std::memory_order_relaxed); }
        if (v2 && mv) { g_replayMs.store(GetTickCount64(), std::memory_order_relaxed); g_replayPlay.fetch_add(1, std::memory_order_relaxed); } } } __except (EXCEPTION_EXECUTE_HANDLER) {}
    // (14/11 g, Agustin: "que el slow mo del lua tambien permita que la reproduccion normal ... vaya en camara lenta") REPETICION NORMAL
    // EN CAMARA LENTA ([PES21FX] repeticion_lenta): mientras la linea avanza a velocidad normal (sin flechas ni pausa del visor), su
    // velocidad (+0x1C) se multiplica por la de la flecha derecha (0.5 del exe o la de SlowMoServer) solo durante la llamada. Si el paso
    // del cuadro no es el normal (flechas, stick, pausa, salto) no se toca: se vuelve a escalar en cuanto la reproduccion vuelve a la normal.
    static const void* s_nlLine = nullptr; static bool s_nlScale = false;
    float spd0 = 0.0f, pos0 = 0.0f, mul = 1.0f; bool scaled = false, st5 = false;
    const bool nlOn = g_cfg.replaySlow != 0 && obj && s_pfLine == nullptr;
    if (nlOn) { __try {
        st5 = *(const int*)obj == 5;
        if (st5) {
            if (s_nlLine != obj) { s_nlLine = obj; s_nlScale = false; }
            pos0 = *(const float*)((const char*)obj + 0x14); spd0 = *(const float*)((const char*)obj + 0x1c);
            mul = RplSpeedConst(0x14217C133ull, 0.5f);
            if (s_nlScale && spd0 > 0.0f && spd0 < 10.0f && mul > 0.0f && mul < 1.0f) { *(float*)((char*)obj + 0x1c) = spd0 * mul; scaled = true; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { st5 = false; } }
    const uint64_t r = o_rpl(obj, pad, p3);
    if (nlOn && st5) { __try {
        if (scaled && *(const float*)((const char*)obj + 0x1c) == spd0 * mul) *(float*)((char*)obj + 0x1c) = spd0;
        const float d = *(const float*)((const char*)obj + 0x14) - pos0, expect = scaled ? spd0 * mul : spd0;
        s_nlScale = spd0 > 0.0f && fabsf(d - expect) < 0.02f + 0.05f * expect;   // reproduccion normal: en la siguiente llamada, lenta
    } __except (EXCEPTION_EXECUTE_HANDLER) { s_nlScale = false; } }
    else s_nlScale = false;
    PhotoReplayCtl(obj);   // (14/11 g) pausa / avance / pasos del modo foto sobre la linea del visor
    return r;
}

using LogFn = void(*)(void*, const char*, ...);
using RelFn = void(*)(void*);
LogFn o_log = nullptr; RelFn o_rel = nullptr;
std::atomic<bool> s_on{false}, s_tried{false};
std::atomic<uint64_t> s_bytes{0}; uint64_t s_cap = 2000ull << 20;
std::atomic<unsigned> s_msgs{0}, s_drop{0}, s_snaps{0}, s_skip{0};
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
    // (08/11 j) el 87 % del primer rastro era "[GetMoveMode] (old)moveMode=ERROR" (dos por jugador y tick): se cuenta y no se escribe
    if (n > 13 && memcmp(msg, "[GetMoveMode]", 13) == 0 && strstr(msg, "=ERROR")) { s_skip.fetch_add(1, std::memory_order_relaxed); return; }
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
    // (08/11 j) el objeto ENTERO de cada jugador (AnimeInfo, PlayerMove, PlayerInfo y DemoPlayerInfo son vistas de el, paso 0x3F40
    // entre jugadores): la estamina no estaba en las ventanas de arriba
    { "JugadorEntero", 0x19c8, 22, 0x0, 0x3f40 },
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

// (08/11, hilo del publico) eventos del partido para los gestos de la tribuna: fase, tipo, reanudacion y falta de MatchInfo
// (ver rastro-partido: +0x1168 fase, +0x116c 1 juego / 2 detenida / 3 reanudando, +0x1554 reanudacion, +0x1588 tipo de falta)
std::atomic<bool> s_ev{false}, s_relHooked{false};
void EventTick() {
    const uintptr_t reg = RdPtr(kRegGlobal);
    if (!reg) return;
    const uintptr_t mi = RdPtr(reg + 0x270);
    if (!mi) return;
    int v[5] = { -1, -1, -1, -1, -1 };
    Rd((const void*)(mi + 0x1198), &v[0], 4); Rd((const void*)(mi + 0x1168), &v[1], 4); Rd((const void*)(mi + 0x116c), &v[2], 4);
    Rd((const void*)(mi + 0x1554), &v[3], 4); Rd((const void*)(mi + 0x1588), &v[4], 4);
    PublicoMatchTick(v[0], v[1], v[2], v[3], v[4]);
}

void hk_rel(void* view) {
    const uintptr_t ra = (uintptr_t)_ReturnAddress();
    if (ra > kTickEndLo && ra < kTickEndHi) {
        if (s_on.load(std::memory_order_relaxed)) { __try { Snapshot(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        if (s_ev.load(std::memory_order_relaxed) && g_cfg.live[394][0] > 0.0f) { __try { EventTick(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        // (12/11) desgaste real (fila 431): el tick del partido avanza solo con el partido corriendo (no en pausa ni en las repeticiones)
        __try { const uintptr_t reg = RdPtr(kRegGlobal); const uintptr_t mi = reg ? RdPtr(reg + 0x270) : 0; int tk = -1; if (mi) Rd((const void*)(mi + 0x1198), &tk, 4); g_simTick.store(tk, std::memory_order_relaxed); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    o_rel(view);
}

// ---- (08/11 j) vigia del registro de faltas: quien escribe "offside". El registro de la ultima falta vive en match::MatchControl
// (+0x2940, se copia cada tick a MatchInfo+0x1584): +0x2944 tipo (0 nada, 1 OFFSIDE, 2 contacto, 3 piscinazo, 4 mano), +0x294c
// infractor; +0x2930 = motivo de la detencion (MatchInfo+0x1574, 5 = falta). En el exe no hay ninguna escritura directa a esos
// campos (se escriben por un puntero, o desde codigo protegido), asi que se ponen puntos de parada de ESCRITURA por hardware
// (DR0-DR2, en todos los hilos) y cada escritura que cambia el valor anota la instruccion que escribio y las direcciones de
// retorno de la pila -> falta.log. Solo mira: no cambia nada. Se engancha MatchControl::vf19 (0x1404228d0, una vez por tick)
// para saber donde esta el objeto.
const uintptr_t kMcVf19 = 0x1404228d0ull;
const unsigned char kMcSig[16] = { 0x48,0x8b,0xc4,0x48,0x89,0x50,0x10,0x55,0x53,0x56,0x57,0x41,0x56,0x48,0x8b,0xec };
const uint32_t kWatchOff[3] = { 0x2944, 0x294c, 0x2930 };
const char* const kWatchName[3] = { "tipo_falta(MI+0x1588)", "infractor(MI+0x1590)", "motivo_detencion(MI+0x1574)" };
using Vf19Fn = void(*)(void*, void*);
Vf19Fn o_vf19 = nullptr;
std::atomic<uintptr_t> s_mc{0}, s_armedMc{0};
struct FaltaHit { uint32_t ms; int tick; int slot; uint32_t val; uint32_t tid; uintptr_t rip; uintptr_t ret[12]; };
const int kFaltaN = 4096;
FaltaHit s_fh[kFaltaN];
std::atomic<int> s_fhN{0}; int s_fhDone = 0;
uint32_t s_fhLast[3] = { 0xffffffffu, 0xffffffffu, 0xffffffffu }; uintptr_t s_fhLastRip[3] = {};
std::atomic<unsigned> s_fhAll{0};
FILE* s_falta = nullptr;

bool LooksLikeRet(uintptr_t v) {
    if (v < 0x140001000ull || v >= kRdLo) return false;
    unsigned char b[7] = {};
    if (!Rd((const void*)(v - 7), b, 7)) return false;
    return b[2] == 0xe8 /* call rel32 */ || (b[1] == 0xff && b[2] == 0x15) /* call [rip+x] */ || (b[4] == 0xff && (b[5] & 0x38) == 0x10) /* call reg/[reg+d8] */ ||
           (b[5] == 0xff && (b[6] & 0x38) == 0x10) || (b[3] == 0xff && (b[4] & 0x38) == 0x10) || (b[0] == 0xff && (b[1] & 0x38) == 0x10);
}

LONG CALLBACK FaltaVeh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* cx = ep->ContextRecord;
    const uintptr_t mc = s_armedMc.load(std::memory_order_relaxed);
    if (!mc || !(cx->Dr6 & 7) || cx->Dr0 != mc + kWatchOff[0]) return EXCEPTION_CONTINUE_SEARCH;
    const int slot = (cx->Dr6 & 1) ? 0 : (cx->Dr6 & 2) ? 1 : 2;
    cx->Dr6 = 0;
    s_fhAll.fetch_add(1, std::memory_order_relaxed);
    uint32_t v = 0; Rd((const void*)(mc + kWatchOff[slot]), &v, 4);
    if (v == s_fhLast[slot] && (uintptr_t)cx->Rip == s_fhLastRip[slot]) return EXCEPTION_CONTINUE_EXECUTION;   // misma escritura de siempre
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

// pone los puntos de parada en todos los hilos del proceso menos este (corre en un hilo propio)
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
                for (int s = 0; s < 3; s++) {
                    d7 &= ~((DWORD64)3 << (2 * s)); d7 &= ~((DWORD64)0xF << (16 + 4 * s));
                    d7 |= ((DWORD64)1 << (2 * s)) | ((DWORD64)1 << (16 + 4 * s)) | ((DWORD64)3 << (18 + 4 * s));   // local, escritura, 4 bytes
                }
                c.Dr0 = mc + kWatchOff[0]; c.Dr1 = mc + kWatchOff[1]; c.Dr2 = mc + kWatchOff[2]; c.Dr7 = d7;
                if (SetThreadContext(th, &c)) done++;
            }
            ResumeThread(th);
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
    static int s_logs = 0;
    if (s_logs++ < 6) Log("rastro: vigia de faltas en MatchControl %p: %d de %d hilos", (void*)mc, done, tot);
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
    if (!s_relHooked.load() && SigAt(kRegRelease, kRelSig, sizeof kRelSig) && MH_CreateHook((void*)kRegRelease, (void*)&hk_rel, (void**)&o_rel) == MH_OK && MH_EnableHook((void*)kRegRelease) == MH_OK) s_relHooked = true;
    relOk = s_relHooked.load();
    bool mcOk = false;
    if (g_cfg.rastroFalta && SigAt(kMcVf19, kMcSig, sizeof kMcSig)) {
        s_falta = _wfopen((s_dir + L"falta.log").c_str(), L"wb");
        if (s_falta) fputs("ms\ttick\tcampo\tvalor_nuevo\thilo\tinstruccion_que_escribe\tretornos (de adentro hacia afuera)\n", s_falta);
        AddVectoredExceptionHandler(1, FaltaVeh);
        mcOk = MH_CreateHook((void*)kMcVf19, (void*)&hk_vf19, (void**)&o_vf19) == MH_OK && MH_EnableHook((void*)kMcVf19) == MH_OK;
    }
    s_on = logOk || relOk;
    Log("rastro: registro de depuracion del juego %s, foto del partido %s, vigia de faltas %s -> PES21FX\\rastro\\%ls\\ (tope %llu MB)",
        logOk ? "enganchado" : "NO (firma distinta)", relOk ? "enganchada" : "NO (firma distinta)",
        !g_cfg.rastroFalta ? "apagado" : mcOk ? "enganchado" : "NO (firma distinta)", name, (unsigned long long)(s_cap >> 20));
}

// (14/11 g, Agustin: "la camara libre no esta linkeada a la repeticion, las opciones no funcionan") la camara libre solo en la
// repeticion (x344 = 3) y la lenta solo en la repeticion (z343 = 2) necesitan el gancho del visor de repeticiones, que antes solo se
// ponia con el desgaste real (x431). Se pone apenas se elige uno de esos modos (una vez, misma firma y mismo gancho).
void RastroReplayHookEnsure() {
    static bool tried = false;
    if (tried || g_replayHook.load() || !(g_cfg.live[344][0] > 0.5f || g_cfg.live[343][2] > 1.5f)) return;   // cualquier camara libre: el modo foto tambien maneja el visor
    tried = true;
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("camara en la repeticion: exe distinto, sin gancho del visor"); return; }
    const bool sig = SigAt(kReplayCtl, kReplaySig, sizeof kReplaySig);
    if (sig && MH_CreateHook((void*)kReplayCtl, (void*)&hk_rpl, (void**)&o_rpl) == MH_OK && MH_EnableHook((void*)kReplayCtl) == MH_OK) g_replayHook = 1;
    Log("camara libre / lenta en la repeticion: visor de repeticiones %s", g_replayHook.load() ? "enganchado (0x14217c0b0)" : sig ? "NO (MinHook)" : "NO (firma distinta)");
}

// (08/11) solo el gancho del final del tick, para los eventos del partido que usan los gestos del publico (fila 394), aunque
// el rastro este apagado. Solo lectura de MatchInfo. Se llama en cada Present despues de RastroPresent.
void RastroEventosPresent() {
    static bool tried = false;
    if (tried || (g_cfg.live[394][0] <= 0.0f && g_cfg.live[431][0] <= 0.5f)) return;   // (12/11) tambien para el desgaste real (reloj del partido)
    tried = true;
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) { Log("publico: exe distinto (sello %08X): sin eventos del partido", (unsigned)nt->FileHeader.TimeDateStamp); return; }
    if (!s_relHooked.load() && SigAt(kRegRelease, kRelSig, sizeof kRelSig) && MH_CreateHook((void*)kRegRelease, (void*)&hk_rel, (void**)&o_rel) == MH_OK && MH_EnableHook((void*)kRegRelease) == MH_OK) s_relHooked = true;
    s_ev = s_relHooked.load();
    if (g_cfg.live[431][0] > 0.5f && !g_replayHook.load()) {   // (12/11 16:45) visor de repeticiones para el desgaste real
        const bool sig = SigAt(kReplayCtl, kReplaySig, sizeof kReplaySig);
        if (sig && MH_CreateHook((void*)kReplayCtl, (void*)&hk_rpl, (void**)&o_rpl) == MH_OK && MH_EnableHook((void*)kReplayCtl) == MH_OK) g_replayHook = 1;
        Log("desgaste real: visor de repeticiones %s", g_replayHook.load() ? "enganchado (0x14217c0b0)" : sig ? "NO (MinHook)" : "NO (firma distinta)");
    }
    Log("publico: eventos del partido (gol, falta, offside, corner) %s", s_ev.load() ? "enganchados al final del tick" : "NO (firma distinta)");
}

// resumen cada tanto (lo llama el medidor de 10 s) y vaciado al cerrar
void RastroFlush(bool final) {
    if (!s_on.load()) return;
    AcquireSRWLockExclusive(&s_lk);
    if (s_log) fflush(s_log); if (s_bin) fflush(s_bin); if (s_csv) fflush(s_csv);
    FaltaFlush();
    if (!final) { const uintptr_t mc = s_mc.load(); if (mc) { static ULONGLONG t = 0; ULONGLONG n = GetTickCount64(); if (n - t > 30000) { t = n; FaltaRequestArm(mc); } } }   // hilos nuevos
    ReleaseSRWLockExclusive(&s_lk);
    static unsigned lastMsgs = 0;
    const unsigned m = s_msgs.load();
    if (final || m != lastMsgs) Log("rastro: %u mensajes del juego (%u GetMoveMode ERROR sin escribir), %u fotos del partido, %.1f MB, %u descartados por el tope, faltas: %u escrituras / %d distintas%s",
        m, s_skip.load(), s_snaps.load(), s_bytes.load() / 1048576.0, s_drop.load(), s_fhAll.load(), s_fhN.load(), final ? " (final)" : "");
    lastMsgs = m;
}

} // namespace fx
