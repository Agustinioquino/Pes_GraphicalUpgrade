// PES21 FX - paquete de shaders compilados (shaders.pak) para la version publica.
// Lo usan la DLL (shaderpak.cpp, para leer) y tools/pakbuild (para armarlo en la CI). Sin dependencias de Windows.
//
// El paquete guarda solo bytecode compilado por fxc (sin reflexion ni depuracion) y nunca el HLSL ni los nombres de archivo:
// cada entrada se identifica por el hash del shader del juego que reemplaza (kind 1) o por el FNV-64 del nombre del archivo
// en minusculas (kind 2, shaders propios como pes_fs_vs.hlsl). Tabla y datos van cifrados con un flujo derivado de una clave
// de 128 bits que la CI genera en cada compilacion (la misma clave entra a la DLL como definicion del compilador).
// Es ofuscacion: evita copiar los archivos del mod tal cual, no es criptografia fuerte.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace pak {

constexpr uint32_t kMagic = 0x31584650u;   // "PFX1"
constexpr int kKindReplace = 1, kKindNamed = 2;

#pragma pack(push, 1)
struct Header { uint32_t magic, count; uint64_t tableNonce, tableCheck; };
struct Entry {
    uint8_t kind, type;        // type: 'p' / 'v' / 'c' (kind 1); 0 en kind 2
    uint16_t pad;
    uint32_t size;             // bytes del bytecode
    uint64_t id;               // kind 1: hash del shader del juego; kind 2: Fnv64Lower(nombre)
    uint64_t nonce, check;     // check = Fnv64 del bytecode sin cifrar
    uint64_t offset;           // desde el principio del archivo
};
#pragma pack(pop)

inline uint64_t Fnv64(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data; uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
    return h;
}
// nombre de archivo en minusculas (ASCII), wide o char
template <class C> inline uint64_t Fnv64Lower(const C* s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (; *s; s++) { unsigned c = (unsigned)*s; if (c >= 'A' && c <= 'Z') c += 32; h ^= (uint8_t)c; h *= 0x100000001b3ULL; }
    return h;
}

inline uint64_t Mix(uint64_t z) {   // splitmix64
    z += 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
// cifra / descifra en el lugar (XOR con el flujo: la misma funcion hace las dos cosas)
inline void Crypt(uint8_t* data, size_t len, uint64_t k0, uint64_t k1, uint64_t nonce) {
    uint64_t s0 = Mix(k0 ^ Mix(nonce)), s1 = Mix(k1 ^ Mix(nonce ^ 0x5045533231465831ULL));
    for (size_t i = 0, blk = 0; i < len; blk++) {
        uint64_t ks = Mix(s0 + blk * 0xd1342543de82ef95ULL) ^ Mix(s1 ^ (blk * 0x2545f4914f6cdd1dULL));
        for (int b = 0; b < 8 && i < len; b++, i++) data[i] ^= (uint8_t)(ks >> (8 * b));
    }
}

}  // namespace pak
