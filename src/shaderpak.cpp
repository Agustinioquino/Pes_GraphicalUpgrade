// PES21 FX - version publica: los shaders salen de PES21FX\shaders.pak (bytecode compilado y cifrado), nunca de HLSL.
// Formato y cifrado en pakcrypt.h; el paquete lo arma tools/pakbuild en la CI.
#include "core.h"
#include "pakcrypt.h"
#include <vector>

#ifndef PES21FX_PAK_K0
#define PES21FX_PAK_K0 0x0ULL
#define PES21FX_PAK_K1 0x0ULL
#endif

namespace fx {

static std::vector<uint8_t> s_file;
static std::vector<pak::Entry> s_table;
static bool s_tried = false, s_ok = false;

static void Keys(uint64_t& k0, uint64_t& k1) {
    // la clave entra ya mezclada con una mascara (CMake): asi no queda tal cual en el binario
    volatile uint64_t m0 = 0x6a09e667f3bcc908ULL, m1 = 0xbb67ae8584caa73bULL;
    k0 = (uint64_t)PES21FX_PAK_K0 ^ m0; k1 = (uint64_t)PES21FX_PAK_K1 ^ m1;
}

bool PakOpen() {
    if (s_tried) return s_ok;
    s_tried = true;
    std::wstring path = g_cfg.baseDir + L"shaders.pak";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { Log("shaders.pak: no encontrado"); return false; }
    DWORD sz = GetFileSize(h, nullptr), rd = 0;
    s_file.resize(sz);
    BOOL ok = sz ? ReadFile(h, s_file.data(), sz, &rd, nullptr) : FALSE;
    CloseHandle(h);
    if (!ok || rd != sz || sz < sizeof(pak::Header)) { Log("shaders.pak: no se pudo leer"); s_file.clear(); return false; }
    pak::Header hd; memcpy(&hd, s_file.data(), sizeof(hd));
    uint64_t tbytes = (uint64_t)hd.count * sizeof(pak::Entry);
    if (hd.magic != pak::kMagic || hd.count == 0 || hd.count > 100000 || sizeof(hd) + tbytes > sz) { Log("shaders.pak: formato invalido"); s_file.clear(); return false; }
    uint64_t k0, k1; Keys(k0, k1);
    s_table.resize(hd.count);
    memcpy(s_table.data(), s_file.data() + sizeof(hd), (size_t)tbytes);
    pak::Crypt((uint8_t*)s_table.data(), (size_t)tbytes, k0, k1, hd.tableNonce);
    if (pak::Fnv64(s_table.data(), (size_t)tbytes) != hd.tableCheck) { Log("shaders.pak: no corresponde a esta DLL"); s_table.clear(); s_file.clear(); return false; }
    for (auto& e : s_table) if (e.offset + e.size > sz) { Log("shaders.pak: entrada fuera del archivo"); s_table.clear(); s_file.clear(); return false; }
    s_ok = true;
    Log("shaders.pak: %u shaders compilados", hd.count);
    return true;
}

bool PakGet(int kind, uint64_t id, char type, std::string& out) {
    if (!PakOpen()) return false;
    for (auto& e : s_table) {
        if (e.kind != kind || e.id != id || (kind == pak::kKindReplace && e.type != (uint8_t)type)) continue;
        out.assign((const char*)s_file.data() + e.offset, e.size);
        uint64_t k0, k1; Keys(k0, k1);
        pak::Crypt((uint8_t*)out.data(), out.size(), k0, k1, e.nonce);
        if (pak::Fnv64(out.data(), out.size()) != e.check) { Log("shaders.pak: entrada danada"); out.clear(); return false; }
        return true;
    }
    return false;
}

bool PakGetNamed(const wchar_t* file, std::string& out) { return PakGet(pak::kKindNamed, pak::Fnv64Lower(file), 0, out); }

void PakListReplacements(std::vector<std::pair<uint64_t, char>>& out) {
    out.clear();
    if (!PakOpen()) return;
    for (auto& e : s_table) if (e.kind == pak::kKindReplace) out.push_back({e.id, (char)e.type});
}

}  // namespace fx
