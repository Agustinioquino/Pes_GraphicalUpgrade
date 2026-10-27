// PES21 FX - de donde sale el sol de las repeticiones (26/10). En 7894072 el log muestra que en el juego normal la luz del
// sol (LightCB, pasada a mundo) es la del archivo del cielo (0.634 0.441 0.635), pero en la previa del partido y en las
// repeticiones el motor la gira de toma en toma (misma altura, ~26 grados; otro rumbo) y el shadow map gira con ella (0 grados
// entre las dos). No es una lectura mal hecha: lo hace el motor. Para poder dejarla fija hay que encontrar quien la escribe.
//
// Aca, solo para encontrarlo (no cambia nada del juego):
//  1) con el sol quieto (juego normal) se recorre la memoria escribible del proceso buscando la direccion del sol en mundo
//     (+ o -, tres floats seguidos): "candidatos".
//  2) cuando la luz se separa del cielo (toma girada), se releen: los que ahora tienen la direccion nueva son "seguidores"
//     (la guardan de verdad). Si no hay ninguno, se busca de nuevo la direccion nueva en toda la memoria.
//  3) en hasta 4 seguidores se pone un punto de parada de escritura por hardware (DR0-DR3, en todos los hilos) por 60 s:
//     cada escritura anota RIP (modulo + offset), las direcciones de retorno del exe que haya en la pila y el valor nuevo.
//     El log queda con "sol: escribe ..." para buscar esa funcion en el volcado del exe (tools/re).
// [PES21FX] sol_vigilar = 0 lo apaga.
#include "core.h"
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace fx {

std::string ModOff(void* p);

static std::atomic<bool> s_swStarted{false};
static std::mutex s_swMutex;
static float s_swW[3] = {}; static float s_swDev = -1.0f; static int s_swFrame = -1;

// ---------------------------------------------------------------- escrituras vistas por el punto de parada
struct SwHit { uintptr_t rip; uintptr_t ret[4]; float v[3]; int slot; };
static const int kHits = 512;
static SwHit s_hits[kHits];
static std::atomic<int> s_hitN{0};
static std::atomic<bool> s_armed{false};
static uintptr_t s_watch[4] = {};
static uintptr_t s_exeLo = 0, s_exeHi = 0, s_selfLo = 0, s_selfHi = 0;

static LONG CALLBACK SwVectored(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !s_armed.load(std::memory_order_relaxed)) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* cx = ep->ContextRecord;
    DWORD64 d6 = cx->Dr6;
    if (!(d6 & 0xF)) return EXCEPTION_CONTINUE_SEARCH;
    int slot = (d6 & 1) ? 0 : (d6 & 2) ? 1 : (d6 & 4) ? 2 : 3;
    int i = s_hitN.fetch_add(1, std::memory_order_relaxed);
    if (i < kHits) {
        SwHit& h = s_hits[i]; h.rip = (uintptr_t)cx->Rip; h.slot = slot; memset(h.ret, 0, sizeof h.ret);
        // direcciones de retorno del exe en la pila (heuristica: valores de la pila que caen dentro de la imagen del exe)
        const uintptr_t* sp = (const uintptr_t*)cx->Rsp; int k = 0;
        __try { for (int j = 0; j < 96 && k < 4; j++) { uintptr_t v = sp[j]; if (v > s_exeLo && v < s_exeHi) h.ret[k++] = v; } } __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try { memcpy(h.v, (const void*)s_watch[slot], sizeof h.v); } __except (EXCEPTION_EXECUTE_HANDLER) { h.v[0] = h.v[1] = h.v[2] = NAN; }
    }
    if (i > 2048) cx->Dr7 &= ~(DWORD64)0xFF;   // (27/10) un origen que se reescribe sin parar: este hilo deja de vigilar (no frena el juego)
    cx->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// pone (o saca, n = 0) los puntos de parada de escritura en todos los hilos del proceso menos este
static int SwSetDebugRegs(const uintptr_t* a, int n) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te{}; te.dwSize = sizeof te;
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId(); int done = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!th) continue;
        if (SuspendThread(th) != (DWORD)-1) {
            CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(th, &c)) {
                DWORD64 d7 = c.Dr7;
                for (int s = 0; s < 4; s++) {
                    d7 &= ~((DWORD64)3 << (2 * s)); d7 &= ~((DWORD64)0xF << (16 + 4 * s));
                    if (s < n && a[s]) d7 |= ((DWORD64)1 << (2 * s)) | ((DWORD64)1 << (16 + 4 * s)) | ((DWORD64)3 << (18 + 4 * s));   // local, escritura, 4 bytes
                }
                c.Dr0 = n > 0 ? a[0] : 0; c.Dr1 = n > 1 ? a[1] : 0; c.Dr2 = n > 2 ? a[2] : 0; c.Dr3 = n > 3 ? a[3] : 0; c.Dr7 = d7;
                if (SetThreadContext(th, &c)) done++;
            }
            ResumeThread(th);
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
    return done;
}

// ---------------------------------------------------------------- busqueda en la memoria
struct SwCand { uintptr_t a; int sign; };
static bool SwNear(const float* p, const float* w, int sign, float tol) {
    return fabsf(p[0] - sign * w[0]) < tol && fabsf(p[1] - sign * w[1]) < tol && fabsf(p[2] - sign * w[2]) < tol;
}
static int SwScanRegion(const float* p, size_t n, const float* w, float tol, uintptr_t base, SwCand* out, int cap) {
    int k = 0;
    __try {
        for (size_t i = 0; i + 2 < n && k < cap; i++) {
            float f = p[i];
            int s = fabsf(f - w[0]) < tol ? 1 : fabsf(f + w[0]) < tol ? -1 : 0;
            if (s && SwNear(p + i, w, s, tol)) { out[k].a = base + i * 4; out[k].sign = s; k++; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return k;
}
static std::vector<SwCand> SwScan(const float* w, float tol, double* secs, size_t* bytes) {
    std::vector<SwCand> r; static SwCand buf[4096];
    double t0 = Now(); size_t tot = 0;
    uintptr_t p = 0x10000;
    while (p < 0x7FFFFFFF0000ull && r.size() < 4096) {
        MEMORY_BASIC_INFORMATION mi{};
        if (!VirtualQuery((void*)p, &mi, sizeof mi)) break;
        uintptr_t b = (uintptr_t)mi.BaseAddress; size_t sz = mi.RegionSize;
        // sin la DLL propia (fila 111, copias del sol) ni memoria de la placa (combinada / sin cache: lentisima de leer)
        bool rw = mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_GUARD | PAGE_NOACCESS | PAGE_WRITECOMBINE | PAGE_NOCACHE)) &&
                  !(b >= s_selfLo && b < s_selfHi) &&
                  (mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
        if (rw && sz <= (1ull << 30)) {
            int k = SwScanRegion((const float*)b, sz / 4, w, tol, b, buf, (int)(4096 - r.size()));
            r.insert(r.end(), buf, buf + k); tot += sz;
        }
        p = b + sz;
    }
    if (secs) *secs = Now() - t0; if (bytes) *bytes = tot;
    return r;
}
static bool SwRead3(uintptr_t a, float* v) { __try { memcpy(v, (const void*)a, 12); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
static std::string SwWhere(uintptr_t a) {
    if (a >= s_exeLo && a < s_exeHi) return ModOff((void*)a);
    MEMORY_BASIC_INFORMATION mi{}; char b[96];
    if (VirtualQuery((void*)a, &mi, sizeof mi)) {
        snprintf(b, sizeof b, "%p (%s, region %p + 0x%llx)", (void*)a, mi.Type == MEM_IMAGE ? "imagen" : mi.Type == MEM_MAPPED ? "mapeada" : "privada",
                 mi.AllocationBase, (unsigned long long)(a - (uintptr_t)mi.AllocationBase));
    } else snprintf(b, sizeof b, "%p", (void*)a);
    return b;
}
static float SwAng(const float* a, const float* b) { float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; d = d > 1 ? 1 : d < -1 ? -1 : d; return acosf(d) * 57.2958f; }

// (27/10) la busqueda en memoria (pasos 1 y 2) no sirvio: el unico candidato lo escribe la GPU. Ahora el rastreo de la luz
// (hooks.cpp) sabe de donde copia el juego el cbuffer de la luz (el origen del memcpy de su subida) y pide vigilar ESA
// direccion: la escritura de m_lightDir0 en espacio de vista sale con su RIP y sus retornos ("sol: escribe ...").
static const bool kScan = false;
static std::atomic<uintptr_t> s_reqArm{0};
void SunWatchRequestArm(uintptr_t a) { if (a && !(a & 3)) s_reqArm.store(a, std::memory_order_relaxed); }

static DWORD WINAPI SwThread(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    HMODULE exe = GetModuleHandleW(nullptr);
    { auto* dos = (IMAGE_DOS_HEADER*)exe; auto* nt = (IMAGE_NT_HEADERS*)((char*)exe + dos->e_lfanew); s_exeLo = (uintptr_t)exe; s_exeHi = s_exeLo + nt->OptionalHeader.SizeOfImage; }
    { HMODULE me = nullptr;
      if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&SwThread, &me) && me) {
          auto* dos = (IMAGE_DOS_HEADER*)me; auto* nt = (IMAGE_NT_HEADERS*)((char*)me + dos->e_lfanew); s_selfLo = (uintptr_t)me; s_selfHi = s_selfLo + nt->OptionalHeader.SizeOfImage; } }
    AddVectoredExceptionHandler(1, SwVectored);
    std::vector<SwCand> cand; float base[3] = {}, last[3] = {}; double stableSince = -1, armedAt = -1, lastArm = -1;
    int shots = 0, rescans = 0, arms = 0, logged = 0; bool haveBase = false;
    float shotW[3] = {};
    for (;;) {
        Sleep(200);
        float w[3]; float dev; int fr;
        { std::lock_guard<std::mutex> lk(s_swMutex); memcpy(w, s_swW, sizeof w); dev = s_swDev; fr = s_swFrame; }
        double t = Now();
        // pedido del rastreo de la luz: m_lightDir0 (x, y, z) en el origen de la copia, 10 s, hasta 4 veces
        { uintptr_t ra = s_reqArm.exchange(0);
          if (ra && !s_armed.load() && arms < 4 && (lastArm < 0 || t - lastArm > 20.0)) {
              memset(s_watch, 0, sizeof s_watch); s_watch[0] = ra; s_watch[1] = ra + 4; s_watch[2] = ra + 8;
              s_hitN = 0; s_armed = true; int th = SwSetDebugRegs(s_watch, 3); armedAt = lastArm = t - 50.0; arms++; logged = 0;
              Log("sol: vigilando escrituras en el origen del cbuffer de la luz %s (%d hilos) por 10 s", SwWhere(ra).c_str(), th);
          } }
        if (fr >= 0 && kScan) {   // pasos 1 y 2 (busqueda en memoria), apagados
        // 1) base: la direccion quieta 3 s (y pegada al cielo) o 10 s quieta aunque no coincida con el cielo
        if (SwAng(w, last) > 0.2f) stableSince = t;
        memcpy(last, w, sizeof w);
        if (!haveBase && stableSince > 0 && ((t - stableSince > 3.0 && dev >= 0 && dev < 2.0f) || t - stableSince > 10.0)) {
            double secs; size_t bytes; cand = SwScan(w, 1.5e-3f, &secs, &bytes);
            memcpy(base, w, sizeof w); haveBase = true;
            Log("sol: busqueda en memoria del sol quieto (%.3f %.3f %.3f, %.1f grados del cielo): %zu candidatos en %.0f MB (%.1f s)",
                w[0], w[1], w[2], dev, cand.size(), bytes / 1048576.0, secs);
            for (size_t i = 0; i < cand.size() && i < 12; i++) Log("  candidato %s%s", cand[i].sign < 0 ? "(-) " : "", SwWhere(cand[i].a).c_str());
            continue;
        }
        if (!haveBase) continue;
        // 2) toma girada: quienes siguen a la luz
        if (SwAng(w, base) > 8.0f && SwAng(w, shotW) > 3.0f && t - stableSince > 0.6 && shots < 6) {
            memcpy(shotW, w, sizeof w); shots++;
            std::vector<SwCand> fol; int changed = 0;
            for (auto& c : cand) { float v[3]; if (!SwRead3(c.a, v)) continue;
                if (SwNear(v, w, c.sign, 3e-3f)) fol.push_back(c); else if (!SwNear(v, base, c.sign, 3e-3f)) changed++; }
            Log("sol: toma con la luz girada %.1f grados (%.3f %.3f %.3f): %zu candidatos la siguen, %d cambiaron a otra cosa, %zu quedaron con el sol quieto",
                SwAng(w, base), w[0], w[1], w[2], fol.size(), changed, cand.size() - fol.size() - changed);
            for (size_t i = 0; i < fol.size() && i < 12; i++) Log("  sigue a la luz: %s%s", fol[i].sign < 0 ? "(-) " : "", SwWhere(fol[i].a).c_str());
            if (fol.empty() && rescans < 2) {
                rescans++; double secs; size_t bytes; auto nw = SwScan(w, 1.5e-3f, &secs, &bytes);
                Log("sol: ningun candidato sigue a la luz; la direccion girada esta en %zu lugares (%.1f s)", nw.size(), secs);
                for (size_t i = 0; i < nw.size() && i < 12; i++) Log("  direccion girada en %s%s", nw[i].sign < 0 ? "(-) " : "", SwWhere(nw[i].a).c_str());
                fol = nw;
            }
            // 3) puntos de parada de escritura en hasta 4 seguidores
            if (!fol.empty() && !s_armed.load() && arms < 3 && (lastArm < 0 || t - lastArm > 90.0)) {
                memset(s_watch, 0, sizeof s_watch);
                int n = 0; for (auto& c : fol) { if (n >= 4) break; if ((c.a & 3) == 0) s_watch[n++] = c.a; }
                if (n) { s_hitN = 0; s_armed = true; int th = SwSetDebugRegs(s_watch, n); armedAt = lastArm = t; arms++; logged = 0;
                         Log("sol: vigilando escrituras en %d direcciones (%d hilos) por 60 s", n, th); }
            }
        }
        }
        // escrituras: se anotan las nuevas (RIP distinto) y a los 60 s se sacan los puntos de parada
        if (s_armed.load()) {
            int n = (std::min)(s_hitN.load(), kHits);
            static std::vector<uintptr_t> seen; if (logged == 0) seen.clear();
            for (; logged < n; logged++) {
                const SwHit& h = s_hits[logged];
                bool dup = false; for (uintptr_t r : seen) if (r == h.rip) { dup = true; break; }
                if (dup || seen.size() >= 24) continue; seen.push_back(h.rip);
                Log("sol: escribe %s (direccion %d) el valor (%.3f %.3f %.3f); retornos %s %s %s %s", ModOff((void*)h.rip).c_str(), h.slot, h.v[0], h.v[1], h.v[2],
                    h.ret[0] ? ModOff((void*)h.ret[0]).c_str() : "-", h.ret[1] ? ModOff((void*)h.ret[1]).c_str() : "", h.ret[2] ? ModOff((void*)h.ret[2]).c_str() : "", h.ret[3] ? ModOff((void*)h.ret[3]).c_str() : "");
            }
            if (t - armedAt > 60.0) {
                SwSetDebugRegs(s_watch, 0); s_armed = false;
                Log("sol: fin de la vigilancia: %d escrituras, %zu lugares distintos que escriben", s_hitN.load(), seen.size());
            } else if (t - armedAt > 2.0 && fmod(t - armedAt, 5.0) < 0.2) SwSetDebugRegs(s_watch, 4);   // hilos nuevos
        }
    }
    return 0;
}

// Lo llama SunPassPresent cada cuadro medido: sol de la luz en mundo (hacia el sol) y su distancia al del cielo (grados)
void SunWatchFrame(const float* w, float devSky, int frame) {
    if (!g_cfg.sunWatch) return;
    { std::lock_guard<std::mutex> lk(s_swMutex); memcpy(s_swW, w, sizeof s_swW); s_swDev = devSky; s_swFrame = frame; }
    if (!s_swStarted.exchange(true)) { HANDLE h = CreateThread(nullptr, 0, SwThread, nullptr, 0, nullptr); if (h) CloseHandle(h); Log("sol: vigilancia del sol de las repeticiones prendida (sol_vigilar = 0 la apaga)"); }
}

} // namespace fx
