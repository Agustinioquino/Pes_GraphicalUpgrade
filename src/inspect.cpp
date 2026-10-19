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
struct InspClass { std::vector<uint64_t> ps; std::string name, desc; std::map<int, std::string> slotNames; bool unknown = false; };
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
            else if (k.size() > 1 && k[0] == 't' && isdigit((unsigned char)k[1])) cur->slotNames[atoi(k.c_str() + 1)] = v;
        }
        Log("inspector: PES21FX_inspector.ini leido (%d clases)", (int)s_classes.size());
    }
    if (s_classes.empty()) for (auto& d : kDefaults) s_classes.push_back(d);
}
static const int kW = 320, kH = 180;

struct TexInfo { int slot; std::string desc; };
struct ClassFrame {
    bool seen = false; bool bboxOk = false; float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    UINT verts = 0; int kind = 0; bool deferred = false; uint64_t vs = 0;
    std::vector<TexInfo> tex;
    // recursos
    ID3D11Texture2D* rt = nullptr; ID3D11RenderTargetView* rtv = nullptr; ID3D11Texture2D* st[2] = {}; int stIdx = 0; bool stValid[2] = {};
};
static std::vector<ClassFrame> s_cls;   // una por clase (misma posicion que s_classes), mas las desconocidas al final
static std::vector<uint64_t> s_unkHash;   // hash de cada clase desconocida (indices >= s_classes.size())
static const int kMaxUnknown = 6;
static bool s_loaded = false;
static std::mutex s_mx;
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
    EnsureRes(); if (!s_whitePS) return false;
    int ci = -1;
    std::lock_guard<std::mutex> lk(s_mx);
    if (!s_loaded) { LoadClasses(); s_loaded = true; for (auto& f : s_cls) { if (f.rt) f.rt->Release(); if (f.rtv) f.rtv->Release(); for (auto* t : f.st) if (t) t->Release(); } s_cls.clear(); s_unkHash.clear(); s_cls.resize(s_classes.size()); }
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
            InspClass u; u.ps.push_back(ps->hash); u.name = std::string("Desconocido ") + nm; u.desc = "Shader sin clase: agregalo a PES21FX_inspector.ini ([seccion] ps = " + std::string(nm) + ", nombre = ..., que = ...)"; u.unknown = true;
            s_classes.push_back(u);
        }
    }
    ClassFrame& f = s_cls[ci];
    {
        if (f.seen) return false;       // una muestra por clase y cuadro (la primera)
        f.seen = true;
        if (!EnsureClassRT(f)) return false;
        f.verts = verts; f.kind = kind; f.deferred = c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED; f.vs = vs ? vs->hash : 0;
        f.tex.clear();
        ID3D11ShaderResourceView* srv[32] = {}; c->PSGetShaderResources(0, 32, srv);
        for (int i = 0; i < 32; i++) if (srv[i]) { std::string d = DescribeSRV(srv[i]); auto sn = s_classes[ci].slotNames.find(i); if (sn != s_classes[ci].slotNames.end()) d = sn->second + ": " + d; f.tex.push_back({ i, d }); srv[i]->Release(); }
    }
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
    std::lock_guard<std::mutex> lk(s_mx);
    for (size_t i = 0; i < s_cls.size(); i++) {
        ClassFrame& f = s_cls[i];
        if (!f.rt) continue;
        if (!g_inspectOn) { f.seen = false; f.bboxOk = false; continue; }
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
    dl->AddRectFilled(ImVec2(colX - 10, 40), ImVec2(io.DisplaySize.x - 10, 62), IM_COL32(20, 22, 28, 220), 4.0f);
    dl->AddText(ImVec2(colX, 44), IM_COL32(255, 255, 255, 255), "INSPECTOR VISUAL  (clic en una ficha = detalle; clases en PES21FX_inspector.ini; Inspector en el menu = apagar)");
    for (int i = 0; i < (int)s_cls.size() && i < (int)s_classes.size(); i++) {
        ClassFrame& f = s_cls[i];
        if (!f.bboxOk) continue;
        ImU32 col = cols[(k++) % ncol];
        ImVec2 a(f.x0 * sx, f.y0 * sy), b(f.x1 * sx, f.y1 * sy);
        bool isSel = sel == i;
        dl->AddRect(a, b, col, 3.0f, 0, isSel ? 3.0f : 1.5f);
        ImVec2 center((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        // ficha
        float h = isSel ? 26.0f + 16.0f * (2 + (float)f.tex.size()) : 26.0f;
        ImVec2 fa(colX, y), fb(io.DisplaySize.x - 10, y + h);
        dl->AddRectFilled(fa, fb, IM_COL32(20, 22, 28, 215), 4.0f);
        dl->AddRect(fa, fb, col, 4.0f, 0, isSel ? 2.0f : 1.0f);
        char t[200]; snprintf(t, sizeof(t), "%s   %u vert %s", s_classes[i].name.c_str(), f.verts, f.deferred ? "(dif)" : "(inm)");
        dl->AddText(ImVec2(fa.x + 8, fa.y + 5), col, t);
        if (isSel) {
            float ty = fa.y + 24;
            dl->AddText(nullptr, 0.0f, ImVec2(fa.x + 8, ty), IM_COL32(220, 220, 220, 255), s_classes[i].desc.c_str(), nullptr, fb.x - fa.x - 16); ty += 32;
            for (auto& tx : f.tex) { char l[260]; snprintf(l, sizeof(l), "t%-2d %s", tx.slot, tx.desc.c_str()); dl->AddText(ImVec2(fa.x + 8, ty), IM_COL32(200, 210, 230, 255), l); ty += 16; }
        }
        // flecha: de la ficha al centro del rectangulo
        ImVec2 from(fa.x, fa.y + 13);
        ImVec2 to = center;
        // punta en el borde del rectangulo mas cercano a la ficha
        if (from.x > b.x) to = ImVec2(b.x, center.y); else if (from.x < a.x) to = ImVec2(a.x, center.y);
        dl->AddLine(from, to, col, isSel ? 2.5f : 1.5f);
        ImVec2 dir(to.x - from.x, to.y - from.y); float len = sqrtf(dir.x * dir.x + dir.y * dir.y); if (len > 1) { dir.x /= len; dir.y /= len;
            ImVec2 n(-dir.y, dir.x); dl->AddTriangleFilled(to, ImVec2(to.x - dir.x * 12 + n.x * 6, to.y - dir.y * 12 + n.y * 6), ImVec2(to.x - dir.x * 12 - n.x * 6, to.y - dir.y * 12 - n.y * 6), col); }
        // clic en la ficha
        if (io.MouseClicked[0] && io.MousePos.x >= fa.x && io.MousePos.x <= fb.x && io.MousePos.y >= fa.y && io.MousePos.y <= fb.y) g_inspectSel = isSel ? -1 : i;
        y += h + 6;
    }
    if (k == 0) dl->AddText(ImVec2(colX, 70), IM_COL32(255, 220, 120, 255), "Sin dibujos conocidos en pantalla (abri un partido / repeticion).");
}

} // namespace fx
