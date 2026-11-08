// PES21 FX - (04/11) caras en alta desde una carpeta (pedido de Agustin: "upscalear las texturas de las faces y que se guarden").
//
// 1) Volcado: la primera vez que se genera la quinta textura de una cara (facegen.cpp, Present, contexto inmediato) se guardan
//    sus texturas TAL CUAL en PES21FX\caras_hd\originales\<id>_bsm.dds (t0), _nrm.dds (t1), _srm.dds (t2), _trm.dds (t3)
//    + <id>_info.txt (formato de vista de cada una). Una sola vez por cara (si el archivo ya existe no se vuelve a escribir).
// 2) tools/caras_hd.py (en la PC, con el juego cerrado) escribe PES21FX\caras_hd\<id>_<tex>_4k.dds y _2k.dds con todos los mips.
// 3) Carga: si existen, se cargan en un hilo aparte; en el dibujo de la PIEL de la cara (solo la regla facegen de los PS
//    5ce3ed87 / ce188ae1; la boca 63fab773 no tiene esa regla y no se toca) se cambian t0..t3 por las grandes.
//    4096 solo para las caras "cercanas" (cuadros con pocas caras distintas: editor, repeticion, celebracion), 2048 para el resto.
//    De lejos la GPU elige sola los mips chicos.
// (04/11 b) Pelo y barba: misma idea con los PS del pelo (c75f6047, barba 02f1d510; t0 bsm con alfa, t1 normal, t2 srm_hair,
//    t3 translucent). Id = 'p' + huella de su t0 (p<hash>_bsm.dds ...). Tambien en la composicion (4a848614) y la sombra (213f9f12)
//    del pelo, solo en los slots que tienen la MISMA textura del juego que se volco (alfa igual en las tres pasadas).
// Interruptor: PES21FX\caras_hd\caras_hd.ini (se relee cada 2 s): activo, volcar, caras_4k, caras_cerca.
#include "core.h"
#include <d3d11_1.h>
#include <DDSTextureLoader.h>
#include <ScreenGrab.h>
#include <unordered_map>
#include <map>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <string>
#include <set>
#include <cstdio>
#include <algorithm>

namespace fx {

static const char* kTexName[4] = { "bsm", "nrm", "srm", "trm" };   // t0 difusa (face_bsm), t1 normal, t2 srm, t3 trm (translucent)

struct HdTex { ID3D11ShaderResourceView* srv = nullptr; UINT size = 0; };
struct HdFace {
    HdTex t2k[4], t4k[4];
    bool has2k[4] = {}, has4k[4] = {};        // archivos presentes en la carpeta
    bool want4k = false, loading = false, tried2k = false, tried4k = false;
    DXGI_FORMAT orig[4] = {};                 // formato de la vista original (para avisar si el archivo no coincide en sRGB)
    UINT origW = 0;
    int lastFrame = -100000, last4kFrame = -100000;
};

static std::mutex s_mx;
static std::unordered_map<std::string, HdFace> s_faces;
static std::set<std::string> s_dumped;                    // caras ya revisadas para el volcado en esta sesion
static std::vector<std::pair<int, ID3D11ShaderResourceView*>> s_grave;   // (cuadro, vista) a liberar mas tarde (listas diferidas)
static int s_frame = 0;
static std::set<std::string> s_frameIds;                  // caras con la piel dibujada en este cuadro
static std::set<std::string> s_frameHair;                 // (04/11 b) pelos dibujados en este cuadro
struct HairKey { std::string id, n1; ID3D11Resource* orig[4] = {}; };   // n1 = huella de su normal (para reconocer texturas de cara)   // id vacio = pendiente; orig = recursos del juego (sin retener)
static std::unordered_map<ID3D11Resource*, HairKey> s_hairKeys;      // por la textura t0 del pelo
struct HairReq { ID3D11Resource* key; ID3D11ShaderResourceView* in[4]; };
static std::vector<HairReq> s_hairReqs;
static std::atomic<int> s_stBind{0}, s_stBind4k{0}, s_stMiss{0};

// configuracion (caras_hd.ini)
static std::atomic<bool> s_on{true}, s_dump{true}, s_pelo{false};   // (04/11 c) pelo apagado por defecto (crash 12:30 PC con f300006)
static std::atomic<int> s_max4k{4}, s_near{4};
static double s_iniNext = 0;

static std::wstring Dir() { return g_cfg.baseDir + L"caras_hd\\"; }
static std::wstring W(const std::string& a) { return std::wstring(a.begin(), a.end()); }
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static void ReadIni() {
    std::wstring ini = Dir() + L"caras_hd.ini";
    if (!Exists(ini)) {   // primera vez: carpeta e ini con los valores por defecto
        CreateDirectoryW(Dir().c_str(), nullptr);
        if (FILE* f = _wfopen(ini.c_str(), L"wb")) {
            fputs("; PES21FX caras en alta (caras_hd.cpp). Se relee cada 2 s con el juego abierto.\r\n"
                  "[caras_hd]\r\n"
                  "; 1 = usar las texturas reescaladas de esta carpeta (<id>_bsm_4k.dds ...), 0 = las del juego (para comparar)\r\n"
                  "activo = 1\r\n"
                  "; 1 = guardar las texturas originales de cada cara nueva en originales\\ (una vez por cara)\r\n"
                  "volcar = 1\r\n"
                  "; cuantas caras a 4096 a la vez como maximo (cada una ~85 MB de video); el resto usa 2048\r\n"
                  "caras_4k = 4\r\n"
                  "; un cuadro con esta cantidad de caras distintas o menos es un primer plano (editor, repeticion): esas caras piden 4096\r\n"
                  "caras_cerca = 4\r\n"
                  "; 1 = tambien pelo y barba (ids p...), 0 = solo caras\r\n"
                  "pelo = 0\r\n", f);
            fclose(f);
        }
    }
    s_on = GetPrivateProfileIntW(L"caras_hd", L"activo", 1, ini.c_str()) != 0;
    s_dump = GetPrivateProfileIntW(L"caras_hd", L"volcar", 1, ini.c_str()) != 0;
    s_max4k = (int)GetPrivateProfileIntW(L"caras_hd", L"caras_4k", 4, ini.c_str());
    s_near = (int)GetPrivateProfileIntW(L"caras_hd", L"caras_cerca", 4, ini.c_str());
    { bool p = GetPrivateProfileIntW(L"caras_hd", L"pelo", 0, ini.c_str()) != 0;
      if (p != s_pelo.load()) { s_pelo = p; Log("caras hd: pelo y barba %s (caras_hd.ini pelo = %d)", p ? "PRENDIDO" : "apagado", p ? 1 : 0); } }
}

// ---------------------------------------------------------------- carga en un hilo aparte
struct LoadJob { std::string id; bool big; };
static std::deque<LoadJob> s_jobs;
static std::condition_variable s_cv;
static bool s_threadOn = false;

// (08/11 g) cierres en CCommandList::LUCDeferDeletion (PES2021+0x1ff674a) justo al terminar de cargar una cara a 4096 (05:12 PC,
// editor, cara de E. Martinez): el hilo de carga creaba las texturas en el dispositivo mientras el hilo de dibujo del juego liberaba
// sus listas de comandos. Ahora el hilo solo lee los archivos a memoria; las texturas se crean en el Present (hilo de dibujo),
// una cara por cuadro.
struct ReadyJob { std::string id; bool big; std::vector<uint8_t> data[4]; };
static std::deque<ReadyJob> s_ready;   // con s_mx
static bool ReadFileAll(const std::wstring& path, std::vector<uint8_t>& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{}; bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 128 && sz.QuadPart < (1ll << 30);
    if (ok) { out.resize((size_t)sz.QuadPart); DWORD rd = 0; size_t at = 0;
        while (ok && at < out.size()) { DWORD want = (DWORD)(std::min)(out.size() - at, (size_t)(64u << 20)); ok = ReadFile(h, out.data() + at, want, &rd, nullptr) && rd > 0; at += rd; } }
    CloseHandle(h);
    if (!ok) out.clear();
    return ok;
}
static ID3D11ShaderResourceView* LoadOne(const std::vector<uint8_t>& data, const char* what, UINT* size) {
    ID3D11Resource* res = nullptr; ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = DirectX::CreateDDSTextureFromMemoryEx(g_dev.dev, data.data(), data.size(), 0, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0,
                                                       DirectX::DDS_LOADER_DEFAULT, &res, &srv);
    if (FAILED(hr)) { Log("caras hd: no se pudo crear %s (0x%08x)", what, (unsigned)hr); return nullptr; }
    if (res) { D3D11_RESOURCE_DIMENSION dim; res->GetType(&dim);
        if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { D3D11_TEXTURE2D_DESC d; ((ID3D11Texture2D*)res)->GetDesc(&d); *size = d.Width; }
        res->Release(); }
    return srv;
}
static void Worker() {
    for (;;) {
        LoadJob j;
        { std::unique_lock<std::mutex> lk(s_mx); s_cv.wait(lk, [] { return !s_jobs.empty(); }); j = s_jobs.front(); s_jobs.pop_front(); }
        bool want[4] = {};
        { std::lock_guard<std::mutex> lk(s_mx); auto it = s_faces.find(j.id); if (it == s_faces.end()) continue;
          for (int i = 0; i < 4; i++) want[i] = j.big ? it->second.has4k[i] : it->second.has2k[i]; }
        ReadyJob r; r.id = j.id; r.big = j.big;
        for (int i = 0; i < 4; i++) {
            if (!want[i]) continue;
            std::wstring p = Dir() + W(j.id) + L"_" + W(kTexName[i]) + (j.big ? L"_4k.dds" : L"_2k.dds");
            if (!ReadFileAll(p, r.data[i])) Log("caras hd: no se pudo leer %ls", p.c_str());
        }
        std::lock_guard<std::mutex> lk(s_mx);
        s_ready.push_back(std::move(r));
    }
}
// en el Present, con s_mx tomado: crea las texturas de UNA cara leida
static void CreateReady() {
    if (s_ready.empty() || !g_dev.dev) return;
    ReadyJob r = std::move(s_ready.front()); s_ready.pop_front();
    auto it = s_faces.find(r.id);
    if (it == s_faces.end()) return;
    HdFace& f = it->second;
    HdTex t[4]; int n = 0;
    for (int i = 0; i < 4; i++) {
        if (r.data[i].empty()) continue;
        char what[96]; snprintf(what, sizeof what, "%s_%s_%s", r.id.c_str(), kTexName[i], r.big ? "4k" : "2k");
        t[i].srv = LoadOne(r.data[i], what, &t[i].size);
        if (!t[i].srv) continue;
        n++;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd; t[i].srv->GetDesc(&vd);
        auto srgb = [](DXGI_FORMAT x) { return x == DXGI_FORMAT_BC1_UNORM_SRGB || x == DXGI_FORMAT_BC2_UNORM_SRGB || x == DXGI_FORMAT_BC3_UNORM_SRGB || x == DXGI_FORMAT_BC7_UNORM_SRGB || x == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || x == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; };
        if (f.orig[i] && srgb(f.orig[i]) != srgb(vd.Format)) Log("caras hd: OJO %s_%s: el juego la lee %s y el archivo es %s (colores cambiados); revisar tools/caras_hd.py", r.id.c_str(), kTexName[i], srgb(f.orig[i]) ? "sRGB" : "lineal", srgb(vd.Format) ? "sRGB" : "lineal");
    }
    f.loading = false;
    HdTex* dst = r.big ? f.t4k : f.t2k;
    for (int i = 0; i < 4; i++) { if (dst[i].srv) s_grave.push_back({ s_frame, dst[i].srv }); dst[i] = t[i]; }
    Log("caras hd: cara %s a %s: %d de 4 texturas cargadas (bsm %u, nrm %u, srm %u, trm %u px)", r.id.c_str(), r.big ? "4096" : "2048", n, t[0].size, t[1].size, t[2].size, t[3].size);
}
static void Queue(const std::string& id, bool big) {   // con s_mx tomado
    if (!s_threadOn) { s_threadOn = true; std::thread(Worker).detach(); }
    s_jobs.push_back({ id, big });
    s_cv.notify_one();
}
static void Scan(const std::string& id, HdFace& f) {   // que archivos hay para esta cara
    for (int i = 0; i < 4; i++) {
        std::wstring b = Dir() + W(id) + L"_" + W(kTexName[i]);
        f.has2k[i] = Exists(b + L"_2k.dds"); f.has4k[i] = Exists(b + L"_4k.dds");
    }
}

// ---------------------------------------------------------------- volcado (facegen.cpp, Present, contexto inmediato)
void CarasHdNewFace(ID3D11DeviceContext* c, const std::string& id, ID3D11ShaderResourceView* const in[4]) {
    if (id.empty() || !c) return;
    if (s_iniNext == 0) { ReadIni(); s_iniNext = Now() + 2.0; }
    DXGI_FORMAT fmt[4] = {}; UINT w0 = 0;
    for (int i = 0; i < 4; i++) if (in[i]) { D3D11_SHADER_RESOURCE_VIEW_DESC vd; in[i]->GetDesc(&vd); fmt[i] = vd.Format; }
    if (in[0]) { ID3D11Resource* r = nullptr; in[0]->GetResource(&r); if (r) { D3D11_RESOURCE_DIMENSION dim; r->GetType(&dim);
        if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { D3D11_TEXTURE2D_DESC d; ((ID3D11Texture2D*)r)->GetDesc(&d); w0 = d.Width; } r->Release(); } }
    bool doDump = false;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        if (id[0] != 'p') {   // (04/11 c) texturas de esta cara que antes pasaron por el camino del pelo como p<huella del t0>: que usen la cara
            for (auto& kv : s_hairKeys) {
                if (kv.second.n1 != id || kv.second.id == id) continue;
                auto pt = s_faces.find(kv.second.id);
                if (pt != s_faces.end() && !pt->second.loading) {
                    for (auto& x : pt->second.t2k) if (x.srv) s_grave.push_back({ s_frame, x.srv });
                    for (auto& x : pt->second.t4k) if (x.srv) s_grave.push_back({ s_frame, x.srv });
                    s_faces.erase(pt);
                }
                kv.second.id = id;
            } }
        HdFace& f = s_faces[id];
        for (int i = 0; i < 4; i++) f.orig[i] = fmt[i];
        f.origW = w0;
        Scan(id, f);
        doDump = s_dump && s_dumped.insert(id).second;
        bool any2k = f.has2k[0] || f.has2k[1] || f.has2k[2] || f.has2k[3];
        if (s_on && any2k && !f.tried2k && !f.loading) {   // (08/11 h) activo = 0: no se carga nada (se cargaba la trm_2k de Messi antes del cierre 08:19)
         f.tried2k = true; f.loading = true; Queue(id, false); }
    }
    if (!doDump) return;
    std::wstring od = Dir(); CreateDirectoryW(od.c_str(), nullptr); od += L"originales\\"; CreateDirectoryW(od.c_str(), nullptr);
    int wrote = 0, fail = 0;
    std::string info = "; caras_hd.cpp: texturas originales de la cara " + id + " (slot ; nombre ; ancho x alto ; mips ; formato del recurso ; formato de la vista)\r\n";
    for (int i = 0; i < 4; i++) {
        if (!in[i]) continue;
        ID3D11Resource* r = nullptr; in[i]->GetResource(&r); if (!r) continue;
        D3D11_RESOURCE_DIMENSION dim; r->GetType(&dim);
        if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
            D3D11_TEXTURE2D_DESC d; ((ID3D11Texture2D*)r)->GetDesc(&d);
            char line[200]; snprintf(line, sizeof(line), "t%d ; %s ; %ux%u ; %u ; %d ; %d\r\n", i, kTexName[i], d.Width, d.Height, d.MipLevels, (int)d.Format, (int)fmt[i]); info += line;
            std::wstring p = od + W(id) + L"_" + W(kTexName[i]) + L".dds";
            if (!Exists(p)) { HRESULT hr = DirectX::SaveDDSTextureToFile(c, r, p.c_str());
                if (SUCCEEDED(hr)) wrote++; else { fail++; Log("caras hd: no se pudo volcar %s_%s (formato %d, 0x%08x)", id.c_str(), kTexName[i], (int)d.Format, (unsigned)hr); } }
        }
        r->Release();
    }
    std::wstring ip = od + W(id) + L"_info.txt";
    if (!Exists(ip)) if (FILE* f = _wfopen(ip.c_str(), L"wb")) { fwrite(info.data(), 1, info.size(), f); fclose(f); }
    if (wrote || fail) Log("caras hd: cara %s (%u px): %d texturas guardadas en caras_hd\\originales%s", id.c_str(), w0, wrote, fail ? " (algunas fallaron)" : "");
}

// ---------------------------------------------------------------- en el dibujo de la piel (hooks.cpp, regla facegen; cualquier contexto)
// out[i] = textura grande para t<i> o null (dejar la del juego). Las vistas viven al menos 240 cuadros despues de soltarlas.
bool CarasHdGet(const std::string& id, ID3D11ShaderResourceView* out[4]) {
    for (int i = 0; i < 4; i++) out[i] = nullptr;
    if (!s_on || id.empty()) return false;
    std::lock_guard<std::mutex> lk(s_mx);
    s_frameIds.insert(id);
    auto it = s_faces.find(id); if (it == s_faces.end()) return false;
    HdFace& f = it->second; f.lastFrame = s_frame;
    bool any = false, big = false;
    for (int i = 0; i < 4; i++) {
        ID3D11ShaderResourceView* v = (f.want4k && f.t4k[i].srv) ? f.t4k[i].srv : f.t2k[i].srv;
        if (v && v == f.t4k[i].srv) big = true;
        if (v) { out[i] = v; any = true; }
    }
    if (any) { s_stBind++; if (big) s_stBind4k++; } else s_stMiss++;
    return any;
}

// ---------------------------------------------------------------- (04/11 b) pelo y barba (hooks.cpp, cualquier contexto)
void CarasHdHair(ID3D11DeviceContext* c, uint64_t psHash, ID3D11ShaderResourceView* out[4]) {
    for (int i = 0; i < 4; i++) out[i] = nullptr;
    if (!s_pelo || (!s_on && !s_dump)) return;
    const bool main = psHash == 0xc75f6047d5817df9ULL || psHash == 0x02f1d51037669342ULL;   // el pelo / la barba (las otras dos solo usan)
    if (!main) {   // composicion / sombra: nada que hacer si ningun pelo tiene texturas grandes
        bool any = false; { std::lock_guard<std::mutex> lk(s_mx); for (auto& kv : s_hairKeys) if (!kv.second.id.empty()) { any = true; break; } }
        if (!any) return; }
    ID3D11ShaderResourceView* in[4] = {}; c->PSGetShaderResources(0, 4, in);
    ID3D11Resource* res[4] = {};
    for (int i = 0; i < 4; i++) if (in[i]) in[i]->GetResource(&res[i]);
    if (res[0]) {
        std::lock_guard<std::mutex> lk(s_mx);
        auto it = s_hairKeys.find(res[0]);
        if (it == s_hairKeys.end()) {
            if (main && in[1] && in[2] && s_hairKeys.size() < 256 && s_hairReqs.size() < 8) {
                HairKey& k = s_hairKeys[res[0]]; for (int i = 0; i < 4; i++) k.orig[i] = res[i];
                HairReq r{ res[0], {} }; for (int i = 0; i < 4; i++) { r.in[i] = in[i]; if (in[i]) in[i]->AddRef(); }
                s_hairReqs.push_back(r);
            }
        } else if (!it->second.id.empty() && s_on) {
            s_frameHair.insert(it->second.id);
            auto ft = s_faces.find(it->second.id);
            if (ft != s_faces.end()) { HdFace& f = ft->second; f.lastFrame = s_frame;
                for (int i = 0; i < 4; i++) {
                    if (!res[i] || res[i] != it->second.orig[i]) continue;   // otra textura en ese slot: no tocar
                    ID3D11ShaderResourceView* v = (f.want4k && f.t4k[i].srv) ? f.t4k[i].srv : f.t2k[i].srv;
                    if (v) out[i] = v;
                } }
        }
    }
    for (int i = 0; i < 4; i++) { if (res[i]) res[i]->Release(); if (in[i]) in[i]->Release(); }
}
static void HairPresent(ID3D11DeviceContext* c) {   // fuera de s_mx: huella y volcado de 1 pelo nuevo por cuadro
    std::vector<HairReq> todo;
    { std::lock_guard<std::mutex> lk(s_mx); size_t n = std::min<size_t>(1, s_hairReqs.size());
      todo.assign(s_hairReqs.begin(), s_hairReqs.begin() + n); s_hairReqs.erase(s_hairReqs.begin(), s_hairReqs.begin() + n); }
    for (auto& r : todo) {
        // (04/11 c) primero la huella de la NORMAL (t1), igual que las caras: si es la textura de una cara ya registrada (la barba 3D
        // y otras partes usan la de la cara) se usa esa cara, sin volcarla otra vez con otro id; si no, pelo = 'p' + huella del t0
        std::string h; { ID3D11Resource* n = nullptr; if (r.in[1]) r.in[1]->GetResource(&n); if (n) { h = HashFaceTexture(c, n); n->Release(); } }
        if (!h.empty()) {
            bool isFace; { std::lock_guard<std::mutex> lk(s_mx); isFace = s_faces.count(h) > 0; }
            std::string id = h;
            if (!isFace) { std::string h0 = HashFaceTexture(c, r.key); id = h0.empty() ? std::string() : "p" + h0; }   // pelo: huella del t0 (ids ya volcados)
            if (id.empty()) { for (auto* v : r.in) if (v) v->Release(); continue; }
            { std::lock_guard<std::mutex> lk(s_mx); auto it = s_hairKeys.find(r.key); if (it != s_hairKeys.end()) { it->second.id = id; it->second.n1 = h; } }
            if (isFace) Log("caras hd: pelo con las texturas de la cara %s (usa las de la cara)", id.c_str());
            else { Log("caras hd: pelo %s: volcando", id.c_str()); CarasHdNewFace(c, id, r.in); Log("caras hd: pelo %s registrado", id.c_str()); }
        }
        for (auto* v : r.in) if (v) v->Release();
    }
}
// partido nuevo (facegen.cpp FaceGenClear): las direcciones de las texturas del pelo se pueden reutilizar
void CarasHdClearKeys() {
    std::lock_guard<std::mutex> lk(s_mx);
    s_hairKeys.clear();
    for (auto& r : s_hairReqs) for (auto* v : r.in) if (v) v->Release();
    s_hairReqs.clear();
}

// ---------------------------------------------------------------- Present: que caras piden 4096, cola de carga, liberar
void CarasHdPresent() {
    double t = Now();
    if (t > s_iniNext) { s_iniNext = t + 2.0; ReadIni(); }
    static double logNext = 0;
    if (g_dev.imm) HairPresent(g_dev.imm);
    static double scanNext = 0;
    std::lock_guard<std::mutex> lk(s_mx);
    s_frame++;
    CreateReady();   // (08/11 g) las texturas se crean aca, en el hilo de dibujo
    // cada 10 s: caras sin archivos reescalados -> volver a mirar la carpeta (tools/caras_hd.py corrido con el juego abierto)
    if (t > scanNext) { scanNext = t + 10.0;
        for (auto& kv : s_faces) { HdFace& f = kv.second; bool any = false; for (int i = 0; i < 4; i++) any |= f.has2k[i] || f.has4k[i];
            if (!any) { Scan(kv.first, f); f.tried2k = false; f.tried4k = false; } } }
    // primer plano: pocas caras distintas en este cuadro -> esas piden 4096
    bool close = !s_frameIds.empty() && (int)s_frameIds.size() <= s_near.load();
    if (close) for (auto& id : s_frameIds) { auto it = s_faces.find(id); if (it != s_faces.end()) it->second.last4kFrame = s_frame; }
    if (close) for (auto& id : s_frameHair) { auto it = s_faces.find(id); if (it != s_faces.end()) it->second.last4kFrame = s_frame; }   // pelo del primer plano
    s_frameIds.clear(); s_frameHair.clear();
    // las mas recientes con pedido de 4096 (hasta caras_4k) la tienen; el resto vuelve a 2048 a los ~10 s sin primer plano
    std::vector<std::pair<int, std::string>> cand;
    for (auto& kv : s_faces) if (s_frame - kv.second.last4kFrame < 600) cand.push_back({ kv.second.last4kFrame, kv.first });
    std::sort(cand.begin(), cand.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::set<std::string> keep; int nf = 0, nh = 0;   // caras_4k caras y caras_4k pelos
    for (auto& cd : cand) { int& n = cd.second[0] == 'p' ? nh : nf; if (n < s_max4k.load()) { n++; keep.insert(cd.second); } }
    for (auto& kv : s_faces) {
        HdFace& f = kv.second; bool w = keep.count(kv.first) > 0;
        bool any4k = f.has4k[0] || f.has4k[1] || f.has4k[2] || f.has4k[3];
        if (s_on && w && any4k && !f.tried4k && !f.loading) { f.tried4k = true; f.loading = true; Queue(kv.first, true); }
        if (!w && f.tried4k && !f.loading) {   // soltar la 4096 (la vista se libera mas tarde: puede estar en una lista diferida)
            for (auto& x : f.t4k) if (x.srv) { s_grave.push_back({ s_frame, x.srv }); x = HdTex(); }
            f.tried4k = false;
        }
        f.want4k = w;
        // caras que no se dibujan hace ~2 min: soltar tambien la 2048 (se recarga si vuelve)
        if (s_frame - f.lastFrame > 7200 && f.tried2k && !f.loading) {
            for (auto& x : f.t2k) if (x.srv) { s_grave.push_back({ s_frame, x.srv }); x = HdTex(); }
            f.tried2k = false;
        }
        if (s_on && !f.tried2k && !f.loading && s_frame - f.lastFrame <= 2 && (f.has2k[0] || f.has2k[1] || f.has2k[2] || f.has2k[3])) { f.tried2k = true; f.loading = true; Queue(kv.first, false); }
    }
    // liberar lo soltado hace mas de 240 cuadros
    size_t k = 0;
    for (auto& g : s_grave) { if (s_frame - g.first > 240) DeferRelease(g.second); else s_grave[k++] = g; }   // (08/11 e) + 30 s (listas del editor)
    s_grave.resize(k);
    if (t > logNext) { logNext = t + 15.0;
        int n2 = 0, n4 = 0; for (auto& kv : s_faces) { if (kv.second.t2k[0].srv || kv.second.t2k[2].srv) n2++; if (kv.second.t4k[0].srv || kv.second.t4k[2].srv) n4++; }
        if (s_stBind.load() || s_stMiss.load()) Log("caras hd: 15 s: %d dibujos de piel con texturas grandes (%d a 4096), %d sin archivos | caras con 2048 %d, con 4096 %d | %s",
            s_stBind.load(), s_stBind4k.load(), s_stMiss.load(), n2, n4, s_on ? "activo" : "APAGADO (caras_hd.ini activo = 0)");
        s_stBind = 0; s_stBind4k = 0; s_stMiss = 0; }
}

}  // namespace fx
