// PES21 FX (27/10) - inflate (zlib / deflate, RFC 1950 y 1951) sin dependencias: para leer las texturas FTEX del juego y de los
// mods (brazos.cpp). Cada trozo de una FTEX es un flujo zlib chico (hasta 64 KB sin comprimir), asi que no hace falta nada
// mas rapido que esto: bloques sin comprimir, Huffman fijo y Huffman dinamico, tabla de longitudes / distancias estandar.
#include "core.h"
#include <cstring>
#include <vector>

namespace fx {

namespace {
struct Bits {
    const uint8_t* p; size_t n, pos; uint32_t buf; int cnt; bool bad;
    Bits(const uint8_t* d, size_t len) : p(d), n(len), pos(0), buf(0), cnt(0), bad(false) {}
    uint32_t get(int k) {   // k <= 24 bits, LSB primero
        while (cnt < k) {
            if (pos >= n) { bad = true; return 0; }
            buf |= (uint32_t)p[pos++] << cnt; cnt += 8;
        }
        uint32_t v = buf & ((k == 32) ? 0xffffffffu : ((1u << k) - 1)); buf >>= k; cnt -= k; return v;
    }
    void align() { buf = 0; cnt = 0; }
};
// tabla de Huffman canonica: cuenta por longitud + simbolos ordenados (como zlib / puff)
struct Huff { uint16_t count[16]; uint16_t sym[320]; };
static bool Build(Huff& h, const uint8_t* len, int n) {
    memset(h.count, 0, sizeof(h.count));
    for (int i = 0; i < n; i++) h.count[len[i]]++;
    h.count[0] = 0;
    int left = 1;   // codigos que quedan por asignar: si queda negativo la tabla esta sobre-suscripta
    for (int l = 1; l < 16; l++) { left <<= 1; left -= h.count[l]; if (left < 0) return false; }
    uint16_t offs[16]; offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h.count[l];
    for (int i = 0; i < n; i++) if (len[i]) h.sym[offs[len[i]]++] = (uint16_t)i;
    return true;
}
static int Decode(Bits& b, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= (int)b.get(1);
        if (b.bad) return -1;
        int count = h.count[l];
        if (code - count < first) return h.sym[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -1;
}
static const uint16_t kLenBase[] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t kLenExtra[] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t kDistBase[] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t kDistExtra[] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static bool Codes(Bits& b, std::vector<uint8_t>& out, const Huff& lit, const Huff& dist) {
    for (;;) {
        int s = Decode(b, lit);
        if (s < 0) return false;
        if (s < 256) { out.push_back((uint8_t)s); continue; }
        if (s == 256) return true;
        s -= 257; if (s >= 29) return false;
        int len = kLenBase[s] + (int)b.get(kLenExtra[s]);
        int ds = Decode(b, dist);
        if (ds < 0 || ds >= 30) return false;
        size_t d = kDistBase[ds] + b.get(kDistExtra[ds]);
        if (b.bad || d > out.size()) return false;
        size_t from = out.size() - d;
        for (int i = 0; i < len; i++) out.push_back(out[from + i]);   // (puede solaparse: copia byte a byte)
    }
}
static bool Fixed(Bits& b, std::vector<uint8_t>& out) {
    static Huff lit, dist; static bool built = false;
    if (!built) {
        uint8_t l[288]; int i = 0;
        for (; i < 144; i++) l[i] = 8; for (; i < 256; i++) l[i] = 9; for (; i < 280; i++) l[i] = 7; for (; i < 288; i++) l[i] = 8;
        Build(lit, l, 288);
        uint8_t d[30]; for (i = 0; i < 30; i++) d[i] = 5;
        Build(dist, d, 30);
        built = true;
    }
    return Codes(b, out, lit, dist);
}
static bool Dynamic(Bits& b, std::vector<uint8_t>& out) {
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    int nlen = (int)b.get(5) + 257, ndist = (int)b.get(5) + 1, ncode = (int)b.get(4) + 4;
    if (b.bad || nlen > 286 || ndist > 30) return false;
    uint8_t lengths[320]; memset(lengths, 0, sizeof(lengths));
    for (int i = 0; i < ncode; i++) lengths[order[i]] = (uint8_t)b.get(3);
    Huff lencode; if (!Build(lencode, lengths, 19)) return false;
    int idx = 0;
    while (idx < nlen + ndist) {
        int s = Decode(b, lencode);
        if (s < 0) return false;
        if (s < 16) { lengths[idx++] = (uint8_t)s; continue; }
        int len = 0, rep;
        if (s == 16) { if (idx == 0) return false; len = lengths[idx - 1]; rep = 3 + (int)b.get(2); }
        else if (s == 17) rep = 3 + (int)b.get(3);
        else rep = 11 + (int)b.get(7);
        if (idx + rep > nlen + ndist) return false;
        while (rep--) lengths[idx++] = (uint8_t)len;
    }
    if (lengths[256] == 0) return false;
    Huff lit, dist;
    if (!Build(lit, lengths, nlen)) return false;
    if (!Build(dist, lengths + nlen, ndist)) return false;
    return Codes(b, out, lit, dist);
}
}   // namespace

// deflate crudo (RFC 1951). expect = tamano esperado (0 = cualquiera; se usa para reservar)
bool InflateRaw(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t expect) {
    out.clear(); if (expect) out.reserve(expect);
    Bits b(in, n);
    for (;;) {
        int last = (int)b.get(1), type = (int)b.get(2);
        if (b.bad) return false;
        bool ok;
        if (type == 0) {
            b.align();
            if (b.pos + 4 > n) return false;
            unsigned len = in[b.pos] | (in[b.pos + 1] << 8), nlen = in[b.pos + 2] | (in[b.pos + 3] << 8);
            b.pos += 4;
            if ((len ^ 0xffff) != nlen || b.pos + len > n) return false;
            out.insert(out.end(), in + b.pos, in + b.pos + len); b.pos += len; ok = true;
        } else if (type == 1) ok = Fixed(b, out);
        else if (type == 2) ok = Dynamic(b, out);
        else return false;
        if (!ok) return false;
        if (last) break;
    }
    return true;
}
// flujo zlib (RFC 1950): cabecera de 2 bytes + deflate + adler32 (no se comprueba: las FTEX ya traen el tamano)
bool InflateZlib(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t expect) {
    if (n < 6) return false;
    unsigned cmf = in[0], flg = in[1];
    if ((cmf & 0x0f) != 8 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20)) return false;
    return InflateRaw(in + 2, n - 2 - 4, out, expect);
}

}   // namespace fx
