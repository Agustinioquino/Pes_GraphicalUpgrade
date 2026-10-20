// PES21 FX - volcado del exe desempaquetado (20/10). PES2021.exe en disco esta empaquetado: el codigo real solo existe en
// memoria con el juego corriendo. La DLL vive dentro del proceso, asi que lo puede leer directo y escribirlo a disco para
// analizarlo (distancias de sombras, LOD, streaming, TAA) y despues aplicar parches EN MEMORIA desde la DLL o Sider
// (el archivo del exe nunca se modifica). Una sola vez por version del juego: si PES21FX\dump\dump.txt ya tiene el mismo
// sello de tiempo del exe, no se repite. Corre en un hilo aparte ~40 s despues del primer Present.
//   PES21FX\dump\<seccion>.bin (memoria de cada seccion), dump.txt (base, secciones), imports.txt (DLL!funcion -> RVA del IAT)
#include "core.h"
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <cstdio>

namespace fx {

static std::atomic<bool> s_started{false};
static double s_firstT = -1;

static bool ReadableRegion(const void* p, size_t& runLen) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(p, &mi, sizeof(mi))) { runLen = 4096; return false; }
    size_t left = (size_t)((const char*)mi.BaseAddress + mi.RegionSize - (const char*)p);
    runLen = left ? left : 4096;
    if (mi.State != MEM_COMMIT) return false;
    DWORD pr = mi.Protect & 0xff;
    if (pr == PAGE_NOACCESS || (mi.Protect & PAGE_GUARD)) return false;
    return true;
}

static bool WriteFileAll(const std::wstring& path, const void* data, size_t n) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const char* p = (const char*)data; bool ok = true;
    while (n) { DWORD w = 0, chunk = (DWORD)(n > (1u << 24) ? (1u << 24) : n); if (!WriteFile(h, p, chunk, &w, nullptr) || !w) { ok = false; break; } p += w; n -= w; }
    CloseHandle(h); return ok;
}

static void DumpThread() {
    HMODULE mod = GetModuleHandleW(nullptr);
    const unsigned char* base = (const unsigned char*)mod;
    auto dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { Log("volcado del exe: cabecera MZ no encontrada"); return; }
    auto nt = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { Log("volcado del exe: cabecera PE no encontrada"); return; }
    std::wstring dir = g_cfg.baseDir + L"dump\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    char stamp[64]; snprintf(stamp, sizeof stamp, "timestamp = %08X\r\n", (unsigned)nt->FileHeader.TimeDateStamp);
    // ya volcado para esta version?
    { FILE* f = _wfopen((dir + L"dump.txt").c_str(), L"rb");
      if (f) { char buf[4096] = {}; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
        if (strstr(buf, stamp) && strstr(buf, "completo = 1")) { Log("volcado del exe: ya esta en PES21FX\\dump (misma version)"); return; } } }
    Log("volcado del exe: empezando (base %p, %u secciones)", base, (unsigned)nt->FileHeader.NumberOfSections);
    std::string info; char line[512];
    snprintf(line, sizeof line, "base = %p\r\nimagen = %X\r\nentrada = %X\r\n", base, (unsigned)nt->OptionalHeader.SizeOfImage, (unsigned)nt->OptionalHeader.AddressOfEntryPoint); info += line; info += stamp;
    auto sec = IMAGE_FIRST_SECTION(nt);
    std::vector<unsigned char> buf;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        char nm[9] = {}; memcpy(nm, sec[i].Name, 8);
        DWORD va = sec[i].VirtualAddress, vs = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        if (vs > 0x4000000) { snprintf(line, sizeof line, "%s va=%X size=%X carac=%08X (omitida: muy grande)\r\n", nm, va, vs, (unsigned)sec[i].Characteristics); info += line; continue; }
        buf.assign(vs, 0);
        size_t off = 0, readable = 0;
        while (off < vs) {
            size_t run = 0; bool ok = ReadableRegion(base + va + off, run);
            size_t n = run < vs - off ? run : vs - off;
            if (ok) { memcpy(buf.data() + off, base + va + off, n); readable += n; }
            off += n;
        }
        std::string safe; for (char ch : std::string(nm)) safe += (isalnum((unsigned char)ch) || ch == '_') ? ch : '_';
        std::wstring fn = dir + std::wstring(safe.begin(), safe.end()) + L".bin";
        bool w = WriteFileAll(fn, buf.data(), buf.size());
        snprintf(line, sizeof line, "%s va=%X size=%X carac=%08X legible=%zu%% -> %s\r\n", nm, va, vs, (unsigned)sec[i].Characteristics, vs ? readable * 100 / vs : 0, w ? (safe + ".bin").c_str() : "NO SE PUDO ESCRIBIR");
        info += line;
        Log("volcado del exe: %s (%u KB, %zu%% legible)", nm, vs / 1024, vs ? readable * 100 / vs : 0);
    }
    // tabla de importaciones (si el directorio apunta dentro de la imagen ya desempaquetada)
    std::string imp;
    auto& dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dd.VirtualAddress && dd.Size && dd.VirtualAddress < nt->OptionalHeader.SizeOfImage) {
        size_t run = 0;
        for (auto d = (const IMAGE_IMPORT_DESCRIPTOR*)(base + dd.VirtualAddress); ReadableRegion(d, run) && d->Name; d++) {
            if (d->Name >= nt->OptionalHeader.SizeOfImage) break;
            const char* dll = (const char*)(base + d->Name);
            DWORD thunkRva = d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk;
            if (!thunkRva || thunkRva >= nt->OptionalHeader.SizeOfImage) continue;
            auto th = (const IMAGE_THUNK_DATA64*)(base + thunkRva);
            for (DWORD k = 0; ReadableRegion(&th[k], run) && th[k].u1.AddressOfData; k++) {
                DWORD iat = d->FirstThunk + k * 8;
                if (th[k].u1.Ordinal & IMAGE_ORDINAL_FLAG64) snprintf(line, sizeof line, "%08X %s!#%u\r\n", (unsigned)iat, dll, (unsigned)(th[k].u1.Ordinal & 0xffff));
                else { ULONGLONG r = th[k].u1.AddressOfData; if (r >= nt->OptionalHeader.SizeOfImage) break; snprintf(line, sizeof line, "%08X %s!%s\r\n", (unsigned)iat, dll, ((const IMAGE_IMPORT_BY_NAME*)(base + r))->Name); }
                imp += line;
                if (imp.size() > 4 * 1024 * 1024) break;
            }
        }
    }
    WriteFileAll(dir + L"imports.txt", imp.data(), imp.size());
    info += "completo = 1\r\n";
    WriteFileAll(dir + L"dump.txt", info.data(), info.size());
    Log("volcado del exe: listo en PES21FX\\dump (%zu bytes de importaciones)", imp.size());
}

void ExeDumpPresent() {
    if (s_started.load() || !g_cfg.exeDump) return;
    double t = Now();
    if (s_firstT < 0) { s_firstT = t; return; }
    if (t - s_firstT < 40.0) return;   // el exe ya se desempaqueto y el juego termino de arrancar
    if (s_started.exchange(true)) return;
    std::thread(DumpThread).detach();
}

} // namespace fx
