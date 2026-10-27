// PES21 FX - distancia del pasto 3D del juego (26/10). Idea de Agustin: en vez de emitir hojas propias en toda la cancha,
// forzar los parametros del motor que recortan el pasto 3D del juego por distancia y despues montar el shader nuevo encima
// (ese pasto ya trae el color de cada estadio, el desgaste y la sombra plana de los jugadores).
//
// gfx_enhancer.lua (Sider) trae un parche "Pasto 3D sin recorte de distancia": busca 41 0F 28 D0 48 8B CB E8
// (movaps xmm2,xmm8 ; mov rcx,rbx ; call) y pisa el movaps. Probado con Sider el 26/10: 0 coincidencias. El patron no esta en
// el exe en disco ni en el volcado de exedump.cpp: el codigo de render del motor vive en la seccion .debug$P (393 MB), que
// esta cifrada en disco y se descifra en memoria DESPUES del arranque de Sider.
//
// Aca la DLL lo busca con el juego ya andando (45 s despues del primer Present y cada 60 s hasta encontrarlo, 20 veces como
// mucho), en un hilo aparte y solo leyendo (VirtualQuery antes de cada region; PAGE_GUARD / NOACCESS no se tocan):
//  - toda la imagen de PES2021.exe y la memoria ejecutable privada (por si la proteccion descifra a otro lado);
//  - en el log: cuanto de cada seccion grande se pudo leer y su entropia (8 bits/byte = sigue cifrada, ~6 = codigo),
//    cada coincidencia con 64 bytes antes / 40 despues y a donde va el call;
//  - [Motor] pasto_sin_recorte = 1 y UNA sola coincidencia (con la version 62787C6A del exe): pisa el movaps con un nop de
//    4 bytes (0F 1F 40 00, una sola instruccion escrita de una vez) y deja la proteccion como estaba;
//  - una ventana de 256 KB alrededor de cada coincidencia queda en PES21FX\dump\pasto_<rva>.bin, y con
//    [Motor] volcar_debugp = 1 la seccion .debug$P entera (descifrada) en PES21FX\dump\debugP.bin, una vez por version,
//    para analizarla en la PC (tools/re, capstone).
// [Motor] pasto_buscar = 0 apaga todo esto.
#include "core.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace fx {

std::string ModOff(void* p);

static const unsigned kGdExeStamp = 0x62787C6A;
// sin el prefijo 41: 0F 28 D0 = movaps xmm2,xmm0; con 41 = movaps xmm2,xmm8 (el de gfx_enhancer)
static const unsigned char kGdTail[7] = { 0x0F, 0x28, 0xD0, 0x48, 0x8B, 0xCB, 0xE8 };
static const unsigned char kGdNop4[4] = { 0x0F, 0x1F, 0x40, 0x00 };
static const int kGdMaxScans = 20;

static std::atomic<bool> s_gdStarted{false};
static double s_gdFirstT = -1;

struct GdHit { uintptr_t at; unsigned char prefix; bool inImage; DWORD prot; };

static bool GdCopy(void* d, const void* s, size_t n) { __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }

static bool GdReadable(const MEMORY_BASIC_INFORMATION& mi) {
    if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (mi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}
static bool GdExec(DWORD p) { return (p & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0; }

static void GdHex(char* out, size_t cap, const unsigned char* b, size_t n) {
    size_t k = 0; out[0] = 0;
    for (size_t i = 0; i < n && k + 4 < cap; i++) k += snprintf(out + k, cap - k, "%02x ", b[i]);
    if (k) out[k - 1] = 0;
}

static bool GdWriteFile(const std::wstring& path, const void* data, size_t n) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const char* p = (const char*)data; bool ok = true;
    while (n) { DWORD w = 0, chunk = (DWORD)(n > (1u << 24) ? (1u << 24) : n); if (!WriteFile(h, p, chunk, &w, nullptr) || !w) { ok = false; break; } p += w; n -= w; }
    CloseHandle(h); return ok;
}

// recorre [lo, hi) por regiones legibles, en trozos de 1 MB que se solapan 8 bytes; cuenta bytes legibles, histograma y coincidencias
struct GdRange { uintptr_t lo, hi; size_t readable = 0; uint64_t hist[256] = {}; };
static void GdScanRange(GdRange& r, bool wantHist, bool execOnly, bool inImage, std::vector<GdHit>& hits, std::vector<unsigned char>& buf) {
    const size_t kChunk = 1u << 20;
    buf.resize(kChunk + 16);
    uintptr_t p = r.lo;
    while (p < r.hi) {
        MEMORY_BASIC_INFORMATION mi{};
        if (!VirtualQuery((void*)p, &mi, sizeof mi)) break;
        uintptr_t end = (uintptr_t)mi.BaseAddress + mi.RegionSize; if (end > r.hi) end = r.hi; if (end <= p) break;
        if (GdReadable(mi) && (!execOnly || GdExec(mi.Protect))) {
            for (uintptr_t q = p; q < end; q += kChunk) {
                size_t n = (size_t)((end - q) < kChunk ? (end - q) : kChunk);
                size_t nRead = n + ((q + n + 8 <= end) ? 8 : 0);   // solape para no perder un patron partido entre trozos
                if (!GdCopy(buf.data(), (const void*)q, nRead)) continue;
                r.readable += n;
                const unsigned char* b = buf.data();
                if (wantHist) for (size_t i = 0; i < n; i++) r.hist[b[i]]++;
                for (size_t i = 1; i + sizeof kGdTail <= nRead && i <= n; i++) {
                    if (b[i] != 0x0F || b[i + 1] != 0x28 || b[i + 2] != 0xD0) continue;
                    if (memcmp(b + i, kGdTail, sizeof kGdTail) != 0) continue;
                    if (hits.size() < 64) hits.push_back({ q + i - 1, b[i - 1], inImage, mi.Protect });
                }
            }
        }
        p = end;
    }
}
static double GdEntropy(const uint64_t* h) {
    uint64_t t = 0; for (int i = 0; i < 256; i++) t += h[i];
    if (!t) return 0.0;
    double e = 0.0; for (int i = 0; i < 256; i++) if (h[i]) { double q = (double)h[i] / (double)t; e -= q * std::log2(q); }
    return e;
}

// pisa 4 bytes de codigo de una vez (el render del juego puede estar corriendo esa funcion en otro hilo)
static bool GdPatch4(uintptr_t at, const unsigned char* nb) {
    DWORD old = 0;
    if (!VirtualProtect((void*)at, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    bool ok = true;
    if ((at & 7) <= 4) {
        volatile LONG64* q = (volatile LONG64*)(at & ~(uintptr_t)7);
        LONG64 cur = *q, nw;
        do { nw = cur; memcpy((unsigned char*)&nw + (at & 7), nb, 4); } while (InterlockedCompareExchange64(q, nw, cur) != cur && ((cur = *q), true));
    } else {
        ok = GdCopy((void*)at, nb, 4);
    }
    DWORD tmp = 0; VirtualProtect((void*)at, 4, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, 4);
    return ok;
}

static void GdThread() {
    HMODULE mod = GetModuleHandleW(nullptr);
    const unsigned char* base = (const unsigned char*)mod;
    auto dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto nt = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    const unsigned stamp = (unsigned)nt->FileHeader.TimeDateStamp;
    const uintptr_t imgLo = (uintptr_t)base, imgHi = imgLo + nt->OptionalHeader.SizeOfImage;
    auto sec = IMAGE_FIRST_SECTION(nt);
    const unsigned nSec = nt->FileHeader.NumberOfSections;
    std::wstring dir = g_cfg.baseDir + L"dump\\";
    std::vector<unsigned char> buf;
    bool dumpedDebugP = false;
    size_t lastHits = 0;
    for (int scan = 1; scan <= kGdMaxScans; scan++) {
        if (scan > 1) Sleep(60000);
        double t0 = Now();
        std::vector<GdHit> hits;
        std::string secLine;
        uintptr_t dpLo = 0, dpHi = 0; size_t dpRead = 0; double dpEnt = 0.0;
        for (unsigned i = 0; i < nSec; i++) {
            char nm[9] = {}; memcpy(nm, sec[i].Name, 8);
            DWORD vs = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
            GdRange r; r.lo = imgLo + sec[i].VirtualAddress; r.hi = r.lo + vs;
            bool big = vs >= (1u << 20);
            GdScanRange(r, big && (scan == 1 || strcmp(nm, ".debug$P") == 0), false, true, hits, buf);
            if (big && (scan == 1 || strcmp(nm, ".debug$P") == 0)) {
                double e = GdEntropy(r.hist);
                char l[160]; snprintf(l, sizeof l, " %s %u MB (legible %zu%%, entropia %.2f)", nm, vs >> 20, vs ? r.readable * 100 / vs : 0, e);
                secLine += l;
            }
            if (strcmp(nm, ".debug$P") == 0) { dpLo = r.lo; dpHi = r.hi; dpRead = r.readable; dpEnt = GdEntropy(r.hist); }
        }
        // memoria ejecutable fuera de la imagen (privada: lo que la proteccion pueda haber descifrado aparte)
        size_t outExec = 0;
        {
            uintptr_t p = 0x10000, top = (uintptr_t)0x7FFFFFFEFFFF;
            while (p < top) {
                MEMORY_BASIC_INFORMATION mi{};
                if (!VirtualQuery((void*)p, &mi, sizeof mi)) break;
                uintptr_t end = (uintptr_t)mi.BaseAddress + mi.RegionSize; if (end <= p) break;
                if (mi.Type == MEM_PRIVATE && GdReadable(mi) && GdExec(mi.Protect) && (end <= imgLo || p >= imgHi)) {
                    GdRange r; r.lo = p; r.hi = end;
                    GdScanRange(r, false, true, false, hits, buf);
                    outExec += r.readable;
                }
                p = end;
            }
        }
        int nA = 0, nB = 0, nO = 0;
        for (auto& h : hits) { if (h.prefix == 0x41) nA++; else if (h.prefix >= 0x40 && h.prefix <= 0x4F) nO++; else nB++; }
        Log("pasto del juego: busqueda %d/%d (%.1f s):%s; ejecutable fuera del exe %zu KB; movaps xmm2,xmm8 + call: %d, movaps xmm2,xmm0 + call: %d, otros: %d",
            scan, kGdMaxScans, Now() - t0, secLine.c_str(), outExec >> 10, nA, nB, nO);
        // el detalle (bytes y ventana) solo cuando cambia lo encontrado, para no repetirlo en cada busqueda
        int shown = hits.size() == lastHits ? 8 : 0;
        lastHits = hits.size();
        for (auto& h : hits) {
            if (shown++ >= 8) break;
            unsigned char ctx[104] = {}; bool okc = GdCopy(ctx, (const void*)(h.at - 64), sizeof ctx);
            int32_t rel = 0; GdCopy(&rel, (const void*)(h.at + 8), 4);
            uintptr_t tgt = h.at + 12 + (intptr_t)rel;
            const char* sn = "?";
            static char snb[9];
            if (h.inImage) for (unsigned i = 0; i < nSec; i++) {
                uintptr_t lo = imgLo + sec[i].VirtualAddress, hi = lo + (sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData);
                if (h.at >= lo && h.at < hi) { memset(snb, 0, sizeof snb); memcpy(snb, sec[i].Name, 8); sn = snb; break; }
            }
            char a[400], b[200], where[64];
            snprintf(where, sizeof where, "memoria privada %p", (void*)h.at);
            if (okc) { GdHex(a, sizeof a, ctx, 64); GdHex(b, sizeof b, ctx + 64, 40); } else { strcpy(a, "(no se pudo leer)"); b[0] = 0; }
            Log("pasto del juego:   %s en %s (%s, proteccion 0x%X), call a %s", h.prefix == 0x41 ? "A (41 0f 28 d0)" : (h.prefix >= 0x40 && h.prefix <= 0x4F) ? "otro prefijo" : "B (0f 28 d0)", h.inImage ? ModOff((void*)h.at).c_str() : where, h.inImage ? sn : "-", (unsigned)h.prot, ModOff((void*)tgt).c_str());
            Log("pasto del juego:     antes:   %s", a);
            Log("pasto del juego:     despues: %s", b);
            // ventana de 256 KB para analizar la funcion en la PC
            if (h.inImage) {
                const size_t win = 256u << 10;
                uintptr_t lo = h.at > imgLo + win / 2 ? h.at - win / 2 : imgLo;
                std::vector<unsigned char> w(win, 0);
                size_t off = 0;
                while (off < win) {
                    MEMORY_BASIC_INFORMATION mi{};
                    if (!VirtualQuery((void*)(lo + off), &mi, sizeof mi)) break;
                    size_t run = (size_t)((uintptr_t)mi.BaseAddress + mi.RegionSize - (lo + off)); if (!run) run = 4096;
                    size_t n = run < win - off ? run : win - off;
                    if (GdReadable(mi)) GdCopy(w.data() + off, (const void*)(lo + off), n);
                    off += n;
                }
                CreateDirectoryW(dir.c_str(), nullptr);
                wchar_t fn[64]; swprintf(fn, 64, L"pasto_%llX.bin", (unsigned long long)(lo - imgLo));
                if (GdWriteFile(dir + fn, w.data(), w.size())) Log("pasto del juego:     ventana de 256 KB desde %s -> PES21FX\\dump\\pasto_%llX.bin", ModOff((void*)lo).c_str(), (unsigned long long)(lo - imgLo));
            }
        }
        // .debug$P entera, una vez por version del exe, si ya esta descifrada (entropia de codigo, no de datos cifrados)
        if (g_cfg.grassDumpDebugP && !dumpedDebugP && dpLo && dpRead * 10 >= (dpHi - dpLo) * 9 && dpEnt > 0.5 && dpEnt < 7.6 && (nA + nB + nO > 0 || scan >= 3)) {
            dumpedDebugP = true;
            char want[64]; snprintf(want, sizeof want, "timestamp = %08X\r\n", stamp);
            bool already = false;
            { FILE* f = _wfopen((dir + L"debugP.txt").c_str(), L"rb");
              if (f) { char tb[512] = {}; size_t n = fread(tb, 1, sizeof tb - 1, f); fclose(f); tb[n] = 0; already = strstr(tb, want) != nullptr; } }
            if (already) Log("pasto del juego: .debug$P ya esta volcada en PES21FX\\dump\\debugP.bin (misma version)");
            else {
                // de a 1 MB directo al archivo (lo que no se pueda leer queda en ceros)
                size_t sz = dpHi - dpLo;
                CreateDirectoryW(dir.c_str(), nullptr);
                HANDLE fh = CreateFileW((dir + L"debugP.bin").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                bool ok = fh != INVALID_HANDLE_VALUE;
                const size_t kChunk = 1u << 20;
                buf.resize(kChunk + 16);
                for (size_t off = 0; ok && off < sz; ) {
                    size_t n = sz - off < kChunk ? sz - off : kChunk;
                    MEMORY_BASIC_INFORMATION mi{};
                    if (VirtualQuery((void*)(dpLo + off), &mi, sizeof mi)) {
                        size_t run = (size_t)((uintptr_t)mi.BaseAddress + mi.RegionSize - (dpLo + off));
                        if (run && run < n) n = run;
                        if (!GdReadable(mi) || !GdCopy(buf.data(), (const void*)(dpLo + off), n)) memset(buf.data(), 0, n);
                    } else memset(buf.data(), 0, n);
                    DWORD w = 0;
                    if (!WriteFile(fh, buf.data(), (DWORD)n, &w, nullptr) || w != n) ok = false;
                    off += n;
                }
                if (fh != INVALID_HANDLE_VALUE) CloseHandle(fh);
                if (ok) {
                    char info[256]; snprintf(info, sizeof info, "%sva = %llX\r\nsize = %zX\r\nentropia = %.2f\r\nbusqueda = %d\r\n", want, (unsigned long long)(dpLo - imgLo), sz, dpEnt, scan);
                    GdWriteFile(dir + L"debugP.txt", info, strlen(info));
                }
                Log("pasto del juego: .debug$P (%zu MB, entropia %.2f) %s", sz >> 20, dpEnt, ok ? "-> PES21FX\\dump\\debugP.bin" : "NO SE PUDO ESCRIBIR");
            }
        }
        if (nA == 0) continue;   // el de gfx_enhancer todavia no aparece (puede descifrarse mas tarde, ya en el partido)
        // parche: solo el de gfx_enhancer (prefijo 41), una sola coincidencia, y la version del exe conocida
        if (!g_cfg.grassNoCull) { Log("pasto del juego: encontrado; [Motor] pasto_sin_recorte = 0, no se cambia nada"); return; }
        if (stamp != kGdExeStamp) { Log("pasto del juego: el exe es otra version (%08X, esperado %08X), no se cambia nada", stamp, kGdExeStamp); return; }
        if (nA != 1) { Log("pasto del juego: %d coincidencias de movaps xmm2,xmm8 + call (hace falta exactamente 1), no se cambia nada", nA); return; }
        for (auto& h : hits) {
            if (h.prefix != 0x41) continue;
            unsigned char before[4] = {}, after[4] = {};
            GdCopy(before, (const void*)h.at, 4);
            if (memcmp(before, "\x41\x0F\x28\xD0", 4) != 0) { Log("pasto del juego: los bytes cambiaron antes de parchear, no se cambia nada"); return; }
            bool ok = GdPatch4(h.at, kGdNop4);
            GdCopy(after, (const void*)h.at, 4);
            Log("pasto del juego: pasto 3D sin recorte de distancia en %s: movaps xmm2,xmm8 -> nop (%s, ahora %02x %02x %02x %02x)",
                ModOff((void*)h.at).c_str(), ok && memcmp(after, kGdNop4, 4) == 0 ? "aplicado" : "NO SE PUDO", after[0], after[1], after[2], after[3]);
            return;
        }
    }
    Log("pasto del juego: %d busquedas sin encontrar el patron; no se busca mas", kGdMaxScans);
}

void GrassDistPresent() {
    if (s_gdStarted.load() || !g_cfg.grassScan) return;
    double t = Now();
    if (s_gdFirstT < 0) { s_gdFirstT = t; return; }
    if (t - s_gdFirstT < 45.0) return;
    if (s_gdStarted.exchange(true)) return;
    std::thread(GdThread).detach();
}

} // namespace fx
