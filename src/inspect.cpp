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
#include <set>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include "imgui.h"
#include <ScreenGrab.h>
#include <wincodec.h>

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
struct InspClass { std::vector<uint64_t> ps; std::string name, desc; std::map<int, std::string> slotNames; bool unknown = false; std::string group = "otros"; uint64_t alias = 0; std::vector<uint64_t> vsAny; UINT minVerts = 0; bool autoDump = false; };   // autoDump: "volcar = 1": sus texturas se vuelcan solas (sin abrir el inspector)   // minVerts: el comodin de VS solo vale para dibujos con al menos tantos vertices   // vsAny: "vs = <hash>": cualquier PS sin registrar con ese vertex shader es de esta clase (comodin)
static std::mutex s_autoMx; static std::vector<std::pair<uint64_t, std::string>> s_autoDump;   // volcado automatico (clases con "volcar = 1")
std::atomic<bool> g_autoDumpAny{false};
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
// parametros del mod (xN..wN) que lee un shader: se buscan "IniParams.Load(int2(N" en shaders\<hash>-ps_replace.txt (una vez)
static std::map<uint64_t, std::vector<int>> s_paramsUsed;
static const std::vector<int>& ParamsUsedBy(uint64_t h) {
    auto it = s_paramsUsed.find(h); if (it != s_paramsUsed.end()) return it->second;
    std::vector<int>& v = s_paramsUsed[h];
    wchar_t nm[64]; swprintf(nm, 64, L"shaders\\%016llx-ps_replace.txt", (unsigned long long)h);
    std::string txt; FILE* fp = _wfopen((g_cfg.baseDir + nm).c_str(), L"rb");
    if (fp) { char buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) txt.append(buf, n); fclose(fp); }
    size_t pos = 0; const std::string key = "IniParams.Load(int2(";
    while ((pos = txt.find(key, pos)) != std::string::npos) { pos += key.size(); int n = atoi(txt.c_str() + pos); if (n >= 0 && n < kParamCount && std::find(v.begin(), v.end(), n) == v.end()) v.push_back(n); }
    std::sort(v.begin(), v.end());
    return v;
}
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
            else if (k == "como") cur->alias = strtoull(v.c_str(), nullptr, 16);
            else if (k == "minverts") cur->minVerts = (UINT)atoi(v.c_str());
            else if (k == "volcar") cur->autoDump = atoi(v.c_str()) != 0;
            else if (k == "vs") { std::string h; for (char ch : v + ",") { if (ch == ',' || ch == ' ') { h = Trim2(h); if (!h.empty()) cur->vsAny.push_back(strtoull(h.c_str(), nullptr, 16)); h.clear(); } else h += ch; } }
            else if (k.size() > 1 && k[0] == 't' && isdigit((unsigned char)k[1])) cur->slotNames[atoi(k.c_str() + 1)] = v;
        }
        Log("inspector: PES21FX_inspector.ini leido (%d clases)", (int)s_classes.size());
    }
    if (s_classes.empty()) for (auto& d : kDefaults) s_classes.push_back(d);
    { std::lock_guard<std::mutex> lk(s_autoMx); s_autoDump.clear();
      for (auto& cl : s_classes) if (cl.autoDump) for (uint64_t h : cl.ps) s_autoDump.push_back({ h, cl.name });
      g_autoDumpAny = !s_autoDump.empty(); if (g_autoDumpAny) Log("inspector: volcado automatico de texturas para %d shaders", (int)s_autoDump.size()); }
}
static const int kW = 192, kH = 108;   // 19/10: mas chico (alcanza para el rectangulo)
static std::atomic<int> s_frame{0};
static float s_pickX = -1, s_pickY = -1;   // selector por pixel (boton derecho): coordenadas de pantalla; -1 = sin seleccion
static bool s_sceneOpen = false;            // ventana "Escena" (luz, sol, camara, niebla)   // se captura 1 de cada 3 cuadros (el rectangulo no necesita mas)

struct TexInfo { int slot; std::string desc; ID3D11ShaderResourceView* srv = nullptr; };   // srv con referencia propia (miniatura en la ficha)
struct ClassFrame {
    bool seen = false; bool bboxOk = false; int extra = 0; float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    UINT verts = 0; int kind = 0; bool deferred = false; uint64_t vs = 0;
    std::vector<TexInfo> tex;
    std::vector<TexInfo> vtex;                 // texturas del vertex shader
    int order = 0, nrt = 0, draws = 0, drawsShown = 0; char rtDesc[96] = ""; bool depthOn = false; bool underPick = false;
    ID3D11Buffer* matCopy = nullptr; ID3D11Buffer* matStg = nullptr; float mat[32] = {}; bool matOk = false;   // b4 m_materials[8]
    ID3D11Buffer* objCopy = nullptr; ID3D11Buffer* objStg = nullptr; float obj[16] = {}; bool objOk = false;   // b5 m_world (4x4) + m_useWeightCount
    // recursos
    ID3D11Texture2D* rt = nullptr; ID3D11RenderTargetView* rtv = nullptr; ID3D11Texture2D* st[2] = {}; int stIdx = 0; bool stValid[2] = {};
};
static std::vector<ClassFrame> s_cls;   // una por clase (misma posicion que s_classes), mas las desconocidas al final
static std::vector<uint64_t> s_unkHash;   // hash de cada clase desconocida (indices >= s_classes.size())
static const int kMaxUnknown = 24;
static bool s_loaded = false;
static ID3D11PixelShader* s_whitePS = nullptr; static bool s_psTried = false;
static std::mutex s_mx;
// ---- capas editables (PES21FX_capas.ini): [<hash>]  tN = file:ruta | none   oculta = 1
std::atomic<bool> g_layersAny{false};
static std::map<uint64_t, LayerOv> s_layers; static std::mutex s_layMx;
static std::vector<std::string> s_files;   // "file:textures\\x.dds", "file:hair\\x.png", ... (para el selector)
std::atomic<int> g_layerSkips{0}; static int s_skipsShown = 0; static int s_aliasShown = 0;
static std::atomic<int> s_solo{-1};   // clase "solo esta" (las demas clases conocidas no se dibujan)
static std::atomic<bool> s_soloAll{false};   // "solo esta capa": tambien oculta todo lo que no tiene clase (geometria; las pasadas de pantalla completa siguen)
bool InspectShouldSkip(uint64_t ps, UINT verts) {
    if (!g_inspectOn) return false;   // ocultar y "solo esta" son de debug: solo con el inspector prendido
    std::lock_guard<std::mutex> lk(s_layMx);
    auto it = s_layers.find(ps); if (it != s_layers.end() && it->second.hidden) return true;
    int so = s_solo.load();
    if (so >= 0 && so < (int)s_classes.size()) {
        for (uint64_t h : s_classes[so].ps) if (h == ps) return false;
        for (auto& c : s_classes) for (uint64_t h : c.ps) if (h == ps) return true;   // las clases conocidas
        if (s_soloAll.load() && verts > 6) return true;                                 // y todo lo demas que sea geometria (no las pasadas de 3 vertices)
    }
    return false;
}
ID3D11PixelShader* InspectHighlightPS(uint64_t ps) {
    if (!g_inspectOn || !s_whitePS) return nullptr;
    std::lock_guard<std::mutex> lk(s_layMx);
    auto it = s_layers.find(ps); return (it != s_layers.end() && it->second.highlight) ? s_whitePS : nullptr;
}
bool InspectLayerFor(uint64_t ps, LayerOv& out) {
    std::lock_guard<std::mutex> lk(s_layMx); auto it = s_layers.find(ps); if (it == s_layers.end()) return false; out = it->second; return true;
}
static void LayersUpdateAny() { bool any = s_solo.load() >= 0; for (auto& kv : s_layers) if (kv.second.hidden || kv.second.highlight || !kv.second.slot.empty()) any = true; g_layersAny = any; }
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
// "Guardar informe": todo lo recopilado de una clase (shaders, texturas, material, regla, parametros, capas) + luz de la escena
static std::string FmtCB(const std::vector<float>& v, int from, int n) { std::string o; char b[64]; for (int i = 0; i < n && from + i < (int)v.size(); i++) { snprintf(b, sizeof(b), "%s%.4f", i ? " " : "", v[from + i]); o += b; } return o; }
static bool SaveReport(int ci, std::string& outPath);
void InspectReload() { std::lock_guard<std::mutex> lk(s_mx); s_loaded = false; }
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

// ---- historial de texturas (19/10): cada textura distinta que pasa por una clase inspeccionada se anota una vez
// (hash de recurso, tamano, formato, clase y slot) en PES21FX\inspector\historial.txt y, si "Volcar texturas" esta
// prendido, se guarda como png en PES21FX\inspector\texturas\<clase>\t<slot>_<id>.png (solo una vez por textura).
static std::atomic<bool> s_dumpOn{false};
static std::set<uint64_t> s_seenTex, s_dumpedTex; static std::mutex s_histMx;

static std::vector<std::pair<ID3D11Resource*, std::wstring>> s_dumpQueue;   // (recurso con AddRef, ruta) -> se vuelca en Present
static int s_dumpCount = 0, s_histCount = 0;
static void HistAdd(ID3D11ShaderResourceView* v, const std::string& cls, int slot, const std::string& desc, bool force = false) {
    ID3D11Resource* res = nullptr; v->GetResource(&res); if (!res) return;
    uint64_t key = (uint64_t)(uintptr_t)res;
    bool isNew; { std::lock_guard<std::mutex> lk(s_histMx); isNew = s_seenTex.insert(key).second; }
    bool dump = false; if (s_dumpOn || force) { std::lock_guard<std::mutex> lk(s_histMx); dump = s_dumpedTex.insert(key).second; }
    if (!isNew && !dump) { res->Release(); return; }
    std::wstring dir = g_cfg.baseDir + L"inspector"; CreateDirectoryW(dir.c_str(), nullptr);
    if (isNew) { FILE* f = _wfopen((dir + L"\\historial.txt").c_str(), L"ab");
      if (f) { SYSTEMTIME st; GetLocalTime(&st); char b[512]; snprintf(b, sizeof(b), "%02d:%02d:%02d | %s | t%d | %s\n", st.wHour, st.wMinute, st.wSecond, cls.c_str(), slot, desc.c_str()); fwrite(b, 1, strlen(b), f); fclose(f); s_histCount++; } }
    if (dump) {
        D3D11_RESOURCE_DIMENSION dm; res->GetType(&dm);
        if (dm == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
            std::string safe; for (char ch : cls) safe += (isalnum((unsigned char)ch) ? ch : '_');
            std::wstring sub = dir + L"\\texturas"; CreateDirectoryW(sub.c_str(), nullptr); sub += L"\\" + std::wstring(safe.begin(), safe.end()); CreateDirectoryW(sub.c_str(), nullptr);
            wchar_t nm[96]; swprintf(nm, 96, L"\\t%02d_%06llx.png", slot, (unsigned long long)((key >> 4) & 0xffffff));
            res->AddRef(); std::lock_guard<std::mutex> lk(s_histMx); s_dumpQueue.push_back({ res, sub + nm });
        }
    }
    res->Release();
}
static void HistFlush() {   // Present: volcar hasta 2 texturas por cuadro (SaveWIC hace una copia a staging y espera a la GPU)
    for (int n = 0; n < 2; n++) {
        std::pair<ID3D11Resource*, std::wstring> it; { std::lock_guard<std::mutex> lk(s_histMx); if (s_dumpQueue.empty()) return; it = s_dumpQueue.back(); s_dumpQueue.pop_back(); }
        EnsureComInit();
        HRESULT hr = DirectX::SaveWICTextureToFile(g_dev.imm, it.first, GUID_ContainerFormatPng, it.second.c_str(), nullptr, nullptr, false);
        if (FAILED(hr)) { // formatos comprimidos o de profundidad: WIC no los convierte; se guardan como dds
            std::wstring dds = it.second.substr(0, it.second.size() - 4) + L".dds"; hr = DirectX::SaveDDSTextureToFile(g_dev.imm, it.first, dds.c_str()); }
        if (SUCCEEDED(hr)) s_dumpCount++;
        it.first->Release();
    }
}
static std::string DescribeSRV(ID3D11ShaderResourceView* v);
// volcado automatico (19/10): las clases con "volcar = 1" en PES21FX_inspector.ini vuelcan sus texturas (t0..t15 del PS) la primera
// vez que se ven, sin abrir el inspector. Se mira una vez por segundo por shader.
void InspectAutoDump(ID3D11DeviceContext* c, const ShaderEntry* ps) {
    if (!ps || !g_dev.dev) return;
    std::string cls;
    { std::lock_guard<std::mutex> lk(s_autoMx); for (auto& e : s_autoDump) if (e.first == ps->hash) { cls = e.second; break; } }
    if (cls.empty()) return;
    static std::map<uint64_t, double> next; double t = Now();
    { std::lock_guard<std::mutex> lk(s_autoMx); double& nx = next[ps->hash]; if (t < nx) return; nx = t + 1.0; }
    ID3D11ShaderResourceView* srv[16] = {}; c->PSGetShaderResources(0, 16, srv);
    for (int i = 0; i < 16; i++) if (srv[i]) { HistAdd(srv[i], cls, i, DescribeSRV(srv[i]), true); srv[i]->Release(); }
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
// huella estable de un PS sin registrar: VS + formato y tipo (array) de cada textura t0..t7 (sin tamanos: las caras vienen en 1024 y 2048)
uint64_t InspectFingerprint(ID3D11DeviceContext* c, const ShaderEntry* vs) {
    uint64_t h = 1469598103934665603ULL; auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ULL; };
    mix(vs ? vs->hash : 0);
    ID3D11ShaderResourceView* sv[8] = {}; c->PSGetShaderResources(0, 8, sv);
    for (int i = 0; i < 8; i++) { if (!sv[i]) { mix(0); continue; } ID3D11Resource* r = nullptr; sv[i]->GetResource(&r);
        if (r) { D3D11_RESOURCE_DIMENSION dm; r->GetType(&dm); if (dm == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { D3D11_TEXTURE2D_DESC td; ((ID3D11Texture2D*)r)->GetDesc(&td); mix(((uint64_t)td.Format << 8) | (td.ArraySize > 1 ? 1 : 0)); } else mix(7); r->Release(); }
        sv[i]->Release(); }
    return 0xF000000000000000ULL | (h & 0x0FFFFFFFFFFFFFFFULL);
}
// alias: "como = <hash>" en el ini -> el dibujo se trata como ese shader del juego (reemplazo + reglas). 0 = sin alias
// clase de un PS sin registrar: por huella exacta o por comodin de VS ("vs = ..."); en ese caso la huella se agrega a la clase
static int ClassForUnreg(uint64_t fingerprint, uint64_t vsHash, UINT verts) {
    for (size_t i = 0; i < s_classes.size(); i++) for (uint64_t h : s_classes[i].ps) if (h == fingerprint) return (int)i;
    if (vsHash) for (size_t i = 0; i < s_classes.size(); i++) if (!s_classes[i].unknown && verts >= s_classes[i].minVerts) for (uint64_t h : s_classes[i].vsAny) if (h == vsHash) {
        s_classes[i].ps.push_back(fingerprint);
        Log("inspector: PS sin registrar huella %016llx (VS %016llx, %u vertices) -> clase '%s' por comodin de VS", (unsigned long long)fingerprint, (unsigned long long)vsHash, verts, s_classes[i].name.c_str());
        return (int)i; }
    return -1;
}
uint64_t InspectAliasFor(uint64_t fingerprint, uint64_t vsHash, UINT verts) {
    std::lock_guard<std::mutex> lk(s_mx);
    if (!s_loaded) { LoadClasses(); LoadLayers(); ScanFiles(); s_loaded = true; s_cls.clear(); s_unkHash.clear(); s_cls.resize(s_classes.size()); }
    int ci = ClassForUnreg(fingerprint, vsHash, verts);
    if (ci < 0 || !s_classes[ci].alias) return 0;
    // si la clase declara "vs = ...", el alias solo vale para dibujos con esos VS (las caras genericas de Konami comparten clase pero usan otro VS)
    const auto& va = s_classes[ci].vsAny;
    if (!va.empty() && std::find(va.begin(), va.end(), vsHash) == va.end()) return 0;
    return s_classes[ci].alias;
}
bool InspectBegin(ID3D11DeviceContext* c, const ShaderEntry* ps, const ShaderEntry* vs, int kind, UINT verts, UINT inst) {
    if (!g_dev.dev) return false;
    if ((s_frame.load() % 3) != 0) return false;
    EnsureRes(); if (!s_whitePS) return false;
    // PS no registrado por el mod (creado antes del enganche u otro camino): se identifica por puntero
    uint64_t psHash = ps ? ps->hash : 0; bool unreg = false;
    if (!ps) {
        // PS sin registrar (lo creo otra DLL por fuera de nuestro enganche): huella ESTABLE entre sesiones = VS + formato/tamano de
        // sus texturas (t0..t7). Asi se puede nombrar en PES21FX_inspector.ini y ocultar/resaltar como cualquier clase.
        ID3D11PixelShader* raw = nullptr; c->PSGetShader(&raw, nullptr, nullptr); if (!raw) return false; raw->Release();
        psHash = InspectFingerprint(c, vs); unreg = true;
    }
    int ci = -1;
    std::lock_guard<std::mutex> lk(s_mx);
    if (!s_loaded) { LoadClasses(); LoadLayers(); ScanFiles(); s_loaded = true; for (auto& f : s_cls) { if (f.rt) f.rt->Release(); if (f.rtv) f.rtv->Release(); for (auto* t : f.st) if (t) t->Release(); for (auto& t : f.tex) if (t.srv) t.srv->Release(); for (auto& t : f.vtex) if (t.srv) t.srv->Release(); if (f.matCopy) f.matCopy->Release(); if (f.matStg) f.matStg->Release(); if (f.objCopy) f.objCopy->Release(); if (f.objStg) f.objStg->Release(); } s_cls.clear(); s_unkHash.clear(); s_cls.resize(s_classes.size()); }
    if (unreg) ci = ClassForUnreg(psHash, vs ? vs->hash : 0, verts);
    else for (size_t i = 0; i < s_classes.size() && ci < 0; i++) for (uint64_t h : s_classes[i].ps) if (h == psHash) { ci = (int)i; break; }
    if (ci < 0) {
        // desconocido: geometria (>= 100 vertices) en un dibujo con render target de pantalla (G-buffer), hasta kMaxUnknown
        if (!s_unknownOn || verts < 100) return false;
        for (size_t i = 0; i < s_unkHash.size(); i++) if (s_unkHash[i] == psHash) { ci = (int)(s_classes.size() + i); break; }
        if (ci < 0) {
            if ((int)s_unkHash.size() >= kMaxUnknown) return false;
            ID3D11RenderTargetView* rts[8] = {}; c->OMGetRenderTargets(8, rts, nullptr); int nrt = 0; for (auto* r : rts) if (r) { nrt++; r->Release(); }
            if (nrt < 2) return false;   // (la pasada de sombras tiene 0-1 targets)
            s_unkHash.push_back(psHash); s_cls.push_back(ClassFrame()); ci = (int)s_cls.size() - 1;
            char nm[64]; snprintf(nm, sizeof(nm), "%016llx", (unsigned long long)psHash);
            InspClass u; u.ps.push_back(psHash);
            if (unreg) { u.name = std::string("SIN REGISTRAR ") + (nm + 4); u.desc = "Pixel shader creado por otra DLL (cloth-mod / 3DMigoto), fuera de nuestro enganche. Huella estable " + std::string(nm) + " (VS + formatos de texturas). En PES21FX_inspector.ini: ps = " + std::string(nm) + " y 'como = <hash del juego>' para que reciba el reemplazo del mod (p.ej. como = 5ce3ed8702271bc1).";
                Log("inspector: PS sin registrar, huella %s (VS %016llx, %u vertices)", nm, (unsigned long long)(vs ? vs->hash : 0), verts); }
            else { u.name = std::string("Desconocido ") + nm; u.desc = "Shader sin clase: agregalo a PES21FX_inspector.ini ([seccion] ps = " + std::string(nm) + ", nombre = ..., que = ..., grupo = ...)"; }
            u.unknown = true; u.group = "desconocidos";
            s_classes.push_back(u);
        }
    }
    { int selNow = g_inspectSel.load(); if (selNow >= 0 && selNow != ci) return false; }   // con Capas abierta solo se captura la clase elegida
    if (s_group != "todo" && s_classes[ci].group != s_group) return false;   // solo el grupo elegido (cara, pelo, kit...)
    ClassFrame& f = s_cls[ci];
    {
        f.draws++;
        if (f.seen) { if (f.extra >= 8) return false; f.extra++; f.verts += verts; goto drawOnly; }   // 19/10: todas las partes de la clase (la cara viene en varios dibujos)
        f.seen = true; f.extra = 0;
        if (!EnsureClassRT(f)) return false;
        f.verts = verts; f.kind = kind; f.deferred = c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED; f.vs = vs ? vs->hash : 0;
        for (auto& t : f.tex) if (t.srv) t.srv->Release();
        f.tex.clear();
        ID3D11ShaderResourceView* srv[32] = {}; c->PSGetShaderResources(0, 32, srv);
        for (int i = 0; i < 32; i++) if (srv[i]) { std::string d = DescribeSRV(srv[i]); auto sn = s_classes[ci].slotNames.find(i); if (sn != s_classes[ci].slotNames.end()) d = sn->second + ": " + d;
            TexInfo ti; ti.slot = i; ti.desc = d; ti.srv = srv[i]; f.tex.push_back(ti);
            HistAdd(srv[i], s_classes[ci].name, i, d); }   // la referencia queda en la ficha (se dibuja como miniatura)
        // vertex shader: texturas (t0..t15)
        for (auto& t : f.vtex) if (t.srv) t.srv->Release(); f.vtex.clear();
        { ID3D11ShaderResourceView* vsrv[16] = {}; c->VSGetShaderResources(0, 16, vsrv);
          for (int i = 0; i < 16; i++) if (vsrv[i]) { TexInfo ti; ti.slot = i; ti.desc = DescribeSRV(vsrv[i]); ti.srv = vsrv[i]; f.vtex.push_back(ti); } }
        // orden, render targets, profundidad
        f.order = g_drawOrder.load();
        { ID3D11RenderTargetView* rts[8] = {}; ID3D11DepthStencilView* dsv = nullptr; c->OMGetRenderTargets(8, rts, &dsv);
          f.nrt = 0; f.rtDesc[0] = 0; for (int i = 0; i < 8; i++) if (rts[i]) { f.nrt = i + 1; if (i == 0) { ID3D11Resource* r = nullptr; rts[i]->GetResource(&r); if (r) { D3D11_RESOURCE_DIMENSION dm; r->GetType(&dm); if (dm == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { D3D11_TEXTURE2D_DESC td; ((ID3D11Texture2D*)r)->GetDesc(&td); snprintf(f.rtDesc, sizeof(f.rtDesc), "%ux%u fmt %d", td.Width, td.Height, (int)td.Format); } r->Release(); } } rts[i]->Release(); }
          f.depthOn = dsv != nullptr; if (dsv) dsv->Release(); }
        // cbuffers: material (b4) y objeto (b5) -> copia DEFAULT (lectura en Present)
        { ID3D11Buffer* cb4 = nullptr; c->PSGetConstantBuffers(4, 1, &cb4);
          if (cb4) { if (!f.matCopy) { D3D11_BUFFER_DESC d{}; d.ByteWidth = 128; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; g_dev.dev->CreateBuffer(&d, nullptr, &f.matCopy); d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &f.matStg); }
            D3D11_BUFFER_DESC sd; cb4->GetDesc(&sd); if (f.matCopy && sd.ByteWidth >= 128) { D3D11_BOX bx{ 0, 0, 0, 128, 1, 1 }; c->CopySubresourceRegion(f.matCopy, 0, 0, 0, 0, cb4, 0, &bx); }
            cb4->Release(); }
          ID3D11Buffer* cb5 = nullptr; c->VSGetConstantBuffers(5, 1, &cb5);
          if (cb5) { if (!f.objCopy) { D3D11_BUFFER_DESC d{}; d.ByteWidth = 144; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; g_dev.dev->CreateBuffer(&d, nullptr, &f.objCopy); d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &f.objStg); }
            D3D11_BUFFER_DESC sd; cb5->GetDesc(&sd); if (f.objCopy && sd.ByteWidth >= 144) { D3D11_BOX bx{ 64, 0, 0, 64 + 80, 1, 1 }; c->CopySubresourceRegion(f.objCopy, 0, 0, 0, 0, cb5, 0, &bx); }   // m_world (offset 64) + m_useWeightCount
            cb5->Release(); }
        }
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
    HistFlush();
    // al apagar el inspector vuelven todas las capas ocultas (y la prueba de cejas); las texturas elegidas por slot se mantienen
    { static bool wasOn = false; bool on = g_inspectOn.load();
      if (wasOn && !on) { { std::lock_guard<std::mutex> lk(s_layMx); for (auto& kv : s_layers) { kv.second.hidden = false; kv.second.highlight = false; } s_solo = -1; s_soloAll = false; LayersUpdateAny(); }
          if (g_cfg.live[75][2] > 0.5f) g_cfg.live[75][2] = 0; g_inspectSel = -1; }
      wasOn = on; }
    s_skipsShown = g_layerSkips.exchange(0); s_aliasShown = g_aliasDraws.exchange(0);
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
                f.underPick = false;
                if (s_pickX >= 0) { int px = (int)(s_pickX * kW / (s_screenW > 1 ? s_screenW : 1920.f)), py = (int)(s_pickY * kH / (s_screenH > 1 ? s_screenH : 1080.f));
                    if (px >= 0 && px < kW && py >= 0 && py < kH) { const uint8_t* row = (const uint8_t*)m.pData + (size_t)py * m.RowPitch; f.underPick = row[px] > 8; } }
                for (int y = 0; y < kH; y++) { const uint8_t* row = (const uint8_t*)m.pData + (size_t)y * m.RowPitch;
                    for (int x = 0; x < kW; x++) if (row[x] > 8) { if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y; } }
                g_dev.imm->Unmap(f.st[rd], 0);
                if (maxx >= 0) { f.bboxOk = true; f.x0 = minx * s_screenW / kW; f.x1 = (maxx + 1) * s_screenW / kW; f.y0 = miny * s_screenH / kH; f.y1 = (maxy + 1) * s_screenH / kH; }
                else f.bboxOk = false;
            }
        }
        // 1b) cbuffers: leer la copia del cuadro anterior y copiar la de este
        if (f.matStg && f.matCopy) { D3D11_MAPPED_SUBRESOURCE m{}; if (SUCCEEDED(g_dev.imm->Map(f.matStg, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) { memcpy(f.mat, m.pData, 128); f.matOk = true; g_dev.imm->Unmap(f.matStg, 0); } if (f.seen) g_dev.imm->CopyResource(f.matStg, f.matCopy); }
        if (f.objStg && f.objCopy) { D3D11_MAPPED_SUBRESOURCE m{}; if (SUCCEEDED(g_dev.imm->Map(f.objStg, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) { memcpy(f.obj, m.pData, 64); float* uw = (float*)m.pData + 16; f.obj[15] = f.obj[15]; (void)uw; f.objOk = true; g_dev.imm->Unmap(f.objStg, 0); } if (f.seen) g_dev.imm->CopyResource(f.objStg, f.objCopy); }
        f.drawsShown = f.draws; f.draws = 0;
        // 2) copiar lo de este cuadro y limpiar
        if (f.seen) { g_dev.imm->CopyResource(f.st[f.stIdx], f.rt); f.stValid[f.stIdx] = true; }
        else { f.stValid[f.stIdx] = false; f.bboxOk = false; }
        f.stIdx ^= 1;
        const float clr[4] = { 0, 0, 0, 0 }; g_dev.imm->ClearRenderTargetView(f.rtv, clr);
        f.seen = false;
    }
}

static bool SaveReport(int ci, std::string& outPath) {
    if (ci < 0 || ci >= (int)s_cls.size() || ci >= (int)s_classes.size()) return false;
    ClassFrame& f = s_cls[ci]; InspClass& cl = s_classes[ci];
    std::wstring dir = g_cfg.baseDir + L"inspector"; CreateDirectoryW(dir.c_str(), nullptr);
    char hn[32]; snprintf(hn, sizeof(hn), "%016llx", (unsigned long long)(cl.ps.empty() ? 0 : cl.ps[0]));
    std::string safe; for (char ch : cl.name) safe += (isalnum((unsigned char)ch) ? ch : '_');
    std::string fn = safe + "_" + hn + ".txt"; outPath = "PES21FX\\inspector\\" + fn;
    std::string r; char b[512];
    SYSTEMTIME st; GetLocalTime(&st); snprintf(b, sizeof(b), "PES21 FX - informe del inspector  %04d-%02d-%02d %02d:%02d\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute); r += b;
    r += "clase: " + cl.name + "  (grupo " + cl.group + ")\n" + "que hace el mod: " + cl.desc + "\n";
    r += "pixel shaders: "; for (uint64_t h : cl.ps) { snprintf(b, sizeof(b), "%016llx ", (unsigned long long)h); r += b; } r += "\n";
    snprintf(b, sizeof(b), "vertex shader: %016llx | vertices: %u | dibujos por cuadro: %d | orden: %d | render targets: %d (%s)%s | contexto: %s\n", (unsigned long long)f.vs, f.verts, f.drawsShown, f.order, f.nrt, f.rtDesc, f.depthOn ? " + profundidad" : "", f.deferred ? "diferido" : "inmediato"); r += b;
    r += "\n[texturas del pixel shader]\n";
    for (auto& t : f.tex) { auto sn = cl.slotNames.find(t.slot); snprintf(b, sizeof(b), "t%-2d %s%s%s\n", t.slot, sn != cl.slotNames.end() ? sn->second.c_str() : "", sn != cl.slotNames.end() ? ": " : "", t.desc.c_str()); r += b; }
    r += "\n[texturas del vertex shader]\n"; for (auto& t : f.vtex) { snprintf(b, sizeof(b), "vs t%-2d %s\n", t.slot, t.desc.c_str()); r += b; }
    r += "\n[material cPSMaterial b4: m_materials[8]]\n";
    if (f.matOk) for (int i = 0; i < 8; i++) { snprintf(b, sizeof(b), "m_materials[%d] = %.4f %.4f %.4f %.4f\n", i, f.mat[i * 4], f.mat[i * 4 + 1], f.mat[i * 4 + 2], f.mat[i * 4 + 3]); r += b; } else r += "(sin b4)\n";
    if (f.objOk) { snprintf(b, sizeof(b), "\nposicion del objeto (m_world): %.3f %.3f %.3f\n", f.obj[3], f.obj[7], f.obj[11]); r += b; }
    for (uint64_t h : cl.ps) {
        const Rule* ru = RuleForHash(h); wchar_t nm[64]; swprintf(nm, 64, L"shaders\\%016llx-ps_replace.txt", (unsigned long long)h);
        bool repl = GetFileAttributesW((g_cfg.baseDir + nm).c_str()) != INVALID_FILE_ATTRIBUTES;
        snprintf(b, sizeof(b), "\n[shader %016llx] %s\n", (unsigned long long)h, repl ? "reemplazado por el mod" : "del juego"); r += b;
        if (ru) for (auto& a : ru->actions) { const char* k = a.kind == RuleAction::CopyCB ? "copy_cb" : a.kind == RuleAction::RefSRV ? "ref_srv" : a.kind == RuleAction::BindCB ? "cb" : a.kind == RuleAction::BindSRV ? "srv" : a.kind == RuleAction::SetParam ? "param" : a.kind == RuleAction::EventTurf ? "event" : "facegen";
            snprintf(b, sizeof(b), "regla: %s %d %s %d\n", k, a.slot, a.name.c_str(), a.idx); r += b; }
        if (repl) for (int n : ParamsUsedBy(h)) { snprintf(b, sizeof(b), "param %d: %.4f %.4f %.4f %.4f\n", n, g_cfg.live[n][0], g_cfg.live[n][1], g_cfg.live[n][2], g_cfg.live[n][3]); r += b; }
        std::lock_guard<std::mutex> lk(s_layMx); auto it = s_layers.find(h);
        if (it != s_layers.end()) { if (it->second.hidden) r += "capa: OCULTA\n"; if (it->second.highlight) r += "capa: resaltada\n"; for (auto& sl : it->second.slot) { snprintf(b, sizeof(b), "capa t%d = %s\n", sl.first, sl.second.c_str()); r += b; } }
    }
    std::vector<float> L, S;
    if (NamedCBRead("LightCB", L) && L.size() >= 44) { r += "\n[luz de la escena cPSLight b3]\nm_lightDir0 = " + FmtCB(L, 0, 4) + "\nm_lightColor0 = " + FmtCB(L, 4, 4) + "\nm_lightColor1 = " + FmtCB(L, 8, 4) + "\nm_lightSourcePos = " + FmtCB(L, 12, 4) + "\nm_lightSourceColor = " + FmtCB(L, 16, 4) + "\nm_lightSourceDir = " + FmtCB(L, 20, 4) + "\nm_lightSourceParam1 = " + FmtCB(L, 24, 4) + "\nm_lightSourceParam2 = " + FmtCB(L, 28, 4) + "\nm_lightSourceParam3 = " + FmtCB(L, 32, 4) + "\nm_lightPos0 = " + FmtCB(L, 36, 4) + "\nm_lightParam2 = " + FmtCB(L, 40, 4) + "\n"; }
    if (NamedCBRead("SceneCB", S) && S.size() >= 120) { r += "\n[escena cPSScene b2]\nm_eyepos = " + FmtCB(S, 80, 4) + "\nm_projectionParam = " + FmtCB(S, 84, 4) + "\nm_viewportSize = " + FmtCB(S, 88, 4) + "\nm_exposure = " + FmtCB(S, 92, 4) + "\nm_fogParam = " + FmtCB(S, 96, 12) + "\nm_fogColor = " + FmtCB(S, 108, 4) + "\nm_shadowMapResolutions = " + FmtCB(S, 116, 4) + "\n"; }
    FILE* fp = _wfopen((dir + L"\\" + std::wstring(fn.begin(), fn.end())).c_str(), L"wb"); if (!fp) return false; fwrite(r.data(), 1, r.size(), fp); fclose(fp);
    Log("inspector: informe guardado en %s", outPath.c_str());
    return true;
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
      }
      { ImVec2 ts = ImGui::CalcTextSize("Escena (luz)"); ImVec2 ba(gx + 10, gy + 3), bb(gx + 10 + ts.x + 12, gy + 21);
        dl->AddRectFilled(ba, bb, s_sceneOpen ? IM_COL32(120, 90, 50, 255) : IM_COL32(45, 48, 58, 255), 3.0f);
        dl->AddText(ImVec2(ba.x + 6, ba.y + 1), IM_COL32(255, 255, 255, 255), "Escena (luz)");
        if (io.MouseClicked[0] && io.MousePos.x >= ba.x && io.MousePos.x <= bb.x && io.MousePos.y >= ba.y && io.MousePos.y <= bb.y) s_sceneOpen = !s_sceneOpen; } }
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
    // ---- selector por pixel: boton derecho = "que hay aca"; lista en orden de dibujo; clic en un nombre abre su ventana
    if (io.MouseClicked[1] && !ImGui::GetIO().WantCaptureMouse) { s_pickX = io.MousePos.x / sx; s_pickY = io.MousePos.y / sy; }
    if (s_pickX >= 0) {
        ImVec2 p(s_pickX * sx, s_pickY * sy);
        dl->AddCircle(p, 7, IM_COL32(255, 255, 255, 255), 12, 2.0f); dl->AddCircle(p, 9, IM_COL32(0, 0, 0, 255), 12, 1.0f);
        std::vector<int> under; for (int i = 0; i < (int)s_cls.size() && i < (int)s_classes.size(); i++) if (s_cls[i].bboxOk && s_cls[i].underPick) under.push_back(i);
        std::sort(under.begin(), under.end(), [](int a2, int b2) { return s_cls[a2].order < s_cls[b2].order; });
        float lx = p.x + 14, ly = p.y - 10; float lw = 300, lh = 22 + 18.0f * (float)(under.empty() ? 1 : under.size());
        if (lx + lw > io.DisplaySize.x) lx = p.x - 14 - lw;
        dl->AddRectFilled(ImVec2(lx, ly), ImVec2(lx + lw, ly + lh), IM_COL32(20, 22, 28, 230), 4.0f);
        dl->AddText(ImVec2(lx + 6, ly + 3), IM_COL32(255, 255, 255, 255), "En este pixel (orden de dibujo):");
        float ty = ly + 20;
        if (under.empty()) dl->AddText(ImVec2(lx + 6, ty), IM_COL32(180, 180, 180, 255), "nada conocido (fondo / sin clase)");
        for (int i : under) { char t[160]; snprintf(t, sizeof(t), "#%d  %s", s_cls[i].order, s_classes[i].name.c_str());
            bool hov = io.MousePos.x >= lx && io.MousePos.x <= lx + lw && io.MousePos.y >= ty && io.MousePos.y <= ty + 18;
            dl->AddText(ImVec2(lx + 6, ty), hov ? IM_COL32(255, 230, 120, 255) : IM_COL32(220, 220, 220, 255), t);
            if (hov && io.MouseClicked[0]) g_inspectSel = i;
            ty += 18; }
        if (io.MouseClicked[1] && ImGui::GetIO().WantCaptureMouse) {}
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) s_pickX = -1;
    }
    // ---- ventana "Escena": sol, luces, camara, niebla, exposicion (de los cbuffers copiados por regla)
    if (s_sceneOpen) {
        ImGui::SetNextWindowSize(ImVec2(520, 520), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(40, io.DisplaySize.y - 560), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Escena: luz y camara##escena", &s_sceneOpen)) {
            std::vector<float> L, S;
            bool lok = NamedCBRead("LightCB", L), sok = NamedCBRead("SceneCB", S);
            if (lok && L.size() >= 44) {
                float dx = L[0], dy = L[1], dz = L[2]; float len = sqrtf(dx * dx + dy * dy + dz * dz); if (len > 1e-4f) { dx /= len; dy /= len; dz /= len; }
                float elev = asinf(fmaxf(-1.f, fminf(1.f, dy))) * 57.2958f, azi = atan2f(dx, dz) * 57.2958f;
                ImGui::TextUnformatted("SOL / luz direccional (cPSLight b3)");
                ImGui::Text("direccion (hacia el sol): %.3f  %.3f  %.3f", dx, dy, dz);
                ImGui::Text("elevacion: %.1f grados   azimut: %.1f grados", elev, azi);
                ImGui::Text("color/intensidad (m_lightColor0): %.0f  %.0f  %.0f", L[4], L[5], L[6]);
                float mx = fmaxf(L[4], fmaxf(L[5], L[6])); if (mx > 1e-3f) ImGui::Text("   tinte: %.3f %.3f %.3f   (blanco = 1 1 1)", L[4] / mx, L[5] / mx, L[6] / mx);
                ImGui::Text("m_lightColor1 (ambiente/segunda): %.3f  %.3f  %.3f  %.3f", L[8], L[9], L[10], L[11]);
                ImGui::Text("m_lightSourcePos: %.1f  %.1f  %.1f  %.3g", L[12], L[13], L[14], L[15]);
                ImGui::Text("m_lightSourceColor: %.2f  %.2f  %.2f  %.2f", L[16], L[17], L[18], L[19]);
                ImGui::Text("m_lightSourceDir: %.3f  %.3f  %.3f  %.2f", L[20], L[21], L[22], L[23]);
                ImGui::Text("m_lightSourceParam1/2/3: %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f", L[24], L[25], L[26], L[27], L[28], L[29], L[30], L[31], L[32], L[33], L[34], L[35]);
                ImGui::Text("m_lightPos0: %.2f %.2f %.2f %.2f   m_lightParam2: %.3f %.3f %.3f %.3f", L[36], L[37], L[38], L[39], L[40], L[41], L[42], L[43]);
            } else ImGui::TextDisabled("LightCB todavia no copiado (hace falta un dibujo de la pasada de luz).");
            ImGui::Separator();
            if (sok && S.size() >= 120) {
                ImGui::TextUnformatted("CAMARA / ESCENA (cPSScene b2)");
                ImGui::Text("posicion de la camara: %.2f  %.2f  %.2f", S[80], S[81], S[82]);
                ImGui::Text("adelante (vista): %.3f  %.3f  %.3f   arriba: %.3f %.3f %.3f", S[40], S[41], S[42], S[36], S[37], S[38]);
                float fovy = 2.0f * atanf(1.0f / fmaxf(fabsf(S[21]), 1e-4f)) * 57.2958f; ImGui::Text("FOV vertical: %.1f grados   viewport: %.0fx%.0f   lejos: %.0f", fovy, S[88], S[89], S[91]);
                ImGui::Text("exposicion: %.3f %.3f %.3f %.3f", S[92], S[93], S[94], S[95]);
                ImGui::Text("niebla param: %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.3f %.3f %.3f %.0f", S[96], S[97], S[98], S[99], S[100], S[101], S[102], S[103], S[104], S[105], S[106], S[107]);
                ImGui::Text("color de niebla: %.2f %.2f %.2f %.2f", S[108], S[109], S[110], S[111]);
                ImGui::Text("shadow maps: %.0f %.0f %.0f %.0f", S[116], S[117], S[118], S[119]);
                ImGui::Text("cascadas: escala1 %.4f escala2 %.4f   escala prof. %.4f", S[64 + 3], S[64 + 7], S[64 + 12]);
            } else ImGui::TextDisabled("SceneCB todavia no copiado.");
            ImGui::Separator();
            ImGui::TextUnformatted("MOD (parametros de luz en vivo)");
            ImGui::Text("sol realista: color z4=%.2f intensidad w4=%.2f | sol de calidad: on x64=%.0f diametro y64=%.2f transmision z64=%.2f", g_cfg.live[4][2], g_cfg.live[4][3], g_cfg.live[64][0], g_cfg.live[64][1], g_cfg.live[64][2]);
            ImGui::Text("pupila (brillo adaptado x93): %.2f   en partido (w93): %.0f", g_cfg.live[93][0], g_cfg.live[93][3]);
            ImGui::Text("curva del dia 65/66/67: %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f", g_cfg.live[65][0], g_cfg.live[65][1], g_cfg.live[65][2], g_cfg.live[65][3], g_cfg.live[66][0], g_cfg.live[66][1], g_cfg.live[66][2], g_cfg.live[66][3], g_cfg.live[67][0], g_cfg.live[67][1], g_cfg.live[67][2], g_cfg.live[67][3]);
            ImGui::Text("sombras fisicas 87: %.2f %.2f %.2f %.2f   micro-sombras 88: %.2f %.2f", g_cfg.live[87][0], g_cfg.live[87][1], g_cfg.live[87][2], g_cfg.live[87][3], g_cfg.live[88][0], g_cfg.live[88][1]);
            ImGui::TextDisabled("(estos valores se editan en las pestanas Iluminacion / Sombras del menu)");
        }
        ImGui::End();
    }
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
              if (ImGui::RadioButton("todo", so < 0)) { s_solo = -1; s_soloAll = false; std::lock_guard<std::mutex> lk(s_layMx); LayersUpdateAny(); } ImGui::SameLine();
              if (ImGui::RadioButton("solo esta capa", so == sel && !s_soloAll.load())) { s_solo = sel; s_soloAll = false; std::lock_guard<std::mutex> lk(s_layMx); LayersUpdateAny(); } ImGui::SameLine();
              if (ImGui::RadioButton("SOLO ESTA (ocultar todo lo demas)", so == sel && s_soloAll.load())) { s_solo = sel; s_soloAll = true; std::lock_guard<std::mutex> lk(s_layMx); LayersUpdateAny(); }
              if (ImGui::IsItemHovered()) ImGui::SetTooltip("Oculta todas las demas partes del juego (conocidas o no): queda solo esta capa sobre el fondo. La luz y el post-proceso siguen.");
              ImGui::SameLine();
              bool browOnly = false; int pi = -1; for (int ci2 = 0; ci2 < (int)s_classes.size(); ci2++) for (uint64_t h : s_classes[ci2].ps) if (h == 0x0b3f9684ad026e54ULL) pi = ci2;
              if (pi >= 0) { std::lock_guard<std::mutex> lk(s_layMx); auto it = s_layers.find(0x0b3f9684ad026e54ULL); browOnly = it != s_layers.end() && it->second.hidden; }
              if (pi >= 0 && ImGui::Checkbox("cejas: solo hebras", &browOnly)) {
                  std::lock_guard<std::mutex> lk(s_layMx); for (uint64_t h : s_classes[pi].ps) s_layers[h].hidden = browOnly; LayersUpdateAny();
                  g_cfg.live[75][2] = browOnly ? 1.0f : 0.0f;   // hebras en magenta para verlas solas
              }
              if (ImGui::IsItemHovered()) ImGui::SetTooltip("Oculta las cejas/barba/pestanas pintadas del juego (capa alfa 0b3f/1fc2) y pinta las hebras de magenta."); }
            if (cl.alias) ImGui::Text("alias: se trata como %016llx  (dibujos redirigidos por cuadro: %d)", (unsigned long long)cl.alias, s_aliasShown);
            ImGui::TextDisabled("dibujos ocultados por cuadro: %d%s", s_skipsShown, s_skipsShown == 0 && (ov.hidden || s_solo.load() >= 0) ? "  (ninguno: esta capa no paso por el filtro)" : "");
            bool hidden = ov.hidden;
            if (ImGui::Checkbox("Ocultar esta capa (no dibujar esta parte)", &hidden)) { std::lock_guard<std::mutex> lk(s_layMx); for (uint64_t h : cl.ps) s_layers[h].hidden = hidden; LayersUpdateAny(); }
            bool hl = ov.highlight; ImGui::SameLine();
            if (ImGui::Checkbox("Resaltar en magenta", &hl)) { std::lock_guard<std::mutex> lk(s_layMx); for (uint64_t h : cl.ps) s_layers[h].highlight = hl; LayersUpdateAny(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dibuja esta capa con un shader magenta plano: lo que se pinte de magenta es exactamente lo que dibuja este shader.");
            ImGui::SameLine(); if (ImGui::Button("Guardar capas")) Log("capas: %s", SaveLayers() ? "guardadas en PES21FX_capas.ini" : "no se pudo guardar");
            ImGui::SameLine(); { static std::string rep; static double repT = 0; if (ImGui::Button("Guardar informe")) { std::string pth; rep = SaveReport(sel, pth) ? "Informe: " + pth : "No se pudo guardar el informe"; repT = Now(); }
              if (ImGui::IsItemHovered()) ImGui::SetTooltip("Escribe PES21FX\\inspector\\<clase>_<hash>.txt con todo lo recopilado: shaders, texturas (origen y formato), material, regla, parametros del mod, capas cambiadas y la luz de la escena.");
              if (!rep.empty() && Now() - repT < 6.0) { ImGui::SameLine(); ImGui::TextDisabled("%s", rep.c_str()); } }
            { bool dmp = s_dumpOn.load(); if (ImGui::Checkbox("Volcar texturas", &dmp)) s_dumpOn = dmp;
              if (ImGui::IsItemHovered()) ImGui::SetTooltip("Guarda como png/dds cada textura nueva que pase por las capas inspeccionadas (PES21FX\\inspector\\texturas\\<clase>\\). El historial de texturas (historial.txt) se escribe siempre con el inspector prendido.");
              ImGui::SameLine(); ImGui::TextDisabled("historial: %d texturas, volcadas: %d, en cola: %d", s_histCount, s_dumpCount, (int)s_dumpQueue.size()); }
            { bool flat = g_cfg.live[89][0] > 0.5f; if (ImGui::Checkbox("Luz plana", &flat)) g_cfg.live[89][0] = flat ? 1.0f : 0.0f;
              if (ImGui::IsItemHovered()) ImGui::SetTooltip("Apaga la iluminacion del sol (sin direccion, sombras ni brillos): se ven las texturas y materiales tal cual. Nivel y brillo en Iluminacion > Luz plana.");
              ImGui::SameLine(); ImGui::SetNextItemWidth(120); ImGui::SliderFloat("nivel##flat", &g_cfg.live[89][1], 0.2f, 3.0f, "%.2f"); }
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
            if (ImGui::BeginTabBar("##insp")) {
            if (ImGui::BeginTabItem("Capas")) {
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
            ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Material")) {
                ImGui::BeginChild("mat", ImVec2(0, 0));
                ImGui::TextUnformatted("cPSMaterial b4 -> m_materials[8] (lo que el juego le pasa a este shader: brillo, rugosidad, mezclas...)");
                if (f.matOk) { for (int r = 0; r < 8; r++) ImGui::Text("m_materials[%d] = %9.4f  %9.4f  %9.4f  %9.4f", r, f.mat[r * 4], f.mat[r * 4 + 1], f.mat[r * 4 + 2], f.mat[r * 4 + 3]); }
                else ImGui::TextDisabled("(sin cbuffer b4 en este dibujo)");
                ImGui::Separator();
                ImGui::TextUnformatted("cVSObject b5 -> m_world (posicion del objeto en el mundo) y m_useWeightCount");
                if (f.objOk) { ImGui::Text("posicion: %.2f  %.2f  %.2f   (m_world._m03/_m13/_m23)", f.obj[3], f.obj[7], f.obj[11]); for (int r = 0; r < 4; r++) ImGui::TextDisabled("  %8.3f %8.3f %8.3f %8.3f", f.obj[r * 4], f.obj[r * 4 + 1], f.obj[r * 4 + 2], f.obj[r * 4 + 3]); }
                else ImGui::TextDisabled("(sin cbuffer b5)");
                ImGui::Separator();
                ImGui::TextUnformatted("Texturas del vertex shader:");
                if (f.vtex.empty()) ImGui::TextDisabled("(ninguna)");
                for (auto& t : f.vtex) { if (t.srv) { ImGui::Image((ImTextureID)(uintptr_t)t.srv, ImVec2(40, 40)); ImGui::SameLine(); } ImGui::Text("vs t%d", t.slot); ImGui::SameLine(); ImGui::TextDisabled("%s", t.desc.c_str()); }
                ImGui::EndChild();
                ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Shader")) {
                ImGui::BeginChild("sh", ImVec2(0, 0));
                ImGui::Text("Orden en el cuadro: %d   dibujos de esta clase por cuadro: %d", f.order, f.drawsShown);
                ImGui::Text("Render targets: %d (%s)%s", f.nrt, f.rtDesc, f.depthOn ? ", con profundidad" : "");
                ImGui::Text("Contexto: %s   VS: %016llx", f.deferred ? "diferido" : "inmediato", (unsigned long long)f.vs);
                for (uint64_t h : cl.ps) {
                    const Rule* ru = RuleForHash(h);
                    wchar_t nm[64]; swprintf(nm, 64, L"shaders\\%016llx-ps_replace.txt", (unsigned long long)h);
                    bool repl = GetFileAttributesW((g_cfg.baseDir + nm).c_str()) != INVALID_FILE_ATTRIBUTES;
                    ImGui::Separator();
                    ImGui::Text("PS %016llx: %s", (unsigned long long)h, repl ? "REEMPLAZADO por el mod" : "del juego (sin reemplazo)");
                    if (ru) { ImGui::TextUnformatted("Regla (PES21FX_rules.ini):");
                        for (auto& a : ru->actions) {
                            const char* k = a.kind == RuleAction::CopyCB ? "copy_cb" : a.kind == RuleAction::RefSRV ? "ref_srv" : a.kind == RuleAction::BindCB ? "cb" : a.kind == RuleAction::BindSRV ? "srv" : a.kind == RuleAction::SetParam ? "param" : a.kind == RuleAction::EventTurf ? "event" : "facegen";
                            if (a.kind == RuleAction::FaceGen) ImGui::BulletText("facegen: quinta textura en t%d / t%d, mapa de pelo en t26, semillas u5/u6", a.slot, a.idx);
                            else if (a.kind == RuleAction::SetParam) ImGui::BulletText("param %d.%d = %s", a.idx, a.comp, a.var.empty() ? std::to_string(a.value).c_str() : a.var.c_str());
                            else ImGui::BulletText("%s %d %s", k, a.slot, a.name.c_str()); } }
                    else ImGui::TextDisabled("sin regla");
                    if (repl) { const std::vector<int>& pv = ParamsUsedBy(h);
                        ImGui::Text("Parametros del mod que lee (%d):", (int)pv.size());
                        for (int n : pv) ImGui::BulletText("%d: x=%.3f y=%.3f z=%.3f w=%.3f", n, g_cfg.live[n][0], g_cfg.live[n][1], g_cfg.live[n][2], g_cfg.live[n][3]); }
                }
                ImGui::EndChild();
                ImGui::EndTabItem(); }
            ImGui::EndTabBar(); }
        }
        ImGui::End();
        if (!open) g_inspectSel = -1;
    }
}

} // namespace fx
