// PES21 FX (04/11) - boca propia por jugador SIN tocar su cara (Agustin: "que usas para guardar el fisico de cada jugador podrias
// usarlo para guardar el oral o el mouth?"). Lo que se probo antes: un face.fpk con oral.fmdl adentro armado desde la cara de Konami
// (pisaba la cara del Dream Patch, que viene cifrada) y un oral.fmdl suelto en la carpeta de la cara (el juego no lo lee).
// Como funciona:
//  - Todas las bocas usan la misma malla comun (mouth.fmdl de TorsoBody). La boca de un perfil (tools/fox/mouth/build_player_vb.py)
//    tiene LOS MISMOS triangulos, pesos y huesos, otra forma de dientes: solo cambian sus dos buffers de vertices
//    (PES21FX\bocas\<boca>.vb).
//  - La boca se dibuja justo despues de su cara en el mismo contexto (asi ya heredaba el b11 de la cara): hooks.cpp guarda la
//    identidad de la ultima cara dibujada (hash de su mapa de normales, la misma que usan el pelo y el LOD de caras).
//  - caras.ini, en el perfil del jugador: "boca = <nombre>" (bocas\<nombre>.vb y, si existe, textures\pes_oral_<nombre>_bsm.dds
//    como color) y "cara = <hash>" (la cara de ese jugador). Si falta "cara", la DLL la aprende sola cuando en pantalla hay UN
//    SOLO personaje y es ese jugador (p. ej. el menu de edicion) y la guarda en PES21FX\bocas\caras_aprendidas.txt.
//  - En el dibujo de la boca (PS 63fab773) de esa cara: se dibuja con los buffers de vertices del perfil y el indice del juego
//    (mismo mecanismo que el LOD0 forzado: LodBind / LodRestore). Si algo no cuadra (cantidad de indices, paso de los buffers),
//    no se toca nada y queda la boca comun.
// No cambia nada de la boca comun ni de los demas jugadores. Registro: lineas "boca por jugador: ...".
#include "core.h"
#include <d3d11_1.h>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace fx {

namespace {
struct BocaVB { ID3D11Buffer* vb[2] = {}; UINT stride[2] = {}; UINT verts = 0, idx = 0; bool tried = false, ok = false; };
struct BocaProf { std::string sec, boca; uint32_t id = 0; uint64_t cara = 0; };
std::mutex s_mx;
std::vector<BocaProf> s_profs; FILETIME s_iniTime = {}; bool s_iniHave = false; double s_iniCheck = -1e9;
std::map<uint32_t, uint64_t> s_learned; bool s_learnedRead = false;   // id del jugador -> cara aprendida
std::map<std::string, BocaVB> s_vb;
// aprendizaje: caras que dibujaron boca en la ventana en curso
std::map<uint64_t, int> s_win; double s_winStart = 0;
std::atomic<int> s_swaps{0}, s_mouths{0}, s_noFace{0}, s_reject{0};
double s_lastLog = 0; int s_logs = 0; std::map<uint64_t, bool> s_seenFace;

void Trim(char*& a) { while (*a == ' ' || *a == '\t') a++; char* e = a + strlen(a); while (e > a && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0; }

std::wstring Widen(const std::string& a) { return std::wstring(a.begin(), a.end()); }

void ReadLearned() {   // con s_mx
    if (s_learnedRead) return; s_learnedRead = true;
    FILE* f = _wfopen((g_cfg.baseDir + L"bocas\\caras_aprendidas.txt").c_str(), L"rb"); if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* a = line; if (char* c = strchr(a, ';')) *c = 0; Trim(a); if (!*a) continue;
        char* eq = strchr(a, '='); if (!eq) continue; *eq = 0; char* v = eq + 1; Trim(a); Trim(v);
        const uint32_t id = (uint32_t)strtoul(a, nullptr, 10); const uint64_t h = strtoull(v, nullptr, 16);
        if (id && h) s_learned[id] = h;
    }
    fclose(f);
    if (!s_learned.empty()) Log("boca por jugador: %d caras aprendidas leidas de bocas\\caras_aprendidas.txt", (int)s_learned.size());
}

void LoadIni() {   // con s_mx; caras.ini, solo las claves de la boca (lo demas lo lee exprbones.cpp)
    const double now = Now();
    if (now - s_iniCheck < 10.0) return;
    s_iniCheck = now;
    ReadLearned();
    const std::wstring path = g_cfg.baseDir + L"caras.ini";
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    const bool have = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) != 0;
    if (have == s_iniHave && (!have || CompareFileTime(&fa.ftLastWriteTime, &s_iniTime) == 0)) return;
    s_iniHave = have; s_iniTime = fa.ftLastWriteTime; s_profs.clear();
    if (!have) return;
    FILE* f = _wfopen(path.c_str(), L"rb"); if (!f) return;
    char line[512]; BocaProf cur; bool in = false;
    auto flush = [&] { if (in && !cur.boca.empty()) s_profs.push_back(cur); };
    while (fgets(line, sizeof line, f)) {
        char* a = line; if (char* c = strchr(a, ';')) *c = 0; Trim(a); if (!*a) continue;
        if (*a == '[') { flush(); cur = BocaProf(); in = true; if (char* e = strchr(a, ']')) *e = 0; cur.sec = a + 1; continue; }
        char* eq = strchr(a, '='); if (!eq || !in) continue;
        *eq = 0; char* k = a; char* v = eq + 1; Trim(k); Trim(v);
        if (!_stricmp(k, "id")) cur.id = (uint32_t)strtoul(v, nullptr, 10);
        else if (!_stricmp(k, "boca")) { cur.boca = v; for (char& ch : cur.boca) if (!isalnum((unsigned char)ch) && ch != '_' && ch != '-') ch = '_'; }
        else if (!_stricmp(k, "cara")) cur.cara = strtoull(v, nullptr, 16);
    }
    flush(); fclose(f);
    for (const BocaProf& p : s_profs) {
        const auto it = s_learned.find(p.id);
        Log("boca por jugador: perfil [%s] id %u -> bocas\\%s.vb, cara %s", p.sec.c_str(), p.id, p.boca.c_str(),
            p.cara ? "en caras.ini" : (it != s_learned.end() ? "aprendida" : "sin saber todavia (se aprende con ese jugador SOLO en pantalla, p. ej. menu de edicion)"));
    }
}

BocaVB* LoadVB(const std::string& name) {   // con s_mx
    BocaVB& b = s_vb[name];
    if (b.tried) return b.ok ? &b : nullptr;
    b.tried = true;
    if (!g_dev.dev) { b.tried = false; return nullptr; }
    const std::wstring path = g_cfg.baseDir + L"bocas\\" + Widen(name) + L".vb";
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) { Log("boca por jugador: no esta bocas\\%s.vb (queda la boca comun)", name.c_str()); return nullptr; }
    std::vector<char> d; char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    uint32_t h[8] = {};
    if (d.size() < 32 || memcmp(d.data(), "PFXB", 4)) { Log("boca por jugador: bocas\\%s.vb no tiene el formato esperado", name.c_str()); return nullptr; }
    memcpy(h, d.data() + 4, 28);
    const uint32_t ver = h[0], nv = h[1], ni = h[2], s0 = h[3], s1 = h[4];
    if (ver != 1 || !nv || nv > 65535 || !s0 || !s1 || d.size() != 32 + (size_t)nv * (s0 + s1)) {
        Log("boca por jugador: bocas\\%s.vb con cabecera rara (version %u, %u vertices, pasos %u/%u, %u bytes)", name.c_str(), ver, nv, s0, s1, (unsigned)d.size());
        return nullptr;
    }
    const char* p = d.data() + 32;
    for (int i = 0; i < 2; i++) {
        const UINT st = i ? s1 : s0;
        D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = nv * st; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sd = {}; sd.pSysMem = p;
        if (FAILED(g_dev.dev->CreateBuffer(&bd, &sd, &b.vb[i]))) {
            Log("boca por jugador: no se pudo crear el buffer %d de bocas\\%s.vb", i, name.c_str());
            for (auto*& v : b.vb) if (v) { v->Release(); v = nullptr; }
            return nullptr;
        }
        b.stride[i] = st; p += (size_t)nv * st;
    }
    b.verts = nv; b.idx = ni; b.ok = true;
    Log("boca por jugador: bocas\\%s.vb cargado (%u vertices, %u indices)", name.c_str(), nv, ni);
    return &b;
}

// una vez por ventana de 2 s: si en pantalla hay UN solo personaje (con id) y en la ventana solo una cara dibujo boca, esa cara es
// la suya; si su perfil tiene boca y no tiene cara, se aprende y se guarda
void LearnTick(double now) {   // con s_mx
    if (now - s_winStart < 2.0) return;
    std::map<uint64_t, int> win; win.swap(s_win); s_winStart = now;
    if (win.size() != 1 || win.begin()->second < 20) return;
    static CharInfo ci[kCharSlots]; bool alive[kCharSlots] = {};
    CharSnapshot(ci, alive);
    int nAlive = 0, at = -1;
    for (int i = 0; i < kCharSlots; i++) if (alive[i]) { nAlive++; at = i; }
    if (nAlive != 1 || !ci[at].known) return;
    const uint32_t id = ci[at].id; const uint64_t face = win.begin()->first;
    for (const BocaProf& p : s_profs) {
        if (p.id != id || p.cara) continue;
        auto it = s_learned.find(id);
        if (it != s_learned.end() && it->second == face) return;
        s_learned[id] = face;
        if (FILE* f = _wfopen((g_cfg.baseDir + L"bocas\\caras_aprendidas.txt").c_str(), L"ab")) {
            fprintf(f, "%u = %012llx   ; %s\r\n", id, (unsigned long long)face, ci[at].name); fclose(f);
        }
        Log("boca por jugador: aprendida la cara de [%s] (jugador %u '%s'): %012llx (guardada en bocas\\caras_aprendidas.txt; tambien se puede poner 'cara = %012llx' en caras.ini)",
            p.sec.c_str(), id, ci[at].name, (unsigned long long)face, (unsigned long long)face);
        return;
    }
}
}   // namespace

// desde BeforeDraw, en cada dibujo indexado de la boca (PS 63fab773) con la cara anterior del contexto (0 = no se sabe).
// Devuelve true y llena L (con AddRef) si este dibujo va con los vertices de una boca propia; *bsm = su color (o nullptr)
bool BocaJugDraw(ID3D11DeviceContext* c, uint64_t face, UINT count, UINT start, LodIA& L, ID3D11ShaderResourceView** bsm) {
    *bsm = nullptr;
    s_mouths.fetch_add(1, std::memory_order_relaxed);
    if (!face) { s_noFace.fetch_add(1, std::memory_order_relaxed); return false; }
    std::string name;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        const double now = Now();
        LoadIni();
        if (s_profs.empty()) return false;
        s_win[face]++;
        LearnTick(now);
        for (const BocaProf& p : s_profs) {
            uint64_t want = p.cara;
            if (!want) { auto it = s_learned.find(p.id); if (it != s_learned.end()) want = it->second; }
            if (want && want == face) { name = p.boca; break; }
        }
        if (name.empty()) return false;
        BocaVB* b = LoadVB(name);
        if (!b) return false;
        // el dibujo del juego tiene que ser la boca comun entera con el mismo formato de vertice
        ID3D11Buffer* gv[2] = {}; UINT gs[2] = {}, go[2] = {};
        c->IAGetVertexBuffers(0, 2, gv, gs, go);
        const bool okIA = gv[0] && gv[1] && gs[0] == b->stride[0] && gs[1] == b->stride[1] && count == b->idx;
        if (!okIA) {
            s_reject.fetch_add(1, std::memory_order_relaxed);
            if (s_logs < 6) { s_logs++; Log("boca por jugador: [%s] dibujo de boca que no cuadra (indices %u desde %u, pasos %u/%u, se esperaba %u indices, pasos %u/%u): queda la comun",
                name.c_str(), count, start, gs[0], gs[1], b->idx, b->stride[0], b->stride[1]); }
        }
        if (!s_seenFace[face] && okIA) { s_seenFace[face] = true; Log("boca por jugador: cara %012llx -> boca '%s' (offsets del juego %u/%u)", (unsigned long long)face, name.c_str(), go[0], go[1]); }
        for (auto* v : gv) if (v) v->Release();
        if (!okIA) return false;
        L = LodIA();
        for (int i = 0; i < 2; i++) { L.vb[i] = b->vb[i]; L.vb[i]->AddRef(); L.stride[i] = b->stride[i]; L.offset[i] = 0; }
        c->IAGetIndexBuffer(&L.ib, &L.ibFmt, &L.ibOff);   // el indice del juego (misma topologia)
        if (!L.ib) { L.Release(); return false; }
        L.count = count; L.start = start; L.base = 0;
    }
    *bsm = FileTextureAny("file:textures\\pes_oral_" + name + "_bsm.dds");
    s_swaps.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// cada ~30 s (desde el registro de 'recursos por dibujo'): cuantas bocas salieron con la forma propia
void BocaJugLog() {
    const int m = s_mouths.exchange(0), sw = s_swaps.exchange(0), nf = s_noFace.exchange(0), rj = s_reject.exchange(0);
    if (!sw && !rj) return;
    Log("boca por jugador (30 s): %d de %d dibujos de boca con forma propia, %d sin cara conocida, %d no cuadraron", sw, m, nf, rj);
}

void BocaJugReset() {   // cambio de dispositivo / recarga: se vuelven a crear los buffers
    std::lock_guard<std::mutex> lk(s_mx);
    for (auto& kv : s_vb) for (auto*& v : kv.second.vb) if (v) { v->Release(); v = nullptr; }
    s_vb.clear(); s_iniCheck = -1e9; s_iniHave = false;
}

}   // namespace fx
