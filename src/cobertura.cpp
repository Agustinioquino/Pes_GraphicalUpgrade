// PES21 FX - medidor de cobertura del codigo del juego (08/11). Pregunta de Agustin / crabshank: "que porcentaje del codigo del
// juego se ejecuta durante un partido, con la pausa y los menus de despues?". Leyendo el codigo solo se puede estimar (25 % del
// partido en si, 35-50 % con el motor); para el numero exacto hay que medirlo con el juego corriendo. Esto hace eso:
//   1. Lee la tabla de excepciones del exe (.pdata: una entrada por funcion, la misma lista que uso Ghidra) y se queda con los
//      inicios de funcion dentro de .text (sin los trozos encadenados, que son continuaciones de otra funcion).
//   2. Con [PES21FX] cobertura = 1, `cobertura_demora` segundos despues del primer cuadro (defecto 30: los ganchos nuestros y los de
//      Sider ya estan puestos; una funcion que empieza con un salto ya enganchado se salta), pone un byte int3 (0xCC) al inicio de
//      cada funcion. La escritura es de un byte: un hilo que llega ve el byte viejo o el int3, nunca algo a medias.
//   3. La primera vez que el juego entra a una funcion salta la excepcion de punto de parada; el manejador vectorizado (primero de la
//      cadena) restaura el byte original, anota el orden, la fase y el momento, y vuelve a ejecutar la instruccion. El costo es una
//      excepcion por funcion en toda la sesion (unas decenas de microsegundos cada una): el juego no se frena.
//   4. La tecla [KeyPESCoberturaFase] (Ctrl+Alt+F) marca el corte de fase (menu, previa, 1er tiempo, pausa, entretiempo, 2do tiempo,
//      resultado, repeticion, vuelta al menu...). Cada funcion queda con la fase en la que se ejecuto por primera vez.
//   5. Cada 5 s (si hubo funciones nuevas) y al cerrar el juego se escribe PES21FX\cobertura\<fecha>\: funciones.tsv (rva, tamano,
//      orden, fase, segundos), fases.txt y resumen.txt (porcentaje por cantidad y por bytes, por fase y acumulado). Con la tabla de
//      Ghidra (decompilado/tablas/functions.tsv, rtti) se cruza por rva para saber que clases/modulos son.
// Limites: lo que corrio antes de la demora (arranque, logos) no se cuenta; una funcion vale "ejecutada" aunque haya corrido una sola
// vez; los bytes son los de la entrada principal de .pdata. Con cobertura = 1 la vigilancia del codigo (z173) queda apagada
// (veria 117 mil cambios). El manejador de cierres (main.cpp) no mira los puntos de parada (no son fatales).
#include "core.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace fx {

namespace {
struct Fn { uint32_t rva, size; };
std::vector<Fn> s_fn;                     // ordenadas por rva
std::vector<unsigned char> s_orig;        // byte original de cada funcion
std::vector<std::atomic<unsigned char>> s_state;   // 0 sin parchar, 1 parchada, 2 ejecutada, 3 salteada (ya enganchada / no legible)
std::vector<uint32_t> s_order, s_phase, s_ms;      // por funcion: orden del primer golpe (1..), fase, ms desde que se armaron
std::atomic<uint32_t> s_hits{0}, s_phaseNo{0}, s_newSince{0};
std::atomic<bool> s_armed{false}, s_started{false};
uintptr_t s_mod = 0, s_textLo = 0, s_textHi = 0;
ULONGLONG s_t0 = 0;
std::wstring s_dir;
uint64_t s_bytesAll = 0;
struct Phase { uint32_t no, ms; };
std::vector<Phase> s_phases;
CRITICAL_SECTION s_cs;

int Find(uintptr_t a) {
    if (a < s_mod) return -1;
    const uint64_t r = a - s_mod; if (r > 0xFFFFFFFFull) return -1;
    const uint32_t rva = (uint32_t)r;
    size_t lo = 0, hi = s_fn.size();
    while (lo < hi) { size_t m = (lo + hi) / 2; if (s_fn[m].rva < rva) lo = m + 1; else hi = m; }
    return (lo < s_fn.size() && s_fn[lo].rva == rva) ? (int)lo : -1;
}

LONG CALLBACK CovVectored(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT || !s_armed.load(std::memory_order_acquire)) return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t a = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    const int i = Find(a);
    if (i < 0) return EXCEPTION_CONTINUE_SEARCH;   // un int3 que no es nuestro (depurador, el juego)
    unsigned char st = 1;
    if (s_state[i].compare_exchange_strong(st, 2)) {   // primero en llegar: restaura y anota
        *(volatile unsigned char*)a = s_orig[i];
        FlushInstructionCache(GetCurrentProcess(), (void*)a, 1);
        const uint32_t n = s_hits.fetch_add(1) + 1;
        s_order[i] = n; s_phase[i] = s_phaseNo.load(std::memory_order_relaxed);
        s_ms[i] = (uint32_t)(GetTickCount64() - s_t0);
        s_newSince.fetch_add(1);
    }
    // segundo hilo en la misma funcion (el byte ya fue restaurado) o el primero: ejecutar la instruccion real
    ep->ContextRecord->Rip = (DWORD64)a;
    return EXCEPTION_CONTINUE_EXECUTION;
}

bool Readable(uintptr_t p) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery((void*)p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (mi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY | PAGE_EXECUTE)) != 0;
}

bool LoadPdata() {
    HMODULE m = GetModuleHandleW(nullptr);
    s_mod = (uintptr_t)m;
    auto dos = (const IMAGE_DOS_HEADER*)m; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)m + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sh = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (memcmp(sh[i].Name, ".text\0\0\0", 8) == 0) { s_textLo = s_mod + sh[i].VirtualAddress; s_textHi = s_textLo + sh[i].Misc.VirtualSize; }
    const IMAGE_DATA_DIRECTORY& d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!s_textLo || !d.VirtualAddress || !d.Size) { Log("cobertura: sin .text o sin tabla de excepciones (.pdata)"); return false; }
    auto rf = (const RUNTIME_FUNCTION*)(s_mod + d.VirtualAddress);
    const size_t n = d.Size / sizeof(RUNTIME_FUNCTION);
    size_t chained = 0, out = 0, bad = 0;
    s_fn.reserve(n);
    for (size_t i = 0; i < n; i++) {
        const uint32_t b = rf[i].BeginAddress, e = rf[i].EndAddress, u = rf[i].UnwindInfoAddress;
        if (!b || e <= b) { bad++; continue; }
        const uintptr_t a = s_mod + b;
        if (a < s_textLo || a >= s_textHi) { out++; continue; }
        if (u & 1) { chained++; continue; }   // bit 0: la "unwind info" es otra RUNTIME_FUNCTION (trozo encadenado)
        if (u && u < nt->OptionalHeader.SizeOfImage) {
            const unsigned char flags = *(const unsigned char*)(s_mod + u) >> 3;
            if (flags & 4 /*UNW_FLAG_CHAININFO*/) { chained++; continue; }
        }
        s_fn.push_back({ b, e - b });
    }
    std::sort(s_fn.begin(), s_fn.end(), [](const Fn& x, const Fn& y) { return x.rva < y.rva; });
    s_fn.erase(std::unique(s_fn.begin(), s_fn.end(), [](const Fn& x, const Fn& y) { return x.rva == y.rva; }), s_fn.end());
    for (auto& f : s_fn) s_bytesAll += f.size;
    Log("cobertura: .pdata con %zu entradas: %zu funciones en .text (%.1f MB de codigo), %zu trozos encadenados, %zu fuera de .text, %zu invalidas",
        n, s_fn.size(), s_bytesAll / 1048576.0, chained, out, bad);
    return !s_fn.empty();
}

std::string PhaseName(uint32_t p) { char b[32]; snprintf(b, sizeof b, "fase %u", p); return b; }

void WriteAll(bool final) {
    EnterCriticalSection(&s_cs);
    const uint32_t hits = s_hits.load();
    std::vector<Phase> phases = s_phases;
    const uint32_t curPhase = s_phaseNo.load();
    std::wstring dir = s_dir;
    LeaveCriticalSection(&s_cs);
    if (dir.empty()) return;
    // funciones.tsv: todas las funciones medidas (ejecutada 0/1) para cruzar con Ghidra
    if (FILE* f = _wfopen((dir + L"funciones.tsv").c_str(), L"wb")) {
        fprintf(f, "rva\tbytes\tejecutada\torden\tfase\tsegundos\testado\n");
        for (size_t i = 0; i < s_fn.size(); i++) {
            const unsigned char st = s_state[i].load(std::memory_order_relaxed);
            if (st == 2) fprintf(f, "%x\t%u\t1\t%u\t%u\t%.3f\t\n", s_fn[i].rva, s_fn[i].size, s_order[i], s_phase[i], s_ms[i] / 1000.0);
            else fprintf(f, "%x\t%u\t0\t\t\t\t%s\n", s_fn[i].rva, s_fn[i].size, st == 3 ? "salteada" : st == 1 ? "" : "sin parchar");
        }
        fclose(f);
    }
    // resumen.txt: porcentajes por fase y acumulado
    std::vector<uint32_t> cntPh(curPhase + 1, 0); std::vector<uint64_t> bytPh(curPhase + 1, 0);
    uint32_t cnt = 0, measured = 0; uint64_t byt = 0, bytMeasured = 0;
    for (size_t i = 0; i < s_fn.size(); i++) {
        const unsigned char st = s_state[i].load(std::memory_order_relaxed);
        if (st == 1 || st == 2) { measured++; bytMeasured += s_fn[i].size; }
        if (st != 2) continue;
        cnt++; byt += s_fn[i].size;
        const uint32_t p = s_phase[i] <= curPhase ? s_phase[i] : curPhase;
        cntPh[p]++; bytPh[p] += s_fn[i].size;
    }
    if (FILE* f = _wfopen((dir + L"resumen.txt").c_str(), L"wb")) {
        fprintf(f, "Cobertura del codigo de PES2021.exe (%s)\r\n", final ? "final" : "parcial, se actualiza cada 5 s");
        fprintf(f, "Funciones en .text (.pdata): %zu = %.1f MB. Medidas (con punto de parada puesto): %u = %.1f MB. Tiempo medido: %.0f s\r\n",
            s_fn.size(), s_bytesAll / 1048576.0, measured, bytMeasured / 1048576.0, (GetTickCount64() - s_t0) / 1000.0);
        fprintf(f, "EJECUTADAS: %u funciones (%.2f %% de las medidas) = %.2f MB (%.2f %% de los bytes medidos)\r\n", cnt,
            measured ? 100.0 * cnt / measured : 0.0, byt / 1048576.0, bytMeasured ? 100.0 * byt / bytMeasured : 0.0);
        fprintf(f, "\r\nPor fase (funciones que se ejecutaron por PRIMERA vez en esa fase):\r\n");
        fprintf(f, "fase\tdesde_seg\tfunciones_nuevas\t%%_funciones\tbytes_nuevos\t%%_bytes\tacumulado_%%_funciones\tacumulado_%%_bytes\r\n");
        uint32_t ac = 0; uint64_t ab = 0;
        for (uint32_t p = 0; p <= curPhase; p++) {
            ac += cntPh[p]; ab += bytPh[p];
            double t0 = 0; for (auto& ph : phases) if (ph.no == p) t0 = ph.ms / 1000.0;
            fprintf(f, "%u\t%.0f\t%u\t%.2f\t%llu\t%.2f\t%.2f\t%.2f\r\n", p, t0, cntPh[p], measured ? 100.0 * cntPh[p] / measured : 0.0,
                (unsigned long long)bytPh[p], bytMeasured ? 100.0 * bytPh[p] / bytMeasured : 0.0,
                measured ? 100.0 * ac / measured : 0.0, bytMeasured ? 100.0 * ab / bytMeasured : 0.0);
        }
        fprintf(f, "\r\nLa fase 0 arranca cuando se ponen los puntos de parada (%u s despues del primer cuadro); cada Ctrl+Alt+F (KeyPESCoberturaFase) abre la siguiente.\r\n", (unsigned)g_cfg.coverDelay);
        fprintf(f, "Cruce con Ghidra: decompilado\\tablas\\functions.tsv (columna dir = 140000000 + rva) y rtti.txt para saber la clase de cada funcion.\r\n");
        fclose(f);
    }
    if (FILE* f = _wfopen((dir + L"fases.txt").c_str(), L"wb")) {
        fprintf(f, "fase\tsegundos\r\n0\t0\r\n");
        for (auto& ph : phases) fprintf(f, "%u\t%.1f\r\n", ph.no, ph.ms / 1000.0);
        fclose(f);
    }
}

DWORD WINAPI CovThread(LPVOID) {
    while (PresentNo() < 60) Sleep(500);   // primer cuadro de verdad
    for (int i = 0; i < g_cfg.coverDelay * 2; i++) Sleep(500);
    if (!LoadPdata()) return 0;
    s_orig.assign(s_fn.size(), 0);
    { std::vector<std::atomic<unsigned char>> v(s_fn.size()); for (auto& x : v) x.store(0); s_state.swap(v); }
    s_order.assign(s_fn.size(), 0); s_phase.assign(s_fn.size(), 0); s_ms.assign(s_fn.size(), 0);
    // toda .text pasa a lectura/escritura/ejecucion por el resto de la sesion (la restauracion de cada byte ocurre dentro del manejador)
    DWORD old = 0;
    if (!VirtualProtect((void*)s_textLo, s_textHi - s_textLo, PAGE_EXECUTE_READWRITE, &old)) {
        Log("cobertura: no se pudo abrir .text para escritura (error %lu): sin medicion", GetLastError()); return 0;
    }
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t name[48]; swprintf(name, 48, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    s_dir = g_cfg.baseDir + L"cobertura\\";
    CreateDirectoryW(s_dir.c_str(), nullptr);
    s_dir += name; s_dir += L"\\";
    CreateDirectoryW(s_dir.c_str(), nullptr);
    AddVectoredExceptionHandler(1, CovVectored);   // al frente de la cadena: antes que el registro de cierres y que Sider
    s_t0 = GetTickCount64();
    s_armed.store(true, std::memory_order_release);
    size_t patched = 0, skipped = 0;
    for (size_t i = 0; i < s_fn.size(); i++) {
        const uintptr_t a = s_mod + s_fn[i].rva;
        if (!Readable(a)) { s_state[i].store(3); skipped++; continue; }
        const unsigned char b0 = *(volatile unsigned char*)a;
        // ya enganchada por un mod (salto E9 / FF 25) o ya es un int3: no se toca (si despues alguien copia el int3 a su trampolin, se cae)
        if (b0 == 0xE9 || b0 == 0xCC || (b0 == 0xFF && *(volatile unsigned char*)(a + 1) == 0x25)) { s_state[i].store(3); skipped++; continue; }
        s_orig[i] = b0;
        s_state[i].store(1, std::memory_order_release);
        *(volatile unsigned char*)a = 0xCC;
        patched++;
        if ((i & 0x3FF) == 0) Sleep(0);
    }
    FlushInstructionCache(GetCurrentProcess(), (void*)s_textLo, s_textHi - s_textLo);
    Log("cobertura: %zu puntos de parada puestos, %zu funciones salteadas (ya enganchadas o no legibles). Fase 0 abierta; Ctrl+Alt+F = fase siguiente. Salida: PES21FX\\cobertura\\%ls\\", patched, skipped, name);
    uint32_t lastLogged = 0; int tick = 0;
    for (;;) {
        Sleep(5000);
        tick++;
        const uint32_t h = s_hits.load();
        if (s_newSince.exchange(0) || (tick % 12) == 0) WriteAll(false);
        if ((tick % 12) == 0 && h != lastLogged) { Log("cobertura: %u funciones ejecutadas hasta ahora (%.1f %%), fase %u", h, 100.0 * h / (double)patched, s_phaseNo.load()); lastLogged = h; }
    }
}
} // namespace

void CoverStart() {
    if (!g_cfg.coverOn || s_started.exchange(true)) return;
    InitializeCriticalSection(&s_cs);
    if (HANDLE t = CreateThread(nullptr, 0, CovThread, nullptr, 0, nullptr)) { SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(t); }
    Log("cobertura: medicion ACTIVA: %d s despues del primer cuadro se marcan todas las funciones del exe (cobertura = 0 para apagar)", g_cfg.coverDelay);
}

// tecla KeyPESCoberturaFase (config.cpp PollKeys): corte de fase
void CoverNextPhase() {
    if (!s_armed.load()) { Log("cobertura: la tecla de fase llego antes de armar la medicion (se ignora)"); return; }
    const uint32_t p = s_phaseNo.fetch_add(1) + 1;
    const uint32_t ms = (uint32_t)(GetTickCount64() - s_t0);
    EnterCriticalSection(&s_cs); s_phases.push_back({ p, ms }); LeaveCriticalSection(&s_cs);
    Log("cobertura: FASE %u desde %.1f s (%u funciones ejecutadas hasta aca)", p, ms / 1000.0, s_hits.load());
    s_newSince.fetch_add(1);
}

// al cerrar el juego (main.cpp DllMain DETACH) y desde el registro de cierres
void CoverFlush() { if (s_armed.load()) WriteAll(true); }

} // namespace fx
