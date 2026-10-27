// PES21 FX (27/10) - brazos, manos y muslos de TorsoBody para las caras reales.
// Las caras reales del parche traen face\real\<id>\sourceimages\#windx11\arm_bsm.ftex y thigh_bsm.ftex (el atlas de Konami a
// 2048 con el tono que eligio cada facemaker). El juego las usa en las mallas del brazo, de las manos y del muslo en lugar de
// arm_N_bsm / thigh_N_bsm de TorsoBody (4096), mientras el torso si usa torso_N_bsm de TorsoBody: salto de tono y de detalle
// justo en la linea de la manga (borde de arm.fmdl con pants_*_sub) y en el muslo, con o sin los shaders del mod.
// Aca, en esos dibujos (VS de la piel 1c02a92a, reconocidos por su cantidad de indices como en manos.cpp), si t0 no es una
// textura de 4096 se enlaza la de TorsoBody del tono del jugador. El tono N sale de la textura del torso del mismo personaje:
// el dibujo del torso lleva torso_N_bsm en t0; se copia un mip chico de esa textura a staging (en Present, contexto inmediato)
// y se compara byte a byte con el mismo mip de torso_1..6_bsm.ftex de TorsoBody (son los mismos datos: el juego las creo de
// esos archivos). El personaje de cada dibujo se reconoce por su bloque de huesos (b6: buffer + desplazamiento); si no
// coincide con ningun torso del cuadro vale el ultimo torso dibujado en ese contexto (el juego dibuja las mallas de un
// personaje seguidas).
// Fila 321: x prendido, y tambien muslos, z tono forzado (0 = el del torso, 1..6), w log de cada dibujo (los primeros).
// Las texturas se leen de livecpk\<carpeta>\Asset\model\character\common\sourceimages\#windx11 (la primera carpeta de livecpk
// de Sider que tenga arm_1_bsm.ftex de 4096; o la carpeta escrita en PES21FX\torsobody_texturas.txt) con un lector de FTEX
// propio (inflate.cpp) y se crean una sola vez por tono, con todos sus mips y en sRGB si la del juego lo es.
#include "core.h"
#include <d3d11_1.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fx {

bool InflateZlib(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t expect);   // inflate.cpp

namespace {

static const uint64_t kSkinVS = 0x1c02a92ab4ae3362ULL;
// mallas (cantidad de indices del dibujo): = kLimbs / kVars de manos.cpp
static const UINT kArmIdx[] = { 37458, 37461, 34272, 34278 };                     // arm, arm_cap, arm_glove, arm_glove_cap
static const UINT kThighIdx[] = { 50340, 33102, 21690, 51876, 24525, 15996 };     // thigh_short / middle / long y classic
static const UINT kTorsoIdx[] = { 26634, 27201, 34683 };                          // pants_*_sub mallas 1..3
static const int kTones = 6;

struct FtexInfo { uint16_t fmt = 0, w = 0, h = 0; int mips = 0; std::vector<std::vector<uint8_t>> data; };

static inline uint16_t Rd16(const std::vector<uint8_t>& d, size_t o) { return (uint16_t)(d[o] | (d[o + 1] << 8)); }
static inline uint32_t Rd32(const std::vector<uint8_t>& d, size_t o) { return (uint32_t)d[o] | ((uint32_t)d[o + 1] << 8) | ((uint32_t)d[o + 2] << 16) | ((uint32_t)d[o + 3] << 24); }

static bool ReadAll(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (1 << 28)) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    size_t got = 0;
    while (got < out.size()) {
        DWORD rd = 0; DWORD want = (DWORD)std::min<size_t>(out.size() - got, 1u << 24);
        if (!ReadFile(h, out.data() + got, want, &rd, nullptr) || rd == 0) { CloseHandle(h); return false; }
        got += rd;
    }
    CloseHandle(h);
    return true;
}
// cabecera y mips [from, to) de una FTEX (tools/ftex.py): 64 bytes de cabecera, tabla de mips de 16 bytes y, por mip, una tabla
// de trozos de 8 bytes (comprimido u16, descomprimido u16 (0 = 65536), desplazamiento u32 con el bit 31 = sin comprimir)
static bool ReadFtex(const std::vector<uint8_t>& d, FtexInfo& f, int from, int to) {
    if (d.size() < 64 || memcmp(d.data(), "FTEX", 4) != 0) return false;
    f.fmt = Rd16(d, 8); f.w = Rd16(d, 10); f.h = Rd16(d, 12); f.mips = d[16];
    if (f.mips < 1 || f.mips > 16 || d.size() < 64 + 16 * (size_t)f.mips) return false;
    f.data.resize(f.mips);
    for (int i = from; i < to && i < f.mips; i++) {
        const size_t e = 64 + 16 * (size_t)i;
        uint32_t off = Rd32(d, e), usz = Rd32(d, e + 4); uint16_t cc = Rd16(d, e + 14);
        std::vector<uint8_t> m; m.reserve(usz);
        for (int k = 0; k < cc; k++) {
            const size_t t = (size_t)off + 8 * (size_t)k; if (t + 8 > d.size()) return false;
            uint16_t c = Rd16(d, t), u = Rd16(d, t + 2); uint32_t o = Rd32(d, t + 4);
            const bool raw = (o & 0x80000000u) != 0; o &= 0x7fffffffu;
            const size_t s = (size_t)off + o; if (s + c > d.size()) return false;
            if (raw || (c == u && d[s] != 0x78)) m.insert(m.end(), d.begin() + s, d.begin() + s + c);
            else { std::vector<uint8_t> t2; if (!InflateZlib(&d[s], c, t2, u ? u : 65536)) return false; m.insert(m.end(), t2.begin(), t2.end()); }
        }
        if (m.size() != usz) return false;
        f.data[i] = std::move(m);
    }
    return true;
}
static bool FmtOf(uint16_t ftexFmt, bool srgb, DXGI_FORMAT* fmt, UINT* blk) {
    switch (ftexFmt) {
    case 2: *fmt = srgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM; *blk = 8; return true;
    case 3: *fmt = srgb ? DXGI_FORMAT_BC2_UNORM_SRGB : DXGI_FORMAT_BC2_UNORM; *blk = 16; return true;
    case 4: *fmt = srgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM; *blk = 16; return true;
    case 9: *fmt = DXGI_FORMAT_BC5_UNORM; *blk = 16; return true;
    case 11: *fmt = srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM; *blk = 16; return true;
    }
    return false;
}
static bool IsSrgb(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_BC1_UNORM_SRGB || f == DXGI_FORMAT_BC2_UNORM_SRGB || f == DXGI_FORMAT_BC3_UNORM_SRGB || f == DXGI_FORMAT_BC7_UNORM_SRGB || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}
static ID3D11ShaderResourceView* MakeTexture(const FtexInfo& f, bool srgb, const char* what) {
    DXGI_FORMAT fmt; UINT blk;
    if (!FmtOf(f.fmt, srgb, &fmt, &blk)) { Log("brazos de TorsoBody: %s: formato FTEX %u no previsto", what, f.fmt); return nullptr; }
    std::vector<D3D11_SUBRESOURCE_DATA> sd(f.mips);
    for (int i = 0; i < f.mips; i++) {
        const UINT w = std::max<UINT>(1, f.w >> i), h = std::max<UINT>(1, f.h >> i);
        const size_t need = (size_t)((w + 3) / 4) * ((h + 3) / 4) * blk;
        if (f.data[i].size() != need) { Log("brazos de TorsoBody: %s: mip %d tiene %zu bytes, esperaba %zu", what, i, f.data[i].size(), need); return nullptr; }
        sd[i].pSysMem = f.data[i].data(); sd[i].SysMemPitch = ((w + 3) / 4) * blk; sd[i].SysMemSlicePitch = (UINT)need;
    }
    D3D11_TEXTURE2D_DESC td{}; td.Width = f.w; td.Height = f.h; td.MipLevels = f.mips; td.ArraySize = 1; td.Format = fmt;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* t = nullptr; ID3D11ShaderResourceView* v = nullptr;
    if (FAILED(g_dev.dev->CreateTexture2D(&td, sd.data(), &t)) || !t) { Log("brazos de TorsoBody: %s: no se pudo crear la textura", what); return nullptr; }
    g_dev.dev->CreateShaderResourceView(t, nullptr, &v); t->Release();
    return v;
}

// ---------------------------------------------------------------- estado
static std::mutex s_mx;
static bool s_init = false, s_ok = false;
static std::wstring s_dir;                        // carpeta #windx11 de TorsoBody (con barra final)
static int s_cmpMip = -1; static UINT s_cmpW = 0; // mip de comparacion (64 px) y su ancho
static std::vector<uint8_t> s_torsoFp[kTones + 1];   // mip de comparacion de torso_N_bsm (1..6)
static ID3D11ShaderResourceView* s_arm[kTones + 1] = {}, *s_thigh[kTones + 1] = {};
static bool s_armFail[kTones + 1] = {}, s_thighFail[kTones + 1] = {};
static bool s_srgb = true, s_srgbKnown = false;
static std::unordered_map<ID3D11Resource*, int> s_texTone;   // textura del juego -> tono (1..6; 0 = no es un torso de TorsoBody; -1 = pendiente)
struct Pend { ID3D11Resource* res; ID3D11Texture2D* st; int tries; };
static std::vector<Pend> s_pend;
struct KeyN { ID3D11Buffer* b; UINT f; int n; };
static std::vector<KeyN> s_keys;                  // bloque de huesos -> tono, dibujos del cuadro
static thread_local int t_lastTone = 0;           // ultimo torso dibujado en este contexto
static std::atomic<int> s_logs{0};
static std::atomic<unsigned> s_swaps{0}, s_unknown{0};

static std::wstring Widen(const std::string& a) {
    int n = MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0'); if (n) MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, w.data(), n);
    return w;
}
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static std::wstring Trim(std::wstring s) {
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\r' || s.back() == L'\n' || s.back() == L'\t' || s.back() == L'"')) s.pop_back();
    size_t i = 0; while (i < s.size() && (s[i] == L' ' || s[i] == L'\t' || s[i] == L'"')) i++;
    return s.substr(i);
}
// la carpeta #windx11 de TorsoBody: PES21FX\torsobody_texturas.txt (una linea) o la primera raiz de livecpk de Sider que tenga
// arm_1_bsm.ftex de 4096 de ancho
static std::wstring FindDir() {
    std::vector<uint8_t> txt;
    if (ReadAll(g_cfg.baseDir + L"torsobody_texturas.txt", txt)) {
        std::string a(txt.begin(), txt.end()); size_t nl = a.find_first_of("\r\n"); if (nl != std::string::npos) a = a.substr(0, nl);
        std::wstring d = Trim(Widen(a));
        if (!d.empty() && d.back() != L'\\') d += L'\\';
        if (Exists(d + L"arm_1_bsm.ftex")) return d;
        Log("brazos de TorsoBody: torsobody_texturas.txt apunta a %ls pero ahi no esta arm_1_bsm.ftex", d.c_str());
    }
    const std::wstring game = g_cfg.baseDir + L"..\\";
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((game + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    std::wstring best;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        const std::wstring sd = game + fd.cFileName + L"\\";
        if (!Exists(sd + L"sider.ini")) continue;
        WIN32_FIND_DATAW fr; HANDLE hr = FindFirstFileW((sd + L"livecpk\\*").c_str(), &fr);
        if (hr == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fr.cFileName[0] == L'.') continue;
            const std::wstring d = sd + L"livecpk\\" + fr.cFileName + L"\\Asset\\model\\character\\common\\sourceimages\\#windx11\\";
            if (!Exists(d + L"arm_1_bsm.ftex")) continue;
            std::vector<uint8_t> hdr; FtexInfo f;
            if (ReadAll(d + L"arm_1_bsm.ftex", hdr) && ReadFtex(hdr, f, 0, 0) && f.w >= 4096) { best = d; break; }
            Log("brazos de TorsoBody: %ls tiene arm_1_bsm.ftex pero no es de 4096: se sigue buscando", d.c_str());
        } while (FindNextFileW(hr, &fr));
        FindClose(hr);
        if (!best.empty()) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}
// huellas de torso_1..6: el mip de 64 px
static void Init() {
    if (s_init) return;
    s_init = true;
    s_dir = FindDir();
    if (s_dir.empty()) { Log("brazos de TorsoBody: no encontre la carpeta de texturas de TorsoBody (livecpk\\*\\Asset\\model\\character\\common\\sourceimages\\#windx11 con arm_1_bsm.ftex de 4096): apagado"); return; }
    int got = 0;
    for (int n = 1; n <= kTones; n++) {
        wchar_t name[48]; swprintf(name, 48, L"torso_%d_bsm.ftex", n);
        std::vector<uint8_t> d; FtexInfo f;
        if (!ReadAll(s_dir + name, d) || !ReadFtex(d, f, 0, 0)) { Log("brazos de TorsoBody: falta o no se lee %ls", name); continue; }
        int mip = 0; UINT w = f.w; while (w > 64 && mip + 1 < f.mips) { mip++; w >>= 1; }
        if (s_cmpMip < 0) { s_cmpMip = mip; s_cmpW = w; }
        if (mip != s_cmpMip || w != s_cmpW) { Log("brazos de TorsoBody: %ls no tiene el mismo tamano que torso_1: no se puede reconocer ese tono", name); continue; }
        if (!ReadFtex(d, f, mip, mip + 1)) { Log("brazos de TorsoBody: %ls: no se pudo leer el mip %d", name, mip); continue; }
        s_torsoFp[n] = std::move(f.data[mip]); got++;
    }
    for (int a = 1; a <= kTones; a++) for (int b = a + 1; b <= kTones; b++)
        if (!s_torsoFp[a].empty() && s_torsoFp[a] == s_torsoFp[b]) Log("brazos de TorsoBody: torso_%d y torso_%d tienen el mismo mip de %u px: no se distinguen", a, b, s_cmpW);
    s_ok = got > 0;
    Log("brazos de TorsoBody: carpeta %ls, huellas de %d torsos (mip %d, %u px)%s", s_dir.c_str(), got, s_cmpMip, s_cmpW, s_ok ? "" : ": apagado");
}
static ID3D11ShaderResourceView* Tone(int n, bool thigh) {
    if (n < 1 || n > kTones) return nullptr;
    ID3D11ShaderResourceView** slot = thigh ? &s_thigh[n] : &s_arm[n]; bool* fail = thigh ? &s_thighFail[n] : &s_armFail[n];
    if (*slot || *fail) return *slot;
    wchar_t name[48]; swprintf(name, 48, thigh ? L"thigh_%d_bsm.ftex" : L"arm_%d_bsm.ftex", n);
    std::vector<uint8_t> d; FtexInfo f; char what[48]; snprintf(what, sizeof what, "%ls", name);
    if (!ReadAll(s_dir + name, d) || !ReadFtex(d, f, 0, 99)) { Log("brazos de TorsoBody: falta o no se lee %ls", name); *fail = true; return nullptr; }
    *slot = MakeTexture(f, s_srgb, what);
    if (!*slot) *fail = true;
    else Log("brazos de TorsoBody: %ls cargada (%ux%u, %d mips, formato %u%s)", name, f.w, f.h, f.mips, f.fmt, s_srgb ? ", sRGB" : "");
    return *slot;
}
static bool In(const UINT* v, size_t n, UINT x) { for (size_t i = 0; i < n; i++) if (v[i] == x) return true; return false; }
static ID3D11Resource* Tex0(ID3D11DeviceContext* c, D3D11_TEXTURE2D_DESC* desc) {   // con AddRef
    ID3D11ShaderResourceView* v = nullptr; c->PSGetShaderResources(0, 1, &v);
    if (!v) return nullptr;
    ID3D11Resource* r = nullptr; v->GetResource(&r); v->Release();
    if (!r) return nullptr;
    D3D11_RESOURCE_DIMENSION dim; r->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) { r->Release(); return nullptr; }
    ((ID3D11Texture2D*)r)->GetDesc(desc);
    return r;
}
static bool KeyOf(ID3D11DeviceContext* c, ID3D11Buffer** b, UINT* f) {
    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) || !c1) return false;
    UINT n = 0; *b = nullptr; *f = 0;
    c1->VSGetConstantBuffers1(6, 1, b, f, &n); c1->Release();
    if (*b) { (*b)->Release(); return true; }   // solo se usa como clave (sin tener la referencia)
    return false;
}
}   // namespace

// dibujo del VS de la piel: si es un brazo / mano / muslo con una textura que no es la de TorsoBody, la textura de TorsoBody
// del tono del jugador para enlazar en t0 (el que llama la devuelve despues del dibujo); nullptr = no cambiar nada
ID3D11ShaderResourceView* ArmsTextureFor(ID3D11DeviceContext* c, uint64_t vsHash, UINT indexCount) {
    if (vsHash != kSkinVS || g_cfg.live[321][0] < 0.5f || !g_dev.dev) return nullptr;
    const bool torso = In(kTorsoIdx, 3, indexCount);
    const bool arm = In(kArmIdx, 4, indexCount) || (indexCount && (indexCount == (UINT)(g_cfg.live[302][0] + 0.5f) || indexCount == (UINT)(g_cfg.live[302][1] + 0.5f)));
    const bool thigh = !arm && g_cfg.live[321][1] > 0.5f && In(kThighIdx, 6, indexCount);
    if (!torso && !arm && !thigh) return nullptr;
    std::lock_guard<std::mutex> lk(s_mx);
    Init();
    if (!s_ok) return nullptr;
    D3D11_TEXTURE2D_DESC td{}; ID3D11Resource* r = Tex0(c, &td);
    if (!r) return nullptr;
    const bool verbose = g_cfg.live[321][3] > 0.5f && s_logs.load(std::memory_order_relaxed) < 60;
    if (torso) {
        // el torso de TorsoBody (4096): que tono es (huella pendiente hasta que Present la lea)
        int tone = 0;
        if (td.Width >= 4096) {
            auto it = s_texTone.find(r);
            if (it == s_texTone.end()) { s_texTone[r] = -1; r->AddRef(); s_pend.push_back({ r, nullptr, 0 }); }
            else tone = it->second;
            if (!s_srgbKnown) { s_srgb = IsSrgb(td.Format); s_srgbKnown = true; }
        }
        if (tone > 0) {
            t_lastTone = tone;
            ID3D11Buffer* b; UINT f;
            if (KeyOf(c, &b, &f)) {
                bool had = false; for (KeyN& k : s_keys) if (k.b == b && k.f == f) { k.n = tone; had = true; break; }
                if (!had && s_keys.size() < 256) s_keys.push_back({ b, f, tone });
            }
        }
        r->Release();
        return nullptr;
    }
    // brazo, mano o muslo: solo si la textura no es la de TorsoBody (la de una cara real: el atlas de Konami a 2048 / 1024)
    r->Release();
    if (td.Width >= 4096 || td.Width < 1024 || td.Width != td.Height) return nullptr;
    int tone = (int)(g_cfg.live[321][2] + 0.5f);
    if (tone < 1 || tone > kTones) {
        tone = 0;
        ID3D11Buffer* b; UINT f;
        if (KeyOf(c, &b, &f)) for (const KeyN& k : s_keys) if (k.b == b && k.f == f) { tone = k.n; break; }
        if (!tone) tone = t_lastTone;
    }
    if (!tone) { s_unknown++; if (verbose) { s_logs++; Log("brazos de TorsoBody: %s de %ux%u sin torso conocido todavia (%u indices)", thigh ? "muslo" : "brazo / mano", td.Width, td.Height, indexCount); } return nullptr; }
    if (!s_srgbKnown) { s_srgb = IsSrgb(td.Format); s_srgbKnown = true; }
    ID3D11ShaderResourceView* v = Tone(tone, thigh);
    if (v) { s_swaps++; if (verbose) { s_logs++; Log("brazos de TorsoBody: %s de %ux%u (%u indices) -> %s_%d_bsm de TorsoBody", thigh ? "muslo" : "brazo / mano", td.Width, td.Height, indexCount, thigh ? "thigh" : "arm", tone); } }
    return v;
}

// Present (contexto inmediato): leer las huellas pendientes de los torsos del juego y olvidar las claves del cuadro
void ArmsPresent() {
    std::lock_guard<std::mutex> lk(s_mx);
    s_keys.clear();
    if (s_pend.empty() || !g_dev.imm || !g_dev.dev) return;
    for (size_t i = 0; i < s_pend.size();) {
        Pend& p = s_pend[i];
        bool done = false;
        if (!p.st) {
            D3D11_TEXTURE2D_DESC d; ((ID3D11Texture2D*)p.res)->GetDesc(&d);
            int mip = 0; UINT w = d.Width; while (w > s_cmpW && mip + 1 < (int)d.MipLevels) { mip++; w >>= 1; }
            if (w != s_cmpW) { s_texTone[p.res] = 0; done = true; }
            else {
                D3D11_TEXTURE2D_DESC sd = d; sd.Width = sd.Height = w; sd.MipLevels = 1; sd.ArraySize = 1; sd.Usage = D3D11_USAGE_STAGING;
                sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
                if (FAILED(g_dev.dev->CreateTexture2D(&sd, nullptr, &p.st)) || !p.st) { s_texTone[p.res] = 0; done = true; }
                else g_dev.imm->CopySubresourceRegion(p.st, 0, 0, 0, 0, p.res, D3D11CalcSubresource(mip, 0, d.MipLevels), nullptr);
            }
        } else {
            D3D11_MAPPED_SUBRESOURCE m{};
            HRESULT hr = g_dev.imm->Map(p.st, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
            if (SUCCEEDED(hr)) {
                int tone = 0;
                const UINT rows = (s_cmpW + 3) / 4;
                for (int n = 1; n <= kTones && !tone; n++) {
                    const std::vector<uint8_t>& fp = s_torsoFp[n];
                    if (fp.empty()) continue;
                    const size_t rowBytes = fp.size() / rows;
                    if (rowBytes * rows != fp.size() || rowBytes > m.RowPitch) continue;
                    bool eq = true;
                    for (UINT y = 0; y < rows && eq; y++) eq = memcmp((const uint8_t*)m.pData + (size_t)y * m.RowPitch, fp.data() + (size_t)y * rowBytes, rowBytes) == 0;
                    if (eq) tone = n;
                }
                g_dev.imm->Unmap(p.st, 0);
                s_texTone[p.res] = tone;
                Log("brazos de TorsoBody: textura del torso %p (%u px) -> %s", (void*)p.res, s_cmpW, tone ? ("tono " + std::to_string(tone)).c_str() : "no es un torso de TorsoBody");
                done = true;
            } else if (++p.tries > 120) { s_texTone[p.res] = 0; done = true; }
        }
        if (done) { if (p.st) p.st->Release(); s_pend.erase(s_pend.begin() + i); }   // la referencia a p.res queda en s_texTone (asi el puntero no se reusa)
        else i++;
    }
}

void ArmsStats(unsigned* swaps, unsigned* unknown) { if (swaps) *swaps = s_swaps.load(); if (unknown) *unknown = s_unknown.load(); }

}   // namespace fx
