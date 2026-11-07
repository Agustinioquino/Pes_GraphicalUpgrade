// pakbuild: arma PES21FX\shaders.pak con el bytecode compilado por fxc (version publica, sin HLSL).
//   pakbuild <salida.pak> <clave: 32 hex> <carpeta con *.cso>
// Cada .cso se llama como el fuente + ".cso": "18afff0f79d9aaa5-ps_replace.txt.cso" (reemplazo de un shader del juego) o
// "pes_fs_vs.hlsl.cso" (shader propio, se busca por el nombre del .hlsl).
#include "../../src/pakcrypt.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

static bool ParseKey(const std::string& hex, uint64_t& k0, uint64_t& k1) {
    if (hex.size() != 32) return false;
    for (char c : hex) if (!isxdigit((unsigned char)c)) return false;
    k0 = strtoull(hex.substr(0, 16).c_str(), nullptr, 16);
    k1 = strtoull(hex.substr(16, 16).c_str(), nullptr, 16);
    return true;
}

int main(int argc, char** argv) {
    if (argc != 4) { fprintf(stderr, "uso: pakbuild <salida.pak> <clave 32 hex> <carpeta cso>\n"); return 2; }
    uint64_t k0, k1;
    if (!ParseKey(argv[2], k0, k1)) { fprintf(stderr, "clave invalida\n"); return 2; }
    struct Item { pak::Entry e; std::vector<uint8_t> data; std::string name; };
    std::vector<Item> items;
    std::random_device rd;
    auto rnd64 = [&]() { return ((uint64_t)rd() << 32) ^ rd(); };
    std::vector<fs::path> files;
    for (auto& de : fs::directory_iterator(argv[3])) if (de.is_regular_file() && de.path().extension() == ".cso") files.push_back(de.path());
    std::sort(files.begin(), files.end());
    for (auto& p : files) {
        std::string src = p.filename().string(); src.resize(src.size() - 4);   // sin ".cso"
        std::string low = src; for (auto& c : low) c = (char)tolower((unsigned char)c);
        Item it{}; it.name = src;
        if (low.size() == 31 && low[16] == '-' && low.compare(19, 12, "_replace.txt") == 0) {
            it.e.kind = pak::kKindReplace; it.e.type = (uint8_t)low[17];
            if (it.e.type != 'p' && it.e.type != 'v' && it.e.type != 'c') { fprintf(stderr, "tipo desconocido: %s\n", src.c_str()); return 1; }
            it.e.id = strtoull(low.substr(0, 16).c_str(), nullptr, 16);
        } else if (low.size() > 5 && low.compare(low.size() - 5, 5, ".hlsl") == 0) {
            it.e.kind = pak::kKindNamed; it.e.id = pak::Fnv64Lower(low.c_str());
        } else { fprintf(stderr, "nombre no reconocido: %s\n", src.c_str()); return 1; }
        std::ifstream f(p, std::ios::binary);
        it.data.assign(std::istreambuf_iterator<char>(f), {});
        if (it.data.empty()) { fprintf(stderr, "vacio: %s\n", src.c_str()); return 1; }
        it.e.size = (uint32_t)it.data.size();
        it.e.check = pak::Fnv64(it.data.data(), it.data.size());
        it.e.nonce = rnd64();
        for (auto& o : items) if (o.e.kind == it.e.kind && o.e.id == it.e.id && o.e.type == it.e.type) { fprintf(stderr, "repetido: %s / %s\n", o.name.c_str(), src.c_str()); return 1; }
        items.push_back(std::move(it));
    }
    if (items.empty()) { fprintf(stderr, "no hay .cso en %s\n", argv[3]); return 1; }
    pak::Header h{}; h.magic = pak::kMagic; h.count = (uint32_t)items.size(); h.tableNonce = rnd64();
    uint64_t off = sizeof(h) + sizeof(pak::Entry) * items.size();
    std::vector<pak::Entry> table;
    for (auto& it : items) { it.e.offset = off; off += it.data.size(); table.push_back(it.e); }
    h.tableCheck = pak::Fnv64(table.data(), table.size() * sizeof(pak::Entry));
    pak::Crypt((uint8_t*)table.data(), table.size() * sizeof(pak::Entry), k0, k1, h.tableNonce);
    std::ofstream o(argv[1], std::ios::binary);
    o.write((const char*)&h, sizeof(h));
    o.write((const char*)table.data(), table.size() * sizeof(pak::Entry));
    int nr = 0, nn = 0;
    for (auto& it : items) {
        pak::Crypt(it.data.data(), it.data.size(), k0, k1, it.e.nonce);
        o.write((const char*)it.data.data(), it.data.size());
        (it.e.kind == pak::kKindReplace ? nr : nn)++;
    }
    if (!o) { fprintf(stderr, "no se pudo escribir %s\n", argv[1]); return 1; }
    printf("shaders.pak: %d reemplazos + %d propios, %llu bytes\n", nr, nn, (unsigned long long)off);
    return 0;
}
