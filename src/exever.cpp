// Version del exe y tabla de direcciones 1.07.02 -> 1.00 (ver exever.h).
#include "core.h"
#include "exever.h"
#include <cstring>

namespace fx {

static const unsigned kStamp107 = 0x62787C6A, kStamp100 = 0x5F5ED4BF;
static const uintptr_t kBase = 0x140000000ull;
struct Pair { uint32_t a107, a100; };
static const Pair kTab100[] = {
#include "exever_100.inc"
};
static const int kTab100N = (int)(sizeof kTab100 / sizeof kTab100[0]);

// XA() corre tambien en los inicializadores estaticos de los otros archivos (dentro de DllMain): nada de estaticos con guarda ni
// de configuracion aca. Todo sale de la cabecera del exe, y repetir la cuenta desde dos hilos da el mismo resultado.
static volatile long s_ver = -1;
static volatile long s_missN = 0; static uintptr_t s_miss[8];

int ExeVer() {
    long v = s_ver;
    if (v >= 0) return (int)v;
    v = 0;
    HMODULE mod = GetModuleHandleW(nullptr);
    if (mod) {
        auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
        if ((uintptr_t)mod == kBase) {
            if (nt->FileHeader.TimeDateStamp == kStamp107) v = 107;
            else if (nt->FileHeader.TimeDateStamp == kStamp100) v = 100;
        }
    }
    s_ver = v;
    return (int)v;
}

uintptr_t XA(uintptr_t a107) {
    if (ExeVer() != 100) return a107;            // 1.07.02 y exes desconocidos: como siempre (los desconocidos no pasan ExeOk)
    if (a107 >= kBase && a107 - kBase <= 0xffffffffull) {
        const uint32_t r = (uint32_t)(a107 - kBase);
        int lo = 0, hi = kTab100N - 1;
        while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            if (kTab100[mid].a107 == r) return kBase + kTab100[mid].a100;
            if (kTab100[mid].a107 < r) lo = mid + 1; else hi = mid - 1;
        }
    }
    const long i = InterlockedIncrement(&s_missN) - 1;
    if (i < 8) s_miss[i] = a107;
    return 0;
}

bool ExeStampOk(unsigned stamp) {
    if (stamp == kStamp107) return true;
    // una direccion sin traducir apaga todo en 1.00: mejor sin ganchos que con uno en cualquier lado
    return stamp == kStamp100 && g_cfg.optExe100 && s_missN == 0;
}

bool ExeOk() {
    HMODULE mod = GetModuleHandleW(nullptr);
    if (!mod || (uintptr_t)mod != kBase) return false;
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    return ExeStampOk((unsigned)nt->FileHeader.TimeDateStamp);
}

bool ExeCodeSection(uintptr_t* lo, size_t* size) {
    HMODULE mod = GetModuleHandleW(nullptr);
    if (!mod) return false;
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sh = IMAGE_FIRST_SECTION(nt);
    int pick = -1;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (memcmp(sh[i].Name, ".text\0\0\0", 8) == 0) { pick = i; break; }
    if (pick < 0 && ExeVer() == 100)             // en 1.00 el codigo del juego esta en la primera seccion ejecutable (".trace")
        for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
            if (sh[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) { pick = i; break; }
    if (pick < 0 || !sh[pick].Misc.VirtualSize) return false;
    *lo = (uintptr_t)mod + sh[pick].VirtualAddress; *size = sh[pick].Misc.VirtualSize;
    return true;
}

void ExeVerLog() {
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    const unsigned stamp = (unsigned)nt->FileHeader.TimeDateStamp;
    const int v = ExeVer();
    if (v == 107) Log("exe: PES2021.exe 1.07.02 (sello %08X): direcciones propias", stamp);
    else if (v == 100) {
        const long miss = s_missN;
        if (miss) {
            Log("exe: PES2021.exe 1.00 (sello %08X): %ld direcciones sin traducir, las partes que dependen del exe quedan apagadas", stamp, miss);
            for (long i = 0; i < miss && i < 8; i++) Log("exe:   sin traducir 0x%llx", (unsigned long long)s_miss[i]);
        } else if (!g_cfg.optExe100) Log("exe: PES2021.exe 1.00 (sello %08X): exe_100 = 0, las partes que dependen del exe quedan apagadas", stamp);
        else Log("exe: PES2021.exe 1.00 (sello %08X): direcciones traducidas con la tabla de %d (exe_100 = 0 lo apaga)", stamp, kTab100N);
    } else Log("exe: PES2021.exe desconocido (base %p, sello %08X): las partes que dependen del exe quedan apagadas", (void*)mod, stamp);
}

}
