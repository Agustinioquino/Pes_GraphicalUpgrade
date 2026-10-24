// PES21 FX - vigilancia del codigo del juego (24/10). Un cierre del 24/10 (excepcion 0xc000001d en PES2021.exe+0x4f7c16)
// cayo en la mitad de una instruccion de una funcion del partido que no tiene saltos indirectos: eso solo se explica si el
// codigo del juego en memoria estaba cambiado en ese momento (otro mod que parchea, o la proteccion del exe que reacciona
// a algo). Esta vigilancia copia la seccion .text de PES2021.exe 20 s despues de arrancar (con los ganchos propios ya
// puestos) y cada 2 s la compara con la memoria: lo que cambie queda en el log (donde, cuando, los bytes de antes y de ahora
// y, si es un salto, a que modulo va). El registro de cierres (main.cpp) muestra ademas los bytes alrededor de RIP y los de
// la copia. Solo lee (VirtualQuery antes de cada pagina; las paginas con PAGE_GUARD / NOACCESS no se tocan).
#include "core.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace fx {

static unsigned char* s_snap = nullptr;
static std::vector<unsigned char> s_skip;   // paginas que no se pueden leer (no se comparan)
static uintptr_t s_base = 0, s_mod = 0; static size_t s_size = 0;
static std::atomic<bool> s_ready{false}, s_started{false};
static std::atomic<unsigned> s_changes{0};

static bool PageReadable(uintptr_t p) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery((void*)p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (mi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}
static bool CwCopy(void* d, const void* s, size_t n) { __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
static int CwDiffers(const void* a, const void* b, size_t n) { __try { return memcmp(a, b, n) != 0 ? 1 : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; } }
static void Hex(char* out, size_t cap, const unsigned char* b, size_t n) {
    size_t k = 0; out[0] = 0;
    for (size_t i = 0; i < n && k + 4 < cap; i++) k += snprintf(out + k, cap - k, "%02x ", b[i]);
    if (k) out[k - 1] = 0;
}
// si lo nuevo es un salto (e9 rel32 / ff 25 [rip+rel32]), a donde va (asi se ve que modulo engancho la funcion)
static void DescribeJump(const unsigned char* b, size_t n, uintptr_t at, char* out, size_t cap) {
    out[0] = 0;
    for (size_t i = 0; i + 5 <= n; i++) {
        if (b[i] == 0xE9) {
            int32_t rel; memcpy(&rel, b + i + 1, 4);
            snprintf(out, cap, " | salto en +%zu a %s", i, ModOff((void*)(at + i + 5 + (intptr_t)rel)).c_str()); return;
        }
        if (i + 6 <= n && b[i] == 0xFF && b[i + 1] == 0x25) {
            int32_t rel; memcpy(&rel, b + i + 2, 4); uintptr_t t = 0;
            if (CwCopy(&t, (const void*)(at + i + 6 + (intptr_t)rel), 8)) snprintf(out, cap, " | salto indirecto en +%zu a %s", i, ModOff((void*)t).c_str());
            return;
        }
    }
}

static DWORD WINAPI WatchThread(void*) {
    Sleep(20000);
    HMODULE m = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)m; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)m + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sh = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (memcmp(sh[i].Name, ".text\0\0\0", 8) == 0) { s_base = (uintptr_t)m + sh[i].VirtualAddress; s_size = sh[i].Misc.VirtualSize; break; }
    s_mod = (uintptr_t)m;
    if (!s_base || !s_size) { Log("codigo: no encontre la seccion .text de PES2021.exe, sin vigilancia"); return 0; }
    s_size = (s_size + 0xFFF) & ~(size_t)0xFFF;
    s_snap = (unsigned char*)VirtualAlloc(nullptr, s_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!s_snap) { Log("codigo: sin memoria para la copia (%zu MB), sin vigilancia", s_size >> 20); return 0; }
    const size_t np = s_size / 0x1000;
    s_skip.assign(np, 0);
    size_t bad = 0;
    for (size_t p = 0; p < np; p++) {
        uintptr_t a = s_base + p * 0x1000;
        if (!PageReadable(a) || !CwCopy(s_snap + p * 0x1000, (const void*)a, 0x1000)) { s_skip[p] = 1; bad++; }
    }
    s_ready = true;
    Log("codigo: vigilando la seccion .text de PES2021.exe (%zu MB, %zu paginas sin leer): cada 2 s se compara con la copia de ahora", s_size >> 20, bad);
    unsigned logged = 0, lastSummary = 0; double lastT = Now();
    std::vector<unsigned char> cur(0x1000);
    for (;;) {
        Sleep(2000);
        for (size_t p = 0; p < np; p++) {
            if (s_skip[p]) continue;
            const uintptr_t a = s_base + p * 0x1000; unsigned char* sp = s_snap + p * 0x1000;
            const int c = CwDiffers((const void*)a, sp, 0x1000);
            if (c == 0) continue;
            if (c < 0 || !CwCopy(cur.data(), (const void*)a, 0x1000)) {
                s_skip[p] = 1; Log("codigo: la pagina PES2021.exe+0x%llx dejo de poder leerse", (unsigned long long)(a - s_mod)); continue;
            }
            for (size_t i = 0; i < 0x1000;) {   // tramos que cambiaron (juntos si hay menos de 16 bytes iguales en el medio)
                if (cur[i] == sp[i]) { i++; continue; }
                size_t last = i, j = i;
                while (j < 0x1000 && j - last < 16) { if (cur[j] != sp[j]) last = j; j++; }
                const size_t len = last - i + 1;
                s_changes.fetch_add(1);
                if (logged < 200) {
                    logged++;
                    char hb[3 * 24 + 4], ha[3 * 24 + 4], jmp[200];
                    const size_t show = std::min<size_t>(len, 24);
                    Hex(hb, sizeof hb, sp + i, show); Hex(ha, sizeof ha, cur.data() + i, show);
                    DescribeJump(cur.data() + i, std::min<size_t>(len + 8, 0x1000 - i), a + i, jmp, sizeof jmp);
                    Log("codigo del juego CAMBIADO en PES2021.exe+0x%llx (%zu bytes, hilo vigilante): antes %s%s | ahora %s%s%s",
                        (unsigned long long)(a + i - s_mod), len, hb, len > show ? " ..." : "", ha, len > show ? " ..." : "", jmp);
                }
                i = last + 1;
            }
            memcpy(sp, cur.data(), 0x1000);   // cada cambio se anota una sola vez
        }
        const double t = Now();
        if (t - lastT > 60.0) {   // resumen cada minuto (solo si hubo cambios nuevos)
            lastT = t;
            const unsigned n = s_changes.load();
            if (n != lastSummary) { Log("codigo: %u tramos del codigo del juego cambiaron desde que empezo la vigilancia", n); lastSummary = n; }
        }
    }
}

void CodeWatchStart() {
    if (s_started.exchange(true)) return;
    HANDLE h = CreateThread(nullptr, 0, WatchThread, nullptr, 0, nullptr);
    if (!h) { Log("codigo: no se pudo crear el hilo de vigilancia"); return; }
    SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL);
    CloseHandle(h);
}

// bytes de la copia (para el registro de cierres): false si la direccion no esta en .text o todavia no hay copia
bool CodeWatchOrig(uintptr_t a, unsigned char* out, size_t n) {
    if (!s_ready.load() || a < s_base || a + n > s_base + s_size) return false;
    memcpy(out, s_snap + (a - s_base), n);
    return true;
}

} // namespace fx
