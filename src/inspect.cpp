// PES21 FX - Inspector visual (19/10). Con el inspector prendido, cada "clase" de dibujo conocida (cara, pelo, kit,
// medias, botines, pasto, cielo...) se vuelve a dibujar una vez por cuadro, con un pixel shader blanco, en una
// textura chica propia (320x180). En Present se lee esa textura y se calcula el rectangulo que ocupa en pantalla;
// el menu dibuja encima flechas con el nombre de la clase, que hace el mod ahi y de donde sale cada textura
// (del juego / archivo del mod / generada en vivo). Costo: un dibujo extra por clase y cuadro; solo en modo debug.
#include "core.h"
#include <d3d11_1.h>
#include <vector>
#include <string>
#include <mutex>
#include <cstring>
#include <map>
#include <cstdio>
#include <algorithm>
#include "imgui.h"

namespace fx {

std::atomic<bool> g_inspectOn{false};
std::atomic<int> g_inspectSel{-1};

// Clases: vienen de PES21FX_inspector.ini (editable sin recompilar); si no existe, se usan estas por defecto.
//   [clase]            una seccion por clase
//   ps = 5ce3ed8702271bc1, aa7d...   hashes de pixel shader que la forman (uno o varios)
//   nombre = Cara (piel)
//   que = SSS por zonas, arrugas...  (que hace el mod ahi)
//   t0 = difusa  t1 = normal ...     (nombre de cada slot de textura del juego; opcional)
//   [inspector]  desconocidos = 1    tambien marcar los shaders de geometria que no estan en ninguna clase (hasta 6), con su hash
struct InspClass { std::vector<uint64_t> ps; std::string name, desc; std::map<int, std::string> slotNames; bool unknown = false; std::string group = "otros"; };
static std::string s_group = "todo";   // filtro de grupo del inspector (todo | cara | pelo | kit | estadio | desconocidos | ...)
static std::vector<InspClass> s_classes;
static bool s_unknownOn = true;
static const InspClass kDefaults[] = {
    { { 0x5ce3ed8702271bc1ULL }, "Cara (piel)",        "Shader 5ce3: SSS por zonas, arrugas, quinta textura (cavidades/poros), sudor, vello, semillas de cejas con hebras", { {0,"difusa"},{1,"normal (bsm)"},{2,"srm"},{3,"translucida"},{4,"suciedad"},{5,"normal animacion"},{6,"mascara arrugas"},{7,"mascara rotacion"},{15,"malla"},{20,"arrugas normal (mod)"},{21,"arrugas mascara (mod)"},{22,"mapa SSS (mod)"},{23,"quinta textura A"},{24,"quinta textura B"},{25,"zonas de cara (mod)"},{26,"mapa de pelo"} } },
    { { 0xaa7d1833f6a11481ULL }, "Piel del cuerpo",    "Shader aa7d: brazos, piernas y cuello con la misma luz de piel", {} },
    { { 0x63fab7735eee149cULL }, "Boca (interior)",    "Shader 63fa: dientes, encias y lengua", {} },
    { { 0x0b3f9684ad026e54ULL, 0x1fc2d9225371a498ULL }, "Pestanas / cejas (alfa)", "Shaders 0b3f/1fc2: partes de la cara con transparencia; mas definicion", {} },
    { { 0x02f1d51037669342ULL, 0xc75f6047d5817df9ULL }, "Pelo (tarjetas)", "Shaders 02f1/c75f: pelo del juego; inercia, viento, brillo anisotropico, hebras finas", { {0,"difusa"},{1,"normal"},{2,"srm pelo"},{3,"translucida"} } },
    { { 0x7e647d9c43b453f9ULL, 0xa41e7343f874154fULL, 0xc7ae89b63e889007ULL }, "Camiseta", "Shaders 7e64/a41e/c7ae: tela (patron por tipo), arrugas v3 por tension, costuras, mojado", { {27,"patrones de tela (mod)"} } },
    { { 0x55297925e0c66085ULL }, "Short / medias",     "Shader 5529: relieve y oclusion de pliegues; inercia de medias (VS 1c9b)", {} },
    { { 0x0cc62c9ee4d52538ULL, 0xce188ae110354aeaULL }, "Botines / guantes", "Shaders 0cc6/ce18", {} },
    { { 0x18afff0f79d9aaa5ULL }, "Pasto 3D",           "Shader 18af: hojas; desgaste, sombreado entre hojas, zonas secas", {} },
    { { 0x9adf9d4673d24359ULL }, "Cielo (domo)",       "Shader 9adf: cielo del juego (nubes volumetricas: pendiente de mover aca)", {} },
};
static std::string Trim2(const std::string& x) { size_t a = x.find_first_not_of(" \t\r\n"), b = x.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : x.substr(a, b - a + 1); }
static std::string Lower2(std::string x) { for (auto& ch : x) ch = (char)tolower((unsigned char)ch); return x; }
static bool ReadAll(const std::wstring& path, std::string& out) { FILE* f = _wfopen(path.c_str(), L"rb"); if (!f) return false; char buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n); fclose(f); return true; }
static void LoadClasses() {
    s_classes.clear(); s_unknownOn = true;
    std::string txt;
    if (ReadAll(g_cfg.baseDir + L"PES21FX_inspector.ini", txt)) {
        InspClass* cur = nullptr; bool inCfg = false; size_t pos = 0;
        while (pos <= txt.size()) {
            size_t e = txt.find('\n', pos); std::string l = Trim2(txt.substr(pos, e == std::string::npos ? std::string::npos : e - pos)); pos = e == std::string::npos ? txt.size() + 1 : e + 1;
            if (l.empty() || l[0] == ';' || l[0] == '#') continue;
            if (l[0] == '[') { std::string sec = Lower2(Trim2(l.substr(1, l.find(']') - 1))); inCfg = sec == "inspector"; if (!inCfg) { s_classes.push_back(InspClass()); cur = &s_classes.back(); cur->name = sec; } continue; }
            size_t eq = l.find('='); if (eq == std::string::npos) continue;
            std::string k = Lower2(Trim2(l.substr(0, eq))), v = Trim2(l.substr(eq + 1));
            if (inCfg) { if (k == "desconocidos") s_unknownOn = atoi(v.c_str()) != 0; continue; }
            if (!cur) continue;
            if (k == "ps") { std::string h; for (char ch : v + ",") { if (ch == ',' || ch == ' ') { h = Trim2(h); if (!h.empty()) cur->ps.push_back(strtoull(h.c_str(), nullptr, 16)); h.clear(); } else h += ch; } }
            else if (k == "nombre") cur->name = v;
            else if (k == "que") cur->desc = v;
            else if (k == "grupo") cur->group = Lower2(v);
            else if (k.size() > 1 && k[0] == 't' && isdigit((unsigned char)k[1])) cur->slotNames[atoi(k.c_str() + 1)] = v;
        }
        Log("inspector: PES21FX_inspector.ini leido (%d clases)", (int)s_classes.size());
    }
    if (s_classes.empty()) for (auto& d : kDefaults) s_classes.push_back(d);
}
static const int kW = 192, kH = 108;   // 19/10: mas chico (alcanza para el rectangulo)
static std::atomic<int> s_frame{0};   // se captura 1 de cada 3 cuadros (el rectangulo no necesita mas)

struct TexInfo { int slot; std::string desc; ID3D11ShaderResourceView* srv = nullptr; };   // srv con referencia propia (miniatura en la ficha)
struct ClassFrame {
    bool seen = false; bool bboxOk = false; int extra = 0; float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    UINT verts = 0; int kind = 0; bool deferred = false; uint64_t vs = 0;
    std::vector<TexInfo> tex;
    // recursos
    ID3D11Texture2D* rt = nullptr; ID3D11RenderTargetView* rtv = nullptr; ID3D11Texture2D* st[2] = {}; int stIdx = 0; bool stValid[2] = {};
};
static std::vector<ClassFrame> s_cls;   // una por clase (misma posicion que s_classes), mas las desconocidas al final
static std::vector<uint64_t> s_unkHash;   // hash de cada clase desconocida (indices >= s_classes.size())
static const int kMaxUnknown = 12;
static bool s_loaded = false;
static std::mutex s_mx;
// ---- capas editables (PES21FX_capas.ini): [<hash>]  tN = file:ruta | none   oculta = 1
std::atomic<bool> g_layersAny{false};
static std::map<uint64_t, LayerOv> s_layers; static std::mutex s_layMx;
static std::vector<std::string> s_files;   // "file:textures\\x.dds", "file:hair\\x.png", ... (para el selector)
std::atomic<int> g_layerSkips{0}; static int s_skipsShown = 0;
static std::atomic<int> s_solo{-1};   // clase "solo esta" (las demas clases conocidas no se dibujan)
bool InspectShouldSkip(uint64_t ps) {
    if (!g_inspectOn) return false;   // ocultar y "solo esta" son de debug: solo con el inspector prendido
    std::lock_guard<std::mutex> lk(s_layMx);
    auto it = s_layers.find(ps); if (it != s_layers.end() && it->second.hidden) return true;
    int so = s_solo.load();
    if (so >= 0 && so < (int)s_classes.size()) {
        for (uint64_t h : s_classes[so].ps) if (h == ps) return false;
        for (auto& c : s_classes) for (uint64_t h : c.ps) if (h == ps) return true;   // solo afecta a las clases conocidas
    }
    return false;
}
bool InspectLayerFor(uint64_t ps, LayerOv& out) {
    std::lock_guard<std::mutex> lk(s_layMx); auto it = s_layers.find(ps); if (it == s_layers.end()) return false; out = it->second; return true;
}
static void LayersUpdateAny() { bool any = s_solo.load() >= 0; for (auto& kv : s_layers) if (kv.second.hidden || !kv.second.slot.empty()) any = true; g_layersAny = any; }
static void ScanFiles() {
    s_files.clear();
    const wchar_t* dirs[] = { L"textures\\*.dds", L"textures\\*.png", L"hair\\*.png", L"hair\\*.dds", L"hair\\auto\\*.png" };
    for (auto* d : dirs) {
        std::wstring pat = g_cfg.baseDir + d; std::wstring sub = pat.substr(g_cfg.baseDir.size()); sub = sub.substr(0, sub.find_last_of(L'\\') + 1);
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { std::wstring w = sub + fd.cFileName; std::string a; for (wchar_t ch : w) a += (char)(ch < 128 ? ch : '?'); s_files.push_back("file:" + a); } } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
static void LoadLayers() {
    std::lock_guard<std::mutex> lk(s_layMx); s_layers.clear();
    std::string txt; uint64_t cur = 0;
    if (ReadAll(g_cfg.baseDir + L"PES21FX_capas.ini", txt)) {
        size_t pos = 0;
        while (pos <= txt.size()) {
            size_t e = txt.find('\n', pos); std::string l = Trim2(txt.substr(pos, e == std::string::npos ? std::string::npos : e - pos)); pos = e == std::string::npos ? txt.size() + 1 : e + 1;
            if (l.empty() || l[0] == ';') continue;
            if (l[0] == '[') { cur = strtoull(l.substr(1, l.find(']') - 1).c_str(), nullptr, 16); continue; }
            size_t eq = l.find('='); if (eq == std::string::npos || !cur) continue;
            std::string k = Lower2(Trim2(l.substr(0, eq))), v = Trim2(l.substr(eq + 1));
            if (k == "oculta") s_layers[cur].hidden = atoi(v.c_str()) != 0;
            else if (k.size() > 1 && k[0] == 't') s_layers[cur].slot[atoi(k.c_str() + 1)] = v;
        }
        Log("capas: PES21FX_capas.ini leido (%d shaders)", (int)s_layers.size());
    }
    LayersUpdateAny();
}
static bool SaveLayers() {
    std::lock_guard<std::mutex> lk(s_layMx);
    std::string out = "; PES21 FX - capas editadas desde el inspector (ventana Capas). [hash del pixel shader]  tN = file:ruta | none   oculta = 1\n";
    for (auto& kv : s_layers) { if (!kv.second.hidden && kv.second.slot.empty()) continue;
        char h[32]; snprintf(h, sizeof(h), "[%016llx]\n", (unsigned long long)kv.first); out += h;
        if (kv.second.hidden) out += "oculta = 1\n";
        for (auto& sl : kv.second.slot) out += "t" + std::to_string(sl.first) + " = " + sl.second + "\n";
        out += "\n"; }
    FILE* f = _wfopen((g_cfg.baseDir + L"PES21FX_capas.ini").c_str(), L"wb"); if (!f) return false; fwrite(out.data(), 1, out.size(), f); fclose(f); return true;
}
void InspectReload() { std::lock_guard<std::mutex> lk(s_mx); s_loaded = false; }
static ID3D11PixelShader* s_whitePS = nullptr; static bool s_psTried = false;
static ID3D11DepthStencilState* s_noDepth = nullptr; static ID3D11BlendState* s_noBlend = nullptr;
static float s_screenW = 1920, s_screenH = 1080;
// estado guardado entre InspectBegin e InspectEnd (por hilo: cada contexto diferido vive en su hilo)
struct Saved { ID3D11PixelShader* ps; ID3D11RenderTargetView* rt[8]; ID3D11DepthStencilView* ds; UINT nvp; D3D11_VIEWPORT vp[8]; ID3D11BlendState* bs; float bf[4]; UINT sm; ID3D11DepthStencilState* dss; UINT sref; int cls; };
static thread_local Saved t_sv;

static void EnsureRes() {
    if (!s_psTried && g_dev.dev) {
        s_psTried = true;
        ID3DBlob* pb = CompileNamedShader(L"pes_insp_ps.hlsl", "ps_5_0");
        if (pb) { g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_whitePS); pb->Release(); }
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = FALSE; dd.StencilEnable = FALSE; g_dev.dev->CreateDepthStencilState(&dd, &s_noDepth);
        D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; g_dev.dev->CreateBlendState(&bd, &s_noBlend);
        Log("inspector: %s", s_whitePS ? "listo" : "no compilo pes_insp_ps.hlsl");
    }
}
static bool EnsureClassRT(ClassFrame& f) {
    if (f.rt) return true;
    D3D11_TEXTURE2D_DESC d{}; d.Width = kW; d.Height = kH; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8_UNORM; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(g_dev.dev->CreateTexture2D(&d, nullptr, &f.rt))) return false;
    if (FAILED(g_dev.dev->CreateRenderTargetView(f.rt, nullptr, &f.rtv))) return false;
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (int i = 0; i < 2; i++) g_dev.dev->CreateTexture2D(&d, nullptr, &f.st[i]);
    return true;
}

static std::string DescribeSRV(ID3D11ShaderResourceView* v) {
    if (!v) return "";
    std::string origin = FaceGenOrigin(v);
    if (origin.empty()) origin = NamedOrigin(v);
    ID3D11Resource* res = nullptr; v->GetResource(&res);
    char buf[160] = "";
    if (res) {
        D3D11_RESOURCE_DIMENSION dim; res->GetType(&dim);
        if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { D3D11_TEXTURE2D_DESC td; ((ID3D11Texture2D*)res)->GetDesc(&td);
            snprintf(buf, sizeof(buf), "%ux%u fmt %d, %u mips%s", td.Width, td.Height, (int)td.Format, td.MipLevels, td.ArraySize > 1 ? ", array" : ""); }
        else if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) snprintf(buf, sizeof(buf), "buffer");
        else snprintf(buf, sizeof(buf), "dim %d", (int)dim);
        if (origin.empty()) { char h[32]; snprintf(h, sizeof(h), "del juego #%llx", (unsigned long long)(((uintptr_t)res >> 4) & 0xffffff)); origin = h; }
        res->Release();
    }
    return origin + "  (" + buf + ")";
}

// llamado desde FX_DRAW despues del dibujo real. Si devuelve true, el dibujo se repite (con nuestro PS y RT) y luego InspectEnd.
bool InspectBegin(ID3D11DeviceContext* c, const ShaderEntry* ps, const ShaderEntry* vs, int kind, UINT verts, UINT inst) {
    if (!ps || !g_dev.dev) return false;
    if ((s_frame.load() % 3) != 0) return false;
    EnsureRes(); if (!s_whitePS) return false;
    int ci = -1;
    std::lock_guard<std::mutex> lk(s_mx);
    if (!s_loaded) { LoadClasses(); LoadLayers(); ScanFiles(); s_loaded = true; for (auto& f : s_cls) { if (f.rt) f.rt->Release(); if (f.rtv) f.rtv->Release(); for (auto* t : f.st) if (t) t->Release(); for (auto& t : f.tex) if (t.srv) t.srv->Release(); } s_cls.clear(); s_unkHash.clear(); s_cls.resize(s_classes.size()); }
    for (size_t i = 0; i < s_classes.size() && ci < 0; i++) for (uint64_t h : s_classes[i].ps) if (h == ps->hash) { ci = (int)i; break; }
    if (ci < 0) {
        // desconocido: solo geometria con bastantes vertices en un dibujo con varios render targets (G-buffer), hasta kMaxUnknown
        if (!s_unknownOn || verts < 300) return false;
        for (size_t i = 0; i < s_unkHash.size(); i++) if (s_unkHash[i] == ps->hash) { ci = (int)(s_classes.size() + i); break; }
        if (ci < 0) {
            if ((int)s_unkHash.size() >= kMaxUnknown) return false;
            ID3D11RenderTargetView* rts[8] = {}; c->OMGetRenderTargets(8, rts, nullptr); int nrt = 0; for (auto* r : rts) if (r) { nrt++; r->Release(); }
            if (nrt < 3) return false;
            s_unkHash.push_back(ps->hash); s_cls.push_back(ClassFrame()); ci = (int)s_cls.size() - 1;
            char nm[64]; snprintf(nm, sizeof(nm), "%016llx", (unsigned long long)ps->hash);
            InspClass u; u.ps.push_back(ps->hash); u.name = std::string("Desconocido ") + nm; u.desc = "Shader sin clase: agregalo a PES21FX_inspector.ini ([seccion] ps = " + std::string(nm) + ", nombre = ..., que = ..., grupo = ...)"; u.unknown = true; u.group = "desconocidos";
            s_classes.push_back(u);
        }
    }
    { int selNow = g_inspectSel.load(); if (selNow >= 0 && selNow != ci) return false; }   // con Capas abierta solo se captura la clase elegida
    if (s_group != "todo" && s_classes[ci].group != s_group) return false;   // solo el grupo elegido (cara, pelo, kit...)
    ClassFrame& f = s_cls[ci];
    {
        if (f.seen) { if (f.extra >= 8) return false; f.extra++; f.verts += verts; goto drawOnly; }   // 19/10: todas las partes de la clase (la cara viene en varios dibujos)
        f.seen = true; f.extra = 0;
        if (!EnsureClassRT(f)) return false;
        f.verts = verts; f.kind = kind; f.deferred = c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED; f.vs = vs ? vs->hash : 0;
        for (auto& t : f.tex) if (t.srv) t.srv->Release();
        f.tex.clear();
        ID3D11ShaderResourceView* srv[32] = {}; c->PSGetShaderResources(0, 32, srv);
        for (int i = 0; i < 32; i++) if (srv[i]) { std::string d = DescribeSRV(srv[i]); auto sn = s_classes[ci].slotNames.find(i); if (sn != s_classes[ci].slotNames.end()) d = sn->second + ": " + d;
            TexInfo ti; ti.slot = i; ti.desc = d; ti.srv = srv[i]; f.tex.push_back(ti); }   // la referencia queda en la ficha (se dibuja como miniatura)
    }
drawOnly:
    Saved& s = t_sv; s.cls = ci;
    c->PSGetShader(&s.ps, nullptr, nullptr);
    c->OMGetRenderTargets(8, s.rt, &s.ds);
    s.nvp = 8; c->RSGetViewports(&s.nvp, s.vp);
    c->OMGetBlendState(&s.bs, s.bf, &s.sm);
    c->OMGetDepthStencilState(&s.dss, &s.sref);
    if (s.nvp > 0) { s_screenW = s.vp[0].Width; s_screenH = s.vp[0].Height; }
    // nuestro estado: PS blanco, RT chico, sin profundidad (silueta completa aunque este tapado)
    const float clr[4] = { 0, 0, 0, 0 };
    c->PSSetShader(s_whitePS, nullptr, 0);
    ID3D11RenderTargetView* rts[1] = { f.rtv };
    c->OMSetRenderTargets(1, rts, nullptr);
    D3D11_VIEWPORT vp{ 0, 0, (float)kW, (float)kH, 0, 1 }; c->RSSetViewports(1, &vp);
    c->OMSetBlendState(s_noBlend, nullptr, 0xffffffff);
    c->OMSetDepthStencilState(s_noDepth, 0);
    (void)clr;
    return true;
}
void InspectEnd(ID3D11DeviceContext* c) {
    Saved& s = t_sv;
    c->PSSetShader(s.ps, nullptr, 0); if (s.ps) s.ps->Release();
    c->OMSetRenderTargets(8, s.rt, s.ds); for (auto* r : s.rt) if (r) r->Release(); if (s.ds) s.ds->Release();
    c->RSSetViewports(s.nvp, s.vp);
    c->OMSetBlendState(s.bs, s.bf, s.sm); if (s.bs) s.bs->Release();
    c->OMSetDepthStencilState(s.dss, s.sref); if (s.dss) s.dss->Release();
}

// en Present (contexto inmediato): leer la textura del cuadro anterior, calcular el rectangulo, limpiar para el proximo
void InspectPresent() {
    if (!g_dev.imm) return;
    // al apagar el inspector vuelven todas las capas ocultas (y la prueba de cejas); las texturas elegidas por slot se mantienen
    { static bool wasOn = false; bool on = g_inspectOn.load();
      if (wasOn && !on) { { std::lock_guard<std::mutex> lk(s_layMx); for (auto& kv : s_layers) kv.second.hidden = false; s_solo = -1; LayersUpdateAny(); }
          if (g_cfg.live[75][2] > 0.5f) g_cfg.live[75][2] = 0; g_inspectSel = -1; }
      wasOn = on; }
    s_skipsShown = g_layerSkips.exchange(0);
    s_frame++;
    if ((s_frame.load() % 3) != 1) return;   // leer/limpiar solo en el cuadro siguiente a una captura
    std::lock_guard<std::mutex> lk(s_mx);
    for (size_t i = 0; i < s_cls.size(); i++) {
        ClassFrame& f = s_cls[i];
        if (!f.rt) continue;
        if (!g_inspectOn) { f.seen = false; f.bboxOk = false; f.extra = 0; for (auto& t : f.tex) if (t.srv) t.srv->Release(); f.tex.clear(); continue; }
        // 1) leer la copia de hace un cuadro
        int rd = f.stIdx ^ 1;
        if (f.stValid[rd]) {
            D3D11_MAPPED_SUBRESOURCE m{};
            if (SUCCEEDED(g_dev.imm->Map(f.st[rd], 0, D3D11_MAP_READ, 0, &m))) {
                int minx = kW, miny = kH, maxx = -1, maxy = -1;
                for (int y = 0; y < kH; y++) { const uint8_t* row = (const uint8_t*)m.pData + (size_t)y * m.RowPitch;
                    for (int x = 0; x < kW; x++) if (row[x] > 8) { if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y; } }
                g_dev.imm->Unmap(f.st[rd], 0);
                if (maxx >= 0) { f.bboxOk = true; f.x0 = minx * s_screenW / kW; f.x1 = (maxx + 1) * s_screenW / kW; f.y0 = miny * s_screenH / kH; f.y1 = (maxy + 1) * s_screenH / kH; }
                else f.bboxOk = false;
            }
        }
        // 2) copiar lo de este cuadro y limpiar
        if (f.seen) { g_dev.imm->CopyResource(f.st[f.stIdx], f.rt); f.stValid[f.stIdx] = true; }
        else { f.stValid[f.stIdx] = false; f.bboxOk = false; }
        f.stIdx ^= 1;
        const float clr[4] = { 0, 0, 0, 0 }; g_dev.imm->ClearRenderTargetView(f.rtv, clr);
        f.seen = false;
    }
}

// capa de ImGui: rectangulos, flechas y fichas (se dibuja con el menu abierto o cerrado)
void InspectDrawOverlay() {
    if (!g_inspectOn) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    float sx = io.DisplaySize.x / (s_screenW > 1 ? s_screenW : io.DisplaySize.x), sy = io.DisplaySize.y / (s_screenH > 1 ? s_screenH : io.DisplaySize.y);
    std::lock_guard<std::mutex> lk(s_mx);
    // fichas en columna a la derecha
    float colX = io.DisplaySize.x - 430.0f, y = 70.0f;
    int sel = g_inspectSel.load();
    ImU32 cols[] = { IM_COL32(255, 80, 200, 255), IM_COL32(80, 220, 255, 255), IM_COL32(255, 200, 60, 255), IM_COL32(120, 255, 120, 255), IM_COL32(255, 120, 80, 255), IM_COL32(180, 140, 255, 255) };
    int ncol = 6, k = 0;
    // barra de grupos: Todo | Cara | Pelo | Kit | ... | Desconocidos (clic = filtrar rectangulos, fichas y capturas)
    { std::vector<std::string> groups; groups.push_back("todo");
      for (auto& c : s_classes) if (!c.unknown && std::find(groups.begin(), groups.end(), c.group) == groups.end()) groups.push_back(c.group);
      groups.push_back("desconocidos");
      float gx = colX, gy = 40.0f;
      dl->AddRectFilled(ImVec2(colX - 10, gy), ImVec2(io.DisplaySize.x - 10, gy + 24), IM_COL32(20, 22, 28, 220), 4.0f);
      for (auto& g : groups) {
          std::string lab = g; lab[0] = (char)toupper((unsigned char)lab[0]);
          ImVec2 ts = ImGui::CalcTextSize(lab.c_str()); ImVec2 ba(gx, gy + 3), bb(gx + ts.x + 12, gy + 21);
          bool on = s_group == g;
          dl->AddRectFilled(ba, bb, on ? IM_COL32(60, 120, 90, 255) : IM_COL32(45, 48, 58, 255), 3.0f);
          dl->AddText(ImVec2(ba.x + 6, ba.y + 1), IM_COL32(255, 255, 255, 255), lab.c_str());
          if (io.MouseClicked[0] && io.MousePos.x >= ba.x && io.MousePos.x <= bb.x && io.MousePos.y >= ba.y && io.MousePos.y <= bb.y) { s_group = g; g_inspectSel = -1; }
          gx = bb.x + 6;
      } }
    for (int i = 0; i < (int)s_cls.size() && i < (int)s_classes.size(); i++) {
        ClassFrame& f = s_cls[i];
        if (s_group != "todo" && s_classes[i].group != s_group) continue;
        if (!f.bboxOk) continue;
        ImU32 col = cols[(k++) % ncol];
        ImVec2 a(f.x0 * sx, f.y0 * sy), b(f.x1 * sx, f.y1 * sy);
        bool isSel = sel == i;
        if (sel >= 0 && !isSel) { k++; continue; }   // con la ventana Capas abierta solo queda el rectangulo de la clase elegida (sin flechas)
        dl->AddRect(a, b, col, 3.0f, 0, isSel ? 3.0f : 1.5f);
        ImVec2 center((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        // ficha (el detalle, con miniaturas y selectores, va en la ventana "Capas")
        float h = 26.0f;
        ImVec2 fa(colX, y), fb(io.DisplaySize.x - 10, y + h);
        dl->AddRectFilled(fa, fb, IM_COL32(20, 22, 28, 215), 4.0f);
        dl->AddRect(fa, fb, col, 4.0f, 0, isSel ? 2.0f : 1.0f);
        bool hid = false; { std::lock_guard<std::mutex> lk2(s_layMx); for (uint64_t hh : s_classes[i].ps) { auto it = s_layers.find(hh); if (it != s_layers.end() && it->second.hidden) hid = true; } int so = s_solo.load(); if (so >= 0 && so != i) hid = true; }
        char t[200]; snprintf(t, sizeof(t), "%s   %u vert %s%s", s_classes[i].name.c_str(), f.verts, f.deferred ? "(dif)" : "(inm)", hid ? "   (OCULTO)" : "");
        dl->AddText(ImVec2(fa.x + 8, fa.y + 5), col, t);
        // sin flechas (19/10): el nombre va pegado al rectangulo
        { char nm[96]; snprintf(nm, sizeof(nm), " %s%s ", s_classes[i].name.c_str(), hid ? " (oculto)" : ""); ImVec2 ts = ImGui::CalcTextSize(nm);
          ImVec2 la(a.x, a.y - ts.y - 2), lb(a.x + ts.x, a.y - 2); if (la.y < 0) { la.y = a.y + 2; lb.y = a.y + ts.y + 2; }
          dl->AddRectFilled(la, lb, IM_COL32(20, 22, 28, 200), 3.0f); dl->AddText(ImVec2(la.x, la.y), col, nm); }
        if (isSel) { if (io.MouseClicked[0] && io.MousePos.x >= fa.x && io.MousePos.x <= fb.x && io.MousePos.y >= fa.y && io.MousePos.y <= fb.y) g_inspectSel = -1; y += h + 6; continue; }
        // clic en la ficha
        if (io.MouseClicked[0] && io.MousePos.x >= fa.x && io.MousePos.x <= fb.x && io.MousePos.y >= fa.y && io.MousePos.y <= fb.y) g_inspectSel = isSel ? -1 : i;
        y += h + 6;
    }
    if (k == 0) dl->AddText(ImVec2(colX, 70), IM_COL32(255, 220, 120, 255), s_group == "todo" ? "Sin dibujos conocidos en pantalla (abri un partido / repeticion)." : "Nada de este grupo en pantalla.");
    // ---- ventana "Capas" de la clase elegida: miniatura + nombre + selector por slot, ocultar, guardar
    if (sel >= 0 && sel < (int)s_cls.size() && sel < (int)s_classes.size()) {
        ClassFrame& f = s_cls[sel]; InspClass& cl = s_classes[sel];
        ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 1010, 70), ImGuiCond_FirstUseEver);
        bool open = true;
        LayerOv ov; { std::lock_guard<std::mutex> lk(s_layMx); auto it = s_layers.find(cl.ps.empty() ? 0 : cl.ps[0]); if (it != s_layers.end()) ov = it->second; }
        if (ImGui::Begin(("Capas: " + cl.name + (ov.hidden ? " (OCULTO)" : "") + "##capas").c_str(), &open)) {
            ImGui::TextWrapped("%s", cl.desc.c_str());
            char hs[200] = ""; for (uint64_t h : cl.ps) { char t[24]; snprintf(t, sizeof(t), "%016llx ", (unsigned long long)h); strncat(hs, t, sizeof(hs) - strlen(hs) - 1); }
            ImGui::TextDisabled("shader(s): %s | %u vert, VS %016llx, %s", hs, f.verts, (unsigned long long)f.vs, f.deferred ? "diferido" : "inmediato");
            // --- vista debug ---
            ImGui::TextUnformatted("Vista:"); ImGui::SameLine();
            { int so = s_solo.load();
              if (ImGui::RadioButton("todo", so < 0)) { s_solo = -1; std::lock_guard<std::mutex> lk(s_layMx); LayersUpdateAny(); } ImGui::SameLine();
              if (ImGui::RadioButton("solo esta capa", so == sel)) { s_solo = sel; std::lock_guard<std::mutex> lk(s_layMx); LayersUpdateAny(); } ImGui::SameLine();
              bool browOnly = false; int pi = -1; for (int ci2 = 0; ci2 < (int)s_classes.size(); ci2++) for (uint64_t h : s_classes[ci2].ps) if (h == 0x0b3f9684ad026e54ULL) pi = ci2;
              if (pi >= 0) { std::lock_guard<std::mutex> lk(s_layMx); auto it = s_layers.find(0x0b3f9684ad026e54ULL); browOnly = it != s_layers.end() && it->second.hidden; }
              if (pi >= 0 && ImGui::Checkbox("cejas: solo hebras", &browOnly)) {
                  std::lock_guard<std::mutex> lk(s_layMx); for (uint64_t h : s_classes[pi].ps) s_layers[h].hidden = browOnly; LayersUpdateAny();
                  g_cfg.live[75][2] = browOnly ? 1.0f : 0.0f;   // hebras en magenta para verlas solas
              }
              if (ImGui::IsItemHovered()) ImGui::SetTooltip("Oculta las cejas/barba/pestanas pintadas del juego (capa alfa 0b3f/1fc2) y pinta las hebras de magenta."); }
            ImGui::TextDisabled("dibujos ocultados por cuadro: %d%s", s_skipsShown, s_skipsShown == 0 && (ov.hidden || s_solo.load() >= 0) ? "  (ninguno: esta capa no paso por el filtro)" : "");
            bool hidden = ov.hidden;
            if (ImGui::Checkbox("Ocultar esta capa (no dibujar esta parte)", &hidden)) { std::lock_guard<std::mutex> lk(s_layMx); for (uint64_t h : cl.ps) s_layers[h].hidden = hidden; LayersUpdateAny(); }
            ImGui::SameLine(); if (ImGui::Button("Guardar capas")) Log("capas: %s", SaveLayers() ? "guardadas en PES21FX_capas.ini" : "no se pudo guardar");
            ImGui::SameLine(); if (ImGui::Button("Releer archivos")) ScanFiles();
            ImGui::SameLine(); if (ImGui::Button("Quitar todas")) { std::lock_guard<std::mutex> lk(s_layMx); for (uint64_t h : cl.ps) s_layers.erase(h); LayersUpdateAny(); }
            bool isFace = false; for (uint64_t h : cl.ps) if (h == 0x5ce3ed8702271bc1ULL) isFace = true;
            if (isFace) {
                ImGui::Separator();
                ImGui::TextUnformatted("Prueba de cejas con hebras:");
                int dz = (int)(g_cfg.live[75][2] + 0.5f);
                struct Opt { int v; ImVec4 c; const char* t; };
                const Opt opts[] = { {0, ImVec4(0.5f,0.5f,0.5f,1), "normal"}, {1, ImVec4(1,0,1,1), "hebras en magenta"}, {2, ImVec4(1,0.3f,1,1), "semillas sobre la cara"}, {3, ImVec4(1,0.6f,0.2f,1), "grilla de prueba"} };
                for (auto& o : opts) { ImGui::PushID(o.v);
                    if (ImGui::ColorButton("##c", o.c, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(18, 18))) g_cfg.live[75][2] = (float)o.v;
                    ImGui::SameLine(); if (ImGui::Selectable(o.t, dz == o.v, 0, ImVec2(150, 0))) g_cfg.live[75][2] = (float)o.v;
                    ImGui::PopID(); if (o.v < 3) ImGui::SameLine(); }
                // leyenda de colores del modo "semillas"
                const Opt leg[] = { {0, ImVec4(1,0,1,1), "celda con semilla"}, {1, ImVec4(0.1f,0.2f,1,1), "zona de ceja sin semilla"}, {2, ImVec4(1,0,0,1), "t24 llega (rojo)"}, {3, ImVec4(0,1,0,1), "densidad del mapa (verde)"}, {4, ImVec4(0,0,0.9f,1), "alfa del mapa (azul)"} };
                for (auto& o : leg) { ImGui::PushID(100 + o.v); ImGui::ColorButton("##l", o.c, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(14, 14)); ImGui::SameLine(); ImGui::TextDisabled("%s", o.t); ImGui::PopID(); if (o.v < 4) ImGui::SameLine(); }
            }
            ImGui::Separator();
            ImGui::BeginChild("slots", ImVec2(0, 0));
            std::vector<int> slots; for (auto& t : f.tex) slots.push_back(t.slot); for (auto& kv : ov.slot) if (std::find(slots.begin(), slots.end(), kv.first) == slots.end()) slots.push_back(kv.first);
            std::sort(slots.begin(), slots.end());
            for (int slot : slots) {
                ImGui::PushID(slot);
                const TexInfo* ti = nullptr; for (auto& t : f.tex) if (t.slot == slot) ti = &t;
                if (ti && ti->srv) ImGui::Image((ImTextureID)(uintptr_t)ti->srv, ImVec2(56, 56)); else ImGui::Dummy(ImVec2(56, 56));
                ImGui::SameLine();
                ImGui::BeginGroup();
                auto sn = cl.slotNames.find(slot);
                ImGui::Text("t%d  %s", slot, sn != cl.slotNames.end() ? sn->second.c_str() : "");
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440); ImGui::TextDisabled("%s", ti ? ti->desc.c_str() : "(no enlazada por el juego)"); ImGui::PopTextWrapPos();
                auto cur = ov.slot.find(slot); std::string curV = cur != ov.slot.end() ? cur->second : "";
                const char* label = curV.empty() ? "Del juego / del mod (sin cambio)" : curV.c_str();
                // ojo: ocultar esta textura con un valor neutro (normal plana si el slot es una normal, gris si no)
                { bool off = !curV.empty(); std::string nm = sn != cl.slotNames.end() ? sn->second : "";
                  std::string neutral = (nm.find("normal") != std::string::npos) ? "gen:normal" : "gen:gris";
                  ImGui::PushStyleColor(ImGuiCol_Button, off ? ImVec4(0.65f, 0.25f, 0.2f, 1) : ImVec4(0.2f, 0.45f, 0.3f, 1));
                  if (ImGui::Button(off ? "oculta" : "visible", ImVec2(70, 0))) { std::lock_guard<std::mutex> lk(s_layMx); for (uint64_t h : cl.ps) { if (off) s_layers[h].slot.erase(slot); else s_layers[h].slot[slot] = neutral; } LayersUpdateAny(); }
                  ImGui::PopStyleColor(); ImGui::SameLine(); }
                ImGui::SetNextItemWidth(360);
                if (ImGui::BeginCombo("##capa", label)) {
                    if (ImGui::Selectable("Del juego / del mod (sin cambio)", curV.empty())) { std::lock_guard<std::mutex> lk(s_layMx); for (uint64_t h : cl.ps) s_layers[h].slot.erase(slot); LayersUpdateAny(); }
                    if (ImGui::Selectable("Ninguna (negra)", curV == "none")) { std::lock_guard<std::mutex> lk(s_layMx); for (uint64_t h : cl.ps) s_layers[h].slot[slot] = "none"; LayersUpdateAny(); }
                    const char* gens[] = { "gen:gris", "gen:blanco", "gen:negro", "gen:normal" };
                    const char* gensL[] = { "Neutra: gris 50%", "Neutra: blanca", "Neutra: negra (alfa 1)", "Neutra: normal plana" };
                    for (int g = 0; g < 4; g++) if (ImGui::Selectable(gensL[g], curV == gens[g])) { std::lock_guard<std::mutex> lk(s_layMx); for (uint64_t h : cl.ps) s_layers[h].slot[slot] = gens[g]; LayersUpdateAny(); }
                    for (auto& fn : s_files) if (ImGui::Selectable(fn.c_str(), curV == fn)) { std::lock_guard<std::mutex> lk(s_layMx); for (uint64_t h : cl.ps) s_layers[h].slot[slot] = fn; LayersUpdateAny(); }
                    ImGui::EndCombo();
                }
                ImGui::EndGroup();
                ImGui::Separator();
                ImGui::PopID();
            }
            static int newSlot = 26;
            ImGui::SetNextItemWidth(80); ImGui::InputInt("##ns", &newSlot); ImGui::SameLine();
            if (ImGui::Button("Agregar capa en ese slot")) { std::lock_guard<std::mutex> lk(s_layMx); for (uint64_t h : cl.ps) if (!s_layers[h].slot.count(newSlot)) s_layers[h].slot[newSlot] = "none"; LayersUpdateAny(); }
            ImGui::EndChild();
        }
        ImGui::End();
        if (!open) g_inspectSel = -1;
    }
}

} // namespace fx
